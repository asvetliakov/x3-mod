#!/usr/bin/env python3
"""Bind reviewed resident workloads; do not regenerate/copy frozen captures."""
import argparse
import json
import re
import subprocess
from pathlib import Path
import numpy as np
from fog_spatial_build import digest,ROOT
from fog_spatial_run import FRAMES,windows
FILES=('constants.f32','depth.rgba32f','scene.rgba16f')

def repair_count(depth):
    geom=(depth[...,0]>=0)&(depth[...,0]<=1)
    if not np.isfinite(depth).all() or np.any(geom&(depth[...,2]<=0)):raise ValueError('finite positive captured depth required')
    h,w=geom.shape;y,x=np.mgrid[:h,:w];present=np.zeros((h,w),bool)
    for dy in (0,1):
        for dx in (0,1):
            qx=np.minimum(x//2+dx,(w+1)//2-1);qy=np.minimum(y//2+dy,(h+1)//2-1)
            present|=(geom==geom[2*qy,2*qx])&((x%2!=0) if dx else True)&((y%2!=0) if dy else True)
    return int((~present).sum())

def case_text(manifest):
    lines=[]
    for c in manifest['cases']:
        lines.extend([c['name'],c['family'],windows(c['directory']),'1' if c['family']=='bluewell' else '2'])
        lines.extend(windows(c['expected'][kind]) if c['expected'][kind] else '-' for kind in ('composite','st'))
        if manifest.get('paired_shadows'):lines.extend([windows(c['shadow_metadata']),'1' if c['repair_stress'] else '0'])
    return '\n'.join(lines)+'\n'

def prepare(workload,accepted,output,shadow_dump=None):
    workload,accepted,output=workload.resolve(),accepted.resolve(),output.resolve()
    if output.exists():raise ValueError('new preparation directory required')
    original=json.loads(workload.read_text());report=json.loads(accepted.read_text())
    if original['checkpoint']!='performance-only' or len(original['cases'])!=8:raise ValueError('qualified timing workload')
    if not report['passed'] or not report['numerical']['passed'] or len(report['numerical']['cases'])!=(40 if shadow_dump else 32):raise ValueError('accepted production four-view report')
    hashes=report['numerical']['readback_hashes'];inputs={str(workload):digest(workload),str(accepted):digest(accepted)}
    build_path=accepted.parent.parent/'build.json';build=json.loads(build_path.read_text())
    if digest(build_path)!=report['build_sha256']:raise ValueError('accepted build binding')
    inputs[str(build_path)]=digest(build_path)
    for rel in ('src/renderer/fog_pass.cpp','src/renderer/fog_pass.h','src/renderer/fog_volume_math.h','src/fog/fog_field_inc.h','src/fog/fog_march_ps.hlsl','src/fog/fog_composite_ps.hlsl','src/renderer/fog_march_program_inc.h','src/renderer/fog_composite_program_inc.h'):
        matching=[h for path,h in build['inputs'].items() if Path(path).as_posix().endswith('/'+rel)]
        if len(matching)!=1 or digest(ROOT/rel)!=matching[0]:raise ValueError('qualified production input changed: '+rel)
        inputs[str(ROOT/rel)]=matching[0]
    profiles=[dict(name=f'{w}x{h}',width=w,height=h,warm=16,samples=64) for w,h in ((1280,768),(1920,1080))]
    if [(p['width'],p['height']) for p in original['profiles']]!=[(1280,768),(1920,1080)]:raise ValueError('workload profiles')
    cases=[]
    for profile in profiles:
        for frame,family in FRAMES.items():
            source=[c for c in original['cases'] if str(c['frame'])==frame and c['profile']==profile['name']]
            if len(source)!=1:raise ValueError('balanced workload case')
            source=source[0];directory=Path(source['directory']);w,h=profile['width'],profile['height']
            if source['family']!=family or (source['width'],source['height'])!=(w,h):raise ValueError('case family/dimensions')
            for name in FILES:
                path=directory/name
                if digest(path)!=source['files'][name]:raise ValueError('frozen workload input changed')
                inputs[str(path.resolve())]=digest(path)
            c=np.fromfile(directory/'constants.f32','<f4').reshape(8,4);d=np.fromfile(directory/'depth.rgba32f','<f4').reshape(h,w,4);scene=np.fromfile(directory/'scene.rgba16f','<f2').reshape(h,w,4)
            sigma=np.float32(2.5e-6 if family=='bluewell' else 6.25e-6)
            if not np.isfinite(c).all() or not np.isfinite(scene).all() or c[2,3]!=sigma or c[7,0]!=0 or not np.array_equal(c[1],[w,h,(w+1)//2,(h+1)//2]):raise ValueError('unchanged24-step workload constants')
            repairs=repair_count(d)
            if repairs!=source['repair_pixels']:raise ValueError('repair count changed')
            expected={kind:None for kind in ('composite','st')}
            if w==1280:
                for kind in expected:
                    path=accepted.parent/f'{frame}-v1.{kind}.rgba16f'
                    if digest(path)!=hashes[path.name]:raise ValueError('accepted borrowed-open readback changed')
                    expected[kind]=str(path.resolve());inputs[str(path.resolve())]=digest(path)
            cases.append(dict(name=source['name'],frame=frame,family=family,profile=profile['name'],width=w,height=h,directory=str(directory.resolve()),expected=expected,repair_pixels=repairs,repair_fraction=repairs/(w*h),label=source['label']))
    result=dict(checkpoint='actual-production-whole-transaction-performance',profiles=profiles,cases=cases,inputs=inputs,accepted_report=str(accepted),accepted_report_sha256=digest(accepted),workload_manifest=str(workload),workload_manifest_sha256=digest(workload),borrowed_scene_open=True,quality_acceptance=False,native_windows='unverified')
    output.mkdir(parents=True)
    if shadow_dump:prepare_shadow_cases(result,report,accepted,shadow_dump.resolve(),output)
    (output/'cases.txt').write_text(case_text(result));result['cases_sha256']=digest(output/'cases.txt')
    (output/'manifest.json').write_text(json.dumps(result,indent=2)+'\n');return result

def shadow_text(frame,entries):
    lines=[str(frame)]
    for c in entries:
        rows=np.asarray(c['rows'],np.float32)
        lines.extend([' '.join(map(str,[c['size'],c['bias'],*rows.reshape(-1).tolist()])),windows(c['path'])])
    return '\n'.join(lines)+'\n'

def verify_repair_depth(depth,original):
    if depth.shape!=original.shape:raise ValueError('repair derivative shape')
    expected=original.copy();expected[::2,::2,0]=.5;expected[::2,::2,2]=np.nan
    if not np.array_equal(depth,expected,equal_nan=True):raise ValueError('repair derivative changed other source pixels')
    # Original normal workload validation already requires finite/positive depth.
    mask=np.ones(depth.shape[:2],bool);mask[::2,::2]=False
    return int(mask.sum())

def validate_shadow_manifest(manifest):
    cases=manifest['cases'];maps=manifest['shadow_maps']
    if [(p['name'],p['width'],p['height']) for p in manifest['profiles']]!=[('1280x768',1280,768),('1920x1080',1920,1080)]:raise ValueError('fixed paired profiles')
    if len(cases)!=10 or set(maps)!=set(FRAMES):raise ValueError('paired shadow cases/maps')
    for profile in manifest['profiles']:
        selected=[c for c in cases if c['profile']==profile['name']]
        if len(selected)!=5 or [c['frame'] for c in selected]!=[*FRAMES,'26447'] or [c['repair_stress'] for c in selected]!=[False]*4+[True]:raise ValueError('paired workload order')
        if any((c['width'],c['height'],c['family'])!=(profile['width'],profile['height'],FRAMES[c['frame']]) or c['shadow_metadata']!=maps[c['frame']]['metadata'] for c in selected):raise ValueError('paired case frame/family/dimensions/maps')
        if (profile['warm_pairs_per_view'],profile['measured_pairs_per_view'])!=(4,16):raise ValueError('paired sample counts')
    for frame,entry in maps.items():
        if len(entry['maps'])!=3:raise ValueError('three current maps required')
        for c in entry['maps']:
            rows=np.asarray(c['rows'],np.float32)
            if c['frame']!=int(frame) or c['source'] not in (1,2,3) or c['size']<64 or rows.shape!=(3,4) or not np.isfinite(rows).all() or not np.isfinite(c['bias']) or not 0<=c['bias']<=1:raise ValueError('current-map metadata')
        if [c['source'] for c in entry['maps']]!=[1,2,3]:raise ValueError('general-cascade order')

def prepare_shadow_cases(result,report,accepted,dump,output):
    variants={(str(r['frame']),r['variant']) for r in report['numerical']['cases'] if r['passed']}
    if variants!={(frame,v) for frame in FRAMES for v in range(10)}:raise ValueError('new shadow numerical qualification required')
    logs=list(dump.glob('session-*.log'))
    if len(logs)!=1:raise ValueError('one captured session log required')
    lines=subprocess.run(['rg',r'^sun_shadow_apply_params device=1 frame=(1974|9204|21901|26447) ',str(logs[0])],check=True,capture_output=True,text=True).stdout.splitlines()
    metadata={}
    for line in lines:
        row=dict(re.findall(r'(\w+)=([^ ]+)',line));frame=row['frame']
        if frame in metadata:raise ValueError('duplicate captured map publication')
        metadata[frame]=row
    if set(metadata)!=set(FRAMES):raise ValueError('missing captured maps')
    result['paired_shadows']=True;result['shadow_maps']={}
    # A small immutable metadata extract is bound instead of hashing the large
    # unrelated session log. Raw captured maps are referenced in place.
    for frame,row in metadata.items():
        target=output/('maps-'+frame+'.txt');entries=[]
        for slot in range(1,4):
            if row['valid'+str(slot)]!='1' or row['map_frame'+str(slot)]!=frame or int(row['source'+str(slot)])!=slot:raise ValueError('stale or wrong cascade')
            size=int(row['map'+str(slot)]);bias=float(row['bias'+str(slot)]);rows=np.asarray(row['rows'+str(slot)].split(','),np.float32).reshape(3,4)
            path=(dump/f'shadow_map{slot}_1_{frame}.r32f').resolve();values=np.fromfile(path,'<f4')
            if values.size!=size*size or not np.isfinite(values).all():raise ValueError('captured R32F extent/finiteness')
            result['inputs'][str(path)]=digest(path)
            entries.append(dict(frame=int(frame),source=slot,size=size,bias=bias,rows=rows.tolist(),path=str(path)))
        target.write_text(shadow_text(frame,entries));result['inputs'][str(target.resolve())]=digest(target)
        result['shadow_maps'][frame]=dict(metadata=str(target.resolve()),maps=entries)
    cases=[]
    for profile in result['profiles']:
        profile['warm_pairs_per_view']=4;profile['measured_pairs_per_view']=16
        ordinary=[c for c in result['cases'] if c['profile']==profile['name']]
        for c in ordinary:c.update(shadow_metadata=result['shadow_maps'][c['frame']]['metadata'],repair_stress=False)
        cases.extend(ordinary);original=ordinary[-1];folder=output/(profile['name']+'-repair-26447');folder.mkdir()
        for name in ('constants.f32','scene.rgba16f'):(folder/name).symlink_to(Path(original['directory'])/name)
        depth=np.fromfile(Path(original['directory'])/'depth.rgba32f','<f4').reshape(original['height'],original['width'],4)
        original_depth=depth.copy();depth[::2,::2,0]=.5;depth[::2,::2,2]=np.nan;verify_repair_depth(depth,original_depth);depth.tofile(folder/'depth.rgba32f')
        repair=np.ones(depth.shape[:2],bool);repair[::2,::2]=False
        expected={kind:None for kind in ('composite','st')}
        if original['width']==1280:
            for kind in expected:
                path=accepted.parent/f'26447-v7.{kind}.rgba16f'
                if digest(path)!=report['numerical']['readback_hashes'][path.name]:raise ValueError('accepted repair readback changed')
                expected[kind]=str(path.resolve());result['inputs'][str(path.resolve())]=digest(path)
        stress=dict(original,name=original['name']+'-repair',directory=str(folder.resolve()),expected=expected,repair_stress=True,repair_pixels=int(repair.sum()),repair_fraction=float(repair.mean()),label='forced full24 repair stress, separate from captured gates')
        cases.append(stress)
        for name in FILES:result['inputs'][str((folder/name).resolve())]=digest(folder/name)
    result['cases']=cases
    result['interpretation']='Identical resources/program; only three map-valid flags differ between paired arms. Repair stress excluded from captured gates.'
    validate_shadow_manifest(result)

def verify_prepared(data):
    manifest=json.loads((data/'manifest.json').read_text())
    if manifest['checkpoint']!='actual-production-whole-transaction-performance' or manifest.get('borrowed_scene_open') is not True:raise ValueError('production timing contract')
    if digest(data/'cases.txt')!=manifest['cases_sha256'] or (data/'cases.txt').read_text()!=case_text(manifest):raise ValueError('changed timing case routing')
    if manifest.get('paired_shadows'):
        validate_shadow_manifest(manifest)
        for frame,entry in manifest['shadow_maps'].items():
            if Path(entry['metadata']).read_text()!=shadow_text(frame,entry['maps']):raise ValueError('changed map metadata routing')
        for c in manifest['cases']:
            if not c['repair_stress']:continue
            original=next(o for o in manifest['cases'] if o['profile']==c['profile'] and o['frame']==c['frame'] and not o['repair_stress'])
            shape=(c['height'],c['width'],4)
            count=verify_repair_depth(np.fromfile(Path(c['directory'])/'depth.rgba32f','<f4').reshape(shape),np.fromfile(Path(original['directory'])/'depth.rgba32f','<f4').reshape(shape))
            if count!=c['repair_pixels']:raise ValueError('repair count changed')
    for path,h in manifest['inputs'].items():
        if digest(path)!=h:raise ValueError('changed timing dependency: '+path)
    return manifest

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--workload-manifest',type=Path,required=True);ap.add_argument('--accepted-report',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);ap.add_argument('--shadow-dump',type=Path);a=ap.parse_args()
    result=prepare(a.workload_manifest,a.accepted_report,a.output,a.shadow_dump);print(json.dumps(dict(cases=len(result['cases']),manifest=str(a.output/'manifest.json'),sha256=digest(a.output/'manifest.json'))))
