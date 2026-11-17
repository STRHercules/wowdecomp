"""Encode sampled source skinning and facial deformation as embedded Wrath animation tracks."""
import json,struct,sys,math,hashlib,zlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'Tools/FortniteCosmetics'),str(ROOT.parent/'WoW-Dance-Studio')]
from convert_backpacks import append_array,texture_bytes
from studio_core.m2_binary import compressed_quat

def continuous_quaternions(items):
    result=[]
    for value in items:
        length=math.sqrt(sum(v*v for v in value))
        if not math.isfinite(length) or length<1e-8:raise ValueError('Invalid source quaternion')
        q=[v/length for v in value]
        if result and sum(a*b for a,b in zip(result[-1],q))<0:q=[-v for v in q]
        result.append(q)
    return result

def sample_quaternions(times,items):
    items=continuous_quaternions(items)
    output_times=[times[0]];output=[items[0]]
    for i in range(1,len(items)):
        a,b=items[i-1],items[i]
        dot=max(-1,min(1,sum(x*y for x,y in zip(a,b))))
        angle=math.acos(dot)
        steps=min(max(1,times[i]-times[i-1]),max(1,math.ceil(angle/math.radians(7.5))))
        for step in range(1,steps+1):
            t=step/steps
            if angle>1e-6:
                q=[(x*math.sin((1-t)*angle)+y*math.sin(t*angle))/math.sin(angle) for x,y in zip(a,b)]
            else:q=[x*(1-t)+y*t for x,y in zip(a,b)]
            length=math.sqrt(sum(v*v for v in q));q=[v/length for v in q]
            output_times.append(round(times[i-1]+(times[i]-times[i-1])*t));output.append(q)
    return output_times,output

def playback_clip(clips):
    frames=[];times=[];sources=[];elapsed=0
    def append(time,frame):
        if times and time==times[-1]:frames[-1]=frame;return
        assert not times or time>times[-1]
        times.append(time);frames.append(frame)
    def transition(target,end):
        start=times[-1];original=frames[-1]
        for step in range(1,9):
            t=step/8;frame=[]
            for a,b in zip(original,target):
                qb=list(b[1])
                if sum(x*y for x,y in zip(a[1],qb))<0:qb=[-v for v in qb]
                q=[x*(1-t)+y*t for x,y in zip(a[1],qb)];length=math.sqrt(sum(v*v for v in q));q=[v/length for v in q]
                frame.append([[x*(1-t)+y*t for x,y in zip(a[0],b[0])],q,[x*(1-t)+y*t for x,y in zip(a[2],b[2])]])
            append(round(start+(end-start)*t),frame)
    for index,clip in enumerate(clips):
        if frames:transition(clip['transforms'][0],elapsed)
        first=len(frames)-1 if frames else 0
        for frame,sample in enumerate(clip['transforms']):append(elapsed+round(frame/clip['fps']*1000),sample)
        sources.append({'name':clip['name'],'first_key':first,'frames':clip['frames']})
        elapsed+=round(clip['frames']/clip['fps']*1000)+250
    transition(clips[0]['transforms'][0],elapsed)
    return dict(name='Full idle cycle',frames=len(frames),fps=30,times=times,duration=elapsed,transforms=frames,sources=sources)

def track(buf,header,series,kind):
    struct.pack_into('<HH',buf,header,1,65535)
    n=len(series)
    append_array(buf,header+4,n,bytes(8*n));times=struct.unpack_from('<I',buf,header+8)[0]
    append_array(buf,header+12,n,bytes(8*n));values=struct.unpack_from('<I',buf,header+16)[0]
    for i,(timestamps,items) in enumerate(series):
        if kind=='quat':timestamps,items=sample_quaternions(timestamps,items)
        if all(max(abs(a-b) for a,b in zip(items[0],v))<1e-7 for v in items[1:]):timestamps,items=[0],[items[0]]
        payload=b''.join(compressed_quat(v) if kind=='quat' else struct.pack('<3f',*v) for v in items)
        append_array(buf,times+8*i,len(timestamps),struct.pack('<'+'I'*len(timestamps),*timestamps))
        append_array(buf,values+8*i,len(items),payload)

def build(out):
    baked=json.loads((out/'baked.json').read_text());original_clips=baked['clips'];clips=[playback_clip(original_clips)];bones=baked['bones']
    vertices=[];weights=[];normals=[];uvs=[];indices=[];sections=[];combos=[]
    surfaces=[]
    for material,mesh in enumerate(baked['meshes']):
        if 'triangle_materials' in mesh:
            for slot in sorted(set(mesh['triangle_materials'])):
                surfaces.append((slot,dict(mesh,triangles=[t for t,m in zip(mesh['triangles'],mesh['triangle_materials']) if m==slot])))
        else:surfaces.append((material,mesh))
    for material,mesh in surfaces:
        groups=[];triangles=[];palette=set()
        for triangle in mesh['triangles']:
            used={b for i in triangle for b,w in mesh['weights'][i] if w>0}
            if len(palette|used)>75 and triangles:groups.append((triangles,sorted(palette)));triangles=[];palette=set()
            triangles.append(triangle);palette|=used
        if triangles:groups.append((triangles,sorted(palette)))
        for triangles,palette in groups:
            first_vertex=len(vertices);first_index=len(indices);mapping={};combo=len(combos);combos+=palette
            for triangle in triangles:
                for source in triangle:
                    if source not in mapping:
                        mapping[source]=len(vertices);vertices.append(mesh['vertices'][source]);normals.append(mesh['normals'][source]);uvs.append(mesh['uvs'][source])
                        ws=mesh['weights'][source];q=[round(w*255) for _,w in ws];q[0]+=255-sum(q)
                        weights.append((q+[0]*(4-len(q)),[palette.index(b) for b,_ in ws]+[0]*(4-len(q))))
                    indices.append(mapping[source])
            sections.append(dict(vertex=first_vertex,count=len(vertices)-first_vertex,index=first_index,indices=len(indices)-first_index,
                palette=palette,combo=combo,material=material))
    assert len(vertices)<=65535 and len(indices)<=65535 and len(combos)<=65535
    low=[min(v[k] for v in vertices) for k in range(3)];high=[max(v[k] for v in vertices) for k in range(3)]
    # Sequence bounds include sampled motion, not only the bind pose.
    radius=math.sqrt(sum(max(abs(low[k]),abs(high[k]))**2 for k in range(3)))+0.1
    bounds=struct.pack('<7f',*low,*high,radius)
    model=bytearray(304);struct.pack_into('<4sI',model,0,b'MD20',264)
    append_array(model,8,9,b'WolfPup\0\0');model[160:188]=bounds;model[188:216]=bounds
    seqs=[]
    for i,clip in enumerate(clips):
        seq=bytearray(64);struct.pack_into('<HHIfII',seq,0,0,i,clip['duration'],0,0x20,32767)
        seq[32:60]=bounds;struct.pack_into('<HH',seq,60,i+1 if i+1<len(clips) else 65535,0);seqs.append(seq)
    append_array(model,28,len(seqs),b''.join(seqs));append_array(model,36,1,struct.pack('<H',0))
    bone_records=[]
    for bone in bones:
        b=bytearray(88);struct.pack_into('<iIHHI',b,0,-1,0x200,65535,0,zlib.crc32(bone['name'].encode()))
        struct.pack_into('<3f',b,76,*bone['pivot']);bone_records.append(b)
    append_array(model,44,len(bones),b''.join(bone_records));bone_offset=struct.unpack_from('<I',model,48)[0]
    for i,bone in enumerate(bones):
        for channel,(offset,kind) in enumerate([(16,'vec'),(36,'quat'),(56,'vec')]):
            series=[]
            for clip in clips:
                timestamps=clip['times']
                items=[frame[i][channel] for frame in clip['transforms']]
                series.append((timestamps,items))
            track(model,bone_offset+88*i+offset,series,kind)
    append_array(model,60,len(vertices),b''.join(struct.pack('<3f4B4B3f4f',*v,*w,*b,*n,*uv,*uv)
        for v,(w,b),n,uv in zip(vertices,weights,normals,uvs)))
    struct.pack_into('<I',model,68,1)
    asset=baked.get('asset',{})
    texture_paths=asset.get('texture_paths',['custom\\fortnite\\pets\\wolf_pup_dog.blp','custom\\fortnite\\pets\\wolf_pup_carrier.blp'])
    material_count=len(texture_paths)
    append_array(model,80,material_count,bytes(16*material_count));texture_offset=struct.unpack_from('<I',model,84)[0]
    for i,name in enumerate(texture_paths):append_array(model,texture_offset+16*i+8,len(name)+1,name.encode()+b'\0')
    append_array(model,88,1,struct.pack('<HH4I',0,65535,0,0,0,0));alpha=struct.unpack_from('<I',model,92)[0]
    for field,fmt,value in [(4,'I',0),(12,'h',32767)]:
        append_array(model,alpha+field,len(clips),bytes(8*len(clips)));outer=struct.unpack_from('<I',model,alpha+field+4)[0]
        for i in range(len(clips)):append_array(model,outer+8*i,1,struct.pack('<'+fmt,value))
    # This dense facial bake uses ordinary lighting but opts out of incompatible exterior shadow-map passes.
    append_array(model,112,material_count,struct.pack('<'+'HH'*material_count,*([0x140,0]*material_count)));append_array(model,120,len(combos),struct.pack('<'+'H'*len(combos),*combos))
    for header,values in [(128,list(range(material_count))),(136,[0]*material_count),(144,[0]*material_count),(152,[65535]*material_count)]:append_array(model,header,len(values),struct.pack('<'+'H'*len(values),*values))
    skin=bytearray(48);skin[:4]=b'SKIN'
    append_array(skin,4,len(vertices),struct.pack('<'+'H'*len(vertices),*range(len(vertices))))
    append_array(skin,12,len(indices),struct.pack('<'+'H'*len(indices),*indices))
    append_array(skin,20,len(vertices),b''.join(bytes(b) for w,b in weights))
    records=[];batches=[]
    for i,s in enumerate(sections):
        center=[(low[k]+high[k])/2 for k in range(3)]
        records.append(struct.pack('<I8H7f',0,s['vertex'],s['count'],s['index'],s['indices'],len(s['palette']),s['combo'],4,s['palette'][0],*center,*center,radius))
        m=s['material'];batches.append(struct.pack('<BB11H',0,0,0,i,i,65535,m,0,1,m,m,m,m))
    append_array(skin,28,len(records),b''.join(records));append_array(skin,36,len(batches),b''.join(batches));struct.pack_into('<I',skin,44,75)
    source=Path('G:/FortnitePorter/Exported')
    textures=[source/'BRCosmetics/Characters/Pets/Wolf_Pup/Textures/T_Pet_Wolf_D.png',
        source/'BRCosmetics/Accessories/FORT_Backpacks/Backpack_Pets_Wolf/Texture/T_WolfPack_Skull__D.png']
    textures=[Path(p) for p in asset.get('textures',textures)]
    model_path=asset.get('model','custom/fortnite/pets/wolf_pup.m2')
    entries={model_path.replace('/','\\'):bytes(model),(model_path[:-3]+'00.skin').replace('/','\\'):bytes(skin)}
    entries.update({name:texture_bytes(path) for name,path in zip(texture_paths,textures)})
    for name,data in entries.items():
        p=out/'patch-root'/Path(name.replace('\\','/'));p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
    # Reparse every embedded bone track's outer and inner spans.
    for i in range(len(bones)):
        for offset,stride in [(16,12),(36,8),(56,12)]:
            for field,inner_stride in [(4,4),(12,stride)]:
                count,outer=struct.unpack_from('<II',model,bone_offset+88*i+offset+field);assert count==len(clips)
                for j in range(count):
                    n,p=struct.unpack_from('<II',model,outer+8*j);assert n and p+n*inner_stride<=len(model)
    assert all(sum(w)==255 for w,b in weights) and max(len(s['palette']) for s in sections)<=75
    moving=[i for i in range(len(bones)) if any(max(abs(a-b) for a,b in zip(clips[0]['transforms'][0][i][0],f[i][0]))>1e-6 for f in clips[0]['transforms'][1:])]
    report={'slug':asset.get('slug','animated_wolf_pup'),'name':asset.get('name','Wolf Pup (Remus)'),'model':model_path,
        'width':high[1]-low[1],'height':high[2]-low[2],'depth':high[0]-low[0],
        'vertices':len(vertices),'triangles':len(indices)//3,'bones':len(bones),'sections':len(sections),
        'facial_helpers':baked['facial_helper_count'],'moving_bones':len(moving),'clips':[{k:c[k] for k in ['name','frames','fps','sample_max_position_error','animated_shape_keys']} for c in original_clips],
        'playback':{'duration_ms':clips[0]['duration'],'source_key_ranges':clips[0]['sources'],'transition_ms':250},'seating':baked.get('seating'),
        'entries':{n:hashlib.sha256(d).hexdigest() for n,d in entries.items()},
        'losses':['Unreal material normal/specular response and runtime cloth physics not reproduced'],
        'state':'original idle and facial animation baked and embedded; native shader-skinning/render validation pending'}
    (out/'catalog.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:report[k] for k in ['model','vertices','triangles','bones','sections','facial_helpers','moving_bones','state']}))

if __name__=='__main__':build(Path(sys.argv[1] if len(sys.argv)>1 else 'G:/FortniteCosmetics/wolf-pup-v1'))
