# Loading performance: evidence and one-session measurement plan

This document preserves the pre-0.3 measurement plan. The requested combined run
is complete; see [loading observations](loading-observations.md) for measured
costs, uncovered mesh processing and the next investigation steps.

Reviewed 2026-09-10 against capture 0.2 source, existing capture summaries, local PE metadata and targeted Ghidra analysis. No game was launched for this investigation. No loading-time measurement or improvement is claimed. The next instrumented build should measure startup, the animated menu, and one user-started new game in the same session, while preserving the requested F8 renderer capture.

## What is established

The current trace has no monotonic timestamps or API durations. Forty-seven unique shader hashes in a prior flight session do not reveal total shader creation calls, shader processing time, or the time spent loading a sector. Log/file modification times are not adequate substitutes.

### Proxy overhead already present

| Source path | Current behavior | Consequence to measure |
|---|---|---|
| `capture.cpp:shader_id` | Two `GetFunction` calls, vector allocation, full byte hash on every invocation | Runs for every successfully created VS/PS, even outside detailed capture; repeated shaders still pay this cost |
| `capture.cpp:shader_id` | First hash seen in each process synchronously opens/writes/closes a `.bin` | Dump set is process-local: existing files can be overwritten on the next run; measure bytes/time, do not assume disk cost dominates |
| `capture.cpp:snapshot` | Queries shaders, geometry, targets, textures, states and constants at every captured draw; hashes current shaders again | Detailed F8 frames deliberately perturb execution and must be excluded from loading/steady-frame conclusions |
| `capture.cpp:present` | Calls `fflush` after every Present, even if no new lines were generated | Measure calls, actual flush time and bytes; a clean buffer need not cause OS I/O |
| Draw/Clear/target/Present/shader hooks | Recursive global mutex remains held across backend calls | Potential overhead/serialization; no observed contention measurement exists |
| Draw hooks | Map lookup, draw counter and capture check outside detailed capture | Small work is still work; establish its cost before redesigning ownership |
| `loader.cpp:load_backend` | Log initialization and absolute backend DLL load on first forwarded call | Part of the proxy's startup path; timing starts later than process creation |

The bounded state snapshot is a plausible cause of a visible F8 hitch, but cannot explain an entire load merely because the log is large. The unconditional shader path is a stronger candidate for *startup* diagnostic overhead. Neither is quantified yet.

### Engine resource and effect path

All addresses refer to SHA-256 `fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`, rechecked during this investigation.

- `0x004bae10` builds a shader path, calls resource loader `0x004e8e10`, then calls imported `D3DXCreateEffect` at `0x004bafca`. The selected resource effects are precompiled `.fb` containers. Label this **effect creation**, not proven HLSL source compilation. Backend translation/validation or lazy driver compilation can still occur later.
- `0x004e8e10` invokes open/lookup helper `0x004e8780`, resource reader `0x004e8880`, and cleanup. Its callers include shader loading and three image/texture loading functions (`0x004dc540`, `0x004dd2c0`, `0x004de9c0`). This is a common resource path, not exclusively shaders.
- `0x004e8880` recognizes gzip headers and a transformed header variant, allocates an output buffer, seeks/reads resource data and calls `inflateInit2_`/`inflate`/`inflateEnd`. It also has an uncompressed read branch. Decompilation and assembly at `0x004e8d50`–`0x004e8da4` corroborate a **1,024-byte input chunk** per inflate-loop iteration. That is a user-space read request, not proof of a separate physical disk read per kilobyte; CRT buffering and the OS cache intervene.
- `0x004e9210` dispatches through ordinary file reads, `gzread`, or a bounded archive slice. The archive branch applies bytewise XOR `0x33`. This supports separately measuring compressed read/decompression from effect/texture construction.
- `0x004dc540` calls resource loading, image-info inspection, then 2D/cube texture creation. A D3DX helper can include image decode/conversion, resource allocation and upload; its wall duration is not pure disk time or pure GPU work.
- Imported `xmlReadMemory` has callers in `0x00499480` and `0x004e6b70`. Some engine data parsing can be measured at this API, but script execution, object creation, scene setup and other engine CPU work remain outside the selected imports.

Several internal resource routines use values passed through EAX/ESI and Ghidra does not recover ordinary C prototypes. **Do not detour those function addresses from guessed signatures.** They are research anchors. Parsed import slots provide a substantially safer first timing boundary.

## Validated import boundaries

These are observed IAT virtual addresses, not fixed-address patch instructions. Resolve the main executable's PE import directory by DLL and symbol name, verify PE32 and executable hash, preserve the actual resolved original pointer, and install only an explicitly enabled reversible hook. Retain forwarding signatures and Win32 error state exactly. Do not assume import addresses remain valid in another binary.

| DLL / import | IAT VA | Intended metric |
|---|---|---|
| `d3dx9_37!D3DXCreateEffect` | `0x00532324` | Effect create calls, input bytes, duration, HRESULT; nested VS/PS creation |
| `d3dx9_37!D3DXCreateTextureFromFileInMemoryEx` | `0x00532334` | 2D texture helper time/input size, requested dimensions/format/filter |
| `d3dx9_37!D3DXCreateCubeTextureFromFileInMemoryEx` | `0x00532338` | Cube helper time/input size |
| `d3dx9_37!D3DXLoadSurfaceFromFileInMemory` | `0x00532330` | Surface decode/copy helper time/input size |
| `KERNEL32!ReadFile` | `0x00532158` | Main-module read calls, returned bytes, wall time/errors |
| `KERNEL32!CreateFileA` | `0x005320cc` | Bounded path categories/handle generation and open latency |
| `KERNEL32!SetFilePointer` | `0x005320ac` | Seek count/distance when known |
| `zlib1!gzopen` | `0x005323d8` | Compressed stream open count/time |
| `zlib1!gzread` | `0x005323e4` | Compressed-stream read wall time and output bytes |
| `zlib1!gzseek` | `0x005323fc` | Compressed-stream seeking time; may include work beyond seek |
| `zlib1!inflate` | `0x005323e8` | Inflate-call wall time; input/output deltas from the compatible z_stream layout |
| `libxml2!xmlReadMemory` | `0x005323ac` | XML parse calls, bytes and wall time |

If tracking handles, additionally parse and forward `CloseHandle` (`0x005320c0`) and `gzclose` (`0x005323f0`), retaining generation IDs so recycled handles cannot inherit another file's category. Timing can remain per API if this metadata layer is omitted. Do not read arbitrary caller buffers for hashing or filenames in hot callbacks.

**Coverage limits:** main-executable IAT hooks miss DLL-internal calls through their own imports. `gzread` can include internal I/O and decompression invisible to the main executable's `ReadFile` hook. D3DX-internal work is only included in its outer span. Install time at first D3D initialization can miss earlier archive/configuration loads. Report `coverage_begin` and missing imports, rather than presenting partial counts as whole-process totals. Do not patch every loaded module in this first pass.

`tools/analysis/X3LoadingXrefs.java` reproduces the import-to-thunk/caller references without modifying code. Targeted decompilation remains in `/tmp/x3-loading-functions.c` and is not committed.

## One next capture build

Use an opt-in `X3M_TELEMETRY=1` mode with bounded in-memory aggregates. Retain F8 detailed captures, but label their intervals. This is a single diagnostic build/session, not a sequence of launches to discover that each needs another counter.

1. **Startup anchors:** process creation time when available; telemetry initialization QPC/frequency plus wall-clock correlation; backend DLL load begin/end; Direct3DCreate9 begin/end; CreateDevice/Reset begin/end; first successful Present; API-hook coverage begin. The gap before telemetry initialization stays explicit.
2. **Phase marks:** identify main-menu ready, immediately before requesting New Game, and first controllable gameplay. Use one documented marker mechanism independent of Present (a small telemetry worker polling a dedicated key only while the game is foreground, or a host-to-process marker command). A key polled only from Present cannot timestamp a long no-Present loading interval. Preserve F8 for capture. User supplies the launch/gameplay interaction once, per AGENTS.md.
3. **Always-on aggregates:** count, sum, maximum, fixed histogram, failures and byte counts for effect/texture helpers, shader/resource creation, reads/seeks/decompression/XML, backend Present and draw calls. Record per-second deltas in a bounded buffer; emit only bounded slow-call events. Resource creation may include CreateTexture/Cube/Volume, VB/IB, RT and DS dimensions/formats/usage and backend CPU duration. No texture/buffer payload capture is needed.
4. **Per-frame timing:** interval between Present entries, time inside backend Present, CPU time in the existing capture snapshot, and `capture_active`. Keep loading gaps and captured frames distinguishable from ordinary rendering. Present blocking includes runtime/compositor/frame pacing; call duration is **not GPU execution time**.
5. **Own cost:** measure shader GetFunction/copy, hash, synchronous dump/file close, snapshot, formatting/log writes, flushing and sampled lock wait independently. Count shader creation calls separately from distinct dumped hashes. Measure counters/serialization overhead in a synthetic fixture with telemetry disabled/enabled before shipping the build.
6. **Non-rendering progress:** optionally sample process user/kernel CPU totals and memory once per second, with API success/failure flags. A long loading gap with low traced API time and high process CPU points toward uninstrumented CPU work; low CPU may suggest waiting, but neither identifies the responsible engine subsystem by itself. Do not infer disk-bound behavior from a loading screen alone.
7. **Bounded reporting:** aggregate in fixed arrays/counters; avoid per-Draw text, allocations, resource getters or global trace locks. Flush at most about once a second/phase/end and measure that work separately. Include buffer limits, dropped-event count and telemetry interval in the final report. Do not wait for shutdown to preserve the first useful report.

### Accounting rules

Time backend calls around the original function only; keep proxy work before/after in separate spans. Timestamp lock acquisition separately from backend call duration. Never include the existing shader dump and call the result shader compilation. Instrumented API calls may nest (D3DX effect creation → backend shader creation → proxy metadata), so aggregate both inclusive and exclusive durations with a per-thread nesting stack, or explicitly keep them non-additive. Report process wall time independently. Summing inclusive times or concurrent threads does not produce a valid fraction of loading duration.

For read hooks, preserve return values and `GetLastError`; copy those values before timing/logging helpers can alter them. Do not force synchronous behavior on overlapped I/O: pending reads need an explicit `pending` bucket and returned-byte availability flag. Record process-local profiling writes separately so the profiler's own output does not become “game archive I/O.”

For the own-overhead number, report measured sections and unmeasured remainder. A bracket around `snapshot()` includes its nested hashing/logging: present its total and sub-breakdown, **not their sum**. Likewise `proxy_total` must exclude measured backend-call spans and cannot silently include mutex-blocked time from another thread as CPU execution. CPU time and elapsed waiting time are distinct metrics.

## Safe optimization candidates after evidence

| Candidate | Why it is worth checking | Gate / limit |
|---|---|---|
| Gate raw shader dumping independently from timing and renderer state capture | Startup currently writes each first-seen shader again per process | Preserve requested renderer evidence; record mode and changed behavior. Existing 47 shader matches do not guarantee new scenes need no new shaders |
| Hash each shader object once using a lifetime-safe registry | Repeated `GetFunction`/hashing occurs on captured draws and repeated creation calls | Hook/track destruction or another validated identity scheme; never cache solely by a pointer that may be recycled |
| Defer unique shader dump writes to a bounded queue or batch | Removes synchronous file operations from CreateShader hot paths | Queue memory/overflow policy, shutdown handling, timing attribution; do not retain unbounded bytes in an x86 process |
| Flush on dirty/interval/phase boundaries | Current Present always calls `fflush` | Confirm latency and crash-diagnostic tradeoff; no assumed speedup if clean flushes are already cheap |
| Reduce lock scope and redundant map lookups | Current backend calls run under the proxy's global recursive mutex | Preserve lifetime/reset/reentrancy/thread safety and COM identity; measure contention first |
| Improve compressed-resource chunking/buffering | Verified resource loop requests only 1 KiB per inflate iteration | Only pursue if read/decompression dominates. Increasing the immediate alone would overflow the existing stack buffer; this needs a correctly rebuilt loader path, not a one-byte patch |
| Cache decoded resources/effects | Could help if repeated identical assets dominate | Requires resource keys, invalidation, ownership, thread/lifetime behavior and address-space budget; no implementation justified yet |

Do not change archive order, pre-extract the whole game, change shader quality/backend, disable virus protection, or add aggressive disk prefetch based on this static inspection. One consolidated measured session should first establish whether startup/new-game time is dominated by proxy diagnostics, image/effect work, archive processing, or the uninstrumented engine remainder. Later controlled before/after runs are still needed to claim a speed improvement.


## Implemented next-build loading telemetry

`src/proxy/loading_trace.{h,cpp}` now supplies opt-in main-module IAT diagnostics for all 14 relevant boundaries: the three Win32 file APIs, four D3DX helpers, `SetCursor`, `SetCursorPos`, and `gzopen`/`gzread`/`gzseek`/`inflate`/`xmlReadMemory`. `ShowCursor` is not a static import of this executable; absence of those calls in this trace is not evidence it never runs in other modules. Graphics telemetry separately observes D3D cursor methods.

Initialization occurs outside loader lock and before import patching reads the executable file using unmodified file APIs. The production compatibility gate requires FNV-1a 64 `96f0b2777c624f6d`, file size 2,153,984, PE32/i386, image base `0x00400000` and mapped image size `0x002f5000`. The fixture override only exists under `X3M_LOADING_TRACE_FIXTURE` and is absent from the production object. Import names/DLLs are parsed; none of the address-table numbers above is used for writes. Atomic pointer comparisons, restored page protections and owned-only teardown preserve other interceptors.

Callback signatures for Win32/D3DX are compile-time checked against the MinGW SDK. The zlib/libxml signatures are corroborated by local SDK declarations and target assembly: `gzread`/`gzseek` use three 32-bit arguments and caller cleanup; `inflate` uses two; `xmlReadMemory` uses five. `gzseek` is the legacy 32-bit signed-long API, not `gzseek64`. The production code treats codec/XML objects as opaque pointers and never reads `z_stream`, resource contents, paths or XML source text. Inflate records time/status only; its byte count is unavailable. `gzread` reports returned output bytes, not compressed disk bytes.

Counters are fixed atomic aggregates, exchanged by periodic reports with no per-call logging or explicit event/payload allocations. MinGW uses emulated thread-local storage, which may initialize on a thread's first callback; incoming LastError is saved before that access. The original call's LastError and results are restored after accounting. No mutex is held around the original API. Asynchronous ReadFile pending results have a separate bucket and are not counted as failures. SetFilePointer's ambiguous sentinel and inflate's nonfatal `Z_BUF_ERROR` use `ambiguous`; neither is silently treated as definitive failed work.

The event schema is `loading_trace coverage_begin=... frequency=... hooks=... module=main`, `loading_hook name=... installed=...`, and periodic `loading_metric op=... qpc=... count=... failures=... pending=... ambiguous=... bytes=... inclusive_ticks=... exclusive_ticks=... max_ticks=... wrapper_tail_ticks=... total_us=... exclusive_us=... max_us=... wrapper_tail_us=...`. Per-field exchanges are not a transactional snapshot: concurrent updates can straddle adjacent reports, so use complete-run totals and do not infer an impossible operation from one sparse delta. All nonzero fields are retained when deciding to emit a sample.

Loading spans subtract nested **loading** spans only. D3DX effect time still includes nested graphics creation/proxy shader inspection. The graphics and loading tables are not additive. `wrapper_tail_ticks` measures only a known tail of the callback accounting; it is a lower bound on wrapper overhead, not a complete subtraction of instrumentation. API intervals also include small entry/clock/error-preservation costs. These explicit limitations avoid implying perfectly unperturbed timing.

### Synthetic verification

`verification/probe/build_loading_trace.sh` builds a standalone executable and original stub D3DX/zlib/XML DLLs in its private build directory. Do not copy these stub DLLs into X3. The fixture completed **57 checks with zero failures** under CrossOver Preview without launching the game. It verifies disabled/wrong/production fingerprint rejection, named imports, all forwarded D3DX/codec/XML argument values, success/failure LastError, exact returned pointers/integers, output buffers, async pending reads, counters, IAT page protections, snapshot reset, and preservation of another interceptor installed before teardown.

One same-process 20,000-call fake-inflate loop measured approximately 0.003 microseconds per direct stub call and 0.307 microseconds per instrumented stub call on this run. This is an illustrative callback-cost check with a trivial backend, not a game benchmark or a forecast of loading impact. Recorded evidence is `verification/results/loading-trace-fixture.txt`. Production compilation completed with `-Wall -Wextra` and no warnings; symbol inspection confirms the fixture-only entry points are absent.

```sh
sh verification/probe/build_loading_trace.sh
'/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine' \
  --bottle Steam --no-update --dll 'd3dx9_37,zlib1,libxml2=n,b' \
  --workdir "$PWD/verification/probe/build/loading_trace" \
  "$PWD/verification/probe/build/loading_trace/loading_trace_fixture.exe"
```

Startup/menu/new-game attribution still requires the requested single user-coordinated game session. There has been no archive or engine optimization, and none of the synthetic outcomes establishes faster game loading.
