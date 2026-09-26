# Iteration 8: the corrected history convention in gameplay

Second TAA gameplay run, with the jitter-convention fix of commit `162b2f7`
(history read at the previous position **plus the current jitter**; see
[review-17](../archive/review-17.md)). The run answers the acceptance test
[iteration 7](../archive/iteration-07.md#implications) set: the tremble measurement must
turn from `tracks_jitter` into `stable` and the gradient-energy ratio must move
away from 0.56.

It does. The user's remaining report — *still trembling, especially at object
edges, maybe a little less; thin distant objects and thin emissive lines (the
station's yellow guide lines) shimmer even when standing still* — is measured
here for the first time as a **per-pixel flicker classification**, which says
where the residual motion lives. No gameplay was launched by the agent and no
`src/` change was made for this report.

## Provenance

| | run A (TAA) | run B (control) |
| --- | --- | --- |
| log | `session-20260912-074543-296.log`, 262,691,737 B | `session-20260912-075643-2624.log`, 124,245,193 B |
| sha256 | `3f862cdf98997e1c52f67403ff4264d7a447d5d972b019d46f07f30b508b5565` | `ab2975e077509602315eb01a9cd534b5c134228bf6fa38bf668e501ffee968d4` |
| DLL | `200aefff27e2e36d528c5ce02e1ea5720155ed525b66840d4c75053045af1da9` (`162b2f7`) | same |
| mode | `taa=1 taa_debug=1 jitter=1 jitter_samples=8 temporal_consumer=1` | `taa=0 taa_debug=0 jitter=0 temporal_consumer=0` |
| captured frames | 20 (five bursts of 4), 80 readbacks | 12 (three bursts of 4), 24 readbacks |

1280×768 windowed, present interval 1. Run A witnesses: `motion_output_device
... enabled=1 reason=ok ... history_available=1 history_capacity=4096 depth=1
depth_reason=ok jitter=1 jitter_samples=8 taa=1 taa_reason=ok taa_debug=1`, one
`motion_output_taa device=1 initialize=00000000 references=1`, one
`motion_output_target device=1 width=1280 height=768 create=00000000 depth=1
depth_create=00000000`, 56 `motion_output_variant`, `ownership_factory
mode=wrapped` (three factories, no fallback), `object_trace active=1
status=active`, `object_lifetime active=1 status=active_without_baseline`
(unchanged since iteration 5). The logs, the readbacks and the shader dumps
stay untracked.

## Reproduction

```sh
python3 tools/analysis/analyze_iteration08_taa.py <run A log> \
    --captures <run A captures> \
    --run-b-log <run B log> --run-b-label run_b_taa_off \
    --output verification/results/iteration-08-taa-summary.json \
    --text verification/results/iteration-08-taa-summary.txt

python3 -m unittest verification/analysis/test_iteration08_taa.py   # 17 cases
```

Results: [`iteration-08-taa-summary.json`](../../verification/results/iteration-08-taa-summary.json)
and `.txt`. The log evidence, the telemetry aggregation, the two shift
estimators and the blur measurement are **imported unchanged** from
`analyze_iteration07_taa.py`, so the iteration-7 and iteration-8 numbers are
produced by the same code; only the flicker classification, the guide-line
probe and the two-session timing hook are new.

## 1. Run health

109 `motion_output_frame` records (the 20 captured frames plus every 60th).

| | frames |
| --- | ---: |
| `taa_attempted=1 taa_resolved=1 taa_result=00000000` | **102** |
| `taa_skip=2` (the selector never reached the copy: menu, map, load) | 7 |
| `taa_history=1` | 102 |
| `taa_skip=3` (no jitter) or `6` (open application query) | 0 |
| `motion_output_taa_failed`, `apply_failures`, `restore_failures` | 0 |
| nonzero HRESULT anywhere in the session | 0 |
| `motion_output_reset` / `device_reset`, `motion_output_release` / `device_destroy` | 0 (again no clean shutdown) |

All 20 captured frames resolved with valid history. Route counters, whole
session: 55,545 draws, 40,214 routed, 40,181 matched (**99.92%**), gates
`gate1=0 gate2=6,196 gate3=931 gate4=8,204 gate5=0 gate6=33`. Captured frames:
14,741 draws, 11,744 routed, 11,738 matched (**99.95%**), `gate2=400 gate3=257
gate4=2,340 gate5=0 gate6=6`; the 14,341 per-draw `motion_route` records
reproduce the DLL's own counters on all 20 frames (`counter_consistency_pass`).
All 80 readbacks are 1280×768 with `result=00000000`; no failure, unsupported,
reset or shutdown record anywhere.

### Cut detector and jitter per captured frame

`bound_px=48.000`, `bound_missing=0.250`; **no frame reports `cut=1`**.

| burst | frames | cut median px | jitter indices (jx, jy) px |
| --- | --- | --- | --- |
| 0 | 2554–2557 | 0.0223, 0.0729, 0.0729, 0.0727 | 0–3 |
| **1** | **2678–2681** | **0.0004, 0.0015, 0.0012, 0.0012** | 4 (0.125, 0.278), 5 (−0.125, −0.278), 6 (0.375, 0.056), 7 (−0.438, 0.389) |
| 2 | 3030–3033 | 3.51, 9.49, 9.41, 9.46 | 4–7 |
| 3 | 3362–3365 | 0.205, 0.579, 0.583, 0.594 | 0–3 |
| **4** | **5275–5278** | **0.0000 ×4** | 1 (−0.250, 0.167), 2 (0.250, −0.389), 3 (−0.375, −0.056), 4 (0.125, 0.278) |

Bursts 1 and 4 are the ones the route itself certifies stationary in pixels
(median origin displacement ≤ 0.01 px in every frame); both carry the whole
image analysis below. Burst 4 is the strictest stationary burst captured so far
— an exactly zero median in all four frames — and burst 1 is the station view
that contains the yellow guide lines.

## 2. Tremble: fixed

The iteration-7 measurement, unchanged code, on both stationary bursts. Two
independent estimators on the luminance of consecutive frames, restricted to the
pixels the route marked valid (motion alpha 1) in **both** frames and eroded by
2 px (152,338–155,677 px in burst 1, 222,712–224,334 px in burst 4).

**Control.** The pre-resolve colour of a stationary scene must move by exactly
the logged jitter step; the largest error over the six pairs is **0.021 px**
(burst 1) and **0.016 px** (burst 4), so the jitter sign convention and the
estimator are both sound in this run as well.

**Measurement.** `d_n` is the resolved image's displacement, `w = 0.9`. Under
the old convention `d_n → j_n` (the full jitter step between frames); under the
corrected one `d_n` is a `(1−w)`-weighted running mean of a zero-mean sequence
and the inter-frame shift is bounded by `stable_resolve_bound_px = (1−w)·2·max|j|`
(0.0875 px in burst 1, 0.0778 px in burst 4).

| pair | jitter step px | resolved shift px | ‖−jitter-tracking‖ | ‖−stable‖ | resolved / colour |
| --- | --- | --- | ---: | ---: | --- |
| 2678→2679 | (−0.2500, −0.5556) | (−0.0461, −0.1179) | 0.4828 | **0.1266** | (0.187, 0.208) |
| 2679→2680 | (+0.5000, +0.3333) | (+0.1502, +0.0709) | 0.4373 | **0.1661** | (0.291, 0.204) |
| 2680→2681 | (−0.8125, +0.3333) | (−0.1919, +0.0627) | 0.6771 | **0.2018** | (0.237, 0.191) |
| 5275→5276 | (+0.5000, −0.5556) | (+0.1518, −0.1179) | 0.5593 | **0.1922** | (0.305, 0.212) |
| 5276→5277 | (−0.6250, +0.3333) | (−0.1777, +0.0468) | 0.5312 | **0.1837** | (0.280, 0.144) |
| 5277→5278 | (+0.5000, +0.3333) | (+0.1076, +0.0637) | 0.4761 | **0.1250** | (0.217, 0.182) |

**Every one of the six pairs returns `stable`** (iteration 7: three of three
returned `tracks_jitter`), and the pairing-free per-frame discriminator — the
shift between a frame's own pre-resolve colour and its resolved image — returns
`stable` for all eight frames (residual to the stable prediction 0.040–0.146 px
against 0.196–0.454 px to the jitter-tracking prediction; iteration 7 had it the
other way round on all four frames). The resolved image now moves **0.14–0.31×**
as far as the jittered render, where iteration 7 measured **0.84–1.06×**.

The residual is **1.4–2.3× the ideal bound**, not zero, and the reason is in
§4: the mask contains routed silhouette pixels whose history the resolve
rejects, and those follow the jitter in full, pulling a global-translation
estimator up. The cleanest independent view is phase correlation on burst 4,
whose 256² tile is **99.8% routed** (burst 1's is 49%): there the resolved
image moves **0.05–0.11 px** against the colour's 0.64–0.78 px, i.e. at or just
above the 0.078 px bound. On surfaces that actually accumulate, the image is
stable; the leftover global motion is an edge effect.

## 3. Blur

Same mask, resolved FP16 against the pre-resolve 8-bit colour of the same frame.

| burst | frame | gradient-energy ratio | high-frequency fraction ratio | iteration 7 |
| --- | --- | ---: | ---: | --- |
| 1 | 2678–2681 | 0.6637, 0.6715, 0.6645, **0.6439** | 0.8385, 0.8621, 0.8244, 0.8620 | 0.5646–0.5805 / 0.5679–0.6174 |
| 4 | 5275–5278 | 0.7558, 0.7529, 0.7529, **0.7549** | 0.5128, 0.5202, 0.5043, 0.5272 | — |

The gradient-energy ratio moved from **0.56–0.58 to 0.64–0.76**: about a third
of the lost detail came back, exactly as predicted when the history stopped
being resampled at a new sub-pixel offset every frame. It is still not 1: with
`w = 0.9` and a bilinear history tap, a static scene should read one texel
exactly (`f = 0`) and lose nothing, so the remaining 25–35% is the part of the
image whose history *is* still resampled or rejected — the same edge and
thin-feature population §4 isolates. The high-frequency fraction is a single
256² tile and is therefore tile-dependent (0.82–0.86 on burst 1's half-routed
tile, 0.50–0.53 on burst 4's fully routed one); the gradient ratio, which is
computed over the whole mask, is the comparable figure.

## 4. Flicker classification: where the residual shimmer is

New in this iteration. On each stationary burst every pixel is classified from
the four frames' RT2 depth (`depth_1_<frame>.r32f`) and motion alpha, and the
**temporal variance of the resolved image** is compared with the temporal
variance of the **pre-resolve colour** at the same pixel. A ratio near 1 means
the resolve changes nothing there; far below 1 means it stabilized the pixel.
`rms` below is the temporal standard deviation in 8-bit levels
(`sqrt(mean variance)·255`), and *flicker energy share* is that class's share of
the whole screen's resolved temporal variance — the "where does the shimmer come
from" number.

Classes: **sentinel** — RT2 current depth negative in all four frames, which the
resolve returns current-only by construction (background, effects, particles,
unrouted programs); **routed interior** — depth valid and motion alpha 1 in all
four frames and the depth spread across the burst ≤ 1e-4, the resolve's own
absolute rejection tolerance; **routed edge** — everything else (validity, alpha
or depth changes across the burst: silhouettes, thin geometry, anything whose
coverage flips with the jitter). **Thin feature** is an *overlay*, not a fourth
class: a morphological white/black top-hat with a 3×3 square (which erases every
feature wider than 2 px) gated on the 3×3 morphological gradient, on the raw
colour, unioned over the burst.

### Burst 1 (frames 2678–2681, the station view)

| class | pixels | share | raw rms | resolved rms | aggregate ratio | ratio p25/p50/p75 | flicker energy share |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| sentinel | 789,117 | 80.3% | 1.56 | 1.56 | **0.999** | 0.999/0.999/1.000 | 0.199 |
| routed interior | 129,704 | 13.2% | 13.68 | 1.19 | **0.008** | 0.005/0.006/0.009 | 0.019 |
| routed edge | 64,219 | 6.5% | 11.84 | 10.87 | **0.843** | 0.758/0.999/1.003 | **0.782** |
| thin feature (overlay) | 71,205 | 7.2% | 21.27 | 10.52 | 0.245 | 0.005/0.008/0.261 | **0.812** |
| ↳ thin ∧ sentinel | 12,473 | 1.3% | 11.62 | 11.62 | 0.999 | | 0.174 |
| ↳ thin ∧ routed interior | 47,091 | 4.8% | 22.13 | 1.91 | 0.007 | | 0.018 |
| ↳ thin ∧ routed edge | 11,641 | 1.2% | 25.30 | 22.75 | 0.808 | | 0.621 |

### Burst 4 (frames 5275–5278)

| class | pixels | share | raw rms | resolved rms | aggregate ratio | ratio p25/p50/p75 | flicker energy share |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| sentinel | 737,007 | 75.0% | 0.62 | 0.62 | **0.999** | 0.998/0.999/1.000 | 0.080 |
| routed interior | 188,386 | 19.2% | 9.20 | 0.75 | **0.007** | 0.004/0.005/0.006 | 0.030 |
| routed edge | 57,647 | 5.9% | 7.92 | 7.36 | **0.863** | 0.957/0.999/1.003 | **0.890** |
| thin feature (overlay) | 49,520 | 5.0% | 17.34 | 6.94 | 0.160 | 0.004/0.005/0.012 | **0.679** |
| ↳ thin ∧ sentinel | 7,093 | 0.7% | 6.20 | 6.20 | 0.999 | | 0.078 |
| ↳ thin ∧ routed interior | 37,587 | 3.8% | 17.95 | 1.51 | 0.007 | | 0.024 |
| ↳ thin ∧ routed edge | 4,840 | 0.5% | 22.80 | 20.45 | 0.805 | | 0.577 |

Four findings, both bursts agreeing:

1. **The resolve works where it is allowed to work.** Routed interior pixels —
   13–19% of the screen — have their temporal variance cut by a factor of
   **133–151** (ratio 0.0075 and 0.0066; 60,678 → 10,690 and 65,612 → 5,588 pixels
   above a visible 2-level temporal sd). This is the supersampling the feature
   exists for, and it is now real.
2. **Routed edges are where the trembling is.** 6% of the screen carries
   **78–89% of the resolved image's whole temporal variance**, and the median
   edge pixel has ratio **0.999** — the resolve does nothing for it at all.
   That is the direct signature of the hard depth rejection: when the jitter
   flips a silhouette's coverage, the history tap's depth differs from the
   expected depth by more than 1e-4, every tap is dropped, and the pixel falls
   back to the raw jittered sample. It is exactly the limitation
   [review-17](../archive/review-17.md) recorded as a known design consequence, and it is
   what the user sees "especially at object edges".
3. **Thin features are the visible part of it.** 5–7% of the screen, detected
   purely from the raw colour with no knowledge of the classes, carries
   **68–81%** of the residual variance. Split by class it decomposes into a
   thin ∧ edge population (0.5–1.2% of the screen, ratio 0.81, **58–62%** of
   all residual flicker) and a thin ∧ sentinel population (0.7–1.3%, ratio
   0.999, 8–17%). Thin geometry is the worst case for a coverage-flip rejection
   because its coverage flips *everywhere along it* at every jitter phase.
4. **Sentinel pixels are untouched but mostly quiet.** 75–80% of the screen is
   sentinel (no routed opaque draw wrote RT2 there) and its ratio is 0.999 by
   construction, but its raw amplitude is small (rms 0.6–1.6 levels) because
   the background is *not* jittered, so it contributes only 8–20% of the
   residual energy — concentrated in the thin ∧ sentinel pixels (rms 6.2–11.6
   levels), which is the "thin distant objects shimmer" the user reports for
   everything the route does not cover.

**What `routed_edge` does and does not mean.** The class is defined on the
*readbacks*: RT2 validity, motion alpha or the RT2 depth at a fixed pixel
changing across the burst by more than 1e-4. That is deliberately the same
quantity the shader's per-tap test compares, but it is not only silhouettes: on
a steeply slanted surface the ±0.5 px jitter moves the raster sample along the
slope, so the device depth at a fixed pixel changes between phases with no
silhouette anywhere. Both populations are in the 6%, both are rejected by the
same absolute tolerance, and both need the same fix — which is why a *relative*
tolerance alone would not be enough and the test has to become a disocclusion
test rather than an equality test.

## 5. The station's yellow guide lines

Bright yellow pixels of the pre-resolve colour (`min(R,G) > 60`,
`min(R,G) − B > 40`, `R` and `G` close), unioned over the burst.

| | burst 1 (2678–2681) | burst 4 (5275–5278) |
| --- | ---: | ---: |
| per frame | 825, 823, 827, 840 | 264, 272, 275, 254 |
| union / yellow in **all** frames | 1,643 / **217** | 403 / **151** |
| coverage flip fraction | **0.87** | **0.63** |
| centroid (x, y) | (678, 211) — upper middle | (838, 394) |
| class: routed interior / routed edge / sentinel | 1,420 / 189 / 34 | 174 / 164 / 65 |
| with RT2 depth in all four frames | 1,530 (93%) | 309 (77%) |
| raw rms / resolved rms (levels) | 40.60 / **11.36** | 31.83 / **13.06** |
| aggregate ratio | 0.078 | 0.168 |
| ↳ routed interior subset | ratio 0.007, resolved rms 3.35 | ratio 0.010, resolved rms 3.99 |
| ↳ routed edge subset | ratio **0.483**, resolved rms **32.22** | ratio **0.545**, resolved rms **20.06** |
| ↳ sentinel subset | raw variance exactly 0 (static background yellow) | idem |

The guide lines exist in the capture, they sit where the user says (burst 1's
centroid is in the upper middle of the frame), and they **are routed**: 93% of
their pixels carry RT2 depth in every frame. Three things are true at once:

* their raw temporal amplitude is enormous (40.6 levels rms) because a
  sub-pixel-wide emissive line moves in and out of a pixel with every ±0.5 px
  jitter phase — **87% of the line's pixels are not even yellow in all four
  frames**;
* the resolve does reduce it, by 140× in energy (12× in amplitude) on the
  interior pixels, to 3.35 levels rms;
* but the 11% of the line classified as edge keeps **32 levels rms** of
  residual flicker and dominates what the eye sees, and even the filtered
  interior pixels stay above the 2-level visibility threshold (1,012 of 1,420).

So the shimmer of the yellow lines is *not* a sentinel problem. It is the same
coverage-flip rejection as §4, concentrated on the thinnest, highest-contrast
feature in the scene.

## 6. Timing

### Run A alone

CPU-side wall clock only, never GPU time; per-window figures only (the build
records count/failures/min/max/total and six buckets per one-second window, so
no session-wide median exists). `frame_normal` excludes captured frames, which
have their own `frame_capture` metric — the `--taa-debug` readbacks are
therefore *not* inside these numbers.

| metric | run A | iteration 7 |
| --- | --- | --- |
| `frame_normal` count / mean / max | 5,272 / 71.7 ms / 106.7 s | 8,105 / 49.8 ms / 97.4 s |
| per-window mean, scene windows (median) | 60.2 ms (n = 228) | 38.5 ms (n = 217) |
| per-window minimum, scene windows (median) | 58.8 ms | 36.1 ms |
| `present_normal` scene per-window mean | 61.7 µs | 48.6 µs |
| `draw_backend` count / mean | 2,468,457 / 2.51 µs | 2,226,284 / 2.46 µs |
| `lock_wait` mean | 0.21 µs | 0.21 µs |
| boundary `stretch_rect` bracket (20 samples) | median 22.6 ms (15.6–28.1) | 21.5 ms |

The boundary bracket still contains both `--taa-debug` readbacks (31.5 MB per
captured frame) and remains an upper bound only; telemetry still has no
`StretchRect` metric. Read on its own this table looks like a further
regression against iteration 7; the controlled comparison below shows it is
not one.

### Controlled timing: run A against run B

Run B is the same save and scene on the same build, launched **without** `--taa`
and `--taa-debug`: `taa=0 taa_debug=0 jitter=0 temporal_consumer=0`, with the
route still active (15,554 of 22,285 draws routed, 15,486 matched, `gate5=0`,
0 jittered draws, `taa_skip=1` on all 55 frame records). What differs is
therefore the jitter, the resolve, the history retention and the debug
readbacks — **not** the route.

Compared naively, run A looks twice as slow:

| per-window scene statistic | run A | run B | ratio |
| --- | ---: | ---: | ---: |
| `frame_normal` mean, median over windows | 60.2 ms (n = 228) | 30.9 ms (n = 82) | **1.95** |
| `frame_normal` minimum, median over windows | 58.8 ms | 29.3 ms | 2.01 |
| `frame_normal` mean, p10 over windows | 28.4 ms | 26.7 ms | 1.06 |

The p10 row is the clue: **both sessions are bimodal.** Binned by per-window
mean, run A's 228 scene windows are 39 at 20–30 ms, 34 at 30–40, 19 at 40–50,
16 at 50–60, 84 at 60–70 and 36 above; run B's 82 are 28 at 10–30, 25 at 30–40, 1 at
40–50 and 28 at 50–70. The session visits a light and a heavy part of the scene, and a
pooled median of one-second windows mostly reports **how long each run stayed
where**. The analyzer therefore splits the scene windows at 45 ms
(`--window-mode-split-us`) and compares within regime:

| regime | run A | run B | ratio |
| --- | --- | --- | ---: |
| fast (`< 45 ms`) | 78 windows, median **29.93 ms** | 53 windows, median **29.90 ms** | **1.0009** |
| slow (`≥ 45 ms`) | 150 windows, median **63.87 ms** | 29 windows, median **64.04 ms** | **0.9973** |

**Within each regime the two runs are the same frame time to within 0.3%.** The
1.95× pooled ratio is occupancy: run A spent 66% of its scene windows in the
heavy regime, run B 35%. This matches the user's report that the frame rate
feels the same in both runs, and it **retracts the iteration-7 reading** of a
"2.6× per-frame regression, unexplained by any measurement" — that comparison
(iteration 6 against iteration 7) was between different sessions of different
scenes and was measuring the same occupancy effect.

What *is* attributable to the feature, from the same pair:

| metric | run A | run B | per frame |
| --- | ---: | ---: | --- |
| `draw_backend` mean (whole session) | 2.51 µs | 2.43 µs | ≈ +40 µs at ~470 draws/frame (the per-draw jitter upload) |
| `present_normal` scene per-window mean | 61.7 µs | 31.9 µs | +30 µs |
| `lock_wait` mean | 0.21 µs | 0.21 µs | unchanged |

So the measurable cost of jitter + resolve + history retention is of the order
of **70 µs per frame, ~0.2% of a 30 ms frame**, and the frame time itself is
unchanged within the resolution of this measurement (±0.3%, i.e. ≲ 0.2 ms).
That is consistent with the 0.67–1.0 ms synthetic bench being an upper bound and
inconsistent with any per-frame regression of milliseconds.

Two caveats. The runs are the same save and scene but not the same path through
it, so the regime split is the comparison, not the pooled numbers. And both
sessions present at interval 1: if the frames were display-paced the two runs
could snap to the same quantum and hide a sub-quantum cost — the observed modes
(29.9 and 63.9 ms) are not small multiples of a 60 Hz refresh, so they are
probably not vsync-quantized, but this measurement cannot exclude a cost below
a few hundred microseconds.

**The route's own cost is still unmeasured.** Both runs have the live route,
the RT2 depth target and the object observers active, so nothing here bounds
what the route itself costs against the plain proxy. That needs a third run of
the same save with telemetry only (no `--motion-output`, no observers), which is
also the run that would finally give the colour bit-identity evidence
(criterion 6) that remains unevidenced.

## Anomalies

1. **The residual tremble is an edge effect, not a convention error.** Six of
   six pairs and eight of eight frames return `stable`, but the global estimate
   is 1.4–2.3× the ideal bound because the mask contains the rejected-history
   edge population of §4. The fully routed phase-correlation tile of burst 4
   reads 0.05–0.11 px, at the bound.
2. **`routed_edge` is 6% of the screen and 78–89% of the residual flicker**,
   with a median ratio of 0.999 — no temporal filtering whatsoever.
3. **Sentinel coverage is 75–80% of the screen.** Anything not routed (the
   background, effects, particles, the HUD) receives no temporal treatment at
   all. Its amplitude is low today only because it is not jittered; any future
   camera-driven motion would make it the dominant artefact.
4. **The session did not shut down** (third run in a row): no
   `motion_output_release`, no `device_destroy`, `reset_count=0`. The gameplay
   witnesses for teardown and for Reset with the temporal pass allocated remain
   unobtained.
5. **`object_lifetime` still ran `active_without_baseline`** and gate 5 still
   never fired (0 in 55,545 draws).
6. **The yellow guide lines flip coverage on 87% of their pixels between
   consecutive jitter phases**, which is the strongest single piece of evidence
   that sub-pixel-wide emissive geometry needs explicit handling rather than a
   coverage-based accept/reject.
7. **Iteration 7's frame-rate finding is withdrawn.** Its "factor of 2.6,
   per frame, unexplained" compared two sessions of different scenes; run B
   reproduces the same 30 ms / 64 ms bimodal structure with the resolve off, and
   within regime the two runs differ by 0.3%. The frame-rate defect the user
   reported in iteration 7 has no support in the telemetry of a controlled pair.

## Implications

Another agent is implementing the edge and thin-feature handling concurrently;
`src/` was not touched for this report. The measurements above support, in
order of how much residual flicker each would remove:

1. **Accept history at coverage flips and bound it with the neighborhood clip
   instead of rejecting it on depth** (removes up to the 78–89% carried by
   `routed_edge`). The hard `|depth − expected| ≤ 1e-4` per-tap test is what
   turns a silhouette or a thin line into a current-only pixel: its median
   ratio of 0.999 is the test firing on every frame. Depth should reject only
   a genuine *disocclusion* (history clearly in front of the expected surface,
   an occluder that moved away); history behind it or on the sentinel is the
   background the silhouette swept over and is exactly the sample needed to
   accumulate a coverage fraction. A variance/min-max clip of the current 3×3
   neighborhood is the right bound for it, because at an edge the neighborhood
   contains both sides.
2. **Dilate the correspondence to the closest depth of the 4-neighbourhood.**
   A pixel on the far side of a silhouette currently reprojects with the
   background's motion while showing part of the foreground; taking the
   velocity of the nearest valid neighbour is the standard fix and is what lets
   the edge pixels of item 1 find a history worth accepting.
3. **Handle thin features explicitly** (the 5–7% of the screen carrying 68–81%
   of the residual variance). Two things matter: a history filter whose
   response does not erase a one-pixel line at fractional offsets (a
   Catmull-Rom tap set rather than repeated bilinear), and a clip that does not
   collapse a thin line to its neighbourhood mean — a variance clip with
   γ ≈ 1.25 keeps it, a min/max box alone dims it. Snapping a zero fractional
   offset to the texel grid also matters: a static scene must read exactly one
   texel, which is what keeps the gradient-energy ratio of §3 from drifting.
4. **Give sentinel pixels a camera reprojection** (the remaining 8–20%, and far
   more once the camera moves). A negative RT2 depth means no routed opaque
   draw wrote the pixel, not that it has no motion; reprojecting it at the far
   plane through a valid `clip_to_previous` is the correct treatment, and it
   must stay opt-in until the route actually uploads a camera matrix for the
   background rather than the identity.
5. **Do not weaken the tremble or blur acceptance tests.** They are now the
   regression guard: `verdict.stable` on every pair of a stationary burst, the
   pairing-free per-frame test likewise, resolved/colour ratio ≤ 0.31, and the
   gradient-energy ratio at or above 0.64. Any change to the resolve should
   move the gradient ratio towards 1 and the `routed_edge` aggregate ratio
   towards the interior's 0.008 without moving these.
6. **Measure the route, not just the resolve.** §6 leaves the route's own cost
   unmeasured; and telemetry still has no `StretchRect` metric, so the boundary
   cost cannot be observed in gameplay at all.
