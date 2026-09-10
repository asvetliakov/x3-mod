#!/usr/bin/env python3
"""Fresh build and actual imported/shared-slot mesh cache matrix; never launches X3."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
ROOT=Path(__file__).resolve().parents[2]
RESULTS=ROOT/'verification/results'
BUILD=ROOT/'verification/probe/build/mesh_cache_hook'
BOTTLE=Path.home()/'Library/Application Support/CrossOver/Bottles/Steam/drive_c'
NATIVE={
 'd3dx9_37.dll':(BOTTLE/'X3/d3dx9_37.dll','c2ccb84c672a9d8966e82a28005a4269886ee304972ac3590c0b8a9c1622a3d8'),
 'd3d9.dll':(BOTTLE/'windows/syswow64/d3d9.dll','58cc36cf74128ae4b6211100430d146c3692808146d8d2075e6c5d846162f8cf'),
 'wined3d.dll':(BOTTLE/'windows/syswow64/wined3d.dll','f4997bc0465de7e87bac9921bf0274db00ac3b3ba0754fa03f1f33e309a8e863')}
SOURCES=['src/proxy/capture.h','src/proxy/loading_trace.h','src/proxy/loading_trace.cpp','src/proxy/mesh_adjacency_cache.h','src/proxy/mesh_adjacency_cache.cpp','src/ownership/d3d9_ownership.h','src/ownership/d3d9_ownership.cpp','src/ownership/d3d9_classes_inc.h','src/ownership/d3d9_forwarders_inc.h','verification/probe/mesh_cache_hook_fixture.cpp','verification/probe/mesh_preparation.cpp','verification/probe/loading_trace_stub.def','verification/probe/build_mesh_cache_hook.sh','verification/probe/run_mesh_cache_hook.py']
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def inputs():return {p:sha(ROOT/p) for p in SOURCES}|{'native/'+n:sha(p) for n,(p,_) in NATIVE.items()}
def binaries():return {p.name:sha(p) for p in [BUILD/'mesh_cache_hook_fixture.exe',BUILD/'d3dx9_37.dll']}
def main():
    summary=RESULTS/'mesh-cache-hook-summary.json'
    meta={'passed':False,'phase':'building','fresh_build':False,'game_launched':False,'started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'cases':{}}
    def save():summary.write_text(json.dumps(meta,indent=2)+'\n')
    save()
    try:
        meta['sources_before_build']=inputs();save()
        for name,(_,expected) in NATIVE.items():assert meta['sources_before_build']['native/'+name]==expected,'Unexpected runtime '+name
        with (RESULTS/'mesh-cache-hook-build.txt').open('wb') as log:
            subprocess.run(['sh','verification/probe/build_mesh_cache_hook.sh'],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=90)
        meta['sources_after_build']=inputs();assert meta['sources_after_build']==meta['sources_before_build'],'Build inputs changed'
        shutil.copyfile(NATIVE['d3dx9_37.dll'][0],BUILD/'d3dx9_37.dll')
        meta.update(fresh_build=True,phase='running',binaries_before=binaries());save()
        for ownership in ('native','wrapped'):
            for mode,fault in (('off','normal'),('on','normal'),('on','fault')):
                name=f'{ownership}-{mode}-{fault}';out=RESULTS/f'mesh-cache-hook-{name}.txt';err=RESULTS/f'mesh-cache-hook-{name}-wine.log'
                command=['/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine','--bottle','Steam','--no-update','--dll','d3dx9_37=n,b;d3d9=b','--workdir',str(BUILD),str(BUILD/'mesh_cache_hook_fixture.exe'),mode,ownership,fault]
                assert inputs()==meta['sources_before_build'] and binaries()==meta['binaries_before'],'Inputs changed before case'
                with out.open('wb') as stdout,err.open('wb') as stderr:
                    run=subprocess.run(command,stdout=stdout,stderr=stderr,env=dict(os.environ,WINEDLLOVERRIDES='d3dx9_37=n,b;d3d9=b'),timeout=90)
                text=out.read_text();matches=re.findall(r'^RESULT PASS checks=(\d+)\r?$',text,re.M);match=matches[0] if len(matches)==1 and re.fullmatch(r'RESULT PASS checks=\d+',text.rstrip().splitlines()[-1]) else None
                case={'exit_code':run.returncode,'checks':int(match) if match else 0,'report_sha256':sha(out),'wine_log_sha256':sha(err),'command':command}
                case['cache_result']=[dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in text.splitlines() if line.startswith('CACHE_RESULT ')][0] if 'CACHE_RESULT ' in text else {}
                case['cache_metrics']=[dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in text.splitlines() if line.startswith('mesh_cache_metric ')]
                meta['cases'][name]=case;save()
                assert run.returncode==0 and match and 'RESULT FAIL' not in text,text[-4000:]
                assert 'LIVE32 parity=1' in text and len(re.findall(r'^CASE ',text,re.M))==5,'Missing live32/downstream controls'
                result=case['cache_result']
                assert (int(result['hits'])>0 and int(result['misses'])>0) if mode=='on' else int(result['calls'])==0,'Cache activation coverage'
                assert len(re.findall(r'^mesh_cache_fault ',text,re.M))==(1 if fault=='fault' else 0),'Fault event count'
                assert inputs()==meta['sources_before_build'] and binaries()==meta['binaries_before'],'Inputs changed during case'
        meta.update(sources_after_run=inputs(),binaries_after=binaries(),passed=True,phase='complete')
    except (Exception,KeyboardInterrupt) as error:
        meta.update(passed=False,phase='failed',error=repr(error))
    save();print(json.dumps(meta,indent=2));return 0 if meta['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
