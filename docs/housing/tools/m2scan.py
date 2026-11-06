"""Read-only scan of Patch-Housing.MPQ M2 headers. Writes m2scan.json next to this script."""
import sys, os, struct, json, collections
sys.path.insert(0, r"R:\Users\Zach\Documents\GitHub\wowdecomp\Tools\VulkanShaders\bls")
from mpq_extract import MPQ

here = os.path.dirname(os.path.abspath(__file__))
m = MPQ(r"G:\3.3.5a - Dev\Data\Patch-Housing.MPQ")
L = [l.strip().replace('/', '\\') for l in open(os.path.join(here, "listfile.txt"), encoding="utf-8", errors="replace") if l.strip()]
low = {l.lower() for l in L}

def read(name):
    r = m.read_file(name)
    return r[0] if isinstance(r, tuple) else r

out = []
for path in [l for l in L if l.lower().endswith('.m2')]:
    rec = {"path": path}
    try:
        d = read(path)
    except Exception as e:
        rec["error"] = repr(e); out.append(rec); continue
    rec["size"] = len(d)
    magic = d[:4]
    chunks = {}
    if magic == b'MD21':
        off = 0
        while off + 8 <= len(d):
            cid, sz = d[off:off+4], struct.unpack_from('<I', d, off+4)[0]
            chunks[cid.decode('latin1')] = (off + 8, sz)
            off += 8 + sz
        md20 = d[8:8+chunks['MD21'][1]] if 'MD21' in chunks else b''
    else:
        md20 = d
    rec["magic"] = magic.decode('latin1')
    rec["chunks"] = sorted(chunks)
    if md20[:4] == b'MD20':
        ver = struct.unpack_from('<I', md20, 4)[0]
        rec["version"] = ver
        if ver >= 264:  # WotLK+ : skin profiles external, nViews at 0x44
            rec["nviews"] = struct.unpack_from('<I', md20, 0x44)[0]
        # textures M2Array at 0x50 (wotlk layout, also >=264)
        ntex, otex = struct.unpack_from('<II', md20, 0x50)
        texs = []
        for i in range(min(ntex, 64)):
            ttype, flags, nlen, noff = struct.unpack_from('<IIII', md20, otex + 16 * i)
            name = md20[noff:noff+nlen].split(b'\0')[0].decode('latin1') if nlen else ''
            texs.append([ttype, name])
        rec["textures"] = texs
    if 'TXID' in chunks:
        o, sz = chunks['TXID']
        rec["txid"] = list(struct.unpack_from('<%dI' % (sz // 4), d, o))
    if 'SFID' in chunks:
        o, sz = chunks['SFID']
        rec["sfid"] = list(struct.unpack_from('<%dI' % (sz // 4), d, o))
    base = path[:-3].lower()
    rec["skins_present"] = sorted(s for s in low if s.startswith(base) and s.endswith('.skin') and len(s) == len(base) + 7)
    out.append(rec)

json.dump(out, open(os.path.join(here, "m2scan.json"), "w"), indent=0)
c = collections.Counter((r.get("magic"), r.get("version")) for r in out)
print(c)
print("errors", sum(1 for r in out if "error" in r))
print("no skins", sum(1 for r in out if not r.get("skins_present")))
print("chunks", collections.Counter(tuple(r.get("chunks", [])) for r in out).most_common(8))
