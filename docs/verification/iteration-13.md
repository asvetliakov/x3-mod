# Iteration 13: the presented sharpened image measured, and the sharpen A/B

**Run D is the first capture of the presented image.** `MotionOutput::resolve`
reads the game's main target back a third time, *after* the RCAS draw
(`present_<device>_<frame>.bgra8`, `motion_output_present_readback`, review 27 —
[taa-sharpen.md](taa-sharpen.md), [capture-format.md](../architecture/capture-format.md)),
so the sharpen that [iteration-12.md](iteration-12.md) §2.4 could only *model*
is here **measured on the GPU**, at `--taa-sharpen 1.0` (gain `2^0 = 1.0`, the
limiter fully open) with the mip LOD bias unchanged at −0.5.

What this document establishes:

1. **The model is validated.** Over the 16 stationary readback frames the
   presented image equals the double-precision RCAS reference of the resolved
   FP16 image to **within half a code** (worst excess over 0.5: **6.5 × 10⁻⁶
   code**; mean error 0.092–0.155 code) — so iteration 12's modelled run-10
   numbers were sound, and so are the 0.75 / 0.5 models used below for the A/B.
2. **The neighbourhood contract holds at gain 1.0**: **0 of 47,185,920
   channels** leave the 3 × 3 min/max of the unsharpened codes, on the measured
   image, not a model.
3. **The A/B, on identical frames.** 1.0 buys roughly twice 0.5's sharpening
   (interior gradient energy +9.2…+15.7 % against +4.3…+6.9 %; 21–96 % of the
   resolve's 10–90 % rise loss recovered against 12–26 %) for roughly twice its
   halo (+2.9…+3.8 % strong-edge local contrast against +1.4…+1.8 %) and, on
   the one burst with a **0.01 px stationary gate met**, **four times its added
   temporal variance** (+28.3 % against +7.2 %).
4. **Health is unchanged and clean**: 251/251 resolved, 248 with history, hook
   Agree on all 251, 0 apply/restore failures, 0 state-shadow resyncs, 87 line
   kinds with no failure kind; frame time indistinguishable from run 10 at
   matched draw counts (8 bins, median −0.008 ms, sign p = 1.000); loads
   7.98 / 34.03 / 5.61 / 6.89 s, all at or below run 10's.

---

## Provenance

| | run D (this run) | run 10 = "run 4" (iteration 12) |
| --- | --- | --- |
| log | `/tmp/x3-bottleX3-run14/session-20260912-234846-212.log` | `/tmp/x3-bottleX3-run10/session-20260912-201725-212.log` |
| bytes / lines | 227,174,915 / 4,249,618 | 156,086,502 / 2,918,984 |
| sha256 | `f3bc2e2557c6ea4447e465dfc24d0bd22aa64dba16a52e4a581b0824f025efdf` | `589f236b…` |
| build | **review 29 (`a34c389`)** | review 26 (`c782a5a`) |
| route | `requested=1 taa=1 taa_debug=1 jitter=1 jitter_samples=8 rt_mode=perdraw scene_hook=1 hdr=0 mip_bias=-0.5 taa_sharpen=1.000` | same with `taa_sharpen=0.500` |
| `motion_output_taa` | `references=2 sharpen=1.000` | `references=2 sharpen=0.500` |
| per-draw stamps | `per_draw=0` | `per_draw=0` |
| line kinds | **87** (new: `motion_output_present_readback`, 24 lines; none missing) | 86 |
| readbacks | **6 bursts × 4 frames** (7294–7297, 7574–7577, 7921–7924, 8500–8503, 13370–13373, 13640–13643); colour + taa + depth + motion **+ present** | 6 bursts × 4 frames, no present |

Burst classification (`analyze_iteration09_run2.classify_burst`):

| burst | peak `cut_median_px` | max rotation | routed interior px | class |
| --- | ---: | ---: | ---: | --- |
| 7294–7297 | **0.0000** | 1.062° | 183,066 | **stationary, 0.01 px gate met** |
| 7574–7577 | 33.06 | 2.688° | 6,128 | turning |
| 7921–7924 | 80.32 | 7.620° | 2,942 | turning (the flagged cut) |
| 8500–8503 | 0.755 | 1.011° | 70,709 | stationary |
| 13370–13373 | 0.0103 | 1.093° | 60,428 | stationary |
| 13640–13643 | 0.0272 | 1.142° | 192,858 | stationary |

Four stationary bursts, one of them with `cut_median_px` **exactly 0.0** — the
first gated burst on this bottle since iteration 10 run 6, and the burst every
flicker claim below rests on. The two turning bursts are excluded from the
image sections (the resolve's own reprojection dominates there).

### Reproduction

```sh
D=/tmp/x3-bottleX3-run14/session-20260912-234846-212.log   # run D, sharpen 1.0
T=/tmp/x3-bottleX3-run10/session-20260912-201725-212.log   # run 10, sharpen 0.5
S=/tmp/x3-bottleX3-run5/session-20260912-170743-352.log    # run 5, route off
R=verification/results; W=/tmp/it13                        # W: untracked scratch

python3 tools/analysis/analyze_iteration09.py $D --no-camera-detail \
    --output $W/runD-health.json --text $W/runD-health.txt
python3 tools/analysis/analyze_iteration09_run2.py $D --captures /tmp/x3-bottleX3-run14 \
    --ideal-frames 1 --output $W/runD-taa.json
python3 tools/analysis/analyze_motion_readback.py $D --readback-dir /tmp/x3-bottleX3-run14 \
    --label iteration13 --results-dir $W/rb --jitter-from-log --no-draw-details
python3 tools/analysis/analyze_loading_profile.py $D --output $W/loading-runD
python3 tools/analysis/analyze_loading_profile.py $T --output $W/loading-run10
python3 tools/analysis/analyze_iteration11.py --run runD=$D --run run10=$T --run run5=$S \
    --primary runD --route-off run5 --previous run10 \
    --output $W/runD-cost.json --text $W/runD-cost.txt

# the image work: the measured present readback, then the two models of a
# sharpen value this run did not use (same frames, --ignore-present).
# About 4 minutes each for the 16 stationary frames in pure Python.
python3 tools/analysis/analyze_iteration12.py $D --captures /tmp/x3-bottleX3-run14 \
    --build-commit a34c389 --bottle X3 --previous-log $T \
    --output $W/runD-it12style.json --text $W/runD-it12style.txt
for s in 0.75 0.5; do
  python3 tools/analysis/analyze_iteration12.py $D --captures /tmp/x3-bottleX3-run14 \
      --sharpen $s --ignore-present --no-hash --build-commit a34c389 --bottle X3 \
      --output $W/runD-model${s/./}.json --text $W/runD-model${s/./}.txt
done

# this document's tables
python3 tools/analysis/analyze_iteration13.py \
    --measured $W/runD-it12style.json --measured-label runD_1.0 \
    --model runD_0.75=$W/runD-model075.json --model runD_0.5=$W/runD-model05.json \
    --same-scene-low runD_0.5 \
    --modelled-previous $R/iteration-12.json --previous-label run10_0.5 \
    --health $W/runD-health.json --taa $W/runD-taa.json \
    --readback $W/rb/motion-readback-iteration13-summary.json \
    --loading $W/loading-runD/loading-profile.json \
    --loading-previous $W/loading-run10/loading-profile.json \
    --cost $W/runD-cost.json --cost-pair runD:run10 \
    --flicker-baseline $R/iteration-10-run6-flicker.json \
    --build-commit a34c389 --bottle X3 \
    --output $R/iteration-13.json --text $R/iteration-13.txt

python3 -m unittest verification.analysis.test_iteration13 verification.analysis.test_iteration12
```

`analyze_iteration12.py` gained one backward-compatible flag, `--ignore-present`
(model a sharpen value the run did not use even where a present readback
exists); nothing else in it changed, and `test_iteration12.py` still passes.
`analyze_iteration13.py` reads only the JSON of the tools above — no capture
file — so its tables are cheap to regenerate.

---

## 1. Direct sharpen measurement (first time possible)

### 1.1 The presented image against the RCAS reference — the model validated

`present_1_<frame>.bgra8` (the main target after the RCAS draw) against the
double-precision RCAS reference of `taa_1_<frame>.rgba16f` at gain 1.0
(`rcas_rows`, the row-vectorised port of `run_motion_output.rcas_reference`),
16 frames, `model_agreement` in `iteration-13.json`:

| burst | max error (code) | excess over ½ code | mean error (code) |
| --- | ---: | ---: | ---: |
| 7294–7297 | 0.5000065 | **6.5 × 10⁻⁶** | 0.10914–0.10973 |
| 8500–8503 | 0.5000025 | 2.5 × 10⁻⁶ | 0.09177–0.09316 |
| 13370–13373 | 0.5000000 | 0 | 0.15216–0.15506 |
| 13640–13643 | 0.5000007 | 6.9 × 10⁻⁷ | 0.14983–0.15237 |

**Agreement to within the 8-bit quantiser on all 16 frames** (`status: pass`,
`frames_outside_tolerance: 0`). Half a code is the floor of this comparison,
not a slack: the readback is 8-bit and the reference is double, so a pixel
whose reference value lands on the `.5` boundary can round the other way on the
GPU; the excess above 0.5 is at most 6.5 × 10⁻⁶ code, i.e. the ties are ties.
This reproduces the fixture's own bound (max 0.498–0.50002 code over its five
sharpen cases, [taa-sharpen.md](taa-sharpen.md)) **on game content at
1280 × 768**, and it is what licenses:

* iteration 12 §2.4's modelled run-10 table — the tool that produced it is the
  same code path, checked here against the GPU;
* the 0.75 and 0.5 rows of §2 below, modelled on run D's own resolved frames.

### 1.2 Escapes, halo and gradient energy on the measured image

| burst | 3×3 channel escapes | strong-edge px | local-contrast ratio | changed px | max / mean change (codes) |
| --- | ---: | ---: | ---: | ---: | ---: |
| 7294–7297 | **0** | 25.4–25.8 k | 1.0287–1.0292 | 27.4–27.5 % | 21 / 0.274 |
| 8500–8503 | **0** | 25.8–27.8 k | 1.0364–1.0378 | 21.1–21.2 % | 21 / 0.226–0.230 |
| 13370–13373 | **0** | 12.7–13.3 k | 1.0319–1.0328 | 45.0–45.9 % | 20 / 0.414–0.424 |
| 13640–13643 | **0** | 13.6–15.4 k | 1.0317–1.0325 | 40.8–41.3 % | 20 / 0.370–0.371 |

**0 of 47,185,920 channels** (16 × 983,040 × 3) lie outside the 3 × 3 min/max
of the unsharpened codes, and **0** at the strong-edge pixels (3 × 3 luma range
≥ 0.2). That was the shader's clamp as a *contract* in iteration 12; it is now
a measurement of the presented frames. At gain 1.0 the sharpen touches
21–46 % of a frame by at most 20–21 codes (mean 0.23–0.42 code) and raises the
local contrast at strong edges by **2.9–3.8 %**.

Gradient energy (mean squared central-difference luma gradient,
`routed_interior`):

| burst | presented / resolved | presented / raw | resolved / raw |
| --- | ---: | ---: | ---: |
| 7294–7297 | **1.0911–1.0930** | 0.630–0.660 | 0.576–0.605 |
| 8500–8503 | **1.1495–1.1610** | 0.443–0.502 | 0.382–0.436 |
| 13370–13373 | **1.1363–1.1403** | 0.707–0.737 | 0.622–0.646 |
| 13640–13643 | **1.1521–1.1577** | 0.616–0.737 | 0.534–0.640 |

### 1.3 Rise and MTF50 on the highest-gradient tiles

Edge spread on the strongest locally-1D edges of the routed interior, each
profile aligned on its own 50 % crossing — a raw/resolved/presented comparison
on the same edges, never an absolute MTF. `recovered` is the share of the
resolve's own loss the sharpen gives back, (presented − resolved) / (raw −
resolved), computed per frame and averaged over the burst:

| burst | raw rise | resolved rise | **presented rise** | rise recovered | raw MTF50 | resolved MTF50 | **presented MTF50** | MTF50 recovered |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 7294–7297 | 0.85–1.62 px | 1.05–1.64 | **0.96–1.00** | 0.21 | 0.69–1.17 c/px | 0.35–0.36 | **0.40–0.47** | 0.18 |
| 8500–8503 | 0.81–0.83 | 1.45–1.69 | **1.16–1.25** | 0.49 | 1.23–1.31 | 0.26–0.29 | **0.31–0.32** | 0.05 |
| 13370–13373 | 0.89–0.90 | 1.15–1.25 | **1.00–1.06** | 0.56 | 0.51–0.58 | 0.31–0.31 | **0.34–0.36** | 0.15 |
| 13640–13643 | 0.85–1.67 | 1.46–1.94 | **1.16–1.75** | 0.96 | 0.65–0.67 | 0.27–0.31 | **0.31–0.35** | 0.10 |

The sharpen at 1.0 recovers **0.07–0.68 px of 10–90 % rise** per frame (burst
means 0.17–0.37 px) and **+0.030–0.113 c/px** of MTF50 (burst means +0.035 to
+0.083) — between a twentieth and (on burst 13640, where the resolve's loss on
those edges is small and the share is correspondingly noisy) nearly all of what
the resolve costs on these edges. It cannot do better: a single-lobe sharpen
forbidden to leave the 3 × 3 neighbourhood cannot restore detail the resolve
never kept.

---

## 2. A/B: 1.0 measured against 0.5 modelled

Two comparisons, and the first is the honest one:

* **same scene, same frames** (`same_scene`, `sharpen_ab.runD_*`): run D's own
  four stationary bursts at 0.5, 0.75 and 1.0. Only the gain differs; the 1.0
  column is the measured present readback, the others the reference validated
  in §1.1 applied to the same resolved frames;
* **run 10 at 0.5** (`sharpen_ab.run10_0.5`, from the tracked
  `iteration-12.json`) as a cross-check on different scene content.

### 2.1 Same-scene table (means over each burst's four frames)

| burst | s | gradient p/res | rise recovered | MTF50 recovered | halo (strong-edge range) | ESF excursion − resolved | changed px | max change | flicker energy p/res | / square law |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| **7294** gate met | 0.5 | 1.0430 | 0.125 | 0.085 | 1.0141 | **−0.008** | 14.5 % | 10 | **1.0723** | 0.986 |
| | 0.75 | 1.0622 | 0.146 | 0.129 | 1.0201 | **−0.009** | 20.2 % | 14 | **1.1237** | 0.996 |
| | **1.0** | **1.0919** | 0.208 | 0.175 | 1.0289 | +0.014 | 27.5 % | 21 | **1.2834** | **1.076** |
| 8500 | 0.5 | 1.0694 | 0.193 | 0.021 | 1.0177 | +0.023 | 11.9 % | 10 | 1.0367 | 0.907 |
| | 0.75 | 1.1032 | 0.286 | 0.031 | 1.0255 | +0.035 | 15.7 % | 14 | 1.0542 | 0.866 |
| | **1.0** | 1.1568 | 0.489 | 0.047 | 1.0371 | +0.049 | 21.2 % | 21 | 1.0817 | 0.808 |
| 13370 | 0.5 | 1.0600 | 0.261 | 0.070 | 1.0161 | +0.019 | 22.3 % | 9 | 1.0120 | 0.901 |
| | 0.75 | 1.0897 | 0.388 | 0.095 | 1.0228 | +0.024 | 33.5 % | 13 | 1.0179 | 0.857 |
| | **1.0** | 1.1381 | 0.563 | 0.149 | 1.0324 | +0.032 | 45.4 % | 20 | 1.0274 | 0.793 |
| 13640 | 0.5 | 1.0664 | 0.227 | 0.046 | 1.0157 | +0.006 | 20.5 % | 10 | 1.0273 | 0.903 |
| | 0.75 | 1.0994 | 0.404 | 0.069 | 1.0224 | +0.008 | 30.3 % | 13 | 1.0405 | 0.861 |
| | **1.0** | 1.1540 | 0.957 | 0.104 | 1.0322 | +0.012 | 41.1 % | 20 | 1.0618 | 0.797 |

Run 10's 0.5 rows on its own content (stationary 1380, 2717, 3856; slow 9536):
gradient p/res **1.0395 / 1.0467 / 1.0709 / 1.0518**, halo **1.0146 / 1.0166 /
1.0182 / 1.0155**, flicker energy **1.0701 / 1.0360 / 1.0376 / 1.0326**,
escapes 0 — the same band as run D's 0.5 column, which is the second
independent check that the two runs' 0.5 behaviour is the same thing.

Every metric is **monotone in the sharpen value on every burst**
(`test_the_ab_is_monotone_in_the_sharpen_value`), and the increments are close
to linear in the gain: 1.0 delivers about twice 0.5's gradient amplification
(+9.2…+15.7 % against +4.3…+6.9 %), about twice its halo (+2.9…+3.8 % against
+1.4…+1.8 %) and about twice its MTF50 recovery, with 0.75 landing between the
two on all of them (+6.2…+10.3 % gradient, +2.0…+2.6 % halo, 64–74 % of 1.0's
MTF50 recovery).

### 2.2 Over-sharpening indicators

| indicator | contract / reference | 0.5 | 0.75 | **1.0 (measured)** |
| --- | --- | ---: | ---: | ---: |
| channels outside the 3 × 3 min/max | **0** | 0 | 0 | **0** |
| channel escapes at strong edges | **0** | 0 | 0 | **0** |
| strong-edge local contrast | as low as possible | 1.014–1.018 | 1.020–1.026 | 1.029–1.038 |
| ESF excursion ±2 px, against the resolve | ≤ 0 ideally | −0.008…+0.023 | −0.009…+0.035 | +0.012…+0.049 |
| ESF excursion, against the raw jittered frame | must stay below | 0.06–0.51 below | 0.06–0.50 below | 0.05–0.48 below |
| flicker energy vs its own square-law prediction | ≤ 1 | 0.90–0.99 | 0.86–1.00 | 0.79–**1.076** |

Reading. Two indicators move from "clean" to "visible" between 0.75 and 1.0:

* the **ESF excursion** on the gated burst 7294 is *below* the unsharpened
  resolve's at 0.5 and 0.75 (−0.008, −0.009 of the edge contrast) and *above*
  it at 1.0 (+0.014). On all values and bursts it stays far below the raw
  jittered frames' own excursion (raw 0.115–0.744, presented 0.058–0.327), so
  the presented image never rings more than its own input aliases;
* the **square-law check**: amplifying gradients by *g* should multiply a
  temporal variance by about *g*². On the gated burst the measured flicker
  energy is 0.986 × the prediction at 0.5, 0.996 × at 0.75 and **1.076 × at
  1.0** — the only value and burst where the sharpen adds more variance than
  its own edge amplification accounts for. On the three non-gated bursts every
  value stays at 0.79–0.91 × the prediction (residual scene motion dominates
  the denominator there).

### 2.3 Flicker (temporal variance of Rec.709 luma over each burst)

Burst **7294–7297**, the only burst meeting the 0.01 px stationary gate
(`cut_median_px` peak exactly 0.0):

| class | px | resolved / raw | presented / raw at **1.0** | p/res at 0.5 | p/res at 0.75 | **p/res at 1.0** | iteration-10 run 6 baseline (resolved/raw) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| sentinel | 734,140 | 0.01186 | **0.01515** | 1.065 | 1.109 | **1.278** | 0.0152 |
| routed interior | 183,066 | 0.00732 | **0.00901** | 1.055 | 1.095 | **1.231** | 0.0097 |
| routed edge | 65,834 | 0.00604 | **0.00828** | 1.101 | 1.173 | **1.370** | 0.0089 |
| thin feature (overlay) | 46,855 | 0.00702 | **0.00847** | 1.061 | 1.105 | **1.207** | 0.0096 |
| whole image (energy) | — | — | — | 1.0723 | 1.1237 | **1.2834** | — |

The other three bursts (gate not met, 0.755 / 0.0103 / 0.0272 px):
presented/resolved energy **1.0367 / 1.0120 / 1.0273** at 0.5, **1.0542 /
1.0179 / 1.0405** at 0.75 and **1.0817 / 1.0274 / 1.0618** at 1.0; per class
1.005–1.107 at 1.0, with no class flickering disproportionately.

Two things matter here, and they point in opposite directions:

* **relative**: at 1.0 the sharpen adds **28.3 %** temporal variance energy to
  the resolve's output on a fully static frame, against **7.2 %** at 0.5 and
  **12.4 %** at 0.75 — four times the cost of 0.5 for twice the sharpening, and
  the only place any indicator exceeds its contract-free prediction (§2.2);
* **absolute**: the resolve on this burst is *much* quieter than iteration 10's
  gated burst — resolved/raw 0.0119 / 0.0073 / 0.0060 / 0.0070 against the
  baseline's 0.0152 / 0.0097 / 0.0089 / 0.0096 — so **even after sharpening at
  1.0 the presented image's variance (0.0152 / 0.0090 / 0.0083 / 0.0085) is at
  or below the iteration-10 baseline's unsharpened resolve in every class**.
  1.0 does not take the presented image outside the flicker envelope TAA has
  already been accepted at; it spends the margin this scene had.

*Not established:* scene-independence of the absolute comparison (different
sector, different content), and whether 28 % more variance on a static frame is
visible. Both of those are eye judgements the numbers can only bound.

---

## 3. The mip bias is unchanged at −0.5, so the A/B is the sharpen

| | run D | run 10 |
| --- | --- | --- |
| announced | `mip_bias=-0.5`, `taa_sharpen=1.000` | `mip_bias=-0.5`, `taa_sharpen=0.500` |
| per-frame `mip_bias` values | `-0.5` on all 266 records | `-0.5` on all 194 |
| biased / routed draws | **54,275 / 54,275** | **34,625 / 34,625** |
| sets / restores / failures | 66,591 / 66,584 / **0** | 43,326 / 43,319 / **0** |
| `mip_bias_biased_now` nonzero records (left on) | **0** | **0** |
| stage masks | `0x0f`–`0x7f` (histogram over 266 records) | `0x0f`–`0x7f` |
| stationary/slow **raw** 10–90 % rise | 0.813–1.671 px (mean 1.054) | 0.787–2.677 px (mean 1.201) |
| stationary/slow **raw** MTF50 | 0.509–1.315 c/px (mean 0.852) | 0.312–1.393 c/px (mean 1.005) |

Per stationary burst the raw frames read **0.81–0.83, 0.85–1.62, 0.85–1.67 and
0.89–0.90 px** rise in run D against run 10's **0.79–0.87, 0.84–0.86 and
0.79–0.87 px** — the same sharpness class on three of four bursts, the wider
spans belonging to bursts whose edge population includes softer content (run
10's slow burst 9536 reads 1.94–2.68 px, wider than anything in run D). The
bias is applied identically, to every routed draw, with no failure and nothing
left on, and `taa/raw` interior gradient energy is in the same band in both
runs (run D 0.404–0.638 on the stationary bursts, run 10 0.410–0.523). **The
A/B of §2 isolates the sharpen**; the raw-frame differences that remain are
scene content, not LOD.

The capture's own `sampler … state=8` reads still show the application value
(191,936 records, **0** with a nonzero bias): the per-draw diagnostics restore
the bias before they run — the documented limit from iteration 12 §1.4, not a
change.

---

## 4. Health, certification, loads, frame time

### 4.1 TAA health (`analyze_iteration09.py`)

| | run D | run 10 |
| --- | --- | --- |
| frame records / attempted / resolved | 266 / **251 / 251** | 194 / 181 / 181 |
| resolves with history | **248 of 251** | 180 of 181 |
| history match (`matched`/`routed`) | 54,056 / 54,275 = **99.60 %** | 99.82 % |
| `scene_end_check` / source | **Agree 251**, None 15, Disagree 0; hook 251 | Agree 181, None 13, Disagree 0 |
| `apply_failures` / `restore_failures` | **0 / 0** | 0 / 0 |
| `rs_resyncs` | **0** (3 misses in 494,256 queries, hit rate 0.999994) | 2 |
| gates | gate2 15,400, gate3 2,189, gate4 15,107, gate6 219, **gate1/gate5 0** | gate2 12,116, gate3 1,883, gate4 9,883, gate6 62 |
| cut detector | **3 events**: frames 7922–7924, `cut_median_px` 80.3 / 72.9 / 71.9, `taa_history=0` on each | 1 event (frame 7500, 254 px) |
| camera records / valid / cuts | 73 / 69 / **0**; rotation max **7.620°** (floor 1.626°) | 59 / 55 / 0; max 2.741° |
| `frame_normal` | 175 windows, 1 stall, fast 168 median **10.67 ms**, slow 6 median 267.8 ms | 127 windows, fast median 10.74 ms |
| sharpen | **251 of 251 resolves sharpened**, `taa_copy=S_FALSE` on all 266 records, 0 `sharpen_failed`, copy-back **0.0 µs**, `taa_run` 372.6 µs | 181/181, 0, 0.0 µs, — |
| line kinds | **87**, none missing, **no failure kind** | 86 |

The three cut events are one genuine fast turn (7.62° of rotation, 71–80 px
median displacement) that the route correctly refused to reuse history for
(`taa_history=0`); the camera cut detector's own threshold (20°) is not
reached, which is why it reports 0. Run D's history match is 0.22 pp below run
10's, entirely within the gate-4 population that grew with the faster motion.

### 4.2 Readback certification (`analyze_motion_readback.py`, 24 frames)

| check | run D | run 10 |
| --- | --- | --- |
| `readback_integrity` | **pass**, 24 frames, 24 clean | pass |
| `counter_consistency` | **pass** | pass |
| `row_consistency` | **pass** — 781,504 sampled px, **0 unexplained**, max error **0.0762 px** (tol 0.5) | pass, 0.0547 px |
| `temporal_coverage` | **pass** — worst covered fraction **0.9877** | pass, 0.9809 |
| `history_pairing` | **pass** — 6,606 predictable draws, **0 disagreements** | pass, 4,386 / 0 |
| `depth_image_integrity` | **pass** — `valid_motion_without_depth` 0 | pass |
| `taa_image` | **pass** (finite; signal only) | pass |
| `static_consistency` | unavailable (needs a static burst by its own rule) | unavailable |
| `displacement` | **fail** — 141,742 of 4,188,179 px (3.38 %) over the 64 px bound, max **244.5 px** | pass (max 50.9 px) |
| `depth` (previous-depth agreement) | **fail** — within-fraction **0.358**, max error 0.155 (tol 1e-4, nearest sampling) | fail, 0.340 |

`displacement` is attributed per burst by this tool
(`health.displacement.bursts`): **all 141,742 suspicious pixels are in burst
7921–7924** — the fast-turn burst whose `cut_median_px` peak is 80.3 px, i.e.
already past the check's own 64 px bound — and **0** in the other five bursts,
whose maxima are 1.3 / 56.4 / 22.1 / 3.4 / 10.4 px. The check's bound is a
heuristic for a scene without a cut; the cut detector flagged exactly the same
three frames. **Not a motion defect: the certification of the stationary
material stands** (row-pair consistency 0.076 px over 781,504 px, 0 history
disagreements over 6,606 predictable draws). `depth` fails at **0.358, the best
value recorded** (run 10 0.340, run 6 0.335) — pre-existing, the nearest-sampled
previous-depth comparison documented in [iteration-10.md](../archive/iteration-10.md).

### 4.3 Loads and frame time

| load | run D | run 10 |
| --- | ---: | ---: |
| startup → menu | **7.984 s** (ends frame 36 at 13.1 s) | 8.584 s |
| save load (menu → sector) | **34.027 s** (ends frame 642 at 54.8 s) | 39.167 s |
| sector change | **5.608 s** (ends frame 13,025 at 211.9 s) | 5.725 s |
| return to menu | **6.889 s** (ends frame 14,318 at 236.9 s) | 6.920 s |
| total in gaps | **54.51 s** over 4 | 60.40 s over 4 |

Every load is at or below run 10's, the save load by 5.14 s — the same
run-to-run spread iteration 12 flagged in the other direction, with the zlib
work inside the save-load gap unchanged (`gzread` 175 calls / 0.188 s,
`inflate` 359,874 calls / **5.556 s** of the 34.03 s, against run 10's 359,874
calls / 5.844 s; session `inflate` 773,757 calls / 11.76 s). The gz buffer
reproduces exactly: **14,461,803 savegame calls (99.26 % small), 45.76 MB in
175 real reads**, 0 errors.

Frame time (`analyze_iteration11.py`, both runs `per_draw=0`): scene-fast
**10.539 ms at 281.0 draws/frame** against run 10's **10.444 ms at 275.5**
(+0.91 % at +2 % draws). At **matched draw counts** — 8 paired bins over
84.5–815.2 draws — the median difference is **−0.008 ms (−0.201 µs/draw)** with
4 bins each way, **sign test p = 1.000**: the extra sharpen gain costs nothing
measurable (it is the same one-lobe draw at a different constant). Route
recovery against the route-off run 5 improved on its own account: 8.90 µs/draw
attributed against run 10's 11.45 (2.55 µs/draw recovered, 22.3 %), route share
of the frame 0.319 against 0.384.

---

## 5. Report

1. **Measured, and the model validated.** `present_1_*` (after the RCAS draw) equals the double RCAS reference of `taa_1_*` to **≤ 0.5 code** on all 16 stationary frames (worst excess 6.5 × 10⁻⁶, mean 0.09–0.16): iteration 12's modelled run-4 table was sound, and so are the 0.75 / 0.5 models here.
2. **Clamp contract holds on the GPU at gain 1.0**: **0 of 47,185,920 channels** outside the 3 × 3 min/max, 0 at strong edges.
3. **1.0 buys** (four stationary bursts, means): interior gradient energy **+9.2…+15.7 %**, rise **0.07–0.68 px** of the resolve's loss back per frame (burst-mean share 0.21–0.96), MTF50 **+0.030–0.113 c/px**.
4. **1.0 costs**: strong-edge contrast **+2.9…+3.8 %**, ESF excursion **+0.012…+0.049** above the resolve (still 0.05–0.48 *below* the raw frames'), and on the one 0.01-px-gated static burst **+28.3 %** variance energy — **1.076 × its own square-law prediction**, the only indicator above prediction anywhere.
5. **A/B against 0.5** (same frames): 0.5 → +4.3…+6.9 % gradient, +1.4…+1.8 % halo, **+7.2 %** static variance (0.986 × square law, excursion below the resolve). 0.75 → +6.2…+10.3 %, +2.0…+2.6 %, **+12.4 %** (0.996 ×, excursion still below) and **64–74 % of 1.0's MTF50 recovery**. Run 10's 0.5 on other content agrees with run D's 0.5 (gradient 1.040–1.071, halo 1.015–1.018).
6. **Recommendation: default `X3M_TAA_SHARPEN=0.75`** (0 stays the shipped default until you choose). Every indicator stays inside contract — escapes 0, halo ≤ 2.6 %, excursion at or below the unsharpened resolve on the static burst, variance 0.996 × square law — for two thirds of 1.0's edge recovery. **1.0 is defensible, not unsafe**: its presented variance (0.0152 / 0.0090 / 0.0083 / 0.0085 per class) is at or below iteration 10's *unsharpened* resolve (0.0152 / 0.0097 / 0.0089 / 0.0096), and it costs no frame time (p = 1.000 at matched draws). Pick 1.0 for sharpness over static stability, 0.5 if any added flicker is unacceptable. Next run should *measure* 0.75 (one env var, no code).
7. **Mip bias −1.0: not next.** −0.5 is unchanged (54,275/54,275 routed draws biased, 0 failures, 0 left on; raw rise 0.81–0.90 px on three of four stationary bursts vs run 10's 0.79–0.87), so the A/B is the sharpen. Raw MTF50 is already **0.51–1.31 c/px**, at/above what the resolve keeps — `resolve_lowpass` single-tap **0.000** at period 2 px and **0.688** at 3 px (steady 0.100 / 0.262) — so another octave of texture mostly feeds variance, not retained detail. Settle the sharpen first; if −1.0 is tried, pair it with the chosen sharpen and require a gated static burst in the same sector.
8. **Health**: 251/251 resolved, 248 with history, hook Agree 251, 0 apply/restore/shadow failures, 87 line kinds, no failure kind; loads 7.98 / 34.03 / 5.61 / 6.89 s (all ≤ run 10); certification passes everything except pre-existing `depth` (0.358, best recorded) and `displacement`, whose 141,742 flagged pixels are **all** in the 80 px fast-turn burst the cut detector flagged.

### Tracked artefacts written

* `verification/results/iteration-13.json` / `.txt` — every table above.
* `tools/analysis/analyze_iteration13.py` — the tables; reads only JSON.
* `tools/analysis/analyze_iteration12.py` — one new flag, `--ignore-present`.
* `verification/analysis/test_iteration13.py` — 23 tests (16 synthetic, 7
  pinning the published summary).

Untracked (scratch): the per-run inputs under `/tmp/it13/` and the capture
directory `/tmp/x3-bottleX3-run14/` (24 each of `color_1_*`, `taa_1_*`,
`depth_1_*`, `motion_1_*`, `present_1_*` plus the log).
