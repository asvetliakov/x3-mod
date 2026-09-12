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
