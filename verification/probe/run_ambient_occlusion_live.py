#!/usr/bin/env python3
"""Ambient occlusion at the live scene-end hook (docs/architecture/ambient-occlusion.md, step 2).

Runs the motion-output fixture's `aohook` script (verification/probe/motion_output_fixture.cpp)
against the fixture-seam DLL built by build_motion_output.sh, once per twin, and
writes <results>/ambient-occlusion-live1.json. Twins: ao-on (multiply law on the
crease frames, identity on the flat frames), ao-off (bit-identical crease frames,
no AO lines), ao-fault (X3M_FIXTURE_AO_FAULT=attach: the shader-model gate refuses,
frames bit-identical with AO off), ao-debug (the grayscale factor view), ao-hdr
(attach and identity on the FP16 target). Every twin parses the DLL's
ambient_occlusion_device / ambient_occlusion_frame / scene_end_marker lines.
Run through wine_lock.py with X3M_FIXTURE_BOTTLE=X3. Never launches the game.
"""
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys

import bottle  # CrossOver bottle selection (X3M_FIXTURE_BOTTLE) and the per-bottle results directory

ROOT = Path(__file__).resolve().parents[2]
PROBE = ROOT / 'verification/probe'
EXE = PROBE / 'build/motion_output_fixture.exe'
SEAM = PROBE / 'build/motion-output-seam/d3d9.dll'
RAW = [Path('/tmp/x3-shader-sweep/programs/vs_53a0a641107ed76c.bin'), Path('/tmp/x3-shader-sweep/programs/ps_8759c7838bbc86c2.bin')]
SOURCES = ('src/proxy/motion_output.cpp', 'src/proxy/motion_output.h', 'src/proxy/capture.cpp',
           'src/renderer/ambient_occlusion_pass.cpp', 'src/renderer/ambient_occlusion_pass.h',
           'verification/probe/motion_output_fixture.cpp', 'verification/probe/build_motion_output.sh',
           'verification/probe/run_ambient_occlusion_live.py')
FRAMES = {'default': 8, 'debug': 5, 'toggle': 7}                 # aohook script frames per twin
PIXEL_FRAMES = {'default': (3, 6), 'debug': (0, 3), 'toggle': (5,)}  # frames without history whose pixel law is checked
# The first frame after each Reset in the aohook script. A Reset gives AO a
# fresh attach: these frames must re-attach and run, not wait out the
# re-attach hysteresis (the defect candidate 740a6dd7 caught).
RESET_FRAMES = {'default': (3, 6), 'debug': (3,), 'toggle': (3, 5)}
TOGGLE_ENABLED = [1, 1, 1, 0, 0, 1, 1]                            # the toggle twin's per-frame enable
FORMAT_A8R8G8B8, FORMAT_A16B16G16R16F = 21, 113
# The twins. hdr: the FP16 route. fault: the attach seam. debug: the grayscale view.
CASES = [dict(name='ao-on', ao=1), dict(name='ao-off', ao=0), dict(name='ao-fault', ao=1, fault='attach'),
         dict(name='ao-debug', ao=1, debug=True), dict(name='ao-hdr', ao=1, hdr=True),
         dict(name='ao-toggle', ao=1, toggle=True), dict(name='ao-pollfault', ao=1, fault='poll')]


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def fields(line):
    out = {}
    for key, value in re.findall(r'(\w+)=(\S+)', line):
        try:
            out[key] = int(value) if re.fullmatch(r'-?\d+', value) else float(value)
        except ValueError:
            out[key] = value
    return out


def parse_fixture(text):
    """The fixture's stdout: the HOOK line, the AO_CREASE lines, violations, the RESULT line."""
    report = {'hook': None, 'crease': [], 'violations': [], 'result': None, 'checks': 0, 'restorations': 0}
    for line in text.splitlines():
        if line.startswith('HOOK '):
            report['hook'] = fields(line)
        elif line.startswith('AO_CREASE '):
            report['crease'].append(fields(line))
        elif line.startswith('AO_VIOLATION '):
            report['violations'].append(fields(line))
        elif line.startswith('RESULT '):
            report['result'] = line.split(' ', 2)[1]
            report.update({k: fields(line).get(k, 0) for k in ('checks', 'restorations')})
    return report


def parse_trace(text):
    """The DLL session log: the AO device/frame lines, the timing refusal, the scene-end markers."""
    report = {'device': [], 'frames': [], 'markers': [], 'failed': [], 'toggles': [], 'timing_unavailable': False, 'timing_lost': 0, 'mode': None}
    for line in text.splitlines():
        if 'ambient_occlusion_device ' in line:
            report['device'].append(fields(line))
        elif 'ambient_occlusion_frame ' in line:
            report['frames'].append(fields(line))
        elif 'scene_end_marker ' in line:
            report['markers'].append(fields(line))
        elif 'ambient_occlusion_failed ' in line:
            report['failed'].append(fields(line))
        elif 'ambient_occlusion_timing ' in line and 'queries=unavailable' in line:
            report['timing_unavailable'] = True
        elif 'ambient_occlusion_timing ' in line and 'queries=lost' in line:
            report['timing_lost'] += 1
        elif 'ambient_occlusion_toggle ' in line:
            report['toggles'].append(fields(line))
        elif 'ambient_occlusion_mode ' in line:
            report['mode'] = fields(line)
    return report


def validate_case(case, fixture, trace):
    """One twin's verdict: raises AssertionError with the first failed expectation."""
    name = case['name']
    ao, fault, debug, hdr, toggle = case.get('ao', 0), case.get('fault'), case.get('debug', False), case.get('hdr', False), case.get('toggle', False)
    active = ao and fault != 'attach'
    script = 'debug' if debug else 'toggle' if toggle else 'default'
    assert fixture['result'] == 'PASS', f'{name}: fixture result {fixture["result"]}'
    assert fixture['hook'] and fixture['hook'].get('installed') == 1, f'{name}: hook not installed'
    assert not fixture['violations'], f'{name}: pixel-law violations {fixture["violations"][:2]}'
    crease = fixture['crease']
    pixel = [c for c in crease if 'darkened' in c]
    expected_pixel = 0 if hdr else len(PIXEL_FRAMES[script])
    assert len(pixel) == expected_pixel, f'{name}: {len(pixel)} pixel-law frames, expected {expected_pixel}'
    law = 'identity' if not active else 'debug' if debug else 'multiply'
    assert all(c['law'] == law for c in crease), f'{name}: crease law {[c["law"] for c in crease]} != {law}'
    assert [c['frame'] for c in pixel] == list(PIXEL_FRAMES[script]) if not hdr else True, (name, [c['frame'] for c in pixel])
    for c in pixel:
        assert c['violations'] == 0 and c['sentinel'] > 0, (name, c)
        if active:
            assert c['darkened'] > 0 and c['centre_mean'] > c['outer_mean'], (name, c)
        else:
            assert c['darkened'] == 0 and c['changed'] == 0, (name, c)
    frames_expected = FRAMES[script]
    summary = {'frames': frames_expected, 'checks': fixture['checks'], 'restorations': fixture['restorations'], 'law': law,
               'pixel_frames': [{k: c[k] for k in ('frame', 'darkened', 'sentinel', 'max_drop', 'centre_mean', 'outer_mean', 'changed')} for c in pixel]}
    # The DLL side.
    frames = trace['frames']
    if not ao:
        assert not frames and not trace['device'], f'{name}: AO lines with the switch off'
        summary['ao_lines'] = 0
        return summary
    assert trace['mode'] and trace['mode'].get('enabled') == 1, (name, trace['mode'])
    assert len(frames) == frames_expected, f'{name}: {len(frames)} ambient_occlusion_frame lines, expected {frames_expected}'
    assert [f['frame'] for f in frames] == list(range(frames_expected)), (name, [f['frame'] for f in frames])
    for f in frames:
        for key in ('attached', 'ran', 'reason', 'gpu_us', 'cpu_us', 'width', 'height', 'radius_px', 'enabled', 'source', 'gpu_timing'):
            assert key in f, (name, key, f)
    devices = trace['device']
    expected_enabled = TOGGLE_ENABLED if toggle else [1] * frames_expected
    # One attach per device lifetime plus one per Reset that is followed by an
    # enabled frame: the hysteresis holds within a lifetime, a Reset clears it.
    reset_frames = [f for f in RESET_FRAMES[script] if expected_enabled[f]]
    expected_attaches = 1 + len(reset_frames)
    assert len(devices) >= 1, f'{name}: no ambient_occlusion_device line'
    assert len(devices) == expected_attaches, f'{name}: {len(devices)} attaches, expected {expected_attaches} (one per format, one per Reset)'
    assert [f['enabled'] for f in frames] == expected_enabled, (name, [f['enabled'] for f in frames])
    assert [t['enabled'] for t in trace['toggles']] == ([0, 1] if toggle else []), (name, trace['toggles'])
    # Flat frame 1 signals before the scene: the chain runs at the bloom copy.
    expected_source = ['hook'] * frames_expected
    if not debug:
        expected_source[1] = 'copy'
    assert [f['source'] for f in frames] == expected_source, (name, [f['source'] for f in frames])
    if fault == 'attach':
        assert all(d['attached'] == 0 and d['reason'] == 'ps_3_0' for d in devices), (name, devices)
        assert all(f['attached'] == 0 and f['ran'] == 0 and f['reason'] == 'attach' for f in frames), (name, frames[0])
        assert not trace['failed'], (name, trace['failed'])
        summary.update(ao_lines=len(frames), attach_reason='ps_3_0', ran=0)
        return summary
    assert all(d['attached'] == 1 and d['reason'] == 'ok' for d in devices), (name, devices)
    expected_format = FORMAT_A16B16G16R16F if hdr else FORMAT_A8R8G8B8
    assert all(d['target_format'] == expected_format for d in devices), (name, [d['target_format'] for d in devices])
    # A Reset gives AO a fresh attach: the first enabled frame after each Reset
    # attaches and runs, it does not sit out the re-attach hysteresis.
    post_reset = [f for f in trace['frames'] if f['frame'] in reset_frames]
    assert [f['frame'] for f in post_reset] == reset_frames, (name, reset_frames, [f['frame'] for f in trace['frames']])
    assert all(f['attached'] == 1 and f['ran'] == 1 and f['applied'] == 1 and f['reason'] == 'ok' for f in post_reset), \
        f'{name}: post-Reset frames {reset_frames} must re-attach and run, got ' + repr([{k: f[k] for k in ("frame", "attached", "ran", "applied", "reason")} for f in post_reset])
    summary['post_reset_frames'] = reset_frames
    on = [f for f, e in zip(frames, expected_enabled) if e]
    off = [f for f, e in zip(frames, expected_enabled) if not e]
    assert all(f['attached'] == 1 and f['ran'] == 1 and f['reason'] == 'ok' and f['applied'] == 1 for f in on), (name, [f['reason'] for f in frames])
    assert all(f['ran'] == 0 and f['applied'] == 0 and f['reason'] == 'disabled' for f in off), (name, [f['reason'] for f in off])
    frames = on
    if fault == 'poll':
        # The forced poll failure released the sets before any issue: no crash, the
        # chain still ran, every line reports the lost state, one notice per set.
        assert trace['timing_lost'] >= 1 and all(f['gpu_timing'] == 'lost' and f['gpu_us'] == -1 for f in frames), (name, trace['timing_lost'], frames[0])
    else:
        assert all(f['gpu_timing'] in ('queries', 'unavailable') for f in frames), (name, [f['gpu_timing'] for f in frames])
    assert all(f['width'] == 64 and f['height'] == 64 and f['cpu_us'] > 0 for f in frames), (name, frames[0])
    assert all(f['debug'] == int(bool(debug)) for f in frames), (name, frames[0])
    assert not trace['failed'], (name, trace['failed'])
    gpu = [f['gpu_us'] for f in frames if f['gpu_us'] >= 0]
    if not trace['timing_unavailable'] and fault != 'poll':
        assert gpu, f'{name}: timestamp queries created but no pair completed'
    markers = trace['markers']
    assert markers and all(m['draw_index'] >= 1 for m in markers), (name, markers[:2])
    cpu = sorted(f['cpu_us'] for f in frames)
    summary.update(ao_lines=len(trace['frames']), enabled=expected_enabled, toggles=len(trace['toggles']), timing_lost=trace['timing_lost'],
                   gpu_timing_states=sorted({f['gpu_timing'] for f in trace['frames']}), sources=[f['source'] for f in trace['frames']],
                   ran=sum(f['ran'] for f in frames), applied=sum(f['applied'] for f in frames),
                   target_format=expected_format, slots=devices[0].get('slots'), attaches=len(devices), post_reset_frames=reset_frames,
                   cpu_us_median=cpu[len(cpu) // 2], cpu_us_min=cpu[0], gpu_us=gpu, gpu_us_median=sorted(gpu)[len(gpu) // 2] if gpu else None,
                   gpu_timing='timestamp' if gpu else 'unavailable', radius_px=frames[0]['radius_px'],
                   markers=[{'frame': m['frame'], 'draw_index': m['draw_index']} for m in markers])
    return summary


def environment(case):
    hdr = case.get('hdr', False)
    env = dict(os.environ, X3M_CAMERA='vanilla', X3M_CHASE_SCENE_FIX='0', X3M_CHASE_COMBAT_TIGHTNESS='0', X3M_MOTION_OUTPUT='1',
               X3M_MOTION_JITTER='1', X3M_MOTION_JITTER_SAMPLES='8', X3M_TAA='1', X3M_TAA_DEBUG='1', X3M_TAA_SHARPEN='0',
               X3M_CAPTURE_START='1', X3M_CAPTURE_FRAMES='8', X3M_TELEMETRY='1', X3M_TELEMETRY_DRAW='0',
               X3M_FIXTURE_CAMERA='rotate', X3M_TAA_SENTINEL='auto', X3M_FIXTURE_WRAP='0',
               X3M_MOTION_RT_MODE='perdraw', X3M_MOTION_FRAME_LOG='1', X3M_STATE_SHADOW='1', X3M_SCENE_HOOK='1',
               X3M_HDR='1' if hdr else '0', X3M_HDR_EXPOSURE='fixed', X3M_HDR_EV_MANUAL='', X3M_HDR_TONEMAP='identity', X3M_FIXTURE_HDR_FAULT='',
               X3M_OWNERSHIP='0', X3M_DEPTH_COPY='0', X3M_SCENE_DEPTH_CAPTURE='0', X3M_OBJECT_TRACE='0', X3M_OBJECT_LIFETIME='0',
               X3M_MESH_CACHE='0', X3M_ADMISSION='0', X3M_FINITE_POSITIONS='0', X3M_MOTION_CAPTURE='0',
               X3M_CRYPT_CACHE='0', X3M_LOADING_PROBES='0', X3M_MESH_ADJACENCY='native', X3M_MESH_ADJACENCY_DUMP='0',
               X3M_RESOURCE_READ='native', X3M_DAT_HANDLES='0', X3M_GZ_BUFFER='0', X3M_GZ_BUFFER_KB='256',
               X3M_LINEAR_MATERIALS='0', X3M_LINEAR_EMISSIONS='0', X3M_LINEAR_DISTANCE_FADE='0', X3M_SCREEN_EMISSION='0',
               X3M_AMBIENT_OCCLUSION=str(case.get('ao', 0)), X3M_AO_RADIUS='2', X3M_AO_STRENGTH='0.5',
               X3M_AO_DEBUG='1' if case.get('debug') else '0', X3M_AO_TIMING='1',
               X3M_FIXTURE_AO_FAULT=case.get('fault') or '', X3M_FIXTURE_AO_TOGGLE='1' if case.get('toggle') else '0')
    return env


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture', type=Path, default=EXE, help='motion_output_fixture.exe (build_motion_output.sh)')
    parser.add_argument('--seam', type=Path, default=SEAM, help='fixture-seam d3d9.dll (build_motion_output.sh)')
    parser.add_argument('--build', action='store_true', help='run build_motion_output.sh first (needs the CMake build/ objects)')
    parser.add_argument('--result', type=Path, default=None)
    parser.add_argument('cases', nargs='*', help='twin names; default all')
    args = parser.parse_args(argv)
    assert bottle.BOTTLE == 'X3', 'new qualification requires X3'
    result_path = args.result or bottle.results_dir(ROOT) / 'ambient-occlusion-live1.json'
    if args.build:
        subprocess.run(['sh', str(PROBE / 'build_motion_output.sh')], cwd=ROOT, check=True)
    inputs = [args.fixture.resolve(), args.seam.resolve(), *RAW]
    assert all(p.is_file() for p in inputs), f'inputs missing: {[str(p) for p in inputs if not p.is_file()]}'
    only = set(args.cases)
    build = PROBE / 'build'
    result = dict(passed=False, bottle=bottle.describe(), game_launched=False, script='aohook', frames_per_case=FRAMES,
                  inputs={str(p): sha(p) for p in inputs}, sources={s: sha(ROOT / s) for s in SOURCES}, cases={})
    try:
        for case in CASES:
            if only and case['name'] not in only:
                continue
            directory = build / ('ambient-occlusion-live-' + case['name'] + '-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f'))
            directory.mkdir(parents=True)
            shutil.copy(args.fixture, directory / 'fixture.exe'); shutil.copy(args.seam, directory / 'd3d9.dll')
            command = [bottle.WINE, *bottle.wine_args(), '--dll', 'd3d9=n,b', '--workdir', str(directory), str(directory / 'fixture.exe'),
                       *['Z:' + str(p) for p in RAW], 'aohook']
            with (directory / 'stdout.txt').open('w') as out, (directory / 'wine.log').open('w') as err:
                child = subprocess.run(command, env=environment(case), stdout=out, stderr=err, timeout=300)
            text = (directory / 'stdout.txt').read_text()
            traces = list((directory / 'x3-modern-captures').glob('session-*.log'))
            assert child.returncode == 0 and len(traces) == 1, f'{case["name"]}: exit {child.returncode}, traces {len(traces)}; {directory}'
            summary = validate_case(case, parse_fixture(text), parse_trace(traces[0].read_text()))
            summary['raw'] = str(directory)
            result['cases'][case['name']] = summary
            print(f'{case["name"]}: passed, {summary["frames"]} frames, checks={summary["checks"]} law={summary["law"]} ao_lines={summary["ao_lines"]}'
                  + (f' cpu_us_median={summary["cpu_us_median"]} gpu={summary["gpu_timing"]} gpu_us_median={summary["gpu_us_median"]}' if 'cpu_us_median' in summary else ''), flush=True)
        assert result['inputs'] == {str(p): sha(p) for p in inputs}, 'inputs changed during the run'
        result['passed'] = not only or set(result['cases']) == {c['name'] for c in CASES}
    finally:
        path = result_path if result['passed'] else build / 'ambient-occlusion-live-failed.json'
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(result, indent=1) + '\n')
        print(('PASS' if result['passed'] else 'FAIL') + f' result={path}', flush=True)
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
