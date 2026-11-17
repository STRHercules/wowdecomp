"""Converts the Adventure Finder art in wowdecomp/Assets into BLPs under Art/.

  python make_art.py
"""
import io
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
ASSETS = HERE.parents[1] / "Assets"
sys.path.insert(0, str(HERE.parents[1] / "Tools" / "CosmeticsPack"))
from backpack_icons import blp_bytes  # noqa: E402  the BLP2 BGRA writer the other Esteria art uses

# output name -> (source, size): power-of-two sizes for the client
ART = {
    "background": (ASSETS / "GroupFinderBackground.png", 512),
    "tank": (ASSETS / "TANK.png", 64),
    "healer": (ASSETS / "HEALS.png", 64),
    "damager": (ASSETS / "DPS.png", 64),
    "guide": (ASSETS / "Full" / "GUIDE.png", 64),
}


def main():
    out = HERE / "Art"
    out.mkdir(exist_ok=True)
    for name, (source, size) in ART.items():
        image = Image.open(source).convert("RGBA").resize((size, size), Image.Resampling.LANCZOS)
        png = io.BytesIO()
        image.save(png, "PNG")
        png.seek(0)
        (out / f"{name}.blp").write_bytes(blp_bytes(png))
        print(name, size)


if __name__ == "__main__":
    main()
