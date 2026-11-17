import bpy,json
out={}
for name in ('Pets_Wolf_Pup','WolfPack_Backpack','Pets_Wolf_Pup_LOD0','WolfPack_Backpack_LOD0'):
 o=bpy.data.objects[name]
 out[name]={'matrix':[list(r) for r in o.matrix_world],'type':o.type,'parent':o.parent.name if o.parent else None}
 if o.type=='ARMATURE':
  out[name]['bones']=[{'name':b.name,'parent':b.parent.name if b.parent else None,'pivot':list(b.head_local),'matrix':[list(r) for r in b.matrix_local],'props':dict(b.items())} for b in list(o.data.bones)[:4]]
 else:
  used={g.group for v in o.data.vertices for g in v.groups if g.weight>1e-6}
  out[name]['used_bones']=[o.vertex_groups[i].name for i in sorted(used)]
  out[name]['textures']=[{'material':m.name,'images':[(n.image.name,n.image.filepath) for n in m.node_tree.nodes if n.type=='TEX_IMAGE' and n.image]} for m in o.data.materials]
print('REFERENCE='+json.dumps(out))
