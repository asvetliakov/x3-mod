# Outstanding user gameplay runs

Updated 2026-09-13. Run 17 crypto acceptance and the first-person/chase
left-centre-right diagnostic are complete and are not in this queue. The agent
never launches the game. Installed build: chase firing fix with 13° pitch and
0.85 distance, opt-in FP16 bloom and reviewed DEFAULT/BUMPMAP linear materials;
[build record](../../verification/results/linear-material-install.json).
From the repository root, paste a `./x3run` command below. The executable
[launcher script](../../x3run) handles the shared lock and log snapshots; no shell
function setup is needed. Run 1 is complete and its loading verification passed; run 5 is now ready. Complete the remaining
ready comparisons over several sessions as convenient.
Close X3 between runs and report completed numbers. After exit, the helper prints
a fresh `/tmp/x3-bottleX3-run<N>/` path containing that session’s log and referenced
captures, so later A/B runs cannot overwrite them. Vanilla/dry-run creates no snapshot.

| Run | Purpose | Sessions | Status |
| --- | --- | ---: | --- |
| 1 | Chase aiming/framing + reader/adjacency verification | 0 | Accepted as run 18 |
| 2 | Sharpen/shimmer + camera cuts with TAA | 0 | Merged into run 6 |
| 3 | Automatic exposure + bloom off/on | 2 | Ready |
| 4 | Vanilla double-cursor/menu-bar comparison | 1 | After any enhanced run |
| 5 | Reader/adjacency fast modes | 1 | Ready; run 18 verification accepted |
| 6 | Linear hull materials off/on plus sharpen/cuts at fixed exposure | 2 | Ready |

Six sessions remain; complete them at your convenience. The shared TAA shader
now fits the standard instruction budget and passes exact fixture comparisons;
run 6 also covers that installed update. Emission integration is still agent
work and adds no gameplay request yet.



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

## 3. Space-aware exposure and bloom comparison — Ready

Keep sharpen and mip bias at zero so this remains comparable to the earlier HDR
baseline:

```sh
./x3run --direct --ownership --object-trace --object-lifetime --motion-output --taa \
  --telemetry --taa-debug --taa-sharpen 0 --taa-mip-bias 0 \
  --hdr --hdr-tonemap --capture-start 999999 --capture-frames 4
```

Keep the game's **Glow enabled**. In this baseline, capture settled dark space,
a bright object/emitter, and a turn between them. Do not set manual EV. Exit,
then repeat the same save and camera positions with bloom enabled:

```sh
./x3run --direct --ownership --object-trace --object-lifetime --motion-output --taa \
  --telemetry --taa-debug --taa-sharpen 0 --taa-mip-bias 0 \
  --hdr --hdr-tonemap --hdr-bloom --capture-start 999999 --capture-frames 4
```

Repeat the F8 samples. Report exposure pumping/adaptation, bright and dark
detail, glow around emitters, HUD/menu readability and any obvious slowdown.
After the comparison, change resolution once if practical. F8 records the
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

## 5. Reader and adjacency fast modes — Ready

Run 18 verified 6,958 reader outputs and 15,354 meshes exactly, with zero
mismatches or faults. Both fast modes may now share one functional load:

```sh
./x3run --direct --telemetry --resource-read fast --dat-handles \
  --mesh-adjacency fast
```

The combined run can establish fault-free co-activation. Any claim about which
feature changed loading time still requires isolated, same-save comparisons.


## 6. First linear materials plus sharpen and TAA cuts — Ready; after camera acceptance is convenient

This is a fixed-exposure comparison with bloom off. Both sides use sharpen 0.75
and mip bias -0.5 so the material toggle remains the only A/B difference. Use
the usual ship/save with a visible hull or station, emissive panels and, if
available, an active light.

First run with linear materials off:

```sh
./x3run --direct --camera chase --ownership --object-trace --object-lifetime \
  --motion-output --taa --telemetry --taa-debug --camera-log 1 \
  --taa-sharpen 0.75 --taa-mip-bias -0.5 \
  --hdr --hdr-tonemap --hdr-ev-manual 0 \
  --capture-start 999999 --capture-frames 4
```

Exit, then repeat the same save, camera positions and sequence with linear
materials on. Leave the three material gains at their default 1:

```sh
./x3run --direct --camera chase --ownership --object-trace --object-lifetime \
  --motion-output --taa --telemetry --taa-debug --camera-log 1 \
  --taa-sharpen 0.75 --taa-mip-bias -0.5 \
  --hdr --hdr-tonemap --hdr-ev-manual 0 --linear-materials \
  --capture-start 999999 --capture-frames 4
```

In each session, take separate F8 four-frame bursts and screenshots at settled
rest, during a slow turn and while moving or steering. Complete the matched A/B
scene captures before switching between internal and external back views; then
record the view cuts and cross a gate if practical. Use the same active-light
moment on both sides when available.

After the next build is installed, combine the chase follow-up here: check that
the predictive aiming hint appears on a selected target and follows it while
turning. Report the new distance/softer follow. If crossing a gate resets the
view, switch back to chase once; the combined diagnostics will record both
events. Docking is optional. The installed build does not yet include this
lead-marker correction.

Report sharpness, halos, shimmer or flicker, ghosting and recovery after view
transitions. Separately compare hull color and brightness, emissive detail,
active-light response and any obvious slowdown. Both logs are needed even if
the image looks unchanged.

This slice covers thirty reviewed pairs: twenty DEFAULT hull materials and
ten Argon bump-mapped materials. It does not cover every ship/effect. Analysis
must confirm nonzero material routes, inspect `bump_routed` to establish whether
the new bump path was exercised, and inspect refusal reasons and captured
constants before judging appearance or expanding coverage. This fixed-EV pair
cannot replace run 3's automatic-exposure/bloom comparison. It establishes the
0.75 sharpen and -0.5 mip-bias behavior on the HDR/AgX route; it does not count
as a separate non-HDR gameplay test.
