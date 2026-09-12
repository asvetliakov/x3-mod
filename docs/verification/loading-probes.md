# Light loading hooks, probe batch 2 and `frame_end` load times

Three loading-time diagnostics changes of 2026-09-12 (night), verified here:

* **A. Light hooks.** The loading-trace rows whose wrappers only count, time and
  forward (`CreateFileA`, `ReadFile`, `SetFilePointer`, `SetCursor`,
  `SetCursorPos`, `FindFirstFileA`/`FindNextFileA`/`FindClose`, `gzopen`,
  `gzread`, `gzseek`, `gzgetc`, `gztell`, `gzclose`, `gzwrite`, `inflate`,
  `xmlReadMemory`) moved into `src/proxy/loading_trace_light.cpp`, compiled with
  `-mno-sse -mno-mmx -mfpmath=387`, and run without `CpuCallBoundary` and without
  the admission scope. The D3DX rows (`D3DXCreateEffect`, the three texture
  helpers, `D3DXCreateMesh`, `D3DXCleanMesh`) and the mesh vtable rows keep the
  full boundary: they observe meshes and log.
* **B. Probe batch 2** (`X3M_TELEMETRY=1 X3M_LOADING_PROBES=1`,
  `tools/manage.py launch --telemetry --loading-probes`): 18 more light import
  rows and 12 entry-counting trampolines on the engine's loading functions,
  documented in
  [docs/reverse-engineering/loading-probes.md](../reverse-engineering/loading-probes.md).
* **`frame_end` load times.** Every mode's `frame_end` line (every 300 frames or
  a capture frame) now ends with `elapsed_ms=<since DllMain> dt_ms=<since the
  previous frame_end line> qpc=<stamp>` — one `QueryPerformanceCounter` per
  logged line, integer arithmetic, the existing fields unchanged. The analysis
  tools derive load gaps from these when the log has no telemetry (below).

## Why the light rows need no CPU-state boundary

The rule is the one `engine_memory.cpp` already follows: a unit compiled without
SSE/MMX that does no floating-point work touches no XMM, MMX or x87 register, so
the only caller state a wrapper can disturb is the thread's last error, which the
span saves and restores exactly as before. The unit therefore avoids the two
things GCC would otherwise emit: 64-bit `std::atomic` loads/stores (x87
`fild`/`fistp` without SSE — the counters use `lock cmpxchg8b` helpers) and any
logging (the printf formatter is x87 code). The nesting pointer (parent span for
the exclusive column) lives in a `TlsAlloc` slot instead of emulated TLS.

**objdump proof** (`i686-w64-mingw32-objdump -d` of the objects in
`build/CMakeFiles/d3d9.dir/src/proxy/`, DLL of this checkpoint):

| Object | Functions | Instructions | `xmm`/`mm` references | x87 mnemonics | `lock cmpxchg8b` |
| --- | ---: | ---: | ---: | ---: | ---: |
| `loading_trace_light.cpp.obj` | 53 | 3,834 | **0** | **0** | 45 |
| `resource_reader_core.cpp.obj` | 12 | 2,077 | **0** | **0** | 56 |

`verification/probe/check_no_x87.py build/d3d9.dll`: **PASS, 51 roots, 189
reachable functions, no violation**. The roots now include the 35 light rows
(`x3m::loading_trace::light::*`), the probe handlers `x3m_probe_enter` /
`x3m_probe_exit`, the reader entry `x3m_resource_read_entry` and the pool
`x3m_pool_fopen` / `x3m_pool_fclose`, walked through every direct call
(indirect calls — the originals, zlib, the game's CRT — stop the walk as before).

## Envelope measurement (gz-buffer fixture timing case, bottle X3, FEX)

`run_gz_buffer.py` times 10 M three-byte `gzread` calls (30 MB) four ways:
raw `zlib1.dll`; raw plus the three `QueryPerformanceCounter` reads a span
takes (`qpc`); the light span (`light`: last-error transport, TLS nesting,
`cmpxchg8b` accounting, the three QPC reads — exactly the production light
wrapper's envelope); and the former `CpuCallBoundary` envelope (`hooked`).

| Mode | ns per call | Envelope over raw |
| --- | ---: | ---: |
| raw `zlib1.dll` | 32.5 | – |
| raw + 3 × QPC (`qpc`) | 243.2 | 210.7 ns (≈ 70 ns per `QueryPerformanceCounter` under FEX) |
| light span (`light`) | 386.8 | **354.3 ns** |
| `CpuCallBoundary` (`hooked`) | 1,237.1 | 1,204.6 ns |

So the light envelope is 3.4× cheaper than the boundary one and 60 % of it is
the three clock reads the row format needs (begin, end, wrapper tail); the
remaining ≈ 144 ns are the two `GetLastError`/`SetLastError` pairs, the TLS
nesting slot (`TlsGetValue` + two `TlsSetValue`) and the `cmpxchg8b`
accounting. Consequence for the profiles: the 13.9 M `gzread` calls of a
savegame load cost ≈ 5 s of instrumentation instead of ≈ 17 s (the
`--gz-buffer` switch removes them entirely), and every tiny-call attribution
(`ReadFile`, `inflate`, `FindNextFileA`, `CloseHandle`) shrinks by the same
ratio.

Recorded run: `verification/results/bottle-X3/gz-buffer-summary.json`
(735,876 checks, 0 failures; `qpc_envelope_ns`, `light_envelope_ns`,
`hooked_envelope_ns` recorded).

## Probe batch 2 fixture

`verification/probe/resource_reader_fixture.cpp` (through
`run_resource_reader.py`, bottle X3) exercises the patch machinery on fixture
functions whose prologues are the real sites' shapes: `push ebp; mov ebp,esp;
and esp,-8` (resource_load), `push ebx; mov ebx,[esp+8]` + `ret 4`
(resource_open), `test byte [esi+4],1; push ebx` + `ret 4` (read_dispatch,
count only) and `sub esp,0x454` (resource_read, the reader's site). Cases:
byte mismatch fails closed for that site alone; nested calls counted with
inclusive time; `ret 4` frames matched; the dispatcher's bytes (`ECX·EAX`) and
branch classification; a `longjmp` from depth 1 to a catcher at depth 3 (two
probed frames never return: their shadow entries are discarded at the catcher's
exit, `desync=2`, the next call is clean); 200 nested calls on each of two
threads (1,600 entries/exits, no overflow, no desync); the probe stub chained
in front of the reader stub on one site; restoration returns the original
bytes. Numbers: see [resource-reader.md](resource-reader.md).

`run_loading_trace.py` (Steam bottle, stub codec/D3DX): with
`X3M_LOADING_PROBES=1` the probe rows the fixture executable imports install
as light rows (`CloseHandle` counted; 86 checks, was 85); the trampolines stay
off because the executable gate answers false in a fixture.

## Byte verification of the twelve sites (review 27, 2026-09-12)

Read from the installed `~/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/X3AP.exe`
(SHA-256 `fdbf3418d8f0a897…`, 2,153,984 bytes, `.text` VA `0x401000` at file
offset `0x400`, read only) through the PE section table, and against the Ghidra
listings (`X3LoadingOrchestration.java decompile … -- listing …`, output under
`/tmp/x3-review27`, untracked):

* All twelve `expected` byte strings equal the executable's bytes at the site
  VAs; the five CRT callees (`_fread 0x00512213`, `_fseek 0x00510343`, `_ftell
  0x005128e6`, `_malloc 0x005112c4`, `_free 0x0050e1b0`) match their first five
  bytes; the two pool call sites are `E8` calls whose displacements resolve to
  `_fopen 0x00512a18` and `_fclose 0x0050f8d4`.
* Every `length` ends on an instruction boundary (`push ebp | mov ebp,esp | and
  esp,-8`; `push ebx | mov ebx,[esp+8]`; `push -1 | push imm32`; `sub esp,imm32`;
  `test byte [esi+4],1 | push ebx`; `push imm8 | push imm32`; `push ebp | mov
  ebp,esp | sub esp,imm8`; `push ×4 | mov ebp,imm32`; `sub esp,imm8 | push ebx |
  mov ebx,[esp+0x34]`; `sub esp,imm8 | push ebx | push ebp`), none of the
  displaced instructions is a relative branch or EIP-relative, and the byte
  after each site is the start of the next instruction seen in the listing
  (`6a ff`, `57`, `64 a1`, `53`, `8b 5c 24 08`, `e8`, `53`, `55`, `55`, `64
  a1`, `64 a1`, `8b 6c 24 1c`).
* Return forms in the listings: `resource_open` `RET 0x4` (×3), `name_resolve`
  `RET 0xc` at `0x004e874a` (the decompile is `__thiscall` with three stack
  arguments), `read_dispatch` `RET 0x4` (×3); every other site's function
  returns with a plain `RET` (`resource_load` ×2, `resource_read` ×4,
  `sopen_helper`, `find_wrapper`, `signature_check`, `texture_body`,
  `texture_loader`, `mesh_body`, `crt_fgetc`) — matching the `ret_pop` column
  the exit handler uses.
* No overlap: the scene hook's call at `0x004721b1` and the object-trace call
  site at `0x004c5228` are `E8` calls in other functions, more than 16 bytes
  from every probe site and from the two pool call sites; the three patch
  owners never touch the same instruction and all install in the same
  `initialize_log` pass before the first `Present` (the install window).
* Atomic write: eleven sites and the `_fclose` call site have their first five
  bytes inside one aligned qword and are written with `lock cmpxchg8b`;
  `crt_fgetc` (`0x0050fff5`) and the `_fopen` call site (`0x004e87ff`) take
  the plain copy under the install window.
* CPU-state contract: the stubs save and restore EFLAGS and all eight GPRs
  (`pushfd/pushad … popad/popfd`); the handlers live in the no-SSE unit
  (`check_no_x87.py`: PASS, 53 roots, 195 reachable functions, 0 violations on
  the review build) and transport only the last error; callee EAX/EDX survive
  the exit stub (`mov [esp+0x24],eax` writes the reserved slot, not the saved
  EAX).

## Log lines and analysis

* `loading_trace … light_rows=1 nesting=<0|1> probes=<0|1>` at install;
  `loading_probes requested=1 installed=<n> sites=12 …` and one
  `loading_probe_site … atomic_write=<0|1>` per site (`status=late_claim` and
  `window_closed_by=` when a claim arrives after the first `Present`);
  `loading_probes shutdown retained_shadow_blocks= retained_bytes=` at restore.
* Per report window: `loading_probe site= va= qpc= calls= exits=
  inclusive_ticks= max_ticks= total_us= max_us= overflow= desync= bytes= x0= x1=
  x2= x3=`, `loading_probe_caller site=find_wrapper caller=0x… calls=`,
  `loading_probe_path op= path=` (first 16 write-side paths).
* `tools/analysis/analyze_loading_profile.py` prints an "Engine probes" table
  per gap and per report stall (extras named from the site header; callers
  listed for `find_wrapper`); `tools/analysis/summarize_profile.py --frame-gaps`
  turns the `frame_end` gaps into profile windows; without any telemetry both
  tools bound the gaps from `frame_end` (`gap_source=frame_end`, seconds since
  DllMain). Tests: `verification/analysis/test_loading_profile.py`,
  `test_profile_summary.py`.

## What the next game run must show

`tools/manage.py launch --direct --telemetry --loading-probes --gz-buffer
--profile` (X3 bottle), one savegame load:

1. `loading_probes … installed=12` and twelve `loading_probe_site … status=active`
   lines (any `bytes_mismatch` means the executable differs from the studied
   one and that site stays untouched).
2. In the script/XML stall: `resource_open` calls ≈ 1,376 with
   `loose ≈ 790 / catalogue ≈ 586`, `name_resolve hit/miss`, `resource_read`
   bytes ≈ 46 MB, `crt_fgetc` ≈ 10–40 per gz file, `signature_check` calls and
   inclusive time (the CryptoAPI rows next to it), `sopen_helper` inclusive time
   next to `CreateFileA`, `texture_body`/`texture_loader`/`mesh_body` inclusive
   time — the decomposition §7 of the stall study asked for, with
   `desync=0 overflow=0` on every site.
3. The `loading_metric` rows of the light imports with `wrapper_tail_us`
   near the fixture's envelope, and the instrumented load time close to the
   stopwatch time (≈ 40 s without telemetry on X3 versus 65 s with the old
   envelope).
4. `frame_end … elapsed_ms= dt_ms=` lines whose `dt_ms` gaps agree with the
   presentation gaps of the telemetry.
