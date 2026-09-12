# Loading on the new "X3" bottle (arm64 Wine + FEX): runs 5 and 6

**Moving the game from the x86_64/Rosetta `Steam` bottle to the arm64 `X3` bottle
(FEX x86 emulation, `FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`) deleted the loading
bottleneck we had been chasing and replaced it with a different one.** Same installed
build `1d36c29`, same save, same route.

1. **`ID3DXMesh::GenerateAdjacency` is no longer the load.** Whole run: **69.154 s →
   3.133 s** (8,766 → 8,813 calls), per call **3.30 ms → 0.356 ms** (9.3× faster), and
   the pathological single call went from **26.601 s to 34.6 ms**. The D3DX epsilon
   welding loop is x87 (`fld/fsub/fmul/fcompp`); `FEX_X87REDUCEDPRECISION=1` executes it
   ~9× faster than Rosetta did. The menu load fell from 64.06 s to **9.73 s** and the
   sector change from 24.13 s to **7.98 s**.
2. **The savegame load is now two roughly equal halves and is the only slow phase
   left** (65.46 s, route off). A **25.31 s savegame-decode stall** driven by the
   3-byte `gzread` loop (13,882,714 calls, 3.11 B per call, 9.103 s hooked — and FEX
   made this call **3.2× more expensive per call**, 0.204 → 0.656 µs) and a **24.98 s
   script/XML stall** with only 3.556 s hooked (1,376 `CreateFileA` at 1.43 ms each).
   What FEX fixed was float-heavy engine/D3DX code; what it made worse is exactly the
   millions-of-tiny-cross-DLL-calls pattern of the save decoder.
3. **The sampling profiler is half-blind under FEX.** `GetThreadContext` succeeds
   (1 failure in 94,961 ticks) but reports `Eip = 0x10000` for every guest-code sample
   — a constant outside every module — so the leaf-module split, the basis of the
   old-bottle attribution, is gone. The EBP/stack scan still recovers real X3AP return
   addresses for 81.6 % of engine-thread samples overall, but **during the savegame
   decode it recovers none at all** (four consecutive 5 s blocks with 100 % of slot 0's
   samples carrying no main-module frame and an empty pair table), and it mixes in
   impossible frames (`CPureDeviceStateManager::SetRenderState` ← `__expandlocale`).
   Sampled seconds on this bottle are corroboration, not measurement.

Provenance. Run 5 (route off, `--direct --telemetry`):
`/tmp/x3-bottleX3-run5/session-20260912-170743-352.log`, 24,838,333 B, sha256
`8105f3364968bbb3295d5377d3e4a4efb17716204366ccca08b6d557e0e108d3`, analysis
`verification/results/loading-x3-run5.json`
(`python3 tools/analysis/analyze_iteration08_loading.py <log> --output …`). Run 6
(route on, `--profile --scene-hook --taa --taa-debug --telemetry --object-trace
--object-lifetime --ownership`): `/tmp/x3-bottleX3-run6/session-20260912-171241-212.log`,
295,274,646 B, sha256 `c0fe801ccff79874…`, analysis
`verification/results/loading-profile-x3-run6/{loading-profile.md,loading-profile.json,rvas.txt,symbols.json}`
(`analyze_loading_profile.py <log> --output … --ghidra`; 270 RVAs collected in
`rvas.txt`, 265 symbolized by `X3ProfileSymbols.java`, 147 of the 148 that survive the
top-40 table truncation resolved in the report; 118 labelled function starts). QPC 10 MHz, anchor `proxy_initialize`, 19 hooks,
311 report windows, 74 `scope=delta` profiler blocks over 5.038–370.307 s.
Decompiler output stays in `/tmp/x3-x3run6/` and is untracked. No game was launched
for this analysis. Reference for the old bottle:
[loading-profile-run1.md](loading-profile-run1.md), [iteration08-loading.md](iteration08-loading.md).

**Run 6 loaded a different save than usual.** The user loaded the wrong save, exited
to the main menu, started a **new game** in the usual sector, docked and flew the usual
path. Run 6's phases therefore map as:

| # | Pipeline label | Interval | Gap | Hooked excl. | Unexplained | Actual phase |
| ---: | --- | --- | ---: | ---: | ---: | --- |
| 1 | menu load | 4.18–13.44 s | 8.741 s | 4.982 s | 3.759 s | startup main-menu load |
| 2 | save load | 23.87–68.75 s | 44.540 s | 21.864 s | 22.676 s | load of the *other* save |
| 3 | menu load | 82.60–90.39 s | 7.382 s | 4.616 s | 2.766 s | **return to the main menu** |
| 4 | unlabelled | 121.08–125.37 s | 3.117 s | 2.417 s | 0.700 s | **new game: intro video setup** (DirectShow, §7) |
| 5 | menu load | 123.92–148.92 s | 24.696 s | 12.854 s | 11.842 s | **new game: world build** (3,347 adjacency, 651 MB textures) |
| 6 | sector change | 331.43–338.26 s | 6.173 s | 3.851 s | 2.322 s | sector change in flight |
| 7 | menu load | 362.30–371.16 s | 8.396 s | 4.767 s | 3.629 s | final return to the main menu |

Gaps 4 and 5 are separate presentation gaps 3 s apart; their *accounting* windows
overlap because two report windows straddle both boundaries, so their hooked tables
share content. Together the new game costs **27.8 s** between 121.1 s and 148.9 s.

## 1. Per phase, old bottle versus new

Old-bottle columns are the whole-gap numbers from `loading-profile-run1.md` (same
build, x86_64 Rosetta) and iterations 06–08 where they are the cleaner reference.

| Phase | Old bottle | New bottle, route off (run 5) | New bottle, route on (run 6) | Change |
| --- | ---: | ---: | ---: | ---: |
| Startup menu load | **64.063 s** (it-08: 30.110 / 29.786 s) | **9.726 s** (hooked 5.178, unexpl. 4.548) | 8.741 s (hooked 4.982) | **−6.6× vs run 1, −3.1× vs it-08** |
| Save load | **86.758 s** (it-08: 106.681 / 101.892 s) | **65.460 s** (hooked 25.942, unexpl. 39.517) | 44.540 s (different save) | **−1.33× vs run 1** |
| Menu return | **32.826 s** (it-06/07: 30.115 / 31.099 s) | — (not in run 5) | **7.382 s** and 8.396 s | **−4.4×** |
| Sector change | **24.126 s** | **7.976 s** and 8.784 s | 6.173 s | **−3.0×** |
| New game | not measured | — | 3.117 + 24.696 s | — |

The route (TAA + scene hook + object trace + profiler) costs **1.0 s of the menu load**
and **1.8 s of a sector change** by this pairing; it is not the reason any phase is
slow.

## 2. What FEX changed, per hooked call

Whole-run hooked totals. Old-bottle column from `loading-profile-run1.md` §8.

| Operation | Old bottle (run 1) | run 5 (route off) | run 6 (route on) | Per-call verdict |
| --- | ---: | ---: | ---: | --- |
| `ID3DXMesh::GenerateAdjacency` | 8,766 / 69.154 s / 7.89 ms (3.30 ms without the 6 outliers); **max 26.601 s** | 8,813 / **3.133 s** / **0.356 ms**; max **0.034 s** | 9,876 / 4.649 s / 0.471 ms; max 0.035 s | **9.3× faster**, outliers gone |
| `D3DXCreateMesh` | 8,766 / 0.18 s / ~0.021 ms | 8,813 / **5.096 s** / **0.578 ms** | 9,876 / 2.597 s / 0.263 ms | **12–27× slower** |
| `ID3DXMesh::OptimizeInplace` | 8,766 / 1.96 s / 0.223 ms | 8,813 / 1.698 s / 0.193 ms | 9,876 / 2.405 s / 0.244 ms | unchanged |
| `D3DXCleanMesh` | 8,832 / 0.97 s / ~0.11 ms | 8,832 / 1.741 s / 0.197 ms | 9,896 / 1.620 s / 0.164 ms | ~1.7× slower |
| `D3DXCreateTextureFromFileInMemoryEx` | 12.66 s whole run; worst call **3.48 s** | 1,712 / **3.182 s** / 1.86 ms; max **0.140 s** | 2,676 / 3.504 s / 1.31 ms | **~4× faster**, outlier gone |
| `inflate` | 780,093 / 8.988 s / **11.5 µs** | 780,091 / **12.183 s** / **15.6 µs** | 1,213,528 / 19.708 s / 16.2 µs | **1.36× slower** |
| `gzread` | 13,970,478 / 2.845 s / **0.204 µs** (of which 2.136 s was our wrapper tail) | 13,970,478 / **9.162 s** / **0.656 µs** (wrapper tail only **1.282 s**) | 14,430,560 / 9.720 s / 0.673 µs | **3.2× slower per call, ~11× slower inside zlib** |
| `CreateFileA` | 4,291 / 4.814 s / 1.12 ms | 4,288 / 2.537 s / 0.592 ms | 6,474 / 4.047 s / 0.625 ms | **~1.9× faster** |
| `FindFirstFileA` | 4,625 / ~1.07 s / 0.232 ms; **83 % fail** | 4,611 / 2.559 s / 0.555 ms; **84 % fail** (3,851); one **1.76 s** call | 7,141 / 1.492 s / 0.209 ms | mixed; still 84 % failures |
| `ReadFile` | 103,675 / 0.698 s / 6.7 µs (save load) | 209,790 / 1.695 s / 8.1 µs | 325,978 / 2.918 s / 9.0 µs | ~1.2× slower |
| `xmlReadMemory` | 790 / ~0.21 s | 794 / 0.250 s / 0.316 ms | 1,602 / 0.553 s | unchanged |

The pattern is consistent: **FEX is much faster than Rosetta on long float-heavy
translated loops (D3DX welding, jpeg decode) and slower on every short call that
crosses a module boundary** (`gzread`, `inflate`, `D3DXCreateMesh`, `ReadFile`). Wine
file-API latency is a wash — `CreateFileA` improved, `FindFirstFileA` did not, and the
84 % negative-probe rate is a property of the engine's name resolver, not the bottle.

`gzread`'s own wrapper tail fell from 75 % of its measured time to 14 %, so the new
9.162 s is real callee time: the same 14 M calls that cost ~0.7 s of zlib time on the
old bottle now cost ~7.9 s.

## 3. The save load on the new bottle — 65.460 s, route off

The gap is two ~25 s report stalls with 14 s of instrumented work between them.
Hooked table for the whole gap (run 5):

| Operation | Calls | Incl. s | Mean | Bytes |
| --- | ---: | ---: | ---: | ---: |
| `gzread` | 13,970,460 | 9.162 | 0.656 µs | 43,542,721 (3.12 B/call) |
| `inflate` | 380,599 | 6.066 | 15.9 µs | — |
| `CreateFileA` | 2,362 | 2.159 | 0.914 ms | — |
| `D3DXCreateMesh` | 3,584 | 2.023 | 0.565 ms | — |
| `D3DXCreateTextureFromFileInMemoryEx` | 749 | 1.499 | 2.00 ms | 679,014,071 |
| `ID3DXMesh::GenerateAdjacency` | 3,584 | 1.484 | 0.414 ms | — |
| `D3DXCleanMesh` / `OptimizeInplace` | 3,589 / 3,584 | 0.865 / 0.791 | 0.241 / 0.221 ms | — |
| `ReadFile` | 103,678 | 0.749 | 7.2 µs | 411,936,455 |
| `FindFirstFileA` | 2,467 (1,750 fail) | 0.716 | 0.290 ms | — |
| `xmlReadMemory` | 790 | 0.208 | 0.264 ms | 22,531,669 |

Total hooked exclusive **25.942 s**, unexplained **39.517 s**. Split by stall:

| Stall | Length | Hooked | Content | Nature |
| --- | ---: | ---: | --- | --- |
| **A** 23.55–48.86 s | **25.31 s** | 11.97 s | `gzread` 13,882,714 / 9.103 s; `inflate` 150,279 / 2.549 s; `ReadFile` 38,199 / 0.219 s; `CreateFileA` 189 / 0.045 s | **savegame decode**: the 3-byte read loop of the dispatcher `0x004e9210` and its inflate. Engine CPU + our hook; no Wine waiting to speak of |
| **B** 62.87–87.85 s | **24.98 s** | 3.56 s | `CreateFileA` 1,376 / 1.968 s (**1.43 ms/call**); `FindFirstFileA` 1,398 / 0.679 s (0.486 ms); `xmlReadMemory` 790 / 0.208 s; `inflate` 13,292 / 0.208 s | **script/XML phase** (run 1's S3, 12.71 s there). **21.4 s is unhooked**; the frame evidence in run 6's equivalent stall is CRT `fread` 15.3 %, `_malloc` 15.5 %, CRT file-lock release `0x00517d8f` 15.2 %, resource load `0x004e8e10` 10.7 % — a per-file open/read/close/allocate pattern over ~1,400 script files |
| remainder | ~15.2 s | ~10.4 s | mesh preparation 5.163 s (`CreateMesh` 2.023 + adjacency 1.484 + `CleanMesh` 0.865 + `Optimize` 0.791), textures 1.499 s, the rest of `inflate` 3.3 s | object/mesh/texture construction |

**Which functions, and engine CPU or Wine waits?**

* Stall A is the read dispatcher loop. Its 13.3 s of unhooked time is the engine's own
  code around 13.9 M `gzread` calls (the XOR-0x33 slice and per-record deserialization
  documented for `0x004e9210` in run 1). Run 6 confirms the *location* negatively and
  exactly: for the four 5 s profiler blocks covering 25–50 s, **100 % of the engine
  thread's samples carry no X3AP frame and the pair table is empty**, while the only
  hooked activity in those windows is `gzread`/`inflate`. Nothing else in the engine can
  be running. It is **CPU, not a Wine wait**: the hooked Wine file APIs in that stall
  total 0.264 s of 25.31 s.
* Stall B is the **script/save-XML file storm**: ~1,400 opens plus ~1,400 directory
  probes (56 % of them failing) at Wine round-trip latency, but those only account for
  2.65 s. The remaining 21.4 s sits in the engine's CRT read path and allocator
  (`0x00511f77` `fread` body → `__VEC_memcpy`, `_malloc`, `___lock_fhandle`,
  `0x00517d8f`) around those files. The script VM `0x004ab880` itself is again not
  visible (0 samples in run 6's save gap), matching run 1: the cost is in the per-file
  I/O path, not in script execution.
* No phase of the save load shows the old bottle's 15.4 % `ntdll` share, but that
  comparison cannot be made on this bottle at all (§5): the leaf module is unreadable.
  The only quantified Wine-side cost is the hooked file APIs, **3.70 s of the 65.46 s**.

## 4. Per-gap sampled attribution (run 6)

Read these as *ranked corroboration with the caveats of §5*, never as seconds. Shares
are of the engine thread (slot 0, tid 216) — the only thread that ever executes engine
code (it is the only slot that ever carries an X3AP frame); of the 55 other threads
seen, two also report the unresolvable `0x10000` leaf and the rest sit in the same
`ntdll.dll+0x4dd1c` wait thunk for the whole run.

| Gap | Engine samples | ms/sample | No X3AP frame | Top frames (share of engine samples) |
| --- | ---: | ---: | ---: | --- |
| 1 startup menu 8.74 s | 4,997 | 1.75 | 22 % | resource load `0x004e8e10` **18.1 %**; `SetRenderState 0x004b4f80` 5.8 %; object construction `0x0043ce30` 4.5 %; collision split `0x004e1220` 4.2 %; `_malloc` 3.8 %; `CloneMesh 0x004bcb60` 2.7 %; mesh prep `0x004bc680` 2.5 % |
| 2 save load 44.54 s | 15,879 | 2.80 | **55 %** | material submission `0x004c4fc0` 6.6 % (stale, §5); resource load 6.0 %; CRT `fread` 5.7 %; `_malloc` 3.7 %; CRT lock release `0x00517d8f` 3.5 %; `__VEC_memcpy` 2.1 %; mesh prep 1.7 %; collision split 1.7 % |
| 3 menu return 7.38 s | 4,503 | 1.64 | 5 % | `SetRenderState` **19.2 %**; resource load **17.9 %**; **OBB overlap test `0x004e3280` 9.1 %**; text/overlay `0x004efa70` 4.1 %; collision split 3.9 % |
| 4 new-game video 3.12 s | 2,132 | 1.47 | 12 % | **Win32 input/message pump `0x004d5a90` 17.3 %**; CRT `fread` 13.3 %; resource load 12.2 %; `_malloc` 10.9 %; `__isleadbyte_l` 6.5 %; **DirectShow movie setup `0x004cf460` 4.8 %** |
| 5 new-game world 24.70 s | 7,279 | 3.39 | 6 % | resource load **26.7 %**; `_malloc` **13.8 %**; `__VEC_memzero` 6.2 %; `CloneMesh` 5.4 %; message pump 5.2 %; CRT `fread` 4.4 %; mesh fill `0x004bb470` 2.6 %; collision split 2.5 % |
| 6 sector change 6.17 s | 2,378 | 2.60 | 5 % | `SetRenderState` 15.6 %; `CloneMesh` **10.1 %**; `__VEC_memzero` 9.7 %; message pump 7.4 %; resource load 6.5 %; CRT `fread` 5.7 %; `__VEC_memcpy` 5.5 %; mesh fill 4.5 % |
| 7 final menu 8.40 s | 2,315 | 3.63 | 7 % | resource load **24.3 %**; **trig/orientation `0x0044c620` 12.1 %**; collision split 6.3 %; `CloneMesh` 3.6 %; texture loader `0x004dc540` 3.6 % |

Consistent with the hooked tables: on this bottle the loading phases are dominated by
the **archive/resource read path** (`0x004e8e10` open+read+close, `0x004e8880` resource
read, CRT `fread`, `_malloc`, `inflate`) and by **mesh construction**
(`0x004bb470`/`0x004bcb60`/`0x004bc680`), not by D3DX welding. The collision-tree build
(`0x004e1220`) is still present in every phase at 1.3–6.3 %, i.e. ≤0.5 s per gap — an
order of magnitude less prominent than on the old bottle.

## 5. The profiler under FEX

Whole run 6, 74 delta blocks, 370.2 s covered:

| Quantity | Old bottle (run 1) | New bottle (run 6) |
| --- | ---: | ---: |
| Achieved tick rate (nominal 500/s) | 192.5 /s | **256.5 /s** |
| Per-tick cost, mean / max | 1,959 µs / 85.1 ms | **1,429 µs / 52.1 ms** |
| Threads sampled per tick / seen / cap | 15.89 / ≤30 / 32 | **25.16 / 56 seen / 32** (up to **24 unsampled**) |
| Sampler busy (one host core) | 177.4 s = 37.7 % | 139.5 s = **37.7 %** |
| Per suspended thread | ~123 µs → 2.37 % of wall | **~57 µs → 1.46 % of wall** |
| Dropped samples | 0 | 0 |
| Engine-thread sample resolution | 3.5–7.8 ms | **1.5–3.9 ms** |
| `SuspendThread` / `GetThreadContext` failures | — | 6,323 (dying threads; none on slot 0) / **1** |
| Engine-thread leaf in a known module | 96.7 % | **0 %** |
| Engine-thread samples with no X3AP frame | ~? (leaf-based analysis used instead) | **18.4 % overall, 100 % during the save decode** |

**Three concrete accuracy defects, all FEX-specific:**

1. **The leaf PC is a constant.** Every guest-code sample reports
   `slot=0 module=65535 name=- rva=0x10000`. `GetThreadContext` returns success and the
   `Ebp`/`Esp` it reports are usable, but `Eip` is a fixed value outside every pinned
   module — FEX parks the guest at a thunk rather than materializing the guest PC. Leaf
   RVAs, the per-thread module split and every `self_*` share in
   `loading-profile.json` are therefore **void on this bottle** (`x3ap 0, d3dx 0,
   zlib 0, proxy 0` in every gap; the split degenerates to "guest code" (`other`)
   versus "parked in an `ntdll` wait" (`ntdll.dll+0x4dd1c`), and even that distinction
   cannot separate a Wine wait entered from guest code).
2. **Whole phases resolve to nothing.** During the savegame decode (blocks ending
   30.0, 35.1, 40.1, 45.1 s) *every* thread reports exactly `ticks` samples with one
   constant leaf, **zero frames** and an **empty pair table** (`table_used=11,11,0`).
   18.4 % of the engine thread's samples over the whole run and 55 % inside the save gap
   never resolve to X3AP. The artifact also appears in flight blocks (40–70 %). The
   sampler is not wrong about time there — the hooks agree the phase is the gzread loop
   — but it cannot name a function in it.
3. **Stale frames are frequent, not exceptional.** The pair table contains impossible
   edges: `CPureDeviceStateManager::SetRenderState` ← `__expandlocale` (106 samples,
   gap 1) and ← `_malloc` (64, gap 6); `0x0043ce30` ← `_malloc` (193, gap 1);
   and the whole render chain `0x004c4fc0` ← `0x0047d9c0` (896 samples) inside a 44 s
   presentation gap where no frame is drawn. Run 1 had one such row
   (`__VEC_memzero`); here it is several of the top rows of every gap.

Net: on this bottle **the hooked tables are the measurement and the frame table is a
ranking hint**. Raising accuracy needs a guest-PC source that does not come from
`CONTEXT.Eip` (for example reading the innermost frame's return address as the
"leaf"), plus a `threads_max` above 32 — 24 threads went unsampled at peak, and the
per-block top-48 row budget is spent on threads whose leaf never moves.

## 6. New function roles

Decompiled 2026-09-12 into `/tmp/x3-x3run6/unknowns.txt` (untracked). Added to
`tools/analysis/x3ap_function_labels.json`.

| Function | Role | Evidence |
| --- | --- | --- |
| `0x004b4f80` | `CPureDeviceStateManager::SetRenderState` | filters through `FUN_004b5620`, then `(*(vtable+0xe4))(device, state, value)`; already named in [constant-uploads.md](constant-uploads.md) |
| `0x004e3280` | **OBB/OBB separating-axis overlap test** — 15 axes (3 + 3 + 9 cross products), `fabsf` `0x0040e710` on each, margin `_DAT_00565600` = 1e-6; returns the failing axis index 1…15, 0 on overlap | 175-line x87 chain; caller `0x004e2530` |
| `0x004e2530` | **collision query: recursive OBB-tree pair descent** — SAT test first, children at `+0x3c`/`+0x40`, half-extents at `+0x30…+0x38`, leaf pair test `0x004e2190`, recurses into itself on the larger node; budget counters `DAT_0060854c`/`DAT_00608538`, hit counter `_DAT_00608544` | decompile |
| `0x004d5a90` | **Win32 input/message handling** (`GetKeyboardLayout`, `MapVirtualKeyExA`, `Ordinal_2`), 561 lines, helper `0x004d6850` | decompile |
| `0x004cf460` | **DirectShow movie playback setup**: `CoCreateInstance` of `"X File Source"`, `"X MPEG-I Stream Splitter"`, `"X MPEG Video Decoder"`, `"X MPEG Audio Decoder"`, `"X MPEG Layer-3 Decoder"`, `"Output"` pin; paths `"%sData\\mov\\%s"`, `"%05d.dat"` | decompile; caller `0x00498140`; 4.8 % of run 6 gap 4, which is the new game's intro video |
| `0x00498140` | movie playback driver (allocates, then `0x004cf460`) | decompile |
| `0x0043ce30` | **type-table object construction**: type lookup `0x00492970`, node create `0x00486d10`, then a per-part loop over the type's table (`+0x50`, stride 0x44, model id at `+0xc`) calling `0x0043d1d0`, under SEH | decompile |
| `0x004f8600` | text/overlay layout and string formatting (`"enOverlay"`, a German free-disk-space message), calls `0x004efa70` | decompile |
| `0x004efa70` | overlay/glyph state helper: `0x004dce20` → `0x004ee6e0` == 0x18 → `0x004b2730` | decompile |
| `0x0044c620`, `0x0044ccc0` | **orientation/rotation math** (`fsin`, `fcos`, `fpatan`, helper `0x0052b5d0`), called from `0x0042fb20` | decompile; 12.1 % / 2.4 % of run 6 gap 7 |
| `0x004f22c0` | small `fpatan` angle helper | decompile |
| `0x00517d8f` | CRT file-lock release tail (`LeaveCriticalSection`), reached from `0x00524cfa` | decompile; 15.2 % of run 6's script/XML stall |
| `0x0051f464` | CRT `_read` text-mode body (`ReadFile`, `MultiByteToWideChar`, `__malloc_crt`) | decompile |
| `0x004e6a40` | `malloc` + `memset` allocation wrapper (via `0x004b8b60`) | decompile |
| `0x004e29f0` | collision helper wrapper (`0x004e2780`, `0x0052b5d0`) | decompile |
| `0x004eeab0` | thin wrapper over `0x004db520` | decompile |

## 7. Ranked candidates for the new bottle

Bounds are run 5 (route off) seconds and assume the named work goes to zero. They are
opportunity ceilings, they overlap, and they must not be summed.

| # | Candidate | Measured bound on the new bottle | Was, on the old bottle | Mechanism |
| ---: | --- | ---: | ---: | --- |
| 1 | **Buffer the savegame read loop** (`0x004e9210`, 3.12 B per `gzread`) | **25.31 s** (the whole decode stall), of which **11.97 s is hooked** (`gzread` 9.103 + `inflate` 2.549 + `ReadFile` 0.219). Whole run: `gzread` 9.162 s + `inflate` 12.183 s | 8.3 s (run 1 rank 4) | read-ahead in the dispatcher, or a buffering `gzread` hook: 13.9 M calls → ~11 k at 4 KiB. FEX tripled the per-call price, so this is now the largest single item in the run |
| 2 | **The script/XML phase** (stall B) | **24.98 s**, only **3.556 s hooked** (1,376 opens at 1.43 ms, 1,398 probes, 790 `xmlReadMemory`) | 12.71 s (run 1 S3) | not yet attributable — needs a targeted probe (per-file timing around `0x004e8e10`/`0x004e7590` and the XML path) before a mechanism can be chosen. Biggest unknown left |
| 3 | **`inflate` chunking** (1 KiB input loop) | **12.183 s** whole run (780,091 calls at 15.6 µs); **2.484 s of the 9.73 s menu load**, 6.066 s of the save load, 1.022 s of a sector change | 8.99 s (rank 7) | widen the input chunk (needs the stack buffer enlarged); FEX's per-call penalty makes call-count the lever |
| 4 | **Cache the prepared mesh** (`X3M_MESH_CACHE`) | **11.668 s** whole run (`D3DXCreateMesh` 5.096 + adjacency 3.133 + `CleanMesh` 1.741 + `OptimizeInplace` 1.698); **1.309 s menu**, **5.163 s save**, **3.508 s sector** | 72.26 s (rank 2) | content-keyed cache; note `D3DXCreateMesh` is now the *largest* member, 12–27× its old per-call cost |
| 5 | **Fast exact-match adjacency** (`X3M_MESH_ADJACENCY=fast`, in flight) | **3.133 s whole run** — **0.379 s** of the menu load, **1.484 s** of the save load, **0.865 s** of a sector change; per call 0.356 ms, worst call 34.6 ms | **69.154 s** whole run, 33.945 s menu, 18.771 s save, worst call 26.601 s | unchanged design; the bound is now **22× smaller**. Still worth shipping (it is written, and removes the worst-case tail), but it is no longer a loading fix — ~0.4 s per menu |
| 6 | **Cut the failed name probes / cache negative resolution** (`0x004e7590`) | **6.861 s** whole run of Wine file APIs (`CreateFileA` 2.537 + `FindFirstFileA` 2.559 + `ReadFile` 1.695 + `FindNextFileA` 0.072); **3.696 s inside the save load**; 3,851 of 4,611 `FindFirstFileA` calls fail; one 1.76 s call | 7.50 s (rank 6) | memoize negative results in the proxy hooks; the win is call count, not code speed |
| 7 | **Converted-texture cache** (`0x004dd2c0`/`0x004dc540`) | **3.182 s** whole run (1,712 calls, 1.86 ms each); 1.499 s save, 0.785 s menu; worst call 0.140 s | 12.66 s (rank 5) | content-keyed cache of the converted surface |
| 8 | Collision tree build (`0x004e0c80`) and OBB query (`0x004e2530`) | not quantifiable on this bottle; by frame share ≤ **0.5 s** per gap (`0x004e1220` 1.3–6.3 %), and the query `0x004e3280` 9.1 % of the 7.38 s menu return | ≥3.3 s menu, ≥5.3 s save (rank 3) | keep the tree alive across the teardown; re-measure only if §5's defects are fixed |
| 9 | Our own instrumentation | `gzread` wrapper tail **1.282 s**; profiler suspension **1.46 % of wall** (≈0.96 s of the save load); sampler busy 37.7 % of one host core | 2.37 s tails + 2.37 % suspend | drop `--profile` and the `gzread` hook for timing runs |

**The shape of the problem changed.** On the old bottle 4 of the top 5 candidates were
D3DX/float work and the fix was to stop calling D3DX. On this bottle the top three are
**call-count problems on tiny cross-module calls** (`gzread`, `inflate`, per-file
opens) plus one **unattributed 25 s engine phase**. The adjacency work already built
should be finished and shipped for its worst-case tail, but the next measurement effort
belongs on the savegame decode loop and the script/XML stall.

## 8. Limits

* Run 5 and run 6 loaded **different saves** (13,970,460 vs 14,430,515 `gzread` calls),
  so their save-load lengths are not comparable to each other. Run 5 is the reference
  for the usual save; run 1 is the reference for the old bottle with that same save.
* Every sampled share in §3 and §4 inherits §5: the leaf split is void, 18.4 % of
  engine-thread samples (55 % in the save gap) resolve to no function at all, and some
  top rows are stale frames. Shares are lower bounds where frames resolve and
  meaningless where they do not; the seconds in §7 come from hooks, not samples.
* Hooked seconds are completion deltas of report windows overlapping the interval; only
  the exclusive column may be added and it is a lower bound. Two report windows straddle
  every gap boundary; gaps 4 and 5 share windows.
* Gap labels are the pipeline's mechanical rules (`label_gap`); the phase column in the
  timeline table is the user's reported path, not a log marker.
* Nothing here implements or measures a loading improvement, and no comparison isolates
  FEX from the other bottle differences (arm64 Wine build, `WINEMSYNC=1`, a different
  wined3d, host CPU state). "What FEX changed" means "what changed with the bottle".
