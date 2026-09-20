#!/usr/bin/env python3
"""Bounded all-family oracle/runner. Only the root may invoke --run-existing under wine_lock."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import time
import numpy as np
import fog_spatial_reference as reference

ROOT=Path(__file__).resolve().parents[2]
ORDER=list(range(1,15))+[1,2,14,14]
SCOPE='FAMILY_SCOPE profiles=14 switches=17 executes=18 real_reset=1 view=64x48 shafts=unshadowed'
REQUIRED={'family_'+str(i)+'_'+name for i in range(17) for name in ('cpu_prepare','switch','one_atlas','warm_reuse','cpu_execute','transaction','nonempty_alpha')}
REQUIRED.update('family_'+str(i)+'_stale_refused' for i in range(1,17))
REQUIRED.update('family_'+str(i)+'_revisit_exact' for i in range(14,17))
REQUIRED.update(('family_invalid_id_disarmed','family_invalid_rearm_cached','family_reset_retains_one_cpu','family_real_reset','family_reset_cached_upload','family_reset_exact_pixels_state','family_detach_empty'))
F=np.float32

def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def windows(path):return 'Z:'+str(Path(path).resolve()).replace('/','\\')
def metrics(values):
    values=np.asarray(values)
    return {'p99':float(np.percentile(values,99)) if values.size else 0.,'max':float(values.max()) if values.size else 0.}

def sample(atlas,points):
    """Independent wrapped trilinear sampler of the half atlas's interior cells."""
    u=(np.asarray(points,F)/F(32768)%F(1))*F(128)-F(.5)
    base=np.floor(u).astype(np.int32);frac=u-base.astype(F)
    result=np.zeros(u.shape[:-1]+(4,),F)
    for dz in (0,1):
        z=(base[...,2]+dz)%128
        for dy in (0,1):
            y=(base[...,1]+dy)%128
            for dx in (0,1):
                x=(base[...,0]+dx)%128
                weight=(frac[...,0] if dx else F(1)-frac[...,0])*(frac[...,1] if dy else F(1)-frac[...,1])*(frac[...,2] if dz else F(1)-frac[...,2])
                result+=atlas[(z//12)*130+y+1,(z%12)*130+x+1].astype(F)*weight[...,None]
    return result

class Sampler:
    sample=staticmethod(sample)
    metrics=staticmethod(metrics)

def decode(path,row):
    packet=Path(path).read_bytes()
    if hashlib.sha256(packet).hexdigest()!=row['resource_sha256']:raise ValueError('packet hash')
    header=struct.unpack_from('<8sIIIIIIIIIQI',packet)
    magic,version,size,profile,recipe,w,h,texel,decoded,runs,checksum,reserved=header
    if (magic,version,size,profile,recipe,w,h,texel,decoded,reserved)!=(b'X3FOGPK\0',1,56,row['profile_id'],1,1560,1430,8,17846400,0):raise ValueError('packet metadata')
    if checksum!=int(row['decoded_fnv1a'],16):raise ValueError('declared checksum')
    data=bytearray(decoded);cursor=56;offset=0
    for _ in range(runs):
        if cursor+4>len(packet):raise ValueError('truncated run')
        word=struct.unpack_from('<I',packet,cursor)[0];cursor+=4;count=word&0x7fffffff
        if not count or offset+count*8>decoded:raise ValueError('run bounds')
        if word&0x80000000:
            end=cursor+count*8
            if end>len(packet):raise ValueError('literal bounds')
            data[offset:offset+count*8]=packet[cursor:end];cursor=end
        offset+=count*8
    if cursor!=len(packet) or offset!=decoded or hashlib.sha256(data).hexdigest()!=row['decoded_sha256']:raise ValueError('decoded content')
    atlas=np.frombuffer(data,'<f2').reshape(1430,1560,4)
    if not np.isfinite(atlas).all() or np.any(atlas<0):raise ValueError('decoded half')
    return atlas

def fixed_view():
    c=np.zeros((8,4),'<f4');c[0]=[1,1,0,0];c[1]=[64,48,32,24]
    c[2]=[0,0,0,2.5e-6];c[3]=[0,0,1,12000]
    c[4,0]=c[5,1]=c[6,2]=1
    depth=np.zeros((48,64,4),'<f4');depth[...,0]=2
    scene=np.empty((48,64,4),'<f2');scene[:]=[.08,.12,.16,.375]
    return c,depth,scene

def prepare(assets,out):
    if out.exists():raise ValueError('new output directory required')
    manifest=json.loads((assets/'manifest.json').read_text());rows=manifest['profiles']
    if [r['profile_id'] for r in rows]!=list(range(1,15)):raise ValueError('all 14 IDs in order required')
    out.mkdir(parents=True);c,depth,scene=fixed_view()
    for name,array in (('constants.f32',c),('depth.rgba32f',depth),('scene.rgba16f',scene)):array.tofile(out/name)
    (out/'cases.txt').write_text('fixed-family-view\n'+windows(out)+'\n1\n')
    y,x=np.mgrid[:48:2,:64:2];direction,limit,_=reference.rays(depth[::2,::2],c,x,y)
    references=[];bindings={str((assets/'manifest.json').resolve()):digest(assets/'manifest.json')}
    for row in rows:
        path=assets/(row['name']+'.fogbin');bindings[str(path.resolve())]=digest(path)
        atlas=decode(path,row);constants=c.copy();constants[2,3]=row['base_sigma']
        st=reference.march(Sampler,atlas,constants,direction,limit).astype('<f2')
        comp,_,_,_=reference.composite(Sampler,atlas,constants,depth,scene,st)
        nonempty=int(np.any(st!=np.array([0,0,0,1],'<f2'),axis=-1).sum())
        changed=int(np.any(comp[...,:3]!=scene[...,:3],axis=-1).sum())
        if nonempty<64 or changed<64:raise ValueError('fixed view has insufficient density: '+row['name'])
        for suffix,array in (('st',st),('composite',comp)):array.tofile(out/f"reference-{row['profile_id']}.{suffix}.rgba16f")
        references.append(dict(profile=row['profile_id'],family=row['name'],nonempty=nonempty,changed=changed))
        del atlas
    for name in ('fog_family_gpu.py','fog_family_gpu_cases_inc.h','fog_spatial_reference.py'):
        path=ROOT/'verification/probe'/name;bindings[str(path)]=digest(path)
    for path in out.iterdir():bindings[str(path.resolve())]=digest(path)
    record=dict(schema=1,order=ORDER,view='fixed origin0, identity basis,64x48 sky,horizon12000,unit density',references=references,inputs=bindings)
    (out/'inputs.json').write_text(json.dumps(record,indent=2)+'\n')
    return record

def validate_log(text):
    checks=re.findall(r'^CHECK (\S+) PASS$',text,re.M)
    if set(checks)!=REQUIRED or len(checks)!=len(REQUIRED):raise ValueError('incomplete or duplicate checks')
    if re.findall(r'^RESULT .*$',text,re.M)!=[f'RESULT production_fog checks={len(REQUIRED)} PASS']:raise ValueError('terminal')
    if text.splitlines().count(SCOPE)!=1 or re.search(r'^(FAIL|GAP|STATE_DIFF|SURFACE_DIFF|PROTECTED_DIFF)',text,re.M):raise ValueError('failure or scope')
    rows=re.findall(r'^FAMILY_GPU run=(\d+) profile=(\d+) generation=(\d+) nonempty=(\d+) changed=(\d+) cpu_bytes=(\d+) refs=(\d+)$',text,re.M)
    if len(rows)!=17:raise ValueError('family observations')
    generation=0
    for i,row in enumerate(rows):
        run,profile,current,nonempty,changed,bytes_,refs=map(int,row)
        if run!=i or profile!=ORDER[i] or current<=generation or nonempty<64 or changed<64 or bytes_!=17846400 or refs!=10:raise ValueError('family observation mismatch')
        generation=current
    return len(checks)

def verify_inputs(record):
    if record['order']!=ORDER:raise ValueError('case order')
    for path,expected in record['inputs'].items():
        if digest(path)!=expected:raise ValueError('changed input: '+path)

def analyze(record,out):
    _,depth,scene=fixed_view();results=[]
    for run,profile in enumerate(ORDER):
        st=np.fromfile(out/f'family-{run}.st.rgba16f','<f2').reshape(24,32,4)
        comp=np.fromfile(out/f'family-{run}.composite.rgba16f','<f2').reshape(48,64,4)
        expected_st=np.fromfile(out/f'reference-{profile}.st.rgba16f','<f2').reshape(st.shape)
        expected_comp=np.fromfile(out/f'reference-{profile}.composite.rgba16f','<f2').reshape(comp.shape)
        sg=reference.numeric_groups(Sampler,st,expected_st,depth[::2,::2])
        cg=reference.numeric_groups(Sampler,comp,expected_comp,depth,np.zeros((48,64),bool))
        nonempty=int(np.any(st!=np.array([0,0,0,1],'<f2'),axis=-1).sum());changed=int(np.any(comp[...,:3]!=scene[...,:3],axis=-1).sum())
        finite=bool(np.isfinite(st).all() and np.isfinite(comp).all() and ((st[...,3]>=0)&(st[...,3]<=1)).all())
        alpha=bool(np.array_equal(comp[...,3],scene[...,3]))
        passed=finite and alpha and nonempty>=64 and changed>=64 and all(g['passed'] for groups in (sg,cg) for g in groups.values())
        results.append(dict(run=run,profile=profile,passed=bool(passed),finite=finite,alpha_exact=alpha,nonempty=nonempty,changed=changed,ST=sg,composite=cg))
    exact_revisits=all((out/f'family-{run}.{kind}.rgba16f').read_bytes()==(out/f'family-{original}.{kind}.rgba16f').read_bytes()
                      for run,original in ((14,0),(15,1),(16,13),(17,13)) for kind in ('st','composite'))
    return dict(passed=all(row['passed'] for row in results) and exact_revisits,exact_revisits=exact_revisits,cases=results)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--assets',type=Path);ap.add_argument('--build',type=Path);ap.add_argument('--output',type=Path,required=True)
    group=ap.add_mutually_exclusive_group(required=True);group.add_argument('--prepare-only',action='store_true');group.add_argument('--run-existing',action='store_true');group.add_argument('--analyze-existing',action='store_true');args=ap.parse_args();out=args.output.resolve()
    if args.prepare_only:
        if args.assets is None:ap.error('--assets required')
        print(json.dumps(prepare(args.assets.resolve(),out)['references']));return
    if args.build is None:ap.error('--build required')
    record=json.loads((out/'inputs.json').read_text());verify_inputs(record)
    build_path=args.build/'build.json';build=json.loads(build_path.read_text());exe=args.build/'fog_spatial_fixture.exe'
    if digest(exe)!=build['executable_sha256']:raise ValueError('changed executable')
    extension=str((ROOT/'verification/probe/fog_family_gpu_cases_inc.h').resolve())
    if extension not in build['inputs']:raise ValueError('family extension not bound in build')
    for path,expected in build['inputs'].items():
        if digest(path)!=expected:raise ValueError('changed build input')
    binding=dict(executable_sha256=digest(exe),inputs_sha256=digest(out/'inputs.json'),build_sha256=digest(build_path))
    if args.run_existing:
        import bottle
        if os.environ.get('X3M_FIXTURE_BOTTLE')!='X3':raise ValueError('root must use X3 wine_lock')
        if (out/'fixture.txt').exists():raise ValueError('immutable runtime evidence')
        command=[bottle.WINE,*bottle.wine_args(),str(exe),windows(out/'cases.txt'),windows(out),'--families']
        start=time.monotonic();run=subprocess.run(command,capture_output=True,timeout=60,env=dict(os.environ,WINEDLLOVERRIDES='d3d9=b'))
        (out/'fixture.txt').write_bytes(run.stdout);(out/'wine.log').write_bytes(run.stderr)
        (out/'execution.json').write_text(json.dumps(dict(command=command,returncode=run.returncode,seconds=time.monotonic()-start,bottle=bottle.describe(),**binding),indent=2)+'\n')
    execution=json.loads((out/'execution.json').read_text())
    if execution['returncode']!=0 or any(execution.get(k)!=v for k,v in binding.items()):raise ValueError('execution binding or exit')
    verify_inputs(record);checks=validate_log((out/'fixture.txt').read_text(errors='replace'));result=analyze(record,out)
    result.update(checks=checks,execution=execution,inputs_sha256=digest(out/'inputs.json'),limits=['synthetic fixed view, unshadowed family coverage','not game visual acceptance or native Windows evidence'])
    report=out/('report-reanalysis.json' if args.analyze_existing else 'report.json')
    if report.exists():raise ValueError('immutable report')
    report.write_text(json.dumps(result,indent=2)+'\n');print(report);raise SystemExit(0 if result['passed'] else 1)
if __name__=='__main__':main()
