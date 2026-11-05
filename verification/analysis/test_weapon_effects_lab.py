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
                 f'{gen.BEGIN}\nconst LAB_DATA = {{}};\n{gen.END}\nrest();\n</script>')
        page = gen.render(shell, data, hv)
        self.assertEqual(gen.render(page, data, hv), page)              # idempotent
        self.assertIn('const HEADER = {', page)
        self.assertIn('ripple_speed:', page)
        self.assertTrue(page.endswith('\nrest();\n</script>'))
        dstart = page.index('const LAB_DATA = {') + len('const LAB_DATA = {')
        body = page[dstart:page.index('};\n' + gen.END, dstart)]
        view, rec = body.strip().rstrip(',').split(': ', 1)
        self.assertEqual(json.loads(view), 'stock_ap')
        self.assertEqual(json.loads(rec), json.loads(json.dumps(data['stock_ap'])))
        with self.assertRaises(SystemExit):
            gen.render('<script></script>', data, hv)

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
        self.assertEqual(self.html.count(gen.END), 2)   # the header block and the data block both close with /* @end */
        self.assertIn('const HEADER = {', self.html)    # the injected header constants
        self.assertRegex(self.html, r'ripple_speed: [0-9.]+')
        self.assertRegex(self.html, r'cell_m: \[[0-9., ]+\]')
        self.assertNotRegex(self.html, r'function renderOurs\(record, t\) \{ // eslint-disable-line no-unused-vars\n\}')
        self.assertIn('function ourControls()', self.html)
        # docs/architecture/weapon-effects.md 5.1.1's lab controls: cell size and radius per tier, ripple speed, core
        # and fade durations, stagger, edge versus fill, the three colour bands, the SETA rate, the hit player.
        for cid in ('c_cell_fighter', 'c_cell_corvette', 'c_cell_capital', 'c_radius_fighter', 'c_radius_corvette',
                    'c_radius_capital', 'c_ripple_speed', 'c_core_ms', 'c_fade_ms', 'c_stagger', 'c_fill_ratio',
                    'c_colour_high', 'c_colour_mid', 'c_colour_low', 'c_colour_full', 'c_colour_half', 'c_colour_empty',
                    'c_weapon_tint', 'c_seta', 'c_hits'):
            self.assertIn(f'id="{cid}"', self.html)
        self.assertRegex(self.html, r'<select id="c_hits">.*value="1".*value="4".*value="12"')
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


    @unittest.skipUnless(shutil.which('node'), 'node not installed')
    def test_patch_law_matches_the_gpu(self):
        """The lab's hexLaw at the hit point against the native D3D fixture's readbacks of the same cases (the fighter
        patch, the hit on a cell centre, seed 0): the core at 0.05 s with and without the core term, 0.2 s, and the
        cells-only hit point at 0.7 s and 0.3 s. Tolerance: the FP16 target's step (2^-10 relative)."""
        record = json.loads((ROOT / 'verification/results/native-windows/weapon-effects/summary.json').read_text())
        core, relight = record['metrics']['core'][0], record['metrics']['relight'][0]
        script = re.search(r'<script>\n(.*)</script>', self.html, re.S).group(1)
        header = script[script.index(gen.HEADER_BEGIN):script.index(gen.END, script.index(gen.HEADER_BEGIN))]
        ours = script[script.index('// ---------------------------------------------------------------- our renderer'):
                      script.index('// ---------------------------------------------------------------- helpers')]
        probe = header + ours + r'''
const c = {stagger: HEADER.stagger, fill_ratio: HEADER.fill_ratio, fade_s: HEADER.fade_ms / 1000,
           core_s: HEADER.core_ms / 1000, ripple_speed: HEADER.ripple_speed};
const at = (age, over) => {
  const saved = Object.assign({}, HEADER);
  Object.assign(HEADER, over || {});
  const v = hexLaw([0, 0], hexCell(0, 0), 1e-3, {radius: 45, cell: 7.5, seed: 0, hits: [{q: [0, 0], age}]}, c);
  Object.assign(HEADER, saved);
  return v;
};
console.log(JSON.stringify({at05: at(0.05), at05_no_core: at(0.05, {core_gain: 0}), at20: at(0.2),
                            old: at(0.7, {core_gain: 0, ring_gain: 0}), new: at(0.3, {core_gain: 0, ring_gain: 0})}));
'''
        with tempfile.NamedTemporaryFile('w', suffix='.js', delete=False) as f:
            f.write(probe.replace('const HEADER', 'var HEADER'))
        try:
            r = subprocess.run(['node', f.name], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr[:500])
            js = json.loads(r.stdout)
        finally:
            Path(f.name).unlink()
        gpu = {'at05': core['at05'], 'at05_no_core': core['at05_no_core'], 'at20': core['at20'],
               'old': relight['old'], 'new': relight['new']}
        for key, value in gpu.items():
            self.assertAlmostEqual(js[key], value, delta=max(2e-3, 2 ** -10 * value), msg=key)


if __name__ == '__main__':
    unittest.main()
