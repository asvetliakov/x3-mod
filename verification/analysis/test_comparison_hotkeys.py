"""In-game keys (comparison-hotkeys.md, "Removed 2026-09-26"): F8 is the only
key the proxy reads, and only under X3M_DEBUG=1. Focused host execution: no
Wine, game, shader compiler or DLL build."""
import argparse
import ast
import contextlib
import io
import math
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import textwrap
import unittest
from unittest import mock

from verification.analysis.test_capture_bloom_lifetime import extract_function
from source_text import source_text

ROOT = Path(__file__).resolve().parents[2]
F8_BEGIN = '    // F8 is the one in-game key'
F8_END = 'ctx.key_down=down; ctx.capture=ctx.remaining>0;\n'


def f8_block(capture):
    start = capture.index(F8_BEGIN)
    return capture[start:capture.end(F8_END, start)]


class InGameKeys(unittest.TestCase):
    def test_f8_is_the_only_key_and_only_under_debug(self):
        sources = {path: source_text(path) for path in (ROOT / 'src').rglob('*') if path.suffix in ('.cpp', '.h')}
        pollers = {str(path.relative_to(ROOT)): text.count('GetAsyncKeyState(')
                   for path, text in sources.items() if 'GetAsyncKeyState(' in text}
        self.assertEqual(pollers, {'src/proxy/capture.cpp': 1})
        for path, text in sources.items():
            for absent in ('GetKeyState(', 'VK_CONTROL', 'VK_SHIFT', 'VK_MENU', 'ComparisonControls', 'comparison_begin_frame',
                           'telemetry_phase_marker', 'fps_overlay_toggle', 'sun_shadow_toggle', 'renderer_comparison',
                           'screen_emission_additive_toggle', 'emission_source_gain_toggle', 'capture_armed', 'X3M_CAPTURE_DELAY'):
                self.assertNotIn(absent, text, (path, absent))
            self.assertIsNone(re.search(r'key=ctrl_|keys=ctrl_', text), path)
        for gone in ('src/proxy/comparison_controls.h', 'src/proxy/capture_arm_core.h'):
            self.assertFalse((ROOT / gone).exists(), gone)
        capture = sources[ROOT / 'src/proxy/capture.cpp']
        block = f8_block(capture)
        # The key is polled only when the debug tier is on (short-circuit before the call).
        self.assertIn('const bool down=log_tier::cached_debug && (GetAsyncKeyState(VK_F8)&0x8000)!=0;', block)
        # The flag is cached once at initialize_log, never read from the environment per frame (log_tiers.h).
        self.assertIn('log_tier::init();', extract_function(capture, 'void initialize_log('))
        tiers = source_text(ROOT / 'src/proxy/log_tiers.h')
        self.assertIn('inline void init() noexcept { cached_debug = debug(); cached_perf = perf(); cached_draw_trace = draw_trace(); }', tiers)
        self.assertNotIn('log_tier::debug()', extract_function(capture, 'HRESULT WINAPI present('))
        present = extract_function(capture, 'HRESULT WINAPI present(')
        self.assertIn(block, present)
        # X3M_CAPTURE_START: 0 (unset) never starts a burst; the launcher sends 999999.
        self.assertIn('unsigned capture_start = 0;', capture)
        self.assertIn('x3m::config::get(L"X3M_CAPTURE_START",setting,32)', capture)
        # The fps overlay has no key: visible whenever requested.
        overlay = source_text(ROOT / 'src/proxy/fps_overlay.h')
        self.assertIn('bool visible() const noexcept { return requested_; }', overlay)
        self.assertNotIn('toggle', overlay)
        # Bloom runs at full strength; the fog begin-frame step runs every frame without the sampler.
        self.assertIn('call.input.filter.strength=1.f;', extract_function(capture, 'void retain_compositor_scene('))
        self.assertIn('ctx.motion_output.volumetric_fog_begin_frame();', present)
        # The remaining toggles are fixture seams: no production caller.
        for seam in ('hull_emission_gain_toggle(', 'volumetric_fog_toggle(', 'volumetric_fog_step(', 'volumetric_fog_dust_motes_toggle('):
            callers = [str(p.relative_to(ROOT)) for p, t in sources.items()
                       if re.search(r'(?<!MotionOutput::)(?<!int )' + re.escape(seam), t)]
            self.assertLessEqual(set(callers), {'src/proxy/capture.cpp'}, seam)
        hull = capture[capture.index('x3m_hull_emission_fixture_toggle'):]
        self.assertLess(hull.index('#endif'), hull.index('\n}\n') + 10)  # inside the fixture-only export block
        self.assertNotIn('x3m_fog_dust_motes_fixture_toggle', capture)
        self.assertNotIn('x3m_sun_shadow_fixture_toggle', capture)

    def test_f8_press_captures_only_under_debug(self):
        """The production F8 block, executed: no poll and no capture without the
        debug tier; with it, one burst of X3M_CAPTURE_FRAMES per press edge. The
        fixture seam X3M_CAPTURE_START starts one burst either way."""
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler)
        block = f8_block(source_text(ROOT / 'src/proxy/capture.cpp'))
        harness = textwrap.dedent('''
            #include <cstdint>
            #include <cstdio>
            #define VK_F8 0x77
            static unsigned polls = 0, failures = 0, checks = 0; static bool f8 = false;
            short GetAsyncKeyState(int key) { ++polls; return key == VK_F8 && f8 ? short(-32768) : short(0); }
            namespace log_tier { static bool cached_debug = false; }
            static unsigned capture_start = 0, capture_count = 8;
            struct Ctx { std::uint64_t frame = 0; unsigned remaining = 0; bool key_down = false, capture = false; };
            static void frame(Ctx& ctx) {
                if (ctx.capture && ctx.remaining) --ctx.remaining;
                ++ctx.frame;
            @BLOCK@
            }
            #define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL line=%d %s\\n", __LINE__, #x); } } while (0)
            static unsigned run(Ctx& ctx, unsigned frames, bool key) { unsigned n = 0; f8 = key; for (unsigned i = 0; i < frames; ++i) { frame(ctx); n += ctx.capture; } return n; }
            int main() {
                { Ctx ctx; log_tier::cached_debug = false; // no debug: never polled, never captures
                  CHECK(run(ctx, 5, false) == 0); CHECK(run(ctx, 20, true) == 0); CHECK(run(ctx, 5, false) == 0); CHECK(run(ctx, 20, true) == 0);
                  CHECK(polls == 0); }
                { Ctx ctx; log_tier::cached_debug = true; polls = 0; // debug: one burst of 8 per press edge
                  CHECK(run(ctx, 5, false) == 0); CHECK(polls == 5);
                  CHECK(run(ctx, 30, true) == 8); // a held key is one press
                  CHECK(run(ctx, 2, false) == 0); CHECK(run(ctx, 1, true) == 1); CHECK(run(ctx, 20, false) == 7); }
                { Ctx ctx; log_tier::cached_debug = false; capture_start = 3; // the fixture seam, without debug
                  unsigned first = 0, n = 0; f8 = false;
                  for (unsigned i = 0; i < 40; ++i) { frame(ctx); if (ctx.capture) { ++n; if (!first) first = unsigned(ctx.frame); } }
                  CHECK(first == 3 && n == 8); capture_start = 0; }
                std::printf("f8 checks=%u failures=%u\\n", checks, failures);
                return failures ? 1 : 0;
            }
        ''').replace('@BLOCK@', block)
        with tempfile.TemporaryDirectory(prefix='x3-f8-') as temporary:
            source, executable = Path(temporary) / 'f8.cpp', Path(temporary) / 'f8'
            source.write_text(harness)
            result = subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-O1', str(source), '-o', str(executable)],
                                    capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(result.stdout, 'f8 checks=12 failures=0\n')

    def test_production_notice_state_failure_and_allocation_contract(self):
        # The notice panel class now draws only the FPS overlay.
        self.run_host('comparison_notice_fixture.cpp', [], notice=True)

    def test_production_bloom_handoff_preserves_display_at_full_strength(self):
        source = source_text(ROOT / 'src/proxy/capture.cpp')
        functions = [extract_function(source, 'void retain_compositor_scene(')]
        self.run_host('comparison_bloom_handoff_fixture.cpp', functions, handoff=True)

    def run_host(self, fixture, functions, notice=False, handoff=False):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory(prefix='x3-comparison-') as temporary:
            directory = Path(temporary)
            (directory / ('comparison_handoff_under_test_inc.h' if handoff else 'comparison_exposure_under_test_inc.h')).write_text('\n'.join(functions))
            stub = ROOT / 'verification/probe/hdr_display_snapshot_stubs/d3d9.h'
            (directory / 'd3d9.h').write_text(f'#include "{stub}"\n' + textwrap.dedent('''
                #define WINAPI
                using LONG=std::int32_t;
                using D3DCOLOR=DWORD;
                struct D3DRECT { LONG x1,y1,x2,y2; };
                struct D3DVIEWPORT9 { DWORD X,Y,Width,Height; float MinZ,MaxZ; };
                enum D3DBACKBUFFER_TYPE { D3DBACKBUFFER_TYPE_MONO };
                constexpr HRESULT D3DERR_NOTFOUND=-99;
                constexpr DWORD D3DCLEAR_TARGET=1;
            '''))
            extra = [str(ROOT / 'src/proxy/comparison_notice.cpp')] if notice else []
            for name, flags in [('release', ['-O2']), ('sanitized', ['-O1', '-g', '-fsanitize=address,undefined'])]:
                with self.subTest(fixture=fixture, mode=name):
                    executable = directory / name
                    result = subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', *flags,
                        '-I', str(directory), str(ROOT / 'verification/probe' / fixture), *extra,
                        '-o', str(executable)], capture_output=True, text=True, timeout=60)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=20)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn('failures=0', result.stdout)
                    print(name, result.stdout.strip())

    def test_launcher_auto_default_fixed_and_manual_override(self):
        # Execute the real parser/validation AST and exact exposure environment
        # assignments. Stop before filesystem/launch/install handling.
        source = source_text(ROOT / 'tools/manage.py')
        tree = ast.parse(source)
        main = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == 'main')
        statements = []
        for node in main.body:
            if isinstance(node, ast.Assign) and isinstance(node.targets[0], ast.Name) and node.targets[0].id == 'game':
                break
            statements.append(node)
        assignments = [node for node in ast.walk(main) if isinstance(node, ast.Assign)
            and isinstance(node.targets[0], ast.Subscript)
            and isinstance(node.targets[0].value, ast.Name) and node.targets[0].value.id == 'env'
            and isinstance(node.targets[0].slice, ast.Constant)
            and node.targets[0].slice.value in ('X3M_HDR_EXPOSURE', 'X3M_HDR_EV_MANUAL', 'X3M_HDR_EV_MAX')]
        main.body = statements + assignments + [ast.Return(value=ast.Name(id='env', ctx=ast.Load()))]
        # Include the launcher's literal defaults used by validation, without
        # executing filesystem/installation setup or duplicating their values.
        defaults = []
        for node in tree.body:
            if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name) and node.targets[0].id.isupper():
                try:
                    ast.literal_eval(node.value)
                except (ValueError, TypeError):
                    continue
                defaults.append(node)
        # Module-level helpers main() names (argparse `type=` callables such as pause_key_code).
        referenced = {n.id for n in ast.walk(main) if isinstance(n, ast.Name)}
        helpers = [node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name in referenced]
        compiled = compile(ast.fix_missing_locations(ast.Module(body=defaults + helpers + [main], type_ignores=[])), 'manage_under_test.py', 'exec')
        scope = dict(argparse=argparse, math=math, Path=Path, GAME=Path('/unused'), BOTTLE='X3', ROOT=ROOT, __doc__='test')
        exec(compiled, scope)
        base = ['manage.py', 'launch', '--motion-output', '--hdr', '--hdr-tonemap']
        cases = [([], 'auto', ''), (['--hdr-exposure', 'fixed'], 'fixed', ''),
                 (['--hdr-exposure', 'auto'], 'auto', ''),
                 (['--hdr-ev-manual', '-1'], 'fixed', '-1.0'),
                 (['--hdr-exposure', 'auto', '--hdr-ev-manual', '0.5'], 'fixed', '0.5')]
        for arguments, policy, manual in cases:
            with self.subTest(arguments=arguments), mock.patch.object(sys, 'argv', base + arguments):
                scope['env'] = {'X3M_HDR_EXPOSURE': 'fixed', 'X3M_HDR_EV_MANUAL': '2', 'X3M_HDR_EV_MAX': '2.0'}
                result = scope['main']()
                self.assertEqual(result, {'X3M_HDR_EXPOSURE': policy, 'X3M_HDR_EV_MANUAL': manual, 'X3M_HDR_EV_MAX': '1.3'})
        with mock.patch.object(sys, 'argv', base + ['--hdr-ev-max', '1.5']):
            scope['env'] = {}
            self.assertEqual(scope['main']()['X3M_HDR_EV_MAX'], '1.5')
        for policy in ('fixed', 'auto'):
            with mock.patch.object(sys, 'argv', ['manage.py', 'launch', '--no-hdr-tonemap', '--hdr-exposure', policy]), contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as error:
                    scope['main']()
                self.assertEqual(error.exception.code, 2)


if __name__ == '__main__':
    unittest.main()
