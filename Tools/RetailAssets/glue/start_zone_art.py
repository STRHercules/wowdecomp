"""Starting-zone art (start_zones.png) -> the creator's DXT5 BLP2 texture.

start_zones.png is a 2048x1024 atlas of 4x3 slots of 512x341, cut from the retail character-create sheets
(interface/glues/charactercreate/charactercreatestartingzones1..5.blp, FileDataIDs 3552990, 3553247, 3553268,
3553272, 3553331). Each sheet is 2048x2048 with 1024x666 tiles in two columns. Rebuild it from sheets extracted with

    RetailAssetProbe <RetailCasc.ini> retail/<FileDataID>.blp --extract <dir>/<FileDataID>.blp
    python start_zone_art.py <dir>

start_zone_page.lua reads slot i (column i % 4, row i // 4) in TILES order.
"""

import io
import struct
import sys
from pathlib import Path

from PIL import Image

SOURCE = Path(__file__).with_name("start_zones.png")
ENTRY = "Interface" + chr(92) + "Glues" + chr(92) + "CharacterCreate" + chr(92) + "EsteriaStartZones.blp"
SLOT = (512, 341)
# (sheet FileDataID, tile index on the sheet), in slot order: Northshire, Coldridge, Shadowglen, Ammen Vale,
# Valley of Trials, Camp Narache, Deathknell, Sunstrider Isle.
TILES = ((3553247, 4), (3553247, 0), (3553268, 4), (3552990, 5), (3553268, 5), (3553272, 1), (3553272, 3), (3552990, 0))


def compose(sheets):
    atlas = Image.new("RGBA", (2048, 1024), (0, 0, 0, 255))
    for slot, (sheet, tile) in enumerate(TILES):
        image = Image.open(Path(sheets) / (str(sheet) + ".blp")).convert("RGBA")
        box = ((tile % 2) * 1024, (tile // 2) * 666)
        piece = image.crop((box[0], box[1], box[0] + 1024, box[1] + 666)).resize(SLOT, Image.LANCZOS)
        atlas.paste(piece, ((slot % 4) * SLOT[0], (slot // 4) * SLOT[1]))
    atlas.save(SOURCE, optimize=True)


def dxt5(level):
    # Mips under 4px are padded to one block; the client reads the top-left texels.
    padded = Image.new("RGBA", (max(4, level.width), max(4, level.height)))
    padded.paste(level)
    out = io.BytesIO()
    padded.save(out, format="DDS", pixel_format="DXT5")
    return out.getvalue()[128:]


def build():
    level = Image.open(SOURCE).convert("RGBA")
    width, height = level.size
    blp = bytearray(1172)
    blp[0:4] = b"BLP2"
    struct.pack_into("<I", blp, 4, 1)
    blp[8:12] = bytes((2, 8, 7, 1))  # DXT, 8-bit alpha, DXT5, mips present
    struct.pack_into("<II", blp, 12, width, height)
    for mip in range(16):
        data = dxt5(level)
        struct.pack_into("<I", blp, 20 + mip * 4, len(blp))
        struct.pack_into("<I", blp, 84 + mip * 4, len(data))
        blp += data
        if level.size == (1, 1):
            break
        level = level.resize((max(1, level.width // 2), max(1, level.height // 2)), Image.BOX)
    return bytes(blp)


if __name__ == "__main__":
    if len(sys.argv) > 1:
        compose(sys.argv[1])
    print(len(build()), "bytes")
