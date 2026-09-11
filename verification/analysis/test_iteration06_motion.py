"""Unit fixtures for the iteration-06 live-motion-route session analyzer.

The real input is a 384 MB gameplay log; these fixtures are small synthetic logs
written to a temporary directory with the same record grammar, so every derived
number the report quotes has a test that fixes its meaning. No game asset,
shader byte or capture file is involved: the shader "dumps" here are four-byte
D3D9 version tokens produced by the test itself.
"""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location(
    'analyze_iteration06_motion',
    Path(__file__).resolve().parents[2] / 'tools/analysis/analyze_iteration06_motion.py')
ITER06 = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ITER06)

BACKGROUND_VS, BACKGROUND_PS = '7b6393fe2d3e1d85', '6109cf64c03529dd'
MATERIAL_VS, MATERIAL_PS = '4944d81dfe531b37', 'ca6bfa4a6cca7e2a'
UNKNOWN_VS, UNKNOWN_PS = 'aaaaaaaaaaaaaaaa', 'bbbbbbbbbbbbbbbb'
FOREIGN_BG_VS, FOREIGN_BG_PS = 'f80f7af59b667bb7', 'd6f6ba4fee1cd53e'
ZONLY_VS, NULL_PS = 'c78b4c68a87fce74', '0' * 16

# Version tokens: vs_3_0 / ps_3_0 / vs_1_1 / ps_1_1 / vs_2_0 / ps_2_0.
VERSIONS = {
    BACKGROUND_VS: 0xFFFE0101, BACKGROUND_PS: 0xFFFF0101,
    MATERIAL_VS: 0xFFFE0300, MATERIAL_PS: 0xFFFF0300,
    UNKNOWN_VS: 0xFFFE0300, UNKNOWN_PS: 0xFFFF0300,
    FOREIGN_BG_VS: 0xFFFE0200, FOREIGN_BG_PS: 0xFFFF0200,
    ZONLY_VS: 0xFFFE0101,
}

OPAQUE_STATES = {'7': 1, '14': 1, '15': 0, '27': 0, '168': 15, '194': 0}
ALPHA_TESTED_STATES = dict(OPAQUE_STATES, **{'15': 1, '168': 7})
BLENDED_STATES = dict(OPAQUE_STATES, **{'14': 0, '27': 1, '168': 7})


def draw_block(frame, index, vs, ps, states, gate=None, routed=0, matched=0,
               node_serial='100', camera_serial='200', load_epoch='1', registry_epoch='2',
               vb='10', ib='11', primitives='12', integer0='0,0,1,0',
               before_known='1', before_reason='0'):
    """One draw's records in the order the capture writes them."""
    lines = [f'draw device=1 frame={frame} index={index} kind=indexed topology=4 '
             f'primitives={primitives} vs={vs} ps={ps}']
    lines += [f'state id={key} value={value}' for key, value in sorted(states.items())]
    lines.append(f'constant kind=vs type=i reg=0 values={integer0}')
    if gate is not None:
        lines.append(
            f'motion_route device=1 frame={frame} index={index} gate={gate} routed={routed} '
            f'matched={matched} vs={vs} ps={ps} node=1000 camera=2000 node_handle=1 '
            f'camera_handle=2 node_serial={node_serial} camera_serial={camera_serial} '
            f'load_epoch={load_epoch} registry_epoch={registry_epoch} model=00000001 '
            f'lod=00000000 vb={vb} ib={ib} declaration=deadbeefdeadbeef offset=0 stride=40 '
            f'position_offset=0 position_type=16 topology=4 first=0 primitives={primitives} '
            f'base_vertex=0 min_vertex=0 vertex_count=24 indexed=1 '
            f'rows_hash=0123456789abcdef result=00000000')
    lines.append(f'motion_lifetime device=1 frame={frame} index={index} '
                 f'before_known={before_known} after_known=1 before_reason={before_reason} '
                 f'after_reason=0 registry=1 observer_epoch=1 load_epoch={load_epoch} '
                 f'registry_epoch={registry_epoch} node_serial={node_serial} '
                 f'camera_serial={camera_serial}')
    lines.append(f'draw_result device=1 frame={frame} index={index} result=00000000')
    return lines


def counters(frame, draws, routed, matched, gates, committed=1, latched=1, filled=1,
             selector_state=9, apply_failures=0, restore_failures=0):
    gate_text = ' '.join(f'gate{i}={gates.get(i, 0)}' for i in range(1, 7))
    return (f'motion_output_frame device=1 frame={frame} latched={latched} filled={filled} '
            f'fill_result=00000000 fill_restore=00000000 draws={draws} routed={routed} '
            f'matched={matched} {gate_text} apply_failures={apply_failures} '
            f'restore_failures={restore_failures} history_previous={matched} '
            f'history_current=0 committed={committed} selector_state={selector_state} '
            f'present=00000000')


def frame_block(frame, draws, counter_line):
    """Wrap per-draw records in the frame/phase event structure of a real capture.

    ``draws`` is a list of ``(phase, lines)`` where phase is one of the four
    render phases; the event records that separate them are inserted here.
    """
    lines = [f'frame_begin device=1 frame={frame}',
             f'capture_event device=1 frame={frame} seq=1 after_draw=0 op=clear result=00000000',
             'clear flags=3 color=00000000 z=1 stencil=0 rect_count=0 rect_ptr=00000000']
    emitted = set()
    for phase, block in draws:
        if phase != 'background' and 'scene' not in emitted:
            lines += [f'capture_event device=1 frame={frame} seq=2 after_draw=0 op=clear result=00000000',
                      'clear flags=2 color=00000000 z=1 stencil=0 rect_count=0 rect_ptr=00000000']
            emitted.add('scene')
        if phase in ('bloom', 'overlay_gui') and 'bloom' not in emitted:
            lines += [f'capture_event device=1 frame={frame} seq=3 after_draw=0 op=set_depth result=00000000',
                      'set_depth ptr=00000000 result=00000000']
            emitted.add('bloom')
        if phase == 'overlay_gui' and 'overlay' not in emitted:
            lines += [f'capture_event device=1 frame={frame} seq=4 after_draw=0 op=set_depth result=00000000',
                      'set_depth ptr=023018e8 result=00000000']
            emitted.add('overlay')
        lines += block
    lines.append(counter_line)
    lines.append(f'frame_end device=1 frame={frame} draws={len(draws)} capture=1 present=00000000')
    return lines


def session_header():
    return [
        'x3-modern-renderer schema=2',
        'motion_output_mode requested=1 scope=live_same_draw_diagnostic '
        'history_requires=object_trace,object_lifetime temporal_consumer=0',
        'ownership_factory mode=wrapped native=1 public=2 result=00000000',
        'object_trace active=1 status=active recovery_required=0',
        'object_lifetime active=1 status=active_without_baseline recovery_required=0 '
        'baseline_complete=0 baseline_entries=0',
        'adapter description=Test driver=test.dll vendor=00000000 device=00000000',
        'motion_output_device device=1 enabled=1 reason=ok detail=stage=compare '
        'color_errors=0 motion_errors=0 mrt=4 vs_constants=256 misc=002ecff2 '
        'history_available=1 history_capacity=4096',
        'motion_output_target device=1 width=64 height=64 create=00000000 level=00000000',
        'telemetry_start schema=1 qpc_frequency=10000000 qpc=1 anchor=proxy_initialize cpu_only=1',
    ] + [f'shader kind={"vs" if VERSIONS[h] >> 16 == 0xFFFE else "ps"} id={h} bytes=64 dumped=1'
         for h in sorted(VERSIONS)]


def routed_session():
    """Two consecutive captured frames that route, plus one rejected frame.

    Frame 10 matches nothing (no previous frame); frame 11 matches one draw,
    misses history on a second whose key is new, rejects an unreviewed pair at
    gate 3 and an alpha-tested draw at gate 4. Frame 40 is rejected in the
    Background phase by a foreign background pair.
    """
    lines = session_header()
    common = dict(vb='10', ib='11')
    frame10 = [
        ('background', draw_block(10, 1, BACKGROUND_VS, BACKGROUND_PS, OPAQUE_STATES)),
        ('scene', draw_block(10, 2, MATERIAL_VS, MATERIAL_PS, OPAQUE_STATES, gate=6, routed=1, **common)),
        ('bloom', draw_block(10, 3, BACKGROUND_VS, BACKGROUND_PS, OPAQUE_STATES)),
        ('overlay_gui', draw_block(10, 4, BACKGROUND_VS, BACKGROUND_PS, OPAQUE_STATES)),
    ]
    lines += frame_block(10, frame10, counters(10, 4, 1, 0, {2: 3, 6: 1}))
    frame11 = [
        ('background', draw_block(11, 1, BACKGROUND_VS, BACKGROUND_PS, OPAQUE_STATES)),
        # Same key as frame 10 draw 2: this one matches.
        ('scene', draw_block(11, 2, MATERIAL_VS, MATERIAL_PS, OPAQUE_STATES, gate=0, routed=1,
                             matched=1, **common)),
        # New key: history miss with the previous frame captured.
        ('scene', draw_block(11, 3, MATERIAL_VS, MATERIAL_PS, OPAQUE_STATES, gate=6, routed=1,
                             vb='20', ib='21')),
        # Unreviewed but fully opaque pair: a profile-table candidate.
        ('scene', draw_block(11, 4, UNKNOWN_VS, UNKNOWN_PS, OPAQUE_STATES, gate=3)),
        # Alpha-tested draw: gate 4, and i0.x inside the permitted range.
        ('scene', draw_block(11, 5, MATERIAL_VS, MATERIAL_PS, ALPHA_TESTED_STATES, gate=4,
                             integer0='3,0,0,0')),
        # Blended draw: gate 4 with z-write off, which the DLL sees first.
        ('scene', draw_block(11, 6, MATERIAL_VS, MATERIAL_PS, BLENDED_STATES, gate=4)),
        # Scope failure.
        ('scene', draw_block(11, 7, MATERIAL_VS, MATERIAL_PS, OPAQUE_STATES, gate=5, routed=1,
                             vb='30', ib='31', before_known='0', before_reason='4')),
        ('overlay_gui', draw_block(11, 8, BACKGROUND_VS, BACKGROUND_PS, OPAQUE_STATES)),
    ]
    lines += frame_block(11, frame11, counters(11, 8, 3, 1, {2: 2, 3: 1, 4: 2, 5: 1, 6: 1}))
    frame40 = [
        ('background', draw_block(40, 1, BACKGROUND_VS, BACKGROUND_PS, OPAQUE_STATES)),
        ('background', draw_block(40, 2, FOREIGN_BG_VS, FOREIGN_BG_PS, OPAQUE_STATES)),
        ('scene', draw_block(40, 3, MATERIAL_VS, MATERIAL_PS, OPAQUE_STATES)),
    ]
    lines += frame_block(40, frame40, counters(40, 3, 0, 0, {2: 3}, committed=1))
    lines += [
        'telemetry_summary device=1 frame=12 reason=interval qpc=10 since_start_us=1 interval_us=1',
        'telemetry_metric device=1 name=frame_normal count=2 failures=0 total_us=40000.0 '
        'min_us=20000.0 max_us=20000.0 bytes=0 buckets=0,0,0,0,2,0',
        'telemetry_metric device=1 name=present_normal count=2 failures=0 total_us=20.0 '
        'min_us=10.0 max_us=10.0 bytes=0 buckets=0,2,0,0,0,0',
        'telemetry_summary device=1 frame=41 reason=interval qpc=20 since_start_us=2 interval_us=1',
        'telemetry_metric device=1 name=frame_normal count=2 failures=0 total_us=80000.0 '
        'min_us=40000.0 max_us=40000.0 bytes=0 buckets=0,0,0,0,2,0',
    ]
    return lines


def zonly_session():
    """A frame whose first scene draw binds no pixel shader (z-only prepass)."""
    lines = session_header()
    frame = [
        ('background', draw_block(70, 1, BACKGROUND_VS, BACKGROUND_PS, OPAQUE_STATES)),
        ('scene', draw_block(70, 2, ZONLY_VS, NULL_PS, OPAQUE_STATES, gate=3)),
        ('scene', draw_block(70, 3, MATERIAL_VS, MATERIAL_PS, OPAQUE_STATES)),
    ]
    lines += frame_block(70, frame, counters(70, 3, 0, 0, {2: 2, 3: 1}))
    return lines


class Iteration06MotionTests(unittest.TestCase):
    def analyze(self, lines, with_dumps=True):
        directory = Path(tempfile.mkdtemp())
        log = directory / 'session-test.log'
        log.write_text('\n'.join(lines) + '\n', encoding='utf-8')
        if with_dumps:
            for digest, version in VERSIONS.items():
                stage = 'vs' if version >> 16 == 0xFFFE else 'ps'
                (directory / f'{stage}_{digest}.bin').write_bytes(struct.pack('<I', version))
        return ITER06.analyze(str(log), str(directory), None, None)

    # ---- motion_output_frame ----
    def test_frame_counters_aggregate_gates_and_flags(self):
        summary = self.analyze(routed_session())
        counters_ = summary['frame_counters']
        self.assertEqual(counters_['count'], 3)
        self.assertEqual(counters_['totals']['routed'], 4)
        self.assertEqual(counters_['totals']['matched'], 1)
        self.assertEqual(counters_['totals']['gate2'], 8)
        self.assertEqual(counters_['totals']['gate4'], 2)
        self.assertEqual(counters_['apply_failures'], 0)
        self.assertEqual(counters_['restore_failures'], 0)
        self.assertEqual(counters_['frames_with_routed'], 2)
        self.assertEqual(counters_['selector_state'], [['Rejected', 3]])
        self.assertEqual(counters_['committed'], [[1, 3]])

    def test_bursts_group_consecutive_frames_only(self):
        summary = self.analyze(routed_session())
        bursts = summary['bursts']
        self.assertEqual([b['frames'] for b in bursts], [[10, 11], [40]])
        self.assertEqual(bursts[0]['matched'], 1)
        self.assertIsNone(bursts[1]['matched_rate'])

    # ---- motion_route ----
    def test_gate_outcomes_and_matched_rate(self):
        routes = self.analyze(routed_session())['routes']
        self.assertEqual(dict(map(tuple, routes['gate_outcomes'])),
                         {'matched': 1, 'gate3_pair': 1, 'gate4_draw_state': 2,
                          'gate5_scope': 1, 'gate6_history': 2})
        self.assertEqual(routes['routed'], 4)
        self.assertEqual(routes['matched'], 1)
        self.assertAlmostEqual(routes['matched_rate_of_routed'], 0.25)

    def test_unclassified_gate3_pairs_flag_opaque_candidates(self):
        routes = self.analyze(routed_session())['routes']
        pairs = routes['gate3_unclassified_pairs']
        self.assertEqual(len(pairs), 1)
        self.assertEqual((pairs[0]['vs'], pairs[0]['ps']), (UNKNOWN_VS, UNKNOWN_PS))
        self.assertEqual(pairs[0]['draws'], 1)
        # Fully opaque, so gate 4 would have admitted it: a profile-row candidate.
        self.assertEqual(pairs[0]['draws_passing_gate4_states'], 1)

    def test_gate4_reports_first_and_all_failing_predicates_with_i0(self):
        gate4 = self.analyze(routed_session())['routes']['gate4']
        self.assertEqual(gate4['draws'], 2)
        self.assertEqual(dict(map(tuple, gate4['first_failing_predicate'])),
                         {'alpha_test': 1, 'z_write': 1})
        counts = dict(map(tuple, gate4['predicate_counts']))
        self.assertEqual(counts['color_write_mask'], 2)
        self.assertEqual(counts['alpha_blend'], 1)
        # i0.x of 0 and 3 are both inside the permitted light-loop range.
        self.assertEqual(dict(map(tuple, gate4['i0_x_values'])), {0: 1, 3: 1})

    def test_gate5_reports_the_observer_reason_tuple(self):
        gate5 = self.analyze(routed_session())['routes']['gate5']
        self.assertEqual(gate5['draws'], 1)
        self.assertEqual(gate5['lifetime_states'], [[['0', '1', '4', '0'], 1]])

    def test_gate6_reason_needs_the_previous_frame_to_be_captured(self):
        gate6 = self.analyze(routed_session())['routes']['gate6']
        reasons = dict(map(tuple, gate6['reasons']))
        # Frame 10 has no captured predecessor; frame 11's miss has a new key.
        self.assertEqual(reasons['previous_frame_not_captured'], 1)
        self.assertEqual(reasons['key_absent_in_previous_frame'], 1)

    def test_keys_are_taken_only_from_draws_that_passed_gate_4(self):
        routes = self.analyze(routed_session())['routes']
        # Frames 10/11 contribute four keyed draws over two distinct vertex buffers
        # plus the scope failure's own buffer; gate-3/gate-4 lines are excluded.
        self.assertEqual(routes['distinct_key_values']['vb'], 3)
        self.assertEqual(routes['duplicate_keys_within_a_frame'], 0)

    # ---- scene selection ----
    def test_foreign_background_pair_rejects_the_whole_frame(self):
        selection = self.analyze(routed_session())['scene_selection']
        self.assertEqual(selection['frames_without_routed_draws'], 1)
        rejected = selection['rejected_frames'][0]
        self.assertEqual(rejected['frame'], 40)
        self.assertEqual(rejected['state'], 'Background')
        self.assertEqual(rejected['vs'], FOREIGN_BG_VS)

    def test_null_pixel_shader_rejects_the_scene_phase(self):
        selection = self.analyze(zonly_session())['scene_selection']
        rejected = selection['rejected_frames'][0]
        self.assertEqual(rejected['state'], 'Scene')
        self.assertEqual(rejected['ps'], NULL_PS)
        self.assertIn('null pixel shader', rejected['reason'])

    # ---- shader models and phases ----
    def test_shader_models_come_from_the_dumped_version_word(self):
        models = self.analyze(routed_session())['shader_models']
        counts = dict(map(tuple, models['model_counts']))
        self.assertEqual(counts['vs_3_0'], 2)
        self.assertEqual(counts['ps_3_0'], 2)
        self.assertEqual(counts['vs_1_1'], 2)
        self.assertEqual(counts['ps_1_1'], 1)
        self.assertEqual(counts['vs_2_0'], 1)
        self.assertEqual(counts['ps_2_0'], 1)

    def test_draws_are_split_by_render_phase(self):
        models = self.analyze(routed_session())['shader_models']
        self.assertEqual(models['draws_by_phase'],
                         {'background': 4, 'scene': 8, 'bloom': 1, 'overlay_gui': 2})
        scene = dict((tuple(pair), count) for pair, count in models['pairs_by_phase']['scene'])
        self.assertEqual(scene[('vs_3_0', 'ps_3_0')], 8)

    def test_null_pixel_shader_is_reported_as_a_below_sm3_scene_pair(self):
        models = self.analyze(zonly_session())['shader_models']
        scene = [row for row in models['below_sm3_pairs'] if row['phase'] == 'scene']
        self.assertEqual(len(scene), 1)
        self.assertEqual(scene[0]['vs_model'], 'vs_1_1')
        self.assertEqual(scene[0]['ps_model'], 'ps_null')

    def test_models_are_unknown_without_dumps_rather_than_guessed(self):
        models = self.analyze(routed_session(), with_dumps=False)['shader_models']
        counts = dict(map(tuple, models['model_counts']))
        self.assertEqual(counts.get('vs_unknown', 0) + counts.get('ps_unknown', 0),
                         models['programs_created'])

    # ---- telemetry and diagnostics ----
    def test_telemetry_splits_windows_by_route_activity(self):
        telemetry = self.analyze(routed_session())['telemetry']
        self.assertEqual(telemetry['frame_interval']['normal']['count'], 4)
        routed = telemetry['by_route_activity']['routed']['frame_normal']
        unrouted = telemetry['by_route_activity']['unrouted']['frame_normal']
        # The window at frame 12 follows frame 11 (routed); frame 41 follows 40.
        self.assertAlmostEqual(routed['mean_us'], 20000.0)
        self.assertAlmostEqual(unrouted['mean_us'], 40000.0)
        self.assertTrue(any('not GPU execution time' in limit for limit in telemetry['limits']))

    def test_missing_shutdown_records_are_reported(self):
        diagnostics = self.analyze(routed_session())['diagnostics']
        self.assertEqual(sorted(diagnostics['missing_shutdown_records']),
                         ['device_destroy', 'motion_output_release'])
        self.assertEqual(diagnostics['nonzero_results'], [])
        self.assertEqual(diagnostics['object_lifetime']['baseline_complete'], '0')

    def test_summary_is_json_serializable_and_deterministic(self):
        lines = routed_session()
        first = json.dumps(self.analyze(lines), default=str, sort_keys=False)
        second = json.dumps(self.analyze(lines), default=str, sort_keys=False)
        # Only the temporary log path and its hash differ between the two runs.
        self.assertEqual(json.loads(first)['routes'], json.loads(second)['routes'])
        self.assertEqual(json.loads(first)['shader_models'], json.loads(second)['shader_models'])


if __name__ == '__main__':
    unittest.main()
