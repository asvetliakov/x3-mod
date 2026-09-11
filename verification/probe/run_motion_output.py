#!/usr/bin/env python3
"""Fresh-build live motion route integration through the actual DLL; no game launch.

Four runs of one original synthetic device program under CrossOver Preview
Wine with a process-local d3d9 override: the production build/d3d9.dll with the
route off and on (fill, exact restoration, Reset with RT1 owned, capture-frame
readback, device release), and a seam DLL (production objects + capture.cpp and
motion_output.cpp compiled with X3M_MOTION_OUTPUT_FIXTURE) off and on, which
also exercises scene recognition, variant routing, history, sentinel-only mode
and state block resynchronization against a CPU oracle. Reviewed shader bytes
are read from local files and never enter the repository or the reports.
"""
from pathlib import Path
import datetime
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[2]
PROBE = ROOT / 'verification/probe'
BUILD = PROBE / 'build'
RESULTS = ROOT / 'verification/results'
EXE = BUILD / 'motion_output_fixture.exe'
SEAM = BUILD / 'motion-output-seam/d3d9.dll'
DLL = ROOT / 'build/d3d9.dll'
WINE = Path('/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine')
RAW = {
    Path('/tmp/x3-shader-sweep/programs/vs_53a0a641107ed76c.bin'): 'bc402d1c2bfbbcb9fedd98890db845dab2a24da8cfb5a88a74c4eafa40f7a50c',
    Path('/tmp/x3-shader-sweep/programs/ps_8759c7838bbc86c2.bin'): '9fd15484fe419295cfb3534bd4f978efc8855c1e3e6a06e776533497dad48dc0'}
CASES = [('production-off', 'production', '0'), ('production-on', 'production', '1'),
         ('seam-off', 'seam', '0'), ('seam-on', 'seam', '1')]
# Expected per-frame route counters for captured frames 1..8 of the seam-on
# script (see motion_output_fixture.cpp run()). gate2 counts the background draw.
SEAM_FRAMES = {
    1: dict(draws=3, routed=2, matched=2, gate2=1, gate3=0, gate4=0, gate5=0, gate6=0),
    2: dict(draws=3, routed=2, matched=0, gate2=1, gate3=0, gate4=0, gate5=2, gate6=0),
    3: dict(draws=3, routed=2, matched=0, gate2=1, gate3=0, gate4=0, gate5=0, gate6=2),
    4: dict(draws=5, routed=2, matched=2, gate2=1, gate3=1, gate4=1, gate5=0, gate6=0),
    5: dict(draws=4, routed=3, matched=2, gate2=1, gate3=0, gate4=0, gate5=0, gate6=1),
    6: dict(draws=3, routed=2, matched=1, gate2=1, gate3=0, gate4=0, gate5=0, gate6=1),
    7: dict(draws=3, routed=1, matched=1, gate2=1, gate3=1, gate4=0, gate5=0, gate6=0),
    8: dict(draws=3, routed=2, matched=1, gate2=1, gate3=0, gate4=0, gate5=0, gate6=1)}
SENTINEL = (0.0, 0.0, 0.0, -1.0)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fields(line):
    return dict(re.findall(r'(\w+)=([^\s]+)', line))


def sources():
    paths = [p for folder in ('src/proxy', 'src/renderer', 'src/ownership', 'src/temporal', 'cmake')
             for p in (ROOT / folder).glob('*') if p.suffix in ('.cpp', '.h', '.hlsl', '.def', '.cmake')]
    paths += [ROOT / 'CMakeLists.txt', PROBE / 'motion_output_fixture.cpp', PROBE / 'build_motion_output.sh',
              PROBE / 'run_motion_output.py', PROBE / 'abi_check.cpp']
    return {str(p.relative_to(ROOT)): sha(p) for p in sorted(paths)}


def no_game():
    p = subprocess.run(['pgrep', '-ifl', '[X]3AP[.]exe'], capture_output=True, text=True)
    assert p.returncode == 1 and not p.stdout.strip(), 'X3AP running; no synthetic GPU run'


def read_motion(path, width=64, height=64):
    data = path.read_bytes()
    assert len(data) == width * height * 16, f'{path}: unexpected size {len(data)}'
    return [struct.unpack_from('<4f', data, i * 16) for i in range(width * height)]


def validate_case(name, mode, enabled, text, trace, directory):
    lines = text.splitlines()
    assert lines and lines[-1].startswith('RESULT PASS '), f'{name}: fixture did not pass'
    assert 'FAIL' not in text and text.count('RESULT ') == 1, f'{name}: failures reported'
    terminal = fields(lines[-1])
    seam = mode == 'seam'
    live = seam and enabled
    assert fields([l for l in lines if l.startswith('MODE ')][0]) == {'seam': str(int(seam)), 'enabled': str(int(enabled)),
                                                                        'dll': fields([l for l in lines if l.startswith('MODE ')][0])['dll']}
    assert int(terminal['checks']) == (30 if live else 6), (name, terminal)
    assert int(terminal['restorations']) == 39 and int(terminal['frames']) == 12, (name, terminal)
    assert text.count('RESET PASS') == 1
    restores = [fields(l) for l in lines if l.startswith('RESTORE ')]
    assert len(restores) == 39 and all(r['differences'] == '0' for r in restores), f'{name}: restoration differences'
    colors = {int(fields(l)['frame']): fields(l)['hash'] for l in lines if l.startswith('COLOR ')}
    assert sorted(colors) == list(range(12)), f'{name}: color inventory'
    motion = [fields(l) for l in lines if l.startswith('MOTION ')]
    result = {'mode': mode, 'enabled': enabled, 'checks': int(terminal['checks']), 'restorations': 39, 'frames': 12,
              'color_hashes': colors, 'motion_pixels': int(terminal['motion_pixels']),
              'matched_pixels': int(terminal['matched_pixels']), 'max_uv_error_pixels': float(terminal['max_uv_pixels']),
              'max_previous_depth_error': float(terminal['max_depth_error'])}
    if live:
        assert len(motion) == 12 and all(m['mismatches'] == '0' and int(m['checked']) > 3000 for m in motion), f'{name}: oracle'
        assert result['motion_pixels'] > 40000 and result['matched_pixels'] > 10000
        assert result['max_uv_error_pixels'] <= .01 and result['max_previous_depth_error'] <= 4e-6
        assert sum(int(m['matched']) for m in motion if int(m['frame']) in (0, 2, 3, 9)) == 0, 'sentinel-only frames carried motion'
    else:
        assert not motion and result['motion_pixels'] == 0
    # Capture log: mode line, device gate, variants, target lifecycle, frames, readback.
    tl = trace.splitlines()
    assert sum(l.startswith('device_hooked ') for l in tl) == sum(l.startswith('device_destroy ') for l in tl) == 1
    modes = [fields(l) for l in tl if l.startswith('motion_output_mode ')]
    assert len(modes) == 1 and modes[0]['requested'] == str(int(enabled)) and modes[0]['temporal_consumer'] == '0'
    devices = [fields(l) for l in tl if l.startswith('motion_output_device ')]
    variants = [fields(l) for l in tl if l.startswith('motion_output_variant ')]
    targets = [l for l in tl if l.startswith('motion_output_target ')]
    frames = {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('motion_output_frame ')}
    readbacks = {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('motion_output_readback ')}
    routes = [fields(l) for l in tl if l.startswith('motion_route ')]
    if not enabled:
        assert not devices and not variants and not targets and not frames and not readbacks and not routes, f'{name}: disabled route logged activity'
        assert not any(l.startswith('motion_output_release ') for l in tl)
        return result
    assert len(devices) == 1 and devices[0]['enabled'] == '1' and devices[0]['reason'] == 'ok', (name, devices)
    assert devices[0]['history_available'] == '0', 'synthetic process must not claim game observers'
    assert devices[0]['detail'] == 'stage=compare' and 'color_errors=0' in trace and 'motion_errors=0' in trace
    assert [(v['kind'], v['transform'], v['create']) for v in variants] == [('vs', '0', '00000000'), ('ps', '0', '00000000')] * 2, (name, variants)
    assert all(v['original'] in ('53a0a641107ed76c', '8759c7838bbc86c2') for v in variants)
    assert len(targets) == 2 and all('create=00000000 level=00000000' in t for t in targets), 'target created at first latch and after Reset'
    assert 'motion_output_reset device=1 result=00000000 generation=2' in trace
    assert sum(l.startswith('motion_output_release ') for l in tl) == 1, 'owned objects released before the final device Release'
    assert not any(l.startswith(('motion_output_fill_failed', 'motion_output_apply_failed', 'motion_output_restore_failed')) for l in tl)
    assert sorted(frames) == list(range(0, 9)), (name, sorted(frames))  # frame 0 via telemetry, 1..8 via capture
    for frame, summary in frames.items():
        assert summary['latched'] == summary['filled'] == '1' and summary['fill_result'] == summary['fill_restore'] == '00000000'
        assert summary['apply_failures'] == summary['restore_failures'] == '0' and summary['committed'] == '1'
        assert summary['present'] == '00000000'
        if live and frame in SEAM_FRAMES:
            got = {k: int(summary[k]) for k in SEAM_FRAMES[frame]}
            assert got == SEAM_FRAMES[frame], (name, frame, got, SEAM_FRAMES[frame])
            assert summary['selector_state'] == '2', 'seam frames end inside the Scene phase'
        elif not live:
            assert summary['routed'] == summary['matched'] == '0' and int(summary['gate2']) == int(summary['draws'])
            assert summary['selector_state'] == '9', 'default signatures must reject the synthetic background'
    assert sorted(readbacks) == list(range(1, 9)), (name, sorted(readbacks))
    for frame, r in readbacks.items():
        assert r['result'] == '00000000' and r['bytes'] == '65536' and r['width'] == r['height'] == '64'
        pixels = read_motion(directory / 'x3-modern-captures' / r['file'])
        valid = sum(p[3] == 1.0 for p in pixels)
        sentinel = sum(p == SENTINEL for p in pixels)
        assert valid + sentinel == len(pixels), f'{name}: frame {frame} readback holds values outside the ABI'
        if live and frame in (1, 4):
            assert valid > 2000, f'{name}: frame {frame} readback lacks matched motion'
        elif not live or frame in (2, 3):
            assert sentinel == len(pixels), f'{name}: frame {frame} readback is not all sentinel'
    # Per-draw decisions logged in capture frames must agree with the fixture's script.
    expects = [fields(l) for l in lines if l.startswith('EXPECT ') and 1 <= int(fields(l)['frame']) <= 8]
    assert len(routes) == len(expects) == sum(SEAM_FRAMES[f]['draws'] - 1 for f in SEAM_FRAMES) if live else True
    if live:
        for e in expects:
            match = [r for r in routes if r['frame'] == e['frame'] and r['index'] == e['index']]
            assert len(match) == 1 and match[0]['routed'] == e['routed'] and match[0]['matched'] == e['matched'], (name, e, match)
            assert match[0]['result'] == '00000000'
    else:
        assert not routes, f'{name}: production DLL must not reach the scene phase'
    result.update(variants=len(variants), frames_logged=len(frames), readbacks=len(readbacks), routes=len(routes))
    return result


def main():
    RESULTS.mkdir(exist_ok=True)
    summary_path = RESULTS / 'motion-output-summary.json'
    report_path = RESULTS / 'motion-output.txt'
    result = {'passed': False, 'status': 'RUNNING', 'game_launched': False,
              'scope': 'Live same-draw route (checkpoint B1) through the actual proxy DLL with one original synthetic device program; seam DLL adds fixture scope/signature injection. Not gameplay validation.',
              'cases': {}}
    save = lambda: summary_path.write_text(json.dumps(result, indent=2) + '\n')
    save()
    try:
        no_game()
        assert WINE.is_file(), 'CrossOver Preview Wine missing'
        assert all(p.is_file() for p in RAW), 'Local reviewed shader files missing under /tmp/x3-shader-sweep/programs; skipping is not a pass'
        assert all(sha(p) == h for p, h in RAW.items()), 'Local shader bytes differ from the reviewed pair'
        result['local_inputs'] = {str(p): h for p, h in RAW.items()}
        result['sources_before_build'] = sources()
        commands = [['cmake', '-S', '.', '-B', 'build', '-DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake', '-DCMAKE_BUILD_TYPE=RelWithDebInfo'],
                    ['cmake', '--build', 'build', '--clean-first', '-j4'],
                    ['i686-w64-mingw32-g++', '-std=c++17', '-Wall', '-Wextra', '-c', 'verification/probe/abi_check.cpp', '-o', 'verification/probe/build/abi_check.o'],
                    ['sh', 'verification/probe/build_motion_output.sh']]
        result['build_commands'] = commands
        with (RESULTS / 'motion-output-build.log').open('w') as out:
            for command in commands:
                subprocess.run(command, cwd=ROOT, check=True, stdout=out, stderr=subprocess.STDOUT)
        assert sources() == result['sources_before_build'], 'Sources changed during build'
        result['binaries'] = {str(p.relative_to(ROOT)): sha(p) for p in (EXE, SEAM, DLL)}
        save()
        report = []
        wine_log = (RESULTS / 'motion-output-wine.log').open('w')
        for name, mode, enabled in CASES:
            directory = BUILD / ('motion-output-' + name + '-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f'))
            directory.mkdir(parents=True)
            shutil.copy(EXE, directory)
            shutil.copy(SEAM if mode == 'seam' else DLL, directory / 'd3d9.dll')
            env = dict(os.environ, X3M_MOTION_OUTPUT=enabled, X3M_CAPTURE_START='1', X3M_CAPTURE_FRAMES='8', X3M_TELEMETRY='1',
                       X3M_DEPTH_COPY='0', X3M_SCENE_DEPTH_CAPTURE='0', X3M_OBJECT_TRACE='0', X3M_OBJECT_LIFETIME='0',
                       X3M_MESH_CACHE='0', X3M_ADMISSION='0', X3M_FINITE_POSITIONS='0', X3M_MOTION_CAPTURE='0')
            env.pop('X3M_OWNERSHIP', None)
            command = [str(WINE), '--bottle', 'Steam', '--no-update', '--dll', 'd3d9=n,b', '--workdir', str(directory),
                       str(directory / EXE.name)] + ['Z:' + str(p) for p in RAW] + [mode]
            no_game()
            wine_log.write(f'==== {name}\n'); wine_log.flush()
            completed = subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=wine_log, text=True, timeout=240)
            text = completed.stdout
            report.append(f'==== {name} exit={completed.returncode}\n{text}')
            traces = list((directory / 'x3-modern-captures').glob('session-*.log'))
            assert completed.returncode == 0 and len(traces) == 1, f'{name}: exit {completed.returncode}, traces {len(traces)}'
            trace = traces[0].read_text()
            case = validate_case(name, mode, enabled == '1', text, trace, directory)
            case.update(exit=completed.returncode, directory=str(directory.relative_to(ROOT)), trace_sha256=sha(traces[0]),
                        dll_sha256=sha(directory / 'd3d9.dll'), exe_sha256=sha(directory / EXE.name))
            if enabled == '1':
                shutil.copy(traces[0], RESULTS / f'motion-output-{name}-capture.log')
            result['cases'][name] = case
            save()
            print(f'{name}: exit={completed.returncode} checks={case["checks"]} motion_pixels={case["motion_pixels"]}', flush=True)
        wine_log.close()
        for mode in ('production', 'seam'):
            off, on = result['cases'][mode + '-off']['color_hashes'], result['cases'][mode + '-on']['color_hashes']
            assert off == on, f'{mode}: color differs between route off and on'
        result['color_identical_off_vs_on'] = True
        report_path.write_text(''.join(report))
        result['report_sha256'] = sha(report_path)
        assert sources() == result['sources_before_build'], 'Sources changed during run'
        assert all(sha(p) == h for p, h in RAW.items()), 'Local shader bytes changed during run'
        assert {str(p.relative_to(ROOT)): sha(p) for p in (EXE, SEAM, DLL)} == result['binaries'], 'Binaries changed during run'
        result['sources_after_run'] = sources()
        result['limits'] = ['Synthetic device program; not gameplay validation, TAA or temporal consumption.',
                            'Object scope is injected through the fixture seam; the game observers are not exercised here.',
                            'CrossOver Preview builtin D3D9 only; Windows is cross-compiled, not verified.']
        result['passed'] = True; result['status'] = 'PASS'
    except BaseException as error:
        result['status'] = 'FAIL'; result['error'] = repr(error)
        raise
    finally:
        save()
        print(json.dumps({k: v for k, v in result.items() if k in ('status', 'passed', 'error')}))


if __name__ == '__main__':
    main()
