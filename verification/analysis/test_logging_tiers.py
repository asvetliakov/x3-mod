"""Logging tiers (docs/architecture/logging-tiers.md, 2026-09-26): the launcher's --debug / --perf and the options they
replaced, and the DLL rule that every switch a group stands for is read through src/proxy/log_tiers.h.

Hermetic like test_launcher_defaults.py: tools/manage.py launch --dry-run against a temporary game directory with a fake
proxy; nothing is launched.
"""
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
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
# Options the two groups replaced (removed from the launcher; their variables stay DLL reads for the fixtures).
REMOVED = (('--telemetry',), ('--frame-timing',), ('--frame-timing', '5'), ('--frame-phases',), ('--fps-overlay',), ('--volumetric-fog-timing',),
           ('--camera-log', '1'), ('--shadow-retention-census',), ('--shadow-retention-timing',), ('--object-bounds-log',),
           ('--cull-census',), ('--lod-switch-log',), ('--lod-switch-log', '16'), ('--media-cue-trace',), ('--music-trace',),
           ('--window-trace',), ('--shadow-sun-trace',), ('--sector-background',), ('--loading-probes',),
           ('--collide-narrow-census',), ('--collide-query-phases',),
           # 2026-09-26, second step: folded into --perf or fixture-only (the DLL reads stay), and the removed debug switches.
           ('--telemetry-draw',), ('--game-phases',), ('--game-phase-threshold-ms', '20'), ('--pass-phases',), ('--residual-phases',),
           ('--light-phases',), ('--submit-phases',), ('--loop-phases',), ('--frame-end-stride', '1'), ('--frame-timing-state-stamps', '4'),
           ('--mesh-adjacency', 'verify'), ('--mesh-adjacency-dump',), ('--resource-read', 'verify'), ('--collide-memo-verify',),
           ('--volumetric-fog-everywhere',))
# The DLL switches each group stands for (the read sites under src/proxy/ must go through log_tiers.h).
DEBUG_SWITCHES = ('X3M_MOTION_FRAME_LOG', 'X3M_CAMERA_LOG', 'X3M_SHADOW_ROWS', 'X3M_SHADOW_RETENTION_CENSUS', 'X3M_OBJECT_BOUNDS_LOG',
                  'X3M_CULL_CENSUS', 'X3M_LOD_SWITCH_LOG', 'X3M_MEDIA_CUE_TRACE', 'X3M_MUSIC_TRACE', 'X3M_WINDOW_TRACE',
                  'X3M_SHADOW_SUN_TRACE', 'X3M_SECTOR_BACKGROUND', 'X3M_LOADING_PROBES', 'X3M_COLLIDE_NARROW_CENSUS',
                  'X3M_COLLIDE_QUERY_PHASES')
# --draw-trace (2026-09-26): the heavy per-draw and engine-stamp attribution, in no other group (the submit stamps in none).
DRAW_TRACE_SWITCHES = ('X3M_TELEMETRY_DRAW', 'X3M_GAME_PHASES', 'X3M_PASS_PHASES', 'X3M_RESIDUAL_PHASES', 'X3M_LIGHT_PHASES', 'X3M_LOOP_PHASES')
PERF_SWITCHES = ('X3M_FRAME_TIMING', 'X3M_FRAME_PHASES', 'X3M_FPS_OVERLAY', 'X3M_VOLUMETRIC_FOG_TIMING', 'X3M_SHADOW_TIMING',
                 'X3M_FRAME_END_STRIDE')


def load_manage():
    sys.path.insert(0, str(ROOT / 'tools'))
    spec = importlib.util.spec_from_file_location('logging_tiers_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class LauncherGroups(unittest.TestCase):
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

    def launch(self, *args, inherited=None):
        argv = ['manage.py', 'launch', '--dry-run', '--game-dir', str(self.game), *args]
        environ = {k: v for k, v in os.environ.items() if not k.startswith('X3M_')}
        environ.update(inherited or {})
        output, error = io.StringIO(), io.StringIO()
        module = self.module
        with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', self.wine), \
                mock.patch.object(module, 'VOICE_DECODER_REPO', None), mock.patch.dict(module.os.environ, environ, clear=True), \
                mock.patch.object(module.subprocess, 'call', side_effect=AssertionError('must never launch')), \
                contextlib.redirect_stdout(output), contextlib.redirect_stderr(error):
            try:
                module.main()
            except SystemExit as exit_error:
                return exit_error.code, None, error.getvalue()
        data = json.loads(output.getvalue())
        return 0, {k: v for k, v in data['env'].items() if k.startswith('X3M_')}, error.getvalue()

    def test_replaced_options_exit_2(self):
        for args in REMOVED:
            with self.subTest(args=args):
                code, _, error = self.launch(*args)
                self.assertEqual(code, 2, args)
                self.assertTrue('unrecognized arguments' in error or 'invalid choice' in error, error[-300:])

    def test_parser_takes_no_abbreviations(self):
        # allow_abbrev=False (2026-09-26): a prefix of a registered option is an unknown argument.
        for args in (('--capture', '4'), ('--capture-frame', '4'), ('--deb',), ('--draw',)):
            with self.subTest(args=args):
                code, _, error = self.launch(*args)
                self.assertEqual(code, 2, args)
                self.assertIn('unrecognized arguments', error)

    def test_groups_add_exactly_their_variable(self):
        _, empty, _ = self.launch()
        tiered = set(self.module.TIERED_VARIABLES)
        self.assertFalse(set(empty) & tiered)
        for args, added in ((('--debug',), {'X3M_DEBUG': '1'}), (('--perf',), {'X3M_PERF': '1'}),
                            (('--debug', '--perf'), {'X3M_DEBUG': '1', 'X3M_PERF': '1'})):
            with self.subTest(args=args):
                code, env, error = self.launch(*args)
                self.assertEqual(code, 0, error)
                self.assertEqual(env, {**empty, **added})

    def test_inherited_logging_variables_are_dropped(self):
        inherited = {name: '1' for name in self.module.TIERED_VARIABLES}
        _, empty, _ = self.launch()
        code, env, error = self.launch(inherited=inherited)
        self.assertEqual(code, 0, error)
        self.assertEqual(env, empty)
        code, env, error = self.launch('--debug', inherited=inherited)
        self.assertEqual((code, env), (0, {**empty, 'X3M_DEBUG': '1'}), error)
        # Every variable of a replaced option is in the dropped set (the DLL still reads them for the fixtures).
        self.assertTrue(set(DEBUG_SWITCHES) | set(PERF_SWITCHES) | {'X3M_TELEMETRY', 'X3M_DEBUG', 'X3M_PERF', 'X3M_LOG_FILE'} <= set(self.module.TIERED_VARIABLES))

    def test_developer_options_travel_only_when_given(self):
        # Section 2 of the inventory since 2026-09-26: the two groups and four explicit developer options.
        _, empty, _ = self.launch()
        for args, added in ((('--gpu-sync-timing',), {'X3M_GPU_SYNC_TIMING': '1'}),
                            (('--profile',), {'X3M_PROFILE': '1', 'X3M_PROFILE_INTERVAL_US': '2000'}),
                            (('--taa-debug',), {'X3M_TAA_DEBUG': '1'}), (('--sun-occlusion-log',), {'X3M_SUN_OCCLUSION_LOG': '1'}),
                            (('--perf',), {'X3M_PERF': '1'})):
            with self.subTest(args=args):
                code, env, error = self.launch(*args)
                self.assertEqual(code, 0, error)
                self.assertEqual(env, {**empty, **added})
        # --draw-trace (the fifth developer option) needs a group for the telemetry counters and the frame boundary; it sends
        # only X3M_DRAW_TRACE (the DLL expands it) and nothing under --vanilla.
        for args, added in ((('--perf', '--draw-trace'), {'X3M_PERF': '1', 'X3M_DRAW_TRACE': '1'}),
                            (('--debug', '--draw-trace'), {'X3M_DEBUG': '1', 'X3M_DRAW_TRACE': '1'})):
            with self.subTest(args=args):
                code, env, error = self.launch(*args)
                self.assertEqual(code, 0, error)
                self.assertEqual(env, {**empty, **added})
        code, _, error = self.launch('--draw-trace')
        self.assertEqual(code, 2)
        self.assertIn('--draw-trace requires --perf or --debug', error)
        # Neither group carries the heavy families, and --draw-trace never sends them as launcher variables.
        heavy = set(DRAW_TRACE_SWITCHES) | {'X3M_SUBMIT_PHASES'}
        self.assertFalse(heavy & (set(self.launch('--perf')[1]) | set(self.launch('--debug')[1]) | set(self.launch('--perf', '--draw-trace')[1])))
        sources = ''.join(p.read_text() for p in (ROOT / 'src/proxy').glob('*.cpp'))
        for name in DRAW_TRACE_SWITCHES:
            self.assertIn(f'log_tier::draw_trace_flag(L"{name}")', sources, name)
            self.assertNotIn(f'log_tier::debug_flag(L"{name}")', sources, name)
            self.assertNotIn(f'log_tier::perf_flag(L"{name}")', sources, name)
        self.assertIn('log_tier::perf_flag(L"X3M_FRAME_PHASES")||log_tier::debug()||log_tier::draw_trace()', (ROOT / 'src/proxy/frame_phases.cpp').read_text())
        self.assertIn('X3M_DRAW_TRACE', self.module.TIERED_VARIABLES)


def enclosing_function(lines, index):
    """The top-level function body around line `index` (a line at column 0 ending in '{' up to the next '}' at column 0)."""
    start = index
    while start > 0 and not (lines[start][:1] not in ('', ' ', '\t', '/', '#', '}') and lines[start].rstrip().endswith('{')):
        start -= 1
    end = index
    while end < len(lines) - 1 and not lines[end].startswith('}'):
        end += 1
    return '\n'.join(lines[start:end + 1])


class DllGroupReads(unittest.TestCase):
    """Every function that reads a switch a group stands for consults src/proxy/log_tiers.h (log_tier::), so
    X3M_DEBUG=1 / X3M_PERF=1 on a bare proxy reach it; X3M_TELEMETRY itself is read only inside the header."""

    def test_group_switch_reads_use_the_tier_helper(self):
        sources = {path: path.read_text().splitlines() for path in (ROOT / 'src/proxy').glob('*.cpp')}
        for name in DEBUG_SWITCHES + PERF_SWITCHES + DRAW_TRACE_SWITCHES:
            reads = [(path, i) for path, lines in sources.items() for i, line in enumerate(lines)
                     if f'L"{name}"' in line and not line.lstrip().startswith('//')]
            with self.subTest(name=name):
                self.assertTrue(reads, name)
                for path, i in reads:
                    self.assertIn('log_tier::', enclosing_function(sources[path], i), f'{path.name}:{i + 1} reads {name} without the group')
        self.assertFalse([p.name for p, lines in sources.items() if any('L"X3M_TELEMETRY"' in l for l in lines)])
        # The submit stamps stay outside --perf: they claim the lens traversal call the sun-occlusion default patches.
        submit = (ROOT / 'src/proxy/submit_phases.cpp').read_text().splitlines()
        reads = [i for i, line in enumerate(submit) if 'L"X3M_SUBMIT_PHASES"' in line]
        self.assertEqual(len(reads), 1)
        self.assertNotIn('log_tier::', enclosing_function(submit, reads[0]))
        header = (ROOT / 'src/proxy/log_tiers.h').read_text()
        for function in ('debug()', 'perf()', 'telemetry()', 'debug_flag(', 'perf_flag(', 'cadence_default('):
            self.assertIn(function, header)
        self.assertIn('env_flag(L"X3M_TELEMETRY") || perf() || debug()', header)
        self.assertIn('inline bool draw_trace_flag(const wchar_t* name) noexcept { return env_flag(name) || draw_trace(); }', header)


class SessionLogContracts(unittest.TestCase):
    """Review of 2026-09-26: the exit path, the crash filter and the game-thread I/O rule, pinned in the source."""

    def test_no_game_thread_writefile_outside_the_session_log(self):
        # Left: the voice DMO fallback's exception-context witness (it may run on a thread that holds the buffer lock and
        # is about to die); the four patch restore rows, written only inside DllMain on a dynamic FreeLibrary (never on a
        # game thread in play; their fixture records are bound to these sources); loading_trace.cpp's are the game's own
        # file I/O it forwards.
        allowed = {'session_log.cpp', 'voice_dmo_fallback.cpp', 'loading_trace.cpp', 'loading_trace_light.cpp',
                   'fov.cpp', 'lod_occlusion.cpp', 'sun_flare_fix.cpp', 'terran_station_lod.cpp'}
        offenders = [path.name for path in (ROOT / 'src/proxy').glob('*.cpp')
                     if path.name not in allowed and re.search(r'\bWriteFile\(', path.read_text())]
        self.assertEqual(offenders, [])

    def test_exit_path_and_crash_filter(self):
        loader = (ROOT / 'src/proxy/loader.cpp').read_text()
        self.assertIn('if (reserved != nullptr) x3m::abandon_devices_at_exit();', loader)
        self.assertLess(loader.index('x3m::abandon_fog_density_workers();'), loader.index('x3m::abandon_devices_at_exit();'))
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertIn('Map* kept=new(storage) Map(); // placement: never destroyed', capture)
        self.assertIn('session_log::park_writer("last_device");', capture)
        self.assertIn('session_log::start_writer(telemetry::enabled()); // re-arms', capture)
        source = (ROOT / 'src/proxy/session_log.cpp').read_text()
        self.assertIn('previous_filter_ = SetUnhandledExceptionFilter(&x3m_exception_witness);', source)
        self.assertNotIn('AddVectoredExceptionHandler', source)
        self.assertIn('return previous ? previous(info) : EXCEPTION_CONTINUE_SEARCH;', source)
        self.assertIn('GetTickCount64() + kExceptionDrainMs', source)
        self.assertIn('FreeLibraryAndExitThread(module_ref_, 0);', source)
        self.assertIn('const bool game_known = game.size() >= 2;', source)


if __name__ == '__main__':
    unittest.main()
