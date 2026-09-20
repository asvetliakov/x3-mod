# Project status

Updated 2026-09-20 (Run52 accepted; fog, lattice, media and collision experiments continue). This is the
short current status; the session handoff is [handoff-2026-09-20.md](handoff-2026-09-20.md).
Older session sections are in
[archive/status-sessions-through-2026-09-19.md](archive/status-sessions-through-2026-09-19.md),
the earlier narrative in [status history 2026-09-14](archive/status-history-2026-09-14.md) and
[status history 2026-09-13](archive/status-history-2026-09-13.md).
Read history only for a relevant unresolved question. The
[goal checklist](goals.md), [run queue](verification/user-runs.md) and
[original objective](user-objective.md) retain the full scope.

## Installed build

Bottle **X3**, **CrossOver Preview.app**. Run52 diagnostic candidate DLL SHA-256:
`4bee98b40420ff8a7ddc433435a1ee6727d72c586f26f169b23d492f628df685`
(19,559,896 bytes), built once from clean committed source `976307f2`; retained
at `/tmp/x3-run52-candidate/d3d9.dll`. Run52 flight qualification is complete; the reviewed submission-attribution
source is merged to main. The installed DLL bytes are unchanged.
The [qualification record](../verification/results/submission-attribution-qualification-2026-09-20.json)
binds 2,353 host tests (2 skipped), linked audit 95 roots / 579 reachable / zero
violations, 8,881 CPU fixture checks, and two selected rendering cases (126 checks).
A separate automatic-exposure diagnostic passed 59 process checks and 248 scoped
validation checks, including Reset and ten exhaustive readback timing rows. Its
generic harness rejection for deliberate every-frame logging is preserved;
lease runtime coverage is 24 empty scans, not nonempty releases.
The [install record](../verification/results/run52-candidate-install.json) verifies
installed bytes and unchanged EXE/bottle configuration. Rollback retains run49's
DLL and manifest in `/tmp/x3-run52-candidate/rollback`. Installation used
`python3 tools/manage.py install --bottle X3 --dll-source <retained DLL>`.
The agent did not launch the game; the corrected [run52 attribution command](archive/run52-completed-2026-09-20.md)
passed `--dry-run`. This build adds diagnostics, not the experimental fog, media,
lattice or collision changes.

Launcher defaults now use **lazy render-target binding** with motion output
after [Run52 acceptance](verification/motion-output.md#run52-lazy-render-target-binding-accepted-as-launcher-default-2026-09-20); explicit `--motion-rt-mode perdraw` remains available.
Launcher defaults retain original hull shading, `--cull-small-parts 2` scope
`all`, `--collide-sat-sse2`, `--collide-memo`, and the user-selected light-map
fade **80,220** (the optional third value defaults to 1, so the stored setting
is `80,220,1`; `--no-light-map-far-fade` disables it).
New opt-in features: `--volumetric-fog-cards replace`, read-only
`--sector-background`, `--light-phases`, and `--collide-query-phases`.
Fog still uses manual strength and card presence; automatic family strengths
are not active. Far stabiliser 0.985 and thin region 0.97 remain explicit options:
stationary improvement is accepted, moving-lattice quality remains open.
Native Windows runtime remains unverified.

## Current state (2026-09-20)

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
No game decoder change. A [native same-stack control](verification/media-cues.md#native-same-v5-pipeline-proves-preroll-reaches-the-wine-destination-2026-09-20) proves the Wine graph presents a 9.4-second preroll frame after a ten-second seek; an explicit portable-provider fixture is being implemented. The [owned collision query oracle](reverse-engineering/sector-collide.md#owned-original-query-parity-checkpoint-2026-09-20) passes 3,118 frozen queries with zero result/ABI mismatches after correcting a truncated fixture constant. Paired tree-layout timing is next. Historical memo/query-phase fixtures share that extraction limitation; their repair is underway. Installed collision code and the separate builder qualification are unaffected. Live snapshot lifetime remains unproved. Moving collision is
about 98% of printed instrumented query time inside descent in the expensive interval, so query setup is not
the missing lever. Details are in the [frame-time note](architecture/engine-frame-time.md#run49-a-three-scene-diagnosticcounter-flight-2026-09-20) and
[collision note](reverse-engineering/sector-collide.md#run49-a-moving-query-cost-is-inside-descent-2026-09-20).
Run181 did not close the engine/proxy or moving-collision investigations.

Card replacement with card-only state validation is reviewed and committed
(`cd004f35`), preserving the normal setter path. The read-only sector diagnostic
and the reviewed [239-sector fog census](reverse-engineering/sector-fog-census.md)
(`a104f376`) are complete. The planned family anchors, bluewell 0.01 and
foggreenoutlands 0.05, remain manual comparisons. Run185 validates the reader
in its observed sectors; automatic sector policy remains held during the spatial
fog redesign. Count-only strength scaling is held.

Light-selection and collision-query timers are integrated, reviewed and committed
(`9fa4da5a`): 2,093 light-timer checks with zero failures; 142 collision-timer
checks and 675 queries with zero differences. Run49 A combined them with
the existing loop/game/residual phases. Qualification and installation are complete. [Run49](verification/user-runs.md#49-consolidated-attribution-and-fog-card-replacement--ready-for-flight) has its performance pair reported; B is reported as run185. The user rejects the uniform fog wash and requests
patchy clouds with clear gaps. Sector-reader/card-replacement technical triage
is recorded in the [fog ledger](verification/volumetric-fog.md#run49b-run185-visual-rejection-and-reader-validation-2026-09-20); the offline family-density recipe passes its fixed view and temporal checks, and the [first D3D9 march checkpoint](verification/volumetric-fog.md#spatial-fog-first-gpu-march-checkpoint-2026-09-20) passes. Composite identity checks and subsequent recovery/performance qualification remain in progress. No repeat fog flight is
requested. A cheaper moving-lattice display-history replay also failed its quality
thresholds; the [lattice note §16](architecture/taa-lattice-crawl.md#16-cheaper-post-display-history-replay-rejected-2026-09-20)
records the result. [Mesh ownership is now identified](architecture/taa-lattice-crawl.md#17-moving-truss-mesh-ownership-recovered-2026-09-20), with signed-position conversion
proved and about 99.7% projected support agreement. The corrected [source-coverage oracle](architecture/taa-lattice-crawl.md#19-corrected-source-coverage-oracle-passes-2026-09-20) passes its 32-frame checks and reduces tracked coverage variation by about 75%.
The subsequent RGB prediction failed its quality gates; an actual-shader owner/alpha/depth fixture is now being implemented. Actual RGB/TAA benefit and a production GPU implementation remain unqualified.

Ownership fixture runners and the 563-check inventory are repaired with fresh
passes; all 31 generated shader checks now pass. Fresh collision memo (59 checks)
and cull (113 checks, including default `all`) fixtures also pass. The two
bottle-scoped host-test result-path defects were corrected before the full rerun. Eight obsolete agent worktrees
were removed after preserving unique evidence; the live locked Claude worktree
was retained. Every
worktree branch of the previous 2026-09-19/20 session is merged. Earlier session sections of this
file moved to [archive/status-sessions-through-2026-09-19.md](archive/status-sessions-through-2026-09-19.md).
