#!/usr/bin/env python3
"""Prepare/check on host; --run-existing is exclusively root/Wine-lock owned."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import numpy as np
import bottle
import fog_spatial_reference as reference
from fog_spatial_build import digest
ROOT=Path(__file__).resolve().parents[2]
FRAMES={'1974':'bluewell','9204':'bluewell','21901':'foggreenoutlands','26447':'foggreenoutlands'}
STATE_REQUIRED = set("""cpu_attach cpu_prepare_field cpu_prepare_targets one_cpu_atlas_and_ten_refs
resource_lookup_failure_disarmed composite_actual_write open_closed_identical_pixels
warm_allocation_upload_refs_stable actual_query_zero_writes actual_recording_zero_writes
actual_msaa_refusal wrong_rt2_format_refusal sigma_zero_zero_calls failed_composite_no_false_rollback
cpu_execute_fault cpu_execute_success cpu_fault_result cpu_success_result resize_preserves_field
clear_disarms same_cached_field_no_reupload failed_family_transition_disarmed family_retry_one_buffer_upload
cpu_before_reset cpu_after_reset cpu_detach reset_releases_defaults_retains_cpu native_reset_blocker_failure
failed_reset_pending_zero_device_calls native_reset_retry_success reset_reupload_cached_bytes
reset_reupload_identical_pixels_state detach_releases_all""".split())
STATE_REQUIRED.update('caps_'+name for name in ('ps2','vs2','slots','conditional_npot','square','dimensions','min_linear','mag_linear','stretch','streams'))
STATE_REQUIRED.update('format_'+name for name in ('116_0','113_1','113_131072'))
STATE_REQUIRED.update('attach_partial_'+name for name in ('91_1','86_1','106_1','106_2'))
for group,points in [('field',('23_1','23_2','31_1')),('targets',('23_1','23_2','59_1'))]:
    STATE_REQUIRED.update(f'{group}_{stage}_{point}' for stage in ('partial','retry') for point in points)
STATE_REQUIRED.update(f'{scene}_{name}' for scene in ('open','closed') for name in ('success_scene_contract','scene_copy_draw_counts','hostile_all_state_aux_depth'))
STATE_REQUIRED.update('refusal_'+str(i) for i in range(7))
FAULTS=('close_failure','copy_failure_closed','copy_failure_open_recovered','reopen_once_recovered','reopen_twice_poison','march_failure','composite_before_write','composite_after_write','end_failure')
STATE_REQUIRED.update(FAULTS);STATE_REQUIRED.update(name+'_bindings_aux_restored' for name in FAULTS)
STATE_REQUIRED.update(name+'_zero_scene_writes' for name in FAULTS[:6])
STATE_REQUIRED.update('capture_partial_'+str(i) for i in (101,103));STATE_REQUIRED.update('restore_failure_'+str(i) for i in (100,102))
STATE_REQUIRED.update('injected_loss_'+name for name in ('capture','close','copy','reopen','recovery_after_error','march','composite_after_write','end_after_error','restore_after_error','stream_restore'))

def windows(path):return 'Z:'+str(Path(path).resolve()).replace('/','\\')
def helper(root):
    path=root/'verification/analysis/prepare_fog_volume_gpu.py'
    sys.path.insert(0,str(path.parent));spec=importlib.util.spec_from_file_location('qualified_fog_sampler',path);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module

def prepare(data,out,sequence=False):
    if (out/'inputs.json').exists():raise ValueError('immutable inputs already prepared')
    out.mkdir(parents=True,exist_ok=True);lines=[];inputs={};cases=[]
    frames=FRAMES
    sequence_manifest=None
    if sequence:
        manifest_path=data/'manifest.json';manifest=json.loads(manifest_path.read_text())
        ids=list(map(str,manifest['captured_frame_ids']));entries={str(c['frame']):c for c in manifest['cases']}
        if len(ids)!=32 or len(set(ids))!=32:raise ValueError('sequence must contain32 distinct captured frames')
        frames={frame:entries[frame]['family'] for frame in ids}
        if any(entries[frame].get('kind')!='captured-sequence' or frames[frame] not in ('bluewell','foggreenoutlands') for frame in ids):raise ValueError('sequence captured family contract')
        sequence_manifest=dict(path=str(manifest_path.resolve()),sha256=digest(manifest_path))
        inputs[str(manifest_path.resolve())]=digest(manifest_path)
    for frame,family in frames.items():
        source=data/frame;profile=1 if family=='bluewell' else 2
        c=np.fromfile(source/'constants.f32','<f4');depth=np.fromfile(source/'depth.rgba32f','<f4');scene=np.fromfile(source/'scene.rgba16f','<f2')
        if c.size!=32 or not np.isfinite(c).all():raise ValueError('constants')
        w,h=map(int,c[4:6]);sigma=np.float32(2.5e-6 if profile==1 else 6.25e-6)
        if c[11]!=sigma or c[28]!=0 or depth.size!=w*h*4 or scene.size!=depth.size or not np.isfinite(depth).all() or not np.isfinite(scene).all():raise ValueError('actual input contract')
        for name in ('constants.f32','depth.rgba32f','scene.rgba16f','reference.rgba16f','composite-reference.rgba16f'):
            path=source/name;inputs[str(path.resolve())]=digest(path)
        lines.extend([frame,windows(source),str(profile)]);cases.append(dict(frame=frame,family=family,width=w,height=h,source=str(source.resolve())))
    for family in set(frames.values()):inputs[str((data/f'{family}.atlas16f').resolve())]=digest(data/f'{family}.atlas16f')
    (out/'cases.txt').write_text('\n'.join(lines)+'\n');record=dict(cases=cases,inputs=inputs,data=str(data.resolve()),cases_sha256=digest(out/'cases.txt'),sequence_manifest=sequence_manifest)
    (out/'inputs.json').write_text(json.dumps(record,indent=2)+'\n');return record

def verify_inputs(record,cases_path):
    if digest(cases_path)!=record['cases_sha256']:raise ValueError('changed executable case routing')
    lines=[]
    for case in record['cases']:lines.extend([case['frame'],windows(case['source']),'1' if case['family']=='bluewell' else '2'])
    if cases_path.read_text()!='\n'.join(lines)+'\n':raise ValueError('case routing does not match prepared manifest')
    for path,expected in record['inputs'].items():
        if digest(path)!=expected:raise ValueError('changed immutable input: '+path)

def verify_execution(execution,build,inputs_path,cases_path,build_path):
    expected=dict(executable_sha256=build['executable_sha256'],inputs_sha256=digest(inputs_path),cases_sha256=digest(cases_path),build_sha256=digest(build_path))
    for key,value in expected.items():
        if execution.get(key)!=value:raise ValueError('execution provenance mismatch: '+key)

def controls(variant):return (0 if variant==3 else .9 if variant==4 else .3,1 if variant==2 else 2.2,(0,2,.5) if variant==5 else (1,1,1))
def altered_depth(depth,variant):
    result=depth.copy()
    if variant==6:
        result[...,0]=.5;result[...,2]=np.resize(np.array([0,-1,np.nan,np.inf],np.float32),result.shape[:2])
    if variant==7:result[::2,::2,0]=.5;result[::2,::2,2]=np.nan
    return result

def analyze(record,out,prototype):
    p=helper(prototype);volumes={};rows=[];all_pass=True;raw={}
    for case in record['cases']:
        frame=case['frame'];h,w=case['height'],case['width'];source=Path(case['source']);family=case['family']
        if family not in volumes:volumes[family]=p.volume_from_atlas(np.fromfile(Path(record['data'])/f'{family}.atlas16f','<f2').reshape(p.AH,p.AW,4))
        v=volumes[family];c=np.fromfile(source/'constants.f32','<f4').reshape(8,4);depth=np.fromfile(source/'depth.rgba32f','<f4').reshape(h,w,4);scene=np.fromfile(source/'scene.rgba16f','<f2').reshape(h,w,4)
        cache={}
        for variant in range(8):
            paths=[out/f'{frame}-v{variant}.{name}.rgba16f' for name in ('st','composite')]
            for path in paths:raw[path.name]=digest(path)
            st=np.fromfile(paths[0],'<f2').reshape((h+1)//2,(w+1)//2,4);gpu=np.fromfile(paths[1],'<f2').reshape(h,w,4)
            d=altered_depth(depth,variant);g,gamma,radiance=controls(variant);y,x=np.mgrid[:h:2,:w:2]
            if variant==1:half_ref,comp_ref,repair,empty=cache[0]
            else:
                direction,limit,_=reference.rays(d[::2,::2],c,x,y);half_ref=reference.march(p,v,c,direction,limit,g,radiance).astype('<f2')
                comp_ref,repair,empty,_=reference.composite(p,v,c,d,scene,half_ref,g,gamma,radiance)
                cache[variant]=half_ref,comp_ref,repair,empty
            _,_,_,actual_empty=reference.composite(p,v,c,d,scene,st,g,gamma,radiance)
            changed=np.any(gpu.view('<u2')!=scene.view('<u2'),axis=-1);repair_empty=repair&empty
            identity=bool(not np.any(changed&actual_empty) and not np.any(changed&repair_empty))
            alpha=bool(np.array_equal(gpu[...,3].view('<u2'),scene[...,3].view('<u2')))
            sg=reference.numeric_groups(p,st,half_ref,d[::2,::2]);cg=reference.numeric_groups(p,gpu,comp_ref,d,repair)
            finite=bool(np.isfinite(st).all() and np.isfinite(gpu).all() and ((st[...,3]>=0)&(st[...,3]<=1)).all())
            baseline=None
            if variant==0:
                original_st=np.fromfile(source/'reference.rgba16f','<f2').reshape(st.shape);original_comp=np.fromfile(source/'composite-reference.rgba16f','<f2').reshape(gpu.shape)
                baseline=dict(ST=reference.numeric_groups(p,st,original_st,d[::2,::2]),composite=reference.numeric_groups(p,gpu,original_comp,d,repair))
            open_equal=variant!=1 or (np.array_equal(st.view('<u2'),np.fromfile(out/f'{frame}-v0.st.rgba16f','<u2').reshape(st.shape)) and np.array_equal(gpu.view('<u2'),np.fromfile(out/f'{frame}-v0.composite.rgba16f','<u2').reshape(gpu.shape)))
            ok=finite and alpha and identity and open_equal and all(v['passed'] for v in [*sg.values(),*cg.values()])
            if baseline:ok&=all(v['passed'] for group in baseline.values() for v in group.values())
            all_pass &= ok
            rows.append(dict(frame=frame,variant=variant,passed=bool(ok),finite=finite,alpha_exact=alpha,identity_exact=identity,actual_half_empty=int(actual_empty.sum()),actual_half_empty_changed=int((changed&actual_empty).sum()),float32_empty_repair=int(repair_empty.sum()),float32_empty_repair_changed=int((changed&repair_empty).sum()),open_closed_exact=bool(open_equal),ST=sg,composite=cg,qualified_baseline=baseline))
            print(f'CHECKED {frame} variant={variant} pass={bool(ok)}',flush=True)
    return dict(passed=bool(all_pass),cases=rows,readback_hashes=raw,sampler_sha256=digest(prototype/'verification/analysis/prepare_fog_volume_gpu.py'),limitations=['GPU full-float repaired ST is not separately read back; CPU-float32-empty repair identity checked independently','Native Windows unverified; no game or TAA evidence'])

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--data',type=Path,required=True);ap.add_argument('--build',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);ap.add_argument('--prototype-root',type=Path,required=True)
    group=ap.add_mutually_exclusive_group(required=True);group.add_argument('--prepare-only',action='store_true');group.add_argument('--run-existing',action='store_true');group.add_argument('--analyze-existing',action='store_true');ap.add_argument('--state',action='store_true');ap.add_argument('--sequence',action='store_true');a=ap.parse_args()
    if a.prepare_only:prepare(a.data.resolve(),a.output.resolve(),a.sequence);return
    out=a.output.resolve();record=json.loads((out/'inputs.json').read_text());verify_inputs(record,out/'cases.txt')
    build=json.loads((a.build/'build.json').read_text());exe=a.build/'fog_spatial_fixture.exe'
    if digest(exe)!=build['executable_sha256']:raise ValueError('changed frozen executable')
    for path,h in build['inputs'].items():
        if digest(path)!=h:raise ValueError('changed build input: '+path)
    if a.run_existing:
        if os.environ.get('X3M_FIXTURE_BOTTLE')!='X3':raise ValueError('fixture requires X3 bottle')
        if (out/'fixture.txt').exists():raise ValueError('refuse overwrite of runtime evidence')
        cmd=[bottle.WINE,*bottle.wine_args(),str(exe),windows(out/'cases.txt'),windows(out)]
        if a.state:cmd.append('--state')
        binding=dict(executable_sha256=digest(exe),inputs_sha256=digest(out/'inputs.json'),cases_sha256=digest(out/'cases.txt'),build_sha256=digest(a.build/'build.json'))
        start=time.monotonic();run=subprocess.run(cmd,capture_output=True,timeout=180,env=dict(os.environ,WINEDLLOVERRIDES='d3d9=b'))
        (out/'fixture.txt').write_bytes(run.stdout);(out/'wine.log').write_bytes(run.stderr)
        (out/'execution.json').write_text(json.dumps(dict(command=cmd,returncode=run.returncode,seconds=time.monotonic()-start,bottle=bottle.describe(),**binding),indent=2)+'\n')
    verify_inputs(record,out/'cases.txt')
    log=(out/'fixture.txt').read_text(errors='replace');execution=json.loads((out/'execution.json').read_text())
    verify_execution(execution,build,out/'inputs.json',out/'cases.txt',a.build/'build.json')
    checks=re.findall(r'^CHECK (\S+) PASS$',log,re.M);complete=execution['returncode']==0 and bool(re.search(r'^RESULT production_fog checks='+str(len(checks))+r' PASS$',log,re.M)) and len(set(checks))==len(checks) and not re.search(r'^(?:FAIL|GAP|RESULT FAIL|STATE_DIFF|SURFACE_DIFF|PROTECTED_DIFF)',log,re.M)
    if a.state:
        complete &= set(checks)==STATE_REQUIRED
        complete &= 'LOSS_EVIDENCE injected_only=1 native_loss_observed=0' in log
        failed_reset=re.findall(r'^RESET failed_attempt_hr=([0-9a-f]{8})$',log,re.M)
        complete &= len(failed_reset)==1 and bool(int(failed_reset[0],16)&0x80000000)
        if set(checks)!=STATE_REQUIRED:print('Missing checks:',sorted(STATE_REQUIRED-set(checks)),'extra:',sorted(set(checks)-STATE_REQUIRED))
    else:
        expected={f'{frame}_{name}_{variant}' for frame in (c['frame'] for c in record['cases']) for name in ('transaction','alpha') for variant in range(8)}
        expected.update(f'{frame}_invalid_depth_exact_{name}' for frame in (c['frame'] for c in record['cases']) for name in ('identity','st'))
        complete &= set(checks)==expected
    result=dict(passed=bool(complete),checks=checks,execution=execution,inputs_sha256=digest(out/'inputs.json'),build_sha256=digest(a.build/'build.json'),checker_sources={str(Path(__file__).resolve()):digest(__file__),str(Path(reference.__file__).resolve()):digest(reference.__file__)})
    if complete and not a.state:result['numerical']=analyze(record,out,a.prototype_root.resolve());result['passed']&=result['numerical']['passed']
    if a.state:result['scope']='actual production pass: open/closed scene, hostile state, field/cache, real Reset blocker/retry and injected HRESULT loss; native loss not observed'
    report=out/('report-reanalysis.json' if a.analyze_existing else 'report.json')
    if report.exists():raise ValueError('refuse overwrite of report')
    report.write_text(json.dumps(result,indent=2)+'\n');print(report);raise SystemExit(0 if result['passed'] else 1)
if __name__=='__main__':main()
