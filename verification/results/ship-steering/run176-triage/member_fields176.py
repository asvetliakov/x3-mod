# Run176 Q1/Q4: per ship and gate from the joined ship_gate_ctl_member + ship_gate_ctl_lane rows (the lane row follows its member row):
# rows, game-time span, funnel=0 rows (game s covered, first/last), funnel toggles, w_ok=0 rows, own=0 rows, pt=point rows, loop= state counts,
# law counts inside / outside the funnel, lane writes (w != none) outside the funnel, v (m/s) and |lateral| (m, from p_m R,U) on funnel=0 rows,
# rank changes (swaps of the ship ahead: lane row rank changes while on a lane stage), and the funnel=0 -> 1 entry rows (t, |p|, lateral, s).
# Usage: python3 -I member_fields176.py ROWS
import sys, re, math, collections as C
kv = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
st = {}; m = {}
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_gate_ctl_member '): d = kv(l); m[(d['ship'], d['gate'])] = d; continue
    if not l.startswith('ship_gate_ctl_lane '): continue
    d = kv(l); k = (d['ship'], d['gate']); M = m.pop(k, None)
    if not M: continue
    t = int(d['t_ms']) / 1000; p = [float(x) for x in M['p_m'].split(',')]
    s = st.setdefault(k, dict(n=0, t0=t, t1=t, f0=0, f0s=0.0, f0t=[], tog=0, wok0=0, own0=0, pt=0, loop=C.Counter(), law_in=C.Counter(), law_out=C.Counter(),
                              w_out=C.Counter(), v_out=[], lat_out=[], rank_ch=0, last=None, lt=t, lr=None, entries=[], roles=C.Counter()))
    s['n'] += 1; s['t1'] = t; s['roles'][M['role'] + '/' + M['stage']] += 1
    fun = M.get('funnel'); s['loop'][d['loop']] += 1
    if M.get('w_ok') == '0': s['wok0'] += 1
    if d.get('own') == '0': s['own0'] += 1
    if d.get('pt') == 'point': s['pt'] += 1
    if fun == '0':
        s['f0'] += 1; s['f0s'] += t - s['lt']; s['law_out'][d['law']] += 1; s['v_out'].append(float(M['v_ms'])); s['lat_out'].append(math.hypot(p[0], p[1]))
        if d['w'] != 'none': s['w_out'][d['w']] += 1
        if not s['f0t']: s['f0t'] = [t, t]
        s['f0t'][1] = t
    else: s['law_in'][d['law']] += 1
    if s['last'] is not None and fun != s['last']:
        s['tog'] += 1
        if fun == '1': s['entries'].append('%.1f |p|=%.0f lat=%.0f s=%.0f v=%s' % (t, math.dist((0, 0, 0), p), math.hypot(p[0], p[1]), p[2], M['v_ms']))
    s['last'] = fun; s['lt'] = t
    if M['stage'] in ('inbound', 'funnel') and d['rank'] != '255':
        if s['lr'] is not None and d['rank'] != s['lr']: s['rank_ch'] += 1
        s['lr'] = d['rank']
for (sh, g), s in sorted(st.items(), key=lambda x: x[1]['t0']):
    print('%s gate=%s rows=%d t=%.1f..%.1f roles=%s' % (sh, g, s['n'], s['t0'], s['t1'], dict(s['roles'])))
    print('   funnel=0 rows=%d (~%.0f game s, %s) toggles=%d w_ok=0 rows=%d own=0 rows=%d pt=point rows=%d rank changes=%d' % (
        s['f0'], s['f0s'], '%.1f..%.1f' % tuple(s['f0t']) if s['f0t'] else '-', s['tog'], s['wok0'], s['own0'], s['pt'], s['rank_ch']))
    print('   loop=%s law_in=%s law_out=%s w_out=%s' % (dict(s['loop']), dict(s['law_in']), dict(s['law_out']), dict(s['w_out'])))
    if s['v_out']: print('   outside: v %.0f..%.0f mean %.0f m/s, lateral %.0f..%.0f m' % (min(s['v_out']), max(s['v_out']), sum(s['v_out']) / len(s['v_out']), min(s['lat_out']), max(s['lat_out'])))
    for e in s['entries']: print('   enter funnel', e)
