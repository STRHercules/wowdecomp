"""Blender (headless) step: Meshy .glb -> the densest decimation whose vertices fit the client's 16-bit skin indices,
with its own UV atlas and the original colour baked onto it, plus a small collision hull. Dumps JSON + PNG for build.py.

    blender -b --factory-startup --python export_blender.py -- <in.glb> <out.json> <out.png> [start_tris] [tex_size]

Why bake (2026-11-23): Meshy's UV layout is thousands of tiny islands. Decimating 1-2M triangles down to ~70k drags
those seams around (smeared text, streaks across the atlas) and the seam duplicates eat the vertex budget (0.93
vertices per triangle). Smart UV project straight on the decimated surface shatters just as badly (its normals are
noisy), so the atlas is projected on a heavily smoothed copy (same topology) and copied back: ~0.66 vertices per
triangle, and the colour is resampled from the full-resolution model. An earlier bake (2026-11-22) looked darker; save
through image.save() (raw pixels), never save_render (view transform).
Blender's glTF importer already yields Z-up with the model's front facing -Y; build.py turns that into WoW's +X front.
"""
import json
import math
import sys

import bpy

args = sys.argv[sys.argv.index("--") + 1:]
source, out, out_png = args[0], args[1], args[2]
target = int(args[3]) if len(args) > 3 else 120000
size = int(args[4]) if len(args) > 4 else 2048
VERTEX_LIMIT, COLLISION_TRIS = 65000, 400

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=source)
meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
assert len(meshes) == 1, f"expected one mesh, got {len(meshes)}"
high = meshes[0]
faces = sum(len(p.vertices) - 2 for p in high.data.polygons)


def apply_all(obj):
    bpy.context.view_layer.objects.active = obj
    for m in list(obj.modifiers):
        bpy.ops.object.modifier_apply(modifier=m.name)


def decimated(tris, normals=True):
    copy = high.copy()
    copy.data = high.data.copy()
    copy.data.materials.clear()
    bpy.context.collection.objects.link(copy)
    # glTF arrives unwelded (every UV island is a separate shell); decimating open shells independently opens
    # hairline gaps between them, so weld first.
    copy.modifiers.new("weld", "WELD").merge_threshold = 1e-5
    copy.modifiers.new("dec", "DECIMATE").ratio = min(1.0, tris / faces)
    copy.modifiers.new("tri", "TRIANGULATE")
    if normals:  # welding drops Meshy's split normals; take them back
        n = copy.modifiers.new("normals", "DATA_TRANSFER")
        n.object, n.use_loop_data = high, True
        n.data_types_loops, n.loop_mapping = {"CUSTOM_NORMAL"}, "POLYINTERP_NEAREST"
    apply_all(copy)
    return copy


def unwrap(low):
    proxy = low.copy()
    proxy.data = low.data.copy()
    bpy.context.collection.objects.link(proxy)
    smooth = proxy.modifiers.new("smooth", "SMOOTH")
    smooth.factor, smooth.iterations = 1.0, 40
    apply_all(proxy)
    bpy.ops.object.select_all(action="DESELECT")
    proxy.select_set(True)
    bpy.context.view_layer.objects.active = proxy
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(66), island_margin=0.004)
    bpy.ops.object.mode_set(mode="OBJECT")
    for layer in list(low.data.uv_layers):
        low.data.uv_layers.remove(layer)
    baked = low.data.uv_layers.new(name="baked")
    src = proxy.data.uv_layers.active.data
    for i, loop in enumerate(baked.data):  # same topology, same loop order
        loop.uv = src[i].uv
    bpy.data.objects.remove(proxy)


def corners(mesh):
    uv = mesh.uv_layers.active.data
    index_of, vertices, normals, uvs, indices = {}, [], [], [], []
    for poly in mesh.polygons:
        flat = None if poly.use_smooth else poly.normal
        for li in poly.loop_indices:
            loop = mesh.loops[li]
            nn = flat if flat is not None else loop.normal
            u, v = uv[li].uv
            key = (loop.vertex_index, round(u, 5), round(v, 5), round(nn.x, 3), round(nn.y, 3), round(nn.z, 3))
            if key not in index_of:
                index_of[key] = len(vertices)
                vertices.append(tuple(mesh.vertices[loop.vertex_index].co))
                normals.append(tuple(nn))
                uvs.append((u, 1.0 - v))  # Blender V is bottom-up, BLP is top-down
            indices.append(index_of[key])
    return vertices, normals, uvs, indices


while True:
    low = decimated(target)
    unwrap(low)
    vertices, normals, uvs, indices = corners(low.data)
    print(f"try {target} tris -> {len(vertices)} verts", flush=True)
    if len(vertices) <= VERTEX_LIMIT:
        break
    bpy.data.objects.remove(low)
    target = int(target * VERTEX_LIMIT / len(vertices) * 0.98)

# Bake the original colour (an emission pass: no lighting, exact texels) from the high-poly onto the atlas.
color = next(n.image for n in high.active_material.node_tree.nodes if n.type == "TEX_IMAGE"
             and any(l.to_socket.name == "Base Color" for l in n.outputs["Color"].links))
emit = bpy.data.materials.new("emit")
emit.use_nodes = True
nodes = emit.node_tree.nodes
nodes.clear()
tex, shader, output = nodes.new("ShaderNodeTexImage"), nodes.new("ShaderNodeEmission"), nodes.new("ShaderNodeOutputMaterial")
tex.image = color
tex.interpolation = "Cubic"
emit.node_tree.links.new(tex.outputs["Color"], shader.inputs["Color"])
emit.node_tree.links.new(shader.outputs["Emission"], output.inputs["Surface"])
high.data.materials.clear()
high.data.materials.append(emit)

target_image = bpy.data.images.new("baked", size, size, alpha=False)
bake = bpy.data.materials.new("bake")
bake.use_nodes = True
node = bake.node_tree.nodes.new("ShaderNodeTexImage")
node.image = target_image
bake.node_tree.nodes.active = node
low.data.materials.append(bake)

scene = bpy.context.scene
scene.render.engine = "CYCLES"
scene.cycles.device = "CPU"
scene.cycles.samples = 4
dims = max(high.dimensions)
bpy.ops.object.select_all(action="DESELECT")
high.select_set(True)
low.select_set(True)
bpy.context.view_layer.objects.active = low
scene.render.bake.use_selected_to_active = True
scene.render.bake.cage_extrusion = dims * 0.01
scene.render.bake.max_ray_distance = dims * 0.03
scene.render.bake.margin = 16
scene.render.bake.margin_type = "EXTEND"
bpy.ops.object.bake(type="EMIT")
target_image.filepath_raw = out_png
target_image.file_format = "PNG"
target_image.save()

hull = decimated(COLLISION_TRIS, normals=False)
result = {
    "vertices": vertices, "normals": normals, "uvs": uvs, "indices": indices,
    "collision_vertices": [tuple(v.co) for v in hull.data.vertices],
    "collision_indices": [i for p in hull.data.polygons for i in p.vertices],
}
with open(out, "w") as f:
    json.dump(result, f)
print(f"visual {len(indices)//3} tris / {len(vertices)} verts, collision {len(hull.data.polygons)} tris, baked {out_png}")
