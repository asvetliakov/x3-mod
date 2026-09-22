"""BOB1 reader/writer and LOD overlay builder (tools/analysis/bob1.py, lod_overlay.py).

Synthetic bodies are built here; no game bytes are stored. The installed-body
checks run only when the X3 bottle is present and never write into it.
"""
import contextlib
import gzip
import io
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import unittest.mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'analysis'))
import bob1
import lod_overlay
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
        self.assertEqual(len(new['parts'][0]['groups'][0]['extra']), 3 + 4)
        self.assertEqual(new['parts'][0]['bounds'], coarse['parts'][0]['bounds'])
        self.assertNotIn('extra', new['parts'][1]['groups'][0])

    def test_collapse_two(self):
        def mat(alpha):
            return {'index': 99, 'flags': 0x02000000, 'technique': 1, 'effect': b'argon.fx',
                    'params': [(b'g_AlphaValue', 2, [65536]), (b't_AlphaTexture', 8, alpha)]}
        mats = [mat(b'NULL'), mat(b'metals\\argon\\grid_alpha.tga'), mat(b'C:\\Maps\\NONE_WHITE.dds'),
                {'index': 3, 'flags': 0x10, 'texture': b'plain.tga'}, mat(b'')]
        self.assertEqual(lod_overlay.alpha_materials(mats), {1})   # positions; NULL/NONE_*/empty/classic opaque
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

    def test_pad(self):
        for ladder in ([(9000, 4)], [(9000, 4), (30, 2)]):
            self.assertEqual(lod_overlay.default_placement(bob1.lods(ladder_tree(ladder))), 'pad')
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
            for argv in (['stations/test/body'], ['stations/test/body=250'], ['--threshold', '200', 'stations/test/body'],
                         ['stations/test/body=x']):                       # pad: T_pad required and > 250
                with self.subTest(argv=argv), self.assertRaises(SystemExit):
                    run(base + argv)
            text = run(base + ['--threshold', '200', '--force-threshold', 'stations/test/body'])
            self.assertIn('pad: new LOD2 threshold=250 + pad copy LOD3 threshold=200', text)
            text = run(base + ['--threshold', '999', 'stations/test/body=300'])  # NAME=T wins
            self.assertIn("collapse=two groups=['opaque:mat0:5f', 'opaque:mat0:4f']", text)
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

    def test_overlay_files(self):
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
            self.assertEqual([l['value'] for l in ladder], [12345, 250, 250, 300])
            self.assertEqual([len(p['groups']) for p in ladder[2]['parts']], [1, 1])
            self.assertEqual(ladder[2]['parts'], ladder[3]['parts'])
            self.assertEqual(ladder[:2], source)
            import json
            (body_rec,) = json.loads((out / 'addon/03.x3m-lod.json').read_text())['bodies']
            self.assertEqual((body_rec['placement'], body_rec['new_lod'], body_rec['pad_lod'],
                              body_rec['threshold'], body_rec['pad_threshold']), ('pad', 2, 3, 250, 300))
            self.assertEqual(lod_overlay.hash_files(originals), before)

            # --install targets the game directory; the new slot wins under the resolver's precedence
            run(['--game', str(game), '--install', 'stations/test/body=300'])
            self.assertEqual(lod_overlay.hash_files(originals), before)
            data, src = Assets(game).get('objects/stations/test/body.pbb')
            self.assertEqual(src['source'], 'addon/03.cat')
            self.assertEqual(len(bob1.lods(bob1.parse(data))), 4)
            with self.assertRaises(SystemExit):          # installed marker: refuse to stack
                run(['--game', str(game), '--out', str(Path(folder) / 'out2'), 'stations/test/body=300'])


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
            text = run(['--dry-run', 'stations/docks/argon_dock_center=40'])      # pad (default)
            self.assertIn('very-high drawable [0, 5] by s: before s<30:LOD3 s>=30:LOD0 | after s<40:LOD5 s>=40:LOD0',
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
