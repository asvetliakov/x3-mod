"""Launch-time merged-LOD overlay check (tools/analysis/lod_overlay_check.py, `lod_overlay.py --check`, the
`lod overlay:` line of tools/manage.py launch; docs/architecture/lod-overlay-mods.md section 2) on synthetic
game trees: none / ok / orphaned slot / catalogue above the overlay / named and digest-only source changes,
the derived package states, the cost-free path (no .dat opened) and the user.reg ModName parse."""
import builtins
import contextlib
import hashlib
import importlib.util
import io
import json
import os
import shutil
import sys
import tempfile
import time
import unittest
import unittest.mock
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'analysis'))
import bob1
import lod_overlay
import lod_overlay_check as check
import test_lod_overlay_batch as tb
from sector_fog_census import write_catalogue

ROOT = Path(__file__).resolve().parents[2]


def user_reg(bottle, mod_name=None, extra=''):
    """A Wine user.reg with the X3AP key (ModName omitted when mod_name is None)."""
    value = '' if mod_name is None else f'"ModName"="{mod_name}"\n'
    (bottle / 'user.reg').write_text(
        'WINE REGISTRY Version 2\n;; All keys relative to REGISTRY\\\\User\\\\S-1-5-21-0-0-0-1000\n\n'
        '[Software\\\\EGOSOFT\\\\X3TC] 1790307522\n"ModName"="other"\n\n'
        '[Software\\\\EGOSOFT\\\\X3AP] 1790307522\n#time=1dd4c9f5c90d1bc\n"AudioFlags"=hex:03,01,00,00\n'
        + value + extra + '\n[Software\\\\EGOSOFT\\\\X3AP\\\\Values] 1789388691\n"ModName"="nested"\n')


class Installed:
    """One synthetic bottle with the batch overlay installed in addon/02 (built once, copied per test)."""
    template = None

    @classmethod
    def build(cls):
        if cls.template is None:
            cls._dir = tempfile.TemporaryDirectory()
            base = Path(cls._dir.name) / 'bottle'
            game = tb.make_game(base / 'drive_c')
            game.rename(base / 'drive_c' / 'X3')
            game = base / 'drive_c' / 'X3'
            with unittest.mock.patch.object(lod_overlay, 'running_game', return_value=[]):
                code, _ = tb.run(tb.BATCH + ['--game', str(game), '--install', '--mod', 'none'])
            assert code == 0
            user_reg(base, '')
            cls.template = base
        return cls.template

    @classmethod
    def copy(cls, folder):
        base = Path(folder) / 'bottle'
        shutil.copytree(cls.build(), base, copy_function=shutil.copy2)   # keep mtimes: the fingerprints hold
        return base, base / 'drive_c' / 'X3'


def line(game, **kw):
    return check.overlay_line(game, **kw)


class States(unittest.TestCase):
    def test_none_and_ok(self):
        with tempfile.TemporaryDirectory() as folder:
            base = Path(folder) / 'bottle'
            game = tb.make_game(base / 'drive_c')
            user_reg(base, '')
            self.assertEqual(line(game), 'lod overlay: none (no overlay installed; no package: ModName empty)')
            base, game = Installed.copy(Path(folder) / 'b')
            marker = json.loads((game / 'addon/02.x3m-lod.json').read_text())
            self.assertEqual(len(marker['originals_fingerprints']), 6)     # 01, 02, addon/01 cat + dat
            self.assertEqual(marker['overlay_dat_bytes'], (game / 'addon/02.dat').stat().st_size)
            size = check.gb((game / 'addon/02.dat').stat().st_size)
            self.assertEqual(line(game), f'lod overlay: ok (9 bodies in slots 02, {size}; sources unchanged;'
                                         ' no package: ModName empty)')
            (base / 'user.reg').unlink()
            self.assertIn('ModName unknown (no user.reg)', line(game))
            code, text = tb.run(['--check', '--game', str(game), '--registry', str(Path(folder) / 'none.reg')])
            self.assertEqual(code, 0)
            self.assertIn('ModName unknown', text)

    def test_orphaned_above_and_sources(self):
        with tempfile.TemporaryDirectory() as folder:
            base, game = Installed.copy(folder)
            # a catalogue added above the overlay holding one overlay body
            write_catalogue(game / 'addon/03.cat', [('objects/ships/x/good.pbb', b'x'), ('types/T.txt', b'y')])
            text = line(game)
            self.assertTrue(text.startswith('lod overlay: stale (9 bodies in slots 02'), text)
            self.assertIn('addon/03.cat above the overlay holds 1 overlay bodies; sources unchanged', text)
            self.assertIn('rebake with `python3 tools/analysis/lod_overlay.py --batch --sync --install --game', text)
            for suffix in ('.cat', '.dat'):
                (game / 'addon' / ('03' + suffix)).unlink()
            # an original changed: named from originals_fingerprints
            dat = game / '02.dat'
            os.utime(dat, ns=(dat.stat().st_atime_ns, dat.stat().st_mtime_ns + 10 ** 9))
            self.assertIn('stale (9 bodies in slots 02', line(game))
            self.assertIn('sources changed since the bake (02.dat)', line(game))
            # an older marker without originals_fingerprints: the digest says changed, no file named
            path = game / 'addon/02.x3m-lod.json'
            marker = json.loads(path.read_text())
            marker.pop('originals_fingerprints')
            marker.pop('overlay_dat_bytes')                    # falls back to batch.slots bytes
            os.utime(dat, ns=(dat.stat().st_atime_ns, dat.stat().st_mtime_ns - 10 ** 9))
            # the old digest is keyed by absolute paths: re-derive it for this copy of the tree (as if baked here)
            marker['originals_sha256'] = check.originals_digest(check.fingerprint_files(
                check.original_archives(game.resolve(), {2})))
            os.utime(dat, ns=(dat.stat().st_atime_ns, dat.stat().st_mtime_ns + 10 ** 9))
            path.write_text(json.dumps(marker))
            self.assertIn('sources changed since the bake (the marker predates originals_fingerprints', line(game))
            os.utime(dat, ns=(dat.stat().st_atime_ns, dat.stat().st_mtime_ns - 10 ** 9))
            self.assertIn('ok (9 bodies in slots 02', line(game))
            # a mod overwrote the slot: orphaned (cat hash), and the digest now includes it as a source
            write_catalogue(game / 'addon/02.cat', [('objects/ships/x/modship.bob', b'z')])
            text = line(game)
            self.assertTrue(text.startswith('lod overlay: orphaned (slot 02 overwritten by a mod'), text)
            self.assertIn('rebake with', text)

    def test_cost_free_path(self):
        """No .dat is opened or read, and the line takes well under 0.2 s on the fixture."""
        with tempfile.TemporaryDirectory() as folder:
            base, game = Installed.copy(folder)
            write_catalogue(game / 'addon/03.cat', [('objects/ships/x/good.pbb', b'x')])
            opened = []
            real_open, real_path_open = builtins.open, Path.open

            def spy_open(file, *a, **k):
                opened.append(str(file))
                return real_open(file, *a, **k)

            def spy_path_open(self, *a, **k):
                opened.append(str(self))
                return real_path_open(self, *a, **k)
            t = time.perf_counter()
            with unittest.mock.patch.object(builtins, 'open', spy_open), \
                    unittest.mock.patch.object(Path, 'open', spy_path_open):
                text = line(game)
            elapsed = time.perf_counter() - t
            self.assertIn('stale', text)
            self.assertEqual([p for p in opened if p.lower().endswith('.dat')], [])
            self.assertTrue(any(p.endswith('user.reg') for p in opened))
            self.assertLess(elapsed, 0.2)


class PackageStates(unittest.TestCase):
    """The selected package clause: derived copy valid / stale / orphaned / source_missing, a plain package
    without its derived copy, a derived name without a marker."""

    @unittest.mock.patch.object(lod_overlay, 'running_game', return_value=[])
    def test_package_clauses(self, _running):
        with tempfile.TemporaryDirectory() as folder:
            base, game = Installed.copy(folder)
            tb.make_package(game)
            reg = base / 'user.reg'
            user_reg(base, 'Big')
            text = line(game)                                  # the package shadows ships/x/good: stale
            self.assertIn('stale (9 bodies in slots 02', text)
            self.assertIn('package Big selected (no derived package: 1 overlay bodies shadowed)', text)
            self.assertIn('--mod Big`', text)
            code, _ = tb.run(tb.BATCH + ['--game', str(game), '--install', '--sync', '--registry', str(reg)])
            self.assertEqual(code, 0)
            self.assertIn('ok (9 bodies in slots 02', line(game))
            self.assertIn('package Big selected instead of its derived Big-x3m-lod: 1 overlay bodies shadowed'
                          ' (select Big-x3m-lod in the start menu)', line(game))
            user_reg(base, 'Big-x3m-lod')
            self.assertTrue(line(game).endswith('; package Big-x3m-lod (3 bodies, from Big))'), line(game))
            src = game / 'addon/mods/Big.dat'
            os.utime(src, ns=(src.stat().st_atime_ns, src.stat().st_mtime_ns + 10 ** 9))
            text = line(game)
            self.assertIn('stale (9 bodies', text)
            self.assertIn('package Big-x3m-lod selected, stale (Big.dat changed)', text)
            self.assertIn('--batch --sync --install --game', text)
            self.assertTrue(text.endswith('--mod Big`'), text)
            user_reg(base, '')                                 # not selected: named, not escalated
            self.assertIn('ok (9 bodies', line(game))
            self.assertIn('addon/mods/Big-x3m-lod (not selected) stale: Big.dat changed', line(game))
            user_reg(base, 'Big-x3m-lod')
            saved = {s: (game / f'addon/mods/Big{s}').read_bytes() for s in ('.cat', '.dat')}
            for s in saved:
                (game / f'addon/mods/Big{s}').unlink()
            text = line(game)
            self.assertTrue(text.startswith('lod overlay: source_missing ('), text)
            self.assertIn('addon/mods/Big.cat missing (remove the copy with lod_overlay.py --remove-package Big)', text)
            for s, data in saved.items():
                (game / f'addon/mods/Big{s}').write_bytes(data)
            copy_cat = game / 'addon/mods/Big-x3m-lod.cat'
            copy_cat.write_bytes(copy_cat.read_bytes()[:-1] + b'\0')
            self.assertTrue(line(game).startswith('lod overlay: orphaned ('), line(game))
            self.assertIn('Big-x3m-lod.cat/.dat do not match the marker', line(game))
            user_reg(base, 'Foo-x3m-lod')
            self.assertIn('package Foo-x3m-lod selected but it has no x3m-lod marker (not ours)', line(game))
            user_reg(base, 'Gone')
            self.assertIn('package Gone selected but addon/mods/Gone.cat is missing', line(game))


class Registry(unittest.TestCase):
    def test_parse(self):
        with tempfile.TemporaryDirectory() as folder:
            base = Path(folder)
            for value, want in (('', ''), ('Big', 'Big'), ('Big-x3m-lod', 'Big-x3m-lod'), ('a\\\\b', 'a\\b')):
                user_reg(base, value)
                self.assertEqual(check.read_mod_name(base / 'user.reg'), (want, str(base / 'user.reg')))
            user_reg(base, None)                                           # value absent: no package
            self.assertEqual(check.read_mod_name(base / 'user.reg')[0], '')
        p = check.parse_mod_name
        self.assertEqual(p('[Software\\\\EGOSOFT\\\\X3AP] 1\n"ModName"=hex:00\n'), None)   # not a string
        self.assertEqual(p('[Software\\\\EGOSOFT\\\\X3AP] 1\n"ModName"=str(2):"Env"\n'), 'Env')
        self.assertEqual(p('[software\\\\egosoft\\\\x3ap] 1\n"modname"="Low"\n'), 'Low')
        self.assertEqual(p('[Software\\\\EGOSOFT\\\\X3TC] 1\n"ModName"="tc"\n'), '')
        self.assertEqual(p(''), '')
        self.assertIsNone(check.bottle_registry(Path('/nonexistent/drive_c/X3')))
        self.assertTrue(check.valid_package_name('Big_1-a'))
        self.assertFalse(any(check.valid_package_name(n) for n in ('', 'a.b', 'a/b', 'Ünï', ' a')))


def load_manage():
    spec = importlib.util.spec_from_file_location('lod_check_manage', ROOT / 'tools/manage.py')
    manage = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(manage)
    return manage


class Launcher(unittest.TestCase):
    """tools/manage.py launch --dry-run prints the line on stderr and in the JSON; nothing under --vanilla."""

    def dry_run(self, folder, game, vanilla=False):
        manage = load_manage()
        (game / 'X3AP.exe').touch()
        (game / 'd3d9.dll').write_bytes(b'proxy')
        (game / 'x3-modern-install.json').write_text(json.dumps({'sha256': hashlib.sha256(b'proxy').hexdigest()}))
        wine = Path(folder) / 'wine'
        wine.touch()
        argv = ['manage.py', 'launch', '--dry-run', *(['--vanilla'] if vanilla else []), '--bottle', 'X3',
                '--game-dir', str(game)]
        out, err = io.StringIO(), io.StringIO()
        with unittest.mock.patch.object(sys, 'argv', argv), unittest.mock.patch.object(manage, 'WINE', wine), \
                unittest.mock.patch.object(manage, 'VOICE_DECODER_REPO', Path(folder) / 'no-decoder'), \
                unittest.mock.patch.object(manage.subprocess, 'call', side_effect=AssertionError('never launch')), \
                contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            manage.main()
        got = json.loads(out.getvalue())['lod_overlay']
        lines = [l for l in err.getvalue().splitlines() if l.startswith('lod overlay')]
        return got, lines

    def test_dry_run_line(self):
        with tempfile.TemporaryDirectory() as folder:
            base, game = Installed.copy(folder)
            got, lines = self.dry_run(folder, game)
            self.assertEqual(lines, [got])
            self.assertTrue(got.startswith('lod overlay: ok (9 bodies in slots 02'), got)
            self.assertTrue(got.endswith('no package: ModName empty)'), got)
            self.assertEqual(self.dry_run(folder, game, vanilla=True), (None, []))
            write_catalogue(game / 'addon/03.cat', [('objects/ships/x/good.pbb', b'x')])
            got, lines = self.dry_run(folder, game)                   # stale: still exit 0, one line
            self.assertEqual(lines, [got])
            self.assertTrue(got.startswith('lod overlay: stale'), got)
            (game / 'addon/02.x3m-lod.json').write_text('{')          # a broken marker never blocks
            got, lines = self.dry_run(folder, game)
            self.assertIn('marker 02.x3m-lod.json unreadable', got)

    def test_game_dir_outside_a_bottle(self):
        """No other bottle's user.reg is read: ModName unknown, no package, whatever this machine has."""
        with tempfile.TemporaryDirectory() as folder:
            game = tb.make_game(folder)                               # <folder>/game: no drive_c above it
            tb.make_package(game)
            home = Path(folder) / 'home'                              # an X3 bottle selecting Big: must be ignored
            (home / 'Library/Application Support/CrossOver/Bottles/X3').mkdir(parents=True)
            user_reg(home / 'Library/Application Support/CrossOver/Bottles/X3', 'Big')
            with unittest.mock.patch.object(Path, 'home', return_value=home):
                got, lines = self.dry_run(folder, game)
            self.assertEqual(lines, [got])
            self.assertEqual(got, 'lod overlay: none (no overlay installed; ModName unknown (no bottle registry))')


if __name__ == '__main__':
    unittest.main()
