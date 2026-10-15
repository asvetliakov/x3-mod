"""The ship_trace row format (ship_steering.cpp, X3M_SHIP_TRACE=1): a synthetic row set parses field by field, the e=-
variant marks a frame without a leaf call, rows group per ship in order, and a row carries every input the fixture's
engine model needs plus the next row as its oracle. The format string in the DLL and the regex in
verification/probe/ship_trace_rows.py are kept in step by the sample rows below (copied from the DLL's two printf
formats with synthetic values)."""
import unittest
from pathlib import Path

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


# The gate traffic controller, stage A (gate-traffic-controller.md D.7): its rows as the DLL prints them since the stage B deletion
# (2026-10-15; ship_steering.cpp ctl_rows) and the census row's capsule fields.
CTL_ROWS = [
    'ship_gate_ctl frame=900 t_ms=25503900 gate=0x8e1edfb0 ship=0xa908da10 id=4711 seq=6 key=0 order=0x14 from=none to=escort why=escort rank=5 '
    'stage=inbound>inbound',
    'ship_gate_ctl_pair frame=901 t_ms=25504000 gate=0x8e1edfb0 a=0x8e223c48 b=0xa908da10 seq=4,6 rank=3,5 kind=verdict verdict=unresolved '
    'before=clear why=- t_c_s=12.5 yielder=b cap_ms=-1.0 d_m=2331 cause=contact trig=plan gap_m=-42',
    'ship_gate_ctl_pair frame=905 t_ms=25504100 gate=0x8e1edfb0 a=0x8e223c48 b=0xa908da10 seq=4,6 rank=3,5 kind=e_pass verdict=none '
    'before=none why=stale t_c_s=-1.0 yielder=none cap_ms=-1.0 d_m=2310 cause=stale trig=clock gap_m=na',
    'ship_gate_launch frame=902 t_ms=24274300 ship=0x8e1d0090 gate=0x8e1ef948 class=M2 capital=1 slot=-1 gate_type=3 kind=capital_offset '
    'p_m=1200,7400,-56000 off_r_m=0 off_u_m=663 r_gate_m=1105 r_m=1092 w_m=583',
    'ship_capital_census frame=100 type=11 class=M2 r_m=1092 w_m=583 w_r=0.535 w_src=lod retries=0 l_m=1092 h_rw_m=1238 len_m=1092 '
    'h_m=1238 part_id=20071 part_lod=0 o_x=145 o_y=324 e_x=169 e_y=169 l_r=1.000 capsule_L_m=1092',
    'ship_gate_ctl_member frame=904 t_ms=23272598 gate=0x8e051030 ship=0x8e049da0 class=M2 role=escort stage=funnel order=0x15 '
    'target=0x8e051030 p_m=-1200,450,-9330 nose_f=0.940 v_ms=60.0 key=49 traced=1 st_age_ms=50 dec_age_ms=310 live=1 zx=-1800',
    'ship_gate_ctl_lane frame=905 t_ms=23272598 gate=0x8e051030 ship=0x8e049da0 lane_s=-9330 rank=1 gap_m=612 law=follow lat_m=140 '
    'lc_m=0,-929 v_law=92.5 v_w=na w=none on=0 merged=1 loop=out',
    'ship_gate_ctl_lane frame=907 t_ms=23273598 gate=0x8e051030 ship=0x8e049da0 lane_s=-9230 rank=1 gap_m=na law=gate lat_m=12 '
    'lc_m=-3,-931 v_law=40.0 v_w=40.0 w=hold on=1 merged=1 loop=none',
    'ship_gate_ctl_rewrite frame=908 t_ms=23274000 gate=0x8e051030 ship=0x8e049da0 site=b slot=5 from_m=0,-666,-4394 to_m=0,-929,-1471',
    'ship_gate_ctl_rewrite frame=909 t_ms=23275000 gate=0x8e051030 ship=0x8e1d0090 site=miss slot=-1 from_m=310,-80,-3900 to_m=310,-80,-3900',
    'ship_gate_ctl_merge frame=906 t_ms=23280000 gate=0x8e051030 ship=0x8e049da0 pred=0x8e1d0090 event=finish gap_m=455 law=merge dur_ms=27400',
    'ship_gate_ctl_defer frame=910 t_ms=23281000 gate=0x8e051030 ship=0x8e049da0 order=0x0 kind=defer f210_read=23281000 f210_written=23283517 '
    'ahead_ms=2517',
]


class GateTrafficRows(unittest.TestCase):
    def test_ctl_rows(self):
        c = st.parse_kv_rows(CTL_ROWS, 'ship_gate_ctl')[0]
        self.assertEqual((c['gate'], c['ship'], c['id'], c['seq'], c['key'], c['order']), (0x8e1edfb0, 0xa908da10, 4711, 6, 0, 0x14))
        self.assertEqual((c['from'], c['to'], c['why'], c['rank'], c['stage']), ('none', 'escort', 'escort', 5, 'inbound>inbound'))
        p = st.parse_kv_rows(CTL_ROWS, 'ship_gate_ctl_pair')[0]
        self.assertEqual((p['a'], p['b'], p['seq'], p['rank']), (0x8e223c48, 0xa908da10, (4, 6), (3, 5)))
        self.assertEqual((p['verdict'], p['before'], p['t_c_s'], p['yielder'], p['cap_ms'], p['d_m']), ('unresolved', 'clear', 12.5, 'b', -1.0, 2331))
        self.assertEqual((p['kind'], p['why']), ('verdict', '-'))
        self.assertEqual((p['cause'], p['trig'], p['gap_m']), ('contact', 'plan', -42))  # B.9: the verdict's reason and the sweep's gap
        p2 = st.parse_kv_rows(CTL_ROWS, 'ship_gate_ctl_pair')[1]
        self.assertEqual((p2['kind'], p2['why'], p2['cause'], p2['trig'], p2['gap_m']), ('e_pass', 'stale', 'stale', 'clock', 'na'))
        lr = st.parse_kv_rows(CTL_ROWS, 'ship_gate_launch')[0]
        self.assertEqual((lr['ship'], lr['class'], lr['capital'], lr['slot'], lr['gate_type'], lr['kind']),
                         (0x8e1d0090, 'M2', 1, -1, 3, 'capital_offset'))
        self.assertEqual((lr['p_m'], lr['off_r_m'], lr['off_u_m'], lr['r_gate_m'], lr['w_m']), ((1200, 7400, -56000), 0, 663, 1105, 583))
        cen = st.parse_kv_rows(CTL_ROWS, 'ship_capital_census')[0]
        self.assertEqual((cen['l_r'], cen['capsule_L_m']), (1.0, 1092))
        self.assertEqual(st.parse_kv_rows(CTL_ROWS, 'ship_gate_ctl_seq'), [])  # the sequencer row was deleted with stage B
        mr = st.parse_kv_rows(CTL_ROWS, 'ship_gate_ctl_member')[0]  # B.4: one per board member per game second
        self.assertEqual((mr['gate'], mr['ship'], mr['class'], mr['role'], mr['stage'], mr['order'], mr['target']),
                         (0x8e051030, 0x8e049da0, 'M2', 'escort', 'funnel', 0x15, 0x8e051030))
        self.assertEqual((mr['p_m'], mr['nose_f'], mr['v_ms'], mr['key'], mr['traced'], mr['st_age_ms'], mr['dec_age_ms']),
                         ((-1200, 450, -9330), 0.94, 60.0, 49, 1, 50, 310))
        self.assertEqual((mr['live'], mr['zx']), (1, -1800))  # B.5: the liveness proof; X along F
        for gone in ('petal', 'rho', 's', 'stage_s', 'd_join', 't_rel', 'ringed'):  # stage B's petal frame, deleted 2026-10-15
            self.assertNotIn(gone, mr)
        ln = st.parse_kv_rows(CTL_ROWS, 'ship_gate_ctl_lane')[0]  # lane stage 1: the lane fields of a member
        self.assertEqual((ln['ship'], ln['lane_s'], ln['rank'], ln['gap_m'], ln['law']), (0x8e049da0, -9330, 1, 612, 'follow'))
        # review item 9: the lateral, l_c, the law's and the written speed, the write's kind, on Lambda, merged
        self.assertEqual((ln['lat_m'], ln['lc_m'], ln['v_law'], ln['v_w'], ln['w'], ln['on'], ln['merged']), (140, (0, -929), 92.5, 'na', 'none', 0, 1))
        ln2 = st.parse_kv_rows(CTL_ROWS, 'ship_gate_ctl_lane')[1]
        self.assertEqual((ln2['gap_m'], ln2['law'], ln2['v_w'], ln2['w'], ln2['on']), ('na', 'gate', 40.0, 'hold', 1))
        self.assertEqual((ln['loop'], ln2['loop']), ('out', 'none'))  # review of rounds A-C item 9: the loop-back join's state
        glue = (Path(__file__).resolve().parents[2] / 'src/proxy/ship_gate_ctl_glue.cpp').read_text()
        self.assertIn('merged=%u loop=%s', glue)  # (the DLL's row carries it)
        rw, miss = st.parse_kv_rows(CTL_ROWS, 'ship_gate_ctl_rewrite')  # review item 9: the per-ship order-4 point decisions
        self.assertEqual((rw['ship'], rw['site'], rw['slot'], rw['from_m'], rw['to_m']), (0x8e049da0, 'b', 5, (0, -666, -4394), (0, -929, -1471)))
        self.assertEqual((miss['site'], miss['slot'], miss['from_m'] == miss['to_m']), ('miss', -1, True))
        me = st.parse_kv_rows(CTL_ROWS, 'ship_gate_ctl_merge')[0]  # lane stage 1: a merge's completion
        self.assertEqual((me['pred'], me['event'], me['gap_m'], me['law'], me['dur_ms']), (0x8e1d0090, 'finish', 455, 'merge', 27400))
        df = st.parse_kv_rows(CTL_ROWS, 'ship_gate_ctl_defer')[0]  # lane fix round 1 item 5: the tick deferral per ship
        self.assertEqual((df['ship'], df['order'], df['kind'], df['f210_read'], df['f210_written'], df['ahead_ms']),
                         (0x8e049da0, 0, 'defer', 23281000, 23283517, 2517))
        self.assertEqual(len(st.parse_kv_rows(CTL_ROWS)), 12)

    def test_rows_of_logs_before_the_stage_b_deletion_still_parse(self):
        # Parsing is by row name and key=value: a log from before 2026-10-15 still parses. Its ship_gate_ctl_seq row (a legacy row
        # name) and the member row's deleted petal-frame fields come through as keys, and the fields an old row lacks (key=, stage=,
        # kind=) are simply absent, so a reader of old logs uses .get().
        old = [
            'ship_gate_ctl frame=900 t_ms=25503900 gate=0x8e1edfb0 ship=0xa908da10 id=4711 seq=6 order=0x14 from=none to=escort why=escort rank=5',
            'ship_gate_ctl_seq frame=903 t_ms=24470000 gate=0x8e4fbdb0 holders=3 inbound=1 cand=0x4b9cfd08 key=16 role=committed stage=hold '
            'ringed=1 busy_by=0x00000000 window=1 timing=- free_ms=-1200 dx_m=8350 v_feas=100 block_ms=0 skips=0 releases=2',
            'ship_gate_ctl_member frame=904 t_ms=23272598 gate=0x8e051030 ship=0x8e049da0 class=M2 role=escort stage=hold order=0x15 '
            'target=0x8e051030 p_m=-1200,450,-9330 nose_f=0.940 v_ms=60.0 petal=3 key=49 traced=1 st_age_ms=50 dec_age_ms=310 live=1 '
            'zx=-1800 rho=2400 s=8950 stage_s=1420 d_join=2310 t_rel=-35 ringed=0',
        ]
        self.assertIn('ship_gate_ctl_seq', st.LEGACY_KV_ROWS)
        c = st.parse_kv_rows(old, 'ship_gate_ctl')[0]
        self.assertEqual((c['why'], c['rank'], c.get('key'), c.get('stage')), ('escort', 5, None, None))
        sq = st.parse_kv_rows(old, 'ship_gate_ctl_seq')[0]
        self.assertEqual((sq['holders'], sq['cand'], sq['timing'], sq['free_ms'], sq['releases']), (3, 0x4b9cfd08, '-', -1200, 2))
        mr = st.parse_kv_rows(old, 'ship_gate_ctl_member')[0]
        self.assertEqual((mr['stage'], mr['zx'], mr['petal'], mr['rho'], mr['s'], mr['stage_s'], mr['d_join'], mr['t_rel'], mr['ringed']),
                         ('hold', -1800, 3, 2400, 8950, 1420, 2310, -35, 0))


# Lane fix round 2 item 6: the lane summary (ship_steering.cpp's 1 Hz report under the lane key) and the engine warn's pre-row, copied from
# the DLL's formats with synthetic values: the round-1 / round-2 counters, each lane gate's frame, the own-gate and obstacle rows' reasons.
LANE_ROWS = [
    'ship_steering_lane_summary frame=9100 lane=active ctl_merge_yield=3 ctl_merge_stop=1 ctl_point_rewrites=7 ctl_lane_writes=812 '
    'ctl_x_refused=2 ctl_x_wait_s=113 ctl_grid_miss=0 ctl_point_adopts=1 ctl_lane_defers=4 ctl_defer_rewrites=2 ctl_recommit_held=5 '
    'e_lane_obst=640 e_lane_obst_roll=12 gates_gate:F:G:r=0x8e1edfb0:0.0000,0.0000,1.0000:1200,-300,56000:1105|0x8e51c010:0.7071,0.0000,0.7071:-9000,0,4000:3208',
    'ship_steering_lane_summary frame=9200 lane=off ctl_merge_yield=0 ctl_merge_stop=0 ctl_point_rewrites=0 ctl_lane_writes=0 ctl_x_refused=0 '
    'ctl_x_wait_s=0 ctl_grid_miss=0 ctl_point_adopts=0 ctl_lane_defers=0 ctl_defer_rewrites=0 ctl_recommit_held=0 e_lane_obst=0 '
    'e_lane_obst_roll=0 gates_gate:F:G:r=-',
    'ship_avoid_engine_warn_pre frame=9101 obj=0x8e049da0 order=0x4 tgt=0x8e1edfb0 count=2 '
    'entries_other:class:code:order:leader:member:d_m:r_m:e_verdict=0x8e1edfb0:18:-:0x0:0x00000000:0x00000000:6900:1105:suppress_gate|'
    '0x9a00c000:5:station:0x0:0x00000000:0x00000000:3100:2500:suppress_lane_obst gate_row=lane obst_row=lane',
    'ship_avoid_engine_warn_pre frame=9102 obj=0x8e049da0 order=0x0 tgt=0x00000000 count=0 entries_other:class:code:order:leader:member:d_m:r_m:e_verdict=- '
    'gate_row=- obst_row=-',
]


class LaneRows(unittest.TestCase):
    def test_lane_summary_and_warn_pre_rows(self):
        s, s0 = st.parse_kv_rows(LANE_ROWS, 'ship_steering_lane_summary')
        self.assertEqual((s['frame'], s['lane'], s['ctl_merge_yield'], s['ctl_merge_stop'], s['ctl_point_rewrites'], s['ctl_lane_writes']),
                         (9100, 'active', 3, 1, 7, 812))
        self.assertEqual((s['ctl_x_refused'], s['ctl_x_wait_s'], s['ctl_grid_miss'], s['ctl_point_adopts'], s['ctl_lane_defers'],
                          s['ctl_defer_rewrites']), (2, 113, 0, 1, 4, 2))
        # round 1 item 4 (the re-commit hysteresis), round 2 item 1 (the lane obstacle row)
        self.assertEqual((s['ctl_recommit_held'], s['e_lane_obst'], s['e_lane_obst_roll']), (5, 640, 12))
        g = s['gates_gate:F:G:r']  # round 1 item 5: each lane gate's frame (gate, F, G, r_gate)
        self.assertEqual(g[0], (0x8e1edfb0, (0.0, 0.0, 1.0), (1200, -300, 56000), 1105))
        self.assertEqual((g[1][0], g[1][1], g[1][3]), (0x8e51c010, (0.7071, 0.0, 0.7071), 3208))
        self.assertEqual(s0['gates_gate:F:G:r'], [])
        w, w0 = st.parse_kv_rows(LANE_ROWS, 'ship_avoid_engine_warn_pre')
        self.assertEqual((w['obj'], w['order'], w['tgt'], w['count']), (0x8e049da0, 4, 0x8e1edfb0, 2))
        e = w['entries_other:class:code:order:leader:member:d_m:r_m:e_verdict']
        self.assertEqual(e[0], (0x8e1edfb0, 18, '-', 0, 0, 0, 6900, 1105, 'suppress_gate'))
        self.assertEqual((e[1][2], e[1][8]), ('station', 'suppress_lane_obst'))
        # round 1 item 5 (gate_row=: the own-gate row's reason), round 2 item 1 (obst_row=: the lane obstacle row's)
        self.assertEqual((w['gate_row'], w['obst_row']), ('lane', 'lane'))
        self.assertEqual((w0['entries_other:class:code:order:leader:member:d_m:r_m:e_verdict'], w0['gate_row'], w0['obst_row']), ([], '-', '-'))


# The docking approach (amendment 18, docking-approach.md): the 1 Hz own-container row.
DOCK_ROWS = [
    'ship_avoid_dock frame=1200 t_ms=31000400 ship=0x8e2a1000 cont=0x8e1f0040 class=6 order=0x4 parts=1 n_parts=12 path_m=412 ray_m=388 '
    'margin_m=147 reach_m=760 e=suppress_dock zone=near d_c_m=4210 r_c_m=3000 v_ms=158.4 kept=1 detour=0',
    'ship_avoid_dock frame=1201 t_ms=31000900 ship=0x8e2a1100 cont=0x8e300000 class=7 order=0x1d parts=0 n_parts=0 path_m=-1 ray_m=-1 '
    'margin_m=64 reach_m=0 e=- zone=inner d_c_m=500 r_c_m=850 v_ms=60.0 kept=0 detour=0',
]


# The arrival round (convoy-gate-transit.md section 11 "Arrival round (Run174)"): the lane summary's ring-row counters (item 4), the
# warn pre-row's ring kinds (item 4), the piloted ship's row (item 6), copied from the DLL's formats with synthetic values.
ARRIVAL_ROWS = [
    'ship_steering_lane_summary frame=9300 lane=active ctl_merge_yield=0 ctl_merge_stop=0 ctl_point_rewrites=3 ctl_lane_writes=90 ctl_x_refused=0 '
    'ctl_x_wait_s=0 ctl_grid_miss=0 ctl_point_adopts=0 ctl_lane_defers=1 ctl_defer_rewrites=1 ctl_recommit_held=0 e_lane_obst=2 '
    'e_lane_obst_roll=0 e_gate_ring=4418 e_gate_ring_kept=3 ctl_mouth_expired=1 gates_gate:F:G:r=-',
    'ship_avoid_engine_warn_pre frame=9301 obj=0x90448e28 order=0x14 tgt=0x39e43950 count=1 '
    'entries_other:class:code:order:leader:member:d_m:r_m:e_verdict=0x90429090:18:-:0x0:0x00000000:0x00000000:6900:1105:suppress_gate_ring '
    'gate_row=escort obst_row=-',
    'ship_player_pos frame=9302 t_ms=24005000 obj=0x2f0c1000 pos=-5498,10774,-57969 gate=0x90429090 d_gate_m=2040 member=0x39e43950 d_member_m=5120',
    'ship_player_pos frame=9303 t_ms=24006000 obj=0x2f0c1000 pos=-5498,10774,-57969 gate=0x90429090 d_gate_m=2040 member=0x00000000 d_member_m=-1',
]


# Review of the arrival round (convoy-gate-transit.md section 11 "Arrival round review fixes"): the gate's collision-tree read (item 1)
# and the player's verdict (item 2), copied from the DLL's formats with synthetic values.
RING_READ_ROWS = [
    'ship_gate_ring_read frame=9400 t_ms=24100000 gate=0x90429090 id=4711 type=2 board=1 state=none why=ok nodes=1 scene=679 seen=1 verdict=none',
    'ship_gate_ring_read frame=9401 t_ms=24100500 gate=0x9042a000 id=4712 type=0 board=0 state=unknown why=parent nodes=1 scene=679 seen=0 '
    'verdict=ring',
]


class ArrivalReviewRows(unittest.TestCase):
    def test_ring_read_row(self):
        a, b = st.parse_kv_rows(RING_READ_ROWS, 'ship_gate_ring_read')
        self.assertEqual((a['gate'], a['id'], a['type'], a['board'], a['state'], a['why'], a['nodes'], a['scene'], a['seen'], a['verdict']),
                         (0x90429090, 4711, 2, 1, 'none', 'ok', 1, 679, 1, 'none'))
        self.assertEqual((b['state'], b['why'], b['seen'], b['verdict']), ('unknown', 'parent', 0, 'ring'))
        root = Path(__file__).resolve().parents[2] / 'src/proxy'
        rows = (root / 'ship_steering_rows.cpp').read_text()
        self.assertIn('ship_gate_ring_read frame=%llu t_ms=%lu gate=0x%08lx id=%lu type=%u board=%u state=%s why=%s nodes=%u scene=%lu '
                      'seen=%u verdict=%s', rows)
        walk = (root / 'ship_avoidance_walk_core.h').read_text()
        for why in ('"ok"', '"gate_ptr"', '"class"', '"root"', '"scene"', '"node_ptr"', '"parent"', '"chain"', '"depth"', '"budget"',
                    '"no_scene_node"'):
            self.assertIn(why, walk)
        self.assertIn('"pass_player"', (root / 'ship_avoidance_core.h').read_text())


RUN175_ROWS = [
    'ship_gate_ctl_member frame=904 t_ms=23272598 gate=0x8e051030 ship=0x8e049da0 class=M2 role=escort stage=inbound order=0x15 '
    'target=0x8e051030 p_m=-1200,450,-9330 nose_f=0.940 v_ms=60.0 key=49 traced=1 st_age_ms=50 dec_age_ms=310 live=1 zx=-1800 w_m=454 '
    'w_ok=1 funnel=0',
    'ship_gate_ctl_lane frame=905 t_ms=23272598 gate=0x8e051030 ship=0x8e049da0 lane_s=-9330 rank=1 gap_m=na law=join lat_m=19508 '
    'lc_m=0,-929 v_law=100.0 v_w=na w=none on=0 merged=1 loop=none pt=lane own=0',
    'ship_gate_ctl_rewrite frame=908 t_ms=23274000 gate=0x8e051030 ship=0x8e049da0 site=a slot=0 from_m=0,0,-3871 to_m=0,-929,-1471 '
    'r_m=778 implied_m=831 kc_m=831',
    'ship_gate_ctl_rewrite frame=909 t_ms=23275000 gate=0x8e051030 ship=0x8e1d0090 site=miss slot=-1 from_m=310,-80,-3900 to_m=310,-80,-3900 '
    'r_m=778 implied_m=-1 kc_m=-1',
    'ship_gate_ring_node frame=785 t_ms=22669000 gate=0x916fd518 i=0 depth=1 scene=50252 f25c=0x00000000 f140=0x00000001 flags=0x00000002',
    'ship_gate_ring_reread frame=4900 t_ms=22730017 gate=0x916fd518 id=24209 nodes_before=33 nodes=33 state_before=none state=none changed=0',
    'ship_steering_handler_slow frame=31626 t_ms=23500000 site=A obj=0x9127ebb8 leaf=0 order=0x4 us=4375 cycles=12345678 snap_walk=1 snap_us=120',
]


class Run175RoundRows(unittest.TestCase):
    def test_member_lane_and_rewrite_fields(self):
        mr = st.parse_kv_rows(RUN175_ROWS, 'ship_gate_ctl_member')[0]
        self.assertEqual((mr['w_m'], mr['w_ok'], mr['funnel']), (454, 1, 0))  # item 2: the width and its read; item 3: the funnel
        ln = st.parse_kv_rows(RUN175_ROWS, 'ship_gate_ctl_lane')[0]
        self.assertEqual((ln['pt'], ln['own'], ln['lat_m']), ('lane', 0, 19508))  # item 1 / 3: the point stands, the lane does not own it
        a, miss = st.parse_kv_rows(RUN175_ROWS, 'ship_gate_ctl_rewrite')
        self.assertEqual((a['site'], a['slot'], a['r_m'], a['implied_m'], a['kc_m']), ('a', 0, 778, 831, 831))  # item 1: a script point by shape
        self.assertEqual((miss['site'], miss['implied_m'], miss['kc_m']), ('miss', -1, -1))
        glue = (Path(__file__).resolve().parents[2] / 'src/proxy/ship_gate_ctl_glue.cpp').read_text()
        self.assertIn('live=%u zx=%.0f w_m=%.0f w_ok=%u funnel=%u', glue)
        self.assertIn('merged=%u loop=%s pt=%s own=%u', glue)
        self.assertIn('r_m=%.0f implied_m=%.0f kc_m=%.0f', glue)

    def test_ring_node_reread_and_handler_rows(self):
        nd = st.parse_kv_rows(RUN175_ROWS, 'ship_gate_ring_node')[0]
        self.assertEqual((nd['gate'], nd['i'], nd['depth'], nd['scene'], nd['f25c'], nd['f140'], nd['flags']), (0x916fd518, 0, 1, 50252, 0, 1, 2))
        rr = st.parse_kv_rows(RUN175_ROWS, 'ship_gate_ring_reread')[0]
        self.assertEqual((rr['nodes_before'], rr['nodes'], rr['state_before'], rr['state'], rr['changed']), (33, 33, 'none', 'none', 0))
        hs = st.parse_kv_rows(RUN175_ROWS, 'ship_steering_handler_slow')[0]
        self.assertEqual((hs['site'], hs['obj'], hs['leaf'], hs['order'], hs['us'], hs['cycles'], hs['snap_walk'], hs['snap_us']),
                         ('A', 0x9127ebb8, 0, 4, 4375, 12345678, 1, 120))
        rows = (Path(__file__).resolve().parents[2] / 'src/proxy/ship_steering_rows.cpp').read_text()
        self.assertIn('ship_gate_ring_node frame=%llu t_ms=%lu gate=0x%08lx i=%u depth=%u scene=%lu f25c=0x%08lx f140=0x%08lx flags=0x%08lx', rows)
        self.assertIn('ship_gate_ring_reread frame=%llu t_ms=%lu gate=0x%08lx id=%lu nodes_before=%u nodes=%u state_before=%s state=%s changed=%u', rows)
        self.assertIn('ship_steering_handler_slow frame=%llu t_ms=%lu site=A obj=0x%08lx leaf=%u order=0x%x us=%lu cycles=%llu snap_walk=%u snap_us=%lu', rows)
        self.assertEqual(len(st.parse_kv_rows(RUN175_ROWS)), 7)


# Run176 diagnostics (convoy-gate-transit.md section 11 "Run176 diagnostics"), copied from the DLL's formats with synthetic values.
RUN176_ROWS = [
    'ship_steering_handler_slow frame=31626 t_ms=23500000 site=A obj=0x9127ebb8 leaf=0 order=0x4 us=4375 cycles=18800000 snap_walk=0 snap_us=0 '
    'cyc_dyn=12000 cyc_find=3000 cyc_avoid=40000 cyc_gate=18700000 cyc_steer=30000 cyc_ctl=2000 cyc_tail=13000 prev_order=0x0 new_slot=1 '
    'snap_n=57 conflicts=0',
    'ship_steering_handler_slow frame=31627 t_ms=23500050 site=A obj=0x9127ebb8 leaf=0 order=0x12 us=610 cycles=0 snap_walk=0 snap_us=0 '
    'cyc_dyn=0 cyc_find=0 cyc_avoid=0 cyc_gate=0 cyc_steer=0 cyc_ctl=0 cyc_tail=0 prev_order=0xffff new_slot=-1 snap_n=-1 conflicts=-1',
    'ship_gate_ring_read frame=847 t_ms=22669000 gate=0x90a1c340 id=24209 type=2 board=1 state=none why=ok nodes=33 scene=50252 seen=1 '
    'verdict=none parts=1 effects=32 node_rows=8',
]


class Run176DiagnosticsRows(unittest.TestCase):
    def test_handler_phases_and_ring_counts(self):
        a, b = st.parse_kv_rows(RUN176_ROWS, 'ship_steering_handler_slow')
        phases = ('cyc_dyn', 'cyc_find', 'cyc_avoid', 'cyc_gate', 'cyc_steer', 'cyc_ctl', 'cyc_tail')
        self.assertEqual(sum(a[k] for k in phases), a['cycles'])  # the phases cover the call
        self.assertEqual((a['prev_order'], a['new_slot'], a['snap_n'], a['conflicts']), (0, 1, 57, 0))
        self.assertEqual((b['prev_order'], b['new_slot'], b['snap_n'], b['conflicts'], b['cyc_gate']), (0xffff, -1, -1, -1, 0))
        r = st.parse_kv_rows(RUN176_ROWS, 'ship_gate_ring_read')[0]
        self.assertEqual((r['nodes'], r['parts'], r['effects'], r['node_rows'], r['verdict']), (33, 1, 32, 8, 'none'))
        root = Path(__file__).resolve().parents[2] / 'src/proxy'
        rows = (root / 'ship_steering_rows.cpp').read_text()
        self.assertIn('"cyc_dyn=%llu cyc_find=%llu cyc_avoid=%llu cyc_gate=%llu cyc_steer=%llu cyc_ctl=%llu cyc_tail=%llu prev_order=0x%x new_slot=%d "\n'
                      '                "snap_n=%d conflicts=%d"', rows)
        self.assertIn('"parts=%u effects=%u node_rows=%u"', rows)
        self.assertIn('ctl_decide_cyc_max=%llu ctl_decide_max_gate=0x%08lx ctl_decide_max_ship=0x%08lx ctl_decide_max_id=%lu', rows)
        walk = (root / 'ship_avoidance_walk_core.h').read_text()
        self.assertIn('constexpr unsigned kGateTreeBudget = 256;', walk)
        self.assertIn('unsigned budget = kGateTreeBudget;', walk)
        self.assertIn('kRingNodeRowsPerRead = 8', (root / 'ship_steering_internal.h').read_text())


class ArrivalRoundRows(unittest.TestCase):
    def test_ring_row_and_player_rows(self):
        s = st.parse_kv_rows(ARRIVAL_ROWS, 'ship_steering_lane_summary')[0]
        self.assertEqual((s['e_gate_ring'], s['e_gate_ring_kept'], s['ctl_mouth_expired'], s['gates_gate:F:G:r']), (4418, 3, 1, []))
        w = st.parse_kv_rows(ARRIVAL_ROWS, 'ship_avoid_engine_warn_pre')[0]
        e = w['entries_other:class:code:order:leader:member:d_m:r_m:e_verdict']
        self.assertEqual((e[0][1], e[0][8], w['gate_row'], w['order']), (18, 'suppress_gate_ring', 'escort', 0x14))
        p, p0 = st.parse_kv_rows(ARRIVAL_ROWS, 'ship_player_pos')
        self.assertEqual((p['obj'], p['pos'], p['gate'], p['d_gate_m'], p['member'], p['d_member_m']),
                         (0x2f0c1000, (-5498, 10774, -57969), 0x90429090, 2040, 0x39e43950, 5120))
        self.assertEqual((p0['member'], p0['d_member_m']), (0, -1))
        root = Path(__file__).resolve().parents[2] / 'src/proxy'
        rows = (root / 'ship_steering_rows.cpp').read_text()  # (the DLL's formats carry them)
        self.assertIn('e_gate_ring=%lu e_gate_ring_kept=%lu ctl_mouth_expired=%lu gates_gate:F:G:r=%s', rows)
        self.assertIn('ship_player_pos frame=%llu t_ms=%lu obj=0x%08lx pos=%.0f,%.0f,%.0f gate=0x%08lx d_gate_m=%.0f member=0x%08lx d_member_m=%.0f', rows)
        lane = (root / 'ship_gate_lane_core.h').read_text()
        for kind in ('"escort"', '"arrival"', '"other"', '"refused_hull"'):
            self.assertIn(kind, lane)
        self.assertIn('"suppress_gate_ring"', (root / 'ship_avoidance_core.h').read_text())


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
