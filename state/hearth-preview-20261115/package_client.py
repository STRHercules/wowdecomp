from pathlib import Path
import ctypes
import hashlib
import json
import runpy
import shutil
import sys
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[2]
OUT=Path(__file__).resolve().parent
CLIENT=Path('G:/3.3.5a - Dev')
sys.path.insert(0,str(ROOT/'Tools/CosmeticsPack'))
import stage

def sha(data):return hashlib.sha256(data).hexdigest()
def readonly(self,path):
    handle=stage.mc.ed.cmp.H()
    if not self.dll.SFileOpenArchive(str(path),0,0x100,ctypes.byref(handle)):
        raise ctypes.WinError(ctypes.get_last_error())
    return handle
stage.mc.ed.cmp.Storm.open_archive=readonly

key='Interface\\AddOns\\EsteriaCosmetics\\EsteriaCosmetics.lua'
client=stage.mc.Client(CLIENT,True)
try:
    original=client.read(key)
    winning=str(client.where()[stage.mc.ed.norm(key)])
finally:client.archives.close()
(OUT/'EsteriaCosmetics-before.lua').write_bytes(original)
text=original.decode('utf-8').replace('\r\n','\n')
edits=[
    ('    model:SetScript("OnUpdate", function(self, elapsed)\n',
     '    model:SetScript("OnUpdate", function(self, elapsed)\n'
     '        local hearthReady\n'
     '        if self.playerPreview and kind(self.row)=="hearth" then\n'
     '            self.castClock = self.castClock + elapsed * 1000\n'
     '            hearthReady = EsteriaCosmeticsHearthPreview(self,self.row.spell,self.castClock)\n'
     '        end\n'),
    ('                if kind(self.row)=="wing" then ready = EsteriaCosmeticsWingPreview(self, self.row.spell)\n',
     '                if kind(self.row)=="wing" then ready = EsteriaCosmeticsWingPreview(self, self.row.spell)\n'
     '                elseif kind(self.row)=="hearth" then ready = hearthReady\n'),
    ('        self.row, self.playerPreview, self.waiting = row, player, nil\n',
     '        self.row, self.playerPreview, self.waiting = row, player, nil\n        self.castClock = 0\n'),
    ('            card.model:ShowRow(row,false)\n',
     '            local playerPreview=kind(row)=="hearth" and type(EsteriaCosmeticsHearthPreview)=="function"\n'
     '            card.model:ShowRow(row,playerPreview)\n'),
    ('            shown(card.icon,not row.model)\n','            shown(card.icon,not row.model and not playerPreview)\n'),
    ('    local playerPreview=focus and (kind(focus)=="wing" and type(EsteriaCosmeticsWingPreview)=="function"\n',
     '    local playerPreview=focus and (kind(focus)=="wing" and type(EsteriaCosmeticsWingPreview)=="function"\n'
     '        or kind(focus)=="hearth" and type(EsteriaCosmeticsHearthPreview)=="function"\n'),
]
for old,new in edits:
    assert text.count(old)==1,old
    text=text.replace(old,new)
payload=text.encode('utf-8')
(OUT/'EsteriaCosmetics.lua').write_bytes(payload)
read_text=Path.read_text
def staged_read(path,*args,**kwargs):
    if path.resolve()==(ROOT/'Addons/EsteriaCosmetics/EsteriaCosmetics.lua').resolve():return text
    return read_text(path,*args,**kwargs)
with patch.object(Path,'read_text',staged_read):
    runpy.run_path(str(ROOT/'Addons/EsteriaCosmetics/test_cosmetics.py'),run_name='__main__')

archive=OUT/'Patch-ZzzzzzzzzzzzzHearthPreview.MPQ'
assert not archive.exists()
s=stage.mc.ed.storm();s.create_archive(archive,{key:payload})
h=s.open_archive(archive)
try:assert s.read(h,key)==payload
finally:s.dll.SFileCloseArchive(h)
installed=CLIENT/'Data'/archive.name
executable=CLIENT/'WowCollectionsPreview.exe'
assert not installed.exists() and not executable.exists()
baseline={name:sha((CLIENT/name).read_bytes()) for name in ('Wow.exe','WowCosmetics.exe','WowMountPreviewFix.exe')}
shutil.copy2(archive,installed)
shutil.copy2(ROOT/'bin/Win/WowClientRetailInventory.exe',executable)
assert installed.read_bytes()==archive.read_bytes()
assert executable.read_bytes()==(ROOT/'bin/Win/WowClientRetailInventory.exe').read_bytes()
assert all(sha((CLIENT/name).read_bytes())==digest for name,digest in baseline.items())
receipt=dict(executable=str(executable),executable_sha256=sha(executable.read_bytes()),
    archive=str(installed),archive_sha256=sha(installed.read_bytes()),
    lua_sha256=sha(payload),addon_baseline_archive=winning,addon_baseline_sha256=sha(original),
    patch_scope='Only six hearth-preview edits to the winning installed Lua file; one-entry overlay',
    existing_executables_preserved=baseline,
    validation=['Release WowClient build passed','staged Lua regression passed','MPQ readback passed','mount bounds regression passed'],
    includes_mount_preview_fix=True,live_acceptance=False,
    activation='Restart using WowCollectionsPreview.exe; the running client is unchanged',
    rollback='Remove the new HearthPreview MPQ and WowCollectionsPreview.exe after exiting the client')
(OUT/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps(receipt,indent=2))
