# Outstanding user gameplay runs

Archive with `python3 tools/analysis/archive_user_runs.py`.

Updated 2026-09-21 (Run56 accepted for media stability; Run57 station-flash correction accepted; fog range remains under investigation). Run 17 crypto acceptance and the first-person/chase
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
| 55 | First-person fog correction and crash diagnosis | 1 | Run199: first-person fog fixed by user report; F8 taken in first person. Crash recurred inside LAVVideo; user accepts ID2 omission. Superseded by Run56. [Archived instructions](../archive/run55-completed-2026-09-21.md). |
| 56 | ID2 video omission | 1 | Run200: user confirms no crash or media-related stutter; omission accepted. New fog-range and station-lighting observations remain separate. [Archived instructions](../archive/run56-completed-2026-09-21.md). |
| 57 A/B/C | Station-flash threshold comparison | 3 | Run203/run204 returned with flashes; B disables missing-key cuts but leaves median-motion cuts. C completed as run205; both heuristics default-off accepted. [Archive](../archive/run57ab-completed-2026-09-21.md). |
| 67 A/B/C | SETA smear with strict + band term (A), draw accounting at the stand (B), fog shadow pass and caster footprint 8 (C) | 3 | Completed: run249 / run250 / run251; C second sector: run253. A: band term clears the fast border, a 3-12 px hull share remains, strict stays opt-in ([ledger](temporal-resolve.md#run-249-strict--band-term-in-flight-2026-09-22)). B: 391 / 416 draws, cullable buckets 1.0-1.3 ms, no next cull ([note](../architecture/engine-frame-time.md#run-250-draw-accounting-at-the-stand-and-the-busy-view-2026-09-22)). C: shadow pass ran without failures, look and cost need an in-session A/B ([ledger](volumetric-fog.md#run-251-fog-shadow-pass-in-flight-no-ab-2026-09-22)); footprint 8 drops no visible shadow, accepted as default ([ledger](directional-shadows.md#run-251-minimum-caster-footprint-p8-in-flight-2026-09-22)). [Instructions archive](../archive/user-runs-completed.md#run-67-completed-2026-09-22-run249-run250-run251-run253). |
| 72 A/B | Run72 defaults (box cull, forward reticle, 1.05 boom, pause key-only, decoder discovery, identity without hash) and the music trace (A), then --music-keep (B) | 2 | A completed 2026-09-23 (run270): reticle, camera, pause and collisions accepted; alt-tab same-id replay confirmed 7/7, save not traced; bolts faint in third person (open, [ledger](screen-emission.md#run-270-run-72-a-2026-09-23-bolt-visibility-in-third-person-open)); 1080p CPU passes unchanged, proxy 6.8 % ([note](../architecture/engine-frame-time.md#run-270-first-flight-at-19201080-run-72-a-2026-09-23)). B queued. DLL `c17792a9…` ([install](../verification/results/run72-candidate-install.json)). |
| 71 A | Merged-LOD atlas overlay from the LOD 0 meshes at the stand (Run70 DLL) | 1 | Completed 2026-09-23: run269 accepted, no visible transition; coarse ships 2 draws / outpost 4 with the four atlases bound ([note](../architecture/engine-frame-time.md#run-269-lod-0-atlas-overlay-accepted-2026-09-23)). |
| 70 A/B/C | SETA hull blur with --taa-motion-weight (A); fog dust motes with the Ctrl+Alt+F11 A/B (B); merged-LOD atlas overlay at the stand (C) | 3 | Run69 DLL `70abe438…` from `5a11ad00`. A: run262 (0.8) and run263 (0.7) accepted, default 0.7,2,8 ([ledger](temporal-resolve.md#run-262-motion-weight-08-2-8-in-flight-accepted-2026-09-23)). B: run264 + B2, accepted at 1300,3 / MAX_PX 8, default in the next DLL ([ledger](volumetric-fog.md#run-264-fog-dust-motes-in-flight-2026-09-23)). C: run265 (specular placeholder) and C2 run268 (specular fixed; remaining step = light-atlas mip bleed on exhausts/windows, fix in progress) ([note](../architecture/engine-frame-time.md#run-268-atlas-overlay-with-the-specular-atlas-2026-09-23)). |
| 69 A/B/C/D | Merged-LOD pilot overlay (addon/05.cat, installed DLL): two-group collapse (A), glow collapse (B), compact placement at 80 / 150 px (C), area-ranked light maps + synthesized material (D) | 4 | A: run257, engine glows lost ([note](../architecture/engine-frame-time.md#run-257-merged-lod-pilot-in-flight-2026-09-23)). B: run258, glows back. C: run259/run260, 80/150 acceptable, lighting loss = light maps and diffuse strength ([note](../architecture/engine-frame-time.md#run-259--260-compact-placement-at-80--150-px-the-lighting-question-2026-09-23)). D: run261, less visible, outpost sun 94 % / non-sun 73 % of fine ([note](../architecture/engine-frame-time.md#run-261-area-ranked-light-maps-and-the-synthesized-material-2026-09-23)). |
| 68 A/B/C | SETA exit reset; stand census ladder/body/alpha boxes; fog A/B toggle | 3 | Completed: run254 / run255 / run256. A: SETA smear gone by the numbers (3-12 px share 7 % / 6 % vs 27 % / 34 %), strict + band + exit become defaults; station blur under SETA is history resample softening ([ledger](temporal-resolve.md#run-254-exit-reset-in-flight-accepted-2026-09-23)). B: 466 draws at the stand, twelve bodies at LOD 0 carry ~390; the pilot needs the pad placement, T_pad 50/100 ([note](../architecture/engine-frame-time.md#run-255-stand-census-with-ladder-body-and-screen-size-2026-09-23)). C: the toggle fired 27 times without a notice by design; no visible or median-cost difference, pass stays off ([ledger](volumetric-fog.md#run-256-fog-shadow-pass-ab-toggle-in-flight-2026-09-23)). |

Completed run commands and instructions are preserved in
[the completed-run archive](../archive/user-runs-completed.md) and the
[run 48 archive](../archive/run48-completed-2026-09-20.md); they are provenance,
not rerun requests.


<a id="51-media-retry-counter--same-view-longer-diagnostic-interval"></a>
<a id="53-spatial-fog-and-moving-lattice-state--ready-for-flight"></a>
Run51/53 instructions are [archived](../archive/run53-completed-2026-09-20.md); they are not rerun requests.

Run 72 B is the only queued run (A returned run270). Run 71 returned run269 (A, accepted).

**Run 72 (queued 2026-09-23): Run72 DLL `c17792a9…` from `dcf3728b` ([qualification](../verification/results/run72-candidate-qualification.json)). New since Run70, all default on modded launches: `--collide-box-cull` (opt out `--no-collide-box-cull`), chase HUD reticle anchored `forward`, chase boom 1.05× (17 % longer), `--pause-key-only` (only the Pause key or a click ends the flight pause; `--pause-key CODE` for a rebound key), the voice decoder discovered from the repository copy (`--voice-decoder none` opts out), the executable identity without a file hash (the identity line now logs `exe_laa=` and `exe_max_app=`). Opt-in this round: `--music-trace` (logs the engine's music stop/play calls) and `--music-keep` (the fix; off until A's trace confirms the same-track replay). The fog-families file is not installed (vanilla needs none); the 14 compiled profiles apply.**

Session A, the stand, one launch, everything default plus `--music-trace`:

1. Fly the stand as usual for a minute and note the chase view: does a corvette (M6) fit fully now, and is the reticle on the true forward point (it moves only a few px at the 0.5° pitch)?
2. Pause with the Pause key: press a few ordinary keys, then alt-tab out and back. The box must stay up; then click or press Pause to resume. Report what unpaused it.
3. With sector music playing: alt-tab out and back, save the game, pause and unpause. The music is expected to restart each time (vanilla behaviour; the fix is off) and the log records the calls.
4. Keep an eye on collisions (box cull is on): anything odd when close to stations or in asteroid fields.
5. Two F8 bursts anywhere; no held-view pair needed.

```sh
env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 X3M_MOTION_FRAME_LOG=1 /Users/asvetl/x3-mod/x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --light-map-far-fade 80,220 --motion-rt-mode lazy --frame-end-stride 1 --taa-thin-region 0.97 --volumetric-fog 0.02 --volumetric-fog-cards replace --volumetric-fog-range stored --volumetric-fog-timing --capture-start 999999 --capture-frames 8 --capture-delay 300 --cull-small-parts 4 --frame-timing --frame-phases --object-bounds-log --cull-census --music-trace
```

Session B (ready): the same command with `--music-keep` added (keep `--music-trace`). Steps: (1) with sector music playing, alt-tab out and back, then save the game at a moment you note (say "saved at about N minutes"), then pause and unpause; report whether the track continued at its position each time and whether a sector change still switches tracks. (2) Bolts: in the corvette in third person, hold fire continuously and take one F8 burst while firing; then launch once more with `--cull-small-parts 0` added and repeat that burst (this rules the small-part cull in or out). Report whether the bolts look any different between the two launches.

