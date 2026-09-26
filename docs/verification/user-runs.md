# Outstanding user gameplay runs

Archive with `python3 tools/analysis/archive_user_runs.py`.

Updated 2026-09-26 (Run 91 A completed and accepted: run338 default flight, run339 player mode with the shipped x3m.ini; no run queued). Earlier: 2026-09-21 (Run56 accepted for media stability; Run57 station-flash correction accepted; fog range remains under investigation). Run 17 crypto acceptance and the first-person/chase
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
| 91 A | Run91 (developer options trimmed with `--draw-trace`; hotkeys removed except F8 under `--debug`; `--capture-start`/`--capture-delay` gone; the `x3m.ini` settings file; plus everything since Run 88 A: logging tiers, option cleanup, single shadow map removed, cascade 3 at 4096): launch 1 default stand `x3run --direct --debug --perf` (run338), launch 2 player mode `x3run --direct --config --debug --perf` with F8 captures (run339), optional `--no-shadow-cascades` not flown | 2 | Completed 2026-09-26: "everything looks okay" (user), Run91 accepted; run338 `config_open source=bare`, 20,416 frames, `session_end ... exception=0 dropped=0 filter=ours`; run339 `config_open file=C:\X3\x3m.ini source=game keys=0` (394 lines, 11.4 ms), 121 `proxy_options` values `@default`, `log_open previous=renamed`, three F8 captures (frames 2242, 6650, 7102), 11,202 frames, no drops, exit clean; two pre-existing notes raised by the user and triaged afterwards: distant stations/asteroids drawn over the player's bullets (captures in run339), stations blurry under a pan (no capture) |
| 90 A | Run90 (logging tiers: `--debug` / `--perf`, `x3m.log`; obsolete options and code removed; `--taa-k` / `--taa-sentinel` removed; plus Run89's single-map removal and cascade 3 at 4096): one launch with `x3run --direct --debug --perf`, a second short launch for the log rotation and exit, an optional `--no-shadow-cascades` launch | 0 | Superseded 2026-09-26 by Run 91 A before it was flown (Run91 installed the option trim, the hotkey removal and the `x3m.ini` file the same morning); its checks are folded into Run 91 A |
| 89 A | Run89 (single shadow map removed, cascades only; `--no-shadow-cascades` = shadows off; cascade sizes 2048,4096,4096,4096,2048): one launch with the short stand command, plus an optional short `--no-shadow-cascades` launch | 0 | Superseded 2026-09-26 by Run 90 A before it was flown (Run90 installed the logging tiers and the option cleanup the same night); its checks are folded into Run 90 A |
| 88 A | Run88 (shadow pop fix, adjacency without telemetry, promoted defaults with cascade sizes 2048,4096,4096,2048,2048): one launch, short stand command (run337) | 1 | Completed 2026-09-25: all good (user); retention store never flushed (0 rows vs 18,992 in run336), probe never fired, no underflow; replay us p50 251 (was 124: two 4096 maps + live retention); defaults in force; cascade 3 bumped to 4096 afterwards (launcher default 2048,4096,4096,4096,2048, unflown) |
| 86 A | Run86 (far clip 7x7 + ramp 60/68; opt-in effects stage phase 1, chase view across docking; fog empty table; launcher report lines): plants + regression (run333), combat + effects look (run334), docking (run335) | 3 | Completed 2026-09-25: plants sparkles fixed (rest 120 -> 1 measured, pan 65 -> 46 invisible remainder; run333-run86a-plants/); effects modernisation dropped by the user after seeing it (old effect design, many tuning hours); docking restore worked (transfer path=dock, 258 at f0c4b) but the selection boxes vanished after undock and saves while docked restore first person anyway: dropped; both removed from production |
| 85 A | Run85 (far clip 7x7 + ramp 60/68): plants at rest/pans, regression, combat capture | 0 | Superseded 2026-09-25 by Run 86 A before it was flown (Run86 installed the same night with the opt-ins); its checks are folded into Run 86 A |
| 84 A | Run84 (far gate camera default, sun occlusion default with core dimming, cursor launch arm, fill 0.01): defaults + cursor at launch + sun crossing + fog-band plants (run329 5120x1440; run330 1920x1080 menu only; run332 the plants with --taa-debug, F8 at rest and in a pan) | 3 | Completed 2026-09-25: rows as expected (cursor_reassert armed_by=launch fired, sun_occlusion_config default=1, far_gate=camera default=1, original_fill_mode default=1, window_mode moved); the desktop arrow is still visible from launch and the double cursor still appears sometimes after alt-tab: the re-assert sequence fires correctly on all 20 fires, the game makes no cursor calls and gets no WM_SETCURSOR while active, cause inside the Cocoa driver, **parked by the user** (run329-run84a-cursor/); the plants' sparkles persist and occur at rest too (user correction): not the far gate but sub-pixel highlights leaking through a partial far weight (plants at 89-137k view units = 18-27 km, ramp 102-166k) and erased by the 3x3 clip on the dark phases (run332 rest 120/120 clamped; run329-run84a-rest-sparkles/); fix on main 22776b6f (far ramp 60/68 + 7x7 far clip, design taa-thin-classification.md); the far-gate screen A/B was dropped (fixture: screen + 7x7 still sparkles on a 0.4 px line under fractional pans, 19.2 codes vs 4.9) |
| 83 A/B | Run83 (FOV load remap, mask fold, window rect default, cursor trace/re-assert opt-in, fill default) + install-fleet4: defaults with the pre-patch save, window, folded TAA, fps (A launch 1+3 = run323, `both` relaunch run324); gpu attribution (A launch 2 = run325); plants shimmer diagnostic + partial sun occlusion (B = run326 sun, run327 plants with --taa-debug) | 5 | Completed 2026-09-25: FOV remapped from the first scene frame after the save load; window moved 0,31 -> 0,0 (menu bar gone, user); double cursor from launch until the first alt-tab (no launch arm; fixed on main b45b3735); lattice ok; fog-band plants show one-frame sparkles at thin-line edges under a pan only, with the search on or off: resolved-output analysis (run327) puts them on owned plant pixels outside the region on the plain path, the far stabiliser's screen-speed gate dropping 0.985 -> 0.9 under any pan (fix: --taa-far-gate camera, in review); the sun hidden at half cover was the vanilla CPU probe, --sun-occlusion accepted (run326) and made the launcher default (50a5f98e); TAA span 5.44-5.68 -> 3.88-4.05 ms measured (-1.6 to -1.8 ms, run325); results run323-run83a-launch1-3/, run323-fog-plants-shimmer/, run325-run83a-gpu/, run327-run83b-sparkles/ |


No run is queued. Completed instructions for Runs 73-88 are in the [archive](../archive/user-runs-completed.md).

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
