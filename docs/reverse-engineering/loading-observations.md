# Diagnostics 0.3: completed loading observations

The 2026-09-10 user-run session establishes substantial texture-helper work and an active mesh-preparation path. It does **not** identify a single cause for the entire long loading stall, and no loading optimization or speedup has been implemented. Effect creation and shader dumping were small measured costs in this session.

## Evidence and accounting

The completed process was native PID 26319 / Windows PID 212. Source `session-20260910-214701-212.log` has 42,007,138 bytes, SHA-256 `81cd598428312b709889b0fb892c6243a20818f20d0a3bf400eacdc33af017f0`. The user finished the test and the process was confirmed absent. Last telemetry report is 357.045 s after proxy initialization. There is no terminal `device_destroy` summary: totals below are **observed flushed totals**, with possible missing tail counters.

All 14 intended main-module imports were installed. Coverage began 4.554 ms after telemetry initialization, which itself occurs after process launch. QPC frequency was 10 MHz. No phase markers were recorded; first Present is not a menu-ready timestamp. Reports are emitted on observed graphics activity, so long report gaps do not mean that every API was idle. Report windows contain call-completion deltas, not exact call intervals. Per-field atomic exchanges may split one update across adjacent reports.

API spans are inclusive wall times and can overlap other graphics/proxy measurements. Loading-exclusive spans subtract nested loading hooks only. Do not sum the tables or subtract them from process wall time to manufacture an attribution percentage. The main EXE's file hooks miss DLL-internal I/O and earlier process startup.

Derived machine-readable evidence is [game-loading-observations.json](../../verification/results/game-loading-observations.json). `tools/analysis/analyze_loading_phases.py` reproduces totals and unchanged-frame runs from a newline-complete snapshot; six synthetic parser tests pass. Raw logs, native samples and decompiler output remain local and untracked.

## Measured costs

| Observed API | Calls | Inclusive time | Longest call | Byte meaning / observed bytes |
| --- | ---: | ---: | ---: | --- |
| D3DXCreateTextureFromFileInMemoryEx | 1,452 | 16.382 s | 3.114 s | Input payload: 1,128,172,189 |
| inflate | 607,534 | 7.109 s | 349.9 ms | Bytes not measured |
| CreateFileA | 3,393 | 4.019 s | 54.2 ms | No path/payload capture |
| ReadFile | 163,626 | 1.199 s | 4.75 ms | Returned bytes: 678,026,693 |
| xmlReadMemory | 810 | 406.2 ms | 25.9 ms | Input bytes: 43,092,946 |
| D3DXLoadSurfaceFromFileInMemory | 12 | 196.4 ms | 54.4 ms | Input bytes: 8,672,232 |
| D3DXCreateEffect | 18 | 67.6 ms | 13.7 ms | Input bytes: 598,648 |
| D3DXCreateCubeTextureFromFileInMemoryEx | 33 | 36.7 ms | 1.44 ms | Input bytes: 6,297,528 |
| SetFilePointer | 9,362 | 17.1 ms | 24.2 µs | Distance not measured |
| gzopen | 14 | 11.3 ms | 9.49 ms | Paths not measured |
| gzread | 9 | 0.111 ms | 0.106 ms | Returned output bytes: 92 |

There were two failed CreateFileA calls and twelve failed gzopen calls. Paths were deliberately not logged; optional-file probes and errors cannot be distinguished from these counters alone. No other reported loading failures, pending or ambiguous results occurred. No gzseek or main-IAT cursor metrics were emitted; this is not proof of absence in other modules.

The first successful Present was 3.572 s after initialization. Backend DLL loading took 1.741 ms; three Direct3DCreate9 calls took 1.134 s, 120.2 ms and 104.2 ms. CreateDevice took 15.4 ms. Uninstrumented launch time precedes these anchors.

The 1,483 D3D CreateTexture calls took only 38.7 ms in aggregate, whereas the outer 2D image helpers took 16.382 s. This establishes substantial work beyond initial texture allocation; the outer span can include decoding, filtering, format conversion, population/upload and synchronization. The trace does not isolate their shares. Likewise effect creation is not synonymous with source shader compilation; existing resource research identified precompiled effects.

## Long presentation gap and remaining coverage

Device 1 repeatedly reported frame 1578 from 55.312 s through 143.920 s: 21 observations spanning **88.607 s** without advancing that reported frame. The longest measured ordinary Present-to-Present interval was **89.092 s**. This is a presentation gap, not a precise user input-to-ready measurement.

Loading report intervals spanning 54.024 s through 143.920 s include 522 2D texture calls (11.664 s), 270,794 inflate calls (3.681 s), 1,531 file opens (3.667 s), 73,405 reads (0.521 s / 293,078,971 returned bytes), and 798 XML parses (0.293 s). These report groups overlap the gap but do not provide exact phase boundaries.

Two particularly large intervals remain poorly covered:

- Reports at 58.995–89.562 s are 30.567 s apart; their ending deltas include 3.535 s of opens, 0.660 s of texture helpers and 0.196 s of XML work.
- Reports at 89.562–123.620 s are 34.058 s apart; ending deltas include 1.149 s of inflate, 0.214 s of reads and 0.075 s of XML work.

An API may remain in flight across those reports, and uncovered engine/DLL work or waits may occur. There is no defensible complete split of those intervals yet. Native CPU observations below establish activity during part of this region, without making every untraced second CPU-bound.

## Native sample and targeted disassembly

Native `ps` reported 99.7% CPU at elapsed 2:08 with 1:58.04 accumulated CPU, and later 138.2% at elapsed 2:46 with 2:37.11 accumulated CPU. These are process-wide observations and may include multiple threads. A bounded macOS sample at 21:49:14.223 +0400 requested 3 s at 10 ms intervals and recorded 241 root-stack occurrences on native thread 18103496. There is no precise native-wall-clock/QPC correlation.

A separate read-only Toolhelp/VirtualQueryEx helper mapped the live Windows modules and memory protection. Wine GetProcessTimes returned only 0.02 s kernel / 0.03 s user, inconsistent with native process observations, so those Windows CPU totals were discarded. No process writes, injection, suspension or game input were used.

| Sample address | Runtime module / RVA | Validated finding |
| --- | --- | --- |
| 0x00396582 | zlib1.dll at 0x00390000 / 0x6582 | Inside exported inflate; 34 root occurrences |
| 0x7a01f063 | d3dx9_37.dll at 0x79e80000 / 0x19f063 | Internal mesh adjacency routine; 14 root occurrences |
| 0x7a095810 | same / 0x215810 | Surface/volume conversion loop; 7 root occurrences |
| 0x7a00caf6 | same / 0x18caf6 | Vertex-declaration conversion used by mesh routines; 6 root occurrences |

The selected roots map to zlib (34), D3DX (34), X3AP (8), proxy (1), and ntdll (1); **163 of 241 remain native, unmapped or unwind artifacts**. These are bounded sample occurrences, not percentages of full-run CPU. In particular, apparent low addresses 0x37060020 and 0x377d0020 are PAGE_READWRITE private memory, not executable engine routines. Rosetta/unwind artifacts prevent treating the native stack listing as a complete Windows profile.

Ghidra's D3DX preferred image base is 0x00400000, different from its observed runtime base. At preferred VA 0x0059f063 the sampled routine is inside `FUN_0059edc6`, called by `FUN_005a27e4`. The latter occupies vtable slot 22 (+0x58), corroborated against the local `ID3DXBaseMesh` header as **GenerateAdjacency**. Another root at RVA 0x19f189 lands in the same internal routine. This identifies active mesh adjacency work, rather than inferring shader compilation from the DLL name.

The X3 mesh path is also validated: `FUN_004bb470` calls D3DXCreateMesh, then `FUN_004bc680`. The latter invokes mesh GenerateAdjacency at 0x004bc76a with epsilon approximately **1e-6** (float at 0x00565600), retries/falls back to ConvertPointRepsToAdjacency on failure, calls D3DXCleanMesh with flags 3, and calls OptimizeInplace with **D3DXMESHOPT_VERTEXCACHE (0x04000000)**. Imported creation/cleaning and these mesh methods are outside the current loading counters. This is an actionable coverage gap and optimization candidate, not a measured full-gap cost.

At preferred VA 0x00615810, `FUN_0061578a` iterates conversion callbacks over rows/slices; its parent is directly called by exported D3DXLoadSurfaceFromMemory and D3DXLoadVolumeFromMemory. At 0x0058caf6, `FUN_0058caa2` converts vertex-declaration elements. These findings support CPU image/mesh processing during the short sample, without attributing all helper time to those loops.

Binary identities: X3AP SHA-256 `fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`; D3DX `c2ccb84c672a9d8966e82a28005a4269886ee304972ac3590c0b8a9c1622a3d8`; zlib `27b95a87be89090df67f5f1e7fc88437da400b7c3b6728418d97eca71882cad5`. `X3SampleFunctions.java` reproduces targeted local decompilation; it must not export copyrighted code into the repository.

## Diagnostic overhead and next work

Observed capture CPU was 2.111 s, including 2.085 s of nested snapshots; do not add them. Captured frame intervals totaled 2.473 s and deliberately include backend/game time. Shader inspection took 112.9 ms, nested hashing 45.3 ms and GetFunction 3.14 ms; separate raw shader dumping took 53.4 ms. Measured log flushing totaled about 7.23 ms. Sampled lock acquisition spans totaled 410.0 ms, with a 166 µs maximum; this does not include every cost of holding the lock. Loading wrapper-tail accounting totaled 134.5 ms, a lower bound that omits some entry, clock, TLS and counter costs. These measurements do not establish zero instrumentation overhead, but they do not support shader dumps or flushes as the primary observed loading expense.

The evidence supports these priorities before another requested gameplay test:

1. **Texture processing:** investigate original-image offline fixtures and exact format/filter behavior, then consider a bounded cache of validated decoded/converted results. The measured 16.382 s makes this the largest instrumented candidate. Required gates include output parity, cache invalidation, resource lifetime and x86 address-space budget. No global filter/format shortcut is justified.
2. **Mesh preparation:** characterize GenerateAdjacency, cleaning and vertex-cache optimization using synthetic meshes and exact call options. Derived-mesh caching or an equivalent algorithm may remove repeat work, but require geometry/declaration/topology/epsilon/options/version keys and matching output. Do not skip adjacency or cleaning based on a three-second sample. If another diagnostic game session is already planned, batch mesh-method timing with remaining rendering diagnostics.
3. **Compressed resources:** the established 1 KiB inflate input loop and 607,534 calls merit an offline buffering experiment, but only 7.109 s is measured here. Changing the input immediate alone would overflow the existing buffer; a correct replacement must preserve bounds and archive behavior.

The 89-second presentation gap still lacks a complete attribution. Preserve that uncertainty rather than declaring a disk, shader, CPU or mesh bottleneck for the entire load. Controlled before/after measurement remains necessary before claiming any faster loading.
