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

**Run 67 (queued 2026-09-22): Run67 DLL `621cad63…` from `1b4d745c`. Three sessions; all dry-runs verified. `--capture-delay 300` stays in every command.**

Session A, SETA smear and pan, strict sky history with the new band term (default 3 px). (1) Same SETA approach at an off-centre station: the trailing band beside the silhouette should now be gone as well; if a faint one remains, try `--taa-sky-history-band-px 2`. (2) A slow pan past a static station against bright nebula: the sky border must stay smooth (no crawl, no flicker). (3) New thing to watch: a close flyby or a docking approach at normal speed, where a nearby station slides across the sky: its one-pixel sky border is now current-only under strict; say whether you notice any aliasing there. Delayed F8 on the SETA approach and on the flyby. If all three are clean, strict becomes the default.

```sh
env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 X3M_MOTION_FRAME_LOG=1 /Users/asvetl/x3-mod/x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --light-map-far-fade 80,220 --motion-rt-mode lazy --frame-end-stride 1 --taa-thin-region 0.97 --sun-occlusion --sun-occlusion-log --taa-debug --capture-start 999999 --capture-frames 32 --capture-delay 300 --taa-sky-history strict
```

Session B, the frame-time stand with the draw accounting. Same stand as run248, `--lod-scale 0.5` plus `--object-bounds-log`: one delayed F8 at the stand, one at the busy view. The log then carries every routed draw's screen box, and `python3 tools/analysis/draw_accounting.py /tmp/x3-bottleX3-run<N>` buckets the 367 draws into off-screen, occluded, tiny and visible. Nothing to judge visually.

```sh
env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 X3M_MOTION_FRAME_LOG=1 /Users/asvetl/x3-mod/x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --light-map-far-fade 80,220 --motion-rt-mode lazy --frame-end-stride 1 --taa-thin-region 0.97 --volumetric-fog 0.02 --volumetric-fog-cards replace --volumetric-fog-range stored --volumetric-fog-timing --capture-start 999999 --capture-frames 8 --capture-delay 300 --cull-small-parts 4 --frame-timing --frame-phases --fps-overlay --lod-scale 0.5 --object-bounds-log
```

Session C, fog shadow pass and cascade footprint A/B (fog on, single look, vanilla LOD). At a shaft-rich spot (a station between you and the sun in fog): shafts should have soft edges instead of hard beams, no seam at the cascade boundaries, sharper shafts close to the ship; watch for any banding at a fixed distance and note the fps overlay against the same spot with `--fog-shadow-pass off`. The cascade footprint at 8 drops casters smaller than about 110 m from the fourth cascade and 560 m from the farthest: any shadow that disappears at distance is worth a note. Delayed F8 at the shaft spot.

```sh
env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 X3M_MOTION_FRAME_LOG=1 /Users/asvetl/x3-mod/x3run --direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --voice-decoder /tmp/x3-wma-plugin-v4 --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --taa-far-stabiliser 0.985 --light-map-far-fade 80,220 --motion-rt-mode lazy --frame-end-stride 1 --taa-thin-region 0.97 --volumetric-fog 0.02 --volumetric-fog-cards replace --volumetric-fog-range stored --volumetric-fog-timing --capture-start 999999 --capture-frames 8 --capture-delay 300 --cull-small-parts 4 --frame-timing --frame-phases --fps-overlay --fog-shadow-pass on --shadow-cascade-min-footprint 8
```

Run 67 is the only queued run. Run 66 returned **run244** (A: strict removes 42 % of the SETA dark sky, the near band 63 %; residual band fixed by the band term; blur is pre-existing history reprojection) and **run245-run248** (B: lod-scale 0.25/0.5 both ~275 draws vs 394 vanilla; the heavy station body reaches LOD 3 at 0.5; user: 0.5 helps with a few pops, default pending; run248 accounting: 367 draws, 19 % tiny, no second pass).
