#!/usr/bin/env python3
"""Fresh-build ABI stubs plus exact native mesh timing/lifetime verification."""
from pathlib import Path
import argparse,hashlib,json,os,re,shutil,subprocess
from game_guard import game_running  # real game processes only (review 18)
parser=argparse.ArgumentParser();parser.add_argument('--admission',choices=('0','1'));args=parser.parse_args()
admission=args.admission or '0';suffix=('-admission-on' if admission=='1' else '-admission-off') if args.admission is not None else ''
root=Path(__file__).resolve().parents[2];results=root/'verification/results';build=root/'verification/probe/build'
native=Path.home()/'Library/Application Support/CrossOver/Bottles/Steam/drive_c/X3/d3dx9_37.dll'
paths=[root/name for name in ('src/proxy/capture.h','src/proxy/cpu_state.h','src/proxy/loading_trace.h','src/proxy/loading_trace.cpp','src/proxy/mesh_adjacency_cache.h','src/proxy/mesh_adjacency_cache.cpp','src/ownership/d3d9_ownership.h','src/ownership/d3d9_ownership.cpp', 'src/ownership/application_admission.h', 'src/ownership/application_admission.cpp', 'src/ownership/application_admission_abi.h', 'src/ownership/application_admission_abi.cpp', 'src/ownership/execution_state.cpp', 'src/ownership/execution_state.h', 'src/ownership/finite_buffer_evidence.cpp', 'src/ownership/finite_buffer_evidence.h', 'src/ownership/portable_managed_upload.cpp', 'src/ownership/portable_managed_upload.h','src/ownership/d3d9_classes_inc.h','src/ownership/d3d9_forwarders_inc.h','verification/probe/loading_admission_witness.h','verification/probe/loading_trace_fixture.cpp','verification/probe/loading_trace_stub.cpp','verification/probe/loading_trace_stub.def','verification/probe/loading_codec_stub.cpp','verification/probe/loading_mesh_fixture.cpp','verification/probe/build_loading_trace.sh', 'verification/probe/build_admission_dependencies.sh','verification/probe/run_loading_trace.py')]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
hashes=lambda:{str(p.relative_to(root)):sha(p) for p in paths}
report={'passed':False,'phase':'building','admission':int(admission),'game_launched':False,'cases':{}}
(results/('loading-trace-mesh'+suffix+'-summary.json')).write_text(json.dumps(report,indent=2)+'\n')
def refuse_game():
    running=game_running()
    assert not running,'X3AP is running: '+'; '.join(running)
try:
    refuse_game();report['sources_before_build']=hashes();report['native_before']=sha(native)
    subprocess.run(['sh',str(root/'verification/probe/build_loading_trace.sh')],cwd=root,check=True)
    assert hashes()==report['sources_before_build'],'Sources changed during build'
    shutil.copy(native,build/'loading_mesh/d3dx9_37.dll')
    for case,folder,exe,override in [('loading-trace','loading_trace','loading_trace_fixture.exe','d3dx9_37,zlib1,libxml2=n,b'),('loading-mesh','loading_mesh','loading_mesh_fixture.exe','d3dx9_37=n,b;d3d9=b')]:
        refuse_game()
        assert hashes()==report['sources_before_build'] and sha(native)==report['native_before'],'Inputs changed before case'
        directory=build/folder;binary=directory/exe;before=sha(binary)
        dlls={p.name:sha(p) for p in directory.glob('*.dll')}
        command=['/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine','--bottle','Steam','--no-update','--dll',override,'--workdir',str(directory),str(binary)]
        with (results/(case+suffix+'-fixture.txt')).open('w') as out,(results/(case+suffix+'-fixture-wine.log')).open('w') as err:
            run=subprocess.run(command,env=dict(os.environ, X3M_ADMISSION=admission,WINEDLLOVERRIDES=override,X3M_MESH_CACHE='0'),stdout=out,stderr=err,timeout=90)
        text=(results/(case+suffix+'-fixture.txt')).read_text()
        witnesses=re.findall(r'^ADMISSION_WITNESS requested=(\d+) enabled=(\d+) active_roots=(\d+) waiting_roots=(\d+) admitted_roots=(\d+) promotions=(\d+) vetoes=(\d+) valid=(\d+)\r?$',text,re.M)
        assert len(witnesses)==1 and witnesses[0][0:2]==(admission,admission) and witnesses[0][2:4]==('0','0') and witnesses[0][5]=='0' and witnesses[0][7]=='1','Admission mode/root witness failed'
        assert int(witnesses[0][4])>0 if admission=='1' else int(witnesses[0][4])==0,'Admission root count'
        terminal=('loading_fixture checks=85 failures=0' if case=='loading-trace' else 'LOADING MESH RESULT checks=123 failures=0')
        assert text.rstrip().endswith(terminal) and text.count(terminal)==1,'Incomplete exact case inventory'
        report['cases'][case]={'admission_witness':witnesses[0],'checks':85 if case=='loading-trace' else 123,'wine_log_sha256':sha(results/(case+suffix+'-fixture-wine.log')),'exit_code' :run.returncode,'executable_sha256':before,'dll_sha256':dlls,'report_sha256':sha(results/(case+suffix+'-fixture.txt'))}
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
