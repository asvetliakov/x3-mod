"""Host checks of --window-monitor-rect (src/proxy/window_mode_core.h).

The pure predicate compiled on the host against its Python twin over a case
grid (every refusal reason, the noop, the work-origin move, a top-docked
taskbar, the negative virtual-desktop origin, Retina doubling), the production
wiring (create_before / reset_before, the SetWindowPos flags, the row) and
the Wine runner's parser. No Wine, no game.
"""
import importlib.util
import itertools
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from source_text import source_text

ROOT = Path(__file__).resolve().parents[2]
WS_POPUP, WS_VISIBLE, WS_CLIPSIBLINGS, WS_CAPTION, WS_THICKFRAME = 0x80000000, 0x10000000, 0x04000000, 0x00C00000, 0x00040000
WS_EX_CLIENTEDGE, WS_EX_TOOLWINDOW = 0x00000200, 0x00000080
DECORATED_EX = 0x00000001 | 0x00000100 | 0x00000200 | 0x00020000
GAME_STYLE = WS_POPUP | WS_VISIBLE | WS_CLIPSIBLINGS  # 94000000, run320 telemetry_window


def twin(case):
    """Python twin of core::decide: (action, reason)."""
    width = lambda r: r[2] - r[0]
    height = lambda r: r[3] - r[1]
    if not case['windowed']:
        return 'refused', 'fullscreen'
    if not case['single_window']:
        return 'refused', 'foreign_window'
    if not case['window_valid']:
        return 'refused', 'no_window'
    if not case['top_level']:
        return 'refused', 'child_window'
    if not case['same_thread']:
        return 'refused', 'foreign_thread'
    if not case['style'] & WS_POPUP:
        return 'refused', 'not_popup'
    if case['style'] & (WS_CAPTION | WS_THICKFRAME) or case['exstyle'] & DECORATED_EX:
        return 'refused', 'decorated'
    monitor, work, window = case['monitor'], case['work'], case['window']
    if not case['monitor_known'] or width(monitor) <= 0 or height(monitor) <= 0:
        return 'refused', 'no_monitor'
    if case['bb'] != (width(monitor), height(monitor)):
        return 'refused', 'backbuffer_mismatch'
    if window == monitor:
        return 'noop', 'at_monitor_rect'
    if window == (work[0], work[1], work[0] + width(monitor), work[1] + height(monitor)):
        return 'moved', 'work_origin'
    return 'refused', 'not_work_origin'


def base(**changes):
    case = dict(windowed=True, single_window=True, window_valid=True, top_level=True, same_thread=True, style=GAME_STYLE, exstyle=0,
                monitor_known=True, monitor=(0, 0, 5120, 1440), work=(0, 31, 5120, 1440), window=(0, 31, 5120, 1471), bb=(5120, 1440))
    case.update(changes)
    return case


# Named cases: (label, case, expected action, expected reason).
NAMED = [
    ('run320 layout', base(), 'moved', 'work_origin'),
    ('already at the monitor rect', base(window=(0, 0, 5120, 1440)), 'noop', 'at_monitor_rect'),
    ('windows bottom taskbar', base(work=(0, 0, 1920, 1040), monitor=(0, 0, 1920, 1080), window=(0, 0, 1920, 1080), bb=(1920, 1080)), 'noop', 'at_monitor_rect'),
    ('windows top taskbar', base(work=(0, 40, 1920, 1080), monitor=(0, 0, 1920, 1080), window=(0, 40, 1920, 1120), bb=(1920, 1080)), 'moved', 'work_origin'),
    ('negative origin display', base(monitor=(-1512, 0, 0, 982), work=(-1512, 25, 0, 982), window=(-1512, 25, 0, 1007), bb=(1512, 982)), 'moved', 'work_origin'),
    ('retina doubling', base(monitor=(0, 0, 3024, 1964), work=(0, 74, 3024, 1964), window=(0, 74, 3024, 2038), bb=(3024, 1964)), 'moved', 'work_origin'),
    ('fullscreen device', base(windowed=False), 'refused', 'fullscreen'),
    ('device window differs from focus', base(single_window=False), 'refused', 'foreign_window'),
    ('no window', base(window_valid=False), 'refused', 'no_window'),
    ('child window', base(top_level=False), 'refused', 'child_window'),
    ('foreign thread', base(same_thread=False), 'refused', 'foreign_thread'),
    ('decorated game mode 14ca0000', base(style=0x14CA0000), 'refused', 'not_popup'),
    ('popup with caption', base(style=GAME_STYLE | WS_CAPTION), 'refused', 'decorated'),
    ('popup with thick frame', base(style=GAME_STYLE | WS_THICKFRAME), 'refused', 'decorated'),
    ('popup with client edge', base(exstyle=WS_EX_CLIENTEDGE), 'refused', 'decorated'),
    ('tool window is not decoration', base(exstyle=WS_EX_TOOLWINDOW), 'moved', 'work_origin'),
    ('monitor unknown', base(monitor_known=False), 'refused', 'no_monitor'),
    ('smaller back buffer', base(bb=(2560, 1440)), 'refused', 'backbuffer_mismatch'),
    ('back buffer zero (client-sized)', base(bb=(0, 0)), 'refused', 'backbuffer_mismatch'),
    ('moved by the user', base(window=(100, 31, 5220, 1471)), 'refused', 'not_work_origin'),
    ('work-origin but smaller window', base(window=(0, 31, 5120, 1440)), 'refused', 'not_work_origin'),
]

HARNESS_HEAD = r'''
#include "window_mode_core.h"
#include <cstdio>
using namespace x3m::window_mode::core;
static Rect R(int l, int t, int r, int b) { Rect x; x.left = l; x.top = t; x.right = r; x.bottom = b; return x; }
static void emit(bool windowed, bool single, bool valid, bool top, bool thread, unsigned style, unsigned exstyle, bool known,
                 Rect monitor, Rect work, Rect window, unsigned bw, unsigned bh) {
    Input in; in.windowed = windowed; in.single_window = single; in.window_valid = valid; in.top_level = top; in.same_thread = thread;
    in.style = style; in.exstyle = exstyle; in.monitor_known = known; in.monitor = monitor; in.work = work; in.window = window;
    in.backbuffer_width = bw; in.backbuffer_height = bh;
    const Decision d = decide(in);
    std::printf("%s %s\n", action_name(d.action), d.reason);
}
int main() {
'''


def cases():
    """The named cases plus a grid over the boolean gates and the geometry."""
    out = [case for _, case, _, _ in NAMED]
    geometries = [dict(), dict(window=(0, 0, 5120, 1440)), dict(window=(3, 31, 5123, 1471)), dict(bb=(5120, 1409)),
                  dict(monitor=(-1512, 0, 0, 982), work=(-1512, 25, 0, 982), window=(-1512, 25, 0, 1007), bb=(1512, 982))]
    for flags in itertools.product((True, False), repeat=5):
        for style in (GAME_STYLE, 0x14CA0000, GAME_STYLE | WS_THICKFRAME):
            for geometry in geometries:
                out.append(base(windowed=flags[0], single_window=flags[1], window_valid=flags[2], top_level=flags[3], same_thread=flags[4], style=style, **geometry))
    return out


def harness(all_cases):
    rect = lambda r: 'R(%d, %d, %d, %d)' % r
    lines = [HARNESS_HEAD]
    for c in all_cases:
        lines.append('    emit(%d, %d, %d, %d, %d, 0x%08xu, 0x%08xu, %d, %s, %s, %s, %du, %du);' % (
            c['windowed'], c['single_window'], c['window_valid'], c['top_level'], c['same_thread'], c['style'], c['exstyle'], c['monitor_known'],
            rect(c['monitor']), rect(c['work']), rect(c['window']), c['bb'][0], c['bb'][1]))
    lines.append('    return 0;\n}\n')
    return '\n'.join(lines)


class WindowModeCore(unittest.TestCase):
    def test_named_cases_twin(self):
        for label, case, action, reason in NAMED:
            with self.subTest(label):
                self.assertEqual(twin(case), (action, reason))
        self.assertEqual({reason for _, case, _, reason in NAMED if twin(case)[0] == 'refused'},
                         {'fullscreen', 'foreign_window', 'no_window', 'child_window', 'foreign_thread', 'not_popup', 'decorated', 'no_monitor',
                          'backbuffer_mismatch', 'not_work_origin'})

    def test_compiled_core_matches_twin(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        all_cases = cases()
        with tempfile.TemporaryDirectory(prefix='x3-window-mode-') as temporary:
            directory = Path(temporary)
            (directory / 'harness.cpp').write_text(harness(all_cases))
            build = subprocess.run([compiler, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/proxy'),
                                    str(directory / 'harness.cpp'), '-o', str(directory / 'core')], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(directory / 'core')], capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stderr)
        compiled = [tuple(line.split()) for line in run.stdout.splitlines()]
        self.assertEqual(len(compiled), len(all_cases))
        mismatches = [(i, compiled[i], twin(c)) for i, c in enumerate(all_cases) if compiled[i] != twin(c)]
        self.assertEqual(mismatches, [])
        self.assertGreater(sum(1 for c in compiled if c[0] == 'moved'), 0)

    def test_core_is_windows_free(self):
        header = source_text(ROOT / 'src/proxy/window_mode_core.h')
        self.assertNotIn('#include <windows.h>', header)
        for needle in ('ws_popup = 0x80000000u', 'ws_caption = 0x00c00000u', 'ws_thickframe = 0x00040000u'):
            self.assertIn(needle, header)


class WindowModeWiring(unittest.TestCase):
    def test_production_wiring(self):
        capture = source_text(ROOT / 'src/proxy/capture.cpp')
        self.assertIn('window_mode::apply("create_before",window,p->hDeviceWindow,p->Windowed!=FALSE,p->BackBufferWidth,p->BackBufferHeight);', capture)
        self.assertIn('window_mode::apply("reset_before",ctx.stats.focus_window,p->hDeviceWindow,p->Windowed!=FALSE,p->BackBufferWidth,p->BackBufferHeight);', capture)
        # Both before the native call, inside the hook's CpuCallBoundary.
        create = capture[capture.index('HRESULT WINAPI create_device('):]
        self.assertLess(create.index('window_mode::apply("create_before"'), create.index('cpu.before_original();'))
        reset = capture[capture.index('HRESULT reset_common('):]
        self.assertLess(reset.index('CpuCallBoundary cpu;'), reset.index('window_mode::apply("reset_before"'))
        self.assertLess(reset.index('window_mode::apply("reset_before"'), reset.index('cpu.before_original();'))
        module = source_text(ROOT / 'src/proxy/window_mode.cpp')
        for needle in ('SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER', 'GetAncestor(hwnd, GA_PARENT) == GetDesktopWindow()',
                       'GetWindowThreadProcessId(hwnd, nullptr)', 'MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST)', 'SetLastError(saved_error);',
                       'L"X3M_WINDOW_MONITOR_RECT"', 'L"X3M_WINDOW_MONITOR_RECT_DEFAULT"', 'log("window_mode phase=%s'):
            self.assertIn(needle, module)
        for forbidden in ('wine_', 'GetProcAddress', 'SetForegroundWindow', 'ClipCursor', 'ShowCursor'):
            self.assertNotIn(forbidden, module, forbidden)


class WindowModeRunnerParser(unittest.TestCase):
    def test_parse_and_accept(self):
        spec = importlib.util.spec_from_file_location('run_window_mode', ROOT / 'verification/probe/run_window_mode.py')
        module = importlib.util.module_from_spec(spec)
        sys.path.insert(0, str(ROOT / 'verification/probe'))
        try:
            spec.loader.exec_module(module)
        finally:
            sys.path.pop(0)
        checks = '\n'.join(f'CHECK c{i} PASS' for i in range(module.EXPECTED_CHECKS))
        text = (f'GEOMETRY monitor=0,0,5120,1440 work=0,31,5120,1440 offset=31\n{checks}\n'
                'SCREEN available=1 rows=40 fill_rows_top=31 fill_rows=40\n'
                f'RESULT checks={module.EXPECTED_CHECKS} failed=0 PASS\n')
        report = module.parse(text)
        module.accept(report)
        self.assertEqual(report['geometry']['offset'], 31)
        self.assertEqual(report['screen']['fill_rows_top'], 31)
        bad = module.parse(text.replace('CHECK c0 PASS', 'CHECK c0 FAIL'))
        with self.assertRaises(AssertionError):
            module.accept(bad)


if __name__ == '__main__':
    unittest.main()
