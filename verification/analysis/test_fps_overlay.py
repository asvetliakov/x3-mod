"""FPS overlay (comparison-hotkeys.md, "FPS overlay"): host execution of the
accumulator, the Present-path wiring and the launcher option. No Wine, game
or DLL build."""
import argparse
import math
import ast
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

from verification.analysis.test_capture_bloom_lifetime import extract_function
from source_text import source_text

ROOT = Path(__file__).resolve().parents[2]


class FpsOverlay(unittest.TestCase):
    def test_accumulator_window_rounding_and_reset(self):
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
        capture = source_text(ROOT / 'src/proxy/capture.cpp')
        header = source_text(ROOT / 'src/proxy/fps_overlay.h')
        present = extract_function(capture, 'HRESULT WINAPI present(')
        # The overlay draws before the native Present, admitted at a clean frame
        # boundary with the process in the foreground, under the pin, with its own bitmap.
        self.assertLess(present.index('ctx.fps_notice.draw('), present.index('const HRESULT hr=fn('))
        self.assertIn('if(ctx.fps_overlay.visible() && comparison_foreground()\n'
                      '            && !ctx.reset_active && !ctx.compositor && !ctx.bloom_busy\n'
                      '            && ctx.motion_output.comparison_boundary_available()){', present)
        self.assertIn('ctx.get<ULONG (WINAPI*)(IDirect3DDevice9*)>(1)(d);notice_pin.device=d;notice_pin.owner=owner;', present)
        self.assertEqual(present.count('BloomOperation internal(ctx);'), 1)
        self.assertIn('comparison_state_failed(overlay.restore)', present)
        self.assertIn('renderer_fps_overlay device=%llu frame=%llu operation=%08lx restore=%08lx drawn=%u', present)
        # No hotkey notice any more (comparison-hotkeys.md, "Removed 2026-09-26").
        self.assertNotIn('ctx.comparison_notice', capture)
        # One QPC per shown frame after the native Present, before draws reset;
        # the text is rebuilt only when the accumulator or the fog state says so.
        accounting = present.index('if(ctx.fps_overlay.visible()){')
        self.assertLess(present.index('const HRESULT hr=fn('), accounting)
        self.assertLess(accounting, present.index('++ctx.frame; ctx.draws=0;'))
        self.assertIn('const bool refreshed=ctx.fps_overlay.frame(uint64_t(stamp.QuadPart),ctx.draws);', present)
        self.assertIn('if(ctx.fps_overlay.fog(fog)||refreshed){', present)
        self.assertNotIn('SHADOWS', present)
        self.assertEqual(present.count('QueryPerformanceCounter(&stamp)'), 2)  # the overlay and the frame_end line
        # No key: on for the whole session whenever requested (X3M_FPS_OVERLAY or X3M_PERF).
        self.assertNotIn('toggle', header)
        self.assertIn('bool visible() const noexcept { return requested_; }', header)
        self.assertIn('log("fps_overlay_mode requested=1 refresh_ms=250 window_ms=1000");', capture)
        # A draw failure keeps the mode on, retries next frame and logs once per episode.
        self.assertIn('if(ctx.fps_overlay.draw_outcome(FAILED(overlay.operation)||FAILED(overlay.restore)))', present)
        self.assertNotIn('reason=draw_failed', present)
        # Reset empties the window and the bitmap; the overlay stays on.
        reset = extract_function(capture, 'HRESULT reset_common(')
        self.assertIn('ctx.fps_overlay.reset();ctx.fps_notice.text("","");', reset)
        self.assertIn('ComparisonNotice fps_notice{72};', capture)
        self.assertIn('fps_overlay_requested=log_tier::perf_flag(L"X3M_FPS_OVERLAY");', capture)  # or X3M_PERF=1
        # The accumulator itself: no OS or D3D header, no allocation.
        for absent in ('windows.h', 'd3d9.h', 'new', 'malloc', 'std::string', 'std::vector'):
            self.assertNotIn(absent, header)

    def test_launcher_option_writes_the_environment(self):
        # Since the logging tiers (2026-09-26) the overlay is part of --perf: the launcher sends X3M_PERF=1, the DLL reads
        # X3M_FPS_OVERLAY or the group; --fps-overlay is gone and an inherited X3M_FPS_OVERLAY never leaks.
        import json
        import tempfile
        from verification.analysis.test_lod_scale_launch import LodScaleLaunchOption
        helper = LodScaleLaunchOption()
        with tempfile.TemporaryDirectory() as directory:
            code, _, error = helper.launch(directory, '--fps-overlay')
            self.assertEqual(code, 2)
            self.assertIn('unrecognized arguments', error)
            for arguments, perf in (([], None), (['--perf'], '1')):
                with self.subTest(arguments=arguments):
                    code, output, error = helper.launch(directory, *arguments, inherited={'X3M_FPS_OVERLAY': '1'})
                    self.assertEqual(code, 0, error)
                    env = json.loads(output)['env']
                    self.assertNotIn('X3M_FPS_OVERLAY', env)
                    self.assertEqual(env.get('X3M_PERF'), perf)


if __name__ == '__main__':
    unittest.main()
