from pathlib import Path
import ctypes,sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Tools/CosmeticsPack'))
import stage
def readonly(self,path):
 h=stage.mc.ed.cmp.H()
 if not self.dll.SFileOpenArchive(str(path),0,0x100,ctypes.byref(h)):raise ctypes.WinError(ctypes.get_last_error())
 return h
stage.mc.ed.cmp.Storm.open_archive=readonly
c=stage.mc.Client(Path('G:/3.3.5a - Dev'),True)
try:
 for relative in ('modules/collections/core.lua','modules/collections/window.lua','modules/collections/microbutton.lua','data/companion_traits.lua'):
  key='Interface/AddOns/DragonUI/'+relative
  try:
   data=c.read(key);out=Path(__file__).parent/relative.replace('/','-');out.write_bytes(data)
   print(relative,c.where()[stage.mc.ed.norm(key)],len(data))
  except KeyError:print('MISSING',relative)
 print('COLLECTION FILES',[k for k in c.where() if 'dragonui' in k and ('collection' in k or 'companion' in k)])
finally:c.archives.close()
