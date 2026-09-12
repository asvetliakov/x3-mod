#!/usr/bin/env python3
"""Reference port of src/proxy/mesh_adjacency_fast.cpp (exact-equality D3DX adjacency).

Pure Python, float32 arithmetic emulated per operation, so the host driver of the
C++ module can be cross-checked on random meshes (verification/analysis/
test_mesh_adjacency_fast.py). The algorithm and its D3DX-equivalence argument are
documented in docs/verification/mesh-adjacency-fast.md.
"""
import math
import struct

UNUSED = 0xFFFFFFFF


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


def bits_to_float(bits):
    return struct.unpack('<f', struct.pack('<I', bits))[0]


def float_to_bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def normalize_zero(bits):
    return 0 if bits == 0x80000000 else bits


def _sub(a, b):
    return tuple(f32(a[i] - b[i]) for i in range(3))


def _cross(a, b):
    return (f32(f32(a[1] * b[2]) - f32(a[2] * b[1])),
            f32(f32(a[2] * b[0]) - f32(a[0] * b[2])),
            f32(f32(a[0] * b[1]) - f32(a[1] * b[0])))


def _dot(a, b):
    return f32(f32(f32(a[0] * b[0]) + f32(a[1] * b[1])) + f32(a[2] * b[2]))


def _normalize(v):
    length = f32(math.sqrt(_dot(v, v)))
    if length > 0.0:
        return tuple(f32(c / length) for c in v)
    return (0.0, 0.0, 0.0)


def _face_normal(p, v1, v2, v3):
    return _normalize(_cross(_sub(p[v1], p[v2]), _sub(p[v1], p[v3])))


def generate(position_bits, faces, epsilon, head_insertion=True, normal_selection=True,
             skip_raw_degenerate=True, skip_rep_degenerate=False, drop_welded_corners=True, single_adjacency=True):
    """position_bits: list of (xbits, ybits, zbits); faces: list of (i0, i1, i2).

    Returns (status, report, adjacency); adjacency is None unless status == 'ok'.
    """
    report = dict(representatives=0, welded=0, quantized=False, degenerate_faces=0, welded_degenerate_faces=0, dropped_edges=0,
                  multi_candidates=0, normal_selected=0, repeated_neighbours=0, unmatched=0)
    V, F = len(position_bits), len(faces)
    if not V or not F or not (epsilon >= 0.0) or math.isinf(epsilon):
        return 'input', report, None
    if epsilon > 0.0:
        sq = f32(epsilon * epsilon)
        if sq == 0.0 or sq < 1.1754943508222875e-38:
            return 'input', report, None
    keys, positions = [], []
    for k in position_bits:
        for c in k:
            if (c & 0x7F800000) == 0x7F800000:
                return 'non_finite', report, None
        key = tuple(normalize_zero(c) for c in k)
        keys.append(key)
        positions.append(tuple(bits_to_float(c) for c in key))
    first = {}
    rep = []
    for v, key in enumerate(keys):
        rep.append(first.setdefault(key, v))
    reps = sorted(set(rep))
    report['representatives'] = len(reps)
    report['welded'] = V - len(reps)
    if epsilon > 0.0:
        _, exponent = math.frexp(2.0 * epsilon)
        grid_inverse = math.ldexp(1.0, -exponent)
        quantized = all(float(c) * grid_inverse == math.floor(float(c) * grid_inverse) for r in reps for c in positions[r])
        report['quantized'] = quantized
        if not quantized:
            cell = 4.0 * epsilon
            threshold = 4.0 * epsilon * epsilon
            cells = {}
            for r in reps:
                coords = tuple(math.floor(positions[r][i] / cell) for i in range(3))
                if any(abs(c) >= 1073741824.0 for c in coords):
                    return 'magnitude', report, None
                cells.setdefault(tuple(int(c) for c in coords), []).append(r)
            for coords, members in cells.items():
                for r in members:
                    for dx in (-1, 0, 1):
                        for dy in (-1, 0, 1):
                            for dz in (-1, 0, 1):
                                for o in cells.get((coords[0] + dx, coords[1] + dy, coords[2] + dz), ()):
                                    if o == r:
                                        continue
                                    d = sum((positions[o][i] - positions[r][i]) ** 2 for i in range(3))
                                    if d <= threshold:
                                        return 'epsilon_neighbour', report, None
    corners = []
    for face in faces:
        for index in face:
            if index >= V:
                return 'index_range', report, None
            corners.append(rep[index])
    active, dropped = [], [False] * (F * 3)
    for f, face in enumerate(faces):
        raw_degenerate = len(set(face)) < 3
        rep_degenerate = not raw_degenerate and len({corners[f * 3], corners[f * 3 + 1], corners[f * 3 + 2]}) < 3
        report['degenerate_faces'] += raw_degenerate
        report['welded_degenerate_faces'] += rep_degenerate
        active.append(not (skip_raw_degenerate and raw_degenerate) and not (skip_rep_degenerate and rep_degenerate))
    edges = []  # v1, v2, other, face, point
    buckets = {}
    for f in range(F):
        c, raw = corners[f * 3:f * 3 + 3], faces[f]
        valid = [not drop_welded_corners or all(not (j != k and c[j] == c[k] and raw[j] < raw[k]) for j in range(3)) for k in range(3)]
        for k in range(3):
            va, vb, other = c[k], c[(k + 1) % 3], c[(k + 2) % 3]
            edges.append((va, vb, other, f, k))
            if not active[f]:
                continue
            if not (valid[k] and valid[(k + 1) % 3]):
                dropped[f * 3 + k] = True
                report['dropped_edges'] += 1
                continue
            bucket = buckets.setdefault(va, [])
            if head_insertion:
                bucket.insert(0, f * 3 + k)
            else:
                bucket.append(f * 3 + k)
    adjacency = [UNUSED] * (F * 3)
    for f in range(F):
        for k in range(3):
            if adjacency[f * 3 + k] != UNUSED:
                continue
            if not active[f] or dropped[f * 3 + k]:
                report['unmatched'] += 1
                continue
            vb, va, other = corners[f * 3 + k], corners[f * 3 + (k + 1) % 3], corners[f * 3 + (k + 2) % 3]
            if va == vb:
                report['unmatched'] += 1
                continue
            found, candidates, best, own = None, 0, -2.0, None
            for cur in buckets.get(va, []):
                e = edges[cur]
                if e[0] != va or e[1] != vb:
                    continue
                candidates += 1
                if found is None:
                    found = cur
                    continue
                if not normal_selection:
                    continue
                if own is None:
                    own = _face_normal(positions, vb, va, other)
                    fe = edges[found]
                    best = _dot(_face_normal(positions, fe[0], fe[1], fe[2]), own)
                diff = _dot(_face_normal(positions, e[0], e[1], e[2]), own)
                if diff > best:
                    best, found = diff, cur
                    report['normal_selected'] += 1
            if candidates > 1:
                report['multi_candidates'] += 1
            if (f * 3 + k) in buckets.get(vb, []):
                buckets[vb].remove(f * 3 + k)
            if found is None:
                report['unmatched'] += 1
                continue
            g = edges[found][3]
            if single_adjacency and g in adjacency[f * 3:f * 3 + 3]:
                report['repeated_neighbours'] += 1
                report['unmatched'] += 1
                continue
            buckets[va].remove(found)
            adjacency[f * 3 + k] = g
            adjacency[g * 3 + edges[found][4]] = f
    return 'ok', report, adjacency


def quantized_bits(k, scale=1.0 / 16384.0):
    """The game's int16 * 1/16384 position quantum (loading-profile-run1.md)."""
    return float_to_bits(f32(k * scale))
