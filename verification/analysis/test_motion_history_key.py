"""Original synthetic fixtures for the cross-frame motion-history key audit.

No captured game data is embedded: every register value, identity and serial
below is invented. The tests assert that the analyzer fails closed on missing,
unscoped, duplicated and misattributed records, that the Scene-phase bracket is
taken from the recorded Clear/bloom boundary rather than from draw indices, and
that the committed derived summary stays internally consistent.
"""
import importlib.util
import json
from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    'motion_history_key', ROOT / 'tools/analysis/analyze_motion_history_key.py')
KEY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(KEY)

SUMMARY = ROOT / 'verification/results/motion-history-key-summary.json'
SNAPSHOT_SHA256 = 'e5beaa861d04659fe9c7df05a01845bd05d656a33c643f4b484ff379cf3ccaf8'


def rows_hash(rows):
    """FNV-1a-64 over the sixteen little-endian register words, as the producer."""
    words = [struct.unpack('<I', struct.pack('<f', value))[0] for row in rows for value in row]
    return f'{KEY.fnv1a64(b"".join(struct.pack("<I", word) for word in words)):016x}'


def identity_rows(scale=1.0, tx=0.0):
    return [[scale, 0.0, 0.0, tx], [0.0, scale, 0.0, 0.0],
            [0.0, 0.0, scale, 0.0], [0.0, 0.0, 0.0, 1.0]]


def draw_block(frame, index, *, node='1000', camera='2000', vb='10', ib='11',
               declaration='aa00000000000001', model='00004fb0', lod='00000000',
               primitives=8, num_vertices=24, start_index=0, stride=40, offset=0,
               rows=None, hash_override=None, scoped=True, known=True, result='00000000',
               vs='1111111111111111', ps='2222222222222222', sparse=True, omit_rows=False,
               device='1', life_device=None, life_frame=None, load_epoch=1):
    """One synthetic captured draw block in recorded log order."""
    rows = identity_rows() if rows is None else rows
    coordinate = f'device={device} frame={frame} index={index}'
    life_coordinate = (f'device={life_device or device} '
                       f'frame={life_frame if life_frame is not None else frame} index={index}')
    lines = [f'draw {coordinate} kind=indexed topology=4 primitives={primitives} vs={vs} ps={ps}']
    if scoped:
        lines.append(f'object_context {coordinate} scoped=1 valid=127 session=1 scope_depth=1 '
                     f'mesh=00aa00bb node=0f739af0 node_handle=21960 camera=0f16e630 '
                     f'camera_handle=21924 registry=021fb3c0 engine=02217168 '
                     f'model={model} lod={lod} flags12c=00001002 flags130=00000000')
    else:
        lines.append(f'object_context {coordinate} scoped=0 valid=0 session=1 scope_depth=0')
    lines.append(f'stream slot=0 result=00000000 identity={vb} offset={offset} stride={stride} '
                 f'frequency=1 frequency_result=00000000')
    lines.append(f'indices result=00000000 identity={ib}')
    lines.append('constants kind=vs type=f count=256 result=00000000 encoding='
                 + ('sparse_zero' if sparse else 'dense'))
    if not omit_rows:
        for offset_index, row in enumerate(rows):
            bits = ','.join(f'{struct.unpack("<I", struct.pack("<f", value))[0]:08x}' for value in row)
            lines.append(f'constant kind=vs type=f reg={24 + offset_index} bits={bits}')
    lines.append(f'draw_args base_vertex=0 min_vertex=0 num_vertices={num_vertices} '
                 f'start_index={start_index}')
    lines.append(f'motion_input {coordinate} blockers=00000000 proofs=31 position_path=1 '
                 f'vs={vs} ps={ps} declaration={declaration} '
                 f'rows_hash={hash_override or rows_hash(rows)} color=1 depth=2 width=1280 '
                 f'height=768 cull=3 vb={vb} vb_revision=1 ib={ib} ib_revision=1 '
                 f'position_offset=0 position_type=16 lifetime_verified=1 vertex_finite_verified=0')
    if known:
        lines.append(f'motion_lifetime {life_coordinate} before_known=1 after_known=1 '
                     f'before_reason=0 after_reason=0 registry=021fb3c0 observer_epoch=1 '
                     f'load_epoch={load_epoch} registry_epoch=2 mutation_before=9 mutation_after=9 '
                     f'node_serial={node} camera_serial={camera} observer_epoch_after=1 '
                     f'load_epoch_after={load_epoch} registry_epoch_after=2 '
                     f'node_serial_after={node} camera_serial_after={camera}')
    else:
        lines.append(f'motion_lifetime {life_coordinate} before_known=0 after_known=0 '
                     f'before_reason=8 after_reason=8 registry=021fb3c0')
    lines.append(f'draw_result {coordinate} result={result}')
    return lines


def clear_event(frame, seq, after_draw, flags, device='1', z='1', rect_count='0',
                rt0='1', depth='2'):
    return [f'capture_event device={device} frame={frame} seq={seq} after_draw={after_draw} '
            f'op=clear result=00000000 qpc=1',
            f'clear flags={flags} color=00000000 z={z} stencil=0 rect_count={rect_count} '
            f'rect_ptr=00000000',
            f'surface role=clear_rt0 ptr=aa identity={rt0} width=1280 height=768 format=21 '
            f'usage=1 msaa=0 container=0 container_type=0 container_result=80004002',
            f'surface role=clear_depth ptr=bb identity={depth} width=1280 height=768 format=77 '
            f'usage=2 msaa=0 container=0 container_type=0 container_result=80004002',
            'clear_viewport result=00000000 x=0 y=0 w=1280 h=768 minz=0 maxz=1']


def frame(number, scene_blocks, *, device='1', background=1, trailing=1, boundary=True,
          first_clear_flags=3, scene_clear_flags=2, epoch=10):
    """Assemble a complete captured frame around a Scene-phase draw list."""
    lines = [f'frame_begin device={device} frame={number}']
    lines += clear_event(number, 1, 0, first_clear_flags, device=device)
    index = 0
    for _ in range(background):
        index += 1
        lines += draw_block(number, index, node='9000', vb='90', ib='91', device=device)
    lines += clear_event(number, 2, index, scene_clear_flags, device=device)
    for block in scene_blocks:
        index += 1
        lines += block(number, index, device)
    lines.append(f'capture_event device={device} frame={number} seq=3 after_draw={index} '
                 f'op=stretch_rect result=00000000 qpc=2')
    for _ in range(trailing):
        index += 1
        lines += draw_block(number, index, node='9500', vb='95', ib='96', device=device)
    if boundary:
        lines.append(f'scene_depth_copy device={device} frame={number} event=4 result=00000000 '
                     f'valid=1 generation=1 source_epoch={epoch} copy_epoch={epoch} color=1 depth=2')
        lines.append(f'scene_depth_boundary device={device} frame={number} event=4 confirmed=1 '
                     f'generation=1 copy_epoch={epoch} source_epoch={epoch + 1}')
    lines.append(f'frame_end device={device} frame={number} draws={index} capture=1 '
                 f'present=00000000')
    return lines


def block(**kwargs):
    return lambda number, index, device='1': draw_block(number, index, device=device, **kwargs)


PROFILES = {'1111111111111111': dict(matrix_register=24, shader_model='3_0',
                                    named_world_view_projection=True,
                                    position_path='homogeneous_row_dots')}


def gameplay(report):
    return [item for item in report['frames'] if item['gameplay_scene']]


class SceneBoundaryTests(unittest.TestCase):
    def test_scene_phase_excludes_background_and_bloom_draws(self):
        report = KEY.analyze(frame(10, [block(node='1000'), block(node='1001')],
                                   background=2, trailing=3))
        item = gameplay(report)[0]
        self.assertEqual(item['total_draws'], 7)
        self.assertEqual(item['boundary']['first_scene_draw'], 3)
        self.assertEqual(item['boundary']['last_scene_draw'], 4)
        self.assertEqual(item['boundary']['end_op'], 'stretch_rect')
        self.assertEqual(item['counts']['scene_draws'], 2)

    def test_frame_without_confirmed_depth_boundary_is_not_gameplay(self):
        report = KEY.analyze(frame(10, [block()], boundary=False))
        self.assertEqual(gameplay(report), [])
        self.assertFalse(report['frames'][0]['selected_depth_boundary'])

    def test_color_only_first_clear_never_latches_main_targets(self):
        report = KEY.analyze(frame(10, [block()], first_clear_flags=1))
        self.assertEqual(gameplay(report), [])
        self.assertIsNone(report['frames'][0]['boundary'])

    def test_partial_or_non_depth_scene_clear_does_not_open_the_phase(self):
        for override in ({'scene_clear_flags': 1}, {'scene_clear_flags': 2}):
            lines = frame(10, [block()], **override)
            if override['scene_clear_flags'] == 2:
                lines = [line.replace('rect_count=0', 'rect_count=1') if line.startswith('clear flags=2')
                         else line for line in lines]
            self.assertEqual(gameplay(KEY.analyze(lines)), [], override)

    def test_incomplete_frame_is_excluded(self):
        lines = [line for line in frame(10, [block()]) if not line.startswith('frame_end ')]
        self.assertEqual(gameplay(KEY.analyze(lines)), [])


class ScopeAndRowTests(unittest.TestCase):
    def test_unscoped_and_unknown_lifetime_draws_are_not_keyable(self):
        report = KEY.analyze(frame(10, [block(scoped=False), block(known=False),
                                        block(result='8876086c'), block(node='1003')]))
        counts = gameplay(report)[0]['counts']
        self.assertEqual(counts['scene_draws'], 4)
        self.assertEqual(counts['known_lifetime'], 1)
        self.assertEqual(counts['without_known_lifetime'], 3)
        self.assertEqual(counts['unscoped'], 1)

    def test_lifetime_record_for_another_frame_is_not_attached(self):
        report = KEY.analyze(frame(10, [block(life_frame=11)]))
        self.assertEqual(gameplay(report)[0]['counts']['known_lifetime'], 0)

    def test_duplicate_lifetime_record_fails_closed(self):
        lines = frame(10, [block()])
        duplicate = next(line for line in lines if line.startswith('motion_lifetime ') and 'node_serial=1000' in line)
        lines.insert(lines.index(duplicate) + 1, duplicate)
        self.assertEqual(gameplay(KEY.analyze(lines))[0]['counts']['known_lifetime'], 0)

    def test_rows_hash_mismatch_never_becomes_zero_rows(self):
        report = KEY.analyze(frame(10, [block(hash_override='0123456789abcdef')]))
        counts = gameplay(report)[0]['counts']
        self.assertEqual(counts['keyable'], 1)
        self.assertEqual(counts.get('rows_verified', 0), 0)
        self.assertEqual(counts.get('rows_verified_assumed_register', 0), 0)
        self.assertEqual(counts['rows_rows_hash_mismatch_assumed_register'], 1)

    def test_registry_matrix_register_separates_verified_from_assumed(self):
        lines = frame(10, [block()])
        assumed = gameplay(KEY.analyze(lines))[0]['counts']
        self.assertEqual(assumed['rows_verified_assumed_register'], 1)
        known = gameplay(KEY.analyze(lines, PROFILES))[0]['counts']
        self.assertEqual(known['rows_verified'], 1)

    def test_wrong_registry_matrix_register_is_rejected_not_silently_shifted(self):
        wrong = {'1111111111111111': dict(matrix_register=8, shader_model='3_0')}
        counts = gameplay(KEY.analyze(frame(10, [block()]), wrong))[0]['counts']
        self.assertEqual(counts.get('rows_verified', 0), 0)
        # Absent rows under a successful sparse_zero query read as zeros, so the
        # producer's rows_hash - not a plausible-looking matrix - is what rejects it.
        self.assertEqual(counts['rows_rows_hash_mismatch'], 1)

    def test_omitted_rows_accepted_only_under_successful_sparse_zero(self):
        zero = [[0.0] * 4] * 4
        digest = rows_hash(zero)
        sparse = KEY.analyze(frame(10, [block(omit_rows=True, hash_override=digest)]), PROFILES)
        self.assertEqual(gameplay(sparse)[0]['counts']['rows_verified'], 1)
        dense = KEY.analyze(frame(10, [block(omit_rows=True, hash_override=digest, sparse=False)]),
                            PROFILES)
        self.assertEqual(gameplay(dense)[0]['counts']['rows_row_unavailable'], 1)

    def test_submitted_rows_blocker_rejects_the_rows(self):
        lines = [line.replace('blockers=00000000', 'blockers=00000100')
                 for line in frame(10, [block()])]
        counts = gameplay(KEY.analyze(lines))[0]['counts']
        self.assertEqual(counts['rows_submitted_rows_blocked'], 1)

    def test_fnv1a64_matches_the_published_reference_vectors(self):
        self.assertEqual(KEY.fnv1a64(b''), 0xcbf29ce484222325)
        self.assertEqual(KEY.fnv1a64(b'a'), 0xaf63dc4c8601ec8c)
        self.assertEqual(KEY.fnv1a64(b'foobar'), 0x85944171f73967e8)


class KeyMatchingTests(unittest.TestCase):
    def pair(self, first, second):
        return KEY.analyze(frame(10, first) + frame(11, second))['transitions'][0]

    def test_stable_scene_matches_every_keyable_draw(self):
        blocks = [block(node='1000', vb='10', ib='11'), block(node='1001', vb='12', ib='13')]
        moved = [block(node='1000', vb='10', ib='11', rows=identity_rows(tx=5.0)),
                 block(node='1001', vb='12', ib='13', rows=identity_rows(tx=5.0))]
        transition = self.pair(blocks, moved)
        self.assertEqual(transition['keys']['K1']['matched'], 2)
        self.assertEqual(transition['keys']['K1']['match_rate_of_keyable'], 1.0)
        self.assertEqual(transition['rows']['changed_rows'], 2)
        self.assertEqual(transition['rows']['unchanged_rows'], 0)

    def test_unchanged_rows_are_counted_as_static(self):
        blocks = [block(node='1000')]
        transition = self.pair(blocks, [block(node='1000')])
        self.assertEqual(transition['rows']['unchanged_rows'], 1)
        self.assertEqual(transition['rows']['unchanged_fraction'], 1.0)

    def test_duplicate_previous_key_is_poisoned_not_paired_by_order(self):
        duplicated = [block(node='1000', vb='10', ib='11'), block(node='1000', vb='10', ib='11')]
        transition = self.pair(duplicated, duplicated)
        self.assertEqual(transition['keys']['K1']['matched'], 0)
        self.assertEqual(transition['keys']['K1']['ambiguous_previous'], 2)

    def test_duplicate_current_key_consumes_the_previous_entry_once(self):
        transition = self.pair([block(node='1000', vb='10', ib='11')],
                               [block(node='1000', vb='10', ib='11'),
                                block(node='1000', vb='10', ib='11')])
        self.assertEqual(transition['keys']['K1']['matched'], 1)
        self.assertEqual(transition['keys']['K1']['duplicate_current'], 1)

    def test_vertex_buffer_identity_disambiguates_what_K2_cannot(self):
        blocks = [block(node='1000', vb='10', ib='11'), block(node='1000', vb='20', ib='21')]
        transition = self.pair(blocks, blocks)
        self.assertEqual(transition['keys']['K1']['matched'], 2)
        self.assertEqual(transition['keys']['K2']['matched'], 0)
        self.assertEqual(transition['keys']['K2']['ambiguous_previous'], 2)
        report = KEY.analyze(frame(10, blocks))
        self.assertEqual(report['frames'][0]['keys']['K1']['duplicated_keys'], 0)
        self.assertEqual(report['frames'][0]['keys']['K2']['duplicated_keys'], 1)

    def test_ordinal_key_silently_repairs_when_a_submission_disappears(self):
        previous = [block(node='1000', vb='10', ib='11'),
                    block(node='1000', vb='20', ib='21'),
                    block(node='1000', vb='30', ib='31')]
        current = [block(node='1000', vb='20', ib='21'),
                   block(node='1000', vb='30', ib='31')]
        transition = self.pair(previous, current)
        self.assertEqual(transition['keys']['K1']['matched'], 2)
        self.assertEqual(transition['keys']['K3']['matched'], 2)
        # K3 pairs both survivors with the wrong previous submission.
        self.assertEqual(transition['keys']['K3']['pairs_differing_from_K1'], 2)
        self.assertEqual(transition['node_serials']['shared_with_changed_scene_draw_count'], 1)

    def test_load_epoch_change_prevents_a_match(self):
        transition = self.pair([block(node='1000')], [block(node='1000', load_epoch=2)])
        self.assertEqual(transition['keys']['K1']['matched'], 0)
        self.assertEqual(transition['keys']['K2']['matched'], 0)

    def test_nonadjacent_frames_are_not_compared(self):
        report = KEY.analyze(frame(10, [block()]) + frame(20, [block()]))
        self.assertEqual(report['transitions'], [])
        self.assertEqual(len(report['bursts']), 2)

    def test_separate_devices_reusing_frame_numbers_stay_separate(self):
        lines = frame(10, [block()]) + frame(10, [block()], device='2')
        report = KEY.analyze(lines)
        self.assertEqual(len(gameplay(report)), 2)
        self.assertEqual(report['transitions'], [])

    def test_vertex_buffer_switch_on_a_stable_node_serial_is_reported(self):
        transition = self.pair([block(node='1000', vb='10', ib='11')],
                               [block(node='1000', vb='40', ib='41')])
        switches = transition['vertex_buffer_switches_on_stable_node_serial']
        self.assertEqual(switches['nodes'], 1)
        self.assertEqual(switches['examples'][0]['previous_vb'], ['10'])
        self.assertEqual(switches['examples'][0]['current_vb'], ['40'])
        self.assertEqual(switches['examples'][0]['matched_by_K1'], 0)

    def test_dramatic_linear_change_against_a_static_neighbourhood_is_flagged(self):
        previous = [block(node=f'10{index:02d}', vb=f'{index}0', ib=f'{index}1') for index in range(1, 9)]
        current = [block(node=f'10{index:02d}', vb=f'{index}0', ib=f'{index}1') for index in range(1, 8)]
        current.append(block(node='1008', vb='80', ib='81', rows=identity_rows(scale=9.0)))
        transition = self.pair(previous, current)
        self.assertEqual(transition['keys']['K1']['matched'], 8)
        report = KEY.analyze(frame(10, previous) + frame(11, current))
        self.assertEqual(report['suspicious_row_changes']['count'], 1)
        self.assertEqual(report['suspicious_row_changes']['examples'][0]['node_serial'], '1008')


class LimitTests(unittest.TestCase):
    def test_frame_draw_and_event_bounds_are_enforced(self):
        lines = frame(10, [block()])
        with self.assertRaises(ValueError):
            KEY.parse(iter(lines), {}, max_draws=1)
        with self.assertRaises(ValueError):
            KEY.parse(iter(lines), {}, max_frames=0)
        with self.assertRaises(ValueError):
            KEY.parse(iter(lines), {}, max_events=1)

    def test_duplicate_frame_begin_is_rejected(self):
        with self.assertRaises(ValueError):
            KEY.analyze(frame(10, [block()]) + ['frame_begin device=1 frame=10'])


@unittest.skipUnless(SUMMARY.exists(), 'derived summary not generated')
class SummarySelfConsistencyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.report = json.loads(SUMMARY.read_text())

    def test_provenance_names_the_expected_snapshot(self):
        self.assertEqual(self.report['provenance']['sha256'], SNAPSHOT_SHA256)
        self.assertEqual(self.report['provenance']['size'], 216605445)

    def test_totals_equal_the_sum_over_gameplay_frames(self):
        frames = gameplay(self.report)
        self.assertEqual(len(frames), self.report['totals_gameplay_scene_frames']['gameplay_scene_frames'])
        for name in ('scene_draws', 'known_lifetime', 'keyable', 'rows_verified'):
            self.assertEqual(sum(item['counts'][name] for item in frames),
                             self.report['totals_gameplay_scene_frames'][name], name)

    def test_every_gameplay_frame_has_a_well_formed_scene_bracket(self):
        for item in gameplay(self.report):
            boundary = item['boundary']
            self.assertLessEqual(boundary['first_scene_draw'], boundary['last_scene_draw'])
            self.assertLessEqual(boundary['last_scene_draw'], item['total_draws'])
            self.assertEqual(boundary['first_scene_draw'], boundary['start_after_draw'] + 1)
            self.assertEqual(item['counts']['scene_draws'],
                             boundary['last_scene_draw'] - boundary['first_scene_draw'] + 1)
            self.assertEqual(item['counts']['known_lifetime'] + item['counts']['without_known_lifetime'],
                             item['counts']['scene_draws'])
            self.assertEqual(item['keys']['K1']['distinct'], item['counts']['keyable'])

    def test_key_totals_agree_with_the_per_transition_records(self):
        for name, totals in self.report['key_totals'].items():
            self.assertEqual(sum(record['keys'][name]['matched'] for record in self.report['transitions']),
                             totals['matched'], name)
            self.assertEqual(sum(record['keys'][name]['ambiguous_previous'] for record in self.report['transitions']),
                             totals['ambiguous_previous'], name)
            self.assertLessEqual(totals['matched'], totals['keyable_draws'])

    def test_no_transition_matches_more_draws_than_it_has(self):
        for record in self.report['transitions']:
            for name, stats in record['keys'].items():
                self.assertLessEqual(stats['matched'], record['current_keyable'], (record['frame'], name))
                self.assertLessEqual(stats['matched'], record['previous_keyable'], (record['frame'], name))
            self.assertLessEqual(record['rows']['compared'], record['keys']['K1']['matched'])
            self.assertEqual(record['rows']['unchanged_rows'] + record['rows']['changed_rows'],
                             record['rows']['compared'])

    def test_argon_pair_counts_never_exceed_the_scene_phase(self):
        for item in self.report['argon_pair_per_frame']:
            self.assertLessEqual(item['argon_draws'], item['keyable_scene_draws'])
            self.assertLessEqual(item['keyable_scene_draws'], item['scene_draws'])


if __name__ == '__main__':
    unittest.main()
