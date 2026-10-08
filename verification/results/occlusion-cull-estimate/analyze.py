#!/usr/bin/env python3
"""Occlusion-cull estimate per capture frame from extract.py JSON + the depth readbacks.

Bounds: object_bounds rows (draw's vertex-extent box through its own clip rows: screen box
sx0..sy1, device-depth zmin). Depth: depth_<d>_<f>.rgba32f (.r device depth z/w, .b view
depth w, -1 = no routed draw). The box's nearest view depth w_min = B / (A - zmin) with
A, B fitted per frame from the readback's (.r, .b) pairs (z = A - B / w). A footprint pixel
"hides" the box when its .b < w_min * (1 - tol); sentinel pixels never hide.
fully hidden = every footprint pixel hides; mostly hidden = > 95 %.
Usage: analyze.py <extract.json> <run_dir> <out_prefix> [tol ...]
"""
import json, re, sys
from collections import Counter, defaultdict
from pathlib import Path
import numpy as np

def dock_model(model):
    # src/proxy/cull_small_parts_core.h:260 dock_model(): stock dock cut-scene inline bodies
    try:
        v = int(model, 16)
    except (TypeError, ValueError):
        return False
    return 901300000 <= v < 901500000 or 909800000 <= v < 910000000

def classify(body, engine_name, model, dock_models):
    if engine_name:
        return 'jet'
    if body is None:
        return 'unjoined'
    b = body.lower()
    if dock_model(model):
        return 'dock'
    if b == '-':
        return 'unnamed'
    if 'effects\\engines' in b:
        return 'jet'
    if 'turret' in b or 'weapon' in b or 'gbarrel' in b:
        return 'turret'
    if 'dock' in b:
        return 'dock'
    if 'menugfx' in b or b.startswith('environments') or 'gate_effect' in b:
        return 'hud_env'
    if b.startswith('ships\\props') or 'antenna' in b:
        return 'other_part'
    return 'hull'

SUBPARTS = ('turret', 'dock', 'jet', 'other_part')

def per_draw_cost(R, frame):
    """view_submit_p50_us / median frame_end draws over the frame_phases window containing frame."""
    fe = {int(k): v for k, v in R['frame_end'].items()}
    best = None
    for p in R['phases']:
        lo, hi = p['frame'] - p['frames'] + 1, p['frame']
        if lo <= frame <= hi + 300:
            ds = [fe[f] for f in range(lo, hi + 1) if f in fe]
            if ds:
                cand = (abs(frame - (lo + hi) / 2), p, float(np.median(ds)))
                if best is None or cand[0] < best[0]:
                    best = cand
    if best is None:
        return None
    _, p, dmed = best
    return {'window_end': p['frame'], 'view_submit_p50_us': p['view_submit_p50_us'], 'draws_median': dmed,
            'us_per_draw': p['view_submit_p50_us'] / dmed}

def main(js, run_dir, prefix, tols):
    R = json.load(open(js))
    dock_models = set()
    for F in R['frames'].values():
        for c in F['census']:
            if c['verdict'] == 'culled_dock':
                dock_models.add(c['model'])
    out_rows = []; summary = []
    for fr_s, F in sorted(R['frames'].items(), key=lambda x: int(x[0])):
        fr = int(fr_s)
        rb = R['depth'][fr_s]
        W, H = int(rb['width']), int(rb['height'])
        d = np.fromfile(Path(run_dir) / rb['file'], dtype='<f4').reshape(H, W, 4)
        z, w = d[..., 0], d[..., 2]
        valid = z != -1
        zs, ws = z[valid].astype(np.float64), w[valid].astype(np.float64)
        # z = A - B * (1/w): least squares
        X = np.stack([np.ones_like(ws), -1.0 / ws], 1)
        (A, B), *_ = np.linalg.lstsq(X, zs, rcond=None)
        fit_err = float(np.max(np.abs(X @ np.array([A, B]) - zs)))
        wv = np.where(valid, w, np.inf)
        cen = {}
        for c in F['census']:
            cen.setdefault(c['node'], c)
        cost = per_draw_cost(R, fr)
        props_culled = len(F['props_culled'])
        census_cull = Counter(c['verdict'] for c in F['census'])
        stats = {tol: defaultdict(Counter) for tol in tols}
        cls_total = Counter(); cls_box = Counter()
        for idx_s, dr in F['draws'].items():
            e = cen.get(dr.get('node'))
            body = e['body'] if e else None
            cls = classify(body, F['engine'].get(idx_s), dr.get('model'), dock_models)
            cls_total[cls] += 1
            bx = F['bounds'].get(idx_s)
            verts = dr.get('num_vertices') or 0
            row = {'run': Path(run_dir).name, 'frame': fr, 'index': int(idx_s), 'class': cls, 'body': body,
                   'node': dr.get('node'), 'prims': dr['prims'], 'verts': verts,
                   'blend': dr['states'].get('27'), 'zwrite': dr['states'].get('14'), 'atest': dr['states'].get('15'),
                   's': e['s'] if e else None, 'box': bx is not None}
            if bx is None:
                out_rows.append(row); continue
            cls_box[cls] += 1
            off = 'offscreen' in bx; near = 'near' in bx
            row.update(offscreen=off, near=near, alpha_tested='alpha_tested' in bx, stale='stale' in bx, zmin=float(bx['zmin']))
            if off or near:
                row['frac'] = {str(t): 0.0 for t in tols}; out_rows.append(row)
                for tol in tols:
                    stats[tol][cls]['offscreen' if off else 'near'] += 1
                continue
            x0 = max(0, int(float(bx['sx0']))); y0 = max(0, int(float(bx['sy0'])))
            x1 = min(W, max(x0 + 1, int(float(bx['sx1'])) + 1)); y1 = min(H, max(y0 + 1, int(float(bx['sy1'])) + 1))
            zmin = float(bx['zmin'])
            wmin = B / (A - zmin) if A - zmin > 0 else np.inf
            patch = wv[y0:y1, x0:x1]
            row['area_px'] = int(patch.size); row['wmin'] = float(wmin)
            row['frac'] = {}
            for tol in tols:
                frac = float(np.mean(patch < wmin * (1.0 - tol))) if patch.size else 0.0
                row['frac'][str(tol)] = frac
                s = stats[tol][cls]
                s['tested'] += 1
                if frac >= 1.0:
                    s['full'] += 1; s['full_prims'] += dr['prims']; s['full_verts'] += verts
                if frac > 0.95:
                    s['mostly'] += 1; s['mostly_prims'] += dr['prims']; s['mostly_verts'] += verts
            out_rows.append(row)
        summary.append({'run': Path(run_dir).name, 'frame': fr, 'A': A, 'B': B, 'fit_max_err': fit_err,
                        'covered_px_frac': float(valid.mean()), 'draws': len(F['draws']), 'boxes': len(F['bounds']),
                        'class_draws': dict(cls_total), 'class_boxes': dict(cls_box), 'cost': cost,
                        'already_culled': {'engine_small': int((F['parts'] or {}).get('culled', 0)),
                                           'engine_dock': int((F['parts'] or {}).get('dock_culled', 0)),
                                           'proxy_small_props': props_culled, 'census_verdicts': dict(census_cull)},
                        'stats': {str(t): {c: dict(v) for c, v in stats[t].items()} for t in tols}})
    json.dump(summary, open(prefix + '_summary.json', 'w'), indent=1)
    with open(prefix + '_draws.jsonl', 'w') as f:
        for r in out_rows:
            f.write(json.dumps(r) + '\n')
    print('wrote', prefix + '_summary.json', len(summary), 'frames', len(out_rows), 'draw rows')

if __name__ == '__main__':
    tols = [float(t) for t in sys.argv[4:]] or [0.01]
    main(sys.argv[1], sys.argv[2], sys.argv[3], tols)
