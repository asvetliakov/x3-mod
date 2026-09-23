"""Fleet census for the merged-LOD atlas batch (tools/analysis/lod_batch_census.py) on a synthetic catalogue."""
import copy
import gzip
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
from test_bob1 import atlas_textures, atlas_tree, atlas_tree_lod0


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
        opts = dict(sizes=(1024, 2048), include_other=False, rule=dict(census.RULE), widths=(1920, 1280))
        with tempfile.TemporaryDirectory() as folder:
            game = self.game(folder)
            rows, skipped = census.run(game, opts)
            other, _ = census.run(game, dict(opts, include_other=True))
            tiny, _ = census.run(game, dict(opts, rule=dict(census.RULE, ship_min=1, ship_factor=0)))
        by = {r['name']: r for r in rows}
        self.assertEqual(skipped, ['addon/01.cat'])
        self.assertEqual(sorted(by), ['effects/fx/e', 'others/z/twin', 'ships/x/amb', 'ships/x/capped', 'ships/x/good',
                                      'ships/x/m3', 'ships/x/mixed', 'ships/x/oob', 'ships/x/over', 'ships/x/single',
                                      'ships/x/twin', 'ships/x/uv', 'stations/y/good', 'stations/y/tall'])   # CUT1 scene left out
        refuse = {n: by[n]['refuse'] for n in ('ships/x/uv', 'ships/x/mixed', 'ships/x/oob', 'ships/x/m3',
                                               'ships/x/over', 'ships/x/amb', 'ships/x/capped')}
        self.assertEqual(refuse, {'ships/x/uv': ['uv2'], 'ships/x/mixed': ['mixed_effects'],
                                  'ships/x/oob': ['material_outside_table'], 'ships/x/m3': ['mat3'],
                                  'ships/x/over': ['t_pad_below_t1'], 'ships/x/amb': ['ambiguous_body_ext'],
                                  'ships/x/capped': []})
        self.assertEqual((by['ships/x/capped']['t_pad'], by['ships/x/over']['t_pad']), (200, 200))
        self.assertTrue(by['ships/x/capped']['eligible'])
        e = {r['name']: r for r in other}['effects/fx/e']
        self.assertEqual((e['eligible'], e['t_pad'], e['filter']), (True, 150, []))
        t = {r['name']: r for r in tiny}['ships/x/good']
        self.assertEqual((t['t_pad'], t['refuse']), (1, ['t_pad_below_t1']))
        good = by['ships/x/good']
        self.assertTrue(good['eligible'])                         # its stem peer stations/y/good is refused, so no collision
        self.assertEqual(by['stations/y/good']['refuse'], ['t_pad_below_t1'])
        self.assertEqual((good['lods'], good['thresholds'], good['t_pad'], good['mat']), (2, [5], 80, 'MAT6'))
        self.assertEqual((good['r0_groups'], good['r0_drawn'], good['coarse_groups'], good['c_drawn']), (3, 2, 2, 1))
        self.assertEqual((good['effects'], good['uv2'], good['alpha'], good['slots']),
                         (1, 0, 0, ['diffuse', 'light', 'bump', 'specular']))
        self.assertEqual(list(good['atlas']), [1920, 1280])
        self.assertEqual((good['saved_r0'], good['saved_coarse']), (1, 1))
        a = good['atlas'][1920]
        self.assertEqual((a['size'], a['bytes']), (1024, 699192 + 3 * 1398256))
        self.assertEqual(good['member_bytes'], int(good['r0_bytes'] * 1.2))
        self.assertEqual(by['ships/x/twin']['refuse'], ['batch_stem_collision'])   # both otherwise eligible
        self.assertEqual(by['others/z/twin']['stem_peers'], ['ships/x/twin'])
        self.assertEqual((by['stations/y/tall']['t_pad'], by['stations/y/tall']['refuse']), (150, ['t_pad_below_t1']))
        self.assertEqual(by['ships/x/single']['filter'], ['no_draw_gain'])
        self.assertEqual(by['effects/fx/e']['filter'], ['category_other'])
        self.assertIn('ELIGIBLE', census.format_row(good))

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
