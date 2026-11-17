"""Stage a one-entry-per-changed-file overlay from the winning installed UI baseline."""
from datetime import datetime
import ctypes,json,shutil
from pathlib import Path
import stage

FILES=(
    'EsteriaCosmetics/EsteriaCosmetics.lua','EsteriaCosmetics/EsteriaCosmetics.toc',
    'EsteriaCosmetics/Companions.lua','EsteriaCosmetics/EsteriaPetsData.lua',
    'EsteriaIncarnations/EsteriaIncarnations.lua',
    'Manastorm_Transmog/Manastorm_Transmog.lua',
)

def patch_micromenu(text):
    old='MicroButtonFor("Incarnation"), '
    if old in text:
        assert text.count(old)==1
        text=text.replace(old,'')
    marker="-- The strip's default anchor allows for an eleventh button, which Pets & Mounts now supplies."
    assert text.count(marker)==1
    removal=('for i = #MICRO_BUTTONS, 1, -1 do\n'
        '    if MICRO_BUTTONS[i] == _G.CollectionsMicroButton then table.remove(MICRO_BUTTONS, i) end\n'
        'end\n\n')
    return text if removal in text else text.replace(marker,removal+marker)

def package(client,out):
    def readonly(self,path):
        handle=stage.mc.ed.cmp.H()
        if not self.dll.SFileOpenArchive(str(path),0,0x100,ctypes.byref(handle)):
            raise ctypes.WinError(ctypes.get_last_error())
        return handle
    stage.mc.ed.cmp.Storm.open_archive=readonly
    out.mkdir(parents=True,exist_ok=False)
    entries={};before={}
    c=stage.mc.Client(client,True)
    try:
        for relative in FILES:
            key='Interface\\AddOns\\'+relative.replace('/','\\')
            entries[key]=(stage.REPO/'Addons'/relative).read_bytes()
        key='Interface\\AddOns\\DragonUI\\modules\\esteria_microbuttons.lua'
        entries[key]=(stage.REPO/'Addons/EsteriaShell/esteria_microbuttons.lua').read_bytes()
        key='Interface\\AddOns\\DragonUI\\modules\\micromenu.lua'
        entries[key]=patch_micromenu(c.read(key).decode('utf-8').replace('\r\n','\n')).encode()
        for key in entries:
            loose=client/key.replace('\\','/')
            if loose.is_file():
                payload=loose.read_bytes()
                path=out/'before'/key.replace('\\','/')
                path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(payload)
                before[key]=dict(loose=str(loose),sha256=stage.sha(payload))
                continue
            try:
                payload=c.read(key)
                path=out/'before'/key.replace('\\','/')
                path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(payload)
                before[key]=dict(archive=str(c.where()[stage.mc.ed.norm(key)]),sha256=stage.sha(payload))
            except KeyError:before[key]=None
    finally:c.archives.close()
    from lupa.lua51 import LuaRuntime
    lua=LuaRuntime()
    for key,payload in entries.items():
        if key.endswith('.lua'):
            loaded,error=lua.eval('function(s) local f,e=loadstring(s);return f~=nil,e end')(payload.decode())
            assert loaded,(key,error)
    archive=out/'Patch-ZzzzzzzzzzzzzUnifiedCollections.MPQ'
    s=stage.mc.ed.storm();s.create_archive(archive,entries);handle=s.open_archive(archive)
    try:
        for key,payload in entries.items():assert s.read(handle,key)==payload,key
    finally:s.dll.SFileCloseArchive(handle)
    receipt=dict(archive=str(archive),archive_sha256=stage.sha(archive.read_bytes()),before=before,
        entries={k:stage.sha(v) for k,v in entries.items()},
        scope='Collections tabs, companion catalog/services, embedded Incarnations/transmog and retired DragonUI journal/buttons',
        validation=['Lua 5.1 compilation of all packaged Lua files','MPQ entry readback'],live_acceptance=False)
    (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    return receipt

def install(receipt,client):
    source=Path(receipt['archive']);destination=client/'Data'/source.name
    assert stage.sha(source.read_bytes())==receipt['archive_sha256']
    if destination.exists():
        # Only replace this task's overlay; the new package holds the same narrow file set.
        import subprocess
        running=subprocess.check_output(['powershell','-NoProfile','-Command',
            "Get-Process Wow* -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Path"],text=True).strip()
        if running:raise RuntimeError('Exit the client before replacing the Collections overlay: '+running)
        backup=source.parent/'previous-overlay.MPQ';shutil.copy2(destination,backup)
        receipt['previous_overlay']=str(backup)
    shutil.copy2(source,destination)
    assert stage.sha(destination.read_bytes())==receipt['archive_sha256']
    s=stage.mc.ed.storm();h=s.open_archive(source)
    try:
        for key,baseline in receipt['before'].items():
            if baseline and baseline.get('loose'):
                target=Path(baseline['loose'])
                assert stage.sha(target.read_bytes())==baseline['sha256'],str(target)
                payload=s.read(h,key);target.write_bytes(payload)
                assert stage.sha(target.read_bytes())==receipt['entries'][key]
    finally:s.dll.SFileCloseArchive(h)
    receipt['installed_archive']=str(destination)
    receipt['rollback_files']={}
    for key,baseline in receipt['before'].items():
        if not (baseline and baseline.get('loose')):continue
        backup=source.parent/'before'/key.replace('\\','/')
        for previous in sorted((stage.REPO/'state').glob('unified-collections-package-*/receipt.json')):
            saved=json.loads(previous.read_text()).get('before',{}).get(key)
            original=previous.parent/'before'/key.replace('\\','/')
            if saved and saved.get('loose') and original.is_file():
                assert stage.sha(original.read_bytes())==saved['sha256']
                backup=original;break
        receipt['rollback_files'][baseline['loose']]=str(backup)
    receipt['rollback']='After exiting the client, remove '+str(destination)+' and restore the loose files listed in rollback_files.'
    (source.parent/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps(receipt,indent=2))

if __name__=='__main__':
    client=Path('G:/3.3.5a - Dev')
    out=stage.REPO/'state'/('unified-collections-package-'+datetime.now().strftime('%Y%m%d-%H%M%S'))
    receipt=package(client,out)
    install(receipt,client)
