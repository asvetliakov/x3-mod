# Iteration-09 run 1: sampled loading attribution

**The unexplained ~70% of iteration-08's two loads is now named.** For the first time the
loads are attributed to *functions*, not to counters. The two headline results:

1. **`ID3DXMesh::GenerateAdjacency` is the load.** In this run it cost **69.15 s** of 473.8 s
   of session (8,766 calls), including **33.95 s of the 64.06 s menu load** and **18.77 s of
   the 86.76 s save load**. 93.6 % of the engine thread's samples in the menu load's largest
   stall are inside the call at `0x004bc76a`, with the leaf in a **60-byte x87
   squared-distance-versus-epsilon² scan** at `d3dx9_37.dll+0x19f65a … +0x19f695`. The engine
   passes **Epsilon = 1e-6** (`0x00565600`) while its vertex positions are int16 × 1/16384
   (`0x005655d4` = 6.10e-5), i.e. **the welding epsilon is 61× finer than the position
   quantum**, so the scan cannot merge anything a bitwise comparison would not.
2. **The largest *uninstrumented* engine routine is a per-body collision-tree build**,
   `0x0047eb90` → `0x004e0c80` → `0x004e0f00` → `0x004e1140` → `0x004e1220` (recursive). It is
   the top named leaf in the menu load's 17.65 s opening stall (≥18.9 % of that stall's engine
   samples, ≥3.3 s; 21.5 % / 3.8 s counting the shared `sqrtf` helper) and in the menu *return*
   (≥7.5 %), and no hook sees a microsecond of it.

Provenance: log `/tmp/x3-iteration09-run1/session-20260912-160404-1632.log`, 154,442,312 B,
sha256 `ca149c414df62aabacbfecfb3ab0fdc586d182c8a23aec918f14293a169e0d47`, produced by the
user's run of build `1d36c29` with `--direct --ownership --object-trace --object-lifetime
--motion-output --taa --telemetry --profile` (start → main menu → load save → flight). No game
was launched for this analysis. Derived report:
`verification/results/loading-profile-run1/{loading-profile.md,loading-profile.json,rvas.txt,symbols.json}`
(`python3 tools/analysis/analyze_loading_profile.py <log> --output … --ghidra`; 579 RVAs
collected, 269 resolved by `X3ProfileSymbols.java` against `/tmp/x3-ghidra-research`).
Decompiler output stays in `/tmp/x3-run1-profile/` and is not committed. QPC 10 MHz, anchor
`proxy_initialize`, 19 import/vtable hooks, 332 loading report windows.

## 1. Timeline

Seconds after proxy initialization. One device, no `Reset`.

| Anchor | run 1 |
| --- | ---: |
| Loading-hook coverage begins | 0.0011 s |
| First successful `Present` (frame 0) | 4.791 s |
| First profiler delta report / last | 5.004 s / 470.574 s |
| Last observed `Present` | 473.825 s |

| # | Label | Interval | Length | Hooked excl. | Unexplained | Engine samples |
| ---: | --- | --- | ---: | ---: | ---: | ---: |
| 1 | unlabelled (pre-menu init) | 4.791–9.415 s | 2.965 s | 0.579 s | 2.386 s (80 %) | 3,011 |
| 2 | **main-menu load** | 8.507–73.716 s | **64.063 s** | 39.813 s | 24.250 s (38 %) | 18,234 |
| 3 | **save-game load** | 124.197–211.015 s | **86.758 s** | 41.239 s | 45.519 s (52 %) | 21,947 |
| 4 | sector change | 383.924–409.065 s | 24.126 s | 14.710 s | 9.416 s (39 %) | 4,503 |
| 5 | **main-menu load** (return) | 437.990–471.572 s | 32.826 s | 8.912 s | 23.914 s (73 %) | 4,193 |

Gap labels are the pipeline's mechanical rules; gaps 2 and 5 both match the 1,017-call /
~299 MB main-menu work vector of iteration-08, gap 3 holds the run's only successful `gzopen`
and 13,970,460 `gzread` calls.

**Reading "engine samples".** The sampler saw up to 30 threads, but only **one** ever executes
engine code: slot 0, tid 1260. Every other sampled thread sits in `ntdll`/`win32u`/`mmdevapi`
waits for the whole run (e.g. in gap 2, slots 1–5 each contribute exactly 18,234 samples, all
of them waits, and 173,913 of the gap's 192,149 samples have no main-module frame at all). All
shares below are therefore **of slot 0**, and seconds are `share × gap length`. The pipeline
was changed to normalize this way (§7).

## 2. Main-menu load — 64.063 s

Engine thread: 18,234 samples over 64.06 s = **3.51 ms per sample**.

**Module split of the engine thread** (leaf module of each sample):

| x3ap | d3dx | wine | ntdll | zlib | proxy | xml | other |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 6,372 (34.9 %) | 9,682 (53.1 %) | 1,001 (5.5 %) | 605 (3.3 %) | 480 (2.6 %) | 84 (0.5 %) | 4 | 6 |

**d3dx is the majority of the menu load**, and it is CPU (only 3.3 % of the thread is in an
`ntdll` syscall thunk). The `wine` share is almost entirely `mmdevapi.dll` (856 samples).

### Structure: four report stalls

| Stall | Length | Hooked excl. | Engine samples | Engine leaf split | Dominant attribution |
| --- | ---: | ---: | ---: | --- | --- |
| 10.415–28.060 s | 17.649 s | 0.316 s | 5,129 | **x3ap 87.7 %**, d3dx 8.0 %, ntdll 1.4 % | collision-tree build (≥18.9 %), adjacency 4.5 % |
| 30.067–32.351 s | 2.284 s | 0.541 s | 1,294 | d3dx 91.6 %, x3ap 2.4 % | adjacency call site **59.1 %**; `___lock_fhandle` 31.3 % |
| 32.351–36.406 s | 4.055 s | 5.589 s | 2,599 | d3dx 93.4 %, x3ap 2.9 % | adjacency call site **76.5 %** (1 call, 3.849 s) |
| 36.406–63.482 s | 27.076 s | 26.862 s | 7,819 | **d3dx 94.1 %**, x3ap 5.3 % | adjacency call site **93.6 %** (4 calls, 26.761 s; max **26.601 s**) |

The last two stalls, 31.1 s together, are **five `GenerateAdjacency` calls**. They are hooked,
which is why this gap's unexplained share fell to 38 % while the gap itself doubled.

### Top 15 functions

Self = leaf samples in the function; Incl. = samples whose first main-executable frame is a
return address in the function (so DLL and wait time lands on the engine caller). Both are
**lower bounds**: per-block tables carry only the top 48 rows.

| Function | Ghidra | Self | Incl. (frame) | Role / evidence |
| --- | --- | ---: | ---: | --- |
| `0x004bc76a` call site in `0x004bc680` | — | — | **8,817 (48.4 %, 31.0 s)** | **`ID3DXMesh::GenerateAdjacency(1e-6f, adj)`.** `CALL EDX` where `EDX = [mesh_vtable+0x58]` (slot 22 = `GenerateAdjacency`), epsilon `FLD [0x00565600]` = 1e-6. Hooked: 1,017 calls / 33.945 s. See the note on `__VEC_memzero` below |
| `0x0052403b` | `__VEC_memzero` | 0 | 8,358 (45.8 %, 29.4 s) | CRT memset; **stale return address**, not a real frame — it stands for the row above |
| `0x004de060` | `FUN_004de060` | 0 | 857 (4.70 %, 3.01 s) | **Audio/COM subsystem init**: `CoInitialize`, `Ordinal_11`, from process init `0x004033e1`; leaf is `mmdevapi.dll+0x34bb` (856 samples). Wine-side device enumeration |
| `0x004e1220` | `FUN_004e1220` | **551 (3.02 %, 1.94 s)** | 647 (3.55 %) | **Collision tree: recursive split body** (2,267 B, self-recursive at `0x004e1533`/`0x004e1a13`) |
| `0x004bc680` | `FUN_004bc680` | 0 | 503 (2.76 %) | Mesh preparation (adjacency/clean/optimise); the non-stale half of the call-site row |
| `0x00511f77` | `FUN_00511f77` | 0 | 425 (2.33 %, 1.49 s) | CRT `fread` body (pair: ← `_fread_s@0x1121d1` 346, ← resource read `0x004e8880@0xe8da9` 78) |
| `0x00524aa7` | `___lock_fhandle` | 0 | 418 (2.29 %, 1.47 s) | CRT file-handle lock; 405 of these are one 2.28 s stall (31.3 % of it) |
| `0x004e0770` | `FUN_004e0770` | **326 (1.79 %, 1.15 s)** | 390 (2.14 %) | **Collision tree: 13-float node summary accumulate** (`n[1..3] += p[1..3]*p[0]`, `n[4..12] += p[4..12]`) |
| `0x004dfef0` | `FUN_004dfef0` | **189 (1.04 %, 0.66 s)** | 233 (1.28 %) | **Collision tree: split-candidate extents** (2,162 B; calls `sqrtf 0x00412440` ×3, `fabsf 0x0040e710` ×3) |
| `0x004dd2c0` | `FUN_004dd2c0` | 0 | 196 (1.07 %, 0.69 s) | **jpg/tga texture load body**: `"jpg"` string, `0x004e8e10` resource load, `D3DXGetImageInfoFromFileInMemory`, `D3DXCreateTextureFromFileInMemoryEx`, `D3DXLoadSurfaceFromFileInMemory`; callee of `0x004f3510` |
| `0x0050e1b0` | `_free` | 0 | 175 (0.96 %) | CRT free; 46 samples from `0x004bc680@0xbc90a` (adjacency buffer release) |
| `0x00412440` | `FUN_00412440` | **172 (0.94 %, 0.60 s)** | 172 (0.94 %) | **`sqrtf` helper, 15 bytes** (`SQRT`); reached from `0x004dfef0` at three sites |
| `0x004bcb60` | `FUN_004bcb60` | 0 | 137 (0.75 %) | **Mesh build: `CloneMesh` into the final options** (`GetNumVertices` > 0xFFFF → `D3DXMESH_32BIT`; two attempts; OOM callback on `0x8007000E`/`0x8876017C`), then install via `0x004bc9c0`. Pair: ← `0x004bb470@0xbb9ac` |
| `0x004bc1c0` | `FUN_004bc1c0` | 11 | 94 (0.52 %) | **Mesh build: vertex positions (int16 × `0x005655d4` = 1/16384) and normals** (`D3DXVec3Normalize` ×3); ← `0x004bb470@0xbb94b` |
| `0x0051f464` | `FUN_0051f464` | 0 | 91 (0.50 %) | CRT (self-recursive) |
| `0x004dc540` | `FUN_004dc540` | 0 | 82 (0.45 %) | Texture loader (pck/dds); hooked 461 + 9 helper calls / 2.706 s |

**Why `__VEC_memzero` appears as the hottest frame.** `0x004bc680` allocates two
`3 × faces × 4`-byte adjacency arrays (`malloc` at `0x004bc6be`/`0x004bc711`) and memsets each
(`0x00518de0` at `0x004bc6ea`/`0x004bc73e`), then calls `GenerateAdjacency` at `0x004bc76a`.
D3DX's adjacency code omits frame pointers, so the stack scan's first main-module hit is the
**leftover** `0x00524071` (the return address after `call fastzero_I` inside `__VEC_memzero`)
from that memset, with the genuine `0x004bc76c` right behind it. This is exactly the stale
return address documented in `docs/verification/sampling-profiler.md` "Limits". The two rows
must be read as one: the engine is inside `GenerateAdjacency`, called from `0x004bc76a`.

### The five pathological adjacency calls

| Report window | Calls | Inclusive | Max single call |
| ---: | ---: | ---: | ---: |
| 36.41 s | 1 | 3.849 s | 3.849 s |
| 63.48 s | 4 | 26.761 s | **26.601 s** |

During the 26.6 s call the engine thread was 94 % in `d3dx9_37.dll` user code with 15 samples
in `ntdll` — **CPU-bound, not waiting**. 6,988 of 7,359 d3dx samples (95 %) fall in the
60-byte span `+0x19f65a…+0x19f695`, whose disassembly is:

```
movsl ×3                      ; load candidate vertex (3 floats)
fsubs (%ebx) / 0x4 / 0x8      ; delta = candidate − reference
dx*dx + dy*dy + dz*dz         ; x87 squared length
flds -0x4c(%ebp); fcompp      ; compare against Epsilon²
jne  → next candidate
```

driven by an index at `-0x10(%ebp)` over a bucket list at `(-0xc(%ebp))[eax]` — the
point-welding candidate scan of `GenerateAdjacency(Epsilon, …)`. A degenerate bucket
distribution makes it O(n²), which is what a 26.6 s single call on one mesh looks like. The
same 1,017-call vector in this session's menu *return* (gap 5) cost 3.587 s and iteration-08
measured 3.16/3.22 s, so this is **per-mesh data-dependent**, not a per-call regression: over
the whole run, the 6 calls above 1 s cost 40.24 s and the remaining 8,760 calls averaged
**3.30 ms** (iteration-08: 3.13/3.20 ms).

## 3. Save-game load — 86.758 s

Engine thread: 21,947 samples over 86.76 s = **3.95 ms per sample**.

| x3ap | d3dx | ntdll | zlib | proxy | wine | xml | other |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 8,951 (40.8 %) | 7,207 (32.8 %) | 3,385 (15.4 %) | 1,153 (5.3 %) | 810 (3.7 %) | 323 (1.5 %) | 16 (0.1 %) | 77 (0.4 %) |

**15.4 % (≈13.4 s) of the save load is the engine thread parked in an `ntdll` syscall thunk** —
Wine/wineserver file-API latency, which the menu load barely has (3.3 %). **3.7 % (≈3.2 s) is
our own proxy** (the hook wrappers; `gzread`'s measured wrapper tail alone is 2.136 s).

### Structure: four report stalls

| Stall | Length | Hooked excl. | Engine samples | Engine leaf split | Dominant attribution |
| --- | ---: | ---: | ---: | --- | --- |
| S1 123.733–160.359 s (savegame decode) | 36.626 s | 5.024 s | 11,377 | **x3ap 61.6 %, ntdll 16.3 %**, d3dx 9.2 %, zlib 5.6 %, proxy 5.6 % | read dispatcher `0x004e9210` ≥16.7 % (≥6.1 s) |
| 164.569–168.231 s | 3.662 s | 3.489 s | 2,603 | d3dx 79.9 %, x3ap 10.4 % | adjacency call site **51.3 %** (30 calls, max 3.259 s) |
| S3 193.177–205.889 s (script/XML) | 12.712 s | 6.232 s | 3,227 | d3dx 39.2 %, **ntdll 34.2 %**, x3ap 13.4 % | `CreateFileA` 1,376 / 4.044 s; `FindFirstFileA` 1,398 / 0.933 s; `0x004dd2c0` 21.6 % |
| S4 205.889–209.416 s (texture burst) | 3.527 s | 0.489 s | 939 | d3dx 74.5 %, ntdll 10.5 %, x3ap 7.0 % | **`0x004dd2c0` 71.7 %** (jpg/tga load body) with only 0.073 s hooked |

### Top 15 functions

| Function | Ghidra | Self | Incl. (frame) | Role / evidence |
| --- | --- | ---: | ---: | --- |
| `0x004bc76a` call site in `0x004bc680` | — | — | **4,872 (22.2 %, 19.3 s)** | `GenerateAdjacency(1e-6f)`; hooked 3,585 calls / 18.771 s (agrees to 0.5 s) |
| `0x0052403b` | `__VEC_memzero` | 0 | 3,581 (16.3 %) | memset; 3,194 stale-attributed to the row above, 358 to `0x004e9210` |
| `0x004e9210` | `FUN_004e9210` | 0 | **1,532 (7.0 %)**; with its memset/malloc pairs **2,096 (9.6 %, 8.3 s)** | **Read dispatcher** (`fread`/`gzread`/XOR-0x33 slice). Hooked inside it: `gzread` 13,970,460 calls / 2.845 s (**3.12 B per call**), `ReadFile` 103,675 / 0.698 s. Self-recursive frame 1,344 in S1 alone |
| `0x004dd2c0` | `FUN_004dd2c0` | 0 | **1,451 (6.6 %, 5.74 s)** | jpg/tga texture load body (see §2); 697 samples in S3, 673 in S4 |
| `0x00511f77` | `FUN_00511f77` | 0 | 770 (3.5 %, 3.04 s) | CRT `fread` body; 596 ← `_fread_s`, 171 ← resource read `0x004e8880` |
| `0x004bc1c0` | `FUN_004bc1c0` | **301 (1.37 %)** | 724 (3.30 %, 2.86 s) | Mesh build: vertex positions + `D3DXVec3Normalize` |
| `0x004e1220` | `FUN_004e1220` | **610 (2.78 %, 2.41 s)** | 653 (2.98 %) | Collision tree: recursive split body |
| `0x005112c4` | `_malloc` | 0 | 526 (2.40 %) | 206 ← `0x004e9210`, 121 ← `0x004a0880`; per-read allocation in the gzread loop |
| `0x0050e1b0` | `_free` | 0 | 511 (2.33 %) | 242 ← `0x004bc680@0xbc90a` (adjacency buffers) |
| `0x004bcb60` | `FUN_004bcb60` | 0 | 509 (2.32 %) | Mesh `CloneMesh` + install |
| `0x004e0770` | `FUN_004e0770` | **412 (1.88 %, 1.63 s)** | 416 (1.90 %) | Collision tree: node summary accumulate |
| `0x004dfef0` | `FUN_004dfef0` | **308 (1.40 %, 1.22 s)** | 342 (1.56 %) | Collision tree: split-candidate extents |
| `0x0051da61` | `__VEC_memcpy` | 0 | 333 (1.52 %) | all 333 ← `0x00511f77` (CRT `fread` copy-out) |
| `0x00412440` | `FUN_00412440` | **214 (0.98 %)** | 214 (0.98 %) | `sqrtf` helper |
| `0x00517d8f` | `FUN_00517d8f` | 0 | 187 (0.85 %) | CRT; 177 ← `__sopen_helper`-adjacent `0x00524cfa` (file open path) |
| `0x004cabc0` | `FUN_004cabc0` | 0 | 179 (0.82 %) | unidentified |

Mapping to `loading-orchestration.md`: the save load's own orchestration functions —
`0x00404cc0` save open/validate, `0x0043f420` universe restore, `0x00434e40` type pass,
`0x004ab880` script VM — are **cheap in self time** (`0x004ab880` 28 samples = 0.13 %; the
others below the table cut). The time is in their callees: the read dispatcher `0x004e9210`
under the savegame decode, `0x004bc680`/`0x004bb470` under the mesh path, `0x004dd2c0` under
the texture path, and the Wine file syscalls under `0x004e8780`/`0x004e7590`.

## 4. The other three gaps

* **Gap 1, unlabelled pre-menu 2.965 s** (iteration-08's unexplained 2.213 s): engine thread
  3,011 samples, of which **915 (30.4 %, ≈0.90 s) are `FUN_004de060` with an `mmdevapi.dll`
  leaf** — Wine audio-device initialization — plus 297 in `FUN_0042fe10` ← `FUN_004b3860` ←
  process init `0x00402741`. This gap is startup, not game work.
* **Gap 4, sector change 24.126 s**: engine thread 4,503; d3dx 49.0 %, x3ap 39.4 %.
  `GenerateAdjacency` 3,022 calls / 12.815 s (max 4.076 s) = 53 % of the gap; the mesh-build
  vertex/normal routine `0x004bc1c0` is the top self (1.87 %), and the collision-tree cluster
  follows (`0x004e1220` 1.13 %, `sqrtf` 1.15 %).
* **Gap 5, menu return 32.826 s**: engine thread 4,193; **x3ap 67.1 %**, d3dx 18.4 %. Only
  8.912 s is hooked (adjacency 3.587 s on the same 1,017 calls). The **collision-tree cluster
  is the top self group** — `0x004e1220` 4.79 %, `sqrtf` 1.79 %, `0x004e0770` 1.53 %,
  `0x004dfef0` 1.14 % → ≥7.5 % without the shared `sqrtf` (≥2.5 s), 9.3 % with it — confirming that a mid-session return to the menu
  rebuilds every collision tree from scratch, exactly as it rebuilds every mesh.

## 5. The collision-tree build, in full

Decompiled 2026-09-12 (output in `/tmp/x3-run1-profile/`, untracked):

* `0x0047eb90` (849 B) — driver. Resolves the body for scene object `obj+0x140` through the
  body cache `0x004863c0`; returns early if `obj+0x12c & 0x8000000`; if `body+0x5c` (the tree)
  is null it creates one (`0x004e2970`), walks parts → sub-lists → triangles (stride 0x10,
  vertex indices at `+0x4`/`+0x8`, vertex stride 0x18), converts each int16 coordinate with
  `_DAT_005655d4` = 1/16384, calls `0x004e0ce0(tree, v0, v1, v2, index)` per triangle, then
  `0x004e0c80()`; finally sets `obj+0x12c |= 0x1000000`.
* `0x004e0c80` (82 B) → `0x004e0f00` (697 B) — allocates `2N × 0x48` nodes and `N × 0x34`
  records for N triangles, memsets both, seeds the root bounds to `0x7fc00000` (NaN), loops N
  times through `0x004e0770`, then `0x004e07f0`, `0x004e0e70` and the recursive split.
* `0x004e1140` → `0x004e1220` (2,267 B, recurses into itself) — the split; calls `0x004e0770`,
  `0x004e07f0`, `0x004e0e70`, `0x004e1b00`, `0x004e11c0`, `0x004dfd80`.
* `0x004e0e70` (141 B) — calls `0x004dfef0`, then orders three vec3 triples by the three floats
  it returned (a 3-way sort of split candidates).
* `0x004dfef0` (2,162 B) — the candidate metric: `fabsf` (`0x0040e710`) ×3 and `sqrtf`
  (`0x00412440`) ×3.
* `0x004e0770` (—) — accumulates a 13-float node summary: `n[1..3] += p[1..3] * p[0]`,
  `n[4..12] += p[4..12]`, `n[0] += p[0]` (area-weighted centroid plus nine sums).

Callers of `0x004eb90`'s entry chain include `0x00479d10` (node deserialization, load path),
`0x0043ffa0` (reached from universe restore `0x0043f936`), `0x00487e30` (38 call sites) and the
object-construction paths at `0x00426e10`/`0x004273d0` — i.e. it runs once per scene object
whose body has no tree yet. Because the tree hangs off the **body** (`body+0x5c`), the same
cache-emptying that forces the 1,017 repeated `GenerateAdjacency` calls also forces the trees
to be rebuilt; `loading-orchestration.md`'s open question ("which teardown frees the body
hash") now has a second cost attached to its answer.

## 6. Sampler cost and effective rate

Whole run, summed over the 94 `scope=delta` blocks:

| Quantity | Value |
| --- | ---: |
| Covered wall time | 470.4 s |
| Ticks / samples | 90,546 / 1,438,347 (15.89 threads per tick) |
| Achieved tick rate / interval | **192.5 ticks/s / 5.20 ms** (nominal 2.00 ms) |
| Per-tick cost, mean / max | **1,959 µs / 85.1 ms** |
| Sampler busy | **177.4 s = 37.7 % of wall** (one host core) |
| Module/thread refresh | 15.70 s (all threads running) |
| Reports | 0.139 s |
| Dropped samples / threads seen / unsampled | 0 / ≤30 / 1 |
| Per suspended thread | ~123 µs → **each game thread suspended 2.37 % of wall** |

Per-gap busy share: 37.4 % (menu), 39.6 % (save), 45.4 % (sector), 43.6 % (menu return); the
tick mean grows from 1.31 ms to 3.39 ms as the thread count rises, so the engine thread's
sample resolution degrades from 3.51 ms to 7.83 ms across the session. **The sampler resolves
the engine thread at 3.5–7.8 ms, which is coarser than the 2 ms nominal and is why per-function
shares are lower bounds.**

Perturbation of the game (all measured, not modelled):

* Thread suspension: 2.37 % of wall = **1.52 s of the menu load, 2.06 s of the save load**.
* Loading-hook wrapper tails: **2.37 s** over the run, of which `gzread` is 2.136 s.
* Proxy code as a sampled leaf on the engine thread: 0.5 % (menu), **3.7 % ≈ 3.2 s** (save).

## 7. Pipeline fixes made

1. **`summarize_profile.py` crashed on real data** (`KeyError: 'size'`): a module that cannot
   be pinned is logged as `profile_module index=69 name=winevulkan.dll base=0x77e60000 pinned=0
   error=126` with no `kind`/`size`/`text_rva`, exactly as the schema documents, and the parser
   required `size`. It now records such a module with `pinned=False` and null ranges (and
   records `pinned` for every module). `main_module_names` no longer assumes `name` is present.
   Regression test: `test_unpinnable_module_line_has_no_size`.
2. **Function shares were divided by all threads' samples**, so the real hot spots read as
   "0.29 % of all samples" when ~15 of the 16 sampled threads are idle waiters. The table now
   carries `engine_slot`/`engine_samples` (the slot holding the main-module samples) and
   `self_share_engine`/`inclusive_share_engine`, and the markdown reports those. Regression
   test: `test_function_shares_are_normalized_to_the_engine_thread`.

`verification/analysis/test_loading_profile.py` (12 tests),
`test_profile_summary.py` (9) and `test_iteration08_loading.py` (10) are green.

**Recommended profiler change (not made here).** The per-block top-48 leaf budget is consumed
by the idle threads, so in the menu load's 17.6 s stall the retained rows cover only 16 % of
the engine thread's samples. A per-slot quota (or skipping a thread whose leaf has not moved
out of a wait) would raise engine coverage several-fold at the same log volume.

## 8. Comparison with iteration-08

| Quantity | it-08 A | it-08 B | run 1 |
| --- | ---: | ---: | ---: |
| Menu load (startup) | 30.110 s | 29.786 s | **64.063 s** |
| … hooked exclusive | 8.433 s | 8.395 s | 39.813 s |
| … unexplained | 21.677 s (72 %) | 21.391 s (72 %) | **24.250 s (38 %)** |
| Menu return | 30.115 s (it-06) | 31.099 s (it-07) | 32.826 s |
| Save load | 106.681 s | 101.892 s | **86.758 s** |
| … hooked exclusive | 33.268 s | 28.064 s | 41.239 s |
| … unexplained | 73.413 s (69 %) | 73.828 s (72 %) | **45.519 s (52 %)** |
| Save S1 decode stall / hooked | 32.68 / 4.59 s | 33.59 / 4.71 s | 36.63 / 5.02 s |
| Save S3 script-XML stall / hooked | 45.01 / 6.95 s | 38.95 / 1.26 s | **12.71 / 6.23 s** |
| Save S4 texture stall / hooked | 3.14 / 0.44 s | 3.33 / 0.46 s | 3.53 / 0.49 s |
| `GenerateAdjacency`, whole run | 4,627 / 14.492 s / 3.13 ms | 4,621 / 14.694 s / 3.18 ms | 8,766 / **69.154 s** / 7.89 ms |
| … excluding calls over 1 s | — | — | 8,760 / 28.9 s / **3.30 ms** |
| `CreateFileA`, whole run | 3,617 / 6.855 s | 3,616 / 1.073 s | 4,291 / 4.814 s |
| `inflate`, whole run | 551,846 / 5.728 s | 551,846 / 5.749 s | 780,093 / 8.988 s (11.5 µs) |
| `gzread`, whole run | 13,970,478 / 2.581 s | 13,970,478 / 2.632 s | 13,970,478 / 2.845 s |

**Did the profiler lengthen the loads? By roughly 2–3 s per load, not more.**

* The only clean like-for-like comparison is the mid-session menu return on the identical
  1,017-call work vector: **32.826 s here versus 30.115 / 31.099 s** in iterations 06/07 =
  **+1.7 to +2.7 s (+6–9 %)**. That is consistent with the measured 2.37 % suspend share
  (≈0.8 s) plus 3 more hooks, the wrapper tails and this build's HDR/TAA work.
* Per-call hooked means are within ~10 % of iteration-08 (`GenerateAdjacency` 3.30 vs 3.13 ms
  excluding the outliers; `OptimizeInplace` 0.223 vs 0.234 ms; `inflate` 11.5 vs 10.4 µs;
  `gzread` 0.204 vs 0.185 µs), so sampling did not inflate the instrumented work.
* The startup menu load's **+34 s is not instrumentation**: 30.6 s of it is five
  `GenerateAdjacency` calls that the same session's own menu return (3.587 s for the same 1,017
  calls) and both iteration-08 runs (3.16/3.22 s) did not incur. The profiler *observed* that
  d3dx loop; it did not create it.
* The save load ran **15.1 s shorter** than iteration-08 run B, entirely because the S3
  script/XML stall fell from 38.95/45.01 s to 12.71 s. This is **not explained** here: the
  hooked S3 content is the same shape (1,376 opens vs 1,368; 790 `xmlReadMemory`), and
  iteration-08 has no profile to compare against. Treat it as a confound when comparing save
  loads across these sessions, and re-measure S3 with the profiler before drawing a
  conclusion.

**Did the new hooks explain more of the gaps?** Yes, but modestly, and not via the find-file
family. `FindFirstFileA` + `FindNextFileA` + `FindClose` account for **1.181 s across the whole
run** (4,625 + 2,202 + 756 calls), of which the save load's S3 stall holds 0.933 s (7.3 % of
that stall) and the whole save load 1.080 s. `FindFirstFileA` **fails 3,857 of 4,625 times
(83 %)** at 0.232 ms per call; in S3 the surviving 1,398 calls cost 0.668 ms each while the
engine thread was 34 % in `ntdll`, so this is Wine/wineserver directory-lookup latency. The
share of the two big gaps that the hook set explains moved from 28 %/31 % (iteration-08) to
**62 %/48 %** — almost all of that from `GenerateAdjacency`'s outliers and `CreateFileA`, not
from the find-file hooks.

## 9. Ranked optimization candidates, with the seconds measured in this run

Bounds assume the named work goes to zero. They are **opportunity ceilings, not forecasts**,
they **overlap each other** (1 and 2 especially) and must not be summed. "Engine CPU" versus
"Wine-side" is taken from the engine thread's leaf module.

| # | Candidate | Measured bound in run 1 | Mechanism | Kind |
| ---: | --- | ---: | --- | --- |
| 1 | **Stop D3DX epsilon point-welding in `GenerateAdjacency`** | **69.15 s** whole run; **33.95 s** menu load, **18.77 s** save load, **12.81 s** sector change, 3.59 s menu return | (a) patch the constant at `0x00565600` from 1e-6 to 0.0 — D3DX then takes the exact-match path; or (b) compute adjacency ourselves in the proxy's existing `ID3DXMesh::GenerateAdjacency` vtable hook with a hash over the exact positions, O(n) | engine patch / algorithm replacement | 
| 2 | **Cache the whole mesh-preparation result** (`X3M_MESH_CACHE`, still inactive) | **72.26 s** whole run (adjacency 69.15 + `OptimizeInplace` 1.96 + `CleanMesh` 0.97 + `CreateMesh` 0.18); **34.30 s** menu, **20.26 s** save, **13.73 s** sector | content-keyed cache of the prepared mesh; supersedes 1 where it hits | cache |
| 3 | **Cache the per-body collision tree** (`body+0x5c`) | ≥**3.3 s** of the menu load's 17.6 s stall (3.8 s with the shared `sqrtf` helper), ≥**5.3 s** of the save load, ≥**2.5 s** of the menu return, ≥**0.5 s** of the sector change (all lower bounds; `0x004e1220`+`0x004e0770`+`0x004dfef0` self samples only) | keep the tree alive across the teardown that empties the body hash, or memoize `0x004e0c80` per body id | cache (engine) |
| 4 | **Buffer the savegame read loop** (`0x004e9210`, 3.12 B per `gzread`) | ≥**8.3 s** of the save load (9.6 % of the engine thread: dispatcher + its memset + its malloc), of which 2.85 s is the hooked `gzread` itself and **2.14 s is our own hook tail** | read-ahead in the dispatcher (engine patch at `0x004e9210`) or a buffering `gzread` hook; removing our tail is free | algorithm / engine patch |
| 5 | **Converted-texture cache for the jpg/tga path** (`0x004dd2c0`) | **5.74 s** of the save load by frame attribution (71.7 % of the 3.53 s S4 stall ≈ 2.5 s; 21.6 % of S3 ≈ 2.7 s), plus hooked `D3DXCreateTextureFromFileInMemoryEx` **12.66 s** whole run / 7.24 s save / 2.69 s menu, worst single call 3.48 s | content-keyed cache of the converted surface; 1,312,542,504 B of helper input per run | cache |
| 6 | **Cut the failed name probes** (`0x004e7590` resolver) | **Wine-side: 7.50 s** whole run of file APIs (`CreateFileA` 4.81 + `ReadFile` 1.50 + `FindFirstFileA` 1.08 + `FindNextFileA` 0.11), of which ≈**4.3 s** lands in the save load's S3 stall (34.2 % of it is `ntdll`); 3,857 of 4,625 `FindFirstFileA` calls fail | memoize negative resolution results in the proxy hooks; per-call latency is wineserver round trips, so the win is call-count, not code speed | hook (Wine-side) |
| 7 | **`inflate` chunking** | **8.99 s** whole run (780,093 calls at 11.5 µs); 4.51 s save, 1.85 s menu, 0.76 s sector | widen the 1 KiB input loop (needs the stack buffer enlarged too) | engine patch |
| 8 | Audio/COM device init (`0x004de060` → `mmdevapi`) | **Wine-side: ≈3.9 s** (0.90 s of the pre-menu gap + 3.01 s of the menu load) | none in scope — CrossOver audio backend cost; record, do not chase | (host) |
| 9 | CRT file-handle locking (`___lock_fhandle`) | 1.47 s of the menu load, 31.3 % of one 2.28 s stall | consequence of the per-file open/read pattern; falls out of 6 | — |
| 10 | Our own instrumentation | 2.37 s wrapper tails + 2.37 % suspend (1.52 s menu / 2.06 s save) + 3.2 s proxy leaf in the save load | drop `--profile` and the `gzread` hook for timing runs | (measurement) |

**Wine-side versus engine CPU, per gap** (engine-thread leaf module):

| Gap | Engine CPU (x3ap + d3dx + zlib) | Wine/ntdll waits | Our proxy |
| --- | ---: | ---: | ---: |
| Menu load 64.06 s | 90.7 % (58.1 s) | 8.8 % (5.7 s, mostly `mmdevapi` init) | 0.5 % (0.3 s) |
| Save load 86.76 s | 78.9 % (68.4 s) | 16.9 % (14.7 s) | 3.7 % (3.2 s) |
| Sector change 24.13 s | 90.8 % (21.9 s) | 7.8 % (1.9 s) | 1.3 % (0.3 s) |
| Menu return 32.83 s | 90.0 % (29.5 s) | 9.0 % (3.0 s) | 0.8 % (0.3 s) |

**These loads are CPU-bound engine and d3dx work, not I/O.** Only the save load has a material
Wine-side component (14.7 s), and it concentrates in the script/XML phase's file opens and
directory probes.

## 10. Limits

* Per-block leaf/frame/pair tables carry the top 48/48/32 rows, and ~15 idle threads compete
  for that budget, so every function share here is a **lower bound**; per-thread sample totals
  and module splits are exact. In the menu load's 17.6 s stall the retained rows cover 16 % of
  the engine thread's samples, so its named total (18.9 %) understates the real concentration.
* The engine thread is sampled every 3.5–7.8 ms, not 2 ms.
* Frame and pair rows are return addresses recovered from an EBP chain plus a stack scan;
  stale return addresses above `Esp` can be mis-read as live, which is exactly what the
  `__VEC_memzero` rows are. Pairs are evidence to confirm against a call graph, never call
  counts.
* Hooked seconds are completion deltas of report windows overlapping the interval; only the
  exclusive column may be added and it is a lower bound. Two report windows straddle every gap
  boundary, and one stall's hooked total (5.589 s in 4.055 s) exceeds its own length for that
  reason.
* Nothing here implements or measures a loading improvement. Every bound above needs a
  controlled before/after run before any speedup is claimed. `Epsilon = 0` in particular
  changes `GenerateAdjacency`'s output whenever two vertices are closer than 1e-6 without
  being equal; the int16 × 1/16384 quantization argument makes that unlikely for these bodies
  but does not prove it for meshes whose positions pass through a transform before the fill in
  `0x004bbb10`/`0x004bc1c0`. Verify adjacency equality on a mesh sample before shipping it.
