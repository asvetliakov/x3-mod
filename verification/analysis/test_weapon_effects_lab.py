"""Weapon effects lab (vanilla half): tools/effects/weapon_effects_lab_assets.py (DDS mip choice and decode through
lod_atlas, PNG writer, alpha masks, LOD-0 triangles, negative text-body materials, scene parts and the scene record
used for the objects/cut fallback, propagated rows) and
tools/effects/generate_weapon_effects_lab.py (compact records, death tiers, reachable rows, the data block and its
stale check), on inline fixtures with no catalogue access; plus the page shell (markers, the renderOurs stub, one file
that opens from disk, script syntax under node when present)."""
import importlib.util
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
HTML = ROOT / 'tools/effects/weapon_effects_lab.html'


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'tools/effects' / f'{name}.py')
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


assets_mod = load('weapon_effects_lab_assets')
gen = load('generate_weapon_effects_lab')


def dds(fourcc, w, h, levels):
    """Minimal DDS: header with the FOURCC pixel format and the given level payloads."""
    head = bytearray(128)
    head[:4] = b'DDS '
    struct.pack_into('<IIIII', head, 4, 124, 0x1007 | 0x20000, h, w, 0)
    struct.pack_into('<I', head, 28, len(levels))
    struct.pack_into('<II4s', head, 76, 32, 4, fourcc)
    return bytes(head) + b''.join(levels)


def dxt1_block(c0, c1, indices):
    bits = 0
    for i, v in enumerate(indices):
        bits |= v << (2 * i)
    return struct.pack('<HHI', c0, c1, bits)


def png_pixels(data):
    """(w, h, RGBA rows) of a filter-0 RGBA8 PNG as png_bytes writes it."""
    assert data[:8] == b'\x89PNG\r\n\x1a\n'
    pos, idat, w, h = 8, b'', None, None
    while pos < len(data):
        n, tag = struct.unpack_from('>I4s', data, pos)
        body = data[pos + 8:pos + 8 + n]
        crc = struct.unpack_from('>I', data, pos + 8 + n)[0]
        assert crc == zlib.crc32(tag + body) & 0xffffffff
        if tag == b'IHDR':
            w, h, depth, colour = struct.unpack('>IIBB', body[:10])
            assert (depth, colour) == (8, 6)
        elif tag == b'IDAT':
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = 1 + 4 * w
    rows = [raw[y * stride:(y + 1) * stride] for y in range(h)]
    assert all(r[0] == 0 for r in rows)
    return w, h, np.frombuffer(b''.join(r[1:] for r in rows), np.uint8).reshape(h, w, 4)


class DecodeTest(unittest.TestCase):
    RED, BLUE = 0xF800, 0x001F

    def test_dxt1_level_choice_and_colours(self):
        # 8x8 level 0 (four blocks, all red), 4x4 level 1 (red/blue halves by index), 2x2, 1x1.
        l0 = dxt1_block(self.RED, self.BLUE, [0] * 16) * 4
        l1 = dxt1_block(self.RED, self.BLUE, [0, 0, 1, 1] * 4)
        l2 = l3 = dxt1_block(self.BLUE, self.RED, [0] * 16)
        data = dds(b'DXT1', 8, 8, [l0, l1, l2, l3])
        self.assertEqual(assets_mod.dds_level_for(data, max_side=8), 0)
        self.assertEqual(assets_mod.dds_level_for(data, max_side=4), 1)
        self.assertEqual(assets_mod.dds_level_for(data, max_side=0), 3)     # the last level when none fits
        img = assets_mod.dds_rgba(data, max_side=4)
        self.assertEqual(img.shape, (4, 4, 4))
        self.assertEqual(img[0, 0].tolist(), [255, 0, 0, 255])
        self.assertEqual(img[3, 3].tolist(), [0, 0, 255, 255])
        self.assertEqual(assets_mod.dds_rgba(data, max_side=8)[7, 7].tolist(), [255, 0, 0, 255])

    def test_dxt5_alpha(self):
        alpha = struct.pack('<BB', 255, 0) + (0).to_bytes(6, 'little')       # every texel index 0 -> 255
        alpha_half = struct.pack('<BB', 255, 0) + int('001' * 8, 2).to_bytes(6, 'little')   # half texels index 1 -> 0
        colour = dxt1_block(0xFFFF, 0xFFFF, [0] * 16)
        img = assets_mod.dds_rgba(dds(b'DXT5', 4, 4, [alpha + colour]))
        self.assertTrue((img[:, :, 3] == 255).all())
        img = assets_mod.dds_rgba(dds(b'DXT5', 4, 4, [alpha_half + colour]))
        self.assertEqual(sorted(set(img[:, :, 3].ravel().tolist())), [0, 255])

    def test_unsupported_fourcc_refused(self):
        with self.assertRaises(assets_mod.lod_atlas.AtlasError):
            assets_mod.dds_rgba(dds(b'ATI2', 4, 4, [b'\0' * 16]))

    def test_png_roundtrip_and_determinism(self):
        img = np.arange(5 * 3 * 4, dtype=np.uint8).reshape(3, 5, 4)
        data = assets_mod.png_bytes(img)
        self.assertEqual(data, assets_mod.png_bytes(img.copy()))
        w, h, back = png_pixels(data)
        self.assertEqual((w, h), (5, 3))
        self.assertTrue((back == img).all())

    def test_mask_and_reduce(self):
        grey = np.zeros((2, 2, 4), np.uint8)
        grey[:, :, :3] = [[[10] * 3, [200] * 3], [[0] * 3, [255] * 3]]
        grey[:, :, 3] = 255
        self.assertEqual(assets_mod.mask_image(grey)[:, :, 3].tolist(), [[10, 200], [0, 255]])
        shaped = grey.copy()
        shaped[:, :, 3] = [[0, 50], [100, 255]]
        m = assets_mod.mask_image(shaped)
        self.assertEqual(m[:, :, 3].tolist(), [[0, 50], [100, 255]])
        self.assertTrue((m[:, :, :3] == 255).all())
        big = np.full((8, 4, 4), 100, np.uint8)
        self.assertEqual(assets_mod.reduce(big, max_side=2).shape, (2, 1, 4))

    def test_safe_stem(self):
        self.assertEqual(assets_mod.safe_stem('addon/dds/fx bullet (1).dds'), 'fx_bullet_1_')
        self.assertEqual(assets_mod.engine_name('id:163'), '163')


class GeometryTest(unittest.TestCase):
    def tree(self):
        # flags 0x1b: position + uv (+ normal etc.); a unit quad at LOD value 65536 (body unit = point / 65536).
        pts = [(0x1b, -65536, -65536, 0, 0, 0), (0x1b, 65536, -65536, 0, 65536, 0),
               (0x1b, 65536, 65536, 0, 65536, 65536), (0x1b, -65536, 65536, 0, 0, 65536)]
        lod = dict(value=65536, flags=0, points=pts,
                   parts=[dict(flags=0, groups=[dict(material=0, faces=[(0, 1, 2, 0), (0, 2, 3, 0)]),
                                                dict(material=-73, faces=[(0, 1, 2, 0)])])])
        return dict(sections=[('BODY', [lod])])

    def test_triangles_and_truncation(self):
        tris, truncated = assets_mod.triangles(self.tree())
        self.assertFalse(truncated)
        self.assertEqual(sorted(tris), [-73, 0])
        self.assertEqual(tris[0][0][1], [1.0, -1.0, 0.0, 1.0, 0.0])
        tris, truncated = assets_mod.triangles(self.tree(), max_triangles=2)
        self.assertTrue(truncated)
        self.assertEqual(sum(len(v) for v in tris.values()), 2)

    def test_material_textures_and_negative_material(self):
        effect = dict(kind='effect', textures={'t_DiffuseTexture': 'A\\B.tga', 't_AlphaTexture': 'NULL'})
        self.assertEqual(assets_mod.material_textures(effect), ('a\\b.tga', None))
        self.assertEqual(assets_mod.material_textures(dict(kind='classic', texture=17)), ('id:17', None))
        self.assertEqual(assets_mod.material_textures(dict(kind='material3', texture_id_inferred=311)),
                         ('id:311', None))
        anim = dict(kind='effect', textures={'t_DiffuseTexture': '-73.tga'})
        other = dict(kind='effect', textures={'t_DiffuseTexture': 'x.tga'})
        self.assertIs(assets_mod.negative_material(-73, [other, anim]), anim)
        self.assertIs(assets_mod.negative_material(-5, [other]), other)
        self.assertIsNone(assets_mod.negative_material(-5, [other, anim]))
        self.assertIsNone(assets_mod.negative_material(1, [other]))

    TEXT_SCENE = (b'VER: 3;\r\nP 0; B 446;\r\n  { 18;  0.5; 0.0; 0.0; 1.0;  75;  2; } // rotation only\r\n'
                  b'P 1; B environments\\debris\\D8; N Bx; b\r\n'
                  b'  { 0x2003;  -131072; 65536; 0;  0.0; 0.5; 0.0; -0.8;  100;  1; } // 1\r\n'
                  b'  { 0x2003;  0; 0; 0;  0.0; 0.5; 0.0; -0.8;  100;  2; } // 2\r\n'
                  b'P 2; B 999;\r\n  { 0xe082; 65536; 0; 0; 0;0;0;0;0; 0.3;0.1;-0.8;-0.5; 0;0;0;0;0; 4733; 1; }\r\n')

    def test_scene_parts(self):
        self.assertEqual(assets_mod.scene_parts(self.TEXT_SCENE),
                         [('446', None), ('environments\\debris\\D8', (-131072.0, 65536.0, 0.0)),
                          ('999', (65536.0, 0.0, 0.0))])
        cut1 = b'CUT1INFO$x$\0/INF\0\0\0\x06PATH\0\0\0\x0010656\0NAMEB10656l\0/NAMSTATxxNAMEB262b\0/NAM'
        self.assertEqual(assets_mod.scene_parts(cut1), [('10656', None), ('262', None)])

    def test_scene_record(self):
        quad = GeometryTest.tree(self)
        bodies = {'v\\00446': quad, 'environments\\debris\\D8': quad}
        summary = dict(materials=[dict(kind='effect', index=0, blend='ONE/ONE',
                                       textures={'t_DiffuseTexture': 'glow.tga'})])

        def read(name):
            if name not in bodies:
                raise FileNotFoundError(name)
            return name.encode(), 'cat:' + name
        parse, summ = assets_mod.body_tree, assets_mod.wec.body_summary
        assets_mod.body_tree = lambda data: bodies[data.decode()]
        assets_mod.wec.body_summary = lambda data: summary
        try:
            rec = assets_mod.scene_record(self.TEXT_SCENE, read, 'cut/04253.pbd', 'cut_fallback_inferred')
            empty = assets_mod.scene_record(b'P 0; B 7;\r\n', read, 'cut/x', 'scene')
            via = assets_mod.body_record(None, 'k', dict(name='x_scene', resolved='x', error='text scene, not a body'),
                                         lambda name: (self.TEXT_SCENE, 'cat:scene') if name == 'x_scene' else read(name))
        finally:
            assets_mod.body_tree, assets_mod.wec.body_summary = parse, summ
        self.assertEqual((rec['parts'], rec['parts_drawn'], rec['positions_decoded']), (3, 2, 2))
        self.assertNotIn('error', rec)
        d8 = [m for m in rec['materials']][2]                 # part 1: shifted by (-2, 1, 0) body units
        self.assertEqual(d8['triangles'][0][1][:3], [-1.0, 0.0, 0.0])
        self.assertEqual(rec['materials'][0]['triangles'][0][1][:3], [1.0, -1.0, 0.0])     # no position: origin
        self.assertEqual(rec['materials'][0]['diffuse'], 'glow.tga')
        self.assertIn('none of 1 parts', empty['error'])
        self.assertEqual((via['source'], via['parts']), ('scene', 3))

    def test_propagated_rows(self):
        census = dict(effects={'10': dict(elements=[dict(flag_names=['EEDF_PROPAGATE'], prop_effect_id=20)])})
        effects_all = {20: dict(elements=[dict(flag_names=['EEDF_PROPAGATE'], prop_effect_id=30)]),
                       30: dict(elements=[dict(flag_names=['EEDF_TIMEOUT'], bodyid=5)])}
        self.assertEqual(sorted(assets_mod.propagated_rows(census, effects_all)), ['20', '30'])
        effects_all.pop(30)
        self.assertEqual(assets_mod.propagated_rows(census, effects_all)['30'], dict(missing=True))


def census_fixture():
    el = dict(flag_names=['EEDF_TIMEOUT', 'EEDF_GROW'], i3dflags=0, bodyid=10661, minsize=0.01, maxsize=0.2,
              duration=250, rndsize=0.0, xoffset=0, yoffset=0, zoffset=0, lensflarenr=-1, atten=100, halfspeed=0.25,
              timeoffset=0, light=False, body_key='objects/v/10661')
    prop = dict(flag_names=['EEDF_TIMEOUT', 'EEDF_PROPAGATE'], i3dflags=0, prop_effect_id=149, min_props=2,
                max_props=4, min_prop_delay=50, max_prop_delay=100, prop_length=10, prop_delay=250, prop_steps=10,
                timeoffset=0)
    weapon = dict(index=0, weapon_id='SS_BULLET_X', body='b', body_key='objects/b', lifetime_ms=1200, speed=581000,
                  flag_word=0, flag_bits=[], light_colour=[48, 16, 192], size=[0.07, 0.07], beam_length=4.0,
                  trail_particles=0, col24_not_effect=0, flight_effect=0, impact_effect=15, muzzle_effect=22,
                  death_effect=None)
    return dict(
        tables={'TBullets': dict(source='addon/02.cat', member='addon/types/TBullets.pck', sha256='be76'),
                'Effects': dict(source='addon/01.cat', member='addon/types/Effects.pck', sha256='3d33'),
                'TShips': dict(source='x', member='y', sha256='z')},
        weapons=[weapon],
        effects={'15': dict(effectflags=0, elements=[el]), '22': dict(effectflags='EDF_REINIT', elements=[el]),
                 '35': dict(effectflags='EDF_NOROTATE|EDF_NODRIFT', elements=[prop, el]),
                 '37': dict(effectflags=0, elements=[el]), '99': dict(effectflags=0, elements=[el])},
        death_rows=dict(default_row=48,
                        ship_classes={'OBJ_SHIP_M3': {'35': 64}, 'OBJ_SHIP_M5': {'35': 46, '37': 1},
                                      'OBJ_SHIP_M6': {'40': 27}, 'OBJ_SHIP_M1': {'37': 24}},
                        TDocks={'201': 35, '37': 27}, TFactories={'37': 390},
                        TAsteroids=[dict(id='SS_ASTEROID_01', row=59), dict(id='SS_ASTEROID_02', row=59)]))


def manifest_fixture():
    return dict(textures={'a.tga': dict(status='ok', images={'a': dict(placeholder=None)}),
                          'b.tga': dict(status='ok', images={'b': dict(placeholder='NONE_BLACK')})},
                bodies={'objects/v/10661': dict(error=None), 'objects/v/04262': dict(error='FileNotFoundError: x'),
                        'objects/v/00002': dict(error=None, truncated=True),
                        'objects/v/00446': dict(error=None, source='cut_fallback_inferred')},
                extra_effects={'149': dict(effectflags=0, elements=[dict(
                    flag_names=['EEDF_TIMEOUT'], bodyid=4264, minsize=0.002, maxsize=0.12, duration=1900,
                    timeoffset=0, body_key='objects/v/04264')])})


class GeneratorTest(unittest.TestCase):
    def test_compact_element(self):
        el = census_fixture()['effects']['15']['elements'][0]
        c = gen.compact_element(el)
        self.assertEqual(c['f'], ['TIMEOUT', 'GROW'])
        self.assertEqual((c['body'], c['bodyid'], c['min'], c['max'], c['dur'], c['flare']),
                         ('objects/v/10661', 10661, 0.01, 0.2, 250, -1))
        self.assertNotIn('off', c)                     # all-zero offsets and zero values are omitted
        self.assertNotIn('toff', c)
        self.assertNotIn('rnd', c)
        p = gen.compact_element(census_fixture()['effects']['35']['elements'][0])
        self.assertEqual((p['pid'], p['pmin'], p['pmax'], p['pdelay']), (149, 2, 4, 250))
        self.assertNotIn('min', p)

    def test_tiers(self):
        tiers = dict(gen.tier_rows(census_fixture()['death_rows']))
        self.assertEqual(tiers['M5/M4/M3'], [[35, 110], [37, 1]])
        self.assertEqual(tiers['M6/M8/TS/TP'], [[40, 27]])
        self.assertEqual(tiers['M1/M2/M7/TL'], [[37, 24]])
        self.assertEqual(tiers['station'], [[37, 417], [201, 35]])
        self.assertEqual(tiers['asteroid'], [[59, 2]])
        self.assertEqual([t for t, _ in gen.tier_rows(census_fixture()['death_rows'])],
                         ['M5/M4/M3', 'M6/M8/TS/TP', 'M1/M2/M7/TL', 'station', 'asteroid'])

    def test_view_record(self):
        rec = gen.view_record('stock_ap', census_fixture(), manifest_fixture())
        # 15, 22 (weapon), 35, 37 (tiers), 149 (propagated, manifest only), 40/59/201 named but absent; 99 dropped.
        self.assertEqual(list(rec['effects']), ['15', '22', '35', '37', '40', '59', '149', '201'])
        self.assertTrue(rec['effects']['149']['propagated_only'])
        self.assertEqual(rec['effects']['40'], dict(missing=True))
        self.assertEqual(rec['effects']['35']['flags'], ['NOROTATE', 'NODRIFT'])
        self.assertEqual(rec['weapons'][0]['muzzle'], 22)
        self.assertEqual(sorted(rec['tables']), ['Effects', 'TBullets'])
        st = rec['status']
        self.assertEqual((st['texture_names'], st['texture_status'], st['placeholder_bound']), (2, {'ok': 2}, ['b.tga']))
        self.assertEqual((st['body_errors'], st['truncated_bodies']), (['objects/v/04262'], ['objects/v/00002']))
        self.assertEqual(st['scene_bodies'], {'cut_fallback_inferred': ['objects/v/00446'], 'scene': []})

    def test_catalogue_inlined(self):
        cat = dict(weapons=[dict(id='SS_BULLET_X', size='small', textures=['a.tga'], light_rgb=[1, 2, 3],
                                 lasers=[dict(id='SS_LASER_X', name='X Gun', used_by={'M3': 2}, size='small',
                                              name_id=8003, shots_per_min=400.0)]),
                            dict(id='SS_BULLET_ABSENT')])
        rec = gen.view_record('stock_ap', census_fixture(), manifest_fixture(), cat)
        self.assertEqual(rec['weapons'][0]['cat'], dict(id='SS_BULLET_X', size='small',
                                                        lasers=[dict(id='SS_LASER_X', name='X Gun', shots_per_min=400.0)]))
        self.assertNotIn('cat', gen.view_record('stock_ap', census_fixture(), manifest_fixture())['weapons'][0])

    def test_block_render_and_check(self):
        data = {'stock_ap': gen.view_record('stock_ap', census_fixture(), manifest_fixture())}
        hv = gen.header_constants((gen.REPO / 'src/proxy/weapon_effects_core.h').read_text())
        shell = (f'<script>\n{gen.HEADER_BEGIN}\nconst HEADER = {{}};\n{gen.HEADER_END}\n'
                 f'{gen.BEGIN}\nconst LAB_DATA = {{}};\n{gen.END}\n{gen.PRESETS_BEGIN}\nconst PRESETS = [];\n{gen.END}\n'
                 f'rest();\n</script>')
        step4 = [next(p for p in gen.PRESETS if p['family'] == f) for f in ('bolt', 'muzzle', 'boom')]
        presets = [dict(gen.PRESETS[0], view='stock_ap', weapon='SS_BULLET_X'),
                   dict(gen.PRESETS[2], view='stock_ap', weapon='SS_BULLET_X')] + \
                  [dict(p, view='stock_ap', weapon='SS_BULLET_X') for p in step4]
        page = gen.render(shell, data, hv, presets)
        self.assertEqual(gen.render(page, data, hv, presets), page)     # idempotent
        self.assertIn('const PRESETS = [{"id":"shield/', page)
        self.assertIn('  hull: {"r_base":0.35', page)                    # the namespaced constants (weapon-look.md 5)
        self.assertIn('"heat_ramp":[[0.0,0.0,0.0]', page)
        self.assertIn('const HEADER = {', page)
        self.assertIn('ripple_speed:', page)
        self.assertTrue(page.endswith('\nrest();\n</script>'))
        dstart = page.index('const LAB_DATA = {') + len('const LAB_DATA = {')
        body = page[dstart:page.index('};\n' + gen.END, dstart)]
        view, rec = body.strip().rstrip(',').split(': ', 1)
        self.assertEqual(json.loads(view), 'stock_ap')
        self.assertEqual(json.loads(rec), json.loads(json.dumps(data['stock_ap'])))
        with self.assertRaises(SystemExit):
            gen.render('<script></script>', data, hv, presets)
        with self.assertRaises(SystemExit):                              # a preset naming an id its view lacks
            gen.render(page, data, hv, presets[:1] + [dict(presets[1], weapon='SS_BULLET_NOPE')])

    def test_preset_problems(self):
        """weapon-look.md 6: --check fails when a preset names an id missing from its view or a family has no preset."""
        hc = {'SS_BULLET_MD': 'ballistic', 'SS_BULLET_GAUSS': 'heavy', 'SS_BULLET_HEPT': 'plasma', 'SS_BULLET_PSP': 'plasma',
              'SS_BULLET_GKE': 'heavy', 'SS_BULLET_RG': 'heavy', 'SS_BULLET_PBC': 'beam', 'SS_BULLET_PAL': 'beam',
              'SS_BULLET_ID': 'ion', 'SS_BULLET_IONC': 'ion', 'SS_BULLET_RL': 'repair'}
        data = {'mayhem3': {'weapons': [{'id': i, 'hc': hc.get(i, 'energy')} for i in (
                    'SS_BULLET_IRE', 'SS_BULLET_PPC', 'SS_BULLET_RL', 'SS_BULLET_HEPT', 'SS_BULLET_MD', 'SS_BULLET_PSP',
                    'SS_BULLET_GKE', 'SS_BULLET_RG', 'SS_BULLET_PBC', 'SS_BULLET_ID', 'SS_BULLET_PAC', 'SS_BULLET_PSG')]},
                'stock_ap': {'weapons': [{'id': i, 'hc': hc.get(i, 'energy')} for i in (
                    'SS_BULLET_IRE', 'SS_BULLET_MASS', 'SS_BULLET_FLAK_ALPHA', 'SS_BULLET_GAUSS', 'SS_BULLET_PAL',
                    'SS_BULLET_IONC', 'SS_BULLET_PAC', 'SS_BULLET_PSP', 'SS_BULLET_PSG')]}}
        self.assertEqual(gen.preset_problems(gen.PRESETS, data), [])
        # hull-impact-classes.md section 7: one preset per class; the ion presets removed, --check refuses
        self.assertEqual(gen.class_problems(gen.PRESETS, data), [])
        no_ion = [pr for pr in gen.PRESETS if not pr['id'].startswith('hull/ion-')]
        self.assertEqual(gen.class_problems(no_ion, data), ['class ion has no preset'])
        hull_only = [pr for pr in gen.PRESETS if pr['family'] == 'hull']
        self.assertIn('family shield has no preset', gen.preset_problems(hull_only, data))
        missing = [dict(gen.PRESETS[0], weapon='SS_BULLET_NOPE')] + list(gen.PRESETS[1:])
        self.assertTrue(any('SS_BULLET_NOPE' in m for m in gen.preset_problems(missing, data)))
        bad = [dict(gen.PRESETS[0], camera='orbit')] + list(gen.PRESETS[1:])
        self.assertTrue(any('camera' in m for m in gen.preset_problems(bad, data)))
        # every case of the 3.1 list
        self.assertEqual({pr['id'] for pr in hull_only},
                         {'hull/' + n for n in ('fighter-ire-side', 'fighter-ire-chase-nose', 'fighter-ire-cockpit-own',
                                                'corvette-ppc-flank', 'capital-ppc-flank-300m', 'capital-close-12hz',
                                                'station-ppc-1km', 'mining-asteroid-60m', 'missile-hit-corvette',
                                                'seta6-12hz', 'far-2km', 'drained', 'repair-corvette', 'stock-vs-mayhem',
                                                # the heavy families (2026-11-06)
                                                'heavy-plasma-corvette', 'heavy-flak-fighter', 'heavy-missile-hit',
                                                # the hull-impact classes (hull-impact-classes.md section 7)
                                                'energy-ire-fighter-side', 'energy-fighter60-20hits', 'ballistic-md-fighter-side',
                                                'ballistic-gauss-capital-300m', 'plasma-hept-corvette-flank',
                                                'plasma-psp-capital-close', 'heavy-gke-capital-300m', 'heavy-rg-chase-nose',
                                                'heavy-thin-wing-exit-jet', 'beam-pbc-pulsed-corvette',
                                                'beam-pal-sustained-sweep', 'ion-id-fighter-side', 'ion-ionc-capital',
                                                'class-lineup-fighter-side', 'class-lineup-capital-300m', 'scorch-12hps-20s',
                                                'seta6-classes')})

    def test_main_check_against_inputs(self):
        tmp = Path(tempfile.mkdtemp())
        try:
            (tmp / 'census').mkdir()
            (tmp / 'lab' / 'stock_ap').mkdir(parents=True)
            (tmp / 'census' / 'stock_ap.json').write_text(json.dumps(census_fixture()))
            (tmp / 'lab' / 'stock_ap' / 'manifest.json').write_text(json.dumps(manifest_fixture()))
            data = gen.load(views=('stock_ap',), census_dir=tmp / 'census', lab_dir=tmp / 'lab')
            self.assertEqual(list(data), ['stock_ap'])
            self.assertEqual(data['stock_ap']['weapons'][0]['id'], 'SS_BULLET_X')
        finally:
            shutil.rmtree(tmp)


class PageTest(unittest.TestCase):
    def setUp(self):
        self.html = HTML.read_text()

    def test_shell(self):
        self.assertEqual(self.html.count(gen.BEGIN), 1)
        self.assertEqual(self.html.count(gen.HEADER_BEGIN), 1)
        self.assertEqual(self.html.count(gen.END), 3)   # the header, data and presets blocks all close with /* @end */
        self.assertEqual(self.html.count(gen.PRESETS_BEGIN), 1)
        self.assertIn('const HEADER = {', self.html)    # the injected header constants
        self.assertRegex(self.html, r'ripple_speed: [0-9.]+')
        self.assertRegex(self.html, r'cell_m: \[[0-9., ]+\]')
        self.assertNotRegex(self.html, r'function renderOurs\(record, t\) \{ // eslint-disable-line no-unused-vars\n\}')
        self.assertIn('function ourControls()', self.html)
        # docs/architecture/weapon-effects.md 5.1.1's lab controls: cell size and rings per tier, ripple speed, core
        # and fade durations, stagger, edge versus fill, the three colour bands, the SETA rate, the hit player.
        for cid in ('c_cell_fighter', 'c_cell_corvette', 'c_cell_capital', 'c_rings_fighter', 'c_rings_corvette',
                    'c_rings_capital', 'c_ripple_speed', 'c_centre_gain', 'c_fade_ms', 'c_stagger', 'c_fill_ratio',
                    'c_colour_high', 'c_colour_mid', 'c_colour_low', 'c_colour_full', 'c_colour_half', 'c_colour_empty',
                    'c_weapon_tint', 'c_seta', 'c_hits'):
            self.assertIn(f'id="{cid}"', self.html)
        self.assertRegex(self.html, r'<select id="c_hits"[^>]*>.*value="1".*value="4".*value="12"')
        self.assertNotIn('c_core_ms', self.html)                        # the core flash is gone (2026-11-06)
        self.assertRegex(self.html, r'<select id="c_seta"><option selected>1</option>.*<option>6</option>')
        # the stage replaces the vanilla/ours canvas pair: no dashed "ours" figure, no "draws nothing yet" caption
        self.assertNotIn('figure class="ours"', self.html)
        self.assertNotIn('draws nothing yet', self.html)
        self.assertNotIn('<figcaption>Vanilla reconstruction</figcaption>', self.html)

    def test_tabs_and_panels(self):
        """2026-11-07 rework: one stage under a row of six tabs (Shield hit, Hull hit and Vanilla enabled; Explosion,
        Bolt and Beam disabled with a "later step" note), the controls in the Scene, Weapon, Playback, Layers and Tuning
        panels in that order (Tuning collapsed), a readable caption with its details collapsed, the legend collapsed,
        every control's id kept, and the controls of the other tabs hidden by their data-tabs."""
        tabs = re.findall(r'<button type="button" class="tab" role="tab" data-tab="(\w+)"( disabled)?[^>]*>([^<]+)(?:<small>([^<]*)</small>)?',
                          self.html)
        # step 3 (2026-11-07): the Explosion tab became the enabled Boom tab
        self.assertEqual([(t[0], t[2].strip()) for t in tabs],
                         [('shield', 'Shield hit'), ('hull', 'Hull hit'), ('vanilla', 'Vanilla'),
                          ('boom', 'Boom'), ('bolt', 'Bolt'), ('muzzle', 'Muzzle'), ('beam', 'Beam')])
        self.assertEqual([t[0] for t in tabs if not t[1]], ['shield', 'hull', 'vanilla', 'boom', 'bolt', 'muzzle'])
        self.assertTrue(all(t[3] == 'later step' for t in tabs if t[1]))
        legends = re.findall(r'<fieldset id="p_(\w+)"[^>]*><legend>(\w+)</legend>', self.html)
        self.assertEqual(legends, [('scene', 'Scene'), ('weapon', 'Weapon'), ('playback', 'Playback'),
                                   ('layers', 'Layers'), ('tuning', 'Tuning')])
        self.assertRegex(self.html, r'<legend>Tuning</legend>\n  <details id="tuning_more"><summary>')
        self.assertIn('<details id="legend"><summary>What this draws', self.html)
        self.assertIn('<p id="caption"></p>', self.html)
        self.assertIn('<details id="caption_more"><summary>counts and details</summary><pre id="caption_detail">', self.html)
        markup = self.html[self.html.index('<body>'):self.html.index('<script>')]
        self.assertNotIn('<canvas', markup)                            # the stage and the slot canvases come from init()
        for cid in ('preset', 'view', 'weapon', 'tier', 'drow', 'play', 'scrub', 'tread', 'loop', 'speed', 'zoom', 'tint',
                    'travel', 'flares', 'lights', 'reseed', 'c_family', 'c_camera', 'c_variant', 'c_k', 'c_class',
                    'c_h_spark_x', 'c_pulse_on', 'c_pulse_period', 'c_sweep', 'c_loop', 'c_h_ramp1', 'c_h_ramp2',
                    'c_h_ramp3', 'c_h_ramp4', 'c_h_min_frames', 'c_reset', 'c_h_reset', 'c_class_rows', 'c_tier',
                    'c_shield', 'c_drain', 'c_frame', 'c_surface', 'c_bubble_scale', 'c_bubble_far', 'c_hull', 'c_own',
                    'c_cockpit', 'winfo', 'status', 'wdesc'):
            self.assertEqual(self.html.count(f'id="{cid}"'), 1, cid)
        # the hull-only and shield-only rows name their tab; the tab switch hides, never disables
        for cid, tabs_of in (('c_camera', 'hull'), ('c_class', 'hull'), ('c_k', 'hull'), ('c_surface', 'shield'),
                             ('c_shield', 'shield'), ('c_hits', 'shield hull'), ('tier', 'vanilla'), ('zoom', 'vanilla')):
            self.assertRegex(self.html, r'<label class="row" data-tabs="%s"[^\n]*id="%s"' % (tabs_of, cid), cid)
        self.assertIn("el.hidden = !el.dataset.tabs.split(' ').includes(tab);", self.html)
        self.assertIn("setTab(p.family);", self.html)                  # a preset picks its tab
        self.assertIn("try { localStorage.setItem('x3m.weapon_lab.tab', tab); } catch (e)", self.html)
        self.assertIn('@media (max-width: 900px) { #panels { grid-template-columns: 1fr; }', self.html)

    def test_layers_panel(self):
        """The Layers panel: one checkbox per drawn element of the active family, all on by default (hull: flash, flash
        halo, burst, sparks, chips, entry streak, ring with its noise, refraction and wash, glow, ember, scorch, the ion's
        channels, branches and corona, the beam's weapon colour and the shimmer's disabled
        placeholder; shield: patch, bubble outline, native impact sprite; vanilla: the tint, travel, flare and light
        options), "all" and "none", the state persisted in localStorage inside try/catch and read by the renderers."""
        panel = re.search(r'<fieldset id="p_layers">(.*?)</fieldset>', self.html, re.S).group(1)
        boxes = re.findall(r'<label class="check[^"]*" data-tabs="([\w ]+)"[^>]*><input id="(\w+)" type="checkbox"([^>]*)>', panel)
        by_tab = {}
        for tab, cid, attrs in boxes:
            by_tab.setdefault(tab, []).append(cid)
        self.assertEqual(by_tab['hull'], ['layer_flash', 'layer_star', 'layer_flash_halo', 'layer_burst', 'layer_sparks',
                                          'layer_chips', 'layer_streak', 'layer_ring', 'layer_ring_noise',
                                          'layer_ring_refraction', 'layer_ring_wash', 'layer_glow', 'layer_ember',
                                          'layer_scorch', 'layer_smoke', 'layer_ion_channels', 'layer_ion_branches',
                                          'layer_ion_corona', 'layer_beam_colour', 'layer_shimmer'])
        self.assertEqual(by_tab['shield hull'], ['layer_hull'])       # the hull body under both previews
        self.assertEqual(by_tab['shield'], ['layer_patch', 'layer_bubble', 'layer_native'])
        self.assertEqual(by_tab['vanilla'], ['tint', 'travel', 'flares', 'lights'])
        for tab, cid, attrs in boxes:
            if cid.startswith('layer_'):
                self.assertIn('checked', attrs, cid)
        self.assertIn('disabled', dict((c, a) for _, c, a in boxes)['layer_shimmer'])
        self.assertIn('id="layers_all"', panel)
        self.assertIn('id="layers_none"', panel)
        keys = json.loads(re.search(r'const LAYER_KEYS = (\[.*?\]);', self.html, re.S).group(1).replace("'", '"'))
        self.assertEqual(sorted(keys), sorted(re.findall(r'data-layer="(\w+)"', panel)))
        render_hull = self.html[self.html.index('function renderHull('):self.html.index('const HULL_IDS')]
        for name in ('glow', 'flash', 'star', 'flash_halo', 'ember', 'ring', 'scorch', 'smoke', 'ion_channels',
                     'ion_branches', 'ion_corona', 'beam_colour', 'burst'):
            self.assertIn(f'layers.{name}', render_hull, name)
        self.assertIn('if (layers.glow && Rb > 0 && an !== AN.asteroid && a > 0 && a < puffLife)', render_hull)
        self.assertIn('layers.patch', self.html[self.html.index('function renderOurs('):self.html.index('function ourLoopMs(')])
        self.assertRegex(self.html, r"try \{\n  const saved = JSON\.parse\(localStorage\.getItem\('x3m\.weapon_lab\.layers'\)")

    def test_stage_capacities_come_from_the_headers(self):
        """CELL_KEY_BIAS, PATCH_CELLS, PATCH_HITS, OWNER_HITS and REGION_DRAWS are injected into the HEADER block from
        the C++ headers (review F11), never literals in the page."""
        pass_h = (ROOT / 'src/renderer/weapon_effects_pass.h').read_text()
        core_h = (ROOT / 'src/proxy/weapon_effects_core.h').read_text()
        stage_h = (ROOT / 'src/proxy/weapon_effects_stage.h').read_text()
        want = {'cell_key_bias': re.search(r'weapon_cell_key_bias = (\d+)', pass_h),
                'patch_cells': re.search(r'weapon_patch_cells = (\d+)', pass_h),
                'patch_hits': re.search(r'weapon_patch_hits = (\d+)', pass_h),
                'owner_hits': re.search(r'\bowner_hits = (\d+)', core_h),
                'region_draws': re.search(r'\bregion_draws = (\d+)', stage_h)}
        for name, m in want.items():
            self.assertIsNotNone(m, name)
            self.assertIn('  %s: %s' % (name, m.group(1)), self.html, name)
        self.assertEqual(int(want['patch_cells'].group(1)), 128)
        self.assertIn('const CELL_KEY_BIAS = HEADER.cell_key_bias, PATCH_CELLS = HEADER.patch_cells, '
                      'PATCH_HITS = HEADER.patch_hits;', self.html)
        self.assertIn('const OWNER_HITS = HEADER.owner_hits, REGION_DRAWS = HEADER.region_draws;', self.html)
        self.assertNotRegex(self.html, r'<script[^>]+type="module"|fetch\(|https?://')
        for slot in ('muzzle', 'projectile', 'impact', 'death'):
            self.assertIn(f"key: '{slot}'", self.html)
        self.assertIn('renderOurs(rec, state.t)', self.html)
        self.assertIn('<aside id="wdesc">', self.html)                 # catalogue description panel
        self.assertIn('function updateDesc()', self.html)

    def test_bolt_and_muzzle_tabs_layers_presets(self):
        """Step 4 (docs/architecture/bolt-muzzle-look.md 6): the Bolt and Muzzle tabs are live with their layers (core,
        body, halo, tail, head, rim, streak; disc, cone, star, bloom, puff, hull glow), the header's bolt and muzzle
        constants are injected by name, every preset of section 6 and weapon-look 3.3's list is a Scenario grouped under
        Bolt or Muzzle, and the three proving scenarios are among them."""
        self.assertIn('<button type="button" class="tab" role="tab" data-tab="bolt">Bolt</button>', self.html)
        self.assertIn('<button type="button" class="tab" role="tab" data-tab="muzzle">Muzzle</button>', self.html)
        self.assertNotRegex(self.html, r'data-tab="bolt" disabled')
        for k in ('b_core', 'b_body', 'b_halo', 'b_tail', 'b_head', 'b_rim', 'b_streak', 'm_disc', 'm_cone', 'm_star',
                  'm_bloom', 'm_puff', 'm_glow'):
            self.assertIn(f'data-layer="{k}"', self.html)
        header = gen.header_constants((ROOT / 'src/proxy/weapon_effects_core.h').read_text())
        self.assertEqual(header['bolt']['w_px_target'], 3)
        self.assertEqual(header['bolt']['d_eng_m'], [600.0, 1500.0, 4000.0])
        self.assertEqual(header['muzzle']['cluster_n0'], 2.5)
        self.assertIn('bolt:{"mode":"full"', re.sub(r'\s', '', self.html[self.html.index(gen.HEADER_BEGIN):][:200000]))
        ids = {p['id'] for p in gen.PRESETS}
        for pid in ('bolt/own-fire-chase', 'bolt/incoming-cockpit', 'bolt/stream-over-station-3km', 'bolt/far-10km-floor',
                    'bolt/convergent-barrage', 'bolt/mayhem-gke-fighter-heavy', 'bolt/psg-wave-endon',
                    'muzzle/md-ballistic-chase', 'muzzle/gke-heavy-chase', 'muzzle/capital-broadside-12',
                    'bolt/ire-chase-ahead-600m', 'bolt/psp-orb', 'bolt/psg-wave-200m', 'bolt/seta6-stream',
                    'muzzle/ire-own-chase', 'muzzle/pac-own-cockpit', 'muzzle/flak-alpha-turret', 'muzzle/seta6-10hz'):
            self.assertIn(pid, ids)
        self.assertIn("group('bolt', 'Bolt') + group('muzzle', 'Muzzle')", self.html)
        problems = gen.preset_problems([p for p in gen.PRESETS if p['family'] in ('bolt', 'muzzle')],
                                       {v: {'weapons': [{'id': p['weapon']} for p in gen.PRESETS if p['view'] == v]}
                                        for v in gen.VIEWS})
        self.assertEqual([m for m in problems if 'has no preset' not in m], [])

    BOLT = r'''
const OW = 720, OH = 405, W = OW, H = OH; function fmt(v) { return v; } function assets() { return null; }
function stageImage() { return {data: new Uint8ClampedArray(OW * OH * 4)}; }
const vals = {c_b_scene: 'single', c_m_scene: 'own_chase', c_b_rows: 1440, c_b_fov: 'vanilla', c_b_dist: 600, c_b_angle: 90,
  c_b_seta: 1, c_b_hull: 0, c_m_hz: 4};
const checks = {};
function $(id) { return {value: vals[id], checked: !!checks[id]}; }
// the stock IRE (fighter, k 0) and the Mayhem PPC (capital slug, k 0.8) as the lab data carries them
const IRE = {id: 'SS_BULLET_IRE', mt: 0, rg: 1394.4, colour: [48, 16, 192], speed: 581000, life: 1200,
  cat: {style: 'tracer', damage_shield: 100, damage_hull: 100, speed_mps: 1162, range_m: 1394.4, flag_word: '0x0'}};
const PPC = {id: 'SS_BULLET_PPC', mt: 2, rg: 19250, colour: [255, 255, 255], speed: 1750000, life: 5500,
  cat: {style: 'slug', damage_shield: 11943, damage_hull: 7500, speed_mps: 3500, range_m: 19250, flag_word: '0x0'}};
function lit(fr) { let n = 0; for (let i = 0; i < OW * OH; i++) if (fr.sum[i * 3] + fr.sum[i * 3 + 1] + fr.sum[i * 3 + 2] > 0.05) n++; return n; }
'''

    @unittest.skipUnless(shutil.which('node'), 'node not installed')
    def test_bolt_renders_the_width_law_and_layers(self):
        """The Bolt tab's law (section 1, 2.4, 2.6) in canvas pixels at 1440 rows: the stock IRE side-on at its d_eng is
        the 3 px body floor with its 37.5 px core and 24 px tail; at 60 m the own ceiling caps it at 16 px while another
        ship's is not capped; end-on it is a disc of at least the end-on floor; the halo layer off lights fewer pixels."""
        probe = self.ours_script() + self.BOLT + r'''
const out = {};
const near = fr => fr.qs.reduce((a, b) => (a.dist < b.dist ? a : b));
vals.c_b_dist = 600; const mid = boltLoopMs(boltControls(), IRE) / 2;  // the bolt crosses the canvas centre at mid-loop
let fr = boltFrame(boltControls(), IRE, mid); const q = near(fr);
out.ire = {w: q.wh, L: q.Lc, T: q.T, lit: lit(fr), w_m: boltLook(IRE).w};
layers.b_halo = false; out.ire.lit_nohalo = lit(boltFrame(boltControls(), IRE, mid)); layers.b_halo = true;
vals.c_b_dist = 60; checks.c_b_own = true; out.own = near(boltFrame(boltControls(), IRE, 0)).wh;
checks.c_b_own = false; out.other = near(boltFrame(boltControls(), IRE, 0)).wh;
vals.c_b_dist = 600; vals.c_b_angle = 0; out.endon = near(boltFrame(boltControls(), IRE, 0)).wh;
vals.c_b_angle = 90; vals.c_b_dist = 4000; out.ppc_w_m = boltLook(PPC).w; out.ppc = near(boltFrame(boltControls(), PPC, 0)).wh;
console.log(JSON.stringify(out));
'''
        js = self.run_node(probe)
        self.assertAlmostEqual(js['ire']['w_m'], 1.875, places=3)
        self.assertAlmostEqual(js['ire']['w'], 3.0, places=2)
        self.assertAlmostEqual(js['ire']['L'], 37.5 * (0.75 + 0.5 * 1162 / 2000) / 1.0425, delta=1.0)  # 12 w s_v at 600 m
        self.assertAlmostEqual(js['ire']['T'], 24.0, delta=0.6)       # T_max 8 w (v tau 17.4 m > 15 m)
        self.assertLess(js['ire']['lit_nohalo'], js['ire']['lit'])
        self.assertAlmostEqual(js['own'], 16.0, places=3)             # own_body_max_px at 1440 rows
        self.assertGreater(js['other'], 16.0)                         # another ship's bolt is not capped
        self.assertGreaterEqual(js['endon'], 9.0 - 1e-6)              # the end-on floor
        self.assertAlmostEqual(js['ppc_w_m'], 18.5, delta=0.1)        # the capital slug, 4.44 px at 4 km
        self.assertAlmostEqual(js['ppc'], 4.44, delta=0.05)

    @unittest.skipUnless(shutil.which('node'), 'node not installed')
    def test_muzzle_light_counting(self):
        """Section 3.2's broadside: 12 turrets along a capital hull cast 12 lights whose cluster factor s(N) bounds their
        summed glow to <= 2.3 single glows (12 / 5.4 = 2.2); the own fighter's two guns in the cockpit cast theirs at the
        cockpit gain 0.35."""
        probe = self.ours_script() + self.BOLT + r'''
const out = {};
vals.c_m_scene = 'broadside_12'; vals.c_m_hz = 0.33;
const all = muzzleLights(boltControls(), PPC, 10);
out.count = all.length; out.sum = all.reduce((a, l) => a + l.E, 0); out.n = all.map(l => l.N);
vals.c_m_scene = 'capital_600m'; out.single = muzzleLights(boltControls(), PPC, 10)[0].E;
vals.c_m_scene = 'own_chase'; vals.c_m_hz = 4; out.chase = muzzleLights(boltControls(), IRE, 5).map(l => l.E);
vals.c_m_scene = 'own_cockpit'; out.cockpit = muzzleLights(boltControls(), IRE, 5).map(l => l.E);
console.log(JSON.stringify(out));
'''
        js = self.run_node(probe)
        self.assertEqual(js['count'], 12)
        self.assertEqual(max(js['n']), 12)
        self.assertLessEqual(js['sum'], 2.3 * js['single'] + 1e-9)
        self.assertGreaterEqual(js['sum'], 2.0 * js['single'])
        self.assertEqual(len(js['chase']), len(js['cockpit']))
        for a, b in zip(js['chase'], js['cockpit']):
            self.assertAlmostEqual(b, 0.35 * a, places=6)

    @unittest.skipUnless(shutil.which('node'), 'node not installed')
    def test_muzzle_view_rule(self):
        """The view rule (core::muzzle::endon_cos, 2026-11-07): the own guns in the chase view (the gun axis about 11
        degrees off the eye ray) take the four-arm star for the cone; the own_side scene (guns across the view) keeps the
        cone; a class with a star of its own (ballistic) keeps its own star and cone; the side preset is listed."""
        probe = self.ours_script() + self.BOLT + r'''
const out = {};
const at = (sc, g) => g.map((v, k) => v + sc.centre[k]);
vals.c_m_scene = 'own_chase'; const sc = muzzleScene(boltControls());
out.chase = sc.guns.map(g => muzzleEndOn(at(sc, g), sc.dir || [0, 0, 1], 0));
out.ballistic = muzzleEndOn(at(sc, sc.guns[0]), sc.dir || [0, 0, 1], 1);
vals.c_m_scene = 'own_side'; const ss = muzzleScene(boltControls());
out.side = ss.guns.map(g => muzzleEndOn(at(ss, g), ss.dir, 0));
out.cos = MH.endon_cos;
console.log(JSON.stringify(out));
'''
        js = self.run_node(probe)
        self.assertEqual(js['chase'], [True, True])
        self.assertEqual(js['side'], [False, False])
        self.assertFalse(js['ballistic'])
        self.assertAlmostEqual(js['cos'], 0.8660254, places=6)
        self.assertIn('muzzle/ire-own-side-cone', [p['id'] for p in gen.PRESETS])

    @unittest.skipUnless(shutil.which('node'), 'node not installed')
    def test_boom_tab_presets_and_laws(self):
        """Step 3 (docs/architecture/explosion-look.md 8): the Boom tab, its fourteen scenarios, and renderBoom's laws
        against the stage's (src/proxy/weapon_effects_boom.h, the header's HEADER.boom): the fighter's body radius at 0.3 L
        R_0 + (R_f - R_0)(1 - e^(-0.3 / rho)) + 0.03 R_f, the star only within its 40 ms, the missile's shock only within
        shock_end L, the nested case's three bodies, the glow and smoke only over the hull plane, the halo in air, embers
        random per seed, hidden inside the body, of varied lengths (decisions of 2026-11-07)."""
        ids = [p['id'] for p in gen.PRESETS if p['family'] == 'boom']
        for want in ('fighter-m5-600m-side', 'fighter-m3-2km-floor', 'corvette-m6-1500m', 'freighter-ts-1500m',
                     'capital-m2-1500m-preroll', 'capital-m7-far-5km', 'capital-m2-close-720m',
                     'station-factory-3km-preroll', 'missile-300m', 'missile-near-own-ship-40m', 'nested-3-fighters-area',
                     'own-death-chase', 'own-death-cockpit', 'seta6'):
            self.assertIn('boom/' + want, ids)
        self.assertIn('<button type="button" class="tab" role="tab" data-tab="boom">Boom</button>', self.html)
        probe = self.ours_script() + self.CONTROLS + r"""
const checks = {c_x_smoke: true};
function $(id) { return {value: vals[id], checked: !!checks[id]}; }
const out = {};
const run = (o, t) => {
  Object.assign(vals, {c_family: 'boom', c_x_tier: 'fighter', c_x_dist: 600, c_x_radius: 14, c_x_nested: 1, c_x_seta: 1,
                       c_x_seed: 1}, o.vals || {});
  checks.c_x_hull = !!o.hull;
  const data = new Uint8ClampedArray(OW * OH * 4);
  const rec = {slot: 'impact', ctx: {setTransform() {}, getImageData() { return {data: data.slice()}; }, putImageData() {}}};
  renderOurs(rec, t);
  return rec.boomStats;
};
out.birth = run({}, 0);
out.f03 = run({}, 600);
out.missile = run({vals: {c_x_tier: 'missile', c_x_dist: 300, c_x_radius: 3}}, 80);
out.missile_late = run({vals: {c_x_tier: 'missile', c_x_dist: 300, c_x_radius: 3}}, 400);
out.nested = run({vals: {c_x_nested: 3}}, 600);
out.hull = run({hull: true}, 1200);
out.sky = run({}, 1200);
out.seed2 = run({vals: {c_x_seed: 2}}, 600);
// the big tiers (2026-11-07): the capital's pre-roll (the carrier box lit, secondaries, no body), its birth at P, the
// hull copy faded by P + 1 s; the corvette (P 0) mid-fade; the station's pre-roll
const cap = {vals: {c_x_tier: 'capital', c_x_dist: 1500, c_x_radius: 1288}};
out.cap_pre = run(cap, 700);
out.cap_birth = run(cap, 1700);
out.cap_late = run(cap, 2600);
out.corvette = run({vals: {c_x_tier: 'corvette', c_x_dist: 1500, c_x_radius: 87}}, 500);
out.station_pre = run({vals: {c_x_tier: 'station', c_x_dist: 3000, c_x_radius: 1800}}, 2000);
run({}, 0); // the fighter's controls again for the loop
vals.c_family = 'boom'; out.loop = ourLoopMs();
out.boom = HEADER.boom;
console.log(JSON.stringify(out));
"""
        js = self.run_node(probe)
        bm = js['boom']
        R_f = min(max(bm['tier_f'][0] * 14, bm['tier_min_m'][0]), bm['tier_max_m'][0])
        R_0 = bm['tier_r0_obj'][0] * 14
        import math
        want = R_0 + (R_f - R_0) * (1 - math.exp(-0.3 / bm['tier_rise'][0])) + bm['body_grow'] * R_f * 0.3
        self.assertAlmostEqual(js['f03']['R'], want, places=4)
        self.assertEqual((js['birth']['stars'], js['f03']['stars']), (1, 0))
        self.assertEqual((js['f03']['bodies'], js['f03']['shocks']), (1, 0))       # no shock for fighters
        # every ember drawn or hidden inside the body (decision 2026-11-07); lengths not uniform, under the cap
        self.assertEqual(js['f03']['embers'] + js['f03']['fragments'] + js['f03']['hidden'], int(bm['tier_embers'][0]))
        lens = js['sky']['lengths']
        mean = sum(lens) / len(lens)
        self.assertGreaterEqual((sum((x - mean) ** 2 for x in lens) / len(lens)) ** 0.5, 0.2 * mean)
        self.assertGreater(js['sky']['light']['halo'], 0)                         # the halo in air, over sky too
        self.assertEqual(js['sky']['halos'], 1)
        self.assertEqual((js['missile']['shocks'], js['missile_late']['shocks']), (1 if bm['missile_shock'] else 0, 0))
        self.assertEqual(js['nested']['bodies'], 3)
        self.assertGreater(js['hull']['light']['glow'], 0)
        self.assertGreater(js['hull']['light']['smoke'], 0)
        self.assertEqual((js['sky']['light']['glow'], js['sky']['light']['smoke']), (0, 0))  # surface-bound
        self.assertNotEqual(js['f03']['light']['ember'], js['seed2']['light']['ember'])
        self.assertAlmostEqual(js['loop'], (bm['smoke_t0'] + bm['smoke_span']) * bm['tier_life_ms'][0] + 250, places=3)
        # the big tiers' pre-roll (explosion-look.md 4): the carrier at full alpha and lit (the hull light, the secondaries'
        # glows), secondaries drawn and born, no body before P; the body from P, the hull copy at 0.8 at P + 0.2 s, gone at
        # P + 1 s; the corvette (P 0) at half alpha at 0.5 s with no secondaries; the station's twelve over 3 s
        pre, birth, late = js['cap_pre'], js['cap_birth'], js['cap_late']
        self.assertEqual((pre['bodies'], pre['hull_lights'], pre['hull_alpha']), (0, 1, 1))
        self.assertGreater(pre['secondaries'], 0)
        self.assertGreater(pre['light']['hull_light'], 0)
        self.assertGreater(pre['light']['secondary'], 0)
        self.assertEqual((birth['bodies'], birth['hull_lights']), (1, 1))   # the hull light fades with the copy
        self.assertAlmostEqual(birth['hull_alpha'], 0.8, places=6)
        self.assertEqual((late['hull_alpha'], late['hull_lights']), (0, 0))
        self.assertEqual((js['corvette']['secondaries'], js['corvette']['bodies']), (0, 1))
        self.assertAlmostEqual(js['corvette']['hull_alpha'], 0.5, places=6)
        self.assertGreater(js['station_pre']['secondaries'], 0)
        self.assertEqual(js['station_pre']['bodies'], 0)
        self.assertEqual(bm['tier_secondaries'], [0, 0, 8, 12, 0])
        self.assertEqual((bm['shock_tiers'], birth['shocks'], js['corvette']['shocks']), (0, 0, 0))  # D14: no shell
        self.assertEqual((birth['halos'], js['cap_late']['halos']), (1, 1))                         # D15: drawn to 0.6 L

    @unittest.skipUnless(shutil.which('node'), 'node not installed')
    def test_script_parses(self):
        script = re.search(r'<script>\n(.*)</script>', self.html, re.S).group(1)
        with tempfile.NamedTemporaryFile('w', suffix='.js', delete=False) as f:
            f.write(script)
        try:
            r = subprocess.run(['node', '--check', f.name], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr[:500])
        finally:
            Path(f.name).unlink()


    def ours_script(self):
        script = re.search(r'<script>\n(.*)</script>', self.html, re.S).group(1)
        header = script[script.index(gen.HEADER_BEGIN):script.index(gen.END, script.index(gen.HEADER_BEGIN))]
        ours = script[script.index('// ---------------------------------------------------------------- our renderer'):
                      script.index('// ---------------------------------------------------------------- helpers')]
        helpers = script[script.index('function rng('):script.index('function hash(')]
        return (header + helpers + ours).replace('const HEADER', 'var HEADER')

    def run_node(self, probe):
        with tempfile.NamedTemporaryFile('w', suffix='.js', delete=False) as f:
            f.write(probe)
        try:
            r = subprocess.run(['node', f.name], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr[:500])
            return json.loads(r.stdout)
        finally:
            Path(f.name).unlink()

    CONTROLS = r'''
const OW = 720, OH = 405, W = OW, H = OH; function fmt(v) { return v; } function assets() { return null; }
function drawBackground() {} function label() {} function stageImage(ctx) { return ctx.getImageData(0, 0, OW, OH); }
const vals = {c_cell_fighter: HEADER.cell_m[0], c_cell_corvette: HEADER.cell_m[1], c_cell_capital: HEADER.cell_m[2],
  c_rings_fighter: HEADER.rings[0], c_rings_corvette: HEADER.rings[1], c_rings_capital: HEADER.rings[2],
  c_ripple_speed: HEADER.ripple_speed, c_centre_gain: HEADER.centre_gain, c_fade_ms: HEADER.fade_ms, c_stagger: HEADER.stagger,
  c_fill_ratio: HEADER.fill_ratio, c_colour_high: HEADER.colour_high, c_colour_mid: HEADER.colour_mid,
  c_colour_low: HEADER.colour_low, c_colour_full: '#a0c0ff', c_colour_half: '#ffff80', c_colour_empty: '#ff8080',
  c_weapon_tint: 0, c_seta: 1, c_hits: 0, c_tier: 0, c_shield: 100, c_drain: 0, c_frame: 'patch'};
function $(id) { return {value: vals[id]}; }
layers.hull = false; // the shield tests count lit pixels: the dim hull body under the patch would count with them
'''

    @unittest.skipUnless(shutil.which('node'), 'node not installed')
    def test_impact_patch_is_whole_and_facing(self):
        """The impact slot's single hit (the user's half circle: the preview's ray-sphere intersection took the wrong
        root, so pixels mapped off the sphere) lights a patch centred on the canvas, on both sides of the hit in both
        axes, per tier; the automatic loop covers our patch's lifetime over SETA."""
        probe = self.ours_script() + self.CONTROLS + r'''
const out = {};
for (const tier of [0, 1, 2]) {
  vals.c_tier = tier;
  const data = new Uint8ClampedArray(W * H * 4);
  renderOurs({slot: 'impact', ctx: {setTransform() {}, getImageData() { return {data}; }, putImageData() {}}, weapon: {}}, 400);
  const side = [0, 0, 0, 0];
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) if (data[(y * W + x) * 4 + 2] > 0) {
    if (x + 0.5 < W / 2) side[0]++; else side[1]++;
    if (y + 0.5 < H / 2) side[2]++; else side[3]++;
  }
  out[tier] = side;
}
vals.c_tier = 0;
out.lifetime = patchLifetimeMs(ourControls()); out.loop1 = ourLoopMs();
vals.c_seta = 6; out.loop6 = ourLoopMs();
console.log(JSON.stringify(out));
'''
        js = self.run_node(probe)
        for tier in ('0', '1', '2'):
            side = js[tier]
            self.assertGreater(min(side), 100, tier)
            self.assertGreaterEqual(min(side[0], side[1]) / max(side[0], side[1]), 0.95, tier)
            self.assertGreaterEqual(min(side[2], side[3]) / max(side[2], side[3]), 0.95, tier)
        self.assertEqual(js['lifetime'], 2941)
        self.assertAlmostEqual(js['loop1'], 2941)
        self.assertAlmostEqual(js['loop6'], 2941 / 6, places=3)
        self.assertIn('ours > 0 ? ours + 250 : 0', self.html)        # loopMs takes our patch into account

    @unittest.skipUnless(shutil.which('node'), 'node not installed')
    def test_from_above_presets_and_own_ship_cockpit(self):
        """Run236: the hulls seen from above and below light their +y / -y face around the canvas centre (the screen-
        space patch on the hull under the hit, never the opposite face); Flight C: the own ship in the cockpit view
        draws at own_cockpit_gain, in the chase view and for another ship unchanged."""
        probe = self.ours_script() + self.CONTROLS + r'''
const checks = {};
function $(id) { return {value: vals[id], checked: !!checks[id]}; }
const render = () => {
  const data = new Uint8ClampedArray(W * H * 4);
  renderOurs({slot: 'impact', ctx: {setTransform() {}, getImageData() { return {data}; }, putImageData() {}}, weapon: {}}, 400);
  const side = [0, 0, 0, 0];
  let blue = 0;
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) if (data[(y * W + x) * 4 + 2] > 0) {
    if (x + 0.5 < W / 2) side[0]++; else side[1]++;
    if (y + 0.5 < H / 2) side[2]++; else side[3]++;
    blue += data[(y * W + x) * 4 + 2];
  }
  const c = ourControls(), hull = previewHull(c), S = hull.S, cell = c.cell_m[c.tier];
  const seeds = patchDraws(hitSequence(400, c, S, cell), S, c).map(d => d.seed);
  return {side, blue, seeds};
};
const out = {};
for (const hull of ['narrow_top', 'flat_top', 'flat_bottom']) { vals.c_hull = hull; out[hull] = render(); }
vals.c_hull = 'flat_top';
checks.c_own = true; out.own_chase = render();
checks.c_cockpit = true; out.own_cockpit = render();
checks.c_own = false; out.other_cockpit = render();
out.gain = HEADER.own_cockpit_gain;
checks.c_cockpit = false;
vals.c_hull = 'sphere'; out.sphere = render().seeds;
console.log(JSON.stringify(out));
'''
        js = self.run_node(probe)
        for hull, seed in (('narrow_top', 4), ('flat_top', 4), ('flat_bottom', 5)):
            side = js[hull]['side']
            self.assertIn(seed, js[hull]["seeds"], hull)                # the face on the hit's side (the normal picks)
            self.assertGreater(min(side), 100, hull)
            self.assertGreaterEqual(min(side[0], side[1]) / max(side[0], side[1]), 0.9, hull)
            if hull != 'narrow_top':  # 40 % aft the top falls away along z: the soft fade cuts the half towards the centre
                self.assertGreaterEqual(min(side[2], side[3]) / max(side[2], side[3]), 0.9, hull)
        self.assertEqual(js['own_chase']['blue'], js['flat_top']['blue'])
        self.assertEqual(js['other_cockpit']['blue'], js['flat_top']['blue'])
        self.assertLess(js['own_cockpit']['blue'], 0.95 * js['own_chase']['blue'])  # 1 - exp, sRGB: dimmer, not x gain
        self.assertAlmostEqual(js['gain'], 0.35)
        self.assertEqual(sorted(js['sphere']), [0, 1, 2, 3, 4, 5])  # review F1: every hit on both faces of every axis
        self.assertNotIn('id="c_tilt"', self.html)
        self.assertIn('id="c_own"', self.html)
        self.assertIn('id="c_cockpit"', self.html)

    @unittest.skipUnless(shutil.which('node'), 'node not installed')
    def test_hull_presets_render_their_elements(self):
        """hull-impact-classes.md 4 and 7: every hull preset of the page draws through renderHull, and every element its
        class should have deposits light on the canvas (its summed radiance and its lit pixels) at its preset's time: the
        energy class its flash, puff, spatter, glow and ember (40 ms) and its scorch (400 ms); ballistic its flash, dust,
        sparks, chips and pit glow and ember, no ring; plasma its bloom, globe, ring, sparks, glow and hot spot; heavy its
        entry streak, puff, jets, ring, glow and hole; ion its flash, arcs and arc sparks and no heat; a beam its spot (a
        flash), boiling puff, spatter and glow; the missile its flash, burst, debris, ring, glow and ember; repair its glow
        and ember only, the tug its glow; an asteroid its dust puff and rock sparks and chips, no flash, glow, ember or
        scorch; the lineup every class's element at once. The spark count is the stage's hashed law per set; the hull
        lifetime sets the loop; the canvas is 720 x 405."""
        presets = json.loads(re.search(r'const PRESETS = (\[.*?\]);', self.html).group(1))
        weapons = {}
        for view in ('stock_ap', 'mayhem3', 'fl'):
            m = re.search(r'\n%s: (\{.*?\}),\n' % json.dumps(view), self.html)
            if m:
                weapons[view] = {w['id']: w for w in json.loads(m.group(1))['weapons']}
        probe = self.ours_script() + self.CONTROLS + r"""
const checks = {};
function $(id) { return {value: vals[id], checked: !!checks[id]}; }
const PRESETS = __PRESETS__, WEAPONS = __WEAPONS__;
const out = {};
for (const p of PRESETS.filter(x => x.family === 'hull')) {
  Object.assign(vals, {c_family: 'hull', c_tier: p.tier, c_hull: p.hull, c_camera: p.camera, c_seta: p.seta, c_hits: p.hps,
                       c_variant: p.variant, c_k: p.k, c_shield: p.shield, c_class: p.cls, c_pulse_on: p.pulse[0],
                       c_pulse_period: p.pulse[1], c_sweep: p.sweep, c_loop: p.loop});
  checks.c_own = p.own; checks.c_cockpit = p.cockpit;
  const data = new Uint8ClampedArray(OW * OH * 4).fill(128);
  const weapon = WEAPONS[p.view][p.weapon];
  const at = t => {
    const rec = {slot: 'impact', ctx: {setTransform() {}, getImageData() { return {data: data.slice()}; }, putImageData() {}}, weapon};
    renderOurs(rec, t);
    return rec.hullStats;
  };
  const st = at(40), late = at(400), chase = at(200);
  out[p.id] = {anatomy: st.anatomy, light: st.light, lit_px: st.lit_px, lit: st.lit, sparks: st.sparks, scorch_late: late.light.scorch,
               ember_late: late.light.ember, sparks_chase: chase.light.sparks + chase.light.chips};
  const h = hullControls(), a = weaponAnatomy(weapon, h);
  // the stage's count per set (the classes whose sets flicker, ion and missile, light a share of theirs)
  out[p.id].want_sparks = p.hps === 0 && p.cls !== 'lineup' && !['beam', 'repair', 'tug', 'ion', 'missile'].includes(ANATOMIES[a])
    ? [0, 1, 2].map(s => sparkCount(7000, a === AN.tug ? 0.1 : p.k, h, a, s)) : null;
  out[p.id].stats = st;
}
vals.c_seta = 1; vals.c_loop = 0; out.loop = ourLoopMs();
out.canvas = [OW, OH];
console.log(JSON.stringify(out));
""".replace('__PRESETS__', json.dumps(presets)).replace('__WEAPONS__', json.dumps(weapons))
        js = self.run_node(probe)
        self.assertEqual(js.pop('canvas'), [720, 405])
        self.assertEqual(js.pop('loop'), 3601)                  # core::hull_lifetime_ms at SETA 1
        want_by = {'energy': {'flash', 'burst', 'sparks', 'glow', 'ember'},
                   'ballistic': {'flash', 'burst', 'sparks', 'chips', 'glow', 'ember'},
                   'plasma': {'flash', 'burst', 'sparks', 'ring', 'glow', 'ember'},
                   'heavy': {'flash', 'streak', 'burst', 'sparks', 'ring', 'glow', 'ember'},
                   'ion': {'flash', 'filament'},  # no sparks (the arc sparks dropped, review 2026-11-07)
                   'beam': {'flash', 'burst', 'glow'},
                   'missile': {'flash', 'burst', 'sparks', 'ring', 'glow', 'ember'},
                   'asteroid': {'burst', 'sparks', 'chips'},
                   'repair': {'flash', 'glow', 'ember'}, 'tug': {'glow'}}
        never_by = {'ion': {'burst', 'glow', 'ember', 'scorch', 'ring', 'sparks', 'chips'}, 'asteroid': {'flash', 'glow', 'ember', 'scorch'},
                    'repair': {'sparks', 'burst', 'scorch'}, 'tug': {'flash', 'sparks', 'burst', 'ember', 'scorch'},
                    'energy': {'ring', 'streak', 'filament'}, 'ballistic': {'ring', 'streak', 'filament'}}
        by_id = {p['id']: p for p in presets}
        for pid, st in js.items():
            p = by_id[pid]
            an = st['anatomy']
            if p['cls'] == 'lineup':
                want, never = {'flash', 'streak', 'burst', 'sparks', 'chips', 'ring', 'filament', 'glow', 'ember'}, set()
            else:
                want, never = want_by[an], never_by.get(an, set())
            # Out of view by the preset's geometry, neither required nor refused: the cockpit preset's first hit lies
            # below the canvas, the chase nose's ember sits inside the hull's outline seen from behind, the chase's sparks
            # clear the outline later (the fixture's 200 ms); the beam's spatter starts with its first 40 ms.
            unseen = {'hull/fighter-ire-cockpit-own': {'flash', 'sparks', 'ember', 'burst'},
                      'hull/fighter-ire-chase-nose': {'ember', 'sparks', 'glow'},  # the burst's light: R(t) small at 40 ms
                      'hull/heavy-rg-chase-nose': {'ember', 'sparks', 'chips', 'streak', 'ring', 'glow'}}.get(pid, set())
            for el in sorted(want | never):
                lit = (st['light'][el], st['lit_px'][el])
                if el in unseen:
                    continue
                if el in want:
                    self.assertGreater(lit[0], 0, (pid, an, el))
                    self.assertGreater(lit[1], 0, (pid, an, el))
                else:
                    self.assertEqual(lit, (0, 0), (pid, an, el))
            self.assertGreater(st['lit'], 0, pid)
            # the scorch at 400 ms (out of view on the chase nose seen from behind and below the cockpit's canvas)
            if an in ('energy', 'ballistic', 'plasma', 'heavy', 'missile') and not pid.endswith(('chase-nose', 'cockpit-own')):
                self.assertGreater(st['scorch_late'], 0, (pid, 'scorch at 400 ms'))
            if pid.endswith('chase-nose'):
                self.assertGreater(st['sparks_chase'], 0, (pid, 'sparks at 200 ms'))
            if st['want_sparks'] is not None and p['hps'] == 0:
                self.assertEqual(st['sparks'], sum(st['want_sparks']), pid)
        self.assertEqual(js['hull/fighter-ire-side']['anatomy'], 'energy')
        self.assertEqual(js['hull/ballistic-md-fighter-side']['anatomy'], 'ballistic')  # the Mayhem pin
        self.assertEqual(js['hull/ballistic-gauss-capital-300m']['anatomy'], 'heavy')   # 0x40 in the heavy band
        self.assertEqual(js['hull/beam-pbc-pulsed-corvette']['anatomy'], 'beam')
        self.assertEqual(js['hull/ion-ionc-capital']['anatomy'], 'ion')
        self.assertEqual(js['hull/repair-corvette']['anatomy'], 'repair')
        self.assertEqual(js['hull/mining-asteroid-60m']['anatomy'], 'asteroid')

    @unittest.skipUnless(shutil.which('node'), 'node not installed')
    def test_ion_lightning_and_beam_colour(self):
        """2026-11-07 (hull-impact-classes.md 4.5, 4.6): the ion Scenarios draw the lightning (stage::ion_strike's law:
        2..4 channels with 1..2 branches each over 16 sequences, 6..10 segments a channel; on the hull only), the layers
        "ion: channels", "ion: branches" and "ion: corona" each removing light, a re-strike another channel set, nothing
        at 400 ms game time; the beam Scenarios' held spot glow in the weapon colour's hue (within 10 degrees) with
        "beam: weapon colour" on, the heat ramp's with it off, the trail ember the same either way."""
        presets = json.loads(re.search(r'const PRESETS = (\[.*?\]);', self.html).group(1))
        weapons = {}
        for view in ('stock_ap', 'mayhem3', 'fl'):
            m = re.search(r'\n%s: (\{.*?\}),\n' % json.dumps(view), self.html)
            if m:
                weapons[view] = {w['id']: w for w in json.loads(m.group(1))['weapons']}
        probe = self.ours_script() + self.CONTROLS + r"""
const checks = {};
function $(id) { return {value: vals[id], checked: !!checks[id]}; }
const PRESETS = __PRESETS__, WEAPONS = __WEAPONS__, out = {law: [], scen: {}};
for (let i = 0; i < 16; i++) {
  const st = ionStrike(1 + 7919 * i, 0), mains = st.lines.filter(l => !l.branch);
  const per = [];
  st.lines.forEach((l, j) => { if (!l.branch) { let n = 0; for (let m = j + 1; m < st.lines.length && st.lines[m].branch; m++) n++; per.push(n); } });
  out.law.push({channels: st.channels, mains: mains.length, branches: per, segments: mains.map(l => l.pts.length - 1),
                strikes: ionStrikeCount(1 + 7919 * i)});
}
const ids = ['hull/ion-id-fighter-side', 'hull/ion-ionc-capital', 'hull/beam-pbc-pulsed-corvette', 'hull/beam-pal-sustained-sweep'];
for (const p of PRESETS.filter(x => ids.includes(x.id))) {
  Object.assign(vals, {c_family: 'hull', c_tier: p.tier, c_hull: p.hull, c_camera: p.camera, c_seta: p.seta, c_hits: p.hps,
                       c_variant: p.variant, c_k: p.k, c_shield: p.shield, c_class: p.cls, c_pulse_on: p.pulse[0],
                       c_pulse_period: p.pulse[1], c_sweep: p.sweep, c_loop: p.loop});
  checks.c_own = p.own; checks.c_cockpit = p.cockpit;
  const data = new Uint8ClampedArray(OW * OH * 4).fill(128);
  let weapon = WEAPONS[p.view][p.weapon];
  const at = (t, off = []) => {
    for (const k of LAYER_KEYS) layers[k] = !off.includes(k);
    layers.hull = false;
    const rec = {slot: 'impact', ctx: {setTransform() {}, getImageData() { return {data: data.slice()}; }, putImageData() {}}, weapon};
    renderOurs(rec, t);
    for (const k of LAYER_KEYS) layers[k] = true;
    return rec.hullStats;
  };
  const seta = Math.max(p.seta, 1e-3), r = {tint: weaponLinear(weapon), saturated: tintSaturated(weapon)};
  if (p.id.includes('/ion-')) {
    const s40 = at(40 / seta);
    r.ion = s40.ion; r.segments = s40.ion_segments; r.light = s40.light.filament; r.lit = s40.lit_px.filament;
    r.no_branches = at(40 / seta, ['ion_branches']).light.filament;
    r.no_corona = at(40 / seta, ['ion_corona']).light.filament;
    r.no_channels = at(40 / seta, ['ion_channels']).light.filament;
    const s70 = at(70 / seta); r.strike70 = s70.ion ? s70.ion.strike : -1;
    r.at400 = at(400 / seta).light.filament;
    r.heat = ['burst', 'glow', 'ember', 'scorch'].map(k => s40.light[k]);
  } else {
    const on = at(100 / seta), off = at(100 / seta, ['beam_colour']);
    r.glow_on = on.rgb.glow; r.glow_off = off.rgb.glow; r.flash_on = on.rgb.flash;
    r.ember_on = on.light.ember; r.ember_off = off.light.ember; r.burst_on = on.light.burst; r.burst_off = off.light.burst;
    // the same beam with a saturated green light colour (the Scenarios' own lights are white or grey: the game takes
    // their texture average, which the page has only with its assets)
    const green = Object.assign({}, weapon, {colour: [64, 255, 96]}), keep = weapon;
    weapon = green;
    const g = at(100 / seta);
    weapon = keep;
    r.green_tint = weaponLinear(green); r.green_glow = g.rgb.glow; r.green_flash = g.rgb.flash;
  }
  out.scen[p.id] = r;
}
console.log(JSON.stringify(out));
""".replace('__PRESETS__', json.dumps(presets)).replace('__WEAPONS__', json.dumps(weapons))
        js = self.run_node(probe)
        for row in js['law']:
            self.assertEqual(row['mains'], row['channels'])
            self.assertTrue(2 <= row['channels'] <= 4, row)
            self.assertTrue(all(1 <= n <= 2 for n in row['branches']), row)
            self.assertTrue(all(6 <= n <= 10 for n in row['segments']), row)
            self.assertIn(row['strikes'], (3, 4))
        self.assertEqual({r['channels'] for r in js['law']}, {2, 3, 4})

        def hue(c):
            mx, mn = max(c), min(c)
            if mx - mn <= 1e-9:
                return None
            if mx == c[0]:
                h = ((c[1] - c[2]) / (mx - mn)) % 6
            elif mx == c[1]:
                h = (c[2] - c[0]) / (mx - mn) + 2
            else:
                h = (c[0] - c[1]) / (mx - mn) + 4
            return 60 * h

        for pid in ('hull/ion-id-fighter-side', 'hull/ion-ionc-capital'):
            r = js['scen'][pid]
            self.assertTrue(2 <= r['ion']['channels'] <= 4, pid)
            self.assertGreater(r['segments'], 0, pid)
            self.assertGreater(r['lit'], 0, pid)
            for k in ('no_branches', 'no_corona', 'no_channels'):
                self.assertLess(r[k], r['light'], (pid, k))
            self.assertEqual(r['strike70'], 1, pid)
            self.assertEqual(r['at400'], 0, pid)
            self.assertEqual(r['heat'], [0, 0, 0, 0], pid)
        for pid in ('hull/beam-pbc-pulsed-corvette', 'hull/beam-pal-sustained-sweep'):
            r = js['scen'][pid]
            self.assertGreater(sum(r['glow_on']), 0, pid)
            self.assertAlmostEqual(sum(r['glow_on']), sum(r['glow_off']), delta=1e-6 * sum(r['glow_off']))  # its mean radiance
            self.assertEqual((r['ember_on'], r['burst_on']), (r['ember_off'], r['burst_off']), pid)    # the heat stays
            if hue(r['tint']) is None:  # a white or grey beam keeps the heat glow (review 2026-11-07, stage::beam_coloured)
                self.assertEqual(r['glow_on'], r['glow_off'], pid)
            for tint, glow in ((r['tint'], r['glow_on']), (r['green_tint'], r['green_glow'])):
                want = hue(tint)
                if want is not None:
                    got = hue(glow)
                    self.assertLessEqual(min(abs(got - want), 360 - abs(got - want)), 10.0, pid)
                    self.assertNotEqual(hue(r['glow_off']), got, pid)
            self.assertIsNotNone(hue(r['green_tint']))
            # the spot (flash core and halo) nearer white than its glow: lower saturation
            sat = lambda c: (max(c) - min(c)) / max(c)
            self.assertLess(sat(r['green_flash']), sat(r['green_glow']), pid)

    @unittest.skipUnless(shutil.which('node'), 'node not installed')
    def test_scenarios(self):
        """The Scenario selector (the presets, ids unchanged): grouped Shield / Hull, every preset a distinct readable
        label and a one-line description built from its fields in the page, the hand-written notes only for existing
        ids, choosing one switches the tab, and a later control change shows "(modified)"."""
        presets = json.loads(re.search(r'const PRESETS = (\[.*?\]);', self.html).group(1))
        self.assertIn('Scenario <select id="preset">', self.html)
        self.assertNotIn('>Preset <select', self.html)
        self.assertIn('''group('shield', 'Shield') + group('hull', 'Hull')''', self.html)
        self.assertIn("$('scenario_mod').textContent = p && state.scenarioModified ? '(modified)' : '';", self.html)
        self.assertIn("$('panels').addEventListener('change', touched);", self.html)
        self.assertIn('setTab(p.family);', self.html[self.html.index('function applyPreset('):self.html.index('function setView(')])
        script = re.search(r'<script>\n(.*)</script>', self.html, re.S).group(1)
        names = script[script.index('const SCENARIO_NOTES'):script.index('function fillScenarios(')]
        probe = self.ours_script() + self.CONTROLS + names + r"""
const PRESETS = __PRESETS__;
console.log(JSON.stringify({labels: Object.fromEntries(PRESETS.map(p => [p.id, scenarioLabel(p)])),
                            desc: Object.fromEntries(PRESETS.map(p => [p.id, scenarioDescription(p)])),
                            shown: scenarioLabels(),
                            notes: Object.keys(SCENARIO_NOTES)}));
""".replace('__PRESETS__', json.dumps(presets))
        js = self.run_node(probe)
        ids = [p['id'] for p in presets]
        self.assertEqual(sorted(js['labels']), sorted(ids))
        self.assertEqual(len(set(js['shown'].values())), len(ids))      # the selector's labels are distinct
        self.assertEqual(set(js['notes']) - set(ids), set())
        self.assertEqual(js['labels']['hull/fighter-ire-chase-nose'], 'Own fighter, IRE hit on the nose, chase view')
        self.assertEqual(js['desc']['hull/fighter-ire-chase-nose'],
                         'k 0.33, Mayhem 3, no SETA, single hit; checks the chase-view nose placement')
        self.assertIn('stock AP', js['desc']['hull/stock-vs-mayhem'])
        self.assertEqual(js['labels']['shield/capital-ppc-flank'], 'Capital, PPC shield hit on the flank, side view, 12 hits/s')
        self.assertEqual(js['labels']['hull/stock-vs-mayhem'], 'Fighter, IRE hit from above, side view, stock AP')
        self.assertIn('shield 60 %', js['desc']['shield/capital-ppc-flank'])
        for pid, text in js['labels'].items():
            self.assertNotIn('undefined', text + js['desc'][pid], pid)

    @unittest.skipUnless(shutil.which('node'), 'node not installed')
    def test_shock_front(self):
        """The ring's shock front (2026-11-07, hull-impact-classes.md 3.5; weapon_ring_ps.hlsl's law): shockFront around
        the front of four seeds at least 2:1 max / min (the noise's arcs), flat without it; the leading edge's peak over
        the trailing band's (the lead colour against lead + trail ratio); the wash inside the front only; the front's
        radius along an in-plane heading ring_stretch (1.25) times across. The four shock-front Scenarios at 60 ms: an
        oblique heading (SCENARIO_INCIDENCE), the ring lit, the noise and the wash each changing its light when off, the
        refraction band on the heavy and plasma hits (their shimmer rects) moving pixels and no light, none on the missile."""
        presets = json.loads(re.search(r'const PRESETS = (\[.*?\]);', self.html).group(1))
        weapons = {}
        for view in ('stock_ap', 'mayhem3', 'fl'):
            m = re.search(r'\n%s: (\{.*?\}),\n' % json.dumps(view), self.html)
            if m:
                weapons[view] = {w['id']: w for w in json.loads(m.group(1))['weapons']}
        incidence = re.search(r'const SCENARIO_INCIDENCE = (\{.*?\});', self.html, re.S).group(1).replace("'", '"')
        probe = self.ours_script() + self.CONTROLS + r"""
const checks = {};
function $(id) { return {value: vals[id], checked: !!checks[id]}; }
const PRESETS = __PRESETS__, WEAPONS = __WEAPONS__, SCENARIO_INCIDENCE = __INCIDENCE__;
const HH = HEADER.hull, out = {ratios: [], flat: []};
const rr = 1, w = 0.12;
for (const seq of [7000, 7001, 7002, 7003]) {
  const seed = HH.ring_seed_scale * unitOf(elementHash(seq, HH.ring_seed_element));
  let hi = 0, lo = Infinity, fhi = 0, flo = Infinity;
  for (let k = 0; k < 256; k++) {
    const ph = 2 * Math.PI * k / 256, q = [rr * Math.cos(ph), rr * Math.sin(ph), 0];
    const sf = shockFront(q, rr, w, seed, 0), v = sf.m * sf.lead, fv = shockFront(q, rr, w, -1, 0).lead;
    hi = Math.max(hi, v); lo = Math.min(lo, v); fhi = Math.max(fhi, fv); flo = Math.min(flo, fv);
  }
  out.ratios.push(hi / lo); out.flat.push(fhi / flo);
}
const lead = heatRamp(HH.ring_lead_t), trailC = heatRamp(HH.ring_trail_t), lum = c => (c[0] + c[1] + c[2]) / 3;
const trail = trailC.map((v, j) => HH.ring_trail_gain * v / lead[j]);
const radial = [];
for (let k = 0; k <= 400; k++) {
  const d = 0.5 + k / 400 * 0.7, sf = shockFront([d, 0, 0], rr, w, -1, HH.ring_wash_gain);
  radial.push({d, front: lum(lead.map((v, j) => v * (sf.lead + sf.band * trail[j]))), wash: sf.wash});
}
out.lead_peak = Math.max(...radial.filter(r => Math.abs(r.d - rr) < 0.02).map(r => r.front));
out.band_peak = Math.max(...radial.filter(r => r.d < rr - 0.6 * w && r.d > rr - HH.ring_trail_span * w).map(r => r.front));
out.wash_inside = Math.max(...radial.filter(r => r.d < rr).map(r => r.wash));
out.wash_outside = Math.max(...radial.filter(r => r.d >= rr).map(r => r.wash));
// the stretch: q = rel - (1 - 1 / E) a (a . rel) with a = x; the leading edge's peak along x over along y
const E = HH.ring_stretch, peak = dir => {
  let best = 0, at = 0;
  for (let k = 0; k <= 2000; k++) {
    const d = 0.8 + k / 2000 * 0.8, rel = dir.map(v => v * d), q = [rel[0] / E, rel[1], rel[2]], v = shockFront(q, rr, w, -1, 0).lead;
    if (v > best) { best = v; at = d; }
  }
  return at;
};
out.stretch = peak([1, 0, 0]) / peak([0, 1, 0]);
out.scenarios = {};
for (const id of Object.keys(SCENARIO_INCIDENCE)) {
  const p = PRESETS.find(x => x.id === id);
  if (!p) { out.scenarios[id] = null; continue; }
  Object.assign(vals, {c_family: 'hull', c_tier: p.tier, c_hull: p.hull, c_camera: p.camera, c_seta: p.seta, c_hits: p.hps,
                       c_variant: p.variant, c_k: p.k, c_shield: p.shield, c_class: p.cls, c_pulse_on: p.pulse[0],
                       c_pulse_period: p.pulse[1], c_sweep: p.sweep, c_loop: p.loop, c_incidence: SCENARIO_INCIDENCE[id]});
  checks.c_own = p.own; checks.c_cockpit = p.cockpit;
  const data = new Uint8ClampedArray(OW * OH * 4).fill(128), weapon = WEAPONS[p.view][p.weapon];
  const at = () => {
    let put = null;
    const rec = {slot: 'impact', ctx: {setTransform() {}, getImageData() { return {data: data.slice()}; }, putImageData(img) { put = img.data; }}, weapon};
    renderOurs(rec, 60);
    return {ring: rec.hullStats.light.ring, refractions: rec.hullStats.refractions, light: rec.hullStats.light,
            anatomy: rec.hullStats.anatomy, put};
  };
  const on = at();
  layers.ring_noise = false; const noNoise = at(); layers.ring_noise = true;
  layers.ring_wash = false; const noWash = at(); layers.ring_wash = true;
  layers.ring_refraction = false; const noRefr = at(); layers.ring_refraction = true;
  let moved = 0;
  for (let i = 0; i < on.put.length; i++) moved += on.put[i] !== noRefr.put[i];
  out.scenarios[id] = {anatomy: on.anatomy, ring: on.ring, no_noise: noNoise.ring, no_wash: noWash.ring, refractions: on.refractions,
                       moved, same_light: JSON.stringify(on.light) === JSON.stringify(noRefr.light)};
}
vals.c_incidence = 0;
console.log(JSON.stringify(out));
""".replace('__PRESETS__', json.dumps(presets)).replace('__WEAPONS__', json.dumps(weapons)).replace('__INCIDENCE__', incidence)
        js = self.run_node(probe)
        self.assertTrue(all(r >= 2.0 for r in js['ratios']), js['ratios'])
        self.assertTrue(all(abs(r - 1.0) < 1e-9 for r in js['flat']), js['flat'])
        self.assertGreater(js['lead_peak'], js['band_peak'])
        self.assertGreater(js['band_peak'], 0)
        self.assertGreater(js['wash_inside'], 0)
        self.assertEqual(js['wash_outside'], 0)
        self.assertAlmostEqual(js['stretch'], 1.25, delta=0.0625)
        self.assertEqual(sorted(js['scenarios']), ['hull/heavy-gke-capital-300m', 'hull/heavy-missile-hit',
                                                   'hull/heavy-rg-chase-nose', 'hull/plasma-psp-capital-close'])
        for pid, r in js['scenarios'].items():
            self.assertIsNotNone(r, pid)
            self.assertGreater(r['ring'], 0, pid)
            self.assertNotEqual(r['no_noise'], r['ring'], pid)
            self.assertLess(r['no_wash'], r['ring'], pid)
            self.assertTrue(r['same_light'], pid)                     # the refraction moves pixels, adds no light
            if r['anatomy'] == 'missile':
                self.assertEqual((r['refractions'], r['moved']), (0, 0), pid)
            else:
                self.assertGreater(r['refractions'], 0, pid)
                self.assertGreater(r['moved'], 0, pid)

    @unittest.skipUnless(shutil.which('node'), 'node not installed')
    def test_hull_layers_remove_their_element(self):
        """The Layers panel's glow (the soft bulb within class_glow r_hit of the contact) and flash halo: every hull preset
        at 40 ms renders with all layers on (the default), with glow off and with the flash halo off. Glow off leaves no
        glow light, pixel or count and every other element's light unchanged; the flash halo off keeps the glow and
        lowers the flash's light (the white core stays where it is in view). The hull body (on by default) paints the hull's
        pixels under the elements and leaves every element's light, lit pixels and count as with it off."""
        presets = json.loads(re.search(r'const PRESETS = (\[.*?\]);', self.html).group(1))
        weapons = {}
        for view in ('stock_ap', 'mayhem3', 'fl'):
            m = re.search(r'\n%s: (\{.*?\}),\n' % json.dumps(view), self.html)
            if m:
                weapons[view] = {w['id']: w for w in json.loads(m.group(1))['weapons']}
        probe = self.ours_script() + self.CONTROLS + r"""
const checks = {};
function $(id) { return {value: vals[id], checked: !!checks[id]}; }
const PRESETS = __PRESETS__, WEAPONS = __WEAPONS__;
layers.hull = true; // CONTROLS turns the hull body off for the pixel-counting shield tests; the page's default is on
const out = {defaults: Object.assign({}, layers)};
for (const p of PRESETS.filter(x => x.family === 'hull')) {
  Object.assign(vals, {c_family: 'hull', c_tier: p.tier, c_hull: p.hull, c_camera: p.camera, c_seta: p.seta, c_hits: p.hps,
                       c_variant: p.variant, c_k: p.k, c_shield: p.shield, c_class: p.cls, c_pulse_on: p.pulse[0],
                       c_pulse_period: p.pulse[1], c_sweep: p.sweep, c_loop: p.loop});
  checks.c_own = p.own; checks.c_cockpit = p.cockpit;
  const data = new Uint8ClampedArray(OW * OH * 4).fill(128), weapon = WEAPONS[p.view][p.weapon];
  const at = () => {
    let put = null;
    const rec = {slot: 'impact', ctx: {setTransform() {}, getImageData() { return {data: data.slice()}; }, putImageData(img) { put = img.data; }}, weapon};
    renderOurs(rec, 40);
    return {light: rec.hullStats.light, lit_px: rec.hullStats.lit_px, glows: rec.hullStats.glows, lit: rec.hullStats.lit,
            caption: rec.caption, put};
  };
  const on = at();
  layers.glow = false; const noGlow = at(); layers.glow = true;
  layers.star = false; const noHalo = at(); layers.star = true;
  layers.hull = false; const noBody = at(); layers.hull = true;
  let body = 0; // the pixels the hull body changes
  for (let i = 0; i < on.put.length; i += 4) body += on.put[i] !== noBody.put[i] || on.put[i + 2] !== noBody.put[i + 2];
  for (const r of [on, noGlow, noHalo, noBody]) delete r.put;
  out[p.id] = {on, noGlow, noHalo, noBody, body};
}
console.log(JSON.stringify(out));
""".replace('__PRESETS__', json.dumps(presets)).replace('__WEAPONS__', json.dumps(weapons))
        js = self.run_node(probe)
        self.assertTrue(all(js.pop('defaults').values()))               # all layers on by default
        glowing = 0
        for pid, r in js.items():
            on, no_glow, no_halo, no_body = r['on'], r['noGlow'], r['noHalo'], r['noBody']
            # the hull body only paints the pixels under the elements: every element's light, lit pixels and count stay
            self.assertEqual((no_body['light'], no_body['lit_px'], no_body['lit']), (on['light'], on['lit_px'], on['lit']), pid)
            self.assertGreater(r['body'], 0, pid)                       # the hull body shows (36 px for a fighter at 2 km)
            self.assertIn('hidden: hull body', no_body['caption'])
            self.assertEqual((no_glow['light']['glow'], no_glow['lit_px']['glow'], no_glow['glows']), (0, 0, 0), pid)
            for el, v in on['light'].items():
                if el != 'glow':
                    self.assertEqual(no_glow['light'][el], v, (pid, el))
            if on['light']['glow'] > 0:
                glowing += 1
                if on['light']['ring'] > 0:  # the ring's wash lights the hull inside its front: it may cover the bulb
                    self.assertLessEqual(no_glow['lit'], on['lit'], pid)
                else:
                    self.assertLess(no_glow['lit'], on['lit'], pid)
                self.assertIn('hidden: glow', no_glow['caption'])
            self.assertEqual(no_halo['light']['glow'], on['light']['glow'], pid)
            if on['light']['flash'] > 0:
                self.assertLess(no_halo['light']['flash'], on['light']['flash'], pid)
            self.assertTrue(on['caption'].startswith('Hull hit, '), on['caption'])
            self.assertNotIn('hidden', on['caption'])
        self.assertGreaterEqual(glowing, 8)                             # the classes with a glow light it at 40 ms

    @unittest.skipUnless(shutil.which('node'), 'node not installed')
    def test_stage_draws_on_a_tainted_canvas(self):
        """Opened from file://, the native impact sprite's texture taints the stage and getImageData on it throws. The
        renderers take the stage's pixels from the background canvas (stageImage), so the close plasma capital preset
        still draws the hull body and the hit at 40, 150, 400 and 900 ms with a stage whose getImageData throws."""
        script = re.search(r'<script>\n(.*)</script>', self.html, re.S).group(1)
        self.assertEqual(script.count('getImageData('), 1)                # only stageImage, on the background canvas
        self.assertIn("backgrounds[key].getContext('2d').getImageData(", script)
        presets = json.loads(re.search(r'const PRESETS = (\[.*?\]);', self.html).group(1))
        p = next(x for x in presets if x['id'] == 'hull/plasma-psp-capital-close')
        weapon = next(w for w in json.loads(re.search(r'\n%s: (\{.*?\}),\n' % json.dumps(p['view']), self.html).group(1))
                      ['weapons'] if w['id'] == p['weapon'])
        probe = self.ours_script() + self.CONTROLS + r"""
const checks = {};
function $(id) { return {value: vals[id], checked: !!checks[id]}; }
function stageImage() { return {data: new Uint8ClampedArray(OW * OH * 4).fill(8)}; }
const p = __P__, weapon = __W__;
Object.assign(vals, {c_family: 'hull', c_tier: p.tier, c_hull: p.hull, c_camera: p.camera, c_seta: p.seta, c_hits: p.hps,
                     c_variant: p.variant, c_k: p.k, c_shield: p.shield, c_class: p.cls, c_pulse_on: p.pulse[0],
                     c_pulse_period: p.pulse[1], c_sweep: p.sweep, c_loop: p.loop, c_incidence: 60});
const tainted = {setTransform() {}, getImageData() { throw new Error('SecurityError: tainted'); }};
const at = t => { let put = null; const rec = {slot: 'impact', ctx: Object.assign({putImageData(img) { put = img.data; }}, tainted), weapon};
                  renderOurs(rec, t); return {put, light: rec.hullStats.light}; };
const out = {};
for (const t of [40, 150, 400, 900]) {
  layers.hull = true; const on = at(t); layers.hull = false; const off = at(t);
  let body = 0;
  for (let i = 0; i < on.put.length; i += 4) body += on.put[i] !== off.put[i];
  out[t] = {body, light: Object.values(on.light).reduce((a, b) => a + Math.max(b, 0), 0)};
}
console.log(JSON.stringify(out));
""".replace('__P__', json.dumps(p)).replace('__W__', json.dumps(weapon))
        js = self.run_node(probe)
        for t, r in js.items():
            self.assertGreater(r['body'], 0.2 * 720 * 405, t)            # the close capital fills about 0.38 of the stage
            self.assertGreater(r['light'], 0, t)                         # the hit's elements light the stage

    def test_scenario_select_is_synchronous(self):
        """Choosing a scenario sets the view's weapon list, the weapon and the controls and draws at once (LAB_DATA is
        inline; only assets.js loads later and redraws); window.labReady resolves after that view's assets settled and
        the stage drew; the animation loop reschedules before drawing, so a draw that throws cannot stop it."""
        script = re.search(r'<script>\n(.*)</script>', self.html, re.S).group(1)
        apply = script[script.index('function applyPreset('):script.index('function setView(')]
        self.assertNotIn('loadAssets(', apply)
        self.assertIn('fillWeapons(); }', apply)
        self.assertIn('settle(p.view);', apply)
        settle = script[script.index('function settle('):script.index('function drawNative(')]
        self.assertLess(settle.index('draw();'), settle.index('loadAssets('))
        self.assertIn('window.labReady = new Promise(', settle)
        frame = script[script.index('function frame('):script.index('function draw()')]
        self.assertTrue(frame.split('\n')[1].strip().startswith('requestAnimationFrame(frame);'))
        draw = script[script.index('function draw()'):script.index('function setTab(')]
        self.assertIn('} catch (e) {', draw)
        self.assertIn('window.labReady = Promise.resolve();', script)    # no data block: nothing to wait for

    @unittest.skipUnless(shutil.which('node'), 'node not installed')
    def test_fireball_drift_law_matches_the_program(self):
        """The fireball noise's drift law (2026-11-07, docs/verification/weapon-effects.md "Fireball drift law"): the
        program and both lab body loops (the hull burst's puffAt, the death body) sample octave i at p (f_i - g_i), g_i =
        (f_i / 2) / sqrt(1 + (|p| f_i / 2a)^2); no seed-hashed centre direction (the old pinhole blend) is left. The radial
        map u -> u - a y / sqrt(1 + y^2), y = u / 2a, is strictly increasing (slope >= 1/2: no circle collapses onto one
        noise point) and tends to the full shift u - a."""
        ps = (ROOT / 'src/effects/weapon_fireball_ps.hlsl').read_text()
        self.assertIn('float k = 0.5 / max(advect, 1e-4);', ps)
        self.assertIn('float3 g = 0.5 * freq.xyz * rsqrt(1.0 + dot(p, p) * freq.xyz * freq.xyz * (k * k));', ps)
        for k in 'xyz':
            self.assertIn(f'value_noise(p * (freq.{k} - g.{k})', ps)
        self.assertNotIn('hdir', ps)
        self.assertEqual(self.html.count('const gk = 0.5 / Math.max(adv, 1e-4), pp = (px * px + py * py) * gk * gk;'), 2)
        for i in range(3):
            self.assertEqual(self.html.count(f'g{i} = 0.5 * fq[{i}] / Math.sqrt(1 + pp * fq[{i}] * fq[{i}])'), 2)
            self.assertEqual(self.html.count(f'valueNoise2(px * (fq[{i}] - g{i})'), 2)
        self.assertNotIn('pr / 0.08', self.html)
        for a in (0.05, 0.5, 1.0):
            u = np.linspace(0.0, 400.0 * a, 40001)
            rho = u - 0.5 * u / np.sqrt(1.0 + (u / (2 * a)) ** 2)
            self.assertGreaterEqual(float(np.diff(rho).min() / (u[1] - u[0])), 0.5 - 1e-6, a)
            self.assertAlmostEqual(float(rho[-1]), float(u[-1] - a), delta=1e-4 * a)

    def test_lab_classes_match_the_census(self):
        """The lab's `auto` class of every catalogued weapon (LAB_DATA weapons[].hc: the generator's rule and the override
        file's pins) is the census script's (verification/results/weapon-effects/hull_class_census.py)."""
        sys.path.insert(0, str(gen.REPO / 'verification/results/weapon-effects'))
        import hull_class_census as census  # noqa: E402
        pins = gen.hull_pins()
        self.assertEqual(pins, census.PINS)
        for view in ('stock_ap', 'mayhem3', 'fl'):
            cat = json.loads((gen.CATALOG / f'{view}.json').read_text())
            for w in cat['weapons']:
                flags = int(w.get('flag_word') or '0', 16) if isinstance(w.get('flag_word'), str) else int(w.get('flag_word') or 0)
                dmg = max(w.get('damage_shield') or 0, w.get('damage_hull') or 0)
                want = census.PINS.get(w['id']) or census.classify(flags, dmg, w.get('elongation'))
                self.assertEqual(gen.hull_class_of(w, pins), want, (view, w['id']))

    @unittest.skipUnless(shutil.which('node'), 'node not installed')
    def test_spark_hashes_match_the_stage(self):
        """The lab's sparks are the stage's: mix32, element_hash, spark_count, spark_factors and pair_direction in 32-bit
        arithmetic give the C++ values (a host build of weapon_effects_hull.h prints them for three sequences)."""
        probe = self.ours_script() + self.CONTROLS + r"""
const out = [];
for (const seq of [1, 438, 7000]) {
  const h = {variant: 'none', spark_x: 1};
  const f = sparkFactors(seq, 3, 0.3), d = pairDirection(seq, 2);
  out.push([elementHash(seq, 7), sparkCount(seq, 0.5, h, 1, 0), +f.speed_x.toFixed(5), +f.life_x.toFixed(5), +f.len_x.toFixed(5),
            +f.width_x.toFixed(5), f.curl ? 1 : 0, f.curl_hash, ...d.map(v => +v.toFixed(5))]);
}
console.log(JSON.stringify(out));
"""
        js = self.run_node(probe)
        cxx = shutil.which('g++') or shutil.which('clang++')
        if not cxx:
            self.skipTest('no host C++ compiler')
        src = r"""
#include "weapon_effects_hull.h"
#include <cstdio>
namespace st = x3m::weapon_effects::stage;
namespace core = x3m::weapon_effects::core;
int main() {
    for (unsigned seq : {1u, 438u, 7000u}) {
        core::Event e{};
        e.sequence = seq;
        e.table = core::table_bullets;
        const st::SparkFactors f = st::spark_factors(seq, 3, 0.3f);
        float d[3];
        st::pair_direction(seq, 2, d);
        std::printf("%u %u %.5f %.5f %.5f %.5f %u %u %.5f %.5f %.5f\n", st::element_hash(seq, 7),
                    st::spark_count(e, 0.5f, st::HullTuning{}, core::hull::an_ballistic, 0), double(f.speed_x),
                    double(f.life_x), double(f.len_x), double(f.width_x), unsigned(f.curl), f.curl_hash, double(d[0]),
                    double(d[1]), double(d[2]));
    }
}
"""
        sys.path.insert(0, str(Path(__file__).resolve().parent))  # under PYTHONPATH=verification/probe
        import test_weapon_effects_core as core_test
        with tempfile.TemporaryDirectory() as tmp:
            Path(tmp, 'd3d9.h').write_text(core_test.D3D9_STUB)
            Path(tmp, 'p.cpp').write_text(src)
            exe = Path(tmp, 'p')
            subprocess.run([cxx, '-std=c++17', '-O2', '-msse2', '-I', tmp, '-I', str(gen.REPO / 'src/proxy'),
                            str(Path(tmp, 'p.cpp')), '-o', str(exe)], check=True)
            rows = [line.split() for line in subprocess.run([str(exe)], capture_output=True, text=True,
                                                             check=True).stdout.splitlines()]
        for got, want in zip(js, rows):
            self.assertEqual(got[0], int(want[0]))
            self.assertEqual(got[1], int(want[1]))
            for a, b in zip(got[2:6], want[2:6]):
                self.assertAlmostEqual(a, float(b), places=4)
            self.assertEqual((got[6], got[7]), (int(want[6]), int(want[7])))
            for a, b in zip(got[8:], want[8:]):
                self.assertAlmostEqual(a, float(b), places=4)

    @unittest.skipUnless(shutil.which('node'), 'node not installed')
    def test_bubble_presets_near_far_and_chase(self):
        """5.1.1 Placement (user decision after Run238 Flight E): a ship's patch on its bubble (the header's scale and far
        gain by default), the chase preset's nose hit drawing on the far side, the far gain at 0 drawing no far side, the
        nose hit never lighting the -z face (the tail), the station surface on the hull decal."""
        probe = self.ours_script() + self.CONTROLS + r"""
const checks = {};
function $(id) { return {value: vals[id], checked: !!checks[id]}; }
const render = () => {
  const data = new Uint8ClampedArray(W * H * 4), rec = {slot: 'impact', ctx: {setTransform() {}, getImageData() { return {data}; }, putImageData() {}}, weapon: {}};
  renderOurs(rec, 400);
  let lit = 0;
  for (let i = 0; i < W * H; i++) lit += data[i * 4 + 2] > 0;
  const c = ourControls(), hull = previewHull(c), E = bubbleOf(hull, c);
  const seeds = bubbleDraws(bubbleHits(hitSequence(400, c, hull.S, c.cell_m[c.tier]), E), c, E).map(d => d.seed);
  return {lit, near: rec.bubbleLit.near, far: rec.bubbleLit.far, seeds, scale: c.bubble_scale, gain: c.bubble_far, E};
};
const out = {};
vals.c_hull = 'chase_nose'; out.chase = render();
vals.c_bubble_far = 0; out.chase_no_far = render();
delete vals.c_bubble_far;
vals.c_hull = 'sphere'; out.sphere = render();
vals.c_surface = 'hull'; out.station = render();
console.log(JSON.stringify(out));
"""
        js = self.run_node(probe)
        ch = js['chase']
        self.assertAlmostEqual(ch['scale'], 1.15)
        self.assertAlmostEqual(ch['gain'], 0.6)
        self.assertGreater(ch['far'], 50)                      # the nose hit seen from behind: the far side
        # its near side outside the footprint (at 720 x 405 a pixel or two on the limb, where the two roots meet)
        self.assertLessEqual(ch['near'], 0.05 * ch['far'])
        self.assertIn(0, ch['seeds'])                          # +z at the nose
        self.assertNotIn(1, ch['seeds'])                       # the -z lattice's draw dropped (the tail: outside the window)
        self.assertEqual(js['chase_no_far']['far'], 0)
        self.assertLess(js['chase_no_far']['lit'], ch['lit'])
        self.assertAlmostEqual(js['sphere']['E'][0], 1.15 * 15, places=6)  # at the 1.15 x radius bound, not cut
        self.assertGreater(js['sphere']['near'], 100)         # the front hit: the near side
        self.assertEqual((js['station']['near'], js['station']['far']), (0, 0))
        self.assertGreater(js['station']['lit'], 100)         # the hull decal
        for cid in ('c_surface', 'c_bubble_scale', 'c_bubble_far'):
            self.assertIn(f'id="{cid}"', self.html)
        self.assertIn('value="chase_nose"', self.html)

    @unittest.skipUnless(shutil.which('node'), 'node not installed')
    def test_cell_map_holds_clocks_at_12_hits_per_second(self):
        """The lab's cell map under the 12 hits/s player at 60 frames per second for 4 s: a cell's clock never jumps
        ahead by more than the frame (no pop: it fades on its own clock or is reset by a newer hit), a shared cell is
        held once, and the map stays within its 64 cells."""
        probe = self.ours_script() + self.CONTROLS + r'''
vals.c_hits = 12;
const c = ourControls(), S = PREVIEW_OWNER_M[0], cell = c.cell_m[0];
let prev = new Map(), worst = 0, maxCells = 0, maxLive = 0;
for (let f = 0; f <= 240; f++) {
  const t = f * 1000 / 60, hits = hitSequence(t, c, S, cell), draws = patchDraws(hits, S, c);
  maxLive = Math.max(maxLive, hits.length);
  const cur = new Map();
  for (const d of draws) for (const [k, v] of d.cells) cur.set(d.seed * 1e8 + k, v.since);
  maxCells = Math.max(maxCells, ...draws.map(d => d.cells.size), 0);
  for (const [k, v] of prev) {
    const now = cur.has(k) ? cur.get(k) : Infinity, fadeMin = c.fade_s * (1 - 0.5 * c.stagger);
    if (v >= 0 && v < fadeMin) worst = Math.max(worst, now - v - 1 / 60); // lit cells (a ring's unlit cells carry since < 0)
  }
  prev = cur;
}
// hits per game second: SETA x6 at a sixth of the wall time holds the same live hits at the same game ages
const at1 = hitSequence(2010, c, S, cell); // between two hits (no rounding at a boundary)
vals.c_seta = 6;
const at6 = hitSequence(2010 / 6, ourControls(), S, cell);
console.log(JSON.stringify({worst, maxCells, maxLive, n1: at1.length, n6: at6.length,
                            ageGap: Math.max(...at1.map((h, i) => Math.abs(h.age - (at6[i] ? at6[i].age : 1e9))))}));
'''
        js = self.run_node(probe)
        self.assertLessEqual(js['worst'], 1e-6)
        self.assertLessEqual(js['maxCells'], 128)
        self.assertGreaterEqual(js['maxLive'], 30)
        self.assertEqual(js['n1'], js['n6'])
        self.assertLess(js['ageGap'], 1e-9)

    @unittest.skipUnless(shutil.which('node'), 'node not installed')
    def test_patch_law_matches_the_gpu(self):
        """The lab's hexLaw against the native D3D fixture's readbacks of the same cases (the fighter patch, the hit on
        a cell centre, seed 0, its cell map from fillCells): the hit point at 0.05 s with and without the ring (no core
        term), the cells-only hit cell (the marker level) and the next cell (the ring level) at 0.5 s, and the
        cells-only hit cell at 0.7 s and 0.3 s. Tolerance: the FP16 target's step (2^-10 relative)."""
        record = json.loads((ROOT / 'verification/results/native-windows/weapon-effects/summary.json').read_text())
        centre, relight = record['metrics']['centre'][0], record['metrics']['relight'][0]
        script = re.search(r'<script>\n(.*)</script>', self.html, re.S).group(1)
        header = script[script.index(gen.HEADER_BEGIN):script.index(gen.END, script.index(gen.HEADER_BEGIN))]
        ours = script[script.index('// ---------------------------------------------------------------- our renderer'):
                      script.index('// ---------------------------------------------------------------- helpers')]
        probe = header + ours + r'''
const c = {stagger: HEADER.stagger, fill_ratio: HEADER.fill_ratio, fade_s: HEADER.fade_ms / 1000,
           centre_gain: HEADER.centre_gain, ripple_speed: HEADER.ripple_speed};
const CELL = HEADER.cell_m[0] * HEADER.engine_units_per_metre * 0.01; // the fixture's fighter cell, view units
const at = (age, over, x) => {
  const saved = Object.assign({}, HEADER);
  Object.assign(HEADER, over || {});
  const d = Object.assign({cell: CELL, seed: 0}, fillCells([{q: [0, 0], age}], CELL, HEADER.rings[0], c));
  const q = [x || 0, 0];
  const v = hexLaw(q, hexCell(q[0] / CELL, 0), 1e-3, d, c);
  Object.assign(HEADER, saved);
  return v;
};
const cells = {ring_gain: 0};
console.log(JSON.stringify({at05: at(0.05), at05_cells: at(0.05, cells), hit_cell: at(0.5, cells),
                            ring_cell: at(0.5, cells, CELL), old: at(0.7, cells), new: at(0.3, cells)}));
'''
        with tempfile.NamedTemporaryFile('w', suffix='.js', delete=False) as f:
            f.write(probe.replace('const HEADER', 'var HEADER'))
        try:
            r = subprocess.run(['node', f.name], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr[:500])
            js = json.loads(r.stdout)
        finally:
            Path(f.name).unlink()
        gpu = {'at05': centre['at05'], 'at05_cells': centre['at05_cells'], 'hit_cell': centre['hit_cell'],
               'ring_cell': centre['ring_cell'], 'old': relight['old'], 'new': relight['new']}
        for key, value in gpu.items():
            self.assertAlmostEqual(js[key], value, delta=max(2e-3, 2 ** -10 * value), msg=key)


if __name__ == '__main__':
    unittest.main()
