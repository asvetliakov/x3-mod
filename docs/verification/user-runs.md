# Outstanding user gameplay runs

Archive with `python3 tools/analysis/archive_user_runs.py`.

Updated 2026-09-28 (Run 99 A completed: lock fixes the pan blur, far spacedock struts shimmer under pan; Run100 = section-12 lock installed; Run 100 A queued: the same A/B with --taa-debug). Earlier: 2026-09-21 (Run56 accepted for media stability; Run57 station-flash correction accepted; fog range remains under investigation). Run 17 crypto acceptance and the first-person/chase
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
| 100 A | Run100 (luminance lock section 12: locks form under a camera pan, 7x7 box for far pixels under a pan; opt-in): launch 1 `--taa-debug`; launch 2 `--taa-debug --taa-luma-lock 16`; F8 at rest, mid-pan and after the pan in each, same stand and pan speed | 2 | Queued 2026-09-28 |
| 99 A | Run99 (TAA luminance lock, opt-in `--taa-luma-lock 16`): launch 1 default (run353); launch 2 `--taa-luma-lock 16` (run354), 1920x1080 | 2 | Completed 2026-09-28: **blur fixed** ("much better in regard of blurring"); the far spacedock ("Federal Argon Shipyard") shimmers under pan: rest-created locks cover ~80 % of the pan ripple, creation was closed under the pan, unlocked far pixels took the 3x3 clip -> section 12; no --taa-debug dumps, frame time 15 ms both |
| 98 A | Run98 = release 0.5.2 (run_in_background patch default on, stripped DLL): one launch from the CrossOver shortcut without `-runinbg`, alt-tab out and back, speech | 1 | Completed 2026-09-27: "all is good now" (user): the alt-tab resumes at once, speech plays from the CrossOver launch via the bottle environment setting; rows `run_in_background status=patched`, `music_keep_active run_in_background=1` |
| 94 A | Run94 (bolts through the TAA, default on at W 0.5): launch 1 defaults with F8 firing at a far station (run348); launch 2 `--bolt-far-show 1` with F8 firing at a far station and at a self-shadowed hull (run349) | 2 | Completed 2026-09-27: **bullets fixed** (user); `--bolt-far-show 1` looks better than 0.5 -> **W default 1** (the user's decision; 0.5 had been chosen to match the bolt's measured retention over empty space so a bolt crossing a silhouette stayed continuous); no dark spots on the self-shadowed hull reported; both sessions exited clean |
| 93 A | Run93 (single-copy bullets on by default; opt-in rotation-aware TAA motion weight): launch 1 defaults with F8 firing at a station / rest / pan / firing at a second station (run346); launch 2 `--taa-history-weight 0.9 --taa-motion-weight-rotation 0.7`, rest + pan (run347) | 2 | Completed 2026-09-26: **bullets still vanish over distant stations** (user, both scenes): the single-copy rule works (`bolt_copies early_dropped=2 late=2` every firing frame; before TAA the bolt adds as much over the station as over space; the late copy passes the depth test) but the **TAA resolve erases the bolt**: its pixels carry the far station's depth, so the far/thin history weights keep ~0.99 there vs 0.39-0.91 over space (run346-run93a-bolts/; design note for the fix in progress); **rotation weight**: engaged (policy 2, pan 8.5-8.8 px/frame, applied weight 0.70 vs 0.85), +34 % pan sharpness on the one station visible in both pans, but the user's reference station left the screen in run347's pan; the user noticed shimmer under the pan and **keeps it off** ("better blurring than shimmering"); both sessions exited clean |
| 92 A | Run92 (TAA history weight default 0.85, `bolt_copy` capture diagnostic): launch 1 default with `--taa-debug`, F8 at rest / mid-pan / while firing (run341); launch 2 `--taa-history-weight 0.9` (run342); launch 3 `--taa-history-weight 0.8` (run343), rest + pan F8 each | 3 | Completed 2026-09-26: **0.85 accepted as the default** (user: with 0.8 the adjacent station shimmers at rest and under the pan; 0.85 and 0.9 look similar, shimmer less noticeable); **bullet copies proven identical**: 32 `bolt_copy` rows over 8 capture frames (2098-2105), the early draws 6/7 and the late draws 211-214 of each of the two bullet buffers carry the same `hash`, `qsum` and `bbox` in 8 of 8 frames (revisions n / n+1) -> the fix drops the early copy (single-copy rule, in implementation); all three sessions `session_end ... exception=0 dropped=0` |
| 91 A | Run91 (developer options trimmed with `--draw-trace`; hotkeys removed except F8 under `--debug`; `--capture-start`/`--capture-delay` gone; the `x3m.ini` settings file; plus everything since Run 88 A: logging tiers, option cleanup, single shadow map removed, cascade 3 at 4096): launch 1 default stand `x3run --direct --debug --perf` (run338), launch 2 player mode `x3run --direct --config --debug --perf` with F8 captures (run339), optional `--no-shadow-cascades` not flown | 2 | Completed 2026-09-26: "everything looks okay" (user), Run91 accepted; run338 `config_open source=bare`, 20,416 frames, `session_end ... exception=0 dropped=0 filter=ours`; run339 `config_open file=C:\X3\x3m.ini source=game keys=0` (394 lines, 11.4 ms), 121 `proxy_options` values `@default`, `log_open previous=renamed`, three F8 captures (frames 2242, 6650, 7102), 11,202 frames, no drops, exit clean; two pre-existing notes raised by the user and triaged afterwards: distant stations/asteroids drawn over the player's bullets (captures in run339), stations blurry under a pan (no capture) |
| 90 A | Run90 (logging tiers: `--debug` / `--perf`, `x3m.log`; obsolete options and code removed; `--taa-k` / `--taa-sentinel` removed; plus Run89's single-map removal and cascade 3 at 4096): one launch with `x3run --direct --debug --perf`, a second short launch for the log rotation and exit, an optional `--no-shadow-cascades` launch | 0 | Superseded 2026-09-26 by Run 91 A before it was flown (Run91 installed the option trim, the hotkey removal and the `x3m.ini` file the same morning); its checks are folded into Run 91 A |


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

**Run 100 A (queued 2026-09-28; Run100 = DLL `20680908…` from a627f5b5, installed 05:35; the lock in its section-12 form,
still off by default).** Same stand as run353/run354 (the far military outpost, the far spacedock next to it, the
equipment dock), 1920x1080 is fine (say the resolution). Two launches, this time both with `--taa-debug` so the
resolved frames and the lock lane are dumped at each F8:

```
./x3run --direct --debug --perf --taa-debug
./x3run --direct --debug --perf --taa-debug --taa-luma-lock 16
```

In each: stop at the stand, F8 at rest; pan at the same speed in both launches (a slow steady pan, about a quarter turn
in two seconds) with F8 mid-pan; stop and F8 two seconds after the pan ends. Judge the lock launch against the default
one: (1) the far spacedock's struts under the pan: quiet, or still shimmering; (2) the outpost's plates under the pan:
still sharp; (3) softness that outlives the pan (a lock left behind); (4) very bright struts flickering under the pan
(the lock can toggle on a high-contrast strut). Rows expected in the lock launch: `motion_output_taa_luma_lock ...
configured=1 gate=camera`, `session_end`, no `Reset`. Triage measures the locked share and flicker on the spacedock
from the dumps.

Run 100 A is the only queued run. Completed instructions for Runs 73-99 are in the [archive](../archive/user-runs-completed.md).
