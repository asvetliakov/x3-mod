"""The muzzle rework's lab numbers (bolt-muzzle-look.md 11 #1, #7, #8; the ledger's "Muzzle rework (2026-11-08)").

The frames come from the review's batch_shots.py on a muzzle-only plan (make_plan_muzzle.py, make_grid_muzzle.py and
make_sheets_muzzle.py here: the review's scripts with the bolt shots dropped; copy them into the output directory and run
them there, beside its shots.jsonl, plan.json and meta.json). Usage:
    python3 summarize_muzzle.py <outdir>/shots.jsonl > muzzle_rework_lab.csv
Columns per weapon: the muzzle class, shots/s, the chase core diameter and body length (px at 1440 rows: the end-on star's
longest arm, the cone, or the bloom's radius), the same at 600 m, the cockpit's, and the flash life (the stage's law:
max(min(class life, 0.5 period), 3 frames of 16.7 ms)) with its lit fraction life x rate (the review's measure).
"""
import json, re, sys

LIFE = {0: 120, 1: 60, 2: 180, 3: 80, 4: 80}
CLASSES = ['energy', 'ballistic', 'plasma', 'heavy', 'flak']
rows = {}
for line in open(sys.argv[1]):
    r = json.loads(line)
    tag, shot = r['name'].split('__')
    rows.setdefault(tag, {})[shot] = r
print('tag,mclass,hz,disc_px_chase,body_px_chase,disc_px_600m,body_px_600m,disc_px_cockpit,body_px_cockpit,life_ms,lit_fraction')


def nums(r):
    m = re.search(r'disc ([\d.]+) px across, body ([\d.]+) px', r['detail'])
    return (m.group(1), m.group(2)) if m else ('', '')


for tag, shots in rows.items():
    a = shots.get('m_chase_a')
    if not a:
        continue
    hz = a['spec']['hz']
    cls = a['look']['mclass']
    period = 1000.0 / hz
    life = max(min(LIFE[cls], 0.5 * period), 3 * 1000 / 60)
    lit = min(1.0, life * hz / 1000)
    c, rng, ck = nums(a), nums(shots.get('m_range', a)), nums(shots.get('m_cockpit', a))
    print(f'{tag},{CLASSES[cls]},{hz},{c[0]},{c[1]},{rng[0]},{rng[1]},{ck[0]},{ck[1]},{life:.0f},{lit:.2f}')
