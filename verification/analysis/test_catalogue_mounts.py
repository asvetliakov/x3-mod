"""Mount order of the numbered archives (tools/analysis/catalogue_mounts.py).

Miniature synthetic installations; no game data. Covers the edition rule
(X3AP.exe: root then addon; X3FL.exe: root then addon2, no addon), the
first-gap end of each numbered series, and that the archive scans built on it
(`effect_passes._archive_effects`, `index_shaders.index`,
`shader_coverage_offline.archive_pairs`' override count) resolve a virtual
path to the last mounted catalogue.
"""
import argparse
import contextlib
import io
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'analysis'))
from catalogue_mounts import add_edition_argument, edition, mounted_catalogues  # noqa: E402
from effect_passes import _archive_effects, archive_passes  # noqa: E402
from index_shaders import index  # noqa: E402
from shader_coverage_offline import archive_pairs  # noqa: E402

def effect(tag):
    """An effect container with no parameters or techniques; `tag` (padded to
    16 bytes) sits before the parameter section and names the catalogue."""
    tag = tag.encode().ljust(16, b'\0')
    return struct.pack('<II', 0xfeff0901, len(tag)) + tag + struct.pack('<6I', 0, 0, 0, 0, 0, 0)


def archive(root, name, entries):
    """Write `name` (e.g. 'addon2/01.cat') holding {virtual path: payload}."""
    cat = root / name
    cat.parent.mkdir(parents=True, exist_ok=True)
    text = cat.with_suffix('.dat').name + '\n' + ''.join(
        '%s %d\n' % (path, len(data)) for path, data in entries.items())
    raw = text.encode()
    cat.write_bytes(bytes(byte ^ ((0xdb + i) & 255) for i, byte in enumerate(raw)))
    cat.with_suffix('.dat').write_bytes(bytes(byte ^ 0x33 for data in entries.values() for byte in data))


def names(game, which=None):
    return [str(cat.relative_to(game)) for cat in mounted_catalogues(game, which)]


class CatalogueMountTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.game = Path(self.folder.name)

    def tearDown(self):
        self.folder.cleanup()

    def install(self, fl):
        for name in ('01.cat', '02.cat', '04.cat', 'addon/01.cat', 'addon/02.cat',
                     'addon2/01.cat', 'addon2/03.cat'):
            archive(self.game, name, {'shader/3_0/a.fb': effect(name)})
        if fl:
            (self.game / 'X3FL.exe').write_bytes(b'MZ')

    def test_ap_mounts_root_then_addon_and_stops_at_the_first_gap(self):
        self.install(fl=False)
        self.assertEqual(edition(self.game), 'ap')
        self.assertEqual(names(self.game), ['01.cat', '02.cat', 'addon/01.cat', 'addon/02.cat'])

    def test_fl_mounts_root_then_addon2_without_addon(self):
        self.install(fl=True)
        self.assertEqual(edition(self.game), 'fl')
        self.assertEqual(names(self.game), ['01.cat', '02.cat', 'addon2/01.cat'])
        self.assertEqual(names(self.game, 'ap'), ['01.cat', '02.cat', 'addon/01.cat', 'addon/02.cat'])
        with self.assertRaises(ValueError):
            mounted_catalogues(self.game, 'tc')

    def test_fl_needs_both_the_executable_and_addon2_01(self):
        archive(self.game, '01.cat', {})
        archive(self.game, 'addon2/02.cat', {})
        (self.game / 'X3FL.exe').write_bytes(b'MZ')
        self.assertEqual(edition(self.game), 'ap')
        archive(self.game, 'addon2/01.cat', {})
        self.assertEqual(edition(self.game), 'fl')
        (self.game / 'X3FL.exe').unlink()
        self.assertEqual(edition(self.game), 'ap')

    def test_game_edition_explicit_then_environment_then_installation(self):
        import os
        from unittest import mock
        from catalogue_mounts import EDITION_ENV, expansion, game_edition
        self.install(fl=True)
        with mock.patch.dict(os.environ, {EDITION_ENV: ''}):
            self.assertEqual((game_edition(self.game), expansion(self.game)), ('fl', 'addon2'))
            self.assertEqual(game_edition(self.game, 'ap'), 'ap')
        with mock.patch.dict(os.environ, {EDITION_ENV: 'ap'}):
            self.assertEqual((game_edition(self.game), game_edition(self.game, 'auto')), ('ap', 'ap'))
            self.assertEqual(expansion(self.game, 'fl'), 'addon2')
        with mock.patch.dict(os.environ, {EDITION_ENV: 'tc'}), self.assertRaises(ValueError):
            game_edition(self.game)

    def test_last_mounted_catalogue_wins_in_every_scan(self):
        self.install(fl=True)
        resolved = list(_archive_effects(self.game))
        self.assertEqual([(cat, path, data[8:21]) for cat, path, data in resolved],
                         [('addon2/01.cat', 'shader/3_0/a.fb', b'addon2/01.cat')])
        everything = [cat for cat, _, _ in _archive_effects(self.game, resolve=False)]
        self.assertEqual(everything, ['01.cat', '02.cat', 'addon2/01.cat'])
        self.assertEqual([effect['catalogue'] for effect in index(self.game)['effects']],
                         ['01.cat', '02.cat', 'addon2/01.cat'])
        # Root 01, 02 and addon2/01 hold the path: two hidden entries.
        self.assertEqual(archive_pairs(self.game)['overridden_entry_count'], 2)
        (self.game / 'X3FL.exe').unlink()
        self.assertEqual([cat for cat, _, _ in _archive_effects(self.game)], ['addon/02.cat'])

    def test_explicit_edition_overrides_detection_in_every_scan(self):
        self.install(fl=True)
        ap, fl = ['01.cat', '02.cat', 'addon/01.cat', 'addon/02.cat'], ['01.cat', '02.cat', 'addon2/01.cat']
        self.assertEqual(names(self.game, 'auto'), fl)
        self.assertEqual([cat for cat, _, _ in _archive_effects(self.game, edition='ap')], ['addon/02.cat'])
        self.assertEqual([cat for cat, _, _ in _archive_effects(self.game, resolve=False, edition='ap')], ap)
        self.assertEqual([effect['catalogue'] for effect in index(self.game, 'ap')['effects']], ap)
        self.assertEqual(archive_pairs(self.game, 'ap')['overridden_entry_count'], 3)
        self.assertEqual(archive_passes(self.game, 'ap')['effect_count'], 4)
        (self.game / 'X3FL.exe').unlink()
        self.assertEqual([effect['catalogue'] for effect in index(self.game, 'fl')['effects']], fl)
        parser = argparse.ArgumentParser()
        add_edition_argument(parser)
        self.assertEqual(parser.parse_args([]).edition, 'auto')
        self.assertEqual(parser.parse_args(['--edition', 'fl']).edition, 'fl')
        with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()):
            parser.parse_args(['--edition', 'tc'])


if __name__ == '__main__':
    unittest.main()
