# Run166 Q4: health from the ship_steering_summary rows (cumulative counters): ctl_live (gone, launch, sector resets, dropped, unproven
# member-frames, overflow, us_max) per change with frame/game/real; ctl_evicted, ctl_budget_cut, gate_rows_dropped, rows_dropped, every
# own-gate pass_* > 0, handler_us_p99 max, ctl_frame_us_max; the sector values logged; and the last summary/session_end rows' frame.
# Usage: python3 -I ctl_live.py rows.txt
import sys, os, re; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import sb_time
tm = sb_time.load(sys.argv[1]); prev = None; p99 = []; last = None; sec = {}
for l in open(sys.argv[1], errors='replace'):
    for m in re.finditer(r'\bsector=(0x)?([0-9a-f]+)', l): sec[m[2]] = sec.get(m[2], 0) + 1
    if l.startswith('session_end'): print('session_end:', l.strip()[:200])
    if not l.startswith('ship_steering_summary '): continue
    d = dict(re.findall(r'(\w+)=(\S+)', l)); f = int(d['frame']); last = d; p99.append((float(d['handler_us_p99']), f))
    cur = d['ctl_live']
    if cur != prev: print('ctl_live %s at f %d game %.1f real %.1f (ctl_entered %s ctl_left %s ctl_evicted %s)' % (cur, f, tm.game(f) / 1000, tm.real(f) / 1000, d['ctl_entered'], d['ctl_left'], d['ctl_evicted']))
    prev = cur
d = last
print('last summary f %s: ctl_live %s ctl_live_us_max %s | ctl_evicted %s ctl_budget_cut %s ctl_releases %s ctl_x_refused %s ctl_go_around %s | gate_rows_dropped %s rows_dropped %s' % (
    d['frame'], d['ctl_live'], d['ctl_live_us_max'], d['ctl_evicted'], d['ctl_budget_cut'], d['ctl_releases'], d['ctl_x_refused'], d['ctl_go_around'], d['gate_rows_dropped'], d['rows_dropped']))
print('pass_* > 0:', {k: v for k, v in d.items() if k.startswith('pass_') and v != '0'}, '| stuck_total', d['stuck_total'], 'unresolved', d['unresolved'], 'snapshot_overflow', d['snapshot_overflow'], 'ctl_full', d['ctl_full'])
p99.sort(); print('handler_us_p99 max %.2f at f %d, median %.2f over %d summaries; ctl_frame_us_max %s ctl_decide_us_max %s ctl_sb_us_max(last) %s' % (
    p99[-1][0], p99[-1][1], p99[len(p99) // 2][0], len(p99), d['ctl_frame_us_max'], d['ctl_decide_us_max'], d['ctl_sb_us_max']))
print('sector values in rows:', sec)
