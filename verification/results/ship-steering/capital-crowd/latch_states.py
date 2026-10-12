"""Run158 gate 2: when and from which state the capital join fallback latched.
Usage: LC_ALL=C grep -aE '^(ship_gate_capital_join|ship_steering_summary|ship_avoid_detour)' /mnt/c/X3/x3m.log > j.txt; python3 -I latch_states.py j.txt
1. the summary interval (end frame) in which gate_capital_fallback_pred / _past_x / gate_capital_join incremented (all capitals,
   traced or not); 2. per traced fallback capital: its first join row and the last join-mode row before the latch;
3. order-4 capital-vs-capital detour rows of each before its latch (detours exist for untraced ships too)."""
import re, sys, collections as C
CAPS = ('0x8f88cbf0', '0x3a2c2c30', '0x8f893d48')
ALL = set(CAPS) | {'0x8f43c4a8', '0xa4c06c10', '0x3a2b1b30'}
prev = {}; J = C.defaultdict(list); D = C.Counter(); first_det = {}
F = ('frame', 'mode', 'reason', 's_m', 'e_m', 'nose_deg', 'vel_deg', 'v_ms', 'v_cmd', 'pred_nose_deg', 'pred_e_m')
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_steering_summary'):
        f = re.search(r'frame=(\d+)', l).group(1)
        for k in ('gate_capital_fallback_pred', 'gate_capital_fallback_past_x', 'gate_capital_join'):
            m = re.search(' ' + k + r'=(\d+)', l)
            if m and int(m.group(1)) != prev.get(k, 0): print('interval end', f, k, prev.get(k, 0), '->', m.group(1))
            if m: prev[k] = int(m.group(1))
    elif l.startswith('ship_gate_capital_join'):
        d = dict(re.findall(r'(\w+)=(\S+)', l))
        if d['obj'] in CAPS: J[d['obj']].append(d)
    elif l.startswith('ship_avoid_detour'):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); f = int(d['frame'])
        if d['obj'] in CAPS and d['obstacle'] in ALL and d['order'] == '0x4' and 55000 <= f <= 77000:
            D[(d['obj'], d['obstacle'])] += 1; first_det.setdefault(d['obj'], f)
for o in CAPS:
    rows = J[o]; print('==', o, 'first order-4 cap-vs-cap detour frame', first_det.get(o), dict((k[1], n) for k, n in D.items() if k[0] == o))
    if rows: print('  first join row:', ' '.join('%s=%s' % (k, rows[0].get(k)) for k in F))
    lj = [r for r in rows if r['mode'] == 'join']
    if lj: print('  last join-mode row:', ' '.join('%s=%s' % (k, lj[-1].get(k)) for k in F))
    fb = [r for r in rows if r['mode'] == 'fallback']
    if fb: print('  first fallback row:', ' '.join('%s=%s' % (k, fb[0].get(k)) for k in F))
