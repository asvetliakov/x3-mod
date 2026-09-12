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

The resolve runs at that copy point, before the application's `StretchRect`
— or, since 2026-09-12 with the engine scene-end hook (`X3M_SCENE_HOOK`,
on by default with the route since review 26; `0` turns it off), at the
engine's scene-end callsite just before the compositor is called, which
precedes the same copy and exists whether or not glow is enabled (see
"Resolve placement with the engine hook" under step 3). The copy point stays
the fallback whenever the patch is absent or refused (a differing executable
or differing bytes at the site fail closed):

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
already produces the previous *unjittered* UV of the content at the jittered
sample (a static object at pixel `p` reports `p - current jitter`), which is
what the RGBA32F ABI specifies. The resolve reads history at that UV **plus
the current jitter** ("pixel center minus velocity"), so a static scene reads
its own texel centers; it never applies the previous jitter, which
`FrameInputs::previous_jitter` still carries for ABI stability only. Passing
the actual prior jitter in `c216.zw` would shift the motion path's lookup by
that jitter and destabilize static geometry. The route keeps both the current
and the previous jitter in its frame diagnostics.

**Stationary-stability fix (2026-09-12).** Gameplay under `X3M_TAA=1` showed
stationary objects trembling and blurring. The resolve added the *previous*
raster jitter to the history lookup, which is right only for a raw
one-frame-old jittered rendering (the detached fixture's original setup); the
live history is the accumulated output on the unjittered grid, so every
lookup was off by the jitter difference and alternated with the Halton
sequence, and the fractional bilinear resampling blurred it. Both resolve
paths now add the current jitter instead (camera: removed before the inverse
projection, restored after; motion: added to the producer's RG). The
convention, the re-derived fixture cases, the new stationary scene
(zero interior change between phases, zero centroid drift, edges converging
to the jitter-sampled coverage) and the negative proof against the previous
shader are in [temporal-resolve.md](../verification/temporal-resolve.md),
"Stationary stability". The silhouette limitation that remained (edge
pixels against a different depth rejected their history whenever their
coverage flipped) is addressed by the resolve quality pass below.

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
all 12 frames with jitter on. Temporal pass 154 numerical / 158 state (174 / 162 since the 2026-09-12 stationary-stability cases)
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
- **Jitter on the motion path.** The ABI defines the producer's RG as the
  previous *unjittered* UV of the content at the jittered sample; the resolve
  adds the **current** jitter to it (since 2026-09-12; it added the previous
  jitter before, which destabilized static geometry once the history became
  the accumulated output, see "Jitter" above). `rigid_motion_ps.hlsl`
  subtracts `c216.zw` from its interpolated previous projection, which is
  only correct if the previous rows were jittered. The shadow keeps
  unjittered rows, so the route must upload **zero** for `c216.zw` (or shadow
  jittered rows); passing the actual prior jitter would shift the motion
  path's lookup by that jitter. This is a wiring rule for step 3.
- **Sentinel semantics.** A sentinel history tap is accepted as the
  background behind a silhouette (since the resolve quality pass; it was
  dropped individually before); it never rejects the footprint the way a
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
invalidates the history.

**Resolve placement with the engine hook (2026-09-12; the default since
review 26).** With the hook active (`X3M_SCENE_HOOK` unset or `1` while
`X3M_MOTION_OUTPUT=1`; `0` disables it) the primary resolve point is the
engine scene-end signal
(`src/proxy/scene_hook.cpp`: the patched `CALL 0x004c4750` at `0x004721b1`,
[camera-state-and-frame-routine.md](../reverse-engineering/camera-state-and-frame-routine.md#implemented-hook-scene-end--compositing-begin-2026-09-12)),
delivered to `MotionOutput::scene_end_hook` before the compositor runs and
therefore before its `GetRenderTarget(0)` / depth unbind / bloom copy
([compositor-and-glow.md](../reverse-engineering/compositor-and-glow.md)).
There the selector is still in the Scene phase and the depth surface is
still bound; the resolve reads and rewrites the bound RT0, which must be the
latched main target (skip reason 10 otherwise), with the same `FrameInputs`
as at the copy (RT2 is the current depth, so the bound D24X8 surface is not
needed). The frame's one resolve attempt is recorded with its source: the
`StretchRect` path then finds the frame attempted and does nothing, so a
glow-on frame resolves once, at the hook, and the bloom copy receives the
resolved image exactly as before; a glow-off frame — no compositor device
calls at all — resolves at the hook too, which removes the video-option
dependency. The copy path remains the fallback when the hook is off, refused
(wrong executable or bytes) or fired outside the Scene phase. The fixture's
hook script proves the two points equivalent: the frames both the patched
and the unpatched run resolve are bit-identical, and every hook resolve
equals the reference pass byte for byte
([motion-output.md](../verification/motion-output.md#engine-scene-end-hook-x3m_scene_hook)).
`X3M_TAA=1` requires `X3M_MOTION_OUTPUT=1`, implies
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
numerical / 158 state comparisons at the time; 174 / 162 after the 2026-09-12
jitter-convention fix and its stationary scene, unchanged by the native-slot and
state-block changes), `check_no_x87` (6 light hooks, 125 reachable
functions, 0 violations) and 442 analysis unit tests pass on the rebuilt
`build/` and `build-ownership/`.

## Resolve quality (2026-09-12)

Gameplay after the jitter-convention fix still showed trembling object edges
and shimmering thin geometry and emissive lines with the camera still. Causes
in the resolve: the absolute 1e-4 depth test against the history depth at the
reprojected position, which at a silhouette belongs to the other surface, so
an edge pixel lost its history on every phase that flipped its coverage;
sentinel current pixels always current-only; no velocity dilation; bilinear
history resampling. `src/temporal/resolve.hlsl` now implements the standard
TAA structure (details and the per-step reasoning in the
[algorithm section](../../src/temporal/README.md#algorithm-resolve-quality-pass-2026-09-12)):

- **Neighborhood clip as the primary gate**: history clamped per channel to
  mean +/- 1.25 sigma of the finite current 3x3 within its min/max box.
- **Depth as a one-sided disocclusion test only**: every contributing tap of
  the bilinear footprint must be at or behind the expected previous depth
  of the closest-depth pixel of the 3x3, `max(1e-4, 0.02 * depth)`
  tolerance, or the -1 sentinel; history clearly in front (an occluder that
  moved away) rejects. The comparisons use only `>=` and `<=`: on the
  verified backend `v == v` folds to true and `<` / `>` compile to negated
  forms a NaN passes (measured with a probe shader), so a corrupt history
  depth fails closed only in that form.
- **Closest-depth dilation over the 3x3** (velocity of the closest pixel
  applied to this one); the cross was tried first and lost half the
  coverage of silhouette corners.
- **Catmull-Rom history** (16 point taps under the point-sampler contract,
  one tap on the grid under a real branch; all fetches are `tex2Dlod`).
- **Sentinel policy** `c7.w`: 1 current-only (the route today), 2
  reprojection at the far plane through `clip_to_previous`
  (`FrameInputs::sentinel_camera`). The route must keep policy 1 until it
  uploads a real camera matrix and fills RT1's alpha with 0 (camera path)
  instead of -1 for unrouted pixels; with the identity matrix policy 2
  would accumulate a moving background in place.
- Weight stays `c5.z` = 0.9; 0.85-0.95 documented as the range.

Cost: 10 current color, 9 current depth, 1 motion, 4 history depth and 1 or
16 history color fetches per pixel (20 before), program 3,794 words (1,695).
The route's measured resolve cost was not re-benchmarked here
(`run_motion_output.py` is owned by another agent at the time of writing).

Evidence ([temporal-resolve.md](../verification/temporal-resolve.md),
"Resolve quality"): detached suite 78 / 78 with ten new checks per
generation and no previous value changed; pass suite 318 numerical / 164
state / 292 samples, negative controls intact. Against a far background or
under policy 2: a 1-px line converges to 0.995 of its width with the
integrated brightness stable to one FP16 ulp across phases, drift 0.0035 px,
per-row coverage within 0.06 of analytic; a silhouette's ring variance drops
178x with zero ghost behind the square moving 1 px/frame; the scrolling
sinusoid keeps 0.93 of its amplitude (bilinear model 0.71). Under the
route's present policy 1 over the sentinel background the same 1-px line
keeps 14% of its coverage: that is the remaining source of thin-feature
shimmer against the space background.

Open items:

- Route: supply the camera reprojection and enable `sentinel_camera` —
  done the same day, see [Camera reprojection for sentinel
  pixels](#camera-reprojection-for-sentinel-pixels) below (the fill keeps
  its alpha -1: the resolve's policy 2 accepts the fill sentinel as the
  far-plane pixel's own correspondence instead of requiring alpha 0).
- Rebenchmark the boundary cost at 5120x1440: done with the 3,840-word program
  (resolve 1.756 ms, [motion-output.md](../verification/motion-output.md#camera-reprojection-cases)).
- The seam TAA reference comparison of the motion-output suite shares the
  bytecode and has to be rerun by its owner.
- Per-pixel ripple of a toggling edge sample is (1-w) * contrast per frame
  (0.05-0.06 measured); a longer Halton period or a higher weight trades it
  against convergence time and ghost duration.

## Camera reprojection for sentinel pixels (2026-09-12)

Sentinel pixels (RT2 depth -1: background, nebula, effects, everything the
route does not write, 75-80% of a gameplay frame per
[iteration-08.md](../verification/iteration-08.md)) resolved current-only
under policy 1, so the background crawled whenever the camera turned. The
route now reads the engine's live camera and hands the resolve a far-plane
camera transform (policy 2), end to end:

**Camera read** (`src/proxy/camera_state.{h,cpp}`). Per
[camera-state-and-frame-routine.md](../reverse-engineering/camera-state-and-frame-routine.md)
the projection buffer is `*0x00608a38` and the view buffer `*0x00608a40`
(16 floats each, row-major, row-vector, left-handed; `view = world * V`,
`m23 = 1`; `m22`/`m32` are per-submission scratch and are never read). The
values are final at the per-view Clear the view activation issues right after
building them, so the route reads them in its Clear hook: the scene view at
the depth-only Clear that moves the selector from Background to Scene (the
view every routed draw and the sentinel pixels of the scene phase belong
to), and the background view at the latching Clear (diagnostics only: the
`camera_state` line reports its rotation against the scene view and both
projections, so the next gameplay run settles whether the sky view shares
the scene view's FOV; the transform uses the scene view). The read is gated
on the exact executable identity object_trace already verifies (SHA-256,
base, PE headers; shared and cached in `object_trace::executable_verified`)
and on `X3M_MOTION_OUTPUT=1 X3M_TAA=1`; no code is patched and no engine
memory is read for a foreign executable. The two pointer slots live in the
image's data section and are validated once with `VirtualQuery`; each
buffer pointer is validated with `VirtualQuery` when its value changes and
the verdict cached (the buffers are allocated once at renderer init), so a
steady frame costs two 64-byte copies. Validation of the values
(`renderer::camera_state_from_matrices`): all 32 floats finite,
`m00, m11 > 0`, `m23 == 1`, the view's upper-left 3x3 orthonormal within
1e-3 (camera-numerics measured 3.6e-5) and its elements 3/7/11/15 the
identity template's 0/0/0/1. The state keeps `{m00, m11, m20, m21, R, t}`.

**Transform** (`src/renderer/camera_reprojection.h`, pure arithmetic shared
with the fixtures and the host unit test). For a current NDC direction at
infinity `d_view = ((x - m20)/m00, (y - m21)/m11, 1)`, `d_world = d_view *
R_cur^T`, `d_prev = d_world * R_prev`, previous NDC
`(d_prev.x * m00_prev / d_prev.z + m20_prev, d_prev.y * m11_prev / d_prev.z +
m21_prev)`, valid iff `d_prev.z > 0`. Translation is ignored: a background at
infinity has no parallax; for a far but finite unrouted object (a distant
station, a planet at a finite distance) the residual is its parallax, which
the neighborhood clip bounds. The resolve's policy-2 path forms
`currentClip = (2u - 1, 1 - 2v, depth = 1, 1)` in D3D NDC (y up) from the
unjittered position, applies `clip_to_previous` as four rows dotted with that
vector (column-vector multiplication, row-major storage), divides by `w`,
flips y back and restores the half texel plus the current jitter — exactly
the routed path's convention, so no shader convention had to change. The
builder therefore emits the 4x4 with rows `(N00, N10, 0, N20)`, `(N01, N11,
0, N21)`, `(N02, N12, 0, N22)`, `(N02, N12, 0, N22)` where `(x, y, 1) * N =
(X, Y, W)` is the row-vector chain above: `x/w`, `y/w` are the previous NDC,
`z/w = 1` (the far plane: the disocclusion test then accepts sentinel and
far history and rejects a nearer occluder that moved away), the current `z`
column is zero (the map depends on the direction only) and `w > 0` exactly
when the direction is in front of the previous camera.

**Shader fix.** The resolve expected far-plane pixels to carry motion alpha
0, which the route's fill (RT1 alpha -1, RT2 -1 in one draw) never
produces without changing the RGBA32F ABI. Under policy 2 a far-plane pixel
that is its own correspondence (no closer neighbor won the dilation, motion
alpha exactly -1) now keeps the camera path; a dilated neighbor with alpha
-1 is a routed draw without history and still rejects, any other alpha
rejects as before. The fixture's `sentinel-camera-fill` mode (the route's
ABI) reproduces the alpha-0 mode's metrics exactly (thin line drift 0.003
px, silhouette ratio 178x, no ghost). Bytecode 3,840 words (3,794).

**Policy selection per frame** (`renderer::camera_sentinel_policy`,
`X3M_TAA_SENTINEL`): `auto` (default, `--taa-sentinel auto`) takes policy 2
when the scene view of this frame and the scene view of the frame the
history came from (the last frame that completed a resolve on this device;
cleared whenever the history is invalidated, by Reset, and when a frame does
not resolve) are both valid, the rotation between them is at or below
`X3M_CAMERA_CUT_DEG` (default 20, `--camera-cut-deg`) and the transform
builds; otherwise policy 1 with the identity matrix, which the resolve never
applies. A rotation above the bound is a **cut**: the route feeds it into the
resolve's `cut` input beside the displacement/missing-key verdict (the frame
line keeps `cut` as the displacement verdict and adds `camera_cut`), the
frame resolves current-only and re-establishes the history and its view.
`1` never reprojects (the previous behaviour, bit-identical: the seam suite
proves the colour equal to the run without a camera). `2` is the strict
diagnostic form of auto: a frame whose camera cannot be read or whose
transform fails **skips the resolve** (`taa_skip` 9) instead of degrading to
policy 1, so a broken camera read shows in gameplay and in the log; frames
without a previous view still resolve (that is how the history bootstraps).

**Diagnostics.** `motion_output_frame` gains `camera_valid
camera_background_valid camera_reads camera_policy camera_reason camera_cut
camera_rotation_deg`; a bounded `camera_state` line (capture frames and every
`X3M_CAMERA_LOG` frames, default 300, `--camera-log`) carries the read status
and failure codes, the buffer addresses, `p00 p11 p20 p21`, `r00..r22`, `t`,
the background view's `p00 p11` and rotation against the scene view, the
view the history holds after the frame and the decision. The device line
reports the camera status (`active`, `executable_mismatch`, `disabled`,
`fixture`) and the switch. `tools/analysis/analyze_camera_state.py` reads
these lines and, for capture frames with per-draw constants, compares
`inverse(g_mViewInverse)` (c34-36) and the recovered projection
`P = WVP * W^-1 * C` with the read state, plus the object-trace
`object_matrix role=view` rows bit-exactly; the iteration-08 log carries no
`camera_state` lines (0 of 109 frames), so that check waits for the next run.

**Environment-map exclusion.** The frame routine's second scene
(`0x00472201`: mid-frame `EndScene`, six `ID3DXRenderToEnvMap::Face` target
changes each with `Clear(TARGET|ZBUFFER)`, a per-face view written to the
same globals and a full material traversal, then `BeginScene`) must reach
neither the route nor the history nor the camera state. The existing gating
guarantees it without new code: the selector rejects the frame at the first
face's `SetRenderTarget` (a target change is not accepted in
`AwaitInitialClear`, `Background` or `Scene`), after which no draw passes
gate 2 (routing, jitter and row recording need the Scene phase), the
Background-to-Scene Clear never happens (the scene camera is never read), the
resolve is not reached (`taa_skip` 2) and the history and its view are
dropped before Present. The motion-output fixture's `envmap` script proves
both placements (before the scene's depth Clear, and before the initial
Clear where the frame does not even latch): frames with the sequence show
`routed=0 gate2=9 selector_state=9 taa_skip=2 camera_valid=0`, RT1 holds the
fill alone, the `camera_state` line reports the scene view unread and the
history's view dropped, and the routed frames around them resolve without
history. The cost is the whole frame's TAA (and the following frame's,
whose keyed draws all miss); a game situation that rendered environment maps
every frame would show as `selector_state=9` on every frame line. The
per-view Clear when `view[0x278] == 0` (no viewport) is the one activation
this read does not observe; the callsite patch of
camera-state-and-frame-routine.md section 8 remains the fallback.

**Cost.** Two 64-byte copies per frame (plus two `VirtualQuery` calls when a
buffer pointer changes), one 3x3 chain per frame at the resolve; no per-draw
work. Boundary cost with the 3,840-word program: see
[motion-output.md](../verification/motion-output.md#camera-reprojection-cases).

**Verification.** Host unit test
`verification/analysis/test_camera_reprojection.py` (the header compiled
natively, yaw/pitch/roll/FOV/off-center/translation against a basis-vector
oracle, behind-camera invalid, validation failures, the switch and cut);
`run_temporal_pass.py` camera cases (a sky rendered from the camera state
with the route's sentinel ABI: yaw and pitch track the render within 0.12
and 0.18 px of the static control under the jitter, 0.06 px unjittered at 90
degrees and 0.026 px at 28 degrees, five chained reprojections within 0.034
px, the identity matrix and the swapped convention crawl 0.86 and 1.05 px,
a 25-degree jump is a cut whose frame equals its render exactly);
`run_motion_output.py` camera cases (fake engine globals through the seam,
the DLL's resolve equal to the reference driven by the same builder byte
for byte, the switch off bit-identical to no camera, the strict mode, the
logged state and decisions, the environment-map script). Details in
[temporal-resolve.md](../verification/temporal-resolve.md#camera-reprojection-sentinel-policy-2-2026-09-12)
and [motion-output.md](../verification/motion-output.md#camera-reprojection-cases).

**Limits.** Translation is ignored (finite unrouted objects keep their
parallax); the transform is one rigid rotation per frame (no rolling-shutter
or per-view differences: a sky view with a different FOV from the scene view
would be reprojected with the scene FOV — the `camera_state` line reports
both so the next run can tell); the read observes only views that issue a
per-view Clear; the first frame, every cut and every Reset resolve
current-only for one frame.

## Stage 3 of the HDR scene path: TAA on HDR (2026-09-12)

Implemented per [hdr-scene-path.md](hdr-scene-path.md) §3 ("TAA on HDR")
and §4 (pass order), behind the existing switches: with `X3M_HDR=1` and
`X3M_TAA=1` the resolve consumes the FP16 scene target directly; with
`X3M_HDR=0` the 8-bit path of step 3 above is untouched, bit for bit
(`run_temporal_pass.py`, `temporal_run.py` and every 8-bit motion-output
case reproduce their records; the tracked reports `temporal-pass.txt` and
`temporal-resolve.txt` are byte-identical to the pre-change commit).

**HDR input path.** `FrameInputs::color` (the `A16B16G16R16F` texture
input `TemporalPass` always had) receives the container of the HDR pass's
level-0 target surface (`GetContainer`, one reference for the run). The pass
samples it as `s0` with no scratch copy and no `CheckDeviceFormatConversion`
gate: `ensure_scratch` and `ticks_copy_color` are simply not exercised (they
remain for the 8-bit `color_surface` input). The output is the pass's FP16
history texture (`Output::color`, borrowed until the next run); nothing is
copied back. Instead `MotionOutput` publishes it as `hdr_resolved_` and the
write-back that ends the redirect samples it (`HdrPass::write_back(...,
source)`: the meter chain and the tonemap draw read the resolved image; the
emergency `StretchRect` rung still copies the target, i.e. the unresolved
scene, never a black frame). Ping-pong (no copy) was chosen over a copy-back
into the target: one full-screen FP16 copy less per frame, and the stage-1/2
write-back is unchanged apart from the sampled texture.

**Order at the scene end** (both scene ends: the engine hook and the bloom
`StretchRect` fallback): `resolve_hdr` — the single resolve attempt of the
frame, on the FP16 target while it is the physical RT0 (the pass's
`SavedState` saves and restores that binding, so the redirect and the pass
compose; RT0 not being the target is skip 10 as on the 8-bit path) — then
`end_redirect`: meter chain over the resolved image, AgX (or identity)
write-back of it into the game's RT0, rebind, and only then the compositor
sees RT0. `resolve_allowed` records the attempt, so the 8-bit resolve that
follows `end_redirect` in the code is a no-op on a frame the HDR resolve
handled; without the redirect (HDR off, refused, blocked, suspended) the
frame takes the 8-bit path exactly as before.

**Failure and reset.** A failed HDR run (the pass reports it and drops its
history) leaves `hdr_resolved_` null: the write-back presents the unresolved
scene, `motion_output_taa_failed … hdr=1` logs the HRESULTs, the frame line
carries `taa_hdr=1 taa_result=…`, and the next frame resolves current-only.
`before_reset`, `release_resources` and every end of the redirect clear the
borrowed pointer. The fixture seam `HdrFault::Resolve` (14) exercises the
path (`seam-taa-hdr-tonemap-fault`).

**Luminance weighting in `resolve.hlsl`.** One new constant register,
`c22.x = k` (`ResolveConstants::luminance`, `kLuminanceRegister`; c8..c21
stay the AgX block). Every colour that enters the temporal statistics —
the current pixel, its 3×3 neighbourhood (min/max box, mean ± 1.25σ) and
each of the 16 Catmull-Rom history taps — is first scaled by

```
w(c) = 1 / (1 + k · max(luma(c), 0)),   luma = dot(c, (0.2126, 0.7152, 0.0722))
```

and the blended result is inverted by `c' / (1 − k · max(luma(c'), 0))`
(denominator floored at 1/65504). The clip and the blend therefore run in a
bounded domain in which a bright sub-pixel feature carries a fraction of its
radiance, while the stored history stays in engine radiance (no rescaling
when `k` changes between frames). The current-only early returns hand the
unweighted colour through. `k = 0` is selected by a compare, not by
`1/(1+0)`: the colours are multiplied by the constant 1.0 exactly, which is
why the 8-bit route is bit-identical (the migration test). Inputs are
already finite and ≤ `rejection.z` = 65000 in magnitude (FP16 Inf reads as
Inf and is rejected; 65504 is the FP16 maximum, so the bound rejects
Inf-adjacent values) but not necessarily positive — an FP16 scene keeps the
negative result of a subtractive blend — which is why the luma is floored
at 0 in both directions (review 24): a pixel of non-positive luma is the
identity and its inverse too, every weight lies in (0, 1], and a pixel with
`luma ≤ −1/k` can no longer make `1 + k · luma` zero or negative (an Inf or
sign-flipped weight that poisoned its 3×3 neighbourhood's statistics). The
inverse of a convex combination of weighted colours is exact (`k · luma' <
1` strictly); the per-channel clamp can in principle move the history to a
box corner whose luma exceeds every neighbour's (adjacent saturated
primaries several stops over the exposure with a history of a third
chromaticity), which is what the denominator floor is for: the output is
then finite but large, a one-pixel flash bounded again by the next frame's
weighting, never Inf or NaN. The sentinel/camera path, the disocclusion
test and the alpha carry are untouched: weighting only touches colour
values. Cost: the compiled resolve grew from 3,840 to 4,487 words (28
weightings of six instructions each; 4,375 before the luma floor).

**Derivation of `k`** (`MotionOutput`, at the latch, `hdr_taa_k_`):
`k = exp2(EV)`, the exposure multiplier the AgX write-back applies to the
resolved image in the same frame (EV manual, or adapted at this latch from
the previous frame's meter; `ExposureState::k()` = `taa_k(exposure)` of
`exposure_reference.py`). Units: inverse engine radiance, so `k · luma` is
the display-relative (pre-tonemap) luminance the tonemap sees — the key
luminance 0.18 gets `w ≈ 0.85` at any exposure, a 16× brighter feature
`w ≈ 0.26`. Typical range `2^±8` = 1/256 … 256 (the EV clamps); a normally
exposed scene sits near 1. With the identity write-back (stage 1, no
exposure model) `k = 0`: the unweighted resolve, which keeps the stage-1 HDR
twins of the TAA runs within one code of the 8-bit twins. `X3M_TAA_K=<k>`
(0 ≤ k ≤ 65504) overrides the derivation for A/B and fixtures; the frame
line reports `taa_k`, `hdr_frame … k=`. Not chosen: a fixed default with
manual exposure (manual EV is still an exposure the tonemap applies, so the
derivation holds), and `k = 1` with the identity write-back (it would move
the HDR twins away from their 8-bit references for no display benefit).

## Mip LOD bias for routed material draws (2026-09-12)

The sampler half of the TAA blur fix. Jittering the raster by sub-pixel
offsets and blending frames supersamples the scene, so the texture
minification the hardware computes per pixel is no longer the right
footprint: the resolved image integrates several jittered samples of every
pixel, and a mip level chosen for one sample's footprint is one level too
coarse for the integrated result. The customary correction is a negative
`D3DSAMP_MIPMAPLODBIAS` on the material samplers; the intended value here is
**−0.5**, applied only while the jitter is active and only on the routed
draws. Switch: `X3M_TAA_MIP_BIAS=<float>` (`tools/manage.py launch --taa
--taa-mip-bias -0.5`); unset or `0` is off and bit-identical to before, which
the fixture proves ([taa-mip-bias.md](../verification/taa-mip-bias.md)).

**Why −0.5.** A mip level halves the sampling rate per axis (LOD +1 ≙ one
octave). With the route's jitter sequence, four consecutive frames place the
raster sample at the four quadrant offsets of the pixel and the resolve's
exponential blend carries most of its weight over those frames, so the
effective sampling rate per axis is about doubled: 2× per axis is one
octave, and half of it, −0.5, is the conservative value that keeps the
trilinear blend between the level the hardware would choose and the next
finer one rather than jumping a whole level (a full −1 sharpens toward level
0 at the cost of texture aliasing under motion, which the temporal filter
then has to hide). The game's material mip chains are complete down to 1×1
and level 0 is the sharpest image the game ever holds for a texture
([sampler-states-and-mips.md](../reverse-engineering/sampler-states-and-mips.md),
section 2), so the bias only moves the blend toward detail that exists;
`VideoTextureQuality`'s top-level skip composes additively (a lower setting
has a lower-resolution level 0, and the bias sharpens toward that).

**Why only routed draws.** The game's `ID3DXEffectStateManager` shadows
sampler state per `(stage, type)` and never writes `MIPMAPLODBIAS`, so a
value the proxy sets stays on the device until the proxy puts it back
(section 1 of the study). Left resident it would also bias the compositor,
the bloom chain, `gui2d`/UI, particles and the skybox, whose stages sample
1:1 render targets or unmipped textures with `MIPFILTER` `NONE`/`POINT` — a
visible softening or aliasing of the composite. The routed
transformed-shader pairs (the reviewed material `(VS, PS)` pairs of
`motion_output_profiles`) are the only safe discriminator: they are exactly
the ship, station, asteroid and planet materials whose stages 0–2 and 5–6
run `ANISOTROPIC/LINEAR/LINEAR` over DXT mip chains, and they automatically
exclude everything the bias must not touch ("after bloom == HUD" is not a
valid partition; the sky pair is shared with `gui2d`). Within a routed draw
the bias goes only to stages whose bound texture has more than one level
and whose `MIPFILTER` is not `NONE` (the cube stages 3–4 are unmipped and
stay untouched; the bias would be inert there anyway).

**Mechanism** (`MotionOutput::apply_mip_bias` / `restore_mip_bias`,
`src/proxy/motion_output.cpp`). Two light hooks (`SetTexture` slot 65,
`SetSamplerState` slot 69; installed only with a non-zero bias) feed a
16-stage sampler shadow: the application's texture pointer (identity only,
never dereferenced later) with its level count (`GetLevelCount` asked once
per pointer change inside the hook's native section), its `MIPFILTER`, and
the value the bias replaced. A routed draw walks the bound-stage bit mask:
an eligible stage not yet biased gets one `SetSamplerState` (after one
`GetSamplerState` of the value to restore, the first time; the game's shadow
means that value never changes behind the proxy's back), a biased stage that
stopped qualifying (texture or filter changed) is restored, everything else
is a few compares. Consecutive routed draws find the bias set. The restore
runs at every restore point of the lazy RT mode (`restore_bindings`): before
any draw that does not route, at `Clear`, `StretchRect`/scene end,
`EndScene`, `Present`, before `Reset`, around the state block hooks and the
getters the lazy mode hooks, and at the final `Release`; a state block
`Apply` or a `Reset` resynchronizes the shadow (bindings re-read natively,
filters and saved values forgotten). An application write of
`MIPMAPLODBIAS` is counted, logged (`motion_output_mip_bias_game_write`) and
becomes the restore value; the static analysis says it never happens. No
`GetSamplerState` runs per draw; nothing allocates. Per-frame line:
`mip_bias`, `mip_bias_sets`, `mip_bias_restores`, `mip_bias_draws`,
`mip_bias_stages` (mask), `mip_bias_reads`, `mip_bias_game_writes[_total]`,
`mip_bias_failures`, `mip_bias_biased_now`; session totals in
`motion_output_mip_bias_summary`. Capture frames restore the bias before
every draw's diagnostics (as the lazy mode does for RT1/RT2), so the
capture's own `sampler … state=8 bias=` lines show the application's value,
never the proxy's.

**Interaction with 16× anisotropic minification and the fill-rate caveat.**
The material stages run `MINFILTER = ANISOTROPIC` with `MAXANISOTROPY = 16`
(section 4 of the study). Anisotropic filtering already picks a finer level
along the major axis and takes up to 16 taps along it; a negative bias
shifts that footprint's level by half a level, so the taps land on a finer
level and each trilinear lookup touches more texels — a texture bandwidth
cost proportional to the biased draws' coverage, not a shader cost. The
fixture measures set/restore counts and the image effect, not throughput,
so the performance pass must measure the routed draws' GPU time in the game
with the bias on versus off before the value is promoted from diagnostic to
default; if it shows, the levers are a smaller bias (−0.25) or biasing stage
0 (diffuse) only. Restore cost: one `SetSamplerState` per biased stage at
each transition from a routed to an unrouted draw (the scene interleaves
`z_only`, particle and material draws, so up to ten sampler calls per
material batch; D3D9 state changes with no GPU synchronization), counted
per frame for the telemetry.

**Not covered.** The value stays diagnostic until a gameplay run shows the
resolved image sharper without new shimmer; `MAXMIPLEVEL` is now captured
so a stage with a clamped top level (where the bias buys nothing) can be
recognized; native Windows is cross-compiled only.
## Post-resolve sharpen (2026-09-12)

Run 2 of iteration 9 ([iteration-09-run2.md](../verification/iteration-09-run2.md)
§4) put 87–93% of the stationary softness on the jitter supersampling itself
(gradient-energy ratio 0.51–0.63, MTF50 0.69 → 0.35 c/px on burst 629) and
found no knob inside the resolve. The remedy is therefore outside it: a
robust contrast-adaptive sharpen of the **display image only**, behind
`X3M_TAA_SHARPEN=<0..1>` (`tools/manage.py launch --taa-sharpen`, requires
`--taa`). 0 or unset is the pass off and every route is bit-identical to
before (the sharpen program is not even created); 1 is the strongest
setting.

**Algorithm** (`src/temporal/rcas.hlsl`, our HLSL reimplementation of the
RCAS AMD published with FidelityFX Super Resolution 1.0, MIT): the five-tap
cross around the pixel, a luma noise detector that halves the lobe on pure
noise, a per-channel peak-range limiter derived from the ring's min/max
(the least-permissive channel rules, so a ring touching 0 or 1 gets no
sharpening in that channel), one negative lobe of at most −0.1875 applied to
the ring and normalised. Sharpness is the published stops parameter:
`stops = 2 · (1 − s)`, gain `exp2(−stops)` (`s = 1` → gain 1, `0.5` → 0.5,
`0.25` → 0.354; `src/temporal/sharpen.h`, register `c23`, uploaded once per
draw). Two additions of ours: every division is guarded (a black or white
ring gives a zero lobe instead of `0 · ∞`) and the result is clamped to the
five taps' own min/max, so the output can never ring past its
neighbourhood — the fixtures check the 3×3 bound, which contains the cross.
Every tap is saturated first, so a non-finite input becomes the backend's
`saturate()` of it (0 for NaN and −∞, 1 for +∞ on the verified backend) and
can neither propagate nor widen the limiter.

**Placement, and why the history never sees it.** The sharpen is a display
transform: feeding a sharpened image back as history would sharpen it again
every frame (an unstable accumulation of the residual) and would also break
the resolve's neighbourhood clip statistics. Both routes therefore sharpen
*after* the history set is complete and only on the way to the game's 8-bit
target:

* *8-bit route* (`TemporalPass::run`, `FrameInputs::sharpen`): the resolve
  writes its FP16 colour history as before; then, inside the same state
  bracket and scene, RT0 becomes the caller's `color_surface` (the game's
  RT0, whose contents the resolve already copied into the scratch), the fresh
  history is bound as `s0` and `taa_sharpen_ps.hlsl` draws RCAS of it with
  the centre alpha carried. `Output::display_written` tells `MotionOutput`
  to skip the `StretchRect` copy-back (`taa_copy` stays `S_FALSE`,
  `taa_sharpen=1` on the frame line). No second capture/apply, no extra
  copy: the draw replaces the copy. The pass refuses `sharpen > 0` without
  the program, on the FP16 input path (there is no 8-bit destination in the
  pass on that path) and outside `[0, 1]`.
* *HDR route* (`HdrPass::write_back`): the resolve publishes its FP16 history
  by ping-pong as in stage 3; the write-back that samples it selects the
  RCAS variant of its program when the source is a resolved TAA image
  (`hdr_frame … sharpened=1`): `taa_sharpen_ps.hlsl` for the identity
  write-back, `agx_sharpen_ps.hlsl` for the tonemap. The latter tonemaps
  each of the five taps through the unchanged `agxTonemap()` of `agx.hlsl`
  and combines the five display-encoded colours, i.e. the sharpen acts
  **after AgX, before the 8-bit write**; the fixture tells this order from
  `AgX(RCAS(resolved))` per pixel. An unresolved scene (failed or absent
  resolve) is written back unsharpened; a failed sharpened draw with a
  clean restoration is redrawn unsharpened and counted, three failures
  disable the sharpen for the device (`sharpen_fallback`), and the
  programs gate themselves at attach (`caps.sharpen_reason`). On the 8-bit
  route (review 26) a failed sharpened draw that did not lose the device
  keeps the resolve and its published history and falls back to the
  `StretchRect` copy-back of that frame (`Output::sharpen_result`,
  `motion_output_sharpen_failed`); three such failures stop requesting the
  sharpen for the device. Before review 26 the failure failed the whole
  run, dropping the resolve and the history for a display-only draw.

**Spaces.** The game's 8-bit route is display-referred already (gamma
encoded by the game, copied linearly into the FP16 history), so the taps are
the game's own code values in `[0, 1]`; the HDR identity write-back sharpens
the same values; the tonemapped write-back sharpens AgX's display-encoded
output. No decode or encode happens around the sharpen on any route.

**Cost.** `taa_sharpen` is 418 words (five point taps and ~50 ALU
instructions); `hdr_tonemap_sharpen` is 1,691 words (five AgX evaluations
plus RCAS; the AgX program is 414). Numbers per route and size are in
[taa-sharpen.md](../verification/taa-sharpen.md). Not chosen: sharpening in
scene-linear space before the tonemap (one AgX evaluation instead of five,
but it sharpens radiance the sigmoid then compresses unevenly), a separate
full-screen pass on the 8-bit route (an extra target and copy where the
copy-back could simply become a draw), and a 3×3 kernel (the cross with the
min/max clamp already cannot overshoot; the wider support would only cost).
