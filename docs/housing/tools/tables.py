"""Read the MPQ's DB2 sidecars (read-only) and dump headers + FDID path maps."""
import sys, os, struct, json
sys.path.insert(0, r"R:\Users\Zach\Documents\GitHub\wowdecomp\Tools\VulkanShaders\bls")
from mpq_extract import MPQ

here = os.path.dirname(os.path.abspath(__file__))
m = MPQ(r"G:\3.3.5a - Dev\Data\Patch-Housing.MPQ")
def read(name):
    r = m.read_file(name); return r[0] if isinstance(r, tuple) else r

os.makedirs(os.path.join(here, "db2"), exist_ok=True)
for n in ['house', 'housedecor', 'housedecormaterial', 'housedecorthemeset', 'houseexteriorwmodata',
          'houseleveldata', 'houseroom', 'housetheme', 'ModelFilePath', 'texturefiledata', 'TextureFilePath']:
    d = read("DBFilesClient\\%s.db2" % n)
    open(os.path.join(here, "db2", n + ".db2"), "wb").write(d)
    print(n, d[:4], len(d), struct.unpack_from('<5I', d, 4))

def path_table(d):
    cnt, fields, rsz, ssz = struct.unpack_from('<4I', d, 4)
    base = 92; sb = base + cnt * rsz; res = {}
    for i in range(cnt):
        fid, so = struct.unpack_from('<II', d, base + i * rsz)
        s = d[sb + so: d.index(b'\0', sb + so)].decode('latin1')
        res[fid] = s
    return res

for n in ['ModelFilePath', 'TextureFilePath']:
    d = open(os.path.join(here, "db2", n + ".db2"), "rb").read()
    try:
        t = path_table(d)
        json.dump(t, open(os.path.join(here, n + ".json"), "w"))
        print(n, len(t), list(t.items())[:3])
    except Exception as e:
        print(n, "parse fail", e)
