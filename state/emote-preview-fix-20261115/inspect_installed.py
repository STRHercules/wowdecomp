from pathlib import Path
import ctypes,sys,struct
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
 for name in ('EsteriaEmotes.lua','EsteriaEmotesData.lua'):
  key='Interface\\AddOns\\EsteriaEmotes\\'+name
  path=c.where()[stage.mc.ed.norm(key)]
  payload=c.read(key)
  (Path(__file__).parent/('installed-'+name)).write_bytes(payload)
  print(name,'WINNER',path)
  if name=='EsteriaEmotes.lua':
   text=payload.decode();print(text[text.index('local function danceModel'):text.index('-- state')])
 for model in ('Character\\Human\\Male\\HumanMale.m2','Character\\Human\\Female\\HumanFemale.m2','retail\\1011653.m2'):
  try:
   data=c.read(model);count,offset=struct.unpack_from('<II',data,0x1c)
   sequences=[struct.unpack_from('<HHIII',data,offset+i*64) for i in range(count)]
   print(model,'WINNER',c.where()[stage.mc.ed.norm(model)],'sequences',count,'custom',[(s[0],s[2],s[4]) for s in sequences if s[0]>=3000][:8])
  except Exception as e:print(type(e).__name__,str(e))
 for archive in c.archives.paths:
  if 'emotes' in archive.name.lower():
   names=c.archives.listed(archive)
   print('EMOTE ARCHIVE',len(names),'EXAMPLES',names[:12])
   for n in names:
    if n.endswith('.bin'):
     d=c.read(n)
     if d[:4] in (b'MD21',b'MD20'):
      base=8 if d[:4]==b'MD21' else 0
      count,offset=struct.unpack_from('<II',d,base+0x1c)
      seq=[struct.unpack_from('<HHIII',d,base+offset+i*64) for i in range(count)]
      print('PATCHED MODEL',n,'sequences',count,'custom',[(x[0],x[2],x[4]) for x in seq if x[0]>=3000][:3])
     elif d[:4]==b'SKL1':print('SKEL',n)
   for n in names:
    if n.lower().endswith('/1011653.bin') or n.lower().endswith('\\1011653.bin'):
     d=c.read(n);print('HUMAN PATCH',n,'MAGIC',d[:8])
     base=8 if d[:4]==b'MD21' else 0
     count,offset=struct.unpack_from('<II',d,base+0x1c)
     seq=[struct.unpack_from('<HHIII',d,base+offset+i*64) for i in range(count)]
     print('SEQUENCES',count,'CUSTOM',[(x[0],x[2],x[4]) for x in seq if x[0]>=3000][:8])
finally:c.archives.close()
