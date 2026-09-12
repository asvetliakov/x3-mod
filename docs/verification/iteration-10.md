# Iteration 10: the new "X3" bottle (arm64 Wine + FEX) against the old one

Build `1d36c29`, unchanged. The only variable is the bottle: a new CrossOver
bottle **X3** (arm64 Wine + FEX, `FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`)
against the iteration-9 runs in the old Steam bottle (x86-64 Wine under
Rosetta). Same save family, same reported adapter
(`NVIDIA GeForce 8800 GTX`, `nvd3dum.dll`, `vendor=000010de device=00000191`),
same `C:\windows\system32\d3d9.dll` backend.

**Verdict, three sentences.** The proxy is healthy on FEX: no new warning or
error line kind, zero route apply/restore failures, zero resets, 214 of 214
resolves on every frame that latched a scene, and a 99.16 % history match rate
inside the old spread (99.42 % / 97.84 %). There is **no evidence that reduced
x87 precision moved any engine value the route consumes** — every residual we
measure (view-matrix orthonormality, the projection recovered from the uploaded
draw constants, the per-pixel motion against the analytic rigid displacement) is
**equal to or smaller than** on Rosetta. The bottle is **2.7–4.1× faster with
the route off** and **1.6–2.5× faster with it on**, which is the user's
"feels faster" — but because the route's own per-draw cost barely changed while
everything else got ~3× cheaper, the route went from 12–26 % of the frame to
**31 % by its own spans** and now owns the entire draw-count slope.

## Provenance

| | run 5 (route off) | run 6 (route on) |
| --- | --- | --- |
| log | `/tmp/x3-bottleX3-run5/session-20260912-170743-352.log` | `/tmp/x3-bottleX3-run6/session-20260912-171241-212.log` |
| bytes / lines | 24,838,333 / 490,146 | 295,274,646 / 5,334,487 |
| sha256 | `8105f3364968bbb3295d5377d3e4a4efb17716204366ccca08b6d557e0e108d3` | `c0fe801ccff79874c5be6c0e9386c8430a235eb618f676071989fbbee6963e3d` |
| flags | `--direct --telemetry` | `--ownership --object-trace --object-lifetime --motion-output --taa --taa-debug --telemetry --profile --scene-hook` |
| route | `requested=0` | `requested=1 taa=1 taa_debug=1 jitter=1 rt_mode=perdraw scene_hook=1 hdr=0` |
| readbacks | 1 capture frame | 9 bursts × 4 frames (color/taa/depth/motion) |

Old-bottle baselines, all build `1d36c29`, from
[iteration-09.md](iteration-09.md), [iteration-09-run2.md](iteration-09-run2.md)
and [iteration-09-cost.md](iteration-09-cost.md): run 1
(`session-20260912-160404-1632.log`, route on, no scene hook), run 2
(`session-20260912-162050-2040.log`, route on + `taa_debug` + scene hook), run 3
(`session-20260912-164116-2548.log`, route off). Logs and readbacks stay
untracked.

**Path caveat, stated once and applied everywhere below.** Run 6 loaded a
*different* save, went back to the menu, started a new game in the usual sector,
docked at the usual station and then flew the usual route with a sector change.
Only the station-onward segment is comparable with run 5 and with the old bottle.
Every cross-bottle number in §2 is therefore either **binned by draws/frame**
(paired medians of the same 50-draw bin) or a **per-call metric median**, both of
which are path-insensitive; pooled means are reported only as occupancy.

## Reproduction

```sh
S=/tmp/x3-bottleX3-run5/session-20260912-170743-352.log   # run 5
T=/tmp/x3-bottleX3-run6/session-20260912-171241-212.log   # run 6
A=/tmp/x3-iteration09-run1/session-20260912-160404-1632.log
B=/tmp/x3-iteration09-run2/session-20260912-162050-2040.log
C=/tmp/x3-iteration09-run3/session-20260912-164116-2548.log
R=verification/results

# frame time: route cost on the new bottle, then each bottle pair
python3 tools/analysis/analyze_iteration09_cost.py --run run6=$T --run run5=$S \
    --run run1=$A --run run2=$B --run run3=$C --baseline run5 \
    --json $R/iteration-10-cost.json --text $R/iteration-10-cost.txt
python3 tools/analysis/analyze_iteration09_cost.py --run run5=$S --run run3=$C \
    --baseline run3 --json $R/iteration-10-bottle-route-off.json \
    --text $R/iteration-10-bottle-route-off.txt
python3 tools/analysis/analyze_iteration09_cost.py --run run6=$T --run run2=$B \
    --baseline run2 --json $R/iteration-10-bottle-route-on.json \
    --text $R/iteration-10-bottle-route-on.txt

# health, camera, scene hook + blur, flicker, readbacks
python3 tools/analysis/analyze_iteration09.py $T --no-camera-detail \
    --output $R/iteration-10-run6-health.json --text $R/iteration-10-run6-health.txt
python3 tools/analysis/analyze_iteration09.py $S --no-camera-detail \
    --output $R/iteration-10-run5-health.json
python3 tools/analysis/analyze_camera_state.py $T \
    --metadata $R/shader-registers.json --output $R/iteration-10-run6-camera.json
python3 tools/analysis/analyze_iteration09_run2.py $T --captures /tmp/x3-bottleX3-run6 \
    --baseline-log $B --baseline-label run2 --ideal-frames 1 \
    --output $R/iteration-10-run6-taa.json --text /tmp/it10-taa.txt   # text render
    # raises on a run without mesh_cache lines; the JSON is written first
python3 tools/analysis/analyze_iteration08_taa.py $T --captures /tmp/x3-bottleX3-run6 \
    --output $R/iteration-10-run6-flicker.json --text $R/iteration-10-run6-flicker.txt
python3 tools/analysis/analyze_iteration08_taa.py $B --captures /tmp/x3-iteration09-run2 \
    --output $R/iteration-10-run2-flicker-baseline.json --text /tmp/it10-run2-flicker.txt
python3 tools/analysis/analyze_motion_readback.py $T --readback-dir /tmp/x3-bottleX3-run6 \
    --label iteration10 --results-dir /tmp/it10 --jitter-from-log --no-draw-details
mv /tmp/it10/motion-readback-iteration10-summary.json $R/iteration-10-readback-summary.json

# the consolidated report (this document's tables)
python3 tools/analysis/analyze_iteration10.py \
    --run run6=$T --run run5=$S --run run1=$A --run run2=$B --run run3=$C \
    --new-run run6 --new-run-peer run5 \
    --it09 run6=$R/iteration-10-run6-health.json --it09 run5=$R/iteration-10-run5-health.json \
    --it09 run1=<run1 health json> --it09 run2=<run2 health json> \
    --camera run6=$R/iteration-10-run6-camera.json --camera run1=<...> --camera run2=<...> \
    --readback run6=$R/iteration-10-readback-summary.json \
    --readback run1=$R/motion-readback-iteration09-summary.json --readback run2=<...> \
    --cost $R/iteration-10-cost.json --speedup run6:run5 \
    --metric-pair run6:run2 --metric-pair run5:run1 \
    --taa $R/iteration-10-run6-taa.json --taa-baseline $R/iteration-09-run2-summary.json \
    --output $R/iteration-10.json --text $R/iteration-10.txt

python3 -m unittest verification/analysis/test_iteration10.py    # 26 cases
```

`analyze_iteration10.py` derives only what no existing tool produces — the log
line-kind diff, the per-call telemetry metric medians, the precision table, the
paired-bin speed-up factors, the stationary-burst ideal-supersampling floor (the
table [iteration-09-run2.md §4](iteration-09-run2.md) tabulated by hand; the
paired test reproduces its published `0.5675 / 0.890 / 87.4 %` and
`0.6556 / 0.958 / 92.5 %` rows from the tracked run-2 summary) and the per-run
health table. Everything else is imported from the iteration-9 tools.

---

## 1. FEX health of the proxy

### 1.1 Line kinds: exactly one new kind, and it is path, not emulator

Run 6 emits **92** distinct line kinds; the union of the three old-bottle runs is
**94**. The diff is complete:

| | |
| --- | --- |
| new in run 6 | **`motion_output_scene_hook_disagreement` × 16** (§1.4) |
| absent from run 6 | `mesh_cache_metric`, `mesh_cache_bypass`, `mesh_cache_fp_first` — run 6 did not pass `--mesh-cache`; configuration, not behaviour |

No new warning or error kind. A direct scan of every failure-bearing field finds
the same two populations as the old bottle and nothing else:
`container_result=80004002` (`GetContainer` returning `E_NOTIMPL` on the
back-buffer surface, 32,596 in run 6 and 24,464 in run 2 — proportional to the
frame count), and `buffer_content result=80070057` in the *route-off* runs only
(3,242 in run 5, 1,272 in run 3 — the same capture-path `E_INVALIDARG`, both
bottles). `taa_result=00000001` × 64 and `fill_result=00000001` × 40 are the skip
frames of §1.3. The single `failure=1` match is the substring
`restart_on_cleanup_failure=1` in the `mesh_cache` config line, present in every
run.

### 1.2 Object trace, lifetime and the route counters

`object_trace active=1 status=active recovery_required=0`,
`object_lifetime active=1 status=active_without_baseline` — unchanged since
iteration 5, no recovery required, no degradation on FEX.

| | run 1 (old) | run 2 (old) | **run 6 (FEX)** |
| --- | ---: | ---: | ---: |
| frame records | 128 | 94 | **278** |
| draws / routed / matched | 48,949 / 25,669 / 25,520 | 33,285 / 22,134 / 21,656 | **93,278 / 47,213 / 46,818** |
| **history match rate** | **99.42 %** | **97.84 %** | **99.16 %** |
| gates 1/2/3/4/5/6 | 0 / 15,231 / 1,483 / 6,566 / 0 / 149 | 0 / 4,616 / 1,025 / 5,510 / 0 / 478 | 0 / 30,902 / 1,779 / 13,384 / 0 / **395** |
| `apply_failures` / `restore_failures` | 0 / 0 | 0 / 0 | **0 / 0** |
| `motion_output_reset` / `device_reset` | 0 | 0 | **0** |

99.16 % sits inside the old-bottle spread (97.8–99.4 %). Gate 5 (`Scope`) still
never fires. No apply or restore failure, no reset event: run 6, like the old
runs, ended without a clean shutdown, so no release/destroy records either.

### 1.3 TAA resolves and skips

| | run 1 | run 2 | **run 6** |
| --- | ---: | ---: | ---: |
| `taa_attempted` = `taa_resolved` | 108 | 89 | **214** |
| `taa_result=00000000` on those | 108 | 89 | **214** |
| `taa_history=1` | 105 | 83 | **203** |
| resolved without history (cut frames) | 3 | 6 | **11** |
| `taa_skip=2` (selector never reached the copy) | 20 | 5 | **64** |
| `taa_skip` 3 / 4 / 6 / 9 | 0 | 0 | **0** |

**Every one of the 64 skips is a frame with nothing to resolve.** 40 of them are
`scene_end_check=None`, unlatched, at frames 0–780 (start → menu → first save
load), 1,500–2,880 (back to the menu → new game load) and 14,460–14,520 (the
final menu); the other 24 are the transition screen of §1.4. Every frame that
latched a scene *and* routed draws resolved: 214 of 214.

### 1.4 The scene-end hook: 214 agree, 24 disagree on a screen with no draws

`scene_end_check` distribution over the 278 records: **Agree 214, Disagree 24,
None 40**; `scene_end_source` **hook 214, none 64**; `resolved_by_source`
**{hook: 214}**; `draws_after_hook` max **0**.

The 24 Disagree records are the logged frames **2,940 → 4,320** (every 60th) and
are identical to each other:

```
latched=1  draws=8-9  routed=0  hook_signals=1  hook_outside_scene=1
hook_state=CameraState  draws_after_hook=0  bloom_copy_seen=0  taa_skip=2
```

plus 16 `motion_output_scene_hook_disagreement` records on the *consecutive*
frames 2,933–2,948. That frame range is the new-game transition screen that only
run 6's path visits — 8–9 draws per frame, a latched but empty scene, nothing
routed, nothing resolved, no draw after the hook. **The hook remains safe as the
default resolve point:** it carried every one of the 214 resolves and disagreed
only where there was nothing to resolve. What is new is that the Disagree branch
is now *observed*, and its cause is recorded here.

### 1.5 State-shadow resyncs: 24, on exactly those frames

| | run 1 | run 2 | **run 6** |
| --- | ---: | ---: | ---: |
| `rs_queries` / `rs_hits` | 231,950 / 231,950 | 199,484 / 199,481 | **431,026 / 431,026** |
| `rs_gets` | 1,512 | 1,249 | **3,332** |
| **`rs_resyncs`** | 0 | 0 | **24** |

The first non-zero resync count in the project. It is not a rate: the per-frame
field is `rs_resyncs=1` on each of the frames 2,940, 3,000, … 4,320 and `0`
everywhere else — the same 24 frames as the hook disagreement, the same
transition screen. Shadow hit rate is otherwise perfect (431,026 / 431,026,
better than run 2's three misses).

### 1.6 Camera state and the draw-constant cross-check

| | run 1 | run 2 | **run 6** |
| --- | ---: | ---: | ---: |
| `camera_state` records / valid | 42 / 38 | 38 / 37 | **85 / 77** |
| policy 1 / 2 (frame records) | 20 / 108 | 5 / 89 | **64 / 214** |
| camera cuts | 0 | 0 | **0** |
| routed frames without a camera | 0 | 0 | **0** |
| invalid states | frames 0, 300, 600, 900 | frame 0 | frames 0, 300, 600, 1500, 1800, 2100, 2400, 2700 |
| rotation °, max / median | 6.23 / 0.53 | 5.81 / 0.81 | **8.73 / 1.02** |
| draw constants agreeing | 7,841 / 7,881 = **99.49 %** | 11,419 / 11,499 = **99.30 %** | **15,218 / 15,258 = 99.74 %** |
| `object_matrix role=view` rows | 7,989 / 8,313 = 96.1 % | 11,572 / 11,992 = 96.5 % | **15,362 / 15,938 = 96.4 %** |

The invalid states are all menu/loading frames (the camera path is not active);
the 40 disagreeing draws are the same post-scene HUD/overlay class identified in
[iteration-09.md §2.4](iteration-09.md) — an identity-like view, hence a rotation
deviation near 2.0, while the recovered projection still matches. Run 6's
agreement fraction is the **highest of the three**.

### 1.7 Did FEX's reduced x87 precision move anything we depend on? No.

`FEX_X87REDUCEDPRECISION=1` computes 80-bit x87 operations at 64-bit: a relative
error near 1e-16. It would show up as a **widened residual**, never as a changed
value. Every residual we can measure got **smaller or stayed equal**:

| residual | run 1 | run 2 | **run 6 (FEX)** |
| --- | ---: | ---: | ---: |
| view-matrix row-norm deviation, max (orthonormality) | 2.254e-4 | 2.254e-4 | **1.810e-4** |
| projection deviation in the draw-constant cross-check, max | 3.573e-4 | 1.278e-4 | **1.086e-4** |
| per-capture-frame rotation / translation deviation, routed scene draws | ≤ 1.2e-7 / ≤ 8.0e-7 | same order | **7.19e-8 / 2.5–2.9e-7** |
| motion readback row-pair consistency, max error | 0.0676 px | 0.0618 px | **0.0591 px** |
| motion readback unexplained pixels | 0 | 0 | **0 of 1,195,394** |
| depth cross-check, max error / worst within-fraction | 0.0943 / 0.2870 | 0.2246 / 0.2462 | **0.1537 / 0.3346** |

* **Projection `p00`/`p11`.** Run 6 carries two distinct projections:
  `p00=0.8 p11=1.333333` on 72 states (the flight camera, bit-identical to run 1
  and run 2) and `p00=0.8000767 p11=1.333461` on **5 states, frames 3,000–4,200**
  — inside the same transition screen as §1.4/§1.5. That is a 9.6e-5 relative
  difference, **eleven orders of magnitude above** anything an 80→64-bit x87
  reduction can produce, and it is a self-consistent matrix (aspect 1.6666665
  against 1.6666663, FOV 102.675° against 102.680°). It is a different engine
  camera on a screen the old runs never visited, not rounding.
* **Position quantization in the motion readbacks.** The row-pair consistency
  check is the direct test: it predicts each pixel's motion vector from the
  analytic rigid displacement of its draw and compares. 1,195,394 sampled pixels
  over 27 frame pairs, **0 unexplained**, max error **0.0591 px** — the tightest
  of the three runs.
* **Depth.** The depth cross-check still fails its 0.99 within-fraction
  criterion, as it did on both old runs; run 6's numbers land *between* run 1 and
  run 2 (max error 0.15 against 0.09 and 0.22), i.e. inside the existing spread.

**Conclusion: no sign that reduced x87 precision reached any engine-side value
the route consumes.** Nothing here needs `FEX_X87REDUCEDPRECISION=0` to be
retested, though a confirmation run with it off would be cheap if the user wants
belt and braces.

---

## 2. Frame time: the bottle is 2.7–4.1× (route off) and 1.6–2.5× (route on)

CPU-side wall clock only; `frame_normal` is Present-to-Present and contains
application work, pacing and driver blocking. No GPU timing anywhere.

### 2.1 Occupancy (pooled, path-dependent — context only)

| run | bottle | route | regime | windows | mean | min | draws/f | fps |
| --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: |
| 3 | old | off | fast | 90 | 24.05 ms | 21.69 | 229 | 42 |
| **5** | **new** | **off** | fast | 83 | **8.42 ms** | 5.67 | 320 | **119** |
| 1 | old | on | fast | 124 | 33.08 ms | 30.67 | 270 | 30 |
| 2 | old | on | fast | 81 | 34.24 ms | 30.82 | 227 | 29 |
| **6** | **new** | **on** | fast | 201 | **16.91 ms** | 14.58 | 358 | **59** |
| 3 / 5 | | off | menu | 10 / 8 | 34.97 / **9.90 ms** | | 653 / 644 | |
| 2 / 6 | | on | menu | 10 / 12 | 39.89 / **12.34 ms** | | 647 / 648 | |

**This is the user's "feels faster": 42 → 119 fps with the route off and
29 → 59 fps with it on, and the menu 35 → 10 ms.**

### 2.2 Draw-count-normalised: paired medians of the same bin

Route off, run 5 against run 3 (`iteration-10-bottle-route-off.txt`): slope
**−46.07 µs/draw** (IQR −55.95 … −39.35), through the origin −56.81, sign test
**0+/9−, p = 0.0039**.

| draws/frame | run 5 (new) | run 3 (old) | factor |
| ---: | ---: | ---: | ---: |
| 68 | 8.33 ms | 12.13 ms | 1.46× |
| 226 | 8.36 | 22.91 | 2.74× |
| 326 | 8.43 | 26.95 | **3.20×** |
| 387 | 8.41 | 32.37 | 3.85× |
| 429 | 8.49 | 32.43 | 3.82× |
| 718 | 11.52 | 47.69 | **4.14×** |
| 766 | 12.57 | 47.89 | 3.81× |

**Route off, the new bottle's frame time is essentially flat in draw count** —
8.31 to 8.83 ms from 68 to 429 draws, 11.5–12.6 ms at 718–766 — while the old
bottle scaled 12 → 48 ms. `present_normal` is 6.3 µs (run 5) against 9.9 µs
(run 3), so this is not a vsync or frame-rate cap: with the route off the frame
is simply no longer bound by per-draw CPU submission.

Route on, run 6 against run 2 (`iteration-10-bottle-route-on.txt`): slope
**−58.86 µs/draw** (IQR −87.17 … −37.03), sign test **0+/14−, p = 0.0001**;
factors 1.60× (67 draws) to 2.51× (469 draws), **median 2.13×** over the 13
clean bins. One bin (682 draws, 12.84 ms, 18 windows) reads 5.57× because menu
frames — ~650 draws at ~12 ms — fall into a high-draw *scene* bin; it inflates
that bin, not the slope.

### 2.3 What the route costs on FEX

Run 6 against run 5 is a **controlled** pair: same bottle, same session series,
route the only difference (`iteration-10-cost.txt`).

| draws/frame | run 6 (route on) | run 5 (route off) | route on / off |
| ---: | ---: | ---: | ---: |
| 64 | 8.35 ms | 8.33 ms | 1.00× |
| 226 | 13.91 | 8.36 | 1.66× |
| 328 | 19.05 | 8.43 | **2.26×** |
| 429 | 23.80 | 8.49 | 2.80× |
| 718 | 35.78 | 11.52 | 3.11× |
| 776 | 40.44 | 12.57 | 3.22× |

Slope **+37.77 µs/draw** (IQR +35.96 … +41.20), sign test 10+/0−, p = 0.0019;
minus the menu overhead of 3.33 µs/draw (profiler, object trace, per-draw stamps
with nothing routed) → **34.44 µs/draw attributed to the route**, against
**14.61 µs/draw** from the route's own spans, leaving **19.83 µs/draw
unexplained**.

The route's own telemetry, per ordinary frame (n = 178, 198 draws, 120 routed):

| | run 1 (old) | run 2 (old) | **run 6 (FEX)** |
| --- | ---: | ---: | ---: |
| route exclusive | 4,968 µs = 17.25 µs/draw | 4,604 µs = 17.37 µs/draw | **3,259 µs = 16.33 µs/draw** |
| per **routed** draw | 24.65 µs | 25.23 µs | **26.82 µs** |
| gate | 3,410 µs (11.61/draw) | 2,938 µs (11.61/draw) | **2,523 µs (12.30/draw)** |
| `route_draw` | 1,338 µs (6.58/routed) | 1,306 µs (6.86/routed) | **613 µs (5.09/routed)** |
| `route_set_rt` | 611 µs (0.773/call) | 590 µs (0.783/call) | **311 µs (0.623/call)** |
| jitter / fill | 136 / 60 µs | 126 / 63 µs | **60 / 56 µs** |
| `taa_run` / `taa_resolve_draw` | 327 / 293 µs | 354 / 313 µs | **304 / 265 µs** |

**Route metric medians, per call** (`iteration-10.txt`; these are path-insensitive):

| metric | run 6 (FEX) | run 2 (old) | ratio | calls (run 6) |
| --- | ---: | ---: | ---: | ---: |
| `route_gate` | **14.5047 µs** | 12.2226 µs | **×1.19** | 4,639,821 |
| `route_draw` | **5.1124 µs** | 7.0018 µs | ×0.73 | 2,089,438 |
| `route_set_rt` | **0.6278 µs** | 0.8167 µs | ×0.77 | 8,357,752 |
| `route_jitter` | 0.1723 µs | 0.2813 µs | ×0.61 | 5,428,058 |
| `route_fill` | 62.3962 µs | 70.6147 µs | ×0.88 | 12,131 |
| `taa_run` | **316.8184 µs** | 374.8121 µs | ×0.85 | 10,697 |
| `taa_resolve_draw` | 277.8115 µs | 329.5639 µs | ×0.84 | 10,697 |
| `draw_backend` | 2.9967 µs | 2.8108 µs | ×1.07 | 4,639,820 |
| `lock_wait` | **0.0946 µs** | 0.2282 µs | **×0.41** | 12,788,234 |
| `frame_normal` | 18.88 ms | 42.97 ms | ×0.44 | 14,476 |

Zero failures on every metric in every run. Three readings worth keeping:

1. **The route is not cheaper on FEX in the way the rest of the frame is.** Its
   own exclusive cost is 16.33 µs/draw against 17.25–17.37 — a 5 % gain, against
   the ~3× the rest of the frame gained. The **gate is 19 % slower per call**
   (14.50 vs 12.22 µs) while `route_draw`, `route_set_rt` and `route_jitter` are
   23–39 % cheaper. The gate is the `ReadProcessMemory`-heavy observer path
   identified in [route-cost-run1.md](route-cost-run1.md), and FEX evidently does
   not help it.
2. **`lock_wait` fell 2.4×** (0.228 → 0.095 µs/call over 12.8 M calls) —
   `WINEMSYNC=1` doing its job. `draw_backend` is 7 % *slower* (3.00 vs 2.81 µs),
   so the D3D9 submission path itself is not where the win comes from; the win is
   in the game's own x86 CPU work.
3. **The route's share of the frame roughly doubled.** Its own spans are
   5.255 ms of a 16.91 ms frame = **31.1 %** (old bottle: 4.08–4.70 ms of
   33–34 ms = 12–14 %), and the frame-time attribution puts 12.45 ms = **73 %**
   of the frame on it. Read the 73 % with care: run 5 is flat in draws, so the
   baseline slope is ~0 and the whole of run 6's slope lands on the route; part
   of the 19.8 µs/draw unexplained gap is per-draw CPU work that was previously
   hidden behind slack the route has now consumed. The honest bracket is
   **14.6 µs/draw by the route's own spans (5.3 ms/frame, 31 %) up to
   34.4 µs/draw attributed (12.5 ms/frame)**, the same shape of uncertainty as
   run 2's on the old bottle (17.97 measured vs 33.65 attributed).

**Diagnostics not charged to the route:** the sampling profiler costs
**0.546 ms/frame = 2.97 % of wall** (run 1: 0.705 ms = 1.93 %; run 2: 1.005 ms =
2.37 %) — its absolute cost fell 46 % while its *share* rose, because the frames
are shorter; 241.3 ticks/s over 32 threads. Telemetry stamps cost
**0.343 ms/frame** (1,905 spans × 0.1801 µs; old bottle 0.524–0.547 ms). Capture
frames cost 36.3 ms each for four readbacks (9.08 ms per readback, against
11.6–11.9 ms on the old bottle).

The lever ranking is unchanged: gate 2,523 µs/frame (save ~1,261), telemetry
stamps 343 µs (save all), lazy RT mode 311 µs (save 124), jitter writes 60 µs,
sentinel fill 56 µs, temporal resolve 304 µs (no saving).

---

## 3. TAA quality on FEX

### 3.1 Readback integrity and the row pairs

36 captured frames, all four readback kinds present
(`iteration-10-readback-summary.json`):

| check | result |
| --- | --- |
| `readback_integrity` | **pass**, 36/36 clean frames, no errors |
| `counter_consistency` | **pass**, 0 failing frames (the per-draw `motion_route` records reproduce the DLL's own counters) |
| `history_pairing` | **pass**, 9,224 predictable draws, **0** disagreements |
| `row_consistency` | **pass**, 27 frame pairs, 1,195,394 sampled px, **0 unexplained**, max **0.0591 px** |
| `temporal_coverage` | **pass**, worst covered fraction 0.9787 |
| `depth_image_integrity` | **pass**, no non-finite or out-of-range depth, `valid_motion_without_depth=0` |
| `taa_image` | **pass**, 36/36 finite FP16 resolves |
| `displacement` | **fail** — 3.79 % of pixels above the 64 px bound (run 1: 4.74 %); pre-existing bound, not a regression |
| `depth` | **fail** — within-fraction 0.335 against the 0.99 criterion (run 1 0.287, run 2 0.246); pre-existing |

Both failures are the classes iteration 9 already carries, and run 6 is the
*better* of the three on each. Nothing in the readbacks is bottle-specific.

### 3.2 Blur on the stationary bursts, against the ideal supersampling floor

Same method as [iteration-09-run2.md §4](iteration-09-run2.md): mean squared
central-difference luma gradient, resolved over raw, on the `routed_interior`
class; the floor is the burst's own four *real* jitter phases averaged with no
resampling, rescaled by the analytic white-source factor from that four-phase
kernel to the resolve's eight-phase geometric kernel.

| burst | interior px | cut peak | measured | ideal floor | measured/ideal | share of loss that is supersampling | iteration-8-comparable (mean) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 4566–4569 | 29,688 | 0.79 px | 0.4170 | 0.4681 | 0.891 | **91.2 %** | 0.558 |
| 4754–4757 | 37,230 | **0.00 px** | 0.5346 | 0.6369 | 0.839 | **78.0 %** | **0.715** |
| 5394–5397 | 34,207 | 0.91 px | 0.4548 | 0.4545 | 1.001 | **100.1 %** | 0.552 |
| 9711–9714 | 79,257 | 0.04 px | 0.5497 | 0.6264 | 0.878 | **83.0 %** | **0.747** |
| 13128–13131 | 22,446 | 0.04 px | 0.7150 | 0.7690 | 0.930 | **81.0 %** | **0.816** |

Run 2's two stationary bursts read 87.4 % and 92.5 %; run 6's five read
**78–100 %**, so the headline conclusion is unchanged: **most of the measured
sharpness loss on a stationary camera is what an ideal eight-phase jitter
supersample of the same content costs**, and on burst 5394 the resolve adds
nothing measurable at all. The analytic 1×1 px box reference is 0.6158,
unchanged.

The iteration-8-comparable column (iteration 7/8's exact mask, operator and band)
is the regression detector, and it needs one caveat. Iteration 8 read 0.64–0.75
and run 2 read 0.64–0.73. Run 6's three well-conditioned bursts read
**0.715, 0.747, 0.816** — at or above that band. The two bursts that read
0.552–0.558 are precisely the two whose camera was *not* fully still (cut medians
0.79 and 0.91 px) and whose raw frames spread most across jitter phases (7.0 %
and 8.8 % on routed interior, against 2.6–4.5 % on the others), which is the
condition [iteration-09-run2.md](iteration-09-run2.md) already flags as making a
ratio unreliable. **No sharpness regression attributable to the bottle.**

### 3.3 Flicker classes

`analyze_iteration08_taa.py` certifies a burst as stationary only when
`motion_output_cut median_px ≤ 0.01` in all four frames. Run 6 has one:
**4,754–4,757**.

| class | pixels | share | raw rms | resolved rms | ratio | flicker energy share | visible (>2 levels) raw → resolved |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| sentinel | 887,770 | 90.3 % | 0.56 | 0.07 | **0.0152** | 0.031 | 1,042 → 155 |
| routed interior | 37,230 | 3.8 % | 16.03 | 1.58 | **0.0097** | 0.677 | 23,243 → 4,503 |
| routed edge | 58,040 | 5.9 % | 8.78 | 0.83 | **0.0089** | 0.292 | 11,478 → 1,796 |
| thin feature (overlay) | 32,723 | 3.3 % | 20.26 | 1.99 | **0.0096** | 0.946 | 21,656 → 6,159 |
| yellow guide lines | 403 union / 123 stable | | 35.44 | 4.10 | 0.013 | | coverage flip 0.69 |

Against [iteration-08.md §5](iteration-08.md), where the same tool measured
interior 0.008 but **routed edge 0.843–0.863** and **sentinel 0.999**, this is a
large improvement on exactly the two classes that used to carry 78–89 % of the
residual flicker. That is the sentinel-reprojection work landed since iteration 8
(commit `9dbd4dd`), not the bottle — the resolve runs on the GPU through the same
D3D9 path in both bottles.

**A strict run-2 comparison is not possible and does not need run 6 re-run.**
`analyze_iteration08_taa.py` reports `flicker: unavailable — no burst selected
(none certified stationary)` for run 2: its stationary bursts carry 0.22–0.30 px
cut medians, above the 0.01 px gate. Fixing that would need a **run-2-path**
capture with the camera fully still on the **old** bottle, which is not something
a repeat of run 6 can supply.

---

## 4. Verdict: run 6 does not need to be repeated

Nothing in §1–§3 is blocked by the path difference.

* The **route-on against route-off** comparison (§2.3) is run 6 against run 5 —
  same bottle, same session series, adjacent in time, route the only difference.
  That is the controlled pair, and it is intact.
* The **bottle** comparisons (§2.2) are paired medians of the same draws/frame
  bin plus per-call metric medians, both path-insensitive, with sign tests at
  p = 0.0039 (9 bins) and p = 0.0001 (14 bins).
* **Health, precision, hook, camera and readbacks** (§1, §3.1, §3.2) are
  per-frame and per-pixel properties; the station-onward flight is covered by the
  214 Agree frames and all nine capture bursts.
* The only measurement the run pair cannot produce is a **run-2-comparable
  flicker ratio** (§3.3), and the missing capture is on the **old** bottle, not
  the new one.

**No repeat run is needed.** If the user wants one more data point, the cheap and
genuinely new one is a route-on run on the new bottle with
`FEX_X87REDUCEDPRECISION=0` to close §1.7 by construction rather than by
residual, or a `--motion-output` run without `--profile` to remove the 0.55 ms
profiler from the 16.91 ms frame.

---

## 5. What this changes for the plan

1. **The route is now the dominant per-draw cost of the frame** (31 % by its own
   spans, and it owns the entire draw-count slope, which route-off no longer
   has). The gate lever — ~21 self `ReadProcessMemory` calls per routed draw in
   the object observers — was worth 1.3 ms/frame out of 33 ms on the old bottle;
   on FEX it is worth 1.3 ms out of 16.9 ms, and the gate is the one route span
   that got *slower* under FEX (×1.19 per call). It should move up the list.
2. **`WINEMSYNC=1` is a real win** (`lock_wait` ×0.41 over 12.8 M calls) and
   costs nothing; keep it in the documented bottle configuration.
3. **`FEX_X87REDUCEDPRECISION=1` shows no measurable effect on anything we
   consume** (§1.7). It can stay on.
4. **The scene-end hook's Disagree branch is now characterised** (§1.4): it fires
   on latched-but-empty transition screens, with `routed=0` and
   `draws_after_hook=0`. It stays safe as the default resolve point; the
   `motion_output_scene_hook_disagreement` record is doing exactly the job it was
   added for.
5. **`rs_resyncs` is no longer always zero** (§1.5) — one per frame on that same
   screen. Worth a look at the shadow's invalidation path on a scene that latches
   with no draws, but it cost nothing measurable and broke nothing.
   *Review 26 follow-up:* the shadow resynchronizes on exactly three paths,
   state-block `Apply`/`EndStateBlock`, `Reset` and a failed restoration of
   the route's own state; those frames show `restore_failures=0` and no
   `motion_output_reset`, so the resync is an application state block per
   frame on that screen (a sprite/font-style save-and-restore), which is the
   correct behaviour (an `Apply` can change every shadowed state). The frame
   line now carries `sb_resyncs` so the next run attributes it directly, and
   the disagreement record has its own 16-line budget (the 16 consecutive
   records here would otherwise have exhausted the failure log's).
