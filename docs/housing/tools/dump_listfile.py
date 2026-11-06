import sys; sys.path.insert(0, r"R:\Users\Zach\Documents\GitHub\wowdecomp\Tools\VulkanShaders\bls")
from mpq_extract import MPQ
m = MPQ(r"G:\3.3.5a - Dev\Data\Patch-Housing.MPQ")
r = m.read_file("(listfile)")
data = r[0] if isinstance(r, tuple) else r
open("listfile.txt","wb").write(data)
print(len(data.splitlines()))
