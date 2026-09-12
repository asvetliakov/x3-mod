# Loading attribution: `session-20260912-234117-216.log`

Source 46,661,564 bytes, sha256 `57a5c14a7970c13b68a899feff005b833cb1ac12a6c63bcd4535312d18d915ec`; QPC 10,000,000 Hz; 120 loading report windows; 40 import hooks; last Present at 150.558 s.

Engine probes: 12 of 12 sites active (crt_fgetc, find_wrapper, mesh_body, name_resolve, read_dispatch, resource_load, resource_open, resource_read, signature_check, sopen_helper, texture_body, texture_loader).

**The log contains no `profile_*` lines**: every section below has hooked time only. Launch with `--profile` (`X3M_PROFILE=1`) for sampled attribution.

## Gaps over 2.0 s

| # | Label | Gap | Interval | Hooked excl. | Unexplained | Samples | Stalls |
| ---: | --- | ---: | --- | ---: | ---: | ---: | ---: |
| 1 | menu load | 7.621 s | 2.735–11.413 s | 4.574 s | 3.047 s | none | 0 |
| 2 | save load | 38.528 s | 21.966–60.631 s | 27.104 s | 11.424 s | none | 2 |
| 3 | sector change | 5.368 s | 120.283–125.951 s | 3.487 s | 1.881 s | none | 1 |
| 4 | menu load | 7.344 s | 139.199–147.131 s | 4.233 s | 3.110 s | none | 0 |

## Gap 1: menu load (7.621 s, ends at frame 5)

Label evidence: 1017 GenerateAdjacency calls and 299,196,640 B of 2D texture-helper input match the main-menu work vector.
Interval 2.735–11.413 s, length 7.621 s; hooked exclusive 4.574 s over 9 report windows (2 straddling); unexplained 3.047 s.
**No profile data**: no `profile_report scope=delta` block overlaps this interval.

**Hooked time**

| Operation | Calls | Incl. s | Excl. s | Mean ms | Bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `inflate` | 167,720 | 2.414 | 2.414 | 0.014 | 0 |
| `D3DXCreateTextureFromFileInMemoryEx` | 467 | 0.478 | 0.478 | 1.023 | 299,196,640 |
| `ID3DXMesh::GenerateAdjacency` | 1,017 | 0.382 | 0.382 | 0.375 | 0 |
| `ReadFile` | 45,855 | 0.380 | 0.380 | 0.008 | 193,204,112 |
| `D3DXCreateMesh` | 1,017 | 0.248 | 0.248 | 0.244 | 0 |
| `ID3DXMesh::OptimizeInplace` | 1,017 | 0.202 | 0.202 | 0.199 | 0 |
| `D3DXCleanMesh` | 1,023 | 0.116 | 0.116 | 0.113 | 0 |
| `D3DXLoadSurfaceFromFileInMemory` | 4 | 0.094 | 0.094 | 23.451 | 4,918,784 |
| `CreateFileA` | 1,176 | 0.070 | 0.070 | 0.059 | 0 |
| `D3DXCreateEffect` | 10 | 0.059 | 0.059 | 5.900 | 309,928 |
| `CloseHandle` | 1,176 | 0.033 | 0.033 | 0.028 | 0 |
| `FindFirstFileA` | 1,289 | 0.030 | 0.030 | 0.024 | 0 |

**Engine probes (loading_probe deltas of the overlapping windows)**

| Probe | Calls | Exits | Incl. s | Excl. s | Mean ms | Max ms | Bytes | Extras |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `resource_load` | 1,280 | 1,280 | 3.215 | 0.036 | 2.511 | 212.2 | 0 | — |
| `resource_read` | 1,174 | 1,174 | 3.012 | 3.012 | 2.566 | 212.0 | 423,115,362 | plain=1, catalogue=1,173 |
| `texture_loader` | 503 | 503 | 2.716 | — | 5.399 | 111.9 | 0 | — |
| `mesh_body` | 1,017 | 1,017 | 0.710 | 0.710 | 0.698 | 57.4 | 0 | — |
| `resource_open` | 1,284 | 1,284 | 0.167 | 0.007 | 0.130 | 2.7 | 0 | loose=1, catalogue=1,175, failed=108 |
| `texture_body` | 13 | 13 | 0.157 | — | 12.084 | 51.8 | 0 | — |
| `sopen_helper` | 1,176 | 1,176 | 0.095 | 0.095 | 0.081 | 2.6 | 0 | — |
| `name_resolve` | 1,284 | 1,284 | 0.064 | 0.034 | 0.050 | 0.6 | 0 | hit=1,176, miss=108 |
| `find_wrapper` | 1,284 | 1,284 | 0.031 | 0.031 | 0.024 | 0.5 | 0 | caller 0x004e7947 1,284 |
| `read_dispatch` | 171,206 | 0 | 0.000 | 0.000 | 0.000 | 0.0 | 185,611,570 | plain=106, catalogue=171,100 |
| `crt_fgetc` | 9,104 | 0 | 0.000 | 0.000 | 0.000 | 0.0 | 0 | — |

**Write-side file APIs**

No write-side call (`CreateDirectoryA`, `DeleteFileA`, `MoveFileA`, `MoveFileExA`, `SetEndOfFile`, `WriteFile`) completed in this interval, against 1,173 catalogue reads: nothing can invalidate a catalogue handle or a negative name probe here.

**Instrumentation cost**

| Item | Calls | Seconds | Share of interval |
| --- | ---: | ---: | ---: |
| import wrapper tails (measured) | 228,765 | 0.018 | 0.2 % |
| timed probe stubs (bound, 282 ns/call) | 9,015 | 0.003 | 0.0 % |
| count-only probe stubs (bound, 143 ns/call) | 180,310 | 0.026 | 0.3 % |
| **total instrumentation** | 418,090 | **0.047** | 0.6 % |

Hooked inclusive time in the interval is 4.574 s; the wrapper tail is 0.4 % of it. The span between the two clock reads of a light row is inside the inclusive column, so that part of the envelope is already attributed to the operation it wraps.

**Candidates**

Hooked calls account for 4.574 s of the 7.621 s interval (60.0 %); 3.047 s is unexplained by any hook. No sampled attribution exists for this interval.

## Gap 2: save load (38.528 s, ends at frame 1119)

Label evidence: 14461803 gzread calls and 1 successful gzopen inside the gap (gzip save stream).
Interval 21.966–60.631 s, length 38.528 s; hooked exclusive 27.104 s over 13 report windows (2 straddling); unexplained 11.424 s.
**No profile data**: no `profile_report scope=delta` block overlaps this interval.

**Hooked time**

| Operation | Calls | Incl. s | Excl. s | Mean ms | Bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `CryptAcquireContextA` | 2,538 | 10.378 | 10.378 | 4.089 | 0 |
| `inflate` | 359,874 | 5.566 | 5.566 | 0.015 | 0 |
| `gzread` | 14,461,803 | 2.117 | 2.117 | 0.000 | 45,754,974 |
| `ID3DXMesh::GenerateAdjacency` | 3,342 | 1.491 | 1.491 | 0.446 | 0 |
| `CryptReleaseContext` | 846 | 0.967 | 0.967 | 1.143 | 0 |
| `D3DXCreateTextureFromFileInMemoryEx` | 660 | 0.866 | 0.866 | 1.312 | 646,944,232 |
| `ID3DXMesh::OptimizeInplace` | 3,342 | 0.794 | 0.794 | 0.238 | 0 |
| `D3DXCreateMesh` | 3,342 | 0.753 | 0.753 | 0.225 | 0 |
| `ReadFile` | 98,189 | 0.624 | 0.624 | 0.006 | 389,895,114 |
| `CryptImportKey` | 846 | 0.612 | 0.612 | 0.723 | 233,496 |
| `FindFirstFileA` | 2,371 | 0.606 | 0.606 | 0.256 | 0 |
| `D3DXCleanMesh` | 3,343 | 0.553 | 0.553 | 0.165 | 0 |

**Engine probes (loading_probe deltas of the overlapping windows)**

| Probe | Calls | Exits | Incl. s | Excl. s | Mean ms | Max ms | Bytes | Extras |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `signature_check` | 846 | 846 | 12.876 | 12.876 | 15.220 | 27.6 | 0 | — |
| `resource_load` | 2,358 | 2,358 | 7.928 | 0.075 | 3.362 | 235.1 | 0 | — |
| `resource_read` | 2,268 | 2,268 | 6.706 | 6.706 | 2.957 | 234.9 | 1,112,636,311 | plain=700, catalogue=1,568 |
| `texture_loader` | 706 | 706 | 4.241 | — | 6.007 | 55.9 | 0 | — |
| `mesh_body` | 3,342 | 3,342 | 2.875 | 2.875 | 0.860 | 49.2 | 0 | — |
| `resource_open` | 2,358 | 2,358 | 1.146 | 0.013 | 0.486 | 4.2 | 0 | loose=700, catalogue=1,568, failed=90 |
| `name_resolve` | 2,358 | 2,358 | 0.667 | 0.055 | 0.283 | 2.9 | 0 | hit=2,268, miss=90 |
| `find_wrapper` | 2,362 | 2,362 | 0.612 | 0.612 | 0.259 | 6.3 | 0 | caller 0x004e7947 2,358, caller 0x004adea1 2, caller 0x004adf29 1, caller 0x0049790c 1 |
| `sopen_helper` | 2,269 | 2,269 | 0.466 | 0.466 | 0.205 | 2.9 | 0 | — |
| `texture_body` | 29 | 29 | 0.370 | — | 12.767 | 140.3 | 0 | — |
| `read_dispatch` | 14,828,441 | 0 | 0.000 | 0.000 | 0.000 | 0.0 | 424,462,758 | plain=4,634, catalogue=362,004, gzhandle=14,461,803 |
| `crt_fgetc` | 17,816 | 0 | 0.000 | 0.000 | 0.000 | 0.0 | 0 | — |

**Write-side file APIs**

No write-side call (`CreateDirectoryA`, `DeleteFileA`, `MoveFileA`, `MoveFileExA`, `SetEndOfFile`, `WriteFile`) completed in this interval, against 1,568 catalogue reads: nothing can invalidate a catalogue handle or a negative name probe here.

**Instrumentation cost**

| Item | Calls | Seconds | Share of interval |
| --- | ---: | ---: | ---: |
| import wrapper tails (measured) | 14,967,507 | 1.200 | 3.1 % |
| timed probe stubs (bound, 282 ns/call) | 18,896 | 0.005 | 0.0 % |
| count-only probe stubs (bound, 143 ns/call) | 14,846,257 | 2.129 | 5.5 % |
| **total instrumentation** | 29,832,660 | **3.334** | 8.7 % |

Hooked inclusive time in the interval is 27.104 s; the wrapper tail is 4.4 % of it. The span between the two clock reads of a light row is inside the inclusive column, so that part of the envelope is already attributed to the operation it wraps.

**Candidates**

Hooked calls account for 27.104 s of the 38.528 s interval (70.3 %); 11.424 s is unexplained by any hook. No sampled attribution exists for this interval.

### Gap 2 stall 1: report stall 12.107 s

Interval 21.421–33.528 s, length 12.107 s; hooked exclusive 4.948 s over 1 report windows (0 straddling); unexplained 7.159 s.
**No profile data**: no `profile_report scope=delta` block overlaps this interval.

**Hooked time**

| Operation | Calls | Incl. s | Excl. s | Mean ms | Bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `inflate` | 153,159 | 2.535 | 2.535 | 0.017 | 0 |
| `gzread` | 14,351,234 | 2.101 | 2.101 | 0.000 | 45,302,235 |
| `ReadFile` | 38,931 | 0.188 | 0.188 | 0.005 | 162,667,745 |
| `CryptAcquireContextA` | 6 | 0.032 | 0.032 | 5.262 | 0 |
| `D3DXLoadSurfaceFromFileInMemory` | 3 | 0.030 | 0.030 | 10.150 | 1,077,573 |
| `CreateFileA` | 193 | 0.015 | 0.015 | 0.076 | 0 |
| `CloseHandle` | 193 | 0.014 | 0.014 | 0.074 | 0 |
| `FindFirstFileA` | 206 | 0.008 | 0.008 | 0.039 | 0 |
| `D3DXCreateTextureFromFileInMemoryEx` | 1 | 0.007 | 0.007 | 7.490 | 1,180,058 |
| `GetFileType` | 193 | 0.007 | 0.007 | 0.035 | 0 |
| `CryptHashData` | 2 | 0.004 | 0.004 | 1.771 | 2,337,865 |
| `CryptReleaseContext` | 2 | 0.002 | 0.002 | 1.212 | 0 |

**Engine probes (loading_probe deltas of the overlapping windows)**

| Probe | Calls | Exits | Incl. s | Excl. s | Mean ms | Max ms | Bytes | Extras |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `resource_load` | 204 | 204 | 2.974 | 0.015 | 14.578 | 235.1 | 0 | — |
| `resource_read` | 192 | 192 | 2.925 | 2.925 | 15.234 | 234.9 | 420,684,617 | plain=1, catalogue=191 |
| `signature_check` | 2 | 2 | 0.041 | 0.041 | 20.638 | 22.6 | 0 | — |
| `texture_body` | 4 | 4 | 0.037 | — | 9.310 | 33.8 | 0 | — |
| `resource_open` | 204 | 204 | 0.034 | 0.001 | 0.166 | 3.1 | 0 | loose=1, catalogue=191, failed=12 |
| `sopen_helper` | 193 | 193 | 0.022 | 0.022 | 0.113 | 2.9 | 0 | — |
| `name_resolve` | 204 | 204 | 0.011 | 0.003 | 0.056 | 0.3 | 0 | hit=192, miss=12 |
| `find_wrapper` | 205 | 205 | 0.008 | 0.008 | 0.041 | 0.8 | 0 | caller 0x004e7947 204, caller 0x0049790c 1 |
| `texture_loader` | 9 | 8 | 0.001 | — | 0.059 | 0.2 | 0 | — |
| `read_dispatch` | 14,504,965 | 0 | 0.000 | 0.000 | 0.000 | 0.0 | 206,735,028 | plain=3, catalogue=153,728, gzhandle=14,351,234 |
| `crt_fgetc` | 1,496 | 0 | 0.000 | 0.000 | 0.000 | 0.0 | 0 | — |

**Write-side file APIs**

No write-side call (`CreateDirectoryA`, `DeleteFileA`, `MoveFileA`, `MoveFileExA`, `SetEndOfFile`, `WriteFile`) completed in this interval, against 191 catalogue reads: nothing can invalidate a catalogue handle or a negative name probe here.

**Instrumentation cost**

| Item | Calls | Seconds | Share of interval |
| --- | ---: | ---: | ---: |
| import wrapper tails (measured) | 14,545,109 | 1.164 | 9.6 % |
| timed probe stubs (bound, 282 ns/call) | 1,217 | 0.000 | 0.0 % |
| count-only probe stubs (bound, 143 ns/call) | 14,506,461 | 2.080 | 17.2 % |
| **total instrumentation** | 29,052,787 | **3.245** | 26.8 % |

Hooked inclusive time in the interval is 4.948 s; the wrapper tail is 23.5 % of it. The span between the two clock reads of a light row is inside the inclusive column, so that part of the envelope is already attributed to the operation it wraps.

**Candidates**

Hooked calls account for 4.948 s of the 12.107 s interval (40.9 %); 7.159 s is unexplained by any hook. No sampled attribution exists for this interval.

### Gap 2 stall 2: report stall 16.758 s

Interval 42.575–59.333 s, length 16.758 s; hooked exclusive 14.969 s over 1 report windows (0 straddling); unexplained 1.789 s.
**No profile data**: no `profile_report scope=delta` block overlaps this interval.

**Hooked time**

| Operation | Calls | Incl. s | Excl. s | Mean ms | Bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `CryptAcquireContextA` | 2,532 | 10.346 | 10.346 | 4.086 | 0 |
| `CryptReleaseContext` | 844 | 0.964 | 0.964 | 1.143 | 0 |
| `CryptImportKey` | 844 | 0.610 | 0.610 | 0.723 | 232,944 |
| `FindFirstFileA` | 1,422 | 0.578 | 0.578 | 0.406 | 0 |
| `CryptVerifySignatureA` | 844 | 0.546 | 0.546 | 0.647 | 216,064 |
| `inflate` | 27,190 | 0.405 | 0.405 | 0.015 | 0 |
| `CreateFileA` | 1,400 | 0.243 | 0.243 | 0.173 | 0 |
| `xmlReadMemory` | 790 | 0.204 | 0.204 | 0.259 | 22,531,669 |
| `CryptHashData` | 844 | 0.200 | 0.200 | 0.236 | 24,994,790 |
| `ReadFile` | 12,226 | 0.145 | 0.145 | 0.012 | 37,356,759 |
| `GetFileType` | 1,400 | 0.144 | 0.144 | 0.103 | 0 |
| `CryptCreateHash` | 844 | 0.092 | 0.092 | 0.109 | 0 |

**Engine probes (loading_probe deltas of the overlapping windows)**

| Probe | Calls | Exits | Incl. s | Excl. s | Mean ms | Max ms | Bytes | Extras |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `signature_check` | 844 | 844 | 12.835 | 12.835 | 15.207 | 27.6 | 0 | — |
| `resource_load` | 1,412 | 1,412 | 1.672 | 0.038 | 1.184 | 45.4 | 0 | — |
| `resource_open` | 1,412 | 1,412 | 1.024 | 0.007 | 0.725 | 4.2 | 0 | loose=699, catalogue=701, failed=12 |
| `name_resolve` | 1,412 | 1,412 | 0.626 | 0.044 | 0.444 | 2.9 | 0 | hit=1,400, miss=12 |
| `resource_read` | 1,400 | 1,400 | 0.611 | 0.611 | 0.436 | 43.8 | 113,425,778 | plain=699, catalogue=701 |
| `find_wrapper` | 1,415 | 1,415 | 0.582 | 0.582 | 0.411 | 6.3 | 0 | caller 0x004e7947 1,412, caller 0x004adea1 2, caller 0x004adf29 1 |
| `texture_loader` | 47 | 48 | 0.494 | — | 10.520 | 49.4 | 0 | — |
| `sopen_helper` | 1,400 | 1,400 | 0.390 | 0.390 | 0.278 | 2.7 | 0 | — |
| `mesh_body` | 301 | 301 | 0.197 | 0.197 | 0.655 | 13.9 | 0 | — |
| `texture_body` | 3 | 2 | 0.012 | — | 3.979 | 9.1 | 0 | — |
| `read_dispatch` | 141,952 | 0 | 0.000 | 0.000 | 0.000 | 0.0 | 31,790,339 | plain=4,631, catalogue=26,752, gzhandle=110,569 |
| `crt_fgetc` | 11,144 | 0 | 0.000 | 0.000 | 0.000 | 0.0 | 0 | — |

**Write-side file APIs**

No write-side call (`CreateDirectoryA`, `DeleteFileA`, `MoveFileA`, `MoveFileExA`, `SetEndOfFile`, `WriteFile`) completed in this interval, against 701 catalogue reads: nothing can invalidate a catalogue handle or a negative name probe here.

**Instrumentation cost**

| Item | Calls | Seconds | Share of interval |
| --- | ---: | ---: | ---: |
| import wrapper tails (measured) | 176,949 | 0.015 | 0.1 % |
| timed probe stubs (bound, 282 ns/call) | 9,646 | 0.003 | 0.0 % |
| count-only probe stubs (bound, 143 ns/call) | 153,096 | 0.022 | 0.1 % |
| **total instrumentation** | 339,691 | **0.040** | 0.2 % |

Hooked inclusive time in the interval is 14.969 s; the wrapper tail is 0.1 % of it. The span between the two clock reads of a light row is inside the inclusive column, so that part of the envelope is already attributed to the operation it wraps.

**Candidates**

Hooked calls account for 14.969 s of the 16.758 s interval (89.3 %); 1.789 s is unexplained by any hook. No sampled attribution exists for this interval.

## Gap 3: sector change (5.368 s, ends at frame 7323)

Label evidence: 2232 GenerateAdjacency calls with 62,652,551 B of texture input after an earlier labelled phase.
Interval 120.283–125.951 s, length 5.368 s; hooked exclusive 3.487 s over 6 report windows (2 straddling); unexplained 1.881 s.
**No profile data**: no `profile_report scope=delta` block overlaps this interval.

**Hooked time**

| Operation | Calls | Incl. s | Excl. s | Mean ms | Bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `inflate` | 80,643 | 1.242 | 1.242 | 0.015 | 0 |
| `ID3DXMesh::GenerateAdjacency` | 2,232 | 0.714 | 0.714 | 0.320 | 0 |
| `D3DXCreateMesh` | 2,232 | 0.491 | 0.491 | 0.220 | 0 |
| `ID3DXMesh::OptimizeInplace` | 2,232 | 0.397 | 0.397 | 0.178 | 0 |
| `D3DXCleanMesh` | 2,233 | 0.272 | 0.272 | 0.122 | 0 |
| `D3DXCreateTextureFromFileInMemoryEx` | 115 | 0.207 | 0.207 | 1.804 | 62,652,551 |
| `ReadFile` | 20,670 | 0.137 | 0.137 | 0.007 | 84,300,800 |
| `CreateFileA` | 156 | 0.010 | 0.010 | 0.065 | 0 |
| `CloseHandle` | 156 | 0.005 | 0.005 | 0.031 | 0 |
| `FindFirstFileA` | 184 | 0.005 | 0.005 | 0.027 | 0 |
| `D3DXCreateEffect` | 2 | 0.003 | 0.003 | 1.647 | 28,932 |
| `GetFileType` | 156 | 0.003 | 0.003 | 0.018 | 0 |

**Engine probes (loading_probe deltas of the overlapping windows)**

| Probe | Calls | Exits | Incl. s | Excl. s | Mean ms | Max ms | Bytes | Extras |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `resource_load` | 183 | 183 | 1.506 | 0.005 | 8.230 | 201.5 | 0 | — |
| `resource_read` | 156 | 156 | 1.480 | 1.480 | 9.484 | 201.2 | 194,682,493 | catalogue=156 |
| `mesh_body` | 2,232 | 2,232 | 1.406 | 1.406 | 0.630 | 48.1 | 0 | — |
| `texture_loader` | 124 | 124 | 0.681 | — | 5.490 | 74.2 | 0 | — |
| `texture_body` | 9 | 9 | 0.133 | — | 14.748 | 16.3 | 0 | — |
| `resource_open` | 183 | 183 | 0.021 | 0.001 | 0.116 | 0.4 | 0 | catalogue=156, failed=27 |
| `sopen_helper` | 156 | 156 | 0.013 | 0.013 | 0.084 | 0.3 | 0 | — |
| `name_resolve` | 183 | 183 | 0.007 | 0.002 | 0.038 | 0.2 | 0 | hit=156, miss=27 |
| `find_wrapper` | 183 | 183 | 0.005 | 0.005 | 0.027 | 0.1 | 0 | caller 0x004e7947 183 |
| `read_dispatch` | 81,099 | 0 | 0.000 | 0.000 | 0.000 | 0.0 | 83,343,957 | catalogue=81,099 |
| `crt_fgetc` | 1,152 | 0 | 0.000 | 0.000 | 0.000 | 0.0 | 0 | — |

**Write-side file APIs**

No write-side call (`CreateDirectoryA`, `DeleteFileA`, `MoveFileA`, `MoveFileExA`, `SetEndOfFile`, `WriteFile`) completed in this interval, against 156 catalogue reads: nothing can invalidate a catalogue handle or a negative name probe here.

**Instrumentation cost**

| Item | Calls | Seconds | Share of interval |
| --- | ---: | ---: | ---: |
| import wrapper tails (measured) | 111,768 | 0.009 | 0.2 % |
| timed probe stubs (bound, 282 ns/call) | 3,409 | 0.001 | 0.0 % |
| count-only probe stubs (bound, 143 ns/call) | 82,251 | 0.012 | 0.2 % |
| **total instrumentation** | 197,428 | **0.022** | 0.4 % |

Hooked inclusive time in the interval is 3.487 s; the wrapper tail is 0.3 % of it. The span between the two clock reads of a light row is inside the inclusive column, so that part of the envelope is already attributed to the operation it wraps.

**Candidates**

Hooked calls account for 3.487 s of the 5.368 s interval (65.0 %); 1.881 s is unexplained by any hook. No sampled attribution exists for this interval.

### Gap 3 stall 1: report stall 2.049 s

Interval 119.595–121.644 s, length 2.049 s; hooked exclusive 0.833 s over 1 report windows (0 straddling); unexplained 1.216 s.
**No profile data**: no `profile_report scope=delta` block overlaps this interval.

**Hooked time**

| Operation | Calls | Incl. s | Excl. s | Mean ms | Bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `inflate` | 46,144 | 0.759 | 0.759 | 0.016 | 0 |
| `ReadFile` | 11,629 | 0.066 | 0.066 | 0.006 | 47,431,680 |
| `CreateFileA` | 28 | 0.002 | 0.002 | 0.085 | 0 |
| `D3DXCreateMesh` | 5 | 0.001 | 0.001 | 0.255 | 0 |
| `CloseHandle` | 28 | 0.001 | 0.001 | 0.044 | 0 |
| `FindFirstFileA` | 28 | 0.001 | 0.001 | 0.041 | 0 |
| `GetFileType` | 28 | 0.001 | 0.001 | 0.026 | 0 |
| `ID3DXMesh::OptimizeInplace` | 5 | 0.000 | 0.000 | 0.035 | 0 |
| `SetFilePointer` | 84 | 0.000 | 0.000 | 0.001 | 0 |
| `ID3DXMesh::GenerateAdjacency` | 5 | 0.000 | 0.000 | 0.020 | 0 |
| `D3DXCleanMesh` | 5 | 0.000 | 0.000 | 0.013 | 0 |
| `inflateEnd` | 28 | 0.000 | 0.000 | 0.002 | 0 |

**Engine probes (loading_probe deltas of the overlapping windows)**

| Probe | Calls | Exits | Incl. s | Excl. s | Mean ms | Max ms | Bytes | Extras |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `resource_load` | 28 | 28 | 0.893 | 0.001 | 31.909 | 201.5 | 0 | — |
| `resource_read` | 28 | 28 | 0.887 | 0.887 | 31.673 | 201.2 | 131,131,466 | catalogue=28 |
| `resource_open` | 28 | 28 | 0.005 | 0.000 | 0.188 | 0.4 | 0 | catalogue=28 |
| `sopen_helper` | 28 | 28 | 0.003 | 0.003 | 0.113 | 0.3 | 0 | — |
| `name_resolve` | 28 | 28 | 0.002 | 0.001 | 0.064 | 0.1 | 0 | hit=28 |
| `find_wrapper` | 28 | 28 | 0.001 | 0.001 | 0.043 | 0.1 | 0 | caller 0x004e7947 28 |
| `mesh_body` | 5 | 5 | 0.000 | 0.000 | 0.075 | 0.2 | 0 | — |
| `read_dispatch` | 46,228 | 0 | 0.000 | 0.000 | 0.000 | 0.0 | 47,251,764 | catalogue=46,228 |
| `crt_fgetc` | 224 | 0 | 0.000 | 0.000 | 0.000 | 0.0 | 0 | — |

**Write-side file APIs**

No write-side call (`CreateDirectoryA`, `DeleteFileA`, `MoveFileA`, `MoveFileExA`, `SetEndOfFile`, `WriteFile`) completed in this interval, against 28 catalogue reads: nothing can invalidate a catalogue handle or a negative name probe here.

**Instrumentation cost**

| Item | Calls | Seconds | Share of interval |
| --- | ---: | ---: | ---: |
| import wrapper tails (measured) | 58,045 | 0.005 | 0.2 % |
| timed probe stubs (bound, 282 ns/call) | 173 | 0.000 | 0.0 % |
| count-only probe stubs (bound, 143 ns/call) | 46,452 | 0.007 | 0.3 % |
| **total instrumentation** | 104,670 | **0.011** | 0.6 % |

Hooked inclusive time in the interval is 0.833 s; the wrapper tail is 0.6 % of it. The span between the two clock reads of a light row is inside the inclusive column, so that part of the envelope is already attributed to the operation it wraps.

**Candidates**

Hooked calls account for 0.833 s of the 2.049 s interval (40.7 %); 1.216 s is unexplained by any hook. No sampled attribution exists for this interval.

## Gap 4: menu load (7.344 s, ends at frame 8777)

Label evidence: 1017 GenerateAdjacency calls and 298,138,122 B of 2D texture-helper input match the main-menu work vector.
Interval 139.199–147.131 s, length 7.344 s; hooked exclusive 4.233 s over 8 report windows (2 straddling); unexplained 3.110 s.
**No profile data**: no `profile_report scope=delta` block overlaps this interval.

**Hooked time**

| Operation | Calls | Incl. s | Excl. s | Mean ms | Bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `inflate` | 166,017 | 2.433 | 2.433 | 0.015 | 0 |
| `D3DXCreateTextureFromFileInMemoryEx` | 459 | 0.435 | 0.435 | 0.949 | 298,138,122 |
| `ID3DXMesh::GenerateAdjacency` | 1,017 | 0.359 | 0.359 | 0.353 | 0 |
| `ReadFile` | 43,380 | 0.304 | 0.304 | 0.007 | 183,541,385 |
| `D3DXCreateMesh` | 1,017 | 0.223 | 0.223 | 0.220 | 0 |
| `ID3DXMesh::OptimizeInplace` | 1,017 | 0.188 | 0.188 | 0.185 | 0 |
| `D3DXCleanMesh` | 1,023 | 0.100 | 0.100 | 0.098 | 0 |
| `CreateFileA` | 584 | 0.062 | 0.062 | 0.106 | 0 |
| `D3DXLoadSurfaceFromFileInMemory` | 3 | 0.048 | 0.048 | 15.974 | 2,480,817 |
| `CloseHandle` | 584 | 0.019 | 0.019 | 0.033 | 0 |
| `xmlReadMemory` | 2 | 0.019 | 0.019 | 9.724 | 3,688,819 |
| `FindFirstFileA` | 686 | 0.016 | 0.016 | 0.024 | 0 |

**Engine probes (loading_probe deltas of the overlapping windows)**

| Probe | Calls | Exits | Incl. s | Excl. s | Mean ms | Max ms | Bytes | Extras |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `resource_load` | 683 | 683 | 3.082 | 0.021 | 4.512 | 220.3 | 0 | — |
| `resource_read` | 584 | 584 | 2.939 | 2.939 | 5.032 | 220.1 | 409,289,073 | plain=1, catalogue=583 |
| `texture_loader` | 489 | 489 | 2.684 | — | 5.489 | 110.6 | 0 | — |
| `mesh_body` | 1,017 | 1,017 | 0.654 | 0.654 | 0.643 | 54.1 | 0 | — |
| `resource_open` | 683 | 683 | 0.122 | 0.004 | 0.179 | 11.3 | 0 | loose=1, catalogue=583, failed=99 |
| `texture_body` | 12 | 12 | 0.106 | — | 8.872 | 52.7 | 0 | — |
| `sopen_helper` | 584 | 584 | 0.075 | 0.075 | 0.128 | 11.3 | 0 | — |
| `name_resolve` | 683 | 683 | 0.044 | 0.028 | 0.064 | 0.5 | 0 | hit=584, miss=99 |
| `find_wrapper` | 683 | 683 | 0.016 | 0.016 | 0.024 | 0.5 | 0 | caller 0x004e7947 683 |
| `read_dispatch` | 167,747 | 0 | 0.000 | 0.000 | 0.000 | 0.0 | 180,073,005 | plain=106, catalogue=167,641 |
| `crt_fgetc` | 4,496 | 0 | 0.000 | 0.000 | 0.000 | 0.0 | 0 | — |

**Write-side file APIs**

No write-side call (`CreateDirectoryA`, `DeleteFileA`, `MoveFileA`, `MoveFileExA`, `SetEndOfFile`, `WriteFile`) completed in this interval, against 583 catalogue reads: nothing can invalidate a catalogue handle or a negative name probe here.

**Instrumentation cost**

| Item | Calls | Seconds | Share of interval |
| --- | ---: | ---: | ---: |
| import wrapper tails (measured) | 219,262 | 0.018 | 0.2 % |
| timed probe stubs (bound, 282 ns/call) | 5,418 | 0.002 | 0.0 % |
| count-only probe stubs (bound, 143 ns/call) | 172,243 | 0.025 | 0.3 % |
| **total instrumentation** | 396,923 | **0.044** | 0.6 % |

Hooked inclusive time in the interval is 4.233 s; the wrapper tail is 0.4 % of it. The span between the two clock reads of a light row is inside the inclusive column, so that part of the envelope is already attributed to the operation it wraps.

**Candidates**

Hooked calls account for 4.233 s of the 7.344 s interval (57.6 %); 3.110 s is unexplained by any hook. No sampled attribution exists for this interval.

## Limits

- Hooked seconds are completion deltas of report windows overlapping the interval; only the exclusive column may be added and it is a lower bound.
- Profiler delta blocks are attributed by overlap of their report interval; a block straddling a boundary counts in both neighbours and is reported as straddling.
- Per-block leaf/frame/pair tables are truncated to the top 48/48/32 rows, so function sums are lower bounds; per-thread module splits and sample totals are exact.
- Frame RVAs are return addresses; inclusive-by-frame shares attribute DLL and wait time to the first main-executable frame, not to a full call stack.
- Gap labels are heuristics over hooked counts (label_gap); no phase marker exists in the log.
- The probe exclusive column subtracts only the timed probe children of the documented nesting (analyze_iteration08_loading.PROBE_CHILDREN); it still contains the hooked imports and the CRT work below the site, and it is left blank for the two texture sites whose resource_load child has eleven callers.
- Probe stub cost is a bound from the fixture envelope components, not a per-site measurement; the wrapper tail column is measured.
- Write-side paths are logged once each (first 16) and are placed by their report window, so a path may be listed one window away from the interval that made the call.
- Function labels come from hand-maintained notes; an unlabelled function is merely undocumented, not unimportant.
