#!/usr/bin/env python3
"""FL run 34 (own Argon Heavy Centaur SS_SH_A_M6M, chase view): why two of the four hidden-emitter plumes look shifted.
Read-only over C:\\X3FL (no launch, no Wine). Prints (1) the scene's fx_engine_emitter parts (position, rotation
fields, structure), (2) the hull body's exhaust nozzles: the connected components of the exhaust-material faces
(metals\\argon\\metal_argon_exhaust_*) behind z < STERN_Z, each with its bounding box and centre, and (3) each emitter
against its nearest nozzle component: dx, dy, dz in scene units and in record units (x 0.01, the context scale).
The record origin the mod builds is the emitter node's +0xb0 x 0.01 (engine_effects_core.h far_record) with no lateral
term, so (3) is also the expected plume-centre error when the engine's world row equals the scene position.

  python3 verification/results/fl-run34/plume_offset_triage.py [--fl /mnt/c/X3FL] [--log LOG]
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'verification/results/plumes-no-glow'))
import census as K  # noqa: E402

C, bob1 = K.C, K.bob1
STERN_Z = -25000
SHIP = 'SS_SH_A_M6M'
HULL = 'ships\\mm6\\argon_mm6'
EXHAUST = re.compile(r'metal_argon_exhaust_', re.I)
SCALE = 0.01


def emitters(assets):
    ships, _ = C.tships(assets)
    s = next(s for s in ships if s['id'] == SHIP)
    data, _ = C.scene_member(assets, s['scene'])
    txt = data.decode('latin1')
    out, nparts = [], 0
    for m in re.finditer(r'^P (\d+);([^\n]*)\n((?:\{[^}]*\}[^\n]*\n)+)', txt, re.M):
        nparts += 1
        if 'fx_engine_emitter' not in m.group(2):
            continue
        frames = re.findall(r'\{([^}]*)\}', m.group(3))
        f = [x.strip() for x in frames[0].split(';') if x.strip()]
        out.append(dict(part=int(m.group(1)), name=re.search(r'N ([^;]+);', m.group(2)).group(1).strip(),
                        pos=np.array([float(f[1]), float(f[2]), float(f[3])]), rot=[float(x) for x in f[4:8]],
                        flags=f[0], frames=len(frames), header=m.group(2).split('//')[0].strip()))
    return s, nparts, out


def nozzles(assets):
    tree, *_ = C.load_body(assets, HULL)
    rec = bob1.lods(tree)[0]
    pts = np.array([q[1:4] for q in rec['points']], np.float64) * (rec['value'] / 65536.0)
    names = [' '.join(v.decode('latin1') for _, t, v in m.get('params', []) if t == 8) for m in bob1.materials(tree)]
    mats = {i for i, n in enumerate(names) if EXHAUST.search(n)}
    key = [tuple(p) for p in pts.round()]
    faces = [f[:3] for p in rec['parts'] for g in p['groups'] if g['material'] in mats for f in g['faces']
             if min(pts[i][2] for i in f[:3]) < STERN_Z]
    par = {}

    def find(a):
        while par.setdefault(a, a) != a:
            par[a] = par[par[a]]
            a = par[a]
        return a
    for f in faces:
        for i in f[1:]:
            ra, rb = find(key[f[0]]), find(key[i])
            if ra != rb:
                par[ra] = rb
    comps = {}
    for f in faces:
        comps.setdefault(find(key[f[0]]), set()).update(key[i] for i in f)
    res = []
    for v in comps.values():
        a = np.array(sorted(v))
        lo, hi = a.min(0), a.max(0)
        res.append(dict(n=len(a), lo=lo, hi=hi, c=(lo + hi) / 2))
    return sorted(mats), sorted(res, key=lambda r: (r['c'][2], r['c'][0]))


def main(argv=None):
    a = argparse.ArgumentParser()
    a.add_argument('--fl', default='/mnt/c/X3FL')
    a.add_argument('--log')
    args = a.parse_args(argv)
    assets, _ = K.lod_overlay.original_assets(args.fl, edition='fl')
    s, nparts, em = emitters(assets)
    print(f'== {SHIP}: scene {s["scene"]}, {nparts} parts; fx_engine_emitter parts {len(em)}')
    for e in em:
        print(f'  P{e["part"]} {e["name"]}: pos {e["pos"].tolist()} rot {e["rot"]} flags {e["flags"]} frames {e["frames"]} '
              f'| {e["header"]}')
    mats, comps = nozzles(assets)
    print(f'== hull {HULL}: exhaust materials {mats}; exhaust components behind z<{STERN_Z}: {len(comps)}')
    for c in comps:
        print('  n=%3d x %6.0f..%6.0f y %6.0f..%6.0f z %6.0f..%6.0f centre (%6.0f, %6.0f, %6.0f)' % (
            c['n'], c['lo'][0], c['hi'][0], c['lo'][1], c['hi'][1], c['lo'][2], c['hi'][2], *c['c']))
    print('== emitter vs nearest exhaust component centre (y within 1000 of the emitter: same deck):')
    print('  emitter            pos(x,y,z)              nozzle centre            dx     dy     dz  |dx| outward?  dx_rec  dy_rec  dz_rec  nozzle half-width')
    for e in em:
        same = [c for c in comps if abs(c['c'][1] - e['pos'][1]) < 1000] or comps
        c = min(same, key=lambda c: np.linalg.norm(c['c'] - e['pos']))
        d = e['pos'] - c['c']
        out = 'yes' if abs(e['pos'][0]) > abs(c['c'][0]) + 50 else 'no'
        print('  %-20s (%6.0f,%6.0f,%6.0f)  (%6.0f,%6.0f,%6.0f)  %6.0f %6.0f %6.0f  %-3s  %7.2f %7.2f %7.2f  %6.0f' % (
            e['name'], *e['pos'], *c['c'], *d, out, *(d * SCALE), (c['hi'][0] - c['lo'][0]) / 2))
    if args.log:
        for pat in ('engine_plumes_hidden_emitter_hull', 'engine_plumes_anchor', 'anchor_record'):
            r = subprocess.run(['grep', '-a', '-c', pat, args.log], capture_output=True, text=True)
            print(f'== log rows matching {pat}: {r.stdout.strip()}')


if __name__ == '__main__':
    main()
