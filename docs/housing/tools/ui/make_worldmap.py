"""Turn a retail UiMap's art into a 3.3.5 world map: Interface\\WorldMap\\<name>\\<name>1..12.blp.

    python -I make_worldmap.py <UiMapArtTile.csv> <UiMapArtID> <name> <out dir> <tile cache dir>

UiMapArtTile.csv is a wago.tools export (https://wago.tools/db2/UiMapArtTile/csv). The art's layer-0 tiles (256 px,
by FileDataID) come from wago.tools' CASC download and are kept in the cache dir. 3.3.5 draws a zone map as 4 x 3
tiles of 256 px of which the top-left 1002 x 668 shows; retail's art has the same 3:2 shape, so it is scaled to that and
cut up. The tiles are palettised BLP2, which the 3.3.5 client reads.
"""
import csv
import os
import sys
import urllib.request

from PIL import Image

SHOWN, CANVAS, TILE = (1002, 668), (1024, 768), 256


def fetch(fdid, cache):
    path = os.path.join(cache, f"{fdid}.blp")
    if not os.path.exists(path):
        request = urllib.request.Request(f"https://wago.tools/api/casc/{fdid}?download", headers={"User-Agent": "Mozilla/5.0"})
        data = urllib.request.urlopen(request, timeout=60).read()
        assert data[:4] == b"BLP2", f"{fdid}: not a BLP"
        open(path, "wb").write(data)
    return Image.open(path).convert("RGB")


def main(tiles_csv, art_id, name, out, cache):
    os.makedirs(out, exist_ok=True)
    os.makedirs(cache, exist_ok=True)
    tiles = [r for r in csv.DictReader(open(tiles_csv, encoding="utf-8")) if r["UiMapArtID"] == art_id and r["LayerIndex"] == "0"]
    assert tiles, f"no layer-0 tiles for UiMapArt {art_id}"
    cols = 1 + max(int(r["ColIndex"]) for r in tiles)
    rows = 1 + max(int(r["RowIndex"]) for r in tiles)
    art = Image.new("RGB", (cols * TILE, rows * TILE))
    for r in tiles:
        art.paste(fetch(int(r["FileDataID"]), cache), (int(r["ColIndex"]) * TILE, int(r["RowIndex"]) * TILE))
    canvas = Image.new("RGB", CANVAS)
    canvas.paste(art.resize(SHOWN, Image.LANCZOS), (0, 0))
    for i in range(12):
        x, y = (i % 4) * TILE, (i // 4) * TILE
        part = canvas.crop((x, y, x + TILE, y + TILE)).quantize(256, method=Image.Quantize.MEDIANCUT)
        part.save(os.path.join(out, f"{name}{i + 1}.blp"), format="BLP", blp_version="BLP2")
    print(f"{len(tiles)} tiles ({cols} x {rows}) of UiMapArt {art_id} -> {out}\\{name}1..12.blp")


if __name__ == "__main__":
    main(*sys.argv[1:6])
