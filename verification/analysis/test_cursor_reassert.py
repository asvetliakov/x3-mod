"""Host checks of --cursor-reassert and the --window-trace pure parts.

The state machine (src/proxy/cursor_reassert_core.h: arming, the 120-frame
window, the gates, fire once, disarm on mismatch) and the WM_SETCURSOR summary
rule (src/proxy/window_trace_core.h) compiled on the host and compared with
their Python twins over named and seeded random scripts; the production wiring
of the hooks (thread hooks, LightCallBoundary, removal at device destruction
and DLL detach, no forbidden calls) and the Wine runner's parser. No Wine.
"""
import importlib.util
import random
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WINDOW_FRAMES = 120
WM_ACTIVATE, WM_ACTIVATEAPP = 0x0006, 0x001C
ARM_LAUNCH = 0x10000
GATES = ('same_thread', 'foreground', 'visible', 'cursor_hidden', 'pointer_in_client', 'clip_is_client')


class Twin:
    """Python twin of core::Machine / observe / present / after_fire."""

    def __init__(self):
        self.state, self.frames_left, self.armed_by = 'idle', 0, 0
        self.arms = self.fires = self.refusals = self.mismatches = 0

    def observe(self, message, wparam):
        arming = (message == WM_ACTIVATE and wparam & 0xFFFF) or (message == WM_ACTIVATEAPP and wparam)
        if not arming or not (self.state == 'idle' or (self.state == 'armed' and self.armed_by == ARM_LAUNCH)):
            return False
        self.state, self.frames_left, self.armed_by = 'armed', WINDOW_FRAMES, message
        self.arms += 1
        return True

    def launch(self):
        if self.state != 'idle':
            return False
        self.state, self.frames_left, self.armed_by = 'armed', WINDOW_FRAMES, ARM_LAUNCH
        self.arms += 1
        return True

    @staticmethod
    def failing(g):
        for name, reason in (('same_thread', 'thread'), ('foreground', 'foreground'), ('visible', 'not_visible'), ('cursor_hidden', 'cursor_visible')):
            if not g[name]:
                return reason
        if not g['pointer_in_client'] and not g['clip_is_client']:
            return 'pointer_outside'
        return None

    def present(self, g):
        if self.state != 'armed':
            return 'none', '-'
        reason = self.failing(g)
        if reason:
            if self.frames_left:
                self.frames_left -= 1
            if self.frames_left:
                return 'wait', reason
            self.state = 'idle'
            self.refusals += 1
            return 'refused', reason
        self.state, self.frames_left = 'idle', 0
        self.fires += 1
        return 'fired', 'gates_passed'

    def after_fire(self, up, down, flags_same, cursor_same):
        balanced = ((up, down) in ((0, -1), (1, 0))) and flags_same and cursor_same
        if not balanced:
            self.state = 'disabled'
            self.mismatches += 1
        return balanced

    def status(self):
        source = 'launch' if self.armed_by == ARM_LAUNCH else 'activate'
        return f'{self.state} {self.arms} {self.fires} {self.refusals} {self.mismatches} {source} {self.frames_left}'


def setcursor_twin(rows):
    """Python twin of core::setcursor_row over (now_ms, result, hit, trigger)."""
    state = dict(any=False, last=0, result=0, hit=0, trigger=0, suppressed=0)
    out = []
    for now, result, hit, trigger in rows:
        row = not state['any'] or (now - state['last']) % 2**32 >= 250 or (result, hit, trigger) != (state['result'], state['hit'], state['trigger'])
        if row:
            state.update(any=True, last=now, result=result, hit=hit, trigger=trigger)
        else:
            state['suppressed'] += 1
        out.append(f"{int(row)} {state['suppressed']}")
    return out


def script(seed):
    rng = random.Random(seed)
    events = []
    all_pass = dict.fromkeys(GATES, True)
    # Launch part: the launch arm fires once when the gates hold, waits and is refused when the window is
    # not foreground, is replaced (fresh window) by an arming message, and never arms over a pending arm.
    events += [('launch',), ('present', dict(all_pass, foreground=False)), ('present', all_pass), ('after', 0, -1, True, True), ('present', all_pass)]
    events += [('launch',)] + [('present', dict(all_pass, foreground=False))] * WINDOW_FRAMES
    events += [('launch',), ('present', dict(all_pass, foreground=False)), ('msg', WM_ACTIVATE, 0), ('msg', WM_ACTIVATE, 1), ('launch',), ('msg', WM_ACTIVATEAPP, 1)]
    events += [('present', all_pass), ('after', 0, -1, True, True), ('reset',)]
    launch = len(events)
    # Named part: inactive, arm, re-arm attempts, waits, fire once, refusal after the window, mismatch disables.
    events += [('msg', WM_ACTIVATE, 0), ('msg', WM_ACTIVATE, 1), ('msg', WM_ACTIVATEAPP, 1), ('msg', WM_ACTIVATE, 2)]
    events += [('present', dict(all_pass, foreground=False))] * 5 + [('present', all_pass), ('after', 0, -1, True, True), ('present', all_pass)]
    events += [('msg', WM_ACTIVATEAPP, 1)] + [('present', dict(all_pass, cursor_hidden=False))] * WINDOW_FRAMES + [('present', all_pass)]
    events += [('msg', WM_ACTIVATE, 1), ('present', dict(all_pass, pointer_in_client=False, clip_is_client=True)), ('after', 1, 0, True, True)]
    events += [('msg', WM_ACTIVATE, 1), ('present', dict(all_pass, pointer_in_client=False)), ('present', dict(all_pass, same_thread=False))]
    events += [('present', dict(all_pass, visible=False)), ('present', all_pass), ('after', 0, -1, True, False)]
    events += [('msg', WM_ACTIVATE, 1), ('present', all_pass)]
    named = len(events)
    # Seeded random part on a fresh machine (the harness resets at 'reset').
    events.append(('reset',))
    for _ in range(4000):
        roll = rng.random()
        if roll < 0.004:
            events.append(('launch',))
        elif roll < 0.03:
            events.append(('msg', rng.choice((WM_ACTIVATE, WM_ACTIVATEAPP, 0x0007)), rng.choice((0, 1, 2, 0x10001))))
        elif roll < 0.035:
            events.append(('after', rng.choice((0, 1, -1)), rng.choice((-1, 0, -2)), rng.random() < 0.95, rng.random() < 0.95))
        else:
            events.append(('present', {name: rng.random() < 0.9 for name in GATES}))
    return events, named, launch


def twin_run(events):
    machine, out = Twin(), []
    for event in events:
        if event[0] == 'reset':
            machine = Twin()
            out.append('reset')
        elif event[0] == 'msg':
            armed = machine.observe(event[1], event[2])
            out.append(f'msg {int(armed)} {machine.status()}')
        elif event[0] == 'launch':
            armed = machine.launch()
            out.append(f'launch {int(armed)} {machine.status()}')
        elif event[0] == 'present':
            step, reason = machine.present(event[1])
            out.append(f'present {step} {reason} {machine.status()}')
        else:
            balanced = machine.after_fire(event[1], event[2], event[3], event[4])
            out.append(f'after {int(balanced)} {machine.status()}')
    return out


HEAD = r'''
#include "cursor_reassert_core.h"
#include "window_trace_core.h"
#include <cstdio>
using namespace x3m::cursor_reassert::core;
static Machine m;
static const char* state_name(State s) { return s == State::idle ? "idle" : s == State::armed ? "armed" : "disabled"; }
static void status() { std::printf(" %s %u %u %u %u %s %u\n", state_name(m.state), m.arms, m.fires, m.refusals, m.mismatches, arm_source(m.armed_by), m.frames_left); }
static void msg(unsigned message, unsigned wparam) { const bool a = observe(m, message, wparam); std::printf("msg %d", int(a)); status(); }
static void launch() { const bool a = arm_launch(m); std::printf("launch %d", int(a)); status(); }
static void pres(bool t, bool f, bool v, bool h, bool p, bool c) {
    Gates g; g.same_thread = t; g.foreground = f; g.visible = v; g.cursor_hidden = h; g.pointer_in_client = p; g.clip_is_client = c;
    const Decision d = present(m, g);
    std::printf("present %s %s", step_name(d.step), d.reason ? d.reason : "-"); status();
}
static void after(int up, int down, bool flags_same, bool cursor_same) {
    Sequence s; s.up = up; s.down = down;
    int a = 1, b = 2;
    const bool ok = balanced(s, 1u, flags_same ? 1u : 0u, &a, cursor_same ? static_cast<const void*>(&a) : static_cast<const void*>(&b));
    after_fire(m, ok);
    std::printf("after %d", int(ok)); status();
}
static x3m::window_trace::core::SetCursorSummary sc;
static void setcursor(unsigned now, unsigned result, unsigned hit, unsigned trigger) {
    const bool row = x3m::window_trace::core::setcursor_row(sc, now, result, hit, trigger);
    std::printf("sc %d %u\n", int(row), sc.suppressed);
}
int main() {
'''


def harness(events, setcursor_rows):
    lines = [HEAD]
    for event in events:
        if event[0] == 'reset':
            lines.append('    m = Machine(); std::printf("reset\\n");')
        elif event[0] == 'msg':
            lines.append(f'    msg({event[1]}u, {event[2]}u);')
        elif event[0] == 'launch':
            lines.append('    launch();')
        elif event[0] == 'present':
            lines.append('    pres(%s);' % ', '.join('1' if event[1][g] else '0' for g in GATES))
        else:
            lines.append(f'    after({event[1]}, {event[2]}, {int(event[3])}, {int(event[4])});')
    for now, result, hit, trigger in setcursor_rows:
        lines.append(f'    setcursor({now}u, {result}u, {hit}u, {trigger}u);')
    lines.append('    return 0;\n}\n')
    return '\n'.join(lines)


def setcursor_script():
    rows = [(1000, 1, 1, 0x200), (1010, 1, 1, 0x200), (1249, 1, 1, 0x200), (1250, 1, 1, 0x200), (1260, 0, 1, 0x200), (1270, 0, 2, 0x200),
            (1280, 0, 2, 0x201), (1290, 0, 2, 0x201), (0xFFFFFFF0, 1, 1, 0x200), (0x00000100, 1, 1, 0x200)]
    rng = random.Random(7)
    now = 5000
    for _ in range(500):
        now = (now + rng.choice((1, 5, 16, 100, 300))) % 2**32
        rows.append((now, rng.choice((1, 1, 1, 0)), rng.choice((1, 1, 2)), 0x200))
    return rows


class CursorReassertCore(unittest.TestCase):
    def test_launch_twin(self):
        events, _, launch = script(1)
        out = twin_run(events[:launch])
        self.assertEqual(out[0], 'launch 1 armed 1 0 0 0 launch 120')              # the launch arm
        self.assertEqual(out[1], 'present wait foreground armed 1 0 0 0 launch 119')
        self.assertEqual(out[2], 'present fired gates_passed idle 1 1 0 0 launch 0')  # fires once when the gates hold
        self.assertEqual(out[4], 'present none - idle 1 1 0 0 launch 0')
        self.assertEqual(sum(1 for line in out[6:126] if line.startswith('present wait foreground')), WINDOW_FRAMES - 1)
        self.assertEqual(out[125], 'present refused foreground idle 2 1 1 0 launch 0')  # not foreground: refused, never fired
        self.assertEqual(out[128], 'msg 0 armed 3 1 1 0 launch 119')                   # WM_ACTIVATE inactive keeps the launch arm
        self.assertEqual(out[129], 'msg 1 armed 4 1 1 0 activate 120')                 # an activation replaces it, fresh window
        self.assertEqual(out[130], 'launch 0 armed 4 1 1 0 activate 120')              # a launch never arms over a pending arm
        self.assertEqual(out[131], 'msg 0 armed 4 1 1 0 activate 120')
        self.assertEqual(out[132], 'present fired gates_passed idle 4 2 1 0 activate 0')

    def test_named_twin(self):
        events, named, launch = script(1)
        out = twin_run(events[:named])[launch:]
        self.assertEqual(out[1], 'msg 1 armed 1 0 0 0 activate 120')          # WM_ACTIVATE active arms
        self.assertEqual(out[2], 'msg 0 armed 1 0 0 0 activate 120')          # WM_ACTIVATEAPP while armed does not
        self.assertEqual(out[9], 'present fired gates_passed idle 1 1 0 0 activate 0')
        self.assertEqual(out[11], 'present none - idle 1 1 0 0 activate 0')  # fire once
        refusal = [line for line in out if line.startswith('present refused')]
        self.assertEqual(refusal[0], 'present refused cursor_visible idle 2 1 1 0 activate 0')
        self.assertEqual(sum(1 for line in out if line.startswith('present wait cursor_visible')), WINDOW_FRAMES - 1)
        self.assertTrue(out[-3].startswith('after 0 disabled'))  # handle changed -> disabled
        self.assertEqual(out[-2], 'msg 0 disabled 4 3 1 1 activate 0')       # never re-arms
        self.assertEqual(out[-1], 'present none - disabled 4 3 1 1 activate 0')

    def test_compiled_core_matches_twin(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        events, _, _ = script(1)
        rows = setcursor_script()
        with tempfile.TemporaryDirectory(prefix='x3-cursor-reassert-') as temporary:
            directory = Path(temporary)
            (directory / 'harness.cpp').write_text(harness(events, rows))
            build = subprocess.run([compiler, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/proxy'),
                                    str(directory / 'harness.cpp'), '-o', str(directory / 'core')], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(directory / 'core')], capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stderr)
        output = run.stdout.splitlines()
        machine_out = [line for line in output if not line.startswith('sc ')]
        expected = twin_run(events)
        self.assertEqual(len(machine_out), len(expected))
        mismatches = [(i, a, b) for i, (a, b) in enumerate(zip(machine_out, expected)) if a != b]
        self.assertEqual(mismatches[:5], [])
        fired = sum(1 for line in machine_out if line.startswith('present fired'))
        self.assertGreater(fired, 3)
        sc_out = [line[3:] for line in output if line.startswith('sc ')]
        self.assertEqual(sc_out, setcursor_twin(rows))

    def test_cores_are_windows_free(self):
        for name in ('cursor_reassert_core.h', 'window_trace_core.h'):
            self.assertNotIn('#include <windows.h>', (ROOT / 'src/proxy' / name).read_text())


class WindowHookWiring(unittest.TestCase):
    def test_hooks_and_lifetime(self):
        trace = (ROOT / 'src/proxy/window_trace.cpp').read_text()
        for needle in ('SetWindowsHookExW(WH_CALLWNDPROC, x3m_window_hook_call, nullptr, thread)',
                       'SetWindowsHookExW(WH_CALLWNDPROCRET, x3m_window_hook_ret, nullptr, thread)',
                       'SetWindowsHookExW(WH_GETMESSAGE, x3m_window_hook_get, nullptr, thread)',
                       'window_thread != thread', 'cursor_reassert::refuse(', 'UnhookWindowsHookEx', 'InterlockedExchangePointer',
                       'x3m::LightCallBoundary cpu;', 'cpu.before_original();', 'cpu.after_original();', 'CallNextHookEx(nullptr, code, wparam, lparam)',
                       'GetCurrentThreadId() != hook_thread_'):
            self.assertIn(needle, trace)
        self.assertEqual(trace.count('x3m::LightCallBoundary cpu;'), 3)
        # The hook procedures never log: the log() calls all sit before the extern "C" definitions.
        hooks = trace[trace.index('extern "C" LRESULT CALLBACK x3m_window_hook_call(int code, WPARAM wparam, LPARAM lparam) {'):]
        self.assertNotIn('log(', hooks)
        for forbidden in (r'SetWindowLong', r'SetForegroundWindow', r'(?<!Get)ClipCursor\(', r'SetCursorPos\(', r'ShowCursor\(', r'SendMessage', r'wine_'):
            self.assertIsNone(re.search(forbidden, trace), forbidden)
        reassert = (ROOT / 'src/proxy/cursor_reassert.cpp').read_text()
        for forbidden in (r'SetForegroundWindow', r'(?<!Get)ClipCursor\(', r'SetCursorPos', r'SendMessage', r'while \(', r'for \(', r'wine_'):
            self.assertIsNone(re.search(forbidden, reassert), forbidden)
        self.assertEqual(reassert.count('ShowCursor('), 1)  # only inside the balanced sequence's stand-in
        core = (ROOT / 'src/proxy/cursor_reassert_core.h').read_text()
        self.assertIn('s.up = api.show_cursor(true);', core)
        self.assertIn('s.down = api.show_cursor(false);', core)
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        for needle in ('window_trace::attach(window,devices.at(d)->id);', 'window_trace::detach(devices.at(d)->id);forget_cached_device();devices.erase(d);',
                       'window_trace::present(ctx.stats.window,ctx.id,ctx.frame,ctx.stats.markers);',
                       'window_trace::initialize(telemetry::enabled(),&loading_trace::light::cursor_drain);', 'loading_trace::light::cursor_observe(true);'):
            self.assertIn(needle, capture)
        present = capture[capture.index('HRESULT WINAPI present(IDirect3DDevice9* d'):]
        self.assertLess(present.index('cpu.after_original();'), present.index('window_trace::present('))
        loader = (ROOT / 'src/proxy/loader.cpp').read_text()
        detach = loader[loader.index('reason == DLL_PROCESS_DETACH'):]
        self.assertIn('x3m::window_trace::shutdown();', detach)
        audit = (ROOT / 'verification/probe/check_no_x87.py').read_text()
        for root in ('_x3m_window_hook_call@12', '_x3m_window_hook_ret@12', '_x3m_window_hook_get@12'):
            self.assertIn(f"'{root}'", audit)
        light = (ROOT / 'src/proxy/loading_trace_light.cpp').read_text()
        self.assertIn('if(cursor_observing)cursor_record(0,', light)
        for needle in ('__atomic_thread_fence(__ATOMIC_RELEASE);', '__atomic_thread_fence(__ATOMIC_ACQUIRE);', 'InterlockedCompareExchange(&cursor_lock,1,0)',
                       'counts->dropped=dropped;'):
            self.assertIn(needle, light)
        self.assertIn('if (reserved == nullptr) x3m::window_trace::shutdown();', loader)
        for needle in ('device != hook_device_) return;', 'reason=foreign_release', 'orphaned_by_foreign_release', 'reason=already_hooked',
                       'GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN', 'if (pinned_) return;', 'pinned=%u'):
            self.assertIn(needle, trace)
        self.assertIn('if(cursor_observing)cursor_record(1,', light)
        record = light[light.index('void cursor_record('):light.index('void cursor_observe(')]
        self.assertTrue(record.startswith('void cursor_record(uint32_t op,uint32_t a,uint32_t b,uint32_t result) noexcept {\n    const DWORD error=GetLastError();'))
        self.assertIn('    SetLastError(error);\n}', record)


class CursorReassertRunnerParser(unittest.TestCase):
    def test_parse_and_accept(self):
        spec = importlib.util.spec_from_file_location('run_cursor_reassert', ROOT / 'verification/probe/run_cursor_reassert.py')
        module = importlib.util.module_from_spec(spec)
        sys.path.insert(0, str(ROOT / 'verification/probe'))
        try:
            spec.loader.exec_module(module)
        finally:
            sys.path.pop(0)
        checks = '\n'.join(f'CHECK c{i} PASS' for i in range(module.EXPECTED_CHECKS))
        text = ('COUNT initial_after_hide=-1\nREAL_GATES same_thread=1 foreground=1 cursor_flags=0 show_count=-1 step=not_fired\n'
                'LOG cursor_reassert frame=221 hwnd=0001 action=refused reason=foreground window_frames=120 refusals=1 armed_by=launch\n'
                'LOG cursor_reassert frame=222 hwnd=0001 action=fired armed_by=launch up=0 down=-1 balanced=1 disabled=0 message=none\n'
                'LOG cursor_reassert frame=230 hwnd=0001 action=fired armed_by=activate up=0 down=-1 balanced=1 disabled=0 message=WM_ACTIVATE\n'
                'LOG cursor_reassert frame=350 hwnd=0001 action=refused reason=foreground window_frames=120 refusals=2 armed_by=activate\n'
                'LOG cursor_reassert frame=351 hwnd=0001 action=fired armed_by=activate up=-1 down=-2 balanced=0 disabled=1 message=WM_ACTIVATE\n'
                f'{checks}\nRESULT checks={module.EXPECTED_CHECKS} failed=0 PASS\n')
        report = module.parse(text)
        module.accept(report)
        with self.assertRaises(AssertionError):
            module.accept(module.parse(text.replace('up=0 down=-1 balanced=1', 'up=1 down=0 balanced=1')))
        with self.assertRaises(AssertionError):  # the launch firing must be first and armed_by=launch
            module.accept(module.parse(text.replace('armed_by=launch up=0', 'armed_by=activate up=0')))


if __name__ == '__main__':
    unittest.main()
