# Iteration 12: post-resolve sharpen 0.5 and mip LOD bias −0.5 in flight

**Status: COMPLETE (2026-09-12 21:10; paused 20:36 for the account switch,
resumed in a worktree).** Every number in this document is now emitted by
`tools/analysis/analyze_iteration12.py` (tests in
`verification/analysis/test_iteration12.py`), which re-emitted
`verification/results/iteration-12.json`/`.txt` from the run-10 data with the
same numbers the hand-off's one-off code had produced; **§2.4** (over-
sharpening and flicker on the RCAS-modelled presented image) is analysed
below, burst 9536–9539 is evaluated, and the two tool defects are fixed. The
measurement gap itself — the presented image was never read back — is closed
in production by the `present_<device>_<frame>.bgra8` readback of review 27
([taa-sharpen.md](taa-sharpen.md), [capture-format.md](../architecture/capture-format.md));
run 10 predates it, so §2.4 is a **model**, and the next `--taa-debug` run
measures the GPU image with the same tool. See
[Completed after the hand-off](#completed-after-the-hand-off).

Run 4 on the **X3** bottle (arm64 Wine + FEX, `FEX_X87REDUCEDPRECISION=1`,
`WINEMSYNC=1`) with the review-26 build, commit `c782a5a`, installed `d3d9.dll`
SHA-256 `8864bff0…`:

```
tools/manage.py launch --direct --ownership --object-trace --object-lifetime \
    --motion-output --taa --taa-debug --telemetry --gz-buffer \
    --taa-sharpen 0.5 --taa-mip-bias -0.5 \
    --capture-start 999999 --capture-frames 4
```

Scene hook on by default. Same path as run 3 (iteration 11): start → menu →
save → flight → sector change → back to the menu → exit.

**Verdict so far, four sentences.** Both features are live in game and neither
reports a fault: `mip_bias=-0.5` on all 194 `motion_output_frame` records with
**43,326 sets / 43,319 restores, 0 failures, `mip_bias_biased_now=0` on every
frame**, and the resolve drew the sharpened display image on **181 of 181
resolves** (`taa_sharpen=1`, `taa_copy=S_FALSE`, no `motion_output_sharpen_failed`
line anywhere). The sampler cost is **1.268 `SetSamplerState` calls per routed
draw = 232.5 calls/frame** in ordinary frames, and the route already uses the
cheap policy the question asks about — the bias survives consecutive routed
draws and is only re-set after an intervening unrouted draw, which the source
(`apply_mip_bias`, `if (eligible == s.biased) continue;`) and the numbers agree
on. That cost is **not visible in the frame time**: at a matched draw count run
10 is statistically indistinguishable from run 3 (11 bins, +1.599 µs/draw, sign
p = 1.00) and its fast-regime frame is *lower* — **10.44 ms at 275.5 draws/frame
against 12.10 ms at 275.0** — with the attributed route cost essentially
unchanged (11.45 vs 11.89 µs/draw). The mip bias **is** visible in the pictures
— stationary-burst raw MTF50 **0.69–1.39 c/px** against iteration 9 run 2's
0.34–0.74, raw edges sub-pixel at 0.79–0.87 px — but the sharpen is **not
measurable from this run at all**: the `taa_1_*.rgba16f` readback is taken from
`out.color_surface` *before* the RCAS draw, so it is the **unsharpened
resolve**, and the sharpened display image is never read back
(see [section 2](#2-image-sharpness)).

## Provenance

| | run 10 (this run) | run 9 (iteration 11) | run 5 (route off) |
| --- | --- | --- | --- |
| log | `/tmp/x3-bottleX3-run10/session-20260912-201725-212.log` | `/tmp/x3-bottleX3-run9/session-20260912-194757-212.log` | `/tmp/x3-bottleX3-run5/session-20260912-170743-352.log` |
| bytes / lines | 156,086,502 / 2,918,984 | 56,092,939 / 1,021,360 | 24,838,333 / 490,146 |
| sha256 | `589f236bb850fc0a7c68158eee9fe80161c7e5e7e2a9c46204630da48a4d6c02` | `1a00d7d0…` | `8105f336…` |
| build | review 26 (`c782a5a`) | review 25 (`1924c55`) | `1d36c29` |
| route | `requested=1 taa=1 taa_debug=1 jitter=1 rt_mode=perdraw scene_hook=1 hdr=0 mip_bias=-0.5 taa_sharpen=0.500` | `… taa_debug=0 … mip_bias/sharpen absent` | `requested=0` |
| per-draw stamps | `per_draw=0` | `per_draw=0` | absent flag (= on) |
| line kinds | **86** | 83 | 62 |
| readbacks | **6 bursts × 4 frames** (1380–1383, 1746–1749, 2717–2720, 3856–3859, 8369–8372, 9536–9539), colour + taa + depth + motion | 7 single frames | 1 |

The readback directory `/tmp/x3-bottleX3-run10/` holds **only this run's** 96
files (24 each of `color_1_*.bgra8`, `taa_1_*.rgba16f`, `depth_1_*.r32f`,
`motion_1_*.rgba32f`) plus the log — unlike run 9's cumulative directory.

Line kinds against run 9: three new (`motion_output_color_readback`,
`motion_output_taa_readback` — `--taa-debug` is back — and
`motion_output_mip_bias_game_write`), **none missing**, and **no kind whose name
contains fail / error / warn / fault / reset / unwind / veto / disagree exists
at all**.

### Reproduction

```sh
T=/tmp/x3-bottleX3-run10/session-20260912-201725-212.log     # run 10
U=/tmp/x3-bottleX3-run9/session-20260912-194757-212.log      # run 9  (iteration 11)
S=/tmp/x3-bottleX3-run5/session-20260912-170743-352.log      # run 5  (route off)

python3 tools/analysis/analyze_iteration09.py $T --no-camera-detail \
    --output $R/iteration-12-run10-health.json --text $R/iteration-12-run10-health.txt

# cost: the iteration-11 draws-per-frame surrogate (this run is per_draw=0 too)
python3 tools/analysis/analyze_iteration11.py --run run10=$T --run run9=$U --run run5=$S \
    --primary run10 --route-off run5 --previous run9 \
    --output /tmp/it12/iteration-12-cost.json --text $R/iteration-12-cost.txt

# blur (the fixed grouping evaluates burst 9536 too) and the readback certification
python3 tools/analysis/analyze_iteration09_run2.py $T --captures /tmp/x3-bottleX3-run10 \
    --ideal-frames 1 --output /tmp/it12/run10-taa.json --text /tmp/it12/run10-taa.txt
python3 tools/analysis/analyze_motion_readback.py $T --readback-dir /tmp/x3-bottleX3-run10 \
    --label iteration12 --results-dir /tmp/it12/rb --jitter-from-log --no-draw-details

# this document's numbers (sections 1, 2, 2.4, 3.1-3.2, 4.2): about 12 minutes,
# the RCAS model of the 16 stationary/slow frames being the bulk of it
python3 tools/analysis/analyze_iteration12.py $T --captures /tmp/x3-bottleX3-run10 \
    --taa /tmp/it12/run10-taa.json --taa-baseline $R/iteration-09-run2-summary.json \
    --readback /tmp/it12/rb/motion-readback-iteration12-summary.json --previous-log $U \
    --flicker-baseline $R/iteration-10-run6-flicker.json \
    --build-commit c782a5a --dll-sha256-prefix 8864bff0 --bottle X3 \
    --output $R/iteration-12.json --text $R/iteration-12.txt
```

---

## 1. Did the sharpen and the mip bias work in game?

### 1.1 Announcement lines

```
motion_output_mode  … taa=1 taa_debug=1 jitter=1 jitter_samples=8 … scene_hook=1 hdr=0
                       taa_k=-1.00000 mip_bias=-0.5 taa_sharpen=0.500
motion_output_device … mip_bias=-0.5
motion_output_taa    device=1 initialize=00000000 references=2 sharpen=0.500
```

`references=2` is the pass holding both the resolve and the sharpen program, as
the fixture record requires; `initialize=00000000` — the sharpen program
compiled on the device.

### 1.2 The sharpen: 181 of 181 resolves drew the sharpened display image

`taa_sharpen` on the frame line is a **flag** (`counters_.taa.sharpened`), not
the setting. Over the 194 `motion_output_frame` records:

| field | value |
| --- | --- |
| `taa_attempted` / `taa_resolved` / `taa_history` | 181 / 181 / **180** |
| `taa_sharpen` (sharpened flag) | **1 on all 181 resolved frames**, 0 on the 13 unresolved ones |
| `taa_copy` | `00000001` = **S_FALSE on every record** — no copy-back ever ran |
| `motion_output_sharpen_failed` | **0 lines** |
| `taa_copy_back_us` | **0.0 µs/frame** median (run 9: 0.9) |

`taa_copy = S_FALSE` with `hdr=0` is reached only through the
`out.display_written` branch (`motion_output.cpp:720`), i.e. the RCAS draw
replaced the `StretchRect` copy-back. Every resolved frame took that branch, the
sharpen never fell back, and no failure counter moved.

### 1.3 The mip bias: applied, restored, never left on

| field | value over 194 records |
| --- | --- |
| `mip_bias` | **−0.5 on every record** |
| `mip_bias_sets` / `mip_bias_restores` | **43,326 / 43,319** |
| `mip_bias_draws` (routed draws that carried a bias) | **34,625** = every routed draw |
| `mip_bias_failures` | **0 on every record** |
| `mip_bias_biased_now` | **`0000` on every record** — never left set at the frame boundary |
| `mip_bias_reads` | 0 on 193 records, **5 on frame 3858** (the shadow re-read after a resync) |
| `mip_bias_stages` (per-frame union mask) | `000f` ×37, `001f` ×5, `002f` ×21, `003f` ×13, `006f` ×50, `007f` ×55, `0000` ×13 |
| `apply_failures` / `restore_failures` | 0 / 0 |

The stage masks say the bias lands on **stages 0–3 in every routed frame**
(`0x0f`) and on stages 4, 5 and 6 as well when the frame binds mip chains there
— up to `0x7f`, seven stages. Stages 7+ never carry it.

**The game's own writes.** 16 `motion_output_mip_bias_game_write` lines (the log
cap) and `mip_bias_game_writes_total=715` on the last record. Every one of the
16 is `bias=0` on **stage 4 then stage 3**, paired, at frames 4 and 558–564:

```
motion_output_mip_bias_game_write device=1 frame=4   stage=4 value=00000000 bias=0 writes=1
motion_output_mip_bias_game_write device=1 frame=4   stage=3 value=00000000 bias=0 writes=2
… frames 558, 559, 560, 561, 562, 563, 564, the same stage-4/stage-3 pair …
```

So the engine writes `D3DSAMP_MIPMAPLODBIAS = 0` on its two cube-sampler stages
(3 and 4, per the effect files) 715 times in the session, and 0 is exactly what
the route's restore puts back there — the documented interaction, working.

**`motion_output_mip_bias_summary`: the line does not exist in this log.** It is
emitted at teardown (`motion_output.cpp:284`) and the session log ends at frame
10200 with no teardown records, so the session totals
(`mip_bias_total_sets_`/`_restores_`/`_reads_`/`_failures_`) are not available.
The per-frame counters above are the whole evidence; the cumulative
`mip_bias_game_writes_total=715` is the only session total that survives.
*Flagged as an observability gap.*

### 1.4 The capture's `sampler … state=8 bias=` lines cannot see the bias

All **131,536** `sampler stage=N state=8` records in the six bursts read
`value=0 bias=0` — **every one, routed and unrouted draws alike**:

```
$ grep '^sampler ' $T | grep ' state=8 ' | awk '{print $3,$4,$5}' | sort | uniq -c
131536 state=8 value=0 bias=0
```

This is the documented limit of the capture, not evidence of absence:
[taa-mip-bias.md](taa-mip-bias.md) *Limits* — "the capture's own `sampler …
state=8 bias=` lines show the application's value, because capture frames
restore before every draw's diagnostics." The counters confirm it from the other
side: those same 24 capture frames spent **25,070 sets and 25,069 restores** on
5,841 routed draws (4.29 sets/draw — every routed draw re-setting after the
diagnostics' restore), against 0.634 sets/draw in ordinary frames. So the
question "are the routed draws' mipped stages at −0.5 and the unrouted ones at
0?" **cannot be answered from the capture log of this build**; it is answered by
the fixture (`production-mipbias-on`: mask `0x11` after every routed draw, `0x00`
after every unrouted one, 112 verdicts per run) and, in game, only by the
counters and by the raw-frame sharpness of section 2.

*Flagged:* to see the bias in a capture, the diagnostics would have to read
sampler state **before** the restore, or record the route's shadow alongside.

### 1.5 Conclusion (question 1)

**Yes, both work.** The sharpen ran on every resolve (181/181 sharpened,
`taa_copy=S_FALSE` throughout, 0 failures, the copy-back gone from the timings)
and the mip bias was applied to every routed draw (34,625 of 34,625) across up
to seven sampler stages, with **0 failures, 0 frames left biased, and the
engine's own 715 writes of bias 0 on its cube stages 3/4 preserved**. Two
observability gaps: the teardown `motion_output_mip_bias_summary` line is absent
from this log, and the capture's own sampler reads structurally cannot show the
route's bias.

---

## 2. Image sharpness

**PARTIAL.** The resolve-vs-raw metrics and the like-for-like iteration-9 table
are **done** (§2.2–2.3); the sharpen's own effect and the over-sharpening /
flicker checks are **TODO: not yet analysed** (§2.4).

### 2.1 What the readbacks are, and what they cannot show

What is established, and it changes how the question must be asked:

* `motion_output_color_readback` → `color_1_*.bgra8` is the **pre-resolve** main
  target, read before `taa_->run()` (`motion_output.cpp:655`). These raw frames
  **were sampled with the −0.5 bias**, so raw-frame gradient energy against
  iteration 9 run 2's raw frames is the in-game mip-bias evidence.
* `motion_output_taa_readback` → `taa_1_*.rgba16f` is `out.color_surface`, read
  **after** the resolve but **before/independently of** the RCAS draw
  (`motion_output.cpp:713`) — it is the **unsharpened resolve**, in both this run
  and iteration 9 run 2. **The sharpened presented image is never read back.**
  The `taa/raw` ratio therefore measures the resolve exactly as iteration 9 run 2
  measured it, with the mip bias now in the raw frame, and says *nothing* about
  the sharpen.
* Consequently the sharpen's effect has to be **modelled**: apply the tracked
  double-precision RCAS reference (`run_motion_output.py:rcas_reference`, gain
  `sharpen_gain(0.5) = 2^-1 = 0.5`) to the resolved FP16 image and measure the
  iteration-9 metrics on the result. Over-sharpening checks (pixels outside the
  3×3 min/max — the shader clamps, so 0 is the contract; halo ratio at strong
  edges) are checks on that modelled image, and are a *reference* claim, not a
  GPU measurement. `numpy` is **not installed**, so the reference needs a
  row-vectorised pure-Python rewrite before it can run on 1280×768.
* *Flagged as the run's real verification gap, closed in review 27:* to
  measure the presented sharpened image the route needs a readback of the
  **main target after** the sharpen draw. `MotionOutput::resolve` now reads it
  as `present_<device>_<frame>.bgra8` (`motion_output_present_readback`)
  after the RCAS draw or the copy-back, and `hdr_writeback` after the HDR
  write-back; the fixture shows it byte-identical to the presented frame and
  equal to the RCAS reference of `taa_1_*` within 0.5 code
  ([taa-sharpen.md](taa-sharpen.md)). `analyze_iteration12.py` §2.4 reads the
  file directly when a capture has it. **Run 10 predates the readback, so its
  §2.4 is the model; the next run measures.**

Burst classification (from `cut_median_px` / `camera_rotation_deg` on the burst
frames, `analyze_iteration09_run2.classify_burst`):

| burst | peak `cut_median_px` | max rotation | routed / draws | class |
| --- | --- | --- | --- | --- |
| 1380–1383 | 0.0068 | 1.069° | 193 / 246 | **stationary** |
| 1746–1749 | 23.63 | 2.741° | 237–247 / 290–300 | turning |
| 2717–2720 | 0.2702 | 1.101° | 178 / 223 | **stationary** |
| 3856–3859 | 0.8072 | 1.020° | 597–599 / 729–732 | **stationary** |
| 8369–8372 | 12.31 | 1.714° | 229 / 363 | turning |
| 9536–9539 | 2.7726 | 1.101° | 19 / 39 | slow |

Three stationary bursts (against iteration 9 run 2's two) and one slow burst is
a better population than run 2 had. Every burst frame resolved with history and
`taa_sharpen=1`; jitter indices walk the 8-phase table as expected. Burst
9536–9539 first came out **unevaluated**: `burst_frames(gap=1)` merged the
adjacent `frame_log` record at 9540 (logged because 9540 % 60 == 0, no
readback), so the group reported `missing_readbacks`. **Fixed**: `scan_log`
now collects the frames with `motion_output_readback` lines and
`burst_frames(readback_frames=…)` groups only those (a log without readback
lines keeps the old behaviour; `test_iteration09_run2.py`). The burst is
evaluated in the table below (slow, 118,248 interior pixels — the largest
routed interior of the run).

### 2.2 Like-for-like table, identical code

`analyze_iteration09_run2.py` unmodified, on run 10 and on the tracked
iteration-9-run-2 summary (same tool, same parameters, `--ideal-frames 1`).
Mean squared central-difference luma gradient on `routed_interior`; edge
spread/MTF on the strongest locally-1D edges of that mask, each profile aligned
on its own 50 % crossing — **a raw-vs-resolved comparison on the same content,
never an absolute MTF** (values above 0.5 c/px are the estimator on sub-pixel
edges, and appear in both runs).

| run / burst | class | interior px | ratio taa/raw | **raw** 10–90 % rise | **raw** MTF50 | resolved rise | resolved MTF50 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| **run 10** 1380–1383 | stationary | 71,519 | 0.517–0.537 (**0.523**) | **0.80–0.83 px** | **1.34–1.39 c/px** | 0.97–1.00 px | 0.42–0.44 |
| **run 10** 2717–2720 | stationary | 66,811 | 0.426–0.500 (**0.459**) | **0.79–0.87** | **1.35–1.39** | 1.07–1.37 | 0.31–0.36 |
| **run 10** 3856–3859 | stationary | 63,447 | 0.381–0.455 (**0.410**) | **0.84–0.86** | **0.69–1.24** | 1.30–1.58 | 0.28–0.31 |
| **run 10** 1746–1749 | turning | 3,576 | 0.519–0.677 (0.610) | 1.36–1.71 | 0.44–1.31 | 1.60–4.50 | 0.18–0.25 |
| **run 10** 8369–8372 | turning | 3,539 | 0.618–0.791 (0.728) | 0.98–5.25 | 0.31–0.52 | 1.38–2.01 | 0.22–0.31 |
| **run 10** 9536–9539 | slow | 118,248 | 0.598–0.691 (0.648) | 1.94–2.68 | 0.31–0.32 | 2.25–2.49 | 0.21–0.26 |
| it09r2 629–632 | stationary | 34,361 | 0.488–0.521 (0.505) | 0.86–0.90 | 0.68–0.69 | 1.07–1.67 | 0.35–0.46 |
| it09r2 1427–1430 | stationary | 167,472 | 0.621–0.642 (0.628) | 1.81–1.83 | 0.34–0.74 | 1.93–2.03 | 0.27–0.29 |
| it09r2 3685–3688 | slow | 5,815 | 0.630–0.804 (0.731) | 0.99–1.64 | 0.32–0.42 | 1.31–1.53 | 0.24–0.29 |

**The mip bias is visible in the raw frame, which is the half of the fix this
table can see.** Every run-10 stationary burst has a raw 10–90 % rise of
**0.79–0.87 px** and a raw MTF50 of **0.69–1.39 c/px**, against iteration 9 run
2's 0.86–1.83 px and 0.34–0.74 c/px: on the two best-conditioned bursts the raw
MTF50 is **≈2× iteration 9 run 2's best raw frame** and the raw edge is
sub-pixel in all three. The bottles, scenes and content differ, so this is
consistent-with, not proof-of, the −0.5 LOD — but it is exactly the signature
[iteration-09-run2.md](iteration-09-run2.md) §7 predicted for it ("the raw-frame
gradient energy should rise and the *resolved* ratio should [fall]").

**And the resolved ratio did fall**, exactly as predicted: 0.410–0.523 on the
stationary bursts against 0.505–0.628. That is not a regression — it is the
resolve filtering away the extra high-frequency energy the bias added. The
**resolved** MTF50 (0.28–0.44) and rise (0.97–1.58 px) are statistically the
same as iteration 9 run 2's (0.27–0.46, 1.07–2.03 px), i.e. **the resolve's
output is no sharper than before**. The presented image — resolve + RCAS — is
the one that should be, and it is not captured.

### 2.3 Ideal-supersampling floor, stationary bursts

`captured_phase_average` (the burst's own four raw phases averaged, no
resampling) scaled to the resolve's 8-phase kernel:

| run / burst | raw phase spread (all / interior) | 4-phase ratio | scale to resolve kernel | **ideal floor** | measured | measured / ideal | share of loss that is ideal SS |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| **run 10** 1380 | 1.52 % / **0.57 %** | 0.6406 | 0.9962 | **0.6382** | 0.5230 | 0.819 | **75.8 %** |
| **run 10** 2717 | 2.27 % / 23.6 % | 0.3879 | 0.9558 | 0.3708 | 0.4590 | 1.238 | **116.3 %** |
| **run 10** 3856 | 0.95 % / 4.18 % | 0.3649 | 1.0366 | 0.3782 | 0.4102 | 1.085 | **105.4 %** |
| it09r2 629 | 1.75 % / 7.03 % | 0.6497 | 0.8735 | 0.5675 | 0.5050 | 0.890 | 87.4 % |
| it09r2 1427 | 0.96 % / 1.60 % | 0.6723 | 0.9751 | 0.6556 | 0.6277 | 0.958 | 92.5 % |

On bursts 2717 and 3856 the measured resolve is **sharper than the ideal
4-phase supersample of the same content** (share ≥ 100 %), so the resolve adds
**no** loss beyond what jitter supersampling of this now-sharper content costs.
Burst 1380, the only burst whose raw phase spread is tight enough (0.57 %
interior) to trust unreservedly, reads 75.8 % — the resolve adding 24 %, more
than run 2's 11–7 %, which is what a sharper raw frame does to this ratio. No
burst indicates a defect in the resolve.

### 2.4 Over-sharpening and flicker on the RCAS-modelled presented image

> **Validated after the fact:** run D of [iteration-13.md](iteration-13.md)
> reads the presented image back and finds it equal to the reference this
> section models, to within half a code on all 16 of its stationary frames —
> so the modelled table below is sound, and iteration 13 carries the measured
> 1.0-vs-0.5 A/B.

**A reference claim, not a GPU measurement.** Run 10 has no `present_1_*`
readback (review 27 adds it), so `analyze_iteration12.py` models the presented
image: RCAS at gain `sharpen_gain(0.5) = 0.5` of the resolved FP16 readback
`taa_1_*.rgba16f`, in double, the fixture runner's `rcas_reference` arithmetic
row-vectorised (`rcas_rows`; `test_iteration12.py` pins it to the runner's
per-pixel reference to 1e-12 and the runner pins that reference to the GPU
within 0.5 code), quantised to 8-bit codes. The four bursts whose motion is
not `turning` were modelled — the three stationary ones and the slow 9536 —
16 frames, about 40 s each in pure Python. Section keys:
`presented.bursts[]` in `iteration-12.json`; when a capture carries
`present_<device>_<frame>.bgra8` the same code reads the file instead
(`source=present_readback`) and reports its error against the model.

**(a) Neighbourhood escapes: 0.** Over the 16 frames, **0 of 47,185,920
channels** (16 × 983,040 × 3) lie outside the 3×3 min/max of the unsharpened
codes, and 0 at the strong-edge pixels below. In the model this follows from
the clamp the reference implements — it is the contract the fixture measured
on the GPU (0 outside on every sharpen case, incl. the new present readback),
and the check is what the run-11 files will be judged by.

**(b) What the sharpen does to the image, and the halo.** Per frame:

| burst | class | pixels changed | max / mean abs change (codes) | interior gradient presented/raw (resolved/raw) | presented / resolved | edge rise resolved → presented | MTF50 resolved → presented | ESF excursion ±2 px resolved → presented | strong-edge px, local range ratio |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1380–1383 | stationary | 12.6–12.7 % | 9 / 0.088–0.089 | 0.537–0.558 (0.517–0.537) | **1.038–1.040** | 0.97–1.00 → **0.94–0.96 px** | 0.42–0.44 → **0.44–0.47** | +0.257–0.297 → +0.302–0.317 | 22.4–22.7 k, **1.014–1.015** |
| 2717–2720 | stationary | 13.9–14.2 % | 10 / 0.100–0.101 | 0.447–0.522 (0.426–0.500) | **1.043–1.050** | 1.07–1.37 → 1.04–1.31 | 0.31–0.36 → 0.32–0.37 | +0.271–0.443 → +0.273–0.485 | 26.1–27.1 k, 1.016–1.017 |
| 3856–3859 | stationary | 10.3–10.4 % | 10 / 0.079–0.084 | 0.408–0.486 (0.381–0.455) | **1.069–1.072** | 1.30–1.58 → **1.16–1.38** | 0.28–0.31 → 0.30–0.33 | +0.207–0.321 → +0.227–0.331 | 25.6–28.1 k, 1.018 |
| 9536–9539 | slow | 21.0–21.3 % | 10 / 0.125–0.128 | 0.629–0.725 (0.598–0.691) | 1.050–1.053 | 2.25–2.49 → 2.20–2.44 | 0.21–0.26 → 0.22–0.27 | +0.033–0.051 → +0.032–0.055 | 27.4–29.9 k, 1.015–1.016 |

Reading. The sharpen at 0.5 touches 10–14 % of a stationary frame's pixels
(21 % of the slow burst's) by at most 9–10 codes and 0.08–0.13 code on
average, and raises the routed-interior gradient energy of the resolve by
**3.8–7.2 %** — the fixture's synthetic 1.063 at 0.5 ([taa-sharpen.md](taa-sharpen.md))
reproduced on game content. On the edges it recovers **0.03–0.20 px of the
resolve's 10–90 % rise** (1380: 0.98 → 0.95 px against a raw edge of 0.82;
3856: 1.45 → 1.28 against 0.85) and 0.01–0.03 c/px of MTF50 — a fifth to a
third of what the resolve costs on these bursts, as expected of a single-lobe
sharpen forbidden to overshoot. Halo: the mean aligned edge profile's
excursion beyond its end values within ±2 px rises by **+0.02 to +0.06 of the
edge contrast** on the stationary bursts (e.g. 1380: +0.26 → +0.31) and not
at all on the slow burst (+0.04 → +0.04); the raw jittered frames read
+0.55–1.30 on the same edges, so the sharpened image is far below the input's
own aliasing. At strong edges (3×3 luma range ≥ 0.2, 22–30 k px per frame)
the local contrast rises by **1.4–1.8 %** with 0 channels leaving the
neighbourhood. No over-sharpening signature at 0.5; the headroom this leaves
is the case for the 1.0 comparison in §5.

**(c) Flicker.** Temporal variance of Rec.709 luma over the four frames of
each burst, `analyze_iteration08_taa`'s classes, resolved and presented
against the raw frames (`presented.bursts[].flicker`):

| burst | 0.01 px gate | class | pixels | resolved/raw | presented/raw | presented/resolved |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| **1380–1383** | **met** (peak 0.0068 px) | sentinel | 848,111 | 0.0528 | 0.0564 | 1.068 |
| | | routed interior | 71,519 | 0.0564 | 0.0604 | **1.070** |
| | | routed edge | 63,410 | 0.0293 | 0.0314 | 1.071 |
| | | thin feature (overlay) | 37,842 | 0.0392 | 0.0416 | 1.061 |
| 2717–2720 | not met (0.27 px) | routed interior | 66,811 | 0.4285 | 0.4429 | 1.034 |
| 3856–3859 | not met (0.81 px) | routed interior | 63,447 | 0.4623 | 0.4838 | 1.047 |
| 9536–9539 | not met (2.77 px) | routed interior | 118,248 | 0.5258 | 0.5472 | 1.041 |

Whole-image presented/resolved variance energy: **1.070, 1.036, 1.038,
1.033**. The sharpen adds 3–7 % temporal variance to the resolve's output,
which is what amplifying the gradients by 4–7 % predicts (variance goes with
the square of the amplitude, 1.04² ≈ 1.08), and it adds it uniformly across
the classes (1.06–1.07 on burst 1380 for sentinel, interior, edge and thin
features alike) — no class flickers disproportionately, which is the
over-sharpening signature this check looks for. Against the iteration-10
stationary baseline (run 6, burst 4754–4757, the only burst of that run that
met the gate: sentinel **0.0152**, routed interior **0.0097**, routed edge
**0.0089**, thin 0.0096 — `iteration-10-run6-flicker.json`; the file the
hand-off named, `iteration-10-run2-flicker-baseline.json`, has
`flicker: unavailable`, run 2 having no gated burst), run 10's only gated
burst reads 3.5–5.8× higher *before* the sharpen (resolved/raw 0.029–0.056).
That gap is in the resolve's own ratio, not in the sharpen (which adds 7 %
of it), and it is confounded: different scene, a 0.0068 px peak against run
6's exact 0.0, and the −0.5 mip bias, which raises the raw frames' variance
denominator as much as the resolved numerator. Not established as a
regression; the clean comparison is a run with the bias off on the same
scene, which §5 does not ask for. The `taa_image` signal of §4.2 (0.152 of
frame 9539's pixels moved by more than one code from the pre-resolve colour)
remains a reported, unjudged number.

---

## 3. Cost of the sampler-state traffic

### 3.1 How much traffic

Separating the 24 capture frames (whose per-draw diagnostics restore the bias
before every draw) from the 157 ordinary routed records:

| | ordinary frames (n=157) | capture frames (n=24) |
| --- | ---: | ---: |
| `mip_bias_sets` | 18,256 | 25,070 |
| `mip_bias_restores` | 18,250 | 25,069 |
| biased routed draws | 28,784 | 5,841 |
| sets / routed draw | **0.634** | 4.292 |
| **(sets + restores) / routed draw** | **1.268** | 8.584 |
| `SetSamplerState` calls / frame | **232.5** | 2,089.1 |

1.268 per routed draw is the ≈1.25 the question predicts. Note that a "set" is
**one `SetSamplerState` call on one stage** (`motion_output.cpp:526`), not one
per draw.

### 3.2 The cheap policy is already the policy

From the 7,591 `motion_route` records of the six bursts, the draw stream's
interleaving:

| | value |
| --- | ---: |
| draws in the 24 capture frames | 7,591 |
| routed | 5,841 |
| **maximal runs of consecutive routed draws** | **624** (26.0 per frame) |
| mean routed draws per run | **9.361** |

A capture frame re-sets on *every* routed draw, so its 4.292 sets/draw is the
**mean number of biased stages per routed draw**. The run-boundary lower bound
for an ordinary frame is therefore `runs × 4.292`; the measured 0.634 sets/draw
is equivalent to one set-group every **6.77 routed draws** against the **9.361**
that the interleaving strictly requires — a factor of **1.38**, the residual
being stages whose bound texture or mip filter changes *between* consecutive
routed draws, which re-saving cannot avoid without leaving a stale bias.

The source says the same thing outright (`apply_mip_bias`,
`src/proxy/motion_output.cpp:503`):

```c
if (eligible == s.biased) { any = any || s.biased; continue; }   // consecutive routed draws find it set
```

and `restore_mip_bias()` is called only from the shared restore point
(`motion_output.cpp:375`, `if (sampler_biased_mask_) restore_mip_bias();`).
**The proposed "leave it set across consecutive routed draws, restore only when
an unrouted draw actually follows" policy is what the build already does**, and
the remaining 38 % of calls is rebinding, not policy.

### 3.3 Does it cost anything measurable?

Draw-binned frame time with the iteration-11 surrogate (115 of run 10's windows
injected; no `draw_backend`, `per_draw=0` again):

| comparison | bins | draws span | µs/draw | origin µs/draw | fixed ms | sign |
| --- | --- | --- | --- | --- | --- | --- |
| run 10 − run 9 (sharpen + bias added) | 11 | 81–931 | **+1.599** | −0.081 | −1.22 | **p = 1.00000** |
| run 10 − run 5 (route cost now) | 8 | 68–759 | +14.540 | +7.613 | −1.55 | 8+/0−, p = 0.0078 |
| run 9 − run 5 (route cost, iteration 11) | 10 | 73–774 | +14.554 | +12.074 | −0.99 | 10+/0−, p = 0.0020 |

| | run 10 | run 9 |
| --- | --- | --- |
| fast-regime frame | **10.44 ms at 275.5 draws/f** | 12.10 ms at 275.0 draws/f |
| attributed route cost | **11.453 µs/draw** | 11.887 µs/draw |
| `fill_us` median | **45.5 µs/f** | 63.6 µs/f |
| `taa_run_us` median | **372.6 µs/f** | 379.1 µs/f |
| `taa_draw_us` median | 337.9 | 338.1 |
| `taa_copy_back_us` median | **0.0** | 0.9 |
| route exclusive (frame records) | 45.1 µs/f | 63.5 µs/f |

**The added work does not appear.** The paired-bin slope between the two route-on
runs is +1.599 µs/draw with a two-sided sign test of **p = 1.00** (as
indistinguishable as the test can report), the origin-constrained estimate is
*negative* (−0.081), and run 10's fast-regime frame is **1.66 ms lower** than run
9's at the same draw count. Upper bound from that slope: 1.599 µs/draw ×
275.5 = **0.44 ms/frame**, which spread over 232.5 `SetSamplerState` calls would
be 1.9 µs/call — implausible for the call, and contradicted by the sign test, so
read 0.44 ms/frame as a **noise ceiling**, not a cost. The sharpen shows the
fixture's result reproduced in game: `taa_copy_back_us` fell from 0.9 to 0.0
µs/frame (the RCAS draw *replaced* the copy-back) while `taa_run_us` fell
slightly, i.e. **the sharpen is free on the 8-bit route at 1280×768**, as
[taa-sharpen.md](taa-sharpen.md)'s bench predicted (−0.014 ms at 5120×1440).

**Conclusion (question 3).** The bias costs **232.5 `SetSamplerState` calls per
frame (1.268 per routed draw)**, and that is already the coalesced minimum
within 1.38× of the draw stream's interleaving — the cheaper policy the question
proposes is the policy in force, so there is nothing to win there. The cost is
below this measurement's resolution: ceiling 0.44 ms/frame, sign test p = 1.00,
and the fast-regime frame moved the *other* way (10.44 vs 12.10 ms). No policy
change is warranted; if the ceiling ever needs tightening, the lever is
`X3M_TELEMETRY_DRAW=1` on one run, not a new policy.

---

## 4. TAA health and the loads

### 4.1 Health (`analyze_iteration09.py`)

| | run 10 | run 9 (iteration 11) |
| --- | --- | --- |
| frame records / latched / resolves | 194 / 182 / **181** | 180 / 163 / 162 |
| attempted = resolved | **181 = 181** | 162 = 162 |
| resolves with history | **180 of 181** | 162 of 162 |
| history match (`matched`/`routed`) | **34,563 / 34,625 = 99.82 %** | 33,747 / 33,805 = 99.83 % |
| `scene_end_check` | **Agree 181, None 13, Disagree 0** | Agree 162, None 18, Disagree 0 |
| `scene_end_source` / `resolved_by_source` | hook 181 / **hook 181 of 181** | hook 162 / 162 |
| `draws_after_hook` | 0 on all 194 | 0 on all 180 |
| `apply_failures` / `restore_failures` | **0 / 0** | 0 / 0 |
| `rs_resyncs` / `sb_resyncs` | **2 / 2** (1 each on two frames) | 0 / 0 |
| `rs_queries` / `rs_hits` | 316,126 / 316,123 (**0.999991**) | 307,334 / 307,334 (1.000000) |
| gate rejections | gate2 12,116, gate3 1,883, gate4 9,883, gate6 62, gate1/gate5 **0** | gate2 15,077, gate3 1,312, gate4 9,297, gate6 58 |
| cut detector | **1 event: frame 7500, `cut_median_px` 254.4** | 0 flagged |
| camera records / valid / cuts | 59 / 55 / **0** | 43 / 38 / 0 |
| camera rotation | max **2.7408°**, floor 0.9222–1.6169° | max 1.6258°, floor 0.7219–1.6258° |
| `frame_normal` | 127 windows, 1 stall, fast 119 median **10.74 ms**, slow 7 median 295.9 ms | — |
| line kinds | **86**, none missing, no failure kind | 83 |

Two differences from run 3 worth naming, both small: **two state-shadow resyncs**
(run 3 had none; 3 shadow misses in 316,126 queries) and **one flagged cut** at
frame 7500 with a 254 px median displacement — a real scene transition, well
outside every burst, and the camera cut detector still reports 0. The single
resolve without history is the first resolve of the session. Camera rotation
reached **2.74°**, above run 3's 1.63° and — for the first time on this bottle —
**above the orthonormality noise floor (max 1.617°)**, so the two turning bursts
carry genuine rotation.

Sampler state on routed draws (unchanged from run 3's baseline): stage 0 is
`ANISOTROPIC/LINEAR/LINEAR, MAXANISOTROPY 16` on all 5,804 routed-and-matched
capture draws and on the 1,448 gate-4 rejects — the bias is being applied over
16× anisotropic minification of DXT chains, which the fixture explicitly does
*not* represent.

### 4.2 Readback checks — the certification run 3 could not do

`analyze_motion_readback.py`, 24 frames. The 4-frame bursts and `--taa-debug`
restore every check iteration 11 lost:

| check | run 10 | run 9 (iteration 11) | run 6 (iteration 10) |
| --- | --- | --- | --- |
| `readback_integrity` | **pass**, 24 frames, **24 clean** | fail (benign) | pass, 36/36 |
| `counter_consistency` | **pass**, 24 frames | pass | pass |
| `row_consistency` | **pass** — 756,468 sampled px, **0 unexplained**, max error **0.0547 px** (tol 0.5) | unavailable | pass, max 0.0591 px |
| `temporal_coverage` | **pass** — worst covered fraction **0.9809** (min 0.90) | unavailable | pass, 0.9787 |
| `history_pairing` | **pass** — 4,386 predictable draws, **0 disagreements** | unavailable | pass, 9,224 / 0 |
| `displacement` | **pass** — 3,071,517 valid px, **0 suspicious**, max **50.93 px** (bound 64) | pass (max 4.80 px) | **fail** (3.79 % suspicious, max 265 px) |
| `depth_image_integrity` | **pass** — `valid_motion_without_depth` **0**, no non-finite or out-of-range value | pass | pass |
| `taa_image` | **pass** (finite; signal only) | unavailable | pass |
| `static_consistency` | unavailable (needs a static burst) | unavailable | unavailable |
| `depth` (previous-depth agreement) | **fail** — within-fraction **0.340** against the 0.99 criterion, max error 0.153, tol 1e-4, nearest sampling | unavailable | fail, 0.335 |

**The per-pixel motion output is certified for this build**, which is precisely
what iteration 11 flagged as its one real regression: row-pair consistency exact
to 0.055 px over 756,468 sampled pixels, temporal coverage 98.1 %, history
pairing 0 disagreements over 4,386 predictable draws, and **`displacement` now
passes** where run 6 failed it (0 suspicious pixels against 3.79 %, at a
genuinely larger maximum displacement of 50.9 px). The `depth` check still
fails its 0.99 within-fraction, at **0.340 — the best value recorded**
(iteration 10 run 6 0.335, run 1 0.287, run 2 0.246): **pre-existing, unchanged
by this build**, and documented in [iteration-10.md](iteration-10.md) as the
nearest-sampling previous-depth comparison, not a motion defect.

### 4.3 Loads and zlib

| load | run 10 | run 9 (iteration 11) | run 8 |
| --- | --- | --- | --- |
| startup → menu | **8.584 s** (ends frame 25 at 14.3 s) | 8.386 s | see `loading-x3-run8.md` |
| save load (menu → sector) | **39.167 s** (ends frame 620 at 60.8 s) | 35.707 s | " |
| sector change | **5.725 s** (ends frame 7658 at 158.8 s) | 5.944 s | " |
| return to menu / exit | **6.920 s** (ends frame 10160 at 192.0 s) | 7.947 s | " |
| total in gaps | 60.40 s over 4 gaps | — | — |

The savegame's gz buffer reproduces run 3 exactly: **14,461,803 calls
(99.26 % small), 45.76 MB served in 175 real reads** (82,638.9 calls per real
read, 3.164 bytes/call), no amplification, 0 errors. Session zlib: `inflate`
**773,833 calls / 12.303 s** (max 0.752 ms), `gzread` 179 calls / 46.80 MB /
0.197 s, `gzopen` 27 (22 failures — the loader's probes for absent files). Inside
the save-load gap `[21.6, 60.8] s`: **359,874 `inflate` calls, 5.844 s** and 175
`gzread`, 0.188 s — so **6.03 s of the 39.17 s save load is zlib**, the same
6 s of a longer load. The save load is **3.46 s slower than run 3's**; with the
gz and inflate work identical to the tenth of a second, that difference is
outside zlib and matches the run-to-run spread the run-8 analysis describes
rather than anything this build changed. *Not established: whether the 3.46 s is
noise; it needs the run-8 table's spread to judge.*

**Conclusion (question 4).** TAA is as healthy as run 3 on every counter that
can report a fault — **181/181 resolved, 180 with history, hook Agree on all 181,
0 apply/restore failures, 0 disagreements, 0 camera cuts, 86 line kinds with no
failure kind** — with two minor regressions (2 state-shadow resyncs against 0,
3 shadow misses in 316,126 queries) and one legitimate flagged cut at frame
7500. Loads: **menu 8.584 s, save 39.167 s, sector 5.725 s, exit 6.920 s**,
against run 3's 8.386 / 35.707 / 5.944 / 7.947; zlib inside the save load is
6.03 s, unchanged. The per-pixel readback certification is **TODO**.

---

## 5. Report

1. **Sharpen.** Live and clean: `sharpen=0.500`, pass `references=2`,
   **181 of 181 resolves sharpened**, `taa_copy=S_FALSE` on every record (the
   RCAS draw replaced the copy-back), **0 `motion_output_sharpen_failed`**,
   `taa_copy_back_us` 0.9 → **0.0 µs/frame**.
2. **Mip bias.** Live and clean: `-0.5` on all 194 records, **43,326 sets /
   43,319 restores / 0 failures**, applied to **34,625 of 34,625 routed draws**
   over up to seven stages (`mip_bias_stages` `0x0f`–`0x7f`),
   `mip_bias_biased_now=0` always, and the engine's **715** writes of bias 0 on
   its cube stages 3/4 preserved by the restore. Two gaps: no teardown
   `motion_output_mip_bias_summary` line, and all **131,536** capture
   `sampler state=8` reads show `bias=0` by construction (documented limit).
3. **Sharpness.** The **mip bias is visible in the raw frame**: stationary-burst
   raw 10–90 % rise **0.79–0.87 px**, raw MTF50 **0.69–1.39 c/px**, against
   iteration 9 run 2's 0.86–1.83 px / 0.34–0.74 c/px — ≈2× on the
   best-conditioned bursts. The resolved ratio fell as predicted
   (**0.410–0.523** vs 0.505–0.628) and the **resolved** MTF50 (0.28–0.44) is
   unchanged (0.27–0.46): the resolve is no sharper. On two of three stationary
   bursts the resolve now sits **at or above** the ideal 4-phase supersampling
   floor (share of loss 105–116 %; the tight burst 1380 reads 75.8 %). The
   sharpen itself is **not measurable from this run** — the `taa_1_*.rgba16f`
   readback is the **unsharpened resolve** and run 10 predates the
   `present_1_*` readback of review 27. **Modelled** (RCAS of `taa_1_*` at
   gain 0.5, §2.4): 10–14 % of the pixels change by ≤ 10 codes, routed-
   interior gradient energy **×1.04–1.07** (the fixture's 1.063 on game
   content), 0.03–0.20 px of edge rise recovered, **0 of 47.2 M channels**
   outside the 3×3 neighbourhood, halo excursion +0.02–0.06 of the edge
   contrast, strong-edge contrast +1.4–1.8 %, flicker energy **+3–7 %**
   spread evenly across the classes. No over-sharpening signature at 0.5.
4. **Cost.** **232.5 `SetSamplerState` calls/frame = 1.268 per routed draw**
   (capture frames 2,089/frame — diagnostics, not gameplay). The coalescing
   policy the question proposes **is already in force** (source + 0.634 sets/draw
   against a 9.36-draw interleave run length, a factor of 1.38 from rebinding).
   Frame time: **p = 1.00** against run 3 at matched draw counts, ceiling
   **0.44 ms/frame**, fast-regime frame **10.44 ms vs 12.10 ms** — the added work
   is below the measurement's resolution.
5. **Health / loads / certification.** 181/181 resolved, 180 with history, hook
   Agree 181 / Disagree 0, 0 apply/restore failures, 99.82 % history match, 86
   line kinds with no failure kind; **2 state-shadow resyncs** (run 3: 0) and
   **1 flagged cut** (frame 7500, 254 px) are the only new marks. The per-pixel
   certification iteration 11 lost is **back and passing**: `row_consistency`
   0 unexplained of 756,468 px (max 0.055 px), `temporal_coverage` 0.9809,
   `history_pairing` 0/4,386, and `displacement` **passes** (0 suspicious,
   max 50.9 px) where run 6 failed it; `depth` still fails at **0.340**, the
   best recorded, pre-existing. Loads **8.584 / 39.167 / 5.725 / 6.920 s**;
   save-load zlib 6.03 s.
6. **Next A/B — provisional.** With the sharpen measurement still outstanding
   this is the recommendation the evidence already supports: **hold mip bias at
   −0.5** (it is applied everywhere, costs nothing measurable, and the fixture's
   LOD linearity is verified) and compare **sharpen 0.5 against 1.0** in
   side-by-side screenshots of a *stationary* scene, since the fixture puts 0.5
   at only 1.063× gradient energy and 1.0 at 1.147× — 0.5 may simply be too
   small to see against a 0.51–0.63 resolve loss. If 1.0 reads as over-sharpened
   on real content, the second pair is **bias −0.5 vs −1.0 at sharpen 1.0**,
   because the bias attacks the same softness at the sampler with no per-frame
   cost. Revisit once section 2 has numbers.

---

## Completed after the hand-off

| item | done | where |
| --- | --- | --- |
| **§2.4** over-sharpening | `rcas_rows`, the runner's double reference row-vectorised (struct `'e'` decodes the FP16 files natively; ~40 s per 1280×768 frame in pure Python), gain `rcas_gain(0.5) = 0.5`; 3×3 escapes, change against the unsharpened codes, §2.2 metrics, ESF excursion, strong-edge contrast | `analyze_iteration12.py`, `iteration-12.json: presented` |
| **§2.4** flicker | class ratios of the presented and resolved images against raw on the four modelled bursts, against `iteration-10-run6-flicker.json` (the run-2 file has no gated burst) | same |
| **§2.1** burst 9536–9539 | `burst_frames(readback_frames=…)`: grouped over the frames with `motion_output_readback` lines | `analyze_iteration09_run2.py`, `test_iteration09_run2.py` |
| `analyze_iteration09_run2.py --text` | `render_mesh_cache` prints the section without a per-call mean when the cache logged no calls | same |
| like-for-like re-run | not needed: the it09r2 column is the tracked `iteration-09-run2-summary.json` from the unmodified tool; the grouping fix changes nothing for a run whose bursts are not followed by a periodic record | — |
| `tools/analysis/analyze_iteration12.py` + `verification/analysis/test_iteration12.py` | written; the JSON/TXT re-emitted with the numbers of the one-off code (the only differences: burst 9536 evaluated, and additional keys — `sharpened_resolved_frames`, `routed_draws`, `bursts`, `presented`, `announcement`, `open`) | `verification/results/iteration-12.json`/`.txt`; 786 analysis tests OK |
| the measurement gap | `present_<device>_<frame>.bgra8` readback after the sharpen draw / copy-back and after the HDR write-back, fixture-verified | [taa-sharpen.md](taa-sharpen.md), [capture-format.md](../architecture/capture-format.md) |

### Tracked artefacts written

| path | source |
| --- | --- |
| `docs/verification/iteration-12.md` | this document |
| `verification/results/iteration-12.json` / `.txt` | `analyze_iteration12.py` (the command above): §1, §2.1–2.4, §3.1–3.2, §4.2 |
| `tools/analysis/analyze_iteration12.py`, `verification/analysis/test_iteration12.py` | the tool and its tests |
| `verification/results/iteration-12-run10-health.json` / `.txt` | `analyze_iteration09.py`, unmodified |
| `verification/results/iteration-12-cost.txt` | `analyze_iteration11.py`, unmodified |

Untracked, in the scratchpad
(`/private/tmp/claude-501/-Users-asvetl-x3-mod/8a545eeb-48d6-46e4-ab43-b08234469167/scratchpad/it12/`):
`run10-taa.json` (382 KB, the full blur report), `run10-it11cost.json` (87 KB),
`rb/motion-readback-iteration12-summary.json` (3.8 MB) and `.txt` (900 KB), and
the per-kind line extracts.

Two pre-existing tool defects were hit again and are **fixed** in this
checkpoint: `analyze_iteration09_run2.py --text` raised
`TypeError: unsupported format string passed to NoneType.__format__` in
`render_text` on a run without `mesh_cache` metrics (iterations 10, 11, 12;
`mesh_cache_report` had `native_seconds = 0.0` with `mean_native_ms = None`
when the frequency was known and no call was logged — now `render_mesh_cache`
prints "no calls"), and `burst_frames(gap=1)` merged a readback-less
`frame_log` record into an adjacent burst (§2.1). The re-run of the tool on
run 10 with the fixes wrote the text report and evaluated all six bursts.

Production source changed only by the review-27 present readback
(`motion_output.cpp`: `resolve` and `hdr_writeback`, capture frames with
`--taa-debug` only) and a forward declaration of `ID3DXMesh` in
`loading_trace.h` that the `b10d129` checkpoint needed to compile.
