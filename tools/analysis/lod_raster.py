#!/usr/bin/env python3
"""Orthographic point-sample rasteriser for BOB1 records (verification of the merged-LOD strut widening,
docs/architecture/lod-strut-widening.md section 5). Not called by the baker: used by
verification/analysis/test_lod_strut_widening.py and verification/results/lod-strut-widening/raster_compare.py.

A view looks down one body axis from its + side; screen u, v are the other two axes in cyclic order, depth is
-coordinate (smaller is nearer, LESSEQUAL). k pixels per raw unit; a pixel's sample sits at its centre shifted by
the jitter (pixels). Faces draw in the record's part / group / face order: every opaque fragment first (per sample
the nearest wins, ties to the earlier face), then the blended fragments that pass the opaque depth, source-over
(a S + (1 - a) C) in draw order with depth writes (a later blended fragment behind an earlier one fails), as the
widened group (blend on, z-write on) draws after its part's atlas groups. The cross-part order of the engine (a
widened group of part 0 before part 1's opaque groups) is not modelled. Back faces (vertex-normal mean facing
away from the viewer) are culled unless the face's material has g_CullMode 1 (NONE)."""
import numpy as np

import lod_atlas



def halton(i, base):
    f, r = 1.0, 0.0
    while i > 0:
        f /= base
        r += f * (i % base)
        i //= base
    return r


JITTER8 = tuple((halton(i + 1, 2) - 0.5, halton(i + 1, 3) - 0.5) for i in range(8))   # 8 Halton(2, 3) phases, pixels
CHUNK = 4_000_000             # candidate samples per vectorised batch


def _normal_offset(flags):
    o = 4 if flags & 1 else 1
    if flags & 2:
        o += 4 if flags & 4 else 2
    return o if flags & 8 else None


def record_faces(record, mats=None, parts=None):
    """Arrays over the drawn faces of `record` in draw order (hidden parts skipped): positions (n, 3, 3), vertex
    normal mean (n, 3; zero without normals), material, (part, group, face) index, cull flag (False for a material
    with g_CullMode 1), UV centroid (n, 2; NaN without UVs)."""
    P, N, M, W, C, UV = [], [], [], [], [], []
    pts = record['points']
    cull_of = {}
    for pi, part in enumerate(record['parts']):
        if part['flags'] & lod_atlas.HIDDEN_PART or (parts is not None and pi not in parts):
            continue
        for gi, g in enumerate(part['groups']):
            m = g['material']
            if m not in cull_of:
                cm = None
                if mats is not None and 0 <= m < len(mats):
                    cm = next((v[0] for n, t, v in mats[m].get('params', ()) if n.lower() == b'g_cullmode' and t in (0, 1)), None)
                cull_of[m] = cm != 1
            for fi, f in enumerate(g['faces']):
                q = [pts[i] for i in f[:3]]
                P.append([p[1:4] for p in q])
                o = _normal_offset(q[0][0])
                N.append(np.mean([p[o:o + 3] for p in q], 0) if o is not None and all(_normal_offset(p[0]) == o for p in q)
                         else (0.0, 0.0, 0.0))
                uv = [lod_atlas.point_uv(p) for p in q]
                UV.append(np.mean(uv, 0) if all(x is not None for x in uv) else (np.nan, np.nan))
                M.append(m); W.append((pi, gi, fi)); C.append(cull_of[m])
    return dict(P=np.array(P, float).reshape(-1, 3, 3), N=np.array(N, float).reshape(-1, 3), M=np.array(M, int),
                where=np.array(W, int).reshape(-1, 3), cull=np.array(C, bool), uv=np.array(UV, float).reshape(-1, 2))


def view(faces, axis):
    """(tri2d (n, 3, 2), depth (n, 3), front mask) of the faces seen down `axis` from its + side."""
    u, v = (axis + 1) % 3, (axis + 2) % 3
    P = faces['P']
    front = ~faces['cull'] | (faces['N'][:, axis] > 0)
    return P[:, :, [u, v]], -P[:, :, axis], front


def render(tri, depth, colour, alpha, blended, k, origin, size, jitter=(0.0, 0.0), background=0.0, sub=1,
           blend_ztest=True, blend_zwrite=True):
    """Point-sample `tri` (n, 3, 2 raw units) at k px per unit into a size (w, h) pixel image whose pixel (0, 0)
    starts at `origin` (raw units), sub x sub samples per pixel (pixel centres shifted by jitter at sub 1);
    returns dict(image (h, w) mean colour, cover (h, w) share of samples with any fragment, owner (h*sub, w*sub)
    face index of the nearest fragment or -1). blend_ztest / blend_zwrite False (diagnostics): the blended
    fragments ignore the depth buffer / do not write it."""
    w, h = size
    ks = k * sub
    W, H = w * sub, h * sub
    xy = (tri - np.asarray(origin, float)) * ks - np.asarray(jitter, float) * sub   # sample (i, j) sits at i + 0.5
    n = len(tri)
    lo = np.ceil(xy.min(1) - 0.5).astype(np.int64)
    hi = np.floor(xy.max(1) - 0.5).astype(np.int64)
    lo = np.maximum(lo, 0); hi[:, 0] = np.minimum(hi[:, 0], W - 1); hi[:, 1] = np.minimum(hi[:, 1], H - 1)
    bw, bh = np.maximum(hi[:, 0] - lo[:, 0] + 1, 0), np.maximum(hi[:, 1] - lo[:, 1] + 1, 0)
    cnt = bw * bh
    frag_s, frag_f, frag_d = [], [], []
    order = np.nonzero(cnt)[0]
    start = 0
    while start < len(order):
        c = np.cumsum(cnt[order[start:]])
        end = start + max(1, int(np.searchsorted(c, CHUNK, side='right')))
        ids = order[start:end]
        start = end
        m = cnt[ids]
        f = np.repeat(ids, m)
        off = np.arange(m.sum()) - np.repeat(np.cumsum(m) - m, m)
        px = lo[f, 0] + off % bw[f]
        py = lo[f, 1] + off // bw[f]
        sx, sy = px + 0.5, py + 0.5
        a, b, cc = xy[f, 0], xy[f, 1], xy[f, 2]
        e0 = (b[:, 0] - a[:, 0]) * (sy - a[:, 1]) - (b[:, 1] - a[:, 1]) * (sx - a[:, 0])
        e1 = (cc[:, 0] - b[:, 0]) * (sy - b[:, 1]) - (cc[:, 1] - b[:, 1]) * (sx - b[:, 0])
        e2 = (a[:, 0] - cc[:, 0]) * (sy - cc[:, 1]) - (a[:, 1] - cc[:, 1]) * (sx - cc[:, 0])
        area = e0 + e1 + e2
        inside = (((e0 >= 0) & (e1 >= 0) & (e2 >= 0)) | ((e0 <= 0) & (e1 <= 0) & (e2 <= 0))) & (area != 0)
        f, px, py = f[inside], px[inside], py[inside]
        e0, e1, e2, area = e0[inside], e1[inside], e2[inside], area[inside]
        d = (e1 * depth[f, 0] + e2 * depth[f, 1] + e0 * depth[f, 2]) / area
        frag_s.append(py * W + px); frag_f.append(f); frag_d.append(d)
    S = np.concatenate(frag_s) if frag_s else np.zeros(0, np.int64)
    Fi = np.concatenate(frag_f) if frag_f else np.zeros(0, np.int64)
    D = np.concatenate(frag_d) if frag_d else np.zeros(0)
    img = np.full(W * H, float(background))
    zbuf = np.full(W * H, np.inf)
    owner = np.full(W * H, -1, np.int64)
    op = ~blended[Fi]
    if op.any():
        s_, f_, d_ = S[op], Fi[op], D[op]
        o = np.lexsort((f_, d_, s_))                      # per sample: nearest, then earliest face
        s_, f_, d_ = s_[o], f_[o], d_[o]
        first = np.ones(len(s_), bool); first[1:] = s_[1:] != s_[:-1]
        img[s_[first]] = colour[f_[first]]; zbuf[s_[first]] = d_[first]; owner[s_[first]] = f_[first]
    bl = ~op
    if bl.any():
        s_, f_, d_ = S[bl], Fi[bl], D[bl]
        keep = d_ <= zbuf[s_] + 1e-9 if blend_ztest else np.ones(len(s_), bool)
        s_, f_, d_ = s_[keep], f_[keep], d_[keep]
        o = np.lexsort((f_, s_))                          # per sample in draw order
        s_, f_, d_ = s_[o], f_[o], d_[o]
        rank = np.arange(len(s_)) - np.searchsorted(s_, s_, side='left')
        for r in range(int(rank.max()) + 1 if len(rank) else 0):
            sel = rank == r
            ss, ff, dd = s_[sel], f_[sel], d_[sel]
            ok = dd <= zbuf[ss] + 1e-9 if blend_ztest and blend_zwrite else np.ones(len(ss), bool)
            ss, ff, dd = ss[ok], ff[ok], dd[ok]
            a_ = alpha[ff]
            img[ss] = a_ * colour[ff] + (1 - a_) * img[ss]
            if blend_zwrite:
                zbuf[ss] = dd
            owner[ss] = ff
    img = img.reshape(H, W); cov = (owner >= 0).reshape(H, W)
    if sub > 1:
        img = img.reshape(h, sub, w, sub).mean((1, 3))
        cov = cov.reshape(h, sub, w, sub).mean((1, 3))
    return dict(image=img, cover=cov.astype(float), owner=owner.reshape(H, W))


def frame(tri, k, margin=2):
    """(origin, size) of a pixel frame around the projected triangles, the origin on the pixel lattice."""
    lo = np.floor(tri.reshape(-1, 2).min(0) * k) - margin
    hi = np.ceil(tri.reshape(-1, 2).max(0) * k) + margin
    return lo / k, (int(hi[0] - lo[0]), int(hi[1] - lo[1]))
