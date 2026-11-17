"""Render the sampled facial rig before/after neutral-pose retargeting for offline visual review."""
import sys,json
from pathlib import Path
import bpy
from mathutils import Vector

spec_path=Path(sys.argv[sys.argv.index('--spec')+1]);spec=json.loads(spec_path.read_text())
output=Path(sys.argv[sys.argv.index('--')+1]);output.mkdir(parents=True,exist_ok=True)
prefix=Path(__file__).with_name('export_blender.py').read_text().split('idle_path=clip_paths[0]')[0]
exec(compile(prefix,'export_blender.py','exec'),globals())
for obj,slots in ((dog,spec['pet_materials']),(carrier,spec['carrier_materials'])):
    for index,slot in enumerate(slots):
        material=bpy.data.materials.new(obj.name+str(slot));material.use_nodes=True
        nodes=material.node_tree.nodes;shader=nodes.get('Principled BSDF')
        image=nodes.new('ShaderNodeTexImage');image.image=bpy.data.images.load(spec['textures'][slot])
        material.node_tree.links.new(image.outputs['Color'],shader.inputs['Base Color']);shader.inputs['Roughness'].default_value=.85
        obj.data.materials[index]=material
scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.samples=16
scene.render.resolution_x=480;scene.render.resolution_y=480;scene.render.resolution_percentage=100
scene.world.color=(.3,.3,.3)
camera_data=bpy.data.cameras.new('inspection');camera=bpy.data.objects.new('inspection',camera_data);scene.collection.objects.link(camera);scene.camera=camera
camera_data.type='ORTHO';camera_data.ortho_scale=.42
light_data=bpy.data.lights.new('softbox','AREA');light=bpy.data.objects.new('softbox',light_data);scene.collection.objects.link(light)
light_data.energy=120;light_data.shape='DISK';light_data.size=2
for repaired in (False,True):
    spec['facial_bind_retarget']=repaired
    sample_frame=int(sys.argv[sys.argv.index('--frame')+1]) if '--frame' in sys.argv else 40
    action,dto=import_clip(clip_paths[0]);evaluate_frame(sample_frame,dto)
    head=arm.pose.bones.get('head') or arm.pose.bones.get('Head')
    eyes=[arm.pose.bones.get(name) for name in ('L_eye','R_eye')]
    center=sum((arm.matrix_world@b.matrix.translation for b in eyes if b),Vector())/2
    jaw=arm.pose.bones.get('C_jaw')
    forward=center-(arm.matrix_world@jaw.matrix.translation);forward.z=0
    nose=arm.pose.bones.get('C_nose_bridge')
    if nose:forward=(arm.matrix_world@nose.matrix.translation)-center
    if forward.length<.01:forward=Vector((0,-1,0))
    if spec['slug']=='animated_woodsy':forward=-forward
    if spec['slug']=='animated_woodsy' and spec.get('facial_bind_retarget'):
        skin=head.matrix@head.bone.matrix_local.inverted()
        center=skin@((arm.data.bones['L_eye'].matrix_local.translation+arm.data.bones['R_eye'].matrix_local.translation)/2)
        forward=(skin.to_3x3()@Vector((0,-1,0))).normalized()
    forward.normalize();camera.location=center+forward*.9+Vector((.12,0,.08))
    camera.rotation_euler=(center-camera.location).to_track_quat('-Z','Y').to_euler()
    light.location=center+forward*.6+Vector((.2,0,.5));light.rotation_euler=(center-light.location).to_track_quat('-Z','Y').to_euler()
    scene.render.filepath=str(output/('retargeted.png' if repaired else 'shared.png'));bpy.ops.render.render(write_still=True)
    print('FACIAL_INSPECT',spec['slug'],repaired,'jaw_rotation',list(jaw.rotation_quaternion),flush=True)
