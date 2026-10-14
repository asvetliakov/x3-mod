# Run163 Q2/Q4: the sequencer's ship_gate_ctl_seq rows (one per gate per game second, --debug) run-length grouped by state
# (cand, key, role, stage, ringed, busy_by, window, timing, v_feas sign, block>0), with game/real span, row count and the dx/free_ms/v_feas ranges.
# Ship pointers are shown as ids (from ship_gate_ctl rows). Usage: python3 -I sb_seq.py rows.txt gate_suffix [--from F] [--to F]
import sys, os, re; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import sb_time
a = sys.argv; gs = a[2]; f0 = int(a[a.index('--from') + 1]) if '--from' in a else 0; f1 = int(a[a.index('--to') + 1]) if '--to' in a else 10**9
tm = sb_time.load(a[1]); ids = {'0x00000000': '-'}; R = []
for l in open(a[1], errors='replace'):
    if l.startswith('ship_gate_ctl '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); ids[d['ship']] = d['id']
    elif l.startswith('ship_gate_ctl_seq ') and ('gate=0x' in l) and re.search(r'gate=\S*' + gs + r' ', l):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); f = int(d['frame'])
        if f0 <= f <= f1: R.append(d)
nm = lambda p: ids.get(p, p)
cur = None; grp = []
def flush():
    if not grp: return
    d0, d1 = grp[0], grp[-1]; F0, F1 = int(d0['frame']), int(d1['frame'])
    rng = lambda k: '%s..%s' % (min(float(x[k]) for x in grp), max(float(x[k]) for x in grp))
    print('f %6d-%6d g %8.1f-%8.1f r %5.1f-%5.1f n %4d | cand %-6s key %-3s %-9s %-7s ringed %s busy %-6s win %s tim %s | hold %s inb %s dx %s free_ms %s v_feas %s block_ms %s skips %s rel %s' % (
        F0, F1, int(d0['t_ms'])/1000, int(d1['t_ms'])/1000, tm.real(F0)/1000, tm.real(F1)/1000, len(grp), nm(d0['cand']), d0['key'], d0['role'], d0['stage'],
        d0['ringed'], nm(d0['busy_by']), d0['window'], d0['timing'], d0['holders'], d0['inbound'], rng('dx_m'), rng('free_ms'), rng('v_feas'), rng('block_ms'), d1['skips'], d1['releases']))
for d in R:
    k = (d['cand'], d['key'], d['role'], d['stage'], d['ringed'], d['busy_by'], d['window'], d['timing'], float(d['v_feas']) > 0, int(d['block_ms']) > 0, d['holders'], d['inbound'])
    if k != cur: flush(); grp = []; cur = k
    grp.append(d)
flush()
