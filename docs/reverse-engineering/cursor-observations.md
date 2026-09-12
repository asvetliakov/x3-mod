# Cursor observations: completed 0.3 game session

Analyzed offline on 2026-09-10. No game launch, bottle setting, binary patch or production source change was made for this analysis.

## Conclusion

The strongest current explanation is a disagreement between **the game's software cursor and native cursor presentation after input/focus transitions**. The evidence does not identify the exact Wine/AppKit transition yet. The game regains Win32 focus and its client clip rectangle, while Win32 reports the cursor hidden with a null handle. A persistent native arrow is therefore not explained by merely forgetting to regain Win32 focus. Changing to DXGI flip presentation is not a demonstrated repair.

The user confirms one game cursor plus one macOS arrow at different positions during this 0.3 run, and recalls that switching away/back and clicking out/in did not remove them. This is direct user observation. The log contains no host cursor image/visibility sample or exact symptom marker, so do not claim a particular recorded frame proves the arrow was present.

## Evidence and coverage

Source: `Steam/drive_c/X3/x3-modern-captures/session-20260910-214701-212.log`, 42,007,138 bytes, 867,195 lines, SHA-256 `81cd598428312b709889b0fb892c6243a20818f20d0a3bf400eacdc33af017f0`. A reproducible compact [JSON report](../../verification/cursor/session-20260910-214701-cursor.json) retains line numbers and matching snapshots; [the parser](../../verification/cursor/analyze_cursor.py) reads the original without modifying it.

This run uses a decorated **windowed** 1280×768 D3D9 device, `ex=0`, swap effect 1/discard, no MSAA and presentation interval 1. Window `000a0064` has style `14ca0000`, outer rectangle `(1917,82)–(3203,882)` and a client clip rectangle `(1920,111)–(3200,879)` on the external scale-1 display. The work area starts at y=31. The geometry is stable across recorded focus changes. This session does not reproduce the separate borderless menu-bar case.

| Snapshot | Source line | Foreground/focus | Clip rectangle | Same-time cursor poll |
| --- | --- | --- | --- | --- |
| Initial Present, frame 0 | 42–43 | Game/game | Whole virtual desktop | visible flag 1, null handle, `(0,0)` |
| Frame 1 | 63–64 | Game/game | Game client | flags 0, null, `(2026,733)` |
| First sampled departure, frame 1884 | 861–862 | `00030020`/none | Whole virtual desktop | flags 1, handle `00010022`, `(1977,878)` |
| First sampled return, frame 2305 | 900–901 | Game/game | Game client | flags 0, null, `(1970,878)` |
| Second sampled departure, frame 6478 | 866030–866031 | `00030020`/none | Whole virtual desktop | flags 1, handle `00010022`, `(1977,878)` |
| Second sampled return, frame 6488 | 866041–866042 | Game/game | Game client | flags 0, null, `(1977,878)` |

Both GUI active/focus handles restore to the game, render/window thread IDs both equal 216, and GUI mouse capture remains null throughout. Null capture does not mean clipping failed: clip state is separate and demonstrably changes to the client rectangle. The other HWND's owning process was not logged, so it cannot be confidently named from this file.

There are 312 emitted cursor polls: 309 report flags 0 and three report flags 1. Two of the visible records correspond to the sampled departures; the third is startup. All other records have a null handle. These are **change-only, at-most-4-Hz** records, not 312 uniformly spaced samples or proof that no short visible state occurred between them. The first sampled departure/return are about 4.354 seconds apart, the second about 0.253 seconds; those are observation intervals, not exact activation-message timings.

No D3D cursor API records or cursor-call metrics are emitted, and no Reset records occur. The vtable cursor hooks have separately passed a fixture, making a D3D cursor path less likely for this run. Main-module IAT hooks for `SetCursor` and `SetCursorPos` report installation, but emit no call metrics. Their scope excludes calls originating in other modules, direct/dynamically resolved calls and code before hook installation. `ShowCursor` is not among these main-module hooks. Consequently, **the trace does not establish that no Win32 cursor management happened**.

## DirectInput is a relevant missing boundary

Wine's upstream DirectInput mouse implementation hides the cursor and applies clipping on exclusive acquisition; unacquisition releases clipping, shows the cursor and may restore its original position. This resembles the observed clip/visibility transitions, but X3's cooperative flags and acquire results were not captured, so the connection is an inference. A game cursor driven by relative input can have a different position from the host cursor; the reported offset does not by itself prove a Retina-coordinate bug. [Wine DirectInput mouse implementation](https://raw.githubusercontent.com/wine-mirror/wine/master/dlls/dinput/mouse.c), [Microsoft device acquisition semantics](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee415221(v=vs.85)).

The next synthetic reproduction should therefore exercise foreground/exclusive DirectInput acquire/unacquire, not only `IDirect3DDevice9::ShowCursor`. A native arrow drawn after DirectInput hiding can coexist with a correctly hidden Win32 cursor state and a game-drawn pointer.

## Local Mac-driver evidence

Read-only Objective-C metadata and disassembly of installed `lib/wine/x86_64-unix/winemac.so` establish the following methods and state for SHA-256 `d13a1f9e37abc9f90f46de5f5c00bf062b42b23bf8c0ef9cb94fa8b66404db37`:

| Method/state | File virtual address/ivar offset | Observed behavior |
| --- | --- | --- |
| `updateCursor:` | `0x7d10` | Uses forced-update argument or `lastTargetWindow` to enter client branch. Calls native hide only when hide requested and internal hidden flag false. Outside client branch, restores arrow and unhides if internally hidden. |
| `hideCursor` | `0x7e00` | Returns immediately if requested-hidden flag already true; otherwise sets it and calls `updateCursor:YES`. |
| `unhideCursor` | `0x7e30` | Clears requested-hidden flag only on change, then calls `updateCursor:NO`. |
| `lastTargetWindow` | `0xa0` | Window-target state independent of Win32 focus. |
| `cursorIsCurrent`, `cursorHidden`, `clientWantsCursorHidden` | `0x120`, `0x121`, `0x122` | Three separate one-byte states. |

The [saved disassembly](../../verification/cursor/winemac-x86_64-cursor-disassembly.txt) includes the binary hash. These addresses are specific evidence, **not patch targets**. The native module architecture loaded by the completed process was not independently captured; inspecting the installed x86_64 driver does not alone prove it was loaded. The method structure matches the relevant upstream implementation. [Wine AppKit cursor state machine](https://raw.githubusercontent.com/wine-mirror/wine/master/dlls/winemac.drv/cocoa_app.m).

A plausible sequence is requested-hidden remaining true while loss of a target window causes native unhide; a later repeated hide request can return early. If normal native target/mouse processing does not reapply the hidden state, the arrow can persist. **This is a candidate causal sequence, not observed native state.** The current game log cannot read `lastTargetWindow`, `cursorHidden`, or `clientWantsCursorHidden`.

There is another guard to consider before prescribing a Win32 workaround. Upstream Wine sends a driver cursor update when the effective cursor handle changes; setting null again when its effective value is already null can be a no-op for the driver. If the cursor show count is negative, even changing the stored cursor handle may leave the effective handle null. Thus repeated `SetCursor(NULL)` or a transparent HCURSOR cannot be promised to reconcile native hiding. [Wine cursor server handling](https://raw.githubusercontent.com/wine-mirror/wine/master/server/queue.c), [Wine User32 cursor bridge](https://raw.githubusercontent.com/wine-mirror/wine/master/dlls/win32u/cursoricon.c).

## Concrete reversible candidate for review

**Candidate A: reconcile existing native hide state at the scoped client reactivation boundary.** Prototype this only in a disposable synthetic process before adding anything to X3:

1. Associate one known fixture HWND with its native Wine client window. Trigger after activation/reacquisition or client reentry, once per transition, not every frame.
2. Before queuing work, require the fixture to be the foreground window, visible/not minimized; successful Win32 hidden-cursor observation; client clipping active; pointer within the client. Recheck native application activation and the intended window on the Cocoa main thread when the callback executes. Drop stale queued callbacks by generation on deactivation/destruction.
3. Inspect the controller through Objective-C runtime names after verifying the expected class/method layout. Require `clientWantsCursorHidden=true` and `cursorHidden=false`. This proves the precise internal discrepancy the candidate addresses. Then invoke **the existing** `updateCursor:YES` method once. Do not change show counts, set private ivar offsets, force focus, warp the pointer or patch the driver binary.
4. Log before/after native flags, target window and Win32 state. The existing method should perform one balanced native hide and update its own bookkeeping. Allow ordinary Wine deactivation/client-exit handling to restore the cursor; verify this behavior in the fixture. If it does not restore cleanly, reject the approach.
5. If `cursorHidden=true` while a physical arrow remains visible, **do not add another native hide call**. That is a different mismatch, and this candidate cannot repair it. Record the failed predicate and investigate actual native state/cursor ownership instead.

This is narrow because it reuses Wine's own state machine only when its requested/actual bookkeeping differs and the game client should own presentation. It is platform-specific and depends on internal interfaces, so any later implementation must be opt-in, version-checked and disabled safely if runtime symbols/layouts differ. No Objective-C calls can be made directly from an ordinary x86 PE DLL without a supported native bridge; that bridge must be demonstrated in the fixture first. A global helper calling `NSCursor.hide` in its own process is not equivalent.

**Candidate B, only if the fixture shows the game window policy is wrong:** a `WM_SETCURSOR` observer may reapply the established game-client policy when `HTCLIENT`, the game is foreground and its software cursor is active. Preserve original handler semantics elsewhere. However, repeated null setting is weak against the effective-handle guard above, so this is not the preferred fix for the current evidence. [Microsoft WM_SETCURSOR](https://learn.microsoft.com/en-us/windows/win32/menurc/wm-setcursor).

Neither candidate is deployed, and neither is yet a verified repair. Do not use unbounded `ShowCursor` loops, permanent global cursor hiding, global menu-bar preferences, or a renderer migration as a substitute for validating this input transition. Apple defines unhide as undoing a prior hide; extra calls must not accumulate. [Apple NSCursor](https://developer.apple.com/documentation/appkit/nscursor).

## Synthetic verification without launching X3

Build a disposable x86 Win32 fixture with a small decorated window, a visually distinct software crosshair, DirectInput foreground/exclusive mouse input, and an automatic timeout. Use only process-local diagnostics/backend selection, with no bottle registry edits. Record acquire/unacquire HRESULTs, cooperative flags, input deltas, focus messages and Win32 cursor/clip state. Include a separate nonexclusive/ordinary Win32 cursor phase to show that the trace distinguishes policies. Preserve and restore any fixture-owned clip/cursor resources on exit.

Run with candidate disabled first. Add scoped native observation of `lastTargetWindow`, requested-hidden and hidden flags around activation, clipping and client reentry. The primary success gate is **reproducing the actual mismatch without manufacturing it**. If no mismatch reproduces, the fixture can verify bridge and cleanup behavior but cannot establish that X3's defect is fixed.

With the same reproducible transition, enable candidate A and require: one software cursor only in the active client; native arrow usable immediately outside/in another application; unchanged Win32 hide count and pointer position; clean repeated activation/minimize/restore/exit; no focus stealing or pointer lock; one reconciliation per eligible transition; no added hide when native bookkeeping already says hidden. Test the external display first to match this run. Add built-in scale-2 and borderless coverage afterward.

Keep host-cursor observation separate from screenshots that might omit the cursor. Use capture with explicit cursor inclusion or a direct observer; associate the symptom with a timestamp. Save native/Win32 event evidence whether the candidate succeeds or fails. Rendering-core work can continue independently while this bounded fixture resolves the host-input branch.

## Observation 2026-09-12 (bottle X3, arm64 Wine + FEX, review-25 build)

User report after run 3: at launch the game window comes up behind the
desktop. The first alt-tab into it shows the duplicate cursor. Switching to
the desktop and back again (a second alt-tab cycle) makes the duplicate
disappear. The user attributes it to the original game/Wine behaviour rather
than to our changes; it is consistent with the activation-transition
mechanism above (a mismatch created on the first activation and reconciled by
the next deactivate/activate pair). Not yet compared against a vanilla launch
on this bottle (`--vanilla`) — that A/B stays the first step before any
candidate is tried.
