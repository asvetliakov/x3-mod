#!/usr/bin/env python3
"""Fresh-build live motion route integration through the actual DLL; no game launch.

Eighteen runs of one original synthetic device program under CrossOver Preview
Wine with a process-local d3d9 override: the production build/d3d9.dll with the
route off and on (fill of RT1/RT2, exact restoration, Reset with the targets
owned, capture-frame readback of motion and depth, device release), and a seam
DLL (production objects + capture.cpp and motion_output.cpp compiled with
X3M_MOTION_OUTPUT_FIXTURE) off and on, which also exercises scene recognition,
variant routing, history, sentinel-only mode, the R32F current-depth target and
state block resynchronization against a CPU oracle. The four runs repeat in
three environments the gameplay diagnostic launch uses: the ownership wrapper
(X3M_OWNERSHIP=1), the wrapper with the copy-depth/scene-depth path
(X3M_DEPTH_COPY=1 X3M_SCENE_DEPTH_CAPTURE=1) and the wrapper with the
admission monitor (X3M_ADMISSION=1). Two more plain runs enable the per-draw
jitter (X3M_MOTION_JITTER=1): coverage must follow the Halton offsets, the
readback must still match the oracle from unjittered history rows with zero
prior jitter uploaded, and every application row must be restored bit-exactly.
Four TAA runs (X3M_TAA=1, temporal step 3; production and seam, plain and under
the wrapper) end every frame at the game's pre-bloom boundary: the resolve runs
inside the StretchRect hook, the bloom copy must receive the resolved image,
every touched state is restored, frames without usable history keep the 8-bit
color bit-identical, the seam's main target equals a reference TemporalPass run
on a plain second device from the same read-back inputs byte for byte (the
X3M_TAA_DEBUG FP16 files equal the reference FP16 output), and the device still
reaches zero references. Four bench runs time the boundary with the resolve
off and on at 1280x768 and 5120x1440 (EVENT-synchronized QPC, CPU-inclusive).
The resolve's jitter convention (history read at the content's previous
unjittered UV plus the CURRENT jitter, never the previous one) is covered by
the reference comparison only indirectly, because the reference runs the same
production TemporalPass bytecode; the stationary-stability evidence (zero
change between jitter phases, zero centroid drift, edge coverage converging)
lives in run_temporal_pass.py. The seam script cannot host that check: its
objects alternate between two poses every frame (a moves between t=.75/p=0
and t=.8/p=.125, b between the origin and t=-.05/zo=.1), so no frame is
stationary with respect to the previous one, and the cut schedule leaves at
most two consecutive history frames (1-2, 10-11), far short of the ~48 frames
the weight-0.9 accumulation needs to converge.
Lazy RT binding (X3M_MOTION_RT_MODE=lazy, an experiment that keeps RT1/RT2
bound across consecutive routed draws): four regular-script runs repeat with
the mode on (production and seam, plain, seam under the wrapper and seam with
TAA) and must be indistinguishable from the per-draw runs (colour hashes,
readback files, checks, restorations), and four "burst" runs (both DLLs, both
modes) run consecutive routed draws with no application getter between them,
interleaved with gate-3/gate-4 draws and an application SetRenderTarget or
depth Clear inside the scene: colour, the fixture's state signature, the
seam's RT1/RT2 hashes and the DLL's readback files must be identical between
the modes, the final state must equal the pre-burst state, and the DLL's
per-frame SetRenderTarget count must drop from 20 to 12 in the lazy
frames without capture diagnostics (capture frames restore before every
draw's diagnostics and count 20 in both modes).
Reviewed shader bytes are read from local files and never enter the repository
or the reports.
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
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'verification/probe'))
from verify_ownership_integration import verify_admission  # noqa: E402
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import analyze_motion_readback as readback_analysis  # noqa: E402
sys.path.insert(0, str(Path(__file__).resolve().parent))
from game_guard import game_running  # noqa: E402
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
# Environments beyond the route switch. 'plain' is the original four runs; the
# other three wrap the device exactly as tools/manage.py does for the gameplay
# diagnostic run (--ownership --object-trace --object-lifetime --motion-output),
# with the copy-depth path and the admission monitor added because the launcher
# may enable them alongside the route. The route's native-slot calls then land
# in the wrapper's methods, so these runs cover its bookkeeping, refcount model,
# Reset ordering and HRESULT observation against the route's own calls.
VARIANTS = {
    'plain': {},
    'ownership': dict(X3M_OWNERSHIP='1'),
    'depth': dict(X3M_OWNERSHIP='1', X3M_DEPTH_COPY='1', X3M_SCENE_DEPTH_CAPTURE='1'),
    'admission': dict(X3M_OWNERSHIP='1', X3M_ADMISSION='1')}
def case(name, mode, variant='plain', enabled='1', jitter=False, taa=False, bench=None, lazy=False, burst=False):
    return dict(name=name, mode=mode, variant=variant, enabled=enabled, jitter=jitter, taa=taa, bench=bench, lazy=lazy, burst=burst)


CASES = [case(f'{dll}-{state}' if variant == 'plain' else f'{dll}-{variant}-{state}', dll, variant, enabled)
         for variant in VARIANTS for dll in ('production', 'seam') for state, enabled in (('off', '0'), ('on', '1'))]
CASES += [case(f'{dll}-jitter-on', dll, jitter=True) for dll in ('production', 'seam')]
# Temporal step 3: the resolve at the boundary (jitter implied by the DLL),
# plain and through the wrapper, with the debug readbacks of the resolved image.
CASES += [case(f'{dll}-taa-on', dll, jitter=True, taa=True) for dll in ('production', 'seam')]
CASES += [case(f'{dll}-ownership-taa-on', dll, 'ownership', jitter=True, taa=True) for dll in ('production', 'seam')]
BENCH_SIZES = ('1280x768', '5120x1440')
CASES += [case(f'bench-{size}-taa-{state}', 'bench', jitter=True, taa=state == 'on', bench=size) for size in BENCH_SIZES for state in ('off', 'on')]
# Lazy RT binding: the regular script (equivalent by construction, every
# getter restores) and the burst script (consecutive routed draws).
CASES += [case(f'{dll}-lazy-on', dll, lazy=True) for dll in ('production', 'seam')]
CASES += [case('seam-ownership-lazy-on', 'seam', 'ownership', lazy=True), case('seam-taa-lazy-on', 'seam', jitter=True, taa=True, lazy=True)]
CASES += [case(f'{dll}-burst-{rt}', dll, lazy=rt == 'lazy', burst=True) for dll in ('production', 'seam') for rt in ('perdraw', 'lazy')]
# Burst script: nine frames, capture in frames 7-8 only (capture diagnostics
# restore the lazy binding before every draw), the frame line every frame.
BURST_FRAMES = 9
BURST_CAPTURE = (7, 8)
BURST_EXPECT = dict(draws=9, routed=5, matched=0, gate2=2, gate3=1, gate4=1, gate5=5, gate6=0, depth_routed=5)
BURST_SET_RT = {'perdraw': 20, 'lazy': 12}   # 5 routed draws x 4 versus 3 bind/flush pairs x 4
BURST_FLUSHES = {'perdraw': 0, 'lazy': 3}
# Seam script: frames whose keyed draws miss (cut) and frames the resolve blends
# with history (a previous resolved frame since Reset and no cut).
SEAM_TAA_CUTS = {0, 3, 5, 6, 8, 9}
SEAM_TAA_HISTORY = {1, 2, 4, 7, 10, 11}
JITTER_SAMPLES = 8
# Application depth clears per fixture frame with the original depth bound
# (color+depth Clear, depth-only Clear): the wrapper's source_epoch must count
# exactly these, so the route's own depth unbind inside the fill never reached one.
DEPTH_CLEARS_PER_FRAME = 2
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
# Cut detector verdict per seam frame: the missing-key fraction exceeds 0.25
# wherever a keyed draw found no previous entry (gate 6); the matched draws'
# origins move 0.05 NDC = 1.6 px, below the 48 px * 64/1280 = 2.4 px bound.
SEAM_CUTS = {1: 0, 2: 0, 3: 1, 4: 0, 5: 1, 6: 1, 7: 0, 8: 1}
ORIGIN_DISPLACEMENT_PX = 1.6


def halton(index, base):
    fraction, result = 1.0, 0.0
    while index:
        fraction /= base
        result += fraction * (index % base)
        index //= base
    return result


def expected_jitter(frame):
    index = frame % JITTER_SAMPLES
    return index, halton(index + 1, 2) - 0.5, halton(index + 1, 3) - 0.5


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
    assert not game_running(), 'X3AP running; no synthetic GPU run'


def read_motion(path, width=64, height=64):
    data = path.read_bytes()
    assert len(data) == width * height * 16, f'{path}: unexpected size {len(data)}'
    return [struct.unpack_from('<4f', data, i * 16) for i in range(width * height)]


def read_depth(path, width=64, height=64):
    data = path.read_bytes()
    assert len(data) == width * height * 4, f'{path}: unexpected size {len(data)}'
    return list(struct.unpack('<%df' % (width * height), data))


def validate_ownership(name, variant, enabled, trace):
    """Wrapper-side witnesses: adoption once, no fallback, depth storage and
    epochs, scene-depth adapter, admission roots/vetoes; balanced retirement is
    proven by the fixture's zero final device/factory Release, which through the
    wrapper requires every child wrapper (the route's included) to be gone."""
    tl = trace.splitlines()
    env = VARIANTS[variant]
    wrapped = env.get('X3M_OWNERSHIP') == '1'
    depth_mode = env.get('X3M_DEPTH_COPY') == '1'
    admission = env.get('X3M_ADMISSION') == '1'
    result = {'wrapped': wrapped, 'depth_copy': depth_mode, 'admission': admission}
    factory = [l for l in tl if l.startswith('ownership_factory ')]
    assert len(factory) == int(wrapped) and all('mode=wrapped' in l for l in factory), (name, factory)
    assert 'mode=native_fallback' not in trace, name
    modes = [l for l in tl if l.startswith('ownership_mode ')]
    if wrapped:
        assert modes == [f'ownership_mode requested=1 depth_copy_requested={int(depth_mode)} depth_copy_enabled={int(depth_mode)} scope=normal9 fallback=native'], (name, modes)
    else:
        assert not modes, (name, modes)
    depth = [fields(l) for l in tl if l.startswith('ownership_copy_depth ')]
    phases = {}
    for d in depth:
        phases.setdefault(d['phase'], []).append(d)
    if wrapped:
        assert len(phases.get('create_after', [])) == len(phases.get('reset_after', [])) == 1, (name, sorted(phases))
        assert all(d['result'] == '00000000' and d['requested'] == str(int(depth_mode)) for d in depth), (name, depth)
        if depth_mode:
            assert all(d['available'] == '1' and d['source_bound'] == '1' and d['copy_valid'] == '0' and d['copy_epoch'] == '0'
                       and d['source_format'] == '77' for d in depth), (name, depth)
            # Reset retires the storage (one generation) and allocates anew (another).
            generations = (int(phases['create_after'][0]['generation']), int(phases['reset_after'][0]['generation']))
            assert generations[0] == 1 and generations[1] > generations[0], (name, generations)
            result['depth_generations'] = generations
        else:
            assert all(d['available'] == '0' for d in depth), (name, depth)
    else:
        assert not depth, (name, depth)
    # Per-capture-frame epochs are logged only while the route is requested.
    present = {int(d['frame']): d for d in phases.get('present', [])}
    if wrapped and enabled:
        assert sorted(present) == list(range(1, 9)), (name, sorted(present))
        if depth_mode:
            for frame, d in present.items():
                assert int(d['source_epoch']) == DEPTH_CLEARS_PER_FRAME * (frame + 1), (name, frame, d)
                assert d['generation'] == '1' and d['source_bound'] == '1' and d['copy_valid'] == '0' and d['copy_epoch'] == '0', (name, frame, d)
        result['source_epochs'] = {f: int(d['source_epoch']) for f, d in present.items()}
    else:
        assert not present, (name, sorted(present))
    scene = [fields(l) for l in tl if l.startswith('scene_depth_frame ')]
    if depth_mode:
        begins = sorted(int(s['frame']) for s in scene if s['phase'] == 'begin')
        ends = [s for s in scene if s['phase'] == 'end']
        assert begins == list(range(1, 9)) and sorted(int(s['frame']) for s in ends) == begins, (name, begins)
        assert all(s['attempted'] == s['copied'] == s['confirmed'] == '0' and s['present'] == '00000000' for s in ends), (name, ends)
        assert 'scene_depth_copy ' not in trace and 'scene_depth_boundary ' not in trace, 'synthetic frames must not select a game boundary'
    else:
        assert not scene, (name, len(scene))
    result['admission'] = verify_admission(trace, admission, final_count=1, device_count=1)
    metrics = [fields(l) for l in tl if l.startswith('admission_metric ')]
    if admission:
        # Present logs the metric in captured frames and every 300th frame (frame 0).
        assert sorted(int(m['frame']) for m in metrics) == list(range(0, 9)), (name, len(metrics))
        assert all(m['veto_bits'] == '0' and m['first_reason'] == '0' and m['waiting_roots'] == '0' and m['replay_active'] == '0' for m in metrics), (name, metrics)
        assert result['admission']['final_vetoes'] == 0, (name, result['admission'])
    else:
        assert not metrics, (name, len(metrics))
    return result


def validate_bench(name, taa, size, text, trace):
    lines = text.splitlines()
    assert lines and lines[-1].startswith('RESULT PASS '), f'{name}: bench did not pass'
    mode_line = fields([l for l in lines if l.startswith('MODE ')][0])
    width, height = size.split('x')
    assert (mode_line['bench'], mode_line['taa'], mode_line['width'], mode_line['height'], mode_line['enabled']) == ('1', '1', width, height, '1'), (name, mode_line)
    summary = fields([l for l in lines if l.startswith('BENCH_SUMMARY ')][0])
    samples = [float(fields(l)['boundary_ms']) for l in lines if l.startswith('BENCH ')]
    assert len(samples) == 24 and int(summary['frames']) == 20, (name, summary)
    frames = {int(fields(l)['frame']): fields(l) for l in trace.splitlines() if l.startswith('motion_output_frame ')}
    # Frame 0 is logged by telemetry: the boundary was recognized and, with the
    # switch on, resolved (current-only, first frame); off, nothing ran.
    assert 0 in frames and frames[0]['latched'] == frames[0]['filled'] == '1' and frames[0]['selector_state'] == '9', (name, frames.get(0))
    assert frames[0]['taa'] == str(int(taa)) and frames[0]['taa_resolved'] == str(int(taa)), (name, frames[0])
    if taa:
        assert frames[0]['taa_attempted'] == '1' and frames[0]['taa_skip'] == '0' and frames[0]['taa_result'] == frames[0]['taa_copy'] == '00000000', (name, frames[0])
    assert not any(l.startswith(('motion_output_taa_failed', 'motion_output_fill_failed', 'motion_output_apply_failed', 'motion_output_restore_failed')) for l in trace.splitlines()), name
    return {'mode': 'bench', 'taa': taa, 'width': int(width), 'height': int(height), 'frames_timed': int(summary['frames']),
            'boundary_ms': {'min': float(summary['min_ms']), 'median': float(summary['median_ms']), 'max': float(summary['max_ms'])},
            'samples_ms': samples, 'timing': summary['timing']}


def validate_case(name, mode, variant, enabled, jitter, taa, text, trace, directory, lazy=False):
    lines = text.splitlines()
    assert lines and lines[-1].startswith('RESULT PASS '), f'{name}: fixture did not pass'
    assert 'FAIL' not in text and text.count('RESULT ') == 1, f'{name}: failures reported'
    terminal = fields(lines[-1])
    seam = mode == 'seam'
    live = seam and enabled
    rt_mode = 'lazy' if lazy else 'perdraw'
    mode_line = fields([l for l in lines if l.startswith('MODE ')][0])
    assert mode_line == {'seam': str(int(seam)), 'enabled': str(int(enabled)), 'jitter': str(int(jitter)),
                         'jitter_samples': str(JITTER_SAMPLES), 'taa': str(int(taa)), 'bench': '0', 'width': '64', 'height': '64',
                         'dll': mode_line['dll'], 'burst': '0', 'rt_mode': rt_mode}, (name, mode_line)
    # Per frame: the coverage oracle's background sample and verdict (every
    # case) plus, live, motion/depth dimensions, the pixel-ABI upload, the
    # motion oracle and the depth oracle. TAA: per frame the bloom copy, the
    # history verdict and (frames without history) the bit-identical color,
    # plus the script totals; seam adds the reference device, the byte-exact
    # comparison and the FP16 file per frame and the reference teardown.
    expected_checks = 30 + (60 if live else 0)
    if taa:
        expected_checks += 12 * 2 + (12 - len(SEAM_TAA_HISTORY) if live else 12) + 3 + (1 + 24 + 2 if live else 0)
    assert int(terminal['checks']) == expected_checks, (name, terminal, expected_checks)
    restorations = 39 + (12 if taa else 0)
    assert int(terminal['restorations']) == restorations and int(terminal['frames']) == 12, (name, terminal)
    assert text.count('RESET PASS') == 1
    restores = [fields(l) for l in lines if l.startswith('RESTORE ')]
    assert len(restores) == restorations and all(r['differences'] == '0' for r in restores), f'{name}: restoration differences'
    colors = {int(fields(l)['frame']): fields(l)['hash'] for l in lines if l.startswith('COLOR ')}
    assert sorted(colors) == list(range(12)), f'{name}: color inventory'
    motion = [fields(l) for l in lines if l.startswith('MOTION ')]
    coverage = {int(fields(l)['frame']): fields(l) for l in lines if l.startswith('COVERAGE ')}
    result = {'mode': mode, 'enabled': enabled, 'jitter': jitter, 'taa': taa, 'lazy': lazy, 'checks': int(terminal['checks']), 'restorations': restorations, 'frames': 12,
              'color_hashes': colors, 'motion_pixels': int(terminal['motion_pixels']),
              'matched_pixels': int(terminal['matched_pixels']), 'max_uv_error_pixels': float(terminal['max_uv_pixels']),
              'max_previous_depth_error': float(terminal['max_depth_error']),
              'depth_pixels': int(terminal['depth_pixels']), 'depth_written_pixels': int(terminal['depth_written']),
              'max_current_depth_error': float(terminal['max_current_depth_error']),
              'coverage_frames': int(terminal['coverage_frames']), 'coverage_pixels': int(terminal['coverage_pixels']),
              'coverage_ambiguous_pixels': int(terminal['coverage_ambiguous'])}
    # Rasterized coverage agrees with the CPU reference at the (jittered)
    # sample positions in every frame of every case (route off: the oracle's
    # own control); the reference uses the route's Halton offsets, so a sign
    # or scale error in the jitter would fail here.
    assert sorted(coverage) == list(range(12)), (name, sorted(coverage))
    assert all(c['mismatches'] == '0' and int(c['checked']) > 3000 and int(c['background']) > 0 for c in coverage.values()), (name, coverage)
    assert len({c['background_color'] for c in coverage.values()}) == 1, (name, 'background colour differs between frames')
    assert all(c['jitter'] == str(int(jitter)) for c in coverage.values())
    for frame, c in coverage.items():
        _, jx, jy = expected_jitter(frame) if jitter else (0, 0.0, 0.0)
        assert abs(float(c['jx']) - jx) < 1e-6 and abs(float(c['jy']) - jy) < 1e-6, (name, frame, c)
    assert result['coverage_frames'] == 12 and result['coverage_pixels'] > 40000
    taa_lines = {int(fields(l)['frame']): fields(l) for l in lines if l.startswith('TAA ')}
    if taa:
        # Fixture verdicts per frame: the bloom copy equals the main target,
        # history follows the script, no-history frames are bit-identical.
        assert sorted(taa_lines) == list(range(12)), (name, sorted(taa_lines))
        history = {f for f, t in taa_lines.items() if t['history'] == '1'}
        cuts = {f for f, t in taa_lines.items() if t['cut'] == '1'}
        assert history == (SEAM_TAA_HISTORY if live else set()) and cuts == (SEAM_TAA_CUTS if live else set()), (name, history, cuts)
        assert all(t['changed'] == '0' for f, t in taa_lines.items() if f not in history), (name, 'no-history frame changed the color')
        assert (terminal['taa'], terminal['taa_frames'], terminal['taa_history_frames'], terminal['taa_reference_frames']) == ('1', '12', str(len(history)), '12' if live else '0'), (name, terminal)
        result['taa_history_frames'] = sorted(history)
        result['taa_changed_pixels'] = {f: int(t['changed']) for f, t in taa_lines.items()}
        result['color_hashes_before_boundary'] = {int(fields(l)['frame']): fields(l)['hash'] for l in lines if l.startswith('COLOR_BEFORE ')}
    else:
        assert not taa_lines and terminal['taa'] == '0'
    if live:
        assert len(motion) == 12 and all(m['mismatches'] == '0' and m['depth_mismatches'] == '0' and int(m['checked']) > 3000 for m in motion), f'{name}: oracle'
        assert result['motion_pixels'] > 40000 and result['matched_pixels'] > 10000
        assert result['max_uv_error_pixels'] <= .01 and result['max_previous_depth_error'] <= 4e-6
        assert result['depth_pixels'] == result['motion_pixels'] and result['depth_written_pixels'] > 20000
        assert result['max_current_depth_error'] <= 4e-6
        # Frame 7 routes only the small triangle (the A draw rebinds the flat PS
        # through the state block, gate 3): about 170 pixels; other frames > 2,600.
        assert all(int(m['depth_written']) > 100 for m in motion), f'{name}: every frame writes RT2 for its routed draws'
        assert sum(int(m['matched']) for m in motion if int(m['frame']) in (0, 2, 3, 9)) == 0, 'sentinel-only frames carried motion'
    else:
        assert not motion and result['motion_pixels'] == 0 and result['depth_pixels'] == 0
    # Capture log: mode line, device gate, variants, target lifecycle, frames, readback.
    tl = trace.splitlines()
    assert sum(l.startswith('device_hooked ') for l in tl) == sum(l.startswith('device_destroy ') for l in tl) == 1
    modes = [fields(l) for l in tl if l.startswith('motion_output_mode ')]
    assert len(modes) == 1 and modes[0]['requested'] == str(int(enabled)) and modes[0]['temporal_consumer'] == modes[0]['taa'] == str(int(taa)) and modes[0]['taa_debug'] == str(int(taa))
    devices = [fields(l) for l in tl if l.startswith('motion_output_device ')]
    variants = [fields(l) for l in tl if l.startswith('motion_output_variant ')]
    targets = [l for l in tl if l.startswith('motion_output_target ')]
    frames = {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('motion_output_frame ')}
    readbacks = {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('motion_output_readback ')}
    depth_readbacks = {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('motion_output_depth_readback ')}
    cuts = {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('motion_output_cut ')}
    routes = [fields(l) for l in tl if l.startswith('motion_route ')]
    modes = [fields(l) for l in tl if l.startswith('motion_output_mode ')]
    assert modes[0]['jitter'] == str(int(jitter)) and modes[0]['jitter_samples'] == str(JITTER_SAMPLES), (name, modes)
    assert modes[0]['rt_mode'] == rt_mode and modes[0]['frame_log'] == '60', (name, modes)
    taa_readbacks = {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('motion_output_taa_readback ')}
    color_readbacks = {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('motion_output_color_readback ')}
    taa_lines_log = [fields(l) for l in tl if l.startswith('motion_output_taa ')]
    if not enabled:
        assert not devices and not variants and not targets and not frames and not readbacks and not depth_readbacks and not cuts and not routes, f'{name}: disabled route logged activity'
        assert not any(l.startswith('motion_output_release ') for l in tl)
        assert not taa_readbacks and not color_readbacks and not taa_lines_log
        return result
    assert len(devices) == 1 and devices[0]['enabled'] == '1' and devices[0]['reason'] == 'ok', (name, devices)
    assert devices[0]['rt_mode'] == rt_mode, (name, devices)
    assert devices[0]['history_available'] == '0', 'synthetic process must not claim game observers'
    # Three-format self test (A8R8G8B8 + A32B32G32R32F + R32F) on this backend.
    assert devices[0]['depth'] == '1' and devices[0]['depth_reason'] == 'ok' and devices[0]['r32f'] == '00000000', (name, devices)
    assert devices[0]['detail'] == 'stage=compare' and 'color_errors=0 motion_errors=0 depth_errors=0 targets=3' in trace
    assert devices[0]['jitter'] == str(int(jitter)) and devices[0]['jitter_samples'] == str(JITTER_SAMPLES)
    assert devices[0]['taa'] == str(int(taa)) and devices[0]['taa_reason'] == ('ok' if taa else 'off') and devices[0]['taa_debug'] == str(int(taa)), (name, devices)
    assert [(v['kind'], v['transform'], v['create'], v['depth']) for v in variants] == [('vs', '0', '00000000', '1'), ('ps', '0', '00000000', '1')] * 2, (name, variants)
    assert all(v['original'] in ('53a0a641107ed76c', '8759c7838bbc86c2') for v in variants)
    assert len(targets) == 2 and all('create=00000000 level=00000000 depth=1 depth_create=00000000 depth_level=00000000' in t for t in targets), 'targets created at first latch and after Reset'
    assert 'motion_output_reset device=1 result=00000000 generation=2' in trace
    assert not any(l.startswith('motion_output_taa_failed') for l in tl), f'{name}: resolve failures logged'
    if taa:
        # One lazy initialization holding one device reference (the resolve
        # shader); after Reset only that reference remains until the next run.
        assert [t['initialize'] for t in taa_lines_log] == ['00000000'] and taa_lines_log[0]['references'] == '1', (name, taa_lines_log)
        assert 'generation=2 taa_references=1' in trace, name
    else:
        assert not taa_lines_log and not taa_readbacks and not color_readbacks
    assert sum(l.startswith('motion_output_release ') for l in tl) == 1, 'owned objects released before the final device Release'
    assert not any(l.startswith(('motion_output_fill_failed', 'motion_output_apply_failed', 'motion_output_restore_failed')) for l in tl)
    assert sorted(frames) == list(range(0, 9)), (name, sorted(frames))  # frame 0 via telemetry, 1..8 via capture
    assert sorted(cuts) == list(range(1, 9)), (name, sorted(cuts))
    for frame, summary in frames.items():
        assert summary['latched'] == summary['filled'] == '1' and summary['fill_result'] == summary['fill_restore'] == '00000000'
        assert summary['apply_failures'] == summary['restore_failures'] == '0' and summary['committed'] == '1'
        assert summary['present'] == '00000000'
        assert summary['depth'] == '1' and summary['depth_routed'] == summary['routed'], (name, frame, summary)
        # Cost fields: every routed draw of this script binds and releases RT1
        # and RT2 (four SetRenderTarget calls) in both modes, because the
        # fixture's state snapshot after each draw (a getter) restores a lazy
        # binding: one flush per routed draw. Timing is CPU QPC (telemetry on).
        assert summary['rt_mode'] == rt_mode and summary['timing'] == 'cpu_qpc', (name, frame, summary)
        assert int(summary['set_rt']) == 4 * int(summary['routed']), (name, frame, summary)
        assert int(summary['lazy_flushes']) == (int(summary['routed']) if lazy else 0), (name, frame, summary)
        assert int(summary['jitter_writes']) == 2 * int(summary['jittered']), (name, frame, summary)
        assert int(summary['readbacks']) == (0 if frame == 0 else 4 if taa else 2), (name, frame, summary)
        cost_fields = ('gate_us', 'route_draw_us', 'set_rt_us', 'lazy_flush_us', 'jitter_us', 'fill_us', 'taa_run_us', 'taa_capture_us',
                       'taa_copy_color_us', 'taa_copy_depth_us', 'taa_draw_us', 'taa_apply_us', 'taa_copy_back_us', 'readback_us')
        costs = {k: float(summary[k]) for k in cost_fields}
        assert all(v >= 0 for v in costs.values()) and costs['fill_us'] > 0 and costs['route_draw_us'] > 0, (name, frame, costs)
        # The five phases nest inside the run (0.1 us rounding per field).
        assert (costs['taa_run_us'] > 0) == taa and costs['taa_run_us'] + 0.5 >= costs['taa_capture_us'] + costs['taa_copy_color_us'] + costs['taa_copy_depth_us'] + costs['taa_draw_us'] + costs['taa_apply_us'], (name, frame, costs)
        assert (costs['readback_us'] > 0) == (frame > 0), (name, frame, costs)
        # The resolve ran at the boundary of every TAA frame (frame 0 included)
        # with the history use the fixture expects; the fixture's depth rebind
        # then rejects the selector (9), which must not drop the history.
        # Without the switch nothing was attempted (skip 1).
        assert summary['taa'] == str(int(taa)) and summary['active_queries'] == '0', (name, frame, summary)
        if taa:
            # The pass reports whether it blended a valid history: the seam's
            # script frames; the production DLL from frame 1 on (the sentinel
            # policy establishes history although every pixel resolves
            # current-only), except the first frame after Reset (9, not captured).
            expect_history = int(frame in SEAM_TAA_HISTORY) if live else int(frame not in (0, 9))
            assert (summary['taa_attempted'], summary['taa_resolved'], summary['taa_history'], summary['taa_skip']) == ('1', '1', str(expect_history), '0'), (name, frame, summary)
            # The frame line is written after Present, outside the application's scene.
            assert summary['taa_result'] == summary['taa_restore'] == summary['taa_copy'] == '00000000' and summary['scene_open'] == '0', (name, frame, summary)
            assert int(summary['taa_references']) >= 1, (name, frame, summary)
        else:
            assert (summary['taa_attempted'], summary['taa_resolved'], summary['taa_skip']) == ('0', '0', '1'), (name, frame, summary)
        # Jitter: the sequence index advances at every latch whether or not the
        # switch is on; the sample values and the previous latch's values are
        # the Halton offsets with jitter on and zero otherwise, and every scene
        # draw (all draw the table VS) is jittered only with the switch on.
        index, jx, jy = expected_jitter(frame)
        _, pjx, pjy = expected_jitter(frame - 1) if frame else (0, 0.0, 0.0)
        if not jitter:
            jx = jy = pjx = pjy = 0.0
        assert summary['jitter'] == str(int(jitter)) and int(summary['jitter_index']) == index, (name, frame, summary)
        assert abs(float(summary['jitter_x']) - jx) < 1e-6 and abs(float(summary['jitter_y']) - jy) < 1e-6, (name, frame, summary)
        assert abs(float(summary['jitter_previous_x']) - pjx) < 1e-6 and abs(float(summary['jitter_previous_y']) - pjy) < 1e-6, (name, frame, summary)
        assert int(summary['jittered']) == (int(summary['draws']) - 1 if jitter else 0), (name, frame, summary)
        # Cut detector: samples are the matched draws, the median their 1.6 px
        # origin displacement; the verdict follows the missing-key fraction.
        assert int(summary['cut_samples']) == int(summary['matched']), (name, frame, summary)
        if int(summary['matched']):
            assert abs(float(summary['cut_median_px']) - ORIGIN_DISPLACEMENT_PX) < 1e-3, (name, frame, summary)
        if live and frame in SEAM_FRAMES:
            got = {k: int(summary[k]) for k in SEAM_FRAMES[frame]}
            assert got == SEAM_FRAMES[frame], (name, frame, got, SEAM_FRAMES[frame])
            assert summary['selector_state'] == ('9' if taa else '2'), 'seam frames end inside the Scene phase (rejected after the copy with the TAA boundary: the fixture rebinds depth without a bloom sequence)'
            assert int(summary['cut']) == SEAM_CUTS[frame], (name, frame, summary)
            cut = cuts[frame]
            assert int(cut['cut']) == SEAM_CUTS[frame] and cut['samples'] == summary['cut_samples'] and abs(float(cut['bound_px']) - 2.4) < 1e-6 and cut['bound_missing'] == '0.250'
        elif not live:
            # Production DLL: the structural selector enters the synthetic frame's
            # scene phase too, but without the game observers every scene draw
            # that passes gates 3-4 routes in sentinel-only mode (gate 5).
            got = {k: int(summary[k]) for k in ('draws', 'routed', 'matched', 'gate2', 'gate3', 'gate4', 'gate5', 'gate6')}
            assert got['matched'] == 0 and got['gate2'] == 1 and got['gate6'] == 0, (name, frame, got)
            assert got['routed'] == got['gate5'] == got['draws'] - 1 - got['gate3'] - got['gate4'], (name, frame, got)
            if frame in SEAM_FRAMES:
                assert (got['gate3'], got['gate4']) == (SEAM_FRAMES[frame]['gate3'], SEAM_FRAMES[frame]['gate4']), (name, frame, got)
            assert summary['selector_state'] == ('9' if taa else '2'), 'production frames also end inside the Scene phase (rejected after the copy with the TAA boundary)'
    assert sorted(readbacks) == sorted(depth_readbacks) == list(range(1, 9)), (name, sorted(readbacks), sorted(depth_readbacks))
    depth_stats = {}
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
        # RT2: device depth where a routed draw covered the pixel (matched or
        # sentinel-only alike), -1 elsewhere; every motion-valid pixel has depth.
        d = depth_readbacks[frame]
        assert d['result'] == '00000000' and d['bytes'] == '16384' and d['width'] == d['height'] == '64' and d['format'] == 'r32f_row_major'
        assert d['file'] == f'depth_1_{frame}.r32f'
        depth = read_depth(directory / 'x3-modern-captures' / d['file'])
        written = sum(1 for v in depth if v != -1.0)
        assert all(v == -1.0 or 0.0 <= v <= 1.0 for v in depth), f'{name}: frame {frame} depth outside [0,1] or sentinel'
        assert written > (100 if frame == 7 else 1000), f'{name}: frame {frame} depth target lacks routed coverage'
        assert all(depth[i] != -1.0 for i, p in enumerate(pixels) if p[3] == 1.0), f'{name}: frame {frame} motion-valid pixel without depth'
        depth_stats[frame] = {'written': written, 'min': min(v for v in depth if v != -1.0), 'max': max(v for v in depth if v != -1.0)}
    result['depth_readbacks'] = depth_stats
    if taa:
        # X3M_TAA_DEBUG: the resolved FP16 image and the pre-resolve color of
        # frames 1-8. Seam: the FP16 bytes equal the reference pass's output.
        # Both: the analyzer's sanity signal loads them (finite; production is
        # current-only, so no pixel differs from the pre-resolve color).
        assert sorted(taa_readbacks) == sorted(color_readbacks) == list(range(1, 9)), (name, sorted(taa_readbacks), sorted(color_readbacks))
        for frame in range(1, 9):
            t, c = taa_readbacks[frame], color_readbacks[frame]
            assert t['result'] == c['result'] == '00000000' and t['bytes'] == '32768' and c['bytes'] == '16384', (name, frame, t, c)
            assert t['file'] == f'taa_1_{frame}.rgba16f' and c['file'] == f'color_1_{frame}.bgra8' and t['format'] == 'rgba16f_row_major' and c['format'] == 'bgra8_row_major'
            resolved = (directory / 'x3-modern-captures' / t['file']).read_bytes()
            assert len(resolved) == 32768
            if live:
                reference = (directory / f'reference_taa_{frame}.rgba16f').read_bytes()
                assert resolved == reference, f'{name}: frame {frame} resolved FP16 image differs from the reference pass'
        analysis = readback_analysis.analyze(next((directory / 'x3-modern-captures').glob('session-*.log')),
                                             directory / 'x3-modern-captures', readback_analysis.default_options(draw_details=False))
        taa_check = analysis['checks']['taa_image']
        assert taa_check['status'] == 'pass' and taa_check['frames'] == list(range(1, 9)), (name, taa_check)
        if not live:
            assert all(fraction == 0 for fraction in taa_check['differing_fraction'].values()), (name, taa_check)
        result['taa_image'] = {'differing_fraction': taa_check['differing_fraction'], 'max_difference': taa_check['max_difference']}
    # Per-draw decisions logged in capture frames must agree with the fixture's script.
    expects = [fields(l) for l in lines if l.startswith('EXPECT ') and 1 <= int(fields(l)['frame']) <= 8]
    assert len(routes) == len(expects) == sum(SEAM_FRAMES[f]['draws'] - 1 for f in SEAM_FRAMES), (name, len(routes), len(expects))
    assert all(r['jittered'] == e['jittered'] for e in expects for r in routes if r['frame'] == e['frame'] and r['index'] == e['index']), f'{name}: jittered flags'
    assert all(r['depth'] == r['routed'] for r in routes), f'{name}: every routed draw of the reviewed row binds RT2'
    if live:
        for e in expects:
            match = [r for r in routes if r['frame'] == e['frame'] and r['index'] == e['index']]
            assert len(match) == 1 and match[0]['routed'] == e['routed'] and match[0]['matched'] == e['matched'], (name, e, match)
            assert match[0]['result'] == '00000000'
    else:
        # Every production route is sentinel-only: routed exactly when scope is
        # unknown (gate 5), never matched, and the fixture's own EXPECT lines
        # (which assume no live routing) still name each scene draw once.
        assert all(r['matched'] == '0' and r['routed'] == str(int(r['gate'] == '5')) and r['result'] == '00000000' for r in routes), f'{name}: production routes must be sentinel-only'
        assert {(r['frame'], r['index']) for r in routes} == {(e['frame'], e['index']) for e in expects}, f'{name}: production routes must cover the scene draws'
    result.update(variants=len(variants), frames_logged=len(frames), readbacks=len(readbacks), routes=len(routes))
    return result


def finish_case(name, mode, variant, enabled, jitter, taa, text, trace, directory, lazy=False):
    result = validate_case(name, mode, variant, enabled, jitter, taa, text, trace, directory, lazy)
    result['variant'] = variant
    result['ownership'] = validate_ownership(name, variant, enabled, trace)
    return result


def validate_burst(name, mode, lazy, text, trace, directory):
    """Burst script (see the module docstring): per-frame counters of the DLL,
    the fixture's own restoration and oracle verdicts, and the signatures the
    cross-mode comparison in main() uses."""
    lines = text.splitlines()
    assert lines and lines[-1].startswith('RESULT PASS '), f'{name}: fixture did not pass'
    assert 'FAIL' not in text and text.count('RESULT ') == 1, f'{name}: failures reported'
    terminal = fields(lines[-1])
    seam = mode == 'seam'
    rt_mode = 'lazy' if lazy else 'perdraw'
    mode_line = fields([l for l in lines if l.startswith('MODE ')][0])
    assert mode_line == {'seam': str(int(seam)), 'enabled': '1', 'jitter': '0', 'jitter_samples': str(JITTER_SAMPLES), 'taa': '0', 'bench': '0',
                         'width': '64', 'height': '64', 'dll': mode_line['dll'], 'burst': '1', 'rt_mode': rt_mode}, (name, mode_line)
    # Per frame: the fill and the burst restoration comparisons, the coverage
    # oracle (both DLLs) and, seam, the motion/depth oracle.
    assert int(terminal['frames']) == BURST_FRAMES and int(terminal['restorations']) == 2 * BURST_FRAMES, (name, terminal)
    assert int(terminal['checks']) == (68 if seam else 23), (name, terminal)
    restores = [fields(l) for l in lines if l.startswith('RESTORE ')]
    assert len(restores) == 2 * BURST_FRAMES and all(r['differences'] == '0' for r in restores), f'{name}: restoration differences'
    assert [r['label'] for r in restores] == ['fill', 'burst'] * BURST_FRAMES, (name, [r['label'] for r in restores])
    colors = {int(fields(l)['frame']): fields(l)['hash'] for l in lines if l.startswith('COLOR ')}
    states = {int(fields(l)['frame']): fields(l)['hash'] for l in lines if l.startswith('STATE ')}
    motion_hashes = {int(fields(l)['frame']): (fields(l)['motion'], fields(l)['depth']) for l in lines if l.startswith('MOTION_HASH ')}
    coverage = {int(fields(l)['frame']): fields(l) for l in lines if l.startswith('COVERAGE ')}
    motion = [fields(l) for l in lines if l.startswith('MOTION ')]
    assert sorted(colors) == sorted(states) == sorted(coverage) == list(range(BURST_FRAMES)), (name, sorted(colors), sorted(states), sorted(coverage))
    assert all(c['mismatches'] == '0' and int(c['checked']) > 3000 for c in coverage.values()), (name, coverage)
    if seam:
        assert sorted(motion_hashes) == list(range(BURST_FRAMES)) and len(motion) == BURST_FRAMES, (name, sorted(motion_hashes), len(motion))
        assert all(m['mismatches'] == '0' and m['depth_mismatches'] == '0' and m['matched'] == '0' and int(m['depth_written']) > 1000 for m in motion), (name, motion)
        assert int(terminal['matched_pixels']) == 0 and int(terminal['depth_written']) > 10000, (name, terminal)
    else:
        assert not motion_hashes and not motion
    expects = [fields(l) for l in lines if l.startswith('EXPECT ')]
    assert len(expects) == 8 * BURST_FRAMES and all(e['matched'] == '0' and e['jittered'] == '0' for e in expects), (name, len(expects))
    tl = trace.splitlines()
    modes = [fields(l) for l in tl if l.startswith('motion_output_mode ')]
    assert len(modes) == 1 and modes[0]['rt_mode'] == rt_mode and modes[0]['frame_log'] == '1', (name, modes)
    devices = [fields(l) for l in tl if l.startswith('motion_output_device ')]
    assert len(devices) == 1 and devices[0]['enabled'] == '1' and devices[0]['rt_mode'] == rt_mode and devices[0]['depth'] == '1', (name, devices)
    assert not any(l.startswith(('motion_output_fill_failed', 'motion_output_apply_failed', 'motion_output_restore_failed', 'motion_output_taa_failed')) for l in tl), name
    frames = {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('motion_output_frame ')}
    assert sorted(frames) == list(range(BURST_FRAMES)), (name, sorted(frames))  # X3M_MOTION_FRAME_LOG=1
    set_rt, flushes, costs = {}, {}, {}
    for frame, summary in frames.items():
        got = {k: int(summary[k]) for k in BURST_EXPECT}
        assert got == BURST_EXPECT, (name, frame, got)
        # The application SetRenderTarget (even frames) or depth Clear (odd)
        # inside the scene rejects the selector; the draw after it stops at gate 2.
        assert summary['selector_state'] == '9' and summary['latched'] == summary['filled'] == '1', (name, frame, summary)
        assert summary['apply_failures'] == summary['restore_failures'] == '0' and summary['present'] == '00000000', (name, frame, summary)
        assert summary['rt_mode'] == rt_mode and summary['timing'] == 'cpu_qpc' and summary['jitter_writes'] == '0', (name, frame, summary)
        captured = frame in BURST_CAPTURE
        expected_set_rt = BURST_SET_RT['perdraw'] if captured else BURST_SET_RT[rt_mode]
        expected_flushes = (5 if captured else BURST_FLUSHES['lazy']) if lazy else 0
        assert int(summary['set_rt']) == expected_set_rt and int(summary['lazy_flushes']) == expected_flushes, (name, frame, summary)
        assert int(summary['readbacks']) == (2 if captured else 0) and (float(summary['readback_us']) > 0) == captured, (name, frame, summary)
        set_rt[frame] = int(summary['set_rt']); flushes[frame] = int(summary['lazy_flushes'])
        costs[frame] = {k: float(summary[k]) for k in ('gate_us', 'route_draw_us', 'set_rt_us', 'lazy_flush_us', 'fill_us', 'readback_us')}
        assert all(v >= 0 for v in costs[frame].values()) and costs[frame]['set_rt_us'] > 0 and costs[frame]['fill_us'] > 0, (name, frame, costs[frame])
        assert (costs[frame]['lazy_flush_us'] > 0) == lazy, (name, frame, costs[frame])
    routes = [fields(l) for l in tl if l.startswith('motion_route ')]
    # Capture frames log the seven scene draws that reach gate 2 (the draw
    # after the rejection is not a scene draw); five route sentinel-only.
    assert len(routes) == 7 * len(BURST_CAPTURE) and sum(r['routed'] == '1' for r in routes) == 5 * len(BURST_CAPTURE), (name, len(routes))
    assert all(r['matched'] == '0' and r['result'] == '00000000' and r['depth'] == r['routed'] for r in routes), name
    readbacks = {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('motion_output_readback ')}
    depth_readbacks = {int(fields(l)['frame']): fields(l) for l in tl if l.startswith('motion_output_depth_readback ')}
    assert sorted(readbacks) == sorted(depth_readbacks) == list(BURST_CAPTURE), (name, sorted(readbacks), sorted(depth_readbacks))
    files = {}
    for frame in BURST_CAPTURE:
        assert readbacks[frame]['result'] == depth_readbacks[frame]['result'] == '00000000', (name, frame)
        files[frame] = {kind: sha(directory / 'x3-modern-captures' / r[frame]['file']) for kind, r in (('motion', readbacks), ('depth', depth_readbacks))}
    assert sum(l.startswith('motion_output_release ') for l in tl) == 1, name
    return {'mode': mode, 'burst': True, 'lazy': lazy, 'checks': int(terminal['checks']), 'restorations': int(terminal['restorations']),
            'frames': BURST_FRAMES, 'color_hashes': colors, 'state_hashes': states, 'motion_hashes': motion_hashes,
            'readback_sha256': files, 'set_rt_per_frame': set_rt, 'lazy_flushes_per_frame': flushes, 'costs_us_per_frame': costs,
            'coverage_pixels': int(terminal['coverage_pixels']), 'depth_written_pixels': int(terminal['depth_written'])}


def main():
    RESULTS.mkdir(exist_ok=True)
    summary_path = RESULTS / 'motion-output-summary.json'
    report_path = RESULTS / 'motion-output.txt'
    result = {'passed': False, 'status': 'RUNNING', 'game_launched': False,
              'scope': 'Live same-draw route (checkpoint B1 + temporal steps 1 and 3: RT2 current depth, per-draw jitter, cut detector, the temporal resolve at the bloom copy with copy-back) through the actual proxy DLL with one original synthetic device program, plain and under the ownership wrapper (plus copy-depth and admission); seam DLL adds fixture scope injection, target readback and the reference resolve comparison. Bench runs time the boundary. Not gameplay validation.',
              'variants': VARIANTS,
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
        result['bench'] = {}
        for entry in CASES:
            name, mode, variant, enabled, jitter, taa, bench, lazy, burst = (entry[k] for k in ('name', 'mode', 'variant', 'enabled', 'jitter', 'taa', 'bench', 'lazy', 'burst'))
            directory = BUILD / ('motion-output-' + name + '-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f'))
            directory.mkdir(parents=True)
            shutil.copy(EXE, directory)
            shutil.copy(SEAM if mode == 'seam' else DLL, directory / 'd3d9.dll')
            env = dict(os.environ, X3M_MOTION_OUTPUT=enabled, X3M_MOTION_JITTER='1' if jitter else '0', X3M_MOTION_JITTER_SAMPLES=str(JITTER_SAMPLES),
                       X3M_TAA='1' if taa else '0', X3M_TAA_DEBUG='1' if taa and not bench else '0',
                       X3M_CAPTURE_START='1000' if bench else str(BURST_CAPTURE[0]) if burst else '1',
                       X3M_CAPTURE_FRAMES='1' if bench else str(len(BURST_CAPTURE)) if burst else '8', X3M_TELEMETRY='1',
                       X3M_MOTION_RT_MODE='lazy' if lazy else 'perdraw', X3M_MOTION_FRAME_LOG='1' if burst else '60',
                       X3M_OWNERSHIP='0', X3M_DEPTH_COPY='0', X3M_SCENE_DEPTH_CAPTURE='0', X3M_OBJECT_TRACE='0', X3M_OBJECT_LIFETIME='0',
                       X3M_MESH_CACHE='0', X3M_ADMISSION='0', X3M_FINITE_POSITIONS='0', X3M_MOTION_CAPTURE='0')
            env.update(VARIANTS[variant])
            command = [str(WINE), '--bottle', 'Steam', '--no-update', '--dll', 'd3d9=n,b', '--workdir', str(directory),
                       str(directory / EXE.name)] + ['Z:' + str(p) for p in RAW] + ['burst' if burst else mode] + ([bench] if bench else [])
            no_game()
            wine_log.write(f'==== {name}\n'); wine_log.flush()
            completed = subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=wine_log, text=True, timeout=360)
            text = completed.stdout
            report.append(f'==== {name} exit={completed.returncode}\n{text}')
            traces = list((directory / 'x3-modern-captures').glob('session-*.log'))
            assert completed.returncode == 0 and len(traces) == 1, f'{name}: exit {completed.returncode}, traces {len(traces)}'
            trace = traces[0].read_text()
            if bench:
                case = validate_bench(name, taa, bench, text, trace)
                case.update(exit=completed.returncode, directory=str(directory.relative_to(ROOT)), trace_sha256=sha(traces[0]))
                result['bench'][name] = case
                save()
                print(f'{name}: exit={completed.returncode} boundary_ms={case["boundary_ms"]}', flush=True)
                continue
            if burst:
                case = validate_burst(name, mode, lazy, text, trace, directory)
                case.update(exit=completed.returncode, directory=str(directory.relative_to(ROOT)), trace_sha256=sha(traces[0]),
                            dll_sha256=sha(directory / 'd3d9.dll'), exe_sha256=sha(directory / EXE.name))
                shutil.copy(traces[0], RESULTS / f'motion-output-{name}-capture.log')
                result['cases'][name] = case
                save()
                print(f'{name}: exit={completed.returncode} checks={case["checks"]} set_rt={case["set_rt_per_frame"]}', flush=True)
                continue
            case = finish_case(name, mode, variant, enabled == '1', jitter, taa, text, trace, directory, lazy)
            case.update(exit=completed.returncode, directory=str(directory.relative_to(ROOT)), trace_sha256=sha(traces[0]),
                        dll_sha256=sha(directory / 'd3d9.dll'), exe_sha256=sha(directory / EXE.name))
            if enabled == '1':
                shutil.copy(traces[0], RESULTS / f'motion-output-{name}-capture.log')
            result['cases'][name] = case
            save()
            print(f'{name}: exit={completed.returncode} checks={case["checks"]} motion_pixels={case["motion_pixels"]}', flush=True)
        wine_log.close()
        # Resolve cost: the boundary with the switch on minus off, per size.
        for size in BENCH_SIZES:
            off, on = result['bench'][f'bench-{size}-taa-off']['boundary_ms'], result['bench'][f'bench-{size}-taa-on']['boundary_ms']
            result['bench'][f'resolve-{size}'] = {'median_ms': on['median'] - off['median'], 'min_ms': on['min'] - off['min'],
                                                  'boundary_off_median_ms': off['median'], 'boundary_on_median_ms': on['median']}
        # Color is bit-identical with the route off and on in every environment,
        # and the wrapper/depth/admission environments change nothing either.
        # With jitter on the raster moves: the colour must differ from the
        # unjittered run (the per-pixel coverage oracle above says by how much).
        for mode in ('production', 'seam'):
            reference = result['cases'][mode + '-off']['color_hashes']
            for variant in VARIANTS:
                prefix = mode + '-' if variant == 'plain' else f'{mode}-{variant}-'
                off, on = result['cases'][prefix + 'off']['color_hashes'], result['cases'][prefix + 'on']['color_hashes']
                assert off == on, f'{prefix}: color differs between route off and on'
                assert off == reference, f'{prefix}: color differs from the plain run'
            jittered = result['cases'][mode + '-jitter-on']['color_hashes']
            differing = [f for f in jittered if jittered[f] != reference[f]]
            assert len(differing) >= 6, f'{mode}-jitter-on: jitter changed the colour of only {len(differing)} of 12 frames'
            result['cases'][mode + '-jitter-on']['frames_differing_from_unjittered'] = differing
        assert result['cases']['production-jitter-on']['color_hashes'] == result['cases']['seam-jitter-on']['color_hashes'], 'jitter colour differs between the production and the seam DLL'
        # Lazy RT binding equivalence. Regular script: the lazy runs equal their
        # per-draw twins in colour, readback files, checks and restorations.
        # Burst script: colour, state signature, seam RT1/RT2 hashes and the
        # DLL's readback files agree between the modes; the SetRenderTarget
        # count per frame drops from 20 to 12 outside the capture frames.
        def readback_files(case_name):
            directory = ROOT / result['cases'][case_name]['directory'] / 'x3-modern-captures'
            return {p.name: sha(p) for p in sorted(directory.glob('*.rgba32f')) + sorted(directory.glob('*.r32f'))}
        equivalence = {'regular': {}, 'burst': {}}
        for lazy_name, twin in (('production-lazy-on', 'production-on'), ('seam-lazy-on', 'seam-on'),
                                ('seam-ownership-lazy-on', 'seam-ownership-on'), ('seam-taa-lazy-on', 'seam-taa-on')):
            a, b = result['cases'][lazy_name], result['cases'][twin]
            assert a['color_hashes'] == b['color_hashes'], f'{lazy_name}: colour differs from {twin}'
            assert (a['checks'], a['restorations'], a['motion_pixels'], a['matched_pixels'], a['depth_written_pixels']) == \
                   (b['checks'], b['restorations'], b['motion_pixels'], b['matched_pixels'], b['depth_written_pixels']), (lazy_name, twin)
            files_a, files_b = readback_files(lazy_name), readback_files(twin)
            assert files_a and files_a == files_b, f'{lazy_name}: readback files differ from {twin}'
            if 'color_hashes_before_boundary' in b:
                assert a['color_hashes_before_boundary'] == b['color_hashes_before_boundary'], (lazy_name, twin)
            equivalence['regular'][lazy_name] = {'twin': twin, 'readback_files': len(files_a), 'identical': True}
        for dll in ('production', 'seam'):
            per, lz = result['cases'][f'{dll}-burst-perdraw'], result['cases'][f'{dll}-burst-lazy']
            assert per['color_hashes'] == lz['color_hashes'], f'{dll}-burst: colour differs between the modes'
            assert per['state_hashes'] == lz['state_hashes'], f'{dll}-burst: final state differs between the modes'
            assert per['motion_hashes'] == lz['motion_hashes'], f'{dll}-burst: RT1/RT2 differ between the modes'
            assert per['readback_sha256'] == lz['readback_sha256'], f'{dll}-burst: readback files differ between the modes'
            assert all(per['set_rt_per_frame'][f] == BURST_SET_RT['perdraw'] for f in range(BURST_FRAMES)), per['set_rt_per_frame']
            assert all(lz['set_rt_per_frame'][f] == (BURST_SET_RT['perdraw'] if f in BURST_CAPTURE else BURST_SET_RT['lazy']) for f in range(BURST_FRAMES)), lz['set_rt_per_frame']
            equivalence['burst'][dll] = {'frames': BURST_FRAMES, 'capture_frames': list(BURST_CAPTURE), 'identical_color': True, 'identical_state': True,
                                         'identical_motion_depth': bool(per['motion_hashes']) or dll == 'production', 'identical_readback_files': True,
                                         'set_rt_per_frame': {'perdraw': per['set_rt_per_frame'], 'lazy': lz['set_rt_per_frame']},
                                         'lazy_flushes_per_frame': lz['lazy_flushes_per_frame']}
        result['lazy_equivalence'] = equivalence
        # TAA: the production DLL resolves current-only (sentinel routes), so
        # its presented image is bit-identical to the jittered run without the
        # resolve, plain and through the wrapper; the seam's frames without
        # history are identical too, and its history frames differ (the moving
        # edges blend the previous frame). The image before the boundary is the
        # jittered raster in every TAA run.
        jittered = result['cases']['production-jitter-on']['color_hashes']
        for prefix in ('production-taa-on', 'production-ownership-taa-on'):
            assert result['cases'][prefix]['color_hashes'] == jittered, f'{prefix}: current-only resolve changed the colour'
            assert result['cases'][prefix]['color_hashes_before_boundary'] == jittered, f'{prefix}: pre-boundary colour differs'
        for prefix in ('seam-taa-on', 'seam-ownership-taa-on'):
            hashes = result['cases'][prefix]['color_hashes']
            assert result['cases'][prefix]['color_hashes_before_boundary'] == jittered, f'{prefix}: pre-boundary colour differs'
            assert all(hashes[f] == jittered[f] for f in hashes if f not in SEAM_TAA_HISTORY), f'{prefix}: a no-history frame differs from the jittered raster'
            blended = [f for f in hashes if f in SEAM_TAA_HISTORY and hashes[f] != jittered[f]]
            assert blended, f'{prefix}: no history frame changed the colour'
            result['cases'][prefix]['frames_changed_by_history'] = blended
        assert result['cases']['seam-taa-on']['color_hashes'] == result['cases']['seam-ownership-taa-on']['color_hashes'], 'resolved colour differs through the wrapper'
        result['color_identical_off_vs_on'] = True
        result['color_identical_across_variants'] = True
        result['jitter_changes_color'] = True
        report_path.write_text(''.join(report))
        result['report_sha256'] = sha(report_path)
        assert sources() == result['sources_before_build'], 'Sources changed during run'
        assert all(sha(p) == h for p, h in RAW.items()), 'Local shader bytes changed during run'
        assert {str(p.relative_to(ROOT)): sha(p) for p in (EXE, SEAM, DLL)} == result['binaries'], 'Binaries changed during run'
        result['sources_after_run'] = sources()
        result['limits'] = ['Synthetic device program; not gameplay validation or temporal image quality.',
                            'Jitter is proven by coverage against a CPU reference at the Halton offsets on a 64x64 target.',
                            'The resolve is proven by byte-exact agreement with the same TemporalPass on a plain device from the same inputs, current-only frames by the 8-bit round trip; the reference is not an independent implementation of the resolve.',
                            'Bench timings are CPU-inclusive wall-clock times of the boundary StretchRect with EVENT synchronization on the Preview backend, not GPU timestamps.',
                            'Ownership modes wrap the synthetic device; the game observers stay inactive, so wrapper interaction is proven for fill, routing, Reset and release, not for object history.',
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
