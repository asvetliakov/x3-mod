"""tools/analysis/fog_families.py: palette rule, bake, file writer, --check and install guards."""
import contextlib
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import fog_families as ff  # noqa: E402
import sector_fog_census as sfc  # noqa: E402

recipe, baker = ff.recipe, ff.baker
PROVISIONAL = [name for name, p in recipe.PROFILES.items() if p.get('density_status') == 'provisional_artistic']


def dds_dxt1(width, height, blocks):
    head = bytearray(128)
    head[:4] = b'DDS '
    struct.pack_into('<IIIIIII', head, 4, 124, 0x1007, height, width, 0, 0, 1)
    struct.pack_into('<II4s', head, 76, 32, 4, b'DXT1')
    return bytes(head) + b''.join(blocks)


def dxt1_block(c0, c1, indices):
    bits = sum((index & 3) << (2 * i) for i, index in enumerate(indices))
    return struct.pack('<HHI', c0, c1, bits)


def dds_rgb24(pixels):
    height, width = pixels.shape[:2]
    head = bytearray(128)
    head[:4] = b'DDS '
    struct.pack_into('<IIIIIII', head, 4, 124, 0x100f, height, width, width * 3, 0, 1)
    struct.pack_into('<III', head, 76, 32, 0x40, 0)
    struct.pack_into('<I4I', head, 88, 24, 0xff0000, 0xff00, 0xff, 0)
    return bytes(head) + pixels[..., ::-1].astype(np.uint8).tobytes()  # BGR in memory


def background_line(family, rates, dust, index):
    values = ['0', '0', '0.000000', '0.000000', '0.000000', '0', '0', family, '1', '1', '1', *map(str, rates), str(dust),
              '0', '0', '0', '0', '1000', '2000', '100', '120', '120', '120', '0', '0', '0', '0', '0', '0', '0', f'SS_BG_{index}']
    assert len(values) == 38
    return ';'.join(values) + ';'


def body(textures):
    lines = ['1000; / Automatic body size']
    lines += [f'MATERIAL6:{i};33554432;0;nebulafog.fx;1;t_DiffuseTexture;8;{t};' for i, t in enumerate(textures)]
    return ('\n'.join(lines) + '\n').encode()


class PaletteRuleTests(unittest.TestCase):
    def test_dxt1_decode_is_floor_expansion_with_truncating_interpolation(self):
        # red 4 -> 32 (floor; bit replication gives 33), red 2 -> 16; (2*32+16)//3 = 26 (rounding gives 27).
        block = dxt1_block(4 << 11, 2 << 11, [0, 1, 2, 3] * 4)
        rgb = ff.decode_rgb(dds_dxt1(4, 4, [block]))
        self.assertEqual(sorted(set(rgb[:, 0].tolist())), [16, 21, 26, 32])
        self.assertEqual(rgb[:4, 0].tolist(), [32, 16, 26, 21])
        three = dxt1_block(2 << 11, 4 << 11, [0, 1, 2, 3] * 4)  # c0 <= c1: (a+b)//2 and black
        self.assertEqual(ff.decode_rgb(dds_dxt1(4, 4, [three]))[:4, 0].tolist(), [16, 32, 24, 0])

    def test_uncompressed_and_unsupported_formats(self):
        pixels = np.zeros((4, 4, 3), np.uint8)
        pixels[..., 0], pixels[..., 1], pixels[..., 2] = 200, 100, 7
        self.assertEqual(ff.decode_rgb(dds_rgb24(pixels))[0].tolist(), [200, 100, 7])
        refused = ff.family_job(dict(name='x', textures=[dict(sha256='0', data=b'not a dds' * 20, weight=1.0)]))
        self.assertEqual(refused['refusal'], 'texture_format_unsupported')
        black = ff.family_job(dict(name='x', textures=[dict(sha256='0', data=dds_rgb24(np.zeros((4, 4, 3), np.uint8)), weight=1.0)]))
        self.assertEqual(black['refusal'], 'palette_degenerate')

    def test_uniform_weights_are_numpy_linear_inclusive_bands(self):
        rng = np.random.default_rng(3)
        codes = rng.integers(1, 256, size=(4096, 3))
        linear = ff.SRGB_TO_LINEAR[codes]
        y = linear[:, 0] * 0.2126 + linear[:, 1] * 0.7152 + linear[:, 2] * 0.0722
        stops, edges, uniform = ff.palette_stops([(linear, y, 1 / 4096, 4096, 4096)])
        self.assertTrue(uniform)
        np.testing.assert_array_equal(edges, np.percentile(y, ff.BANDS))
        band = (y >= edges[3]) & (y <= edges[4])
        mean = linear[band].mean(0)
        self.assertEqual(stops[3], [ff.round9(v) for v in mean / mean.max()])

    def test_rate_weighting_not_resolution(self):
        # A: 16 dim red texels at rate 3; B: 64 bright blue texels at rate 1. By pixel count B is
        # 80 % of the pool; by rate A carries 75 % of the weight, so bands 25-40..55-70 are red.
        red = np.tile(ff.SRGB_TO_LINEAR[[90, 0, 0]], (16, 1))
        blue = np.tile(ff.SRGB_TO_LINEAR[[0, 0, 250]], (64, 1))
        pools = [(red, red @ [0.2126, 0.7152, 0.0722], 3 / 16, 16, 16), (blue, blue @ [0.2126, 0.7152, 0.0722], 1 / 64, 64, 64)]
        stops, _, uniform = ff.palette_stops(pools)
        self.assertFalse(uniform)
        self.assertEqual(stops[0], [1.0, 0.0, 0.0])
        self.assertEqual(stops[2], [1.0, 0.0, 0.0])
        self.assertEqual(stops[3][2], 1.0)  # the 70-85 band reaches the blue texture
        # Equal pixel weights through the weighted estimator give numpy's linear edges.
        rng = np.random.default_rng(5)
        y = np.sort(rng.random(101)) + 0.01
        lin = np.stack([y, y, y], 1)
        split = [(lin[:50], y[:50], 0.5, 50, 50), (lin[50:], y[50:], 0.5 * (1 + 1e-13), 51, 51)]
        _, edges, uniform = ff.palette_stops(split)
        self.assertFalse(uniform)
        np.testing.assert_allclose(edges, np.percentile(y, ff.BANDS), rtol=0, atol=1e-11)

    def test_profile_id_and_names(self):
        self.assertEqual(ff.profile_id('litcube0'), ff.fnv1a32(b'litcube0') | 0x10000)
        self.assertGreaterEqual(ff.profile_id('a'), 0x10000)
        self.assertTrue(ff.valid_name('x' * 31))
        self.assertFalse(ff.valid_name('x' * 32) or ff.valid_name('') or ff.valid_name('bad\x07') or ff.valid_name('a"b') or ff.valid_name('a\\b'))


@unittest.skipUnless((ff.bob1.DEFAULT_GAME / '01.cat').exists(), 'installed game catalogues unavailable')
class StockPaletteRegression(unittest.TestCase):
    def test_twelve_provisional_palettes_bit_exact(self):
        assets = sfc.Assets(ff.bob1.DEFAULT_GAME)
        grouped, _ = ff.enumerate_families(assets)
        self.assertEqual(len(PROVISIONAL), 12)
        exact = []
        for name in PROVISIONAL:
            plan = ff.plan_family(assets, name, grouped[name], set(), {})
            self.assertEqual(plan['status'], 'covered_by_build', name)
            self.assertEqual({t['member'] for t in plan['textures']}, {recipe.PROFILES[name]['palette_texture']}, name)
            result = ff.family_job(dict(name=name, textures=ff.texture_payload(assets, plan)))
            self.assertTrue(result['uniform_weights'], name)
            if np.array_equal(np.array(result['colours'], np.float32), recipe.PROFILES[name]['colours']):
                exact.append(name)
        self.assertEqual(exact, PROVISIONAL)


class BakeAndFileTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.shared = baker.bake_fields()

    def test_bake_path_reproduces_the_pinned_bluewell_packet_and_chroma(self):
        ff._init_worker(self.shared)
        carrier, mask, eligible, t1, interpolation = self.shared
        p = recipe.PROFILES['bluewell']
        atlas = baker.atlas_from_volume(baker.colour_volume(baker.family_density(carrier, mask, eligible, t1, p['occupancy']),
                                                            interpolation, p['colours']))
        packet, row = baker.packetize(atlas, dict(id=1))
        self.assertEqual(hashlib.sha256(packet).hexdigest(), 'fea1a4bf3842b417007af27c6cc25092c3ac8e815660b02bc3ac410953f1f372')
        tracked = (ROOT / 'src/renderer/fog_family_chroma_inc.h').read_text().splitlines()
        line = next(l for l in tracked if l.startswith('{1u,'))
        values = [np.float32(v.rstrip('f')) for v in line.split('{')[2].split('}')[0].split(', ')]
        self.assertEqual(ff.atlas_chroma(atlas), [float(v) for v in values])

    def test_synthetic_family_bake_dedup_and_file(self):
        ff._init_worker(self.shared)
        colours = [[1.0, 0.5, 0.25], [1.0, 0.6, 0.3], [0.9, 1.0, 0.4], [0.2, 0.4, 1.0]]
        texture = dds_rgb24(np.full((8, 8, 3), 180, np.uint8))
        a = ff.family_job(dict(name='zza', textures=[dict(sha256='t', data=texture, weight=1.0)], bake=True, occupancy=.12,
                               profile_id=ff.profile_id('zza')))
        self.assertNotIn('refusal', a)
        self.assertEqual(a['colours'], [[1.0, 1.0, 1.0]] * 4)
        self.assertEqual(struct.unpack_from('<I', a['packet'], 16)[0], ff.profile_id('zza'))
        self.assertTrue(all(0 <= c <= 1 for c in a['chroma']))
        rows = [dict(name='zza', profile_id=ff.profile_id('zza'), packet=0, base_sigma=2.5e-6, occupancy=.12, chroma=a['chroma'],
                     colours=a['colours'], flags=0),
                dict(name='zzb', profile_id=ff.profile_id('zzb'), packet=0, base_sigma=2.5e-6, occupancy=.12, chroma=a['chroma'],
                     colours=colours, flags=ff.FLAG_OVERRIDE)]
        packets = [dict(bytes=a['packet'], decoded_fnv1a=int(a['packet_row']['decoded_fnv1a'], 16), profile_id=ff.profile_id('zza'),
                        decoded_sha256=a['packet_row']['decoded_sha256'])]
        data = ff.build_file(rows, packets)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / ff.FILE_NAME
            path.write_bytes(data)
            loaded = ff.read_file(path)
            self.assertEqual((loaded['status'], [r['disabled'] for r in loaded['rows']]), ('loaded', [None, None]))
            self.assertEqual([r['profile_id'] for r in loaded['rows']], [ff.profile_id('zza'), ff.profile_id('zzb')])
            self.assertEqual(len(data), 64 + 2 * 112 + 80 + len(a['packet']))
            bad = bytearray(data)
            struct.pack_into('<f', bad, 64 + 40, 1e-3)
            struct.pack_into('<Q', bad, 48, ff.fnv1a64(bytes(bad[64:64 + 2 * 112 + 80])))
            path.write_bytes(bytes(bad))
            self.assertEqual([r['disabled'] for r in ff.read_file(path)['rows']], ['sigma', None])
            # Run data and the decoded checksum are the decoder's, at the family switch (C++ fixture).
            path.write_bytes(data[:-1] + bytes([data[-1] ^ 1]))
            self.assertEqual(ff.read_file(path)['reason'], 'ok')
            path.write_bytes(data + b'\0')
            self.assertEqual(ff.read_file(path)['reason'], 'file_size')


class EndToEndTests(unittest.TestCase):
    """A synthetic game root: TBackgrounds, text bodies and DDS members in one catalogue."""

    def make_game(self, root, tex_a_value=180, family='zzmod'):
        rows = [background_line(family, [3, 1, 0, 0, 0, 0, 0, 0], 5, 1),
                background_line('zzmissing', [1, 0, 0, 0, 0, 0, 0, 0], 3, 2),
                background_line('zznobody', [1, 0, 0, 0, 0, 0, 0, 0], 2, 3),
                background_line('zzclear', [1, 0, 0, 0, 0, 0, 0, 0], 0, 4),
                background_line('this_family_name_is_longer_than_31', [1, 0, 0, 0, 0, 0, 0, 0], 1, 5)]
        table = ('25;%d;\n' % len(rows) + '\n'.join(rows) + '\n').encode()
        nebula = 'objects/environments/nebulae'
        grey = np.zeros((8, 8, 3), np.uint8)
        grey[:4] = [tex_a_value, 40, 20]
        grey[4:] = [30, 60, tex_a_value]
        members = [('types/TBackgrounds.pck', table),
                   (f'{nebula}/{family}/nebula_{family}_dust_part01.pbd', body([rf'environments\nebulae\{family}\tex_a.tga'])),
                   (f'{nebula}/{family}/nebula_{family}_dust_part02.pbd', body([rf'environments\nebulae\{family}\tex_b.tga'])),
                   (f'{nebula}/zzmissing/nebula_zzmissing_dust_part01.pbd', body([r'environments\nebulae\zzmissing\gone.tga'])),
                   ('dds/tex_a.pck', dds_rgb24(grey)),
                   ('dds/tex_b.pck', dds_rgb24(np.full((16, 16, 3), [20, 200, 90], np.uint8)))]
        sfc.write_catalogue(root / '01.cat', members)

    def run_tool(self, *argv):
        stdout = io.StringIO()
        with contextlib.redirect_stdout(stdout):
            code = ff.main([str(a) for a in argv])
        return code, stdout.getvalue()

    def test_generate_check_and_install(self):
        with tempfile.TemporaryDirectory() as tmp:
            game, out = Path(tmp) / 'game', Path(tmp) / 'out'
            self.make_game(game)
            code, text = self.run_tool('--game', game, '--dry-run', '--jobs', '2')
            self.assertEqual(code, 0)
            summary = json.loads(text.strip().splitlines()[-1])
            self.assertEqual(summary['counts'], {'ok': 1, 'refused:name_invalid': 1, 'refused:no_dust_bodies': 1,
                                                 'refused:texture_missing': 1})
            self.assertIsNone(summary['file_bytes'])
            with self.assertRaises(SystemExit):
                self.run_tool('--game', game, '--out', game / 'inside')
            code, text = self.run_tool('--game', game, '--out', out, '--jobs', '1')
            self.assertEqual(code, 0)
            record = json.loads((out / 'x3m' / ff.RECORD_NAME).read_text())
            loaded = ff.read_file(out / 'x3m' / ff.FILE_NAME)
            self.assertEqual([(r['name'], r['profile_id'], r['disabled']) for r in loaded['rows']], [('zzmod', ff.profile_id('zzmod'), None)])
            family = next(f for f in record['families'] if f['name'] == 'zzmod')
            self.assertEqual(family['rates'][:2], [3, 1])
            self.assertEqual(sorted(t['weight'] for t in family['textures']), [1.0, 3.0])
            self.assertFalse(family['uniform_weights'])
            self.assertEqual(record['file']['bytes'], (out / 'x3m' / ff.FILE_NAME).stat().st_size)
            self.assertEqual(self.run_tool('--game', game, '--check', '--out', out)[0], 0)
            self.make_game(game, tex_a_value=120)  # a texture changes under the installed file
            code, text = self.run_tool('--game', game, '--check', '--out', out)
            self.assertEqual(code, 1)
            self.assertIn('texture inputs changed', text)

            # Install: refused while the game runs, refused over an existing file, --replace keeps one .previous.
            original = ff.running_game
            try:
                ff.running_game = lambda: ['123 C:\\X3\\X3AP.exe']
                with self.assertRaises(SystemExit) as caught:
                    self.run_tool('--game', game, '--install')
                self.assertIn('game is running', str(caught.exception))
                ff.running_game = lambda: []
                (game / 'x3m').mkdir()
                (game / 'x3m' / ff.FILE_NAME).write_bytes(b'old')
                with self.assertRaises(SystemExit) as caught:
                    self.run_tool('--game', game, '--install')
                self.assertIn('--replace', str(caught.exception))
                code, _ = self.run_tool('--game', game, '--install', '--replace', '--jobs', '1')
                self.assertEqual(code, 0)
                self.assertEqual((game / 'x3m' / (ff.FILE_NAME + '.previous')).read_bytes(), b'old')
                self.assertEqual(ff.read_file(game / 'x3m' / ff.FILE_NAME)['status'], 'loaded')
                self.assertEqual(self.run_tool('--game', game, '--check')[0], 0)
                installed = {n: (game / 'x3m' / n).read_bytes() for n in (ff.FILE_NAME, ff.RECORD_NAME)}
                # The game starts during the bake: the second check refuses before anything moves.
                calls = []
                ff.running_game = lambda: calls.append(1) or (['123 X3AP.exe'] if len(calls) > 1 else [])
                with self.assertRaises(SystemExit) as caught:
                    self.run_tool('--game', game, '--install', '--replace', '--jobs', '1')
                self.assertIn('game is running', str(caught.exception))
                self.assertEqual(len(calls), 2)
                self.assertEqual({n: (game / 'x3m' / n).read_bytes() for n in installed}, installed)
                # A failed post-write validation restores the moved pair.
                ff.running_game = lambda: []
                with mock.patch.object(ff, 'read_file', return_value=dict(status='rejected', reason='simulated', rows=[])):
                    with self.assertRaises(SystemExit) as caught:
                        self.run_tool('--game', game, '--install', '--replace', '--jobs', '1')
                self.assertIn('the previous installation was restored', str(caught.exception))
                self.assertEqual({n: (game / 'x3m' / n).read_bytes() for n in installed}, installed)
                self.assertFalse(any(p.name.endswith('.tmp') for p in (game / 'x3m').iterdir()))
            finally:
                ff.running_game = original


def load_manage():
    spec = importlib.util.spec_from_file_location('fog_families_manage', ROOT / 'tools/manage.py')
    manage = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(manage)
    return manage


class LauncherReportTests(unittest.TestCase):
    """tools/manage.py launch --dry-run: one 'fog families:' line, missing / stale / ok (fog-family-data.md, "Mod flow")."""

    def dry_run(self, directory, environ, vanilla=False):
        manage = load_manage()
        game = Path(directory) / 'game'
        game.mkdir(exist_ok=True)
        (game / 'X3AP.exe').touch()
        (game / 'd3d9.dll').write_bytes(b'proxy')  # an owned install, so the modded dry run passes its install check
        (game / 'x3-modern-install.json').write_text(json.dumps({'sha256': hashlib.sha256(b'proxy').hexdigest()}))
        wine = Path(directory) / 'wine'
        wine.touch()
        argv = ['manage.py', 'launch', '--dry-run', *(['--vanilla'] if vanilla else []), '--bottle', 'X3', '--game-dir', str(game)]
        output, error = io.StringIO(), io.StringIO()
        with mock.patch.dict(os.environ, environ), mock.patch.object(sys, 'argv', argv), mock.patch.object(manage, 'WINE', wine), \
                mock.patch.object(manage, 'VOICE_DECODER_REPO', Path(directory) / 'no-decoder'), \
                mock.patch.object(manage.subprocess, 'call', side_effect=AssertionError('must never launch')), \
                contextlib.redirect_stdout(output), contextlib.redirect_stderr(error):
            manage.main()
        line = json.loads(output.getvalue())['fog_families']
        if vanilla:
            self.assertIsNone(line)
            self.assertNotIn('fog families', error.getvalue())
        else:
            self.assertEqual([l for l in error.getvalue().splitlines() if l.startswith('fog families')], [line])
        return game, line

    def run_tool(self, *argv):
        with contextlib.redirect_stdout(io.StringIO()) as stdout:
            code = ff.main([str(a) for a in argv])
        return code, stdout.getvalue()

    def test_launch_line_missing_stale_ok(self):
        with tempfile.TemporaryDirectory() as directory, mock.patch.dict(os.environ, {}), \
                mock.patch.object(ff, 'running_game', return_value=[]):
            os.environ.pop('X3M_FOG_FAMILIES', None)
            game = Path(directory) / 'game'
            EndToEndTests.make_game(None, game)
            hint = '`python3 tools/manage.py fog-families --bottle X3 --install`'
            self.assertEqual(self.dry_run(directory, {})[1],
                             f'fog families: missing; run {hint} to cover mod sectors, compiled 14 names only')
            self.dry_run(directory, {}, vanilla=True)
            self.assertEqual(self.run_tool('--game', game, '--install', '--jobs', '1')[0], 0)
            record = json.loads((game / 'x3m' / ff.RECORD_NAME).read_text())
            self.assertEqual(record['launch_inputs']['layers'], ['01.cat'])
            self.assertEqual(self.dry_run(directory, {})[1], 'fog families: ok (1 families, 1 packets)')
            self.dry_run(directory, {}, vanilla=True)  # present file: still nothing under --vanilla
            # A mod adds a catalogue: stale, named; removing it restores ok.
            (game / 'addon').mkdir()
            sfc.write_catalogue(game / 'addon' / '05.cat', [('types/Dummy.txt', b'x')])
            line = self.dry_run(directory, {})[1]
            self.assertTrue(line.startswith('fog families: stale (catalogue list changed (+addon/05.cat); 1 families, 1 packets still load)'), line)
            self.assertIn(f'{hint[:-1]} --replace`', line)
            for suffix in ('.cat', '.dat'):
                (game / 'addon' / ('05' + suffix)).unlink()
            self.assertTrue(self.dry_run(directory, {})[1].startswith('fog families: ok'))
            # A loose TBackgrounds appears: stale.
            (game / 'types').mkdir()
            (game / 'types' / 'TBackgrounds.txt').write_text('25;0;\n')
            self.assertIn('stale (types/TBackgrounds.txt changed', self.dry_run(directory, {})[1])
            (game / 'types' / 'TBackgrounds.txt').unlink()
            # A touched catalogue with unchanged content: stale until --check PASSes and refreshes the fingerprint.
            dat = game / '01.dat'
            os.utime(dat, ns=(dat.stat().st_atime_ns, dat.stat().st_mtime_ns + 10**9))
            self.assertIn('stale (01.dat changed (size or mtime)', self.dry_run(directory, {})[1])
            code, text = self.run_tool('--game', game, '--check')
            self.assertEqual(code, 0)
            self.assertIn('launch fingerprint refreshed (01.dat changed', text)
            self.assertEqual(self.dry_run(directory, {})[1], 'fog families: ok (1 families, 1 packets)')
            # The record goes missing or no longer describes the file.
            record_path = game / 'x3m' / ff.RECORD_NAME
            saved = record_path.read_bytes()
            record_path.unlink()
            self.assertIn('stale (no fog-families.json beside it', self.dry_run(directory, {})[1])
            record_path.write_bytes(saved.replace(b'"bytes": ', b'"bytes": 1'))
            self.assertIn('stale (file differs from fog-families.json', self.dry_run(directory, {})[1])
            record_path.write_bytes(saved)
            # Header damage and the DLL's own opt-out.
            data = (game / 'x3m' / ff.FILE_NAME).read_bytes()
            (game / 'x3m' / ff.FILE_NAME).write_bytes(data + b'\0')
            self.assertIn('header invalid (the proxy will reject it)', self.dry_run(directory, {})[1])
            (game / 'x3m' / ff.FILE_NAME).write_bytes(data[:10])
            self.assertIn('header truncated', self.dry_run(directory, {})[1])
            self.assertEqual(self.dry_run(directory, {'X3M_FOG_FAMILIES': 'none'})[1], 'fog families: disabled (X3M_FOG_FAMILIES)')

    def test_zero_packet_install(self):
        # Every family is compiled in or refused (vanilla's shape): --install still writes the empty table.
        with tempfile.TemporaryDirectory() as directory, mock.patch.dict(os.environ, {}), \
                mock.patch.object(ff, 'running_game', return_value=[]):
            os.environ.pop('X3M_FOG_FAMILIES', None)
            game = Path(directory) / 'game'
            EndToEndTests.make_game(None, game, family=sorted(recipe.PROFILES)[0])
            code, text = self.run_tool('--game', game, '--install', '--jobs', '1')
            self.assertEqual(code, 0)
            summary = json.loads(text.strip().splitlines()[-2])
            self.assertEqual(summary['counts'], {'covered_by_build': 1, 'refused:name_invalid': 1, 'refused:no_dust_bodies': 1,
                                                 'refused:texture_missing': 1})
            self.assertEqual((summary['file_bytes'], summary['packets']), (64, 0))
            self.assertIn('families=0 packets=0 (empty table: nothing to add; 1 compiled cover 1 families, 3 refused)',
                          text.strip().splitlines()[-1])
            data = (game / 'x3m' / ff.FILE_NAME).read_bytes()
            self.assertEqual((len(data), data[:8], struct.unpack_from('<5I', data, 8), struct.unpack_from('<Q', data, 56)[0]),
                             (64, b'X3FOGFAM', (1, 64, recipe.RECIPE_ID, 0, 0), 64))
            loaded = ff.read_file(game / 'x3m' / ff.FILE_NAME)
            self.assertEqual((loaded['status'], loaded['reason'], loaded['rows'], loaded['packets']), ('loaded', 'ok', [], []))
            record = json.loads((game / 'x3m' / ff.RECORD_NAME).read_text())
            self.assertEqual((record['file']['bytes'], record['launch_inputs']['layers']), (64, ['01.cat']))
            self.assertEqual(self.dry_run(directory, {})[1], 'fog families: ok (0 packets; 1 compiled cover 1 families, 3 refused)')
            (game / 'addon').mkdir()
            sfc.write_catalogue(game / 'addon' / '05.cat', [('types/Dummy.txt', b'x')])
            self.assertTrue(self.dry_run(directory, {})[1].startswith(
                'fog families: stale (catalogue list changed (+addon/05.cat); 0 families, 0 packets still load)'))
            for suffix in ('.cat', '.dat'):
                (game / 'addon' / ('05' + suffix)).unlink()
            code, text = self.run_tool('--game', game, '--check')
            self.assertEqual(code, 0, text)
            self.assertIn('PASS', text)
            self.assertIn('families=0 packets=0 bytes=64', text)
            self.assertEqual(self.dry_run(directory, {})[1], 'fog families: ok (0 packets; 1 compiled cover 1 families, 3 refused)')
            record_path = game / 'x3m' / ff.RECORD_NAME
            malformed = json.loads(record_path.read_text())
            malformed['counts'] = ['not', 'a', 'mapping']
            record_path.write_text(json.dumps(malformed))
            self.assertIn(f'stale ({ff.RECORD_NAME} unreadable (AttributeError)', self.dry_run(directory, {})[1])


class WrapperTests(unittest.TestCase):
    """tools/manage.py fog-families forwards to fog_families.py with the bottle's game directory."""

    def test_argument_forwarding(self):
        manage = load_manage()
        tool = str(ROOT / 'tools/analysis/fog_families.py')
        bottle_game = str(Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3')
        self.assertEqual(manage.fog_families_command(['--bottle', 'X3', '--install', '--replace', '--jobs', '2']),
                         [sys.executable, tool, '--game', bottle_game, '--install', '--replace', '--jobs', '2'])
        self.assertEqual(manage.fog_families_command(['--bottle', 'X3']), [sys.executable, tool, '--game', bottle_game, '--check'])
        self.assertEqual(manage.fog_families_command(['--game-dir', '/g', '--out=/o', '--mod-cat', '/m/05.cat']),
                         [sys.executable, tool, '--game', '/g', '--out=/o', '--mod-cat', '/m/05.cat'])
        self.assertEqual(manage.fog_families_command(['--bottle', 'Other', '--dry-run'])[3],
                         str(Path.home() / 'Library/Application Support/CrossOver/Bottles/Other/drive_c/X3'))
        argv = ['manage.py', 'fog-families', '--bottle', 'X3', '--install']
        with mock.patch.object(sys, 'argv', argv), mock.patch.object(manage.subprocess, 'call', return_value=3) as call:
            with self.assertRaises(SystemExit) as caught:
                manage.main()
        self.assertEqual(caught.exception.code, 3)
        call.assert_called_once_with([sys.executable, tool, '--game', bottle_game, '--install'])


if __name__ == '__main__':
    unittest.main()
