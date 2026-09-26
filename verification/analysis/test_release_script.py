"""The one-command release (tools/release/release.py): argument handling, the dirty-tree refusal, the dry run, the
record schema with every build step mocked, the version single source (CMakeLists.txt project(VERSION) -> package.py
and the DLL's X3M_VERSION define) and tools/regenerate/build.py's --dist and lock-holder detection. Host only: no
compiler, PyInstaller or Wine runs."""
import contextlib
import hashlib
import importlib.util
import io
import json
import os
import re
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def quiet(function, *args):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        code = function(*args)
    return code, out.getvalue(), err.getvalue()


class ReleaseScript(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.release = load('release_script', 'tools/release/release.py')
        cls.build = load('regenerate_build', 'tools/regenerate/build.py')

    def test_arguments(self):
        with contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):
                self.release.parse([])  # --out is required
            with self.assertRaises(SystemExit):
                self.release.parse(['--out', 'x', '--regenerate-dir', 'y', '--skip-windows'])
        args = self.release.parse(['--out', 'x'])
        self.assertEqual((args.allow_dirty, args.regenerate_dir, args.skip_windows, args.dry_run), (False, None, False, False))
        with contextlib.redirect_stdout(io.StringIO()) as out, self.assertRaises(SystemExit):
            self.release.parse(['-h'])
        for word in ('--allow-dirty', '--regenerate-dir', '--skip-windows', '--dry-run', 'release-<version>.json'):
            self.assertIn(word, out.getvalue())

    def test_tracked_changes(self):
        porcelain = ' M src/proxy/capture.cpp\n?? scratch.txt\nA  tools/new.py\n!! build/\n'
        self.assertEqual(self.release.tracked_changes(porcelain), [' M src/proxy/capture.cpp', 'A  tools/new.py'])
        self.assertEqual(self.release.tracked_changes('?? a\n'), [])

    def test_dirty_tree_refused(self):
        with tempfile.TemporaryDirectory() as out:
            release = self.release.Release(self.release.parse(['--out', out]))
            status = {'rev-parse': 'a' * 40 + '\n', 'status': ' M CMakeLists.txt\n'}
            with mock.patch.object(self.release, 'git', side_effect=lambda *a: status[a[0]]):
                with self.assertRaisesRegex(self.release.ReleaseError, 'source commit.*--allow-dirty'):
                    release.tree()
                allowed = self.release.Release(self.release.parse(['--out', out, '--allow-dirty']))
                quiet(allowed.tree)
            self.assertEqual((allowed.record['dirty'], allowed.record['dirty_files']), (True, ['CMakeLists.txt']))

    def test_dry_run_builds_nothing(self):
        with tempfile.TemporaryDirectory() as base:
            out = Path(base) / 'rel'
            tools = {'mingw_gcc': 'gcc 1', 'cmake': 'cmake 2', 'python': '3'}
            with mock.patch.object(self.release, 'toolchain', return_value=tools), \
                    mock.patch.object(self.release, 'windows_state', return_value=(True, 'bottle ok')), \
                    mock.patch.object(self.release, 'git', return_value=''), \
                    mock.patch.object(self.release, 'run', side_effect=AssertionError('dry run ran a command')):
                code, text, _ = quiet(self.release.main, ['--out', str(out), '--dry-run'])
                self.assertEqual(code, 0)
                self.assertFalse(out.exists())
                self.assertIn('wine_lock.py build.py --windows', text)
                self.assertIn('gcc 1', text)
                with mock.patch.object(self.release, 'windows_state', return_value=(False, 'bottle X3M-Build absent')):
                    code, text, _ = quiet(self.release.main, ['--out', str(out), '--dry-run'])
                self.assertEqual(code, 1)
                self.assertIn('build.py --windows --dry', text)  # tells the user how to set the bottle up
                code, text, _ = quiet(self.release.main, ['--out', str(out), '--dry-run', '--skip-windows'])
                self.assertIn('NO release zip', text)
                self.assertIn('package  SKIPPED', text)

    def fake_run(self, out, calls):
        """A stand-in for release.run: fakes each tool's output files and text."""
        def run(cmd, env=None, cwd=None, echo=True):
            cmd = [str(c) for c in cmd]
            calls.append((cmd, env))
            if cmd[0] == 'cmake' and '--build' in cmd:
                (Path(cmd[cmd.index('--build') + 1]) / 'd3d9.dll').write_bytes(b'MZ fake proxy')
                return 0, '[100%] Built target d3d9\n'
            if cmd[0] == 'cmake':
                build = Path(cmd[cmd.index('-B') + 1])
                build.mkdir(parents=True)
                (build / 'CMakeCache.txt').write_text('')
                return 0, '-- Configuring done\n'
            script = Path(cmd[1]).name
            if script == 'check_no_x87.py':
                return 0, json.dumps({'result': 'PASS', 'reachable_functions': 7, 'violations': {}})
            if script == 'generate.py':
                return 0, ''
            if script in ('build.py', 'wine_lock.py'):
                dist = Path(cmd[cmd.index('--dist') + 1])
                name = 'x3m-regenerate.exe' if '--windows' in cmd else 'x3m-regenerate'
                (dist / name).write_bytes(name.encode())
                if script == 'wine_lock.py':
                    Path(cmd[cmd.index('--timings-json') + 1]).write_text('{"lock_wait_seconds": 0.1}')
                return 0, 'smoke test PASS: exit 0\n'
            if script == 'package.py':
                with contextlib.redirect_stdout(io.StringIO()):
                    return self.package.main(cmd[2:]), ''
            raise AssertionError(cmd)
        return run

    def test_record_schema(self):
        self.package = load('release_package_for_release', 'tools/release/package.py')
        with tempfile.TemporaryDirectory() as out, \
                mock.patch.object(self.release, 'git', side_effect=lambda *a: {'rev-parse': 'b' * 40, 'status': ''}[a[0]]), \
                mock.patch.object(self.release, 'toolchain', return_value={'mingw_gcc': 'gcc', 'cmake': 'cmake', 'python': '3'}), \
                mock.patch.object(self.release, 'windows_state', return_value=(True, 'ok')), \
                mock.patch.object(self.release, 'game_guard_clear') as guard, \
                mock.patch.object(self.package, 'source_commit', return_value='b' * 40):
            calls = []
            with mock.patch.object(self.release, 'run', side_effect=self.fake_run(out, calls)):
                code, text, err = quiet(self.release.main, ['--out', out])
            self.assertEqual(code, 0, err)
            guard.assert_called_once()
            wine = [(cmd, env) for cmd, env in calls if Path(cmd[1]).name == 'wine_lock.py']
            self.assertEqual(len(wine), 1)
            self.assertEqual(wine[0][1]['X3M_FIXTURE_BOTTLE'], 'X3')
            self.assertIn('--windows', wine[0][0])
            version = self.release.generate.version()
            record = json.loads((Path(out) / f'release-{version}.json').read_text())
            self.assertEqual(record['schema'], 1)
            self.assertEqual(set(record), {'schema', 'version', 'complete', 'commit', 'dirty', 'dirty_files', 'toolchain',
                                           'generate_check', 'dll_warnings', 'x87', 'dll', 'regenerate_source', 'regenerate',
                                           'regenerate_windows_lock', 'zip', 'wall_seconds'})
            self.assertEqual((record['complete'], record['commit'], record['dirty'], record['generate_check']),
                             (True, 'b' * 40, False, 'PASS'))
            self.assertEqual(record['dll']['sha256'], hashlib.sha256(b'MZ fake proxy').hexdigest())
            self.assertEqual(set(record['regenerate']), {'x3m-regenerate', 'x3m-regenerate.exe'})
            self.assertEqual(record['x87']['violations'], 0)
            for step in ('tree', 'config', 'dll', 'regenerate', 'regenerate_host', 'regenerate_windows', 'package', 'total'):
                self.assertIn(step, record['wall_seconds'])
            zip_path = Path(record['zip']['path'])
            self.assertEqual(record['zip']['sha256'], hashlib.sha256(zip_path.read_bytes()).hexdigest())
            with zipfile.ZipFile(zip_path) as archive:
                self.assertEqual(archive.read('d3d9.dll'), b'MZ fake proxy')
            self.assertIn(f'release {version}', text)

    def test_build_warning_fails(self):
        with tempfile.TemporaryDirectory() as out:
            release = self.release.Release(self.release.parse(['--out', out]))

            def run(cmd, **_):
                cmd = [str(c) for c in cmd]
                if '-B' in cmd:
                    Path(cmd[cmd.index('-B') + 1]).mkdir(parents=True)
                    return 0, ''
                return 0, 'src/a.cpp:1:2: warning: unused variable [-Wunused]\n'
            with mock.patch.object(self.release, 'run', side_effect=run):
                with self.assertRaisesRegex(self.release.ReleaseError, '(?s)1 build warning.*unused variable'):
                    quiet(release.dll)
            # a directory that is not a CMake build is never deleted
            with self.assertRaisesRegex(self.release.ReleaseError, 'not a CMake build'):
                release.dll()

    def test_skip_windows_makes_no_zip(self):
        self.package = load('release_package_for_skip', 'tools/release/package.py')
        with tempfile.TemporaryDirectory() as out, \
                mock.patch.object(self.release, 'git', side_effect=lambda *a: {'rev-parse': 'c' * 40, 'status': ''}[a[0]]), \
                mock.patch.object(self.release, 'toolchain', return_value={'mingw_gcc': 'gcc', 'cmake': 'cmake', 'python': '3'}):
            calls = []
            with mock.patch.object(self.release, 'run', side_effect=self.fake_run(out, calls)):
                code, text, _ = quiet(self.release.main, ['--out', out, '--skip-windows'])
            self.assertEqual(code, 0)
            self.assertFalse(any('--windows' in cmd for cmd, _ in calls))
            self.assertFalse(any(Path(cmd[1]).name == 'package.py' for cmd, _ in calls if len(cmd) > 1))
            record = json.loads((Path(out) / f'release-{self.release.generate.version()}.json').read_text())
            self.assertEqual((record['complete'], record['zip']), (False, None))
            self.assertIn('NO release zip', text)

    def test_version_single_source(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        self.assertEqual(len(re.findall(r'^project\([^)]*VERSION', cmake, re.M)), 1)
        self.assertRegex(cmake, r'target_compile_definitions\(d3d9 PRIVATE X3M_VERSION="\$\{PROJECT_VERSION_MAJOR\}\.'
                                r'\$\{PROJECT_VERSION_MINOR\}"\)')
        version = load('generate_for_version', 'tools/config/generate.py').version()
        self.assertRegex(version, r'^\d+\.\d+\.\d+$')
        self.assertEqual(self.release.generate.version(), version)
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertIn('"x3-modern-renderer version=" X3M_VERSION " schema=2', capture)
        self.assertIsNone(re.search(r'x3-modern-renderer version=\d', capture))  # no second literal
        self.assertIn(f'X3 Modern Renderer {version}:', (ROOT / 'assets/x3m.ini').read_text().splitlines()[0])

    def test_build_py_dist_and_lock_holder(self):
        with tempfile.TemporaryDirectory() as base:
            dist = Path(base) / 'dist'
            with mock.patch.object(self.build, 'smoke') as smoke, mock.patch.object(self.build, 'DIST', self.build.DIST):
                self.assertEqual(self.build.main(['--smoke-only', '--dist', str(dist)]), 0)
                smoke.assert_called_once_with(dist.resolve() / 'x3m-regenerate', False)
            lock = Path(base) / 'lock'
            me, parent = os.getpid(), os.getppid()
            table = lambda: {me: (parent, 'python3 build.py'), parent: (1, 'python3 wine_lock.py')}  # noqa: E731
            lock.write_text(f'{parent} 12:00:00 x3m release')
            self.assertTrue(self.build.lock_held_by_ancestor(lock, table))
            lock.write_text('99999999 12:00:00 other runner')
            self.assertFalse(self.build.lock_held_by_ancestor(lock, table))
            lock.write_text('')  # released lock: wine_lock truncates the file
            self.assertFalse(self.build.lock_held_by_ancestor(lock, table))
            self.assertFalse(self.build.lock_held_by_ancestor(Path(base) / 'absent', table))


if __name__ == '__main__':
    sys.exit(unittest.main())
