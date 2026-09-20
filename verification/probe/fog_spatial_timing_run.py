#!/usr/bin/env python3
"""Root-owned measured execution; invoke through wine_lock.py in bottle X3."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import numpy as np
import bottle
from fog_spatial_build import ROOT,digest
from fog_spatial_timing_prepare import verify_prepared
from fog_spatial_timing_build import FLAGS,unchanged

def distribution(values):
    values=np.asarray(values,dtype=np.float64)
    if not len(values) or not np.isfinite(values).all() or np.any(values<0):raise ValueError('invalid timing distribution')
    return dict(median=float(np.percentile(values,50,method='linear')),p95=float(np.percentile(values,95,method='linear')),min=float(values.min()),max=float(values.max()))


def metrics(rows,frequency):
    submit=[(row['submitted']-row['start'])*1000/frequency for row in rows]
    fenced=[(row['completed']-row['start'])*1000/frequency for row in rows]
    residual=[(row['completed']-row['submitted'])*1000/frequency for row in rows]
    return dict(count=len(rows),cpu_submit_ms=distribution(submit),event_fenced_ms=distribution(fenced),fenced_minus_submit_estimate_ms=distribution(residual))


def analyze(text,manifest):
    if [(p['width'],p['height'],p['name']) for p in manifest['profiles']]!=[(1280,768,'1280x768'),(1920,1080,'1920x1080')]:raise ValueError('fixed profile dimensions')
    if len(manifest['cases'])!=8 or len({c['name'] for c in manifest['cases']})!=8 or any(sum(c['profile']==p['name'] for c in manifest['cases'])!=4 for p in manifest['profiles']):raise ValueError('fixed four-view profiles')
    if re.search(r'^(?:FAIL|RESULT FAIL|CHECK .* FAIL)',text,re.M):raise ValueError('fixture failure')
    if text.splitlines()[-1:]!=['RESULT checkpoint=timing profiles=2 warm_per_profile=16 samples_per_profile=64 PASS']:raise ValueError('incomplete timing fixture')
    clocks=re.findall(r'^TIMING_CLOCK frequency=(\d+) timestamps=not_collected_no_active_query_contract$',text,re.M)
    if len(clocks)!=1 or int(clocks[0])<=0:raise ValueError('clock contract')
    frequency=int(clocks[0]);expected_checks=set()
    for case in manifest['cases']:
        expected_checks.update((case['name']+'_baseline_state',case['name']+'_final_bytes'))
        if case['width']==1280:expected_checks.add(case['name']+'_accepted_bytes')
    for profile in manifest['profiles']:
        expected_checks.add(str(profile['width'])+'_stable_caller_refs')
        expected_checks.add(str(profile['width'])+'_all_samples_recorded')
        expected_checks.update(str(profile['width'])+'_'+family+'_stable_owned' for family in {c['family'] for c in manifest['cases'] if c['profile']==profile['name']})
    checks=re.findall(r'^CHECK (\S+) PASS$',text,re.M)
    if len(checks)!=len(set(checks)) or set(checks)!=expected_checks:raise ValueError('timing correctness/lifetime check set')
    prep_rows=re.findall(r'^TIMING_PREP width=(\d+) height=(\d+) ticks=(\d+) families=(\d+) atlas_bytes=(\d+) input_bytes=(\d+) pass_target_bytes=(\d+) witness_bytes=(\d+) streams=(\d+) counted_calls_exclude=resource_validation_and_COM_Releases$',text,re.M)
    if len(prep_rows)!=2 or sum(line.startswith('TIMING_PREP ') for line in text.splitlines())!=2:raise ValueError('preparation records')
    preparations=[]
    for raw,profile in zip(prep_rows,manifest['profiles']):
        w,h,ticks,families,atlas_bytes,input_bytes,targets,witness_bytes,streams=map(int,raw)
        if (w,h)!=(profile['width'],profile['height']) or ticks<=0 or not 1<=streams<=16 or families!=2 or atlas_bytes!=17846400*families or targets!=families*(8*w*h+8*((w+1)//2)*((h+1)//2)) or input_bytes!=4*w*h*32+2*w*h+128 or witness_bytes!=4*8*((w+1)//2)*((h+1)//2):raise ValueError('preparation dimensions/memory')
        preparations.append(dict(width=w,height=h,resident_setup_event_completed_ms=ticks*1000/frequency,families=families,atlas_bytes=atlas_bytes,input_and_caller_bytes=input_bytes,pass_target_bytes=targets,readback_witness_bytes=witness_bytes,streams=streams))
    field_rows=re.findall(r'^TIMING_FIELD width=(\d+) height=(\d+) family=(\S+) field_ticks=(\d+) target_ticks=(\d+) cpu_atlas_bytes=(\d+)$',text,re.M)
    fields=[];expected_fields=[(p['width'],p['height'],family) for p in manifest['profiles'] for family in ('bluewell','foggreenoutlands')]
    if len(field_rows)!=4 or sum(line.startswith('TIMING_FIELD ') for line in text.splitlines())!=4:raise ValueError('field preparation records')
    for raw,expected in zip(field_rows,expected_fields):
        w,h,family,field_ticks,target_ticks,cpu_bytes=raw
        if (int(w),int(h),family)!=expected or int(field_ticks)<=0 or int(target_ticks)<=0 or int(cpu_bytes)!=17846400:raise ValueError('field preparation contract')
        fields.append(dict(width=int(w),height=int(h),family=family,field_decode_allocate_upload_submit_ms=int(field_ticks)*1000/frequency,target_prepare_submit_ms=int(target_ticks)*1000/frequency,cpu_atlas_bytes=int(cpu_bytes)))
    pattern=r'^TIMING_SAMPLE width=(\d+) height=(\d+) phase=(warm|measured) index=(\d+) view=(\S+) start=(\d+) submitted=(\d+) completed=(\d+) hr=([0-9a-f]{8}) restore=([0-9a-f]{8}) fence=([0-9a-f]{8}) valid=([01]) counted_calls=(\d+)$'
    raw_rows=re.findall(pattern,text,re.M)
    if len(raw_rows)!=160 or sum(line.startswith('TIMING_SAMPLE ') for line in text.splitlines())!=160:raise ValueError('requires exactly 160 warm/measured samples')
    samples=[];previous_end=0
    for ordinal,raw in enumerate(raw_rows):
        w,h,phase,index,view,start,submitted,completed,hr,restore,fence,valid,calls=raw
        profile=manifest['profiles'][ordinal//80];iteration=ordinal%80
        names=[c['name'] for c in manifest['cases'] if c['profile']==profile['name']]
        row=dict(width=int(w),height=int(h),phase=phase,index=int(index),view=view,start=int(start),submitted=int(submitted),completed=int(completed),hr=hr,restore=restore,fence=fence,valid=valid=='1',counted_calls=int(calls))
        if (row['width'],row['height'])!=(profile['width'],profile['height']) or view!=names[iteration%4] or phase!=('warm' if iteration<16 else 'measured') or row['index']!=(iteration if iteration<16 else iteration-16):raise ValueError('sample order/balance')
        if hr!=restore or restore!=fence or fence!='00000000' or not row['valid'] or row['counted_calls']<=0:raise ValueError('failed/unfenced transaction')
        if not previous_end<row['start']<row['submitted']<=row['completed']:raise ValueError('nonmonotone sample clocks')
        previous_end=row['completed'];samples.append(row)
    if len({row['counted_calls'] for row in samples})!=1:raise ValueError('transaction call count changed')
    profiles=[]
    for profile in manifest['profiles']:
        rows=[s for s in samples if s['width']==profile['width'] and s['phase']=='measured']
        stats=metrics(rows,frequency);fenced=stats['event_fenced_ms'];submit=stats['cpu_submit_ms']
        gate=dict(cpu_submit_median_ms=.25,event_fenced_median_ms=1.25 if profile['width']==1280 else 2.,event_fenced_p95_ms=None if profile['width']==1280 else 2.5)
        passed=submit['median']<=gate['cpu_submit_median_ms'] and fenced['median']<=gate['event_fenced_median_ms'] and (gate['event_fenced_p95_ms'] is None or fenced['p95']<=gate['event_fenced_p95_ms'])
        profiles.append(dict(profile=profile['name'],passed=bool(passed),gates=gate,**stats,views={view:metrics([r for r in rows if r['view']==view],frequency) for view in dict.fromkeys(r['view'] for r in rows)}))
    return dict(passed=all(p['passed'] for p in profiles),profiles=profiles,preparations=preparations,field_preparations=fields,samples=samples,frequency=frequency,percentile_method='linear',
                timestamps='not collected: preserve no-active-query caller contract; hardware support not assessed',
                counted_transaction_calls=samples[0]['counted_calls'],count_exclusions='resource-validation calls and COM Releases; all actual work included in clock interval',checks=checks)


def signed_distribution(values):
    a=np.asarray(values,np.float64)
    if not a.size or not np.isfinite(a).all():raise ValueError('invalid paired differences')
    return dict(median=float(np.percentile(a,50,method='linear')),p95=float(np.percentile(a,95,method='linear')),min=float(a.min()),max=float(a.max()))

def pair_metrics(rows,frequency):
    arms={arm:metrics([r for r in rows if r['arm']==arm],frequency) for arm in (0,1)}
    pairs={}
    for row in rows:
        key=(row['view'],row['index']);entry=pairs.setdefault(key,{})
        if row['arm'] in entry:raise ValueError('duplicate paired arm')
        entry[row['arm']]=row
    cpu=[];completed=[]
    for pair in pairs.values():
        if set(pair)!={0,1}:raise ValueError('missing paired arm')
        a,b=pair[0],pair[1]
        cpu.append(((b['submitted']-b['start'])-(a['submitted']-a['start']))*1000/frequency)
        completed.append(((b['completed']-b['start'])-(a['completed']-a['start']))*1000/frequency)
    return dict(pairs=len(pairs),off=arms[0],on=arms[1],paired_delta_ms=dict(cpu_submit=signed_distribution(cpu),event_fenced=signed_distribution(completed)))

def paired_checks(manifest):
    expected=set()
    for c in manifest['cases']:
        expected.add(c['name']+'_paired_T_empty')
        for arm in (0,1):
            expected.update(c['name']+'_'+str(arm)+suffix for suffix in ('_baseline_state','_final_bytes'))
        if c['width']==1280:expected.add(c['name']+'_0_accepted_bytes')
    for p in manifest['profiles']:
        w=str(p['width']);expected.update((w+'_paired_samples',w+'_paired_refs'))
        expected.update(w+'_'+family+'_paired_owned' for family in ('bluewell','foggreenoutlands'))
    return expected

def analyze_pairs(text,manifest):
    from fog_spatial_timing_prepare import validate_shadow_manifest
    validate_shadow_manifest(manifest)
    if re.search(r'^(?:FAIL|RESULT FAIL|CHECK .* FAIL|STATE_DIFF|SURFACE_DIFF)',text,re.M):raise ValueError('paired fixture failure')
    if text.splitlines()[-1:]!=['RESULT checkpoint=shadow_pairs profiles=2 common_transactions=320 repair_transactions=80 PASS']:raise ValueError('incomplete paired fixture')
    clocks=re.findall(r'^PAIR_CLOCK frequency=(\d+) timestamps=not_collected_no_active_query_contract$',text,re.M)
    if len(clocks)!=1 or int(clocks[0])<=0:raise ValueError('paired clock')
    frequency=int(clocks[0]);checks=re.findall(r'^CHECK (\S+) PASS$',text,re.M)
    if len(checks)!=len(set(checks)) or set(checks)!=paired_checks(manifest):raise ValueError('paired correctness/lifetime checks')
    def records(prefix,keys):
        out=[]
        for line in text.splitlines():
            if not line.startswith(prefix+' '):continue
            tokens=line.split()[1:]
            if any('=' not in v for v in tokens):raise ValueError('malformed '+prefix)
            row=dict(v.split('=',1) for v in tokens)
            if len(row)!=len(tokens) or set(row)!=set(keys.split()):raise ValueError('fields '+prefix)
            out.append(row)
        return out
    maps=records('PAIR_MAP_PREP','submit_ticks completed_ticks sets bytes')
    map_bytes=sum(c['size']**2*4 for e in manifest['shadow_maps'].values() for c in e['maps'])
    if len(maps)!=1:raise ValueError('map preparation count')
    m={k:int(v) for k,v in maps[0].items()}
    if not 0<m['submit_ticks']<=m['completed_ticks'] or m['sets']!=4 or m['bytes']!=map_bytes:raise ValueError('map preparation extent')
    prep=records('PAIR_PREP','width height ticks atlas_bytes input_bytes target_bytes witness_bytes')
    if len(prep)!=2:raise ValueError('paired preparation count')
    preparations=[]
    for raw,profile in zip(prep,manifest['profiles']):
        r={k:int(v) for k,v in raw.items()};w,h=profile['width'],profile['height'];half=8*((w+1)//2)*((h+1)//2)
        if (r['width'],r['height'])!=(w,h) or r['ticks']<=0 or r['atlas_bytes']!=35692800 or r['input_bytes']!=5*w*h*32+2*w*h+128 or r['target_bytes']!=2*(8*w*h+half) or r['witness_bytes']!=10*(8*w*h+half):raise ValueError('paired preparation memory/dimensions')
        preparations.append(dict(**r,event_completed_ms=r['ticks']*1000/frequency))
    fields=records('PAIR_FIELD','width family field_ticks target_ticks cpu_atlas_bytes')
    if [(int(f['width']),f['family']) for f in fields]!=[(p['width'],family) for p in manifest['profiles'] for family in ('bluewell','foggreenoutlands')]:raise ValueError('paired field count/order')
    for r in fields:
        if min(int(r['field_ticks']),int(r['target_ticks']))<=0 or int(r['cpu_atlas_bytes'])!=17846400:raise ValueError('paired field prep')
    raw=records('PAIR_SAMPLE','width height phase index pair order arm view stress start submitted completed hr restore fence valid maps calls')
    if len(raw)!=400:raise ValueError('requires400 paired transactions')
    samples=[];last=0
    for ordinal,r in enumerate(raw):
        for key in ('width','height','index','pair','order','arm','stress','start','submitted','completed','valid','maps','calls'):r[key]=int(r[key])
        profile=manifest['profiles'][ordinal//200];iteration=ordinal%200;pair=iteration//2;round_index=pair//5;order=iteration%2
        case=[c for c in manifest['cases'] if c['profile']==profile['name']][pair%5]
        if (r['width'],r['height'],r['pair'],r['order'],r['arm'],r['view'],r['stress'])!=(profile['width'],profile['height'],pair,order,(round_index&1)^order,case['name'],int(case['repair_stress'])):raise ValueError('paired ordering/arm/workload')
        if r['phase']!=('warm' if round_index<4 else 'measured') or r['index']!=(round_index if round_index<4 else round_index-4):raise ValueError('paired warm/sample split')
        if r['hr']!=r['restore'] or r['restore']!=r['fence'] or r['fence']!='00000000' or r['valid']!=1 or r['maps']!=(3 if r['arm'] else 0) or r['calls']<=0:raise ValueError('paired transaction failure')
        if not last<r['start']<r['submitted']<=r['completed']:raise ValueError('paired clocks')
        last=r['completed'];samples.append(r)
    if len({r['calls'] for r in samples})!=1:raise ValueError('paired call count drift')
    profiles=[]
    for p in manifest['profiles']:
        selected=[r for r in samples if r['width']==p['width'] and r['phase']=='measured'];ordinary=[r for r in selected if not r['stress']];stress=[r for r in selected if r['stress']]
        stats=pair_metrics(ordinary,frequency);gate=dict(cpu_submit_median_ms=.25,event_fenced_median_ms=1.25 if p['width']==1280 else 2.,event_fenced_p95_ms=None if p['width']==1280 else 2.5)
        passed=all(stats[a]['cpu_submit_ms']['median']<=gate['cpu_submit_median_ms'] and stats[a]['event_fenced_ms']['median']<=gate['event_fenced_median_ms'] and (gate['event_fenced_p95_ms'] is None or stats[a]['event_fenced_ms']['p95']<=gate['event_fenced_p95_ms']) for a in ('off','on'))
        profiles.append(dict(profile=p['name'],passed=bool(passed),gates=gate,common=stats,repair_stress=pair_metrics(stress,frequency),views={c['name']:pair_metrics([r for r in selected if r['view']==c['name']],frequency) for c in manifest['cases'] if c['profile']==p['name']},families={family:pair_metrics([r for r in ordinary if next(c['family'] for c in manifest['cases'] if c['name']==r['view'])==family],frequency) for family in ('bluewell','foggreenoutlands')}))
    return dict(passed=all(p['passed'] for p in profiles),profiles=profiles,samples=samples,frequency=frequency,checks=checks,map_preparation=dict(**m,interpretation='Fixture-only file read, validation, allocation and upload; not incremental production residency',submit_ms=m['submit_ticks']*1000/frequency,event_completed_ms=m['completed_ticks']*1000/frequency),preparations=preparations,field_preparations=[dict(**r,field_submit_ms=int(r['field_ticks'])*1000/frequency,target_submit_ms=int(r['target_ticks'])*1000/frequency) for r in fields],timestamps='not collected; EVENT-completed wall time only',readback_interpretation='Captured-map B readbacks establish stability and invariants; no new captured-map GPU-versus-CPU pixel oracle',counted_transaction_calls=samples[0]['calls'])

def verify_build(build_path):
    record=json.loads(build_path.read_text());exe=build_path.parent/'fog_spatial_timing.exe'
    if record.get('command',[])[:len(FLAGS)+1]!=['i686-w64-mingw32-g++',*FLAGS]:raise ValueError('timing ABI/compiler flags changed')
    if record['kind']!='actual-production-timing' or digest(exe)!=record['executable_sha256']:raise ValueError('frozen timing executable changed')
    for path,h in record['inputs'].items():
        if digest(path)!=h:raise ValueError('timing build source changed: '+path)
    return record,exe

def verify_execution(execution,binding):
    for key,value in binding.items():
        if execution.get(key)!=value:raise ValueError('timing execution binding mismatch: '+key)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--build',type=Path,required=True);ap.add_argument('--data',type=Path,required=True);ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--analyze-existing',action='store_true');a=ap.parse_args();build_path=a.build.resolve()/'build.json';data=a.data.resolve();out=a.output.resolve()
    checker_sources={str(Path(__file__).resolve()):digest(__file__),str(ROOT/'verification/probe/fog_spatial_timing_prepare.py'):digest(ROOT/'verification/probe/fog_spatial_timing_prepare.py'),str(ROOT/'verification/probe/fog_spatial_timing_build.py'):digest(ROOT/'verification/probe/fog_spatial_timing_build.py')}
    manifest=verify_prepared(data);build,exe=verify_build(build_path)
    if manifest.get('paired_shadows') and 'shadow-pairs' not in build.get('modes',[]):raise ValueError('timing executable lacks paired mode')
    binding=dict(executable_sha256=digest(exe),build_sha256=digest(build_path),manifest_sha256=digest(data/'manifest.json'),cases_sha256=digest(data/'cases.txt'))
    if not a.analyze_existing:
        if os.environ.get('X3M_FIXTURE_BOTTLE')!='X3':raise ValueError('X3M_FIXTURE_BOTTLE=X3 required; use wine_lock.py')
        if out.exists():raise ValueError('new timing output directory required')
        out.mkdir(parents=True)
        command=[bottle.WINE,*bottle.wine_args(),str(exe), 'Z:'+str(data/'cases.txt').replace('/','\\'),'Z:'+str(out).replace('/','\\')]
        if manifest.get('paired_shadows'):command.append('--shadow-pairs')
        start=time.monotonic()
        with (out/'fixture.txt').open('w') as stdout,(out/'wine.log').open('w') as stderr:
            result=subprocess.run(command,stdout=stdout,stderr=stderr,timeout=240,env=dict(os.environ,WINEDLLOVERRIDES='d3d9=b'))
        execution=dict(**binding,command=command,returncode=result.returncode,seconds=time.monotonic()-start,bottle=bottle.describe(),runner_sources=checker_sources)
        (out/'execution.json').write_text(json.dumps(execution,indent=2)+'\n')
    execution=json.loads((out/'execution.json').read_text());verify_execution(execution,binding)
    verify_prepared(data);verify_build(build_path)
    report=dict(checkpoint='actual-production-borrowed-scene-performance',passed=False,execution=execution,inputs=manifest,**binding,
                checker_sources=checker_sources,fixture_log_sha256=digest(out/'fixture.txt'),native_windows='unverified',quality_acceptance=False,
                interpretation='EVENT-completed whole-transaction wall time, including polling; CPU submit is wall time. No game FPS or GPU timestamp claim.')
    if execution['returncode']==0:
        try:
            paired=manifest.get('paired_shadows',False)
            report['timing']=(analyze_pairs if paired else analyze)((out/'fixture.txt').read_text(),manifest)
            expected=set()
            for case in manifest['cases']:
                st_arms={}
                for arm in ((0,1) if paired else (0,)):
                    name=case['name']+('_'+str(arm) if paired else '')
                    for kind in ('st','composite'):
                        before=out/f'{name}.baseline.{kind}.rgba16f';after=out/f'{name}.final.{kind}.rgba16f'
                        expected.update((before.name,after.name))
                        if before.read_bytes()!=after.read_bytes():raise ValueError('baseline/final readback mismatch')
                        if not arm and case['expected'][kind] and digest(before)!=digest(case['expected'][kind]):raise ValueError('accepted production readback mismatch')
                        shape=((case['height']+1)//2,(case['width']+1)//2,4) if kind=='st' else (case['height'],case['width'],4)
                        values=np.fromfile(before,'<f2').reshape(shape)
                        if not np.isfinite(values).all():raise ValueError('nonfinite timing output')
                        if kind=='st':
                            if np.any((values[...,3]<0)|(values[...,3]>1)):raise ValueError('timing T range')
                            st_arms[arm]=values
                        else:
                            original=np.fromfile(Path(case['directory'])/'scene.rgba16f','<f2').reshape(shape)
                            if not np.array_equal(values[...,3].view('<u2'),original[...,3].view('<u2')):raise ValueError('timing alpha identity')
                if paired:
                    off,on=st_arms[0],st_arms[1];empty=np.all(off[...,:3]==0,axis=-1)&(off[...,3]==1)
                    if not np.array_equal(off[...,3].view('<u2'),on[...,3].view('<u2')) or not np.array_equal(off[empty].view('<u2'),on[empty].view('<u2')):raise ValueError('paired T or empty identity')
            actual={path.name for path in out.glob('*.rgba16f')}
            if actual!=expected:raise ValueError('timing readback set')
            report['readback_sha256']={name:digest(out/name) for name in sorted(expected)};report['passed']=report['timing']['passed']
        except (ValueError,OSError) as error:report['analysis_error']=str(error)
    unchanged(checker_sources)
    target=out/('report-reanalysis.json' if a.analyze_existing else 'report.json')
    if target.exists():raise ValueError('refuse report overwrite')
    target.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(dict(passed=report['passed'],report=str(target),profiles=report.get('timing',{}).get('profiles',[])),indent=2));return 0 if report['passed'] else 1
if __name__=='__main__':sys.exit(main())
