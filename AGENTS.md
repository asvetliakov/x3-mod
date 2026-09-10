# Project instructions and session continuity

Read `docs/status.md`, `docs/user-objective.md`, and the architecture/reverse-engineering
notes before continuing. This is X3: Albion Prelude, x86, in the Steam bottle of
**CrossOver Preview.app**, not CrossOver.app. The game executable is
`~/Library/Application Support/CrossOver/Bottles/Steam/drive_c/X3/X3AP.exe`.

User preferences recorded 2026-09-10:

- Work in testable iterations. TAA is required; spatial AA alone does not satisfy it.
- Commit after each logical checkpoint, including relevant documentation and
  verification evidence. Keep generated build products and raw captures untracked.
- Review code changes before checkpoint commits. Use an independent reviewer
  when practical, fix concrete findings, and rerun affected verification.
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
- Close unused launcher menus promptly: they sit above other windows.
- For future game tests beyond menus, tell the user what is needed and let them
  launch/load the scene. Do not autonomously repeat gameplay launches. Menu-only
  work may be performed directly. Desktop automation did not reliably deliver
  input to the game's DirectInput menus; the user supplied the flight captures.

Use reversible app-local installs and process-local DLL overrides. Preserve the
EXE/CAT/DAT files and unrelated bottle settings. Do not claim HDR/TAA/lighting are
implemented when only capture or API capability testing exists. Keep extracted
copyrighted game shader bytes/decompiler output local and untracked. Derived
names, hashes and technical findings may be documented.
