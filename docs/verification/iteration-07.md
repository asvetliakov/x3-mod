# Iteration 7: first gameplay run with the temporal resolve

The user completed the TAA gameplay run described in
[motion-output](motion-output.md#gameplay-diagnostic-run) with the route, the
resolve, the jitter, the ownership wrapper and both object observers active, and
reported three defects from the screen: **stationary objects tremble**, the image
is **blurry**, and the **frame rate drops, worst when few objects are visible**.
No gameplay was launched by the agent and no `src/` change was made for this
report.

DLL SHA256 `54ee429feac0e2a314ab39446f5291b33ce107696334997421a5e3f6256dfc70`
(commit `b9a1094`). Capture log SHA256
`dfcb6610677209e663e3933a324b978a3f25f3689be1a9a539cead499c7f3689`, 90,351,004
bytes (`session-20260912-070240-1764.log`, snapshot copy
`/tmp/x3-iteration07-snapshot.log`). The log, the 48 readbacks (12 frames ×
`motion_*.rgba32f` 15,728,640 B, `depth_*.r32f` 3,932,160 B,
`color_*.bgra8` 3,932,160 B, `taa_*.rgba16f` 7,864,320 B) and the shader dumps
stay untracked.

## Configuration

```sh
python3 tools/manage.py launch --direct --ownership --object-trace \
    --object-lifetime --motion-output --taa --taa-debug --telemetry \
    --capture-start 999999 --capture-frames 4
```

1280×768 windowed, present interval 1, `NVIDIA GeForce 8800 GTX` as reported by
the CrossOver Preview backend. Witnesses in the log: `motion_output_mode ...
taa=1 taa_debug=1 jitter=1 jitter_samples=8 cut_median_px=48.000
cut_missing=0.250`; `motion_output_device device=1 enabled=1 reason=ok ...
history_available=1 history_capacity=4096 depth=1 depth_reason=ok jitter=1
jitter_samples=8 taa=1 taa_reason=ok taa_format=00000000 taa_debug=1`; one lazy
`motion_output_taa device=1 initialize=00000000 references=1`; one
`motion_output_target device=1 width=1280 height=768 create=00000000 depth=1
depth_create=00000000`; 56 `motion_output_variant` records;
`ownership_factory mode=wrapped` (three factories, no fallback);
`object_trace active=1 status=active`; `object_lifetime active=1
status=active_without_baseline baseline_complete=0` (unchanged from
iteration 6).

## What the user did

About 414 s of instrumented wall clock, 8,121 presents, **three** F8 presses
producing three bursts of four consecutive frames (12 captured frames, 48
readbacks, every one `result=00000000`). Before the first press the user held
the ship still; the route's own cut detector confirms it (below). The session
ends in a map/menu screen with no clean shutdown, as in iteration 6.

## Reproduction

```sh
python3 tools/analysis/analyze_iteration07_taa.py <log> \
    --captures <captures dir> \
    --baseline-log <iteration-6 log> --baseline-label iteration06 \
    --output verification/results/iteration-07-taa-summary.json \
    --text verification/results/iteration-07-taa-summary.txt

python3 tools/analysis/analyze_motion_readback.py <log> \
    --readback-dir <captures dir> --label iteration07 \
    --jitter-from-log --no-draw-details
python3 tools/analysis/analyze_motion_readback.py <log> \
    --readback-dir <captures dir> --label iteration07-bilinear \
    --jitter-from-log --depth-sampling bilinear --no-draw-details

python3 -m unittest verification/analysis/test_iteration07_taa.py   # 17 cases
python3 -m unittest verification/analysis/test_motion_readback.py   # 23 cases
```

Results: [`iteration-07-taa-summary.json`](../../verification/results/iteration-07-taa-summary.json)
and `.txt`,
[`motion-readback-iteration07-summary.json`](../../verification/results/motion-readback-iteration07-summary.json)
and the `-bilinear` variant.

`--jitter-from-log` is **new in this iteration** and is required for any
jittered capture; see [analyzer change](#analyzer-change-jitter-from-log).

## Resolve evidence

148 `motion_output_frame` records (12 captured frames plus every 60th).

| | frames |
| --- | ---: |
| `taa_attempted=1 taa_resolved=1 taa_result=00000000` | **118** |
| `taa_skip=2` (the selector never reached the copy: menu, map, load) | 30 |
| `taa_history=1` | 117 (every resolved frame but the first) |
| `taa_skip=3` (no jitter) or `6` (open application query) | **0** |
| `motion_output_taa_failed`, `apply_failures`, `restore_failures` | **0** |
| nonzero HRESULT anywhere in the session | **0** |
| `motion_output_reset` / `device_reset` (`reset_count=0`) | 0 |
| `motion_output_release` / `device_destroy` | 0 (no clean shutdown) |

`taa_references` is 12 while the pass is allocated and 0 before the first latch.
**All 12 captured frames resolved with valid history**, and the selector reached
the boundary in every frame that had a scene: the iteration-6 anomaly where 47%
of captured frames routed nothing is gone (`selector_state=9`, the corrected
structural selector, on all 148 records).

Route counters, whole session: 41,916 draws seen, 29,709 routed, 29,644 matched
(**99.78%**), gate histogram `gate1=0 gate2=5,771 gate3=694 gate4=5,742
gate5=0 gate6=65`. Captured frames only: 5,098 draws, 4,034 routed, 4,030
matched (**99.90%**), `gate2=244 gate3=52 gate4=768 gate5=0 gate6=4`. The 4,854
per-draw `motion_route` records decompose as gate 0: 4,030, gate 3: 52,
gate 4: 768, gate 6: 4, gate 5: **0** — the counter cross-check reproduces the
DLL's own counters on all 12 frames.

### Cut detector and jitter per captured frame

`bound_px=48.000`, `bound_missing=0.250`; **no frame reports `cut=1`**.

| burst | frames | draws | routed / matched | cut median px | missing | jitter index → (jx, jy) px |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| 1 (held still) | 2017–2020 | 710 | 570 / 570 | **0.0000** ×4 | 0 | 3 (−0.375, −0.056), 4 (0.125, 0.278), 5 (−0.125, −0.278), 6 (0.375, 0.056) |
| 2 | 2259–2262 | 312 | 249 / 249 | 1.34, 7.76, 7.91, 8.05 | 0 | 5, 6, 7 (−0.438, 0.389), 0 (0.000, −0.167) |
| 3 | 3170–3173 | 252–254 | 189/187, 191/189, 189/189, 189/189 | 2.82, 15.77, 15.92, 16.13 | 0.0106, 0.0105, 0, 0 | 4, 5, 6, 7 |

Burst 1 is the first frame set in the project that the route itself certifies as
stationary **in pixels** — the restatement iteration 6 anomaly 1 asked for. All
570 matched draws have a zero median origin displacement in all four frames.

## Readback evidence

`analyze_motion_readback.py --jitter-from-log`: 12 frames, 48 readbacks,
891,141 valid pixels, 0 nonfinite, 0 out-of-ABI, 0 zero-alpha.

| check | result |
| --- | --- |
| `readback_integrity` | **pass**, 12/12 |
| `counter_consistency` | **pass**, 12/12 |
| `history_pairing` | **pass**, 3,026 predictable draws, **0 disagreements** |
| `displacement` | **pass**, 0 suspicious at 64 px, largest 42.82 px (frame 3171) |
| `row_consistency` | **pass**, 9 frames, 336,230 sampled pixels, **0 unexplained**, max error **0.0081 px** |
| `temporal_coverage` | **pass**, 9 pairs (7 with the criterion), worst covered fraction **0.9853** |
| `depth_image_integrity` | **pass**, 12/12, sentinel fraction 0.917–0.939, 0 nonfinite, 0 out of range, 0 valid motion pixels without depth |
| `taa_image` | **pass**, 12/12 finite, differing fraction 0.0102–0.0249 |
| `static_consistency` | **unavailable** — still no bit-identical row set; the cut detector's pixel criterion replaces it |
| `depth` (previous-depth cross-check) | **fail** at the default tolerance — see anomaly 2 |

Per-burst displacement (with the frame's own raster jitter removed):

| burst | valid px / frame | median px | p99 | max |
| --- | ---: | ---: | ---: | ---: |
| 2017–2020 | 75,804–76,048 | **0.0010–0.0011** | 0.002 | 0.99–3.50 |
| 2259–2262 | 60,120–60,972 | 0.067–0.205 | 2.18–12.84 | 2.36–14.50 |
| 3170–3173 | 81,715–90,675 | 0.156–0.214 | 6.79–39.51 | 7.38–42.82 |

## Observed defects

### 1. Stationary objects tremble: the resolved image tracks the jitter

The measurement is in `analyze_iteration07_taa.py` and lives entirely in the
readbacks of burst 1, the burst the log certifies stationary. Two independent
estimators of one global translation are run on the luminance of consecutive
frames, restricted to the pixels the route marked valid (alpha 1) in
`motion_*.rgba32f` in **both** frames, eroded by 2 px (64,115–64,215 pixels).
That mask matters: only routed scene geometry is jittered, and including the
unjittered background halves the measured shift.

Estimators: `lucas_kanade` (iterated gradient least squares on a 2× binomially
blurred image, 40,000 highest-gradient masked pixels) and `phase_correlation`
(Gaussian-low-passed normalized cross-power spectrum of one 256² Hann-windowed
tile, parabolic peak fit; its tile cannot be masked — 48% masked coverage —
so it reads low and serves as a sign/magnitude cross-check). Both return `d`
with `I2(x) = I1(x − d)`, +X right, +Y down.

**Control.** The pre-resolve colour readback of a stationary scene must move by
exactly the logged jitter step. It does:

| pair | logged step (px) | measured colour shift (px) | error |
| --- | --- | --- | ---: |
| 2017→2018 | (+0.5000, +0.3333) | (+0.5086, +0.3441) | 0.0138 px |
| 2018→2019 | (−0.2500, −0.5556) | (−0.2563, −0.5696) | 0.0154 px |
| 2019→2020 | (+0.5000, +0.3333) | (+0.4922, +0.3446) | 0.0137 px |

That also confirms the jitter sign convention `jx_ndc = 2·jx_px/W`,
`jy_ndc = −2·jy_px/H` in live gameplay, independently of the synthetic coverage
oracle.

**Prediction.** Write the resolved image's displacement as `d_n` and the history
weight as `w` (`TemporalPass::Input::weight = 0.9`).

* History on the unjittered grid (what a temporal resolve is for):
  `d_n = (1−w)·j_n + w·d_(n−1)`, a running mean of a zero-mean jitter sequence.
  `d` stays near zero and the whole inter-frame motion is the `(1−w)` term,
  here bounded by **0.075 px**.
* History sampled at the previous position **plus the previous jitter**:
  `d_n = (1−w)·j_n + w·(d_(n−1) + j_n − j_(n−1))`, hence
  `d_n − j_n = w·(d_(n−1) − j_(n−1))`. The deviation decays by `w` per frame and
  `d_n → j_n`: the resolved image ends up displaced by the *current* jitter,
  exactly like the unresolved render, and its inter-frame shift is the **full**
  jitter step.

**Measurement.**

| pair | resolved shift (px) | ‖measured − jitter-tracking‖ | ‖measured − stable‖ | resolved / colour |
| --- | --- | ---: | ---: | --- |
| 2017→2018 | (+0.5375, +0.3252) | **0.0384** | 0.6282 | (1.057, 0.945) |
| 2018→2019 | (−0.2148, −0.5317) | **0.0425** | 0.5735 | (0.838, 0.933) |
| 2019→2020 | (+0.4964, +0.3205) | **0.0133** | 0.5909 | (1.008, 0.930) |

Every pair is within 0.043 px of the jitter-tracking prediction, while a stable
resolve would have to stay inside 0.075 px and the measured motion is 7.6–8.4×
that. Phase correlation, on its unmasked tile, agrees in sign in all six
components, and its resolved magnitude is 0.72–1.10 of its colour magnitude —
the same order, nowhere near zero.

A second, **pairing-free** discriminator: the shift between a frame's own
pre-resolve colour and its resolved image. Under jitter tracking the two sit on
top of each other (prediction 0); under a stable resolve the resolved image sits
at `−j_n`.

| frame | colour → resolved (px) | ‖ − jitter-tracking (0,0)‖ | ‖ − stable (−j_n)‖ |
| --- | --- | ---: | ---: |
| 2017 | (−0.0657, −0.0656) | **0.0929** | 0.4571 |
| 2018 | (−0.0409, −0.0815) | **0.0912** | 0.2135 |
| 2019 | (+0.0030, −0.0397) | **0.0398** | 0.3401 |
| 2020 | (+0.0055, −0.0676) | **0.0679** | 0.3807 |

All seven independent measurements say the same thing: **the temporal resolve
does not stabilize the jittered render; the output carries the full ±0.5 px
Halton wobble to the screen at frame rate.** That is exactly what the user
described as trembling, and it is the convention in `src/temporal/resolve.hlsl`,
`previousUV += 0.5 * sizeJitter.xy + history.xy`, where `history.xy` is the
previous frame's jitter in UV (`src/temporal/resolve.h`, `prepare`). The comment
at `src/renderer/temporal_pass.h:31` states the assumption explicitly: the
previous colour is treated as the previous *jittered raster* rather than as an
accumulated history that lives on the unjittered grid.

### 2. Blur: 44% of the gradient energy is gone

Same mask, same frames; the resolved FP16 image against the pre-resolve 8-bit
colour of the **same** frame.

| frame | mean squared luminance gradient, colour | resolved | ratio | high-frequency energy fraction ratio (raw / noise-corrected) |
| --- | ---: | ---: | ---: | --- |
| 2017 | 2.527e-3 | 1.427e-3 | **0.5646** | 0.6090 / 0.6092 |
| 2018 | 2.593e-3 | 1.440e-3 | **0.5554** | 0.6174 / 0.6177 |
| 2019 | 2.546e-3 | 1.478e-3 | **0.5805** | 0.6058 / 0.6060 |
| 2020 | 2.527e-3 | 1.450e-3 | **0.5737** | 0.5679 / 0.5681 |

"High-frequency energy fraction" is the share of windowed 256² tile spectral
energy at radius > size/4 (above half Nyquist). The 8-bit quantization of the
colour readback contributes a flat noise floor; it is estimated and removed in
the corrected column and turns out to be **0.04%** of the colour high band, so
the loss is real image detail, not a quantization artefact.

This is the direct consequence of defect 1. Because the history is resampled
bilinearly at a *different*, non-zero sub-pixel offset every frame instead of at
the texel centre, every frame re-filters the whole accumulated image, and with
`w = 0.9` each pixel's history survives many such passes before it decays away.
Instead of the extra detail a correct jittered accumulation produces, the run
lost about 44% of its gradient energy and 39% of its above-half-Nyquist energy.

Consistent with this, `taa_image` reports that only **1.0–2.5%** of pixels
(2.1–2.2% in burst 1) differ from the current colour by more than 2/255: the
history contributes almost nothing the current frame does not already contain,
because it has been dragged onto the current frame's position.

### 3. Frame rate, worst with few objects visible

Telemetry is CPU-side wall clock only, never GPU time; it records
count/failures/min/max/total and six duration buckets per one-second window, so
no session-wide median or percentile of frame time exists. Only per-window
figures are honest. `frame_normal` is the interval between completed Presents
and contains application work, pacing, loading and the diagnostics.

| metric | run A (TAA) | iteration 6 (route only) |
| --- | --- | --- |
| `frame_normal` count / mean / max | 8,105 / 49.8 ms / 97.4 s | 17,476 / 31.0 ms / 93.7 s |
| `frame_normal` buckets ≤1 ms / ≤10 ms / ≤100 ms / >100 ms | 0 / 737 / 7,340 / 28 | 0 / 1,990 / 15,445 / 41 |
| per-window mean, **scene windows** | **38.5 ms** (n = 217) | **16.1 ms** (n = 136) |
| per-window minimum, scene windows (median) | **36.1 ms** | **14.1 ms** |
| per-window minimum, scene windows (p10) | 17.6 ms | 10.0 ms |
| per-window mean, non-scene windows | 34.6 ms (n = 29) | 25.0 ms (n = 230) |
| `present_normal` mean / scene per-window mean | 34.1 µs / 48.6 µs | 14.7 µs / 16.3 µs |
| `draw_backend` count / mean | 2,226,284 / 2.46 µs | 3,498,808 / 2.70 µs |
| `lock_wait` count / mean / total | 6,559,985 / 0.21 µs / 1.38 s of 414 s | 10,057,622 / 0.19 µs / 1.92 s of 571 s |

The means are inflated by load stalls in both runs (a single 97.4 s interval
here). The per-window minimum is the fastest frame of each second and no stall
can inflate it: in scene windows it is **36.1 ms in run A against 14.1 ms in
iteration 6**, a factor of 2.6. Per-draw cost did not grow — `draw_backend`
mean fell slightly and `lock_wait` is unchanged per acquisition — so the added
time is per *frame*, not per draw, which is exactly the shape the user
reported ("worst when few objects are visible"). Burst 3 has only 252 draws per
frame and the run was still at ~26 fps.

**This is not a controlled measurement.** The two runs are different sectors,
different save states and different sessions, and the feature was on for the
whole of run A; even run A's non-scene windows are slower than iteration 6's.
Nothing here isolates the resolve. What is established is the magnitude of the
regression the user is seeing and that it is per-frame.

### Boundary `StretchRect` cost

Telemetry has **no** `StretchRect` metric: `Metric` in `src/proxy/telemetry.h`
covers Present, frame intervals, draws, resources, shaders, `lock_wait`,
`log_flush`, `create_device`, `reset` and cursor calls, and nothing else. There
is no `StretchBackend`-like counter with TAA on or off.

The only in-game timing of the boundary is the QPC bracket the ordered-boundary
hooks give: the `capture_event op=stretch_rect` timestamp (recorded after the
backend call) minus the previous `op=color_fill` timestamp of the same captured
frame. That bracket contains the whole hook, and the analyzer reports it for
both runs:

| run | samples | median | min | max |
| --- | ---: | ---: | ---: | ---: |
| iteration 6 (no resolve, no `--taa-debug`) | 68 | **18.3 µs** | 16.0 | 29.1 |
| run A (resolve + `--taa-debug`) | 12 | **21.5 ms** | 17.0 | 32.8 |

Run A's bracket is **not** the resolve. The log's own ordering shows
`motion_output_color_readback` and `motion_output_taa_readback` inside it
(11.8 MB read back and written to disk per captured frame), so 21.5 ms is an
upper bound dominated by `--taa-debug`. The resolve's only measurement remains
the synthetic bench in [motion-output](motion-output.md#temporal-resolve-step-3):
**0.67 ms** at 1280×768 and 2.04 ms at 5120×1440, CPU-inclusive on the Preview
backend, for the resolve plus its two copies, the state block and the copy-back.
0.67 ms does not explain a 22 ms per-frame regression; the gap is unexplained by
any measurement this run contains.

## Analyzer change: `--jitter-from-log`

`analyze_motion_readback.py` compared the producer's previous UV against the raw
raster pixel. The producer writes the **unjittered** previous UV — the design requires every
routed draw to upload `c216 = (1/W, 1/H, 0, 0)`, zero prior jitter, and the seam
fixture checks it — while the scene is rasterized at `p + j`, so every
displacement of a static object was reported as `−j`. The pre-existing `--jitter-uv` models a producer-side prior jitter, a
different quantity, and is a single constant for the whole run.

`--jitter-from-log` takes each captured frame's own `jitter_x`/`jitter_y` from
`motion_output_frame` (ignored when that frame's `jitter=0`) and unjitters the
raster pixel in `analyze_pixels` and `row_consistency`, and offsets the
previous-frame coverage lookup in `temporal_cross_check` by
`jitter_previous_x/y` (`depth_cross_check` already did this). Effect on this
run:

| | without | with |
| --- | --- | --- |
| burst 1 displacement median | 0.379, 0.305, 0.305, 0.379 px, `(dx, dy)` exactly `−(jx, jy)` | **0.0010–0.0011 px** |
| `row_consistency` max error | 0.4400 px (the 0.5 px tolerance was nearly breached) | **0.0081 px** |

Three unit tests cover it (`test_motion_readback.py`, now 23 cases): the exact
`+j` displacement shift, inertness when `jitter=0` or the fields are absent, and
the previous-coverage offset.

## Anomalies

1. **The resolve convention is wrong** (defects 1 and 2 above). This is the
   finding of the run.
2. **The previous-depth cross-check fails at its default tolerance, for reasons
   of sampling, not production.** This is the first gameplay capture with RT2
   readbacks, so `depth` is evaluated for the first time. With nearest sampling
   the within-1e-4 fraction is 0.25–0.60; with `--depth-sampling bilinear` it is
   **0.945–0.972** (median error 3e-7, p95 ≈ 7e-5) and the previous-sentinel
   count drops from 5,208 to 331. The residual 3–5% sits at depth
   discontinuities, where a bilinear tap crosses a silhouette — precisely what
   the resolve's per-tap point-sampled depth test exists to reject. The 1e-4 /
   0.99 criterion was calibrated on a 64×64 synthetic target; it needs
   restating for real perspective depth at 1280×768 (bilinear sampling, and
   either a relative tolerance or an explicit allowance for edge taps).
   No conclusion about the producer should be drawn from the current `FAIL`.
3. **The session did not shut down** (again): no `motion_output_release`, no
   `device_destroy`, `reset_count=0` and no `motion_output_reset` anywhere. The
   gameplay witnesses for teardown and for Reset with the temporal pass
   allocated remain unobtained; only the fixture covers them.
4. **`object_lifetime` still ran `active_without_baseline`** and still resolved
   scope for every draw that reached gate 5 (gate 5 = 0 in 41,916 draws).
5. **Only three bursts.** Burst 1 is the only stationary one, so the tremble
   measurement rests on three consecutive pairs and four frames. They are
   mutually consistent and the pairing-free per-frame test agrees, but a longer
   stationary capture would tighten it.
6. **`frame_capture` is far worse than iteration 6** (mean 392 ms, max 752 ms,
   n = 15 against 300 ms / 1.40 s for 85 in iteration 6) because each F8 frame
   now reads back four images totalling 31.5 MB. Expected, and a diagnostic
   cost only.

## Implications

The resolve convention fix is being implemented concurrently by another agent;
`src/temporal` and `src/renderer` were not touched for this report.

1. **Sample the history at the reprojected previous position without the
   previous jitter.** The history texture is the accumulated resolve output and
   lives on the unjittered pixel grid; only the *current* frame's samples are
   jittered, and the shader already removes that jitter before reprojecting
   (`unjittered = uv − 0.5·sizeJitter.xy − sizeJitter.zw`). The `+ history.xy`
   term in both the camera path and the motion-override path re-applies a jitter
   that the history does not carry. Expected effect, from the model the
   measurement confirms: `d_n` collapses from `j_n` to a `(1−w)`-weighted mean
   of the jitter sequence, so the per-frame wobble falls from about 0.5 px to
   under 0.075 px — the trembling stops — and the history stops being resampled
   at a new sub-pixel offset each frame, so the gradient energy loss should
   largely disappear and turn into the intended super-sampling. The same
   measurement re-run on the next stationary burst is the acceptance test:
   `verdict.tracks_jitter` must become `stable` and the gradient energy ratio
   must move from 0.56 towards 1.
2. **Keep `previous_jitter` plumbed but as a no-op for the resolve**, or delete
   it: `analyze_motion_readback.py`'s `depth_cross_check` still needs
   `jitter_previous_x/y` because RT2 *is* a jittered raster. The two buffers
   have different conventions and the fix must not conflate them.
3. **The frame-rate regression is not explained by any measurement in this
   run.** The one number that exists, the 0.67 ms synthetic bench at 1280×768,
   is 3% of the 22 ms per-frame gap. Before optimizing anything, run the paired
   capture that isolates it: the same save, the same route, once with `--taa`
   and once without, no `--taa-debug` in either. That also finally gives the
   route-cost comparison iteration 6 asked for and the colour bit-identity
   evidence (criterion 6) that is still unevidenced.
4. **Add a `StretchRect` telemetry metric** (a `Metric::StretchBackend`
   alongside `PresentNormal`), and time the resolve separately from the
   application's copy inside the hook. Without it the boundary cost cannot be
   observed in gameplay at all, and the `capture_event` bracket is unusable
   whenever `--taa-debug` is on.
5. **Restate the `depth` acceptance criterion** (anomaly 2) before it is used as
   a gate: bilinear sampling, and a tolerance that admits silhouette taps.
6. **The selector correction is confirmed in gameplay.** Every captured frame
   routed, matched 99.90% of routed draws and resolved with valid history; gate 5
   never failed in 41,916 draws; `history_pairing` found 0 disagreements over
   3,026 predictable draws; `row_consistency` explained all 336,230 sampled
   pixels within 0.0081 px. Nothing in this run argues against the route, the
   jitter, the cut detector or the readback ABI — the defect is confined to one
   term in the resolve's history lookup.
