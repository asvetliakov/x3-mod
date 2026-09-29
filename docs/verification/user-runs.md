# Outstanding user gameplay runs

Archive with `python3 tools/analysis/archive_user_runs.py`.

Updated 2026-09-29 (Run104 = release 0.8.0 installed: defaults gain 1 / clamp 0.7 / flare 0.3; Run 104 A queued: plain flight on the defaults). Earlier the same day: Run103 = registry capacity fix + emission clamp + lens-flare gain installed on the Mayhem 3 tree; Run 103 A queued: one Mayhem flight, shimmer at rest + Ocelot exhausts). Earlier the same day: Run102 = release 0.7.0 installed; Run 102 A superseded (its flights became run357-run364: TAA had no history because the lifetime observer had disabled itself during the Mayhem load; engine exhausts = the hull-card gain 2 plus Mayhem's own lens flares). Earlier: 2026-09-28 (Run 100 A completed: no noticeable shimmer, lock made the default; Run101 = release 0.6.0 installed with the template and the regenerate tool; Run 101 A queued: a plain flight on the defaults). Earlier: 2026-09-21 (Run56 accepted for media stability; Run57 station-flash correction accepted; fog range remains under investigation). Run 17 crypto acceptance and the first-person/chase
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
| 104 A | Run104 = release 0.8.0 (defaults `emission_source_gain 1`, `emission_source_clamp 0.7`, `lens_flare_gain 0.3`; renderer code equal to Run103). Optional: delete the three explicit lines from the game-folder `x3m.ini` (they equal the defaults, except clamp 0.75 if you prefer it). One plain flight: `env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 ./x3run --direct --debug --perf` (the launcher sends the same defaults). Confirm the Run 103 A look: stations steady at rest, exhausts and halos as accepted; rows expected: `version=0.8`, `emission_source_gain_mode gain=1 clamp=0.7`, `lens_flare_gain value=0.3`, `object_lifetime ... capacity=262144` | 1 | Queued 2026-09-29 |
| 103 A | Run103 (registry 262,144 + backward-shift deletion, `object_lifetime_disabled`/`object_lifetime_stats` rows, `hull_emission_gain` follows `emission_source_gain` in the file, `emission_source_clamp`, `lens_flare_gain`, `--vanilla` guard). In the game-folder `x3m.ini`: `emission_source_gain = 2` (or leave the default), `emission_source_clamp = 0.7`, `lens_flare_gain = 0.5`, restore `;hdr_ev_max`, `;hdr_bloom` to their defaults, drop `hull_emission_gain`. One flight on the Mayhem save: `env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 ./x3run --direct --debug --perf --config`. Observations: stations and the own ship at rest (shimmer as in run358, or steady as on the stock tree?), the Ocelot exhaust cores and halos versus `screenshots/engines2.png`, any dark spots where guide lights cross lit hull windows. Rows expected: `object_lifetime ... capacity=262144` at install, no `object_lifetime_disabled` row, `object_lifetime_stats ... live=` rising past 16,384 on the Mayhem load, `motion_output_frame ... matched=` above 0, `emission_source_gain_mode ... clamp=0.7`, `lens_flare_gain ... configured=1`, `lens_flare_gain_frame ... gained=` above 0 | 1 | Completed 2026-09-29: **accepted** (\"working now, no issues\"): stations steady at rest, exhausts accepted at clamp 0.7 / flare 0.5; run365 rows: `object_lifetime ... capacity=262144`, no `object_lifetime_disabled`, `object_lifetime_stats peak_live=44913` on the Mayhem load (past the old 16,384: capacity cause confirmed, measured), `motion_output_frame matched=` 168-216 per frame, `emission_source_gain_mode clamp=0.7`, `lens_flare_gain configured=1`; user decision: defaults become gain 1 / clamp 0.7 / flare 0.3, release 0.8.0 |
| 102 A | Run102 = release 0.7.0 (bake tooling; renderer unchanged). Step 1: close the game and run `~/Library/Application\ Support/CrossOver/Bottles/X3/drive_c/X3/x3m-regenerate` (about 25 min; every body rebuilds once). Step 2: one plain flight. Observations, any sector, when convenient: a Stellaris machine flagship keeps its shade across the LOD switch; five-port dock marker lights stay bright; the 23 Teladi/pirate/Split bodies show grey, not black, untextured trim at distance; Falchion, Kyoto, Pride of Albion show no shading step | 1 | Superseded 2026-09-29 by Run103 (flights run357-run364 diagnosed the shimmer and the exhausts) |
| 101 A | Run101 = release 0.6.0 (lock on by default; template and x3m-regenerate.exe refreshed): one plain flight from the launcher (`./x3run --direct --debug --perf`) or the CrossOver shortcut; stations at rest and under pan, speech, alt-tab | 1 | Not reported; superseded by Run102 (2026-09-29) |
| 100 A | Run100 (luminance lock section 12, opt-in): launch 1 `--taa-debug` (run355); launch 2 `--taa-debug --taa-luma-lock 16` (run356), 1920x1080 | 2 | Completed 2026-09-28: **accepted**, "I don't notice shimmer now", foggy sector with two solar plants fine; triage: 25-50 % sharper under pan, rest flicker 2.4-2.9x the blanket (between the 0.95/0.90 blankets), locked share spacedock 31 % rest / 45-49 % pan, sky 0.04-0.10 %, frame time 15 ms both; lock made the default (0.6.0) |
| 99 A | Run99 (TAA luminance lock, opt-in `--taa-luma-lock 16`): launch 1 default (run353); launch 2 `--taa-luma-lock 16` (run354), 1920x1080 | 2 | Completed 2026-09-28: **blur fixed** ("much better in regard of blurring"); the far spacedock ("Federal Argon Shipyard") shimmers under pan: rest-created locks cover ~80 % of the pan ripple, creation was closed under the pan, unlocked far pixels took the 3x3 clip -> section 12; no --taa-debug dumps, frame time 15 ms both |
| 98 A | Run98 = release 0.5.2 (run_in_background patch default on, stripped DLL): one launch from the CrossOver shortcut without `-runinbg`, alt-tab out and back, speech | 1 | Completed 2026-09-27: "all is good now" (user): the alt-tab resumes at once, speech plays from the CrossOver launch via the bottle environment setting; rows `run_in_background status=patched`, `music_keep_active run_in_background=1` |
| 94 A | Run94 (bolts through the TAA, default on at W 0.5): launch 1 defaults with F8 firing at a far station (run348); launch 2 `--bolt-far-show 1` with F8 firing at a far station and at a self-shadowed hull (run349) | 2 | Completed 2026-09-27: **bullets fixed** (user); `--bolt-far-show 1` looks better than 0.5 -> **W default 1** (the user's decision; 0.5 had been chosen to match the bolt's measured retention over empty space so a bolt crossing a silhouette stayed continuous); no dark spots on the self-shadowed hull reported; both sessions exited clean |


Run 104 A is the only open run; its instructions are its table row. Completed instructions for Runs 73-88 are in the [archive](../archive/user-runs-completed.md).

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

**Run 101 A (queued 2026-09-28; Run101 = release 0.6.0, DLL `8b164ee1…` stripped, from 018d7e49, installed 05:57 together
with the 0.6.0 `x3m.ini` template and `x3m-regenerate.exe`; the luminance lock is now on by default).** One plain
flight, launcher or CrossOver shortcut, at your resolution:

```
./x3run --direct --debug --perf
```

Nothing to A/B: confirm that distant stations look as in run356 (sharp plates under pan, no noticeable shimmer) with no
option given, that speech plays and alt-tab resumes at once (the shortcut path), and mention anything that looks
different from Run 100 A. Rows expected: `motion_output_taa_luma_lock ... configured=1 gate=camera` without the option
on the command line, `version=0.6` in the header, `session_end`. To turn the lock off for a comparison: `--taa-luma-lock
0`, or `taa_luma_lock = 0` in `x3m.ini` for a shortcut launch.

Run 101 A is the only queued run. Completed instructions for Runs 73-100 are in the [archive](../archive/user-runs-completed.md).
