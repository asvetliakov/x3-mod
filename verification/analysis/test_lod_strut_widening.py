"""Strut widening of the merged-LOD baker (docs/architecture/lod-strut-widening.md section 8): lod_overlay
widen_thin_patches on a synthetic BOB1 body with box struts, a rib on a plate, a mitred picture-frame rim, a plate
tessellated into slivers, a bevelled plate edge, a hexagonal antenna and one alpha-tested card, each in its own
cell along the diagonal so no two overlap in an axis view. Every thin part is w = 320 units wide and the design
size puts W_u at 1,024 units (w k_d = 0.3125 px): widths, untouched classes, the blended group and its material,
tile alpha, an orthographic raster (luminance and sub-pixel coverage), bounds, round trip, and a byte-identical
bake of a body without thin patches. A two-facet chamfer (each facet half sewn to a plate, so the one-patch bevel
rule misses it) is part of the hull shell and stays (widen_thin_patches shell_share). A fin shares one point with a
wide triangle (the triangle stays bit-identical: the point is duplicated). Plates carry a darker hull texture than
the thin parts. A single-record body's pad is its vanilla record, not the widened C. The CLI options and their
refusals."""
import contextlib
import copy
import gzip
import io
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'analysis'))
import bob1
import lod_atlas
import lod_overlay
import lod_raster
import thin_patches
from sector_fog_census import write_catalogue
from test_bob1 import atlas_textures

W, W_U, CELL, R_RAW, T_PAD, T_1 = 320, 1024, 11008, 153600, 200, 100
OPTS = dict(sizes=(128, 256), min_texels=0, widen=lod_overlay.WIDEN_DEFAULTS)   # widening is opt-in      # screen width 1800 (1920x1080): F 960, k_d = 960 * 100 / (R_RAW 640)
STATE = {b'g_alphablendenable': 1, b'g_blendop': 1, b'g_srcblend': 5, b'g_destblend': 6, b'g_alphatestenable': 0,
         b'g_zwriteenable': 1, b'g_alphavalue': 65536}


def material(i, diffuse, card=False):
    state = [(b'g_CullMode', 0, [1 if card else 2]), (b'g_AlphaBlendEnable', 0, [int(card)]), (b'g_BlendOp', 0, [1]),
             (b'g_SrcBlend', 0, [5 if card else 2]), (b'g_DestBlend', 0, [6 if card else 1]),
             (b'g_ZWriteEnable', 0, [1]), (b'g_ALPHATESTENABLE', 0, [int(card)]), (b'g_AlphaValue', 2, [65536])]
    return {'index': i, 'flags': 0x02000000, 'technique': 1, 'effect': b'argon.fx',
            'params': [(b't_DiffuseTexture', 8, diffuse), (b't_LightMapTexture', 8, b'NULL')] + state
                      + [(b'g_MatDiffuseStrength', 2, [65536])]}


class Body:
    """Points (own four per quad, merged by position in the patch analysis), faces per material, named elements."""
    def __init__(self):
        self.pts, self.faces, self.parts = [], {0: [], 1: [], 2: []}, {}

    def poly(self, name, corners, normal, material=0, cell=0):
        o = cell * CELL
        n = np.asarray(normal, float) / np.linalg.norm(normal)
        base = len(self.pts)
        uv = ((0, 0), (1, 0), (1, 1), (0, 1), (0.5, 1), (0.5, 0))
        for (x, y, z), (u, v) in zip(corners, uv):
            self.pts.append((0x1b, int(round(x)) + o, int(round(y)) + o, int(round(z)) + o, int(u * 65536),
                             int(v * 65536), *(int(round(c * 65536)) for c in n), 1))
        self.faces[material] += [(base, base + i, base + i + 1, 1) for i in range(1, len(corners) - 1)]
        self.parts.setdefault(name, []).append(list(range(base, base + len(corners))))

    def box(self, name, lo, hi, cell, top_only=False, bottom=True):
        (x0, y0, z0), (x1, y1, z1) = lo, hi
        self.poly(name + '_side', [(x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1)], (1, 0, 0), cell=cell)
        self.poly(name + '_side', [(x0, y1, z0), (x0, y0, z0), (x0, y0, z1), (x0, y1, z1)], (-1, 0, 0), cell=cell)
        self.poly(name + '_side', [(x1, y1, z0), (x0, y1, z0), (x0, y1, z1), (x1, y1, z1)], (0, 1, 0), cell=cell)
        self.poly(name + '_side', [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)], (0, -1, 0), cell=cell)
        self.poly(name + '_cap', [(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)], (0, 0, 1), cell=cell)
        if bottom:
            self.poly(name + '_cap', [(x0, y1, z0), (x1, y1, z0), (x1, y0, z0), (x0, y0, z0)], (0, 0, -1), cell=cell)


def fixture_body():
    b = Body()
    for k, c in enumerate((176, 3376, 6576)):                                      # (i) box struts, pitch 3200
        b.box(f'strut{k}', (c - 160, c - 160, 16), (c + 160, c + 160, 8016), cell=0)
    b.poly('rib_plate', [(16, 16, 16), (8016, 16, 16), (8016, 8016, 16), (16, 8016, 16)], (0, 0, 1), material=2, cell=1)
    x0, x1, y0, y1, z0, z1 = 1008, 7024, 3856, 4176, 16, 336                           # (ii) rib on a plate
    b.poly('rib_side', [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)], (0, -1, 0), cell=1)
    b.poly('rib_side', [(x1, y1, z0), (x0, y1, z0), (x0, y1, z1), (x1, y1, z1)], (0, 1, 0), cell=1)
    b.poly('rib_top', [(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)], (0, 0, 1), cell=1)
    b.poly('rib_end', [(x0, y1, z0), (x0, y0, z0), (x0, y0, z1), (x0, y1, z1)], (-1, 0, 0), cell=1)
    b.poly('rib_end', [(x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1)], (1, 0, 0), cell=1)
    o, i = (16, 8016, 16, 4016), (336, 7696, 336, 3696)                               # (iii) mitred frame rim
    for name, q in (('rim_y', [(o[0], o[2]), (o[1], o[2]), (i[1], i[2]), (i[0], i[2])]),
                    ('rim_x', [(o[1], o[2]), (o[1], o[3]), (i[1], i[3]), (i[1], i[2])]),
                    ('rim_y', [(o[1], o[3]), (o[0], o[3]), (i[0], i[3]), (i[1], i[3])]),
                    ('rim_x', [(o[0], o[3]), (o[0], o[2]), (i[0], i[2]), (i[0], i[3])])):
        b.poly(name, [(x, y, 16) for x, y in q], (0, 0, 1), cell=2)
    for k in range(25):                                                             # (iv) plate in slivers
        b.poly('sliver', [(16 + W * k, 16, 16), (336 + W * k, 16, 16), (336 + W * k, 8016, 16), (16 + W * k, 8016, 16)],
               (0, 0, 1), material=2, cell=3)
    b.poly('bevel_top', [(16, 16, 16), (8016, 16, 16), (8016, 8016, 16), (16, 8016, 16)], (0, 0, 1), material=2, cell=4)
    b.poly('bevel', [(8016, 16, 16), (8200, 16, -246), (8200, 8016, -246), (8016, 8016, 16)], (262, 0, 184), cell=4)
    b.poly('bevel_side', [(8200, 16, -246), (5464, 16, -7764), (5464, 8016, -7764), (8200, 8016, -246)],
           (7518, 0, -2736), material=2, cell=4)                                                # (v) bevelled plate edge
    ang = [np.radians(60 * k) for k in range(6)]                                   # (vi) hexagonal antenna
    ring = [(4016 + W * np.cos(a), 4016 + W * np.sin(a)) for a in ang]
    for k in range(6):
        (xa, ya), (xb, yb) = ring[k], ring[(k + 1) % 6]
        m = np.radians(60 * k + 30)
        b.poly('antenna', [(xa, ya, 16), (xb, yb, 16), (xb, yb, 8016), (xa, ya, 8016)], (np.cos(m), np.sin(m), 0),
               cell=5)
    b.poly('antenna_cap', [(x, y, 8016) for x, y in ring], (0, 0, 1), cell=5)
    b.poly('antenna_cap', [(x, y, 16) for x, y in reversed(ring)], (0, 0, -1), cell=5)
    b.poly('card', [(16, 4016, 16), (336, 4016, 16), (336, 4016, 8016), (16, 4016, 8016)], (0, 1, 0), material=1,
           cell=6)                                                                  # alpha-tested card
    edge = [(8016, 16), (8222, -229), (8166, -544), (1238, -4544)]                 # two-facet chamfer (50 deg folds):
    b.poly('chamfer2_plate', [(16, 16, 16), (8016, 16, 16), (8016, 8016, 16), (16, 8016, 16)], (0, 0, 1), material=2,
           cell=7)
    for name, (x0, z0), (x1, z1) in zip(('chamfer2', 'chamfer2', 'chamfer2_plate'), edge, edge[1:]):
        b.poly(name, [(x0, 16, z0), (x1, 16, z1), (x1, 8016, z1), (x0, 8016, z0)], (z0 - z1, 0, x1 - x0),
               material=2 if name.endswith('plate') else 0, cell=7)                # each facet half sewn to a plate
    b.poly('fin', [(4016, 2016, 16), (4336, 2016, 16), (4336, 2016, 8016), (4016, 2016, 8016)], (0, 1, 0), cell=6)
    corner = b.parts['fin'][0][0]                                                   # a wide triangle on the fin's corner
    base = len(b.pts)
    for x, y in ((8016, 16), (8016, 4016)):
        b.pts.append((0x1b, x + 6 * CELL, y + 6 * CELL, 16 + 6 * CELL, 0, 0, 0, 0, 65536, 1))
    b.faces[2].append((corner, base, base + 1, 1))
    b.parts['fin_base'] = [[corner, base, base + 1]]
    return tree_of(b)


def cube_body():
    b = Body()
    b.box('cube', (16, 16, 16), (8016, 8016, 8016), cell=1)
    return tree_of(b)


def single_body():
    """One record only (C comes from the coarsest record): two box struts over a plate."""
    b = Body()
    for k, c in enumerate((176, 3376)):
        b.box(f'strut{k}', (c - 160, c - 160, 16), (c + 160, c + 160, 8016), cell=0)
    b.poly('plate', [(16, 16, 16), (8016, 16, 16), (8016, 8016, 16), (16, 8016, 16)], (0, 0, 1), material=2, cell=2)
    return tree_of(b, single=True)


def tree_of(b, single=False):
    pts = b.pts + [(0x1b, R_RAW, 0, 0, 0, 0, 0, 0, 65536, 1)]                  # far point: r_raw = 153,600
    groups = [{'material': m, 'faces': f, 'extra': [(i, 1000 + i, 0, 65536, 0, 65536, 0)
                                                   for i in dict.fromkeys(j for t in f for j in t[:3])]}
              for m, f in b.faces.items() if f]
    used = np.array([pts[i][1:4] for g in groups for f in g['faces'] for i in f[:3]], float)
    lo, hi = used.min(0), used.max(0)
    pivot = np.rint(used.mean(0))
    c = np.floor((lo + hi) / 2)
    bounds = [int(x) for x in pivot] + [int(np.ceil(np.abs(used - pivot).max()))] + [int(x) for x in c] \
        + [int(x) for x in np.ceil(np.maximum(c - lo, hi - c))]
    lod0 = {'value': 65536, 'flags': 0, 'points': pts, 'parts': [{'flags': 0x10000001, 'groups': groups, 'bounds': bounds}]}
    lod1 = dict(copy.deepcopy(lod0), value=T_1)
    tree = {'sections': [('MAT6', [material(0, b'a_diff.tga'), material(1, b'c_diff.tga', card=True),
                                   material(2, b'h_diff.tga')]),
                         ('BODY', [lod0] if single else [lod0, lod1])]}
    return bob1.parse(bob1.serialise(tree)), b.parts


def c_diff():
    y, x = np.mgrid[0:16, 0:16]
    img = np.stack([np.full((16, 16), 90), 16 * x, 16 * y, np.where((x // 4 + y // 4) % 2, 255, 0)], -1)
    return gzip.compress(lod_atlas.write_dds([img.astype(np.uint8)], 'A8R8G8B8'), mtime=0)


def h_diff():
    img = np.broadcast_to(np.array((70, 60, 50, 255), np.uint8), (16, 16, 4)).copy()   # a dark hull, luma ~0.24
    return gzip.compress(lod_atlas.write_dds([img], 'A8R8G8B8'), mtime=0)


def run(argv):
    out = io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(io.StringIO()):
        code = lod_overlay.main(argv)
    return code, out.getvalue()


def params(m):
    return {n.lower(): v for n, t, v in m['params']}


class StrutWidening(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        game = cls.game = Path(cls.tmp.name) / 'game'
        (cls.tree, cls.parts), (cls.cube, _), (cls.single, _) = fixture_body(), cube_body(), single_body()
        write_catalogue(game / '01.cat', atlas_textures() + [('dds/c_diff.pck', c_diff()), ('dds/h_diff.pck', h_diff())])
        write_catalogue(game / '02.cat', [(f'objects/ships/x/{n}.pbb', gzip.compress(bob1.serialise(t), mtime=0))
                                          for n, t in (('w', cls.tree), ('cube', cls.cube), ('single', cls.single))])
        cls.assets, _ = lod_overlay.original_assets(game)
        cls.plan = cls.bake('ships/x/w')
        cls.flat = cls.bake('ships/x/w', widen=None)
        rec0 = bob1.lods(cls.tree)[0]
        cls.k_d, cls.s_d, _, cls.r_raw = lod_overlay.design_scale(rec0, T_PAD, lod_overlay.WIDEN_DEFAULTS, 1800)
        cls.before = copy.deepcopy(rec0)
        cls.src, cls.rep = lod_overlay.widen_thin_patches(rec0, bob1.materials(cls.tree), cls.k_d, exclude=frozenset({1}))

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    @classmethod
    def bake(cls, name, **kw):
        return lod_overlay.plan_body(cls.assets, name, T_PAD, 'compact', False, 'atlas', False, lod_overlay.GLOW_LUMA,
                                     lod_overlay.GLOW_SHARE, None, True, dict(OPTS, **kw), 0)

    def pos(self, rec, name):
        return [np.array([rec['points'][i][1:4] for i in q], float) for q in self.parts[name]]

    def test_design_size_and_report(self):
        self.assertEqual((self.k_d, self.s_d, self.r_raw), (1 / W_U, 100.0, R_RAW))
        self.assertEqual(self.rep['w_units'], W_U)
        self.assertEqual(self.plan['widen']['widened_faces'], self.rep['widened_faces'])
        self.assertEqual(self.rep['kinds'], dict(strip=3 * 4 + 3 + 6 + 1, band=1, isotropic=3 * 2 + 2 + 2, isotropic_moved=0))
        self.assertEqual(self.rep['materials'], [0])
        self.assertEqual(self.rep['skipped']['shell'], 2)             # the two-facet chamfer: part of the shell
        self.assertEqual(self.rep['alpha_classes'], {'3': 3 * 4 + 4 + 8, '10': 3 * 8 + 6 + 8 + 12 + 2})
        self.assertEqual(self.rep['duplicated_points'], 1)            # the fin corner the wide triangle uses
        self.assertEqual(self.before, bob1.lods(self.tree)[0])                  # the input record is not modified

    def test_widths(self):
        """Every strut, rib, rim and antenna part is W_u wide within 1 unit and unchanged along its axis."""
        for k in range(3):
            q = np.concatenate(self.pos(self.src, f'strut{k}_side') + self.pos(self.src, f'strut{k}_cap'))
            ext = q.max(0) - q.min(0)
            np.testing.assert_allclose(ext, (W_U, W_U, 8000), atol=1)
            np.testing.assert_allclose((q[:, 2].min(), q[:, 2].max()), (16, 8016), atol=1)
        rib = np.concatenate(self.pos(self.src, 'rib_side') + self.pos(self.src, 'rib_top') + self.pos(self.src, 'rib_end'))
        np.testing.assert_allclose(rib.max(0) - rib.min(0), (6016, W_U, W_U), atol=1)
        np.testing.assert_allclose(rib[:, 2].min() - CELL, 176 - W_U / 2, atol=1)  # the base sinks into the plate
        for axis, name in ((1, 'rim_y'), (0, 'rim_x')):
            for q in self.pos(self.src, name):
                self.assertAlmostEqual(np.ptp(q[:, axis]), W_U, delta=1)
        for q in self.pos(self.src, 'antenna'):
            self.assertAlmostEqual(np.linalg.norm(q[1] - q[0]), W_U, delta=1)
            self.assertAlmostEqual(np.ptp(q[:, 2]), 8000, delta=1)
        geo = thin_patches.face_geometry(self.src)                       # the op's own measure: strips at W_u
        groups = self.src['parts'][0]['groups']
        wid = {geo['patch'][f] for f, (_, g, _) in enumerate(geo['where']) if groups[g].get('widen')}
        strips = [p for p in wid if geo['pext'][p, 0] > 2 * geo['pext'][p, 1] and geo['pwidth'][p] < 2 * W_U]
        self.assertTrue(strips)
        np.testing.assert_allclose(geo['pwidth'][strips], W_U, atol=1)

    def test_untouched_classes_and_records(self):
        src, orig = self.src, bob1.lods(self.tree)[0]
        for name in ('rib_plate', 'sliver', 'bevel_top', 'bevel', 'bevel_side', 'card', 'chamfer2', 'chamfer2_plate',
                     'fin_base'):
            for q in self.parts[name]:
                self.assertEqual([src['points'][i] for i in q], [orig['points'][i] for i in q], name)
        n = len(orig['points'])
        rest = [i for g in src['parts'][0]['groups'] if not g.get('widen') for f in g['faces'] for i in f[:3]]
        self.assertTrue(all(i < n and src['points'][i] == orig['points'][i] for i in rest))   # non-widened: bit-identical
        self.assertEqual(len(src['points']), n + 1)
        self.assertEqual([p[:1] + p[4:] for p in src['points'][:n]], [p[:1] + p[4:] for p in orig['points']])
        rest_of = lambda r: [p[:1] + p[4:] for p in r['points']]                     # flags, UV, normal, u32
        face = lambda r: sorted(tuple(rest_of(r)[i] for i in f[:3]) + (f[3],)
                                for p in r['parts'] for g in p['groups'] for f in g['faces'])
        self.assertEqual(face(src), face(orig))
        og = {g['material']: {e[0]: e for e in g['extra']} for g in orig['parts'][0]['groups']}
        for g in src['parts'][0]['groups']:
            used = {i for f in g['faces'] for i in f[:3]}
            self.assertEqual({e[0] for e in g['extra']}, used)
            for e in g['extra']:                                             # fixture records carry 1000 + origin
                self.assertEqual(og[g['material']][e[1] - 1000][1:], tuple(e[1:]))
                self.assertTrue(g.get('widen') or e[0] == e[1] - 1000)
            self.assertTrue(g.get('widen') is None or (g['material'] == 0 and 1 <= g['widen'] < 32))
        ladder, flat = self.plan['ladder'], self.flat['ladder']
        self.assertEqual(ladder[0], orig)                                 # record 0 untouched
        self.assertEqual(ladder[2], dict(bob1.lods(self.tree)[1], value=T_PAD))   # the pad: the vanilla record 1
        self.assertEqual(flat[2], ladder[2])

    def test_single_record_pad_is_vanilla(self):
        """C of a one-record body comes from its coarsest record: the pad (collision tree) is that vanilla record,
        byte for byte, never the widened C."""
        plan, orig = self.bake('ships/x/single'), bob1.lods(self.single)[0]
        self.assertGreater(plan['widen']['widened_patches'], 0)
        ladder = bob1.lods(bob1.parse(gzip.decompress(plan['stored'])))
        self.assertEqual((plan['pad_index'], plan['pad_source']), (2, 0))
        self.assertEqual(lod_overlay.record_bytes(ladder[2]), lod_overlay.record_bytes(dict(orig, value=T_PAD)))
        self.assertEqual(ladder[0], orig)
        self.assertNotEqual(sorted(p[1:4] for p in ladder[1]['points']), sorted(p[1:4] for p in orig['points']))
        flat = self.bake('ships/x/single', widen=None)                  # unwidened: the pad stays a copy of C
        self.assertIsNone(flat['pad_source'])

    def test_cli_options(self):
        base = ['--game', str(self.game), '--dry-run', '--collapse', 'atlas', '--atlas-size', '128', '--atlas-max-size',
                '256', '--min-texels', '0', 'ships/x/w=200@0']
        self.assertNotIn('  widen ', run(base)[1])                     # opt-in: off by default
        self.assertNotIn('  widen ', run(base + ['--no-widen'])[1])
        self.assertIn('widen 1 px at s_d = T_pad / 2 = 100 (k_d 0.000976562', run(base + ['--widen'])[1])
        self.assertIn('widen 2 px at s_d = T_pad / 4 = 50 (k_d 0.000488281',
                      run(base + ['--widen', '--widen-px', '2', '--widen-design-divisor', '4'])[1])
        for bad in (['--widen-px', '0'], ['--widen-px', '-1'], ['--widen-design-divisor', '0.5']):
            with self.assertRaises(SystemExit) as cm:
                run(base + bad)
            self.assertEqual(cm.exception.code, 2, bad)

    def test_blended_group_and_material(self):
        plan, c = self.plan, self.plan['new']
        mats = bob1.materials(bob1.parse(gzip.decompress(plan['stored'])))
        wm = plan['atlas']['widened_materials']
        self.assertEqual(len(wm), 1)
        kinds = [lod_overlay.group_kind(plan, g) for g in c['parts'][0]['groups']]
        self.assertEqual(kinds, ['atlas', 'widened', 'alpha'])          # atlas, widened, then the alpha group
        grp = c['parts'][0]['groups'][1]
        self.assertEqual(len(grp['faces']), self.rep['widened_faces'])
        p, a = params(mats[wm[0]]), params(mats[plan['atlas']['material']])
        self.assertEqual({k: p[k][0] for k in STATE}, STATE)
        self.assertEqual({k: v for k, v in p.items() if k not in STATE}, {k: v for k, v in a.items() if k not in STATE})
        self.assertEqual(p[b'g_cullmode'], [2])
        self.assertEqual(plan['widen']['draws_added'], 1)
        self.assertEqual(len(self.flat['new']['parts'][0]['groups']), 2)  # --no-widen: atlas + alpha

    def test_tile_alpha(self):
        """The widened faces' diffuse and light atlas alpha is the coverage w / W_u (caps (w / W_u)^2), 1/32 steps."""
        plan, c = self.plan, self.plan['new']
        enc = plan['atlas_build']['encoded']
        self.assertEqual(enc['diffuse']['format'], 'DXT5')
        pos = {tuple(p[1:4]) for p in self.src['points']}
        cap_axis = {0: 2, 1: 0, 5: 2}                   # cell -> the normal axis of its caps (struts, rib ends, antenna)
        for slot in ('diffuse', 'light'):
            atlas = enc[slot]['decoded'].astype(np.float64)
            n = 0
            for f in c['parts'][0]['groups'][1]['faces']:
                q = [c['points'][i] for i in f[:3]]
                self.assertTrue(all(tuple(p[1:4]) in pos for p in q))
                uv = np.mean([lod_atlas.point_uv(p) for p in q], 0)
                got = lod_atlas.bilinear(atlas, np.array([uv[0]]), np.array([uv[1]]), False)[0, 3] / 255
                cell = int(np.mean([p[1] for p in q]) // CELL)
                cap = cell in cap_axis and abs(q[0][6 + cap_axis[cell]]) > 60000     # stored normal along that axis
                want = (W / W_U) ** 2 if cap else W / W_U
                self.assertLessEqual(abs(got - want), 1 / 32, (slot, got, want))
                n += 1
            self.assertEqual(n, self.rep['widened_faces'])

    def faces(self, plan):
        """Faces of C, the widened (blended) mask, the atlas alpha of the widened faces and every atlased face's
        colour: Rec.709 luminance of the diffuse atlas at its UV centroid (thin parts ~0.46, hull plates ~0.24);
        the alpha card 0.5."""
        f = lod_raster.record_faces(plan['new'], bob1.materials(bob1.parse(gzip.decompress(plan['stored']))))
        wm = set((plan['atlas'] or {}).get('widened_materials', ()))
        blended = np.isin(f['M'], list(wm))
        atlased = np.isin(f['M'], list(wm | set(plan['atlas']['materials'])))
        atlas = plan['atlas_build']['encoded']['diffuse']['decoded'].astype(np.float64)
        s = lod_atlas.bilinear(atlas, f['uv'][atlased, 0], f['uv'][atlased, 1], False)
        colour, alpha = np.full(len(f['M']), 0.5), np.ones(len(f['M']))
        colour[atlased] = s[:, :3] @ np.array([0.2126, 0.7152, 0.0722]) / 255
        alpha[atlased] = np.where(blended[atlased], s[:, 3] / 255, 1.0)
        return f, blended, alpha, colour

    def test_raster_luminance(self):
        """Per axis view (16 x 16 samples per pixel at k_d), the light change of C over sky (black) and over a hull
        colour (0.6, unlike both the thin parts, ~0.46, and the plates, ~0.24), as a share of C's light over sky:
        1 + (sum after - sum before) / sum before over sky, within 2 %; the rib also stands on a darker plate."""
        before, after = self.faces(self.flat), self.faces(self.plan)
        for axis in range(3):
            tri0, d0, front0 = lod_raster.view(before[0], axis)
            tri1, d1, front1 = lod_raster.view(after[0], axis)
            origin, size = lod_raster.frame(np.concatenate([tri0, tri1]), self.k_d)
            light = None
            for bg in (0.0, 0.6):
                img = []
                for (f, blended, alpha, colour), tri, d, front in ((before, tri0, d0, front0), (after, tri1, d1, front1)):
                    img.append(lod_raster.render(tri[front], d[front], colour[front], alpha[front], blended[front],
                                                 self.k_d, origin, size, background=bg, sub=16)['image'])
                light = img[0].sum() if light is None else light
                self.assertAlmostEqual(1 + (img[1].sum() - img[0].sum()) / light, 1.0, delta=0.02, msg=(axis, bg))

    def test_raster_coverage(self):
        """Seen down x, every row of every widened strut covers at least one pixel centre in each of the 8 jitter
        phases (no coverage below 1 px at k_d); the unwidened struts leave rows empty."""
        struts = {i for k in range(3) for q in self.parts[f'strut{k}_side'] for i in q}
        for plan, want in ((self.plan, True), (self.flat, False)):
            f = self.faces(plan)[0]
            pts = plan['new']['points']
            pos = {tuple(p[1:4]) for p in (self.src if want else bob1.lods(self.tree)[0])['points'][min(struts):max(struts) + 1]}
            sel = np.array([all(tuple(pts[i][1:4]) in pos for i in plan['new']['parts'][0]['groups'][g]['faces'][fi][:3])
                            for _, g, fi in f['where']])
            tri, d, front = lod_raster.view(f, 0)
            sel &= front
            origin, size = lod_raster.frame(tri[sel], self.k_d)
            full = True
            for jit in lod_raster.JITTER8:
                cov = lod_raster.render(tri[sel], d[sel], np.ones(sel.sum()), np.ones(sel.sum()), np.zeros(sel.sum(), bool),
                                        self.k_d, origin, size, jitter=jit)['cover']
                rows = cov[4:-4]                                    # z rows inside the struts (ends excluded)
                for k in range(3):
                    c = (176 + 3200 * k) * self.k_d - origin[0] * self.k_d
                    band = rows[:, max(0, int(c) - 2):int(c) + 3]
                    full &= bool((band.sum(1) >= 1).all())
            self.assertEqual(full, want)

    def test_bounds_and_round_trip(self):
        c = self.plan['new']
        for part in c['parts']:
            q = np.array([c['points'][i][1:4] for g in part['groups'] for f in g['faces'] for i in f[:3]], float)
            b = part['bounds']
            lo = np.array(b[4:7]) - np.array(b[7:10]); hi = np.array(b[4:7]) + np.array(b[7:10])
            self.assertTrue((q >= lo).all() and (q <= hi).all())
            self.assertLessEqual(np.abs(q - np.array(b[:3])).max(), b[3])
        self.assertEqual(self.rep['bounds_grown'], [0])
        back = bob1.lods(bob1.parse(gzip.decompress(self.plan['stored'])))
        self.assertEqual([l['value'] for l in back], [65536, T_1, T_PAD])
        self.assertEqual(back[1], dict(c, value=T_1))

    def test_body_without_thin_patches_bakes_identically(self):
        on, off = self.bake('ships/x/cube'), self.bake('ships/x/cube', widen=None)
        self.assertEqual(on['widen']['widened_patches'], 0)
        self.assertEqual(on['stored'], off['stored'])
        self.assertEqual(on['extra_members'], off['extra_members'])


if __name__ == '__main__':
    unittest.main()
