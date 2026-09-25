#!/usr/bin/env python3
"""Link the real loader/capture objects with an adoption-failure test double."""
from pathlib import Path
import datetime
import json
import os
import re
import shutil
import subprocess
import bottle  # CrossOver bottle selection (X3M_FIXTURE_BOTTLE) and the per-bottle results directory
import fixture_log  # X3M_LOG_FILE: the session log where this runner reads it (logging tiers)
root=Path(__file__).resolve().parents[2];probe=root/'verification/probe/build';results=bottle.results_dir(root)
from run_ownership_integration import sources, binaries, sha, require_no_game
from verify_ownership_integration import verify_geometry, verify_motion_mode, verify_admission


def production_link_inputs(project_root=root):
    object_root = project_root / 'build-ownership/CMakeFiles/d3d9.dir'
    objects = sorted(p for p in (object_root / 'src').rglob('*.obj')
                     if 'ownership' not in p.parts or p.name in
                     ('execution_state.cpp.obj', 'application_admission.cpp.obj', 'application_admission_abi.cpp.obj'))
    # Reconcile the linked set with the actual production target, retaining the
    # ownership test-double boundary. The generated compositor inputs are part
    # of that target too; omitting them made the fallback link stale.
    production_target = re.search(r'add_library\(d3d9 SHARED(.*?)\)',
                                  (project_root / 'CMakeLists.txt').read_text(), re.S).group(1)
    expected = {name + '.obj' for name in re.findall(r'src/[\w/]+\.cpp', production_target)
                if '/ownership/' not in name or Path(name).name in
                ('execution_state.cpp', 'application_admission.cpp', 'application_admission_abi.cpp')}
    assert {str(p.relative_to(object_root)) for p in objects} == expected, 'Build all current production components first'
    compositor = project_root / 'build-ownership/compositor_bridge'
    generated = [compositor / 'compositor_bridge.o', compositor / 'compositor_bridge_seh_gnu.obj']
    runtime = compositor / 'libx3m_compositor_seh_runtime.a'
    assert all(p.is_file() for p in (*generated, runtime)), 'Build current production compositor inputs first'
    return object_root, objects, generated, runtime


def main():
    (results / 'ownership-integration-fallback.json').write_text(json.dumps({'result': 'RUNNING'}) + '\n')
    require_no_game()
    manifest = json.loads((results / 'ownership-integration-build.json').read_text())
    assert manifest['result'] == 'PASS' and manifest['sources'] == sources(), 'Run fresh-build integration against current sources first'
    assert manifest['binaries_at_end'] == binaries(), 'Integration binaries changed'
    object_root, objects, generated, runtime = production_link_inputs()
    link_inputs = [*objects, *generated, runtime]
    object_hashes = {str(p.relative_to(root)): sha(p) for p in link_inputs}
    directory = probe / ('ownership-integration-fallback-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    directory.mkdir(parents=True)
    command = ['i686-w64-mingw32-g++', '-std=c++17', '-pthread', '-msse2', '-mfpmath=sse',
               '-mstackrealign', '-mincoming-stack-boundary=2', '-shared', '-static', '-static-libgcc',
               '-static-libstdc++', '-Wl,--kill-at', '-Wl,--enable-stdcall-fixup', '-o', str(directory / 'd3d9.dll'),
               str(root / 'verification/probe/ownership_integration_fallback_stub.cpp'),
               *[str(p) for p in objects], *[str(p) for p in generated], str(root / 'src/proxy/d3d9.def'),
               str(runtime), '-ldxguid', '-luser32', '-ladvapi32']
    subprocess.run(command, check=True)
    shutil.copy(probe / 'd3d9_smoke.exe', directory)
    local_hashes = {name: sha(directory / name) for name in ('d3d9.dll', 'd3d9_smoke.exe')}
    assert local_hashes['d3d9_smoke.exe'] == manifest['binaries_at_end']['verification/probe/build/d3d9_smoke.exe']
    env = dict(os.environ, X3M_CAMERA='vanilla', X3M_CHASE_SCENE_FIX='0', X3M_CHASE_COMBAT_TIGHTNESS='0', X3M_OWNERSHIP='1', X3M_ADMISSION='1', X3M_DEPTH_COPY='0', X3M_SCENE_DEPTH_CAPTURE='1', X3M_OBJECT_TRACE='0', X3M_OBJECT_LIFETIME='0', X3M_MESH_CACHE='0', X3M_FINITE_POSITIONS='1', X3M_MOTION_CAPTURE='1', X3M_TELEMETRY='1', X3M_CAPTURE_START='1', X3M_CAPTURE_FRAMES='1')
    wine = '/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine'
    stdout_path = results / 'ownership-integration-fallback.txt'
    with stdout_path.open('w') as stdout, (results / 'ownership-integration-fallback-wine.log').open('w') as stderr:
        require_no_game()
        completed = subprocess.run([wine, '--bottle', bottle.BOTTLE, '--no-update', '--dll', 'd3d9=n,b', '--workdir', str(directory), str(directory / 'd3d9_smoke.exe')], env={**env, **fixture_log.session_log_env(directory)}, stdout=stdout, stderr=stderr, timeout=90)
    trace = max((directory / 'x3-modern-captures').glob('session-*.log'), key=lambda p: p.stat().st_mtime)
    shutil.copy(trace, results / 'ownership-integration-fallback-capture.log')
    text = trace.read_text(); output = stdout_path.read_text()
    assert completed.returncode == 0 and 'SMOKE RESULT: 0 failures' in output
    assert 'ownership_factory mode=native_fallback' in text and 'result=8007000e' in text
    assert 'ownership_factory mode=wrapped' not in text and 'ownership_copy_depth ' not in text
    assert 'finite_upload_mode requested=1 enabled=1 ' in text
    geometry = verify_geometry(text, requested=True, enabled=True, native_fallback=True)
    motion_mode = verify_motion_mode(text, configured=True)
    admission = verify_admission(text, requested=True, final_count=1, device_count=2)
    assert admission['final_vetoes'] & 2, 'Failed adoption must veto the unobserved native escape'
    assert geometry['scoped_geometry_records'] > 0, 'Fallback captured draw geometry missing'
    assert sum(line.startswith('device_hooked ') for line in text.splitlines()) == 2
    assert sum(line.startswith('device_destroy ') for line in text.splitlines()) == 2
    assert all(sha(directory / name) == digest for name, digest in local_hashes.items()), 'Fallback executable/DLL changed during run'
    assert manifest['sources'] == sources() and manifest['binaries_at_end'] == binaries(), 'Inputs changed during fallback verification'
    assert all(sha(root / path) == digest for path, digest in object_hashes.items()), 'Proxy link inputs changed during fallback verification'
    report = {'bottle': bottle.describe(), 'sources_before_and_after': manifest['sources'], 'binaries_before_and_after': manifest['binaries_at_end'], 'production_object_hashes': object_hashes, 'result': 'PASS', 'fault': 'wrap_factory E_OUTOFMEMORY without consuming native ref', 'production_objects': [str(p.relative_to(object_root)) for p in objects], 'production_generated_inputs': [str(p.relative_to(root)) for p in (*generated, runtime)], 'verification_dll_sha256': sha(directory / 'd3d9.dll'), 'production_dll_sha256': sha(root / 'build-ownership/d3d9.dll'), 'trace_sha256': sha(trace), 'stub_sha256': sha(root / 'verification/probe/ownership_integration_fallback_stub.cpp'), 'installed': False, 'finite_requested': True, 'geometry': geometry, 'motion_mode': motion_mode, 'admission': admission, 'local_binaries_before_and_after': local_hashes}
    (results / 'ownership-integration-fallback.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
