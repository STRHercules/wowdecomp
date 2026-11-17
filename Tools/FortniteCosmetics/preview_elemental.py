"""Offline render of the emitted M2 surfaces and UV atlas phase; native client acceptance remains separate."""
import bpy,json,struct,sys,math
from pathlib import Path
from mathutils import Vector

work=Path('G:/FortniteCosmetics/elemental-swords-20261114')
rows=json.loads((work/'catalog.json').read_text())
scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.samples=24
scene.render.resolution_x=640;scene.render.resolution_y=640;scene.render.resolution_percentage=100
scene.world.color=(.07,.07,.07)
for row in rows:
    bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
    model=(work/'patch-root'/row['model']).read_bytes();skin=(work/'patch-root'/(row['model'][:-3]+'00.skin')).read_bytes()
    n,p=struct.unpack_from('<II',model,60);vertices=[];uvs=[]
    for i in range(n):
        v=struct.unpack_from('<3f4B4B3f4f',model,p+48*i);vertices.append(v[:3]);uvs.append(v[-4:-2])
    ni,ip=struct.unpack_from('<II',skin,12);indices=struct.unpack_from('<'+'H'*ni,skin,ip)
    nm,mp=struct.unpack_from('<II',skin,28);materials=[]
    for i in range(nm):
        section=struct.unpack_from('<I8H7f',skin,mp+48*i);start,count=section[3:5]
        mesh=bpy.data.meshes.new('surface');mesh.from_pydata(vertices,[],[indices[j:j+3] for j in range(start,start+count,3)])
        obj=bpy.data.objects.new('surface',mesh);scene.collection.objects.link(obj)
        layer=mesh.uv_layers.new()
        for loop in mesh.loops:layer.data[loop.index].uv=(uvs[loop.vertex_index][0],1-uvs[loop.vertex_index][1])
        material=bpy.data.materials.new(str(row['display'])+'_'+str(i));material.use_nodes=True
        nodes=material.node_tree.nodes;nodes.clear();output=nodes.new('ShaderNodeOutputMaterial');tex=nodes.new('ShaderNodeTexImage')
        effect=i in row['animation']['animated_materials']
        texture=(work/str(row['display'])/'effect-atlas.png') if effect else Path(row['textures'][i])
        tex.image=bpy.data.images.load(str(texture),check_existing=True)
        if effect or (row['effect']=='plasma' and i==0):
            emission=nodes.new('ShaderNodeEmission');emission.inputs['Strength'].default_value=2
            material.node_tree.links.new(tex.outputs['Color'],emission.inputs['Color'])
            transparent=nodes.new('ShaderNodeBsdfTransparent');mix=nodes.new('ShaderNodeMixShader')
            material.node_tree.links.new(tex.outputs['Alpha'],mix.inputs[0]);material.node_tree.links.new(transparent.outputs[0],mix.inputs[1]);material.node_tree.links.new(emission.outputs[0],mix.inputs[2]);material.node_tree.links.new(mix.outputs[0],output.inputs['Surface'])
        else:
            shader=nodes.new('ShaderNodeBsdfPrincipled');shader.inputs['Roughness'].default_value=.55
            material.node_tree.links.new(tex.outputs['Color'],shader.inputs['Base Color']);material.node_tree.links.new(shader.outputs[0],output.inputs['Surface'])
        mesh.materials.append(material);materials.append((mesh,layer,effect))
    camera_data=bpy.data.cameras.new('camera');camera=bpy.data.objects.new('camera',camera_data);scene.collection.objects.link(camera);scene.camera=camera
    center=Vector(((max(v[0] for v in vertices)+min(v[0] for v in vertices))/2,0,0));camera.location=center+Vector((.1,-.15,2))
    camera.rotation_euler=(center-camera.location).to_track_quat('-Z','Y').to_euler();camera_data.type='ORTHO';camera_data.ortho_scale=1.05
    light_data=bpy.data.lights.new('key','AREA');light=bpy.data.objects.new('key',light_data);scene.collection.objects.link(light);light.location=center+Vector((0,-.2,1.5));light.rotation_euler=(center-light.location).to_track_quat('-Z','Y').to_euler();light_data.energy=80;light_data.size=1
    for phase in (0,21,42):
        for mesh,layer,effect in materials:
            if effect:
                for loop in mesh.loops:
                    u,v=uvs[loop.vertex_index];layer.data[loop.index].uv=(u+phase%8/8,1-(v+phase//8/8))
        scene.render.filepath=str(work/str(row['display'])/('preview-'+str(phase)+'.png'));bpy.ops.render.render(write_still=True)
