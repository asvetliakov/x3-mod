#!/usr/bin/env python3
"""Fresh build and actual imported/shared-slot mesh cache matrix; never launches X3."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from game_guard import game_running  # noqa: E402
ROOT=Path(__file__).resolve().parents[2]
RESULTS=ROOT/'verification/results'
BUILD=ROOT/'verification/probe/build/mesh_cache_hook'
BOTTLE=Path.home()/'Library/Application Support/CrossOver/Bottles/Steam/drive_c'
NATIVE={
 'd3dx9_37.dll':BOTTLE/'X3/d3dx9_37.dll',
 'd3d9.dll':BOTTLE/'windows/syswow64/d3d9.dll',
 'wined3d.dll':BOTTLE/'windows/syswow64/wined3d.dll'}
SOURCES=['src/proxy/capture.h','src/proxy/cpu_state.h','src/proxy/loading_trace.h','src/proxy/loading_trace.cpp','src/proxy/mesh_adjacency_cache.h','src/proxy/mesh_adjacency_cache.cpp','src/ownership/d3d9_ownership.h','src/ownership/d3d9_ownership.cpp', 'src/ownership/application_admission.h', 'src/ownership/application_admission.cpp', 'src/ownership/application_admission_abi.h', 'src/ownership/application_admission_abi.cpp', 'src/ownership/execution_state.cpp', 'src/ownership/execution_state.h', 'src/ownership/finite_buffer_evidence.cpp', 'src/ownership/finite_buffer_evidence.h', 'src/ownership/portable_managed_upload.cpp', 'src/ownership/portable_managed_upload.h','src/ownership/d3d9_classes_inc.h','src/ownership/d3d9_forwarders_inc.h','verification/probe/loading_admission_witness.h','verification/probe/mesh_cache_hook_fixture.cpp','verification/probe/mesh_preparation.cpp','verification/probe/loading_trace_stub.def','verification/probe/build_mesh_cache_hook.sh', 'verification/probe/build_admission_dependencies.sh','verification/probe/run_mesh_cache_hook.py']
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def inputs():return {p:sha(ROOT/p) for p in SOURCES}|{'native/'+n:sha(p) for n,p in NATIVE.items()}
def binaries():return {p.name:sha(p) for p in [BUILD/'mesh_cache_hook_fixture.exe',BUILD/'d3dx9_37.dll']}
def refuse_game():
    if game_running():
        raise RuntimeError('Refusing synthetic runtime while X3AP.exe is running')
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--admission',choices=('0','1'));args=parser.parse_args()
    admission=args.admission or '0';suffix=('-admission-on' if admission=='1' else '-admission-off') if args.admission is not None else ''
    summary=RESULTS/('mesh-cache-hook'+suffix+'-summary.json')
    meta={'passed':False,'admission':int(admission),'phase':'building','fresh_build':False,'game_launched':False,'started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'cases':{}}
    def save():summary.write_text(json.dumps(meta,indent=2)+'\n')
    save()
    try:
        refuse_game()
        meta['sources_before_build']=inputs();save()
        with (RESULTS/('mesh-cache-hook'+suffix+'-build.txt')).open('wb') as log:
            subprocess.run(['sh','verification/probe/build_mesh_cache_hook.sh'],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=90)
        meta['sources_after_build']=inputs();assert meta['sources_after_build']==meta['sources_before_build'],'Build inputs changed'
        shutil.copyfile(NATIVE['d3dx9_37.dll'],BUILD/'d3dx9_37.dll')
        meta.update(fresh_build=True,phase='running',binaries_before=binaries());save()
        for ownership in ('native','wrapped'):
            for mode,fault in (('off','normal'),('on','normal'),('on','fault')):
                name=f'{ownership}-{mode}-{fault}';out=RESULTS/f'mesh-cache-hook{suffix}-{name}.txt';err=RESULTS/f'mesh-cache-hook{suffix}-{name}-wine.log'
                command=['/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine','--bottle','Steam','--no-update','--dll','d3dx9_37=n,b;d3d9=b','--workdir',str(BUILD),str(BUILD/'mesh_cache_hook_fixture.exe'),mode,ownership,fault]
                refuse_game()
                assert inputs()==meta['sources_before_build'] and binaries()==meta['binaries_before'],'Inputs changed before case'
                with out.open('wb') as stdout,err.open('wb') as stderr:
                    run=subprocess.run(command,stdout=stdout,stderr=stderr,env=dict(os.environ, X3M_ADMISSION=admission,WINEDLLOVERRIDES='d3dx9_37=n,b;d3d9=b'),timeout=90)
                text=out.read_text();matches=re.findall(r'^RESULT PASS checks=(\d+)\r?$',text,re.M);match=matches[0] if len(matches)==1 and re.fullmatch(r'RESULT PASS checks=\d+',text.rstrip().splitlines()[-1]) else None
                case={'exit_code':run.returncode,'checks':int(match) if match else 0,'report_sha256':sha(out),'wine_log_sha256':sha(err),'command':command}
                case['cache_result']=[dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in text.splitlines() if line.startswith('CACHE_RESULT ')][0] if 'CACHE_RESULT ' in text else {}
                case['cache_metrics']=[dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in text.splitlines() if line.startswith('mesh_cache_metric ')]
                case['gate_counts']=[dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in text.splitlines() if line.startswith('mesh_cache_gate ')]
                case['gate_details']=[dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in text.splitlines() if line.startswith('mesh_cache_gate_first ')]
                case['core_bypass']=[dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in text.splitlines() if line.startswith('mesh_cache_bypass ')]
                case['dynamic_options']=[dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in text.splitlines() if line.startswith('DYNAMIC ')]
                witnesses=re.findall(r'^ADMISSION_WITNESS requested=(\d+) enabled=(\d+) active_roots=(\d+) waiting_roots=(\d+) admitted_roots=(\d+) promotions=(\d+) vetoes=(\d+) valid=(\d+)\r?$',text,re.M)
                assert len(witnesses)==1 and witnesses[0][0:2]==(admission,admission) and witnesses[0][2:4]==('0','0') and witnesses[0][5]=='0' and witnesses[0][7]=='1','Admission mode/root witness failed'
                assert int(witnesses[0][4])>0 if admission=='1' else int(witnesses[0][4])==0,'Admission root count'
                case['admission_witness']=witnesses[0]
                expected_checks={'native-off-normal':1714,'native-on-normal':1927,'native-on-fault':1935,'wrapped-off-normal':2189,'wrapped-on-normal':2566,'wrapped-on-fault':2574}
                assert case['checks']==expected_checks[name] and sum(line.startswith('RESULT ') for line in text.splitlines())==1,'Exact case inventory changed'
                meta['cases'][name]=case;save()
                assert run.returncode==0 and match and 'RESULT FAIL' not in text,text[-4000:]
                assert 'LIVE32 parity=1' in text and len(re.findall(r'^CASE ',text,re.M))==5,'Missing live32/downstream controls'
                assert [d['options'] for d in case['dynamic_options']]==['00000990','00000991','00018990','00018991'] and all(d['parity']=='1' for d in case['dynamic_options']),'Missing exact game options'
                reasons=[d['reason'] for d in case['gate_details']]
                assert len(reasons)==len(set(reasons)),'First gate detail repeated'
                if mode=='on':
                    assert {'mesh_options','mesh_pool','mesh_method','mesh_table','descriptor_call','descriptor_pool','descriptor_usage','descriptor_format','descriptor_size'}.issubset(reasons),'Missing distinct rejection diagnostics'
                    if ownership=='wrapped':assert 'OFF_TRACKER_ROUTE vertex=2 index=2 requested_preserved=1' in text,'Missing off-tracking route controls'
                    assert re.findall(r'^PUBLIC_DESCRIPTOR type=(vertex|index) forwarding=1 negatives=8 restored=1\r?$',text,re.M)==['vertex','index'],'Missing public VB/IB controls'
                    assert 'file_fingerprint_required=0' in text and 'buffer_contract=public_systemmem_readonly' in text,'Missing portable buffer contract'
                    assert not any(r.startswith('backend_') for r in reasons),'Backend-specific gate returned'
                    assert any(d['reason']=='floating_point' and int(d['count'])>0 for d in case['core_bypass']),'Missing core bypass reason'
                    assert len(re.findall(r'^mesh_cache_fp_first ',text,re.M))==1,'Missing or repeated first FP details'
                else:
                    assert not reasons and not case['core_bypass'],'Disabled cache diagnostics performed work'
                result=case['cache_result']
                assert (int(result['hits'])>0 and int(result['misses'])>0) if mode=='on' else int(result['calls'])==0,'Cache activation coverage'
                assert len(re.findall(r'^mesh_cache_fault ',text,re.M))==(1 if fault=='fault' else 0),'Fault event count'
                assert inputs()==meta['sources_before_build'] and binaries()==meta['binaries_before'],'Inputs changed during case'
        meta.update(sources_after_run=inputs(),binaries_after=binaries(),passed=True,phase='complete')
    except (Exception,KeyboardInterrupt) as error:
        meta.update(passed=False,phase='failed',error=repr(error))
    save();print(json.dumps(meta,indent=2));return 0 if meta['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
