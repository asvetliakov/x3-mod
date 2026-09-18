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
import sun_shadow_apply as sun_apply  # the bias law (resolve_bias) for the shadow_apply record

ROOT = Path(__file__).resolve().parents[2]
# sun_shadow_apply_frame skip reasons (motion_output.cpp run_sun_shadow_apply and
# SunShadowApplyPass::execute; legacy-sun-application.md section 2).
APPLY_SKIP_REASONS = frozenset(('none', 'lane', 'replay', 'owner', 'depth', 'recording', 'queries', 'camera', 'target', 'attach',
                                'reset_pending', 'depth_container', 'bias', 'failed', 'detached', 'input', 'params', 'format', 'device',
                                'sun', 'basis', 'rows', 'cascades', 'absent'))  # the cascade branch's own
CASES = ('positive', 'caps', 'cutout_drop', 'alpha_mask', 'allocation', 'late_shader', 'bind', 'untracked', 'composition', 'composition_missing', 'composition_failed',
         'xt_state', 'effects', 'xt_state_lane_off', 'cutout_pair', 'cutout_pair_bias', 'original_lane', 'shadow_apply', 'original_share_refused', 'hull_emission',
         'shadow_apply_cascades', 'shadow_apply_linear', 'shadow_apply_cascades_linear')
# shadow_apply_linear / shadow_apply_cascades_linear: the same scripts under the receiver-depth
# option (X3M_SUN_SHADOW_RECEIVER_DEPTH=linear, docs/architecture/shadow-receiver-depth.md): the
# lane's RT2 is A32B32G32R32F (116) and the params lines say depth_encoding=linear; the plain
# cases keep G32R32F (115), the shipping default. Both are validated by the shadow_apply checks.
APPLY_CASES = ('shadow_apply', 'shadow_apply_cascades', 'shadow_apply_linear', 'shadow_apply_cascades_linear')


def apply_base(case):
    """The script a case runs: the -linear siblings run shadow_apply / shadow_apply_cascades."""
    return case[:-len('_linear')] if case.endswith('_linear') else case
# shadow_apply_cascades (docs/architecture/shadow-cascades.md): the shadow_apply
# script and its validation unchanged, with two cascades through the DLL's own
# wiring (the seam narrows them to 32 and 256 units; cascade 0 is the single
# map's box, so the CPU-shadowed reference stands) and the capture window open:
# one shadow_map<i> readback and one shadow_replay_map_basis line per cascade,
# the sun_shadow_apply_params line with per-cascade rows, bias and texel, and
# the cascade twin on those dumps.
CASCADE_LIVE_ENV = dict(X3M_SHADOW_CASCADES='250,1500', X3M_FIXTURE_SHADOW_CASCADES='32,256', X3M_SHADOW_CASCADE_SIZES='512', X3M_CAPTURE_FRAMES='8',
                        X3M_LINEAR_MATERIALS='0')  # original shading, as shadow_apply
# hull_emission (emitter plan phase 3, hull_emission_live_inc.h): the covered
# standard_lighting pair vs_494fe349b8bc12ec / ps_7c83ed50c9894e44 under the
# HDR scene with X3M_HULL_EMISSION_GAIN=2 (variant created, the ONE/ONE draw
# admitted at 2 x base, opaque and screen draws refused) and then unset (no
# variant, no admission, every frame at the base). Sun lane, linear materials
# and TAA off; the fixture mode is `hullemission`.
HULL_PROGRAMS = ('vs_494fe349b8bc12ec.bin', 'ps_7c83ed50c9894e44.bin')
HULL_PROGRAM_INDEX = 7  # ps_7c83ed50c9894e44 in linear_emission_hull_program_index order
HULL_GAIN = 2.0
# original_lane / shadow_apply run without linear materials (docs/architecture/
# legacy-sun-application.md sections 4.1-4.2 and 3.4): the original share
# producer, the cutout pair through the tested-opaque arm, and the scene-end
# apply quad over the depth replay of the same frame.
ORIGINAL_CASES = ('original_lane', 'shadow_apply', 'original_share_refused')  # by apply_base(case)
# Bucket names of the sun_shadow_lane_refusals line (src/renderer/sun_share_frame.h, SunUntrackedReason order).
REASONS = ('unknown', 'feature', 'scene', 'unregistered', 'pair', 'no_zwrite', 'blended', 'state', 'rows',
           'geometry', 'no_depth', 'fade_arm', 'apply_failed', 'scope', 'history', 'read_failed')

def fields(line):
    return dict(re.findall(r'(\w+)=([^\s=]+)(?=\s|$)', line))

def fp16_codes(data):
    # FP16 bit patterns as signed code indices (monotonic in value, so one code = one ULP).
    return [(c ^ 0x7fff) - 0x8000 if c & 0x8000 else c for c in struct.unpack(f'<{len(data)//2}H', data)]

def compare_taa(readbacks, work, case):
    # Returns the number of frames whose every pixel matched the reference byte for byte.
    # Every frame's TAA output must equal the independent R32 reference byte for
    # byte. shadow_apply: the reference is the CPU-shadowed scene and the
    # fixture's per-pixel mask says exact (0), within one FP16 code (1: history
    # of an earlier shadowed frame) or excluded (2: the footprint edge band).
    exact = 0
    for row in readbacks:
        frame = int(row['frame'])
        assert row['result'] == '00000000' and (int(row['width']), int(row['height'])) == (64, 64)
        actual = (work/'x3-modern-captures'/row['file']).read_bytes()
        expected = (work/f'reference_taa_{frame}.rgba16f').read_bytes()
        assert len(actual) == 64*64*8
        exact += actual == expected
        if case == 'shadow_apply':
            mask = (work/f'apply_mask_{frame}.u8').read_bytes()
            assert len(mask) == 64*64, (case, frame, 'mask size')
            a, e = fp16_codes(actual), fp16_codes(expected)
            worst = 0
            for pixel, kind in enumerate(mask):
                if kind == 2: continue
                for c in range(4):
                    delta = abs(a[pixel*4+c] - e[pixel*4+c])
                    assert delta <= (1 if kind == 1 else 0), (case, frame, pixel, c, kind, 'TAA differs from the CPU-shadowed reference')
                    worst = max(worst, delta)
            assert not any(a[i] != e[i] for i in range(len(a)) if mask[i//4] == 0), (case, frame)
        else:
            assert actual == expected, (case, frame, 'TAA differs from ordinary R32 reference')
        values = list(struct.iter_unpack('<4e', actual))
        assert all(all(math.isfinite(v) for v in pixel) for pixel in values)
        assert sum(any(v > 0 for v in pixel[:3]) for pixel in values) > 3600
    return exact

def validate_original(text, trace, case, publications):
    # No converted material anywhere: the share comes from the original share
    # producer (one sun_shadow_original_variant line per registered original,
    # share_applied=1), the lane line reports the producer totals.
    lines = trace.splitlines()
    assert not any(l.startswith(('linear_material_variant ', 'linear_material_frame ', 'sun_shadow_lane_variant ')) for l in lines), case
    originals = [fields(l) for l in lines if l.startswith('sun_shadow_original_variant ')]
    refused = case == 'original_share_refused'
    if refused:
        # Fail closed: the producer refused (share_applied=0, nothing created), every
        # frame's lane failed with the routed draw counted, the fill kept.
        assert len(originals) >= 1 and all(r['share_applied'] == '0' and r['create'] != '00000000' for r in originals), originals
        witnesses = [fields(l) for l in text.splitlines() if l.startswith('SUN_SHARE_REFUSED ')]
        assert [int(r['frame']) for r in witnesses] == list(range(6)) and all((r['refused_draws'], r['fill_draws'], r['failed']) == ('1', '1', '0') for r in witnesses), witnesses
        fills = [fields(l) for l in lines if l.startswith('original_fill_frame ')]
        assert len(fills) == 6 and all(r['admitted'] == '1' and r['fill'] == '0.05' for r in fills), fills
    else:
        assert len(originals) >= (2 if case == 'original_lane' else 1) and all(r['share_applied'] == '1' and r['create'] == '00000000' and r['transform'] == '0' for r in originals), (case, originals)
    if case == 'original_lane': assert {r['original'] for r in originals} >= {'8759c7838bbc86c2', '63f96eba9eea7880'}, originals
    for row in publications:
        assert int(row['original_variants']) == (0 if refused else len(originals)) and int(row['original_refused']) == (len(originals) if refused else 0), (case, row)
        assert int(row['original_refused_draws']) == int(refused), (case, row)
        if refused: assert (row['failed'], row['available']) == ('1', '0'), row
        assert int(row['cutout_opaque_routed']) == int(case == 'original_lane' and int(row['frame']) == 2), (case, row)
        assert int(row['cutout_opaque_lane']) == int(row['cutout_opaque_routed']) and row['cutout_opaque_refused'] == '0', (case, row)
    cutouts = [fields(l) for l in text.splitlines() if l.startswith('SUN_ORIGINAL_CUTOUT ')]
    assert len(cutouts) == (1 if case == 'original_lane' else 0), cutouts
    assert all((r['frame'], r['ps'], r['test'], r['mask'], r['routed'], r['gate4'], r['opaque_routed'], r['opaque_lane'], r['opaque_refused'], r['untracked'], r['refused'])
               == ('2', '63f96eba9eea7880', '1', '7', '1', '0', '1', '1', '0', '0', '0') for r in cutouts), cutouts
    if case != 'shadow_apply': return {}
    # The apply quad: attached once per device epoch (two epochs: the Reset),
    # one line per frame, drawn exactly on the frames with the lane available,
    # a replayed map and the owner; frame 0 (no sun: replay refused) and frame
    # 2 (untracked writer: lane unavailable) skip and stay byte-identical.
    devices = [fields(l) for l in lines if l.startswith('sun_shadow_apply_device ')]
    assert 1 <= len(devices) <= 2 and all(r['attached'] == '1' and r['reason'] == 'ok' for r in devices), devices  # attached once; the programs survive Reset
    modes = [l for l in lines if l.startswith('sun_shadow_apply_mode ')]
    assert len(modes) == 1 and modes[0].startswith('sun_shadow_apply_mode requested=1 enabled=1 lane=1 replay=1 linear_materials=0'), modes
    # The receiver-depth option (shadow-receiver-depth.md): with the DLL's sun_shadow_receiver_depth line
    # (X3M_SUN_SHADOW_RECEIVER_DEPTH=linear) the lane's RT2 is A32B32G32R32F (116) on every published frame
    # and every params line names the linear encoding; without it (a trace before the option) G32R32F (115).
    receiver = [fields(l) for l in lines if l.startswith('sun_shadow_receiver_depth ')]  # logged whenever the variable is set, either value
    assert len(receiver) <= 1 and all(r['lane'] == '1' and r['enabled'] == str(int(r['requested'] == 'linear')) for r in receiver), receiver
    linear = bool(receiver) and receiver[0]['requested'] == 'linear'
    lane_frames = [fields(l) for l in lines if l.startswith('sun_shadow_lane_frame ')]
    formats = sorted({r['format'] for r in lane_frames if 'format' in r})  # the synthetic host traces abbreviate the line
    assert formats in ([], ['116' if linear else '115']), (linear, formats)
    params_lines = [fields(l) for l in lines if l.startswith('sun_shadow_apply_params ')]
    assert all(r.get('depth_encoding', 'device') == ('linear' if linear else 'device') for r in params_lines), sorted({r.get('depth_encoding') for r in params_lines})
    mode = fields(modes[0])
    applies = [fields(l) for l in lines if l.startswith('sun_shadow_apply_frame ')]
    assert [int(r['frame']) for r in applies] == list(range(6)), applies
    assert all(r['skip_reason'] in APPLY_SKIP_REASONS for r in applies), [r['skip_reason'] for r in applies]
    replays = {int(r['frame']): r for r in (fields(l) for l in lines if l.startswith('shadow_replay_depth '))}
    assert set(replays) == set(range(6)), sorted(replays)
    expected = {0: 'replay', 1: 'none', 2: 'lane', 3: 'none', 4: 'none', 5: 'none'}
    for row in applies:
        i = int(row['frame'])
        assert row['skip_reason'] == expected[i] and int(row['applied']) == (expected[i] == 'none'), (row, expected[i])
        assert row['exponent'].startswith('1.0000') and row['result'] == ('00000000' if int(row['applied']) else '00000001'), row
        if int(row['applied']): assert row['map'] == '512' and float(row['us']) > 0, row
        assert (int(replays[i]['replayed']) == 2) == (i != 0) and int(replays[i]['draws']) == 2 and int(replays[i]['skipped_lease']) == (2 if i == 0 else 0), (i, replays[i])
        assert int(row['applied']) <= int(replays[i]['replayed']), (row, replays[i])
        assert int(row['applied']) <= int(publications[i]['available']), (row, publications[i])
    witnesses = [fields(l) for l in text.splitlines() if l.startswith('SUN_APPLY ')]
    assert [int(r['frame']) for r in witnesses] == list(range(6)), witnesses
    for r in witnesses:
        i = int(r['frame'])
        assert (r['applied'], r['attempted'], r['expect_applied']) == (str(int(expected[i] == 'none')), '1', str(int(expected[i] == 'none'))), r
        assert (int(r['replayed']) > 0) == (i != 0) and int(r['inner']) >= (400 if i >= 3 else 0) and int(r['changed']) >= (400 if i >= 3 else 0), r
    # The bias this case ran under: the mode line's world-unit inputs and their
    # resolution by the law (sun_shadow_apply.resolve_bias) on the case's
    # 32 / 64 / 512 cascade; the DLL prints the resolved values only on capture
    # frames, which this script has none of.
    bias_units, clamp_texels = float(mode.get('bias_units', sun_apply.BIAS_UNITS_DEFAULT)), float(mode.get('clamp_texels', sun_apply.BIAS_CLAMP_TEXELS))
    return dict(apply_frames=sum(int(r['applied']) for r in applies), apply_skipped={i: expected[i] for i in expected if expected[i] != 'none'},
                receiver_depth='linear' if linear else 'device', rt2_format=int(formats[0]) if formats else None,
                apply_us_max=max(float(r['us']) for r in applies), shadowed_pixels_min=min(int(r['inner']) for r in witnesses if int(r['frame']) >= 3),
                bias=dict(bias_units=bias_units, clamp_texels=clamp_texels, cascade=dict(extent=32.0, depth_half=64.0, size=512),
                          resolved_by_law=sun_apply.resolve_bias(bias_units, 32.0, 64.0, 512, clamp_texels)))

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
        available = lane and not (i == 2 and (late or case in ('untracked', 'cutout_pair', 'shadow_apply') or failed_coverage)) and case != 'original_share_refused'
        assert int(row['lane']) == lane and int(row['available']) == available
        assert int(row['history']) == (i not in ((0, 2, 3, 4) if failed_coverage else (0, 4)))
        assert int(row['drawn']) == (0 if lane_off else 3600)
        if case != 'original_share_refused':  # no share is written: the values are the motion variant's
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
        exact_frames = compare_taa(readbacks, work, case)
        states = [fields(line) for line in text.splitlines() if line.startswith('SUN_XT_STATE ')]
        assert len(states) == 6 and all((int(r['frame']), r['test'], r['ref'], r['func'], r['mask'], r['lane'], r['routed'], r['gate4']) == (i, '1', '1', '7', '7', '0', '0', '1') for i, r in enumerate(states)), states
        assert exact_frames == 6
        return dict(frames=6, histories=4, resets=1, exact_taa_frames=exact_frames, refusal_frames=0, writer_signatures=0,
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
        if case in ('untracked', 'shadow_apply') and i == 2:
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
    if case in ('untracked', 'shadow_apply'):
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
        # arm: the exact cutout arm is configured only with linear materials (original shading: 0).
        assert (w['test'], w['mask'], w['srgb'], w['cutout_pair'], w['arm']) == (('0', '7', '0', '1', '1') if case == 'cutout_pair' else ('-1', '-1', '-1', '0', '0' if case in ORIGINAL_CASES else '1')), (case, w)
    signatures = {(w['vs'], w['ps'], w['reason'], w['declaration'], w['stride'], w['z'], w['zwrite'], w['registered']) for w in writers}
    assert len(signatures) == len(writers), 'signature logged twice'
    exact_frames = compare_taa(readbacks, work, case)
    assert exact_frames >= (3 if case == 'shadow_apply' else 6), (case, exact_frames)  # shadow_apply: frames 0-2 exact, 3-5 within one code
    original = validate_original(text, trace, case, publications) if case in ORIGINAL_CASES else {}
    return dict(frames=6, histories=2 if failed_coverage else 4, resets=1, exact_taa_frames=exact_frames,
                refusal_frames=len(refusals), writer_signatures=len(writers),
                composition_frames=len(masks), mask_union_pixels=sum(int(r['excluded']) for r in masks),
                positive_frames=sum(int(r['positive']) > 0 for r in rows),
                zero_frames=sum(int(r['zero']) > 0 for r in rows),
                negative_selftest=case in ('cutout_drop', 'alpha_mask'), linear_materials=case not in ORIGINAL_CASES, **original)

def validate_hull_emission(text, trace, gain):
    # Fixture: five frames on B (opaque, additive, screen, additive after the
    # F4 action switched the population off, additive after it switched it on
    # again); an admitted additive frame is gain x base within one FP16 code,
    # every other frame the base, alpha native.
    lines = text.splitlines()
    assert any(l.startswith('RESULT PASS ') for l in lines) and not any(l.startswith('RESULT FAIL') for l in lines), 'native fixture completion'
    rows = [fields(l) for l in lines if l.startswith('HULL_EMISSION ')]
    assert [(r['kind'], int(r['frame'])) for r in rows] == [('opaque', 0), ('additive', 1), ('screen', 2), ('additive_off', 3), ('additive_on', 4)], rows
    for r in rows:
        assert float(r['gain']) == gain and int(r['mismatches']) == 0 and int(r['alpha_mismatches']) == 0 and int(r['max_codes']) <= 1, r
        assert int(r['pixels']) >= 100 and int(r['positive']) == 3 * int(r['pixels']), r
        assert float(r['factor']) == (gain if r['kind'] in ('additive', 'additive_on') else 1.0), r
    assert not any(l.startswith('HULL_EMISSION_DIFF ') for l in lines)
    toggles = [(int(r['frame']), int(r['state'])) for r in (fields(l) for l in lines if l.startswith('HULL_EMISSION_TOGGLE '))]
    assert toggles == ([(3, -1), (4, -1)] if gain == 1.0 else [(3, 0), (4, 1)]), toggles
    # DLL: one whole-output variant of the covered program at the gain, the
    # ONE/ONE draw admitted (programs bit 7) and the two other draws refused
    # on blend state; with the option off, no hull line at all.
    traces = trace.splitlines()
    variants = [fields(l) for l in traces if l.startswith('hull_emission_variant ')]
    frames = [fields(l) for l in traces if l.startswith('hull_emission_frame ')]
    programs = [fields(l) for l in traces if l.startswith('hull_emission_program ')]
    modes = [l for l in traces if l.startswith('hull_emission_gain_mode ')]
    draws = [fields(l) for l in traces if l.startswith('hull_emission_draw ')]
    routes = [fields(l) for l in traces if l.startswith('motion_route ') and l.split(' ps=')[1][:16] == '7c83ed50c9894e44']
    # The capture window opens at frame 1 (capture_start counts Presents), so
    # the per-draw record covers the four blended draws of the covered pair:
    # none is routed, with the option on or off (gate 4 refuses blending).
    # Frame 0's blend-off draw is the routed one; its witness is the hull
    # accounting (opaque=1, refused_routed=0) and, across the two gains, the
    # identical colour hash of that frame (checked by the caller).
    assert [(int(r['frame']), r['routed'], r['blend']) for r in routes] == [(1, '0', '1'), (2, '0', '1'), (3, '0', '1'), (4, '0', '1')], routes
    if gain == 1.0:
        assert not variants and not frames and not programs and not modes and not draws, (variants, frames, programs, modes, draws)
        # The refused F4 action is the only hull line of an option-off run.
        hull = [l for l in traces if l.startswith('hull_emission')]
        assert len(hull) == 2 and all(l.startswith('hull_emission_gain_toggle ') and ' accepted=0 enabled=1 requested=0 ' in l for l in hull), hull
    else:
        # No effects gain in this run: the hull population stands alone.
        assert modes == ['hull_emission_gain_mode requested=1 enabled=1 source_gain=1 gain=%g gain_valid=1' % gain], modes
        assert not any(l.startswith('emission_source_gain') for l in traces)
        assert len(variants) == 1 and (variants[0]['original'], int(variants[0]['program']), variants[0]['transform'], variants[0]['create']) == ('7c83ed50c9894e44', HULL_PROGRAM_INDEX, '0', '00000000'), variants
        assert float(variants[0]['gain']) == gain, variants
        # Frame 0's blend-off draw of the reviewed pair is the one the motion
        # route takes: the hull gain counts it opaque, never routed (the
        # routed pair keeps its bytes); the screen draw is refused on blend
        # state; frame 3 (population off) never enters the admission, so it
        # has no line; frame 4 admits again.
        bit = '%03x' % (1 << HULL_PROGRAM_INDEX)
        assert [(int(r['frame']), int(r['admitted']), int(r['refused_blend']), int(r['opaque']), int(r['alpha']), r['programs'], int(r['refused_other']), int(r['refused_routed']), int(r['refused_variant']), r['toggled']) for r in frames] == \
            [(0, 0, 1, 1, 0, '000', 0, 0, 0, '1'), (1, 1, 0, 0, 0, bit, 0, 0, 0, '1'), (2, 0, 1, 0, 0, '000', 0, 0, 0, '1'), (4, 1, 0, 0, 0, bit, 0, 0, 0, '1')], frames
        assert [(int(r['frame']), int(r['program']), r['ps']) for r in programs] == [(1, HULL_PROGRAM_INDEX, '7c83ed50c9894e44')], programs
        refused = [fields(l) for l in traces if l.startswith('hull_emission_refused ')]
        assert [(int(r['frame']), r['reason'], r['blend'], r['src'], r['dst']) for r in refused] == [(2, 'blend', '1', '2', '4')], refused
        assert not any(l.startswith(('hull_emission_bind_failed', 'hull_emission_refused_state', 'motion_output_restore_failed')) for l in traces)
        pressed = [fields(l) for l in traces if l.startswith('hull_emission_gain_toggle ')]
        assert [(int(r['frame']), r['accepted'], r['enabled'], r['requested']) for r in pressed] == [(3, '1', '0', '1'), (4, '1', '1', '1')], pressed
        # Capture frames: one line per admitted draw naming object B (the
        # fixture scope: node 0x1100, handle 8, serial 12, model 0x12, LOD 2).
        assert [(int(r['frame']), int(r['program']), r['ps'], r['routed'], r['known'], int(r['node_handle']), int(r['node_serial']), r['model'], r['lod'], int(r['primitives'])) for r in draws] == \
            [(f, HULL_PROGRAM_INDEX, '7c83ed50c9894e44', '0', '1', 8, 12, '00000012', '00000002', 1) for f in (1, 4)], draws
        assert all(float(r['gain']) == gain and int(r['node'], 16) == 0x1100 for r in draws), draws
    colour = [(int(r['frame']), r['hash']) for r in (fields(l) for l in lines if l.startswith('COLOR '))]
    return dict(gain=gain, frames=[dict(kind=r['kind'], pixels=int(r['pixels']), max_codes=int(r['max_codes'])) for r in rows],
                variants=len(variants), admitted=sum(int(r['admitted']) for r in frames), refused_blend=sum(int(r['refused_blend']) for r in frames),
                refused_routed=sum(int(r['refused_routed']) for r in frames), draw_lines=len(draws), colour=colour)

def validate_cascade_capture(trace, work):
    """The F8 record of the cascade apply: per applied capture frame one basis
    line and one readback per cascade, the params line (parse_apply_params
    checks every printed bias against the law) and the cascade twin on the
    dumps: cascade 0 owns the receiver and the twin finds the caster's shadow."""
    import numpy as np
    lines = trace.splitlines()
    captures = next(work.glob('x3-modern-captures'))
    mode = [fields(l) for l in lines if l.startswith('shadow_cascades_mode ')]
    assert len(mode) == 1 and (mode[0]['enabled'], mode[0]['cascades']) == ('1', '2'), mode
    device = [fields(l) for l in lines if l.startswith('sun_shadow_apply_device ')]
    assert device and all(r['attached'] == '1' for r in device), device
    frames = {}
    for l in lines:
        if l.startswith('sun_shadow_apply_params '):
            row = sun_apply.line_fields(l); frames[int(row['frame'])] = row
    assert len(frames) >= 2 and all(r['cascades'] == '2' for r in frames.values()), sorted(frames)  # the capture frames on which the quad drew
    record = {}
    for frame, row in sorted(frames.items()):
        basis = [fields(l) for l in lines if l.startswith('shadow_replay_map_basis ') and f' frame={frame} ' in l]
        assert [(b['cascade'], b['cascades'], b['valid'], b['replayed_frame']) for b in basis] == [('0', '2', '1', str(frame)), ('1', '2', '1', str(frame))], (frame, basis)
        assert all(b['sun_verdict'] == 'sampled' and b['sun_register'] == '4' for b in basis), basis
        assert (float(basis[0]['extent']), float(basis[0]['depth_light']), float(basis[0]['depth_behind'])) == (32.0, 512.0, 64.0), basis[0]
        assert (float(basis[1]['extent']), float(basis[1]['depth_light']), float(basis[1]['depth_behind'])) == (256.0, 512.0, 512.0), basis[1]
        casters = [fields(l) for l in lines if l.startswith('shadow_replay_caster ') and f' frame={frame} ' in l]
        assert len(casters) == 2 and all(c['cascades'] == '3' and c['sun_register'] == '4' and c['sun_agrees'] == '1' and c['verdict'] in ('bounds', 'origin') for c in casters), casters
        params, extra = sun_apply.parse_apply_params(row)
        assert [c['valid'] for c in params['cascades']] == [True, True] and [c['map_frame'] for c in extra['cascades']] == [frame, frame] and [c['map'] for c in extra['cascades']] == [512, 512], (frame, extra)
        device_id = int(row['device'])
        maps = sun_apply.load_cascade_maps(captures, device_id, frame, extra, params)
        assert all(m is not None and m.shape == (512, 512) for m in maps), frame
        encoding = params['depth_encoding']  # the line's depth_encoding names the RT2 dump's format (rg32f / rgba32f) and the twin's law
        d, s = sun_apply.unpack_rt2((captures / f'depth_{device_id}_{frame}.{sun_apply.rt2_suffix(encoding)}').read_bytes(), extra['width'], extra['height'], encoding)
        # The script's RT2 dump carries the depth but a zero share by the time of the readback; the
        # visibility f does not depend on the share, so the twin runs with share 1 on every receiver.
        out = sun_apply.expected_factor_cascades(d, np.where(d >= 0.0, 1.0, 0.0), maps, params)
        owned = [int(np.count_nonzero(out['valid'] & (out['selected'] == c))) for c in range(2)]
        shadowed = int(np.count_nonzero(out['valid'] & (out['f'] < 1.0)))
        assert owned[0] > 1000 and owned[1] == 0 and shadowed >= 400, (frame, owned, shadowed)
        record[frame] = dict(owned=owned, shadowed=shadowed, bias=[c['bias_constant'] for c in params['cascades']], texel_world=[c['texel_world'] for c in extra['cascades']], depth_encoding=encoding)
    return dict(capture_frames=sorted(record), frames=record)

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--dll', type=Path, required=True, help='the fixture-seam DLL (verification/probe/build/motion-output-seam/d3d9.dll after run_motion_output.py builds it); the production DLL fails the "sun live needs the HDR/TAA native seam" check')
    parser.add_argument('--programs', type=Path, default=Path('/tmp/x3-shader-sweep/programs'))
    parser.add_argument('--case', choices=CASES, action='append')
    parser.add_argument('--result', type=Path)
    args = parser.parse_args()
    assert os.environ.get('X3M_FIXTURE_BOTTLE') == 'X3' and bottle.BOTTLE == 'X3', 'Set X3M_FIXTURE_BOTTLE=X3 explicitly'
    fixture, dll = args.fixture.resolve(), args.dll.resolve()
    selected = args.case or CASES
    names = ['vs_53a0a641107ed76c.bin', 'ps_8759c7838bbc86c2.bin']
    if any(case.startswith('composition') for case in selected): names += ['vs_089091aab2d5eb13.bin', 'ps_8559522220507d5e.bin']
    if any(case in selected for case in ('cutout_pair', 'cutout_pair_bias', 'original_lane')): names += ['ps_63f96eba9eea7880.bin']
    if 'hull_emission' in selected: names += list(HULL_PROGRAMS)
    programs = [args.programs.resolve()/name for name in names]
    inputs = {str(p): sha(p) for p in (fixture, dll, *programs)}
    raw = Path(tempfile.mkdtemp(prefix='x3-sun-share-live-'))
    result = args.result or bottle.results_dir(ROOT)/'sun-share-live.json'
    report = dict(passed=False, bottle=bottle.describe(), raw=str(raw), inputs=inputs, game_launched=False, cases={})
    try:
        for case in selected:
            if case == 'hull_emission':
                report['cases'][case] = {}
                for gain in (HULL_GAIN, 1.0):
                    work = raw/case/('gain-%g' % gain); work.mkdir(parents=True)
                    shutil.copy2(fixture, work/'fixture.exe'); shutil.copy2(dll, work/'d3d9.dll')
                    env = {k:v for k,v in os.environ.items() if not k.startswith('X3M_')}
                    # No effects gain: the hull population stands alone (its own
                    # option and key). The capture window covers every frame of
                    # the script, so the per-draw hull_emission_draw lines and
                    # the motion_route lines of the covered pair are on record.
                    env.update(X3M_MOTION_OUTPUT='1', X3M_TAA='0', X3M_HDR='1', X3M_HDR_CLAMP='0', X3M_HDR_BLOOM='0',
                               X3M_SCENE_HOOK='0', X3M_OWNERSHIP='0', X3M_TELEMETRY='1', X3M_MOTION_FRAME_LOG='1',
                               X3M_CAPTURE_START='1', X3M_CAPTURE_FRAMES='8', X3M_MOTION_RT_MODE='perdraw', X3M_STATE_SHADOW='1',
                               WINEDLLOVERRIDES='d3d9=n,b')
                    if gain != 1.0: env['X3M_HULL_EMISSION_GAIN'] = repr(gain)
                    command = [bottle.WINE, *bottle.wine_args(), '--dll', 'd3d9=n,b', '--workdir', str(work), str(work/'fixture.exe'),
                               *('Z:'+str(p) for p in programs[:2]), 'hullemission']
                    start = time.monotonic()
                    with (work/'stdout.txt').open('w') as out, (work/'wine.log').open('w') as error:
                        completed = subprocess.run(command, env=env, stdout=out, stderr=error, timeout=180)
                    assert completed.returncode == 0, (case, gain, completed.returncode, str(work))
                    logs = list((work/'x3-modern-captures').glob('session-*.log')); assert len(logs) == 1
                    check = validate_hull_emission((work/'stdout.txt').read_text(), logs[0].read_text(), gain)
                    report['cases'][case]['gain-%g' % gain] = dict(check, elapsed_seconds=time.monotonic()-start, command=command)
                # Across the two runs the refused frames (0 opaque/routed, 2
                # screen, 3 toggled off) present the same colour hash: the
                # option changes only the admitted ONE/ONE frames (1 and 4).
                on, off = (dict(report['cases'][case]['gain-%g' % g]['colour']) for g in (HULL_GAIN, 1.0))
                assert set(on) == set(off) == {0, 1, 2, 3, 4}, (on, off)
                same = sorted(f for f in on if on[f] == off[f])
                assert same == [0, 2, 3], (on, off)
                report['cases'][case]['identical_frames_across_gains'] = same
                continue
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
            if apply_base(case) in ORIGINAL_CASES:
                # Original shading: the lane without linear materials.
                env['X3M_LINEAR_MATERIALS'] = '0'
            if case == 'original_share_refused':
                env.update(X3M_FIXTURE_SUN_LANE_FAULT='original_share', X3M_ORIGINAL_FILL='0.05')
            if case in APPLY_CASES:
                # The depth replay (ownership bookends, rotating camera seam, a
                # 512^2 map over a 32-unit half-extent centred on the camera so
                # the receiver at view depth 12 and the caster at 16 lie inside cascade 0) and the quad.
                env.update(X3M_OWNERSHIP='1', X3M_FIXTURE_CAMERA='rotate', X3M_SHADOW_REPLAY_DEPTH='1', X3M_SHADOW_REPLAY_SIZE='512',
                           X3M_FIXTURE_SLICE_NEAR='0.5', X3M_FIXTURE_SHADOW_EXTENT='32', X3M_SUN_SHADOW_APPLY='1', X3M_FIXTURE_SUN_LIVE_CASE='shadow_apply',
                           X3M_SUN_SHADOW_RECEIVER_DEPTH='linear' if case.endswith('_linear') else 'device')  # the wide RT2 (.b = view depth) on the -linear siblings; TAA reads .r unchanged either way
            if apply_base(case) == 'shadow_apply_cascades':
                env.update(CASCADE_LIVE_ENV)  # the same script; only the DLL's options differ
            command = [bottle.WINE, *bottle.wine_args(), '--dll', 'd3d9=n,b', '--workdir', str(work), str(work/'fixture.exe'),
                       *('Z:'+str(p) for p in programs[:2]), 'sunlane']
            start = time.monotonic()
            with (work/'stdout.txt').open('w') as out, (work/'wine.log').open('w') as error:
                completed = subprocess.run(command, env=env, stdout=out, stderr=error, timeout=180)
            assert completed.returncode == 0, (case, completed.returncode, str(work))
            logs = list((work/'x3-modern-captures').glob('session-*.log')); assert len(logs) == 1
            check = validate((work/'stdout.txt').read_text(), logs[0].read_text(), work, 'shadow_apply' if apply_base(case) == 'shadow_apply_cascades' else apply_base(case))
            if case in APPLY_CASES:
                wanted = ('linear', 116) if case.endswith('_linear') else ('device', 115)
                assert (check.get('receiver_depth'), check.get('rt2_format')) == wanted, (case, 'RT2 encoding and format of this case', check.get('receiver_depth'), check.get('rt2_format'), wanted)
            if apply_base(case) == 'shadow_apply_cascades':
                check['cascades'] = validate_cascade_capture(logs[0].read_text(), work)
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
