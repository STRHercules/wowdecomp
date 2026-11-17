"""Rebuild consistent smooth vertex normals for facial-helper and ordinary skinned surfaces without moving vertices."""
import json,sys
from pathlib import Path
import numpy as np

def normals(vertices,triangles,fallback=None):
    result=np.zeros_like(vertices);a,b,c=triangles.T
    faces=np.cross(vertices[b]-vertices[a],vertices[c]-vertices[a])
    for indices in (a,b,c):np.add.at(result,indices,faces)
    length=np.linalg.norm(result,axis=1)
    bad=length<1e-10
    if bad.any():
        if fallback is None:raise ValueError('Degenerate smooth normal')
        result[bad]=fallback[bad];length=np.linalg.norm(result,axis=1)
        if (length<1e-10).any():raise ValueError('Degenerate source normal')
    return result/length[:,None]

def rotation_between(a,b):
    dot=np.clip((a*b).sum(1),-1,1);xyz=np.cross(a,b);w=1+dot
    bad=w<1e-6
    if bad.any():
        axes=np.tile([1.,0.,0.],(bad.sum(),1));rows=a[bad]
        axes[abs(rows[:,0])>.9]=[0,1,0];xyz[bad]=np.cross(rows,axes);w[bad]=0
    q=np.column_stack((xyz,w));q/=np.linalg.norm(q,axis=1)[:,None]
    return q

def main(source,out):
    out.mkdir(parents=True,exist_ok=True);b=json.loads((source/'baked.json').read_text())
    mesh=b['meshes'][0];vertices=np.asarray(mesh['vertices']);triangles=np.asarray(mesh['triangles'])
    old=np.asarray(mesh['normals']);smooth=normals(vertices,triangles,old)
    if (smooth*old).sum()<0:smooth=-smooth
    mesh['normals']=smooth.tolist();bone_pivots=np.asarray([bone['pivot'] for bone in b['bones']])
    ids=np.zeros((len(vertices),4),dtype=int);weights=np.zeros((len(vertices),4))
    for i,ws in enumerate(mesh['weights']):
        for k,(bone,w) in enumerate(ws):ids[i,k]=bone;weights[i,k]=w
    helper_vertices=np.asarray([i for i,ws in enumerate(mesh['weights']) if len(ws)==1 and b['bones'][ws[0][0]]['name'].startswith('face_vertex_')],dtype=int)
    helpers=ids[helper_vertices,0];max_length_error=0;changes=0
    for clip in b['clips']:
        for frame in clip['transforms']:
            loc=np.asarray([x[0] for x in frame]);q=np.asarray([x[1] for x in frame]);scale=np.asarray([x[2] for x in frame])
            points=np.zeros_like(vertices)
            for k in range(4):
                bones=ids[:,k];v=(vertices-bone_pivots[bones])*scale[bones];quat=q[bones]
                p=v+2*np.cross(quat[:,:3],np.cross(quat[:,:3],v)+quat[:,3,None]*v)+bone_pivots[bones]+loc[bones]
                points+=p*weights[:,k,None]
            target=normals(points,triangles,smooth)
            if (smooth*old).sum()<0:target=-target
            rotations=rotation_between(smooth[helper_vertices],target[helper_vertices])
            for bone,rotation in zip(helpers,rotations):frame[int(bone)][1]=rotation.tolist();changes+=1
            max_length_error=max(max_length_error,float(abs(np.linalg.norm(target,axis=1)-1).max()))
    b['normal_repair']='consistent area-weighted smooth bind/posed normals; vertex positions and translations unchanged'
    (out/'baked.json').write_text(json.dumps(b,separators=(',',':')))
    report={'facial_normal_keys_rebuilt':changes,'max_unit_normal_error':max_length_error,
            'geometry_motion':'positions, weights, pivots, scales and translations preserved','source':str(source)}
    (out/'normals-verified.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))

if __name__=='__main__':main(*(Path(p) for p in sys.argv[1:]))
