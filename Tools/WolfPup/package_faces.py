"""Replace the four repaired pets and publish exact global backpack rotations without disturbing other appearances."""
import json,sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'Tools/CosmeticsPack'),str(ROOT/'Tools/EclipseImport')]
import stage
from eclipse_data import storm,list_files

WORK=Path('G:/FortniteCosmetics/backpack-face-repair-20261114')
def main():
    out=WORK/'integration'
    if out.exists():raise FileExistsError(out)
    fits=stage.default_fits();assert len(fits)==108 and all(f[4]==-7.5 and f[6]==7.5 for f in fits.values())
    client=Path('G:/3.3.5a - Dev');installed=client/'Data/Patch-ZzzzzzzzzzzFortniteBackpacks.MPQ'
    s=storm();h=s.open_archive(installed)
    try:entries={n:s.read(h,n) for n in list_files(s,h) if n not in ('(listfile)','(attributes)','(signature)')}
    finally:s.dll.SFileCloseArchive(h)
    before={n:stage.sha(d) for n,d in entries.items()};changed=set();checks={}
    for name in ('fox','drift_fox','hamster','woodsy'):
        path=WORK/'pets'/name
        check=json.loads((path/'animation-verified.json').read_text());assert check['bone_key_max_error']<.0001
        assert check['original_clips_retained']==3 and check['changing_channel_samples']>0
        checks[name]=check
        for p in (path/'patch-root').rglob('*'):
            if p.is_file():
                entry=p.relative_to(path/'patch-root').as_posix().replace('/','\\');assert entry in entries
                entries[entry]=p.read_bytes();changed.add(entry)
    key='EsteriaCosmetics\\Backpacks.csv';rows=[]
    for line in entries[key].decode().splitlines():
        row=line.split(';');fit=fits[int(row[0])]
        # Assign absolute angles; never add them to an already-correct profile.
        row[5:]=[f'{v:.6f}' for v in fit];rows.append(';'.join(row))
    assert len(rows)==108
    entries[key]=('\n'.join(rows)+'\n').encode();changed.add(key)
    for name in ('BackpackFitter.lua','EsteriaCosmeticsData.lua'):
        key='Interface\\AddOns\\EsteriaCosmetics\\'+name;entries[key]=(ROOT/'Addons/EsteriaCosmetics'/name).read_bytes();changed.add(key)
    family='EsteriaCosmetics\\FamilyFits.json'
    if family in entries:entries[family]=(ROOT/'Tools/CosmeticsPack/family-default-fits.json').read_bytes();changed.add(family)
    assert all(stage.sha(data)==before[name] for name,data in entries.items() if name not in changed)
    out.mkdir();archive=out/installed.name;s.create_archive(archive,entries);h=s.open_archive(archive)
    try:
        for name,data in entries.items():assert s.read(h,name)==data,name
    finally:s.dll.SFileCloseArchive(h)
    report=dict(archive=str(archive),archive_sha256=stage.sha(archive.read_bytes()),entries={n:stage.sha(d) for n,d in entries.items()},
                config_after_sha256=stage.sha((client/'Data/Retail/RetailCasc.ini').read_bytes()),
                changed_entries=sorted(changed),rotations=dict(x=-7.5,z=7.5,absolute=True),pets=checks,
                state='four pet repairs and 108 absolute rotation defaults; MPQ readback verified; in-game acceptance pending')
    (out/'receipt.json').write_text(json.dumps(report,indent=2)+'\n');print('PACKAGED',len(entries),'entries; 108 exact rotation defaults and four repaired animated pets')

if __name__=='__main__':main()
