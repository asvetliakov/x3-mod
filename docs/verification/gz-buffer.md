# gz read-ahead buffer (X3M_GZ_BUFFER)

`src/proxy/gz_buffer.{h,cpp}` is a read-ahead buffer in front of the game's
`zlib1.dll` gz imports, routed through the loading-trace import hooks
(`src/proxy/loading_trace.cpp`). It targets the savegame decode: the read
dispatcher `0x004e9210` issues one `gzread` per field, 13.9 M calls of 3.11 B
on a load ([loading-profile-bottle-x3.md](../reverse-engineering/loading-profile-bottle-x3.md),
[savegame-gz-stream.md](../reverse-engineering/savegame-gz-stream.md)).
The buffer answers those calls from 256 KB chunks and keeps the semantics of
the bundled zlib 1.2.3 observable through every hooked import.

## Switches and gate

* `X3M_GZ_BUFFER=1` (`tools/manage.py launch --gz-buffer`), chunk size
  `X3M_GZ_BUFFER_KB` (`--gz-buffer-kb`, 1..65536, default 256). No
  `--telemetry` needed: `capture.cpp` initializes the loading-trace machinery
  when either switch is set, and with telemetry off `install()` patches only
  the six rows the buffer needs (`gzopen`, `gzread`, `gzseek`, `gzgetc`,
  `gztell`, `gzclose`); `gzwrite`, `inflate` and every non-zlib row stay
  unpatched, and the mesh observation / cache / adjacency services are not
  started. The session log shows `gz_buffer requested=1 enabled=…
  capacity_kb=… telemetry=… imports=… rewind=…` followed by the
  `loading_hook` lines and a `loading_trace … scope=gz_buffer` line.
* With telemetry on and the buffer on, the buffer's real calls go through the
  traced wrappers: the `GzRead` metric then counts the chunk reads, and the
  calls served from memory are in the per-file summary line.
* With the buffer off nothing changes: the `gzread` hook is the traced
  wrapper of before (same timing), and the four new rows (`GzGetc`, `GzTell`,
  `GzClose`, `GzWrite`, inserted before `MeshPointReps`) are traced the same way.
* Only handles opened with a read mode are buffered (the last of `r`/`w`/`a`
  in the mode string, as zlib parses it; the game passes `"rb"` and `"wb"`).
  A write handle, an unknown handle, a `gzopen` failure and the 33rd
  concurrent read handle (fixed 32-slot table) pass straight through. A
  `gzopen` that returns a pointer still registered from an earlier handle
  (one closed behind the hooks, so zlib reused the allocation) resets that
  slot in place instead of registering a shadowed duplicate (review 25).
  `X3M_GZ_BUFFER_KB` is parsed as decimal digits only; any other text, `0`
  or an unset variable gives the 256 KB default, and the result is clamped to
  1..65536 (review 25: a signed value no longer wraps to the maximum).
* No per-call allocation, lock or log: the slot lookup is a hint index plus a
  scan of 32 atomic pointers, the state is per handle (one thread per handle,
  zlib's own contract), the buffer is one `malloc` at open and one `free` at
  close, and the only log line per file is written at `gzclose`
  (`gz_buffer_file slot= capacity= calls= small_calls= served_bytes=
  real_reads= real_bytes= direct_reads= getcs= tells= seeks= seeks_served=
  seeks_real= error= end= close=`) under `PreserveCpuState`. The hot path has
  no floating point; `check_no_x87.py` walks `gz_read`, `gz_getc`, `gz_tell`
  and `gz_seek` next to the light setter hooks and passes on the built DLL.

## Semantics (zlib 1.2.3 gzio.c, verified against the real DLL)

The real stream sits at `R = base + fill`; the caller's logical position is
`L = base + pos <= R`. After every real read the arithmetic position is
checked against the real `gztell` (once per chunk; it is also the error
probe). A real error is sticky in zlib; the buffer makes it visible only when
`L` reaches `R`, because an unbuffered stream would have detected it in the
call that produced its last byte.

| import | zlib 1.2.3 | buffered behaviour |
|---|---|---|
| `gzread(f, p, n)` | inflates up to `n` bytes; returns the count, `-1` only when nothing was produced and the stream is in `Z_DATA_ERROR`/`Z_ERRNO` (sticky), `0` after `Z_STREAM_END`; `Z_STREAM_ERROR` (-2) on a write handle; no `(int)n < 0` check (that arrived in 1.2.4) | copies from the buffer; a remainder `>= capacity` is read straight into the caller's memory; otherwise one real `gzread(capacity)`; loops until satisfied or the real stream returns `<= 0`; real `-1` gives the bytes copied in this call if any, else `-1`; real `0` gives the short count; `n = 0` returns `-1` only while the error is visible |
| `gzgetc(f)` | `gzread(f, &c, 1) == 1 ? c : -1` | the 1-byte path (byte from the buffer or `-1`) |
| `gztell(f)` | `gzseek(f, 0, SEEK_CUR)`: `-1` in the error state, else the output position | `-1` while the error is visible, else `base + pos` |
| `gzseek(f, off, SEEK_SET/SEEK_CUR)` | `-1` in the error state or for a negative target; forward: read-and-discard immediately (`-1` if it runs into the end, position where it stopped); backward: rewind and re-inflate; `SEEK_CUR` relative to the output position | target inside `[base, R]`: cursor move only; outside: drop the buffer and real `gzseek(target, SEEK_SET)` (a backward target while the real stream holds a not-yet-visible error first calls `gzrewind`, resolved with `GetProcAddress`, which is what zlib's own backward seek does); the result resyncs `base` (`gztell` after a failure) |
| `gzseek(f, off, SEEK_END)` | always `-1`, no movement (every zlib version) | real stream realigned to `L`, buffer dropped, real call passed through with the original arguments, `base` resynced |
| other whence | 1.2.3 treats it as absolute | same realign-and-pass-through |
| `gzclose(f)` | `0` on success | unregister, free, real close, one summary line |
| `gzwrite` | write handles only | never buffered; traced row only |
| write handle | `gzread` `-2`, `gztell` = input position | pass-through, identical |

Corner the model cannot close: a data error whose bad input lies exactly
across zlib's 16 KB input-buffer boundary is detected by the unbuffered
stream only on the *next* read call, while the buffer already knows of it
when its bytes are consumed; `gztell`/`gzseek` between those two calls would
differ (`-1` vs the position) on such a corrupted file. Not observable on a
valid file. A transparent (non-gzip) file is served like any other, except
that a 4 GB-length request on it is excluded from the fixture: Wine's msvcrt
`fread` with that count returns one CRT buffer and leaves the `FILE` unable to
read after the next `fseek` (which zlib's `gztell` performs on such a file),
while a sequential `fread` still works; the divergence is below zlib and the
game never issues such a request.

## Fixture and runner

`verification/probe/gz_buffer_fixture.cpp` links the production module and
loads the bottle's real `zlib1.dll` (`verification/probe/run_gz_buffer.py`
copies it from `bottle.game_dir()`; SHA-256
`27b95a87be89090df67f5f1e7fc88437da400b7c3b6728418d97eca71882cad5`, zlib
1.2.3, identical in the `X3` and `Steam` bottles). It generates the inputs
with the DLL's own `gzwrite` (1,000,003 B, 100 B, empty, 3 × 4096 B, a
two-member file, a truncated copy, a bad-CRC copy, a mid-stream corrupted
copy, a bad-CRC copy of the 3 × 4096 B file, and a 5,000 B non-gzip file)
and drives random sequences of `gzread` (1–8 B mostly, 1–5000 B, around the
chunk size, above it, 0, and 0xFFFFFFFF), `gzgetc`, `gztell`, `gzseek`
(`SEEK_SET` over the whole file and beyond, `SEEK_CUR` ± two chunks,
`SEEK_END` 0/−8 as the archive reader does, whence 3) through the buffer on
one handle and through the raw DLL on a second handle of the same file,
comparing every return value and byte. Chunk sizes 4 KB, 64 KB and 256 KB;
deterministic EOF/error paths; the decoder's 3-byte loop to the end; four
interleaved handles with a write handle among them; 33 handles (the 33rd
passes through). The timing case reads 30 MB (level 1) as 10 M 3-byte
`gzread` calls three ways: raw, raw inside the loading-trace hook envelope
(`CpuCallBoundary` = `fnsave`/`frstor` twice, two `QueryPerformanceCounter`
reads, last-error transport — the same envelope the profile's `gzread` hook
has, minus its admission scope and counters), and buffered.

Run (one Wine command at a time, under the lock):
`X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3
verification/probe/run_gz_buffer.py` → `verification/results/bottle-X3/gz-buffer-{fixture.txt,fixture-wine.log,summary.json}`;
`verification/analysis/test_gz_buffer.py` re-parses every recorded summary.

### Results, bottle X3 (arm64 Wine, FEX), 2026-09-12

* **735,871 checks, 0 failures** over 20 cases (804,135 buffered calls,
  164.5 MB served from 12,924 chunk reads, 24,116 seeks of which 1,311 served
  in place, 9 files ending in the error state). The run before the transparent
  4 GB-request exclusion had 94 failures, all in that one case, all after
  that request.
* **Timing, 10 M × 3-byte `gzread` (30 MB):**

  | mode | seconds | ns per call |
  |---|---|---|
  | raw `zlib1.dll` | 0.329 | **32.9** |
  | raw inside the hook envelope | 12.676 | **1,267.6** |
  | buffered (256 KB) | 0.422 | **42.2** |

  Rerun after the light rows (2026-09-12 night, same bottle, `gz-buffer-summary.json`
  of that run; [loading-probes.md](loading-probes.md) has the derivation): raw
  32.5 ns; raw + the three QPC reads of a span (`qpc`): 243.2 ns; the light
  span that now wraps every counting/timing row (`light`): 386.8 ns; the former
  `CpuCallBoundary` envelope (`hooked`): 1,237.1 ns; buffered 43.3 ns. The
  production `gzread` row therefore costs ≈ 354 ns of envelope per call instead
  of ≈ 1,205 ns, 211 ns of which are the clock reads.

## What this means for the loading stall

The profile attributed 9.103 s (0.656 µs per call) of the 25.31 s savegame
stall to `gzread` "inside zlib". The fixture shows that the DLL itself costs
33 ns per 3-byte call under FEX, while the hook envelope around it costs
about 1.27 µs (38.5× the call): **the measured per-call cost was the instrumentation** (x87
state transport under FEX; the Rosetta run's 0.204 µs with a 0.15 µs wrapper
tail agrees). Consequences:

* With `--telemetry` (every profiled run), `--gz-buffer` removes 13.9 M hook
  envelopes: about 13.9 M × 1.2 µs ≈ 16 s of the instrumented stall (the
  9.1 s the span counted plus the part of the envelope that fell into
  "unhooked" time). Profiles taken with both switches no longer carry that
  artefact, and the remaining stall is the engine's per-record
  deserialization, which no buffer reaches.
* Without telemetry the game's own cost of the 13.9 M calls is about
  13.9 M × 33 ns ≈ 0.46 s; the buffered path is 42 ns per call, so the
  plain-game gain is nil (≈ −0.13 s, within noise). The inflate work does not
  change: zlib's internal `inflate` calls fall from one per `gzread` to about
  one per 16 KB input chunk (the game's own 150,279 hooked `inflate` calls are
  the archive reader's, untouched), but they were never the cost.
* The "13.9 M × 0.6 µs ≈ 9 s" bound in the brief therefore describes the
  instrumented run only. The switch stays off by default; use it with
  `--telemetry` for loading profiles, and treat every earlier hooked
  `gzread`/`inflate` cost on the FEX bottle as an upper bound inflated by
  the envelope (the same applies to every other `CpuCallBoundary` hook on a
  tiny cross-module call: `ReadFile`, `inflate`, `FindNextFileA`).

## Suite results (2026-09-12, under the lock)

* `run_gz_buffer.py` (bottle X3): passed, 735,871 checks, 0 failures,
  `verification/results/bottle-X3/gz-buffer-summary.json`.
* `run_loading_trace.py` (bottle Steam, default): passed; the four new rows
  install as `installed=0` against the stub codec (it exports none of them),
  inventory unchanged at 85 / 123 / 2179 / 2219 checks.
* `check_no_x87.py build/d3d9.dll`: PASS, 144 reachable functions, no
  violation, with `gz_read`/`gz_getc`/`gz_tell`/`gz_seek` as extra roots.
* `python3 -m unittest discover` with `PYTHONPATH=verification/probe`: see the
  session report (`test_gz_buffer.py` re-parses the recorded summary).
* Not verified: the in-game gate path (`X3M_GZ_BUFFER=1` without telemetry)
  needs a user-launched load; the loading-trace fixture's stub codec cannot
  exercise it because it exports no `gzgetc`/`gztell`/`gzclose`.
