# Run162: rises of the stage-B counters across ship_steering_summary rows (cumulative), with game/real time.
# Usage: python3 -I sb_counters.py rows.txt
import sys, os, re; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import sb_time
tm = sb_time.load(sys.argv[1])
F = ['ctl_releases', 'ctl_go_around', 'ctl_x_refused', 'ctl_x_wait_s', 'ctl_guarded', 'ctl_p2_zeroed', 'ctl_released', 'ctl_yields',
     'ctl_conflicts', 'ctl_evicted', 'ctl_sb_misc', 'ctl_sb_pred', 'arr_offset_capital', 'arr_small_side', 'arr_small_unseparated', 'small_b_rewrites',
     'ctl_sb_us_max', 'ctl_frame_us_max', 'ctl_decide_us_max', 'dock_e_us_max', 'gate_rows_dropped']
prev = {}
for l in open(sys.argv[1], errors='replace'):
    if not l.startswith('ship_steering_summary '): continue
    f = int(re.search(r' frame=(\d+)', l)[1]); d = dict(kv.split('=', 1) for kv in l.split()[1:] if '=' in kv)
    ch = ['%s %s->%s' % (k, prev.get(k, '-'), d[k]) for k in F if k in d and d[k] != prev.get(k)]
    if ch and prev: print('frame %d game_s %.1f real_s %.1f | %s' % (f, tm.game(f) / 1000, tm.real(f) / 1000, ' '.join(ch)))
    prev.update({k: d[k] for k in F if k in d})
