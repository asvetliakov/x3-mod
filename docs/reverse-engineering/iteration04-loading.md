# Diagnostics 0.4: completed loading and window observations

**GenerateAdjacency is now the largest measured loading category: 6,588 calls took 21.287 seconds.** Three separated loading blocks repeat the same five consecutive report vectors, including API counts, failures and byte totals. This makes exact-key adjacency reuse a concrete next optimization candidate. The trace does not contain content identities, so it does not yet establish identical mesh inputs, cache hit rate or a game speedup. A substantial part of the longest presentation gap remains unexplained.

## Source and accounting

The user completed the test and identified the last capture as third-person. No X3AP/X3TC/X3Steam process appeared in native process inventories before or after analysis. The file was unchanged during a bounded read and a later stat check, ended on a complete newline, and contains 4,263,229 lines:

- `session-20260910-234001-212.log`, **214,204,690 bytes**.
- SHA-256 `f8a9f43e18c16d132e5337ba9dd3f69aabc54343c71b4ef57532d8125d9e5196`.
- Last flushed telemetry summary: **305.217 s** after proxy initialization; later cursor polls exist through frame 4123.
- No terminal `device_destroy` summary, so totals are observed flushed totals with a possible missing tail.
- QPC frequency 10 MHz; main loading coverage began at **4.923 ms** after proxy initialization. Earlier process startup is outside coverage.
- No phase markers. Report times are not automatically menu-ready, New Game, or gameplay phase boundaries.

[iteration04-loading-summary.json](../../verification/results/iteration04-loading-summary.json) contains compact derived totals, exact recurring vectors, relevant windows, cursor events and source provenance. Existing `analyze_loading_phases.py` / `summarize_telemetry.py` parsed all telemetry/loading records with **zero rejected records**. For this analysis, 2,819 relevant records were retained while streaming and hashing the entire raw file; no captured geometry/shader payload was needed. Raw captures remain local and untracked.

All durations below are CPU-side inclusive wall spans, not GPU execution times. Calls may overlap other measurements, and report deltas are posted on completion rather than split across report windows. Do not add the categories into a purported complete loading-time attribution.

## Actual hook coverage and loading costs

All **16 intended main-module imports** installed. The exact native D3DX fingerprint passed, and **two shared native mesh tables each installed all three method slots**. These method hooks cover any object sharing those tables, including DLL-originated calls, rather than only main-EXE calls. No ConvertPointRepsToAdjacency samples were emitted; all observed GenerateAdjacency calls succeeded. Unobserved direct internal calls or other tables remain outside that conclusion.

| Operation | Calls | Failures | Inclusive seconds | Longest call |
| --- | ---: | ---: | ---: | ---: |
| GenerateAdjacency | 6,588 | 0 | **21.287** | 311.175 ms |
| D3DXCreateTextureFromFileInMemoryEx | 2,091 | 0 | **13.976** | 3.151 s |
| inflate | 882,627 | 0 | **9.307** | 0.338 ms |
| CreateFileA | 4,268 | 2 | 4.170 | 58.120 ms |
| OptimizeInplace | 6,588 | 0 | 1.496 | 18.246 ms |
| ReadFile | 235,225 | 0 | 1.475 | 5.182 ms |
| D3DXCleanMesh | 6,610 | 44 | 0.705 | 6.599 ms |
| xmlReadMemory | 809 | 0 | 0.406 | 25.265 ms |
| D3DXCreateEffect | 22 | 0 | 0.340 | 64.244 ms |
| D3DXLoadSurfaceFromFileInMemory | 18 | 0 | 0.259 | 52.142 ms |
| D3DXCreateMesh | 6,588 | 0 | 0.144 | 2.885 ms |
| D3DXCreateCubeTextureFromFileInMemoryEx | 41 | 0 | 0.059 | 3.059 ms |
| SetFilePointer | 11,986 | 0 | 0.020 | 0.134 ms |
| gzopen | 26 | 24 | 0.015 | 7.009 ms |
| gzread | 18 | 0 | 0.000221 | 0.115 ms |

The 2D texture helper received **1,597,010,005 input bytes**; ReadFile returned **974,085,716 bytes**; XML received 43,513,663 bytes. These are different byte meanings, not independent disk-volume measurements. Inflate does not report input/output byte volume. Mesh methods deliberately report no geometry bytes or identity.

The observed CreateMesh/GenerateAdjacency/OptimizeInplace counts match, supporting the verified preparation path's frequent use. The 44 CleanMesh failures cannot be classified from aggregate counters: there are no per-call error codes or mesh identities. Do not skip cleaning or alter fallback behavior on that basis. The two failed opens and 24 failed gzopen calls likewise do not establish a user-visible loading error without paths or context.

One texture-helper report contains exactly one call, 136,658 input bytes and **3.150714 s**. Its 100.078–101.078 s report interval is shorter than that call, demonstrating why report windows cannot be treated as exact API execution intervals. The span includes helper work and possible synchronization, not merely file reading.

## Repetition evidence and next optimization

The following blocks have **identical five-window signatures** after removing timestamps/durations. Equality includes every emitted operation's count, failure/pending/ambiguous counts and byte totals—not just the mesh count:

| Report block, seconds after proxy init | GenerateAdjacency | Adjacency time | 2D helper time |
| --- | ---: | ---: | ---: |
| 22.647–28.870 | 344 | 1.922 s | 1.454 s |
| 60.484–66.709 | 344 | 1.931 s | 1.482 s |
| 289.383–295.652 | 344 | 1.919 s | 1.487 s |

Each block also contains 344 CreateMesh calls, 344 OptimizeInplace calls, 349 CleanMesh calls with 10 failures, 62 2D texture calls receiving **53,823,944 bytes**, 8,212 ReadFile calls returning **33,140,736 bytes**, and 31,805 inflate calls. The five successive adjacency counts are **28, 251, 49, 5, 11**. A later 42-mesh report pattern also recurs three times.

This is direct evidence of recurring preparation activity. It is consistent with repeated asset loading, but counters cannot prove that two meshes or textures have identical contents. Similar sizes/counts must never be used as a cache key.

The next candidate is a **bounded cache of successful GenerateAdjacency output keyed by exact current input identity**, while continuing CleanMesh and OptimizeInplace on the current mesh. The [original-mesh offline fixture](../../docs/verification/mesh-preparation.md) already establishes exact synthetic downstream parity for this approach. The live trace now adds actual cost and recurrence evidence. It does not supply the remaining production gates:

- Account for complete vertex/index content, declaration, counts/options, exact epsilon bits and verified runtime/schema identity; preserve native failures and optional-output semantics.
- Measure content-key reuse without logging geometry payloads: aggregate hits/misses, retained/evicted bytes and key/read/lookup/copy cost are sufficient diagnostics.
- Demonstrate useful retention within the 32-bit memory budget. A small cache can evict an entire recurring block before the next occurrence; these three blocks alone do not prove an attainable hit rate.
- Include real buffer acquisition/synchronization and allocation-failure fallback in net-cost and parity tests. Synthetic host-memory lookup times are not production costs.

Texture conversion remains a second candidate, supported by 13.976 s of measured helper work and recurring input-volume patterns. Neither candidate justifies a global filter/format shortcut. No cache, mesh shortcut or load-time improvement is implemented by this analysis.

## Presentation gaps and the remaining unknown

The longest ordinary Present-to-Present interval was **87.045 s**. Device summaries repeatedly reported frame 1422 from **100.558 to 182.934 s**, an observed unchanged-frame span of 82.377 s. Other long ordinary intervals were 30.293, 29.585 and 29.865 s. These measure gaps between presentations, not necessarily backend Present blocking or a precisely labeled user action.

Two regions distinguish the newly explained work from what remains unmeasured:

| Report region | Observed activity |
| --- | --- |
| **101.078–157.809 s** (56.731 s between grouped reports) | Only 4 adjacency calls / **0.000224 s**; 4 texture helpers / 0.683 s; 151,804 inflate calls / 1.762 s; 1,083 file opens / 3.744 s; reads / 0.300 s; XML / 0.269 s |
| **157.809–182.934 s** | 3,435 adjacency calls / **11.444 s**; 614 texture helpers / 1.909 s; inflate / 2.044 s |

The first region consists of two large report intervals, **28.121 s** and **28.610 s**. Completed mesh spans do not account for most of that region. Calls can cross report boundaries, and uninstrumented engine/DLL work or waits remain possible; this log contains no process CPU samples to distinguish them. Even with the new mesh coverage, attributing the whole 87-second gap to adjacency would be incorrect.

The first successful Present was at **4.076 s** after proxy initialization. Backend load took 2.106 ms; the three Create9 spans took 1.276 s, 114.263 ms and 112.082 ms; CreateDevice took 33.520 ms. These are anchors, not full launch or menu-ready timings.

Backend ordinary Present calls themselves totaled **41.133 ms across 4,094 calls**, with a 0.325 ms maximum. The large gaps therefore appear between observed Present calls rather than inside the measured ordinary Present backend spans. This does not establish GPU time or exclude synchronization elsewhere.

The capture disturbance is material: 20 captured frames, 11.604 s of capture CPU work including 11.224 s of nested snapshots, and 12.955 s across 25 capture-adjacent frame intervals. These overlap and must not be added. Shader inspection was 0.550 s including nested work; separate shader dumping was 0.326 s. Loading wrapper tails totaled 0.275 s, a lower bound on tracing overhead. Explicit flush spans totaled about 3 ms, but formatting and buffered writes also occur inside capture work, so this is not the total logging-I/O cost.

## Cursor, window and focus observations

CreateDevice used a **1280×768 windowed** backbuffer, DISCARD swap effect, one backbuffer, no MSAA, automatic D24X8 depth and presentation interval ONE. No Reset records occur. The Present window override remained null.

At frames 0 and 1, window/foreground/render-thread focus/GUI-thread focus all identify HWND `000a0064`; the window is visible and not iconic. GUI capture is null. The window rectangle is `(1917,82)–(3203,882)`, client rectangle `(0,0)–(1280,768)`, and monitor rectangle `(0,0)–(5120,1440)`. The cursor clip changes from the virtual-desktop rectangle `(-1512,0)–(5120,1440)` to `(1920,111)–(3200,879)`, the client area in screen coordinates. No further polled context change was logged.

There are **224 GetCursorInfo records**: the first reports flags 1 at 4.077 s; frame 1 changes to flags 0 at 4.839 s, and the remaining polls retain flags 0. All logged cursor handles are null. No D3D cursor-API events or main-IAT SetCursor/SetCursorPos metrics were emitted despite those hooks being installed. No rate-suppression counts were reported.

This supports a hidden Win32 cursor in the sampled game state and no observed focus change after startup. It does not exclude a game-drawn cursor, a native host cursor, DLL-internal User32 calls, or changes during long polling gaps. A visual cursor symptom cannot be disproved by these records alone.

## Comparison with diagnostics 0.3

The earlier completed capture observed 357.045 s, 1,452 2D texture calls / 16.382 s, and 607,534 inflate calls / 7.109 s. This run observed 305.217 s, 2,091 2D calls / 13.976 s and 882,627 inflate calls / 9.307 s. Capture volume also differs sharply: about 42 MB versus 214 MB. The sessions have different work, duration, captures and instrumentation, with no common timestamped phase markers.

Consequently these totals are **not a controlled before/after benchmark** and establish neither faster loading nor a regression. The substantive improvement is diagnostic coverage: 0.3 did not measure mesh methods; 0.4 measured 21.287 s of adjacency and demonstrated recurring preparation patterns. The large presentation gap remained present in both sessions, and its full attribution is still incomplete.
