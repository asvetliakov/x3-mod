# Completed user runs: commands and instructions

Moved from `docs/verification/user-runs.md` on 2026-09-14. Each section is the
launch command and instructions as issued for that run, kept as provenance.
Links are relative to this directory.

## 9. Stronger glow and selection/voice timing — Completed

[Run 28 observations and analysis](../verification/run28-glow-materials.md) preserve the user's
bloom ON/OFF screenshots and docking-port dark-to-bright reproduction. The user
also recalls this material symptom elsewhere. The command and steps below are
provenance, not a rerun request.

Installed source `d9413fc` raises authored glow from 0.10 to 0.35, defaults to
Auto capped at +1.5 EV, and adds timings inside target notification and voice
playback. Camera values and material coverage stay as accepted. This build
measures the selection pause; it does not claim to fix it or distant shimmer.
Keep the **game's Glow enabled**.

```sh
./x3run --direct --camera chase --ownership --object-trace --object-lifetime \
  --motion-output --taa --telemetry --game-phases \
  --taa-sharpen 0 --taa-mip-bias 0 \
  --hdr --hdr-tonemap --hdr-bloom --linear-materials \
  --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast \
  --capture-start 999999 --capture-frames 4
```

1. Load the usual save and settle. Select several different ships/objects, about
   five seconds apart; then select the same targets again. Note whether the pause
   differs on repeat selection and whether target speech plays around the pause.
2. Face an engine emitter or station lights. Compare **Ctrl+Shift+F10** OFF/ON
   without moving. Report whether glow strength and spread now look right;
   screenshots are useful if it is still weak, excessive or oddly shaped.
3. Exit normally. No F8 captures, sector changes or full camera retest are needed.
   `x3run` saves the log automatically. Auto/+1.5 is the new default;
   Ctrl+Shift+F9 still compares with fixed EV 0 if desired.

One short session combines both checks. The command passes `--dry-run`; the
agent has not launched the game.

## 8. Restored glow, milder exposure and selection trace — Completed

[Completed run 27 analysis](../verification/run27-glow-selection.md); snapshot `/tmp/x3-bottleX3-run27/`. Bloom is visible but too subtle; +1.5 EV
looks good. Selection pauses, distant asteroid/station shimmer, and occasional
dark-to-bright object changes remain. The agent is analyzing these together
before requesting another session. The command below is provenance, not a rerun request.

Installed source `541e380` includes the authored-glow correction, 162 material
pairs and native phase diagnostics. Camera values stay as accepted. Keep the
**game's Glow enabled**. The command was validated with `--dry-run`; the agent
has not launched the game.

```sh
./x3run --direct --camera chase --ownership --object-trace --object-lifetime \
  --motion-output --taa --telemetry --taa-debug --game-phases \
  --taa-sharpen 0 --taa-mip-bias 0 \
  --hdr --hdr-tonemap --hdr-exposure auto --hdr-ev-max 1.5 --hdr-bloom --linear-materials \
  --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast \
  --capture-start 999999 --capture-frames 4
```

1. Load the usual save and let it settle. Select several ships/objects, leaving
   about five seconds between selections. Report whether the pause remains.
   Keep this portion free of F8 captures so capture work does not obscure timing.
2. Face an engine emitter or station lights and compare **Ctrl+Shift+F10** OFF/ON
   at the same view. Take screenshots if the glow still appears missing or odd.
3. Compare **Ctrl+Shift+F9** AUTO with fixed EV 0. Auto is now capped at +1.5 EV
   for this run; give it roughly five seconds to settle. Compare a dark view,
   a bright nebula and a nearby hull. Report which looks better and any pulsing.
4. Take F8 captures in each exposure mode, then fly/turn briefly and exit normally.
   Report obvious material/gloss or shimmer problems; no full camera retest is needed.

This is one session. `x3run` saves its log/captures after exit. F8 images precede
final bloom, so screenshots are the useful glow evidence. F10 OFF still executes
the filter and is not a performance baseline. The general exposure default stays
fixed 0; this run evaluates the milder Auto option before changing that policy.



## 1. Camera correction plus loading verification — Completed as run 18

Completed: aiming works, the 13° angle is approved, and no trembling was noticed.
The [saved loading verification](../verification/run18-camera-loading.md) passed; do not repeat this run.
Distance/response tuning, the missing predictive aim indicator and sector-change
view persistence will be combined into a later camera check when ready.

Command used:

```sh
./x3run --direct --camera chase --telemetry \
  --resource-read verify --dat-handles \
  --mesh-adjacency verify --mesh-adjacency-dump
```

Load the usual save and let the scene settle. Fly straight, turn gently, stop,
switch once between internal and external back view, and briefly test a target.
In chase view, fire with the cursor left, centre and right; the completed
first-person diagnostic need not be repeated in full; briefly confirm it still
aims correctly. Check that trembling stays absent,
the ship top/framing/distance feel right, and shots follow the cursor. If practical,
open/close a menu, toggle SETA, and change resolution once to check camera survival.

Report whether the load completed, visible camera or firing problems, and any
crash/hang. Log analysis must find meaningful reader and adjacency verify work,
zero admitted mismatches and no hook/fault failures. Verify mode deliberately
runs native and candidate work together, so this run provides **no loading-time
or FPS attribution**.

## 2. Sharpen 0.75, mip bias -0.5 and chase/TAA cuts — Merged into run 6

Do not launch a separate run for this item. Run 6 applies the same sharpen and
mip-bias settings to both sides of its fixed-exposure material comparison and
includes the required motion and view-cut checks.

## 3. Space-aware exposure and bloom comparison — Completed

Part A is complete as run 24; do not repeat the baseline. Its log and all 155
referenced artifacts are preserved. Automatic exposure is active, and the user
reports selection stutter with chase disabled and bright backgrounds without
the earlier severe overexposure. [Analysis](../verification/run24-exposure-baseline.md) confirms
near-permanent +2 EV; the [reviewed policy](../architecture/space-exposure-policy.md)
selects fixed EV 0 for the next default, with Auto retained for comparison.
B is complete as run 25, with all 174 referenced artifacts preserved. The user
noticed no visual bloom difference; [analysis](../verification/run25-bloom-comparison.md) shows
that bloom never attached because the game requested a pure D3D9 device.
The source fix is reviewed and installed; actual bloom execution needs run 7. Do not repeat either command for
this item. Separate runtime exposure
and bloom toggles are now installed for run 7. Commands below remain as provenance
for the old Auto-default build.

Use the current installed build; no update is needed. Keep sharpen and mip bias
at zero so this remains comparable to the earlier HDR baseline. Both commands
include the accepted loading accelerators and use the vanilla camera mode:

```sh
./x3run --direct --ownership --object-trace --object-lifetime --motion-output --taa \
  --telemetry --taa-debug --taa-sharpen 0 --taa-mip-bias 0 \
  --hdr --hdr-tonemap \
  --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast \
  --capture-start 999999 --capture-frames 4
```

Keep the game's **Glow enabled**. In this baseline, capture settled dark space,
a bright object/emitter, and a turn between them. Do not set manual EV. Exit,
then repeat the same save and camera positions with bloom enabled:

```sh
./x3run --direct --ownership --object-trace --object-lifetime --motion-output --taa \
  --telemetry --taa-debug --taa-sharpen 0 --taa-mip-bias 0 \
  --hdr --hdr-tonemap --hdr-bloom \
  --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast \
  --capture-start 999999 --capture-frames 4
```

Repeat the F8 samples. Report exposure pumping/adaptation, bright and dark
detail, glow around emitters, HUD/menu readability and any obvious slowdown.
Also select a target once and report whether the stutter occurs with chase
disabled. After the comparison, change resolution once if practical. F8 records the
scene/exposure inputs before bloom; a screenshot is useful for the final glow.
Tell us which run was baseline/bloom and which bursts were captured. Log analysis
must confirm `bloom_prepare ready=1` and `bloom_commit committed=1`; a silent
fallback is not bloom acceptance. This combines the former separate bloom run
with exposure acceptance. It does not yet establish game FPS or real radiance.

## 5. Reader and adjacency fast modes — Completed as run 19

[Run 19](../verification/run19-fast-loading.md) confirms all intended fast routes with no faults:
4,096 compressed resources, 3,516 reused DAT opens and 7,642 meshes. The user
reported no stutters or issues. The 41.571 s save load is not a controlled speed
comparison; the crypto cache was off. Do not repeat this standalone run.
Run 6 includes the reviewed crypto cache and all three fast paths on both sides.
The read-ahead buffer is also enabled because telemetry is active; it reduces
instrumentation overhead, not the engine's decompression or deserialization work.


## 6. Linear materials, chase lead marker and TAA cuts — Completed

This is a fixed-exposure comparison with bloom off. Both sides use sharpen 0.75
and mip bias -0.5 so the material toggle remains the only A/B difference. Use
the usual ship/save with a visible hull or station, emissive panels and, if
available, an active light.

Part A is complete as run 20 (linear materials off). Retain this command for
comparison; do not repeat A:

```sh
./x3run --direct --camera chase --ownership --object-trace --object-lifetime \
  --motion-output --taa --telemetry --taa-debug --camera-log 1 \
  --taa-sharpen 0.75 --taa-mip-bias -0.5 \
  --hdr --hdr-tonemap --hdr-ev-manual 0 \
  --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast \
  --capture-start 999999 --capture-frames 4
```

Completed B command (runs 21–23; do not repeat for this item):
the same save/sequence with linear materials on. Leave the three material gains at their default 1:

```sh
./x3run --direct --camera chase --ownership --object-trace --object-lifetime \
  --motion-output --taa --telemetry --taa-debug --camera-log 1 \
  --taa-sharpen 0.75 --taa-mip-bias -0.5 \
  --hdr --hdr-tonemap --hdr-ev-manual 0 --linear-materials \
  --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast \
  --capture-start 999999 --capture-frames 4
```

In each session, take separate F8 four-frame bursts and screenshots at settled
rest, during a slow turn and while moving or steering. Complete the matched A/B
scene captures before switching between internal and external back views; then
record the view cuts and cross a gate if practical. Use the same active-light
moment on both sides when available.

The new camera build is installed. Combine its follow-up here: check that
the predictive aiming hint appears on a selected target and follows it while
turning. Report the new distance/softer follow. If crossing a gate resets the
view, switch back to chase once; the combined diagnostics will record both
events. Docking is optional. The installed build includes the lead-marker correction
and transition diagnostics; automatic chase-view restoration is not implemented.

Report sharpness, halos, shimmer or flicker, ghosting and recovery after view
transitions. Separately compare hull color and brightness, emissive detail,
active-light response and any obvious slowdown. Both logs are needed even if
the image looks unchanged.

This slice covers 110 reviewed pairs across Argon, shared hulls, Split, Terran
and standard-lighting DEFAULT/BUMPMAP/LOW techniques. It does not cover every ship/effect. Analysis
must confirm nonzero material routes, inspect `bump_routed` to establish whether
the new bump path was exercised, and inspect refusal reasons and captured
constants before judging appearance or expanding coverage. This fixed-EV pair
cannot replace run 3's automatic-exposure/bloom comparison. It establishes the
0.75 sharpen and -0.5 mip-bias behavior on the HDR/AgX route; it does not count
as a separate non-HDR gameplay test.

Run 20 A feedback: loading is visibly faster, the predictive hint is visible,
and camera placement is accepted; keep the current values. Check in B whether
selection causes the same immediate and 1–2-second stutters, and whether the
star-lit distant asteroid shimmers at the same distance. The other selected
object distance/crosshair missing in chase is a separate HUD investigation.
Both parts use fixed EV 0, so absence of overexposure here does not accept the
automatic meter.

Run 6 B is complete. Run 21 froze while docked after alt-tab/back; cause is
unknown and no repeat is requested. Run 22 completed without F8; run 23 completed
with two bursts: distant selected asteroid while shimmering (10752–10755), then
closer after shimmering disappeared (11607–11610). Selection stutters remain.
Paired screenshots show brighter converted hull/station surfaces, partial
coverage, and less apparent gloss. Final gloss/lighting/reflection tuning may
follow later at the user's preference; accidental loss of native terms still
needs exclusion. Keep accepted camera values. The central HUD correction and
selection timings are now installed and combined into run 7. The current run-6 commands remain above as provenance.

## 7. Same-run exposure/bloom and chase HUD — Complete

Completed as [run 26](../verification/run26-comparison.md). Bloom executes but fails visual
acceptance: native emitter glow is missing. Selection stutter remains; the
central chase display is visible in screenshots. A brighter exposure baseline
is being evaluated. **Do not repeat this run yet; wait for the corrected build.**

Candidate `75dbbed` was used with the following command, retained as provenance.
This combines the exposure and bloom follow-up with the missing central chase
display and selection-stutter diagnostics. Camera settings remain the accepted
values. Keep the game's **Glow enabled**.

```sh
./x3run --direct --camera chase --ownership --object-trace --object-lifetime \
  --motion-output --taa --telemetry --taa-debug --camera-log 1 \
  --taa-sharpen 0 --taa-mip-bias 0 \
  --hdr --hdr-tonemap --hdr-exposure fixed --hdr-bloom --linear-materials \
  --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast \
  --capture-start 999999 --capture-frames 4
```

Fixed exposure is the new default (the explicit flag makes this comparison clear).
Automatic exposure is optional. Use the Control key, not Command, for the
comparison shortcuts.

Checks in one session:

1. Load the usual save, switch to chase, and select a ship or station. Check
   whether the central crosshair/distance display is now visible and report
   whether selection still stutters. No extra diagnostic mode changes are needed.
2. Hold Ctrl+Shift, then press F10 to compare enhanced bloom OFF/ON while
   keeping the same view. Keep the game's Glow enabled. The short panel reports
   whether the selected mode actually ran; take matched screenshots if useful.
3. Hold Ctrl+Shift, then press F9 to compare fixed brightness with AUTO. Give
   AUTO about five seconds to settle, then return to fixed. Judge dark space,
   nebula colours and hull highlights, rather than whether the change is obvious.
   F8 captures in each exposure mode help link feedback to the log.
4. Fly and turn briefly, then exit normally so x3run preserves the session.
   Report any new camera, aiming, display or frame-time problem.

F10 OFF preserves the same compositor and removes enhanced bloom contribution;
it is a visual comparison, not a zero-filter-cost performance baseline. F8
captures precede final bloom, so screenshots are the useful final-glow evidence.
A successful log must show actual bloom preparation/commit and correct exposure
mode transitions; a fallback or unavailable panel is not acceptance.

## 11. Fade region route and alpha-tested cutout, combined — Completed

Completed as user run 11, snapshot `/tmp/x3-bottleX3-run36/` (the launcher
numbered the session 36), on installed checkpoint `3f06979`. The command and
steps below are provenance, not a rerun request.

**Result.** The witness logged 543 `fade_witness` lines: 196 sampled
(`sampled=1`) and 347 unsampled, all `reason=no_fade`. Covered pixels outside
the derived rectangles were 0 in every sampled frame (max 0), with
`rects_unprepared`, `overflow` and `lines_truncated` all 0; the aggregated
`f_hist` is 1962, 98, 11, 0, 0, 0, 0, 0, so every admitted fade was at most 5 %
(max `f_permille` 24). The region route logged 2087 per-DIP lines and 100 frame
summaries with 0 full-viewport fallbacks (`full=0`, all `status=bound`);
rectangle area was mean 3367 px², median 2352 px², max 24150 px² against a
983040 px² viewport (≈0.3 % typical, 2.5 % max). The cutout runtime reported
`cutout_caps=1` throughout (277 windows) and `cutout_device verdict=1
result=00000000 mrt=4`, with 7827 routed draws; `cutout_missed` and
`cutout_unavailable` each reached 1 only in the windows covering frames
10500–16200 and were 0 during both F8 bursts. Frame time (`frame_end dt_ms`,
60-frame samples) was n=59 median 4097 µs, p95 10104 µs, max 27040 µs against
run 28 (`/tmp/x3-bottleX3-run28/`) n=99 median 4104 µs, p95 8824 µs, max
26467 µs; the medians are equal and the run-11 tail comes from a smaller sample
with no fade-cost field, so it is not attributable. Five F8 bursts landed on
frames 1476, 1699, 1917, 13681 and 14601, each with `hdr_1_*`, `depth_1_*` and
`motion_1_*` files; the docking-port pair `vs=4944d81dfe531b37
ps=64bac8bb307eb896` ([station material distance](../reverse-engineering/station-material-distance.md))
appears in all five with `motion_route gate=4 routed=0 matched=0`, i.e. it stays
on the native path, so the run-28 finding stands and neither new feature touches
it. No poisoned or evicted regions and no reset/recovery events occurred. The
user reports the docking port still darkening on approach (expected) and
distant asteroids and stations shimmering in motion "like parts of geometry
disappearing", unsure whether it is new; two zoom-view and one normal-view
screenshot were supplied and are not stored in the repository. The witness
evidence does not implicate the region route, so the shimmer is carried as the
open TAA distant-shimmer item with the cause unidentified.

The candidate built from `3f06979` is installed (DLL `4022a3a4…`, record
`verification/results/fade-region-cutout-install.json`); the previous `8442f43`
DLL is retained for rollback. The command adds the distance-fade region route and its witness to the
run-10 enhanced set (no `--voice-decoder`). The alpha-tested cutout runtime has
no flag of its own: it arms whenever linear materials are requested, the cutout
capability is Ready, HDR is enabled and the mip bias is zero
([alpha-tested materials](../architecture/alpha-tested-materials.md)), which is
why `--taa-mip-bias` must not be added here.

```sh
./x3run --direct --camera chase --ownership --object-trace --object-lifetime \
  --motion-output --taa --telemetry --camera-log 1 \
  --hdr --hdr-tonemap --hdr-exposure fixed --hdr-bloom --linear-materials \
  --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast \
  --linear-distance-fade --fade-witness \
  --capture-start 999999 --capture-frames 1
```

`--linear-distance-fade` composes admitted distance-fade draws through an
in-place region bracket; `--fade-witness` (K=30) reads the M coverage target
back every 30th frame and logs `fade_witness` lines counting covered pixels
outside the derived rectangles. Emission stays off.

Load the usual save, fly near asteroids and distant objects, then approach an
Argon station. Report:

1. any visual difference on fading asteroids and distant objects, and whether
   the frame rate is acceptable;
2. one F8 capture during the Argon station approach with the docking port in
   view;
3. whether the docking-port darkening seen in run 28 changes (the cutout
   runtime is not expected to fix it);
4. the session log path printed by `./x3run`.

Acceptance needs zero outside pixels in every `fade_witness` line; analysis
reads those lines and the fade `f` histogram with
`verification/probe/run_linear_distance_fade_live.py`
([region note](../architecture/linear-distance-fade-region.md)) before any
acceptance claim.

## 14. Station source-over linear route, fade region and shimmer trace, combined — Completed

Completed as user run 14, snapshot `/tmp/x3-bottleX3-run39/` (74 referenced files: five F8 captures
at frames 1812, 2071, 2316 on the station approach and 9163, 11940 later; zoom was unavailable, so no
zoom frames), on the installed candidate `76d7750`, run-14 command of the run queue. User report: the
docking port still darkens then brightens with distance.

Analysis (log queried, never read whole): `fade_witness` 407 lines / 238 sampled, zero covered pixels
outside the derived rectangles in every sampled frame, no `rects_unprepared`/`overflow`/truncation;
`fade_region` 2347 lines, all `status=bound`, 0 full-viewport fallbacks; `f_hist` 2267/53/0/0/0/0/0/0.
The docking-port pair (`4944d81dfe531b37`/`64bac8bb307eb896`, exact source-over) was admitted and
composed on the linear route in all five capture frames (`fade_eligible = fade_prepared = fade_linear`
in all 208 `linear_composition_frame` lines, 1179 draws; `fade_refused_rect` wrote no line because no
draw was refused). The coexisting-lighting split is therefore no longer present, and the darkening
persists: across the three approach captures every logged port input (node, model, lod, seven texture
identities, blend/Z/test/mask, fog state and b0, `alpha13c`, every PS constant) is byte-identical; only
the world and view matrix registers change. Depth-masked port luminance moves 5–8 % over 115–3000
pixels, within TAA/dither noise. The evidence supports unchanged-state minification (the mechanism of
`reverse-engineering/asteroid-specular-minification.md`), not a state, fog, material or LOD change; the
magnitude the user sees is not established by the captures. Shimmer trace: armed (12,209 `shimmer_frame`,
131,615 `shimmer_draw` lines) but it never logs the station pair, and no zoom frames exist. `motion_route`
gates: 2346 matched, 104 pair, 165 draw-state; top draw-state signatures: exact source-over 84, alpha-tested
opaque 69. Frame time (45 `frame_end` windows, ms per window, see the unit correction at the end of this file): median 5196, p95 10484, versus 4097 in run 11;
no rank correlation with `shimmer_draw` count (0.07) or fade draws (0.04); frames with 0 fade draws
median 2787 ms per window. The cross-run comparison is confounded by the new shimmer trace and the low sample count.

## 15. Screen emission on bullets (packed policy 8 in the region bracket) — Completed, acceptance failed

Completed as user run 15, snapshot `/tmp/x3-bottleX3-run40/` (82 referenced files; seven F8 captures,
five while firing at frames 10128, 11469, 11967, 15281, 15432), on the installed candidate `76d7750`,
run-15 command of the run queue. User report: the laser bullets look dimmer than in run 14.

Analysis (log 31.7 MB / 252,068 lines, queried): witness clean (587 sampled `fade_witness` lines, all
`outside=0`, no unprepared/overflow/truncation; 0 of 201 `fade_region` lines full viewport).
`locked_prefix_frame` (292 lines): draws 800, bound 400, refused 400, every refusal `reason_w`
(`NonPositiveW`), `lookup_*` 0, so the bounded rate is 50 %, not the expected near 100 %, and the
design's first-draw-per-buffer refusal never fired. `linear_composition_frame` (301 lines):
`packed_eligible` 508 = admitted 400 + unbounded-refused 108, `packed_incomplete` 0,
`packed_caps_refused` 0; 4 admitted brackets per firing frame (median = p95 = max). The admitted
bound rects at the firing captures cover 58–90 % of the 1280×768 viewport (e.g.
`rect=189,28,985,742 f_permille=578`), against 38×47 px for a distant bullet at frame 990, so the
bracket cost is near-fullscreen while firing and bullet pixels cannot be isolated from the scene in
the HDR captures (max channel > 1.0 in every firing capture; no bullet-only reference in run 14).
Frame time: 66 throttled `frame_end` samples; firing windows median 2591 ms / p95 4699 ms, non-firing
2536 ms per window (p95 contaminated by loading; window totals, see the unit correction at the end of this file); too sparse for a per-bracket figure. Fade and docking-port
counters show no refusals under policy 8. Interpretation handed to implementation: the player's own
bullets start at or behind the camera plane, so the locked-prefix extrema have w ≤ 0 (refused, drawn
native) or barely positive w (extrema explode to near-fullscreen rects); the mixed native/packed
result is the likely "dimmer" appearance. Fix: near-plane clipping of the bound in clip space plus a
capture-only per-draw tight-AABB luminance line. No bracket cap is adopted: the data show the bound,
not the count, is the cost driver.


**Unit correction (2026-09-14 evening):** `frame_end dt_ms` is the elapsed time in milliseconds since
the previous `frame_end` line, which is logged every 300 frames or on a capture frame (`capture.cpp`,
`ctx.capture || ctx.frame%300==0`). The figures quoted as "µs" in the run entries above are therefore
window totals in ms, not per-frame microseconds; a 4097 ms window over 300 frames is ≈ 13.7 ms per frame
(≈ 73 fps) on a diagnostic build. Cross-run comparisons of the same field remain valid; absolute per-frame
figures must divide by the actual frame delta between consecutive lines. Recomputed that way: run 14
median 18.2 ms/frame (p95 52.6, shimmer trace on); run 16 median 27.1 ms/frame (p95 84.7, `--voice-decoder
--game-phases --audio-sites`, windows include loading). Diagnostic timings are not game FPS.

## 16. Run 13 retry: speech with the decoder plugin and the fixed DMO fallback hook — Completed

Completed as user run 16, snapshot `/tmp/x3-bottleX3-run41/`, installed candidate `5b92484a…`
(`066e18f`). User report: the game loads and target-name speech works; the voice crackles on some
words or vowels. Log (16,805 lines): hook install line with arena `01d70000`, stub/tail/dispatcher in the
arena and `enter=793e4740` in the DLL image; 6 activations, all `qi_hr=0 init_hr=0`, `retries_ok` up to 6,
no `voice_dmo_fallback_fault` line; `game_phase_audio` monotonic (loops 5107, cue_play 78). No audio-path
telemetry exists in the build, so the crackle is not attributable from the log; the offline replica PCM
dump and scan (`docs/verification/voice-decoder.md`) is the next evidence. Seven `motion_output_frame
hook_outside_scene` frames are menu/loading frames, benign. `frame_end` windows recomputed per frame:
median 27.1 ms, p95 84.7 ms with `--voice-decoder --game-phases --audio-sites` and loading windows
included (run 14 recomputed: 18.2 ms / 52.6 ms with the shimmer trace); diagnostic, not game FPS.

## 17. Bullet bound after near-plane clipping, packed_sample brightness — Completed

Completed as user run 17, snapshot `/tmp/x3-bottleX3-run42/` (75 referenced files), same candidate,
`--screen-emission --fade-witness`, fade route default-on. User report: bullets look overall, perhaps
consistently, dimmer. `locked_prefix_frame` (198 lines): draws 500, bound 500, refused 0, clipped 262,
every refusal reason 0 (run 15: 800 / 400 / 400 `reason_w`); bound rate 100 % while firing (382/382).
Admitted rect `f_permille` at capture frames: median 472 ‰, p95/max 1000 ‰ (6 of 26 rows are clipped
boxes touching the camera, full viewport, as the step D note predicts). `packed_region_pixels` up to
3.9 M px per firing frame. `packed_incomplete`, `packed_unbounded_refused`, `packed_caps_refused`,
`packed_sample_skipped` all 0 over 208 frame lines. `fade_witness` 403 lines, `outside=0` everywhere;
`fade_region_frame` 86 lines, bound 88, no misses or poison. `packed_sample`: 20 of 20 lines have
`post` bit-identical to `pre` at the rect centre (both S_OK), and the HDR captures match `post` at
those points with a maximum channel of 0.59; because the centre of a near-fullscreen box is rarely a
bullet pixel, this does not yet prove a no-op composite. The diagnostic is being extended to report
whole-rect change counts and max/argmax pre/post, plus a per-frame timing line, for run 18. Frame
time is not resolvable per firing frame from the 300-frame `frame_end` windows (≈ 9.9 ms/frame average
over one 83-frame window).

## 18. Voice crackle fix: decoder plugin v4 — Completed

Completed as user run 18, snapshot `/tmp/x3-bottleX3-run46/`, installed DLL `5b92484a…` with plugin
`/tmp/x3-wma-plugin-v4` (backup `~/x3-mod-resume-2026-09-14/artifacts/wma-plugin-v4/`). User report:
no crackling any more, the voice is fine. Target-name speech is therefore working in gameplay through
the plugin path plus the DMO fallback hook, with the decoder float limit removing the stock converter's
full-scale wrap (`docs/verification/voice-decoder.md`). Selection latency (phase join, same caller `0042dd6e` as run 28): no target publication crossed the
10 ms slow-call threshold in run 46 (13 publisher entries) or run 41 (33 entries), against run 28's
10 retained publishers with median 463.5 ms / p95 512.4 ms; the retained samples are 1.24 ms (run 46,
0.83 ms in nested stream creation) and 3.69 ms (run 41). The selection pause is gone with the working
decoder. Per-frame proxy from `frame_end` windows: run 46 29.4 ms, run 41 27.1 ms (both with the site
trace and phase telemetry on); no per-frame regression from the plugin.

## 19. Combined: AO off/on, bullets at gain 1, cutout shimmer fix, same-port far/near pair — Completed

Completed as user run 19, snapshot `/tmp/x3-bottleX3-run47/`, log
`session-20260914-225307-216.log` (69.7 MB), installed DLL `ab6e17ba…` (`5d06316`, record
`verification/results/ao-stepe-install.json`). User report: the cutout shimmer fix works on
stations but distant asteroids still lose triangles that reappear; the docking port still
darkens, and so does a ship; the bolts look right at gain 1 (no gain-2 run was made); ambient
occlusion makes no visible difference on or off. Per-question analysis is in the owning ledgers —
[motion-output](../verification/motion-output.md) (`reason=3` 0.01 %, no `cutout_missed`),
[screen-emission](../verification/screen-emission.md) (firing frames 7877/9326),
[ambient-occlusion](../verification/ambient-occlusion.md) (12,163 attached / 12,036 disabled frames)
and [station-material-distance](../reverse-engineering/station-material-distance.md) (the same-port
pair `22cba120`). The instructions as issued follow.

One run covers four questions on the installed candidate `ab6e17ba…` (`5d06316`, record `verification/results/ao-stepe-install.json`) (AO step 2, screen emission step E, the
cutout-miss exemption, the w-scaled pad; fade route default-on; plugin v4 for voice):

1. **Distant shimmer**: fly the run-11 asteroid path in normal view; say whether distant asteroids
   and stations still shimmer or vanish in parts. Analysis: `camera_state reason=3` rate (was
   11–15 %; expected ≈0 outside real cuts), `taa_invalidate site=` lines.
2. **Docking port**: approach an Argon station docking port and press F8 twice on the *same* port,
   once far (the port small on screen) and once near (four times closer or more). Say whether it
   still darkens/brightens. Analysis: per-draw inputs, `packed`/fade rects and the HDR captures
   of the two frames (the first same-node distance pair).
3. **Bullets**: fire at a target for a few seconds, F8 once while firing. Say whether the bolts
   look like run 14 (they should: gain 1 is native parity) and nothing else changed.
   Analysis: `packed_sample` `changed_px`/`max_post_y` vs `max_pre_y` on bolt pixels,
   `screen_emission_frame` firing vs not.
4. **Ambient occlusion**: near a station and near an asteroid, press Ctrl+Shift+F11 a few times to
   toggle AO off/on, F8 once with AO on and once off in the same spot. Say whether the effect is
   visible, where it looks right or wrong (dark halos, crawling, HUD affected), and the frame
   rate on vs off. Analysis: `ambient_occlusion_frame cpu_us` on vs off, `ambient_occlusion_toggle`.

```sh
./x3run --direct --camera chase --ownership --object-trace --object-lifetime \
  --motion-output --taa --telemetry --camera-log 1 \
  --hdr --hdr-tonemap --hdr-exposure fixed --hdr-bloom --linear-materials \
  --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast \
  --fade-witness --screen-emission --screen-emission-timing \
  --ambient-occlusion --ao-timing \
  --voice-decoder /tmp/x3-wma-plugin-v4 \
  --capture-start 999999 --capture-frames 1
```

Optional second short run for the bolt HDR look: the same command plus `--screen-emission-gain 2`,
fire a few seconds, F8 once, and say whether the brighter bolts with bloom look right.

Moved from the open queue on 2026-09-14 (run 10 superseded by the DMO fallback hook, run 12 completed as snapshot run37, run 13 attempted as run 38 and completed as run 16).

## 10. Target-name speech with the opt-in WMA decoder — On hold (load hang, root cause open)

The process-local decoder plugin is delivered by environment only, to this one
game process; nothing is written into the game, the bottle or any global
configuration. The plugin lives in `/tmp/x3-wma-plugin`; if `/tmp` was cleared,
copy the backup back first:

```sh
cp -R /Users/asvetl/x3-mod-resume-2026-09-14/artifacts/wma-plugin /tmp/x3-wma-plugin
```

```sh
./x3run --direct --camera chase --ownership --object-trace --object-lifetime \
  --motion-output --taa --telemetry --camera-log 1 \
  --hdr --hdr-tonemap --hdr-exposure fixed --hdr-bloom --linear-materials \
  --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast \
  --voice-decoder /tmp/x3-wma-plugin \
  --capture-start 999999 --capture-frames 1
```

Load the usual save and select several different targets (ships and stations,
including repeats of the same name). Report:

1. whether target-name speech is heard at all, and for which selections;
2. whether the pause on selection changed compared with run 28 (longer, shorter
   or the same);
3. whether spoken lines start clipped (first syllable missing) or run into
   the start of the following line; the [cue-timing note](../reverse-engineering/voice-cue-timing.md)
   predicts an onset error of up to about 0.7 s from the decoder's sample
   timestamps, so this run tests restored speech, not cue timing.

No F8 capture is needed. If the game fails to start, rerun the same command
without `--voice-decoder` and report which of the two failed.

The distance-fade capture that was noted here is completed run 11; its command and
instructions are in [the completed-run archive](../archive/user-runs-completed.md).

## 12. Voice load-hang Wine trace witness — Completed

Purpose: capture CrossOver's own quartz/amstream trace of the loading-screen
hang with the decoder plugin, to see which filter fails `Pause`/`Run` inside
`SetState(RUN)` (`docs/reverse-engineering/voice-startup-sequence.md` §11). The
installed build already contains the default-off witness sites; no new DLL.
The game is expected to hang on the loading screen: wait about 30 s after the
loading screen stops progressing, then force-quit X3 (Cmd-Option-Esc). Do not
load a save if the main menu does appear; just quit and report.

```sh
CX_LOG=/tmp/x3-witness-quartz.log.z \
CX_DEBUGMSG='-all,trace+quartz,trace+amstream,warn+winegstreamer,+timestamp,+loaddll' \
./x3run --direct --telemetry --game-phases --audio-sites \
  --voice-decoder /tmp/x3-wma-plugin-v3
```

Report: whether it hung or reached the menu, the session path the launcher
prints, and the size of `/tmp/x3-witness-quartz.log.z`. Analysis greps that
trace for the failing stream's graph composition and the filter that returned
`80004005`; the log is never read whole.

## 13. Target-name speech with the decoder plugin and the DMO fallback hook — Ready

The candidate `76d7750` (DLL `608b35d8…`, record
`verification/results/screen-emission-install.json`) is installed; run 13 first, then 14, then 15. The hook acts only when the game's speech-decoder `Init`
fails with class-not-registered, so the load hang of runs 29–36 should be gone
(`docs/architecture/voice-decoder-adapter.md`, "DMO fallback hook"). Keep the
run short: load the usual save, select five or six different targets (ships and
stations), listen for the target-name speech, note whether selection still
pauses, open one NPC comm dialogue, then quit. If the loading screen hangs again
for more than 30 s, force-quit and report; do not retry.

```sh
./x3run --direct --camera chase --ownership --object-trace --object-lifetime \
  --motion-output --taa --telemetry --camera-log 1 \
  --hdr --hdr-tonemap --hdr-exposure fixed --hdr-bloom --linear-materials \
  --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast \
  --game-phases --audio-sites --voice-decoder /tmp/x3-wma-plugin-v3 \
  --capture-start 999999 --capture-frames 1
```

Report: hang or not, speech heard or not (and whether it starts at the right
word), selection pauses, comm video/audio, and the session path. Analysis reads
the `voice_dmo_fallback` activation lines, the `game_phase_audio` counters and
the selection timing.

Completed run commands and instructions are preserved in
[the completed-run archive](../archive/user-runs-completed.md); they are provenance,
not rerun requests.

Moved from the open queue on 2026-09-14. Outcome: the user ran vanilla (`./x3run --direct --vanilla`) and the double cursor reproduces there too, with the recipe "alt-tab out, move the desktop cursor outside the game window position, alt-tab back". So it is vanilla CrossOver/game behaviour, not a proxy regression; owner note `docs/architecture/window-and-cursor.md`.

## 4. Vanilla window/cursor comparison — Completed

```sh
./x3run --direct --vanilla
```

Alt-tab out and back once. Report whether both the macOS arrow and game cursor
appear, whether their positions differ, and whether the macOS menu bar overlaps
the game. Compare the same screen as the enhanced run; load the save if the
problem only appears during gameplay. No F8 capture is needed.
