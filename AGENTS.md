# Project instructions and session continuity

The orchestrator reads `docs/status.md`, `docs/user-objective.md`, and the relevant
architecture/reverse-engineering notes when resuming the project. Secondary
agents read these binding instructions, the short current status, and the files
needed for their assigned task; their brief supplies the relevant objective.
Do not load historical status/review archives or the entire conversation for a
bounded subtask unless a specific unresolved question requires them.
This is X3: Albion Prelude, x86, in the X3 bottle of
**CrossOver Preview.app**, not CrossOver.app. The game executable is
`~/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/X3AP.exe`.
Use X3 for new fixture runs as well as gameplay. Legacy runners still default
to Steam internally, so set `X3M_FIXTURE_BOTTLE=X3` explicitly (user update,
2026-09-13); do not routinely repeat verification on both bottles.

User preferences recorded 2026-09-10:

- Native Windows/Direct3D is a required target alongside CrossOver Preview
  (added 2026-09-11). Rendering features must support both; forwarding without
  the enhancements is not Windows feature support. Use documented Windows/D3D
  APIs in shared production code. Do not make Wine/CrossOver-specific exports,
  internal locks, layouts or patched binaries a renderer prerequisite. Isolate
  any optional backend-specific adapter behind an explicit capability boundary
  and provide a portable/native-Windows implementation for required features.
  The user cannot test Windows currently: distinguish Windows-compatible source
  and cross-compilation from verified behavior on native Windows. Track remaining
  portability gaps in `docs/architecture/platform-portability.md`.
  Normal feature support must not depend on exact DLL hashes, including
  Wine/CrossOver, system D3D and bundled game DLLs. Replace private-layout
  dependencies with documented API
  contracts/capability checks; do not merely remove hashes from code that still
  reads fixed private offsets. Hashes remain useful as test provenance.
- Game EXE/DLL internals remain in scope (clarified 2026-09-11). Trampolines,
  instruction patches, vtable hooks and private game structures are allowed.
  Disassemble/decompile the relevant game code when needed and document the
  findings. This applies generally to bloom and all later features: use
  targeted disassembly to resolve uncertain engine behavior, not only to
  investigate observed bugs (user clarification, 2026-09-13).
  The ban on backend-private prerequisites does not restrict game
  modification; retain appropriate hook-site, ABI and layout validation.
- Work in testable iterations. TAA is required; spatial AA alone does not satisfy it.
- Commit after each logical checkpoint, including relevant documentation and
  verification evidence. Keep generated build products and raw captures untracked.
- Review code changes before checkpoint commits. Use an independent reviewer
  when practical, fix concrete findings, and rerun affected verification.
- Include a performance pass for new code: inspect per-draw work, allocations,
  repeated validation and locking; measure suspected hotspots and fix avoidable
  cost without weakening correctness. Separate diagnostic timings from game FPS.
- Batch tracing needs into a consolidated diagnostic build before asking for
  another load/test cycle. Use targeted disassembly where needed. Investigate
  loading time alongside rendering; track the double cursor after alt-tab and
  verify it explicitly when changing window/presentation behavior.
- Keep logically grouped disassembly/architecture findings in separate documents.
- Document source. Keep verification/probes/test assets separate from production.
- Name generated C/C++ include fragments `*_inc.h`, not `*.inc`, so the editor
  applies syntax highlighting.
- Use SSE2 for our x86 CPU floating-point arithmetic (`-msse2 -mfpmath=sse`),
  preserving the game's calling ABI. Do not reproduce x87 arithmetic unnecessarily.
  Use `-mstackrealign -mincoming-stack-boundary=2` for the four-byte incoming
  stack contract; do not assume legacy callbacks arrive aligned to 16 bytes.
  Newer SIMD requires verified runtime support and a measured benefit; do not
  enable fast-math globally.
- Subagents may be used when helpful for independent research/context management.
- Agent model allocation (user preference, 2026-09-13): use `gpt-5.6-sol`
  with `high` reasoning for bounded log analysis, documentation, routine
  verification and artifact checks. Use `gpt-5.6-sol` with `high` for
  independent reviews, including code/correctness (user update, 2026-09-13:
  `xhigh` takes too long). Do not default reviews to `xhigh`; escalate difficult
  or consequential findings to the Astra orchestrator. Use `gpt-6-astra`
  for implementation, planning, architecture and difficult debugging.
  Apply this split to new agents; do not interrupt a useful in-flight run
  just to change models. Give secondary agents a focused task and the needed
  files/evidence instead of duplicating the entire conversation history.
- Close unused launcher menus promptly: they sit above other windows.
- Never launch the game, including menu-only tests (user clarification,
  2026-09-13). Tell the user what is needed and let them launch/load the scene.
  Launch-command validation with `--dry-run` is allowed. Desktop automation did
  not reliably deliver input to the game's DirectInput menus; the user supplied
  the flight captures.

Use reversible app-local installs and process-local DLL overrides. Preserve the
EXE/CAT/DAT files and unrelated bottle settings. Do not claim HDR/TAA/lighting are
implemented when only capture or API capability testing exists. Keep extracted
copyrighted game shader bytes/decompiler output local and untracked. Derived
names, hashes and technical findings may be documented.

Wine runner lock (added 2026-09-12): every command that executes under Wine
(a `run_*.py` runner or a hand-started fixture `.exe`) must be wrapped as
`python3 verification/probe/wine_lock.py <command...>`, which serialises on
`/tmp/x3-wine-runner.lock` and waits for the holder. `game_guard.py` still
refuses to start while the game is up. Never run two Wine commands at once,
even from different agents.

Context discipline for agents (added 2026-09-12):

- Never read large files whole: anything over about 50 KB under
  `verification/results/`, capture logs, shader dumps and transcripts. Query them
  with a short Python, `jq` or `grep` command that prints only the needed fields
  or rows. Validate produced JSON with a script or its paired pytest, not by
  reading it back.
- Reports to the orchestrator carry numbers, conclusions and paths, never pasted
  file contents. The main session keeps architecture decisions; subagents keep
  the bulk reading.

Proportional verification and evidence (user-requested workflow simplification,
2026-09-13; rationale in `docs/verification/workflow-audit-2026-09-13.md`):

- Small changes: review the delta and run only affected tests/compilation. A
  default, CLI or prose edit does not require a full suite, Wine run, whole-tree
  manifest or new artifact review. Documentation-only changes need ordinary
  factual/link review, not a separate code reviewer.
- Hooks/ABI/lifetime/GPU changes: retain checks for the actual failure modes—
  instruction boundaries, CPU/LastError preservation, rollback, native parity,
  state/Reset/recovery and relevant performance. Reuse unchanged evidence; do
  not rerun unrelated fixtures or an included focused suite after full discovery.
- Use one reviewer for the logical code change and its evidence when practical.
  Fix findings and rerun affected checks; do not create separate routine source,
  artifact and post-install review rounds. Escalate an actual mismatch or an
  unresolved concern rather than adding checks to every checkpoint.
- For an install candidate, one owner makes one clean build from reviewed
  production inputs and retains that DLL. Runners must not rebuild it implicitly.
  Select integration tests by changed dependencies; reserve full-project chains
  for shared infrastructure changes or deliberate release checkpoints. No
  benchmark or broad regression rerun without a relevant change or open concern.
- Bind the candidate to its Git commit (and dirty diff if unavoidable), toolchain,
  DLL hash and scoped test results in one compact record. Do not generate nested
  evidence manifests, duplicate whole-tree hash maps or repeated successful
  artifact copies. Before/after input hashes are useful for genuinely concurrent
  uncommitted runs, not a requirement for every short command.
- Keep one previous DLL/manifest for rollback; check installed bytes and preserve
  EXE/bottle configuration. One affected launch `--dry-run` suffices; add vanilla
  only when launcher/config behavior changed. Never launch the game ourselves.
- Track compact results and useful small failure witnesses. Keep verbose logs,
  readbacks and temporary successful/rejected copies local. Preserve existing
  historical evidence; do not rewrite Git history as routine cleanup.
- `docs/status.md` stays a short current handoff; history lives in linked archives.
  Update `docs/goals.md` when goal/acceptance state changes, not for each test.
  Update the owning architecture/verification note instead of mirroring the same
  hash and prose across status, goals, roadmap, handoff and several new reviews.
- Keep one Wine queue/lease owner. A focused agent returns findings and paths;
  it must not create a parallel global qualification or rebuild shared artifacts
  owned by another task. Record simple command/lock timings when available; do
  not infer agent effort from overlapping timestamp spans.

Canonical full host-test command, only when that scope is justified:

```sh
PYTHONPATH=verification/probe python3 -m unittest discover -s verification/analysis -p 'test_*.py'
```

For focused checks, use the same `PYTHONPATH` with the selected unittest modules.

Test coordination (added 2026-09-12):

- The game now lives in the CrossOver bottle `X3` (`WineArch = arm64`: CrossOver
  Preview's native arm64 Wine with FEX x86 emulation, `FEX_X87REDUCEDPRECISION=1`,
  `WINEMSYNC=1`). The old `Steam` bottle (x86_64 Wine under Rosetta) remains.
  `tools/manage.py` launches into `X3` (`X3M_BOTTLE` overrides). The fixture
  runners under `verification/probe/` select their bottle through
  `verification/probe/bottle.py`: `X3M_FIXTURE_BOTTLE` (default `Steam`, so the
  recorded results stay comparable). **User update, 2026-09-13: new verification
  needs only the X3 bottle. Set `X3M_FIXTURE_BOTTLE=X3` for all new fixture
  invocations; do not run a Steam counterpart as a routine acceptance step.**
  Existing Steam evidence stays as historical provenance. Any other bottle writes its records under
  `verification/results/bottle-<name>/`, and every summary records the bottle
  name, WineArch and the two emulation environment lines. See
  `docs/verification/bottles.md` for the X3 validation record.
- One Wine runner at a time, across all agents and the user's game.
  `wine_lock.py` now checks for the game and competing project runners/fixtures
  from one process snapshot after acquiring the lease; a failed inventory
  refuses execution. This replaces the separate manual preflight scans.
  Existing runner-local game guards remain as close-to-execution protection.
  Use bounded waits of at most 60 seconds and keep the user informed while
  waiting rather than ending the turn.
