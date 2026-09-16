# State-call fast path

Design note, 2026-09-16. Question: the proxy's hooked state-setter path is the
largest proxy cost in busy scenes; what to change, in which order, and what each
step is worth. Inputs: run87 (DLL `bbadc568`, source `f94290c`, `--frame-timing`
on, log `session-20260916-055042-216.log`) and the bottle microbenchmark
`verification/results/bottle-X3/state-hook-benchmark.json` (same DLL, section in
`docs/verification/sampling-profiler.md`). Steps 2 and 3 are implemented
(Envelope, below); the benchmark record now holds the post-change numbers.

## Measured inputs

run87, `frame_timing` windows (300 frames each, p50): busiest windows 49.1 ms at
873 draws with 53,079 hooked state calls and a 16.2 ms `state` bucket (305 ns
per call); the brief's representative busy window 28.5 ms at 457 draws, 30,076
state calls, 8.9 ms (297 ns per call); quiet windows 9.6-10.4 ms at 86-100
draws, 4,700-5,750 state calls at 360-390 ns per call. The per-call figure is
flat across load, so it is fixed per-call overhead, not driver work. Of 200
`frame_timing_slow` witnesses 192 name `compositor_pre` as the slowest call
(out of scope here, noted in open issues).

Benchmark, ns per call (native backend / proxy with timing off / timing on):
SetRenderState 13.5 / 121.5 / 349.9; SetTexture 16.2 / 128.1 / 357.2;
SetSamplerState 10.6 / 110.3 / 338.0; SetVertexShaderConstantF(4 regs)
16.7 / 79.5 / 310.2; SetStreamSource (heavy guard at the time) 15.8 / 1393.7 / 1618.9;
SetTextureStageState is not hooked (11.4 / 11.1). Primitives:
QueryPerformanceCounter 67.8; uncontended `std::recursive_mutex` lock+unlock
6.8; Get/SetLastError pair 3.9; `LightCallBoundary` envelope 9.9;
`CpuCallBoundary` envelope (four FNSAVE/FRSTOR) 1004.4.

Attribution of one hooked light setter in run87 (~297 ns): 14 native backend,
~96 proxy guard/shadow/dispatch (6.8 mutex, 9.9 envelope, ~79 map lookup,
admission object, `Scope` bookkeeping, shadow update), ~229 frame-timing
stamps (two QPC plus two LastError pairs plus the scope code). The game's mix
is heavier than the benchmark's equal shares (constants with more registers,
texture changes that call `GetLevelCount`/`QueryInterface`, and the heavy-guard
stream/index/declaration setters), which is where the game's 297 exceeds the
benchmark's 122+229 split.

Other run87 facts used below: `create_device flags=00000052`
(HARDWARE_VERTEXPROCESSING | PUREDEVICE | FPU_PRESERVE, no MULTITHREADED);
`device_creation_policy effective=00000042` (the proxy strips PUREDEVICE when
the motion route is on, `capture.cpp` 2072); `X3M_MOTION_RT_MODE=perdraw`;
`X3M_ADMISSION` unset (null monitor: the admission object is one atomic load);
`lock_wait count=249 total_us=54 max_us=4.4` per one-second interval (never
contended); `rs_queries=2559 rs_hits=2559` per interval (about ten shadow reads
per draw, all hits); `sb_resyncs=0`, no state-block line in the whole log; no
`profile_thread` lines (profiler off).

## (a) What a hooked SetRenderState does today

In order (`capture.cpp` 1772): `LightCallBoundary` construct (GetLastError,
stmxcsr; 5 ns); `ApplicationAdmissionAbi` (null monitor: one acquire load and
two out-of-line calls; ~5-10 ns); `PlainHookGuard` (recursive_mutex 6.8 ns,
then `frame_timing::Scope` begin: with the option on, GetLastError, QPC,
SetLastError, global stores); `devices.at(d)` (`std::map` lookup, exception
check); `before_set_render_state` (two compares; lazy mode only);
`before_original` (ldmxcsr, SetLastError); native call (13.5 ns);
`after_original` (GetLastError, stmxcsr); `set_render_state` shadow store
(`shadow_index`: two range tests then a linear scan of up to 16 entries, plus
an 8-entry `composition_blend_index` scan when the blend shadow is requested);
Scope end (GetLastError, QPC, subtractions, SetLastError); unlock; boundary
destructor (ldmxcsr, SetLastError). Needed for correctness on every call: the
forward, and the shadow store when a hook remains installed. The mutex is
needed only while more than one thread can enter (see b). The envelope is
needed only to hide the proxy's own CPU-state effects (see c). The stamps are a
diagnostic (see e).

## (b) Thread model and the mutex

Evidence that the device is driven from one thread: the game creates the
device without `D3DCREATE_MULTITHREADED`, which is the documented promise that
the application calls the device from one thread at a time; the proxy's own
composition permission already requires `ctx.scene_thread ==
GetCurrentThreadId()` and has never refused on that ground in run87; the lock
wait metric shows no contention. Direct per-call thread ids are not logged
(the profiler was off), so this is contract plus absence of contention, not a
per-call census.

Decision: keep `std::recursive_mutex`. It costs 6.8 ns per call, 0.2 ms per
busy frame, and it also serialises `log()` (which shares the same mutex) with
the profiler thread and the loader-side paths. An owner-thread fast path that
skips the lock is only correct if a foreign thread can still exclude the owner,
which needs a store-fence-load per call (a `dmb ish` under FEX, an `mfence`
natively) that costs about what the uncontended lock costs. A CAS-based
recursive lock would save at most ~5 ns per call. Neither is worth a new
synchronisation primitive on a hook path.

## (c) The CPU-state envelope

What FNSAVE/FRSTOR protects: the caller's x87 register stack, tags, control
word and status word across the proxy's own code. The i386 ABI already
requires the FPU stack to be empty at a call boundary, and the game passes
FPU_PRESERVE, so the only things the proxy can damage are the control word,
the status flags, MXCSR and LastError, and only by executing x87/CRT code
(logging, `double` returns such as `telemetry::us()`) or Win32 calls. The
forwarded native call needs no envelope at all: vanilla calls the same d3d9
without one, and `before_original`/`after_original` exist to make the proxy
transparent around it, not to protect it.

So on a hook whose reachable code is audited x87-free (`check_no_x87.py`; the last
recorded audit `verification/results/review30-no-x87.json` walked 196 reachable
functions from 53 roots, zero violations) the full envelope protects nothing.
`LightCallBoundary` (9.9 ns) keeps MXCSR/LastError transparency and stays.
The 1004 ns `CpuCallBoundary` remains on `set_stream_source`, `set_indices`,
`set_declaration`, `set_fvf`, the four draw hooks and the per-frame hooks. On
the setters it guards `resource_id` (GetPrivateData, a D3D accessor exactly
like the `GetLevelCount` the light `set_texture` already makes inside its
native section) and `GetDeclaration`, plus a `log()` on the never-observed
metadata-failure path. On the draw hooks it guards `telemetry::record`
(double arithmetic in `us()`), capture-only logging and the route. Making
those paths light requires: `log()` preserving CPU state itself
(`PreserveCpuState` at its entry, paid only when a line is written);
`telemetry::record` bucketing in integer ticks against precomputed thresholds;
the audit walking the new roots. Fail-closed: the audit is a build gate, and a
path that still reaches x87 fails the build rather than the game. Under FEX
each FNSAVE/FRSTOR pair is ~250 ns; natively it is tens of ns, so the gain is
FEX-specific while the change is portable.

## (d) Redundant-state elision

Not recommended. A shadowed-equal SetRenderState could return D3D_OK before
`before_original`, saving the 13.5 ns native call and ~5 ns of envelope; at 50
% redundancy over ~15k SetRenderState calls that is 0.15 ms per busy frame.
On native Windows the proxy's device is non-pure (PUREDEVICE stripped), and the
runtime already filters redundant state on non-pure devices, so elision there
changes nothing but the call count. For SetTexture the runtime AddRefs the new
and Releases the old binding; same-pointer elision is refcount-neutral, but the
mip-bias path needs the level count and the composition path needs the
container identity, so it would still do work. State that changes without the
hooks: state-block Apply and EndStateBlock (both hooked, both `resync_shadow`),
Reset (`after_reset` resyncs), the route's own `native<>` writes (restored per
draw, `invalidate_render_states` on a failed restore), lazy-mode held write
masks (flushed in `before_set_render_state`), SetRenderTarget (resets viewport
and scissor, not render states), device loss (Reset resyncs). All already
resync; recording (`shadow_.recording`) would have to disable elision because a
recorded call must reach the block regardless of the device value. The only
worthwhile addition is a per-entry call counter (below), not elision.

## (e) Frame-timing self-cost

With `X3M_FRAME_TIMING=1` every outermost hooked call pays ~229 ns: 30,076
calls is 6.9 ms of the 28.5 ms busy frame (16.4 ms of the 49 ms windows). The
stamp cost lands partly inside the measured bucket (the end QPC) and partly
outside (begin QPC, scope bookkeeping), so the bucket overstates the game's
share of the hooked call and the "remainder" hides the rest of the diagnostic.
The user should play with `--frame-timing` off. The diagnostic should keep the
draw, scene and Present stamps and make the State bucket count-only by default
(one increment, no QPC), with full stamps behind `X3M_FRAME_TIMING=2`.

## (f) The 5 µs per draw

Measured components: `CpuCallBoundary` 1.0 µs; ten QPC stamps per draw
(`HeldHookLock` lock-wait pair, `CallTimer` four, `frame_timing` four) 0.68
µs. The remaining ~3.3 µs is the motion route's `before_draw`/`after_draw`
(selector event, `bindings`, ten shadow reads, `restore_bindings_checked`,
composition/source-gain/cutout admission) and is not split by any current
measurement; `X3M_TELEMETRY_DRAW=1` records `RouteGate`/`RouteDraw` and would
split it in one run. The `CallTimer` constructor/destructor stamps are wasted
when the frame is not captured (only `begin`/`end` feed `DrawBackend`).

## Unhooking the light setters (coordinator option)

Read the needed states at draw time instead of shadowing every write. Get* is
available: the proxy already strips PUREDEVICE and `resync_shadow`,
`render_state()` with `X3M_STATE_SHADOW=0` and `recover_motion_state` use
GetRenderState today; on native Windows a non-pure device keeps the runtime
state copy that Get* reads (documented; PUREDEVICE is exactly the flag that
removes it). Every reader of the render-state, blend, fill-mode and sampler
sRGB shadows is at a draw or at a restore-after-substitution (`motion_output.cpp`
3593-3772, 3870, 4048, 4191-4228, 4454), never between draws. Between-draw
duties of the three hooks: the lazy-mode flush (`X3M_MOTION_RT_MODE=lazy`,
not the production mode); the mip-bias restore when the application writes
LODBIAS while the route's bias is still on the device (replace by restoring
the bias right after each routed draw, one native SetSamplerState per biased
stage); the composition reader identity (`QueryInterface` once per changed
pointer, replaced by a GetTexture pointer compare per sampled stage at the
draws that need it). SetVertexShaderConstantF must stay hooked: the reserved
range check (c252-255, ps c216-217) is a write observation Get* cannot make,
and its hook is the cheapest (79.5 ns). SetVertexShader/PixelShader, stream,
indices, declaration, viewport and targets stay hooked.

Cost: about ten GetRenderState per draw (13-15 ns each) plus GetTexture/
GetLevelCount/Release for the biased stages of routed draws, ~0.15-0.4 µs per
draw, 0.07-0.2 ms per busy frame. Saving: the SetRenderState, SetSamplerState
and SetTexture share of the 30k calls at ~110 ns (timing off). That share is
not measured (the bucket has one counter); at the plausible 60-70 % it is
~2.0-2.3 ms per busy frame, at 40 % ~1.3 ms. What is lost: per-write telemetry
(`mip_bias_game_writes`, `rs_invalidations`, sRGB write admission becomes
sRGB value admission), the setter-failure seams (`fixture_setter_result` on
slots 57/69) and the whole render-state resync machinery (a simplification).
Risk: medium; it touches admission inputs of every material route, so the
motion-output fixtures must hash-match before and after.

The hybrid (drop the render-state and sampler hooks first, keep texture,
constants and targets) is the right first slice of this option: the render
state hook's only production duty is the shadow and the sampler hook's only
between-draw duty is the LODBIAS restore. The install gate for slot 57 already
exists (`capture.cpp` 2026: installed only for shadow, lazy mode or
composition), so the slice is "composition and admission read at draw,
`X3M_STATE_SHADOW` default off, sampler admission by GetSamplerState". It is
not the first step overall: it depends on the per-entry counts to know its
value and on a benchmark row for Get*, and steps 1-3 are larger or contained.

## Recommendation, in order (ms per run87 busy frame, 457 draws, 30,076 calls)

1. Play with `--frame-timing` off; then make the State bucket count-only with
   per-entry counters (calls per hooked slot in the per-frame sample, one
   increment each) and full stamps behind `X3M_FRAME_TIMING=2`. Saves 6.9 ms
   (diagnostic self-cost). Fixture: `frame_timing_host.cpp` for the reduction;
   the benchmark's `proxy-timing-off` row is the per-call proof. Risk none.
   The counters also settle the setter mix that steps 3 and 5 depend on.
2. `CpuCallBoundary` to `LightCallBoundary` on the four draw hooks, with
   `log()` self-preserving, integer telemetry bucketing and the audit
   extended to those roots. Saves 457 × 0.99 µs = 0.45 ms; also drop the
   `CallTimer` outer stamps when not capturing (0.06 ms). Fixture:
   `check_no_x87.py` on the built DLL (build gate), `run_motion_output.py`
   seams unchanged, `hook_admission` benchmark. Risk low with the audit;
   behaviour outside the audited graph is unchanged.
3. Same conversion on `set_stream_source`, `set_indices`, `set_declaration`,
   `set_fvf` (`GetPrivateData`/`GetDeclaration` inside the native section as
   `set_texture` already does; `PlainHookGuard` drops the lock-wait QPC pair).
   Saves 1.1-1.25 µs per call; at two to four such calls per draw 1.0-2.3 ms.
   The count comes from step 1's counters. Fixture: the benchmark's
   SetStreamSource row (target under 150 ns), the same seams. Risk low.
4. Per-call dispatch on the remaining light hooks: a one-entry device cache
   before `devices.at`, a 256-entry `shadow_index` table (D3DRS max is 209;
   out of range fails closed to "not shadowed"), inline null-monitor admission.
   Target ≤ 40 ns over native (from ~108). Saves ~60 ns × ~28k = 1.7 ms if
   all light hooks stay, less after the hybrid. Fixture: benchmark rows for
   SetRenderState/SetTexture/SetSamplerState; a host test that the table
   equals `shadow_index` for 0..255; seams unchanged. Risk low.
5. The hybrid unhook (render-state and sampler hooks) once step 1's counts and
   a Get* benchmark row are in: 1.3-2.3 ms, medium risk; it replaces step 4 for
   those two entries and removes the render-state resync path. Full unhooking
   of SetTexture is a later slice if the composition reader detection can be
   moved to the draw at acceptable cost.
6. Elision: not done (≤ 0.15 ms, no Windows benefit).

Expected result after 1-4: the hooked state calls cost ~30k × (14 + ~40) ≈
1.6 ms instead of 8.9 ms measured plus ~6 ms hidden; per-draw proxy work ~3.5
µs (1.6 ms); scene passes 1.1 ms; the busy frame falls from 28.5 ms to roughly
18-19 ms if the game's own CPU time is what the remainder implies. After 5 the
hooked calls cost a further ~0.6-0.9 ms less. These are projections from the
benchmark's per-call numbers and run87's counts, not measured frames.

## Envelope (steps 2 and 3, implemented 2026-09-16)

`CpuCallBoundary`/`HookGuard` became `LightCallBoundary`/`PlainHookGuard` on
`set_stream_source`, `set_indices`, `set_declaration`, `set_fvf` and the four
draw hooks (`capture.cpp`); the `CallTimer` outer QPC pair now runs only on
capture frames and its inner pair only when `DrawBackend` telemetry is on.
What the draw path reached that was x87, found by extending
`check_no_x87.py` to root at those eight hooks: `log()` (the MinGW `vfprintf`
formatter), `telemetry::record` (`us()` double bucketing, `fildll`) and its
once-per-second `summary` (formatting, the loading reporters), `snapshot`'s
`snprintf` and `shader_id`'s `swprintf` (capture only), `std::fabs` in
`fade_region_math.h` (the MinGW header inlines it as x87 `fabs`),
`std::floor`/`std::ceil` there (CRT routines returning in `st(0)`),
`std::sqrt(float)` in `fade_route_core.h` and `evaluate_draw` (`_sqrtf` is
x87) and `unsigned(float)` in `fade_route::permille` (`fistpll`). Fixes:
`log()` formats under `call_preserved` (a full FNSAVE/FRSTOR save around an
indirect call, paid per written line; `cpu_state.h`), `record` buckets in
integer ticks against edges computed at `initialize` and calls `summary`
through `call_preserved`, the shader dump's formatting and file write run
under `call_preserved`, the render-target role string is built by hand, and
`sse_scalar.h` supplies `abs`, `floor`, `ceil` (truncating conversion, so
the result does not depend on the application's live MXCSR rounding mode)
and `sqrt` (`sqrtss`). The stream/indices/declaration shadows keep their D3D
accessors after the native call (D3D runtime entry points like the slot
itself; LastError/MXCSR restored by the boundary's destructor). Audit: 72
roots, 480 reachable functions, 0 violations. Measured (same fixture, X3
bottle, worktree DLL `207d4ede`, `state-hook-benchmark.json`): SetStreamSource
133.3 ns timing off (was 1393.7), 371.5 timing on (was 1618.9); the
SetStreamSource + DrawIndexedPrimitive pair 1275.8 ns timing off (was 4197.4),
1877.2 timing on (was 5276.4), against 389.2 native, so ~0.89 µs of proxy
work per pair with the timing off. Fail-closed proof: the fixture's
`PRESERVE` rows enter SetStreamSource, SetIndices, SetVertexDeclaration,
SetFVF, SetRenderState, DrawIndexedPrimitive and DrawPrimitive with three live
x87 registers, a sticky invalid flag, control word 0x0f7f, MXCSR 0xbf80 and
LastError 0x3ac, and every row reports the FNSAVE image, MXCSR and LastError
unchanged in both proxy configurations (and natively). `lock_wait` (`HeldHookLock`) is now sampled only by the remaining `HookGuard` users: begin_scene, end_scene, present, reset, clear, set_rt, set_depth, get_rt, get_rt_data, stretch_rect, color_fill, update_surface, update_texture, the create_* resource/shader/query/state-block entries, query_issue/query_release, stateblock begin/end/apply/release, get_render_state, draw_rect_patch/draw_tri_patch, the cursor hooks and device/factory release; its count per interval drops by the draw and binding calls (run87: most of the 249 per second), which tools/analysis/analyze_iteration07_taa.py, analyze_iteration09_cost.py and analyze_iteration10.py read as `lock_wait`.
`CpuState::capture` now leaves the FPU initialised (FNSAVE, then FNINIT) instead
of restoring the live image, so heavy-envelope code and the formatter under
`call_preserved` run on an empty x87 stack; `restore` is unchanged. Run 87's
per-draw figure is not remeasured here; the next flight capture settles it.

## Dispatch trim (step 4, implemented)

Implemented in `capture.cpp` (light hook bodies, the device lookup, the null
admission early-out) and `motion_output.cpp` (the index tables): a one-entry
device cache ahead of `devices.at` (any other pointer falls back to the map
lookup, which for an unknown device terminates instead of throwing); 256-entry compile-time
`shadow_index`/`composition_blend_index` byte tables built from the old scans,
with a `static_assert` that the tables equal the scans for 0..255 and an
out-of-range value keeping the scans' "not shadowed" answer; an inline null
test on the admission monitor, which is now published for an inline read
(`process_admission_monitor_published`), so the option-off path constructs no
ABI adapter and makes no cross-unit call; `noexcept` on the forwarded native
slot of the light setters and on the admission adapter's constructor and
destructor (its unit is already `-fno-exceptions`), which removes the
exception regions that surrounded the native call; and inline guards that skip
`before_set_render_state` unless the route holds a write mask (lazy mode only)
and `before_set_sampler_state` unless the write is `D3DSAMP_MIPMAPLODBIAS`.
The shadow's contents, the resync semantics, the telemetry counters, the hook
mutex and the CPU-state envelope are unchanged.

Benchmark, ns per call with timing off, X3 bottle, same fixture. Baseline:
`state-hook-benchmark.json`, DLL `207d4ede`, which is main after the envelope
merge (e8bac89). Trim: `state-hook-benchmark-dispatch-trim.json`, DLL
`5b8e6f8d`, built from this branch's point 27bfdc4, that is BEFORE the envelope
merge. The two DLLs therefore differ by both changes, and only the rows the
envelope did not touch are a valid A/B. Those are the per-state-write setters:
SetRenderState 119.6 -> 79.0, SetSamplerState 108.3 -> 68.6,
SetVertexShaderConstantF(4) 79.0 -> 77.0, SetTexture 126.4 -> 125.9, and the
record's mean proxy guard/shadow/dispatch cost of those four, 93.7 -> 73.5 ns.
The unhooked control is unchanged (SetTextureStageState 10.9 -> 11.0 through
the proxy) and the getters are within noise.

Not comparable in this pair, because the trim DLL still has `CpuCallBoundary`
on the draw and binding hooks: SetStreamSource (133.3 baseline against 1398.5
in the trim record), the SetStreamSource + DrawIndexedPrimitive pair (1275.8
against 4322.8) and the equal-share `state_mix` (98.4 against 302.8, since the
mix includes SetStreamSource). Those trim-record numbers are the pre-envelope
values, not a regression. The combined per-call figures on one DLL carrying
both changes were measured on the run 31 candidate (commit 4adf3dd, DLL
`a9ebfa3b`, `verification/results/bottle-X3/state-hook-benchmark-run31.json`,
bound by `verification/results/run31-candidate-build.json`). Timing off, ns per
call, against the same `207d4ede` baseline: SetRenderState 119.6 -> 79.5,
SetSamplerState 108.3 -> 68.5, SetVertexShaderConstantF(4) 79.0 -> 75.4,
SetTexture 126.4 -> 123.9, mean proxy guard/shadow/dispatch cost 93.7 -> 71.8.
The rows the envelope owns, now on the same DLL as the trim: SetStreamSource
133.3 -> 134.1, the SetStreamSource + DrawIndexedPrimitive pair 1275.8 ->
1092.1 and the equal-share `state_mix` 98.4 -> 84.6, that is the envelope's
gain over `CpuCallBoundary` is retained and the trim adds to it.

The ≤ 40 ns-over-native target is not reached. What remains per call, measured
or counted: the forwarded native call (11-17), the hook mutex (6.9), the
`LightCallBoundary` envelope (10.0, two GetLastError/SetLastError pairs), one
`__Unwind_SjLj_Register`/`Unregister` pair, and the out-of-line shadow store.
The SJLJ frame cannot be removed from these hooks while the reachable code can
throw: `std::lock_guard` on the recursive mutex and the `dllimport`
GetLastError/SetLastError of `cpu_state.h` are both potentially-throwing to
GCC. Moving the lock into nothrow out-of-line helpers was measured and
rejected: each helper then carried its own SJLJ frame and the four light
setters lost 30-70 ns (SetVertexShaderConstantF 77 -> 147, a measurement kept
only in this note). Removing the frame needs the light hooks in a
`-fno-exceptions` unit, which is a separate change; step 5 (the hybrid unhook)
removes the render-state and sampler hooks altogether and is the larger
remaining saving. SetTexture is dominated by its two out-of-line shadow
queries, not by dispatch.

## Hybrid unhook (step 5, implemented)

Implemented in `capture.cpp` (the render-state configuration decided per
device in `hook_device`, the slot-57/69 install gates) and `motion_output.cpp`
(the draw-time readers). In the production configuration (`X3M_STATE_SHADOW`
unset, now "auto") the SetRenderState and SetSamplerState hooks are not
installed. The hooks stay installed for exactly four reasons, logged per device
as `state_hooks device=N installed=1 reason=<explicit|lazy_rt|frame_timing|
get_failed>`: `X3M_STATE_SHADOW=1` (the shadow as before), lazy RT mode
(`X3M_MOTION_RT_MODE=lazy`: the held write masks need the write observation),
`X3M_FRAME_TIMING=1` (the diagnostic build keeps both hooks so its per-entry
state-call counts stay complete), and a failed capability check. The check
issues one `GetRenderState(D3DRS_ZENABLE)` and one
`GetSamplerState(0, D3DSAMP_SRGBTEXTURE)` through the saved native entries at
device creation; a device that refuses either (documented behaviour of a
`D3DCREATE_PUREDEVICE` device, which the proxy strips) fails closed to the
hooked configuration. Composition no longer forces slot 57: its blend and
fill-mode needs are value reads, not write observations. SetTexture,
SetVertexShaderConstantF, the shaders, stream, indices, declaration, viewport
and targets stay hooked; the composition reader identity stays on SetTexture.

Draw-time reads with the hooks off: `before_draw` drops a per-draw cache
(`begin_draw_reads`: the render-state, blend and fill-mode known flags and
the sampler sRGB/MIPFILTER flags; a biased stage keeps its saved LODBIAS),
`render_state` fills that cache like the shadow, and the readers that used to
test the shadow's flags directly (composition's fade/screen state, the
additive option, the source-gain law, the material sampler gate, the fade
rectangle's fill mode, the sun writer's write mask) go through `state_known`/
`blend_known`/`fill_mode_known`/`sampler_srgb_known`, which read once per
state per draw and count into `rs_queries`/`rs_hits`/`rs_gets` (the sampler
read included: one query, a hit or one native read, so the runner's
`rs_gets = fill + queries - hits` identity holds in every mode). The bounded
first `linear_material_refused` log reads the six stages through the same
helper with the hooks off, so its `unknown=` mask means a failed read there,
not an unqueried stage. The
restore-after-substitution sites (DESTBLEND for the screen law, the additive
alpha triple, the wrap states, COLORWRITEENABLE1/2) restore the values the
same draw's admission cached, so admission and restore see one consistent
snapshot in both configurations. The mip bias: `apply_mip_bias` runs as
before at the routed draw (MIPFILTER and the saved LODBIAS re-read per routed
draw, since no hook sees the application change them) and `after_draw` puts
the bias back right after the draw, one native SetSamplerState per biased
stage, so no application LODBIAS write can land under the route's bias. With
the hooks on nothing changed: the helpers return the shadow's flags, the
resync and invalidation paths are the hooked configuration's.

Telemetry: the per-frame summary carries `rs_mode=get|shadow|native` beside
`state_shadow`; in get mode `rs_invalidations` is 0 by construction (no hook,
nothing to invalidate) and `rs_resyncs` still counts the shadow resyncs of the
other bindings. Lost with the hooks off: `mip_bias_game_writes` (no write
observation; the saved value is read instead), the `rs_invalidations` of failed
application setters, the `fixture_setter_result` seams on slots 57/69 (they
run in the hooked fixture cases), and the "sRGB write admission" becomes sRGB
value admission (a failed application SRGBTEXTURE write leaves the device
value, which is what is read). The `motion_output_mode` line prints
`state_shadow=auto|0|1` (the request: exactly `1` or `0`; any other value is
auto and logged once as `state_shadow_setting ignored=1`); the device line
adds `state_hooks=`.

Frame timing measures the hooked configuration: a `--frame-timing` session
keeps slots 57 and 69 installed, so its state-call counts and per-call costs
are those of the hooked build, and the felt FPS of the unhooked configuration
comes from a session without `--frame-timing` (the frame-phase stamps need
only `--telemetry`).

Benchmark (`state-hook-benchmark-hybrid.json`, X3 bottle, timing off, ns per
call; `state-hook-benchmark-run31.json`, DLL `a9ebfa3b`, as the previous
record). DLL provenance, stated plainly: the benchmark ran on `c136e425`, the
cmake build of the merged tree (fe25ca7: this branch plus main's frame-timing
counters) before the last review fix (the blend triple of the fade arm and
the cutout marker now fills the per-draw cache; no hook or setter path
changed); the fixture reruns below ran on the runner's own clean builds of
the same tree (`ee80fdf3` for the six twins at 7b2a611, `e371c83b` for the
production-configuration rerun on the final tree); no committed DLL carries
both proofs. Builds of one tree are not byte-reproducible here (PE timestamps
and debug line tables), so the run 32 candidate build rebinds both: it reruns
the benchmark on the candidate DLL and the twins rerun is the candidate's
fixture run. Rows on `c136e425`: production SetRenderState 15.0 (native
14.1; run31 hooked 79.5; this DLL hooked 78.8), SetSamplerState 10.4 (native
11.1; run31 68.5; hooked 67.7), the per-draw read set `GetState_draw_set_10`
(eight GetRenderState + two GetSamplerState) 90.9 per draw (native 92.7),
SetStreamSource 131.9 (run31 134.1), the SetStreamSource + DrawIndexedPrimitive
pair 1015.1 (run31 1092.1), SetTexture 123.7 (run31 123.9),
SetVertexShaderConstantF(4) 74.2 (run31 75.4), equal-share `state_mix` 63.6
(run31 84.6; the mix still carries the hooked SetTexture, constants and
SetStreamSource). The SLOT lines of the production case show SetRenderState and
SetSamplerState owned by the backend, the three retained setters and the draw
by the proxy; with `X3M_FRAME_TIMING=1` both are the proxy's again (152.0 /
143.0 with the stamps on), which is the `state_hooks_reason == "frame_timing"`
branch of `hook_device` and the `frame_timing::state_write` calls inside the
shadow updates counting as before. Projection for run89's busy frame (18,449
SetRenderState + 31,149 SetSamplerState per 987 draws): 49,598 × (~74 − ~12)
≈ 3.1 ms of hook cost removed, against 987 × ~91 ns ≈ 0.09 ms of draw-time
reads added plus the mip-bias row below; a projection, not a measured frame.

Mip bias per routed draw, measured rather than argued. With the hooks off
`apply_mip_bias` re-reads MIPFILTER and MIPMAPLODBIAS of every bias-eligible
stage and sets the bias, and `after_draw` puts the saved value back: two Gets
and two Sets per stage per routed draw. The row `routed_draw_mip_bias_2stages`
(two stages, the whole sequence as one "call") costs 78.0 ns in the production
configuration (native 78.3; 463.2 in the hooked configuration, where the row's
SetSamplerState calls pass through slot 69, which the route's own native
writes never do, so that figure overstates the hooked route). At two eligible
stages that is ≤ 987 × 78 ns ≈ 0.08 ms on run89's busy frame if every draw
routed. The alternative, keeping the bias across consecutive routed draws
with the hooks off, was rejected: without a write observation it would still
need the two Gets per stage per routed draw to detect an application
MIPFILTER or LODBIAS write, could not tell an application write of exactly the
bias value from its own, and would restore a stale saved value over it; the
after-draw restore is exact and costs the two Sets. The hooked configuration
keeps the bias across consecutive routed draws as before.

Fixture (`run_motion_output.py`, worktree build, X3 bottle, compact witness
`verification/results/bottle-X3/motion-output-hybrid-twins.json`, untracked):
the six `X3M_STATE_SHADOW=0` twins are identical to their shadow-on twins in
colour, pre-boundary colour, `state_hashes`, `motion_hashes`, readback files,
route decisions, checks, restorations, `motion_pixels` and depth pixels, and
equal the committed record on `state_hashes`, `motion_pixels` and colour.
`production-shadow-off`, `seam-shadow-off`, `seam-taa-shadow-off` and
`seam-burst-perdraw-shadow-off` ran in `rs_mode=get` (205/205/205/513 queries
over nine frames, every one a native read: the regular script reads no state
twice in a draw; `rs_gets` = queries + the sentinel fill's 14 saves per
frame; 0 invalidations); the two lazy twins keep the hooks (`rs_mode=native`).
110 cases validated individually (the six twins in `rs_mode=get` served no
repeat read, `rs_hits=0`: the regular script reads no state twice per draw;
see the production-configuration rerun below for the cache proof), 98 of the
102 with a committed counterpart
equal it on colour, `state_hashes`, `motion_pixels`, checks and restorations;
the four that differ (`seam-hdr-exposure`, `seam-ownership-hdr-exposure`,
`seam-hdr-tonemap-fault`, `seam-taa-hdr-tonemap-auto`, colour only, hooked
configuration) equal the main checkout's pre-change seam DLL (source
`4adf3dd`) frame for frame in a retained-binary A/B: drift since the
2026-09-15 record, not this change (resolved on main by 39d9863: the record
predates 3df7b9f's material fill 0 and EV ceiling 1.5, which the runner now
pins on those cases). Two harness findings outside the change:
the runner's TAA cases did not pin `X3M_TAA_SHARPEN`/`X3M_TAA_MIP_BIAS`, which
`ca6ad2e` defaulted to 0.75/-0.5 after the last full run, so
`production-taa-on` failed its FP16 round-trip check with any current DLL
(the installed `a9ebfa3b` included); the runner now pins both to 0 for TAA
cases that do not set them. `seam-taa-fade-route-routed` fails "routed fade
draw equals the fade oracle" (frame 0, pixel 12,12, channel 2: 0.4651 against
0.4583) with this worktree's seam DLL and with the main checkout's `4adf3dd`
seam DLL alike; the suite stops there, so the run's own cross-case block did
not execute and the twin comparison above was made from the per-case records
with the runner's assertions. Resolved on main by 39d9863 (the same fill-0
pin on `CUTOUT_ENV`): with it the fade oracle matches bit-identically.

Production-configuration rerun (review follow-up, merged tree with 39d9863's
pins, runner build `e371c83b`, recorded under `production_configuration_rerun`
in the twins witness): the runner now runs every production-DLL case with
`X3M_STATE_SHADOW` unset except `production-on` (the shadow twins' reference)
and the mip-bias script cases (their hand-derived per-frame sampler counts
describe the hooked configuration); the eighteen switched cases and the new
`seam-taa-cutout-opaque-get` case (the opaque cutout script per-draw with the
hooks off) reran as selected sets on one retained binary set and every one
equals the committed record on colour, `state_hashes`, `motion_pixels`,
checks, restorations, route decisions and readback hashes. Sixteen ran in
`rs_mode=get` (205 queries, 0 hits, 331 gets per case over nine frames; the
burst script 513/0/639), the two lazy cases keep the hooks with the shadow on
(`production-lazy-on` 205 queries, 183 hits; `production-burst-lazy` 485/485).
The cutout case is the per-draw cache's proof: the gate reads ALPHATESTENABLE
and the eight cutout states and the candidate marker reads them again, 426
queries, 66 hits, 528 gets over twelve frames, `rs_invalidations=0`, the same
coverage verdicts as the lazy hooked twin. 104 runner cases still set
`X3M_STATE_SHADOW=1`: the seam-DLL cases, whose fixture seams (setter
failures, the lazy hole) need the hooks, and the hooked references.

## Native Windows

Every step uses documented D3D9 and Win32 only. The FNSAVE cost and the 68 ns
QPC are FEX/Wine figures; natively FNSAVE/FRSTOR are tens of ns and QPC ~20-30
ns, so steps 1-3 gain less there but change no contract. Get* on a non-pure
device (step 5) is the documented path; the per-device capability check fails
closed to the hooked configuration when a device refuses the reads. None of
it is verified natively; `platform-portability.md` carries the entry.

## Unknowns and what settles them

The setter mix per frame (step 1's counters). The 3.3 µs split of the draw
path (`X3M_TELEMETRY_DRAW=1` run). Get* cost under the bottle (add
GetRenderState, GetSamplerState, GetTexture+Release rows to
`state_hook_benchmark.cpp`). Whether the game ever uses state blocks (none in
run87). `compositor_pre` as the slowest call in 192 of 200 slow-frame
witnesses is a separate question for the bloom bridge.
