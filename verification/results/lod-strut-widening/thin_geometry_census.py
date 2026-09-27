#!/usr/bin/env python3
"""Sub-pixel geometry census of the overlay bodies' coarse-record source (record 0 of the winning member,
which the compact placement keeps byte-identical to vanilla record 0) at the record's switch size s = T_pad
(the manifest's pad_threshold: C draws for s < T_pad, so T_pad is C's largest on-screen size).

Per face: altitude h = 2 x area / longest edge (the face's thin extent, raw body units), shortest edge e.
Pixels per raw unit at size s and focal length F: k = F x s / (r_raw x 640) (lattice-baker-fix.md pixels.py;
r_raw = max |position| of record 0; s = r x 640 / D so the bounding radius projects to F x s / 640 px).
F = 1280 px at 1440 rows and 960 at 1080 rows (vertical FOV 58.72 deg, run315 fov row).

Classes at s = T_pad:
  thin face      h x k < W (W = 1 px): the face itself is under a pixel across.
  thin feature   thin face whose longest edge is open (no neighbour) or folds by more than 45 deg
                 (a box strut side, a fin, a card edge); a thin face inside a smooth surface (a sliver of
                 a plate tessellation, dihedral small) is not a visible feature.
  short edge     e x k < W.
Output: one row per body (faces, thin share, feature share by count and by area, short-edge share, r_raw,
scale, T_pad, engine radius, switch distance), a fleet summary, and --detail rows per material for named
bodies at extra sizes (--at NAME=s,s,...).

Usage: python3 thin_geometry_census.py [--jobs N] [--out FILE] [--only stem[,stem]] [--at stem=s,s]
       [--vanilla stem[,stem]]   (bodies read from the vanilla ladder, no manifest: record 0 and its ladder)
Host-side catalogue read through tools/analysis/bob1.py; no Wine, no game launch."""
import argparse, json, math, os, sys, time
from collections import defaultdict
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools' / 'analysis'))
import bob1                      # noqa: E402
import sector_fog_census as sfc  # noqa: E402

GAME = Path(os.path.expanduser('~/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'))
FOCAL = {'5120x1440': 1280.0, '1920x1080': 960.0}
W_PX = 1.0
FOLD_DEG = 45.0
UNITS_PER_M = 505.0


def manifest_bodies(game):
    out = {}
    for p in sorted((game / 'addon').glob('[0-9][0-9].x3m-lod.json')):
        d = json.load(open(p))
        for b in d['bodies']:
            out[b['name'].lower()] = dict(name=b['name'], t_pad=int(b['pad_threshold']), slot=p.stem[:2],
                                          source_record=int(b.get('source_record', 0)))
    return out


def components(n, u, v):
    """Connected-component labels (min label) over n nodes joined by edges (u, v), numpy label propagation
    with pointer jumping."""
    lab = np.arange(n)
    while True:
        lu, lv = lab[u], lab[v]
        lo, hi = np.minimum(lu, lv), np.maximum(lu, lv)
        new = lab.copy()
        np.minimum.at(new, hi, lo)
        while True:
            nn = new[new]
            if np.array_equal(nn, new):
                break
            new = nn
        if np.array_equal(new, lab):
            return lab
        lab = new


def face_geometry(record):
    """positions, faces, materials, per-face area and altitude h (2 area / longest edge), shortest edge, and
    the smooth patch of every face: faces joined across edges whose fold is under FOLD_DEG (an open edge or a
    sharper fold ends the patch). Each patch's width (second PCA extent) and length (first) in raw units;
    a face's 'width' is its patch's width, so a strut side (151 x 6,282) is thin and a plate cut into slivers
    is not. Patch width = min(second PCA extent, area / first extent): a picture-frame rim (5.5 wide round a
    1,372-wide pane) has a wide box but a 13-unit mean width."""
    P = np.array([p[1:4] for p in record['points']], float)
    F, M = [], []
    for part in record['parts']:
        for g in part['groups']:
            for f in g['faces']:
                F.append(f[:3]); M.append(g['material'])
    F = np.array(F, int).reshape(-1, 3); M = np.array(M, int)
    if len(F) == 0:
        return None
    _, pid = np.unique(P, axis=0, return_inverse=True)     # merge coincident positions
    pid = pid.reshape(-1)
    Fp = pid[F]
    A, B, C = P[F[:, 0]], P[F[:, 1]], P[F[:, 2]]
    e = np.stack([B - A, C - B, A - C], 1)
    el = np.linalg.norm(e, axis=2)
    n = np.cross(B - A, C - A)
    area2 = np.linalg.norm(n, axis=1)
    with np.errstate(divide='ignore', invalid='ignore'):
        nrm = n / np.where(area2[:, None] > 0, area2[:, None], 1)
    L = el.max(1)
    h = np.where(L > 0, area2 / np.where(L > 0, L, 1), 0)
    emin = el.min(1)
    ends = np.stack([Fp, np.roll(Fp, -1, axis=1)], 2)
    ends.sort(axis=2)
    key = ends[:, :, 0].astype(np.int64) * (int(pid.max()) + 1) + ends[:, :, 1]
    uniq, inv, cnt = np.unique(key.reshape(-1), return_inverse=True, return_counts=True)
    inv = inv.reshape(-1, 3)
    order = np.argsort(inv.reshape(-1), kind='stable')
    face_of = np.repeat(np.arange(len(F)), 3)[order]
    starts = np.concatenate([[0], np.cumsum(cnt)[:-1]])
    two = np.nonzero(cnt == 2)[0]                            # manifold edges: exactly two faces
    fa, fb = face_of[starts[two]], face_of[starts[two] + 1]
    cosang = np.abs(np.einsum('ij,ij->i', nrm[fa], nrm[fb]))
    smooth = np.degrees(np.arccos(np.clip(cosang, 0, 1))) < FOLD_DEG
    lab = components(len(F), fa[smooth], fb[smooth])
    plab, pinv = np.unique(lab, return_inverse=True)
    pinv = pinv.reshape(-1)
    n_patch = len(plab)
    # sharp manifold edges between two patches: (patch a, patch b, length) for the bevel test
    sharp = ~smooth
    ea, eb = pinv[fa[sharp]], pinv[fb[sharp]]
    # edge length: the edge index within face fa is the j with inv[fa, j] == edge id
    eid = two[sharp]
    j = np.argmax(inv[fa[sharp]] == eid[:, None], axis=1)
    elen = el[fa[sharp], j]
    keep = ea != eb
    sharp_edges = (ea[keep], eb[keep], elen[keep])
    parea = np.bincount(pinv, weights=area2 / 2, minlength=n_patch)
    # per-patch extents: bounding box first (cheap), SVD for every patch (exact width = second extent)
    width = np.zeros(n_patch); length = np.zeros(n_patch)
    pf = np.argsort(pinv, kind='stable')
    bounds = np.concatenate([[0], np.cumsum(np.bincount(pinv, minlength=n_patch))])
    for i in range(n_patch):
        fs = pf[bounds[i]:bounds[i + 1]]
        Q = P[np.unique(F[fs].reshape(-1))]
        if len(Q) < 3:
            continue
        Qc = Q - Q.mean(0)
        try:
            _, _, vt = np.linalg.svd(Qc, full_matrices=False)
        except np.linalg.LinAlgError:
            continue
        with np.errstate(all='ignore'):
            ext = Qc @ vt.T
        ext = ext.max(0) - ext.min(0)
        if not np.all(np.isfinite(ext)):
            continue
        # width: the second PCA extent, or the mean width area / length when the patch is a frame, ring or L
        # (its box is wide but the material is a narrow band); the smaller of the two
        length[i], width[i] = ext[0], min(ext[1], parea[i] / max(ext[0], 1e-9))
    return dict(P=P, F=F, M=M, area=area2 / 2, h=h, emin=emin, patch=pinv, n_patch=n_patch, pwidth=width,
                plength=length, parea=parea, fwidth=width[pinv], sharp_edges=sharp_edges,
                r_raw=float(np.linalg.norm(P, axis=1).max()))


def feature_components(geo, k, s):
    """Connected components (shared merged positions) of the thin-feature faces at size s: count, and the
    distribution of their across extent (second PCA extent, raw units and px) and length (first extent)."""
    feat = np.nonzero(geo['fwidth'] * k < W_PX)[0]
    if len(feat) == 0:
        return dict(s=s, n=0)
    _, pid = np.unique(geo['P'], axis=0, return_inverse=True)
    pid = pid.reshape(-1)
    parent = {}

    def find(a):
        while parent.setdefault(a, a) != a:
            parent[a] = parent[parent[a]]; a = parent[a]
        return a
    for f in geo['F'][feat]:
        a, b, c = (pid[i] for i in f)
        for x, y in ((a, b), (b, c)):
            ra, rb = find(x), find(y)
            if ra != rb:
                parent[rb] = ra
    comps = defaultdict(list)
    for fi in feat:
        comps[find(pid[geo['F'][fi][0]])].append(fi)
    widths, lengths, areas = [], [], []
    for fs in comps.values():
        idx = np.unique(geo['F'][fs].reshape(-1))
        Q = geo['P'][idx]
        if len(Q) < 3:
            continue
        _, sv, _ = np.linalg.svd(Q - Q.mean(0), full_matrices=False)
        e = (Q - Q.mean(0)) @ np.linalg.svd(Q - Q.mean(0), full_matrices=False)[2].T
        ext = e.max(0) - e.min(0)
        lengths.append(float(ext[0])); widths.append(float(ext[1])); areas.append(float(geo['area'][fs].sum()))
    w = np.array(widths); l = np.array(lengths)
    return dict(s=s, n=len(comps), faces=int(len(feat)), width_px_p10=float(np.percentile(w * k, 10)),
                width_px_p50=float(np.percentile(w * k, 50)), width_px_p90=float(np.percentile(w * k, 90)),
                width_units_p50=float(np.median(w)), length_px_p50=float(np.median(l * k)),
                area_share=float(sum(areas) / geo['area'].sum()))


def classify(geo, k, w=W_PX):
    thin = geo['h'] * k < w                                   # the face itself is a sub-pixel sliver
    feat = geo['fwidth'] * k < w                              # its smooth patch is under a pixel across
    short = geo['emin'] * k < w
    a = geo['area']; tot = a.sum() or 1.0
    pth = geo['pwidth'] * k < w
    # bevel: a thin patch whose sharp boundary (length) borders wide patches on >= 90 % of it (a plate chamfer);
    # a strut side borders other thin patches (box) or one wide surface on one side only (a rib)
    ea, eb, elen = geo['sharp_edges']
    n = geo['n_patch']
    total = np.bincount(ea, weights=elen, minlength=n) + np.bincount(eb, weights=elen, minlength=n)
    wide_b = np.bincount(ea, weights=elen * (~pth[eb]), minlength=n) + np.bincount(eb, weights=elen * (~pth[ea]), minlength=n)
    bevel_p = pth & (total > 0) & (wide_b >= 0.9 * total)
    strut_p = pth & ~bevel_p
    bevel = bevel_p[geo['patch']]
    strut = strut_p[geo['patch']]
    return dict(thin=int(thin.sum()), feat=int(feat.sum()), short=int(short.sum()),
                thin_area=float(a[thin].sum() / tot), feat_area=float(a[feat].sum() / tot),
                bevel_area=float(a[bevel].sum() / tot), strut_area=float(a[strut].sum() / tot),
                strut_faces=int(strut.sum()), bevel_faces=int(bevel.sum()),
                patches=int(geo['n_patch']), thin_patches=int(pth.sum()), bevel_patches=int(bevel_p.sum()),
                feat_h_p50=float(np.median(geo['fwidth'][feat] * k)) if feat.any() else None)


def one_body(args):
    name, t_pad, sizes, vanilla, detail = args
    t0 = time.time()
    assets = sfc.Assets(GAME)
    try:
        entry = bob1.resolve_body(assets, name)
        data = assets.read_entry(entry)
        tree = bob1.parse(data, 8)
    except Exception as exc:  # noqa: BLE001
        return dict(name=name, error=f'{type(exc).__name__}: {exc}')
    ladder = bob1.lods(tree)
    rec = ladder[0]
    geo = face_geometry(rec)
    if geo is None:
        return dict(name=name, error='no faces')
    scale = ladder[0]['value']
    r_raw = geo['r_raw']
    r_engine = r_raw * scale / 65536.0
    row = dict(name=name, faces=int(len(geo['F'])), points=len(rec['points']), r_raw=r_raw, scale=scale,
               r_engine=r_engine, t_pad=t_pad, thresholds=[l['value'] for l in ladder[1:]],
               switch_km=(r_engine * 640 / t_pad / UNITS_PER_M / 1000) if t_pad else None, seconds=0.0)
    for s in sizes:
        for disp, focal in FOCAL.items():
            k = focal * s / (r_raw * 640)
            row[f's{s}@{disp}'] = dict(k=k, **classify(geo, k))
    if detail:
        det = {}
        try:
            import lod_overlay
            alpha_set = set(lod_overlay.alpha_materials(bob1.materials(tree), assets, record=rec))
        except Exception as exc:  # noqa: BLE001
            alpha_set = set(); row['alpha_error'] = str(exc)
        row['alpha_materials'] = sorted(int(x) for x in alpha_set)
        alpha_faces = np.isin(geo['M'], list(alpha_set))
        row['alpha_split'] = {}
        for s_ in sizes:
            k = FOCAL['5120x1440'] * s_ / (r_raw * 640)
            feat = geo['fwidth'] * k < W_PX
            tot = geo['area'].sum()
            row['alpha_split'][s_] = dict(feat_area_alpha=float(geo['area'][feat & alpha_faces].sum() / tot),
                                          feat_area_opaque=float(geo['area'][feat & ~alpha_faces].sum() / tot),
                                          alpha_area=float(geo['area'][alpha_faces].sum() / tot))
        for m in np.unique(geo['M']):
            sel = geo['M'] == m
            g2 = {kk: (v[sel] if isinstance(v, np.ndarray) and v.shape[:1] == (len(geo['F']),) else v) for kk, v in geo.items()}
            d = dict(faces=int(sel.sum()), area_share=float(geo['area'][sel].sum() / geo['area'].sum()),
                     h_p10=float(np.percentile(geo['fwidth'][sel], 10)), h_p50=float(np.percentile(geo['fwidth'][sel], 50)))
            for s in sizes:
                k = FOCAL['5120x1440'] * s / (r_raw * 640)
                c = classify(g2, k)
                d[f's{s}'] = dict(thin=c['thin'], feat=c['feat'], feat_area=c['feat_area'], feat_h_px=c['feat_h_p50'])
            det[int(m)] = d
        row['materials'] = det
        row['components'] = feature_components(geo, FOCAL['5120x1440'] * (sizes[-1] if sizes else t_pad) / (r_raw * 640), sizes[-1] if sizes else t_pad)
    row['seconds'] = time.time() - t0
    return row


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--jobs', type=int, default=6)
    ap.add_argument('--out', type=Path)
    ap.add_argument('--only', default='')
    ap.add_argument('--vanilla', default='', help='stems read with their vanilla ladder (T_pad = record-1 threshold)')
    ap.add_argument('--at', action='append', default=[], help='stem=s,s extra sizes and per-material detail')
    a = ap.parse_args()
    man = manifest_bodies(GAME)
    extra = {}
    for spec in a.at:
        stem, ss = spec.split('=')
        extra[stem.lower()] = [int(x) for x in ss.split(',')]
    jobs = []
    names = [n.strip().lower() for n in a.only.split(',') if n.strip()] or sorted(man)
    for n in names:
        if n in man:
            jobs.append((man[n]['name'], man[n]['t_pad'], [man[n]['t_pad']] + extra.get(n, []), False, n in extra))
    for n in [x.strip() for x in a.vanilla.split(',') if x.strip()]:
        jobs.append((n, None, extra.get(n.lower(), []), True, True))
    t0 = time.time()
    rows = []
    with ProcessPoolExecutor(a.jobs) as ex:
        for r in ex.map(one_body, jobs, chunksize=4):
            rows.append(r)
            if len(rows) % 50 == 0:
                print(f'{len(rows)}/{len(jobs)} {time.time() - t0:.0f} s', file=sys.stderr, flush=True)
    ok = [r for r in rows if 'error' not in r]
    print(f'bodies {len(rows)} ok {len(ok)} errors {len(rows) - len(ok)} wall {time.time() - t0:.0f} s')
    for r in rows:
        if 'error' in r:
            print(f'ERROR {r["name"]}: {r["error"]}')
    key = lambda r, d: r.get(f's{r["t_pad"]}@{d}') if r.get('t_pad') else None
    for disp in FOCAL:
        rr = [(r, key(r, disp)) for r in ok if key(r, disp)]
        any_feat = [r for r, c in rr if c['feat'] > 0]
        pct1 = [r for r, c in rr if c['feat'] / r['faces'] > 0.01]
        area1 = [r for r, c in rr if c['feat_area'] > 0.01]
        area5 = [r for r, c in rr if c['feat_area'] > 0.05]
        tf = sum(c['feat'] for _, c in rr); tt = sum(c['thin'] for _, c in rr); ta = sum(r['faces'] for r, _ in rr)
        sa = sum(c['strut_faces'] for _, c in rr); ba = sum(c['bevel_faces'] for _, c in rr)
        s1 = [r for r, c in rr if c['strut_area'] > 0.01]; s5 = [r for r, c in rr if c['strut_area'] > 0.05]
        print(f'[{disp}] at s = T_pad: bodies with any thin-feature face {len(any_feat)}/{len(rr)}; >1% of faces {len(pct1)};'
              f' >1% of area {len(area1)}; >5% of area {len(area5)}; faces thin {tt} feature {tf} of {ta}'
              f' ({100 * tf / max(1, ta):.1f} %); strut faces {sa} bevel faces {ba}; bodies with strut area >1% {len(s1)} >5% {len(s5)}')
    print('\nper body (5120x1440 at s = T_pad): name faces T_pad r_engine switch_km thin% feat% feat_area% short% strut_area% bevel_area% | 1080: feat% feat_area%')
    for r in sorted(ok, key=lambda r: -(key(r, '5120x1440') or {'feat_area': 0})['feat_area']):
        c = key(r, '5120x1440'); d = key(r, '1920x1080')
        if not c:
            continue
        print(f'{r["name"]:62s} {r["faces"]:7d} {r["t_pad"]:4d} {r["r_engine"]:10.0f} {r["switch_km"]:6.1f}'
              f' {100 * c["thin"] / r["faces"]:5.1f} {100 * c["feat"] / r["faces"]:5.1f} {100 * c["feat_area"]:5.2f}'
              f' {100 * c["short"] / r["faces"]:5.1f} {100 * c["strut_area"]:5.2f} {100 * c["bevel_area"]:5.2f} | {100 * d["feat"] / r["faces"]:5.1f} {100 * d["feat_area"]:5.2f}')
    for r in ok:
        if 'materials' in r:
            print(f'\n== {r["name"]}: faces {r["faces"]} points {r["points"]} r_raw {r["r_raw"]:.0f} scale {r["scale"]}'
                  f' r_engine {r["r_engine"]:.0f} thresholds {r["thresholds"]} T_pad {r["t_pad"]}')
            for kk, v in r.items():
                if kk.startswith('s') and '@' in kk:
                    print(f'  {kk}: k {v["k"]:.5f} px/unit thin {v["thin"]} feat {v["feat"]} feat_area {100 * v["feat_area"]:.2f} %'
                          f' short {v["short"]} feat_w_px_p50 {v["feat_h_p50"]} patches {v["patches"]} thin_patches {v["thin_patches"]} bevel_patches {v["bevel_patches"]} strut_area {100 * v["strut_area"]:.2f} % bevel_area {100 * v["bevel_area"]:.2f} %')
            print(f'  feature components at s {r["components"]["s"]}: {r["components"]}')
            print(f'  alpha materials {r.get("alpha_materials")} split {r.get("alpha_split")}')
            for m, d in sorted(r['materials'].items(), key=lambda kv: -kv[1]['faces']):
                ss = ' '.join(f'{k}:thin {v["thin"]} feat {v["feat"]} area {100 * v["feat_area"]:.1f}% h {v["feat_h_px"] and round(v["feat_h_px"], 2)}'
                              for k, v in d.items() if k.startswith('s'))
                print(f'  mat {m:3d} faces {d["faces"]:7d} area {100 * d["area_share"]:5.1f} % w_p10 {d["h_p10"]:8.1f} w_p50 {d["h_p50"]:8.1f} | {ss}')
    if a.out:
        json.dump(rows, open(a.out, 'w'), default=float)


if __name__ == '__main__':
    main()
