# Optional CPU, rendering-boundary and cursor telemetry

The 0.3 diagnostic build retains capture schema 2 and adds optional telemetry.
Set `X3M_TELEMETRY=1` for a process to enable it. Every other value leaves
telemetry disabled. This is instrumentation, not a graphics or performance fix.
No game was launched for this verification.

## Coverage and interpretation

QPC timing begins during proxy log initialization; it cannot account for process
startup before the proxy was loaded. The loader records explicit backend-load
and Direct3DCreate9 spans. Device creation, resource creation, shaders, Present,
Reset and draw submission have CPU-side backend durations. Full CreateDevice
and Reset presentation parameters are recorded before and after the backend,
including swap effect, count, depth settings, HWNDs, flags and refresh/interval;
Present HWND override is logged on change. They are not GPU
execution times and include backend blocking, driver work and scheduling.

Each category has fixed-size count/failure/min/max/total counters and six duration
buckets (up to 10 microseconds, 100 microseconds, 1 ms, 10 ms, 100 ms, and longer).
There is no dynamically growing telemetry event buffer. Summaries are triggered
from observed API calls at intervals of at least one second, including resource
calls during a load without Present. Reset and destruction also report. No
background reporting thread exists: a period without instrumented calls is
reported upon the next observed call, not while the process is stalled. Summary
output is flushed independently of Present and flush cost is measured.

`present_normal` and `present_capture` are backend Present durations.
`frame_normal` and `frame_capture` are intervals between consecutive completed
Present calls. Intervals adjacent to a captured frame are classified as capture
intervals, so they cannot enter the normal distribution. Reset clears that
adjacency and the next Present establishes a new anchor with `reset_count`.
Application work, pacing, resource loading and diagnostics are all part of frame
intervals. They must not be interpreted as renderer-only time.

`capture_cpu` measures proxy work around captured draw and boundary hooks while
excluding the original backend operation. `snapshot` includes state inspection
and logging inside that work. Shader inspection separately records GetFunction
and allocation/copy work, hashing, and synchronous file dumping; these are
nested within `shader_inspect`. Shader backend creation is separate. The count
of dumps is unique shader hashes in this process, whereas inspection can occur
at creation and again in captured draws. `lock_wait` records the existing global
capture mutex's acquisition delay. `log_flush` records explicit flushes,
including summary flushes. Do not add overlapping categories together.

Resource categories cover 2D, cube and volume textures, render targets, depth
surfaces, vertex buffers and index buffers. `bytes` for VB/IB is the successful
requested length; it is not physical GPU allocation size. Other resource byte
counts are zero because no format/pitch/mip allocation estimate is made. Shader
hash/dump byte fields concern shader bytecode, not GPU allocation.

The verified loading import tracer is initialized after telemetry and contributes
its own bounded aggregate reports to global summaries. Its fingerprint gate,
observed imports, nested spans and coverage exclusions are described in
[loading-performance.md](../reverse-engineering/loading-performance.md). A
standalone fixture correctly fails the real game's fingerprint gate; its device
telemetry still runs. Loading import behavior has a separate synthetic fixture.

## Ordered capture boundaries

With telemetry enabled, capture logs include SetDepthStencilSurface and
StretchRect interception as well as the existing Clear and SetRenderTarget
capture paths. Outside requested capture frames these hooks emit no operation
records. A `capture_event` gives device, frame, a contiguous per-frame `seq`,
`after_draw`, operation, result and QPC. `draw_begin` means the pending draw has
not been forwarded yet, so `after_draw` is the preceding draw count. Other events
are recorded after their backend call. The legacy draw argument/result records
remain authoritative for the actual draw outcome.

Clear logs flags/color/depth/stencil, rectangle count/pointer (up to 16 supplied rectangle values, with omissions counted), and current target
and depth identities. SetDepth records null unbinding explicitly. StretchRect
records source/destination identities, filter, and supplied rectangle values.
Failed SetRT/SetDepth/StretchRect calls retain their result and pointer arguments
without interrogating the failed resource arguments. Draw snapshots and all
boundary observations preserve the backend's returned COM objects and pointers.
The new depth/stretch hooks are not installed with telemetry disabled.

## Cursor, window, focus and optional phase markers

Only when telemetry is enabled, the proxy observes D3D SetCursorProperties,
SetCursorPosition and ShowCursor. It never hides, shows, moves or clips a cursor
on its own. D3D ShowCursor's return is **previous visibility**, not the integer
User32 ShowCursor display count. Cursor API return values and last-error values
are preserved. All calls are counted, while change records are bounded; position
text is at most four events per second with suppression counts. Cached surface
pointers are diagnostic values only; no COM reference is retained.

Present polls window/cursor state no more than four times per second and logs
changes only. The log distinguishes rendering-thread GetFocus from the window
thread's GetGUIThreadInfo active/focus/capture windows. It includes foreground,
window styles, visibility/minimization, screen-space window/clip/monitor/work
rectangles, client-space client rectangle, and GetCursorInfo visibility/handle/
position. These observations do not consume messages or install a WndProc hook.
No polling occurs while Present is absent. Window-thread focus/capture and clip
changes participate in change detection. See
[window-and-cursor.md](../architecture/window-and-cursor.md) for remaining
hypotheses and the distinction from main-executable User32 import tracing.

Ctrl+Shift+F7 optionally adds an edge-triggered `telemetry_phase_marker` when the
game's root window is foreground. Input is observed, not consumed; the game can
also see the chord. Coverage is explicitly `present_poll`, so tapping during a
load without Present may not create a marker. Existing F8 capture behavior is
unchanged. The synthetic tests do not inject keyboard input.

## Standalone verification

Build the production DLL normally, then run:

```sh
sh verification/probe/build_telemetry.sh
sh verification/probe/build.sh
python3 verification/probe/run_telemetry.py
python3 verification/probe/verify_telemetry.py
python3 tools/analysis/summarize_telemetry.py \
  verification/results/telemetry-on-capture.log \
  --output verification/results/telemetry-summary.json
```

The runner uses **CrossOver Preview.app**, the Steam bottle, `--no-update`, and
process-local DLL/environment overrides. It copies the executable/DLL into
isolated verification directories. It does not copy files into the game or edit
bottle settings.

The fixture checks baseline versus proxy-off versus proxy-on API results and
readback pixel equality; invalid calls; resource categories; shader creation;
Clear/SetRT/SetDepth/StretchRect ordering; Present; Reset; and final COM reference
counts. A deliberate 1.1-second no-Present gap proves resource calls trigger
reporting. Three hundred cursor position calls prove aggregation and rate
limiting. Cursor state/position is restored before completion. SDK compile-time
`offsetof` assertions cover every hooked vtable slot.

Additional legacy smoke and capture-state fixtures verify unchanged device/API/
resource identity, native stateblock restoration, shader bytecode, typed
constants, resource allocation identity, normal and pure devices, and reset.
The saved reports document synthetic observations only. They do not establish
X3's load-time bottleneck, cursor cause, image equivalence during gameplay,
threaded workload overhead, or a performance improvement.

### Recorded result (2026-09-10)

The final integrated DLL SHA-256 was
`71f59c8e6422d5bbf2f55c116e2c0388026d956ba45a3a03eee53c4d85a232a6`.
All three telemetry fixture modes exited successfully with identical API/readback
output, including cursor last-error sentinels. The verifier confirmed all resource
counts, one failed texture allocation, nine ordered capture events, and one
cursor position text event plus 299 suppressed calls. The legacy smoke passed;
the capture-state verifier passed for two devices (normal and pure), ten captured
frames, and four distinct vertex-buffer allocations. The telemetry parser accepted
all 26 metric series with zero rejected records. Evidence is in
`verification/results/telemetry-verification.json`, `telemetry-summary.json`,
`telemetry-build-verification.json` and `capture-state-v3-summary.json`.

To repeat the legacy checks after building their executables:

```sh
python3 verification/probe/run_legacy_telemetry.py
python3 verification/probe/verify_capture_state.py \
  verification/results/capture-state-v3-capture.log \
  --output verification/results/capture-state-v3-summary.json
```

Timers, counters and reporting themselves add CPU overhead. `capture_cpu`
excludes backend time and lock acquisition, and does not include its own final
counter update. Overlapping metrics and synthetic run durations are diagnostic
observations, not a measurement of zero overhead or a gameplay speedup.
