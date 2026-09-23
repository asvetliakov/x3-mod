"""Fleet census for the merged-LOD atlas batch (tools/analysis/lod_batch_census.py) on a synthetic catalogue."""
import contextlib
import copy
import gzip
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'analysis'))
import bob1
import lod_batch_census as census
from sector_fog_census import write_catalogue
from test_bob1 import atlas_textures, atlas_tree, atlas_tree_lod0, text_body


def packed(tree):
    return gzip.compress(bob1.serialise(tree), mtime=0)


def with_threshold(tree, t):
    tree = copy.deepcopy(tree)
    bob1.lods(tree)[1]['value'] = t
    return tree


def variant(kind):
    """atlas_tree_lod0 with one refusal cause."""
    tree = atlas_tree_lod0()
    mats, r0 = bob1.materials(tree), bob1.lods(tree)[0]
    if kind == 'uv2':                                   # point 0 gets a second UV pair (flag 0x04)
        p = r0['points'][0]
        r0['points'][0] = (p[0] | 4,) + p[1:6] + (0, 0) + p[6:]
    elif kind == 'mixed':
        mats[1]['effect'] = b'other.fx'
    elif kind == 'oob':                                 # visible part 0 group -> material 7 of 3
        r0['parts'][0]['groups'][0]['material'] = 7
    elif kind == 'mat3':
        classic = lambda i: {'index': i, 'texture': 0, 'colors': [0] * 12, 'w24': 0, 'w26': 0,
                             'flagword': 0, 'w2c': 0, 'maps': [(0, 0)] * 3}
        tree['sections'] = [('MAT3', [classic(i) for i in range(len(mats))]) if t == 'MAT6' else (t, v)
                            for t, v in tree['sections']]
    return tree


class BatchCensus(unittest.TestCase):
    def game(self, folder):
        game = Path(folder) / 'game'
        write_catalogue(game / '01.cat', atlas_textures())
        write_catalogue(game / '02.cat', [
            ('objects/ships/x/good.pbb', packed(atlas_tree_lod0())),              # 2 drawn -> 1: eligible
            ('objects/ships/x/twin.pbb', packed(atlas_tree_lod0())),              # stem shared with others/z/twin
            ('objects/others/z/twin.pbb', packed(atlas_tree_lod0())),
            ('objects/stations/y/good.pbb', packed(with_threshold(atlas_tree_lod0(), 160))),   # refused peer of ships/x/good
            ('objects/stations/y/tall.pbb', packed(with_threshold(atlas_tree_lod0(), 160))),   # T_1 160 > 150
            ('objects/ships/x/single.pbb', packed(atlas_tree())),                 # one opaque group: no gain
            ('objects/effects/fx/e.pbb', packed(atlas_tree_lod0())),              # other top directory
            ('objects/ships/x/uv.pbb', packed(variant('uv2'))),
            ('objects/ships/x/mixed.pbb', packed(variant('mixed'))),
            ('objects/ships/x/oob.pbb', packed(variant('oob'))),
            ('objects/ships/x/m3.pbb', packed(variant('mat3'))),
            ('objects/ships/x/capped.pbb', packed(with_threshold(atlas_tree_lod0(), 100))),  # 2.5 x 100 -> cap 200
            ('objects/ships/x/over.pbb', packed(with_threshold(atlas_tree_lod0(), 300))),    # cap 200 < T_1 300
            ('objects/ships/x/amb.pbb', packed(atlas_tree_lod0())),               # .pbb and .pbd both exist
            ('objects/ships/x/amb.pbd', b'BODY 0\n'),
            ('objects/cut/00001.pbb', gzip.compress(b'CUT1' + b'\0' * 8, mtime=0))])
        write_catalogue(game / 'addon/01.cat', [('objects/ships/x/good.pbb', gzip.compress(b'junk', mtime=0))])
        (game / 'addon/01.x3m-lod.json').write_text(json.dumps({'bodies': []}))       # installed overlay: skipped
        return game

    def test_rows_refusals_and_costs(self):
        opts = dict(sizes=(1024, 2048), include_other=False, rule=dict(census.RULE, aspect=False),   # class rule
                    widths=(1920, 1280))
        with tempfile.TemporaryDirectory() as folder:
            game = self.game(folder)
            rows, skipped = census.run(game, opts)
            other, _ = census.run(game, dict(opts, include_other=True))
            tiny, _ = census.run(game, dict(opts, rule=dict(opts['rule'], ship_min=1, ship_factor=0)))
        by = {r['name']: r for r in rows}
        self.assertEqual(skipped, ['addon/01.cat'])
        self.assertEqual(sorted(by), ['effects/fx/e', 'others/z/twin', 'ships/x/amb', 'ships/x/capped', 'ships/x/good',
                                      'ships/x/m3', 'ships/x/mixed', 'ships/x/oob', 'ships/x/over', 'ships/x/single',
                                      'ships/x/twin', 'ships/x/uv', 'stations/y/good', 'stations/y/tall'])   # CUT1 scene left out
        refuse = {n: by[n]['refuse'] for n in ('ships/x/uv', 'ships/x/mixed', 'ships/x/oob', 'ships/x/m3',
                                               'ships/x/over', 'ships/x/amb', 'ships/x/capped')}
        self.assertEqual(refuse, {'ships/x/uv': [], 'ships/x/mixed': [],                  # handled, not refused
                                  'ships/x/oob': ['material_outside_table'], 'ships/x/m3': ['mat3'],
                                  'ships/x/over': [], 'ships/x/amb': ['ambiguous_body_ext'],
                                  'ships/x/capped': []})
        self.assertEqual((by['ships/x/capped']['t_pad'], by['ships/x/over']['t_pad']), (200, 200))
        self.assertTrue(by['ships/x/capped']['eligible'])
        self.assertTrue(by['ships/x/over']['eligible'] and by['ships/x/over']['t_pad_below_t1'])   # guard waived
        self.assertIn('T_pad=200<T_1', census.format_row(by['ships/x/over']))
        self.assertEqual((by['ships/x/uv']['uv2'], by['ships/x/uv']['occlusion']), (1, {'argon.fx': 'none'}))
        self.assertEqual((by['ships/x/mixed']['effects'], by['ships/x/mixed']['atlas_materials'],
                          by['ships/x/mixed']['c_drawn']), (2, 2, 2))
        e = {r['name']: r for r in other}['effects/fx/e']
        self.assertEqual((e['eligible'], e['t_pad'], e['filter']), (True, 150, []))
        t = {r['name']: r for r in tiny}['ships/x/good']
        self.assertEqual((t['t_pad'], t['refuse']), (1, ['t_pad_below_2']))
        good = by['ships/x/good']
        self.assertTrue(good['eligible'])
        self.assertTrue(by['stations/y/good']['eligible'])       # same stem as ships/x/good: names are qualified
        self.assertNotEqual(good['atlas_stem'], by['stations/y/good']['atlas_stem'])
        self.assertEqual(len(good['inputs_sha256']), 64)
        self.assertEqual(good['texture_sources'], ['dds/a_bump.pck', 'dds/a_diff.pck', 'dds/b_diff.pck', 'dds/b_light.pck'])
        self.assertEqual((good['lods'], good['thresholds'], good['t_pad'], good['mat']), (2, [5], 80, 'MAT6'))
        self.assertEqual((good['r0_groups'], good['r0_drawn'], good['coarse_groups'], good['c_drawn']), (3, 2, 2, 1))
        self.assertEqual((good['effects'], good['uv2'], good['alpha'], good['slots']),
                         (1, 0, 0, ['diffuse', 'light', 'bump']))          # no t_SpecularTexture: no specular atlas
        self.assertEqual(list(good['atlas']), [1920, 1280])
        self.assertEqual((good['saved_r0'], good['saved_coarse']), (1, 1))
        a = good['atlas'][1920]
        self.assertEqual((a['size'], a['bytes']), (1024, 699192 + 2 * 1398256))
        self.assertEqual(good['member_bytes'], int(good['r0_bytes'] * 1.2))
        self.assertTrue(by['ships/x/twin']['eligible'] and by['others/z/twin']['eligible'])   # no stem collision
        self.assertEqual((by['stations/y/tall']['t_pad'], by['stations/y/tall']['refuse'],
                          by['stations/y/tall']['t_pad_below_t1']), (150, [], True))
        self.assertEqual(by['ships/x/single']['filter'], ['no_draw_gain'])
        self.assertEqual(by['effects/fx/e']['filter'], ['category_other'])
        self.assertIn('ELIGIBLE', census.format_row(good))

    def test_text_bodies(self):
        opts = dict(sizes=(1024, 2048), include_other=False, rule=dict(census.RULE), widths=(1920,))
        with tempfile.TemporaryDirectory() as folder:
            game = Path(folder) / 'game'
            write_catalogue(game / '01.cat', atlas_textures())
            write_catalogue(game / '02.cat', [
                ('objects/ships/x/good.pbb', packed(atlas_tree_lod0())),
                ('objects/ships/t/good.pbd', gzip.compress(text_body(atlas_tree_lod0()), mtime=0)),   # bump-mapped
                ('objects/stations/t/plain.bod', text_body(atlas_tree_lod0())),      # unpacked text member
                ('objects/ships/t/mat3.pbd', b'MATERIAL3: 1; 71; 1;2;3; 4;5;6; 7;8;9; 1; 100; 25; 5; 0;0;1; 100;'
                                             b' 3;4; 5;6;\n1;\n0; 0; 1;\n-1; -1; -1;\n1; 0; 0; 0; -1;\n-99; 1;\n-99; 0;\n'),
                ('objects/ships/t/bad.pbd', b'BODY 0\n'),
                ('objects/ships/t/binary.pbd', packed(atlas_tree_lod0())),          # BOB1 bytes under a text name
                ('objects/ships/t/scene.pbd', b'VER: 3;\nP 0; B ships\\x\\good; b\n')])
            rows, _ = census.run(game, opts, include_text=True)
            binary_only, _ = census.run(game, opts)
        by = {r['name']: r for r in rows}
        self.assertEqual(sorted(by), ['ships/t/bad', 'ships/t/binary', 'ships/t/good', 'ships/t/mat3', 'ships/x/good',
                                      'stations/t/plain'])                        # the text scene is skipped
        self.assertEqual(by['ships/t/mat3']['refuse'], ['mat3'])                   # global-material text body
        self.assertEqual([r['name'] for r in binary_only], ['ships/x/good'])
        text, binary = by['ships/t/good'], by['ships/x/good']
        self.assertTrue(text['eligible'] and text['text'] and 'text' not in binary)
        same = ('lods', 'thresholds', 't_pad', 'aspect_k', 'mat', 'r0_faces', 'r0_points', 'r0_drawn', 'c_drawn', 'slots', 'tiles')
        self.assertEqual({k: text[k] for k in same}, {k: binary[k] for k in same})
        self.assertNotEqual(text['atlas_stem'], binary['atlas_stem'])              # qualified by the member path
        self.assertTrue(by['stations/t/plain']['eligible'])
        self.assertEqual((by['ships/t/bad']['refuse'], by['ships/t/binary']['refuse']),
                         (['text_parse_error'], ['text_parse_error']))
        self.assertIn('binary data', by['ships/t/binary']['atlas_error'])
        self.assertIn(' text refuse=- filter=- ELIGIBLE', census.format_row(text))
        self.assertIn('+ 5 text bodies (.pbd/.bod, scenes skipped)', census.summary(rows, [], opts, {})[0])

    def test_aspect_rule(self):
        box = lambda ex, ey, ez: [(1, sx * ex, sy * ey, sz * ez) for sx in (-1, 1) for sy in (-1, 1) for sz in (-1, 1)]
        k, note = census.aspect_k(box(5, 5, 5))                                           # cube
        self.assertEqual((round(k, 12), note), (1.0, ''))
        k, note = census.aspect_k(box(100, 36, 23) + [(0, 900, 900, 900)])               # flag 0: no position
        self.assertAlmostEqual(k, 1.44, places=2)
        k, note = census.aspect_k(box(100, 100, 0))                                       # flat plane
        self.assertAlmostEqual(k, (2 ** 0.5) / (3 ** 0.5))
        self.assertIn('1 zero extent', note)
        self.assertEqual(census.aspect_k([(1, 7, 7, 7)]), (1.0, 'zero extent on every axis'))
        self.assertEqual(census.aspect_k([(0, 1, 2, 3)]), (1.0, 'no positions'))
        t = census.t_aspect
        self.assertEqual((t('ship', 80, 1.4), t('ship', 80, 1.6), t('ship', 80, 0.8)), (112, 120, 80))   # cap 1.5
        self.assertEqual((t('station', 150, 1.44), t('station', 150, 1.856), t('station', 150, 2.5)),
                         (216, 278, 300))                                                 # cap 2.0
        self.assertEqual(t('other', 150, 2.5), 300)                                       # station rule
        self.assertEqual(t('station', 150, 1.856, dict(census.RULE, aspect=False)), 150)  # --no-aspect
        self.assertEqual(t('ship', 80, 1.6, dict(census.RULE, aspect_ship=1.25)), 100)
        with tempfile.TemporaryDirectory() as folder:
            log = Path(folder) / 'r.txt'
            log.write_text('  4 draws 108429 prim s=80,d=8955643,r=1132932,lod=1,kept s=81,d=1,r=1000,lod=1,kept'
                           ' body=stations\\station_scenes\\others\\argon_equipmentdock\n'
                           'cull_census device=1 frame=2 view=main node=1 model=2 s=3 measure=4 d=5 radius=77 lod=0'
                           ' verdict=kept lods=2 thr=100000,5 body=ships\\x\\good\nno body here r=5\n')
            radii = census.world_radii([log])
            self.assertEqual(radii, {'objects/stations/station_scenes/others/argon_equipmentdock': 1132932,
                                     'objects/ships/x/good': 77})
            row = dict(name='stations/station_scenes/others/argon_equipmentdock', t_class=150, t_pad=216, aspect_k=1.44)
            census.attach_world([row], radii)
            self.assertAlmostEqual(row['switch_km_class'], 9.572, places=3)             # 1132932 * 640 / 150 / 505
            self.assertAlmostEqual(row['switch_km'], 6.647, places=3)
            self.assertIn('k=1.44 T_class=150 T=216 r_body=0 r_world=1132932 D=9.57->6.65 km', census.aspect_text(row))
            game = self.game(folder)
            on, _ = census.run(game, dict(sizes=(1024, 2048), include_other=False, rule=dict(census.RULE),
                                          widths=(1920,)), only={'objects/ships/x/good', 'objects/stations/y/tall'})
        by = {r['name']: r for r in on}
        k = census.aspect_k(bob1.lods(atlas_tree_lod0())[0]['points'])[0]
        good, tall = by['ships/x/good'], by['stations/y/tall']
        self.assertEqual((good['aspect_k'], good['t_class'], good['t_pad'], good['threshold_aspect']),
                         (round(k, 4), 80, round(80 * min(max(k, 1), 1.5)), good['t_pad']))
        self.assertEqual((tall['t_class'], tall['t_pad']), (150, round(150 * min(max(k, 1), 2.0))))
        self.assertEqual(tall['t_pad_below_t1'], 160 > tall['t_pad'])                   # the guard sees T_aspect
        self.assertIn(f'k={k:.2f} T_class=80 T={good["t_pad"]}', census.format_row(good))
        import lod_overlay
        self.assertEqual(lod_overlay.build_parser().parse_args(['--batch']).aspect_cap, (1.5, 2.0))
        self.assertEqual(lod_overlay.build_parser().parse_args(['--batch', '--aspect-cap', '1.2,1.8']).aspect_cap,
                         (1.2, 1.8))
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            lod_overlay.build_parser().parse_args(['--batch', '--aspect-cap', '0.5,2'])

    def test_texel_fallback(self):
        """T_fb = round(T * weighted / W), rebuilt until weighted >= W; guards T_1, T_pad/4 and 2; W not reached."""
        linear = lambda t: (dict(weighted_texels_per_px=0.468 * 119 / t, starved_share=0.0 if 0.468 * 119 / t >= 1
                                 else 0.34, refuse=0.468 * 119 / t < 0.5), 1024)
        x0 = dict(weighted_texels_per_px=0.468, starved_share=0.34, refuse=True)
        fb = census.texel_fallback(linear, 119, 30, x0, 0.5, 0.10, 1.0)
        self.assertEqual((fb['accepted'], fb['t_fb'], [s['t'] for s in fb['steps']]), (True, 55, [56, 55]))
        self.assertLess(fb['steps'][0]['weighted'], 1.0)                 # 0.9945 at 56: one more step
        self.assertAlmostEqual(fb['weighted_after'], 0.468 * 119 / 55)
        fb = census.texel_fallback(linear, 119, 60, x0, 0.5, 0.10, 1.0)  # 56 < T_1 60: refused
        self.assertEqual((fb['accepted'], fb['guard'], fb['guard_t'], fb['steps']), (False, 'T_1', 56, []))
        fb = census.texel_fallback(linear, 300, None, dict(x0, weighted_texels_per_px=0.028), 0.5, 0.10, 1.0)
        self.assertEqual((fb['accepted'], fb['guard'], fb['guard_t'], fb['guard_floor']), (False, 'relative', 8, 75))
        self.assertIn('guard relative (T_fb 8 < 75)', census.fallback_text(fb))   # torus_barrier_node: 300 -> 8
        fb = census.texel_fallback(linear, 6, None, dict(x0, weighted_texels_per_px=0.1), 0.5, 0.10, 1.0)
        self.assertEqual((fb['accepted'], fb['guard']), (False, 'min_2'))   # single-record body: engine s >= 2
        flat = lambda t: (dict(weighted_texels_per_px=0.9, starved_share=0.4, refuse=True), 2048)
        fb = census.texel_fallback(flat, 119, 2, dict(x0, weighted_texels_per_px=0.9), 0.5, 0.10, 1.0)
        self.assertEqual((fb['accepted'], fb['guard'], [s['t'] for s in fb['steps']]), (False, None, [107, 96, 86]))
        self.assertIn('W 1 not reached in 3 steps (last T 86 weighted 0.900; steps 107:0.900,96:0.900,86:0.900)',
                      census.fallback_text(fb))
        row = dict(name='ships/x/a', t_class=80, t_pad=55, aspect_k=1.49,
                   texel_fallback=census.texel_fallback(linear, 119, 30, x0, 0.5, 0.10, 1.0))
        census.attach_world([row], {'objects/ships/x/a': 505000})
        self.assertAlmostEqual(row['texel_fallback']['km_after'], 505000 * 640 / 55 / 505 / 1000)
        self.assertIn('T=119->55(texel_fallback)', census.aspect_text(row))
        self.assertIn('T 119->55 weighted 0.468->1.013 starved 34.0%->0.0% size None->1024 D 5.38->11.64 km'
                      ' steps 56:0.995,55:1.013',
                      census.fallback_text(row['texel_fallback']))
        with tempfile.TemporaryDirectory() as folder:                    # through census_body at 1920 wide
            game = self.game(folder)
            base = dict(sizes=(1024, 2048), include_other=False, rule=dict(census.RULE), widths=(1920,))
            only = {'objects/ships/x/good', 'objects/stations/y/tall'}
            on, _ = census.run(game, dict(base, texel=dict(min_texels=0.5, floor_share=0.1, fallback=1.0)), only=only)
            off, _ = census.run(game, base, only=only)                   # no texel options: no fallback
        on, off = ({r['name']: r for r in rows} for rows in (on, off))
        good = on['ships/x/good']
        self.assertTrue(good['texel_fallback']['accepted'])
        self.assertEqual((good['t_pad'], good['threshold_aspect']), (good['texel_fallback']['t_fb'],
                                                                     off['ships/x/good']['t_pad']))
        self.assertGreaterEqual(good['atlas'][1920]['weighted_ratio'], 1.0)
        self.assertLess(off['ships/x/good']['atlas'][1920]['weighted_ratio'], 0.5)
        self.assertNotIn('texel_fallback', off['ships/x/good'])
        self.assertEqual(on['stations/y/tall']['texel_fallback']['guard'], 'T_1')   # T_1 160
        self.assertEqual(on['stations/y/tall']['t_pad'], off['stations/y/tall']['t_pad'])
        self.assertIn('texel_fallback="T ', census.format_row(good))

    def test_rule_sizes_and_sector_parse(self):
        self.assertEqual([census.t_pad('ship', t) for t in ([], [30], [60], [100])], [80, 80, 150, 200])
        self.assertEqual(census.t_pad('station', [250]), 150)
        self.assertEqual([census.dds_bytes(n, f) for n, f in ((1024, 'DXT1'), (1024, 'DXT5'), (2048, 'DXT1'),
                                                              (2048, 'DXT5'))], [699192, 1398256, 2796344, 5592560])
        self.assertEqual(census.category('objects/Stations/others/x.pbb'), 'station')
        text = ('== frame 1 node_draws=9\nnode model draws\n'
                '2870d5b8 00005434    38    10     28 [0] 17,0,kept 4 100000,30 ships\\argon\\argon_TL\n'
                '00000000 00000000     8     0      8 [0] - - - -\n'
                '== frame 2 node_draws=5\n'
                'N 288dcac8 00005434   38  10 [0] 17,0,kept 4 100000,30,15,5 ships\\argon\\argon_TL\n'
                'B   38  10   1 00005434 ships\\argon\\argon_TL\nB 2 0 1 00000000 -\n')
        with tempfile.TemporaryDirectory() as folder:
            p = Path(folder) / 'c.txt'
            p.write_text(text)
            self.assertEqual(census.parse_census(p, '1'), {'objects/ships/argon/argon_tl': 38})
            self.assertEqual(census.parse_census(p, '2'), {'objects/ships/argon/argon_tl': 38})


if __name__ == '__main__':
    unittest.main()
