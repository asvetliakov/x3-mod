# Loading on the X3 bottle, run 8: gz read-ahead buffer + adjacency verify

Provenance. `/tmp/x3-bottleX3-run8/session-20260912-194314-212.log`, 43,439,473 B,
858,806 lines, sha256 `5234ba73e78a4dd062da7f9f9431cb5f2ddc074c6a2a21b8cc1877477734afce`.
Bottle `X3` (`WineArch=arm64`, FEX, `FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`),
`launch --direct --telemetry --mesh-adjacency verify --gz-buffer`, no profiler, route off.
Path: start → main menu → the usual save → flight → sector change → exit.
Analysis `verification/results/loading-x3-run8.json`
(`python3 tools/analysis/analyze_loading_profile.py <log> --output …` for the gap tables,
a short script for the whole-run sums, stall decomposition and adjacency tables).
Reference: [loading-profile-bottle-x3.md](../reverse-engineering/loading-profile-bottle-x3.md)
(run 5, same bottle, route off, no buffer, native adjacency). No game was launched for this analysis.

## Report

1. **Phases (run 8 → run 5):** menu load **8.159 s** → 9.726; **save load 33.363 s** → 65.460 (**−49 %**); sector change **5.411 s** → 7.976; menu return **7.448 s** → 8.784. QPC 10 MHz (`inclusive_ticks/total_us` = 65627/6562.700 = 1.0e7). `analyze_loading_profile.py` runs unchanged on a telemetry-only log (4 gaps, 0 profile blocks); the log writes no phase marker, so gaps are its presentation-gap heuristic.
   *The save load is now under the user's ~40 s plain-run stopwatch while carrying full telemetry and a double adjacency pass — the read-ahead buffer paid for the whole instrumentation and then some.*
2. **gz:** `gzread` **179 calls / 0.195 s / 46,803,550 B** whole run (run 5: 13,970,478 / 9.162 s / 43,542,947 B); 175 / 0.186 s / 45,754,974 B inside the save gap. `gztell` 179, `gzopen` 27 (22 fail), `gzclose` 5, **`gzgetc`/`gzseek`/`gzwrite` zero calls all run**. `inflate` **774,489 / 12.180 s** vs run 5's 780,091 / 12.183 s — unchanged, and 153,159 / 2.630 s in the decode stall vs 150,279 / 2.549 s. Decode stall **25.31 s → 5.13 s**.
   *The buffer removed the gzread call-count problem outright (82,639 engine calls per real read) and left `inflate` untouched, which is now the single largest hooked item in the run.*
3. **`gz_buffer_file`:** 5 lines because it is emitted once per `gzclose` of a buffered handle, never per report window. Only four are tiny; the third **is** the savegame — 14,461,803 calls / 14,354,293 small / 45,754,974 served B / **175 real reads** — closing at **39.202 s, inside the save gap**.
   *Nothing is lost and no unpatched path exists: the buffer's `real_bytes` equals the in-gap `gzread` metric bytes to the byte. The four tiny handles are save-selection header probes (410 B wanted, 1 MiB prefetched, 9.1 ms).*
4. **Instrumentation envelope:** wrapper tail **0.133 s of 26.034 s hooked** (0.51 %; run 5: 1.424 s of 43.870 s, of which gzread alone was 1.282 s) = 0.09 % of the 155 s run. `CreateFileA` 4,250 / 0.445 s / **104.7 µs** (run 5: 4,288 / 2.537 s / 591.7 µs); `FindFirstFileA` 4,571 (83.3 % fail) / 0.718 s / **157.0 µs** (4,611 / 2.559 s / 555.1 µs); `ReadFile` 208,263 / 1.714 s / **8.23 µs**, tail 21.7 ms (209,790 / 1.695 s / 8.08 µs).
   *Our measurable overhead on FEX is now negligible — but `CreateFileA`/`FindFirstFileA` are 4–6× cheaper per call than run 5 at identical call counts with no configuration difference, so the script/XML stall's 24.98 s → 16.91 s belongs to the host/bottle, not this build, and run 5 ↔ run 8 file latency is not a controlled pair.*
5. **Adjacency verify:** 7,715 calls, 0 fallbacks/faults/native failures, all quantized; **7,548 equal (97.83 %), 167 mismatched (2.17 %), 26,217 mismatch entries of 19,956,021 (0.131 %)**; **fast 0.804 s vs native 2.971 s (3.70×)**, 104 vs 385 µs/call, 3.5–4.1× per phase. Of the 64 logged lines: **degenerate_faces>0 in 5, multi_candidates>0 in 56 (88 %)**, normal_selected>0 in 43; first mismatch **native≠-1/fast=-1 in 11, both valid in 30 (9 off by one face index), native=-1/fast≠-1 in 23**, both -1 in 0.
   *The fast path is not equivalent: it disagrees on 2.2 % of meshes and the disagreement tracks the multi-candidate (coincident-edge) tie-break rather than degeneracy, so it must not ship as a silent replacement until the tie-break reproduces D3DX's choice.*

## 1. Phase durations

Gaps are presentation gaps (`analyze_loading_profile.py`, threshold 2 s). Anchor `proxy_initialize`,
QPC 10 MHz, 131 report windows, 70 of them carrying `loading_metric`, last present 154.967 s.

| # | Label | Interval | Gap | Hooked excl. | Unexplained | Run 5 gap | Run 5 hooked |
| ---: | --- | --- | ---: | ---: | ---: | ---: | ---: |
| 1 | menu load | 3.529–12.005 s | **8.159 s** | 4.781 s | 3.378 s | 9.726 s | 5.178 s |
| 2 | save load | 24.044–57.545 s | **33.363 s** | 12.825 s | 20.538 s | 65.460 s | 25.942 s |
| 3 | sector change | 126.329–131.873 s | **5.411 s** | 3.503 s | 1.908 s | 7.976 s | 4.743 s |
| 4 | menu load (return) | 145.462–153.165 s | **7.448 s** | 4.468 s | 2.980 s | 8.784 s | 5.310 s |

`loading_metric` rows are **per-window deltas**, not cumulative: `take_snapshot()` exchanges every
counter to 0, and the report is emitted from `telemetry_summary` (1 s interval, device 0). Only
`mesh_adjacency_metric` carries `cumulative=1`. A window closes only when the engine thread reaches the
1 s poll, so each stall appears as one long window whose content cannot be subdivided — which is exactly
what makes the two save-load stalls addressable.

### The save load, 33.363 s, decomposed by report-window interval

| Segment | Interval | Length | Hooked | Content | Run 5 |
| --- | --- | ---: | ---: | --- | ---: |
| **savegame decode** | 23.70–29.17 s | **5.13 s** | 3.140 s | `inflate` 153,159/2.630 s; `ReadFile` 38,931/0.234 s; `gzread` 175/0.185 s; `CreateFileA` 193/0.044 s | **25.31 s** / 11.97 s |
| object/mesh/texture build | 29.17–39.20 s | 10.03 s | 7.171 s | 3,342 meshes (adjacency+create+clean+optimize), `inflate`, 647 MB texture input | ~15.2 s / ~10.4 s |
| **script/XML file storm** | 39.20–56.11 s | **16.91 s** | 1.642 s | `FindFirstFileA` 1,396/0.610 s; `CreateFileA` 1,374/0.231 s; `xmlReadMemory` 790/0.216 s; `inflate` 15,619/0.246 s | **24.98 s** / 3.56 s |
| tail (last textures/effects) | 56.11–57.54 s | 1.43 s | 0.872 s | textures, 4 effects | — |

The 5.13 s decode figure is the gap-clipped view of a 5.48 s blocked stretch (23.698–29.174 s): the
device-0 summary itself reported `interval_us=5,476,226`, so the engine thread did not reach the 1 s
poll for 5.48 s. Its residue after hooks is ≤2.34 s, covering both the engine's own per-record
deserialization and the buffer's uninstrumented fast path — an upper bound of **0.16 µs per served
call** for the buffer, against the ~0.95 µs per call (hook tail + zlib entry + engine call overhead)
that the same loop cost in run 5.

## 2. gz family, whole run and in the save load

| Op | Run 8 count | excl. s | mean | bytes | Run 5 count | Run 5 excl. s | Run 5 mean |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `gzread` | **179** | **0.195** | 1.092 ms | 46,803,550 | 13,970,478 | 9.162 | 0.656 µs |
| `inflate` | 774,489 | **12.180** | 15.73 µs | — | 780,091 | 12.183 | 15.62 µs |
| `gzopen` | 27 (22 fail) | 0.008 | 308 µs | — | 27 (24 fail) | 0.036 | 1.35 ms |
| `gztell` | 179 | 0.0002 | 1.38 µs | — | not hooked | — | — |
| `gzclose` | 5 | 0.0004 | 85.8 µs | — | not hooked | — | — |
| `gzgetc` / `gzseek` / `gzwrite` | **0** | 0 | — | — | not hooked | — | — |

With the buffer on, the `GzRead` metric counts only the real 256 KiB chunk reads the buffer issues
(documented at `src/proxy/loading_trace.cpp:604`); the engine-side served calls are in `gz_buffer_file`.
In the save gap the metric shows `gzread` 175 / 0.186 s / 45,754,974 B, `gzopen` 1, `gztell` 175,
`gzclose` 1 — **the same 175 reads and the same byte count as the savegame handle's `real_reads` and
`real_bytes`**, which settles that the last `gzread` window inside the gap does cover the whole save load
and that no part of it takes an unpatched path. `gzgetc`/`gzseek` having zero calls also settles
negatively the open question of whether the save decoder uses the character or seek entry points.

### The five `gz_buffer_file` lines

Emitted once per `gzclose` of a buffered handle, so there are exactly as many lines as buffered handles
closed in the run — not one per report window, and not one per gz call site.

| # | Report window | calls | small_calls | served B | real_reads | real B | notes |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 1–2 | 17.70 s | 9 / 9 | 8 / 7 | 92 / 113 | 1 / 1 | 262,144 each | save-selection header probes |
| 3 | **39.20 s** | **14,461,803** | 14,354,293 | **45,754,974** | **175** | 45,754,974 | **the savegame** |
| 4–5 | 141.78 s | 9 / 9 | 8 / 7 | 92 / 113 | 1 / 1 | 262,144 each | save-selection probes on the way out |

99.26 % of the savegame's calls are small (<256 B) and **82,639 engine calls are served per real read**;
`getcs`, `tells`, `seeks`, `direct_reads` are all 0 and `error=0`, so the buffer never fell back or
rewound on the save stream. The only cost it adds is read amplification on the four probe handles:
410 B were wanted and 1 MiB was prefetched, for 9.1 ms of hooked `gzread`. Capping the first chunk for
a handle that is closed early would recover that; it is not worth a change at 9 ms.

## 3. Per-hooked-call envelope

Whole run, exclusive seconds and the wrapper tail (the hook's own measured overhead).

| Op | Count | Fail | Excl. s | Mean | Max | Tail s | Tail % | Run 5 count/excl/mean |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `inflate` | 774,489 | 0 | 12.180 | 15.73 µs | 0.87 ms | 0.0750 | 0.6 % | 780,091 / 12.183 / 15.62 µs |
| `ID3DXMesh::GenerateAdjacency` | 7,715 | 0 | 4.015 | 520.4 µs | 59.1 ms | 0.0014 | 0.0 % | 8,813 / 3.133 / 355.5 µs |
| `D3DXCreateTextureFromFileInMemoryEx` | 1,692 | 0 | 1.845 | 1.09 ms | 145 ms | 0.0004 | 0.0 % | 1,712 / 3.182 / 1.86 ms |
| `ReadFile` | 208,263 | 0 | 1.714 | **8.23 µs** | 9.9 ms | 0.0217 | 1.3 % | 209,790 / 1.695 / 8.08 µs |
| `D3DXCreateMesh` | 7,715 | 0 | 1.657 | 214.8 µs | 12.4 ms | 0.0137 | 0.8 % | 8,813 / 5.096 / 578.2 µs |
| `ID3DXMesh::OptimizeInplace` | 7,715 | 0 | 1.599 | 207.2 µs | 19.0 ms | 0.0013 | 0.1 % | 8,813 / 1.698 / 192.7 µs |
| `D3DXCleanMesh` | 7,729 | 28 | 1.022 | 132.2 µs | 14.3 ms | 0.0151 | 1.5 % | 8,832 / 1.741 / 197.1 µs |
| `FindFirstFileA` | 4,571 | 3,809 | 0.718 | **157.0 µs** | 6.0 ms | 0.0009 | 0.1 % | 4,611 / 2.559 / 555.1 µs |
| `CreateFileA` | 4,250 | 2 | 0.445 | **104.7 µs** | 19.3 ms | 0.0009 | 0.2 % | 4,288 / 2.537 / 591.7 µs |
| `xmlReadMemory` | 794 | 0 | 0.258 | 324.4 µs | 17.1 ms | 0.0003 | 0.1 % | 794 / 0.250 / 315.5 µs |
| `gzread` | 179 | 0 | 0.195 | 1.09 ms | 3.7 ms | 0.0000 | 0.0 % | 13,970,478 / 9.162 / 0.656 µs |
| `SetFilePointer` | 11,935 | 0 | 0.020 | 1.68 µs | 0.06 ms | 0.0016 | **8.2 %** | 12,049 / 0.022 / 1.85 µs |
| `FindNextFileA` | 2,202 | 0 | 0.044 | 19.8 µs | 1.6 ms | 0.0004 | 0.8 % | 2,202 / 0.072 / 32.8 µs |
| **total** | — | — | **26.034** | — | — | **0.133** | **0.51 %** | — / **43.870** / tail **1.424** |

The three tiny-call classes behave differently. `ReadFile` is unchanged from run 5 in both count and
per-call cost (8.1 → 8.2 µs) and carries the largest absolute tail after `inflate` (21.7 ms over 208 k
calls, 0.10 µs/call). `CreateFileA` and `FindFirstFileA` run the same call counts as run 5 (4,250 vs
4,288; 4,571 vs 4,611, with the same 83–84 % negative-probe rate) at **4–6× lower latency**, and that
is unexplained: run 8 differs from run 5 only by `--gz-buffer` and `--mesh-adjacency verify`, neither of
which touches the Wine file path, and run 6's slower figures are confounded by its sampling profiler.
Until that is isolated, the script/XML stall's 24.98 s → 16.91 s must be credited to the host/bottle,
not to this build. Our own visible overhead is 0.133 s per 155 s run; the previously dominant term
(gzread's 1.282 s of tail over 13.9 M calls) is gone with the calls.
`mesh_adjacency_fp_first` reports `control=0x023f status=0 mxcsr=0x9fc0 compute_mxcsr=0x1f80
restored=1`: the fast path runs with default MXCSR and restores the caller's x87/SSE state.

## 4. Adjacency verify

Final cumulative `mesh_adjacency_metric` (`cumulative=1`, mode `verify`, `faulted=0`):

| Quantity | Value |
| --- | ---: |
| calls / computed / fallbacks / faults / native failures | 7,715 / 7,715 / **0** / **0** / **0** |
| quantized / unquantized | 7,715 / 0 |
| faces / vertices / adjacency entries | 6,652,007 / 10,585,424 / 19,956,021 |
| verify meshes / **equal** / **mismatched** | 7,715 / **7,548 (97.83 %)** / **167 (2.17 %)** |
| mismatch entries | **26,217** (0.131 % of entries) |
| **fast** cumulative / max | **0.8038 s** / 24.623 ms |
| **native** cumulative / max | **2.9711 s** / 33.753 ms |
| fast vs native | **3.70×** (104.2 vs 385.1 µs per call) |
| welded vertices | 5,186,561 |
| multi-candidate meshes / normal_selected / degenerate_faces | 1,165 (15.1 %) / 10,114 / 194 |
| every `fallback_*` and `module_*` counter | 0 |

Per phase (differencing the cumulative line at the gap boundaries):

| Phase | Calls | fast s | native s | native/fast | equal | mismatched | mismatch entries | multi-cand meshes | degen faces |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| menu load | 989 | 0.091 | 0.369 | 4.05 | 972 | 17 | 2,936 | 92 | 93 |
| save load | 3,341 | 0.418 | 1.468 | 3.51 | 3,254 | 87 | 10,512 | 617 | 4 |
| sector change | 2,198 | 0.195 | 0.736 | 3.77 | 2,153 | 45 | 9,829 | 339 | 4 |
| menu return | 995 | 0.085 | 0.351 | 4.11 | 978 | 17 | 2,936 | 94 | 93 |

In `verify` the hook runs native first, returns the native result, then recomputes and compares, so the
4.015 s `GenerateAdjacency` hook total above is native 2.971 + fast 0.804 + 0.24 s of compare/alloc.
Replacing native with fast would save **2.17 s over the whole run** (0.28 s per menu load, 1.05 s of the
save load, 0.54 s of a sector change) — the same order as run 5's estimate, and still not a loading fix.

### The 64 logged mismatch lines

167 meshes mismatched; the log caps detail at 64 lines (`adjacency_mismatch_line_limit`), so this table
is the first 64 in call order, not a sample of the 167. `native` is the D3DX value at the first
differing entry, `fast` ours; `-1` is "no neighbour". No mesh data is in the log.

| Statistic | Value |
| --- | ---: |
| lines / of mismatched meshes | 64 / 167 |
| **degenerate_faces > 0** | **5** (8 %) |
| **multi_candidates > 0** | **56** (88 %) |
| normal_selected > 0 | 43 (67 %) |
| first mismatch **native ≠ -1, fast = -1** | **11** |
| first mismatch **both valid** | **30** (9 of them differing by exactly one face index) |
| first mismatch **native = -1, fast ≠ -1** | **23** |
| first mismatch both -1 | 0 (impossible: equal entries are not mismatches) |
| logged faces / mismatch entries | 448,739 / 12,871 (0.96 % of the logged entries) |
| concentration | the top 2 meshes carry **78.5 %** of the 12,871 |
| per-mesh mismatch fraction, median / max | 0.16 % / 9.91 % |
| per-mesh native/fast, median / min / max | 3.37× / 1.37× / 13.54× |

The signal is the tie-break, not degeneracy: 88 % of mismatching meshes report multiple neighbour
candidates for some edge and only 8 % report any degenerate face, and the worst two meshes
(59,029 faces / 8,519 mismatches / 1,269 multi-candidates, and 5,577 faces / 1,586 mismatches / **1,920**
multi-candidates) are the two with the most multi-candidate edges. Both directions of disagreement
occur, with "fast finds a neighbour that D3DX left at -1" (23) more common than the reverse (11), and a
third of cases picking a different valid neighbour — 9 of those differ by a single face index, i.e. two
coincident faces claimed in the opposite order.

| # | faces | vertices | mismatches | first | native | fast | native µs | fast µs | welded | multi_cand | normal_sel | degen |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 5404 | 11070 | 54 | 102 | 102 | -1 | 2727.200 | 792.700 | 7246 | 24 | 16 | 0 |
| 2 | 2344 | 3758 | 72 | 372 | 158 | -1 | 779.400 | 282.500 | 860 | 18 | 18 | 0 |
| 3 | 4550 | 8021 | 100 | 954 | 354 | -1 | 1798.900 | 528.600 | 4318 | 24 | 22 | 3 |
| 4 | 2406 | 3551 | 90 | 173 | 74 | 91 | 747.900 | 272.800 | 629 | 16 | 7 | 36 |
| 5 | 2826 | 5539 | 8 | 437 | 156 | 194 | 1156.800 | 258.700 | 3317 | 2 | 0 | 1 |
| 6 | 4203 | 7028 | 98 | 203 | 86 | 103 | 1465.300 | 468.500 | 3182 | 18 | 5 | 40 |
| 7 | 5392 | 11055 | 64 | 4698 | 1634 | -1 | 2572.600 | 692.200 | 7253 | 24 | 16 | 1 |
| 8 | 1089 | 2126 | 6 | 1251 | 418 | 594 | 424.300 | 157.000 | 1136 | 1 | 0 | 0 |
| 9 | 5577 | 12856 | 1586 | 4218 | 2937 | 3247 | 13125.200 | 969.100 | 10225 | 1920 | 524 | 0 |
| 10 | 4727 | 9207 | 2 | 8027 | 2676 | -1 | 1841.400 | 524.900 | 6395 | 0 | 0 | 0 |
| 11 | 12919 | 24267 | 805 | 2386 | -1 | 2726 | 14557.700 | 1714.400 | 16589 | 992 | 222 | 0 |
| 12 | 394 | 731 | 2 | 1178 | -1 | 393 | 168.300 | 55.300 | 236 | 3 | 0 | 0 |
| 13 | 7066 | 8524 | 8 | 9550 | 6716 | 6954 | 2685.500 | 696.900 | 4877 | 16 | 10 | 0 |
| 14 | 284 | 539 | 2 | 646 | -1 | 216 | 152.600 | 49.500 | 263 | 19 | 9 | 0 |
| 15 | 2206 | 2758 | 24 | 384 | 295 | 481 | 800.100 | 273.000 | 1520 | 34 | 10 | 0 |
| 16 | 1698 | 1975 | 11 | 306 | 476 | 791 | 543.800 | 204.500 | 1082 | 62 | 26 | 0 |
| 17 | 838 | 1047 | 4 | 315 | 357 | 788 | 269.500 | 91.500 | 590 | 15 | 9 | 0 |
| 18 | 8397 | 12570 | 9 | 2877 | 8360 | 7629 | 3368.000 | 966.800 | 6814 | 4 | 3 | 0 |
| 19 | 12636 | 19426 | 12 | 36591 | 12200 | 12201 | 4979.300 | 1409.900 | 9752 | 2 | 0 | 0 |
| 20 | 350 | 850 | 17 | 281 | 274 | 275 | 185.600 | 48.500 | 611 | 4 | 1 | 0 |
| 21 | 7740 | 8164 | 2 | 7260 | -1 | 2432 | 2069.400 | 875.100 | 1562 | 2 | 0 | 0 |
| 22 | 1780 | 2022 | 147 | 603 | 202 | 1225 | 483.900 | 231.800 | 316 | 31 | 18 | 0 |
| 23 | 1644 | 3146 | 11 | 397 | 280 | -1 | 683.800 | 195.500 | 1766 | 0 | 0 | 0 |
| 24 | 4315 | 10344 | 17 | 3765 | -1 | 3672 | 2654.200 | 546.900 | 7337 | 9 | 4 | 0 |
| 25 | 1616 | 3114 | 30 | 1920 | -1 | 1038 | 741.600 | 186.300 | 1918 | 5 | 2 | 0 |
| 26 | 5655 | 7830 | 161 | 2742 | 5094 | -1 | 1901.500 | 637.100 | 4077 | 33 | 15 | 0 |
| 27 | 17212 | 35210 | 9 | 19374 | -1 | 6460 | 11260.100 | 2276.600 | 23105 | 1 | 1 | 0 |
| 28 | 3200 | 3823 | 163 | 351 | -1 | 2428 | 931.900 | 340.100 | 1383 | 50 | 15 | 0 |
| 29 | 1685 | 2243 | 66 | 1715 | -1 | 969 | 483.300 | 183.600 | 596 | 13 | 8 | 0 |
| 30 | 2360 | 2613 | 64 | 60 | -1 | 1724 | 601.800 | 299.400 | 693 | 25 | 13 | 0 |
| 31 | 2171 | 3974 | 62 | 261 | 88 | 116 | 825.300 | 278.700 | 2337 | 12 | 6 | 0 |
| 32 | 1864 | 3378 | 8 | 5004 | -1 | 1725 | 687.400 | 206.000 | 1941 | 3 | 2 | 0 |
| 33 | 74 | 74 | 22 | 89 | 47 | 63 | 73.000 | 10.500 | 8 | 2 | 0 | 0 |
| 34 | 3874 | 3209 | 29 | 6498 | -1 | 2167 | 872.900 | 386.100 | 335 | 9 | 1 | 0 |
| 35 | 152 | 202 | 6 | 397 | -1 | 151 | 83.700 | 21.700 | 10 | 2 | 2 | 0 |
| 36 | 8716 | 18025 | 28 | 818 | -1 | 1001 | 4605.100 | 1124.400 | 11225 | 5 | 3 | 0 |
| 37 | 2721 | 4558 | 16 | 458 | 914 | 362 | 975.700 | 376.700 | 2580 | 14 | 1 | 0 |
| 38 | 17615 | 43115 | 81 | 5961 | 15364 | 17131 | 9686.100 | 2415.000 | 33378 | 21 | 6 | 0 |
| 39 | 59029 | 78232 | 8519 | 1613 | 43763 | 24110 | 33753.300 | 24622.800 | 48502 | 1269 | 549 | 0 |
| 40 | 3646 | 5493 | 8 | 8361 | 3572 | -1 | 1278.600 | 384.700 | 3611 | 65 | 0 | 0 |
| 41 | 5446 | 10731 | 68 | 13416 | 4965 | 4966 | 2091.500 | 710.300 | 7883 | 16 | 12 | 0 |
| 42 | 14695 | 36224 | 10 | 7774 | 14450 | 13302 | 7576.400 | 1779.800 | 27944 | 9 | 2 | 0 |
| 43 | 18765 | 35653 | 49 | 253 | 16190 | 1326 | 8736.200 | 2712.200 | 23022 | 353 | 263 | 0 |
| 44 | 4912 | 10058 | 3 | 13413 | -1 | 4476 | 1909.900 | 571.800 | 7485 | 7 | 7 | 0 |
| 45 | 4806 | 9432 | 16 | 1919 | 4436 | 4733 | 2255.100 | 632.100 | 5455 | 4 | 2 | 0 |
| 46 | 3471 | 6660 | 10 | 3982 | 2419 | 1328 | 1618.500 | 466.000 | 3581 | 25 | 3 | 0 |
| 47 | 8247 | 13248 | 22 | 1323 | 7894 | 7806 | 4727.900 | 1024.700 | 6668 | 0 | 0 | 0 |
| 48 | 3892 | 6583 | 8 | 8386 | 3011 | 3012 | 1482.400 | 490.800 | 3067 | 1 | 1 | 0 |
| 49 | 10867 | 17725 | 4 | 21709 | 7240 | -1 | 3994.900 | 1447.100 | 7895 | 0 | 0 | 0 |
| 50 | 878 | 1702 | 3 | 822 | -1 | 276 | 344.500 | 104.200 | 725 | 3 | 0 | 0 |
| 51 | 9124 | 17766 | 36 | 2231 | 793 | 794 | 3718.600 | 1112.400 | 9134 | 3 | 0 | 0 |
| 52 | 1750 | 4250 | 39 | 402 | -1 | 136 | 869.000 | 189.700 | 3029 | 13 | 0 | 0 |
| 53 | 386 | 892 | 3 | 510 | -1 | 172 | 196.900 | 45.200 | 608 | 1 | 0 | 0 |
| 54 | 7764 | 12882 | 6 | 7586 | 2578 | 2577 | 3475.400 | 886.200 | 5751 | 0 | 0 | 0 |
| 55 | 8912 | 13443 | 2 | 12974 | -1 | 4328 | 3733.300 | 1111.800 | 5121 | 12 | 12 | 0 |
| 56 | 5359 | 8385 | 8 | 7318 | 2443 | -1 | 1925.900 | 700.100 | 3554 | 0 | 0 | 0 |
| 57 | 7918 | 12581 | 2 | 4787 | -1 | 1599 | 2943.400 | 934.300 | 5321 | 0 | 0 | 0 |
| 58 | 642 | 1276 | 3 | 1290 | -1 | 432 | 281.700 | 82.500 | 607 | 0 | 0 | 0 |
| 59 | 24268 | 37652 | 23 | 36906 | 23783 | -1 | 11527.800 | 2867.600 | 19318 | 26 | 21 | 0 |
| 60 | 17800 | 33916 | 61 | 18482 | 14724 | 14723 | 9867.400 | 2370.500 | 19364 | 5 | 3 | 0 |
| 61 | 14884 | 23510 | 18 | 14896 | 10036 | 10035 | 5877.400 | 1735.100 | 10514 | 2 | 2 | 0 |
| 62 | 35195 | 54085 | 28 | 14810 | -1 | 32701 | 17229.200 | 4772.300 | 25947 | 82 | 78 | 0 |
| 63 | 1668 | 3812 | 6 | 2748 | -1 | 918 | 856.400 | 201.700 | 2404 | 2 | 0 | 0 |
| 64 | 4715 | 4549 | 20 | 1521 | 508 | 509 | 1282.300 | 514.800 | 1220 | 1 | 0 | 0 |


## 5. Limits

* `loading_metric` rows are per-window deltas and a window closes only when the engine thread reaches
  the 1 s telemetry poll; a stall is therefore one long window and its content cannot be time-ordered
  inside itself. Only exclusive seconds may be summed, and they are lower bounds.
* **Run 8's savegame is not byte-identical to run 5's**: 14,461,803 engine gz calls / 45,754,974 B
  against 13,970,460 / 43,542,721 B (+3.5 % calls, +5.1 % bytes), and 3,342 vs 3,584 adjacency calls in
  the save gap. Run 8's is closer to run 6's 14,430,515. The save-load comparison carries that ~5 % gap.
* Wine file-API latency is 4–6× lower in run 8 than run 5 at equal call counts with no configuration
  difference that explains it (§3). The run 5 ↔ run 8 pair is not controlled for it, and the script/XML
  stall's improvement is not attributed to this build.
* The gz buffer's fast path is uninstrumented; its cost for the 14,354,293 served small calls is bounded
  only by the decode stall's ≤2.34 s residue (≤0.16 µs per call).
* `verify` mode runs both adjacency implementations and returns the native result; every
  `GenerateAdjacency` number here includes the fast pass, and the shippable cost is the fast column.
* The 64 mismatch lines are the first 64 of 167 in call order, capped by
  `adjacency_mismatch_line_limit`; the per-category counts in §4 describe those 64, while the cumulative
  metric describes all 7,715 meshes.
* Gap labels are `analyze_loading_profile.py` heuristics over hooked counts; the log contains no phase
  marker (`telemetry_phase_marker`: 0 lines). No sampling profiler ran, so there is no frame attribution
  for the 20.5 s unexplained in the save load.
