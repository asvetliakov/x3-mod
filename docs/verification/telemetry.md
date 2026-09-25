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
capture mutex's acquisition delay. `log_wake` (named `log_flush` before the
logging tiers of 2026-09-26, when it timed the stdio flush) records the one
`SetEvent` a summary uses to wake the log writer thread; no metric times the
file writes, which the writer's `log_writer` rows report. Do not add
overlapping categories together.

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

## Route and boundary cost

Added for the iteration-7 timing question (scene frames 38.5 ms with TAA
against 16.1 ms with the route alone, unexplained by any metric of that
run): the live motion route and the temporal boundary report their own
CPU-side cost. Every value is a QPC wall-clock span around the proxy's own
Direct3D calls on the render thread, **CPU-inclusive and never GPU time**: a
`SetRenderTarget` or `StretchRect` span is the cost of submitting the call
through the backend (Wine's wined3d command stream on CrossOver, the runtime
and driver natively), including any blocking the backend chooses to do
there, not the time the GPU spends on it. The counters exist only with
`X3M_TELEMETRY=1`; without it the counts below are still kept but every
tick total is zero and the frame line says `timing=off`. Each timed span
costs one QPC pair (two `QueryPerformanceCounter` calls) plus the
bucketing of `record`; the metrics never change a device call.

Per-call metrics (`telemetry_metric name=...`, one `count` per call):

| metric | one sample is | not included |
| --- | --- | --- |
| `stretch_backend` | the application's own `StretchRect` backend call (the hook's `timer.begin`/`end` span) | the resolve, which runs before `timer.begin` in the same hook |
| `route_gate` | `before_draw` for one draw the route is enabled on: selector event, gate evaluation, history lookup | the sentinel fill, the apply and a lazy flush run inside the same call and are subtracted |
| `route_draw` | one routed draw's apply (variant pair, reserved constants, RT1/RT2 and write masks) plus its undo in `after_draw` | the native draw between them; the jitter writes |
| `route_set_rt` | one route-issued `SetRenderTarget` of the per-draw apply/undo path or of a lazy flush | the fill's and the resolve's own target binds (inside `route_fill` and the `taa_*` phases) |
| `route_jitter` | one jitter constant write: the jittered clip rows before a scene draw, or their bit-exact restoration after it | the jitter arithmetic (SSE, a few ns) |
| `route_fill` | the per-frame sentinel fill: state save, fullscreen quad, state restore | – |
| `route_lazy_flush` | one restoration of RT1/RT2 in `X3M_MOTION_RT_MODE=lazy` (the write masks are never held) | – |
| `route_readback` | one capture-frame readback to disk (RT1, RT2, the pre-resolve color, the resolved FP16 image); `bytes` is what was written | – |
| `taa_run` | `TemporalPass::run` inclusive; nests the five phases below | the copy-back and the readbacks |
| `taa_state_capture` | the cached `D3DSBT_ALL` block's `Capture` plus the target/depth/viewport/scissor getters | – |
| `taa_copy_color` | the 8-bit main target to FP16 scratch `StretchRect` (and the scratch allocation the first time) | – |
| `taa_copy_depth` | the R32F current-depth to depth-history `StretchRect` | – |
| `taa_resolve_draw` | the state normalization, the scene bracket when the pass owns it, and the resolve quad(s) | – |
| `taa_state_apply` | the target/depth unbind and rebind plus the block's `Apply`, viewport and scissor | – |
| `taa_copy_back` | the resolved FP16 image to the 8-bit main target `StretchRect` | – |
| `hdr_redirect` | the latching Clear's RT0 substitution (`X3M_HDR`): viewport/scissor getters, `SetRenderTarget(0, FP16)`, their restoration | – |
| `hdr_writeback` | one write-back through the ladder, inclusive (the shader copy with its state save/restore, the `StretchRect` rung, the explicit rebind) | the capture-frame FP16 readback (`route_readback`) |
| `hdr_writeback_draw` | the identity copy draw: explicit state save, quad, restore | – |
| `hdr_writeback_stretch` | the emergency `StretchRect` rung, only after a failed draw | – |
| `hdr_bind` | an explicit rebind of RT0 ending a write-back (unwind, or nothing pending) | – |
| `hdr_recheck` | the recovery self test at a latch while blocked after an unwind | – |

`route_gate`, `route_draw`, `route_fill`, `route_jitter` and
`route_lazy_flush` are exclusive of each other and may be added into "route
CPU per frame"; `route_set_rt` is inside `route_draw`/`route_lazy_flush`;
`taa_run` contains the `taa_*` phases; `route_readback` occurs only in
capture frames and must be kept out of ordinary-frame estimates.
`capture_cpu`, `draw_backend` and `snapshot` overlap with all of them.

Per-frame totals are appended to `motion_output_frame` (fields after
`taa_references`; earlier fields are unchanged): `rt_mode=perdraw|lazy`,
`timing=cpu_qpc|off`, the counts `set_rt`, `lazy_flushes`, `lazy_mask_writes`
(lazy routed draws, one count per draw, that met an application
`COLORWRITEENABLE1`, or `COLORWRITEENABLE2` on a depth row, other than 15; the
route's own RT2 = 0 write of a fade-band draw is not counted), `jitter_writes`,
`readbacks`, and the microsecond totals `gate_us`, `route_draw_us`,
`set_rt_us`, `lazy_flush_us`, `jitter_us`, `fill_us`, `taa_run_us`,
`taa_capture_us`, `taa_copy_color_us`, `taa_copy_depth_us`, `taa_draw_us`,
`taa_apply_us`, `taa_copy_back_us`, `readback_us`; after them (2026-09-12)
the render-state shadow counters `state_shadow`, `rs_queries`, `rs_hits`,
`rs_gets`, `rs_resyncs` and the engine-hook fields `scene_hook`,
`scene_end_source=none|hook|stretchrect`, `scene_end_check`, `hook_signals`,
`hook_outside_scene`, `hook_state`, `draws_after_hook`, `bloom_copy_seen`
([live-motion-route.md](../architecture/live-motion-route.md#engine-boundaries-and-state-shadow-2026-09-12)). With `X3M_HDR=1` a separate
`hdr_frame` line follows at the same cadence (`redirected`, `end`,
`writebacks`, `flushes`, `writeback_source`, `unwind`, `unwind_reason` and
the rung HRESULTs, `blocked`, `recheck`, `suspended`, `resumed`,
`dirty_at_present`, `target`, `target_bytes`, `caps`, and the `hdr_*` totals
in microseconds; [hdr-scene-path.md](../architecture/hdr-scene-path.md),
"Stage 1 implementation"). The line is written in
every capture frame and, with telemetry on, every `X3M_MOTION_FRAME_LOG`
frames (default 60). `readbacks > 0` identifies a capture frame:
`tools/analysis/summarize_telemetry.py` aggregates the lines per device,
RT mode and capture/normal class (`route_costs.frames`) and prints the
route metrics and the per-frame means and maxima as a table.

`X3M_MOTION_RT_MODE=lazy` (`tools/manage.py launch --motion-rt-mode lazy`)
is an A/B experiment, not a default: RT1/RT2 and their write masks stay
bound across consecutive routed draws and are restored before the first
application call that could observe or depend on them (a draw that does
not route, `SetRenderTarget`, `GetRenderTarget`, `Clear`, `StretchRect`,
`ColorFill`, `UpdateSurface/Texture`, patch draws, state block
create/begin/end/apply, query `Issue`, `EndScene`, `Present`, `Reset`,
`GetRenderTargetData`, an application `SetRenderState` of a held
`COLORWRITEENABLE1/2` (the light hook flushes quietly; its metric sample
and any failure line are deferred to the next heavy call), an application
`GetRenderState`, the engine scene-end signal, the final device Release)
and when the selector leaves the scene phase. Capture frames additionally restore before each
draw's diagnostics (they read the application's bindings), so the
`set_rt` count of a capture frame equals the per-draw mode's; compare
periodic frame lines. Not intercepted: an application write or read of
`COLORWRITEENABLE1/2` between two routed draws of one scene phase
(`SetRenderState`/`GetRenderState` are not hooked); the route reads the
masks when it binds and restores those values. The mode is proven
equivalent by the motion-output fixture's burst cases
([motion-output.md](motion-output.md#fixture)).

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

**Removed 2026-09-25** (user decision): `--depth-copy`, `--scene-depth-capture`, `--motion-capture` and `--finite-positions` from the launcher only (the DLL paths stay for the ownership-integration and motion fixtures, which set the variables); `docs/verification/launcher-options-inventory.md`, "4. Removed 2026-09-25".

**Logging tiers, 2026-09-26** ([design and implementation](../architecture/logging-tiers.md), "Implemented"): the launcher's
`--telemetry` and the per-row options are replaced by `--perf` (`X3M_PERF=1`) and `--debug` (`X3M_DEBUG=1`), expanded
inside the DLL (`src/proxy/log_tiers.h`); `X3M_TELEMETRY=1` and every individual variable stay DLL reads for the fixtures.
Without either group a launch logs the always tier only (header, errors and refusals, Reset, `frame_end` every 3600
frames, `session_end`, one `exception` row from a chained unhandled-exception filter; review fixes of 2026-09-26: no D3D call from the teardown at ExitProcess, the writer parked with the last device). The log is `<game dir>\x3m.log` (previous launch
`x3m.prev.log`); rows are written by one writer thread from a 4 MiB in-memory buffer, so the per-frame `fflush` and its
`log_flush` metric are gone from the render thread. The 300-frame health windows and `taa_invalidate` are telemetry
rows now (either group or `X3M_TELEMETRY=1`). Evidence: `run_motion_output.py` PASS, the 229 committed cases at 346,327
checks plus the new `seam-log-tiers` (47: group/individual row-name equivalence, the always tier's volume, the `log()`
benchmark, the exception witness) and `seam-exit-path` (8: exit after a throw past `BeginScene`, `FreeLibrary` unloads the proxy after the last device); `run_d3d9_exports.py` PASS on the new file policy; x87 audit 709 reachable, 0 violations;
host suite 268 / 2,792 / 0 (`verification/results/logging-tiers/`).
