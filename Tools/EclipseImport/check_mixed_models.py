"""Find models drawn from a mix of Esteria and Eclipse files.

Where Esteria already had a model (.m2) Esteria's copy wins, but an Eclipse-only companion file of that same model
(<model>NNNN-NN.anim, <model>NN.skin) could still have been shipped at a path Esteria lacked. The client then pairs
Esteria's .m2 with Eclipse's animation/skin bytes, whose offsets do not match: a crash in CM2Model::ProcessCallbacks.
Prints the shipped companions to remove (from our archives) for every such model.
"""
import re
import sys
from pathlib import Path

import eclipse_data as ed

OURS = ("patch-EclipseMounts.MPQ", "patch-EclipseIncarnations.MPQ", "patch-EclipseIncarnationsExtra.MPQ",
        "patch-EclipseHearths.MPQ", "patch-EclipseItems.MPQ")
SOURCES = [ed.ECLIPSE / "packs" / n for n in ("mounteclipse.mpq", "incarneclipse.mpq", "heartheclipse.mpq", "eclipse.mpq")]

archives = ed.Archives()
ours = {p: h for p, h in archives.handles.items() if p.name in OURS}
theirs = {p: h for p, h in archives.handles.items() if p.name not in OURS and p.name != "patch-EclipseMountsFix.MPQ"}
shipped = {}
for path, handle in ours.items():
    for name in archives.listed(path):
        shipped.setdefault(ed.norm(name), path.name)
companion = re.compile(r"^(.*?)(\d{4}-\d{2}\.anim|\d{2}\.skin)$")
by_model = {}
for name in shipped:
    match = companion.match(name)
    if match:
        by_model.setdefault(match.group(1) + ".m2", []).append(name)


def esteria_has(name):
    if name in archives.names() and not shipped.get(name):
        return True
    key = name.encode("ascii", "replace")
    import ctypes
    for handle in theirs.values():
        file_handle = ed.cmp.H()
        if archives.storm.dll.SFileOpenFileEx(handle, key, 0, ctypes.byref(file_handle)):
            archives.storm.dll.SFileCloseFile(file_handle)
            return True
    return False


mixed = {}
for model, files in sorted(by_model.items()):
    if model in shipped:
        continue                      # we shipped the model too: the set is Eclipse's own
    if esteria_has(model):
        mixed[model] = files
archives.close()
for model, files in mixed.items():
    print(model, "->", len(files), "Eclipse companions:", ", ".join(f.rsplit("\\", 1)[-1] for f in files[:6]))
print(len(mixed), "mixed models,", sum(len(f) for f in mixed.values()), "files")
Path("G:/EclipseImport/mixed_companions.txt").write_text("".join(f"{shipped[f]}\t{f}\n" for fs in mixed.values() for f in fs),
                                                         encoding="ascii")
sys.exit(0)
