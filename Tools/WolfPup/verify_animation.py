"""Compare emitted, quantized M2 bone keys with the sampled original idle/facial transforms."""
import json,struct,math,sys,bisect
from pathlib import Path
from build_model import playback_clip

def verify(root):
    baked=json.loads((root/'baked.json').read_text());data=(root/'patch-root'/baked.get('asset',{}).get('model','custom/fortnite/pets/wolf_pup.m2')).read_bytes()
    clips=[playback_clip(baked['clips'])]
    count,bones=struct.unpack_from('<II',data,44);assert count==len(baked['bones'])
    error=0;moving=0
    for i in range(count):
        for ci,clip in enumerate(clips):
            for ch,(offset,stride,width) in enumerate([(16,12,3),(36,8,4),(56,12,3)]):
                n,outer=struct.unpack_from('<II',data,bones+88*i+offset+12);assert n==len(clips)
                size,keys=struct.unpack_from('<II',data,outer+8*ci)
                assert size==1 or size>=clip['frames']
                _,time_outer=struct.unpack_from('<II',data,bones+88*i+offset+4)
                nt,time_keys=struct.unpack_from('<II',data,time_outer+8*ci);assert nt==size
                times=struct.unpack_from('<'+'I'*nt,data,time_keys)
                first=None
                for frame in sorted({0,clip['frames']//3,clip['frames']//2,clip['frames']-1}):
                    index=0 if size==1 else bisect.bisect_left(times,clip['times'][frame])
                    if size!=1:assert times[index]==clip['times'][frame]
                    record=keys+index*stride
                    if ch==1:
                        signed=struct.unpack_from('<4h',data,record)
                        value=[(v&65535)*2/65535-1 for v in signed]
                    else:value=struct.unpack_from('<3f',data,record)
                    source=clip['transforms'][frame][i][ch]
                    if ch==1:
                        length=math.sqrt(sum(v*v for v in source));assert length>1e-8
                        source=[v/length for v in source]
                    if ch==1 and sum(a*b for a,b in zip(value,source))<0:source=[-v for v in source]
                    error=max(error,max(abs(a-b) for a,b in zip(value,source)))
                    if first is None:first=value
                    elif max(abs(a-b) for a,b in zip(value,first))>1e-5:moving+=1
    assert error<0.0001,error
    assert moving>0,'Idle animation unexpectedly frozen'
    report={'original_clips_retained':len(baked['clips']),'bone_key_max_error':error,
            'changing_channel_samples':moving,'facial_helpers':baked['facial_helper_count'],
            'blender_vertex_max_error':max(c['sample_max_position_error'] for c in baked['clips'])}
    (root/'animation-verified.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))

if __name__=='__main__':verify(Path(sys.argv[1] if len(sys.argv)>1 else 'G:/FortniteCosmetics/wolf-pup-v1'))
