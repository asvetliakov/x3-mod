# Route CPU cost attributed — iteration 9, run 1

[iteration-09.md §1.4](../archive/iteration-09.md) measured the live motion route at
~5.3 ms of engine-thread CPU per routed frame and asked where it goes. This
document answers that from the same log, by combining the route's own telemetry
spans with the in-process sampling profiler that ran throughout the session.

**Verdict: the route's per-draw cost is one thing.** About 21 self
`ReadProcessMemory` calls per routed draw — each a real `NtReadVirtualMemory`
Wine syscall — account for **92–98 % of the gate span**. Telemetry `QueryPerformanceCounter`
stamps are the second item at 1.5–2.1 ms/frame, and the four `SetRenderTarget`
calls the `perdraw` policy pays per routed draw are only the third at
0.6–1.1 ms/frame — so `--motion-rt-mode lazy`, the fix iteration 9 reached for,
addresses the smallest of the three.

## Provenance and reproduction

| | |
| --- | --- |
| log | `/tmp/x3-iteration09-run1/session-20260912-160404-1632.log`, 154,442,312 B |
| installed DLL | `db63e120afcbb38e1382f22dffb96fb6030bbab50d1c3faf2b5587e055ce7e3d` (`1d36c29`) |
| engine thread | slot 0, tid 1260 (= `profile_start init_tid`), the only D3D-calling thread |
| session | 470.4 s of delta reports, 90,546 engine-thread samples, 5.196 ms of engine wall time per sample |
| tool | `tools/analysis/analyze_route_cost.py` (paired test `verification/analysis/test_route_cost.py`, 11 cases) |
| results | `verification/results/route-cost-run1.json`, `…-run1.txt` |

```
python3 tools/analysis/analyze_route_cost.py <log> \
  --proxy-dll "$BOTTLE/drive_c/X3/d3d9.dll" \
  --ntdll "$CROSSOVER/lib/wine/i386-windows/ntdll.dll" \
  --engine-tid 1260 --top 30 \
  --window menu-noroute=76:122 --window flight-early=212:232 \
  --window flightA=250:300 --window flightB=340:370 --window flightC=409:428 \
  --window load-gap=125:210 --output verification/results/route-cost-run1.json
```

Windows around the five capture bursts are derived from the frame records that
report `readbacks > 0`, padded 5 s; the flight and menu windows are given
explicitly. Reports are attributed by overlap, so a window's `covered_s` is the
union of the report intervals it selected and is wider than the requested span;
frames and elapsed time are both taken over `covered_s` so ms/frame is
self-consistent.

### Symbolization confidence

**High, and machine-checked.** Both binaries are the ones this session loaded,
and the tool refuses to symbolize unless the layout matches:

| module | `.text` in the binary | `text_rva`/`text_size` in the log's module table | verdict |
| --- | --- | --- | --- |
| our `d3d9.dll` (installed, `db63e120…`) | rva `0x1000`, span `0xd7000` | rva `0x1000`, span `0xd7000` | matches |
| `ntdll.dll` (CrossOver Preview `i386-windows`) | rva `0x1000`, span `0x6b000` | rva `0x1000`, span `0x6b000` | matches |

The installed proxy DLL retains its DWARF debug sections, so proxy RVAs resolve
to function *and* `file:line` via `addr2line`. The tree's `build/d3d9.dll` was
rebuilt by another agent (`8f86f976…`) and was **not** used; a scratch rebuild of
`git show 1d36c29` reproduced the same `.text` size and VMA (`0xd6a48` at
`0x6fb41000`), which is the independent confirmation that the layout above is
this commit's. ntdll RVAs are resolved to the nearest preceding export: every
sampled address is a Wine syscall thunk at a fixed `+0xc`, the return site of
the syscall instruction, which is why the names below are all `Zw*+0xc`.

### What this log cannot answer

The deliverable asked for the top proxy leaf functions with inclusive-by-caller
time and a proxy call-pair table. **Neither exists in this log**, for two
reasons in `src/proxy/sampling_profiler.cpp`:

* `profile_frame` and `profile_pair` RVAs are **main-module (X3AP.exe) addresses
  only** — `aggregate()` walks the merged stack for the nearest address inside
  the main image and records `no_frame` otherwise. The pair table therefore says
  which *game* call site called which, never which route function called which.
  The x3ap pair tables are in the JSON (`x3ap_pairs`) and are dominated by the
  game's draw-submission chain (`0xb4f99` ← `0xc403e`, 955–1813 counts per
  flight window), i.e. the call sites that enter the proxy, not its interior.
* `profile_leaf` rows *do* carry a module index, so proxy leaves are
  identifiable — but the per-report tables are truncated (top 48 delta rows,
  256 cumulative, across all threads). The proxy's samples are spread thinly
  over hundreds of RVAs, so **only 0–11 % of them are listed** in the gameplay
  windows (`proxy.coverage` in the JSON). The listed rows are real but are a
  biased tail sample and cannot be ranked into a top-30.

Leaf, frame and pair tables are also separate aggregates with no per-sample
join, so a proxy leaf cannot be attributed to the game frame above it.

The instruments that *do* answer the question are the exact per-thread
leaf-kind counters and the ntdll syscall rows (89–96 % coverage), because every
syscall our code makes is named there.

## 1. Engine-thread module split

`ms/frame` is engine-thread CPU, never GPU time.

| window | frame time | x3ap | our `d3d9.dll` (self) | ntdll | wined3d etc. | d3dx9 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| menu, route off (76–122 s) | 46.45 ms | 71.0 % / 33.0 | **3.5 % / 1.60** | 9.1 % / 4.22 | 4.4 % / 2.06 | 11.4 % / 5.29 |
| flight, early (212–232 s) | 37.50 ms | 55.1 % / 20.7 | **5.3 % / 1.98** | 23.3 % / 8.72 | 6.2 % / 2.31 | 9.4 % / 3.52 |
| flight A, heavy (250–300 s) | 49.09 ms | 57.7 % / 28.3 | **5.7 % / 2.81** | 21.1 % / 10.34 | 5.5 % / 2.71 | 9.5 % / 4.67 |
| flight B, steady (340–370 s) | 34.87 ms | 61.0 % / 21.3 | **4.9 % / 1.72** | 20.1 % / 7.00 | 5.0 % / 1.76 | 8.5 % / 2.95 |
| flight C, sector change (409–428 s) | 38.08 ms | 48.7 % / 18.6 | **4.8 % / 1.82** | 21.1 % / 8.03 | 5.5 % / 2.09 | 18.9 % / 7.21 |
| burst 1 (225–240 s) | 36.65 ms | 53.3 % / 19.5 | **7.1 % / 2.60** | 26.3 % / 9.64 | 4.9 % / 1.81 | 7.9 % / 2.90 |
| burst 2 (235–250 s) | 48.76 ms | 48.7 % / 23.8 | **6.9 % / 3.34** | 32.2 % / 15.68 | 4.5 % / 2.19 | 7.3 % / 3.58 |
| burst 3 (295–310 s) | 73.42 ms | 47.0 % / 34.5 | **6.9 % / 5.06** | 32.1 % / 23.54 | 4.7 % / 3.43 | 9.1 % / 6.65 |
| burst 4 (360–380 s) | 25.79 ms | 58.7 % / 15.2 | **5.3 % / 1.36** | 22.2 % / 5.72 | 6.1 % / 1.58 | 7.1 % / 1.84 |
| burst 5 (415–430 s) | 29.07 ms | 55.3 % / 16.1 | **5.5 % / 1.60** | 26.1 % / 7.59 | 5.8 % / 1.69 | 6.7 % / 1.96 |

### Answer to (1): what fraction of the engine thread is in the proxy

Two numbers, and the second is the one that matters:

* **proxy self code: 4.8–5.7 %** of the engine thread in flight (1.7–2.8 ms/frame),
  against 3.5 % in the menu with the route off. Our own instructions are a small
  part of the cost.
* **our total footprint, including the syscalls our code makes: 20.7 % (flight B)
  to 22.9 % (flight A)**, i.e. 7.2–11.2 ms/frame — proxy self plus
  `ZwReadVirtualMemory` plus `ZwQueryPerformanceCounter` plus `ZwWriteFile`,
  every one of which has us as its only caller in this process. Plus an unknown
  part of wined3d (below).

That is consistent with the route's own telemetry, which reports an exclusive
route total of **4.59 ms/frame (flight B)** and **8.33 ms/frame (flight A)** —
13.2 % and 17.0 % of the frame. The remainder of the 20.7–22.9 % is the object
observer's baseline reads and our logging, which are separate features the route
spans do not cover.

## 2. Inside the proxy: where the route's CPU goes

### 2.1 ntdll syscalls, per frame (all named, 89–96 % of the ntdll samples listed)

| syscall | our caller | menu (route off) | flight B | flight A | burst 3 |
| --- | --- | ---: | ---: | ---: | ---: |
| `ZwReadVirtualMemory+0xc` | `ReadProcessMemory` in `object_trace`, `object_lifetime`, `scene_hook` | 0.48 | **3.66** | **5.97** | 7.81 |
| `ZwQueryPerformanceCounter+0xc` | every telemetry `stamp()` | 1.58 | **1.47** | **2.14** | 3.43 |
| `ZwWriteFile+0xc` | log flush | 0.00 | 0.38 | 0.29 | 8.84 |
| `ZwDelayExecution+0xc` | the game's own frame pacing | 0.94 | 0.68 | 0.79 | 0.74 |
| `ZwAlertThreadByThreadId+0xc` | futex wake (not route-specific: highest in the menu) | 0.90 | 0.50 | 0.74 | 1.09 |
| `RtlEnter/LeaveCriticalSection` | lock traffic | – | – | – | 0.30 |

Only our code calls `ReadProcessMemory` in this process (three sites, all ours),
so every `ZwReadVirtualMemory` sample is ours. The menu window, where the route
routed nothing, gives the non-route baseline: **0.48 ms/frame** is the object
observer, and everything above it is the route.

### 2.2 The route's own spans

| | flight B | flight A | session, routed span |
| --- | ---: | ---: | ---: |
| draws / routed per frame | 275 / 191 | 463 / 354 | 316 / 223 |
| `route_gate` per **routed** draw | 16.95 µs | 17.44 µs | **16.65 µs** |
| `route_gate` per **rejected** draw | ~0 | ~0 | **0.33 µs** |
| `route_draw` (apply + undo) per routed draw | 6.71 µs | 6.66 µs | 6.68 µs |
| `route_set_rt` per call (4 per routed draw) | 0.79 µs | 0.77 µs | 0.78 µs |
| exclusive route (`gate` + `route_draw` + `fill`) | 4.59 ms/frame | 8.33 ms/frame | 5.31 ms/frame |
| `set_rt_us` (nested in `route_draw`) | 0.61 ms/frame | 1.10 ms/frame | 0.70 ms/frame |
| telemetry stamps (QPC calls) per frame | 4,382 | 7,831 | 5,081 |

**The 8.15 µs "per draw" in iteration 9 is an artefact of averaging.** The gate
costs **16.65 µs on a routed draw and 0.33 µs on a rejected one**; the session
mean is diluted by ~724 k menu draws that exit at gate 2. The per-window
regressions are unstable (`routed` and `rejected` barely vary inside one window,
so the split can come out slightly negative); the session-wide fit over 87
non-capture frame records is the number to use.

### 2.3 Attribution of the gate

`MotionOutput::before_draw` brackets `evaluate_draw` and subtracts the apply,
the fill and any lazy flush, so the gate span contains no `SetRenderTarget`, no
constant upload and no `stamp()` of its own. What it does contain, for a draw
that reaches gate 5, is `sample_scope`, and that is two functions of
`ReadProcessMemory` calls:

| reads | function | purpose |
| ---: | --- | --- |
| 1 | `object_trace::current` — node block (0x150 B) | `node_handle`, `model`, `lod` (and position/scale/basis, diagnostics only) |
| 1 | " — `camera + 0x28` | `camera_handle` |
| 2 | " — engine slot, `engine + 0xc` | registry pointer |
| 8 | " — world, world-basis, view, projection (pointer + 64 B each) | **diagnostics only; the route never reads them** |
| 2 | `object_lifetime::read_registry` | re-reads the same engine slot and `engine + 0xc` |
| ~7 | `object_lifetime::lookup` ×2 (header, bucket, chain nodes) | node and camera birth serials |
| **~21** | | |

Measured against that count:

| window | `ZwReadVirtualMemory` | − observer baseline | per routed draw | `route_gate` per routed draw | share of the gate |
| --- | ---: | ---: | ---: | ---: | ---: |
| flight B | 3.66 ms | 3.18 ms | 16.7 µs | 16.95 µs | **98 %** |
| flight A | 5.97 ms | 5.49 ms | 15.5 µs | 17.44 µs | **89 %** |
| flight early | 3.29 ms | 2.81 ms | 14.7 µs | 13.61 µs | ~100 % |

So ~21 Wine syscalls at **~0.75 µs each** are the gate. Everything else the gate
does — the selector event, the shader-profile pair lookup, five render-state
queries (`rs_queries` 5,542 with `rs_hits` 5,542 and only 14 real backend gets,
so the state shadow is already doing its job), the history hash lookup and the
cut detector's `sqrt` — fits in the ≤ 1.5 µs residual, and matches the 0.33 µs
that a rejected draw costs.

### 2.4 Apply/undo, `SetRenderTarget`, stamps, logging, locks

* **apply/undo** (`route_draw`, 6.68 µs per routed draw = 1.28 ms/frame on
  flight B, 2.36 ms on flight A): of that, **four `SetRenderTarget` calls are
  3.1 µs (47 %)**; the rest is two shader sets, two constant uploads
  (`SetVertexShaderConstantF` c216 rows, `SetPixelShaderConstantF` the pixel
  ABI), two `SetRenderState` write-mask writes and the undo.
* **`SetRenderTarget` lands in the backend, not in us.** Those samples are in
  the `wine` kind (wined3d), which is 1.76–2.71 ms/frame in flight. The route's
  own `set_rt_us` is 0.61–1.10 ms/frame, so **the route is roughly 35–40 % of
  all wined3d leaf time in a scene frame**. The sampler cannot confirm that by
  caller (frames are x3ap-only); the telemetry span is the evidence.
* **telemetry stamps**: `MotionOutput::stamp()` is `QueryPerformanceCounter`
  when telemetry is on and returns 0 otherwise, and under Wine QPC *is* a real
  syscall (`ZwQueryPerformanceCounter+0xc` is a sampled leaf). 4,382 stamps per
  flight-B frame and 7,831 per flight-A frame, against a measured QPC line of
  1.47 and 2.14 ms/frame — **≤ 0.33 µs per stamp**. The menu window
  (670 draws x 4 stamps — the gate pair and the `draw_backend` pair — so ~2,680
  per frame, against a 1.58 ms QPC line) shows part of the QPC line is the
  game's own, so the stamps' share is 0.9–1.5 ms/frame (flight B) and
  1.3–2.1 (flight A).
* **object-trace reads**: §2.3, 3.2–5.5 ms/frame of route cost.
* **logging**: 0.29–0.38 ms/frame in steady flight, but **1.1–8.8 ms/frame
  inside the capture bursts** (`ZwWriteFile` is the largest single line in
  bursts 2 and 3). A capture burst is not a valid performance sample.
* **locks**: `RtlEnter/LeaveCriticalSection` is 0–0.30 ms/frame and only visible
  in the bursts; the `lock_wait` metric is 0.2 µs over 332 windows.
  `ZwAlertThreadByThreadId` (0.50–0.74 ms/frame) is *higher* in the menu
  (0.90 ms) and is therefore not route traffic.

### Answer to (3): is the 8.15 µs dominated by one thing

**Yes — by the object-scope `ReadProcessMemory` syscalls, not by QPC.** The
"~4 stamps per draw" hypothesis is wrong for the gate: the gate's own stamp pair
is *outside* the interval it measures and the apply's pair is subtracted, so the
gate span contains no QPC at all. The per-draw QPC traffic (gate pair +
`draw_backend` pair, plus ten more pairs on a routed draw) is real but costs
1.5–2.1 ms/frame against the gate's 3.2–6.2 ms.

## 3. Expected savings, ranked

Per frame, for a steady 35 ms scene frame (flight B: 275 draws, 191 routed) and
a heavy 49 ms one (flight A: 463 draws, 354 routed).

| # | change | flight B | flight A | confidence |
| ---: | --- | ---: | ---: | --- |
| 1 | Replace `ReadProcessMemory(GetCurrentProcess(), …)` with direct reads of ranges validated once (the pattern `sampling_profiler.cpp` already uses: `VirtualQuery` inside a pinned mapping, then plain loads), in `object_trace`, `object_lifetime` and `scene_hook` | **−3.2 ms** | **−5.5 ms** | high — this is the whole measured `ZwReadVirtualMemory` line above the observer baseline |
| 2 | Stop reading what the route does not use: the four engine matrices are 8 of `object_trace::current`'s 12 reads and are diagnostics only (the submitted rows come from the shader-constant shadow). Gate them on `capture_` | **−1.2 ms** | **−2.1 ms** | high — 8 of the route's 21 reads; the cheap fallback if 1 is refused, not an addition to it |
| 3 | Drop per-draw telemetry spans in production (keep the per-frame counters and `X3M_TELEMETRY=1` for diagnosis) | **−0.9…−1.5 ms** | **−1.3…−2.1 ms** | medium — 4,382/7,831 stamps per frame are exact; the per-stamp cost is bounded at ≤ 0.33 µs but not separated from the game's own QPC |
| 4 | Share one registry read between `object_trace::current` and `object_lifetime::read_registry` (they read the same engine slot and `engine + 0xc` back to back) | −0.3 ms | −0.5 ms | high — 2 of 21 reads |
| 5 | `--motion-rt-mode lazy` | −0.2…−0.6 ms | −0.4…−1.1 ms | low — the upper bound is the whole `set_rt_us` (0.61/1.10 ms); the fixture's burst case gets 20 → 12 binds per frame, but with 69 % of draws routing and rejections interleaved the run structure in gameplay is unmeasured. This is run 4 of the plan |
| 6 | Memoize the scope snapshot per node within a frame (the same node is drawn in several passes/LODs) | −0.5…−1.5 ms? | −1…−3 ms? | speculative — needs the per-frame distinct-node histogram; the log does not carry it outside capture frames |
| 7 | Do not read performance off a capture burst | — | — | measurement hygiene: logging alone is 1.1–8.8 ms/frame there |

Items 2 and 4 are subsets of item 1 (the same reads), so they are the fallback
if 1 is refused, not additions to it. Items **1 + 3 + 5** are independent and
together remove **about 4.8 ms/frame on flight B and 7.9 ms on flight A** — that
is, essentially all of the route's measured CPU cost — with no change to any
rendering behaviour. Item 1 is the one to do first and it is portable: a `VirtualQuery`-validated
direct read is documented Win32 and is what the profiler already does, so it
does not add a backend-private prerequisite.

### Answer to (4): lazy RT mode and disabling stamps

Neither is the fix. Lazy RT mode targets `set_rt_us`, which is 0.61–1.10 ms of a
4.6–8.3 ms route cost and is *nested inside* `route_draw`, so it cannot save
more than that; `docs/verification/motion-output.md` §"Lazy RT binding
equivalence" reports 20 → 12 binds per frame in the burst fixture (−40 %) and
`set_rt` unchanged at four per routed draw in the regular fixture, which brackets
the gameplay expectation at −0.2 to −0.6 ms/frame. Disabling the stamps is worth
roughly twice as much (0.9–1.5 ms/frame) and costs the measurement. Both are
small beside item 1.

## 4. Incidental finding — loading, not the route

The 90 s load gap (125–210 s) is a separate regime and belongs with the loading
work, but the profiler names its proxy cost precisely: the loading-trace file-I/O
wrapper is the only window where the proxy's leaf table is well covered (61 % of
778 samples), and its hottest rows are `CpuState::capture`/`restore`
(`src/proxy/cpu_state.h:16/20/21`, 279 of the 472 listed samples) and
`loading_trace.cpp:97 tick` (the QPC read, 65 samples).
`ZwQueryPerformanceCounter` is **6.1 %** of the engine thread across the gap —
the largest single ntdll line there (ntdll is 15.3 % in total), ahead of
`ZwReadVirtualMemory`, `ZwCreateFile` and `ZwReadFile` combined. The loading
instrumentation is a measurable fraction of the load it measures.

## Implemented (2026-09-12, items 1–3 of the ranking)

Pure cost fix, no rendering change, no new switch on the fast path; the tree's
`build/d3d9.dll` is `ab9b4a0f19824de372dfed463f28184435b00f8403cdddc443a63a3aeb62dc88` (rebuilt, not installed, nothing committed).

### 1. Validated direct reads replace `ReadProcessMemory` (`src/proxy/engine_memory.{h,cpp}`)

`object_trace::current` and every `object_lifetime` read (`read_registry`,
`lookup`, `verify_ownership`, the baseline snapshot, the patch-site checks)
go through `engine_memory::read`, which validates the span against a
32-entry cache of `VirtualQuery`'d regions (committed, readable, not
`PAGE_GUARD`/`PAGE_NOACCESS`; region = `BaseAddress..+RegionSize`, so one
entry covers a whole heap segment or the image's data section) and then
copies with `rep movsb`. The unit is compiled with `-mno-sse -mno-mmx
-mfpmath=387` (CMake source property, mirrored in the two fixture build
scripts) and its frame epoch is a 32-bit atomic, so the read path executes no
XMM/MMX/x87 instruction: the lifetime observer runs it inside the game's map
mutations, where the wrapper restores the original's FX state only around its
own C++, and the fixture's in-mutation probe compares that state; `objdump` of
`engine_memory.cpp.obj` shows zero `%xmm`/`%mm`/`%st` references. `X3M_ENGINE_READS=rpm` forces the previous
syscall path for A/B; `scene_hook.cpp` (the third `ReadProcessMemory` site,
out of this change's scope) still uses it.

**Invalidation policy.** A cached region is trusted for the rest of the frame
in which it was validated. `MotionOutput::begin_frame` calls
`engine_memory::next_frame()`, after which the first touch of every region
re-queries it; a region is also dropped after 100 ms without a frame advance
(`GetTickCount`, sampled every 64th read so that it is not itself a per-read
Wine dispatch) for callers outside the route (`capture.cpp`'s capture-frame
snapshots when the route is off). Cost: one `NtQueryVirtualMemory` per
distinct region per frame — in gameplay the image's data section, the heap
segment(s) holding nodes and cameras, and the registry's — instead of ~21
`NtReadVirtualMemory` per routed draw. The cache is behind a spinlock because
the lifetime hooks' `finish()` reads on whichever thread mutates the registry.

**Residual risk.** The hazard is a page validated earlier in the frame and
*decommitted* before the copy (a freed block on a page that stays committed
yields stale bytes, which the serial/epoch checks already reject — the
lifetime fixture's decommit case shows `LookupUnavailable` retiring the
identities). The pointers dereferenced on the per-draw path are the node and
camera the engine is submitting on this thread (alive by construction), the
engine object and the bound registry header (long-lived), its bucket array
and small chain links (`0x270`/`0x790`-byte nodes and 12-byte links are below
any heap's decommit threshold), and image globals. The only in-frame free of
one of those is a registry rehash that replaces the bucket array, and a rehash
on another thread is rejected by the observer's `in_flight` guard before the
lookup; on the engine thread it cannot interleave with a draw. The residual
window is therefore the interval between that guard check and the read, on a
cross-thread rehash whose old bucket array was large enough for the heap to
decommit it. Accepted; `X3M_ENGINE_READS=rpm` is the fallback.

### 2. The four engine matrices are read only on capture frames

`object_trace::current(out, matrices)`: the route passes `capture_`, so a
routed draw in an ordinary frame does 4 reads (node block, `camera+0x28`,
engine slot, `engine+0xc`) instead of 12; `capture.cpp`'s capture-frame
snapshot keeps the default and the `world/view/projection` log fields.

### 3. Per-draw telemetry stamps behind `X3M_TELEMETRY_DRAW=1`

With `X3M_TELEMETRY=1` alone the route takes no `QueryPerformanceCounter` per
draw. The metrics `route_gate`, `route_draw`, `route_set_rt`, `route_jitter`,
`route_lazy_flush` and `draw_backend` are not recorded (`telemetry::enabled(Metric)`
drops their samples; `telemetry_start` logs `per_draw=0|1`), and the
`motion_output_frame` totals `gate_us`, `route_draw_us`, `set_rt_us`,
`lazy_flush_us` and `jitter_us` read 0; the counts (`set_rt`, `lazy_flushes`,
`jitter_writes`, gates, routed/matched) and every per-frame metric (`route_fill`,
`taa_*`, `hdr_*`, `route_readback`, present/frame) stay. `X3M_TELEMETRY_DRAW=1`
restores the previous per-draw set. Still stamped per D3D call with telemetry
on: `capture.cpp`'s `CallTimer` (three QPC per hooked call for `capture_cpu`
and the `draw_backend` interval; the sample is dropped but the stamps are
taken) — `capture.cpp` was outside this change; it is the remaining part of
the 1.5–2.1 ms/frame QPC line.

### Fixture evidence (synthetic, Wine; no game)

`run_object_trace.py` and `run_object_lifetime.py` now hash every per-call
record (FNV-1a over all snapshot fields) under both read modes, time 20,000
calls per mode with a frame advance every 256 calls, and exercise a fake
node / bucket array on a `VirtualAlloc`'d page that is decommitted between
frames, recommitted, and (trace) a `0x150`-byte span running into a
reserved-only page. The `TIMING`/`IDENTITY` lines are parsed into the
summaries' `read_path` field and `equal=1` is part of `passed`.

| fixture | records | `rpm` | `direct` | decommit case |
| --- | --- | ---: | ---: | --- |
| object_trace, route path (4 reads) | `5d9c86811e5cd583 / 0e4093189888fb83 (route / capture)` | 3.36 µs/call | 1.31 µs/call | Node bit clear, Camera valid; recommit readable; page-edge span refused |
| object_trace, capture path (12 reads) | identical | 7.75 µs/call | 2.17 µs/call | (same) |
| object_lifetime `current` (12 reads) | `853bfaca11e07f83` | 7.01 µs/call | 0.69 µs/call | `LookupUnavailable`, identities retired, no revival after recommit |

The µs are call costs inside the fixture under Wine (dispatch baseline
0.034 µs subtracted for the trace rows: `route_read_us`/`capture_read_us`
in the summary), 0.0040 `VirtualQuery` per call on the route path. Suites:
`run_object_lifetime.py` PASS 574 checks / 80 backend calls (the earlier
`output x87/SSE/MXCSR matches original` failure is gone with the no-SSE unit),
`run_object_trace.py` PASS 166 / 120017, `run_motion_output.py` PASS (90 cases,
identical hashes), `run_scene_capture.py` PASS 4908 checks,
`check_no_x87.py build/d3d9.dll` PASS (130 reachable functions, no violation;
144 and still clean after a concurrent agent relinked the DLL at 18:2x),
`unittest discover -s verification/analysis` 710 tests OK (bottle `Steam`,
2026-09-12 18:00–18:20).

Not yet measured: the gameplay number. The next user-managed run with
`X3M_TELEMETRY=1` should show the `ZwReadVirtualMemory` line at the observer
baseline and the QPC line near the menu's; `route_gate` needs
`X3M_TELEMETRY_DRAW=1` to be reported at all.
