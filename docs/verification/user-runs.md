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
| 51 | Longer media retry counter | 0 | Not flown; superseded by Run54 replacement-media verification. [Archived instructions](../archive/run53-completed-2026-09-20.md#51-media-retry-counter--same-view-longer-diagnostic-interval). |
| 52 | Busy-station attribution and lazy-RT counter | 0 | Completed: A run187, B run188, C run189. Matched 478-draw separate-session B/C medians were 19.70 / 18.90 ms; lazy accepted as launcher default, no new engine patch justified. [Instructions archive](../archive/run52-completed-2026-09-20.md), [results](motion-output.md#run52-lazy-render-target-binding-accepted-as-launcher-default-2026-09-20). |
| 53 | Spatial fog and lattice state | 0 | Completed: A run193, B run194. Fog preference 1.50×; camera-cut native-card flicker and observer reference interference reproduced and corrected. Visual acceptance remains open: fog follow-up is Run55; lattice Session B returned run201 and is under analysis. [Archive](../archive/run53-completed-2026-09-20.md), [lattice findings](../architecture/taa-lattice-crawl.md#27-bound-observer-reference-callbacks-and-device-lifetime-2026-09-20). |
| 54 A | Media and expanded fog/shafts | 3 | Analysed: run195/run196/run197 crashed; Run197 first-person F8 exposes camera tolerance refusal. Session A superseded by Run55; B returned run201 and is under analysis. |
| 54 B | Guarded lattice state observation | 1 | Run201 received; three capture bursts and state packets are under analysis. No repeat flight requested. [Archived instructions](../archive/run54b-completed-2026-09-21.md). |
| 55 | First-person fog correction and crash diagnosis | 1 | Run199: first-person fog fixed by user report; F8 taken in first person. Crash recurred inside LAVVideo; user accepts ID2 omission. Superseded by Run56. [Archived instructions](../archive/run55-completed-2026-09-21.md). |
| 56 | ID2 video omission | 1 | Run200: user confirms no crash or media-related stutter; omission accepted. New fog-range and station-lighting observations remain separate. [Archived instructions](../archive/run56-completed-2026-09-21.md). |
| 57 A/B/C | Station-flash threshold comparison | 3 | Run203/run204 returned with flashes; B disables missing-key cuts but leaves median-motion cuts. C completed as run205; both heuristics default-off accepted. [Archive](../archive/run57ab-completed-2026-09-21.md). |
| 67 A/B/C | SETA smear with strict + band term (A), draw accounting at the stand (B), fog shadow pass and caster footprint 8 (C) | 3 | Completed: run249 / run250 / run251; C second sector: run253. A: band term clears the fast border, a 3-12 px hull share remains, strict stays opt-in ([ledger](temporal-resolve.md#run-249-strict--band-term-in-flight-2026-09-22)). B: 391 / 416 draws, cullable buckets 1.0-1.3 ms, no next cull ([note](../architecture/engine-frame-time.md#run-250-draw-accounting-at-the-stand-and-the-busy-view-2026-09-22)). C: shadow pass ran without failures, look and cost need an in-session A/B ([ledger](volumetric-fog.md#run-251-fog-shadow-pass-in-flight-no-ab-2026-09-22)); footprint 8 drops no visible shadow, accepted as default ([ledger](directional-shadows.md#run-251-minimum-caster-footprint-p8-in-flight-2026-09-22)). [Instructions archive](../archive/user-runs-completed.md#run-67-completed-2026-09-22-run249-run250-run251-run253). |

Completed run commands and instructions are preserved in
[the completed-run archive](../archive/user-runs-completed.md) and the
[run 48 archive](../archive/run48-completed-2026-09-20.md); they are provenance,
not rerun requests.


<a id="51-media-retry-counter--same-view-longer-diagnostic-interval"></a>
<a id="53-spatial-fog-and-moving-lattice-state--ready-for-flight"></a>
Run51/53 instructions are [archived](../archive/run53-completed-2026-09-20.md); they are not rerun requests.

Run 67 returned **run249** (A: SETA still smears a little, normal speed and pan clean; the residual is a hull share carried outward in the 3-12 px band, a design question), **run250** (B: draw accounting at the stand, no cull justified; the alpha-tested draws need one flight with the extended bounds log), **run251** (C: fog shadow pass "softer?", no A/B yet; footprint 8 without a missing shadow) and **run253** (C, second sector: no shadow popping; footprint 8 accepted as the launcher default).
