# Iteration 11: the review-25 build (validated direct engine reads) in flight

Run 3 on the **X3** bottle (arm64 Wine + FEX, `FEX_X87REDUCEDPRECISION=1`,
`WINEMSYNC=1`) with the review-25 build, commit `1924c55`, installed
`d3d9.dll` SHA-256 `38562f3a…`:

```
tools/manage.py launch --direct --ownership --object-trace --object-lifetime \
    --motion-output --taa --telemetry --gz-buffer --scene-hook
```

Route on, TAA on, engine scene-end hook on, validated direct engine reads
(`X3M_ENGINE_READS` unset), gz read-ahead buffer on, **no sampling profiler, no
`--taa-debug`, and `X3M_TELEMETRY_DRAW` unset so the per-draw telemetry stamps
are off**. Path: start → menu → the usual save → flight → sector change → back
to the menu → exit.

**Verdict, four sentences.** The route's per-draw cost fell from **34.44 to
11.89 µs/draw** attributed (paired draw-count bins against the same route-off
run), and at a matched draw count the frame is **22.2 µs/draw cheaper than run 6
(12 of 13 bins, sign p = 0.0034)**: 12.10 ms against 16.91 ms in the fast
regime. The proxy is healthier than in run 6 on every counter that can report a
fault — **0 apply/restore failures, 0 state-shadow resyncs (run 6: 24), 162 of
162 resolves with history (run 6: 214 resolved but 203 with history), a 99.83 %
history match rate (run 6: 99.16 %), Agree on every latched frame and no
disagreement record at all (run 6: 24 Disagree, 16 records)**. The gz read-ahead
buffer did what it was built for: **14,461,803 game-level `gzread` calls of
mean 3.16 bytes became 175 real reads** of 45.75 MB with no read amplification
and no error. Two things did *not* improve and are flagged below: the route
still adds **~3.3 ms/frame ≈ 27 % of the frame** and almost none of it is
stamped any more (11.65 of 11.89 µs/draw unattributed), and this run's
configuration **removed the readback row-pair, temporal and depth-agreement
checks** that iterations 9 and 10 used to certify the motion output.

## Provenance

| | run 9 (this run) | run 6 (iteration 10) | run 5 (route off) |
| --- | --- | --- | --- |
| log | `/tmp/x3-bottleX3-run9/session-20260912-194757-212.log` | `/tmp/x3-bottleX3-run6/session-20260912-171241-212.log` | `/tmp/x3-bottleX3-run5/session-20260912-170743-352.log` |
| bytes / lines | 56,092,939 / 1,021,360 | 295,274,646 / 5,334,487 | 24,838,333 / 490,146 |
| sha256 | `1a00d7d03b9aad416e7708d042a177e2ea8a2717b5eedb0fbdb5245c7ccdf927` | `c0fe801c…` | `8105f336…` |
| build | review 25 (`1924c55`) | `1d36c29` | `1d36c29` |
| route | `requested=1 taa=1 taa_debug=0 jitter=1 rt_mode=perdraw scene_hook=1 hdr=0` | `… taa_debug=1 … scene_hook=1` | `requested=0` |
| per-draw stamps | **`per_draw=0`** | absent flag (= on) | absent flag (= on) |
| profiler | absent | 32 threads, 241.3 ticks/s | absent |
| line kinds | 83 | 92 | 62 |
| readbacks | 7 single frames (1711, 2227, 3235, 4386, 5295, 9645, 9878), motion + depth | 9 bursts × 4 frames | 1 capture frame |

The readback directory `/tmp/x3-bottleX3-run9/` is **cumulative**: 361 readback
files, of which run 9 wrote 14 (the 7 frames above, motion and depth — matching
the route's own `readbacks=14` total). The other 347 are the 4-frame bursts of
earlier runs; every tool here pairs files to the `motion_output_readback` lines
of *this* log, so they are ignored.

Loading work is reported here only for comparison with the run-8 loading
analysis; the loading investigation itself is not duplicated.

## Reproduction

```sh
T=/tmp/x3-bottleX3-run9/session-20260912-194757-212.log     # run 9
U=/tmp/x3-bottleX3-run6/session-20260912-171241-212.log     # run 6, route on
S=/tmp/x3-bottleX3-run5/session-20260912-170743-352.log     # run 5, route off
R=verification/results

python3 tools/analysis/analyze_iteration09.py $T --no-camera-detail \
    --output $R/iteration-11-run9-health.json --text $R/iteration-11-run9-health.txt
python3 tools/analysis/analyze_iteration09_cost.py --run run9=$T --run run6=$U \
    --run run5=$S --baseline run5 --json /tmp/it11/iteration-11-cost.json \
    --text $R/iteration-11-cost.txt
python3 tools/analysis/analyze_iteration09_run2.py $T --captures /tmp/x3-bottleX3-run9 \
    --baseline-log $U --baseline-label run6 --ideal-frames 1 \
    --output /tmp/it11/iteration-11-run9-taa.json --text /tmp/it11-taa.txt
    # the JSON is written first; render_text then raises on a run with no
    # mesh_cache metrics (`native_seconds` is None) - the same defect iteration 10 hit
python3 tools/analysis/analyze_motion_readback.py $T --readback-dir /tmp/x3-bottleX3-run9 \
    --label iteration11 --results-dir /tmp/it11/rb --jitter-from-log --no-draw-details

# the consolidated report (this document's tables)
python3 tools/analysis/analyze_iteration11.py \
    --run run9=$T --run run6=$U --run run5=$S \
    --primary run9 --route-off run5 --previous run6 \
    --health $R/iteration-11-run9-health.json \
    --taa /tmp/it11/iteration-11-run9-taa.json \
    --readback /tmp/it11/rb/motion-readback-iteration11-summary.json \
    --output $R/iteration-11.json --text $R/iteration-11.txt

python3 -m unittest verification/analysis/test_iteration11.py     # 34 cases
```

`analyze_iteration11.py` derives only what no existing tool can produce on a
`per_draw=0` run — the draws-per-frame surrogate and everything that depends on
it, the route-cost availability statement, the recovery arithmetic, the decoded
hook fields, the gz/zlib accounting and the labelled loads. The frame-time
statistics, phase labelling, regimes, Theil-Sen paired fits and sign tests are
the tracked iteration-9 cost code, called unmodified.

---

## 1. Frame time and the route's own cost

### 1.1 What `per_draw=0` took away, and the surrogate that replaces it

Review 25 put the per-draw telemetry behind `X3M_TELEMETRY_DRAW`, which this run
did not set. Consequences, all confirmed in the log:

* `draw_backend`, `route_gate`, `route_draw`, `route_set_rt`, `route_jitter`,
  `route_lazy_flush` are **absent** (0 records each).
* `gate_us`, `route_draw_us`, `set_rt_us`, `lazy_flush_us`, `jitter_us` in
  `motion_output_frame` are **0.0 on all 155 routed ordinary records** — that is
  *unmeasured*, not free. The route's own 31 %-of-frame figure of iteration 10
  has no counterpart in this run.
* The per-frame brackets survive: `fill_us` **63.6**, `taa_run_us` **379.1**
  (`taa_draw_us` 338.1, `taa_capture_us` 8.7, `taa_apply_us` 16.0,
  `taa_copy_color_us` 3.5, `taa_copy_depth_us` 2.5, `taa_copy_back_us` 0.9),
  `readback_us` 0.0 µs per frame, medians over the routed ordinary frames.
* Without `draw_backend` the cost tool cannot bin windows by draws/frame, so no
  paired comparison is possible at all (`analyze_iteration09_cost` on its own:
  *"no shared draw bins"* for every run-9 pair).

The surrogate: `motion_output_frame` logs the frame's own `draws` every
`frame_log=60` frames, one or two samples per one-second report window, and the
median of the samples inside a window is injected as a synthetic `draw_backend`
count (time left at zero). Validated against the measurement it replaces on
run 6, which logged both: **189 windows, median relative error 1.3 %, p90 7.1 %,
92.1 % of windows within 10 %** (worst case 85 %, a window whose single sampled
frame sat on a scene transition). 125 of run 9's 137 windows were injected; the
phases then label correctly — menu `[104..871]`, scene `[948..9423]`, scene
`[9517..10186]`, menu `[10324]` — which is exactly the flight the user reported.

### 1.2 Frame time per draw-count bin

Median per-window mean frame time, 50-draw bins, scene phases only
(`iteration-11.json` → `draw_bins`):

| draws/frame bin | run 9 (route on, direct reads) | run 6 (route on, RPM + stamps) | run 5 (route off) |
| --- | --- | --- | --- |
| 50–100 | 8.36 ms (n=6) | 8.35 ms (n=20) | 8.33 ms (n=12) |
| 100–150 | 8.55 (14) | 10.79 (12) | 8.30 (4) |
| 150–200 | 10.61 (13) | 13.20 (18) | 8.48 (1) |
| 200–250 | 10.40 (8) | 13.91 (7) | 8.36 (16) |
| 250–300 | **12.24 (18)** | **16.56 (14)** | **8.83 (4)** |
| 300–350 | 12.32 (8) | 19.05 (13) | 8.43 (9) |
| 350–400 | 13.01 (10) | 19.70 (14) | 8.41 (13) |
| 400–450 | 14.54 (4) | 23.80 (6) | 8.48 (9) |
| 500–550 | 17.83 (2) | 27.87 (6) | 8.86 (1) |
| 700–750 | 21.04 (5) | 35.78 (8) | 11.52 (5) |
| 750–800 | 21.99 (2) | 40.44 (4) | 12.57 (3) |
| 800–850 | 21.75 (5) | 38.21 (14) | — |

Paired-bin fits (Theil-Sen on the bins both runs populate, plus the
origin-constrained per-bin estimate and the two-sided sign test):

| comparison | bins | draws span | µs/draw | origin µs/draw | fixed ms | sign |
| --- | --- | --- | --- | --- | --- | --- |
| run 9 − run 5 (route cost now) | 10 | 73–774 | **+14.55** | +12.07 | −0.99 | 10+/0−, p=0.00195 |
| run 6 − run 5 (route cost then) | 10 | 64–776 | **+37.77** | +31.29 | −2.41 | 10+/0−, p=0.00195 |
| run 9 − run 6 (direct) | 13 | 77–819 | **−22.21** | −18.94 | +1.35 | 1+/12−, p=0.00342 |

Subtracting each run's own menu overhead (the same instrumentation with nothing
routed: 2.67 µs/draw for run 9, 3.33 for run 6) gives the attributed route cost:

| | run 9 | run 6 |
| --- | --- | --- |
| attributed route cost | **11.89 µs/draw** | **34.44 µs/draw** |
| at the run's own draw count | 3.27 ms of 12.10 ms = **27.0 %** at 275 draws/f | 12.34 ms of 16.91 ms = **73.0 %** at 358 draws/f |
| measured by the route's own spans | 0.065 ms = 0.5 % (frame brackets only) | 5.26 ms = **31.1 %** |
| unattributed | 11.65 µs/draw | 19.83 µs/draw |
| frame without the route | 8.83 ms | 4.57 ms |

Run 6 also carried a sampling profiler (0.546 ms/frame, a share of wall time, so
it lands in the fit's fixed term) and 1,904.7 telemetry spans/frame
(0.343 ms/frame = **0.96 µs/draw**, which does land in the slope). Removing only
the stamp term from both sides: **33.48 → 11.88 µs/draw, a 64.5 % reduction.**

**Conclusion (question 1).** Yes — the engine-read change recovered most of the
route's per-draw cost: 34.4 → 11.9 µs/draw attributed, −64.5 % after charging
run 6's per-draw stamps to the diagnostics, and at a matched draw count the
frame is 22.2 µs/draw (−28.5 % of the mean, −31.0 % of the window minima)
faster. The iteration-10 headline — "the route owns 31 % of the frame by its own
spans" — is *gone as a measured quantity*, because with `per_draw=0` the spans
measure only the per-frame brackets (0.5 % of the frame). What remains is the
frame-time evidence, and it still charges the route **3.27 ms/frame, 27 % of the
frame**, of which the route's instrumentation now explains 2 %. This comparison
is not single-variable: read path, per-draw stamps, profiler and flight path all
changed together, and the route-off baseline is still the old build.

### 1.3 `engine_memory` / `engine_reads` lines: none exist

The log carries **no** `engine_memory`, `engine_reads` or equivalent line, and
**no line records `X3M_ENGINE_READS`** — `engine_memory::Stats` (reads,
VirtualQuery queries, rejected spans, rpm syscalls;
`src/proxy/engine_memory.h`) is never emitted to the session log by this build,
and `tools/manage.py` has no switch for the read path. The read path of a
session therefore cannot be established from its log, only from the launch
environment (unset = direct), the installed binary and the fixture suites; the
VirtualQuery rate and the fallback counts are known only from those fixtures
(`docs/verification/handoff-engine-reads.md`: `object_trace` route 3.362 µs rpm
vs 1.312 µs direct at 0.0040 queries/call, `object_lifetime::current` 7.007 vs
0.693 µs at 0.0237 queries/call). **Flagged as an observability gap**: one
cumulative `engine_memory` line per report window would make the claim above
checkable from the log and would show a per-frame region-cache miss rate that no
fixture can.

---

## 2. The engine scene-end hook

180 `motion_output_frame` records, 163 latched, 162 resolves.

| | run 9 | run 6 |
| --- | --- | --- |
| `scene_end_check` | **Agree 162, None 18, Disagree 0** | Agree 214, None 40, Disagree 24 |
| `scene_end_source` | hook 162, none 18 | hook 214, none 64 |
| `resolved_by_source` | **hook 162 / 162** | hook 214 / 214 |
| `motion_output_scene_hook_disagreement` | **0 records** | 16 records |
| `draws_after_hook` | 0 on all 180 records | 0 on all 278 |
| `hook_signals` | 1 on 179, 0 on the frame-0 record | 1 per record |
| `hook_outside_scene` | 1 on 17 records, all `hook_state=9` (no scene latched) | — |
| `rs_resyncs` | **0 on all 180 records** | **24** |
| `rs_queries` / `rs_hits` / `rs_gets` | 307,334 / 307,334 (**hit rate 1.000000**) / 2,268 | 431,026 / 431,026 / 3,332 |
| `apply_failures` / `restore_failures` | 0 / 0 | 0 / 0 |

The patch site is the one iteration 10 characterised (`0x004721b1` → target
`0x004c4750`, verified by executable SHA-256 + exact callsite bytes + resolved
rel32 before the write), `status=active`, and there is no unwind, veto or
fail-closed record anywhere in the log. One latched frame carries no verdict —
frame 0, which latched but resolved nothing (`taa_skip=2`), so there was nothing
to cross-check; `analyze_iteration09_run2` reports this as
`latched_all_agree=false`, which is the only reason that flag is not true.

**Conclusion (question 2).** The hook is the resolve point for every frame that
had a scene, with **zero disagreements** — the Disagree branch that iteration 10
characterised (24 frames on a screen with no draws) did not fire once here, and
the state-shadow resyncs that run 6 recorded 24 of are **gone** (0), with a
perfect 307,334/307,334 shadow hit rate. No hook failure, no unwind, no draw
after the hook.

---

## 3. TAA health

| | run 9 | run 6 |
| --- | --- | --- |
| resolves attempted / resolved / with history | **162 / 162 / 162** | 214 / 214 / **203** |
| `taa_skip` | 0 ×162, 2 ×18 (unlatched frames) | 0 ×214, 2 ×64 |
| history match (`matched`/`routed`) | **33,747 / 33,805 = 99.83 %** | 46,818 / 47,213 = 99.16 % |
| gate rejections | gate2 15,077, gate3 1,312, gate4 9,297, gate6 58, gate1/gate5 **0** | gate5 (Scope) still 0 |
| cut detector | **0 flagged**, median_px max 8.04, missing max 0.0797 | 0 flagged |
| camera records / valid / cuts | 43 / 38 / **0** | 214 records, 0 cuts |
| camera policy / reason | `camera_path` 38, `switch_off` 5 (menu) | — |
| rotation | max **1.6258°**, median 1.0748, against a self-rotation floor of 0.7219–1.6258° (row-norm deviation 1.81e-4) | max 8.73°, median 1.02 |
| `taa_run` per call | 380.7 µs | 316.8 µs (**1.20×**) |
| `route_fill` per call | 66.4 µs | 62.4 µs (1.06×) |
| `frame_normal` per call | 12,218 µs | 18,881 µs (0.65×) |

Readback checks (`analyze_motion_readback`, 7 frames):

| check | result |
| --- | --- |
| `counter_consistency` | **pass**, 8 frames |
| `displacement` | **pass**, 1,212,147 valid pixels, max 4.80 px, 0 suspicious |
| `depth_image_integrity` | **pass**, 7 frames, sentinel 73.3–89.0 %, written depth ∈ [0.6106, 0.99999], `valid_motion_without_depth` 0, no non-finite or out-of-range value |
| `readback_integrity` | **fail — benign**: 8 captured frames, 7 clean; the miss is frame 120, the `capture_start=120` frame in the *menu*, where `routed=0` so the route wrote no readback |
| `row_consistency`, `temporal_coverage`, `depth` (previous-depth agreement), `static_consistency`, `history_pairing` | **unavailable** |
| `taa_image` | unavailable (no `--taa-debug`, so no resolved-image readback) |

The unavailable group is a **configuration regression, not a code one**: those
checks need consecutive captured frames, and this run captured single frames
(`capture_frames=1`) instead of run 6's 4-frame bursts. The row-pair error and
the previous-depth agreement that iterations 9 and 10 used to certify the motion
output are therefore **not reproduced for this build**.

Line kinds: 83, against a union of 92 across the two baselines. New kinds are
three feature announcements — `gz_buffer`, `gz_buffer_file`, `mesh_adjacency` —
and the missing ones are all configuration: nine `profile_*` kinds (no
profiler), `motion_output_color_readback` and `motion_output_taa_readback` (no
`--taa-debug`) and `motion_output_scene_hook_disagreement` (nothing to report).
**No new warning or error kind, and no kind whose name contains fail/error/warn/
reset/unwind exists at all.** The only non-zero result codes in the log are
`surface container_result=80004002` (E_NOINTERFACE from the capture's
`GetContainer` on a non-texture surface) 6,334 times — 32,596 times in run 6, so
pre-existing — and `fill_result=1`/`taa_result=1` on the 18 unlatched records.

**Conclusion (question 3).** TAA is healthier than in run 6: every resolve
landed (162/162) and every resolve had history, against 203 of 214; the history
match rate 99.83 % is the best measured on either bottle (old-bottle spread
97.8–99.4 %, run 6 99.16 %); no cut, no camera cut, no reset, no apply/restore
failure, no new warning kind. Two caveats: the resolve is now the largest
*measured* route cost and is 1.20× more expensive per call than in run 6
(380.7 vs 316.8 µs, ≈3.1 % of a 12.1 ms frame — worth a second measurement at a
matched draw count before calling it a regression), and every logged camera
rotation (max 1.6258°, median 1.0748°) sits **at the orthonormality noise
floor** (0.72–1.63°), so this run says nothing about reprojection under real rotation.

---

## 4. The gz read-ahead buffer and the loads

`gz_buffer requested=1 enabled=1 capacity_kb=256 telemetry=1 imports=1
rewind=1 slots=32`; five `gz_buffer_file` summaries.

The savegame file:

| counter | value |
| --- | --- |
| `calls` | **14,461,803** |
| `small_calls` | **14,354,293** (99.26 %) |
| `served_bytes` | **45,754,974** (45.75 MB), mean **3.164 bytes/call** |
| `real_reads` | **175** → 82,639 buffered calls per real read |
| `real_bytes` | 45,754,974 — **equal to served: no read amplification** |
| `direct_reads` / `getcs` / `tells` / `seeks` / `error` / `end` | 0 / 0 / 0 / 0 / **0** / 0 |

The other four files are tiny (9 calls, 92–113 bytes served) and each cost one
full 256 KB chunk read — a bounded over-read of 256 KB per small file, four
times in the session, which is the documented trade of a fixed chunk size.

Session zlib totals (`loading_metric` rows are per-window **deltas**, summed):
`inflate` **773,054 calls / 12.60 s** (max 2.76 ms), `gzread` **179 calls /
46.80 MB / 0.196 s**, `gztell` 179, `gzopen` 27 (22 failures — the loader's
probes for absent files), `gzclose` 5. Inside the save-load gap
(`[24.9, 60.6] s`): `gzread` **175 calls, 0.186 s**, `inflate` **359,874 calls,
5.98 s**, one `gzopen` and one `gzclose` — so about **6.2 s of the 35.7 s save
load is zlib work**, and the ~14.5 M three-byte reads the buffer absorbed cost
the decoder nothing measurable any more.

Loads, from the `frame_normal` window maxima with the phase each gap separates
(this run performed four, not three):

| load | duration | ends at frame | ends at | `GenerateAdjacency` in the gap |
| --- | --- | --- | --- | --- |
| startup → menu | **8.386 s** | 28 | 13.5 s | 1,017 calls, 0.410 s |
| save load (menu → sector) | **35.707 s** | 872 | 60.6 s | 3,168 + 174 calls, 1.687 s |
| sector change | **5.944 s** | 9439 | 175.1 s | 2,271 calls, 0.833 s |
| return to menu | 7.947 s | 10228 | 194.4 s | 1,020 calls, 0.438 s |

Session `ID3DXMesh::GenerateAdjacency`: 7,693 calls, 3.38 s, all on the native
path (`--mesh-cache` not passed, so `mesh_cache` reports
`effect=none: every call took the original native path`).

**Conclusion (question 4).** The gz buffer works exactly as designed on the
savegame — 14,461,803 calls of mean 3.16 bytes served from 175 real reads of
45.75 MB, zero errors, zero amplification, `served == real_bytes` — and inside
the 35.71 s save load only 6.2 s is zlib (359,874 `inflate` calls, 5.98 s; 175
`gzread`, 0.186 s), which moves the remaining save-load cost off zlib and onto
whatever the run-8 loading analysis attributes it to. The three load durations
for comparison are **menu 8.386 s, save 35.707 s, sector change 5.944 s** (plus
7.947 s to return to the menu).

---

## 5. Report

1. **Frame time / route cost.** Attributed route cost **34.44 → 11.89 µs/draw**
   (−65 %; −64.5 % after charging run 6's 0.96 µs/draw of per-draw stamps to
   diagnostics), and at matched draw counts run 9 is **22.21 µs/draw** cheaper
   than run 6 (12 of 13 bins, p=0.0034): fast-regime frame **12.10 ms vs
   16.91 ms**, window minima **10.06 vs 14.58 ms**. The direct-read change
   therefore recovered most of the cost iteration 10 measured — but the route
   still costs **3.27 ms/frame = 27.0 % of the frame** at 275 draws/frame
   against the route-off baseline's 8.83 ms, so the *share* did not collapse
   from 31 % to nothing; it stopped being measurable.
2. **Engine-read observability.** **No** `engine_memory`/`engine_reads` line, no
   VirtualQuery or fallback counter, no record of `X3M_ENGINE_READS` anywhere in
   the log. The read path is inferred from the launch, not measured.
3. **Scene hook.** Agree **162**, None 18, **Disagree 0**; all 162 resolves
   `resolved_by_source=hook`; `draws_after_hook` 0 everywhere; **`rs_resyncs` 0
   (run 6: 24)** with a 307,334/307,334 shadow hit rate; zero apply/restore
   failures; no disagreement record, no unwind, no fail-closed.
4. **TAA.** 162/162 attempted = resolved = **with history** (run 6: 203/214);
   history match **99.83 %** (run 6 99.16 %); 0 cuts, 0 camera cuts, 0 resets;
   readbacks pass on counters, displacement (max 4.80 px, 0 suspicious) and
   depth-image integrity (0 `valid_motion_without_depth`); **83 line kinds, no
   new warning or error kind.**
5. **gz buffer / loads.** Savegame: 14,461,803 calls → **175 real reads**,
   45.75 MB, 99.26 % small calls, 3.16 bytes/call, no amplification, 0 errors;
   save-load zlib 6.2 s of 35.7 s. Loads: **menu 8.386 s, save 35.707 s, sector
   5.944 s** (return to menu 7.947 s).

### Flagged as regressions or gaps

* **Verification coverage lost** (the one real regression): `capture_frames=1`
  and no `--taa-debug` leave `row_consistency`, `temporal_coverage`, the
  previous-depth agreement, `static_consistency`, `history_pairing` and every
  image check **unavailable**. The review-25 build has no per-pixel motion
  certification. *Next run needs 4-frame bursts and `--taa-debug`.*
* **Route cost is now unmeasurable from inside.** 11.65 of 11.89 µs/draw is
  unattributed (run 6: 19.83 of 34.44). One run with `X3M_TELEMETRY_DRAW=1` and
  nothing else changed would localise it at a known ~1 µs/draw stamp cost.
* **`taa_run` 1.20× more expensive per call than run 6** (380.7 vs 316.8 µs) and
  `route_fill` 1.06× — small in absolute terms (3.1 % of the frame) but
  measured, and the two runs flew different paths, so it needs a matched-draw
  measurement before it is called a regression.
* **No engine-read telemetry** (item 2 above).
* **Camera rotation never left the noise floor** (max 1.6258° against a
  0.72–1.63° self-rotation floor), so this run cannot confirm reprojection under
  rotation; the 20° cut threshold was never approached (run 6 reached 8.73°).
* `analyze_iteration09_run2.py --text` still raises on a run without
  `mesh_cache` metrics (`native_seconds` None) after writing its JSON — the same
  defect iteration 10 recorded, now hit for the second time.

Tracked artefacts: `verification/results/iteration-11.json` /
`iteration-11.txt` (this document's tables), `iteration-11-cost.txt`,
`iteration-11-run9-health.json` / `.txt`. The logs, readbacks and the
multi-megabyte readback/TAA summaries stay untracked.
