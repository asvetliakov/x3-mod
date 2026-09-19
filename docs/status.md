# Project status

Updated 2026-09-20 (run49 installed; run48 analysis complete; awaiting run49 user flight). This is the
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
submission findings are recorded in their owning notes. Remaining engine/proxy
costs, moving collision and first-view stalls await the consolidated diagnostic
flight; run181 did not close those investigations.

Card replacement with card-only state validation is reviewed and committed
(`cd004f35`), preserving the normal setter path. The read-only sector diagnostic
and the reviewed [239-sector fog census](reverse-engineering/sector-fog-census.md)
(`a104f376`) are complete. The planned family anchors, bluewell 0.01 and
foggreenoutlands 0.05, remain manual comparisons; automatic sector policy awaits
the sector-chain flight and is not active. Count-only strength scaling is held.

Light-selection and collision-query timers are integrated, reviewed and committed
(`9fa4da5a`): 2,093 light-timer checks with zero failures; 142 collision-timer
checks and 675 queries with zero differences. The next flight combines them with
the existing loop/game/residual phases. Qualification and installation are complete. [Run49](verification/user-runs.md#49-consolidated-attribution-and-fog-card-replacement--ready-for-flight) is awaiting
the user: a matched performance pair, then fog replacement/sector validation.

Ownership fixture runners and the 563-check inventory are repaired with fresh
passes; all 31 generated shader checks now pass. Fresh collision memo (59 checks)
and cull (113 checks, including default `all`) fixtures also pass. The two
bottle-scoped host-test result-path defects were corrected before the full rerun. Eight obsolete agent worktrees
were removed after preserving unique evidence; the live locked Claude worktree
was retained. Every
worktree branch of the previous 2026-09-19/20 session is merged. Earlier session sections of this
file moved to [archive/status-sessions-through-2026-09-19.md](archive/status-sessions-through-2026-09-19.md).
