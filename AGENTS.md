# Project instructions and session continuity

Read `docs/status.md`, `docs/user-objective.md`, and the architecture/reverse-engineering
notes before continuing. This is X3: Albion Prelude, x86, in the X3 bottle of
**CrossOver Preview.app**, not CrossOver.app. The game executable is
`~/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/X3AP.exe`.
The Steam bottle remains the default for fixtures, not gameplay.

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

Test coordination (added 2026-09-12):

- The game now lives in the CrossOver bottle `X3` (`WineArch = arm64`: CrossOver
  Preview's native arm64 Wine with FEX x86 emulation, `FEX_X87REDUCEDPRECISION=1`,
  `WINEMSYNC=1`). The old `Steam` bottle (x86_64 Wine under Rosetta) remains.
  `tools/manage.py` launches into `X3` (`X3M_BOTTLE` overrides). The fixture
  runners under `verification/probe/` select their bottle through
  `verification/probe/bottle.py`: `X3M_FIXTURE_BOTTLE` (default `Steam`, so the
  recorded results stay comparable). Any other bottle writes its records under
  `verification/results/bottle-<name>/`, and every summary records the bottle
  name, WineArch and the two emulation environment lines. See
  `docs/verification/bottles.md` for the X3 validation record.
- One Wine runner at a time, across all agents and the user's game: before a
  suite, confirm `game_guard.game_running()` prints `[]` and no other
  `verification/probe/run_*.py`, fixture `.exe` or `wine ... fixture` process
  exists; use bounded waits of at most 60 seconds and keep the user informed
  while waiting rather than ending the turn.
