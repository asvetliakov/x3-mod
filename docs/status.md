# Project status

Updated 2026-09-20 (run 48 reported; fog census and diagnostic qualification complete; next candidate in preparation). This is the
short current status; the session handoff is [handoff-2026-09-20.md](handoff-2026-09-20.md).
Older session sections are in
[archive/status-sessions-through-2026-09-19.md](archive/status-sessions-through-2026-09-19.md),
the earlier narrative in [status history 2026-09-14](archive/status-history-2026-09-14.md) and
[status history 2026-09-13](archive/status-history-2026-09-13.md).
Read history only for a relevant unresolved question. The
[goal checklist](goals.md), [run queue](verification/user-runs.md) and
[original objective](user-objective.md) retain the full scope.

## Installed build

Bottle **X3**, **CrossOver Preview.app**. Installed DLL SHA-256 is
`9bbe69387fdf87505f26ce6625f6c8e1c239367bc5634930c5e307240d38d7ef`
(19,270,118 bytes), built once from clean committed main `b698c32c`
(2026-09-20; marker without `-dirty`), installed through `manage.py install`.
The [build record](../verification/results/run48-candidate-build.json) binds the
clean build, the no-x87 audit (88 roots, 559 reachable), 17 exports, ten site
verifiers (submit 17, collide memo 33), all 31 authored shaders + 9 bloom programs
(every program ≤ 512 ps_3_0 slots; tightest 509), the full 190-case motion-output
suite (0 behavioural regressions), sun-share live 25, submit-phase CPU 393, fog pass
89, temporal pass 459/19, collide memo 59, the state-hook benchmark (+627.9 ns draw
pair), the four §48 dry-runs and the full host suite (2,322; its two failures were
stale test expectations for the prepass table, corrected in the following commit
without touching a production input). The
[install record](../verification/results/run48-candidate-install.json) binds the
installed bytes, unchanged EXE/bottle hashes and the rollback: run47
`95d6306b…` in `/tmp/x3-candidate-jpEyJK/rollback`. No game launched.

Launcher defaults for modded launches: original hull shading; `--cull-small-parts 2`
scope `all`; `--collide-sat-sse2` and `--collide-memo` on (`--no-…` switches).
New in this build, default off: `--taa-thin-region` (lattice arm
crawl), `--volumetric-fog`
(Ctrl+Alt+F9 / Ctrl+Alt+F10), `--submit-phases` (diagnostic); always on: the sun-lane
stamp (run180: available and applied in all 128 captured frames; advertisement-sign artifacts not specifically inspected). `--taa-far-stabiliser 0.985` was accepted by the user in
run 47 C and remains a default candidate. After an additional flight the user
selected **light-map fade 80,220,1 as the launcher default** (2026-09-20);
`--no-light-map-far-fade` disables it. Thin region 0.97 fixes stationary crawl;
moving quality remains open. The DLL is unchanged.

## Current state (2026-09-20)

Run 47 is read; **run 48 A/B/C are reported** ([user-runs.md](verification/user-runs.md) §48):
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
the existing loop/game/residual phases. The root owns candidate preparation:
the full host suite is running (`/tmp/x3-run49-host-suite-final-X3.log`); the next
candidate has not yet been built or installed.

Ownership fixture runners and the 563-check inventory are repaired with fresh
passes; all 31 generated shader checks now pass. Eight obsolete agent worktrees
were removed after preserving unique evidence; the live locked Claude worktree
was retained. Every
worktree branch of the previous 2026-09-19/20 session is merged. Earlier session sections of this
file moved to [archive/status-sessions-through-2026-09-19.md](archive/status-sessions-through-2026-09-19.md).
