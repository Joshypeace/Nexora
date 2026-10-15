"""Generates Nexora's logo assets (resources/logo.png and resources/nexora.ico).

Usage: python tools/make_icon.py
Requires Pillow.
"""
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
RES = ROOT / "resources"

VIOLET = (139, 92, 246)
BLUE = (59, 130, 246)
CYAN = (34, 211, 238)


def lerp(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))


def gradient(size):
    img = Image.new("RGB", (size, size))
    px = img.load()
    for y in range(size):
        for x in range(size):
            t = (x + y) / (2 * (size - 1))
            px[x, y] = lerp(VIOLET, BLUE, t / 0.55) if t < 0.55 else lerp(BLUE, CYAN, (t - 0.55) / 0.45)
    return img


def render(size):
    ss = 4  # supersampling factor for smooth edges
    s = size * ss
    tile = gradient(s).convert("RGBA")

    mask = Image.new("L", (s, s), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, s - 1, s - 1), radius=round(s * 0.26), fill=255)
    tile.putalpha(mask)

    # Soft top highlight for a glassy feel.
    glow = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    ImageDraw.Draw(glow).ellipse((-s * 0.3, -s * 0.75, s * 1.3, s * 0.45), fill=(255, 255, 255, 46))
    glow.putalpha(Image.composite(glow.getchannel("A"), Image.new("L", (s, s), 0), mask))
    tile = Image.alpha_composite(tile, glow)

    # Stylised "N": two stems and a diagonal with round caps.
    d = ImageDraw.Draw(tile)
    w = round(s * 0.115)
    left, right = s * 0.31, s * 0.69
    top, bottom = s * 0.30, s * 0.70
    for a, b in (((left, bottom), (left, top)), ((left, top), (right, bottom)), ((right, bottom), (right, top))):
        d.line((a, b), fill=(255, 255, 255, 255), width=w)
    for x, y in ((left, bottom), (left, top), (right, bottom), (right, top)):
        r = w / 2
        d.ellipse((x - r, y - r, x + r, y + r), fill=(255, 255, 255, 255))

    return tile.resize((size, size), Image.LANCZOS)


def main():
    RES.mkdir(exist_ok=True)
    render(256).save(RES / "logo.png")
    sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
    images = [render(n) for n in sizes]
    images[-1].save(RES / "nexora.ico", sizes=[(n, n) for n in sizes], append_images=images[:-1])
    print("wrote", RES / "logo.png", "and", RES / "nexora.ico")


if __name__ == "__main__":
    main()
