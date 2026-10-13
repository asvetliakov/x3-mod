# Run162 Q2: the holders' wait split by the sequencer's reason, game and real s. The segment boundaries are frames taken from the
# evidence (sb_timeline.txt: ship_gate_ctl rows; sb_counters.txt: ctl_sb_pred / ctl_sb_misc rises; sb_track_*.txt: the speed drop at
# the head skip and the first turn-back into the petal). Reasons marked [i] are inferred from the code path (ship_gate_ctl_core.h).
# Usage: python3 -I sb_reasons.py rows.txt
import sys, os, collections as C; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import sb_time
tm = sb_time.load(sys.argv[1]); END = 45645
SEG = [  # ship, gate, from frame, to frame, reason
    ('24965', 'bdb0', 970, 6620, 'head infeasible (predictor: all 4 candidates fail, 4 predictions per game s) -> head skip key 16->32'),
    ('24965', 'bdb0', 6620, 29361, 'escort-ahead deadlock [i]: escorts key 17/18 reserved ahead of leader key 32; cruise to the petal at 40 m/s'),
    ('24965', 'bdb0', 29361, 38866, 'escort-ahead deadlock [i]: petal laps at 40 m/s'),
    ('24965', 'bdb0', 38866, 39039, 'evicted: engine 0x12 vs 3838 (M2, escort of 2782) at 2231 m'),
    ('24965', 'bdb0', 39039, 39186, 'no feasibility check (out of window after the 0x12 turn) [i]'),
    ('2782', 'c158', 32215, 34361, 'head infeasible (2 head skips: key 16->32->48)'),
    ('2782', 'c158', 34361, 34514, 'evicted: engine 0x12 (other=0)'),
    ('2782', 'c158', 34514, 35216, 'head infeasible (head skip key 48->64)'),
    ('2782', 'c158', 35216, END, 'escort-ahead deadlock [i]: escort 3583 key 49 reserved ahead of leader key 64 (to session end)'),
    ('25287', 'bdb0', 40352, 40666, 'behind escort 25763 (reserved key 33) waiting for its order 4; no backfill'),
    ('25287', 'bdb0', 40666, 41810, 'funnel busy / out of window: 25763 funnel -> 0x12 -> order 5 -> order 8 (+0.9 s)'),
    ('25287', 'bdb0', 42778, END, 're-committed at X after its own 0x12 (vs gate): back to hold, U-turn (to session end)'),
]
tot = C.defaultdict(lambda: [0.0, 0.0])
for s, g, a, b, why in SEG:
    dg = (tm.game(b) - tm.game(a)) / 1000; dr = (tm.real(b) - tm.real(a)) / 1000
    print('%-5s %s f %5d-%5d game %8.1f-%8.1f real %5.1f-%5.1f | %7.1f game s %6.1f real s | %s' % (s, g, a, b, tm.game(a) / 1000, tm.game(b) / 1000,
          tm.real(a) / 1000, tm.real(b) / 1000, dg, dr, why))
    k = why.split(':')[0].split(' (')[0]; tot[k][0] += dg; tot[k][1] += dr
print('== per reason (game s, real s)')
for k, (g, r) in sorted(tot.items(), key=lambda kv: -kv[1][0]): print('%8.1f %6.1f  %s' % (g, r, k))
