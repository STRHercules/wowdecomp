"""Replace only the repaired dog/weapon art, retaining installed item IDs, icons, UI and fit settings."""
import json,hashlib,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Tools/EclipseImport'))
from eclipse_data import storm,list_files

def main(wolf,weapon,out):
    if out.exists():raise FileExistsError(out)
    client=Path('G:/3.3.5a - Dev');source=client/'Data/Patch-ZzzzzzzzzzzFortniteBackpacks.MPQ'
    s=storm();h=s.open_archive(source)
    try:entries={name:s.read(h,name) for name in list_files(s,h) if name not in ['(listfile)','(attributes)','(signature)']}
    finally:s.dll.SFileCloseArchive(h)
    old={name:hashlib.sha256(data).hexdigest() for name,data in entries.items()}
    changed=[]
    for root in [wolf,weapon]:
        for p in (root/'patch-root').rglob('*'):
            if p.is_file():
                name=p.relative_to(root/'patch-root').as_posix().replace('/','\\')
                if name not in entries:raise ValueError('Repair unexpectedly introduces new asset path: '+name)
                entries[name]=p.read_bytes();changed.append(name)
    model=json.loads((wolf/'catalog.json').read_text());csv='EsteriaCosmetics\\Backpacks.csv'
    rows=[]
    for line in entries[csv].decode().splitlines():
        fields=line.split(';')
        if fields[0]=='3400100':fields[2:5]=[str(model[k]) for k in ['width','height','depth']]
        rows.append(';'.join(fields))
    entries[csv]=('\n'.join(rows)+'\n').encode();changed.append(csv)
    for p in (ROOT/'Addons/EsteriaCosmetics').rglob('*'):
        if p.is_file() and p.suffix.lower() in ('.lua','.toc','.xml'):
            name='Interface\\AddOns\\EsteriaCosmetics\\'+p.relative_to(ROOT/'Addons/EsteriaCosmetics').as_posix().replace('/','\\')
            entries[name]=p.read_bytes();changed.append(name)
    # Native preview cameras measure posed geometry; item/catalog identity and player fit offsets are unchanged.
    out.mkdir(parents=True);archive=out/source.name;s.create_archive(archive,entries);h=s.open_archive(archive)
    try:
        for name,data in entries.items():assert s.read(h,name)==data,name
    finally:s.dll.SFileCloseArchive(h)
    for name,data in entries.items():
        if name not in changed:assert hashlib.sha256(data).hexdigest()==old[name]
    report={'archive':str(archive),'archive_sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),
        'installed_before_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
        'entries':{n:hashlib.sha256(d).hexdigest() for n,d in entries.items()},'entry_count':len(entries),
        'config_after_sha256':hashlib.sha256((client/'Data/Retail/RetailCasc.ini').read_bytes()).hexdigest(),
        'changed_entries':changed,'wolf':str(wolf),'weapon':str(weapon),
        'checks':'all MPQ entries read back; unrelated entries preserved byte-for-byte'}
    (out/'receipt.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='entries'}))

if __name__=='__main__':main(*(Path(p) for p in sys.argv[1:]))
