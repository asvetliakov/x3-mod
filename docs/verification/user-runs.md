# Outstanding user gameplay runs

Archive with `python3 tools/analysis/archive_user_runs.py`.

Updated 2026-09-26 (Run90 installed: logging tiers, option cleanup, single shadow map removed; Run 90 A queued, Run 89 A superseded). Earlier: 2026-09-21 (Run56 accepted for media stability; Run57 station-flash correction accepted; fog range remains under investigation). Run 17 crypto acceptance and the first-person/chase
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
| 90 A | Run90 (logging tiers: `--debug` / `--perf`, `x3m.log`; obsolete options and code removed; `--taa-k` / `--taa-sentinel` removed; plus Run89's single-map removal and cascade 3 at 4096): one launch with `x3run --direct --debug --perf`, a second short launch for the log rotation and exit, an optional `--no-shadow-cascades` launch | 0 | Queued 2026-09-26 |
| 89 A | Run89 (single shadow map removed, cascades only; `--no-shadow-cascades` = shadows off; cascade sizes 2048,4096,4096,4096,2048): one launch with the short stand command, plus an optional short `--no-shadow-cascades` launch | 0 | Superseded 2026-09-26 by Run 90 A before it was flown (Run90 installed the logging tiers and the option cleanup the same night); its checks are folded into Run 90 A |
| 88 A | Run88 (shadow pop fix, adjacency without telemetry, promoted defaults with cascade sizes 2048,4096,4096,2048,2048): one launch, short stand command (run337) | 1 | Completed 2026-09-25: all good (user); retention store never flushed (0 rows vs 18,992 in run336), probe never fired, no underflow; replay us p50 251 (was 124: two 4096 maps + live retention); defaults in force; cascade 3 bumped to 4096 afterwards (launcher default 2048,4096,4096,4096,2048, unflown) |
| 86 A | Run86 (far clip 7x7 + ramp 60/68; opt-in effects stage phase 1, chase view across docking; fog empty table; launcher report lines): plants + regression (run333), combat + effects look (run334), docking (run335) | 3 | Completed 2026-09-25: plants sparkles fixed (rest 120 -> 1 measured, pan 65 -> 46 invisible remainder; run333-run86a-plants/); effects modernisation dropped by the user after seeing it (old effect design, many tuning hours); docking restore worked (transfer path=dock, 258 at f0c4b) but the selection boxes vanished after undock and saves while docked restore first person anyway: dropped; both removed from production |
| 85 A | Run85 (far clip 7x7 + ramp 60/68): plants at rest/pans, regression, combat capture | 0 | Superseded 2026-09-25 by Run 86 A before it was flown (Run86 installed the same night with the opt-ins); its checks are folded into Run 86 A |
| 84 A | Run84 (far gate camera default, sun occlusion default with core dimming, cursor launch arm, fill 0.01): defaults + cursor at launch + sun crossing + fog-band plants (run329 5120x1440; run330 1920x1080 menu only; run332 the plants with --taa-debug, F8 at rest and in a pan) | 3 | Completed 2026-09-25: rows as expected (cursor_reassert armed_by=launch fired, sun_occlusion_config default=1, far_gate=camera default=1, original_fill_mode default=1, window_mode moved); the desktop arrow is still visible from launch and the double cursor still appears sometimes after alt-tab: the re-assert sequence fires correctly on all 20 fires, the game makes no cursor calls and gets no WM_SETCURSOR while active, cause inside the Cocoa driver, **parked by the user** (run329-run84a-cursor/); the plants' sparkles persist and occur at rest too (user correction): not the far gate but sub-pixel highlights leaking through a partial far weight (plants at 89-137k view units = 18-27 km, ramp 102-166k) and erased by the 3x3 clip on the dark phases (run332 rest 120/120 clamped; run329-run84a-rest-sparkles/); fix on main 22776b6f (far ramp 60/68 + 7x7 far clip, design taa-thin-classification.md); the far-gate screen A/B was dropped (fixture: screen + 7x7 still sparkles on a 0.4 px line under fractional pans, 19.2 codes vs 4.9) |
| 83 A/B | Run83 (FOV load remap, mask fold, window rect default, cursor trace/re-assert opt-in, fill default) + install-fleet4: defaults with the pre-patch save, window, folded TAA, fps (A launch 1+3 = run323, `both` relaunch run324); gpu attribution (A launch 2 = run325); plants shimmer diagnostic + partial sun occlusion (B = run326 sun, run327 plants with --taa-debug) | 5 | Completed 2026-09-25: FOV remapped from the first scene frame after the save load; window moved 0,31 -> 0,0 (menu bar gone, user); double cursor from launch until the first alt-tab (no launch arm; fixed on main b45b3735); lattice ok; fog-band plants show one-frame sparkles at thin-line edges under a pan only, with the search on or off: resolved-output analysis (run327) puts them on owned plant pixels outside the region on the plain path, the far stabiliser's screen-speed gate dropping 0.985 -> 0.9 under any pan (fix: --taa-far-gate camera, in review); the sun hidden at half cover was the vanilla CPU probe, --sun-occlusion accepted (run326) and made the launcher default (50a5f98e); TAA span 5.44-5.68 -> 3.88-4.05 ms measured (-1.6 to -1.8 ms, run325); results run323-run83a-launch1-3/, run323-fog-plants-shimmer/, run325-run83a-gpu/, run327-run83b-sparkles/ |
| 82 B | Ad hoc on the Run82 DLL: defaults again (run319), `--taa-thin-region 0` (run320), the install-fleet4 overlay with the louvre recipe (run321) | 3 | Completed 2026-09-25: run319 defaults (shell still exporting source vote + stabiliser 0): the Terran lattice crawled with the search on too; run320 thin treatment off: a little shimmer under pan (rejected, the hold stays); frame time without `--gpu-sync-timing` p50 16 ms in fog and clear sectors vs 30-33 ms with it (the flag doubles the frame; playable fps is quoted only from launches without it); run321 louvre recipe: crawl gone, no issues (3 draws per node, flip share 0.326 vs 0.497) |
| 82 A | FOV remap (menu in game units), sun flare fix, chase compensation, S4 half default, cutout ownership: defaults look + menu/sun/chase checks; stabiliser 0 retry; thin-region source vote A/B, at 5120x1440 (candidate d1a4e360) | 5 | Completed 2026-09-25 (run312 defaults; run313/314 stabiliser 0; run315/318 source vote, flown with the stabiliser still 0 from the shell): FOV menu, sun at 100, chase size and the fog-band plants accepted by the user; run312 loaded a pre-patch save and ran at the vanilla 0x4000 until the first menu step (the savegame restores registry+0x24, RE §7.4; load-site remap in progress); stabiliser 0: plant crops valid depth 0.97 vs 0.17-0.48 in run311, no no_zwrite refusals, no shimmer seen -> default flips to 0 (§5 conditions 1, 2, 4; condition 3 replay still open); vote source: mask draw 1.38-1.40 ms in every session (no saving), and the lattice crawl the user reported in run315 is explained by vote-only flagging (the coarse record's solid cell draws never vote; 7.5 % of the flickering lattice pixels in the A' region vs 72-80 % under both) -> vote rejected as a source; results `verification/results/run312-run82a-launch1/`, `run313-run82a-stabiliser-off/`, `run315-run82a-thin-source-vote/`, `run315-run82a-lattice-crawl/` |


**Run 90 A (queued 2026-09-26; Run90 DLL `6f6be732…` from f9cceb17, installed 04:00; overlay install-fleet4 unchanged).**
New since Run 88 A (the last flight): the **logging tiers** (`--debug` / `--perf` replace the telemetry options; the DLL writes
`x3m.log` into the game directory and keeps the previous launch's as `x3m.prev.log`; no file I/O on game threads; the always
tier is bounded), the **obsolete options and their code removed**, **`--taa-k` / `--taa-sentinel` removed** (k derived, sentinel
auto: no visible change expected), the **single shadow map removed** (cascades only) and **cascade 3 at 4096**. The old stand
command no longer works (its options are gone); the new one is below. Please name the sector of each stand and note whether
X3AP.exe stays in the process list after quitting.

Launch 1, at 5120x1440:

```sh
env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 /Users/asvetl/x3-mod/x3run --direct --debug --perf
```

1. **Log**: after the game is up, `x3m.log` exists in the game directory (`drive_c/X3/x3m.log`) and grows; no `session-*.log` is
   written any more. After quitting: the file ends with a `session_end` row and X3AP.exe is gone within a few seconds.
2. **Stutter**: fly the run337 station stand and a busy fight or a sector with many ships for a few minutes: any microstutter or
   hitching that run337 did not have? The fps overlay against run337 (no `--gpu-sync-timing`, so the numbers are playable fps).
   Rows: `log_dropped` must not appear; `telemetry_metric` rows for `log_wake` and the writer's write buckets.
3. **Shadows unchanged**: the run337 stand: shadows present as in run337, no pop, no missing or black shadows; an alt-tab and back.
   Rows: `shadow_cascades_mode ... sizes=2048,4096,4096,4096,2048`, `sun_shadow_apply_mode ... enabled=1`,
   `shadow_retention_flush` at most once (teardown).
4. **Cascade 3 at 4096**: shadow edges 3-7 km out (large stations, the far ring of a complex): cleaner than run337 or no visible
   difference; any new stutter. Rows: `shadow_replay_depth ... us=` (run337: p50 251, p90 360, p99 528).
5. **TAA look**: no change expected on the plants, edges and the sky (k and the sentinel policy were already the derived / auto
   values). Say if anything looks different.
6. Exit through the menu.

Launch 2 (short): the same command again; load, look, quit. Expected: `x3m.prev.log` holds launch 1's log (ending with
`session_end`), `x3m.log` is launch 2's. Rows: `log_open ... previous=renamed`.

Launch 3 (optional, short): the same command plus `--no-shadow-cascades`: load the save, look at the stand, quit. Expected: no
replayed shadows and vanilla-looking hull lighting, no crash; the fog is off in this launch too (the launcher needs the cascade
maps for it). Rows: `shadow_cascades_mode ... reason=off`, `sun_shadow_apply_mode ... enabled=0 ... cascades=0`,
`shadow_replay_candidates_device enabled=0 reason=no_cascades`.

Run 90 A is the only queued run. Completed instructions for Runs 73-88 are in the [archive](../archive/user-runs-completed.md).

## Stand command

Since 2026-09-25 the functional options of the Run 84 A stand command, `--music-keep` and `--shadow-alpha-casters on`
are launcher defaults ([inventory](launcher-options-inventory.md#defaults-promoted-2026-09-25)); since 2026-09-26 its
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
# --capture-start 999999 --capture-frames 8 --capture-delay 300 (no automatic capture; F8 captures on demand)
# --music-keep --shadow-alpha-casters on
```
