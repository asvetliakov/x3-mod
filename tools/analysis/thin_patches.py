#!/usr/bin/env python3
"""Smooth-patch decomposition of a BOB1 LOD record and its sub-pixel classes (docs/architecture/lod-strut-widening.md
sections 1.2 and 3). Shared by the merged-LOD baker's widen_thin_patches (lod_overlay.py) and the census
verification/results/lod-strut-widening/thin_geometry_census.py.

A smooth patch is a connected set of faces joined across manifold edges (exactly two faces on the merged-position
edge) whose face normals fold by less than FOLD_DEG; an open edge, a non-manifold edge or a sharper fold ends it.
Per patch: the PCA of its distinct positions (mean, axes, extents) and its width min(second extent, area / first
extent) (the second term catches frames and rings, whose box is wide but whose material is a narrow band).
Per face: area and altitude h = 2 area / longest edge. Pixels per raw unit at size s and focal length F:
k = F s / (r_raw 640) (r_raw = max |position| of the record). At k a patch is thin when its width is under W
pixels; a thin patch is a bevel when its sharp boundary borders wide patches on BEVEL_SHARE (90 %) or more of its
length (a plate chamfer), a strut otherwise."""
import numpy as np

FOCAL = {'5120x1440': 1280.0, '1920x1080': 960.0}   # px at vertical FOV 58.72 deg (run315 fov row)
W_PX = 1.0
FOLD_DEG = 45.0
BEVEL_SHARE = 0.9


def focal_px(rows):
    """Focal length in pixels of a display `rows` high at the vertical FOV of FOCAL (1280 at 1440 rows)."""
    return rows * 1280.0 / 1440.0


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


def face_geometry(record, fold=FOLD_DEG):
    """positions, faces, materials, per-face area and altitude h (2 area / longest edge), shortest edge, and
    the smooth patch of every face: faces joined across edges whose fold is under `fold` degrees (an open edge
    or a sharper fold ends the patch). Each patch's width (second PCA extent) and length (first) in raw units;
    a face's 'width' is its patch's width, so a strut side (151 x 6,282) is thin and a plate cut into slivers
    is not. Patch width = min(second PCA extent, area / first extent): a picture-frame rim (5.5 wide round a
    1,372-wide pane) has a wide box but a 13-unit mean width. Every part's faces take part (hidden ones too);
    'where' gives each face's (part, group, face) index, 'pid' each point's merged position, 'Fp' the faces
    over merged positions, 'edge_id' (n, 3) the merged edge (Fp[:, j], Fp[:, j + 1]) of each face and
    'edge_internal' per edge (a smooth manifold edge, inside one patch; every other face edge bounds its patch),
    and per patch 'pmean', 'paxes' (rows: PCA axes) and 'pext' (extents along them; zero for a patch with fewer
    than 3 distinct positions or a failed SVD, whose width is then 0)."""
    P = np.array([p[1:4] for p in record['points']], float)
    F, M, where = [], [], []
    for pi, part in enumerate(record['parts']):
        for gi, g in enumerate(part['groups']):
            for fi, f in enumerate(g['faces']):
                F.append(f[:3]); M.append(g['material']); where.append((pi, gi, fi))
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
    smooth = np.degrees(np.arccos(np.clip(cosang, 0, 1))) < fold
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
    # per-patch extents: SVD for every patch (exact width = second extent)
    width = np.zeros(n_patch); length = np.zeros(n_patch)
    pmean = np.zeros((n_patch, 3)); paxes = np.zeros((n_patch, 3, 3)); pext = np.zeros((n_patch, 3))
    npts = np.zeros(n_patch, int)
    pf = np.argsort(pinv, kind='stable')
    bounds = np.concatenate([[0], np.cumsum(np.bincount(pinv, minlength=n_patch))])
    for i in range(n_patch):
        fs = pf[bounds[i]:bounds[i + 1]]
        Q = P[np.unique(F[fs].reshape(-1))]
        npts[i] = len(Q)
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
        pmean[i] = Q.mean(0)
        paxes[i, :len(vt)] = vt
        pext[i, :len(ext)] = ext
    internal = np.zeros(len(uniq), bool)                  # smooth manifold edges: inside one patch
    internal[two[smooth]] = True
    return dict(P=P, F=F, M=M, where=np.array(where, int).reshape(-1, 3), pid=pid, Fp=Fp, area=area2 / 2,
                edge_id=inv, edge_internal=internal,
                normal=nrm, edges=e, elen=el, h=h, emin=emin, patch=pinv, n_patch=n_patch, pwidth=width,
                plength=length, parea=parea, pmean=pmean, paxes=paxes, pext=pext, npts=npts, fwidth=width[pinv],
                sharp_edges=sharp_edges, r_raw=float(np.linalg.norm(P, axis=1).max()))


def patch_classes(geo, k, w=W_PX, bevel=BEVEL_SHARE):
    """(thin, bevel, strut) boolean arrays over the patches at k px per unit: thin = width under w px; bevel =
    a thin patch whose sharp boundary (length) borders wide patches on >= bevel of it (a plate chamfer); a strut
    side borders other thin patches (box) or one wide surface on one side only (a rib); strut = thin, not bevel."""
    pth = geo['pwidth'] * k < w
    ea, eb, elen = geo['sharp_edges']
    n = geo['n_patch']
    total = np.bincount(ea, weights=elen, minlength=n) + np.bincount(eb, weights=elen, minlength=n)
    wide_b = (np.bincount(ea, weights=elen * (~pth[eb]), minlength=n)
              + np.bincount(eb, weights=elen * (~pth[ea]), minlength=n))
    bevel_p = pth & (total > 0) & (wide_b >= bevel * total)
    return pth, bevel_p, pth & ~bevel_p


def classify(geo, k, w=W_PX):
    """Face and area counts of the classes at k px per unit (census rows): thin faces (own altitude under w px),
    feature faces (patch under w px), short edges, and the strut / bevel split of patch_classes."""
    thin = geo['h'] * k < w                                   # the face itself is a sub-pixel sliver
    feat = geo['fwidth'] * k < w                              # its smooth patch is under a pixel across
    short = geo['emin'] * k < w
    a = geo['area']; tot = a.sum() or 1.0
    pth, bevel_p, strut_p = patch_classes(geo, k, w)
    bevel = bevel_p[geo['patch']]
    strut = strut_p[geo['patch']]
    return dict(thin=int(thin.sum()), feat=int(feat.sum()), short=int(short.sum()),
                thin_area=float(a[thin].sum() / tot), feat_area=float(a[feat].sum() / tot),
                bevel_area=float(a[bevel].sum() / tot), strut_area=float(a[strut].sum() / tot),
                strut_faces=int(strut.sum()), bevel_faces=int(bevel.sum()),
                patches=int(geo['n_patch']), thin_patches=int(pth.sum()), bevel_patches=int(bevel_p.sum()),
                feat_h_p50=float(np.median(geo['fwidth'][feat] * k)) if feat.any() else None)
