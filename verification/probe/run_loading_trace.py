#!/usr/bin/env python3
"""Fresh-build ABI stubs plus exact native mesh timing/lifetime verification."""
from pathlib import Path
import hashlib,json,os,shutil,subprocess
root=Path(__file__).resolve().parents[2];results=root/'verification/results';build=root/'verification/probe/build'
native=Path.home()/'Library/Application Support/CrossOver/Bottles/Steam/drive_c/X3/d3dx9_37.dll'
paths=[root/name for name in ('src/proxy/capture.h','src/proxy/loading_trace.h','src/proxy/loading_trace.cpp','src/proxy/mesh_adjacency_cache.h','src/proxy/mesh_adjacency_cache.cpp','src/ownership/d3d9_ownership.h','src/ownership/d3d9_ownership.cpp','src/ownership/d3d9_classes_inc.h','src/ownership/d3d9_forwarders_inc.h','verification/probe/loading_trace_fixture.cpp','verification/probe/loading_trace_stub.cpp','verification/probe/loading_trace_stub.def','verification/probe/loading_codec_stub.cpp','verification/probe/loading_mesh_fixture.cpp','verification/probe/build_loading_trace.sh','verification/probe/run_loading_trace.py')]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
hashes=lambda:{str(p.relative_to(root)):sha(p) for p in paths}
report={'passed':False,'sources_before_build':hashes(),'game_launched':False,'cases':{}}
(results/'loading-trace-mesh-summary.json').write_text(json.dumps(report,indent=2)+'\n')
try:
    assert sha(native)=='c2ccb84c672a9d8966e82a28005a4269886ee304972ac3590c0b8a9c1622a3d8','Unverified native D3DX'
    subprocess.run(['sh',str(root/'verification/probe/build_loading_trace.sh')],cwd=root,check=True)
    assert hashes()==report['sources_before_build'],'Sources changed during build'
    shutil.copy(native,build/'loading_mesh/d3dx9_37.dll')
    for case,folder,exe,override in [('loading-trace','loading_trace','loading_trace_fixture.exe','d3dx9_37,zlib1,libxml2=n,b'),('loading-mesh','loading_mesh','loading_mesh_fixture.exe','d3dx9_37=n,b;d3d9=b')]:
        directory=build/folder;binary=directory/exe;before=sha(binary)
        dlls={p.name:sha(p) for p in directory.glob('*.dll')}
        command=['/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine','--bottle','Steam','--no-update','--dll',override,'--workdir',str(directory),str(binary)]
        with (results/(case+'-fixture.txt')).open('w') as out,(results/(case+'-fixture-wine.log')).open('w') as err:
            run=subprocess.run(command,env=dict(os.environ,WINEDLLOVERRIDES=override,X3M_MESH_CACHE='0'),stdout=out,stderr=err,timeout=90)
        text=(results/(case+'-fixture.txt')).read_text()
        report['cases'][case]={'exit_code':run.returncode,'executable_sha256':before,'dll_sha256':dlls,'report_sha256':sha(results/(case+'-fixture.txt'))}
        assert run.returncode==0 and 'failures=0' in text and 'FAIL' not in text,text[-4000:]
        assert before==sha(binary) and dlls=={p.name:sha(p) for p in directory.glob('*.dll')},'Binaries changed during run'
    assert hashes()==report['sources_before_build'],'Sources changed during run'
    report['sources_and_binaries_unchanged']=True;report['passed']=True
finally:
    (results/'loading-trace-mesh-summary.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
