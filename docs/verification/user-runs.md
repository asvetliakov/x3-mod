# Outstanding user gameplay runs

Updated 2026-09-13. Run 17 crypto acceptance and the first-person/chase
left-centre-right diagnostic are complete and are not in this queue. The agent
never launches the game. Installed build: chase firing fix with 13° pitch and
0.85 distance, opt-in FP16 bloom and reviewed DEFAULT/BUMPMAP linear materials;
[build record](../../verification/results/linear-material-install.json).
From the repository root, define this terminal helper once, then paste a run
command below. Start with run 1; complete the rest over several sessions as convenient.
Close X3 between runs and report completed numbers. After exit, the helper prints
a fresh `/tmp/x3-bottleX3-run<N>/` path containing that session’s log and referenced
captures, so later A/B runs cannot overwrite them. Vanilla/dry-run creates no snapshot.

| Run | Purpose | Sessions | Status |
| --- | --- | ---: | --- |
| 1 | Chase aiming/framing + reader/adjacency verification | 1 | Start here |
| 2 | Sharpen/shimmer + camera cuts with TAA | 1 | Ready |
| 3 | Automatic exposure + bloom off/on | 2 | Ready |
| 4 | Vanilla double-cursor/menu-bar comparison | 1 | After any enhanced run |
| 5 | Reader/adjacency fast modes | 1 | Wait for run 1 log acceptance |
| 6 | Linear hull materials off/on at fixed exposure | 2 | Ready |

These are separate comparisons, not one long required session. The shared TAA shader now fits the standard instruction budget and passes exact
fixture comparisons; run 2 also covers that installed update. Emission integration
is still agent work and adds no gameplay request yet.

```sh
x3run() {
  local x3run_since_ns x3run_status
  x3run_since_ns=$(python3 -c 'import time; print(time.time_ns())') || return
  if X3M_BOTTLE=X3 python3 verification/probe/wine_lock.py --holder user-game \
      python3 tools/manage.py launch --bottle X3 "$@"; then
    x3run_status=0
  else
    x3run_status=$?
  fi
  python3 tools/analysis/snapshot_x3_run.py --since-ns "$x3run_since_ns" || true
  return "$x3run_status"
}
```

## 1. Camera correction plus loading verification — Ready, start here

The **13° / 0.85-distance camera and chase firing correction** are installed.
Combine reader and adjacency verification with the first normal save load:

```sh
x3run --direct --camera chase --telemetry \
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

## 2. Sharpen 0.75, mip bias -0.5 and chase/TAA cuts — Ready

Use the new camera build so one session can also check temporal history across
view changes:

```sh
x3run --direct --camera chase --ownership --object-trace --object-lifetime \
  --motion-output --taa --telemetry --taa-debug --camera-log 1 \
  --taa-sharpen 0.75 --taa-mip-bias -0.5 \
  --capture-start 999999 --capture-frames 4
```

First preserve a focused sharpen sample: press F8 for separate settled,
slow-turn and moving/steering bursts. Then switch internal/back views and cross
a gate if convenient. Report sharpness, shimmer/flicker, halos, ghosting after
view transitions, and whether each F8 burst was captured. This is the actual
0.75 measurement; do not fold HDR exposure into it.

## 3. Space-aware exposure and bloom comparison — Ready

Keep sharpen and mip bias at zero so this remains comparable to the earlier HDR
baseline:

```sh
x3run --direct --ownership --object-trace --object-lifetime --motion-output --taa \
  --telemetry --taa-debug --taa-sharpen 0 --taa-mip-bias 0 \
  --hdr --hdr-tonemap --capture-start 999999 --capture-frames 4
```

Keep the game's **Glow enabled**. In this baseline, capture settled dark space,
a bright object/emitter, and a turn between them. Do not set manual EV. Exit,
then repeat the same save and camera positions with bloom enabled:

```sh
x3run --direct --ownership --object-trace --object-lifetime --motion-output --taa \
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
x3run --direct --vanilla
```

Alt-tab out and back once. Report whether both the macOS arrow and game cursor
appear, whether their positions differ, and whether the macOS menu bar overlaps
the game. Compare the same screen as the enhanced run; load the save if the
problem only appears during gameplay. No F8 capture is needed.

## 5. Reader and adjacency fast modes — Waiting for log acceptance

Do not run until run 1 analysis accepts meaningful verify coverage with zero
admitted mismatches. An all-fallback run does not qualify either fast path.
After acceptance, both fast modes may share one functional load:

```sh
x3run --direct --telemetry --resource-read fast --dat-handles \
  --mesh-adjacency fast
```

The combined run can establish fault-free co-activation. Any claim about which
feature changed loading time still requires isolated, same-save comparisons.


## 6. First linear materials — Ready; after camera acceptance is convenient

This is a separate fixed-exposure comparison: keep bloom, sharpen and mip bias
off, so changes in hull lighting cannot be hidden by automatic exposure or glow.
Use the usual ship/save with a visible hull or station and emissive panels:

```sh
x3run --direct --ownership --object-trace --object-lifetime --motion-output --taa \
  --telemetry --taa-debug --taa-sharpen 0 --taa-mip-bias 0 \
  --hdr --hdr-tonemap --hdr-ev-manual 0 \
  --capture-start 999999 --capture-frames 4
```

Take F8 samples at rest and while slowly turning, plus a screenshot. Exit, then
repeat the same command **adding `--linear-materials`**, at the same save and
camera positions. Leave the three material gains at their default 1. If
convenient, fire or pass an active light during the second session and capture
it. Report hull color/brightness, emissive detail, flicker/ghosting, and any
obvious slowdown. Both logs are needed even if the image looks unchanged.

This slice covers thirty reviewed pairs: twenty DEFAULT hull materials and
ten Argon bump-mapped materials. It does not cover every ship/effect. Analysis
must confirm nonzero material routes, inspect `bump_routed` to establish whether
the new bump path was exercised, and inspect refusal reasons and captured
constants before judging appearance or expanding coverage. This fixed-EV pair
cannot replace run 3's automatic-exposure/bloom comparison.
