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
| 50 | Argon Prime media-retry attribution | 0 | Completed: run186. Two failed media-ID-2 constructions account for almost all of their 359/408 ms frames; later retries follow the 30-second cadence. Other first-view stalls remain separately scoped. [Instructions archive](../archive/run49-50-completed-2026-09-20.md#50-argon-prime-first-view-stutters--existing-media-trace), [findings](media-cues.md#run50-periodic-retries-directly-explain-argon-freezes-2026-09-20). |
| 51 | Longer media retry counter | 0 | Not flown; superseded by Run54 replacement-media verification. [Archived instructions](../archive/run53-completed-2026-09-20.md#51-media-retry-counter--same-view-longer-diagnostic-interval). |
| 52 | Busy-station attribution and lazy-RT counter | 0 | Completed: A run187, B run188, C run189. Matched 478-draw separate-session B/C medians were 19.70 / 18.90 ms; lazy accepted as launcher default, no new engine patch justified. [Instructions archive](../archive/run52-completed-2026-09-20.md), [results](motion-output.md#run52-lazy-render-target-binding-accepted-as-launcher-default-2026-09-20). |
| 53 | Spatial fog and lattice state | 0 | Completed: A run193, B run194. Fog preference 1.50×; camera-cut native-card flicker and observer reference interference reproduced and corrected. Visual acceptance remains open: fog follow-up is Run55; lattice Session B returned run201 and is under analysis. [Archive](../archive/run53-completed-2026-09-20.md), [lattice findings](../architecture/taa-lattice-crawl.md#27-bound-observer-reference-callbacks-and-device-lifetime-2026-09-20). |
| 54 A | Media and expanded fog/shafts | 3 | Analysed: run195/run196/run197 crashed; Run197 first-person F8 exposes camera tolerance refusal. Session A superseded by Run55; B returned run201 and is under analysis. |
| 54 B | Guarded lattice state observation | 1 | Run201 received; three capture bursts and state packets are under analysis. No repeat flight requested. [Archived instructions](../archive/run54b-completed-2026-09-21.md). |
| 55 | First-person fog correction and crash diagnosis | 1 | Run199: first-person fog fixed by user report; F8 taken in first person. Crash recurred inside LAVVideo; user accepts ID2 omission. Superseded by Run56. [Archived instructions](../archive/run55-completed-2026-09-21.md). |
| 56 | ID2 video omission | 1 | Run200: user confirms no crash or media-related stutter; omission accepted. New fog-range and station-lighting observations remain separate. [Archived instructions](../archive/run56-completed-2026-09-21.md). |
| 57 A/B/C | Station-flash threshold comparison | 3 | Run203/run204 returned with flashes; B disables missing-key cuts but leaves median-motion cuts. C completed as run205; both heuristics default-off accepted. [Archive](../archive/run57ab-completed-2026-09-21.md). |

Completed run commands and instructions are preserved in
[the completed-run archive](../archive/user-runs-completed.md) and the
[run 48 archive](../archive/run48-completed-2026-09-20.md); they are provenance,
not rerun requests.


<a id="51-media-retry-counter--same-view-longer-diagnostic-interval"></a>
<a id="53-spatial-fog-and-moving-lattice-state--ready-for-flight"></a>
Run51/53 instructions are [archived](../archive/run53-completed-2026-09-20.md); they are not rerun requests.

**Run 66 (queued 2026-09-22): Run66 DLL `1f9a85f5…` from `387d5cd9`. Two sessions; both dry-runs verified. Launcher defaults since Run 65: widening 4 and the emissive vote apply by themselves on HDR sessions, core dimming is the default with `--sun-occlusion`, the fog has one look (no look flag), `--capture-delay 300` stays in both commands.**

Session A, SETA smear and sun (fog off), with `--taa-sky-history strict`. (1) The black smear: approach a station that sits off-centre under SETA (press F8, re-engage SETA, let the delayed burst fire while the station slides across the screen): the dark trail on the nebula should be gone. (2) The review's one risk with strict: a slow pan past a station against bright nebula, camera otherwise still: watch the two-pixel ring of sky just outside the silhouette for any flicker or outline; F8 during the pan. (3) Sun: nothing should have changed from Run 65 (core dimming is now the default). If both (1) and (2) are clean, strict becomes the default.

```sh
env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 X3M_MOTION_FRAME_LOG=1 /Users/asvetl/x3-mod/x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --light-map-far-fade 80,220 --motion-rt-mode lazy --frame-end-stride 1 --taa-thin-region 0.97 --sun-occlusion --sun-occlusion-log --taa-debug --capture-start 999999 --capture-frames 32 --capture-delay 300 --taa-sky-history strict
```

Session B, the frame-time stand (fog on, single look, widening off for the fog comparison). Same stand as run239/run240. Three readings of the fps overlay and one delayed F8 each: (a) as given below with `--lod-scale 0.25`; (b) the same with `--lod-scale 0.5`; (c) the same without `--lod-scale` (vanilla ladder) but keeping `--cull-small-parts 4`. Report fps and, if you can, how the distant stations look at 0.25 (they should drop to their coarse meshes much sooner; any visible pop or missing detail matters). The draw count per frame is what decides between fixing the LOD thresholds and generating coarse LOD bodies (docs/architecture/merged-lod-feasibility.md).

```sh
env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 X3M_MOTION_FRAME_LOG=1 /Users/asvetl/x3-mod/x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --light-map-far-fade 80,220 --motion-rt-mode lazy --frame-end-stride 1 --taa-thin-region 0.97 --volumetric-fog 0.02 --volumetric-fog-cards replace --volumetric-fog-range stored --volumetric-fog-timing --capture-start 999999 --capture-frames 8 --capture-delay 300 --lod-scale 0.25 --cull-small-parts 4 --frame-timing --frame-phases --fps-overlay
```

Run 66 is the only queued run. Run 65 returned **run235** (A: sun accepted, core dimming liked; SETA smear captured under the delayed burst: cause is the far-plane history tolerance, fixed behind `--taa-sky-history strict`), **run236/run237** (B: widening 3 and 4 accepted, 4 chosen as default with the emissive vote), **run238/run239/run240/run242/run243** (C: fog fine; the ~50 fps stand is 448 engine draws from 58 nodes, 400 at LOD 0; `--cull-small-parts 4` removed ~50 draws; run243's `--lod-scale 0.5` was refused by the Run65 DLL).
