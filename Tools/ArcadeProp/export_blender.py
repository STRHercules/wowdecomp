"""Blender (headless) step: Meshy .glb -> densest decimation whose unique vertices fit the client's 16-bit skin indices,
keeping Meshy's own UVs, plus a small collision hull. Dumps JSON for build.py.

    blender -b --factory-startup --python export_blender.py -- <in.glb> <out.json> [start_tris]

Tried and rejected (2026-11-22): re-unwrap + bake from the high-poly. Smart-project shatters Meshy's noisy surface into
tiny islands, so fewer triangles fit and the bake looked darker with its own seams.
Blender's glTF importer already yields Z-up with the model's front facing -Y; build.py turns that into WoW's +X front.
"""
import json
import sys

import bpy

args = sys.argv[sys.argv.index("--") + 1:]
source, out = args[0], args[1]
target = int(args[2]) if len(args) > 2 else 130000
VERTEX_LIMIT, COLLISION_TRIS = 65000, 400

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=source)
meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
assert len(meshes) == 1, f"expected one mesh, got {len(meshes)}"
high = meshes[0]
faces = sum(len(p.vertices) - 2 for p in high.data.polygons)


def decimated(tris):
    copy = high.copy()
    copy.data = high.data.copy()
    bpy.context.collection.objects.link(copy)
    # glTF arrives unwelded (every UV island is a separate shell); decimating open shells independently opens
    # hairline gaps between them, so weld first and keep the islands as UV seams on one watertight surface.
    copy.modifiers.new("weld", "WELD").merge_threshold = 1e-5
    copy.modifiers.new("dec", "DECIMATE").ratio = min(1.0, tris / faces)
    copy.modifiers.new("tri", "TRIANGULATE")
    normals = copy.modifiers.new("normals", "DATA_TRANSFER")  # welding drops Meshy's split normals; take them back
    normals.object, normals.use_loop_data = high, True
    normals.data_types_loops, normals.loop_mapping = {"CUSTOM_NORMAL"}, "POLYINTERP_NEAREST"
    mesh = bpy.data.meshes.new_from_object(copy.evaluated_get(bpy.context.evaluated_depsgraph_get()))
    mesh.transform(copy.matrix_world)
    bpy.data.objects.remove(copy)
    return mesh


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
    vertices, normals, uvs, indices = corners(decimated(target))
    print(f"try {target} tris -> {len(vertices)} verts", flush=True)
    if len(vertices) <= VERTEX_LIMIT:
        break
    target = int(target * VERTEX_LIMIT / len(vertices) * 0.98)

hull = decimated(COLLISION_TRIS)
result = {
    "vertices": vertices, "normals": normals, "uvs": uvs, "indices": indices,
    "collision_vertices": [tuple(v.co) for v in hull.vertices],
    "collision_indices": [i for p in hull.polygons for i in p.vertices],
}
with open(out, "w") as f:
    json.dump(result, f)
print(f"visual {len(indices)//3} tris / {len(vertices)} verts, collision {len(hull.polygons)} tris")
