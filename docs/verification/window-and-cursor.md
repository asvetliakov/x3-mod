# Window mode and cursor (`--window-monitor-rect`, `--window-trace`, `--cursor-reassert`)

Design: [window-mode-and-cursor-fix.md](../architecture/window-mode-and-cursor-fix.md) (sections
2.1, 3.2, 3.3, 4); investigation state and the rules kept:
[window-and-cursor.md](../architecture/window-and-cursor.md). Evidence scripts:
`verification/results/window-mode-and-cursor/`.

## 2026-09-25: implementation (worktree at 48486634, uncommitted, not installed, not flown)

Source: `src/proxy/window_mode{_core.h,.h,.cpp}`, `src/proxy/window_trace{_core.h,.h,.cpp}`,
`src/proxy/cursor_reassert{_core.h,.h,.cpp}`; wiring in `capture.cpp` (create_before,
reset_before, hook_device, final Release, Present), `loader.cpp` (DLL detach) and the light
`SetCursor`/`SetCursorPos` rows (`loading_trace_light.cpp`, change ring); launcher
`tools/manage.py`. Defaults: `--window-monitor-rect` on for modded launches
(`X3M_WINDOW_MONITOR_RECT=1`, `X3M_WINDOW_MONITOR_RECT_DEFAULT=1`; `--window-monitor-rect off` or
`--no-window-monitor-rect` sends 0/0; nothing under `--vanilla`; DLL default when unset: off),
`--window-trace` (requires `--telemetry`) and `--cursor-reassert` opt-in.

| Check | Command | Result |
| --- | --- | --- |
| Build | `cmake --build build --clean-first -j4` (MinGW i686, RelWithDebInfo, all targets incl. both fixtures) | exit 0, 0 warnings (measured) |
| x87 audit | `python3 verification/probe/check_no_x87.py build/d3d9.dll` (three new roots: the hook procedures) | PASS, 690 reachable (684 in the Run82 record), 0 violations (measured) |
| Host, focused | `PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_window_mode verification.analysis.test_cursor_reassert verification.analysis.test_window_options` (+ the two capture host fixtures, check_no_x87, pause/lod-scale launch tests) | 33 tests OK (measured): predicate vs Python twin over 501 cases, state machine vs twin over a 4,146-event script, WM_SETCURSOR summary vs twin over 510 rows |
| Host, full | `/usr/bin/python3 verification/probe/run_host_suite.py` | 267 modules, 2,767 tests, 0 failing (measured; before the last `window_trace.cpp` edit, whose modules were rerun above). `capture_device_creation_fixture.cpp` / `capture_bloom_lifetime_fixture.cpp` gained inert `window_mode` / `window_trace` stubs |
| Wine, window mode | `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_window_mode.py --exe build/window_mode_fixture.exe` | PASS 19/19, 8.8 s (measured) |
| Wine, cursor re-assert | `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_cursor_reassert.py --exe build/cursor_reassert_fixture.exe` | PASS 28/28, 7.4 s (measured) |
| Dry runs | `python3 verification/results/window-mode-and-cursor/dry_runs.py` | see below (measured) |

Window mode fixture (bottle X3, primary display): monitor 0,0,5120,1440, work area
0,31,5120,1440 (menu bar 31 rows). The game's own placement is accepted by the driver
(0,31,5120,1471, as in run320); the production `apply` moves it to 0,0,5120,1440
(`action=moved reason=work_origin result=1`), the rectangle is still the monitor rectangle
after 0.3 s of message pumping (the Mac driver did not constrain it), the client stays
5120x1440, a second call and a window created at the monitor rectangle are `noop`, and the
six refusals (`backbuffer_mismatch`, `fullscreen`, `foreign_window`, `not_popup`,
`decorated`, `foreign_thread`) and the option off left every rectangle unchanged; last
error kept. Screenshot: `BitBlt` from the screen DC returns FALSE with last error 0 (DIB and
screen DC valid) on this driver, so documented Win32 cannot read the menu-bar rows;
whether the bar hides for the moved window is not measured (the fixture window was
foreground). It stays a flight observation.

Cursor re-assert fixture: the stand-in sequence runs `LoadCursor, SetCursor(arrow),
ShowCursor(TRUE), ShowCursor(FALSE), SetCursor(previous)` with counts (0, -1) from -1,
(1, 0) from 0 and (-1, -2) from -2 (rejected). `WH_CALLWNDPROCRET` saw the handler's magic
result and 11 of 11 messages; the handler's last error survived both hooks. `WM_ACTIVATE`
inactive did not arm, active armed once, `WM_ACTIVATEAPP` while armed did not re-arm. In
the game's state (handle NULL, count -1) five Presents with the foreground gate failing
waited, the sixth fired once with `up=0 down=-1 balanced=1`, `GetCursorInfo`
flags/handle, cursor position and last error equal before and after, and never fired again
for that activation; the real sequence alone gave (0, -1) and (1, 0); a failing gate for
120 Presents refused (`reason=foreground`). A start count of -2 (not an expected state)
fires once: the pair is still net zero (`up=-1 down=-2`, handle restored), but no driver
transition happens, the row says `balanced=0 disabled=1`, and the option is disabled for
the process and never re-arms (as implemented; the sequence runs before the counts are known). Trace ring: transition
rows with the handler's result, the `WM_SETCURSOR` summary (one row for five messages, `hit=1 trigger=0200
result=00000001`), `window_msg_frame mousemove=3 setcursor=5`, flushes on
transition (2), marker and device destroy, a `cursor_snapshot` at each transition flush (2); removal returned TRUE for
all three hooks and nothing was observed afterwards. Findings: (1) thread hooks see a
second, unidentified window on the thread (`WM_SIZE`/`WM_MOVE`/`WM_WINDOWPOSCHANGED` during
the message pump), so only messages to the device window count as transitions or arm the
re-assert (fixed before the passing run); (2) in the fixture `GetCursorInfo` reported
`flags=1` while the thread's count was -1 (measured, hidden window); the review-round
witness below shows `flags=0` at the same count once the window is foreground. The gate therefore relies on the game
being foreground, which is also the first gate.

Dry runs (installed bottle, `dry_runs.py`): modded `--taa` (`--direct --motion-output --taa
--object-trace --object-lifetime --ownership`) carries `X3M_WINDOW_MONITOR_RECT=1`,
`X3M_WINDOW_MONITOR_RECT_DEFAULT=1` and neither `X3M_WINDOW_TRACE` nor
`X3M_CURSOR_REASSERT`; `--window-monitor-rect off` carries 0/0; `--telemetry --window-trace
--cursor-reassert` carries 1/1 plus `X3M_WINDOW_TRACE=1`, `X3M_CURSOR_REASSERT=1`; `--vanilla`
carries none. The command line is identical across the three modded runs.

Not verified: the menu bar and the bottom rows in the game (flight, launch 1 of section 4),
the duplicate cursor after alt-tab with `--cursor-reassert` (launch 2), the hook cost per
message in the game (inferred small: `LightCallBoundary`, a ring store, no logging), native
Windows. DLL `fdde5912…` from this worktree (build only, not a candidate).

## 2026-09-25: review fixes (same worktree, not installed, not flown)

Fixes: (1) the light cursor ring (`loading_trace_light.cpp`) is a per-slot sequence lock
(writer: seq 0, release fence, fields, release fence, seq; reader: acquire load, fields,
acquire fence, re-check) with an interlocked writer lock guarding the change state, and
`cursor_drain` reports `dropped` (more than 64 changes between drains or a slot overwritten
while read) as `cursor_dropped=` on `window_msg_frame`; (2) a final Release off the
installing thread leaves the hooks and the ring alone (`window_trace_hooks removed=0
reason=foreign_release`), the next attach on that thread or DLL detach removes them;
(3) `UnhookWindowsHookEx` at DLL detach only on FreeLibrary (`reserved == NULL`); (4) the
Present step acts only for the device that installed the hooks; (5) `--window-monitor-rect`
requires `on|off` (omitted = default on; `--no-window-monitor-rect` kept), so it can no
longer swallow the `launch` action; (6) fixture coverage below; (7) design note: `SetWindowsHookExW`.

| Check | Result |
| --- | --- |
| Build, `cmake --build build --clean-first -j4` | exit 0, 0 warnings (measured) |
| `check_no_x87.py build/d3d9.dll` | PASS, 690 reachable, 0 violations (measured); DLL `abf19459…` |
| Host: test_window_mode, test_cursor_reassert, test_window_options, test_capture_device_creation, test_capture_bloom_lifetime, test_check_no_x87, test_pause_key_only, test_lod_scale_launch | 34 tests OK (measured) |
| `run_cursor_reassert.py` (bottle X3) | PASS 35/35, 7.6 s, exe `6363f864…` (measured) |
| `run_window_mode.py` (bottle X3) | PASS 19/19, 8.9 s, exe `366f5e85…` (measured) |
| `dry_runs.py` | unchanged mapping: default 1/1, off 0/0, trace+reassert 1/1 + both variables, vanilla none (measured) |

Light ring through the real wrappers (measured): 10 changes plus 3 repeats drain as 10
events in sequence order, 13 set calls, 0 dropped; one `SetCursorPos` to the current
position plus a repeat gives one op=pos event, 2 calls; 100 changes between two drains
give the newest 64 in order (first 48, newest 111) and `dropped=36`; the caller's last
error survives the wrapper; an empty drain returns 0.

Real gate witness (measured): the fixture's window shown under the pointer and made
foreground, thread show count -1: `GetCursorInfo` flags 0 (hidden), gates same_thread,
foreground, visible, cursor_hidden, pointer_in_client all 1 (clip_is_client 0, no clip
set); the real `present()` fired (`up=0 down=-1 balanced=1`) and the Win32 end state
(flags, handle, position, count -1) equalled the start. So `GetCursorInfo` reports the
show count of the foreground window's thread: flags 1 at count -1 earlier was the hidden
(background) window case. Inferred: the gate's `cursor_hidden` is meaningful only
together with the foreground gate, which precedes it.

## 2026-09-25: second review (same worktree)

The first successful hook attach pins the module (`GetModuleHandleExW(FROM_ADDRESS | PIN)`,
as `cull_census.cpp`; `pinned=` on the attach row), so `shutdown()`'s unhook at a FreeLibrary
detach is dead code kept only for a failed pin. A second device while the hooks exist logs one
`window_trace_hooks ... installed=0 reason=already_hooked hooked_device=` row and keeps the
first device's hooks. Measured: clean build 0 warnings; `check_no_x87.py` PASS, 690 reachable,
0 violations (DLL `8b91b296…`); test_window_mode + test_cursor_reassert + test_window_options
15 tests OK; `run_cursor_reassert.py` (bottle X3) PASS 36/36, 8.9 s, exe `71300add…`, attach row
`installed=1 ... pinned=1`, second attach `device=2 installed=0 reason=already_hooked
hooked_device=1`. The window fixture was not rerun (window_mode sources unchanged).

## 2026-09-25: launch arm and first-Present trace flush (not flown)

Run 83 A (run323, `verification/results/run323-run83a-launch1-3/cursor_launch.sh` and
`cursor_launch_out.txt`): foreground from frame 0, cursor flags 0 from frame 2, first
re-activation at frame 139, so nothing armed the re-assert at launch and the trace showed
nothing before frame 109. Change (design section 3.3 "Launch arm"): the first complete hook
installation per process arms the machine once (`armed_by=launch`); an arming message replaces
a pending launch arm; `--window-trace` flushes at the device's first Present
(`reason=first_present`) and starts a snapshot burst there. Measured in the worktree at
f19f31ab + diff: clean build 0 warnings; `check_no_x87.py` 690 reachable, 0 violations (DLL
`557d638b…`); test_cursor_reassert + test_window_options + test_window_mode 16 tests OK
(new: launch twin, launch in the seeded random script and the compiled-core comparison);
`run_cursor_reassert.py` (bottle X3) PASS 42/42 (was 36), 9.8 s, exe `fc7bc734…`: attach row
`launch_arm=1`; first Present `reason=first_present written=2` with `cursor_snapshot burst=0`;
launch arm with foreground false refused at its 120th frame (`armed_by=launch`, 0 fires);
a launch arm with the gates holding fired once `armed_by=launch up=0 down=-1 balanced=1`,
end state and last error unchanged, no second fire; a later `WM_ACTIVATE` replaced a pending
launch arm (fresh 120-frame window) and fired `armed_by=activate`; real-gate witness fired
balanced. `dry_runs.py`: unchanged mapping (trace+reassert 1/1 with both variables).

## 2026-09-25 Run 84 A (run329 5120x1440, run330 1920x1080): launch arm fires, arrow stays; parked

The launch arm fired at frame 1 on both runs and every alt-tab cycle fired (17/17 on run330), each with the balanced
sequence (count -1 -> 0 -> -1, previous NULL, pointer in client), measured. The user still sees the desktop arrow from
launch and, sometimes, the double cursor after alt-tab. The game itself makes no SetCursor/ShowCursor/SetCursorPos calls
(0 `cursor_call` rows with the IAT hooks installed) and receives no WM_SETCURSOR or WM_MOUSEMOVE while active; the cursor
is hidden between frames 0 and 1 by dinput's exclusive acquire (inferred from 3/18 unacquire pointer restores), and our
fire lands one Present later (250-350 ms). The only Win32-visible activity afterwards is pointer motion. The cause is
therefore inside the Cocoa driver's hidden state, not observable from Win32 (`excludes=cocoa` in the trace). Ring
coverage complete to frame 2442 (run329) and 4174 (run330), expired 0, overwritten 0. Scripts and outputs:
`verification/results/run329-run84a-cursor/`. Side finding: the design note's §1.2 "WM_SETCURSOR always returns 1"
is contradicted by the two observed returns (0, inactive state).
Candidate fix (not built): one bounded repeat fire at the first pointer motion after each arm, plus a diagnostic
schedule of fires. **User decision 2026-09-25: parked, not a big deal.** `--cursor-reassert` stays opt-in.

**2026-09-26** (`docs/architecture/comparison-hotkeys.md`, "Removed 2026-09-26"): the window trace no longer flushes on the Ctrl+Shift+F7 marker (`window_trace::present` lost its marker argument, `flush=first_present,transition`); `run_cursor_reassert.py` PASS, 41 checks (42 before, `flush_on_marker` removed; bottle X3).

## 2026-09-27: alt-tab freeze on a plain CrossOver launch (triage, no build, no launch)

Symptom (user): from the CrossOver shortcut (bare `x3m.ini`, always tier) an alt-tab away and back
freezes the game 5-10 s; from `manage.py launch` it does not. Script and output:
`verification/results/alt-tab-freeze/altab_gaps.py` / `altab_gaps.out` (inputs: `x3m.prev.log` =
CrossOver session 20260927-021454, `x3m.log` = bare x3run session, run347 `--debug --perf`).

Measured:
- CrossOver session: one alt-tab stop (`music_keep_stop name=alt_tab`, WndProc WM_ACTIVATE inactive,
  caller `0x004d36c2`) at frame 614, t=27.9 s; the next stamped row is frame 899 at t=71.8 s:
  **43.9 s for 285 frames** (every other 300-frame window: 2.8-4.5 s). The only `music_keep_active` row is
  frame 0 (`run_in_background=0`); the active flag `0x00608adc` never read 0 at a Present, i.e. no Present
  ran while the game was inactive. `resets=0`; no `device_lost`, Reset, `cursor_reassert` or Present-error
  row; `media_cue_window` 599..1199 has 0 attempts (no cue retry); 4 new PS and 1 VS (32 variant rows) were created in
  that window. The later gaps are loads: frame 1286 (sector `greenvoid`, 43.5 MB gz, fog prefill
  stall_ms 22,987) and frame 15507 (6.1 s), not alt-tabs.
- x3run sessions: three alt-tabs in `x3m.log` and two in run347, each `music_keep_active 1->0` the frame
  after the stop and `0->1` 43/64/60 (x3m.log) and 15/7 (run347) frames later, all
  `run_in_background=1`; run347 has no frame over 100 ms within 60 frames of its alt-tab.
- Launch-path difference: `manage.py` `--direct` (default) appends `-noabout -skipintro -runinbg`
  (`tools/manage.py:2163-2164`). The CrossOver shortcut `X3AP.lnk` has `HasArguments=0` (LinkFlags 0x93).
  Environment rows differ only in `WINEDLLOVERRIDES` (unset vs `d3d9=n,b`), `WINEUSERLOCALE` (en-AE vs C),
  `CX_APP_BUNDLE_PATH`/`CX_WINEWRAPPER_ALT_LOADER_SOCKET` (CrossOver only), host PATH/PWD; `CX_GRAPHICS_BACKEND=dxmt`,
  `WINEMSYNC=1`, `FEX_X87REDUCEDPRECISION=1` identical; no GST variable in either row. Window row identical
  in both 1920x1080 sessions (1600,72,3520,1152 windowed, `action=refused reason=backbuffer_mismatch`);
  run346-349 ran 5120x1440 moved to the monitor rect.

Inference: without `-runinbg` the RunInBackground bit (`0x4000` of `[*0x00606f3c]`) is clear and the
pump `0x004d34b0` blocks in `GetMessageA` while `[0x00608adc]==0` (`docs/reverse-engineering/music-restart.md`
§2, `pause-dialog-input.md` §2.3) until WM_ACTIVATE active. So only the CrossOver path stops the game loop on
alt-tab; the freeze is the time between the user's return and the first frame after it (delayed or missing
WM_ACTIVATE active from the Cocoa driver, or a stall in the first frame after reactivation). The always tier
cannot split the 43.9 s into time away and time frozen, and does not show which. Nothing in the proxy
blocks on focus: the session-log writer is asynchronous, the cursor re-assert is off, no Reset ran.

Diagnostic (one launch): CrossOver shortcut with `debug = 1` and `perf = 1` in `x3m.ini`, load a save, fly
10 s, alt-tab away about 10 s, return by clicking the window, note when it moves, quit. Rows read:
`window_msg` WM_ACTIVATE/WM_ACTIVATEAPP/WM_SETFOCUS `tick_ms` (return time), `music_keep_active` 1->0 and 0->1
with `run_in_background`, `frame_timing_slow` (`gap_pre_us`) and `frame_phases_slow` (`pre_render_us` = pump
and script, `present_us`) of the first frame after the return. WM_ACTIVATE active arriving seconds after
the click = the macOS/Cocoa side; activation prompt and a long `pre_render_us` = the game's own resume;
a long `present_us` = DXMT/Metal. A/B second launch: the same shortcut with the argument `-runinbg`.
