"""Stage additive Wolf Pup backpack and Rainbow Smash item/display records against the current install."""
import json,struct,hashlib,shutil,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'Tools/CosmeticsPack'),str(ROOT/'Tools/EclipseImport')]
import stage
from eclipse_data import storm,list_files

def build(out):
    if out.exists():raise FileExistsError(out)
    client=Path('G:/3.3.5a - Dev');config=client/'Data/Retail/RetailCasc.ini';text=config.read_text()
    values=dict(line.split('=',1) for line in text.splitlines() if '=' in line and not line.startswith('#'))
    base=client/'Data/Retail'/values['tables'];before=stage.file_manifest(base)
    wolf=Path('G:/FortniteCosmetics/wolf-pup-v1');weapon=Path('G:/FortniteCosmetics/rainbow-smash-v1')
    w=json.loads((wolf/'catalog.json').read_text());axe=json.loads((weapon/'catalog.json').read_text())
    for query in ['SELECT entry FROM item_template WHERE entry IN (3400100,3410100,3430001)',
                  'SELECT source_item FROM esteria_backpack_appearances WHERE source_item=3400100']:
        if stage.db.mysql(query,'acore_world'):raise ValueError('New item IDs already installed; use the existing receipt to update')
    ids=json.loads(stage.ID_MANIFEST.read_text());ids['animated_wolf_pup']={'index':100,'source':3400100,'token':3410100,'display':3400100}
    assert len(ids)==100
    native=stage.native
    items=[[3400100,4,1,0xffffffff,0,3400100,16,0],[3410100,0,8,0xffffffff,0,3400100,0,0],
           [3430001,2,1,0xffffffff,0,3430001,17,1]]
    displays=[[0]*25 for _ in range(2)];displays[0][0]=3400100;displays[0][7]=1;displays[1][0]=3430001
    # A known stock cape supplies the temporary texture until the animated child finishes loading.
    cmd=stage.mc.Client(client,True)
    try:
        for name,allocated in [('Item',{3400100,3410100,3430001}),('ItemDisplayInfo',{3400100,3430001})]:
            if allocated&cmd.ids(name):raise ValueError('Client ID collision')
        cape=next(strings[3] for row,strings in cmd.table('ItemDisplayInfo').rows.values()
                  if row[7]==1 and strings.get(3) and not strings.get(1) and not strings.get(2))
    finally:cmd.archives.close()
    strings={(0,3):cape,(0,5):'INV_Misc_Head_Wolf_01',(1,1):axe['model_name'],(1,3):axe['model_texture'],(1,5):'INV_Axe_09'}
    additions={'Item':native.make_wdbc(8,items),'ItemDisplayInfo':native.make_wdbc(25,displays,strings)}
    out.mkdir(parents=True);shutil.copytree(base,out/'registry')
    for name,data in additions.items():
        target=out/'registry'/(name.lower()+'.dbc');target.write_bytes(stage.merge_registry(target.read_bytes(),data,stage.mc.LAYOUT[name][0],stage.STRING_FIELDS[name]))
    after=stage.file_manifest(out/'registry')
    assert all(after[n]==digest for n,digest in before.items() if n not in ['item.dbc','itemdisplayinfo.dbc'])
    revision='wolf-rainbow-'+hashlib.sha256(b''.join(additions.values())).hexdigest()[:12]
    newtext=text.replace('tables='+values['tables'],'tables=Registries/'+revision).replace('aliases='+values['tables']+'/aliases.csv','aliases=Registries/'+revision+'/aliases.csv')
    (out/'RetailCasc.ini').write_text(newtext)
    s=storm();installed=client/'Data/Patch-ZzzzzzzzzzzFortniteBackpacks.MPQ';h=s.open_archive(installed)
    try:entries={name:s.read(h,name) for name in list_files(s,h) if name not in ['(listfile)','(attributes)','(signature)']}
    finally:s.dll.SFileCloseArchive(h)
    for root in (wolf,weapon):
        for p in (root/'patch-root').rglob('*'):
            if p.is_file():entries[p.relative_to(root/'patch-root').as_posix().replace('/','\\')]=p.read_bytes()
    runtime='EsteriaCosmetics\\Backpacks.csv'
    line=f"3400100;{w['model']};{w['width']:.9g};{w['height']:.9g};{w['depth']:.9g};1.000000;0.000000;0.000000;0.000000\n"
    entries[runtime]=entries[runtime].rstrip(b'\r\n')+b'\n'+line.encode()
    row={'id':3400100,'item':3410100,'display':3400100,'name':w['name'],'model':w['model'],
         'width':w['width'],'height':w['height'],'depth':w['depth'],'icon':'Interface\\Icons\\INV_Misc_Head_Wolf_01',
         'fitScale':1,'fitX':0,'fitY':0,'fitZ':0}
    lua='    {'+', '.join(k+'='+(stage.lua(v) if isinstance(v,str) else str(v)) for k,v in row.items())+'},\n'
    catalog=ROOT/'Addons/EsteriaCosmetics/EsteriaCosmeticsData.lua';old=catalog.read_text();assert old.rstrip().endswith('}')
    old='\n'.join(line for line in old.splitlines() if not line.lstrip().startswith('{id=3400100,'))+'\n'
    new=old.rstrip()[:-1]+lua+'}\n';catalog.write_text(new)
    entries['Interface\\AddOns\\EsteriaCosmetics\\EsteriaCosmeticsData.lua']=new.encode()
    stage.ID_MANIFEST.write_text(json.dumps(ids,indent=2,sort_keys=True)+'\n')
    sql="""-- Animated Wolf Pup backpack and Rainbow Smash cosmetic weapon.
START TRANSACTION;
DELETE FROM `item_template` WHERE `entry` IN (3400100,3410100,3430001);
INSERT INTO `item_template` (`entry`,`class`,`subclass`,`name`,`displayid`,`Quality`,`InventoryType`,`AllowableClass`,`AllowableRace`,`ItemLevel`,`RequiredLevel`,`stackable`,`maxcount`,`spellid_1`,`spelltrigger_1`,`ScriptName`,`description`,`bonding`,`dmg_min1`,`dmg_max1`,`delay`,`sheath`) VALUES
(3400100,4,1,'Wolf Pup (Remus)',3400100,4,16,-1,-1,1,0,1,1,0,0,'','Animated cosmetic cloak appearance.',0,0,0,0,0),
(3410100,0,8,'Appearance: Wolf Pup (Remus)',3400100,4,0,-1,-1,1,0,1,1,3420001,0,'item_esteria_backpack_appearance','Use: Learn the animated Wolf Pup cloak appearance for your account.',1,0,0,0,0),
(3430001,2,1,'Rainbow Smash',3430001,4,17,-1,-1,1,0,1,1,0,0,'','Cosmetic two-handed axe appearance.',1,1,1,3000,1);
DELETE FROM `esteria_backpack_appearances` WHERE `source_item`=3400100;
INSERT INTO `esteria_backpack_appearances` (`source_item`,`token_item`) VALUES (3400100,3410100);
COMMIT;
"""
    sqlpath=stage.SERVER/'data/sql/updates/pending_db_world/rev_20261114150000000_wolf_pup_rainbow_smash.sql'
    sqlpath.write_text(sql);(out/'world.sql').write_text(sql)
    archive=out/installed.name;s.create_archive(archive,entries);h=s.open_archive(archive)
    try:
        for name,data in entries.items():assert s.read(h,name)==data,name
    finally:s.dll.SFileCloseArchive(h)
    receipt={'revision':revision,'archive':str(archive),'archive_sha256':stage.sha(archive.read_bytes()),
        'entries':{k:stage.sha(v) for k,v in entries.items()},'registry_before':{'path':str(base),'files':before},'registry_after':after,
        'config_before_sha256':stage.sha(config.read_bytes()),'config_after_sha256':stage.sha((out/'RetailCasc.ini').read_bytes()),
        'backpacks':100,'new_items':[3400100,3410100,3430001],'sql':str(sqlpath),'sql_sha256':stage.sha(sql.encode()),
        'state':'additive model/item integration staged, old content preserved and MPQ readback verified'}
    (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps({k:receipt[k] for k in ['revision','backpacks','new_items','state']}))

if __name__=='__main__':build(Path(sys.argv[1] if len(sys.argv)>1 else 'G:/FortniteCosmetics/wolf-rainbow-integration-v1'))
