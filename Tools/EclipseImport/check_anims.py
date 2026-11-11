"""Models whose external animations are missing or whose bone tracks are short.

A sequence without flag 0x20 keeps its keys in <model><animId>-<subId>.anim; if that file is absent the client has no
data for it and CM2Model::SetBoneSequence reads garbage when the sequence plays (crash). A bone track with fewer
per-sequence entries than the model has sequences is read out of range the same way.

    python check_anims.py [--write]   (--write: G:/EclipseImport/broken_anims.json for fix_missing_anims.py)
"""
import json
import struct
import sys
from pathlib import Path

import eclipse_data as ed

archives = ed.Archives()


def read(name):
    for handle in reversed(list(archives.handles.values())):
        try:
            return archives.storm.read(handle, name)
        except OSError:
            pass
    return None


models = {ed.norm(json.loads(p.read_text())["model"]) for p in Path("G:/EclipseImport/mounts/packs").glob("*/resolved-source.json")}
data_lua = Path(__file__).resolve().parents[2] / "Addons/EsteriaIncarnations/EsteriaIncarnationsData.lua"
for line in data_lua.read_text(encoding="utf-8").splitlines():
    for part in line.split('"')[1::2]:
        if part.lower().endswith(".m2"):
            models.add(ed.norm(part.replace("\\\\", "\\")))

report = {}
for model in sorted(models):
    data = read(model)
    if not data:
        continue
    n_seq, o_seq = struct.unpack_from("<II", data, 0x1C)
    missing = []
    for i in range(n_seq):
        anim, sub = struct.unpack_from("<HH", data, o_seq + i * 0x40)
        flags = struct.unpack_from("<I", data, o_seq + i * 0x40 + 0x0C)[0]
        if not flags & 0x60:
            name = f"{model[:-3]}{anim:04d}-{sub:02d}.anim"
            if not archives.has(name):
                missing.append((i, anim))
    n_bone, o_bone = struct.unpack_from("<II", data, 0x2C)
    short = 0
    for b in range(n_bone):
        for track in (0x10, 0x24, 0x38):
            n_times = struct.unpack_from("<I", data, o_bone + b * 0x58 + track + 4)[0]
            if 0 < n_times < n_seq:
                short += 1
    if missing or short:
        report[model] = {"missing": missing, "short_tracks": short, "sequences": n_seq}
        stand = any(anim == 0 for _, anim in missing)
        print(f"{model.rsplit(chr(92), 1)[-1]}: {len(missing)}/{n_seq} sequences without .anim"
              f"{' (incl. Stand)' if stand else ''}; {short} short tracks")
archives.close()
print(len(report), "models with problems")
if "--write" in sys.argv:
    Path("G:/EclipseImport/broken_anims.json").write_text(json.dumps(report, indent=1), encoding="utf-8")
