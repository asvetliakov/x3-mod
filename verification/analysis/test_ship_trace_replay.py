"""ship_trace_replay.py against the host fixture (verification/probe/ship_steering_host.cpp): the Python port of the
controller equals the C++ on 4000 grid cases (every branch), the replay of the fixture's own gate runs written as
ship_trace rows (--emit-trace) has zero rate and speed residuals and floating-point heading and position residuals
(the slip term, the order-8 launch), the dock run at a station port recovers the port's roll from its aligned steps and
replays the port-roll match and the final zone, two deliberate perturbations of the gate trace (rate and remainder sign
flipped, dt doubled) and a third (W halved) are classified as orientation, position and rate controller, and the --gate
summary carries its transition facts."""
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import ship_trace_replay as R

ROOT = Path(__file__).resolve().parents[2]
DOCK = 0x5d0c4a90
TOOL = ROOT / 'verification/probe/ship_trace_replay.py'


class ShipTraceReplay(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='x3-ship-replay-')
        d = Path(cls.tmp.name)
        exe = d / 'fixture'
        build = subprocess.run(['c++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', str(ROOT / 'verification/probe/ship_steering_host.cpp'),
                                '-o', str(exe)], capture_output=True, text=True)
        if build.returncode:
            raise RuntimeError(build.stdout + build.stderr)
        cls.trace, cls.ctl = d / 'trace.log', d / 'ctl.txt'
        for flag, path in (('--emit-trace', cls.trace), ('--emit-controller', cls.ctl)):
            run = subprocess.run([str(exe), flag, str(path)], capture_output=True, text=True)
            if run.returncode:
                raise RuntimeError(run.stdout + run.stderr)
        cls.groups = {}
        for r in R.iter_rows(str(cls.trace)):
            cls.groups.setdefault(r['obj'], []).append(r)
        cls.dock = cls.groups.pop(DOCK)  # the station dock run: its own test; the others are the gate runs

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def replay(self, mutate=None):
        out = {}
        for obj, rows in self.groups.items():
            rows = [mutate(r) for r in rows] if mutate else rows
            steps, skipped, dt_match = R.replay_ship(rows)
            out[obj] = R.summarise(obj, rows, steps, skipped, dt_match)
        return out

    def test_controller_port_equals_the_fixture(self):
        n = 0
        for line in self.ctl.read_text().splitlines():
            p = line.split()
            e, w, W, acc, dt = map(int, p[1:6])
            e1, w1 = map(int, p[7:9])
            got = R.integrate_axis(e, w, W, acc, dt)
            self.assertEqual((got[0], got[1]), (e1, w1), line)
            n += 1
        self.assertEqual(n, 4000)

    def test_fixture_trace_replays_with_zero_residuals(self):
        self.assertEqual(len(self.groups), 3)
        launch = 0
        for obj, s in self.replay().items():
            self.assertGreater(s['steps'], 500)
            self.assertEqual(s['skipped'], {'gap': 0, 'no_e': 0})
            self.assertEqual(s['dt_vs_t_ms']['row'], s['steps'])
            for k in ('rate_yaw', 'rate_pitch', 'rate_roll'):
                self.assertEqual(s['residuals'][k]['max'], 0, (hex(obj), k))
            self.assertLess(s['residuals']['heading']['max'], 0.002)  # deg: fwd printed to 5 decimals
            self.assertLess(s['residuals']['pos']['max'], 0.005)  # m: pos printed in whole units
            # the speed only off at the vanilla P4 stop (speed = 0 outside the slew): an order boundary
            nonboundary = {c: n for c, n in s['step_classes'].items() if c != 'order boundary'}
            self.assertEqual(sum(nonboundary.values()), 0, (hex(obj), s['step_classes']))
            self.assertLessEqual(s['step_classes']['order boundary'], 1)
        for obj, rows in self.groups.items():  # order 8 (the vanilla M6's launch): pos = f+0x240 + axis s(u) to the unit
            steps, _, _ = R.replay_ship(rows)
            o8 = [st for st in steps if st['order'] == 8 and not st['boundary']]
            launch += len(o8)
            for st in o8:
                self.assertLess(st['pos'], 0.005, (hex(obj), st['frame']))
        self.assertGreater(launch, 20)

    def test_velocity_lag_and_the_slip_read(self):
        # amendment 14: the rows carry obj+0x60 (slip=), the replay seeds its carried slip from it and reports the velocity's lag
        # behind the nose; on the fixture's own trace the carried slip matches the row's within the 5-decimal fwd's rounding,
        # and the flown lag equals the model's
        for obj, s in self.replay().items():
            lag = s['lag']
            self.assertGreater(lag['row']['n'], 500, hex(obj))
            self.assertLess(lag['slip_res']['max'], 0.05, hex(obj))  # m/s
            self.assertLess(lag['obs_minus_model']['p95'], 0.01, hex(obj))  # deg
        m5 = self.replay()[0x3741e840]['lag']  # the M5 on the rotated gate: the order-4 lead-in turns, its velocity lags
        self.assertGreater(m5['by_order']['0x4']['p95'], 20.0)
        self.assertLess(abs(m5['row']['p95'] - m5['obs']['p95']), 1.0)
        rows = [dict(r, slip=None) for r in self.groups[0x3741e840]]  # rows before amendment 14: the lag from the displacement alone
        steps, skipped, dt_match = R.replay_ship(rows)
        old = R.summarise(0x3741e840, rows, steps, skipped, dt_match)['lag']
        self.assertEqual(old['row']['n'], 0)
        self.assertGreater(old['obs']['n'], 500)

    def test_dock_at_a_station_port(self):
        # the port's roll (30 deg = 5461 binary, floored) from the aligned match steps
        self.assertEqual(set(R.port_rolls(self.dock).values()), {5461})
        steps, skipped, _ = R.replay_ship(self.dock)
        s = R.summarise(DOCK, self.dock, steps, skipped, {'row': 0, 'next': 0, 'neither': 0})
        self.assertEqual(skipped, {'gap': 0, 'no_e': 0})
        self.assertGreater(sum(1 for r in self.dock if R.match_condition(r)), 300)  # the match ran on the approach
        self.assertGreater(s['unmodelled'].get('final zone', 0), 300)
        self.assertNotIn('port roll unknown', s['unmodelled'])
        self.assertEqual(s['residuals']['rate_yaw']['max'], 0)
        self.assertEqual(s['residuals']['rate_pitch']['max'], 0)
        # the roll of the match steps: exact but for the ship's roll read off the row's 5-decimal fwd/up (one frame of
        # roll acceleration on at most 2 steps; 0 with 9 decimals)
        self.assertLessEqual(s['residuals']['rate_roll']['max'], 10)
        self.assertLessEqual(s['step_classes']['rate controller'], 2)
        self.assertEqual(s['step_classes']['final zone'] + s['step_classes']['position'] + s['step_classes']['orientation'] +
                         s['step_classes']['speed law'] + s['step_classes']['order boundary'], 0, s['step_classes'])

    def _dominant(self, summaries):
        for obj, s in summaries.items():
            self.assertGreater(s['divergent_steps'], 100, hex(obj))
            other = {c: n for c, n in s['step_classes'].items() if c != 'order boundary'}
            yield obj, s, max(other, key=other.get), other

    def test_rate_sign_flip_is_orientation(self):
        def flip(r):
            return dict(r, rate=tuple(-x for x in r['rate']), e=None if r['e'] is None else tuple(-x for x in r['e']))
        for obj, s, top, other in self._dominant(self.replay(flip)):
            self.assertEqual(top, 'orientation', (hex(obj), other))
            self.assertEqual(other['orientation'], sum(other.values()), (hex(obj), other))
            self.assertEqual(s['residuals']['rate_yaw']['max'], 0)  # the rates still match: a convention, not the controller
            self.assertEqual({c for c, n in s['window_classes'].items() if n and c != 'order boundary'}, {'orientation'})

    def test_dt_doubled_is_position(self):
        for obj, s, top, other in self._dominant(self.replay(lambda r: dict(r, dt_ms=2 * r['dt_ms']))):
            self.assertEqual(top, 'position', (hex(obj), other))
            self.assertEqual(other['position'], sum(other.values()), (hex(obj), other))
            self.assertEqual({c for c, n in s['window_classes'].items() if n and c != 'order boundary'}, {'position'})

    def test_rate_cap_halved_is_rate_controller(self):
        def half(r):
            return dict(r, W=tuple(w // 2 for w in r['W']))
        for obj, s in self.replay(half).items():
            self.assertGreater(s['step_classes']['rate controller'], 0, hex(obj))
            self.assertEqual(s['step_classes']['orientation'] + s['step_classes']['speed law'] + s['step_classes']['position'], 0,
                             (hex(obj), s['step_classes']))

    def test_gate_mode_summary(self):
        with tempfile.TemporaryDirectory(prefix='x3-ship-replay-json-') as d:
            js = Path(d) / 'out.json'
            run = subprocess.run([sys.executable, '-I', str(TOOL), str(self.trace), '--gate', '--json', str(js)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertIn('gate orders=4/0/5/0', run.stdout)
            out = json.loads(js.read_text())
        ships = {s['obj']: s for s in out['summary']['ships']}
        self.assertEqual(len(out['steps']['0x8e19d750']), ships['0x8e19d750']['steps'])
        g = ships['0x8e19d750']['gate']
        self.assertEqual(g['order_sequence'], [4, 0, 5, 0])
        self.assertAlmostEqual(g['min_v_order5_ms'], 62.0, places=1)
        seg4, seg5 = g['segments'][0], g['segments'][2]
        for key in ('frames', 'duration_ms', 'given_first', 'given_first_rel_prev_m', 'given_drift_max_m', 'v_first_ms', 'v_last_ms',
                    'v_min_ms', 'heading_err_first_deg', 'heading_err_last_deg'):
            self.assertIn(key, seg5)
        self.assertIsNone(seg4['given_first_rel_prev_m'])
        # B - A in the identity gate frame, theta 0: (0, -0.55 + 0.8, -0.01 + 1) r with r = 1105 m
        self.assertEqual(seg5['given_first_rel_prev_m'], [0.0, 276.25, 1093.95])
        self.assertEqual(seg5['given_drift_max_m'], 0.0)
        self.assertGreater(seg4['duration_ms'], 20000)
        self.assertIsNone(ships['0x436289c8']['gate']['segments'][0]['given_first_rel_prev_m'])  # starts on order 5

    def test_help_under_isolated_mode(self):
        run = subprocess.run([sys.executable, '-I', str(TOOL), '--help'], capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertIn('--gate', run.stdout)


if __name__ == '__main__':
    unittest.main()
