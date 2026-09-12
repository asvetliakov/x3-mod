# Handoff: gz read-ahead buffer for the savegame decode (paused 2026-09-12)

**Superseded 2026-09-12 (evening):** implemented; see
[gz-buffer.md](gz-buffer.md) and
[savegame-gz-stream.md](../reverse-engineering/savegame-gz-stream.md). Kept as
the record of the plan and of the facts established before implementation.

Paused on the orchestrator's request before any source edit. Nothing in this
task is implemented yet; the tree compiles unchanged. Decompiler output lives
only under `/tmp/x3-gz-study/` (xrefs.txt so far; never commit it).

## Brief (unchanged)

Cut the 25.3 s savegame-decode stall on the FEX bottle: 13,882,714 `gzread`
calls at 3.11 B each (9.1 s inside zlib1.dll, 0.656 µs per call) plus 150,279
`inflate` calls, all from the read dispatcher `0x004e9210`
([profile](../reverse-engineering/loading-profile-bottle-x3.md), rank 1). Plan:
a proxy-side read-ahead buffer (256 KB default) in front of the gz imports,
gated by `X3M_GZ_BUFFER=1` / `X3M_GZ_BUFFER_KB` and `tools/manage.py --gz-buffer`.

## Disassembly facts established so far (Ghidra, read-only)

* The bundled `zlib1.dll` is **zlib 1.2.3** (gzio.c era). Consequences for the
  buffer's semantics: `gzgetc` is a 1-byte `gzread` (`-1` at EOF/error);
  `gztell` is `gzseek(file, 0, SEEK_CUR)` and returns the uncompressed output
  position; forward `gzseek` reads and discards immediately (not lazy),
  backward `gzseek` rewinds and re-inflates; `gzread` returns the partial count
  when an error occurs after some bytes were produced in that call and `-1`
  only when nothing was produced (the error is sticky: the next call returns
  `-1`); after `Z_STREAM_END` every `gzread` returns 0; a write-mode file gets
  `Z_STREAM_ERROR` (-2) from `gzread`.
* IAT slots (main module) and their thunks: `gzopen` 0x5323d8 / thunk
  0x4fae64, `gzwrite` 0x5323e0 / 0x4fae46, `gzread` 0x5323e4 / 0x4fae40,
  `inflate` 0x5323e8 / 0x4fae5e, `gztell` 0x5323ec / 0x4fae58, `gzclose`
  0x5323f0 / 0x4fae52, `gzgetc` 0x5323f4 / 0x4fae3a, `gzseek` 0x5323fc /
  0x4fae2e. Every import is reached only through its thunk.
* Callers of the thunks (one function each for the read side):
  * `gzopen`: only `0x004e90f0` (open wrapper; called from 0x404530, 0x475b10,
    0x472610, 0x4ab880, 0x405280, 0x404cc0).
  * `gzread`: only the read dispatcher `0x004e9210` (35 callers, among them
    0x4e8880, 0x4a0880, 0x4e9420, 0x4e9470, 0x4e93f0).
  * `gzgetc`: only `0x004e91b0`, which is called only by `0x004e8880`.
  * `gztell` and `gzseek`: only `0x004e8880` (the 1 KiB-per-`inflate` archive
    reader of [loading orchestration](../reverse-engineering/loading-orchestration.md);
    callers 0x4f7830 and the resource loader 0x4e8e10). So seek/tell interleave
    with `gzgetc`/`gzread` inside 0x004e8880, not in the 3-byte loop.
  * `gzclose`: 0x4ec9e0, 0x4e7350, 0x4a5940, 0x405280, 0x4e9360 (close
    dispatcher; callers 0x404530, 0x4f7830, 0x4e90f0, 0x4e8780, 0x4e73f0,
    0x4e8ed0), 0x4a52a0.
  * `gzwrite`: 0x417610, 0x419430, 0x46f1c0, 0x473e10, 0x479010, 0x4e92e0
    (write dispatcher, ~55 callers), 0x49f930, 0x4a52a0.
* Not yet decompiled (next step): 0x004e9210, 0x004e90f0 (which mode strings
  reach `gzopen`), 0x004e8880 (the seek/tell/getc pattern: absolute vs
  relative seeks, whether it seeks backward), 0x004e91b0, 0x004e9360 and
  0x004e92e0 (whether one handle is ever both written and read).

## Facts about our hook machinery relevant to the implementation

* `src/proxy/loading_trace.cpp`: `Hook hooks[]` table (line ~410) is indexed by
  `Operation` (`loading_trace.h`), with `static_assert(import_count ==
  Operation::MeshPointReps)`; new table rows need new `Operation` values
  inserted before `MeshPointReps` (reports name operations by `hooks[i].name`).
* `install()` returns early unless `X3M_TELEMETRY=1` (`requested()`), and
  `capture.cpp:1311` calls `loading_trace::initialize()` only when telemetry is
  enabled. The buffer must have its own gate: initialize when telemetry OR
  `X3M_GZ_BUFFER=1` is set, and when telemetry is off patch only the gz rows
  (leave `mesh_observation_enabled` and the other hooks alone).
* A Wine fixture runner belonging to another agent (`run_sampling_profiler.py`,
  bottle X3) was active during this session; keep the one-runner rule.

## Planned design (decided, not written)

`src/proxy/gz_buffer.{h,cpp}`: fixed table of 32 slots `{atomic<void*> file,
State*}` (lock-free lookup on the read path, exclusive SRWLOCK only at
open/close); a file opened with a mode containing `w`/`a`, or when the table
is full, is passed through. State: `buf, capacity, fill, pos, base` where
`base` is the logical position of `buf[0]`, refreshed from the real `gztell`
at every refill and after every real seek; logical position = `base + pos`.
`gzread`: `(int)size < 0` returns -1 (zlib check); copy from the buffer;
remainder ≥ capacity is read directly into the caller's buffer; otherwise
refill with one real `gzread(capacity)`; real `-1` returns the bytes already
copied in this call if any, else -1; real 0 ends the call (short read).
`gzgetc` = 1-byte path. `gzseek`: SEEK_CUR translated to `base+pos+offset`,
served in place when `base <= target <= base+fill`, otherwise drop the buffer
and call the real `gzseek(target, SEEK_SET)` (other `whence` values pass
through after dropping). `gzclose`: one summary log line (bytes served, real
reads, small calls), free, real close. Registration through the existing hook
table with four new rows (`gzgetc`, `gztell`, `gzclose`, `gzwrite`).

## Exact next step

1. Decompile the six functions above:
   `JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home
   /opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless /tmp/x3-ghidra-research
   X3Render -process X3AP.exe -noanalysis -readOnly -scriptPath tools/analysis
   -postScript X3DecompileFunctions.java /tmp/x3-gz-study/decompile.txt 004e9210
   004e90f0 004e8880 004e91b0 004e9360 004e92e0` (retry after `sleep 90` if
   the project is locked), then write
   `docs/reverse-engineering/savegame-gz-stream.md` from the findings.
2. Implement `gz_buffer.{h,cpp}`, the table rows and gate, `--gz-buffer`,
   the fixture against the bottle's `zlib1.dll` (1.2.3), its pytest, results,
   `docs/verification/gz-buffer.md`, then run the suite chain (one runner).
