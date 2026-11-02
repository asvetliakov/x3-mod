"""Host checks of the own sun's flare-probe hook (docs/architecture/own-sun-pass.md sections 5, 6 and 14, step 2).

The probe's eligibility test (the main view's re-probe of a background-view-owned record, run223) and the main-view
registry walk through the C++ driver verification/probe/own_sun_hook_host.cpp over the production header; the
production hook's answer (hidden for the eligible record in an own-sun frame, the original for every other probe, no
read before the latch) and its install conditions (the own sun only); the deletion of the lens bracket with the
lens-flare gain and its engine-side cull (2026-11-02) and of the partial-sun-occlusion override (wraps, scan,
classification, deferral, visibility pass, settings, rows); the static site verifier; the launcher's own-sun log
option. No device, no Wine.
"""
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
from source_text import source_text

ROOT = Path(__file__).resolve().parents[2]


def fields(line):
    return dict(re.findall(r'(\w+)=([^ ]+)', line))


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def body(text, signature):
    """The brace-balanced body of the first function whose definition starts with `signature`."""
    start = text.index(signature)
    at = text.index('{', start)
    depth = 0
    for i in range(at, len(text)):
        depth += {'{': 1, '}': -1}.get(text[i], 0)
        if depth == 0:
            return text[at:i + 1]
    raise AssertionError(signature)


class OwnSunHookHost(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.dir = tempfile.TemporaryDirectory(prefix='x3-own-sun-hook-')
        cls.driver = Path(cls.dir.name) / 'own_sun_hook_host'
        compiler = shutil.which('clang++') or shutil.which('c++')
        subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                        str(ROOT / 'verification/probe/own_sun_hook_host.cpp'), '-o', str(cls.driver)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.dir.cleanup()

    def run_driver(self, *args):
        return fields(subprocess.check_output([str(self.driver)] + [str(a) for a in args], text=True).strip())

    # Defaults: the main view (0x100) re-probes a record owned by the background view (0x200, +0x270 & 0x400000).
    def eligible(self, owner=0x200, view=0x100, main=0x100, owner_flags=0x400135):
        return int(self.run_driver('eligible', owner, view, main, owner_flags)['eligible'])

    def test_eligibility(self):
        self.assertEqual(self.eligible(), 1)
        self.assertEqual(self.eligible(owner=0x100), 0)            # a record the main view owns (ship flares, group 25): run223
        self.assertEqual(self.eligible(view=0x200), 0)             # the owner's own probe (layer 15)
        self.assertEqual(self.eligible(view=0x300), 0)             # a later view's re-probe
        self.assertEqual(self.eligible(owner_flags=0x135), 0)      # a foreign record whose owner is not a background-regime view
        self.assertEqual(self.eligible(owner_flags=0), 0)          # owner flags unreadable
        self.assertEqual(self.eligible(owner=0), 0)
        self.assertEqual(self.eligible(main=0), 0)                 # main view unknown
        # run223's measured case: owner 334fe888 layer 15 flags 0x00400135, main 334fe0f0.
        self.assertEqual(self.eligible(owner=0x334fe888, view=0x334fe0f0, main=0x334fe0f0, owner_flags=0x00400135), 1)

    def test_main_view_walk(self):
        view = lambda case: int(self.run_driver('view', case)['view'], 16)
        self.assertEqual(view('ok'), 0x6000)
        for case in ('no_marker', 'no_handle', 'bad_count', 'missing', 'cycle', 'misaligned', 'unreadable'):
            self.assertEqual(view(case), 0, case)

    def test_site_constants(self):
        sites = self.run_driver('sites')
        self.assertEqual((sites['probe_site'], sites['probe_target'], sites['lens_site'], sites['lens_target']), ('0x471630', '0x488720', '0x472491', '0x47e6e0'))
        self.assertEqual((sites['gates'], sites['gates_length']), ('0xe5f40888a926996', '148'))


class OwnSunHookSource(unittest.TestCase):
    def test_probe_answers_hidden_only_for_the_own_sun(self):
        sun = source_text(ROOT / 'src/proxy/own_sun_hook.cpp')
        decide = body(sun, 'int decide_probe(const std::uint32_t* record, const std::uint32_t* view) {\n    if')
        # Nothing to answer, no closed (late) frame and no log: the original, before the main-view walk and the owner's read.
        early = decide.index('if (!record || !view || (!own_sun_frame_ && !own_sun_late_ && !log_)) {')
        self.assertLess(decide.index('++counters_.probes;'), early)
        self.assertLess(early, decide.index('begin_frame(frame);'))
        self.assertLess(early, decide.index('x3m::engine_memory::read('))
        branch = decide.index('if (own && x3m::own_sun::core::answer(own_sun_frame_, own) == x3m::own_sun::core::answer_hidden) {')
        self.assertLess(decide.index('const bool own = core::eligible(record_owner, probing, main_view_, owner_flags);'), branch)
        self.assertIn('++counters_.own_sun_hidden;\n        own_sun_hidden_seen_ = true;\n        own_sun_hidden_frame_ = frame;\n'
                      '        return core::Hidden;\n    }', decide[branch:])
        # Every other probe is the original's; only the log runs it itself, once, and returns its own answer.
        self.assertIn('if (!log_) return core::Original;', decide)
        self.assertIn('return vanilla ? core::Hidden : core::Visible;', decide)
        self.assertEqual(decide.count('x3m_sun_probe_target)(record, view)'), 1)
        for gone in ('core::decide', 'ready_', 'latch_', 'deferral_', 'video_flags', 'core::Visible;\n'):
            self.assertNotIn(gone, decide.replace('return vanilla ? core::Hidden : core::Visible;', ''), gone)
        self.assertIn('void set_own_sun_frame(bool active) {\n    own_sun_frame_ = patched_ && own_sun_wanted_ && active;\n'
                      '    own_sun_late_ = false;\n}', sun)
        # Installed for the own sun only; the log alone installs nothing; one claim, the probe call.
        init = body(sun, 'bool initialize() {')
        self.assertIn('if (!own_sun_wanted_) {', init)
        self.assertLess(init.index('if (!own_sun_wanted_) {'), init.index('L"X3M_OWN_SUN_LOG"'))
        self.assertRegex(init, r'log\("own_sun_hook own_sun=%u log=%u patched=%u reason=%s probe_site=0x%08lx probe_target=0x%08lx probe_write=%s",')
        self.assertNotIn('X3M_SUN_OCCLUSION', sun)
        self.assertEqual(sun.count('engine_patch::claim_call('), 1)
        self.assertIn('engine_patch::claim_call(probe_site_, a.probe_site, a.probe_target,', sun)
        # The lens call's bytes stay in the identity; the call itself is never claimed.
        for kept in ('state_ = "lens_site_mismatch";', 'state_ = "lens_body_mismatch";'):
            self.assertIn(kept, init)
        for gone in ('x3m_sun_lens', 'lens_site_{', 'lens_site_.', 'bracket', 'set_listener', 'X3M_SUBMIT_PHASES', 'PreserveCpuState'):
            self.assertFalse(gone in sun, gone)

    def test_lens_bracket_gain_and_cull_are_deleted(self):
        # 2026-11-02: the own sun replaces the engine sun and the engine plumes remove the small engine flares, so the
        # lens bracket, the lens-flare gain (and its fog share) and the gain-0 engine cull went together.
        for path in ('src/proxy/motion_output_lens_bracket_inc.h', 'src/proxy/lens_flare_gain.h', 'src/proxy/lens_flare_cull.h',
                     'src/proxy/lens_flare_cull.cpp', 'src/proxy/lens_flare_cull_core.h', 'verification/probe/lens_flare_gain_fixture.cpp',
                     'verification/probe/verify_lens_flare_cull_site.py', 'verification/analysis/test_lens_flare_cull.py'):
            self.assertFalse((ROOT / path).exists(), path)
        sources = ''.join(source_text(p) for p in (ROOT / 'src').rglob('*') if p.suffix in ('.h', '.cpp'))
        for gone in ('lens_flare_gain', 'lens_gain', 'lens_bracket', 'bracket_open', 'bracket_installed', 'lens_flare_cull', 'prepare_lens',
                     'set_listener', 'sun_lens_begin', 'X3M_LENS_FLARE_GAIN', 'lens_flare_fog'):
            self.assertFalse(gone in sources, gone)
        self.assertNotIn('lens_flare_cull', (ROOT / 'CMakeLists.txt').read_text())
        schema = load('config_schema', 'tools/config/schema.py')
        self.assertFalse(any(e['key'].startswith('lens_flare') for e in schema.SETTINGS))
        manage = (ROOT / 'tools/manage.py').read_text()
        self.assertNotIn("'--lens-flare-gain'", manage)
        self.assertIn("    'X3M_LENS_FLARE_GAIN',\n)", manage)  # REMOVED_VARIABLES: an inherited value is dropped

    def test_partial_sun_occlusion_is_deleted(self):
        for path in ('src/renderer/lens_visibility_variant.h', 'src/renderer/lens_visibility_variant.cpp', 'src/renderer/sun_occlusion_pass.h',
                     'src/renderer/sun_occlusion_pass.cpp', 'src/renderer/sun_visibility_program_inc.h', 'src/renderer/sun_visibility_taps_inc.h',
                     'src/temporal/sun_visibility_ps.hlsl', 'src/proxy/sun_occlusion.cpp', 'src/proxy/sun_occlusion_core.h',
                     'src/proxy/motion_output_sun_occlusion_inc.h', 'verification/probe/sun_occlusion_fixture.cpp'):
            self.assertFalse((ROOT / path).exists(), path)
        core = source_text(ROOT / 'src/proxy/own_sun_hook_core.h')
        for gone in ('classify_body', 'footprint', 'Deferral', 'struct Hold', 'struct Ready', 'struct Latch', 'decide(', 'classify_blend',
                     'smoothing_alpha', 'jitter_uv', '<cmath>'):
            self.assertNotIn(gone, core, gone)
        for kept in ('inline bool eligible(', 'std::uintptr_t main_view(Read& read)', 'config_video_flags = 0xfc', 'record_visible = 0x30'):
            self.assertIn(kept, core)
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        self.assertNotIn('sun_occlusion_pass', cmake)
        self.assertNotIn('lens_visibility_variant', cmake)
        sources = ''.join(source_text(p) for p in (ROOT / 'src').rglob('*') if p.suffix in ('.h', '.cpp'))
        for row in ('"sun_lens_draw ', '"sun_visibility ', '"sun_lens_body_unclassifiable ', '"sun_occlusion_blocked ', '"sun_occlusion_released ',
                    '"sun_occlusion_config ', '"sun_lens_bracket ', '"sun_lens_readback ', 'L"lens", L"bgra8"', 'X3M_SUN_OCCLUSION_RADIUS', 'X3M_SUN_OCCLUSION_CURVE',
                    'X3M_SUN_OCCLUSION_CORE_F', 'X3M_SUN_OCCLUSION_DEFAULT', 'L"X3M_SUN_OCCLUSION"', 'L"X3M_SUN_OCCLUSION_LOG"'):
            self.assertFalse(row in sources, row)

    def test_dll_reads_own_sun_as_default_on(self):
        capture = source_text(ROOT / 'src/proxy/capture.cpp')
        self.assertIn('const bool own_sun_requested = !(x3m::config::get(L"X3M_OWN_SUN", value, 32) == 1 && value[0] == L\'0\');', capture)
        self.assertIn('own_sun_on = own_sun_requested && motion_output_requested && own_sun_radiance > 0.f && sun_installed;', capture)


class OwnSunHookSites(unittest.TestCase):
    def test_installed_executable_matches_every_site_claim(self):
        verifier = load('own_sun_hook_sites', 'verification/probe/verify_own_sun_hook_sites.py')
        if not verifier.DEFAULT_EXE.exists():
            self.skipTest('X3AP.exe is not installed')
        result = verifier.verify(verifier.DEFAULT_EXE)
        self.assertTrue(result['passed'], result['checks'])
        self.assertEqual(len(result['checks']), 16)
        self.assertEqual(result['conflicts'], ['submit_phase_sort_return_b'])


class OwnSunHookFixtureRunner(unittest.TestCase):
    def test_expected_counts_match_the_fixtures(self):
        runner = load('run_own_sun_hook', 'verification/probe/run_own_sun_hook.py')
        hook = source_text(ROOT / 'verification/probe/own_sun_hook_fixture.cpp')
        self.assertEqual(runner.EXPECTED_HOOK_CHECKS, len(re.findall(r'\bcheck\("', hook)))
        self.assertEqual(runner.EXPECTED_HOOK_CHECKS, 40)  # 46 until the lens bracket's six checks went (2026-11-02)
        self.assertFalse(hasattr(runner, 'EXPECTED_GPU_CHECKS'))


class OwnSunLogLaunchOption(unittest.TestCase):
    NAMES = ('X3M_OWN_SUN_LOG', 'X3M_SUN_OCCLUSION', 'X3M_SUN_OCCLUSION_LOG', 'X3M_SUN_OCCLUSION_RADIUS', 'X3M_SUN_OCCLUSION_CURVE',
             'X3M_SUN_OCCLUSION_CORE_F', 'X3M_SUN_OCCLUSION_DEFAULT', 'X3M_LENS_FLARE_GAIN')

    def launch(self, directory, *args, inherited=None):
        module = load('own_sun_hook_manage', 'tools/manage.py')
        game = Path(directory) / 'game'
        game.mkdir(exist_ok=True)
        (game / 'X3AP.exe').touch()
        (game / 'd3d9.dll').write_bytes(b'fixture')   # a modded dry run checks the installed DLL against its manifest
        (game / 'x3-modern-install.json').write_text(json.dumps({'sha256': hashlib.sha256(b'fixture').hexdigest()}))
        wine = Path(directory) / 'wine'
        wine.touch()
        argv = ['manage.py', 'launch', '--dry-run', '--game-dir', str(game), *args]
        output, error = io.StringIO(), io.StringIO()
        with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', wine), mock.patch.object(module, 'VOICE_DECODER_REPO', None), \
                mock.patch.dict(module.os.environ, inherited or {}), \
                mock.patch.object(module.subprocess, 'call', side_effect=AssertionError('must never launch')), \
                contextlib.redirect_stdout(output), contextlib.redirect_stderr(error):
            try:
                module.main()
            except SystemExit as exit_error:
                return exit_error.code, output.getvalue(), error.getvalue()
        return 0, output.getvalue(), error.getvalue()

    def sun_env(self, output):
        return {k: v for k, v in json.loads(output)['env'].items() if k in self.NAMES}

    def test_log_only_when_asked_and_stale_values_dropped(self):
        with tempfile.TemporaryDirectory() as directory:
            code, output, error = self.launch(directory, inherited={name: '1' for name in self.NAMES})
            self.assertEqual(code, 0, error)
            self.assertEqual(self.sun_env(output), {})
            code, output, error = self.launch(directory, '--own-sun-log')
            self.assertEqual(code, 0, error)
            self.assertEqual(self.sun_env(output), {'X3M_OWN_SUN_LOG': '1'})
            code, output, error = self.launch(directory, '--vanilla', '--own-sun-log', inherited={'X3M_OWN_SUN_LOG': '1'})
            self.assertEqual(code, 0, error)
            self.assertEqual(self.sun_env(output), {})

    def test_the_deleted_options_are_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            for args in (('--sun-occlusion',), ('--no-sun-occlusion',), ('--sun-occlusion-log',), ('--sun-occlusion-radius', '0.05'),
                         ('--sun-occlusion-curve', '2'), ('--sun-occlusion-core-f', 'on'), ('--lens-flare-gain', '0.3')):
                code, _, error = self.launch(directory, *args)
                self.assertNotEqual(code, 0, args)
                self.assertIn('unrecognized arguments', error, args)


if __name__ == '__main__':
    unittest.main()
