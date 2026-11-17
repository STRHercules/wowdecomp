"""Apply the owner's November 14 weapon decisions and exported per-appearance fits."""
import csv
from datetime import datetime
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import struct
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'Tools/CosmeticsPack'),str(ROOT/'Tools/EclipseImport')]
import stage
from eclipse_data import storm,list_files
from port_collection import write_json

CLIENT=Path('G:/3.3.5a - Dev')
WORK=Path('G:/FortniteCosmetics/collection-revision-20261114')
POLICY=Path(__file__).with_name('weapon-policy.json')
WEAPONS=ROOT/'Tools/CosmeticsPack/weapon-default-fits.json'
TYPES={'2H Axe':(1,17,1,15),'2H Sword':(8,17,1,15),'1H Axe':(0,13,3,15),
       '1H Sword':(7,13,3,15),'1H Mace':(4,13,3,15),'Dagger':(15,13,3,15),
       'Polearm':(6,17,2,15),'Fist (Left)':(13,22,7,16),'Fist (Right)':(13,21,7,15)}

def parse_fits(text,weapon):
    result={}
    for row in csv.reader((s for s in text.splitlines() if s.strip() and not s.startswith('#')),delimiter=';'):
        if len(row) not in ((10,) if weapon else (6,9)):raise ValueError('Unexpected exported fit shape')
        display=int(row[0]);state=row[-1] if weapon else 'backpack'
        if weapon and state not in ('drawn','sheathed'):raise ValueError('Invalid state')
        fit=list(map(float,row[2:9] if weapon else row[2:]))
        if not all(math.isfinite(v) for v in fit) or not .1<=fit[0]<=4 or any(abs(v)>3 for v in fit[1:4]) or any(abs(v)>180 for v in fit[4:]):
            raise ValueError('Invalid fit')
        key=(display,state)
        if key in result and result[key]!=fit:raise ValueError('Conflicting per-body exports need an explicit body default')
        result[key]=fit
    return result

def prepare():
    WORK.mkdir(exist_ok=True)
    policy=json.loads(POLICY.read_text());removed=set(policy['removed'])
    original=json.loads(Path('G:/FortniteCosmetics/collection-20261114/weapons/catalog.json').read_text())
    axes=CLIENT/'WTF/Esteria/PickaxeFits/PickaxeFits-20261114-112450.txt'
    packs=CLIENT/'WTF/Esteria/BackpackFits/BackpackFits-20261114-112806.txt'
    exported=parse_fits(axes.read_text(),True);backpacks=parse_fits(packs.read_text(),False)
    known={r['display'] for r in original};known_packs={r['display'] for r in json.loads(stage.ID_MANIFEST.read_text()).values()}
    assert {k[0] for k in exported}<=known and {k[0] for k in backpacks}<=known_packs
    family=json.loads((ROOT/'Tools/CosmeticsPack/family-default-fits.json').read_text())['fortnite_2h_axes']
    fits={};rows=[]
    for row in original:
        display=row['display']
        if display in removed:continue
        row=dict(row);kind=policy['types'].get(str(display),'2H Axe')
        subclass,inventory,sheath,slot=TYPES[kind]
        row.update(weapon_type=kind,subclass=subclass,inventory=inventory,sheath=sheath,slot=slot)
        fits[str(display)]={state:exported.get((display,state),family[state]) for state in ('drawn','sheathed')}
        rows.append(row)
    assert len(rows)==60 and len(removed)==23 and {r['display'] for r in rows}|removed==known
    write_json(WORK/'catalog.json',rows);write_json(WEAPONS,fits)
    current=stage.default_fits();lines=['# display;body_model;scale_multiplier;x;y;z']
    for display in sorted(current):
        fit=backpacks.get((display,'backpack'),current[display]);lines.append(str(display)+';retail/974343.m2;'+';'.join(f'{v:.6f}' for v in fit))
    stage.DEFAULT_FITS.write_text('\n'.join(lines)+'\n')
    header=['#pragma once','#include <array>','#include <cstdint>','namespace Retail {',
            'inline std::array<float,7> PickaxeDefaultFit(uint32_t display, bool sheathed) {','    switch (display) {']
    for display,states in fits.items():
        values=lambda state:','.join(f'{v:.6f}f' for v in states[state])
        header.append('    case '+display+': return sheathed ? std::array<float,7>{'+values('sheathed')+'} : std::array<float,7>{'+values('drawn')+'};')
    header+=['    default: return {1,0,0,0,0,0,0};','    }','}','}']
    (ROOT/'Engine/Source/Retail/PickaxeDefaults.h').write_text('\n'.join(header)+'\n')
    header=['#pragma once','#include <cstdint>','#include <string_view>','namespace Retail {',
            'struct PickaxeAppearance { uint32_t display; std::string_view model; uint32_t slot; };',
            'inline constexpr PickaxeAppearance PickaxeAppearances[] = {']
    header+=['    {'+str(r['display'])+','+json.dumps(r['model'].lower())+','+str(r['slot'])+'},' for r in rows]
    header+=['};','inline uint32_t PickaxeDisplay(std::string_view model) {',
             '    for (const auto& appearance : PickaxeAppearances) if (appearance.model == model) return appearance.display;',
             '    return 0;','}','inline bool IsPickaxeDisplay(uint32_t display) {',
             '    for (const auto& appearance : PickaxeAppearances) if (appearance.display == display) return true;',
             '    return false;','}','}']
    (ROOT/'Engine/Source/Retail/PickaxeCatalog.h').write_text('\n'.join(header)+'\n')
    lua=['-- Generated by Tools/FortniteCosmetics/revise_collection.py.','EsteriaPickaxesData = {']
    for r in rows:
        fields=dict(display=r['display'],name=r['name'],weaponType=r['weapon_type'],slot=r['slot'])
        for state,fit in fits[str(r['display'])].items():fields[state]=fit
        parts=[]
        for k,v in fields.items():parts.append(k+'='+('{'+','.join(str(n) for n in v)+'}' if isinstance(v,list) else json.dumps(v)))
        lua.append('    {'+','.join(parts)+'},')
    (ROOT/'Addons/EsteriaCosmetics/EsteriaPickaxesData.lua').write_text('\n'.join(lua+['}'])+'\n')
    addon=ROOT/'Addons/EsteriaCosmetics/EsteriaCosmeticsData.lua';text=addon.read_text();fits=stage.default_fits()
    for display,fit in fits.items():
        pattern=r'(?m)^.*\{id='+str(display)+r',.*$'
        def update(match):
            line=match[0]
            for field,value in zip(('fitScale','fitX','fitY','fitZ','fitPitch','fitYaw','fitRoll'),fit):
                line,count=re.subn(r'\b'+field+r'=[^,}]+',field+'='+str(value),line)
                if count==0:line=line.replace('},',','+field+'='+str(value)+'},')
                else:assert count==1
            return line
        text,count=re.subn(pattern,update,text);assert count==1
    addon.write_text(text)
    write_json(WORK/'intake.json',dict(weapon_export=str(axes),backpack_export=str(packs),
               weapon_sha256=stage.sha(axes.read_bytes()),backpack_sha256=stage.sha(packs.read_bytes()),
               imported_weapon_states=len(exported),imported_backpacks=len(backpacks),removed=sorted(removed),retained=len(rows)))
    print('PREPARED',len(rows),'weapons;',len(exported),'exported states;',len(backpacks),'backpack defaults')

def package():
    out=WORK/'integration'
    if out.exists():raise FileExistsError(out)
    policy=json.loads(POLICY.read_text());removed=set(policy['removed']);rows=json.loads((WORK/'catalog.json').read_text())
    out.mkdir();config=CLIENT/'Data/Retail/RetailCasc.ini';text=config.read_text()
    values=dict(s.split('=',1) for s in text.splitlines() if '=' in s and not s.startswith('#'))
    base=CLIENT/'Data/Retail'/values['tables'];before=stage.file_manifest(base);shutil.copytree(base,out/'registry')
    path=out/'registry/item.dbc';blob=bytearray(path.read_bytes());count,fields=struct.unpack_from('<II',blob,4);found=set()
    byid={r['display']:r for r in rows}
    for i in range(count):
        offset=20+i*fields*4;display=struct.unpack_from('<I',blob,offset)[0]
        if display in byid:
            r=byid[display];struct.pack_into('<I',blob,offset+2*4,r['subclass']);struct.pack_into('<II',blob,offset+6*4,r['inventory'],r['sheath']);found.add(display)
    assert found==set(byid)
    # Removed sources disappear from the supplemental client table; their stable IDs remain reserved in weapon-ids.json.
    raw=[bytes(blob[20+i*32:20+(i+1)*32]) for i in range(count) if struct.unpack_from('<I',blob,20+i*32)[0] not in removed]
    pool=blob[20+count*32:];blob=struct.pack('<4s4I',b'WDBC',len(raw),8,32,len(pool))+b''.join(raw)+pool
    path.write_bytes(blob)
    after=stage.file_manifest(out/'registry');assert all(before[n]==h for n,h in after.items() if n!='item.dbc')
    revision='fortnite-types-'+stage.sha(blob)[:12]
    newtext=text.replace('tables='+values['tables'],'tables=Registries/'+revision).replace('aliases='+values['tables']+'/aliases.csv','aliases=Registries/'+revision+'/aliases.csv')
    (out/'RetailCasc.ini').write_text(newtext)
    installed=CLIENT/'Data/Patch-ZzzzzzzzzzzFortniteBackpacks.MPQ';s=storm();h=s.open_archive(installed)
    try:entries={n:s.read(h,n) for n in list_files(s,h) if n not in ('(listfile)','(attributes)','(signature)')}
    finally:s.dll.SFileCloseArchive(h)
    original={n:stage.sha(d) for n,d in entries.items()};changed=set();fits=stage.default_fits();csv='EsteriaCosmetics\\Backpacks.csv'
    lines=[]
    for line in entries[csv].decode().splitlines():
        row=line.split(';');row[5:]=[f'{v:.6f}' for v in fits[int(row[0])]];lines.append(';'.join(row))
    entries[csv]=('\n'.join(lines)+'\n').encode();changed.add(csv)
    for p in (ROOT/'Addons/EsteriaCosmetics').rglob('*'):
        if p.is_file() and p.suffix in ('.lua','.toc','.xml'):
            n='Interface\\AddOns\\EsteriaCosmetics\\'+p.relative_to(ROOT/'Addons/EsteriaCosmetics').as_posix().replace('/','\\');entries[n]=p.read_bytes();changed.add(n)
    art=WORK/'repairs/patch-root'
    for p in art.rglob('*'):
        if p.is_file():
            n=p.relative_to(art).as_posix().replace('/','\\');entries[n]=p.read_bytes();changed.add(n)
    for n,h in original.items():
        if n not in changed:assert stage.sha(entries[n])==h
    sql=['-- Owner-reviewed Fortnite weapon classifications and removals. Stable IDs are not reused.','START TRANSACTION;']
    for r in rows:
        sql.append(f"UPDATE `item_template` SET `subclass`={r['subclass']}, `InventoryType`={r['inventory']}, `sheath`={r['sheath']} WHERE `entry`={r['display']};")
    sql+=['DELETE FROM `item_template` WHERE `entry` IN ('+','.join(map(str,sorted(removed)))+');','COMMIT;']
    sql=('\n'.join(sql)+'\n').encode();(out/'world.sql').write_bytes(sql)
    pending=stage.SERVER/'data/sql/updates/pending_db_world/rev_20261114200000000_fortnite_weapon_types.sql';pending.write_bytes(sql)
    archive=out/installed.name;s.create_archive(archive,entries);h=s.open_archive(archive)
    try:
        for n,d in entries.items():assert s.read(h,n)==d,n
    finally:s.dll.SFileCloseArchive(h)
    report=dict(revision=revision,archive=str(archive),archive_sha256=stage.sha(archive.read_bytes()),entries={n:stage.sha(d) for n,d in entries.items()},
                registry_before=dict(path=str(base),files=before),registry_after=after,config_before_sha256=stage.sha(config.read_bytes()),
                config_after_sha256=stage.sha((out/'RetailCasc.ini').read_bytes()),removed=sorted(removed),weapons=rows,
                sql=str(pending),sql_sha256=stage.sha(sql),state='staged, archive readback verified')
    write_json(out/'receipt.json',report);print('PACKAGED',len(rows),'weapons;',len(entries),'MPQ entries')

def verify_sql():
    out=WORK/'integration';receipt=json.loads((out/'receipt.json').read_text());sql=(out/'world.sql').read_bytes()
    assert stage.sha(sql)==receipt['sql_sha256']
    ids=[r['display'] for r in receipt['weapons']]+receipt['removed'];where='entry IN ('+','.join(map(str,ids))+')'
    schema='fortnite_types_verify_'+datetime.now().strftime('%Y%m%d_%H%M%S')
    stage.db.mysql('CREATE DATABASE `'+schema+'` CHARACTER SET utf8mb4')
    stage.db.mysql(f'CREATE TABLE `{schema}`.`item_template` LIKE `acore_world`.`item_template`')
    stage.db.mysql(f'INSERT INTO `{schema}`.`item_template` SELECT * FROM `acore_world`.`item_template` WHERE '+where)
    before=stage.db.mysql('SELECT COUNT(*) FROM item_template',schema);assert before=='83',before
    for _ in range(2):stage.db.mysql(sql,schema)
    actual=stage.db.mysql('SELECT entry,subclass,InventoryType,sheath FROM item_template ORDER BY entry',schema)
    expected={r['display']:(r['subclass'],r['inventory'],r['sheath']) for r in receipt['weapons']}
    assert {int(line.split('\t')[0]):tuple(map(int,line.split('\t')[1:])) for line in actual.splitlines()}==expected
    write_json(out/'sql-verified.json',dict(schema=schema,idempotent=True,retained=60,removed=23,sql_sha256=stage.sha(sql)))
    print('SQL_VERIFIED: 60 exact classifications; 23 removed; migration applied twice')

def install_server():
    out=WORK/'integration';receipt=json.loads((out/'receipt.json').read_text());verified=json.loads((out/'sql-verified.json').read_text())
    sql=(out/'world.sql').read_bytes();assert stage.sha(sql)==verified['sql_sha256']==receipt['sql_sha256']
    if (out/'server-installed.json').exists():raise FileExistsError('Already installed')
    removed=','.join(map(str,receipt['removed']));allids=','.join(map(str,[r['display'] for r in receipt['weapons']]+receipt['removed']))
    issued=stage.db.mysql('SELECT guid FROM item_instance WHERE itemEntry IN ('+removed+')','acore_characters')
    if issued:raise ValueError('Removed sources have issued physical items; review their migration first')
    world_dump=stage.db.dump('acore_world',['item_template'],'--where="entry IN ('+allids+')"','--no-create-info')
    (out/'backup-world-items.sql').write_bytes(world_dump)
    for table,column in [('custom_unlocked_appearances','item_template_id'),('custom_transmogrification','FakeEntry')]:
        data=stage.db.dump('acore_characters',[table],'--where="'+column+' IN ('+removed+')"','--no-create-info')
        (out/('backup-'+table+'.sql')).write_bytes(data)
    cleanup=('START TRANSACTION;\nDELETE FROM `custom_unlocked_appearances` WHERE `item_template_id` IN ('+removed+');\n'
             'DELETE FROM `custom_transmogrification` WHERE `FakeEntry` IN ('+removed+');\nCOMMIT;\n').encode()
    pending=stage.SERVER/'data/sql/updates/pending_db_characters/rev_20261114200000000_fortnite_removed_appearances.sql'
    pending.write_bytes(cleanup);(out/'characters.sql').write_bytes(cleanup)
    stage.db.mysql(sql,'acore_world');stage.db.mysql(cleanup,'acore_characters')
    actual=stage.db.mysql('SELECT entry,subclass,InventoryType,sheath FROM item_template WHERE entry IN ('+allids+') ORDER BY entry','acore_world')
    expected={r['display']:(r['subclass'],r['inventory'],r['sheath']) for r in receipt['weapons']}
    assert {int(line.split('\t')[0]):tuple(map(int,line.split('\t')[1:])) for line in actual.splitlines()}==expected
    assert stage.db.mysql('SELECT COUNT(*) FROM custom_unlocked_appearances WHERE item_template_id IN ('+removed+')','acore_characters')=='0'
    assert stage.db.mysql('SELECT COUNT(*) FROM custom_transmogrification WHERE FakeEntry IN ('+removed+')','acore_characters')=='0'
    write_json(out/'server-installed.json',dict(retained=60,removed=23,backups='scoped world/character SQL backups beside this receipt',
              state='data read back; worldserver restart required'))
    print('SERVER_DATA_INSTALLED: 60 weapon types; 23 removed appearances and any active uses cleared; scoped backups saved')

if __name__=='__main__':
    if sys.argv[1]=='prepare':prepare()
    elif sys.argv[1]=='package':package()
    elif sys.argv[1]=='verify-sql':verify_sql()
    elif sys.argv[1]=='install-server':install_server()
