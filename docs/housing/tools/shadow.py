"""Which Patch-Housing.MPQ files also exist in other archives (read-only hash lookups)."""
import sys, os, glob, json, collections
sys.path.insert(0, r"R:\Users\Zach\Documents\GitHub\wowdecomp\Tools\VulkanShaders\bls")
from mpq_extract import MPQ
here = os.path.dirname(os.path.abspath(__file__))
L = [l.strip().replace('/', '\\') for l in open(os.path.join(here, "listfile.txt"), encoding="utf-8", errors="replace") if l.strip()]
data = r"G:\3.3.5a - Dev\Data"
names = sorted({os.path.basename(p) for p in glob.glob(os.path.join(data, "*.[mM][pP][qQ]"))}, key=str.lower, reverse=True)
names = [n for n in names if n.lower() != "patch-housing.mpq"]
hits = collections.defaultdict(list)
higher = [n for n in names if n.lower() > "patch-housing.mpq"]
for n in names:
    try: m = MPQ(os.path.join(data, n))
    except Exception as e: print("skip", n, e); continue
    for p in L:
        if p.lower().endswith(('.m2', '.skin', '.blp')) and m.find(p) is not None:
            hits[p].append(n)
over = {p: a for p, a in hits.items() if any(x.lower() < "patch-housing.mpq" for x in a)}
under = {p: a for p, a in hits.items() if any(x.lower() > "patch-housing.mpq" for x in a)}
print("shared paths", len(hits), "housing overrides lower archive", len(over), "housing shadowed by higher", len(under))
print(collections.Counter(p.rsplit('.', 1)[-1].lower() for p in over))
print(collections.Counter(a for v in over.values() for a in v).most_common(12))
json.dump({"over": over, "under": under}, open(os.path.join(here, "shadow.json"), "w"), indent=0)
print([p for p in over if p.lower().endswith('.m2')][:20])
