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

## 22. LOD scale 2×, fade-band trembling fix, docking-port screenshot pair — Completed

Completed as user run 22, snapshot `/tmp/x3-bottleX3-run51/`, log
`session-20260915-030036-468.log` (large; queried, never read whole), installed DLL
`53a0d8a7…` (`509a273`, record `verification/results/run22-candidate-install.json`). User
report: no trembling anywhere, station or asteroids, near or far — the run-21 hangar/dock
symptom is gone and accepted; the LOD detail is "slightly better" at 2× and the user asks for
3×; the docking module still darkens at range. Per-question analysis is in the owning ledgers —
[motion-output](../verification/motion-output.md) ("User run 22": 507 `linear_material_frame`
lines, `fade_routed` 860, `fade_refused` 7, `fade_held` 0, `unjittered_depth_writers=0`
throughout; the frame-time difference against run 49 is confounded by a different route),
[lod-scale](../architecture/lod-scale.md) ("In game (run 22)": `applied=2` from one
`lod_scale_value` line, the LOD-share table) and
[station-material-distance](../reverse-engineering/station-material-distance.md) ("Run 22":
the module darkening is owned by the per-node point-light range cull, witnessed). Run 23
repeats the run-49 route at 3× for a controlled before/after. The instructions as issued follow.

One run on the installed candidate `53a0d8a7…` (`509a273`, record `verification/results/run22-candidate-install.json`): `--lod-scale 2` (the
engine's LOD switch distances doubled by the byte-verified patch,
[lod-scale.md](../architecture/lod-scale.md)) and the fade-band trembling fix
([asteroid-fog-temporal.md](../reverse-engineering/asteroid-fog-temporal.md), "Run 49"). Load the
usual save.

1. **Docking port, what you see**: pick one Argon station port. When it looks fine near, take a
   normal screenshot (save it as `screenshots/port-near.png`) and press F8. Fly away until it has
   gone black, take a screenshot (`screenshots/port-far.png`) and press F8 again. Say the target
   distance readout at each. Analysis: the two capture groups against the screenshots, the draws
   under the screenshot region, and the LOD of the node at each.
2. **LOD scale**: say whether stations and ships keep their detail noticeably farther than before
   (docking bays, greebles) and whether the frame rate suffers in a busy sector. Analysis:
   `lod_scale requested=2 patched=1 write=plain`, then `lod_scale_value … applied=2` on the frame
   lines; `object_context lod=` distribution vs run 21; `frame_end dt_ms` windows vs run 21.
3. **Trembling**: view a station at 3–5 km in chase view with asteroids or other fading objects
   around; say whether any part still trembles. Analysis: fade regions of reviewed pairs now
   `routed=1` with the mask excluded above the threshold; `unjittered_depth_writers=0`.
4. Anything else, including loading time by feel.

```sh
./x3run --direct --camera chase --ownership --object-trace --object-lifetime \
  --motion-output --taa --telemetry --camera-log 1 \
  --hdr --hdr-tonemap --hdr-exposure fixed --hdr-bloom --linear-materials \
  --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast \
  --fade-witness 1 --screen-emission \
  --lod-scale 2 \
  --voice-decoder /tmp/x3-wma-plugin-v4 \
  --capture-start 999999 --capture-frames 8
```

Report: the session path, the two screenshots with their distances, the LOD and trembling
observations.

## 21. AO appearance at radius 100, bullet witness on firing frames, vanilla port approach — Completed (session A)

Completed as user run 21 session A, snapshot `/tmp/x3-bottleX3-run49/`, log
`session-20260915-010311-212.log` (495 MB), installed DLL `39b090d0…` (`77a649b`, no new build).
**Session B (the vanilla port-approach comparison) has not been run or reported by the user**;
the port-darkening question therefore stays open where it is tracked
([station-material-distance](../reverse-engineering/station-material-distance.md)). User report:
ambient occlusion makes no visible difference on or off at radius 100, near a station or in an
asteroid field; the bolts are unchanged; a station section (hangar/dock bay, `screenshots/jitter1.png`)
trembles up and down at about 4.7 km in chase view and stops closer. Per-question analysis is in the
owning ledgers — [ambient-occlusion](../verification/ambient-occlusion.md) (8,866 attached frames,
`cpu_us` mean 177.3 µs, the default-off decision), [screen-emission](../verification/screen-emission.md)
(13,385 witness lines, 0 outside; firing frames 9889–9896) and
[motion-output](../verification/motion-output.md) (the new station jitter, diagnosis in progress).
The instructions as issued follow.

No new build; the installed `39b090d0…` (`77a649b`) is used. Two short sessions.

**Session A (enhanced).** Load the usual save.

1. **Ambient occlusion**: near a station (within ~1 km) and in an asteroid field, press
   Ctrl+Shift+F11 a few times. This time the scene is shaded (no gray view), and the radius is
   100 m (the launcher and DLL cap), the readable-footprint proxy from
   [ambient-occlusion-scale.md](../architecture/ambient-occlusion-scale.md). Say whether creases,
   docking bays, hull plating and asteroid contact areas darken visibly, whether it looks wrong
   anywhere (dark halos around objects against the nebula, crawling, HUD), and whether you would
   keep it on. Press F8 once with AO on and once off at the same spot near the station, without
   moving. Analysis: `ambient_occlusion_frame reason=ok` on the on-capture, the on/off HDR pair,
   `cpu_us`.
2. **Bullets**: fire at a target for a few seconds, F8 once while firing. The witness now samples
   every frame. Analysis: `fade_witness outside` on the firing frames, `packed_sample` peaks.
3. Anything else you notice, including the loading time by feel.

```sh
./x3run --direct --camera chase --ownership --object-trace --object-lifetime \
  --motion-output --taa --telemetry --camera-log 1 \
  --hdr --hdr-tonemap --hdr-exposure fixed --hdr-bloom --linear-materials \
  --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast \
  --fade-witness 1 --screen-emission --screen-emission-timing \
  --ambient-occlusion --ao-timing --ao-radius 100 \
  --voice-decoder /tmp/x3-wma-plugin-v4 \
  --capture-start 999999 --capture-frames 8
```

**Session B (vanilla, eyes only).** Same save, same Argon station: fly out until the docking port
is thumbnail-sized, then back until it fills a third of the screen, and do the same on one ship.
Say whether the port and the ship darken when near in vanilla too. No capture, no snapshot.

```sh
./x3run --direct --vanilla
```

Report: session A path, the AO verdict (keep / adjust / drop) with where it looked right or
wrong, the bullets, and the vanilla darkening answer.

## 20. Step D bullets, asteroid prepass jitter, loading markers, AO radius 20, port and ship far/near — Completed

Completed as user run 20, snapshot `/tmp/x3-bottleX3-run48/`, log
`session-20260915-002408-212.log` (335 MB), installed DLL `39b090d0…` (`77a649b`, record
`verification/results/run20-candidate-install.json`). User report: no asteroid shimmer at all;
the bolts look the same, possibly very slightly dimmer, unsure; the AO view was "mostly white,
twin gray lines when close" (the run carried `--ao-debug`, so the whole session showed the gray
occlusion factor, not AO applied to the image); the docking port still darkens on approach.
Per-question analysis is in the owning ledgers —
[motion-output](../verification/motion-output.md) (prepass `gate=3 jittered=1`, `reason=3` 0.008 %),
[screen-emission](../verification/screen-emission.md) (firing frames 23902–23909, hull/aabb 0.32–0.35),
[ambient-occlusion](../verification/ambient-occlusion.md) (4,464 attached frames, the radius law)
and [station-material-distance](../reverse-engineering/station-material-distance.md) (the far/near
port pair `0f768ad8`); the `loading_phase` markers are in
[loading-observations](../reverse-engineering/loading-observations.md) (menu 12.9 s, save load 21.7 s).
The instructions as issued follow.

One run on the installed candidate `39b090d0…` (`77a649b`, record
`verification/results/run20-candidate-install.json`: z_only prepass jitter, step D, `loading_phase` markers). Load the usual save.

1. **Asteroids**: zoom on a distant asteroid field as in run 19 and say whether triangles still
   vanish and reappear. While zoomed and at rest on a far asteroid, press F8 once (this launch
   captures 8 consecutive frames). Analysis: `unjittered_depth_writers=0` on every
   `motion_output_frame` line, no jitter-side holes in the 8 pre-resolve frames.
2. **Docking port and ship**: pick one Argon docking port; fly out until the port is clearly small
   (about a thumbnail, under ~30 px), F8; fly back until it fills about a third of the screen, F8.
   Do the same far/near F8 pair on one ship. Say whether each darkens. Analysis: the per-draw path
   and the HDR captures of each pair (`docs/reverse-engineering/station-material-distance.md`).
3. **Bullets**: fire at a target for a few seconds, F8 once while firing. Say whether the bolts
   look like run 19. Analysis: `hull_px`/`aabb_px`, `window_end_scans`/`scans`, `sentinel_us`,
   brackets no longer near-fullscreen (`docs/verification/screen-emission.md`).
4. **Ambient occlusion at radius 20 m**: near a station and near an asteroid press Ctrl+Shift+F11 a
   few times; say whether the darkening in creases and contact areas is visible now, and whether
   it looks wrong anywhere (halos, crawling, HUD), and the frame rate on versus off if you
   notice it. Analysis: `ambient_occlusion_frame cpu_us`, `radius_px`, the on/off captures.
   Optional second short session (B, below): the same launch with `--ao-debug` replaces the
   image by the gray occlusion factor whenever AO is on; load the save, look at a station and
   an asteroid, quit, and say whether the gray view shows creases and contact darkening.
5. **Loading**: nothing to do; the log now carries `loading_phase` markers for menu-shown and
   save-load; report roughly how long the menu and the save load took by feel.

```sh
./x3run --direct --camera chase --ownership --object-trace --object-lifetime \
  --motion-output --taa --telemetry --camera-log 1 \
  --hdr --hdr-tonemap --hdr-exposure fixed --hdr-bloom --linear-materials \
  --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast \
  --fade-witness --screen-emission --screen-emission-timing \
  --ambient-occlusion --ao-timing --ao-radius 20 --ao-debug \
  --voice-decoder /tmp/x3-wma-plugin-v4 \
  --capture-start 999999 --capture-frames 8
```

Report: the session path, and the five observations above. Frame rate on versus off for AO is
still useful if you notice it.

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

## 23. Material fill at the run-51 station — completed

Completed as `/tmp/x3-bottleX3-run54`. The user reports brighter hulls; fixed-EV0
threshold evidence supported 0.06 provisionally. The user subsequently chose fill
**0.03** and Auto EV ceiling **+1.0** as defaults; the defaults are now applied.
No further preference bracket is requested.

Original active instructions follow verbatim.

This reuses the withdrawn LOD comparison number for the requested fill run.
The candidate is installed; the command below is ready.
Fill stays default-off; this run selects 0.06 explicitly. Keep AO off and use the
same save, station, approach and sun direction as snapshot run51.

```sh
./x3run --direct --camera chase --ownership --object-trace --object-lifetime \
  --motion-output --taa --telemetry --camera-log 1 \
  --hdr --hdr-tonemap --hdr-bloom --linear-materials --material-fill 0.06 \
  --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast \
  --fade-witness 1 --screen-emission \
  --voice-decoder /tmp/x3-wma-plugin-v4 \
  --capture-start 999999 --capture-frames 8
```

1. At the run-51 docking ring, take a screenshot and press F8 at about **350 m**
   (far, sun-averted clamps), then again at about **210 m**. Report both distances.
2. Say whether the far arm tips now read as surfaces, the cylinder's night side
   stays dark, and any adjacent material looks conspicuously different.
3. Auto exposure keeps its +1.5 EV ceiling. Run51 actually used **fixed EV 0**;
   for an appearance comparison at the same spot, Ctrl+Shift+F9 selects fixed
   EV 0, then capture once more. Tell us which screenshot uses fixed exposure.
4. If 0.06 is clearly too weak or too strong, close X3 and repeat the same
   far/near pair with only `--material-fill` changed to **0.04** or **0.10**.
   Report the snapshot path and the value for each session.

Analysis follows [fill acceptance](../architecture/fill-light.md#5-acceptance-run):
startup fill and routed-pair evidence; far-module dark fraction ≤0.10 and p10
≥0.045; cylinder mean ≤0.165; chroma difference ≤0.03; matched far-dark points'
near/far gain ≤2.0. Scene-linear readbacks precede exposure. Auto exposure must
be evaluated separately from the fixed-EV run51 baseline; no auto-EV difference
against that baseline is an adaptation measurement. The result informs the fill
default decision; it does not change the default automatically.


## 24. Chase reset-writer telemetry — gate completed as run56

Gate portion completed as `/tmp/x3-bottleX3-run56`; the user confirms a camera
reset. Jumpdrive was not run because no suitable save is available. The trace
recreates the cockpit, so the proposed same-lifetime restore would cancel.
Analysis of a safe cross-recreation identity is in progress. The shared static
warp path is documented; another jumpdrive attempt is not a prerequisite for
run25. Instructions below are retained as provenance; no repeat is currently
requested.

The recorded diagnostic command is:

```sh
./x3run --direct --camera chase --ownership --object-trace --object-lifetime \
  --motion-output --taa --telemetry --camera-log 1 \
  --hdr --hdr-tonemap --hdr-bloom --linear-materials --material-fill 0.06 \
  --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast \
  --fade-witness 1 --screen-emission \
  --voice-decoder /tmp/x3-wma-plugin-v4 \
  --capture-start 999999 --capture-frames 8
```

The user may append `--hdr-ev-max 1.0` voluntarily; the default exposure limit
and material-fill default remain unchanged. Start in rear chase view, make one
gate jump, reselect rear chase if it resets, then make one jumpdrive jump. Do
not press a view key during either transition. After each arrival wait a few
seconds, report whether it reset, then verify the normal view keys still work.
Report the snapshot path and the order of the two jumps. No view-restoration
option is enabled.

Analysis: identify the mode-1 writer's `next_pc`, the update ordering against
cockpit `+0x1fc`, lifetime changes, and deltas in `+0x130`, `+0x160`, `+0xa8`
and `+0x1c0`. [The ratified restore policy](../architecture/chase-view-restore-and-hud-anchor.md)
is implemented only after these observations settle its prerequisite.


## 25. Consolidated diagnostic — completed as run60

Received as `/tmp/x3-bottleX3-run60` (60 referenced files). The user reports that
the forward crosshair/distance group is aligned, but prefers the screen centre
default and may revisit forward anchoring after future camera tuning. Centre
remains the default; forward remains opt-in. The gate transition still reset the
view; this build records diagnostics and contains no restoration. Gate identity,
loading intervals and sun-lane evidence are under analysis. No repeat is
requested at this checkpoint.

One session collects the additional evidence needed for chase restoration and
loading attribution, while checking forward HUD placement and sun-share
coverage. Auto ceiling is +1.0 EV and fill is 0.03. The sun-share option is
diagnostic only; it applies no shadows. Selective exposure and automatic chase
restoration are not enabled.

```sh
./x3run --direct --camera chase --chase-hud-anchor forward \
  --ownership --object-trace --object-lifetime \
  --motion-output --taa --telemetry --camera-log 1 \
  --loading-intervals --sun-shadow-lane \
  --hdr --hdr-tonemap --hdr-bloom --linear-materials \
  --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast \
  --fade-witness 1 --screen-emission \
  --voice-decoder /tmp/x3-wma-plugin-v4 \
  --capture-start 999999 --capture-frames 8
```

1. Load the save normally and wait several seconds after the scene appears.
2. In rear chase view, briefly check whether the crosshair/distance group is
   sensibly aligned with the ship’s forward firing direction. A screenshot is
   useful if the placement looks wrong.
3. Use one jump gate. Do not press view keys during the transition. After
   arrival, wait several seconds, report whether the camera reset, then check
   that the normal view keys still work. No jumpdrive is needed.
4. Exit normally and report the printed snapshot path, HUD observation and
   any new rendering or loading problem.

Analysis: compare script IDs/native lifetimes, destructor ancestry, persistent
mode and geometry across the gate; validate loading interval completeness and
report per-thread/combined occupancy and uncovered intervals without assigning
a causal residual; check sun-lane admitted coverage/refusals and TAA continuity.

## 26. Cheap HDR emitters, chase view restore and replay candidates — completed (run65/run66)

Installed: DLL `5726a37b…` from `f2b7406` (see [status](../status.md)). No linear
materials: this is the user's preferred original hull look. One session A covers
everything except the sun-lane buckets; session B is optional.

**Session A** (from the repository root):

```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --emission-source-gain 2 --shadow-replay-candidates --loading-intervals --capture-start 999999 --capture-frames 8
```

Please do, in this order, and report what you saw:

1. Load the usual save (the one from run 20/48 if possible, for the loading
   stall attribution). Note whether loading felt like the 21.7 s stall.
2. Fire at something and look at engines and bolts: are they clearly brighter
   and bloomed than before, and does anything look wrong over a bright
   background (sun, nebula)? Say whether gain 2 is too much, right, or too little.
3. In rear chase view, fly through a gate. Does the view stay in rear chase on
   the other side (no reset to the default view)? If it resets, say so; the log
   tells us why.
4. Optional second short session with a raised camera instead of a tilted one:
   add `--chase-pitch-down-deg 0.5 --chase-offset-y 0.50`. Does the centre
   reticle now sit on where the bolts go, and is the top view still acceptable?

**Session B (optional, linear materials only for the diagnostic):** the sun-lane
refusal buckets need converted materials; if you have time, run the same
command plus `--linear-materials --linear-distance-fade --sun-shadow-lane` for
a minute of ordinary flight near a station and quit. Nothing to look at.

## 27. Corrected restore, engine gain, point-light admission, depth replay — session A completed (run68)

Installed: DLL `215d8fbe…` from `46dc822` (see [status](../status.md)). Original hulls, new defaults (camera 0.5°/0.50, EV ceiling 1.3).

**Session A** (from the repository root):

```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --taa-mip-bias -0.5 --taa-sharpen 0.75 --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --emission-source-gain 2 --point-light-root-admission --shadow-replay-candidates --shadow-replay-depth --loading-intervals --capture-start 999999 --capture-frames 8
```

1. Load the usual save. Engines: are they now visibly brighter and bloomed
   (the gain admitted no draws in run 26; that is fixed)? Bolts as before?
2. Mip bias -0.5 and sharpen 0.75 are on for the first time on this build:
   sharper textures at distance, any shimmer or over-sharpening? Say keep/drop.
3. Rear chase view, fly through a gate: does the view stay in rear chase?
4. Fly to the station of run 22/51 (docking modules that went black at range)
   and press F8 once at about 1.2 km facing the modules, once close. With the
   point-light option on the modules should stay lit at range.
5. Optional short session A2: same command without `--point-light-root-admission`,
   same station spot, one F8 at the same distance, for the frame-time and
   appearance comparison.

**Session B (optional, linear materials, diagnostics only):** the same command
plus `--linear-materials --linear-distance-fade --sun-shadow-lane`, a minute of
flight near a station. The sun-lane frames should now report `available=1`.

## 29. Emitter hotkeys, bolt alpha, chase pose, frame timing, sun lane — completed as run83–run85

Received as `/tmp/x3-bottleX3-run83` (session A), `run84` (A2 with the profiler), `run85` (session B). Outcomes are in [status](../status.md) (session 2026-09-16/17) and the owning ledgers. Original instructions follow.

Installed: DLL `7f296d53…` from `dd29770` (see [status](../status.md)). Defaults unchanged.
New in this build: Ctrl+Shift+F5 toggles the additive bolts, Ctrl+Shift+F6 the
engine source gain, Ctrl+Shift+F4 the effect source gain (each between its
configured gain and native, with an on-screen notice; the next candidate
merges F6/F4 into one F6 over all twenty pairs and drops
`--effect-source-gain`, `linear-emission-cost.md` "Screen substitution"); `--screen-emission-additive-alpha K`
keeps the bolts out of the authored-glow bloom term; the chase pose now lands on
the first frame after a transit; `--frame-timing` logs frame-time windows.

**Session A, emitter attribution and halo** (gains at 5 so each family is unmistakable):

```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --frame-timing --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 5 --screen-emission-additive-alpha 0 --emission-source-gain 5 --effect-source-gain 5 --shadow-replay-candidates --shadow-replay-depth --loading-intervals --capture-start 999999 --capture-frames 8
```

1. With engines burning and while firing, press each key in turn and say what
   changes: F5 (bolts), F6 (engines), F4 (effect sprites). If F5 also dims the
   engine glow, say so; the log's `screen_emission_additive_frame` line names
   the admitted pairs per frame either way.
2. Bolt halo over black space, bloom on: with alpha 0 the bolts should keep
   their brightness but bloom much less. Toggle bloom (Ctrl+Shift+F10) once to
   compare. Say whether the halo is now acceptable or still too strong.
3. Rear chase through a gate and back, twice: the ship should sit at the
   configured position on the first frame after each transit, no centre-then-jump.
4. Low FPS: find a view with many objects (a busy station or fleet) and hold it
   for 30 s, then look away to empty space for 30 s. Note the rough FPS both
   ways; the `frame_timing` windows tie the slow frames to draw counts.

**Session A2, profiler in the busy scene** (short): the same command plus
`--profile`, load, fly to the same busy view, hold 30 s, quit. Nothing to look
at; the sampler attributes the slow frames to game, Wine, GPU wait or proxy.

**Session B (linear materials, diagnostics only):** the session A command with
gains back at 2 plus `--linear-materials --linear-distance-fade --sun-shadow-lane`,
a minute near a station: sun-lane frames should now report `available=1`.

After the tests, play with gains at 2 (`--screen-emission-additive 2
--screen-emission-additive-alpha 0 --emission-source-gain 2 --effect-source-gain 1`;
the next candidate takes no `--effect-source-gain`) and say whether alpha 0
should stay.

## 28. Restore re-arm, emitter split and original fill — completed as run74–run81

Installed: DLL `2b0969e5…` from `a26eb9b`. Mip bias -0.5 and sharpen 0.75 are now defaults, as
are the raised camera and the 1.3 EV ceiling; the point-light option stays out
unless you want it compared again.

**Session A** (from the repository root):

```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --emission-source-gain 2 --shadow-replay-candidates --shadow-replay-depth --loading-intervals --capture-start 999999 --capture-frames 8
```

1. Halo isolation, two short relaunches firing in empty space: (a) the command
   plus `--hdr-ev-max 1.0`; (b) the command plus `--taa-sharpen 0
   --taa-mip-bias 0`. Report which one removes the halo (run65 had EV 1.0, no
   sharpen, no mip bias; run68 had all three). If neither does, note the window
   resolution of both sessions: run68's bloom working set was 21 MB against
   run65's 13 MB and nothing logged explains it. Engines stay under
   `--emission-source-gain 2`; effect sprites are at gain 1 in this build.
2. Rear chase, through a gate and back through the same gate, twice: the view
   should stay in rear chase on every transit now.
3. Original fill A/B at the run-22 station: one F8 at about 1.2 km facing the
   docking modules in this session, then quit and run the same command plus
   `--original-fill 0.05`, same spot, one F8. Say whether the unlit sides read
   better and whether anything else looks lifted. The frame-time delta comes
   from the logs.

**Session B (optional, linear materials, diagnostics only):** the same command
plus `--linear-materials --linear-distance-fade --sun-shadow-lane`, a minute
near a station: sun-lane frames should now report `available=1`.

## 31. Frame split, engine phases, lighter proxy — completed as run89–run90

Installed: DLL `a9ebfa3b…` from `4adf3dd` (see [status](../status.md)). This
build removes most of the proxy's own per-call cost (light CPU envelope on the
draw and binding hooks, trimmed setter dispatch, count-only frame timing), adds
`--frame-phases` (ten stamps in the engine's frame routine: input, script,
scene update, views, overlays, text, scene end, present) and reports the
unknown-program census (`shader_unknown`). Appearance is unchanged from run 30.

**Session A** (the busy sector, frame split):

```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --frame-timing --frame-phases --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --shadow-replay-candidates --shadow-replay-depth --loading-intervals --capture-start 999999 --capture-frames 8
```

1. Fly to the busy view of run 30 (the one that felt slow), hold it 30 s, then
   face empty space 30 s, then quit. Say roughly how the FPS compares with
   run 30 in the same place; the logs give the exact split: proxy versus game,
   and inside the game which phase grows with the object count.
2. Nothing to look at otherwise; engines, bolts and halo should look exactly
   as in run 30. Say if anything changed.

**Session B (linear materials, cutout admission):** the session A command plus
`--linear-materials --linear-distance-fade --sun-shadow-lane`, one minute close
to an **Argon** factory, farm, solar plant or trading station (they carry the
lattice and crop cutouts): the log should show `cutout_routed` above 0 and
`sun_shadow_lane_writer` lines with `arm=`.

## 30. Single emission gain, bloom source clamp, frame-time split — completed as run87–run88

Installed: DLL `bbadc568…` from `f94290c` (see [status](../status.md)). New in this
build: one `--emission-source-gain G` for every emitter drawn by the effects
program (engines, gate, beams, flares, shield hits; screen-blended engine
materials are drawn additively under the gain), Ctrl+Shift+F6 toggles it,
Ctrl+Shift+F4 and `--effect-source-gain` are gone; `--bloom-source-clamp C`
bounds what any pixel feeds the bloom (1.0 = a native bright pixel's halo);
`--frame-timing` now splits each frame into proxy draw/scene/state time versus
game time and names the slowest hooked call.

**Session A** (appearance and frame split):

```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --frame-timing --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --shadow-replay-candidates --shadow-replay-depth --loading-intervals --capture-start 999999 --capture-frames 8
```

1. Engines: are they now visibly brighter and bloomed? Press Ctrl+Shift+F6 to
   compare with native. Look at an engine glow over a nebula or the sun and
   say whether the additive look is acceptable there (it is brighter than
   native over bright backgrounds).
2. Bolt halo over black with the clamp at 1.0: acceptable, too faint, or still
   too strong? Then quit and relaunch with `--bloom-source-clamp 2.0` and say
   which you prefer; if both look wrong, once more without the option.
3. Fire at something until it explodes, and fly past a station with lattice
   or grating trim (the run-22 station): the log then shows which program draws
   explosions and exercises the cutout admission.
4. Hold a busy view for 30 s, then empty space for 30 s: the frame-time split
   says whether the slow frames are proxy or game time.
5. One gate transit in rear chase: no centre-then-jump expected.

**Session B (linear materials, diagnostics only):** the session A command plus
`--linear-materials --linear-distance-fade --sun-shadow-lane`, a minute at the
same lattice station: sun-lane frames should report `available=1` and the
`sun_shadow_lane_writer` lines should name any cutout draw with `arm=`.

After the tests, say the gain and clamp you want to play with.

## 33. Pass phases, loop-region split, cutout lane telemetry — completed as run95–run97

Installed: DLL `03c0c9f4…` from `a3cafd5` (see [status](../status.md)). This
build adds `--pass-phases` (four accumulate-only stamps in the effect pass
loop: pass apply, draw, pass end, per draw, about 0.36 ms per busy frame),
the `cutout_opaque_*` lane counters on `linear_material_frame`, and
`--loop-phases` (six stamps in the per-sector update driver `0x0043a360`:
collision, simulation, global object pass, economy/attach pass, per sector and
per frame, with a slow-frame line naming the worst interval). Appearance unchanged from run 32.

**Session A** (busy view, production hooks off, pass split):

```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --frame-phases --pass-phases --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --shadow-replay-candidates --shadow-replay-depth --loading-intervals --capture-start 999999 --capture-frames 8
```

1. The busy view of runs 31/32, hold 30 s, then empty space 30 s, quit. No
   `--frame-timing`, so the proxy runs unhooked as you play; the pass stamps
   split the engine's per-draw time into pass apply, draw and pass end.

**Session B** (the slow sector): session A's command plus `--game-phases
--loop-phases`. Fly to the sector that stalled in runs 93/94, stay 30 s while
it is slow, quit. The `loop_phases_slow` lines name which of the five
per-sector routines owns each slow frame and how many sectors were walked.

**Session C** (cutout lane): session A's command plus
`--linear-materials --linear-distance-fade --sun-shadow-lane`, the same busy
view 30 s, quit; the log now reports `cutout_opaque_routed/lane/refused`.

## 32. Hybrid unhook, draw and state counters — open

Installed: DLL `11c1f119…` from `baee232` (see [status](../status.md)). This
build stops hooking SetRenderState and SetSamplerState in production (the
proxy reads what it needs at draw time) and adds three count-only diagnostics
to `--frame-timing`: draws per program pair with the two cutout pairs called
out, redundant state sets against the shadow, and draw batchability (same
mesh, same material). `--frame-timing` keeps the two hooks installed so it can
count; the felt FPS comes from session A2 without it. Appearance unchanged.

**Session A1** (busy view, counters):

```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --frame-timing --frame-phases --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --shadow-replay-candidates --shadow-replay-depth --loading-intervals --capture-start 999999 --capture-frames 8
```

1. The busy view of run 31 (run89), hold 30 s, then empty space 30 s, quit.

**Session A2** (same place, felt FPS): the same command without
`--frame-timing`. Hold the same busy view 30 s and say how the FPS compares
with run 31 there; the phase stamps give the frame time.

**Session B** (cutout draws under the lane): session A1's command plus
`--linear-materials --linear-distance-fade --sun-shadow-lane`; hold the same
busy view as A1/A2 for 30 s, then quit. Run91 (A1) counted about 108 cutout
draws per frame in that view (`cutout_pairs=9073,23509` per 300 frames), so
the cutout admission is exercised there; no station hunt needed.

**Session C** (the slow sector, installed build, no new candidate): session
A1's command plus `--game-phases`. Fly to the sector where run93 slowed down
(the third sector of that route, not visually busy), stay there 30 s while it
is slow, then quit. `--game-phases` records every frame over 50 ms with the
main loop's sub-phases (input, script VM, deferred callbacks, simulation/AI,
cockpit), which is what run93's `pre_render` at 98 % of a 420 ms frame could
not split.

## 34. Stall evidence: stderr capture, module identity, media-cue trace and cache — completed as run98–run102

Installed: DLL `7102a2f1…` from `ee5a406` (see [status](../status.md)). This
build tees Wine's stderr into the session directory (`launcher-stderr.log`,
UTC-prefixed) with a `clock_anchor` and `qpc=` on every window line so a
GStreamer burst maps to a frame, logs `loaded_module` for the D3D9 backend and
`d3dx9_37.dll`, `--media-cue-trace` (one line per media-cue graph build attempt at
`0x00498140`: cue id, file, result, attempts per frame) and the media-cue
negative cache (a cue whose graph build failed is not retried every frame;
retried on sector change and after a fixed interval; default on after this
run, `--media-cue-cache on|off`). Appearance unchanged.

**Session A1** (the slow sector, trace only):

```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --frame-phases --pass-phases --game-phases --loop-phases --media-cue-trace --media-cue-cache off --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --shadow-replay-candidates --shadow-replay-depth --loading-intervals --capture-start 999999 --capture-frames 8
```

Fly to the stalling sector, stay 30 s while it is slow, quit. The trace names
the cue, its file and the result of every build attempt; the stall should
still be there. Wine's stderr is now in the session directory.

**Session A2** (the slow sector, cache on): the same command with
`--media-cue-cache on` (the default once this run confirms it). Same sector,
30 s, quit. The stall should be gone and the trace should show one failed
attempt per cue followed by cached refusals; say whether the sector's music
or ambient sound is missing and whether anything else changed.

**Session A3** (decode fix, only if the v5 runtime is reported built): session
A1's command with `--voice-decoder /tmp/x3-wma-plugin-v5` instead of v4 and
`--media-cue-cache off`. Same sector, 30 s, quit. If the runtime decodes the
cue, the trace shows the build succeeding, the stall is gone without the
cache, and the sector's music or ambient sound plays; say whether speech
still works.

**Session B** (DXVK experiment, no proxy change): in CrossOver, enable the
DXVK backend for the X3 bottle (bottle settings, D3D9 via DXVK), then run
run 33 session A's command in the busy view for 30 s and quit. The
`loaded_module` line says which D3D9 backend the proxy actually forwarded to,
and the `pass_phases` draw µs per pass says whether the draw call got cheaper.
Switch DXVK back off afterwards unless it wins.

## 35. D3DX builtin vs native at the busy view — completed (run103)

Installed: DLL `7102a2f1…` from `ee5a406` (see [status](../status.md)); the
bottle's graphics backend is back on its default. The launcher option
`--d3dx builtin` forces Wine's builtin `d3dx9_37` for the game child only
([design](../architecture/effect-pass-replay.md), bottle experiments); the
proxy's `loaded_module` line reports which `d3dx9_37.dll` actually loaded.

**Session A** (from the repository root):
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --frame-phases --pass-phases --d3dx builtin --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --shadow-replay-candidates --shadow-replay-depth --loading-intervals --capture-start 999999 --capture-frames 8
```
1. Load the usual save and go to the run95 busy Argon view (the station
   complex where the frame was 26.5 ms); hold it for about two minutes, then
   look away to an empty view for a minute and quit.
2. Say whether anything rendered differently or failed to render (effects,
   HUD, text) with the builtin D3DX; the game's effects compile through it.
3. Optional **A2**: the same command without `--d3dx builtin`, same spot, same
   duration, as the native control on this build (run95 is the control from the
   run33 build otherwise).

What is read: the `d3dx9_37` `loaded_module` line (builtin vs the game
directory's native file), and the `--pass-phases` per-pass BeginPass median
against run95's 6.7 µs at the same draw count. If the builtin loads and
BeginPass changes by less than 1 µs, the experiment is void and the pass-replay
prerequisites follow; if the builtin fails to load, the log says so and the
override syntax is the suspect.

## 36. D3DX on one build, first sun shadows — completed as run104–run106

Installed: DLL `51a3d764…` from `c9a8145` (see [status](../status.md)). Original hull shading
throughout; no `--linear-materials` anywhere.

**Session A1** (builtin D3DX, the run95 busy view):
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --capture-start 999999 --capture-frames 8 --frame-phases --pass-phases --d3dx builtin
```
**Session A2** (native D3DX, same spot, same duration):
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --capture-start 999999 --capture-frames 8 --frame-phases --pass-phases
```
For A1 and A2: load the usual save, hold the busy Argon station view about two
minutes, then an empty view for one minute, quit. Nothing to look at beyond
"anything rendered wrong?". The log's `loaded_module` line now says
`wine_builtin=1` when the builtin loaded, and the launcher's first stderr line
records the command.

**Session B** (first shadows):
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --capture-start 999999 --capture-frames 8 --sun-shadow-lane --shadow-replay-depth --sun-shadow-apply --shadow-replay-candidates
```
1. Fly near a station in daylight with the sun to one side. Look at the
   station's own parts shadowing each other and at your ship's hull; also at
   engine glow and bolts over a shadowed hull (a known limitation darkens
   effects composed over a shadowed receiver).
2. Press F8 twice: once at about 1 km facing a station with the sun to the
   side, once close to your own ship's shadowed side. The capture now dumps
   the sun map so the mask can be judged offline.
3. Report what you see in plain words: are there shadows at all, are they in
   the right place, edge quality, acne or striping, anything flickering, and
   whether the frame rate changed noticeably. Shadows are expected to be
   rough on this first look; the run decides bias and cascade tuning.
If the game misbehaves, the same command without `--sun-shadow-apply` keeps
the lane and replay diagnostics only.

## 37. Environment experiments, station shadows, avatar video — completed as run107–run110

Installed: DLL `61725145…` from `1f2de5d` (see [status](../status.md)). Original hull shading
throughout. Sessions A1/A2 are the busy-frame experiments at the run95/run105
view (two minutes busy, one minute empty, quit); B is the shadow look; C is a
deliberate throwaway.

**Session A1** (FEX memory-ordering relaxation; may expose races: crashes,
glitches, audio trouble are findings, not surprises):
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --capture-start 999999 --capture-frames 8 --frame-phases --pass-phases --fex-tso off
```
**Session A2** (wined3d command-stream thread off):
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --capture-start 999999 --capture-frames 8 --frame-phases --pass-phases --wined3d csmt=0x0
```
For both: say whether the game misbehaved in any way and whether it felt
faster. The log compares per-pass and frame time against run105.

**Session B** (shadows, geometry casters):
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --capture-start 999999 --capture-frames 8 --sun-shadow-lane --shadow-replay-depth --sun-shadow-apply --shadow-replay-candidates
```
Go back to the run106 spot above the station deck (tower and box near you),
but turn so the sun is off to one side (60–120° from your view direction)
rather than behind you. Station parts within about 250 m of your ship now
cast; the rest of the complex does not yet. Press F8 twice: once with the
tower's shadow expected across the deck, once close to your own hull with the
sun to the side. Report: shadows visible or not, where, edge quality, acne or
striping, flicker, frame-rate change. The lane costs are in the log.

**Session C** (avatar video through stock decoders; expect a possible freeze):
before this session the H.264/AVI transcode of `mov\00001.dat` is installed
over the original with `python3 tools/media_transcode.py install --id 1 --from
/tmp/x3-media-transcode` (the original is kept as `00001.dat.orig`; restore
with `python3 tools/media_transcode.py restore --id 1`). Then:
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --capture-start 999999 --capture-frames 8 --media-cue-trace
```
Load, open a comm dialog with any ship or station once, note whether the
avatar video plays (moving picture, black box, or freeze), quit or force-quit.
The log's `media_video_blit` lines say whether decoded frames reached
Direct3D before any freeze. Restore the original file afterwards.

## 38. Wide shadow map, own-ship baseline, residual phases — completed as run111–114

Installed: DLL `5b4be52e…` from `e575136` (see [status](../status.md)). Original hull shading throughout. The cascade
design ([note](../architecture/shadow-cascades.md)) is calibrated by A and A2.

**Session A** (one wide map: 1,500-unit half-extent, 3,000 along the sun,
4096 texels, cap 512):
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --capture-start 999999 --capture-frames 8 --sun-shadow-lane --shadow-replay-depth --sun-shadow-apply --shadow-replay-candidates --shadow-replay-extent 1500 --shadow-replay-depth-half 3000 --shadow-replay-size 4096 --shadow-replay-cap 512 --sun-shadow-bias-clamp-texels 4 --frame-phases
```
Go to the run109 spot above the station deck with the sun to one side and
look for station parts shadowing each other across the deck (tower, boxes,
antennae). Press F8 twice: one wide station view, one close to a shadow edge.
Report: station-on-station shadows yes/no, how far across the station they
reach, edge sharpness, acne or striping, light leaking at silhouettes,
flicker while flying, and the frame-rate feel. The log carries the caster
counts, the replay cost at this size and the resolved bias.

**Session A2** (the near-map baseline for the own ship, same spot, one
minute): the run37 configuration at 4096 texels:
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --capture-start 999999 --capture-frames 8 --sun-shadow-lane --shadow-replay-depth --sun-shadow-apply --shadow-replay-candidates --shadow-replay-size 4096 --frame-phases
```
One F8 close to your own hull with the sun to the side; say whether the
ship's self-shadow is sharper than in run 37 B.

**Session B** (residual attribution at the busy view, two minutes busy, one
empty, quit):
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --capture-start 999999 --capture-frames 8 --frame-phases --pass-phases --residual-phases
```
Nothing to look at; the log splits the engine's between-pass time into its
own preparation and the D3DX technique lookup per draw, and the time outside
submission into particles and the rest.

**Session C** (hull emitters bracket, any station with position lights and
signs, one minute):
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --capture-start 999999 --capture-frames 8 --hull-emitters
```
Press F6 to toggle the emitter gains (engines, effects and now hull lights
together) and say whether the station's position lights, warning and
construction signs read as lights at gain 2, and whether anything else on
the hull brightened that should not have.

## 39. Cascades, positional sun, caster retention census, hull emitters — completed as run115 (session A only; A2/B/C carried into run 40)

Installed: run39 candidate `cc966fb2…` from `7492137` (see [status](../status.md)). Original hull
shading throughout. Design: [cascade extents](../architecture/shadow-cascade-extents.md)
set R, [caster retention](../architecture/shadow-caster-retention.md) stage 1 census.
Hotkeys: **Ctrl+Shift+F12** toggles the shadows (replay + apply) at rest for the fill-cost
A/B; **Ctrl+Shift+F4** toggles the hull emitters; F6 the engine/effect gains; F8 capture.

Common prefix for every session:
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --capture-start 999999 --capture-frames 8 --frame-phases
```

**Session A** (set R, census only — retained casters are counted, not drawn; 5–8 minutes):
```sh
<prefix> --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-cascades 250,1500,7500,25000 --shadow-cascade-sizes 4096,4096,4096,2048 --shadow-sun-poll on --shadow-retention-census
```
1. Same station as run109/run111, sun to one side. Look at: your ship's shadow on the deck,
   station parts shadowing each other across the whole station, the far edge of the shadows,
   and whether anything pops when you pitch or turn the camera (the run 38 defect).
2. At rest above the deck, press **Ctrl+Shift+F12** off for ~10 s, on for ~10 s, twice
   (the fill-cost A/B; note the frame-rate feel each time).
3. F8 twice: one wide station view, one close to a shadow edge on your hull.
4. Fly 1–2 km away and look back at the station; then one **gate jump** and one **save load**
   (the retention census needs both transitions), then quit.
Report: correct direction and placement yes/no, popping yes/no, self-shadow flicker yes/no,
how far shadows reach, seams between cascades (a visible change in softness), the distant
flicker every ~30 s seen in run 38 A (still there?), and the frame-rate feel with the toggle.

**Session A2** (same spot, 2–3 minutes, the 10 km far cascade):
```sh
<prefix> --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-cascades 250,1500,7500,50000 --shadow-cascade-sizes 4096,4096,4096,4096 --shadow-sun-poll on --shadow-retention-census
```
Look at distant stations (5–10 km) for shadows between their parts, and at the frame-rate
feel; one Ctrl+Shift+F12 A/B; one F8 on a distant station. The log's `c3=` / `capped3=`
counters decide 5 km vs 10 km.

**Session B** (retained casters drawn; 2–3 minutes, only after A ran):
```sh
<prefix> --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-cascades 250,1500,7500,25000 --shadow-cascade-sizes 4096,4096,4096,2048 --shadow-sun-poll on --shadow-caster-retention
```
At the station, turn the camera so a shadow-casting part leaves the screen: its shadow
must stay. Report any shadow that lingers after its caster is destroyed or moves, or any
crash/hang (this session holds references to game buffers across frames).

**Session C** (hull emitters, any station with position lights and signs, 1–2 minutes):
```sh
<prefix> --hull-emitters --hull-emission-gain 4
```
Press **Ctrl+Shift+F4** to toggle only the hull emitters; one F8 on the station. Report
whether position lights and signs read as lights at gain 4 and whether anything else on the
hull brightened. The F8 lines name the models that carry these emitters.

## 40. Five cascades to 30 km, 2048² A/B, corvette ladder, retained casters, hull emitters — completed as run117 (A), run118 (A2), run119 (B), run121/run122 (C)

Installed: run40 candidate `c47f039c…` from `e8ae3357` (see [status](../status.md)). Original hull
shading. New since run 39: the apply-quad half-pixel fix (the moving serrated band), the run116
flicker fix (back-face far cascades, verdict cycle), five cascades with a 30 km reach, caster pool control (importance drop order, per-cascade records; the
static-only rule stays available but is off after run116), own-ship-adaptive C0 with the
sliding ladder. Hotkeys: **Ctrl+Shift+F12** shadows at rest, **Ctrl+Shift+F4** hull
emitters, F6 effect gains, F8 capture.

Common prefix:
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --capture-start 999999 --capture-frames 8 --frame-phases --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --shadow-sun-trace --frame-end-stride 1
```
Shadow set (add to every shadow session):
```sh
--shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096
```

**Session A** (fighter save, 4096² maps, 5–8 minutes):
```sh
<prefix> <shadow set> --shadow-cascade-sizes 4096,4096,4096,4096,4096 --shadow-retention-census
```
Same station as run115. Check: the serrated band is gone; shadows correct in direction and
placement near and far; stations at 5–12 km now shadowed (fly out to the distance of your
distance1/distance2 screenshots and look again); seams between cascades. Ctrl+Shift+F12
off/on twice at rest. F8 twice (one close, one on a distant station). Report frame-rate feel.

**Session A2** (same save and spot, 2048² maps, 2–3 minutes):
```sh
<prefix> <shadow set> --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census
```
Compare the hull self-shadow and the nearest station wall against A; one Ctrl+Shift+F12 A/B;
one F8. Say which you prefer.

**Session B** (corvette save; retained casters drawn; 3–5 minutes):
```sh
<prefix> <shadow set> --shadow-cascade-sizes 4096,4096,4096,4096,4096 --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5
```
Look at the corvette's self-shadow and its shadow on a station. Turn the camera so a
shadow-casting station part leaves the screen: its shadow must stay. Report any lingering
shadow after a caster moves or is destroyed, any crash/hang, and the frame-rate feel. The log
gives the measured hull radius, the slid cascade set and the chase-camera distance.

**Session C** (hull emitters, any station with position lights and signs, 1–2 minutes):
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --capture-start 999999 --capture-frames 8 --frame-phases --hull-emitters --hull-emission-gain 4
```
Ctrl+Shift+F4 toggles only the hull emitters; one F8 on the station. Report whether position
lights and signs read as lights at gain 4 and whether anything else brightened.

## 41. Receiver depth A/B, slope margin A/B, hull light-map gain, FPS overlay — completed as run123 (A), run124 (A2), run125/run126 (B), run128 (C)

Installed: run41 candidate from `e1c4afcc` (hash in [status](../status.md)). New since run 40:
`--sun-shadow-receiver-depth linear` (RT2 stores linear view depth; the far-station flicker fix; in the
installed run41 build the default was `device` = old behaviour, the A/B; on main since A2 ratified it,
`linear` is the only encoding, the option a no-op and `device` refused), the cascade apply slope margin
(default on, `--sun-shadow-bias-slope-texels 0`
turns it off; the run119 grazing-plane flicker), `--hull-lightmap-gain G` (windows and hull lights in the
original hull programs; in this build Ctrl+Shift+F4 toggles it together with the guide-light gain, and
from the next candidate F4 is the light-map gain alone while Ctrl+Shift+F6 switches the effects gain and
the guide lights together, the launcher defaulting `--hull-lightmap-gain` to 4 under `--hdr`), `--fps-overlay`
(Ctrl+Alt+F7 = Option+Ctrl+F7 toggles; line 1 FPS / frame ms / draws, line 2 shadows on/off), and the
telemetry fields `apply_us=` and `flip_c<k>=`. 2048² maps and K 1.5 are now the defaults in the shadow set.
Hotkeys: **Ctrl+Shift+F12** shadows at rest, **Ctrl+Shift+F4** hull emission (light-map gain plus the guide
lights in this build; the light-map gain alone from the next one, with **Ctrl+Shift+F6** taking the effects
gain and the guide lights), **Ctrl+Alt+F7** FPS overlay, F8 capture.

Common prefix:
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --capture-start 999999 --capture-frames 8 --frame-phases --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --frame-end-stride 1 --fps-overlay
```
Shadow set (every shadow session):
```sh
--shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census
```

**Session A** (fighter save, run117 station, old receiver encoding, 3–4 minutes):
```sh
<prefix> <shadow set>
```
Fly to the spot of your run117 distant-station flicker. At rest: F8 twice (one close station, one distant lit
station), Ctrl+Shift+F12 off/on twice, read the FPS overlay in the busy view and note the numbers with
shadows on and off.

**Session A2** (same save and spot, new receiver encoding, 3–4 minutes):
```sh
<prefix> <shadow set> --sun-shadow-receiver-depth linear
```
Same two F8 spots, same toggles, same FPS reading. Report: is the distant-station flicker gone; anything
changed near; FPS with and without shadows compared to A.

**Session B** (corvette save, retention live, new encoding, 2–3 minutes):
```sh
<prefix> <shadow set> --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --sun-shadow-receiver-depth linear
```
Go to the station part that flickered in run119; F8 there. Then relaunch the same command with
`--sun-shadow-bias-slope-texels 0` added, same spot, F8 again: report which of the two shows the
flicker. If a Terran solar power plant is reachable, look at the noisy leg and press Ctrl+Shift+F12: report
whether the noise stops with shadows off.

**Session C** (run122 station with windows, new encoding, hull light-map gain, 2 minutes):
```sh
<prefix> <shadow set> --sun-shadow-receiver-depth linear --hull-emitters --hull-emission-gain 4 --hull-lightmap-gain 4
```
At rest with the station filling the view: F8, Ctrl+Shift+F4, F8. Report whether windows and station
lights now read as lights at gain 4, whether anything else brightened (panels, decals, hull stripes), and
whether a window inside a shadow still glows. Say what gain you would want (2, 4, 8).

Report frame-rate feel per session and the time into the session of each F8.

## 42. Pre-render attribution, fade-route shimmer check, cull census, View Distance A/B — completed as run129 (A), run130 (B), run131 (C), run132 (D)

Installed: run42 candidate (hash in [status](../status.md)). New since run 41: linear receiver depth is the
only encoding (`--sun-shadow-receiver-depth linear` is a no-op, `device` refused); the fade-band motion
route now works under original shading (the run125 solar-panel shimmer: fading surfaces got the raw
jittered sample); Ctrl+Shift+F4 toggles the hull light-map gain alone (launcher default 4 under `--hdr`),
Ctrl+Shift+F6 toggles effects and guide lights together (guide lights take `--emission-source-gain`);
`--cull-census` (engine cull-pass rows on capture frames); `--game-phase-threshold-ms` (default 20) and
`--telemetry-draw`; `fade_route_mode` startup line. Hotkeys: **Ctrl+Shift+F12** shadows at rest,
**Ctrl+Shift+F4** hull light maps, **Ctrl+Shift+F6** effects + guide lights, **Ctrl+Alt+F7** FPS overlay, F8.

Common prefix:
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --frame-phases --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay
```
Shadow set (every session):
```sh
--shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census
```

**Session A** (telemetry only, no captures; corvette save; ≈ 4 minutes):
```sh
<prefix> <shadow set> --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --loop-phases --game-phases --pass-phases --residual-phases --telemetry-draw --frame-end-stride 10 --capture-start 999999 --capture-frames 0
```
Stand (a) in the run125 area where you saw ≈ 24 fps for 90 s (if it recurs, stay in it), (b) facing empty
space 30 s, (c) at the run117 station's ≈ 900-draw view 60 s. Note the FPS overlay at each spot. This
attributes the pre-render episode and measures the proxy's per-draw cost on the current build.

**Session B** (fade-route check; corvette save; ≈ 3 minutes):
```sh
<prefix> <shadow set> --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --capture-start 999999 --capture-frames 8 --frame-end-stride 1
```
Go back to the Terran solar power plant (run125 spots): are the panel arrays still shimmering? F8 once
near. Then find a thin distant object (an antenna or a mast at 3–10 km): does it still shimmer? F8 once on
it. Report both by eye.

**Session C** (cull census; fighter save; ≈ 2 minutes):
```sh
<prefix> <shadow set> --cull-census --capture-start 999999 --capture-frames 2 --frame-end-stride 1
```
At the run117 station, the same ≈ 900-draw view as run124 (FPS overlay ≈ 32 ms): F8 once at rest. One
more F8 at a second dense view if convenient. (Each captured frame writes up to 8,192 census rows.)

**Session D** (View Distance A/B, no proxy option; fighter save; ≈ 3 minutes):
Same command as C without `--cull-census`. At the same station view, read the FPS overlay (ms and
draws) for 30 s with View Distance **Very High** (current), then change the in-game graphics option to
**High**, return to the same view and read it again for 30 s. Report both readings; restore Very High
afterwards.

Report frame-rate feel per session and the time into the session of each F8.

## 43. Collide box cull, small-parts cull, shimmer fixes — completed as run133/134 (A), run135–138 (B), run139 (C)

Installed: run43 candidate (hash in [status](../status.md)). New since run 42: `--collide-box-cull`
(integer bounding-box early-out in the sector collide loop `0x0045d250`; the run129 26 ms pre-render
episode; pair counters on `loop_phases`), `--cull-small-parts <px>` (engine cull-pass minimum size for
nodes with no per-node threshold; run131 census: 403 draws / 9.5 ms under 2 px), the fade route for
modules behind the camera plane and the overlay arm for a node's translucent sub-mesh (the run130
residual shimmer), `unmatched=<reason>` on every route row, and two per-draw trims. Hotkeys unchanged:
**Ctrl+Shift+F12** shadows at rest, **Ctrl+Shift+F4** hull light maps, **Ctrl+Shift+F6** effects + guide
lights, **Ctrl+Alt+F7** FPS overlay, F8.

Every command below is complete (run 42 prefix and shadow set included); run from the repository root.

**Session A** (collide A/B; corvette save, the run125/run129 24 fps area; ≈ 4 minutes):
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --frame-phases --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --loop-phases --frame-end-stride 10 --capture-start 999999 --capture-frames 0
```
Hold the 24 fps spot 60 s and read the FPS overlay. Then relaunch with the same command plus
`--collide-box-cull`:
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --frame-phases --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --loop-phases --frame-end-stride 10 --capture-start 999999 --capture-frames 0 --collide-box-cull
```
Same spot, 60 s, read again (the pair counters are logged only in this launch). Then fly normally for 2 minutes with the option on: any collision that does not
happen (ramming an asteroid or a station part must still stop you), any docking oddity, any script event
that looks wrong. Report the two readings and anything odd.

**Session B** (small-parts cull A/B; fighter save, the run117 station ≈ 900-draw view; ≈ 6 minutes, three launches):
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --frame-phases --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --cull-small-parts 2 --cull-small-parts-scope bodies --capture-start 999999 --capture-frames 2 --frame-end-stride 1
```
At the run131 view: FPS overlay reading (ms and draws), F8 once. Then approach a station from 10 km to
1 km and watch for pop-in. Relaunch with `--cull-small-parts-scope all` in place of `bodies` (whole far objects *and* small
sub-parts):
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --frame-phases --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --cull-small-parts 2 --cull-small-parts-scope all --capture-start 999999 --capture-frames 2 --frame-end-stride 1
```
Same view, same reading, same approach: do small glowing parts of the station you are near
disappear, and is the extra frame time worth it? Optionally a third launch with `--cull-small-parts 4
--cull-small-parts-scope bodies`. Report the readings and which you would keep: off / bodies 2 / bodies 4 / all 2.

**Session C** (shimmer check; corvette save; ≈ 3 minutes):
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --frame-phases --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --cull-small-parts 2 --cull-small-parts-scope bodies --capture-start 999999 --capture-frames 8 --frame-end-stride 1
```
The Terran solar power plant (both legs) and the distant object of run130: still shimmering? F8 once on
each. Then Ctrl+Shift+F4 off/on once at a station with windows to confirm the glow itself is stable.

Report frame-rate feel per session and the time into the session of each F8.

## 44. Collide narrow-phase census, TAA shimmer A/B, new defaults — completed as run140/141 (A), run142–146 (B), run147 (C); mip-bias A/B run148/149

Installed: run44 candidate (hash in [status](../status.md)). New since run 43: `--collide-narrow-census`
(counts and times the engine's exact mesh-vs-mesh collision tests and lists the object pairs on F8),
`--taa-current-filter A` and `--taa-history-weight W` (two candidate fixes for the thin-geometry shimmer),
`--cull-small-parts 2` with scope `all` **on by default** (`--cull-small-parts 0` = off), the proxy per-draw
saving (lever 1), `--capture-frames` up to 64. Hotkeys unchanged. Every command is complete; run from the
repository root.

**Session A** (collide census; corvette save, the 24 fps area of run133; ≈ 3 minutes):
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --frame-phases --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --loop-phases --collide-narrow-census --frame-end-stride 1 --capture-start 999999 --capture-frames 2
```
Fly to the spot, wait until the FPS has dropped to its ~25 fps plateau, hold 30 s, press **F8 once**, hold
another 30 s. Then fly away until the FPS recovers and press F8 once more. The FPS with this option on is
not comparable to other runs (the census costs time). Report the two F8 times.

**Session B** (TAA shimmer A/B; corvette save at the Terran solar power plant, 4–5 km, camera nearly still;
five short launches, ≈ 2 minutes each; look at the panel legs and at a distant station with glowing windows):
1. Baseline with resolved-frame dumps (**about 1.5 GB in /tmp per F8**):
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --frame-phases --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-debug --capture-start 999999 --capture-frames 32 --frame-end-stride 1
```
2. Filtered current sample, with dumps:
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --frame-phases --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-debug --capture-start 999999 --capture-frames 32 --frame-end-stride 1 --taa-current-filter 1.0
```
3. History weight 0.95 (watch for trails behind moving ships and slower settling after a view change):
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --frame-phases --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --capture-start 999999 --capture-frames 8 --frame-end-stride 1 --taa-history-weight 0.95
```
4. Both:
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --frame-phases --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --capture-start 999999 --capture-frames 8 --frame-end-stride 1 --taa-current-filter 1.0 --taa-history-weight 0.95
```
5. Both, without the sharpen pass:
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --frame-phases --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --capture-start 999999 --capture-frames 8 --frame-end-stride 1 --taa-current-filter 1.0 --taa-history-weight 0.95 --taa-sharpen 0
```
For each: shimmer on the legs / windows / antennas (gone, less, same), softness of the whole image, any
ghost trails. **Two F8 per launch**: one at the plant, then load the fighter save and one at the distant
station view (hold the view still 5–10 s after loading before F8; launches 1–2 then write about 3 GB each).
Same spot and view in every launch. Report which one you would keep and which F8 was which.

**Session C** (new defaults; fighter save, the run117 busy station view; ≈ 2 minutes):
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --frame-phases --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --capture-start 999999 --capture-frames 8 --frame-end-stride 1
```
FPS overlay reading (ms and draws) at the ~900-draw view (now ≈ 480 draws with the default cull), F8 once;
then approach a station from 10 km to 1 km once more watching for pop-in.

Report frame-rate feel per session and the time into the session of each F8.

## 45. SSE2 collision box test A/B, adaptive TAA history weight — completed as run150/151/152 (A), run153/154 (B)

Installed: run45 candidate (hash in [status](../status.md)). New since run 44: `--collide-sat-sse2` (the engine's
x87 box-overlap test of its mesh collider replaced by an SSE2 one; fixture 6–9× per call; never prunes a pair the
engine keeps), a triangle-test counter in `--collide-narrow-census`, and three default-off TAA options:
`--taa-thin-clip`, `--taa-adaptive-weight`, `--taa-alpha-history`. Every command is complete; run from the
repository root.

**Session A** (collision A/B; corvette save, the 24 fps area; three launches, ≈ 2 minutes each):
1. Census only (reference):
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --frame-phases --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --loop-phases --collide-narrow-census --frame-end-stride 1 --capture-start 999999 --capture-frames 2
```
2. Census + SSE2 box test:
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --frame-phases --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --loop-phases --collide-narrow-census --frame-end-stride 1 --capture-start 999999 --capture-frames 2 --collide-sat-sse2
```
3. SSE2 box test alone (the FPS that counts; the census costs time):
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --frame-phases --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --collide-sat-sse2 --capture-start 999999 --capture-frames 2 --frame-end-stride 1
```
In each: fly to the same spot, wait for the FPS plateau, hold 30 s, **F8 once**, note the FPS overlay reading.
In launch 3 afterwards fly normally for 2–3 minutes near the station: ram an asteroid or a station part on
purpose (it must still stop or damage you), dock once, and report anything odd about collisions.

**Session B** (adaptive TAA weight; corvette save at the solar plant, lattice at the angle that crawls; two
launches, each F8 writes about 1.5 GB):
1. Baseline:
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --frame-phases --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-debug --capture-start 999999 --capture-frames 32 --frame-end-stride 1
```
2. Adaptive history weight + accumulated alpha for the glow:
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --frame-phases --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-debug --capture-start 999999 --capture-frames 32 --frame-end-stride 1 --taa-thin-clip 0.75 --taa-adaptive-weight 0.97 --taa-alpha-history
```
In each: first hold the ship as still as you can (throttle zero, no rotation) for 10 s and judge the lattice,
**F8**; then let it drift/rotate slowly as in earlier runs and judge again, **F8**; then load the fighter save,
distant station view, hold still 10 s, **F8**. Report for each of the three situations: crawl/shimmer gone,
less or same; any ghost trails behind moving ships; blinking lights looking sluggish; overall sharpness.

Report frame-rate feel per session and the time into the session of each F8.

## 46. Collision memo, lattice line filter, far stabiliser, frame attribution — completed as run155/156 (A), run157–159 (B), run160/161 (C), run162 (D)

Installed: run46 candidate (hash in [status](../status.md)). New since run 45, all default off: `--collide-memo`
(a pair whose inputs are bit-identical to the previous frame and had no contact is not re-tested; contacts are
always recomputed; `--collide-memo-verify` still runs the engine on every would-be hit and counts mismatches),
`--taa-line-filter A[,W]` (softens only thin geometry lines with background on both sides: the lattice crawl) and
`--taa-far-stabiliser W[,A]` (longer history, optionally a soft filter, only on far pixels: the distant station
lines). Every command is complete; run from the repository root.

**Session A** (collision memo; corvette save, the 24 fps area; two launches, ≈ 3 minutes each):
1. Verify mode (no speed-up expected; it proves the memo never answers wrongly):
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --frame-phases --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --loop-phases --collide-sat-sse2 --collide-memo-verify --capture-start 999999 --capture-frames 2 --frame-end-stride 1
```
Hold the spot 60 s, then fly around the station for a minute, ram something once, dock once if convenient.
2. Memo on (the FPS that counts):
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --frame-phases --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --collide-sat-sse2 --collide-memo --capture-start 999999 --capture-frames 2 --frame-end-stride 1
```
Same spot, wait for the plateau, hold 30 s, note the FPS overlay; then fly normally 2 minutes incl. one deliberate
collision. Report both FPS readings and anything odd about collisions.

**Session B** (lattice crawl; corvette save at the solar plant, lattice at the crawling angle, ship drifting
slowly; three launches, one F8 each ≈ 1.5 GB):
1. Baseline:
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --frame-phases --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-debug --capture-start 999999 --capture-frames 32 --frame-end-stride 1
```
2. Line filter, strength 1, lines up to 2 px:
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --frame-phases --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-debug --capture-start 999999 --capture-frames 32 --frame-end-stride 1 --taa-line-filter 1,2
```
3. Line filter, milder (strength 2 = narrower blur), lines up to 2 px:
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --frame-phases --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-debug --capture-start 999999 --capture-frames 32 --frame-end-stride 1 --taa-line-filter 2,2
```
Judge the crawl drifting and still, and whether anything else on screen (hull edges, text, other stations) looks
softer than baseline. **F8** once while drifting.

**Session C** (distant station lines; fighter save, the distant station view; two launches, one F8 each):
1. Far stabiliser, weight only:
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --frame-phases --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-debug --capture-start 999999 --capture-frames 32 --frame-end-stride 1 --taa-far-stabiliser 0.985
```
2. Far stabiliser, weight + soft filter:
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --frame-phases --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-debug --capture-start 999999 --capture-frames 32 --frame-end-stride 1 --taa-far-stabiliser 0.985,1
```
Judge the station still and while moving/turning slowly: shimmer gone, less or same; sharpness of the far station
against the run 45 baseline; any ghost trails behind distant moving ships; far blinking lights looking sluggish.

**Session D** (frame attribution, no judging needed; fighter save at the busy station ≈ 480-draw view, ≈ 2 minutes):
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --frame-phases --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --collide-sat-sse2 --collide-memo --loop-phases --pass-phases --frame-timing --frame-timing-state-stamps 8 --capture-start 999999 --capture-frames 2 --frame-end-stride 1
```
Hold the busy view 60 s, **F8** once. This splits the 11 ms `view_submit` share into engine, proxy and driver.

Report frame-rate feel per session and the time into the session of each F8.

## 47. Relaxed collision memo, lazy render targets, engine profile, far stabiliser speed gate — completed as run163/164 (A), run165–167 (B), run168–171 (C), run172/173 (D); extra: run174 (Argon Prime fog captures), run175 + screenshots/lattice.mov

Installed: run47 candidate (hash in [status](../status.md)). New since run 46: `--collide-sat-sse2` and
`--collide-memo` are **on by default** (`--no-collide-sat-sse2`, `--no-collide-memo` switch them off); the memo
also answers a pair whose only difference is the engine's running-minimum value when the stored run never reached
a triangle test, and its log row names the reason for every miss; `--motion-rt-mode lazy` (the proxy keeps its
motion targets bound across routed draws; bench 1.7–2.5 µs per draw); the far stabiliser lets go of its long
history between 0.03 and 0.25 px/frame of screen motion instead of 0.5–2 (the run 46 C blur). Every command is
complete; run from the repository root.

**Session A** (collision; corvette save, the former 24 fps area; two launches, ≈ 2 minutes each):
1. Verify mode for the relaxed rule:
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --frame-phases --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --loop-phases --collide-memo-verify --capture-start 999999 --capture-frames 2 --frame-end-stride 1
```
Hold the spot 60 s, fly around the station for a minute, one deliberate collision.
2. Defaults (the FPS that counts):
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --frame-phases --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --loop-phases --capture-start 999999 --capture-frames 2 --frame-end-stride 1
```
Same spot, plateau, hold 30 s, note the FPS overlay.

**Session B** (per-draw cost and engine profile; fighter save at the busy station ≈ 480-draw view; three
launches, hold the view 60 s in each and note the FPS overlay):
1. Per-draw render-target mode (today's default):
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --frame-phases --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --motion-rt-mode perdraw --frame-timing --capture-start 999999 --capture-frames 2 --frame-end-stride 1
```
2. Lazy mode:
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --frame-phases --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --motion-rt-mode lazy --frame-timing --capture-start 999999 --capture-frames 2 --frame-end-stride 1
```
3. Lazy mode with the sampling profiler (finds where the engine's ≈ 10 ms between API calls goes):
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --frame-phases --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --motion-rt-mode lazy --profile --profile-interval-us 500 --capture-start 999999 --capture-frames 2 --frame-end-stride 1
```
Report the three FPS readings and anything that looks wrong in lazy mode (missing TAA on some objects, smearing,
flicker of shadows or glows).

**Session C** (distant station, far stabiliser with the new speed gate; fighter save, the distant station view):
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --frame-phases --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-debug --capture-start 999999 --frame-end-stride 1 --capture-frames 32 --taa-far-stabiliser 0.985
```
Judge still, drifting slowly, and **turning the camera slowly and fast**: shimmer and blur. **F8** once while
turning slowly. If it is still soft in slow motion, relaunch with `--taa-far-stabiliser 0.985,0,80,130,0.02,0.15`;
if the shimmer is back while drifting, with `--taa-far-stabiliser 0.985,0,80,130,0.05,0.5`. Report which you keep.

**Session D** (lattice crawl, one experiment left on the screen side; corvette save at the plant, lattice at the
crawling angle, drifting at the speed where the crawl looks worst; two launches, **F8** once each ≈ 3 GB):
1. Baseline, 64 frames:
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --frame-phases --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-debug --capture-start 999999 --frame-end-stride 1 --capture-frames 64
```
2. Line filter plus a longer history:
```sh
./x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --frame-phases --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-debug --capture-start 999999 --frame-end-stride 1 --capture-frames 64 --taa-line-filter 1,2 --taa-history-weight 0.97
```
Report whether 2 crawls less, and how much dimming of the lattice at that distance you would accept if the fix has
to fade it.

Report frame-rate feel per session and the time into the session of each F8.


## Run 60 (completed 2026-09-21: run212, run214)

**Run 60 (queued 2026-09-21): Run60 DLL `aaa8abb2…`. Two sessions; both dry-runs verified.**

Session A, TAA at the solar plant. (1) Fly forward toward and past the plant, without and with SETA: strut flicker gone / reduced / same. (2) Approach a station from far away and watch for the one-time arm/panel flash. (3) Pan and roll as before, to confirm nothing regressed. (4) Judge strut blur while moving; then repeat the session with `0.94,1` in place of `0.97` and say which you prefer. One F8 during forward flight is enough (32 frames). Quit normally and confirm the game closes.

```sh
env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 X3M_MOTION_FRAME_LOG=1 /Users/asvetl/x3-mod/x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --light-map-far-fade 80,220 --motion-rt-mode lazy --frame-end-stride 1 --taa-unmatched-static node --taa-thin-region 0.97 --taa-debug --capture-start 999999 --capture-frames 32
```

Session B, stored long-range fog, in a fog sector (bluewell or foggreenoutlands family). Watch for: clouds visible out to 30–40 km without an abrupt edge; no pop when the fog appears after load (about 1–2 s ramp, vanilla cards stay until the far fog is fully in); the same cloud layout when you leave and re-enter the sector and after a reload; FPS with fog on versus off (Ctrl+Alt+F9 toggles; overlay Ctrl+Alt+F7); any hitch while flying fast or under SETA; and that quitting the game does not hang. One F8 facing distant clouds.

```sh
env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 X3M_MOTION_FRAME_LOG=1 /Users/asvetl/x3-mod/x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --light-map-far-fade 80,220 --motion-rt-mode lazy --frame-end-stride 1 --taa-unmatched-static node --taa-thin-region 0.97 --volumetric-fog 0.03 --volumetric-fog-cards replace --volumetric-fog-range stored --capture-start 999999 --capture-frames 8
```



## Run 61 (completed 2026-09-22: run215, run216, run220)

**Run 61 (queued 2026-09-21): Run61 DLL `0bc8ff36…` from `ed105485`. Two sessions; both dry-runs verified.**

Session A, TAA. (1) Solar plant, fly forward toward and past it, without and with SETA: strut crawl gone / reduced / same (the panel-glass gate fix; replay predicts about half). (2) Pan up/down at rest on the plant. (3) Distant stations (The Hole or similar) while panning vertically: shimmer with `--taa-sentinel-stabiliser 0.7` versus a second launch without that option; note any blur of far detail during slow pans. (4) Fire lasers across open sky and watch ships passing in front of lattices: any trails or smears. (5) Approach a station: the one-time flash should stay gone (`node` is now the default; no option needed). (6) Blur preference: repeat with `0.94,1` in place of `0.97`. F8 bursts: forward flight, slow vertical pan across distant stations, one at rest, one firing lasers over sky.

```sh
env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 X3M_MOTION_FRAME_LOG=1 /Users/asvetl/x3-mod/x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --light-map-far-fade 80,220 --motion-rt-mode lazy --frame-end-stride 1 --taa-thin-region 0.97 --taa-sentinel-stabiliser 0.7 --taa-debug --capture-start 999999 --capture-frames 32
```

Session B, stored fog with look presets, fog sector with a station and the sun in view. Ctrl+Alt+F11 cycles L0 (old look) → L1 (shaped, thicker, sun glow, ambient, soft warped edges) → L2 (+cloud self-shadow) → L3 (+sample jitter); the overlay shows `FOG … Ln`; Ctrl+Alt+F10 still changes strength. Look for: (1) toward versus away from the sun: bright side and a differently coloured dark side; (2) real gaps with untouched skybox, cloud bodies with edges, and whether the "bulbs" at patch edges are gone or need more work; (3) station between you and the sun inside a cloud: shafts, and the former black smear now coloured; any hard ring in the shafts; (4) distant objects sinking into the fog colour; (5) L3 versus L2 while strafing and at rest: banding gone, no boiling; (6) FPS / fog timing per preset. One F8 burst per preset from the same pose.

```sh
env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 X3M_MOTION_FRAME_LOG=1 /Users/asvetl/x3-mod/x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --light-map-far-fade 80,220 --motion-rt-mode lazy --frame-end-stride 1 --taa-thin-region 0.97 --volumetric-fog 0.03 --volumetric-fog-cards replace --volumetric-fog-range stored --volumetric-fog-look 1 --volumetric-fog-timing --capture-start 999999 --capture-frames 8
```



## Run 62 (completed 2026-09-22: run221, run222)

**Run 62 (queued 2026-09-22): Run62 DLL `924be5c0…` from `35f5a9cf`. Two short sessions; both dry-runs verified.**

Session A, TAA far plane. The far-plane history fix: distant stations (and stars/sky) while panning horizontally and vertically, slow and fast: shimmer gone / reduced / same, compared with Run 61. One F8 burst during a moderate pan across the distant solar plant. Nothing else changed for TAA.

```sh
env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 X3M_MOTION_FRAME_LOG=1 /Users/asvetl/x3-mod/x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --light-map-far-fade 80,220 --motion-rt-mode lazy --frame-end-stride 1 --taa-thin-region 0.97 --taa-sentinel-stabiliser 0.7 --taa-debug --capture-start 999999 --capture-frames 32
```

Session B, stored fog. Starts in L2 at 1.0x. (1) The ribbed pattern should be gone from every direction; if any regular stripes remain, screenshot and note the heading. (2) Patch edges: organic enough, or are the "bulbs" back? (3) Far range: full fog to 13 km, fading out by 22.5 km; judge the fade-in of clouds while flying toward a distant station. To compare ranges in the same flight restart with `X3M_FOG_LOOK_TAPER_START=52500 X3M_FOG_LOOK_SKY_CAP=70000` (14 km) or `150000` / `200000` (35 km) in front of the command. (4) FPS against fog off (Ctrl+Alt+F9). One F8 burst in L2.

```sh
env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 X3M_MOTION_FRAME_LOG=1 /Users/asvetl/x3-mod/x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --light-map-far-fade 80,220 --motion-rt-mode lazy --frame-end-stride 1 --taa-thin-region 0.97 --volumetric-fog 0.02 --volumetric-fog-cards replace --volumetric-fog-range stored --volumetric-fog-timing --capture-start 999999 --capture-frames 8
```



## Run 63 (completed 2026-09-22: run223, run224; run225/run227 glow-line captures)

**Run 63 (queued 2026-09-22): Run63 DLL `b0cde491…` from `5112bb43`. Two sessions; both dry-runs verified.**

Session A, sun and shadows (fog off). (1) Sun: slide the sun slowly behind a station edge and back, then behind a ship: the glare should now fade with the covered fraction instead of popping off at about half, and must never shine through solid geometry. Also look at the sun in clear sky (any dimming or flicker there is a bug) and through a cockpit monitor if handy. F8 bursts: sun fully visible in clear sky (needed to fingerprint the flare draws for step two), sun half covered, sun just fully covered. (2) Shadows: caster retention engages in flight for the first time. With a station close by, turn away so it leaves the screen and check that its shadow on your hull / nearby hulls stays; watch for stale shadows from ships that left or docked while unseen, and for shadow pops when you look back. (3) TAA regression glance: panel crawl, distant stations under pans (the stabiliser is now default, no option on the line).

```sh
env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 X3M_MOTION_FRAME_LOG=1 /Users/asvetl/x3-mod/x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --light-map-far-fade 80,220 --motion-rt-mode lazy --frame-end-stride 1 --taa-thin-region 0.97 --sun-occlusion --sun-occlusion-log --taa-debug --capture-start 999999 --capture-frames 32
```

Session B, stored fog (L2, 1.0x). (1) The station shadow in fog: the smeared comb should now be clean shafts that stay put as you move; the shaft should no longer vanish when the station leaves the screen (retention). Compare with `X3M_FOG_LOOK_SHADOW_JITTER=0` in front of the command (old behaviour). (2) Look closely at station silhouettes that stand in front of a shaft: any thin bright/dark rim? (3) Optional: `X3M_FOG_LOOK_SHADOW_FLOOR=.3` for softer shafts. (4) FPS against fog off. F8: one burst with a shaft in view.

```sh
env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 X3M_MOTION_FRAME_LOG=1 /Users/asvetl/x3-mod/x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --light-map-far-fade 80,220 --motion-rt-mode lazy --frame-end-stride 1 --taa-thin-region 0.97 --volumetric-fog 0.02 --volumetric-fog-cards replace --volumetric-fog-range stored --volumetric-fog-timing --capture-start 999999 --capture-frames 8
```

## Run 64 (completed 2026-09-22: run228, run229 (sun), run231 (widening), run232 (fog), run233 (sentinel stabiliser off A/B))

**Run 64 (queued 2026-09-22): Run64 DLL `e839dc7c…` from `6837a968`. Three sessions; all dry-runs verified.**

Session A, sun (fog off). Run 63's sun observations were vanilla: the override never engaged (the sun record belongs to the background view). It should engage now, and the core disc is clipped per pixel against geometry. (1) Slide the sun slowly behind a station edge and back: the covered part of the disc should vanish behind the edge with a soft rim while the uncovered part stays at full brightness; the glare/streaks should dim with the covered fraction; nothing should pop at half coverage and nothing should shine through solid geometry. (2) Sun in clear sky: no dimming or flicker. (3) Ships: a ship crossing the sun should clip it too; watch for any flare oddity when a ship's own engine flare lines up with the sun. F8 bursts: clear sky, half covered, just fully covered (the log now also saves the final frame with the chain in it). Optional A/B: `X3M_SUN_OCCLUSION_CORE_F=1` in front of the command also dims the clipped disc by the visible fraction.

```sh
env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 X3M_MOTION_FRAME_LOG=1 /Users/asvetl/x3-mod/x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --light-map-far-fade 80,220 --motion-rt-mode lazy --frame-end-stride 1 --taa-thin-region 0.97 --sun-occlusion --sun-occlusion-log --taa-debug --capture-start 999999 --capture-frames 32
```

Session B, thin glow lines (fog off). At the run225 station (1–2 km, spar and drum in view) with `--hull-emissive-widening 3,2,8`: (1) still: are the glow strips continuous instead of torn? (2) pitching/moving the camera as in run225: flicker gone / reduced / same? (3) close up (inside 1 km) nothing should change; (4) large lit panels must not dim or blur; (5) any colour shift on distant strips (DXT1 mips). Try `2,2,8` if 3 looks too soft, or `3,3,10` if the softening starts too close. F8 bursts: still and pitching. Also glance at a Boron/Teladi/Split/Paranid station if one is nearby: every family is transformed.

```sh
env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 X3M_MOTION_FRAME_LOG=1 /Users/asvetl/x3-mod/x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --light-map-far-fade 80,220 --motion-rt-mode lazy --frame-end-stride 1 --taa-thin-region 0.97 --hull-emissive-widening 3,2,8 --taa-debug --capture-start 999999 --capture-frames 32
```

Session C, stored fog regression (L2, 1.0x), short: same as Run 63 session B; just confirm nothing changed.

```sh
env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 X3M_MOTION_FRAME_LOG=1 /Users/asvetl/x3-mod/x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --light-map-far-fade 80,220 --motion-rt-mode lazy --frame-end-stride 1 --taa-thin-region 0.97 --volumetric-fog 0.02 --volumetric-fog-cards replace --volumetric-fog-range stored --volumetric-fog-timing --capture-start 999999 --capture-frames 8
```

## Run 65 (completed 2026-09-22: run235 (sun/SETA), run236/run237 (widening 3/4), run238/run239/run240/run242/run243 (fog regression and the frame-time stand))

**Run 65 (queued 2026-09-22): Run65 DLL `2d11aac4…` from `85fbc868`. Three sessions; all dry-runs verified. New in every session: `--capture-delay 300`: F8 arms a burst that fires about five seconds later (300 frames), so press F8, re-engage SETA, keep flying; the log line `capture_armed` confirms the arming.**

Session A, sun (fog off). Run 64 showed the override engaging; two things to check: (1) the shimmer at the edge where a station or ship covers the sun should be much reduced (the rim is now a smooth five-pixel gradient and the visible fraction no longer flips with the TAA jitter); a small residual breathing of the rim is expected, a hard one-pixel jump is not. (2) The black smear when approaching a station fast under SETA: fly at a station that is off-centre in the view, press F8, re-engage SETA and let the delayed burst fire while the station slides across the screen; say whether the smear trails on the side the station is moving away from, and whether it also appears at top speed without SETA. Core dimming is now the default whenever `--sun-occlusion` is on (accepted in run235): the clipped disc is also dimmed by the visible fraction. `--sun-occlusion-core-f off` restores clip-only (the Run64 behaviour); a shell `X3M_SUN_OCCLUSION_CORE_F` is ignored, only the option works. F8 bursts: half covered while still, plus the SETA approach.

```sh
env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 X3M_MOTION_FRAME_LOG=1 /Users/asvetl/x3-mod/x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --light-map-far-fade 80,220 --motion-rt-mode lazy --frame-end-stride 1 --taa-thin-region 0.97 --sun-occlusion --sun-occlusion-log --taa-debug --capture-start 999999 --capture-frames 32 --capture-delay 300
```

Session B, thin glow lines (fog off). Widening rebuilt: k is decided per pixel from the light map's own texel size (no growing or shrinking under yaw) and a guarded boost restores a thin strip's brightness; the emissive vote of the thin region also makes TAA hold the history of bright thin peaks (rest flicker about three times smaller). Both are now launcher defaults after run236/run237 (2026-09-22) and are no longer spelled in the command: omitting `--hull-emissive-widening` gives `4,4` because the light-map gain is active, and omitting `--taa-thin-region-emissive` gives `1` because `--hdr` and the thin region are on, so this session exercises the resolved defaults. At the run225/run231 station (1–3 km): (1) still, facing the strips: continuous and about as bright as up close, no longer dim? (2) rest flicker reduced? (3) yaw and pitch: strips must not grow or shrink; (4) large lit panels unchanged; look at panel corners, especially panels seen diagonally: bright corner dots are a known gap, report them; (5) close up nothing changes; (6) fps overlay: note the frame time versus Run 64 session B at the same spot. If the strips look too bright, try `--hull-emissive-widening 3,2`; `--hull-emissive-widening off` and `--taa-thin-region-emissive 0` are the opt-outs. F8 bursts: still facing the strips, and a slow yaw sweep through frontal.

```sh
env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 X3M_MOTION_FRAME_LOG=1 /Users/asvetl/x3-mod/x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --light-map-far-fade 80,220 --motion-rt-mode lazy --frame-end-stride 1 --taa-thin-region 0.97 --taa-debug --capture-start 999999 --capture-frames 32 --capture-delay 300
```

Session C, stored fog regression (the single look, 1.0x), short: fog source unchanged (the look presets L0/L1/L3 were retired on 2026-09-22 and `--volumetric-fog-look` is refused, so the command carries no look option and the overlay shows no `L` suffix; the drawn law is the accepted L2, proven bit-identical by the fog fixture); confirm nothing changed, and glance at glow strips with widening off (now explicit, `--hull-emissive-widening off`, since the default turns it on) for comparison with B.

```sh
env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 X3M_MOTION_FRAME_LOG=1 /Users/asvetl/x3-mod/x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --light-map-far-fade 80,220 --motion-rt-mode lazy --frame-end-stride 1 --taa-thin-region 0.97 --hull-emissive-widening off --volumetric-fog 0.02 --volumetric-fog-cards replace --volumetric-fog-range stored --volumetric-fog-timing --capture-start 999999 --capture-frames 8 --capture-delay 300
```

## Run 58 (completed 2026-09-22: run206)

**Run 58 completed as run206 (2026-09-21): two bursts 5730–5761 and 6936–6967, recording `screenshots/lattice-taa.mov` (crawl visible at 0:00–0:05 and 0:46–0:49); user reports the crawl still visible, perhaps slightly less, and accepts a little ghosting if it fixes the crawl. Triage in progress.** Original instructions: **Run 58 (queued 2026-09-21): moving-lattice ground truth on the installed build, no new DLL.** Solar plant at the run177 position. Take two TAA-debug captures of 32 frames each with F8: (a) a **slow pan**, barely turning so the struts move about 0.1–0.5 px per frame; (b) a **fast pan** at the usual speed. Screen-record each pan for a few seconds. Report the `/tmp/x3-bottleX3-runNN` path and one sentence per pan: crawl gone, reduced, or replaced by blur or trails. Both global history-cut heuristics are disabled as in Run57. Command below; dry-run verified.

```sh
env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 X3M_MOTION_FRAME_LOG=1 X3M_MOTION_CUT_MEDIAN_PX=1e30 X3M_MOTION_CUT_MISSING=1 /Users/asvetl/x3-mod/x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --taa-thin-region 0.97 --light-map-far-fade 80,220 --motion-rt-mode lazy --frame-end-stride 1 --taa-debug --capture-start 999999 --capture-frames 32
```

## Run 59 (completed 2026-09-22: run207, run208)

**Run 59 completed as run207 (A) and run208 (B), 2026-09-21: user reports the crawl "I think it's fixed" with the camera gate; ship/camera roll (Q/E) still produces it slightly. Triage in progress.** Original instructions: **Run 59 (queued 2026-09-21): camera-relative thin-region gate A/B on the Run59 DLL `b1bb05fb…`.** Two sessions at the solar plant (run177 position), same pans in each: a steady pan at the usual speed and a gentle one. One F8 mid-pan per pan (32 frames each), a short screen recording of each session. Report both `/tmp/x3-bottleX3-runNN` paths and, per session: crawl on the struts (gone / reduced / same), softness of the struts while panning, any ghost trail where something bright crosses them, and anything odd on other thin geometry (antennas, distant stations). Session A is today's behaviour on the new DLL and also confirms nothing regressed; session B turns the new gate on. Both dry-runs verified.

Session A (installed behaviour):

```sh
env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 X3M_MOTION_FRAME_LOG=1 /Users/asvetl/x3-mod/x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --taa-thin-region 0.97 --light-map-far-fade 80,220 --motion-rt-mode lazy --frame-end-stride 1 --taa-debug --capture-start 999999 --capture-frames 32
```

Session B (camera-relative gate):

```sh
env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 X3M_MOTION_FRAME_LOG=1 /Users/asvetl/x3-mod/x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --taa-thin-region 0.97 --light-map-far-fade 80,220 --motion-rt-mode lazy --frame-end-stride 1 --taa-debug --capture-start 999999 --capture-frames 32 --taa-thin-region-gate camera
```

## Completed-run table (archived rows)

| Run | Purpose | Sessions | Status |
| --- | --- | ---: | --- |
| 1 | Chase aiming/framing + reader/adjacency verification | 0 | Accepted as run 18 |
| 2 | Sharpen/shimmer + camera cuts with TAA | 0 | Merged into run 6 |
| 3 | Automatic exposure + bloom off/on | 0 | Completed: A run 24; B run 25 exposed bloom initialization failure |
| 4 | Vanilla double-cursor/menu-bar comparison | 0 | Completed 2026-09-14 (vanilla, no snapshot): the double cursor reproduces in vanilla after alt-tab when the desktop cursor is moved outside the window before returning; vanilla behaviour, not a proxy regression |
| 5 | Reader/adjacency fast modes | 0 | Accepted as run 19 |
| 6 | Linear hull materials off/on plus sharpen/cuts at fixed exposure | 0 | Completed: A run 20, B runs 21–23; analysis/quality follow-ups remain |
| 7 | Fixed/automatic exposure and bloom toggles, central chase HUD and selection timing | 0 | Completed as run 26; follow-ups combined into run 8 |
| 8 | Restored glow, milder exposure and native selection-stutter trace | 0 | Completed as run 27 |
| 9 | Stronger glow and selection/voice timing | 0 | Completed as run 28 on source `d9413fc` |
| 10 | Target-name speech with the opt-in WMA decoder | 0 | Superseded: load hang fixed by the DMO fallback hook; speech accepted as run 18 |
| 11 | Fade region route and alpha-tested cutout, combined | 1 | Completed as user run 11, snapshot run36 |
| 12 | Voice load-hang Wine trace witness (no new build) | 0 | Completed as user run 12, snapshot run37 (trace `/tmp/x3-witness-quartz.log.z`, 5.0 GB) |
| 13 | Target-name speech with the decoder plugin and the DMO fallback hook | 0 | Attempted as run 38 (execute-access fault, fixed); retried and completed as run 16 |
| 14 | Station source-over linear route, fade region and shimmer trace, combined | 0 | Completed as user run 14, snapshot run39 (witness clean, port composed, darkening persists) |
| 16 | Run 13 retry: target-name speech with the decoder plugin and the fixed DMO fallback hook | 0 | Completed as user run 16, snapshot run41: loads, speech works, crackle under investigation |
| 17 | Bullet bound after near-plane clipping, packed_sample brightness | 0 | Completed as user run 17, snapshot run42: 100 % bound, witness clean, dimmer unresolved (centre sample) |
| 18 | Voice crackle fix: decoder plugin v4 (no new DLL) | 0 | Completed as user run 18, snapshot run46: no crackle, voice fine |
| 19 | Combined: AO off/on (Ctrl+Shift+F11), bullets at gain 1, cutout shimmer fix, same-port far/near pair | 0 | Completed as user run 19, snapshot run47: cutout exemption holds (`reason=3` 0.01 %), bolts accepted at gain 1; AO runs but is invisible at 2 m, port darkening and asteroid triangle dropout still open |
| 15 | Screen emission on bullets (packed policy 8 in the region bracket) | 0 | Completed as user run 15, snapshot run40: witness clean, 50 % of bullet draws refused (w ≤ 0), near-fullscreen brackets; bound fix in progress |
| 20 | Asteroid prepass jitter, port and ship far/near pairs, step D bullets, AO radius 20, loading markers | 0 | Completed as user run 20, snapshot run48: asteroid triangle dropout fixed and accepted, step-D brackets no longer fullscreen, loading markers fired; AO ran in debug view only, port darkening still open |
| 21 | AO appearance at a readable footprint (`--ao-radius 100`, no debug view), bullet witness every frame, vanilla port approach | 1 | Completed as user run 21, snapshot run49 (session A only): the fade witness is clean on the firing frames and the bolts are accepted, AO is invisible at radius 100 and is now default-off, a new station-section jitter at ~4.7 km is under diagnosis; session B (vanilla port approach) has not been reported |
| 22 | LOD scale 2×, fade-band trembling fix, docking-port screenshot pair | 0 | Completed as user run 22, snapshot run51: trembling gone, LOD 2× applied, module darkening owned by point-light range |
| 23 | Material fill 0.06 at the run-51 station | 0 | Completed as snapshot run54; fill visibly works. User subsequently chose default 0.03 and Auto EV ceiling +1.0; defaults selected and applied |
| 24 | Chase reset-writer telemetry: gate jump and jumpdrive | 0 | Gate portion completed as run56; user confirms reset. Jumpdrive not run (no suitable save); cross-recreation diagnostics prepared for run25 |
| 25 | Consolidated loading, gate identity, forward HUD and sun-lane diagnostics | 0 | Completed as run60: forward HUD aligned, centre preferred; gate reset; trace analysis in progress |
| 26 | Original hulls with cheap HDR emitters, chase view restore, elevated-camera reticle, replay candidates, loading intervals (A); optional linear sun-lane refusal buckets (B) | 1 | Session A completed as run65 (60 files): bolts brighter and liked; engines unchanged; loading possibly faster; **camera still reset at the gate**. Session B completed as run66 (55 files). Run65 triage: restore refused at the consume seam on the ref cell (2/2), engine gain refused on separate-alpha blend, loading stall 21.5 s again, replay predicates all pass; follow-ups in flight |
| 27 | Next candidate: corrected chase restore, engine gain on separate-alpha, point-light root admission, original-program fill baseline, depth replay, mip bias/sharpen trial (A); linear sun lane after the state-gate fix (B) | 1 | Session A completed as run68 (108 files): engines brighter, halo around bolt impacts (weapon-effect sprites now gained), mip bias/sharpen kept as defaults, chase restore consumed on transits 1–2 but the view still reset on the return transit and no transfer on 3–4, docking modules partly dark with the point-light option (no per-node telemetry yet); A2 without the option in progress |
| 28 | Next candidate: restore re-arm fix, emitter gain split (engines vs weapon effects), original-program fill A/B, point-light telemetry (opt-in) | 0 | Completed as snapshots run74–run80 (session A) and run81 (session B), DLL `2b0969e5…` from `a26eb9b`: halo identified as bloom of the >1.0 bolts (bloom working-set 21 vs 13 MB is the sharpen-stage buffer, not resolution); chase restore consumed on both run78 transits, the centre-then-jump was the camera hook refusing the unbound view phase (37 and 13 frames), fixed `e4fd22a`; original fill 0.05 accepted visually, no clean frame-time A/B; engine-family gain admits draws, effect family 0; session B sun lane `available=0` on all 2,123 frames, fixed `e62722a` |
| 29 | Emitter hotkeys, bolt alpha, chase pose, frame timing, sun lane | 3 | Completed as run83 (A), run84 (A2 profiler), run85 (B): engines unchanged (gain refused screen blend), halo persisted (bloom amplitude), chase pose fixed, profiler blind under FEX, sun lane available 4577/4695 |
| 30 | Single emission gain, bloom source clamp, frame-time split | 2 | Session A run87 and B run88 received: engines respond, halo accepted at clamp 1.0, no cutout draws yet (B to repeat at an Argon industrial station), frame split: state hooks 8.9 ms of a 28.5 ms busy frame |
| 31 | Frame split, engine phases, lighter proxy | 2 | Session A run89 and B run90 received: busy frame 37.5 ms at 987 draws is 87 % engine view submission (63 state calls per draw), scene update 65 µs; lane available on all 16,041 frames, still zero cutout draws (third time) |
| 32 | Hybrid unhook, draw and state counters | 4 | A1 run91 (redundancy 95/99/40 %, batchability 5 %), A2 run92 (busy frame 37.5 to 26.5 ms unhooked), B run93 (cutout pairs draw everywhere, lane admission unobservable; slow sector is `pre_render` 98 % of a 420 ms frame); C queued with `--game-phases` |
| 33 | Pass phases, loop-region split, cutout lane telemetry | 3 | A run95 (view submit: draw 43 %, BeginPass 33 %, engine between passes 22 %), B run96 (stall 99.8 % in the per-sector object pass `0x0045b720`, one sector; GStreamer criticals repeat during it), C run97 (cutout draws admitted through the tested-opaque arm, ~90/frame with the lane share; linear materials + lane +3 ms) |
| 34 | Stall evidence: stderr capture, module identity, media-cue trace and cache | 4 | A1 run98: the stalling cue is id 2 = `mov\00002.dat` (MPEG-1 video), one failed build per frame at ~390 ms, GStreamer bursts aligned within 16 ms; native d3dx9_37 loads. A2 run99: cache on, one real attempt per 30 s, frames back to 7–9 ms p50 in the sector (stall gone, user confirmed); the exit-time "zero area" warning is pre-existing teardown noise (31 lines in run98 too). A3 run100/101: the v5 decoder runtime hangs the main loop on the first comm dialog (avatar video graph now builds and blocks in Wine's video path); v5 parked, cache is the fix. B pending |
| 35 | D3DX builtin vs native (no new DLL) | 1 | Completed as run103: visuals unchanged; identity line could not prove the builtin loaded; BeginPass 8.90 vs 6.64 µs/pass confounded by build; repeat as run 36 on one build |
| 36 | D3DX builtin vs native on one build (A1/A2); first sun shadows on original shading (B) | 0 | A1 run104 / A2 run105: builtin D3DX confirmed loaded (`wine_builtin=1`) and costs +2.1 µs per pass (BeginPass 8.71 vs 6.58 µs); native stays, experiment closed. B run106: no visible shadow; apply path proven correct offline (backlit view, only the own ship cast because casters were chosen by origin); fixed for run 37 |
| 37 | FEX memory-order relaxation (A1), wined3d command stream off (A2), station shadows side-lit with geometry casters (B), comm dialog with the H.264 avatar file and the blit witness (C) | 0 | A1 run107: FEX TSO off no change (within 1 %); A2 run108: CSMT off doubles the draw call (17.6 vs 8.8 µs), +46 % frame; both closed. B run109: first visible shadows (hull from station, ship on station when close); station-on-station missing because the 250-unit box around the camera admits only nearby casters (casters median 8 / max 49 of 93–930 routed, cap never binding); extent/depth/cap/bias options for run 38. C run110: comm dialog froze after a successful H.264 graph build with zero blits (witness), same stage as the MPEG-1 attempt; codec-independent Wine amstream/DirectDraw block; video parked; original file restored |
| 38 | Wide single shadow map at 4096 (A), own-ship near map baseline (A2), residual phases at the busy view (B), hull emitters bracket (C) | 0 | A run111 / A2 run112: shadows popped with camera pitch, wrong direction, own-ship flicker — root cause the sun read from PS c4 regardless of program (42 % of frames no sun ⇒ 250-unit fallback, 57 % a bogus (1,0,0) sun, 789 flips); plus extent-cache thrash, near-plane clipping, half-texel lookup; all fixed on main. B run113: one stamp only, `prepare` 6.36 µs/draw bundled; the technique lookup measured offline at 0.007 ms/frame, trampoline dropped. C run114: 2 of 12 programs fired; 71 % refusals were opaque routed hull draws (never ONE/ONE), F6 shared with the effects gain; own toggle and gain option on main |
| 39 | Cascades with positional sun and caster census (A), 50k far cascade (A2), retained casters live (B), hull emitters at gain 4 (C) | 0 | A run115 only: sun poll on every frame, no cap hit, replay 1.19 µs/draw, shadows 1–3 ms at rest, retention census clean through a gate jump and a load; a camera-following serrated shadow band — root cause the apply quad reconstructing receivers half a pixel off the RT2 sample (C1 over-bias 71.9 % → 1.3 % corrected), fixed on main; stations at 5.6/12 km unshadowed (C3 = 5 km reach). A2/B/C not flown, carried into run 40 |
| 40 | 30 km five-cascade set (A), 2048² maps (A2), corvette with the adaptive ladder + retained casters (B), hull emitters at gain 4 (C) | 0 | A run116 on the previous build (band gone, 30 km reach, distant flicker → run116 fix); A re-flown as run117: period-2 blink 23.8 % → 0.79 %, asteroids clean, lit station faces still flicker = RT2 fp32 z/w receiver precision (13.6 u per ULP at 37 km), fixed on main behind `--sun-shadow-receiver-depth linear` (run 41 A/B); A2 run118: 2048² accepted as default, replay 539 → 386 µs, c4 record cap never fired, far casters admitted to 44.6 km (150,000 is a half-extent); B run119: own radius 449 u, K 1.5 keeps the corvette in C0 with margin, retention clean (no caps, no orphans, 4 retirements, no crash); near flicker on a sun-grazing plane = receiver-plane extrapolation, slope margin fix in review; C run121/run122: Ctrl+Shift+F4 reaches only the two additive guide-light programs; windows are the light-map term inside 100 opaque hull programs (`--hull-lightmap-gain` in implementation). |
| 41 | Receiver depth A/B (A/A2), slope margin A/B (B), hull light-map gain 4 (C), FPS overlay | 0 | A run123 / A2 run124: distant flicker gone with the linear encoding (±1 ULP flips 7–20 % of far-cascade pixels under z/w, 0 under w); shadows ≈ 1.4 ms (toggle) / 0.8–1.7 ms (apply+replay); ≥ 30 ms frames are the engine's > 800-draw view submission (28 of 33 ms), not a proxy phase; B run125/run126: retention clean, K 1.5 confirmed, no near flicker seen; the Terran solar-panel shimmer is unaffected by shadows and was the fade-band route inert under original shading (fixed on main); the 24 fps area is an 18 ms engine pre-render episode; C run128: hull light-map gain accepted at 4 (default), guide lights moved to the effects key and gain. |
| 42 | Pre-render attribution (A), fade-route shimmer check (B), cull census (C), View Distance A/B (D) | 0 | A run129: the 24 fps area is the sector collide routine `0x0045d250` at 26 ms/frame flat (96 % of pre_render; an unguarded all-pairs loop with a full x87 sqrt per pair — box early-out patch in flight); proxy per-routed-draw 11–13 µs (trim pass in flight); B run130: the fade route now routes the first plant leg (16/16), the residual shimmer on the second leg and on distant objects is draws whose engine node never matches (`matched=0 node=0`), fix in flight; light-map gain off does not stop it (run131); C run131: engine census 403 draws / 9.55 ms under 2 px, 458 / 10.85 ms under 4 px, survivors have zero per-node thresholds — `--cull-small-parts` in flight; D run132: View Distance High buys ≈ 1.5 fps (923 → 861 draws), LOD lever closed. |
| 43 | Collide box cull A/B (A), small-parts cull scope A/B (B), shimmer check (C) | 0 | A run133/run134: the box cull installs and rejects 18 % of ~200 P1 pairs per frame but the collide phase stays 22 ms: the cost is the narrow phase on accepted pairs (`0x0048ac80` → BVH×BVH descent), not the pair loop; `--collide-narrow-census` built for run 44; B run135–138: scope `bodies` culls 36 nodes/frame (no gain: nearly every small node has a parent), scope `all` 884 → 477 draws and ~30 → ~42 fps at 2 px with no pop-in seen, 4 px adds little; default now 2 px / `all`; C run139: shimmer persists although every scene draw is routed and matched: jittered sub-pixel geometry leaving (1 − w) ripple in the resolve; `--taa-current-filter` / `--taa-history-weight` built for run 44. |
| 44 | Collide narrow-phase census (A), TAA shimmer A/B (B), new defaults (C); mip-bias A/B | 0 | A run140/run141: one station-vs-ship pair makes 99 % of 2.3e5 RAPID OBB node-pair visits per frame at ~114 ns (= the 25 ms), no contact; SSE2 SAT replacement built (fixture 6–9×); B run142–146: filter 1.0 blurs, W 0.95 alone not enough; shimmer depends on the lattice angle; textures are aniso 16 with full mips (not texture aliasing), flicker is coverage toggling; run148/149: mip bias is not the driver; replay of the real dumps: only the adaptive history weight helps (near-static ×0.31), the drift/crawl case is untouched — fresh design pass in flight; C run147: 478 draws, 19.8 ms (~50 fps) at the busy view, no pop-in. |
| 45 | SSE2 collision box test A/B (A), adaptive TAA history weight (B) | 0 | A run150/151/152: the SSE2 box test takes the collide phase from ~27 to 12.7 ms (116 → 66 ns per node-pair visit, 24 → ~38 fps), 0 triangle tests, collisions still work (ship destroyed by ramming); the census shows 83 % of visits belong to pairs unchanged since the previous frame with no contact (memo_unsafe 0) → `--collide-memo`; a whole-descent SSE2 rewrite measured no gain in the fixture and was dropped. B run153/154: adaptive weight does not fix the lattice crawl and helps the distant station only when the ship is stopped: rejected, stays default-off. | [sampling-profiler.md](../verification/sampling-profiler.md) "Run 45 A", [taa-distant-line-fade.md](../architecture/taa-distant-line-fade.md) |
| 46 | Collision memo (A), lattice line filter (B), far stabiliser (C), frame attribution (D) | 0 | A run155/156: memo verify 808,408 checked, 0 mismatches; memo on ≈ 43 fps (24 before run 45), 62 % of node visits skipped, 70 % of queries still miss → running-minimum relaxation + miss-reason counters, both collision options become defaults. B run157–159: filter engaged, beads ×0.32, but the user sees no change: the visible crawl is the sub-pixel lattice image changing shape as it slides (creep residual 0.40 → 0.26 at best); resolve-side spatial filters are exhausted. C run160/161: both nearly remove the distant shimmer but blur the object under slow camera motion (resampling blur of the 65-frame history under a speed gate that only relaxed at 0.5 px/frame) → gate 0.03–0.25; weight-only picked. D run162: busy view = engine between API calls ≈ 10 ms, state/D3DX apply 9 ms (stamp-inflated), proxy per-draw 3.9, post passes 3.1, native draws 1.3. | [sampling-profiler.md](../verification/sampling-profiler.md) "Run 46 A", [taa-lattice-crawl.md](../architecture/taa-lattice-crawl.md) §10, [taa-distant-line-fade.md](../architecture/taa-distant-line-fade.md) §10, [engine-frame-time.md](../architecture/engine-frame-time.md) "Run 46 D" |
| 47 | Relaxed collision memo (A), lazy RT + profiler (B), far stabiliser gate (C), lattice 64-frame pair (D); recording + run175; Argon Prime fog captures run174 | 0 | A run163/164: verify 0 mismatches; 65 fps standing, 45 moving; transform-b misses dominated, but the fixture rate is not a live per-visit floor; advancement/front tracking remain not built ([moving-case audit](../reverse-engineering/sector-collide.md#1410-moving-case-audit-the-remaining-cost-is-not-yet-attributed-2026-09-20)). B run165–167: lazy RT removes 1,557 SetRenderTarget calls per frame, but the higher native draw time is unexplained rather than a proved driver offset; no FPS gain, stays optional ([Run 47 B ledger](../verification/motion-output.md#run-47-b-2026-09-19--session-b-per-draw-vs-lazy-rt-mode-at-the-busy-station-view)). C far stabiliser 0.985 accepted; remaining distant shimmer is light-map windows. D run172/173 were static; run175 identified edge-on ARM coverage toggling → `--taa-thin-region`. run174’s advertisement-sign veto motivated the sun-lane stamp. |
| 48 | Lattice/distant-window checks (A), fog/shadow flight (B), submit timing (C) | 0 | Completed: run176–178, run180 and run181. Stationary thin-region improvement confirmed; moving crawl remains open. At 0.02 the user estimated about 2 FPS fog cost; no controlled timing measured it. Replacement and sector-reader flight validation remain pending. Submit timings close only the measured candidates for this view. [Archive](../archive/run48-completed-2026-09-20.md). |
| 49 | Consolidated attribution and fog-card replacement | 0 | Completed: A run183/184, B run185. Phase-off busy plateau about 51 FPS; R7 too small for a patch, moving collision remains descent-dominated. Sector reader validated in observed families; uniform fog appearance rejected. [Instructions archive](../archive/run49-50-completed-2026-09-20.md#49-consolidated-attribution-and-fog-card-replacement--ready-for-flight). |
| 50 | Argon Prime media-retry attribution | 0 | Completed: run186. Two failed media-ID-2 constructions account for almost all of their 359/408 ms frames; later retries follow the 30-second cadence. Other first-view stalls remain separately scoped. [Instructions archive](../archive/run49-50-completed-2026-09-20.md#50-argon-prime-first-view-stutters--existing-media-trace), [findings](../verification/media-cues.md#run50-periodic-retries-directly-explain-argon-freezes-2026-09-20). |
| 51 | Longer media retry counter | 0 | Not flown; superseded by Run54 replacement-media verification. [Archived instructions](../archive/run53-completed-2026-09-20.md#51-media-retry-counter--same-view-longer-diagnostic-interval). |
| 52 | Busy-station attribution and lazy-RT counter | 0 | Completed: A run187, B run188, C run189. Matched 478-draw separate-session B/C medians were 19.70 / 18.90 ms; lazy accepted as launcher default, no new engine patch justified. [Instructions archive](../archive/run52-completed-2026-09-20.md), [results](../verification/motion-output.md#run52-lazy-render-target-binding-accepted-as-launcher-default-2026-09-20). |
| 53 | Spatial fog and lattice state | 0 | Completed: A run193, B run194. Fog preference 1.50×; camera-cut native-card flicker and observer reference interference reproduced and corrected. Visual acceptance remains open: fog follow-up is Run55; lattice Session B returned run201 and is under analysis. [Archive](../archive/run53-completed-2026-09-20.md), [lattice findings](../architecture/taa-lattice-crawl.md#27-bound-observer-reference-callbacks-and-device-lifetime-2026-09-20). |
| 54 A | Media and expanded fog/shafts | 3 | Analysed: run195/run196/run197 crashed; Run197 first-person F8 exposes camera tolerance refusal. Session A superseded by Run55; B returned run201 and is under analysis. |
| 54 B | Guarded lattice state observation | 1 | Run201 received; three capture bursts and state packets are under analysis. No repeat flight requested. [Archived instructions](../archive/run54b-completed-2026-09-21.md). |
| 55 | First-person fog correction and crash diagnosis | 1 | Run199: first-person fog fixed by user report; F8 taken in first person. Crash recurred inside LAVVideo; user accepts ID2 omission. Superseded by Run56. [Archived instructions](../archive/run55-completed-2026-09-21.md). |
| 56 | ID2 video omission | 1 | Run200: user confirms no crash or media-related stutter; omission accepted. New fog-range and station-lighting observations remain separate. [Archived instructions](../archive/run56-completed-2026-09-21.md). |
| 57 A/B/C | Station-flash threshold comparison | 3 | Run203/run204 returned with flashes; B disables missing-key cuts but leaves median-motion cuts. C completed as run205; both heuristics default-off accepted. [Archive](../archive/run57ab-completed-2026-09-21.md). |
| 67 A/B/C | SETA smear with strict + band term (A), draw accounting at the stand (B), fog shadow pass and caster footprint 8 (C) | 3 | Completed: run249 / run250 / run251; C second sector: run253. A: band term clears the fast border, a 3-12 px hull share remains, strict stays opt-in ([ledger](../verification/temporal-resolve.md#run-249-strict--band-term-in-flight-2026-09-22)). B: 391 / 416 draws, cullable buckets 1.0-1.3 ms, no next cull ([note](../architecture/engine-frame-time.md#run-250-draw-accounting-at-the-stand-and-the-busy-view-2026-09-22)). C: shadow pass ran without failures, look and cost need an in-session A/B ([ledger](../verification/volumetric-fog.md#run-251-fog-shadow-pass-in-flight-no-ab-2026-09-22)); footprint 8 drops no visible shadow, accepted as default ([ledger](../verification/directional-shadows.md#run-251-minimum-caster-footprint-p8-in-flight-2026-09-22)). [Instructions archive](../archive/user-runs-completed.md#run-67-completed-2026-09-22-run249-run250-run251-run253). |
| 68 A/B/C | SETA exit reset; stand census ladder/body/alpha boxes; fog A/B toggle | 3 | Completed: run254 / run255 / run256. A: SETA smear gone by the numbers (3-12 px share 7 % / 6 % vs 27 % / 34 %), strict + band + exit become defaults; station blur under SETA is history resample softening ([ledger](../verification/temporal-resolve.md#run-254-exit-reset-in-flight-accepted-2026-09-23)). B: 466 draws at the stand, twelve bodies at LOD 0 carry ~390; the pilot needs the pad placement, T_pad 50/100 ([note](../architecture/engine-frame-time.md#run-255-stand-census-with-ladder-body-and-screen-size-2026-09-23)). C: the toggle fired 27 times without a notice by design; no visible or median-cost difference, pass stays off ([ledger](../verification/volumetric-fog.md#run-256-fog-shadow-pass-ab-toggle-in-flight-2026-09-23)). |
| 71 A | Merged-LOD atlas overlay from the LOD 0 meshes at the stand (Run70 DLL) | 1 | Completed 2026-09-23: run269 accepted, no visible transition; coarse ships 2 draws / outpost 4 with the four atlases bound ([note](../architecture/engine-frame-time.md#run-269-lod-0-atlas-overlay-accepted-2026-09-23)). |
| 70 A/B/C | SETA hull blur with --taa-motion-weight (A); fog dust motes with the Ctrl+Alt+F11 A/B (B); merged-LOD atlas overlay at the stand (C) | 3 | Run69 DLL `70abe438…` from `5a11ad00`. A: run262 (0.8) and run263 (0.7) accepted, default 0.7,2,8 ([ledger](../verification/temporal-resolve.md#run-262-motion-weight-08-2-8-in-flight-accepted-2026-09-23)). B: run264 + B2, accepted at 1300,3 / MAX_PX 8, default in the next DLL ([ledger](../verification/volumetric-fog.md#run-264-fog-dust-motes-in-flight-2026-09-23)). C: run265 (specular placeholder) and C2 run268 (specular fixed; remaining step = light-atlas mip bleed on exhausts/windows, fix in progress) ([note](../architecture/engine-frame-time.md#run-268-atlas-overlay-with-the-specular-atlas-2026-09-23)). |
| 69 A/B/C/D | Merged-LOD pilot overlay (addon/05.cat, installed DLL): two-group collapse (A), glow collapse (B), compact placement at 80 / 150 px (C), area-ranked light maps + synthesized material (D) | 4 | A: run257, engine glows lost ([note](../architecture/engine-frame-time.md#run-257-merged-lod-pilot-in-flight-2026-09-23)). B: run258, glows back. C: run259/run260, 80/150 acceptable, lighting loss = light maps and diffuse strength ([note](../architecture/engine-frame-time.md#run-259--260-compact-placement-at-80--150-px-the-lighting-question-2026-09-23)). D: run261, less visible, outpost sun 94 % / non-sun 73 % of fine ([note](../architecture/engine-frame-time.md#run-261-area-ranked-light-maps-and-the-synthesized-material-2026-09-23)). |
| 72 A/B | Run72 defaults (box cull, forward reticle, 1.05 boom, pause key-only, decoder discovery, identity without hash) and the music trace (A), then --music-keep (B) | 2 | A completed 2026-09-23 (run270): reticle, camera, pause and collisions accepted; alt-tab same-id replay confirmed 7/7, save not traced; bolts faint in third person (open, [ledger](../verification/screen-emission.md#run-270-run-72-a-2026-09-23-bolt-visibility-in-third-person-open)); 1080p CPU passes unchanged, proxy 6.8 % ([note](../architecture/engine-frame-time.md#run-270-first-flight-at-19201080-run-72-a-2026-09-23)). B completed (run271): --music-keep keeps the position through alt-tab, pause and save (two saves while docked) but the DirectSound re-arm leaves a ~1 s gap on alt-tab; the trace did not install beside the keep; bolts are 1–2 px dots with the cull off ([ledgers](../verification/music-keep.md), [emission](../verification/screen-emission.md), [fog](../verification/volumetric-fog.md)). DLL `c17792a9…` ([install](../verification/results/run72-candidate-install.json)). |
| 73 B/C | Fog hand-over, --music-keep alt-tab, bolt footprint (B); --gpu-sync-timing stand (C), Run73 DLL | 2 | B completed (run273): music keep accepted (6 alt-tabs, all skip_all); docked view in flight shows our fog; open: same-family gate jump 3.5 s of engine fog (residency drop + ramp), confirmed prefill still re-filled after arrival (1.15 s), docked save load keeps engine fog until undock (cards armed but refused, 4.5 s), new-game cold start 1.34 s (0.7 s ordering + 0.6 s fill latch); bolts unchanged (rule leaves half-length >= 3 px untouched; no additive admission after the new game) ([triage](../verification/results/run273-fog-bolts/), fixes in progress). C completed (run274): per-pass GPU table ([note](../architecture/engine-frame-time.md#run-274-gpu-per-pass-cost-at-19201080-run-73-c-2026-09-23)); proxy 11.7 ms serialised vs engine 4.7 ms, fog_route 4.4 / taa 2.9 ms. |
| 73 A | Merged-LOD batch overlay of the flown sectors (19 bodies) in a busy sector, Run72 DLL | 1 | Completed 2026-09-23 (run272): FPS better, no oddity reported; overlay bodies draw the merged record at 2–4 draws; 234/217 draws per burst come from texel_floor-refused tech stations and the gate (36–38 / 14–32 each), single-LOD pipes (42), refused signs (~35), the spacedock above its switch and the outpost with the camera inside its sphere ([triage](../verification/results/run272-batch-busy/burst_draws_out.txt)); baker per-tile clamp in progress. |
| 74 A | Re-baked overlay: aspect thresholds + area-weighted texel rule, 22 bodies, busy sector, Run73 DLL | 1 | Completed 2026-09-23 (run277, flown on the Run75 DLL): accepted, FPS much better, almost no visible transition; one visible switch on the solar-panel arms of the plasma thrower factory (F8 bursts 35568–35575 coarse, 36781–36788 fine), triage in progress; overlay stays installed. |
| 74 B | Fog shadow pass GPU cost with --gpu-sync-timing, Ctrl+Shift+F11 on/off/on | 2 | Completed 2026-09-23: run275 flew with the pass disabled at launch (toggle inert by design; reproduces the Run 274 route figures); run276 with `--fog-shadow-pass`: the pass costs about 0.3 ms GPU (fog_route 4.79 on vs 4.42–4.53 off), dt unchanged; stays off ([ledger](../verification/fog-shadow-pass.md#run-276-run-74-b-2026-09-23-fog-shadow-pass-gpu-cost-at-19201080)). |

## Run history paragraphs

Moved from `user-runs.md` on 2026-09-22:

Run 64 returned **run228/run229** (A: sun engaged; edge shimmer was the jitter-phase clip step; CORE_F never applied because the launcher strips the shell variable), **run231** (B: widening dimmed the strips and popped under yaw), **run232** (C: fog fine; strip flicker at rest is the baseline TAA residual) and **run233** (sentinel stabiliser off: smear unchanged).

Run 63 returned **run223** (A: shadows fine; sun unchanged because the override never engaged) and **run224** (B: shafts clean and persistent, fog accepted); run225/run227 (thin glow lines) led to the hull emissive widening.

Run 62 returned **run221** (A: distant-station pan flicker fixed) and **run222** (B: fog look accepted as "very good"; station shadow smears in L2 = bin-centre shaft sampling; shaft appears/disappears = shadow caster retention never engaged, transpose inverse of the 16.16 view basis).

Run 61 returned **run215/run216** (A: panel crawl accepted, weight stays 0.97, lasers clean, distant-station pan flicker traced to far-plane history rejection) and **run220** (B: looks accepted, L2 preferred, ribs from axis-aligned coverage waves, sun/flare pop at ~50% occlusion is vanilla).

Run 60 returned **run212** (A) and **run214** (B): forward-flight crawl traced to sentinel-depth panel glass closing the camera gate; approach flash gone with `node`; stored fog accepted in principle but thin and flat, black smear = unlit station shadow shaft, far-station shimmer = unrouted blended draws ([instructions](../archive/user-runs-completed.md)). Run57 C returned **run205**: no flash or transition ghosting reported; both global heuristic cuts are now disabled by default. [Completed instructions](../archive/run57c-completed-2026-09-21.md), [acceptance evidence](../verification/temporal-resolve.md#run57-global-heuristic-cuts-disabled-by-default-2026-09-21).
Moved from `user-runs.md` on 2026-09-22:

Run 65 returned **run235** (A: sun accepted, core dimming liked; SETA smear captured under the delayed burst: cause is the far-plane history tolerance, fixed behind `--taa-sky-history strict`), **run236/run237** (B: widening 3 and 4 accepted, 4 chosen as default with the emissive vote), **run238/run239/run240/run242/run243** (C: fog fine; the ~50 fps stand is 448 engine draws from 58 nodes, 400 at LOD 0; `--cull-small-parts 4` removed ~50 draws; run243's `--lod-scale 0.5` was refused by the Run65 DLL).

Moved from `user-runs.md` on 2026-09-22:

Run 66 returned **run244** (A: strict removes 42 % of the SETA dark sky, the near band 63 %; residual band fixed by the band term; blur is pre-existing history reprojection) and **run245-run248** (B: lod-scale 0.25/0.5 both ~275 draws vs 394 vanilla; the heavy station body reaches LOD 3 at 0.5; user: 0.5 helps with a few pops, default pending; run248 accounting: 367 draws, 19 % tiny, no second pass).
Moved from `user-runs.md` on 2026-09-23:

Run 67 returned **run249** (A: SETA still smears a little, normal speed and pan clean; the residual is a hull share carried outward in the 3-12 px band, a design question), **run250** (B: draw accounting at the stand, no cull justified; the alpha-tested draws need one flight with the extended bounds log), **run251** (C: fog shadow pass "softer?", no A/B yet; footprint 8 without a missing shadow) and **run253** (C, second sector: no shadow popping; footprint 8 accepted as the launcher default).
Moved from `user-runs.md` on 2026-09-23:

Run 68 returned **run254** (A: SETA smear gone by the numbers, 7 % / 6 % against 27 % / 34 %; strict + band + exit reset become defaults; the SETA station blur is resample softening, a motion-weight option is being built), **run255** (B: 466 draws at the stand, the pilot placement corrected to the pad rule, T_pad 50/100) and **run256** (C: the toggle worked without a notice, no visible or median-cost difference, the pass stays off).
Moved from `user-runs.md` on 2026-09-23:

Run 69 D returned **run261** (lighting difference less visible; outpost pair: sun part 94 % of fine, non-sun 73 %, was 77 % / 29 %); the atlas collapse is being built for Run 69 E.

Run 69 C returned **run259** / **run260** (80 / 150 px acceptable; the coarse model is sun-lit but 23 % weaker and loses 72 % of its light-map self-illumination, from the collapse; fixed by the area-ranked rule and the synthesized material).

Run 69 B returned **run258** (engine glows back with the glow collapse; user asks for 80 / 150).

Run 69 A returned **run257** (the pilot draws as designed, 297 vs 447 draws; engine glows lost because the two-group collapse dropped the exhaust materials' light maps; fixed by the glow collapse, reinstalled).

## Run 66 (completed 2026-09-22: run244 (SETA/pan, strict), run245-run248 (lod-scale A/B at the stand))

**Run 66 (queued 2026-09-22): Run66 DLL `1f9a85f5…` from `387d5cd9`. Two sessions; both dry-runs verified. Launcher defaults since Run 65: widening 4 and the emissive vote apply by themselves on HDR sessions, core dimming is the default with `--sun-occlusion`, the fog has one look (no look flag), `--capture-delay 300` stays in both commands.**

Session A, SETA smear and sun (fog off), with `--taa-sky-history strict`. (1) The black smear: approach a station that sits off-centre under SETA (press F8, re-engage SETA, let the delayed burst fire while the station slides across the screen): the dark trail on the nebula should be gone. (2) The review's one risk with strict: a slow pan past a station against bright nebula, camera otherwise still: watch the two-pixel ring of sky just outside the silhouette for any flicker or outline; F8 during the pan. (3) Sun: nothing should have changed from Run 65 (core dimming is now the default). If both (1) and (2) are clean, strict becomes the default.

```sh
env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 X3M_MOTION_FRAME_LOG=1 /Users/asvetl/x3-mod/x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --light-map-far-fade 80,220 --motion-rt-mode lazy --frame-end-stride 1 --taa-thin-region 0.97 --sun-occlusion --sun-occlusion-log --taa-debug --capture-start 999999 --capture-frames 32 --capture-delay 300 --taa-sky-history strict
```

Session B, the frame-time stand (fog on, single look, widening off for the fog comparison). Same stand as run239/run240. Three readings of the fps overlay and one delayed F8 each: (a) as given below with `--lod-scale 0.25`; (b) the same with `--lod-scale 0.5`; (c) the same without `--lod-scale` (vanilla ladder) but keeping `--cull-small-parts 4`. Report fps and, if you can, how the distant stations look at 0.25 (they should drop to their coarse meshes much sooner; any visible pop or missing detail matters). The draw count per frame is what decides between fixing the LOD thresholds and generating coarse LOD bodies (docs/architecture/merged-lod-feasibility.md).

```sh
env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 X3M_MOTION_FRAME_LOG=1 /Users/asvetl/x3-mod/x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --light-map-far-fade 80,220 --motion-rt-mode lazy --frame-end-stride 1 --taa-thin-region 0.97 --volumetric-fog 0.02 --volumetric-fog-cards replace --volumetric-fog-range stored --volumetric-fog-timing --capture-start 999999 --capture-frames 8 --capture-delay 300 --lod-scale 0.25 --cull-small-parts 4 --frame-timing --frame-phases --fps-overlay
```

## Run 67 (completed 2026-09-22: run249, run250, run251, run253)

**Run 67 (queued 2026-09-22): Run67 DLL `621cad63…` from `1b4d745c`. Three sessions; all dry-runs verified. `--capture-delay 300` stays in every command.**

Session A, SETA smear and pan, strict sky history with the new band term (default 3 px). (1) Same SETA approach at an off-centre station: the trailing band beside the silhouette should now be gone as well; if a faint one remains, try `--taa-sky-history-band-px 2`. (2) A slow pan past a static station against bright nebula: the sky border must stay smooth (no crawl, no flicker). (3) New thing to watch: a close flyby or a docking approach at normal speed, where a nearby station slides across the sky: its one-pixel sky border is now current-only under strict; say whether you notice any aliasing there. Delayed F8 on the SETA approach and on the flyby. If all three are clean, strict becomes the default.

```sh
env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 X3M_MOTION_FRAME_LOG=1 /Users/asvetl/x3-mod/x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --light-map-far-fade 80,220 --motion-rt-mode lazy --frame-end-stride 1 --taa-thin-region 0.97 --sun-occlusion --sun-occlusion-log --taa-debug --capture-start 999999 --capture-frames 32 --capture-delay 300 --taa-sky-history strict
```

Session B, the frame-time stand with the draw accounting. Same stand as run248, `--lod-scale 0.5` plus `--object-bounds-log`: one delayed F8 at the stand, one at the busy view. The log then carries every routed draw's screen box, and `python3 tools/analysis/draw_accounting.py /tmp/x3-bottleX3-run<N>` buckets the 367 draws into off-screen, occluded, tiny and visible. Nothing to judge visually.

```sh
env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 X3M_MOTION_FRAME_LOG=1 /Users/asvetl/x3-mod/x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --light-map-far-fade 80,220 --motion-rt-mode lazy --frame-end-stride 1 --taa-thin-region 0.97 --volumetric-fog 0.02 --volumetric-fog-cards replace --volumetric-fog-range stored --volumetric-fog-timing --capture-start 999999 --capture-frames 8 --capture-delay 300 --cull-small-parts 4 --frame-timing --frame-phases --fps-overlay --lod-scale 0.5 --object-bounds-log
```

Session C, fog shadow pass and cascade footprint A/B (fog on, single look, vanilla LOD). At a shaft-rich spot (a station between you and the sun in fog): shafts should have soft edges instead of hard beams, no seam at the cascade boundaries, sharper shafts close to the ship; watch for any banding at a fixed distance and note the fps overlay against the same spot with `--fog-shadow-pass off`. The cascade footprint at 8 drops casters smaller than about 110 m from the fourth cascade and 560 m from the farthest: any shadow that disappears at distance is worth a note. Delayed F8 at the shaft spot.

```sh
env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 X3M_MOTION_FRAME_LOG=1 /Users/asvetl/x3-mod/x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --light-map-far-fade 80,220 --motion-rt-mode lazy --frame-end-stride 1 --taa-thin-region 0.97 --volumetric-fog 0.02 --volumetric-fog-cards replace --volumetric-fog-range stored --volumetric-fog-timing --capture-start 999999 --capture-frames 8 --capture-delay 300 --cull-small-parts 4 --frame-timing --frame-phases --fps-overlay --fog-shadow-pass on --shadow-cascade-min-footprint 8
```

## Run 70 (completed 2026-09-23: run262, run263, run264 + B2, run265, run268)

**Run 70 (queued 2026-09-23): Run69 DLL `70abe438…` from `5a11ad00` ([qualification](../verification/results/run69-candidate-qualification.json), [install](../verification/results/run69-candidate-install.json)). Changes against Run68: strict sky history + exit reset 0.25 are the defaults; four dead TAA variants removed; `--taa-motion-weight` (default off); `--fog-dust-motes` (default off). All three dry-runs verified. `--capture-delay 300` stays in every command.**

Session A, the SETA station blur: the Run 68 A command plus `--taa-motion-weight 0.8,2,8`. Pixels of a hull moving more than 2 px/frame on screen (and against the camera path) keep less history, down to 0.8 at 8 px/frame, so a station under SETA should stay sharper; pans, your own ship and static hulls are untouched by design. Judge: (1) is a station approached under SETA sharper than in Run 68 A, and does it shimmer or crawl more while it moves fast (the trade the option makes); (2) normal-speed flyby and a pan unchanged; (3) your own ship's hull in chase view during a fast turn unchanged. Delayed F8 under SETA on the approach (twice) and at normal speed. If the blur is better and the shimmer acceptable, the option becomes a default; if the shimmer is too much, the next A/B is `0.7,2,8` or a higher V1.

```sh
env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 X3M_MOTION_FRAME_LOG=1 /Users/asvetl/x3-mod/x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --light-map-far-fade 80,220 --motion-rt-mode lazy --frame-end-stride 1 --taa-thin-region 0.97 --sun-occlusion --sun-occlusion-log --taa-debug --capture-start 999999 --capture-frames 32 --capture-delay 300 --taa-sky-history strict --taa-sky-history-exit-px 0.25 --taa-motion-weight 0.8,2,8
```

Session B returned **run264** and B2 (2026-09-23): no fps cost, ~110 discs of 6–8 px at fog brightness (too many, "snow"); B2 at `1300,2` with `MAX_PX 8`: "good setting, I like it", settled on size 3 → **motes are on by default at `1300,3` / MAX_PX 8 in the next DLL** (`--fog-dust-motes 0` opts out). No further B session; the command below is kept for reference:

```sh
env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 X3M_FOG_MOTES_MAX_PX=8 X3M_MOTION_FRAME_LOG=1 /Users/asvetl/x3-mod/x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --light-map-far-fade 80,220 --motion-rt-mode lazy --frame-end-stride 1 --taa-thin-region 0.97 --volumetric-fog 0.02 --volumetric-fog-cards replace --volumetric-fog-range stored --volumetric-fog-timing --capture-start 999999 --capture-frames 8 --capture-delay 300 --cull-small-parts 4 --frame-timing --frame-phases --fps-overlay --fog-dust-motes 1300,3
```

Session C returned **run265** (2026-09-23): the atlas overlay was in effect (one atlas draw per ship, two for the outpost, the 2048²/1024² atlases bound with full mip chains); the outpost pair matches the fine model in sun part (100 %) and non-sun part (94 %), mean brightness within 1 %; what still shows at the switch is the shine: with the specular slot NULL the engine binds a 32×32 placeholder that is brighter than the real specular maps, so coarse highlights are 22–27 % brighter and fine-scale contrast about 1.4× ([note](../architecture/engine-frame-time.md#run-265-atlas-overlay-in-flight-2026-09-23)). **Session C2 returned run268:** specular fixed, the remaining step is the lit windows/exhausts (×1.5, light-atlas mip bleed, inferred); the next overlay build uses the LOD 0 mesh and tile-aware atlas mips; it will be queued as C3 when installed. Command for reference:

```sh
env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 X3M_MOTION_FRAME_LOG=1 /Users/asvetl/x3-mod/x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --light-map-far-fade 80,220 --motion-rt-mode lazy --frame-end-stride 1 --taa-thin-region 0.97 --volumetric-fog 0.02 --volumetric-fog-cards replace --volumetric-fog-range stored --volumetric-fog-timing --capture-start 999999 --capture-frames 8 --capture-delay 300 --cull-small-parts 4 --frame-timing --frame-phases --fps-overlay --object-bounds-log --cull-census
```

## Run 71 (completed 2026-09-23: run269)

**Run 71 (queued 2026-09-23): Run70 DLL `a773e9f2…` from `0bfa11ac` ([qualification](../verification/results/run70-candidate-qualification.json), [install](../verification/results/run70-candidate-install.json)). Defaults now: motion weight 0.7,2,8 and dust motes 1300,3 (MAX_PX 8); every launch carries them without flags.**

Session A, the merged-LOD atlas overlay built from the LOD 0 meshes: **installed** (`05.cat` `d29b0c88…` / `05.dat` `5fd877c1…`, 19.5 MB, [install record](../verification/results/lod-overlay-pilot/install.json)); start the game fresh. Each ship's coarse record is now its full LOD 0 mesh as one atlas draw plus one alpha-tested group (2 draws instead of 31–38); the outpost its full mesh as three atlas groups plus the lattice (4 draws instead of 34); the collision mesh is vanilla's; atlas mips are tile-aware. So only the texture resampling changes at the switch: judge whether the switch at 80 / 150 px is visible at all now, and if so what changes (lit windows/exhausts brightness, plating, silhouette should be identical). Same stand command as Run 70 C (motes will be on by default; add `--fog-dust-motes 0` only if they get in the way of the judgement). Paired captures across the switch on the outpost (below 150 px this time) and on the Titan at a held camera.

```sh
env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 X3M_MOTION_FRAME_LOG=1 /Users/asvetl/x3-mod/x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --light-map-far-fade 80,220 --motion-rt-mode lazy --frame-end-stride 1 --taa-thin-region 0.97 --volumetric-fog 0.02 --volumetric-fog-cards replace --volumetric-fog-range stored --volumetric-fog-timing --capture-start 999999 --capture-frames 8 --capture-delay 300 --cull-small-parts 4 --frame-timing --frame-phases --fps-overlay --object-bounds-log --cull-census
```

To remove the pilot: delete `addon/05.cat`, `05.dat` and `05.x3m-lod.json`; the originals are untouched.


## Runs 73, 73 B/C, 74 A, 75 A/B/C, 76 A (completed 2026-09-23/24; run272-run281)

Instructions as issued (provenance only). All used the Run 73 A stand command kept in user-runs.md.

- Run 73 A: batch overlay of the flown sectors (19 bodies) in a busy sector; two F8 bursts. Result: run272.
- Run 73 B: fog hand-over transits + docked view, --music-keep alt-tab x2, bolts third then first person with an F8 burst; C: --gpu-sync-timing stand. Results: run273, run274.
- Run 74 A: overlay re-baked with aspect thresholds + texel rule (22 bodies); factories and the gate near their switch, flat stations, capital ships; two F8 bursts. Result: run277. Run 74 B: fog shadow pass A/B under --gpu-sync-timing (run275 pass off at launch, run276 on).
- Run 75 A: two gate jumps incl. same family, a docked save load, a new game in fog; B: bolts 3,12 third/first person with a burst; C: timing split still/moving in fogged and busy sectors. Results: run278, run279, run280.
- Run 76 A: overlay re-baked with the light-atlas bleed guard; factory panels across the switch; ships at their switch. Result: run281 (with the share-gate re-bake).

## Run 77 (completed 2026-09-24: see table)

**Run 77 A2 (queued 2026-09-24 03:40): one F8 burst next to a slot-06 body.** Run 77 A (run287) confirmed slot 05 (14 bodies drew their merged record, frame time better than Run 74 A) but no slot-06 body was on screen during its burst, so the second archive is still unproven. Slot 06 holds the Terran docks and shipyards, the five race trading stations, the equipment docks and the torus docks. In any session, take one F8 burst with a Terran dock or shipyard, a trading station or an equipment dock in view at a distance (2 km or more), and one closer; say which station. Run 78 A run297 (USC orbital supply station, Terran Solar Power Plant XL) shows every slot-06 body staying at lod 0 far below the switch size while slot-05 bodies switch: the two-slot layout is **not** proven; cause under triage-deep ([triage](../../verification/results/run295-298-run78a/summary.md)). The overlay was rebaked with the refusal-class lift and reinstalled 2026-09-24 04:57 (611 bodies: 05 488, 06 123; [record](../../verification/results/lod-overlay-batch/install-fleet2/install.json)), unflown.

**Run 77 C2 completed 2026-09-24 (run291 scale 4, run292 scale 4 + SHADOW_JITTER=0, run293 scale 2 + SHADOW_JITTER=0; no F8 bursts taken): the user finds the rings move with the auto exposure and are faintly present at scale 2 too, and accepts scale 4 (now the default on main); triage on the run289/290 captures: the rings are 8-bit output contours (no dither at the tonemap write; contours 22 px apart at scale 4, moved 4-6 px per 0.01 EV by the exposure), fix = output dither in the next candidate ([triage](../../verification/results/run291-293-rings/)). run294 (scale 4, far bins 24, no timing): dt p50 19.3 vs 19.7 ms, p95 20.4 vs 22.4 against run291; 40 stays the default ([triage](../../verification/results/run294-far-bins-scale4/)). Open: Run 77 A2 (one burst at a slot-06 body) and Run 77 D (deferred): bolts in a busy fight, third and first person, one F8 burst mid-fight. Run 78 A (dither A/B, default scale 4, exit without a crash, the slot-06 burst, `--taa-history-taps 16` vs 5) is queued with the Run78 candidate.

## Run 78 (completed 2026-09-24: see table)

**Run 78 A (queued 2026-09-24 06:20; Run78 DLL `d4ba9f05…` from ee3bbf88 installed 06:58): output dither, S3 5-tap history, the exit fix, scale 4 default, fleet 611.** One session at 5120x1440, several launches, all with the stand command below (Run 77 C2's line without `--fog-march-scale`: 4 is the default now). Please report per launch:

1. **Dither A/B at the fogged spot where the rings were (run291).** Launch 1 with the command as is (dither on); launch 2 adds `--hdr-dither off`. Question: are the moving rings gone with the dither on and back with it off? One F8 burst in each.
2. **Normal exit.** Exit the game through the menu at the end of a launch: expect no "Unhandled page fault" dialog; the log should carry one `engine_memory_read_refused` row (the fix c45c5dc0, unflown).
3. **Slot-06 burst (Run 77 A2).** In any launch, one F8 burst with a Terran dock or shipyard, a trading station or an equipment dock in view at 2 km or more, and one closer; say which station.
4. **History taps A/B on the lattice stand.** Launch 3 adds `--taa-history-taps 16` (the pre-S3 reconstruction); compare against the default 5 taps of launch 1 on the lattice stand: crawl, ghosting, sharpness of thin struts under a slow pan. Say whether you see any difference.
5. Optional: `--fog-far-bins 24 --gpu-sync-timing` on the fog spot (run294 gave -0.4 ms p50 / -2 ms p95 without timing).

Run 77 D (bolts in a busy fight) closed by Run 78 A run298: bolts ok.

```sh
env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 X3M_MOTION_FRAME_LOG=1 /Users/asvetl/x3-mod/x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --light-map-far-fade 80,220 --motion-rt-mode lazy --frame-end-stride 1 --taa-thin-region 0.97 --volumetric-fog 0.02 --volumetric-fog-cards replace --volumetric-fog-range stored --volumetric-fog-timing --capture-start 999999 --capture-frames 8 --capture-delay 300 --cull-small-parts 4 --frame-timing --frame-phases --object-bounds-log --cull-census
```


## Run 80 A — Completed 2026-09-24 (run304-308)

**Run 80 A (queued 2026-09-24 evening; Run80 DLL `593112dc…` from 9cf5decf installed 19:49; fleet overlay install-fleet3 installed 20:16: the coarse Terran records carry their red plates).** One session at
5120x1440, several launches, the stand command below unchanged (`--taa-region-hold` no longer exists; the launcher
refuses it). Please report per launch:

1. **Baseline** (launch 1, defaults): the A'-only build on the lattice stand and under a slow pan: same look as Run 79 A's
   hold-on launch; nothing new expected. One F8 burst at the stand.
2. **Occlusion patch** (launch 2, add `--lod-occlusion all`): the Terran Orbital Defence Station at the run297 "a little
   far" distance (coarse record): expect the ambient-occlusion shading back on the coarse record (darker panels, closer to
   the fine model), the log row `lod_occlusion site=004c34f7 status=patched write=atomic`; watch for a hitch or stutter the
   first time a distant station comes into view (the occlusion map loads at draw time), and look at any distant vanilla
   station for misplaced occlusion (their UV2 is about 1 % off). One F8 burst on the coarse ODS.
3. **LOD switch log** (same launch, add `--lod-switch-log`): near the ODS, turn the ship slowly with the station in the
   right third of the screen and say whether the part flicker is a single pop per crossing or continuous. The
   `lod_switch` rows settle whether the flicker is the record 0 <-> coarse pop.
4. **Thin vote** (launch 3, add `--taa-thin-vote on`; needs the lane, already in the command): the lattice stand at rest
   and under a slow pan, then a large near station: any change in crawl on hull-backed arms, any ghosting on voted
   panels during pans, loading time on sector entry; `--gpu-sync-timing` optional for the `taa_mask` row. One F8 burst.
5. **Fade owner** (launch 4, add `--fade-rt2-owner on`): the run214 stand (two distant stations in the fade band), slow
   vertical pan 3-9 px/frame: flicker on the stations under the pan, trail behind the silhouette, a pop at the band
   edges on approach. One F8 burst at rest and one during the pan.
6. If bolts are fired anywhere: nothing to do; the `bolt_footprint_refused` rows now carry the sub-clause.
7. **Rebaked overlay**: the coarse ODS should now show its red plates (compare with screenshots/terran-lod1.png); say if any body looks wrong (missing texture, black or wrong-coloured panel).
8. Exit through the menu.


Stand command: Run 73 A's (with `--taa-far-stabiliser 0.985 --taa-thin-region 0.97`, defaults since Run 81).

## Run 81 A — Completed 2026-09-24 (run309-311)

**Run 81 A (queued 2026-09-24 22:15; Run81 DLL `b4e945ed…` from 9e1645be installed 22:15; fleet overlay install-fleet3 unchanged).**
New defaults in this build: `--taa-thin-vote on`, `--fade-rt2-owner on`, `--lod-occlusion all`, `--fov 58.7155` (58.7 deg vertical
= 90 / 106 / 127 deg horizontal at 16:9 / 21:9 / 32:9; the game's 73.7 deg vertical is `--fov game`), and under `--taa` the age
programs `--taa-far-stabiliser 0.985` and `--taa-thin-region 0.97` (dropped from the stand command below; they are on). New
opt-ins: `--taa-box-resolution half` (S4), `--shadow-alpha-casters on`. One session at 5120x1440, three launches; each command is
the stand command below plus the flags shown. Please report per launch:

1. **Defaults, FOV and alpha casters** (launch 1: stand command + `--gpu-sync-timing --shadow-alpha-casters on`). In order:
   a. FOV: the lattice stand and a station near the screen edge (the old edge stretch should be gone; the picture is zoomed in
      against Run 80 A, 127 deg across the width instead of 139); target brackets, the lead reticle, mouse aim and cockpit zoom must
      still sit on their objects. Then open the in-game FOV menu, move the slider to 90 (the vanilla look) and back to its minimum
      (about 74, close to the new default) to compare; say which you prefer and whether the menu value survives a save/reload.
   b. TAA defaults: the lattice stand at rest and under a slow pan (thin vote and fade owner now on), two distant stations in the
      fade band under a slow vertical pan.
   c. ODS: the Terran Orbital Defence Station close (fine record) and at the run297 "a little far" distance (coarse record): the
      underside strip below the long spine should now be shadowed at both records (the transition you marked in Run 80 A gone);
      any shadow acne or missing shadow elsewhere; the coarse record's occlusion and red plates (the small brightness change is
      accepted).
   F8 bursts: one at the lattice stand, one on the close ODS, one on the coarse ODS. Rows to confirm: `fov ... status=patched
   value=0x3470`, `fov_confirm ... match=1`, `cull_small_parts_value ... focus=`, `taa_thin_vote_configured ... default=1`,
   `fade_rt2_owner_configured ... default=1`, `lod_occlusion ... setting=all default=1`, `shadow_alpha_casters` per frame
   (`refused_pool` must stay 0).
2. **S4 half-resolution box** (launch 2: stand command + `--gpu-sync-timing --taa-box-resolution half`): the lattice stand at rest and
   in a pan, a laser or engine trail over sky (a halo up to 2 px is expected), a silhouette against sky under a pan, the run221
   distant-station pan; any difference from launch 1. One F8 burst at the stand. The `taa_box` row against launch 1 decides the cost.
3. **Sentinel stabiliser off** (launch 3: stand command + `--taa-sentinel-stabiliser 0`; an env value would be overwritten by the launcher's derived 0.7;
   owner, vote and the thin-region weight stay on by default): the run214 stand (two distant stations in the fade band), slow
   vertical pan 3-9 px/frame: flicker on the stations, trail behind the silhouette, pop at the band edges on approach; then the
   lattice stand at rest and under a pan. One F8 burst at rest and one during the pan on the run214 stand. Decides whether the
   sentinel stabiliser is removed now that the owner gives fade-band stations their depth.
4. Exit through the menu after each launch.


Stand command: Run 73 A's minus the two age-program flags (defaults since Run 81).

## Run 82 (completed 2026-09-25: see table)

**Run 82 A (queued 2026-09-25 00:26; Run82 DLL `cd8ef8e4…` from d1a4e360 installed 00:26; fleet overlay install-fleet3 unchanged).**
New in this build: FOV remap (`--fov N` in game units, default 90 = "90 horizontal on 16:9", the in-game menu 70..100 now works in
the same units; `--fov game` = vanilla), sun lens-flare overflow fix (`--sun-flare-fix`, on), chase camera FOV compensation
(`--chase-fov-compensate`, on), S4 half box as the default, the small-parts cull reading the scene projection, fade ownership of
alpha-tested station cutouts (with the owner, on), and the opt-in `--taa-thin-region-source both|screen|vote`. Three launches at
5120x1440; each command is the stand command below plus the flags shown. Please report per launch, and name the sector of each stand:

1. **Defaults, FOV, sun, chase** (launch 1: stand command + `--gpu-sync-timing`). In order:
   a. Chase view: the fighter should sit at its Run 80 size again (the boom is 1.33x longer); then open the in-game FOV menu: it starts
      at 90 (= today's look); step to 80 and 100 and back to 90: each step must change the view smoothly and 90 must return the same
      picture; the ship keeps its size at every setting.
   b. Sun: at the menu's 100, look straight at the sun and around it: the disc and flare must stay visible near the view centre (Run 81
      A launch 1 lost it there); then back to 90 and the same check.
   c. TAA: the lattice stand at rest and under a slow pan (the S4 half box is now the default; any halo on a laser or engine trail over
      sky beyond 2 px); the two solar plants in the fog band (run311's stand) under a slow vertical pan with the stabiliser at its
      default 0.7: no change expected.
   F8 bursts: one at the lattice stand, one on the fog-band plants at rest. Rows: `fov ... status=patched`, `fov_confirm ... match=1`,
   `sun_flare_fix site=0047e391 status=patched`, `chase_fov_compensate factor=1.333`, `motion_output_taa_box_resolution ... default=1`,
   `cull_small_parts_value ... source=scene`, `fade_route_frame ... fade_tested=` > 0 on the plants, `fade_owner_masked=0`.
2. **Stabiliser off, retry** (launch 2: stand command + `--taa-sentinel-stabiliser 0`): the same two solar plants in the fog band, slow
   vertical pan 3-9 px/frame, then at rest: the Run 81 A launch 3 shimmer on the panels and trusses should be gone now that the
   cutouts are owned; then the lattice. One F8 burst at rest and one during the pan on the plants. Decides the stabiliser's removal.
3. **Thin-region source** (launch 3: stand command + `--gpu-sync-timing --taa-thin-region-source vote`): the lattice stand at rest and
   under a pan, hull-backed arms and thin masts: any crawl or shimmer that launch 1 did not have. One F8 burst at the stand. The
   `taa_mask_tests` row against launch 1 (about 0.1 ms less expected) and the look decide whether the screen-space test can go later.
4. Exit through the menu after each launch.

Stand command (Run 73 A's minus the two age-program flags, which are defaults now):

```sh
env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 X3M_MOTION_FRAME_LOG=1 /Users/asvetl/x3-mod/x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --light-map-far-fade 80,220 --motion-rt-mode lazy --frame-end-stride 1 --volumetric-fog 0.02 --volumetric-fog-cards replace --volumetric-fog-range stored --volumetric-fog-timing --capture-start 999999 --capture-frames 8 --capture-delay 300 --cull-small-parts 4 --frame-timing --frame-phases --object-bounds-log --cull-census
```

To remove the overlay: delete `addon/05.cat`, `05.dat`, `05.x3m-lod.json` and `x3m-lod-batch*.json/txt`; the originals are untouched.

Completed run commands and instructions are preserved in
[the completed-run archive](../archive/user-runs-completed.md) and the
[run 48 archive](../archive/run48-completed-2026-09-20.md); they are provenance,
not rerun requests.


<a id="51-media-retry-counter--same-view-longer-diagnostic-interval"></a>
<a id="53-spatial-fog-and-moving-lattice-state--ready-for-flight"></a>
Run51/53 instructions are [archived](../archive/run53-completed-2026-09-20.md); they are not rerun requests.

No run is queued (Run 83 A follows the Run 83 candidate). Completed instructions for Runs 73-82 are in the [archive](../archive/user-runs-completed.md).
