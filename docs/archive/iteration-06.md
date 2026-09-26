# Iteration 6: first gameplay run of the live motion route

The reviewed motion-route build is installed and the user completed a gameplay
run with the route, the ownership wrapper and both object observers active. The
installed DLL has not been replaced by follow-up source work. No gameplay was
launched by the agent, and no `src/` change was made for this report.

DLL SHA256: `fb08b324ea8ad6303ab40e346957c8fcfeeeb996a47c679cb12b6b721191b077`
(commit `66d91a4`). Capture log SHA256
`4ea7d9119182e65198d96e6688bf34aa575f1fe254d9eecdf2cfbc9efb7affbd`,
383,673,249 bytes (`session-20260912-032315-1464.log`). The log, the 68
`motion_1_<frame>.rgba32f` readbacks (15,728,640 bytes each) and the shader
dumps stay untracked.

## Configuration

```sh
python3 tools/manage.py launch --direct --ownership --object-trace \
    --object-lifetime --motion-output --telemetry \
    --capture-start 999999 --capture-frames 4
```

1280x768 windowed, present interval 1, `NVIDIA GeForce 8800 GTX` as reported by
the CrossOver Preview backend. Witnesses in the log: `ownership_factory
mode=wrapped` with no fallback, `object_trace active=1 status=active`,
`object_lifetime active=1 status=active_without_baseline baseline_complete=0`,
`motion_output_device ... enabled=1 reason=ok ... color_errors=0
motion_errors=0 mrt=4 vs_constants=256 history_available=1
history_capacity=4096`, one `motion_output_target device=1 width=1280
height=768 create=00000000`, 47 `motion_output_variant` records (23 distinct
`(stage, program)` pairs, every one `transform=0 create=00000000`). Depth copy
and scene-depth capture were not requested, so all 69 `ownership_copy_depth`
records report `status=00000001 requested=0`, as expected.

## What the user did

Several sectors were visited, one ship was shot, and **F8** was pressed 17
times, producing 17 bursts of four consecutive frames (68 captured frames,
68 readbacks, all present and all `result=00000000`). The session ran for about
571 s of instrumented wall clock and 17,562 presents.

Exact burst-to-action correspondence is not inferred. What the evidence does
establish per burst is in the tables below: seven bursts observed a routed
scene, two more observed a routed scene at low draw counts, and eight bursts
observed no routed draw at all.

## Reproduction

```sh
python3 tools/analysis/analyze_iteration06_motion.py <log> \
    --captures <captures dir> \
    --output verification/results/iteration-06-motion-summary.json \
    --text verification/results/iteration-06-motion-summary.txt

python3 tools/analysis/analyze_motion_readback.py <log> \
    --readback-dir <captures dir> --label iteration06 --no-draw-details

python3 tools/analysis/crosscheck_motion_origin.py <log> \
    --readback-dir <captures dir> --previous 2793 --current 2794

python3 -m unittest verification/analysis/test_iteration06_motion.py   # 17 cases
python3 -m unittest verification/analysis/test_motion_readback.py      # 15 cases
```

Results: [`iteration-06-motion-summary.json`](../../verification/results/iteration-06-motion-summary.json),
[`motion-readback-iteration06-summary.json`](../../verification/results/motion-readback-iteration06-summary.json)
and `motion-readback-iteration06.txt`,
[`iteration-06-origin-crosscheck.txt`](../../verification/results/iteration-06-origin-crosscheck.txt).

## Route evidence

361 `motion_output_frame` records (68 captured frames plus every 60th frame).
Totals: 79,298 draws seen by the route, 17,390 routed, 17,346 matched, gate
histogram `gate1=0 gate2=55,129 gate3=1,367 gate4=5,412 gate5=0 gate6=44`.
**Zero** `apply_failures`, **zero** `restore_failures`, no
`motion_output_fill_failed`, no `motion_output_apply_failed`, no
`motion_output_restore_failed`, no `motion_output_reset`, and no nonzero
HRESULT anywhere in the session. `latched=1 filled=1 committed=1` on 352 of 361
records; the nine exceptions are the periodic samples at frames 0, 60, 120,
180, 240, 300, 360, 420 and 17,520, i.e. the menu/loading frames before the
first save was in view and the frame after the last burst, where no scene was
latched and nothing was committed. `selector_state` is `Rejected` on all 361
records: that is the route selector's state *at Present*, after the overlays,
and is unrelated to whether the Scene phase was entered during the frame.

The 10,517 per-draw `motion_route` records of the captured frames decompose as:

| gate | draws | meaning |
| --- | ---: | --- |
| 0 (matched) | 7,883 | variant bound with real previous rows |
| 3 (pair) | 398 | scene draw whose VS/PS pair has no reviewed profile row |
| 4 (draw state) | 2,194 | opaque-state gate rejected the draw |
| 5 (scope) | **0** | object/camera scope was known for every draw that reached it |
| 6 (history) | 42 | key had no previous entry |

Matched/routed = 7,883/7,925 = **99.47%**. Gate 5 never failed: the two object
observers supplied a known node and camera lifetime for all 7,925 draws that
reached them, despite `baseline_complete=0`. The 762 distinct keys observed
had **zero** duplicates inside any single frame, and 761 of them recur across
frames — the key discrimination the
[key validation](../reverse-engineering/motion-history-key.md) predicted holds
in live gameplay.

Gate-4 rejections split cleanly into two families, using the capture's own
per-draw render states and the DLL's evaluation order:

| first failing predicate | draws | co-occurring states |
| --- | ---: | --- |
| `alpha_test` | 1,546 | ALPHATESTENABLE 1, COLORWRITEENABLE 7, Z write 1 |
| `z_write` | 648 | ZWRITEENABLE 0, ALPHABLENDENABLE 1, COLORWRITEENABLE 7 |

All 2,194 gate-4 draws also carry `COLORWRITEENABLE=7` (RGB, no alpha), while
every one of the 7,883 matched draws carries 15. The bound `i0.x` was 0
(2,170 draws), 1 (16) or 3 (8) — **always inside the permitted `[0, 8]` light
loop range**, so the integer-register clause never rejected a draw in this
session.

The 42 gate-6 misses all reconstruct to `key_absent_in_previous_frame` and sit
in exactly three frames of burst 2 (2245: 5, 2246: 17, 2247: 1), three of
burst 3 (2504: 5, 2505: 7, 2506: 6) and one of burst 13 (14264: 1) — the same
bursts whose matched rate is below 1.0. These are objects entering the view or
changing LOD/mesh between frames, which is the intended sentinel case.

### Why eight bursts routed nothing

All 68 captured frames have the same structural frame (color+depth Clear,
background draws, depth-only Clear, scene draws, depth unbind, three
ColorFills, one StretchRect, the four-draw bloom chain, depth rebind,
overlays). **No captured frame is a menu-only frame.** The 32 frames that
routed nothing were rejected by the route's own `SceneBoundarySelector`, which
fails closed: one unrecognized event ends the frame's scene and every later
draw is counted at gate 2.

| cause | frames | detail |
| --- | ---: | --- |
| Background phase, unreviewed pair `f80f7af59b667bb7`/`d6f6ba4fee1cd53e` | 20 | `vs_2_0`/`ps_2_0`, `shader/2_0/moon.fb` |
| Scene phase, null pixel shader with `c78b4c68a87fce74` | 8 | `vs_1_1`, `shader/1_1/z_only.fb` — a z-only prepass draw |
| Background phase, unreviewed pair `72f8dbb8567bbf88`/`00fcc903c7f085d5` | 4 | `vs_2_0`/`ps_2_0`, `shader/2_0/planet_v.fb` — **the two newly dumped programs** |

`SceneSignatures::background` lists only three pairs
(`7b6393fe…/6109cf64…`, `37c34a74…/5f82ecac…`, `be199829…/cd6d6eb4…`).
A sector whose background includes the moon or the planet_v effect therefore
loses motion output entirely, and `SceneBoundarySelector::scene_draw` requires
both stages bound, so a single z-only depth-prepass draw inside the scene ends
it. Together these two rules cost 47% of the captured frames.

## Readback evidence

`analyze_motion_readback.py` on all 17 bursts: **PASS** (exit 0), 68 frames,
68 readbacks, 2,673,640 valid pixels, 64,173,080 sentinel, 0 zero-alpha, 0
out-of-ABI, 0 nonfinite.

| burst (frames) | routed/frame | matched/frame | valid px fraction | median displacement px | p99 | max | temporal coverage |
| --- | --- | --- | --- | --- | ---: | ---: | --- |
| 2020–2023 | 252 | 252 | 0.0737 | 0.001 | 0.01 | 2.96 | 0.9999 |
| 2244–2247 | 308–325 | 303–323 | 0.0725–0.0733 | 0.113–0.324 | 15.45 | 36.36 | 0.9940–0.9944 |
| 2503–2506 | 312–330 | 312–324 | 0.0826–0.0832 | 0.001 | 1.59 | 1.80 | 0.9933–0.9937 |
| 2630–2633 | 341 | 341 | 0.0469–0.0477 | 0.189–1.016 | 2.22 | 2.34 | 0.9870–0.9879 |
| 2793–2796 | 318–341 | 318–341 | 0.0678–0.0718 | 1.45–8.41 | 14.73 | 15.36 | 0.9850–0.9855 |
| 2996–2999 | 274–282 | 274–282 | 0.0637–0.0650 | 0.068–0.332 | 7.45 | 7.76 | 0.9849–0.9868 |
| 5322–5325 | 18 | 18 | 0.1036–0.1050 | 0.002 | 2.08 | 2.23 | 0.9949–0.9954 |
| 6202–6205, 7189–7192, 7511–7514, 10921–10924, 13399–13402, 15049–15052, 15127–15130, 17379–17382 | 0 | 0 | 0.0000 | – | – | – | – |
| 14262–14265 | 60–61 | 60–61 | 0.0816–0.0829 | 0.056–0.977 | 27.10 | 32.63 | 0.9960–0.9965 |
| 14391–14394 | 59 | 59 | 0.0811–0.0835 | 0.104–0.239 | 14.14 | 24.03 | 0.9949–0.9955 |

Per check:

* `readback_integrity` **pass** on all 68 frames; `counter_consistency`
  **pass** on all 68 (every `motion_route` line reproduces the DLL's
  `routed`, `matched` and `gate3..6` counters); `history_pairing` **pass**
  over 5,941 predictable draws with **0 disagreements**.
* `row_consistency` **pass**: 27 moving frames, 1,022,880 sampled pixels,
  **0 unexplained** at 0.5 px, maximum explained error **0.149 px**
  (frame 2998); every other frame is below 0.014 px.
* `displacement` **pass**: 0 suspicious pixels at the 64 px bound; the largest
  single displacement in the session is 36.36 px (frame 2245).
  1,904 valid pixels of 2.67 M point at a previous UV outside `[0,1]`, i.e.
  disocclusion at the screen edge, which the resolve must reject anyway.
* `temporal_coverage` **pass**: 27 pairs evaluated, 22 with the criterion
  applied, worst covered fraction **0.9849** (≥ 0.9).
* `static_consistency` **unavailable** — see the anomaly below.
* `depth` **unavailable**, as documented: no capture writes
  `depth_<device>_<frame>.r32f`.

The analyzer needed no fix: it handled the 384 MB log and 1 GB of readbacks in
64 s, and its 15 unit tests still pass.

## Independent cross-check

`crosscheck_motion_origin.py` shares no code with the readback analyzer. For
frames 2793→2794 (the burst where the camera turned, 8.4 px median
displacement) it pairs the 341 matched draws by their logged key, projects each
draw's **object origin** — the translation column `(c24.w, c25.w, c26.w,
c27.w)` divided by `w` — with the current and the previous submitted rows, and
compares that screen displacement with the 3×3 median of the readback's own
`uv_prev·size − (px+0.5, py+0.5)` at the origin pixel.

* 341 of 341 matched draws pair across the two frames.
* 275 draws have a predicted origin displacement above 0.5 px. **275 of 275
  agree in sign on both axes.**
* Magnitude ratio measured/predicted: median **1.0006**, range 0.9836–1.1241.
* Absolute magnitude difference: median **0.0056 px**, maximum 0.96 px.
* Largest case: draw 286 at pixel (1278, 305), predicted (−11.216, +4.805) px,
  measured (−11.228, +4.802) px.

Repeating it for 14264→14265 (39 scored draws): sign agreement 39/39, median
ratio 1.0001, median absolute difference 0.0742 px, maximum 0.32 px.

The residual is expected and bounded: the origin pixel's *surface*
displacement differs from the *origin's* displacement under perspective and
rotation, exactly as [motion-readback](../verification/motion-readback.md) records for the
seam fixture. Sign and magnitude agree, so the readback's displacement field is
attributable to the draws the log says produced it.

## Shader models in use

Every program the session created was resolved from the first DWORD of its own
`{vs,ps}_<hash>.bin` dump; effect paths come from the untracked archive alias
table. 61 programs: `ps_3_0` 28, `vs_3_0` 10, `vs_1_1` 8, `ps_1_1` 6,
`vs_2_0` 5, `ps_2_0` 4. Draws in the 68 captured frames, by phase:

| phase | draws | (VS model, PS model) |
| --- | ---: | --- |
| background | 393 | `vs_1_1/ps_1_1` 344, `vs_2_0/ps_2_0` 49 |
| main scene | 20,076 | `vs_3_0/ps_3_0` **18,605 (92.7%)**, `vs_1_1/ps_null` 915, `vs_2_0/ps_2_0` 306, `vs_1_1/ps_1_1` 250 |
| bloom | 272 | `vs_3_0/ps_3_0` 272 |
| post-bloom overlays / GUI | 948 | `vs_1_1/ps_1_1` 748, `vs_2_0/ps_2_0` 200 |

No menu-only frame was captured, so that column is empty by observation, not by
assumption.

The programs below SM3 that draw **in the main scene**:

| VS / PS | model | scene draws | effect (catalogue) |
| --- | --- | ---: | --- |
| `c78b4c68a87fce74` / *null* | `vs_1_1` / – | 915 | `shader/1_1/z_only.fb` (depth prepass) |
| `d5e1c75351ed3f04` / `8360f422de08b5bd` | `vs_2_0`/`ps_2_0` | 294 | `shader/2_0/effects.fb`, `engine.fb` |
| `36f98d151fd6b0c6` / `222bee0defcb1852` | `vs_1_1`/`ps_1_1` | 68 | `shader/1_1/particles.fb` |
| `5e484a06672e28fb` / `0a523f33ac47ae05` | `vs_1_1`/`ps_1_1` | 68 | VS `shader/1_1/effects.fb`; PS `shader/1_1/stardust.fb`, `gui2d.fb` |
| `803ebfd17f79e413` / `652a7c5d1e9909a0` | `vs_1_1`/`ps_1_1` | 60 | `shader/1_1/z_only.fb` |
| `5e484a06672e28fb` / `ec1f5c4a2f4e1445` | `vs_1_1`/`ps_1_1` | 54 | `shader/1_1/effects.fb`, `engine.fb` |
| `ac2319bc3953efc6` / `03a16e5c63daa6e8` | `vs_2_0`/`ps_2_0` | 12 | `shader/2_0/adeffects.fb` |

Reading for the replacement decision:

* The only SM1/SM2 programs that matter structurally are the two **z-only**
  vertex programs (975 scene draws). They write no color, so they need no
  motion variant — but `SceneBoundarySelector::scene_draw` currently treats a
  null pixel shader as an unrecognized event and kills the frame. The fix is in
  the selector, not in a hand-authored SM3 replacement.
* Everything else below SM3 in the scene is transparent or additive: engine
  glow, particles, stardust, ad effects. All 110 + 36 + 36 gate-3 draws from
  these pairs also fail the gate-4 opaque test (blend on or Z write off), so
  they would keep the sentinel even with a profile row. No hand-authored SM3
  replacement is justified by this session.
* The background and overlay phases are entirely SM1/SM2, as expected; they are
  outside the scene and keep the sentinel by design.

## Acceptance criteria (motion-readback.md)

| # | criterion | result |
| --- | --- | --- |
| 1 | integrity, counters, history pairing pass on every captured frame; no nonfinite/out-of-ABI pixel; no valid pixel in a `matched=0` frame | **met** — 68/68, 0 nonfinite, 0 out-of-ABI, 5,941 predictable draws with 0 disagreements |
| 2 | `static_consistency` on at least one static frame, 0 violations at 0.01 px | **not met** — no frame has bit-identical rows; see anomaly 1 |
| 3 | `row_consistency` over ≥ 3 moving frames and ≥ 50,000 sampled pixels, unexplained ≤ 1% at 0.5 px | **met** — 27 frames, 1,022,880 pixels, 0 unexplained, max error 0.149 px |
| 4 | `displacement` suspicious ≤ 1% at 64 px, medians/p99 plausible and reported | **met** — 0 suspicious; medians 0.001–8.41 px track what the camera was doing |
| 5 | `temporal_coverage` ≥ 0.9 on every pair where the criterion applies, ≥ 2 such pairs | **met** — 22 pairs, worst 0.9849 |
| 6 | colour bit-identity with routing disabled | **not evaluated here** — no disabled-route run of this scene exists; still required |
| 7 | `depth` stays unavailable | **as expected** — unavailable |

**The run does not clear the gate as written**: criterion 2 is unmet and
criterion 6 has no evidence in this session.

## Anomalies

1. **No static frame exists in the sense the criterion requires.** Three bursts
   (2020–2023, 2503–2506, 5322–5325) are visually stationary — median
   displacement 0.001–0.002 px and p99 0.007–0.01 px in burst 1 — yet the
   submitted rows are never bit-identical between consecutive frames, so the
   analyzer labels them `moving` and the static check stays `unavailable`. The
   residual is floating-point recomputation of the row set, not motion: burst 1
   reports a static max error of 2.78 px over 459 of 72,453 pixels, from a
   single genuinely moving object in view. Requiring bit equality is therefore
   probably unsatisfiable in gameplay; the criterion should be restated in
   measured pixels (for example "a frame whose matched draws all move less
   than 0.01 px"), or a docked view with the ship's own geometry withheld must
   be captured.
2. **47% of the captured frames produced no motion at all** (32 of 68), for the
   two selector reasons tabulated above. This is the largest correctness gap in
   the run and it is entirely in the recognizer, not in the route.
3. **The session did not shut down.** The log ends on a complete record, after
   a routine one-second telemetry window, with no `motion_output_release` and
   no `device_destroy` (the release hook's records), so
   the final-Release probe and the teardown counters are unverified in
   gameplay. The fixture covers them; a gameplay witness still requires a run
   that quits through Esc → quit to desktop.
4. **`object_lifetime` ran `active_without_baseline`** (`baseline_complete=0`,
   `baseline_entries=0`) for the whole session. It nevertheless resolved scope
   for every draw that reached gate 5, so the route is not blocked, but the
   baseline path is untested in gameplay.
5. **Two very large `frame_normal` intervals** (93.7 s and 30.8 s, plus 41
   intervals above 100 ms) are load stalls inside the normal distribution. They
   are not route cost; they make the `frame_normal` mean unusable as a frame
   time. Only the buckets and the medians of individual windows are meaningful.
6. **Camera-serial and epoch behaviour is thinner than expected.** Across the
   whole session `load_epoch` stayed 1 and `registry_epoch` stayed 2; the
   camera serial changed exactly once, 29,523 → 32,904, somewhere between frame
   5,325 and frame 14,262 (five bursts routed nothing in between, so the change
   cannot be localized further). Sector changes and the ship destruction
   produced **no observable load/registry epoch change**, which means
   temporal-history invalidation cannot currently rely on those epochs.

## Cost observations

CPU-side wall clock only, never GPU time; see
[telemetry](../verification/telemetry.md) for what these numbers are and are not.

| metric | n | mean | max |
| --- | ---: | ---: | ---: |
| `frame_normal` (interval between presents) | 17,476 | 31.0 ms (inflated by load stalls) | 93.7 s |
| `frame_capture` (F8 frames) | 85 | 300.4 ms | 1.40 s |
| `present_normal` | 17,494 | 14.7 µs | 527.7 µs |
| `present_capture` | 68 | 34.9 µs | 68.6 µs |
| `draw_backend` | 3,498,808 | 2.70 µs | 17.9 ms |
| `lock_wait` (global capture mutex) | 10,057,622 | 0.19 µs | 150.5 µs |
| `capture_cpu` / `snapshot` (F8 frames only) | 22,437 / 21,689 | 945 µs / 875 µs | 14.5 ms |

* `frame_normal` buckets: 1,990 intervals ≤ 10 ms, 15,445 ≤ 100 ms, 41 longer.
  The playable steady state is the 10–100 ms bucket.
* The route's setter hooks share the existing global capture mutex. It was
  acquired 10.06 M times for a total of 1.92 s in a 571 s session — about
  **0.34% of wall clock in acquisition delay alone**, at 17.6 k acquisitions
  per second. This is the one measurable per-draw cost the route adds, and it
  is small but no longer negligible; it is the first thing to measure again if
  the hook set grows.
* Splitting the 1-second telemetry windows by whether the nearest
  `motion_output_frame` was routing gives `frame_normal` 23.0 ms (n = 7,017)
  while routing and 36.4 ms (n = 10,459) while not. **This does not measure the
  route**: the non-routing parts of the session are menus, maps and loads with
  different scene content, and the route was enabled throughout. No metric in
  this session isolates route cost; that needs a paired run with
  `--motion-output` off.
* The F8 hitch is real and expected: each press reads back and writes 15.7 MB.

## Concrete implications

Before wiring the temporal resolve:

1. **Widen the background signature and admit z-only draws.** The selector must
   accept `72f8dbb8567bbf88`/`00fcc903c7f085d5` (`shader/2_0/planet_v.fb`) and
   `f80f7af59b667bb7`/`d6f6ba4fee1cd53e` (`shader/2_0/moon.fb`) as background
   families, and `SceneBoundarySelector::scene_draw` must tolerate a draw with
   a null pixel shader (z-only prepass) instead of rejecting the frame. Without
   both changes, nearly half of the captured gameplay frames produce no motion.
   Both are recognizer changes with no effect on what routes.
2. **One profile row is clearly worth adding:** `4944d81dfe531b37` (already a
   reviewed Argon VS) paired with `3006f8030a467739`
   (`ps_3_0`, `shader/3_0/split.fb`) — 208 gate-3 draws in 16 frames, **all
   208 opaque and passing every gate-4 state predicate**, so they would route
   and match today if the pair had a row. The remaining unclassified pairs are
   not worth a row: `d5e1c75351ed3f04`/`8360f422de08b5bd` (110 draws),
   `36f98d151fd6b0c6`/`222bee0defcb1852` (36) and
   `5e484a06672e28fb`/`0a523f33ac47ae05` (36) are all blended or Z-write-off
   and would still fail gate 4; `c78b4c68a87fce74`/null (8) is the z-only pass.
3. **Do not key temporal invalidation on the load/registry epoch.** Neither
   changed in a session with several sector changes and a ship destruction.
   The camera serial did change once and is the only identity signal this run
   exercised; history behaved correctly across it (100% matched before,
   99.59% in the first routed burst after, the single miss being a
   `key_absent_in_previous_frame`). Invalidation needs a signal that is
   actually observed to move — or the resolve must rely on its own depth and
   colour rejection.
4. **Restate acceptance criterion 2** in measured pixels, or capture a docked
   view. As written it cannot be met by gameplay.
5. **Get a clean shutdown and a routing-off comparison run.** Criterion 6
   (colour bit-identity) and the gameplay final-Release probe both remain
   unevidenced. One additional run with the same save, quitting normally, plus
   one with `--motion-output` omitted, closes both and also gives the only
   honest route-cost measurement.
6. **Write the depth readback** if criterion 7 is ever to become a check
   rather than a waiver; the resolve otherwise relies on its own depth
   rejection, as the document already notes.

Nothing in this run argues against the route itself: where a scene was
recognized, 99.47% of routed draws matched, every pixel obeyed the ABI, the
row-pair reconstruction explained all 1,022,880 sampled pixels within 0.149 px,
and an independent origin projection agreed in sign on 314 of 314 scored draws
across two bursts.

## Selector correction (addendum)

Anomaly 1 was fixed in the recognizer, as the implications above required, but
structurally rather than by widening the allowlist: the Background phase now
accepts every successful draw on the latched color/depth pair with a full
viewport until the first depth-only Clear of that pair, and the Scene phase
tolerates a draw with a null pixel shader without counting it as the required
depth writer. All other gates (initial Clear shape, no background draw, a
second depth-only Clear inside the scene, copy, fills, the four-draw bloom
chain, rebind, final Clear) are unchanged. Details and evidence are in the
[selector notes](../reverse-engineering/scene-boundary-selector.md).

Replaying this session's 68 captured frames through the previous and the
corrected header (`tools/analysis/replay_scene_boundary.py --baseline-header`,
report `verification/results/scene-boundary-replay-iteration06.json`): the
previous header rejects exactly the 32 frames tabulated above, with the same
three causes at the same draws; the corrected header selects all 68 at the
depth-only Clear that follows background + scene + the four bloom draws, and
the 36 frames that were already selected keep their boundary event. The
iteration-05 log replays unchanged (24 selected at the events the live adapter
had confirmed, 4 menu frames rejected at event 1), as do the twelve 0.3 fixture
frames. The recovered frames have not been re-run in the game; the readback
figures in this document are from the original session with the old selector.
