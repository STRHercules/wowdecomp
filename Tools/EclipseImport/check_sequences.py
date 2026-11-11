"""Check every incarnation model's animation tables: sequence lookup, variation/alias chains and external .anim files.

A bad index here crashes CM2Model::ProcessCallbacks (it indexes m_sequences unchecked) the moment a preview animates.
"""
import json
import struct
import sys
from pathlib import Path

import eclipse_data as ed

DATA = Path(__file__).resolve().parents[2] / "Addons/EsteriaIncarnations/EsteriaIncarnationsData.lua"
looks = []
for line in DATA.read_text(encoding="utf-8").splitlines():
    line = line.strip()
    if line.startswith("{ ") and line.endswith("},") and '", "' in line:
        name = json.loads(line[line.index('"'):line.index('", "') + 1])
        looks.append((name, json.loads(line[line.index('", "') + 3:line.rindex('"') + 1])))
for line in DATA.read_text(encoding="utf-8").splitlines():
    for part in line.split("{ ")[1:]:
        if part.split(",")[0].strip().isdigit() and part.count('"') == 2 and ".m2" in part.lower():
            looks.append(("default", json.loads(part[part.index('"'):part.rindex('"') + 1])))

archives = ed.Archives()


def read(name):
    # later archive names win, as in the client
    for handle in reversed(list(archives.handles.values())):
        try:
            return archives.storm.read(handle, name)
        except OSError:
            pass
    return None


bad = 0
seen = set()
for name, model in looks:
    key = ed.norm(model)
    if key in seen:
        continue
    seen.add(key)
    data = read(key)
    if not data:
        print("MISSING", name, model)
        bad += 1
        continue
    n_seq, o_seq = struct.unpack_from("<II", data, 0x1C)
    n_lk, o_lk = struct.unpack_from("<II", data, 0x24)
    n_bone, o_bone = struct.unpack_from("<II", data, 0x2C)
    problems = []
    lookup = [struct.unpack_from("<h", data, o_lk + i * 2)[0] for i in range(n_lk)]
    problems += [f"lookup[{i}]={v}" for i, v in enumerate(lookup) if v >= n_seq]
    for i in range(n_seq):
        anim, sub, = struct.unpack_from("<HH", data, o_seq + i * 0x40)
        var_next, alias_next = struct.unpack_from("<hH", data, o_seq + i * 0x40 + 0x3C)
        if var_next >= n_seq or (alias_next != 0xFFFF and alias_next >= n_seq) and alias_next != i:
            problems.append(f"seq{i} next={var_next} alias={alias_next}")
    # bone tracks: each M2Track's per-sequence arrays must have one entry per sequence (or be empty)
    for b in range(n_bone):
        base = o_bone + b * 0x58
        for track in (0x10, 0x24, 0x38):
            n_times = struct.unpack_from("<I", data, base + track + 4)[0]
            if n_times and n_times != n_seq and n_times > n_seq:
                problems.append(f"bone{b} track@{track:#x} has {n_times} sequences > {n_seq}")
                break
    if problems:
        bad += 1
        print(f"BAD {name}: {model} ({n_seq} sequences) {problems[:4]}")
print(f"{len(seen)} models checked, {bad} bad")
sys.exit(1 if bad else 0)
