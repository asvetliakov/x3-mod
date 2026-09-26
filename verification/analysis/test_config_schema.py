"""The configuration file x3m.ini (docs/architecture/config-file.md, steps 1 and 2): the schema tools/config/schema.py is
the one source of every setting, its generated views are current, every variable the DLL reads is a schema name and goes
through the resolver, the template holds exactly the user-facing keys at their defaults, the schema defaults are what the
launcher's default launch sends (so a bare DLL flies the launcher's default configuration), and the DLL's own parser
(src/config/config_parse.h, compiled on the host) reads the grammar as specified. No game, no Wine."""
import contextlib
import hashlib
import importlib.util
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/config'))
import schema  # noqa: E402
import resolve  # noqa: E402

TEMPLATE = ROOT / 'assets/x3m.ini'
# Set from the installation, not from the options (the voice decoder discovery; test_launcher_defaults.INSTALLATION):
# the hermetic launch below finds no decoder, the bottle's dry run sends 1 (compare_dry_runs.py).
INSTALLATION = {'X3M_VOICE_DMO_FALLBACK'}
# A read whose name is formatted at run time: X3M_FOG_MOTES_<field> over renderer::fog_mote_fields (MAX_PX only).
FORMATTED = {'L"X3M_FOG_MOTES_%hs"': {'X3M_FOG_MOTES_MAX_PX'}}
FIXTURE_MACROS = ('X3M_MOTION_OUTPUT_FIXTURE', 'X3M_QUAD_FVF_SWITCH')


def source_files():
    for path in sorted((ROOT / 'src').rglob('*')):
        if path.suffix in ('.cpp', '.h', '.c') and path.is_file():
            yield path


def outside_fixture_blocks(text):
    """The lines of `text` outside #ifdef <fixture macro> blocks (their #else branch counts as outside)."""
    stack, kept = [], []
    for line in text.split('\n'):
        s = line.strip()
        if s.startswith('#if'):
            stack.append(s.startswith('#ifdef') and any(m in s for m in FIXTURE_MACROS))
        elif s.startswith('#else') and stack:
            stack[-1] = False
        elif s.startswith('#endif') and stack:
            stack.pop()
        elif not any(stack):
            kept.append(line)
    return kept


def hermetic_launcher():
    """(manage module, game directory, wine path, TemporaryDirectory) for launch_env; the caller cleans up."""
    spec = importlib.util.spec_from_file_location('config_schema_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec)
    sys.path.insert(0, str(ROOT / 'tools'))
    spec.loader.exec_module(module)
    directory = tempfile.TemporaryDirectory()
    game = Path(directory.name) / 'game'
    game.mkdir()
    (game / 'X3AP.exe').touch()
    (game / 'd3d9.dll').write_bytes(b'proxy')
    (game / 'x3-modern-install.json').write_text(json.dumps({'sha256': hashlib.sha256(b'proxy').hexdigest()}))
    wine = Path(directory.name) / 'wine'
    wine.touch()
    return module, game, wine, directory


def launch_env(module, game, wine, *args):
    """The X3M_* environment of a hermetic `manage.py launch --dry-run` (test_launcher_defaults' harness)."""
    argv = ['manage.py', 'launch', '--dry-run', '--game-dir', str(game), *args]
    environ = {k: v for k, v in os.environ.items() if not k.startswith('X3M_')}
    output = io.StringIO()
    with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', wine), \
            mock.patch.object(module, 'VOICE_DECODER_REPO', None), mock.patch.dict(module.os.environ, environ, clear=True), \
            mock.patch.object(module.subprocess, 'call', side_effect=AssertionError('must never launch')), \
            contextlib.redirect_stdout(output), contextlib.redirect_stderr(io.StringIO()):
        module.main()
    return {k: v for k, v in json.loads(output.getvalue())['env'].items() if k.startswith('X3M_')}


class ConfigSchema(unittest.TestCase):
    def test_generated_views_are_current(self):
        run = subprocess.run([sys.executable, str(ROOT / 'tools/config/generate.py'), '--check'], capture_output=True, text=True, timeout=60)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertIn('PASS', run.stdout)

    def test_template_holds_the_user_facing_keys_at_their_defaults(self):
        text = TEMPLATE.read_text()
        self.assertLess(len(text.encode()), 65536)
        self.assertLess(max(len(line.encode()) for line in text.splitlines()), 512)
        found = {}
        for line in text.splitlines():
            match = re.fullmatch(r';([a-z0-9_]+) =(?: (.*))?', line)
            if match:
                self.assertNotIn(match.group(1), found)
                found[match.group(1)] = match.group(2) or ''
            else:  # every other line is a comment, a section header or blank: nothing is set by the template
                self.assertTrue(not line or line.startswith('; ') or line == ';' or re.fullmatch(r'\[[a-z]+\]', line), line)
        user = {e['key']: schema.shown_value(e) for e in schema.user_facing()}
        self.assertEqual(found, user)
        developer = {e['key'] for e in schema.SETTINGS if e['developer']}
        self.assertFalse(developer & set(found))
        self.assertEqual(re.findall(r'^\[([a-z]+)\]$', text, re.M), [s for s in schema.SECTIONS if any(e['section'] == s for e in schema.user_facing())])

    def test_schema_shape(self):
        self.assertEqual(len(schema.BY_KEY), len(schema.SETTINGS))
        for e in schema.SETTINGS:
            self.assertEqual(e['env'], 'X3M_' + e['key'].upper())
            if e['env'].startswith('X3M_FIXTURE_') or e['env'].endswith('_DEFAULT') or e['key'] == 'config':
                self.assertTrue(e['env_only'] and e['developer'], e['key'])
            if e['env'].endswith('_DEFAULT'):
                self.assertIsNotNone(e['marker_of'], e['key'])
                self.assertEqual(e['env'], schema.BY_KEY[e['marker_of']]['env'] + '_DEFAULT')
            self.assertIsNone(e['off'], e['key'])  # bare = the pre-config "absent" behaviour for every entry

    def test_bare_dll_equals_the_launcher_default_flight(self):
        """Step 2: every variable the default launch sends is a schema key at its default (or the launcher's own
        X3M_CONFIG), and every schema default is sent: with no environment and no file the DLL resolves the same values."""
        module, game, wine, directory = hermetic_launcher()
        with directory:
            sent = launch_env(module, game, wine)
            with_file = launch_env(module, game, wine, '--config')
            with_path = launch_env(module, game, wine, '--config', '/tmp/x3m-test.ini')
        for name, value in sent.items():
            if name in schema.LAUNCHER_ONLY:
                self.assertEqual(value, schema.LAUNCHER_ONLY[name], name)
                continue
            self.assertIn(name, schema.BY_ENV, name)
            self.assertEqual(value, schema.BY_ENV[name]['default'], name)
        defaults = {e['env']: e['default'] for e in schema.SETTINGS if e['default'] is not None}
        self.assertEqual(set(defaults) - set(sent), INSTALLATION)
        self.assertEqual(defaults['X3M_VOICE_DMO_FALLBACK'], '1')
        self.assertEqual(sent['X3M_CONFIG'], 'bare')
        # Player mode (--config): nothing but the file selection; the DLL's defaults stand in for the launcher's values.
        self.assertEqual(with_file, {})
        self.assertEqual(with_path, {'X3M_CONFIG': 'Z:/tmp/x3m-test.ini'})
        # The bare DLL (no environment, no file) resolves what the launcher's default flight resolves.
        flight = {k: v for k, (v, _) in resolve.resolve(sent).items() if k != 'X3M_CONFIG'}
        bare_dll = {k: v for k, (v, _) in resolve.resolve({}).items() if k not in INSTALLATION}
        self.assertEqual(flight, bare_dll)
        self.assertEqual({s for _, s in resolve.resolve(sent).values()}, {'env'})  # bare: nothing from the defaults

    def test_launcher_fields_are_registered_options(self):
        # (see also OptOuts below: the resolved values of every opt-out, default launch and player mode)
        registered = set(re.findall(r"add_argument\('(--[a-z0-9-]+)'", (ROOT / 'tools/manage.py').read_text()))
        for e in schema.SETTINGS:
            if e['launcher']:
                self.assertIn(e['launcher'], registered, e['key'])

    def test_every_dll_read_is_a_schema_name_and_goes_through_the_resolver(self):
        names, direct = set(), []
        for path in source_files():
            text = path.read_text(errors='replace')
            names |= set(re.findall(r'L?"(X3M_[A-Z0-9_]+)"', text))
            for literal, expanded in FORMATTED.items():
                if literal in text:
                    names |= expanded
            if path.relative_to(ROOT).as_posix() == 'src/proxy/config.cpp':
                continue
            for line in outside_fixture_blocks(text):
                if re.search(r'GetEnvironmentVariable[AW]?\(\s*L?"X3M_', line):
                    direct.append(f'{path.relative_to(ROOT)}: {line.strip()[:100]}')
        self.assertTrue(names)
        self.assertEqual(sorted(names - set(schema.BY_ENV)), [])
        self.assertEqual(direct, [])
        self.assertEqual(sorted(set(schema.BY_ENV) - names), [])  # no schema entry the DLL no longer reads

    def test_resolver_is_first_in_initialize_log(self):
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        body = capture[capture.index('void initialize_log(HMODULE module) {'):]
        self.assertLess(body.index('config::load(module);'), body.index('log_tier::init();'))
        self.assertLess(body.index('proxy_identity::log_identity(module);'), body.index('config::log_rows();'))


OFF = (None, '0', 'off', 'vanilla', 'native', 'keep', 'legacy', 'identity')
# Opt-outs and the variables that must end up off (docs/architecture/config-file.md; the reviewer's list of 2026-09-26 and
# test_launcher_defaults.OPT_OUTS).
OPT_OUTS = {
    ('--no-sun-occlusion',): ('X3M_SUN_OCCLUSION',), ('--no-music-keep',): ('X3M_MUSIC_KEEP',),
    ('--cull-small-parts', '0'): ('X3M_CULL_SMALL_PARTS_PX',), ('--no-shadow-cascades',): ('X3M_SHADOW_CASCADES', 'X3M_VOLUMETRIC_FOG'),
    ('--no-hdr',): ('X3M_HDR', 'X3M_HDR_BLOOM', 'X3M_HDR_TONEMAP'), ('--no-taa',): ('X3M_TAA', 'X3M_VOLUMETRIC_FOG', 'X3M_SUN_SHADOW_LANE'),
    ('--no-ownership',): ('X3M_OWNERSHIP', 'X3M_TAA'), ('--no-motion-output',): ('X3M_MOTION_OUTPUT', 'X3M_TAA', 'X3M_HDR'),
    ('--no-volumetric-fog',): ('X3M_VOLUMETRIC_FOG',), ('--camera', 'vanilla'): ('X3M_CAMERA', 'X3M_CHASE_VIEW_RESTORE'),
    ('--no-crypt-cache',): ('X3M_CRYPT_CACHE',), ('--no-bloom-source-clamp',): ('X3M_BLOOM_SOURCE_CLAMP',),
    ('--no-screen-emission-additive-alpha',): ('X3M_SCREEN_EMISSION_ADDITIVE_ALPHA',),
    ('--no-shadow-cascade-adaptive-c0',): ('X3M_SHADOW_CASCADE_ADAPTIVE_C0',), ('--no-pause-key-only',): ('X3M_PAUSE_KEY_ONLY',),
    ('--no-window-monitor-rect',): ('X3M_WINDOW_MONITOR_RECT',), ('--no-light-map-far-fade',): ('X3M_LIGHT_MAP_FAR_FADE',),
}


class OptOuts(unittest.TestCase):
    """Every opt-out resolves to off in the DLL, on a default launch (X3M_CONFIG=bare: no defaults fill what the launcher
    leaves out) and in player mode (--config, no file: the defaults fill in, the opted-out variables are sent off or
    empty); player mode with no file resolves exactly what the default launch resolves."""
    @classmethod
    def setUpClass(cls):
        cls.module, cls.game, cls.wine, cls.directory = hermetic_launcher()

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def test_opt_outs(self):
        for args, names in OPT_OUTS.items():
            developer = launch_env(self.module, self.game, self.wine, *args)
            player = launch_env(self.module, self.game, self.wine, '--config', *args)
            self.assertEqual(developer['X3M_CONFIG'], 'bare', args)
            dev_resolved, player_resolved = resolve.resolve(developer), resolve.resolve(player)
            self.assertEqual({s for _, s in dev_resolved.values()}, {'env'}, args)  # nothing picked up from the defaults
            for name in names:
                self.assertIn(resolve.value(dev_resolved, name), OFF, (args, name))
                self.assertIn(resolve.value(player_resolved, name), OFF, (args, name, player.get(name)))
            # Player mode without a file == the developer launch, setting by setting (an empty value counts as unset).
            effective = lambda resolved: {k: resolve.value(resolved, k) for k in resolved if k not in ('X3M_CONFIG', *INSTALLATION)}
            a, b = effective(dev_resolved), effective(player_resolved)
            diff = {k: (a.get(k), b.get(k)) for k in set(a) | set(b) if a.get(k) != b.get(k)}
            self.assertEqual(diff, {}, args)


class ConfigParser(unittest.TestCase):
    """The DLL's parser (src/config/config_parse.h) on the host."""
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('clang++') or shutil.which('c++')
        if compiler is None:
            raise unittest.SkipTest('A host C++ compiler is required')
        cls.directory = tempfile.TemporaryDirectory(prefix='x3-config-parse-')
        cls.executable = Path(cls.directory.name) / 'config_parse_host'
        build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', str(ROOT / 'verification/probe/config_parse_host.cpp'),
                                '-o', str(cls.executable)], capture_output=True, text=True, timeout=120)
        if build.returncode:
            raise AssertionError(build.stdout + build.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def parse(self, data):
        path = Path(self.directory.name) / 'x3m.ini'
        path.write_bytes(data if isinstance(data, bytes) else data.encode())
        run = subprocess.run([str(self.executable), str(path)], capture_output=True, text=True, timeout=30)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        lines = run.stdout.splitlines()
        if lines == ['refused size']:
            return {'refused': True}
        counts = dict(field.split('=') for field in lines[0].split()[1:])
        values = {m.group(1): m.group(3) for m in (re.fullmatch(r'value (\S+) line=(\d+) \[(.*)\]', l) for l in lines) if m}
        issues = [re.fullmatch(r'issue line=(\d+) problem=(\S+) key=\[(.*)\] value=\[(.*)\]', l).groups() for l in lines if l.startswith('issue ')]
        return {'counts': {k: int(v) for k, v in counts.items()}, 'values': values, 'issues': issues}

    def test_the_whole_template_uncommented_is_accepted(self):
        text = re.sub(r'^;([a-z])', r'\1', TEMPLATE.read_text(), flags=re.M)
        result = self.parse(text)
        user = schema.user_facing()
        self.assertEqual(result['counts']['keys'], len(user))
        self.assertEqual({k: v for k, v in result['counts'].items() if k not in ('keys', 'lines')},
                         {'unknown': 0, 'invalid': 0, 'duplicate': 0, 'env_only': 0, 'renamed': 0, 'more': 0})
        self.assertEqual(result['values'], {e['key']: schema.shown_value(e) for e in user})

    def test_grammar(self):
        text = ('\ufeff# comment\r\n  ; indented comment\r\n[graphics]\r\n'
                'TAA-Sharpen = 0.5\r\n'                 # 4: '-' and case folded
                'taa = Off\r\n'                         # 5: bool word normalised
                'shadow_cascades = 250, 1500 ,7500\r\n'  # 6: blanks around commas removed
                'camera = Chase\r\n'                    # 7: enums are exact: invalid
                'bogus_key = 1\r\n'                     # 8: unknown
                'fixture_exception = 1\r\n'             # 9: env only
                'taa_sharpen = 0.25\r\n'                # 10: duplicate, last wins
                'fov = game\r\n'                        # 11: a number's extra word
                'no equals sign\r\n'                    # 12: syntax
                '[bad\r\n'                              # 13: syntax
                'log_file = "C:\\my path\\x.log"\r\n'   # 14: quotes stripped, no escapes
                'hdr_ev = 1e1\r\n'                      # 15: exponent
                'hdr_ev_min = 17\r\n'                   # 16: out of range
                'capture_frames = 3.5\r\n'              # 17: an int
                'taa_mip_bias = -0.25 ; note\r\n'       # 18: no trailing comments: invalid
                'taa_far_clip_default = 1\r\n'          # 19: a marker is environment only
                f'motion_jitter = {"1" * 600}\r\n'      # 20: over 512 bytes
                'hdr_ev_manual =\r\n'                   # 21: empty = its default
                'pause_key =\r\n'                       # 22: empty string: the site's default
                'hdr_tonemap =\r\n'                     # 23: empty: invalid
                'shadow_cascade_sizes = 2048,4096,4096,4096,2048,1024\r\n'  # 24: six maps: invalid
                'gz_buffer_kb = 512')                   # 25: a developer key, no final newline
        r = self.parse(text.encode())
        self.assertEqual(r['values'], {'taa_sharpen': '0.25', 'taa': '0', 'shadow_cascades': '250,1500,7500', 'fov': 'game',
                                       'log_file': 'C:\\my path\\x.log', 'hdr_ev': '1e1', 'hdr_ev_manual': '', 'pause_key': '',
                                       'gz_buffer_kb': '512'})
        self.assertEqual(r['counts'], {'keys': 9, 'unknown': 1, 'invalid': 9, 'duplicate': 1, 'env_only': 2, 'renamed': 0, 'lines': 25,
                                       'more': 0})
        problems = [(int(line), problem, key) for line, problem, key, _ in r['issues']]
        self.assertEqual(problems, [(7, 'invalid', 'camera'), (8, 'unknown', 'bogus_key'), (9, 'env_only', 'fixture_exception'),
                                    (10, 'duplicate', 'taa_sharpen'), (12, 'syntax', ''), (13, 'syntax', ''), (16, 'invalid', 'hdr_ev_min'),
                                    (17, 'invalid', 'capture_frames'), (18, 'invalid', 'taa_mip_bias'),
                                    (19, 'env_only', 'taa_far_clip_default'), (20, 'too_long', ''), (23, 'invalid', 'hdr_tonemap'),
                                    (24, 'invalid', 'shadow_cascade_sizes')])

    def test_issue_rows_are_bounded(self):
        r = self.parse(''.join(f'unknown_{i} = 1\n' for i in range(40)))
        self.assertEqual((r['counts']['unknown'], len(r['issues']), r['counts']['more']), (40, 32, 8))

    def test_file_bound(self):
        self.assertEqual(self.parse(b';' * 65537), {'refused': True})
        self.assertEqual(self.parse(b';' * 65535 + b'\n')['counts']['lines'], 1)

    def resolved(self, text, profile, env, names):
        path = Path(self.directory.name) / 'resolve.ini'
        path.write_text(text)
        run = subprocess.run([str(self.executable), 'resolve', str(path), profile, *(f'{k}={v}' for k, v in env.items()), '--', *names],
                             capture_output=True, text=True, timeout=30)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        out = {}
        for line in run.stdout.splitlines():
            name, _, rest = line.partition('=') if '=' in line.split(' ')[0] else (line.split(' ')[0], '', None)
            out[name] = None if rest is None else tuple(rest.rsplit('@', 1))
        # The Python mirror (tools/config/resolve.py, used by the launcher checks) agrees entry by entry.
        file_values = {} if profile in ('none', 'bare') else {k: v for k, v in self.parse(text)['values'].items() for k in ['X3M_' + k.upper()]}
        mirror = resolve.resolve({**env, **({'X3M_CONFIG': profile} if profile in ('none', 'bare') else {})}, file_values)
        for name in names:
            self.assertEqual(out[name], mirror.get(name), name)
        return out

    def test_resolver_layers(self):
        """config::get's layers with the DLL's below_environment (host harness) and the Python mirror."""
        text = 'music_keep = on\ncamera = vanilla\nshadow_cascades = 300, 1600\nframe_end_stride = 7\n'
        names = ('X3M_MUSIC_KEEP', 'X3M_CAMERA', 'X3M_SHADOW_CASCADES', 'X3M_TAA_SHARPEN', 'X3M_FRAME_END_STRIDE', 'X3M_NOT_A_SETTING')
        out = self.resolved(text, 'file', {'X3M_MUSIC_KEEP': '0', 'X3M_FRAME_END_STRIDE': ''}, names)
        self.assertEqual(out['X3M_MUSIC_KEEP'], ('0', 'env'))               # a bool: the environment's 0 over the file's 1
        self.assertEqual(out['X3M_CAMERA'], ('vanilla', 'file'))            # an enum from the file
        self.assertEqual(out['X3M_SHADOW_CASCADES'], ('300,1600', 'file'))  # a list from the file, blanks removed
        self.assertEqual(out['X3M_TAA_SHARPEN'], ('0.75', 'default'))       # a float from the default
        self.assertEqual(out['X3M_FRAME_END_STRIDE'], ('', 'env'))          # set empty: beats the file, the site reads it as unset
        self.assertIsNone(out['X3M_NOT_A_SETTING'])
        marker = ('X3M_TAA_FAR_CLIP', 'X3M_TAA_FAR_CLIP_DEFAULT')
        self.assertEqual(self.resolved('', 'file', {}, marker),                              # (a) no file: both defaults
                         {'X3M_TAA_FAR_CLIP': ('7x7', 'default'), 'X3M_TAA_FAR_CLIP_DEFAULT': ('1', 'default')})
        self.assertEqual(self.resolved('taa_far_clip = 3x3\n', 'file', {}, marker),          # (b) the file sets the key
                         {'X3M_TAA_FAR_CLIP': ('3x3', 'file'), 'X3M_TAA_FAR_CLIP_DEFAULT': None})
        self.assertEqual(self.resolved('', 'file', {'X3M_TAA_FAR_CLIP': '7x7'}, marker),      # (c) the environment sets it
                         {'X3M_TAA_FAR_CLIP': ('7x7', 'env'), 'X3M_TAA_FAR_CLIP_DEFAULT': None})
        self.assertEqual(self.resolved('taa_far_clip = 3x3\n', 'bare', {}, marker),          # (d) bare: nothing
                         {'X3M_TAA_FAR_CLIP': None, 'X3M_TAA_FAR_CLIP_DEFAULT': None})
        self.assertEqual(self.resolved('camera = vanilla\n', 'none', {}, ('X3M_CAMERA',)), {'X3M_CAMERA': ('chase', 'default')})

    def test_nul_byte_refuses_the_line(self):
        r = self.parse(b'taa_sharpen = 0.5\x00junk\ncamera = vanilla\n; a comment\x00\n')
        self.assertEqual(r['values'], {'camera': 'vanilla'})
        self.assertEqual([(int(line), problem) for line, problem, _, _ in r['issues']], [(1, 'nul'), (3, 'nul')])
        self.assertEqual(r['counts']['invalid'], 2)

    def test_ranges_are_the_sites(self):
        text = ''.join(f'{k} = {v}\n' for k, v in (
            ('hdr_key', '0'), ('fog_motes_max_px', '1'), ('fog_motes_max_px', '65'), ('bloom_source_clamp', '0'),
            ('screen_emission_additive', '0.5'), ('shadow_cascades', '10,20'), ('fog_dust_motes', '32,3,128'),
            ('taa_far_stabiliser', '0.985,0,60'), ('bolt_footprint', '3,300')))
        self.assertEqual(self.parse(text)['counts']['invalid'], 9)
        text = ''.join(f'{k} = {v}\n' for k, v in (
            ('hdr_key', '0.001'), ('fog_motes_max_px', '2'), ('bloom_source_clamp', '64'), ('screen_emission_additive', '0'),
            ('shadow_cascades', '0'), ('fog_dust_motes', '0,3,128'), ('taa_far_stabiliser', '0'), ('bolt_footprint', '0'),
            ('shadow_cascade_sizes', '64'), ('capture_frames', '100')))
        r = self.parse(text)
        self.assertEqual((r['counts']['invalid'], r['counts']['keys']), (0, 10))

    def test_table_lookup(self):
        for env, expected in (('X3M_TAA', True), ('X3M_TAA_SHARPEN', True), ('X3M_NOPE', False), ('X3M_', False), ('LOCALAPPDATA', False)):
            run = subprocess.run([str(self.executable), 'find', env], capture_output=True, text=True, timeout=30)
            self.assertEqual(int(run.stdout.split()[-1]) >= 0, expected, env)


if __name__ == '__main__':
    unittest.main()
