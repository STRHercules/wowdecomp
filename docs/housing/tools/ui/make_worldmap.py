"""Turn a retail UiMap's art into a 3.3.5 world map: Interface\\WorldMap\\<name>\\<name>1..12.blp.

    python -I make_worldmap.py <UiMapArtTile.csv> <UiMapArtID> <name> <out dir> <tile cache dir>

UiMapArtTile.csv is a wago.tools export (https://wago.tools/db2/UiMapArtTile/csv). The art's layer-0 tiles (256 px,
by FileDataID) come from wago.tools' CASC download and are kept in the cache dir. 3.3.5 draws a zone map as 4 x 3
tiles of which the top-left 1002 x 668 (in 256-unit tiles) shows; retail's art has the same 3:2 shape, so it is scaled to
that and cut up. Tiles are 1024 px DXT5 BLP2 with mips, as this install's HD world maps (patch-M.mpq) are: the client
did not draw palettised 256 px tiles without mips.
"""
import csv
import io
import os
import struct
import sys
import urllib.request

from PIL import Image

UNIT, TILE = 4, 1024  # 1024 px per 256-unit tile
SHOWN, CANVAS = (1002 * UNIT, 668 * UNIT), (4 * TILE, 3 * TILE)
RETAIL_TILE = 256


def dxt(level, kind):
    padded = Image.new("RGBA", (max(4, level.width), max(4, level.height)))
    padded.paste(level)
    out = io.BytesIO()
    padded.save(out, format="DDS", pixel_format=kind)
    return out.getvalue()[128:]


def blp(image, opaque=False):
    """BLP2 with every mip down to 1 x 1: DXT5, or DXT1 for an opaque image (as stock loading screens are)."""
    level = image.convert("RGBA")
    kind = "DXT1" if opaque else "DXT5"
    out = bytearray(1172)
    out[0:4] = b"BLP2"
    struct.pack_into("<I", out, 4, 1)
    out[8:12] = bytes((2, 0, 0, 1)) if opaque else bytes((2, 8, 7, 1))  # DXT; alpha bits; DXT1 / DXT5; mips present
    struct.pack_into("<II", out, 12, level.width, level.height)
    for mip in range(16):
        data = dxt(level, kind)
        struct.pack_into("<I", out, 20 + mip * 4, len(out))
        struct.pack_into("<I", out, 84 + mip * 4, len(data))
        out += data
        if level.size == (1, 1):
            break
        level = level.resize((max(1, level.width // 2), max(1, level.height // 2)), Image.BOX)
    return bytes(out)


def fetch(fdid, cache):
    return fetch_image(fdid, cache).convert("RGB")


def fetch_image(fdid, cache):
    """A retail BLP by FileDataID (wago.tools' CASC download), kept in the cache dir."""
    path = os.path.join(cache, f"{fdid}.blp")
    if not os.path.exists(path):
        request = urllib.request.Request(f"https://wago.tools/api/casc/{fdid}?download", headers={"User-Agent": "Mozilla/5.0"})
        data = urllib.request.urlopen(request, timeout=60).read()
        assert data[:4] == b"BLP2", f"{fdid}: not a BLP"
        open(path, "wb").write(data)
    return Image.open(path)


def main(tiles_csv, art_id, name, out, cache):
    os.makedirs(out, exist_ok=True)
    os.makedirs(cache, exist_ok=True)
    tiles = [r for r in csv.DictReader(open(tiles_csv, encoding="utf-8")) if r["UiMapArtID"] == art_id and r["LayerIndex"] == "0"]
    assert tiles, f"no layer-0 tiles for UiMapArt {art_id}"
    cols = 1 + max(int(r["ColIndex"]) for r in tiles)
    rows = 1 + max(int(r["RowIndex"]) for r in tiles)
    art = Image.new("RGB", (cols * RETAIL_TILE, rows * RETAIL_TILE))
    for r in tiles:
        art.paste(fetch(int(r["FileDataID"]), cache), (int(r["ColIndex"]) * RETAIL_TILE, int(r["RowIndex"]) * RETAIL_TILE))
    canvas = Image.new("RGB", CANVAS)
    canvas.paste(art.resize(SHOWN, Image.LANCZOS), (0, 0))
    for i in range(12):
        x, y = (i % 4) * TILE, (i // 4) * TILE
        open(os.path.join(out, f"{name}{i + 1}.blp"), "wb").write(blp(canvas.crop((x, y, x + TILE, y + TILE))))
    print(f"{len(tiles)} tiles ({cols} x {rows}) of UiMapArt {art_id} -> {out}\\{name}1..12.blp")


if __name__ == "__main__":
    main(*sys.argv[1:6])
