# Window mode and cursor fix: borderless under the menu bar, double cursor after alt-tab

Design note, 2026-09-25 (design agent, Fable). Decision for the main session
to ratify. Nothing implemented; no game or Wine run. Investigation state and
the rules this note obeys: [window-and-cursor.md](window-and-cursor.md),
[cursor-observations.md](../reverse-engineering/cursor-observations.md).
Evidence scripts and log one-liners:
`verification/results/window-mode-and-cursor/` (README lists the results).

Marks: [s] read from the EXE disassembly or the proxy source; [m] measured in
the run320 log or the bottle registry; [i] inferred (upstream Wine source
knowledge or reasoning), not verified on the installed CrossOver.

## 1. Facts

### 1.1 How the game creates its window [s]

Routine `0x4dac90(width, height, bpp, flags)`, called once from `0x4f95d0`
(caller `0x40332a`) with `flags = VideoFlags | 0xe0`, where `VideoFlags` is
the registry dword `HKCU\Software\EGOSOFT\X3AP\VideoFlags` read at
`0x4b7211` into `[[0x606f34]]`. Bottle value: `00,01,00,28` = `0x28000100` [m]
(`VideoWidth` 0x1400 = 5120, `VideoHeight` 0x5a0 = 1440, `VideoMode` 0x16).

| Step | Site | What it does |
| --- | --- | --- |
| RegisterClassA | 0x4dadd1 | style 3 (CS_HREDRAW/VREDRAW), wndproc `0x4d3620`, **hCursor NULL**, hbrBackground BLACK_BRUSH (or 6 with flag 0x2000), no extra bytes |
| Style choice | 0x4dad58..0x4dadb2 | flag 0x2000: `0x10cf0000` (overlapped, special mode); else flag **0x20000000: `0x90000000` = WS_POPUP\|WS_VISIBLE**; else flag 0x08000000 ? `0x10ca0000` (caption, sysmenu, minimize box) : `0x10000000` |
| CreateWindowExA | 0x4dae0d | exstyle 0, same string for class and title, style above, **x = 0, y = 0, 640 x 480**, no parent/menu, hInstance `[0x608aa8]`; handle to `[0x608ab0]` |
| SetCursor(NULL) | 0x4dae35 | only when flag 0x08000000 is clear |
| GetAdapterMonitor + GetMonitorInfoA | 0x4daee3, 0x4daeeb | `IDirect3D9::GetAdapterMonitor` (slot 15) of the chosen adapter, MONITORINFO on the stack (`cbSize` 0x28) |
| Target rect | 0x4daf1b..0x4daf4e | **left = rcWork.left, top = rcWork.top**, right = left + width, bottom = top + height (`[esp+0x5c]`/`[esp+0x60]` are `rcWork`; `rcMonitor` at `[esp+0x4c]`/`[esp+0x50]` is never read) |
| Centring | 0x4daf5a..0x4daff0 | flag 0x08000000 only, and only when the window is smaller than the work area (not our case: 1440 > 1409) |
| AdjustWindowRectEx | 0x4db006 | skipped when flag 0x20000000 is set |
| SetWindowPos | 0x4db040 | `(hwnd, HWND_TOP, left, top, right-left, bottom-top, SWP_NOZORDER)`, then GetClientRect into `[0x608ab4]` |
| Device | 0x4db058 -> `0x4d8f10` | creates the D3D device (the proxy's CreateDevice hook runs inside) |

The routine is re-entered on a mode change: `[0x608ab0] != 0` jumps to
`0x4dae3b`, so the work-area positioning and `SetWindowPos` run again with the
new size, followed by the device path (Reset). There is exactly one
`SetWindowPos` site in the EXE.

Bit semantics established by the two sessions: `0x08000000` = windowed
(2026-09-10 decorated session had style `14ca0000` [m]); `0x20000000` = popup
("borderless") style. The user's configuration has both: popup style,
windowed device. Which bit selects an exclusive-fullscreen device
(`Windowed = FALSE`) is not established (not needed here).

### 1.2 Window procedure `0x4d3620` [s]

- `WM_ACTIVATE` (6): `LOWORD(wParam)==WA_INACTIVE` -> `[0x608adc]=0`,
  `[state+0x484]=0`, call `0x4982b0` (input release); else both flags 1. Returns
  0 without DefWindowProc.
- `WM_ACTIVATEAPP` (0x1c): call `0x4d4950(!wParam)` (pause/resume), return 0.
- `WM_SETCURSOR` (0x20): if the video state exists and `[0x608adc]` (active):
  `GetCursorPos`, in the active branch accumulate the delta from the
  **screen** centre `(w/2, h/2)` into the game cursor, clamp to `[0,w]x[0,h]`,
  `SetCursor(NULL)`, and when the delta is non-zero `SetCursorPos(w/2, h/2)`
  in **screen** coordinates. Always returns 1 (handled); DefWindowProc never
  sees WM_SETCURSOR. In the inactive branch the game cursor is the absolute
  screen position, no SetCursor.
- `WM_SYSCOMMAND` (0x112): SC_KEYMENU/SC_SCREENSAVE/SC_MONITORPOWER family
  swallowed (returns 1), SC_CLOSE -> `0x401d60`.
- Mouse buttons 0x201..0x205 and non-client 0xa1..0xa5 handled; the rest to
  `DefWindowProcA` (0x4d3a79).

The game therefore assumes its client origin is the screen origin: cursor
clamping and the re-centre warp use the configured width/height as screen
coordinates. With the window at y = 31 the game-drawn cursor sits 31 px below
the host pointer (one of the "offset" rows in the hypotheses table of
window-and-cursor.md; [i], follows from the code, not observed).

### 1.3 Imports [s]

The EXE imports `CreateWindowExA`, `RegisterClassA`, `SetWindowPos`,
`AdjustWindowRectEx`, `GetMonitorInfoA`, `GetClientRect`, `GetWindowRect`,
`ShowWindow`, `SetCursor`, `SetCursorPos`, `GetCursorPos`, `DefWindowProcA`,
`DirectInput8Create`. It imports **neither `ShowCursor` nor `ClipCursor`**
(nor `SetWindowLongA`, `MoveWindow`, `SystemParametersInfoA`), and none of
those names occur as strings, so no GetProcAddress path exists. The clip
rectangle and the hidden cursor observed below are therefore set by a DLL,
not by the game: Wine's dinput mouse acquire in exclusive mode clips to the
client rectangle and calls `ShowCursor(FALSE)`, unacquire the reverse [i,
matches the observations note]. The game never calls the D3D9 device cursor
methods: 0 `telemetry_cursor_api` rows in run320 [m] (the proxy wraps slots
10/11/12 under `--telemetry`).

### 1.4 Presentation parameters and window state [m]

`create_before`/`create_after`: `windowed=1 width=5120 height=1440 format=21
count=1 msaa=0 swap_effect=1 (DISCARD) refresh=0 interval=1 auto_depth=1
depth_format=77 flags=00000003`, `focus_window = device_window = 000a0064`,
`device_creation_policy requested=00000052 effective=00000042`. No Reset in
run320. `telemetry_window` (4 rows, identical): `style=94000000`
(WS_POPUP|WS_VISIBLE|WS_CLIPSIBLINGS; CLIPSIBLINGS is added by CreateWindowEx
for top-level windows), `exstyle=0`, **`window_rect=0,31,5120,1471`**,
`client_rect=0,0,5120,1440`, `monitor=0,0,5120,1440`, `work=0,31,5120,1440`,
`render_thread = window_thread = 216`, foreground = focus = game.
`clip` alternates between `-1512,0,5120,1440` (the virtual desktop: a second
display of 1512 pt width sits left of the primary) and `0,31,5120,1440` (the
client rectangle clamped to the primary display). Cursor poll: `flags=1
cursor=0` at frame 0, `flags=0 cursor=0` afterwards.

So the window is 5120 x 1440 placed at the work-area origin (0, 31): the top
31 rows are the menu bar, the bottom 31 rows of the client area are
off-screen. This is the game's own `rcWork` choice (1.1), not a Wine
constraint. On native Windows `rcWork.top` is 0 unless the taskbar is docked
at the top, which is why the game's authors never saw it.

### 1.5 Mac driver rules [i]

Bottle: no `Software\Wine\Mac Driver` key in `user.reg`/`system.reg` [m], so
every driver option is at its default (Retina mode off, so Wine coordinates
are CG points; `WindowsFloatWhenInactive` default; displays not captured).
`cxbottle.conf` carries no window options [m]. Upstream winemac.drv
(`cocoa_window.m`) classifies a window as fullscreen when its **content
rectangle covers a whole screen** and it has no decorations; a fullscreen
window is exempt from `constrainFrameRect:toScreen:`, is raised above the
menu-bar level while the app is active, and the app controller hides the
menu bar and Dock (presentation options) while such a window is ordered in
on the main screen. A window that does not cover a screen is an ordinary
Cocoa window: Cocoa keeps it below the menu bar and the menu bar stays
visible. Our window (0,31)-(5120,1471) does not cover the screen, so it is
ordinary. Whether CrossOver 27.0's driver keeps these upstream rules is the
main unknown of part 2; the fixture in section 4 settles it without the game.

## 2. Menu bar

### 2.1 Recommendation: proxy repositions the game's window to the monitor rectangle

At `create_before` and `reset_before` (both hooks exist in
`src/proxy/capture.cpp`), when all of the following hold, call one
`SetWindowPos(hwnd, HWND_TOP, mon.left, mon.top, mon.w, mon.h,
SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOOWNERZORDER)`, `hwnd` = `hDeviceWindow`
(or the focus window when it is NULL):

1. `p->Windowed == TRUE`;
2. `hDeviceWindow` is NULL or equals the focus window (one window; measured);
   the window is top-level (`GetAncestor(GA_PARENT)` is the desktop);
3. `GetWindowThreadProcessId(hwnd) == GetCurrentThreadId()` (measured 216 =
   216; SetWindowPos from another thread would be a cross-thread send);
4. style has `WS_POPUP` and lacks `WS_CAPTION|WS_THICKFRAME`
   (the game's borderless style; a decorated window is left alone);
5. `MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST)` gives `rcMonitor` with
   `mon.w == BackBufferWidth && mon.h == BackBufferHeight`;
6. `GetWindowRect(hwnd) != rcMonitor` (else no-op, one row `action=noop`).

The decision is a pure function (`window_mode_core.h`: style, exstyle,
rect, monitor, back buffer, windowed, same-thread, same-window ->
moved/noop/refused reason) so a host test covers every branch. One
`window_mode` row per call: `phase= hwnd= style= exstyle= before= monitor=
work= backbuffer= action= result=` (SetWindowPos BOOL, LastError kept by the
existing `CpuCallBoundary`). Option: launcher `--window-monitor-rect` ->
`X3M_WINDOW_MONITOR_RECT=1`; nothing under `--vanilla`; the DLL default is
off until a flight accepts it, then the launcher sends it by default on
modded launches with the `_DEFAULT=1` marker pattern of
`--taa-sentinel-stabiliser`.

Why here: the game has finished its own positioning (`SetWindowPos` at
0x4db040 and `GetClientRect`) before it enters device creation, and the same
order holds on the mode-change path before Reset. The client size is
unchanged (the game caches the client rectangle, never the position), so no
game state goes stale, and wined3d sees the final rectangle at creation. It
touches only the window the game handed to D3D, with documented Win32 calls,
on the window's own thread.

Cost: zero per frame; two calls (GetWindowRect, MonitorFromWindow/
GetMonitorInfo) plus one SetWindowPos at CreateDevice/Reset.

Expected effect [i]: the window becomes (0,0)-(5120,1440), covers the screen,
the Mac driver classifies it fullscreen, hides the menu bar while the game is
active, and the bottom 31 rows of the client area come back. Side effect:
the game's screen-origin assumption (1.2) becomes true, removing the 31 px
offset between the game cursor and the host pointer.

Reset and alt-tab: on a resolution change the game repositions at the
work-area origin again and Resets; `reset_before` reapplies. On alt-tab the
game does not move the window; the Mac driver lowers a fullscreen window
behind other apps when inactive (`WindowsFloatWhenInactive` default) and
restores the level on activation [i]; the 2026-09-12 "window behind the
desktop at launch" observation belongs to the same level logic and is a
watch item for the flight. Device loss does not apply to a windowed device.

DPI/Retina and monitors: the predicate uses the monitor rectangle of the
window's own monitor in Wine's coordinate space, so it is right at scale 1
(external 5120x1440) and scale 2 (built-in, 1512x982 pt, Retina mode off), and
with the negative virtual-desktop origin (second display at x = -1512); it
never uses (0,0) literally. If the user enables `RetinaMode`, the monitor and
the back buffer double together and the predicate still matches.

Native Windows: `rcWork.top == 0` in the common taskbar-at-bottom layout, so
`GetWindowRect == rcMonitor` already and the call is a no-op (`action=noop`).
With a top-docked taskbar the window moves over it, which is what every
borderless-fullscreen game does; Windows allows a popup window to cover the
taskbar. Documented APIs only.

### 2.2 Alternatives considered

- **Present fullscreen (`Windowed = FALSE` with the display mode) when the
  game asked borderless.** wined3d then owns the window (style, position,
  topmost, display mode), and the device is lost on alt-tab (minimise,
  Reset on return). That changes the game's device-loss path, resolution
  semantics and the alt-tab behaviour the user chose borderless for. The
  user can select the game's own fullscreen in its configuration without
  the proxy. Loses on risk and on not being borderless.
- **Bottle registry setting written by the launcher.** No documented Mac
  Driver key changes the work-area origin or the classification of a
  non-covering window; `CaptureDisplaysForFullscreen` acts only on windows
  already classified fullscreen. A system-wide "auto-hide the menu bar"
  setting makes `rcWork.top = 0` and would make the game place the window
  at (0,0) by itself; that is a user desktop change, not a launcher option,
  but it is the zero-code cross-check if 2.1 fails to hide the bar.
- **EXE/in-memory patch: read `rcMonitor` instead of `rcWork`** at 0x4daf25
  (`8B 7C 24 60` -> `8B 7C 24 50`) and 0x4daf29 (`8B 5C 24 5C` -> `8B 5C 24 4C`).
  Two 1-byte displacement edits, zero runtime cost, but unconditional: the
  decorated windowed mode would then mix a monitor origin with work-area
  extents in its centring, and the Wine-side classification question is the
  same. Kept as the fallback if the proxy's SetWindowPos turns out to run
  too late for some driver ordering; the proxy option is conditional and
  reversible.
- **Sizing the window to the work area** (5120x1409): shows everything but
  is not full screen; rejected.

## 3. Cursor

### 3.1 What the facts say

Win32 state after a return is correct in every recorded transition
(foreground/focus/active = game, clip = client, `GetCursorInfo` hidden,
handle NULL; observations note tables). The game's own cursor logic is
`SetCursor(NULL)` on each WM_SETCURSOR plus a re-centre warp; hiding is
dinput's `ShowCursor(FALSE)` on acquire [i]. Nothing in the EXE touches
`ShowCursor`/`ClipCursor`. The persistent native arrow therefore lives in the
Mac driver's Cocoa state, which the local disassembly of `winemac.so`
describes (`hideCursor` returns early when its requested-hidden flag is
already set; `updateCursor:` only hides in the client branch, entered by the
forced argument or a known target window). The vanilla recipe (pointer
outside the window's screen area when returning) is exactly the case in
which Wine's re-hide request arrives while the requested-hidden flag is
still set and no mouse event names a target window: the arrow stays until
a real mouse move over the window [i]. Two things follow: (a) any fix must
make the driver execute a **hide transition**, not repeat a hide it thinks
it already did; (b) `SetCursor(NULL)` again does not reach the driver
(win32u forwards SetCursor to the driver only on a handle change and only
while the show count is >= 0; ShowCursor forwards only when the count
crosses zero) [i, upstream win32u].

### 3.2 `--window-trace`: the one consolidated trace

`X3M_WINDOW_TRACE=1`, requires `--telemetry` (it extends the existing
`telemetry_window` / `telemetry_window_context` / `telemetry_cursor_poll`
rows and the slot 10/11/12 wrappers). Passthrough only; no hide/show, warp
or capture call is ever made by the trace.

| Group | Mechanism | Rows |
| --- | --- | --- |
| Focus/window messages, with the original handler's result | `SetWindowsHookExA(WH_CALLWNDPROC, ..., GetCurrentThreadId())` and `WH_CALLWNDPROCRET`, installed at `hook_device` on the window thread (refused with one row if the thread differs) | `window_msg seq= frame= hwnd= msg= wparam= lparam= result=` for WM_ACTIVATEAPP, WM_ACTIVATE, WM_SETFOCUS, WM_KILLFOCUS, WM_MOUSEACTIVATE, WM_CAPTURECHANGED, WM_CANCELMODE, WM_SYSCOMMAND, WM_SIZE, WM_MOVE, WM_WINDOWPOSCHANGED, WM_DISPLAYCHANGE; WM_SETCURSOR with hit-test and trigger message, summarised (first per 250 ms and on a result change); WM_MOUSEMOVE/WM_NCMOUSEMOVE counted per frame |
| Win32 cursor calls by the game | existing main-module IAT hooks on `SetCursor`/`SetCursorPos` (`loading_trace` light spans) extended to record the handle / coordinates on change and a per-frame count | `cursor_call op=set handle= previous=`, `cursor_call op=pos x= y= result= count=` |
| Snapshot after each transition | at every Present for 120 frames after any message of the first group, then the existing 4 Hz poll: `GetForegroundWindow`, `GetFocus`, `GetActiveWindow`, `GetCapture`, `GetGUIThreadInfo`, `GetClipCursor`, `GetCursorInfo`, `GetCursorPos`, `GetWindowRect`/`GetClientRect`, style/exstyle, `MonitorFromWindow` rects | change-only rows (existing names) |
| D3D9 cursor methods | already wrapped (slots 10/11/12) | `telemetry_cursor_api` (expected 0) |
| Uninstrumented, labelled in the identity row | dinput's own `ShowCursor`/`ClipCursor` (inside dinput8.dll, not in the EXE's IAT; inferred from the clip/visibility snapshots), Cocoa cursor state | `window_trace_scope excludes=dinput_user32,cocoa` |

Why thread hooks rather than subclassing: `WH_CALLWNDPROCRET` delivers the
original return value without touching the window's procedure chain, so the
preservation rules of window-and-cursor.md (keep the original, no reentrant
logging, restore only what we own) reduce to `UnhookWindowsHookEx` at device
release and DLL detach and a reentrancy flag in the hook. Cost: one hook call
per message on the game thread (a few hundred per second; under 10 us per
frame [i], measured by the row's own self-time in the flight). The ring
buffer holds the last 4 s and flushes on transitions and on the
`Ctrl+Shift+F7` marker.

### 3.3 Candidate fix: `--cursor-reassert`

`X3M_CURSOR_REASSERT=1`, independent of `--telemetry`. State machine on the
window thread, armed by the `WH_CALLWNDPROC` observer (same hook as the
trace, installed also without it) on `WM_ACTIVATE` with `WA_ACTIVE`/
`WA_CLICKACTIVE` or `WM_ACTIVATEAPP` with `wParam != 0`, and evaluated at the
next Presents (the game's handler has run, and dinput's re-acquire happens in
the game's frame loop after the message, so the check waits for it):

Fire once, at the first Present within 120 frames of arming where
`GetForegroundWindow() == hwnd`, the window is visible and not iconic,
`GetCursorInfo().flags == 0` (Win32 says hidden: the game's state is
re-established) and the pointer is inside the client rectangle or the clip
rectangle equals the client rectangle; otherwise disarm with a refusal row.
The sequence, on the window thread, inside `CpuCallBoundary`:

    HCURSOR arrow = LoadCursorW(NULL, IDC_ARROW);
    HCURSOR previous = SetCursor(arrow);      // handle change while hidden: no driver call
    int up   = ShowCursor(TRUE);              // count -1 -> 0: driver SetCursor(arrow): unhide
    int down = ShowCursor(FALSE);             // count 0 -> -1: driver SetCursor(NULL): hide transition
    SetCursor(previous);                      // restore the game's NULL handle

Win32 end state equals the start state (handle and count), so the game and
dinput see nothing; the two driver calls make Cocoa run `unhideCursor` then
`hideCursor` with its flag going false -> true, which is the forced
`updateCursor:YES` client-branch hide the local disassembly shows [i on the
CrossOver revision]. If the count was 0 at the start (no dinput exclusive
acquire, e.g. the decorated windowed mode), the two `SetCursor` calls carry
the driver transitions instead and the `ShowCursor` pair is a no-op; both
cases are covered by the same sequence. Row: `cursor_reassert frame= hwnd=
before_flags= before_cursor= previous= up= down= after_flags= after_cursor=
clip= pointer_in_client=`; a mismatch between before and after (count not
-1/0 as expected) is logged and the option disarms for the process.

Rules from window-and-cursor.md kept: one balanced pair per activation,
never a loop until hidden, no global hide, no forced foreground, no pointer
trap (the pointer-in-client gate), no suppression of default handling, and
the trace never calls the sequence. On the 2026-09-12 recipe (duplicate on
the first activation, gone after a second cycle) the same trigger fires on
the first `WM_ACTIVATE`.

Native Windows: the four calls are documented and balanced; `ShowCursor`'s
count is tied to the calling thread's input state on both platforms [i] and
the sequence runs on the thread that owns the window and the game's input,
the same thread dinput used (the fixture asserts `up == 0`); the momentary
show/hide happens inside one Present, before any paint, so no visible
flicker is expected. Nothing in it is Wine-specific.

### 3.4 Alternatives considered

- **Candidate A of the observations note (call the driver's `updateCursor:`
  through the Objective-C runtime).** Needs a native bridge out of an x86
  PE and depends on private class/ivar layout; excluded by the platform rule
  (no Wine-private exports or layouts in production code).
- **`WM_SETCURSOR` observer re-setting NULL (candidate B).** Does not reach
  the driver: the effective handle is already NULL and the count is
  negative (3.1). Loses.
- **`SetCursorPos` to the current position after re-acquire** (a warp that
  might refresh the driver's target window). Depends on whether the
  driver's warp path updates its target-window state, which the local
  disassembly does not cover; and the game already warps every
  WM_SETCURSOR without effect. Not chosen; the trace will show whether the
  game's own warps precede or follow the symptom.
- **DXGI flip / renderer migration, forcing foreground, `ShowCursor` loops,
  global menu-bar or cursor preferences.** Excluded by the note.

## 4. Launcher options, rows, proofs

| Option | Environment | Default | Rows |
| --- | --- | --- | --- |
| `--window-monitor-rect` | `X3M_WINDOW_MONITOR_RECT=1` | off until accepted, then on for modded launches with `X3M_WINDOW_MONITOR_RECT_DEFAULT=1`; `--no-window-monitor-rect` opts out; nothing under `--vanilla` | `window_mode` at create/reset |
| `--window-trace` | `X3M_WINDOW_TRACE=1` (requires `--telemetry`) | off | `window_msg`, `cursor_call`, `window_trace_scope`, extended snapshots |
| `--cursor-reassert` | `X3M_CURSOR_REASSERT=1` | off until accepted | `cursor_reassert` |

Proofs before a flight:

- Host: `test_window_mode_core.py` (the pure predicate: every refusal
  reason, the noop case, the taskbar-on-top and negative-origin monitors,
  Retina doubling); `test_window_options.py` for the three launcher mappings,
  `--vanilla`, the `_DEFAULT` marker and the dry-run JSON; a Python twin of
  the reassert state machine (arm, 120-frame window, gates, fire-once,
  disarm-on-mismatch).
- Wine fixture `run_window_mode.py` (new, pattern of `run_telemetry.py`): a
  32-bit fixture registers the game's class shape (NULL class cursor),
  creates a WS_POPUP|WS_VISIBLE window at `rcWork` origin sized to
  `rcMonitor` exactly as 0x4dac90 does, links `window_mode_core.h`, applies
  the move, and asserts `GetWindowRect == rcMonitor` afterwards (the driver
  did not constrain it), the noop on a second call, and the refusals (a
  decorated window, a foreign thread, a smaller back buffer). While the
  window is up for 2 s the runner takes a host `screencapture -x` of the
  top 40 rows of the primary display; rows 0..30 in the fixture's fill
  colour prove the menu bar hides for a screen-covering popup on this
  CrossOver build (soft check: recorded, not asserted, because focus of a
  freshly launched fixture is not guaranteed).
- Wine fixture `run_cursor_reassert.py`: the fixture puts its thread into
  the game's state (`SetCursor(NULL)`, `ShowCursor(FALSE)` -> -1), runs the
  sequence, asserts `up == 0`, `down == -1`, `GetCursorInfo` flags/handle
  identical before and after, LastError preserved, `GetCursorPos`
  unchanged; then the count-0 variant; then the observer: a probe window
  returning a magic result for a custom message is seen by
  `WH_CALLWNDPROCRET` with that result and the message count matches, the
  hooks uninstall (`UnhookWindowsHookEx` TRUE) and a simulated
  `WM_ACTIVATE` arms exactly once.
- Dry run: proves the option-to-environment mapping and the refusals only;
  it says nothing about the window.

The flight (user): launch 1 `--window-monitor-rect --telemetry
--window-trace` with the alt-tab recipe of window-and-cursor.md (pointer on
the other display when returning, and the 2026-09-12 first-activation
case). Expected: `window_mode action=moved before=0,31,5120,1471
after=0,0,5120,1440`, `telemetry_window window_rect=0,0,5120,1440`, no menu
bar, bottom rows visible; the trace rows around the return give the message
order, the clip/visibility snapshots and whether the game's warps precede
the duplicate. Launch 2 adds `--cursor-reassert`: one `cursor_reassert`
row per return with `up=0 down=-1` and the user sees one cursor. If launch 2
still shows two cursors with the row present, the driver's forced path is
not the mechanism and the next step is Cocoa-side observation, not another
Win32 sequence. Regression checklist: the one in window-and-cursor.md.

## 5. Risks

- Menu bar (2.1): the Mac driver of this CrossOver build may not classify
  the moved window as fullscreen, in which case the window covers the
  screen but the menu bar is still drawn above it; the fixture screenshot
  detects that before the flight, and the fallback is the game's own
  fullscreen mode, not a driver key. Level changes on activation may
  reproduce the "behind the desktop at launch" symptom; the trace's
  `WM_WINDOWPOSCHANGED`/`WM_ACTIVATE` rows locate it.
- Reset: the mode-change path repositions before Reset and we reapply; a
  Reset the game calls without repositioning (device lost) keeps the rect.
  If the game ever set `Windowed = FALSE` the predicate refuses (row) and
  wined3d owns the window.
- Cursor (3.3): the sequence briefly shows the arrow at the screen centre
  for less than one frame; if dinput has not re-acquired by the time the
  gate passes (count 0, handle NULL), the two `SetCursor` calls still carry
  the hide transition. A game thread other than the window thread would
  break the per-thread count assumption; measured equal (216), and the
  option refuses otherwise.
- Native Windows: both fixes are no-ops or harmless (2.1 and 3.3); the
  thread hooks are standard. Unverified on Windows, as everything else;
  recorded in platform-portability.md when implemented.

## 6. Unknowns and what settles them

1. CrossOver 27.0 winemac.drv fullscreen classification and menu-bar
   hiding for a screen-covering popup: the `run_window_mode.py` screenshot,
   then launch 1.
2. Whether the forced `updateCursor:YES` hide is the branch that fixes the
   recipe: launch 2 with the row present. A read-only disassembly of the
   installed `winemac.so` `setCursorPosition:`/`handleMouseMove:` (same
   method as `verification/cursor/winemac-x86_64-cursor-disassembly.txt`,
   but on the arm64 driver this bottle loads) would show whether warps
   update the target window; not required for the fix.
3. Which `VideoFlags` bit selects an exclusive-fullscreen device: the
   `0x4d8f10` device routine (pp `Windowed` at +0x20) — only needed if the
   fullscreen fallback becomes the plan.
