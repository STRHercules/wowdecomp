from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Tools/CosmeticsPack'))
import stage
import ctypes
def readonly(self,path):
    handle=stage.mc.ed.cmp.H()
    if not self.dll.SFileOpenArchive(str(path),0,0x100,ctypes.byref(handle)):
        raise ctypes.WinError(ctypes.get_last_error())
    return handle
stage.mc.ed.cmp.Storm.open_archive=readonly
c=stage.mc.Client(Path('G:/3.3.5a - Dev'),True)
try:
    spells=c.table('Spell').rows
    visuals=c.table('SpellVisual').rows
    kits=c.table('SpellVisualKit').rows
    effects=c.table('SpellVisualEffectName').rows
    attaches=c.table('SpellVisualKitModelAttach').rows
    for spell in [8690,302827,302828,302829,302830]:
        row,text=spells[spell];v=visuals[row[131]][0]
        print('SPELL',spell,text.get(136),'kits',v[1:7])
        for kitid in dict.fromkeys(v[1:7]):
            if kitid not in kits: continue
            kit=kits[kitid][0]
            print(' KIT',kitid,'anims',kit[1:3],'effects',[(i,e,effects[e][1].get(2)) for i,e in enumerate(kit[3:15],3) if e in effects])
            print(' ATTACH',[(r[:4],effects.get(r[2],([],{}))[1].get(2)) for r,_ in attaches.values() if r[1]==kitid])
finally:c.archives.close()
