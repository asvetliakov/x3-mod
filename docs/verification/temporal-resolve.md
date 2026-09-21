# Original temporal resolve GPU verification

The production [resolve shader](../../src/temporal/resolve.hlsl) and
[CPU parameter/history contract](../../src/temporal/resolve.h) pass **78 of 78
numeric GPU readback checks**, across two complete resource generations separated
by successful device Reset. This is an implemented temporal reconstruction
algorithm verified in isolation. It is **not yet connected to X3** and does not
establish actual game motion vectors, scene-depth routing, jitter eligibility or
final TAA visual quality.

The current shader also supports current/previous reactive RGB coverage. Its
owned-history and original particle-blending tests are documented separately in
[reactive-history verification](reactive-history.md). This 78-sample fixture
explicitly exercises scenes without reactive contributors and remains passing
against the extended shader. The 2026-09-12 resolve quality pass (variance
clip, closest-depth dilation, one-sided depth disocclusion test, Catmull-Rom
history, sentinel policy) left every one of the previous 58 values unchanged
and added ten checks per generation; see "Resolve quality" below.

The original fixture uses a hidden standalone 16×16 D3D9 pure device in CrossOver
Preview's Steam bottle with process-local `d3d9=b`. It compiles the production
`resolve.hlsl` directly using the local D3DX compiler, uploads synthetic FP16 color, RGBA32F motion
and R32F depth, executes the SM3 shader into FP16 targets, and reads numerical
results. No game process is launched, no game shader bytes are used, and no
installation/settings are changed. Full parameter/sampler/pixel-center contracts
are in [the source README](../../src/temporal/README.md).

## Numeric checks in each generation

| Case | Expected RGB | What the input distinguishes |
| --- | --- | --- |
| Raw viewport geometry, center/neighbor | 0.5 / 0.53125 | Unadjusted clip-space plane at x2 zoom is rasterized by D3D9; sample positions come from the rasterizer |
| Zoom with rasterized history | 0.375 | Current center 0.25 reprojects to actual GPU history at optical center 0.5 |
| Zoom / 90° rotation optical center | 0.5 | Raw center pixel remains center; textured-UV half-texel errors move it |
| Static accumulation | 0.5 | Current 0.25 plus matching history 0.75 at weight 0.5 |
| Invalid history | 0.25 | Explicit invalidation suppresses the old sample |
| Disocclusion | 0.25 | Current depth 0.5, previous 0.25 rejects history |
| Camera one-pixel shift | 0.5625 | Matrix selects previous x+1 (0.875), not current x (0.25) |
| Perspective previous depth | 0.5625 | Previous W=2, expected previous depth=0.25, offset selects x+1 |
| Mixed-depth footprint, disocclusion | 0.25 | A half-pixel camera translation puts the lookup between previous x=8 (depth 0.5) and x=9 (depth 0.25, in front of the expected 0.5 by more than the 0.01 tolerance): a contributing tap is an occluder that moved away, the whole footprint is rejected. Re-derived 2026-09-12: the case used to pass 0.5 px as the PREVIOUS jitter, unread since the convention fix, so it had silently become a single-tap lookup at x=8 (0.5) |
| Mixed-depth footprint, surface | 0.5664 | Both taps at the surface depth: Catmull-Rom over previous x=7..10 (0.25, 0.75, 0.875, 0.25) weighted (-1, 9, 9, -1)/16 = 0.8828 |
| History behind the surface | 0.5 | Previous depth 0.9 against expected 0.5 is the background a silhouette moved over: accepted (rejected before 2026-09-12) |
| Sentinel history tap | 0.5 | A -1 previous depth under the sentinel policy is that background too: accepted (dropped before) |
| Current sentinel pixel, policy 1 / 2 / 2 in front | 0.25 / 0.5 / 0.25 | Current depth -1: policy 1 current-only; policy 2 reprojects at the far plane through the identity camera and accumulates over sentinel history; a valid history depth 0.5 in front of the far plane rejects (an object that moved off the background) |
| Variance clip | 0.439 | One 1.0 among eight 0.25 in the current 3x3: mean 1/3 + 1.25 sigma = 0.628 bounds the history 1 (the min/max box alone would give 0.625) |
| Catmull-Rom half-texel history | 0.4609 | Previous x=7..10 hold 0.25, 0.25, 1, 0.25 weighted (-1, 9, 9, -1)/16 = 0.671875; bilinear would give 0.625 (0.4375) |
| Closest-depth dilation / tie | 0.5625 / 0.25 | The right neighbor is nearer (0.25 against 0.5) and its correspondence says it came from x=10; the pixel applies that velocity and reads previous x=9 (0.875) instead of its own static correspondence (x=8, 0.25); equal depths keep the pixel's own correspondence |
| Out-of-bounds / behind-camera | 0.25 | Invalid projected UV/W rejects history |
| Object motion | 0.5625 | Explicit previous UV and object previous depth override camera path |
| Full precision object depth | 0.5625 | Expected/previous depth 0.5002 matches through RGBA32F; FP16 would quantize to 0.5 and reject |
| Invalid object sentinel / reserved state | 0.25 | Unknown correspondence and noncontract motion states reject history |
| Explicit static fallback | 0.5 | Motion state zero selects camera accumulation, distinct from rejecting history |
| Motion plus current jitter | 0.5625 | Object UV 9.25 is the content's previous unjittered UV; the current +0.25 pixel jitter is applied once (9.5); the previous jitter (-0.25, packed in c5.xy) is not (9.0 would give 0.40625, no jitter 0.484375) |
| Neighborhood clipping | 0.625 | History 8 clips to current neighborhood maximum 1 before blending |
| HDR preservation | 5 | Current 2, history 8, neighborhood up to 16; no saturation |
| Invalid history color | 0.25 | NaN old color cannot poison output |
| Invalid current color | 0 | Infinite current color falls back to finite black |
| Invalid current/previous depth | 0.25 | NaN depth prevents accumulation |
| Camera jitter through zoom | 0.5625 | Current -1 pixel jitter is removed before the x2 zoom reprojection and restored after it (8 -> 9 -> 10 -> 9); the previous +0.5 jitter is ignored (the old convention would sample 10.5, a flipped current sign 5, no jitter 8: all 0.25 against the single 0.875 texel at 9) |
| Static jittered coverage, frames 30/31 | 0.4526 / 0.5073 | GPU history reduces alternating raw 0/1 aliasing around analytic coverage 0.5; the values are the pure temporal average of the pixel's own samples (weight 0.9: 0.4536 / 0.5083 modeled, FP16-rounded per frame), proving the history tap stays on the pixel's own center instead of blending the opposite-phase neighbors |
| New epoch | 0.25 | Old ping-pong history cannot leak into a new scene epoch |

Each numeric check verifies all three color channels and alpha one. Ordinary
tolerance is 0.002, HDR tolerance 0.01. The coverage test compares the last two
frames against the CPU model of the pixel's own exponential average (tolerance
0.02; measured 0.4526 / 0.5073 against 0.4536 / 0.5083); both lie within 0.055
of the analytic coverage 0.5 versus a raw deviation of 0.5. This is evidence of
actual temporal accumulation of subpixel-jittered samples, not just a shader
compile or single-frame spatial filter. It is not a universal TAA-quality metric.

The coverage sequence uses a static binary stripe signal with boundaries passing
through raster sample centers. Alternating ±0.25-pixel raster jitter yields raw
0/1 values. Thirty-two resolves ping-pong real GPU history textures with history
weight 0.9 (the route default); CPU code supplies no filtered replacement
history. Analytic pixel coverage is 0.5; the exponential average of an
alternating signal ripples by (1-w)/(1+w) = 0.053 around it. Before the
2026-09-12 convention fix the same sequence (16 frames, weight 0.875) measured
0.4375 / 0.5625: the previous-jitter lookup blended the opposite-phase
neighbors into every frame, which looked like faster convergence but was the
blur the gameplay defect showed. Camera/motion cases use separately
hand-specified expected results rather than running a CPU copy of the shader
as the oracle.

CPU checks independently exercise FP16 rounding carry (values just below 2 and
8) and nearest-even tie behavior. They also reject NaN weight and invalidate on
resize, explicit reset and
new scene epoch. The fixture unbinds/releases all default-pool objects, executes
actual D3D9 Reset, recreates shader resources and repeats every numeric case.

## TemporalPass route-input cases (step 2)

The production runtime fixture (`verification/probe/temporal_pass_fixture.cpp`,
runner `run_temporal_pass.py`) now passes **318 numerical checks and 164
complete state comparisons** (292 `SAMPLE` lines) across two device
generations (174 / 162 / 162 before the 2026-09-12 resolve quality pass, whose
scenes are described in "Resolve quality" below); the earlier 98/102
regression cases are unchanged inside it except for the jitter cases
re-derived below and the sentinel history tap. The step-2 cases exercise the
inputs the live route provides at the pre-bloom copy point, on the same hidden
16x16 pure device:

| Case | Result | What it establishes |
| --- | --- | --- |
| 8-bit main surface copy | all 768 RGB samples within one FP16 ulp of `v/255`, alpha one | A plain `CreateRenderTarget` A8R8G8B8 surface (not a texture level) filled with every 8-bit code is copied by `StretchRect` into the FP16 scratch under hostile sRGB sampler/write states; the backend truncates (421 of 768 also match round-to-nearest); max absolute error 0.000486 < 2^-11; no gamma curve |
| Copy path equals FP16 path | bit-exact color and depth, first frame and accumulation | An FP16 texture holding the detected conversion of the same bytes resolves to identical bits through a second `TemporalPass` instance |
| Copy-back round trip | exact bytes | `StretchRect` of `Output::color_surface` back into the 8-bit target restores the original 256 pixels exactly; `color_surface` is level 0 of `Output::color` |
| Input exclusivity | `E_INVALIDARG` | Both or neither color inputs, both or neither depth inputs, resolved surface or owned depth history as input, wrong depth format |
| R32F depth equals decoded D24X8 | bit-exact color; depth max error 2.98e-8 (half a D24 step) | One synthetic scene (clear 0.5, quad 0.25 shrinking between frames) rasterized into the D24X8 snapshot and modeled into R32F; outside/inside stable regions blend to 0.5, the uncovered region rejects to 0.25 |
| Depth-sentinel reactive | 0.25 / -1 / 0.5 / 0.375 / 0.375 | Sentinel pixel resolves current-only and its -1 reaches the depth history; the opaque neighbor accumulates; a motion correspondence onto a previous sentinel tap is accepted as the background behind a silhouette (re-derived 2026-09-12: one flat 0.5 frame gives the sentinel pixel a current-only history of 0.5, the next frame's current 0.25 blends to 0.375; the dropped tap of the previous shader gave 0.25); a correspondence onto opaque history keeps accumulating; the policy establishes history without a mask; it refuses the D24X8 input; transitions to and from it invalidate |
| Route cut | current-only, then resumes | `cut=true` rejects history for that frame only |
| Jitter on both paths | 0.375 / 0.5 / 0.375 / 0.5 / 0.375 / 0.375 | History 0.5/0.75 against current 0.25/1, so own position (0.375), neighbor (0.5) and rejection (0.25) differ. Motion path: previous jitter +1 with RG at the own center stays put; current jitter +1 selects the neighbor; RG = own center minus the current jitter (what the producer emits for static content) lands on the own center; RG at the neighbor with previous jitter +1 selects the neighbor exactly. Camera path (identity): current jitter +1 lands on the own center (removed before and restored after the reprojection); previous jitter +1 is ignored. The `cases()` regression `camera path ignores previous jitter` (0.375) replaces the former `jitter routing` (0.625) |
| Reset continuity | history rebuilt without `initialize` | One pass accumulates, `before_reset` pends and refuses runs, `after_reset(E_FAIL)` keeps refusing, a real device `Reset` then `after_reset(S_OK)` resumes with invalid history and accumulates again |
| Stationary stability | 10 metrics, see below | A static 32x32 scene over four 16-phase Halton periods: stable across phases, no drift, edges converge |
| Thin lines, silhouette, resampling blur | 73 metrics, see "Resolve quality" | Jittered thin features and a silhouette against a far or sentinel background converge; a moving square leaves no ghost; Catmull-Rom against the previous bilinear history filter |

Evidence: [report](../../verification/results/temporal-pass.txt) and
[summary](../../verification/results/temporal-pass-summary.json). The
conversion-rule detection is printed as `COPY rule=...`; the D24 comparison
as `DEPTH decoded_vs_r32f_max_error=...`; the stationary metrics as
`STATIONARY ...` and `EDGE ...`.

### Stationary stability (jitter convention, 2026-09-12)

Gameplay with `X3M_TAA=1` showed stationary objects trembling and blurring.
The resolve read history at the previous position **plus the previous raster
jitter**, which is right only when the history is a raw one-frame-old jittered
rendering; the live history is the accumulated output, which converges to the
unjittered grid, so every lookup landed up to a pixel off, alternating with
the Halton sequence. The producer (`rigid_motion_ps.hlsl`, interpolating the
previous unjittered rows across the current jittered raster) reports for the
sample at pixel `p` the content's previous unjittered UV, which for static
content is `p - current jitter`; reading history at that UV verbatim is still
off by the current jitter. Standard TAA reads history at "pixel center minus
velocity" = content's previous UV **plus the current jitter**, which is `p`
for a static scene. The corrected shader does exactly that on both paths
(camera: subtract the current jitter before the inverse projection, add it
back after; motion: RG plus the current jitter); the previous jitter stays
packed in `c5.xy` and is never read.

`stationary_cases` renders a 32x32 scene (flat 0.25 backdrop; a bright
rectangle with fractional edges at x in [4.28, 12.72), y in [4.37, 12.59); an
integer-aligned 3x3 bright square at [20, 23) x [6, 9); a bilinear-sampled
64-texel horizontal ramp on a 24x10 quad), all on one surface at depth 0.5 so
depth rejection never fires, rasterized with pre-transformed vertices
displaced by the Halton (2, 3) jitter of frame `n` (`index n % 16 + 1`, the
route's sequence), 64 frames, weight 0.9, `PerPixel` motion with the
producer's stationary correspondence (`RG = (p + 0.5 - j)/32`, B = 0.5,
A = 1) and `KnownNonReactive`. Every frame's current image and resolved
output are read back. Metrics (both device generations identical):

| Metric | Measured | Bound | Meaning |
| --- | --- | --- | --- |
| One-step oracle, max error, 64 frames, all pixels | 0.000488 | 0.002 | `FP16(lerp(current, clip3x3(previous output), 0.9))` with the history tap at the pixel's own center and the clip = mean +/- 1.25 sigma of the 3x3 within its min/max (the min/max clamp alone before 2026-09-12); one FP16 ulp |
| Interior delta between consecutive phases (last period) | 0 | 1/255 | Flat regions and the integer-aligned square are bit-identical across all 16 phases |
| Fractional-edge delta between consecutive phases | 0.0825 (0.0591 with the min/max clamp) | 0.102 | The raw sample toggles, so the average moves by at most (1-w) per frame; the variance clip adds a clamp step at the corner pixel |
| Bilinear-ramp delta between consecutive phases | 0.0044 | 0.01 | The jitter shifts the sampled texture by up to 0.94 px (0.02 in value) |
| Edge coverage: period mean of the output vs period mean of the raw samples (left/right/top/bottom) | 0.0040 | 0.01 | DC gain one: the resolve converges to the jitter-sampled coverage (supersampling) |
| Corner coverage: period mean of the output vs raw samples | 0.0128 (0.0040 before) | 0.02 | Re-derived 2026-09-12: on the phases where only the corner is covered its 3x3 holds one bright sample among eight dark, so the variance box (mean + 1.25 sigma = 0.628) trims the history; a deliberate bias of the clip the linear DC-gain argument does not cover |
| Edge coverage vs analytic coverage (left/right/top/bottom/corner) | 0.0551 | 0.1 | 0.748/0.685/0.685/0.559/0.487 against 0.72/0.72/0.63/0.59/0.454; the 16-phase estimate is quantized to 1/16 and biased by the Halton set's mean offset |
| Ramp period mean vs supersampled texture mean | 0.0064 | 0.01 | Texture detail converges to its jitter-averaged value |
| Square centroid drift, max over 64 frames | 0.000000 px | 0.05 px | The 3x3 square's centroid (backdrop subtracted, 9x9 window) stays at (21, 7) |
| Ramp gradient energy of the period-mean output over the period-mean input | 0.9947 | >= 0.9 | No resampling blur on static content: every history tap sits on a texel center, so this ratio is one for any history filter (bilinear before, Catmull-Rom now) |

Negative proof: the same scene run with the previous shader (git b9a1094,
`stationary-only` mode) fails the first metric with oracle error 0.421,
interior delta 0.307, edge delta 0.370, ramp delta 0.181, edge DC error
0.151, ramp DC error 0.252 and centroid drift 0.464 px:
[report](../../verification/results/temporal-stationary-regression.txt),
[hashes](../../verification/results/temporal-stationary-regression.json).
`run_temporal_pass.py` repeats that proof automatically after the passing
run: it mutates the two history lookup lines of `resolve.hlsl` in a temporary
copy (never the tree) into the three plausible wrong conventions, `previous
jitter` (`+ history.xy` on both paths, the b9a1094 shader), `flipped sign`
(`- sizeJitter.zw`) and `no jitter` (the producer UV verbatim), runs the
stationary-only mode on each and requires exit code 1, the oracle metric as
the failing check and an oracle error above 0.1
(`temporal-stationary-negative-<variant>.txt`, hashes and numbers in
`temporal-pass-summary.json` under `negative_controls`; with the 2026-09-12
shader the three controls fail with oracle errors 0.423 / 0.454 / 0.287 and
centroid drifts 0.49 / 0.91 / 0.56 px). The detached
`temporal_resolve.cpp` cases are sign-sensitive as well: the motion case
distinguishes current (0.5625), previous (0.40625), flipped (0.40625) and no
jitter (0.484375), and the zoom case (0.5625) reads 0.25 under every wrong
convention.

The motion-output seam script cannot host a stationary check (its objects
alternate poses every frame and at most two consecutive frames keep history);
its reference comparison shares the production bytecode and follows the
resolve bit for bit.

### Resolve quality (2026-09-12)

Gameplay after the convention fix still showed object edges trembling and
thin distant geometry and emissive lines shimmering with the camera still.
The absolute 1e-4 depth test rejected the history of every edge pixel whose
jittered coverage flipped (the history depth at a silhouette belongs to the
other surface), and sentinel current pixels were current-only, so edges and
thin features never accumulated. The shader now uses depth only as a
one-sided disocclusion test against the closest depth of the current 3x3
(history in front rejects; history behind or sentinel is the background
behind a silhouette and is accepted), clips the history to mean +/- 1.25
sigma of the current 3x3 within its min/max box, takes the correspondence of
the closest-depth pixel of the 3x3 (dilated velocity), reads the history
with a 16-tap Catmull-Rom filter (one tap on the grid), and has a sentinel
policy 2 that reprojects sentinel pixels at the far plane for a route that
supplies a camera matrix (the route keeps policy 1, current-only, today);
see the [algorithm section](../../src/temporal/README.md#algorithm-resolve-quality-pass-2026-09-12).

`edge_cases` rasterizes color, RGBA32F motion (from `VPOS`, the producer
contract, checked at every covered pixel of every scene's first frame) and
R32F depth per frame with the route's Halton jitter on a 32x32 scene, over a
0.25 background that is either a routed far surface (depth 0.9, static
correspondence) or the -1 sentinel with motion fill alpha -1 (the route
today) or 0 (a route with a camera path); every jittered edge stays 0.03 px
from the sample centers. Three modes are run: `far-background` (policy 1,
asserted), `sentinel-camera` (policy 2 with the identity camera, asserted)
and `sentinel-current-only` (policy 1 over the sentinel, reported only: the
route's present configuration). Weight 0.9, `PerPixel` motion,
`DerivedFromDepthSentinel`. Numbers are identical in both device
generations and in the two asserted modes.

Thin lines (64 frames; a 1-px line at y in [6.37, 7.37), a 2-px line at
[14.59, 16.59), a 1-px vertical line at x in [12.28, 13.28); metrics over
the last period, 20 pixels along each line):

| Metric | 1 px | 2 px | 1 px vertical | Bound |
| --- | --- | --- | --- | --- |
| Interior delta between phases (rows never or always covered) | 0 | 0 | 0 | 1/255 |
| Edge delta between phases (toggling rows) | 0.0557 | 0.0474 | 0.0593 | (1-w)*0.75 + 0.002 = 0.077 |
| Cross-section brightness delta between phases (sum over the rows) | 0.000244 | 0 | 0.000244 | 1/255 |
| Centroid drift between consecutive periods | 0.0035 px | 0.0067 px | 0.0030 px | 0.05 px |
| Centroid wobble across phases (EMA ripple of the toggling rows) | 0.052 px | 0.072 px | 0.041 px | 0.1 px |
| Centroid vs analytic center | 6.811 vs 6.87 | 15.562 vs 15.59 | 12.749 vs 12.78 | 0.1 px |
| Total coverage / width | 0.995 | 0.997 | 0.996 | within 20% |
| Per-row coverage vs analytic | 0.060 | 0.032 | 0.032 | 0.1 |

In `sentinel-current-only` the same lines keep 14% / 60% / 17% of their
coverage (edge delta 0.19-0.20, cross-section delta 0.06-0.13, wobble up to
0.75 px): the uncovered phases wipe the history, which is the measured
shimmer of the route's present sentinel handling.

Silhouette (a 6x6 square at [10.28, 16.28) x [10.37, 16.37), depth 0.5,
static for 32 frames then moving +1 px/frame for 12; the ring is the 24
pixels of fractional coverage):

| Metric | far-background / sentinel-camera | sentinel-current-only (reported) | Bound |
| --- | --- | --- | --- |
| Ring variance across phases, raw / resolved | 0.1095 / 0.000614, ratio 178 | 0.1095 / 0.0044, ratio 25 | >= 4 |
| Ring coverage vs jitter-sampled coverage | 0.033 | 0.598 | 0.05 (variance-clip bias) |
| Ring coverage vs analytic | 0.084 | 0.568 | 0.1 |
| Interior delta between phases | 0 | 0 | 1/255 |
| Revealed background (no square pixel in the current 3x3) vs 0.25, moving phase | 0 | 0 | 1/255 |
| Moving interior minimum | 1.0 | 1.0 | >= 0.9 |

With the cross instead of the 3x3 for the dilation the four corner pixels
lost half their coverage (0.257 against 0.500 sampled at (10, 10)): on the
phases that uncover a corner on both axes its only foreground neighbor is
the diagonal one, and the cross made the corner reject its own foreground
history. That measurement is why the shader uses the 3x3.

Resampling blur (a static 24x8 quad whose period-8 sinusoid, 0.25..1,
scrolls 0.25 px/frame with matching motion, so the history is resampled
at a constant 0.75-texel offset every frame; amplitude of the period-8
component over 16 interior columns and 6 rows, last period):

| Filter | Amplitude ratio output/input | Energy ratio |
| --- | --- | --- |
| Catmull-Rom, shader | 0.930 | 0.864 |
| Catmull-Rom, CPU model of the resolve | 0.932 | 0.868 |
| Bilinear, CPU model of the previous resolve | 0.712 | 0.507 |

The shader matches its CPU model within 0.0034 over 64 frames (bound 0.01),
keeps at least 0.9 of the input amplitude (bound) and 0.22 more than the
bilinear model (bound 0.1). On the stationary ramp both filters keep 0.9947
of the gradient energy (no fractional lookup). Under a continuous
fractional velocity the retained energy depends on the frequency and
velocity; the 0.9 target of the quality pass holds for the amplitude, not
the energy, of this period-8 pattern.

Re-derived expectations in this suite: the sentinel history tap (0.25 to
0.375, table above), the corner DC metric (split from the edge metric, 0.02
bound) and the fractional-edge delta (0.0825 within the unchanged 0.102
bound); the stationary oracle models the variance clip. The runner's expected
counts moved from 174 / 162 / 162 to 318 / 164 / 292 (the sentinel flat
frame adds one state comparison per generation).

Step 3 changed the pass without changing these counts: every device call goes
through numbered vtable slots (the route hands it the original table; this
fixture passes none, so its vtable-swapping fault injection still reaches the
pass), one `D3DSBT_ALL` state block is created per device generation and
captured/applied per run instead of created per run, the decoder bytecode is
optional, and the resolve's output alpha is the current color's alpha (this
fixture's inputs carry alpha one, so its samples are unchanged). The suite was
rerun after these changes with the same 154/158 result; the route-side
evidence is in [motion output](motion-output.md#temporal-resolve-step-3).
Review 16 added stage 0's `D3DTSS_TEXCOORDINDEX = 0` and
`D3DTSS_TEXTURETRANSFORMFLAGS = D3DTTFF_DISABLE` to `normalize`: the fixture's
hostile state now sets a texture transform (scale 0.5, offset 0.25) with
`D3DTTFF_COUNT2` and coordinate index 1 on stage 0 before every run, and
without the reset the Preview backend applied them to the pre-transformed
resolve quad (`matrix routing` sampled 0.25 instead of 0.625); the counts
are unchanged at 154/158 and the state comparison (texture stage states of
stages 0-7 and `D3DTS_TEXTURE0`) proves the hostile values come back.

### Camera reprojection (sentinel policy 2, 2026-09-12)

The route now builds the resolve's `clip_to_previous` from the live engine
camera and runs sentinel pixels under policy 2
([temporal-integration.md](../architecture/temporal-integration.md#camera-reprojection-for-sentinel-pixels-2026-09-12)).
`run_temporal_pass.py` covers the resolve side; the builder itself is
unit-tested on the host (`verification/analysis/test_camera_reprojection.py`,
9 tests: the header compiled natively, identity to identity, yaw/pitch/roll
and FOV/off-center changes against an oracle written from camera basis
vectors, translation ignored, a direction behind the previous camera has
`w <= 0`, every validation failure code, the switch and the rotation cut).

**Shader fix and its fixture.** Policy 2 expected motion alpha 0 on
far-plane pixels; the route's fill writes -1. The resolve now keeps the
camera path for a far-plane pixel that is its own correspondence (no closer
neighbor won the dilation) with alpha exactly -1. The edge cases gained the
mode `sentinel-camera-fill` (background depth -1, alpha -1, policy 2): its
thin-line and silhouette metrics equal the alpha-0 mode's to the printed
digits (1-px line interior delta 0, centroid drift 0.003 px, coverage error
0.03; silhouette ring variance ratio 178.32, ghost 0, revealed background
clean). Bytecode 3,840 words (3,794 before).

**Camera cases** (`camera_cases`, per generation). A sky at infinity is
rendered from a camera state by the fixture's own shader — raster pixel `p`
at NDC `2(p - j)/S - 1`, the resolve's raster convention, through the same
row-vector, left-handed convention as the builder, coloured by a smooth
angular pattern (12 cycles per radian: period 8.4 px at the centre of the
90-degree view) — with the route's sentinel ABI (RT2 -1, RT1 alpha -1), and
the resolve runs 48 frames under the route's Halton jitter with
`camera_sentinel_policy(auto)` deciding policy and matrix from consecutive
`(P, V)` pairs. Drift is the sub-pixel shift of the accumulated output
against the unjittered render of the same camera, from the phase of the
pattern's fundamental (a least-squares fit against a bilinearly shifted
reference matched the resampling blur of a moving history with a fractional
offset and reported 0.5 px on a correct run); the recency-weighted mean of
the Halton set (about -0.03/-0.04 px, phase dependent) is removed by
subtracting the same-frame shift of a static control run. Measured (both
generations identical):

| case | drift px | mean abs error | note |
| --- | ---: | ---: | --- |
| static control (jittered, identity transform) | 0.069 raw | 0.0075 | the jitter set's own bias; bound 0.15 |
| yaw 0.5 deg/frame, jittered | 0.124 | 0.015 | against the static control; bound 0.2 |
| pitch 0.5 deg/frame, jittered | 0.176 | 0.016 | bound 0.2 |
| yaw unjittered, 90 deg view | 0.062 | 0.006 | bound 0.1: the resampling of the accumulated history plus the perspective chirp |
| yaw unjittered, 28 deg view (pattern uniform in pixels) | 0.026 | 0.008 | bound 0.05 |
| five chained reprojections of one render (weight 1), yaw / pitch | 0.033 / 0.029 | 0.004 / 0.003 | bound 0.05: 0.007 px per lookup |
| identity matrix under policy 2 (the route before the camera read) | 0.862 | 0.076 | must be at least 0.5: the crawl this work removes |
| swapped rotation convention | 1.051 | 0.087 | must be at least 0.5 |
| 25-degree jump at frame 16 | cut, policy 1, frame equals its render exactly (max difference 0) | | history resumes: 0.100 after the cut, bound 0.2 |

The per-lookup error does not scale with the rotation (0.5, 1 and 2
degrees per frame gave 0.062, 0.055 and 0.049 px accumulated) and flips
sign with the fractional pixel velocity: it is the Catmull-Rom resampling
of the sampled pattern, not a reprojection bias. The reported rotation per
frame is 0.4998 degrees (bound 1e-3) and 25.5 degrees at the jump. Totals
after the additions: 416 numerical / 164 state checks, 386 samples per
run, negative controls of the jitter convention intact
(`run_temporal_pass.py` asserts the exact counts and records the camera
metrics under `camera` in `temporal-pass-summary.json`).

### Stage 3 of the HDR scene path: luminance weighting (2026-09-12)

`resolve.hlsl` gained the reversible luminance weighting of
[hdr-scene-path.md](../architecture/hdr-scene-path.md) §3 (`c22.x = k`;
mechanism and the derivation of `k` in
[temporal-integration.md](../architecture/temporal-integration.md#stage-3-of-the-hdr-scene-path-taa-on-hdr-2026-09-12)).
The compiled resolve grew from 3,840 to 4,487 words (4,375 before review
24's luma floor).

**Migration identity (k = 0), before any case was added.** With the
weighting compiled in and every input at its default `k = 0`, the suite
reproduced review 23's record exactly: 416 numerical / 164 state checks,
386 samples, 2 generations, the camera drifts 0.0686 / 0.1243 / 0.1764 /
0.0619 / 0.0263 / 0.0334 / 0.0292 / 0.8621 / 1.0506 / 0.1004 px and the
negative controls (oracle errors 0.423 / 0.454 / 0.287). `temporal_run.py`
(which uploads c22 = 0 explicitly since the change) passed 78 / 78 samples,
2 generations. The tracked reports `verification/results/temporal-pass.txt`
and `temporal-resolve.txt` were **byte-identical** to the committed ones
(`git diff` empty), which is the byte-for-byte evidence: the identity is
selected by a compare that multiplies by the constant 1.0, never by
`1/(1+0)`.

**k > 0 cases** (`hdr_cases`, per generation; the FP16 texture input, weight
0.9, R32F depth 0.5 everywhere, identity camera, no jitter; the CPU model
of one grey pixel applies the weighting, the 3×3 mean ± 1.25σ clip inside
the min/max box, the blend and the inverse):

| Case | k = 0 | k = 1 | k = 4 |
| --- | ---: | ---: | ---: |
| Firefly 8.0 against a 0.2 (FP16 0.199951) history: output | 0.979492 (model 0.979956) | 0.313721 (model 0.313816) | 0.246826 (model 0.246934) |
| flicker energy (output − background) | 0.7795 | 0.1138 | 0.0469 |
| energy ratio to unweighted, measured / analytic | 1 | 0.1459 / 0.1460 | 0.0601 / 0.0602 |
| dark neighbour of the firefly | 0.199951 | 0.199951 | 0.199951 |
| Stationary HDR gradient (2⁻⁸…2^7.6, RGB 1 : ½ : ¼), second frame vs input, worst FP16 ulp | 0 | 1 | 1 |
| Stable saturated edge (4, .1, .1) \| (.1, .1, 4), worst ulp / min channel ratio (input 40) | — | 1 / 39.98 | 1 / 39.98 |
| Negative channels (review 24): 5×5 blocks of (−.5, −.5, −.5) and (−.5, 1, 0) among 0.2 greys, second frame vs input, worst ulp / all finite | — | k = 2: 1 / yes | 1 / yes |

Reading: at k = 0 the clip window of the firefly pixel (mean ± 1.25σ =
[0.2, 4.13], within the [0.2, 8] box) admits the dark history, so the
unweighted blend keeps 0.1 · 8 + 0.9 · 0.2 = 0.98 — a 0.78 flash of
energy at the next frame. Weighted, the same pixel enters the statistics
as 8/9 (k = 1) and leaves the blend at 0.314: the weighting suppresses the
flash by the analytic factor to 4 significant digits; the model differs
from the FP16 output by the output rounding (≤ 5e-4). The stationary
gradient and the saturated edge (its edge columns include neighbourhoods
spanning both sides) survive the weighting and its inverse within one FP16
ulp, exactly at k = 0, and the 40 : 1 channel ratio is preserved. Negative,
NaN and above-FP16 `k` are refused (`E_INVALIDARG`). The negative-channel
case (review 24, finding 1) exercises the luma floor: run against the
pre-fix shader, the same scene at k = 2 made `1 + k · luma` zero for the
−0.5 block and the block resolved to NaN (`finite=0`, worst 34,202 ulp; the
fixture stops at the first failure, so k = 4 — a weight of −1, then the
inverse's floor — was not reached); with the floor both blocks return within
one ulp at both k, every output finite. The blocks are 5×5 because the mean ± 1.25σ clip preserves
a stationary value only where at least four of its nine taps share it (a
block corner has exactly four); an isolated pixel is an outlier at any k,
0 included. Totals after the additions: **448 numerical / 204 state checks,
386 samples**, negative controls intact (`run_temporal_pass.py` asserts the
counts).

## Reproduce

```sh
python3 verification/probe/temporal_run.py
```

The runner freshly compiles the fixture before each execution, verifies source
hashes match before/after compilation and execution, and checks the executable
hash after the run. This prevents reporting current source hashes for a stale
binary. The build passes `-Wall -Wextra` without warnings. The runner has a 60-second
process timeout and records source and executable SHA-256, process command,
exit status, sample counts and reset result. Evidence:

- [Numeric results](../../verification/results/temporal-resolve.txt)
- [Metadata](../../verification/results/temporal-resolve-summary.json)
- [Wine diagnostics](../../verification/results/temporal-resolve-wine.log)

The backend reports NVIDIA GeForce 8800 GTX/nvd3dum.dll through Wine's adapter
interface; this is an emulated capability description, not identification of the
physical Mac GPU. The current fixture does not measure game performance. Depth
rejection uses configurable raw device-depth tolerance; distant geometry needs
real camera/depth validation before adopting defaults. Renderer integration must
supply accurate previous object correspondence, separate camera/depth epochs,
matching jittered inputs and safe resource/state ownership.

## Review regression: camera half-texel conversion

Independent review identified that the original shader used texture-center UV
directly in raw camera reprojection. Identity and translation fixtures canceled
the missing offset and passed. The expanded geometry fixture was deliberately
run against that original shader first: D3D9 rasterized optical-center history
near 0.5, but its zoomed reprojection resolved **0.382568 versus expected 0.375**,
exceeding tolerance 0.002 and failing the test. The corrected shader subtracts a
half texel before raw VP inversion and restores it after prior projection.
Additional x2 zoom and 90-degree roll tests keep raster pixel (8,8) fixed as the
raw optical center. The separate object-motion input contract already contains
texture-center UV, so it adds only prior raster jitter.

[Deliberately failing regression output](../../verification/results/temporal-viewport-regression.txt)
and [source/executable hashes](../../verification/results/temporal-viewport-regression.json)
preserve this negative proof. Final passing results use the corrected source.

## Run57 global heuristic cuts disabled by default (2026-09-21)

Run203/204 normal-speed recordings linked all five inspected station-flash peaks
to discarded global history: four median-motion cuts and one missing-key cut.
Missing-key-only disablement left median cuts and visible flashes. No gain
apply/restore failure was found at those peaks; raw light-map stability is not
therefore proven universally.

Run205 disables both heuristics. The user reports the flash fixed and no smear,
old scenery or ghost trails during save loading and sector travel, and explicitly
accepts both disabled by default. Streaming analysis finds 26,371 contiguous
frames, zero heuristic cuts despite 142 frames exceeding the old bounds, and
24,629 successful resolves out of 24,629 attempts. Apply/restore failures: zero.
Nine resolved frames omit history: initial scene, four chase snaps and four
large camera rotations. Save loading is logged; sector travel is user-observed,
not proof that all transition epochs fired. Native Windows runtime remains
unverified.

Launcher defaults are now median `1e30`, missing fraction `1`, preserving
explicit inherited diagnostic overrides. Native fallback initializers match for
future builds; no predicate, shader, hook, camera or recovery policy changed.
This adds no per-draw work. The installed DLL is unchanged and already supports
these environment values. Focused launcher/default checks cover unset values,
overrides and retained camera/sentinel defaults; no new candidate qualification
or Wine fixture is needed for this default-only change.

[Compact counts](../../verification/results/run57-heuristic-cuts/summary.json),
[analysis](../../verification/results/run57-heuristic-cuts/summary.md) and
[streaming reproducer](../../verification/results/run57-heuristic-cuts/summarize.py).

Independent review: no findings; **14 focused host tests passed**. The affected
launch dry-run with both cut variables unset confirms `1e30/1`, camera `20`,
and TAA enabled. No game was launched. [Review record](../../verification/results/run57-heuristic-cuts/review.json).

## Camera-relative thin-region gate with 7x7 box clip (2026-09-21)

Opt-in `--taa-thin-region-gate camera` (`X3M_TAA_THIN_REGION_GATE`); default unchanged. Design and numbers:
[lattice note section 32.1](../architecture/taa-lattice-crawl.md). Unflown; no candidate built or installed.

- Shaders (native `d3dx9_37` through `generate_rigid_motion_pixel.py` under the Wine lock): `temporal_line_mask_camera`
  897 words / 223 slots (bytecode `e681518b…`), `temporal_resolve_far_camera` 2023 words / 508 slots (`6f2228f4…`), `temporal_thin_box` 238 words / 51
  slots (`e2c80743…`). The eleven existing programs sharing the edited sources (ten resolve variants, the line mask)
  regenerate from the final sources to unchanged headers, word counts and `bytecode_sha256`; their manifests differ
  from HEAD only in `source_sha256` / `includes` / `tool_sources`, and every recorded hash matches the tree.
- Fixture `temporal_pass_fixture.exe lattice` (bottle X3), exit 0: `RESULT PASS numerical=495 state_restorations=21`
  (was 459 / 19; `FAR_BASE` 382 / 17 unchanged). Installed mode: 19 of 19 thin-region metric rows identical to the
  previous record. Camera gate with a static camera: bit-identical to the screen gate at rest and at 0.12 / 0.30
  px/frame (colour, alpha, age, gate). Pan 0.5 px/frame: gate share 0.000 -> 1.000, shard rms 16.34 -> 1.42 codes
  (x 0.087), oracle error 0.0093 (bound 0.02). Stale patch one frame after injection: installed 0.623, camera 0.727
  (x 1.17; bound 2 x), clip-off estimate 3.64. Box-allocation failure: screen gate bit for bit. Box domain (k = 0.5,
  one 65504 tap): oracle error 0.0103. Non-finite speed by overflow: both gates closed within 8 px, output = screen
  gate. NaN correspondence: reported only; this backend reads it open in the plain and the camera mask alike.
- Pass time 1280x768 (the record): thin region +0.78 ms over plain; camera gate +0.16 ms without a region, +0.59 ms
  with the whole frame reopened. Session spread: +0.47 .. +0.98, -0.05 .. +0.26 and +0.37 .. +0.65 ms.
- Host: `test_taa_image_defaults` 12 tests OK; full `run_temporal_pass.py` exit 0, `passed: true` (new `test_thin_region_gate_is_absent_unless_given`).
- Limits: the pan scene is uniform along the pan axis (no resampling loss); moving-lattice quality is section 32's
  replay, not this fixture. Native Windows unverified. Full runner 112 s, lock wait 0 s.


## Run59 accepts the camera thin-region gate as the default (2026-09-21)

Run 59 flew the A/B on DLL `b1bb05fb` at the lattice position: run207 with
`--taa-thin-region-gate screen`, run208 with `camera`, otherwise the same command.
The user accepts the camera gate ("I think it's fixed") for camera pans. Triage
of run208 found no anomaly: **0 non-finite texels**, gate-open share **0.44 % ->
9.3 %**, tracked rms **x 0.874** and gradient **x 0.913** measured on the same
capture. The roll residual (crawl under camera roll) was not measured in this run
and stays open.

The gate therefore becomes the default whenever the thin region is active, in the
launcher and in the DLL's native fallback. `--taa-thin-region-gate screen` is the
opt-out and remains bit-identical to the pre-Run59 behaviour. With
`--taa-line-filter` active the default resolves to `screen` silently, because the
mask's line channel carries the only gate that mode can have; an explicit
`camera` plus the line filter is still refused by the launcher, and the route's
own fallbacks are unchanged (configure-time refusal, box-allocation failure ->
screen gate bit for bit). Nothing changes without the thin region: no gate
variable is emitted and the native default is untouched.

Checks for this default-only change: `test_taa*` **25 host tests OK** (the gate
test now pins default -> camera, explicit screen, line filter -> screen, explicit
camera + line filter refused, and no thin region -> no variable, plus the native
fallback site and its ordering after the thin-region and line-filter parses);
`src/proxy/capture.cpp` cross-compiles clean under
`i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -msse2 -mfpmath=sse
-mstackrealign -mincoming-stack-boundary=2`. No shader, predicate, hook or
recovery policy changed and no per-draw work was added, so the fixture and
shader evidence of the section above carries over unchanged. No game launched, no
Wine run, no new candidate. Native Windows runtime remains unverified.

Note 2026-09-21, user tuning on the installed Run59 DLL (no capture, by eye): with
the camera gate on, panels and arms blur while the camera moves (the expected
resampling cost of W 0.97 with the clip off). Of `0.97,0.5`, `0.94,0.5` and
`0.94,1` the user tentatively prefers **`--taa-thin-region 0.94,1`**, with no
crawl at rest. They will retest after the depth-aware camera path lands; the
0.97 default is unchanged until then.

## Static-world previous rows for unmatched draws (2026-09-21)

`--taa-unmatched-static node|all` (default off), design and run209 diagnosis in
`docs/architecture/temporal-integration.md`, "Unmatched draws: static-world
previous rows". Worktree of main `184843cd`, uncommitted; fresh CMake build
(`-DPython3_EXECUTABLE=/usr/bin/python3`), DLL `5f4a1010`, seam DLL `8970a9e9` (after the review follow-up below: see its hashes),
fixture exe `17355a8a`. Bottle X3, arm64, `FEX_X87REDUCEDPRECISION=1`,
`WINEMSYNC=1`.

Host: `test_static_previous_rows` **6 OK** (the header against a basis-vector
oracle: rotation + translation + per-draw depth law, off-centre projection terms,
run209-scale coordinates 0.05 px bound at 1280x720, identity, three refusals;
`classify_miss` over absent/present-object/new-object/reused-pointer/poisoned/
consumed/invalid keys) and `test_taa_image_defaults` **14 OK** (launcher forwards
only when given, drops an inherited value, requires `--taa`; native default off).

Fixture: new `unmatchedstatic` script of `motion_output_fixture` (world-placed
bodies under a translating, yawing camera; oracle composed from the script's own
matrices) through `run_motion_output.py` in consume-only mode, cases
`seam-taa-unmatched-static-{unset,off,node,all}`, **95 checks each, all pass**,
lock wait 0 s, 12.6 s for the four. Frame 3 changes body S's key by the LOD word
alone (same node and serial); frame 6 introduces a new node N.

| case | S at frame 3 | N at frame 6 | motion/depth hashes |
| --- | --- | --- | --- |
| unset | sentinel, 0 of 221 px changed by the resolve | sentinel, 0 of 82 | reference |
| off (`0`) | same | same | identical to unset on all 9 frames, presented frames identical |
| node | camera-path motion, max 0.00066 px / 1.7e-7 depth, 100 of 221 px changed by the history blend | sentinel, 0 of 82 | differs from off in frame 3 only |
| all | as node | camera-path motion, max 0.0011 px, 82 of 82 | differs from node in frame 6 only |

In every case the DLL's frame lines keep `matched`/`gate6` as a miss (frame 3:
routed 2, matched 1, gate6 1) and the resolved image equals the reference resolve
byte for byte on all 9 frames. Compact records:
`verification/results/bottle-X3/seam-taa-unmatched-static-*-fixture.json`; copies
and timings in `/tmp/x3-taa-unmatched-static-v1/`.

Not run: the existing `seam-taa-camera-on` regression case (the Wine lock was held
by the user's game session afterwards). The option-off path is covered by the
unset/off twin only. No flight yet; native Windows runtime unverified.

Review follow-up (same day): `classify_miss` mirrors the lookup's collecting and
overflow guards and uses one search; verdict and rows share one latch snapshot; the
per-frame line is limited to applied frames (cap 256) and the detail line carries
the projection check. Limits on record: a camera translation jump without a
rotation cut is not gated; for static geometry the reprojection stays exact, but
disocclusion under such a jump is untested and matters mainly for `all`. The
fixture proves the arithmetic through the DLL at about 0.5-1 px of reprojection per
frame; run209-scale motion (250 units per frame at 4e4-unit coordinates) rests on
the host oracle test (0.05 px bound), not on a fixture. Private-projection draws
are a flight-verification item (architecture note).
Rerun after the follow-up: the four seam cases pass again, 95 checks each, same numbers; DLL `9aef914e`, seam DLL `e974190c`, fixture exe `544022e5`; the detail lines report `projection_x/y = 1.00000` for both fixture bodies.

## Camera gate made depth- and translation-aware: c8 of the camera mask (2026-09-21)

Design and numbers: `docs/architecture/taa-lattice-crawl.md` section 32.3. Not committed as a
candidate, not installed, no game launched. Bottle X3.

- Shader flow: `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3
  tools/shaders/generate_rigid_motion_pixel.py --shader temporal_line_mask_camera` -> PASS, 920
  words / 226 slots (was 897 / 223), bytecode `cf7c1764095d...`; `--shader temporal_line_mask` ->
  PASS, bytecode `a44bfebd9767...` unchanged (manifest source hash only). sha256 of all 61
  `src/renderer/*_inc.h` before/after: only `temporal_line_mask_camera_program_inc.h` differs.
- Fixture: `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3
  verification/probe/run_temporal_pass.py` -> passed, lattice mode `RESULT PASS numerical=501
  state_restorations=21` (495 + the 6 forward-flight checks); main mode 508 / 278 / 386 unchanged.
  `THIN_REGION_CAMERA_FORWARD`: speeds 0.600 / 0.300 px/frame, c8-form residual 0.000213 /
  0.000107 px, routed-row residual 0.005840 / 0.002921 px, screen share 0, rotation-only share 0
  (output = screen gate bit for bit), camera share 1.0000 / 1.0000, rms 16.340 -> 1.508 codes
  (x0.0923), p2p 99.4 -> 7.8, mover gate maximum within 8 px 0.0000, window minimum 1.0000.
  Pan (x0.0866, share 1.0), at-rest identity (3/3), overflow and stale-patch rows equal the
  previous summary. `LINE_TIMING_CAMERA` deltas 0.1556 / 0.5758 ms (before 0.1640 / 0.5915).
  Local copies: `/tmp/x3-taa-camera-gate-depth-v1/`.
- Host: `PYTHONPATH=verification/probe python3 -m unittest discover -s verification/analysis -p
  'test_taa*.py'` -> 32 OK; `-p 'test_camera_reprojection.py'` -> 10 OK (new case: worst 0.00013 px
  against the double reprojection at 8e5 sector coordinates, 0.00018 px against the replay's
  `camera_previous_ndc`). Scratch CMake (MinGW i686, Release, `-DPython3_EXECUTABLE=/usr/bin/python3`):
  84 TUs compiled, `d3d9.dll` linked.
- Open: the latched m22 / m32 are per-submission scratch in the engine; `camera_state` now logs
  `p22` / `p32` so the next capture can confirm them. The run209 replay was not rerun. Native
  Windows runtime unverified (documented D3D9 calls only: one more `SetPixelShaderConstantF`).

x87 audit fix (2026-09-21): the draw-path arithmetic of the option is SSE2-only (bit-mask magnitude instead of `std::fabs`, the rotation bound compared as a cosine from a Taylor half-angle series instead of `acos`, `sqrtss` for the diagnostic ratios). `check_no_x87.py` on a scratch production build: roots 95, reachable 547, 0 violations. `test_static_previous_rows` 7 OK (game-scale bound 0.05 px unchanged, series within 1e-9 of cos), `test_taa*` 33 OK; the four seam cases rerun with the same numbers (95 checks each, node differs from off in frame 3 only, max 0.00066 px).

Runner cut bounds (2026-09-21): `seam-taa-camera-on` failed at frame 5 (4 px, x=4..7 y=3) on the Run60 candidate and identically on the Run59 build `b1bb05fb` (source 526851e4), so it is not a Run60 regression. Cause: 73080396 turned the DLL default cut bounds off (1e30 / 1) while the seam scripts (`expected_cut` 0.25, `SEAM_CUTS`, `bound_px 2.4`) model the diagnostic detector; at frame 5 (duplicate key, 1 of 3 keyed draws missing) the reference resolve cut and the DLL did not. Production is as intended. `run_motion_output.py` now requests `X3M_MOTION_CUT_MEDIAN_PX=48` / `X3M_MOTION_CUT_MISSING=0.25` for every case (the unmatched-static cases keep the production bounds in their own env). Evidence on a build of main-equivalent source (DLL `9132951a`): default env FAIL (same 4 px), env-forced 48/0.25 PASS 177 checks, fixed runner: `seam-taa-camera-on` 177, `seam-taa-on` 164, `production-taa-on` 83, the four unmatched-static cases 95 each, all pass.
## Camera gate: latch-free lane term (c9 / s5), general-flight cases (2026-09-21)

Design and numbers: `docs/architecture/taa-lattice-crawl.md` section 32.4. Not installed, no game
launched. Bottle X3; local copies `/tmp/x3-taa-camera-gate-depth-v2/`.

- Shader flow under the Wine lock: `--shader temporal_line_mask_camera` PASS, 983 words / 245 slots
  (was 920 / 226), bytecode `e280346e7ce1...`; `--shader temporal_line_mask` PASS, bytecode
  `a44bfebd9767...` unchanged. sha256 of all `src/renderer/*_inc.h` before/after: one file differs.
- `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3
  verification/probe/run_temporal_pass.py` -> passed; lattice mode `RESULT PASS numerical=519
  state_restorations=21`. `THIN_REGION_CAMERA_FLIGHT` x10: oracle error 0.000000 on every modelled
  row; yaw+forward share 1/1 (law), 1/1 (lane), 0 withheld; wrong latch 0 (law), 1/1 (lane); near
  window mean 0.1173 law = lane, 0 withheld; behind 0 / 0. Forward, pan, at-rest, overflow and stale
  rows equal the previous summary. `LINE_TIMING_CAMERA` deltas 0.1848 / 0.5209 ms (R32F scene; the
  lane's one extra fetch in the tests draw is not in this timing).
- Host: `test_taa*.py` OK; `test_camera_reprojection.py` 10 OK under discover and as
  `python3 -m unittest verification.analysis.test_camera_reprojection` (lane form worst 0.00013 px).
  Scratch CMake incremental build: 11 TUs, `d3d9.dll` linked.
- Replay: `taa_lattice_gate_replay.py --camera-check` run209 forward 0.0325/0.0531/0.0606 ->
  0.0012/0.0023/0.0104 px with the exact inverse; best-fit z scale 0.979 -> 1.000.
- Review items not changed, with reason: `src/temporal/line_mask_camera_ps.hlsl` exists and is tracked
  (three lines: the `#define X3M_CAMERA_GATE 1` and the include), so the header's source line and the
  manifest's `defines: null` plus `includes` are accurate. `fog_distance_replay.py`'s digest covers the
  selected rows of one capture and is never compared across captures; new captures simply digest the
  two extra fields (accepted). `run_motion_output.py` parses `camera_state` with a key=value regex, so
  the trailing `p22=` / `p32=` cannot disturb `prev_valid_at_policy`; its assertions need the Wine
  fixture and are left to the candidate's seam run.

Review closure of the lane follow-up (2026-09-21), `/tmp/x3-taa-camera-gate-depth-v3/`: with the lane
bound the c8 law is no longer consulted (valid depth without a positive `.b` stays on the far plane);
`temporal_line_mask_camera` 974 words / **243 slots**, bytecode `011116196d78...`, all other headers
byte-identical, `temporal_line_mask` `a44bfebd9767...`. `run_temporal_pass.py` on X3 passed, lattice
mode `numerical=522 state_restorations=21`; new row `wrong-latch-lane-mixed` (hole x < 6): window
share 1/1, maximum within 8 px of the hole 0, oracle error 0; the other ten flight rows, forward,
pan and at-rest rows as before. `LINE_TIMING_CAMERA_LANE`: law 1.9344 ms, lane 2.0492 ms, fetch
+0.1148 ms, lane input over R32F -0.0525 ms. "Latch-free" is scoped to the depth law in the header
and in section 32.4 (m00 / m11 / m20 / m21 remain latched); section 32.2 points at 32.4 for the z
scale.

## Sentinel stabiliser for unrouted depth-sentinel pixels, opt-in (2026-09-21)

Design and status: `docs/architecture/temporal-integration.md`, "Distant unrouted stations under a pan". Not installed,
not flown; WIP on a worktree branch over `298487fc`. `--taa-sentinel-stabiliser S[,E]` -> `X3M_TAA_SENTINEL_STABILISER`
(default absent = off; S 0..1, suggested 0.7; E >= 0, default 1, 0 = no emitter bound; the launcher requires the camera
gate, the route turns the option off with `motion_output_taa_sentinel unavailable=1` otherwise; the `motion_output_taa` line
ends with `sentinel_stabiliser=... sentinel_emitter=...`). `--taa-debug` capture frames now also dump the final mask as
`taa_mask` (bgra8: r filter weight, g far gate, b camera-gated strength, a screen-gated strength).

Programs (X3 bottle, `d3dx9_37.dll` `c2ccb84c...`; slots from `RESOLVE_BUDGET`): `temporal_line_mask_camera` 983 -> 1101
words, 245 -> **281 slots** (one `[branch]`, two fetches, skipped at S = 0); new `temporal_thin_box_rows` 204 words /
**35 slots** (7 fetches) and `temporal_thin_box_columns` 420 words / **94 slots** (1 mask + 14 row fetches, plus 1 depth +
9 colour fetches only under the emitter bound; includes the empty-box fail-safe); `temporal_line_mask` (305), `temporal_resolve_far_camera` (508 of 512) and
`temporal_thin_box` (51) byte-identical. All manifests were regenerated because the generator's hash is part of each; every
other header is unchanged. `generate_rigid_motion_pixel.py --check` passes for all fragments.

`X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_temporal_pass.py`: passed,
lattice mode `numerical=576 state_restorations=23` (538 / 21 before; 38 / 2 are the new cases). Against the previous
summary 2664 leaves were compared and every non-timing value of the existing rows is identical (only the changed mask
program's budget and the checkout paths differ). Rows `SENTINEL_STABILISER` (32 px scene, colour-only facets 0.8 px / pitch
2.37 / value 0.75 over the unrouted sentinel, W_thin 0.97, S 0.7):

| row | result |
|---|---|
| off (S = 0, E = 0 against S = 0, E = 1), arm scene at rest and pan 0.5 | colour, age and mask bit-identical |
| facets at rest | oracle 0.0029 (bound 0.02), age 0, mask 0; flicker 13.16 -> 5.18 codes, ratio **0.394**; detail 19.8 -> 30.9 |
| facets, VERTICAL pan (content crosses the facets), steady 0.3 px/frame | oracle 0.0022; flicker 24.20 -> 11.61, ratio **0.480**; detail 17.8 -> **9.3** |
| vertical pan reversing every frame, 0.3 / 2 / 4 px/frame (history stays in the 32 px frame) | oracle 0.0024 / 0.0029 / 0.0029; ratio **0.378 / 0.386 / 0.389**; detail 15.5 -> 9.4, 20.6 -> 32.0, 25.6 -> 34.9; oracle skipped 0 / 1638 / 8242 border pixels (ceilings 0 / 9984 / 16640) |
| vertical pan steady 2 / 4 px/frame, rows [20, 28): at most 14 / 7 frames of history (reported, not asserted) | ratio 0.657 / 0.666; detail 20.4 -> 47.5, 29.7 -> 59.5 |
| silhouette: 8x8 geometry square, camera at rest, 64 frames | square pixels differing from S = 0: 0; unrouted pixels differing: 32 986 |
| routed sentinel (glass, motion alpha 1), 64 frames | colour / age differing: 0; mask b / a differing: 0 |
| routed 2x2 object claiming 2 px/frame against the camera path | 284 sentinel pixels within 8 px: 0 differ, strength 0; beyond: strength >= 0.698, 37 456 pixel-frames differ |
| emitter: colour-only bar, luma 4, 8 px wide, 6 px/frame, camera at rest | trail 1 px (E = 1) / 3 px (E = 0), also relative to S = 0; changed pixels at most 1 / 3 px from an edge of the bar; E = 0 also darkens up to 3 px INSIDE each edge (6 of the bar's 8 columns, up to 2.46 below S = 0), E = 1 changes one column; trail peak above the background: S = 0 **3.11**, E = 1 **3.44**, E = 0 2.46 (asserted: at most 0.4 above S = 0 and below the emitter's value); zero pixels differ from the background once the bar has left |
| non-finite 3x3 block (65504) 3 px from a luma-4 bar | block pixels equal S = 0 and are 0 on all 32 frames; every output finite, within [0, 4] |
| camera cut on frame 40 | output = current exactly (previous frame differed by 0.46) |

Also covered: S outside [0, 1] or E negative / non-finite refused with the camera gate and ignored without it; hostile
c0..c7 / c22..c24 and s1..s3, s6, s9, s10 restored; a failed columns draw (the fifth) publishes nothing and the next run
restarts without history; Reset recreates the row targets; a row-target creation failure that is not a lost device turns the
stabiliser off for the session and the run equals the camera-gate run bit for bit.

Reading of the pan rows. Flicker is the rms of the output against the motion-compensated earlier output of the same
content at the same pixel phase (lag 1 at rest, 10 frames / 3 px for steady 0.3, 2 frames for the reversing pans). The
stabiliser works under a pan that moves content: 0.38-0.48 x the S = 0 flicker wherever the history is old enough, 0.66 x
with only 5-14 frames of history (the fixture's frame is 32 px; in the game a sky pixel under a 4 px/frame pan has far more).
Cost in sharpness under a slow FRACTIONAL pan: at 0.3 px/frame the vertical detail falls to 0.52-0.60 x the S = 0 run
(9.3 against 17.8 codes), the long history being resampled every frame; integer pans and rest do not show it (detail rises).
This is the far stabiliser's "blurry when the camera moves" mechanism, bounded here by the box instead of a speed gate; it
is the thing to look at in the flight.

Emitter peak. The E = 1 trail pixel is brighter (3.44) than the unbound one (2.46) because of what the pixel held a frame
earlier, not because the bound adds light: that pixel was inside the bar, within 3 px of its leading edge, where the unbound
7x7 box had kept the old background (its dark ghost), so its history is dim. The installed resolve itself leaves 3.11 there
(the 3x3 variance clip reaches 3.71 of the bar's 4); E = 1 adds 0.33.

Review fixes (2026-09-21): the separable programs are created by `TemporalPass::configure_sentinel()`, which the route calls
only with S > 0 and the camera gate (`configure_far` no longer creates them; asserted); the emitter bound is validated only
while S > 0; `--taa-sentinel-stabiliser` requires `--taa`; the shared oracle counts the border pixels it leaves to the
shader and throws above a per-case ceiling, 0 for every case but the fast sentinel pans; an empty box is written as (0, 0).
That last case is unreachable by a reader (a pixel with no finite inner tap is itself non-finite, hence current-only), so
the fixture can only assert the output, not the box bytes.

Cost (`LINE_TIMING_SENTINEL`, 1280x768, all-unrouted-sentinel frame under a 1 px/frame pan, CPU wall with query drain). The
tracked results hold one run: separable box and the resolve's box clip on every pixel **+0.305 ms**, against **+0.513 ms**
for the 49-tap program over a whole frame (`fragmented_pan_camera_delta_ms`) in the same run. Earlier runs of this session,
overwritten in the results, gave +0.20..0.23 against +0.36..0.62, so the run-to-run spread is about 0.1-0.25 ms and only
the ordering is established; by pixel count roughly 0.6 against 1.1 ms at 1920x1080 [I]. Memory: one more A16B16G16R16F
pair while S > 0 (15.7 MiB at 1280x768, 33 MiB at 1920x1080), released by the first run without it. No per-draw or
per-frame CPU work beyond one constant and one texture bind.

Host: `test_taa_image_defaults` 12 tests OK; full `run_temporal_pass.py` exit 0, `passed: true` (new `test_thin_region_gate_is_absent_unless_given`).
- Limits: the pan scene is uniform along the pan axis (no resampling loss); moving-lattice quality is section 32's
  replay, not this fixture. Native Windows unverified. Full runner 112 s, lock wait 0 s.


## Run59 accepts the camera thin-region gate as the default (2026-09-21)

Run 59 flew the A/B on DLL `b1bb05fb` at the lattice position: run207 with
`--taa-thin-region-gate screen`, run208 with `camera`, otherwise the same command.
The user accepts the camera gate ("I think it's fixed") for camera pans. Triage
of run208 found no anomaly: **0 non-finite texels**, gate-open share **0.44 % ->
9.3 %**, tracked rms **x 0.874** and gradient **x 0.913** measured on the same
capture. The roll residual (crawl under camera roll) was not measured in this run
and stays open.

The gate therefore becomes the default whenever the thin region is active, in the
launcher and in the DLL's native fallback. `--taa-thin-region-gate screen` is the
opt-out and remains bit-identical to the pre-Run59 behaviour. With
`--taa-line-filter` active the default resolves to `screen` silently, because the
mask's line channel carries the only gate that mode can have; an explicit
`camera` plus the line filter is still refused by the launcher, and the route's
own fallbacks are unchanged (configure-time refusal, box-allocation failure ->
screen gate bit for bit). Nothing changes without the thin region: no gate
variable is emitted and the native default is untouched.

Checks for this default-only change: `test_taa*` **25 host tests OK** (the gate
test now pins default -> camera, explicit screen, line filter -> screen, explicit
camera + line filter refused, and no thin region -> no variable, plus the native
fallback site and its ordering after the thin-region and line-filter parses);
`src/proxy/capture.cpp` cross-compiles clean under
`i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -msse2 -mfpmath=sse
-mstackrealign -mincoming-stack-boundary=2`. No shader, predicate, hook or
recovery policy changed and no per-draw work was added, so the fixture and
shader evidence of the section above carries over unchanged. No game launched, no
Wine run, no new candidate. Native Windows runtime remains unverified.

Note 2026-09-21, user tuning on the installed Run59 DLL (no capture, by eye): with
the camera gate on, panels and arms blur while the camera moves (the expected
resampling cost of W 0.97 with the clip off). Of `0.97,0.5`, `0.94,0.5` and
`0.94,1` the user tentatively prefers **`--taa-thin-region 0.94,1`**, with no
crawl at rest. They will retest after the depth-aware camera path lands; the
0.97 default is unchanged until then.

## Static-world previous rows for unmatched draws (2026-09-21)

`--taa-unmatched-static node|all` (default off), design and run209 diagnosis in
`docs/architecture/temporal-integration.md`, "Unmatched draws: static-world
previous rows". Worktree of main `184843cd`, uncommitted; fresh CMake build
(`-DPython3_EXECUTABLE=/usr/bin/python3`), DLL `5f4a1010`, seam DLL `8970a9e9` (after the review follow-up below: see its hashes),
fixture exe `17355a8a`. Bottle X3, arm64, `FEX_X87REDUCEDPRECISION=1`,
`WINEMSYNC=1`.

Host: `test_static_previous_rows` **6 OK** (the header against a basis-vector
oracle: rotation + translation + per-draw depth law, off-centre projection terms,
run209-scale coordinates 0.05 px bound at 1280x720, identity, three refusals;
`classify_miss` over absent/present-object/new-object/reused-pointer/poisoned/
consumed/invalid keys) and `test_taa_image_defaults` **14 OK** (launcher forwards
only when given, drops an inherited value, requires `--taa`; native default off).

Fixture: new `unmatchedstatic` script of `motion_output_fixture` (world-placed
bodies under a translating, yawing camera; oracle composed from the script's own
matrices) through `run_motion_output.py` in consume-only mode, cases
`seam-taa-unmatched-static-{unset,off,node,all}`, **95 checks each, all pass**,
lock wait 0 s, 12.6 s for the four. Frame 3 changes body S's key by the LOD word
alone (same node and serial); frame 6 introduces a new node N.

| case | S at frame 3 | N at frame 6 | motion/depth hashes |
| --- | --- | --- | --- |
| unset | sentinel, 0 of 221 px changed by the resolve | sentinel, 0 of 82 | reference |
| off (`0`) | same | same | identical to unset on all 9 frames, presented frames identical |
| node | camera-path motion, max 0.00066 px / 1.7e-7 depth, 100 of 221 px changed by the history blend | sentinel, 0 of 82 | differs from off in frame 3 only |
| all | as node | camera-path motion, max 0.0011 px, 82 of 82 | differs from node in frame 6 only |

In every case the DLL's frame lines keep `matched`/`gate6` as a miss (frame 3:
routed 2, matched 1, gate6 1) and the resolved image equals the reference resolve
byte for byte on all 9 frames. Compact records:
`verification/results/bottle-X3/seam-taa-unmatched-static-*-fixture.json`; copies
and timings in `/tmp/x3-taa-unmatched-static-v1/`.

Not run: the existing `seam-taa-camera-on` regression case (the Wine lock was held
by the user's game session afterwards). The option-off path is covered by the
unset/off twin only. No flight yet; native Windows runtime unverified.

Review follow-up (same day): `classify_miss` mirrors the lookup's collecting and
overflow guards and uses one search; verdict and rows share one latch snapshot; the
per-frame line is limited to applied frames (cap 256) and the detail line carries
the projection check. Limits on record: a camera translation jump without a
rotation cut is not gated; for static geometry the reprojection stays exact, but
disocclusion under such a jump is untested and matters mainly for `all`. The
fixture proves the arithmetic through the DLL at about 0.5-1 px of reprojection per
frame; run209-scale motion (250 units per frame at 4e4-unit coordinates) rests on
the host oracle test (0.05 px bound), not on a fixture. Private-projection draws
are a flight-verification item (architecture note).
Rerun after the follow-up: the four seam cases pass again, 95 checks each, same numbers; DLL `9aef914e`, seam DLL `e974190c`, fixture exe `544022e5`; the detail lines report `projection_x/y = 1.00000` for both fixture bodies.

## Camera gate made depth- and translation-aware: c8 of the camera mask (2026-09-21)

Design and numbers: `docs/architecture/taa-lattice-crawl.md` section 32.3. Not committed as a
candidate, not installed, no game launched. Bottle X3.

- Shader flow: `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3
  tools/shaders/generate_rigid_motion_pixel.py --shader temporal_line_mask_camera` -> PASS, 920
  words / 226 slots (was 897 / 223), bytecode `cf7c1764095d...`; `--shader temporal_line_mask` ->
  PASS, bytecode `a44bfebd9767...` unchanged (manifest source hash only). sha256 of all 61
  `src/renderer/*_inc.h` before/after: only `temporal_line_mask_camera_program_inc.h` differs.
- Fixture: `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3
  verification/probe/run_temporal_pass.py` -> passed, lattice mode `RESULT PASS numerical=501
  state_restorations=21` (495 + the 6 forward-flight checks); main mode 508 / 278 / 386 unchanged.
  `THIN_REGION_CAMERA_FORWARD`: speeds 0.600 / 0.300 px/frame, c8-form residual 0.000213 /
  0.000107 px, routed-row residual 0.005840 / 0.002921 px, screen share 0, rotation-only share 0
  (output = screen gate bit for bit), camera share 1.0000 / 1.0000, rms 16.340 -> 1.508 codes
  (x0.0923), p2p 99.4 -> 7.8, mover gate maximum within 8 px 0.0000, window minimum 1.0000.
  Pan (x0.0866, share 1.0), at-rest identity (3/3), overflow and stale-patch rows equal the
  previous summary. `LINE_TIMING_CAMERA` deltas 0.1556 / 0.5758 ms (before 0.1640 / 0.5915).
  Local copies: `/tmp/x3-taa-camera-gate-depth-v1/`.
- Host: `PYTHONPATH=verification/probe python3 -m unittest discover -s verification/analysis -p
  'test_taa*.py'` -> 32 OK; `-p 'test_camera_reprojection.py'` -> 10 OK (new case: worst 0.00013 px
  against the double reprojection at 8e5 sector coordinates, 0.00018 px against the replay's
  `camera_previous_ndc`). Scratch CMake (MinGW i686, Release, `-DPython3_EXECUTABLE=/usr/bin/python3`):
  84 TUs compiled, `d3d9.dll` linked.
- Open: the latched m22 / m32 are per-submission scratch in the engine; `camera_state` now logs
  `p22` / `p32` so the next capture can confirm them. The run209 replay was not rerun. Native
  Windows runtime unverified (documented D3D9 calls only: one more `SetPixelShaderConstantF`).

x87 audit fix (2026-09-21): the draw-path arithmetic of the option is SSE2-only (bit-mask magnitude instead of `std::fabs`, the rotation bound compared as a cosine from a Taylor half-angle series instead of `acos`, `sqrtss` for the diagnostic ratios). `check_no_x87.py` on a scratch production build: roots 95, reachable 547, 0 violations. `test_static_previous_rows` 7 OK (game-scale bound 0.05 px unchanged, series within 1e-9 of cos), `test_taa*` 33 OK; the four seam cases rerun with the same numbers (95 checks each, node differs from off in frame 3 only, max 0.00066 px).

Runner cut bounds (2026-09-21): `seam-taa-camera-on` failed at frame 5 (4 px, x=4..7 y=3) on the Run60 candidate and identically on the Run59 build `b1bb05fb` (source 526851e4), so it is not a Run60 regression. Cause: 73080396 turned the DLL default cut bounds off (1e30 / 1) while the seam scripts (`expected_cut` 0.25, `SEAM_CUTS`, `bound_px 2.4`) model the diagnostic detector; at frame 5 (duplicate key, 1 of 3 keyed draws missing) the reference resolve cut and the DLL did not. Production is as intended. `run_motion_output.py` now requests `X3M_MOTION_CUT_MEDIAN_PX=48` / `X3M_MOTION_CUT_MISSING=0.25` for every case (the unmatched-static cases keep the production bounds in their own env). Evidence on a build of main-equivalent source (DLL `9132951a`): default env FAIL (same 4 px), env-forced 48/0.25 PASS 177 checks, fixed runner: `seam-taa-camera-on` 177, `seam-taa-on` 164, `production-taa-on` 83, the four unmatched-static cases 95 each, all pass.
## Camera gate: latch-free lane term (c9 / s5), general-flight cases (2026-09-21)

Design and numbers: `docs/architecture/taa-lattice-crawl.md` section 32.4. Not installed, no game
launched. Bottle X3; local copies `/tmp/x3-taa-camera-gate-depth-v2/`.

- Shader flow under the Wine lock: `--shader temporal_line_mask_camera` PASS, 983 words / 245 slots
  (was 920 / 226), bytecode `e280346e7ce1...`; `--shader temporal_line_mask` PASS, bytecode
  `a44bfebd9767...` unchanged. sha256 of all `src/renderer/*_inc.h` before/after: one file differs.
- `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3
  verification/probe/run_temporal_pass.py` -> passed; lattice mode `RESULT PASS numerical=519
  state_restorations=21`. `THIN_REGION_CAMERA_FLIGHT` x10: oracle error 0.000000 on every modelled
  row; yaw+forward share 1/1 (law), 1/1 (lane), 0 withheld; wrong latch 0 (law), 1/1 (lane); near
  window mean 0.1173 law = lane, 0 withheld; behind 0 / 0. Forward, pan, at-rest, overflow and stale
  rows equal the previous summary. `LINE_TIMING_CAMERA` deltas 0.1848 / 0.5209 ms (R32F scene; the
  lane's one extra fetch in the tests draw is not in this timing).
- Host: `test_taa*.py` OK; `test_camera_reprojection.py` 10 OK under discover and as
  `python3 -m unittest verification.analysis.test_camera_reprojection` (lane form worst 0.00013 px).
  Scratch CMake incremental build: 11 TUs, `d3d9.dll` linked.
- Replay: `taa_lattice_gate_replay.py --camera-check` run209 forward 0.0325/0.0531/0.0606 ->
  0.0012/0.0023/0.0104 px with the exact inverse; best-fit z scale 0.979 -> 1.000.
- Review items not changed, with reason: `src/temporal/line_mask_camera_ps.hlsl` exists and is tracked
  (three lines: the `#define X3M_CAMERA_GATE 1` and the include), so the header's source line and the
  manifest's `defines: null` plus `includes` are accurate. `fog_distance_replay.py`'s digest covers the
  selected rows of one capture and is never compared across captures; new captures simply digest the
  two extra fields (accepted). `run_motion_output.py` parses `camera_state` with a key=value regex, so
  the trailing `p22=` / `p32=` cannot disturb `prev_valid_at_policy`; its assertions need the Wine
  fixture and are left to the candidate's seam run.

Review closure of the lane follow-up (2026-09-21), `/tmp/x3-taa-camera-gate-depth-v3/`: with the lane
bound the c8 law is no longer consulted (valid depth without a positive `.b` stays on the far plane);
`temporal_line_mask_camera` 974 words / **243 slots**, bytecode `011116196d78...`, all other headers
byte-identical, `temporal_line_mask` `a44bfebd9767...`. `run_temporal_pass.py` on X3 passed, lattice
mode `numerical=522 state_restorations=21`; new row `wrong-latch-lane-mixed` (hole x < 6): window
share 1/1, maximum within 8 px of the hole 0, oracle error 0; the other ten flight rows, forward,
pan and at-rest rows as before. `LINE_TIMING_CAMERA_LANE`: law 1.9344 ms, lane 2.0492 ms, fetch
+0.1148 ms, lane input over R32F -0.0525 ms. "Latch-free" is scoped to the depth law in the header
and in section 32.4 (m00 / m11 / m20 / m21 remain latched); section 32.2 points at 32.4 for the z
scale.

## Sentinel stabiliser for unrouted depth-sentinel pixels, opt-in (2026-09-21)

Design and status: `docs/architecture/temporal-integration.md`, "Distant unrouted stations under a pan". Not installed,
not flown; WIP on a worktree branch over `298487fc`. `--taa-sentinel-stabiliser S[,E]` -> `X3M_TAA_SENTINEL_STABILISER`
(default absent = off; S 0..1, suggested 0.7; E >= 0, default 1, 0 = no emitter bound; the launcher requires the camera
gate, the route turns the option off with `motion_output_taa_sentinel unavailable=1` otherwise; the `motion_output_taa` line
ends with `sentinel_stabiliser=... sentinel_emitter=...`). `--taa-debug` capture frames now also dump the final mask as
`taa_mask` (bgra8: r filter weight, g far gate, b camera-gated strength, a screen-gated strength).

Programs (X3 bottle, `d3dx9_37.dll` `c2ccb84c...`; slots from `RESOLVE_BUDGET`): `temporal_line_mask_camera` 983 -> 1101
words, 245 -> **281 slots** (one `[branch]`, two fetches, skipped at S = 0); new `temporal_thin_box_rows` 204 words /
**35 slots** (7 fetches) and `temporal_thin_box_columns` 420 words / **94 slots** (1 mask + 14 row fetches, plus 1 depth +
9 colour fetches only under the emitter bound; includes the empty-box fail-safe); `temporal_line_mask` (305), `temporal_resolve_far_camera` (508 of 512) and
`temporal_thin_box` (51) byte-identical. All manifests were regenerated because the generator's hash is part of each; every
other header is unchanged. `generate_rigid_motion_pixel.py --check` passes for all fragments.

`X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_temporal_pass.py`: passed,
lattice mode `numerical=576 state_restorations=23` (538 / 21 before; 38 / 2 are the new cases). Against the previous
summary 2664 leaves were compared and every non-timing value of the existing rows is identical (only the changed mask
program's budget and the checkout paths differ). Rows `SENTINEL_STABILISER` (32 px scene, colour-only facets 0.8 px / pitch
2.37 / value 0.75 over the unrouted sentinel, W_thin 0.97, S 0.7):

| row | result |
|---|---|
| off (S = 0, E = 0 against S = 0, E = 1), arm scene at rest and pan 0.5 | colour, age and mask bit-identical |
| facets at rest | oracle 0.0029 (bound 0.02), age 0, mask 0; flicker 13.16 -> 5.18 codes, ratio **0.394**; detail 19.8 -> 30.9 |
| facets, VERTICAL pan (content crosses the facets), steady 0.3 px/frame | oracle 0.0022; flicker 24.20 -> 11.61, ratio **0.480**; detail 17.8 -> **9.3** |
| vertical pan reversing every frame, 0.3 / 2 / 4 px/frame (history stays in the 32 px frame) | oracle 0.0024 / 0.0029 / 0.0029; ratio **0.378 / 0.386 / 0.389**; detail 15.5 -> 9.4, 20.6 -> 32.0, 25.6 -> 34.9; oracle skipped 0 / 1638 / 8242 border pixels (ceilings 0 / 9984 / 16640) |
| vertical pan steady 2 / 4 px/frame, rows [20, 28): at most 14 / 7 frames of history (reported, not asserted) | ratio 0.657 / 0.666; detail 20.4 -> 47.5, 29.7 -> 59.5 |
| silhouette: 8x8 geometry square, camera at rest, 64 frames | square pixels differing from S = 0: 0; unrouted pixels differing: 32 986 |
| routed sentinel (glass, motion alpha 1), 64 frames | colour / age differing: 0; mask b / a differing: 0 |
| routed 2x2 object claiming 2 px/frame against the camera path | 284 sentinel pixels within 8 px: 0 differ, strength 0; beyond: strength >= 0.698, 37 456 pixel-frames differ |
| emitter: colour-only bar, luma 4, 8 px wide, 6 px/frame, camera at rest | trail 1 px (E = 1) / 3 px (E = 0), also relative to S = 0; changed pixels at most 1 / 3 px from an edge of the bar; E = 0 also darkens up to 3 px INSIDE each edge (6 of the bar's 8 columns, up to 2.46 below S = 0), E = 1 changes one column; trail peak above the background: S = 0 **3.11**, E = 1 **3.44**, E = 0 2.46 (asserted: at most 0.4 above S = 0 and below the emitter's value); zero pixels differ from the background once the bar has left |
| non-finite 3x3 block (65504) 3 px from a luma-4 bar | block pixels equal S = 0 and are 0 on all 32 frames; every output finite, within [0, 4] |
| camera cut on frame 40 | output = current exactly (previous frame differed by 0.46) |

Also covered: S outside [0, 1] or E negative / non-finite refused with the camera gate and ignored without it; hostile
c0..c7 / c22..c24 and s1..s3, s6, s9, s10 restored; a failed columns draw (the fifth) publishes nothing and the next run
restarts without history; Reset recreates the row targets; a row-target creation failure that is not a lost device turns the
stabiliser off for the session and the run equals the camera-gate run bit for bit.

Cost (`LINE_TIMING_SENTINEL`, 1280x768, all-unrouted-sentinel frame under a 1 px/frame pan, CPU wall with query drain, four
runs): **+0.20 to +0.23 ms** for the separable box and the resolve's box clip on every pixel, against **+0.61 /
+0.62 ms** for the 49-tap program over a whole frame (`fragmented_pan_camera_delta_ms`); by pixel count about 0.45 ms
against 1.3 ms at 1920x1080 [I]. Memory: one more A16B16G16R16F pair while S > 0 (15.7 MiB at 1280x768, 33 MiB at
1920x1080), released by the first run without it. No per-draw or per-frame CPU work beyond one constant and one texture bind.

Host: `test_taa_image_defaults` 15 tests OK (new: the option is opt-in, forwarded only when given, refused without the
camera gate, malformed values refused), `test_shader_compiler_provenance` OK. Scratch RelWithDebInfo build links;
`build_motion_output.sh` (fixture and seam DLL) compiles and links against it. `check_no_x87.py` on this branch's own scratch DLL (GCC 16.2.0, RelWithDebInfo): **PASS**, roots 95, reachable 547,
violations {}. Correction of an earlier reading: a first audit reported 2 violations because the shared scratch directory
already held another worktree's build tree (`CMAKE_HOME_DIRECTORY` of a different agent), so that DLL was not this source;
the build was redone in a directory of its own. Hardening kept from that detour: `static_previous_rows.h` compiled ALONE
with the production flags emitted two x87 `fld / fabs / fstp` for its integer bit-mask `magnitude()` (GCC recognises the
sign-bit clear of a double in memory), i.e. the 0-violation result depended on inlining context. `magnitude()` now uses the
SSE2 intrinsic (`_mm_andnot_pd(_mm_set_sd(-0.), _mm_set_sd(x))`, `andnpd`; same bits) under `__SSE2__`, the bit mask on
other hosts; the isolated compile has 0 x87 instructions, `test_static_previous_rows` 7 OK.

`taa_mask` dump consumers (static check, no capture run): the file is `taa_mask_<device>_<frame>.bgra8`; the directory
readers match `taa_1_*` / `taa_*.rgba16f` (`taa_resolve_replay.py`, `evaluate_space_exposure.py`, `run_motion_output.py`), as
with the existing `taa_age_*`, and the log parsers dispatch on exact tags, so `motion_output_taa_mask_readback` is ignored.

Not verified: any flight; real emitter luma against E = 1; GPU time in the game; native Windows (ps_3_0, `tex2Dlod`,
two-target MRT and FP16 targets only, all already required by the camera gate; cross-compiled, not run).

## 2026-09-22 triage: run215, distant solar-plant flicker during horizontal pan

Read-only log/capture triage (no Wine, no game) of `/tmp/x3-bottleX3-run215`
(session `session-20260921-234823-216.log`, 32-frame F8 burst, frames
46573-46604) against the user report of persistent flicker on a distant
non-Terran solar plant while panning horizontally. Full data in
`verification/results/run215-distant-station-triage.json`.

Config confirmed from the log: DLL `0bc8ff36...` / commit `ed105485`;
`motion_output_taa`: `thin_region=0.9700 thin_gate=camera far_weight=0.9850
far_speed_lo=0.030 far_speed_hi=0.250 sentinel_stabiliser=0.700
sentinel_emitter=1.000`; no `box_rows_failed`/`sentinel_failed` strings
anywhere in the log (no explicit program-creation failure, but also no
explicit success line); 0 of 32 burst frames have `camera_cut=1`.

The station (template-matched, screen track ~(660,294)@f46581 to
(780,290)@f46590/91 back to (644,294)@f46597) is **100% sentinel depth (-1)
and 100% unrouted (motion alpha -1)** in its crop across every sampled
frame; its shader (`vs=4944d81dfe531b37`, matching `motion_unmatched_static`
model `000054b3`) is refused from the motion route as `no_zwrite` (512) and
`overlay_node` (576) out of 1216 draws in the window — this is exactly the
sentinel-stabiliser's target case, not the far-stabiliser/thin-line case
(far stabiliser is closed: station's own screen speed reaches ~40 px/frame,
far above `far_speed_hi=0.25`; measured mask `a` = 0 there).

`taa_mask.b` (camera-gated strength) reads **~0.698 (~0.7 = configured S)
uniformly on both the station crop and a plain-sky reference crop**, every
sampled frame: the sentinel stabiliser is engaging at full configured
strength, but does not distinguish the lattice/strut structure from plain
background at all. Motion-compensated frame-to-frame RMS (crude, stride-4
template alignment) on `color_1` vs `taa_1` in the crop: color RMS 22.1 (of
mean 67, bgra8), taa RMS 0.021 (of mean 0.16, rgba16f linear) — roughly 33%
vs 13% relative, implying an effective history weight near 0.75, below the
0.9-0.985 ceilings the flags advertise.

First mechanism (inference from source, not from a controlled A/B capture):
`line_mask_ps.hlsl`'s camera-relative gate (`gateOpen`, lines ~126-140) adds
camera-translation parallax only `if (validDepth(depth))`; a sentinel pixel
always takes `depth=1` with zero translation term, so its computed "speed"
is near 0 under a pure yaw regardless of the object's true, finite distance.
The composition (lines ~184-190) then ORs the sentinel term in with a plain
`max()`, unconditioned on the line mask's own fragmentation test, so a
finite-distance, unrouted, no-zwrite/overlay object (this solar plant) gets
the same full-strength box/history relaxation as genuine infinite
background. This matches the existing fixture line above ("routed 2x2
object claiming 2 px/frame against the camera path ... beyond: strength >=
0.698, 37 456 pixel-frames differ"): an object whose real motion disagrees
with the far-plane assumption produces exactly this kind of mismatch.
File:line candidates: `src/temporal/line_mask_ps.hlsl:70-74,126-140,184-190`,
`src/temporal/resolve.hlsl:453-457`, `src/renderer/temporal_pass.cpp:409,430`.

Open: no A/B capture (`--taa-sentinel-stabiliser 0` vs `0.7`, same camera
path) exists to confirm the stabiliser is net-harmful here rather than
merely insufficient; RMS measurement is coarse (single global per-frame
pixel shift, no sub-pixel optical flow); no confirmation the thin-box
programs actually built successfully this run (silence, not a positive
line).

## 2026-09-22 follow-up: run216 (laser trails + at-rest station baseline) and run215 pan deep dive

Same DLL/commit as above. `/tmp/x3-bottleX3-run216`, `--taa-thin-region 0.94,1`
(`thin_region=0.9400 thin_relax=1.000`), `sentinel_stabiliser=0.700` unchanged.
Two F8 bursts: 6721-6752 (firing lasers over sky) and 29737-29768 (same
distant solar plant, camera stationary: drift < 0.3 world units/frame,
0/32 `camera_cut`). Full numbers in
`verification/results/run215-distant-station-triage.json` (`run216_followup`).

**Laser trails (burst 1):** no isolated far-traveling bolt crossing plain
sky was found in this capture (the sampled ROI's transient brightness stays
adjacent to the ship's own muzzle/exhaust glow every frame). Where a sampled
pixel does return to near-black in `color_1` the frame after saturation,
`taa_1` (scaled to `color_1`'s 0-255 range via a sky-patch ratio) shows a
residual excess of at most ~16 of 255 code-equivalents for exactly one
frame, gone the next. Small and short-lived, consistent with the user's "no
issues" report; not a clean single-bolt-over-pure-sky measurement.

**Station at rest (burst 2, corrected per user: this is the working
baseline, not a symptom):** same pixel class as the run215 pan case (100%
sentinel depth, 100% unrouted, every frame); `taa_mask` b=0.698, a=0.0 (a=0
because the FRAGMENTED/line test never fires on a uniformly-sentinel depth
neighbourhood, not because the gate is closed); `taa_age` saturated at the
64-frame cap everywhere in the crop. Frame-to-frame RMS, no motion
compensation needed: `color_1` 10.38, `taa_1` 0.0014, ratio **1.3e-4** — the
resolve suppresses the raw per-frame jitter/alias noise (which is already
present in `color_1` even at rest, from the 8-sample jitter pattern) almost
completely once age is fully ramped and the camera gate is open. This is the
mechanism working as intended.

**What limits suppression under the run215 pan (deep dive):** (a) raw pan
speed correlates with residual output flicker far more than sub-pixel
bilinear phase alone (r=0.691 for |dx| vs output RMS, r=0.243 for
fractional-pixel phase vs output RMS, with phase and speed themselves
uncorrelated at r=-0.023 in this sample — not confounded). (b)
Reconstructing the resolve's 7x7 same-size min/max box from `hdr_1` and the
previous frame's `taa_1` bilinear-shifted by the tracked sub-pixel dx (same
linear space, `weigh()`/`unweigh()` tonemap not reproduced — approximation,
not exact): the fraction of the station crop where this reprojected history
falls outside the local box (i.e. the clip actively overrides history)
tracks pan speed almost linearly, **r=0.956**, from ~1-3% near zero speed to
~24-42% at 30-40 px/frame. (c) `taa_age` resets (age<=2) are ~0 while the
pan is monotonic (frames 46584-46591) and jump to 9-18% right where the pan
reverses direction (46592-46597); crop-mean age stays moderate (12-21,
many pixels still capped at 64) throughout, meaning resets are concentrated
at the high-contrast strut edges rather than spread uniformly — consistent
with edge-localized flicker despite a moderate-looking mean age.

**Sharpness, run215 (0.97) vs run216 (0.94), uncontrolled (different pose):**
`taa_1` gradient energy / `hdr_1` gradient energy on the station crop:
run215 (near-zero relative-speed frame 46591) 0.0309, run216 (at rest) 0.0965
— run215 retains proportionally less input sharpness, but poses/apparent
size differ (input gradient energy differs 2x between the two crops), so
this is suggestive only, not a controlled A/B.

Open: no controlled A/B for thin_region 0.97 vs 0.94 at matched pose; the
box-bind reconstruction skips `weigh()`/`unweigh()` (luminance-dependent
tonemap) and uses a rigid horizontal shift for a 3-D object, so it is an
approximation of the actual clip test, not a bit-exact replay.

## 2026-09-22 run215 pan flicker: replay diagnosis (far-plane `expectedDepth` rounding rejects history)

Record: `verification/results/run215-pan-flicker-replay.json`. Tools: `tools/analysis/taa_sentinel_pan_replay.py`
(Run61 resolve: Run60 camera gate + sentinel stabiliser + emitter bound), `taa_sentinel_pan_variants.py`,
`taa_far_plane_reject_check.py`. Host only. [M] measured, [I] inferred. Codes = `codes()` of the replay (bounded luma, 8 bit).

**Fidelity [M].** Replay vs dumped `taa_1`, crop 16 215 1264 365, frames 46574-46604: open loop mean 0.021 / p99 0.06 /
max 1.4 codes, closed loop 0.050 / 0.19 / 2.4, *only after* marking the pixels the installed resolve handed through
current-only (`taa == hdr` bit for bit). Without that the error is 0.07 mean, max 91: the replay accepts every pixel,
the build does not. The design's 0.0016 (run212) was not reached; the production fetch position is confirmed
(error minimum at offset (0, 0) in a +-0.03 px scan).

**Root cause [M].** On the far-plane camera path `camera_far_plane_reprojection` uploads bit-identical z and w rows
(`src/renderer/camera_reprojection.h:111-112`) and the resolve forms `expectedDepth = previousClip.z /
max(previousClip.w, c6.w)` (`src/temporal/resolve.hlsl:302`), then returns current-only with age 1 on
`!validDepth(expectedDepth)` (`:324`). On this GPU the quotient rounds above 1 for 8-11 % of the pixels where
`previousClip.w < 1`, never where `w > 1`: sky current-only share 0.000 (w > 1) against 0.084-0.106 (w <= 1) in all 8
frames checked, including 46591 at 0.5 px/frame; the comb starts exactly at the `w = 1` column (624 / 654, just off
centre, on the side the camera turns toward). IEEE float32 `x * (1 / x)` never exceeds 1 but differs from 1 on 22-27 %
of `w < 1` pixels and 0-2 % of `w > 1` pixels, the same split; the GPU's rounding is not IEEE [I]. At rest `w = 1`
exactly: run216 burst 2 has 0.000 current-only, age 64 everywhere. So during ANY pan, in the leading half of the
screen, each unrouted-sentinel pixel (and every other far-plane camera-path pixel) loses its history about every 10
frames: station mean age 10-25 in run215, age <= 4 on 20-35 % of the station while it is in that half. Station flicker
follows the half, not the speed: 2.0 codes while in the `w <= 1` half (46579-46584 at 20-26 px/frame, 46592-46599 at 3-33),
0.99 in the `w > 1` half at 27-36 px/frame (46600-46603). The triage's speed correlation came from the station being in
the rejecting half during the fast segments.

**Q1 [M].** (a) rotation-only reprojection vs sub-pixel registration of consecutive dumped outputs: residual <= 0.1 px
(grid 0.05) at every speed 0.5-36 px/frame; no frame lag (a one-frame camera lag would show as tens of px); background
rotation 0.0000 deg in this burst. (b) parallax below the same 0.1 px floor. (c) `ev_adapted` 1.15862 and `taa_k`
constant; the resolve input is pre-tonemap. (d) true 7x7 bind share 1.6 % below 5 px/frame, 15 % above 15 (the triage's
24-42 % was its integer-tracking error); it stays 14 % with a non-negative kernel (Keys 0), so it is not Catmull-Rom
overshoot, and removing the box changes flicker by <= 6 %: the bind is not the flicker. Why it rises with speed was not
isolated. (e) the history is already Catmull-Rom; Keys -0.65 raises flicker 6 %.

**Q2 [M], station box 161x73 tracked, frames 46579-46604; flicker = rms vs motion-compensated previous output.**
Input 8.4; at-rest reference in the same metric (run216 burst 2) 0.37. "fix" = no `expectedDepth` rejection.
Gradient of the installed row is inflated by current-only sparkle, not detail. Trail = px more than 4 codes above the
current 3x3 max, per frame of 187 200 (pan streaks of stars). Emitter = run216 burst 1 (6721-6752, roi 500 360 780 756,
19 626 px within 8 px of raw luma > 1, routed ship included): mean excess codes / px > 4 codes.

| variant | flicker <5 | 5-15 | >15 px/frame | gradient | trail px | emitter excess |
|---|---|---|---|---|---|---|
| installed S 0.7 W 0.97 box7 | 1.53 | 1.19 | 1.67 | 21.7 | 736 | 0.74 / 511 (W 0.94) |
| installed + S 1.0 / W 0.94 / box11 / box off | 1.52 / 1.54 / 1.52 / 1.52 | 1.15-1.21 | 1.61 / 1.68 / 1.63 / 1.62 | 22-24 | 4912 / 663 / 1931 / 4655 | |
| S 0 (installed rejects) | 1.70 | 1.40 | 1.98 | 21.3 | 0 | |
| **fix** | **0.56** | **0.68** | **0.84** | 9.7 | 765 | 0.89 / 538 |
| fix + S 0.85 | 0.49 | 0.62 | 0.74 | 9.3 | 2255 | 1.09 / 609 |
| fix + S 1.0 | 0.44 | 0.59 | 0.67 | 9.4 | 4931 | 1.39 / 743 |
| fix + S 1.0 + box11 | 0.41 | 0.58 | 0.58 | 10.2 | 11900 | 2.07 / 919 |
| fix + box11 / box off | 0.54 / 0.54 | 0.68 | 0.80 / 0.79 | 9.8 | 2015 / 4898 | 1.14 / 606, 1.18 / 678 |
| fix + variance box k 2 / k 1 | 0.56 / 0.55 | 0.68 / 0.67 | 0.85 / 0.90 | 9.2 / 7.3 | 410 / 173 | 0.78 / 502 (k 2) |
| fix + W 0.94 | 0.66 | 0.74 | 0.92 | 10.9 | 659 | |
| fix + Keys -0.65 / Keys 0 | 0.60 / 0.52 | 0.75 / 0.60 | 0.89 / 0.79 | 13.1 / 5.0 | 754 / 797 | |
| fix, S 0 (emitter baseline) | 0.99 | 1.00 | 1.35 | 13.1 | 0 | 0.40 / 154 |

Without the fix no tuning helps (<= 4 %). With it the installed settings halve the flicker at every speed. Corrected
reprojection: not applicable (no residual). The burst is 32 frames from a young history (mean age 10), so W variants are
not at steady state and understate a higher W.

**Q3 recommendation.** One CPU-side change, no shader slot: in `camera_far_plane_reprojection`
(`src/renderer/camera_reprojection.h:111-112`) scale the z row (M[2]) by `1 - 0x1p-16` so `expectedDepth` is 0.999985
whatever the GPU's division rounding; the value feeds only `validDepth` and the disocclusion threshold
(`resolve.hlsl:324,350,360`, tolerance >= 1e-4), `line_mask_ps.hlsl` reads rows 0, 1, 3 only. Documented D3D9 float
behaviour only; harmless where division is exact. Update whatever pins row 2 == row 3
(`verification/analysis/test_camera_reprojection.py`, `test_taa_camera_path.py`, `verification/probe/temporal_pass_fixture.cpp`
oracle) and add a fixture row: far-plane yaw, all-sentinel frame, zero current-only pixels in the `w < 1` half. Keep
S 0.7, W 0.97, box7, E 1.0. Residual after the fix [I from replay]: 0.56 / 0.68 / 0.84 codes at <5 / 5-15 / >15 px/frame
against 0.37 at rest and 1.5 / 1.2 / 1.7 today: slight shimmer growing mildly with speed, about half of today's; S 0.85
buys another 12 % for 3x the star-streak pixels and +22 % emitter excess and is the next knob only if the user still
sees it. Not verified on the GPU: the replay's "fix" assumes every such pixel then accepts.
