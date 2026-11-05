"""tools/effects/weapon_effects_census.py: TBullets/Effects/TShields/TShips parsers, body materials and extents,
the MATERIAL3 fallback and the census join, on inline fixtures (no catalogue access)."""
import importlib.util
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import bob1  # noqa: E402

_spec = importlib.util.spec_from_file_location('weapon_effects_census', ROOT / 'tools/effects/weapon_effects_census.py')
wec = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(wec)


def bullet_row(body, flag, flight, impact, muzzle, wid, trail=0, col24=0):
    f = ['0'] * 50
    f[0], f[7], f[10], f[11], f[12], f[22] = body, '4500', '750000', '2600', str(flag), '18000'
    f[13:19] = ['255', '128', '0', '2.0', '3.0', '5.0']
    f[19], f[20], f[21], f[23], f[24] = str(flight), str(impact), str(muzzle), str(trail), str(col24)
    f[48], f[49] = wid, ''
    return ';'.join(f)


TBULLETS = '\r\n'.join(['// Created with X3 Editor 2', '55;3;',
                        bullet_row('effects\\weapons\\bolt', 0, 0, 202, 0, 'SS_BULLET_A', col24=122),
                        bullet_row('effects\\weapons\\bolt', 162, 308, 202, 122, 'SS_BULLET_B', trail=2),
                        bullet_row('10385', 0x101, -1, 999, 0, 'SS_BULLET_C', trail=5)]) + '\r\n'

EFFECTS = """// Effect file
1;
// effectnr;numelements;effectflags;
202;2;0;0;1.0; // Laser, Impact
  EEDF_FADEOUT|EEDF_GROWANDSHRINK|EEDF_TIMEOUT ;0; // idx 0
    589;0.0;0.015;500;0.0;0;0;0;-1;30;0.25;0;
  EEDF_LIGHT|EEDF_TIMEOUT|EEDF_PARTICLEGENERATOR;I3DF_2D; // idx 1
    -1;0.001;0.01;500;0.1;0;0;50;17;0;0.25;0;
308;1;EDF_NODRIFT|EDF_PROPAGATE;30000;1.0;
  EEDF_PROPAGATE|EEDF_TIMEOUT;0;
    142;1;3;10;20;1;500;1.0;0;
122;2;0;0;1.0; // truncated: one element only
  EEDF_TIMEOUT;0;
    7;0.1;0.2;100;0.0;0;0;0;-1;0;0.25;0;
48;1;0;0;1.0; // default death row
  EEDF_TIMEOUT;0;
    7;0.1;0.2;100;0.0;0;0;0;-1;0;0.25;0;
600;0;EDF_HIDEOBJECT;0;1.0;
37;0;0;0;1.0;
59;0;0;0;1.0;
"""

TSHIELDS = '47;1;\n27;154;0;0;0;0;2713;100000;1000;1;1.0;1;8;20;20;1;8;0;0;0;SS_SHIELD_A;\n'


def ship_row(cls, engine, death=600):
    f = ['0'] * 60
    f[11], f[47], f[52] = str(engine), str(death), cls
    return ';'.join(f) + ';'


TSHIPS = '\n'.join(['50;5;', ship_row('OBJ_SHIP_M5', 3), ship_row('OBJ_SHIP_M5', 4), ship_row('OBJ_SHIP_M5', 3, 0),
                    ship_row('OBJ_SHIP_TL', 9, 37), ship_row('2061', 1), '0;short;row']) + '\n'


def t_row(n, column, row, name):
    f = ['0'] * n
    f[column], f[-1] = str(row), name
    return ';'.join(f) + ';'


TDOCKS = '\n'.join(['47;2;', t_row(29, 14, 37, 'SS_DOCK_A'), t_row(29, 14, 37, 'SS_DOCK_B')]) + '\n'
TFACTORIES = '\n'.join(['47;1;', t_row(30, 14, 0, 'SS_FAC_A')]) + '\n'
TASTEROIDS = '\n'.join(['47;1;', t_row(22, 8, 59, 'SS_ASTEROID_01')]) + '\n'
TMISSILES = '\n'.join(['50;1;', t_row(38, 21, 0, 'SS_MISSILE_A')]) + '\n'
PARTICLES = """// FileVersion;
1;
2; 2; NULL; // idx 2
  PEDF_2D; 395;0.0;0.5;0.5;1.0;0.5; 0.1; 0.0; 0.0; -1.0;  100; 600;1000; 600; 1400; fadeduration: 500;
//  PEDF_2D; 999;0.0;0.5;0.5;1.0;0.5; 0.1; 0.0; 0.0; -1.0;  100; 600;1000; 600; 1400;
  PEDF_2D|PEDF_TRAIL;406;0;0;0.25;0.25;0.36;0.1;0.0;0.0;-1.0;50;1000;500;2967;8900; lifetimevariation: 20;
"""


def effect_body(points, tex=b'fx_bolt_diff.dds'):
    mat = dict(index=0, flags=bob1.EFFECT_MATERIAL, technique=1, effect=b'effects.fx', params=[
        (b'g_srcblend', 0, [2]), (b'g_destblend', 0, [4]), (b'g_alphablendenable', 1, [1]),
        (b'g_zwriteenable', 1, [0]), (b't_DiffuseTexture', 8, tex), (b't_BumpTexture', 8, b'NULL'),
        (b'effectfile', 8, b'effects.fx')])
    lod = dict(value=3137, flags=0, points=[(1,) + p for p in points], parts=[])
    return bob1.serialise({'sections': [('MAT6', [mat]), ('BODY', [lod])]})


BOLT = effect_body([(-100, -50, -2000), (100, 50, 2000), (0, 0, 0)])
SPRITE = effect_body([(-65536, -65536, 0), (65536, 65536, 0)], tex=b'Effects\\Spark.TGA')


class FakeAssets:
    def __init__(self, files):
        self.files = files
        self.entries = {'tex/true/406.jpg': [], 'addon/tex/true/395.dds': [], 'tex/true/40.jpg': []}

    def get(self, path):
        data = self.files[path].encode('latin1')
        return data, dict(source='01.cat', member=path, decoded_sha256='ab' * 32)


class Parsers(unittest.TestCase):
    def test_tbullets_columns(self):
        rows = wec.parse_tbullets(TBULLETS)
        self.assertEqual([r['weapon_id'] for r in rows], ['SS_BULLET_A', 'SS_BULLET_B', 'SS_BULLET_C'])
        r = rows[1]
        self.assertEqual((r['speed'], r['lifetime_ms'], r['flag_word']), (2600, 750000, 162))
        self.assertEqual(r['flag_bits'], ['beam_gun_tied', 'hull_damage_through_shields', 'repair'])
        self.assertEqual((r['light_colour'], r['size'], r['beam_length']), ([255, 128, 0], [2.0, 3.0], 5.0))
        self.assertEqual((r['flight_effect'], r['impact_effect'], r['muzzle_effect'], r['trail_particles']),
                         (308, 202, 122, 2))
        self.assertEqual(rows[0]['col24_not_effect'], 122)
        self.assertEqual((r['damage_shield'], r['damage_hull'], r['damage_per_shot']), (4500, 18000, 18000))
        shield_heavy = wec.parse_tbullets('55;1;\n' + bullet_row('b', 0, 0, 0, 0, 'SS_BULLET_D').replace(
            ';4500;', ';90000;', 1) + '\n')[0]
        self.assertEqual((shield_heavy['damage_shield'], shield_heavy['damage_per_shot']), (90000, 90000))
        self.assertEqual(rows[2]['flag_bits'], ['unknown_0x1', 'area_flak'])
        with self.assertRaises(ValueError):
            wec.parse_tbullets('55;1;\nbody;1;2;\n')

    def test_effects_grammar(self):
        eff = wec.parse_effects(EFFECTS)
        self.assertEqual(sorted(eff), [37, 48, 59, 122, 202, 308, 600])
        e = eff[202]
        self.assertEqual((e['numelements'], e['effectflags'], e['header_extra']), (2, 0, [0, 1.0]))
        a, b = e['elements']
        self.assertEqual(a['flag_names'], ['EEDF_FADEOUT', 'EEDF_GROWANDSHRINK', 'EEDF_TIMEOUT'])
        self.assertEqual((a['bodyid'], a['minsize'], a['maxsize'], a['duration'], a['lensflarenr'], a['light']),
                         (589, 0.0, 0.015, 500, -1, False))
        self.assertEqual((b['light'], b['particlenr'], b['i3dflags'], b['zoffset']), (True, 17, 'I3DF_2D', 50))
        self.assertNotIn('lensflarenr', b)
        p = eff[308]
        self.assertEqual(p['effectflags'], 'EDF_NODRIFT|EDF_PROPAGATE')
        self.assertEqual((p['elements'][0]['prop_effect_id'], p['elements'][0]['prop_steps']), (142, 1.0))
        self.assertEqual(len(eff[122]['elements']), 1)
        self.assertIn('warning', eff[122])

    def test_shields_and_ships(self):
        s = wec.parse_tshields(TSHIELDS)
        self.assertEqual((len(s), s[0]['shield_id'], s[0]['fields'][6]), (1, 'SS_SHIELD_A', 2713))
        c = wec.tships_engine_classes(TSHIPS)
        self.assertEqual(c['rows'], 5)
        self.assertEqual(c['distinct_engine_effects_per_class'], {'OBJ_SHIP_M5': 2, 'OBJ_SHIP_TL': 1, 'class_id:2061': 1})
        self.assertEqual(wec.ship_death_rows(TSHIPS), {
            'OBJ_SHIP_M5': {'48': 1, '600': 2}, 'OBJ_SHIP_TL': {'37': 1}, 'class_id:2061': {'600': 1}})
        self.assertEqual(wec.death_column(TFACTORIES, 14), [('SS_FAC_A', 48)])
        self.assertEqual(wec.death_column(TASTEROIDS, 8), [('SS_ASTEROID_01', 59)])

    def test_particles(self):
        p = wec.parse_particles(PARTICLES)
        self.assertEqual(list(p), [2])
        self.assertEqual([e['mat_id'] for e in p[2]], [395, 406])
        self.assertEqual((p[2][1]['flags'], p[2][1]['lifetime'], p[2][1]['size']), (['PEDF_2D', 'PEDF_TRAIL'], 500, [2967, 8900]))

    def test_body_name_rule(self):
        self.assertEqual(wec.element_body_name(589), 'v\\00589')
        self.assertEqual(wec.element_body_name('10385'), 'v\\10385')
        self.assertIsNone(wec.element_body_name(-1))
        self.assertEqual(wec.element_body_name('effects\\x'), 'effects\\x')


class Bodies(unittest.TestCase):
    def test_effect_material_and_extents(self):
        b = wec.body_summary(BOLT)
        m = b['materials'][0]
        self.assertEqual((m['kind'], m['effect'], m['blend'], m['zwrite']), ('effect', 'effects.fx', 'ONE/INVSRCCOLOR', False))
        self.assertEqual(m['textures'], {'t_DiffuseTexture': 'fx_bolt_diff.dds', 't_BumpTexture': 'NULL'})
        self.assertEqual(b['extents']['half_extents'], [100.0, 50.0, 2000.0])
        self.assertEqual(b['extents']['z_over_xy'], 20.0)
        self.assertEqual(wec.body_textures(b), ['fx_bolt_diff.dds'])

    def test_material3_rows(self):
        data = b'/=== x\nMATERIAL3: 0;131; 150;150;150; 1;0;\nMATERIAL3: 1;7;\n'
        self.assertEqual(wec.material3_records(data), [
            dict(kind='material3', index=0, texture_id_inferred=131),
            dict(kind='material3', index=1, texture_id_inferred=7)])
        self.assertEqual(wec.body_textures({'materials': wec.material3_records(data)}), ['id:131', 'id:7'])

    def test_unparseable_body(self):
        self.assertIn('error', wec.body_summary(b'BOB1garbage'))


class Census(unittest.TestCase):
    def test_join(self):
        assets = FakeAssets({'types/TBullets.txt': TBULLETS, 'types/Effects.txt': EFFECTS,
                             'types/TShields.txt': TSHIELDS, 'types/TShips.txt': TSHIPS, 'types/TDocks.txt': TDOCKS,
                             'types/TFactories.txt': TFACTORIES, 'types/TAsteroids.txt': TASTEROIDS,
                             'types/TMissiles.txt': TMISSILES, 'types/Particles3.txt': PARTICLES})
        store = {'objects/effects/weapons/bolt': BOLT, 'objects/v/00589': SPRITE, 'objects/v/00007': SPRITE}

        def read(name):
            key = bob1.body_stem(name).lower()
            if key not in store:
                raise FileNotFoundError(key)
            return store[key], '01.cat:' + key

        r = wec.census(assets, read_body=read, hint=lambda name: ['objects/cut/10385.bod'])
        self.assertEqual(sorted(r['effects'], key=int), ['37', '48', '59', '122', '202', '308', '600', '999'])
        self.assertTrue(r['effects']['999']['missing'])
        self.assertEqual([w['death_effect'] for w in r['weapons']], [None, None, 999])
        d = r['death_rows']
        self.assertEqual(d['rows'], [37, 48, 59, 600, 999])
        self.assertEqual((d['TDocks'], d['TFactories'], d['TMissiles']), ({'37': 2}, {'48': 1}, {'48': 1}))
        self.assertEqual(d['TAsteroids'], [dict(id='SS_ASTEROID_01', row=59)])
        self.assertEqual(d['TBullets_area_flak'], [dict(id='SS_BULLET_C', row=999)])
        self.assertEqual(d['ship_classes']['OBJ_SHIP_M5'], {'48': 1, '600': 2})
        self.assertEqual(r['effects']['600']['effectflags'], 'EDF_HIDEOBJECT')
        t = r['trails']
        self.assertEqual(sorted(t), ['2', '5'])
        self.assertTrue(t['5']['missing'])
        self.assertEqual([e['texture_members'] for e in t['2']['emitters']], [['addon/tex/true/395.dds'], ['tex/true/406.jpg']])
        self.assertEqual(r['effects']['202']['elements'][0]['body_key'], 'objects/v/00589')
        self.assertNotIn('body_key', r['effects']['202']['elements'][1])
        self.assertEqual(r['weapons'][2]['body_key'], 'objects/v/10385')
        self.assertEqual(r['bodies']['objects/v/10385']['other_members_hint'], ['objects/cut/10385.bod'])
        self.assertEqual(r['bodies']['objects/v/00589']['textures'], ['effects\\spark.tga'])
        self.assertEqual(r['tables']['TBullets']['sha256'], 'ab' * 8)
        text = wec.summary('fixture', r)
        self.assertIn('weapons=3 distinct bullet bodies=2 distinct impact rows=1 flight rows=1 muzzle rows=1 trails=2', text)
        self.assertIn('distinct textures=2', text)
        self.assertIn('flag word values=[0, 162, 257]', text)
        self.assertIn('M5:48x1,600x2', text)


if __name__ == '__main__':
    unittest.main()
