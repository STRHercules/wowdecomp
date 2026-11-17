"""Integrate only the three reviewed elemental sword appearances into the current client/server collection."""
from datetime import datetime
import hashlib,json,shutil,sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'Tools/CosmeticsPack'),str(ROOT/'Tools/EclipseImport')]
import stage
from eclipse_data import storm,list_files
from elemental_swords import WORK,SOURCE

CLIENT=Path('G:/3.3.5a - Dev')

def register(rows):
    ids_path=Path(__file__).with_name('weapon-ids.json');ids=json.loads(ids_path.read_text())
    defaults_path=ROOT/'Tools/CosmeticsPack/weapon-default-fits.json';defaults=json.loads(defaults_path.read_text())
    policy_path=Path(__file__).with_name('weapon-policy.json');policy=json.loads(policy_path.read_text())
    catalog=ROOT/'Engine/Source/Retail/PickaxeCatalog.h';text=catalog.read_text()
    fit_header=ROOT/'Engine/Source/Retail/PickaxeDefaults.h';fittext=fit_header.read_text()
    lua_path=ROOT/'Addons/EsteriaCosmetics/EsteriaPickaxesData.lua';lua=lua_path.read_text()
    old_fits=json.loads(defaults_path.read_text())
    for row in rows:
        display=row['display'];key=row['source'];assert key not in ids and display not in ids.values()
        assert str(display) not in defaults and str(display) not in policy['types'] and display not in policy['removed']
        ids[key]=display;policy['types'][str(display)]='1H Sword'
        fit={mode:list(old_fits['3430040'][mode]) for mode in ('drawn','sheathed')};defaults[str(display)]=fit
        record='    {'+str(display)+','+json.dumps(row['model'].lower())+','+str(row['slot'])+'},\n'
        text=text.replace('};\ninline uint32_t PickaxeDisplay',record+'};\ninline uint32_t PickaxeDisplay',1)
        values=lambda state:','.join(f'{v:.6f}f' for v in fit[state])
        record='    case '+str(display)+': return sheathed ? std::array<float,7>{'+values('sheathed')+'} : std::array<float,7>{'+values('drawn')+'};\n'
        fittext=fittext.replace('    default: return',record+'    default: return',1)
        record='    {display='+str(display)+',name='+json.dumps(row['name'])+',weaponType="1H Sword",slot='+str(row['slot'])
        record+=',drawn={'+','.join(map(str,fit['drawn']))+'},sheathed={'+','.join(map(str,fit['sheathed']))+'}},\n'
        assert lua.rstrip().endswith('}');lua=lua.rstrip()[:-1]+record+'}\n'
    for path,value in ((ids_path,ids),(defaults_path,defaults),(policy_path,policy)):path.write_text(json.dumps(value,indent=2)+'\n')
    catalog.write_text(text);fit_header.write_text(fittext);lua_path.write_text(lua)

def build():
    out=WORK/'integration'
    if out.exists():raise FileExistsError(out)
    rows=json.loads((WORK/'catalog.json').read_text());newids={r['display'] for r in rows};assert len(newids)==3
    if stage.db.mysql('SELECT entry FROM item_template WHERE entry IN ('+','.join(map(str,newids))+')','acore_world'):
        raise ValueError('Allocated sword IDs already occupied')
    config=CLIENT/'Data/Retail/RetailCasc.ini';text=config.read_text()
    values=dict(s.split('=',1) for s in text.splitlines() if '=' in s and not s.startswith('#'))
    base=CLIENT/'Data/Retail'/values['tables'];before=stage.file_manifest(base)
    client=stage.mc.Client(CLIENT,True)
    try:
        for name in ('Item','ItemDisplayInfo'):
            if newids&client.ids(name):raise ValueError('Client ID collision: '+name)
    finally:client.archives.close()
    items=[];displays=[];strings={}
    for i,row in enumerate(rows):
        display=row['display'];items.append([display,2,7,0xffffffff,0,display,row['inventory'],3])
        record=[display]+[0]*24;displays.append(record);strings[i,1]=Path(row['model']).name;strings[i,5]='INV_Sword_39'
    additions={'Item':stage.native.make_wdbc(8,items),'ItemDisplayInfo':stage.native.make_wdbc(25,displays,strings)}
    out.mkdir();shutil.copytree(base,out/'registry')
    for name,data in additions.items():
        path=out/'registry'/(name.lower()+'.dbc');path.write_bytes(stage.merge_registry(path.read_bytes(),data,stage.mc.LAYOUT[name][0],stage.STRING_FIELDS[name]))
    after=stage.file_manifest(out/'registry');assert all(after[n]==h for n,h in before.items() if n not in ('item.dbc','itemdisplayinfo.dbc'))
    revision='elemental-swords-'+stage.sha(b''.join(additions.values()))[:12]
    (out/'RetailCasc.ini').write_text(text.replace('tables='+values['tables'],'tables=Registries/'+revision).replace('aliases='+values['tables']+'/aliases.csv','aliases=Registries/'+revision+'/aliases.csv'))
    register(rows)
    installed=CLIENT/'Data/Patch-ZzzzzzzzzzzFortniteBackpacks.MPQ';s=storm();h=s.open_archive(installed)
    try:entries={n:s.read(h,n) for n in list_files(s,h) if n not in ('(listfile)','(attributes)','(signature)')}
    finally:s.dll.SFileCloseArchive(h)
    original={n:stage.sha(d) for n,d in entries.items()};changed=set()
    for p in (WORK/'patch-root').rglob('*'):
        if p.is_file():
            n=p.relative_to(WORK/'patch-root').as_posix().replace('/','\\');assert n not in entries
            entries[n]=p.read_bytes();changed.add(n)
    key='Interface\\AddOns\\EsteriaCosmetics\\EsteriaPickaxesData.lua';entries[key]=(ROOT/'Addons/EsteriaCosmetics/EsteriaPickaxesData.lua').read_bytes();changed.add(key)
    assert all(stage.sha(entries[n])==h for n,h in original.items() if n not in changed)
    sql=['-- Swords of Fire and Steel main/off-hand and Halo Energy Sword, with native animated material loops.','START TRANSACTION;',
         'DELETE FROM `item_template` WHERE `entry` IN ('+','.join(map(str,sorted(newids)))+');',
         'INSERT INTO `item_template` (`entry`,`class`,`subclass`,`name`,`displayid`,`Quality`,`InventoryType`,`AllowableClass`,`AllowableRace`,`ItemLevel`,`RequiredLevel`,`stackable`,`maxcount`,`bonding`,`dmg_min1`,`dmg_max1`,`delay`,`sheath`,`description`) VALUES']
    sql += [',\n'.join(f"({r['display']},2,7,{stage.quote(r['name'])},{r['display']},4,{r['inventory']},-1,-1,1,0,1,1,1,1,1,2400,3,'Animated cosmetic sword appearance.')" for r in rows)+';','COMMIT;']
    sql=('\n'.join(sql)+'\n').encode();(out/'world.sql').write_bytes(sql)
    pending=stage.SERVER/'data/sql/updates/pending_db_world/rev_20261115010000000_elemental_swords.sql';pending.write_bytes(sql)
    archive=out/installed.name;s.create_archive(archive,entries);h=s.open_archive(archive)
    try:
        for n,d in entries.items():assert s.read(h,n)==d,n
    finally:s.dll.SFileCloseArchive(h)
    report=dict(revision=revision,archive=str(archive),archive_sha256=stage.sha(archive.read_bytes()),entries={n:stage.sha(d) for n,d in entries.items()},
                config_before_sha256=stage.sha(config.read_bytes()),config_after_sha256=stage.sha((out/'RetailCasc.ini').read_bytes()),
                registry_before=dict(path=str(base),files=before),registry_after=after,sql=str(pending),sql_sha256=stage.sha(sql),
                new_items=sorted(newids),weapons=rows,unrelated_entries_preserved=True,state='staged and archive readback verified; live visual acceptance pending')
    (out/'receipt.json').write_text(json.dumps(report,indent=2)+'\n');print('STAGED',len(entries),'entries; new sword items',sorted(newids))

def verify_sql():
    out=WORK/'integration';r=json.loads((out/'receipt.json').read_text());sql=(out/'world.sql').read_bytes();assert stage.sha(sql)==r['sql_sha256']
    schema='elemental_swords_verify_'+datetime.now().strftime('%Y%m%d_%H%M%S')
    stage.db.mysql('CREATE DATABASE `'+schema+'` CHARACTER SET utf8mb4')
    stage.db.mysql(f'CREATE TABLE `{schema}`.`item_template` LIKE `acore_world`.`item_template`')
    for _ in range(2):stage.db.mysql(sql,schema)
    result=stage.db.mysql('SELECT entry,class,subclass,InventoryType,sheath FROM item_template ORDER BY entry',schema)
    assert result=='\n'.join(f"{row['display']}\t2\t7\t{row['inventory']}\t3" for row in r['weapons'])
    (out/'sql-verified.json').write_text(json.dumps(dict(schema=schema,idempotent=True,items=3,sql_sha256=stage.sha(sql)),indent=2)+'\n');print('SQL_VERIFIED three swords, exact hand restrictions, applied twice')

def refresh_art():
    out=WORK/'integration';r=json.loads((out/'receipt.json').read_text());archive=Path(r['archive'])
    assert stage.sha(archive.read_bytes())==r['archive_sha256']
    s=storm();h=s.open_archive(archive)
    try:entries={n:s.read(h,n) for n in r['entries']}
    finally:s.dll.SFileCloseArchive(h)
    changed=set()
    for p in (WORK/'patch-root').rglob('*'):
        if p.is_file():
            n=p.relative_to(WORK/'patch-root').as_posix().replace('/','\\');assert n in entries
            entries[n]=p.read_bytes();changed.add(n)
    assert all(stage.sha(d)==r['entries'][n] for n,d in entries.items() if n not in changed)
    temporary=out/'refined-art.MPQ';assert not temporary.exists();s.create_archive(temporary,entries);h=s.open_archive(temporary)
    try:
        for n,d in entries.items():assert s.read(h,n)==d,n
    finally:s.dll.SFileCloseArchive(h)
    backup=out/'archive-before-art-refinement.MPQ';assert not backup.exists()
    assert archive.resolve().parent==temporary.resolve().parent==out.resolve()
    archive.rename(backup);temporary.rename(archive)
    r.update(archive_sha256=stage.sha(archive.read_bytes()),entries={n:stage.sha(d) for n,d in entries.items()},weapons=json.loads((WORK/'catalog.json').read_text()))
    (out/'receipt.json').write_text(json.dumps(r,indent=2)+'\n');print('REFINED_ART readback verified; item/registry data unchanged')

def install_server():
    out=WORK/'integration';r=json.loads((out/'receipt.json').read_text());v=json.loads((out/'sql-verified.json').read_text());sql=(out/'world.sql').read_bytes()
    assert stage.sha(sql)==r['sql_sha256']==v['sql_sha256']
    if (out/'server-installed.json').exists():raise FileExistsError('Already installed')
    ids=','.join(map(str,r['new_items']))
    assert not stage.db.mysql('SELECT entry FROM item_template WHERE entry IN ('+ids+')','acore_world')
    account=int(stage.db.mysql("SELECT account FROM characters WHERE name='Zach'",'acore_characters'))
    before=stage.db.mysql(f'SELECT item_template_id FROM custom_unlocked_appearances WHERE account_id={account} ORDER BY item_template_id','acore_characters')
    (out/'server-before.json').write_text(json.dumps(dict(account=account,new_items=r['new_items'],prior_appearances=list(map(int,before.split()))),indent=2)+'\n')
    stage.db.mysql(sql,'acore_world')
    stage.db.mysql('INSERT IGNORE INTO custom_unlocked_appearances (account_id,item_template_id) VALUES '+','.join(f'({account},{i})' for i in r['new_items']),'acore_characters')
    assert stage.db.mysql('SELECT COUNT(*) FROM item_template WHERE entry IN ('+ids+')','acore_world')=='3'
    learned=stage.db.mysql(f'SELECT item_template_id FROM custom_unlocked_appearances WHERE account_id={account} ORDER BY item_template_id','acore_characters')
    assert set(r['new_items'])|set(map(int,before.split())) <= set(map(int,learned.split()))
    (out/'server-installed.json').write_text(json.dumps(dict(items=r['new_items'],account=account,prior_appearances_preserved=True,state='installed and read back; worldserver restart required'),indent=2)+'\n')
    print('SERVER_INSTALLED and three appearances granted to Zach')

if __name__=='__main__':
    if sys.argv[1]=='stage':build()
    elif sys.argv[1]=='refresh-art':refresh_art()
    elif sys.argv[1]=='verify-sql':verify_sql()
    elif sys.argv[1]=='install-server':install_server()
