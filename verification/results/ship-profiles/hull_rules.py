"""The ship profiles' hull rules (tools/effects/ship_profiles.py: (a) thin parts, (b) thin ends, the half-width easing;
FL 69 A) on a game tree: every profiled TShips row before (both fractions 0, no easing: the 2026-11-12 profile) and
after (the module's defaults), a sweep of fraction pairs, and the Titan's (row 11) slices raw vs eased. Usage:
  python3 -I verification/results/ship-profiles/hull_rules.py [game-root] [edition] [out-dir]
Writes hull_rules_<edition>.json (the sweep, the Titan slices, old/new of the rows whose z changed), hull_rules_<edition>.txt (the tables,
also printed) and titan_profile_raw.json / titan_profile_smoothed.json (the Titan entry of ship_profiles(assets,
smooth=False / True)) into out-dir (default: this folder)."""
import json, sys, time
from pathlib import Path
R = Path(__file__).resolve().parents[3]
sys.path[:0] = [str(R / 'tools/effects'), str(R / 'tools/analysis')]
import engine_bodies, ship_profiles as sp, weapon_effects_census as census

root = Path(sys.argv[1] if len(sys.argv) > 1 else '/mnt/c/X3FL')
edition = sys.argv[2] if len(sys.argv) > 2 else 'fl'
out = Path(sys.argv[3]) if len(sys.argv) > 3 else Path(__file__).resolve().parent
# the picks: the Titan, four capitals and a TL with masts or spars (TShips ids, the row's last field)
PICKS = ['SS_SH_A_M2', 'SS_SH_T_M2', 'SS_SH_B_M2', 'SS_SH_S_M2', 'SS_SH_P_M1', 'SS_SH_A_TL']
TITAN = 11

assets, _ = engine_bodies.load_assets(root, False, edition)
t = time.monotonic()
entries, skipped, prov = sp.ship_profiles(assets)
seconds = round(time.monotonic() - t, 2)
raw_entries = sp.ship_profiles(assets, smooth=False)[0]
rows = census.data_rows(assets.get('types/TShips.txt')[0].decode('latin1'))[1:]
ident = {i: (f[-1].strip() if f else '') for i, f in enumerate(rows)}

cache, chunks = {}, {}
for e in entries:
    key = sp.scene_name(e['scene']).lower()
    if key not in chunks:
        chunks[key] = sp.scene_points(assets, sp.scene_name(e['scene']), cache)[0]


def run(part, trim, smooth=False):
    got = {}
    for key, c in chunks.items():
        pts, thin = sp.hull_points(c, part)
        got[key] = dict(sp.profile(pts, fraction=trim, smooth=smooth), parts_excluded=thin)
    return got


def ends(p):
    s = p['slices']
    return [s[0][2], s[0][3], s[-1][2], s[-1][3]]


def rel_change(o, n):
    return max(abs(n['z0'] - o['z0']), abs(n['z1'] - o['z1'])) / (o['z1'] - o['z0'])


old = run(0.0, 0.0)
new = {sp.scene_name(e['scene']).lower(): e for e in entries}
cur = run(sp.PART_FRACTION, sp.TRIM_FRACTION, sp.SMOOTH)
assert all(all(new[k][f] == v for f, v in cur[k].items()) for k in new)  # the generator's own path

per_row = []
for e in entries:
    k = sp.scene_name(e['scene']).lower()
    o, n = old[k], new[k]
    per_row.append(dict(tships_index=e['tships_index'], id=ident[e['tships_index']], scene=e['scene'],
                        **{'class': e['class']}, old_z=[o['z0'], o['z1']], new_z=[n['z0'], n['z1']],
                        trim=[n['trim_lo'], n['trim_hi']], parts_excluded=n['parts_excluded'],
                        old_ends=ends(o), new_ends=ends(n), rel=round(rel_change(o, n), 4)))

sweep = []
for part in (0.0, 0.04, 0.06, 0.08, 0.10):
    for trim in (0.0, 0.005, 0.01, 0.015, 0.02, 0.03, 0.05):
        got = run(part, trim)
        rel = [rel_change(old[k], got[k]) for k in (sp.scene_name(e['scene']).lower() for e in entries)]
        titan = got['ships\\argon\\argon_m2_scene']
        sweep.append(dict(part=part, trim=trim, rows_z_changed=sum(r > 0 for r in rel),
                          rows_over_5pct=sum(r > 0.05 for r in rel), rows_over_10pct=sum(r > 0.10 for r in rel),
                          parts_excluded=sum(got[k]['parts_excluded'] for k in got),
                          titan_z=[titan['z0'], titan['z1']], titan_ends=ends(titan)))

titan = next(e for e in entries if e['tships_index'] == TITAN)
titan_raw = next(e for e in raw_entries if e['tships_index'] == TITAN)
titan_old = old['ships\\argon\\argon_m2_scene']
step = (titan['z1'] - titan['z0']) / sp.SLICES
titan_slices = [dict(i=i, z=[round(titan['z0'] + i * step), round(titan['z0'] + (i + 1) * step)],
                     raw=titan_raw['slices'][i][2:], smoothed=titan['slices'][i][2:]) for i in range(sp.SLICES)]

lines = ['game %s, %d profiles, section %.1f s, defaults part %.3f trim %.3f smooth %s' %
         (root, len(entries), seconds, sp.PART_FRACTION, sp.TRIM_FRACTION, sp.SMOOTH),
         '', 'picks (ends: eased): row id | old z0..z1 | new z0..z1 | trim lo/hi | parts excl | '
         'old ends hx,hy / hx,hy | new ends']
fmt = '%3d %-14s | %9.0f..%9.0f | %9.0f..%9.0f | %7.0f/%7.0f | %3d | %6.0f,%6.0f / %6.0f,%6.0f | %6.0f,%6.0f / %6.0f,%6.0f'
by_id = {}
for r in per_row:
    by_id.setdefault(r['id'], r)
for pid in PICKS:
    r = by_id.get(pid)
    if r:
        lines.append(fmt % (r['tships_index'], r['id'], *r['old_z'], *r['new_z'], *r['trim'], r['parts_excluded'],
                            *r['old_ends'], *r['new_ends']))
changed = sorted((r for r in per_row if r['rel'] > 0), key=lambda r: -r['rel'])
lines += ['', 'rows with z0/z1 changed: %d of %d (rel = max end move / old length); > 5 %%: %d; > 10 %%: %d' %
          (len(changed), len(per_row), sum(r['rel'] > 0.05 for r in per_row), sum(r['rel'] > 0.10 for r in per_row)),
          'largest:']
for r in changed[:15]:
    lines.append(fmt % (r['tships_index'], r['id'], *r['old_z'], *r['new_z'], *r['trim'], r['parts_excluded'],
                        *r['old_ends'], *r['new_ends']) + ' | rel %.3f' % r['rel'])
classes = {}
for r in per_row:
    c = classes.setdefault(r['class'], [0, 0, 0])
    c[0] += 1
    c[1] += r['rel'] > 0
    c[2] += r['rel'] > 0.05
lines += ['', 'changed (> 5 %) / rows per class: ' +
          ', '.join('%s %d(%d)/%d' % (k, v[1], v[2], v[0]) for k, v in sorted(classes.items())),
          '', 'sweep (no easing): part trim | rows z changed | > 5 % | > 10 % | parts excluded | titan z0..z1 | titan ends']
for s in sweep:
    lines.append('%.3f %.3f | %3d | %3d | %3d | %5d | %9.0f..%9.0f | %6.0f,%6.0f / %6.0f,%6.0f' % (
        s['part'], s['trim'], s['rows_z_changed'], s['rows_over_5pct'], s['rows_over_10pct'], s['parts_excluded'],
        *s['titan_z'], *s['titan_ends']))
lines += ['', 'Titan row 11: z %.0f..%.0f (was %.0f..%.0f), trim lo %.0f hi %.0f, %d parts excluded; slice hx,hy '
          'old (no rules) | raw | eased' % (titan['z0'], titan['z1'], titan_old['z0'], titan_old['z1'],
                                           titan['trim_lo'], titan['trim_hi'], titan['parts_excluded'])]
for s, o in zip(titan_slices, titan_old['slices']):
    lines.append('%2d %8d..%8d | %7.0f %7.0f | %7.0f %7.0f | %7.0f %7.0f' % (s['i'], *s['z'], o[2], o[3], *s['raw'],
                                                                         *s['smoothed']))
text = '\n'.join(lines) + '\n'
(out / ('hull_rules_%s.txt' % edition)).write_text(text)
head = dict(game_root=str(root), section_seconds=seconds, provenance=prov, titan=titan, titan_slices=titan_slices,
            sweep=sweep, rows_unchanged=sum(r['rel'] == 0 for r in per_row),
            changed_rows=[dict(i=r['tships_index'], id=r['id'], cls=r['class'], old_z=[round(v) for v in r['old_z']],
                               new_z=[round(v) for v in r['new_z']], excl=r['parts_excluded'], rel=r['rel'],
                               old_ends=[round(v) for v in r['old_ends']], new_ends=[round(v) for v in r['new_ends']])
                          for r in per_row if r['rel'] > 0])
(out / ('hull_rules_%s.json' % edition)).write_text(  # compact: one key, sweep point, slice or row per line
    '{\n' + ',\n'.join('"%s": %s' % (k, '[\n' + ',\n'.join(json.dumps(x, separators=(',', ':')) for x in v) + ']'
                                      if isinstance(v, list) else json.dumps(v, separators=(',', ':')))
                        for k, v in head.items()) + '\n}\n')
(out / 'titan_profile_raw.json').write_text(json.dumps(titan_raw, indent=1) + '\n')
(out / 'titan_profile_smoothed.json').write_text(json.dumps(titan, indent=1) + '\n')
print(text)
