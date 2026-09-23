"""Host tests of the --voice-decoder delivery and discovery in tools/manage.py.

A chosen decoder (explicit DIR, or discovered on a modded launch) must reach
the launched process with exactly the two versioned GStreamer variables of
docs/architecture/voice-decoder-adapter.md, create only its registry
directory, and an invalid explicit DIR is refused. Without a chosen decoder
the launch environment carries no decoder variable. Dry runs, or a mocked
launch_teed: no game, no Wine, no bottle or application write.
"""
import contextlib
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
FORBIDDEN = ('DYLD_LIBRARY_PATH', 'GST_PLUGIN_PATH', 'GST_REGISTRY', 'GST_PLUGIN_SYSTEM_PATH')


def load_manage():
    spec = importlib.util.spec_from_file_location('voice_decoder_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class VoiceDecoderLaunchOption(unittest.TestCase):
    def plugin_tree(self, directory, *, plugin=True, libs=True):
        root = Path(directory) / 'wma-plugin'
        (root / 'runtime/plugins').mkdir(parents=True)
        if plugin:
            (root / 'runtime/plugins/libgstlibav.dylib').touch()
        if libs:
            (root / 'runtime/lib').mkdir(parents=True)
        return root

    def launch(self, directory, *args):
        module = load_manage()
        game = Path(directory) / 'game'
        game.mkdir(exist_ok=True)
        (game / 'X3AP.exe').touch()
        wine = Path(directory) / 'wine'
        wine.touch()
        argv = ['manage.py', 'launch', '--dry-run', '--vanilla', '--game-dir', str(game), *args]
        output, error = io.StringIO(), io.StringIO()
        with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', wine), \
                mock.patch.object(module.subprocess, 'call', side_effect=AssertionError('must never launch')), \
                contextlib.redirect_stdout(output), contextlib.redirect_stderr(error):
            try:
                module.main()
            except SystemExit as exit_error:
                return exit_error.code, output.getvalue(), error.getvalue()
        return 0, output.getvalue(), error.getvalue()

    def test_absent_option_leaves_the_launch_environment_unchanged(self):
        with tempfile.TemporaryDirectory() as directory:
            code, output, _ = self.launch(directory)
            self.assertEqual(code, 0)
            environment = json.loads(output)['env']
            self.assertTrue(all(name.startswith('X3M_') for name in environment), environment)

    def test_the_two_versioned_variables_and_nothing_else_are_delivered(self):
        with tempfile.TemporaryDirectory() as directory:
            root = self.plugin_tree(directory)
            baseline = json.loads(self.launch(directory)[1])
            code, output, error = self.launch(directory, '--voice-decoder', str(root))
            self.assertEqual(code, 0, error)
            delivered = json.loads(output)
            self.assertEqual(delivered['command'], baseline['command'])
            added = {k: v for k, v in delivered['env'].items() if k not in baseline['env']}
            self.assertEqual(added, {'GST_PLUGIN_PATH_1_0': str(root / 'runtime/plugins'),
                                     'GST_REGISTRY_1_0': str(root / 'registry/x3-arm64.bin'),
                                     'X3M_VOICE_DMO_FALLBACK': '1'})
            self.assertEqual({k: v for k, v in delivered['env'].items() if k in baseline['env']}, baseline['env'])
            for name in FORBIDDEN:
                self.assertNotIn(name, delivered['env'])
            # Only the registry directory is created, nothing else.
            self.assertTrue((root / 'registry').is_dir())
            self.assertEqual(sorted(p.name for p in root.iterdir()), ['registry', 'runtime'])
            self.assertEqual(sorted(p.name for p in (root / 'registry').iterdir()), [])

    def test_an_existing_registry_directory_is_accepted(self):
        with tempfile.TemporaryDirectory() as directory:
            root = self.plugin_tree(directory)
            (root / 'registry').mkdir()
            (root / 'registry/x3-arm64.bin').write_bytes(b'stale')
            code, output, error = self.launch(directory, '--voice-decoder', str(root))
            self.assertEqual(code, 0, error)
            self.assertEqual(json.loads(output)['env']['GST_REGISTRY_1_0'], str(root / 'registry/x3-arm64.bin'))
            self.assertEqual((root / 'registry/x3-arm64.bin').read_bytes(), b'stale')

    def test_an_invalid_directory_is_refused_with_a_message(self):
        with tempfile.TemporaryDirectory() as directory:
            cases = {'missing': Path(directory) / 'does-not-exist',
                     'no plugin': self.plugin_tree(tempfile.mkdtemp(dir=directory), plugin=False),
                     'no lib': self.plugin_tree(tempfile.mkdtemp(dir=directory), libs=False)}
            for label, root in cases.items():
                with self.subTest(case=label):
                    code, _, error = self.launch(directory, '--voice-decoder', str(root))
                    self.assertEqual(code, 2)
                    self.assertIn('--voice-decoder', error)
            blocked = self.plugin_tree(tempfile.mkdtemp(dir=directory))
            os.chmod(blocked, 0o555)
            try:
                code, _, error = self.launch(directory, '--voice-decoder', str(blocked))
            finally:
                os.chmod(blocked, 0o755)
            self.assertEqual(code, 2)
            self.assertIn('registry', error)

    def test_the_option_is_launch_only(self):
        with tempfile.TemporaryDirectory() as directory:
            root = self.plugin_tree(directory)
            module = load_manage()
            error = io.StringIO()
            with mock.patch.object(sys, 'argv', ['manage.py', 'status', '--voice-decoder', str(root)]), \
                    contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(error):
                with self.assertRaises(SystemExit) as raised:
                    module.main()
            self.assertEqual(raised.exception.code, 2)
            self.assertIn('launch only', error.getvalue())



class VoiceDecoderDiscovery(unittest.TestCase):
    """Without --voice-decoder a modded launch takes <game>/x3m/voice-decoder,
    then the repository copy; `none` opts out; --vanilla never discovers."""

    def tree(self, root, *, registry=True, plugin=True):
        (root / 'runtime/plugins').mkdir(parents=True)
        (root / 'runtime/lib').mkdir(parents=True)
        if plugin:
            (root / 'runtime/plugins/libgstlibav.dylib').touch()
        if registry:
            (root / 'registry').mkdir()
        return root.resolve()  # the launcher resolves --game-dir (/var -> /private/var)

    def launch(self, directory, *args, vanilla=False, environ=None, dry_run=True, seen=None):
        module = load_manage()
        base = Path(directory).resolve()
        game = base / 'game'
        game.mkdir(exist_ok=True)
        (game / 'X3AP.exe').touch()
        if not vanilla:
            (game / 'd3d9.dll').write_bytes(b'proxy')
            (game / 'x3-modern-install.json').write_text(json.dumps({'sha256': hashlib.sha256(b'proxy').hexdigest()}))
        wine = base / 'wine'
        wine.touch()
        argv = ['manage.py', 'launch', *(['--dry-run'] if dry_run else []), *(['--vanilla'] if vanilla else []),
                '--game-dir', str(game), *args]
        output, error = io.StringIO(), io.StringIO()

        def fake_launch(command, env, cwd, log_path, **kwargs):
            seen.update(env)
            return 0

        with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', wine), \
                mock.patch.object(module, 'VOICE_DECODER_REPO', base / 'repo/v4'), \
                mock.patch.object(module, 'launch_teed', fake_launch), \
                mock.patch.dict(module.os.environ, {}, clear=False), \
                mock.patch.object(module.subprocess, 'call', side_effect=AssertionError('must never launch')), \
                contextlib.redirect_stdout(output), contextlib.redirect_stderr(error):
            for name in ('X3M_VOICE_DECODER_REPO', 'X3M_VOICE_DMO_FALLBACK', 'GST_PLUGIN_PATH_1_0', 'GST_REGISTRY_1_0'):
                module.os.environ.pop(name, None)
            module.os.environ.update(environ or {})
            try:
                module.main()
            except SystemExit as exit_error:
                return exit_error.code, output.getvalue(), error.getvalue()
        return 0, output.getvalue(), error.getvalue()

    def chosen(self, output):
        return json.loads(output)['env'].get('GST_PLUGIN_PATH_1_0')

    def test_game_directory_wins_over_the_repository_copy(self):
        with tempfile.TemporaryDirectory() as directory:
            game_copy = self.tree(Path(directory) / 'game/x3m/voice-decoder')
            self.tree(Path(directory) / 'repo/v4')
            code, output, error = self.launch(directory)
            self.assertEqual(code, 0, error)
            self.assertEqual(self.chosen(output), str(game_copy / 'runtime/plugins'))
            self.assertEqual(json.loads(output)['env']['GST_REGISTRY_1_0'], str(game_copy / 'registry/x3-arm64.bin'))
            self.assertEqual(json.loads(output)['env']['X3M_VOICE_DMO_FALLBACK'], '1')
            line = f'voice decoder: {game_copy} (discovered: game directory)'
            self.assertIn(line, error.splitlines())
            self.assertEqual(json.loads(output)['voice_decoder'], line)

    def test_repository_copy_is_the_fallback(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = self.tree(Path(directory) / 'repo/v4')
            code, output, error = self.launch(directory)
            self.assertEqual(code, 0, error)
            self.assertEqual(self.chosen(output), str(repo / 'runtime/plugins'))
            self.assertIn(f'voice decoder: {repo} (discovered: repository copy)', error.splitlines())
            self.assertNotIn('skipping', error)

    def test_an_invalid_discovered_directory_falls_through_with_a_note(self):
        with tempfile.TemporaryDirectory() as directory:
            game_copy = self.tree(Path(directory) / 'game/x3m/voice-decoder', plugin=False, registry=False)
            repo = self.tree(Path(directory) / 'repo/v4')
            code, output, error = self.launch(directory)
            self.assertEqual(code, 0, error)
            self.assertEqual(self.chosen(output), str(repo / 'runtime/plugins'))
            self.assertIn(f'voice decoder: skipping game directory {game_copy}:', error)
            self.assertFalse((game_copy / 'registry').exists())  # a rejected candidate is never written to

    def test_a_dry_run_reports_but_does_not_create_a_discovered_registry(self):
        with tempfile.TemporaryDirectory() as directory:
            game_copy = self.tree(Path(directory) / 'game/x3m/voice-decoder', registry=False)
            broken = self.tree(Path(directory) / 'repo/v4', registry=False, plugin=False)
            code, output, error = self.launch(directory)
            self.assertEqual(code, 0, error)
            self.assertEqual(self.chosen(output), str(game_copy / 'runtime/plugins'))
            self.assertIn(f'voice decoder: {game_copy} (discovered: game directory; registry will be created)',
                          error.splitlines())
            self.assertFalse((game_copy / 'registry').exists())
            self.assertFalse((broken / 'registry').exists())

    def test_a_launch_creates_the_discovered_registry(self):
        with tempfile.TemporaryDirectory() as directory:
            game_copy = self.tree(Path(directory) / 'game/x3m/voice-decoder', registry=False)
            seen = {}
            code, _, error = self.launch(directory, dry_run=False, seen=seen)
            self.assertEqual(code, 0, error)
            self.assertEqual(seen['GST_PLUGIN_PATH_1_0'], str(game_copy / 'runtime/plugins'))
            self.assertTrue((game_copy / 'registry').is_dir())
            self.assertEqual(list((game_copy / 'registry').iterdir()), [])

    def test_stale_decoder_variables_are_stripped_without_a_decoder(self):
        stale = {'X3M_VOICE_DMO_FALLBACK': '1', 'GST_PLUGIN_PATH_1_0': '/stale/plugins',
                 'GST_REGISTRY_1_0': '/stale/registry.bin'}
        for label, extra in (('none', ('--voice-decoder', 'none')), ('nothing found', ())):
            with self.subTest(case=label), tempfile.TemporaryDirectory() as directory:
                seen = {}
                code, _, error = self.launch(directory, *extra, environ=stale, dry_run=False, seen=seen)
                self.assertEqual(code, 0, error)
                self.assertIn('voice decoder: none', error)
                for name in stale:
                    self.assertNotIn(name, seen)

    def test_the_word_none_is_literal_and_dot_none_is_a_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            named = self.tree(Path(directory) / 'none')
            previous = os.getcwd()
            os.chdir(directory)
            try:
                code, output, error = self.launch(directory, '--voice-decoder', './none')
            finally:
                os.chdir(previous)
            self.assertEqual(code, 0, error)
            self.assertEqual(os.path.realpath(self.chosen(output)), str(named / 'runtime/plugins'))
            self.assertIn('(explicit --voice-decoder)', error)

    def test_the_repository_override_is_consumed_and_not_forwarded(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = self.tree(Path(directory) / 'repo/v4')
            code, output, error = self.launch(directory, environ={'X3M_VOICE_DECODER_REPO': ''})
            self.assertEqual(code, 0, error)
            self.assertIn('voice decoder: none (no valid plugin directory found)', error.splitlines())
            self.assertNotIn('X3M_VOICE_DECODER_REPO', json.loads(output)['env'])
            seen = {}
            code, _, error = self.launch(directory, environ={'X3M_VOICE_DECODER_REPO': str(repo)}, dry_run=False, seen=seen)
            self.assertEqual(code, 0, error)
            self.assertEqual(seen['GST_PLUGIN_PATH_1_0'], str(repo / 'runtime/plugins'))
            self.assertNotIn('X3M_VOICE_DECODER_REPO', seen)

    def test_none_found_delivers_nothing(self):
        with tempfile.TemporaryDirectory() as directory:
            self.tree(Path(directory) / 'repo/v4', plugin=False)
            code, output, error = self.launch(directory)
            self.assertEqual(code, 0, error)
            environment = json.loads(output)['env']
            for name in ('GST_PLUGIN_PATH_1_0', 'GST_REGISTRY_1_0', 'X3M_VOICE_DMO_FALLBACK'):
                self.assertNotIn(name, environment)
            self.assertIn('voice decoder: none (no valid plugin directory found)', error.splitlines())
            self.assertIn('voice decoder: skipping repository copy', error)

    def test_none_opts_out_even_when_candidates_are_valid(self):
        with tempfile.TemporaryDirectory() as directory:
            self.tree(Path(directory) / 'game/x3m/voice-decoder')
            self.tree(Path(directory) / 'repo/v4')
            code, output, error = self.launch(directory, '--voice-decoder', 'none')
            self.assertEqual(code, 0, error)
            self.assertNotIn('GST_PLUGIN_PATH_1_0', json.loads(output)['env'])
            self.assertNotIn('X3M_VOICE_DMO_FALLBACK', json.loads(output)['env'])
            self.assertIn('voice decoder: none (--voice-decoder none)', error.splitlines())

    def test_vanilla_does_not_discover(self):
        with tempfile.TemporaryDirectory() as directory:
            self.tree(Path(directory) / 'game/x3m/voice-decoder')
            self.tree(Path(directory) / 'repo/v4')
            code, output, error = self.launch(directory, vanilla=True)
            self.assertEqual(code, 0, error)
            self.assertNotIn('GST_PLUGIN_PATH_1_0', json.loads(output)['env'])
            self.assertIn('voice decoder: none (--vanilla: no discovery)', error.splitlines())

    def test_explicit_directory_wins_and_its_failure_stays_fatal(self):
        with tempfile.TemporaryDirectory() as directory:
            self.tree(Path(directory) / 'game/x3m/voice-decoder')
            self.tree(Path(directory) / 'repo/v4')
            explicit = self.tree(Path(directory) / 'explicit', registry=False)
            code, output, error = self.launch(directory, '--voice-decoder', str(explicit))
            self.assertEqual(code, 0, error)
            self.assertEqual(self.chosen(output), str(explicit / 'runtime/plugins'))
            self.assertTrue((explicit / 'registry').is_dir())  # an explicit DIR still gets its registry created
            self.assertIn(f'voice decoder: {explicit} (explicit --voice-decoder)', error.splitlines())
            broken = self.tree(Path(directory) / 'broken', plugin=False)
            code, _, error = self.launch(directory, '--voice-decoder', str(broken))
            self.assertEqual(code, 2)
            self.assertIn('--voice-decoder', error)

    def test_the_shipped_copy_is_deliverable_and_matches_its_hash_list(self):
        module = load_manage()
        shipped = ROOT / 'tools/voice-decoder/v4'
        self.assertEqual(module.VOICE_DECODER_REPO, shipped)
        self.assertTrue((shipped / 'runtime/plugins/libgstlibav.dylib').is_file())
        self.assertTrue((shipped / 'runtime/lib').is_dir())
        self.assertIn('tools/voice-decoder/v4/registry/', (ROOT / '.gitignore').read_text().splitlines())  # cache and temp files never tracked
        checked = 0
        for row in (shipped / 'artifact-sha256.txt').read_text().splitlines():
            digest, name = row.split(maxsplit=1)
            if name.startswith('src/'):  # the patched build-tree source is not shipped
                continue
            self.assertEqual(hashlib.sha256((shipped / name).read_bytes()).hexdigest(), digest, name)
            checked += 1
        self.assertEqual(checked, 7)


if __name__ == '__main__':
    unittest.main()
