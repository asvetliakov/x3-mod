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

FRAME = ('shadow_replay_candidates device=1 frame={frame} routed=12 zwrite=10 slice0=7 managed={managed} dynamic=1 default_pool=0'
         ' excluded=1 unknown=0 shadow_mismatch=0 leased={leased} serial_changed={serial} readonly_after=0 writable_after={writable} pending=0'
         ' in_flight=0 quiet={quiet} cold_thread=0 stale=0 roots=1 waiting=0 nested=0 overflow=0')
WITNESS = ('shadow_replay_lock_witness device={device} frame={frame} allocation=77 flags=00000010 offset=0 size=0 thread=220'
           ' serial_delta=1 revision_delta=0')


def frame(index, managed=5, leased=5, serial=0, writable=0, quiet=None):
    return FRAME.format(frame=index, managed=managed, leased=leased, serial=serial, writable=writable,
                        quiet=leased - serial if quiet is None else quiet)


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

    def test_predicates_pass(self):
        lines = [FRAME.format(frame=i, managed=7, leased=7, serial=0, writable=0, quiet=7).replace('dynamic=1', 'dynamic=0').replace('excluded=1', 'excluded=0')
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


if __name__ == '__main__':
    unittest.main()
