#!/usr/bin/env python3
"""The fly's teaching data from the teach page (phase 2): progress, and replays.

A take is one JSON document ("dendrite-teach/1"), as the page saves it to its store or puts in
its download ({"takes": [...]}). Its events are "sample action value..." strings, each applied
at the start of a 128-sample block, counted from the take's first sample:
    controls feed space time | play 0/1 | cue <move 1..7> | throw 0/1 | freeze 0/1
and its starts are [beat, move] for each move that started (the fly's body snapped it to the grid).
Moves: 1 hop, 2 leap, 3 duck, 4 dive, 5 rush, 6 hold, 7 sink.

    python3 tools/fly/teach/teach.py stats takes.json   (or a folder of take files)
        progress against the plan's data target (10 pieces x 2 takes x 32 bars, 300 moves)
    python3 tools/fly/teach/teach.py replay takes.json <take id or index> loop.wav out.wav
        renders the take again through the native rig and checks every move started on the
        beat the page logged (needs build/teach_replay, see replay.cpp)
"""
import hashlib
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import wave

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parents[3]
MOVES = ["none", "hop", "leap", "duck", "dive", "rush", "hold", "sink"]


def load_takes(path):
    """A download ({"takes": [...]}), one take, or a folder of takes (the store read with out_dir)."""
    path = pathlib.Path(path)
    if path.is_dir():
        return sorted((json.loads(f.read_text()) for f in path.rglob("*.json")), key=lambda t: t["startedAt"])
    data = json.loads(path.read_text())
    if isinstance(data, list):
        return data
    return data.get("takes", [data])


def stats(takes):
    full = [t for t in takes if t["bars"] >= 32]
    per_piece = {}
    for t in full:
        key = (t["piece"]["name"], t["piece"]["sha256"])
        per_piece[key] = per_piece.get(key, 0) + 1
    moves = sum(t["moves"] for t in takes)
    kinds = {}
    for t in takes:
        for _, m in t["starts"]:
            kinds[MOVES[m]] = kinds.get(MOVES[m], 0) + 1
    done = sum(1 for n in per_piece.values() if n >= 2)
    print(f"takes {len(takes)}, of 32 bars or more {len(full)} (target 20)")
    print(f"pieces with two full takes {done} (target 10): " +
          ", ".join(f"{k[0]} x{n}" for k, n in sorted(per_piece.items())))
    print(f"moves {moves} (target 300): " + ", ".join(f"{k} {n}" for k, n in sorted(kinds.items(), key=lambda x: -x[1])))
    bars = sum(t["bars"] for t in takes)
    print(f"bars played {bars:.0f}, moves per bar {moves / max(bars, 1):.2f}")
    reached = done >= 10 and len(full) >= 20 and moves >= 300
    print("data target: " + ("reached" if reached else "not yet"))
    return reached


def read_wav(path):
    with wave.open(str(path)) as w:
        sr, ch, width, n = w.getframerate(), w.getnchannels(), w.getsampwidth(), w.getnframes()
        raw = w.readframes(n)
    if width == 3:
        b = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        x = (b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16))
        x = np.where(x >= 1 << 23, x - (1 << 24), x).astype(np.float32) / 8388608.0
    elif width == 2:
        x = np.frombuffer(raw, dtype="<i2").astype(np.float32) / 32768.0
    elif width == 4:
        x = np.frombuffer(raw, dtype="<i4").astype(np.float32) / 2147483648.0
    else:
        sys.exit(f"{path}: {8 * width}-bit WAV not supported here")
    x = x.reshape(-1, ch)
    left = x[:, 0]
    right = x[:, 1] if ch > 1 else left
    return sr, left.astype(np.float32), right.astype(np.float32)


def write_wav(path, sr, stereo):
    pcm = (np.clip(stereo, -1, 1) * 32767).astype("<i2")
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(pcm.tobytes())


def replay(takes, which, loop_wav, out_wav, binary=ROOT / "build/teach_replay"):
    take = next((t for t in takes if t["id"] == which), None)
    if take is None:
        take = takes[int(which)]
    piece = take["piece"]
    digest = hashlib.sha256(pathlib.Path(loop_wav).read_bytes()).hexdigest()
    if digest != piece["sha256"]:
        print(f"warning: {loop_wav} is not the file the take was played on (sha256 differs)")
    sr, left, right = read_wav(loop_wav)
    if sr != piece["sampleRate"] or piece.get("resampled"):
        print("warning: the page played this loop resampled; the replay will not be sample-exact")
    frames = take["frames"] + 2 * sr  # and the echo's tail
    with tempfile.TemporaryDirectory() as d:
        d = pathlib.Path(d)
        np.concatenate([left, right]).astype("<f4").tofile(d / "loop.f32")
        (d / "events.txt").write_text("\n".join(take["events"]) + "\n")
        printed = subprocess.run([str(binary), str(sr), str(piece["bpm"]), str(piece["barBeats"]), str(frames),
                                  str(d / "loop.f32"), str(d / "events.txt"), str(d / "out.f32")],
                                 check=True, capture_output=True, text=True).stdout
        out = np.fromfile(d / "out.f32", dtype="<f4").reshape(-1, 2)
    write_wav(out_wav, sr, out)
    native = [(round(float(b), 4), MOVES.index(m)) for _, b, m in (line.split() for line in printed.split("\n") if line)]
    logged = [(round(float(b), 4), int(m)) for b, m in take["starts"]]
    same = native == logged
    print(f"{out_wav}: {frames / sr:.1f} s, {len(native)} moves, same beats as the page: {same}")
    if not same:
        print("page:  ", logged)
        print("native:", native)
    return same


def main(argv):
    if len(argv) >= 2 and argv[0] == "stats":
        stats(load_takes(argv[1]))
    elif len(argv) >= 5 and argv[0] == "replay":
        sys.exit(0 if replay(load_takes(argv[1]), argv[2], argv[3], argv[4]) else 1)
    else:
        print(__doc__)
        sys.exit(2)


if __name__ == "__main__":
    main(sys.argv[1:])
