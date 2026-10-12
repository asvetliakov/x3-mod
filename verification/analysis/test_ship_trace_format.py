"""The ship_trace row format (ship_steering.cpp, X3M_SHIP_TRACE=1): a synthetic row set parses field by field, the e=-
variant marks a frame without a leaf call, rows group per ship in order, and a row carries every input the fixture's
engine model needs plus the next row as its oracle. The format string in the DLL and the regex in
verification/probe/ship_trace_rows.py are kept in step by the sample rows below (copied from the DLL's two printf
formats with synthetic values)."""
import unittest

import ship_trace_rows as st

ROWS = [
    'ship_trace frame=1234 t_ms=560000 obj=0x8e19d750 class=TS order=0x4 tgt=0x4b51efe8 tgt_class=18 pos=-1234567,98765,4567890 '
    'fwd=0.12345,-0.00500,0.99235 up=0.00100,0.99999,0.00400 rate=-120,35,0 v=62000 des=50000 des_engine=62000 point=-1230000,97000,4600000 '
    'given=-1200000,90000,4650000 e=-1200,300,0 dt_ms=6 W=3840,3840,3072 rot=0x00010000 rot_pct=20 vmax=62000 acc=15000 emg=1 '
    'omega_cmd=-12.50,3.25,0.00 obst=0x9030de88',
    'ship_trace frame=1235 t_ms=560006 obj=0x8e19d750 class=TS order=0x4 tgt=0x4b51efe8 tgt_class=18 pos=-1234200,98780,4568260 '
    'fwd=0.12300,-0.00480,0.99240 up=0.00100,0.99999,0.00400 rate=-118,34,0 v=62000 des=62000 des_engine=62000 point=-1200000,90000,4650000 '
    'given=-1200000,90000,4650000 e=- dt_ms=6 W=3840,3840,3072 rot=0x00010000 rot_pct=20 vmax=62000 acc=15000 emg=0 '
    'omega_cmd=0.00,0.00,0.00 obst=0x00000000',
    'ship_steering_summary frame=1235 interval_ms=1000 claims=4 calls=1 handled=1 ships=1',
    'ship_trace frame=1235 t_ms=560006 obj=0x3741e840 class=M6 order=0x5 tgt=0x4b51efe8 tgt_class=18 pos=0,0,0 '
    'fwd=0.00000,0.00000,1.00000 up=0.00000,1.00000,0.00000 rate=0,0,0 v=0 des=10000 des_engine=500 point=1,2,3 given=1,2,3 e=0,0,0 dt_ms=7 '
    'W=2211,2211,1769 rot=0x0000c000 rot_pct=10 vmax=90000 acc=10000 emg=0 omega_cmd=1.00,-1.00,0.00 obst=0x00000000',
]


class ShipTraceFormat(unittest.TestCase):
    def test_rows_parse_field_by_field(self):
        rows = st.parse_rows(ROWS)
        self.assertEqual(len(rows), 3)  # the summary line is not a trace row
        r = rows[0]
        self.assertEqual((r['frame'], r['t_ms'], r['obj'], r['class'], r['order'], r['tgt'], r['tgt_class']),
                         (1234, 560000, 0x8e19d750, 'TS', 4, 0x4b51efe8, 18))
        self.assertEqual(r['pos'], (-1234567, 98765, 4567890))
        self.assertEqual(r['fwd'], (0.12345, -0.005, 0.99235))
        self.assertEqual(r['up'], (0.001, 0.99999, 0.004))
        self.assertEqual(r['rate'], (-120, 35, 0))
        self.assertEqual((r['v'], r['des'], r['des_engine'], r['dt_ms']), (62000, 50000, 62000, 6))  # des after the module's write, des_engine before
        self.assertEqual((r['point'], r['given'], r['e']), ((-1230000, 97000, 4600000), (-1200000, 90000, 4650000), (-1200, 300, 0)))
        self.assertEqual((r['W'], r['rot'], r['rot_pct'], r['vmax'], r['acc']), ((3840, 3840, 3072), 0x10000, 20, 62000, 15000))
        self.assertEqual((r['emg'], r['omega_cmd'], r['obst']), (1, (-12.5, 3.25, 0.0), 0x9030de88))  # amendment 11
        self.assertTrue(r['leaf_call'])

    def test_a_frame_without_a_leaf_call_omits_e_and_repeats_the_engine_point(self):
        r = st.parse_rows(ROWS)[1]
        self.assertIsNone(r['e'])
        self.assertFalse(r['leaf_call'])
        self.assertEqual(r['point'], r['given'])

    def test_rows_group_per_ship_in_order_and_carry_the_model_inputs_and_the_oracle(self):
        groups = st.by_ship(st.parse_rows(ROWS))
        self.assertEqual(sorted(groups), [0x3741e840, 0x8e19d750])
        ts = groups[0x8e19d750]
        self.assertEqual([r['frame'] for r in ts], [1234, 1235])
        # the model's inputs at frame n and its oracle at frame n + 1 are both present
        for key in ('pos', 'fwd', 'up', 'rate', 'v', 'des', 'des_engine', 'given', 'dt_ms', 'W', 'rot', 'rot_pct', 'vmax', 'acc'):
            self.assertIn(key, ts[0])
            self.assertIn(key, ts[1])
        self.assertEqual(ts[1]['t_ms'] - ts[0]['t_ms'], ts[0]['dt_ms'])

    def test_a_malformed_row_is_not_a_trace_row(self):
        self.assertIsNone(st.parse_row('ship_trace frame=1 obj=0x1 pos=1,2'))
        self.assertIsNone(st.parse_row(ROWS[0].replace(' e=-1200,300,0', ' e=1,2')))

    def test_byte_rate_estimate(self):
        # the row the DLL writes is up to ~420 bytes (amendment 11 added emg, omega_cmd, obst): 6 ships at 173 fps are ~1,040 rows/s, ~440 KB/s
        longest = max(len(r) for r in ROWS if r.startswith('ship_trace'))
        self.assertLessEqual(longest, 440)
        self.assertLessEqual(6 * 173 * longest, 460_000)


if __name__ == '__main__':
    unittest.main()
