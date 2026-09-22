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
