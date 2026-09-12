# Live same-draw motion route

Design record, 2026-09-12. The project direction changed: per-pixel motion is
produced by the game's own material draws through transformed shader variants
and a second render target, not by deferred geometry replay. The replay
admission, execution-scope and geometry-lease machinery stays in the tree as a
numerical reference but is **not** a prerequisite for this route. This document
defines the live integration boundary that the
[strategy](motion-output-strategy.md) left open after the
[detached prototype](../verification/material-motion.md).

## Why replay infrastructure is no longer on the critical path

Replay needed exclusive device access, retained vertex/index storage, finite
position evidence and a second rasterization with exact depth equality. A
same-draw variant needs none of that: the application's own draw already binds
the geometry, states and depth test. The only proxy work is ordinary
application-thread device calls made inside the existing draw hook, which the
capture mutex already serializes. Concretely, the route requires:

| Need | Mechanism | Existing pieces |
| --- | --- | --- |
| Substitute a shader pair for one draw | `SetVertexShader`/`SetPixelShader` around the native draw, then restore | private vtable hooks in `src/proxy/capture.cpp` |
| Supply previous rows and pixel ABI constants | `SetVertexShaderConstantF(252,…,4)`, `SetPixelShaderConstantF(216,…,2)`, restore only if the application had written those ranges | new setter hooks that shadow constant ranges |
| Bind RT1 and restore | `SetRenderTarget(1, motion)` before, `SetRenderTarget(1, nullptr)` after | scene-boundary adapter must ignore our own calls |
| Own RT1 across Reset/device loss | release before native `Reset`, drop on device release, recreate lazily | capture `reset`/`release_device` hooks |
| Cross-frame object identity | `(load_epoch, registry_epoch, node_serial, camera_serial)` from the lifetime observer | `object_trace`, `object_lifetime`, `MotionHistory` key |

Nothing here writes game vertex/index buffers, retains application COM
references across frames, or executes while the application is inside another
device call. Windows and CrossOver Preview both remain targets: every call is a
public D3D9 method, and the engine hooks are game-executable hooks, which the
user explicitly allows.

## Frame flow

```text
Present (frame N-1 succeeded)  → history.commit(): current rows become previous
Clear color+depth on main RT   → selector latches main/depth; motion RT fill draw
                                 writes the invalid sentinel (0,0,0,-1) to RT1
background draws               → no motion (not yet eligible; sentinel stays)
depth-only Clear               → selector enters Scene
material draws (Scene phase)   → eligible pair + known node scope:
                                 lookup previous rows, bind variant + RT1, draw,
                                 restore; record current rows under the key
StretchRect / bloom / overlays → selector leaves Scene; no routing
Present                        → commit or invalidate history; optional readback
```

Eligibility is decided per draw, fail-closed, from the following gates. Any
failed gate draws the original pair with no RT1 bound, exactly as today.

1. Feature switch `X3M_MOTION_OUTPUT=1`, device caps checked once:
   `NumSimultaneousRTs >= 2`, `MaxVertexShaderConst >= 256`,
   `D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS`, A32B32G32R32F render-target support
   with the main color format, and a passing one-time mixed-format MRT self test.
2. Scene phase: `SceneCapture::collecting_scene()` is true and RT0 is the
   selector's main color surface with its D24X8 depth bound. Selection runs every
   frame in this mode, not only in requested capture frames.
3. Shader pair: the currently bound VS and PS are both originals that have a
   registered variant, and the pair is one row of the reviewed profile table
   (16 class A/B/C pairs; see "Pair keying" below).
4. Draw state: alpha blend off, alpha test off, sRGB write off, Z enable and Z
   write on, COLORWRITEENABLE 15, no instancing on stream 0, integer constant
   i0.x in [0, 8] for profiles with a relative light loop.
5. Object scope: `object_trace::current()` and `object_lifetime::current()`
   return a known node and camera lifetime for this draw.
6. History: the previous frame committed, the same load/registry epochs and
   dimensions apply, and the key below matched exactly one previous entry that
   no other draw in this frame has consumed.

Gates 1–4 alone still permit drawing the variant with mode `c217.x = 0`, which
writes the invalid sentinel for the draw's pixels. That is deliberate: covered
pixels of an eligible material without history must not keep stale values from
the sentinel fill, and it lets the route be exercised before history exists.

## History key and previous rows

The key is `RigidDrawKey` from `src/renderer/motion_history.h` with these
fields populated: object and camera lifetimes (serials), load and registry
epochs folded into `draw_domain`, node and camera handles, model and LOD, vertex
and index buffer allocation identities, stream offset, stride, declaration
identity, topology, first index, primitive count, base vertex and vertex range.
The stored value is the four actually submitted rows `c24–27` (or the
profile's matrix register) at draw time, never a recomposed W·V·P
([camera numerics](../reverse-engineering/camera-numerics.md)).

Lookup happens during the frame against the sealed previous table while the
current table collects. Duplicate keys in the previous frame poison that key.
A duplicate in the current frame consumes the previous entry once; the second
draw gets no history. Both cases produce the sentinel rather than a guess.

Constant rows are read from a shadow maintained by hooking
`SetVertexShaderConstantF`, not by `GetVertexShaderConstantF` per draw. The
shadow also records whether the application has written `c252–255` or PS
`c216–217` since the last restore, so the route restores those ranges only when
the application depends on them.

## Jitter

Implemented in temporal step 1 (see "Implementation" below and
[temporal-integration.md](temporal-integration.md)). The sub-pixel clip offset
is applied in the same hook by rewriting the submitted rows
(`row0 += jx_ndc·row3`, `row1 += jy_ndc·row3`) for every scene draw whose VS
has a table row, and the previous rows stored in history stay jitter-free.
Because the history rows are unjittered, `c216.zw` is uploaded as **zero**:
the motion fragment then writes the previous unjittered UV the ABI specifies
and the resolve adds the previous raster jitter once itself.

## Verification

The first live checkpoint is diagnostic: in requested capture frames the route
reads RT1 back after the scene phase and writes it beside the capture log, and
the capture records per-draw route decisions (gate that failed, key match,
constants used). Offline analysis compares the live motion against the CPU
projection of the stored previous rows for the same pixels, using the same
convention the detached fixture verified. Color must remain bit-identical to a
run with routing disabled for the same scene. No temporal consumer reads the
output until that comparison passes on user-managed gameplay captures.

Integration tests run the real DLL under Wine with the synthetic device fixtures
in `verification/probe/` and check: capability refusal paths, sentinel fill,
variant substitution with restore, constant shadow/restore, Reset with RT1
owned, device release, and that non-eligible draws are untouched. The same
runs repeat under the ownership wrapper (`X3M_OWNERSHIP=1`, alone, with the
copy-depth/scene-depth path and with the admission monitor), because the
gameplay run needs the wrapper for object lifetime and the route's native
slots are then the wrapper's forwarders: the route must leave the wrapper's
depth epochs, scene/state-block flags and admission untouched, order its
target release before the wrapper's Reset, hold exactly one logical device
reference per owned object so the final-Release probe still matches, and
observe no loss the application would not observe. The analysis and results
are in [motion-output verification](../verification/motion-output.md)
("Ownership wrapper interaction"), together with the documented gameplay
diagnostic command.

## Coverage plan

Coverage is decided by the shipped archives, not by which sectors a capture
visited. [Motion output profiles](../reverse-engineering/motion-output-profiles.md)
classifies every vertex/pixel pairing that a technique pass of the 3,480
installed compiled effects binds (6,752 passes, 817 distinct pairings): all
**169 transformable SM3 pairs** are rows of the generated table (56 class A,
101 class B, 12 class C; 32 vertex and 108 pixel programs, `DEFAULT`,
`BUMPMAP` and `BUMPMAP_LOW` techniques of every material family, including
the six light-free asteroid/moon/planet_haze variants whose DP4 quad is
spaced), so a pair first drawn in a sector or race the user never tested is
already covered. The 11 SM3 pairs without a row are explicit: nine bloom
passes whose VS writes the position with `mov`, and the two
xt_standard_lighting_damage pixel programs with an `ifc` block. SM2 pairs
(466) carry a feasibility record but no row; SM1 pairs (168) are unsupported
(no second colour target in ps_1_x). Those, the bloom passes, background and
planet draws before the scene's depth clear, particles, stardust, overlays and
GUI keep the sentinel, so the temporal resolve rejects history there.

Capture-derived draw counts remain as metadata only. Against the one captured
session (28 frames, 11,493 clear-segment scene draws over 25 pairs) the 16
rows the session drew cover 97.6% (A 35.0% + B 21.9% + C 40.7%); the other
153 rows were never drawn there and have `observed_scene_draws` 0. The
remaining 2.4% of that session's scene draws (four bloom pairs, two SM2 and
three SM1 pairs) have no row. Against the 24 gameplay frames the runtime
boundary rules select (9,001 scene draws), the Argon pair alone is 24.2%,
between 0% and 41% per frame.

The [key validation](../reverse-engineering/motion-history-key.md) shows the
full key above matches 99.97% of keyable scene draws across adjacent frames
with no in-frame duplicates; dropping buffer identity produces ambiguous
sub-mesh splits. Two clip-row families exist among the rows (c24 with the
relative point-light loop, c0 for the light-free `_0000`/`_0001` variants);
the shadow captures both windows and gate 4 applies each row's own bound
(see [material-motion-prototype.md](material-motion-prototype.md)). A pair
refused at gate 3 now means a program outside the archives (a mod, a loose
override or a dynamically generated shader), not an unvisited sector; it
shows up in the per-frame gate histogram.

## Implementation (checkpoint B1, 2026-09-12; temporal steps 1 and 3 added the same day)

Delivered as a diagnostic route: the motion target (RT1) and, since temporal
step 1, the current-depth target (RT2), the per-draw jitter and the cut
detector are produced and read back; since temporal step 3 the route also
owns one `TemporalPass` per device and, with `X3M_TAA=1`, runs the resolve at
the game's pre-bloom copy and writes the resolved image back into the main
target (section "Temporal resolve at the bloom copy" below). Verified through
the synthetic fixtures in
[motion-output verification](../verification/motion-output.md); the gameplay
TAA run is user-managed.

### Files and switches

| File | Role |
| --- | --- |
| `src/proxy/motion_output.{h,cpp}` | Per-device route: variant registry, state shadow, motion (RT1) and current-depth (RT2) targets, capability self test, sentinel fill, gates, substitution/restoration, per-draw jitter, history, cut detector, diagnostics |
| `src/renderer/motion_row_history.{h,cpp}` | Pure in-frame previous-row table (lookup against the sealed previous frame while collecting); `MotionHistory` stays untouched as the replay reference |
| `src/renderer/material_motion.{h,cpp}` | Table-driven transformer, `material_motion_vertex_variant` / `material_motion_pixel_variant` (each stage is created separately by the game); the pair function remains for the detached fixtures; `material_motion_reviewed_pairs` is the profile table |
| `src/renderer/motion_output_profiles.h` + `motion_output_profiles_inc.h` | Row struct, class enum and the generated 169-row archive-wide table (classes A, B and C, each row with its current-depth registers) with compile-time consistency checks; see [material-motion-prototype.md](material-motion-prototype.md) |
| `src/temporal/current_depth_ps.hlsl` + `src/renderer/current_depth_pixel_program{,_inc}.h` | Authored depth fragment (`oC2 = z/w`), compiled by `tools/shaders/generate_rigid_motion_pixel.py` like the motion fragment |
| `src/proxy/capture.cpp` | Hook installation, state block and query wrapping, refcount-aware release, per-hook calls into the route; `X3M_MOTION_OUTPUT`, `X3M_MOTION_JITTER[_SAMPLES]`, `X3M_MOTION_CUT_*`, `X3M_TAA`, `X3M_TAA_DEBUG`, `X3M_MOTION_RT_MODE`, `X3M_MOTION_FRAME_LOG`, `X3M_STATE_SHADOW` and `X3M_TAA_MIP_BIAS` parsing; the lazy mode's restore points and `GetRenderTarget`/`GetRenderTargetData`/`GetRenderState` hooks; the light `SetRenderState` hook feeding the render-state shadow; `scene_end_signal`, the engine hook's listener |
| `src/proxy/scene_hook.{h,cpp}` | Engine scene-end boundary (`X3M_SCENE_HOOK`, default on with the route, `0` off): the five-byte callsite patch of `CALL 0x004c4750` at `0x004721b1` behind the exact-executable gate, its trampoline and restore; fixture seam for the runner's own callsite (section "Engine boundaries and state shadow") |
| `src/renderer/temporal_pass.{h,cpp}` + `temporal_resolve_program{,_inc}.h` | The resolve the route runs (native-slot calls, cached state block) and its embedded `ps_3_0` bytecode |
| `src/renderer/hdr_pass.{h,cpp}` + `hdr_writeback_program{,_inc}.h`, `hdr_tonemap_program{,_inc}.h`, `hdr_meter_program.h` + `hdr_meter_{level0,reduce}_program_inc.h`, `exposure.{h,cpp}`, `src/temporal/agx.{h,hlsl}` | FP16 HDR scene path (`X3M_HDR=1`): the owned `A16B16G16R16F` RT0, the capability gate and four-format self test, the write-back ladder (stage 1: identity; stage 2 with `X3M_HDR_TONEMAP=agx`: the AgX tonemap, the exposure meter chain and the host adaptation of `exposure.h`); the route decides when to redirect, flush and end ([hdr-scene-path.md](hdr-scene-path.md), "Stage 1 implementation" and "Stage 2 implementation") |
| `src/proxy/scene_capture.{h,cpp}` | `describe_surface` shared with the route |
| `src/proxy/camera_state.{h,cpp}` + `src/renderer/camera_reprojection.h` | Live engine camera read at the selector's Clear events behind the exact-executable gate (no patch), the far-plane `clip_to_previous` builder and the sentinel policy decision (`X3M_TAA_SENTINEL`, `X3M_CAMERA_CUT_DEG`, `X3M_CAMERA_LOG`); see [temporal-integration.md](temporal-integration.md#camera-reprojection-for-sentinel-pixels-2026-09-12) |
| `tools/manage.py` | `--motion-output` (history needs `--object-trace --object-lifetime`; otherwise sentinel-only), `--taa` (implies `--motion-jitter`), `--taa-debug`, `--taa-k`, `--taa-mip-bias <float>` (`X3M_TAA_MIP_BIAS`: the mip LOD bias of the routed material stages while the jitter is on, intended −0.5, default off), `--taa-sentinel auto|1|2`, `--camera-cut-deg`, `--camera-log`, `--state-shadow on|off`, `--scene-hook [on|off]` (default on with `--motion-output`), `--hdr` |

`X3M_MOTION_OUTPUT=1` enables the route. Without `X3M_OBJECT_TRACE=1` and
`X3M_OBJECT_LIFETIME=1` gate 5 never passes and every eligible draw writes the
sentinel (mode 0); the selector, fill, substitution and restoration still run.
`X3M_MOTION_JITTER=1` (default off) enables the per-draw jitter with a
centred Halton(2,3) sequence of `X3M_MOTION_JITTER_SAMPLES` entries (default
8, clamped to 2..64); `X3M_MOTION_CUT_MEDIAN_PX` (default 48, stated at
1280 px width) and `X3M_MOTION_CUT_MISSING` (default 0.25) are the cut
detector bounds. `X3M_TAA=1` (default off; requires `X3M_MOTION_OUTPUT=1`
and implies `X3M_MOTION_JITTER=1`) runs the temporal resolve at the bloom
copy; `X3M_TAA_DEBUG=<n>` (n > 0) writes the resolved FP16 image and the
pre-resolve color in capture frames. With `X3M_HDR=1` the resolve consumes
the FP16 scene target instead of the 8-bit RT0 and the write-back presents
its output (stage 3 of the HDR scene path,
[temporal-integration.md](temporal-integration.md#stage-3-of-the-hdr-scene-path-taa-on-hdr-2026-09-12));
`X3M_TAA_K=<k>` (0 ≤ k ≤ 65504; default unset) fixes the resolve's
luminance-weighting constant there, 0 being the unweighted resolve, where
the default derives it from the write-back's exposure. `X3M_MOTION_RT_MODE=lazy` (default
`perdraw`) keeps RT1/RT2 and `COLORWRITEENABLE1/2` bound across consecutive
routed draws and restores them before any application call that could
observe them (an A/B experiment; equivalence and the restore points are in
[motion-output.md](../verification/motion-output.md#lazy-rt-binding-equivalence-x3m_motion_rt_mode)
and [telemetry.md](../verification/telemetry.md#route-and-boundary-cost)).
`X3M_MOTION_FRAME_LOG=<n>` sets the periodic `motion_output_frame` cadence
with telemetry on (default 60). `X3M_TELEMETRY_DRAW=1` (default off) adds the
per-draw metrics (`route_gate`, `route_draw`, `route_set_rt`, `route_jitter`,
`route_lazy_flush`, `draw_backend`) and their frame totals; without it the
route takes no QPC stamp per draw. `X3M_ENGINE_READS=rpm` forces the object
observers' `ReadProcessMemory` path (A/B only; default validated direct reads). `X3M_TAA_SENTINEL=auto|1|2` (default
`auto`) selects the resolve's depth-sentinel policy: `auto` reprojects the
unrouted (sentinel) pixels through the live engine camera at the far plane
whenever the camera read of this frame and of the history's frame both
validate and the rotation between them is at or below `X3M_CAMERA_CUT_DEG`
degrees (default 20; above it the frame is a cut), `1` keeps them
current-only (the previous behaviour), `2` is strict and skips the resolve
on a frame whose camera cannot be read. `X3M_CAMERA_LOG=<n>` (default 300)
is the cadence of the `camera_state` diagnostic line (capture frames always
log it). The camera read needs the exact executable (the object-trace
identity gate) and `X3M_TAA=1`; it patches nothing. `X3M_STATE_SHADOW=0`
(default on, `--state-shadow off`) turns the render-state shadow off, so
every per-draw state query is a native `GetRenderState` again (A/B only).
`X3M_HDR=1` (default off, `--hdr`; requires `X3M_MOTION_OUTPUT=1`) redirects
the scene into an owned `A16B16G16R16F` RT0 at the latching Clear and writes
it back into the game's 8-bit main target with the stage-1 identity tonemap
at the scene end (the engine hook, else the bloom copy; `EndScene` flushes,
Present ends); every proxy consumer keeps seeing the application's logical
RT0; fails closed on the capability gate and self test; the per-frame
`hdr_frame` line and the `hdr_*` metrics report it
([hdr-scene-path.md](hdr-scene-path.md), "Stage 1 implementation").
`X3M_HDR_TONEMAP=agx` (default `identity`, `--hdr-tonemap`; requires
`X3M_HDR=1`) makes the write-back the AgX tonemap of the FP16 scene with
`X3M_HDR_DECODE=gamma2.2|srgb|none` (`--hdr-decode`, default gamma2.2),
`X3M_HDR_LOOK=none|golden|punchy` (`--hdr-look`), `X3M_HDR_CLAMP=<float>`
(`--hdr-clamp`, default off) and the exposure: `X3M_HDR_EXPOSURE=auto|manual`
(default auto: the log-luminance meter chain over the FP16 target, read back
one frame late, adapted on the host with `X3M_HDR_ADAPT_UP/DOWN` 0.4/1.2 s,
`X3M_HDR_KEY` 0.18, `X3M_HDR_EV_MIN/MAX` ±8), `X3M_HDR_EV=<offset>`
(`--hdr-ev`; alias `X3M_HDR_EV_OFFSET`), `X3M_HDR_EV_MANUAL=<ev>`
(`--hdr-ev-manual`: fixed EV, no meter) and `X3M_HDR_DT_MS` (a fixed
adaptation step for fixtures). A tonemap that fails to create or self-test
falls back to the identity write-back inside the enabled feature; a failed
tonemap draw takes the identity draw (an unwind, reason `tonemap`) and three
such failures disable the tonemap for the device. `hdr_tonemap` (attach),
`hdr_target` (`chain_bytes`) and the `hdr_frame` fields `tonemap`,
`tonemapped`, `look`, `decode`, `exposure`, `ev`, `ev_adapted`, `ev_target`,
`avg_log_l`, `luma_mean`, `dt_ms`, `meter`, `readback`, `meter_us`,
`readback_us`, `k` report it, with the metrics `hdr_meter` and
`hdr_meter_readback` ([hdr-scene-path.md](hdr-scene-path.md), "Stage 2
implementation").
`X3M_SCENE_HOOK` (default on with `X3M_MOTION_OUTPUT=1` since review 26;
`0` or `--scene-hook off` turns it off, `1`/`--scene-hook` forces the
request) patches the frame routine's compositing callsite so the route
learns the scene end from the engine and, with `X3M_TAA=1`, resolves there
instead of at the bloom copy (section "Engine boundaries and state shadow"
below); a refused patch leaves the bloom-copy/selector boundary in charge. With `X3M_TELEMETRY=1` the route reports
its CPU cost per call and per frame (gate, apply/undo, `SetRenderTarget`
count, jitter writes, fill, the resolve's phases, copy-back, readbacks,
render-state shadow hits/misses, engine-hook signals and the cross-check verdict).

### Current depth target (temporal step 1)

Every table row carries a second interpolator: the vertex variant exports the
current clip z and w of the rasterized position (two DP4s against
`c<matrix+2>` and `c<matrix+3>`, written to a second free output register
under a second free TEXCOORD index), and the pixel variant divides them into
`oC2`, so RT2 (R32F, main dimensions) holds ordinary device depth in [0,1]
wherever a routed draw covered the pixel and the -1 sentinel elsewhere. The
TEXCOORD index is chosen per VS/PS sharing component of the table, so the
per-program variant scheme above still links every pair; the header's
`static_assert`s cover the new fields. All 169 archive rows have the export
(`depth_output=true`); the format supports motion-only rows (register 255)
and the route creates motion-only variants (`current_depth=false`) on a
device that fails the RT2 gate: `NumSimultaneousRTs >= 3`, R32F render-target
support with the main format and a three-format self test (A8R8G8B8 +
A32B32G32R32F + R32F). The RT2 gate failing leaves the route motion-only
(`depth=0 depth_reason=...` on the device line); the two-format self test then
decides as before. The route owns RT2 alongside RT1 (same allocation
failure policy: a device producing depth must own both), fills both in one
sentinel draw (a second ps_2_0 writing `oC1 = -1`), binds RT2 with
`COLORWRITEENABLE2 = 15` around depth-capable routed draws and restores
`COLORWRITEENABLE2`, then RT2, before the RT1 restoration, releases RT2 with
RT1 before Reset and at device release, and reads it back in capture frames
as `depth_<device>_<frame>.r32f` (row-major R32F) beside the motion file.

### Per-draw jitter (temporal step 1)

The jitter sequence advances once per frame at the latching Clear (index
`latches mod samples`, sample `index + 1` of Halton(2,3) minus 0.5 per axis,
in raster pixels, +X right and +Y down). Every scene-phase draw whose bound VS
has a table row and whose clip-row window is known is jittered before the
gates that decide routing: the route writes the four rows of the VS row's
matrix register through the native setter with `rows[0] += jx_ndc·rows[3]`
and `rows[1] += jy_ndc·rows[3]`, `jx_ndc = 2·jx_px/width`,
`jy_ndc = -2·jy_px/height`, then draws (the variant or the original), then
writes the application's rows back bit-exactly from the shadow. The shadow
never sees the jittered rows, so the history records unjittered rows and the
route uploads `c216.zw = (0, 0)` (see "Jitter" above). Draws outside the
scene phase, draws whose VS has no row and the route's own fills are not
jittered. The current and previous jitter of the frame are in
`MotionFrameCounters` (`jitter`, `jitter_previous`, `jitter_index`) and on
the `motion_output_frame` line for the resolve caller. Jitter on changes the
rasterized color by construction; the fixture proves the sign and scale with
a coverage oracle at the jittered sample positions
([motion-output verification](../verification/motion-output.md)).

### Temporal resolve at the bloom copy (temporal step 3)

The game unbinds its depth surface (the selector's Scene to AwaitCopy
transition) and then copies the main target into the bloom source with a
full-rect `StretchRect`. The route's `before_stretch` runs in that hook
BEFORE the application's copy: when the selector is in AwaitCopy and a probe
copy of it advances to AwaitBloomTarget on the pending event (the source is
the latched main target, the destination a same-sized color texture), the
frame is a recognized scene frame. Preconditions, each a logged skip reason
and an invalidation of the history: the jitter is active (the resolve without
jitter only blurs), the sentinel fill succeeded and both RT1 and RT2 are owned,
no state block is recording, no application query is between `Issue(BEGIN)`
and `Issue(END)`, and the pass initialized (lazily, once per device: the
embedded resolve shader, one device reference). The route then obtains the
RT1/RT2 textures from its level-0 surfaces (`GetContainer`, released after the
run), builds `FrameInputs` (color_surface = the application's main surface,
current_depth = RT2, motion = RT1, main dimensions, epoch = the route's
resource generation, an identity clip-to-previous matrix because RT1 alpha is
always 1 or -1 so the camera path is never taken, current and previous jitter
in pixels, `cut` = the frame's verdict, `PerPixel` +
`DerivedFromDepthSentinel`, the tracked scene state, `caller_queries_idle`
from the query tracking) and calls `run`. On success the resolved FP16 surface
is copied back into the main target with a point-filtered full-rect
`StretchRect` through the native slot; the application's copy then proceeds
and everything after it (bloom, overlays, Present) sees the resolved image.
Any failure leaves the main target untouched (the pass writes only its own
targets and the copy-back happens only after a complete success),
invalidates the history and logs `motion_output_taa_failed` once for the
frame (at most 16 per device). History is also invalidated by the cut
verdict (inside the pass), before Reset, by a dimension change (the pass
compares), by a failed Present and by any frame that did not resolve
(menu frames, frames the selector rejected before the copy, skipped frames).
A rejection after the copy (a different bloom or overlay sequence) keeps the
history: the resolved scene is complete and the next correspondence is scene
to scene.

The pass calls the device only through the route's native slots (it is
handed the original vtable at initialization), so neither the shadow nor the
selector observes its calls; its `normalize` also resets stage 0's
fixed-function coordinate index and texture transform flags, which the
backend applies to the pre-transformed resolve quad. The attach gate
additionally asks `CheckDeviceFormatConversion` for the 8-bit-to-FP16 copies
(`taa_reason=format_conversion`). Its default-pool objects (two FP16 histories,
two R32F depth histories, the FP16 scratch, the cached `D3DSBT_ALL` state
block) are released after RT1/RT2 in `before_reset` and re-created lazily
after `after_reset(S_OK)`; the resolve shader survives Reset and is released
with the route's objects at the final device Release. The device references
those objects hold are counted by probing the device count before and after
every pass call (`taa_call`), which is exact in the native model and through
the ownership wrapper (where a texture and its level surface are two
children); while such a call runs, `device_references()` reports zero so the
child releases re-entering the device Release hook cannot match its
final-release probe with a stale count. Per frame the pass costs its two
`StretchRect` copies, one resolve draw, one `Capture`/`Apply` of the state
block and the route's copy-back; nothing is added per draw. In capture frames
with `X3M_TAA_DEBUG` the route also writes `color_<device>_<frame>.bgra8`
(the 8-bit main target before the resolve) and `taa_<device>_<frame>.rgba16f`
(the resolved FP16 output) beside the motion and depth readbacks. The frame
line carries `taa`, `taa_attempted`, `taa_resolved`, `taa_history`,
`taa_skip` (`TaaSkip`), `taa_result`, `taa_restore`, `taa_copy`,
`scene_open`, `active_queries` and `taa_references`.

### Engine boundaries and state shadow (2026-09-12)

Two of the assessment's recommendations
([assessment-2026-09-12.md](assessment-2026-09-12.md), 1–3) are implemented
behind their own switches, plus the pass field the third asked for.

**Render-state shadow (`X3M_STATE_SHADOW`, default on).** `SetRenderState`
(slot 57) is hooked with the light boundary of the other hot setters and
stores the application's value of every render state the route reads per
draw: `ZENABLE`, `ZWRITEENABLE` (the selector's z states, every draw while
tracking), `ALPHATESTENABLE`, `ALPHABLENDENABLE`, `COLORWRITEENABLE`,
`SRGBWRITEENABLE` (the gate-4 opaque-draw checks) and `COLORWRITEENABLE1/2`
(saved around RT1/RT2). Rules: the shadow starts unknown and each state
fills with one native `GetRenderState` on its first query; a recorded write
(between `BeginStateBlock` and `EndStateBlock`) never reaches the device and
is ignored; `EndStateBlock`, every state block `Apply` and `Reset` drop the
shadow (the existing `resync_shadow`), as does a failed restoration of the
route's own state changes (the fill, a routed draw, the resolve), after
which the next queries read again. The route's own writes (`COLORWRITEENABLE1/2`
= 15 around a routed draw, the fill's and the resolve's touched states) go
through the native slot and are restored, so the shadow keeps the
application's values. Routed-draw evaluation therefore issues **zero**
`GetRenderState` calls once filled; the remaining native reads per frame are
the sentinel fill's 14-state save. In lazy RT mode the same hook closes the
`COLORWRITEENABLE1/2` hole: an application write to a mask the route holds
first flushes the binding (quietly: no logging or telemetry record on the
light path; the metric and any failure line are deferred to the next heavy
call), and the application's `GetRenderState` (slot 58, lazy mode only)
restores before the native read; both are installed in lazy mode with the
shadow off too. Counters per frame: `rs_queries`, `rs_hits`, `rs_gets`
(native reads: misses plus the fill's save), `rs_resyncs`.

**Engine scene-end boundary (`X3M_SCENE_HOOK`; default on with the route
since review 26, `0` off).** At backend load, behind the object-trace
identity gate (exact X3AP.exe only),
`scene_hook::initialize` reads the five bytes at `0x004721b1`, requires
exactly `E8 9A 25 05 00` (`CALL 0x004c4750`; rel32 = `0x004c4750 −
0x004721b6`) and, only then, rewrites the rel32 to its trampoline with the
same protect/flush/rollback discipline as the object-trace patch. The
trampoline (`pushfl; pushal; call x3m_scene_end_signal; popal; popfl;
jmp *original`) leaves every general register, the flags and the return
address (`0x004721b6`) as the frame routine expects; the callee is
`void(void)` with no stack cleanup. The signal wraps the listener in the full
CPU boundary of `cpu_state.h` (FNSAVE/FRSTOR, MXCSR, last error; once per
frame): the listener runs the resolve, telemetry and the log formatter,
which execute x87 code (int64-to-double conversions, the CRT's float
formatting), so the light MXCSR-only contract of the setter hooks does not
apply here (review 21). The listener,
`capture.cpp::scene_end_signal`, takes the capture mutex and calls
`MotionOutput::scene_end_hook` on every hooked device. In the selector's
Scene phase that is the frame's scene end: the lazy bindings flush, the cut
verdict is finished, routing and jitter stop for the frame (`scene_bound`
refuses later draws: compositing and HUD draws are never routed or jittered)
and, with TAA on, the resolve runs on the bound RT0 (which must be the
latched main target, else skip reason 10 `Target`) while the depth surface
is still bound; RT2 holds the current depth, so nothing reads D24X8. The
bloom copy that `0x004c4750` may then issue (glow on) copies the resolved
image and its `StretchRect` path skips the resolve for the frame; with glow
off no copy follows and the frame is resolved all the same, which is the
failure mode the assessment recorded (the resolve point was a video
option). A signal outside the Scene phase (menu, rejected or environment-map
frame) or a second signal ends nothing and the copy path stays the fallback.
The bytes are restored when the last device is released (`scene_hook_shutdown`
line) and re-installed if a device is created afterwards; a mismatch at any
step refuses the install with a status (`executable_mismatch`,
`callsite_mismatch`, `target_mismatch`, `protect_failed`, `patch_rolled_back`).
Per frame the route logs `scene_end_source=none|hook|stretchrect` (where the
one resolve attempt ran) and `scene_end_check` (1 Agree: hook in Scene then
the bloom copy with no scene draw between; 2 HookOnly: glow off; 3
StretchOnly: no patch; 4 Disagree: a signal outside Scene, more than one
signal, scene draws between the hook and the copy, or a copy without a signal
while patched), with a `motion_output_scene_hook_disagreement` line for the
last case, on its own log budget (16 lines, separate from the failure
log: iteration 10's latch-only transition screen produced 16 consecutive
ones). Iteration 10 confirmed the boundary in gameplay on the X3 bottle
(214/214 resolves agree with the selector; the 24 disagreements are the
latch-only transition screen, nothing routed and nothing to resolve by
either boundary; iteration 9 run 2: 89/89), so since review 26 the hook is
the default resolve point: `tools/manage.py launch --motion-output` requests
it, `--scene-hook off` keeps the copy/selector boundary. The chain below
the hook is unchanged: the bloom-copy `StretchRect` resolves when the hook
is absent (`disabled`, `executable_mismatch`, `callsite_mismatch`, a failed
patch) and the selector alone decides the scene end when neither fires.
A state-block `Apply` or `EndStateBlock` resynchronizes the shadow
(`sb_resyncs` on the frame line attributes those resyncs, review 26): the
shadow's only other invalidations are `Reset` and a failed restoration of
the route's own state (`restore_failures`), and both were zero on the
transition screen, so its one `rs_resyncs` per frame is an application
state block, which the field now confirms directly.

**Pass field.** `RigidDrawKey::pass` (`MotionPass` in
`src/renderer/motion_history.h`: 1 main scene, 2 depth-only, 3 shadow, 4
environment map reserved; 0 unknown never keys) is hashed and compared with
the rest of the key and set to `PassMainScene` by gate 2, which established
the Scene phase on the latched main pair; the analyzer's K1 carries the same
constant (`render_pass`). Matching on the existing fixtures is unchanged
([motion-history-key.md](../reverse-engineering/motion-history-key.md#pass-field-2026-09-12)).

### Cut detector (temporal step 1)

For every matched draw the route records the screen displacement of the
projected object origin (`(c24.w/c27.w, c25.w/c27.w)` of the current versus
the previous unjittered rows, scaled to pixels) into a vector reserved once
at attach (4,096 samples, never grown per draw). When the selector leaves the
scene phase (the depth unbind, before the copy where the resolve runs), or
before Present for a frame that never leaves it, the frame's
median displacement (`nth_element`) and the fraction of keyed routed draws
whose key the previous frame lacked (gate 6 among gates 0 and 6) are
computed; `cut` is set when the median exceeds the bound scaled by
width/1280 or the fraction exceeds its bound. The values are in the frame
counters, on the `motion_output_frame` line and, in capture frames, on the
`motion_output_cut` line. The resolve rejects history for a frame whose
verdict is set (step 3).

### Pair keying

Eligibility is keyed by the exact **pair**: gate 3 passes only when the bound
VS and PS fingerprints appear together in one table row
(`material_motion_pair_reviewed`), never on a VS alias alone. Variants,
however, are created **per original program**, one VS variant per VS object
and one PS variant per PS object, at creation time, because the game creates
the two stages separately and a VS such as `53a0a641107ed76c` or
`4944d81dfe531b37` serves four reviewed pairs each. This is correct only if
every row sharing a VS uses the same VS-side splice (output register,
TEXCOORD index, offsets, constant base), so that the one variant links with
each row's PS variant; the same holds for a PS shared by rows. In the
169-row table 23 vertex programs and 61 pixel programs each serve several
rows (the four `53a0…` rows all use o6/TEXCOORD4, the four `4944…` rows all
use o7/TEXCOORD5). Rather than rely on that
incidentally, `motion_output_profiles.h` proves it with a `static_assert`
over the generated table (`motion_output_profiles_consistent`), so a
regenerated table that broke the agreement would fail to compile instead of
mislinking; the registry code in `motion_output.cpp` then needs no per-pair
variant map, no extra device objects and no per-draw work beyond the existing
row lookup. If a future table needs different VS registers for different
pairs of one VS, the scheme to adopt is a per-pair VS variant keyed by
`(vs, ps)` in the registry; the static_assert marks exactly that point.

The route derives two more table facts at compile time: the constant
shadow captures every distinct clip-row window the rows name (today c24–27
for the point-light programs and c0–3 for the light-free variants, at most
`motion_matrix_windows_max` = 4 windows), each with its own `rows_known`
flag, and gate 4 reads the window and the light-loop bound of the VS row
actually bound (`shadow_.vs_row`, recorded at registration through
`material_motion_vertex_row`; rows sharing a VS agree on these fields by the
static_assert above). A second `static_assert` in `motion_output.cpp`
(`rows_match_shadow`) requires every row's window to be one the shadow holds
and every bounded row's clip rows to lie above the c0–23 light block the
`i0.x` in [0, 8] bound protects, so a regenerated table naming more windows
or another bound would fail to build rather than route draws whose rows the
shadow never captured. `resync_shadow` re-reads every window after a state
block or Reset.

### Hooked vtable slots

Installed only when the switch is on and the device passed the capability gate
at attach, in addition to the existing hooks; a device the route refuses keeps
the plain table and pays nothing per setter call. Every index is asserted
against the SDK layout in `verification/probe/abi_check.cpp`.

| Slot | Method | Purpose |
| --- | --- | --- |
| 47 | SetViewport | viewport shadow for the selector's full-target check |
| 59 / 60 / 61 | CreateStateBlock / BeginStateBlock / EndStateBlock | state blocks get a private vtable (slots 2 Release, 5 Apply); recording suspends the shadow, EndStateBlock and Apply resynchronize it from the public getters |
| 87 / 89 | SetVertexDeclaration / SetFVF | declaration identity (element hash) and POSITION0 layout |
| 92 / 107 | SetVertexShader / SetPixelShader | bound program identity and registered variant |
| 94 / 96 / 109 | SetVertexShaderConstantF / I, SetPixelShaderConstantF | rows c24–27, i0, application writes to c252–255 and PS c216–217 |
| 100 / 104 | SetStreamSource / SetIndices | stream-0 and index allocation identities |
| 30 / 31 / 34 / 35 / 39 / 115 / 116 | UpdateSurface, UpdateTexture, StretchRect, ColorFill, SetDepthStencilSurface, patches | complete selector event stream (previously only with scene-depth capture); StretchRect also runs the resolve before the application's bloom copy (step 3) |
| 41 / 42 | BeginScene / EndScene | scene state for the resolve's caller contract (step 3) |
| 118 | CreateQuery | query objects get a private vtable (slots 2 Release, 6 Issue) so the route counts queries between BEGIN and END; the resolve never draws while one is open (step 3) |
| 57 | SetRenderState | render-state shadow (`X3M_STATE_SHADOW`, default on; light boundary) and, in lazy RT mode, the flush of a held write mask before the application's write (installed in lazy mode with the shadow off too) |
| 58 | GetRenderState | lazy RT mode only: the application's read of a write mask restores the bindings first |
| 38 / 32 | GetRenderTarget / GetRenderTargetData | lazy RT mode and `X3M_HDR`: the application's target getter answers with its logical RT0 while the FP16 target is bound (the logical-binding shim), a read of the main target's contents receives the pending FP16 content first |
| 65 / 69 | SetTexture / SetSamplerState | `X3M_TAA_MIP_BIAS` only (light boundary): the sampler shadow of the mip LOD bias — texture binding and level count (`GetLevelCount` once per pointer change, inside the native section), `MIPFILTER`, and the application's own `MIPMAPLODBIAS` writes (counted, logged, the restore value); see [temporal-integration.md](temporal-integration.md#mip-lod-bias-for-routed-material-draws-2026-09-12) |

Native slots the route calls itself (never the hooked table): 1, 2, 6, 8, 9,
23, 28, 32, 34, 36, 37, 38, 39, 40, 41, 42, 47, 48, 57, 58, 75, 76, 83, 87, 88,
89, 90, 91, 92, 93, 94, 95, 97, 100, 101, 103, 105, 106, 107, 108, 109, 110; the
release hook's reference-count probe and the route's pass accounting use
native 1 and 2 (AddRef/Release). The pass adds 7, 59, 65, 69, 102 and 104
through the same table; the HDR pass adds 64, 66, 67 and 68.

The hot setter hooks (shaders, the three constant setters, viewport, render state, texture and sampler state) use
`LightCallBoundary` (MXCSR and last error only) with a plain lock: their own
code on both sides of the native call is integer/SSE memory work, so the
legacy caller's x87 state is untouched by construction, and
`verification/probe/check_no_x87.py` proves the absence of x87 opcodes on
every function those hooks reach in the built DLL. The stream/indices hooks
(resource private-data identity, which logs on first sight), the declaration
and FVF hooks (foreign getters) and the state block hooks keep
`CpuCallBoundary`.

### Scene recognition and per-draw cost

The route owns its own `SceneBoundarySelector`, fed from the shadow rather than
from `SceneCapture`, because the capture adapter runs only in requested frames
with the ownership wrapper and hashes shader bytecode per draw. Per ordinary
draw the route queries two render states (Z enable/write) while the
selector is in Background or Scene; a candidate draw (gates 1–3 passed) adds
four render-state queries, one stream-frequency getter, the object observers
and one history lookup. With the render-state shadow on (default) every one
of those state queries is answered from the shadow (zero `GetRenderState`
per draw after the first fill); with it off each is a native getter, the
previous behaviour. The object observers read engine memory through
`engine_memory` (validated direct reads: one `VirtualQuery` per distinct
region per frame, re-validated at `begin_frame`, instead of the ~21
`ReadProcessMemory` syscalls per routed draw that were 92–98 % of the gate in
[route-cost-run1.md](../verification/route-cost-run1.md); fixture cost of the
four route-path reads 1.31 µs direct against 3.36 µs over `ReadProcessMemory`,
and the reader unit executes no XMM/x87 instruction); the four engine
matrices are read only on capture frames (`object_trace::current(out,
capture_)`), and the per-draw QPC stamps need `X3M_TELEMETRY_DRAW=1` on top
of `X3M_TELEMETRY=1`. No getter fetches shader bytecode or shader objects on
the routed path, no heap allocation occurs per draw (the history reserves its
tables once), and no state block is created.

### Sentinel fill and restoration

The latching Clear schedules the fill; it runs inside the next draw hook so it
is always within the application's scene on Windows and Wine. The fill draws
one XYZRHW strip with an embedded ps_2_0 (`def c0, 0,0,0,-1; mov oC0, c0`)
into the motion target as RT0 with RT1–3 and depth unbound. Saved before and
restored after, in this order: RT0, RT1–3, depth, viewport, scissor, FVF or
declaration (whichever the application used), vertex shader, pixel shader,
stream 0 (DrawPrimitiveUP clears it), and the render states ZENABLE,
ZWRITEENABLE, ALPHATESTENABLE, ALPHABLENDENABLE, CULLMODE, FILLMODE,
COLORWRITEENABLE, SCISSORTESTENABLE, STENCILENABLE, FOGENABLE, SRGBWRITEENABLE,
CLIPPLANEENABLE. Nothing changes if the initial state query fails.

A routed draw sets, and `after_draw` restores in reverse: COLORWRITEENABLE2
and RT2 (depth-capable rows on a depth-producing device), COLORWRITEENABLE1,
RT1, pixel shader, vertex shader, and the reserved constant ranges only when
this draw set them and the shadow has seen the application write them (state
block Apply marks them written conservatively); a jittered draw, routed or
not, then gets its clip rows written back from the shadow. The fixture
compares every one of these (RT0–2, depth, viewport, scissor, declaration,
shaders, stream, `c24–27`, `c252–255`, the render states including
COLORWRITEENABLE1/2) before and after each fill and each draw.

### Failure behavior

- Capability gate or self-test failure: route disabled for the device, one
  `motion_output_device` line with the reason; nothing else changes.
- Motion or depth target allocation failure: no fill or routing until the
  next Reset (a device that produces depth needs both targets).
- Partial application of a routed draw: already-set state is undone and the
  original pair draws; counted as `apply_failures`.
- Restoration failure (including the jitter row write-back,
  `what=jitter_rows`): counted, logged at most 16 times per device, the
  frame's history is still committed only if the fill succeeded.
- Reset: target released before the native call, then the pass's default-pool
  objects; history/selector invalidated, shadow resynchronized after success;
  variants and the resolve shader survive.
- Resolve failure (`motion_output_taa_failed`): the main target is untouched,
  the history is invalid, the application's copy proceeds with the raw frame.
- Final device Release: owned variants and target each hold a device reference,
  so the release hook probes the count and drops them first when only the
  caller's reference remains, preserving the application's zero return.
- HDR write-back failure (`hdr_unwind=<reason>`): the ladder (shader copy,
  `StretchRect`, binding restore) leaves the main target bound; the redirect
  is blocked until the recovery self test passes at a later latch or a Reset.
  A failed latching Clear rebinds without a write-back (`end=clear_failed`);
  a Reset while redirected binds the main surface back before dropping the
  FP16 target.

### Not covered

Gameplay captures with RT2, jitter or the resolve (the user-managed TAA run
is described in [motion-output verification](../verification/motion-output.md)),
temporal image quality, the SM1/SM2/bloom programs outside the table (their
draws are neither routed nor jittered; their pixels resolve current-only
through the RT2 sentinel), instanced or user-memory draws,
MSAA targets, Direct3D9Ex, native Windows
execution (cross-compiled only), and the measured cost of the setter hooks in
the game (each still takes the capture mutex and the admission entry; the
CPU-state boundary is the light one described above). Shader registry entries
are keyed by object address and replaced on reuse, never removed: a variant
whose original the game destroyed stays alive (one device reference each)
until that address is reused or the device is released.

## Engine constant-upload facts that the route depends on

The [constant upload disassembly](../reverse-engineering/constant-uploads.md)
shows that every shader/constant setter the game issues comes from its two
`ID3DXEffectStateManager` implementations inside `BeginPass`; `CommitChanges`
is never called, and no game code writes VS constants at or above c216 or PS
constants above c23. Two consequences are load-bearing:

- The pure-device state manager memoizes the last VS/PS pointer it forwarded.
  The route must therefore restore the application's shader pointers after
  every routed draw, or the next pass silently keeps rendering with the variant.
- The reserved constant ranges are never written by the game, so the
  conditional restore of c252–255 and c216–217 is a safety net, not a per-draw
  cost. The integer register i0 is written every draw and stays shadowed.

The setter hooks are on the per-draw path but shallow: at most about five VS
float writes, one integer write and a few PS writes per material pass.
