# Project status

Updated 2026-09-20 (Run53 installed and queued; spatial fog/lattice diagnostic; media and offline engine work continue). This is the
short current status; the session handoff is [handoff-2026-09-20.md](handoff-2026-09-20.md).
Older session sections are in
[archive/status-sessions-through-2026-09-19.md](archive/status-sessions-through-2026-09-19.md),
the earlier narrative in [status history 2026-09-14](archive/status-history-2026-09-14.md) and
[status history 2026-09-13](archive/status-history-2026-09-13.md).
Read history only for a relevant unresolved question. The
[goal checklist](goals.md), [run queue](verification/user-runs.md) and
[original objective](user-objective.md) retain the full scope.

## Installed build

Bottle **X3**, **CrossOver Preview.app**. Run53 candidate DLL SHA-256:
`36a6886456f40b4be6c5c8dd8344f6d4cb2e1fc191e6b137ea5530df9a572468`
(26,948,773 bytes), built once from clean committed source `fc52a432` and retained
at `/tmp/x3-run53-candidate/d3d9.dll`. It replaces uniform fog with the spatial
family renderer and adds the opt-in post-route lattice state diagnostic. It
contains no new media, collision or FOV optimization. Run53 flight is queued;
do not merge further production changes to main during qualification.

The [candidate record](../verification/results/run53-candidate-qualification.json)
binds 2,414 full host tests plus three bridge checks, linked audit 95 roots /
540 reachable / zero violations, actual spatial renderer/route/timing evidence,
and two selected actual-DLL HDR/TAA smoke cases (43/83 checks). A combined affected
launch `--dry-run` passes; no game was launched. These do not establish visual
acceptance or native Windows execution.

The [install record](../verification/results/run53-candidate-install.json)
verifies installed bytes and unchanged EXE/bottle configuration. One rollback
DLL and manifest preserve Run52 under `/tmp/x3-run53-candidate/rollback`.
Installation used `python3 tools/manage.py install --bottle X3 --dll-source
/tmp/x3-run53-candidate/d3d9.dll`. The prior accepted Run52 evidence remains in
its [qualification record](../verification/results/submission-attribution-qualification-2026-09-20.json).

Launcher defaults now use **lazy render-target binding** with motion output
after [Run52 acceptance](verification/motion-output.md#run52-lazy-render-target-binding-accepted-as-launcher-default-2026-09-20); explicit `--motion-rt-mode perdraw` remains available.
Launcher defaults retain original hull shading, `--cull-small-parts 2` scope
`all`, `--collide-sat-sse2`, `--collide-memo`, and the user-selected light-map
fade **80,220** (the optional third value defaults to 1, so the stored setting
is `80,220,1`; `--no-light-map-far-fade` disables it).
New opt-in features: `--volumetric-fog-cards replace`, read-only
`--sector-background`, `--light-phases`, and `--collide-query-phases`.
Fog now uses the validated engine family to select bluewell/green spatial
profiles; manual strength 0.02 is a density multiplier of one. Other families
retain native cards. Replacement remains explicit and fog is off by default. Far stabiliser 0.985 and thin region 0.97 remain explicit options:
stationary improvement is accepted, moving-lattice quality remains open.
Native Windows runtime remains unverified.

## Current state (2026-09-20)

Current away-session work is isolated from the qualifying candidate. Spatial fog
now has fourteen source-backed profiles and current-frame directional shafts on
`feat/fog-families-shafts-integration` (`7e698ec6`); the combined GPU run passes
145 checks and full host discovery passes 2,431 tests (two skipped). Reviewed
paired shaft measurements add about 0.11–0.12 ms on captured workloads and pass
the existing transaction-cost limits. A clean full DLL from `a13ae8a7` is retained
uninstalled at `/tmp/x3-fog-families-shafts-dll-v1/build/d3d9.dll` (SHA-256
`ca02a6c4abd8693a18376853240600e4ca0d485d017bf6d7c3026724ffffbd1d`);
linked audit and two selected actual-DLL HDR/TAA smoke cases pass. The queued
fog command passes dry-run. Flight appearance remains unaccepted.
Media checkpoint `ae0743a0` now qualifies two concurrent workers with the owned
clock and actual rendered textures: 35 exact captures and 4,241 clock transactions.
Cold creation and first-use rendering still stall the fixture; this is not a
no-stutter claim. Shared production transport extraction is active. Reviewed
surface leases (`f13acc59`, 100 runtime checks) and bounded session/adapter state
(`4aaae520`, 202 host checks) are committed on isolated branches, with admission
disabled. The corrected presentation-gate fixture passes 576 runtime checks; the initial
22 failures were requested MXCSR status bits that FEX did not materialize, also
in ungated baselines. Exact represented state passes; nonzero status coverage
remains unverified. The reviewed prerequisites are being combined with fog/shafts
on `feat/media-production-integration`; three affected host tests pass.
Production worker extraction cross-compiles and is being connected to its actual
clock/texture fixture. App-local package deployment is reviewed and committed
(`c63f8cbd`, 45 affected host tests); the configuration reader is also committed
(`e2fabd5e`) after 150 actual Win32 file-adapter checks passed. The extracted worker now passes actual end-of-video playback twice with retained
configuration pins (12 exact frames/readbacks and fresh graph completion for both
sessions). Its concurrent playback run passes functional checks but lacks one
pause-before-seek scheduling witness; that fixture coverage is being repaired. The canonical loop
API now distinguishes temporary queue pressure from permanent refusal
(`156c8b63`, 444 host checks).
The startup boundary now includes synchronous observer installation before background
preparation (`23092198`): eleven actual x86 fixture modes pass 819 checks. The engine
consumer checkpoint (`9ac49b48`) passes 5,055 x86 checks across 24 sites and 11 return
guards. Both use authored engine context; actual integration/readiness stays open.
The Services coordinator is reviewed and committed (`bebc9edd`, 1,783 host checks);
actual combined playback remains to qualify. Destination observers pass 2,321 x86
and 142 D3D checks; review is finishing a host-side device-publication tombstone
correction before integration.
Enabled media repair remains unfinished. The owning
media and ownership ledgers retain the scoped evidence.
Collision optimization is **paused by user request**. The reviewed engine/lattice
audit adds no tracing before interpreting the existing queued lattice packet;
[the owning decision](architecture/taa-lattice-crawl.md#25-next-flight-state-decision-and-reopening-gate-2026-09-20)
keeps moving crawl open. No additional user capture is requested now.

Run52 A/B/C (run187–189) is analyzed and independently reviewed. Matching
478-draw separate-session B/C intervals had medians of 19.70 / 18.90 ms, consistent with the user
report and no visible issues. Lazy is accepted; the corrected attribution
does not justify another engine patch. No repeat timing flight is queued.

Run 47 is read; **run 48 A/B/C are reported** ([completed run48](archive/run48-completed-2026-09-20.md)):
A lattice arm + distant station (baseline vs the intended defaults), B Argon Prime
shadows and fog, C `--submit-phases` at the busy station. The
[handoff](handoff-2026-09-20.md) holds the state of every track, the decisions owed
after run 48, the user's preferences and the housekeeping list; the
[goals table](goals.md) is current. Run 48 A evidence is recorded in the [motion-output ledger](verification/motion-output.md):
stationary improvement confirmed; moving-arm crawl remains open. The moving-lattice
replay is complete and found no safe fix to promote. Run180 fog/shadows and run181
submission findings are recorded in their owning notes. Run49 A (run183 diagnostic / run184 counter) now covers busy-station, new-game
Argon Prime and corvette saves, in that order. Triage finds about 51 FPS in the
phase-off busy plateau; R7 light selection is too small to optimize. Argon
stalls persist with phase diagnostics off and correlate with media-backend
errors. [Run50](verification/media-cues.md#run50-periodic-retries-directly-explain-argon-freezes-2026-09-20) now directly attributes two periodic stalls to failed ID2 media
construction; three later retries follow the same pattern. First-view media
failures are a separate caller path. The optional [run51 counter](verification/user-runs.md#51-media-retry-counter--same-view-longer-diagnostic-interval) is ready; retry defaults and decoder remain unchanged. A [standalone media fixture](verification/media-cues.md#standalone-id2-playback-boundary-2026-09-20) reproduces v4 open failure; fixture-only v5 opens but blocks at
zero-seek. Skipping that call delivers six distinct frames to a diagnostic D3D
texture with clean shutdown; correct seeking and game integration remain open.
No game decoder change. A [native same-stack control](verification/media-cues.md#native-same-v5-pipeline-proves-preroll-reaches-the-wine-destination-2026-09-20) proves the Wine graph presents a 9.4-second preroll frame after a ten-second seek; an explicit portable-provider fixture loads and connects successfully but its ordinary constructor hangs at Stop. The [selected-allocator terminal diagnostic](verification/media-cues.md#selected-allocator-decommit-unblocks-terminal-cleanup-2026-09-20) now shuts down cleanly. The [deterministic transport diagnostic](verification/media-cues.md#deterministic-transport-zero-passes-nonzero-content-fails-2026-09-20) also restarts at zero with exact frame equality, but repeated ten-second seeks return content 120 ms later than the sequential reference. A separately qualified lossless derived-source counter now passes exact sequential comparison and repeated 0/10/0/10 seeking ([checkpoint](verification/media-cues.md#derived-source-exact-seeking-checkpoint-2026-09-20)). Millisecond boundaries, pending transitions, engine loops, latency and game integration remain open; no game playback qualification.

The [owned collision query oracle](reverse-engineering/sector-collide.md#owned-original-query-parity-checkpoint-2026-09-20) passes 3,118 frozen queries with zero result/ABI mismatches after correcting a truncated fixture constant. [Paired tree-layout timing](reverse-engineering/sector-collide.md#owned-physical-layout-timing-no-qualified-gain-2026-09-20) found no consistent gain (aggregate medians -0.22% to +0.18%) and is closed. A [targeted retirement-scheduling pass](reverse-engineering/sector-collide.md#retirement-scheduling-follow-up-precise-stop-barrier-2026-09-20) still could not establish the live copy lifetime; collision snapshot work stops at that concrete ownership boundary, with no new flight requested. Historical memo/query-phase fixtures shared that extraction limitation; [corrected fixtures now pass](reverse-engineering/sector-collide.md#shared-collision-extraction-repaired-and-requalified-2026-09-20). Installed collision code and the separate builder qualification are unaffected. Live snapshot lifetime remains unproved. Moving collision is
about 98% of printed instrumented query time inside descent in the expensive interval, so query setup is not
the missing lever. Details are in the [frame-time note](architecture/engine-frame-time.md#run49-a-three-scene-diagnosticcounter-flight-2026-09-20) and
[collision note](reverse-engineering/sector-collide.md#run49-a-moving-query-cost-is-inside-descent-2026-09-20).
Run181 did not close the engine/proxy or moving-collision investigations.

Card replacement with card-only state validation is reviewed and committed
(`cd004f35`), preserving the normal setter path. The read-only sector diagnostic
and the reviewed [239-sector fog census](reverse-engineering/sector-fog-census.md)
(`a104f376`) are complete. Run185 validates the reader
in its observed sectors. The spatial redesign supersedes the old homogeneous
0.01/0.05 family anchors with authored density/occupancy profiles and a manual
multiplier; card count alone does not set density.

Light-selection and collision-query timers are integrated, reviewed and committed
(`9fa4da5a`): 2,093 light-timer checks with zero failures; 142 collision-timer
checks and 675 queries with zero differences. Run49 A combined them with
the existing loop/game/residual phases. Qualification and installation are complete. [Run49](archive/run49-50-completed-2026-09-20.md#49-consolidated-attribution-and-fog-card-replacement--ready-for-flight) has its performance pair reported; B is reported as run185. The user rejects the uniform fog wash and requests
patchy clouds with clear gaps. Sector-reader/card-replacement technical triage
is recorded in the [fog ledger](verification/volumetric-fog.md#run49b-run185-visual-rejection-and-reader-validation-2026-09-20); the offline family-density recipe passes its fixed view and temporal checks, and the [first D3D9 march checkpoint](verification/volumetric-fog.md#spatial-fog-first-gpu-march-checkpoint-2026-09-20) passes. The [composite checkpoint](verification/volumetric-fog.md#spatial-fog-composite-checkpoint-2026-09-20) passes nine cases and preserves all 1,649,517 tested empty-input pixels. The [recovery checkpoint](verification/volumetric-fog.md#spatial-fog-state-and-recovery-checkpoint-2026-09-20) passes 101 checks, and the [32-frame sequence](verification/volumetric-fog.md#spatial-fog-32-frame-gpu-sequence-checkpoint-2026-09-20) passes 282 fixture checks including varying seam controls. [Complete-transaction timing](verification/volumetric-fog.md#spatial-fog-complete-transaction-timing-checkpoint-2026-09-20) passes the fixed gates (1.1002 ms median at 1280, 1.22575 ms at 1920 in the detached fixture). Production integration now passes actual-pass state/recovery, captured numerical/sequence, and synthetic-owner route/card fixtures; complete production transaction timing is 1.0840 ms at 1280x768 and 1.9808 ms on a resized 1920x1080 workload. The integrated full host suite passes 2,414 tests (694.921 s), with three additional bridge checks; the clean candidate is installed for Run53, with game visual acceptance pending. The combined Run53 flight now queues the first spatial fog check. A cheaper moving-lattice display-history replay also failed its quality
thresholds; the [lattice note §16](architecture/taa-lattice-crawl.md#16-cheaper-post-display-history-replay-rejected-2026-09-20)
records the result. [Mesh ownership is now identified](architecture/taa-lattice-crawl.md#17-moving-truss-mesh-ownership-recovered-2026-09-20), with signed-position conversion
proved and about 99.7% projected support agreement. The corrected [source-coverage oracle](architecture/taa-lattice-crawl.md#19-corrected-source-coverage-oracle-passes-2026-09-20) passes its 32-frame checks and reduces tracked coverage variation by about 75%.
The subsequent RGB prediction failed its quality gates. The [GPU depth/clip-W witness](architecture/taa-lattice-crawl.md#22-existing-clip-w-capture-separates-the-face-hypotheses-2026-09-20) distinguishes the two face hypotheses but does not explain the capture/replay discrepancy. The bounded [post-route state diagnostic](architecture/taa-lattice-crawl.md#23-opt-in-post-route-state-observation-2026-09-20) has passed independent source/runtime review and 250 actual-helper checks; the clean DLL build and linked audit pass. Full discovery found one synthetic Reset fixture compatibility failure, now repaired and independently reviewed; the full candidate host rerun has passed. Live geometry copies remain held until their access interval is safe. Actual RGB/TAA benefit and a production crawl fix remain unqualified.

Ownership fixture runners and the 563-check inventory are repaired with fresh
passes; all 31 generated shader checks now pass. Fresh collision memo (59 checks)
and cull (113 checks, including default `all`) fixtures also pass. The two
bottle-scoped host-test result-path defects were corrected before the full rerun. Eight obsolete agent worktrees
were removed after preserving unique evidence; the live locked Claude worktree
was retained. A later [registration cleanup](../verification/results/worktree-registration-cleanup-2026-09-20.json) pruned 25 already-empty stale entries, preserving all branches and files; 25 materialized/active registrations remain. Every
worktree branch of the previous 2026-09-19/20 session is merged. Earlier session sections of this
file moved to [archive/status-sessions-through-2026-09-19.md](archive/status-sessions-through-2026-09-19.md).
