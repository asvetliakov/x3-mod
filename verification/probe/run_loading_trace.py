#!/usr/bin/env python3
"""Fresh-build ABI stubs plus exact native mesh timing/lifetime verification."""
from pathlib import Path
import argparse,hashlib,json,os,re,shutil,subprocess
from game_guard import game_running  # real game processes only (review 18)
import bottle  # CrossOver bottle selection (X3M_FIXTURE_BOTTLE) and the per-bottle results directory
parser=argparse.ArgumentParser();parser.add_argument('--admission',choices=('0','1'));args=parser.parse_args()
admission=args.admission or '0';suffix=('-admission-on' if admission=='1' else '-admission-off') if args.admission is not None else ''
root=Path(__file__).resolve().parents[2];results=bottle.results_dir(root);build=root/'verification/probe/build'
native=bottle.game_dir() / 'd3dx9_37.dll'
paths=[root/name for name in ('src/proxy/capture.h','src/proxy/cpu_state.h','src/proxy/loading_trace.h','src/proxy/loading_trace.cpp','src/proxy/crypt_cache.h','src/proxy/crypt_cache.cpp','src/proxy/loading_trace_light.h','src/proxy/loading_trace_light.cpp','src/proxy/engine_patch.h','src/proxy/engine_patch.cpp','src/proxy/loading_probes.h','src/proxy/loading_probes.cpp','src/proxy/resource_reader.h','src/proxy/resource_reader.cpp','src/proxy/resource_reader_core.cpp','verification/probe/build_loading_light.sh','src/proxy/mesh_adjacency_fast.h','src/proxy/mesh_adjacency_fast.cpp','src/ownership/d3d9_ownership.h','src/ownership/d3d9_ownership.cpp', 'src/ownership/application_admission.h', 'src/ownership/application_admission.cpp', 'src/ownership/application_admission_abi.h', 'src/ownership/application_admission_abi.cpp', 'src/ownership/execution_state.cpp', 'src/ownership/execution_state.h', 'src/ownership/finite_buffer_evidence.cpp', 'src/ownership/finite_buffer_evidence.h', 'src/ownership/portable_managed_upload.cpp', 'src/ownership/portable_managed_upload.h','src/ownership/d3d9_classes_inc.h','src/ownership/d3d9_forwarders_inc.h','verification/probe/loading_admission_witness.h','verification/probe/loading_trace_fixture.cpp','verification/probe/loading_trace_stub.cpp','verification/probe/loading_trace_stub.def','verification/probe/loading_codec_stub.cpp','verification/probe/loading_mesh_fixture.cpp','verification/probe/mesh_adjacency_fast_fixture.cpp','verification/probe/build_loading_trace.sh', 'verification/probe/build_admission_dependencies.sh','verification/probe/run_loading_trace.py')]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
MESH_ADJACENCY_CHECKS=36091 # exact inventory of the run (36,093 in the cache-off run before the adjacency cache was removed on 2026-09-25; mesh_adjacency_fast_fixture.cpp: 54 named cases, the 2,000-mesh random sweep, the self-test dump replay, state sweep, hook part)
ADJACENCY_RANDOM='ADJACENCY_RANDOM trials=2000 equal=2000 mismatched=0 ' # the random differential sweep against d3dx9_37 must be complete and clean
hashes=lambda:{str(p.relative_to(root)):sha(p) for p in paths}
report={'passed':False,'phase':'building','admission':int(admission),'game_launched':False, 'bottle':bottle.describe(),'cases':{}}
(results/('loading-trace-mesh'+suffix+'-summary.json')).write_text(json.dumps(report,indent=2)+'\n')
def refuse_game():
    running=game_running()
    assert not running,'X3AP is running: '+'; '.join(running)
try:
    refuse_game();report['sources_before_build']=hashes();report['native_before']=sha(native)
    subprocess.run(['sh',str(root/'verification/probe/build_loading_trace.sh')],cwd=root,check=True)
    assert hashes()==report['sources_before_build'],'Sources changed during build'
    shutil.copy(native,build/'loading_mesh/d3dx9_37.dll');shutil.copy(native,build/'mesh_adjacency_fast/d3dx9_37.dll')
    for case,folder,exe,override,arguments,timeout in [('loading-trace','loading_trace','loading_trace_fixture.exe','d3dx9_37,zlib1,libxml2=n,b',[],90),('loading-mesh','loading_mesh','loading_mesh_fixture.exe','d3dx9_37=n,b;d3d9=b',[],90),('mesh-adjacency','mesh_adjacency_fast','mesh_adjacency_fast_fixture.exe','d3dx9_37=n,b;d3d9=b',[],300)]:
        refuse_game()
        assert hashes()==report['sources_before_build'] and sha(native)==report['native_before'],'Inputs changed before case'
        directory=build/folder;binary=directory/exe;before=sha(binary)
        dlls={p.name:sha(p) for p in directory.glob('*.dll')}
        command=['/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine','--bottle',bottle.BOTTLE,'--no-update','--dll',override,'--workdir',str(directory),str(binary)]+arguments
        with (results/(case+suffix+'-fixture.txt')).open('w') as out,(results/(case+suffix+'-fixture-wine.log')).open('w') as err:
            run=subprocess.run(command,env=dict(os.environ, X3M_CAMERA='vanilla', X3M_CHASE_SCENE_FIX='0', X3M_CHASE_COMBAT_TIGHTNESS='0', X3M_ADMISSION=admission,WINEDLLOVERRIDES=override),stdout=out,stderr=err,timeout=timeout)
        text=(results/(case+suffix+'-fixture.txt')).read_text()
        witnesses=re.findall(r'^ADMISSION_WITNESS requested=(\d+) enabled=(\d+) active_roots=(\d+) waiting_roots=(\d+) admitted_roots=(\d+) promotions=(\d+) vetoes=(\d+) valid=(\d+)\r?$',text,re.M)
        assert len(witnesses)==1 and witnesses[0][0:2]==(admission,admission) and witnesses[0][2:4]==('0','0') and witnesses[0][5]=='0' and witnesses[0][7]=='1','Admission mode/root witness failed'
        assert int(witnesses[0][4])>0 if admission=='1' else int(witnesses[0][4])==0,'Admission root count'
        expected_checks={'loading-trace':112,'loading-mesh':123,'mesh-adjacency':MESH_ADJACENCY_CHECKS}[case]
        terminal={'loading-trace':'loading_fixture checks=112 failures=0','loading-mesh':'LOADING MESH RESULT checks=123 failures=0'}.get(case,'MESH ADJACENCY RESULT checks=%d failures=0'%expected_checks)
        assert text.rstrip().endswith(terminal) and text.count(terminal)==1,'Incomplete exact case inventory: '+text.rstrip().splitlines()[-1]
        if case=='loading-trace':
            assert len(re.findall(r'^engine_patch_rel32 checks=26 failures=0 directions=2 edges=4 late_claim=1\r?$',text,re.M))==1,'Missing executable Jcc relocation controls'
        report['cases'][case]={'admission_witness':witnesses[0],'checks':expected_checks,'wine_log_sha256':sha(results/(case+suffix+'-fixture-wine.log')),'exit_code' :run.returncode,'executable_sha256':before,'dll_sha256':dlls,'report_sha256':sha(results/(case+suffix+'-fixture.txt'))}
        if case.startswith('mesh-adjacency'):
            admission_control=re.findall(r'^ADJACENCY_ADMISSION requested_domains=13 distinct_runtime_domains=(12|13) canonicalized_domains=(0|1) registry=8 checks=109 untouched=1 incoming_preserved=1 diagnostic_refused=1\r?$',text,re.M)
            assert len(admission_control)==1 and sum(map(int,admission_control[0]))==13,'Missing complete adjacency refusal controls'
            report['cases'][case]['admission_domains']={'requested':13,'distinct_runtime':int(admission_control[0][0]),'canonicalized':int(admission_control[0][1])}
            fields=lambda prefix:[dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in text.splitlines() if line.startswith(prefix+' ')]
            report['cases'][case].update(adjacency_cases=fields('ADJACENCY_CASE'),policies=fields('ADJACENCY_POLICY'),verify_stats=fields('VERIFY_STATS'),fast_stats=fields('FAST_STATS'),metrics=fields('mesh_adjacency_metric'))
            computable=[c for c in report['cases'][case]['adjacency_cases'] if c['status']=='ok']
            assert computable and all(c['equal']=='1' and c['mismatches']=='0' for c in computable),'Module output differs from native D3DX'
            computable_names={c['name'] for c in computable} # non-computable cases (native fallback) print their policy variants with equal=0 by design
            assert all(p['equal']=='1' for p in report['cases'][case]['policies'] if p['variant']=='default' and p['name'] in computable_names),'Default policy differs from native on tie evidence'
            random_lines=[line for line in text.splitlines() if line.startswith('ADJACENCY_RANDOM ')]
            assert len(random_lines)==1 and random_lines[0].startswith(ADJACENCY_RANDOM) and 'ADJACENCY_RANDOM_MISMATCH' not in text,'Random differential sweep against native D3DX: '+(random_lines[0] if random_lines else 'missing')
            report['cases'][case]['random_sweep']=dict(re.findall(r'(\w+)=([^\s]+)',random_lines[0]))
            if case=='mesh-adjacency':
                # The fixture wrote the dump of fan-tilted-abc (native and module arrays) into its
                # working directory; the offline replay tool must read it and reproduce native.
                replay=subprocess.run(['python3',str(root/'tools/analysis/replay_mesh_adjacency.py'),str(directory/'mesh-adjacency-selftest.bin')],cwd=root,capture_output=True,text=True,timeout=300)
                summary=[line for line in replay.stdout.splitlines() if line.startswith('REPLAY_SUMMARY ')]
                assert replay.returncode==0 and summary==['REPLAY_SUMMARY dumps=1 failures=0'],'Self-test dump replay: '+replay.stdout[-2000:]+replay.stderr[-2000:]
                report['cases'][case]['selftest_replay']=summary[0]
        assert run.returncode==0 and 'failures=0' in text and 'FAIL' not in text,text[-4000:]
        assert before==sha(binary) and dlls=={p.name:sha(p) for p in directory.glob('*.dll')},'Binaries changed during run'
    assert hashes()==report['sources_before_build'] and sha(native)==report['native_before'],'Sources changed during run'
    report['sources_after_run']=hashes();report['native_after']=sha(native);report['sources_and_binaries_unchanged']=True;report['passed']=True;report['phase']='complete'
except (Exception,KeyboardInterrupt) as error:
    report.update(passed=False,phase='failed',error=repr(error))
finally:
    (results/('loading-trace-mesh'+suffix+'-summary.json')).write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))

raise SystemExit(0 if report['passed'] else 1)
