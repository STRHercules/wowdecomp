"""Convert exported Fortnite back meshes to rigid, lit-diffuse Wrath cosmetics.

The export's vertices are already in skeletal bind space. Reapplying reference-bone transforms would
double-transform old backpack rigs. Physics and Unreal shader graphs are intentionally not evaluated.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import io
import json
import math
from pathlib import Path
import re
import struct
import sys

from PIL import Image
import zstandard

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "Tools/FortniteTrial"))
from journey_shield import append_array


class Reader:
    def __init__(self, data):
        self.file = io.BytesIO(data)
        self.size = len(data)

    def read(self, size):
        if size < 0 or size > self.size - self.file.tell():
            raise ValueError("Truncated or negative UEFORMAT span")
        return self.file.read(size)

    def unpack(self, fmt):
        return struct.unpack("<"+fmt, self.read(struct.calcsize("<"+fmt)))

    def count(self, limit=1000000):
        value = self.unpack("i")[0]
        if not 0 <= value <= limit:
            raise ValueError("UEFORMAT count outside supported range")
        return value

    def string(self):
        return self.read(self.count(32768)).rstrip(b"\0").decode()

    def attributes(self):
        result = {}
        for _ in range(self.count(100)):
            name = self.string()
            if name in result:
                raise ValueError("Duplicate UEFORMAT attribute")
            result[name] = Reader(self.read(self.count(64*1024*1024)))
        return result


def read_mesh(data):
    r = Reader(data)
    if r.read(8) != b"UEFORMAT" or r.string() != "UEMODEL" or r.unpack("B")[0] != 10:
        raise ValueError("Expected UEFORMAT UEMODEL v10")
    name, source_path = r.string(), r.string()
    compressed = r.unpack("B")[0]
    if compressed != 1 or r.string() != "ZSTD":
        raise ValueError("Expected ZSTD-compressed export")
    size, packed = r.count(64*1024*1024), r.count(64*1024*1024)
    body = zstandard.ZstdDecompressor().decompress(r.read(packed), max_output_size=size)
    if len(body) != size or r.file.tell() != r.size:
        raise ValueError("Invalid UEFORMAT compressed lengths")
    attrs = Reader(body).attributes()
    lods = attrs["LODS"]
    lod_count = lods.count(16)
    if not lod_count or lods.string() != "LOD0":
        raise ValueError("Missing LOD0")
    lod = lods.attributes()
    def vectors(field, width):
        ar = lod[field]
        return [ar.unpack("f"*width) for _ in range(ar.count(65535))]
    vertices, normals = vectors("VERTICES", 3), vectors("NORMALS", 4)
    uv = lod["TEXCOORDS"]
    if not uv.count(16):
        raise ValueError("Missing UV0")
    uv.string()
    uvs = [uv.unpack("ff") for _ in range(uv.count(65535))]
    if not vertices or len(vertices) != len(normals) or len(vertices) != len(uvs):
        raise ValueError("Vertex/normal/UV count mismatch")
    ar = lod["INDICES"]
    count = ar.count(1000000)
    if not count or count % 3:
        raise ValueError("Invalid triangle list")
    indices = list(ar.unpack("I"*count))
    if max(indices) >= len(vertices):
        raise ValueError("Out-of-range triangle index")
    ar = lod["MATERIALS"]
    materials = []
    claimed = bytearray(len(indices)//3)
    for _ in range(ar.count(256)):
        material, path = ar.string(), ar.string()
        start, faces = ar.count(count), ar.count(count//3)
        if start % 3 or start+faces*3 > count:
            raise ValueError("Invalid material triangle range")
        for index in range(start//3,start//3+faces):
            if claimed[index]:
                raise ValueError("Overlapping material triangles")
            claimed[index] = 1
        materials.append({"name": material, "path": path, "first_index": start, "faces": faces})
    if not materials or not all(claimed):
        raise ValueError("Unassigned material triangles")
    bones = 0
    if "SKELETON" in attrs:
        skeleton = attrs["SKELETON"].attributes()
        bone_reader = skeleton["BONES"]
        bones = bone_reader.count(65535)
        for i in range(bones):
            bone_reader.string()
            parent = bone_reader.unpack("i")[0]
            if parent >= i or parent < -1:
                raise ValueError("Invalid reference skeleton parent")
            values = bone_reader.unpack("10f")
            if not all(math.isfinite(x) for x in values):
                raise ValueError("Invalid reference skeleton transform")
    if "WEIGHTS" in lod:
        ar = lod["WEIGHTS"]
        totals = [0.0]*len(vertices)
        for _ in range(ar.count(1000000)):
            bone, vertex, weight = ar.unpack("Hif")
            if bone >= bones or not 0 <= vertex < len(vertices) or not math.isfinite(weight) or not 0 <= weight <= 1.001:
                raise ValueError("Invalid source skin weight")
            totals[vertex] += weight
        if any(abs(total-1) > 0.015 for total in totals):
            raise ValueError("Unnormalized source skin weights")
    if not all(math.isfinite(x) for row in vertices+normals+uvs for x in row):
        raise ValueError("Non-finite mesh values")
    top = max(v[2] for v in vertices)
    front = max(v[1] for v in vertices)
    middle = (max(v[0] for v in vertices)+min(v[0] for v in vertices))*0.5
    vertices = [((v[1]-front)/100,(v[0]-middle)/100,(v[2]-top)/100) for v in vertices]
    normals = [(n[2],n[1],n[3]) for n in normals]
    positive, negative, degenerate = 0, 0, 0
    for i in range(0,len(indices),3):
        a,b,c = indices[i:i+3]
        ab = [vertices[b][k]-vertices[a][k] for k in range(3)]
        ac = [vertices[c][k]-vertices[a][k] for k in range(3)]
        cross = (ab[1]*ac[2]-ab[2]*ac[1],ab[2]*ac[0]-ab[0]*ac[2],ab[0]*ac[1]-ab[1]*ac[0])
        dot = sum(cross[k]*sum(normals[j][k] for j in (a,b,c)) for k in range(3))
        if dot < -1e-12:
            negative += 1
        elif dot > 1e-12:
            positive += 1
        else:
            degenerate += 1
    return {"source_name":name,"source_path":source_path,"vertices":vertices,"normals":normals,
            "uvs":uvs,"indices":indices,"materials":materials,"bones":bones,"lods":lod_count,
            "winding":{"positive":positive,"normal_opposed_preserved":negative,"degenerate":degenerate},
            "source_normalization":{"front_cm":front,"center_cm":middle,"top_cm":top}}


def key(value):
    return re.sub('[^a-z0-9]','',value.lower())


def is_diffuse(path):
    return bool(re.search(r'(?i)(?:_d(?:_v\d+)?|_diff|_diffuse|_basecolor|_albedo|_bc|_cl|_colorl|_c)\Z',path.stem))


def export_dependencies(log_root):
    result={}
    for log in sorted(log_root.glob('*.log'),key=lambda p:p.stat().st_mtime):
        current=None
        for number,line in enumerate(log.read_text(errors='replace').splitlines(),1):
            if 'Exporting SkeletalMesh: ' in line or 'Exporting StaticMesh: ' in line:
                current=line.split('Mesh: ',1)[1].replace('\\','/').lower()
                result[current]=[]
            elif current and 'Exporting Texture2D: ' in line:
                path=Path(line.split('Exporting Texture2D: ',1)[1])
                if path.exists(): result[current].append((path,f'{log}:{number}'))
    return result


def pick_diffuse(candidates,material):
    wanted=key(re.sub(r'^(?:MI_|M_|T_)','',material['name']))
    exact=[]
    for p in candidates:
        base=key(re.sub(r'(?i)(?:_d(?:_v\d+)?|_diff|_diffuse|_basecolor|_albedo|_bc|_cl|_colorl|_c)$','',re.sub(r'^(?:MI_|M_|T_)','',p.stem)))
        if base==wanted: exact.append(p)
    if len(exact)==1: return exact[0]
    if len(candidates)==1: return candidates[0]
    # Prefer the default atlas to explicitly named silver/gold/recolour variants.
    candidates=[p for p in candidates if not re.search(r'(?i)(?:_silver|_gold|_v\d+)(?:_d)?$',p.stem)] or candidates
    if len(candidates)==1: return candidates[0]
    ranked=sorted(((len(set(wanted[i:i+4] for i in range(len(wanted)-3)) & set(key(p.stem)[i:i+4] for i in range(len(key(p.stem))-3))),str(p),p) for p in candidates),reverse=True)
    if ranked and ranked[0][0]>=5 and (len(ranked)==1 or ranked[0][0]>ranked[1][0]): return ranked[0][2]
    return None


def texture_candidates(root, mesh_path, material, dependencies):
    logged=dependencies.get(str(mesh_path).replace('\\','/').lower(),[])
    logged_diffuse=[p for p,evidence in logged if is_diffuse(p)]
    chosen=pick_diffuse(logged_diffuse,material)
    if chosen:
        evidence=next(evidence for p,evidence in logged if p==chosen)
        return chosen,'export dependency '+evidence
    # Exported material paths identify the package family even when texture files were flattened.
    family = root / Path(material['path'].split('.')[0].lstrip('/')).parent.parent
    local = mesh_path.parent.parent
    for scope, reason in ((family,"material package family"),(local,"mesh package family")):
        candidates = [p for p in scope.rglob('*.png') if is_diffuse(p)] if scope.exists() else []
        base = [p for p in candidates if not any(s.lower() in ('skins','v01','v02','v03','v04') for s in p.relative_to(scope).parts)]
        if base:
            candidates = base
        chosen=pick_diffuse(candidates,material)
        if chosen: return chosen,reason+': unique diffuse match'
    return None, "No unambiguous diffuse in source material/mesh package"


def write_model(mesh, texture_paths):
    vertices,normals,uvs = mesh['vertices'],mesh['normals'],mesh['uvs']
    model,skin = bytearray(304),bytearray(48)
    struct.pack_into('<4sI',model,0,b'MD20',264)
    label = mesh['source_name'].encode()+b'\0'
    append_array(model,8,len(label),label)
    low = [min(v[k] for v in vertices) for k in range(3)]
    high = [max(v[k] for v in vertices) for k in range(3)]
    radius = max(math.sqrt(sum(x*x for x in v)) for v in vertices)
    bounds = struct.pack('<7f',*low,*high,radius)
    model[160:188] = bounds
    model[188:216] = bounds
    sequence = bytearray(64)
    struct.pack_into('<HHIfII',sequence,0,0,0,1000,0,0x20,32767)
    sequence[32:60] = bounds
    struct.pack_into('<HH',sequence,60,65535,0)
    append_array(model,28,1,sequence)
    append_array(model,36,1,struct.pack('<H',0))
    bone = bytearray(88)
    struct.pack_into('<iIHHI',bone,0,-1,0,65535,0,0)
    for offset in (16,36,56): struct.pack_into('<HH',bone,offset,0,65535)
    append_array(model,44,1,bone)
    append_array(model,60,len(vertices),b''.join(struct.pack('<3f4B4B3f4f',*v,255,0,0,0,0,0,0,0,*n,*uv,*uv)
                                               for v,n,uv in zip(vertices,normals,uvs)))
    struct.pack_into('<I',model,68,1)
    count = len(texture_paths)
    append_array(model,80,count,bytes(16*count))
    table = struct.unpack_from('<I',model,84)[0]
    for i,path in enumerate(texture_paths):
        value = path.encode()+b'\0'
        append_array(model,table+16*i+8,len(value),value)
    append_array(model,88,1,struct.pack('<HH4I',0,65535,0,0,0,0))
    track = struct.unpack_from('<I',model,92)[0]
    for field,fmt,value in ((4,'I',0),(12,'h',32767)):
        append_array(model,track+field,1,bytes(8))
        outer = struct.unpack_from('<I',model,track+field+4)[0]
        append_array(model,outer,1,struct.pack('<'+fmt,value))
    append_array(model,112,count,b''.join(struct.pack('<HH',0,0) for _ in range(count)))
    for header,values in ((120,[0]),(128,list(range(count))),(136,[0]*count),(144,[0]*count),(152,[65535]*count)):
        append_array(model,header,len(values),struct.pack('<'+'H'*len(values),*values))
    skin[:4] = b'SKIN'
    append_array(skin,4,len(vertices),struct.pack('<'+'H'*len(vertices),*range(len(vertices))))
    append_array(skin,12,len(mesh['indices']),struct.pack('<'+'H'*len(mesh['indices']),*mesh['indices']))
    append_array(skin,20,len(vertices),bytes(4*len(vertices)))
    sections,batches = [],[]
    for i,mat in enumerate(mesh['materials']):
        start = mat['first_index']
        if start > 65535 or mat['faces']*3 > 65535:
            raise ValueError('Skin section exceeds classic index window')
        sections.append(struct.pack('<I8H7f',1502,0,len(vertices),start,mat['faces']*3,1,0,1,0,
                                    *((low[k]+high[k])/2 for k in range(3)),
                                    *((low[k]+high[k])/2 for k in range(3)),radius))
        batches.append(struct.pack('<BB11H',0,0,0,i,i,65535,i,0,1,i,i,i,i))
    append_array(skin,28,count,b''.join(sections))
    append_array(skin,36,count,b''.join(batches))
    struct.pack_into('<I',skin,44,1)
    return bytes(model),bytes(skin),[high[k]-low[k] for k in range(3)]


def texture_bytes(path):
    image=Image.open(path).convert('RGBA')
    if image.width>4096 or image.height>4096 or image.width&(image.width-1) or image.height&(image.height-1):
        raise ValueError('Texture dimensions must be supported powers of two')
    image.putalpha(255)
    blob=bytearray(1172)
    struct.pack_into('<4sI4BII',blob,0,b'BLP2',1,2,0,0,1,*image.size)
    for mip in range(16):
        dds=io.BytesIO()
        image.save(dds,format='DDS',pixel_format='DXT1')
        encoded=dds.getvalue()
        if encoded[:4]!=b'DDS ' or encoded[84:88]!=b'DXT1': raise ValueError('Unexpected DDS codec')
        payload=encoded[128:]
        assert len(payload)==max(1,(image.width+3)//4)*max(1,(image.height+3)//4)*8
        struct.pack_into('<I',blob,20+4*mip,len(blob))
        struct.pack_into('<I',blob,84+4*mip,len(payload))
        blob.extend(payload)
        if image.size==(1,1): break
        image=image.resize((max(1,image.width//2),max(1,image.height//2)),Image.Resampling.BOX)
    return bytes(blob)


def verify(model,skin):
    def array(data,header,stride):
        count,offset = struct.unpack_from('<II',data,header)
        if offset+count*stride > len(data): raise ValueError('Output span out of bounds')
        return count,offset
    assert struct.unpack_from('<4sI',model)==(b'MD20',264) and skin[:4]==b'SKIN'
    nv,vo = array(model,60,48)
    for header,stride in ((8,1),(28,64),(36,2),(44,88),(80,16),(88,20),(112,4),(120,2),(128,2),(136,2),(144,2),(152,2)):
        array(model,header,stride)
    nt,to = array(model,80,16)
    for i in range(nt):
        count,off = array(model,to+16*i+8,1)
        assert model[off+count-1]==0
    nalpha,alpha = array(model,88,20)
    for field,stride in ((4,4),(12,2)):
        count,outer = array(model,alpha+field,8)
        assert count==1 and array(model,outer,stride)[0]==1
    assert nalpha==1
    assert array(skin,4,2)[0]==nv and array(skin,20,4)[0]==nv
    ni,io = array(skin,12,2)
    assert max(struct.unpack_from('<'+'H'*ni,skin,io))<nv
    ns,so = array(skin,28,48)
    nb,bo = array(skin,36,24)
    assert ns==nb==nt
    for i in range(ns):
        section=struct.unpack_from('<I8H7f',skin,so+48*i)
        batch=struct.unpack_from('<BB11H',skin,bo+24*i)
        assert section[3]+section[4]<=ni and section[2]==nv
        assert batch[3]==i and batch[6]<nt and batch[8]==1 and batch[9]<nt
    assert all(sum(model[vo+48*i+12:vo+48*i+16])==255 for i in range(nv))


def slug_of(path,root):
    return re.sub('[^a-z0-9]+','_',path.stem.lower()).strip('_')+'_'+hashlib.sha256(str(path.relative_to(root)).replace('\\','/').lower().encode()).hexdigest()[:8]


def display_name(stem):
    name=re.sub(r'^(?:SK_)?(?:[FM]_[Mm][Ee][Dd]_?|[Mm]ale_[Cc]ommando_|[Ff]emale_?[Cc]ommando_)','',stem)
    name=re.sub(r'(?i)(?:_backpack|_pack)+$','',name)
    name=re.sub(r'([a-z])([A-Z])',r'\1 \2',name).replace('_',' ')
    if name.strip().isdigit(): name='Commando '+name.strip()
    return name.strip() or stem


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,default=Path('G:/FortnitePorter/Exported'))
    parser.add_argument('--out',type=Path,default=Path('G:/FortniteCosmetics/stage'))
    parser.add_argument('--audit-only',action='store_true')
    parser.add_argument('--logs',type=Path,default=Path('C:/Users/Zach/AppData/Local/FortnitePorting/Logs'))
    args=parser.parse_args()
    if args.out.exists(): raise FileExistsError(args.out)
    args.out.mkdir(parents=True)
    files=sorted((p for p in args.source.rglob('*.uemodel') if any('backpack' in s.lower() for s in p.relative_to(args.source).parts)),key=lambda p:str(p).lower())
    rows,entries,texture_cache=[],{},{}
    dependencies=export_dependencies(args.logs)
    for path in files:
        row={'slug':slug_of(path,args.source),'name':display_name(path.stem),'source':str(path),'source_sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
        try:
            mesh=read_mesh(path.read_bytes())
            row.update(vertices=len(mesh['vertices']),triangles=len(mesh['indices'])//3,source_bones=mesh['bones'],source_lods=mesh['lods'],source_normalization=mesh['source_normalization'],winding=mesh['winding'])
            materials=[]
            for material in mesh['materials']:
                texture,reason=texture_candidates(args.source,path,material,dependencies)
                materials.append(dict(material,source_texture=str(texture) if texture else None,texture_match=reason))
            row['materials']=materials
            missing=[m['name'] for m in materials if not m['source_texture']]
            if missing: raise ValueError('Missing material diffuse: '+', '.join(missing))
            row.update(model='custom/fortnite/backpacks/'+row['slug']+'.m2',
                       losses=['Source skeletal motion and physics frozen at exported bind pose','Unreal normal/specular/emissive and shader graphs not reproduced; lit diffuse approximation'])
            if any(re.search(r'(?i)outline|glass|vertexcrunch|edge_test|_line_',m['name']) for m in materials):
                row['losses'].append('Special shader surfaces retain geometry and use the exported package diffuse; outline/glass/vertex-crunch appearance needs visual review')
            paths=[]
            for material in materials:
                texture=Path(material['source_texture'])
                digest=hashlib.sha256(texture.read_bytes()).hexdigest()
                name='custom\\fortnite\\backpacks\\textures\\'+digest[:24]+'.blp'
                paths.append(name)
                material['texture']=name.replace('\\','/')
                material['texture_sha256']=digest
                if not args.audit_only and name not in texture_cache:
                    payload=texture_bytes(texture)
                    Image.open(io.BytesIO(payload)).load()
                    texture_cache[name]=payload
            model,skin,dimensions=write_model(mesh,paths)
            verify(model,skin)
            row.update(width=dimensions[1],height=dimensions[2],depth=dimensions[0],status='converted')
            if not args.audit_only:
                entries[row['model'].replace('/','\\')]=model
                entries[(row['model'][:-3]+'00.skin').replace('/','\\')]=skin
        except Exception as exc:
            row.update(status='blocked',error=str(exc))
        rows.append(row)
        print(row['status'],row['slug'],row.get('error',''),flush=True)
    entries.update(texture_cache)
    names=Counter(row['name'] for row in rows)
    for row in rows:
        if names[row['name']]>1:
            row['name']+=' (Female)' if row['slug'].startswith('f_med') else ' (Male)'
    summary={'source_count':len(files),'counts':dict(Counter(r['status'] for r in rows)),
             'normalization':'UE X lateral Y depth -> WoW X forward Y lateral, centimeters/100, front X=0 center Y=0 top Z=0',
             'accepted_journey_fit':'Runtime midpoint between original cloak edge and full back clearance; width-based scale preserved',
             'rows':rows}
    (args.out/'catalog.json').write_text(json.dumps(summary,indent=2)+'\n')
    if not args.audit_only:
        for name,data in entries.items():
            dest=args.out/'patch-root'/Path(name.replace('\\','/'))
            dest.parent.mkdir(parents=True,exist_ok=True)
            dest.write_bytes(data)
        sys.path.insert(0,str(REPO.parent/'EsteriaWoW/tools'))
        from cars_mount_pack import Storm
        storm=Storm(REPO/'Tools/MountStudio/runtime/backend/StormLib.dll')
        archive=args.out/'Patch-ZzzzzzzzzzzFortniteBackpacks.MPQ'
        storm.create_archive(archive,entries)
        handle=storm.open_archive(archive)
        try:
            for name,data in entries.items():
                assert storm.read(handle,name)==data,name
        finally:
            storm.dll.SFileCloseArchive(handle)
        receipt={'archive':str(archive),'archive_sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),
                 'entries':{name:hashlib.sha256(data).hexdigest() for name,data in entries.items()},
                 'stage':'converted; MPQ readback verified; visual acceptance pending'}
        (args.out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps({k:v for k,v in summary.items() if k!='rows'}))


if __name__=='__main__':
    main()
