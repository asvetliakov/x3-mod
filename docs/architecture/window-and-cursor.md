# Window, focus and duplicate cursor investigation

2026-09-10. User reports two cursors after switching away and returning; the original objective also reports the macOS menu bar above borderless gameplay. Neither report currently establishes the mechanism. This is an investigation plan, not a claimed fix. No game run, production change or settings change was made for this document.

## Current platform facts

X3 is a 32-bit D3D9 application. The standalone graphics probe loads WineD3D for D3D9 and provides strong evidence of DXMT for its separate D3D11 device. The probe's successful FP16/DXGI flip-discard Present does not change X3's swapchain, input focus, native window or cursor ownership. See [platform evidence](platform.md#standalone-runtime-results-and-backend-identification).

**Flip presentation is a separate rendering experiment, not an established cursor fix.** Microsoft's DXGI flip model changes buffer presentation/composition. It does not promise reconciliation of application-drawn cursors, Win32 cursor state and AppKit cursor state, and Windows DWM behavior must not be assumed for Wine's macOS driver. A backend/presentation change could alter focus timing or window handling and therefore change symptoms, but that would require a controlled A/B result. D3D9 swap effects and DXGI flip-discard are not interchangeable constants. [Microsoft flip model](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-flip-model).

## Separate cursor mechanisms

1. **Game-drawn cursor:** ordinary textured geometry in the game backbuffer. It appears in a backbuffer capture and can survive independently of the OS cursor. We have not yet identified such a draw in X3.
2. **D3D9 cursor:** `SetCursorProperties`, `SetCursorPosition`, and `IDirect3DDevice9::ShowCursor`. D3D may implement this through an OS cursor or software drawing. Its `ShowCursor` result is the previous visibility boolean. Microsoft explicitly discusses suppressing the window cursor when using a D3D cursor. [D3D9 ShowCursor](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-showcursor).
3. **Win32 cursor:** `SetCursor`, `ShowCursor`, `SetCursorPos`, window class cursor and `WM_SETCURSOR`. Win32 `ShowCursor` increments/decrements a display counter and returns the new count; it is not the D3D boolean API. Use `GetCursorInfo` to observe visibility without changing that count. [Win32 ShowCursor](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-showcursor).
4. **Host cursor:** macOS draws a native cursor managed through Wine's Mac driver. Win32 visibility alone may not expose a disagreement in host state. Upstream Wine's `updateCursor:` tracks requested hiding, native hiding and the last target window separately. Its deactivation handler releases capture; its activation handler invalidates accumulated mouse deltas. This supplies specific places to investigate, **not proof that the installed CrossOver revision has a bug there**. [Upstream Wine Mac driver](https://raw.githubusercontent.com/wine-mirror/wine/master/dlls/winemac.drv/cocoa_app.m).

`WM_SETCURSOR` default handling can restore the class cursor in the client area. Observe the message, hit-test and actual handler result rather than assuming focus alone hides it. [Microsoft WM_SETCURSOR](https://learn.microsoft.com/en-us/windows/win32/menurc/wm-setcursor).

## Vanilla reproduction (user run 4, 2026-09-14)

The double cursor is present in vanilla (`./x3run --direct --vanilla`, no proxy): alt-tab
out of the game, move the desktop cursor outside the game window's screen position, alt-tab
back, and both the macOS arrow and the game cursor are visible. It is therefore not a proxy
regression, and the regression checklist below only has to show that the proxy does not make
it worse. The recipe ("cursor outside the window on return") weakens the reset/loss and
backbuffer-image hypotheses in the table and favours the host target/capture-state one: on
reactivation the native cursor is left visible until the mouse re-enters the client area.

## Hypotheses and discriminating evidence

| Hypothesis | Evidence to seek | What would weaken it |
| --- | --- | --- |
| Game/D3D cursor plus native arrow after focus regain | Game backbuffer contains one cursor, host display shows an additional arrow; Win32/D3D visibility transition aligns with activation | Both cursors are present in an unmodified backbuffer capture. |
| Window default handler restores class cursor | `WM_SETCURSOR` after reactivation returns unhandled; class cursor is non-null while game cursor remains active | Message is handled and OS visibility remains correctly hidden; host alone still shows arrow. |
| Win32 show/hide counter imbalance | Extra `ShowCursor(TRUE)` or missing matching hide around repeated transitions, grouped by thread | Balanced calls and stable `GetCursorInfo` despite visible duplication. |
| D3D cursor state is not restored after reset/loss | `Reset`/device-lost sequence followed by changed cursor setup/show calls | Duplication occurs without device-loss/reset and setup state remains identical. |
| Host target/capture state diverges | Win32 foreground/focus/capture looks correct but native cursor remains visible until mouse reentry | Duplicate is already baked into backbuffer. |
| Two drawn cursors or a stale rendered image | Both shapes occur in backbuffer; one remains frozen while the other moves | Backbuffer contains only one or no cursor. |
| Coordinate/scale mismatch makes one cursor appear offset | Shape/position discrepancy correlates with monitor origin, Retina scale, client-vs-screen conversion | Two distinct shapes remain at the same relative offset across equal-scale displays. |

A screenshot tool may include or omit the host cursor. Record capture method and cursor-inclusion setting. Absence from a desktop screenshot does not establish absence on the physical display. Pair a raw backbuffer capture with a host capture/user observation at the same marked transition.

## One consolidated trace

Use one bounded event stream with monotonic timestamp, sequence, process/thread ID, device ID, HWND and current Present frame number. Retain the last few seconds in memory and flush on focus transitions or an explicit marker, plus a short post-transition window. Avoid per-mousemove disk writes and broad Wine relay logging during gameplay. Log forwarding exactly; observation must not call hide/show, warp or capture APIs to “query” their state.

| Event group | Required fields |
| --- | --- |
| Identity/configuration | Executable/proxy hashes, backend evidence, mode requested by user, relevant environment, window class and class cursor handle, D3D focus/presentation HWND, target monitor. |
| D3D cursor | `SetCursorProperties`: hotspot, surface ID/description, HRESULT; bounded hash/copy only at setup if safe. `ShowCursor`: requested BOOL and returned previous BOOL. `SetCursorPosition`: x/y, flags, summarized rate and first/last values per interval. |
| Win32 cursor | Existing `SetCursor`: new/previous handle; `ShowCursor`: requested value and returned count, by calling thread; `SetCursorPos`: coordinates/result; observe `GetCursorInfo` flags/handle/position at transitions. Do not synthesize extra ShowCursor calls. |
| Focus/window messages | `WM_ACTIVATEAPP`, `WM_ACTIVATE`, `WM_SETFOCUS`, `WM_KILLFOCUS`, `WM_MOUSEACTIVATE`, `WM_CAPTURECHANGED`, `WM_CANCELMODE`, `WM_SETCURSOR` including hit-test/trigger message and original handler return. Add enter/leave/move summaries and minimize/restore. |
| Snapshot | `GetForegroundWindow`, `GetFocus`, `GetActiveWindow`, `GetCapture`, `GetClipCursor`, `GetCursorInfo`, `GetGUIThreadInfo` for the window thread. GetFocus/GetCapture alone reflect calling-thread state, so label thread provenance. |
| Coordinates/styles | Window/client rectangles; client origin converted to screen; monitor/work rectangles; style/ex-style; DPI/scaling; cursor position in screen and client coordinates. Include negative monitor origins. |
| Rendering/reset | CreateDevice/Reset presentation parameters including windowed flag, swap effect, multisampling and present interval; cooperative-level/Present HRESULT transitions; backbuffer size and HWND override; timestamps for reset success and first resumed frame. |
| Host follow-up if required | Mac driver target-window/cursor/capture events and native active/key window, presentation options and scale. Only add native instrumentation after the Win32/D3D trace points to a host disagreement. |

If a window-procedure observer is added, preserve the original handler and return value, prevent reentrant logging, and restore only the procedure chain still owned by the observer. A proxy that only sees D3D methods cannot establish that X3 never called User32 cursor APIs. Mark uninstrumented groups explicitly.

D3D9 positions are in desktop coordinates when windowed, while fullscreen interpretation depends on backbuffer/display scaling; the hotspot is subtracted when drawing. Do not “correct” positions without tracing both coordinate spaces. `D3DCURSOR_IMMEDIATE_UPDATE` has no effect for a windowed application. [Microsoft D3D9 cursor positions](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-setcursorposition), [cursor properties](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-setcursorproperties).

## Bounded next iteration

1. Instrument the trace once, covering both cursor APIs and window transitions. Label any omitted APIs. Start with passthrough behavior and no forced cursor policy.
2. In one reproducible scene, mark normal cursor state, switch out using the user's usual shortcut, switch back, move inside the client, cross the boundary, and open/close the in-game menu. Record whether duplicates move together, differ in appearance, or disappear after reentry/click. Distinguish physical Command-Tab from a Windows Alt-Tab mapping in the record.
3. Compare the transition against a clean baseline without the mod, using the same game settings. If both reproduce, the proxy is not the only cause; this still does not identify Wine versus X3.
4. Select one change from the evidence: e.g. restore the established client cursor policy on a proven missed transition, preserve cursor state across reset, or investigate Wine's host-state branch. Implement behind a reversible feature flag and retest the same sequence.
5. Consider DXGI flip presentation as part of renderer integration only after the GPU-sharing/translation gate, with its own A/B test. Do not introduce an entire renderer migration solely to work around an unidentified cursor symptom.

Do not hide the global host cursor unconditionally, repeatedly call `ShowCursor(FALSE)` until it disappears, force foreground focus, trap the pointer after deactivation, or suppress default handling for every non-client hit test. Those approaches can make desktop cursor restoration and window controls fail.

## Regression checklist

- Normal launch, menu, loaded sector, in-game menu, loading transition, normal exit: correct cursor shape/count and usable desktop cursor after exit.
- Repeated switch-out/in cycles, slow and rapid; return by shortcut and by click; no click-through, frozen cursor, pointer lock or inability to switch away.
- Mouse at center, border, titlebar and menu bar before activation; normal window controls remain usable.
- Ordinary windowed, borderless and game fullscreen tested separately; capture mode and actual Win32/native geometry rather than trusting a label.
- Borderless menu bar: record whether it is always visible or appears only near the top; monitor rectangle versus work area; native fullscreen/presentation state; restoration after focus loss and exit. The menu-bar issue may share a focus transition with the cursor issue, but requires its own acceptance result.
- External 5120×1440 scale-1 display and built-in scale-2 display; negative virtual-desktop origin; movement between displays and resize/minimize/restore where supported.
- Device reset/lost/recreated, resolution change and relevant overlay/dialog activation; no stale cursor surface or use-after-free in trace.
- With any final cursor fix, original return values and focus behavior preserved; comparison captures show no unrelated image changes. Repeat with diagnostics disabled to exclude timing effects.

macOS menu-bar hiding belongs to native presentation policy, separate from cursor visibility. If a native presentation adjustment eventually proves necessary, use valid AppKit option combinations scoped to the active game and restore prior state. [Apple presentation options](https://developer.apple.com/documentation/appkit/nsapplication/presentationoptions-swift.struct).
