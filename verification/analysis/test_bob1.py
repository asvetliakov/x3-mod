"""BOB1 reader/writer and LOD overlay builder (tools/analysis/bob1.py, lod_overlay.py).

Synthetic bodies are built here; no game bytes are stored. The installed-body
checks run only when the X3 bottle is present and never write into it.
"""
import contextlib
import gzip
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import unittest.mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'analysis'))
import atlas_census
import bob1
import body_materials
import lod_atlas
import lod_overlay
import numpy as np
from inspect_x3 import read_catalogue
from sector_fog_census import Assets, unpack, write_catalogue


def point(flags, seed):
    s = bob1.point_struct(flags)
    n = s.size // 4
    vals = [seed * 7 + i - 3 for i in range(n)]
    if flags & 0x10:
        vals[-1] = 0x80000000 | seed           # u32 with the top bit set
    return (flags,) + tuple(vals)


def group(material, nfaces, npoints, precomputed, seed):
    g = {'material': material,
         'faces': [((seed + i) % npoints, (seed + i + 1) % npoints, (seed + i + 2) % npoints, 1)
                   for i in range(nfaces)]}
    if precomputed:
        g['extra'] = [(i % npoints, 1, -2, 3, -4, 5, -6) for i in range(nfaces + 1)]
    return g


def lod(value, flags, npoints, part_specs):
    parts = []
    for pflags, groups in part_specs:
        pre = bool(pflags & bob1.PART_PRECOMPUTED)
        part = {'flags': pflags, 'groups': [group(m, n, npoints, pre, i) for i, (m, n) in enumerate(groups)]}
        if pre:
            part['bounds'] = list(range(-5, 5))
        parts.append(part)
    return {'value': value, 'flags': flags,
            'points': [point(0x1b if i % 3 else 0x1f, i) for i in range(npoints)], 'parts': parts}


def synthetic_tree():
    effect = {'index': 0, 'flags': 0x02000000, 'technique': 2, 'effect': b'x3_test.fx',
              'params': [(b'g_long', 0, [-3]), (b'g_bool', 1, [1]), (b'g_float', 2, [65536]),
                         (b'g_float4', 5, [1, 2, 3, 4]), (b't_Diffuse', 8, b'tex\\a.tga')]}
    classic = {'index': 1, 'flags': 0x10, 'texture': b'plain.tga', 'colors': list(range(12)),
               'w24': 7, 'w26': 8, 'w2c': 9, 'maps': [(b'', 0), (b'b', 1), (b'c', 2)],
               'extra': [(b'd', 3), (b'', 4)]}
    return {'sections': [
        ('INFO', b'synthetic'),
        ('MAT6', [effect, classic]),
        ('BODY', [lod(12345, 0x40, 9, [(0x30000001, [(0, 4), (1, 6), (0, 3)]), (0x00000001, [(1, 2), (0, 5)])]),
                  lod(250, 0x40, 6, [(0x30000001, [(1, 2), (0, 3)]), (0x00000001, [(0, 1), (1, 1), (0, 2)])])]),
    ]}


def body_bytes():
    return bob1.serialise(synthetic_tree())


def game_dir(root, body):
    """Minimal installation: base 01/02.cat, addon 01/02.cat; body in base 02.cat as .pbb."""
    write_catalogue(root / '01.cat', [('types/Dummy.txt', b'x')])
    write_catalogue(root / '02.cat', [('objects/stations/test/Body.pbb', gzip.compress(body, mtime=0)),
                                      ('objects/stations/test/other.pbb', gzip.compress(body, mtime=0))])
    write_catalogue(root / 'addon/01.cat', [('addon/types/Dummy.txt', b'y')])
    write_catalogue(root / 'addon/02.cat', [('objects/cut/00001.pbd', b'z')])
    return root


def run(argv):
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        lod_overlay.main(argv)
    return out.getvalue()


class Bob1Format(unittest.TestCase):
    def test_hand_assembled_body(self):
        pts = struct.pack('>H3i2i3iI', 0x1b, 1, 2, 3, 4, 5, 6, 7, 8, 9)
        faces = struct.pack('>4i', 0, 0, 0, 1)
        data = (b'BOB1' + b'INFOhi\0/INF' + b'MAT6' + struct.pack('>i', 0) + b'/MAT'
                + b'BODY' + struct.pack('>H', 1) + struct.pack('>iI', 500, 0)
                + b'POIN' + struct.pack('>i', 1) + pts + b'/POI'
                + b'PART' + struct.pack('>iIHii', 1, 1, 1, 3, 1) + faces + b'/PAR' + b'/BOD/BOB')
        tree = bob1.parse(data)
        (l0,) = bob1.lods(tree)
        self.assertEqual(l0['value'], 500)
        self.assertEqual(l0['points'], [(0x1b, 1, 2, 3, 4, 5, 6, 7, 8, 9)])
        self.assertEqual(l0['parts'], [{'flags': 1, 'groups': [{'material': 3, 'faces': [(0, 0, 0, 1)]}]}])
        self.assertEqual(bob1.serialise(tree), data)

    def test_synthetic_round_trip(self):
        data = body_bytes()
        tree = bob1.parse(data)
        self.assertEqual(bob1.serialise(tree), data)
        self.assertEqual([bob1.lod_summary(l)['groups_per_part'] for l in bob1.lods(tree)], [[3, 2], [2, 3]])
        self.assertEqual(bob1.lods(tree)[0]['points'][0][0], 0x1f)

    def test_rejections(self):
        data = body_bytes()
        cases = {
            'unknown tag': data[:4] + b'ZZZZ' + data[4:],
            'wrong closer': data.replace(b'/INF', b'/NAM', 1),
            'trailing bytes': data + b'\0',
            'truncated': data[:-8],
            'CUT1 magic': b'CUT1' + data[4:],
        }
        for name, bad in cases.items():
            with self.subTest(name), self.assertRaises(bob1.FormatError):
                bob1.parse(bad)
        tree = synthetic_tree()
        bob1.lods(tree)[0]['weights'] = [[(0, 1)]]
        with self.assertRaises(bob1.FormatError):
            bob1.serialise(tree)
        self.assertEqual(bob1.kind(b'CUT1....'), 'CUT1')

    def test_body_stem(self):
        for name in ('stations\\test\\body', 'objects/stations/test/body.pbb', 'addon/objects/stations/test/body.bob'):
            self.assertEqual(bob1.body_stem(name).lower(), 'objects/stations/test/body')


SPTYPE = {v: k for k, v in bob1.SPTYPE_NAMES.items()}


def text_body(tree):
    """Text form (.bod) of a tree in the grammar of body-format-bob1.md "Text form": one vertex per
    point (BOD units, x2bc scale), one smoothing group per face, a 9-value N block per face,
    PART_VALUES_RAW for precomputed parts. parse_text of it gives first_use(tree) back."""
    fx = lambda v: f'{v / 65536:.6f}'
    out = ['// synthetic text body']
    for t, v in tree['sections']:
        if t == 'INFO':
            out.append('/# ' + v.decode('latin1'))
        elif t in bob1.MATVER:
            for m in v:
                row = [f'MATERIAL{bob1.MATVER[t]}: {m["index"]}']
                if t == 'MAT6':
                    row.append(hex(m['flags']))
                    if 'params' in m:
                        row += [str(m['technique']), m['effect'].decode(), str(len(m['params']))]
                        for name, typ, val in m['params']:
                            row += [name.decode(), SPTYPE[typ]]
                            row += [val.decode()] if typ == 8 else [str(x) for x in val] if typ < 2 else map(fx, val)
                        out.append('; '.join(row) + ';')
                        continue
                    row.append(m['texture'].decode() or 'NULL')
                else:
                    row.append(str(m['texture']))
                c = m['colors']
                row += [str(x) for x in c[:9]] + [str(c[9] << 16 | c[10]), str(c[11]), str(m['w24']), str(m['w26'])]
                bits = m['flags'] if t == 'MAT6' else m['flagword']
                row += [str(int(bool(bits & b))) for b in (0x2, 0x10, 0x8)] + [str(m['w2c'])]
                pairs = m['maps'] + m['extra'] if t == 'MAT6' else m['maps'][:2 if t == 'MAT3' else 3]
                row += [x for n, val in pairs for x in ((n.decode() or 'NULL') if t == 'MAT6' else str(n), str(val))]
                out.append('; '.join(row) + ';  / classic')
        elif t == 'BODY':
            for lod in v:
                out.append(f'{lod["value"]}; // body size')
                out += ['; '.join(str(round(c * bob1.TEXT_POS_DIVISOR)) for c in p[1:4]) + f'; // {i}'
                        for i, p in enumerate(lod['points'])]
                out.append('-1; -1; -1; // end of verts')
                for part in lod['parts']:
                    out.append('// ----- part -----')
                    for g in part['groups']:
                        for f in g['faces']:
                            cs = [lod['points'][i] for i in f[:3]]
                            uv = [x for c in cs for x in map(fx, c[4:6])]
                            n = [x for c in cs for x in map(fx, c[6:9])]
                            out.append(f'{g["material"]}; {f[0]}; {f[1]}; {f[2]}; -25; {cs[0][-1]}; '
                                       + '; '.join(uv) + '; /! N: { ' + '; '.join(n) + '; } !/ // face')
                    if 'bounds' in part:
                        out.append('/! PART_VALUES_RAW: ' + '; '.join(map(str, part['bounds'])) + '; !/')
                    out.append(f'-99; {part["flags"] & ~bob1.PART_PRECOMPUTED:020b}; // end of part')
                out.append(f'-99; {lod["flags"]:016b}; / end of body')
    return ('\r\n'.join(out) + '\r\n').encode('latin1')


def first_use(tree):
    """tree with every record's points renumbered in first-use order over its faces and the
    precomputed groups' 7-int records dropped (the text form carries none)."""
    import copy
    tree = copy.deepcopy(tree)
    for lod in bob1.lods(tree):
        order = list(dict.fromkeys(i for p in lod['parts'] for g in p['groups'] for f in g['faces'] for i in f[:3]))
        new = {old: k for k, old in enumerate(order)}
        lod['points'] = [lod['points'][i] for i in order]
        for p in lod['parts']:
            for g in p['groups']:
                g['faces'] = [tuple(new[i] for i in f[:3]) + (f[3],) for f in g['faces']]
                if 'extra' in g:
                    g['extra'] = []
    return tree


def text_tree():
    """Every text feature the parser maps: INFO, effect parameters of each numeric type, a classic
    MAT6 material, two records, a precomputed part (flag 0x20000000 kept), a hidden part."""
    tree = atlas_tree_lod0()
    mats = bob1.materials(tree)
    mats[0]['params'] += [(b'g_long', 0, [-3]), (b'g_bool', 1, [1]), (b'g_f4', 5, [65536, -32768, 0, 1])]
    mats[2].update(flags=0x12, texture=b'plain.tga', colors=list(range(9)) + [1, 2, 100],
                   maps=[(b'', 0), (b'b.tga', 1), (b'', 100)], extra=[(b'd', 3), (b'', 4)])
    tree['sections'].insert(0, ('INFO', b'$PATH: synthetic$'))
    return tree


def no_bump(tree):
    """tree with every t_BumpTexture parameter set to NULL (a text body lod_overlay may overlay)."""
    import copy
    tree = copy.deepcopy(tree)
    for m in bob1.materials(tree):
        if 'params' in m:
            m['params'] = [(n, t, b'NULL' if n == b't_BumpTexture' else v) for n, t, v in m['params']]
    return tree


TEXT_HEAD = '/ header comment\nMATERIAL6: 0; 0x2000000; 1; argon.fx; 1; t_DiffuseTexture;SPTYPE_STRING;a_diff.tga;\n'


class TextForm(unittest.TestCase):
    def test_synthetic_round_trip(self):
        tree = text_tree()
        data = text_body(tree)
        got = bob1.parse(data)                                       # dispatch: not 'BOB' -> parse_text
        self.assertEqual(got['sections'], first_use(tree)['sections'])
        self.assertEqual(got['text'], {'inferred_normals': 0, 'collision_boxes': 0})
        self.assertEqual(bob1.parse_binary(bob1.serialise(got))['sections'], got['sections'])
        self.assertTrue(lod_overlay.text_compiles(got))
        self.assertEqual(lod_overlay.text_refusals(got), ['text_no_tangents'])      # material 0 names a bump map
        self.assertEqual(lod_overlay.text_refusals(bob1.parse(text_body(no_bump(tree)))), [])
        part0 = bob1.lods(got)[0]['parts'][0]
        self.assertEqual((part0['flags'], part0['bounds'], part0['groups'][0]['extra']),
                         (0x30000001, list(range(10)), []))
        self.assertEqual(bob1.lods(got)[0]['parts'][1]['flags'], 0x30008001)
        self.assertEqual(bob1.text_kind(data), 'body')

    def test_points_normals_and_units(self):
        text = TEXT_HEAD + (
            '1000; // body size\n100000; 0; 0;\n0; 100000; 0;\n0; 0; 0;\n0; 0; -100000; // 3\n-1; -1; -1;\n'
            # two smoothed faces share vertex 2 with one uv: one point, the first corner's normal
            '0; 0; 1; 2; -25; 1; 0.5; 0.25; 1.0; 0.0; 0.0; 0.0; /! N: { 0; 0; 1; } !/\n'
            '0; 2; 1; 3; -25; -2147483648; 0.0; 0.0; 1.0; 0.0; 0.5; 0.5; /! N: 1; 0; 0; 0; 1; 0; 0; 0; 1; !/\n'
            # no N block, smoothing 0: the face normal cross(b - a, c - a) on every corner, own points
            '0; 0; 1; 2; -9; 0.5; 0.25; 1.0; 0.0; 0.0; 0.0;\n'
            '-99; 00000000000000000001;\n-99; 0000000001000000;\n')
        (lod,) = bob1.lods(bob1.parse_text(text.encode()))
        self.assertEqual((lod['value'], lod['flags']), (1000, 0x40))
        pts = lod['points']
        self.assertEqual(pts[0], (0x1b, 65536, 0, 0, 32768, 16384, 0, 0, 65536, 1))     # 100000 -> 65536
        self.assertEqual(len(pts), 3 + 3 + 3)                                           # smoothing groups differ
        self.assertEqual(pts[3][1:4] + pts[3][9:], (0, 0, 0, 0x80000000))               # vertex 2 again: other smoothing group
        self.assertEqual((pts[3][6:9], pts[5][1:4]), ((65536, 0, 0), (0, 0, -65536)))
        n = (0, 0, 65536)                                                               # (-1,1,0) x (-1,0,0)
        self.assertEqual([f[:3] for f in lod['parts'][0]['groups'][0]['faces']], [(0, 1, 2), (3, 4, 5), (6, 7, 8)])
        self.assertEqual(pts[6:8], [(0x1b, 65536, 0, 0, 32768, 16384) + n + (0,), (0x1b, 0, 65536, 0, 65536, 0) + n + (0,)])
        self.assertEqual(lod['parts'][0]['flags'], 1)                                   # no PART_VALUES_RAW: no 0x10000000

    def test_smoothed_normals_without_blocks(self):
        text = TEXT_HEAD + ('500;\n0; 0; 0;\n1000; 0; 0;\n0; 1000; 0;\n0; 0; 1000;\n-1; -1; -1;\n'
                            '0; 0; 1; 2; -17; 3;\n0; 0; 3; 1; -17; 2;\n0; 0; 2; 3; -17; 4;\n'
                            '-99; 00000000000000000001;\n-99; 0000000000000000;\n')
        tree = bob1.parse_text(text.encode())
        (lod,) = bob1.lods(tree)
        self.assertEqual(tree['text']['inferred_normals'], 3)
        self.assertEqual(lod_overlay.text_refusals(tree), ['text_normals_inferred'])
        pts = lod['points']
        self.assertEqual(pts[0][0], 0x19)                               # no uv
        # vertex 0: faces 0 (group 3) and 1 (group 2) share a bit, face 2 (group 4) shares none
        self.assertEqual([pts[i][4:8] for i in (0, 3, 6)], [(0, 46341, 46341, 3), (0, 46341, 46341, 2),
                                                            (65536, 0, 0, 4)])

    def test_classic_materials(self):
        mat5 = 'MATERIAL5: 0; 1047; 1;2;3; 4;5;6; 7;8;9; 65537; 100; 10; 0; 1;1;0; 100; 5;6; 7;8; 9;10;\n'
        mat3 = 'MATERIAL3: 1; 71; 1;2;3; 4;5;6; 7;8;9; 1; 100; 25; 5; 0;0;1; 100; 3;4; 5;6;\n'
        tail = '1;\n0; 0; 0;\n-1; -1; -1;\n0; 0; 0; 0; -1;\n-99; 1;\n-99; 0;\n'
        m = bob1.materials(bob1.parse_text((mat5 + tail).encode()))[0]
        self.assertEqual(m, {'index': 0, 'texture': 1047, 'colors': [1, 2, 3, 4, 5, 6, 7, 8, 9, 1, 1, 100],
                             'w24': 10, 'w26': 0, 'flagword': 0x12, 'w2c': 100, 'maps': [(5, 6), (7, 8), (9, 10)]})
        t3 = bob1.parse_text((mat3 + tail).encode())
        self.assertEqual([s for s, _ in t3['sections']], ['MAT3', 'BODY'])
        self.assertEqual((bob1.materials(t3)[0]['flagword'], bob1.materials(t3)[0]['maps']),
                         (0x8, [(3, 4), (5, 6), (0, 0)]))

    def test_rejections(self):
        body = '1;\n0; 0; 0;\n-1; -1; -1;\n0; 0; 0; 0; -1;\n-99; 1;\n-99; 0;\n'
        cases = {
            'scene': 'VER: 3;\nP 0; B x; N y; b\n',
            'MAT1 material': 'MATERIAL: 0;62; 1;2;3; 4;5;6; 7;8;9;\n' + body,
            'quad face': TEXT_HEAD + body.replace('0; 0; 0; 0; -1;', '0; 0; 0; 0; 0; -9;'),
            'vertex out of range': TEXT_HEAD + body.replace('0; 0; 0; 0; -1;', '0; 0; 0; 1; -1;'),
            'unknown block': TEXT_HEAD + body.replace('-99; 1;', '/! XYZ: 1; !/\n-99; 1;'),
            'unterminated': TEXT_HEAD + body + '5',
            'truncated': TEXT_HEAD + body[:-10],
            'comment only': '//NoAdsign\n',
            'mixed materials': TEXT_HEAD + 'MATERIAL5: 1; 0; 0;0;0; 0;0;0; 0;0;0; 0; 0; 0; 0; 0;0;0; 0; 0;0; 0;0; 0;0;\n' + body,
            'bad flags digits': TEXT_HEAD + body.replace('-99; 1;', '-99; 12;'),
            'unknown parameter type': TEXT_HEAD.replace('SPTYPE_STRING', 'SPTYPE_QUAT') + body,
            'flags over 32 bits': TEXT_HEAD + body.replace('-99; 1;', '-99; 1' + '0' * 32 + ';'),
            'unclosed block': TEXT_HEAD + body.replace('-99; 1;', '/! N: 1; 0; 0;\n-99; 1;'),
            'underscore digits': TEXT_HEAD + body.replace('1;\n0; 0; 0;', '1_000;\n0; 0; 0;'),
            'plus sign': TEXT_HEAD + body.replace('1;\n0; 0; 0;', '+5;\n0; 0; 0;'),
            'vertex over 32 bits': TEXT_HEAD + body.replace('1;\n0; 0; 0;', '1;\n0; 0; 2147483648;'),
            'face flag without bit 1': TEXT_HEAD + body.replace('0; 0; 0; 0; -1;', '0; 0; 0; 0; -8; 0;0; 0;0; 0;0;'),
            'face flag -100': TEXT_HEAD + body.replace('0; 0; 0; 0; -1;', '0; 0; 0; 0; -100;'),
            'nan uv': TEXT_HEAD + body.replace('0; 0; 0; 0; -1;', '0; 0; 0; 0; -9; nan; 0; 0; 0; 0; 0;'),
            'inf uv': TEXT_HEAD + body.replace('0; 0; 0; 0; -1;', '0; 0; 0; 0; -9; 1e999; 0; 0; 0; 0; 0;'),
            'uv outside 16.16': TEXT_HEAD + body.replace('0; 0; 0; 0; -1;', '0; 0; 0; 0; -9; 40000.0; 0; 0; 0; 0; 0;'),
            'material index over 16 bits': TEXT_HEAD.replace('MATERIAL6: 0;', 'MATERIAL6: 70000;') + body,
            'block as a string': TEXT_HEAD.replace('a_diff.tga;', '/! N: 0; !/') + body,
            'long parameter over 32 bits': TEXT_HEAD.replace('t_DiffuseTexture;SPTYPE_STRING;a_diff.tga;',
                                                             'n;SPTYPE_LONG;4294967296;') + body,
        }
        for name, text in cases.items():
            with self.subTest(name), self.assertRaises(bob1.FormatError):
                bob1.parse_text(text.encode())
        self.assertEqual(bob1.text_kind(b'VER: 3;\n'), 'scene')
        self.assertIsNotNone(bob1.parse_text((TEXT_HEAD + body).encode()))
        self.assertEqual(bob1.parse_text((TEXT_HEAD + body.replace('0; 0; 0; 0; -1;', '0; 0; 0; 0; -9; 1e0; .5;'
                                                                   ' 0.; 0; 0; 0;')).encode())['text']['inferred_normals'], 0)

    def test_collision_box_is_counted_and_refused(self):
        body = ('1;\n0; 0; 0;\n1; 0; 0;\n0; 1; 0;\n-1; -1; -1;\n0; 0; 1; 2; -1;\n'
                '/! COLLISION_BOX: 0.5; 0.1; 0.2; 0.3; 0.4; 0.5; 0.6; !/\n-99; 1;\n-99; 0;\n')
        tree = bob1.parse_text((TEXT_HEAD + body).encode())
        self.assertEqual(tree['text']['collision_boxes'], 1)
        self.assertEqual(lod_overlay.text_refusals(tree), ['text_collision_box'])


def ladder_tree(ladder):
    """ladder: [(value, groups in the one part)]; LOD 0's value is the scale."""
    return {'sections': [('BODY', [lod(t, 0, 4, [(0x30000001, [(0, 1)] * n)]) for t, n in ladder])]}


class Audit(unittest.TestCase):
    def row(self, ladder, view='very-high', f=1.0):
        return bob1.audit_row(bob1.parse(bob1.serialise(ladder_tree(ladder))), view, f)

    def test_drawable_sets(self):
        # sel = highest i with s < trunc(T_i f) (s >= 1), then -1 at Very High, clamped
        self.assertEqual(bob1.reachable([250, 150, 80, 30]), [0, 1, 2, 3, 4])
        self.assertEqual(bob1.drawable([250, 150, 80, 30], 'very-high'), [0, 1, 2, 3])
        self.assertEqual(bob1.drawable([250, 150, 80, 30], 'high'), [0, 1, 2, 3, 4])
        self.assertEqual(bob1.drawable([30, 10, 3, 30], 'very-high'), [0, 3])
        self.assertEqual(bob1.drawable([30, 10, 3, 30], 'low'), [0, 4])
        self.assertEqual(bob1.drawable([30], 'very-high'), [0])
        self.assertEqual(bob1.reachable([100, 1]), [0, 1])          # trunc(T f) < 2 never hit
        self.assertEqual(bob1.reachable([100, 2], f=0.5), [0, 1])   # f truncation: trunc(1.0) = 1
        self.assertEqual(bob1.reachable([15, 15], f=2.0), [0, 2])   # equal thresholds: the later wins

    def test_selection_bands(self):
        for th in ([250, 150, 80, 30], [30, 10, 3, 30], [30, 15, 5, 5, 50], [30], [100, 1], [25, 5]):
            for view in bob1.VIEW_DISTANCE:
                for f in (1.0, 0.5, 2.0):
                    with self.subTest(th=th, view=view, f=f):
                        bands = bob1.selection_bands(th, view, f)
                        self.assertEqual(bands[0][0], 1)
                        self.assertIsNone(bands[-1][1])
                        self.assertEqual(sorted({k for _, _, k in bands}), bob1.drawable(th, view, f))
                        for lo, hi, k in bands:
                            for s in range(lo, (hi or lo + 600)):
                                self.assertEqual(bob1.final_index(th, s, view, f), k)
        # run255 stand: argon_TL 30/15/5 draws LOD 0 at s >= 15 at Very High; LOD 1 for 5 <= s < 15
        self.assertEqual(bob1.format_bands(bob1.selection_bands([30, 15, 5])), 's<5:LOD2 5<=s<15:LOD1 s>=15:LOD0')
        self.assertEqual(bob1.format_bands(bob1.selection_bands([30, 15, 5, 5, 50])), 's<50:LOD4 s>=50:LOD0')

    def test_classes(self):
        keys = ('drawable', 'single_lod', 'last_never_drawn', 'lod0_only', 'dead_any_setting',
                'never_drawn_any_setting', 'coarse_multi_group')
        cases = {
            'single': ([(9000, 3)], ([0], True, False, False, False, [], True)),
            'clean': ([(9000, 4), (250, 2), (30, 1)], ([0, 1], False, True, False, False, [], True)),
            'two records': ([(9000, 4), (30, 1)], ([0], False, True, True, False, [], True)),
            'dock-like': ([(9000, 18), (30, 12), (10, 5), (3, 1), (30, 1)],
                          ([0, 3], False, True, False, True, [1, 2], False)),
            'tiny threshold': ([(9000, 5), (100, 3), (1, 2)], ([0], False, True, True, True, [2], True)),
        }
        for name, (ladder, expect) in cases.items():
            with self.subTest(name):
                row = self.row(ladder)
                self.assertEqual(tuple(row[k] for k in keys), expect)
                self.assertEqual(row['groups'], [n for _, n in ladder])
        row = self.row([(9000, 4), (250, 2), (30, 1)], view='high')
        self.assertEqual((row['drawable'], row['last_never_drawn'], row['coarse_multi_group']), ([0, 1, 2], False, False))

    def test_audit_over_archive(self):
        bodies = {'a': [(9000, 3)], 'b': [(9000, 18), (30, 12), (3, 1), (30, 1)], 'c': [(9000, 4), (20, 2)]}
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            members = [(f'objects/t/{k}.pbb', gzip.compress(bob1.serialise(ladder_tree(v)), mtime=0))
                       for k, v in bodies.items()]
            members.append(('objects/t/scene.pbb', gzip.compress(b'CUT1' + b'\0' * 8, mtime=0)))
            write_catalogue(root / '01.cat', members)
            rows, summary = bob1.audit(Assets(root))
        self.assertEqual(len(rows), 3)
        self.assertEqual({k: summary[k] for k in ('cut1', 'bob1', 'multi_lod', 'single_lod', 'last_never_drawn',
                                                   'lod0_only', 'dead_any_setting', 'coarse_multi_group',
                                                   'coarse_multi_group_multi_lod', 'never_drawn_records',
                                                   'dead_any_setting_records')},
                         dict(cut1=1, bob1=3, multi_lod=2, single_lod=1, last_never_drawn=2, lod0_only=1,
                              dead_any_setting=1, coarse_multi_group=2, coarse_multi_group_multi_lod=1,
                              never_drawn_records=3, dead_any_setting_records=1))


class Overlay(unittest.TestCase):
    def test_collapse(self):
        tree = bob1.parse(body_bytes())
        coarse = bob1.lods(tree)[-1]
        new = lod_overlay.coarse_record(coarse, 125)
        self.assertEqual(new['value'], 125)
        self.assertEqual([len(p['groups']) for p in new['parts']], [1, 1])
        # part 0: materials 1 (2 faces), 0 (3 faces) -> 0; part 1: 0 (1+2), 1 (1) -> 0
        self.assertEqual([p['groups'][0]['material'] for p in new['parts']], [0, 0])
        extra = new['parts'][0]['groups'][0]['extra']                      # records 0-2 + 0-3: one per point,
        self.assertEqual([e[0] for e in extra], [0, 1, 2, 3])              # in the faces' first-use order
        self.assertEqual(new['parts'][0]['bounds'], coarse['parts'][0]['bounds'])
        self.assertNotIn('extra', new['parts'][1]['groups'][0])

    def test_collapse_two(self):
        def mat(test, blend, alpha=b'NULL'):
            return {'index': 99, 'flags': 0x02000000, 'technique': 1, 'effect': b'argon.fx',
                    'params': [(b'g_AlphaBlendEnable', 0, [blend]), (b'g_ALPHATESTENABLE', 0, [test]),
                               (b'g_AlphaValue', 2, [65536]), (b't_AlphaTexture', 8, alpha)]}
        mats = [mat(0, 0, b'metals\\argon\\grid_alpha.tga'), mat(1, 0), mat(0, 1),
                {'index': 3, 'flags': 0x10, 'texture': b'plain.tga'}, mat(0, 0)]
        # positions; an alpha texture with test and blend off (the pilot ships' lattices) is opaque
        self.assertEqual(lod_overlay.alpha_materials(mats), {1, 2})
        coarse = bob1.lods(bob1.parse(body_bytes()))[-1]
        # part 0: materials 1 (2 faces), 0 (3 faces); part 1: 0 (1), 1 (1), 0 (2)
        new = lod_overlay.coarse_record(coarse, 7, {1})
        self.assertEqual([[(g['material'], len(g['faces'])) for g in p['groups']] for p in new['parts']],
                         [[(0, 3), (1, 2)], [(0, 3), (1, 1)]])
        self.assertEqual([len(g['extra']) for g in new['parts'][0]['groups']], [4, 3])
        self.assertEqual(sorted(f for g in new['parts'][0]['groups'] for f in g['faces']),
                         sorted(f for g in coarse['parts'][0]['groups'] for f in g['faces']))
        one = lod_overlay.coarse_record(coarse, 7, {1}, 'one')
        self.assertEqual([len(p['groups']) for p in one['parts']], [1, 1])
        self.assertEqual(lod_overlay.coarse_record(coarse, 7, {0, 1}),       # all alpha: one group per part
                         lod_overlay.coarse_record(coarse, 7, set(), 'one'))
        with self.assertRaises(ValueError):
            lod_overlay.coarse_record(coarse, 7, set(), 'three')

    def test_collapse_glow(self):
        coarse = bob1.lods(bob1.parse(body_bytes()))[-1]
        # part 0: materials 1 (2 faces), 0 (3 faces); part 1: 0 (1), 1 (1), 0 (2)
        shape = lambda lod: [[(g['material'], len(g['faces'])) for g in p['groups']] for p in lod['parts']]
        self.assertEqual(shape(lod_overlay.coarse_record(coarse, 7, set(), 'glow', {1})),
                         [[(0, 3), (1, 2)], [(0, 3), (1, 1)]])
        # glow groups keep their material (same-material groups of a part merge), alpha group last
        new = lod_overlay.coarse_record(coarse, 7, {1}, 'glow', {0})
        self.assertEqual(shape(new), [[(0, 3), (1, 2)], [(0, 3), (1, 1)]])
        self.assertEqual(shape(lod_overlay.coarse_record(coarse, 7, {0}, 'glow', {1})),
                         [[(1, 2), (0, 3)], [(1, 1), (0, 3)]])
        self.assertEqual([len(g['extra']) for g in new['parts'][0]['groups']], [4, 3])
        self.assertEqual(lod_overlay.coarse_record(coarse, 7, {1}, 'glow', set()),
                         lod_overlay.coarse_record(coarse, 7, {1}, 'two'))
        self.assertEqual(lod_overlay.coarse_record(coarse, 7, set(), 'one', {1}),     # one ignores glow
                         lod_overlay.coarse_record(coarse, 7, set(), 'one'))

    def placed(self, ladder, placement, threshold=None):
        tree = ladder_tree(ladder)
        ls = bob1.lods(tree)
        coarse = lod_overlay.coarse_record(ls[-1], None)
        idx = lod_overlay.place(ls, coarse, placement, threshold)
        out = bob1.lods(bob1.parse(bob1.serialise(tree)))       # the written body parses back
        return idx, out

    @staticmethod
    def final(thresholds, s, view='very-high', f=1.0):
        """Main-view index for metric s: first hit from the top, then the Very High -1."""
        sel = next((i for i in range(len(thresholds), 0, -1) if s < int(thresholds[i - 1] * f)), 0)
        return max(sel - 1, 0) if view == 'very-high' else sel

    def test_compact(self):
        for ladder in ([(9000, 4)], [(9000, 4), (30, 2)]):
            self.assertEqual(lod_overlay.default_placement(bob1.lods(ladder_tree(ladder))), 'compact')
        ship = [(9000, 37), (30, 31), (15, 28), (5, 23)]
        source = bob1.lods(ladder_tree(ship))
        (i, pad), ls = self.placed(ship, 'compact', 50)
        self.assertEqual((i, pad, len(ls)), (1, 2, 3))
        self.assertEqual([l['value'] for l in ls], [9000, 30, 50])           # C:T_1, pad:T_pad
        self.assertEqual(ls[0], source[0])
        r0 = bob1.Writer()
        bob1.write_lod(r0, source[0])
        r0 = bytes(r0.b)
        at = bob1.serialise(ladder_tree(ship)).find(r0)
        tree = ladder_tree(ship)
        lod_overlay.place(bob1.lods(tree), lod_overlay.coarse_record(bob1.lods(tree)[-1], None), 'compact', 50)
        self.assertGreater(at, 0)
        self.assertEqual(bob1.serialise(tree)[at:at + len(r0)], r0)         # record 0 byte-identical, same offset
        self.assertEqual((ls[1]['parts'], ls[1]['points'], ls[1]['flags']),
                         (ls[2]['parts'], ls[2]['points'], ls[2]['flags']))  # pad is a copy of C
        self.assertEqual(ls[2]['points'], source[-1]['points'])             # collision source: same points, faces
        self.assertEqual(sorted(f for p in ls[2]['parts'] for g in p['groups'] for f in g['faces']),
                         sorted(f for p in source[-1]['parts'] for g in p['groups'] for f in g['faces']))
        self.assertEqual([len(p['groups']) for p in ls[1]['parts']], [1])
        th = [l['value'] for l in ls[1:]]
        for s in range(1, 120):
            with self.subTest(s=s):
                # Very High: C (index 1) below T_pad, record 0 at and above; Low..High: the pad (index 2)
                self.assertEqual(self.final(th, s), 1 if s < 50 else 0)
                self.assertEqual(bob1.final_index(th, s), self.final(th, s))
                for view in ('low', 'medium', 'high'):
                    self.assertEqual(self.final(th, s, view), 2 if s < 50 else 0)
                    self.assertEqual(bob1.final_index(th, s, view), self.final(th, s, view))
        self.assertEqual({v: bob1.drawable(th, v) for v in bob1.VIEW_DISTANCE},
                         {'low': [0, 2], 'medium': [0, 2], 'high': [0, 2], 'very-high': [0, 1]})
        self.assertEqual(bob1.format_bands(bob1.selection_bands(th)), 's<50:LOD1 s>=50:LOD0')
        (i, pad), ls = self.placed([(9000, 4)], 'compact', 40)               # single-LOD: C gets T_pad
        self.assertEqual(((i, pad), [l['value'] for l in ls]), ((1, 2), [9000, 40, 40]))
        self.assertEqual([self.final([40, 40], s) for s in (1, 39, 40)], [1, 1, 0])
        # x/y/z/30-like: only T_1 matters (records 2..n-1 are dropped); T_pad = T_1 is accepted
        dock = [(9000, 5), (30, 3), (10, 2), (30, 1)]
        self.assertEqual([l['value'] for l in self.placed(dock, 'compact', 30)[1]], [9000, 30, 30])
        for t in (None, 1, 0, 29):                                          # required; >= 2; >= T_1
            with self.subTest(t=t), self.assertRaises(SystemExit):
                self.placed(ship, 'compact', t)
        ls = bob1.lods(ladder_tree(ship))
        self.assertEqual(lod_overlay.place(ls, lod_overlay.coarse_record(ls[-1], None), 'compact', 20,
                                           force_threshold=True), (1, 2))
        self.assertEqual([l['value'] for l in ls], [9000, 30, 20])
        with self.assertRaises(SystemExit):                                  # force never admits T < 2
            ls = bob1.lods(ladder_tree(ship))
            lod_overlay.place(ls, lod_overlay.coarse_record(ls[-1], None), 'compact', 1, force_threshold=True)

    def test_pad(self):
        ship = [(9000, 37), (30, 31), (15, 28), (5, 23)]
        (i, pad), ls = self.placed(ship, 'pad', 50)
        self.assertEqual((i, pad), (4, 5))
        self.assertEqual([l['value'] for l in ls], [9000, 30, 15, 5, 5, 50])   # C:T_last, pad:T_pad
        self.assertEqual(ls[:4], bob1.lods(ladder_tree(ship)))                # originals untouched
        self.assertEqual((ls[4]['parts'], ls[4]['points']), (ls[5]['parts'], ls[5]['points']))
        self.assertEqual([len(p['groups']) for p in ls[4]['parts']], [1])
        th = [l['value'] for l in ls[1:]]
        for s in range(1, 120):
            with self.subTest(s=s):
                # Very High: C below T_pad, record 0 at and above it; Low..High: the pad (same mesh)
                self.assertEqual(self.final(th, s), 4 if s < 50 else 0)
                self.assertEqual(self.final(th, s, 'high'), 5 if s < 50 else 0)
                self.assertEqual(bob1.final_index(th, s), self.final(th, s))
        self.assertEqual(bob1.drawable(th, 'very-high'), [0, 4])
        self.assertEqual(bob1.drawable(th, 'low'), [0, 5])
        (i, pad), ls = self.placed([(9000, 4)], 'pad', 40)                   # single-LOD: C gets T_pad
        self.assertEqual(((i, pad), [l['value'] for l in ls]), ((1, 2), [9000, 40, 40]))
        self.assertEqual([self.final([40, 40], s) for s in (1, 39, 40)], [1, 1, 0])
        # a two-record x/y/z/30-like body: T_pad above every threshold of records 1..n-1
        self.assertEqual([l['value'] for l in self.placed([(9000, 5), (30, 3), (10, 2), (30, 1)], 'pad', 31)[1]],
                         [9000, 30, 10, 30, 30, 31])
        for t in (None, 1, 0, 5, 15, 30):                                    # required; >= 2; above all
            with self.subTest(t=t), self.assertRaises(SystemExit):
                self.placed(ship, 'pad', t)
        tree = ladder_tree(ship)
        ls = bob1.lods(tree)
        self.assertEqual(lod_overlay.place(ls, lod_overlay.coarse_record(ls[-1], None), 'pad', 20,
                                           force_threshold=True), (4, 5))
        self.assertEqual([l['value'] for l in ls], [9000, 30, 15, 5, 5, 20])
        with self.assertRaises(SystemExit):                                  # force never admits T < 2
            lod_overlay.place(ls, lod_overlay.coarse_record(ls[-1], None), 'pad', 1, force_threshold=True)

    def test_before_last(self):
        (i, pad), ls = self.placed([(9000, 4), (100, 3), (30, 2)], 'before-last')
        self.assertEqual((i, pad), (2, None))
        self.assertEqual([l['value'] for l in ls], [9000, 100, 30, 30])      # default T_new = T_last
        self.assertEqual([len(p['groups']) for p in ls[2]['parts']], [1])
        self.assertEqual(len(ls[3]['parts'][0]['groups']), 2)               # old last kept as is
        old, new = [100, 30], [l['value'] for l in ls[1:]]
        for s in range(1, 150):
            with self.subTest(s=s):
                # Very High: the new record takes exactly the band the old n-2 record drew
                self.assertEqual(self.final(new, s) == 2, self.final(old, s) == 1)
                # High and below: the new record is never drawn; the old ladder is unchanged
                self.assertNotEqual(self.final(new, s, 'high'), 2)
                self.assertEqual({0: 0, 1: 1, 3: 2}[self.final(new, s, 'high')], self.final(old, s, 'high'))
        self.assertIn(2, bob1.drawable(new, 'very-high'))
        self.assertNotIn(2, bob1.drawable(new, 'high'))
        self.assertEqual(self.placed([(9000, 4), (100, 3), (30, 2)], 'before-last', 29)[1][2]['value'], 29)
        for t in (31, 100, 0):
            with self.subTest(t=t), self.assertRaises(SystemExit):
                self.placed([(9000, 4), (100, 3), (30, 2)], 'before-last', t)
        # any ladder shape is accepted, including x/y/z/30
        dock = [(9000, 18), (30, 12), (10, 5), (3, 1), (30, 1)]
        self.assertEqual([l['value'] for l in self.placed(dock, 'before-last')[1]], [9000, 30, 10, 3, 30, 30])
        self.assertEqual(bob1.drawable([30, 10, 3, 30, 30], 'very-high'), [0, 4])
        with self.assertRaises(SystemExit):
            self.placed([(9000, 4)], 'before-last')

    def test_append_pad(self):
        for ladder in ([(9000, 4)], [(9000, 4), (30, 2)]):                   # --threshold always required
            with self.subTest(ladder=ladder), self.assertRaises(SystemExit):
                self.placed(ladder, 'append-pad')
        with self.assertRaises(SystemExit):                                  # pad would be < 2
            self.placed([(9000, 4)], 'append-pad', 2)
        (i, pad), ls = self.placed([(9000, 4)], 'append-pad', 10)
        self.assertEqual((i, pad), (1, 2))
        self.assertEqual([l['value'] for l in ls], [9000, 10, 9])
        self.assertEqual(ls[1]['parts'], ls[2]['parts'])                     # pad is a copy of the coarse record
        self.assertEqual(ls[1]['points'], ls[2]['points'])
        self.assertEqual([len(p['groups']) for p in ls[1]['parts']], [1])
        # Very High: coarse below (T-1)*f, LOD 0 in [T-1, T); Low..High: coarse mesh (record or pad) below T*f
        self.assertEqual([self.final([10, 9], s) for s in (1, 8, 9, 10)], [1, 1, 0, 0])
        self.assertEqual([self.final([10, 9], s, 'high') for s in (1, 8, 9, 10)], [2, 2, 1, 0])
        (i, pad), ls = self.placed([(9000, 4), (30, 2)], 'append-pad', 15)
        self.assertEqual([l['value'] for l in ls], [9000, 30, 15, 14])
        self.assertEqual([self.final([30, 15, 14], s) for s in (13, 14, 20)], [2, 1, 0])   # old last in [14, 15)
        with self.assertRaises(SystemExit):
            self.placed([(9000, 4), (30, 2)], 'append-pad', 30)

    def test_slot_placement_and_errors(self):
        body = body_bytes()
        with tempfile.TemporaryDirectory() as folder:
            game = game_dir(Path(folder) / 'game', body)
            base = ['--game', str(game), '--dry-run']
            for slot in ('0', '-1', '100', '2', '4'):          # range, existing, non-contiguous
                with self.subTest(slot=slot), self.assertRaises(SystemExit):
                    run(base + ['--slot', slot, 'stations/test/body'])
            self.assertIn('target addon/03.cat', run(base + ['--slot', '3', 'stations/test/body=300']))
            self.assertIn('target addon/04.cat', run(base + ['--slot', '4', '--force-slot', 'stations/test/body=300']))
            with self.assertRaises(SystemExit):
                run(base + ['--slot', '2', '--force-slot', 'stations/test/body=300'])
            for argv in (['stations/test/body'], ['stations/test/body=249'], ['--threshold', '200', 'stations/test/body'],
                         ['stations/test/body=x']):                       # compact: T_pad required and >= 250
                with self.subTest(argv=argv), self.assertRaises(SystemExit):
                    run(base + argv)
            text = run(base + ['--threshold', '999', 'stations/test/body=300'])  # NAME=T wins; compact default
            self.assertIn("collapse=glow groups=['opaque:mat0:5f', 'opaque:mat0:4f']", text)
            self.assertIn('compact: new LOD1 threshold=250 + pad copy LOD2 threshold=300 (original LOD1..1 dropped)',
                          text)
            self.assertIn('very-high drawable [0, 1] by s: before s>=1:LOD0 | after s<300:LOD1 s>=300:LOD0', text)
            self.assertIn('high drawable [0, 2] by s: before s<250:LOD1 s>=250:LOD0 | after s<300:LOD2 s>=300:LOD0',
                          text)
            text = run(base + ['--threshold', '200', '--force-threshold', 'stations/test/body'])
            self.assertIn('compact: new LOD1 threshold=250 + pad copy LOD2 threshold=200', text)
            pad = base + ['--placement', 'pad']
            for argv in (['stations/test/body'], ['stations/test/body=250'], ['--threshold', '200', 'stations/test/body']):
                with self.subTest(argv=argv), self.assertRaises(SystemExit):   # pad: T_pad required and > 250
                    run(pad + argv)
            text = run(pad + ['--threshold', '200', '--force-threshold', 'stations/test/body'])
            self.assertIn('pad: new LOD2 threshold=250 + pad copy LOD3 threshold=200', text)
            text = run(pad + ['stations/test/body=300'])
            self.assertIn('collapse=one', run(base + ['--collapse', 'one', 'stations/test/body=300']))
            self.assertIn('pad: new LOD2 threshold=250 + pad copy LOD3 threshold=300', text)
            self.assertIn('very-high drawable [0, 2] by s: before s>=1:LOD0 | after s<300:LOD2 s>=300:LOD0', text)
            self.assertIn('high drawable [0, 3] by s: before s<250:LOD1 s>=250:LOD0 | after s<300:LOD3 s>=300:LOD0',
                          text)
            with self.assertRaises(SystemExit):                    # append-pad needs --threshold
                run(base + ['--placement', 'append-pad', 'stations/test/body'])
            text = run(base + ['--placement', 'append-pad', '--threshold', '125', 'stations/test/body'])
            self.assertIn('append-pad: new LOD2 threshold=125 + pad copy LOD3 threshold=124', text)
            with self.assertRaises(SystemExit):
                run(base + ['--keep-coarsest-hidden', 'stations/test/body'])   # option removed
            nomat = bob1.serialise({'sections': [('INFO', b'm'), ('BODY', bob1.lods(bob1.parse(body)))]})
            write_catalogue(game / 'addon/03.cat', [('objects/stations/test/cut.pbb', gzip.compress(body[:-8])),
                                                    ('objects/stations/test/nomat.pbb', gzip.compress(nomat))])
            with self.assertRaises(SystemExit):                    # MAT3 / no per-body materials refused
                run(['--game', str(game), '--dry-run', '--force-slot', '--slot', '5', 'stations/test/nomat=300'])
            self.assertIn('stations/test/nomat', run(['--game', str(game), '--dry-run', '--force-slot', '--slot', '5',
                                                      '--force-mat3', 'stations/test/nomat=300']))
            err = io.StringIO()
            for module, argv in ((lod_overlay, base + ['stations/test/missing']),
                                 (lod_overlay, base + ['stations/test/cut']),
                                 (bob1, ['info', '--game', str(game), 'stations/test/cut'])):
                with self.subTest(argv=argv[-1]), contextlib.redirect_stderr(err), \
                        contextlib.redirect_stdout(io.StringIO()), \
                        unittest.mock.patch.object(sys, 'argv', ['x'] + argv):
                    self.assertEqual(module.cli(), 2)
            self.assertEqual(len(err.getvalue().splitlines()), 3)
            self.assertTrue(all(l.startswith('error: ') for l in err.getvalue().splitlines()))

    @unittest.mock.patch.object(lod_overlay, 'running_game', return_value=[])
    def test_overlay_files(self, _running):
        body = body_bytes()
        source = bob1.lods(bob1.parse(body))
        with tempfile.TemporaryDirectory() as folder:
            game = game_dir(Path(folder) / 'game', body)
            out = Path(folder) / 'out'
            originals = lod_overlay.original_archives(game)
            before = lod_overlay.hash_files(originals)
            text = run(['--game', str(game), '--dry-run', '--placement', 'before-last', 'stations\\test\\body'])
            self.assertIn('before-last: new LOD1 threshold=250', text)
            self.assertIn('target addon/03.cat', text)
            self.assertFalse(out.exists())
            with self.assertRaises(SystemExit):
                run(['--game', str(game), '--out', str(game / 'sub'), 'stations/test/body=300'])
            with self.assertRaises(SystemExit):          # before-last needs T <= T_last
                run(['--game', str(game), '--out', str(out), '--placement', 'before-last', '--threshold', '251',
                     'stations/test/body'])
            self.assertFalse(out.exists())
            run(['--game', str(game), '--out', str(out), 'stations/test/body=300'])
            self.assertEqual(sorted(p.name for p in out.rglob('*') if p.is_file()),
                             ['03.cat', '03.dat', '03.x3m-lod.json'])
            (entry,) = read_catalogue(out / 'addon/03.cat')
            self.assertEqual(entry['path'], 'objects/stations/test/Body.pbb')
            stored = bytes(v ^ 0x33 for v in (out / 'addon/03.dat').read_bytes())
            ladder = bob1.lods(bob1.parse(unpack(stored)))
            self.assertEqual([l['value'] for l in ladder], [12345, 250, 300])      # compact
            self.assertEqual([len(p['groups']) for p in ladder[1]['parts']], [1, 1])
            self.assertEqual(ladder[1]['parts'], ladder[2]['parts'])
            self.assertEqual(ladder[0], source[0])
            (body_rec,) = json.loads((out / 'addon/03.x3m-lod.json').read_text())['bodies']
            self.assertEqual((body_rec['placement'], body_rec['new_lod'], body_rec['pad_lod'],
                              body_rec['threshold'], body_rec['pad_threshold']), ('compact', 1, 2, 250, 300))
            self.assertEqual((body_rec['source_thresholds'], body_rec['thresholds']),
                             ([12345, 250], [12345, 250, 300]))
            self.assertEqual(lod_overlay.hash_files(originals), before)

            # --install targets the game directory; the new slot wins under the resolver's precedence
            with self.assertRaises(SystemExit):          # --replace with nothing installed
                run(['--game', str(game), '--dry-run', '--replace', 'stations/test/body=300'])
            run(['--game', str(game), '--install', 'stations/test/body=300'])
            self.assertEqual(lod_overlay.hash_files(originals), before)
            data, src = Assets(game).get('objects/stations/test/body.pbb')
            self.assertEqual(src['source'], 'addon/03.cat')
            self.assertEqual(len(bob1.lods(bob1.parse(data))), 3)
            with self.assertRaises(SystemExit):          # installed marker: refuse to stack
                run(['--game', str(game), '--out', str(Path(folder) / 'out2'), 'stations/test/body=300'])

            # --replace: a failed write restores the installed overlay byte for byte
            files = lambda: {p.name: p.read_bytes() for p in (game / 'addon').glob('03.*')}
            old = files()
            self.assertEqual(sorted(old), ['03.cat', '03.dat', '03.x3m-lod.json'])
            with unittest.mock.patch.object(lod_overlay, 'write_catalogue', side_effect=OSError('disk full')), \
                    self.assertRaises(OSError):
                run(['--game', str(game), '--install', '--replace', 'stations/test/body=400'])
            self.assertEqual(files(), old)
            with self.assertRaises(SystemExit):          # --slot must be the installed slot
                run(['--game', str(game), '--dry-run', '--replace', '--slot', '4', '--force-slot', 'stations/test/body=400'])
            # --out --replace builds the replacement for the installed slot from the original body
            out3 = Path(folder) / 'out3'
            self.assertIn('target addon/03.cat', run(['--game', str(game), '--out', str(out3), '--replace',
                                                      'stations/test/body=400']))
            built = (out3 / 'addon/03.dat').read_bytes()
            self.assertEqual(files(), old)
            text = run(['--game', str(game), '--install', '--replace', 'stations/test/body=400'])
            self.assertIn('replaced the installed addon/03 overlay', text)
            self.assertEqual(sorted(files()), ['03.cat', '03.dat', '03.x3m-lod.json'])
            self.assertEqual((game / 'addon/03.dat').read_bytes(), built)
            data, src = Assets(game).get('objects/stations/test/body.pbb')
            self.assertEqual([l['value'] for l in bob1.lods(bob1.parse(data))], [12345, 250, 400])
            self.assertEqual(json.loads((game / 'addon/03.x3m-lod.json').read_text())['collapse'], 'glow')
            self.assertEqual(lod_overlay.hash_files(originals), before)
            # partial move-aside: the second rename fails -> the first file goes back, nothing is deleted
            old = files()
            real_rename = Path.rename
            calls = []

            def flaky_rename(path, target):
                calls.append(path.name)
                if len(calls) == 2:
                    raise OSError('sharing violation')
                return real_rename(path, target)
            with unittest.mock.patch.object(Path, 'rename', flaky_rename), self.assertRaises(OSError):
                run(['--game', str(game), '--install', '--replace', 'stations/test/body=500'])
            self.assertEqual(calls, ['03.cat', '03.dat'])
            self.assertEqual(files(), old)
            self.assertEqual(sorted((game / 'addon').glob('*.x3m-replaced')), [])
            # an aside that cannot be deleted after success is a warning, not a rollback
            real_unlink = Path.unlink

            def stuck_unlink(path, missing_ok=False):
                if path.name == '03.dat.x3m-replaced':
                    raise OSError('busy')
                return real_unlink(path, missing_ok=missing_ok)
            err = io.StringIO()
            with unittest.mock.patch.object(Path, 'unlink', stuck_unlink), contextlib.redirect_stderr(err):
                run(['--game', str(game), '--install', '--replace', 'stations/test/body=500'])
            self.assertIn('could not be removed', err.getvalue())
            self.assertEqual([p.name for p in (game / 'addon').glob('*.x3m-replaced')], ['03.dat.x3m-replaced'])
            self.assertEqual(json.loads((game / 'addon/03.x3m-lod.json').read_text())['bodies'][0]['pad_threshold'], 500)
            with self.assertRaises(SystemExit):          # a leftover aside blocks the next --replace
                run(['--game', str(game), '--install', '--replace', 'stations/test/body=400'])
            (game / 'addon/03.dat.x3m-replaced').unlink()
            with (game / '01.dat').open('ab') as f:    # originals changed since the install: refuse
                f.write(b'x')
            with self.assertRaises(SystemExit):
                run(['--game', str(game), '--dry-run', '--replace', 'stations/test/body=400'])
            self.assertEqual(sorted(files()), ['03.cat', '03.dat', '03.x3m-lod.json'])

    def test_install_refused_while_game_runs(self):
        body = body_bytes()
        with tempfile.TemporaryDirectory() as folder:
            game = game_dir(Path(folder) / 'game', body)
            argv = ['--game', str(game), '--install', 'stations/test/body=300']
            with unittest.mock.patch.object(lod_overlay, 'running_game', return_value=['123 C:\\X3\\X3AP.exe']):
                with self.assertRaises(SystemExit):
                    run(argv)
                self.assertFalse((game / 'addon/03.cat').exists())
                run(['--game', str(game), '--out', str(Path(folder) / 'out'), 'stations/test/body=300'])  # --out ok
            with unittest.mock.patch.object(lod_overlay, 'running_game', side_effect=RuntimeError('no ps')):
                with self.assertRaises(SystemExit):     # unknown state refuses too
                    run(argv)
                run(argv + ['--force-running'])
            self.assertTrue((game / 'addon/03.cat').exists())


def material_tree():
    def mat(diff, light, alpha=b'NULL'):
        return {'index': 0, 'flags': 0x02000000, 'technique': 1, 'effect': b'argon.fx',
                'params': [(b'g_ALPHATESTENABLE', 0, [int(alpha != b'NULL')]), (b't_DiffuseTexture', 8, diff),
                           (b't_AlphaTexture', 8, alpha), (b't_LightMapTexture', 8, light)]}
    mats = [mat(b'a_diff.tga', b'NULL'), mat(b'a_diff.tga', b'm\\l1_light.tga'), mat(b'b_diff.tga', b'm\\l1_light.tga'),
            mat(b'a_diff.tga', b'NULL', b'grid_alpha.tga'), mat(b'metal_exhaust_diff.tga', b'NULL'),
            mat(b'a_diff.tga', b'NULL')]                      # 5 = same full texture tuple as 0
    pts = [(1, 0, 0, 0), (1, 10, 0, 0), (1, 0, 10, 0), (1, 0, 0, 20)]
    f50, f100 = (0, 1, 2, 1), (0, 1, 3, 1)                     # triangle areas 50 and 100
    g = lambda m, *faces: {'material': m, 'faces': list(faces)}
    coarse = {'value': 5, 'flags': 0, 'points': pts, 'parts': [
        {'flags': 1, 'groups': [g(0, f50, f100), g(1, f50), g(2, f50), g(3, f50), g(5, f50), g(4, f50)]},
        {'flags': 1, 'groups': [g(1, f50), g(0, f50)]}]}
    lod0 = {'value': 100, 'flags': 0, 'points': pts, 'parts': [{'flags': 1, 'groups': [g(0, f50)]}]}
    return {'sections': [('MAT6', mats), ('BODY', [lod0, coarse])]}


def dds_dxt1_white(colour=0xffff):
    d = bytearray(128)
    d[:4] = b'DDS '
    struct.pack_into('<IIII', d, 8, 0, 4, 4, 0)             # height, width at 12, 16
    struct.pack_into('<I', d, 28, 1)
    struct.pack_into('<I4s', d, 80, 4, b'DXT1')
    return bytes(d) + struct.pack('<HHI', colour, 0, 0)       # one block, all texels colour 0 (white)


class MaterialCensus(unittest.TestCase):
    def test_census_and_rules_without_assets(self):
        tree = bob1.parse(bob1.serialise(material_tree()))
        c = body_materials.census(tree)
        self.assertEqual((c['lod'], c['draws'], c['total_area']), (1, 8, 500.0))
        m = c['materials']
        self.assertEqual([m[i]['light'] for i in range(6)],
                         ['placeholder', 'real', 'real', 'placeholder', 'placeholder', 'placeholder'])
        self.assertEqual((m[0]['faces'], m[0]['groups'], m[0]['share'], m[1]['share']), (3, 2, 0.4, 0.2))
        self.assertTrue(m[3]['alpha'] and not m[0]['alpha'])
        self.assertEqual([i for i in range(6) if m[i]['glow_name']], [4])
        self.assertEqual(c['real_light'], {1, 2})
        r = c['rules']
        self.assertEqual({k: r[k] for k in ('a', 'b', 'c', 'd', 'db', 'e')},
                         dict(a=7, b=6, c=3, d=6, db=6, e=3))
        self.assertEqual((r['b_mixes_alpha'], r['a_param_diffs']), (1, 0))
        self.assertEqual(body_materials.census(tree, 0)['draws'], 1)
        out = io.StringIO()
        body_materials.format_census(c, 'x', out)
        self.assertIn('draws per rule: a=7  b=6  c=3  d=6  db=6  e=3', out.getvalue())

    def test_original_assets_skip_overlay_and_light_stats(self):
        body = bob1.serialise(material_tree())
        tree = material_tree()
        bob1.lods(tree)[1]['parts'] = bob1.lods(tree)[1]['parts'][:1]
        with tempfile.TemporaryDirectory() as folder:
            game = Path(folder)
            write_catalogue(game / '01.cat', [('dds/l1_light.pck', gzip.compress(dds_dxt1_white(), mtime=0)),
                                              ('dds/dark_light.pck', gzip.compress(dds_dxt1_white(0x0841), mtime=0)),
                                              ('dds/NONE_WHITE.pck', gzip.compress(dds_dxt1_white(), mtime=0)),
                                              ('dds/amb_light.pck', gzip.compress(dds_dxt1_white(), mtime=0)),
                                              ('dds/amb_light.tga', b'tga')])
            write_catalogue(game / '02.cat', [('objects/ships/x/b.pbb', gzip.compress(body, mtime=0))])
            write_catalogue(game / 'addon/01.cat', [('objects/ships/x/b.pbb',
                                                     gzip.compress(bob1.serialise(tree), mtime=0))])
            self.assertEqual(bob1.lods(bob1.parse(Assets(game).get('objects/ships/x/b.pbb')[0]))[1]['parts'][1:], [])
            (game / 'addon/01.x3m-lod.json').write_text('{}')
            assets, skipped = body_materials.original_assets(game)
            self.assertEqual(skipped, ['addon/01.cat'])
            c = body_materials.census(bob1.parse(assets.read_entry(bob1.resolve_body(assets, 'ships/x/b'))), None, assets)
            self.assertEqual(c['draws'], 8)                      # the original, not the overlay
            li = c['materials'][1]['light_info']
            self.assertEqual((li['size'], li['luma'], li['bright']), ((4, 4), 1.0, 1.0))
            self.assertTrue(c['materials'][2]['glow_bright'])
            self.assertEqual(c['rules']['e'], c['rules']['d'])   # both real light maps are bright
            mats = bob1.materials(material_tree())
            dark = dict(mats[1], params=[(n, t, b'dark_light.tga' if n == b't_LightMapTexture' else v)
                                         for n, t, v in mats[1]['params']])
            self.assertEqual(lod_overlay.glow_materials(assets, mats + [dark]), {1, 2})
            self.assertEqual(lod_overlay.glow_materials(assets, mats, [0, 1]), {1})
            light = lambda name: dict(mats[0], params=[(n, t, name if n == b't_LightMapTexture' else v)
                                                       for n, t, v in mats[0]['params']])
            stock, amb = light(b'C:\\3ds Max 9\\Maps\\NONE_WHITE.dds'), light(b'm\\amb_light.tga')
            self.assertEqual(lod_overlay.glow_materials(assets, [stock, amb]), {0})   # NONE_WHITE is all bright
            info = body_materials.texture_info(assets, b'm\\amb_light.tga', {})
            self.assertEqual(info['status'], 'error')
            self.assertEqual(body_materials.light_status(info), 'error')
            self.assertEqual(body_materials.light_status(
                body_materials.texture_info(assets, b'x\\NONE_WHITE.dds', {})), 'stock')


def synth_tree(strengths=(26214, 32768, 32768, 32768, 32768, 32768)):
    """material_tree with g_Mat* scalars; mat1 light map dark (real, not glow), mat2 white (glow)."""
    tree = material_tree()
    lights = (b'NULL', b'm\\dark_light.tga', b'm\\l1_light.tga', b'NULL', b'NULL', b'NULL')
    for m, s, l in zip(bob1.materials(tree), strengths, lights):
        m['params'] = ([(n, t, l if n == b't_LightMapTexture' else v) for n, t, v in m['params']]
                       + [(b'g_MatDiffuseStrength', 2, [s]), (b'g_MatSpecularPower', 2, [393216]),
                          (b'g_Brightness', 2, [s])])
    return tree


def light_game(root, body):
    write_catalogue(root / '01.cat', [('dds/l1_light.pck', gzip.compress(dds_dxt1_white(), mtime=0)),
                                      ('dds/dark_light.pck', gzip.compress(dds_dxt1_white(0x0841), mtime=0))])
    write_catalogue(root / '02.cat', [('objects/stations/test/Body.pbb', gzip.compress(body, mtime=0))])
    write_catalogue(root / 'addon/01.cat', [('addon/types/Dummy.txt', b'y')])
    return root


class AreaRuleAndSynth(unittest.TestCase):
    shape = staticmethod(lambda lod: [[(g['material'], len(g['faces'])) for g in p['groups']] for p in lod['parts']])

    def test_area_select(self):
        areas = {3: 10.0, 1: 10.0, 2: 30.0}
        sel = body_materials.area_select
        self.assertEqual(sel(areas, 50), ([2], 30.0, 50.0))
        self.assertEqual(sel(areas, 60), ([2], 30.0, 50.0))            # exactly 60 % is covered
        self.assertEqual(sel(areas, 70)[0], [2, 1])                    # tie: lower index first
        self.assertEqual(sel(areas, 100)[0], [2, 1, 3])
        self.assertEqual(sel(areas, 0)[0], [])
        self.assertEqual(sel({}, 70), ([], 0.0, 0.0))

    def test_rule_f_and_substitution_without_assets(self):
        c = body_materials.census(bob1.parse(bob1.serialise(material_tree())))
        # real light maps 1 (area 100) and 2 (50), neither bright without assets
        self.assertEqual([c['rules'][k] for k in ('c', 'e', 'f50', 'f70', 'f90', 'f100', 'd')], [3, 3, 5, 6, 6, 6, 6])
        self.assertEqual((c['area'][50]['kept'], c['area'][50]['covered'], c['area'][50]['total']), ([1], 100.0, 150.0))
        self.assertEqual(c['area'][70]['kept'], [1, 2])
        s = c['subst']
        self.assertEqual({k: s['c'][k] for k in ('own', 'target', 'same', 'different')},
                         dict(own=0.0, target=250.0, same=150.0, different=100.0))
        self.assertEqual({k: s['f50'][k] for k in ('own', 'target', 'same', 'different')},
                         dict(own=100.0, target=250.0, same=50.0, different=100.0))
        self.assertEqual(s['c']['pairs'], {('a_diff', 'b_diff'): 50.0, ('a_diff', 'metal_exhaust_diff'): 50.0})
        for key, v in s.items():
            self.assertAlmostEqual(v['own'] + v['target'] + v['same'] + v['different'], c['total_area'], msg=key)
        out = io.StringIO()
        body_materials.format_census(c, 'x', out)
        self.assertIn('  f50: draws=5  kept [1]  covered 0.667 of the candidate area', out.getvalue())
        self.assertIn('  c: 0.000  0.500  0.300  0.200', out.getvalue())

    def test_synth_materials(self):
        tree = synth_tree()
        mats = bob1.materials(tree)
        coarse = bob1.lods(tree)[-1]
        alpha = lod_overlay.alpha_materials(mats)
        self.assertEqual(alpha, {3})
        remap, rep = lod_overlay.synth_materials(mats, coarse, alpha, 'glow', {2})
        # part 0: 0 (area 150) absorbs 1, 5, 4 (50 each); part 1: 1 (50, first of a face tie) absorbs 0 (50)
        self.assertEqual(remap, {0: 6, 1: 7})
        self.assertEqual(len(mats), 8)
        self.assertEqual([(r['index'], r['dominant'], r['absorbed']) for r in rep], [(6, 0, [0, 1, 4, 5]), (7, 1, [0, 1])])
        self.assertEqual(rep[0]['params'], [('g_MatDiffuseStrength', 26214, 29491.0, 29491),
                                            ('g_MatSpecularPower', 393216, 393216.0, 393216)])
        param = lambda m, n: [v for pn, _, v in m['params'] if pn == n][0]
        self.assertEqual((param(mats[6], b'g_MatDiffuseStrength'), param(mats[7], b'g_MatDiffuseStrength')),
                         ([29491], [29491]))
        self.assertEqual(param(mats[6], b'g_Brightness'), [26214])              # not a g_Mat* factor
        self.assertEqual((mats[6]['index'], mats[7]['index']), (6, 7))
        self.assertEqual({k: v for k, v in mats[6].items() if k not in ('index', 'params')},
                         {k: v for k, v in mats[0].items() if k not in ('index', 'params')})
        new = lod_overlay.coarse_record(coarse, 7, alpha, 'glow', {2}, remap)
        self.assertEqual(self.shape(new), [[(6, 5), (2, 1), (3, 1)], [(7, 2)]])   # alpha group alone: no copy
        same = synth_tree((32768,) * 6)
        self.assertEqual(lod_overlay.synth_materials(bob1.materials(same), bob1.lods(same)[-1], {3}, 'two'), ({}, []))
        self.assertEqual(len(bob1.materials(same)), 6)
        # glow-area keeps light-map materials like glow ones; 'two' ignores the kept set
        coarse0 = bob1.lods(material_tree())[-1]
        self.assertEqual(lod_overlay.coarse_record(coarse0, 7, {3}, 'glow-area', {1, 2}),
                         lod_overlay.coarse_record(coarse0, 7, {3}, 'glow', {1, 2}))
        self.assertEqual(lod_overlay.coarse_record(coarse0, 7, {3}, 'two', {1, 2}),
                         lod_overlay.coarse_record(coarse0, 7, {3}, 'two'))

    @unittest.mock.patch.object(lod_overlay, 'running_game', return_value=[])
    def test_glow_area_overlay_round_trip(self, _running):
        body = bob1.serialise(synth_tree())
        with tempfile.TemporaryDirectory() as folder:
            game = light_game(Path(folder) / 'game', body)
            assets, _ = lod_overlay.original_assets(game)
            tree = bob1.parse(body)
            mats, coarse = bob1.materials(tree), bob1.lods(tree)[-1]
            self.assertEqual(lod_overlay.glow_materials(assets, mats), {2})
            self.assertEqual(lod_overlay.light_area_materials(assets, mats, coarse, {2}, 50), ({1}, 100.0, 100.0))
            self.assertEqual(lod_overlay.light_area_materials(assets, mats, coarse, {2}, 0), (set(), 0.0, 100.0))
            c = body_materials.census(tree, None, assets)
            self.assertEqual((c['glow'], c['area'][50]['kept'], c['rules']['f50']), ({2}, [1], 6))
            for argv in (['--collapse', 'glow-area', 'stations/test/body=300'],     # no percentage
                         ['--collapse', 'glow-area', '101', 'stations/test/body=300'],
                         ['--collapse', 'glow-area=x', 'stations/test/body=300'], ['--collapse', 'three', 'x=300']):
                with self.subTest(argv=argv), self.assertRaises(SystemExit), \
                        contextlib.redirect_stderr(io.StringIO()):
                    run(['--game', str(game), '--dry-run'] + argv)
            out = Path(folder) / 'out'
            text = run(['--game', str(game), '--out', str(out), '--collapse', 'glow-area', '50', 'stations/test/body=300'])
            self.assertIn("groups=['opaque:mat6~0:4f', 'light:mat1:1f', 'glow:mat2:1f', 'alpha:mat3:1f',"
                          " 'opaque:mat6~0:1f', 'light:mat1:1f']", text)
            self.assertIn('synthesized mat6 = mat0', text)
            self.assertIn('g_MatDiffuseStrength=0.4->0.4333', text)
            data = unpack(bytes(v ^ 0x33 for v in (out / 'addon/02.dat').read_bytes()))
            written = bob1.parse(data)
            self.assertEqual(bob1.serialise(written), data)
            wm, ladder = bob1.materials(written), bob1.lods(written)
            self.assertEqual(wm[:6], bob1.materials(bob1.parse(body)))
            self.assertEqual((len(wm), wm[6]['index']), (7, 6))
            self.assertEqual([v for n, _, v in wm[6]['params'] if n == b'g_MatDiffuseStrength'], [[28399]])
            self.assertEqual(ladder[0], bob1.lods(bob1.parse(body))[0])
            self.assertEqual(self.shape(ladder[1]), [[(6, 4), (1, 1), (2, 1), (3, 1)], [(6, 1), (1, 1)]])
            self.assertEqual(ladder[1]['parts'], ladder[2]['parts'])
            self.assertTrue(all(g['material'] < len(wm) for l in ladder for p in l['parts'] for g in p['groups']))
            (rec,) = (m := json.loads((out / 'addon/02.x3m-lod.json').read_text()))['bodies']
            self.assertEqual((m['collapse'], m['area_percent'], m['synth_material']), ('glow-area', 50.0, True))
            self.assertEqual((rec['glow'], rec['area_kept'], rec['source_materials']), ([2], [1], 6))
            self.assertEqual([(s['index'], s['dominant'], s['absorbed']) for s in rec['synth']], [(6, 0, [0, 4, 5])])
            self.assertEqual(rec['synth'][0]['params'][0], dict(name='g_MatDiffuseStrength', dominant=26214,
                                                               mean=28398.7, written=28399))
            out2 = Path(folder) / 'out2'
            run(['--game', str(game), '--out', str(out2), '--collapse=glow-area', '50', '--no-synth-material',
                 'stations/test/body=300'])
            plain = bob1.parse(unpack(bytes(v ^ 0x33 for v in (out2 / 'addon/02.dat').read_bytes())))
            self.assertEqual(len(bob1.materials(plain)), 6)
            self.assertEqual(self.shape(bob1.lods(plain)[1]), [[(0, 4), (1, 1), (2, 1), (3, 1)], [(0, 1), (1, 1)]])
            self.assertEqual(json.loads((out2 / 'addon/02.x3m-lod.json').read_text())['bodies'][0]['synth'], [])
            text = run(['--game', str(game), '--dry-run', '--collapse', 'glow-area=0', 'stations/test/body=300'])
            self.assertIn("groups=['opaque:mat6~0:5f', 'glow:mat2:1f', 'alpha:mat3:1f', 'opaque:mat7~1:2f']", text)


def atlas_tree():
    def mat(diff, light):
        return {'index': 0, 'flags': 0x02000000, 'technique': 1, 'effect': b'argon.fx',
                'params': [(b't_DiffuseTexture', 8, diff), (b't_LightMapTexture', 8, light)]}
    q = lambda x: int(round(x * 65536))
    pt = lambda x, y, z, u, v: (0x1b, x, y, z, q(u), q(v), 0, 0, 65536, 1)
    pts = [pt(0, 0, 0, 0, 0), pt(10, 0, 0, 1, 0), pt(0, 10, 0, 0, 1), pt(20, 0, 0, 2, 0),        # 0-3
           pt(15, 0, 0, 1.5, 0), pt(15, 5, 0, 1.5, 0.5),                                       # 4-5
           pt(0, 0, 5, 3.2, 0.1), pt(10, 0, 5, 3.8, 0.1), pt(0, 10, 5, 3.2, 0.6),              # 6-8
           pt(0, 0, 8, 0.8, 0), pt(10, 0, 8, 1.2, 0), pt(0, 10, 8, 0.8, 0.5)]                  # 9-11
    # material 0: A in [0,1] (area 50), B tiling u 0..2 (100), F u 1..1.5 shifted by 1 (12.5), shares point 1 with A
    # material 1: C u 3.2..3.8 (50), D straddles u = 1 (50), H in [0,1] (40) and shares point 2 with material 0
    g0 = {'material': 0, 'faces': [(0, 1, 2, 1), (0, 3, 2, 1), (1, 4, 5, 1)]}
    g1 = {'material': 1, 'faces': [(6, 7, 8, 1), (9, 10, 11, 1), (2, 11, 9, 1)]}
    coarse = {'value': 5, 'flags': 0, 'points': pts, 'parts': [{'flags': 1, 'groups': [g0, g1]}]}
    lod0 = {'value': 100, 'flags': 0, 'points': pts, 'parts': [{'flags': 1, 'groups': [g0]}]}
    return {'sections': [('MAT6', [mat(b'a_diff.tga', b'NULL'), mat(b'b_diff.tga', b'b_light.tga')]),
                         ('BODY', [lod0, coarse])]}


def dds_header(w, h, fourcc=b'DXT1'):
    d = bytearray(128)
    d[:4] = b'DDS '
    struct.pack_into('<II', d, 12, h, w)
    struct.pack_into('<I', d, 28, 1)
    struct.pack_into('<I4s', d, 80, 4, fourcc)
    return bytes(d)                                         # no texel data: size and format only


class AtlasCensus(unittest.TestCase):
    def census(self):
        body = bob1.serialise(atlas_tree())
        with tempfile.TemporaryDirectory() as folder:
            game = Path(folder)
            write_catalogue(game / '01.cat', [
                ('dds/a_diff.pck', gzip.compress(dds_header(256, 256), mtime=0)),
                ('dds/b_diff.pck', gzip.compress(dds_header(128, 128), mtime=0)),
                ('dds/b_light.pck', gzip.compress(dds_header(128, 128, b'DXT5'), mtime=0))])
            write_catalogue(game / '02.cat', [('objects/ships/x/b.pbb', gzip.compress(body, mtime=0))])
            assets, _ = body_materials.original_assets(game)
            tree = bob1.parse(assets.read_entry(bob1.resolve_body(assets, 'ships/x/b')))
            return atlas_census.census(tree, None, assets)

    def test_uv_census_and_shared_points(self):
        c = self.census()
        self.assertEqual((c['lod'], c['point_flags'], c['total_area']), (1, {0x1b: 12}, 302.5))
        self.assertEqual((c['shared_points_materials'], c['shared_points_tiles'], c['shift_points']), (1, 1, 1))
        m0, m1 = c['materials'][0], c['materials'][1]
        self.assertEqual((m0['faces'], m0['in01'], m0['in01e'], m0['tiling'], m0['one_period']), (3, 1, 1, 1, 2))
        self.assertEqual((m1['faces'], m1['in01'], m1['tiling'], m1['one_period']), (3, 1, 0, 2))
        self.assertEqual([round(x, 4) for x in m0['bbox']], [0, 2, 0, 1])
        self.assertEqual([round(x, 4) for x in m1['bbox']], [0, 3.8, 0, 1])
        self.assertAlmostEqual(m0['share'], 162.5 / 302.5)
        self.assertEqual((m0['diffuse']['size'], m0['diffuse']['fmt'], m0['light']['kind']), ((256, 256), 'DXT1', 'null'))
        self.assertEqual((m1['light']['name'], m1['light']['size'], m1['light']['fmt']), ('b_light.tga', (128, 128), 'DXT5'))

    def test_atlas_plan(self):
        c = self.census()
        span = atlas_census.tiles(c)
        self.assertEqual([t['mats'] for t in span], [[0], [1]])
        self.assertEqual([tuple(round(x, 4) for x in t['span']) for t in span], [(2, 1), (1.2, 1)])
        self.assertEqual([t['split'] for t in span], [0, 0])
        self.assertEqual([t['base'] for t in span], [(256, 256), (128, 128)])
        period = atlas_census.tiles(c, 1)
        self.assertEqual([t['split'] for t in period], [1, 1])              # B (tiling) and D (straddles u = 1)
        self.assertEqual([tuple(round(x, 4) for x in t['span']) for t in period], [(1, 1), (1, 1)])
        p = atlas_census.plan(span, 1024)
        self.assertTrue(p['fits_full'])
        self.assertEqual((p['scale'], p['split'], p['faces']), (1.0, 0, 6))
        self.assertEqual([r['content'] for r in p['rows']], [(512, 256), (156, 128)])   # 153.6 -> 4-texel multiple
        self.assertEqual(p['rows'][0]['d_density'], 1.0)
        self.assertIsNone(p['rows'][0]['l_density'])                         # NULL light map: constant
        self.assertAlmostEqual(p['rows'][1]['l_density'], 156 / 153.6, places=4)
        small = atlas_census.plan(span, 256)
        self.assertFalse(small['fits_full'])
        self.assertTrue(0 < small['scale'] < 1)
        self.assertLess(small['rows'][0]['d_density'], 1)
        self.assertTrue(atlas_census.shelf_fits([(100, 50), (100, 50), (56, 50)], 256))
        self.assertFalse(atlas_census.shelf_fits([(200, 200), (100, 100)], 256))
        out = io.StringIO()
        atlas_census.format_census(c, 'x', 50, out=out)
        text = out.getvalue()
        self.assertIn('not float32', text)
        self.assertIn('span   1024x1024: tiles=2 fits_at_full_density=yes scale=1.0000 split_or_clamp_faces=0/6', text)
        self.assertIn('period 1024x1024: tiles=2 fits_at_full_density=yes scale=1.0000 split_or_clamp_faces=2/6', text)


def atlas_tree_pre():
    """atlas_tree with a precomputed coarse part: one 7-int record per point a group uses."""
    tree = atlas_tree()
    part = bob1.lods(tree)[1]['parts'][0]
    part['flags'], part['bounds'] = 0x30000001, list(range(10))
    for g in part['groups']:
        used = dict.fromkeys(i for f in g['faces'] for i in f[:3])
        g['extra'] = [(i, 1000 * g['material'] + i, 0, 65536, 0, 65536, 0) for i in used]
    for m, bump in zip(bob1.materials(tree), (b'm\\a_bump.tga', b'NULL')):
        m['params'].append((b't_BumpTexture', 8, bump))
    return tree


def atlas_textures():
    y, x = np.mgrid[0:16, 0:16]
    rgba = lambda r, g, b, a: np.stack([np.broadcast_to(np.asarray(c), (16, 16)) for c in (r, g, b, a)],
                                       -1).astype(np.uint8)
    a_diff = rgba(8 + 15 * x, 8 + 15 * y, 100, 255)
    b_diff = rgba(200, 16 * x, 16 * y, 255)
    b_light = rgba(10 * y, 10 * y, 0, 16 * x)
    dds = lambda img, fmt: lod_atlas.write_dds(lod_atlas.mip_chain(img.astype(np.float32)), fmt)
    nx, ny = 0.6 * np.sin(x * np.pi / 4), 0.3 * np.cos(y * np.pi / 8)          # swizzled normal map: x in A, y in RGB
    a_bump = rgba((ny + 1) * 127.5, (ny + 1) * 127.5, (ny + 1) * 127.5, (nx + 1) * 127.5)
    return [('dds/a_diff.pck', gzip.compress(lod_atlas.write_dds([a_diff], 'A8R8G8B8'), mtime=0)),
            ('dds/a_bump.pck', gzip.compress(dds(a_bump, 'DXT5'), mtime=0)),
            ('dds/b_diff.pck', gzip.compress(dds(b_diff, 'DXT1'), mtime=0)),
            ('dds/b_light.pck', gzip.compress(dds(b_light, 'DXT5'), mtime=0))]


class LodAtlas(unittest.TestCase):
    def test_dds_codec(self):
        img = (np.arange(8 * 8 * 4).reshape(8, 8, 4) * 7 % 256).astype(np.float32)
        levels = lod_atlas.mip_chain(img)
        self.assertEqual([lv.shape[:2] for lv in levels], [(8, 8), (4, 4), (2, 2), (1, 1)])
        self.assertTrue((levels[1][0, 0] == np.rint(img[:2, :2].reshape(-1, 4).mean(0))).all())
        dds = lod_atlas.write_dds(levels, 'A8R8G8B8')
        self.assertEqual(lod_atlas.dds_format(dds)[:3], (8, 8, 4))
        for k, lv in enumerate(levels):
            self.assertTrue((lod_atlas.decode_dds(dds, k) == lv).all())
        two = np.zeros((4, 4, 4), np.uint8)
        two[:, :2] = (255, 0, 0, 255)
        two[:, 2:] = (0, 0, 255, 17)
        dxt1 = lod_atlas.write_dds([two], 'DXT1')
        self.assertEqual((len(dxt1), lod_atlas.dds_format(dxt1)[3]), (136, 'DXT1'))
        self.assertTrue((lod_atlas.decode_dds(dxt1)[:, :, :3] == two[:, :, :3]).all())
        dxt5 = lod_atlas.decode_dds(lod_atlas.write_dds([two], 'DXT5'))
        self.assertTrue((dxt5 == two).all())                              # colour and two alpha levels exact
        vec = lod_atlas.normalize(np.stack(np.broadcast_arrays(np.linspace(-1, 1, 8)[None, :], 0.5,
                                                               np.ones((8, 8))), -1))
        nlev = lod_atlas.normal_mip_chain(vec)
        self.assertEqual(len(nlev), 4)
        for lv in nlev:                                                    # every level renormalised
            self.assertTrue(np.allclose(np.linalg.norm(lod_atlas.to_normals(lv), axis=-1), 1, atol=0.02))
            self.assertTrue((lv[..., 0] == lv[..., 1]).all() and (lv[..., 1] == lv[..., 2]).all())
        self.assertEqual(tuple(nlev[-1][0, 0]), (185, 185, 185, 128))   # x cancels, y = 0.5 / |(0, .5, 1)|
        grad = np.stack(np.broadcast_arrays(np.arange(64)[None, :] * 4, np.arange(64)[:, None] * 4, 90, 255), -1)
        back = lod_atlas.decode_dds(lod_atlas.write_dds([grad.astype(np.uint8)], 'DXT1')).astype(int)
        self.assertLess(np.abs(back - grad).max(), 12)

    def game(self, folder, extra=()):
        game = Path(folder) / 'game'
        write_catalogue(game / '01.cat', atlas_textures() + list(extra))
        write_catalogue(game / '02.cat', [('objects/ships/x/b.pbb',
                                           gzip.compress(bob1.serialise(atlas_tree_pre()), mtime=0))])
        return game

    def test_layout_escalates_size(self):
        with tempfile.TemporaryDirectory() as folder:
            assets, _ = lod_overlay.original_assets(self.game(folder))
            tree = bob1.parse(bob1.serialise(atlas_tree_pre()))
            coarse, mats = bob1.lods(tree)[1], bob1.materials(tree)
            lay = lod_atlas.plan_layout(coarse, mats, set(), lod_atlas.Textures(assets), 8, (32, 64, 128),
                                        gutter=4)                           # 32 fits only below scale 1
            self.assertEqual([(n, s == 1.0) for n, s, _ in lay['tried']], [(32, False), (64, True)])
            self.assertEqual((lay['size'], [t['mats'] for t in lay['tiles']]), (64, [[0], [1]]))
            self.assertEqual([t['content'] for t in lay['tiles']], [(32, 16), (20, 16)])   # 16 x (2, 1), 16 x (1.2, 1)
            self.assertTrue(lay['ratio_ok'] and lay['min_ratio'] >= 2)
            self.assertEqual(lay['face_keys'][0, 0, 2], (0, 1, 0))             # F: u 1..1.5 shifted by 1

    @unittest.mock.patch.object(lod_overlay, 'running_game', return_value=[])
    def test_atlas_overlay(self, _running):
        with tempfile.TemporaryDirectory() as folder:
            game, out, prev = self.game(folder), Path(folder) / 'out', Path(folder) / 'prev'
            text = run(['--game', str(game), '--out', str(out), '--collapse', 'atlas', '--atlas-size', '64',
                        '--atlas-max-size', '128', '--atlas-preview', str(prev), 'ships/x/b=8'])
            self.assertIn("groups=['atlas:mat2~0:6f']", text)
            self.assertIn('atlas 64x64', text)
            entries = {e['path']: e for e in read_catalogue(out / 'addon/01.cat')}
            self.assertEqual(sorted(entries), ['dds/x3m_lod_b_b5dd13_bump.pck', 'dds/x3m_lod_b_b5dd13_diffuse.pck',
                                               'dds/x3m_lod_b_b5dd13_light.pck', 'objects/ships/x/b.pbb'])
            raw = bytes(v ^ 0x33 for v in (out / 'addon/01.dat').read_bytes())
            member = lambda p: unpack(raw[entries[p]['offset']:entries[p]['offset'] + entries[p]['size']])
            written = bob1.parse(member('objects/ships/x/b.pbb'))
            ladder, wm = bob1.lods(written), bob1.materials(written)
            src = bob1.parse(bob1.serialise(atlas_tree_pre()))
            self.assertEqual(ladder[0], bob1.lods(src)[0])
            (grp,) = ladder[1]['parts'][0]['groups']
            self.assertEqual((len(ladder[1]['points']), grp['material'], len(grp['faces'])), (14, 2, 6))
            self.assertEqual(ladder[1]['points'][12][7:], bob1.lods(src)[1]['points'][1][7:])   # copies keep the rest
            rec = {e[0]: e[1] for e in grp['extra']}
            self.assertEqual((sorted(rec), rec[12], rec[13], rec[2]), (list(range(14)), 1, 1002, 2))
            first_use = list(dict.fromkeys(i for f in grp['faces'] for i in f[:3]))
            self.assertEqual([e[0] for e in grp['extra']], first_use)        # shipped order: faces' first use
            self.assertEqual(first_use, [0, 1, 2, 3, 12, 4, 5, 6, 7, 8, 9, 10, 11, 13])
            self.assertEqual(ladder[2]['parts'], ladder[1]['parts'])
            p = {n: v for n, t, v in wm[2]['params']}
            self.assertEqual((wm[2]['index'], p[b't_DiffuseTexture'], p[b't_LightMapTexture'], p[b't_BumpTexture']),
                             (2, b'x3m_lod\\x3m_lod_b_b5dd13_diffuse.tga', b'x3m_lod\\x3m_lod_b_b5dd13_light.tga',
                              b'x3m_lod\\x3m_lod_b_b5dd13_bump.tga'))
            self.assertEqual(wm[:2], bob1.materials(src))
            for path, fmt in (('dds/x3m_lod_b_b5dd13_diffuse.pck', 'DXT1'), ('dds/x3m_lod_b_b5dd13_light.pck', 'DXT5'),
                              ('dds/x3m_lod_b_b5dd13_bump.pck', 'DXT5')):
                dds = member(path)
                self.assertEqual(lod_atlas.dds_format(dds), (64, 64, 7, fmt))
                self.assertEqual(Assets(out).logical(path[:-4], ('.pck', '.dds', '.tga'))[0], dds)
            (body,) = json.loads((out / 'addon/01.x3m-lod.json').read_text())['bodies']
            c = body['atlas']['check']
            self.assertEqual((c['uv_inside_content'], c['vertices'], body['atlas']['duplicated_points']), (18, 18, 2))
            self.assertLess(c['max_map_error_texels'], 0.01)
            self.assertLess(c['slots']['diffuse']['box_rgb'][0], 4)          # layout-exact up to DXT and filtering
            self.assertLess(c['slots']['light']['box_a'][0], 4)
            self.assertLess(c['slots']['bump']['box_angle'][2], 3)           # degrees, max over faces
            bump = lod_atlas.decode_dds(member('dds/x3m_lod_b_b5dd13_bump.pck'))
            self.assertEqual(tuple(bump[0, 63][1:]), (130, 132, 128))         # unused: flat normal (G, A = 128
                                                                              # up to RGB565 quantisation)
            text = run(['--game', str(game), '--dry-run', '--collapse', 'atlas', '--atlas-size', '64',
                        '--no-atlas-bump', 'ships/x/b=8'])
            self.assertNotIn('x3m_lod_b_b5dd13_bump', text)
            self.assertEqual(body['synth'][0]['atlas'], True)
            self.assertEqual(sorted(x.name for x in prev.iterdir()),
                             ['atlas_preview_b_bump.png', 'atlas_preview_b_diffuse.png', 'atlas_preview_b_light.png'])
            self.assertEqual((prev / 'atlas_preview_b_light.png').read_bytes()[:4], b'\x89PNG')
            text = run(['--game', str(game), '--dry-run', '--collapse', 'atlas', '--atlas-size', '64',
                        '--atlas-format', 'a8r8g8b8', 'ships/x/b=8'])
            self.assertIn('A8R8G8B8 21972 bytes', text)                      # 4 x 5461 texels (7 mips) + 128
        with tempfile.TemporaryDirectory() as folder:                      # an existing name would be shadowed
            game = self.game(folder, [('dds/x3m_lod_b_b5dd13_light.pck', b'x')])
            with self.assertRaises(SystemExit):
                run(['--game', str(game), '--dry-run', '--collapse', 'atlas', 'ships/x/b=8'])

    def test_atlas_refusals(self):
        tree = bob1.parse(bob1.serialise(atlas_tree_pre()))
        mats, coarse = bob1.materials(tree), bob1.lods(tree)[1]
        p = coarse['points'][6]
        coarse['points'][6] = p[:6] + (p[4], p[5]) + p[6:]
        coarse['points'][6] = (0x1f,) + coarse['points'][6][1:]           # second UV set
        other = [dict(m) for m in mats]
        other[1] = dict(other[1], effect=b'other.fx')
        with tempfile.TemporaryDirectory() as folder:
            assets, _ = lod_overlay.original_assets(self.game(folder))
            res = lod_atlas.collapse(assets, 'b', list(mats), coarse, set(), 8, (64,))   # second UV set: passed through
            self.assertEqual(res['record']['points'][6][6:8], coarse['points'][6][6:8])
            self.assertEqual((res['uv2'], res['occlusion']), (1, {'argon.fx': 'none'}))
            occl = [dict(m, params=m['params'] + [(b't_OcclusionTexture', 8, t)])
                    for m, t in zip(mats, (b'a_decal.tga', b'NONE_OCCL_DECAL.dds'))]
            with self.assertRaisesRegex(lod_atlas.AtlasError, 'occlusion textures'):
                lod_atlas.collapse(assets, 'b', list(occl), coarse, set(), 8, (64,))
            plain = bob1.lods(bob1.parse(bob1.serialise(atlas_tree_pre())))[1]
            two = list(other)
            res = lod_atlas.collapse(assets, 'b', two, plain, set(), 8, (64,))       # mixed effects: one material each
            self.assertEqual((res['atlas_indices'], res['atlas_of']), ([2, 3], {0: 2, 1: 3}))
            self.assertEqual([(g['material'], len(g['faces'])) for g in res['record']['parts'][0]['groups']],
                             [(2, 3), (3, 3)])
            self.assertEqual([m['effect'] for m in two[2:]], [b'argon.fx', b'other.fx'])
            neg = bob1.lods(bob1.parse(bob1.serialise(atlas_tree_pre())))[1]
            neg['parts'][0]['groups'][1]['material'] = -79
            with self.assertRaisesRegex(lod_atlas.AtlasError, 'outside the material table'):
                lod_atlas.collapse(assets, 'b', list(mats), neg, set(), 8, (64,))

    def test_merged_records_first_use_order(self):
        rec = lambda i: (i, i, 0, 65536, 0, 65536, 0)
        g1 = {'material': 0, 'faces': [(2, 0, 1, 1)], 'extra': [rec(0), rec(1), rec(2)]}      # stored out of order
        g2 = {'material': 1, 'faces': [(3, 1, 4, 1)], 'extra': [rec(4), rec(1), rec(3), rec(9)]}  # 1 shared, 9 unused
        m = lod_overlay.merged_group([g1, g2], True)
        self.assertEqual([e[0] for e in m['extra']], [2, 0, 1, 3, 4, 9])


def atlas_tree_lod0():
    """atlas_tree_pre with a fine record 0: the coarse points at twice the size plus a 3-point
    face, part 0 (precomputed) with the coarse groups, part 1 flagged 0x8000 (hidden, like the
    box of argon_TL / M1 record 0) with a classic non-effect material 2 on the new face."""
    tree = atlas_tree_pre()
    mats, (lod0, coarse) = bob1.materials(tree), bob1.lods(tree)
    mats.append({'index': 2, 'flags': 0, 'texture': b'', 'colors': [0, 0, 0, 255] + [255] * 5 + [0] * 3,
                 'w24': 10, 'w26': 0, 'w2c': 0, 'maps': [(b'', 0)] * 3, 'extra': [(b'', 0)] * 2})
    q = lambda x: int(round(x * 65536))
    pts = [(p[0], 2 * p[1], 2 * p[2], 2 * p[3]) + p[4:] for p in coarse['points']]
    pts += [(0x1b, 90, 0, 0, 0, 0, 0, 0, 65536, 1), (0x1b, 99, 0, 0, q(1), 0, 0, 0, 65536, 1),
            (0x1b, 90, 9, 0, 0, q(1), 0, 0, 65536, 1)]
    rec = lambda i, m: (i, 5000 + 1000 * m + i, 0, 65536, 0, 65536, 0)
    part0 = {'flags': 0x30000001, 'bounds': list(range(10)), 'groups': []}
    for g in coarse['parts'][0]['groups']:
        used = dict.fromkeys(i for f in g['faces'] for i in f[:3])
        part0['groups'].append({'material': g['material'], 'faces': list(g['faces']),
                                'extra': [rec(i, g['material']) for i in reversed(used)]})   # stored out of order
    part1 = {'flags': 0x30008001, 'bounds': list(range(10, 20)),
             'groups': [{'material': 2, 'faces': [(13, 12, 14, 1)], 'extra': [rec(i, 2) for i in (12, 13, 14)]}]}
    lod0.clear()
    lod0.update({'value': 100, 'flags': 0, 'points': pts, 'parts': [part0, part1]})
    return tree


class SourceRecord(unittest.TestCase):
    def game(self, folder):
        game = Path(folder) / 'game'
        write_catalogue(game / '01.cat', atlas_textures())
        write_catalogue(game / '02.cat', [('objects/ships/x/b.pbb',
                                           gzip.compress(bob1.serialise(atlas_tree_lod0()), mtime=0))])
        return game

    def written(self, out):
        entries = {e['path']: e for e in read_catalogue(out / 'addon/01.cat')}
        raw = bytes(v ^ 0x33 for v in (out / 'addon/01.dat').read_bytes())
        e = entries['objects/ships/x/b.pbb']
        tree = bob1.parse(unpack(raw[e['offset']:e['offset'] + e['size']]))
        manifest = json.loads((out / 'addon/01.x3m-lod.json').read_text())
        return bob1.lods(tree), bob1.materials(tree), manifest['bodies'][0]

    @unittest.mock.patch.object(lod_overlay, 'running_game', return_value=[])
    def test_atlas_from_record_0(self, _running):
        src = bob1.lods(bob1.parse(bob1.serialise(atlas_tree_lod0())))
        strip = lambda pt: pt[:4] + pt[6:]                                  # position etc., UV dropped
        with tempfile.TemporaryDirectory() as folder:
            game, out = self.game(folder), Path(folder) / 'out'
            text = run(['--game', str(game), '--out', str(out), '--collapse', 'atlas', '--atlas-size', '64',
                        '--atlas-max-size', '128', '--source-record', '0', 'ships/x/b=8'])
            self.assertIn('from source LOD0 (points 15, faces 7)', text)
            ladder, mats, body = self.written(out)
            self.assertEqual((body['source_record'], body['new_lod'], body['pad_lod']), (0, 1, 2))
            self.assertEqual([l['value'] for l in ladder], [100, 5, 8])
            self.assertEqual(ladder[0], src[0])
            c = ladder[1]
            self.assertEqual((len(c['points']), body['atlas']['points']), (17, 17))   # 15 + 2 duplicated
            for cp, sp in zip(c['parts'], src[0]['parts']):                 # record 0 geometry, UVs aside
                self.assertEqual((cp['flags'], cp['bounds']), (sp['flags'], sp['bounds']))
                face = lambda pts, f: tuple(strip(pts[i]) for i in f[:3]) + (f[3],)
                self.assertEqual(sorted(face(c['points'], f) for g in cp['groups'] for f in g['faces']),
                                 sorted(face(src[0]['points'], f) for g in sp['groups'] for f in g['faces']))
            self.assertEqual(c['points'][12:15], src[0]['points'][12:15])   # hidden part: UVs untouched
            (atlas,), (hidden,) = c['parts'][0]['groups'], c['parts'][1]['groups']
            self.assertEqual((atlas['material'], len(atlas['faces'])), (3, 6))        # one atlas group, part 0
            self.assertEqual((hidden['material'], hidden['faces']), (2, [(13, 12, 14, 1)]))   # copied
            for g in (atlas, hidden):                                       # tangent records: first-use order
                self.assertEqual([e[0] for e in g['extra']],
                                 list(dict.fromkeys(i for f in g['faces'] for i in f[:3])))
            self.assertEqual([e[1] for e in hidden['extra']], [7013, 7012, 7014])
            by_pos = {strip(p): i for i, p in enumerate(src[0]['points'])}
            for e in atlas['extra']:                                        # each copy keeps its point's record
                self.assertEqual(e[1] % 1000, by_pos[strip(c['points'][e[0]])])
            self.assertEqual(ladder[2], dict(src[1], value=8))            # pad: the original coarsest record
            self.assertEqual(body['pad_source'], 1)                         # (collision geometry unchanged)
            self.assertTrue(all(g['material'] < len(mats) for p in ladder[2]['parts'] for g in p['groups']))
            self.assertEqual((len(mats), mats[3]['index']), (4, 3))
            self.assertEqual([t['mats'] for t in body['atlas']['tiles']], [[0], [1]])   # material 2 not atlased

    @unittest.mock.patch.object(lod_overlay, 'running_game', return_value=[])
    def test_other_collapses_and_refusals(self, _running):
        src = bob1.lods(bob1.parse(bob1.serialise(atlas_tree_lod0())))
        with tempfile.TemporaryDirectory() as folder:
            game, out = self.game(folder), Path(folder) / 'out'
            run(['--game', str(game), '--out', str(out), '--collapse', 'two', '--source-record', '0', 'ships/x/b=8'])
            ladder, _, body = self.written(out)
            c = ladder[1]
            self.assertEqual((body['source_record'], c['points'], c['flags']), (0, src[0]['points'], 0))
            self.assertEqual([len(p['groups']) for p in c['parts']], [1, 1])
            (merged,) = c['parts'][0]['groups']
            self.assertEqual([e[0] for e in merged['extra']],
                             list(dict.fromkeys(i for f in merged['faces'] for i in f[:3])))
            self.assertEqual(ladder[2], dict(src[1], value=8))
            text = run(['--game', str(game), '--dry-run', '--collapse', 'two', 'ships/x/b=8'])
            self.assertIn('from source LOD1 (points 12, faces 6)', text)       # default: the coarsest record
            self.assertIn('pad copy LOD2', text)                               # source = coarsest: pad copies C
            for arg in ('ships/x/b=8@0', 'ships/x/b=8,0'):                    # per-body source record
                text = run(['--game', str(game), '--dry-run', '--collapse', 'two', '--source-record', '1', arg])
                self.assertIn('from source LOD0 (points 15, faces 7)', text)
                self.assertIn('pad copy of original LOD1 (12 points) LOD2', text)
            with self.assertRaisesRegex(SystemExit, 'integer threshold and source record'):
                run(['--game', str(game), '--dry-run', 'ships/x/b=8@x'])
            with self.assertRaisesRegex(SystemExit, "outside the body's records 0..1"):
                run(['--game', str(game), '--dry-run', '--source-record', '2', 'ships/x/b=8'])
            with unittest.mock.patch.object(lod_overlay, 'MAX_POINTS', 8), \
                    self.assertRaisesRegex(SystemExit, 'references 12 points, above 8'):
                run(['--game', str(game), '--dry-run', '--collapse', 'two', 'ships/x/b=8@0'])

    def test_split_faces_packing(self):
        a = [(0, 1, 2, 1), (1, 2, 3, 1), (2, 3, 4, 1)]                    # 5 points
        b = [(5, 6, 7, 1), (6, 7, 8, 1)]                                   # 4 points
        c = [(9, 10, 11, 1)]                                               # 3 points
        faces, blocks = a + b + c, [0] * 3 + [1] * 2 + [2]
        self.assertEqual(lod_atlas.split_faces(faces, 12, blocks), [faces])           # within the limit
        self.assertEqual(lod_atlas.split_faces(faces, 8, blocks), [a + c, b])          # first fit, decreasing
        self.assertEqual(lod_atlas.split_faces(faces, 9, blocks), [a + b, c])
        order = [c[0], b[0], a[0], b[1], a[1], a[2]]                        # face order kept inside a chunk
        got = lod_atlas.split_faces(order, 8, [2, 1, 0, 1, 0, 0])
        self.assertEqual(sorted(map(sorted, got)), sorted(map(sorted, [a + c, b])))
        self.assertEqual(got[0], [c[0], a[0], a[1], a[2]])
        self.assertEqual(lod_atlas.split_faces(a, 4, [0, 0, 0]), [a[:2], a[2:]])       # oversized block: face order
        self.assertEqual(lod_atlas.split_faces(a, 4), [a[:2], a[2:]])                  # no blocks: greedy
        for chunk in lod_atlas.split_faces(faces, 4, blocks):
            self.assertLessEqual(len({i for f in chunk for i in f[:3]}), 4)

    @unittest.mock.patch.object(lod_overlay, 'running_game', return_value=[])
    def test_pad_and_append_pad_placements(self, _running):
        src = bob1.lods(bob1.parse(bob1.serialise(atlas_tree_lod0())))
        for placement, arg, values in (('pad', 'ships/x/b=8@0', [100, 5, 5, 8]),
                                       ('append-pad', 'ships/x/b=3@0', [100, 5, 3, 2])):
            with self.subTest(placement), tempfile.TemporaryDirectory() as folder:
                game, out = self.game(folder), Path(folder) / 'out'
                run(['--game', str(game), '--out', str(out), '--collapse', 'two', '--placement', placement, arg])
                ladder, _, body = self.written(out)
                self.assertEqual([l['value'] for l in ladder], values)
                self.assertEqual((body['new_lod'], body['pad_lod'], body['source_record'], body['pad_source']),
                                 (2, 3, 0, 1))
                self.assertEqual(ladder[:2], src)                                # originals kept
                self.assertEqual(ladder[2]['points'], src[0]['points'])          # C from record 0
                self.assertEqual(ladder[3], dict(src[1], value=values[3]))       # pad: original coarsest

    @unittest.mock.patch.object(lod_overlay, 'running_game', return_value=[])
    def test_atlas_group_split(self, _running):
        with tempfile.TemporaryDirectory() as folder:
            game, out = self.game(folder), Path(folder) / 'out'
            with unittest.mock.patch.object(lod_atlas, 'MAX_GROUP_POINTS', 3):
                text = run(['--game', str(game), '--out', str(out), '--collapse', 'atlas', '--atlas-size', '64',
                            'ships/x/b=8@0'])
            ladder, mats, body = self.written(out)
            groups = ladder[1]['parts'][0]['groups']
            self.assertTrue(len(groups) >= 2 and all(g['material'] == 3 for g in groups))   # one atlas material
            used = [set(i for f in g['faces'] for i in f[:3]) for g in groups]
            self.assertTrue(all(len(u) <= 3 for u in used))
            self.assertFalse(any(used[i] & used[j] for i in range(len(used)) for j in range(i)))   # no shared point
            self.assertEqual(sum(len(g['faces']) for g in groups), 6)
            (split,) = body['atlas']['split_groups']
            self.assertEqual((split['part'], split['material'], split['faces'], split['points']),
                             (0, 3, 6, [len(u) for u in used]))
            self.assertIn(f'-> {len(groups)} groups of {split["points"]} points', text)
            for g in groups:                                                  # records in first-use order
                self.assertEqual([e[0] for e in g['extra']], list(dict.fromkeys(i for f in g['faces'] for i in f[:3])))
            copies = sorted(j for u in used[1:] for j in u if j >= 17)        # 17 = points before the split
            self.assertTrue(copies)
            self.assertEqual(len(ladder[1]['points']), 17 + len(copies))
            rec = {e[0]: e[1:] for g in groups for e in g['extra']}
            for dup in copies:                                                # duplicated with its tangent record
                twins = [j for j in range(17) if ladder[1]['points'][j] == ladder[1]['points'][dup] and j in rec]
                self.assertTrue(any(rec[j] == rec[dup] for j in twins))
            self.assertEqual(ladder[2], dict(bob1.lods(bob1.parse(bob1.serialise(atlas_tree_lod0())))[1], value=8))


class TileAwareMips(unittest.TestCase):
    def test_bright_tile_never_bleeds(self):
        """A bright tile (A) beside a black one (B), B's gutter not aligned to the coarse texels:
        at every level every texel outside A's footprint stays black, B's content stays black
        wherever it shares no texel with A's content, and A's content stays bright."""
        class Tex:
            def get(self, name):
                return {b'bright': np.full((8, 8, 4), 255, np.uint8), b'black': np.zeros((8, 8, 4), np.uint8)}[name]
        tile = lambda x, w, name: dict(origin=(x, 8), content=(w, 16), lo=(0.0, 0.0), span=(1.0, 1.0),
                                       names={'light': name})
        layout = dict(size=128, gutter=8, slots=('light',), tiles=[tile(8, 20, b'bright'), tile(44, 16, b'black')])
        levels = lod_atlas.to_levels('light', lod_atlas.bake(layout, Tex())['light'])
        self.assertEqual([lv.shape[0] for lv in levels], [128, 64, 32, 16, 8, 4, 2, 1])
        box = lod_atlas.mip_chain(lod_atlas.bake(layout, Tex(), levels=1)['light'][0])
        for level, lv in enumerate(levels):
            fp, a_in = lod_atlas.tile_boxes(layout['tiles'][0], 8, level)
            _, b_in = lod_atlas.tile_boxes(layout['tiles'][1], 8, level)
            outside = np.ones(lv.shape[:2], bool)
            outside[fp[2]:fp[3], fp[0]:fp[1]] = False
            self.assertEqual(int(lv[outside].max(initial=0)), 0, f'level {level}: light outside the bright tile')
            if a_in[1] <= b_in[0]:                                         # contents share no texel (levels 0-5)
                self.assertTrue((lv[a_in[2]:a_in[3], a_in[0]:a_in[1]] == 255).all(), f'level {level}')
                self.assertEqual(int(lv[b_in[2]:b_in[3], b_in[0]:b_in[1]].max()), 0, f'level {level}')
        _, b_in = lod_atlas.tile_boxes(layout['tiles'][1], 8, 4)
        self.assertGreater(int(box[4][b_in[2]:b_in[3], b_in[0]:b_in[1]].max()), 0)   # a whole-atlas box chain bleeds
        self.assertEqual(lod_atlas.GUTTER, 8)


@unittest.skipUnless(bob1.DEFAULT_GAME.joinpath('X3AP.exe').is_file(), 'X3 bottle not present')
class Installed(unittest.TestCase):
    def test_first_20_bodies_round_trip(self):
        assets = Assets(bob1.DEFAULT_GAME)
        keys = sorted(k for k, v in assets.entries.items() if v[-1]['path'].lower().endswith('.pbb'))
        done = 0
        for key in keys:
            data = assets.read_entry(assets.entries[key][-1])
            if bob1.kind(data) != 'BOB1':
                continue
            with self.subTest(key):
                self.assertEqual(bob1.serialise(bob1.parse(data)), data)
            done += 1
            if done == 20:
                break
        self.assertEqual(done, 20)

    def test_text_twins_structurally_equal(self):
        """Vanilla stems shipped as a .pbd (01.cat) and a dbox2-compiled .pbb (addon/01.cat): the text
        parse equals the game's binary in every record's threshold, flags, points (normals within 4
        units of 16.16), faces and group materials, and in the material table where the compiler did
        not add parameters (verification/results/lod-overlay-batch/text-bodies/text_pairs_out.txt)."""
        assets = Assets(bob1.DEFAULT_GAME)
        for stem, same_materials in (('v/00390', True), ('v/00773', True), ('v/11994', True), ('v/11999', True),
                                     ('v/12000', False), ('v/11014', False)):
            with self.subTest(stem):
                (text,) = assets.candidates(f'objects/{stem}.pbd')
                tree = bob1.parse(assets.read_entry(text))
                ref = bob1.parse(assets.read_entry(assets.candidates(f'objects/{stem}.pbb')[-1]))
                self.assertEqual(len(bob1.lods(tree)), len(bob1.lods(ref)))
                for x, y in zip(bob1.lods(tree), bob1.lods(ref)):
                    self.assertEqual((x['value'], x['flags'], len(x['points'])), (y['value'], y['flags'], len(y['points'])))
                    for p, q in zip(x['points'], y['points']):
                        self.assertEqual(p[:6] + p[9:], q[:6] + q[9:])
                        self.assertLessEqual(max(abs(a - b) for a, b in zip(p[6:9], q[6:9])), 4)
                    self.assertEqual([[(g['material'], g['faces']) for g in p['groups']] for p in x['parts']],
                                     [[(g['material'], g['faces']) for g in p['groups']] for p in y['parts']])
                if same_materials:
                    self.assertEqual(bob1.materials(tree), bob1.materials(ref))

    def test_overlay_on_installed_body(self):
        if sorted((bob1.DEFAULT_GAME / 'addon').glob('*' + lod_overlay.MARKER_SUFFIX)):
            self.skipTest('an x3m-lod overlay is installed in the game directory (pilot flight); '
                          'the tool refuses a second overlay by design')
        originals = lod_overlay.original_archives(bob1.DEFAULT_GAME)
        before = lod_overlay.hash_files(originals)
        with tempfile.TemporaryDirectory() as folder:
            out = Path(folder)
            # the dock's 30/10/3/30 ladder takes before-last at T = 30 (default) and append-pad
            text = run(['--dry-run', '--placement', 'before-last', 'stations/docks/argon_dock_center'])
            self.assertIn('before-last: new LOD4 threshold=30', text)
            text = run(['--dry-run', '--placement', 'pad', 'stations/docks/argon_dock_center=40'])
            self.assertIn('very-high drawable [0, 5] by s: before s<30:LOD3 s>=30:LOD0 | after s<40:LOD5 s>=40:LOD0',
                          text)
            text = run(['--dry-run', 'stations/docks/argon_dock_center=40'])      # compact (default)
            self.assertIn('very-high drawable [0, 1] by s: before s<30:LOD3 s>=30:LOD0 | after s<40:LOD1 s>=40:LOD0',
                          text)
            run(['--out', str(out), '--placement', 'append-pad', '--threshold', '20',
                 'stations/docks/argon_dock_center'])
            cats = sorted(out.rglob('*.cat'))
            self.assertEqual(len(cats), 1)
            (entry,) = read_catalogue(cats[0])
            stored = bytes(v ^ 0x33 for v in cats[0].with_suffix('.dat').read_bytes())
            ladder = bob1.lods(bob1.parse(unpack(stored)))
            source = bob1.lods(bob1.parse(Assets(bob1.DEFAULT_GAME).get(entry['path'])[0]))
            self.assertEqual(ladder[:len(source)], source)
            self.assertEqual([l['value'] for l in ladder[len(source):]], [20, 19])
            self.assertTrue(all(len(p['groups']) == 1 for p in ladder[-2]['parts']))
            self.assertIn(len(source), bob1.drawable([l['value'] for l in ladder[1:]], 'very-high'))
        self.assertEqual(lod_overlay.hash_files(originals), before)


if __name__ == '__main__':
    unittest.main()
