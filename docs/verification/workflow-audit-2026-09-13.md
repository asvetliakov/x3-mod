# Workflow audit: qualification, evidence and coordination

Date: 2026-09-13. This is a process audit, not a code-correctness review. It
samples the camera work through reviews 46, 48 and 49, loading work through
reviews 30–35 and run 17, bloom reviews 39–47, and the review-29/iteration-13
motion/TAA checkpoint. Temporary qualification artifacts were inspected as
evidence, not treated as instructions. No build, Wine process, game, install or
qualification artifact was changed. The sampled camera work subsequently
completed at source checkpoint `dac2994` with install-document checkpoint
`02a9c2e`; completion does not change the process findings below.

## Adopted changes

The user authorized simplification after this audit. `AGENTS.md` now makes
verification proportional to the changed behavior, combines routine review
rounds, assigns one candidate owner, and removes duplicate manifests and status
mirrors as defaults. Agent briefs carry only the relevant objective and files.
The 103,929-byte status page was preserved verbatim in a linked history archive
and replaced with a roughly 4 KB current handoff. No historical results or Git
history were removed.

`wine_lock.py` performs the shared process preflight under its lease and offers
optional lock/child timing output. `run_d3d9_exports.py` requires `--dll PATH`,
builds only its fixture, and accepts `--case writable|readonly|both`.
Use `writable` for a selected load smoke and `both` for log-fallback verification:

```sh
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_d3d9_exports.py --dll build/d3d9.dll --case writable
```

The Astra author ran 19 focused host tests. Independent orchestrator review found
overbroad Python runner detection and optional timing-write errors overriding
child status; both were fixed, then all 9 affected lock tests passed. A real
host process snapshot also passed without executing Wine. Classification cost
was about 10 ms for 647 process rows, once per lease; there is no production
frame-path change. No full-suite, Wine or DLL rebuild was needed for these
host orchestration changes. Other legacy runners should adopt the retained-DLL
contract when touched; this checkpoint does not claim all runners were migrated.

## Executive finding

The project is over-qualified at the workflow level. The strongest parts of the
process are the checks tied to an actual failure mode: exact game hook bytes,
CPU-state/ABI preservation, partial-hook rollback, D3D state and image recovery,
Reset behavior, numerical oracles, focused host tests, and measurements of a
changed hot path. Those checks repeatedly found real defects and should remain.

The weak part is a second assurance system layered around those checks: repeated
whole-tree hash maps, copied source/result snapshots, evidence manifests that
hash other manifests, complete object/archive inventories after every clean
build, full host discovery followed by a duplicate focused run, unrelated
fixture chains, repeated Steam/X3 runs, multiple near-identical review documents,
and post-install re-audits of facts the installer could assert itself. This
adds agent attention and wall time around the tests, while often adding little
confidence once source is committed and the retained candidate has one stable
hash. The available records do not measure that overhead separately from
implementation, review and fixture-development time.

The default should be proportional qualification. A small constant or launcher
change needs focused tests and review. A new game hook needs site, ABI and
rollback checks. A renderer transaction needs GPU state, Reset and recovery
checks. Only an install candidate needs a clean full build and exact candidate
binding. Broad regression should be periodic or dependency-triggered, not a
tax paid by every checkpoint.

## Non-negotiable project invariants

These come from the user and are not candidates for simplification:

- The agent never launches the game. Gameplay and visual acceptance belong to
  the user; dry-run command validation is allowed.
- New CrossOver fixture work uses bottle X3 only, explicitly sets
  `X3M_FIXTURE_BOTTLE=X3`, and every Wine command uses the shared Wine lock.
- Code is reviewed before a logical checkpoint commit; concrete findings are
  fixed and affected verification is rerun. An independent reviewer is used
  when practical.
- New code receives a performance pass, with diagnostic timings kept distinct
  from game FPS.
- Native Windows/D3D compatibility remains a required source target, with
  unverified runtime behavior stated honestly.
- Generated products, raw captures, copyrighted game bytes and raw decompiler
  output remain outside the repository. Game EXE/CAT/DAT files and unrelated
  bottle state are preserved; installs are reversible and app-local.
- Game hook sites, calling ABI and private game layouts retain appropriate
  validation. SSE2 and the four-byte incoming-stack contract remain explicit.

Everything below simplifies self-imposed procedure around those invariants.

## Purpose, observed cost and value

| Practice | Purpose | Observed cost in the sample | Assessment and default |
| --- | --- | --- | --- |
| Focused host tests | Exercise changed math, parsers, site validators and failure paths quickly. | Review 48 ran 56 camera/site tests, then reran the same 56 after the 0.6 constant change. The final camera qualification ran 67 focused tests in 3.427 s. | High value. Run after each relevant edit and after review fixes. A constant-only rerun was appropriate; it did not need a wider qualification of its own. |
| Full host discovery | Catch cross-module regressions. | Final camera qualification: 994 tests in 47.425 s. A first 46.878 s attempt failed only because `PYTHONPATH` omitted `verification/probe`, so the same discovery was rerun; the accepted run was followed by the already-covered 67-test focused run. Review 46: 972 tests in 47.571 s. Review 35: 907 tests in 43.694 s. Review 29: 797 tests in 33.7 s. | Useful before an integrated install or after shared infrastructure changes. Standardize the invocation so an environment typo cannot double its cost. Do not routinely run it and then rerun an included focused suite merely to create a second record. |
| Clean full proxy build | Eliminate stale objects and create the install candidate. | The final camera candidate clean build took 8 s. The surrounding source-manifest-to-DLL filesystem interval was 24 s and includes unmeasured orchestration. | Keep once for the final install candidate. Use incremental builds or changed-translation-unit compilation during iteration. Test runners must not rebuild the frozen production DLL. |
| Whole-tree source manifests | Detect concurrent source drift and bind results to inputs. | Review 46 froze 158 build inputs, 75 analysis inputs and 11 tool inputs. The elevated run contains five byte-identical 25,079-byte production manifests plus two byte-identical 84,335-byte runtime-source manifests; it also retains other freezes. | Disproportionate once work is committed. Bind evidence to a Git commit/tree plus the dirty diff hash, and let each runner record only its real transitive inputs. Use a before/after map only for long uncommitted runs exposed to concurrent edits. |
| Evidence-of-evidence manifests and copies | Prove that logs, summaries and copies were not changed after execution. | The two camera scratch trees occupy about 59 MiB and 34 MiB and contain 104 and 52 files. Their evidence manifests enumerate 103 and 51 entries; the finished checkpoint adds host/runtime/install summaries and review 49. At least one 11,606,225-byte DLL is duplicated exactly across the two trees, alongside repeated result manifests and audits. | Keep one compact machine-readable qualification summary and the failure artifacts needed to diagnose an actual failure. Stop copying successful evidence into several pre-run, retained, baseline and final directories; stop hashing manifests solely to prove other hashes. Hash computation is cheap; designing, explaining and independently rechecking all these layers is the material cost. |
| Candidate hash before/after external execution | Ensure Wine runners or install steps did not replace the qualified DLL. | Cheap hash checks caught no candidate drift in the sampled camera runs. | High value for the final candidate. Record one SHA-256 before external execution, require it afterward and at install. Do not also copy the same candidate repeatedly. |
| PE exports/imports and object inventory | Detect wrong machine type, accidental dependencies, missing exports, duplicate or omitted objects. | Review 46 audited 41 objects, 17 exports, 192 imports and archive/disk byte equality. The new camera candidate repeated the full audit for 42 objects and 193 imports. | Targeted value is high when CMake, loader, exports, link inputs, packaging or backend dependencies change. For ordinary math/default changes, compare candidate machine/import/export metadata to the last accepted candidate and inspect only changed boundary objects. Full archive-member equality after a clean CMake build is low-value duplication. |
| No-x87 and callback disassembly audits | Enforce the x86 CPU contract around injected code. | Camera checkpoints repeatedly walked 211 reachable functions. Review 49's targeted object audit found a real SJLJ boundary gap and verified the corrected save/restore envelope. | Keep targeted callback/object audits whenever hook code or its flags change. Run the whole reachable-function audit only when compiler flags, shared boundary helpers or the final integrated link change. Do not repeat it for a presentation constant. |
| Exact installed-EXE site verification | Prevent patching the wrong executable bytes or splitting instructions. | The earlier camera probe performed nine checks at one camera hook; the aim verifier covered four hook sites. Review 49 also checked decoded lengths and interior branch targets. | Essential for every new or changed game hook and once against the installed EXE before installation. Reuse it when neither the EXE identity nor site declarations changed. |
| X3 fixture execution, lock and game guard | Execute x86 hooks/D3D behavior safely without launching the game and serialize Wine. | Necessary runs are short in the recent camera sample, but exact command runtimes were not recorded. File timestamps span about 26 s for the six aim cases, 13 s for the camera host and 25 s for the export fixture. Five identical 72-byte preflight records were also produced, and an earlier ad-hoc process scan matched its own wrapper. Lock wait time is unavailable. | Keep the lock, game guard and required process check. Put them in one standard runner/lease preflight that excludes ancestors, records `lock_wait_seconds` and runs once per lease. Remove bespoke shell scans and repeated identical preflight files. |
| Steam plus X3 duplicate fixtures | Compare two CrossOver execution paths. | Review 29's Wine phase took 16.4 min; its loading suite alone took 3.4 min on Steam and 45 s on X3. Review 30 ran a 234 s X3 motion suite after retaining/rechecking Steam evidence. Bloom retained identical 206-readback inventories on both bottles. | Remove from all new routine work, as the user has already required X3 only. A second backend is justified only for a specific backend discrepancy; Steam is not a substitute for native-Windows validation. |
| Broad serialized fixture chains | Prove whole-project integration. | Review 29 ran a 16.4 min Wine chain. Review 35 ran 18 serialized wrappers, including complete motion suites and benchmarks on both bottles plus temporal, ownership, loading, exports, lifetime, cache, shader, reader, gzip and crypto gates, for a camera integration. Its total wall time is not recorded. | Disproportionate for feature-local checkpoints. Use a dependency-based integration set. Reserve the broad chain for changes to shared proxy/device infrastructure or a periodic release candidate. Benchmarks do not belong in every correctness chain. |
| Export/load fixture | Check the proxy loads, forwards 17 exports and defaults to safe behavior. | The new elevated-camera cycle copied the prior accepted writable/read-only export outputs as its baseline; it ran only the final pair. The final pair's file timestamps span about 25 s; command timing is unrecorded. | Keep one X3 load smoke for an install candidate when loader/import/export behavior changed. Run both writable and read-only log cases only when log-path/fallback behavior changed. Copying an accepted baseline is not a fresh execution and should be labeled as reuse. |
| Independent review | Find design, correctness and evidence gaps outside the authoring path. | It found substantive issues: camera tests that were compiled but never invoked, optional reads made mandatory, TAA cuts consumed by one device, inherited MXCSR, SJLJ work outside preservation, bloom tessellation/state recovery gaps, weak parser acceptance, and loading lifetime faults. Review/agent time is not recorded. | Keep one independent review per logical risky checkpoint. Fold source, test-evidence and candidate-artifact findings into that review. Separate artifact-only and post-install reviews should occur only after a mismatch, a new installer, or a high-risk packaging change. |
| Rejected-attempt preservation | Prevent failed evidence from being mistaken for success and retain diagnostic context. | Camera and bloom runs kept several full rejected directories. Review 30 incurred two 60 s temporal timeouts before a 65.2 s accepted run, so known temporal attempt time was at least 185.2 s; instrumentation time is unavailable. The aim fixture also had two allocation/setup failures before a controlled PE mapping redesign; that was useful verification-infrastructure work, not product-code qualification. | Preserve a compact rejection record with command, candidate/input identity, terminal reason and the minimal failing output. Retain full failed artifacts only while diagnosing or when they are the only evidence for an important boundary. |
| Install provenance and rollback | Make replacement reversible and bind installed bytes to the qualified candidate. | Chase install records preserve DLL/manifest rollback, EXE/config hashes and exact installed hash. One install recorded 11 post-install dry runs; the next camera install recorded two. Timings are not recorded. | Keep one previous DLL and manifest, game guard, atomic/recoverable replacement, installed rehash, manifest check and EXE/config preservation check. Use one affected-feature dry run plus vanilla only if launcher/configuration changed. Eleven dry runs are disproportionate. |
| Documentation/checkpoint synchronization | Keep state, architecture, review and run plans understandable. | `docs/status.md` is 103,682 bytes and was touched by 37 of 44 commits since midnight; `docs/goals.md` by 20. There are 134 tracked verification documents totaling 1,716,248 bytes. The 0.6 camera-default checkpoint changed ten files, including six documents. | Keep a short current status, one durable architecture/RE document per coherent topic, and one verification ledger per feature. Link to machine summaries. Do not restate the same candidate hash, limitations and pending run in status, goals, roadmap, handoff, run plan and several reviews at every small checkpoint. |
| Tracked raw/snapshot results | Preserve all test output in Git. | There are 912 tracked files under `verification/results`, 180,762,539 bytes total: 334 logs, 327 text files and 248 JSON files. Since midnight, result-file numstat contains 246,616 inserted and 164,993 deleted lines across 519 file-change rows, much of it regenerated output churn. | Track compact summaries, stable golden inputs and small failure witnesses. Leave raw logs, per-case traces, readbacks and rebuilt binaries untracked as the user requires. Stop updating huge generated result sets for unrelated changes. |
| Offline image analysis | Turn user captures into quantitative visual evidence. | Iteration 13 reports about four minutes per 16-frame Python image-analysis pass and ran measured 1.0 plus modeled 0.75 and 0.5 passes, about 12 minutes of stated analysis time. | High domain value: it validated the measured present image and informed the sharpen choice. Cache common decoded/intermediate arrays and evaluate multiple parameter values in one pass; this is a performance issue in the analysis tool, not a reason to drop the evidence. |

The 912 tracked-result count and size describe the current working tree, while
the churn figures cover commits since midnight. They should not be read as time
measurements. Likewise, file timestamp spans are bounds on observed workflow
windows, not command runtimes.

## What the sampled workstreams show

### Camera

Review 48 is a good model for a small but mathematically meaningful feature
change: focused geometry and launcher controls, one x86 translation-unit
compile, a bounded performance measurement, review, and a focused rerun after
the 0.6 default changed. Review 49 also justifies its exact-site, hostile CPU
state and partial-install tests because it adds four game hooks and caught a
real compiler-boundary fault.

The subsequent candidate orchestration is where the process expands beyond its
value. The elevated qualification's active evidence window runs from the first
source snapshot at 07:22:26 to terminal preflight at 07:41:59, a 19 min 33 s
filesystem span. Accepted host-test command time is 50.852 s, the preceding
bad-`PYTHONPATH` discovery cost another 46.878 s, and the clean build took 8 s.
Exact X3 command durations are not recorded; the three fixture-result timestamp
windows above total about 64 s. These numbers overlap with artifact
authoring and cannot be subtracted to produce a precise overhead total. They do
show clear duplication: five equal production manifests, two equal 525-file
runtime maps, copied baseline/final export records and a 103-file evidence
manifest. The records do not isolate the time spent producing these from
fixture development, code review or coordination. The prior camera qualification similarly spans 7 min 26 s from source
freeze to evidence manifest, with 47.571 s recorded for full host discovery.

For a default-distance change, the lean result would have ended after the
focused tests, reviewer check and checkpoint. For the four-hook aim observer,
the lean final candidate still needs one clean build, target object/ABI audit,
site verifier, X3 hostile fixture and one proxy load smoke. It does not need a
new proof that every unchanged archive member and every previous export result
was copied correctly.

### Loading: reader, adjacency and CryptoAPI

This area warrants stronger checks than a camera constant because it alters
game call admission, lifetime, stream position, native API state and fallback
behavior. Reviews 31 and 33 used precise failure witnesses and found real issues:
rewind publication ordering, LastError transport, cache retirement, reentrancy,
FP-domain delivery and parser false passes. Native differential tests and
same-input fallback equality are appropriate. Run 17's user-supplied game log is
especially valuable: it measured the 844-check signature probe at 0.1353276 s
versus 12.835 s earlier, while carefully refusing to attribute the entire load
gap to the cache. That is product evidence, not paperwork.

The excess is running both Wine bottles for each corrected fixture, copying
each bottle's build directories, and repeatedly hashing every source, native
DLL, executable, stdout and stderr before/after/current after the runner already
published a strict result. Review 34 demonstrates a better pattern: it reused
the reader's 4,721-check evidence after verifying that the actual reader inputs
were unchanged, then ran eleven selected integration cases rather than calling
that a full-suite pass. Keep that dependency-aware reuse. Fold its separate
artifact-audit prose into the same integration record.

### Bloom

Bloom is the strongest case for staged qualification. CPU/SEH transport,
composition math, backend precision characterization, device-owner lookup,
production bridge packaging, the transactional executor, Reset/recovery and
shader packaging have materially different failure modes. The sequence caught
invalid numerical assumptions, parser weakness, inherited tessellation state
and incorrect recovery assertions before integration. Collapsing those tests
into one opaque end-to-end run would reduce confidence.

The staging does not require eight standalone narrative review files plus
duplicated artifact audits. Maintain one bloom architecture set and one bloom
verification ledger with sections for CPU bridge, shader/numerics, owner/lifetime
and GPU transaction. Each section can name the reviewed commit and compact
summary. Under the current X3-only rule, do not repeat the 240-check bridge,
393-check production bridge or 206-readback BloomPass corpus on Steam. Preserve
the first numerical/recovery rejection as a concise diagnostic record; the full
failed tree need not become permanent project state.

### Motion/TAA

Review 29's merged loading/readback change triggered a 16.4 min Wine chain,
including a 3.7 min motion suite, a 3.4 min Steam loading suite and a 45 s X3
loading suite, plus many unrelated fixtures. That was credible release-level
evidence but a poor default for an ordinary checkpoint. The fact that runner
relinks changed the final DLL's PE timestamp also created more candidate
identity bookkeeping; frozen-candidate runners should never build production.

Iteration 13 is substantive rather than ceremonial: large user captures were
kept outside Git, the analysis validated the actual presented image, and the
result drove a 0.75 sharpen recommendation. Its repeated four-minute passes
should be optimized, but the measurement belongs. This distinction is the core
of the audit: retain expensive work that answers a product question; remove
expensive work whose only output is another record that unchanged records still
hash the same.

## Lean replacement workflow

### Small source, setting, launcher or documentation change

1. Identify the affected contract and change it in one logical working set.
2. Run focused host/unit tests. For C++, compile the changed translation unit
   with production x86 flags; use an incremental project build only if linkage
   is relevant.
3. Perform the required static performance inspection. Measure only if the
   change adds work to a hot path or changes an existing measured algorithm.
4. Review code changes before the checkpoint and use an independent reviewer
   when practical, especially for production source or runtime behavior. Fix
   findings and rerun only affected checks. A small documentation-only change
   does not require an independent code review.
5. Run `git diff --check`, update the owning architecture/verification document
   and the short current-status pointer, then commit.

Default omissions: Wine, full host discovery, clean full build, export/load
fixture, PE/archive audit, whole-tree manifest, evidence manifest, install and
multiple status mirrors. A setting-only camera change is this class.

### Game hook, ABI, lifetime or GPU transaction change

1. Do the focused workflow above, plus exact installed-EXE site/whole-instruction
   validation for game hooks.
2. Build one isolated x86 fixture that executes the real production boundary.
   Test CPU/LastError preservation, failed admission, partial-install rollback
   and late-window refusal as applicable. For GPU work, test hostile state,
   actual writes, recovery, Reset and alpha/resource lifetime.
3. Run the focused X3 fixture once under the shared lock after the standard
   guard/process preflight. Record lock wait, command runtime, exit, check count,
   bottle/environment, relevant input commit/diff and fixture hash in its own
   compact summary.
4. Inspect only the changed boundary objects and relevant imports/exports.
   Perform the user-required performance pass at the actual hook/draw cadence.
5. Independent review covers source and this evidence together. After fixes,
   rerun the failed/affected fixture rather than the whole project.
6. Commit the reviewed logical checkpoint. A broad integration run waits until
   the feature is linked into an install candidate.

### Integrated install candidate and install

1. Start from a committed reviewed checkpoint and a clean working tree for
   production inputs. Run one clean MinGW full build. Record commit/tree,
   toolchain, candidate SHA-256 and size.
2. Run affected host checks; use full discovery for shared infrastructure or
   a deliberate release checkpoint, once rather than followed by duplicate
   focused runs. Add dependency-selected X3 fixtures: proxy-load/default-off
   smoke for relevant loader/import/export changes; affected hook/ABI/GPU
   fixtures; a compact integration test when a shared device/camera/lifetime
   path changed. Reserve the full fixture chain for that broader scope.
3. Produce one candidate summary containing commands, measured runtimes, checks,
   bottle facts, target object audits, relevant import/export delta, candidate
   hash and explicit limitations. A reviewer validates the summary and source
   delta; no separate evidence-of-evidence manifest is needed.
4. Recheck the candidate hash immediately before installation. Require an empty
   game guard. Preserve one previous DLL and manifest, replace app-local files
   recoverably, rehash installed bytes, verify the install manifest, and confirm
   the EXE and bottle configuration hashes are unchanged.
5. If launcher/configuration changed, run one feature dry run and one vanilla
   dry run. Otherwise one planned user command is sufficient. Record a compact
   install JSON. Do not launch the game; hand the explicit run to the user.

## Keep, combine and remove

**Keep:** user invariants; focused tests; exact hook-site validation; target
CPU/ABI object audits; native differential/fallback witnesses; GPU hostile-state,
Reset and recovery tests; one clean install-candidate build; exact candidate and
installed hashes; one rollback copy; honest limitation statements; user gameplay
evidence; performance measurements that answer a real cost question.

**Combine:** source review, evidence review and artifact review into one review
per logical checkpoint; architecture/reverse-engineering findings into topic
documents; feature verification into one append-only ledger; guard/process/lock
handling into one standard preflight; candidate provenance, command results and
timings into one generated summary; repeated launcher dry runs into affected
feature plus vanilla.

**Remove as defaults:** Steam duplicates; blanket whole-tree manifests around
short runs; before/after/current copies of the same map; evidence manifests that
hash summaries and their copies; full archive-member comparisons when build
inputs did not change; focused reruns immediately after an inclusive full suite;
unrelated fixture chains and benchmarks at feature checkpoints; pre-change
export reruns; complete failed-run retention after diagnosis; repeated candidate
copies; separate post-install review of deterministic installer assertions;
repeated status/goals/roadmap/handoff/run-plan restatement.

## Coordination and time accounting

The lock and user-managed game boundary are necessary. The current coordination
records do not say how long an agent waited for the Wine lock, for another agent,
or for the user to exit the game. Review 44 correctly deferred its fixture when
the game was active; review 31 waited for crypto/exposure agents and a granted
runtime slot. Neither wait is timed. Agent review/context cost is also
unavailable. It would be misleading to infer either from commit gaps because
implementation, reviews, documentation and fixtures overlapped.

Record command and lock timings when available. For an investigated delay,
these optional fields can distinguish its causes; do not build a new accounting
framework or require manual wait tracking for every checkpoint:

- `lock_wait_seconds`: time waiting to own the Wine lease;
- `command_seconds`: child process runtime;
- `retry_seconds`: rejected/timeout attempts, with reason;
- `qualification_wall_seconds`: orchestration start to terminal summary;
- `active_build_test_seconds`: sum of measured build/test children;
- `agent_wait_reason` and `agent_wait_seconds` when known.

Do not add concurrent agent durations together. Do not call a QPC microbenchmark
or filesystem timestamp span wall-clock qualification cost. One orchestrator
should own the install candidate and Wine queue; secondary agents should return
focused findings and paths, not create parallel global source/evidence freezes.
Committed checkpoints or isolated worktrees are a cheaper concurrency boundary
than repeatedly hashing hundreds of shared files.

## Changes allowed now and changes needing a user decision

The following defaults fit the existing rules and can be adopted immediately:

- X3-only Wine work; one standard locked preflight per lease.
- Focused tests for small changes and dependency-selected integration suites.
- One clean build per install candidate; no production rebuild inside a frozen
  runner.
- Git commit/tree plus candidate hash instead of blanket source/evidence maps.
- Targeted PE/object/no-x87 audits based on what changed.
- One independent review per logical checkpoint, with affected reruns.
- Compact summaries, raw evidence untracked, one feature verification ledger,
  and one short current status.
- One rollback pair and a narrow post-install dry run set.

Reversible documentation cleanup also fits the current rules when it is placed
in scope: move historical `docs/status.md` material into a tracked archive,
replace it with a short current page and links, and consolidate duplicated review
prose while preserving the durable technical record.

Explicit user direction is needed for destructive or history-rewriting cleanup:

- deleting tracked result or documentation history, including removal of the
  existing 180,762,539 bytes of tracked results;
- migrating repository history to Git LFS or another store;
- squashing or rewriting historical checkpoint commits;
- weakening the explicit requirements for review-before-commit, a performance
  pass, platform portability, reversible installs, or user-only game launch.

No such historical cleanup is required to get most of the time saving. The
project can stop producing redundant artifacts and adopt the risk-based workflow
for the next checkpoint without changing any user-required invariant.


## Completed-session preservation

The brief [user run queue](user-runs.md) now snapshots after its launch command
returns, using a pre-launch time boundary. Timestamped session logs were already
unique, but image readback names reuse device/frame numbers across launches.
The new [snapshot helper](../../tools/analysis/snapshot_x3_run.py) copies the log
first, streams only recognized file references, and saves them in a fresh
`/tmp/x3-bottleX3-run<N>/` directory. It reports missing/overwritten files, refuses
unsafe paths or an active/unknown game state, and never deletes source data.
Content-addressed shader dumps are checked against their logged identity. There
is no directory-wide copy, persistent counter or new manifest. The shell helper
preserves the original launch exit status; vanilla/no-new-log is a no-op.

Independent review approved the source and final max-existing-number-plus-one
allocation fix. Thirteen [focused host tests](../../verification/analysis/test_snapshot_x3_run.py)
pass, covering actual writer records, copied-log authorization, selection/source
replacement, path safety, stale/missing data, inventory failure, numbering races
and launch exit preservation. No game/Wine execution, DLL build or install was
needed for this workflow-only checkpoint.

## 2026-09-14: bounded XT fixture output

The retained XT depth-off/per-draw/material-off case wrote 42,285,312 stdout
bytes and 1,110,793 successful `CHECK` rows. Three pixel-loop labels account for
1,109,719 rows / 42,178,937 bytes. The fixture now uses a quiet assertion helper
only at those three sites: every numerical check still runs and increments the
same terminal count; failure delegates to the unchanged verbose assertion,
prints the same label and throws immediately. All other fixture output remains.
The report parser consumes `RESULT` totals and structured XT/motion/state rows,
not the number of successful `CHECK` lines.

An executed host test proves 100,000 silent successes and identical counted
first-failure text/exceptions. Strict i686/SSE2 fixture compilation and independent
review pass. Removing those success lines from the retained sample predicts
106,375 bytes of stdout (99.75% less); this is an output-volume calculation,
not a measured runtime improvement. No GPU assertions or acceptance bounds were
removed, no Wine matrix was repeated for this logging-only change, and the
installed renderer and retained qualification binaries are unchanged.


## 2026-09-15: Codex model and effort routing

User-approved balanced routing is now defined in `AGENTS.md` and project-local
`.codex/config.toml` / `.codex/agents/*.toml`: Astra/medium main session,
Sol/medium understood implementation, Sol/high bounded review and disassembly,
Astra/high difficult implementation/design/review, and Terra/medium for
triage, support and mechanical checks. Three concurrent children maximum, no
recursive delegation, one Wine/install owner. Briefs carry relevant current
facts, resolving the previous shared-contract/Claude status-reading conflict.
Claude model settings and renderer acceptance state are unchanged.

The defaults use Standard processing; explicit session overrides can supersede
them. Start a new trusted project session to load the configuration. Direct
collaboration calls without custom-role selection must pass model and effort
explicitly from the routing table. No claim of task-cost or speed improvement is
made: compare available usage and elapsed time through accepted completion,
including child work and review fixes, on real checkpoints without extra Wine runs.

Configuration shape and precedence were checked against the official
[custom-agent documentation](https://learn.chatgpt.com/docs/agent-configuration/subagents#custom-agents)
and [config precedence](https://learn.chatgpt.com/docs/config-file/config-basic#configuration-precedence).
Validation: Python's installed `pip._vendor.tomli` parsed all ten TOML files;
the nine role files have matching names and all required fields.
`git diff --check` passed. Codex CLI 0.154.0 `doctor --summary --ascii --no-color`
reported configuration loaded; its overall exit 1 was due to the noninteractive
`TERM=dumb` terminal, with unrelated optional MCP notes. This is syntax/config
loading evidence, not a live execution of each custom role. No production build,
test suite, Wine or game execution was needed for this configuration change.

Independent Sol/high review identified two scope ambiguities, both corrected:
design proposals must be marked pending parent ratification, and diagnosis-only
briefs must not cause the deep implementation agent to edit. User feedback raised
support and verification to Terra/medium for evidence reconciliation and
reliable failure interpretation. All nine role model/effort settings
were checked against the routing table after these changes.


### Disassembly routing refinement

The user approved Astra/medium for `disassemble`, replacing Sol/high: targeted
questions can still require substantial interpretation of undocumented engine
behavior. `disassemble_deep` uses Astra/high for uncertain ABI, lifetime and
hook-safety contracts. A separate role is needed because custom-role model/effort
settings override explicit spawn values. Both roles remain analysis-only with
findings in the owning RE note; neither patches production code. This is a
workload-based policy change, not a measured performance claim.

Validation: all eleven TOML files parse with `pip._vendor.tomli`; all ten role
names, required fields and model/effort settings match the routing table.
The scoped diff and `git diff --check` pass. No build or Wine run is needed.
