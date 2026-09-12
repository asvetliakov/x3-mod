# Diagnostics 0.4: iteration-08 loading characterization

**The two long loads are reproducible and mostly unexplained by the current hooks.** In both
iteration-08 sessions the main-menu load is a **30.1 s / 29.8 s** presentation gap of which
**8.43 s / 8.40 s** is instrumented, and the save-game load is a **106.7 s / 101.9 s** gap of
which **33.3 s / 28.1 s** is instrumented. The unattributed remainder is **21.7 s / 21.4 s**
for the menu and **73.4 s / 73.8 s** for the save load — the two runs agree on the
unexplained save-load amount to within 0.4 s while differing by 5.2 s in instrumented time.
That stable ~73 s is the direct case for a sampling profiler: no installed hook sees it.

Four separate sessions reload the **identical** main-menu work vector (1,017
`GenerateAdjacency` calls, ~299 MB of 2D texture-helper input, 166,021 `inflate` calls,
43,420 reads of 184,902,281 bytes), including twice inside one session when the player
returned to the menu. Nothing about that scene is cached between loads.

## Provenance

No game was launched for this analysis; the logs were supplied by the completed user runs.
Raw logs stay local and untracked. Derived totals are in
[iteration-08-loading-summary.json](../../verification/results/iteration-08-loading-summary.json),
reproduced by `tools/analysis/analyze_iteration08_loading.py` (ten synthetic unit tests in
`verification/analysis/test_iteration08_loading.py`, `python3 -m unittest` clean).

| Label | File | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| `taa_on` (run A) | `session-20260912-074543-296.log` | 262,691,737 | `3f862cdf98997e1c52f67403ff4264d7a447d5d972b019d46f07f30b508b5565` |
| `taa_off` (run B) | `session-20260912-075643-2624.log` | 124,245,193 | `ab2975e077509602315eb01a9cd534b5c134228bf6fa38bf668e501ffee968d4` |
| `iteration06` | `x3-iteration06-snapshot.log` | 383,673,249 | `4ea7d9119182e65198d96e6688bf34aa575f1fe254d9eecdf2cfbc9efb7affbd` |
| `iteration07` | `x3-iteration07-snapshot.log` | 90,351,004 | `dfcb6610677209e663e3933a324b978a3f25f3689be1a9a539cead499c7f3689` |

All four runs installed **16/16 main-module imports** and **two shared native mesh vtables**
(three slots each). QPC frequency is 10 MHz; coverage began 1.115 ms (A) / 1.110 ms (B) after
proxy initialization, which itself follows uninstrumented process startup.
`mesh_cache requested=0 enabled=0 activation=await_public_mesh_and_buffer_contract` in both
iteration-08 runs: **the adjacency cache was not active for any number below.**

## 1. Startup timeline and presentation gaps

Seconds are relative to proxy initialization. Both runs created one device, `1280×768`
windowed, present interval 1, no `Reset`.

| Anchor | run A (TAA on) | run B (TAA off) |
| --- | ---: | ---: |
| `backend_load` begin / duration | 0.0012 s / 13.68 ms | 0.0012 s / 3.8 ms |
| `Direct3DCreate9` #1 / #2 / #3 | 0.140 s (751.3 ms), 2.779 s (96.5 ms), 2.938 s (100.5 ms) | 0.089 s (517.3 ms), 1.878 s (101.0 ms), 2.007 s (92.8 ms) |
| `CreateDevice` | 22.207 ms | 11.777 ms |
| First successful `Present` | 3.890 s, frame 0 | 2.580 s, frame 0 |
| Last observed Present / frame | 399.099 s / 5,298 | 231.750 s / 2,525 |

Presentation gaps are measured by the proxy's own `frame_normal` Present-to-Present metric.
Summary reports are emitted on observed proxy activity, **not** by Present, so the closing
Present is bounded between the previous device report and the report that carried the metric.

| Run | Gap | Bounded interval | End frame | Label |
| --- | ---: | --- | ---: | --- |
| A | 2.213 s | 3.890 – 7.682 s | 2 | pre-menu initialization (**not** in run B) |
| A | **30.110 s** | 7.015 – 37.982 s | 7 | **main-menu load** |
| A | **106.681 s** | 50.351 – 158.053 s | 365 | **save-game load** |
| B | **29.786 s** | 3.640 – 34.454 s | 5 | **main-menu load** |
| B | **101.892 s** | 41.663 – 143.575 s | 226 | **save-game load** |

**Labelling evidence.** The menu gap is followed immediately by a sustained ~28 fps sequence
(run A frames 5–363 across 38.2–51.4 s) and its instrumented work vector is *byte-identical*
to the gap that iteration-06 and iteration-07 each show when the player **returns to the main
menu** mid-session (iteration-06 at 537.9–568.7 s, 30.115 s; iteration-07 at 378.0–409.9 s,
31.099 s). The save gap contains the run's only successful `gzopen` and **13,882,714 `gzread`
calls returning 43,183,555 bytes** — a gzip savegame stream; iterations 06 and 07 started a
new game instead and have **no `gzread` calls at all** and 13/13 failed `gzopen`. The gap ends
into a sustained ~33 fps gameplay sequence. Run A's 2.213 s gap at 3.9–7.7 s cannot be labeled:
it has no counterpart in run B and no marker distinguishes it from ordinary initialization.
No phase markers exist in any of these logs, so "menu ready" versus "first menu frame
presented" still cannot be separated.

Report stalls — intervals where the proxy's 1 Hz reporting stopped, i.e. the engine crossed
no proxy device boundary at all — line up exactly with the gaps:

| Run | Stall | Length | Instrumented exclusive inside |
| --- | --- | ---: | ---: |
| A | 8.682 – 24.470 s (menu) | 15.788 s | 0.334 s |
| A | 51.265 – 83.941 s (save) | 32.676 s | 4.589 s |
| A | 108.411 – 153.417 s (save) | 45.005 s | 6.948 s |
| A | 153.417 – 156.558 s (save) | 3.142 s | 0.443 s |
| B | 4.301 – 20.540 s (menu) | 16.239 s | 0.437 s |
| B | 41.489 – 75.074 s (save) | 33.585 s | 4.705 s |
| B | 99.796 – 138.746 s (save) | 38.950 s | 1.259 s |
| B | 138.746 – 142.073 s (save) | 3.327 s | 0.464 s |

## 2. Accounting for the two big gaps

**Non-additivity caveats.** Inclusive spans are CPU wall time and may overlap other
measurements; only the exclusive column (nested *loading* spans subtracted) may be added, and
even that sum is a lower bound because concurrent threads overlap and a call may begin before
its report window. Byte columns have different meanings (`ReadFile` returns bytes; the D3DX
helper reports input payload; `gzread` reports returned output bytes; `inflate` and the mesh
methods report none). Report deltas are posted on completion, so a gap's table is an overlap
attribution, not an exact interval. Two report windows straddle each gap boundary in every
case below.

### Main-menu load — 30.110 s (A) / 29.786 s (B)

| Operation | A calls | A incl. s | A mean | B calls | B incl. s | B mean | Bytes (A) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `ID3DXMesh::GenerateAdjacency` | 1,017 | **3.160** | 3.107 ms | 1,017 | **3.217** | 3.163 ms | — |
| `D3DXCreateTextureFromFileInMemoryEx` | 461 | **2.374** | 5.149 ms | 467 | **2.366** | 5.067 ms | 299,186,760 |
| `inflate` | 166,021 | 1.637 | 9.9 µs | 167,720 | 1.672 | 10.0 µs | — |
| `ReadFile` | 43,420 | 0.290 | 6.7 µs | 45,855 | 0.368 | 8.0 µs | 184,902,281 |
| `ID3DXMesh::OptimizeInplace` | 1,017 | 0.203 | 200 µs | 1,017 | 0.217 | 214 µs | — |
| `CreateFileA` | 599 | 0.136 | 226 µs | 1,176 | 0.242 | 206 µs | — |
| `D3DXCreateEffect` | 9 | 0.113 | 12.50 ms | 10 | 0.036 | 3.56 ms | 258,208 |
| `D3DXCleanMesh` | 1,023 (12 fail) | 0.089 | 87 µs | 1,023 (12 fail) | 0.101 | 99 µs | — |
| `D3DXLoadSurfaceFromFileInMemory` | 3 | 0.054 | 18.08 ms | 4 | 0.108 | 26.99 ms | 2,480,817 |
| `xmlReadMemory` | 2 | 0.025 | 12.68 ms | 2 | 0.024 | 12.19 ms | 3,688,819 |
| `D3DXCreateMesh` | 1,017 | 0.020 | 19 µs | 1,017 | 0.024 | 24 µs | — |
| `D3DXCreateCubeTextureFromFileInMemoryEx` | 9 | 0.014 | 1.60 ms | 11 | 0.016 | 1.44 ms | 1,509,024 |
| `SetFilePointer` | 1,792 | 0.002 | 1.0 µs | 3,523 | 0.004 | 1.0 µs | — |
| **Instrumented exclusive** | | **8.433** | | | **8.395** | | |
| **Unexplained remainder** | | **21.677** | | | **21.391** | | |

The run-B open/read/`SetFilePointer` counts are higher only because run B's gap bound starts
earlier and sweeps in more pre-menu windows; the mesh and texture vectors are identical.

### Save-game load — 106.681 s (A) / 101.892 s (B)

| Operation | A calls | A incl. s | A mean | B calls | B incl. s | B mean | Bytes (A) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `ID3DXMesh::GenerateAdjacency` | 3,584 | **11.331** | 3.161 ms | 3,584 | **11.476** | 3.202 ms | — |
| `D3DXCreateTextureFromFileInMemoryEx` | 749 | **6.414** | 8.563 ms | 749 | **6.644** | 8.870 ms | 679,014,071 |
| `CreateFileA` | 2,357 | **6.382** | 2.708 ms | 2,362 | **0.795** | 0.337 ms | — |
| `inflate` | 380,589 | 4.033 | 10.6 µs | 380,599 | 4.041 | 10.6 µs | — |
| `gzread` | 13,970,460 | 2.581 | **0.185 µs** | 13,970,460 | 2.632 | 0.188 µs | 43,542,721 |
| `ID3DXMesh::OptimizeInplace` | 3,584 | 0.839 | 234 µs | 3,584 | 0.841 | 235 µs | — |
| `ReadFile` | 103,660 | 0.799 | 7.7 µs | 103,678 | 0.798 | 7.7 µs | 409,439,719 |
| `D3DXCleanMesh` | 3,589 (10 fail) | 0.433 | 121 µs | 3,589 (10 fail) | 0.434 | 121 µs | — |
| `xmlReadMemory` | 790 | 0.240 | 303 µs | 790 | 0.246 | 311 µs | 22,531,669 |
| `D3DXCreateMesh` | 3,584 | 0.073 | 20 µs | 3,584 | 0.075 | 21 µs | — |
| `D3DXCreateEffect` | 4 | 0.055 | 13.79 ms | 4 | 0.013 | 3.22 ms | 122,040 |
| `D3DXCreateCubeTextureFromFileInMemoryEx` | 13 | 0.021 | 1.64 ms | 13 | 0.019 | 1.50 ms | 3,606,656 |
| `SetFilePointer` | 6,370 | 0.015 | 2.3 µs | 6,385 | 0.014 | 2.2 µs | — |
| `gzopen` | 1 | 0.0004 | 0.43 ms | 1 | 0.0005 | 0.53 ms | — |
| **Instrumented exclusive** | | **33.268** | | | **28.064** | | |
| **Unexplained remainder** | | **73.413** | | | **73.828** | | |

The entire 5.2 s instrumented difference between the runs is `CreateFileA` (6.382 s versus
0.795 s for the same ~2,360 opens). The unexplained remainder is the same in both.

### Internal structure of the save load (run A, run B in brackets)

| Phase | Length | Instrumented | Dominant instrumented work |
| --- | ---: | ---: | --- |
| S1 savegame decode (stall) | 32.68 s [33.59] | 4.59 s [4.71] | `gzread` 13,882,714 calls / 2.565 s returning 43,183,555 B (**3.11 B per call**); `inflate` 150,269 / 1.702 s; `ReadFile` 38,181 / 157.4 MB |
| S2 sector asset build (1 Hz reports) | 24.47 s [24.72] | 17.40 s [17.66] | `GenerateAdjacency` 3,497 / 11.154 s [3,514 / 11.317 s]; texture helpers 2.483 s [2.617 s]; **nine consecutive windows at 89.1–99.3 s run 866 adjacency calls with at most three file opens and three inflate calls between them** |
| S3 script/XML setup (stall) | 45.01 s [38.95] | 6.95 s [1.26] | `CreateFileA` 1,368 / 6.142 s at **4.490 ms per open** [1,362 / 0.548 s at 0.403 ms]; `xmlReadMemory` 790 / 0.240 s / 22.5 MB |
| S4 final texture burst (stall) | 3.14 s [3.33] | 0.44 s [0.46] | 74 texture helpers consuming 187,272,175 B; `inflate` 28,026 |
| tail | ~1.1 s | 3.086 s | one single 3.086 s texture-helper call on 136,658 input bytes |

Phase S3 is where run A and run B diverge: identical call counts, 11× different open latency.
That is host filesystem state, not game work.

## 3. Repetition and reproducibility

**Menu reloads are exactly repeated.** Gap work vectors (call counts and byte totals with all
durations removed) match across sessions:

| Work vector | Occurrences | Gap length | Adjacency | 2D helper bytes |
| --- | --- | ---: | ---: | ---: |
| `f60c00d1a1989bf4` | run A menu (7.0 s), iteration-06 startup menu (5.5 s) | 30.110 / 30.783 s | 1,017 | 299,186,760 |
| `ef901ba64e905f7e` | run B menu (3.6 s), iteration-07 startup menu (5.7 s) | 29.786 / 30.843 s | 1,017 | 299,196,640 |
| (near-match) | iteration-06 / -07 **menu return** mid-session | 30.115 / 31.099 s | 1,017 / 1,022 | 298,138,122 (both) |

The two startup vectors differ only by 6 texture calls / 9,880 bytes at the attribution
boundary. The mid-session **return** to the menu costs the same 30 s and repeats 1,017
`GenerateAdjacency` calls (1,022 in iteration-07), 459 texture-helper calls and the same
298,138,122 bytes of texture input after the
identical assets had already been loaded once in the same process. Inside those blocks the
iteration-04 five-window signatures recur verbatim — 344 adjacency calls, 80 opens,
33,140,736 read bytes, 31,805 inflate calls; and 364 adjacency / 270 opens / 79,132,160 bytes;
and 480 adjacency / 149 opens / 48,546,816 bytes — at both 23.4 s and 555.1 s in iteration-06
and at both 23.7 s and 396.2 s in iteration-07.

**Run A versus run B is deterministic in counts, variable in latency.** Whole-run totals:

| Operation | A calls | B calls | A incl. s | B incl. s | Bytes (both) |
| --- | ---: | ---: | ---: | ---: | ---: |
| `GenerateAdjacency` | 4,627 | 4,621 | 14.492 | 14.694 | — |
| `D3DXCreateTextureFromFileInMemoryEx` | 1,223 | 1,223 | 8.795 | 9.016 | 987,998,503 (identical) |
| `inflate` | 551,846 | 551,846 | 5.728 | 5.749 | — |
| `gzread` | 13,970,478 | 13,970,478 | 2.581 | 2.632 | 43,542,947 (identical) |
| `ReadFile` | 150,572 | 150,568 | 1.206 | 1.210 | 610,567,707 / 610,161,027 |
| `CreateFileA` | 3,617 (2 fail) | 3,616 (2 fail) | **6.855** | **1.073** | — |
| `xmlReadMemory` | 792 | 792 | 0.265 | 0.270 | 26,220,488 (identical) |

Everything except `CreateFileA` reproduces within 4 %. This says the load is dominated by
**deterministic CPU work**, not by I/O variance — except open latency, which varied 6.4× on the
whole run. TAA on/off changed none of the loading counts; captured frames (2554+ in A, 559+ in
B) are all after the loads and do not perturb them.

Counters carry no content identity: equal counts and byte totals are evidence of recurring
work, never proof that two meshes or textures hold identical bytes. Similar sizes must never
be used as a cache key.

## 4. Sector transitions (iteration-06)

Mid-run gaps between 1 s and 30 s, excluding the menu return:

| Gap | Interval | End frame | Instrumented | Unexplained | Dominant work |
| ---: | --- | ---: | ---: | ---: | --- |
| 14.445 s | 259.6 – 274.4 s | 5,787 | 6.697 s | 7.748 s | `GenerateAdjacency` 2,076 / **5.188 s** (2.50 ms); `inflate` 60,534 / 0.669 s; `OptimizeInplace` 2,076 / 0.425 s; only 54 texture helpers / 0.029 s |
| 18.397 s | 499.9 – 519.3 s | 17,241 | 9.938 s | 8.459 s | `GenerateAdjacency` 2,471 / **6.030 s** (2.44 ms); `D3DXCreateTextureFromFileInMemoryEx` 47 / **2.258 s** (48.0 ms); `inflate` 65,507 / 0.705 s |
| 2.960 s | 469.1 – 472.6 s | 15,127 | 2.867 s | **0.093 s** | 25 texture helpers / **2.768 s** (110.7 ms each) on only 9,538,650 input bytes |
| 1.498 s | 65.3 – 67.8 s | 1,868 | 0.878 s | 0.620 s | `inflate` 47,580 / 0.536 s; 172 texture helpers / 0.140 s |
| 1.479 s | 518.8 – 520.9 s | 17,250 | 0.605 s | 0.874 s | `GenerateAdjacency` 189 / 0.304 s |

Sector changes are **mesh-preparation dominated**: `GenerateAdjacency` alone is 36 % of the
14.4 s gap and 33 % of the 18.4 s gap, and adjacency plus `OptimizeInplace` plus `CleanMesh`
account for most of the instrumented time in both. The 2.960 s gap is the one fully explained
gap in this corpus — instrumented work covers 97 % of it, and 25 texture-helper calls
averaging 110.7 ms are 94 % on their own. Both large
sector gaps still leave 7.7–8.5 s unattributed, and both open with a ~5.5 s report stall whose
instrumented content is only ~0.5 s of `inflate`.

## 5. Ranked candidates, with evidence-derived upper bounds

Upper bounds assume the operation's measured inclusive time goes to zero, which no real change
achieves; they bound the opportunity, not a forecast. Instrumented time can also overlap other
measured time, so these must not be added.

1. **Enable the existing adjacency cache (`X3M_MESH_CACHE`).** Disabled in both runs.
   Bound: **11.33 s** of the save load, **3.16 s** of the menu load, **5.19 s / 6.03 s** of the
   two iteration-06 sector changes, **14.49 s** of the whole run A. Strongest supporting
   evidence: the mid-session menu return repeats 1,017 identical-count adjacency calls after
   the same process already did them, and nine consecutive save-load report windows do
   adjacency with essentially no file I/O — geometry already in
   memory being reprocessed (866 calls across nine windows at 89.1–99.3 s). `OptimizeInplace`
   (0.84 s) and `CleanMesh` (0.43 s) still run per
   iteration-04's design and are not part of the bound.
2. **Texture-helper decode/convert.** Bound: **6.41 s** save, **2.37 s** menu, **8.80 s** whole
   run; 2.77 s of one 2.96 s sector gap. The matching D3D `CreateTexture` backend calls took
   only **0.030 s across 1,240 calls**, so ~99.7 % of the helper span is decode, filtering,
   conversion, population or synchronization — not allocation. Worst single call: 3.086 s on
   136,658 input bytes. Per-run input is identical (987,998,503 B), so a content-keyed cache
   of converted results has a real repeat population; the menu return re-submits 298 MB.
3. **Savegame `gzread` chunking.** 13,882,714 calls returning 43,183,555 bytes = **3.11 bytes
   per call**. Measured bound is only **2.57 s**, but this sits inside the 32.7 s S1 stall whose
   caller-side loop is uninstrumented, and **our own tracing adds ≥1.91 s of wrapper tail on
   this hook alone** (136.7 ns/call × 13.97 M) — so part of the observed save-load length in
   these telemetry runs is instrumentation. Fixing the call size is cheap to bound but its true
   payoff cannot be read off these counters.
4. **Open-probe latency and count.** Bound: **6.14 s** in run A's S3 phase (1,368 opens at
   4.490 ms) versus **0.55 s** for the same opens in run B — so **5.6 s is host I/O state**, not
   fixed cost. Reducing the open count is the portable half; 3,617 opens per run with 2
   failures and no path capture means the probe pattern is still unidentified. Paths are
   deliberately not logged, so optional-file probes and real errors cannot be separated.
5. **Compressed-resource chunking (`inflate`).** Bound: **4.03 s** save, **1.64 s** menu,
   **5.73 s** whole run at 10.4 µs per call over 551,846 calls. The 1 KiB input loop from the
   earlier static analysis still stands; the immediate cannot be changed alone without
   overflowing the existing stack buffer.
6. **Everything else is small.** Whole-run A: `D3DXCreateEffect` 0.310 s, `xmlReadMemory`
   0.265 s for 26.2 MB, `D3DXLoadSurfaceFromFileInMemory` 0.148 s, `SetFilePointer` 0.018 s,
   `gzopen` 0.016 s. Proxy diagnostics: `shader_inspect` 0.543 s over 29,594 calls, raw
   `shader_dump` 0.291 s over 58 calls, `log_flush` 4.4 ms over 5,894 calls, sampled
   `lock_wait` 1.495 s over 7.19 M acquisitions, and loading wrapper tails 2.044 s (of which
   1.910 s is the `gzread` hook). None of these is a loading target, but the wrapper tails are
   a real, if lower-bound, perturbation of the measured save load.

### What no hook explains

| Gap | Length | Unexplained | Share |
| --- | ---: | ---: | ---: |
| Menu load, run A | 30.110 s | **21.677 s** | 72 % |
| Menu load, run B | 29.786 s | **21.391 s** | 72 % |
| Save load, run A | 106.681 s | **73.413 s** | 69 % |
| Save load, run B | 101.892 s | **73.828 s** | 72 % |
| Sector change, iteration-06 | 14.445 s | 7.748 s | 54 % |
| Sector change, iteration-06 | 18.397 s | 8.459 s | 46 % |

Even summing every candidate above at its unreachable upper bound leaves the majority of both
loads unaccounted for. The unexplained time concentrates in the report stalls, where the engine
crossed no proxy boundary: 15.8 s in the menu load with 0.33 s instrumented, 32.7 s in save
phase S1 with 4.59 s, and 45.0 s in save phase S3 with 6.95 s. These logs contain **no process
CPU sampling**, so a stall is not proof of CPU-bound work rather than waiting, and main-module
IAT hooks miss DLL-internal I/O entirely. Attributing those seconds requires a sampling
profiler correlated to QPC, not another counter.

Nothing here implements or measures a loading improvement. Controlled before/after runs remain
necessary before claiming any speedup.
