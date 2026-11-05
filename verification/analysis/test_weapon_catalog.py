"""tools/effects/weapon_catalog.py: text-id resolution, TLaser/TShips/TCockpits joins, the size rule, the description
and the markdown emitter, on inline fixtures (no catalogue access)."""
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
_spec = importlib.util.spec_from_file_location('weapon_catalog', ROOT / 'tools/effects/weapon_catalog.py')
wc = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(wc)

BASE_T = b'''<?xml version="1.0" encoding="UTF-8"?><language id="44">
<page id="17"><t id="8003">Old Ray</t><t id="4953">Mass Driver</t><t id="10023">Experimental</t></page>
<page id="350017"><t id="8003">Impulse Ray Emitter</t><t id="18103">{17,10023} {17,8013}</t>
<t id="8013">(PAC)Particle Accelerator Cannon</t><t id="8023">Ion \\(Mk2\\)</t><t id="8033">{17,9999}</t></page>
</language>'''
MOD_T = b'''<?xml version="1.0" encoding="UTF-8"?><language id="44">
<page id="17"><t id="4953">Mass Driver Mk2</t></page></language>'''

# TLaser: cols 5 subtype, 6 name id, 7 refire ms, 9 bullet index; the id is the last SS_ field.
TLASER = '\n'.join(['// TLaser', '50;4;',
                    '27;145;0.42;0.42;0;SG_LASER_IRE;8003;150;913;0;450;0.025;ICON;SS_LASER_IRE;',
                    '27;146;0.34;0.34;0;SG_LASER_PAC;18103;0;959;1;900;0.025;ICON;SS_LASER_PAC_1;',
                    '27;146;0.34;0.34;0;SG_LASER_PPC;8033;4000;959;1;900;0.025;ICON;SS_LASER_PPC;',
                    '27;146;0.34;0.34;0;SG_LASER_WHAT;0;500;959;7;900;0.025;ICON;SS_LASER_ODD;'])
TCOCKPITS = '\n'.join(['51;3;', '0;0;0;0;0;0;0;4512;0;SS_COCKPIT_DEFAULT;',
                       '0;0;0;0;0;0;0;4512;2;SS_COCKPIT_A;',                     # bit 1 = SG_LASER_PAC
                       '0;0;0;0;0;0;0;4512;-1048576;SS_COCKPIT_K;'])              # bits 20..31: PPC (20) ...


def ship(cls, main_mask, cockpits, sid):
    f = ['0'] * 54
    f[18] = str(main_mask)
    for k, c in enumerate(cockpits):
        f[31 + 2 * k] = str(c)
    f[52], f[53] = cls, sid
    return ';'.join(f) + ';'


TSHIPS = '\n'.join(['50;6;', ship('OBJ_SHIP_M5', 1, [], 'SS_SH_A_M5'), ship('OBJ_SHIP_M3', 1, [1], 'SS_SH_A_M3'),
                    ship('OBJ_SHIP_M6', 0, [1, 0], 'SS_SH_A_M6'), ship('OBJ_SHIP_M2', 0, [2], 'SS_SH_A_M2'),
                    ship('OBJ_SHIP_M5', -1, [], 'SS_SH_TURRET'), ship('2061', 1, [9], 'SS_SH_ODD')])


def census():
    def weapon(i, wid, flags, life, speed, light, impact=0):
        return dict(index=i, weapon_id=wid, body=f'effects\\b{i}', body_key=f'objects/effects/b{i}',
                    damage_shield=100 * (i + 1), damage_hull=10 * (i + 1), lifetime_ms=life, speed=speed,
                    flag_word=flags, flag_bits=wc.wec.flag_bits(flags), light_colour=light, trail_particles=0,
                    flight_effect=0, impact_effect=impact, muzzle_effect=-1,
                    death_effect=impact if flags & 0x100 else None)
    return dict(
        weapons=[weapon(0, 'SS_BULLET_IRE', 0, 1200, 581000, [48, 16, 300], impact=15),
                 weapon(1, 'SS_BULLET_PAC', 0x2, 1000, 1000000, [0, 0, 0], impact=40),
                 weapon(2, 'SS_BULLET_FRAG', 0x100, 300, 250000, [255, 255, 255], impact=15)],
        bodies={'objects/effects/b0': dict(extents=dict(z_over_xy=18.5), textures=['fx_alpha8.tga', 'fx_diff.tga']),
                'objects/effects/b1': dict(extents=dict(z_over_xy=1.0), textures=['none_white']),
                'objects/v/10663': dict(textures=['-73.tga'])},
        effects={'15': dict(elements=[
                    dict(flag_names=['EEDF_TIMEOUT', 'EEDF_GROW'], body_key='objects/v/10663', minsize=0.04,
                         maxsize=0.06, duration=800),
                    dict(flag_names=['EEDF_LIGHT', 'EEDF_TIMEOUT'], minsize=0.01, maxsize=0.2, duration=250),
                    dict(flag_names=['EEDF_PROPAGATE'], prop_effect_id=184, min_props=1, max_props=3)]),
                 '40': dict(missing=True)},
        tables=dict(TBullets=dict(source='a.cat', member='types/TBullets.pck', sha256='0' * 16)))


def resolve():
    return wc.resolver(wc.text_table([BASE_T, MOD_T]))


def view():
    return wc.build_view('test', census(), TLASER, TSHIPS, TCOCKPITS, resolve(), dict(
        TBullets='a.cat:types/TBullets.pck 0', TLaser='a', TShips='b', TCockpits='c', text=['t/0001-l044.xml']))


class TextTest(unittest.TestCase):
    def test_page_versions_references_and_comments(self):
        r = resolve()
        self.assertEqual(r(17, 8003), 'Impulse Ray Emitter')                 # 350017 beats 17
        self.assertEqual(r(17, 4953), 'Mass Driver Mk2')                     # a later file overrides
        self.assertEqual(r(17, 18103), 'Experimental Particle Accelerator Cannon')
        self.assertEqual(r(17, 8013), 'Particle Accelerator Cannon')         # (comment) dropped
        self.assertEqual(r(17, 8023), 'Ion (Mk2)')                           # escaped parentheses kept
        self.assertIsNone(r(17, 8033))                                       # dangling reference
        self.assertIsNone(r(17, 1))

    def test_text_file_order(self):
        class A:
            expansion = 'addon'
            entries = {k: [] for k in ('t/0001-l044.xml', 'addon/t/7325-l044.xml', 'addon/t/0001-l044.xml',
                                       't/0002-l049.xml', 't/0003-l044.xml', 'addon2/t/0001-l044.xml')}
        self.assertEqual(wc.text_files(A()), ['t/0001-l044.xml', 't/0003-l044.xml', 'addon/t/0001-l044.xml',
                                              'addon/t/7325-l044.xml'])


class TableTest(unittest.TestCase):
    def test_tlaser(self):
        lasers = wc.parse_tlaser(TLASER, resolve())
        self.assertEqual([(l['id'], l['bullet'], l['subtype_bit'], l['shots_per_min']) for l in lasers],
                         [('SS_LASER_IRE', 0, 0, 400.0), ('SS_LASER_PAC_1', 1, 1, None), ('SS_LASER_PPC', 1, 20, 15.0),
                          ('SS_LASER_ODD', 7, None, 120.0)])
        self.assertEqual([l['name'] for l in lasers], ['Impulse Ray Emitter', 'Experimental Particle Accelerator Cannon',
                                                       None, None])

    def test_ship_masks(self):
        ships, bad, excluded = wc.ship_masks(TSHIPS, wc.parse_tcockpits(TCOCKPITS))
        self.assertEqual(ships, [('M5', 1), ('M3', 3), ('M6', 2), ('M2', 0xfff00000), ('class_id:2061', 1)])
        self.assertEqual((bad, excluded), (1, 1))                            # cockpit index 9 out of range; turret row
        self.assertEqual(wc.used_by(1, ships), {'M3': 1, 'M6': 1})
        self.assertEqual(wc.used_by(None, ships), {})


class SizeTest(unittest.TestCase):
    def test_rule(self):
        self.assertEqual(wc.size_of({'M5': 3, 'M2': 10}), 'small')
        self.assertEqual(wc.size_of({'M3': 1, 'M6': 4, 'M2': 9}), 'medium')    # 1 of 14 is an outlier
        self.assertEqual(wc.size_of({'M3': 1, 'M2': 9, 'TL': 3}), 'big')
        self.assertEqual(wc.size_of({'TS': 2, 'OBJ_FIGHTDRONE': 5}), 'medium')  # non-tier classes do not count
        self.assertEqual(wc.size_of({'UFO': 5}), 'other')
        self.assertEqual(wc.size_of({}), 'unmounted')


class EntryTest(unittest.TestCase):
    def setUp(self):
        self.v = view()
        self.w = {w['id']: w for w in self.v['weapons']}

    def test_entry_facts(self):
        ire = self.w['SS_BULLET_IRE']
        self.assertEqual((ire['speed_mps'], ire['range_m'], ire['light'], ire['style']), (1162.0, 1394.4, '#3010ff', 'tracer'))
        self.assertEqual((ire['texture'], ire['size'], ire['used_by']), ('fx_diff.tga', 'small',
                                                                        {'M5': 1, 'M3': 1, 'class_id:2061': 1}))
        self.assertEqual(ire['effects']['impact'], dict(row=15, summary=(
            '3 elements: 10663 [animation -73] 0.04-0.06 800 ms; light 0.01-0.2 250 ms; propagate row 184 x1-3')))
        self.assertIsNone(ire['effects']['muzzle'])
        pac = self.w['SS_BULLET_PAC']
        self.assertEqual([l['id'] for l in pac['lasers']], ['SS_LASER_PAC_1', 'SS_LASER_PPC'])
        self.assertEqual((pac['style'], pac['size'], pac['used_by']), ('beam', 'small', {'M3': 1, 'M6': 1, 'M2': 1}))
        self.assertEqual(pac['effects']['impact']['summary'], 'row 40 missing from Effects')
        frag = self.w['SS_BULLET_FRAG']
        self.assertEqual((frag['style'], frag['size'], frag['lasers'], frag['effects']['death']['row']),
                         ('slug+burst', 'unmounted', [], 15))

    def test_description(self):
        self.assertEqual(self.w['SS_BULLET_IRE']['description'], (
            'Impulse Ray Emitter: small weapon, mountable on M5, M3, class_id:2061; fires a tracer (body b0, texture '
            'fx_diff) lit #3010ff at 1162 m/s over 1394.4 m, 400 shots/min, 100 shield / 10 hull damage per shot; '
            'impact row 15.'))
        self.assertEqual(self.w['SS_BULLET_FRAG']['description'], (
            'SS_BULLET_FRAG (no TLaser row fires it): no ship row can mount it; fires a slug+burst (body b2) lit '
            '#ffffff at 500 m/s over 150 m, 300 shield / 30 hull damage per shot; flags area_flak; impact row 15, '
            'death row 15.'))
        self.assertTrue(self.w['SS_BULLET_PAC']['description'].startswith(
            'Experimental Particle Accelerator Cannon, SS_LASER_PPC: small weapon, mountable on M3, M6, M2; fires a beam'))

    def test_counts_and_order(self):
        c = self.v['counts']
        self.assertEqual((c['weapons'], c['lasers'], c['laser_names_resolved'], c['laser_names_unresolved'],
                          c['weapons_without_laser']), (3, 4, 2, 2, 1))
        self.assertEqual([u['id'] for u in self.v['unresolved_names']], ['SS_LASER_PPC', 'SS_LASER_ODD'])
        self.assertEqual((self.v['unknown_subtypes'], self.v['lasers_without_bullet']), (['SS_LASER_ODD'], ['SS_LASER_ODD']))
        # size, then the first laser name (Experimental... < Impulse...), unmounted last
        self.assertEqual([w['id'] for w in self.v['weapons']], ['SS_BULLET_PAC', 'SS_BULLET_IRE', 'SS_BULLET_FRAG'])


class MarkdownTest(unittest.TestCase):
    def test_emitter(self):
        md = wc.markdown({'test': view()})
        lines = md.splitlines()
        self.assertEqual(lines[0], '# Weapon catalogue')
        self.assertIn('## Legend', lines)
        self.assertIn('`area_flak` 0x100', md)
        self.assertIn('`tracer` body LOD-0 z/xy >= 4.0', md)
        self.assertIn('Unresolved names: SS_LASER_PPC (TLaser 2, text id 8033), SS_LASER_ODD (TLaser 3, text id 0).', md)
        self.assertIn('Subtype not in the EXE table: SS_LASER_ODD.', md)
        rows = [l for l in lines if l.startswith('| ') and not l.startswith('| bullet') and not l.startswith('| ---')]
        self.assertEqual(len(rows), 3)
        self.assertEqual(rows[1], '| IRE | IRE (Impulse Ray Emitter) | small | M5:1 M3:1 class_id:2061:1 | tracer | 1162 '
                                  '| 1394.4 | 400 | 100/10 | #3010ff | - | -/-/15/- |')
        self.assertEqual(rows[0], '| PAC | PAC_1 (Experimental Particle Accelerator Cannon)<br>PPC (?) | small | '
                                  'M3:1 M6:1 M2:1 | beam | 2000 | 2000 | 15 | 200/20 | #000000 | beam_gun_tied | -/-/40/- |')
        self.assertTrue(all(r.count(' | ') == 11 for r in rows))
        self.assertEqual(wc.markdown({'test': view()}), md)                 # deterministic


if __name__ == '__main__':
    unittest.main()
