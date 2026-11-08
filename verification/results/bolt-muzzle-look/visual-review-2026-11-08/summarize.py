"""Per-weapon numbers from shots.jsonl (the lab's own detail strings) -> weapons.csv. Lit fraction = class life / refire period."""
import json, re, csv, sys, os
D = os.path.dirname(os.path.abspath(__file__))
meta = json.load(open(f'{D}/meta.json'))
rows = {}
for l in open(f'{D}/shots.jsonl'):
    r = json.loads(l); tag, shot = r['name'].split('__'); rows.setdefault(tag, {})[shot] = r
LIFE = {0: 120, 1: 60, 2: 180, 3: 80, 4: 80}; CLS = {0: 'energy', 1: 'ballistic', 2: 'plasma', 3: 'heavy', 4: 'flak'}
def num(s, key):
    m = re.search(key + r' ([\d.]+)', s); return float(m.group(1)) if m else None
out = csv.writer(open(sys.argv[1], 'w'))
out.writerow(['tag', 'view', 'style', 'tier', 'k', 'w_m', 'L_m', 'T_m', 'tint', 'mclass', 'hz', 'body_px_deng', 'axis_px_deng', 'streak_px_seta6', 'g_s_seta6',
              'own_body_px', 'own_axis_px', 'in60_body_px', 'in60_halo_sigma', 'disc_px_chase', 'disc_px_600m', 'lit_fraction'])
for tag, sh in rows.items():
    m = meta[tag]; lk = sh['b_side_a']['look']
    d = lambda s, k: num(sh[s]['detail'], k) if s in sh else None
    hz = m['hz']; life = max(LIFE[lk['mclass']], 50); lit = min(1.0, life * hz / 1000) if hz else None
    out.writerow([tag, m['view'], m['style'], ['fighter', 'corvette', 'capital'][lk['tier']], round(lk['k'], 2), round(lk['w'], 2), round(lk['L'], 1), round(lk['T'], 1),
                  '/'.join(f'{x:.2f}' for x in lk['tint']), CLS[lk['mclass']], hz, d('b_side_a', 'body'), d('b_side_a', 'axis'), d('b_seta6', 'streak'), d('b_seta6', 'g_s'),
                  d('b_own_a', 'body'), d('b_own_a', 'axis'), d('b_in60', 'body'), d('b_in60', 'halo sigma'), d('m_chase_a', 'disc'), d('m_range', 'disc'), round(lit, 2) if lit else None])
print('rows', len(rows))
