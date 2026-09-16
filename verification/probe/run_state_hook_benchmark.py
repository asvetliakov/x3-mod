#!/usr/bin/env python3
"""Per-call cost of the proxy's hooked state setters under the real bottle; no game launch.

Four configurations of verification/probe/state_hook_benchmark.exe, each three
repetitions inside one process (medians reported):

  native            the backend d3d9 loaded directly (C:\\windows\\system32\\d3d9.dll),
                    no proxy in the process and no DLL override
  proxy-timing-off  the installed candidate d3d9.dll with X3M_FRAME_TIMING=0
  proxy-timing-on   the same DLL with X3M_FRAME_TIMING=1 (run87's launch)
  (each device case also times the application getters GetRenderState,
   GetSamplerState, GetTexture including the Release of the returned reference
   and GetVertexShaderConstantF(4), 1,000,000 calls each, and 200,000
   SetStreamSource + DrawIndexedPrimitive draw pairs inside one scene)

  primitives        QueryPerformanceCounter, an uncontended recursive_mutex
                    lock/unlock pair, a GetLastError/SetLastError pair and the
                    LightCallBoundary / CpuCallBoundary envelopes, with no D3D9

The proxy runs carry the route switches that install the setter hooks
(X3M_MOTION_OUTPUT=1 X3M_TAA=1 X3M_MOTION_JITTER=1: the render-state shadow,
and the mip bias which needs the texture and sampler hooks). Each run's SLOT
lines record which module owns the hooked vtable entries, so the record proves
the hooks were live. The DLL under test is the installed candidate, copied
read-only into the run directory; nothing is rebuilt.

Run it under the Wine lock:
  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py \\
      python3 verification/probe/run_state_hook_benchmark.py
"""
import datetime
import hashlib
import json
import os
import shutil
import statistics
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import bottle  # noqa: E402
from game_guard import game_running  # noqa: E402

PROBE = ROOT / 'verification/probe'
BUILD = PROBE / 'build'
EXE = BUILD / 'state_hook_benchmark.exe'
RESULTS = bottle.results_dir(ROOT)
WINE = Path(bottle.WINE)
INSTALLED_DLL = bottle.game_dir() / 'd3d9.dll'

# Unrelated features pinned off, as the other runners do, so an inherited host
# environment cannot change what the proxy installs.
BASE_ENV = dict(
    X3M_CAMERA='vanilla', X3M_CHASE_SCENE_FIX='0', X3M_CHASE_COMBAT_TIGHTNESS='0',
    X3M_OWNERSHIP='0', X3M_DEPTH_COPY='0', X3M_SCENE_DEPTH_CAPTURE='0', X3M_OBJECT_TRACE='0',
    X3M_OBJECT_LIFETIME='0', X3M_MESH_CACHE='0', X3M_ADMISSION='0', X3M_FINITE_POSITIONS='0',
    X3M_MOTION_CAPTURE='0', X3M_CRYPT_CACHE='0', X3M_LOADING_PROBES='0', X3M_MESH_ADJACENCY='native',
    X3M_MESH_ADJACENCY_DUMP='0', X3M_RESOURCE_READ='native', X3M_DAT_HANDLES='0',
    X3M_GZ_BUFFER='0', X3M_GZ_BUFFER_KB='256', X3M_HDR='0', X3M_LINEAR_MATERIALS='0',
    X3M_LINEAR_EMISSIONS='0', X3M_SCREEN_EMISSION='0')
# The route switches that install the setter hooks (capture.cpp: the shadow
# hooks need the route's capability gate, set_render_state the state shadow,
# set_texture/set_sampler_state the mip bias, which needs the jitter).
ROUTE_ENV = dict(X3M_MOTION_OUTPUT='1', X3M_TAA='1', X3M_TAA_DEBUG='0', X3M_MOTION_JITTER='1',
                 X3M_MOTION_RT_MODE='perdraw', X3M_STATE_SHADOW='1', X3M_SCENE_HOOK='0',
                 X3M_TELEMETRY='1', X3M_TELEMETRY_DRAW='0', X3M_CAPTURE_START='0',
                 X3M_CAPTURE_FRAMES='0', X3M_MOTION_FRAME_LOG='60')

CASES = [
    dict(name='primitives', argv=['primitives'], proxy=False, frame_timing=None),
    dict(name='native', argv=['device', 'native'], proxy=False, frame_timing=None),
    dict(name='proxy-timing-off', argv=['device', 'proxy'], proxy=True, frame_timing='0'),
    dict(name='proxy-timing-on', argv=['device', 'proxy'], proxy=True, frame_timing='1'),
]


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def fields(line):
    return dict(token.split('=', 1) for token in line.split()[1:] if '=' in token)


def run_case(entry, log):
    stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    directory = BUILD / f'state-hook-benchmark-{entry["name"]}-{stamp}'
    directory.mkdir(parents=True)
    shutil.copy(EXE, directory)
    env = dict(os.environ, **BASE_ENV)
    command = [str(WINE)] + bottle.wine_args() + ['--workdir', str(directory)]
    if entry['proxy']:
        shutil.copy(INSTALLED_DLL, directory / 'd3d9.dll')
        env.update(ROUTE_ENV)
        env['X3M_FRAME_TIMING'] = entry['frame_timing']
        command += ['--dll', 'd3d9=n,b']
    command += [str(directory / EXE.name)] + entry['argv']
    assert not game_running(), 'the game is running'
    log.write(f'==== {entry["name"]}\n')
    log.flush()
    completed = subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=log, text=True, timeout=1800)
    text = completed.stdout
    (directory / 'stdout.txt').write_text(text)
    assert completed.returncode == 0, f'{entry["name"]}: exit {completed.returncode}\n{text[-2000:]}'
    assert 'status=pass' in text, f'{entry["name"]}: {text[-2000:]}'
    samples, slots = {}, {}
    for line in text.splitlines():
        if line.startswith('BENCH '):
            f = fields(line)
            samples.setdefault(f['op'], []).append(float(f['ns_per_call']))
        elif line.startswith('SLOT '):
            f = fields(line)
            slots[f['name']] = Path(f['module'].replace('\\', '/')).name + (
                ' (proxy)' if entry['proxy'] and 'system32' not in f['module'].lower() else ' (backend)')
    case = {'directory': str(directory.relative_to(ROOT)),
            'command_tail': entry['argv'],
            'frame_timing': entry['frame_timing'],
            'ns_per_call': {op: round(statistics.median(values), 1) for op, values in samples.items()},
            'samples': {op: [round(v, 1) for v in values] for op, values in samples.items()},
            'hooked_slots': slots}
    if entry['proxy']:
        assert slots and all('proxy' in v for k, v in slots.items()
                             if k in ('SetRenderState', 'SetTexture', 'SetSamplerState',
                                      'SetVertexShaderConstantF', 'SetStreamSource')), slots
    return case


# The hooked setters the game calls per state write (light guard: PlainHookGuard
# + LightCallBoundary). SetStreamSource takes the heavy guard (HookGuard +
# CpuCallBoundary) and is a per-draw call, not a per-state-write call, so it is
# reported separately instead of carrying 1/6 of the mix.
LIGHT_SETTERS = ('SetRenderState', 'SetTexture', 'SetSamplerState', 'SetVertexShaderConstantF4')


def derive(result):
    """Add the per-call deltas and the attribution of run87's 297 ns state call."""
    cases = result['cases']
    native, off, on = (cases['native']['ns_per_call'], cases['proxy-timing-off']['ns_per_call'],
                       cases['proxy-timing-on']['ns_per_call'])
    for name in ('proxy-timing-off', 'proxy-timing-on'):
        cases[name]['ns_over_native'] = {op: round(value - native[op], 1)
                                         for op, value in cases[name]['ns_per_call'].items() if op in native}
    mean = lambda table: statistics.mean(table[op] for op in LIGHT_SETTERS)  # noqa: E731
    native_light, off_light, on_light = mean(native), mean(off), mean(on)
    primitives = cases['primitives']['ns_per_call']
    result['attribution'] = {
        'basis': 'mean of the four hooked light-guard setters; equal shares',
        'native_backend_call_ns': round(native_light, 1),
        'proxy_guard_and_shadow_ns': round(off_light - native_light, 1),
        'frame_timing_stamps_ns': round(on_light - off_light, 1),
        'proxy_total_with_frame_timing_ns': round(on_light, 1),
        'of_which_hook_mutex_ns': primitives['recursive_mutex_lock_unlock'],
        'of_which_cpu_envelope_light_ns': primitives['LightCallBoundary_envelope'],
        'of_which_qpc_pair_ns': round(2 * primitives['QueryPerformanceCounter'], 1),
        'heavy_guard_setter_ns': {'SetStreamSource_total_with_frame_timing': on.get('SetStreamSource'),
                                  'SetStreamSource_without_frame_timing': off.get('SetStreamSource'),
                                  'CpuCallBoundary_envelope': primitives['CpuCallBoundary_envelope']},
        'unhooked_control_ns': {'SetTextureStageState_native': native.get('SetTextureStageState'),
                                'SetTextureStageState_through_proxy': on.get('SetTextureStageState')},
        'equal_share_mix_ns': {'native': native['state_mix'], 'timing_off': off['state_mix'], 'timing_on': on['state_mix']},
        'getters_ns': {op: {'native': native.get(op), 'timing_off': off.get(op), 'timing_on': on.get(op)}
                       for op in ('GetRenderState', 'GetSamplerState', 'GetTexture_Release', 'GetVertexShaderConstantF4')},
        'draw_pair_ns': {'native': native.get('SetStreamSource_DrawIndexedPrimitive_pair'),
                         'timing_off': off.get('SetStreamSource_DrawIndexedPrimitive_pair'),
                         'timing_on': on.get('SetStreamSource_DrawIndexedPrimitive_pair')}}
    return result


def main():
    subprocess.run([str(PROBE / 'build_state_hook_benchmark.sh')], cwd=ROOT, check=True)
    result = {'generated': datetime.datetime.now().isoformat(timespec='seconds'),
              'bottle': bottle.describe(),
              'fixture': {'source': 'verification/probe/state_hook_benchmark.cpp',
                          'exe_sha256': sha(EXE)},
              'dll': {'path': str(INSTALLED_DLL), 'sha256': sha(INSTALLED_DLL)},
              'note': ('Three repetitions per configuration inside one process; ns_per_call is the median. '
                       'Equal-share setter mix: run87 frame_timing keeps one counter per bucket, no per-entry breakdown.'),
              'cases': {}}
    out = RESULTS / 'state-hook-benchmark.json'
    with (RESULTS / 'state-hook-benchmark-wine.log').open('w') as log:
        for entry in CASES:
            result['cases'][entry['name']] = run_case(entry, log)
            out.write_text(json.dumps(result, indent=2) + '\n')
            print(entry['name'], json.dumps(result['cases'][entry['name']]['ns_per_call']), flush=True)
    derive(result)
    out.write_text(json.dumps(result, indent=2) + '\n')
    print('wrote', out.relative_to(ROOT))
    print(json.dumps(result['attribution'], indent=2))


if __name__ == '__main__':
    main()
