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
        presets = [dict(gen.PRESETS[0], view='stock_ap', weapon='SS_BULLET_X'),
                   dict(gen.PRESETS[2], view='stock_ap', weapon='SS_BULLET_X')]
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
                    'SS_BULLET_GKE', 'SS_BULLET_RG', 'SS_BULLET_PBC', 'SS_BULLET_ID')]},
                'stock_ap': {'weapons': [{'id': i, 'hc': hc.get(i, 'energy')} for i in (
                    'SS_BULLET_IRE', 'SS_BULLET_MASS', 'SS_BULLET_FLAK_ALPHA', 'SS_BULLET_GAUSS', 'SS_BULLET_PAL',
                    'SS_BULLET_IONC')]}}
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
                                                'energy-ire-fighter-side', 'ballistic-md-fighter-side',
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
        self.assertEqual([(t[0], t[2].strip()) for t in tabs],
                         [('shield', 'Shield hit'), ('hull', 'Hull hit'), ('vanilla', 'Vanilla'),
                          ('explosion', 'Explosion'), ('bolt', 'Bolt'), ('beam', 'Beam')])
        self.assertEqual([t[0] for t in tabs if not t[1]], ['shield', 'hull', 'vanilla'])
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
        halo, burst, sparks, chips, entry streak, ring, glow, ember, scorch, filaments and the shimmer's disabled
        placeholder; shield: patch, bubble outline, native impact sprite; vanilla: the tint, travel, flare and light
        options), "all" and "none", the state persisted in localStorage inside try/catch and read by the renderers."""
        panel = re.search(r'<fieldset id="p_layers">(.*?)</fieldset>', self.html, re.S).group(1)
        boxes = re.findall(r'<label class="check[^"]*" data-tabs="([\w ]+)"[^>]*><input id="(\w+)" type="checkbox"([^>]*)>', panel)
        by_tab = {}
        for tab, cid, attrs in boxes:
            by_tab.setdefault(tab, []).append(cid)
        self.assertEqual(by_tab['hull'], ['layer_flash', 'layer_flash_halo', 'layer_burst', 'layer_sparks', 'layer_chips',
                                          'layer_streak', 'layer_ring', 'layer_glow', 'layer_ember', 'layer_scorch',
                                          'layer_filament', 'layer_shimmer'])
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
        for name in ('glow', 'flash', 'flash_halo', 'ember', 'ring', 'scorch', 'filament', 'burst'):
            self.assertIn(f'layers.{name}', render_hull, name)
        self.assertIn('if (layers.glow && gw[0] > 0 && a < glowLife)', render_hull)
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
function drawBackground() {} function label() {}
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
                   'ion': {'flash', 'filament', 'sparks'},
                   'beam': {'flash', 'burst', 'glow'},
                   'missile': {'flash', 'burst', 'sparks', 'ring', 'glow', 'ember'},
                   'asteroid': {'burst', 'sparks', 'chips'},
                   'repair': {'glow', 'ember'}, 'tug': {'glow'}}
        never_by = {'ion': {'burst', 'glow', 'ember', 'scorch', 'ring'}, 'asteroid': {'flash', 'glow', 'ember', 'scorch'},
                    'repair': {'flash', 'sparks', 'burst', 'scorch'}, 'tug': {'flash', 'sparks', 'burst', 'ember', 'scorch'},
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
                      'hull/fighter-ire-chase-nose': {'ember', 'sparks'},
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
  layers.flash_halo = false; const noHalo = at(); layers.flash_halo = true;
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
                self.assertLess(no_glow['lit'], on['lit'], pid)
                self.assertIn('hidden: glow', no_glow['caption'])
            self.assertEqual(no_halo['light']['glow'], on['light']['glow'], pid)
            if on['light']['flash'] > 0:
                self.assertLess(no_halo['light']['flash'], on['light']['flash'], pid)
            self.assertTrue(on['caption'].startswith('Hull hit, '), on['caption'])
            self.assertNotIn('hidden', on['caption'])
        self.assertGreaterEqual(glowing, 8)                             # the classes with a glow light it at 40 ms

    @unittest.skipUnless(shutil.which('node'), 'node not installed')
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
