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
    'omega_cmd=-12.50,3.25,0.00 obst=0x9030de88 slip=-123456,7890,-45678',
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
        self.assertEqual(r['slip'], (-123456, 7890, -45678))  # amendment 14
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

    def test_rows_before_amendment_14_parse_without_slip(self):
        rows = st.parse_rows(ROWS)
        self.assertIsNone(rows[1]['slip'])
        self.assertEqual(rows[1]['obst'], 0)
        self.assertIsNone(st.parse_row(ROWS[0].replace(' slip=-123456,7890,-45678', ' slip=1,2')))

    def test_rows_with_the_traffic_controllers_cap(self):
        row = st.parse_row(ROWS[0] + ' cap_ms=25.0 cap_age_ms=340')
        self.assertEqual((row['cap'], row['cap_age_ms']), (25.0, 340))
        self.assertEqual(st.parse_row(ROWS[0] + ' cap_ms=-1.0 cap_age_ms=-1')['cap_age_ms'], -1)
        self.assertIsNone(st.parse_row(ROWS[0])['cap'])

    def test_a_malformed_row_is_not_a_trace_row(self):
        self.assertIsNone(st.parse_row('ship_trace frame=1 obj=0x1 pos=1,2'))
        self.assertIsNone(st.parse_row(ROWS[0].replace(' e=-1200,300,0', ' e=1,2')))

    def test_byte_rate_estimate(self):
        # the row the DLL writes is up to ~450 bytes (amendment 11 added emg, omega_cmd, obst; amendment 14 slip): 6 ships at 173 fps
        # are ~1,040 rows/s, ~470 KB/s
        longest = max(len(r) for r in ROWS if r.startswith('ship_trace'))
        self.assertLessEqual(longest, 470)
        self.assertLessEqual(6 * 173 * longest, 490_000)


# Amendment 16: the DLL's three new row formats (frame_boundary in ship_steering.cpp) with synthetic values; amendment 17: the lane
# row without Dx= / seq_cap=, the latch row's candidates with steps and without seq_cap= / seq_leader=, the census row's widest part.
KV_ROWS = [
    'ship_capital_lane frame=70694 A=0x3a2c2c30 B=0x8f893d48 verdict=b gap_m=-285 w_A=491 w_B=460 w_src=lod,lod foot=lane,lane '
    'e_m=12 theta_deg=1.20 ds_m=6900 v_cap=42.5 D_m=6420 route=- watch=0 lat_gap=37 mot=54 h_T=828 h_L=1121 trail=0x3a2c2c30 '
    'bind=1 held=1',
    'ship_gate_capital_latch frame=64829 obj=0x8f88cbf0 class=M1 slot=1 event=slow_join from=join to=slow_join v_cmd=25 s_m=-984 e_m=1023 '
    'psi_deg=42.50 upsi_deg=40.10 w_dps=0.100 v_ms=46.7 W_dps=9.000 alpha_dps2=0.4500 a_ms2=5.00 r_gate_m=1105 '
    'cands_v:grade:nose:vel:e:over:e_max:reached:steps=100:0:30.00:31.00:500:0:1023:1:310|60:0:25.00:26.00:300:0:1023:1:420|'
    '40:0:12.00:14.00:120:20:1023:1:530|25:0:9.13:9.50:29:10:1023:1:720 obstacle=0x00000000 lanes_other:verdict:gap=0x3a2b1b30:c:-300|0x8f893d48:s:60',
    'ship_gate_capital_latch frame=1 obj=0x8f88cbf0 class=M1 slot=1 event=pred_none from=slow_join to=fallback v_cmd=25 s_m=-100 e_m=900 '
    'psi_deg=80.00 upsi_deg=70.00 w_dps=0.000 v_ms=25.0 W_dps=9.000 alpha_dps2=0.4500 a_ms2=5.00 r_gate_m=1105 '
    'cands_v:grade:nose:vel:e:over:e_max:reached:steps=100:0:90.00:85.00:900:0:900:0:1024|60:0:90.00:85.00:900:0:900:0:1024|'
    '40:0:90.00:85.00:900:0:900:0:1024|25:0:90.00:85.00:900:0:900:0:1024 obstacle=0x3a2b1b30 lanes_other:verdict:gap=-',
    'ship_capital_census frame=100 type=211 class=M2 r_m=1092 w_m=463 w_r=0.424 w_src=lod retries=1 l_m=1092 h_rw_m=1186 len_m=1092 h_m=1186 '
    'part_id=5 part_lod=0 o_x=0 o_y=12 e_x=430 e_y=451',
    'ship_trace frame=1 t_ms=1 obj=0x1',
    # a latch row written before amendment 17 still parses
    'ship_gate_capital_latch frame=2 obj=0x8f88cbf0 class=M1 slot=1 event=unlatch from=fallback to=join v_cmd=100 s_m=-100 e_m=0 '
    'psi_deg=0.00 upsi_deg=0.00 w_dps=0.000 v_ms=25.0 W_dps=9.000 alpha_dps2=0.4500 a_ms2=5.00 r_gate_m=1105 '
    'cands_v:grade:nose:vel:e:over:e_max:reached=100:2:0.00:0.00:0:0:0:1 obstacle=0x00000000 lanes_other:verdict:gap=- seq_cap=-1.0 '
    'seq_leader=0x00000000',
]


class CapitalCrowdRows(unittest.TestCase):
    def test_lane_row(self):
        r = st.parse_kv_rows(KV_ROWS, 'ship_capital_lane')[0]
        self.assertEqual((r['frame'], r['A'], r['B'], r['verdict']), (70694, 0x3a2c2c30, 0x8f893d48, 'b'))
        self.assertEqual((r['gap_m'], r['w_A'], r['w_B'], r['w_src'], r['foot']), (-285, 491, 460, ('lod', 'lod'), ('lane', 'lane')))
        self.assertEqual((r['e_m'], r['theta_deg'], r['ds_m'], r['v_cap'], r['D_m']), (12, 1.2, 6900, 42.5, 6420))
        # amendment 16b (section 16.10.6); amendment 17: no Dx= / seq_cap= (the sequencing cap is gone)
        self.assertEqual((r['route'], r['watch'], r['lat_gap'], r['mot'], r['h_T'], r['h_L']), ('-', 0, 37, 54, 828, 1121))
        self.assertEqual((r['trail'], r['bind'], r['held']), (0x3a2c2c30, 1, 1))
        self.assertNotIn('Dx', r)
        self.assertNotIn('seq_cap', r)

    def test_latch_rows(self):
        rows = st.parse_kv_rows(KV_ROWS, 'ship_gate_capital_latch')
        self.assertEqual([r['event'] for r in rows], ['slow_join', 'pred_none', 'unlatch'])
        r = rows[0]
        self.assertEqual((r['obj'], r['class'], r['slot'], r['from'], r['to'], r['v_cmd']), (0x8f88cbf0, 'M1', 1, 'join', 'slow_join', 25))
        self.assertEqual((r['s_m'], r['e_m'], r['psi_deg'], r['upsi_deg'], r['w_dps'], r['v_ms']), (-984, 1023, 42.5, 40.1, 0.1, 46.7))
        self.assertEqual((r['W_dps'], r['alpha_dps2'], r['a_ms2'], r['r_gate_m'], r['obstacle']), (9.0, 0.45, 5.0, 1105, 0))
        cands = r['cands_v:grade:nose:vel:e:over:e_max:reached:steps']
        self.assertEqual(len(cands), 4)
        self.assertEqual(cands[3], (25, 0, 9.13, 9.5, 29, 10, 1023, 1, 720))  # amendment 17: the simulated steps
        self.assertEqual(r['lanes_other:verdict:gap'], [(0x3a2b1b30, 'c', -300), (0x8f893d48, 's', 60)])
        self.assertNotIn('seq_cap', r)
        old = rows[2]  # before amendment 17: the candidates without steps, seq_cap= / seq_leader= present
        self.assertEqual(old['cands_v:grade:nose:vel:e:over:e_max:reached'], [(100, 2, 0.0, 0.0, 0, 0, 0, 1)])
        self.assertEqual((old['seq_cap'], old['seq_leader']), (-1.0, 0))
        self.assertEqual(rows[1]['lanes_other:verdict:gap'], [])
        self.assertEqual(rows[1]['obstacle'], 0x3a2b1b30)

    def test_census_row_and_others(self):
        r = st.parse_kv_rows(KV_ROWS, 'ship_capital_census')[0]
        self.assertEqual((r['type'], r['class'], r['r_m'], r['w_m'], r['w_r'], r['w_src']), (211, 'M2', 1092, 463, 0.424, 'lod'))
        self.assertEqual((r['retries'], r['l_m'], r['h_rw_m'], r['len_m'], r['h_m']), (1, 1092, 1186, 1092, 1186))
        # amendment 17: the widest part
        self.assertEqual((r['part_id'], r['part_lod'], r['o_x'], r['o_y'], r['e_x'], r['e_y']), (5, 0, 0, 12, 430, 451))
        self.assertEqual(len(st.parse_kv_rows(KV_ROWS)), 5)  # the trace row is not one of them
        self.assertIsNone(st.parse_kv_row(KV_ROWS[4]))  # the ship_trace row
        self.assertEqual(st.parse_rows(KV_ROWS), [])  # and the kv rows are not trace rows


# The gate traffic controller, stage A (gate-traffic-controller.md D.7): its three rows and the census row's capsule fields.
CTL_ROWS = [
    'ship_gate_ctl frame=900 t_ms=25503900 gate=0x8e1edfb0 ship=0xa908da10 id=4711 seq=6 order=0x14 from=none to=escort why=escort rank=5',
    'ship_gate_ctl_pair frame=901 t_ms=25504000 gate=0x8e1edfb0 a=0x8e223c48 b=0xa908da10 seq=4,6 rank=3,5 verdict=unresolved before=clear '
    't_c_s=12.5 yielder=b cap_ms=-1.0 d_m=2331',
    'ship_gate_launch frame=902 t_ms=24274300 ship=0x8e1d0090 gate=0x8e1ef948 class=M2 capital=1 slot=-1 gate_type=3 kind=capital_offset '
    'p_m=1200,7400,-56000 off_r_m=0 off_u_m=663 r_gate_m=1105 r_m=1092 w_m=583',
    'ship_capital_census frame=100 type=11 class=M2 r_m=1092 w_m=583 w_r=0.535 w_src=lod retries=0 l_m=1092 h_rw_m=1238 len_m=1092 '
    'h_m=1238 part_id=20071 part_lod=0 o_x=145 o_y=324 e_x=169 e_y=169 l_r=1.000 capsule_L_m=1092',
    'ship_gate_ctl_seq frame=903 t_ms=24470000 gate=0x8e4fbdb0 holders=3 inbound=1 cand=0x4b9cfd08 key=16 role=committed stage=hold ringed=1 '
    'busy_by=0x00000000 window=1 timing=- free_ms=-1200 dx_m=8350 v_feas=100 block_ms=0 skips=0 releases=2',
]


class GateTrafficRows(unittest.TestCase):
    def test_ctl_rows(self):
        c = st.parse_kv_rows(CTL_ROWS, 'ship_gate_ctl')[0]
        self.assertEqual((c['gate'], c['ship'], c['id'], c['seq'], c['order']), (0x8e1edfb0, 0xa908da10, 4711, 6, 0x14))
        self.assertEqual((c['from'], c['to'], c['why'], c['rank']), ('none', 'escort', 'escort', 5))
        p = st.parse_kv_rows(CTL_ROWS, 'ship_gate_ctl_pair')[0]
        self.assertEqual((p['a'], p['b'], p['seq'], p['rank']), (0x8e223c48, 0xa908da10, (4, 6), (3, 5)))
        self.assertEqual((p['verdict'], p['before'], p['t_c_s'], p['yielder'], p['cap_ms'], p['d_m']), ('unresolved', 'clear', 12.5, 'b', -1.0, 2331))
        lr = st.parse_kv_rows(CTL_ROWS, 'ship_gate_launch')[0]
        self.assertEqual((lr['ship'], lr['class'], lr['capital'], lr['slot'], lr['gate_type'], lr['kind']),
                         (0x8e1d0090, 'M2', 1, -1, 3, 'capital_offset'))
        self.assertEqual((lr['p_m'], lr['off_r_m'], lr['off_u_m'], lr['r_gate_m'], lr['w_m']), ((1200, 7400, -56000), 0, 663, 1105, 583))
        cen = st.parse_kv_rows(CTL_ROWS, 'ship_capital_census')[0]
        self.assertEqual((cen['l_r'], cen['capsule_L_m']), (1.0, 1092))
        sq = st.parse_kv_rows(CTL_ROWS, 'ship_gate_ctl_seq')[0]  # B.2: the sequencer's state row
        self.assertEqual((sq['gate'], sq['holders'], sq['inbound'], sq['cand'], sq['key'], sq['role'], sq['stage'], sq['ringed']),
                         (0x8e4fbdb0, 3, 1, 0x4b9cfd08, 16, 'committed', 'hold', 1))
        self.assertEqual((sq['busy_by'], sq['window'], sq['timing'], sq['free_ms'], sq['dx_m'], sq['v_feas'], sq['skips'], sq['releases']),
                         (0, 1, '-', -1200, 8350, 100, 0, 2))
        self.assertEqual(len(st.parse_kv_rows(CTL_ROWS)), 5)


# The docking approach (amendment 18, docking-approach.md): the 1 Hz own-container row.
DOCK_ROWS = [
    'ship_avoid_dock frame=1200 t_ms=31000400 ship=0x8e2a1000 cont=0x8e1f0040 class=6 order=0x4 parts=1 n_parts=12 path_m=412 ray_m=388 '
    'margin_m=147 reach_m=760 e=suppress_dock zone=near d_c_m=4210 r_c_m=3000 v_ms=158.4 kept=1 detour=0',
    'ship_avoid_dock frame=1201 t_ms=31000900 ship=0x8e2a1100 cont=0x8e300000 class=7 order=0x1d parts=0 n_parts=0 path_m=-1 ray_m=-1 '
    'margin_m=64 reach_m=0 e=- zone=inner d_c_m=500 r_c_m=850 v_ms=60.0 kept=0 detour=0',
]


class DockRows(unittest.TestCase):
    def test_dock_rows(self):
        a, b = st.parse_kv_rows(DOCK_ROWS, 'ship_avoid_dock')
        self.assertEqual((a['ship'], a['cont'], a['class'], a['order'], a['parts'], a['n_parts']), (0x8e2a1000, 0x8e1f0040, 6, 4, 1, 12))
        self.assertEqual((a['path_m'], a['ray_m'], a['margin_m'], a['reach_m'], a['e'], a['zone']), (412, 388, 147, 760, 'suppress_dock', 'near'))
        self.assertEqual((a['d_c_m'], a['r_c_m'], a['v_ms'], a['kept'], a['detour']), (4210, 3000, 158.4, 1, 0))
        self.assertEqual((b['order'], b['parts'], b['path_m'], b['e'], b['zone']), (0x1d, 0, -1, '-', 'inner'))
        self.assertEqual(st.parse_rows(DOCK_ROWS), [])  # not trace rows


if __name__ == '__main__':
    unittest.main()
