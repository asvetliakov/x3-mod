# Independent investigation: the upward lattice crawl

2026-09-19. Analysis only. No production change, build, Wine execution or game
launch. Source inspected at `48de5943`; other work in the checkout was left
alone. This note describes capture evidence, not the installed build.

## Finding

The recording contains **a travelling brightness/coverage alias on a stationary
lattice**: diagonal bright/dark packets give the impression of triangles moving
upward along the station. It is temporal moiré, not measured station motion.
The strongest measured travelling component has a roughly **10.5-pixel spatial
period** along the upper-right wing and a **7.27 Hz temporal frequency**. Its
apparent phase velocity is about **(+65, −41) screen pixels/second**. These are
velocities of a brightness pattern, not velocities of vertices or the camera.

In the measured lattice ROI, pre-TAA colour repeats **exactly every eight
frames**. The output also repeats almost exactly. The repeating Halton raster phases change which
thin features cover each pixel; the exponential TAA blend keeps favouring the
latest phase instead of producing a phase-independent average. Current-frame
history clipping worsens some flashes. This mechanism explains crawling at
complete standstill without invoking ship drift, display scaling or OLED motion
behaviour.

The recommended next change is **phase-balanced accumulation for verified
stationary content**, with phase-aware change detection and immediate fallback
on motion/change. It should replace further global blur/weight tuning as the
next diagnostic implementation. It is a demonstrated stationary remedy in
offline arithmetic, **not a demonstrated fix during flight**. Spatial moiré
can remain even after its animation stops.

## Inputs, coordinates and measurement limits

- `screenshots/lattice.mov`: H.264, 1280×768, 361 decoded frames, container duration
  6.436667 s. Variable timestamps: 8.333–33.333 ms increments, median 16.667 ms.
  The reported nominal `120/1` rate is not the measured presentation cadence.
- `/tmp/x3-bottleX3-run175`: 64 consecutive dumps, frames 7485–7548, 1280×768.
  `hdr` is scene colour supplied to the resolve; `taa` is its FP16 output;
  `present` is the tonemapped/sharpened write-back before later game overlays.
  The latter is not an OS/display screenshot. Readback placement is in
  [`MotionOutput::hdr_writeback`](../../src/proxy/motion_output.cpp) and the
  resolve readback path in that file.
- Capture provenance: bottle **X3**, `WineArch=arm64`, CrossOver Preview,
  `FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`. Environment lines come from the
  capture; WineArch was read from the bottle configuration. Nothing executed
  under Wine. Native Windows behaviour was not tested.
- Main ROI: half-open screen rectangle **[880,1100) × [40,175)**, 29,700 pixels.
  The dense lower lattice band is defined in ROI-local coordinates by
  `30 <= x < 190`, `128 < y + .635*x < 135` (1,120 pixel centres).
  The spectral strip uses `y = 112 − .635*x + v`, `30 <= x < 190`,
  `18 <= v < 22`, sampled at quarter-pixel intervals with bilinear interpolation.
  Its angle/extent were chosen from the image, not fitted to maximize a temporal
  effect. Interpolation does not create new source detail.
- Display-code luminance is `.2126 R + .7152 G + .0722 B` on 0–255 encoded RGB;
  these are code differences, not physical luminance or OLED measurements.
  Temporal RMS removes each pixel's time mean before pooling pixels/time.
- The movie and dumps are the same stationary scene, not synchronized frames.
  The debug capture's frame-end timestamps span **49.21 s** because readbacks
  are slow. Its wall-clock cadence must not be substituted for the movie's Hz.

## What the images measure

| Measurement | Result |
|---|---:|
| Movie, whole ROI temporal RMS | 3.85 codes |
| Movie, dense lattice band temporal RMS | 4.84 codes |
| Movie, dense band median / p90 temporal peak-to-peak | 24 / 31 codes |
| Captured present, whole ROI temporal RMS | 4.334 codes |
| Captured present, dense band temporal RMS | 6.492 codes |
| Captured present, dense band median / p90 peak-to-peak | 18.78 / 28.65 codes |
| Dense band pixels whose opaque depth coverage changes across phases | 83.21% |
| Complete horizontal occupied runs in that band that are one pixel wide | 1,450 / 2,335 = 62.10% |

The run-width count excludes runs cut by the measurement-band boundary. It is a
horizontal raster footprint, not a claim about world-space strut thickness or
whether every strut is modeled geometry versus alpha-tested material.

In the broader ROI, 4,209 pixels switch between valid depth and the sentinel.
Their present RMS is **10.554 codes**, median peak-to-peak **20.996 codes**;
the p99 peak-to-peak reaches **137.80 codes** at exceptional pixels. This mask
also includes silhouettes/intersections, so its larger RMS must not be called
the RMS of the dense lattice alone. Simply predicting raw luminance from each
pixel's covered/uncovered state explains **78.59%** of raw temporal variance on
this mask. Coverage changes are a major part of the signal; this measurement
does not exclude jitter-driven shading variation on covered samples.

Eight-phase class means explain **100%** of raw HDR luminance variance,
**99.999948%** of TAA luminance variance, and **99.999962%** of presented-code
luminance variance in the ROI. Mean absolute lag-eight differences are zero in
raw HDR, `3.54e-7` in TAA engine colour, and `3.91e-5` presented codes.
There is no accumulating upward displacement over successive cycles.

The movie independently matches the capture's eight spatial phase templates:
correlation **0.849**, or **72.14%** explained movie variance after fitting one
global amplitude, on the 6,831 pixels with capture phase standard deviation
above 1.5 codes. Of 360 decoded transitions, 293 advance one capture phase,
33 repeat it, 32 advance two and two advance three. This is consistent with
sampling a rendered sequence through a variable-rate recording; template
assignment and compression are not perfect.

For the rectified lattice strip, the capture's strongest travelling output
component is `(temporal frequency, spatial x-frequency) = (−1/8, +0.1125)`
cycles per rendered frame / screen-x pixel. That implies 8.89 horizontal
pixels, or **10.53 pixels along the diagonal**, per wave. In the movie, the
same spatial component peaks at **7.27 Hz**; its power in the upward-right
direction is **7.57×** the opposite direction. The inferred velocity is
`7.27/.1125 = 64.62 px/s` in x and `−.635 × 64.62 = −41.04 px/s` in y.
Other spatial/temporal components coexist: this is not one rigidly translating
texture. That is why a repeating brightness pattern can look triangular.

## Causal isolation

All 64 run175 frames report TAA resolved/history valid, no cut/camera cut,
zero apply/restore failures, 393 draws, 372 routed and jittered draws, and zero
unjittered depth writers. Camera translation and all nine logged rotation
entries are unchanged. Exposure is fixed at **1.24103 EV**, `k=2.36367`.
The active resolve has `weight=.900`, `current_filter=0`; optional line/far/
thin/adaptive interventions were not enabled in this capture. Sharpen is .75
stops and the configured mip bias is −.5.

Routed motion has small numeric residuals: median / p99 / maximum absolute
component **0.000610 / 0.001709 / 0.002747 px** in this ROI. These are much
smaller than the moving brightness wave and themselves phase-locked.

The CPU implementation in
[`taa_resolve_replay.py`](../../tools/analysis/taa_resolve_replay.py) was checked
before using its counterfactuals. Its camera convention 1 reproduces the first
five recurrent frames with maximum error rising only to 0.253 of that tool's
bounded-luma codes. Over 63 recurrent frames, comparison after modeled AgX has
mean absolute error **0.059 displayed codes**, p99 **0.402**. It is an adequate
model of this active plain resolve, not a universal validator of all variants.
No missing reactive-mask behavior was required for this run's active path.

Counterfactuals below use the same captured inputs, constant exposure and
modeled AgX without RCAS/bloom. Recurrent comparisons use the final 32 outputs
after seeding from the captured history. The .98 cases use the replay's
`line2x` mask; they are not claimed to reproduce a prior live experiment.

| Resolve experiment | Dense band RMS | Coverage-flip-mask RMS |
|---|---:|---:|
| Replayed captured resolve | 6.204 | 10.310 |
| Force sub-.01px correspondence residuals to zero | 6.204 | 10.310 |
| Suppress history clipping, keep .9 history | 5.877 | 6.543 |
| .98 history on line mask, existing clip | 1.296 | 8.000 |
| .98 history on line mask, suppress clip | 1.261 | 2.186 |

No depth/validity rejection occurs on the coverage-flip mask in the replay.
Clipping moves history on **3.93%** of those pixel-frame samples. It amplifies
the exceptional flashes substantially, while the typical dense-band ripple
survives removing it. Therefore a blanket claim that rejection, clamping or
motion-vector error alone causes this crawl is inconsistent with these tests.

For another control, repeating the same eight raw input phases through an
unclipped .9 EMA gives **6.540 codes RMS** on the flip mask, essentially the
same as the full no-clip replay's 6.543. Fractional history resampling contributes
little to this particular stationary result. An **equal-weight mean of all
eight raw weighted-colour phases** has zero temporal variation (numeric RMS
`7.1e-14` after AgX) and approximately the same mean brightness: 70.836 versus
70.899 codes for the .9 EMA. It uses no neighbouring pixels. It still contains
spatial alias in its fixed image; this control is not a high-resolution truth
image. A 1/64-current EMA still leaves about **0.996 codes RMS** on that mask.

Final postprocessing is a secondary amplifier here: captured TAA passed through
AgX alone gives 10.308 RMS on the flip mask versus presented 10.554. Thus
removing sharpening/bloom can change contrast but does not remove the causal
oscillation. The movie's existence also rules out a panel-only origin, although
the OLED may make the existing motion more conspicuous.

## Causes ranked

1. **Cyclic subpixel sampling plus a nonconvergent exponential resolve — very
   high confidence.** `MotionOutput::after_clear` advances eight Halton phases
   (`motion_output.cpp`, around line 3767); `resolve.hlsl` takes the point
   current sample and blends with history at lines 435–454. In a static scene,
   the correct unjittered correspondence still leaves a periodic current
   sequence. A fixed .9 EMA is a low-pass filter, not a completed sample
   average: its gain at 1/8 cycles/frame is **0.1364**. A pixel covered once in
   eight equal-contrast phases has steady-state normalized output spanning
   **0.08398–0.17558**, despite a correct mean of .125. Neighbouring lattice
   pixels have different coverage phases, so the remaining oscillation forms
   travelling packets. Both raw capture repetition and movie phase matching
   directly support this mechanism.
2. **Current-frame bounds destroy some valid multi-phase history — high
   confidence, secondary for the dense band.** `resolve.hlsl` lines 374–422
   clamp history to current 3×3 min/max and mean ±1.25σ. When a feature's
   coverage/shading changes with phase, a legitimate average can lie outside
   this instantaneous interval. The counterfactuals explain why a larger
   history weight alone leaves conspicuous exceptional flashes. Removing the
   clamp globally would introduce ghosting and is not the recommendation.
3. **Insufficient spatial sampling and jitter-driven shading detail — high
   confidence as a remaining limitation, incomplete material attribution.**
   One-/two-pixel raster features and the structured spatial beat persist in
   an equal-cycle image. Current filtering can soften them; a finite eight-
   sample average cannot promise correct reconstruction of arbitrarily fine
   lattice detail. This analysis does not attribute the residual covered-pixel
   signal among textures, specular terms and individual mesh faces. Negative
   mip bias may contribute to material alias, but is not established as the
   primary cause by these captures.

A wrong jitter sign/half-texel convention, drifting camera, recurring history
reset, disocclusion rejection, OLED scaling, exposure pumping and sharpen
feedback rank below these causes. The current-jitter cancellation in
`resolve.hlsl` lines 262–303 is consistent with the motion ABI; the replay and
forced-zero-motion test give an empirical check. Shader output already carries
the phenomenon before OS composition.

## Earlier experiments and why this is a different recommendation

The logs confirm actual interventions, not merely requested command lines:
run173 used `.97` history plus `line_filter=1,width=2`; run158 used
`line_filter=1,width=2`; run159 used `line_filter=2,width=2`. Run172 was plain
`.9`. Run172/173 are stationary; run157/158/159/148 move; run153 has stationary,
moving, stationary capture groups. Run173 still has a strong period-eight
component (99.958% phase-explained variance in the broad screen ROI).

Their camera poses differ from run175, so fixed-coordinate RMS comparisons
are **not a controlled A/B of filter effectiveness**. In particular the run175
dense-band coordinates miss the lattice in run172. These records cannot
contradict the user's report of no perceptual improvement. They do show that
the older changes retained the same periodic sampling and finite-memory
weighting; none establishes phase-independent convergence. Lower RMS or a
softer image is not by itself acceptance of the user's crawl complaint.

## Concrete change to make next

Implement one default-off **stationary, phase-balanced resolve prototype**:

1. In [`MotionOutput::evaluate_draw`](../../src/proxy/motion_output.cpp)
   (the matrix/history block around lines 5188–5206), derive a conservative
   static correspondence proof from the existing unjittered current/previous
   matrices and geometry revisions. Check the full transform, not just the
   object's origin displacement. Feed explicit validity to the temporal
   consumer; do not silently overload motion alpha, whose current ABI accepts
   exactly `1`. A small measured motion-vector tolerance alone is not proof
   that shading, visibility or an object is unchanged.
2. Add a distinct shader path alongside [`resolve.hlsl`](../../src/temporal/resolve.hlsl)
   that accumulates each jitter phase once with equal weight for eligible
   pixels. Publish a completed-cycle value rather than a rolling, recency-
   weighted partial estimate. On this proof, use the exact stationary history
   texel. Retain a multi-phase colour envelope or same-phase change test rather
   than clamping the completed mean to the latest single-phase 3×3 bounds.
   The raw current colour is expected to flicker: detecting change from its
   difference against the mean would reject precisely the pixels being fixed.
3. Own phase validity, accumulator and completed output in
   [`TemporalPass::run/allocate/release_history`](../../src/renderer/temporal_pass.cpp)
   and [`FrameInputs`](../../src/renderer/temporal_pass.h). Reset them on all
   existing cuts, failures, Reset and resource epochs. Exit promptly for motion,
   occlusion, material/lighting changes and reactive content. Freezing a whole
   screenshot whenever the ship speed reads zero is not acceptable. Keep
   exposure-dependent accumulation internally consistent across a cycle; the
   constant-k capture does not test changing exposure.

For a first bounded experiment, a completed-cycle texture while the existing
history pair builds the next mean costs **7.5 MiB** at 1280×768. That estimate
is only for a whole-frame stationary prototype. Mixed moving/static content
will need a properly separated publication path, likely a **15 MiB FP16 pair
plus validity state**, and the design must budget its reads/writes explicitly.
An exact sliding eight-phase ring is another option, but eight FP16 phase
images alone cost 60 MiB, before accumulation targets. Avoid allocating per
draw or introducing readback. A 64-byte matrix comparison on matched draws is
bounded; profile it and the additional fullscreen work before enabling it.
Use documented D3D9 resources/passes so the design is portable; this is source
compatibility, not native Windows validation.

This prototype directly tests the strongest diagnosis without global spatial
softening. First prove phase-independent stationarity offline, then check
appearance at normal playback speed. **Do not declare the flight problem
solved by the static branch.** For moving content, the next design question is
motion-compensated phase/sample accumulation with robust visibility, or a
larger actual scene sampling footprint. If residual spatial/flight alias is
unacceptable, a controlled 2×-per-axis internal scene sample/downsample path
with TAA retained is the concrete reference experiment: it requires coherent
HDR colour, depth, motion, viewport and resolve dimensions, not scaling the
final 1280×768 image. That is a larger renderer change and roughly quadruples
scene pixels; it is not proposed as an unmeasured default. Jitter reordering
alone would move the pattern's energy in time, not establish adequate filtering.

## Falsifiers and acceptance observations

- **Primary explanation falsified:** under a captured bit-identical repeated
  input cycle, a correctly published equal-phase output still has the same
  eight-phase travelling wave. Inspect stages: persistence only after a stable
  completed output would locate a downstream cause instead.
- **Visible-feature attribution falsified:** the user's identified crawling
  structure lies outside these measured lattice regions, or a synchronized
  recording shows a different non-phase-locked motion there. The current
  result is strong for the visible upper-right wing, not every possible
  station artefact.
- **Simple eight-phase mechanism falsified for another run:** the relevant
  raw scene no longer repeats with jitter, while its presented crawl remains
  at the old phase/frequency. Recheck animation, lighting, frame pacing and
  other render passes rather than carrying this result over automatically.
- **Static implementation rejected:** history freezes animated illumination,
  new occluders or moving objects; a camera move leaves trails; or cycle
  publication itself visibly steps. Preserving history must not defeat
  legitimate change detection.
- **Quality limitation exposed:** the cycle is temporally stable but its
  stationary triangular/moiré pattern is still objectionable, or it crawls
  again during slow drift. That would refute sufficiency of the stationary
  remedy, not the measured origin of the standstill oscillation; it calls
  for the moving/sampling work above.

For this input, an initial acceptance target is below **0.5 encoded-code RMS**
in the dense band after convergence, no period-eight peak, and no extra
spatial smoothing relative to the equal-phase reference. The user must still
accept normal-speed standstill **and** slow-drift footage; no such new flight
test has been performed here.

## Reproduction artifacts

All working data/scripts are under the permitted scratch directory:
`/private/tmp/claude-501/-Users-asvetl-x3-mod/e335fc62-471f-4c4a-89c9-cfcd2ddb149f/scratchpad/codex2/`.
`analyze.py` decodes movie frames and extracts bounded metadata;
`metrics.py` / `metrics.txt` measure phase repetition and template matching;
`spatial.py`, `lattice_strips.py`, `record_wave.py` measure the travelling wave;
`replay_test.py` / `replay.txt` and `zero_motion.py` contain the counterfactuals.
`rectified.png`, `phase_contact.png` and `movie-first.png` are visual witnesses.
No large raw dump or copyrighted shader output was copied into the repository.

The initial verification command was:
`PYTHONDONTWRITEBYTECODE=1 python3 tools/analysis/taa_resolve_replay.py /tmp/x3-bottleX3-run175 900 50 1090 160 validate`.
The larger counterfactual ROI is recorded explicitly in `replay_test.py`.
No CPU/GPU performance improvement or native Windows runtime claim follows
from this offline analysis.
