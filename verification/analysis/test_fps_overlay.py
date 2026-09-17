"""FPS overlay (comparison-hotkeys.md, "FPS overlay"): host execution of the
accumulator, the Present-path wiring and the launcher option. No Wine, game
or DLL build."""
import argparse
import ast
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

from verification.analysis.test_capture_bloom_lifetime import extract_function

ROOT = Path(__file__).resolve().parents[2]


class FpsOverlay(unittest.TestCase):
    def test_accumulator_window_rounding_toggle_and_reset(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler)
        fixture = ROOT / 'verification/probe/fps_overlay_fixture.cpp'
        with tempfile.TemporaryDirectory(prefix='x3-fps-overlay-') as temporary:
            for name, flags in [('release', ['-O2']), ('sanitized', ['-O1', '-g', '-fsanitize=address,undefined'])]:
                with self.subTest(mode=name):
                    executable = Path(temporary) / name
                    result = subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', *flags,
                        str(fixture), '-o', str(executable)], capture_output=True, text=True, timeout=60)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=20)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn('failures=0 allocations=0', result.stdout)
                    print(name, result.stdout.strip())

    def test_present_path_wiring(self):
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        header = (ROOT / 'src/proxy/fps_overlay.h').read_text()
        controls = (ROOT / 'src/proxy/comparison_controls.h').read_text()
        present = extract_function(capture, 'HRESULT WINAPI present(')
        # The overlay draws after the hotkey notice, before the native Present,
        # under the notice's admission and the same pin, with its own bitmap.
        self.assertLess(present.index('comparison_notice.draw('), present.index('ctx.fps_notice.draw('))
        self.assertLess(present.index('ctx.fps_notice.draw('), present.index('const HRESULT hr=fn('))
        self.assertIn('if(ctx.fps_overlay.visible() && comparison_foreground()\n'
                      '            && !ctx.reset_active && !ctx.compositor && !ctx.bloom_busy\n'
                      '            && ctx.motion_output.comparison_boundary_available()){', present)
        self.assertIn('if(!notice_pin.device){ctx.get<ULONG (WINAPI*)(IDirect3DDevice9*)>(1)(d);notice_pin.device=d;notice_pin.owner=owner;}', present)
        self.assertEqual(present.count('BloomOperation internal(ctx);'), 2)
        self.assertIn('comparison_state_failed(overlay.restore)', present)
        self.assertIn('renderer_fps_overlay device=%llu frame=%llu operation=%08lx restore=%08lx drawn=%u', present)
        # The existing notice's draw site is unchanged.
        self.assertIn('ctx.comparison_notice.visible(GetTickCount64()) && comparison_foreground()', present)
        self.assertEqual(present.count('ctx.comparison_notice.draw(d,ctx.original,ctx.caps.NumSimultaneousRTs)'), 1)
        # One QPC per shown frame after the native Present, before draws reset;
        # the text is rebuilt only when the accumulator says so.
        accounting = present.index('if(ctx.fps_overlay.visible()){')
        self.assertLess(present.index('const HRESULT hr=fn('), accounting)
        self.assertLess(accounting, present.index('++ctx.frame; ctx.draws=0;'))
        self.assertIn('const bool refreshed=ctx.fps_overlay.frame(uint64_t(stamp.QuadPart),ctx.draws);', present)
        self.assertIn('const int shadows=!sun_shadow_apply_requested?-1:int(ctx.motion_output.sun_shadow_enabled());', present)
        self.assertIn('shadows<0?"":shadows?"SHADOWS ON":"SHADOWS OFF"', present)
        self.assertEqual(present.count('QueryPerformanceCounter(&stamp)'), 2)  # the overlay and the frame_end line
        # The key: sampler-owned, option-gated, edge-triggered like the others.
        polling = extract_function(capture, 'void comparison_begin_frame(')
        self.assertIn('&& !sun_shadow_apply_requested && !fps_overlay_requested)return;', polling)
        # Ctrl+Alt+F7 with Shift up: every Ctrl+Shift function key is owned and
        # F1-F3 are engine views; the telemetry marker requires Shift, so the
        # chords are disjoint. One option-gated poller; the sampler edges the
        # folded chord outside its Ctrl+Shift arm.
        self.assertIn('keys.alt=fps_overlay_requested && (GetAsyncKeyState(VK_MENU)&0x8000)!=0;', polling)
        self.assertIn('keys.fps_overlay=fps_overlay_requested && (GetAsyncKeyState(VK_F7)&0x8000)!=0;', polling)
        self.assertEqual(capture.count('GetAsyncKeyState(VK_F7)'), 1)
        self.assertEqual(capture.count('GetAsyncKeyState(VK_MENU)'), 1)
        self.assertIn('(GetAsyncKeyState(VK_SHIFT)&0x8000)!=0;', (ROOT / 'src/proxy/telemetry.cpp').read_text().split('marker_down=', 1)[1].split('\n', 1)[0])
        edge = 'result.fps_overlay = keys.control && keys.alt && !keys.shift && keys.fps_overlay && !fps_overlay_down_;'
        self.assertLess(controls.index(edge), controls.index('latch(keys);\n        return result;'))
        self.assertLess(controls.index('result.sun_shadow = keys.sun_shadow && !sun_shadow_down_;\n        }'), controls.index(edge))
        # A draw failure keeps the mode on, retries next frame and logs once
        # per episode; the second line follows the at-rest state the frame it
        # changes. Only --fps-overlay: the emitter polls stay on their option.
        self.assertIn('if(ctx.fps_overlay.draw_outcome(FAILED(overlay.operation)||FAILED(overlay.restore)))', present)
        self.assertNotIn('reason=draw_failed', present)
        self.assertNotIn('ctx.fps_overlay.toggle()', present)
        self.assertIn('fps_overlay_toggle device=%llu frame=%llu visible=%u reason=key', polling)
        self.assertIn('if(ctx.fps_overlay.shadows(shadows)||refreshed)', present)
        for key in ('VK_F4', 'VK_F5', 'VK_F6'):
            self.assertIn(f'emitter_compare && (GetAsyncKeyState({key})&0x8000)!=0;', polling)
        self.assertIn('if(action.fps_overlay)log("fps_overlay_toggle device=%llu frame=%llu visible=%u reason=key"', polling)
        self.assertIn(edge, controls)
        self.assertIn('fps_overlay_down_ = keys.fps_overlay;', controls)
        # Reset empties the window and the bitmap; visibility survives.
        reset = extract_function(capture, 'HRESULT reset_common(')
        self.assertIn('ctx.fps_overlay.reset();ctx.fps_notice.text("","");', reset)
        self.assertIn('ComparisonNotice fps_notice{72};', capture)
        self.assertIn('fps_overlay_requested=GetEnvironmentVariableW(L"X3M_FPS_OVERLAY",setting,32)==1', capture)
        # The accumulator itself: no OS or D3D header, no allocation.
        for absent in ('windows.h', 'd3d9.h', 'new', 'malloc', 'std::string', 'std::vector'):
            self.assertNotIn(absent, header)

    def test_launcher_option_writes_the_environment(self):
        source = (ROOT / 'tools/manage.py').read_text()
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
            and node.targets[0].slice.value == 'X3M_FPS_OVERLAY']
        self.assertEqual(len(assignments), 1)
        main.body = statements + assignments + [ast.Return(value=ast.Name(id='env', ctx=ast.Load()))]
        compiled = compile(ast.fix_missing_locations(ast.Module(body=[main], type_ignores=[])), 'manage_under_test.py', 'exec')
        scope = dict(argparse=argparse, Path=Path, GAME=Path('/unused'), BOTTLE='X3', ROOT=ROOT, __doc__='test')
        exec(compiled, scope)
        for arguments, expected in (([], '0'), (['--fps-overlay'], '1')):
            with self.subTest(arguments=arguments), mock.patch.object(sys, 'argv', ['manage.py', 'launch', *arguments]):
                scope['env'] = {'X3M_FPS_OVERLAY': '1'}  # an inherited value never leaks
                self.assertEqual(scope['main']()['X3M_FPS_OVERLAY'], expected)


if __name__ == '__main__':
    unittest.main()
