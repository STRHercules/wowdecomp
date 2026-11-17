"""Source-texture-driven fire/plasma loops for BlessFlan and ClaimReflect, encoded as native M2 UV animation."""
import hashlib
import io
import json
import math
from pathlib import Path
import struct
import sys

import numpy as np
from PIL import Image,ImageDraw,ImageFilter
import convert_backpacks as c

ROOT=Path(__file__).resolve().parents[2]
SOURCE=Path('G:/FortnitePorter/Exported')
WORK=Path('G:/FortniteCosmetics/elemental-swords-20261114')
FRAMES=64
PERIOD=2000
TILE=128
GRID=8
ASSETS=[
    dict(display=3430084,name='Swords of Fire and Steel (Main Hand)',family='Pickaxe_BlessFlan',
         source='Meshes/BlessFlan_Axe.uemodel',inventory=21,slot=15,effect='fire'),
    dict(display=3430085,name='Swords of Fire and Steel (Off Hand)',family='Pickaxe_BlessFlan',
         source='Meshes/Offhand/BlessFlan_Offhand_Axe.uemodel',inventory=22,slot=16,effect='fire'),
    dict(display=3430086,name='Halo Energy Sword',family='Pickaxe_ClaimReflect',
         source='Meshes/ClaimReflect_Axe.uemodel',inventory=13,slot=15,effect='plasma')]

def image_array(path):
    return np.asarray(Image.open(path).convert('RGB').resize((TILE,TILE),Image.Resampling.BILINEAR),dtype=np.float64)/255

def sample_wrap(image,u,v):
    h,w=image.shape[:2];x=(u%1)*w;y=(v%1)*h
    ix=np.floor(x).astype(int)%w;iy=np.floor(y).astype(int)%h
    dx=(x-ix)[...,None];dy=(y-iy)[...,None]
    return ((image[iy,ix]*(1-dx)+image[iy,(ix+1)%w]*dx)*(1-dy)+
            (image[(iy+1)%h,ix]*(1-dx)+image[(iy+1)%h,(ix+1)%w]*dx)*dy)

def effect_frame(kind,phase,inputs):
    v,u=np.mgrid[0:TILE,0:TILE]/TILE
    if kind=='fire':
        noise=inputs['noise'];mask=inputs['mask']
        a=sample_wrap(noise,v-2*phase,u+phase)[...,0]
        b=sample_wrap(mask,u-phase+.07*np.sin(2*math.pi*(v+phase)),v+phase)[...,0]
        energy=np.clip((a*.7+b*.3-(.25+.3*v))*3,0,1)
        edge=np.maximum(0,np.sin(math.pi*u))**.75*np.maximum(0,np.sin(math.pi*v))**1.2
        alpha=np.clip(energy**1.6*edge,0,1)
        rgb=np.stack((np.ones_like(energy),.1+.55*energy,.015+.15*energy**3),axis=-1)*alpha[...,None]
    else:
        flow=inputs['flow'];noise=inputs['mask'];base=inputs['emissive']
        x=flow[...,0]*2-1;y=flow[...,1]*2-1
        a=sample_wrap(noise,u+phase+.16*x*np.sin(2*math.pi*phase),v-phase+.16*y*np.cos(2*math.pi*phase))
        b=sample_wrap(noise,u-2*phase,v+phase)
        pulse=.2+.8*np.clip(a[...,0]*.65+b[...,1]*.35,0,1)
        alpha=np.clip(base.max(2)*1.8,0,1)
        rgb=base*(pulse[...,None]*1.5)+np.maximum(a[...,0]-.72,0)[...,None]*base.max(2)[...,None]*1.8
        rgb=np.clip(rgb,0,1)*alpha[...,None]
    return np.uint8(np.clip(np.dstack((rgb,alpha)),0,1)*255)

def atlas(kind,inputs):
    canvas=Image.new('RGBA',(TILE*GRID,TILE*GRID))
    samples=[]
    for i in range(FRAMES):
        frame=Image.fromarray(effect_frame(kind,i/FRAMES,inputs),'RGBA');samples.append(np.asarray(frame))
        canvas.paste(frame,(i%GRID*TILE,i//GRID*TILE))
    assert np.abs(samples[0].astype(float)-samples[5]).mean()>.5,'Effect is static'
    np.testing.assert_allclose(effect_frame(kind,0,inputs),effect_frame(kind,1,inputs),atol=1)
    return canvas

def rgba_blp(image):
    blob=bytearray(1172);struct.pack_into('<4sI4BII',blob,0,b'BLP2',1,3,8,8,1,*image.size)
    for mip in range(16):
        data=image.tobytes('raw','BGRA');struct.pack_into('<I',blob,20+4*mip,len(blob));struct.pack_into('<I',blob,84+4*mip,len(data));blob.extend(data)
        if image.size==(1,1):break
        image=image.resize((max(1,image.width//2),max(1,image.height//2)),Image.Resampling.BOX)
    return bytes(blob)

def read_original(path):
    mesh=c.read_mesh(path.read_bytes());n=mesh['source_normalization']
    shift=[n['front_cm']/100,n['center_cm']/100,n['top_cm']/100]
    mesh['vertices']=[tuple(v[k]+shift[k] for k in range(3)) for v in mesh['vertices']]
    return mesh

def orient(mesh,kind):
    transform=(lambda p:(p[2],p[1],-p[0])) if kind=='fire' else (lambda p:(-p[1],-p[2],p[0]))
    mesh['vertices']=[transform(p) for p in mesh['vertices']];mesh['normals']=[transform(p) for p in mesh['normals']]
    return mesh

def uv_track(model,effect_materials):
    c.append_array(model,20,1,struct.pack('<I',PERIOD))
    c.append_array(model,96,1,bytes(60));offset=struct.unpack_from('<I',model,100)[0]
    struct.pack_into('<HH',model,offset,0,0)
    for channel in (20,40):struct.pack_into('<HH',model,offset+channel,0,65535)
    for field in (4,12):c.append_array(model,offset+field,1,bytes(8))
    times=struct.unpack_from('<I',model,offset+8)[0];keys=struct.unpack_from('<I',model,offset+16)[0]
    timestamps=[i*PERIOD//FRAMES for i in range(FRAMES+1)]
    translations=[(i%FRAMES%GRID/GRID,i%FRAMES//GRID/GRID,0) for i in range(FRAMES+1)]
    c.append_array(model,times,len(timestamps),struct.pack('<'+'I'*len(timestamps),*timestamps))
    c.append_array(model,keys,len(translations),b''.join(struct.pack('<3f',*v) for v in translations))
    count,combos=struct.unpack_from('<II',model,152)
    for i in effect_materials:assert i<count;struct.pack_into('<H',model,combos+2*i,0)

def verify_animation(model,skin,effect_materials):
    c.verify(model,skin)
    assert struct.unpack_from('<I',model,96)[0]==1
    offset=struct.unpack_from('<I',model,100)[0];assert struct.unpack_from('<HH',model,offset)==(0,0)
    loops=struct.unpack_from('<I',model,24)[0];assert struct.unpack_from('<I',model,loops)[0]==PERIOD
    outer=struct.unpack_from('<I',model,offset+8)[0];n,p=struct.unpack_from('<II',model,outer)
    assert n==FRAMES+1 and struct.unpack_from('<'+'I'*n,model,p)==tuple(i*PERIOD//FRAMES for i in range(FRAMES+1))
    outer=struct.unpack_from('<I',model,offset+16)[0];n,p=struct.unpack_from('<II',model,outer)
    keys=[struct.unpack_from('<3f',model,p+i*12) for i in range(n)];assert keys[0]==keys[-1] and len(set(keys))==FRAMES
    combos=struct.unpack_from('<I',model,156)[0];materials=struct.unpack_from('<I',model,116)[0]
    for i in effect_materials:
        assert struct.unpack_from('<H',model,combos+2*i)[0]==0
        assert struct.unpack_from('<HH',model,materials+4*i)==(5,4)
    return dict(frames=FRAMES,duration_ms=PERIOD,global_loop=True,animated_materials=effect_materials)

def build(row):
    family=SOURCE/'BRCosmetics/Weapons/FORT_Melee'/row['family'];kind=row['effect'];out=WORK/str(row['display']);out.mkdir(exist_ok=True)
    path=family/row['source'];mesh=orient(read_original(path),kind);textures=[];flags=[];effect_materials=[]
    if kind=='fire':
        body=family/'Textures/T_BlessFlan_Pickaxe_D.png';textures=[body];flags=[(0,0)]
        inputs=dict(noise=image_array(family/'FX/T_Pickaxe_BlessFlan_FireNoise.png'),mask=image_array(family/'FX/T_Pickaxe_BlessFlan_Mask.png'))
        effects=['SM_Pickaxe_BlessFlan_Idleglow01.uemodel','SM_Pickaxe_BlessFlan_Idleglow02.uemodel'] if row['slot']==15 else ['SM_Pickaxe_BlessFlan_Knifeglow01.uemodel','SM_Pickaxe_BlessFlan_Knifeglow02.uemodel']
        for filename in effects:
            fx=read_original(family/'FX'/filename)
            # Exported effect meshes run along local X; the skeletal weapon's blade runs along local Z.
            fx['vertices']=[(p[1],p[2],-p[0]) for p in fx['vertices']];fx['normals']=[(p[1],p[2],-p[0]) for p in fx['normals']]
            low=[min(p[k] for p in fx['vertices']) for k in (0,1)];high=[max(p[k] for p in fx['vertices']) for k in (0,1)]
            uv=[((p[0]-low[0])/(high[0]-low[0]),(p[1]-low[1])/(high[1]-low[1])) for p in fx['vertices']]
            fx['vertices']=[(.1+(p[0]-low[0])/(high[0]-low[0])*.61,p[1]*.5,p[2]) for p in fx['vertices']]
            start=len(mesh['vertices']);first=len(mesh['indices'])
            mesh['vertices']+=fx['vertices'];mesh['normals']+=fx['normals']
            mesh['uvs'] += [((u*(TILE-4)+2)/(TILE*GRID),(v*(TILE-4)+2)/(TILE*GRID)) for u,v in uv]
            mesh['indices'] += [i+start for i in fx['indices']]
            mesh['materials'].append(dict(name=filename,first_index=first,faces=len(fx['indices'])//3))
            effect_materials.append(len(textures));textures.append(None);flags.append((5,4))
    else:
        blade=mesh['materials'][0];indices=mesh['indices'][blade['first_index']:blade['first_index']+blade['faces']*3]
        used=sorted(set(indices));low=[min(mesh['vertices'][i][k] for i in used) for k in (0,1)];high=[max(mesh['vertices'][i][k] for i in used) for k in (0,1)]
        domain={i:((mesh['vertices'][i][1]-low[1])/(high[1]-low[1]),1-(mesh['vertices'][i][0]-low[0])/(high[0]-low[0])) for i in used}
        coverage=Image.new('L',(TILE,TILE));draw=ImageDraw.Draw(coverage)
        for j in range(0,len(indices),3):draw.polygon([(domain[i][0]*(TILE-1),domain[i][1]*(TILE-1)) for i in indices[j:j+3]],fill=255)
        mask=np.asarray(coverage,dtype=float)/255
        edge=(mask-np.asarray(coverage.filter(ImageFilter.MinFilter(5)),dtype=float)/255)
        source_emissive=image_array(family/'Textures/T_ClaimReflect_Pickaxe_E.png')
        palette=source_emissive[source_emissive.max(2)>.3].mean(0);palette/=max(palette)
        emissive=np.clip(palette[None,None,:]*(.65+.35*edge[...,None])*mask[...,None],0,1)
        base=out/'blade-emissive.png';Image.fromarray(np.uint8(emissive*160),'RGB').save(base)
        textures=[base,family/'Textures/T_ClaimReflect_Pickaxe_D.png'];flags=[(5,4),(0,0)]
        inputs=dict(flow=image_array(family/'Materials/T_Pickaxe_ClaimReflect_FlowMap.png'),mask=image_array(family/'Materials/T_Pickaxe_ClaimReflect_Mask.png'),emissive=emissive)
        # Isolate the shader blade's UVs; hilt/body surfaces keep their original diffuse atlas coordinates.
        mapping={i:len(mesh['vertices'])+j for j,i in enumerate(used)}
        mesh['vertices'] += [mesh['vertices'][i] for i in used];mesh['normals'] += [mesh['normals'][i] for i in used];mesh['uvs'] += [domain[i] for i in used]
        mesh['indices'][blade['first_index']:blade['first_index']+blade['faces']*3]=[mapping[i] for i in indices]
        indices=[mapping[i] for i in indices]
        start=len(mesh['vertices']);first=len(mesh['indices']);count=start
        mesh['vertices'] += [tuple(v[k]+mesh['normals'][i][k]*.0007 for k in range(3)) for i,v in enumerate(mesh['vertices'][:count])]
        mesh['normals']+=mesh['normals'][:count]
        mesh['uvs'] += [((u%1*(TILE-4)+2)/(TILE*GRID),(v%1*(TILE-4)+2)/(TILE*GRID)) for u,v in mesh['uvs'][:count]]
        mesh['indices'] += [i+start for i in indices];mesh['materials'].append(dict(name='Animated plasma',first_index=first,faces=len(indices)//3))
        effect_materials=[2];textures.append(None);flags.append((5,4))
    sheet=atlas(kind,inputs);sheet.save(out/'effect-atlas.png')
    names=[f'custom\\fortnite\\elemental\\{row["display"]}_{i}.blp' for i in range(len(textures))]
    model,skin,dimensions=c.write_model(mesh,names);model=bytearray(model);skin=bytearray(skin)
    n,p=struct.unpack_from('<II',skin,28)
    for i in range(n):struct.pack_into('<I',skin,p+i*48,0)
    _,p=struct.unpack_from('<II',model,112)
    for i,values in enumerate(flags):struct.pack_into('<HH',model,p+i*4,*values)
    seq=struct.unpack_from('<I',model,32)[0];struct.pack_into('<I',model,seq+4,PERIOD)
    uv_track(model,effect_materials);check=verify_animation(model,skin,effect_materials)
    model_path=f'Item/ObjectComponents/Weapon/Esteria_Fortnite_{row["display"]}.m2'
    entries={model_path.replace('/','\\'):model,(model_path[:-3]+'00.skin').replace('/','\\'):skin}
    entries.update({name:(rgba_blp(sheet) if source is None else c.texture_bytes(source)) for name,source in zip(names,textures)})
    for name,data in entries.items():
        target=WORK/'patch-root'/name.replace('\\','/');target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
    report=dict(row,source=str(path.relative_to(SOURCE)),model=model_path,vertices=len(mesh['vertices']),triangles=len(mesh['indices'])//3,
                animation=check,textures=[str(p) for p in textures if p],entries={n:hashlib.sha256(d).hexdigest() for n,d in entries.items()},
                dimensions=dimensions,limitations=['Reconstructed material motion from exported textures; original Unreal material/particle graphs unavailable'])
    (out/'catalog.json').write_text(json.dumps(report,indent=2)+'\n');return report

if __name__=='__main__':
    WORK.mkdir(exist_ok=True);rows=[build(row) for row in ASSETS]
    (WORK/'catalog.json').write_text(json.dumps(rows,indent=2)+'\n')
    print(json.dumps([dict(name=r['name'],display=r['display'],vertices=r['vertices'],animation=r['animation']) for r in rows]))
