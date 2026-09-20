#!/usr/bin/env python3
"""Host-only spatial-shaft witness using actual captured depth and replay maps.

Uses frozen qualified family atlases and camera inputs; this is arithmetic
validation on already-fogged captures, not a visual acceptance or GPU claim.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import time
import numpy as np
from fog_spatial_run import helper
from fog_spatial_build import digest
import fog_spatial_reference as spatial
import fog_shadow_reference as shadow

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--data',type=Path,required=True);ap.add_argument('--dump',type=Path,required=True);ap.add_argument('--prototype-root',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);ap.add_argument('--step',type=int,default=8);a=ap.parse_args()
    if a.step<1:raise ValueError('positive sampling step required')
    start=time.monotonic();sampler=helper(a.prototype_root);log=next(a.dump.glob('session-*.log'))
    frames={'1974':'bluewell','9204':'bluewell','21901':'foggreenoutlands','26447':'foggreenoutlands'}
    lines=subprocess.run(['rg',r'^sun_shadow_apply_params device=1 frame=(1974|9204|21901|26447) ',str(log)],check=True,text=True,capture_output=True).stdout.splitlines()
    params={v['frame']:v for v in (dict(re.findall(r'(\w+)=([^ ]+)',line)) for line in lines)}
    if set(params)!=set(frames):raise ValueError('missing exact captured projection/map metadata')
    inputs={};records=[];volumes={}
    for frame,family in frames.items():
        folder=a.data/frame;cpath=folder/'constants.f32';dpath=folder/'depth.rgba32f';c=np.fromfile(cpath,'<f4').reshape(8,4);h,w=int(c[1,1]),int(c[1,0])
        depth=np.fromfile(dpath,'<f4').reshape(h,w,4);y,x=np.mgrid[0:h:a.step,0:w:a.step];selected=depth[y,x];direction,limit,invalid=spatial.rays(selected,c,x,y)
        for path in (cpath,dpath):inputs[str(path.resolve())]=digest(path)
        if family not in volumes:
            path=a.data/(family+'.atlas16f');volumes[family]=sampler.volume_from_atlas(np.fromfile(path,'<f2').reshape(1430,1560,4));inputs[str(path.resolve())]=digest(path)
        p=params[frame];cascades=[]
        for slot in range(1,4):
            source=int(p['source'+str(slot)]);size=int(p['map'+str(slot)]);path=a.dump/f'shadow_map{source}_1_{frame}.r32f'
            valid=p['valid'+str(slot)]=='1' and p['map_frame'+str(slot)]==frame
            if not valid:raise ValueError('this arithmetic witness needs same-frame captured maps')
            depthmap=np.fromfile(path,'<f4').reshape(size,size)
            if not np.isfinite(depthmap).all():raise ValueError('nonfinite map')
            inputs[str(path.resolve())]=digest(path)
            cascades.append(dict(rows=np.array(p['rows'+str(slot)].split(','),np.float32).reshape(3,4),map=depthmap,bias=float(p['bias'+str(slot)]),valid=True))
        baseline=spatial.march(sampler,volumes[family],c,direction,limit)
        actual=spatial.march(sampler,volumes[family],c,direction,limit,visibility=shadow.world_visibility(c,cascades))
        absent=spatial.march(sampler,volumes[family],c,direction,limit,visibility=shadow.world_visibility(c,[]))
        empty=np.all(baseline[:,:,:3]==0,axis=-1)&(baseline[:,:,3]==1)
        delta=baseline[:,:,:3]-actual[:,:,:3];nonzero=np.max(delta,axis=-1)>1e-9
        passed=bool(np.array_equal(baseline,absent) and np.array_equal(baseline[:,:,3],actual[:,:,3]) and np.array_equal(baseline[empty],actual[empty]) and np.isfinite(actual).all() and np.min(delta)>=-1e-7)
        records.append(dict(frame=int(frame),family=family,samples=int(limit.size),shadowed_samples=int(nonzero.sum()),shadowed_fraction=float(nonzero.mean()),scattering_reduction_max=float(delta.max()),scattering_reduction_mean=float(delta.mean()),transmission_identical=bool(np.array_equal(baseline[:,:,3],actual[:,:,3])),unavailable_identical=bool(np.array_equal(baseline,absent)),empty_samples=int(empty.sum()),passed=passed,map_metadata={k:v for k,v in p.items() if k.startswith(('rows','bias','map_frame','source'))}))
    report=dict(passed=all(r['passed'] for r in records) and sum(r['shadowed_samples'] for r in records)>0,cases=records,inputs=inputs,seconds=time.monotonic()-start,sources={str(Path(__file__).resolve()):digest(__file__),str(Path(shadow.__file__).resolve()):digest(shadow.__file__),str(Path(spatial.__file__).resolve()):digest(spatial.__file__)},limitations=['Host arithmetic, not production GPU execution',f'{a.step}-pixel captured grid; no flight visual acceptance','Existing captures already contain fog','Tracked radiance unavailable in frozen inputs: qualified white radiance'])
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(passed=report['passed'],seconds=report['seconds'],cases=[{k:r[k] for k in ('frame','samples','shadowed_samples','scattering_reduction_max','transmission_identical','empty_samples')} for r in records])))
    return 0 if report['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
