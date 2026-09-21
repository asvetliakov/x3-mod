#!/usr/bin/env python3
"""Cross-build only: actual helper + extracted draw hook with fixture-owned COM mocks."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from verification.analysis.test_capture_bloom_lifetime import extract_function


def build(output):
    output=Path(output).resolve();output.mkdir(parents=True,exist_ok=False)
    extracted=output/'lattice_state_draw_under_test_inc.h'
    extracted.write_text(extract_function((ROOT/'src/proxy/capture.cpp').read_text(),'HRESULT WINAPI draw_indexed('))
    exe=output/'lattice_state_fixture.exe'
    compiler=shutil.which('i686-w64-mingw32-g++')
    if compiler is None:raise RuntimeError('i686-w64-mingw32-g++ unavailable')
    sources=[ROOT/p for p in ['verification/probe/lattice_state_fixture.cpp','verification/probe/lattice_state_only_ownership_stubs.cpp','src/proxy/lattice_state_capture.cpp','src/proxy/capture_state.cpp']]
    command=[compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-msse2','-mfpmath=sse',
             '-mstackrealign','-mincoming-stack-boundary=2','-DX3M_LATTICE_STATE_ONLY_FIXTURE','-ffunction-sections','-fdata-sections',
             '-I',str(ROOT/'src/proxy'),'-I',str(output),*[str(p) for p in sources],
             '-Wl,--gc-sections','-static','-static-libgcc','-static-libstdc++','-ldxguid','-ladvapi32','-o',str(exe)]
    begin=time.monotonic();run=subprocess.run(command,capture_output=True,text=True);elapsed=time.monotonic()-begin
    (output/'build.stdout').write_text(run.stdout);(output/'build.stderr').write_text(run.stderr)
    dependencies=sources+[ROOT/'src/proxy/lattice_state_capture.h',ROOT/'src/proxy/lattice_geometry_packet.h',ROOT/'src/proxy/lattice_geometry_windows.h',ROOT/'src/ownership/clone_upload_observer.h',ROOT/'src/ownership/clone_upload_core.h',ROOT/'src/ownership/d3d9_ownership.h',ROOT/'src/ownership/buffer_lock_observation.h',ROOT/'src/proxy/lattice_state_policy.h',ROOT/'src/proxy/cpu_state.h',ROOT/'src/proxy/capture_state.h',extracted]
    record=dict(command=command,exit_code=run.returncode,seconds=elapsed,inputs={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in dependencies},
                compiler=subprocess.check_output([compiler,'--version'],text=True).splitlines()[0],native_execution=False,upload_requested=False,ownership_upload_stubbed=True,geometry_payload_tested=False)
    if run.returncode==0:record['exe_sha256']=hashlib.sha256(exe.read_bytes()).hexdigest()
    (output/'build.json').write_text(json.dumps(record,indent=2)+'\n')
    if run.returncode:raise RuntimeError(f'cross-build failed; see {output}/build.stderr')
    print(json.dumps({'exe':str(exe),'sha256':record['exe_sha256'],'seconds':elapsed}))

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',required=True)
    build(parser.parse_args().output)
