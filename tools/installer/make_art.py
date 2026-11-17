"""Draws the Windows installer's pictures in dendrite's own look (true black, grey strands,
one point of light, the Urbanist wordmark) at every size Inno Setup picks from for high-DPI
screens, plus the setup icon. Deterministic: same files every run.

    python3 tools/installer/make_art.py            # writes packaging/windows/art/
"""
import math
import os
import random
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "packaging", "windows", "art")
FONT = os.path.join(ROOT, "Resources", "fonts", "Urbanist-Light.ttf")
MONO = os.path.join(ROOT, "Resources", "fonts", "FragmentMono-Regular.ttf")

BLACK = (0, 0, 0)
TISSUE = (118, 118, 118)
INK = (237, 237, 237)
FAINT = (87, 87, 87)
RED = (207, 58, 46)

SCALES = [1.0, 1.25, 1.5, 1.75, 2.0, 2.25, 2.5]


def branch(draw, rng, x, y, ang, length, width, depth, k, shade):
    """One dendrite: a wandering stem that forks, thinning as it goes."""
    steps = int(length / (3 * k)) + 1
    for i in range(steps):
        ang += rng.gauss(0, 0.16)
        nx, ny = x + math.cos(ang) * 3 * k, y + math.sin(ang) * 3 * k
        w = max(1, int(round(width * (1 - 0.6 * i / steps))))
        c = tuple(int(v * shade) for v in TISSUE)
        draw.line([(x, y), (nx, ny)], fill=c, width=w)
        x, y = nx, ny
        if depth > 0 and rng.random() < 0.035:
            branch(draw, rng, x, y, ang + rng.choice([-1, 1]) * rng.uniform(0.4, 0.9),
                   length * rng.uniform(0.3, 0.55), max(1, width * 0.7), depth - 1, k, shade * 0.9)
    if rng.random() < 0.5:
        draw.ellipse([x - 1.2 * k, y - 1.2 * k, x + 1.2 * k, y + 1.2 * k], fill=tuple(int(v * shade) for v in INK))


def glow(img, cx, cy, r, k):
    """The light: a soft halo with a bright core, added on top."""
    halo = Image.new("RGB", img.size, BLACK)
    d = ImageDraw.Draw(halo)
    for i in range(24, 0, -1):
        rr = r * i / 24 * 6
        v = int(255 * (1 - i / 24) ** 2.2 * 0.55)
        d.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], fill=(v, v, v))
    halo = halo.filter(ImageFilter.GaussianBlur(r * 1.2))
    # screen blend: 1 - (1-a)(1-b)
    res = ImageChops.screen(img, halo)
    d = ImageDraw.Draw(res)
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(255, 255, 255))
    return res


def wizard(scale):
    """The tall picture on the welcome and finish pages (164 x 314 at 100%)."""
    w, h = int(round(164 * scale)), int(round(314 * scale))
    k = scale
    img = Image.new("RGB", (w, h), BLACK)
    d = ImageDraw.Draw(img)
    rng = random.Random(7)
    cx, cy = w * 0.56, h * 0.36
    # strands at the edges, behind everything
    for _ in range(9):
        side = rng.choice(["l", "r", "t"])
        if side == "l":
            x, y, a = 0, rng.uniform(0, h * 0.8), rng.uniform(-0.4, 0.4)
        elif side == "r":
            x, y, a = w, rng.uniform(0, h * 0.8), math.pi + rng.uniform(-0.4, 0.4)
        else:
            x, y, a = rng.uniform(0, w), 0, math.pi / 2 + rng.uniform(-0.5, 0.5)
        branch(d, rng, x, y, a, rng.uniform(50, 120) * k, 1.4 * k, 3, k, 0.55)
    # the dendrites that grew around the light
    for i in range(7):
        a = i / 7 * 2 * math.pi + rng.uniform(-0.3, 0.3)
        branch(d, rng, cx, cy, a, rng.uniform(25, 60) * k, 1.6 * k, 3, k, 1.0)
    img = glow(img, cx, cy, 3.2 * k, k)
    d = ImageDraw.Draw(img)
    # the wordmark, low on the page, with the dot of its i as a small light
    font = ImageFont.truetype(FONT, int(30 * k))
    word = "dendrıte"
    tw = d.textlength(word, font=font)
    tx, ty = (w - tw) / 2, h - 74 * k
    d.text((tx, ty), word, font=font, fill=INK)
    ipos = tx + d.textlength("dendr", font=font) + d.textlength("ı", font=font) / 2
    small = ImageFont.truetype(MONO, int(8.5 * k))
    tag = "a dub echo that grows"
    d.text(((w - d.textlength(tag, font=small)) / 2, h - 32 * k), tag, font=small, fill=FAINT)
    img = glow_dot(img, ipos, ty + 8.0 * k, 1.9 * k)
    return img


def glow_dot(img, x, y, r):
    halo = Image.new("L", img.size, 0)
    ImageDraw.Draw(halo).ellipse([x - r * 4, y - r * 4, x + r * 4, y + r * 4], fill=110)
    halo = halo.filter(ImageFilter.GaussianBlur(r * 2))
    white = Image.new("RGB", img.size, (255, 255, 255))
    img = Image.composite(white, img, halo.point(lambda v: v))
    ImageDraw.Draw(img).ellipse([x - r, y - r, x + r, y + r], fill=(255, 255, 255))
    return img


def light_square(size, seed=3, strands=True):
    """The small picture (55 x 55 at 100%) and the icon: the light with its first dendrites."""
    k = size / 55.0
    img = Image.new("RGB", (size, size), BLACK)
    d = ImageDraw.Draw(img)
    rng = random.Random(seed)
    c = size / 2
    if strands:
        for i in range(6):
            a = i / 6 * 2 * math.pi + rng.uniform(-0.3, 0.3)
            branch(d, rng, c, c, a, rng.uniform(10, 18) * k, max(1, 1.2 * k), 2, k, 0.9)
    return glow(img, c, c, max(1.2, 2.6 * k), k)


def main():
    os.makedirs(OUT, exist_ok=True)
    for s in SCALES:
        pct = int(round(s * 100))
        wizard(s).save(os.path.join(OUT, f"wizard-{pct}.bmp"))
        light_square(int(round(55 * s))).save(os.path.join(OUT, f"small-{pct}.bmp"))
    # the icon: strands only where there is room for them
    sizes = [16, 24, 32, 48, 64, 128, 256]
    big = light_square(256, strands=True)
    frames = [light_square(n, strands=n >= 48) if n < 256 else big for n in sizes]
    frames[-1].save(os.path.join(OUT, "dendrite.ico"), sizes=[(n, n) for n in sizes],
                    append_images=frames[:-1])
    print("wrote", OUT)


if __name__ == "__main__":
    sys.exit(main())
