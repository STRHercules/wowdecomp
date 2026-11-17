"""Update approved family fitting defaults while preserving the entire current art/UI/icon package."""
import json,hashlib,re,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Tools/EclipseImport'))
from eclipse_data import storm,list_files

def main(out):
    if out.exists():raise FileExistsError(out)
    client=Path('G:/3.3.5a - Dev');installed=client/'Data/Patch-ZzzzzzzzzzzFortniteBackpacks.MPQ'
    defaults=json.loads(Path(__file__).with_name('family-default-fits.json').read_text())
    s=storm();h=s.open_archive(installed)
    try:entries={name:s.read(h,name) for name in list_files(s,h) if name not in ['(listfile)','(attributes)','(signature)']}
    finally:s.dll.SFileCloseArchive(h)
    before={name:hashlib.sha256(data).hexdigest() for name,data in entries.items()}
    csv='EsteriaCosmetics\\Backpacks.csv';rows=[];changed=0
    for line in entries[csv].decode().splitlines():
        row=line.split(';')
        if row[0]=='3400100':row[5:9]=[f'{v:.6f}' for v in defaults['pet_backpacks']];changed+=1
        rows.append(';'.join(row))
    if changed!=1:raise ValueError('Expected exactly one Wolf Pup runtime profile')
    entries[csv]=('\n'.join(rows)+'\n').encode()
    catalog=ROOT/'Addons/EsteriaCosmetics/EsteriaCosmeticsData.lua';source=catalog.read_text();result=[];changed=0
    for line in source.splitlines():
        if line.lstrip().startswith('{id=3400100,'):
            for field,value in zip(['fitScale','fitX','fitY','fitZ'],defaults['pet_backpacks']):
                line,n=re.subn(r'\b'+field+r'=[^,}]+',field+'='+str(value),line)
                if n!=1:raise ValueError('Wolf Pup Lua default field missing')
            changed+=1
        result.append(line)
    if changed!=1:raise ValueError('Expected exactly one Wolf Pup addon entry')
    text='\n'.join(result)+'\n';catalog.write_text(text)
    prefix='Interface\\AddOns\\EsteriaCosmetics\\'
    entries[prefix+'EsteriaCosmeticsData.lua']=text.encode()
    entries[prefix+'PickaxeFitter.lua']=(ROOT/'Addons/EsteriaCosmetics/PickaxeFitter.lua').read_bytes()
    entries['EsteriaCosmetics\\FamilyFits.json']=(json.dumps(defaults,indent=2)+'\n').encode()
    touched={csv,prefix+'EsteriaCosmeticsData.lua',prefix+'PickaxeFitter.lua','EsteriaCosmetics\\FamilyFits.json'}
    for name,data in entries.items():
        if name not in touched:assert hashlib.sha256(data).hexdigest()==before[name]
    out.mkdir(parents=True);archive=out/installed.name;s.create_archive(archive,entries);h=s.open_archive(archive)
    try:
        for name,data in entries.items():assert s.read(h,name)==data,name
    finally:s.dll.SFileCloseArchive(h)
    report={'archive':str(archive),'archive_sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),
            'config_after_sha256':hashlib.sha256((client/'Data/Retail/RetailCasc.ini').read_bytes()).hexdigest(),
            'entries':{name:hashlib.sha256(data).hexdigest() for name,data in entries.items()},
            'defaults':defaults,'backpack_rows_changed':[3400100],'unrelated_entries_preserved':True,
            'visual_acceptance':'User accepted Wolf Pup shadow/darkness repair'}
    (out/'receipt.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'archive':str(archive),'entries':len(entries),'defaults':defaults}))

if __name__=='__main__':main(Path(sys.argv[1] if len(sys.argv)>1 else 'G:/FortniteCosmetics/family-defaults-v1'))
