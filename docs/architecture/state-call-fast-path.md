# State-call fast path

Design note, 2026-09-16. Question: the proxy's hooked state-setter path is the
largest proxy cost in busy scenes; what to change, in which order, and what each
step is worth. Inputs: run87 (DLL `bbadc568`, source `f94290c`, `--frame-timing`
on, log `session-20260916-055042-216.log`) and the bottle microbenchmark
`verification/results/bottle-X3/state-hook-benchmark.json` (same DLL, section in
`docs/verification/sampling-profiler.md`). Nothing here is implemented.

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
16.7 / 79.5 / 310.2; SetStreamSource (heavy guard) 15.8 / 1393.7 / 1618.9;
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

## Dispatch trim (step 4, implemented)

Implemented in `capture.cpp` (light hook bodies, the device lookup, the null
admission early-out) and `motion_output.cpp` (the index tables): a one-entry
device cache ahead of `devices.at` (any other pointer falls back to the map,
which still throws for an unknown device); 256-entry compile-time
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

Benchmark, ns per call with timing off, installed candidate `bbadc568`
(`state-hook-benchmark.json`) against the trimmed build `5b8e6f8d`
(`state-hook-benchmark-dispatch-trim.json`, same bottle and fixture):
SetRenderState 119.9 -> 79.0, SetSamplerState 109.8 -> 68.6,
SetVertexShaderConstantF(4) 79.7 -> 77.0, SetTexture 130.0 -> 125.9, the
equal-share mix 316.8 -> 302.8; the mean proxy guard/shadow/dispatch cost of
the four light setters 96.0 -> 73.5 ns. Native and unhooked rows are unchanged
(SetTextureStageState 11.7/11.0, the getters within noise). `set_render_state`
falls from 211 to 143 emitted instructions with far fewer on the taken path.

The ≤ 40 ns-over-native target is not reached. What remains per call, measured
or counted: the forwarded native call (11-17), the hook mutex (6.9), the
`LightCallBoundary` envelope (10.0, two GetLastError/SetLastError pairs), one
`__Unwind_SjLj_Register`/`Unregister` pair, and the out-of-line shadow store.
The SJLJ frame cannot be removed from these hooks while the reachable code can
throw: `std::lock_guard` on the recursive mutex and the `dllimport`
GetLastError/SetLastError of `cpu_state.h` are both potentially-throwing to
GCC. Moving the lock into nothrow out-of-line helpers was measured and
rejected: each helper then carried its own SJLJ frame and the four light
setters lost 30-70 ns (SetVertexShaderConstantF 77 -> 147). Removing the
frame needs the light hooks in a `-fno-exceptions` unit, which is a separate
change; step 5 (the hybrid unhook) removes the render-state and sampler hooks
altogether and is the larger remaining saving. SetTexture is dominated by its
two out-of-line shadow queries, not by dispatch.

## Native Windows

Every step uses documented D3D9 and Win32 only. The FNSAVE cost and the 68 ns
QPC are FEX/Wine figures; natively FNSAVE/FRSTOR are tens of ns and QPC ~20-30
ns, so steps 1-3 gain less there but change no contract. Get* on a non-pure
device (step 5) is the documented path. None of it is verified natively; the
existing gap entry in `platform-portability.md` covers it.

## Unknowns and what settles them

The setter mix per frame (step 1's counters). The 3.3 µs split of the draw
path (`X3M_TELEMETRY_DRAW=1` run). Get* cost under the bottle (add
GetRenderState, GetSamplerState, GetTexture+Release rows to
`state_hook_benchmark.cpp`). Whether the game ever uses state blocks (none in
run87). `compositor_pre` as the slowest call in 192 of 200 slow-frame
witnesses is a separate question for the bloom bridge.
