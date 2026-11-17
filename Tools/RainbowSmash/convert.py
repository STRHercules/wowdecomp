"""Convert the supplied Rainbow Smash to a Wrath two-handed weapon, keeping its authored grip origin."""
import json,hashlib,struct,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Tools/FortniteCosmetics'))
import convert_backpacks as converter

def build(out):
    source=Path('G:/FortnitePorter/Exported/BRCosmetics/Weapons/FORT_Melee/Pickaxe_29')
    mesh_path=source/'Meshes/SK_Pickaxe_029.uemodel'
    mesh=converter.read_mesh(mesh_path.read_bytes())
    norm=mesh['source_normalization']
    # read_mesh's handedness/UV conversion is reusable; a weapon keeps origin 0 at its authored grip.
    shift=[norm['front_cm']/100,norm['center_cm']/100,norm['top_cm']/100]
    mesh['vertices']=[tuple(v[k]+shift[k] for k in range(3)) for v in mesh['vertices']]
    # Stock Axe_2H_Battle_B_01 runs along +X (thin Y), whereas Fortnite's shaft runs along +Z.
    mesh['vertices']=[(v[2],v[1],-v[0]) for v in mesh['vertices']]
    mesh['normals']=[(n[2],n[1],-n[0]) for n in mesh['normals']]
    base='Item\\ObjectComponents\\Weapon\\Esteria_RainbowSmash'
    model,skin,dimensions=converter.write_model(mesh,[base+'.blp'])
    skin=bytearray(skin);_,sections=struct.unpack_from('<II',skin,28)
    struct.pack_into('<I',skin,sections,0)
    converter.verify(model,skin)
    texture=converter.texture_bytes(source/'Textures/T_Pickaxe_29_D.png')
    entries={base+'.m2':model,base+'00.skin':bytes(skin),base+'.blp':texture}
    for name,data in entries.items():
        target=out/'patch-root'/Path(name.replace('\\','/'));target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
    report={'name':'Rainbow Smash','model':base.replace('\\','/')+'.m2','model_name':'Esteria_RainbowSmash.m2',
      'model_texture':'Esteria_RainbowSmash','vertices':len(mesh['vertices']),'triangles':len(mesh['indices'])//3,
      'bounds_dimensions':dimensions,'grip':'original grip origin; +90 degrees around Y maps shaft +Z to stock axe +X',
      'entries':{n:hashlib.sha256(d).hexdigest() for n,d in entries.items()},
      'source_sha256':hashlib.sha256(mesh_path.read_bytes()).hexdigest(),
      'losses':['Unreal reactive horn/material effects and harvest sounds not implemented; uses player weapon animations'],
      'state':'converted and reparsed; equipped grip acceptance pending'}
    (out/'catalog.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))

if __name__=='__main__':build(Path(sys.argv[1] if len(sys.argv)>1 else 'G:/FortniteCosmetics/rainbow-smash-v1'))
