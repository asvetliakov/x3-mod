"""tools/regenerate/x3m_regenerate.py on a synthetic game root: the full run (fog table, overlay slots, the
derived mod copy, the log with its per-item lines, exit 0), the running-game and missing-EXE refusals, an
injected bake exception (exit 1, traceback in the log), --no-wait and the frozen-path resolution.

make_root(folder) builds the root (usable from the command line: python3 test_regenerate.py --make-root DIR):
<folder>/drive_c/X3 with an X3AP.exe stub, 01.cat (atlas textures, TBackgrounds, one nebula family),
02.cat (two ships and a station), addon/mods/Big.cat selected through <folder>/user.reg."""
import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
for _p in (HERE, ROOT / 'tools' / 'analysis', ROOT / 'tools' / 'regenerate'):
    if str(_p) not in sys.path:
        sys.path.insert(0, str(_p))
import numpy as np  # noqa: E402

import bob1  # noqa: E402
import lod_overlay  # noqa: E402
import x3m_regenerate as regen  # noqa: E402
from sector_fog_census import write_catalogue  # noqa: E402
from test_bob1 import atlas_textures, atlas_tree_lod0  # noqa: E402
from test_fog_families import background_line, body, dds_rgb24  # noqa: E402
from test_lod_overlay_batch import mixed_tree, packed, reg, with_threshold  # noqa: E402


def tbackgrounds(rows):
    return ('25;%d;\n' % len(rows) + '\n'.join(rows) + '\n').encode()


def fog_members(family='zzmod'):
    rows = [background_line(family, [3, 1, 0, 0, 0, 0, 0, 0], 5, 1),
            background_line('zznobody', [1, 0, 0, 0, 0, 0, 0, 0], 2, 2)]
    table = tbackgrounds(rows)
    nebula = 'objects/environments/nebulae'
    grey = np.zeros((8, 8, 3), np.uint8)
    grey[:4] = [180, 40, 20]
    grey[4:] = [30, 60, 180]
    return [('types/TBackgrounds.pck', table),
            (f'{nebula}/{family}/nebula_{family}_dust_part01.pbd', body([rf'environments\nebulae\{family}\tex_a.tga'])),
            (f'{nebula}/{family}/nebula_{family}_dust_part02.pbd', body([rf'environments\nebulae\{family}\tex_b.tga'])),
            ('dds/tex_a.pck', dds_rgb24(grey)),
            ('dds/tex_b.pck', dds_rgb24(np.full((16, 16, 3), [20, 200, 90], np.uint8)))]


def package_fog_members():
    """The package's own TBackgrounds (the vanilla rows plus zzpkg) and zzpkg's dust part and texture."""
    rows = [background_line('zzmod', [3, 1, 0, 0, 0, 0, 0, 0], 5, 1),
            background_line('zznobody', [1, 0, 0, 0, 0, 0, 0, 0], 2, 2),
            background_line('zzpkg', [1, 0, 0, 0, 0, 0, 0, 0], 4, 3)]
    return [('types/TBackgrounds.pck', tbackgrounds(rows)),
            ('objects/environments/nebulae/zzpkg/nebula_zzpkg_dust_part01.pbd',
             body([r'environments\nebulae\zzpkg\tex_p.tga'])),
            ('dds/tex_p.pck', dds_rgb24(np.full((8, 8, 3), [150, 30, 200], np.uint8)))]


def make_root(folder, mod='Big'):
    """The game directory <folder>/drive_c/X3; with mod, addon/mods/<mod>.cat selected in <folder>/user.reg."""
    game = Path(folder) / 'drive_c' / 'X3'
    game.mkdir(parents=True)
    (game / 'X3AP.exe').write_bytes(b'MZ stub')
    write_catalogue(game / '01.cat', atlas_textures() + fog_members())
    write_catalogue(game / '02.cat', [
        ('objects/ships/x/good.pbb', packed(atlas_tree_lod0())),
        ('objects/ships/x/mixed.pbb', packed(mixed_tree())),
        ('objects/ships/x/badtext.pbd', b'BODY 0\n'),                      # refused: text_parse_error
        ('objects/stations/y/good.pbb', packed(with_threshold(atlas_tree_lod0(), 160)))])
    if mod:
        write_catalogue(game / f'addon/mods/{mod}.cat', [('types/TShips.txt', b'mod types\n'),
                                                         ('objects/ships/x/good.pbb', packed(mixed_tree())),
                                                         ('objects/ships/x/newship.pbb', packed(atlas_tree_lod0()))]
                         + package_fog_members())
        reg(Path(folder) / 'user.reg', mod)
    return game


def run(argv):
    out = io.StringIO()
    with mock.patch.object(regen, 'running_game', return_value=[]), contextlib.redirect_stdout(out):
        code = regen.main(argv)
    return code, out.getvalue()


class Regenerate(unittest.TestCase):
    def test_full_run_and_rerun(self):
        with tempfile.TemporaryDirectory() as folder:
            game = make_root(folder)
            code, text = run(['--game-dir', str(game), '--no-wait', '--jobs', '1'])
            log = (game / regen.LOG_NAME).read_text()
            self.assertEqual(code, 0, log)
            self.assertTrue(all(line in log for line in text.splitlines()))     # the console lines, teed
            fog = json.loads((game / 'x3m/fog-families.json').read_text())
            self.assertEqual((fog['file']['families'], fog['counts'].get('refused:no_dust_bodies')), (2, 1))
            self.assertEqual(fog['mod_cats'], [str((game / 'addon/mods/Big.cat').resolve())])     # the selected package layer
            self.assertRegex(text, r'processing fog zzmod \(\d/2\) baked')
            self.assertRegex(text, r'processing fog zzpkg \(\d/2\) baked')        # only the package has it
            self.assertIn('fog layers: 01.cat, 02.cat, ', text)
            self.assertIn('addon/mods/Big.cat', [l for l in text.splitlines() if 'fog layers:' in l][0])
            self.assertIn('refused fog zznobody: no_dust_bodies', text)
            self.assertIn('selected mod: Big', text)
            for stem in ('ships/x/good', 'ships/x/mixed'):
                self.assertRegex(text, rf'processing model {stem} \(\d/\d\)\n')
            self.assertRegex(text, r'processing model ships/x/newship \(\d/\d\) \[mod Big\]')
            self.assertIn('refused model ships/x/badtext: text_parse_error', text)
            self.assertIn('refused model stations/y/good: texel_floor', text)   # production texel floor
            self.assertIn('slot plan: addon/01 2 bodies', text)
            self.assertIn('fog families: check passed after the LOD overlay', text)
            self.assertIn('all done', text)
            self.assertIn('   | batch: game', log)                           # the tool detail, log only
            self.assertNotIn('batch: game', text)
            marker = json.loads((game / 'addon/01.x3m-lod.json').read_text())
            self.assertEqual(sorted(b['name'] for b in marker['bodies']),
                             ['ships/x/good', 'ships/x/mixed'])
            self.assertTrue((game / 'addon/mods/Big-x3m-lod.cat').is_file())
            # second run: overwrites in place, every body reused (--sync), the log starts fresh
            first = (game / 'addon/01.dat').read_bytes()
            code, text = run(['--game-dir', str(game), '--no-wait', '--jobs', '1'])
            self.assertEqual(code, 0, (game / regen.LOG_NAME).read_text())
            self.assertNotIn('processing model', text)
            self.assertIn('0 baked + 2 unchanged = 2 in the overlay', text)
            self.assertEqual((game / 'addon/01.dat').read_bytes(), first)
            self.assertTrue((game / 'x3m/fog-families.bin.previous').is_file())
            self.assertEqual((game / regen.LOG_NAME).read_text().count('x3m-regenerate: game directory'), 1)
            self.assertFalse(list(game.rglob('*.x3m-replaced')) + list(game.rglob('*.tmp')))

    def test_interrupted_write_recovered(self):
        with tempfile.TemporaryDirectory() as folder:
            game = make_root(folder)
            self.assertEqual(run(['--game-dir', str(game), '--no-wait', '--jobs', '1'])[0], 0)
            names = ['addon/01.cat', 'addon/01.dat', 'addon/01.x3m-lod.json', 'addon/mods/Big-x3m-lod.cat',
                     'addon/mods/Big-x3m-lod.dat']
            before = {n: (game / n).read_bytes() for n in names}
            for n in names:                     # killed mid-write: previous files aside, partial new ones in place
                (game / n).rename(game / (n + '.x3m-replaced'))
            (game / 'addon/01.cat').write_bytes(before['addon/01.cat'][:7])
            (game / 'addon/mods/Big-x3m-lod.cat').write_bytes(b'partial')
            code, text = run(['--game-dir', str(game), '--no-wait', '--jobs', '1'])
            self.assertEqual(code, 0, (game / regen.LOG_NAME).read_text())
            self.assertIn('recovered interrupted write of addon/01.cat', text)
            self.assertIn('recovered interrupted write of addon/mods/Big-x3m-lod.cat', text)
            self.assertIn('0 baked + 2 unchanged = 2 in the overlay', text)       # the restored overlay is reused
            self.assertEqual({n: (game / n).read_bytes() for n in names[:2]}, {n: before[n] for n in names[:2]})
            self.assertFalse(list(game.rglob('*.x3m-replaced')))

    def test_refusals(self):
        with tempfile.TemporaryDirectory() as folder:
            game = make_root(folder, mod=None)
            out = io.StringIO()
            with mock.patch.object(regen, 'running_game', return_value=['123 X3AP.exe']), \
                    contextlib.redirect_stdout(out):
                self.assertEqual(regen.main(['--game-dir', str(game), '--no-wait']), 1)
            self.assertIn('refused: the game is running (123 X3AP.exe)', out.getvalue())
            self.assertFalse((game / 'x3m').exists() or (game / 'addon').exists())
            with mock.patch.object(regen, 'running_game', side_effect=RuntimeError('ps exit 1')), \
                    contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(regen.main(['--game-dir', str(game), '--no-wait']), 1)
            self.assertIn('cannot tell whether the game is running (ps exit 1)', (game / regen.LOG_NAME).read_text())
            (game / 'X3AP.exe').unlink()
            code, text = run(['--game-dir', str(game), '--no-wait'])
            self.assertEqual(code, 1)
            self.assertIn('holds no X3AP.exe', text)

    def test_bake_exception_fails_with_traceback(self):
        with tempfile.TemporaryDirectory() as folder:
            game = make_root(folder, mod=None)

            def boom(*args, **kwargs):
                raise RuntimeError('injected bake failure')
            with mock.patch.object(lod_overlay, 'bake_rows', side_effect=boom):
                code, text = run(['--game-dir', str(game), '--no-wait', '--jobs', '1'])
            log = (game / regen.LOG_NAME).read_text()
            self.assertEqual(code, 1)
            self.assertIn('LOD overlay: FAILED with an unexpected error:', text)
            self.assertIn('Traceback (most recent call last):', log)
            self.assertIn('RuntimeError: injected bake failure', log)
            self.assertIn('FAILED: LOD overlay', text)
            self.assertTrue((game / 'x3m/fog-families.bin').is_file())       # the fog step still ran
            self.assertFalse((game / 'addon').exists() and list((game / 'addon').glob('[0-9][0-9].*')))

    def test_wait_and_no_wait(self):
        with tempfile.TemporaryDirectory() as folder:
            game = Path(folder)                     # no X3AP.exe: the quickest complete run
            with mock.patch.object(regen, 'wait_for_key') as wait, contextlib.redirect_stdout(io.StringIO()):
                regen.main(['--game-dir', str(game), '--no-wait'])
                wait.assert_not_called()
                regen.main(['--game-dir', str(game)])
                wait.assert_called_once()
            with mock.patch.object(sys, 'stdin', io.StringIO('')), contextlib.redirect_stdout(io.StringIO()) as out:
                regen.wait_for_key('press')              # no tty: input() at EOF returns
            self.assertEqual(out.getvalue(), 'press\n')

    def test_frozen_path_resolution(self):
        with tempfile.TemporaryDirectory() as folder:
            exe = Path(folder) / 'x3m-regenerate.exe'
            with mock.patch.object(sys, 'frozen', True, create=True), mock.patch.object(sys, 'executable', str(exe)):
                self.assertEqual(regen.default_game_dir(), Path(folder).resolve())
            with mock.patch.object(sys, 'frozen', False, create=True):
                self.assertEqual(regen.default_game_dir(), Path.cwd())

    def test_lod_jobs_memory_cap(self):
        with mock.patch.object(lod_overlay, 'default_jobs', return_value=6), \
                mock.patch.object(regen, 'host_memory_bytes', return_value=24 << 30):
            self.assertEqual(regen.lod_jobs(15), 2)                  # 24 GiB // 7 GiB - 1
        with mock.patch.object(lod_overlay, 'default_jobs', return_value=6), \
                mock.patch.object(regen, 'host_memory_bytes', return_value=None):
            self.assertEqual((regen.lod_jobs(15), regen.lod_jobs(1)), (6, 1))

    def test_windows_process_check(self):
        csv_text = '"System Idle Process","0","Services","0","8 K"\n"X3AP.exe","4242","Console","1","900,000 K"\n'
        done = mock.Mock(returncode=0, stdout=csv_text)
        with mock.patch.object(regen.sys, 'platform', 'win32'), \
                mock.patch.object(regen.subprocess, 'run', return_value=done):
            self.assertEqual(len(regen.running_game()), 1)
        done.stdout = 'INFO: No tasks are running which match the specified criteria.\n'
        with mock.patch.object(regen.sys, 'platform', 'win32'), \
                mock.patch.object(regen.subprocess, 'run', return_value=done):
            self.assertEqual(regen.running_game(), [])


if __name__ == '__main__':
    if sys.argv[1:2] == ['--make-root']:
        print(make_root(sys.argv[2]))
    else:
        unittest.main()
