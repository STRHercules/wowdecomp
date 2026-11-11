"""Files the incarnation picker's looks need that no Esteria archive and the incarnation pack do not supply.

Reused displays (Esteria already had the display row) were not shipped with art, but some point at models Esteria
never had (the Zandalari guardian bear). This finds them and writes their Eclipse files to
G:/EclipseImport/incarnations/assets_extra.txt for the incarnation archive.
"""
import json
from pathlib import Path

import eclipse_data as ed

STAGE = Path("G:/EclipseImport/incarnations")
DATA = Path(__file__).resolve().parents[2] / "Addons/EsteriaIncarnations/EsteriaIncarnationsData.lua"

models = set()
for line in DATA.read_text(encoding="utf-8").splitlines():
    line = line.strip()
    if line.startswith("{ ") and line.endswith("},") and '", "' in line:
        models.add(json.loads(line[line.index('", "') + 3:line.rindex('"') + 1]))
archives = ed.Archives()
shipped = {ed.norm(n) for n in (STAGE / "assets.txt").read_text(encoding="ascii").splitlines() if n}
missing = sorted(m for m in models if ed.norm(m) not in shipped and not archives.has(m))
print(len(models), "models;", len(missing), "missing:", [m.rsplit("\\", 1)[-1] for m in missing])
storm = ed.storm()
handle = storm.open_archive(ed.ECLIPSE / "packs/incarneclipse.mpq")
eclipse = {ed.norm(n) for n in ed.list_files(storm, handle)}
extra = set()
for model in missing:
    key = ed.norm(model)
    if key not in eclipse:
        print("  not in Eclipse either:", model)
        continue
    data = storm.read(handle, key)
    folder = key.rsplit("\\", 1)[0]
    files = {key} | set(ed.m2_skins(model, data)) | {ed.norm(t) for t in ed.m2_textures(data)}
    files |= set(ed.m2_external_anims(model, data))
    files |= {f for f in eclipse if f.startswith(folder + "\\") and f.endswith(".blp")}  # display skins sit beside it
    extra |= {f for f in files if f in eclipse and not archives.has(f) and f not in shipped}
archives.close()
(STAGE / "assets_extra.txt").write_text("\n".join(sorted(extra)) + "\n", encoding="ascii")
print(len(extra), "extra files")
