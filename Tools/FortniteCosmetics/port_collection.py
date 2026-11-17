"""Convert the exported pet/weapon collection using the accepted Wolf Pup and Rainbow Smash pipelines."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys

import convert_backpacks as c

ROOT=Path(__file__).resolve().parents[2]
SOURCE=Path('G:/FortnitePorter/Exported')
BLENDER=Path('C:/Program Files/Blender Foundation/Blender 5.2/blender.exe')
PETS={
    'cat':('Cat','Cat','Backpack_Pet_Cat','Cat','Cat'),
    'bonesy':('Bonesy','Dog_Bonesy','BackPack_Pet','Dog','Dog'),
    'woodsy':('Woodsy','Dog_Woodsy','Backpack_Pet_Woodsy','Woodsy','Woodsy'),
    'dragon':('Dragon (Blue/Yellow)','Dragon','BackPack_Pet','Dragon','Dragon'),
    'fox':('Fox','Fox','Backpack_Pet_Fox','Fox','Fox'),
    'drift_fox':('Drift Fox','Drift_Fox','Backpack_Pet_Drift_Fox','Fox','Drift_Fox'),
    'hamster':('Hamster','Hamster','Backpack_Pets_Hamster','Hamster','Hamster'),
    'robo_kitty':('Robo Kitty','Robo_kitty','Backpack_Pet_RoboKitty','Cat',None),
}
PET_TEXTURES={
    'Dog_Woodsy':{'MI_Pets_Woodsy':'Textures/T_Pet_Woodsy_D.png'},
    'Dragon':{'MI_Pets_Dragon_Body_VariantA':'Textures/T_Pet_Dragon_BlueYellow_D.png'},
    'Fox':{'Pets_Fox':'Textures/T_Pet_Fox_D.png'},
    'Robo_kitty':{'MI_Pets_Robo_KittyFace':'Textures/FaceShapes.png'},
}
WEAPON_COSMETICS={
    'SK_PickAxe_Jaws':'Pickaxe_ID_017_Shark',
    'SK_Pickaxe_Skull':'Pickaxe_Deathvalley',
    'Cyber_Armor_Female_Axe':'Pickaxe_ID_768_CyberArmorFemale',
    'Desert_Shadow_Male_Axe_L':'Pickaxe_ID_843_DesertShadowMale1H',
    'Desert_Shadow_Male_Axe_R':'Pickaxe_ID_843_DesertShadowMale1H',
    'HighTower_Tapas_Axe':'Pickaxe_ID_458_HightowerTapas1H',
    'Slither_Female_Axe':'Pickaxe_ID_733_SlitherFemale_M1YCL',
    'Tiramisu_Male_Axe_R':'Pickaxe_ID_530_TiramisuMale1H_R94F2',
    'SK_Pickaxe_Viking':'Pickaxe_ID_070_Viking',
    'Wombat_Female_Axe':'Pickaxe_ID_532_WombatFemale_CWE2D',
    'ShrimpStroll_OffHand_Axe':'Pickaxe_ShrimpStroll',
    'SteamPower_Blue_Axe':'Pickaxe_SteamPower',
}

def write_json(path,value):
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(value,indent=2)+'\n')

def prepare(out):
    out.mkdir(parents=True,exist_ok=True)
    deps=c.export_dependencies(Path('C:/Users/Zach/AppData/Local/FortnitePorting/Logs'))
    for slug,(name,family,carrier,anim,pose) in PETS.items():
        pet_root=SOURCE/'BRCosmetics/Characters/Pets'/family
        pet=next(pet_root.rglob('*.uemodel'))
        pack=next((SOURCE/'BRCosmetics/Accessories/FORT_Backpacks'/carrier).rglob('*.uemodel'))
        textures=[];slots=[]
        for path in (pet,pack):
            ids=[]
            for material in c.read_mesh(path.read_bytes())['materials']:
                override=PET_TEXTURES.get(family,{}).get(material['name']) if path==pet else None
                texture=pet_root/override if override else c.texture_candidates(SOURCE,path,material,deps)[0]
                if not texture or not texture.is_file():raise ValueError(f'{path}: missing {material}')
                ids.append(len(textures));textures.append(str(texture))
            slots.append(ids)
        idle=SOURCE/'Game/Animation/Game/Pets'/anim/'Backpack/Idle'
        clips=sorted(p for p in idle.glob('*.ueanim') if p.stem=='Idle' or p.stem.startswith('IdleBreak_'))
        clips.sort(key=lambda p:(p.stem!='Idle',p.name))
        if slug=='robo_kitty':clips.append(SOURCE/'Game/Animation/Game/Pets/Robo_Kitty/Backpack/Idle/IdleBreak_01.ueanim')
        pose_path=SOURCE/'Game/Animation/Game/Pets'/pose/'Pet_FacialPose_Export_PoseAsset.uepose' if pose else None
        assert clips and all(p.is_file() for p in clips)
        assert pose_path is None or pose_path.is_file()
        spec=dict(slug='animated_'+slug,name=name,source=pet.relative_to(SOURCE).as_posix(),
                  carrier=pack.relative_to(SOURCE).as_posix(),clips=[p.relative_to(SOURCE).as_posix() for p in clips],
                  pose=pose_path.relative_to(SOURCE).as_posix() if pose_path else None,
                  pet_materials=slots[0],carrier_materials=slots[1],textures=textures,
                  texture_paths=[f'custom\\fortnite\\pets\\{slug}_{i}.blp' for i in range(len(textures))],
                  model=f'custom/fortnite/pets/{slug}.m2',family='pet',facial_bind_retarget=slug in ('fox','drift_fox','hamster','woodsy'),
                  losses=['Robo Kitty uses Cat base idles; animated Unreal face-atlas shader is not reproduced'] if slug=='robo_kitty' else [])
        write_json(out/'pets'/slug/'spec.json',spec)
    print('Prepared eight new animated pets; existing accepted Wolf Pup remains unchanged.')

def weapons(out):
    target=out/'weapons';target.mkdir(parents=True,exist_ok=True)
    id_path=Path(__file__).with_name('weapon-ids.json')
    ids=json.loads(id_path.read_text()) if id_path.exists() else {}
    sources=sorted(p for p in SOURCE.rglob('*.uemodel') if '/Weapons/' in p.as_posix())
    rainbow='BRCosmetics/Weapons/FORT_Melee/Pickaxe_29/Meshes/SK_Pickaxe_029.uemodel'
    ids.setdefault(rainbow,3430001)
    next_id=max(ids.values())+1
    for path in sources:
        key=path.relative_to(SOURCE).as_posix()
        if key not in ids:ids[key]=next_id;next_id+=1
    assert len(set(ids.values()))==len(ids) and all(3430001<=i<3440000 for i in ids.values())
    write_json(id_path,ids)
    deps=c.export_dependencies(Path('C:/Users/Zach/AppData/Local/FortnitePorting/Logs'))
    metadata_path=out/'pickaxe-metadata.json'
    metadata=json.loads(metadata_path.read_text())['data'] if metadata_path.exists() else []
    rows=[];cache={}
    for path in sources:
        key=path.relative_to(SOURCE).as_posix();display=ids[key]
        if display==3430001:
            rows.append(dict(source=key,display=display,name='Rainbow Smash',existing=True,model='Item/ObjectComponents/Weapon/Esteria_RainbowSmash.m2'));continue
        mesh=c.read_mesh(path.read_bytes());norm=mesh['source_normalization']
        shift=[norm['front_cm']/100,norm['center_cm']/100,norm['top_cm']/100]
        original=[tuple(v[k]+shift[k] for k in range(3)) for v in mesh['vertices']]
        mesh['vertices']=[(v[2],v[1],-v[0]) for v in original]
        mesh['normals']=[(n[2],n[1],-n[0]) for n in mesh['normals']]
        textures=[];paths=[]
        missing=[]
        for mat in mesh['materials']:
            tex,reason=c.texture_candidates(SOURCE,path,mat,deps)
            if not tex:
                # shortcut: shader-only Neon has no exported textures; retain geometry with neutral materials until its parameters are exported.
                if 'Pickaxe_Neon/' not in key:raise ValueError(f'{key}: missing {mat}')
                from PIL import Image
                tex=target/('neon_black.png' if mat['name'].endswith('_black') else 'neon_lines.png')
                Image.new('RGB',(4,4),(15,15,15) if mat['name'].endswith('_black') else (220,220,220)).save(tex)
                missing.append(mat['name'])
            digest=hashlib.sha256(tex.read_bytes()).hexdigest();texture=f'custom\\fortnite\\weapons\\{digest[:24]}.blp'
            if texture not in cache:cache[texture]=c.texture_bytes(tex)
            paths.append(texture);textures.append(dict(material=mat['name'],source=str(tex),reason=reason))
        model,skin,dimensions=c.write_model(mesh,paths);skin=bytearray(skin)
        count,sections=struct.unpack_from('<II',skin,28)
        for i in range(count):struct.pack_into('<I',skin,sections+48*i,0)
        c.verify(model,skin)
        base=f'Item/ObjectComponents/Weapon/Esteria_Fortnite_{display}'
        for suffix,payload in (('.m2',model),('00.skin',skin)):
            dest=target/'patch-root'/(base+suffix);dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(payload)
        name={'SK_StopAxe_StopAxe':'Stop Axe','SK_Pickaxe_Street_Basketball':'Slam Dunk'}.get(path.stem)
        family=next((p for p in Path(key).parts if p.lower().startswith('pickaxe_')),None)
        cosmetic=None
        if family:
            token=c.key(re.sub(r'(?i)^pickaxe_','',family))
            matches=[a for a in metadata if len(token)>=3 and not token.isdigit() and c.key(a['id']).endswith(token)]
            if len(matches)==1:cosmetic=matches[0];name=cosmetic['name']
        if path.stem in WEAPON_COSMETICS:
            cosmetic=next((a for a in metadata if a['id']==WEAPON_COSMETICS[path.stem]),None)
            if cosmetic:name=cosmetic['name']
        if not name:
            name=c.display_name(re.sub(r'(?i)^(SK_|SM_)?Pickaxe_','',path.stem))
        if re.search(r'(?i)(?:_axe)?_l(?:_axe)?$',path.stem):name+=' (Left)'
        elif re.search(r'(?i)(?:_axe)?_r(?:_axe)?$',path.stem):name+=' (Right)'
        if path.stem=='ShrimpStroll_OffHand_Axe':name+=' (Off-hand)'
        if path.stem=='SteamPower_Blue_Axe':name+=' (Blue)'
        rows.append(dict(source=key,source_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),display=display,
                         name=name,model=base+'.m2',vertices=len(mesh['vertices']),triangles=len(mesh['indices'])//3,
                         dimensions=dimensions,materials=textures,missing_shader_parameters=missing,
                         cosmetic_id=cosmetic['id'] if cosmetic else None,
                         losses=['Uses player 2H axe animations; Unreal reactive materials, cloth, VFX and harvest sounds are not reproduced']))
        print('WEAPON',display,name,flush=True)
    for name,payload in cache.items():
        dest=target/'patch-root'/name.replace('\\','/');dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(payload)
    write_json(target/'catalog.json',rows)
    header=['#pragma once','#include <cstdint>','#include <string_view>','namespace Retail {',
            'struct PickaxeAppearance { uint32_t display; std::string_view model; };',
            'inline constexpr PickaxeAppearance PickaxeAppearances[] = {']
    header += ['    {'+str(r['display'])+','+json.dumps(r['model'].lower())+'},' for r in rows]
    header += ['};','inline uint32_t PickaxeDisplay(std::string_view model) {',
               '    for (const auto& appearance : PickaxeAppearances) if (appearance.model == model) return appearance.display;',
               '    return 0;','}',
               'inline bool IsPickaxeDisplay(uint32_t display) {',
               '    for (const auto& appearance : PickaxeAppearances) if (appearance.display == display) return true;',
               '    return false;','}','}']
    (ROOT/'Engine/Source/Retail/PickaxeCatalog.h').write_text('\n'.join(header)+'\n')
    lua=['-- Generated by Tools/FortniteCosmetics/port_collection.py.','EsteriaPickaxesData = {']
    lua += ['    {display='+str(r['display'])+',name='+json.dumps(r['name'])+'},' for r in rows]
    (ROOT/'Addons/EsteriaCosmetics/EsteriaPickaxesData.lua').write_text('\n'.join(lua+['}'])+'\n')
    print('WEAPONS_CONVERTED',len(rows)-1,'existing',1,flush=True)

def pet(out,slug):
    target=out/'pets'/slug;raw=target/'raw';raw.mkdir(exist_ok=True)
    if slug=='woodsy':
        subprocess.run([str(BLENDER),'-b','--python-exit-code','1','-P',str(ROOT/'Tools/WolfPup/bake_woodsy_eyes.py'),
                        '--',str(target/'spec.json')],check=True)
    with (target/'blender.log').open('w') as log:
        subprocess.run([str(BLENDER),'-b','--python-exit-code','1','-P',str(ROOT/'Tools/WolfPup/export_blender.py'),
                        '--',str(raw),'--spec',str(target/'spec.json')],stdout=log,stderr=subprocess.STDOUT,check=True)
    subprocess.run([sys.executable,str(ROOT/'Tools/WolfPup/repair_normals.py'),str(raw),str(target)],check=True)
    subprocess.run([sys.executable,str(ROOT/'Tools/WolfPup/build_model.py'),str(target)],check=True)
    subprocess.run([sys.executable,str(ROOT/'Tools/WolfPup/verify_animation.py'),str(target)],check=True)
    print('PET_VERIFIED',slug,flush=True)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('step',choices=('prepare','weapons','pet'))
    parser.add_argument('--out',type=Path,default=Path('G:/FortniteCosmetics/collection-20261114'))
    parser.add_argument('--pet',choices=PETS)
    args=parser.parse_args()
    if args.step=='pet':pet(args.out,args.pet)
    elif args.step=='weapons':weapons(args.out)
    else:prepare(args.out)
