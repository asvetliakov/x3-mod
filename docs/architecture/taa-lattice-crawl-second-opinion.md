# Lattice crawl: independent second opinion, 2026-09-19

**Outcome.** Source coverage/radiance aliasing remains the leading diagnosis. I found no coordinate,
precision, weighting, rejection, mask, or interpolation correction that removes the real floor.
The exact offending source draw/subfeature is **not established**. Two important corrections to the evidence:
**run172 and run173 are static**, and the cited synthetic model does **not** run the same resolve.
Do not interpret their agreement/disagreement as proof of a hidden reprojection bug or a resolution limit.

Analysis only: no tracked source edits, build, Wine command, game launch, or installation.
Existing capture environment: bottle **X3**, `WineArch=arm64`, `FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`
(session environment plus bottle configuration). Native Windows behavior is untested.

## 1. Capture and measurement checks [measured]

- run172 frames **5164–5227**, run173 **6162–6225**: every full-frame `hdr`, `depth`, and `motion`
  array equals its counterpart eight frames earlier, exactly: **zero changed elements** over
  **56 comparisons per target per run**, not just a selected crop. These captures cannot measure
  drift registration or a slow jitter/drift beat. This does not disprove the user's observation in flight.
- Static check, run153 **4948–4979**, box `1040 440 1180 570`: same-phase raw colour/depth/motion
  likewise identical; resolved mean absolute component difference **2.86e-7**, presented **0.000114 codes**.
  Static run172 routed geometry correspondence residual is **0.00098 px median / 0.00164 p90**;
  glass **0.00104 / 0.00167** (box `720 180 1120 310`). No half-pixel or one-jitter offset appears.
- Drift work therefore uses run157 **5920–5951**, box `714 236 854 366`. Routed median velocity
  geometry **(+0.0191, −0.00961)** px/frame; glass **(+0.0232, −0.00852)**. These class medians
  cover different pixels and are **not** a same-point glass/strut mismatch measurement.
- Replays use the existing oracle, changing functions in memory only. Baseline free-running mean
  resolve error **0.072 bounded-luma codes**. Baseline displayed creep **12.376 codes**, dumped
  display **12.608**. Thus omitted bloom/display details do not explain the large residual here.
- Metric: average frames indexed **8–15** versus **24–31**, fit a subpixel Fourier translation per
  **28×28** tile with 10-pixel padding, lattice mask as in the oracle, **15 overlapping tiles**;
  residual RMS divided by final masked contrast. This is close to, but not the identical interval/search
  of the prior note. Ratios below are comparisons within this experiment, not replacements for its table.
  A1 and changed-weight replays are **31-frame transients**, not converged alternate flights.

## 2. Ranked explanations and ablations

These are overlapping mechanisms, so percentages cannot be added into an attribution pie chart.

| Rank / mechanism | Evidence and explained share | Falsifier / limit |
|---|---|---|
| **1. Source coverage and shaded-detail aliasing** | Raw cycle-mean creep **0.457** in radiance, **0.430** in bounded luma; binary valid-depth coverage alone **0.346**. This term exists before TAA, AgX and RCAS. Leading explanation for most remaining crawl. | A capture with demonstrably moving geometry, followed by a source-integrated replay that keeps this residual unchanged, would argue against it. Precise draw ownership and source footprint are still missing. |
| **2. RCAS amplification** | Same replay before RCAS **10.843 codes**, after **12.376**: **+14.1% amplitude**. Normalized **0.291 → 0.320**. Secondary amplification, not the floor's origin. | Disabling it should remove about 12% of this displayed residual, not cure crawl. Confirms the prior decision against treating sharpen exclusion as the remedy. |
| Perspective/rotation within metric tiles | Free first-order affine fit changes dumped display **0.326 → 0.322**; adding gain/offset gives **0.318**. Only about **1–2.5% RMS** in this fit. | Not an exact projective registration; severe multilayer parallax could remain. It is not evidence for a 0.25 floor being merely a rigid-translation measurement error. |
| Catmull–Rom history transport | Replacing it with normalized Lanczos-3: base **0.3204 → 0.3188**; A1 **0.2607 → 0.2593**. Lanczos-4 + A1 **0.2586**. **Under 1%** normalized improvement. | Does not test a history representation that avoids repeated resampling entirely. A more expensive kernel alone is unsupported as a fix here. |
| Changing filter mask | A1 on line2x **0.2607**; diagnostic full-crop A1 **0.2576**; growing the mask two more pixels **0.2578**. **About 1.2%** improvement. | Global filtering was an offline ablation only; it is not proposed. Mask holes do not explain the missing model gain. |
| Luminance weighting / HDR clip domain | Force `k=0`: base **0.3204 → 0.3210**, A1 **0.2607 → 0.2597**. Removing clip without filtering **0.3258**. Full-crop A1 without clip **0.2455**. | A radiometrically different scene may behave differently. No evidence for an HDR-strut weighting bug in this one. |
| Depth rejection / age / changing exposure | **Zero rejected pixels** throughout the replay crop; history valid throughout; ordinary w=0.9 route, no age option. Run157 k **2.15921–2.15922**, EV **1.11050–1.11051**. | An unrepresented moving capture with resets/rejections would reopen this; these frames do not. |
| Half-texel, wrong jitter, FP16 motion | Code and static measurements agree; **motion is A32B32G32R32F**, not FP16. See §3. | A motion versus image correspondence test in a genuinely moving long capture remains necessary for dynamic-only matrix/history errors. |

Source detail is not a uniform two-colour comb. In six nearby 16×16 patch measurements,
valid-depth samples include approximately **0.15–0.61** radiance at p10–p90, with occasional values
**1.26–3.23**; glass stays around **0.13–0.14**. In five selected 12×12 panel patches, a two-colour
coverage predictor explains **20%, 57%, 56%, 44%, 46%** of the motion-predicted raw bounded-luma
cycle-difference energy. The remainder mixes sampled shading, supports, layers and registration error;
**it is not proof of animated lighting**. Excluding pixels ever below 0.12 or above 0.65, with a
one-pixel exclusion border, still gives raw normalized creep **0.342**. Bright support outliers alone
are not the explanation either. Patch selection is exploratory, not a representative attribution census.

## 3. Coordinate and sampling audit

- `src/proxy/motion_output.cpp::apply_jitter` adds `(2*jx/W, −2*jy/H) * clip.w` to clip rows;
  application rows/history remain unjittered. The motion constant upload at approximately line 5210
  explicitly zeros previous-row jitter. Run157/172/173 log **zero unjittered depth writers**.
- `src/temporal/rigid_motion_ps.hlsl` adds the **+0.5 texel** texture-center correction to projected
  previous UV. `src/temporal/resolve.hlsl` adds **current** jitter to that UV. The camera path removes
  half a texel and current jitter before inversion and restores both afterward. Closest valid depth
  in the **3×3** supplies motion; its pixel offset is subtracted so velocity, not absolute position,
  is borrowed. `src/renderer/temporal_pass.cpp` uses the matching −0.5-pixel fullscreen quad.
- Some comments in `rigid_motion_ps.hlsl` say the resolve adds no jitter, and `resolve.h` says current
  jitter is camera-only; these are stale comments. Executable expressions agree with `resolve.hlsl`
  and the newer README. **Do not change the arithmetic to match those comments.**
- Blended glass can own RT1 while RT2 retains sentinel or underlying opaque depth. Closest-depth
  dilation does not by itself prove colour/motion ownership. This remains a legitimate H1 investigation;
  the above static checks do not rule out a dynamic-only ownership bug.

**A cycle mean is not an exact pixel integral.** For integer alias vector g, the 8-phase jitter's
coherent coefficient `abs(mean(exp(2*pi*i*g·j)))` is **0.0488** for (1,0), **0.1250** for (0,1),
**0.3102** for (1,1), **0.2398** for (2,1). Recentring its mean jitter (−0.05469, 0) changes phase,
not these magnitudes. Such surviving folded source harmonics beat against the surface at `g·velocity`.
At run157 median motion this implies periods around **43–52** frames for x aliases and **104–117**
for y aliases: beyond a 32-frame frequency measurement. Run172/173 have no physical drift beat to fit.
This is a derivation, **not a measured attribution** of the user's visible frequency. Doubling Halton
length is not a guarantee: the (0,1) coefficient is still **0.1216** at 16, versus **0.0313** at 32.

## 4. Why the synthetic comparison does not establish a hidden resolve defect

The cited scratch `crawl/sim.py` uses **160 frames, ideal Fourier history shifts, no clip, uniform
line colour, a fixed velocity, and filtering everywhere**. Real alternate replays start from the
existing history for only 31 frames, and real source radiance/geometry are more complicated.
Replacing only ideal transport by production Catmull–Rom changes the model's A1 result
**0.1167 → 0.1204** (w=0.9), or **0.0598 → 0.0734** (w=0.97). Not enough to explain the real gap.

Catmull–Rom does have phase dispersion: at 0.022 px/frame, a 3.8-pixel sinusoid's history transport
is **0.617×** ideal velocity (Lanczos-3 **1.037×**); at pitch 3 it is **0.428×**. The real-kernel
ablation above nevertheless falsifies it as the main explanation in run157.

A simple source counterexample: keep the same ideal model but narrow the radiance-bearing feature.
With A1/w=0.9, widths **0.7 / 0.5 / 0.3 / 0.2 px** give residuals
**0.1167 / 0.1607 / 0.1972 / 0.2923**. Thus a 0.25 floor is possible without any motion defect.
**0.2 px is not a measured width in these captures**; geometric width measured in another pose is
not sufficient evidence for the bandwidth of the current shaded source.

## 5. Recommended next change, with falsification

**Prototype source footprint integration for the identified lattice geometry, retaining TAA.**
In `src/proxy/motion_output.cpp` admit an exact, verified line/ribbon draw and its geometry identity;
add a dedicated coverage producer beside `src/renderer/material_motion.cpp` rather than globally
changing the existing material programs. Cache topology/edge data once per geometry revision.
The producer must cover the pixel footprint outside a subpixel ribbon (expanded raster support),
and integrate its shaded contribution/coverage before writing current colour. Multiplying alpha
at the original binary-covered samples alone cannot fill missed coverage and is insufficient.
Preserve original lighting evaluation; keep depth/motion provenance explicit, never average sentinel
values, and handle intersections/opaque occlusion. D3D9 shaders and ordinary buffers/targets can
host such a specialized path; geometry admission, blending and native runtime behavior need validation.
This is a **bounded research prototype**, not an implementation-ready universal shader patch.

Prediction from a source-only model: integrate the projected pixel footprint of the stripe before
sampling, then existing A1/TAA: residual **0.1167 → 0.0539** for 0.7-px features, and
**0.2923 → 0.0640** for 0.2-px features; retained contrast **0.1233 → 0.1122** and
**0.0410 → 0.0367** respectively. The model integrates each stripe family and approximates intersection
coverage by their union; it is not a replay of game geometry. **No numerical game improvement is promised.**
Falsify this direction if source-integrated real-draw playback fails to cut normalized creep substantially
(e.g. below 0.15) while retaining at least 85% contrast on the same moving view. Do not implement another
mask blur, wider history kernel, or weight-only flight based on these results.

Before judging any prototype, add a read-only capture sanity gate to the analysis workflow (natural home:
`tools/analysis/taa_resolve_replay.py`): report exact eight-frame input repetition, motion distribution,
and whether the fitted shift hits the search boundary. Reject a **drift claim**, not the capture itself,
when all inputs repeat. A new longer capture must actually move during dumped frames; current 64-frame
data does not settle the remaining dynamic ownership hypothesis. Preserve mip bias −0.5, no MSAA,
no whole-frame SSAA, no global blur, and original hull shading.

## Reproduction / artifacts

All scripts and numeric JSON are local under
`/private/tmp/claude-501/-Users-asvetl-x3-mod/e335fc62-471f-4c4a-89c9-cfcd2ddb149f/scratchpad/codex/`.
Run from repository root with `python3 <scratch>/SCRIPT` (set `OPENBLAS_NUM_THREADS=1` for replays):
`investigate.py`, `registration.py`, `affine.py`, `source_split.py`, `kernel.py`, `source_model.py`,
`coverage_model.py`; `replay_variants.py 157`, `replay_kernels.py 157`, `replay_masks.py 157` produce
`replay157.json`, `kernels157.json`, `masks157.json`. `replay_save.py 157` saves arrays for `interior.py`.
The full-frame equality check used read-only numpy memmaps, comparing each f with f−8 in all three targets.
Only this new untracked note and permitted scratch artifacts were written. Owning prior notes:
[prior crawl analysis](taa-lattice-crawl.md), [flicker](taa-flicker-suppression.md),
[source AA constraints](source-antialiasing.md), [distant lines](taa-distant-line-fade.md).
