"""Generates the crate window's effect textures and crate/key art as BLP files in Art/.

  python make_art.py

Effects are drawn procedurally; crate and key art is resized from the Esteria icon sources (never modified).
"""
import io
import math
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / "Tools" / "CosmeticsPack"))
from backpack_icons import blp_bytes  # noqa: E402  proven BLP2 BGRA writer for this client

ICONS = Path(r"R:\Users\Zach\Documents\GitHub\EsteriaWoW\NewModels\_Interface\_Icons")
# item entry -> source art; keep in sync with ART in EsteriaCrates.lua
ITEM_ART = {
    # transparent tier art for the store list and showcase (bag/tooltip icons keep the square framed version)
    3450101: ICONS / "Crates" / "UI_Icons_Transparent" / "03_Epic_Amethyst.png",
    3450001: ICONS / "Keys" / "Epic Violet Crate Key-20.png",
}


def white(size, alpha):
    image = Image.new("RGBA", size, (255, 255, 255, 0))
    image.putalpha(alpha)
    return image


def radial(size, power):
    mask = Image.new("L", (size, size))
    px = mask.load()
    c = (size - 1) / 2
    for y in range(size):
        for x in range(size):
            d = min(1.0, math.hypot(x - c, y - c) / c)
            px[x, y] = int(255 * (1 - d) ** power)
    return mask


def rays(size, count):
    mask = Image.new("L", (size, size))
    draw = ImageDraw.Draw(mask)
    c = size / 2
    for i in range(count):
        a = 2 * math.pi * i / count
        w = 0.07 if i % 2 else 0.035
        pts = [(c, c), (c + c * 1.5 * math.cos(a - w), c + c * 1.5 * math.sin(a - w)),
               (c + c * 1.5 * math.cos(a + w), c + c * 1.5 * math.sin(a + w))]
        draw.polygon(pts, fill=255 if i % 2 else 170)
    mask = mask.filter(ImageFilter.GaussianBlur(size / 90))
    return ImageChops.multiply(mask, radial(size, 1.2))


def spark(size):
    mask = Image.new("L", (size, size))
    draw = ImageDraw.Draw(mask)
    c = size / 2
    for a in (0, math.pi / 2):
        for s in (1, -1):
            dx, dy = math.cos(a) * s, math.sin(a) * s
            draw.polygon([(c + dx * c, c + dy * c), (c - dy * 3, c + dx * 3), (c + dy * 3, c - dx * 3)], fill=255)
    mask = mask.filter(ImageFilter.GaussianBlur(1.2))
    return ImageChops.lighter(mask, radial(size, 3))


def shine(width, height):
    mask = Image.new("L", (width, height))
    px = mask.load()
    for y in range(height):
        for x in range(width):
            d = abs((x - width / 2) - (y - height / 2) * 0.6) / (width * 0.18)
            px[x, y] = int(200 * max(0.0, 1 - d) ** 2)
    return mask


def shadow(size, blur):
    mask = Image.new("L", (size, size))
    ImageDraw.Draw(mask).rectangle([blur * 2, blur * 2, size - blur * 2 - 1, size - blur * 2 - 1], fill=255)
    return mask.filter(ImageFilter.GaussianBlur(blur))


def main():
    out = HERE / "Art"
    out.mkdir(exist_ok=True)
    images = {
        "glow": white((128, 128), radial(128, 2.2)),
        "rays": white((256, 256), rays(256, 24)),
        "spark": white((64, 64), spark(64)),
        "dot": white((32, 32), radial(32, 1.6)),
        "shine": white((128, 64), shine(128, 64)),
        "shadow": Image.merge("RGBA", [Image.new("L", (128, 128))] * 3 + [shadow(128, 12)]),
    }
    for entry, source in ITEM_ART.items():
        art = Image.open(source).convert("RGBA").resize((256, 256), Image.Resampling.LANCZOS)
        images[f"item-{entry}"] = art
    for name, image in images.items():
        png = io.BytesIO()
        image.save(png, "PNG")
        png.seek(0)
        (out / f"{name}.blp").write_bytes(blp_bytes(png))  # it opens a path or file object
        print(name, image.size)


if __name__ == "__main__":
    main()
