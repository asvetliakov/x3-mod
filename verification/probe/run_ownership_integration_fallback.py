#!/usr/bin/env python3
"""Link the real loader/capture objects with an adoption-failure test double."""
from pathlib import Path
import datetime
import json
import os
import shutil
import subprocess
root=Path(__file__).resolve().parents[2];probe=root/'verification/probe/build';results=root/'verification/results'
from run_ownership_integration import sources, binaries, sha, require_no_game
from verify_ownership_integration import verify_geometry, verify_motion_mode
(results/'ownership-integration-fallback.json').write_text(json.dumps({'result':'RUNNING'})+'\n')
require_no_game()
manifest=json.loads((results/'ownership-integration-build.json').read_text())
assert manifest['result']=='PASS' and manifest['sources']==sources(),'Run fresh-build integration against current sources first'
assert manifest['binaries_at_end']==binaries(),'Integration binaries changed'
object_root=root/'build-ownership/CMakeFiles/d3d9.dir'
objects=sorted(p for p in (object_root/'src').rglob('*.obj') if 'ownership' not in p.parts or p.name == 'execution_state.cpp.obj')
expected={'src/proxy/'+name+'.cpp.obj' for name in ('loader','capture','motion_capture','capture_state','scene_capture','object_trace','telemetry','loading_trace','draw_input','object_lifetime','mesh_adjacency_cache')}
expected.update('src/renderer/'+name+'.cpp.obj' for name in
                ('temporal_pass','material_radiance','motion_history','rigid_position','rigid_motion','rigid_replay_program'))
expected.add('src/ownership/execution_state.cpp.obj')
assert {str(p.relative_to(object_root)) for p in objects}==expected,'Build all current production components first'
object_hashes={str(p.relative_to(root)):sha(p) for p in objects}
directory=probe/('ownership-integration-fallback-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'));directory.mkdir(parents=True)
command=['i686-w64-mingw32-g++','-std=c++17','-msse2','-mfpmath=sse','-mstackrealign','-mincoming-stack-boundary=2','-shared','-static','-static-libgcc','-static-libstdc++','-Wl,--kill-at','-Wl,--enable-stdcall-fixup','-o',str(directory/'d3d9.dll'),str(root/'verification/probe/ownership_integration_fallback_stub.cpp'),*[str(p) for p in objects],str(root/'src/proxy/d3d9.def'),'-ldxguid','-luser32','-ladvapi32']
subprocess.run(command,check=True)
shutil.copy(probe/'d3d9_smoke.exe',directory)
local_hashes={name:sha(directory/name) for name in ('d3d9.dll','d3d9_smoke.exe')}
assert local_hashes['d3d9_smoke.exe']==manifest['binaries_at_end']['verification/probe/build/d3d9_smoke.exe']
env=dict(os.environ,X3M_OWNERSHIP='1',X3M_DEPTH_COPY='0',X3M_SCENE_DEPTH_CAPTURE='1',X3M_OBJECT_TRACE='0',X3M_OBJECT_LIFETIME='0',X3M_MESH_CACHE='0',X3M_FINITE_POSITIONS='1',X3M_MOTION_CAPTURE='1',X3M_TELEMETRY='1',X3M_CAPTURE_START='1',X3M_CAPTURE_FRAMES='1')
wine='/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine'
stdout_path=results/'ownership-integration-fallback.txt'
with stdout_path.open('w') as stdout,(results/'ownership-integration-fallback-wine.log').open('w') as stderr:
    require_no_game()
    completed=subprocess.run([wine,'--bottle','Steam','--no-update','--dll','d3d9=n,b','--workdir',str(directory),str(directory/'d3d9_smoke.exe')],env=env,stdout=stdout,stderr=stderr,timeout=90)
trace=max((directory/'x3-modern-captures').glob('session-*.log'),key=lambda p:p.stat().st_mtime)
shutil.copy(trace,results/'ownership-integration-fallback-capture.log')
text=trace.read_text();output=stdout_path.read_text()
assert completed.returncode==0 and 'SMOKE RESULT: 0 failures' in output
assert 'ownership_factory mode=native_fallback' in text and 'result=8007000e' in text
assert 'ownership_factory mode=wrapped' not in text and 'ownership_copy_depth ' not in text
assert 'finite_upload_mode requested=1 enabled=1 ' in text
# Loader enabled the option, but adoption failed: native queries must stay unknown.
geometry=verify_geometry(text,requested=True,enabled=True,native_fallback=True)
motion_mode=verify_motion_mode(text,configured=True)
assert geometry['scoped_geometry_records']>0, 'Fallback captured draw geometry missing'
assert sum(line.startswith('device_hooked ') for line in text.splitlines())==2
assert sum(line.startswith('device_destroy ') for line in text.splitlines())==2
assert all(sha(directory/name)==digest for name,digest in local_hashes.items()),'Fallback executable/DLL changed during run'
assert manifest['sources']==sources() and manifest['binaries_at_end']==binaries(),'Inputs changed during fallback verification'
assert all(sha(root/path)==digest for path,digest in object_hashes.items()),'Proxy objects changed during fallback verification'
report={'sources_before_and_after':manifest['sources'],'binaries_before_and_after':manifest['binaries_at_end'],'production_object_hashes':object_hashes,'result':'PASS','fault':'wrap_factory E_OUTOFMEMORY without consuming native ref','production_objects':[str(p.relative_to(object_root)) for p in objects],'verification_dll_sha256':sha(directory/'d3d9.dll'),'production_dll_sha256':sha(root/'build-ownership/d3d9.dll'),'trace_sha256':sha(trace),'stub_sha256':sha(root/'verification/probe/ownership_integration_fallback_stub.cpp'),'installed':False,'finite_requested':True,'geometry':geometry,'motion_mode':motion_mode,'local_binaries_before_and_after':local_hashes}
(results/'ownership-integration-fallback.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
