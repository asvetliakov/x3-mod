"""Host contracts of the caster-candidate counter: the parser of the two log
lines (docs/architecture/shadow-replay-gates.md, "Implemented"), its sum
identities, the 16-witness cap and the launcher gate. No Wine, no game."""
import contextlib
import importlib.util
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import shadow_replay_candidates as counter  # noqa: E402

FRAME = ('shadow_replay_candidates device=1 frame={frame} routed=12 zwrite=10 slice0=7 bounds=6 origin=8 fallback=1 managed={managed} dynamic=1 default_pool=0'
         ' excluded=1 unknown=0 shadow_mismatch=0 leased={leased} capped={capped} reads=2 serial_changed={serial} readonly_after=0 writable_after={writable} pending=0'
         ' in_flight=0 quiet={quiet} cold_thread=0 stale=0 roots=1 waiting=0 nested=0 overflow=0')
WITNESS = ('shadow_replay_lock_witness device={device} frame={frame} allocation=77 flags=00000010 offset=0 size=0 thread=220'
           ' serial_delta=1 revision_delta=0')


def frame(index, managed=5, leased=5, serial=0, writable=0, quiet=None, capped=0):
    return FRAME.format(frame=index, managed=managed, leased=leased, serial=serial, writable=writable,
                        quiet=leased - serial if quiet is None else quiet, capped=capped)


CASCADE_TAIL = ' c0={c0} c1={c1} c2={c2} capped0={k0} capped1={k1} capped2={k2}'


def cascade_frame(index, c=(3, 5, 5), capped=(2, 0, 0), **kwargs):
    return frame(index, **kwargs) + CASCADE_TAIL.format(c0=c[0], c1=c[1], c2=c[2], k0=capped[0], k1=capped[1], k2=capped[2])


def launch(directory, *args):
    spec = importlib.util.spec_from_file_location('candidates_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    game = Path(directory) / 'game'; game.mkdir(exist_ok=True); (game / 'X3AP.exe').touch()
    wine = Path(directory) / 'wine'; wine.touch()
    argv = ['manage.py', 'launch', '--dry-run', '--vanilla', '--game-dir', str(game), *args]
    output, error = io.StringIO(), io.StringIO()
    with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', wine), \
            mock.patch.object(module.subprocess, 'call', side_effect=AssertionError('must never launch')), \
            contextlib.redirect_stdout(output), contextlib.redirect_stderr(error):
        try: module.main()
        except SystemExit as exit_error: return exit_error.code, output.getvalue(), error.getvalue()
    return 0, output.getvalue(), error.getvalue()


class Parser(unittest.TestCase):
    def test_synthetic_log(self):
        text = '\n'.join(['motion_route device=1 frame=0 index=3 routed=1', frame(0), WITNESS.format(device=1, frame=1),
                          frame(1, serial=1, quiet=4), 'other line', frame(2), ''])
        frames, witnesses = counter.parse_text(text)
        self.assertEqual([r['frame'] for r in frames], [0, 1, 2])
        self.assertEqual((frames[0]['shadow_mismatch'], frames[0]['stale']), (0, 0))
        stale = counter.parse_text(frame(3, leased=5, quiet=3).replace('stale=0', 'stale=2'))[0][0]
        self.assertEqual(stale['stale'], 2)  # quiet + stale == leased admits a stale record
        self.assertEqual(frames[0]['routed'], 12); self.assertEqual(frames[0]['slice0'], 7); self.assertEqual(frames[0]['managed'], 5)
        self.assertEqual(frames[1]['serial_changed'], 1); self.assertEqual(frames[1]['quiet'], 4)
        self.assertEqual(witnesses, [dict(device=1, frame=1, allocation=77, flags=0x10, offset=0, size=0, thread=220,
                                          serial_delta=1, revision_delta=0)])
        self.assertEqual(counter.check_frame_uniqueness(frames), 3)
        self.assertEqual(counter.check_witness_cap(witnesses), {1: 1})
        summary = counter.summarize(frames, witnesses)
        self.assertEqual(summary['frames'], 3); self.assertEqual(summary['slice0_p50'], 7)
        self.assertAlmostEqual(summary['managed_equals_slice0_share'], 0.0)
        self.assertAlmostEqual(summary['quiet_equals_leased_share'], 2 / 3)
        self.assertEqual(summary['predicates'], dict(managed_boundary=False, lease_contract=False, single_thread=True, promotion_possible=True))
        self.assertEqual((frames[0]['bounds'], frames[0]['origin'], frames[0]['fallback'], frames[0]['capped'], frames[0]['reads']), (6, 8, 1, 0, 2))
        self.assertEqual((summary['capped_frames'], summary['capped_total'], summary['reads_total'], summary['origin_p50']), (0, 0, 6, 8))
        self.assertAlmostEqual(summary['bounds_share'], 0.0)

    def test_bounds_and_cap(self):
        # Casters by bounds: slice0 = bounds + fallback, origin is a statistic bounded by zwrite,
        # and the per-frame cap partitions managed with leased and overflow.
        capped = frame(0, managed=5, leased=3, quiet=3, capped=2)
        rows, _ = counter.parse_text(capped)
        self.assertEqual((rows[0]['capped'], rows[0]['leased'], rows[0]['managed']), (2, 3, 5))
        summary = counter.summarize(rows, [])
        self.assertEqual((summary['capped_frames'], summary['capped_total']), (1, 2))
        steady = frame(1).replace('bounds=6 origin=8 fallback=1', 'bounds=7 origin=8 fallback=0')
        self.assertAlmostEqual(counter.summarize(counter.parse_text(steady)[0], [])['bounds_share'], 1.0)
        for bad in (frame(0).replace('bounds=6', 'bounds=5'),                       # bounds + fallback != slice0
                    frame(0).replace('origin=8', 'origin=11'),                      # origin exceeds zwrite
                    frame(0, managed=5, leased=3, quiet=3, capped=3),               # leased + capped exceeds managed
                    frame(0).replace(' capped=0', ''),                             # missing field
                    frame(0).replace('reads=2', 'reads=-1')):                      # negative
            with self.assertRaises(counter.MalformedLine, msg=bad):
                counter.parse_text(bad)

    def test_predicates_pass(self):
        lines = [FRAME.format(frame=i, managed=7, leased=7, serial=0, writable=0, quiet=7, capped=0).replace('dynamic=1', 'dynamic=0').replace('excluded=1', 'excluded=0')
                 for i in range(100)]
        frames, witnesses = counter.parse_text('\n'.join(lines))
        summary = counter.summarize(frames, witnesses)
        self.assertEqual(summary['predicates'], dict(managed_boundary=True, lease_contract=True, single_thread=True, promotion_possible=True))
        self.assertIsNone(counter.summarize([], [])['slice0_p50'])
        self.assertFalse(any(counter.summarize([], [])['predicates'].values()))

    def test_malformed_lines(self):
        good = frame(0)
        for bad in (good.replace('routed=12', 'routed=x'), good.replace(' overflow=0', ''), good + ' extra=1',
                    good.replace('slice0=7', 'slice0=8'),            # partition broken
                    good.replace('zwrite=10', 'zwrite=13'),          # chain broken
                    good.replace('quiet=5', 'quiet=6'),              # quiet exceeds leased
                    good.replace('readonly_after=0', 'readonly_after=1'),  # readonly without serial change
                    good.replace('stale=0', 'stale=1'),              # quiet + stale exceeds leased
                    good.replace('shadow_mismatch=0', 'shadow_mismatch=1'),  # partition broken by the mismatch bucket
                    WITNESS.format(device=1, frame=0).replace('flags=00000010', 'flags=zz'),
                    WITNESS.format(device=1, frame=0).replace(' revision_delta=0', '')):
            with self.assertRaises(counter.MalformedLine, msg=bad):
                counter.parse_text(bad)
        with self.assertRaises(counter.MalformedLine):
            counter.check_frame_uniqueness(counter.parse_text('\n'.join([good, good]))[0])
        self.assertEqual(counter.parse_text('shadow_replay_candidates_mode requested=1 enabled=1'), ([], []))

    def test_witness_cap(self):
        one = [WITNESS.format(device=1, frame=i) for i in range(16)]
        self.assertEqual(counter.check_witness_cap(counter.parse_text('\n'.join(one))[1]), {1: 16})
        two = one + [WITNESS.format(device=2, frame=i) for i in range(16)]
        self.assertEqual(counter.check_witness_cap(counter.parse_text('\n'.join(two))[1]), {1: 16, 2: 16})
        with self.assertRaises(counter.MalformedLine):
            counter.check_witness_cap(counter.parse_text('\n'.join(one + [WITNESS.format(device=1, frame=16)]))[1])


class LauncherGate(unittest.TestCase):
    def test_cap_option(self):
        with tempfile.TemporaryDirectory() as directory:
            code, output, _ = launch(directory, '--motion-output', '--ownership', '--shadow-replay-candidates')
            self.assertEqual(code, 0); self.assertEqual(json.loads(output)['env']['X3M_SHADOW_REPLAY_CAP'], '512')
            code, output, _ = launch(directory, '--motion-output', '--ownership', '--shadow-replay-candidates', '--shadow-replay-cap', '1024')
            self.assertEqual(code, 0); self.assertEqual(json.loads(output)['env']['X3M_SHADOW_REPLAY_CAP'], '1024')
            code, _, error = launch(directory, '--motion-output', '--ownership', '--shadow-replay-cap', '4')
            self.assertNotEqual(code, 0); self.assertIn('--shadow-replay-cap requires --shadow-replay-candidates or --shadow-replay-depth', error)

    def test_requires_motion_output_and_ownership(self):
        with tempfile.TemporaryDirectory() as directory:
            for args in (['--shadow-replay-candidates'], ['--shadow-replay-candidates', '--motion-output'],
                         ['--shadow-replay-candidates', '--ownership']):
                code, _, error = launch(directory, *args)
                self.assertNotEqual(code, 0, args); self.assertIn('--shadow-replay-candidates requires --motion-output --ownership', error)
            code, output, _ = launch(directory, '--motion-output', '--ownership', '--shadow-replay-candidates')
            self.assertEqual(code, 0)
            env = json.loads(output)['env']
            self.assertEqual(env['X3M_SHADOW_REPLAY_CANDIDATES'], '1'); self.assertEqual(env['X3M_OWNERSHIP'], '1')
            self.assertEqual(env['X3M_MOTION_OUTPUT'], '1'); self.assertEqual(env.get('X3M_LINEAR_MATERIALS', '0'), '0')
            self.assertEqual(env.get('X3M_TAA', '0'), '0'); self.assertEqual(env.get('X3M_SUN_SHADOW_LANE'), '0')
            code, output, _ = launch(directory, '--motion-output', '--ownership')
            self.assertEqual(code, 0); self.assertEqual(json.loads(output)['env']['X3M_SHADOW_REPLAY_CANDIDATES'], '0')



class CascadeFields(unittest.TestCase):
    """The optional cascade tail of the frame line (docs/architecture/
    shadow-cascades.md, section 4): c<i> then capped<i>, one pair per cascade;
    a log without it parses exactly as before."""

    def test_tail_parses_and_old_lines_are_unchanged(self):
        frames, _ = counter.parse_text(frame(1) + '\n' + cascade_frame(2) + '\n')
        self.assertNotIn('cascades', frames[0])
        self.assertEqual(frames[1]['cascades'], {'count': 3, 'records': [3, 5, 5], 'capped': [2, 0, 0]})
        self.assertEqual({k: v for k, v in frames[1].items() if k != 'cascades' and k != 'frame'}, {k: v for k, v in frames[0].items() if k != 'frame'})
        summary = counter.summarize(frames, [])
        self.assertEqual(summary['cascades'], {'frames': 1, 'count': 3, 'records_p50': [3, 5, 5], 'records_max': [3, 5, 5], 'capped_total': [2, 0, 0], 'capped_frames': [1, 0, 0]})
        self.assertNotIn('cascades', counter.summarize(frames[:1], []))

    def test_malformed_tails(self):
        good = cascade_frame(3)
        for bad in (good.replace(' c1=5', ''), good.replace('capped2=0', 'capped3=0'), good + ' c3=1', good.replace('c0=3', 'c0=x'), good.replace('c0=3', 'c0=-1'),
                    good.replace(' capped0=2 capped1=0 capped2=0', ''), good.replace('c0=3 c1=5 c2=5', 'c1=5 c0=3 c2=5')):
            with self.assertRaises(counter.MalformedLine, msg=bad[-80:]):
                counter.parse_text(bad + '\n')

    def test_pool_tails(self):
        """Caster pool control (shadow-cascade-extents.md, "Caster pool control"): the static-only group,
        the importance group, both, and their malformed forms."""
        static = ' static_only_refused0=0 static_only_refused1=1 static_only_refused2=2 large_admitted0=0 large_admitted1=1 large_admitted2=0 class_miss0=0 class_miss1=1 class_miss2=1 class_store=2 class_ring=3'
        importance = ' dropped_min_size0=0 dropped_min_size1=0.3125 dropped_min_size2=1.5e-05 select_us=12.5'
        rows, _ = counter.parse_text(cascade_frame(1) + static + '\n' + cascade_frame(2) + importance + '\n' + cascade_frame(3) + static + importance + '\n')
        self.assertEqual(rows[0]['cascades'], {'count': 3, 'records': [3, 5, 5], 'capped': [2, 0, 0], 'static_only_refused': [0, 1, 2], 'large_admitted': [0, 1, 0], 'class_miss': [0, 1, 1], 'classified': {'class_store': 2, 'class_ring': 3}})
        self.assertEqual(rows[1]['cascades'], {'count': 3, 'records': [3, 5, 5], 'capped': [2, 0, 0], 'dropped_min_size': [0.0, 0.3125, 1.5e-05], 'select_us': 12.5})
        self.assertEqual(rows[2]['cascades']['static_only_refused'], [0, 1, 2]); self.assertEqual(rows[2]['cascades']['dropped_min_size'], [0.0, 0.3125, 1.5e-05])
        summary = counter.summarize(rows, [])['cascades']
        self.assertEqual((summary['static_only_refused_total'], summary['large_admitted_total'], summary['class_miss_total'], summary['classified_total'], summary['dropped_min_size_max'], summary['select_us_max']),
                         ([0, 2, 4], [0, 2, 0], [0, 2, 2], {'class_store': 4, 'class_ring': 6}, [0.0, 0.3125, 1.5e-05], 12.5))
        for bad in (importance + static, static.replace(' class_ring=3', ''), static.replace('static_only_refused2=2 ', ''), static.replace(' large_admitted1=1', ''), static.replace(' class_miss2=1', ''), importance.replace(' select_us=12.5', ''),
                    importance.replace('0.3125', '-1'), importance.replace('0.3125', 'nan'), static.replace('class_ring=3', 'class_ring=x'), static + ' extra=1'):
            with self.assertRaises(counter.MalformedLine, msg=bad):
                counter.parse_text(cascade_frame(4) + bad + '\n')

    def test_footprint_tail(self):
        """The minimum-footprint group (--shadow-cascade-min-footprint;
        shadow-cascades.md, "Minimum caster footprint"): footprint_refused<i> then
        footprint_aged<i>, between the static-only and importance groups, absent
        while the option is off."""
        static = ' static_only_refused0=0 static_only_refused1=1 static_only_refused2=2 large_admitted0=0 large_admitted1=1 large_admitted2=0 class_miss0=0 class_miss1=1 class_miss2=1 class_store=2 class_ring=3'
        importance = ' dropped_min_size0=0 dropped_min_size1=0.3125 dropped_min_size2=1.5e-05 select_us=12.5'
        footprint = ' footprint_refused0=0 footprint_refused1=4 footprint_refused2=7 footprint_aged0=0 footprint_aged1=1 footprint_aged2=3'
        rows, _ = counter.parse_text(cascade_frame(1) + footprint + '\n' + cascade_frame(2) + static + footprint + importance + '\n' + cascade_frame(3) + '\n')
        self.assertEqual(rows[0]['cascades'], {'count': 3, 'records': [3, 5, 5], 'capped': [2, 0, 0], 'footprint_refused': [0, 4, 7], 'footprint_aged': [0, 1, 3]})
        self.assertEqual((rows[1]['cascades']['footprint_refused'], rows[1]['cascades']['class_miss'], rows[1]['cascades']['select_us']), ([0, 4, 7], [0, 1, 1], 12.5))
        self.assertNotIn('footprint_refused', rows[2]['cascades'])  # the option off: the log parses as before
        summary = counter.summarize(rows, [])['cascades']
        self.assertEqual((summary['footprint_refused_total'], summary['footprint_aged_total']), ([0, 8, 14], [0, 2, 6]))
        self.assertNotIn('footprint_refused_total', counter.summarize(rows[2:], [])['cascades'])
        for bad in (footprint.replace(' footprint_refused1=4', ''), footprint.replace(' footprint_aged2=3', ''),
                    footprint.replace('footprint_refused1=4', 'footprint_refused1=-1'), footprint.replace('footprint_aged0=0', 'footprint_aged0=x'),
                    importance + footprint, footprint + static):
            with self.assertRaises(counter.MalformedLine, msg=bad):
                counter.parse_text(cascade_frame(4) + bad + '\n')

    def test_flip_tail(self):
        """Cascade-membership flips (shadow-caster-retention.md, "Membership flips"):
        flip_c<i> then period2_c<i>, at the end of the line, alone and after the
        pool groups; a period-2 count above its frame's flip count is malformed."""
        flips = ' flip_c0=4 flip_c1=2 flip_c2=0 period2_c0=3 period2_c1=0 period2_c2=0 flip_untracked=0 flip_reset=0'
        importance = ' dropped_min_size0=0 dropped_min_size1=0.3125 dropped_min_size2=1.5e-05 select_us=12.5'
        rows, _ = counter.parse_text(cascade_frame(1) + flips + '\n' + cascade_frame(2) + importance + flips + '\n' + cascade_frame(3) + '\n')
        self.assertEqual(rows[0]['cascades'], {'count': 3, 'records': [3, 5, 5], 'capped': [2, 0, 0], 'flip': [4, 2, 0], 'period2': [3, 0, 0],
                                              'flip_untracked': 0, 'flip_reset': 0})
        self.assertEqual(rows[1]['cascades']['flip'], [4, 2, 0]); self.assertEqual(rows[1]['cascades']['select_us'], 12.5)
        self.assertNotIn('flip', rows[2]['cascades'])  # a log written before the counters existed
        seeded = cascade_frame(5) + ' flip_c0=0 flip_c1=0 flip_c2=0 period2_c0=0 period2_c1=0 period2_c2=0 flip_untracked=7 flip_reset=1'
        seed_row = counter.parse_text(seeded + '\n')[0][0]['cascades']
        self.assertEqual((seed_row['flip_reset'], seed_row['flip_untracked'], seed_row['flip']), (1, 7, [0, 0, 0]))
        summary = counter.summarize(rows, [])['cascades']
        self.assertEqual((summary['flip_total'], summary['flip_max'], summary['period2_total'], summary['period2_max'], summary['period2_frames'],
                          summary['flip_untracked_total'], summary['flip_reset_frames']),
                         ([8, 4, 0], [4, 2, 0], [6, 0, 0], [3, 0, 0], [2, 0, 0], 0, 0))
        self.assertNotIn('flip_total', counter.summarize(rows[2:], [])['cascades'])
        for bad in (flips.replace(' flip_c1=2', ''), flips.replace(' period2_c2=0', ''), flips.replace('period2_c1=0', 'period2_c1=3'),
                    flips.replace('flip_c0=4', 'flip_c0=-1'), flips.replace('flip_c0=4', 'flip_c0=x'), flips + ' extra=1', flips + importance,
                    flips.replace(' flip_untracked=0', ''), flips.replace(' flip_reset=0', ''), flips.replace('flip_reset=0', 'flip_reset=2'),
                    flips.replace('flip_reset=0', 'flip_reset=1')):  # a seeded frame cannot report flips
            with self.assertRaises(counter.MalformedLine, msg=bad):
                counter.parse_text(cascade_frame(4) + bad + '\n')

    def test_identities(self):
        with self.assertRaises(counter.MalformedLine):  # a cascade with more records than were leased
            counter.parse_text(cascade_frame(4, c=(6, 5, 5)) + '\n')
        with self.assertRaises(counter.MalformedLine):  # five leased records cannot all be missing from every cascade
            counter.parse_text(cascade_frame(5, c=(1, 1, 1)) + '\n')
        with self.assertRaises(counter.MalformedLine):  # a draw dropped from every cascade is one of the per-cascade drops
            counter.parse_text(cascade_frame(6, managed=6, capped=(0, 0, 0), **{}).replace(' capped=0 ', ' capped=1 ') + '\n')


if __name__ == '__main__':
    unittest.main()
