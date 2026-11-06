import sys, json, os
sys.path.insert(0, r"R:\Users\Zach\Documents\GitHub\wowdecomp\Tools\MountStudio\runtime\reader")
from wotlkconv.db import db2, dbd
S = sys.argv[1]
idx = dbd.DbdIndex(os.path.join(S, "dbd"))
out = {}
for f in sorted(os.listdir(os.path.join(S, "db2"))):
    n = f[:-4]
    t = db2.parse_db2(open(os.path.join(S, "db2", f), "rb").read(), f, idx, n)
    out[n] = {str(k): v for k, v in t}
    print(n, hex(t.layout_hash), len(out[n]), "named" if getattr(t, "named_columns", True) else "")
json.dump(out, open(os.path.join(S, "tables.json"), "w"), indent=0, default=str)
