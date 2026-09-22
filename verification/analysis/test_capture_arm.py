"""Host checks of the delayed F8 capture (X3M_CAPTURE_DELAY, launcher --capture-delay).

F8 cancels the game's SETA time compression, so an immediate burst can never show
the compressed case; with a delay the press only arms the capture and the user
re-engages SETA while it counts down. Covered here: the portable state machine
src/proxy/capture_arm_core.h compiled with the host compiler, its wiring in
src/proxy/capture.cpp (the arming log line, the Reset clear, the unchanged
capture_start path) and the launcher's env mapping (--dry-run only, never a
launch). No Wine, no game.
"""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]

HARNESS = r'''
#include "capture_arm_core.h"
#include <cstdio>
using namespace x3m::capture_arm::core;
int main() {
    unsigned failures = 0;
    auto check = [&](bool ok, const char* what) { if (!ok) { ++failures; std::printf("FAIL %s\n", what); } };
    Pending p;
    // delay 0: an edge starts the burst at once and nothing is ever pending.
    check(step(p, true, 100, 0) == Action::start && !p.armed, "delay 0 starts at once");
    check(step(p, false, 101, 0) == Action::none && !p.armed, "delay 0, no edge");
    // A non-zero delay arms; the burst starts exactly delay frames later.
    clear(p);
    check(step(p, true, 100, 5) == Action::arm && p.armed && p.start_frame == 105, "edge arms at frame+delay");
    for (std::uint64_t f = 101; f < 105; ++f) check(step(p, false, f, 5) == Action::none && p.armed, "still pending");
    check(frames_left(p, 101) == 4 && frames_left(p, 104) == 1, "frames left");
    check(step(p, false, 105, 5) == Action::start && !p.armed && frames_left(p, 105) == 0, "fires on the start frame");
    check(step(p, false, 106, 5) == Action::none, "fires once");
    // A second F8 during the delay neither re-arms nor cancels.
    clear(p);
    check(step(p, true, 10, 4) == Action::arm && p.start_frame == 14, "armed at 14");
    check(step(p, true, 11, 4) == Action::none && p.armed && p.start_frame == 14, "second press does nothing");
    check(step(p, true, 12, 4) == Action::none && p.start_frame == 14, "and does not cancel");
    check(step(p, false, 14, 4) == Action::start && !p.armed, "still fires at 14");
    // An edge on the firing frame is swallowed by the fire, not re-armed.
    clear(p);
    step(p, true, 0, 3);
    check(step(p, true, 3, 3) == Action::start && !p.armed, "edge on the firing frame fires");
    // A skipped frame still fires instead of arming forever.
    clear(p);
    step(p, true, 0, 3);
    check(step(p, false, 9, 3) == Action::start && !p.armed, "late frame fires");
    // Reset: clear() drops the pending state, so the next edge arms afresh.
    clear(p);
    step(p, true, 50, 7);
    clear(p);
    check(!p.armed && p.start_frame == 0 && step(p, false, 57, 7) == Action::none, "clear drops the pending capture");
    check(step(p, true, 60, 7) == Action::arm && p.start_frame == 67, "arms again after clear");
    check(delay_max == 36000, "delay bound");
    std::printf("capture_arm_core checks_failed=%u\n", failures);
    return failures ? 1 : 0;
}
'''


def load_manage():
    spec = importlib.util.spec_from_file_location('capture_arm_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class CaptureArmCore(unittest.TestCase):
    def test_core_compiled(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-capture-arm-host-') as temporary:
            directory = Path(temporary)
            (directory / 'harness.cpp').write_text(HARNESS)
            executable = directory / 'capture_arm_host'
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/proxy'),
                                    str(directory / 'harness.cpp'), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertEqual(run.stdout, 'capture_arm_core checks_failed=0\n')


class CaptureArmWiring(unittest.TestCase):
    """The Present path and Reset in src/proxy/capture.cpp."""

    @classmethod
    def setUpClass(cls):
        cls.source = (ROOT / 'src/proxy/capture.cpp').read_text()

    def test_present_uses_the_core_and_keeps_the_capture_start_path(self):
        self.assertIn('const auto arm=capture_arm::core::step(ctx.capture_pending,down&&!ctx.key_down,ctx.frame,capture_delay);', self.source)
        self.assertIn('if (arm==capture_arm::core::Action::start || (capture_count && ctx.frame==capture_start)) '
                      'ctx.remaining=capture_count ? capture_count : 1;', self.source)
        # The existing per-capture-frame line is unchanged.
        self.assertIn('if (ctx.capture) log("frame_begin device=%llu frame=%llu",ctx.id,ctx.frame);', self.source)

    def test_one_arming_line(self):
        self.assertEqual(self.source.count('capture_armed device=%llu frame=%llu start_frame=%llu delay=%u'), 1)
        self.assertIn('if (arm==capture_arm::core::Action::arm)', self.source)

    def test_reset_clears_the_pending_capture(self):
        self.assertIn('ctx.capture=false; ctx.remaining=0;capture_arm::core::clear(ctx.capture_pending);', self.source)

    def test_the_setting_is_read_once_at_attach_and_bounded(self):
        line = next(l for l in self.source.splitlines() if 'GetEnvironmentVariableW(L"X3M_CAPTURE_DELAY"' in l)
        self.assertIn('const DWORD length=GetEnvironmentVariableW(L"X3M_CAPTURE_DELAY",setting,32);', line)
        # A return of 32 or more is truncation: the 32-wchar buffer then holds undefined
        # content (possibly the previous X3M_CAPTURE_FRAMES text), so it must be refused,
        # and an empty parse (stop==setting) must not count as a value either.
        self.assertIn('if(length>0&&length<32){', self.source)
        self.assertIn("if(stop!=setting&&*stop==L'\\0'&&v<=capture_arm::core::delay_max) capture_delay=unsigned(v);", self.source)
        self.assertIn('unsigned capture_delay = 0;', self.source)


class CaptureArmLaunchOption(unittest.TestCase):
    def launch(self, directory, *args, inherited=None):
        module = load_manage()
        game = Path(directory) / 'game'
        game.mkdir(exist_ok=True)
        (game / 'X3AP.exe').touch()
        wine = Path(directory) / 'wine'
        wine.touch()
        argv = ['manage.py', 'launch', '--dry-run', '--vanilla', '--game-dir', str(game), *args]
        output, error = io.StringIO(), io.StringIO()
        with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', wine), \
                mock.patch.dict(module.os.environ, inherited or {}), \
                mock.patch.object(module.subprocess, 'call', side_effect=AssertionError('must never launch')), \
                contextlib.redirect_stdout(output), contextlib.redirect_stderr(error):
            try:
                module.main()
            except SystemExit as exit_error:
                return exit_error.code, output.getvalue(), error.getvalue()
        return 0, output.getvalue(), error.getvalue()

    def env(self, directory, *args, inherited=None):
        code, output, error = self.launch(directory, *args, inherited=inherited)
        self.assertEqual(code, 0, error)
        return json.loads(output)['env']

    def test_option_maps_to_the_variable(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertEqual(self.env(directory, '--capture-delay', '600')['X3M_CAPTURE_DELAY'], '600')

    def test_default_omits_it_even_when_inherited(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertNotIn('X3M_CAPTURE_DELAY', self.env(directory))
            self.assertNotIn('X3M_CAPTURE_DELAY', self.env(directory, inherited={'X3M_CAPTURE_DELAY': '600'}))
            # An explicit 0 is the documented immediate case, so it drops the variable too.
            self.assertNotIn('X3M_CAPTURE_DELAY', self.env(directory, '--capture-delay', '0', inherited={'X3M_CAPTURE_DELAY': '600'}))

    def test_out_of_band_values_are_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            for value in ('-1', '36001'):
                self.assertEqual(self.launch(directory, '--capture-delay', value)[0], 2, value)
            self.assertEqual(self.env(directory, '--capture-delay', '36000')['X3M_CAPTURE_DELAY'], '36000')

    def test_help_names_frames_and_the_seta_cancel(self):
        text = (ROOT / 'tools/manage.py').read_text()
        start = text.index("'--capture-delay'")
        help_text = text[start:text.index("parser.add_argument('--direct'", start)]
        self.assertIn('X3M_CAPTURE_DELAY', help_text)
        self.assertIn('frames', help_text)
        self.assertIn('60 per second', help_text)
        self.assertIn('SETA', help_text)


if __name__ == '__main__':
    unittest.main()
