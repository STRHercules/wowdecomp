"""In-memory feasibility probe: what does the retail->legacy WMO transform need for one room piece? Writes nothing."""
import sys, os
sys.path.insert(0, os.path.expanduser(r"~/.claude/skills/wow-retroport/scripts/bundle/vendor"))
from wotlkconv.wmo.convert import inspect_wmo_root, convert_wmo_root
from wotlkconv.wmo.group import inspect_group, convert_group_parts
from wotlkconv.options import Options
raw = sys.argv[1]
root = open(os.path.join(raw, "6431804.bin"), "rb").read()
grp = open(os.path.join(raw, "6432330.bin"), "rb").read()
print("root:", inspect_wmo_root(root, "6431804.wmo"))
print("group:", inspect_group(grp, "6432330.wmo"))
o = Options()
out, res, _ = convert_wmo_root(root, "6431804.wmo", o)
print("root ->", len(root), "->", len(out), res.status, [str(m) for m in getattr(res, 'messages', [])][:12])
parts, gres = convert_group_parts(grp, "6432330.wmo", o)
print("group ->", len(grp), "->", [len(p.data) for p in parts], gres.status, [str(m) for m in getattr(gres, 'messages', [])][:12])
import dataclasses
for r in (res, gres):
    d = dataclasses.asdict(r) if dataclasses.is_dataclass(r) else vars(r)
    print({k: v for k, v in d.items() if v not in (None, [], {}, 0, '')})
