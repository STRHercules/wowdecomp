"""Build collection catalogs from the effective client tables, live realm mount models, and curated Fortnite weapons."""
import json,re,sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(Path(__file__).parent))
import stage

OUT=Path('G:/FortniteCosmetics/collections-20261114')

def generate():
    stage.mc.LAYOUT['SkillLineAbility']=(14,())
    OUT.mkdir(exist_ok=True);client=stage.mc.Client(Path('G:/3.3.5a - Dev'),True)
    try:
        spells=client.table('Spell').rows;icons=client.table('SpellIcon').rows
        skill=client.table('SkillLineAbility').rows
        displays=client.table('CreatureDisplayInfo').rows;models=client.table('CreatureModelData').rows
        candidates={r[2] for r,_ in skill.values() if r[1]==777}
        sources={}
        raw=stage.db.mysql('SELECT entry,name,spellid_1,spellid_2 FROM item_template WHERE (`class`=15 AND subclass=5) OR spellid_1 IN (483,55884)','acore_world')
        for line in raw.splitlines():
            item,name,a,b=line.split('\t');a,b=int(a),int(b);spell=b if a in (483,55884) else a
            if spell>0:candidates.add(spell);sources.setdefault(spell,(int(item),name))
        overrides={}
        raw=stage.db.mysql('SELECT ID,EffectAura_1,EffectAura_2,EffectAura_3,EffectMiscValue_1,EffectMiscValue_2,EffectMiscValue_3 FROM spell_dbc','acore_world')
        for line in raw.splitlines():
            values=list(map(int,line.split('\t')));overrides[values[0]]=(values[1:4],values[4:7])
        creature_models={}
        raw=stage.db.mysql('SELECT CreatureID,CreatureDisplayID FROM creature_template_model ORDER BY CreatureID,Probability DESC,Idx','acore_world')
        for line in raw.splitlines():
            creature,display=map(int,line.split('\t'));creature_models.setdefault(creature,display)
        mounts=[]
        for spell in sorted(candidates):
            if spell not in spells:continue
            row,text=spells[spell];auras,misc=overrides.get(spell,(row[95:98],row[110:113]))
            mount=next((m for a,m in zip(auras,misc) if a==78),None)
            if mount is None:continue
            name=text.get(136)
            if not name:continue
            display=creature_models.get(mount,0);model=None
            if display in displays:
                modelid=displays[display][0][1]
                if modelid in models:model=models[modelid][1].get(2)
            if model and model.lower().endswith('.mdx'):model=model[:-4]+'.m2'
            icon=icons.get(row[133],(None,{}))[1].get(1,'Interface\\Icons\\Ability_Mount_RidingHorse')
            fields=dict(kind='mount',spell=spell,name=name,icon=icon,mountType='Flying' if 207 in auras else 'Ground')
            if model:fields.update(model=model,creatureDisplay=display)
            if spell in sources:fields.update(item=sources[spell][0],sourceName=sources[spell][1])
            mounts.append(fields)
        hearths=[]
        raw=stage.db.mysql('SELECT entry,name,spellid_1,displayid FROM item_template WHERE entry=6948 OR entry BETWEEN 1002337 AND 1002340','acore_world')
        visuals=client.table('SpellVisual').rows;kits=client.table('SpellVisualKit').rows;effects=client.table('SpellVisualEffectName').rows
        attaches=client.table('SpellVisualKitModelAttach').rows
        itemdisplays=client.table('ItemDisplayInfo').rows
        for line in raw.splitlines():
            item,name,spell,display=line.split('\t');item,spell,display=int(item),int(spell),int(display)
            row,text=spells[spell];model=None
            if row[131] in visuals:
                visual=visuals[row[131]][0]
                choices=[]
                for kit in (visual[1],visual[4],visual[6],visual[2]):
                    if kit not in kits:continue
                    for attach,_ in attaches.values():
                        if attach[1]==kit:
                            path=effects.get(attach[2],(None,{}))[1].get(2)
                            if path:choices.append(path)
                    for effect in [kits[kit][0][14]]+kits[kit][0][3:14]:
                        path=effects.get(effect,(None,{}))[1].get(2)
                        if path:choices.append(path)
                model=next((p for p in choices if 'portal' in p.lower()),choices[0] if choices else None)
            fields=dict(kind='hearth',id=item,item=item,spell=spell,name=name,
                icon='Interface\\Icons\\'+itemdisplays.get(display,(None,{}))[1].get(5,'INV_Misc_Rune_01'))
            if model:fields['model']=model[:-4]+'.m2' if model.lower().endswith('.mdx') else model
            hearths.append(fields)
        pathmap={int(i):p for i,p in re.findall(r'\{(\d+),"([^"]+)",\d+\}',(ROOT/'Engine/Source/Retail/PickaxeCatalog.h').read_text())}
        from lupa import LuaRuntime
        lua=LuaRuntime();lua.execute((ROOT/'Addons/EsteriaCosmetics/EsteriaPickaxesData.lua').read_text())
        weapons=[]
        for _,weapon in lua.globals().EsteriaPickaxesData.items():
            display=int(weapon['display']);icon=itemdisplays.get(display,(None,{}))[1].get(5,'INV_Axe_09')
            weapons.append(dict(kind='weapon',id=display,display=display,name=weapon['name'],model=pathmap[display],
                                slot=int(weapon['slot']),weaponType=weapon['weaponType'],icon='Interface\\Icons\\'+icon))
        assert len(weapons)==63 and len(hearths)==5 and mounts
        result=dict(weapons=weapons,mounts=mounts,hearths=hearths)
        (OUT/'catalog.json').write_text(json.dumps(result,indent=2)+'\n')
        lines=['-- Generated by Tools/CosmeticsPack/generate_collections.py from effective realm/client data.']
        for label,rows in (('EsteriaCollectionsWeapons',weapons),('EsteriaCollectionsMounts',mounts),('EsteriaCollectionsHearths',hearths)):
            lines.append(label+' = {')
            for row in rows:lines.append('    {'+','.join(k+'='+json.dumps(v,ensure_ascii=False) for k,v in row.items())+'},')
            lines.append('}')
        (ROOT/'Addons/EsteriaCosmetics/EsteriaCollectionsData.lua').write_text('\n'.join(lines)+'\n')
        header=['#pragma once','#include <array>','namespace EsteriaCollections {']
        for label,ids in (('Weapons',[r['id'] for r in weapons]),('Mounts',[r['spell'] for r in mounts]),('Hearths',[r['item'] for r in hearths])):
            header.append('inline constexpr std::array<uint32, '+str(len(ids))+'> '+label+' = {')
            header += ['    '+','.join(map(str,ids[i:i+12]))+',' for i in range(0,len(ids),12)]
            header.append('};')
        header.append('}')
        (stage.SERVER/'modules/mod-transmog/src/CollectionsCatalog.h').write_bytes(('\n'.join(header)+'\n').encode())
        print('GENERATED',len(mounts),'mounts;',sum(bool(r.get('model')) for r in mounts),'3D mount models;',len(hearths),'hearthstones;',len(weapons),'Fortnite weapons')
        print('Hearth previews',[(r['name'],r.get('model')) for r in hearths])
    finally:client.archives.close()

if __name__=='__main__':generate()
