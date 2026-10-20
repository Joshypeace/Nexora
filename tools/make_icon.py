"""Generates Omnipane's logo assets: resources/logo.png and resources/omnipane.ico.

The mark is a glowing particle orb (magenta -> indigo -> cyan) on a dark rounded tile.
Small icon sizes get a simplified drawing (no particles, thicker strokes) so they stay
legible at 16-32 px.

Usage: python tools/make_icon.py
Requires Pillow.
"""
import math
import random
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parent.parent
RES = ROOT / "resources"

MAGENTA = (236, 112, 255)
INDIGO = (112, 92, 255)
CYAN = (64, 234, 255)


def lerp(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))


def palette(t):
    """Colour along the orb's diagonal: magenta (lower left) to cyan (upper right)."""
    t = min(max(t, 0.0), 1.0)
    return lerp(MAGENTA, INDIGO, t / 0.5) if t < 0.5 else lerp(INDIGO, CYAN, (t - 0.5) / 0.5)


def gradient_layer(s, cx, cy, radius):
    """Colour field along the orb's lower-left -> upper-right diagonal, spanning the orb itself
    so its rim reaches pure magenta and pure cyan."""
    img = Image.new("RGB", (s, s))
    px = img.load()
    for y in range(s):
        v = ((cy + radius) - y) / (2 * radius)
        for x in range(s):
            u = (x - (cx - radius)) / (2 * radius)
            # The rim only spans roughly 0.15..0.85 of the diagonal; stretch it to the full palette.
            px[x, y] = palette(((u + v) / 2 - 0.15) / 0.7)
    return img


def tinted(mask, colours, alpha=1.0):
    """RGBA layer: `colours` shown through `mask` (L), scaled by `alpha`."""
    layer = colours.convert("RGBA")
    layer.putalpha(mask.point(lambda v: round(v * alpha)))
    return layer


def render(size, simple):
    ss = 4  # supersampling for smooth edges
    s = size * ss
    cx = cy = s / 2
    radius = s * (0.34 if simple else 0.33)
    colours = gradient_layer(s, cx, cy, radius)

    # Dark rounded tile with a faint vertical gradient.
    tile = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    tile_mask = Image.new("L", (s, s), 0)
    ImageDraw.Draw(tile_mask).rounded_rectangle((0, 0, s - 1, s - 1), radius=round(s * 0.23), fill=255)
    base = Image.new("RGB", (s, s))
    bpx = base.load()
    for y in range(s):
        c = lerp((14, 14, 40), (5, 6, 18), y / (s - 1))
        for x in range(s):
            bpx[x, y] = c
    tile.paste(base, (0, 0), tile_mask)
    canvas = tile

    # Sphere body: deep indigo core with a hint of the palette.
    body = Image.new("L", (s, s), 0)
    ImageDraw.Draw(body).ellipse((cx - radius, cy - radius, cx + radius, cy + radius), fill=255)
    canvas = Image.alpha_composite(canvas, tinted(body, Image.new("RGB", (s, s), (20, 14, 64)), 0.9))
    canvas = Image.alpha_composite(canvas, tinted(body, colours, 0.10))

    # Rim: soft outer glow plus a crisp gradient ring.
    ring_width = s * (0.055 if simple else 0.03)
    ring = Image.new("L", (s, s), 0)
    d = ImageDraw.Draw(ring)
    d.ellipse((cx - radius, cy - radius, cx + radius, cy + radius), outline=255, width=round(ring_width))
    glow = ring.filter(ImageFilter.GaussianBlur(s * 0.045))
    canvas = Image.alpha_composite(canvas, tinted(glow, colours, 0.95))
    canvas = Image.alpha_composite(canvas, tinted(ring, colours, 1.0))

    # Flowing wave lines across the sphere, clipped to it.
    waves = Image.new("L", (s, s), 0)
    wd = ImageDraw.Draw(waves)
    rng = random.Random(7)
    # Three wispy filaments with different rhythms, so they read as flowing strands, not a net.
    wave_specs = [(-0.30, 0.14, 1.15, 0.4), (0.02, 0.22, 0.85, 2.6), (0.34, 0.12, 1.4, 4.4)]
    if simple:
        wave_specs = wave_specs[1:2]
    wave_points = []
    for offset, amplitude, frequency, phase in wave_specs:
        pts = []
        steps = 120
        for i in range(steps + 1):
            u = -1.1 + 2.2 * i / steps
            v = offset + amplitude * math.sin(frequency * math.pi * u + phase)
            pts.append((cx + u * radius, cy + v * radius))
        wd.line(pts, fill=255, width=round(s * (0.03 if simple else 0.009)), joint="curve")
        wave_points.extend(pts)
    inner = Image.new("L", (s, s), 0)
    shrink = ring_width * 0.6
    ImageDraw.Draw(inner).ellipse((cx - radius + shrink, cy - radius + shrink,
                                   cx + radius - shrink, cy + radius - shrink), fill=255)
    waves = ImageChops.multiply(waves, inner)
    canvas = Image.alpha_composite(canvas, tinted(waves.filter(ImageFilter.GaussianBlur(s * 0.012)), colours, 0.8))
    canvas = Image.alpha_composite(canvas, tinted(waves, colours, 0.75 if simple else 0.55))

    # Particle texture (large sizes only): denser towards the rim and along the waves.
    if not simple:
        dots = Image.new("L", (s, s), 0)
        dd = ImageDraw.Draw(dots)
        dot = s * 0.0045
        for _ in range(1400):
            angle = rng.uniform(0, 2 * math.pi)
            r = radius * (rng.random() ** 0.35)
            x, y = cx + r * math.cos(angle), cy + r * math.sin(angle)
            a = round(255 * (0.25 + 0.75 * (r / radius) ** 3))
            dd.ellipse((x - dot, y - dot, x + dot, y + dot), fill=a)
        for x, y in wave_points[::2]:
            jx, jy = rng.gauss(0, s * 0.008), rng.gauss(0, s * 0.008)
            dd.ellipse((x + jx - dot, y + jy - dot, x + jx + dot, y + jy + dot), fill=230)
        dots = ImageChops.multiply(dots, body)
        canvas = Image.alpha_composite(canvas, tinted(dots, colours, 0.9))

    # Specular highlight on the upper right of the rim.
    spec = Image.new("L", (s, s), 0)
    ImageDraw.Draw(spec).arc((cx - radius, cy - radius, cx + radius, cy + radius), start=-70, end=-20,
                             fill=255, width=round(ring_width * 0.6))
    canvas = Image.alpha_composite(canvas, tinted(spec.filter(ImageFilter.GaussianBlur(s * 0.006)),
                                                  Image.new("RGB", (s, s), (235, 255, 255)), 0.55))

    # Keep everything inside the rounded tile.
    canvas.putalpha(ImageChops.multiply(canvas.getchannel("A"), tile_mask))
    return canvas.resize((size, size), Image.LANCZOS)


def main():
    RES.mkdir(exist_ok=True)
    large = render(256, simple=False)
    large.save(RES / "logo.png")
    sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
    images = [render(n, simple=n <= 32) if n < 256 else large for n in sizes]
    images[-1].save(RES / "omnipane.ico", sizes=[(n, n) for n in sizes], append_images=images[:-1])
    print("wrote", RES / "logo.png", "and", RES / "omnipane.ico")


if __name__ == "__main__":
    main()
