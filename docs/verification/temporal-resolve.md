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

## 2026-09-22 run215 pan flicker: fix (far-plane z row = w row x (1 - 2^-16)) and fixture row

**Change.** `camera_far_plane_reprojection` (`src/renderer/camera_reprojection.h`) scales the z row by `1 - 0x1p-16` in double
before the float conversion. No shader, constant layout or program changed (resolve bytecode identical; `resolve_far` stays 508 / 512).

**Consumers checked [M, source].** Row 2 is read only by `resolve.hlsl` (`expectedDepth`, and through the includes every resolve
variant: thin, age, line, far, far_camera). `line_mask_ps.hlsl` (camera gate, c8 / c9 parallax) dots rows 0, 1, 3 only; the thin
box programs read no matrix row; `motion_output.cpp` memcpys the matrix. `expectedDepth` feeds `validDepth` (bound `<= 1`,
`resolve.hlsl:162,324`) and the disocclusion threshold `previous >= expectedDepth - max(1e-4, 0.02 |expectedDepth|)` (`:350,359`).
The far-plane matrix has a zero z column, so a valid-depth camera-path pixel gets the same `expectedDepth` as a sentinel: the
threshold moves from 0.98 to 0.979985, the permissive side, 1/1300 of the tolerance. Nothing assumes z row == w row or
`expectedDepth == 1`. The identity fallback matrix (policy 1 / failed transform) divides by w = 1 exactly and is untouched.
**Margin [I].** The quotient error of a reciprocal-multiply division, the float rounding of the scaled row and the two dots now
rounding separately are each a few float ulps (~1e-7 relative) for any on-frame pixel (w >= ~0.5; cancellation only near w -> 0,
which is off-frame). 2^-16 = 1.5e-5 is ~100x that.

**Fixture [M], bottle X3.** New case (g) in `camera_cases` (`verification/probe/temporal_pass_fixture.cpp`): all-sentinel sky, 2
deg/frame yaw, 48 frames, jittered, the installed age programs (thin clip 0.7, WMAX 0.97); current-only = age target == 1, frames
8-47, excluding pixels whose oracle history position is within 1.5 px of the border or outside, and |w - 1| < 1e-4. Row
`CAMERA_PAN`, one per generation; the runner asserts 510 numerical / 388 samples (was 508 / 386) and zero in the `w < 1` half.

| build | w < 1: current-only / px | w > 1: current-only / px | frames hit |
|---|---|---|---|
| unfixed (scale 1.0) | 1708 / 15680 (10.9 %) | 0 / 19200 | 33 of 40 |
| fixed | 0 / 15680 | 0 / 19200 | 0 |

The backend reproduces run215's split exactly (8-11 % there). A first detector (output == render bit for bit) was discarded: the
neighbourhood clip makes 0.4-0.6 % of accepted pixels equal the render in both halves.
**Other rows.** Against the committed summary, all non-timing values are identical except the `camera` rows, which call the same
builder and were losing the same history: jittered drift / error improve (yaw 0.124 -> 0.066 px / 0.0152 -> 0.0088, pitch 0.176 ->
0.057 / 0.0161 -> 0.0110, cut-resumed 0.100 -> 0.062); unjittered and single-step rows move by <= 0.005 px (more accumulated
resampling), all inside their unchanged tolerances. Host: `test_camera_reprojection` + `test_taa_camera_path` 17 tests OK (pins
updated: z / w = 1 - 2^-16, identity matrix m[11] = 0.999984741). Production scratch build OK, `check_no_x87.py`: no violations.
Not verified in flight; the replay's "fix" row above is the expected effect.

## Run 62 session A verdict (2026-09-22, run221)

Run62 DLL `924be5c0…` (far-plane `expectedDepth` fix, `--taa-sentinel-stabiliser 0.7`,
thin region 0.97): the user reports the distant-station shimmer/flicker under pans as
**fixed**. One F8 burst during a pan is preserved in `/tmp/x3-bottleX3-run221` (not
triaged; no open symptom). Lasers over sky were clean on Run 61 (run216). Follow-up:
make the sentinel stabiliser 0.7 the default with TAA and the camera gate (`off` opts
out), following the Run57/Run59/Run61 default pattern.

## 2026-09-22 thin-region emissive vote (R3): fixture

`--taa-thin-region-emissive E` / `X3M_TAA_THIN_REGION_EMISSIVE`, the luminance admission in the stabiliser mask
([taa-lattice-crawl.md](../architecture/taa-lattice-crawl.md) 32.7; design [thin-glow-lines.md](../architecture/thin-glow-lines.md)
8.3 R3). Default off. `E` is in the units of the scene the pass binds, which is the display-referred 8-bit target's FP16 copy
without `--hdr`: **the suggested `E = 1` needs the HDR route to have any effect**.

New fixture case `THIN_REGION_EMISSIVE` in `run_temporal_pass.py`'s lattice mode (scene "emissive" of
`temporal_thin_region_inc.h`): a routed dark hull (luma 0.16, depth 0.99) over rows [0, 20) of the sentinel, a 1.4 px bright strip
(luma 3) at x in [6.1, 7.5) on it (column 6 bright on every one of the 8 jitter phases, column 7 on 3 of them: the period-2 toggle
of a strip about a pixel wide), a 16x16 uniformly lit panel of the same luma 3, and an unrouted colour-only patch of luma 3 on the
sentinel. No 7-tap line in the window changes depth class twice, so FRAGMENTED is 0 everywhere and `b` carries the vote alone.
64 frames at rest, screen gate, `W = 0.97`, base weight 0.9.

| measurement | number |
| --- | --- |
| `E = 0` against the plain resolve, all frames and channels | max difference **0.000000000**; mask `b` max **0** |
| `E = 1` published strength against the CPU oracle (vote, 11x11 grow, 17x17 gate) | error **0.000000** (tolerance 0.5/255) |
| `E = 1` strip `b`, rows [2, 18), columns 6-7 | minimum **1.0000** |
| `E = 1` lit-panel interior `b` (x [20, 24), y [8, 12)) / unrouted sentinel emitter `b` | **0.0000** / **0.0000** |
| strip rest leak, rms frame-to-frame step of the resolved column 7, last 32 frames | `E = 0` **36.85** codes, `E = 1` **10.70** codes, ratio **3.4453** |

The ratio matches the IIR prediction 3.4 for 0.9 -> 0.97 (8.1 of the design note: 0.136 / 0.040 at the jitter fundamental,
0.0526 / 0.0152 at the period-2 tone this strip carries).

`THIN_REGION_EMISSIVE_NONFINITE` (same hull, 16 frames, `E = 1`): a 15x15 uniformly lit panel of luma 3 with a **NaN** pixel at its
centre, a **65504** pixel (above the resolve's `rejection.z` limit 65000) on the bare hull 7 px clear of the panel ring, and a
finite control peak. Only the panel's boundary ring is a local peak, so after the 11x11 grow the panel's core is exactly the NaN's
3x3: nothing can reach it from outside, and the NaN's eight neighbours have luma 3 > E with a finite 3x3 minimum of 3.

| measurement | number |
| --- | --- |
| NaN present in the readback / 65504 present | **1** / **1** |
| control peak `b` | **1.0000** |
| NaN's 3x3 (= the panel's ungrown core) `b` max | **0.0000** |
| 65504 pixel's 3x3 `b` max | **0.0000** |
| published strength against the CPU oracle over the whole window | error **0.000000** |

Both would read 1 if a non-finite tap counted as a low 3x3 minimum (a folded `max(NaN, 0)`) or if the 65000 limit were absent, so
the row is discriminating rather than merely quiet. Also in the case set: the pass refuses a negative or non-finite `E` while the
thin region is on and neither reads nor validates it with the region off; the hostile-state snapshot now covers `c10` and sampler 0
for both mask programs.

| 2026-09-22 | command | result |
| --- | --- | --- |
| launcher/option mapping | `/usr/bin/python3 verification/probe/run_host_suite.py --modules test_taa_image_defaults` | 16 tests, 0 failures |
| fixture | `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_temporal_pass.py` | `passed: true`; base mode 510 / 278 / 2, 388 samples; lattice `RESULT PASS numerical=583 state_restorations=23` (576 before these two cases) |
| host suite | `/usr/bin/python3 verification/probe/run_host_suite.py` | 226 modules, 2244 tests, 0 failing |
| scratch DLL (not a candidate) | `cmake -S . -B build-r3 -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPython3_EXECUTABLE=/usr/bin/python3 && cmake --build build-r3 --target d3d9 -j8`; `python3 verification/probe/check_no_x87.py build-r3/d3d9.dll` | 0 warnings; PASS, 636 reachable functions, 0 violations |

Mask program bytecode: plain `temporal_line_mask` 1196 -> 1401 DWORDs; camera `temporal_line_mask_camera` 1101 -> 1307 DWORDs
(`verification/results/temporal-line-mask{,-camera}-program.json` carry the sha256 of each).

## Run 235: SETA approach smear (2026-09-22)

Run `/tmp/x3-bottleX3-run235` (Run65 candidate 2d11aac4, `--sun-occlusion
--sun-occlusion-log --sun-occlusion-core-f --capture-delay 300`, TAA thin-region
0.97 camera gate, sentinel stabiliser 0.7, unmatched-static node, fog off).
Two F8 bursts captured (`capture_armed` -> `start_frame=5152`, `start_frame=6967`,
each 32 frames, 1280x768). Readback formats confirmed from the log:
`depth_1_*.rgba32f` carries the depth in channel R (sentinel `-1.0`, matching
`test_motion_readback.py`'s "same statistics from `.r`" contract for the
`rgba32f` depth-format variant); `motion_1_*.rgba32f` alpha is 1 valid / -1
sentinel with xy in **pixels** (not UV).

Burst 1 (5152-5183) is a near-collision frame: depth >=0.99 non-sentinel
geometry already covers the full 1280x768 frame every sample (390-391k px,
centroid stationary at ~661,569) - no open sky to smear against, not usable
for the disocclusion question.

Burst 2 (6967-6998) is the useful approach: the station's silhouette (depth
bucket 0.99-1.0, distinct from a fixed low-depth cockpit cluster at
0.938-0.963 that repeats near-identically between bursts) grows steadily,
leading edge moving from x=192 to x=83 over 31 frames, roughly -3 to -5 px/frame
(`probe1.py` left-edge trace).

**Motion buffer vs. measured silhouette speed.** Inside/around the station
region the motion vector magnitude never exceeds ~1.3-1.4 px across the whole
routed buffer for any sampled frame (max 1.2549/1.3341/1.3671 px at frames
6970/6980/6990; median ~0.9-1.0 px, p99 ~1.23-1.33 px), while the silhouette
itself moves 3-5 px/frame. The routed/alpha=1 fraction inside the bbox is
0.9993-1.0 (motion is present, not simply missing). The vectors are **3-5x too
small** for the true screen motion; the small growth of the max value frame
to frame (1.25 -> 1.33 -> 1.37) is not a hard round-number clamp.

**Smear geometry (dark = present darker than color by >40 luma, sky = current
depth sentinel).** Per frame (6968-6997) dark-sky count 1100-2200 px. Of the
truly newly-uncovered band (previous-frame station AND current-frame sky),
only ~5-10% goes dark (493/5592 at f6975, 421/8510 at f6985, 408/9312 at
f6990) - the one-sided closest-depth disocclusion test **is rejecting most of
the correctly-reprojected uncovered pixels** as documented (history in front
of current closest depth rejects). The remaining 75-80% of dark-sky pixels lie
**outside** the previous frame's station footprint, and a Chebyshev-distance
histogram to the current silhouette is bimodal: a normal edge band at distance
1 (~400-560 px, matches the pre-existing <=1px AA-edge finding) plus a large
tail at distance 13+ (693-1016 px, roughly half of all dark-sky pixels)
scattered away from any geometry boundary - not concentrated at the trailing
edge as pure in-place disocclusion (H1) would predict.

**Temporal decay.** 20 dark sky pixels sampled at f6975 and tracked 8 frames
in `taa_`: most decay very slowly (a few % per frame, e.g. 0.0625->0.0582,
0.2047->0.2026), far slower than a single bad injection at the documented 0.9
history weight would predict if immediately rejected thereafter, i.e. the
pixel keeps re-accepting bad history rather than being caught once and
cleared. A minority instead flicker frame to frame (values swinging between
~0.02 and ~0.33), consistent with jitter-driven resampling of nebula detail,
not the smear.

**Log correlation.** `motion_unmatched_static_frame` fires mid-burst
(frame=6969 `applied=38`, frame=6973 `applied=17 object_unknown=2`) - the
unmatched-static path is live in this exact window. `camera_cut=0` and
`cut_missing` ~0 throughout (no camera-cut invalidation event masks the
result). `gate6` (disocclusion rejections per `motion_output_frame`) is mostly
0 with occasional bursts (19-38) coincident with the unmatched-static frames,
i.e. rejection is intermittent rather than continuous while the station keeps
moving.

**Outcome:** the numbers do not support pure H1 (in-place disocclusion
ghosting): the correct newly-uncovered band is mostly rejected, and most dark
pixels lie away from both the current silhouette and the previous frame's
footprint. They support **H2** (motion vectors undersized for SETA-speed
approach, measured 3-5x too small versus the observed silhouette speed),
compounded by a persistent-history path: the `motion_unmatched_static_frame`
node is active in the same frames, and the slow multi-frame decay of many dark
pixels looks like repeated re-acceptance of stale history rather than a single
rejected disocclusion event. This evidence cannot on its own attribute the
mechanism to the unmatched-static node specifically versus another
per-pixel history-acceptance path; that requires a diagnostic dump of the
resolve's per-pixel accept/reject decision and the motion source (game vs.
unmatched-static-substituted) for the dark-sky pixel set, which this capture
does not carry.

## 2026-09-22: `--taa-thin-region-emissive` default 1

Run 65 session B (run236/run237) accepted the emissive vote in flight; the triage of the same runs put the cut of the
resolved rest-flicker leak at 4.4-6.9x. `tools/manage.py` now resolves an omitted `--taa-thin-region-emissive` to `1`
whenever `--taa`, `--taa-thin-region` with `W > 0` and `--hdr` are in effect, and leaves it absent without `--hdr`, where
the display-referred scene makes the vote inert; an explicit `0` is the opt-out. Covered by
`verification/analysis/test_taa_image_defaults.py` and a `launch --dry-run` of the Run 65 session B command.

## 2026-09-22 run235 SETA approach smear: diagnosis and the strict sky history fixture

Diagnosis in docs/architecture/seta-motion.md. Correction to the Run 235 section above: the
motion buffer's RG is the previous absolute UV, not a displacement; decoded per the ABI the
station's vectors are 3-38 px and match a block match of the depth silhouette within 0.31 +-
0.88 px (x) / 0.24 +- 0.63 px (y) over 9 windows, the cockpit decodes to 0.000 px. The
smear is the far-plane disocclusion test accepting the station's hull (depth 0.9997, within
the 2 % relative tolerance of 1.0) as sky history on the 400-600 freshly uncovered pixels
per frame (uncovered vs control sky, bright nebula: `present - colour` median -8.7 to -29.8
vs -0.4 luma). Fix: `--taa-sky-history strict` (X3M_TAA_SKY_HISTORY, resolve c7.z = 3),
default loose = bit-identical to before.

Fixture: `run_temporal_pass.py` edge case (d) "SETA sweep" (`SETA_SWEEP` lines): a black 8x8
square at depth 0.9997, static 8 frames then +5 px/frame for 4 frames over a textured sky
(colour only, sentinel depth, sentinel-fill background, camera path), loose and strict.

| 2026-09-22 | loose | strict |
| --- | --- | --- |
| uncovered trailing pixels (sky now, square last frame, no square in the current 3x3), 4 moving frames | 128 | 128 |
| max `|output - current|` on them (the smear) | **0.237305** (asserted >= 0.05) | **0.000000** (asserted <= 1/255) |
| motion target vs the 5 px displacement at covered pixels, max error | **0.000001** px (<= 0.1) | **0.000001** px (<= 0.1) |
| max difference strict vs loose eight pixels and more from the square | | **0.000000** (asserted 0) |
| dilated 1-px band beside the silhouette, max deviation (reported, the documented AA-edge behaviour) | 0.738525 | 0.738525 |

Edge case (e) "fade-band sweep" (`SETA_FADE`, review finding 1): the same square at value 0.6, routed and depth-writing
for 8 frames, then a fade-band draw (RT1 alpha 1 with its depth target, RT2 masked: current depth sentinel) moving
+5 px/frame; its frame-8 history taps land on its own depth of frame 7, the case strict must not reject.

| 2026-09-22 | number |
| --- | --- |
| pixels the fade-band draw routes over the 4 moving frames / of them with history in the output (strict) | **256** / **18** |
| max `|strict - loose|` on those pixels (asserted 0: the object's history is accepted under strict) | **0.000000** |
| max `|strict - loose|` on the sky beside it (the smear case, loose accepts the hull; reported) | 0.297363 |

| command | result |
| --- | --- |
| `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py /usr/bin/python3 verification/probe/run_temporal_pass.py` | `passed: true`; base mode **532 / 278 / 2**, **402** samples (510 / 388 before: four sequences with their motion-contract check and seven metrics per generation); lattice `RESULT PASS numerical=583 state_restorations=23`; `RESOLVE_BUDGET` age_line **511**, far_camera 509, age_filter 508, far 497, age 496 (HEAD: 509 / 508 / 506 / 496 / 494), all within 512. The age_line variant (the 512-slot one) is created by `CreatePixelShader` on this backend like every embedded program (`temporal_pass.cpp` `make(temporal_resolve_age_line_program(), &age_line_)`, dropped on failure) and a refusal fails soft: `run` refuses the line filter with the age weight as unavailable (`aged && !far_requested && !age_line_` in its input validation), it does not crash |
| `PYTHONPATH=verification/probe /usr/bin/python3 -m unittest verification/analysis/test_taa_sky_history.py` | 3 tests, 0 failures |
| resolve headers | `generate_rigid_motion_pixel.py` for the twelve resolve programs under the Wine lock, PASS (plain 1706 -> 1712 dwords) |
| review fixes (same day) | strict restricted to unrouted pixels (alpha not 1; fixture row (e)); `X3M_TAA_SKY_HISTORY` read requires 0 < length < 32; the term costs `sge`, `mad`, `sge`, `mul` and is paid for by `nearest >= 1` replacing `all(dilate == 0)` in the fill-pair test and `saturate(2 - 0.5 speed)` replacing `1 - saturate((speed - 2) * 0.5)` in the thin soft clip (both exact for the finite operands); rerun: `passed: true`, 532 / 278 / 2, 402 samples, lattice 583 / 23, `SETA_FADE routed_px=256 strict_vs_loose_on_routed=0.000000 routed_px_with_history=18` |
| scratch DLL (not a candidate) | `cmake -S . -B build-seta ... && cmake --build build-seta`; `check_no_x87.py` 0 violations; sha256 3b880ba5... |


## Run 244: strict sky history in flight (2026-09-22)

Run `/tmp/x3-bottleX3-run244` (Run66 candidate 1f9a85f5, `--taa-sky-history strict`,
sun occlusion with core dimming, widening 4,4, emissive vote 1, `--capture-delay 300`).
Log: `.../x3-modern-captures/session-20260922-195653-212.log`. `proxy_options` and
`motion_output_mode` both confirm `X3M_TAA_SKY_HISTORY=strict` (sky_history=strict).

**Bursts.** Three `capture_armed -> start_frame`: 3733, 7961, 9992 (32 frames each,
1280x768). Camera-state `t` deltas identify burst 1 (3733-3764) as the SETA leg:
constant translation ~862.5 units/frame on an unchanging heading (rotation_deg=0
throughout) with a steadily growing station silhouette, edge speed -2 to -6 px/frame
(cf. run235's -3 to -5 px/frame). Bursts 2 (7961-7992, ~172.5 u/frame, no heading
change) and 3 (9992-10023, 133-296 u/frame with `rotation_deg` climbing 0.10->0.74,
`r00` swinging 0.435->0.197) are non-SETA: burst 2 is a slow pan past a huge nearby
silhouette, burst 3 an accelerating turn. No `camera_cut` fires in any burst.

**Dark-sky pixels, SETA burst vs. run235 burst 2 (same method: dark = color-present
luma > 40, sky = depth sentinel, Chebyshev distance to current silhouette, 9999 =
distance >19):**

| | run235 burst2 (loose) | run244 burst1 (strict) | change |
| --- | --- | --- | --- |
| dark-sky px, sum over 31 frames | 56067 | 32505 | -42% |
| mean fraction in newly-uncovered band | 0.199 | 0.080 | lower |
| dist=1-2 (AA-edge / dilated band) | 17992 (32%) | 6648 (20%) | -63% |
| dist=3..12 | 10214 (18%) | 4202 (13%) | -59% |
| dist>=13 (scattered, away from any edge) | 27861 (50%) | 21655 (67%) | -22% |

Strict cuts the near-silhouette band hardest (-63%), matching the fixture's dilated-
1px-band prediction, but the far, disconnected tail (>=13 px, H2/unmatched-static
territory) drops only 22% and is now most of what remains (67% of dark-sky pixels).
The residual is **not** concentrated at distance 1-2; it is the same far-scattered
population documented in the run235 diagnosis, just smaller.

**Decay.** 20 dark-sky pixels sampled at f3740, tracked 8 frames in `taa_` luma:
most drift slowly (e.g. 0.0629->0.1181, 0.0245->0.0172) or even brighten further
(0.127->0.2361 then flat, 0.0693->0.2523) — no pixel clears within 1-2 frames.
Decay speed is unchanged from the loose-mode finding in the run235 section.

**Blur.** Present/color normalized-gradient-energy ratio (unit-consistent, both
bgra8) over the station bbox vs. a fixed sky patch (20:120,20:220):

| burst | station present/color | sky present/color |
| --- | --- | --- |
| 1 (SETA) | 0.518 (0.497-0.551) | 0.930 (0.866-0.990) |
| 2 (pan) | 0.487 (0.451-0.528) | 0.914 (0.855-0.949) |
| 3 (turn) | 0.294 (0.281-0.308) | 0.436 (0.252-0.540) |

The station consistently loses ~half its gradient energy relative to the
non-resolved frame (history reprojection/bilinear softening at the 3.5-30 px/frame
motion decoded below); the sky stays near 1.0 in the two lower-angular-rate bursts.
Burst 3's sky also drops (0.44) alongside the station, consistent with broader
softening under fast rotation rather than a station-only effect there. Blur is
station-localized in bursts 1-2, more global in burst 3.

**Motion vectors, SETA burst.** Decoded per the run235 ABI correction (`d = (p+0.5-j)
- motion.xy*(W,H)`) on routed/alpha=1 station pixels: f3740 median 3.93, p99 30.08,
max 31.15 px; f3750 median 3.54, p99 22.81, max 24.32 px; f3760 median 3.95, p99
17.09, max 18.43 px — in the same 3-31 px range as the corrected run235 decode, not
the old ~1.3 px undersized bug. `gate6` (disocclusion rejections) sums to 107 over
the burst (max 38/frame) vs. 16 and 13 in the two non-SETA bursts; three
`motion_unmatched_static_frame` lines fire in the SETA burst (3737 applied=38, 3740
applied=17, 3753 applied=16) and one in burst 2 (7979 applied=2); none in burst 3.
No `camera_cut` in any burst.

**Non-SETA pan check.** Per-frame mean luma in a 1-3px dilated ring around the
current silhouette vs. a fixed sky control patch, frame-to-frame std (flicker):
burst 2 ring std 2.96 vs. sky-patch std 0.10 (~30x); burst 3 ring std 2.52 vs.
sky-patch std 1.23 (~2x, burst 3's own sky is noisier from the turn). The ring is
consistently noisier than a matched sky patch in both non-SETA bursts.

**Outcome.** Strict removed 42% of SETA-burst dark-sky pixels, concentrated in the
near-silhouette band it targets; user-visible "still a little smearing" is
consistent with the untouched far-scattered tail (67% of what remains,
-22% only), which the diagnosis already attributed to a separate mechanism
(undersized/persistent-history path via `motion_unmatched_static`), not the
hull-acceptance case strict fixes. "A little blurry" is consistent with the ~2x
gradient-energy loss on the moving station from bilinear history reprojection at
3.5-30 px/frame — present in all three bursts, not new to strict or to SETA.

## 2026-09-22 band term: the dilated band beside a fast silhouette under strict (follow-up on Run 244)

Design and the flight ring numbers: docs/architecture/seta-motion.md section 4. Run 244's
residual near band (dist 1-2 dark-sky pixels 6,648 over 31 frames, "a little smearing") is the
fixture's `adjacent_max 0.738`: the dilated band takes the neighbour's correspondence, so the
strict term (`nearest == 1`) never reached it. `resolve.hlsl`: an **unrouted** far-plane pixel
in the dilated band (`band`, read before the alpha scaling, cleared when the pixel's own motion
alpha is 1: a fade-band or glow draw beside closer geometry keeps its path) whose routed
correspondence differs from the **rotation-only camera path** (`cameraUV`: the resolve's own
reprojection before the motion override, which the route feeds with
`camera_far_plane_reprojection`, zero z column, no translation, so it is the direction-at-
infinity path whatever `nearest` is) by at least the band threshold is refused under strict
(`considered` starts at `refused * c7.z`). That difference is the translation parallax: the SETA
approach (3-38 px/frame, rotation 0) refuses, a pan (rotation) gives 0 and keeps accumulating.
The threshold is a constant lane, c5.y = px^2 (`X3M_TAA_SKY_HISTORY_BAND_PX`, 1..16, default
**3**; `--taa-sky-history-band-px`, requires `--taa`; the DLL logs it as `sky_history_band_px`
in `motion_output_mode`, invalid values fall back to 3 with `taa_sky_history_band_px_setting
invalid=1`), so it can be A/B'd in flight. Against the captures (routed displacement of the
station bucket = the parallax, rotation 0): run244 f3740 / 3750 / 3760 median 3.93 / 3.54 /
3.95 px/frame, run235 f6970 / 6978 / 6986 / 6990 median 3.14 / 4.31 / 7.55 / 8.75; hull share
below 3 px/frame 0.40-0.43 (run244) and 0.34-0.49 (run235), below 2 px/frame 0.31-0.43 and
0.32-0.35; 2 px is ~2 sigma of the route's block-match error (0.3 +- 0.9 px/frame), which a
world-static border could cross sporadically, hence 3. Consequence to watch in the next strict
flight: a camera translation whose parallax at a nearby world-static silhouette exceeds 3
px/frame (close flyby, docking approach) puts that 1-px sky border on the current-only path:
intended for uncovered sky, less anti-aliased than loose. Forms measured and rejected on the
way: lowering the tolerance so only sentinel taps prove (`adjacent_max` 0.738 -> 0.569: the
sentinel-depth texel beside the previous edge carries that edge's accumulated hull share), a
screen-speed gate (current-only on the border of every static silhouette during a pan), and a
2 px threshold as a shader constant.

| `run_temporal_pass.py`, 2026-09-22 | loose | strict |
| --- | --- | --- |
| (d) SETA sweep, 5 px/frame: `adjacent_max` (dilated band, max deviation from the current sample) | 0.738525 | **0.000000** (asserted <= 0.10; was 0.738525) |
| (d) `trail_max` / `far_difference` / `motion_error_px` | 0.237305 / - / 0.000001 | 0.000000 / 0.000000 / 0.000001 (unchanged) |
| (e) fade-band sweep: strict vs loose on the 256 routed pixels / routed pixels with history | | 0.000000 / 18 (unchanged) |
| (f) slow sweep, 0.5 px/frame, 8 moving frames: max strict - loose over every pixel and frame (asserted 0); the band's deviation from the current sample (the anti-aliased edge, both modes) | | **0.000000**; 0.805786 |
| (g) static ring, two 16-phase periods: per-pixel temporal variance of the last period, distance 1 / 2 / 3 / sky (40 / 48 / 56 px) | 1.843e-5 / 2.188e-5 / 1.586e-5 / 3.744e-5 | identical; max strict - loose over 32 frames **0.000000** (asserted 0) |
| (h) pan, camera 3 px/frame past the world-static square, 8 frames: border deviation from the current sample (accumulated, required > 0.05) / max strict - loose over every pixel (asserted 0) / sky pixels using history under strict | 0.815308 | 0.815308 / **0.000000** / 3570 |
| (i) fade-band strip beside a 5 px/frame depth-writing occluder, 7 frames: routed strip pixels / of them beside the occluder / current-only among those, loose / strict (asserted strict - loose = 0; without the alpha gate strict would refuse all 31) | 1344 / 31 / 15 | 15 (**+0**) |
| (j) projective pan (focal 16, yaw 0.6714 deg = 3.0033 px/frame at the centre, far-plane matrix form, z row = w row x (1 - 2^-16)): border accumulated / max strict - loose (asserted 0) | 0.815308 | 0.815308 / **0.000000** |
| (k) camera path off the scale (x row 1e15): band pixels / band deviation from the current sample loose / strict (asserted 0: fail closed) / nonfinite outputs in both modes (asserted 0) | 252 / 0.815308 | **0.000000** / **0** |

Flight (seta-motion.md section 4, table): ring luma flicker under strict (run244 burst 2) is the
same order as under loose (run235 burst 2), 0.72 / 1.08 / 0.93 against 1.27 / 0.82 / 0.79 at
distance 1 / 2 / 3 with far-sky controls 0.35 and 0.08, and the only pixels the strict term can
refuse (geometry within 1 px last frame, none in the 3x3 now) are current-only at 0.0-0.1 %:
the ring is pre-existing and not changed. The triage's 2.96 was measured on a 1-3 px ring that
included the freshly uncovered band and the cockpit's border; this measurement excludes both.

| command | result |
| --- | --- |
| `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py /usr/bin/python3 verification/probe/run_temporal_pass.py` (band term, parallax gate, alpha gate, c5.y threshold) | `passed: true`; `RESULT PASS numerical=570 state_restorations=278 generations=2`, **416** samples (532 / 402 before: one SETA-sweep metric, then the slow sweep, the static ring, the pan, the fade-band-beside-occluder, the projective pan and the huge camera path, two sequences and one metric each, per generation); lattice `RESULT PASS numerical=583 state_restorations=23` |
| the same, unchanged source, same session (baseline for the bit-identity check) | `passed: true`, 532 / 278 / 2, 402 samples |
| bit-identity of the three shared-code rewrites (tap proof as a step/dot product with `threshold = max(expected - tolerance, 0)`; usability test as two float3 range steps; `valid` as a count) against the same-session baseline, whole-line diff with nothing filtered | temporal-lattice.txt: 5242 lines both, 18 removed / 18 added, of which 14 are the `RESOLVE_BUDGET` lines and 4 timing lines; every other line identical, all 375 SAMPLE lines unchanged. temporal-pass.txt: 5172 -> 5576 lines, **3 removed** (the two strict `SETA_SWEEP` rows, adjacent_max 0.738525 -> 0.000000, and the `RESULT PASS numerical=532` line, now 570) / 407 added (376 CHECK lines and the new SETA rows and SAMPLE metrics of the new sequences); all **402** baseline SAMPLE lines present unchanged |
| `RESOLVE_BUDGET` (temporal-lattice.txt) | age_line 511 -> **507**, far_camera 509 -> **507**, age_filter 508 -> **506**, far 497 -> 495, age 496 -> 494, thin 470 -> 465, thin_filter 482 -> 477, thin_line 485 -> 483, line 447 -> 447, plain 434 -> 432, current_filter 445 -> 443, snapshot / line masks / thin boxes unchanged; all within 512 |
| slot accounting (scratch D3DXDisassembleShader listings of resolve_age_line.hlsl) | screen-speed form 509; parallax form 514 before the usability rewrite, 510 after; with the alpha-gate fetch and the c5.y lane 513 before the `valid`-as-count rewrite, **507** after; tap proof 15 -> 9 arithmetic slots; usability test 19 -> 7; a vectorised Catmull-Rom weight chain compiled to 17 against 11 and was reverted |
| resolve headers | `generate_rigid_motion_pixel.py` for the eleven resolve programs that include resolve.hlsl, under the Wine lock, PASS |
| `PYTHONPATH=verification/probe /usr/bin/python3 -m unittest verification/analysis/test_taa_sky_history.py verification/analysis/test_taa_image_defaults.py` | 23 tests, OK (three new: `--taa-sky-history-band-px` forwarded as given, omitted / inherited not forwarded, bounds 1..16 and the `--taa` requirement rejected) |
| `/usr/bin/python3 verification/probe/run_host_suite.py` | 230 modules, 2284 tests, 0 failing |
| scratch DLL (not a candidate) `build-band`, `-DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPython3_EXECUTABLE=/usr/bin/python3` (final shader, headers and plumbing; the later fixture-only edits do not enter the DLL) | built; `check_no_x87.py` 638 reachable functions, 0 violations; sha256 25c6fff3... |

Blur (report only, no change): the history fetch is already the 16-tap Catmull-Rom (fixture
case (c): amplitude retained at 0.25 px/frame 0.930 against 0.712 for the bilinear model), so
the triage's "bilinear history reprojection" premise does not hold. The 5- and 9-tap
Catmull-Rom forms rely on hardware bilinear filtering of the history sampler, which the
point-sampled contract of s2 excludes; adopting one would *save* 7 (9-tap) or 11 (5-tap)
texture slots plus weight arithmetic, at the price of a sampler-state contract change and
FP16 filtering precision, and would not sharpen anything. The station's present/colour
gradient-energy ratio of 0.3-0.5 at 4-30 px/frame is consistent with the vector error of
section 1 (0.3 +- 0.9 px per frame, block match) integrated at weight 0.9, and with the 3x3
clip, not with the filter kernel; the post-resolve RCAS (`--taa-sharpen 0.75`) acts on the
presented image only and cannot recover misregistered history.

Open: unrelated observation from the same measurement: run244's sky
is current-only at 6-8 % out to 6 px from the station (1.1-1.5 % in run235, 2.4-6.3 % on far
sky in both), on pixels the strict term cannot refuse; not investigated.

## Run 249: strict + band term in flight (2026-09-22)

Run 67 session A, Run67 DLL `621cad63…`, `/tmp/x3-bottleX3-run249` (`--taa-sky-history strict`, band
threshold left at its default). [M] = measured by the named script, [I] = inferred. Scripts and their
outputs: `verification/results/run249-band/` (`<script>.py` beside `<script>_out.txt`; `ring_flicker.py`
writes `ring_out.txt`). The first `motion_output_mode` row carries `sky_history=strict
sky_history_band_px=3.00` [M] (`log_facts.py`).

**Bursts** [M] (`burst_log.py`, input pre-filtered by `extract_log.sh`), 32 frames each, no `camera_cut`:

| burst | frames | translation per frame, median (min-max) | `rotation_deg` | `gate6` sum |
| --- | --- | --- | --- | --- |
| SETA 1 | 3515-3546 | 862.5 u (36.2-862.5) | 0.197 constant | 157 |
| normal speed | 4150-4181 | 172.0 u (22.3-172.7) | 0.000-0.244 | 42 |
| SETA 2 | 5538-5569 | 862.5 u (108.7-862.5) | 0.212 constant | 100 |
| pan | 9267-9298 | 96.6 u (10.7-501.7) | 0.136-2.350 | 153 |

The pan burst is a turn with a translating chase camera, not a rotation-only pan [I] (the translation above).

**Dark-sky census** [M] (`darksky.py`, method of the Run 244 section, 32 frames from the given start):

| burst | dark-sky px | d 1-2 | d 3-12 | d >= 13 | mean uncovered fraction |
| --- | --- | --- | --- | --- | --- |
| run235 b2 (loose), 6967 | 56,072 | 18,153 (32 %) | 10,797 (19 %) | 27,122 (48 %) | 0.199 |
| run244 b1 (strict), 3733 | 32,505 | 6,767 (21 %) | 4,775 (15 %) | 20,963 (64 %) | 0.080 |
| run249 SETA 1, 3515 | 26,056 | 3,320 (13 %) | 4,155 (16 %) | 18,581 (71 %) | 0.053 |
| run249 SETA 2, 5538 | 26,676 | 1,625 (6 %) | 4,509 (17 %) | 20,542 (77 %) | 0.008 |
| run249 normal, 4150 | 46,872 | 5,842 (12 %) | 3,165 (7 %) | 37,865 (81 %) | 0.045 |
| run249 pan, 9267 | 38,507 | 2,924 (8 %) | 1,710 (4 %) | 33,873 (88 %) | 0.031 |

Against run244 b1 the SETA totals fall 20 % / 18 % and the 1-2 px band 51 % / 76 %; the 3-12 px band only
13 % / 6 % and the far tail 11 % / 2 % [I: ratios of the rows above]. These totals differ slightly from the
Run 244 table (32,505 total, but 6,648 / 4,202 / 21,655 by distance there, over 31 frames); compare run249
only with the `darksky.py` rows. The normal-speed comparator (run244 b2, 7961: 44,512 dark-sky px, 16,194 /
3,669 / 24,649) is the first row of `far_tail_out.txt` [M] (`far_tail.py` reruns the same census).

**Band engagement by parallax** [M] (`band_parallax.py`: sky pixels at distance 1, binned by the routed
displacement of the nearest routed neighbour, px/frame; current-only proxy `taa == hdr`):

| bin | run244 b1: current-only / dark | run249 SETA 1: current-only / dark | run249 SETA 2: current-only / dark |
| --- | --- | --- | --- |
| < 2 | 0.0 % / 1,682 | 0.0 % / 2,025 | 0.0 % / 290 |
| 2-3 | 0.0 % / 382 | 8.7 % / 489 | 13.5 % / 205 |
| 3-6 | 0.1 % / 391 | 90.6 % / 38 | 96.6 % / 39 |
| >= 6 | 3.9 % / 3,443 | 100.0 % / 0 | 100.0 % / 0 |

The band term does what it was built for: at >= 3 px/frame parallax the distance-1 border is 91-100 %
current-only and carries almost no dark pixels; below 2 px/frame it never engages, and that bin now holds
most of the remaining distance-1 dark pixels. Distance-1 current-only share over the whole burst [M]
(`band.py`): 0.98 % run244 b1, 37.4 % / 66.5 % run249 SETA 1 / 2.

**Border flicker** [M] (`ring_flicker.py`, mean |present luma(f) - luma(f-1)| on sky present in both frames;
comparable only across runs with this script):

| burst | d1 | d2 | d3 | far control | d1 current-only |
| --- | --- | --- | --- | --- | --- |
| run244 b2 (7961) | 3.86 | 0.49 | 0.38 | 0.23 | 0.1 % |
| run249 normal (4150) | 3.43 | 0.58 | 0.38 | 0.27 | 56.5 % |
| run244 b3 (9992) | 18.00 | 6.63 | 6.02 | 4.51 | 0.9 % |
| run249 pan (9267) | 9.23 | 6.58 | 6.14 | 7.17 | 33.0 % |

Normal speed: the border is no noisier than run244's (3.43 against 3.86) although more than half of it is
now current-only. Pan: 33 % of the border is current-only because the chase camera translates [I]; its d1 /
far ratio is 1.3 against 4.0 in run244 b3 [I: ratio of the rows], i.e. the border flickers barely more than
the sky of the same burst. The same script's parallax bins for the pan (99.6 % current-only below 2 px/frame)
are not a parallax under rotation and are not read as one.

**Diagnosis of the 3-12 px band** (the population the band term cannot reach). Frames 3523 / 3530 / 3540 of
SETA 1:

- Camera path [M] (`mid_band_chain.py`): the far sky's phase-correlation shift f-1 -> f is (0, 0), peak
  0.886-0.912, so the unrouted sky's history lookup is the identity on this leg.
- What the pixels are [M] (`mid_band_mech.py`): 193 / 204 / 78 dark pixels, all unrouted (`motion.w = -1`),
  none current-only, none covered by geometry on f-1; `taa / hdr` median 0.48-0.54 against 0.999 for all sky
  at 3-12 px.
- Temporal signature [M] (`mid_band_series.py`): current HDR coefficient of variation 0.37-0.40, lag-1
  autocorrelation -0.02 to 0.00, output 0.85-0.91 of the pixel's own 8-frame HDR mean; the >= 13 px control
  gives lag-1 -0.02 and 0.84-0.89. Frame-to-frame flicker, not a moving feature, and the same statistics as
  far sky. The 1-2 px band (`near_band_series.py`) sits at 0.60-0.74 of its own mean.
- Refined criterion [M] (`dark_vs_own_mean.py`: dark and output below 0.7 x own 8-frame HDR mean), frames
  f0+7..f0+31:

  | burst | d 1-2 | d 3-12 | d >= 13 |
  | --- | --- | --- | --- |
  | SETA 1 (3522-3546) | 1,455 of 2,755 (53 %) | 914 of 3,386 (27 %) | 2,697 of 14,644 (18 %) |
  | SETA 2 (5545-5569) | 656 of 1,169 (56 %) | 1,146 of 3,407 (34 %) | 2,476 of 15,875 (16 %) |

  So 66-73 % of the 3-12 px dark pixels are ordinary accumulation of flickering sky, the same as the far
  tail [I: complement of the refined share].
- The refined 27-34 % [M] (`mid_band_refined.py`): of 37 / 41 / 22 refined pixels, 20 / 32 / 9 were covered
  and 13 / 5 / 9 were in the 1-2 px band at some point of the previous 8 frames (4 each only ever at 3-12 px);
  their silhouette distance grew by a median 4 / 3 / 2 px over those 8 frames (0.25-0.5 px/frame), against 0
  for the rest. A genuine hull share that entered history while covered or in the band below the 3 px/frame
  threshold, carried outward as the silhouette recedes [I].
- Mechanism in `src/temporal/resolve.hlsl` [I: reading of the source against the numbers above]: `band` is
  set only for the dilated pixel (line 308) and refused only at >= the band threshold (line 384); the strict
  tolerance applies to the pixel's own path (line 440); one pixel further out the depth proof passes on
  sentinel taps (lines 449-464); the 3x3 clip box of flickering sky (mean +- gamma sigma, softened by the far
  stabiliser, lines 539-564) contains the darkened history; the keep weight up to the far stabiliser's 0.985
  (lines 575-598) then decays it slowly.

**Outcome.** Strict + band stay opt-in (not the default). The band threshold is not the lever: the residual
enters below it or while covered and survives by ordinary accumulation outside the band. The fix is a design
question (`docs/architecture/seta-sky-hull-share-decay.md`, pending). Flight acceptance for that fix: the
refined 3-12 px share (`dark_vs_own_mean.py`) falls to the far-tail level of 16-18 %. User verdict: SETA
still smears a little; normal speed and the pan looked clean.

## 2026-09-22 exit reset: the strict sky history's hull share leaves in one frame (fixture)

Implements `docs/architecture/seta-sky-hull-share-decay.md` (Fable, scratch build `build-exit`, not a candidate;
`docs/architecture/seta-motion.md` section 5 points here). Option `--taa-sky-history-exit-px P`
(`X3M_TAA_SKY_HISTORY_EXIT_PX`; requires `--taa`, `--taa-sky-history strict` and an age program:
`--taa-far-stabiliser`, `--taa-thin-region` or `--taa-adaptive-weight`; 0 off, else 0.125..the band threshold;
**default off**, 0.25 the flown candidate). The DLL logs it as `sky_history_exit_px` in `motion_output_mode`,
refuses a value without strict (`taa_sky_history_exit_px_setting refused=1 reason=requires_strict`) or out of range
(`invalid=1`), and drops it per device without an age program (`motion_output_taa_sky_history_exit ... unavailable=1
reason=no_age_program`). The pass uploads c25.x = P^2 with c24 as one two-register block on the age programs, and
1e30 whenever the option is off or the strict term is not in effect (loose, policy 1), so the age target's bytes
are those of the option off there.

**Two deviations from the note, both inside `resolve.hlsl`'s age variants only.** (1) The note's reserve
(`step(0.5, f)` for the age tap's rounding) saves nothing with this compiler (every `step` compiles to add + cmp);
the reserve applied instead is two exact rewrites, proven on the existing rows before the term: the age write
splats the count to every lane (R32F stores .x only, same bytes, one instruction fewer) and the [1, 64] range test
becomes `age <= 64 ? age : 1` on `abs(age)` (this program writes 1..64 only and s7 is bound only behind a valid
history every pixel of which it wrote, so no |age| below 1 is readable; a NaN still restarts). (2) The reset is
applied at the blend (`keep` 0 through age 0 on the adaptive variants and an explicit select on the far variants,
count restarted), not by starting `considered` at 3: with the age tap read before the depth verdict the D3DX
optimiser regrouped the Catmull-Rom weight polynomials of the age variants (a different mul/mad order), and the
lattice check "past the speed gate the thin-region run is the plain resolve bit for bit" failed on the drifting
shards (fractional footprint; the static square stayed identical). The reset predicate is one `max(tolerance,
ageRaw) >= 0` ("keep"): `tolerance` is below 0 exactly on a strict-sky pixel under strict (the far-plane term took
3 off it), the texel below 0 exactly when marked. The output of a reset pixel is the current sample exactly at
k = 0 (`x + 0 * (old - x)`), within rounding on the HDR route (`unweigh(weigh(x))`); the history taps are still
fetched that frame (no early return). The band term's `band` is not folded with c7.z (that fold cost 5 slots on
age_line): the loose guarantee is the 1e30 upload, and a band pixel under loose would need a correspondence 1e15
px or more from the camera path (a camera path at `prepare`'s 1e15 bound) to be marked at all, a mark nothing
reads there (the reset is gated by `tolerance`, the count read through `abs`).

Reserve-only run (committed fixture and runner, HEAD shader plus the two rewrites; `compare_reserve_out.txt`):
`passed: true`, 416 samples. "0 removed / 0 added" below means: `compare.py` sets aside the lines that change by
construction (`RESOLVE_BUDGET`, `LOOP_TIMING`, `LINE_TIMING`, `CHECK`, `RESULT PASS`, the `*_BASE` counters and the
new `SETA_EXIT` rows) and compares every other line, the SAMPLE, metric and per-case rows, as a set against the
committed report. Raw counts: temporal-pass.txt 5576 lines both, temporal-lattice.txt 5242 lines both, **0 stable
lines removed / 0 added** in each, all 416 + 375 baseline SAMPLE lines present; slots age 494 -> 489, age_filter
506 -> 501, age_line 507 -> 502, far 495 -> 490, far_camera 507 -> 502 (the reserve alone).

Fixture (`X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_temporal_pass.py`,
worktree `agent-ac09e913c46fd2dd7`, results untracked there; `verification/results/exit-reset/compare.py` and
`compare_out.txt` hold the comparison): `passed: true`; `RESULT PASS numerical=672 state_restorations=278
generations=2`, **488** samples (570 / 416 before: nine age-program edge sequences and fourteen metrics per
generation, then six far / far_camera sequences and eleven metrics); lattice `RESULT PASS numerical=583
state_restorations=23`. Off-path bit-identity (same set comparison
of the stable lines, the volatile prefixes above set aside): temporal-lattice.txt 5242 lines both, **0 stable lines
removed / 0 added** (every FLICKER, LINE_FILTER, FAR_STABILISER, THIN_REGION, SENTINEL_STABILISER and SAMPLE row
identical, the far-camera static-camera `age_identical=1` rows included); temporal-pass.txt 5576 -> 6886 lines, 0
stable lines removed / 72 added (the new metrics; the other added lines are CHECK and SETA_EXIT rows), all 416
baseline SAMPLE lines unchanged (`adjacent_max` 0.000000 band row, (f)-(k) `strict - loose == 0` rows, pans, static
ring).

Case (l), `SETA_EXIT` rows (age program: thin clip 0.7, WMAX 0.9; strict + band 3; 8x8 square of colour (0, 0, 0.5)
at depth 0.9997, static 16 frames then +0.5 px/frame for 20 over a 1 / 0 sky whose texel flickers with period 3;
"cast" = max B - R over trail pixels, 0 for a sky-only history; "fresh" = a strict-sky pixel that was in the band on
the previous moving frame):

| mode | fresh px / current-only | trail cast max | negative ages (static / off-band / band-blend positive) | hull reads own marked texel / continued / unexplained | vs the other run |
| --- | --- | --- | --- | --- | --- |
| straight, exit off (today's strict) | 204 / **0** | **0.118408** | 0 (0 / 0 / 720 band pixels on the blend path unmarked) | 0 | control sky identical to exit on |
| straight, exit 0.25 | 204 / **204** | **0.000000** | **720** = every band blend pixel (0 / 0 / 0) | 162 / **162** / **0** | output diff 0.759 on the trail, age diff 72 |
| 0.125 px/frame, exit 0.25 (below the floor) | 212 / 0 | 0.121948 | **0** | 0 | |
| yaw +0.3 px/frame (f = 0.7), exit off / on | 242 / 0 -> 242 / **242** | 0.112305 -> **0.006714** | 0 -> 720 (0 / 0 / 0) | 190 / 117 / **0** | |
| yaw -0.3 px/frame (f = 0.3), exit off / on | 205 / 48 -> 205 / **205** | 0.063965 -> **0.051392** | 0 -> 720 (0 / 0 / 0) | 163 / 163 / **0** | |
| loose, exit 0.25 uploaded (pass forces 1e30) | 204 / 0 | 0.118408 | 0 | 0 | **output diff 0 / age diff 0** against loose without it |
| **far** program (0.985, far gate d0 0.9995 .. 0.9999), exit 0.25: straight / yaw +0.3 / yaw -0.3 | 204 / **204**, 242 / **242**, 205 / **205** | **0.000000** / 0.018555 / 0.087769 | 720 each (0 / 0 / 0) | 162 / 162 / **0**, 190 / 117 / **0**, 163 / 163 / **0** | |
| **far_camera** program (thin region 0.97, camera gate: the flown configuration), exit 0.25: straight / yaw +0.3 / yaw -0.3 | 204 / **204**, 242 / **242**, 205 / **205** | **0.000000** / 0.018188 / 0.086670 | 720 each (0 / 0 / 0) | 162 / 162 / **0**, 190 / 117 / **0**, 163 / 163 / **0** | |

Asserted (each generation, the age, far and far_camera on rows alike): fresh_current_only == fresh_px, trail cast 0
(straight), negative_static + negative_not_band 0, band_blend_positive 0, hull_mark_unexplained 0, control identical
to exit off, no mark below the floor, loose bit-identical on both targets. The far programs' yaw casts are larger
than the age program's (0.019 / 0.088 against 0.007 / 0.051): their history weight on the sky is the base 0.9
without the adaptive ramp, so the neighbour's re-imported share decays more slowly; the reset itself is identical. The yaw rows answer the note's open item 3: the reset itself is
unchanged under rotation (the nearest age texel is the pixel's own at 0.3 px/frame), but the fractional
Catmull-Rom footprint re-imports the band's share: 6 % of the exit-off cast when the footprint's negative lobe
(-0.07) reaches the band (yaw +0.3), **80 %** when its 0.29 lobe does (yaw -0.3, content moving toward the trail).
The note's fallback (`keep` 0.5) does not address this (the share enters through neighbours, not the pixel's own
history); a per-tap mark read would cost 16 taps. The SETA leg itself has rotation 0 (run249 `mid_band_chain`), so
the straight row is the flown case; a turning leg keeps a residual.

| check | result |
| --- | --- |
| `RESOLVE_BUDGET` (fixture, D3DXDisassembleShader) age / age_filter / age_line / far / far_camera | 494 -> **495**, 506 -> **507**, 507 -> **507**, 495 -> **498**, 507 -> **510** of 512; plain 432, current_filter 443, thin 465, thin_filter 477, line 447, thin_line 483, snapshot 46, masks 368 / 344, boxes 51 / 35 / 94 unchanged (bytes unchanged: the non-age headers did not regenerate) |
| `LINE_TIMING_CAMERA` (1280x768, 6 rounds; the far-camera program carries the term) no_region thin / camera / delta, fragmented pan thin / camera / delta | 1.4375 / 1.6160 / 0.1785, 1.2554 / 1.7464 / 0.4910 ms before; **1.4712 / 1.6113 / 0.1401, 1.3329 / 1.8870 / 0.5541** ms after, from the retained `verification/results/bottle-X3/temporal-lattice.txt` (run-to-run noise of this CPU-wall measurement; `LOOP_TIMING_SUMMARY` belongs to the loop-qualify mode, which times the unchanged plain program) |
| scratch DLL `build-exit` (`cmake/mingw-i686.cmake`, RelWithDebInfo), no warnings; `check_no_x87.py` | built; PASS, 638 reachable functions, 0 violations; sha256 `8b19f6ce2a0c780d6cdf655df48c75e0d36dd85d5155ae8186f3cc5934fed2f9` (after the review fixes: a non-numeric `X3M_TAA_SKY_HISTORY_EXIT_PX` logs `invalid=1`) |
| `PYTHONPATH=verification/probe /usr/bin/python3 -m unittest verification.analysis.test_taa_image_defaults verification.analysis.test_taa_sky_history verification.analysis.test_shader_compiler_provenance` | 28 tests, OK (four new: exit forwarded as the float given with each age program; 0 accepted as the explicit off with `--taa` alone; omitted / inherited dropped; range 0.125..band and the `--taa`, strict and age-program requirements refused; DLL default 0) |
| `--taa-debug` age readback consumers | `tools/analysis/taa_sentinel_pan_replay.py` and `taa_sentinel_pan_variants.py` seed the replay with `abs()` of the `taa_age` dump (a negative count is the mark); no host test covers them |

## Run 254: exit reset in flight, accepted (2026-09-23)

Run 68 A (`/tmp/x3-bottleX3-run254`, Run68 DLL `39c8c70d…`): the Run 67 A command plus
`--taa-sky-history-exit-px 0.25` (strict + band 3 px + exit reset). Three F8 bursts:
SETA 1 (5496–5527, logged yaw 0.2549°/frame but no measurable sky shift), normal speed
(6875–6906), SETA 2 (11177–11208, straight). The user reports no SETA smearing and a clean
normal-speed flyby and pan. Scripts and outputs: `verification/results/run254-exit/`
(`run_all.sh` reproduces them; the run249 scripts reused, plus `age_census.py`,
`age_takers.py`, `hull_sharp.py`, `hull_blurfit.py`, `hull_region_split.py`). All measured.

- **Acceptance met.** `dark_vs_own_mean.py` 3–12 px genuine dark-sky share: SETA 1
  132/2,010 (7 %), SETA 2 101/1,657 (6 %), against run249's 27 % / 34 % and its 16–18 % far
  tail; this run's own far tail (≥13 px) is 9–11 %. Total dark sky 22,189 / 19,560
  (run249: 26,056 / 26,676). Of the remaining d1–2 dark pixels, 82–87 % are band pixels
  still blending history and carrying the mark.
- **Reset active.** `motion_output_mode` logs `sky_history=strict sky_history_band_px=3.00
  sky_history_exit_px=0.250`; no refusal. Marked band pixels per frame (median) 2,330 /
  1,988 / 4,724 (all sky at distance 1–2, none on hull); pixels leaving the band and taking
  the reset 570 / 591 / 881 per frame, 99–100 % restarted at age 1, 74–77 % current-only to
  the bit (the rest HDR-route rounding as designed).
- **Band behaviour unchanged from run249:** 3–6 px bin 91.6 % / 92.3 % current-only,
  ≥6 px 100 %; border flicker at d1 2.94 / 2.41, far sky 0.01.
- **Normal-speed burst is a different scene:** every d1 sky pixel sits below 3 px/frame of
  parallax, so the band never engages (run249's normal burst had most of its border at
  ≥3 px/frame); d1 flicker 6.23 vs 3.43 and uncovered fraction 0.207 match the pre-exit
  run244 b2 (0.185) and are scene, not regression (inferred; that path is bit-identical on
  the fixture with the option on or off). No pan burst was captured. A like-for-like
  normal-speed check stays open.
- **Anomalies:** none; 96/96 frames per target, 1,248 readbacks `result=00000000`, no
  device or TAA failures, 789 `taa_invalidate` rows all `site=not_resolved` outside bursts.
- **Station blur under SETA (the user's question):** hull sharpness ratio (output / pre-
  history HF energy) 0.44 / 0.46 / 0.35 below 0.5 px/frame, 0.25 / 0.38 / 0.12 at 0.5–1,
  0.02–0.10 at ≥1 px/frame in every burst including normal speed; Gaussian-equivalent σ
  0.5–0.7 px at rest, 1.0–1.4 px at ≥1 px/frame, explaining 84–89 % of output−input, so it is
  resample softening of long-lived history (median age 48–64; thin-region b=255 on 52–64 %
  of moving hull gives σ 1.4, b=0 σ 1.0), not ghosting. `taa_weight=0.900`, sharpen and mip
  bias −0.5 constant across motion bins. Hull motion p50/p90/p99: SETA 0.46/12.6/26.5,
  normal 0.38/0.99/1.64 px/frame. Design note: `docs/architecture/taa-motion-history-weight.md`.

**Decision (2026-09-23):** strict sky history, the 3 px band term and the exit reset at
0.25 become the launcher and DLL defaults (`--taa-sky-history loose` and
`--taa-sky-history-exit-px 0` opt out). The age/mask readbacks of the three bursts were
copied from the bottle's capture folder into the run directory (identical colour bytes).

**Defaults merged (2026-09-23):** `--taa-sky-history strict` and `--taa-sky-history-exit-px 0.25`
are the launcher and DLL defaults whenever TAA is on (0.25 only under strict with an age
program, i.e. with `--taa-far-stabiliser` or `--taa-thin-region`; a plain `--taa` launch
resolves 0). Evidence: `verification/results/sky-history-default/dry_run_env_out.txt`
(four cases) and `compare_motion_output_out.txt` (motion-output suite 190 cases / 271,369
checks identical to the committed run after the fixture mirror and runner were aligned);
one review (Opus) with fixes; post-merge gate: host suite 231 modules / 2,333 tests with
only the two installer-lock modules failing while the user's game held the lock and
`test_bob1` skipping its live-overlay case, production scratch build OK, x87 638 reachable /
0 violations. All measured.

## 2026-09-23 cleanup batch 6: dead resolve variants removed

`--taa-current-filter`, `--taa-line-filter`, `--taa-thin-clip` and `--taa-adaptive-weight` are gone from the launcher
(refused by name) and the DLL, with the six programs `resolve_{filter,line,thin_filter,thin_line,age_filter,age_line}`
(HLSL, `_inc.h`, provenance JSON). Kept: plain, snapshot, thin (alpha history), age (the fixture's `SETA_EXIT` rows reach
it through `FrameInputs::thin_clip` / `adaptive_weight`), far, far_camera, the line masks and the box programs
(`docs/architecture/cleanup-inventory-2026-09-22.md`, item 6).

| Check | Result |
| --- | --- |
| Hash gate, `python3 verification/results/cleanup-batch6-hash-gate.py` | HASH_GATE PASS: `temporal-resolve{,-thin,-age,-far,-far-camera}-program.json` byte-identical to 112b6aa9, embedded words = `bytecode_sha256` |
| `run_temporal_pass.py`, bottle X3, main run | RESULT PASS 672 / 278, 488 samples, 28 `SETA_EXIT` rows: unchanged, report line for line the previous one |
| same, lattice run | 583 / 23 -> **500 / 12**: LATTICE_BASE 28 / 9 -> 10 / 0 (filtered run-139 rows and refusals; weight rows kept), flicker block 182 / 4 -> 180 / 4 (filtered bit-identity pair), line block 45 / 2 -> 0 / 0 (timing only), far block 127 / 2 -> 109 / 2 (line-filter far row, `FAR_STABILISER` 24 -> 20, line mask-failure check), thin-region block 201 / 6 unchanged; every kept row's values equal the previous report |


## 2026-09-23 motion history weight: a parallax-gated cap on the age programs' keep weight (fixture)

Implements `docs/architecture/taa-motion-history-weight.md` (Fable, scratch build `build-mw`, not a candidate; section 9
there is the as-built record). Option `--taa-motion-weight F[,V0,V1]` (`X3M_TAA_MOTION_WEIGHT`; requires `--taa`, F > 0
an age program; 0 off, else 0.5 <= F < 1 with 0 <= V0 < V1 <= 64 px/frame, default 2,8; **default off**, `0.8,2,8` the
flight candidate). The DLL logs it as `motion_weight=F,V0,V1` in `motion_output_mode`, refuses a malformed or out-of-range
value (`taa_motion_weight_setting invalid=1`), drops it per device without an age program
(`motion_output_taa_motion_weight ... unavailable=1 reason=no_age_program`) and hands it to the pass under camera policy 2
only. The pass uploads c25.yzw = A, B, F with c24 / c25.x as the existing two-register block (0, 1, 1 when off: the cap is
exactly 1). The age variants cap `keep` at `saturate(max(F, parallax2 * A + B))` before the alpha history, `parallax2` the
smaller of the squared translation parallax and the squared screen motion (review: a hull that moves with the camera has
screen motion 0 and would otherwise be capped by the turn); the age write is untouched. Rebased over cleanup batch 6 (the
age variants are age, far and far_camera) and the sky-history defaults.

**Reserve (before the term, the exit reset's method).** The term costs 4 slots on the far variants and 6 on the age one
(register reshuffling; 6 / 9 on the since-removed age_filter / age_line), so far_camera (510) would not fit; the note's
two candidates are not exact and save one each. Applied instead, on the age variants only: the centre texel fetched with
the dilation's eight neighbours (the skip test and branch gone; exact, one fetch more) and the alpha range test as
`min(blended - low, high - blended) >= 0` (exact). Reserve-only run (committed fixture and runner, HEAD shader plus the two
rewrites; `verification/results/motion-weight/compare_reserve_out.txt`): `passed: true`, `RESULT PASS numerical=672
state_restorations=278`, 488 samples; **0 stable lines removed / 0 added** on temporal-pass.txt (6886 lines both) and
temporal-lattice.txt (5242 both), all 488 + 375 baseline SAMPLE lines present, the 28 SETA_EXIT rows identical; slots age
495 -> 487, age_filter 507 -> 499, age_line 507 -> 499, far 498 -> 488, far_camera 510 -> 500.

**Fixture with the term** (`X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3
verification/probe/run_temporal_pass.py`, worktree `agent-a617e3d7c5fdbe2b3` after the rebase; the reports
`temporal-pass.txt` / `temporal-lattice.txt` and `temporal-pass-summary.json` under `verification/results/bottle-X3/` are
this run's; `verification/results/motion-weight/compare.py` and `compare_out.txt`, baseline main a651c219): `passed: true`,
`RESULT PASS numerical=712 state_restorations=278 generations=2`, **528** samples (672 / 488 before: case (m), ten rows on
two age programs and ten metrics per program and generation); lattice `RESULT PASS numerical=500 state_restorations=12`
(batch 6's count). Off path: temporal-lattice.txt 0 stable lines removed / 0 added (every FLICKER, FAR_STABILISER,
THIN_REGION, SENTINEL_STABILISER and SAMPLE row identical, 331 baseline SAMPLE lines present); temporal-pass.txt 0
removed / 40 added (the new metrics), all 488 baseline SAMPLE lines present, SETA_EXIT rows identical.

Case (m), `MOTION_WEIGHT` rows (a 512x16 routed hull at depth 0.9997 with a period-4 sinusoidal stripe point-sampled at the
jittered position, static 16 frames then moving 32, read at x >= 386 over the last 16 frames; E ratio = interior Laplacian
energy of the resolve over the current sample; sigma_fit from the stripe's amplitude, the x1.4 grid sigma in brackets;
ripple = rms of output[n](x) - output[n - k](x - k v), k the smallest integer shift (1, or 2 at the half-texel speeds);
far_camera = weight 0.9, thin region 0.97 under the camera gate, far 0.985, strict + band 3 + exit 0.25; age = thin clip
0.7, WMAX 0.9; option 0.8,2,8 against off; both programs give the same numbers unless noted):

| row | E ratio off -> on | sigma_fit off -> on (grid) | ripple rms off -> on | output / age diff vs off |
| --- | --- | --- | --- | --- |
| rest | 0.647 (age) / 0.622 (far_camera), unchanged | 0.43 / 0.45 (0.35 / 0.5) | 0.0142 / 0.0089 | **0 / 0** |
| 1, 5 px/frame (integer: the tap lands on the texel grid, nothing is resampled) | 0.649 unchanged | 0.42 (0.35) | 0.0141 | **0 / 0** |
| 12 px/frame (integer) | 0.647 -> **0.696** | 0.43 -> 0.39 | 0.0142 -> 0.0276 | 0.082 (age) / 0.087 (far_camera), age **0** |
| 1.5, 5.5 px/frame (half texel) | 0.197 unchanged | 0.81 (0.7) | 0.0094 | **0 / 0** |
| 6.5 px/frame (half texel; the ramp binds, cap 1.01333 - 0.0033333 x 42.25 = 0.8725) | 0.197 -> **0.255** | 0.81 -> 0.75 | 0.0095 -> 0.0122 | 0.033 (age) / 0.037 (far_camera), age **0**; **0.000000** against the run whose cap is the constant 0.8725 (F 0.8725, V0 0, V1 0.5) |
| **12.5 px/frame** (half texel) | 0.197 -> **0.383 (x1.95)** | 0.81 -> **0.62** (0.7 -> 0.7) | 0.0095 -> 0.0195 (**x2.06**) | 0.093 (age) / 0.104 (far_camera), age **0** |
| pan 12.5 px/frame (camera yaw, hull world-static) | 0.197 unchanged | 0.81 | 0.0095 | **0 / 0** |
| co-moving 12.5 px/frame (camera yaw, hull screen-static: relative 12.5, screen motion 0) | 0.647 (age) / 0.622 (far_camera) unchanged | 0.43 / 0.45 | 0.0142 / 0.0089 | **0 / 0** |

Asserted per program and generation: rest, 1 / 1.5, the pan and the co-moving rows bit-identical on both targets; the
age target identical on every row; 5 / 5.5 E ratio not below off's; 6.5 within 0.002 of the constant-cap run (a wrong A
or B fails; measured 0) and at least 0.01 from off; 12.5 E ratio at least 1.5x off's; 12.5 ripple at most 2.5x off's (the
first run measured 2.06x, above the note's 1.45x random-phase alias model: the bound was set from the measurement as
section 6 asked). The runner re-checks the bit-identity rows, the age identity and the 1.5x E ratio on
the parsed rows. The 5 px/frame rows are unchanged by construction (cap 0.93 above the 0.9 base; nothing in the strip is
fragmented, so the 0.97 thin-region ceiling is not exercised here: b = 0).

| check | result |
| --- | --- |
| `RESOLVE_BUDGET` (fixture, D3DXDisassembleShader) age / far / far_camera | 495 -> **494**, 498 -> **493**, 510 -> **505** of 512 (the co-moving gate's min is +1 each over the section-1 form's 493 / 492 / 504, measured before the rebase with age_filter 505 and age_line 508); plain 432, thin 465, snapshot 46, masks 368 / 344, boxes 51 / 35 / 94 unchanged (bytes unchanged: the non-age headers did not regenerate) |
| `LINE_TIMING_CAMERA` (1280x768, 6 rounds) no_region thin / camera, fragmented pan thin / camera | committed 1.3489 / 1.5017, 1.0570 / 1.4269 ms; reserve-only run 1.0768 / 1.2018, 0.9444 / 1.3342; with the term 1.4235 / 1.5995, 1.4083 / 2.0055; after the rebase 1.4038 / 1.5906, 1.3039 / 1.9146 (a CPU-wall row whose run-to-run spread exceeds the one-fetch and five-slot change: the GPU cost is unmeasured) |
| scratch DLL `build-mw` (`cmake/mingw-i686.cmake`, RelWithDebInfo), no warnings; `check_no_x87.py` | built; PASS, 638 reachable functions, 0 violations; sha256 `926c9d048251396de46885135f26e9f0cdb4e2d9501e5fa450e799c18f9fe6e2` (after the rebase and the review fixes; `ac22cfcf…` before) |
| `PYTHONPATH=verification/probe /usr/bin/python3 -m unittest verification.analysis.test_taa_image_defaults verification.analysis.test_taa_sky_history verification.analysis.test_taa_motion_weight verification.analysis.test_shader_compiler_provenance` | 33 tests, OK after the rebase (new module `test_taa_motion_weight.py`: forwarded as the triple with each age program, 0 the explicit off with `--taa` alone, omitted / inherited dropped, range and `--taa` / age-program refusals, DLL default 0; `test_taa_image_defaults`: absent unless given) |
| `tools/manage.py launch --bottle X3 --dry-run --motion-output --ownership --object-trace --object-lifetime --taa --taa-far-stabiliser 0.985 --taa-thin-region 0.97 --taa-motion-weight 0.8,2,8` | `X3M_TAA_MOTION_WEIGHT=0.8,2,8` beside `X3M_TAA_FAR_STABILISER=0.985,0,80,130,0.03,0.25`, `X3M_TAA_THIN_REGION=0.97,1`, gate camera, strict + exit 0.25 (the defaults; without `--motion-output` the launcher stops at `--taa requires --motion-output`, as before). The triple is forwarded with `%.9g` (0.9999999,2,7.9999999 round-trips); an env value of 32+ chars logs `taa_motion_weight_setting invalid=1 reason=too_long` |

## Run 262: motion weight 0.8,2,8 in flight, accepted (2026-09-23)

Run 70 A (`/tmp/x3-bottleX3-run262`, Run69 DLL `70abe438…`): the Run 68 A command plus
`--taa-motion-weight 0.8,2,8` (strict + exit 0.25 as defaults). Three bursts: SETA 5823,
SETA 12554, normal speed 7062. The user: the SETA blur is better and acceptable. Scripts and
outputs: `verification/results/run262-motion-weight/` (`run_all.sh`; `hull_ripple.py` new).
All measured unless marked.

- Option active: `motion_weight=0.800,2,8`, `camera_policy=2` on all bursts, no refusal.
- **Sharpness by hull motion bin against run254:** below 2 px/frame unchanged (σ 0.5 / 0.7 /
  1.0, E ratio within ±0.01); 8–16 px/frame E ratio 0.068 / 0.062 → 0.117 / 0.147 and σ 1.4 →
  0.7; ≥16 px/frame 0.020 / 0.028 → 0.076 / 0.113, σ 1.4 → 1.0 / 0.7.
- **The trade:** motion-compensated frame-to-frame ripple rms(taa)/rms(hdr) at 8–16 px/frame
  0.33 → 0.37 / 0.34 → 0.46, at ≥16 0.13 → 0.21 / 0.17 → 0.35 (1.1–2.0× against the model's
  1.45×); below 2 px/frame within ±0.03. Median age on moving hull unchanged (the cap changes
  the weight, not the count).
- No co-moving hull with large parallax occurred (parallax equalled screen motion; the logged
  0.28°/frame rotation is matrix precision noise, inferred), so the gate has no in-flight
  witness yet. The normal-speed burst is a different scene from run254's (median motion 4.9 vs
  0.4 px/frame). Far-sky share ≥13 px 11 → 23 % in one SETA burst: scene variance, unproven.
- 0 apply/restore failures, 96/96 frames per burst.

**Decision (2026-09-23):** `--taa-motion-weight 0.8,2,8` becomes the launcher and DLL default
under TAA with an age program and camera policy 2 (`0` opts out). 0.9 would be a no-op (the
base keep is 0.9 and the cap is a floor); 0.7 buys σ 0.80 → 0.73 for ~1.26× more ripple
(model); 0.85 is the knob if the ripple ever shows.

**Run 263 (0.7,2,8), 2026-09-23:** the same command with `--taa-motion-weight 0.7,2,8`, two SETA
bursts (6104 pure translation, 10979 at 0.23°/frame), no normal-speed burst. The user: blur
slightly better than 0.8, no shimmer noticed. Outputs `verification/results/run263-motion-weight/`
(`run_all.sh`). Measured against run262: E ratio at 8–16 px/frame 0.117 / 0.147 → 0.222 / 0.179,
at ≥16 0.076 / 0.113 → 0.139 / 0.165; σ at ≥16 1.0 / 0.7 → 0.7 / 0.7 (the 4–8 bin also fits 0.7,
small counts); ripple ratio at 8–16 ×1.66 / ×1.06, at ≥16 ×1.39 / ×0.89 (model ~1.26×, scene
variance in both directions); below 2 px/frame and the far sky within run262's spread. The
scenes differ, so every cross-run number mixes the weight with the scene; a replayed path at 0.8
and 0.7 would be the clean A/B. **Decision (user, 2026-09-23): the default is `0.7,2,8`.**

**Motion weight default merged (2026-09-23):** `--taa-motion-weight 0.7,2,8` is the launcher and DLL
default under `--taa` with an age program (`--taa-far-stabiliser` or `--taa-thin-region`) and
camera policy not forced to 1 (`--taa-sentinel 1`); `0` opts out and is forwarded as `0,2,8`;
invalid or oversized values stay off and log. Evidence:
`verification/results/motion-weight-default/dry_run_env_out.txt` (four cases); the motion-output
runner pins `X3M_TAA_MOTION_WEIGHT=0` (none of its cases runs an age program, so its committed
results cannot change); one review (Opus). The temporal fixture's current counts are 744 / 278 /
546 (after the dust-motes merge). Post-merge gate recorded in the commit.

## 2026-09-24 depth-copy fold (taa-high-resolution.md S1; fixture, not flown)

On a two- or four-channel current depth (the sun-shadow lane's RT2) with a far-program run, the mask chain's first draw
writes the next R32F depth history as COLOR1 (`line_mask_{,camera_}depth_ps.hlsl`), and the copy draw no longer runs.
Bottle X3, measured:

- `run_temporal_pass.py` PASS 744 / 278 / 546 with `temporal-pass.txt` byte-identical (sha256 `58d85cde…`). The
  default mode has no lane input.
- Lattice mode: RESULT PASS 508 / 89, from 500 / 12. The fold adds 8 numerical and 77 state checks. The cases are
  listed in taa-high-resolution.md, "S1 / S2 implemented":
  - fifteen `DEPTH_FOLD` rows, byte comparisons against the R32F twin or, with the lane term on, against the copy path;
  - the committed one-ulp negative control;
  - the refused RT1 bind and the failed fold draw;
  - a device Reset.
  Every run restores the hostile state.
- The other 4,144 lattice rows equal the committed report outside the timing and slot rows. That includes the seven
  lane-term flight rows, which are 4-decimal summaries, not bytes (`verification/results/taa-high-resolution/s1_identity.py`,
  `_out.txt`).


## 2026-09-24 5-tap bilinear history (taa-high-resolution.md S3; fixture, not flown)

The resolve programs reconstruct the history with Catmull-Rom through five hardware-bilinear fetches (the corner
blocks dropped, the five weights renormalised; derivation in `resolve.hlsl`); `--taa-history-taps 16`
(`X3M_TAA_HISTORY_TAPS`, default 5) keeps the 16-tap point programs, embedded byte-identical to the pre-S3 bytecode
(`resolve*_taps16.hlsl`, `X3M_HISTORY_TAPS16`). `TemporalPass` binds the history a second time at s11 (and the mask at
s12) with LINEAR filtering; without the FP16 / R32F filter caps every slot holds the 16-tap words. The 16-tap twins are
created on the device only while 16 is configured, so a default session holds the pre-S3 device references (the
motion-output runner pins 5; an eager twin failed it at 6). Both program sets are in the DLL: stripped d3d9.dll
+41,984 B (`.rdata` +37,888, `.text` +4,104). Bottle X3, measured:

- Programs (native `d3dx9_37`, `generate_rigid_motion_pixel.py`), words / slots 16-tap -> 5-tap: plain 1,681 / 432 ->
  1,661 / 425; thin 1,818 / 465 -> 1,814 / 469; age 1,947 / 494 -> 1,940 / 495; far 1,948 / 493 -> 1,931 / 493;
  far_camera 2,006 / 505 -> 1,987 / 504. Of that, 2 slots per program are the texel-centre bias (below). The 16-tap twins hash to the committed bytecode (`507d843e…`, `81e24fa9…`,
  `97d65cd8…`, `d88d9dc3…`, `0ef1f895…`). The stale records of removed variants (filter, line, age-line, age-filter,
  thin-line, thin-filter) have no source and were not regenerated.
- Semantic changes against the 16-tap form (stated in the derivation comment, `resolve.hlsl`):
  - HDR route (k > 0, the shipping exposure k): the colour is filtered first and weighed after; the 16-tap form weighed
    every tap before the sum. Identical at k = 0; at k > 0 a bright texel pulls the history harder (weigh() is concave
    in luma), bounded by the unchanged 3x3 clip. Weighing each of the five fetches instead (per block) was measured at
    +24 to +30 slots (plain 453, thin 491, age 517, far 518, far_camera 526), over 512 for three programs, so the new
    order stays. Measured on the HDR route: motion-output `seam-taa-hdr-on` exact fraction against its 8-bit twin
    0.9763 -> 0.9754 (below); bright emitters under motion are unmeasured.
  - The mask test sees the twelve texels of the five blocks: a reactive texel whose only contribution is a dropped
    corner block no longer rejects the history.
  - Out-of-range texels are no longer dropped one by one: a NaN / Inf texel of nonzero weight refuses the whole lookup
    (current only); a finite texel above `rejection.z` (65000) is averaged in, and the lookup is refused only when the
    filtered result exceeds it.
- Texel-centre bias (second review): the float32 UV of a texel centre errs by -1.2e-4 .. +2.4e-4 texel for W = 1280 ..
  5120 (float32 emulation, `taa-high-resolution/s3_centre_error.py` and `_out.txt`; negative at 3440), which a
  truncating 8-bit filter unit would turn into 1/256 of the neighbour at rest in the branchless programs. The edge
  taps (always on centres) take +1/1024 texel and the centre tap `max(w2 / w12, 1/1024)`, putting the error in
  [+8.5e-4, +1.3e-3]: +2 slots per program. A plain +1/1024 on every position (+1 slot) was measured first and moved
  the camera-tracking rows by up to 0.0155 px (narrow-yaw drift 0.0302 -> 0.0421 against a 0.05 tolerance), because it
  shifts every rounding threshold under motion; the centre-only form leaves `temporal-pass.txt` byte-identical to the
  unbiased 5-tap run (`09cd53be…`) and the lattice report identical outside the slot, timing and new rows.
- `run_temporal_pass.py` PASS 744 / 278 / 2 generations, 546 samples (unchanged counts; `report_sha256` `09cd53be…`,
  provenance only). Lattice mode RESULT PASS 528 / 89 (508 / 89 before): the S3 cases add 20 numerical checks.
- `FILTER_PROBE`: FP16 and R32F texel centres exact at 32, 1280 and 5120 texels (0 mismatched); sub-texel weights 8-bit
  (max error 0.00195 = 1/512, mean 1/1024).
- `HISTORY_TAPS`: rest, plain and far_camera, 32 frames: 0 differing against the 16-tap programs. Diagonal drift
  (0.30, 0.20) px/frame: 17,619 channel samples differ, max 0.0171, at most 0.0253 of `w (max - min)` of the current
  3x3, never above it. The 16-tap run equals the 16-tap words bound as the caller's resolve; `history_taps` names the
  program drawn. Fallback (GetDirect3D refused at initialize): `adapter_query`, every run draws 16 taps, byte-identical.
  Reset: a 5-tap pass resumes from an empty history byte-identical to a fresh pass. Hostile-state restoration of the
  s11 / s12 bindings: every base-mode case (Snapshot compares all 16 samplers) runs the 5-tap programs; without a mask
  policy s12 is set to null explicitly (normalize already clears it).
- Why the plain and far_camera drift rows agree to the digit (17,619 / 0.01708984 / 0.0253): `HISTORY_TAPS_FAR_IDLE`
  shows that under drift every channel of the far-camera mask is 0 (filter weight, farw, both thin-region strengths:
  depths 0.5 / 0.9 are below the far gate's 0.9995, and the lattice's 0.36 px/frame exceeds the 0.03 .. 0.25 px/frame
  speed gate, which closes the region) and the far_camera output equals the plain output bit for bit. So that row is not an
  independent check of the far program under motion; its rest row is (mask b = a = 1, output differs from plain, 5 = 16
  taps). The far programs' 5-tap arithmetic under motion is covered by the lattice rows with their 2-D oracle
  (THIN_REGION, SENTINEL_STABILISER, FLICKER, FAR_STABILISER).
- `verification/results/taa-high-resolution/s3_identity.py` (`_out.txt`), per row against the committed reports:
  - base mode: 11,786 rows each; 170 differ, none outside BLUR, SETA_SLOW, SETA_EXIT, MOTION_WEIGHT, CAMERA,
    CAMERA_FRAME and their SAMPLE rows (22 of 546). Largest moves: camera drift px +0.0039 (yaw single step
    0.0369 -> 0.0408, tolerance 0.05), scrolling-wave amplitude 0.9296 -> 0.9300, SETA trail_cast_max within ±0.0008,
    motion-weight e_ratio within ±0.0007 and output_diff +0.0007. Every hand-specified expected value holds; the
    STATIONARY rows and all reactive, route, HDR, sharpen, quad-twin and Reset rows are identical.
  - lattice: 150 rows differ (plus 4 timing rows): slot rows; FLICKER_DRIFT / NEAR_DEPTH / STEP1 / ALPHA metrics within
    ±0.08 codes; THIN_REGION shard rms within ±0.03 codes; SENTINEL flicker_ratio 0.4798 -> 0.4809; oracle errors up by
    0.00024-0.001 (the CPU oracle, `line_model`, uses exact weights against the filter's 8-bit ones; every tolerance
    holds). LATTICE, LATTICE_ORACLE, THIN_REGION_CAMERA_STATIC, THIN_REGION_PAN / CAMERA / CAMERA_FLIGHT and all DEPTH_FOLD
    rows are identical. Why: rows whose lookup fraction is 0 (static, whole-pixel pans such as SETA_PAN 3 px) or 1/2
    on the one moving axis (the 0.5 px THIN_REGION pans: w2 / (w1 + w2) is then exactly 1/2, which the filter's 8-bit
    weights represent) are unchanged. Other fractional 1-D motion moved by the deltas above (scrolling wave 0.25 px,
    SENTINEL_STABILISER pan_axis=y 0.30 px, camera yaw / pitch): there the corner weights are 0 in exact arithmetic
    too, and the difference is the filter's 8-bit weights against the point taps' float weights.
- `test_taa_history_taps.py` now also pins the `reason=too_long` log of an oversized `X3M_TAA_HISTORY_TAPS` (the DLL
  keeps 5, as `X3M_TAA_MOTION_WEIGHT` does).
- The far-stabiliser identity with the flown program now compares the 16-tap twin (`far_sequence(..., 16)`); the 2-D CPU
  oracle models the 5-tap form (`oracleHistoryTaps`, `temporal_line_inc.h`).
- `run_motion_output.py` PASS: 191 cases, 271,533 checks (190 / 271,369 committed; the new case `seam-taa-taps16`,
  `X3M_TAA_HISTORY_TAPS=16`, 164 checks, pins the pass at 6 device references: the 5 of the default plus the plain
  resolve's 16-tap twin; every other case pins 5, and each TAA attachment's `motion_output_taa_history_taps` line must
  name the requested count, `bilinear=1 reason=ok`; the fixture's reference resolve mirrors the setting). That case's
  colour hashes, checks and changed pixels are identical to the committed pre-S3 `seam-taa-on`: the 16-tap path is the
  earlier resolve in the route. `taa-high-resolution/s3_motion_compare.py` (`_out.txt`) against the committed summary,
  the new case aside: 44 cases differ, all TAA cases, only in resolve outputs: `color_hashes`,
  `taa_changed_pixels`, `taa_image/max_difference`, `present_readbacks/*code_error_vs_resolved`, `hdr_taa` image
  statistics, the `sharpen` images, `shifts/*/resolved` and `worst_resolved_residual_px` (the resolved-image shift
  estimates), `bodies/*/changed`, and the top-level HDR and sharpen twin statistics (e.g. `seam-taa-hdr-on` exact
  fraction 0.9763 -> 0.9754). The bench `hdr_frame_last/ev` rows move for the TAA-off benches as well (run to run, not
  S3). The first run failed on the runner's pinned 5 device references (an eager 16-tap twin made 6); the twins are
  lazy since.
- Scratch CMake build (MinGW i686, RelWithDebInfo) 0 warnings; `check_no_x87.py` PASS, 673 reachable functions; host
  suite 247 modules / 2,546 tests, 0 failing (new: `test_taa_history_taps.py`, 5 tests: launcher forwarding and refusal,
  the DLL's read, the twins' pre-S3 bytecode).
- Not verified: GPU time (no `--gpu-sync-timing` flight yet), the look in flight, native Windows filtering.

## ps_3_0 slot budget measured (2026-09-24)

The resolve programs are held under 512 slots (`RESOLVE_BUDGET`). This probe checks what that figure means on the X3
bottle. Fixture `verification/probe/shader_slot_budget_fixture.cpp`, run with
`X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_shader_slot_budget.py`.
Record `verification/results/bottle-X3/shader-slot-budget.json`, table `verification/results/shader-slot-budget/summary.md`,
host test `verification/analysis/test_shader_slot_budget.py`. The backend is the builtin d3d9 + wined3d, game device
shape, `d3dx9_37.dll` from the game directory. Each program is a dependent chain of N `mad`s. The chain value is read
back from an A32B32G32R32F target and counts the instructions the GPU executed.

| check | result |
| --- | --- |
| caps (measured) | `MaxPixelShader30InstructionSlots` 512, `MaxVertexShader30InstructionSlots` 512, `PS20Caps.NumInstructionSlots` 512, `MaxP/VShaderInstructionsExecuted` 65535 |
| enforcement (measured) | nothing refuses above 512. D3DX compile (OPTIMIZATION_LEVEL3) accepted every size up to the largest tried, 16,385 slots. D3DX assemble accepted up to 32,770. `CreatePixelShader` accepted up to 262,146 raw-token slots. vs_3_0 create accepted up to 65,539 |
| execution (measured) | read back as exact counts for every drawn chain up to 32,768 mads (ps) and 4,096 (vs); a [loop] of 132 x 500 executed 66,000, above the 65,535 cap |
| rolled loop (measured) | a [loop] of 16 x 10 mads costs 18 slots and executes 160; 64 x 500 costs 508 slots |
| cost (measured, 512x512) | 0.036 ms/draw at 513 slots -> 0.166 at 4,097: about 36 ns per slot per 262k px, so about 1 us per slot per frame at 5120x1440 (inferred) |
| first draw (measured) | draw + sync of the first draw, dominated by the backend shader compile. Cold (first time the backend compiles the program): 39 ms at 513 slots, 88 ms at 1,025, 219 ms at 2,049, 651 ms at 4,097, 2.2 s at 8,193, 8.6 s at 16,385, 46 s at 32,770 raw. Warm (the next run, identical bytecode): 10 / 21 / 55 / 141 / 475 / 1,784 / 7,122 ms. Both runs gave the same words, slots, HRESULTs and executed counts on all 28 shared cases. The cold figures come from the first run, stopped by hand before its 65k raw draw; that log is kept at `build/shader_slot_budget/shader-slot-budget.run1.stdout` and merged into the record with `--first-run-log`. The drop is attributed to the backend's shader cache (inferred). A raw 65,538-slot draw was still not done after 120 s |

The budget policy is in `docs/architecture/platform-portability.md` ("Shader slot budget").

## 2026-09-24 A' region hold, exact rest read, per-block weighing (taa-plan-lifted-slot-cap.md step 1; fixture, not flown)

Step 1 of the ratified plan, one re-baseline, revised after two reviews (Opus, Fable). Bottle X3, native `d3dx9_37`,
measured unless marked.

- **A' (`--taa-region-hold on|off`, `X3M_TAA_REGION_HOLD`, DLL default on).** With the thin region's camera gate the mask
  chain is its tests draw alone and the second mask target is released; `resolve_far_camera_hold.hlsl` (`X3M_REGION_HOLD`)
  reads the tests target at s8 and composes the region itself. L = `FrameInputs::thin_region_hold_frames`, the jitter
  period (`motion_output` passes its `jitter_samples_`, 2..64; c11.w):
  - region: in the region for L frames after the pixel was last flagged;
  - closure: a peak hold of the camera gate for L frames, of the smaller of the pixel's and its nearest-depth 3x3
    neighbour's camera openness (the texel whose correspondence the resolve's dilation follows);
  - the screen gate is not held: `openS = min(own screen, openC)`; its closure beyond the camera gate's is the camera's own
    motion, which must release the frame the camera stops;
  - `b = max(region * openC, class * S * openC)`, `a = region * openS`, far weights `g * c11.yz`.
  The box programs run through three twins (`thin_box{,_rows,_columns}_hold_ps.hlsl`, `X3M_REGION_HOLD_MASK`) gated on the
  tests texel inside the region: `a > r` and (this frame's flag, or the previous frame's region hold at the same texel from
  the age target at s7, unreprojected), or the sentinel class with the stabiliser. They mark computed texels in the box's
  alpha (1, else 0); the resolve uses the box only where marked and gives the added strength the 3x3 clip elsewhere.
- **Fallback, as an A/B option only.** `off`, `--taa-history-taps 16`, a device without FP16 / R32F filtering, or a refused
  hold program keep the dilation draws and the dilated `far_camera` program (`Diagnostics::region_hold`; the refusal logs
  `motion_output_taa_region_hold ... fallback=dilated`). The dilated program is the `--taa-region-hold off` A/B option, not a
  cap-fallback program set (AGENTS.md "Shader slot budget"): it goes away with the option once a flight accepts A', and a
  refused hold program then turns the thin region off. A run that turns the hold off restarts the history once.
- **Exact rest read** in thin / age / far / far_camera (S3 had it in plain only): rest takes one point fetch of s2. The
  +1/1024 centre bias stays for moving lookups on texel centres (one-axis motion), which the point read does not cover.
- **Per-block weighing** on the HDR route: each of the five bilinear fetches is weighed before the sum (S3 weighed after).
- **Slot cap log:** `motion_output_taa ... region_hold=%u ps30_slots=%u` once per pass creation
  (`TemporalPass::ps30_instruction_slots()`); 512 on this bottle.

As built, against the plan's section 3.1 (deviations, stated; the plan note points here):

1. The closure is a peak hold for one jitter cycle, not the linear 4-frame reopen `carried = max(open - 1/4, 0)` sketched
   there, which decays monotonically as written and, read as a linear reopen, failed the design's own motion-start bounds
   (trail +0.082 against 0.04, and 0.2075 against 2 codes 24 frames after the motion starts; a 4-frame peak hold +0.061 /
   0.156; an 8-frame one +0.061 / 0.00195: the background between 0.8-px struts moving 0.4 px/frame stays uncovered up to
   about 6.4 frames under the ±0.5 px jitter, inferred).
2. Each gate reads the smaller of the pixel's and its nearest-depth neighbour's openness (one more 4-byte fetch): with the
   8-frame hold alone the trail stayed at +0.061, the worst pixel (3, 5) on frame 73, a background pixel that was not
   itself covered and kept its rest-stabilised history (an unrouted pixel casts no camera vote of its own; the dilated
   gate closed it through the 17x17 minimum).
3. The screen gate is not held (second review, item 9): a held screen closure kept the whole region on the 3x3 clip for 8
   frames after every stop of a pan, where the dilated chain reopened at once. Content-motion closure is in the camera
   gate's hold already.
4. The hold length follows the jitter period (second review, item 7): with a fixed 8 and more than 8 phases a pixel
   flagged in one phase left the region for (samples - 8) frames per cycle. Encoding `(h + 128 (q (L + 1) + t)) / 65536`,
   16 fraction bits beside counts up to 64 (h <= 64 in 7 bits, the pair <= 5 L + 4 in 9), exact in FP32; the plan had 10.
5. The box twins and their region gate (first review, item 4; second review, item 8): the plan kept the box draws
   unchanged, but they gated on the composed mask the hold no longer draws, and `a > r` alone opens the whole frame under
   any camera motion above 0.03 px/frame.

Known limit (first review, item 3): the hold keeps one level per texel. A lower closure that arrives while a higher one is
held is released with the higher level, up to L - 1 frames early (7 at L = 8), only down to its own level (the pixel's
current openness still applies), and only where no content motion closes it again. Holding a second level needs 13 more
fraction bits (5 levels x (L + 1) timers per gate); the count leaves 17.

| program | slots S3 (ee3bbf88) | slots now | outside-loop instructions rest / moving, S3 -> now |
| --- | ---: | ---: | --- |
| plain | 425 | 455 | 230 / 274 -> 230 / 298 |
| thin | 469 | 517 | 316 / 316 -> 277 / 344 |
| age | 495 | 544 | 345 / 345 -> 306 / 374 |
| far | 493 | 545 | 347 / 347 -> 310 / 378 |
| far_camera | 504 | 555 | 356 / 356 -> 318 / 386 |
| far_camera_hold | - | 616 (629 before the review fixes) | - -> 378 / 446 |
| thin_box / rows / columns | 51 / 77 / 94 | same (bytecode unchanged) | |
| hold twins box / rows / columns | - | 70 / 155 / 121 | outside loops 31 / 48 / 63 (ps3 table) |

Slots: D3DX's count, the lattice report's `RESOLVE_BUDGET` rows, all within the 2,048 ceiling the runner gates
(`within_ceiling_2048`); `device_limit` 512 recorded. Instructions: `taa-high-resolution/aprime_slots.py` (and
`_out.txt`), ps3 table, straight-line instructions outside loops on the rest and moving paths of the history lookup (the
loops are unchanged code). Executed-instruction delta per pixel, inferred from those counts: far_camera -38 at rest, +30
moving; the hold program against S3 far_camera +22 at rest and +90 moving, plus one 4-byte fetch (the neighbour's tests
texel); about 0.02 / 0.10 ms at 5120x1440 at the plan's 1.07 us per executed instruction per frame, against the two
dilation draws' 1.33 ms (run290, measured). Review items 7 and 9 together: the hold program 13 instructions shorter on
both paths than with the screen hold (391 / 459); the hold length costs a reciprocal and two multiplies. The box twins add
one 4-byte age fetch per tested texel inside the region gate (the rows twin tests up to 7 texels per pixel).

Age lane (census): the fraction of the R32F count, no new lane. Readers of the age target: the resolve programs (the
hold program floors; the other age programs are never fed a held target: a hold-to-no-hold change restarts the history);
the box twins (the region hold, low 7 bits); the capture dump `taa_age` (`motion_output.cpp`), whose two host readers
(`taa_sentinel_pan_replay.py`, `taa_sentinel_pan_variants.py`) seeded counts with `abs()` and now take `floor(abs())`
(identical on integer dumps); the fixture's age oracles and exact age comparisons (`THIN_REGION*`, `SENTINEL_STABILISER`,
`FAR_STABILISER`, `DEPTH_FOLD`, `SETA_EXIT` negative marks, `MOTION_WEIGHT` age_diff) run hold-off, so they read whole
counts; the exit reset's sign survives the fraction. No exact-compare reader breaks; G32R32F not needed.

Evidence (`run_temporal_pass.py`, lattice mode):
- `REGION_HOLD_IDENTITY` (direct draws, 32x32 synthetic: sentinel 217, rest 264, moving 278, unrouted geometry 265
  pixels; k 0 / 0.5 x S 0 / 1): the hold program with whole counts in the previous age (holds reading 0) equals the
  camera program on the one-pixel composition of the same tests texels (openness the smaller of the pixel's and its
  nearest-depth neighbour's, the dilation replicated on the CPU), colour 0 of 1,024 pixels differing, age = count + the
  CPU-encoded holds on 1,024 of 1,024, four configurations (box marker 1 everywhere).
- `REGION_HOLD_STATE`: refusal without `configure_region_hold`, ignored without the camera gate, hostile state (c11
  included) restored with and without the separable box, failed rows and resolve draws publish nothing and recover,
  Reset, hold off restarts the history once, hold on keeps it, 16 taps keep the dilations. Mask targets
  (`line_mask_targets()`): 1 on a hold run, 2 after hold off, 1 on again, 2 with 16 taps, 1 with 5, 0 across Reset and 1
  after it, 2 when the box targets are refused on a hold run (the camera gate falls back to the screen gate), and the hold
  re-armed with 1 after the next Reset.
- Thin-region rows with the hold against the CPU oracle extended by the holds (`line_model` with `LineConfig::hold`, fed
  each frame's published tests target, the box twins' gate included; colour within the FP16 bound 0.02, age exact, tests
  target within 0.5 code):

  | row | oracle / age | result | dilated camera gate (same run) |
  | --- | --- | --- | --- |
  | shards at rest | 0.0083 / 0 | ripple 1.416 codes, 0.0866 of the plain resolve; square bit-identical | 1.416 (hold / dilated 1.0000) |
  | drift 0.12 px/frame (inside the gate) | 0.0054 / 0 | 16.238 codes | 14.756 (1.1004) |
  | drift 0.30 (past HI) | 0.0024 / 0 | 20.936 codes | 20.936 (1.0000) |
  | motion start, 0.4 px/frame after 64 static frames | - | 24 frames on: 0.00024 (bound 2 codes); trail 0.2483, +0.0295 over the plain 0.2188 (bound 0.04) | trail 0.2485 (+0.0298) |
  | pan 0.5 px/frame | 0.0093 / 0 | 1.416 codes, 0.0866 of the screen gate | 1.416 (1.0000) |
  | stop after a 0.5 px/frame pan (frame 64) | - | frame-to-frame step 1.983 codes over the 8 frames after the stop, 1.995 over the next 24 | 1.983 / 1.995 (plain 21.07 / 21.22) |
  | pan + bright mover + stale patch | 0.0088 / 0 | no ghost after the mover; patch +0.727 at frame 101 (screen gate 0.623) | +0.727 |
  | pan, k = 0.5, a non-finite tap | 0.0103 / 0 | | |
  | sentinel facets S = 0.7, rest / reversing 2 px | 0.0029 / 0 | flicker 0.392 / 0.384 of the S = 0 hold run | |

- Box-open fraction under a camera pan (`THIN_REGION_HOLD_BOX_OPEN`: the arm scene carried by a 0.5 px/frame pan, last of 24
  frames, share of the 32x32 frame where the box programs run): the dilated chain 0.7188, the tests gate alone (`a > r`,
  the first build) 1.0000, the region-gated twins 0.5459. The fixture's scene is small and lattice-heavy; the 5120x1440
  `taa_box` cost under a pan is the flight's number.
- Hold-off identity against ee3bbf88 (`taa-high-resolution/aprime_identity.py`, outputs `aprime_identity_pass_out.txt`
  and `aprime_identity_lattice_out.txt`; every existing row runs with the hold off): equal to ee3bbf88 at k = 0 within
  float32 last bits (largest move 2.2e-6 px, 0.001 codes), and different by design at k > 0 (per-fetch weighing; 8 of 118
  motion-output cases with colour hashes moved). `temporal-pass.txt` 11,782 of 11,784 lines identical; `temporal-lattice.txt` 4,848 of 4,872 identical (the rest: 18 budget-check labels, 512 -> the 2,048 ceiling; four `LINE_TIMING*` wall-clock rows; the `RESULT` line; one `FLICKER_DRIFT` row). The two
  moved k = 0 rows: the camera-yaw drift sample of both generations, 0.066310668 -> 0.066308508 px, and one `FLICKER_DRIFT`
  row (age program, v = 0.60, narrow gate: `pixel_p4_8` 4.600 -> 4.601, `block_p2_4` 1.333 -> 1.334, `block_p8_32` 1.743
  -> 1.744). Cause unidentified, last-bit: the accumulation chain of the listing is unchanged at k = 0, only the fetch
  issue order moved; not isolated by a build.
- `run_temporal_pass.py`: PASS, base 744 / 278 / 2 generations, 546 samples (counts unchanged), lattice 566 / 91 (528 / 89 + the 38 / 2 A' checks; `report_sha256` base `e2719f09…`, lattice `10759488…`); every
  `RESOLVE_BUDGET` row `within_ceiling_2048=1`. Generator: all 61 current records regenerated natively (one generator hash
  `52b8729a…`; the six sourceless records of removed variants untouched; the bloom tool's nine regenerated as well, since
  they pin this generator's hash); the 16-tap twins and the three hold-off box programs keep their bytecode.
- Commands (through `wine_lock.py`, `X3M_FIXTURE_BOTTLE=X3`): `run_temporal_pass.py` 144 s, full `run_motion_output.py` 518 s,
  lock wait 0 s each (`--timings-json`).
- `run_motion_output.py`, full suite before the review fixes: PASS, 197 of 197 cases (the four dither cases added since the
  committed summary, plus `seam-taa-region-hold-off` and `seam-taa-region-hold-too-long`: both byte-identical to
  `seam-taa-on`, colour hashes and the eight FP16 history files, device references unchanged; the too-long value logs one
  `taa_region_hold_setting invalid=1 reason=too_long length=41`, "off" none). Against the committed summary 110 of the 118
  shared cases with colour hashes are identical; the eight that moved are the HDR-route cases (`seam-taa-hdr-tonemap-*`,
  `seam-taa-hdr-sharpen-on`, the ownership and hook tonemap twins), with their fixture error unchanged (`max_code_error`
  0.49998 -> 0.49998, 0.49999 -> 0.50004 on `-auto`). After the fixes, selected cases (`motion-output-partial.json`): `seam-taa-on`, `seam-taa-region-hold-off`, `seam-taa-region-hold-too-long`, `seam-taa-thin-hold-on`, `seam-taa-thin-hold-off` PASS. The two thin cases run the thin region (0.97) with its camera gate and the sentinel stabiliser (0.7) through the DLL, every seam frame equal to the fixture's reference pass byte for byte (the reference mirrors the settings): device references 18 with the hold on (base 5 + far / camera programs 7 + separable box 2 + hold resolve and box twins 4) and 14 off, `region_hold=1` / `0` in `motion_output_taa` and `motion_output_taa_history_taps`, no `motion_output_taa_region_hold` refusal row, two extra capture readbacks per frame (the far programs' age and mask dumps). The full suite was not rerun after the fixes.
- Scratch DLL (MinGW i686, RelWithDebInfo), after the review fixes: 0 warnings; `check_no_x87.py` PASS, 673 reachable
  functions, 0 violations. Embedded program words +4,903 (19,612 B: the five resolve programs +949, the hold program 2,479
  and its three box twins 1,475; inferred DLL growth, not measured against a stripped baseline). Host suite: 250 modules,
  2,561 tests, 0 failing (`test_taa_region_hold.py`: launcher forwarding and refusals, the DLL parse, the records).

Look risks only a flight settles (plan section 3.4), the flight items: crawl at rest on hot pixels more than 3 px from any
class change (the fixture's shards match the dilated gate, 1.0000; the section-32 replay's run148 / 161 / 177 / 209 dumps
are no longer on disk, so it was not run); trails behind movers (peak hold one jitter cycle); **pan flicker at
silhouettes** (the closure arrives one frame late instead of 8 px early; no fixture row covers it); **popping shards** (the
8-px halo of the region goes; no fixture row covers it); `taa_box` under a pan (region-gated twins); slow drift inside the
gate (1.10 x the dilated gate's ripple: the carried closure is quantised to quarters, rounded to nearest, so a 0.41 closure
holds as 0.5).

## 2026-09-24 thin vote (B, opt-in; fixture, not flown)

Option B of [taa-thin-geometry-alternatives.md](../architecture/taa-thin-geometry-alternatives.md) section 3.2 as a vote
in the tests draw, `--taa-thin-vote on|off` (`X3M_TAA_THIN_VOTE`, DLL and launcher default off; on needs `--taa
--motion-output --ownership --sun-shadow-lane`; refused under `--vanilla`). Bottle X3, native `d3dx9_37`, measured unless
marked. The note's "Implemented" paragraph records the departures from its text (measurement seam, `c218` transport,
`.a` encoding, readable buffers, what the scale measures) and why. Revised after review (F1-F10), rebased onto `38d7d01f`,
revised again after the second review (tag on every `GetDesc` path, indexed invalidation with a volatility cap, a
successful Lock always unlocked, geometry refusals watched, the policy armed on the vote's own gate, ownership fixture
checks).

- **Census before the work** (`verification/results/thin-vote/census.py`, output beside it): 0x440 clones: the option is
  one configuration bit per session (`*(0x00606f34)+0x100 & 8` at 0x004bcbd0 selects 0x660 MANAGED, else 0x440
  DEFAULT); the caster counter's `shadow_replay_candidates` rows of 218 flown sessions classify the VB/IB pools of
  317,535,095 routed, z-writing, cascade-admitted draws (summed over frames; alpha-tested draws are not classified):
  managed 317,535,095, default_pool 0, dynamic 0, unknown 0. INDEX32: 265 of the 1,634 bodies of the merged-LOD census
  have more than 65,535 record-0 points (upper bound on bodies that can hold an INDEX32 subset; the builder compacts
  each group's vertices, 0x004bb5d0..0x004bb6a2), 11 of them single-group (lower bound). The public READONLY read covers
  INDEX32; DEFAULT-pool subsets stay unflagged.
- **Readable buffers** (review F1): the game's clones are MANAGED and WRITEONLY (`0x660`). With the option and every
  environment prerequisite of the vote (route, TAA, HDR, lane, `X3M_OWNERSHIP=1`: `thin_vote_route_gate()`, computed
  once in `initialize_log` and shared with `hook_device`'s enable) the loader arms
  `ownership::Options::readable_managed_buffers` at `wrap_factory` (`Direct3DCreate9`): eligible MANAGED creations
  asking for WRITEONLY are created without it (`portable_upload::plan_creation`), the requested Usage is kept in a
  private-data tag and returned by `GetDesc` on every path once any tag exists (also after a disarm or a retired finite
  owner), a failed tag or converted creation repeats the original creation. No sidecar, payload or
  Lock/Unlock work, so no 4,096-sidecar bound and no conflict with the locked-prefix scanner (the
  `prepare_readable_managed_uploads` path has both). The reader asks `ownership::get_buffer_readability` for the native
  descriptor and refuses without a Lock anything not MANAGED (`not_managed`) or still WRITEONLY (`not_readable`); an
  unobserved creation or a pending Lock is `not_quiet` (bookends not `known`).
  [platform-portability.md](../architecture/platform-portability.md) "TAA thin vote" records the policy and the gap.
- **Statistic** (`src/proxy/thin_vote_core.h`, `thin_vote::measure`, called from `MotionOutput::read_thin_votes`): per
  subset (VB and IB allocation ids, stream offset, stride, position element, draw range) the heights
  h = 2 area / longest edge in object units, 8 log2 bins anchored at the tallest (bin 7 = floor(log2 h_max), bin 0
  everything at or below 7 octaves down), kept as 9 cumulative fractions; above 16,384 triangles a fixed stride samples.
  Read once at the first scene end after the subset's first routed draw, at most 16 subsets per scene end; a read starts
  while fewer than 65,536 triangles were measured that scene end, so the bound is 81,919 sampled triangles (about 2.1 ms
  at the measured 25 ns per triangle, inferred) (review F6). A not-quiet or failed Lock retries at later scene ends (8
  attempts). Cache: 2,048 x 4-way, LRU by frame stamp, an entry used this frame is never evicted; one 32-bit tag per way
  (a set's four in 16 bytes) is compared before any key, hot fields first in a 64-byte-aligned entry.
- **Freshness by invalidation at the write** (review F2, revised twice): a measured entry keeps the VB and IB wrapper
  pointers it was read through (compared, never dereferenced) and the read marks both wrappers watched
  (`ownership::watch_buffer_writes`, one registry find; the watch is one-shot, the push clears it). Ownership pushes a
  watched wrapper's pointer into a fixed 1,024-entry queue (4 in the ownership fixture's build) at the Unlock that ends
  a writable Lock, at a ProcessVertices into it, at a trusted native-mutation notice and at its final release (bit 0
  set; O(1) under the registry mutex; unwatched buffers cost one flag test, no wrapper-layout read). The route drains
  the queue at `begin_frame`, before the scene-end reads and on a draw when `buffer_invalidations_pending()` says so:
  each queued wrapper's entries are dropped through an index from wrapper pointer to cache slots (4,096 open-addressed
  slots, up to 6 entries per wrapper; a wrapper with more, or a full index, falls back to one pass over the table), a
  queued read of one is skipped (`stale`), an overflowing queue clears the cache and the write counts (`overflows`).
  A write counts against its wrapper; after 4 counted writes the wrapper is volatile and its subsets are refused at
  the scene end without a Lock (`volatile_buffers`, `volatile_refused`; the refusal stays watched so its release
  forgets the count). A Reset keeps the cache, the index and the counts (`before_reset` only releases the queued reads:
  MANAGED buffers, their wrappers and allocation ids survive a Reset; a DEFAULT-pool buffer must be released before it,
  which drops its entries); a new device clears the cache at `attach`. A geometry refusal is watched as well, so a rewrite makes it readable
  again. A draw with nothing queued does one relaxed atomic load; a draw after a watched write takes the registry
  mutex once for the drain. A successful Lock is unlocked even when it returned no pointer. The drained ids live in a
  1,024-entry member array; one process-wide queue with one consumer (`src/ownership/README.md`).
- **Per draw** (`MotionOutput::thin_vote_alpha`, opaque routed rows with the lane active): the pending-invalidation test,
  one cache lookup, the scale log2(|row 0 xyz| W / 2 / row 3 .w) of the draw's
  own submitted rows, the cumulative read at the two window edges (0.5 and 3 px), `alpha = 1 - thin` when thin >= 0.5
  else 1. A miss queues the read (AddRef of the VB/IB wrappers until the scene end); alpha-tested, fade-arm and overlay
  rows and rows without the lane upload 1. The scale is the draw's
  object ORIGIN depth and the clip-x row alone (review F7): a subset near the camera on a large station whose origin
  lies far behind it reads too small a scale, so its near panels can vote; non-uniform scale is resolved along x only.
- **Transport:** one upload of 12 floats (`c216`-`c218`) instead of 8, `c216`/`c217` unchanged; the restore and the
  shadow of the application's reserved constants cover `c218` with the option; off, the 8-float array alone is built and
  uploaded (review F10). The transformer (process-wide switch set at device creation,
  `material_motion_configure_thin_vote`) gives every depth-writing pixel variant the thin depth fragment
  (`current_depth_thin_ps.hlsl`: `.a = c2.x` relocated to `c218`) and repacks the motion fragment's literals from three
  DEFs at `c218`-`c220` into two at `c219`/`c220` (value-exact operand rewrite, fails closed); `linear_material.cpp` takes
  the definition size from `material_motion_pixel_definition_words`. Motion-only and vertex variants unchanged. The two
  listings of our fragments, off and on, are kept (`motion_fragment_{off,thin}.txt`, the diff in
  `motion_fragment_diff.txt`; review F4): the same seven literal values (`-0.5, 1e20, 0, 1, 1e-6, 0.5, -1`) in two
  registers instead of three, every operand reading the same values, 28 -> 29 instructions, the one added instruction
  `mov oC2.w, c218.x` (the `.zw` write of w becomes `.z`).
- **Tests draw:** `line_mask_ps.hlsl` `X3M_THIN_VOTE` (twins `line_mask_depth_thin_ps.hlsl`,
  `line_mask_camera_depth_thin_ps.hlsl`, created by `TemporalPass::configure_thin_vote` only with the option): on a
  valid depth whose lane `.a` is in [0, 1) the flag is set and the 7-tap line search and the emissive vote are skipped;
  `FrameInputs::thin_vote` selects the twin of the fold program on an A32B32G32R32F depth with the thin region on.
  Otherwise the plain program is drawn and `thin_vote_absent` is logged once with the cause
  (`Diagnostics::thin_vote_reason`: `lane_off_r32f`, `two_channel_depth` for a G32R32F lane, `no_fold`,
  `thin_region_off`, `no_twin_program`; review F9).

RT2 `.a` contract: option off, `.a = w` from every routed depth writer (unchanged); option on, `1 - thin` on an opaque
routed row, `1` on every other routed row; the fill leaves `-1`.

Evidence:

- **Motion-output fixture, thin cases** (`motion_output_thin_vote_inc.h`, mode `thinvote`, 128 x 128, ownership wrapper,
  TAA, identity FP16 scene, lane, thin region 0.97 with its camera gate and A'; every buffer MANAGED | WRITEONLY as the
  game's; six struts 1.4 px wide at the far scale, 5.6 px at the near one, in front of a panel at 0.25 vs 0.26 device
  depth, so the 7-tap line search never flags them): `seam-thin-vote-far-on` 54 checks: struts' RT2 `.a` 0 on frames
  1-5 (1 on frame 0, before the first read), panel 1, fill -1, `.b` = w = 2; `c218` = (0, 0, 0, 0) for the strut draw,
  `c216`/`c217` as without the option; tests-target b = 254 at every strut pixel and 0 at every panel pixel more than
  4 px from the fill in all five captured frames; `thin_vote_frame`: reads 2, measured 2, 14 triangles, every refusal
  counter 0, one voted draw per frame. `seam-thin-vote-far-off` 47 checks: `.a` = w = 2, strut b 0, no thin_vote line.
  `seam-thin-vote-near-on` 54 checks: `.a` 1, strut b 0, voted 0. Twin: RT1 and RT2 `.r`/`.g`/`.b` of frames 1-5
  byte-identical between far-on and far-off; `.a` differs at 22,099 pixel-frames.
- **`seam-thin-vote-hostile`** (review F3; 93 checks): U, MANAGED WRITEONLY created while the policy is disarmed
  (`x3m_thin_vote_fixture_readable_policy`), refused without a Lock (`not_readable` 1), never votes; D, a DEFAULT-pool
  copy (`not_managed` 1, and 1 more for its replacement after the Reset); R, the panel drawn with NumVertices past the
  buffer (`range` 1); T1 with the application's READONLY Lock held across frame 1's scene end (`not_quiet` 1, `retries`
  1), read at frame 2's and voting from frame 3 (`.a` 0, b 254); T2 released by the application right after its draw
  with its read queued (read and released at the scene end); T3 released on a frame without a scene end, then a Reset
  (dropped unread); T1 rewritten in place 0.2 units wide at frame 4 (the application's writable Lock/Unlock): its entry is
  dropped at the write, it stops voting at once and is read again at frame 5's scene end; V, an off-screen pair
  rewritten before its draw at frames 1-4, measured four times (each write drops the previous read's entry), volatile
  after the fourth write and refused without a Lock at frame 5's scene end. Final counters (final full run 12:16-12:24,
  identical in the 12:08 thin-case rerun): reads 9, measured 9 (P, S, T1 twice, T2, V four times), unreadable 5, retries 1,
  not_managed 2, not_readable 1, range 1, not_quiet 1, stale 0, geometry 0, 42 triangles, invalidated 7 (T1's VB write,
  V's four writes, T2's VB and IB final releases), dropped entries 6, overflows 0, volatile_buffers 1, volatile_refused
  1; after the Reset S votes at once from the kept cache (frame 5: known 2, voted 1); the fixture's teardown reaches
  zero references on every object. The three plain cases: invalidated 0, volatile 0
  ([frame_rows_out.txt](../../verification/results/thin-vote/frame_rows_out.txt), `frame_rows.py` beside it).
- **Motion-output fixture, full suite** (`run_motion_output.py` through `wine_lock.py` on the final sources after the
  second review, 12:16:04-12:24:46, lock wait 0 s; the summary re-pinned from this run): PASS, 205 cases + 26 bench
  (199 committed + the 2 bolt-shape cases from main + 4 thin cases). Against the committed summary every one of its 199
  cases is present and every recorded leaf is identical except wall-clock ones (48 leaves such as `bounds_bench_ns`,
  `costs_us_per_frame`; 113 cases identical including those; 0 non-clock leaves differ)
  ([compare_motion_out.txt](../../verification/results/thin-vote/compare_motion_out.txt), script beside it). The
  existing cases draw the lane-off R32F RT2, which has no `.a`; the lane's option-off `.a` = w is the far-off case above.
- **Temporal pass** (`run_temporal_pass.py` on the final sources, 12:25:03-12:27:27; the option never reaches
  TemporalPass there): PASS 744 / 278, report identical to the committed one (`e2719f09…`); lattice PASS 566 / 91, 5,076
  lines, 4 differ from the committed report, all wall-clock rows (`LINE_TIMING`, `_CAMERA`, `_CAMERA_LANE`, `_SENTINEL`);
  every `RESOLVE_BUDGET` row unchanged
  ([compare_temporal_out.txt](../../verification/results/thin-vote/compare_temporal_out.txt),
  [lattice_diff_out.txt](../../verification/results/thin-vote/lattice_diff_out.txt), scripts beside them).
- **Ownership** (the readable policy and the invalidation queue are new ownership code): `run_ownership.py` PASS
  12:10:35-12:10:52, baseline 370, wrapped 661 checks (563 before: `thin_vote_case` adds 43 per device iteration:
  `GetDesc` of a converted VB and IB returns the requested WRITEONLY while the native Usage has none, a READONLY Lock
  reads the written bytes, the tag-failure and converted-creation-failure fallbacks keep WRITEONLY storage, disarming
  leaves new creations unconverted while a converted buffer keeps its Usage, the queue's writable-Unlock / native-notice
  / index-buffer / final-release (bit 0) pushes, READONLY and unwatched writes silent, the one-shot watch, overflow of
  the fixture's 4-slot queue; `thin_process_vertices_case` adds 12: ProcessVertices on a software-VP device pushes a
  watched destination, not an unwatched one), `verify_ownership.py` PASS; `run_managed_upload_contract.py` PASS
  12:10:22-12:10:29, 461 checks
  ([managed-upload-contract.txt](../../verification/results/bottle-X3/managed-upload-contract.txt), summary beside it).
- **Slots** (`run_thin_vote_probe.py`, `D3DXDisassembleShader`, [probe.json](../../verification/results/thin-vote/probe.json)):
  tests draw `line_mask_depth` 420 -> twin 429, `line_mask_camera_depth` 407 -> twin 415; current-depth fragment 3 -> 4;
  the reviewed pair's pixel variant (ps `8759c7838bbc86c2`, original 49) 77 -> 78. The plain programs keep their bytecode
  (all generator records and the bloom tool's nine regenerated for the new generator hash; no `_inc.h` byte moved).
- **Cost** (same probe, i686 production flags, under FEX; [probe.json](../../verification/results/thin-vote/probe.json)):
  `measure` on a 10,000-triangle indexed grid 247.8 us warm median with FLOAT16_4 / stride 24 (24.8 ns per triangle; cold
  first call 385.7 us), 165.7 us with FLOAT3 / stride 40. Per draw, after the invalidation change: the probe's core (key,
  lookup over 448 cached subsets, scale, window, alpha) 8.45 ns warm in a loop; one lookup timed alone between two QPC
  reads (64 ns per QPC call, 10 MHz counter) 75.9 ns warm and 92.0 ns with 2 MiB of other memory streamed before it, so
  a cold table costs about 16 ns more. Drain (probe `DRAIN`, a cache of 6,697 entries read through 2,048 VB and 512 IB
  wrappers): 68 ns per invalidated wrapper through the index (its entries dropped and unindexed), against 0.98 us for
  one warm pass over the table (the former per-drain work, before its binary searches). In the DLL (`thin_vote_frame
  draw_us`, the fixture's 2 routed opaque draws per frame, telemetry draw metrics; the final full run): 0.7-1.3 us
  per frame in frames 1-5 of far-on, 0.5-1.2 us in near-on, 0.25-0.65 us per draw with the two QPC reads, against
  1.9-2.8 us per frame (about 1 us per draw) with the per-draw revision views; frame 0 126-131 us (every subset missed
  and queued, the path's first execution; inferred: translation of first-run code); hostile frame 1 345 us, frames 2-5
  3.2-6.9 us for 7-8 draws with drains. The remaining 0.2-0.85 us per draw above the probe's bracketed 90 ns is not the table (inferred: code the
  translator runs cold between two draws a whole frame apart, the timing calls themselves; a game frame's 448 draws keep
  that path hot, the fixture's two do not). Not established against the 100 ns target in the game: needs `draw_us` from a
  flight. The READONLY Locks' in-game cost is unmeasured (fixture, final full run: `lock_us` 105.6-107.6 us for the
  plain cases' 2 reads, 441.7 us for the hostile script's 9 reads, cumulative, first Locks included).
- Scratch DLL (MinGW i686, RelWithDebInfo): 0 warnings; `check_no_x87.py` PASS, 683 reachable functions (673 before
  the vote), 0 violations. Host suite: 253 modules, 2,613 tests, 0 failing (`test_taa_thin_vote.py`, 14 tests: the
  invalidation index against a brute-force count over 100,000 random stores and invalidations, the per-wrapper overflow
  scan, the volatility count, and: the histogram, the
  bin shift, the [0.5, 3] px window at 0.3 / 0.6 / 1.5 / 2.9 / 6 px, the rows' scale, the vote threshold, FLOAT16_4 /
  INDEX32 / index bias / refusals, the cache; the launcher option; the source contract, the readable policy and the
  plain programs' bytecode). Four existing host checks adapted: the HDR-case count of `test_motion_output_runner.py`
  (79 -> 83 after main's two), a `read_thin_votes` stub in `motion_hdr_scene_fixture.cpp`, the option flag in the
  wrap-state seam's mirror class, the new option in `media_startup_loader_fixture.cpp`'s mirror.

Not flown. What a flight has to settle (Run 80 A/B, `--taa-thin-vote on` against `off` at the same spot): whether the
routed subsets get histograms (`thin_vote_frame`: measured against `not_readable`, `not_managed`, `range`, `not_quiet`,
`invalidated`, `overflows` during loading), the readable policy's effect on loading and frame time (every MANAGED WRITEONLY buffer created
without WRITEONLY), the scene-end read cost on a sector entry (`lock_us`, `measure_us`), the per-draw cost (`draw_us`
with `--telemetry` draw metrics), how many opaque routed draws vote at the lattice stand and on hulls (a hull group
whose triangles are mostly 0.5-3 px at distance votes as a whole; near panels of a large station can vote through the
origin-depth scale), crawl on a station arm seen against its own hull (B's intended gain), ghosting on voted panels
under a pan, and the tests draw's cost with the twin (`taa_mask` with `--gpu-sync-timing`).

**Telemetry split (2026-09-24, after run306).** `draw_us` was 0 in run306 because it is timed with the per-draw
stamps, which need `X3M_TELEMETRY_DRAW=1` (run306 logged `telemetry_start ... per_draw=0`); it was never a unit or
reset bug. `draw_us` keeps that meaning (sum over the frame's opaque lookups, draw metrics only); the row adds
`draw_timing=` (1: `draw_us` measured, 0: not measured, so 0 is no longer ambiguous) and, whenever telemetry is on, one
timed lookup per frame without any per-draw QPC: `sample_at=` (the opaque draw number, rotating over the previous
frame's opaque count), `sampled=`, `sample_us=` and `stamp_us=` (an empty QPC pair just before it, the clock's own
cost, which under Wine is of the same order as a lookup). `sample_us * opaque` estimates the frame's lookup time
(`thin_vote_summary.py` prints it). `missed` (unchanged: opaque draws with no usable histogram) now reads `missed =
queued + deferred_cap + already_queued`: `deferred_cap` is the existing `dropped` under its explicit name (a new
subset past `reads_per_frame`, re-queued by the next frame's draw), `already_queued` a further draw of a subset whose
read is queued this frame. A subset whose reads failed `read_attempts` times becomes Unreadable at its last retry
(`Cache::retry`) and counts under `unreadable`, not `missed`. run306 from its old rows
(`verification/results/run306-run80a-thin-vote/thin_vote_missed_split.py`, measured): 1,446 missed on 95 frames =
538 queued + 656 cap-deferred (45 %) + 252 `already_queued` (the remainder, by the identity). The cap dominates, on 19 frames that queued the full 16 (a burst from
frame 6202, the unlogged reload: 191, 151, 113, 97 missed); nothing was lost for another reason. Fixture: the thin
vote cases now check the identity and `draw_timing=1` on every row and a sampled lookup; `run_motion_output.py` on the
five thin-vote cases, bottle X3, exit 0 (far-on 56, far-off 47, near-on 56, hostile 95, far-on-owner 56 checks; partial
run, no twin comparison). Last rows: `draw_us` 2.0-4.0 over 2-7 opaque draws, `sample_us` 0.6-1.6, `stamp_us` 0.1-0.2
(fixture wall clock, not game cost). The sample path with draw metrics off is not fixture-exercised (the runner pins
`X3M_TELEMETRY_DRAW=1`); a flight with `--telemetry` and no draw metrics shows it. The fixture exercises only the
`queued` part of the split: `deferred_cap` and `already_queued` are 0 on all 24 vote-on rows. With
`frame_log_interval` > 1 the logged `sample_at` is biased (it is always draw 1 when the opaque count divides the
interval, since only frames with `frame % interval == 0` are logged); flights log every frame (interval 1).

## 2026-09-24 fade owner (`--fade-rt2-owner`, opt-in; fixture, not flown)

[fade-rt2-ownership.md](../architecture/fade-rt2-ownership.md) as ratified; its "Implemented" paragraph records the
departures (three-lane `c218` encoding, original-shading-only widening, the lane's invalid-share twin).
`--fade-rt2-owner on|off` (`X3M_FADE_RT2_OWNER`, DLL and launcher default off; requires `--taa`, on also
`--motion-output --hdr`; refused under `--vanilla`). Bottle X3, native `d3dx9_37`, measured unless marked.

- **Contract.** A fade-arm row (not the overlay arm) sets `COLORWRITEENABLE2` 15 instead of 0 in both binding modes
  (lazy: no write and no undo when the application holds 15, by inspection) and uploads `c218.y = 1`, `c218.z = 0`; its
  depth fragment (`current_depth_owner_ps.hlsl`, used instead of the plain and thin fragments while the option is on)
  writes `.a = max(w * c218.z + c218.x, c218.y) = 1`, so the engine's SRCALPHA/INVSRCALPHA blend stores the fragment.
  Every other routed row uploads `c218.y = 0` and `c218.z = 1` (B off: `.a = w`, the plain value) or `0` (B on:
  `.a = c218.x = 1 - thin`, B's value). RT2 `.a` / `.b` per row class (`.a` and `.b` exist on the lane RT2 only):

  | Row | B off, owner off | B off, owner on | B on, owner off | B on, owner on |
  | --- | --- | --- | --- | --- |
  | opaque routed | `.a = w`, `.b = w` | same | `.a = 1 - thin` (1 = no vote), `.b = w` | same |
  | alpha-tested routed | `.a = w`, `.b = w` | same | `.a = 1`, `.b = w` | same |
  | fade arm (>= 500, held >= 400) | masked | `.a = 1`, `.b = w`, `.r = z/w`, `.g = -1` | masked | same as B off |
  | overlay arm (no registers row) | masked | masked | masked | masked |
  | not routed (fill) | `.r = -1` | same | same | same |

  On the four-channel lane an owner binds the invalid-share twin (`.g = -1`: not a receiver); a row without one
  (material, XT) stays masked (`fade_owner_masked`). The tests draw is unchanged: B votes on `validDepth && 0 <= a < 1`,
  an owner's `a` is exactly 1. ZWRITEENABLE is never written. Identity: `fade_route::arm_pair(registers row,
  distance_fade_rows pair, owner && original shading)`, the register table 7 -> 9 rows (`494fe349b8bc12ec`,
  `53a0a641107ed76c`, c39 / c41 / b0); `distance_fade_rows` (the bracket) unchanged. `fade_route_frame` gains
  `fade_evicted` (entries a full 64-key hysteresis table displaced this frame), `fade_owner`, `fade_owner_masked`.
- **Host** (`test_fade_region.py` 16 -> 29 tests): the 9-row table, `arm_pair`'s truth table, the identity against the
  real `distance_fade_rows` and the reviewed table (the seven pairs fade either way; the four run214 pairs
  `494fe349/fffdabd9`, `53a0a641/8759c783`, `4944d81d/ca6bfa4a`, `53a0a641/63f96eba` only with the owner, sampler mask 0;
  glass `c30104cb/a66fb198` and damage `37c34a74/5f82ecac` never), the eviction counter (0 until the 65th key, then one
  per displaced key), the launcher option, the source contract. Adapted: `test_taa_thin_vote.py` (the upload lines),
  `test_motion_output_runner.py` (case lists, HDR count 83 -> 93, the frame line's new fields), the wrap-state seam's
  mirror class. Full suite (`run_host_suite.py`): 253 modules, 2,637 tests, 0 failing. Generator and bloom provenance
  records regenerated for the generator's new digest (no `_inc.h` byte moved).
- **Motion-output fixture** (`run_motion_output.py`, full run on the final sources; the summary re-pinned from it): PASS,
  220 cases + 26 bench (15 new). Against the pinned summary (4de081ae) every one of its 205 cases is present and 0 non-clock
  leaves differ (53 clock leaf names; 116 cases identical including clocks)
  ([compare_motion_out.txt](../../verification/results/fade-rt2-owner/compare_motion_out.txt), script beside it). New
  cases, `faderoute` with the option on (`-owner`; lane = the four-channel RT2, on the original-shading scripts) and
  `hull` off:

  | Case | Routed / held frames | RT2 owner px (kept) | z/w error | Resolved residual px |
  | --- | --- | --- | --- | --- |
  | routed-owner, routed-perdraw-owner | 12 / 0 | 4,704 (0) | 1.2e-8 | 0.069 |
  | sentinel-owner | 12 / 0 | 4,704 (0) | 4.8e-8 | 0.069 |
  | hover-owner (thin region 0.97, captures 1-8) | 8 / 5 | 3,136 (1,568) | 1.2e-8 | 0.053 |
  | original-owner, behind-owner (lane) | 8 / 5 | 3,136 (1,568) | 1.2e-8, `.b` error 0 | 0.054 |
  | overlay-owner (lane): fade_routed 2, overlay_routed 0 | 12 / 0 | 4,704 (0) | 1.2e-8 | 0.071 |
  | foreign-owner (lane; g_AlphaValue .390625): `unmatched=fade_threshold` | 0 / 0 | 0 (4,704) | - | 0.103 |
  | hull-owner (lane; `494fe349/fffdabd9`, own nodes, fraction 1000) | 12 / 0 | 4,704 (0) | 1.2e-8 | 0.067 |
  | hull (option off): `unmatched=overlay_node`, overlay_refused 2 | 0 / 0 | - | - | - |

  Every routed quad's interior holds z/w 0.3 (lane: `.g = -1`, `.b = w` exactly, `.a = 1`), a refused quad leaves RT2
  unchanged, no draw changes RT2 outside its own raster, and over the fill the upper half keeps `.r = -1` (the lane fill
  stores `.g = 0`). History resets at the class changes (age dumps, thin region 0.97, captures 1-8; each owner case against
  an option-off twin; "fresh" = history count 1 on every quad pixel, i.e. current-only; the runner pins the fresh frames
  per case, `FADE_ROUTE_AGE_FRESH`, and the twin difference, `FADE_ROUTE_AGE_OWNER_RESETS`):

  | Case (schedule: routed 0-2, refused 3-5, routed 6-7, refused 8; cut 7) | Fresh frames | Twin (option off) | Owner adds |
  | --- | --- | --- | --- |
  | hover-owner / hover-age (over A, linear materials, bracket M on refused frames) | 3, 4, 5, 6, 7, 8 | 3, 4, 5, 6, 7, 8 (counts identical) | none |
  | original-owner-age / original-age (over the fill, lane, original shading, A scissored) | 3, 6, 7, 8 | 6, 7 | 3, 8 |

  Why: in hover the refused frames are current-only through the bracket's M, frame 6 (routed again after a refusal)
  through the rows' one-frame history (unmatched: RT1 alpha 3 over A's rows), frame 7 is the camera cut; none of it is
  the owner's. Over the fill the owner adds exactly its falling edges, frames 3 and 8 (owner -> sentinel: a history
  depth in front of the far plane is rejected once, design section 4 (b)). Frame 6, the rising edge (sentinel -> owner),
  is current-only with the option off too: the rows' one-frame history, not the depth; section 4 (a)'s "keeps history"
  cannot occur while re-routing after a refusal is unmatched (in the game as in the fixture). On the 449 frames
  (2, 4, 5, 7) no quad pixel's region hold rises in any of the four (the hold reaches 8 on 2 quad pixels on owner frames
  1, 2 and 6). The option-off twin over the fill also shows what the owner removes: its routed-but-masked quads follow
  the raw jitter (resolved / raw shift up to 1.11 on matched frames), the owner's stay put (residual 0.054 px).
  Lane under linear materials (`routed-owner-lane`): the material rows have no invalid-share twin, the owner is
  withdrawn, `fade_owner_masked = 2` on every routed frame and RT2 is unchanged under the quads. Thin vote with the
  owner (`seam-thin-vote-far-on-owner`): RT1 and all four RT2 lanes byte-identical to `seam-thin-vote-far-on` on every
  captured frame (the owner fragment reproduces B's `.a`). Fixture fix inside the new cases only: the option-off
  `sentinel` / `original` / `behind` scripts set the scissor rectangle but never enable the test (frame_begin's scene
  states turn it off), so their quads sit over A, not the fill; the owner cases, `hull` and the age twin
  (`X3M_FIXTURE_FADE_SCISSOR=1`) enable it, and over the real
  fill a refused blended quad is the sentinel class (stabiliser pinned off by the runner), reported in `shifts`, not
  asserted. The reference resolve reads the lane RT2's `.r` (a MOREDATA retry; R32F cases unchanged). `hull` feeds the
  station VS a four-lane TEXCOORD0 (its light map's UV in `.zw`) and the ramp on the light-map stage of Q.
- **Temporal pass** (`run_temporal_pass.py`): PASS, report identical to the committed one (`e2719f09...`); lattice PASS
  571 / 91 (566 + 5), differing from the committed report only in timing rows, the RESULT line and the new row's lines;
  the "routed sentinel (glass)" row unchanged
  ([lattice_diff_out.txt](../../verification/results/fade-rt2-owner/lattice_diff_out.txt),
  [compare_temporal_out.txt](../../verification/results/fade-rt2-owner/compare_temporal_out.txt)). New row
  `THIN_REGION_HOLD_FADE_OWNER` (sentinel scene at rest, S 0.7, hold; a far routed square, depth 0.999, a masked fade row
  until frame 32 and an owner from it; against always-masked and always-owner runs): oracle error 0.005859 (bound 0.02),
  age error 0, tests-target error 0, frames before the switch bit-identical to the always-masked run, 52 region openings
  over the square, at most 1 per pixel, 0 frames whose holds differ from the always-owner run, which holds the same 52
  pixels at its last frame (inferred: the square's corners, which a diagonal 7-tap line flags in steady state like any
  routed geometry against the sentinel).
- **Distance fade.** Live script (`run_linear_distance_fade_live.py` on the full run's seam DLL and fixture): PASS,
  `fade_routed = 0` asserted per frame; against the same script on main's seam DLL and fixture (built from
  `git archive 4de081ae` in a scratch directory): 0 non-clock leaves differ
  ([compare_live_out.txt](../../verification/results/fade-rt2-owner/compare_live_out.txt), script beside it; the
  committed result dates from 2026-09-15). Detached `run_linear_distance_fade.py`: its report equals the same fixture
  built from main's sources except `completed_ms`; both fail the same check (`actual native/E alpha identity`), and
  `build_linear_distance_fade.sh` no longer links on main (it lacks `linear_emission.cpp`): pre-existing, open.
- **Slots** (`run_fade_owner_probe.py`, `D3DXDisassembleShader`, [probe.json](../../verification/results/fade-rt2-owner/probe.json),
  our fragments' lines in [owner_fragment.txt](../../verification/results/fade-rt2-owner/owner_fragment.txt)): current-depth
  fragment 3 (plain), 4 (thin), 5 (owner: rcp, mul, mad, max, mov); pixel variants off / thin / owner / owner + thin:
  `8759c7838bbc86c2` 77 / 78 / 79 / 79, `517540ae6d5e5410` 66 / 67 / 68 / 68, `fffdabd910793aba` 128 / 129 / 130 / 130,
  `63f96eba9eea7880` 82 / 83 / 84 / 84. The DEFs and the motion fragment are the thin variant's line for line. With the
  option on every depth-writing variant carries the 5-instruction fragment (opaque rows too: +2 ALU per routed pixel,
  cost inferred). Resolve and tests draw unchanged (RESOLVE_BUDGET rows identical).
- **Cost** ([route_cost_out.txt](../../verification/results/fade-rt2-owner/route_cost_out.txt), `route_cost.py` beside it;
  the full run's frame lines, frames 1-11, CPU QPC under FEX, telemetry draw metrics, 64 x 64): route_draw_us per routed
  draw, off -> owner: routed 13.89 -> 13.65, perdraw 16.54 -> 16.28, sentinel 14.23 -> 14.25, hover 17.09 -> 16.63,
  original 14.55 -> 15.62, behind 14.98 -> 15.36, overlay (overlay arm -> fade arm) 13.91 -> 14.44: within about 1 us
  either way, no systematic cost on already-routed fade draws. Newly admitted hull draws: `hull` off routes A alone
  (14.78 us per frame, gate_us 21.81 with the two overlay refusals), `hull-owner` all three (42.46 us, 14.15 per draw,
  gate_us 17.95): about 13.8 us of route work per newly routed draw in this fixture (the design's 7.49 us is
  route-per-draw-cost.md's production figure). `lazy_mask_writes` counts only application masks other than 15, so the
  lazy saving is not visible in the counters (by inspection).
- Scratch DLL (MinGW i686, RelWithDebInfo, the final full run's clean build `798610e4...`; the hash embeds the tree state): 0 warnings; `check_no_x87.py`
  PASS, 683 reachable functions, 0 violations.

Not done: the prepass parity check (`production-zonly` / `seam-zonly`) has no RT2 interior-hole count; its material
draw is not routed (no TAA, not the fade-band state), so the count needs a new TAA + HDR + camera zonly-owner script.
The two pinned zonly cases are unchanged (the full run).

Run 80 items (fade owner; section 5 of the design note): fly the run214 stand (two distant stations, slow vertical pan
3-9 px/frame, F8 `--taa-debug` bursts at rest and during the pan) with `--fade-rt2-owner on`, twice: the default
`--taa-sentinel-stabiliser` (0.7) and `0`. The stabiliser's default becomes 0 when all hold:
1. Log: on the stand frames `overlay_refused = 0` and no `unmatched=overlay_node` row for the four run214 families;
   `fade_routed` at least the station draw count (62 on frame 24630); `unjittered_depth_writers = 0`; `fade_owner = 1`,
   `fade_owner_masked = 0`, `fade_evicted` reported.
2. RT2 dump (`taa_depth` or the lane readback of the burst): valid depth on at least 0.9 of the station crop's detail
   pixels (540 180 700 340, 4,325 luma-detail pixels) and the `taa_mask` b sentinel-class code (1/255 or 1) on 0 of them.
3. Replay (`tools/analysis/taa_resolve_replay.py` on the burst's true `color_*` input): station-crop flicker rms with
   S = 0 at or below 1.20 codes, gradient at or above 9.44, and the S = 0.7 run within the fixture's oracle bound of
   S = 0 on the crop.
4. User: no flicker on the stations under the pan with S = 0, no trail wider than a pixel behind the silhouettes, no
   visible pop at the band edges on approach. Expected, measured in the fixture: one current-only frame over a station
   each time its fraction falls below 400 while it owned RT2 (the owner's falling edge, `original-owner-age` frames 3 and
   8), and one each time it is routed again after a refusal (the rows' one-frame history, as with the option off). If
   the falling-edge frame shows, the follow-up is resolve-side (section 4: accept a history depth against a far-plane
   current where the history's motion alpha was 1), not in this step.

Also watch: near hulls' glass/window sub-meshes of the `53a0a641` family, routed through the overlay arm before, are now
fade owners at fraction 1000 (their depth, coplanar with the hull, lands in RT2: look for trails on windows under a pan);
route_draw_us and loading time against an option-off flight at the same spot.

## 2026-09-24 fade owner: prepass parity over RT2 (fixture)

Closes the "Not done" item above ([fade-rt2-ownership.md](../architecture/fade-rt2-ownership.md) section 7). Two
`faderoute` owner cases (`motion_output_fade_route_inc.h`, the zonly loop; TAA + HDR + rotating camera, linear
materials, R32F RT2, A scissored below the quads): per frame both quads get a depth-only prepass (null PS, Z-write on,
colour mask 0, LESSEQUAL), then the routed fade-band draws of `b0602757fce6e870/517540ae6d5e5410` at fraction 1000
over the sentinel fill. The clip rows carry a depth slope along x (z = .3 - x/8, 3.9e-3 per pixel), so a sub-pixel
offset between prepass and quad decides the Z test. Holes are counted over the 392 interior pixels of both quads
exactly as the run-47 oracle counts colour holes: colour = the draw left the pixel unchanged, RT2 = the pixel still
holds the fill -1; mismatch = the pixels where colour coverage and RT2 write disagree. Bottle X3, measured
([zonly_holes_out.txt](../../verification/results/fade-rt2-owner/zonly_holes_out.txt), `zonly_holes.py` beside it):

| Case | Prepass | RT2 holes / colour holes per frame | Mismatch | `unjittered_depth_writers` / `jittered` per frame | Max RT2 z/w error |
| --- | --- | --- | --- | --- | --- |
| `seam-taa-fade-route-zonly-owner` | z_only alias `c78b4c68a87fce74`, jittered by the route | 0 / 0 on all 12 frames | 0 | 0 / 5 | 4.70e-6 |
| `seam-taa-fade-route-zonly-unjit-owner` | unreviewed vs_1_1, same clip rows, not jittered | 392 / 392 on frames 2, 4, 6, 10 (the jx > 0 frames), 0 elsewhere | 0 | 2 / 3 | 4.46e-6 |

The z/w bound is the flat quads' 4e-6 plus 1/256 px of sub-pixel position on the slope (1.9e-5). Full run
`run_motion_output.py`: 222 cases + 26 bench PASS, the 220 committed cases identical in every non-clock leaf
([compare_motion_zonly_out.txt](../../verification/results/fade-rt2-owner/compare_motion_zonly_out.txt)); summary
re-pinned; `production-zonly` / `seam-zonly` unchanged. Not proven here: the clip rows are chosen so the z_only alias
and the fade VS compute bit-identical depths (x/8 is exact); with the engine's arbitrary rows the two programs may round
the depth differently by an ulp, which would drop isolated pixels in colour and RT2 alike (parity holds per pixel, holes
would not be 0); native-driver behaviour (section 8) is unverified.

## 2026-09-24 Run 79 A: A' region hold accepted (run299/300/302/303, 5120x1440)

User report: no visible difference between `--taa-region-hold on` (run299, run302) and `off` (run300, run303) on the
lattice stand, under slow pans, at silhouettes, on popping shards or in the frames after a pan stops; the known problem
areas (solar-panel lattice crawl, distant-object smear) are fine on both. A flicker of one Terran station part under
motion is present on both settings (triaged separately: `run299-303-run79a/ods-flicker/`), so it is not an A' regression.

Cost (`--gpu-sync-timing`, run302 hold on vs run303 hold off; median over windows of each window's median/p90, us,
measured; windows classed rest or pan by `camera_rotation_deg`; one rest window per run):

| stage | on, rest | on, pan | off, rest | off, pan |
|---|---|---|---|---|
| taa_mask | 1542/1557 | 1544/1564 | 2926/2958 | 2925/2969 |
| taa_mask_x / _y | absent | absent | 674 / 664 | 674 / 664 |
| taa_box | 2337/2359 | 2341/2417 | 2106/2224 | 2107/2225 |
| taa_resolve | 2550/2566 | 2563/2679 | 2450/2565 | 2551/2569 |
| TAA total | 6586/6698 | 6667/6820 | 7724/7867 | 7754/7899 |

The two dilation draws are gone with the hold on (mask 2.93 -> 1.54 ms), the box costs +0.23 ms at rest and under a
pan alike (the region-gated twins), the resolve +0.1 ms; net -1.1 ms per frame at 5120x1440. `motion_output_taa`
logs `region_hold=1|0 ps30_slots=512` (the wined3d figure; programs above it run), no TAA refusal row. The second mask
target's release is shown only by the absent stages (no row logs `line_mask_targets()`; open). Abnormal rows on the
run277/287/298 pattern, no fault on exit, `engine_memory_read_refused` 0. Scripts: `verification/results/run299-303-run79a/`
(`gpu_taa.py`, `gpu_rest_pan.py`, `pan_windows.py`, `taa_frames.py`, `summary.md`).

**Decision (user report + these rows):** A' accepted. Next per taa-plan-lifted-slot-cap.md: remove `--taa-region-hold off`
and the dilated `far_camera` chain (the refusal path becomes region off), then S4 (half-resolution box).

## 2026-09-24 A' only: dilated chain removed (taa-plan-lifted-slot-cap.md step 1, acceptance clause)

After Run 79 A (section above) the camera gate runs the region hold only. Bottle X3, native `d3dx9_37`, measured unless
marked; scripts under `verification/results/aprime-only/`.

- **Removed.** `--taa-region-hold` (the launcher refuses it by name: "was removed on 2026-09-24 ..."; `X3M_TAA_REGION_HOLD`
  is never forwarded and an inherited value is dropped; the DLL ignores a stale value with one
  `taa_region_hold_setting ignored=1 reason=removed` row), `FrameInputs::thin_region_hold`, `configure_region_hold()` /
  `region_hold_available()` / `region_hold_sentinel_available()`. Programs (records, headers, generator entries): the
  non-hold camera resolve `resolve_far_camera` (2,196 words / 555 slots) and its 16-tap twin (2,006 / 505), the ungated box
  programs `thin_box` (238 / 51), `thin_box_rows` (335 / 77) and `thin_box_columns` (420 / 94); `resolve_far_camera.hlsl` and
  `resolve_far_camera_taps16.hlsl`. The x / y dilations were modes 1 / 3 of the shared mask program, not programs of their
  own: the camera variants (`line_mask_camera`, `_depth`, `_depth_thin`) now compile the tests draw alone
  (`#ifndef X3M_CAMERA_GATE` around the other modes; words 1,579 -> 1,020, 1,533 -> 1,002, 1,556 -> 1,030; slots 427 -> 251,
  407 -> 241), so no camera-gate run draws them and the camera composition's `s6` bind is gone from the tests draw. A
  camera-gate run allocates one mask target (the second, 4 B/px, 29.5 MB at 5120x1440, is released or never created).
  The screen-gate chain (`--taa-thin-region-gate screen`, `line_mask_ps.hlsl` modes 1 / 3) and the far stabiliser alone
  (mode 2 into the second target) are unchanged and keep two targets; their programs' bytecode is identical.
- **Pass contract.** `configure_far()` creates the camera mask, the hold resolve and its region-gated 49-tap box (none without
  the FP16 / R32F filter caps: the hold resolve is 5-tap only); `configure_sentinel()` the separable box twins.
  `camera_gate_available()` = those programs and a 5-tap history; a camera-gate run otherwise is refused (E_INVALIDARG),
  so `--taa-history-taps 16` has no camera-gate program (the 16-tap twin was dilated-chain only). `camera_programs_result()`
  holds the first failed creation; `configure_sentinel()` needs `camera_gate_available()` (so nothing after
  `configure_history_taps(16)`). `hold_history_` still restarts the history when a run leaves the camera gate (refused box
  targets, or a screen-gate run).
- **Refusal path (DLL).** Camera gate asked, thin region on, `camera_gate_available()` false: one row
  `motion_output_taa_region_hold device=D unavailable=1 reason=program|no_filter|history_taps16 create=HR bilinear=B
  history_taps=T thin_region=W effect=thin_region_off`, and the thin region is off for the pass (no fallback program set,
  AGENTS.md "Shader slot budget"); the sentinel stabiliser and emissive vote follow with their existing rows
  (`reason=camera_gate_off`, `reason=thin_region_off`), the far stabiliser (its own option) is untouched. The former
  `camera_gate_unavailable=1 reason=camera_program` fallback to the screen gate is gone; `reason=thin_region_off` stays.
  Refused box targets (the lazy FP16 pair, first camera-gate run; not a lost device) no longer fall back to the screen-gate
  chain (orchestrator decision): the pass turns the thin region off for the session (`camera_gate_failed()`, re-armed by
  Reset; a far stabiliser of its own carries on; the requested age pair stays allocated, unused, so the plain history is
  not cut) and the DLL logs one `motion_output_taa_region_hold ... reason=box_target create=HR ... effect=thin_region_off`
  row per failure.
- **Log rows.** `motion_output_taa` drops `region_hold=` (it would be constant; `thin_gate=` and `thin_region=` say whether
  the camera gate runs). `motion_output_taa_history_taps`, once per attachment on its first completed run (the pass is
  created lazily and allocates its targets on the first run, so a row at creation would read 0), gains `mask_targets=N`
  (`TemporalPass::line_mask_targets()`: 1 on a camera-gate run, 2 on the screen gate or the far stabiliser alone, 0 without a
  far run) beside `region_hold=` (the run was a camera-gate run).

TAA programs embedded in the DLL, `RESOLVE_BUDGET` rows (D3DX `instruction slots used`; `slot_table.py` on the 5a4bbd52 and
this run's `temporal-lattice.txt`): 22 budgeted programs -> 17, 31,056 -> 24,771 words (plus the two thin-vote depth programs,
not budgeted: `line_mask_depth_thin` unchanged, `line_mask_camera_depth_thin` 1,556 -> 1,030 words); 24 TAA programs -> 19.

| program | words / slots before | after |
| --- | ---: | ---: |
| plain, snapshot, thin, age, line_mask, far, line_mask_depth | 1786/455, 187/46, 2013/517, 2143/544, 1574/428, 2144/545, 1554/420 | same (bytecode identical) |
| line_mask_camera | 1579 / 427 | 1020 / 251 |
| line_mask_camera_depth | 1533 / 407 | 1002 / 241 |
| far_camera | 2196 / 555 | removed |
| thin_box / rows / columns | 238/51, 335/77, 420/94 | removed |
| plain / thin / age / far taps16 | 1681/432, 1818/465, 1947/494, 1948/493 | same |
| far_camera_taps16 | 2006 / 505 | removed |
| far_camera_hold, thin_box_hold / rows_hold / columns_hold | 2479/616, 314/70, 628/155, 533/121 | same (bytecode identical) |

All shader records regenerated natively (the generator changed): 80 -> 75 records, bytecode identical except the three
camera mask programs; the nine bloom records regenerated (bytecode identical, they pin the generator's hash).

Evidence:
- `run_temporal_pass.py` (`X3M_FIXTURE_BOTTLE=X3`, through `wine_lock.py`; 133 s, lock wait 0 s): PASS, base
  744 / 278 / 2 generations, 546 samples (counts unchanged; 38 of 11,786 non-timing lines changed, all rows of the
  `far_camera` program, now the hold resolve: 30 `MOTION_WEIGHT`, 4 `SETA_EXIT` yaw rows, 4 `MOTE_STREAK`; the exit
  analysis reads `floor(|age|)` with the sign, as the hold's age readers do), lattice 571 / 91 -> 554 / 91 (the box-target
  rerun: 119 s, base report byte-identical, `report_sha256` 5f3c22f8...)
  (`report_diff.py`; `FAR_BASE` 299 / 6 unchanged, `DEPTH_FOLD_BASE` 508 -> 487, `HISTORY_TAPS_BASE` 528 -> 508).
  Removed (21 checks): the camera configuration's three `THIN_REGION` rows (13; `THIN_REGION_HOLD` carries the same oracle,
  age, tests-target, silhouette and ripple checks), `THIN_REGION_CAMERA_STATIC` x3 (3; the camera gate's bit-identity with
  the screen gate at a static camera was the dilated chain's), the camera `THIN_REGION_PAN` oracle row (3;
  `THIN_REGION_HOLD_PAN`), `SENTINEL_STABILISER row=mover` (2; its 8-px reach was the 17x17 minimum). Rewritten on the hold's
  tests target, per pixel (same counts): `THIN_REGION_CAMERA` (camera openness over the field 1.0000, screen channel 0),
  `THIN_REGION_GLASS_MOVER`, `THIN_REGION_CAMERA_FORWARD` (the rotation-only gate closed on all 70 routed row pixels; the
  output identity with the screen gate is reported, not asserted), `THIN_REGION_CAMERA_FLIGHT` x15 (the tests target
  against the oracle's own per-pixel gates, error 0 on every modelled case; the lane hole and the mover checked on their
  routed pixels), `THIN_REGION_BAD_MOTION` (both gates 0 on the covered pixels, every output finite, in place of the output
  identity with the screen gate); the box-target failure row now expects the plain resolve bit for bit (refused on frame 0 with the two colour
  histories allowed; was: the screen gate bit for bit); the `THIN_REGION_STALE` / `_BOX_DOMAIN` camera halves and the sentinel rows run the
  hold with its oracle (facets flicker ratio 0.38-0.48 on the five asserted pans, bound 0.6). The dilated comparisons in
  the `THIN_REGION_HOLD_*` rows are gone; `THIN_REGION_HOLD_PAN_STOP` now bounds the step against the plain resolve
  (1.983 / 1.995 codes against 21.07 / 21.22, bound 0.15 x; the dilated chain measured 0.094 x) and `_STALE` gains the
  clip-off bound. Added: `REGION_HOLD_IDENTITY_REFERENCE` (the identity's camera program compiled from `resolve.hlsl` with
  `X3M_CAMERA_GATE` / `X3M_FAR_STABILIZE` is the removed embedded program word for word: 2,196 words, FNV-1a
  457159f1f8e5c6b3), `REGION_HOLD_STATE refused_path=1` (the hold resolve refused at `CreatePixelShader`: no camera-gate
  program, `camera_programs_result()` 8876017c, a camera-gate run and `configure_sentinel` refused, the screen gate runs),
  `taps16_refused=1`, `screen_restarts=1`, `box_refused_region_off=1` (box pair refused on a camera-gate run: E_OUTOFVIDEOMEMORY
  in `camera_gate_result()`, plain resolve, no mask or age published; the next run retries nothing and continues the history;
  new check), masks 1 / 2 / 1 / 0 / 1 / 1 (camera, screen, camera again, at Reset, after it, after the refused boxes), `HISTORY_TAPS_NO_FILTER_CAMERA` (filter query refused: far program kept, no camera-gate program, create
  8876086a, camera-gate run refused). `HISTORY_TAPS` runs the far program on the screen gate for its 16-tap twin
  (the camera gate has none). Every `RESOLVE_BUDGET` row `within_ceiling_2048=1`.
- Fixture pass time (`LINE_TIMING_CAMERA`, 1280x768, CPU wall with an event-query drain, one run each, measured, noise
  about +-0.15 ms between runs of this build): camera gate on the fragmented pan frame 1.833 -> 1.319 ms (over the
  screen-gate thin region 0.654 -> 0.212 ms), with no region 1.372 -> 1.127 ms; the flight's figure is Run 79 A's
  (`taa_mask` 2.93 -> 1.54 ms at 5120x1440, already without the x / y draws).
- `run_motion_output.py`, full run (640 s, lock wait 0 s; run A: the build before the box-target edit, the figures of this
  paragraph unless marked run B): PASS, 221 cases + 26 bench. Against the committed summary
  (`compare_motion.py`): 219 of the 222 committed cases identical in every non-clock leaf (`seam-taa-thin-hold-on` included:
  the camera mask's new bytecode writes the same tests target), 53 clock leaves differ; removed `seam-taa-region-hold-off`
  and `seam-taa-region-hold-too-long` (the option's parse) and `seam-taa-thin-hold-off` (the dilated chain through the DLL);
  added `seam-taa-region-hold-ignored` (X3M_TAA_REGION_HOLD=off: one `taa_region_hold_setting ignored=1 reason=removed`
  row, byte-identical to `seam-taa-on`) and `seam-taa-thin-taps16-refused` (thin region 0.97 + stabiliser 0.7 under
  `X3M_TAA_HISTORY_TAPS=16`: one `motion_output_taa_region_hold ... reason=history_taps16 create=00000000 bilinear=1
  history_taps=16 thin_region=0.9700 effect=thin_region_off` row, the stabiliser's `reason=camera_gate_off` row,
  `thin_region=0.0000`, colour hashes and FP16 histories byte-identical to `seam-taa-taps16`; references base + 9). The
  camera-gate case holds 7 + 2 references on top of the base (was 7 + 2 + 4) and its first run logs `region_hold=1
  mask_targets=1`; the other seam TAA cases (no far run) log `mask_targets=0` and no refusal row. `motion_output_taa` carries no `region_hold=`.
  With 16 taps the three camera-gate programs are still created by `configure_far` (before the camera gate is judged) but
  never run: `seam-taa-thin-taps16-refused` holds base + 7 + 1 (configure_far's seven programs and the far 16-tap twin);
  accepted (one-time program creation, no draw, no target).
- Run B, after the box-target edit (review item F1): `run_motion_output.py seam-taa-thin-hold-on
  seam-taa-thin-taps16-refused seam-taa-region-hold-ignored seam-taa-on seam-taa-taps16` on the final sources (87 s, lock
  wait 0 s; seam DLL sha256 8b9faa1b...): all five cases pass their per-case checks, every non-clock leaf identical to
  run A (`compare_partial.py`; `frames_changed_by_history` is a full-run cross-case field and absent in a partial run),
  and the twins' colour hashes identical (taps16-refused = taps16, region-hold-ignored = taa-on). The two reviewed original
  programs the runner pins (`/tmp/x3-shader-sweep/programs`, gone from `/tmp` by then) were staged from the run299 capture
  dump with the pinned SHA-256 and removed afterwards.
- Build (MinGW i686, RelWithDebInfo): 0 warnings; `check_no_x87.py build/d3d9.dll` PASS, 683 reachable functions, 0
  violations (final sources). DLL 56,660,937 -> 56,582,639 bytes (-78,298; `.text` 37,010,116 -> 36,981,824, -28,292 B by
  `i686-w64-mingw32-size`) for the final build against a 5a4bbd52 build with the same toolchain (run A's build, before the
  box-target edit: 56,581,488). Host tests: the 17 affected modules (`test_taa_*`, the launcher modules,
  `test_bloom_programs`, `test_motion_output_runner` / `_profiles`, iteration 07 / 08 TAA) 205 tests, 0 failing; the full
  suite (`run_host_suite.py`, the shader records all regenerated) 253 modules, 2,638 tests, 0 failing.
- Triage parsers (`parser_check.py`, no Wine): every script of `run299-303-run79a` and `run295-298-run78a` that takes run numbers runs on run299 (exit 0);
  the five with a run299 line in their committed output reproduce it exactly (`abnormal_rows`, `bolt_rows`, `dt_windows`,
  `exit_rows`, `taa_frames`); `taa_frames.py`, the only one that reads a reformatted row, parses a synthetic run299 excerpt
  with the new `motion_output_taa` and `motion_output_taa_history_taps ... mask_targets=1` rows (5,000 frames, exit 0,
  the row carried). The others read frame, gpu-sync, abnormal, exit and bolt rows this change does not touch.

## 2026-09-24 S4 half-resolution box (taa-plan-lifted-slot-cap.md step 2; opt-in, fixture, not flown)

`--taa-box-resolution full|half` (`X3M_TAA_BOX_RESOLUTION`; requires `--taa`, refused under `--vanilla`, forwarded only
when given; DLL default full). Bottle X3, native `d3dx9_37`, measured unless marked; scripts under
`verification/results/s4-half-box/`.

- **Window arithmetic.** Block (bx, by) = pixels 2bx..2bx+1 x 2by..2by+1; the union of their 7x7 windows is the 8x8
  window 2b-3..2b+4, the intersection the common 6x6 2b-2..2b+3, the union of their 3x3 the inner 4x4 2b-1..2b+2. The row
  draw (`thin_box_rows_half_ps.hlsl`) writes row PAIRS starting at an odd row, texel (bx, g) = rows 2g-1, 2g over the 8
  columns, into W/2 x (H/2 + 1) targets, so the column draw (`thin_box_columns_half_ps.hlsl`) reads exactly 4 pairs
  (by-1..by+2 = rows 2by-3..2by+4) into the W/2 x H/2 box pair: 16 + 8 fetches per block, 6 per pixel instead of 21.
  The resolve is unchanged: its POINT read of s9 / s10 at (x + 1/2) / W lands on texel x >> 1 (a quarter texel inside) for
  an even size; an odd width or height draws the full-resolution box that run (reason `odd_size`).
- **Containment.** A min / max over a superset window contains the smaller one, and clamped addressing keeps it (a clamped
  window is its in-frame part). The block is marked computed where any of its four pixels opens the full-resolution gate,
  so a pixel of that block whose own gate is closed (b > a only through the hold edge) now clamps its added strength to
  the block box instead of its 3x3 clip: looser there, a behaviour change of the half path (the block box contains that
  3x3; the fixture checks it on the half-only pixels). The emitter bound as sketched in S4 (inner 4x4 when the 8x8 exceeds E) would be tighter than the 7x7 at a
  pixel that does not fire; as built, with a tap above E in the 8x8: all four pixels on the sentinel and a bright tap in the
  common 6x6 (every pixel fires) take the inner 4x4; all four on the sentinel with the bright taps in the outer ring only
  take the 8x8 box of the dim taps (a firing pixel's 3x3 lies in the dim common 6x6, a non-firing pixel's 7x7 is all dim);
  across a silhouette the 8x8 of every tap, fetched in the column draw (64 taps, blocks straddling a silhouette within 4 px
  of a bright tap only). The row targets carry the dim-tap min / max and a code (bright tap in the inner columns of the
  upper / lower row, anywhere in the pair). The sentinel-class term of the gate applies only while the stabiliser runs
  (c23.y): the tests draw writes the class whatever S is, and the 49-tap box does not open on it.
- **Programs and targets** (RESOLVE_BUDGET, D3DX words / slots; every other embedded program byte-identical, the records
  regenerated for the generator's own hash only):

  | program | words | slots |
  | --- | ---: | ---: |
  | `thin_box_hold` (full, stabiliser off) | 314 | 70 |
  | `thin_box_rows_hold` (full) | 628 | 155 |
  | `thin_box_columns_hold` (full) | 533 | 121 |
  | `thin_box_rows_half` (new) | 670 | 158 |
  | `thin_box_columns_half` (new) | 1,316 | 330 |

  Half mode draws the pair for every camera-gate run (stabiliser on: c23 = (E, 1); off: (0, 0), the 49-tap box's gate and
  no bound), row targets 2 x FP16 W/2 x (H/2 + 1), box targets 2 x FP16 W/2 x H/2: about 8 B/px against 32 B/px (box and row
  pairs at full resolution), -177 MB at 5120x1440 while the sentinel stabiliser runs (the default; inferred from the
  sizes); without the stabiliser the full path holds the box pair only (16 B/px) and the half path both pairs, -59 MB. Allocated on the first half run, a pair of the
  other size released first (a configuration change, never per frame), released with the histories / Reset.
  `TemporalPass::configure_box_resolution(1|2)`; 2 creates the pair (none otherwise); a refusal keeps 1 and returns the
  failure. `Diagnostics::box_half` / `box_resolution_reason` (`half`, `not_requested`, `no_camera_gate`, `odd_size`,
  `target`); `Output::box_low` / `box_high` (diagnostic). DLL: one `motion_output_taa_box_resolution requested=half
  configured=half|full reason=ok|program|camera_gate_off create=HR` row at pass creation and one `... drawn=half|full
  reason=...` row per change of reason, both only when half is asked; full logs nothing.
- **Fixture, `run_temporal_pass.py`** (final sources; 157 s, lock wait 0 s): PASS. `temporal-pass.txt` 744 / 278, byte-identical
  to the committed report (`report_sha256` 5f3c22f8..., all 11,786 lines; `report_diff_pass_out.txt`, produced by
  `verification/results/aprime-only/report_diff.py` against the committed report). Lattice: the A' block unchanged
  (`REGION_HOLD_BASE numerical=554 state_restorations=91`, every earlier line identical, `report_diff_lattice_out.txt`, the
  same script), then the S4 block: `RESULT PASS numerical=594 state_restorations=92` (40 + 1, after the review fixes). Rows
  (`box_half_rows.py` -> `box_half_rows_out.txt`):
  - `BOX_HALF_STATE`: 1 / 2 only; a refused half program keeps the full box bit for bit (colour, age, box targets, three
    frames); S/2 targets; hostile state (c12, c23, the viewport) restored; failed row / column draws publish nothing and
    the next run restarts; full <-> half keeps the history; refused half targets (the row pair, or the box pair alone) draw
    full (`target`, no retry, the thin region stays on) until Reset; an odd size (31x31) draws full (`odd_size`).
  - Containment, per pixel and frame, no tolerance, 12 scenes (the 12th, `sentinel_tap_above_e_fp32`, with the review fixes below): 0 violations everywhere. Compared pixels (full box
    computed): pan 0.5 130,848; stale patch 130,272; box domain (k = 0.5, a 65,504 tap) 130,720; stop after pan 65,312;
    sentinel facets at rest and under a reversing 2 px/frame pan 131,072 each; emitter 26,624; emitter over the props
    (silhouette blocks) 23,296; non-finite block 32,480 (288 current-only pixels skipped). The pixels only the half box
    covers (224 per pan scene, 832 in the silhouette scene) contain their own 3x3. The static arm scenes open no box at
    either resolution and are identical bit for bit.
  - Oracle with the block box (9 scenes): colour error 0.0000-0.0103 (bound 0.02), age 0, tests target exact. The
    colour-only bar over routed geometry is containment only: the oracle misses it at full resolution by the same 3.06 / 25.
  - Rest ripple 1.416 codes rms / 6.6 p2p, equal to full (installed resolve 16.34 / 99.4; bounds 0.35 x / 0.5 x); stop
    after pan 1.983 / 1.995 (plain 21.07 / 21.22; bound 0.15 x); stale patch +0.727, equal to full (screen gate 0.623;
    bound 2 x); sentinel flicker ratio 0.392 at rest, 0.384 reversing (equal to full; bound 0.6). Outputs equal full in
    every scene but the two emitter scenes: the box binds on injected or emitter history only (as the lattice replay found).
  - Emitter bar (value 4, 6 px/frame, S = 0.7, E = 1): trail 1 px full, 2 px half (asserted <= 2), none after it left.
  - Timing, the box stage alone (TaaBox boundaries, event-query drained; the fixture's CPU wall clock, all-sentinel pan
    frame, 8 rounds): 1280x768 0.592 -> 0.474 ms (-0.12); 5120x1440 2.450 -> 1.388 ms (-1.06); whole run at 5120x1440
    6.67 -> 5.95 ms. Earlier runs of the same row: 1280x768 -0.09 to -0.17, 5120x1440 -1.08 to -1.19 (fixed per-draw cost
    dominates at 1280x768 on this backend). In the game (inferred): Run 79 A's `taa_box` 2.34 ms at 5120x1440 x
    (1.388 / 2.450) = about 1.33 ms, -1.0 ms; the S4 note's -1.4 to -1.8 assumed the tap ratio (21 -> 6) alone.
- **Motion output, `run_motion_output.py`** (full run, final sources; 562 s, lock wait 6 s; a first attempt stopped at
  `seam-hdr-ramp-identity-dither`, whose fixture hung after device creation until the 360 s timeout, no TAA box in that
  case; the rerun passed it): PASS, 222 cases + 26 bench. `compare_motion.py` (`compare_motion_out.txt`): all 221 committed
  cases present, every non-clock leaf identical (53 clock leaves differ), 26 / 26 bench present, one new case,
  `seam-taa-thin-hold-half` (the thin-hold case with `X3M_TAA_BOX_RESOLUTION=half`): references base + 11 (the half pair on
  top of 7 + 2), rows `requested=half configured=half reason=ok create=00000000` and `drawn=half reason=half width=64
  height=64`, the reference's full-resolution shadow pass `REFERENCE_BOX_CONTAINMENT frames=12 compared_px=19319
  violations=0`, and every summary leaf equal to `seam-taa-thin-hold-on`, colour hashes included (`twin_compare.py`: only
  the directory and trace hash differ): the box does not bind in that script.
- **Build and host tests.** MinGW i686 RelWithDebInfo, final sources (the motion runner's clean build): 0 warnings;
  `check_no_x87.py build/d3d9.dll` 683 reachable functions, 0 violations; DLL 56,660,565 bytes (+77,926 against the
  56,582,639 of bb3a691f's build). Host modules `test_taa_*` (including the new
  `test_taa_box_resolution.py`: forwarded only when given, full / half only, requires `--taa`, refused with `--vanilla`, an
  inherited value dropped; the DLL parse and the logging-only-when-half rule; the generated records), the launcher modules,
  `test_bloom_programs`, `test_check_no_x87`, `test_motion_output_runner`, iteration 07 / 08 TAA: 176 tests, 171 pass; the
  5 of `test_launcher_stderr_tee` / `test_voice_decoder_launch` refuse with "X3AP.exe is running" because another agent's
  shell carried "X3AP.exe" in its command line (`pgrep -ifl` in `media_package.assert_game_closed`); with a scratch
  `pgrep` stub that reports no match those two modules pass (28 tests). Every shader record was regenerated (the generator's
  own hash is in each record's `tool_sources`); every existing header is byte-identical.
- **What stays off with `full`.** The pass without `configure_box_resolution(2)` is the pre-S4 pass: no half program,
  target, constant or draw; the DLL creates and logs nothing for `full` or an absent variable (the motion-output runner
  pins `full` for every case but the half twin). An invalid value logs `taa_box_resolution_setting invalid=1` and stays full.
- **Run 81 A/B (proposed).** 5120x1440, `--gpu-sync-timing`, the default TAA (thin region with the camera gate, sentinel
  stabiliser 0.7 / E 1), `--taa-box-resolution half` against the same session without it. Look at: `taa_box` (expected about
  2.3 -> 1.3 ms, inferred), `motion_output_taa_box_resolution` rows (`configured=half`, then `drawn=half reason=half`, no
  `odd_size` / `target`); the run221 distant-station pan (stabiliser flicker must not return); laser fire and engine trails
  over the sky (halo at most 2 px); silhouettes against the sky under a pan (the 8x8 box reaches 4 px on one side instead
  of 3: slightly longer ghost); the lattice at rest and in a pan (unchanged in the fixture); alt-tab / Reset (targets
  re-created, double cursor). Pass: no visible difference but the stage time.
- **Review fixes (2026-09-24).** (1) The half pair no longer sets the full-frame viewport while RT0 is still the W/2 x H/2
  box target (documented D3D9 refuses a viewport larger than the target); the resolve's `SetRenderTarget(0, ...)` resets it.
  (2) Emitter precision: the full path tests FP16-rounded row maxima (`FP16(luma) > E`), the half rows now test
  `luma >= c23.z` with c23.z = `x3::temporal::fp16_above(E)`, the smallest FP16 value above E (`src/temporal/resolve.h`),
  so a tap bright at half resolution is bright at full resolution under any rounding mode (a luma just below c23.z may
  be bright at full only, which keeps the half box looser). New containment scene `sentinel_tap_above_e_fp32`: a
  colour-only pixel (1 + 2^-10, 1, 1), luma about 1.0002 against E = 1, on the flat sky with S = 0.7: 32 frames, 32,768 px
  compared, 0 violations; negative control (c23.z = the float just above E, the pre-fix test, scratch build only): 1,056
  violations, worst 0.751. (6) `motion_output_taa_box_resolution` per-change rows are capped at 8 per attachment, then one
  `suppressed=1 changes=9` row. Host test additions: the invalid-value row, the cap, and the window arithmetic (the
  resolve's block texel, the row pairs' rows, the pairs the columns read, the common rows of the emitter code, the inner
  4x4) against a brute-force union / intersection at 32, 768, 1080, 1440 and 5120.
  After the fixes: `run_temporal_pass.py` PASS (138 s, lock wait 0 s; an earlier attempt printed the same `RESULT PASS`
  but its fixture hung at process exit past the runner's 600 s lattice timeout, a Wine teardown stall, and was discarded):
  `temporal-pass.txt` byte-identical (744 / 278, 11,786 lines), lattice `REGION_HOLD_BASE 554 / 91` then `RESULT PASS
  numerical=594 state_restorations=92`, 12 containment scenes with 0 violations, box stage 2.420 -> 1.350 ms at 5120x1440
  and 1.024 -> 0.836 ms at 1280x768 (fixture wall clock). Build (incremental over the changed sources) 0 warnings,
  `check_no_x87` 683 / 0, DLL 56,661,939 bytes; host modules as above 150 + 28 tests, 0 failing. `run_motion_output.py`
  not rerun: the DLL-side change is the log cap (no case reaches 8 changes); the pass change is in `temporal_pass.cpp`,
  which `run_temporal_pass.py` compiles and ran.
- **Default since Run 82 (after Run 81 A launch 2 below).** The launcher sends `half` with `X3M_TAA_BOX_RESOLUTION_DEFAULT=1`
  on every modded `--taa` launch (explicit `full`/`half`: marker 0; nothing without `--taa` or under `--vanilla`); the creation
  row carries `default=1|0`; the DLL default when the variable is unset stays full (fixtures unchanged).

## 2026-09-24 Run 80 A launches 1-2 (run304 baseline, run305 occlusion): A'-only build flown

Both sessions on DLL 593112dc (one launch each, 5120x1440, 14,961 / 14,680 frames, no fault, refused, late_claim or
rollback rows; measured, `verification/results/run304-305-run80a/`). `motion_output_taa_history_taps requested=5
drawn=5 region_hold=1 mask_targets=1` in both. No GPU pass figures: `--gpu-sync-timing` was off and frame time is
pinned at vsync (rest p50 17.0 / 15.0 ms, pan 16.0 / 16.0 ms), so the A'-only TAA cost stays at Run 79 A's measured
1.54 ms mask + 0.23 ms box until a `--gpu-sync-timing` launch. CPU encode `taa_run_us` median 197 / 170 µs. Neither
session carried `--taa-thin-vote` or `--fade-rt2-owner` (no setting rows): launches 3 and 4 are pending.

## 2026-09-24 Run 80 A launch 3 (run306, `--taa-thin-vote on`): the vote flown

Active the whole flight (`taa_thin_vote_configured requested=1 enabled=1`; `thin_vote_mode enabled=1 route=1 depth=1
lane=1 cache=1 vote_fraction=0.50 reads_per_frame=16 triangles_per_frame=65536`; measured,
`verification/results/run306-run80a-thin-vote/`). 9,402 frames at 5120x1440, normal exit, no error/late_claim/rollback
row; voted draws on 7,520 frames (median 5, p99 72, max 93 per frame); 440 buffer reads over 95 frames (438 measured, 2
`geometry` unreadable), invalidation queue 880 invalidated / 440 dropped over 6 frames; `not_managed`, `not_readable`,
`stale`, `lock_failed`, `overflows`, `volatile_*`, `refused` all 0. Cost: lock 19.2 ms + measure 52.7 ms over the whole
session; on the 95 read frames lock+measure median 417 µs, max 2.33 ms; `draw_us` always 0 (telemetry gap). The F8
burst (frames 4610-4617, 16 voted draws per frame): routed pixels 5.2-5.4 % of the screen, 42 % of them carry
0 <= `.a` < 1 (16 distinct values, min 0.0113 = the logged `min_alpha`, median 0.485), the rest `.a` = 1; run304 has
`.a` = w > 1 everywhere, so the vote reached the resolve's RT2 input. The user noticed no issue at the lattice stand at
rest and under a pan or on a large near station. No frame-time comparison: run306 also carried `--gpu-sync-timing`
(serialising; median dt 34 ms) while run304 did not (16 ms); the GPU passes (`taa` 6,784 µs, `taa_resolve` 2,624 µs,
`scene` 27,136 µs) are in line with run303. Save load 16.3 s (run304 19.2 s). **Accepted as the Run 81 default
(`--taa-thin-vote on`), with the cost A/B in that flight.** Open: `draw_us` never recorded and `missed` non-zero while
`reads` sits at the 16-per-frame cap (source check pending); a reload at frame 6202 without a `loading_phase` row.

**Run 80 A launch 4 (run307, fade owner):** accepted as the Run 81 default; stabiliser removal still needs the S = 0
launch (details in [linear-distance-fade.md](linear-distance-fade.md)).

**Run 80 A extra launch (run308, baseline + `--gpu-sync-timing`, vote and owner off):** the A'-only TAA GPU cost at
5120x1440 measured: `taa` 6.78 ms (pan, window median), `taa_mask` 1.49, `taa_mask_tests` 1.44, `taa_box` 2.41,
`taa_resolve` 2.68 ms; run306 (vote) and run307 (owner) match every stage within about 50 µs, so neither feature adds
GPU pass time (measured, `verification/results/run308-run80a-gpu-baseline/gpu_rest_pan_out.txt`). Frame dt (gpu-sync,
serialising) p50/p95 over TAA windows 30.7/36.5 ms; run306 33.8/39.9, run307 31.1/35.6: the +3.1 / +0.5 ms follow
scene/engine time and flight content, not the features (inferred; no same-route A/B). Mask stays at Run 79 A's hold-on
level (run302 1.54 ms; hold-off run303 2.93 ms).

**Run 81 defaults (launcher and DLL row, not flown):** `--taa-thin-vote` and `--fade-rt2-owner` default on when their prerequisites are given (else not sent, one `default on not sent:` launcher line); the configured rows log for on and off with `default=1|0`; host tests `test_taa_thin_vote`, `test_fade_region`. Wine (measured, X3, partial `run_motion_output.py`): `seam-thin-vote-far-on` 56 checks (`requested=1 enabled=1 default=1`, marker set), `seam-thin-vote-far-off` 47 (`requested=0 default=0`), `seam-taa-fade-route-routed` 5100 (owner row `requested=0`), its owner twin 5185 (`requested=1 enabled=1`).

**Run 81 defaults, age programs (launcher, not flown):** with `--taa` on a modded launch `--taa-far-stabiliser` defaults to 0.985 and `--taa-thin-region` to 0.97 (the Run 80 A stand command; `off`/0 turn them off; nothing sent under `--vanilla` or without `--taa`); the gate, emissive vote and sentinel stabiliser defaults derive from them as before; host test `test_taa_image_defaults`.

## 2026-09-24 Run 81 A launch 2 (run310, `--taa-box-resolution half` + gpu-sync): S4 flown

Half configured and drawn from frame 410 (`requested=half configured=half reason=ok`, `drawn=half width=5120
height=1440 target_create=00000000`), no fallback, no Reset, clean exit, 8,325 frames (measured,
`verification/results/run310-run81a-s4-half/`). GPU (window medians, µs): `taa_box` 1,384 rest / 1,424 pan against
run309's 2,358 / 2,360 and run308's 2,406; over the clean early windows (300-2099) box 1,296 vs 2,350 and TAA total
5,440 vs 6,600-6,700, so S4 saves about 1.06 ms on the box and about 1.2 ms on the TAA total at 5120x1440 (measured;
the whole-session mask/resolve rise of +0.18 / +0.13 ms follows the heavier later scenes, scene 28.2 vs 26.1 ms,
inferred). The user saw no issue on the lattice at rest and under a pan, trails over sky, silhouettes and the
distant-station pan. **Accepted: `half` becomes the default (Run 82 candidate).** Not logged: the half target sizes;
no box lane in the F8 dumps (block-constant check not possible). Unrelated: 8 `shadow_replay_depth_refused`
(`sun_changing` / `sun_relatched`) from frame 5542.

## 2026-09-24 thin-region source A/B (opt-in; fixture, not flown)

`--taa-thin-region-source both|screen|vote` (`X3M_TAA_THIN_REGION_SOURCE`, DLL default both; forwarded only when given;
needs `--taa` and the thin region with W > 0, vote also `--taa-thin-vote on`, both defaults count; refused under
`--vanilla`) selects what feeds the thin-region flag for the Run 82 A/B ([taa-thin-geometry-alternatives.md](
../architecture/taa-thin-geometry-alternatives.md) section 3.2). Bottle X3, native `d3dx9_37`, measured unless marked.

- **Mechanism.** both: today's mask bit for bit (`c10.y` = 0, the same program choice; the twins gain one uniform branch, slots below). screen: the pass
  draws the plain fold program instead of the thin-vote twin (`Diagnostics::thin_vote_reason` `screen_source`, one
  `thin_vote_absent` row); the route still uploads `c218` and the lane still carries `.a`, so the vote's rows stay
  comparable. vote: the twin with `c10.y` = 1 (`line_mask_ps.hlsl` `X3M_THIN_VOTE`, read by the twins only; it rides the
  emissive constant's existing upload) skips the 7-tap search on every pixel; the emissive vote (E > 0) is its own opt-in
  and still runs. Without the twin (lane off, two-channel depth, refused program) a vote run is a both run
  (`Diagnostics::thin_region_source`). No new draw, program, constant upload or per-draw work.
- **Configured row** (`motion_output.cpp`, per device, only when given): `taa_thin_region_source requested= configured=
  reason=` with `thin_region`, `thin_vote`, `twins`, `camera_gate`; reasons `thin_region_off`, `thin_vote_off`,
  `program` configure both. Invalid or oversized value: `taa_thin_region_source_setting invalid=1`, both.
- **Which mask draws vote can skip: none.** On the flown A' path the mask is the tests draw alone, and it also writes
  the camera and screen gates, the far weight, the sentinel class and (S1) the next depth history; on the screen gate
  the 11x11 / 17x17 draws grow the vote's flags as well. Only the search inside the tests draw goes.
- **Temporal pass** (`run_temporal_pass.py` through `wine_lock.py`, 23:40:54-23:45:04 including a lock wait behind
  another agent's run): PASS 744 / 278 / 546; `temporal-pass.txt` byte-identical to the committed one (`5f3c22f8…`);
  lattice PASS 610 / 107 (594 / 92 before, `BOX_HALF_BASE`), differing lines only wall-clock rows and the new ones, no
  existing `RESOLVE_BUDGET` row changed ([compare_temporal_out.txt](../../verification/results/thin-region-source/compare_temporal_out.txt),
  script beside it). New cases (`temporal_thin_source_inc.h`, 16 numerical, 15 state restorations; 64 x 32 lane, both
  gates): tests-target flag and history kept after 40 frames of 0.8 and one of 0.2, per class: unvoted struts over sky
  (U), voted struts over sky (W), voted bars over a panel at 0.25 vs 0.26 (V), sky and panel controls. both flags U W V,
  screen U W, vote W V, each flagged class keeps 0.9699 of its history (0.97 in FP16) and every unflagged one 0.0000.
  screen, and vote without its twin (no input; both twins refused at creation), are byte-identical to the plain run in
  colour, depth, age and mask on both gates; a source outside the enum returns `E_INVALIDARG` with the hostile state
  intact; the vote-only tests target is unchanged across a device Reset.
- **Motion output** (partial `run_motion_output.py seam-thin-vote-far-on-source-{both,screen,vote}`, 23:47-23:48): exit 0, 61 checks each, one
  configured row each (`configured=` the request, `reason=ok`, twins 1). The struts (1.4 px, never fragmented) are
  flagged (b 254) under both and vote and not under screen, where `thin_vote_frame` still reads `voted=1`. Unvoted
  flagged pixels (the search's own flags, the fill's silhouette corners) 60 per frame under both and screen, 0 under
  vote ([motion_rows_out.txt](../../verification/results/thin-region-source/motion_rows_out.txt), script beside it).
- **Cost.** The tests draw per source at 5120 x 1440 (fixture clock, event-query drained, 8 rounds, not GPU time):
  unvoted panel both / screen / vote 1.53 / 1.45 / 1.40 ms, sky 1.45 / 1.40 / 1.38 ms: vote saves 0.07-0.13 ms, about
  the spread between both and screen, which run the same search. Flown `taa_mask_tests` is 1.44 ms at 5120 x 1440
  (run308, above), so dropping the screen-space test alone is expected to save about 0.1 ms, not the mask pass
  (inferred). Slots (`RESOLVE_BUDGET`): twins 435 and 257 (1,583 and 1,042 words, 1,575 and 1,030 before); the four
  plain line-mask programs' bytecode is unchanged (their records carry only the new include hash).
- Scratch DLL (MinGW i686, RelWithDebInfo): 0 warnings; `check_no_x87.py` PASS, 684 reachable functions, 0 violations.
  Host: `test_taa_thin_region_source` (9 tests: the launcher option, the row parser, the source contract),
  `test_motion_output_runner` (HDR-case count 102 -> 105), `test_taa_thin_vote`, `test_taa_box_resolution`,
  `test_taa_image_defaults`: OK.
- **Found, not changed:** a camera-gate sequence whose first runs were not from the hostile state failed the state
  check on stream 0 / 1 offsets alone (24 -> 0, 48 -> 0; buffers and strides restored): the pass's `D3DSBT_ALL` block,
  created on the first run, did not take the later offsets at `Capture` here (inferred from the byte diff; Wine's
  stateblock). The existing cases create the block under the hostile state, which hides it; the new cases now do too.

Not flown. Run 82 A/B: `--taa-thin-region-source vote` against `both` at the lattice stand and on hulls: whether the
voted draws alone keep the lattice crawl down, and `taa_mask_tests` with `--gpu-sync-timing`.

**Fixed 2026-09-25 with the mask fold (below; was open, filed 2026-09-25, from the thin-region-source review):** the temporal
pass's `D3DSBT_ALL` state block does not refresh stream offsets on `Capture` (fixture: streams 0/1 offsets 24 -> 0 and
48 -> 0 when the pass was created under different offsets); the fixture cases start each frame from the hostile state,
which hides it. `SavedState` now restores every stream's source and frequency itself; `REGION_HOLD_STATE streams_restored=1`
runs from a non-hostile start.

**2026-09-25: sentinel stabiliser off by default on modded `--taa` launches.** Run 82 A launch 2 (run313/run314,
[run313-run82a-stabiliser-off](../../verification/results/run313-run82a-stabiliser-off/)) flew `--taa-sentinel-stabiliser 0`
with the fade RT2 owner covering the alpha-tested station cutouts: valid depth 0.970-0.974 / 0.911-0.965 of the plant
crop's detail pixels (rest burst 1673 / pan burst 2124; the second plant in the pan 0.47-0.49), `fade_refused` 0 and no
`unmatched=no_zwrite` refusal, no shimmer seen by the user (`docs/architecture/fade-rt2-ownership.md` section 5
conditions 1, 2, 4; condition 3, the replay, open: no `color_*` input dumped). `tools/manage.py` now sends
`X3M_TAA_SENTINEL_STABILISER=0` with `X3M_TAA_SENTINEL_STABILISER_DEFAULT=1` when `--taa` is given without the option
(also without the camera gate, where 0 is inert), an explicit value with marker 0 (0.7 restores the previous look for an
A/B), nothing without `--taa` or under `--vanilla`. The DLL reads the marker only with a valid value and logs
`sentinel_stabiliser_default=%u` at the end of the `motion_output_taa` row; its fallback when the variable is unset stays
0.7 under the camera gate (fixtures unchanged). `run_motion_output.py` drops an inherited marker. Host:
`test_taa_sentinel_stabiliser_default` 6 tests, `test_taa_image_defaults` updated to the new default, 55 launcher modules
739 tests OK; dry runs: `--taa` 0/1, `--taa-sentinel-stabiliser 0.7` 0.7/0, `--vanilla` neither; build 0 warnings,
`check_no_x87` 684 / 0.
(Superseded the same day by the mask fold below: the option and its marker are removed, not defaulted.)

## Mask fold (2026-09-25; design `docs/architecture/taa-mask-fold.md`, option (a), ratified with the screen search default off)

The camera gate's line-mask tests draw is folded into the A' resolve: `resolve_far_camera_hold.hlsl` reads the caller's
current depth at s1 (lane or R32F), computes both gates (UNORM8-quantised), the far weight (`c13`), the flag (thin vote at
`c10.z`, else the 7-tap search unless `c10.y` = 1, else the emissive vote at `c10.x`) and the neighbour's gates at its clamped
texel centre, composes the holds as before (the sentinel class term gone), takes the 7x7 in place where the camera term adds
strength and no box ran, and writes the next R32F depth history as RT2 (reason `resolve_mrt`, every current-depth format;
the StretchRect and copy draw are gone for camera-gate runs; a D24X8 snapshot refuses the run). The box programs (49-tap and
the S4 pair) gate on the previous frame's region hold only: the design's same-frame vote read in the gate cost 1.7 ms at
5120x1440 (box 2.5 against 1.0 ms, measured, `FOLD_TIMING` of the first build) and the in-place 7x7 covers those pixels with
the reference box. `camera_gate_available()` requires `NumSimultaneousRTs >= 3` (route row `reason=render_targets`). The
sentinel stabiliser is retired whole (launcher parser error, the variable never sent, one DLL ignored row; the separable
full-resolution twins, `c23`, `fp16_above`, `c11.x`, the class code deleted; `docs/architecture/fade-rt2-ownership.md`
section 5). Thin-region source: vote is the launcher default (with `X3M_TAA_THIN_REGION_SOURCE_DEFAULT=1`) whenever the thin
vote and thin region are on; both turns the search on; screen is refused under the camera gate (parser error; DLL row
`reason=screen_refused_camera_gate`); the DLL default when unset stays both. The state-block stream-offset defect filed above
is fixed: `SavedState` reads every stream's source and frequency (`GetStreamSource` / `GetStreamSourceFreq`) at capture and
sets them after `Apply`.

- Programs (`RESOLVE_BUDGET`, measured): `far_camera_hold` 2,479 -> 3,840 words, 616 -> 1,010 slots (design plan 950; within
  the 2,048 ceiling; the modern cap 32,768); `thin_box_hold` 70 -> 59 slots, `thin_box_rows_half` 158 -> 92,
  `thin_box_columns_half` 330 -> 107. Retired: `line_mask_camera` (251), `_depth` (241), `_depth_thin` (257),
  `thin_box_rows_hold` (155), `thin_box_columns_hold` (121). Every other embedded program's bytecode is unchanged (15
  resolve / mask / box records compared before and after the full regeneration; all manifests regenerated for the tool
  hashes, the bloom records too). The fixture's `X3M_FOLD_TESTS_OUT` twin (tests only) is 1,081 words / 267 slots. The
  compiler helper's source limit rose from 64 KB to 256 KB (the include-expanded resolve is about 70 KB).
- Build (MinGW i686, RelWithDebInfo): 0 warnings; `check_no_x87.py` 684 reachable functions, 0 violations.
- `run_temporal_pass.py` PASS (bottle X3). Main run 744 / 278 / 2 generations (counts unchanged); `temporal-pass.txt` differs
  only in `CHECK` lines and three `MOTE_STREAK` rows (the mote-streak case ran the stabiliser at 0.7; without it the dark-sky
  streak keeps the current sample: overlap 0.655 -> 1.000, the flickering sky's trail beyond 3 px 0.688 -> 0.617), 48 row
  types identical (`verification/results/taa-mask-fold/row_diff.py`, `changed_rows.py` and their outputs).
  Lattice 561 / 90 (610 / 107 before). Unchanged rows: every `THIN_REGION_HOLD`, `_MOTION_START`, `_PAN`, `_STALE`,
  `_BOX_DOMAIN`, `_PAN_STOP` row (the fold reproduces the removed tests draw's results in those scenes; oracle errors
  0.0083 / 0.0054 / 0.0024, age 0, tests 0), the kept `BOX_HALF_ORACLE` rows, 64 row types in all. Changed rows and why:
  `REGION_HOLD_IDENTITY` (2 rows, k = 0 and 0.5: the folded program against the removed camera program on the composition of
  its own tests, colour, age and RT2 depth 0 differ, 1,024 flagged, 618 with the camera term above the screen gate; the 11
  edge pixels that differed before the clamp of the neighbour's texel centre are gone), `REGION_HOLD_STATE` (the 2-RT caps
  override refused, RT2 and `COLORWRITEENABLE2` restored, the stream offsets restored from a non-hostile start, mask targets
  0 on a camera-gate run), `DEPTH_FOLD` 15 -> 10 rows (camera: lane, lane with the screen chain's fold program refused and
  G32R32F byte-identical to the R32F twin, all `resolve_mrt`, the one-ulp negative control differs) and `_FAULT` 2 -> 4 (the
  RT2 bind and the resolve draw), `BOX_HALF_CONTAINMENT` (6 scenes; the arm scenes now compare 59,541 / 60,395 pixels because
  the region hold opens the box at rest, output identical at both resolutions; half-only pixels held to the pixel's 7x7),
  `THIN_REGION_HOLD_BOX_OPEN` (the fold's gate 0.538 of the frame against 0.546 for the removed region-gated one),
  `THIN_REGION_HOLD_FADE_OWNER` (without S: oracle 0.0044, one opening per pixel, extra hold 0 frames),
  `THIN_REGION_CAMERA_FORWARD` (camera rms 1.525 -> 1.511 codes), `THIN_SOURCE` (camera gate: both flags U W V, vote W V,
  flags read from the age target; screen refused `80070057`), the identities 6 -> 4. New: `FOLD_FALLBACK` (the in-place 7x7
  against the oracle on its pixels: arm bars under a 0.5 px/frame pan 451 / 149 pixel-frames at full / half resolution,
  error 0.0007 / 0.0005 against 0.048 / 0.020 if the 3x3 clip were taken there; the 1 px gap / 7.5 px pitch lattice 32 / 8
  pixel-frames, error 0; bound 0.02). Retired: the `SENTINEL_STABILISER` (14), `THIN_REGION_HOLD_SENTINEL` (2),
  `BOX_HALF_SENTINEL` / `_EMITTER` and the sentinel containment scenes, `BOX_HALF_TIMING`, `THIN_SOURCE_TIMING`,
  `LINE_TIMING_SENTINEL` rows.
- Timing at 5120x1440 (`FOLD_TIMING`, the station content: 40 % sky, far panels, a 1024 x 512 lattice of 1 px gaps at 7.5 px,
  voted struts; three alternating rounds of the pre-fold build of the same include, `X3M_FOLD_BASELINE` with the stabiliser
  0.7 as flown, and the fold build; fixture CPU wall clock with event-query drains, measured,
  `verification/results/taa-mask-fold/fold_timing_pairs.py` and `fold_timing_summary.py`): baseline at rest mask 1.68, box
  1.46, resolve 2.59, TAA 5.79 ms (flown run312: 1.39 / 1.30 / 2.45 / 5.45). Fold, vote (the default): box 0.98, resolve 2.91
  (+0.32), TAA 3.93 ms (-1.86); under the pan resolve 3.01 (+0.47), TAA 4.01 (-1.59). Fold, both (the search on): resolve
  3.28 (+0.69), TAA 4.25 (-1.54); pan +0.80 / -1.24. The hinge (resolve growth above +1.0 ms) is not reached. The box at rest
  is its gate reading the previous age for 16 pixels per row texel (the region is small here).
- Route: `Output::stabiliser_mask` null on a camera-gate run (no `taa_mask` dump), `mask_targets=0`; the motion runner's
  thin-vote cases read the flag from the `taa_age` dump (region hold = L exactly where flagged that frame).
  `run_motion_output.py` PASS, 250 cases all exit 0 (bottle X3): `seam-taa-thin-hold-on` / `-half` / `-taps16-refused` 164 checks each (device references 5 / 7 / 6 + base, `mask_targets=0`, one `taa_age` dump per resolved frame instead of two dumps), the thin-vote cases 47-95 checks with the flag read from the age dump, `seam-thin-vote-far-on-source-{both,screen,vote}` 61 each (screen configured both, `reason=screen_refused_camera_gate`; vote flags no unvoted pixel).
- Host: the full suite 262 modules / 2,740 tests OK (`run_host_suite.py`); touched: `test_taa_image_defaults` 23, `test_taa_thin_region_source` 10, `test_taa_thin_vote` 16, `test_taa_box_resolution` 9, `test_taa_region_hold` 5, `test_motion_output_runner` 20, `test_bloom_programs` 5 (records regenerated for the compiler helper's hash), `test_temporal_runner_cleanup` 5, `test_shader_compiler_provenance` 1.
- Dry runs (`verification/results/taa-mask-fold/dry_runs.py`, `dry_runs_out.txt`): the `--taa` default sends
  `X3M_TAA_THIN_REGION_SOURCE=vote` with the default marker 1 and no stabiliser variable; `--taa-thin-region-source both`
  sends both (marker 0); `screen` is a parser error (the camera gate); `--taa-sentinel-stabiliser 0.7` is a parser error naming
  the removal; `--vanilla` sends none of them.
- Native Windows: documented D3D9 only (three render targets under the caps check, `GetStreamSource` on the non-pure device
  the proxy creates); cross-compiled, not run (`docs/architecture/platform-portability.md`, 2026-09-25 entry).

- Review items (2026-09-25, after the merge with main at f895a8c5):
  - Baseline and the flown default: the fixture baseline (5.79 ms TAA at rest) ran the sentinel stabiliser at 0.7, as flown in
    Run 82 A launch 1; main's 59ae2dfe had already made 0 the launcher default before this change landed. Against that default
    the stabiliser's box over the sky is not in the baseline, so the saving the flight will see is smaller than the fixture's
    -1.86 ms: about -0.6 to -0.9 ms (inferred: the mask draw's 1.4 flown minus the resolve's +0.3 and the box's region cost),
    settled by the next `--gpu-sync-timing` flight.
  - `THIN_REGION_CAMERA_FORWARD` camera rms 1.525 -> 1.511 codes: not the sentinel class (that case ran `camera97`, S = 0,
    before as well) and not the in-place 7x7 (the probe with it disabled, `verification/results/taa-mask-fold/probe_*`, gives
    the same 1.511, measured). The remaining differences are the gate programs themselves: the gates now evaluated in the
    resolve and quantised by `floor(x * 255 + 0.5)` instead of the UNORM write, and the box gate on the held region instead
    of `a > r` (inferred; 0.014 codes, 0.9 %, the shares and residuals of the row unchanged).
  - `BOX_HALF_CONTAINMENT pan_box_domain_k0.5_nonfinite output_identical` 1 -> 0: 96 output values of 128 frames differ between
    the full- and half-resolution runs, at most 1.2e-4 absolute and 4.9e-4 relative (2^-11, one FP16 step; measured, probe
    `PROBE_OUTPUT_DIFF`); disabling the FP32 in-place 7x7 leaves the same 96, so it is not that box against the FP16 block
    box. Inferred cause: the fold's gate opens the box on the whole held region, so more pixels take the 7x7 (FP16 targets) at
    full and the 8x8 block box at half, and at k = 0.5 their FP16-stored bounds clamp a few histories one FP16 step apart.
    Acceptable: containment holds (0 violations), the oracle row is within its bound, and the other five scenes are identical.
  - The vote default also applies under `--taa-thin-region-gate screen`: the launcher sends vote (marker 1) whenever the thin
    vote and thin region are on, and the screen-gate chain's thin-vote twin skips its search on `c10.y` the same way (dry run
    `--taa-thin-region-gate screen`: `vote` / `1`, measured; help text says so).
  - `SavedState` now makes up to 64 stream calls per run (16 streams x `GetStreamSource` / `GetStreamSourceFreq` at capture and
    `SetStreamSource` / `SetStreamSourceFreq` after `Apply`), untimed; the pass's capture / apply phases (`ticks_capture`,
    `ticks_apply`) include them.
  - Merge with main (f895a8c5): the stabiliser's launcher default 0 and its `X3M_TAA_SENTINEL_STABILISER_DEFAULT` marker
    (59ae2dfe) are dropped with the option; the marker is popped with the retired variables; `test_taa_sentinel_stabiliser_default`
    deleted. Only comments changed in shader and pass sources after the temporal fixture ran (the resolve-family manifests were
    regenerated for the source hashes, bytecode identical), so `run_temporal_pass.py` was not rerun.

Not flown. Next flight: `--gpu-sync-timing` at the run312 stand (`taa_mask_tests` absent, `taa` expected about 3.7 ms: the
fixture's 3.93 ms scaled by flown / fixture baseline 5.45 / 5.79, inferred), the fog-band plants and the Terran lattice with the default (vote) source.
