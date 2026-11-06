"""Read-only inventory of Patch-Housing.MPQ. Writes existing-assets.tsv + stats.json in this folder."""
import sys, os, json, glob, struct, collections, csv
sys.path.insert(0, r"R:\Users\Zach\Documents\GitHub\wowdecomp\Tools\MountStudio\runtime\reader")
sys.path.insert(0, r"R:\Users\Zach\Documents\GitHub\wowdecomp\Tools\VulkanShaders\bls")
from wotlkconv.db import db2
from mpq_extract import MPQ

here = os.path.dirname(os.path.abspath(__file__))
P = lambda *a: os.path.join(here, *a)
listing = {l.strip().replace('/', '\\').lower() for l in open(P("listfile.txt"), encoding="utf-8", errors="replace") if l.strip()}
models = {int(k): v.lower() for k, v in json.load(open(P("ModelFilePath.json"))).items()}
textures = {int(k): v.lower() for k, v in json.load(open(P("TextureFilePath.json"))).items()}
scan = {r["path"].lower(): r for r in json.load(open(P("m2scan.json")))}

# Client GameObjectDisplayInfo: first archive in load order (descending name) that has it wins.
data = r"G:\3.3.5a - Dev\Data"
archives = sorted(glob.glob(os.path.join(data, "*.mpq")) + glob.glob(os.path.join(data, "*.MPQ")), key=lambda p: os.path.basename(p).lower(), reverse=True)
archives = list(dict.fromkeys(archives)) + sorted(glob.glob(os.path.join(data, "enUS", "*.MPQ")), reverse=True)
godi_src, godi = None, {}
for a in archives:
    try:
        m = MPQ(a)
        if m.find("DBFilesClient\\GameObjectDisplayInfo.dbc") is None: continue
        r = m.read_file("DBFilesClient\\GameObjectDisplayInfo.dbc"); d = r[0] if isinstance(r, tuple) else r
    except Exception as e:
        continue
    godi_src = os.path.basename(a)
    n, f, rs, ss = struct.unpack_from('<4I', d, 4)
    sb = 20 + n * rs
    for i in range(n):
        rid, so = struct.unpack_from('<iI', d, 20 + i * rs)
        godi[rid] = d[sb + so: d.index(b'\0', sb + so)].decode('latin1').replace('/', '\\').lower()
    break
by_path = collections.defaultdict(list)
for rid, p in godi.items(): by_path[p].append(rid)

LIGHT = ["light", "lamp", "lantern", "candle", "torch", "brazier", "fireplace", "sconce", "chandelier"]
NATURE = ["tree", "plant", "flower", "grass", "bush", "foliage", "mushroom", "vine", "boulder", "rock", "shrub", "fern"]
STRUCT = ["wall", "pillar", "column", "door", "window", "fence", "gate", "roof", "beam", "gazebo", "stair", "arch", "floor"]
FURN = ["chair", "table", "desk", "bed", "shelf", "cabinet", "couch", "sofa", "bench", "stool", "rug", "dresser", "wardrobe", "throne"]
ACC = ["book", "banner", "curtain", "painting", "portrait", "statue", "ornament", "vase", "trophy", "food", "crate", "barrel", "pot", "basket", "bottle", "candle"]
def category(path, typ):
    base = path.rsplit('\\', 1)[-1]
    for name, toks in (("Lighting", LIGHT), ("Nature", NATURE), ("Furniture", FURN), ("Structural", STRUCT), ("Accents", ACC)):
        if any(t in base for t in toks): return name
    return "Accents" if typ == 4 else "Misc"

t = db2.parse_db2(open(P("db2", "housedecor.db2"), "rb").read(), "housedecor")
rows, stats = [], collections.Counter()
for rid, r in t:
    fid = r["field_7"]
    path = models.get(fid, "")
    ext = path.rsplit('.', 1)[-1] if path else ""
    in_mpq = path in listing
    s = scan.get(path, {})
    nskin_need = len(s.get("sfid", []))
    nskin_have = len(s.get("skins_present", []))
    tx = [x for x in s.get("txid", []) if x]
    tx_paths = [textures.get(x) for x in tx]
    tx_missing = sum(1 for p in tx_paths if not p or p not in listing)
    flags = []
    if not fid: flags.append("no-model")
    elif not path: flags.append("fdid-not-in-ModelFilePath")
    elif ext == "wmo": flags.append("wmo")
    elif not in_mpq: flags.append("model-not-in-mpq")
    if in_mpq and ext == "m2":
        if nskin_have == 0: flags.append("no-skin")
        elif nskin_have < min(nskin_need, 1): flags.append("skin-missing")
        if tx_missing: flags.append("tex-missing:%d" % tx_missing)
        if s.get("version") not in (272, 274): flags.append("m2-version:%s" % s.get("version"))
    gos = by_path.get(path, []) + by_path.get("retail\\%d.m2" % fid, [])
    ok = in_mpq and ext == "m2" and nskin_have > 0 and not tx_missing
    stats["rows"] += 1; stats["ok" if ok else "flagged"] += 1
    for f in flags: stats[f.split(':')[0]] += 1
    if gos: stats["has-godi"] += 1
    rows.append({"decor_id": rid, "model_fdid": fid, "model_path": path, "ext": ext, "in_mpq": int(in_mpq),
                 "m2_version": s.get("version", ""), "skins": "%d/%d" % (nskin_have, nskin_need) if s else "",
                 "textures": "%d/%d" % (len(tx) - tx_missing, len(tx)) if s else "",
                 "origin": "retail" if fid else "unknown", "category": category(path, r["field_5"]) if path else "",
                 "item_id": r["field_10"], "retail_go_id": r["field_3"], "order": r["field_13"],
                 "godi_rows": ",".join(map(str, gos)), "status": "ok" if ok else ";".join(flags) or "check"})

with open(P("existing-assets.tsv"), "w", newline="", encoding="utf-8") as f:
    w = csv.DictWriter(f, fieldnames=list(rows[0]), delimiter="\t"); w.writeheader(); w.writerows(rows)
decor_paths = {r["model_path"] for r in rows}
orphans = [p for p in scan if p not in decor_paths]
stats["mpq_m2_total"] = len(scan); stats["mpq_m2_not_decor"] = len(orphans)
stats["categories"] = dict(collections.Counter(r["category"] for r in rows if r["status"] == "ok"))
stats["godi_source"] = godi_src; stats["godi_rows"] = len(godi)
stats["godi_retail_paths"] = sum(1 for p in godi.values() if p.startswith("retail\\"))
json.dump({"stats": stats, "orphans_sample": orphans[:25]}, open(P("stats.json"), "w"), indent=1)
print(json.dumps(stats, indent=1)); print(orphans[:10])
