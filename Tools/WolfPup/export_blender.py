"""Sample the original dog actions and facial poses from the supplied assembly; never saves the blend."""
import bpy,json,sys,math
from pathlib import Path
from mathutils import Matrix,Vector,Quaternion
from fortnite_porting.ueformat.options import UEAnimOptions,UEModelOptions,UEPoseOptions
from fortnite_porting.ueformat.importer.import_context import UEFormatImport

out=Path(sys.argv[sys.argv.index('--')+1]);out.mkdir(parents=True,exist_ok=True)
source=Path('G:/FortnitePorter/Exported')
spec=json.loads(Path(sys.argv[sys.argv.index('--spec')+1]).read_text()) if '--spec' in sys.argv else None
if spec:
    bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
    arm=UEFormatImport(UEModelOptions(import_sockets=False)).import_file(source/spec['source'])
    dog=next(o for o in arm.children if o.type=='MESH')
    if spec.get('facial_bind_retarget') and spec['slug']=='animated_woodsy':
        # Woodsy's rigid carved eyes must stay in their mesh bind pose rather than roll behind the face shell.
        eyes=[dog.vertex_groups.get(name) for name in ('L_eye','R_eye')]
        ids={g.index for g in eyes if g};head_group=dog.vertex_groups['head']
        for vertex in dog.data.vertices:
            weight=sum(g.weight for g in vertex.groups if g.group in ids)
            if weight>0:head_group.add([vertex.index],weight,'ADD')
        for group in eyes:
            if group:dog.vertex_groups.remove(group)
    bpy.context.view_layer.objects.active=arm
    pose_dto=None
    if spec.get('pose'):
        _,pose_dto=UEFormatImport(UEPoseOptions(override_skeleton=arm)).import_file_with_dto(source/spec['pose'])
    carrier_arm=UEFormatImport(UEModelOptions(import_sockets=False)).import_file(source/spec['carrier'])
    carrier=next(o for o in carrier_arm.children if o.type=='MESH')
    clip_paths=[source/p for p in spec['clips']]
else:
    arm=bpy.data.objects['Pets_Wolf_Pup'];dog=bpy.data.objects['Pets_Wolf_Pup_LOD0']
    carrier=bpy.data.objects['WolfPack_Backpack_LOD0']
    clip_paths=[source/'Game/Animation/Game/Pets/Dog_WolfPup/Backpack/Idle'/name for name in ('Idle.ueanim','IdleBreak_02.ueanim')]
rotation=Matrix(((0,-1,0,0),(1,0,0,0),(0,0,1,0),(0,0,0,1)))
all_vertices=[rotation@(o.matrix_world@v.co) for o in (dog,carrier) for v in o.data.vertices]
shift=Vector((-max(v.x for v in all_vertices),-(min(v.y for v in all_vertices)+max(v.y for v in all_vertices))/2,-max(v.z for v in all_vertices)))
basis=Matrix.Translation(shift)@rotation;inverse=basis.inverted()
bone_names=[b.name for b in arm.data.bones]
def import_clip(path):
    global facial_channels
    facial_channels=[]
    action,dto=UEFormatImport(UEAnimOptions(override_skeleton=arm,import_curves=not(spec and spec.get('facial_bind_retarget')))).import_file_with_dto(path)
    curves=[c for layer in action.layers for strip in layer.strips for bag in strip.channelbags for c in bag.fcurves]
    # UE rotations can use equivalent +/- keys; Blender's component curves also need continuity before sampling.
    rotations={}
    for curve in curves:
        if curve.data_path.endswith('rotation_quaternion'):rotations.setdefault(curve.data_path,{})[curve.array_index]=curve
    for channels in rotations.values():
        if set(channels)!={0,1,2,3}:raise ValueError('Incomplete source quaternion channels')
        previous=None
        for index in range(len(channels[0].keyframe_points)):
            q=[channels[k].keyframe_points[index].co.y for k in range(4)]
            if previous is not None and sum(a*b for a,b in zip(previous,q))<0:
                q=[-v for v in q]
                for k in range(4):channels[k].keyframe_points[index].co.y=q[k]
            previous=q
        for curve in channels.values():curve.update()
    # UE facial translations use Skeleton retargeting: the Wolf's jaw/tongue bind differs from shared Dog_Skeleton.
    for track in dto.tracks:
        if not track.position_keys or not track.name.startswith(('C_','L_','R_','teeth','tongue','faceAttach')):continue
        first=track.position_keys[0].value
        if any(max(abs(a-b) for a,b in zip(first,key.value))>1e-6 for key in track.position_keys):continue
        bone=arm.pose.bones.get(track.name)
        if bone:
            path=bone.path_from_id('location')
            for curve in curves:
                if curve.data_path==path:
                    for point in curve.keyframe_points:point.co.y=0
                    curve.update()
    if spec and spec.get('facial_bind_retarget'):
        # These clips carry shared-rig neutral facial TRS; species expressions are entirely in their pose curves.
        for track in dto.tracks:
            if not track.name.startswith(('C_','L_','R_','teeth','tongue','faceAttach')):continue
            bone=arm.pose.bones.get(track.name)
            if not bone:continue
            for keys,channel,neutral in ((track.position_keys,'location',(0,0,0)),
                    (track.rotation_keys,'rotation_quaternion',(1,0,0,0)),(track.scale_keys,'scale',(1,1,1))):
                if not keys:continue
                if any(max(abs(a-b) for a,b in zip(keys[0].value,key.value))>1e-5 for key in keys[1:]):
                    raise ValueError('Animated facial bone requires delta retargeting: '+track.name)
                for curve in curves:
                    if curve.data_path==bone.path_from_id(channel):
                        for point in curve.keyframe_points:point.co.y=neutral[curve.array_index]
                        curve.update()
                        facial_channels.append((curve,bone,channel))
    return action,dto

def evaluate_frame(frame,dto):
    bpy.context.scene.frame_set(frame)
    if spec and spec.get('facial_bind_retarget') and pose_dto:
        poses={p.name.lower():p for p in pose_dto.poses}
        bones={bone.name.lower():bone for bone in arm.pose.bones}
        if dog.data.shape_keys:
            dog.data.shape_keys.animation_data_clear()
            for key in dog.data.shape_keys.key_blocks:key.value=0
        for name in {bone.name for _,bone,_ in facial_channels}:
            arm.pose.bones[name].matrix_basis.identity()
        for curve in dto.curves:
            pose=poses.get(curve.name.lower())
            if not pose or not curve.keys:continue
            keys=curve.keys;value=keys[-1].value
            if frame<=keys[0].frame:value=keys[0].value
            else:
                for a,b in zip(keys,keys[1:]):
                    if a.frame<=frame<=b.frame:
                        t=(frame-a.frame)/max(1,b.frame-a.frame);value=a.value*(1-t)+b.value*t;break
            if abs(value)<1e-7:continue
            for key in pose.keys:
                bone=bones.get(key.bone_name.lower())
                if not bone:continue
                post=Quaternion(bone.bone.get('post_quat',(1,0,0,0)))
                delta=Quaternion((key.rotation[3],key.rotation[0],key.rotation[1],key.rotation[2]))
                delta=(post@delta@post.conjugated()).normalized()
                if delta.w<0:delta.negate()
                weighted=Quaternion(delta.axis,delta.angle*float(value))
                bone.rotation_quaternion=weighted@bone.rotation_quaternion
                bone.location+=post.conjugated()@Vector(key.position)*float(value)
                bone.scale+=Vector(key.scale)*float(value)
        # Animated channels otherwise overwrite manual bone values during dependency evaluation.
        for curve,bone,channel in facial_channels:
            value=getattr(bone,channel)[curve.array_index]
            for point in curve.keyframe_points:point.co.y=value
            curve.update()
        bpy.context.scene.frame_set(frame)
    bpy.context.view_layer.update()

idle_path=clip_paths[0]
_,idle_dto=import_clip(idle_path);evaluate_frame(0,idle_dto)
evaluated=dog.evaluated_get(bpy.context.evaluated_depsgraph_get());reference=evaluated.to_mesh()
paws=[]
for name in ('hand_l','hand_r'):
    group=dog.vertex_groups[name].index
    points=[basis@(dog.matrix_world@reference.vertices[v.index].co) for v in dog.data.vertices
        if any(g.group==group and g.weight>0.5 for g in v.groups)]
    paws.append(sum(points,Vector())/len(points))
evaluated.to_mesh_clear()
span=paws[1]-paws[0];angle=math.atan2(span.x,span.y)
center=(paws[0]+paws[1])*0.5
carrier_points=[basis@(carrier.matrix_world@v.co) for v in carrier.data.vertices]
target=Vector((min(v.x for v in carrier_points)+0.015,(min(v.y for v in carrier_points)+max(v.y for v in carrier_points))*0.5,center.z))
seating=Matrix.Translation(target)@Matrix.Rotation(angle,4,'Z')@Matrix.Translation(-center)
dog_basis=seating@basis;dog_inverse=dog_basis.inverted();dog_rotation=(seating@rotation).to_3x3()
bone_pivots=[dog_basis@(arm.matrix_world@bone.matrix_local.translation) for bone in arm.data.bones]
meshes=[]
for o in (dog,carrier):
    mesh_basis=dog_basis if o==dog else basis
    mesh_rotation=dog_rotation if o==dog else rotation.to_3x3()
    mesh=o.data;mesh.calc_loop_triangles();uvs={};normals={}
    for loop in mesh.loops:
        uvs[loop.vertex_index]=list(mesh.uv_layers.active.data[loop.index].uv)
        normals[loop.vertex_index]=list(mesh_rotation@(o.matrix_world.to_3x3().inverted().transposed()@mesh.corner_normals[loop.index].vector).normalized())
    positions=[list(mesh_basis@(o.matrix_world@v.co)) for v in mesh.vertices]
    weights=[]
    for v in mesh.vertices:
        ws=[]
        for group in v.groups:
            name=o.vertex_groups[group.group].name
            if o==dog and name in bone_names and group.weight>1e-6:ws.append((bone_names.index(name),group.weight))
        if o==carrier:ws=[(len(bone_names),1)]
        ws.sort(key=lambda x:-x[1]);ws=ws[:4];total=sum(w for _,w in ws)
        if total<=0:raise ValueError('Unweighted source vertex')
        weights.append([(i,w/total) for i,w in ws])
    triangles=[list(t.vertices) for t in mesh.loop_triangles]
    signs=[]
    for t in triangles:
        p=[Vector(positions[i]) for i in t]
        signs.append((p[1]-p[0]).cross(p[2]-p[0]).dot(sum((Vector(normals[i]) for i in t),Vector())))
    if sum(s<0 for s in signs)>sum(s>0 for s in signs):triangles=[(a,c,b) for a,b,c in triangles]
    meshes.append(dict(name=o.name,vertices=positions,normals=[normals[i] for i in range(len(positions))],
        uvs=[(uvs[i][0],1-uvs[i][1]) for i in range(len(positions))],weights=weights,triangles=triangles))
    if spec:
        slots=spec['pet_materials'] if o==dog else spec['carrier_materials']
        meshes[-1]['triangle_materials']=[slots[t.material_index] for t in mesh.loop_triangles]

clips=[];affected=set();pose_samples=[]
for path in clip_paths:
    filename=path.name
    action,dto=import_clip(path)
    frames=dto.metadata.num_frames;fps=dto.metadata.frames_per_second
    transforms=[];vertex_frames=[];normal_frames=[];shape_names=set()
    for frame in range(frames):
        evaluate_frame(frame,dto)
        mats=[]
        for bone_index,bone in enumerate(arm.pose.bones):
            skin=arm.matrix_world@bone.matrix@bone.bone.matrix_local.inverted()@arm.matrix_world.inverted()
            mat=dog_basis@skin@dog_inverse;location,quaternion,scale=mat.decompose()
            quaternion.normalize()
            rebuilt=Matrix.LocRotScale(location,quaternion,scale)
            if max(abs(mat[r][c]-rebuilt[r][c]) for r in range(4) for c in range(4))>0.001:
                if not spec:raise ValueError('Skeletal shear requires a different bake')
                # M2 TRS cannot express inherited shear; bake those vertices with the same exact helpers as morphs.
                affected.update(i for i,ws in enumerate(meshes[0]['weights']) if any(b==bone_index for b,w in ws))
            pivot=bone_pivots[bone_index]
            translation=location-pivot+quaternion@Vector(tuple(pivot[k]*scale[k] for k in range(3)))
            mats.append([list(translation),[quaternion.x,quaternion.y,quaternion.z,quaternion.w],list(scale)])
        transforms.append(mats)
        keys=dog.data.shape_keys.key_blocks if dog.data.shape_keys else []
        for key in keys[1:]:
            if abs(key.value)>1e-6:
                shape_names.add(key.name)
                for i,p in enumerate(key.data):
                    if (p.co-keys[0].data[i].co).length>1e-7:affected.add(i)
        evaluated=dog.evaluated_get(bpy.context.evaluated_depsgraph_get());em=evaluated.to_mesh()
        if len(em.vertices)!=len(dog.data.vertices):raise ValueError('Evaluated topology changed')
        vertex_frames.append([list(dog_basis@(dog.matrix_world@v.co)) for v in em.vertices])
        normal_frames.append([list((dog_rotation@(dog.matrix_world.to_3x3().inverted().transposed()@v.normal)).normalized()) for v in em.vertices])
        evaluated.to_mesh_clear()
    clips.append(dict(name=Path(filename).stem,frames=frames,fps=fps,transforms=transforms,
        vertices=vertex_frames,normals=normal_frames,animated_shape_keys=sorted(shape_names)))
if spec:
    import numpy as np
    bind=np.asarray(meshes[0]['vertices']);pivots=np.asarray(bone_pivots)
    ids=np.zeros((len(bind),4),dtype=int);weights=np.zeros((len(bind),4))
    for i,ws in enumerate(meshes[0]['weights']):
        for k,(bone,weight) in enumerate(ws):ids[i,k]=bone;weights[i,k]=weight
    # Preserve even small shear and source weights beyond M2's four influences, rather than loosening the motion gate.
    for clip in clips:
        for frame,reference in zip(clip['transforms'],clip['vertices']):
            loc=np.asarray([b[0] for b in frame]);q=np.asarray([b[1] for b in frame]);scale=np.asarray([b[2] for b in frame]);points=np.zeros_like(bind)
            for k in range(4):
                bones=ids[:,k];v=(bind-pivots[bones])*scale[bones];quat=q[bones]
                points+=(v+2*np.cross(quat[:,:3],np.cross(quat[:,:3],v)+quat[:,3,None]*v)+pivots[bones]+loc[bones])*weights[:,k,None]
            affected.update(np.flatnonzero(np.linalg.norm(points-np.asarray(reference),axis=1)>0.00001).tolist())
helpers=sorted(affected)
for clip in clips:
    for frame in range(clip['frames']):
        for index in helpers:
            bind=Vector(meshes[0]['vertices'][index]);position=Vector(clip['vertices'][frame][index])
            original=Vector(meshes[0]['normals'][index]);normal=Vector(clip['normals'][frame][index])
            q=original.rotation_difference(normal)
            clip['transforms'][frame].append([list(position-bind),[q.x,q.y,q.z,q.w],[1,1,1]])
    clip['transforms']=[frame[:len(bone_names)]+[[[0,0,0],[0,0,0,1],[1,1,1]]]+frame[len(bone_names):] for frame in clip['transforms']]
    # Validate exported linear-skin positions against Blender at several frames, including facial helpers.
    max_error=0
    pivots=bone_pivots+[Vector((0,0,0))]+[Vector(meshes[0]['vertices'][i]) for i in helpers]
    for frame in sorted({0,clip['frames']//3,clip['frames']//2,clip['frames']-1}):
        for index in range(0,len(dog.data.vertices),7):
            bind=Vector(meshes[0]['vertices'][index]);ws=meshes[0]['weights'][index]
            if index in affected:ws=[(len(bone_names)+1+helpers.index(index),1)]
            result=Vector()
            from mathutils import Quaternion
            for bone,weight in ws:
                loc,quat,scale=clip['transforms'][frame][bone];q=Quaternion((quat[3],quat[0],quat[1],quat[2]))
                result+=(q@Vector(tuple((bind-pivots[bone])[k]*scale[k] for k in range(3)))+pivots[bone]+Vector(loc))*weight
            max_error=max(max_error,(result-Vector(clip['vertices'][frame][index])).length)
    if max_error>0.002:raise ValueError(f'Animation reconstruction error {max_error}')
    clip['sample_max_position_error']=max_error
    del clip['vertices'];del clip['normals']
for index in helpers:meshes[0]['weights'][index]=[(len(bone_names)+1+helpers.index(index),1)]
result=dict(meshes=meshes,bones=[dict(name=name,pivot=list(pivot)) for name,pivot in zip(bone_names,bone_pivots)]+[dict(name='carrier',pivot=[0,0,0])]+
    [dict(name='face_vertex_'+str(i),pivot=meshes[0]['vertices'][i]) for i in helpers],clips=clips,
    facial_helper_count=len(helpers),normalization=[list(row) for row in basis],
    seating={'yaw_degrees':math.degrees(angle),'paw_center_before':list(center),'paw_center_after':list(target),
             'matrix':[list(row) for row in seating]})
if spec:result['asset']=spec
(out/'baked.json').write_text(json.dumps(result,separators=(',',':')))
print('WOLF_BAKE='+json.dumps(dict(bones=len(result['bones']),facial_helpers=len(helpers),
    clips=[{k:c[k] for k in ['name','frames','fps','animated_shape_keys','sample_max_position_error']} for c in clips])))
