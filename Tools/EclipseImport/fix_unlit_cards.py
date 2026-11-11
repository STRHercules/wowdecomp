"""Light the fur/feather cards of Eclipse's converted creatures.

Eclipse's converter marks a model's alpha-blended skin cards (fur manes, feather fringes) unlit (material flag 0x1).
Unlit cards ignore scene lighting and show as flat, bright triangles over the lit body. This clears the flag on
alpha-blended (blend 2) materials unless the batch's texture is an effect (glow/fire/aura/eye/fx) that is meant to
glow, and writes the patched models to one archive that loads after the Eclipse mount/incarnation archives.

    python fix_unlit_cards.py --out "G:/3.3.5a - Dev/Data/patch-EclipseMountsFix.MPQ"
"""
from __future__ import annotations

import argparse
import json
import re
import struct
from pathlib import Path

import eclipse_data as ed
import eclipse_install as ei

SOURCES = {"mounts": ed.ECLIPSE / "packs/mounteclipse.mpq", "incarnations": ed.ECLIPSE / "packs/incarneclipse.mpq"}
EFFECT = re.compile(r"glow|aura|fire|flame|eye|fx", re.I)


def is_effect(texture: str) -> bool:
    return bool(EFFECT.search(texture)) and "borderfeathers" not in texture.lower()


def fix(model: bytes, skin: bytes) -> bytes | None:
    """The model with its card materials lit, or None when nothing needs changing."""
    m = bytearray(model)
    arr = lambda d, off: struct.unpack_from("<II", d, off)
    n, o = arr(m, 0x70)
    materials = [struct.unpack_from("<2H", m, o + i * 4) for i in range(n)]
    n, t = arr(m, 0x50)
    textures = []
    for i in range(n):
        kind, _, length, name = struct.unpack_from("<4I", m, t + i * 16)
        textures.append(m[name:name + length - 1].decode("latin1") if length else f"<type {kind}>")
    n, lo = arr(m, 0x80)
    lookup = [struct.unpack_from("<H", m, lo + i * 2)[0] for i in range(n)]
    n, bo = arr(skin, 0x24)
    lit, effect = set(), set()
    for i in range(n):
        batch = struct.unpack_from("<BbH10H", skin, bo + i * 24)
        material, combo = batch[6], batch[9]
        flags, blend = materials[material]
        if blend != 2 or not flags & 1:
            continue
        texture = textures[lookup[combo]] if combo < len(lookup) and lookup[combo] < len(textures) else ""
        (effect if is_effect(texture) else lit).add(material)
    lit -= effect  # a material shared with a glow keeps glowing
    for material in lit:
        flags, _ = materials[material]
        struct.pack_into("<H", m, o + material * 4, flags & ~1)
    return bytes(m) if lit else None


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    models = {ed.norm(json.loads(p.read_text())["model"]) for p in Path("G:/EclipseImport/mounts/packs").glob("*/resolved-source.json")}
    models |= {ed.norm(line) for line in Path("G:/EclipseImport/incarnations/assets.txt").read_text(encoding="ascii").splitlines()
               if line.endswith(".m2")}
    storm = ed.storm()
    patched = {}
    for kind, source in SOURCES.items():
        handle = storm.open_archive(source)
        names = {ed.norm(n) for n in ed.list_files(storm, handle)}
        for model in sorted(models):
            if model in names and model[:-3] + "00.skin" in names and model not in patched:
                fixed = fix(storm.read(handle, model), storm.read(handle, model[:-3] + "00.skin"))
                if fixed:
                    patched[model] = fixed
        storm.dll.SFileCloseArchive(handle)
    if args.out.exists():
        args.out.unlink()
    ei.pack_assets(storm, [], [], args.out, patched)
    print(f"{len(patched)} models patched -> {args.out}")
