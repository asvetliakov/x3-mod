"""Per-body baker recipes (tools/analysis/lod_recipes.py; docs/architecture/lattice-baker-fix.md) on a
synthetic louvre body: tilted strips over box girders. weld_strips flattens every strip to y = plane_y,
abutting its neighbours on bit-identical edge coordinates, keeps faces, normals, UVs, flags and tangent
records, removes the poke-through, and survives serialise/parse; the expect block refuses other geometry;
the batch censuses and bakes a matching body from the recipe's source record with the recipe digest in
inputs_sha256 (plain bodies keep their hash) and bakes a mismatching one plainly with recipe_skipped. The
last class checks the shipped recipe on the real terran_spp_panel when the X3 bottle is present."""
import contextlib
import copy
import gzip
import io
import json
import math
import sys
import tempfile
import unittest
import unittest.mock
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'analysis'))
import bob1
import lod_overlay
import lod_recipes
from inspect_x3 import read_catalogue
from sector_fog_census import write_catalogue
from test_bob1 import atlas_textures, atlas_tree

PITCH, WIDTH, LENGTH, TILT, PLANE = 1222.6, 1372.0, 7851.0, 14.6, 105
ACROSS = (-0.5, -0.3, 0.0, 0.3, 0.5)
EXPECT = dict(strips=12, strip_size=(7851, 1372), pitch=1222.6, tilt_deg=14.6, tol=0.02)
RECIPE = dict(source_record=1, ops=[('weld_strips', dict(material=1, plane_y=PLANE, axis='z'))], expect=EXPECT)


def part(groups, pts):
    """Precomputed part: one 7-int record per used point (first-use order), bounds enclosing the points."""
    used = [i for g in groups for i in dict.fromkeys(i for f in g['faces'] for i in f[:3])]
    lo = [min(pts[i][k] for i in used) for k in (1, 2, 3)]
    hi = [max(pts[i][k] for i in used) for k in (1, 2, 3)]
    c = [(a + b) // 2 for a, b in zip(lo, hi)]
    half = [(b - a) // 2 + 1 for a, b in zip(lo, hi)]
    for g in groups:
        g['extra'] = [(i, 1000 * g['material'] + i, 0, 65536, 0, 65536, 0)
                      for i in dict.fromkeys(i for f in g['faces'] for i in f[:3])]
    return {'flags': 0x30000001, 'groups': groups, 'bounds': c + [max(half)] + c + half}


def louvre_record(n=6, rows=2, tilt=TILT, girders=True, rim=False, value=100):
    """n strips per row (material 1) at PITCH across x, tilted about z (the high edge at -x, like the Terran
    panes), 4 x 1 quads each; a box girder (material 0, y -50..100) under every strip's lower edge; rim adds a
    record-0-only quad of material 0 far away."""
    t = math.radians(tilt)
    nx, ny = round(math.sin(t) * 65536), round(math.cos(t) * 65536)
    pts, sf, gf = [], [], []
    for r in range(rows):
        z0 = -9000 * r
        for k in range(n):
            xc, b = round(k * PITCH), len(pts)
            for zi, z in enumerate((z0 - LENGTH / 2, z0 + LENGTH / 2)):
                for ai, a in enumerate(ACROSS):
                    pts.append((0x1b, round(xc + a * WIDTH * math.cos(t)), round(38 - a * WIDTH * math.sin(t)),
                                round(z), ai * 16384 + k, zi * 8 * 65536 + r, nx, ny, 0, 1))
            for ai in range(4):
                p0, p1, q0, q1 = b + ai, b + ai + 1, b + 5 + ai, b + 6 + ai
                sf += [(p0, q0, p1, 1), (p1, q0, q1, 1)]
            if girders:
                xg, g = xc + 0.5 * WIDTH * math.cos(t) - 150, len(pts)
                for z in (z0 - LENGTH / 2, z0 + LENGTH / 2):
                    for x, y in ((xg - 75, -50), (xg + 75, -50), (xg + 75, 100), (xg - 75, 100)):
                        pts.append((0x1b, round(x), y, round(z), 0, 0, 0, 65536, 0, 1))
                for a, bb, c, d in ((3, 2, 6, 7), (0, 3, 7, 4), (2, 1, 5, 6)):     # top and the two sides
                    sf_g = [(g + a, g + d, g + bb, 1), (g + bb, g + d, g + c, 1)]
                    gf += sf_g
    parts = [part([{'material': 1, 'faces': sf}], pts)]
    if gf:
        parts.insert(0, part([{'material': 0, 'faces': gf}], pts))
    if rim:
        b = len(pts)
        pts += [(0x1b, x, 2000, z, 0, 0, 0, 65536, 0, 1) for x, z in ((0, 5000), (500, 5000), (0, 5500))]
        parts[0]['groups'].append({'material': 0, 'faces': [(b, b + 2, b + 1, 1)],
                                   'extra': [(i, i, 0, 65536, 0, 65536, 0) for i in (b, b + 2, b + 1)]})
    return {'value': value, 'flags': 0, 'points': pts, 'parts': parts}


def louvre_tree(**kw):
    r1 = louvre_record(**kw)
    r0 = louvre_record(rim=True, value=65536, **kw)
    r2 = dict(copy.deepcopy(r0), value=50)
    r2['parts'] = [p for p in r2['parts'] if p['groups'][0]['material'] == 0]
    return {'sections': [('MAT6', copy.deepcopy(bob1.materials(atlas_tree()))), ('BODY', [r0, r1, r2])]}


def strip_edges(before, rec):
    """(row z, x lo, x hi) of each welded strip, by the strips of the record before the weld (welded
    neighbours share edge positions, so they no longer separate by position)."""
    out = []
    for s in lod_recipes.strips(before, 1, 'z'):
        xs = [rec['points'][i][1] for i in s['idx']]
        out.append((round(s['centre'][2]), min(xs), max(xs)))
    return out


class Weld(unittest.TestCase):
    def test_flat_abutting_and_attributes_kept(self):
        tree = louvre_tree()
        ladder = bob1.lods(tree)
        before = copy.deepcopy(ladder[1])
        self.assertGreater(lod_recipes.above_share(before, 1)['share'], 0.01)      # girders poke through
        n, rec, rep = lod_recipes.prepare('stations/t/louvre', RECIPE, ladder)
        self.assertEqual((n, rep['ops'][0]['strips'], rep['ops'][0]['abutting']), (1, 12, 10))
        self.assertEqual(ladder[1], before)                                          # the source is not mutated
        strip_pts = {i for f in lod_recipes.material_faces(rec, 1) for i in f[:3]}
        self.assertEqual({rec['points'][i][2] for i in strip_pts}, {PLANE})           # planar at plane_y
        for i, (p, q) in enumerate(zip(before['points'], rec['points'])):
            self.assertEqual(p[3:], q[3:])            # z, uv, normal, u32 kept on every point
            self.assertEqual(p[0], q[0])
            if i not in strip_pts:
                self.assertEqual(p, q)                # girders untouched
        self.assertEqual(rec['parts'], before['parts'])                              # faces, tangents, bounds
        self.assertEqual(sum(len(g['faces']) for p in rec['parts'] for g in p['groups']),
                         sum(len(g['faces']) for p in before['parts'] for g in p['groups']))
        edges = strip_edges(before, rec)
        self.assertEqual(len(edges), 12)
        by_row = {}
        for z, lo, hi in edges:
            by_row.setdefault(z, []).append((lo, hi))
        for row in by_row.values():
            row.sort()
            self.assertTrue(all(a[1] == b[0] for a, b in zip(row, row[1:])))       # bit-identical shared edges
            self.assertTrue(all(abs((hi - lo) - PITCH) <= 2 for lo, hi in row))     # integer rounding
        a = lod_recipes.above_share(rec, 1)
        self.assertEqual(a['above'], 0)
        self.assertAlmostEqual(a['max_above'], 100 - PLANE, delta=0.01)
        box = lambda r: [(min(r['points'][i][k] for i in strip_pts), max(r['points'][i][k] for i in strip_pts))
                         for k in (1, 2, 3)]
        self.assertTrue(all(o[0] <= w[0] and w[1] <= o[1] for o, w in zip(box(before), box(rec))))

    def test_serialise_and_parse_back(self):
        tree = louvre_tree()
        ladder = bob1.lods(tree)
        _, rec, _ = lod_recipes.prepare('stations/t/louvre', RECIPE, ladder)
        ladder[1] = rec
        back = bob1.parse(bob1.serialise(tree))
        self.assertEqual([l['value'] for l in bob1.lods(back)], [65536, 100, 50])
        self.assertEqual(bob1.lods(back)[1]['points'], rec['points'])
        self.assertEqual(bob1.materials(back), bob1.materials(tree))

    def test_expect_refuses_other_geometry(self):
        ladder = bob1.lods(louvre_tree())
        cases = [(dict(EXPECT, strips=13), '12 strips, expected 13'),
                 (dict(EXPECT, pitch=1300.0), 'without a neighbour'),
                 (dict(EXPECT, tilt_deg=20.0), 'off the expected shape'),
                 (dict(EXPECT, strip_size=(9000, 1372)), 'longest strip')]
        for expect, needle in cases:
            with self.assertRaisesRegex(lod_recipes.RecipeMismatch, needle):
                lod_recipes.prepare('k', dict(RECIPE, expect=expect), ladder)
        flat = bob1.lods(louvre_tree(tilt=0.0))
        with self.assertRaisesRegex(lod_recipes.RecipeMismatch, 'off the expected shape'):
            lod_recipes.prepare('k', RECIPE, flat)
        shared = copy.deepcopy(ladder)
        g = shared[1]['parts'][0]['groups'][0]
        g['faces'].append((0, 1, g['faces'][0][0], 1))                                # a girder face on strip points
        with self.assertRaisesRegex(lod_recipes.RecipeMismatch, 'shared with other materials'):
            lod_recipes.prepare('k', RECIPE, shared)
        with self.assertRaisesRegex(lod_recipes.RecipeMismatch, 'outside the body'):
            lod_recipes.prepare('k', dict(RECIPE, source_record=3), ladder)

    def test_lookup_and_digest(self):
        key, r = lod_recipes.lookup('objects\\Stations\\x3tc\\terran_spp_panel.pbb')
        self.assertEqual((key, r['source_record']), ('stations/x3tc/terran_spp_panel', 1))
        self.assertIsNone(lod_recipes.lookup('stations/x3tc/terran_spp_center'))
        d = lod_recipes.digest(key, r)
        self.assertEqual(d, lod_recipes.digest(key, copy.deepcopy(r)))
        self.assertNotEqual(d, lod_recipes.digest(key, dict(r, source_record=2)))
        self.assertNotIn('lod_recipes.py', lod_overlay.TOOL_FILES)     # recipe changes rebuild recipe bodies only


def run(argv):
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        code = lod_overlay.main(argv)
    return code, out.getvalue()


class Batch(unittest.TestCase):
    RECIPES = {'stations/t/louvre': RECIPE,
               'stations/t/other': dict(RECIPE, expect=dict(EXPECT, strips=13)),
               'stations/t/boom': dict(RECIPE, ops=[('no_such_op', {})])}        # an op that raises (KeyError)

    def bake(self, folder, recipes):
        game = Path(folder) / 'game'
        if not game.exists():
            write_catalogue(game / '01.cat', atlas_textures())
            body = gzip.compress(bob1.serialise(louvre_tree()), mtime=0)
            write_catalogue(game / '02.cat', [('objects/stations/t/louvre.pbb', body),
                                              ('objects/stations/t/other.pbb', body),
                                              ('objects/stations/t/boom.pbb', body),
                                              ('objects/stations/t/plain.pbb', body)])
        out = Path(folder) / f'out{len(list(Path(folder).glob("out*")))}'
        with unittest.mock.patch.dict(lod_recipes.RECIPES, recipes, clear=True):
            code, text = run(['--batch', '--jobs', '1', '--no-aspect', '--atlas-size', '64', '--atlas-max-size', '128',
                              '--min-texels', '0', '--game', str(game), '--out', str(out)])
        record = json.loads((out / 'x3m-lod-batch.json').read_text())
        marker = json.loads(next((out / 'addon').glob('*.x3m-lod.json')).read_text())
        return code, text, {b['name']: b for b in record['bodies']}, {b['name']: b for b in marker['bodies']}, out

    def test_recipe_applied_skipped_and_hashes(self):
        with tempfile.TemporaryDirectory() as folder:
            code, text, rows, bodies, out = self.bake(folder, self.RECIPES)
            _, _, plain_rows, plain_bodies, _ = self.bake(folder, {})
            cat = next((out / 'addon').glob('*.cat'))
            raw = bytes(v ^ 0x33 for v in cat.with_suffix('.dat').read_bytes())
            ent = {e['path']: e for e in read_catalogue(cat)}['objects/stations/t/louvre.pbb']
        self.assertIn(code, (0, None))
        lv, ot, pl = rows['stations/t/louvre'], rows['stations/t/other'], rows['stations/t/plain']
        self.assertEqual((lv['recipe'], lv['source_record']), ('stations/t/louvre', 1))
        self.assertEqual(lv['recipe_ops'][0]['strips'], 12)
        self.assertIn('12 strips, expected 13', ot['recipe_skipped'])
        self.assertNotIn('recipe', ot)
        bm = rows['stations/t/boom']                  # any failure of an op bakes plainly, not refused
        self.assertIn('KeyError', bm['recipe_skipped'])
        self.assertTrue(bm['eligible'] and not bm['refuse'])
        self.assertEqual(bm['inputs_sha256'], plain_rows['stations/t/boom']['inputs_sha256'])
        self.assertEqual(bodies['stations/t/boom']['source_record'], 0)
        self.assertNotIn('recipe', bodies['stations/t/boom'])
        self.assertIn('recipe stations/t/louvre: C from record 1 with weld_strips', text)
        self.assertIn('recipe_skipped stations/t/other', text)
        # the recipe digest joins only the recipe body's inputs hash
        self.assertNotEqual(lv['inputs_sha256'], plain_rows['stations/t/louvre']['inputs_sha256'])
        self.assertEqual(ot['inputs_sha256'], plain_rows['stations/t/other']['inputs_sha256'])
        self.assertEqual(pl['inputs_sha256'], plain_rows['stations/t/plain']['inputs_sha256'])
        self.assertEqual(bodies['stations/t/louvre']['source_record'], 1)
        self.assertEqual(bodies['stations/t/louvre']['recipe']['ops'][0]['plane_y'], PLANE)
        self.assertEqual([bodies[n]['source_record'] for n in ('stations/t/other', 'stations/t/plain')], [0, 0])
        self.assertNotIn('recipe', bodies['stations/t/other'])
        self.assertEqual(bodies['stations/t/plain'], plain_bodies['stations/t/plain'])      # plain body identical
        # the baked C of the recipe body: welded strip positions, none of the tilted ones, no rim face
        c = bob1.lods(bob1.parse(gzip.decompress(raw[ent['offset']:ent['offset'] + ent['size']])))[1]
        _, welded, _ = lod_recipes.prepare('k', RECIPE, bob1.lods(louvre_tree()))
        tilted = {p[1:4] for p in bob1.lods(louvre_tree())[1]['points'] if p[2] not in (-50, 100)}
        got = {p[1:4] for p in c['points']}
        self.assertTrue({p[1:4] for p in welded['points']} <= got)
        self.assertFalse(got & (tilted - {p[1:4] for p in welded['points']}))
        self.assertNotIn(2000, {p[2] for p in c['points']})                                  # record 1, not 0


GAME = bob1.DEFAULT_GAME


@unittest.skipUnless((GAME / '01.cat').exists(), 'X3 bottle not present')
class RealBody(unittest.TestCase):
    def test_terran_spp_panel(self):
        assets, _ = lod_overlay.original_assets(GAME)
        tree = bob1.parse(assets.read_entry(bob1.resolve_body(assets, 'stations/x3tc/terran_spp_panel')),
                          lod_overlay.MAX_TRAILING)
        ladder = bob1.lods(tree)
        n, rec, rep = lod_recipes.prepare(*lod_recipes.lookup('stations/x3tc/terran_spp_panel'), ladder)
        op = rep['ops'][0]
        self.assertEqual((n, op['strips'], len(rec['points'])), (1, 132, 25678))
        self.assertTrue(1215 <= op['width_min'] <= op['width_max'] <= 1230)
        pts = {i for f in lod_recipes.material_faces(rec, 21) for i in f[:3]}
        self.assertEqual({rec['points'][i][2] for i in pts}, {105})
        self.assertEqual(lod_recipes.above_share(rec, 21)['above'], 0)
        self.assertGreater(lod_recipes.above_share(ladder[1], 21)['share'], 0.04)


if __name__ == '__main__':
    unittest.main()
