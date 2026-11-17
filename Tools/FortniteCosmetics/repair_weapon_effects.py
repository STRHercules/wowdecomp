"""Native looping sparkle approximations for the exported shader/VFX surfaces; no Unreal particle graph is available."""
import json
import math
from pathlib import Path
import struct
import sys

from PIL import Image
import convert_backpacks as c
from revise_collection import WORK

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'WolfPup'))
from build_model import build
from verify_animation import verify

def sparkle_weapon(row):
    out=WORK/'repairs'/str(row['display']);out.mkdir(parents=True,exist_ok=True)
    source=Path('G:/FortnitePorter/Exported')/row['source'];m=c.read_mesh(source.read_bytes());n=m['source_normalization']
    shift=[n['front_cm']/100,n['center_cm']/100,n['top_cm']/100]
    v=[tuple(p[k]+shift[k] for k in range(3)) for p in m['vertices']]
    v=[(p[2],p[1],-p[0]) for p in v];normals=[(p[2],p[1],-p[0]) for p in m['normals']]
    # The glitter cone is a shader volume, never opaque body geometry.
    indices=m['indices'][:m['materials'][0]['faces']*3]
    body=dict(name='weapon',vertices=v,normals=normals,uvs=m['uvs'],weights=[[[0,1]] for _ in v],
              triangles=[indices[i:i+3] for i in range(0,len(indices),3)])
    head=[v[i] for i in set(indices) if v[i][0]>max(p[0] for p in v)-.18]
    center=[sum(p[k] for p in head)/len(head) for k in range(3)]
    vertices=[];uvs=[];triangles=[];weights=[];pivots=[]
    for i in range(12):
        angle=i*2*math.pi/12;radius=.035+.012*(i%3)
        point=[center[0]+.03*math.sin(i*2.3),center[1]+radius*math.cos(angle),center[2]+radius*math.sin(angle)]
        pivots.append(point);start=len(vertices);size=.008 if row['display']==3430023 else .012
        for a,b in ((-1,0),(0,1),(1,0),(0,-1)):
            vertices.append([point[0]+a*size,point[1]+b*size,point[2]]);weights.append([[i+1,1]]);uvs.append([.5+a*.5,.5+b*.5])
        triangles.extend([[start,start+1,start+2],[start,start+2,start+3],[start+2,start+1,start],[start+3,start+2,start]])
    sparks=dict(name='sparkles',vertices=vertices,normals=[[0,0,1]]*len(vertices),uvs=uvs,weights=weights,triangles=triangles)
    frames=[]
    for frame in range(31):
        t=frame/30;sample=[[[0,0,0],[0,0,0,1],[1,1,1]]]
        for i in range(12):
            phase=(t+i/12)%1;scale=max(.015,math.sin(phase*math.pi)**5)
            sample.append([[.025*math.sin(t*2*math.pi+i),.012*math.sin(t*2*math.pi+i*2),.025*math.cos(t*2*math.pi+i)],
                           [0,0,math.sin(t*math.pi),math.cos(t*math.pi)],[scale]*3])
        frames.append(sample)
    texture=out/'spark.png';Image.new('RGB',(4,4),(255,205,70) if row['display']==3430023 else (255,235,155)).save(texture)
    base='custom\\fortnite\\weapons\\fx_'+str(row['display'])
    baked=dict(meshes=[body,sparks],bones=[dict(name='root',pivot=[0,0,0])]+[dict(name='spark_'+str(i),pivot=p) for i,p in enumerate(pivots)],
               clips=[dict(name='Looping sparkles',frames=31,fps=30,transforms=frames,sample_max_position_error=0,animated_shape_keys=[])],
               facial_helper_count=0,asset=dict(slug='weapon_'+str(row['display']),name=row['name'],model=row['model'],
                    texture_paths=[base+'_body.blp',base+'_spark.blp'],textures=[row['materials'][0]['source'],str(texture)]))
    (out/'baked.json').write_text(json.dumps(baked,separators=(',',':')));build(out)
    model=out/'patch-root'/row['model'];blob=bytearray(model.read_bytes());count,offset=struct.unpack_from('<II',blob,112)
    assert count==2
    struct.pack_into('<HH',blob,offset,0,0)
    struct.pack_into('<HH',blob,offset+4,5,4) # unlit, double-sided additive sparkle surface
    model.write_bytes(blob);verify(out)
    for p in (out/'patch-root').rglob('*'):
        if p.is_file():
            target=WORK/'repairs/patch-root'/p.relative_to(out/'patch-root');target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(p.read_bytes())
    return dict(display=row['display'],repair='native looping additive sparkles'+('; opaque glitter cone removed' if row['display']==3430068 else ''),
                limitation='visual approximation; Fortnite Niagara/material graph was not exported')

def main():
    rows=json.loads((WORK/'catalog.json').read_text());reports=[]
    for row in rows:
        if row['display'] in (3430023,3430068):reports.append(sparkle_weapon(row))
    green=WORK/'repairs/green.png';green.parent.mkdir(parents=True,exist_ok=True);Image.new('RGB',(4,4),(0,255,0)).save(green)
    row=next(r for r in rows if r['display']==3430047)
    # Both exported Neon surfaces are procedural; replace each with the requested solid green material.
    model=Path('G:/FortniteCosmetics/collection-20261114/weapons/patch-root')/row['model'];data=bytearray(model.read_bytes())
    count,offset=struct.unpack_from('<II',data,80)
    for i in range(count):
        name='custom\\fortnite\\weapons\\vibe_green.blp'
        c.append_array(data,offset+16*i+8,len(name)+1,name.encode()+b'\0')
        target=WORK/'repairs/patch-root'/name.replace('\\','/');target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(c.texture_bytes(green))
    target=WORK/'repairs/patch-root'/row['model'];target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
    reports.append(dict(display=3430047,repair='all material surfaces solid green'))
    (WORK/'repairs/receipt.json').write_text(json.dumps(reports,indent=2)+'\n')
    print(json.dumps(reports))

if __name__=='__main__':main()
