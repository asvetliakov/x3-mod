"""tools/manage.py and the --weapon-effects option (docs/architecture/weapon-effects.md 5.1): native|observe|shields
exported as X3M_WEAPON_EFFECTS only when given (an inherited value dropped), observe/shields also send
X3M_WEAPON_EFFECTS_TABLE pointing at <game>/x3m/weapon_effects.json, refused under --vanilla, the `weapon effects
table:` dry-run line, and the install/remove wiring (ownership by the generator's `tool` field, removal by uninstall
only). The launch harness is test_manage_engine_effects'. No game launched."""
import hashlib
import importlib.util
import io
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))


def load_manage():
    spec = importlib.util.spec_from_file_location('weapon_effects_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def table(rows=45, sha='be762076cdc38b03'):
    return {'schema': 1, 'tool': 'tools/effects/weapon_effects_table.py',
            'generated_from': {'view': 'installed'}, 'counts': {'bullets': rows},
            'tables': {'bullets': {'rows': rows, 'sha256': sha, 'entries': []}, 'missiles': {'rows': 36}}}


def fake_generator(content, code=0, stderr=''):
    calls = []

    def run(argv, **kwargs):
        calls.append(argv)
        if content is not None:
            Path(argv[argv.index('--out') + 1]).write_text(content)
        return subprocess.CompletedProcess(argv, code, '', stderr)
    run.calls = calls
    return run


class WeaponEffectsLaunchOption(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.module = load_manage()
        cls.directory = tempfile.TemporaryDirectory()
        game = Path(cls.directory.name) / 'game'
        game.mkdir()
        (game / 'X3AP.exe').touch()
        (game / 'd3d9.dll').write_bytes(b'proxy')
        (game / 'x3-modern-install.json').write_text(json.dumps({'sha256': hashlib.sha256(b'proxy').hexdigest()}))
        cls.game = game
        cls.wine = Path(cls.directory.name) / 'wine'
        cls.wine.touch()

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def launch(self, *args, vanilla=False, inherited=None):
        argv = ['manage.py', 'launch', '--dry-run', *(['--vanilla'] if vanilla else []), '--game-dir', str(self.game), *args]
        environ = {k: v for k, v in os.environ.items() if not k.startswith('X3M_')}
        environ.update(inherited or {})
        output, error = io.StringIO(), io.StringIO()
        module = self.module
        with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', self.wine), \
                mock.patch.object(module, 'VOICE_DECODER_REPO', None), mock.patch.dict(module.os.environ, environ, clear=True), \
                mock.patch.object(module.subprocess, 'call', side_effect=AssertionError('must never launch')), \
                __import__('contextlib').redirect_stdout(output), __import__('contextlib').redirect_stderr(error):
            try:
                module.main()
            except SystemExit as exit_error:
                return exit_error.code, None, error.getvalue()
        return 0, json.loads(output.getvalue()), error.getvalue()

    def test_default_sends_nothing_and_drops_an_inherited_value(self):
        for inherited in (None, {'X3M_WEAPON_EFFECTS': 'shields'}):
            code, data, error = self.launch(inherited=inherited)
            self.assertEqual(code, 0, error)
            self.assertNotIn('X3M_WEAPON_EFFECTS', data['env'])
            self.assertNotIn('X3M_WEAPON_EFFECTS_TABLE', data['env'])
            self.assertIsNone(data['weapon_effects_table'])

    def test_observe_and_shields_send_the_variable_and_table_path(self):
        for value in ('observe', 'shields', 'shields,hull', 'shields,hull,bolts'):
            code, data, error = self.launch('--weapon-effects', value, inherited={'X3M_WEAPON_EFFECTS': 'native'})
            self.assertEqual(code, 0, error)
            self.assertEqual(data['env']['X3M_WEAPON_EFFECTS'], value)
            self.assertIn('x3m', data['env']['X3M_WEAPON_EFFECTS_TABLE'].lower())
            self.assertTrue(data['weapon_effects_table'].startswith('weapon effects table: missing ('),
                            data['weapon_effects_table'])
            self.assertIn(data['weapon_effects_table'], error)

    def test_native_sends_the_variable_without_a_table(self):
        code, data, error = self.launch('--weapon-effects', 'native')
        self.assertEqual(code, 0, error)
        self.assertEqual(data['env']['X3M_WEAPON_EFFECTS'], 'native')
        self.assertNotIn('X3M_WEAPON_EFFECTS_TABLE', data['env'])
        self.assertIsNone(data['weapon_effects_table'])

    def test_table_line_ok_and_foreign(self):
        path = self.game / 'x3m/weapon_effects.json'
        try:
            path.parent.mkdir(exist_ok=True)
            path.write_text(json.dumps(table()))
            _, data, _ = self.launch('--weapon-effects', 'shields')
            self.assertEqual(data['weapon_effects_table'],
                             'weapon effects table: ok (45 TBullets rows, table be762076cdc38b03, schema 1)')
            _, data, _ = self.launch('--weapon-effects', 'shields,hull')
            self.assertEqual(data['weapon_effects_table'],
                             'weapon effects table: ok (45 TBullets rows, table be762076cdc38b03, schema 1)')
            path.write_text('{"tool": "something else", "tables": {}}')
            _, data, _ = self.launch('--weapon-effects', 'observe')
            self.assertTrue(data['weapon_effects_table'].startswith('weapon effects table: unreadable'),
                            data['weapon_effects_table'])
        finally:
            path.unlink(missing_ok=True)
            path.parent.rmdir()

    def test_vanilla_refuses(self):
        code, _, error = self.launch('--weapon-effects', 'shields', vanilla=True)
        self.assertEqual(code, 2)
        self.assertIn('--weapon-effects cannot be combined with --vanilla', error)

    def test_hull_list_in_either_order_with_spaces_resolves(self):
        for given in ('shields,hull', 'hull,shields', ' shields , hull', 'hull, shields'):
            code, data, error = self.launch('--weapon-effects', given)
            self.assertEqual(code, 0, error)
            self.assertEqual(data['env']['X3M_WEAPON_EFFECTS'], 'shields,hull', given)
            self.assertEqual(data['weapon_effects'], 'shields,hull', given)
            self.assertIn('weapon effects: shields,hull (X3M_WEAPON_EFFECTS)', error)
        for given in ('shields,hull,bolts', 'bolts,hull,shields', ' shields , hull , bolts '):
            code, data, error = self.launch('--weapon-effects', given)
            self.assertEqual(code, 0, error)
            self.assertEqual(data['env']['X3M_WEAPON_EFFECTS'], 'shields,hull,bolts', given)
            self.assertEqual(data['weapon_effects'], 'shields,hull,bolts', given)
        for bad in ('hull', 'bolts', 'shields,bolts', 'shields,shields', 'shields,hull,bolts,beams', 'shields;hull'):
            code, _, error = self.launch('--weapon-effects', bad)
            self.assertEqual(code, 2, bad)
            self.assertIn('invalid choice', error)

    def test_invalid_choice(self):
        code, _, error = self.launch('--weapon-effects', 'hull')
        self.assertEqual(code, 2)
        self.assertIn("invalid choice: 'hull'", error)


class WeaponEffectsTableInstall(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.module = load_manage()

    def test_install_unchanged_retained_failed_and_remove(self):
        module = self.module
        with tempfile.TemporaryDirectory() as directory:
            game = Path(directory) / 'game'
            game.mkdir()
            target = game / 'x3m/weapon_effects.json'
            text = json.dumps(table(), indent=1)
            first = module.install_weapon_effects(game, run=fake_generator(text))
            self.assertEqual((first['status'], first['rows']), ('installed', 45))
            self.assertEqual(target.read_text(), text)
            self.assertEqual(module.install_weapon_effects(game, run=fake_generator(text))['status'], 'unchanged')
            failed = module.install_weapon_effects(game, run=fake_generator(None, code=1, stderr='weapon_effects_table: boom\n'))
            self.assertEqual((failed['status'], failed['reason']), ('failed', 'generator exit 1: weapon_effects_table: boom'))
            self.assertEqual(target.read_text(), text)
            self.assertEqual(module.remove_weapon_effects(game), {'removed': ['x3m/weapon_effects.json'], 'retained': []})
            self.assertFalse(target.parent.exists())
            target.parent.mkdir()
            target.write_text('{"player": "file"}')
            self.assertEqual(module.install_weapon_effects(game, run=fake_generator(text))['status'], 'retained')
            self.assertEqual(module.remove_weapon_effects(game), {'removed': [], 'retained': ['x3m/weapon_effects.json']})

    def test_action_wiring(self):
        source = (ROOT / 'tools/manage.py').read_text()
        install = source.index("if args.action == 'install':")
        uninstall = source.index("elif args.action == 'uninstall':")
        self.assertLess(source.index('install_weapon_effects(game)', install), uninstall)
        self.assertLess(uninstall, source.index('remove_weapon_effects(game)', uninstall))
        self.assertEqual(source.count('= remove_weapon_effects(game)'), 1)


if __name__ == '__main__':
    unittest.main()
