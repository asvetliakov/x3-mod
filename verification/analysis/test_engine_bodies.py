"""tools/effects/engine_bodies.py: body list and key rule, extents, colour weighting, clusters, tiers, the missing
array, layer precedence (--stock-only vs mod layers and loose files) and deterministic output."""
import contextlib
import csv
import importlib.util
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import bob1  # noqa: E402
import sector_fog_census as sfc  # noqa: E402

_spec = importlib.util.spec_from_file_location('engine_bodies', ROOT / 'tools/effects/engine_bodies.py')
eb = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(eb)

BODIES_TXT = (b'//bodies for specific purposes\r\nSBTYPE_2D;2;\r\n78;385;\r\n'
              b'SBTYPE_JET;6;\r\n566;effects\\engines\\fx_a_tiny;\teffects\\engines\\fx_b;\r\n'
              b'effects\\engines\\FX_A_TINY; // same body, other case\r\neffects\\engines\\gone;effects\\engines\\fx_m3;\r\n'
              b'effects\\engines\\past_the_count;\r\nSBTYPE_RADAR;1;\r\n7;\r\nSBTYPE_SMALLJET;1;\r\n566;\r\n')


def dds_rgba(pixels):
    """32-bit BGRA DDS with an alpha mask, one level."""
    height, width = pixels.shape[:2]
    head = bytearray(128)
    head[:4] = b'DDS '
    struct.pack_into('<IIIIIII', head, 4, 124, 0x100f, height, width, width * 4, 0, 1)
    struct.pack_into('<III', head, 76, 32, 0x41, 0)
    struct.pack_into('<I4I', head, 88, 32, 0xff0000, 0xff00, 0xff, 0xff000000)
    return bytes(head) + pixels[..., [2, 1, 0, 3]].astype(np.uint8).tobytes()


def effect_material(diffuse, src=2, dst=4, zwrite=0):
    return {'index': 0, 'flags': 0x02000000, 'technique': 1, 'effect': b'engine.fx',
            'params': [(b'g_AlphaBlendEnable', 1, [1]), (b'g_SrcBlend', 1, [src]), (b'g_DestBlend', 1, [dst]),
                       (b'g_ZWriteEnable', 1, [zwrite]), (b't_DiffuseTexture', 8, diffuse)]}


def legacy_material(texture):
    return {'index': 0, 'flags': 0, 'texture': texture, 'colors': [0] * 12, 'w24': 0, 'w26': 0, 'w2c': 0,
            'maps': [(b'', 0)] * 3, 'extra': [(b'', 0)] * 2}


def body(material, value, quads, pin=None):
    """BOB1 bytes: one LOD of `value`; quads = [((x0, x1), (y0, y1), (z0, z1))] in normalised units, each two
    triangles in the plane through the box diagonal; pin = a degenerate sliver reaching z = pin."""
    pts, faces = [], []
    for (x0, x1), (y0, y1), (z0, z1) in quads:
        base = len(pts)
        pts += [(1, x0, y0, z0), (1, x1, y1, z0), (1, x1, y1, z1), (1, x0, y0, z1)]
        faces += [(base, base + 1, base + 2, 1), (base, base + 2, base + 3, 1)]
    if pin is not None:
        base = len(pts)
        pts += [(1, 0, 0, 0), (1, 1, 0, 0), (1, 0, 0, pin)]
        faces.append((base, base + 1, base + 2, 1))
    lod = {'value': value, 'flags': 0, 'points': pts, 'parts': [{'flags': 1, 'groups': [{'material': 0, 'faces': faces}]}]}
    return bob1.serialise({'sections': [('MAT6', [material]), ('BODY', [lod])]})


def red_blue():
    """Left half opaque red, right half fully transparent blue, one transparent white texel."""
    px = np.zeros((4, 4, 4), np.uint8)
    px[:, :2] = [255, 0, 0, 255]
    px[:, 2:] = [0, 0, 255, 0]
    px[0, 3] = [255, 255, 255, 0]
    return px


def game(root, mods=False):
    """A stock layer set (01..13 + addon/01..04); mods=True adds addon/05.cat and loose files."""
    plume = body(effect_material(b'tex_red.dds'), 1000, [((-32768, 32768), (-16384, 16384), (-65536, 0))], pin=65536)
    both = body(effect_material(b'tex_red.dds', 2, 2), 7000, [((-65536, 65536), (0, 0), (-65536, 65536))])
    navjet = body(legacy_material(b'tex_grey.dds'), 350, [((-1000, 1000), (-1000, 1000), (-65536, 0))])
    members = {
        '01.cat': [('types/Bodies.txt', BODIES_TXT), ('objects/v/00566.bob', navjet),
                   ('dds/tex_red.dds', dds_rgba(red_blue())),
                   ('dds/tex_grey.dds', dds_rgba(np.full((2, 2, 4), [128, 128, 128, 255], np.uint8)))],
        'addon/01.cat': [('objects/effects/engines/fx_a_tiny.bob', plume), ('objects/effects/engines/fx_b.bob', both),
                         ('objects/effects/engines/fx_m3.bod', b'MATERIAL3: 0; 1; 2; 3; 4; 5; 6; 7;\n1000;\n')]}
    for cat in sfc.STOCK_AP_CATALOGUES:
        sfc.write_catalogue(root / cat, members.get(cat, [('types/Dummy.txt', b'x')]))
    if mods:
        bigger = body(effect_material(b'tex_red.dds'), 50000, [((-65536, 65536), (-65536, 65536), (-65536, 0))])
        sfc.write_catalogue(root / 'addon/05.cat', [('objects/effects/engines/fx_b.bob', bigger),
                                                    ('objects/effects/engines/fx_a_tiny.bob', bigger)])
        loose = root / 'objects/effects/engines/fx_a_tiny.bob'
        loose.parent.mkdir(parents=True)
        loose.write_bytes(body(effect_material(b'tex_red.dds'), 2000, [((-65536, 65536), (-65536, 65536), (0, 65536))]))
    return root


class EngineBodiesSynthetic(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        base = Path(cls.tmp.name)
        cls.stock_root = game(base / 'stock')
        cls.mod_root = game(base / 'mod', mods=True)
        cls.stock = eb.generate(cls.stock_root, stock_only=True)
        cls.mod = eb.generate(cls.mod_root)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_list_parsing_and_key_rule(self):
        lists = self.stock['generated_from']['lists']
        self.assertEqual(lists, {'jet': {'declared': 6, 'listed': 7}, 'smalljet': {'declared': 1, 'listed': 1}})
        names = set(self.stock['bodies']) | {m['name'] for m in self.stock['missing']}
        # 566 -> v\00566; FX_A_TINY folds into the first spelling; the 7th token is past the declared count
        self.assertEqual(names, {'v\\00566', 'effects\\engines\\fx_a_tiny', 'effects\\engines\\fx_b',
                                 'effects\\engines\\gone', 'effects\\engines\\fx_m3'})
        b = self.stock['bodies']
        self.assertEqual((b['v\\00566']['id'], b['v\\00566']['lists']), (566, ['jet', 'smalljet']))
        self.assertEqual((b['effects\\engines\\fx_a_tiny']['id'], b['effects\\engines\\fx_a_tiny']['lists']),
                         (None, ['jet']))
        self.assertEqual(eb.body_key('10209'), ('v\\10209', 10209))

    def test_extent_classification_and_visible_filter(self):
        a = self.stock['bodies']['effects\\engines\\fx_a_tiny']
        # the pin sliver to z = +1000 is dropped (area below 0.1 % of the largest face)
        self.assertEqual((a['z_min'], a['z_max'], a['z_extent']), (-1000.0, 0.0, 'negative'))
        self.assertEqual(a['half_width'], [500.0, 250.0])
        b = self.stock['bodies']['effects\\engines\\fx_b']
        self.assertEqual((b['z_min'], b['z_max'], b['z_extent']), (-7000.0, 7000.0, 'both'))
        self.assertEqual(eb.z_extent(-10, 0.4), 'negative')
        self.assertEqual(eb.z_extent(-0.4, 10), 'positive')
        self.assertEqual(eb.z_extent(-1, 1), 'both')

    def test_colour_weighting_blend_and_cluster(self):
        a = self.stock['bodies']['effects\\engines\\fx_a_tiny']
        # alpha weighting: the transparent blue half and the transparent white texel count nothing
        self.assertEqual((a['mean_hex'], a['mean_linear'], a['peak_hex'], a['peak_linear']),
                         ('#ff0000', [1.0, 0.0, 0.0], '#ff0000', [1.0, 0.0, 0.0]))
        self.assertEqual(a['alpha_mean'], 0.5)
        self.assertEqual((a['cluster'], a['effect'], a['material_kind'], a['zwrite']), ('red', 'engine.fx', 'effect', 0))
        self.assertEqual(a['blend'], {'src': 2, 'dst': 4, 'law': 'ONE/INVSRCCOLOR'})
        self.assertEqual(self.stock['bodies']['effects\\engines\\fx_b']['blend']['law'], 'ONE/ONE')
        n = self.stock['bodies']['v\\00566']
        self.assertEqual((n['material_kind'], n['cluster'], n['mean_hex'], n['effect']), ('legacy', 'grey', '#808080', None))
        # linear light differs from the byte-domain mean: 128/255 -> 0.2158605
        self.assertAlmostEqual(n['mean_linear'][0], 0.215861, places=6)
        for label, rgb in eb.CLUSTERS.items():
            self.assertEqual(eb.cluster(rgb, False), label)
        self.assertEqual(eb.cluster((20, 20, 30), False), 'grey')
        self.assertIsNone(eb.cluster(None, False))
        stats = eb.texel_stats(np.zeros((2, 2, 4), np.uint8) + np.array([10, 20, 30, 0], np.uint8))
        self.assertEqual(stats['mean'].tolist(), [10.0, 20.0, 30.0])        # all transparent: uniform weights

    def test_tiers(self):
        self.assertEqual((self.stock['bodies']['effects\\engines\\fx_a_tiny']['tier'],
                          self.stock['bodies']['effects\\engines\\fx_a_tiny']['tier_rule']), ('tiny', 'name'))
        self.assertEqual((self.stock['bodies']['effects\\engines\\fx_b']['tier'],
                          self.stock['bodies']['effects\\engines\\fx_b']['tier_rule']), ('big', 'value'))
        self.assertEqual(eb.tier('x_xtc_red_Big3', 1), ('big3', 'name'))
        self.assertEqual(eb.tier('x', 709), ('tiny', 'value'))
        self.assertEqual(eb.tier('x', 711), ('nor', 'value'))
        self.assertEqual(eb.tier('x', 10 ** 7), ('huge3', 'value'))
        self.assertEqual(eb.tier('x', 0), (None, None))
        for (label, value) in eb.TIERS:
            self.assertEqual(eb.tier('x', value), (label, 'value'))

    def test_missing_array(self):
        self.assertEqual([(m['name'], m['reason'].split(':')[0]) for m in self.stock['missing']],
                         [('effects\\engines\\fx_m3', 'format'), ('effects\\engines\\gone', 'not_found')])
        self.assertIn('MATERIAL3', self.stock['missing'][0]['reason'])
        self.assertEqual(self.stock['counts']['missing'], 2)
        self.assertEqual(self.stock['counts']['bodies'], 3)

    def test_layers_loose_wins_and_stock_only_ignores_mods(self):
        self.assertEqual(self.stock['generated_from']['loose_files'], 0)
        self.assertEqual(self.stock['bodies']['effects\\engines\\fx_b']['source'], 'addon/01.cat')
        mod = self.mod['bodies']
        self.assertEqual(mod['effects\\engines\\fx_b']['source'], 'addon/05.cat')
        self.assertEqual(mod['effects\\engines\\fx_b']['value'], 50000)
        self.assertEqual(mod['effects\\engines\\fx_a_tiny']['source'], 'loose:objects/effects/engines/fx_a_tiny.bob')
        self.assertEqual((mod['effects\\engines\\fx_a_tiny']['value'], mod['effects\\engines\\fx_a_tiny']['z_extent']),
                         (2000, 'positive'))
        self.assertEqual(self.mod['generated_from']['view'], 'installed')
        self.assertEqual(self.mod['generated_from']['catalogues'], 18)

    def test_deterministic_output_without_user_paths(self):
        again = eb.dumps(eb.generate(self.mod_root))
        self.assertEqual(again, eb.dumps(self.mod))
        self.assertNotIn(self.tmp.name, again)
        self.assertEqual(json.loads(again)['schema'], eb.SCHEMA)
        self.assertEqual(list(json.loads(again)), sorted(json.loads(again)))

    def test_cli_writes_and_refuses_the_game_root(self):
        out = Path(self.tmp.name) / 'out' / 'engine_bodies.json'
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(eb.main(['--game-root', str(self.stock_root), '--stock-only', '--out', str(out)]), 0)
            with self.assertRaises(SystemExit):
                eb.main(['--game-root', str(self.stock_root), '--out', str(self.stock_root / 'x.json')])
        self.assertEqual(out.read_text(), eb.dumps(self.stock))
        self.assertFalse((self.stock_root / 'x.json').exists())


CENSUS = ROOT / 'verification/results/engine-effects-census/engine_bodies.csv'


@unittest.skipUnless((bob1.DEFAULT_GAME / 'addon/06.cat').exists() and CENSUS.exists(),
                     'installed Mayhem 3 tree unavailable')
class EngineBodiesInstalled(unittest.TestCase):
    """Counts on the X3 bottle (Mayhem 3 installed), against the bodies ship scenes reference (census CSV)."""

    @staticmethod
    def scene_bodies(view):
        with open(CENSUS, newline='') as fh:
            rows = list(csv.DictReader(fh))
        return {('effects/engines/' + r['body'] if not r['body'].startswith(('v/', 'effects/')) else r['body'])
                .replace('/', '\\').lower() for r in rows if r['view'] == view}

    def test_mayhem_glow_bodies(self):
        t = eb.generate(bob1.DEFAULT_GAME)
        bodies = {k.lower(): v for k, v in t['bodies'].items()}
        scene = self.scene_bodies('mayhem')
        used = [v for k, v in bodies.items() if k in scene]
        glow = [v for v in used if v['effect'] == 'engine.fx']
        self.assertEqual(len(glow), 80)
        self.assertEqual(len({v['cluster'] for v in glow}), 11)
        for v in glow:   # each Mayhem glow texture clusters to its own label
            self.assertEqual('fx_engine_%s.dds' % v['cluster'], v['diffuse'].lower())
        rcs = bodies['v\\00566']
        self.assertEqual((rcs['lists'], rcs['z_extent'], rcs['cluster']), (['jet', 'smalljet'], 'negative', 'grey'))

    def test_stock_bodies(self):
        t = eb.generate(bob1.DEFAULT_GAME, stock_only=True)
        scene = self.scene_bodies('stock')
        used = [v for k, v in t['bodies'].items() if k.lower() in scene]
        self.assertEqual(len(used), 140)
        self.assertEqual(sum(v['cluster'] == 'cyan' for v in used), 110)
        self.assertEqual(sum(v['cluster'] == 'grey' for v in used), 30)


if __name__ == '__main__':
    unittest.main()
