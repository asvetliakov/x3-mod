# Outstanding user gameplay runs

Archive with `python3 tools/analysis/archive_user_runs.py`.

Updated 2026-09-26 (Run93 installed: single-copy bullets, opt-in rotation-aware weight; Run 93 A queued; Run 92 A accepted). Earlier: 2026-09-21 (Run56 accepted for media stability; Run57 station-flash correction accepted; fog range remains under investigation). Run 17 crypto acceptance and the first-person/chase
left-centre-right diagnostic are complete and are not in this queue. The agent
never launches the game. Only open runs keep their instructions here; a completed
run keeps only its row in the table below. The installed build is described in [status](../status.md).
Use the exact launcher paths in the commands below. Use the main-checkout launcher for the accepted baseline; no video-package
preflight is required. It handles the shared Wine lock and snapshots; no shell
function setup is needed. Runs 1–3 and 5–20 are complete (queue numbers; reader/DAT/adjacency fast
co-activation passed as snapshot run 19). Run 9 is saved as snapshot run 28.
Close X3 between runs and report completed numbers. After exit, the helper prints
a fresh `/tmp/x3-bottleX3-run<N>/` path containing that session’s log and referenced
captures, so later A/B runs cannot overwrite them. Vanilla/dry-run creates no snapshot.
Since 2026-09-14 the linear distance-fade route is on by default whenever
`--linear-materials --taa` are present ([region note](../architecture/linear-distance-fade-region.md),
"Default"); the queue commands below keep `--linear-distance-fade` spelled out,
which is the same resolved setting, and `--no-linear-distance-fade` opts out.

| Run | Purpose | Sessions | Status |
| --- | --- | ---: | --- |
| 92 A | Run92 (TAA history weight default 0.85, `bolt_copy` capture diagnostic): launch 1 default with `--taa-debug`, F8 at rest / mid-pan / while firing (run341); launch 2 `--taa-history-weight 0.9` (run342); launch 3 `--taa-history-weight 0.8` (run343), rest + pan F8 each | 3 | Completed 2026-09-26: **0.85 accepted as the default** (user: with 0.8 the adjacent station shimmers at rest and under the pan; 0.85 and 0.9 look similar, shimmer less noticeable); **bullet copies proven identical**: 32 `bolt_copy` rows over 8 capture frames (2098-2105), the early draws 6/7 and the late draws 211-214 of each of the two bullet buffers carry the same `hash`, `qsum` and `bbox` in 8 of 8 frames (revisions n / n+1) -> the fix drops the early copy (single-copy rule, in implementation); all three sessions `session_end ... exception=0 dropped=0` |
| 91 A | Run91 (developer options trimmed with `--draw-trace`; hotkeys removed except F8 under `--debug`; `--capture-start`/`--capture-delay` gone; the `x3m.ini` settings file; plus everything since Run 88 A: logging tiers, option cleanup, single shadow map removed, cascade 3 at 4096): launch 1 default stand `x3run --direct --debug --perf` (run338), launch 2 player mode `x3run --direct --config --debug --perf` with F8 captures (run339), optional `--no-shadow-cascades` not flown | 2 | Completed 2026-09-26: "everything looks okay" (user), Run91 accepted; run338 `config_open source=bare`, 20,416 frames, `session_end ... exception=0 dropped=0 filter=ours`; run339 `config_open file=C:\X3\x3m.ini source=game keys=0` (394 lines, 11.4 ms), 121 `proxy_options` values `@default`, `log_open previous=renamed`, three F8 captures (frames 2242, 6650, 7102), 11,202 frames, no drops, exit clean; two pre-existing notes raised by the user and triaged afterwards: distant stations/asteroids drawn over the player's bullets (captures in run339), stations blurry under a pan (no capture) |
| 90 A | Run90 (logging tiers: `--debug` / `--perf`, `x3m.log`; obsolete options and code removed; `--taa-k` / `--taa-sentinel` removed; plus Run89's single-map removal and cascade 3 at 4096): one launch with `x3run --direct --debug --perf`, a second short launch for the log rotation and exit, an optional `--no-shadow-cascades` launch | 0 | Superseded 2026-09-26 by Run 91 A before it was flown (Run91 installed the option trim, the hotkey removal and the `x3m.ini` file the same morning); its checks are folded into Run 91 A |
| 89 A | Run89 (single shadow map removed, cascades only; `--no-shadow-cascades` = shadows off; cascade sizes 2048,4096,4096,4096,2048): one launch with the short stand command, plus an optional short `--no-shadow-cascades` launch | 0 | Superseded 2026-09-26 by Run 90 A before it was flown (Run90 installed the logging tiers and the option cleanup the same night); its checks are folded into Run 90 A |
| 88 A | Run88 (shadow pop fix, adjacency without telemetry, promoted defaults with cascade sizes 2048,4096,4096,2048,2048): one launch, short stand command (run337) | 1 | Completed 2026-09-25: all good (user); retention store never flushed (0 rows vs 18,992 in run336), probe never fired, no underflow; replay us p50 251 (was 124: two 4096 maps + live retention); defaults in force; cascade 3 bumped to 4096 afterwards (launcher default 2048,4096,4096,4096,2048, unflown) |
| 86 A | Run86 (far clip 7x7 + ramp 60/68; opt-in effects stage phase 1, chase view across docking; fog empty table; launcher report lines): plants + regression (run333), combat + effects look (run334), docking (run335) | 3 | Completed 2026-09-25: plants sparkles fixed (rest 120 -> 1 measured, pan 65 -> 46 invisible remainder; run333-run86a-plants/); effects modernisation dropped by the user after seeing it (old effect design, many tuning hours); docking restore worked (transfer path=dock, 258 at f0c4b) but the selection boxes vanished after undock and saves while docked restore first person anyway: dropped; both removed from production |
| 85 A | Run85 (far clip 7x7 + ramp 60/68): plants at rest/pans, regression, combat capture | 0 | Superseded 2026-09-25 by Run 86 A before it was flown (Run86 installed the same night with the opt-ins); its checks are folded into Run 86 A |
| 84 A | Run84 (far gate camera default, sun occlusion default with core dimming, cursor launch arm, fill 0.01): defaults + cursor at launch + sun crossing + fog-band plants (run329 5120x1440; run330 1920x1080 menu only; run332 the plants with --taa-debug, F8 at rest and in a pan) | 3 | Completed 2026-09-25: rows as expected (cursor_reassert armed_by=launch fired, sun_occlusion_config default=1, far_gate=camera default=1, original_fill_mode default=1, window_mode moved); the desktop arrow is still visible from launch and the double cursor still appears sometimes after alt-tab: the re-assert sequence fires correctly on all 20 fires, the game makes no cursor calls and gets no WM_SETCURSOR while active, cause inside the Cocoa driver, **parked by the user** (run329-run84a-cursor/); the plants' sparkles persist and occur at rest too (user correction): not the far gate but sub-pixel highlights leaking through a partial far weight (plants at 89-137k view units = 18-27 km, ramp 102-166k) and erased by the 3x3 clip on the dark phases (run332 rest 120/120 clamped; run329-run84a-rest-sparkles/); fix on main 22776b6f (far ramp 60/68 + 7x7 far clip, design taa-thin-classification.md); the far-gate screen A/B was dropped (fixture: screen + 7x7 still sparkles on a 0.4 px line under fractional pans, 19.2 codes vs 4.9) |


**Run 93 A (queued 2026-09-26; Run93 DLL `5bee0e8a…` from 92f8223d, installed 21:14: single-copy bullets on by default, the opt-in rotation-aware TAA motion weight).**
Two questions: are the bullets right now (one brightness whether or not a distant station is behind them), and does the
rotation-aware weight sharpen stations under a pan without shimmer you would not accept. Same stand as Run 92 A (a large
station 2-10 km away, no lattice type if possible); pan at roughly the same speed in every launch; name the sector. All
launches carry `--taa-debug` so the captures hold the resolved image.

Launch 1 (defaults: history weight 0.85, single-copy bullets):

```sh
env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 /Users/asvetl/x3-mod/x3run --direct --debug --perf --taa-debug
```

1. **Bullets**: fire across the distant station so bolts cross its silhouette: the bolts keep one brightness over the
   station and over empty space (no more "station in front of the bullets"); also fire into empty space in first
   person for a few seconds: the bolts must never vanish for a frame. F8 once while firing at the station. Rows:
   `bolt_copies early_dropped=2 late=2` (or 1/1) on firing frames; `bolt_copy_dropped` on the capture frame.
2. F8 at rest and F8 mid-pan (the 0.85 reference for launch 2).
3. Exit through the menu.

Launch 2 (rotation-aware weight, base 0.9):

```sh
env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 /Users/asvetl/x3-mod/x3run --direct --debug --perf --taa-debug --taa-history-weight 0.9 --taa-motion-weight-rotation 0.7
```

F8 at rest, F8 mid-pan (same speed as launch 1), then a slow turn (about a quarter of the pan speed) for a few seconds and
F8 once during it. Your eye: the station sharper under the pan than in launch 1? Any shimmer during the slow turn, on the
station or on the stars? Any visible change of the whole picture at the moment a turn starts or stops? At rest it should
look exactly like 0.9 (run342).

Launch 3 (optional, if launch 2 shimmers during slow turns): `--taa-motion-weight-rotation 0.75,4,12` instead, same
checks.

Run 93 A is the only queued run. Completed instructions for Runs 73-88 are in the [archive](../archive/user-runs-completed.md).

## Stand command

Since 2026-09-25 the functional options of the Run 84 A stand command, `--music-keep` and `--shadow-alpha-casters on`
are launcher defaults ([inventory](launcher-options-inventory.md#promoted-defaults-2026-09-25)); since 2026-09-26 its
telemetry/debug options are the two logging groups ([logging tiers](../architecture/logging-tiers.md)): `--debug`
(rendering-state rows every frame, censuses, traces) and `--perf` (per-frame cost rows, frame-time and phase windows,
FPS overlay). The DLL expands both; the `X3M_MOTION_FRAME_LOG=1` prefix is gone (the launcher drops it, `--debug`
gives the cadence). The log is `<game dir>\x3m.log` (the previous launch's is `x3m.prev.log`); `x3run`'s snapshot copies
it into the run directory under its `session-*.log` name.

```sh
env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 /Users/asvetl/x3-mod/x3run --direct --debug --perf
```

```sh
# --debug --perf stand for the former --telemetry --camera-log 1 --fps-overlay --frame-end-stride 1
# --volumetric-fog-timing --frame-timing --frame-phases --object-bounds-log --cull-census and the X3M_MOTION_FRAME_LOG=1
# prefix (all removed from the launcher on 2026-09-26; --frame-end-stride stays as an explicit knob), plus the other
# debug-tier diagnostics (retention census, LOD switch rows, window/music/media/sun traces, sector background, loading
# probes, collide narrow census and query phases; docs/architecture/logging-tiers.md, "Implemented").
# Explicit form of the functional defaults: the old Run 84 A functional options give the same environment except an
# explicit --shadow-cascade-sizes 2048,2048,2048,2048,2048 (the default is 2048,4096,4096,4096,2048, user decision 2026-09-25):
# --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa
# --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer
# --resource-read fast --dat-handles --mesh-adjacency fast --screen-emission-additive 2 --screen-emission-additive-alpha 0
# --emission-source-gain 2 --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply
# --shadow-sun-poll on --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance
# --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,4096,4096,4096,2048
# --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --light-map-far-fade 80,220 --motion-rt-mode lazy
# --volumetric-fog 0.02 --volumetric-fog-cards replace --volumetric-fog-range stored --cull-small-parts 4
# --capture-frames 8 (no automatic capture; F8 under --debug captures 8 frames at once; --capture-start and
# --capture-delay were removed on 2026-09-26)
# --music-keep --shadow-alpha-casters on
```
