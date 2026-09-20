# Project status

Updated 2026-09-20 (run49 installed; run48 analysis complete; run49 A/B reported; fog redesign in progress). This is the
short current status; the session handoff is [handoff-2026-09-20.md](handoff-2026-09-20.md).
Older session sections are in
[archive/status-sessions-through-2026-09-19.md](archive/status-sessions-through-2026-09-19.md),
the earlier narrative in [status history 2026-09-14](archive/status-history-2026-09-14.md) and
[status history 2026-09-13](archive/status-history-2026-09-13.md).
Read history only for a relevant unresolved question. The
[goal checklist](goals.md), [run queue](verification/user-runs.md) and
[original objective](user-objective.md) retain the full scope.

## Installed build

Bottle **X3**, **CrossOver Preview.app**. Run49 DLL SHA-256:
`8a711cabc87f59beb2368c4cdfec1b125c4b8ce927d4b946cd9e03978e2105a7`
(19,529,616 bytes), built once from clean committed source `976dbdee`, installed
through `python3 tools/manage.py install --bottle X3 --dll-source <retained DLL>`.
The [build record](../verification/results/run49-candidate-build.json) records
**2,351 host tests passing** (659.194 s), linked audit 95 roots / 579 reachable /
zero violations, 17 exports, and seven selected rendering cases (578 checks).
Those seven cases passed individually; this is not a new full motion-output-suite
pass. New timer qualification is bound in the
[diagnostic record](../verification/results/run49-diagnostics-qualification.json):
2,093 light checks and 142 collision checks / 675 paired queries, all passing.
The [install record](../verification/results/run49-candidate-install.json) verifies
installed bytes and unchanged EXE/bottle hashes. Rollback DLL and manifest retain
run48 `9bbe6938…` in
`/var/folders/l6/0sdq5b49401b_4m_26gsl1f00000gn/T/x3-run49-candidate-9hos95ss/rollback`.
No game launched; all three §49 commands passed `--dry-run`.

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
failures are a separate caller path. The temporary [run51 counter](verification/user-runs.md#51-media-retry-counter--same-view-longer-diagnostic-interval) is ready; retry defaults and decoder remain unchanged. A [standalone media fixture](verification/media-cues.md#standalone-id2-playback-boundary-2026-09-20) reproduces v4 open failure; fixture-only v5 opens but blocks at
zero-seek. Skipping that call delivers six distinct frames to a diagnostic D3D
texture with clean shutdown; correct seeking and game integration remain open.
No game decoder change. Moving collision is
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
is recorded in the [fog ledger](verification/volumetric-fog.md#run49b-run185-visual-rejection-and-reader-validation-2026-09-20); an offline spatial-density redesign is in progress. No repeat flight is
requested. A cheaper moving-lattice display-history replay also failed its quality
thresholds; the [lattice note §16](architecture/taa-lattice-crawl.md#16-cheaper-post-display-history-replay-rejected-2026-09-20)
records the result. [Mesh ownership is now identified](architecture/taa-lattice-crawl.md#17-moving-truss-mesh-ownership-recovered-2026-09-20), with signed-position conversion
proved and about 99.7% projected support agreement. The bounded [coverage oracle](architecture/taa-lattice-crawl.md#18-visible-area-oracle-is-numerically-unreliable-2026-09-20) failed its numerical checks; no temporal-quality verdict or
fix follows from it. Ownership remains established.

Ownership fixture runners and the 563-check inventory are repaired with fresh
passes; all 31 generated shader checks now pass. Fresh collision memo (59 checks)
and cull (113 checks, including default `all`) fixtures also pass. The two
bottle-scoped host-test result-path defects were corrected before the full rerun. Eight obsolete agent worktrees
were removed after preserving unique evidence; the live locked Claude worktree
was retained. Every
worktree branch of the previous 2026-09-19/20 session is merged. Earlier session sections of this
file moved to [archive/status-sessions-through-2026-09-19.md](archive/status-sessions-through-2026-09-19.md).
