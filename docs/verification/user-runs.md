# Outstanding user gameplay runs

Updated 2026-09-13. Run 17 crypto acceptance and the first-person/chase
left-centre-right diagnostic are complete and are not in this queue. The agent
never launches the game. Installed build: chase firing fix with 13° pitch and
0.9 distance, softer 0.28/0.38 s follow, predictive lead marker, opt-in FP16 bloom
116 reviewed linear material pairs, the central chase display correction, and
fixed exposure plus same-run exposure/bloom controls;
[build record](../../verification/results/linear-material-install.json).
From the repository root, paste a `./x3run` command below. The executable
[launcher script](../../x3run) handles the shared lock and log snapshots; no shell
function setup is needed. Runs 1, 3, 5, 6 and 7 are complete; reader/DAT/adjacency
fast co-activation passed as run 19. No enhanced gameplay run is currently ready.
Close X3 between runs and report completed numbers. After exit, the helper prints
a fresh `/tmp/x3-bottleX3-run<N>/` path containing that session’s log and referenced
captures, so later A/B runs cannot overwrite them. Vanilla/dry-run creates no snapshot.

| Run | Purpose | Sessions | Status |
| --- | --- | ---: | --- |
| 1 | Chase aiming/framing + reader/adjacency verification | 0 | Accepted as run 18 |
| 2 | Sharpen/shimmer + camera cuts with TAA | 0 | Merged into run 6 |
| 3 | Automatic exposure + bloom off/on | 0 | Completed: A run 24; B run 25 exposed bloom initialization failure |
| 4 | Vanilla double-cursor/menu-bar comparison | 1 | After any enhanced run |
| 5 | Reader/adjacency fast modes | 0 | Accepted as run 19 |
| 6 | Linear hull materials off/on plus sharpen/cuts at fixed exposure | 0 | Completed: A run 20, B runs 21–23; analysis/quality follow-ups remain |
| 7 | Fixed/automatic exposure and bloom toggles, central chase HUD and selection timing | 0 | Completed as run 26; glow and selection-stutter follow-ups pending |

Wait for the corrected combined build before another enhanced run. Run 4 remains
the optional vanilla cursor comparison. The next enhanced session will combine
restored emitter glow, exposure comparison and broader selection-stutter timing;
its command will be added after build qualification and installation. The shared TAA shader
now fits the standard instruction budget and passes exact fixture comparisons;
run 6 also covers that installed update. Emission integration is installed but stays off in these comparisons; it adds
no gameplay request yet while its performance optimization is under investigation.



## 1. Camera correction plus loading verification — Completed as run 18

Completed: aiming works, the 13° angle is approved, and no trembling was noticed.
The [saved loading verification](run18-camera-loading.md) passed; do not repeat this run.
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
the earlier severe overexposure. [Analysis](run24-exposure-baseline.md) confirms
near-permanent +2 EV; the [reviewed policy](../architecture/space-exposure-policy.md)
selects fixed EV 0 for the next default, with Auto retained for comparison.
B is complete as run 25, with all 174 referenced artifacts preserved. The user
noticed no visual bloom difference; [analysis](run25-bloom-comparison.md) shows
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

## 4. Vanilla window/cursor comparison — Ready after any enhanced run

```sh
./x3run --direct --vanilla
```

Alt-tab out and back once. Report whether both the macOS arrow and game cursor
appear, whether their positions differ, and whether the macOS menu bar overlaps
the game. Compare the same screen as the enhanced run; load the save if the
problem only appears during gameplay. No F8 capture is needed.

## 5. Reader and adjacency fast modes — Completed as run 19

[Run 19](run19-fast-loading.md) confirms all intended fast routes with no faults:
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

Completed as [run 26](run26-comparison.md). Bloom executes but fails visual
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
