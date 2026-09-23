#!/usr/bin/env python3
"""Reference of the engine's text-body geometry path (0x00483f20 + 0x00480830 + 0x00469c20,
docs/reverse-engineering/body-text-loader.md) and its comparison with bob1.parse_text over every
winning text body of a game root. Read-only; prints counts only.

  PYTHONPATH=tools/analysis python3 verification/results/bob1-format/text_loader_reference.py [GAME]

Engine rules modelled (addresses in the note):
  lexing     '/' starts a comment to the end of the line (0x004e98a0, 0x004eb930), so a
             /! N: !/, /! PART_VALUES_RAW: !/ or /! COLLISION_BOX: !/ block is ignored as long
             as it is on one line; values end at ';'.
  positions  per record: m = max |x|,|y|,|z| over the record's vertices; v' = trunc((v<<16)/m)
             (0x00484ff5 idiv); int16 = v' >> 2.
  uv         trunc(value * 65536) of the decimal parse (0x004ec490 -> 0x0052b5d0).
  points     smoothing 0: every face corner is a new point; smoothing s != 0: reuse the first
             earlier point of the record with the same s, the same normalised position and
             |du|,|dv|,|du2|,|dv2| < 2 (part flag 2 masks u,v to 16 bits first).
  normals    face normal = cross(b-a, c-a) with fixed products ((p*q+0x8000)>>16), normalised by
             0x00469c20; s == 0: the face normal on the three new points; s != 0: each corner adds
             fm(faceN, corner angle) with the angle in 1/65536 turns from the asin table
             0x00596990; every point normal is renormalised at the record end; int16 = n >> 2.
  faces      a face of k <= 4 vertices becomes the fan (v0, vi, vi+1); face word = flags & ~1.
"""
import collections
import math
import re
import struct
import sys
from pathlib import Path

import bob1
import sector_fog_census as sfc


def i32(v):
    v &= 0xffffffff
    return v - (1 << 32) if v & 0x80000000 else v


def fm(a, b):                      # imul; add 0x8000; adc; shrd 16 (low 32 bits)
    return i32((a * b + 0x8000) >> 16)


def cdiv(a, b):                    # idiv: truncation toward zero
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b > 0) else -q


def sqrtfix(v):                    # 0x00469a50, bit for bit (32-bit registers)
    v &= 0xffffffff
    root, bit, rem = 0, 0x40000000, v
    while bit:
        if rem >= bit and rem - bit >= root:
            rem = rem - bit - root
            root = (root >> 1) | bit
        else:
            root >>= 1
        bit >>= 2
    bit, root, rem = 0x4000, (root << 16) & 0xffffffff, (rem << 16) & 0xffffffff
    while bit:
        if rem >= bit and rem - bit >= root:
            rem = rem - bit - root
            root = (root >> 1) | bit
        else:
            root >>= 1
        bit >>= 2
    return root


def normalise(x, y, z):            # 0x00469c20
    if x == 0 and y == 0 and z == 0:
        return x, y, z
    while not (abs(x) < 0x600000 and abs(y) < 0x600000 and abs(z) < 0x600000):
        x, y, z = cdiv(x, 4), cdiv(y, 4), cdiv(z, 4)
    while abs(x) <= 0x17ffff and abs(y) <= 0x17ffff and abs(z) <= 0x17ffff:
        x, y, z = x * 4, y * 4, z * 4
    n = sqrtfix(fm(x, x) + fm(y, y) + fm(z, z))
    if n == 0:
        return 0, 0, 0
    return i32(cdiv(x << 16, n)), i32(cdiv(y << 16, n)), i32(cdiv(z << 16, n))


ASIN = [int(math.asin(i / 65536.0) / (4 * math.asin(1.0)) * 65536.0) for i in range(0x10001)]  # 0x004f0110


def f32(v):
    return struct.unpack('<f', struct.pack('<f', v))[0]


def corner_weight(p, q, r):        # 0x00480d65..0x00480f60: angle at p between p-q and p-r
    d1 = [p[k] - q[k] for k in range(3)]
    d2 = [p[k] - r[k] for k in range(3)]
    l1 = int(math.sqrt(f32(float(sum(c * c for c in d1)))))
    l2 = int(math.sqrt(f32(float(sum(c * c for c in d2)))))
    dot = i32(fm(d1[0], d2[0]) + fm(d1[1], d2[1]) + fm(d1[2], d2[2]))
    den = fm(l1, l2)
    c = 0 if den == 0 else i32(cdiv(dot << 16, den))
    if c < 0:
        return (ASIN[-c] if -c <= 0x10000 else 0x4000) + 0x4000
    return 0x4000 - ASIN[c] if c <= 0x10000 else 0


def fixed(tok):                    # 0x004ec490: decimal parse, * 65536.0, truncate
    return int(float(tok) * 65536.0)


class Lex:
    def __init__(self, text):
        code = re.sub(r'/[^\n]*', '', text)
        parts = code.split(';')
        self.toks = [t.strip() for t in parts[:-1]]
        self.i = 0

    def int(self):
        t = self.toks[self.i]; self.i += 1
        t = re.sub(r'\s+', '', t)
        return int(t[1:], 2) if t.startswith('%') else int(t, 0) if t.lower().startswith(('0x', '-0x')) else int(t)

    def bits(self):
        t = re.sub(r'\s+', '', self.toks[self.i]); self.i += 1
        return int(t, 2)

    def raw(self):
        t = self.toks[self.i]; self.i += 1
        return t

    def done(self):
        return self.i >= len(self.toks)


def engine_record(lx, stats):
    value = lx.int()
    verts = []
    while True:
        v = (lx.int(), lx.int(), lx.int())
        if v == (-1, -1, -1):
            break
        verts.append(v)
    m = max(max(abs(c) for c in v) for v in verts) if verts else 0
    stats['record_max_100000'] += m == 100000
    stats['records'] += 1
    norm = [tuple(cdiv(c << 16, m) for c in v) for v in verts]
    points, index, parts = [], collections.defaultdict(list), []
    first = lx.int()
    while first != -99:
        faces, mat = [], first
        while mat != -99:
            idx = []
            x = lx.int()
            while x >= 0:
                idx.append(x); x = lx.int()
            flags = -x
            if len(idx) > 4:
                raise ValueError('more than 4 face vertices (engine error)')
            stats[f'face_vertices_{len(idx)}'] += 1
            smooth, uv, uv2 = 0, [(0, 0)] * len(idx), None
            if len(idx) > 2:
                if flags & 0x10:
                    smooth = lx.int() & 0xffffffff
                if flags & 0x08:
                    uv = [(fixed(lx.raw()), fixed(lx.raw())) for _ in idx]
                if flags & 0x80:
                    uv2 = [(fixed(lx.raw()), fixed(lx.raw())) for _ in idx]
            faces.append((mat, idx, uv, uv2 or uv, flags & ~1, smooth))
            mat = lx.int()
        pflags = lx.bits()
        if pflags & 4:
            lx.int()                                      # pivot vertex index
        stats['part_flag_0x2'] += bool(pflags & 2)
        stats['part_flag_0x30000000'] += bool(pflags & 0x30000000)
        groups, order = {}, []
        for mat, idx, uv, uv2, word, smooth in faces:
            corner = []
            for k, vi in enumerate(idx):
                u, v = uv[k]
                if pflags & 2:
                    u, v = u & 0xffff, v & 0xffff
                u2, v2 = uv2[k]
                pos = norm[vi]
                j = None
                if smooth:
                    for c in index[(smooth, pos)]:
                        pu, pv, pu2, pv2 = points[c]['uv']
                        if abs(pu - u) < 2 and abs(pv - v) < 2 and abs(pu2 - u2) < 2 and abs(pv2 - v2) < 2:
                            j = c
                            break
                if j is None:
                    j = len(points)
                    points.append({'pos': pos, 'uv': (u, v, u2, v2), 'n': [0, 0, 0], 'smooth': smooth})
                    if smooth:
                        index[(smooth, pos)].append(j)
                corner.append(j)
            if mat not in groups:
                groups[mat] = []; order.append(mat)
            for t in range(2, len(corner)):
                tri = (corner[0], corner[t - 1], corner[t])
                groups[mat].append(tri + (word,))
                a, b, c = (points[i]['pos'] for i in tri)
                n = normalise(fm(b[1] - a[1], c[2] - a[2]) - fm(b[2] - a[2], c[1] - a[1]),
                              fm(b[2] - a[2], c[0] - a[0]) - fm(b[0] - a[0], c[2] - a[2]),
                              fm(b[0] - a[0], c[1] - a[1]) - fm(b[1] - a[1], c[0] - a[0]))
                if smooth == 0:
                    for i in tri:
                        points[i]['n'] = list(n)
                else:
                    for p, q, r in ((tri[0], tri[1], tri[2]), (tri[1], tri[2], tri[0]), (tri[2], tri[0], tri[1])):
                        w = corner_weight(points[p]['pos'], points[q]['pos'], points[r]['pos'])
                        points[p]['n'] = [i32(points[p]['n'][k] + fm(n[k], w)) for k in range(3)]
        parts.append({'flags': pflags, 'groups': [(m_, groups[m_]) for m_ in order]})
        first = lx.int()
    lodflags = lx.bits()
    for p in points:
        p['n16'] = tuple(c >> 2 for c in normalise(*p['n']))
        p['p16'] = tuple(c >> 2 for c in p['pos'])
    return {'value': value, 'flags': lodflags, 'points': points, 'parts': parts, 'max': m}


def compare(eng, tree, stats):
    for e, b in zip(eng, bob1.lods(tree)):
        stats['records_compared'] += 1
        stats['lod_value_diff'] += e['value'] != b['value']
        bp = b['points']
        if len(e['points']) != len(bp):
            stats['record_point_count_diff'] += 1
            stats['points_engine'] += len(e['points']); stats['points_bob1'] += len(bp)
            continue
        stats['points_engine'] += len(e['points']); stats['points_bob1'] += len(bp)
        pd = sum(1 for p, q in zip(e['points'], bp) if p['p16'] != tuple(c >> 2 for c in q[1:4]))
        stats['point_position_diff'] += pd
        stats['record_position_diff'] += pd > 0
        for p, q in zip(e['points'], bp):
            d = max(abs(x - (y >> 2)) for x, y in zip(p['n16'], q[-4:-1]))
            kind = ('flat' if p['smooth'] == 0 else 'smooth') + ('' if pd == 0 else '_posdiff')
            stats[f'normal_{kind}_' + ('d0' if d == 0 else 'd1' if d <= 1 else 'd2_16' if d <= 16
                                       else 'd17_1024' if d <= 1024 else 'd_gt1024')] += 1
        ud = sum(1 for p, q in zip(e['points'], bp) if q[0] & 2 and p['uv'][:2] != tuple(q[4:6]))
        stats['point_uv_diff'] += ud
        ef = [f for part in e['parts'] for _, g in part['groups'] for f in g]
        bf = [f for part in b['parts'] for g in part['groups'] for f in g['faces']]
        stats['face_index_diff'] += sum(1 for x, y in zip(ef, bf) if x[:3] != y[:3]) + abs(len(ef) - len(bf))
        for x in ef:
            stats[f'engine_face_word_{x[3]}'] += 1


def main():
    game = Path(sys.argv[1]) if len(sys.argv) > 1 else bob1.DEFAULT_GAME
    a = sfc.Assets(game)
    keys = sorted(k for k, v in a.entries.items() if v[-1]['path'].lower().endswith(('.pbd', '.bod')))
    S = collections.Counter()
    for k in keys:
        d = a.read_entry(a.entries[k][-1]); a.cache.clear()
        if bob1.text_kind(d) == 'scene':
            continue
        S['bodies'] += 1
        text = (bytes(d)[3:] if bytes(d).startswith(b'\xef\xbb\xbf') else bytes(d)).decode('latin1')
        blocks = re.findall(r'/!(.*?)!/', text, re.S)
        S['blocks'] += len(blocks)
        S['blocks_multiline'] += sum('\n' in b for b in blocks)
        S['lines_with_data_after_block'] += len(re.findall(r'!/[ \t]*[^\s/][^\n]*', text))
        try:
            tree = bob1.parse_text(d)
        except bob1.FormatError:
            S['bob1_refused'] += 1
            tree = None
        r = bob1.TextReader(d)
        while not r.done() and r.peek().startswith('MATERIAL'):
            key, _, first = r.next().partition(':')
            try:
                bob1._read_text_material(r, key.strip().upper(), first.strip())
            except (bob1.FormatError, KeyError):
                break
        if r.done() or r.peek().startswith('MATERIAL'):
            S['engine_no_body'] += 1
            continue
        line = r.fields[r.i][1]
        body_text = '\n'.join(text.split('\n')[line - 1:])
        lx = Lex(body_text)
        try:
            if lx.toks and lx.toks[0].strip() != r.peek():
                raise ValueError('body start not found')
            eng = []
            while not lx.done():
                eng.append(engine_record(lx, S))
        except (ValueError, IndexError) as exc:
            S['engine_parse_error'] += 1
            print(f'engine_parse_error {k}: {str(exc)[:80]}')
            continue
        S['engine_parsed'] += 1
        if tree is not None:
            S['compared_bodies'] += 1
            compare(eng, tree, S)
    for k in sorted(S):
        print(f'{k} {S[k]}')


if __name__ == '__main__':
    main()
