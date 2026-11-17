"""Preserve the installed asset package and replace only Collections/Emotes UI and generated catalogs."""
import json,sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(Path(__file__).parent),str(ROOT/'Tools/EclipseImport')]
import stage
from eclipse_data import storm,list_files

def main():
    out=Path('G:/FortniteCosmetics/collections-20261114/client')
    if out.exists():raise FileExistsError(out)
    client=Path('G:/3.3.5a - Dev');installed=client/'Data/Patch-ZzzzzzzzzzzFortniteBackpacks.MPQ'
    s=storm();h=s.open_archive(installed)
    try:entries={n:s.read(h,n) for n in list_files(s,h) if n not in ('(listfile)','(attributes)','(signature)')}
    finally:s.dll.SFileCloseArchive(h)
    before={n:stage.sha(d) for n,d in entries.items()};changed=set()
    for addon in ('EsteriaCosmetics','EsteriaEmotes'):
        root=ROOT/'Addons'/addon
        for path in root.rglob('*'):
            if path.is_file() and path.suffix in ('.lua','.toc','.xml'):
                key='Interface\\AddOns\\'+addon+'\\'+path.relative_to(root).as_posix().replace('/','\\')
                entries[key]=path.read_bytes();changed.add(key)
    key='Interface\\AddOns\\DragonUI\\modules\\esteria_microbuttons.lua'
    assert key in entries;entries[key]=(ROOT/'Addons/EsteriaShell/esteria_microbuttons.lua').read_bytes();changed.add(key)
    assert all(stage.sha(entries[n])==h for n,h in before.items() if n not in changed)
    out.mkdir();archive=out/installed.name;s.create_archive(archive,entries);h=s.open_archive(archive)
    try:
        for n,d in entries.items():assert s.read(h,n)==d,n
    finally:s.dll.SFileCloseArchive(h)
    receipt=dict(archive=str(archive),archive_sha256=stage.sha(archive.read_bytes()),entries={n:stage.sha(d) for n,d in entries.items()},
        config_after_sha256=stage.sha((client/'Data/Retail/RetailCasc.ini').read_bytes()),changed_entries=sorted(changed),
        unrelated_entries_preserved=True,state='Collections UI staged and archive read back; client visual acceptance pending')
    (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print('PACKAGED',len(entries),'entries; existing models and item data preserved')

if __name__=='__main__':main()
