#!/usr/bin/env python3
"""Reference port of src/proxy/mesh_adjacency_fast.cpp (exact-equality D3DX adjacency).

Pure Python, float32 arithmetic emulated per operation, so the host driver of the
C++ module can be cross-checked on random meshes (verification/analysis/
test_mesh_adjacency_fast.py). The rules are those of d3dx9_37's
ID3DXMesh::GenerateAdjacency as decompiled in
docs/reverse-engineering/d3dx-generate-adjacency.md; the module's equivalence
argument is in docs/verification/mesh-adjacency-fast.md. This port keeps the
literal D3DX sweep (window over the sorted keys) rather than the module's
equal-position classes, so the two are independent implementations of the rule.
"""
import math
import struct

UNUSED = 0xFFFFFFFF
NORMALIZE_THRESHOLD = 2.0 ** -46  # D3DXVec3Normalize (SSE table): shorter vectors become zero


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


def bits_to_float(bits):
    return struct.unpack('<f', struct.pack('<I', bits))[0]


def float_to_bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def normalize_zero(bits):
    return 0 if bits == 0x80000000 else bits


def heapsort_order(keys):
    """D3DX's vertex sort: an in-place binary min-heap sort over vertex indices keyed
    by the float at byte 0 of every vertex; the output is in descending key order and
    the order among equal keys is the heap's permutation, not the index order."""
    n = len(keys)
    a = list(range(n))
    if n <= 1:
        return a

    def sift(element, pos, child, size):
        while child < size:
            chosen = child
            if child + 1 < size and keys[a[child + 1]] <= keys[a[child]]:
                chosen = child + 1
            if keys[element] < keys[a[chosen]]:
                break
            a[pos] = a[chosen]
            pos = chosen
            child = chosen * 2 + 1
        a[pos] = element

    for i in range((n >> 1) - 1, -1, -1):
        sift(a[i], i, 2 * i + 1, n)
    for m in range(n - 1, -1, -1):
        element = a[m]
        a[m] = a[0]
        sift(element, 0, 1, m)
    return a


def rsqrt_portable(x):
    """Stands in for rsqrtss on the host: the correctly rounded float 1/sqrt(x).
    The x86 production build uses the instruction itself; both feed the same
    Newton step, so the results agree to about one ulp of the normal."""
    return f32(1.0 / math.sqrt(x))


def _normalize_sse2(v, rsqrt=rsqrt_portable):
    """D3DXVec3Normalize of the SSE2 table (FUN_00756732): single precision, vectors
    shorter than 2^-46 become zero, rsqrtss and one Newton step."""
    x, y, z = v
    length2 = f32(f32(f32(x * x) + f32(y * y)) + f32(z * z))
    if length2 < NORMALIZE_THRESHOLD:
        return (0.0, 0.0, 0.0)
    r = rsqrt(length2)
    r = f32(f32(f32(3.0 - f32(f32(r * length2) * r)) * r) * 0.5)
    return (f32(x * r), f32(y * r), f32(z * r))


def rsqrt_table():
    """The generic table's 512 linear segments of 1/sqrt over [0.5, 2) (d3dx9_37
    DAT_0076c2a0, reproduced from its generating rule: a secant through the
    float-rounded values at the segment ends, the intercept from the upper end)."""
    table = []
    for k in range(512):
        s = 1.0 if k >> 8 else 0.5
        lo, hi = s * (1.0 + (k & 255) / 256.0), s * (1.0 + ((k & 255) + 1) / 256.0)
        r0, r1 = f32(1.0 / math.sqrt(lo)), f32(1.0 / math.sqrt(hi))
        a = f32((r1 - r0) / (hi - lo))
        table.append((a, f32(r1 - a * hi)))
    return table


RSQRT_TABLE = rsqrt_table()


def _normalize_generic(v):
    """D3DXVec3Normalize of the generic table (FUN_005881fc; installed under FEX,
    where D3DX takes its 3DNow branch on IsProcessorFeaturePresent(7) but the
    CPUID bit is absent): x87 at 53-bit precision, so double operations on float
    inputs; a zero float length gives zero, |float(len2 - 1)| <= 1e-5 copies the
    vector unnormalized, otherwise r = (m * a + b) * 2^(-e/2) from the table."""
    x, y, z = v
    length2 = (x * x + y * y) + z * z
    b = float_to_bits(f32(length2))
    if b == 0:
        return (0.0, 0.0, 0.0)
    if (float_to_bits(f32(length2 - 1.0)) & 0x7FFFFFFF) <= 0x3727C5AC:
        return v
    a, c = RSQRT_TABLE[(b >> 15) & 0x1FF]
    m = bits_to_float((b & 0xFFFFFF) | 0x3F000000)
    s = bits_to_float(((0xBEFFFFFF - b) >> 1) & 0xFF800000)
    r = (m * a + c) * s
    return (f32(x * r), f32(y * r), f32(z * r))


def _normalize(v, rsqrt=rsqrt_portable, normalize='sse2'):
    return _normalize_generic(v) if normalize == 'generic' else _normalize_sse2(v, rsqrt)


def _face_normal(p, v1, v2, v3, rsqrt=rsqrt_portable, normalize='sse2'):
    """D3DX's face normal for the corner order (v1, v2, v3): cross(p1 - p2, p1 - p3),
    edge vectors rounded to float, the cross product formed in extended precision
    and rounded to float per component, then D3DXVec3Normalize."""
    e1 = tuple(f32(p[v1][i] - p[v2][i]) for i in range(3))
    e2 = tuple(f32(p[v1][i] - p[v3][i]) for i in range(3))
    n = (f32(e1[1] * e2[2] - e1[2] * e2[1]), f32(e1[2] * e2[0] - e1[0] * e2[2]), f32(e1[0] * e2[1] - e1[1] * e2[0]))
    return _normalize(n, rsqrt, normalize)


def _score(n, m):
    """The x87 dot product (z, x, y order) rounded to float before the comparison."""
    return f32((n[2] * m[2] + n[0] * m[0]) + n[1] * m[1])


def generate(position_bits, faces, epsilon, head_bits=None, head_insertion=True, normal_selection=True,
             weld_refusal=True, heap_order=True, retire_own_entry=False, unlink_refused=True, later_slot_check=False,
             rsqrt=rsqrt_portable, normalize='sse2'):
    """position_bits: list of (xbits, ybits, zbits); faces: list of (i0, i1, i2);
    head_bits: the three float bit patterns at byte 0 of every vertex, which D3DX
    reads for the sweep key (the first) and for the normals of the candidate
    selection (all three); the position itself when it is the first element,
    which is the default. normalize: 'sse2' or 'generic', the D3DXVec3Normalize
    D3DX dispatched in the process that produced the output being reproduced.

    Returns (status, report, adjacency); adjacency is None unless status == 'ok'.
    The keyword policies exist for the fixture and the tests: the defaults are
    d3dx9_37's rules, the alternatives are distinguishable and wrong.
    """
    report = dict(representatives=0, welded=0, quantized=False, degenerate_faces=0, welded_degenerate_faces=0, refused_welds=0,
                  multi_candidates=0, normal_selected=0, repeated_neighbours=0, unmatched=0)
    V, F = len(position_bits), len(faces)
    if not V or not F or not (epsilon >= 0.0) or math.isinf(epsilon):
        return 'input', report, None
    epsilon = f32(epsilon)  # D3DX receives a FLOAT
    if V < 3:
        return 'input', report, None  # D3DX sizes its edge table V/3
    if epsilon > 0.0:
        sq = f32(epsilon * epsilon)
        if sq == 0.0 or sq < 1.1754943508222875e-38:
            return 'input', report, None
    heads_bits = list(head_bits) if head_bits is not None else position_bits
    positions, heads = [], []
    for k, h in zip(position_bits, heads_bits):
        for c in (k[0], k[1], k[2], h[0], h[1], h[2]):
            if (c & 0x7F800000) == 0x7F800000:
                return 'non_finite', report, None
        positions.append(tuple(bits_to_float(c) for c in k))
        heads.append(tuple(bits_to_float(c) for c in h))
    keys = [h[0] for h in heads]
    exact = {}
    for v, k in enumerate(position_bits):
        exact.setdefault(tuple(normalize_zero(c) for c in k), []).append(v)
    if epsilon > 0.0:
        _, exponent = math.frexp(2.0 * epsilon)
        grid_inverse = math.ldexp(1.0, -exponent)
        quantized = all(float(c) * grid_inverse == math.floor(float(c) * grid_inverse) for members in exact.values() for c in positions[members[0]])
        report['quantized'] = quantized
        if not quantized:
            cell = 4.0 * epsilon
            threshold = 4.0 * epsilon * epsilon
            cells = {}
            for members in exact.values():
                r = members[0]
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
    for face in faces:
        for index in face:
            if index >= V:
                return 'index_range', report, None
    faces_of = [[] for _ in range(V)]  # D3DX's per-vertex corner chains, reduced to the face sets they expose
    for f, face in enumerate(faces):
        for index in face:
            faces_of[index].append(f)

    def shares_face(v, w):
        return weld_refusal and any(w in faces[f] for f in faces_of[v])

    rep = [-1] * V
    if epsilon == 0.0:
        chains = {}  # exact position -> representatives, most recent first (the hash bucket order among equal positions)
        for v in range(V):
            chain = chains.setdefault(positions[v], [])
            for r in chain:
                if not shares_face(v, r):
                    rep[v] = r
                    break
            else:
                chain.insert(0, v)
                rep[v] = v
    else:
        order = heapsort_order(keys) if heap_order else sorted(range(V), key=lambda v: -keys[v])
        eps2 = epsilon * epsilon
        j = 0
        for i in range(V):
            vi = order[i]
            while j < V and not (epsilon < keys[vi] - keys[order[j]]):
                j += 1
            if rep[vi] != -1:
                continue
            rep[vi] = vi
            for m in range(i + 1, j):
                w = order[m]
                if rep[w] != -1:
                    continue
                d = sum((positions[w][c] - positions[vi][c]) ** 2 for c in (2, 1, 0))
                if not d < eps2:
                    continue
                if shares_face(vi, w):
                    report['refused_welds'] += 1
                    continue
                rep[w] = vi
    report['representatives'] = sum(1 for v in range(V) if rep[v] == v)
    report['welded'] = V - report['representatives']
    corners = [rep[index] for face in faces for index in face]
    table = {}  # (v1, v2) -> chain of (face, corner) with the most recent insertion first
    active = []
    for f, face in enumerate(faces):
        c = corners[f * 3:f * 3 + 3]
        report['degenerate_faces'] += len(set(face)) < 3
        degenerate = len(set(c)) < 3
        report['welded_degenerate_faces'] += degenerate and len(set(face)) == 3
        active.append(not degenerate)
        if degenerate:
            continue
        for k in range(3):
            chain = table.setdefault((c[k], c[(k + 1) % 3]), [])
            if head_insertion:
                chain.insert(0, (f, k))
            else:
                chain.append((f, k))
    adjacency = [UNUSED] * (F * 3)
    for f in range(F):
        if not active[f]:
            continue
        c = corners[f * 3:f * 3 + 3]
        for s in range(3):
            if adjacency[f * 3 + s] != UNUSED:
                continue
            v2, v1, other = c[s], c[(s + 1) % 3], c[(s + 2) % 3]  # the reverse edge v1 -> v2 is looked up
            chain = table.get((v1, v2))
            if chain:
                best = 0
                if len(chain) > 1:
                    report['multi_candidates'] += 1
                    if normal_selection:
                        query = _face_normal(heads, v2, v1, other, rsqrt, normalize)
                        best_score = None
                        for i in range(1, len(chain)):
                            if best_score is None:
                                g, k = chain[best]
                                best_score = _score(_face_normal(heads, corners[g * 3 + k], corners[g * 3 + (k + 1) % 3], corners[g * 3 + (k + 2) % 3], rsqrt, normalize), query)
                            g, k = chain[i]
                            score = _score(_face_normal(heads, corners[g * 3 + k], corners[g * 3 + (k + 1) % 3], corners[g * 3 + (k + 2) % 3], rsqrt, normalize), query)
                            if best_score < score:
                                best, best_score = i, score
                                report['normal_selected'] += 1
                found_face, found_corner = chain[best]
                del chain[best]  # D3DX unlinks the selected entry inside the lookup
                own = table.get((v2, v1))
                if own and (f, s) in own:
                    own.remove((f, s))  # the querying edge is removed only after a successful lookup
                slots = adjacency[f * 3:f * 3 + (3 if later_slot_check else s)]
                if found_face in slots:
                    report['repeated_neighbours'] += 1
                    if not unlink_refused:
                        chain.insert(best, (found_face, found_corner))
                    continue
                adjacency[f * 3 + s] = found_face
                adjacency[found_face * 3 + found_corner] = f
            else:
                if retire_own_entry:
                    own = table.get((v2, v1))
                    if own and (f, s) in own:
                        own.remove((f, s))
    report['unmatched'] = adjacency.count(UNUSED)
    return 'ok', report, adjacency


def quantized_bits(k, scale=1.0 / 16384.0):
    """The game's int16 * 1/16384 position quantum (loading-profile-run1.md)."""
    return float_to_bits(f32(k * scale))
