"""Rebuild the animation lookup of Eclipse's models where the 3.3.5 client cannot find an animation through it.

CM2Model::SetBoneSequence finds a sequence by id through M2Data::m_sequenceIdxHashById: slot = id % count, then
quadratic probing (slot + step^2) until the entry's sequence has that id or the entry is 0xFFFF. Eclipse's converter
wrote some tables another way, so an animation the model has is "not found" and the client indexes sequences[0xFFFF]
(crash, felstalker_pet). This rebuilds the table the client's way, in place (same size, so no offsets move), for every
model we ship, and writes the patched models into the same fix archive as fix_unlit_cards.py.

    python fix_sequence_lookup.py --check        list broken models
    python fix_sequence_lookup.py --out <MPQ>    build the fix archive (unlit-card fix + lookup fix)
"""
import argparse
import struct
from pathlib import Path

import eclipse_data as ed
import eclipse_install as ei
import fix_unlit_cards as unlit

OURS = ("patch-EclipseMounts.MPQ", "patch-EclipseIncarnations.MPQ", "patch-EclipseIncarnationsExtra.MPQ",
        "patch-EclipseHearths.MPQ", "patch-EclipseItems.MPQ")


def sequences(data):
    count, offset = struct.unpack_from("<II", data, 0x1C)
    return [(struct.unpack_from("<H", data, offset + i * 0x40)[0], struct.unpack_from("<H", data, offset + i * 0x40 + 2)[0])
            for i in range(count)]


def lookup(data):
    count, offset = struct.unpack_from("<II", data, 0x24)
    return offset, [struct.unpack_from("<H", data, offset + i * 2)[0] for i in range(count)]


def find(table, seqs, anim):
    """The client's probe (M2Model.cpp SetBoneSequence); the index found, or None."""
    if not table:
        return next((i for i, s in enumerate(seqs) if s[0] == anim), None)
    slot = anim % len(table)
    index = table[slot]
    for step in range(1, len(table) + 1):
        if index == 0xFFFF:
            return None
        if index < len(seqs) and seqs[index][0] == anim:
            return index
        slot = (slot + step * step) % len(table)
        index = table[slot]
    return None


def broken(data):
    seqs = sequences(data)
    _, table = lookup(data)
    return [anim for anim in sorted({s[0] for s in seqs}) if find(table, seqs, anim) is None]


def normalize_variations(data):
    """Renumber each animation's variations 0..n in file order and chain them (variationNext) within that animation
    only. Eclipse's converter chained some across animations (felhound2: Stand was variation 1 of Walk)."""
    out = bytearray(data)
    count, offset = struct.unpack_from("<II", out, 0x1C)
    groups = {}
    for index in range(count):
        groups.setdefault(struct.unpack_from("<H", out, offset + index * 0x40)[0], []).append(index)
    for members in groups.values():
        for k, index in enumerate(members):
            base = offset + index * 0x40
            struct.pack_into("<H", out, base + 2, k)
            struct.pack_into("<h", out, base + 0x3C, members[k + 1] if k + 1 < len(members) else -1)
    return bytes(out)


def rebuild(data):
    """The model with its lookup rebuilt the client's way: in place when the table is big enough, else a larger table
    appended to the file with the header pointed at it (no other offset moves). Variation chains are normalised
    first."""
    data = normalize_variations(data)
    offset, table = lookup(data)
    fixed = _rebuild(data, offset, len(table))
    if fixed:
        return fixed
    ids = len({s[0] for s in sequences(data)})
    size = max(len(table), 2 * ids + 1)
    while size < 4096:
        out = bytearray(data)
        while len(out) % 16:
            out.append(0)
        new_offset = len(out)
        out += bytes(size * 2)
        struct.pack_into("<II", out, 0x24, size, new_offset)
        fixed = _rebuild(bytes(out), new_offset, size)
        if fixed:
            return fixed
        size = size * 2 + 1
    return None


def _rebuild(data, offset, size):
    seqs = sequences(data)
    if not size:
        return None
    new = [0xFFFF] * size
    for index, (anim, sub) in enumerate(seqs):
        if find(new, seqs, anim) is not None:
            continue                      # the table names the first sequence of each animation (its variation chain follows)
        slot, step = anim % size, 1
        while new[slot] != 0xFFFF and step <= size:
            slot = (slot + step * step) % size
            step += 1
        if new[slot] != 0xFFFF:
            return None
        new[slot] = index
    out = bytearray(data)
    struct.pack_into(f"<{size}H", out, offset, *new)
    return bytes(out) if not broken(bytes(out)) else None


def shipped_models():
    archives = ed.Archives()
    names = {}
    for path, handle in archives.handles.items():
        if path.name in OURS:
            for name in archives.listed(path):
                if ed.norm(name).endswith(".m2"):
                    names.setdefault(ed.norm(name), handle)
    return archives, names


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    archives, models = shipped_models()
    patched, failed = {}, []
    for name, handle in sorted(models.items()):
        data = archives.storm.read(handle, name)
        missing = broken(data)
        skin_name = name[:-3] + "00.skin"
        fixed = None
        if missing:
            fixed = rebuild(data)
            if fixed is None:
                failed.append(name)
            print(f"{name.rsplit(chr(92), 1)[-1]}: {len(missing)} animations unreachable {missing[:8]}"
                  f"{'' if fixed else ' (NOT FIXABLE in place)'}")
        if args.out:
            try:
                skin = archives.storm.read(handle, skin_name)
            except OSError:
                skin = None
            # fur-card lighting stays limited to creatures (mounts, incarnations), as fix_unlit_cards.py does
            creature = name.startswith("creature\\")
            lit = unlit.fix(fixed or data, skin) if skin and creature else None
            if lit or fixed:
                patched[name] = lit or fixed
    print(f"{len(models)} shipped models; {sum(1 for _ in patched)} patched; {len(failed)} not fixable")
    if args.out:
        storm = archives.storm
        if args.out.exists():
            args.out.unlink()
        archives.close()
        ei.pack_assets(ed.storm(), [], [], args.out, patched)
        print("wrote", args.out)
    else:
        archives.close()
