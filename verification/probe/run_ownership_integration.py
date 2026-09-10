#!/usr/bin/env python3
"""Fresh-build and test the opt-in DLL in isolated Preview directories, never X3."""
from pathlib import Path
import datetime
import hashlib
import json
import os
import shutil
import subprocess

root = Path(__file__).resolve().parents[2]
probe = root / 'verification/probe/build'
results = root / 'verification/results'
wine = '/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine'
dll = root / 'build-ownership/d3d9.dll'
fixtures = [('smoke', 'd3d9_smoke.exe', '1'), ('capture', 'capture_state_fixture.exe', '5'),
            ('lifetime', 'ownership_integration_lifetime.exe', '0'),
            ('contracts', 'ownership_integration_baseline.exe', '0'),
            ('auto', 'ownership_integration_auto_depth.exe', '0')]
MODES = ('off', 'on', 'depth_only', 'copy_depth', 'scene_depth', 'scene_only',
         'object_requested', 'finite_on', 'finite_without_ownership')

def selected_fixtures(mode):
    if mode in ('depth_only', 'scene_only', 'object_requested'):
        return fixtures[:1]
    if mode in ('copy_depth', 'scene_depth'):
        return fixtures[-1:]
    if mode == 'finite_on':
        return fixtures[:2]
    if mode == 'finite_without_ownership':
        return fixtures[1:2]
    return fixtures

def require_no_game():
    inventory = subprocess.run(['pgrep', '-ifl', '[X]3AP[.]exe'], capture_output=True, text=True)
    if inventory.returncode != 1 or inventory.stdout.strip():
        raise RuntimeError('Game running or process inventory failed; postpone GPU verification: ' +
                           inventory.stdout + inventory.stderr)



def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def sources():
    paths = [p for folder in ('src/ownership', 'src/proxy', 'src/renderer', 'src/temporal', 'cmake')
             for p in (root / folder).glob('*') if p.suffix in ('.cpp', '.h', '.hlsl', '.def', '.cmake')]
    paths += [root / 'CMakeLists.txt']
    fixture_inputs = ('capability_probe.cpp', 'd3d9_smoke.cpp', 'capture_state_fixture.cpp', 'abi_check.cpp',
                      'ownership_fixture.cpp', 'ownership_integration_lifetime.cpp', 'ownership_integration_auto_depth.cpp',
                      'ownership_integration_fallback_stub.cpp', 'build.sh', 'build_ownership.sh', 'build_ownership_integration.sh',
                      'run_ownership_integration.py', 'run_ownership_integration_fallback.py', 'verify_ownership_integration.py',
                      'verify_capture_state.py')
    paths += [root / 'verification/probe' / name for name in fixture_inputs]
    paths += [root / 'tools/analysis/summarize_capture.py']
    return {str(p.relative_to(root)): sha(p) for p in sorted(paths)}


def binaries():
    return {str(p.relative_to(root)): sha(p) for p in [dll, *(probe / exe for _, exe, _ in fixtures)]}


def main():
    results.mkdir(exist_ok=True)
    manifest = {'result': 'RUNNING', 'runtime': wine, 'bottle': 'Steam', 'game_launched': False,
                'cases': {}}
    manifest_path = results / 'ownership-integration-build.json'
    save = lambda: manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
    save()
    # Clean CMake compilation and fresh fixture compilation prevent an old binary
    # from being paired with hashes of newer source files.
    commands = [
        ['cmake', '-S', '.', '-B', 'build-ownership', '-DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake', '-DCMAKE_BUILD_TYPE=Release'],
        ['cmake', '--build', 'build-ownership', '--clean-first'],
        ['sh', 'verification/probe/build.sh'],
        ['sh', 'verification/probe/build_ownership_integration.sh'],
    ]
    manifest['build_commands'] = commands
    try:
        require_no_game()
        manifest['sources_before_build'] = sources()
        save()
        with (results / 'ownership-integration-build.log').open('w') as output:
            for command in commands:
                subprocess.run(command, cwd=root, check=True, stdout=output, stderr=subprocess.STDOUT)
        manifest['sources_at_start'] = sources()
        assert manifest['sources_before_build'] == manifest['sources_at_start'], 'Sources changed during compilation'
        manifest['binaries_at_start'] = binaries()
        manifest['dll_sha256'] = sha(dll)
        save()
        for mode in MODES:
            selected = selected_fixtures(mode)
            for name, exe, frames in selected:
                if mode == 'scene_depth':
                    frames = '1'  # Exercise the requested-frame gate, not only allocation.
                case = f'ownership-integration-{mode}-{name}'
                directory = probe / (case + '-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f'))
                assert binaries() == manifest['binaries_at_start'], 'Built binary changed during run'
                directory.mkdir(parents=True)
                shutil.copy(probe / exe, directory)
                shutil.copy(dll, directory / 'd3d9.dll')
                env = dict(os.environ, X3M_TELEMETRY='1', X3M_CAPTURE_START='1', X3M_CAPTURE_FRAMES=frames,
                           X3M_DEPTH_COPY='1' if mode in ('depth_only', 'copy_depth', 'scene_depth') else '0',
                           X3M_SCENE_DEPTH_CAPTURE='1' if mode in ('scene_depth', 'scene_only') else '0',
                           X3M_OBJECT_TRACE='1' if mode == 'object_requested' else '0',
                           X3M_OBJECT_LIFETIME='1' if mode == 'object_requested' else '0',
                           X3M_MESH_CACHE='0',
                           X3M_FINITE_POSITIONS='1' if mode in ('finite_on', 'finite_without_ownership') else '0')
                if mode in ('on', 'copy_depth', 'scene_depth', 'finite_on'):
                    env['X3M_OWNERSHIP'] = '1'
                else:
                    env.pop('X3M_OWNERSHIP', None)
                command = [wine, '--bottle', 'Steam', '--no-update', '--dll', 'd3d9=n,b',
                           '--workdir', str(directory), str(directory / exe)]
                stdout_path = results / (case + '.txt')
                trace_path = results / (case + '-capture.log')
                trace_path.unlink(missing_ok=True)
                with stdout_path.open('w') as stdout, (results / (case + '-wine.log')).open('w') as stderr:
                    require_no_game()
                    completed = subprocess.run(command, env=env, stdout=stdout, stderr=stderr, timeout=120)
                traces = list((directory / 'x3-modern-captures').glob('session-*.log'))
                assert traces, f'{case}: missing current-run capture log'
                shutil.copy(max(traces, key=lambda p: p.stat().st_mtime), trace_path)
                manifest['cases'][case] = {'exit': completed.returncode, 'exe_sha256': sha(directory / exe),
                    'dll_sha256': sha(directory / 'd3d9.dll'), 'report_sha256': sha(stdout_path), 'trace_sha256': sha(trace_path)}
                save()
                print(f'{case}: exit={completed.returncode}', flush=True)
                assert completed.returncode == 0, f'{case}: nonzero exit'
        manifest['sources'] = sources()
        manifest['binaries_at_end'] = binaries()
        manifest['source_tree_unchanged_during_run'] = manifest['sources_at_start'] == manifest['sources']
        manifest['binaries_unchanged_during_run'] = manifest['binaries_at_start'] == manifest['binaries_at_end']
        assert manifest['source_tree_unchanged_during_run'], 'Sources changed during run'
        assert manifest['binaries_unchanged_during_run'], 'Binaries changed during run'
        manifest['result'] = 'PASS'
    except (Exception, KeyboardInterrupt) as exc:
        manifest['result'] = 'FAIL'
        manifest['reason'] = str(exc)
        raise
    finally:
        save()


if __name__ == '__main__':
    main()
