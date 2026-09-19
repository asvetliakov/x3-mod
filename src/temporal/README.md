# Temporal resolve

`resolve.hlsl` is an original Shader Model 3 temporal reconstruction pass.
`resolve.h` defines its eight float4 constant registers and the CPU history
validity contract. The standalone GPU fixture loads this production shader
verbatim. Neither file is connected to the game loader yet. The shader does not
supply object identity, recover scene depth, inject jitter, or classify game
passes; those are required inputs from the renderer.

## Required inputs and state

| Slot | Input | Contract |
| --- | --- | --- |
| s0 | Current scene color | Scene-linear floating RGB; FP16 supported; no tonemapping/sRGB transfer |
| s1 | Current scene depth | R32F or raw-sampleable device depth, ordinary D3D depth in [0,1] |
| s2 | Previous resolved color | Same color space, exposure scale and dimensions as s0; FP16 |
| s3 | Previous scene depth | Previous frame's **unfiltered original depth**, not blended depth |
| s4 | Optional object reprojection | RGBA32F, interpretation below; FP16 absolute UV/depth is insufficient |
| s5 | Current reactive coverage | R32F; exactly zero means known safe, every other value means reactive |
| s6 | Previous reactive coverage | Owned R32F snapshot aligned with previous resolved color/depth |

Every sampler uses POINT min/mag, no mip filter, CLAMP U/V and sRGB sampling off.
The resolve performs its own history reconstruction (a 16-tap Catmull-Rom
gather, one tap on the texel grid) and its own depth footprint test; every
fetch is an explicit LOD-0 `tex2Dlod`. Hardware bilinear filtering on any
input would violate this contract. Output is a distinct FP16 target, never simultaneously
bound as an input; alpha is the current color's alpha (the game's main-target
alpha survives the copy-back; history alpha is never blended; a NaN alpha
becomes one). Disable depth, blending, alpha test, fog and
sRGB output; with pre-transformed vertices and no vertex shader, stage 0's
`D3DTSS_TEXCOORDINDEX` must be 0 and `D3DTSS_TEXTURETRANSFORMFLAGS` disabled,
or the fixed-function pipeline remaps the quad's coordinates. The shader does not manage or restore application GPU state.

All textures describe the same full, local viewport with the same dimensions.
Source and previous viewport depth ranges must be MinZ=0, MaxZ=1; otherwise the
producer must first undo the viewport depth scale/bias. Reversed-Z is not this
contract.
A subrect of an atlas requires copying/remapping first. In D3D9, raster sample
positions are integers. Use a fullscreen primitive shifted **-0.5 pixel** in
both raster axes so TEXCOORD0 at pixel `(x,y)` is
`((x+0.5)/width, (y+0.5)/height)`. Do not add another half-texel correction in the
shader when **sampling textures**. Camera reconstruction uses a separate
half-texel conversion described below. The fixture verifies both using original
rasterized geometry and nonidentity zoom/rotation, not only identity/translation.

## Matrix and jitter ABI

Upload `ResolveConstants` to PS c0–c7. Four explicit c0–c3 rows multiply a column
vector using `dot`; HLSL's default matrix packing is irrelevant. The matrix is:

```text
previous_unjittered_view_projection * inverse(current_unjittered_view_projection)
```

It maps current unjittered homogeneous clip coordinates to previous unjittered
clip coordinates **in one consistent world coordinate regime**. At a pixel,
subtract **half a texel** and current jitter UV, reconstruct
`(2*u-1, 1-2*v, device_depth, 1)`, multiply, divide by positive previous W, convert
XY to normalized viewport coordinates, then add **half a texel** and the
**current** jitter UV again. The resolve never applies the previous jitter:
`c5.xy` is packed by `prepare` for ABI stability and is not read by the shader.
Texture center UV and raw camera viewport coordinates differ: at
16×16, pixel (8,8) has texture UV (8.5/16,8.5/16), but raw projection NDC (0,0).
Omitting both half-texel conversions cancels for identity/translation yet causes
errors for zoom and rotation. The unadjusted game camera matrix must not include
another fullscreen-quad sampling correction.
The transformed Z/W is the expected previous depth used for rejection. Merely
comparing old depth against current depth would fail camera translation.

`prepare` accepts jitter in raster pixels: positive X right, positive Y down.
They are actual raster displacements, not camera ray offsets of opposite sign.
Both matrix projections exclude jitter. The input colors and depths must come
from matching jittered rasterization. The resolve outputs on the **unjittered**
pixel grid: output pixel `p` represents unjittered position `p`, its
current-frame sample is the jittered raster value at `p` (which shows content
at `p - current jitter`; that sub-pixel offset is the supersampling), and the
previous resolved color lives on the same unjittered grid. History is read at
the content's previous unjittered position **plus the current jitter**, i.e.
at "pixel center minus velocity". For a static scene that is `p` itself, every
history tap lands on a texel center and the output is stable across jitter
phases while converging to the jitter-averaged coverage of edges. Adding the
previous jitter instead (the convention before 2026-09-12) moved the taps by
the jitter difference every frame: stationary geometry oscillated and repeated
fractional bilinear resampling blurred it.

`prepare` validates dimensions, finite coefficients, weight and thresholds.
On false the caller must skip dispatch and invalidate history; do not upload a
partially prepared structure. Matrix coefficients above 1e15 are rejected and
projected values outside 1e20 are rejected in the shader. Unprojectable samples
use current color.

## Per-pixel object motion

When enabled, s4 alpha carries three states:

- `0`: camera reprojection is explicitly valid for this pixel (static geometry).
- `1`: RG contains the **previous unjittered absolute texture UV of the content
  at this jittered sample** (a static object under jitter `j` reports
  `p - j`), including the half-texel that converts previous raw viewport
  coordinates to texture centers. B contains expected previous device depth for
  this surface. The resolve adds the **current** raster jitter only (so static
  content lands on its own texel center); it adds neither the previous jitter
  nor another half texel on this path.
- `-1`: correspondence is unknown/invalid, so reject history. Use this for dynamic
  geometry without trustworthy previous transforms, deformation or identity.

RGBA32F is required: at a 5120-pixel width, FP16 absolute UV can quantize by
more than a pixel, and FP16 expected depth near 0.5 would lose more than the
1e-4 absolute tolerance. Using R32F history depth cannot repair precision lost in
the motion input. The fixture's expected depth 0.5002 case remains as a routing
regression; with the one-sided, 0.02-relative test of 2026-09-12 it no longer
distinguishes FP16 rounding, the UV argument stands. The detached
[rigid producer](../../docs/verification/rigid-motion.md) separately verifies
RGBA32F rendering and this input contract. It is not live
game routing or complete coverage of transparent contributors.

The producer writes these exact states. Intermediate alpha values are reserved;
invalid/nonfinite alpha is rejected. The default clear value is -1 when motion
coverage is not known. Globally disabling motion is appropriate only when the
caller knows camera reprojection is sufficient. Missing moving-object motion
must not silently become a static camera fallback.

Expected previous depth is necessary even with a correct previous UV. A moving
object's current depth need not equal its previous depth. The object path must
be derived from actual previous object geometry/transforms, not guessed buffer
identity or draw order. This module does not yet obtain that data from X3.

## Algorithm (resolve quality pass, 2026-09-12)

Per output pixel `p` (unjittered grid), in this order:

1. **Early outs.** Snapshot mode; nonfinite current color (black); invalid
   history, zero weight, invalid current depth, reactive current pixel and
   the sentinel policy below all return the current color.
2. **Depth-sentinel policy** (`c7.w`): 0 off; 1 a current pixel whose depth is
   the -1 sentinel (nothing routed wrote it) is current-only; 2 such a pixel
   is reprojected through the camera path at the far plane (depth 1) with
   the `clip_to_previous` the route builds from the live engine camera
   (docs/architecture/temporal-integration.md, "Camera reprojection for
   sentinel pixels"). Such a pixel keeps the camera path whether its motion
   alpha is 0 or the route's fill sentinel -1 (the far-plane pixel is its own
   correspondence when no closer neighbor won the dilation); a dilated
   neighbor with alpha -1 is a routed draw without history and rejects.
   The route selects 2 only with a valid transform (`X3M_TAA_SENTINEL`),
   else 1 with the identity matrix, which the resolve then never applies.
   Under policy 1 a jittered edge against the sentinel background is wiped
   on every phase that uncovers it (see the thin-line fixture,
   "sentinel-current-only": a 1-px line keeps 14% of its coverage); under
   policy 2 it converges like any other edge (modes "sentinel-camera" and
   "sentinel-camera-fill" of the fixture).
3. **Closest-depth dilation.** The closest valid depth of the current 3×3
   (center wins ties) selects the pixel whose correspondence is used: its
   camera reprojection (or its motion RG/B when `c7.x` is set and alpha is
   1), shifted by its offset, i.e. its **velocity** applied to `p`. A pixel on
   the far side of a silhouette follows the foreground it borders. The cross
   is not enough: on the phases that uncover a silhouette corner on both
   axes its only foreground neighbor is the diagonal one.
4. **Lookup validity.** Nonfinite or huge projections, nonpositive W, motion
   alpha other than 0/1 and an out-of-range previous UV reject. The
   sub-texel fraction is snapped to the grid below 1e-4 texel.
5. **Disocclusion test (the only depth rejection).** Every contributing tap
   (weight > 0.01) of the bilinear footprint must be proven either at or
   behind the expected previous depth, `previous >= expected - max(c6.x,
   c6.y·|expected|)` (defaults 1e-4 and 0.02), or the -1 sentinel;
   otherwise the lookup is rejected. History clearly in front was an
   occluder that moved away. History behind, or sentinel, is the background
   the surface's silhouette moved over as the jitter flipped its coverage:
   accepted and bounded by the clip, so edges and thin features accumulate
   their supersampled coverage. Because the threshold is the closest depth
   of the 3×3, a background pixel beside a silhouette accepts the
   foreground history it held on covered phases. NaN or above-range history
   depth is corrupt input and fails closed: only `>=` and `<=` survive
   compilation with NaN semantics on the verified backend (`v == v` folds to
   true, `<` and `>` compile to negated forms a NaN passes), so every such
   test is written with them.
6. **History color.** Catmull-Rom over the 4×4 texel neighborhood (16 point
   taps; the 9-tap form needs hardware bilinear filtering, which the sampler
   contract excludes). On the texel grid (static content) it reads that one
   texel under a real branch. Nonfinite taps contribute no energy and the
   rest renormalize; below half the weight the lookup is rejected. With the
   mask policy any nonzero-weight tap with reactive previous coverage
   rejects the lookup.
7. **Neighborhood clip.** The history is clamped per channel to
   mean ± 1.25 σ of the finite current 3×3, intersected with the 3×3 min/max
   box as the fallback bound, then blended: `lerp(current, history, c5.z)`.
   γ = 1.25 keeps a converged 1-px line within about 5% and biases a corner
   pixel with one bright neighbor by about 0.013 of coverage; 1.0 dims thin
   lines visibly, larger values readmit clamp-bounded ghosting.

Filtered current sample (`resolve_filter.hlsl` = `resolve.hlsl` compiled with
`X3M_CURRENT_FILTER`; `c22.y` = A in (0, 4], `X3M_TAA_CURRENT_FILTER`): the
`current` of that blend is the normalised exp(-A·d²) average of the finite
weighted 3×3 samples the clip already fetched, d in pixels from the pixel
centre to each neighbour's jittered sample position (neighbour offset minus
the current jitter). The clip statistics stay those of the unfiltered samples.
`TemporalPass` binds this variant only when A > 0 (the plain program's
bytecode is unchanged); it needs 521 instruction slots against the plain 507
and the 512 every ps_3_0 device guarantees, so a device may refuse it at
creation, which the pass reports as "filter unavailable" and the route answers
by running the plain resolve
([ledger](../../docs/verification/motion-output.md), "Run 139").

Weight `c5.z` (route default 0.9, `X3M_TAA_HISTORY_WEIGHT` 0.5–0.98): 0.85–0.95 is the sensible range; the
per-phase ripple of a toggling edge sample is (1-w)·contrast, convergence
takes about 2/(1-w) frames, and a larger weight holds clamp-bounded ghosts
longer. Cost per pixel: 10 current color, 9 current depth, 1 motion, 4 history
depth and 1 or 16 history color fetches (20 before; plus 1 + 1/16 mask
fetches under the mask policy); the compiled program is 3,840 words (3,794
before the far-plane fill-sentinel fix, 1,695 before the Catmull-Rom history).

## Rejection and history lifecycle

A nonpositive previous W, out-of-bounds previous UV, nonfinite color/depth, a
corrupt history depth, or a contributing history tap clearly in front of the
expected previous depth rejects history (step 5 above). History behind the
surface and sentinel taps are accepted. Previous RGB is clipped to the
variance box of the finite current 3×3 within its min/max bounds before the
blend. There is no sharpening or special transparency reconstruction.

### Reactive RGB coverage

`c7.y` enables current and previous reactive masks; `c7.x` independently enables
object motion. Exactly-zero coverage is safe. Positive, negative and nonfinite
values are reactive. This must describe **visible RGB contributions**, not source
or destination alpha: `SRCCOLOR/INVSRCCOLOR` particles can change RGB even with
source alpha zero. The producer must include all unsupported contributors or
conservatively overmark them, aligned to the actual jittered color raster.

A reactive current pixel returns current linear HDR RGB. After camera or object
reprojection, **any positive-weight history tap** with reactive previous coverage
rejects the entire lookup. No renormalization around a reactive tap is allowed;
zero-weight taps do not reject. This prevents stale particle color when an effect
disappears or moves away, while preserving accumulation behind genuinely occluded
effects whose visible coverage is zero. It is rejection, not reconstruction of
particle motion or transparent layers.

`c7.z=1` is an explicit mask-snapshot dispatch: it reads only s5, canonicalizes
safe/reactive to 0/1 and writes a distinct R32F target. Since 2026-09-19 the
snapshot modes are their own program (`resolve_snapshot.hlsl`, same registers),
which `TemporalPass` creates itself and binds for those draws; `resolve.hlsl`
no longer reads `c7.z`. The flicker-suppression variants (`resolve_thin*.hlsl`,
`resolve_age*.hlsl`; `docs/architecture/taa-flicker-suppression.md`) add `c24`
(thin-clip S, age wmax, LO, 1 / (HI - LO)), `c22.z` (alpha history), `s7` (previous
R32F age) and `COLOR1` (next age); the plain programs read none of them. The line-filter variants
(`resolve_*line.hlsl`; `docs/architecture/taa-lattice-crawl.md` section 9) add `c22.w` (A) and `s8`, the
A8R8G8B8 line mask `TemporalPass` draws first with `line_mask_ps.hlsl` (`s1` input, `c4.xy`, `c7.z` pass, `c7.w` width). Ordinary resolve uses zero;
`prepare` initializes the mode and reserved component to zero. Runtime code must
not use snapshot mode as a color resolve. `TemporalPass` uses this third GPU draw
only under `ReactivePolicy::RequiredMask` and owns the resulting ping-pong masks.
No CPU data transfer is involved.

Runtime policy is explicit. `Unavailable` produces current-only output and
cannot establish usable history. `KnownNonReactive` authorizes accumulation
without masks. `RequiredMask` rejects missing, wrong-format, wrong-size or
foreign-device inputs even on the first frame. Policy transitions invalidate
history. A low-level shader caller that disables masks is responsible for the
same known-nonreactive precondition; disabling masks is not a safe default for
unclassified particle/effects color.

Depth tolerance is `max(absolute, relative * abs(expected_previous_device_depth))`
and is one-sided: only history in front of the surface by more than it is
rejected. Defaults are absolute 1e-4, relative 0.02. These are **device-depth
units**, not meters. They require per-camera validation/tuning: distant
surfaces in a perspective projection may have very similar device depth. R32F
prevents adding FP16 quantization to D24 depth, but does not solve this
projection ambiguity. No claim of final game disocclusion quality follows
from the synthetic fixture.

Colors outside the configurable finite HDR magnitude limit (default 65000) are
invalid. Invalid current color outputs black, finite current color with invalid
depth outputs current color, and invalid neighbors/history never enter the
clipping/blend arithmetic. Finite scene-linear values above one remain above one.

`HistoryState::begin(width,height,epoch)` invalidates on dimensions/epoch change.
The epoch is a stable scene/camera-regime identity plus resource generation,
**not** the frame number or a per-clear depth-content counter. Ordinary rendering
and clears in each frame do not invalidate history. Explicitly invalidate on
device loss/reset, camera cuts, missing depth or motion, failed render/copy,
changes between camera/depth-content regimes (background versus scene), and any
exposure convention change. Use distinct state/epochs for distinct cameras.
Call `completed()` only when the next resolved color, corresponding current raw
depth **and any required reactive mask** have been saved successfully. Unknown
reactive coverage must not complete usable history. It owns no D3D objects; resource
lifetime, before-Reset release and atomic success belong to the renderer.

The X3 trace clears the same depth allocation between background, scene and UI.
An allocation pointer is not a history epoch. The scene must be resolved while
the matching scene color/depth are intact. UI and unmatched effects must not be
accumulated merely because they appear before Present.

## Verification

See [GPU verification](../../docs/verification/temporal-resolve.md). The fixture
uses original synthetic inputs, FP16 color/history/output, R32F depth, the actual
production shader, and the current CrossOver Preview builtin D3D9 backend.
See [reactive-history verification](../../docs/verification/reactive-history.md)
for original blended-particle GPU tests and owned-mask lifecycle evidence.

## Native D24X8 comparison decoder

`depth_decode.hlsl` is a verified fallback for this WineD3D backend's native
D24X8 RESZ snapshots, which expose shadow comparison rather than raw `tex2D`
depth. Bind the snapshot at s0 with point filtering, clamp addressing, no mipmaps
or sRGB, render at its dimensions with pixel-center UVs, and write to **R32F**.
The output is ordinary device depth in [0,1], matching resolve s1/s3. This shader
must not be used on INTZ or other textures already returning raw depth.

It performs 24 dependent comparison searches plus two endpoint checks (26 fetches
per pixel). The fixture preserves exact 0/1, distinguishes adjacent D24 values
from those endpoints, and observes at most one D24 step of error in its tested
set (acceptance limit two). It is not guaranteed bit-exact for every value.
Keep clear-depth semantics separate from the history rejection threshold.

See [decoder verification and cost limits](../../docs/verification/depth-decode.md).
The copy can occur within an existing scene without drawing or replacing the
original depth surface; decoding is a separate full-screen pass that requires
its own state restoration and resource ownership. Neither shader is injected
into the game yet.

The decoder clamps its interior midpoint below one before explicit endpoint
classification, preventing float32 midpoint rounding from aliasing nearby
geometry with clear depth. An independent CPU oracle covers exact-D24 and
float32-normalized comparison models; see the decoder verification document.

<!-- BEGIN current depth (step 1); appended by the live-route depth output, keep delimited -->
## Current depth (step 1)

`current_depth_ps.hlsl` is the authored fragment the live route appends to
every transformed material pixel program after the motion fragment
(`src/renderer/material_motion.cpp`; compiled by
`tools/shaders/generate_rigid_motion_pixel.py` into
`src/renderer/current_depth_pixel_program_inc.h`). It reads one TEXCOORD
interpolator holding the current clip z in `.x` and w in `.y`, exported by the
vertex variant from the same position temporary and the same matrix rows
(`c<matrix+2>`, `c<matrix+3>`) that produce `o0.zw`, and writes `z/w` to
`COLOR0`; the transformer relocates the input register/index, the one
temporary and the output (to `oC2`). With a MinZ 0 / MaxZ 1 viewport the value
is the screen-linear device depth the rasterizer writes for the same sample,
so RT2 (R32F, main dimensions) is ordinary D3D depth in [0,1] wherever a
routed draw covered the pixel and keeps the fill sentinel **-1** elsewhere:
exactly the `FrameInputs::current_depth` contract of the step-2 section
below. The fragment holds no literal and no constant, so the pixel ABI range
(c216–220) is unchanged.

The route fills RT1 and RT2 in one sentinel draw, binds RT2 with
`COLORWRITEENABLE2 = 15` for depth-capable rows, and reads it back in capture
frames as `depth_<device>_<frame>.r32f` (row-major R32F, no header). The GPU
fixture proves `oC2` against the analytic z/w (4e-6), the covered/uncovered
pattern and the authored replay drawn with `ZFUNC EQUAL` over the original's
D24 depth (exact on the tested backend); the route fixture proves the
sentinel/written pattern and z/w of the front-most routed draw per pixel
(4e-6). Per-draw jitter (`X3M_MOTION_JITTER=1`) moves the raster of RT0, RT1
and RT2 together; the depth is that of the jittered raster, which is what
the resolve's s1 expects. See
[temporal-integration.md](../../docs/architecture/temporal-integration.md),
"Step 1 implementation".
<!-- END current depth (step 1) -->

<!-- BEGIN route inputs (step 2); appended by the TemporalPass adaptation, keep delimited -->
## Route inputs (step 2)

This section describes how `TemporalPass` (`src/renderer/temporal_pass.{h,cpp}`)
maps the live route's outputs onto the sampler contract above. The shader ABI
is unchanged except for one new flag, `c7.w`, and (step 3) the output alpha,
which is the current color's alpha so the route's copy-back preserves the
game's main-target alpha byte. Step 3 wires this pass into the route at the
bloom copy with the embedded bytecode of this shader
(`src/renderer/temporal_resolve_program_inc.h`); see
`docs/architecture/temporal-integration.md`.

**Current color from the 8-bit main target.** `FrameInputs::color_surface`
accepts the game's A8R8G8B8/X8R8G8B8 default-pool render-target surface (it
need not be a texture level). The pass copies it once with `StretchRect`
(point filter, same size) into an owned A16B16G16R16F scratch texture that
becomes s0. The FP16 texture input (`FrameInputs::color`) remains for fixtures;
exactly one of the two must be set. The game writes linear values encoded as
8-bit UNORM; no sRGB decoding or encoding may happen on this path. The pass
sets `D3DSAMP_SRGBTEXTURE=FALSE` on every sampler and `D3DRS_SRGBWRITEENABLE=FALSE`
before every draw, and `StretchRect` performs a plain UNORM-to-float
conversion. The fixture drives all 256 codes through the copy under hostile
sRGB sampler/write states: every value is `v/255` within one FP16 ulp
(the CrossOver Preview backend truncates toward zero; 421 of 768 samples also
equal round-to-nearest-even), and a copy-back `StretchRect` of the resolved
FP16 surface into the 8-bit target restores the exact bytes. A gamma curve
would deviate by tens of ulps (code 63 decodes to 0.0497 instead of 0.247).

**Current depth from RT2.** `FrameInputs::current_depth` accepts the route's
R32F texture: device depth z/w in [0,1] where a routed opaque draw wrote,
and the -1 sentinel elsewhere. The pass copies it with `StretchRect` into the
owned R32F history slot for the frame (that slot is s1 now and s3 next frame)
and runs **no decoder draw**. The D24X8 comparison snapshot (`depth_snapshot`,
26 fetches per pixel through `depth_decode.hlsl`) remains for fixtures and
compatibility; exactly one of the two must be set. On one synthetic scene both
paths produce bit-identical color and depth within half a D24 step.

**Reactive coverage derived from the sentinel.** `ReactivePolicy::DerivedFromDepthSentinel`
requires `current_depth` and no mask texture. `prepare(..., depth_sentinel_reactive=true)`
sets `c7.w=1`: a current pixel with negative depth returns current color
(`validDepth` already rejects it; the flag makes the intent explicit), and a
previous tap with sentinel depth is accepted as the background behind a
silhouette (since 2026-09-12; before, it contributed no energy and the rest
of the footprint renormalized). Unlike `RequiredMask`, a sentinel tap never
rejects: the sentinel marks pixels that had no opaque history at all
(background, particles, unknown programs), not opaque history contaminated
by a blended contributor, so silhouettes against the background accumulate
their jittered coverage under the neighborhood clip. Blended effects drawn
over routed opaque geometry are **not** detected by the sentinel; the clip
bounds, but does not remove, their history contribution. This policy
completes usable history (unlike `Unavailable`, which invalidates every frame).
No third draw and no snapshot mask are involved. `FrameInputs::sentinel_camera`
selects policy 2 (`c7.w=2`, sentinel pixels reprojected at the far plane);
the route must leave it false until it uploads a real camera matrix and
fills the motion target with alpha 0 for unrouted pixels.

**Jitter.** `FrameInputs::current_jitter` / `previous_jitter` are raster
pixels, positive Y down; `prepare` divides them by the viewport size into
`c4.zw` / `c5.xy`. Only the current jitter is used: the camera path subtracts
it before the inverse projection and adds it back after the projection; the
motion path adds it once to the producer's RG. The previous jitter (`c5.xy`)
is uploaded for ABI stability and never read. History is the accumulated
output on the unjittered grid, so it is read at the content's previous
unjittered position plus the current jitter ("pixel center minus velocity"):
a static scene reads its own texel centers. The producer contract is
therefore: RG is the previous **unjittered** texture-center UV of the content
at the jittered sample (a static object reports `p - current jitter`), which
is what interpolating the previous unjittered rows across the current jittered
raster yields. `rigid_motion_ps.hlsl` subtracts `c0.zw` (the route's
`c216.zw`) from the previous projection **only for jittered previous rows**;
the live route keeps unjittered rows in its shadow and uploads **zero** in
`c216.zw`. Passing the actual prior jitter there would shift the motion path's
lookup by that jitter and destabilize static geometry. The fixtures prove the
resolve side: previous jitter +1 pixel with RG at the pixel's own center stays
at the pixel (never applied); current jitter +1 moves the motion-path lookup
by one pixel, and RG = own center minus the current jitter lands on the own
texel center; the camera path with an identity matrix lands on the own texel
center for any current jitter and transforms the jitter offset through a zoom;
and a stationary 32x32 scene rasterized over four 16-phase Halton periods is
resolved with zero interior change between phases, zero centroid drift and
edge coverage converging to the jitter-sampled coverage (see
`docs/verification/temporal-resolve.md`, "Stationary stability").

**Cut.** `FrameInputs::cut` carries the route's displacement/missing-key
verdict; it invalidates history for the frame exactly like `camera_cut`, and
accumulation resumes on the next frame.

**Output for copy-back.** `Output::color_surface` is level 0 of the resolved
FP16 texture. The caller copies it back into the 8-bit main target with
`StretchRect` (point, same size) before the application's own bloom copy. The
caller owns state save/restore around the whole copy / run / copy-back
sequence; `run` captures and restores everything it touches itself (state
block, render targets, depth, viewport, scissor); the two `StretchRect` copies
inside `run` touch no device state.

**Reset.** `before_reset` releases every default-pool object (histories,
masks, scratch) and keeps the compiled shaders and the borrowed device; `run`
is refused until `after_reset(SUCCEEDED)`, after which resources are
re-created lazily on the next run with invalid history. `shutdown` remains the
full teardown. The fixture keeps one pass alive across a real device Reset.
<!-- END route inputs (step 2) -->

## Post-resolve sharpen (`rcas.hlsl`, `taa_sharpen_ps.hlsl`, `agx_sharpen_ps.hlsl`)

`rcas.hlsl` is the shared RCAS core (our reimplementation of AMD's published
FidelityFX Super Resolution 1.0 RCAS, MIT; five-tap cross, luma noise
detector, guarded peak-range limiter, single lobe, plus a clamp to the taps'
min/max), bound to `c23` (`sharpen.h`: gain `exp2(-2 (1 - s))`, 1/width,
1/height). `taa_sharpen_ps.hlsl` includes it and sharpens `s0` as it is (the
8-bit route's resolved history, the HDR identity write-back);
`agx_sharpen_ps.hlsl` includes `agx.hlsl` with `AGX_NO_MAIN` and tonemaps
each of the five taps through `agxTonemap()` before RCAS. The `#include`
lines are expanded textually by the generator and the temporal fixture
(D3DXCompileShader gets no include handler). Contract and placement:
docs/architecture/temporal-integration.md, "Post-resolve sharpen".

## AgX tonemap and exposure meter (HDR scene path, stage 2)

`agx.hlsl` is the `ps_3_0` AgX write-back of the FP16 HDR scene path (s0 the
FP16 scene, c8..c21 the block `agx.h` declares: exposure and clamp, decode
mode, inset/outset matrices, log range, sigmoid coefficients, look; output
display-encoded, alpha carried). `hdr_meter_level0_ps.hlsl` folds the log2
luminance of the decoded scene into the first 4x4 reduction (s0 the scene,
c0 source size, c1 decode mode, c2 meter floor/clip, c3 output size);
`hdr_meter_reduce_ps.hlsl` averages 16 taps of the previous R32F level (c0,
c3). All three are compiled by `tools/shaders/generate_rigid_motion_pixel.py`
into `src/renderer/hdr_*_program_inc.h` with pinned manifests under
`verification/results/`, and their constants are pinned to
`tools/analysis/agx_reference.py` / `exposure_reference.py` by
`verification/analysis/test_agx_reference.py`.
