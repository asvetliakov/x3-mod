#!/usr/bin/env python3
"""Link the real loader/capture objects with an adoption-failure test double."""
from pathlib import Path
import datetime
import hashlib
import json
import os
import shutil
import subprocess
root=Path(__file__).resolve().parents[2];probe=root/'verification/probe/build';results=root/'verification/results'
directory=probe/('ownership-integration-fallback-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'));directory.mkdir(parents=True)
objects=sorted((root/'build-ownership/CMakeFiles/d3d9.dir/src/proxy').glob('*.obj'))
assert len(objects)==5,'Build the experimental DLL first'
command=['i686-w64-mingw32-g++','-std=c++17','-shared','-static','-static-libgcc','-static-libstdc++','-Wl,--kill-at','-Wl,--enable-stdcall-fixup','-o',str(directory/'d3d9.dll'),str(root/'verification/probe/ownership_integration_fallback_stub.cpp'),*[str(p) for p in objects],str(root/'src/proxy/d3d9.def'),'-ldxguid','-luser32']
subprocess.run(command,check=True)
shutil.copy(probe/'d3d9_smoke.exe',directory)
env=dict(os.environ,X3M_OWNERSHIP='1',X3M_SAMPLEABLE_DEPTH='0',X3M_TELEMETRY='1',X3M_CAPTURE_START='1',X3M_CAPTURE_FRAMES='1')
wine='/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine'
stdout_path=results/'ownership-integration-fallback.txt'
with stdout_path.open('w') as stdout,(results/'ownership-integration-fallback-wine.log').open('w') as stderr:
    completed=subprocess.run([wine,'--bottle','Steam','--no-update','--dll','d3d9=n,b','--workdir',str(directory),str(directory/'d3d9_smoke.exe')],env=env,stdout=stdout,stderr=stderr,timeout=90)
trace=max((directory/'x3-modern-captures').glob('session-*.log'),key=lambda p:p.stat().st_mtime)
shutil.copy(trace,results/'ownership-integration-fallback-capture.log')
text=trace.read_text();output=stdout_path.read_text()
assert completed.returncode==0 and 'SMOKE RESULT: 0 failures' in output
assert 'ownership_factory mode=native_fallback' in text and 'result=8007000e' in text
assert 'ownership_factory mode=wrapped' not in text and 'ownership_depth ' not in text
assert sum(line.startswith('device_hooked ') for line in text.splitlines())==2
assert sum(line.startswith('device_destroy ') for line in text.splitlines())==2
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
report={'result':'PASS','fault':'wrap_factory E_OUTOFMEMORY without consuming native ref','production_objects':['src/proxy/'+p.name for p in objects],'verification_dll_sha256':sha(directory/'d3d9.dll'),'production_dll_sha256':sha(root/'build-ownership/d3d9.dll'),'trace_sha256':sha(trace),'stub_sha256':sha(root/'verification/probe/ownership_integration_fallback_stub.cpp'),'installed':False}
(results/'ownership-integration-fallback.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
