"""Turn a retail loading screen into 3.3.5's pair: <name>.blp (4:3) and <name>Wide.blp (16:10).

    python -I make_loadscreen.py <MainImageFileDataID> <name> <out dir> <cache dir>

The image is LoadingScreens.MainImageFileDataID (a wide painting letterboxed in black; the logo retail draws over it is
left out). The black bars are trimmed first. 3.3.5
stretches the whole texture over a 4:3 view, or 16:10 when widescreen, so each is cut from the middle at that shape
and stored as this install's HD loading screens are (patch-M.mpq): 2048 x 2048 and 4096 x 2048, DXT1 with mips.
"""
import os
import sys

from PIL import Image

from make_worldmap import blp, fetch_image

SHAPES = {"": (4 / 3, (2048, 2048)), "Wide": (16 / 10, (4096, 2048))}


def main(fdid, name, out, cache):
    os.makedirs(out, exist_ok=True)
    os.makedirs(cache, exist_ok=True)
    image = fetch_image(int(fdid), cache).convert("RGBA")
    # the painting, without its bars: they are black, or transparent over any colour
    opaque = image.getchannel("A").point(lambda a: 255 if a > 128 else 0)
    coverage = Image.composite(image.convert("L"), Image.new("L", image.size, 0), opaque)
    image = image.convert("RGB").crop(coverage.point(lambda v: 255 if v > 12 else 0).getbbox())
    for suffix, (aspect, size) in SHAPES.items():
        width = min(image.width, round(image.height * aspect))
        height = min(image.height, round(width / aspect))
        left, top = (image.width - width) // 2, (image.height - height) // 2
        cut = image.crop((left, top, left + width, top + height)).resize(size, Image.LANCZOS)
        open(os.path.join(out, f"{name}{suffix}.blp"), "wb").write(blp(cut, opaque=True))
    print(f"{fdid} ({image.width} x {image.height}) -> {out}\\{name}.blp, {name}Wide.blp")


if __name__ == "__main__":
    main(*sys.argv[1:5])
