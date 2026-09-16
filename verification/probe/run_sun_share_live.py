#!/usr/bin/env python3
"""Consume-only actual MotionOutput sun-lane qualification. Run under wine_lock.py.

Requires explicit prebuilt fixture/seam; never builds or changes those inputs.
The detached material producer fixture is independent and is not executed here.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import time
import bottle

ROOT = Path(__file__).resolve().parents[2]
CASES = ('positive', 'caps', 'cutout_drop', 'alpha_mask', 'allocation', 'late_shader', 'bind', 'untracked', 'composition', 'composition_missing', 'composition_failed',
         'xt_state', 'effects', 'xt_state_lane_off', 'cutout_pair', 'cutout_pair_bias')
# Bucket names of the sun_shadow_lane_refusals line (src/renderer/sun_share_frame.h, SunUntrackedReason order).
REASONS = ('unknown', 'feature', 'scene', 'unregistered', 'pair', 'no_zwrite', 'blended', 'state', 'rows',
           'geometry', 'no_depth', 'fade_arm', 'apply_failed', 'scope', 'history', 'read_failed')

def fields(line):
    return dict(re.findall(r'(\w+)=([^\s=]+)(?=\s|$)', line))

def compare_taa(readbacks, work, case):
    # Every frame's TAA output must equal the independent R32 reference byte for byte.
    for row in readbacks:
        frame = int(row['frame'])
        assert row['result'] == '00000000' and (int(row['width']), int(row['height'])) == (64, 64)
        actual = (work/'x3-modern-captures'/row['file']).read_bytes()
        expected = (work/f'reference_taa_{frame}.rgba16f').read_bytes()
        assert len(actual) == 64*64*8 and actual == expected, (case, frame, 'TAA differs from ordinary R32 reference')
        values = list(struct.iter_unpack('<4e', actual))
        assert all(all(math.isfinite(v) for v in pixel) for pixel in values)
        assert sum(any(v > 0 for v in pixel[:3]) for pixel in values) > 3600

def validate(text, trace, work, case):
    rows = [fields(line) for line in text.splitlines() if line.startswith('SUN_LIVE ')]
    assert len(rows) == 6 and [int(r['step']) for r in rows] == list(range(6))
    assert [int(r['frame']) for r in rows] == list(range(6))
    assert 'RESULT PASS ' in text and 'SUN_LIVE_PASS ' in text and 'FAIL' not in text
    assert text.count('RESET PASS') == 1 and 'RESTORE_DIFF' not in text
    early = case in ('caps', 'cutout_drop', 'alpha_mask', 'allocation')
    late = case in ('late_shader', 'bind')
    composition = case.startswith('composition')
    failed_coverage = case in ('composition_missing', 'composition_failed')
    lane_off = case == 'xt_state_lane_off'
    for i, row in enumerate(rows):
        lane = not early and not lane_off and not (late and i == 3) and not (case == 'late_shader' and i >= 4)
        available = lane and not (i == 2 and (late or case in ('untracked', 'cutout_pair') or failed_coverage))
        assert int(row['lane']) == lane and int(row['available']) == available
        assert int(row['history']) == (i not in ((0, 2, 3, 4) if failed_coverage else (0, 4)))
        assert int(row['drawn']) == (0 if lane_off else 3600)
        assert int(row['positive']) == (3600 if lane and i != 1 else 0)
        assert int(row['zero']) == (3600 if lane and i == 1 else 0)
        assert int(row['fault']) == (late and i == 2)
    histories = [fields(line) for line in text.splitlines() if line.startswith('SUN_HISTORY ')]
    # Existing passed positive/capability records predate this failure witness.
    # Late-fault and composition acceptance require explicit actual/reference
    # history, rather than losing the distinction in one combined assertion.
    if late or composition:
        assert len(histories) == 6 and [int(r['frame']) for r in histories] == list(range(6))
    for row in histories:
        frame = int(row['frame']); expected = int(rows[frame]['history'])
        assert [int(row[k]) for k in ('expected','reference','actual')] == [expected]*3
        # No routed draw with the lane off: the cut detector has no sample.
        assert int(row['fixture_cut']) == (frame in (0,4) and not lane_off)
    masks = [fields(line) for line in text.splitlines() if line.startswith('SUN_M ')]
    assert len(masks) == (6 if composition else 0)
    if composition:
        assert [int(r['frame']) for r in masks] == list(range(6))
        for i, row in enumerate(masks):
            bad = failed_coverage and i == 2
            draws = (0,1,1 if case == 'composition_missing' else 2,2,0,1)[i]
            excluded = 0 if bad else (0,768,1536,1536,0,768)[i]
            assert int(row['draws']) == draws and int(row['valid']) == (not bad)
            assert int(row['excluded']) == excluded
            assert int(row['eligible']) == (0 if bad else 3600-excluded)
            assert int(row['required']) == (draws > 0 and not (bad and case == 'composition_missing'))
            assert int(row['interleaved']) == (i == 3)
            assert int(row['stopped']) == bad
            assert int(row['incomplete']) == (bad and case == 'composition_failed')
            assert int(row['linear']) == (1 if bad and case == 'composition_failed' else 0 if bad else draws)
            assert int(row['exchanged']) == (2 if bad and case == 'composition_failed' else 0 if bad else draws)
    qualifications = [fields(line) for line in trace.splitlines() if line.startswith('sun_shadow_lane_device ')]
    if lane_off:
        # Option off: no lane qualification, self-test or publication at all,
        # and the XT-state receiver is refused at gate 4 every frame (the
        # tested-opaque arm is lane-only); the frame line proves nothing routed.
        assert not qualifications and not any(line.startswith(('sun_shadow_lane_', 'motion_output_depth_readback ')) for line in trace.splitlines())
    frames = [fields(line) for line in trace.splitlines() if line.startswith('motion_output_frame ')]
    if case in ('xt_state', 'xt_state_lane_off'):
        assert len(frames) == 6 and [int(r['frame']) for r in frames] == list(range(6)), frames
        assert all((int(r['routed']), int(r['gate4'])) == ((0, 1) if lane_off else (1, 0)) for r in frames), frames
    if lane_off:
        readbacks = [fields(line) for line in trace.splitlines() if line.startswith('motion_output_taa_readback ')]
        assert len(readbacks) == 6 and {int(r['frame']) for r in readbacks} == set(range(6))
        compare_taa(readbacks, work, case)
        states = [fields(line) for line in text.splitlines() if line.startswith('SUN_XT_STATE ')]
        assert len(states) == 6 and all((int(r['frame']), r['test'], r['ref'], r['func'], r['mask'], r['lane'], r['routed'], r['gate4']) == (i, '1', '1', '7', '7', '0', '0', '1') for i, r in enumerate(states)), states
        return dict(frames=6, histories=4, resets=1, exact_taa_frames=6, refusal_frames=0, writer_signatures=0,
                    composition_frames=0, mask_union_pixels=0, positive_frames=0, zero_frames=0, negative_selftest=False, lane_off=True)
    assert len(qualifications) == 2
    assert [int(r['qualified']) for r in qualifications] == ([1,0] if case == 'late_shader' else [int(case not in ('caps','cutout_drop','alpha_mask'))]*2)
    if case == 'late_shader': assert qualifications[1]['reason'] == 'shader_cache'
    depths = [fields(line) for line in trace.splitlines() if line.startswith('sun_shadow_lane_depth ')]
    if case == 'late_shader': assert len(depths) == 1 # Reset refuses incomplete cache before GPU self-test.
    if case in ('cutout_drop', 'alpha_mask'):
        # These deliberately broken GPU executions MUST fail the pixel oracle,
        # rather than merely forcing a final status bit to unavailable.
        assert depths and all(r['qualified'] == '0' and r.get('stage') == 'cutout_pass' and r.get('result') == '80004005' and r.get('restore') == '00000000' for r in depths)
    elif not early or case == 'allocation':
        assert depths and all(r['qualified'] == '1' and r.get('stage') == 'history_r' and r.get('checks') == '18' for r in depths)
    readbacks = [fields(line) for line in trace.splitlines() if line.startswith('motion_output_taa_readback ')]
    assert len(readbacks) == 6 and {int(r['frame']) for r in readbacks} == set(range(6))
    publications = [fields(line) for line in trace.splitlines() if line.startswith('sun_shadow_lane_frame ')]
    assert len(publications) == 6
    for row in publications:
        i = int(row['frame'])
        assert int(row['available']) == int(rows[i]['available']) and int(row['owner']) == (not (failed_coverage and i == 2))
        if composition:
            assert row['exclusion_required'] == masks[i]['required']
            if int(row['exclusion_required']): assert int(row['exclusion_valid']) == int(masks[i]['valid'])
        if case == 'untracked' and i == 2:
            assert int(row['untracked_writers']) > 0 and int(row['receiver_draws']) > 0
        # Only actual depth writers veto: a color-only draw after the receiver
        # is counted non_depth_writers and never fails the frame. The effects
        # case draws one (frame available); the failed-composition cases' frame-2
        # emitter (blend on, z write off, coverage failed) is one too, and that
        # frame is unavailable through owner=0 alone. cutout_pair_bias' frame-2
        # refusal probe (the cutout pair with z write off) is the third. No
        # other case draws one.
        assert 'non_depth_writers' in row, ('build without the non-writer counter', row)
        assert int(row['non_depth_writers']) == int((case in ('effects', 'cutout_pair_bias') or failed_coverage) and i == 2), (case, row)
        if case == 'xt_state' and int(rows[i]['lane']):
            assert int(row['receiver_draws']) >= 1 and int(row['untracked_writers']) == 0, (case, row)
        if late and i == 2:
            assert row['failed'] == '1' and int(row['receiver_draws']) > 0
    # Refusal diagnostics: exactly one bucket line per frame with untracked
    # writers, buckets summing to the total, and the fixture's writer (an
    # unreviewed PS alteration drawn with z write on, an actual depth writer)
    # identified by the first refusing gate (pair) and by a cached signature
    # line that still carries the z-write state.
    refusals = {int(r['frame']): r for r in (fields(line) for line in trace.splitlines() if line.startswith('sun_shadow_lane_refusals '))}
    writers = [fields(line) for line in trace.splitlines() if line.startswith('sun_shadow_lane_writer ')]
    refused = {int(r['frame']) for r in publications if int(r['untracked_writers'])}
    assert set(refusals) == refused, (sorted(refusals), sorted(refused))
    assert {int(w['frame']) for w in writers} <= refused, 'writer signature outside a refusal frame'
    for frame, row in refusals.items():
        untracked = int(row['untracked'])
        assert untracked == int(next(p['untracked_writers'] for p in publications if int(p['frame']) == frame))
        assert all(name in row for name in REASONS), ('truncated refusal line', row)
        assert sum(int(row[name]) for name in REASONS) == untracked, row
        assert int(row['signatures']) + int(row['overflow']) >= 1 and int(row['signatures']) <= 64
    if case == 'untracked':
        # The vetoing writer is an unreviewed PS drawn with z write ON: an
        # actual depth writer the lane did not track.
        assert refused == {2} and int(refusals[2]['pair']) == int(refusals[2]['untracked']) > 0
        assert any(w['reason'] == 'pair' and w['gate'] == '3' and w['registered'] == '1' and w['z'] == '1' and w['zwrite'] == '1' and w['z_known'] == '1' and int(w['frame']) == 2 for w in writers), writers
    elif case in ('xt_state', 'effects', 'cutout_pair_bias'):
        # cutout_pair_bias (run 28 session B): the cutout pair in its exact
        # state under a nonzero mip bias is tracked by the tested-opaque arm,
        # so no frame has an untracked writer or a signature line.
        assert not refused and not writers, (case, refusals, writers)
    elif case == 'cutout_pair':
        # A cutout pair by identity in mask-7/test-off state stays a gate-4
        # state refusal with the lane on: an untracked depth writer.
        assert refused == {2} and int(refusals[2]['state']) == int(refusals[2]['untracked']) > 0, refusals
        assert any(w['reason'] == 'state' and w['gate'] == '4' and w['registered'] == '1' and w['ps'] == '63f96eba9eea7880' and w['z'] == '1' and w['zwrite'] == '1' and w['z_known'] == '1' and int(w['frame']) == 2 for w in writers), writers
    elif not refused:
        assert not writers
    # Case witnesses printed by the fixture: the receiver's XT class-C draw
    # state (alpha test on, ALPHAREF 1, GREATEREQUAL, RT0 mask 7) on every
    # frame, and the blended non-depth effects draw on frame 2.
    states = [fields(line) for line in text.splitlines() if line.startswith('SUN_XT_STATE ')]
    assert len(states) == (6 if case == 'xt_state' else 0), states
    assert all((int(r['frame']), r['test'], r['ref'], r['func'], r['mask'], r['lane'], r['routed'], r['gate4']) == (i, '1', '1', '7', '7', '1', '1', '0') for i, r in enumerate(states)), states
    cutouts = [fields(line) for line in text.splitlines() if line.startswith('SUN_CUTOUT_PAIR ')]
    assert len(cutouts) == (1 if case == 'cutout_pair' else 0), cutouts
    assert all((r['frame'], r['ps'], r['test'], r['mask'], r['gate4']) == ('2', '63f96eba9eea7880', '0', '7', '1') for r in cutouts), cutouts
    biases = [fields(line) for line in text.splitlines() if line.startswith('SUN_CUTOUT_BIAS ')]
    assert len(biases) == (1 if case == 'cutout_pair_bias' else 0), biases
    assert all((r['frame'], r['ps'], r['test'], r['ref'], r['mask'], r['bias'], r['routed'], r['gate4'], r['untracked'], r['stage_bias']) == ('2', '63f96eba9eea7880', '1', '1', '7', '-0.5', '1', '0', '0', '00000000') for r in biases), biases
    # Tested-opaque-arm admission telemetry (linear_material_frame
    # cutout_opaque_*): on the mip-bias frame the exact arm is unconfigured, so
    # the admitted cutout pair is one routed draw with its lane share written
    # and the z-write-off one is a single no_zwrite refusal.
    opaque = [fields(line) for line in text.splitlines() if line.startswith('SUN_CUTOUT_OPAQUE ')]
    assert len(opaque) == (1 if case == 'cutout_pair_bias' else 0), opaque
    assert all((r['frame'], r['routed'], r['lane'], r['refused'], r['no_zwrite'], r['state'], r['untracked'])
               == ('2', '1', '1', '1', '1', '0', '0') for r in opaque), opaque
    effects = [fields(line) for line in text.splitlines() if line.startswith('SUN_EFFECTS ')]
    assert len(effects) == (1 if case == 'effects' else 0), effects
    assert all((r['frame'], r['depth_write'], r['blend']) == ('2', '0', '1') for r in effects), effects
    # Writer-line grammar: the gate-4 values the chain read (-1 when gate 4 was
    # not reached), cutout-pair identity and the exact-arm latch on every line.
    for w in writers:
        assert all(name in w for name in ('test', 'mask', 'srgb', 'cutout_pair', 'arm')), ('writer line without the gate-4 state fields', w)
        assert (w['test'], w['mask'], w['srgb'], w['cutout_pair'], w['arm']) == (('0', '7', '0', '1', '1') if case == 'cutout_pair' else ('-1', '-1', '-1', '0', '1')), (case, w)
    signatures = {(w['vs'], w['ps'], w['reason'], w['declaration'], w['stride'], w['z'], w['zwrite'], w['registered']) for w in writers}
    assert len(signatures) == len(writers), 'signature logged twice'
    compare_taa(readbacks, work, case)
    return dict(frames=6, histories=2 if failed_coverage else 4, resets=1, exact_taa_frames=6,
                refusal_frames=len(refusals), writer_signatures=len(writers),
                composition_frames=len(masks), mask_union_pixels=sum(int(r['excluded']) for r in masks),
                positive_frames=sum(int(r['positive']) > 0 for r in rows),
                zero_frames=sum(int(r['zero']) > 0 for r in rows),
                negative_selftest=case in ('cutout_drop', 'alpha_mask'))

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--dll', type=Path, required=True)
    parser.add_argument('--programs', type=Path, default=Path('/tmp/x3-shader-sweep/programs'))
    parser.add_argument('--case', choices=CASES, action='append')
    parser.add_argument('--result', type=Path)
    args = parser.parse_args()
    assert os.environ.get('X3M_FIXTURE_BOTTLE') == 'X3' and bottle.BOTTLE == 'X3', 'Set X3M_FIXTURE_BOTTLE=X3 explicitly'
    fixture, dll = args.fixture.resolve(), args.dll.resolve()
    selected = args.case or CASES
    names = ['vs_53a0a641107ed76c.bin', 'ps_8759c7838bbc86c2.bin']
    if any(case.startswith('composition') for case in selected): names += ['vs_089091aab2d5eb13.bin', 'ps_8559522220507d5e.bin']
    if 'cutout_pair' in selected or 'cutout_pair_bias' in selected: names += ['ps_63f96eba9eea7880.bin']
    programs = [args.programs.resolve()/name for name in names]
    inputs = {str(p): sha(p) for p in (fixture, dll, *programs)}
    raw = Path(tempfile.mkdtemp(prefix='x3-sun-share-live-'))
    result = args.result or bottle.results_dir(ROOT)/'sun-share-live.json'
    report = dict(passed=False, bottle=bottle.describe(), raw=str(raw), inputs=inputs, game_launched=False, cases={})
    try:
        for case in selected:
            work = raw/case; work.mkdir()
            shutil.copy2(fixture, work/'fixture.exe'); shutil.copy2(dll, work/'d3d9.dll')
            env = {k:v for k,v in os.environ.items() if not k.startswith('X3M_')}
            env.update(X3M_MOTION_OUTPUT='1', X3M_TAA='1', X3M_HDR='1', X3M_HDR_TONEMAP='agx', X3M_HDR_DECODE='gamma2.2',
                       X3M_HDR_EXPOSURE='manual', X3M_HDR_EV_MANUAL='0', X3M_HDR_CLAMP='0', X3M_HDR_BLOOM='0',
                       X3M_LINEAR_MATERIALS='1', X3M_MATERIAL_DIRECT_GAIN='1', X3M_MATERIAL_EMISSIVE_GAIN='1', X3M_LIGHTMAP_EMISSIVE_GAIN='1',
                       X3M_SUN_SHADOW_LANE='1', X3M_TAA_SENTINEL='1', X3M_TAA_SHARPEN='0', X3M_TAA_MIP_BIAS='0',
                       X3M_SCENE_HOOK='0', X3M_OWNERSHIP='0', X3M_TELEMETRY='1', X3M_MOTION_FRAME_LOG='1',
                       X3M_CAPTURE_START='1', X3M_CAPTURE_FRAMES='0', X3M_MOTION_RT_MODE='perdraw', X3M_STATE_SHADOW='1',
                       X3M_FIXTURE_SUN_LIVE_CASE=case, WINEDLLOVERRIDES='d3d9=n,b')
            if case.startswith('composition'):
                env.update(X3M_LINEAR_EMISSIONS='1', X3M_EMISSION_GAIN='1')
            if case == 'xt_state_lane_off':
                env['X3M_SUN_SHADOW_LANE'] = '0'
            if case == 'cutout_pair_bias':
                # Run 28 session B's configuration: a nonzero TAA mip bias leaves the
                # exact cutout arm unconfigured (the fixture textures are single-level,
                # so no stage is actually biased).
                env['X3M_TAA_MIP_BIAS'] = '-0.5'
            if case in ('caps', 'cutout_drop', 'alpha_mask', 'allocation'):
                env['X3M_FIXTURE_SUN_LANE_FAULT'] = case
            command = [bottle.WINE, *bottle.wine_args(), '--dll', 'd3d9=n,b', '--workdir', str(work), str(work/'fixture.exe'),
                       *('Z:'+str(p) for p in programs[:2]), 'sunlane']
            start = time.monotonic()
            with (work/'stdout.txt').open('w') as out, (work/'wine.log').open('w') as error:
                completed = subprocess.run(command, env=env, stdout=out, stderr=error, timeout=180)
            assert completed.returncode == 0, (case, completed.returncode, str(work))
            logs = list((work/'x3-modern-captures').glob('session-*.log')); assert len(logs) == 1
            check = validate((work/'stdout.txt').read_text(), logs[0].read_text(), work, case)
            report['cases'][case] = dict(check, elapsed_seconds=time.monotonic()-start, command=command)
        assert all(sha(Path(p)) == digest for p,digest in inputs.items()), 'prebuilt inputs changed'
        report['passed'] = True
    finally:
        destination = result if report['passed'] else raw/'failed-result.json'
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(json.dumps(report, indent=2)+'\n')
        print(json.dumps(report, indent=2))

if __name__ == '__main__':
    main()
