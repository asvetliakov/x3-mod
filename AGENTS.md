# Project contract

Binding for every agent and tool working on this repository. Claude Code adds
its routing in `CLAUDE.md`; Codex model allocation is in the last section.
Superseded rules and their history are in `docs/archive/` and
`docs/verification/workflow-audit-2026-09-13.md`.

## Reading on entry

The orchestrator reads `docs/status.md`, `docs/goals.md`,
`docs/user-objective.md` and the owning architecture or reverse-engineering
note for the task at hand. A secondary agent reads this file and the files named
in its brief; the brief carries the objective and relevant current status facts.
Do not load historical archives or the whole conversation for a bounded subtask unless
a specific unresolved question requires it.

## Target and environment

- X3: Albion Prelude, x86, non-relocatable EXE at
  `~/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/X3AP.exe`,
  in bottle **X3** of **CrossOver Preview.app** (not CrossOver.app; arm64 Wine
  with FEX, `FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`). The old `Steam`
  bottle exists only as historical provenance.
- All fixture work uses X3: set `X3M_FIXTURE_BOTTLE=X3` explicitly, because
  the runners under `verification/probe/` still default to Steam. Never run a
  Steam counterpart as an acceptance step. Any other bottle writes under
  `verification/results/bottle-<name>/`; every summary records bottle name,
  WineArch and the two emulation environment lines (`docs/verification/bottles.md`).
- Native Windows/Direct3D is a required target alongside CrossOver. Features
  must work on both; forwarding without the enhancement is not Windows support.
  Shared production code uses documented Windows/D3D APIs only; any optional
  backend-specific adapter sits behind an explicit capability boundary with a
  portable implementation for required features. No renderer prerequisite may
  depend on Wine/CrossOver-specific exports, internal locks, layouts, patched
  binaries or exact DLL hashes (hashes remain useful as test provenance).
  Replace private-layout dependencies with documented contracts and capability
  checks, not by merely dropping the hash. The user cannot test Windows:
  distinguish Windows-compatible source and cross-compilation from verified
  native behavior, and track gaps in `docs/architecture/platform-portability.md`.
- Game EXE/DLL internals are in scope: trampolines, instruction patches, vtable
  hooks and private game structures, with hook-site, ABI and layout validation.
  Use targeted disassembly to resolve uncertain engine behavior, not only to
  chase observed bugs, and document findings under `docs/reverse-engineering/`.

## Never launch the game

The agent never launches the game, including menu-only tests; `--dry-run`
launch validation is allowed. Tell the user what is needed and let them
launch, load and report; the user supplies flight captures. Close unused
launcher menus promptly. Installs are reversible and app-local with
process-local DLL overrides; preserve EXE/CAT/DAT files and unrelated bottle
settings.

## Wine lock and test coordination

- Every command that executes under Wine (a `run_*.py` runner or a
  hand-started fixture `.exe`) runs as
  `python3 verification/probe/wine_lock.py <command...>`. It serialises on
  `/tmp/x3-wine-runner.lock`, waits for the holder, and refuses when the game
  or a competing runner is up. Never run two Wine commands at once, from any
  agents. Wait in bounded steps of at most 60 s and keep the user informed
  rather than ending the turn.
- One Wine queue and one install-candidate owner. A focused agent returns
  findings and paths; it must not start a parallel qualification or rebuild
  shared artifacts owned by another task. Runners never rebuild a frozen
  candidate DLL.
- Canonical full host suite, only when that scope is justified:

  ```sh
  PYTHONPATH=verification/probe python3 -m unittest discover -s verification/analysis -p 'test_*.py'
  ```

  Focused checks use the same `PYTHONPATH` with selected modules.

## Code rules

- Work in testable iterations. TAA is required; spatial AA alone does not count.
- x86 CPU arithmetic uses SSE2 (`-msse2 -mfpmath=sse`), preserving the game's
  calling ABI; do not reproduce x87 unnecessarily. Use
  `-mstackrealign -mincoming-stack-boundary=2` for the four-byte incoming stack
  contract; legacy callbacks are not 16-byte aligned. Newer SIMD needs verified
  runtime support and a measured benefit; never enable fast-math globally.
- Every new piece of code gets a performance pass: per-draw work, allocations,
  repeated validation, locking; measure suspected hotspots and fix avoidable
  cost without weakening correctness. Diagnostic timings are not game FPS.
- Batch tracing needs into one consolidated diagnostic build before asking for
  another load/test cycle. Investigate loading time alongside rendering. Track
  the double cursor after alt-tab and verify it whenever window or presentation
  behavior changes.
- Generated C/C++ include fragments are `*_inc.h`, not `*.inc`.
- Keep verification, probes and test assets separate from production source.
  Keep build products, raw captures, extracted copyrighted shader bytes and raw
  decompiler output local and untracked; derived names, hashes and technical
  findings may be documented.
- Do not claim HDR, TAA or lighting are implemented when only capture or API
  capability testing exists.

## Verification, proportional to the change

- Small change (default, CLI, prose): review the delta and run only affected
  tests or compilation. No full suite, Wine run, whole-tree manifest or
  artifact review. Documentation-only changes need factual and link review,
  not a code reviewer.
- Hook, ABI, lifetime or GPU change: check the real failure modes, meaning
  instruction boundaries, CPU/LastError preservation, rollback, native parity,
  state/Reset/recovery and relevant performance. Reuse unchanged evidence; do
  not rerun unrelated fixtures, or a focused suite already included in a full
  discovery run.
- One independent reviewer per logical change, covering source and its
  evidence together, when practical. Fix findings and rerun affected checks.
  No separate routine source, artifact and post-install review rounds;
  escalate a real mismatch instead of adding checks everywhere. If the
  implementing agent has finished its turn, resume its task for review fixes;
  a message alone does not start a turn.
- Install candidate: one owner makes one clean build from reviewed, committed
  production inputs and retains that DLL. Select integration tests by changed
  dependencies; reserve the full fixture chain for shared infrastructure
  changes or a deliberate release checkpoint. No benchmark or broad regression
  without a relevant change or open concern. Bind the candidate to its commit
  (and dirty diff if unavoidable), toolchain, DLL hash and scoped results in
  one compact record; no nested evidence manifests, whole-tree hash maps or
  repeated artifact copies. Recheck the hash before install, keep one previous
  DLL and manifest for rollback, verify installed bytes, and confirm EXE and
  bottle configuration are unchanged. One affected launch `--dry-run` suffices;
  add vanilla only when launcher or config behavior changed.
- Evidence: track compact results and small failure witnesses; keep verbose
  logs, readbacks and successful or rejected copies local. Preserve existing
  history; no routine Git history rewriting. Record command and lock timings
  when available; do not infer agent effort from overlapping timestamps.
- Commit after each logical checkpoint with its documentation and evidence,
  after review.

## Documentation

- `docs/status.md` is the short current handoff and the only place the
  installed build is described; `docs/goals.md` tracks acceptance state and
  changes only when that state changes. Update the owning architecture,
  reverse-engineering or verification note instead of mirroring the same facts
  across status, goals, roadmap, README, handoffs and run plans.
- One verification ledger per feature under `docs/verification/`; append to it.
  The numbered `review-NN` and `iteration-NN` series is closed. Keep logically
  grouped disassembly and architecture findings in separate topic documents.
- `docs/verification/user-runs.md` holds open runs and the completed-run table.
  Completed instructions, superseded handoffs, pause snapshots and status
  archives live in `docs/archive/`.

## Context discipline

- Never read a large file whole: anything over about 50 KB under
  `verification/results/`, capture logs, shader dumps, transcripts. Query it
  with a short Python, `jq` or `grep` that prints only the needed rows.
  Validate produced JSON with a script or its paired test, not by reading it.
- Reports to the orchestrator carry numbers, conclusions and paths, never
  pasted file contents. The orchestrator keeps architecture and install
  decisions; secondary agents get a focused task with the needed files and
  evidence, never the whole conversation history. Do not interrupt a useful
  in-flight run to change models.

## Codex model allocation

Project defaults live in `.codex/config.toml`; specialist definitions live in
`.codex/agents/`. Main session: **gpt-6-astra / medium**, Standard processing.
Keep the main model/effort stable during routine orchestration. The main session
owns architecture decisions, integration, the install candidate and Wine queue.
Claude Code retains its model routing in `CLAUDE.md`.

| Task | Codex agent | Model / effort |
| --- | --- | --- |
| Understood implementation with a meaningful acceptance check | `implement` | gpt-5.6-sol / medium |
| Uncertain hooks, ABI, lifetime, GPU recovery or difficult debugging | `implement_deep` | gpt-6-astra / high |
| Independent bounded source/evidence review | `review` | gpt-5.6-sol / high |
| Consequential hook/ABI review | `review_deep` | gpt-6-astra / high |
| Hard design decision or cross-system diagnosis | `design` | gpt-6-astra / high |
| Targeted disassembly with a precise question | `disassemble` | gpt-5.6-sol / high |
| Log/capture triage and evidence comparisons | `triage` | gpt-5.6-terra / medium |
| Named checks, hashes and counts | `verify` | gpt-5.6-terra / medium |
| File discovery and factual documentation updates | `support` | gpt-5.6-terra / medium |

Route by uncertainty and consequence: an understood mechanical GPU edit can use
`implement`; discovering or validating difficult invariants uses `implement_deep`.
Use Sol/high for deeper tracing of an understood task; use Astra/medium or high
when the explanation fails, assumptions conflict or the question crosses systems.
Start clearly difficult work on Astra/high instead of trying every tier. Reserve
xhigh/max for a specific unresolved question with evidence that more reasoning
helps; missing evidence requires inspection or a fixture. Fast mode is opt-in
when model latency is the bottleneck, never a default for waiting on fixtures.

## Codex delegation and reporting

- Delegate independent substantial work to the roles above when it reduces
  elapsed time or keeps bulk evidence out of the main context. Do short dependent
  operations locally. At most three concurrent children; children do not spawn
  agents. Keep one writer per file set and one Wine/install owner.
- Brief each agent with the goal, relevant current facts, exact files, constraints,
  acceptance command and observable result. Start bounded work and independent
  reviews with fresh context, not a full-history fork. Supply model and effort
  explicitly when using direct collaboration tools without custom-role selection.
- Source-editing agents use an isolated checkout when concurrent writes could
  conflict. Worktree isolation is not implicit in a spawn. Build/fixture work
  stays with the main checkout's queue owner; do not replicate raw result trees.
- Resume the same agent for local corrections and review fixes. With the current
  collaboration tools, `send_message` steers running work; `followup_task` also
  starts an idle agent. Escalate a failed explanation or repeated substantive
  failure with a compact witness; a typo/setup failure alone does not require a
  fresh stronger agent. Do not interrupt useful work merely to switch models.
- Choose reviewer depth upfront. A clean review alone does not trigger a second
  review. Review source and evidence together under the proportional rules above.
- Reports use **Outcome**, **Evidence**, **Files changed**, **Open issues**, at
  most 25 lines. Include commands, counts, paths and material limitations. Put
  longer findings in the owning note; never omit a material finding to meet the
  limit. Distinguish measured facts from inference. No pasted logs or task recap.
- Agents run affected checks and fix in-scope defects; broaden only for changed
  code, a failure or an unresolved concern. They do not commit, install, rebuild
  a frozen DLL or run Wine unless the owner explicitly assigns that operation.
- Compare routing over real checkpoints using available usage, elapsed time,
  retries and substantive review fixes through accepted completion. Include
  child work; distinguish model time from tool/lock/user waits. Do not add Wine
  runs or an accounting framework just to benchmark models. No task-cost savings
  are established yet.
