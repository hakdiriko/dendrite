"""Draws the Windows installer's pictures in the rig's look (3.0, "the preparation"): black, greys,
Fragment Mono, the male fly's 13,065 neurons at their real atlas places (Source/flies/malecns),
a 200 um scale bar. Every size Inno Setup picks from for high-DPI screens, plus the setup icon.
Deterministic: same files every run.

    python3 tools/installer/make_art.py            # writes packaging/windows/art/
"""
import math
import os
import re
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "packaging", "windows", "art")
MONO = os.path.join(ROOT, "Resources", "fonts", "FragmentMono-Regular.ttf")
FLY = os.path.join(ROOT, "Source", "flies", "malecns", "FlyData.cpp")

# the rig's tones (Source/ui/Rig.h): live, bright, mid, dim, floor
def grey(level):
    v = int(round(255 * level))
    return (v, v, v)

LIVE, BRIGHT, MID, DIM, FLOOR = 1.0, 0.72, 0.48, 0.28, 0.12
BLACK = (0, 0, 0)
WIDTH_UM, HEIGHT_UM = 754.0, 496.0  # the male brain's frontal box (the window's header says the same)
SCALES = [1.0, 1.25, 1.5, 1.75, 2.0, 2.25, 2.5]


def neurons():
    text = open(FLY, encoding="utf-8").read()

    def array(name):
        m = re.search(r"const uint16_t " + name + r"\[(\d+)\] = \{(.*?)\};", text, re.S)
        return [int(v) for v in m.group(2).replace("\n", " ").split(",") if v.strip()]

    xs, ys = array("neuronX"), array("neuronY")
    assert len(xs) == len(ys) == 13065, len(xs)
    return list(zip(xs, ys))


def brain(points, width_px, ss=3):
    """The specimen at rest: every neuron a point where the atlas puts it, overlapping points brighter."""
    w, h = int(round(width_px * ss)), int(round(width_px * HEIGHT_UM / WIDTH_UM * ss))
    acc = np.zeros((h, w), np.float32)
    for x, y in points:
        acc[int(y / 65535 * (h - 1)), int(x / 65535 * (w - 1))] += 1.0
    # density to light: the floor where a single neuron sits, up to the mid step where they crowd
    k = 1.0 / ss  # per output pixel
    blur = Image.fromarray(acc).resize((max(1, w // ss), max(1, h // ss)), Image.BOX)
    dens = np.asarray(blur, np.float32) * ss * ss
    lum = np.where(dens > 0.05 * k, 255 * (FLOOR + (MID - FLOOR) * (1 - np.exp(-dens * 0.5))), 0)
    return Image.fromarray(np.clip(lum, 0, 255).astype(np.uint8), "L")


def paste_light(img, lum, x, y):
    img.paste(Image.new("RGB", lum.size, (255, 255, 255)), (int(x), int(y)), lum)


def wizard(points, s):
    """The tall picture on the welcome and finish pages (164 x 314 at 100%)."""
    w, h = int(round(164 * s)), int(round(314 * s))
    img = Image.new("RGB", (w, h), BLACK)
    d = ImageDraw.Draw(img)
    word = ImageFont.truetype(MONO, int(round(14 * s)))
    small = ImageFont.truetype(MONO, int(round(8 * s)))
    m = 14 * s
    d.text((m, m), "dendrite", font=word, fill=grey(LIVE))
    for i, line in enumerate(["drosophila melanogaster", "adult male", "male cns v1.0", "13,065 neurons"]):
        d.text((m, m + 26 * s + i * 12 * s), line, font=small, fill=grey(DIM))
    # the brain, wider than the panel: its middle, where the central brain is, shows
    bw = w * 1.7
    lum = brain(points, bw)
    paste_light(img, lum, (w - lum.width) / 2, h * 0.60 - lum.height / 2)
    # the scale bar, as on the window: 200 um at this magnification
    bar = bw / WIDTH_UM * 200
    y = h - m - 4 * s
    d.line([(m, y), (m + bar, y)], fill=grey(BRIGHT), width=max(1, int(s)))
    for x in (m, m + bar):
        d.line([(x, y - 3 * s), (x, y + 3 * s)], fill=grey(BRIGHT), width=max(1, int(s)))
    d.text((m + bar + 6 * s, y - 6 * s), "200 µm", font=small, fill=grey(MID))
    return img


def square(points, size, margin=0.06):
    """The small picture (55 x 55 at 100%) and the icon: the whole brain, frontal."""
    img = Image.new("RGB", (size, size), BLACK)
    lum = brain(points, size * (1 - 2 * margin), ss=max(3, int(256 / size)))
    if size <= 32:  # at icon sizes the floor step vanishes; lift it so the shape reads
        lum = lum.point(lambda v: min(255, int(v * 1.9)))
    paste_light(img, lum, (size - lum.width) / 2, (size - lum.height) / 2)
    return img


def main():
    os.makedirs(OUT, exist_ok=True)
    points = neurons()
    for s in SCALES:
        pct = int(round(s * 100))
        wizard(points, s).save(os.path.join(OUT, f"wizard-{pct}.bmp"))
        square(points, int(round(55 * s))).save(os.path.join(OUT, f"small-{pct}.bmp"))
    sizes = [16, 24, 32, 48, 64, 128, 256]
    frames = [square(points, n) for n in sizes]
    frames[-1].save(os.path.join(OUT, "dendrite.ico"), sizes=[(n, n) for n in sizes], append_images=frames[:-1])
    print("wrote", OUT)


if __name__ == "__main__":
    sys.exit(main())
