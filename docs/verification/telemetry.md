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

## Motion input and lifetime diagnostics

The next source build adds `motion_input` after each draw in requested capture
frames. It records the actual shader path, submitted-row bit hash, declaration,
target identities, buffer allocation/revision identities and independent blocker
and proof masks. The record is tied to the draw's `device`, `frame` and `index`.
The capture summarizer retains those fields and explicitly reports whether all
three coordinates match; missing or mismatched records establish no eligibility.

`blockers` is hexadecimal; `proofs` is decimal. Blocker bits are position program
1, pixel coverage 2, position layout 4, buffer description 8, buffer revision 16,
draw range 32, raster state 64, target layout 128, submitted rows 256, object scope
512, user-memory draw 1024, query failure 2048 and submission failure 4096. Proof
bits are lifetime 1, known geometry revisions 2, reviewed position 4, supported
coverage 8 and successful submission 16. Zero blockers alone does not prove a
lifetime. Even all five proof bits do not establish finite vertex payloads,
geometry retention until replay, camera-cut policy or final scene-color coverage.
`vertex_finite_verified` is a separate upload-evidence result in the follow-up
source build; it remains zero in installed iteration 5.

`--finite-positions` requires `--ownership --telemetry` in the launcher and sets
`X3M_FINITE_POSITIONS=1`. It is off by default and also enables buffer revision
tracking when object tracing is inactive. The native loader requires ownership;
`finite_upload_mode` reports the request and enable decision. The observer is
restricted to the verified Preview MANAGED/WRITEONLY upload contract. It adds no
draw-time buffer Lock or GPU readback and retains classifications rather than
vertex payload. Unsupported mappings remain unknown.

The scoped `motion_geometry` record separates exact submitted shader qualification
(`source_qualified`, hash, word count), finite XYZ state (`0` unknown, `1` finite,
`2` nonfinite), query status/reason/generation/revision and index-range evidence.
For indexed draws, actual whole-IB extrema must fit the declared min/count before
querying the full declared effective vertex interval. `index_exact=0` means the
extrema conservatively cover the allocation, not exactly the requested subrange.
Nonindexed draws need no IB evidence. `finite_requested=0` can also mean the
reader skipped that query because its layout/range/revision or IB gate failed;
the reason identifies that refusal. Global activation is in `finite_upload_mode`.
Reason numbers and names are defined by `FiniteEvidenceReason` in the ownership
header. Neither a qualified source nor finite XYZ establishes temporal coverage,
successful submission, object correspondence or stability until later replay.

When requested, `finite_upload_metric` records cumulative owner counters at each
captured Present, every 300 other Presents and before/after Reset. It includes
current/peak atlas bytes, sidecars/metadata, process reservations, uploads,
publications, invalidations, allocation failures, scans, classified bytes, scan
QPC ticks, qualifier/query QPC ticks, queries, cached results and examined position
components. Qualifier time and scan time are separate measurements; they are not
total observer overhead. Nonzero `finite_upload_reason` counters retain refusal
names/counts. `finite_upload_first_refusal` retains one bounded reason plus buffer
type, format, pool, size, usage and lock flags for diagnosing unsupported inputs.
These are batched
diagnostics, not per-upload logs. Counters belong to an owner; evidence generation
changes invalidate prior certificates. The QPC frequency comes from the session's
telemetry record. No loading-cost or game coverage result is claimed before a
user-run capture from this build.

`--object-lifetime` requests the exact-build registry observer and requires
`--object-trace --ownership`. The observer is off by default. When active at the
start of a captured draw, `motion_lifetime` records the before/after lookup status,
epochs, mutation revisions and node/camera serials. Lifetime proof survives only
when both lookups agree across the native draw. A failure that disables observation
still produces its terminal record. The loader records activation status and
whether a complete initial registry snapshot was available. These observations
describe storage identity; a camera cut can occur without destroying the camera.
See [lifetime analysis](../reverse-engineering/object-lifetimes.md) and
[draw-input verification](draw-input.md).

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

## Private motion production

`--motion-capture --capture-frames 4` requests private GPU motion diagnostics;
**live dispatch currently refuses with `enabled=0 reason=write_exclusion_unavailable`.**
The component is verified under explicit caller serialization, but the proxy has
not yet excluded worker buffer writes across validation and replay. The option
requires `--scene-depth-capture --depth-copy --finite-positions --object-lifetime
--object-trace --ownership --telemetry`. The launch tool rejects fewer than two
consecutive captured frames because the first frame only seeds correspondence.
No setting is enabled in an installed build by changing these source files.

`motion_replay` records the pre-Clear candidate, observed/eligible/matched/rejected
and completed draw counts, native operation/restoration HRESULTs, inclusive CPU
cost and observed execution/query refusal. `candidate_produced=1` is provisional:
it precedes the application's Clear and Present. `motion_frame` separately records
`storage_history_committed` after successful boundaries and presentation.
`continuity_known=0`, `color_coverage_known=0`, `temporal_consumed=0` and
`temporal_history_committed=0` retain the distinction from gameplay TAA.
The private RGBA32F target and replay shader resources are released inside the
boundary callback. Captures do not read back game pixels or retain buffer payloads.

## Application admission diagnostics

The source build reads the off-by-default `X3M_ADMISSION=1` setting once, before
its first exported operation initializes the backend. This diagnostic option
accounts for installed application entrypoints; it does not enable replay.
`application_admission_mode` reports the selected mode with
`live_replay=0 coverage_complete=0`.

When enabled, `admission_metric` follows the existing capture/300-frame Present
cadence. It records active and waiting roots, cumulative admitted roots and
promotions, permanent veto bits and the first reason. A root is one outer
application transaction; intercepted calls nested within it do not add roots.
The snapshot includes its own active Present transaction. These counters are
neither per-draw timing nor proof of complete application coverage.

Final Release reports `application_admission_final phase=factory|device` after
the corresponding capture mutex and admission scope finish. A nested factory
destruction can still see the outer device root; its subsequent device record
must retire that root in the serial fixture. Other concurrently active callers
can also appear in a real snapshot. See [entry coverage and verification
limits](proxy-application-admission.md).
