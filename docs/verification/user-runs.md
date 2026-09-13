# Outstanding user gameplay runs

Updated 2026-09-13. Run 17 crypto acceptance and the first-person/chase
left-centre-right diagnostic are complete and are not in this queue. The agent
never launches the game. Installed build: chase firing fix with 13° pitch and
0.85 distance; [build record](../../verification/results/chase-fire-install.json).
From the repository root, define this terminal helper once, then paste a run
command below. Close X3 between runs and report completed numbers when convenient.

```sh
x3run() {
  X3M_BOTTLE=X3 python3 verification/probe/wine_lock.py --holder user-game \
    python3 tools/manage.py launch --bottle X3 "$@"
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

## 3. Space-aware exposure baseline — Ready

Keep sharpen and mip bias at zero so this remains comparable to the earlier HDR
baseline:

```sh
x3run --direct --ownership --object-trace --object-lifetime --motion-output --taa \
  --telemetry --taa-debug --taa-sharpen 0 --taa-mip-bias 0 \
  --hdr --hdr-tonemap --capture-start 999999 --capture-frames 4
```

Capture settled dark space, a bright object/emitter, and a turn between them.
Do not set manual EV. Report exposure pumping, adaptation feel, lost bright
detail, crushed dark detail, and HUD/menu readability. Record which F8 bursts
were captured.

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

## 6. Bloom gameplay — Waiting for integration

Bloom components are fixture-qualified but not linked into the installed
renderer. Add bloom to a later graphics session only after integration produces
a reviewed build and an explicit launch command. No bloom command or acceptance
claim exists yet.
