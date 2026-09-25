"""Host checks of the effect key tool (tools/effects/effect_keys.py,
docs/architecture/effects-modernisation-opus.md 2.1 and 8.2).

On copied synthetic roots (never symlinks): the key of a DXT5 DDS member of a
numbered catalogue; determinism (two runs, one text; --check agrees); the
engine's rank (a .pck member beats a .dds member of the same stem, a loose
file under the game folder beats every catalogue); mod precedence (a mod
package mounted above the catalogues replaces the key, the last mod given
wins, and a loose file still beats the mod); an unmodelled 24-bit RGB file is
listed with no key and a reason; the checked-in table's keys reproduce from
the DDS bytes the tool recorded. No Wine, no game files.
"""
import json
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / 'tools/effects/effect_keys.py'
TABLE = ROOT / 'tools/effects/effect_keys.json'
sys.path.insert(0, str(ROOT / 'tools/effects'))
import effect_keys  # noqa: E402


def dds(width, height, fourcc=b'DXT5', seed=1, bits=None):
    """A minimal DDS: DXT5 blocks (or an RGB 24-bit image with bits=24) from a deterministic pattern."""
    header = bytearray(128)
    header[:4] = b'DDS '
    struct.pack_into('<IIIII', header, 4, 124, 0x1 | 0x2 | 0x4 | 0x1000, height, width, 0)
    struct.pack_into('<I', header, 28, 1)
    if bits is None:
        struct.pack_into('<II4sI', header, 76, 32, 0x4, fourcc, 0)
        size = max(1, (width + 3) // 4) * max(1, (height + 3) // 4) * (8 if fourcc == b'DXT1' else 16)
    else:
        struct.pack_into('<II4sIIIII', header, 76, 32, 0x40, b'\0\0\0\0', bits, 0xff0000, 0xff00, 0xff, 0)
        size = width * height * bits // 8
    struct.pack_into('<I', header, 108, 0x1000)
    body = bytes((i * 7919 + seed * 131) & 255 for i in range(size))
    return bytes(header) + body


def write_catalogue(cat, members):
    """A CAT/DAT pair: the obfuscated directory and the XOR-0x33 payloads (inspect_x3.read_catalogue's format)."""
    lines = [cat.with_suffix('.dat').name] + [f'{path} {len(data)}' for path, data in members]
    text = ('\n'.join(lines) + '\n').encode('utf-8')
    cat.write_bytes(bytes(v ^ ((0xdb + i) & 255) for i, v in enumerate(text)))
    cat.with_suffix('.dat').write_bytes(b''.join(bytes(v ^ 0x33 for v in data) for _, data in members))


def run(*args):
    return subprocess.run([sys.executable, str(TOOL), *map(str, args)], capture_output=True, text=True)


class Tool(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.game = Path(self.directory.name) / 'game'
        self.game.mkdir()
        self.image = dds(64, 32)
        write_catalogue(self.game / '01.cat', [('dds\\synth_diff.dds', self.image), ('dds\\rgb_diff.dds', dds(8, 8, bits=24))])

    def tearDown(self):
        self.directory.cleanup()

    def table(self, *extra):
        out = Path(self.directory.name) / 'keys.json'
        result = run('--game', self.game, '--no-default-bodies', '--texture', 'synth_diff=shield_hit', '--texture', 'rgb_diff=shield_hit', '--output', out, *extra)
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(out.read_text()), out

    def entry(self, table, name):
        return next(e for e in table['entries'] if e.get('name') == name)

    def expected(self, image):
        width, height, fmt, _, level0 = effect_keys.dds_level0(image)[0]
        rows, row_bytes = effect_keys.level0_layout(width, height, fmt)
        return '%016x' % effect_keys.sparse_key(width, height, fmt, rows, row_bytes, level0)

    def test_key_determinism_and_rank(self):
        table, out = self.table()
        synth = self.entry(table, 'synth_diff')
        self.assertEqual((synth['key'], synth['width'], synth['height'], synth['format'], synth['class']), (self.expected(self.image), 64, 32, 'DXT5', 'shield_hit'))
        self.assertEqual(synth['texture_source'], '01.cat:dds\\synth_diff.dds')
        self.assertEqual(synth['extent'], [0.0, 0.0, 0.0])
        rgb = self.entry(table, 'rgb_diff')
        self.assertIsNone(rgb['key'])
        self.assertEqual(rgb['reason'], 'rgb24_unmodelled')
        first = out.read_text()
        self.table()
        self.assertEqual(out.read_text(), first)
        check = run('--game', self.game, '--no-default-bodies', '--texture', 'synth_diff=shield_hit', '--texture', 'rgb_diff=shield_hit', '--output', out, '--check')
        self.assertEqual(check.returncode, 0, check.stderr)
        # A .pck member of the same stem outranks the .dds member; a higher catalogue outranks a lower one.
        packed = dds(64, 32, seed=5)
        write_catalogue(self.game / '02.cat', [('dds\\synth_diff.pck', packed)])
        table, _ = self.table()
        self.assertEqual(self.entry(table, 'synth_diff')['key'], self.expected(packed))
        self.assertEqual(self.entry(table, 'synth_diff')['texture_source'], '02.cat:dds\\synth_diff.pck')
        # A loose file under the game folder beats every catalogue.
        loose = dds(64, 32, seed=9)
        (self.game / 'dds').mkdir()
        (self.game / 'dds/synth_diff.pck').write_bytes(loose)
        table, _ = self.table()
        self.assertEqual(self.entry(table, 'synth_diff')['key'], self.expected(loose))
        self.assertTrue(self.entry(table, 'synth_diff')['texture_source'].startswith('loose:'))

    def test_mod_precedence_on_copied_roots(self):
        mods = Path(self.directory.name) / 'mods'
        mods.mkdir()
        first, second = dds(64, 32, seed=21), dds(64, 32, seed=22)
        write_catalogue(mods / 'alpha.cat', [('dds\\synth_diff.pck', first)])
        write_catalogue(mods / 'beta.cat', [('dds\\synth_diff.dds', second)])
        table, _ = self.table('--mod', mods / 'alpha.cat')
        self.assertEqual(self.entry(table, 'synth_diff')['key'], self.expected(first))
        self.assertEqual(self.entry(table, 'synth_diff')['texture_source'], 'mod:alpha.cat:dds\\synth_diff.pck')
        table, _ = self.table('--mod', mods / 'alpha.cat', '--mod', mods / 'beta.cat')
        self.assertEqual(self.entry(table, 'synth_diff')['key'], self.expected(first))  # the .pck rank of alpha beats beta's .dds
        write_catalogue(mods / 'beta.cat', [('dds\\synth_diff.pck', second)])
        table, _ = self.table('--mod', mods / 'alpha.cat', '--mod', mods / 'beta.cat')
        self.assertEqual(self.entry(table, 'synth_diff')['key'], self.expected(second))  # the last mod wins at equal rank
        self.assertEqual(table['mods'], [str(mods / 'alpha.cat'), str(mods / 'beta.cat')])
        loose = dds(64, 32, seed=30)
        (self.game / 'dds').mkdir()
        (self.game / 'dds/synth_diff.pck').write_bytes(loose)
        table, _ = self.table('--mod', mods / 'beta.cat')
        self.assertEqual(self.entry(table, 'synth_diff')['key'], self.expected(loose))  # a loose file still wins
        link = Path(self.directory.name) / 'link.cat'
        link.symlink_to(mods / 'beta.cat')
        result = run('--game', self.game, '--no-default-bodies', '--texture', 'synth_diff=shield_hit', '--output', Path(self.directory.name) / 'x.json', '--mod', link)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('symlink', result.stderr)

    def test_checked_in_table_is_consistent(self):
        table = json.loads(TABLE.read_text())
        self.assertEqual((table['schema'], table['hash']), (1, 'sparse16x256-fnv1a64'))
        keyed = [e for e in table['entries'] if e.get('key')]
        self.assertGreaterEqual(len(keyed), 2)
        self.assertEqual({e['class'] for e in keyed} - {'shield_hit', 'bolt'}, set())
        for e in keyed:
            self.assertEqual(len(e['key']), 16)
            self.assertEqual(len(e['texture_sha256']), 64)
            self.assertTrue(all(0.0 <= v <= 1.0 for v in e['tint']))
            self.assertEqual(len(e['extent']), 3)


if __name__ == '__main__':
    unittest.main()
