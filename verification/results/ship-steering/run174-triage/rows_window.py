# Run174: every ship row naming OBJ (obj=/ship=/A=/a=/b=) in frames [lo, hi], with noisy fields dropped.
# Usage: python3 -I rows_window.py ROWS OBJ LO HI [--all-fields]
import sys, re
rows, obj, lo, hi = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]); full = '--all-fields' in sys.argv
drop = set('up rate e W rot rot_pct acc emg omega_cmd slip cap_age_ms tgt_class B_flags member_age_ms nose_f petal key traced st_age_ms zx rho s stage_s d_join t_rel ringed lc_m'.split())
pat = re.compile(r'(?:obj|ship|A|a|b)=' + re.escape(obj) + r'\b')
for l in open(rows, errors='replace'):
    if not l.startswith('ship_') or l.startswith('ship_steering_decision'): continue
    m = re.search(r'frame=(\d+)', l)
    if not m or not lo <= int(m[1]) <= hi or not pat.search(l): continue
    if full: print(l.rstrip()[:600]); continue
    t = l.split(); print(' '.join(x for x in t if x.split('=')[0] not in drop)[:420])
