"""Bake a forward pupil approximation into Woodsy's exported shader-driven eye atlas without changing the source."""
import json,sys
from pathlib import Path
import numpy as np
from mathutils import Vector
from fortnite_porting.ueformat.options import UEModelOptions
from fortnite_porting.ueformat.importer.import_context import UEFormatImport
import bpy

spec_path=Path(sys.argv[sys.argv.index('--')+1]);spec=json.loads(spec_path.read_text())
root=Path('G:/FortnitePorter/Exported');source=root/spec['source']
arm=UEFormatImport(UEModelOptions(import_sockets=False)).import_file(source)
mesh=next(o for o in arm.children if o.type=='MESH');mesh.data.calc_loop_triangles()
image=bpy.data.images.load(str(source.parent.parent/'Textures/T_Pet_Woodsy_D.png'));width,height=image.size
pixels=np.asarray(image.pixels[:],dtype=np.float32).reshape(height,width,4).copy();changed=0
for name in ('L_eye','R_eye'):
    group=mesh.vertex_groups[name].index
    ids={v.index for v in mesh.data.vertices if any(g.group==group and g.weight>.5 for g in v.groups)}
    points=np.asarray([mesh.data.vertices[i].co[:] for i in ids]);center=(points.min(0)+points.max(0))/2
    for triangle in mesh.data.loop_triangles:
        if not all(i in ids for i in triangle.vertices):continue
        uv=np.asarray([mesh.data.uv_layers.active.data[i].uv[:] for i in triangle.loops])*[width-1,height-1]
        low=np.maximum(np.floor(uv.min(0)).astype(int),0);high=np.minimum(np.ceil(uv.max(0)).astype(int),[width-1,height-1])
        grid=np.stack(np.meshgrid(np.arange(low[0],high[0]+1),np.arange(low[1],high[1]+1)),axis=-1).reshape(-1,2)
        if not len(grid):continue
        edges=np.stack((uv[1]-uv[0],uv[2]-uv[0]),axis=1)
        if abs(np.linalg.det(edges))<1e-8:continue
        bary=(grid-uv[0])@np.linalg.inv(edges).T;weights=np.column_stack((1-bary.sum(1),bary))
        radial=weights@np.asarray([mesh.data.vertices[i].co[:] for i in triangle.vertices])-center
        selected=grid[(weights>=-1e-5).all(1)&(-radial[:,1]>.91*np.linalg.norm(radial,axis=1))]
        changed+=len(selected);pixels[selected[:,1],selected[:,0],:3]=.012
assert changed>30,changed
image.pixels[:]=pixels.ravel();output=spec_path.parent/'woodsy-eyes.png';image.filepath_raw=str(output);image.file_format='PNG';image.save()
spec['textures'][0]=str(output);spec_path.write_text(json.dumps(spec,indent=2)+'\n')
print('WOODSY_PUPILS',changed,'texels; original export unchanged',flush=True)
