"""Add the converted collection to the current client registry/MPQ and generate scoped world SQL."""
from datetime import datetime
import hashlib
import json
from pathlib import Path
import shutil
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'Tools/CosmeticsPack'),str(ROOT/'Tools/EclipseImport')]
import stage
from eclipse_data import storm,list_files
from port_collection import PETS,write_json

CLIENT=Path('G:/3.3.5a - Dev')

def collection_sql(pets,weapons,ids):
    items=[];mappings=[]
    for pet in pets:
        assigned=ids[pet['slug']];source,token,display=(assigned[k] for k in ('source','token','display'))
        items.extend([(source,4,1,pet['name'],display,16,0,'',0,0,0,0,0),
                      (token,0,8,'Appearance: '+pet['name'],display,0,stage.USE_SPELL,'item_esteria_backpack_appearance',1,0,0,0,0)])
        mappings.append((source,token))
    for weapon in weapons:
        if weapon.get('existing'):continue
        display=weapon['display'];items.append((display,2,1,weapon['name'],display,17,0,'',1,1,1,3000,1))
    item_ids=[row[0] for row in items]
    assert len(set(item_ids))==len(item_ids)
    lines=['-- Animated Fortnite pet cloak appearances and Fortnite weapon appearances classified as two-handed axes.',
           'START TRANSACTION;',
           'DELETE FROM `item_template` WHERE `entry` IN ('+','.join(map(str,item_ids))+');',
           'INSERT INTO `item_template` (`entry`,`class`,`subclass`,`name`,`displayid`,`Quality`,`InventoryType`,',
           '`AllowableClass`,`AllowableRace`,`ItemLevel`,`RequiredLevel`,`stackable`,`maxcount`,`spellid_1`,',
           '`spelltrigger_1`,`ScriptName`,`description`,`bonding`,`dmg_min1`,`dmg_max1`,`delay`,`sheath`) VALUES']
    values=[]
    for entry,cls,sub,name,display,inv,spell,script,bond,min_dmg,max_dmg,delay,sheath in items:
        description='Use: Learn this animated pet cloak appearance for your account.' if spell else 'Cosmetic appearance.'
        values.append(f'({entry},{cls},{sub},{stage.quote(name)},{display},4,{inv},-1,-1,1,0,1,1,{spell},0,'
                      f'{stage.quote(script)},{stage.quote(description)},{bond},{min_dmg},{max_dmg},{delay},{sheath})')
    lines += [',\n'.join(values)+';',
              'DELETE FROM `esteria_backpack_appearances` WHERE `source_item` IN ('+','.join(str(s) for s,t in mappings)+');',
              'INSERT INTO `esteria_backpack_appearances` (`source_item`,`token_item`) VALUES',
              ',\n'.join(f'({s},{t})' for s,t in mappings)+';','COMMIT;']
    return '\n'.join(lines)+'\n',item_ids

def build(work):
    out=work/'integration'
    if out.exists():raise FileExistsError(out)
    pets=[json.loads((work/'pets'/slug/'catalog.json').read_text()) for slug in PETS]
    for slug in PETS:
        checked=json.loads((work/'pets'/slug/'animation-verified.json').read_text())
        assert checked['bone_key_max_error']<0.0001 and checked['changing_channel_samples']>0
    weapons=json.loads((work/'weapons/catalog.json').read_text())
    prior_ids=json.loads(stage.ID_MANIFEST.read_text());ids=stage.allocate(pets,prior_ids)
    assert all(ids[k]==v for k,v in prior_ids.items())
    sql,item_ids=collection_sql(pets,weapons,ids)
    occupied=stage.db.mysql('SELECT entry FROM item_template WHERE entry IN ('+','.join(map(str,item_ids))+')','acore_world')
    if occupied:raise ValueError('New item IDs already occupied: '+occupied)
    out.mkdir(parents=True)
    config=CLIENT/'Data/Retail/RetailCasc.ini';text=config.read_text()
    values=dict(line.split('=',1) for line in text.splitlines() if '=' in line and not line.startswith('#'))
    base=CLIENT/'Data/Retail'/values['tables'];before=stage.file_manifest(base)
    client=stage.mc.Client(CLIENT,True)
    try:
        if set(item_ids)&client.ids('Item'):raise ValueError('Client item ID collision')
        cape=client.table('ItemDisplayInfo').rows[3400100][1][3]
        displays={ids[r['slug']]['display'] for r in pets}|{r['display'] for r in weapons if not r.get('existing')}
        if displays&client.ids('ItemDisplayInfo'):raise ValueError('Client display ID collision')
    finally:client.archives.close()
    tables=stage.build_tables(pets,ids,cape)
    item_rows,_=stage.native.wdbc(tables['Item'],8)
    display_rows,display_pool=stage.native.wdbc(tables['ItemDisplayInfo'],25)
    display_text={(i,f):display_pool[row[f]:].split(b'\0')[0].decode() for i,row in enumerate(display_rows) for f in stage.STRING_FIELDS['ItemDisplayInfo'] if row[f]}
    for w in weapons:
        if w.get('existing'):continue
        display=w['display'];item_rows.append([display,2,1,0xffffffff,0,display,17,1])
        i=len(display_rows);display_rows.append([display]+[0]*24)
        display_text[i,1]=Path(w['model']).name
        display_text[i,5]='INV_Axe_09'
    additions={'Item':stage.native.make_wdbc(8,item_rows),'ItemDisplayInfo':stage.native.make_wdbc(25,display_rows,display_text)}
    shutil.copytree(base,out/'registry')
    for name,data in additions.items():
        path=out/'registry'/(name.lower()+'.dbc')
        path.write_bytes(stage.merge_registry(path.read_bytes(),data,stage.mc.LAYOUT[name][0],stage.STRING_FIELDS[name]))
    after=stage.file_manifest(out/'registry')
    assert all(after[n]==digest for n,digest in before.items() if n not in ('item.dbc','itemdisplayinfo.dbc'))
    revision='fortnite-collection-'+hashlib.sha256(b''.join(additions.values())).hexdigest()[:12]
    newtext=text.replace('tables='+values['tables'],'tables=Registries/'+revision).replace('aliases='+values['tables']+'/aliases.csv','aliases=Registries/'+revision+'/aliases.csv')
    (out/'RetailCasc.ini').write_text(newtext)
    installed=CLIENT/'Data/Patch-ZzzzzzzzzzzFortniteBackpacks.MPQ';s=storm();h=s.open_archive(installed)
    try:entries={n:s.read(h,n) for n in list_files(s,h) if n not in ('(listfile)','(attributes)','(signature)')}
    finally:s.dll.SFileCloseArchive(h)
    old={n:stage.sha(data) for n,data in entries.items()};changed=set()
    for asset_root in [work/'weapons']+[work/'pets'/slug for slug in PETS]:
        for p in (asset_root/'patch-root').rglob('*'):
            if p.is_file():
                n=p.relative_to(asset_root/'patch-root').as_posix().replace('/','\\');entries[n]=p.read_bytes();changed.add(n)
    fit=json.loads((ROOT/'Tools/CosmeticsPack/family-default-fits.json').read_text())['pet_backpacks']
    csv='EsteriaCosmetics\\Backpacks.csv';runtime=entries[csv].decode().rstrip()+'\n'
    for pet in pets:
        display=ids[pet['slug']]['display']
        runtime+=';'.join([str(display),pet['model']]+[str(pet[k]) for k in ('width','height','depth')]+[f'{v:.6f}' for v in fit])+'\n'
    assert len(runtime.splitlines())==len(ids)
    entries[csv]=runtime.encode();changed.add(csv)
    write_json(stage.ID_MANIFEST,ids)
    defaults=stage.DEFAULT_FITS.read_text().rstrip()+'\n'
    for pet in pets:defaults+=str(ids[pet['slug']]['display'])+';retail/974343.m2;'+';'.join(f'{v:.6f}' for v in fit)+'\n'
    stage.DEFAULT_FITS.write_text(defaults)
    addon=ROOT/'Addons/EsteriaCosmetics/EsteriaCosmeticsData.lua';catalog=addon.read_text();assert catalog.rstrip().endswith('}')
    rows=[]
    for p in pets:
        assigned=ids[p['slug']]
        row=dict(id=assigned['source'],item=assigned['token'],display=assigned['display'],name=p['name'],model=p['model'],
                 width=p['width'],height=p['height'],depth=p['depth'],icon=stage.ICON,
                 **dict(zip(('fitScale','fitX','fitY','fitZ'),fit)))
        rows.append('    {'+', '.join(k+'='+(stage.lua(v) if isinstance(v,str) else str(v)) for k,v in row.items())+'},')
    addon.write_text(catalog.rstrip()[:-1]+'\n'.join(rows)+'\n}\n')
    for p in addon.parent.rglob('*'):
        if p.is_file() and p.suffix in ('.lua','.toc','.xml'):
            n='Interface\\AddOns\\EsteriaCosmetics\\'+p.relative_to(addon.parent).as_posix().replace('/','\\')
            entries[n]=p.read_bytes();changed.add(n)
    for n,digest in old.items():
        if n not in changed:assert stage.sha(entries[n])==digest
    (out/'world.sql').write_bytes(sql.encode())
    sql_path=stage.SERVER/'data/sql/updates/pending_db_world/rev_20261114190000000_fortnite_pet_weapon_collection.sql'
    sql_path.write_bytes(sql.encode())
    archive=out/installed.name;s.create_archive(archive,entries);h=s.open_archive(archive)
    try:
        for n,data in entries.items():assert s.read(h,n)==data,n
    finally:s.dll.SFileCloseArchive(h)
    report=dict(revision=revision,archive=str(archive),archive_sha256=stage.sha(archive.read_bytes()),
                installed_before_sha256=stage.sha(installed.read_bytes()),entries={n:stage.sha(data) for n,data in entries.items()},
                registry_before=dict(path=str(base),files=before),registry_after=after,
                config_before_sha256=stage.sha(config.read_bytes()),config_after_sha256=stage.sha((out/'RetailCasc.ini').read_bytes()),
                backpacks=len(ids),new_pets=[dict(name=p['name'],**ids[p['slug']]) for p in pets],
                weapons=weapons,new_items=item_ids,sql=str(sql_path),sql_sha256=stage.sha(sql.encode()),
                sources_to_grant=[ids[p['slug']]['source'] for p in pets]+[w['display'] for w in weapons if not w.get('existing')],
                unrelated_entries_preserved=True,state='staged; all archive entries read back; live appearance validation pending')
    write_json(out/'receipt.json',report)
    print(json.dumps(dict(stage=str(out),backpacks=len(ids),weapons=len(weapons),new_items=len(item_ids),entries=len(entries))))

def verify_sql(out):
    receipt=json.loads((out/'receipt.json').read_text());sql=(out/'world.sql').read_bytes()
    assert stage.sha(sql)==receipt['sql_sha256']
    schema='fortnite_collection_verify_'+datetime.now().strftime('%Y%m%d_%H%M%S')
    stage.db.mysql('CREATE DATABASE `'+schema+'` CHARACTER SET utf8mb4')
    for table in ('item_template','esteria_backpack_appearances'):
        stage.db.mysql(f'CREATE TABLE `{schema}`.`{table}` LIKE `acore_world`.`{table}`')
    for _ in range(2):stage.db.mysql(sql,schema)
    counts=stage.db.mysql('SELECT (SELECT COUNT(*) FROM item_template),(SELECT COUNT(*) FROM esteria_backpack_appearances)',schema)
    assert counts==f"{len(receipt['new_items'])}\t{len(receipt['new_pets'])}",counts
    axes=stage.db.mysql('SELECT COUNT(*) FROM item_template WHERE class=2 AND subclass=1 AND InventoryType=17 AND sheath=1',schema)
    assert int(axes)==len(receipt['weapons'])-1
    write_json(out/'sql-verified.json',dict(schema=schema,counts=counts,idempotent=True,world_sql_sha256=stage.sha(sql)))
    print('SQL_VERIFIED',schema,counts)

def install_server(out):
    receipt=json.loads((out/'receipt.json').read_text());verified=json.loads((out/'sql-verified.json').read_text())
    sql=(out/'world.sql').read_bytes()
    if stage.sha(sql)!=verified['world_sql_sha256'] or stage.sha(sql)!=receipt['sql_sha256']:
        raise ValueError('SQL changed after verification')
    if (out/'server-installed.json').exists():raise FileExistsError('Server installation already recorded')
    ids=','.join(map(str,receipt['new_items']))
    if stage.db.mysql('SELECT entry FROM item_template WHERE entry IN ('+ids+')','acore_world'):
        raise ValueError('Additive item IDs are already occupied')
    character=stage.db.mysql("SELECT guid,account FROM characters WHERE name='Zach'",'acore_characters').split('\t')
    assert len(character)==2
    guid,account=map(int,character)
    before=stage.db.mysql(f'SELECT item_template_id FROM custom_unlocked_appearances WHERE account_id={account} ORDER BY item_template_id','acore_characters')
    write_json(out/'server-before.json',dict(new_items=receipt['new_items'],account=account,character=guid,
                                           prior_appearances=[int(v) for v in before.split()]))
    stage.db.mysql(sql,'acore_world')
    count=stage.db.mysql('SELECT COUNT(*) FROM item_template WHERE entry IN ('+ids+')','acore_world')
    assert int(count)==len(receipt['new_items'])
    grants=receipt['sources_to_grant']
    stage.db.mysql('INSERT IGNORE INTO custom_unlocked_appearances (account_id,item_template_id) VALUES '+
                   ','.join(f'({account},{item})' for item in grants),'acore_characters')
    learned=stage.db.mysql(f'SELECT item_template_id FROM custom_unlocked_appearances WHERE account_id={account} ORDER BY item_template_id','acore_characters')
    assert set(grants)|{int(v) for v in before.split()} <= {int(v) for v in learned.split()}
    backpacks=stage.db.mysql('SELECT COUNT(*) FROM esteria_backpack_appearances','acore_world')
    assert int(backpacks)==receipt['backpacks']
    write_json(out/'server-installed.json',dict(items=int(count),backpacks=int(backpacks),account=account,
                                               granted=grants,prior_appearances_preserved=True,state='data installed; restart worldserver to refresh catalogs and collection cache'))
    print('SERVER_DATA_INSTALLED',count,'items;',backpacks,'backpacks; granted',len(grants),'appearances to Zach')

if __name__=='__main__':
    work=Path(sys.argv[2] if len(sys.argv)>2 else 'G:/FortniteCosmetics/collection-20261114')
    if sys.argv[1]=='stage':build(work)
    elif sys.argv[1]=='verify-sql':verify_sql(work/'integration')
    elif sys.argv[1]=='install-server':install_server(work/'integration')
