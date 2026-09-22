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


class Audit(unittest.TestCase):
    @staticmethod
    def tree(ladder):
        """ladder: [(threshold, groups in the one part)]; LOD 0's value is the scale."""
        return {'sections': [('BODY', [lod(t, 0, 4, [(0x30000001, [(0, 1)] * n)]) for t, n in ladder])]}

    def test_classes(self):
        cases = {
            'single': ([(9000, 3)], dict(single_lod=True, non_monotonic=False, coarse_multi_group=True, shadowed=[])),
            'clean': ([(9000, 4), (250, 2), (30, 1)],
                      dict(single_lod=False, non_monotonic=False, coarse_multi_group=False, shadowed=[])),
            'dock-like': ([(9000, 18), (30, 12), (10, 5), (3, 1), (30, 1)],
                          dict(single_lod=False, non_monotonic=True, coarse_multi_group=False, shadowed=[1, 2, 3])),
            'equal middle': ([(9000, 5), (100, 3), (50, 3), (50, 2)],
                             dict(single_lod=False, non_monotonic=True, coarse_multi_group=True, shadowed=[2])),
        }
        for name, (ladder, expect) in cases.items():
            with self.subTest(name):
                row = bob1.audit_row(bob1.parse(bob1.serialise(self.tree(ladder))))
                self.assertEqual({k: row[k] for k in expect}, expect)
                self.assertEqual(row['groups'], [n for _, n in ladder])

    def test_audit_over_archive(self):
        bodies = {'a': [(9000, 3)], 'b': [(9000, 18), (30, 12), (3, 1), (30, 1)], 'c': [(9000, 4), (20, 2)]}
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            members = [(f'objects/t/{k}.pbb', gzip.compress(bob1.serialise(self.tree(v)), mtime=0))
                       for k, v in bodies.items()]
            members.append(('objects/t/scene.pbb', gzip.compress(b'CUT1' + b'\0' * 8, mtime=0)))
            write_catalogue(root / '01.cat', members)
            rows, summary = bob1.audit(Assets(root))
        self.assertEqual(len(rows), 3)
        self.assertEqual({k: summary[k] for k in ('cut1', 'bob1', 'single_lod', 'non_monotonic',
                                                   'coarse_multi_group', 'shadowed_records', 'clean_multi_lod')},
                         dict(cut1=1, bob1=3, single_lod=1, non_monotonic=1, coarse_multi_group=2,
                              shadowed_records=2, clean_multi_lod=0))


class Overlay(unittest.TestCase):
    def test_collapse_and_threshold(self):
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
        self.assertEqual(lod_overlay.default_threshold(bob1.lods(tree)), 125)
        self.assertIsNone(lod_overlay.default_threshold(bob1.lods(tree)[:1]))

    def test_slot_hidden_and_errors(self):
        body = body_bytes()
        with tempfile.TemporaryDirectory() as folder:
            game = game_dir(Path(folder) / 'game', body)
            base = ['--game', str(game), '--dry-run']
            for slot in ('0', '-1', '100', '2', '4'):          # range, existing, non-contiguous
                with self.subTest(slot=slot), self.assertRaises(SystemExit):
                    run(base + ['--slot', slot, 'stations/test/body'])
            self.assertIn('target addon/03.cat', run(base + ['--slot', '3', 'stations/test/body']))
            self.assertIn('target addon/04.cat', run(base + ['--slot', '4', '--force-slot', 'stations/test/body']))
            with self.assertRaises(SystemExit):
                run(base + ['--slot', '2', '--force-slot', 'stations/test/body'])
            text = run(base + ['--keep-coarsest-hidden', 'stations/test/body'])
            self.assertIn('new LOD2: threshold=250', text)
            with self.assertRaises(SystemExit):
                run(base + ['--keep-coarsest-hidden', '--threshold', '5', 'stations/test/body'])
            write_catalogue(game / 'addon/03.cat', [('objects/stations/test/cut.pbb', gzip.compress(body[:-8]))])
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
        with tempfile.TemporaryDirectory() as folder:
            game = game_dir(Path(folder) / 'game', body)
            out = Path(folder) / 'out'
            originals = lod_overlay.original_archives(game)
            before = lod_overlay.hash_files(originals)
            text = run(['--game', str(game), '--dry-run', 'stations\\test\\body'])
            self.assertIn('new LOD2: threshold=125', text)
            self.assertIn('target addon/03.cat', text)
            self.assertFalse(out.exists())
            with self.assertRaises(SystemExit):
                run(['--game', str(game), '--out', str(game / 'sub'), 'stations/test/body'])
            with self.assertRaises(SystemExit):          # threshold must stay below the coarsest
                run(['--game', str(game), '--out', str(out), '--threshold', '250', 'stations/test/body'])
            run(['--game', str(game), '--out', str(out), '--threshold', '100', 'stations/test/body'])
            self.assertEqual(sorted(p.name for p in out.rglob('*') if p.is_file()),
                             ['03.cat', '03.dat', '03.x3m-lod.json'])
            (entry,) = read_catalogue(out / 'addon/03.cat')
            self.assertEqual(entry['path'], 'objects/stations/test/Body.pbb')
            stored = bytes(v ^ 0x33 for v in (out / 'addon/03.dat').read_bytes())
            tree = bob1.parse(unpack(stored))
            ladder = bob1.lods(tree)
            self.assertEqual(len(ladder), 3)
            self.assertEqual(ladder[-1]['value'], 100)
            self.assertEqual([len(p['groups']) for p in ladder[-1]['parts']], [1, 1])
            self.assertEqual(ladder[:2], bob1.lods(bob1.parse(body)))
            self.assertEqual(lod_overlay.hash_files(originals), before)

            # --install targets the game directory; the new slot wins under the Assets precedence
            run(['--game', str(game), '--install', '--threshold', '100', 'stations/test/body'])
            self.assertEqual(lod_overlay.hash_files(originals), before)
            data, source = Assets(game).get('objects/stations/test/body.pbb')
            self.assertEqual(source['source'], 'addon/03.cat')
            self.assertEqual(len(bob1.lods(bob1.parse(data))), 3)
            with self.assertRaises(SystemExit):          # installed marker: refuse to stack
                run(['--game', str(game), '--out', str(Path(folder) / 'out2'), 'stations/test/body'])


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
        originals = lod_overlay.original_archives(bob1.DEFAULT_GAME)
        before = lod_overlay.hash_files(originals)
        with tempfile.TemporaryDirectory() as folder:
            out = Path(folder)
            run(['--out', str(out), '--threshold', '2', 'stations/docks/argon_dock_center'])
            cats = sorted(out.rglob('*.cat'))
            self.assertEqual(len(cats), 1)
            (entry,) = read_catalogue(cats[0])
            stored = bytes(v ^ 0x33 for v in cats[0].with_suffix('.dat').read_bytes())
            ladder = bob1.lods(bob1.parse(unpack(stored)))
            source = bob1.lods(bob1.parse(Assets(bob1.DEFAULT_GAME).get(entry['path'])[0]))
            self.assertEqual(len(ladder), len(source) + 1)
            self.assertEqual(ladder[-1]['value'], 2)
            self.assertTrue(all(len(p['groups']) == 1 for p in ladder[-1]['parts']))
        self.assertEqual(lod_overlay.hash_files(originals), before)


if __name__ == '__main__':
    unittest.main()
