"""Offline package admission and transaction faults: tiny files, no Wine/game."""
import copy
import contextlib
import io
import sys
import threading
import json
from pathlib import Path
import shutil
import struct
import tempfile
import unittest
from unittest import mock
from tools import media_package as mp


def tiny_pe(machine=0x14c):
    data = bytearray(1024)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 0x3c, 0x80)
    data[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<HH', data, 0x84, machine, 1)
    struct.pack_into('<H', data, 0x94, 224)
    struct.pack_into('<H', data, 0x98, 0x10b)
    struct.pack_into('<IIII', data, 0x98 + 224 + 8, 512, 4096, 512, 512)
    return data


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.game = self.root / 'Game ü space'
        self.game.mkdir()
        for name, value in [('X3AP.exe', b'original exe'), ('01.cat', b'cat'), ('01.dat', b'dat'),
                            ('mov/00002.dat', b'original clip'), ('mov/00002.dat.orig', b'original clip'),
                            ('settings.json', b'unrelated settings')]:
            path = self.game / name; path.parent.mkdir(exist_ok=True); path.write_bytes(value)
        self.originals = {p.relative_to(self.game).as_posix(): mp.identity(p) for p in self.game.rglob('*') if p.is_file()}
        self.dll = self.root / 'candidate.dll'; self.dll.write_bytes(b'first proxy')
        self.stage = self.root / 'stage'; self.stage.mkdir()
        files = {}
        for name, (role, origin) in mp.MODULES.items():
            path = self.stage / 'provider' / name; path.parent.mkdir(exist_ok=True)
            path.write_bytes(tiny_pe())
            files[name] = dict(path='provider/' + name, **mp.identity(path), role=role, origin=origin)
        ns = 'urn:schemas-microsoft-com:asm.v1'
        for name in mp.MANIFESTS:
            rows = []
            for module in mp.MODULES:
                role = mp.MODULES[module][0]
                if name == mp.MANIFESTS[1] and role in ('source', 'video'):
                    continue
                child = '<comClass clsid="' + mp.CLASSES[role] + '" threadingModel="Both"/>' if role in mp.CLASSES else ''
                rows.append('<file name="' + module + '">' + child + '</file>')
            path = self.stage / 'provider' / name
            path.write_text('<assembly xmlns="' + ns + '"><assemblyIdentity processorArchitecture="x86"/>' + ''.join(rows) + '</assembly>')
            files[name] = dict(path='provider/' + name, **mp.identity(path), role='manifest', origin='official')
        for name in ('COPYING', 'README.md', 'FFmpeg-LICENSE.md'):
            rel = 'notices/' + name; path = self.stage / 'provider' / rel; path.parent.mkdir(exist_ok=True)
            path.write_text('local test notice')
            files[rel] = dict(path='provider/' + rel, **mp.identity(path), role='notice', origin='notice')
        original = mp.identity(self.game / 'mov/00002.dat')
        asset = 'sources/' + original['sha256'] + '/00002.mkv'
        path = self.stage / asset; path.parent.mkdir(parents=True); path.write_bytes(b'derived test clip')
        self.package = dict(schema=1, kind='x3-owned-media-package', layout='staged', path_base='package_root',
                            scope='local_qualification', package_id='test-v1', architecture='x86', profile=mp.PROFILE,
                            distribution_qualified=False, provider=dict(directory='provider', manifest='provider/provider.manifest',
                            source_clsid=mp.CLASSES['source'], video_clsid=mp.CLASSES['video'], files=files),
                            sources=[dict(id=2,effective_flags=8,original_relative='mov/00002.dat',original_bytes=original['bytes'],
                            original_sha256=original['sha256'],asset=asset,asset_bytes=path.stat().st_size,asset_sha256=mp.sha256(path),
                            codec='mpeg1video',timeline='generated_timestamps',derivation_record_sha256='a'*64)], provenance={})
        self.write_package()
        self.closed = mock.patch.object(mp, 'assert_game_closed'); self.closed.start(); self.addCleanup(self.closed.stop)

    def write_package(self):
        (self.stage / 'package.json').write_bytes(mp.encoded(self.package))

    def install(self, package=True, **kwargs):
        return mp.install(self.game, self.dll, {'source_commit': 'test'}, self.stage / 'package.json' if package else None, **kwargs)

    def assert_originals(self):
        for name, row in self.originals.items():
            mp.verify(self.game / name, row)

    def test_install_relocate_rollback_uninstall(self):
        first = self.install()
        self.assertEqual(first['schema'], 2)
        self.assertEqual(len(mp.selection_files(self.game, first)), 17)
        asset = self.game / mp.read_json(self.game / first['media']['package_record_relative'])['sources'][0]['asset']
        inode = asset.stat().st_ino
        self.dll.write_bytes(b'second proxy')
        second = self.install()
        self.assertEqual(inode, asset.stat().st_ino)  # shared cache reused
        moved = self.root / 'relocated ü'; self.game.rename(moved); self.game = moved
        self.assertEqual(mp.current(self.game)['sha256'], second['sha256'])
        mp.rollback(self.game)
        self.assertEqual(mp.current(self.game)['sha256'], first['sha256'])
        self.assertEqual(mp.uninstall(self.game), [])
        self.assertFalse((self.game / 'd3d9.dll').exists())
        self.assert_originals()

    def test_old_dll_only_manifest(self):
        (self.game / 'd3d9.dll').write_bytes(b'old')
        old = dict(project='x3-modern-renderer',sha256=mp.sha256(self.game / 'd3d9.dll'),source_commit='old')
        (self.game / mp.INSTALL).write_bytes(mp.encoded(old))
        self.install(package=False)
        mp.rollback(self.game)
        self.assertEqual(mp.current(self.game)['sha256'], old['sha256'])
        self.assertNotIn('media', mp.current(self.game))
        mp.uninstall(self.game); self.assert_originals()

    def test_interrupted_each_phase_recovery(self):
        for initial in (True, False):
            for phase in ('prepared', 'dll', 'manifest'):
                with self.subTest(initial=initial, phase=phase):
                    if not initial:
                        self.install()
                    before = (self.game / mp.INSTALL).read_bytes() if (self.game / mp.INSTALL).exists() else None
                    self.dll.write_bytes(('new ' + phase).encode())
                    def fault(at):
                        if at == phase:
                            raise RuntimeError('injected interruption')
                    with self.assertRaises(RuntimeError):
                        self.install(fault=fault)
                    with self.assertRaises(mp.PackageError):
                        mp.no_journal(self.game)
                    mp.recover(self.game)
                    if before:
                        self.assertEqual((self.game / mp.INSTALL).read_bytes(), before)
                        mp.current(self.game); mp.uninstall(self.game)
                    else:
                        self.assertFalse((self.game / mp.INSTALL).exists())
                    self.assert_originals()

    def test_rollback_and_uninstall_interrupted(self):
        self.install(); self.dll.write_bytes(b'next'); self.install()
        for operation in (mp.rollback, mp.uninstall):
            for phase in ('prepared', 'dll', 'manifest'):
                before = (self.game / mp.INSTALL).read_bytes()
                def fault(at):
                    if at == phase:
                        raise RuntimeError('fault')
                with self.assertRaises(RuntimeError):
                    operation(self.game, fault=fault)
                mp.recover(self.game)
                self.assertEqual((self.game / mp.INSTALL).read_bytes(), before)
                mp.current(self.game)
        self.assert_originals()

    def test_changed_dll_and_unowned_refusal(self):
        (self.game / 'd3d9.dll').write_bytes(b'foreign')
        with self.assertRaises(mp.PackageError): self.install()
        (self.game / 'd3d9.dll').unlink(); self.install()
        (self.game / 'd3d9.dll').write_bytes(b'changed')
        for operation in (self.install, lambda: mp.uninstall(self.game), lambda: mp.rollback(self.game)):
            with self.assertRaises(mp.PackageError): operation()
        self.assert_originals()

    def test_changed_media_and_foreign_addition_retained(self):
        installed = self.install()
        directory = (self.game / installed['media']['package_record_relative']).parent
        (directory / 'LAVVideo.ax').write_bytes(b'changed')
        (directory / 'foreign.txt').write_bytes(b'foreign')
        with self.assertRaises(mp.PackageError): self.install()
        retained = mp.uninstall(self.game)
        self.assertIn(str((directory / 'LAVVideo.ax').relative_to(self.game)), retained)
        self.assertEqual((directory / 'foreign.txt').read_bytes(), b'foreign')
        self.assert_originals()

    def test_partial_copy_leaves_selection_unchanged(self):
        self.install(package=False)
        before = (self.game / mp.INSTALL).read_bytes()
        original_copy = shutil.copyfile
        def partial(src, dst):
            Path(dst).write_bytes(b'partial')
            return dst
        with mock.patch.object(shutil, 'copyfile', side_effect=partial):
            with self.assertRaises(mp.PackageError): self.install()
        self.assertEqual((self.game / mp.INSTALL).read_bytes(), before)
        mp.current(self.game); self.assert_originals()

    def test_bad_machine_missing_mixed_and_manifest(self):
        for name, data in [('LAVVideo.ax', tiny_pe(0x8664)), ('provider.manifest', b'<bad/>')]:
            with self.subTest(name=name):
                path = self.stage / 'provider' / name; saved = path.read_bytes(); row = copy.deepcopy(self.package['provider']['files'][name])
                path.write_bytes(data); self.package['provider']['files'][name].update(mp.identity(path)); self.write_package()
                with self.assertRaises(mp.PackageError): self.install()
                path.write_bytes(saved); self.package['provider']['files'][name] = row; self.write_package()
        (self.stage / 'provider/LAVVideo.ax').unlink()
        with self.assertRaises(mp.PackageError): self.install()

    def test_missing_assembly_identity_reports_package_error(self):
        path = self.stage / 'provider/provider.manifest'
        path.write_text(path.read_text().replace('<assemblyIdentity processorArchitecture="x86"/>', ''))
        self.package['provider']['files']['provider.manifest'].update(mp.identity(path))
        self.write_package()
        with self.assertRaisesRegex(mp.PackageError, 'missing manifest assemblyIdentity'):
            self.install()

    def test_changed_control_record_aborts_uninstall(self):
        installed = self.install()
        before = (self.game / mp.INSTALL).read_bytes()
        package = self.game / installed['media']['package_record_relative']
        package.write_bytes(package.read_bytes() + b' ')
        with self.assertRaisesRegex(mp.PackageError, 'changed package record'):
            mp.uninstall(self.game)
        self.assertEqual((self.game / mp.INSTALL).read_bytes(), before)
        self.assertEqual((self.game / 'd3d9.dll').read_bytes(), self.dll.read_bytes())

    def run_launcher(self, launch, *options):
        from tools import manage
        wine = self.root / 'wine-placeholder'; wine.touch()
        with mock.patch.object(sys, 'argv', ['manage.py', 'launch', '--game-dir', str(self.game), *options]), \
             mock.patch.object(manage, 'WINE', wine), mock.patch.object(manage, 'media_package', mp), \
             mock.patch.object(manage, 'launch_teed', side_effect=launch), \
             mock.patch.object(manage.subprocess, 'Popen', side_effect=AssertionError('no actual child')), \
             contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            try:
                manage.main()
            except SystemExit as result:
                return result.code
        return 0

    def test_launch_holds_installer_lock_through_child_lifetime(self):
        self.install(package=False)
        entered, enter_teardown, teardown, finish = (threading.Event() for _ in range(4))
        results = []
        def child(*args, **kwargs):
            entered.set()
            if not enter_teardown.wait(5): raise AssertionError('creation gate timed out')
            teardown.set()
            if not finish.wait(5): raise AssertionError('teardown gate timed out')
            return 0
        thread = threading.Thread(target=lambda: results.append(self.run_launcher(child)))
        thread.start()
        try:
            self.assertTrue(entered.wait(5))
            with self.assertRaisesRegex(mp.PackageError, 'installer or launcher'):
                self.install(package=False)
            enter_teardown.set()
            self.assertTrue(teardown.wait(5))
            with self.assertRaisesRegex(mp.PackageError, 'installer or launcher'):
                mp.uninstall(self.game)
        finally:
            enter_teardown.set(); finish.set(); thread.join(5)
        self.assertFalse(thread.is_alive())
        self.assertEqual(results, [0])
        self.install(package=False)  # released after child lifecycle completion

    def test_final_launch_check_refuses_journal_before_any_child(self):
        self.install(package=False)
        real_lock = mp.installer_lock
        @contextlib.contextmanager
        def inject_journal(game, **kwargs):
            with real_lock(game, **kwargs):
                (game / mp.JOURNAL).write_text('{}')
                yield
        child = mock.Mock(side_effect=AssertionError('journal must block child'))
        for options in ((), ('--dry-run',), ('--vanilla',)):
            with mock.patch.object(mp, 'installer_lock', inject_journal):
                self.assertEqual(self.run_launcher(child, *options), 2)
            child.assert_not_called()
            (self.game / mp.JOURNAL).unlink()

    def assert_launch_refuses_media_owner(self, project):
        installed = self.install()
        if project is None:
            installed.pop('project')
        else:
            installed['project'] = project
        (self.game / mp.INSTALL).write_bytes(mp.encoded(installed))
        # Isolate ownership: both proxy digest and complete media selection verify.
        self.assertEqual(mp.sha256(self.game / 'd3d9.dll'), installed['sha256'])
        mp.selection_files(self.game, installed)
        child = mock.Mock(side_effect=AssertionError('foreign manifest must block child'))
        for options in ((), ('--dry-run',)):
            self.assertEqual(self.run_launcher(child, *options), 2)
        child.assert_not_called()

    def test_media_launch_rejects_foreign_project_with_valid_hashes(self):
        self.assert_launch_refuses_media_owner('foreign-project')

    def test_media_launch_rejects_missing_project_with_valid_hashes(self):
        self.assert_launch_refuses_media_owner(None)

    def test_dry_run_validates_under_lock_without_child_or_process_check(self):
        self.install(package=False)
        guard = mp.assert_game_closed; guard.reset_mock()
        original_validation = mp.selection_files
        def validate(game, selected):
            with self.assertRaisesRegex(mp.PackageError, 'installer or launcher'):
                with mp.installer_lock(game): pass
            return original_validation(game, selected)
        child = mock.Mock(side_effect=AssertionError('dry-run must not start child'))
        with mock.patch.object(mp, 'selection_files', side_effect=validate):
            self.assertEqual(self.run_launcher(child, '--dry-run'), 0)
        child.assert_not_called(); guard.assert_not_called()

    def test_native_legacy_guard_uses_tasklist_without_posix_probe(self):
        from tools import media_transcode as legacy
        self.closed.stop()
        real_guard = mp.assert_game_closed
        def native_guard():
            with mock.patch.object(mp.os, 'name', 'nt'), \
                 mock.patch.object(mp.subprocess, 'run', return_value=mock.Mock(stdout='INFO: No tasks match.')) as run:
                real_guard()
                self.assertEqual(run.call_args.args[0][0], 'tasklist')
        try:
            with mock.patch.object(mp, 'assert_game_closed', side_effect=native_guard), \
                 mock.patch.object(legacy, 'x3_running', side_effect=AssertionError('POSIX probe called')):
                args = mock.Mock(game_dir=str(self.game), id=2)
                self.assertEqual(legacy.cmd_restore(args), 0)
        finally:
            self.closed.start()

    def test_unknown_import_and_unresolved_symbol(self):
        observed = {n.lower(): ({}, set()) for n in mp.MODULES}
        def inspect(path): return observed[path.name.lower()]
        observed['lavvideo.ax'] = ({'foreign.dll': ['x']}, set())
        with mock.patch.object(mp, 'pe_info', side_effect=inspect):
            with self.assertRaises(mp.PackageError): self.install()
        observed['lavvideo.ax'] = ({'avcodec-lav-62.dll': ['missing']}, set())
        with mock.patch.object(mp, 'pe_info', side_effect=inspect):
            with self.assertRaises(mp.PackageError): self.install()

    def test_path_case_symlink_and_unknown_files(self):
        for path in ('../outside', '/absolute', 'C:/foo', 'foo\\bar', 'a//b', 'nul.txt', 'trailing.', 'a:stream'):
            with self.subTest(path=path), self.assertRaises(mp.PackageError): mp.safe(self.game, path)
        outside = self.root / 'outside'; outside.mkdir()
        (self.game / mp.MEDIA).symlink_to(outside, target_is_directory=True)
        with self.assertRaises(mp.PackageError): self.install()
        (self.game / mp.MEDIA).unlink()
        (self.stage / 'provider/foreign.dll').write_bytes(b'foreign')
        with self.assertRaises(mp.PackageError): self.install()
        (self.stage / 'provider/foreign.dll').unlink()
        (self.stage / 'provider/lavvideo.ax').write_bytes(b'alias')
        with self.assertRaises(mp.PackageError): self.install()

    def test_wrong_original_derived_and_source_id(self):
        (self.game / 'mov/00002.dat').write_bytes(b'legacy transcode')
        with self.assertRaises(mp.PackageError): self.install()
        (self.game / 'mov/00002.dat').write_bytes(b'original clip')
        self.package['sources'][0]['id'] = 1; self.write_package()
        with self.assertRaises(mp.PackageError): self.install()
        self.package['sources'][0]['id'] = 2; self.write_package()
        (self.stage / self.package['sources'][0]['asset']).write_bytes(b'wrong asset')
        with self.assertRaises(mp.PackageError): self.install()

    def test_legacy_guard_and_journal(self):
        mp.guard_legacy(self.game, 2)
        self.install()
        with self.assertRaises(mp.PackageError): mp.guard_legacy(self.game, 2)
        mp.guard_legacy(self.game, 1)
        (self.game / mp.JOURNAL).write_text('{}')
        with self.assertRaises(mp.PackageError): mp.guard_legacy(self.game, 1)

    def test_process_enumeration_failure_and_running(self):
        self.closed.stop()
        with mock.patch.object(subprocess := mp.subprocess, 'run', return_value=mock.Mock(returncode=2, stdout='')):
            with self.assertRaises(mp.PackageError): mp.assert_game_closed()
        with mock.patch.object(subprocess, 'run', return_value=mock.Mock(returncode=0, stdout='123 X3AP.exe')):
            with self.assertRaises(mp.PackageError): mp.assert_game_closed()
        self.closed.start()

    def test_interrupted_uninstall_restores_pair_with_changed_media(self):
        installed = self.install()
        directory = (self.game / installed['media']['package_record_relative']).parent
        (directory / 'LAVVideo.ax').write_bytes(b'changed')
        before = (self.game / mp.INSTALL).read_bytes()
        with self.assertRaises(RuntimeError):
            mp.uninstall(self.game, fault=lambda phase: (_ for _ in ()).throw(RuntimeError()) if phase == 'dll' else None)
        mp.recover(self.game)
        self.assertEqual((self.game / mp.INSTALL).read_bytes(), before)
        self.assertEqual((directory / 'LAVVideo.ax').read_bytes(), b'changed')
        with self.assertRaises(mp.PackageError): mp.current(self.game)

    def test_legacy_commands_cannot_change_managed_source(self):
        from tools import media_transcode as legacy
        self.install()
        args = mock.Mock(game_dir=str(self.game), id=2)
        for operation in (legacy.cmd_install, legacy.cmd_restore):
            with self.assertRaises(legacy.ToolError): operation(args)
        self.assert_originals()

    def test_recovery_refuses_changed_current_pair(self):
        self.install()
        self.dll.write_bytes(b'next')
        with self.assertRaises(RuntimeError):
            self.install(fault=lambda phase: (_ for _ in ()).throw(RuntimeError()))
        (self.game / 'd3d9.dll').write_bytes(b'foreign')
        with self.assertRaises(mp.PackageError): mp.recover(self.game)
        self.assertTrue((self.game / mp.JOURNAL).exists())


if __name__ == '__main__':
    unittest.main()
