"""sizes.py <fixture.log>: the three proving scenarios' bolt and muzzle pixel sizes (the D3D fixture's BOLT_SIZE rows,
weapon_effects_bolt_draw_cases.h scenario_sizes: each bolt rendered alone through the production in-place draw, its lit
width across the axis and lit length along it at 10 % of its own peak, measure.py's method) against the targets of
docs/architecture/bolt-muzzle-look.md (section 1's table and law, 2.4, 2.6, 3.1, 6). One markdown row per scenario,
resolution and time (the nearest bolt) and per muzzle row."""
import re
import sys

F1440, F1080 = 960.0, 720.0
rows = []
for line in open(sys.argv[1], encoding='utf-8', errors='replace'):
    if line.startswith('BOLT_SIZE '):
        rows.append(dict((k, v) for k, v in re.findall(r'(\w+)=(\S+)', line)))
# the targets per scenario: (weapon w m, core L m, own ceiling) from the note's table and law
W = {'own_fire_chase': ('stock IRE w 1.875 m', 1.875, 23.4, True), 'incoming_cockpit': ('Mayhem PAC w 2.42 m', 2.42, 32.6, False),
     'stream_station_3km_1x': ('Mayhem PSG w 6.98 m', 6.98, 104.7, False),
     'stream_station_3km_seta6': ('Mayhem PSG w 6.98 m', 6.98, 104.7, False)}
print('| scenario | screen | t ms | dist m | body px (law: w f / d, floors, ceiling) | lit width px (10 %) | core px (law: L f / d) | lit length px (10 %) |')
print('| --- | --- | --- | --- | --- | --- | --- | --- |')
for r in rows:
    if r.get('bolt') != '0':
        continue
    case, h = r['case'], int(r['height'])
    f = F1440 if h == 1440 else F1080
    _, w, L, own = W[case]
    d = float(r['dist_m'])
    law_w = max(w * f / d, 3.0)
    if own:
        law_w = min(law_w, 16.0 * h / 1440)
    c = float(r['c'])
    print(f"| {case} | {r['width']}x{h} | {r['t_ms']} | {d:.0f} | {float(r['body_px']):.2f} ({law_w:.2f}{', c ' + format(c, '.2f') if c < 0.5 else ''}) "
          f"| {float(r['lit_width_px']):.2f} | {float(r['core_px']):.2f} ({L * f / d * (1 if c >= 0.99 else c):.2f}) | {float(r['lit_length_px']):.2f} |")
print()
print('| scenario | screen | t ms | muzzle disc px (target) | cone px | glows |')
print('| --- | --- | --- | --- | --- | --- |')
for r in rows:
    if 'muzzles' in r:
        tgt = '>= 40 at 1440' if r['case'] == 'own_fire_chase' else 'floor 3'
        print(f"| {r['case']} | {r['width']}x{r['height']} | {r['t_ms']} | {float(r['disc_px']):.2f} ({tgt}) | {float(r['cone_px']):.2f} | {r['glows']} |")
