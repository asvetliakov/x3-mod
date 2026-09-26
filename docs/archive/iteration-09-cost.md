# Iteration 9: the route's own cost, measured against a route-off run

[iteration-08.md §6](../verification/iteration-08.md) closed with *"the route's own cost is
still unmeasured"* and named the run that would settle it: the same save with
telemetry only, no `--motion-output`, no observers. The user supplied it on
2026-09-12 as run 3, alongside two route-on runs of the same build (`1d36c29`)
and the same save.

**Verdict.** The live motion route costs **17–18 µs of engine-thread CPU per
submitted draw** by its own telemetry spans — **4.7 ms/frame in the light
regime and 10.7 ms/frame in the heavy one, 14 % and 18 % of the frame** — and
the controlled frame-time difference against the route-off run, once the draw
count and the non-route diagnostics are normalised away, agrees: **17.6 µs/draw
in run 1** (unexplained residual 0.4 µs/draw) and **33.7 µs/draw in run 2**,
which brackets the answer at **18 to 34 µs/draw, 4.8 to 15.8 ms/frame, 14 % to
25 % of the frame**. The temporal resolve inside that is **0.33–0.39 ms/frame,
0.5–1.0 %**: the resolve is cheap, the per-draw route is not. Of the route, the
**gate is 69 %** (11.6–13.1 µs per draw); [route-cost-run1.md](../verification/route-cost-run1.md)
symbolizes that span and finds 92–98 % of it in ~21 self `ReadProcessMemory`
calls per routed draw made by the object observers, which is where the lever is.

## Provenance

| | run 1 | run 2 | run 3 |
| --- | --- | --- | --- |
| log | `session-20260912-160404-1632.log` | `session-20260912-162050-2040.log` | `session-20260912-164116-2548.log` |
| bytes / lines | 154,442,312 / 2,782,766 | 217,449,717 / 3,949,034 | 9,896,772 / 191,112 |
| sha256 | `ca149c414df62aabacbfecfb3ab0fdc586d182c8a23aec918f14293a169e0d47` | `805abac930b13d3c74608a0976c2f0555f9dad3a713fe23bff39b8d82c8ae2ff` | `a45148b7bf1945b0cd5ed0a10f2c374ef0b61783df9a2700820cd75e4164f541` |
| route | `requested=1 taa=1 jitter=1 rt_mode=perdraw` | `requested=1 taa=1 taa_debug=1 jitter=1 rt_mode=perdraw scene_hook=1` | **`requested=0`** |
| observers | `object_trace=active`, `object_lifetime`, `camera_state=active` | same | **all `disabled`** |
| sampling profiler | `enabled=1 interval_us=2000 report_s=5` | same | **absent** |
| mesh cache | `requested=0` | `requested=1` **`enabled=0`** (`await_public_mesh_and_buffer_contract`) | `requested=0` |
| HDR | `hdr=0`, no `hdr_frame`, no `hdr_*` metric | same | same |
| device | `1` | `1` | `1` |

Same sector and same save in all three; **similar but not identical flight
paths**, which is the whole methodological problem of §2. `hdr_*` is absent
everywhere, as expected for `hdr=0` — the FP16 scene path of `1d36c29` is not in
these measurements.

### Reproduction

```
python3 tools/analysis/analyze_iteration09_cost.py \
  --run run1=/tmp/x3-iteration09-run1/session-20260912-160404-1632.log \
  --run run2=/tmp/x3-iteration09-run2/session-20260912-162050-2040.log \
  --run run3=/tmp/x3-iteration09-run3/session-20260912-164116-2548.log \
  --baseline run3 \
  --json verification/results/iteration-09-cost.json \
  --text verification/results/iteration-09-cost.txt
```

Paired test `verification/analysis/test_iteration09_cost.py` (29 cases, two
synthetic logs, no game data): the estimators on analytically known inputs and
every reported quantity on a fixture whose route metrics are exact multiples of
their counts.

### What is and is not a measurement

Every number is a CPU-side QPC span or a `frame_normal` interval between two
completed `Present` calls, so it contains application work, pacing and driver
blocking, and **none of it is GPU time**
([telemetry.md](../verification/telemetry.md#route-and-boundary-cost)). Only the mutually
exclusive route group is summed — `route_gate + route_draw + route_fill +
route_jitter + route_lazy_flush`; `route_set_rt` is *inside* `route_draw`, the
`taa_*` phases are inside `taa_run`, and `route_readback` exists only in capture
frames.

"The route" here means the route *as configured with its observers*: the
`route_gate` span includes the object-scope snapshot and the history lookup, so
the observer reads that `route-cost-run1.md` symbolizes are inside the gate
number, not outside it.

## 1. Window selection

A one-second summary window reports count/total/min/max only, so the honest
statistics are the per-window mean (total/count) and the per-window minimum (the
fastest frame of that second, which no stall can inflate). There is no
session-wide frame-time median in this data.

The existing selector (`analyze_iteration08_taa.stream_scene_windows`) labels a
window "scene" from the nearest `motion_output_frame` with `routed > 0`. **That
marker does not exist in a route-off run at all**, so this analysis classifies
structurally, from evidence all three runs have:

1. a window whose `frame_normal` maximum exceeds 2 s contains a load: dropped,
   and it closes a phase;
2. a phase is **menu** when its median draws/frame is 500–800 *and* its p10–p90
   spread is under 20 % of that median (the main menu submits a nearly fixed
   draw list every frame; a sector sweeps draws/frame over an order of
   magnitude), else **scene**;
3. a window holding a capture frame (`route_readback`, `frame_capture`,
   `present_capture`, `capture_cpu`, `snapshot`) is dropped from every
   frame-time median and reported separately in §4;
4. windows with fewer than 5 frames are dropped from medians.

All three sessions segment identically — main-menu load, **menu**, save load
(85.6 / 105.5 s, the largest gap), **sector**, a 19.5–24 s load, **more
sector**, a 29.9–32.9 s return load, **menu** again:

| | windows | loading | capture | scene usable | menu usable | phase check |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| run 1 | 256 | 5 | 10 | 186 | 52 | `checked` |
| run 2 | 166 | 4 | 12 | 137 | 10 | `checked` |
| run 3 | 125 | 4 | 2 | 107 | 10 | `unavailable` (no route records) |

Where route records exist the structural labels are **cross-checked** against
them: every menu phase must contain no `motion_output_frame` with `routed > 0`
and every scene phase must contain one. Both route-on runs pass.

### The menu is the control, not a nuisance

In the menu the route is **enabled and its gate runs on every draw, but nothing
is routed** (`routed=0`, every draw rejected at gate 2, `route_gate` 0.177 ms of
a 43 ms frame, no jitter, no resolve) — while the sampling profiler, the object
trace, the camera-state reader and every telemetry stamp are on exactly as in
the sector. The menu therefore measures the **non-route** diagnostic overhead of
runs 1 and 2 against run 3, at a draw count (639–677) the sector also visits.

## 2. Per-run distributions

Regime split at 45 ms on the per-window mean, as in
[iteration-08.md §6](../verification/iteration-08.md) — `--window-mode-split-us`. Medians over
windows; µs unless stated.

| run | regime | windows | mean ms | min ms | draws/frame | `draw_backend` | `present_normal` | `stretch_backend` | route excl. | `taa_run` |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | fast | 124 | **33.08** | 30.67 | 270 | 2.73 | 32.5 | 0.75 | **4,697** | 339 |
| 1 | slow | 62 | **58.74** | 55.42 | 563 | 2.64 | 54.2 | 0.77 | **10,738** | 367 |
| 1 | menu | 52 | 43.10 | 40.28 | 678 | 3.04 | 12.9 | 2.15 | 177 | 0 |
| 2 | fast | 81 | **34.24** | 30.82 | 227 | 2.84 | 30.2 | 0.78 | **4,082** | 361 |
| 2 | slow | 56 | **64.43** | 57.37 | 471 | 2.72 | 48.1 | 0.79 | **9,050** | 391 |
| 2 | menu | 10 | 39.89 | 37.78 | 647 | 3.08 | 13.3 | 2.14 | 174 | 0 |
| 3 | fast | 90 | **24.05** | 21.69 | 229 | 2.83 | 9.9 | 1.63 | 0 | 0 |
| 3 | slow | 17 | **47.69** | 45.96 | 720 | 2.80 | 11.7 | 1.78 | 0 | 0 |
| 3 | menu | 10 | 34.97 | 32.85 | 653 | 2.76 | 11.7 | 2.04 | 0 | 0 |

Three side observations that are direct measurements rather than attributions:

* **`present_normal` rises from 9.9–11.7 µs to 30–54 µs with the route on** and
  stays at 12–13 µs in the menu where nothing is routed. The route's extra
  render-target traffic costs +20 to +44 µs inside `Present`, consistent with
  the +30 µs iteration 8 measured.
* **`stretch_backend` *falls*** from 1.63–1.78 µs to 0.75–0.79 µs with the route
  routing. The resolve runs *before* `timer.begin` in the same hook, so the
  application's own `StretchRect` submission is measured into an already-warm
  command stream. It is 1 call/frame either way and never material.
* **`log_flush` is 0.8 µs/call at 1.03–1.04 calls/frame in all three runs.** The
  154 MB / 217 MB / 9.9 MB log-size difference is entirely capture frames; in
  ordinary scene frames the three runs log the same amount, so logging volume is
  not a confounder once capture windows are excluded.

### Route metrics, per window (medians)

`route_set_rt` is nested in `route_draw`, and the `taa_*` phases in `taa_run`;
neither is added.

| metric | run 1 fast | run 1 slow | run 2 fast | run 2 slow | per call |
| --- | ---: | ---: | ---: | ---: | --- |
| `route_gate` | 3,216 | 7,395 | 2,766 | 6,140 | **11.65–13.29 µs per draw** |
| `route_draw` | 1,275 | 2,984 | 1,131 | 2,551 | 6.70–7.04 µs per routed draw |
| ↳ `route_set_rt` | 604 | 1,398 | 532 | 1,215 | 0.785–0.819 µs per call, 4 per routed draw |
| `route_jitter` | 138 | 302 | 115 | 251 | 0.279–0.282 µs per write, 2 per routed draw |
| `route_fill` | 67 | 66 | 69 | 73 | 66–73 µs, once per frame |
| **route exclusive** | **4,697** | **10,738** | **4,082** | **9,050** | |
| `taa_run` | 339 | 367 | 361 | 391 | once per frame |
| ↳ `taa_resolve_draw` | 300 | 327 | 317 | 345 | |
| ↳ `taa_state_apply` | 16.1 | 16.5 | 16.7 | 17.4 | |
| ↳ `taa_state_capture` | 8.0 | 8.3 | 9.1 | 9.7 | |
| ↳ `taa_copy_color` / `_depth` / `_back` | 3.5 / 1.8 / 1.0 | 3.5 / 1.9 / 1.0 | 3.7 / 2.2 / 1.0 | 3.8 / 2.2 / 1.1 | |
| `route_readback` | capture frames only, §4 | | | | |
| `hdr_*` | **absent** | absent | absent | absent | `hdr=0` |

### Route metrics, exact per frame (`motion_output_frame`)

Ordinary frames only (`readbacks = 0`), which is where these records must be
read from; medians over 88 (run 1) and 65 (run 2) routed records.

| | run 1 | run 2 |
| --- | ---: | ---: |
| draws / routed | 284 / 200 (70 %) | 248 / 174 (70 %) |
| route exclusive | **4,968 µs** | **4,604 µs** |
| per submitted draw | **17.25 µs** | **17.37 µs** |
| per routed draw | 24.65 µs | 25.23 µs |
| `gate_us` | 3,410 (11.61 µs/draw) | 2,938 (11.61 µs/draw) |
| `route_draw_us` | 1,338 (6.58 µs/routed) | 1,306 (6.86 µs/routed) |
| ↳ `set_rt_us` | 611 (0.773 µs/call, 4/routed) | 590 (0.783 µs/call, 4/routed) |
| `jitter_us` | 136 (0.273 µs/write, 2/routed) | 126 (0.275 µs/write, 2/routed) |
| `fill_us` | 60 | 63 |
| `taa_run_us` | 327 (`taa_draw_us` 293) | 354 (`taa_draw_us` 313) |
| `lazy_flush_us` | 0 (`rt_mode=perdraw`) | 0 |
| `scene_end_source` | `stretchrect` ×88 (`check=3`, StretchOnly) | `hook` ×65 (`check=1`, Agree) |

This reproduces the **~5.3 ms/frame** figure of
[iteration-09.md §1.4](iteration-09.md) as a median of 4.97 ms/frame over
ordinary routed frames, and resolves it into a gate that is **69 % of the
route**.

### The diagnostics that are *not* the route

| | run 1 | run 2 | run 3 |
| --- | ---: | ---: | ---: |
| profiler ticks/s (own `profile_report` deltas, scene phase) | 157.3 | 192.3 | — |
| sampled threads (median) | 19 | 16 | — |
| thread-suspension share of wall (ticks/s × 123 µs) | **1.93 %** | **2.37 %** | 0 |
| → at the regime's own frame time | **0.71 ms/frame** | **1.01 ms/frame** | 0 |
| route/TAA telemetry spans per frame | 2,074 | 1,929 | 0 |
| menu `route_gate` per call (a stamp plus a gate-2 rejection) | 0.264 µs | 0.272 µs | — |
| → route-specific stamp cost | **0.55 ms/frame** | **0.52 ms/frame** | 0 |

The 123 µs per suspended thread is the measured figure from
[loading-profile-run1.md §6](../reverse-engineering/loading-profile-run1.md).
The whole-run 2.37 % there does **not** transfer to a scene phase whose thread
count has risen: run 1's achieved rate in the sector is 157.3 ticks/s, giving
1.93 %. Because the profiler's cost is a share of *wall time*, it lands almost
entirely in the fixed term of §3's fit and barely in the per-draw slope
(0.0193 × ~54 µs/draw ≈ 1 µs/draw).

The 0.55 ms/frame stamp figure counts **only** `route_*` and `taa_*` spans.
[route-cost-run1.md §3](../verification/route-cost-run1.md) counts 4,382–7,831 stamps per frame
for *all* telemetry (`draw_backend` alone is one span per draw, and run 3 pays
that too) and bounds the total at 0.9–2.1 ms/frame. Both are consistent; they
measure different sets.

## 3. The controlled comparison

### Pooled per regime — mostly occupancy, and the draw counts do not match

| comparison | regime | route-on | route-off (run 3) | Δ mean | route metrics say |
| --- | --- | ---: | ---: | ---: | ---: |
| run 1 | fast | 33.08 ms, 270 draws/f | 24.05 ms, 229 draws/f | **+9.03 ms (+37.5 %)** | 4.70 ms = 52 % of Δ |
| run 1 | slow | 58.74 ms, 563 draws/f | 47.69 ms, 720 draws/f | **+11.05 ms (+23.2 %)** | 10.74 ms = 97 % of Δ |
| run 2 | fast | 34.24 ms, 227 draws/f | 24.05 ms, 229 draws/f | **+10.19 ms (+42.4 %)** | 4.08 ms = 40 % of Δ |
| run 2 | slow | 64.43 ms, 471 draws/f | 47.69 ms, 720 draws/f | **+16.74 ms (+35.1 %)** | 9.05 ms = 54 % of Δ |

Fast-regime occupancy is 0.667 (run 1), 0.591 (run 2), 0.841 (run 3), so the pooled
medians of the three sessions are not comparable at all. Worse, **the draw
counts differ inside each regime** (563 against 720 in the slow one), so even
the within-regime rows are confounded: the "97 % of Δ" is a coincidence of two
errors of opposite sign, not a result.

### Normalised on draws per frame

Frame time in these sessions is close to affine in draws/frame. Windows are
binned on draws/frame (50 wide); a bin with at least two windows on both sides
contributes one paired `(draws, Δms)` observation. The pairs are fitted with
Theil–Sen (median of pairwise slopes); the quoted uncertainty is the
interquartile range of those pairwise slopes.

| pairing | phase | bins | draw span | slope | IQR | fixed | sign test | origin-only |
| --- | --- | ---: | --- | ---: | --- | ---: | --- | ---: |
| run 1 − run 3 | sector | 13 | 73–767 | **+30.11 µs/draw** | +10.7…+40.6 | −0.73 ms | 13+/0−, p = 0.0002 | +25.58 |
| run 1 − run 3 | sector (window minima) | 13 | 73–767 | +33.13 | +20.3…+39.1 | −2.05 ms | 13+/0−, p = 0.0002 | +26.48 |
| run 1 − run 3 | **menu** | 2 | 639–676 | (ill-conditioned) | | | 2+/0− | **+12.49** |
| run 2 − run 3 | sector | 13 | 71–774 | **+41.69 µs/draw** | +32.4…+53.1 | −1.27 ms | 13+/0−, p = 0.0002 | +39.43 |
| run 2 − run 3 | sector (window minima) | 13 | 71–774 | +38.52 | +28.7…+55.3 | −0.12 ms | 13+/0−, p = 0.0002 | +38.20 |
| run 2 − run 3 | **menu** | 2 | 640–669 | (ill-conditioned) | | | 2+/0− | **+8.04** |
| run 1 − run 2 | sector | 14 | 69–774 | −9.20 µs/draw | −38.2…+5.8 | −0.22 ms | 3+/11−, p = 0.057 | −9.89 |
| run 1 − run 2 | sector (window minima) | 14 | 69–774 | −2.98 | −25.0…+11.6 | −1.54 ms | 2+/12−, p = 0.013 | −9.25 |

The menu bins span only 37 draws, so a free-intercept slope there has no
leverage and the tool reports it as ill-conditioned; the **origin-constrained**
estimate (median of per-bin Δ/draws) is the usable one, and it is remarkably
stable: run 1's two menu bins give +8.00 ms at 639 draws and +8.42 ms at 676
draws — 12.5 and 12.4 µs/draw.

### Attribution

| | run 1 − run 3 | run 2 − run 3 |
| --- | ---: | ---: |
| sector, total | +30.11 µs/draw | +41.69 µs/draw |
| − menu overhead (profiler, object trace, per-draw stamps, gate-2 rejection) | −12.49 | −8.04 |
| **= route, attributed** | **+17.62 µs/draw** | **+33.65 µs/draw** |
| route's own metrics | 17.22 µs/draw | 17.97 µs/draw |
| **unexplained** | **+0.40 µs/draw** | **+15.68 µs/draw** |
| at that run's median 324 / 303 draws/frame | 5.70 ms/frame | 10.19 ms/frame |

**Run 1's frame-time attribution and the route's own telemetry agree to
0.4 µs/draw (2 %).** That is the result iteration 8 asked for: the route's spans
are not missing a large hidden cost.

Run 2's residual is +15.7 µs/draw, and the run-1-against-run-2 row above says
why: at matched draw counts run 2 is slower than run 1 by 9.2 µs/draw on window
means (11 of 14 bins, p = 0.057) and 3.0–9.3 µs/draw on window minima (12 of 14,
p = 0.013), with **no route metric differing by more than 30 µs/frame**. That is
the path/session noise this method cannot remove, and it is the uncertainty to
quote.

### Headline: route cost per regime

Attributed per-draw cost carried to each regime's own draw count, beside what
the route's own spans measured there.

| run | regime | draws/frame | frame | route attributed | % of frame | route metrics | % of frame |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | fast | 270 | 33.08 ms | **4.76 ms** | **14.4 %** | 4.70 ms | 14.2 % |
| 1 | slow | 563 | 58.74 ms | **9.91 ms** | **16.9 %** | 10.74 ms | 18.3 % |
| 2 | fast | 227 | 34.24 ms | 7.64 ms | 22.3 % | 4.08 ms | 11.9 % |
| 2 | slow | 471 | 64.43 ms | 15.84 ms | 24.6 % | 9.05 ms | 14.1 % |

**Best estimate: 18 µs per submitted draw (25 µs per routed draw),
4.7–10.7 ms/frame, 14–18 % of the frame, with an upper bracket of 34 µs/draw
and 25 % from run 2 and an uncertainty of ±9 µs/draw from the path
difference.**

### Reconciling iteration 8 and run 1

Nothing here contradicts either earlier reading; they measured different things.

* **Iteration 8's "+0.2 %" was TAA on against TAA off with the route on in
  both.** What differed was the jitter, the resolve, the history retention and
  the debug readbacks — not the route. This run measures exactly those at
  `taa_run` 0.34–0.39 ms/frame plus `route_jitter` 0.12–0.30 ms/frame, i.e.
  **0.47–0.69 ms on a 33–64 ms frame, 1.1–1.4 %** — the same order as the
  ±0.3 % resolution iteration 8 quoted (which also had a different jitter
  baseline, the route being on in both of its runs), so the two are consistent
  and the resolve is confirmed cheap.
* **Run 1's "~5.3 ms/frame of route metrics" is reproduced** as 4.97 ms/frame
  (median over 88 ordinary routed frames) and is now shown to be *real frame
  time*, not accounting: the route-off run is 5.70 ms/frame faster at the same
  draw count.
* So the resolve is ~0.35 ms and the per-draw route is ~5–11 ms: **the route is
  13–30× the resolve**, and it is a per-draw cost, which is why it never showed
  up in a TAA on/off comparison.

## 4. Run 2 against run 1: the engine hook and the `--taa-debug` readbacks

Run 2 differs from run 1 in three ways, two of which are off the scene path.

**The scene hook moves where the resolve runs.** `scene_end_source=hook` in all
65 of run 2's ordinary routed records (`scene_end_check=1`, *Agree*: the hook and
the `StretchRect` heuristic identify the same boundary) against
`scene_end_source=stretchrect` in all 88 of run 1's (`check=3`, *StretchOnly* —
the hook was not installed). The resolve's own cost barely moves:

| | run 1 | run 2 | Δ |
| --- | ---: | ---: | ---: |
| `taa_run` (fast / slow window medians) | 339 / 367 µs | 361 / 391 µs | +22 / +24 µs |
| `taa_resolve_draw` | 300 / 327 µs | 317 / 345 µs | +17 / +18 µs |
| `taa_state_capture` | 8.0 / 8.3 µs | 9.1 / 9.7 µs | +1.1 / +1.4 µs |
| `route_gate` per draw | 11.84 / 13.06 µs | 11.65 / 13.29 µs | −0.19 / +0.23 µs |

**Frame time, however, is 9.2 µs/draw worse in run 2 at matched draw counts**
(≈ +2.8 ms at 300 draws/frame; +0.9–2.8 ms on window minima), and **no route or
TAA metric accounts for more than 30 µs of it**. Three readings are open and
this pair of runs cannot separate them: (a) a stall the spans cannot see — the
resolve now runs at the engine's scene-end signal rather than at the bloom
`StretchRect`, a different point in the frame with different work in flight;
(b) the mesh-cache hooks, which are installed (205 `mesh_cache` records,
`mesh_cache_bypass reason=floating_point`) even though the cache itself never
activated (`enabled=0`, `await_public_mesh_and_buffer_contract`) — so it is off
the *route* path but not out of the process; (c) plain path difference, which the
sign test only weakly excludes. **The honest statement is that the hook costs
nothing measurable in the route's own spans and that a 1–3 ms/frame difference
of unknown origin separates the two sessions.** A run-2-style session with
`--taa-debug` off would settle it.

**The `--taa-debug` readbacks cost 24 ms per capture frame.** Capture windows are
excluded from every median above; their cost is:

| | run 1 | run 2 | run 3 |
| --- | ---: | ---: | ---: |
| capture windows dropped | 10 | 12 | 2 |
| `readbacks` per capture frame | 2 (RT1 motion, RT2 depth) | 4 (+ pre-resolve colour, resolved FP16) | 0 |
| `route_readback` per capture frame | 23,294 µs | 47,732 µs | — |
| per readback | 11,647 µs | 11,933 µs | — |
| `frame_capture` interval (median) | 328.4 ms | 377.1 ms | 328.6 ms |

So `--taa-debug` adds **+24.4 ms per capture frame** in readbacks; the capture
frame itself is 48.7 ms longer, the balance being the extra per-draw
diagnostics. Note that run 3's capture frames cost the *same* 328.6 ms as run
1's despite doing no readbacks at all — the capture frame is dominated by
per-draw diagnostic logging, not by the readbacks. **Performance must never be
read off a capture burst**, which is why §1 drops those windows.

## 5. Cost levers, ranked

Per frame, at run 1's median ordinary routed frame (284 draws, 200 routed, route
exclusive 4,944 µs). Cross-referenced against
[route-cost-run1.md §3](../verification/route-cost-run1.md), which symbolizes the same spans
against the in-process profiler and is the stronger evidence wherever the two
overlap.

| # | lever | now | expected saving | evidence |
| ---: | --- | ---: | ---: | --- |
| 1 | **Replace the observers' self-`ReadProcessMemory` with `VirtualQuery`-validated direct reads** (`object_trace`, `object_lifetime`, `scene_hook`) | `route_gate` **3,410 µs/frame = 11.6 µs/draw = 69 % of the route** | **−3.2 ms/frame** (flight B) / −5.5 ms (flight A) | high — [route-cost-run1.md](../verification/route-cost-run1.md) puts 92–98 % of the gate span in ~21 `NtReadVirtualMemory` syscalls per routed draw. This report independently shows the gate costs 11.6 µs/draw in the sector against **0.26 µs/draw in the menu**, where the same gate rejects at gate 2 before the observer reads — a 45× ratio that is consistent with the reads being the span |
| 2 | Skip the four engine matrices in `object_trace::current` outside capture frames (diagnostics only) | 8 of 21 reads | −1.2 / −2.1 ms | high, but a **subset of 1**, not an addition |
| 3 | **Drop per-draw telemetry spans in production** (keep the per-frame counters and `X3M_TELEMETRY=1` for diagnosis) | 2,074 route/TAA spans per frame at 0.26 µs = **0.55 ms/frame**; 4,382–7,831 spans/frame for all telemetry | **−0.55 ms** (route spans) to **−2.1 ms** (all telemetry) | medium — the per-stamp cost is bounded from above by the menu's 0.264 µs gate sample and by route-cost-run1's ≤ 0.33 µs, but is not separated from the game's own QPC calls |
| 4 | **`X3M_MOTION_RT_MODE=lazy`** | `route_set_rt` **611 µs/frame** (0.773 µs × 4 per routed draw), *nested inside* `route_draw` | **−244 µs/frame** (40 %), bracketed −0.2…−0.6 ms | low — the fixture's burst case gives 20 → 12 binds ([motion-output.md](../verification/motion-output.md#lazy-rt-binding-equivalence-x3m_motion_rt_mode)); with 70 % of draws routing and rejections interleaved, the gameplay bind structure is unmeasured. **This is the smallest of the three real levers**, not the first one to reach for |
| 5 | Share one registry read between `object_trace::current` and `object_lifetime::read_registry` | 2 of 21 reads | −0.3 / −0.5 ms | high, also a subset of 1 |
| 6 | **Halve the jitter constant writes** (write the jittered rows once per frame instead of per draw, if the engine can be shown not to read them back) | `route_jitter` **136 µs/frame**, 0.273 µs × 2 per routed draw | **−68 µs/frame** | medium — the restore half exists precisely because the engine may read the rows back; needs a correctness argument, and the prize is small |
| 7 | Sentinel fill | `route_fill` 60 µs/frame, once per frame | — | no lever; listed to close the accounting |
| 8 | Temporal resolve | `taa_run` 327 µs/frame | — | **not a lever.** 0.5–1.0 % of the frame, consistent with iteration 8's +0.2 % |

Items 1 + 3 + 4 are independent and together remove **about 4.0 ms/frame by this
report's spans** (3.2 + 0.55 + 0.24) and 4.8–7.9 ms/frame by
route-cost-run1's symbolized accounting — that is, essentially all of the
route's measured CPU cost, with no change to any rendering behaviour. The
ordering is the opposite of the one iteration 9 reached for: **lazy RT mode is
lever 4, not lever 1.**

## Limits

1. CPU-side QPC spans and `Present`-to-`Present` intervals only; never GPU time.
   A route cost of 18 µs/draw is engine-thread CPU, and this data cannot say
   whether the GPU is also paying for the second render target.
2. The three sessions are the same build and save but **not the same flight
   path**. The draw-count normalisation removes the first-order effect; the
   residual is bounded by the run-1-against-run-2 slope (±9 µs/draw) and nothing
   in this data bounds it more tightly. A frame-time difference is not proof of
   causation — only the route metrics are direct measurements of route work.
3. Run 2's +15.7 µs/draw unexplained residual is **not** explained here. §4 lists
   three candidate causes and says which run would separate them.
4. The menu control shares subsystems with the route: the object trace is inside
   the `route_gate` span in the sector and outside any route span in the menu, so
   the split between "route" and "menu overhead" is a split by *where the work is
   billed*, not by which code runs.
5. Both route-on runs present at interval 1; a cost below a few hundred
   microseconds could hide in display pacing. The observed modes (24, 33, 48, 59,
   64 ms) are not small multiples of a 60 Hz refresh, so they are probably not
   vsync-quantized, but this measurement cannot exclude it.
6. The window minimum is used as a stall-free cross-check throughout and agrees
   with the mean-based fit to within 3 µs/draw on run 1 and 3 µs/draw on run 2.
7. `hdr_*` is absent in all three runs. The FP16 scene path of `1d36c29` is
   unmeasured by this report.
