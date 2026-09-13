# Exposure for X3's outdoor space scenes

2026-09-14. The next source candidate selects **Auto capped at +1.5 EV**
following run 27 visual acceptance; the installed build still defaults to fixed EV 0.
The scene model is black cosmos, small stars, large colored
nebulae, planets/suns, ships and transient effects. Indoor/hangar adaptation
is not a design premise. The current implementation is described in
[HDR scene path](hdr-scene-path.md); the actual baseline is
[run 24](../verification/run24-exposure-baseline.md) and
[run 25](../verification/run25-bloom-comparison.md).
Published practice in other space games is collected separately in
[the primary-source comparison](../research/space-game-exposure.md).

## Current policy after run 27

The user preferred Auto to fixed EV 0 and then accepted the milder +1.5 EV
comparison. Adopt **Auto with a +1.5 EV maximum** as the next production and
launcher default. Retain `--hdr-exposure fixed`, explicit manual EV, and the
Ctrl+Shift+F9 Auto/fixed-0 comparison. Explicit manual EV remains authoritative;
explicit EV limits remain supported. Standalone component defaults are unchanged.
No metering equation, response time, highlight guard or tone curve changes here.

Run 27 targets the ceiling in 329/331 active Auto reports, with the other two at
startup zero. This is appearance acceptance of an almost steady boost in this
sample, not proof that background metering represents illumination or provides
useful adaptation. The research and light-aware metering limitations below still
apply. A future light/context model should preserve the accepted appearance
without introducing sky-driven pumping. Fixed exposure remains a useful control.

Implementation changes only production initialization and launcher defaults.
Absent direct environment policy selects Auto; explicit unknown/truncated policy
still falls back to fixed, and explicit manual EV overrides either mode. Meter
resources and toggle support already exist; Auto has its existing per-frame
meter cost while fixed has none. No new resource, shader or pass is introduced.
The next combined candidate will install this policy with stronger authored glow;
run 27 already exercised the same explicit Auto/+1.5 configuration.

## Earlier fixed-exposure evaluation (2026-09-13)

At that checkpoint, the orchestrator selected **fixed EV 0 as the default policy** for this legacy-content
stage, with Auto retained as an explicit option and same-run comparison.
The [default and comparison controls](comparison-hotkeys.md) are implemented
and independently reviewed, and installed in candidate `75dbbed`. The user
explicitly accepts no exposure adjustment as a complete option. Here that
means multiplier 1 and no automatic adaptation; AgX and independently enabled
bloom remain. It preserves relative authored brightness as the camera turns and has
no response to stars, sky coverage or explosions. This is a legitimate exposure
policy for continuously outdoor space, not a temporary failure mode;
image-dependent adaptation is not required merely because the render target is FP16. EV 0 still applies AgX and
is not a claim of exact vanilla brightness. Whether it makes the game too dark
is a visual acceptance question, not answered by a luminance statistic.

The separate primary-source comparison strengthens this recommendation without
claiming other games use fixed exposure. CIG's 2017 account specifically tries
to prevent looking into space from over-brightening the view near bright
objects; its 2019 report identifies the ambiguity of mapping dark and white
surfaces towards the same grey and proposes incident-light metering. Elite's
2018 update documents dynamic exposure alongside separate Night Vision for
intentionally dark exteriors. Those are constraints on X3's policy, not
published algorithms to copy. Until X3 has a reliable illumination/context
anchor, camera-invariant EV 0 meets those constraints with fewer unsupported
assumptions than normalizing the captured background median.

If Auto must remain useful in this stage, prefer testing a **near-neutral,
no-positive-lift response to broad bright content** over normalizing dim sky
content to grey. The bounded R2 experiment below provides a concrete example.
It has an explicit cost: an intentionally large bright planet can dim the
whole view by half a stop. Fixed exposure avoids that effect. These captures
do not demonstrate a need for the R2 response: it selects EV 0 in all nine.
The R1 dim-key experiment produces small variable targets on these captures,
but its anchor is arbitrary and its tile-maximum guard retains star sensitivity;
it is useful comparison evidence, not the preferred production design.

The planned same-run Auto/fixed-0 toggle allows the user to judge the existing
+2 baseline against EV 0 without changing scene, camera, material coverage or
bloom simultaneously. R1/R2 remain offline experiments and are not selected for
production. Visual acceptance of the fixed default remains pending; a noticeable
adaptation effect is not a quality requirement. The independent review approved
the capture boundary, numerical method and conclusions after fixing the test
module to propagate repository import failures and cover two reduction levels.
All four focused study tests pass; no Wine or production build was required.

## Why the installed meter mostly requests the ceiling

Its centre-weighted median selects tiles whose geometric-mean decoded
luminance exceeds 1/512, then maps that median towards 0.18 at full strength
when brightening. A dim nebula is eligible scene content, not necessarily
underexposed illumination. In the nine selected frames the eligible medians
are 0.00429–0.01139: the key asks for approximately +3.98 to +5.39 EV.
The installed +2 ceiling therefore becomes the normal target.

The highlight guard does not counter that tendency. It permits the p99 tile
maximum to reach `0.9 * 16.2917424 = 14.6625682` in decoded luminance.
Captured p99 maxima are 0.347–0.927, so the guard allows about +3.98 to
+5.40 EV. This is a reference to AgX's far white endpoint, not to an authored
white surface. Multiplying that endpoint by 0.9 also does not mean a 90%
display-code ceiling. At +2, bright backgrounds can rise substantially while
neither the key nor this guard changes target. Reducing EV maximum alone
would create another mostly fixed ceiling.

Below 1% lit tiles the key abruptly returns to EV 0. Crossing that count gate
changes the installed target between 0 and +2 with almost no underlying
photometric change. The 0.25-EV deadband does not hide a 2-EV jump; the
0.4/1.2-s adaptation only spreads it over time. Run 24 records this behavior
and a minimum applied EV of +0.755 during the sparse interval.

## Capture boundary and bounded numerical method

The study reads only the **first post-TAA frame in each contiguous F8 burst**:
four A frames and five B frames. It does not remeter all raw files or infer
scene identity from burst order. Snapshots remain immutable under
`/tmp/x3-bottleX3-run24/` and `/tmp/x3-bottleX3-run25/`.

The input contract is proved in source:

- `MotionOutput::resolve`, `src/proxy/motion_output.cpp`, saves
  `out.color_surface` as `taa_1_<frame>.rgba16f`, then retains the same resolve
  texture as `hdr_resolved_` for `HdrPass::write_back`.
- `src/temporal/resolve.hlsl` reverses its luminance weighting before storing
  RGB. History remains in engine-space RGB. Its exposure-derived `k` is a
  reversible weighting parameter, **not stored pre-exposure**. Do not divide
  these files by the logged exposure multiplier of 4.
- `HdrPass::copy_draw` meters its supplied texture before tonemapping.
  `hdr_meter_level0_ps.hlsl` applies the configured gamma-2.2 decode exactly
  once, followed by Rec.709 luminance and the [1e-4,64] meter clamp.
- The scene handoff precedes HUD, text and 2D overlays; see
  [the recovered ordering](hdr-scene-path.md#stage-2-implementation-2026-09-12).
  Presented BGRA8 and diagnostic pre-resolve color files are not meter inputs.
  Late lens flare is also outside this captured scene; its bright appearance
  cannot be assumed to affect this meter (see [sun resources](../reverse-engineering/sun-material-identity.md)).

All selected log records confirm successful RGBA16F readbacks, gamma2.2,
AgX/no look and resolved HDR TAA. The helper checks their sizes and finite
samples and records selected input hashes. Recreated tile-image means agree
with the logged `avg_log_l` within 0.0059 EV. These are intentionally not
claimed identical: the capture is frame n and the reported meter is lagged;
GPU floating-point reduction also differs from the double-precision helper.

The paired depth planes have valid nonnegative depth on 7.46–32.55% of pixels.
Their sentinel region includes sky **and uncovered scene writers**. Neither
valid depth nor a sentinel is a semantic ship/background classifier. The
helper records separate distributions for diagnosis, but neither candidate
assumes a depth mask is a reliable lighting meter.

## Measured alternatives

R1 uses `clamp(0.25*log2(0.01/lit_median), -0.5, +0.5)` multiplied by smooth
confidence from zero at 1% lit share to one at 10%. A separate, downward-only
quarter-strength p99-maximum guard references 0.9 and limits darkening to
half a stop. It removes the hard minimum-lit fallback jump but retains the
per-tile background classification. The 0.01 anchor is an artistic hypothesis,
not a recovered physical light value.

R2 uses `clamp(0.25*log2(0.18/p95_tile_geomean), -0.5, 0)`. It never lifts
black space or dim nebulae. Its percentile is of tile **geometric means**,
not maxima, so isolated bright pixels do not represent the whole tile.
Neither candidate has a temporal replay claim: these are fresh current-image
targets before deadband/adaptation.

| Run / first frame | Lit median | p99 tile max | R1 EV | R2/fixed EV | AgX display luma p90 at EV 0 / +2 |
| --- | ---: | ---: | ---: | ---: | ---: |
| A / 9823 | 0.00976 | 0.764 | +0.009 | 0 | 0.260 / 0.488 |
| A / 10398 | 0.01139 | 0.783 | -0.047 | 0 | 0.316 / 0.555 |
| A / 28288 | 0.00657 | 0.504 | +0.151 | 0 | 0.076 / 0.204 |
| A / 28464 | 0.00703 | 0.627 | +0.127 | 0 | 0.128 / 0.298 |
| B / 2180 | 0.00751 | 0.805 | +0.103 | 0 | 0.113 / 0.273 |
| B / 3241 | 0.00787 | 0.690 | +0.086 | 0 | 0.132 / 0.306 |
| B / 3574 | 0.00429 | 0.347 | +0.305 | 0 | 0.055 / 0.158 |
| B / 7596 | 0.00606 | 0.927 | -0.011 | 0 | 0.068 / 0.189 |
| B / 10168 | 0.00660 | 0.694 | +0.150 | 0 | 0.213 / 0.427 |

Installed policy selects +2 in all nine. Display-luma values above are
computed from the captured post-TAA pixels through the existing AgX reference,
without bloom or later overlays. They quantify a large tonal difference; they
do not rate its desirability. Rerendering TAA with another exposure-derived
weight could also change temporal pixels, so these are transform comparisons,
not predicted pixel-exact captures from another run.

Streaming the existing logged scalar statistics, without additional image
reads, gives R1 fresh targets -0.353…+0.397 across A's 485 active reports and
-0.324…+0.423 across B's 241; neither hits its half-stop bounds. The A sparse
interval becomes near zero when the lit share approaches 1%, rather than a
2-stop switch. Sampled reports cannot reconstruct every-frame adaptation.
R2 cannot be replayed from these old scalar logs: p95 tile means were not
logged. A 0.25-EV production deadband would hold many R1 changes; this study
does not accidentally count fresh targets as visible adaptation.

## Counterexamples and temporal response budget

Synthetic inputs below are decoded luminance at 1280×768, not physical units.
The normal reduction produces 80×48 tiles, each covering 16×16 pixels.

| Input | Installed target | R1 target | R2 / fixed-0 target |
| --- | ---: | ---: | ---: |
| Black at meter floor | 0 | 0 | 0 / 0 |
| Full dim nebula at 0.01 | +2 | 0 | 0 / 0 |
| One luminance-64 star per tile on black (0.390625% pixels) | -2.126 | -0.5 | 0 / 0 |
| White planet at 1 covering half the screen | -0.618 | -0.5 | -0.5 / 0 |
| 32×32 luminance-64 flash on black (0.10417% pixels) | 0 | 0 | 0 / 0 |
| 34 lit tiles at 0.01 (0.8854%) | 0 | 0 | 0 / 0 |
| 43 lit tiles at 0.01 (1.1198%) | +2 | 0 | 0 / 0 |

The scattered-star example explains why p99 tile maxima is not a 1%-pixel
highlight guard. Only one high pixel is needed to set a tile maximum. It can
darken the entire screen even when all tile geometric means remain classified
as black. At the current scene's lower star intensities the effect is smaller,
but a future HDR emitter would make the weakness more consequential. R1 retains
that mechanism despite its smaller bound. A sufficiently large explosion can
also cross R2's broad-content percentile: this is deliberately a bounded
response, not semantic rejection of every transient.

For a future R2 trial, a 2-s darkening and 4-s recovery time constant would
limit a fresh -0.5-EV target sustained for 100 ms to -0.0244 EV from neutral,
and a 200-ms interval to -0.0476 EV. A sustained bright planet still converges
to -0.5 EV. These analytic first-order responses assume the fresh target is
accepted by the deadband; they do not prove live event duration or perceptual
quality. Fixed EV is exactly invariant for every example.

## Native lighting/sector anchor: proved inputs and missing semantics

Targeted disassembly already established the shader-upload connection:
`0x004c0150` copies selected directional and point-light RGB fields, and
[the upstream input study](../reverse-engineering/material-color-inputs.md)
traces light channels to signed fixed-point values divided by 256. There is
no separate physical intensity scale on that path. Active point-light counts
and attenuation payloads are captured in
[iteration 0.4](../reverse-engineering/iteration04-lights.md); that is the
per-draw selected light subset, not a camera-independent sector registry.

A sector/illumination anchor could preserve camera-direction invariance and
permit deliberate different exposure in different sectors. Its values may be
authored artistic EV offsets; physical lux are not necessary. But no validated
sector identity/lifetime reader, complete directional/ambient set, or mapping
from those data to intended exposure is established by these readbacks. The
native sun lens-flare source is a late visual effect, not an illumination
meter. Texture colors, TSuns resource fields and selected shader RGB must not
be relabeled luminance or solar irradiance. The existing targeted findings
resolve enough for this comparison; a production sector-anchor implementation
needs its own targeted engine study and transition/lifetime validation.

The scene remains decoded gamma-space lighting, with incomplete linear
material coverage. Native compatible source is required, and the above
policies can use ordinary D3D9 resources and host arithmetic on both targets;
none requires backend-private data. No native Windows execution is implied.

## Performance, artifacts and verification

Fixed/manual exposure already disables meter work. An R1 implementation can
reuse current tiles/weights/scratch and adds only constant-sized host math;
the existing per-frame lit sort and maximum selection still cost work. R2 can
reuse the mean channel and perform one percentile selection on persistent
scratch, avoiding the lit sort and maximum statistic if the old diagnostics
are retired. Neither needs per-draw state, allocation, engine queries or an
extra depth sampler/pass. Keeping the existing GPU chain initially avoids an
unnecessary shader/ABI change. Future reduction optimization is separate.

Existing diagnostic spans include roughly millisecond-scale meter submission
and readback work, with higher maxima; they are CPU/QPC spans, not GPU-only
cost or frame-rate impact. A claimed small budget must include readback and
the existing chain, not merely the new scalar policy formula.

The reproducible helper is
[`evaluate_space_exposure.py`](../../tools/analysis/evaluate_space_exposure.py).
Run it with a Python environment containing NumPy and the two snapshots;
`--output /tmp/x3-space-exposure-study.json` retains compact selected-frame
statistics/input hashes, targets, depth distributions and synthetic cases.
The file is local, not a new evidence-manifest chain. Four focused tests in
[`test_space_exposure_study.py`](../../verification/analysis/test_space_exposure_study.py)
pass: vectorized reduction versus the existing scalar reference including odd
edge taps; vectorized AgX versus scalar colors/EVs; counterexamples; and
continuity of R1 confidence at the old gate. JSON serialization rejects
nonfinite output. No Wine command, game launch, production build or install
was performed.
