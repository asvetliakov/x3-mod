# Temporal integration on the live motion route

Design record, 2026-09-12. This defines how TAA jitter, current depth and the
temporal resolve connect to the [live motion route](live-motion-route.md). It
follows the read-only study of the existing resolve pass against what the game
actually provides; the numbers and file references below come from that study
and from the linked verification documents. Steps 1, 2 and 3 of the order of
work below are implemented (sections at the end); the gameplay comparison of
step 3 is user-managed and pending.

## Placement

The game renders the scene into an A8R8G8B8 main target with D24X8 depth, then
copies the main color to a bloom source with `StretchRect`, composes bloom and
draws overlays. The selector recognizes that copy as `AwaitCopy`
(`src/renderer/scene_boundary.h`). Depth is unbound just before it but its
content survives until the frame's final depth-only Clear.

The resolve runs at that copy point, before the application's `StretchRect`:

```text
main RT (8-bit)  --StretchRect-->  FP16 scratch (current color)
motion RT1, depth RT2, history      --resolve draw-->  FP16 history[next]
history[next]    --StretchRect-->  main RT (in place, 8-bit)
application StretchRect / bloom / overlays proceed unchanged
```

The resolve output cannot be bound as its own input, and the main target is not
known to be a texture level, so the scratch copy is mandatory. Copying back to
8-bit loses nothing the game had; when the FP16 scene path exists the main
target itself becomes FP16 and the copies disappear.

## Inputs the route will produce

| Input | Source | Change from today |
| --- | --- | --- |
| Current color | `StretchRect` of the main target into an owned A16B16G16R16F texture | `TemporalPass` s0 stays FP16; the copy is new |
| Current depth | **RT2, R32F, written by the variants as oC2** from the current clip z/w exported by the VS | new third output in every transformed row; sentinel fill like RT1 |
| Motion | RT1 RGBA32F, unchanged ABI | none |
| Reactive mask | derived from the RT2 sentinel: pixels no routed opaque draw wrote are treated as reactive (history rejected) | resolve samples RT2 instead of a separate mask; `ReactivePolicy::RequiredMask` is satisfied by RT2 |
| Jitter | per-draw explicit rows, see below | new |

Current depth from oC2 covers the pixels of the 16 transformed pairs, which are
97.6% of scene draws but not all scene pixels: background, particles, SM1/SM2
and depth-disabled effects keep the sentinel and therefore fall back to the
current frame in the resolve. That is acceptable for the first visible TAA:
the background has no per-object motion and particles have no history anyway.
The RESZ snapshot plus decoder remains available if complete depth is needed
later; it costs about 26 fetches per pixel.

The VS has enough headroom: every one of the eight vertex programs behind the
16 rows has at least two free output registers after the motion interpolator.
The binding constraint is the ten pixel-shader inputs of SM3; the worst program
declares nine outputs, so motion plus depth reaches exactly ten. The depth
export needs only z and w, so it can share one float4 with a future payload.
The mixed-format MRT gate and self test extend to three formats
(A8R8G8B8, A32B32G32R32F, R32F).

## Jitter

Rows go to the device as `o0.x = dot(r, c24)`, `.y = dot(r, c25)`,
`.w = dot(r, c27)`, so a clip-space offset is `c24 += jx·c27`,
`c25 += jy·c27` with `jx, jy` in NDC units derived from the pixel jitter and
the viewport size. The game's effect state manager caches the rows it last
uploaded and skips identical uploads, so modifying rows inside the upload hook
would leave the previous frame's jitter on every static object (29% of matched
draws have bit-identical rows). Jitter is therefore applied **per draw** in the
route's before-draw step with an explicit constant write of the four rows, and
the original rows are restored after the draw. The shadow keeps the unjittered
rows, so the history stays jitter-free. **`c216.zw` is uploaded as zero**, not
as the prior jitter: `rigid_motion_ps.hlsl` subtracts `c216.zw` from the
interpolated previous projection, which is correct only when the previous rows
it interpolates were jittered. With unjittered history rows the fragment
already produces the previous *unjittered* UV the RGBA32F ABI specifies, and
the resolve adds the previous raster jitter exactly once when it samples
history (verified by the step-2 fixture; passing the actual prior jitter
would make the motion path sample one jitter offset away from the camera
path). The route keeps both the current and the previous jitter in its frame
diagnostics for the resolve's `FrameInputs`.

Jittered draws: every scene-phase draw whose vertex program is in the row-dot
registry with a known matrix register (the c24 material family covers almost
everything observed; the small c0 family and the particle program with
`c4/c5 += j·c7` follow). Not jittered: direct-clip bloom quads, post-bloom
overlays and GUI, and our own fill draws. Draws with an unknown program are not
jittered and are excluded from history by the sentinel.

## History, Reset and cuts

`TemporalPass` owns two FP16 color textures, two R32F depth textures and the
FP16 scratch; its `before_reset` releases those default-pool objects and keeps
the compiled shaders, and `after_reset(result)` re-enables lazy re-creation on
the next run (step 2 below). The route folds its resource generation into the
history epoch. Ordering is the same as for the motion target: release before
the wrapper's Reset, recreate lazily.

History is invalidated on camera-serial change, load or registry epoch change,
dimension change, failed Present and selector rejection. Those signals already
exist in the route, but the [iteration-6 run](../verification/iteration-06.md)
showed they are insufficient: the load and registry epochs did not advance
across several sector changes and a ship destruction, and the camera serial
changed only once in the session. A cut detector based on the route's own
data is therefore required: at the end of the scene phase, compute the median
screen displacement of the matched draws' projected origins (current versus
previous rows, the same quantity the cross-check tool computes) and the
fraction of routed draws whose key was absent in the previous frame. A median
above a configurable bound (a few tens of pixels at 1280×768, scaled with the
viewport) or a missing-key fraction above a bound marks the frame as a cut,
and the resolve runs current-only for it. The bounds are tuned from the
iteration-6 distributions (median displacement 0.001–8.4 px in ordinary flight,
worst missing-key fraction 42 of 10,517 routes). A view-inverse shadow of
`c34–36` remains a possible refinement but is not required for this.

## Cost and memory

At 5120×1440 the added default-pool memory is about 118 MB for RGBA32F motion,
29 MB for R32F depth, and roughly 180 MB for the FP16 scratch and history pairs.
A compact motion encoding is a later change. Per frame the new GPU work is two
copies, the resolve draw and the fills; none of these has a measured cost yet
and the material-motion fixture numbers exclude them.

## Order of work

1. Add the R32F current-depth output (RT2) and the per-draw jitter to the live
   route, with the structural and GPU fixtures extended, and make the readback
   analyzer use the depth image so the gameplay capture can be checked against
   real depth.
2. Adapt `TemporalPass`: FP16 scratch copy of 8-bit color, direct R32F depth
   (skip the decoder), reactive derived from the RT2 sentinel, jitter inputs.
3. Wire the resolve at the copy boundary behind an off-by-default switch with
   copy-back, then a user-managed run comparing still, turning and flying
   captures with the route off and on.

## Step 1 implementation

Implemented 2026-09-12 in `src/renderer/material_motion.{h,cpp}`,
`motion_output_profiles.h` (four new row fields, generated by
`inspect_motion_output_profiles.py --emit-header`), `src/temporal/current_depth_ps.hlsl`
(embedded as `current_depth_pixel_program_inc.h`), `src/proxy/motion_output.{h,cpp}`
and `capture.cpp`; the shader contract gained the delimited section
"Current depth (step 1)" in `src/temporal/README.md`. Evidence:
[material motion](../verification/material-motion.md) (GPU depth output),
[motion output](../verification/motion-output.md) (route, jitter, cut data)
and [motion readback](../verification/motion-readback.md) (analyzer).

**Current depth (RT2, R32F).** Every row names a second free VS output
register, a second free TEXCOORD index (chosen per VS/PS sharing component so
one variant per program still links with every pair) and a second free PS
input. The vertex variant adds one declaration and two DP4s of the position
temporary against `c<matrix+2>` and `c<matrix+3>` (current clip z to `.xz`,
w to `.yw`); the pixel variant appends the relocated depth fragment
(`rcp`, `mul`: `oC2 = z/w`) after the motion fragment. All 169 archive rows
carry the depth output (the worst pixel program declares nine inputs, so
motion plus depth reaches exactly the ten of SM3); the table format supports
`depth_output=false` motion-only rows (register 255 = none), and a device
without a third simultaneous target or R32F render-target support gets the
motion-only variants (`current_depth=false`). The structural revalidation
covers the new registers and index like the motion ones. The route owns the
R32F target at main dimensions, fills it with -1 in the same sentinel draw
as RT1, binds it as RT2 with `COLORWRITEENABLE2=15` for depth-capable rows,
restores both, releases it before Reset and at device release, gates on
`NumSimultaneousRTs >= 3` plus an R32F format check and a three-format MRT
self test, and reads it back in capture frames as
`depth_<device>_<frame>.r32f`. The GPU fixture proves `oC2` equals the
rasterized device depth (analytic z/w at 9 samples per configuration, the
covered/uncovered pattern, and the authored replay drawn with `ZFUNC EQUAL`
over the original's D24 depth, all within 2e-6).

**Jitter.** `X3M_MOTION_JITTER=1` (default off; `X3M_MOTION_JITTER_SAMPLES`,
default 8, 2..64) enables a centred Halton(2,3) sequence in raster pixels,
advanced once per frame at the latching Clear. Every scene-phase draw whose
bound VS has a table row is jittered, routed or not: the route writes
`rows[0] += jx_ndc·rows[3]`, `rows[1] += jy_ndc·rows[3]` with
`jx_ndc = 2·jx_px/width`, `jy_ndc = -2·jy_px/height` at the row's matrix
register before the native draw and writes the application's rows back
bit-exactly from the shadow afterwards; the shadow and the history hold the
unjittered rows. The sign convention (+X right, +Y down in pixels) is proven
by the fixture's coverage oracle: pixel `(x, y)` shows what the unjittered
geometry has at `(x - jx, y - jy)` for every sample of the sequence.
`c216.zw` is uploaded as zero (see "Jitter" above); the motion readback with
jitter on matches the oracle built from unjittered previous rows.

**Cut detector (data only).** When the selector leaves the scene phase (or
before Present for a frame that never leaves it) the route computes the
median screen displacement of the matched draws' projected origins
(`c24.w/c27.w`, current versus previous rows, in pixels) and the fraction of
keyed routed draws whose key the previous frame lacked; `cut` is set when the
median exceeds `X3M_MOTION_CUT_MEDIAN_PX` (48 at 1280 px, scaled by
width/1280) or the fraction exceeds `X3M_MOTION_CUT_MISSING` (0.25). The
verdict and both statistics are in the per-frame summary and the
`motion_output_cut` capture line; nothing consumes them yet.

**Results (2026-09-12, fresh `build/` and `build-ownership/`).** Structure:
169 rows transformed, 169 with depth output, 0 motion-only, 1,691 check
groups, 1,551,936 mutations, 4,394 row and 5,417 program perturbations
(2 absent sites), release and ASan/UBSan. GPU material motion: Argon 1,428
checks / 82 configurations, rows 169 of 169 / 21,103 checks, RT2 exact
against the ZFUNC EQUAL replay (7,755,681 pixels) and 2.6e-6 maximum
analytic z/w error. Motion output: 18 runs (four environments plus jitter),
seam-on 44,284 motion / 27,170 RT2 pixels per environment, jitter coverage
48,957 pixels, colour identical off/on and across environments, differing in
all 12 frames with jitter on. Temporal pass 154 numerical / 158 state
comparisons; ownership integration 26 cases and the fallback; scene capture
36 scenarios / 4,908 checks; `check_no_x87` 125 reachable functions, 0
violations; 441 analysis unit tests. Details in the verification documents
linked above.

**Analyzer.** `analyze_motion_readback.py` loads the depth images when
present: RT2 integrity (finite, in [0,1] or the -1 sentinel, sentinel and
written fractions, every motion-valid pixel written) and the previous-depth
comparison of frame N+1's B channel against frame N's image at the previous
UV plus frame N's raster jitter, nearest by default or bilinear over the
non-sentinel taps, with the error distribution and histogram.

## Step 2 implementation

Implemented 2026-09-12 in `src/renderer/temporal_pass.{h,cpp}`,
`src/temporal/resolve.h` and `resolve.hlsl`; the shader contract gained the
delimited section "Route inputs (step 2)" in `src/temporal/README.md`, and
the fixture evidence is in
[temporal resolve verification](../verification/temporal-resolve.md).

Interface changes:

- `FrameInputs::color_surface` (A8R8G8B8/X8R8G8B8 default-pool surface,
  copied by `StretchRect` into an owned FP16 scratch) alongside the FP16
  `color` texture; exactly one is set.
- `FrameInputs::current_depth` (R32F, -1 sentinel) alongside the D24X8
  `depth_snapshot`; exactly one is set. The R32F path copies the texture into
  the owned depth history with `StretchRect` and skips the decoder draw.
- `ReactivePolicy::DerivedFromDepthSentinel`; `prepare` gained
  `depth_sentinel_reactive`, which sets the previously reserved `c7.w`.
- `FrameInputs::cut`, the route's cut-detector verdict.
- `Output::color_surface`, level 0 of the resolved texture for the copy-back.
- `Diagnostics::reset_pending`; `before_reset` keeps shaders,
  `after_reset(HRESULT)` is new, `shutdown` is the full teardown.

Findings that change the route wiring in step 3:

- **Gamma.** The 8-bit copy is a plain UNORM-to-FP16 conversion; on the
  CrossOver Preview backend it truncates toward zero (within one FP16 ulp of
  `v/255`, never above). No sampler or target sRGB state is set by the pass
  and the copy-back restores the original bytes exactly. The route must not
  add sRGB conversions around the sequence.
- **Previous jitter on the motion path.** The resolve adds the previous
  jitter once to the producer's RG, which the ABI defines as the previous
  *unjittered* UV. `rigid_motion_ps.hlsl` subtracts `c216.zw` from its
  interpolated previous projection, which is only correct if the previous rows
  were jittered. The shadow keeps unjittered rows, so the route must upload
  **zero** for `c216.zw` (or shadow jittered rows); passing the actual prior
  jitter would make the motion path sample one jitter offset away from the
  camera path. This is a wiring rule for step 3, not a shader change.
- **Sentinel semantics.** A sentinel history tap is dropped individually and
  the footprint renormalizes; it does not reject the footprint the way a
  reactive mask tap does. Blended effects over routed opaque geometry stay
  undetected (RT2 keeps the opaque depth under them); only the neighborhood
  clip bounds them.

Per-frame GPU work on the route path: one converting 8-bit to FP16
`StretchRect`, one R32F `StretchRect`, the resolve draw, and the caller's
copy-back; the decoder's 26 fetches per pixel are gone. The scratch is
allocated only when the surface path is used. The per-frame `D3DSBT_ALL`
state block creation noted here was replaced by one cached block in step 3.

## Step 3 implementation

Implemented 2026-09-12 in `src/proxy/motion_output.{h,cpp}` (`before_stretch`,
`resolve`, `taa_call`, scene and query tracking), `src/proxy/capture.cpp`
(`X3M_TAA`, `X3M_TAA_DEBUG`, the StretchRect hook calling the route before the
application's copy, BeginScene/EndScene and CreateQuery hooks with a
query-object wrapper), `src/renderer/temporal_pass.{h,cpp}` (calls through
the native vtable slots, one cached state block, optional decoder),
`src/renderer/temporal_resolve_program{,_inc}.h` (embedded resolve, generated
by `tools/shaders/generate_rigid_motion_pixel.py`), `src/temporal/resolve.hlsl`
(output alpha is the current alpha), `tools/manage.py` (`--taa`,
`--taa-debug`), `tools/analysis/analyze_motion_readback.py` (`taa_image`) and
the motion-output fixture/runner. Evidence:
[motion output](../verification/motion-output.md) (TAA environments and
bench), [temporal resolve](../verification/temporal-resolve.md) (pass suite
unchanged in count). The wiring details are in
[live motion route](live-motion-route.md#temporal-resolve-at-the-bloom-copy-temporal-step-3).

**Where the resolve runs.** In the StretchRect hook, before the application's
call, when the selector is in AwaitCopy and a probe copy of it accepts the
pending event (source = latched main target, destination = same-sized color
texture, full rects): this is the Scene-to-AwaitCopy-to-AwaitBloomTarget
point of the [placement](#placement), after the depth unbind (RT2 holds the
current depth, so the D24X8 surface is not needed) and after the cut verdict
(computed at the depth unbind). The route builds `FrameInputs` exactly as
listed in the task (main surface, RT2, RT1, main dimensions, epoch = resource
generation, identity matrix, current/previous jitter in pixels, cut,
`PerPixel` + `DerivedFromDepthSentinel`), runs the pass and copies
`Output::color_surface` back into the main target with a point-filtered
full-rect `StretchRect` through the native slot; the application's copy then
proceeds on the resolved image. Failure leaves the main target untouched and
invalidates the history. `X3M_TAA=1` requires `X3M_MOTION_OUTPUT=1`, implies
`X3M_MOTION_JITTER=1`, and the device must pass the route's gate with RT2,
FP16 render targets, sampled FP16/RGBA32F/R32F textures and `StretchRect`
format conversion between A8R8G8B8/X8R8G8B8 and FP16
(`CheckDeviceFormatConversion`, `taa_reason=format_conversion`; accepted
unconditionally on Wine). The launcher's `--taa` also requires
`--object-trace --object-lifetime`: without object history every routed draw
carries the sentinel, the resolve is current-only and only the jitter reaches
the screen.

**Findings while wiring.**

- The resolve wrote alpha one; the main target's alpha byte (the fixture's
  material draws write non-opaque alpha) would have been overwritten on every
  frame. The shader now returns the current color's alpha in every path
  (history alpha is never blended) and the embedded bytecode was regenerated.
- The pass must call the device through the route's native slots: through the
  hooked vtable its StretchRect and draw would have fed the selector (an FP16
  destination is not a bloom copy; a draw in AwaitCopy is a pattern failure)
  and the shadow. `TemporalPass::initialize` takes the original table; the
  detached fixture passes none and keeps its vtable-swapping fault injection.
- Device references through the ownership wrapper: the pass's textures and
  their level surfaces are two children each, the state block and the shader
  one. The route counts them by probing the device count around every pass
  call instead of a per-object model, and reports zero held references while
  such a call runs: the wrapper's child releases re-enter the device Release
  hook, and with the count still including the objects being released the
  hook's final-release probe matched by coincidence during `before_reset`
  (found by the wrapper TAA fixture run, fixed).
- A rejection of the selector after the copy (the fixture rebinds its depth
  without a bloom sequence; in the game a changed bloom or overlay sequence)
  must not drop the history; only a frame that did not resolve does.
- The resolve quad is pre-transformed with no vertex shader, so stage 0's
  fixed-function `D3DTSS_TEXCOORDINDEX` and `D3DTSS_TEXTURETRANSFORMFLAGS`
  shape the `TEXCOORD0` it reads; the pass's `normalize` resets both
  (restored by the state block) after the pass fixture's hostile texture
  transform changed the resolve output on the Preview backend
  ([review 16](../verification/review-16.md)).
- The caller contract of the pass needs positive knowledge of no open
  application query: the route wraps query objects and counts BEGIN/END, so
  EVENT queries (the fixture's timing) never block the resolve and an
  occlusion query in flight makes the frame current-only.

**Measured cost (fixture bench, CrossOver Preview backend, CPU-inclusive).**
Fixture bench (`bench WxH`, production DLL, route and jitter on, 20 timed
frames, EVENT-drained before and after the boundary `StretchRect`, QPC):

| Size | Boundary, resolve off (median / min) | Boundary, resolve on (median / min) | Resolve + copies (median of on minus median of off) |
| --- | ---: | ---: | ---: |
| 1280×768 | 0.330 / 0.205 ms | 1.004 / 0.900 ms | **0.67 ms** |
| 5120×1440 | 0.711 / 0.640 ms | 2.754 / 2.625 ms | **2.04 ms** |

"On minus off" is the FP16 copy of the main target, the R32F depth copy,
the resolve draw, the state block capture/apply and the copy-back, plus the
hook's own CPU work; it is CPU-inclusive wall-clock time on the CrossOver
Preview backend with the GPU idle at the start, not a GPU timestamp, and the
synthetic frame has two scene draws, so the numbers bound the added cost of
the boundary rather than a game frame. Nothing was added per draw.

**Results (2026-09-12, fresh `build/` and `build-ownership/`).** Motion output: 26 runs (18 previous, four TAA, four bench), all passing:
production TAA 69 checks / 51 restoration comparisons, seam TAA 150 / 51,
plain and through the wrapper; the seam's main target equals the reference
resolve byte for byte in all 12 frames and its `X3M_TAA_DEBUG` FP16 files
equal the reference FP16 output in frames 1-8; history in seam frames 1, 2,
4, 7, 10, 11 (374 pixels changed by history per run, in frames 1, 4, 7, 10,
11; frame 2 is sentinel-only), none in frames 0, 3, 5, 6, 8 (cuts) and 9
(after Reset); the production presented image is bit-identical to the
jitter-only run in every frame and the analyzer's `taa_image` differing
fraction is 0 there (maximum RGB difference 4.8e-4, one FP16 ulp at 8-bit
values) and 0.0044 / 0.0229 / 0.0046 in the seam's captured history frames
1 / 4 / 7 (maximum difference 0.448 at a flat/material edge); zero final
device and factory references in every run with the pass's objects counted;
`taa_references=1` after Reset. Ownership integration and its fallback,
scene capture (36 scenarios, 4,908 checks), the temporal pass suite (154
numerical / 158 state comparisons, unchanged by the native-slot and
state-block changes), `check_no_x87` (6 light hooks, 125 reachable
functions, 0 violations) and 442 analysis unit tests pass on the rebuilt
`build/` and `build-ownership/`.
