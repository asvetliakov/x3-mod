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
| `KERNEL32!FindFirstFileA` | `0x005321c0` | Resource-resolver directory enumeration (one per lookup, uncached; [loading-orchestration.md](loading-orchestration.md) section 2): count, wall time; a no-match result (`ERROR_FILE_NOT_FOUND`/`ERROR_NO_MORE_FILES`) is `ambiguous`, other invalid handles are failures |
| `KERNEL32!FindNextFileA` | `0x005321bc` | Enumeration steps; `ERROR_NO_MORE_FILES` termination is `ambiguous`, not a failure |
| `KERNEL32!FindClose` | `0x005321b8` | Enumeration close count/time |

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

`src/proxy/loading_trace.{h,cpp}` now supplies opt-in main-module IAT diagnostics for 19 main-module boundaries: the three Win32 file APIs, the three directory-enumeration APIs of the resource resolver (`FindFirstFileA`, `FindNextFileA`, `FindClose`; added 2026-09-12, resolved by name like the others, paths never logged, bytes always zero), six D3DX helpers (including CreateMesh and CleanMesh), `SetCursor`, `SetCursorPos`, and `gzopen`/`gzread`/`gzseek`/`inflate`/`xmlReadMemory`. `ShowCursor` is not a static import of this executable; absence of those calls in this trace is not evidence it never runs in other modules. Graphics telemetry separately observes D3D cursor methods.

Initialization occurs outside loader lock. The current production gate uses
bounded readable PE32/i386 named-import parsing; it no longer reads or fingerprints
the EXE, requires a fixed image base/size, or rejects a different whole-file hash.
Ordinal/missing-name imports are not inferred from their IAT targets. Candidate
slots are staged before mutation; atomic pointer comparisons, restored page
protections and owned-only teardown preserve other interceptors. Fixture-only
entry points are absent from the production object.

Callback signatures for Win32/D3DX are compile-time checked against the MinGW SDK. The zlib/libxml signatures are corroborated by local SDK declarations and target assembly: `gzread`/`gzseek` use three 32-bit arguments and caller cleanup; `inflate` uses two; `xmlReadMemory` uses five. `gzseek` is the legacy 32-bit signed-long API, not `gzseek64`. The production code treats codec/XML objects as opaque pointers and never reads `z_stream`, resource contents, paths or XML source text. Inflate records time/status only; its byte count is unavailable. `gzread` reports returned output bytes, not compressed disk bytes.

Counters are fixed atomic aggregates, exchanged by periodic reports with no steady-state per-call logging or explicit event/payload allocations. First mesh observation additionally verifies/pins the native DLL and installs bounded shared-vtable timing; setup logging and its file read are measured in the create/clean wrapper tail. MinGW uses emulated thread-local storage, which may initialize on a thread's first callback; incoming LastError is saved before that access. The original call's LastError and results are restored after accounting. No mutex is held around the original API. Asynchronous ReadFile pending results have a separate bucket and are not counted as failures. SetFilePointer's ambiguous sentinel and inflate's nonfatal `Z_BUF_ERROR` use `ambiguous`; neither is silently treated as definitive failed work.

The event schema is `loading_trace coverage_begin=... frequency=... hooks=... module=main`, `loading_hook name=... installed=...`, and periodic `loading_metric op=... qpc=... count=... failures=... pending=... ambiguous=... bytes=... inclusive_ticks=... exclusive_ticks=... max_ticks=... wrapper_tail_ticks=... total_us=... exclusive_us=... max_us=... wrapper_tail_us=...`. Per-field exchanges are not a transactional snapshot: concurrent updates can straddle adjacent reports, so use complete-run totals and do not infer an impossible operation from one sparse delta. All nonzero fields are retained when deciding to emit a sample.

Loading spans subtract nested **loading** spans only. D3DX effect time still includes nested graphics creation/proxy shader inspection. The graphics and loading tables are not additive. `wrapper_tail_ticks` measures only a known tail of the callback accounting; it is a lower bound on wrapper overhead, not a complete subtraction of instrumentation. API intervals also include small entry/clock/error-preservation costs. These explicit limitations avoid implying perfectly unperturbed timing.

### Synthetic verification

The ABI fixture (`verification/probe/run_loading_trace.py`, 85 checks since the directory-enumeration hooks) compares each forwarded result, find data and LastError with the raw import, including the `ERROR_NO_MORE_FILES` termination, the `ERROR_FILE_NOT_FOUND` no-match case and `ERROR_PATH_NOT_FOUND`/invalid-handle failures, and checks the count/failure/ambiguous counters of all three.

`verification/probe/build_loading_trace.sh` builds a standalone executable and original stub D3DX/zlib/XML DLLs in its private build directory. Do not copy these stub DLLs into X3. The expanded ABI fixture completed **85 checks with zero failures** (75 before the directory-enumeration hooks) under CrossOver Preview without launching the game. It verifies disabled mode, malformed/escaping PE rejection, named-only import matching, all forwarded D3DX/codec/XML argument values, success/failure LastError, exact returned pointers/integers, output buffers, async pending reads, counters, IAT page protections, snapshot reset, and preservation of another interceptor installed before teardown.

A same-process 20,000-call fake-inflate loop reports direct and instrumented stub costs separately in the raw fixture output. This is an illustrative callback-cost check with a trivial backend, not a game benchmark or a forecast of loading impact. Recorded evidence is `verification/results/loading-trace-fixture.txt`; the fresh-build source/executable/native-DLL fingerprints and native mesh results are in `verification/results/loading-trace-mesh-summary.json`. Production compilation completed with `-Wall -Wextra` and no warnings; symbol inspection confirms the fixture-only entry points are absent.

```sh
sh verification/probe/build_loading_trace.sh
'/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine' \
  --bottle Steam --no-update --dll 'd3dx9_37,zlib1,libxml2=n,b' \
  --workdir "$PWD/verification/probe/build/loading_trace" \
  "$PWD/verification/probe/build/loading_trace/loading_trace_fixture.exe"
```

Startup/menu/new-game attribution still requires the requested single user-coordinated game session. There has been no archive or engine optimization, and none of the synthetic outcomes establishes faster game loading.


## Verified native mesh preparation timing

The consolidated diagnostics now time the observed CreateMesh → GenerateAdjacency → CleanMesh → OptimizeInplace path, including ConvertPointRepsToAdjacency fallback. They do not change geometry, epsilon, adjacency, options, optimization order or caching. The completed user-run observation had no mesh counters; these additions must not be retroactively used to assign its 89-second presentation gap to meshes.

Two new exact main-IAT spans forward `D3DXCreateMesh` and `D3DXCleanMesh` with all original pointers, optional outputs, flags, HRESULT and incoming/outgoing LastError preserved. SDK declarations verify their six-argument stdcall ABI at compile time. Successful returned meshes trigger bounded setup; failures and null outputs are left untouched.

Three further aggregate operation names are `ID3DXMesh::GenerateAdjacency`, `ID3DXMesh::ConvertPointRepsToAdjacency` and `ID3DXMesh::OptimizeInplace`. The implementation changes only native shared vtable slots 22, 20 and 27 respectively. It does not replace an object pointer/vptr, clone an assumed private table extent, intercept Release, or retain any mesh/device reference. Native QueryInterface, aliases, AddRef/Release and zero-reference destruction remain the original implementation. This deliberately avoids per-object lifetime registries and their stale-address/destructor risks.

Shared slots have **broader coverage than main-module imports**: every object using a successfully observed native table is timed, including objects created earlier and calls originating inside other modules. Direct nonvirtual internal helper calls remain outside these spans. Create/Clean inclusive time can contain nested method time; the existing loading-exclusive counter subtracts nested loading spans only. Graphics timings and loading timings remain nonadditive. Geometry byte counts are unavailable (`bytes=0`); no vertices, indices, adjacency arrays or remap payloads are inspected or logged.

### Compatibility, bounds and lifetime

Method hooks use public COM slots and process-local lifetime qualification, not
D3DX file digests or fixed code offsets. The returned mesh must have readable
storage and a module-backed readable shared table; the table and callable original
methods' actual owning modules are pinned. Their module placement need not equal
the factory's. Heap/JIT tables with unproven saved-chain lifetime retain only IAT
coverage. No engine instructions or function prologues are patched. Public buffer
qualification and unchanged observed metadata-method pointers separately govern
optional adjacency caching; see [the cache adapter](../verification/mesh-cache-hook.md).

At most eight table records are retained, each with three exact original pointers and distinct template trampolines. Originals are immutable. A foreign interceptor that later clones a vptr or chains to a saved trampoline therefore reaches the correct original without consulting the object's current vptr or retaining the object. The registry lock is held only for setup/teardown; native mesh work runs outside it. Steady-state method callbacks contain timing/accounting only. Bounded setup events explicitly identify `scope=shared_public_com_vtables`, module lifetime pinning, table index and owned slot count.

Slot writes use atomic ownership comparisons and restore page protections. Partial installation rolls back owned slots. A fixed 64-entry page-protection recovery table retains the true original page protection if restoration and rollback restoration both fail. A later slot on that same page cannot treat temporary PAGE_READWRITE as its original baseline. Quiescent shutdown retries pending protection recovery, preserves foreign-owned slots, and reports active while any owned import/method hook or unresolved protection remains. Original records and the pinned module remain available after teardown for foreign chains. The tracing DLL itself must remain loaded while any retained trampoline may still be called.

Initialization has one installation generation per process. After teardown it refuses reinstallation rather than rebinding an original pointer to a foreign hook that already chains to this tracer. Repeated shutdown is permitted to retry incomplete recovery. Teardown is explicitly quiescent and must never run from DllMain or while callbacks are executing.

### Native and synthetic verification

`python3 verification/probe/run_loading_trace.py` freshly builds both suites, records the loaded DLL identity without an allowlist before copying it into an ignored standalone test directory, and checks all consumed source/binary hashes before and after execution. Only CrossOver Preview's Steam bottle is used. There is no game launch, installation or bottle-settings mutation.

The ABI suite passed **75 checks**, including all CreateMesh/CleanMesh arguments, null outputs, success/failure HRESULTs, incoming/outgoing LastError and counters. Its stub outputs remain outside valid mesh-object/table lifetime qualification.
It also exercises malformed PE headers, escaping import directories, missing named
thunks, ordinal/unknown imports, a foreign IAT interceptor chaining to the saved
tracer, shutdown, refused reinitialization and continued safe dispatch. There are
no setup fingerprint reads. The independent real-mesh suite checks public COM
slot instrumentation, partial patch rollback and preserved native outputs.

The real native suite passed **123 checks** on an original tetrahedral mesh. It compares generated/point-representative/cleaned/optimized adjacency, vertex/index/attribute payloads, face/vertex remaps, all recorded HRESULTs and LastError exactly with an uninstrumented baseline. CleanMesh returns the same input with an additional reference for this case; that native alias behavior is preserved and each owned reference is released once. QueryInterface identity, native refcounts, final mesh Release and final device Release are checked. A preexisting mesh demonstrates shared-table scope.

The native suite also injects failure before a second slot install, failure of both page-protection restore and rollback restore, recovery by the next patch on the same page, persistent shutdown restoration failure followed by a successful retry, and another interceptor owning a slot at teardown. The foreign chain remains callable after shutdown. During the measured synthetic sequence, the five categories observed 1 create, 1 clean, 1 point-representative conversion, 2 adjacency calls and 2 OptimizeInplace calls; one intentionally invalid OptimizeInplace request failed as in the baseline. These are fixture counts, not game-load costs.

Production compilation passes `-Wall -Wextra -Werror`, and the production object
contains no fixture-only fault entry points. Independent review accepted public
interface qualification, saved-original/protection recovery, and the fresh 75/123
suite artifacts. Current loading implementation SHA-256 is
`3d63865fbf60b61eb2f458353191c0936aa87aeb43c086e83e8fa4504faa3ce1`;
exact source/native/executable/report provenance is retained in the summary JSON.
The separately requested [adjacency cache](../verification/mesh-cache-hook.md)
now has public-interface tests. No game loading speedup is established, and
Windows runtime validation remains outstanding.

## Sampling profiler

The counters above cannot see the ~70 % of loading time that crosses no hooked
boundary. `X3M_PROFILE=1` (`tools/manage.py launch --profile`) runs an
in-process sampling profiler in the proxy: one sampler thread suspends each
application thread every 2 ms (`X3M_PROFILE_INTERVAL_US`), records the exact
leaf RVA, the first main-executable frame from an EBP walk plus a bounded
return-address scan, and the caller of that frame, and writes delta reports
(`profile_report`, `profile_thread`, `profile_leaf`, `profile_frame`,
`profile_pair`) into the session log on the same QPC clock as `loading_metric`.
`tools/analysis/summarize_profile.py` windows those reports by seconds after
proxy initialization (the gap bounds from `analyze_iteration08_loading.py`) and
`tools/analysis/X3ProfileSymbols.java` names the RVAs with Ghidra. Design,
safety rules, limits (Wine/Rosetta context accuracy, frame-pointer-omitting
functions, scan false positives, cost) and the synthetic Wine verification are
in [docs/verification/sampling-profiler.md](../verification/sampling-profiler.md).
No game session has been profiled yet; the next user-run loading session with
`--telemetry --profile --mesh-cache` provides the first attribution.

## Exact-equality adjacency switch

The 69 s of `GenerateAdjacency` attributed above is D3DX's epsilon point
welding on positions that are quantized 61 times coarser than the epsilon.
`X3M_MESH_ADJACENCY=native|verify|fast` (`tools/manage.py launch
--mesh-adjacency {native,verify,fast}`, requires `--telemetry`) puts an
exact-equality computation (`src/proxy/mesh_adjacency_fast.cpp`) behind the same
vtable hook: `verify` runs D3DX, recomputes and logs any difference; `fast`
answers from the computation and falls through to D3DX on any qualification
failure. Algorithm, D3DX-equivalence argument, tie-breaking evidence against the
real `d3dx9_37.dll` and fixture timings are in
[docs/verification/mesh-adjacency-fast.md](../verification/mesh-adjacency-fast.md).
The next user run should use `verify` first (`verify_mismatched=0` in the last
`mesh_adjacency_metric` line), then `fast`.

## Savegame gz read-ahead buffer

The 13.9 M three-byte `gzread` calls of the savegame decode come from the
read dispatcher `0x004e9210` issuing one `gzread` per field
([savegame-gz-stream.md](savegame-gz-stream.md): mode strings, seek/tell
usage, no handle both written and read). `X3M_GZ_BUFFER=1`
(`tools/manage.py launch --gz-buffer`, optional `--gz-buffer-kb`, no
`--telemetry` needed) serves them from 256 KB chunks in `src/proxy/gz_buffer.cpp`
behind the same import hooks, keeping zlib 1.2.3 semantics. The fixture against
the real `zlib1.dll`, the semantics table, the timing (which shows that the
profile's 0.656 µs per hooked call is mostly the hook envelope under FEX, not
zlib) and the expected in-game bound are in
[docs/verification/gz-buffer.md](../verification/gz-buffer.md).

## Light hooks, probe batch 2, fast resource reader

The counting/timing import rows now run without `CpuCallBoundary` from a
no-SSE unit (355 ns envelope instead of 1,215 ns under FEX); `X3M_LOADING_PROBES=1`
adds the CryptoAPI/per-open import rows and twelve byte-verified
entry-counting trampolines ([loading-probes.md](loading-probes.md),
[docs/verification/loading-probes.md](../verification/loading-probes.md));
`X3M_RESOURCE_READ=verify|fast` replaces the archive reader's per-kilobyte
decode of `0x004e8880` and `X3M_DAT_HANDLES=1` keeps the catalogue `.dat`
handles ([resource-reader.md](resource-reader.md),
[docs/verification/resource-reader.md](../verification/resource-reader.md)).
`frame_end` lines carry `elapsed_ms`/`dt_ms`/`qpc` in every mode, so a plain
run's load times are readable from the log.

`tools/analysis/analyze_loading_profile.py <session.log> --output <dir> --ghidra`
runs that chain in one command: every presentation gap over 2 s and every
report stall inside it gets the hooked operation table next to the sampled
per-thread module split, the leaf samples aggregated by containing function
(symbolized headless with Ghidra, labelled from
`tools/analysis/x3ap_function_labels.json`), the caller pairs and a mechanical
candidates paragraph. On the iteration-08 run-A log (no `profile_*` lines) it
renders the three gaps and their five stalls with a "No profile data" note in
1.6 s, so the first profiled session needs no further tooling to be read.
