#!/usr/bin/env python3
"""Builds the teach page: page.html with rig.wasm, rig_core.js, wav.js and the built-in loops inlined.

    python3 tools/fly/teach/make_page.py build/teach/rig.wasm build/teach/teach.html
"""
import base64
import json
import pathlib
import sys

here = pathlib.Path(__file__).resolve().parent
root = here.parents[2]
# the loops the fly was tuned on (tools/fly/taste/loops): two bars at 127 bpm
BUILTIN = [
    {"name": "keys", "file": "fly-keys_127bpm.wav", "bpm": 127, "meter": "4"},
    {"name": "groove", "file": "fly-groove_127bpm.wav", "bpm": 127, "meter": "4"},
]


def main(wasm, out):
    page = (here / "page.html").read_text()
    core = (here / "rig_core.js").read_text()
    if "`" in core or "${" in core:
        sys.exit("rig_core.js must not use backticks: the page embeds it in a template string")
    pieces = []
    for p in BUILTIN:
        data = (root / "tools/fly/taste/loops" / p["file"]).read_bytes()
        pieces.append({**p, "b64": base64.b64encode(data).decode()})
    page = page.replace("/*__WAV_JS__*/", (here / "wav.js").read_text())
    page = page.replace("/*__RIG_CORE_SCRIPT__*/", core)
    page = page.replace("/*__RIG_CORE__*/", core)
    page = page.replace("__WASM_B64__", base64.b64encode(pathlib.Path(wasm).read_bytes()).decode())
    page = page.replace("__PIECES__", json.dumps(pieces))
    pathlib.Path(out).write_text(page)
    print(f"{out}: {len(page) / 1e6:.2f} MB")


if __name__ == "__main__":
    main(*sys.argv[1:3])
