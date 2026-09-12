# Loading attribution: `session-20260912-160404-1632.log`

Source 154,442,312 bytes, sha256 `ca149c414df62aabacbfecfb3ab0fdc586d182c8a23aec918f14293a169e0d47`; QPC 10,000,000 Hz; 332 loading report windows; 19 import hooks; last Present at 473.825 s.

Profiler: 94 delta blocks from 5.004 to 470.574 s, interval 2000 µs, report period 5 s, 76 modules, main base 0x00400000. Symbols: 269/269 RVAs resolved (verification/results/loading-profile-run1/symbols.json); 84 labelled function starts.

## Gaps over 2.0 s

| # | Label | Gap | Interval | Hooked excl. | Unexplained | Samples | Stalls |
| ---: | --- | ---: | --- | ---: | ---: | ---: | ---: |
| 1 | unlabelled | 2.965 s | 4.791–9.415 s | 0.579 s | 2.386 s | 10,442 | 1 |
| 2 | menu load | 64.063 s | 8.507–73.716 s | 39.813 s | 24.250 s | 192,149 | 4 |
| 3 | save load | 86.758 s | 124.197–211.015 s | 41.239 s | 45.519 s | 262,812 | 4 |
| 4 | sector change | 24.126 s | 383.924–409.065 s | 14.710 s | 9.416 s | 107,057 | 3 |
| 5 | menu load | 32.826 s | 437.990–471.572 s | 8.912 s | 23.914 s | 121,334 | 2 |

## Gap 1: unlabelled (2.965 s, ends at frame 2)

Label evidence: 42 GenerateAdjacency calls, 6,921,664 B texture input, 0 gzread calls: no rule matches.
Interval 4.791–9.415 s, length 2.965 s; hooked exclusive 0.579 s over 3 report windows (2 straddling); unexplained 2.386 s.
Samples 10,442 in 2 delta blocks (2 straddling), 10.003 s covered, 1044 samples/s, 301 ticks/s per thread, 0 dropped; sampler tick 614 µs mean / 37786 µs max, busy 24.2 % of the covered time.

**Hooked time**

| Operation | Calls | Incl. s | Excl. s | Mean ms | Bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `CreateFileA` | 626 | 0.180 | 0.180 | 0.288 | 0 |
| `D3DXLoadSurfaceFromFileInMemory` | 4 | 0.115 | 0.115 | 28.855 | 4,918,784 |
| `ReadFile` | 2,838 | 0.090 | 0.090 | 0.032 | 19,148,688 |
| `D3DXCreateEffect` | 1 | 0.055 | 0.055 | 55.033 | 51,720 |
| `D3DXCreateTextureFromFileInMemoryEx` | 18 | 0.040 | 0.040 | 2.202 | 6,921,664 |
| `inflate` | 2,721 | 0.035 | 0.035 | 0.013 | 0 |
| `xmlReadMemory` | 2 | 0.027 | 0.027 | 13.442 | 3,688,819 |
| `FindFirstFileA` | 672 | 0.023 | 0.023 | 0.034 | 0 |
| `ID3DXMesh::GenerateAdjacency` | 42 | 0.003 | 0.003 | 0.080 | 0 |
| `ID3DXMesh::OptimizeInplace` | 42 | 0.003 | 0.003 | 0.066 | 0 |
| `D3DXCreateCubeTextureFromFileInMemoryEx` | 2 | 0.002 | 0.002 | 1.234 | 1,048,816 |
| `D3DXCreateMesh` | 42 | 0.002 | 0.002 | 0.046 | 0 |

**Sampled attribution: per-thread module split**

| Slot | TID | Samples | x3ap | ntdll | wine | d3dx | zlib | xml | proxy | other | Start |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 0 | 1260 | 3,011 | 228 | 416 | 2,189 | 39 | 10 | 4 | 53 | 72 | 0:0x112ead |
| 1 | 2984 | 2,087 | 0 | 2,087 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xb220 |
| 2 | 1432 | 1,824 | 0 | 0 | 1,824 | 0 | 0 | 0 | 0 | 0 | 53:0xc780 |
| 3 | 844 | 1,489 | 0 | 1,483 | 6 | 0 | 0 | 0 | 0 | 0 | 49:0x38490 |
| 4 | 1172 | 1,489 | 0 | 0 | 1,489 | 0 | 0 | 0 | 0 | 0 | 4:0xd0f0 |
| 5 | 2332 | 542 | 0 | 542 | 0 | 0 | 0 | 0 | 0 | 0 | 20:0xe740 |

All threads: x3ap 2.2 %, ntdll 43.4 %, wine 52.7 %, d3dx 0.4 %, zlib 0.1 %, xml 0.0 %, proxy 0.5 %, other 0.7 %.

**Top functions (leaf samples by containing function)**

Shares are of the engine thread (slot 0, 3,011 samples); the other sampled threads are idle Wine/audio/input waiters.

| Function start | Ghidra name | Label | Self (engine) | Incl. frame (engine) | Busiest slot | Hooked APIs in interval |
| --- | --- | --- | ---: | ---: | ---: | --- |
| `0x00523fe4` | fastzero_I | — | 1.3 % (40) | 1.3 % (40) | slot 0 1.3 % | — |
| `0x004ec490` | FUN_004ec490 | — | 0.9 % (27) | 1.1 % (33) | slot 0 0.9 % | — |
| `0x0046e400` | FUN_0046e400 | — | 0.7 % (20) | 0.7 % (20) | slot 0 0.7 % | — |
| `0x004f0110` | FUN_004f0110 | — | 0.6 % (18) | 0.7 % (21) | slot 0 0.6 % | — |
| `0x00480830` | FUN_00480830 | — | 0.4 % (12) | 0.4 % (12) | slot 0 0.4 % | — |
| `0x00517a73` | ___ascii_stricmp | — | 0.1 % (3) | 0.2 % (5) | slot 0 0.1 % | — |
| `0x00515810` | _memcpy | — | 0.1 % (3) | 0.1 % (3) | slot 0 0.1 % | — |
| `0x004de060` | FUN_004de060 | — | 0.0 % (0) | 30.4 % (915) | — | — |
| `0x004b3860` | FUN_004b3860 | — | 0.0 % (0) | 14.1 % (426) | — | — |
| `0x0042fe10` | FUN_0042fe10 | — | 0.0 % (0) | 12.0 % (360) | — | — |
| `0x004d3620` | FUN_004d3620 | — | 0.0 % (0) | 5.9 % (179) | — | — |
| `0x0050e1b0` | _free | — | 0.0 % (0) | 3.4 % (102) | — | — |
| `0x005112c4` | _malloc | — | 0.0 % (0) | 1.7 % (51) | — | — |
| `0x00517d8f` | FUN_00517d8f | — | 0.0 % (0) | 1.3 % (38) | — | — |
| `0x004b8ab0` | FUN_004b8ab0 | — | 0.0 % (0) | 1.2 % (37) | — | — |
| `0x004b8920` | FUN_004b8920 | — | 0.0 % (0) | 1.1 % (32) | — | — |
| `0x004d8470` | FUN_004d8470 | — | 0.0 % (0) | 1.0 % (31) | — | — |
| `0x004dac90` | FUN_004dac90 | — | 0.0 % (0) | 0.9 % (28) | — | — |
| `0x0051f464` | FUN_0051f464 | — | 0.0 % (0) | 0.9 % (27) | — | — |
| `0x00519434` | FUN_00519434 | — | 0.0 % (0) | 0.9 % (26) | — | — |
| `0x00527869` | FUN_00527869 | — | 0.0 % (0) | 0.7 % (21) | — | — |
| `0x004efcc0` | FUN_004efcc0 | handle map insert | 0.0 % (0) | 0.7 % (21) | — | — |
| `0x0052403b` | __VEC_memzero | — | 0.0 % (0) | 0.6 % (18) | — | — |
| `0x0051b6bb` | FUN_0051b6bb | — | 0.0 % (0) | 0.5 % (16) | — | — |
| `0x004cf460` | FUN_004cf460 | — | 0.0 % (0) | 0.4 % (13) | — | — |
| `0x004d8f10` | FUN_004d8f10 | — | 0.0 % (0) | 0.4 % (13) | — | — |
| `0x00511f77` | FUN_00511f77 | — | 0.0 % (0) | 0.3 % (10) | — | — |
| `0x004b89f0` | FUN_004b89f0 | — | 0.0 % (0) | 0.3 % (9) | — | — |
| `0x004d56d0` | FUN_004d56d0 | — | 0.0 % (0) | 0.3 % (8) | — | — |
| `0x00517bcb` | __isleadbyte_l | — | 0.0 % (0) | 0.2 % (7) | — | — |
| `0x00524aa7` | ___lock_fhandle | — | 0.0 % (0) | 0.2 % (6) | — | — |
| `0x004991a0` | FUN_004991a0 | — | 0.0 % (0) | 0.2 % (5) | — | — |
| `0x0051fce3` | __getdrive | — | 0.0 % (0) | 0.1 % (3) | — | — |
| `0x00524b47` | FUN_00524b47 | — | 0.0 % (0) | 0.1 % (3) | — | — |
| `0x0050f786` | _memcpy_s | — | 0.0 % (0) | 0.1 % (2) | — | — |
| `0x0051fc2a` | ___dtoxmode | — | 0.0 % (0) | 0.1 % (2) | — | — |
| `0x004dced0` | FUN_004dced0 | — | 0.0 % (0) | 0.1 % (2) | — | — |
| `0x004dd2c0` | FUN_004dd2c0 | — | 0.0 % (0) | 0.1 % (2) | — | — |
| `0x0040e1a0` | FUN_0040e1a0 | — | 0.0 % (0) | 0.1 % (2) | — | — |
| `0x00518de0` | _memset | — | 0.0 % (0) | 0.0 % (1) | — | — |
| (no main-module frame) | | | | 74.6 % (7,791) | | |

**Top caller pairs**

| Slot | Frame | Caller | Samples |
| ---: | --- | --- | ---: |
| 0 | `0xde146` FUN_004de060 | `0xd598e` FUN_004d56d0 | 915 |
| 0 | `0x3002d` FUN_0042fe10 | `0xb38c7` FUN_004b3860 | 297 |
| 0 | `0xd3a7f` FUN_004d3620 | `0xdae13` FUN_004dac90 | 179 |
| 0 | `0xb38c7` FUN_004b3860 | `0x2741` Catch@00402741 | 116 |
| 0 | `0x10e21e` _free | `0x10e23d` _free | 83 |
| 0 | `0x3002d` FUN_0042fe10 | `0x3002d` FUN_0042fe10 | 51 |
| 0 | `0x11133e` _malloc | `0xefd0e` FUN_004efcc0 (handle map insert) | 39 |
| 0 | `0x117da2` FUN_00517d8f | `0xf371d` FUN_004f3510 (texture-file wrapper (.jpg, .tga)) | 37 |
| 0 | `0xd8494` FUN_004d8470 | `0x2741` Catch@00402741 | 31 |
| 0 | `0x11f665` FUN_0051f464 | `0x11f665` FUN_0051f464 | 27 |
| 0 | `0x124004` fastzero_I | `0x124071` __VEC_memzero | 23 |
| 0 | `0xdb046` FUN_004dac90 | `0xf960f` FUN_004f95d0 | 21 |

**Candidates**

Hooked calls account for 0.579 s of the 2.965 s interval (19.5 %); 2.386 s is unexplained by any hook. The sampler recorded 10,442 samples over 10.003 s of report coverage (1044 samples/s, 301 ticks/s per thread); the dominant leaf module is wine at 52.7 %. 2 of 2 delta blocks straddle the interval boundary and carry samples from outside it. Function `0x00523fe4` (fastzero_I) holds 1.3 % of the engine thread's samples as leaf (1.3 % of slot 0's samples) and 1.3 % as first main-module frame; it is not a known routine. Function `0x004ec490` (FUN_004ec490) holds 0.9 % of the engine thread's samples as leaf (0.9 % of slot 0's samples) and 1.1 % as first main-module frame; it is not a known routine. Function `0x0046e400` (FUN_0046e400) holds 0.7 % of the engine thread's samples as leaf (0.7 % of slot 0's samples) and 0.7 % as first main-module frame; it is not a known routine.

### Gap 1 stall 1: report stall 3.648 s

Interval 4.107–7.755 s, length 3.648 s; hooked exclusive 0.094 s over 1 report windows (0 straddling); unexplained 3.554 s.
Samples 10,442 in 2 delta blocks (2 straddling), 10.003 s covered, 1044 samples/s, 301 ticks/s per thread, 0 dropped; sampler tick 614 µs mean / 37786 µs max, busy 24.2 % of the covered time.

**Hooked time**

| Operation | Calls | Incl. s | Excl. s | Mean ms | Bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `D3DXLoadSurfaceFromFileInMemory` | 1 | 0.057 | 0.057 | 56.916 | 2,437,967 |
| `CreateFileA` | 15 | 0.027 | 0.027 | 1.772 | 0 |
| `D3DXCreateTextureFromFileInMemoryEx` | 6 | 0.003 | 0.003 | 0.424 | 9,880 |
| `D3DXCreateCubeTextureFromFileInMemoryEx` | 2 | 0.002 | 0.002 | 1.234 | 1,048,816 |
| `inflate` | 110 | 0.002 | 0.002 | 0.022 | 0 |
| `ReadFile` | 74 | 0.002 | 0.002 | 0.029 | 2,632,704 |
| `FindFirstFileA` | 18 | 0.001 | 0.001 | 0.039 | 0 |
| `SetFilePointer` | 45 | 0.000 | 0.000 | 0.001 | 0 |

**Sampled attribution: per-thread module split**

| Slot | TID | Samples | x3ap | ntdll | wine | d3dx | zlib | xml | proxy | other | Start |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 0 | 1260 | 3,011 | 228 | 416 | 2,189 | 39 | 10 | 4 | 53 | 72 | 0:0x112ead |
| 1 | 2984 | 2,087 | 0 | 2,087 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xb220 |
| 2 | 1432 | 1,824 | 0 | 0 | 1,824 | 0 | 0 | 0 | 0 | 0 | 53:0xc780 |
| 3 | 844 | 1,489 | 0 | 1,483 | 6 | 0 | 0 | 0 | 0 | 0 | 49:0x38490 |
| 4 | 1172 | 1,489 | 0 | 0 | 1,489 | 0 | 0 | 0 | 0 | 0 | 4:0xd0f0 |
| 5 | 2332 | 542 | 0 | 542 | 0 | 0 | 0 | 0 | 0 | 0 | 20:0xe740 |

All threads: x3ap 2.2 %, ntdll 43.4 %, wine 52.7 %, d3dx 0.4 %, zlib 0.1 %, xml 0.0 %, proxy 0.5 %, other 0.7 %.

**Top functions (leaf samples by containing function)**

Shares are of the engine thread (slot 0, 3,011 samples); the other sampled threads are idle Wine/audio/input waiters.

| Function start | Ghidra name | Label | Self (engine) | Incl. frame (engine) | Busiest slot | Hooked APIs in interval |
| --- | --- | --- | ---: | ---: | ---: | --- |
| `0x00523fe4` | fastzero_I | — | 1.3 % (40) | 1.3 % (40) | slot 0 1.3 % | — |
| `0x004ec490` | FUN_004ec490 | — | 0.9 % (27) | 1.1 % (33) | slot 0 0.9 % | — |
| `0x0046e400` | FUN_0046e400 | — | 0.7 % (20) | 0.7 % (20) | slot 0 0.7 % | — |
| `0x004f0110` | FUN_004f0110 | — | 0.6 % (18) | 0.7 % (21) | slot 0 0.6 % | — |
| `0x00480830` | FUN_00480830 | — | 0.4 % (12) | 0.4 % (12) | slot 0 0.4 % | — |
| `0x00517a73` | ___ascii_stricmp | — | 0.1 % (3) | 0.2 % (5) | slot 0 0.1 % | — |
| `0x00515810` | _memcpy | — | 0.1 % (3) | 0.1 % (3) | slot 0 0.1 % | — |
| `0x004de060` | FUN_004de060 | — | 0.0 % (0) | 30.4 % (915) | — | — |
| `0x004b3860` | FUN_004b3860 | — | 0.0 % (0) | 14.1 % (426) | — | — |
| `0x0042fe10` | FUN_0042fe10 | — | 0.0 % (0) | 12.0 % (360) | — | — |
| `0x004d3620` | FUN_004d3620 | — | 0.0 % (0) | 5.9 % (179) | — | — |
| `0x0050e1b0` | _free | — | 0.0 % (0) | 3.4 % (102) | — | — |
| `0x005112c4` | _malloc | — | 0.0 % (0) | 1.7 % (51) | — | — |
| `0x00517d8f` | FUN_00517d8f | — | 0.0 % (0) | 1.3 % (38) | — | — |
| `0x004b8ab0` | FUN_004b8ab0 | — | 0.0 % (0) | 1.2 % (37) | — | — |
| `0x004b8920` | FUN_004b8920 | — | 0.0 % (0) | 1.1 % (32) | — | — |
| `0x004d8470` | FUN_004d8470 | — | 0.0 % (0) | 1.0 % (31) | — | — |
| `0x004dac90` | FUN_004dac90 | — | 0.0 % (0) | 0.9 % (28) | — | — |
| `0x0051f464` | FUN_0051f464 | — | 0.0 % (0) | 0.9 % (27) | — | — |
| `0x00519434` | FUN_00519434 | — | 0.0 % (0) | 0.9 % (26) | — | — |
| `0x00527869` | FUN_00527869 | — | 0.0 % (0) | 0.7 % (21) | — | — |
| `0x004efcc0` | FUN_004efcc0 | handle map insert | 0.0 % (0) | 0.7 % (21) | — | — |
| `0x0052403b` | __VEC_memzero | — | 0.0 % (0) | 0.6 % (18) | — | — |
| `0x0051b6bb` | FUN_0051b6bb | — | 0.0 % (0) | 0.5 % (16) | — | — |
| `0x004cf460` | FUN_004cf460 | — | 0.0 % (0) | 0.4 % (13) | — | — |
| `0x004d8f10` | FUN_004d8f10 | — | 0.0 % (0) | 0.4 % (13) | — | — |
| `0x00511f77` | FUN_00511f77 | — | 0.0 % (0) | 0.3 % (10) | — | — |
| `0x004b89f0` | FUN_004b89f0 | — | 0.0 % (0) | 0.3 % (9) | — | — |
| `0x004d56d0` | FUN_004d56d0 | — | 0.0 % (0) | 0.3 % (8) | — | — |
| `0x00517bcb` | __isleadbyte_l | — | 0.0 % (0) | 0.2 % (7) | — | — |
| `0x00524aa7` | ___lock_fhandle | — | 0.0 % (0) | 0.2 % (6) | — | — |
| `0x004991a0` | FUN_004991a0 | — | 0.0 % (0) | 0.2 % (5) | — | — |
| `0x0051fce3` | __getdrive | — | 0.0 % (0) | 0.1 % (3) | — | — |
| `0x00524b47` | FUN_00524b47 | — | 0.0 % (0) | 0.1 % (3) | — | — |
| `0x0050f786` | _memcpy_s | — | 0.0 % (0) | 0.1 % (2) | — | — |
| `0x0051fc2a` | ___dtoxmode | — | 0.0 % (0) | 0.1 % (2) | — | — |
| `0x004dced0` | FUN_004dced0 | — | 0.0 % (0) | 0.1 % (2) | — | — |
| `0x004dd2c0` | FUN_004dd2c0 | — | 0.0 % (0) | 0.1 % (2) | — | — |
| `0x0040e1a0` | FUN_0040e1a0 | — | 0.0 % (0) | 0.1 % (2) | — | — |
| `0x00518de0` | _memset | — | 0.0 % (0) | 0.0 % (1) | — | — |
| (no main-module frame) | | | | 74.6 % (7,791) | | |

**Top caller pairs**

| Slot | Frame | Caller | Samples |
| ---: | --- | --- | ---: |
| 0 | `0xde146` FUN_004de060 | `0xd598e` FUN_004d56d0 | 915 |
| 0 | `0x3002d` FUN_0042fe10 | `0xb38c7` FUN_004b3860 | 297 |
| 0 | `0xd3a7f` FUN_004d3620 | `0xdae13` FUN_004dac90 | 179 |
| 0 | `0xb38c7` FUN_004b3860 | `0x2741` Catch@00402741 | 116 |
| 0 | `0x10e21e` _free | `0x10e23d` _free | 83 |
| 0 | `0x3002d` FUN_0042fe10 | `0x3002d` FUN_0042fe10 | 51 |
| 0 | `0x11133e` _malloc | `0xefd0e` FUN_004efcc0 (handle map insert) | 39 |
| 0 | `0x117da2` FUN_00517d8f | `0xf371d` FUN_004f3510 (texture-file wrapper (.jpg, .tga)) | 37 |
| 0 | `0xd8494` FUN_004d8470 | `0x2741` Catch@00402741 | 31 |
| 0 | `0x11f665` FUN_0051f464 | `0x11f665` FUN_0051f464 | 27 |
| 0 | `0x124004` fastzero_I | `0x124071` __VEC_memzero | 23 |
| 0 | `0xdb046` FUN_004dac90 | `0xf960f` FUN_004f95d0 | 21 |

**Candidates**

Hooked calls account for 0.094 s of the 3.648 s interval (2.6 %); 3.554 s is unexplained by any hook. The sampler recorded 10,442 samples over 10.003 s of report coverage (1044 samples/s, 301 ticks/s per thread); the dominant leaf module is wine at 52.7 %. 2 of 2 delta blocks straddle the interval boundary and carry samples from outside it. Function `0x00523fe4` (fastzero_I) holds 1.3 % of the engine thread's samples as leaf (1.3 % of slot 0's samples) and 1.3 % as first main-module frame; it is not a known routine. Function `0x004ec490` (FUN_004ec490) holds 0.9 % of the engine thread's samples as leaf (0.9 % of slot 0's samples) and 1.1 % as first main-module frame; it is not a known routine. Function `0x0046e400` (FUN_0046e400) holds 0.7 % of the engine thread's samples as leaf (0.7 % of slot 0's samples) and 0.7 % as first main-module frame; it is not a known routine.

## Gap 2: menu load (64.063 s, ends at frame 5)

Label evidence: 1017 GenerateAdjacency calls and 299,186,760 B of 2D texture-helper input match the main-menu work vector.
Interval 8.507–73.716 s, length 64.063 s; hooked exclusive 39.813 s over 17 report windows (2 straddling); unexplained 24.250 s.
Samples 192,149 in 14 delta blocks (2 straddling), 70.025 s covered, 2744 samples/s, 260 ticks/s per thread, 0 dropped; sampler tick 1315 µs mean / 85069 µs max, busy 37.4 % of the covered time.

**Hooked time**

| Operation | Calls | Incl. s | Excl. s | Mean ms | Bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `ID3DXMesh::GenerateAdjacency` | 1,017 | 33.945 | 33.945 | 33.378 | 0 |
| `D3DXCreateTextureFromFileInMemoryEx` | 461 | 2.689 | 2.689 | 5.833 | 299,186,760 |
| `inflate` | 167,610 | 1.852 | 1.852 | 0.011 | 0 |
| `ReadFile` | 45,781 | 0.350 | 0.350 | 0.008 | 190,571,408 |
| `CreateFileA` | 1,161 | 0.242 | 0.242 | 0.209 | 0 |
| `D3DXCreateEffect` | 10 | 0.236 | 0.236 | 23.573 | 309,928 |
| `ID3DXMesh::OptimizeInplace` | 1,017 | 0.228 | 0.228 | 0.224 | 0 |
| `D3DXCleanMesh` | 1,023 | 0.101 | 0.101 | 0.099 | 0 |
| `D3DXLoadSurfaceFromFileInMemory` | 3 | 0.059 | 0.059 | 19.502 | 2,480,817 |
| `FindFirstFileA` | 1,271 | 0.042 | 0.042 | 0.033 | 0 |
| `xmlReadMemory` | 2 | 0.027 | 0.027 | 13.442 | 3,688,819 |
| `D3DXCreateMesh` | 1,017 | 0.023 | 0.023 | 0.023 | 0 |

**Sampled attribution: per-thread module split**

| Slot | TID | Samples | x3ap | ntdll | wine | d3dx | zlib | xml | proxy | other | Start |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 0 | 1260 | 18,234 | 6,372 | 605 | 1,001 | 9,682 | 480 | 4 | 84 | 6 | 0:0x112ead |
| 1 | 2984 | 18,234 | 0 | 18,234 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xb220 |
| 2 | 1432 | 18,234 | 0 | 0 | 18,234 | 0 | 0 | 0 | 0 | 0 | 53:0xc780 |
| 3 | 844 | 18,234 | 0 | 18,115 | 118 | 0 | 0 | 0 | 0 | 1 | 49:0x38490 |
| 4 | 1172 | 18,234 | 0 | 0 | 18,234 | 0 | 0 | 0 | 0 | 0 | 4:0xd0f0 |
| 5 | 2332 | 17,287 | 0 | 17,282 | 4 | 0 | 0 | 0 | 0 | 1 | 20:0xe740 |
| 6 | 1864 | 16,743 | 0 | 2 | 16,741 | 0 | 0 | 0 | 0 | 0 | 38:0x2400 |
| 7 | 1332 | 16,743 | 0 | 16,743 | 0 | 0 | 0 | 0 | 0 | 0 | 7:0x9900 |
| 8 | 688 | 16,743 | 0 | 16,743 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 9 | 3040 | 16,743 | 0 | 16,743 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 10 | 1816 | 185 | 0 | 0 | 185 | 0 | 0 | 0 | 0 | 0 | 57:0x9f90 |
| 10 | 1888 | 61 | 0 | 2 | 59 | 0 | 0 | 0 | 0 | 0 | 57:0x9f90 |
| 11 | 1716 | 16,474 | 0 | 16,474 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |

All threads: x3ap 3.3 %, ntdll 62.9 %, wine 28.4 %, d3dx 5.0 %, zlib 0.2 %, xml 0.0 %, proxy 0.0 %, other 0.0 %.

**Top functions (leaf samples by containing function)**

Shares are of the engine thread (slot 0, 18,234 samples); the other sampled threads are idle Wine/audio/input waiters.

| Function start | Ghidra name | Label | Self (engine) | Incl. frame (engine) | Busiest slot | Hooked APIs in interval |
| --- | --- | --- | ---: | ---: | ---: | --- |
| `0x004e1220` | FUN_004e1220 | — | 3.0 % (551) | 3.5 % (647) | slot 0 3.0 % | — |
| `0x004e0770` | FUN_004e0770 | — | 1.8 % (326) | 2.1 % (390) | slot 0 1.8 % | — |
| `0x004dfef0` | FUN_004dfef0 | — | 1.0 % (189) | 1.3 % (233) | slot 0 1.0 % | — |
| `0x00412440` | FUN_00412440 | — | 0.9 % (172) | 0.9 % (172) | slot 0 0.9 % | — |
| `0x00523fe4` | fastzero_I | — | 0.4 % (76) | 0.4 % (78) | slot 0 0.4 % | — |
| `0x004ec490` | FUN_004ec490 | — | 0.1 % (27) | 0.2 % (33) | slot 0 0.1 % | — |
| `0x0046e400` | FUN_0046e400 | — | 0.1 % (20) | 0.1 % (20) | slot 0 0.1 % | — |
| `0x004e9e20` | FUN_004e9e20 | — | 0.1 % (19) | 0.1 % (19) | slot 0 0.1 % | — |
| `0x00480830` | FUN_00480830 | — | 0.1 % (17) | 0.1 % (17) | slot 0 0.1 % | — |
| `0x004bc1c0` | FUN_004bc1c0 | — | 0.1 % (11) | 0.5 % (94) | slot 0 0.1 % | — |
| `0x004c0150` | FUN_004c0150 | effect state manager: matrix uploads per draw | 0.0 % (7) | 0.2 % (29) | slot 0 0.0 % | — |
| `0x004e9210` | FUN_004e9210 | read dispatcher (fread / gzread / XOR-0x33 slice) | 0.0 % (5) | 0.0 % (6) | slot 0 0.0 % | `ReadFile` 45,781 calls / 0.350 s |
| `0x00517a73` | ___ascii_stricmp | — | 0.0 % (3) | 0.0 % (5) | slot 0 0.0 % | — |
| `0x00515810` | _memcpy | — | 0.0 % (3) | 0.0 % (3) | slot 0 0.0 % | — |
| `0x0052403b` | __VEC_memzero | — | 0.0 % (0) | 45.8 % (8,358) | — | — |
| `0x004de060` | FUN_004de060 | — | 0.0 % (0) | 4.7 % (857) | — | — |
| `0x004bc680` | FUN_004bc680 | mesh preparation: adjacency / clean / optimise | 0.0 % (0) | 2.8 % (503) | — | `ID3DXMesh::GenerateAdjacency` 1,017 calls / 33.945 s; `D3DXCleanMesh` 1,023 calls / 0.101 s; `ID3DXMesh::OptimizeInplace` 1,017 calls / 0.228 s |
| `0x00511f77` | FUN_00511f77 | — | 0.0 % (0) | 2.3 % (425) | — | — |
| `0x00524aa7` | ___lock_fhandle | — | 0.0 % (0) | 2.3 % (418) | — | — |
| `0x004dd2c0` | FUN_004dd2c0 | — | 0.0 % (0) | 1.1 % (196) | — | — |
| `0x0050e1b0` | _free | — | 0.0 % (0) | 1.0 % (175) | — | — |
| `0x004bcb60` | FUN_004bcb60 | — | 0.0 % (0) | 0.8 % (137) | — | — |
| `0x0051f464` | FUN_0051f464 | — | 0.0 % (0) | 0.5 % (91) | — | — |
| `0x004dc540` | FUN_004dc540 | texture loader (pck dds; image info, cube, 2D helpers) | 0.0 % (0) | 0.4 % (82) | — | `D3DXCreateTextureFromFileInMemoryEx` 461 calls / 2.689 s; `D3DXCreateCubeTextureFromFileInMemoryEx` 9 calls / 0.017 s |
| `0x0051da61` | __VEC_memcpy | — | 0.0 % (0) | 0.4 % (76) | — | — |
| `0x005112c4` | _malloc | — | 0.0 % (0) | 0.3 % (54) | — | — |
| `0x00517d8f` | FUN_00517d8f | — | 0.0 % (0) | 0.2 % (37) | — | — |
| `0x004b8ab0` | FUN_004b8ab0 | — | 0.0 % (0) | 0.2 % (37) | — | — |
| `0x004b8920` | FUN_004b8920 | — | 0.0 % (0) | 0.2 % (32) | — | — |
| `0x004cf460` | FUN_004cf460 | — | 0.0 % (0) | 0.2 % (29) | — | — |
| `0x00519434` | FUN_00519434 | — | 0.0 % (0) | 0.1 % (26) | — | — |
| `0x00527869` | FUN_00527869 | — | 0.0 % (0) | 0.1 % (24) | — | — |
| `0x0051b6bb` | FUN_0051b6bb | — | 0.0 % (0) | 0.1 % (21) | — | — |
| `0x004efcc0` | FUN_004efcc0 | handle map insert | 0.0 % (0) | 0.1 % (21) | — | — |
| `0x004b4600` | FUN_004b4600 | — | 0.0 % (0) | 0.1 % (18) | — | — |
| `0x004b4f80` | FUN_004b4f80 | — | 0.0 % (0) | 0.1 % (18) | — | — |
| `0x004bbb10` | FUN_004bbb10 | — | 0.0 % (0) | 0.1 % (12) | — | — |
| `0x004db520` | FUN_004db520 | — | 0.0 % (0) | 0.1 % (11) | — | — |
| `0x004d1d40` | FUN_004d1d40 | — | 0.0 % (0) | 0.0 % (8) | — | — |
| `0x00517bcb` | __isleadbyte_l | — | 0.0 % (0) | 0.0 % (7) | — | — |
| (no main-module frame) | | | | 90.5 % (173,913) | | |

**Top caller pairs**

| Slot | Frame | Caller | Samples |
| ---: | --- | --- | ---: |
| 0 | `0x124071` __VEC_memzero | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 7,956 |
| 0 | `0xde146` FUN_004de060 | `0xd598e` FUN_004d56d0 | 857 |
| 0 | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 478 |
| 0 | `0x124b30` ___lock_fhandle | `0x124b38` ___lock_fhandle | 405 |
| 0 | `0x1240bd` __VEC_memzero | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 383 |
| 0 | `0x11204f` FUN_00511f77 | `0x1121d1` _fread_s | 346 |
| 0 | `0xdd6a2` FUN_004dd2c0 | `0xdd6a2` FUN_004dd2c0 | 171 |
| 0 | `0xbcc30` FUN_004bcb60 | `0xbb9ac` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 136 |
| 0 | `0x10e21e` _free | `0x10e23d` _free | 97 |
| 0 | `0x11f665` FUN_0051f464 | `0x11f665` FUN_0051f464 | 91 |
| 0 | `0xdc846` FUN_004dc540 (texture loader (pck dds; image info, cube, 2D helpers)) | `0xdc846` FUN_004dc540 (texture loader (pck dds; image info, cube, 2D helpers)) | 81 |
| 0 | `0x11204f` FUN_00511f77 | `0xe8da9` FUN_004e8880 (resource read) | 78 |

**Candidates**

Hooked calls account for 39.813 s of the 64.063 s interval (62.1 %); 24.250 s is unexplained by any hook. The sampler recorded 192,149 samples over 70.025 s of report coverage (2744 samples/s, 260 ticks/s per thread); the dominant leaf module is ntdll at 62.9 %. 2 of 14 delta blocks straddle the interval boundary and carry samples from outside it. Function `0x004e1220` (FUN_004e1220) holds 3.0 % of the engine thread's samples as leaf (3.0 % of slot 0's samples) and 3.5 % as first main-module frame; it is not a known routine. Function `0x004e0770` (FUN_004e0770) holds 1.8 % of the engine thread's samples as leaf (1.8 % of slot 0's samples) and 2.1 % as first main-module frame; it is not a known routine. Function `0x004dfef0` (FUN_004dfef0) holds 1.0 % of the engine thread's samples as leaf (1.0 % of slot 0's samples) and 1.3 % as first main-module frame; it is not a known routine.

### Gap 2 stall 1: report stall 17.645 s

Interval 10.415–28.060 s, length 17.645 s; hooked exclusive 0.316 s over 1 report windows (0 straddling); unexplained 17.330 s.
Samples 56,201 in 4 delta blocks (2 straddling), 20.009 s covered, 2809 samples/s, 256 ticks/s per thread, 0 dropped; sampler tick 1373 µs mean / 85069 µs max, busy 38.2 % of the covered time.

**Hooked time**

| Operation | Calls | Incl. s | Excl. s | Mean ms | Bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `inflate` | 23,307 | 0.285 | 0.285 | 0.012 | 0 |
| `ReadFile` | 5,893 | 0.017 | 0.017 | 0.003 | 25,028,096 |
| `D3DXCreateTextureFromFileInMemoryEx` | 6 | 0.004 | 0.004 | 0.715 | 5,942,628 |
| `ID3DXMesh::GenerateAdjacency` | 17 | 0.004 | 0.004 | 0.213 | 0 |
| `CreateFileA` | 22 | 0.002 | 0.002 | 0.097 | 0 |
| `ID3DXMesh::OptimizeInplace` | 17 | 0.002 | 0.002 | 0.094 | 0 |
| `FindFirstFileA` | 24 | 0.001 | 0.001 | 0.035 | 0 |
| `D3DXCleanMesh` | 17 | 0.001 | 0.001 | 0.046 | 0 |
| `D3DXCreateMesh` | 17 | 0.001 | 0.001 | 0.038 | 0 |
| `SetFilePointer` | 66 | 0.000 | 0.000 | 0.001 | 0 |

**Sampled attribution: per-thread module split**

| Slot | TID | Samples | x3ap | ntdll | wine | d3dx | zlib | xml | proxy | other | Start |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 0 | 1260 | 5,129 | 4,496 | 74 | 52 | 409 | 95 | 0 | 3 | 0 | 0:0x112ead |
| 1 | 2984 | 5,129 | 0 | 5,129 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xb220 |
| 2 | 1432 | 5,129 | 0 | 0 | 5,129 | 0 | 0 | 0 | 0 | 0 | 53:0xc780 |
| 3 | 844 | 5,129 | 0 | 5,119 | 9 | 0 | 0 | 0 | 0 | 1 | 49:0x38490 |
| 4 | 1172 | 5,129 | 0 | 0 | 5,129 | 0 | 0 | 0 | 0 | 0 | 4:0xd0f0 |
| 5 | 2332 | 5,129 | 0 | 5,127 | 2 | 0 | 0 | 0 | 0 | 0 | 20:0xe740 |
| 6 | 1864 | 5,127 | 0 | 2 | 5,125 | 0 | 0 | 0 | 0 | 0 | 38:0x2400 |
| 7 | 1332 | 5,127 | 0 | 5,127 | 0 | 0 | 0 | 0 | 0 | 0 | 7:0x9900 |
| 8 | 688 | 5,127 | 0 | 5,127 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 9 | 3040 | 5,127 | 0 | 5,127 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 10 | 1888 | 61 | 0 | 2 | 59 | 0 | 0 | 0 | 0 | 0 | 57:0x9f90 |
| 11 | 1716 | 4,858 | 0 | 4,858 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |

All threads: x3ap 8.0 %, ntdll 63.5 %, wine 27.6 %, d3dx 0.7 %, zlib 0.2 %, xml 0.0 %, proxy 0.0 %, other 0.0 %.

**Top functions (leaf samples by containing function)**

Shares are of the engine thread (slot 0, 5,129 samples); the other sampled threads are idle Wine/audio/input waiters.

| Function start | Ghidra name | Label | Self (engine) | Incl. frame (engine) | Busiest slot | Hooked APIs in interval |
| --- | --- | --- | ---: | ---: | ---: | --- |
| `0x004e1220` | FUN_004e1220 | — | 10.1 % (518) | 11.1 % (568) | slot 0 10.1 % | — |
| `0x004e0770` | FUN_004e0770 | — | 6.0 % (307) | 6.7 % (344) | slot 0 6.0 % | — |
| `0x004dfef0` | FUN_004dfef0 | — | 2.8 % (143) | 2.8 % (143) | slot 0 2.8 % | — |
| `0x00412440` | FUN_00412440 | — | 2.7 % (136) | 2.7 % (136) | slot 0 2.7 % | — |
| `0x004e9e20` | FUN_004e9e20 | — | 0.4 % (19) | 0.4 % (19) | slot 0 0.4 % | — |
| `0x00523fe4` | fastzero_I | — | 0.3 % (16) | 0.3 % (16) | slot 0 0.3 % | — |
| `0x004bc680` | FUN_004bc680 | mesh preparation: adjacency / clean / optimise | 0.0 % (0) | 4.5 % (229) | — | `ID3DXMesh::GenerateAdjacency` 17 calls / 0.004 s; `D3DXCleanMesh` 17 calls / 0.001 s; `ID3DXMesh::OptimizeInplace` 17 calls / 0.002 s |
| `0x0052403b` | __VEC_memzero | — | 0.0 % (0) | 2.4 % (122) | — | — |
| `0x00511f77` | FUN_00511f77 | — | 0.0 % (0) | 1.0 % (51) | — | — |
| `0x0051da61` | __VEC_memcpy | — | 0.0 % (0) | 0.9 % (45) | — | — |
| `0x004bcb60` | FUN_004bcb60 | — | 0.0 % (0) | 0.8 % (41) | — | — |
| `0x0050e1b0` | _free | — | 0.0 % (0) | 0.4 % (22) | — | — |
| `0x004cf460` | FUN_004cf460 | — | 0.0 % (0) | 0.4 % (22) | — | — |
| `0x004b4600` | FUN_004b4600 | — | 0.0 % (0) | 0.4 % (18) | — | — |
| `0x004dc540` | FUN_004dc540 | texture loader (pck dds; image info, cube, 2D helpers) | 0.0 % (0) | 0.2 % (10) | — | `D3DXCreateTextureFromFileInMemoryEx` 6 calls / 0.004 s |
| `0x004d1d40` | FUN_004d1d40 | — | 0.0 % (0) | 0.2 % (8) | — | — |
| `0x0051f464` | FUN_0051f464 | — | 0.0 % (0) | 0.1 % (7) | — | — |
| `0x004bbb10` | FUN_004bbb10 | — | 0.0 % (0) | 0.1 % (5) | — | — |
| (no main-module frame) | | | | 90.9 % (51,071) | | |

**Top caller pairs**

| Slot | Frame | Caller | Samples |
| ---: | --- | --- | ---: |
| 0 | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 229 |
| 0 | `0x1240bd` __VEC_memzero | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 122 |
| 0 | `0x11204f` FUN_00511f77 | `0x1121d1` _fread_s | 50 |
| 0 | `0x11dab3` __VEC_memcpy | `0x11204f` FUN_00511f77 | 45 |
| 0 | `0xbcc30` FUN_004bcb60 | `0xbb9ac` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 41 |
| 0 | `0x1244b` FUN_00412440 | `0xe036a` FUN_004dfef0 | 35 |
| 0 | `0x1244b` FUN_00412440 | `0xe011b` FUN_004dfef0 | 34 |
| 0 | `0xe033e` FUN_004dfef0 | `0xe0e7d` FUN_004e0e70 | 33 |
| 0 | `0x1244b` FUN_00412440 | `0xe05b5` FUN_004dfef0 | 31 |
| 0 | `0xe00ed` FUN_004dfef0 | `0xe0e7d` FUN_004e0e70 | 31 |
| 0 | `0xe17ea` FUN_004e1220 | `0xe1538` FUN_004e1220 | 27 |
| 0 | `0xe058b` FUN_004dfef0 | `0xe0e7d` FUN_004e0e70 | 23 |

**Candidates**

Hooked calls account for 0.316 s of the 17.645 s interval (1.8 %); 17.330 s is unexplained by any hook. The sampler recorded 56,201 samples over 20.009 s of report coverage (2809 samples/s, 256 ticks/s per thread); the dominant leaf module is ntdll at 63.5 %. 2 of 4 delta blocks straddle the interval boundary and carry samples from outside it. Function `0x004e1220` (FUN_004e1220) holds 10.1 % of the engine thread's samples as leaf (10.1 % of slot 0's samples) and 11.1 % as first main-module frame; it is not a known routine. Function `0x004e0770` (FUN_004e0770) holds 6.0 % of the engine thread's samples as leaf (6.0 % of slot 0's samples) and 6.7 % as first main-module frame; it is not a known routine. Function `0x004dfef0` (FUN_004dfef0) holds 2.8 % of the engine thread's samples as leaf (2.8 % of slot 0's samples) and 2.8 % as first main-module frame; it is not a known routine.

### Gap 2 stall 2: report stall 2.284 s

Interval 30.067–32.351 s, length 2.284 s; hooked exclusive 0.541 s over 1 report windows (0 straddling); unexplained 1.743 s.
Samples 14,234 in 1 delta blocks (1 straddling), 5.008 s covered, 2842 samples/s, 258 ticks/s per thread, 0 dropped; sampler tick 1358 µs mean / 10841 µs max, busy 38.2 % of the covered time.

**Hooked time**

| Operation | Calls | Incl. s | Excl. s | Mean ms | Bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `ID3DXMesh::GenerateAdjacency` | 130 | 0.463 | 0.463 | 3.558 | 0 |
| `inflate` | 3,469 | 0.042 | 0.042 | 0.012 | 0 |
| `ID3DXMesh::OptimizeInplace` | 130 | 0.021 | 0.021 | 0.161 | 0 |
| `D3DXCleanMesh` | 131 | 0.008 | 0.008 | 0.058 | 0 |
| `ReadFile` | 871 | 0.006 | 0.006 | 0.006 | 3,560,448 |
| `D3DXCreateMesh` | 130 | 0.002 | 0.002 | 0.019 | 0 |
| `CreateFileA` | 1 | 0.000 | 0.000 | 0.238 | 0 |
| `FindFirstFileA` | 1 | 0.000 | 0.000 | 0.060 | 0 |
| `SetFilePointer` | 3 | 0.000 | 0.000 | 0.002 | 0 |

**Sampled attribution: per-thread module split**

| Slot | TID | Samples | x3ap | ntdll | wine | d3dx | zlib | xml | proxy | other | Start |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 0 | 1260 | 1,294 | 31 | 18 | 6 | 1,185 | 51 | 0 | 3 | 0 | 0:0x112ead |
| 1 | 2984 | 1,294 | 0 | 1,294 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xb220 |
| 2 | 1432 | 1,294 | 0 | 0 | 1,294 | 0 | 0 | 0 | 0 | 0 | 53:0xc780 |
| 3 | 844 | 1,294 | 0 | 1,290 | 4 | 0 | 0 | 0 | 0 | 0 | 49:0x38490 |
| 4 | 1172 | 1,294 | 0 | 0 | 1,294 | 0 | 0 | 0 | 0 | 0 | 4:0xd0f0 |
| 5 | 2332 | 1,294 | 0 | 1,294 | 0 | 0 | 0 | 0 | 0 | 0 | 20:0xe740 |
| 6 | 1864 | 1,294 | 0 | 0 | 1,294 | 0 | 0 | 0 | 0 | 0 | 38:0x2400 |
| 7 | 1332 | 1,294 | 0 | 1,294 | 0 | 0 | 0 | 0 | 0 | 0 | 7:0x9900 |
| 8 | 688 | 1,294 | 0 | 1,294 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 9 | 3040 | 1,294 | 0 | 1,294 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 11 | 1716 | 1,294 | 0 | 1,294 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |

All threads: x3ap 0.2 %, ntdll 63.7 %, wine 27.3 %, d3dx 8.3 %, zlib 0.4 %, xml 0.0 %, proxy 0.0 %, other 0.0 %.

**Top functions (leaf samples by containing function)**

Shares are of the engine thread (slot 0, 1,294 samples); the other sampled threads are idle Wine/audio/input waiters.

| Function start | Ghidra name | Label | Self (engine) | Incl. frame (engine) | Busiest slot | Hooked APIs in interval |
| --- | --- | --- | ---: | ---: | ---: | --- |
| `0x0052403b` | __VEC_memzero | — | 0.0 % (0) | 55.6 % (720) | — | — |
| `0x00524aa7` | ___lock_fhandle | — | 0.0 % (0) | 31.5 % (407) | — | — |
| `0x00511f77` | FUN_00511f77 | — | 0.0 % (0) | 4.4 % (57) | — | — |
| `0x004bc680` | FUN_004bc680 | mesh preparation: adjacency / clean / optimise | 0.0 % (0) | 3.6 % (46) | — | `ID3DXMesh::GenerateAdjacency` 130 calls / 0.463 s; `D3DXCleanMesh` 131 calls / 0.008 s; `ID3DXMesh::OptimizeInplace` 130 calls / 0.021 s |
| `0x004bc1c0` | FUN_004bc1c0 | — | 0.0 % (0) | 1.8 % (23) | — | — |
| `0x004bcb60` | FUN_004bcb60 | — | 0.0 % (0) | 1.0 % (13) | — | — |
| `0x0050e1b0` | _free | — | 0.0 % (0) | 0.8 % (10) | — | — |
| `0x004bbb10` | FUN_004bbb10 | — | 0.0 % (0) | 0.5 % (7) | — | — |
| `0x0051f464` | FUN_0051f464 | — | 0.0 % (0) | 0.2 % (3) | — | — |
| `0x00523fe4` | fastzero_I | — | 0.0 % (0) | 0.2 % (2) | — | — |
| `0x004bc9c0` | FUN_004bc9c0 | installs the cleaned mesh | 0.0 % (0) | 0.2 % (2) | — | — |
| `0x00519434` | FUN_00519434 | — | 0.0 % (0) | 0.1 % (1) | — | — |
| `0x00527869` | FUN_00527869 | — | 0.0 % (0) | 0.1 % (1) | — | — |
| `0x004bcee0` | FUN_004bcee0 | — | 0.0 % (0) | 0.1 % (1) | — | — |
| `0x004e9210` | FUN_004e9210 | read dispatcher (fread / gzread / XOR-0x33 slice) | 0.0 % (0) | 0.1 % (1) | — | `ReadFile` 871 calls / 0.006 s |
| (no main-module frame) | | | | 90.9 % (12,940) | | |

**Top caller pairs**

| Slot | Frame | Caller | Samples |
| ---: | --- | --- | ---: |
| 0 | `0x124071` __VEC_memzero | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 640 |
| 0 | `0x124b30` ___lock_fhandle | `0x124b38` ___lock_fhandle | 405 |
| 0 | `0x1240bd` __VEC_memzero | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 80 |
| 0 | `0x11204f` FUN_00511f77 | `0xe8da9` FUN_004e8880 (resource read) | 57 |
| 0 | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 45 |
| 0 | `0xbcc30` FUN_004bcb60 | `0xbb9ac` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 13 |
| 0 | `0x10e21e` _free | `0xbc90a` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 9 |
| 0 | `0xbc30e` FUN_004bc1c0 | `0xbb94b` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 4 |
| 0 | `0x11f665` FUN_0051f464 | `0x11f665` FUN_0051f464 | 3 |
| 0 | `0xbbc7c` FUN_004bbb10 | `0xbc1ae` FUN_004bc080 | 2 |
| 0 | `0xbc06f` FUN_004bbb10 | `0xbc1ae` FUN_004bc080 | 2 |
| 0 | `0xbc3a2` FUN_004bc1c0 | `0xbb94b` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 2 |

**Candidates**

Hooked calls account for 0.541 s of the 2.284 s interval (23.7 %); 1.743 s is unexplained by any hook. The sampler recorded 14,234 samples over 5.008 s of report coverage (2842 samples/s, 258 ticks/s per thread); the dominant leaf module is ntdll at 63.7 %. 1 of 1 delta blocks straddle the interval boundary and carry samples from outside it. Function `0x0052403b` (__VEC_memzero) holds 0.0 % of the engine thread's samples as leaf and 55.6 % as first main-module frame; it is not a known routine. Function `0x00524aa7` (___lock_fhandle) holds 0.0 % of the engine thread's samples as leaf and 31.5 % as first main-module frame; it is not a known routine. Function `0x00511f77` (FUN_00511f77) holds 0.0 % of the engine thread's samples as leaf and 4.4 % as first main-module frame; it is not a known routine.

### Gap 2 stall 3: report stall 4.056 s

Interval 32.351–36.406 s, length 4.056 s; hooked exclusive 5.589 s over 1 report windows (0 straddling); unexplained -1.534 s.
Samples 28,589 in 2 delta blocks (2 straddling), 10.011 s covered, 2856 samples/s, 260 ticks/s per thread, 0 dropped; sampler tick 1346 µs mean / 10841 µs max, busy 38.1 % of the covered time.

**Hooked time**

| Operation | Calls | Incl. s | Excl. s | Mean ms | Bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `ID3DXMesh::GenerateAdjacency` | 1 | 3.849 | 3.849 | 3849.046 | 0 |
| `D3DXCreateTextureFromFileInMemoryEx` | 11 | 1.580 | 1.580 | 143.656 | 33,733,664 |
| `inflate` | 15,120 | 0.147 | 0.147 | 0.010 | 0 |
| `ReadFile` | 3,814 | 0.009 | 0.009 | 0.002 | 15,543,296 |
| `CreateFileA` | 11 | 0.002 | 0.002 | 0.198 | 0 |
| `D3DXCreateCubeTextureFromFileInMemoryEx` | 1 | 0.001 | 0.001 | 1.462 | 524,408 |
| `FindFirstFileA` | 11 | 0.000 | 0.000 | 0.036 | 0 |
| `D3DXCreateMesh` | 1 | 0.000 | 0.000 | 0.082 | 0 |
| `SetFilePointer` | 33 | 0.000 | 0.000 | 0.001 | 0 |

**Sampled attribution: per-thread module split**

| Slot | TID | Samples | x3ap | ntdll | wine | d3dx | zlib | xml | proxy | other | Start |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 0 | 1260 | 2,599 | 76 | 22 | 9 | 2,428 | 61 | 0 | 3 | 0 | 0:0x112ead |
| 1 | 2984 | 2,599 | 0 | 2,599 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xb220 |
| 2 | 1432 | 2,599 | 0 | 0 | 2,599 | 0 | 0 | 0 | 0 | 0 | 53:0xc780 |
| 3 | 844 | 2,599 | 0 | 2,593 | 6 | 0 | 0 | 0 | 0 | 0 | 49:0x38490 |
| 4 | 1172 | 2,599 | 0 | 0 | 2,599 | 0 | 0 | 0 | 0 | 0 | 4:0xd0f0 |
| 5 | 2332 | 2,599 | 0 | 2,599 | 0 | 0 | 0 | 0 | 0 | 0 | 20:0xe740 |
| 6 | 1864 | 2,599 | 0 | 0 | 2,599 | 0 | 0 | 0 | 0 | 0 | 38:0x2400 |
| 7 | 1332 | 2,599 | 0 | 2,599 | 0 | 0 | 0 | 0 | 0 | 0 | 7:0x9900 |
| 8 | 688 | 2,599 | 0 | 2,599 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 9 | 3040 | 2,599 | 0 | 2,599 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 11 | 1716 | 2,599 | 0 | 2,599 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |

All threads: x3ap 0.3 %, ntdll 63.7 %, wine 27.3 %, d3dx 8.5 %, zlib 0.2 %, xml 0.0 %, proxy 0.0 %, other 0.0 %.

**Top functions (leaf samples by containing function)**

Shares are of the engine thread (slot 0, 2,599 samples); the other sampled threads are idle Wine/audio/input waiters.

| Function start | Ghidra name | Label | Self (engine) | Incl. frame (engine) | Busiest slot | Hooked APIs in interval |
| --- | --- | --- | ---: | ---: | ---: | --- |
| `0x0052403b` | __VEC_memzero | — | 0.0 % (0) | 74.7 % (1,942) | — | — |
| `0x00524aa7` | ___lock_fhandle | — | 0.0 % (0) | 15.7 % (409) | — | — |
| `0x004bc1c0` | FUN_004bc1c0 | — | 0.0 % (0) | 2.5 % (64) | — | — |
| `0x00511f77` | FUN_00511f77 | — | 0.0 % (0) | 2.2 % (57) | — | — |
| `0x004bc680` | FUN_004bc680 | mesh preparation: adjacency / clean / optimise | 0.0 % (0) | 1.9 % (49) | — | `ID3DXMesh::GenerateAdjacency` 1 calls / 3.849 s |
| `0x004bcb60` | FUN_004bcb60 | — | 0.0 % (0) | 1.0 % (25) | — | — |
| `0x0050e1b0` | _free | — | 0.0 % (0) | 0.7 % (18) | — | — |
| `0x0051da61` | __VEC_memcpy | — | 0.0 % (0) | 0.4 % (10) | — | — |
| `0x004bbb10` | FUN_004bbb10 | — | 0.0 % (0) | 0.3 % (7) | — | — |
| `0x0051f464` | FUN_0051f464 | — | 0.0 % (0) | 0.1 % (3) | — | — |
| `0x00523fe4` | fastzero_I | — | 0.0 % (0) | 0.1 % (2) | — | — |
| `0x004bc9c0` | FUN_004bc9c0 | installs the cleaned mesh | 0.0 % (0) | 0.1 % (2) | — | — |
| `0x00514ea4` | _setlocale | — | 0.0 % (0) | 0.0 % (1) | — | — |
| `0x00519434` | FUN_00519434 | — | 0.0 % (0) | 0.0 % (1) | — | — |
| `0x00527869` | FUN_00527869 | — | 0.0 % (0) | 0.0 % (1) | — | — |
| `0x004bb470` | FUN_004bb470 | mesh build: D3DXCreateMesh + vertex/index fill | 0.0 % (0) | 0.0 % (1) | — | `D3DXCreateMesh` 1 calls / 0.000 s |
| `0x004bcee0` | FUN_004bcee0 | — | 0.0 % (0) | 0.0 % (1) | — | — |
| `0x004e9210` | FUN_004e9210 | read dispatcher (fread / gzread / XOR-0x33 slice) | 0.0 % (0) | 0.0 % (1) | — | `ReadFile` 3,814 calls / 0.009 s |
| (no main-module frame) | | | | 90.9 % (25,990) | | |

**Top caller pairs**

| Slot | Frame | Caller | Samples |
| ---: | --- | --- | ---: |
| 0 | `0x124071` __VEC_memzero | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 1,861 |
| 0 | `0x124b30` ___lock_fhandle | `0x124b38` ___lock_fhandle | 405 |
| 0 | `0x1240bd` __VEC_memzero | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 81 |
| 0 | `0x11204f` FUN_00511f77 | `0xe8da9` FUN_004e8880 (resource read) | 57 |
| 0 | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 45 |
| 0 | `0xbcc30` FUN_004bcb60 | `0xbb9ac` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 25 |
| 0 | `0x11dab3` __VEC_memcpy | `0x11204f` FUN_00511f77 | 10 |
| 0 | `0x10e21e` _free | `0xbc90a` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 9 |
| 0 | `0x10e21e` _free | `0x10e23d` _free | 7 |
| 0 | `0xbc30e` FUN_004bc1c0 | `0xbb94b` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 4 |
| 0 | `0xbc34a` FUN_004bc1c0 | `0xbb94b` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 3 |
| 0 | `0xbc45b` FUN_004bc1c0 | `0xbb94b` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 3 |

**Candidates**

Hooked calls account for 5.589 s of the 4.056 s interval (137.8 %); -1.534 s is unexplained by any hook. The sampler recorded 28,589 samples over 10.011 s of report coverage (2856 samples/s, 260 ticks/s per thread); the dominant leaf module is ntdll at 63.7 %. 2 of 2 delta blocks straddle the interval boundary and carry samples from outside it. Function `0x0052403b` (__VEC_memzero) holds 0.0 % of the engine thread's samples as leaf and 74.7 % as first main-module frame; it is not a known routine. Function `0x00524aa7` (___lock_fhandle) holds 0.0 % of the engine thread's samples as leaf and 15.7 % as first main-module frame; it is not a known routine. Function `0x004bc1c0` (FUN_004bc1c0) holds 0.0 % of the engine thread's samples as leaf and 2.5 % as first main-module frame; it is not a known routine.

### Gap 2 stall 4: report stall 27.076 s

Interval 36.406–63.482 s, length 27.076 s; hooked exclusive 26.862 s over 1 report windows (0 straddling); unexplained 0.214 s.
Samples 86,009 in 6 delta blocks (2 straddling), 30.005 s covered, 2866 samples/s, 261 ticks/s per thread, 0 dropped; sampler tick 1338 µs mean / 3927 µs max, busy 38.0 % of the covered time.

**Hooked time**

| Operation | Calls | Incl. s | Excl. s | Mean ms | Bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `ID3DXMesh::GenerateAdjacency` | 4 | 26.761 | 26.761 | 6690.205 | 0 |
| `ID3DXMesh::OptimizeInplace` | 5 | 0.040 | 0.040 | 8.057 | 0 |
| `inflate` | 3,147 | 0.034 | 0.034 | 0.011 | 0 |
| `D3DXCleanMesh` | 5 | 0.016 | 0.016 | 3.214 | 0 |
| `D3DXCreateTextureFromFileInMemoryEx` | 2 | 0.005 | 0.005 | 2.658 | 5,617,264 |
| `ReadFile` | 794 | 0.004 | 0.004 | 0.006 | 3,237,888 |
| `CreateFileA` | 2 | 0.001 | 0.001 | 0.251 | 0 |
| `D3DXCreateMesh` | 4 | 0.000 | 0.000 | 0.112 | 0 |
| `FindFirstFileA` | 2 | 0.000 | 0.000 | 0.050 | 0 |
| `SetFilePointer` | 6 | 0.000 | 0.000 | 0.002 | 0 |

**Sampled attribution: per-thread module split**

| Slot | TID | Samples | x3ap | ntdll | wine | d3dx | zlib | xml | proxy | other | Start |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 0 | 1260 | 7,819 | 413 | 15 | 4 | 7,359 | 22 | 0 | 5 | 1 | 0:0x112ead |
| 1 | 2984 | 7,819 | 0 | 7,819 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xb220 |
| 2 | 1432 | 7,819 | 0 | 0 | 7,819 | 0 | 0 | 0 | 0 | 0 | 53:0xc780 |
| 3 | 844 | 7,819 | 0 | 7,816 | 3 | 0 | 0 | 0 | 0 | 0 | 49:0x38490 |
| 4 | 1172 | 7,819 | 0 | 0 | 7,819 | 0 | 0 | 0 | 0 | 0 | 4:0xd0f0 |
| 5 | 2332 | 7,819 | 0 | 7,817 | 1 | 0 | 0 | 0 | 0 | 1 | 20:0xe740 |
| 6 | 1864 | 7,819 | 0 | 0 | 7,819 | 0 | 0 | 0 | 0 | 0 | 38:0x2400 |
| 7 | 1332 | 7,819 | 0 | 7,819 | 0 | 0 | 0 | 0 | 0 | 0 | 7:0x9900 |
| 8 | 688 | 7,819 | 0 | 7,819 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 9 | 3040 | 7,819 | 0 | 7,819 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 11 | 1716 | 7,819 | 0 | 7,819 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |

All threads: x3ap 0.5 %, ntdll 63.6 %, wine 27.3 %, d3dx 8.6 %, zlib 0.0 %, xml 0.0 %, proxy 0.0 %, other 0.0 %.

**Top functions (leaf samples by containing function)**

Shares are of the engine thread (slot 0, 7,819 samples); the other sampled threads are idle Wine/audio/input waiters.

| Function start | Ghidra name | Label | Self (engine) | Incl. frame (engine) | Busiest slot | Hooked APIs in interval |
| --- | --- | --- | ---: | ---: | ---: | --- |
| `0x004dfef0` | FUN_004dfef0 | — | 0.3 % (23) | 0.4 % (34) | slot 0 0.3 % | — |
| `0x004e1220` | FUN_004e1220 | — | 0.3 % (22) | 0.5 % (42) | slot 0 0.3 % | — |
| `0x004e0770` | FUN_004e0770 | — | 0.2 % (14) | 0.3 % (20) | slot 0 0.2 % | — |
| `0x00412440` | FUN_00412440 | — | 0.1 % (10) | 0.1 % (10) | slot 0 0.1 % | — |
| `0x0052403b` | __VEC_memzero | — | 0.0 % (0) | 93.6 % (7,318) | — | — |
| `0x004bc1c0` | FUN_004bc1c0 | — | 0.0 % (0) | 0.5 % (41) | — | — |
| `0x004bcb60` | FUN_004bcb60 | — | 0.0 % (0) | 0.3 % (26) | — | — |
| `0x00511f77` | FUN_00511f77 | — | 0.0 % (0) | 0.2 % (12) | — | — |
| `0x0050e1b0` | _free | — | 0.0 % (0) | 0.1 % (11) | — | — |
| `0x0051da61` | __VEC_memcpy | — | 0.0 % (0) | 0.1 % (10) | — | — |
| `0x004bc680` | FUN_004bc680 | mesh preparation: adjacency / clean / optimise | 0.0 % (0) | 0.1 % (6) | — | `ID3DXMesh::GenerateAdjacency` 4 calls / 26.761 s; `D3DXCleanMesh` 5 calls / 0.016 s; `ID3DXMesh::OptimizeInplace` 5 calls / 0.040 s |
| `0x005112c4` | _malloc | — | 0.0 % (0) | 0.1 % (4) | — | — |
| `0x0051f464` | FUN_0051f464 | — | 0.0 % (0) | 0.1 % (4) | — | — |
| `0x004dc540` | FUN_004dc540 | texture loader (pck dds; image info, cube, 2D helpers) | 0.0 % (0) | 0.0 % (3) | — | `D3DXCreateTextureFromFileInMemoryEx` 2 calls / 0.005 s |
| `0x00524aa7` | ___lock_fhandle | — | 0.0 % (0) | 0.0 % (2) | — | — |
| `0x004e1b00` | FUN_004e1b00 | — | 0.0 % (0) | 0.0 % (2) | — | — |
| `0x00514ea4` | _setlocale | — | 0.0 % (0) | 0.0 % (1) | — | — |
| `0x004bb470` | FUN_004bb470 | mesh build: D3DXCreateMesh + vertex/index fill | 0.0 % (0) | 0.0 % (1) | — | `D3DXCreateMesh` 4 calls / 0.000 s |
| (no main-module frame) | | | | 90.9 % (78,190) | | |

**Top caller pairs**

| Slot | Frame | Caller | Samples |
| ---: | --- | --- | ---: |
| 0 | `0x124071` __VEC_memzero | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 7,316 |
| 0 | `0xbcc30` FUN_004bcb60 | `0xbb9ac` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 26 |
| 0 | `0x10e21e` _free | `0x10e23d` _free | 10 |
| 0 | `0x11dab3` __VEC_memcpy | `0x11204f` FUN_00511f77 | 10 |
| 0 | `0x1244b` FUN_00412440 | `0xe036a` FUN_004dfef0 | 6 |
| 0 | `0x11204f` FUN_00511f77 | `0xe8da9` FUN_004e8880 (resource read) | 6 |
| 0 | `0x11204f` FUN_00511f77 | `0x1121d1` _fread_s | 6 |
| 0 | `0xe0124` FUN_004dfef0 | `0xe0e7d` FUN_004e0e70 | 5 |
| 0 | `0xe00ed` FUN_004dfef0 | `0xe0e7d` FUN_004e0e70 | 4 |
| 0 | `0xe033e` FUN_004dfef0 | `0xe0e7d` FUN_004e0e70 | 4 |
| 0 | `0xe0570` FUN_004dfef0 | `0xe0e7d` FUN_004e0e70 | 4 |
| 0 | `0x11f665` FUN_0051f464 | `0x11f665` FUN_0051f464 | 4 |

**Candidates**

Hooked calls account for 26.862 s of the 27.076 s interval (99.2 %); 0.214 s is unexplained by any hook. The sampler recorded 86,009 samples over 30.005 s of report coverage (2866 samples/s, 261 ticks/s per thread); the dominant leaf module is ntdll at 63.6 %. 2 of 6 delta blocks straddle the interval boundary and carry samples from outside it. Function `0x004dfef0` (FUN_004dfef0) holds 0.3 % of the engine thread's samples as leaf (0.3 % of slot 0's samples) and 0.4 % as first main-module frame; it is not a known routine. Function `0x004e1220` (FUN_004e1220) holds 0.3 % of the engine thread's samples as leaf (0.3 % of slot 0's samples) and 0.5 % as first main-module frame; it is not a known routine. Function `0x004e0770` (FUN_004e0770) holds 0.2 % of the engine thread's samples as leaf (0.2 % of slot 0's samples) and 0.3 % as first main-module frame; it is not a known routine.

## Gap 3: save load (86.758 s, ends at frame 1135)

Label evidence: 13970460 gzread calls and 1 successful gzopen inside the gap (gzip save stream).
Interval 124.197–211.015 s, length 86.758 s; hooked exclusive 41.239 s over 33 report windows (2 straddling); unexplained 45.519 s.
Samples 262,812 in 19 delta blocks (2 straddling), 95.041 s covered, 2765 samples/s, 231 ticks/s per thread, 0 dropped; sampler tick 1556 µs mean / 77667 µs max, busy 39.6 % of the covered time.

**Hooked time**

| Operation | Calls | Incl. s | Excl. s | Mean ms | Bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `ID3DXMesh::GenerateAdjacency` | 3,585 | 18.771 | 18.771 | 5.236 | 0 |
| `D3DXCreateTextureFromFileInMemoryEx` | 749 | 7.241 | 7.241 | 9.668 | 679,014,071 |
| `inflate` | 380,599 | 4.510 | 4.510 | 0.012 | 0 |
| `CreateFileA` | 2,361 | 4.232 | 4.232 | 1.793 | 0 |
| `gzread` | 13,970,460 | 2.845 | 2.845 | 0.000 | 43,542,721 |
| `FindFirstFileA` | 2,466 | 0.974 | 0.974 | 0.395 | 0 |
| `ID3DXMesh::OptimizeInplace` | 3,585 | 0.931 | 0.931 | 0.260 | 0 |
| `ReadFile` | 103,675 | 0.698 | 0.698 | 0.007 | 411,827,255 |
| `D3DXCleanMesh` | 3,590 | 0.481 | 0.481 | 0.134 | 0 |
| `xmlReadMemory` | 790 | 0.267 | 0.267 | 0.338 | 22,531,669 |
| `FindNextFileA` | 2,199 | 0.105 | 0.105 | 0.048 | 0 |
| `D3DXCreateMesh` | 3,585 | 0.079 | 0.079 | 0.022 | 0 |

**Sampled attribution: per-thread module split**

| Slot | TID | Samples | x3ap | ntdll | wine | d3dx | zlib | xml | proxy | other | Start |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 0 | 1260 | 21,947 | 8,951 | 3,385 | 323 | 7,207 | 1,153 | 16 | 810 | 102 | 0:0x112ead |
| 1 | 2984 | 21,947 | 0 | 21,947 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xb220 |
| 2 | 1432 | 21,947 | 0 | 0 | 21,947 | 0 | 0 | 0 | 0 | 0 | 53:0xc780 |
| 3 | 844 | 21,947 | 0 | 21,527 | 411 | 0 | 0 | 0 | 0 | 9 | 49:0x38490 |
| 4 | 1172 | 21,947 | 0 | 0 | 21,947 | 0 | 0 | 0 | 0 | 0 | 4:0xd0f0 |
| 5 | 2332 | 21,947 | 0 | 21,938 | 8 | 0 | 0 | 0 | 0 | 1 | 20:0xe740 |
| 6 | 1864 | 21,947 | 0 | 0 | 21,947 | 0 | 0 | 0 | 0 | 0 | 38:0x2400 |
| 7 | 1332 | 21,947 | 0 | 21,947 | 0 | 0 | 0 | 0 | 0 | 0 | 7:0x9900 |
| 8 | 688 | 21,947 | 0 | 21,947 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 9 | 3040 | 21,947 | 0 | 21,947 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 10 | 1816 | 917 | 0 | 0 | 917 | 0 | 0 | 0 | 0 | 0 | 57:0x9f90 |
| 10 | 2192 | 2,927 | 0 | 4 | 2,923 | 0 | 0 | 0 | 0 | 0 | 57:0x9f90 |
| 11 | 1716 | 21,947 | 0 | 21,947 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 12 | 1456 | 917 | 0 | 917 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 12 | 1808 | 2,927 | 0 | 2,927 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 13 | 1248 | 917 | 0 | 0 | 917 | 0 | 0 | 0 | 0 | 0 | 59:0x13a60 |
| 13 | 2916 | 2,927 | 0 | 0 | 2,927 | 0 | 0 | 0 | 0 | 0 | 59:0x13a60 |
| 14 | 1416 | 917 | 0 | 915 | 2 | 0 | 0 | 0 | 0 | 0 | 59:0x12e30 |
| 14 | 1440 | 2,927 | 0 | 2,923 | 4 | 0 | 0 | 0 | 0 | 0 | 59:0x12e30 |
| 15 | 1336 | 2,510 | 0 | 2,510 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 16 | 3024 | 2,510 | 0 | 2,510 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 17 | 2644 | 588 | 0 | 588 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 18 | 3072 | 411 | 0 | 411 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |

All threads: x3ap 3.4 %, ntdll 64.8 %, wine 28.3 %, d3dx 2.7 %, zlib 0.4 %, xml 0.0 %, proxy 0.3 %, other 0.0 %.

**Top functions (leaf samples by containing function)**

Shares are of the engine thread (slot 0, 21,947 samples); the other sampled threads are idle Wine/audio/input waiters.

| Function start | Ghidra name | Label | Self (engine) | Incl. frame (engine) | Busiest slot | Hooked APIs in interval |
| --- | --- | --- | ---: | ---: | ---: | --- |
| `0x004e1220` | FUN_004e1220 | — | 2.8 % (610) | 3.0 % (653) | slot 0 2.8 % | — |
| `0x004e0770` | FUN_004e0770 | — | 1.9 % (412) | 1.9 % (416) | slot 0 1.9 % | — |
| `0x004dfef0` | FUN_004dfef0 | — | 1.4 % (308) | 1.6 % (342) | slot 0 1.4 % | — |
| `0x004bc1c0` | FUN_004bc1c0 | — | 1.4 % (301) | 3.3 % (724) | slot 0 1.4 % | — |
| `0x00412440` | FUN_00412440 | — | 1.0 % (214) | 1.0 % (214) | slot 0 1.0 % | — |
| `0x004c7ad0` | FUN_004c7ad0 | — | 0.3 % (56) | 0.3 % (56) | slot 0 0.3 % | — |
| `0x00523fe4` | fastzero_I | — | 0.2 % (51) | 0.3 % (76) | slot 0 0.2 % | — |
| `0x004c0150` | FUN_004c0150 | effect state manager: matrix uploads per draw | 0.2 % (37) | 0.6 % (125) | slot 0 0.2 % | — |
| `0x004bbb10` | FUN_004bbb10 | — | 0.2 % (33) | 0.5 % (107) | slot 0 0.2 % | — |
| `0x004ab880` | FUN_004ab880 | script VM / command dispatch (SE_XMLImport, ReadText) | 0.1 % (28) | 0.1 % (28) | slot 0 0.1 % | — |
| `0x004a2520` | FUN_004a2520 | — | 0.1 % (19) | 0.1 % (19) | slot 0 0.1 % | — |
| `0x004b47f0` | FUN_004b47f0 | — | 0.1 % (19) | 0.1 % (19) | slot 0 0.1 % | — |
| `0x0046e400` | FUN_0046e400 | — | 0.1 % (15) | 0.1 % (15) | slot 0 0.1 % | — |
| `0x004c7c80` | FUN_004c7c80 | — | 0.1 % (13) | 0.1 % (13) | slot 0 0.1 % | — |
| `0x0040e710` | FUN_0040e710 | — | 0.1 % (13) | 0.1 % (13) | slot 0 0.1 % | — |
| `0x004e3280` | FUN_004e3280 | — | 0.1 % (12) | 0.1 % (12) | slot 0 0.1 % | — |
| `0x004eafc0` | FUN_004eafc0 | — | 0.0 % (9) | 0.0 % (9) | slot 0 0.0 % | — |
| `0x004a84f0` | FUN_004a84f0 | — | 0.0 % (8) | 0.0 % (10) | slot 0 0.0 % | — |
| `0x004a0640` | FUN_004a0640 | — | 0.0 % (6) | 0.0 % (10) | slot 0 0.0 % | — |
| `0x00480830` | FUN_00480830 | — | 0.0 % (5) | 0.1 % (13) | slot 0 0.0 % | — |
| `0x004e9420` | FUN_004e9420 | — | 0.0 % (5) | 0.0 % (8) | slot 0 0.0 % | — |
| `0x0052b5d0` | FUN_0052b5d0 | — | 0.0 % (4) | 0.0 % (5) | slot 0 0.0 % | — |
| `0x004fae40` | gzread | — | 0.0 % (4) | 0.0 % (4) | slot 0 0.0 % | — |
| `0x004aae20` | FUN_004aae20 | — | 0.0 % (3) | 0.0 % (6) | slot 0 0.0 % | — |
| `0x004ee360` | FUN_004ee360 | — | 0.0 % (2) | 0.0 % (5) | slot 0 0.0 % | — |
| `0x004a26a0` | FUN_004a26a0 | — | 0.0 % (2) | 0.0 % (2) | slot 0 0.0 % | — |
| `0x004a7f60` | FUN_004a7f60 | — | 0.0 % (1) | 0.0 % (1) | slot 0 0.0 % | — |
| `0x004a8240` | FUN_004a8240 | — | 0.0 % (1) | 0.0 % (1) | slot 0 0.0 % | — |
| `0x0052403b` | __VEC_memzero | — | 0.0 % (0) | 16.3 % (3,581) | — | — |
| `0x004bc680` | FUN_004bc680 | mesh preparation: adjacency / clean / optimise | 0.0 % (0) | 8.3 % (1,820) | — | `ID3DXMesh::GenerateAdjacency` 3,585 calls / 18.771 s; `D3DXCleanMesh` 3,590 calls / 0.481 s; `ID3DXMesh::OptimizeInplace` 3,585 calls / 0.931 s |
| `0x004e9210` | FUN_004e9210 | read dispatcher (fread / gzread / XOR-0x33 slice) | 0.0 % (0) | 7.0 % (1,532) | — | `ReadFile` 103,675 calls / 0.698 s; `gzread` 13,970,460 calls / 2.845 s |
| `0x004dd2c0` | FUN_004dd2c0 | — | 0.0 % (0) | 6.6 % (1,451) | — | — |
| `0x00511f77` | FUN_00511f77 | — | 0.0 % (0) | 3.5 % (770) | — | — |
| `0x005112c4` | _malloc | — | 0.0 % (0) | 2.4 % (526) | — | — |
| `0x0050e1b0` | _free | — | 0.0 % (0) | 2.3 % (511) | — | — |
| `0x004bcb60` | FUN_004bcb60 | — | 0.0 % (0) | 2.3 % (509) | — | — |
| `0x0051da61` | __VEC_memcpy | — | 0.0 % (0) | 1.5 % (333) | — | — |
| `0x00517d8f` | FUN_00517d8f | — | 0.0 % (0) | 0.9 % (187) | — | — |
| `0x004cabc0` | FUN_004cabc0 | — | 0.0 % (0) | 0.8 % (179) | — | — |
| `0x0051f464` | FUN_0051f464 | — | 0.0 % (0) | 0.8 % (177) | — | — |
| (no main-module frame) | | | | 91.6 % (240,866) | | |

**Top caller pairs**

| Slot | Frame | Caller | Samples |
| ---: | --- | --- | ---: |
| 0 | `0x1240bd` __VEC_memzero | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 3,194 |
| 0 | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 1,678 |
| 0 | `0xe928b` FUN_004e9210 (read dispatcher (fread / gzread / XOR-0x33 slice)) | `0xe928b` FUN_004e9210 (read dispatcher (fread / gzread / XOR-0x33 slice)) | 1,344 |
| 0 | `0xdd6a2` FUN_004dd2c0 | `0xdd6a2` FUN_004dd2c0 | 1,334 |
| 0 | `0x11204f` FUN_00511f77 | `0x1121d1` _fread_s | 596 |
| 0 | `0xbcc30` FUN_004bcb60 | `0xbb9ac` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 500 |
| 0 | `0x124071` __VEC_memzero | `0xe928b` FUN_004e9210 (read dispatcher (fread / gzread / XOR-0x33 slice)) | 358 |
| 0 | `0x11dab3` __VEC_memcpy | `0x11204f` FUN_00511f77 | 333 |
| 0 | `0x10e21e` _free | `0xbc90a` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 242 |
| 0 | `0x11133e` _malloc | `0xe928b` FUN_004e9210 (read dispatcher (fread / gzread / XOR-0x33 slice)) | 206 |
| 0 | `0x117da2` FUN_00517d8f | `0x124d01` FUN_00524cfa | 177 |
| 0 | `0x11f665` FUN_0051f464 | `0x11f665` FUN_0051f464 | 176 |

**Candidates**

Hooked calls account for 41.239 s of the 86.758 s interval (47.5 %); 45.519 s is unexplained by any hook. The sampler recorded 262,812 samples over 95.041 s of report coverage (2765 samples/s, 231 ticks/s per thread); the dominant leaf module is ntdll at 64.8 %. 2 of 19 delta blocks straddle the interval boundary and carry samples from outside it. Function `0x004e1220` (FUN_004e1220) holds 2.8 % of the engine thread's samples as leaf (2.8 % of slot 0's samples) and 3.0 % as first main-module frame; it is not a known routine. Function `0x004e0770` (FUN_004e0770) holds 1.9 % of the engine thread's samples as leaf (1.9 % of slot 0's samples) and 1.9 % as first main-module frame; it is not a known routine. Function `0x004dfef0` (FUN_004dfef0) holds 1.4 % of the engine thread's samples as leaf (1.4 % of slot 0's samples) and 1.6 % as first main-module frame; it is not a known routine.

### Gap 3 stall 1: report stall 36.626 s

Interval 123.733–160.359 s, length 36.626 s; hooked exclusive 5.024 s over 1 report windows (0 straddling); unexplained 31.602 s.
Samples 128,815 in 9 delta blocks (2 straddling), 45.015 s covered, 2862 samples/s, 253 ticks/s per thread, 0 dropped; sampler tick 1423 µs mean / 77667 µs max, busy 39.1 % of the covered time.

**Hooked time**

| Operation | Calls | Incl. s | Excl. s | Mean ms | Bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `gzread` | 13,882,714 | 2.826 | 2.826 | 0.000 | 43,183,555 |
| `inflate` | 150,279 | 1.882 | 1.882 | 0.013 | 0 |
| `ReadFile` | 38,199 | 0.204 | 0.204 | 0.005 | 159,817,778 |
| `CreateFileA` | 189 | 0.050 | 0.050 | 0.263 | 0 |
| `D3DXLoadSurfaceFromFileInMemory` | 3 | 0.038 | 0.038 | 12.828 | 1,196,466 |
| `FindFirstFileA` | 202 | 0.014 | 0.014 | 0.068 | 0 |
| `D3DXCreateTextureFromFileInMemoryEx` | 1 | 0.008 | 0.008 | 7.998 | 1,180,058 |
| `SetFilePointer` | 564 | 0.001 | 0.001 | 0.001 | 0 |
| `FindNextFileA` | 15 | 0.000 | 0.000 | 0.033 | 0 |
| `gzopen` | 1 | 0.000 | 0.000 | 0.375 | 0 |
| `FindClose` | 6 | 0.000 | 0.000 | 0.001 | 0 |

**Sampled attribution: per-thread module split**

| Slot | TID | Samples | x3ap | ntdll | wine | d3dx | zlib | xml | proxy | other | Start |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 0 | 1260 | 11,377 | 7,005 | 1,851 | 119 | 1,049 | 641 | 0 | 635 | 77 | 0:0x112ead |
| 1 | 2984 | 11,377 | 0 | 11,377 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xb220 |
| 2 | 1432 | 11,377 | 0 | 0 | 11,377 | 0 | 0 | 0 | 0 | 0 | 53:0xc780 |
| 3 | 844 | 11,377 | 0 | 11,208 | 166 | 0 | 0 | 0 | 0 | 3 | 49:0x38490 |
| 4 | 1172 | 11,377 | 0 | 0 | 11,377 | 0 | 0 | 0 | 0 | 0 | 4:0xd0f0 |
| 5 | 2332 | 11,377 | 0 | 11,372 | 4 | 0 | 0 | 0 | 0 | 1 | 20:0xe740 |
| 6 | 1864 | 11,377 | 0 | 0 | 11,377 | 0 | 0 | 0 | 0 | 0 | 38:0x2400 |
| 7 | 1332 | 11,377 | 0 | 11,377 | 0 | 0 | 0 | 0 | 0 | 0 | 7:0x9900 |
| 8 | 688 | 11,377 | 0 | 11,377 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 9 | 3040 | 11,377 | 0 | 11,377 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 10 | 1816 | 917 | 0 | 0 | 917 | 0 | 0 | 0 | 0 | 0 | 57:0x9f90 |
| 11 | 1716 | 11,377 | 0 | 11,377 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 12 | 1456 | 917 | 0 | 917 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 13 | 1248 | 917 | 0 | 0 | 917 | 0 | 0 | 0 | 0 | 0 | 59:0x13a60 |
| 14 | 1416 | 917 | 0 | 915 | 2 | 0 | 0 | 0 | 0 | 0 | 59:0x12e30 |

All threads: x3ap 5.4 %, ntdll 64.5 %, wine 28.1 %, d3dx 0.8 %, zlib 0.5 %, xml 0.0 %, proxy 0.5 %, other 0.1 %.

**Top functions (leaf samples by containing function)**

Shares are of the engine thread (slot 0, 11,377 samples); the other sampled threads are idle Wine/audio/input waiters.

| Function start | Ghidra name | Label | Self (engine) | Incl. frame (engine) | Busiest slot | Hooked APIs in interval |
| --- | --- | --- | ---: | ---: | ---: | --- |
| `0x004e1220` | FUN_004e1220 | — | 5.4 % (610) | 5.7 % (653) | slot 0 5.4 % | — |
| `0x004e0770` | FUN_004e0770 | — | 3.6 % (412) | 3.7 % (416) | slot 0 3.6 % | — |
| `0x004dfef0` | FUN_004dfef0 | — | 2.7 % (308) | 3.0 % (342) | slot 0 2.7 % | — |
| `0x00412440` | FUN_00412440 | — | 1.8 % (201) | 1.8 % (201) | slot 0 1.8 % | — |
| `0x004c0150` | FUN_004c0150 | effect state manager: matrix uploads per draw | 0.3 % (32) | 0.8 % (96) | slot 0 0.3 % | — |
| `0x00523fe4` | fastzero_I | — | 0.2 % (24) | 0.3 % (30) | slot 0 0.2 % | — |
| `0x004c7ad0` | FUN_004c7ad0 | — | 0.1 % (17) | 0.1 % (13) | slot 0 0.1 % | — |
| `0x004b47f0` | FUN_004b47f0 | — | 0.1 % (13) | 0.1 % (13) | slot 0 0.1 % | — |
| `0x0040e710` | FUN_0040e710 | — | 0.1 % (13) | 0.1 % (13) | slot 0 0.1 % | — |
| `0x004e3280` | FUN_004e3280 | — | 0.1 % (12) | 0.1 % (12) | slot 0 0.1 % | — |
| `0x004bc1c0` | FUN_004bc1c0 | — | 0.1 % (11) | 0.4 % (41) | slot 0 0.1 % | — |
| `0x004eafc0` | FUN_004eafc0 | — | 0.1 % (9) | 0.1 % (9) | slot 0 0.1 % | — |
| `0x004c7c80` | FUN_004c7c80 | — | 0.1 % (8) | 0.1 % (8) | slot 0 0.1 % | — |
| `0x004a0640` | FUN_004a0640 | — | 0.1 % (6) | 0.1 % (10) | slot 0 0.1 % | — |
| `0x004e9420` | FUN_004e9420 | — | 0.0 % (5) | 0.1 % (8) | slot 0 0.0 % | — |
| `0x00480830` | FUN_00480830 | — | 0.0 % (5) | 0.0 % (5) | slot 0 0.0 % | — |
| `0x004fae40` | gzread | — | 0.0 % (4) | 0.0 % (4) | slot 0 0.0 % | — |
| `0x004e9210` | FUN_004e9210 | read dispatcher (fread / gzread / XOR-0x33 slice) | 0.0 % (0) | 13.3 % (1,513) | — | `ReadFile` 38,199 calls / 0.204 s; `gzread` 13,882,714 calls / 2.826 s |
| `0x004dd2c0` | FUN_004dd2c0 | — | 0.0 % (0) | 5.5 % (628) | — | — |
| `0x0052403b` | __VEC_memzero | — | 0.0 % (0) | 4.1 % (462) | — | — |
| `0x005112c4` | _malloc | — | 0.0 % (0) | 3.1 % (350) | — | — |
| `0x0051da61` | __VEC_memcpy | — | 0.0 % (0) | 2.9 % (325) | — | — |
| `0x00511f77` | FUN_00511f77 | — | 0.0 % (0) | 2.3 % (258) | — | — |
| `0x004a0880` | FUN_004a0880 | — | 0.0 % (0) | 1.5 % (171) | — | — |
| `0x004bc680` | FUN_004bc680 | mesh preparation: adjacency / clean / optimise | 0.0 % (0) | 1.4 % (164) | — | — |
| `0x0051f464` | FUN_0051f464 | — | 0.0 % (0) | 0.7 % (74) | — | — |
| `0x004b4f80` | FUN_004b4f80 | — | 0.0 % (0) | 0.5 % (61) | — | — |
| `0x0050e1b0` | _free | — | 0.0 % (0) | 0.3 % (36) | — | — |
| `0x004dc540` | FUN_004dc540 | texture loader (pck dds; image info, cube, 2D helpers) | 0.0 % (0) | 0.3 % (33) | — | `D3DXCreateTextureFromFileInMemoryEx` 1 calls / 0.008 s |
| `0x004bcb60` | FUN_004bcb60 | — | 0.0 % (0) | 0.2 % (27) | — | — |
| `0x004b51b0` | FUN_004b51b0 | — | 0.0 % (0) | 0.1 % (14) | — | — |
| `0x004db520` | FUN_004db520 | — | 0.0 % (0) | 0.1 % (14) | — | — |
| `0x00517d8f` | FUN_00517d8f | — | 0.0 % (0) | 0.1 % (10) | — | — |
| `0x00519434` | FUN_00519434 | — | 0.0 % (0) | 0.1 % (9) | — | — |
| `0x004bbb10` | FUN_004bbb10 | — | 0.0 % (0) | 0.1 % (8) | — | — |
| `0x004cabc0` | FUN_004cabc0 | — | 0.0 % (0) | 0.1 % (8) | — | — |
| `0x00512a5a` | __vsnprintf_l | — | 0.0 % (0) | 0.1 % (6) | — | — |
| `0x00527869` | FUN_00527869 | — | 0.0 % (0) | 0.0 % (5) | — | — |
| `0x004b5160` | FUN_004b5160 | — | 0.0 % (0) | 0.0 % (5) | — | — |
| `0x004b9ed0` | FUN_004b9ed0 | — | 0.0 % (0) | 0.0 % (5) | — | — |
| (no main-module frame) | | | | 91.2 % (117,438) | | |

**Top caller pairs**

| Slot | Frame | Caller | Samples |
| ---: | --- | --- | ---: |
| 0 | `0xe928b` FUN_004e9210 (read dispatcher (fread / gzread / XOR-0x33 slice)) | `0xe928b` FUN_004e9210 (read dispatcher (fread / gzread / XOR-0x33 slice)) | 1,336 |
| 0 | `0xdd6a2` FUN_004dd2c0 | `0xdd6a2` FUN_004dd2c0 | 546 |
| 0 | `0x124071` __VEC_memzero | `0xe928b` FUN_004e9210 (read dispatcher (fread / gzread / XOR-0x33 slice)) | 358 |
| 0 | `0x11dab3` __VEC_memcpy | `0x11204f` FUN_00511f77 | 325 |
| 0 | `0x11133e` _malloc | `0xe928b` FUN_004e9210 (read dispatcher (fread / gzread / XOR-0x33 slice)) | 206 |
| 0 | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 156 |
| 0 | `0x11204f` FUN_00511f77 | `0xe8da9` FUN_004e8880 (resource read) | 154 |
| 0 | `0x11133e` _malloc | `0xa0b68` FUN_004a0880 | 121 |
| 0 | `0x11204f` FUN_00511f77 | `0x1121d1` _fread_s | 104 |
| 0 | `0x1240bd` __VEC_memzero | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 104 |
| 0 | `0xe928b` FUN_004e9210 (read dispatcher (fread / gzread / XOR-0x33 slice)) | `0xb8a9c` FUN_004b89f0 | 101 |
| 0 | `0xa0a8e` FUN_004a0880 | `0xe928b` FUN_004e9210 (read dispatcher (fread / gzread / XOR-0x33 slice)) | 75 |

**Candidates**

Hooked calls account for 5.024 s of the 36.626 s interval (13.7 %); 31.602 s is unexplained by any hook. The sampler recorded 128,815 samples over 45.015 s of report coverage (2862 samples/s, 253 ticks/s per thread); the dominant leaf module is ntdll at 64.5 %. 2 of 9 delta blocks straddle the interval boundary and carry samples from outside it. Function `0x004e1220` (FUN_004e1220) holds 5.4 % of the engine thread's samples as leaf (5.4 % of slot 0's samples) and 5.7 % as first main-module frame; it is not a known routine. Function `0x004e0770` (FUN_004e0770) holds 3.6 % of the engine thread's samples as leaf (3.6 % of slot 0's samples) and 3.7 % as first main-module frame; it is not a known routine. Function `0x004dfef0` (FUN_004dfef0) holds 2.7 % of the engine thread's samples as leaf (2.7 % of slot 0's samples) and 3.0 % as first main-module frame; it is not a known routine.

### Gap 3 stall 2: report stall 3.662 s

Interval 164.569–168.231 s, length 3.662 s; hooked exclusive 3.489 s over 1 report windows (0 straddling); unexplained 0.173 s.
Samples 28,633 in 2 delta blocks (2 straddling), 10.003 s covered, 2862 samples/s, 260 ticks/s per thread, 0 dropped; sampler tick 1340 µs mean / 5779 µs max, busy 38.0 % of the covered time.

**Hooked time**

| Operation | Calls | Incl. s | Excl. s | Mean ms | Bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `ID3DXMesh::GenerateAdjacency` | 30 | 3.467 | 3.467 | 115.554 | 0 |
| `ID3DXMesh::OptimizeInplace` | 29 | 0.015 | 0.015 | 0.504 | 0 |
| `D3DXCleanMesh` | 29 | 0.006 | 0.006 | 0.221 | 0 |
| `D3DXCreateMesh` | 30 | 0.001 | 0.001 | 0.033 | 0 |

**Sampled attribution: per-thread module split**

| Slot | TID | Samples | x3ap | ntdll | wine | d3dx | zlib | xml | proxy | other | Start |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 0 | 1260 | 2,603 | 272 | 108 | 8 | 2,079 | 129 | 0 | 7 | 0 | 0:0x112ead |
| 1 | 2984 | 2,603 | 0 | 2,603 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xb220 |
| 2 | 1432 | 2,603 | 0 | 0 | 2,603 | 0 | 0 | 0 | 0 | 0 | 53:0xc780 |
| 3 | 844 | 2,603 | 0 | 2,582 | 21 | 0 | 0 | 0 | 0 | 0 | 49:0x38490 |
| 4 | 1172 | 2,603 | 0 | 0 | 2,603 | 0 | 0 | 0 | 0 | 0 | 4:0xd0f0 |
| 5 | 2332 | 2,603 | 0 | 2,603 | 0 | 0 | 0 | 0 | 0 | 0 | 20:0xe740 |
| 6 | 1864 | 2,603 | 0 | 0 | 2,603 | 0 | 0 | 0 | 0 | 0 | 38:0x2400 |
| 7 | 1332 | 2,603 | 0 | 2,603 | 0 | 0 | 0 | 0 | 0 | 0 | 7:0x9900 |
| 8 | 688 | 2,603 | 0 | 2,603 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 9 | 3040 | 2,603 | 0 | 2,603 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 11 | 1716 | 2,603 | 0 | 2,603 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |

All threads: x3ap 0.9 %, ntdll 63.9 %, wine 27.4 %, d3dx 7.3 %, zlib 0.5 %, xml 0.0 %, proxy 0.0 %, other 0.0 %.

**Top functions (leaf samples by containing function)**

Shares are of the engine thread (slot 0, 2,603 samples); the other sampled threads are idle Wine/audio/input waiters.

| Function start | Ghidra name | Label | Self (engine) | Incl. frame (engine) | Busiest slot | Hooked APIs in interval |
| --- | --- | --- | ---: | ---: | ---: | --- |
| `0x004bc1c0` | FUN_004bc1c0 | — | 1.5 % (40) | 4.2 % (110) | slot 0 1.5 % | — |
| `0x004bbb10` | FUN_004bbb10 | — | 0.2 % (4) | 0.7 % (17) | slot 0 0.2 % | — |
| `0x0052403b` | __VEC_memzero | — | 0.0 % (0) | 40.1 % (1,045) | — | — |
| `0x004dd2c0` | FUN_004dd2c0 | — | 0.0 % (0) | 24.1 % (628) | — | — |
| `0x004bc680` | FUN_004bc680 | mesh preparation: adjacency / clean / optimise | 0.0 % (0) | 11.9 % (311) | — | `ID3DXMesh::GenerateAdjacency` 30 calls / 3.467 s; `D3DXCleanMesh` 29 calls / 0.006 s; `ID3DXMesh::OptimizeInplace` 29 calls / 0.015 s |
| `0x00511f77` | FUN_00511f77 | — | 0.0 % (0) | 4.7 % (122) | — | — |
| `0x004bcb60` | FUN_004bcb60 | — | 0.0 % (0) | 2.4 % (62) | — | — |
| `0x004dc540` | FUN_004dc540 | texture loader (pck dds; image info, cube, 2D helpers) | 0.0 % (0) | 1.4 % (37) | — | — |
| `0x0051f464` | FUN_0051f464 | — | 0.0 % (0) | 1.2 % (32) | — | — |
| `0x0050e1b0` | _free | — | 0.0 % (0) | 1.1 % (29) | — | — |
| `0x0051da61` | __VEC_memcpy | — | 0.0 % (0) | 0.5 % (14) | — | — |
| `0x00519434` | FUN_00519434 | — | 0.0 % (0) | 0.4 % (11) | — | — |
| `0x00527869` | FUN_00527869 | — | 0.0 % (0) | 0.3 % (7) | — | — |
| `0x00523fe4` | fastzero_I | — | 0.0 % (0) | 0.2 % (6) | — | — |
| `0x00524aa7` | ___lock_fhandle | — | 0.0 % (0) | 0.2 % (6) | — | — |
| `0x004bb470` | FUN_004bb470 | mesh build: D3DXCreateMesh + vertex/index fill | 0.0 % (0) | 0.2 % (6) | — | `D3DXCreateMesh` 30 calls / 0.001 s |
| `0x004dfef0` | FUN_004dfef0 | — | 0.0 % (0) | 0.2 % (4) | — | — |
| `0x004f3510` | FUN_004f3510 | texture-file wrapper (.jpg, .tga) | 0.0 % (0) | 0.2 % (4) | — | — |
| `0x004e9210` | FUN_004e9210 | read dispatcher (fread / gzread / XOR-0x33 slice) | 0.0 % (0) | 0.1 % (3) | — | — |
| `0x005112c4` | _malloc | — | 0.0 % (0) | 0.1 % (2) | — | — |
| `0x004e1220` | FUN_004e1220 | — | 0.0 % (0) | 0.1 % (2) | — | — |
| `0x004bc080` | FUN_004bc080 | — | 0.0 % (0) | 0.0 % (1) | — | — |
| (no main-module frame) | | | | 90.9 % (26,030) | | |

**Top caller pairs**

| Slot | Frame | Caller | Samples |
| ---: | --- | --- | ---: |
| 0 | `0x1240bd` __VEC_memzero | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 1,043 |
| 0 | `0xdd6a2` FUN_004dd2c0 | `0xdd6a2` FUN_004dd2c0 | 546 |
| 0 | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 293 |
| 0 | `0x11204f` FUN_00511f77 | `0x1121d1` _fread_s | 107 |
| 0 | `0xbcc30` FUN_004bcb60 | `0xbb9ac` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 59 |
| 0 | `0xdc846` FUN_004dc540 (texture loader (pck dds; image info, cube, 2D helpers)) | `0xdc846` FUN_004dc540 (texture loader (pck dds; image info, cube, 2D helpers)) | 34 |
| 0 | `0x11f665` FUN_0051f464 | `0x11f665` FUN_0051f464 | 32 |
| 0 | `0x10e21e` _free | `0xbc90a` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 24 |
| 0 | `0xbc66d` FUN_004bc1c0 | `0xbb94b` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 23 |
| 0 | `0xbc299` FUN_004bc1c0 | `0xbb94b` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 18 |
| 0 | `0xbc878` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | `0xbc878` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 17 |
| 0 | `0x11204f` FUN_00511f77 | `0xe8da9` FUN_004e8880 (resource read) | 15 |

**Candidates**

Hooked calls account for 3.489 s of the 3.662 s interval (95.3 %); 0.173 s is unexplained by any hook. The sampler recorded 28,633 samples over 10.003 s of report coverage (2862 samples/s, 260 ticks/s per thread); the dominant leaf module is ntdll at 63.9 %. 2 of 2 delta blocks straddle the interval boundary and carry samples from outside it. Function `0x004bc1c0` (FUN_004bc1c0) holds 1.5 % of the engine thread's samples as leaf (1.5 % of slot 0's samples) and 4.2 % as first main-module frame; it is not a known routine. Function `0x004bbb10` (FUN_004bbb10) holds 0.2 % of the engine thread's samples as leaf (0.2 % of slot 0's samples) and 0.7 % as first main-module frame; it is not a known routine. Function `0x0052403b` (__VEC_memzero) holds 0.0 % of the engine thread's samples as leaf and 40.1 % as first main-module frame; it is not a known routine.

### Gap 3 stall 3: report stall 12.711 s

Interval 193.177–205.889 s, length 12.711 s; hooked exclusive 6.232 s over 1 report windows (0 straddling); unexplained 6.480 s.
Samples 47,155 in 4 delta blocks (2 straddling), 20.010 s covered, 2357 samples/s, 161 ticks/s per thread, 0 dropped; sampler tick 2280 µs mean / 55958 µs max, busy 42.4 % of the covered time.

**Hooked time**

| Operation | Calls | Incl. s | Excl. s | Mean ms | Bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `CreateFileA` | 1,376 | 4.044 | 4.044 | 2.939 | 0 |
| `FindFirstFileA` | 1,398 | 0.933 | 0.933 | 0.668 | 0 |
| `ID3DXMesh::GenerateAdjacency` | 154 | 0.387 | 0.387 | 2.510 | 0 |
| `xmlReadMemory` | 790 | 0.267 | 0.267 | 0.338 | 22,531,669 |
| `inflate` | 13,292 | 0.168 | 0.168 | 0.013 | 0 |
| `ReadFile` | 8,676 | 0.116 | 0.116 | 0.013 | 22,987,251 |
| `D3DXCreateTextureFromFileInMemoryEx` | 20 | 0.113 | 0.113 | 5.645 | 23,824,030 |
| `FindNextFileA` | 2,184 | 0.105 | 0.105 | 0.048 | 0 |
| `ID3DXMesh::OptimizeInplace` | 155 | 0.043 | 0.043 | 0.278 | 0 |
| `D3DXCleanMesh` | 155 | 0.023 | 0.023 | 0.147 | 0 |
| `gzread` | 87,746 | 0.019 | 0.019 | 0.000 | 359,166 |
| `SetFilePointer` | 3,430 | 0.012 | 0.012 | 0.003 | 0 |

**Sampled attribution: per-thread module split**

| Slot | TID | Samples | x3ap | ntdll | wine | d3dx | zlib | xml | proxy | other | Start |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 0 | 1260 | 3,227 | 433 | 1,105 | 69 | 1,266 | 207 | 16 | 110 | 21 | 0:0x112ead |
| 1 | 2984 | 3,227 | 0 | 3,227 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xb220 |
| 2 | 1432 | 3,227 | 0 | 0 | 3,227 | 0 | 0 | 0 | 0 | 0 | 53:0xc780 |
| 3 | 844 | 3,227 | 0 | 3,197 | 29 | 0 | 0 | 0 | 0 | 1 | 49:0x38490 |
| 4 | 1172 | 3,227 | 0 | 0 | 3,227 | 0 | 0 | 0 | 0 | 0 | 4:0xd0f0 |
| 5 | 2332 | 3,227 | 0 | 3,226 | 1 | 0 | 0 | 0 | 0 | 0 | 20:0xe740 |
| 6 | 1864 | 3,227 | 0 | 0 | 3,227 | 0 | 0 | 0 | 0 | 0 | 38:0x2400 |
| 7 | 1332 | 3,227 | 0 | 3,227 | 0 | 0 | 0 | 0 | 0 | 0 | 7:0x9900 |
| 8 | 688 | 3,227 | 0 | 3,227 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 9 | 3040 | 3,227 | 0 | 3,227 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 10 | 2192 | 2,082 | 0 | 2 | 2,080 | 0 | 0 | 0 | 0 | 0 | 57:0x9f90 |
| 11 | 1716 | 3,227 | 0 | 3,227 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 12 | 1808 | 2,082 | 0 | 2,082 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 13 | 2916 | 2,082 | 0 | 0 | 2,082 | 0 | 0 | 0 | 0 | 0 | 59:0x13a60 |
| 14 | 1440 | 2,082 | 0 | 2,082 | 0 | 0 | 0 | 0 | 0 | 0 | 59:0x12e30 |
| 15 | 1336 | 1,665 | 0 | 1,665 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 16 | 3024 | 1,665 | 0 | 1,665 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |

All threads: x3ap 0.9 %, ntdll 66.1 %, wine 29.6 %, d3dx 2.7 %, zlib 0.4 %, xml 0.0 %, proxy 0.2 %, other 0.0 %.

**Top functions (leaf samples by containing function)**

Shares are of the engine thread (slot 0, 3,227 samples); the other sampled threads are idle Wine/audio/input waiters.

| Function start | Ghidra name | Label | Self (engine) | Incl. frame (engine) | Busiest slot | Hooked APIs in interval |
| --- | --- | --- | ---: | ---: | ---: | --- |
| `0x004bc1c0` | FUN_004bc1c0 | — | 1.1 % (34) | 1.8 % (59) | slot 0 1.1 % | — |
| `0x004ab880` | FUN_004ab880 | script VM / command dispatch (SE_XMLImport, ReadText) | 0.9 % (28) | 0.9 % (28) | slot 0 0.9 % | — |
| `0x00523fe4` | fastzero_I | — | 0.8 % (27) | 1.0 % (33) | slot 0 0.8 % | — |
| `0x004a2520` | FUN_004a2520 | — | 0.6 % (19) | 0.6 % (19) | slot 0 0.6 % | — |
| `0x0046e400` | FUN_0046e400 | — | 0.5 % (15) | 0.5 % (15) | slot 0 0.5 % | — |
| `0x004aae20` | FUN_004aae20 | — | 0.1 % (3) | 0.2 % (6) | slot 0 0.1 % | — |
| `0x00412440` | FUN_00412440 | — | 0.1 % (3) | 0.1 % (3) | slot 0 0.1 % | — |
| `0x004ee360` | FUN_004ee360 | — | 0.1 % (2) | 0.2 % (5) | slot 0 0.1 % | — |
| `0x004a26a0` | FUN_004a26a0 | — | 0.1 % (2) | 0.1 % (2) | slot 0 0.1 % | — |
| `0x004a84f0` | FUN_004a84f0 | — | 0.1 % (2) | 0.1 % (2) | slot 0 0.1 % | — |
| `0x004a7f60` | FUN_004a7f60 | — | 0.0 % (1) | 0.0 % (1) | slot 0 0.0 % | — |
| `0x004a8240` | FUN_004a8240 | — | 0.0 % (1) | 0.0 % (1) | slot 0 0.0 % | — |
| `0x004dd2c0` | FUN_004dd2c0 | — | 0.0 % (0) | 22.5 % (725) | — | — |
| `0x004bc680` | FUN_004bc680 | mesh preparation: adjacency / clean / optimise | 0.0 % (0) | 9.1 % (293) | — | `ID3DXMesh::GenerateAdjacency` 154 calls / 0.387 s; `D3DXCleanMesh` 155 calls / 0.023 s; `ID3DXMesh::OptimizeInplace` 155 calls / 0.043 s |
| `0x0050e1b0` | _free | — | 0.0 % (0) | 8.1 % (263) | — | — |
| `0x00511f77` | FUN_00511f77 | — | 0.0 % (0) | 6.4 % (205) | — | — |
| `0x00517d8f` | FUN_00517d8f | — | 0.0 % (0) | 5.5 % (177) | — | — |
| `0x004cabc0` | FUN_004cabc0 | — | 0.0 % (0) | 5.3 % (171) | — | — |
| `0x005112c4` | _malloc | — | 0.0 % (0) | 5.2 % (169) | — | — |
| `0x0052403b` | __VEC_memzero | — | 0.0 % (0) | 5.2 % (169) | — | — |
| `0x00527869` | FUN_00527869 | — | 0.0 % (0) | 3.6 % (115) | — | — |
| `0x004bcb60` | FUN_004bcb60 | — | 0.0 % (0) | 2.8 % (90) | — | — |
| `0x004efcc0` | FUN_004efcc0 | handle map insert | 0.0 % (0) | 2.7 % (88) | — | — |
| `0x0051f464` | FUN_0051f464 | — | 0.0 % (0) | 1.6 % (53) | — | — |
| `0x00517bcb` | __isleadbyte_l | — | 0.0 % (0) | 1.4 % (46) | — | — |
| `0x004dc540` | FUN_004dc540 | texture loader (pck dds; image info, cube, 2D helpers) | 0.0 % (0) | 1.2 % (38) | — | `D3DXCreateTextureFromFileInMemoryEx` 20 calls / 0.113 s |
| `0x00519434` | FUN_00519434 | — | 0.0 % (0) | 1.1 % (34) | — | — |
| `0x004b8ab0` | FUN_004b8ab0 | — | 0.0 % (0) | 0.8 % (25) | — | — |
| `0x004b8920` | FUN_004b8920 | — | 0.0 % (0) | 0.6 % (18) | — | — |
| `0x004e9210` | FUN_004e9210 | read dispatcher (fread / gzread / XOR-0x33 slice) | 0.0 % (0) | 0.3 % (11) | — | `ReadFile` 8,676 calls / 0.116 s; `gzread` 87,746 calls / 0.019 s |
| `0x004bbb10` | FUN_004bbb10 | — | 0.0 % (0) | 0.3 % (10) | — | — |
| `0x00480830` | FUN_00480830 | — | 0.0 % (0) | 0.2 % (8) | — | — |
| `0x0051b6bb` | FUN_0051b6bb | — | 0.0 % (0) | 0.2 % (7) | — | — |
| `0x004e6b70` | FUN_004e6b70 | pck xml resource class (xmlReadMemory) | 0.0 % (0) | 0.2 % (7) | — | `xmlReadMemory` 790 calls / 0.267 s |
| `0x004bb470` | FUN_004bb470 | mesh build: D3DXCreateMesh + vertex/index fill | 0.0 % (0) | 0.2 % (6) | — | `D3DXCreateMesh` 154 calls / 0.003 s |
| `0x004bc9c0` | FUN_004bc9c0 | installs the cleaned mesh | 0.0 % (0) | 0.2 % (5) | — | — |
| `0x004cf460` | FUN_004cf460 | — | 0.0 % (0) | 0.2 % (5) | — | — |
| `0x004991a0` | FUN_004991a0 | — | 0.0 % (0) | 0.1 % (4) | — | — |
| `0x004e6a40` | FUN_004e6a40 | — | 0.0 % (0) | 0.1 % (4) | — | — |
| `0x004ec490` | FUN_004ec490 | — | 0.0 % (0) | 0.1 % (4) | — | — |
| (no main-module frame) | | | | 93.2 % (43,929) | | |

**Top caller pairs**

| Slot | Frame | Caller | Samples |
| ---: | --- | --- | ---: |
| 0 | `0xdd6a2` FUN_004dd2c0 | `0xdd6a2` FUN_004dd2c0 | 697 |
| 0 | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 262 |
| 0 | `0x11204f` FUN_00511f77 | `0x1121d1` _fread_s | 200 |
| 0 | `0x117da2` FUN_00517d8f | `0x124d01` FUN_00524cfa | 177 |
| 0 | `0x1240bd` __VEC_memzero | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 156 |
| 0 | `0x127b39` FUN_00527869 | `0x128039` FID_conflict:__sopen_helper | 115 |
| 0 | `0x10e21e` _free | `0x10e23d` _free | 93 |
| 0 | `0xbcc30` FUN_004bcb60 | `0xbb9ac` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 90 |
| 0 | `0x11133e` _malloc | `0xefd0e` FUN_004efcc0 (handle map insert) | 82 |
| 0 | `0xcae73` FUN_004cabc0 | `0xaf499` FUN_004ab880 (script VM / command dispatch (SE_XMLImport, ReadText)) | 58 |
| 0 | `0x11f665` FUN_0051f464 | `0x11f665` FUN_0051f464 | 52 |
| 0 | `0x10e21e` _free | `0xbc90a` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 51 |

**Candidates**

Hooked calls account for 6.232 s of the 12.711 s interval (49.0 %); 6.480 s is unexplained by any hook. The sampler recorded 47,155 samples over 20.010 s of report coverage (2357 samples/s, 161 ticks/s per thread); the dominant leaf module is ntdll at 66.1 %. 2 of 4 delta blocks straddle the interval boundary and carry samples from outside it. Function `0x004bc1c0` (FUN_004bc1c0) holds 1.1 % of the engine thread's samples as leaf (1.1 % of slot 0's samples) and 1.8 % as first main-module frame; it is not a known routine. Function `0x004ab880` (FUN_004ab880) holds 0.9 % of the engine thread's samples as leaf (0.9 % of slot 0's samples) and 0.9 % as first main-module frame; it is a known routine (script VM / command dispatch (SE_XMLImport, ReadText)). Function `0x00523fe4` (fastzero_I) holds 0.8 % of the engine thread's samples as leaf (0.8 % of slot 0's samples) and 1.0 % as first main-module frame; it is not a known routine.

### Gap 3 stall 4: report stall 3.528 s

Interval 205.889–209.416 s, length 3.528 s; hooked exclusive 0.489 s over 1 report windows (0 straddling); unexplained 3.039 s.
Samples 15,963 in 1 delta blocks (1 straddling), 5.005 s covered, 3190 samples/s, 188 ticks/s per thread, 0 dropped; sampler tick 2016 µs mean / 3682 µs max, busy 41.2 % of the covered time.

**Hooked time**

| Operation | Calls | Incl. s | Excl. s | Mean ms | Bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `inflate` | 28,026 | 0.364 | 0.364 | 0.013 | 0 |
| `D3DXCreateTextureFromFileInMemoryEx` | 74 | 0.073 | 0.073 | 0.993 | 187,272,175 |
| `ReadFile` | 7,245 | 0.035 | 0.035 | 0.005 | 29,280,768 |
| `CreateFileA` | 74 | 0.013 | 0.013 | 0.182 | 0 |
| `FindFirstFileA` | 77 | 0.003 | 0.003 | 0.034 | 0 |
| `SetFilePointer` | 222 | 0.000 | 0.000 | 0.001 | 0 |

**Sampled attribution: per-thread module split**

| Slot | TID | Samples | x3ap | ntdll | wine | d3dx | zlib | xml | proxy | other | Start |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 0 | 1260 | 939 | 66 | 99 | 7 | 700 | 62 | 1 | 3 | 1 | 0:0x112ead |
| 1 | 2984 | 939 | 0 | 939 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xb220 |
| 2 | 1432 | 939 | 0 | 0 | 939 | 0 | 0 | 0 | 0 | 0 | 53:0xc780 |
| 3 | 844 | 939 | 0 | 939 | 0 | 0 | 0 | 0 | 0 | 0 | 49:0x38490 |
| 4 | 1172 | 939 | 0 | 0 | 939 | 0 | 0 | 0 | 0 | 0 | 4:0xd0f0 |
| 5 | 2332 | 939 | 0 | 939 | 0 | 0 | 0 | 0 | 0 | 0 | 20:0xe740 |
| 6 | 1864 | 939 | 0 | 0 | 939 | 0 | 0 | 0 | 0 | 0 | 38:0x2400 |
| 7 | 1332 | 939 | 0 | 939 | 0 | 0 | 0 | 0 | 0 | 0 | 7:0x9900 |
| 8 | 688 | 939 | 0 | 939 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 9 | 3040 | 939 | 0 | 939 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 10 | 2192 | 939 | 0 | 0 | 939 | 0 | 0 | 0 | 0 | 0 | 57:0x9f90 |
| 11 | 1716 | 939 | 0 | 939 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 12 | 1808 | 939 | 0 | 939 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 13 | 2916 | 939 | 0 | 0 | 939 | 0 | 0 | 0 | 0 | 0 | 59:0x13a60 |
| 14 | 1440 | 939 | 0 | 939 | 0 | 0 | 0 | 0 | 0 | 0 | 59:0x12e30 |
| 15 | 1336 | 939 | 0 | 939 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 16 | 3024 | 939 | 0 | 939 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |

All threads: x3ap 0.4 %, ntdll 65.3 %, wine 29.5 %, d3dx 4.4 %, zlib 0.4 %, xml 0.0 %, proxy 0.0 %, other 0.0 %.

**Top functions (leaf samples by containing function)**

Shares are of the engine thread (slot 0, 939 samples); the other sampled threads are idle Wine/audio/input waiters.

| Function start | Ghidra name | Label | Self (engine) | Incl. frame (engine) | Busiest slot | Hooked APIs in interval |
| --- | --- | --- | ---: | ---: | ---: | --- |
| `0x004ab880` | FUN_004ab880 | script VM / command dispatch (SE_XMLImport, ReadText) | 2.8 % (26) | 2.8 % (26) | slot 0 2.8 % | — |
| `0x004a2520` | FUN_004a2520 | — | 2.0 % (19) | 2.0 % (19) | slot 0 2.0 % | — |
| `0x00523fe4` | fastzero_I | — | 1.1 % (10) | 1.1 % (10) | slot 0 1.1 % | — |
| `0x004dd2c0` | FUN_004dd2c0 | — | 0.0 % (0) | 74.2 % (697) | — | — |
| `0x00511f77` | FUN_00511f77 | — | 0.0 % (0) | 6.9 % (65) | — | — |
| `0x0050e1b0` | _free | — | 0.0 % (0) | 2.8 % (26) | — | — |
| `0x005112c4` | _malloc | — | 0.0 % (0) | 2.7 % (25) | — | — |
| `0x004efcc0` | FUN_004efcc0 | handle map insert | 0.0 % (0) | 2.6 % (24) | — | — |
| `0x004dc540` | FUN_004dc540 | texture loader (pck dds; image info, cube, 2D helpers) | 0.0 % (0) | 1.8 % (17) | — | `D3DXCreateTextureFromFileInMemoryEx` 74 calls / 0.073 s |
| `0x0051f464` | FUN_0051f464 | — | 0.0 % (0) | 0.6 % (6) | — | — |
| `0x00519434` | FUN_00519434 | — | 0.0 % (0) | 0.4 % (4) | — | — |
| `0x004aae20` | FUN_004aae20 | — | 0.0 % (0) | 0.3 % (3) | — | — |
| `0x004ee360` | FUN_004ee360 | — | 0.0 % (0) | 0.3 % (3) | — | — |
| `0x004cabc0` | FUN_004cabc0 | — | 0.0 % (0) | 0.2 % (2) | — | — |
| `0x004e9210` | FUN_004e9210 | read dispatcher (fread / gzread / XOR-0x33 slice) | 0.0 % (0) | 0.2 % (2) | — | `ReadFile` 7,245 calls / 0.035 s |
| `0x0051b6bb` | FUN_0051b6bb | — | 0.0 % (0) | 0.1 % (1) | — | — |
| `0x00481aa0` | FUN_00481aa0 | BOB binary body parse | 0.0 % (0) | 0.1 % (1) | — | — |
| `0x004a7630` | FUN_004a7630 | — | 0.0 % (0) | 0.1 % (1) | — | — |
| `0x004a93d0` | FUN_004a93d0 | — | 0.0 % (0) | 0.1 % (1) | — | — |
| `0x004aa1b0` | FUN_004aa1b0 | — | 0.0 % (0) | 0.1 % (1) | — | — |
| `0x004dced0` | FUN_004dced0 | — | 0.0 % (0) | 0.1 % (1) | — | — |
| `0x004ee2d0` | FUN_004ee2d0 | — | 0.0 % (0) | 0.1 % (1) | — | — |
| `0x004efd30` | FUN_004efd30 | handle map remove | 0.0 % (0) | 0.1 % (1) | — | — |
| `0x004f3510` | FUN_004f3510 | texture-file wrapper (.jpg, .tga) | 0.0 % (0) | 0.1 % (1) | — | — |
| (no main-module frame) | | | | 94.1 % (15,025) | | |

**Top caller pairs**

| Slot | Frame | Caller | Samples |
| ---: | --- | --- | ---: |
| 0 | `0xdd6a2` FUN_004dd2c0 | `0xdd6a2` FUN_004dd2c0 | 673 |
| 0 | `0x11204f` FUN_00511f77 | `0x1121d1` _fread_s | 63 |
| 0 | `0xac490` FUN_004ab880 (script VM / command dispatch (SE_XMLImport, ReadText)) | `0x10e21e` _free | 26 |
| 0 | `0xdc846` FUN_004dc540 (texture loader (pck dds; image info, cube, 2D helpers)) | `0xdc846` FUN_004dc540 (texture loader (pck dds; image info, cube, 2D helpers)) | 17 |
| 0 | `0xefd0e` FUN_004efcc0 (handle map insert) | `0xa86ff` FUN_004a8670 | 15 |
| 0 | `0x11133e` _malloc | `0xefd0e` FUN_004efcc0 (handle map insert) | 10 |
| 0 | `0x124021` fastzero_I | `0x124071` __VEC_memzero | 10 |
| 0 | `0xa2567` FUN_004a2520 | `0xa300e` FUN_004a26a0 | 8 |
| 0 | `0x10e21e` _free | `0x10e23d` _free | 7 |
| 0 | `0xa2563` FUN_004a2520 | `0xa300e` FUN_004a26a0 | 6 |
| 0 | `0x11f665` FUN_0051f464 | `0x11f665` FUN_0051f464 | 6 |
| 0 | `0xa2579` FUN_004a2520 | `0xa300e` FUN_004a26a0 | 5 |

**Candidates**

Hooked calls account for 0.489 s of the 3.528 s interval (13.9 %); 3.039 s is unexplained by any hook. The sampler recorded 15,963 samples over 5.005 s of report coverage (3190 samples/s, 188 ticks/s per thread); the dominant leaf module is ntdll at 65.3 %. 1 of 1 delta blocks straddle the interval boundary and carry samples from outside it. Function `0x004ab880` (FUN_004ab880) holds 2.8 % of the engine thread's samples as leaf (2.8 % of slot 0's samples) and 2.8 % as first main-module frame; it is a known routine (script VM / command dispatch (SE_XMLImport, ReadText)). Function `0x004a2520` (FUN_004a2520) holds 2.0 % of the engine thread's samples as leaf (2.0 % of slot 0's samples) and 2.0 % as first main-module frame; it is not a known routine. Function `0x00523fe4` (fastzero_I) holds 1.1 % of the engine thread's samples as leaf (1.1 % of slot 0's samples) and 1.1 % as first main-module frame; it is not a known routine.

## Gap 4: sector change (24.126 s, ends at frame 5455)

Label evidence: 3022 GenerateAdjacency calls with 26,367,461 B of texture input after an earlier labelled phase.
Interval 383.924–409.065 s, length 24.126 s; hooked exclusive 14.710 s over 16 report windows (2 straddling); unexplained 9.416 s.
Samples 107,057 in 6 delta blocks (2 straddling), 30.013 s covered, 3567 samples/s, 150 ticks/s per thread, 0 dropped; sampler tick 2811 µs mean / 16698 µs max, busy 45.4 % of the covered time.

**Hooked time**

| Operation | Calls | Incl. s | Excl. s | Mean ms | Bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `ID3DXMesh::GenerateAdjacency` | 3,022 | 12.815 | 12.815 | 4.240 | 0 |
| `inflate` | 62,226 | 0.756 | 0.756 | 0.012 | 0 |
| `ID3DXMesh::OptimizeInplace` | 3,022 | 0.571 | 0.571 | 0.189 | 0 |
| `D3DXCleanMesh` | 3,025 | 0.287 | 0.287 | 0.095 | 0 |
| `ReadFile` | 15,816 | 0.122 | 0.122 | 0.008 | 64,655,872 |
| `D3DXCreateMesh` | 3,022 | 0.055 | 0.055 | 0.018 | 0 |
| `D3DXCreateTextureFromFileInMemoryEx` | 30 | 0.042 | 0.042 | 1.397 | 26,367,461 |
| `D3DXCreateEffect` | 2 | 0.036 | 0.036 | 17.805 | 28,932 |
| `CreateFileA` | 80 | 0.017 | 0.017 | 0.215 | 0 |
| `FindFirstFileA` | 92 | 0.009 | 0.009 | 0.103 | 0 |
| `SetFilePointer` | 240 | 0.000 | 0.000 | 0.002 | 0 |
| `FindClose` | 2 | 0.000 | 0.000 | 0.003 | 0 |

**Sampled attribution: per-thread module split**

| Slot | TID | Samples | x3ap | ntdll | wine | d3dx | zlib | xml | proxy | other | Start |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 0 | 1260 | 4,503 | 1,776 | 285 | 68 | 2,208 | 107 | 0 | 57 | 2 | 0:0x112ead |
| 1 | 2984 | 4,503 | 0 | 4,503 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xb220 |
| 2 | 1432 | 4,503 | 0 | 0 | 4,503 | 0 | 0 | 0 | 0 | 0 | 53:0xc780 |
| 3 | 844 | 4,503 | 0 | 4,305 | 194 | 0 | 0 | 0 | 0 | 4 | 49:0x38490 |
| 4 | 1172 | 4,503 | 0 | 0 | 4,503 | 0 | 0 | 0 | 0 | 0 | 4:0xd0f0 |
| 5 | 2332 | 4,503 | 0 | 4,503 | 0 | 0 | 0 | 0 | 0 | 0 | 20:0xe740 |
| 6 | 1864 | 4,503 | 0 | 0 | 4,503 | 0 | 0 | 0 | 0 | 0 | 38:0x2400 |
| 7 | 1332 | 4,503 | 0 | 4,503 | 0 | 0 | 0 | 0 | 0 | 0 | 7:0x9900 |
| 8 | 688 | 4,503 | 0 | 4,503 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 9 | 3040 | 4,503 | 0 | 4,503 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 10 | 860 | 4,503 | 0 | 4,503 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 11 | 1716 | 4,503 | 0 | 4,503 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 12 | 1512 | 4,503 | 0 | 4,503 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 13 | 1824 | 4,503 | 0 | 4,503 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 14 | 1080 | 4,503 | 0 | 4,503 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 15 | 1336 | 4,503 | 0 | 4,503 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 16 | 3024 | 4,503 | 0 | 4,503 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 17 | 2644 | 4,503 | 0 | 4,503 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 18 | 3072 | 4,503 | 0 | 4,503 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 19 | 2488 | 4,503 | 0 | 4,503 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 20 | 1408 | 4,503 | 0 | 4,503 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 21 | 980 | 4,215 | 0 | 4,215 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 22 | 984 | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x9f90 |
| 22 | 2416 | 5 | 0 | 5 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 23 | 2016 | 4,215 | 0 | 4,215 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 24 | 1100 | 4,038 | 0 | 4,038 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 25 | 3044 | 5 | 0 | 0 | 5 | 0 | 0 | 0 | 0 | 0 | 57:0x9f90 |
| 26 | 2656 | 5 | 0 | 5 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 27 | 1352 | 5 | 0 | 0 | 5 | 0 | 0 | 0 | 0 | 0 | 59:0x13a60 |
| 28 | 2280 | 5 | 0 | 5 | 0 | 0 | 0 | 0 | 0 | 0 | 59:0x12e30 |

All threads: x3ap 1.7 %, ntdll 83.2 %, wine 12.9 %, d3dx 2.1 %, zlib 0.1 %, xml 0.0 %, proxy 0.1 %, other 0.0 %.

**Top functions (leaf samples by containing function)**

Shares are of the engine thread (slot 0, 4,503 samples); the other sampled threads are idle Wine/audio/input waiters.

| Function start | Ghidra name | Label | Self (engine) | Incl. frame (engine) | Busiest slot | Hooked APIs in interval |
| --- | --- | --- | ---: | ---: | ---: | --- |
| `0x004bc1c0` | FUN_004bc1c0 | — | 1.9 % (84) | 4.3 % (193) | slot 0 1.9 % | — |
| `0x00412440` | FUN_00412440 | — | 1.2 % (52) | 1.2 % (52) | slot 0 1.2 % | — |
| `0x004e1220` | FUN_004e1220 | — | 1.1 % (51) | 1.3 % (59) | slot 0 1.1 % | — |
| `0x004dfef0` | FUN_004dfef0 | — | 0.5 % (23) | 0.7 % (32) | slot 0 0.5 % | — |
| `0x004e0770` | FUN_004e0770 | — | 0.4 % (19) | 0.6 % (27) | slot 0 0.4 % | — |
| `0x004b0e00` | FUN_004b0e00 | — | 0.2 % (10) | 0.2 % (10) | slot 0 0.2 % | — |
| `0x0052b5d0` | FUN_0052b5d0 | — | 0.2 % (8) | 0.2 % (8) | slot 0 0.2 % | — |
| `0x0049c8b0` | FUN_0049c8b0 | — | 0.1 % (6) | 0.1 % (6) | slot 0 0.1 % | — |
| `0x0047c640` | FUN_0047c640 | view activation callee | 0.1 % (5) | 0.1 % (5) | slot 0 0.1 % | — |
| `0x004c7c80` | FUN_004c7c80 | — | 0.1 % (5) | 0.1 % (5) | slot 0 0.1 % | — |
| `0x004c0150` | FUN_004c0150 | effect state manager: matrix uploads per draw | 0.1 % (4) | 0.5 % (21) | slot 0 0.1 % | — |
| `0x004f0ad0` | FUN_004f0ad0 | — | 0.1 % (4) | 0.2 % (8) | slot 0 0.1 % | — |
| `0x004e07f0` | FUN_004e07f0 | — | 0.1 % (4) | 0.1 % (4) | slot 0 0.1 % | — |
| `0x004f0270` | FUN_004f0270 | — | 0.1 % (4) | 0.1 % (4) | slot 0 0.1 % | — |
| `0x0052403b` | __VEC_memzero | — | 0.0 % (0) | 42.9 % (1,930) | — | — |
| `0x004bcb60` | FUN_004bcb60 | — | 0.0 % (0) | 4.1 % (184) | — | — |
| `0x0050e1b0` | _free | — | 0.0 % (0) | 3.0 % (135) | — | — |
| `0x00511f77` | FUN_00511f77 | — | 0.0 % (0) | 1.7 % (75) | — | — |
| `0x004bc680` | FUN_004bc680 | mesh preparation: adjacency / clean / optimise | 0.0 % (0) | 1.6 % (70) | — | `ID3DXMesh::GenerateAdjacency` 3,022 calls / 12.815 s; `D3DXCleanMesh` 3,025 calls / 0.287 s; `ID3DXMesh::OptimizeInplace` 3,022 calls / 0.571 s |
| `0x004b4f80` | FUN_004b4f80 | — | 0.0 % (0) | 1.5 % (68) | — | — |
| `0x0051da61` | __VEC_memcpy | — | 0.0 % (0) | 0.8 % (36) | — | — |
| `0x004bbb10` | FUN_004bbb10 | — | 0.0 % (0) | 0.5 % (21) | — | — |
| `0x004c4750` | FUN_004c4750 | — | 0.0 % (0) | 0.3 % (15) | — | — |
| `0x004bb470` | FUN_004bb470 | mesh build: D3DXCreateMesh + vertex/index fill | 0.0 % (0) | 0.2 % (9) | — | `D3DXCreateMesh` 3,022 calls / 0.055 s |
| `0x004bc080` | FUN_004bc080 | — | 0.0 % (0) | 0.2 % (8) | — | — |
| `0x004bfd40` | FUN_004bfd40 | local view-projection multiply | 0.0 % (0) | 0.2 % (8) | — | — |
| `0x0051f464` | FUN_0051f464 | — | 0.0 % (0) | 0.2 % (7) | — | — |
| `0x004bf960` | FUN_004bf960 | — | 0.0 % (0) | 0.2 % (7) | — | — |
| `0x005112c4` | _malloc | — | 0.0 % (0) | 0.1 % (6) | — | — |
| `0x004bc9c0` | FUN_004bc9c0 | installs the cleaned mesh | 0.0 % (0) | 0.1 % (6) | — | — |
| `0x004bccc0` | FUN_004bccc0 | — | 0.0 % (0) | 0.1 % (6) | — | — |
| `0x004b51b0` | FUN_004b51b0 | — | 0.0 % (0) | 0.1 % (4) | — | — |
| `0x004cf460` | FUN_004cf460 | — | 0.0 % (0) | 0.1 % (4) | — | — |
| `0x004dd2c0` | FUN_004dd2c0 | — | 0.0 % (0) | 0.1 % (4) | — | — |
| `0x004e9210` | FUN_004e9210 | read dispatcher (fread / gzread / XOR-0x33 slice) | 0.0 % (0) | 0.1 % (4) | — | `ReadFile` 15,816 calls / 0.122 s |
| `0x004a84f0` | FUN_004a84f0 | — | 0.0 % (0) | 0.1 % (3) | — | — |
| `0x004efcc0` | FUN_004efcc0 | handle map insert | 0.0 % (0) | 0.1 % (3) | — | — |
| (no main-module frame) | | | | 95.8 % (102,553) | | |

**Top caller pairs**

| Slot | Frame | Caller | Samples |
| ---: | --- | --- | ---: |
| 0 | `0x1240bd` __VEC_memzero | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 1,430 |
| 0 | `0x124071` __VEC_memzero | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 499 |
| 0 | `0xbcc30` FUN_004bcb60 | `0xbb9ac` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 183 |
| 0 | `0x10e21e` _free | `0x10e23d` _free | 116 |
| 0 | `0x11204f` FUN_00511f77 | `0xe8da9` FUN_004e8880 (resource read) | 71 |
| 0 | `0xbc878` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | `0xbc878` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 48 |
| 0 | `0xb4f99` FUN_004b4f80 | `0xc403e` FUN_004c0150 (effect state manager: matrix uploads per draw) | 47 |
| 0 | `0xbc66d` FUN_004bc1c0 | `0xbb94b` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 42 |
| 0 | `0x11dab3` __VEC_memcpy | `0x11204f` FUN_00511f77 | 36 |
| 0 | `0xbc299` FUN_004bc1c0 | `0xbb94b` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 35 |
| 0 | `0xb4fad` FUN_004b4f80 | `0xb4f99` FUN_004b4f80 | 19 |
| 0 | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 19 |

**Candidates**

Hooked calls account for 14.710 s of the 24.126 s interval (61.0 %); 9.416 s is unexplained by any hook. The sampler recorded 107,057 samples over 30.013 s of report coverage (3567 samples/s, 150 ticks/s per thread); the dominant leaf module is ntdll at 83.2 %. 2 of 6 delta blocks straddle the interval boundary and carry samples from outside it. Function `0x004bc1c0` (FUN_004bc1c0) holds 1.9 % of the engine thread's samples as leaf (1.9 % of slot 0's samples) and 4.3 % as first main-module frame; it is not a known routine. Function `0x00412440` (FUN_00412440) holds 1.2 % of the engine thread's samples as leaf (1.2 % of slot 0's samples) and 1.2 % as first main-module frame; it is not a known routine. Function `0x004e1220` (FUN_004e1220) holds 1.1 % of the engine thread's samples as leaf (1.1 % of slot 0's samples) and 1.3 % as first main-module frame; it is not a known routine.

### Gap 4 stall 1: report stall 6.830 s

Interval 384.256–391.087 s, length 6.830 s; hooked exclusive 0.713 s over 1 report windows (0 straddling); unexplained 6.117 s.
Samples 53,584 in 3 delta blocks (2 straddling), 15.009 s covered, 3570 samples/s, 152 ticks/s per thread, 0 dropped; sampler tick 2775 µs mean / 15442 µs max, busy 45.2 % of the covered time.

**Hooked time**

| Operation | Calls | Incl. s | Excl. s | Mean ms | Bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `inflate` | 49,031 | 0.619 | 0.619 | 0.013 | 0 |
| `ReadFile` | 12,371 | 0.084 | 0.084 | 0.007 | 50,427,904 |
| `CreateFileA` | 34 | 0.008 | 0.008 | 0.233 | 0 |
| `FindFirstFileA` | 37 | 0.002 | 0.002 | 0.043 | 0 |
| `ID3DXMesh::GenerateAdjacency` | 5 | 0.000 | 0.000 | 0.068 | 0 |
| `ID3DXMesh::OptimizeInplace` | 5 | 0.000 | 0.000 | 0.038 | 0 |
| `SetFilePointer` | 102 | 0.000 | 0.000 | 0.002 | 0 |
| `D3DXCreateMesh` | 5 | 0.000 | 0.000 | 0.023 | 0 |
| `D3DXCleanMesh` | 5 | 0.000 | 0.000 | 0.018 | 0 |

**Sampled attribution: per-thread module split**

| Slot | TID | Samples | x3ap | ntdll | wine | d3dx | zlib | xml | proxy | other | Start |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 0 | 1260 | 2,276 | 1,322 | 139 | 31 | 667 | 89 | 0 | 27 | 1 | 0:0x112ead |
| 1 | 2984 | 2,276 | 0 | 2,276 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xb220 |
| 2 | 1432 | 2,276 | 0 | 0 | 2,276 | 0 | 0 | 0 | 0 | 0 | 53:0xc780 |
| 3 | 844 | 2,276 | 0 | 2,154 | 120 | 0 | 0 | 0 | 0 | 2 | 49:0x38490 |
| 4 | 1172 | 2,276 | 0 | 0 | 2,276 | 0 | 0 | 0 | 0 | 0 | 4:0xd0f0 |
| 5 | 2332 | 2,276 | 0 | 2,276 | 0 | 0 | 0 | 0 | 0 | 0 | 20:0xe740 |
| 6 | 1864 | 2,276 | 0 | 0 | 2,276 | 0 | 0 | 0 | 0 | 0 | 38:0x2400 |
| 7 | 1332 | 2,276 | 0 | 2,276 | 0 | 0 | 0 | 0 | 0 | 0 | 7:0x9900 |
| 8 | 688 | 2,276 | 0 | 2,276 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 9 | 3040 | 2,276 | 0 | 2,276 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 10 | 860 | 2,276 | 0 | 2,276 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 11 | 1716 | 2,276 | 0 | 2,276 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 12 | 1512 | 2,276 | 0 | 2,276 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 13 | 1824 | 2,276 | 0 | 2,276 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 14 | 1080 | 2,276 | 0 | 2,276 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 15 | 1336 | 2,276 | 0 | 2,276 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 16 | 3024 | 2,276 | 0 | 2,276 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 17 | 2644 | 2,276 | 0 | 2,276 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 18 | 3072 | 2,276 | 0 | 2,276 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 19 | 2488 | 2,276 | 0 | 2,276 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 20 | 1408 | 2,276 | 0 | 2,276 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 21 | 980 | 1,988 | 0 | 1,988 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 22 | 984 | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x9f90 |
| 23 | 2016 | 1,988 | 0 | 1,988 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 24 | 1100 | 1,811 | 0 | 1,811 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |

All threads: x3ap 2.5 %, ntdll 83.0 %, wine 13.0 %, d3dx 1.2 %, zlib 0.2 %, xml 0.0 %, proxy 0.1 %, other 0.0 %.

**Top functions (leaf samples by containing function)**

Shares are of the engine thread (slot 0, 2,276 samples); the other sampled threads are idle Wine/audio/input waiters.

| Function start | Ghidra name | Label | Self (engine) | Incl. frame (engine) | Busiest slot | Hooked APIs in interval |
| --- | --- | --- | ---: | ---: | ---: | --- |
| `0x00412440` | FUN_00412440 | — | 2.3 % (52) | 2.3 % (52) | slot 0 2.3 % | — |
| `0x004e1220` | FUN_004e1220 | — | 2.2 % (51) | 2.6 % (59) | slot 0 2.2 % | — |
| `0x004dfef0` | FUN_004dfef0 | — | 1.0 % (23) | 1.4 % (32) | slot 0 1.0 % | — |
| `0x004e0770` | FUN_004e0770 | — | 0.8 % (19) | 1.2 % (27) | slot 0 0.8 % | — |
| `0x004bc1c0` | FUN_004bc1c0 | — | 0.7 % (16) | 1.2 % (27) | slot 0 0.7 % | — |
| `0x004b0e00` | FUN_004b0e00 | — | 0.4 % (10) | 0.4 % (10) | slot 0 0.4 % | — |
| `0x0052b5d0` | FUN_0052b5d0 | — | 0.4 % (8) | 0.4 % (8) | slot 0 0.4 % | — |
| `0x0049c8b0` | FUN_0049c8b0 | — | 0.3 % (6) | 0.3 % (6) | slot 0 0.3 % | — |
| `0x0047c640` | FUN_0047c640 | view activation callee | 0.2 % (5) | 0.2 % (5) | slot 0 0.2 % | — |
| `0x004c7c80` | FUN_004c7c80 | — | 0.2 % (5) | 0.2 % (5) | slot 0 0.2 % | — |
| `0x004c0150` | FUN_004c0150 | effect state manager: matrix uploads per draw | 0.2 % (4) | 0.6 % (14) | slot 0 0.2 % | — |
| `0x004f0ad0` | FUN_004f0ad0 | — | 0.2 % (4) | 0.4 % (8) | slot 0 0.2 % | — |
| `0x004e07f0` | FUN_004e07f0 | — | 0.2 % (4) | 0.2 % (4) | slot 0 0.2 % | — |
| `0x004f0270` | FUN_004f0270 | — | 0.2 % (4) | 0.2 % (4) | slot 0 0.2 % | — |
| `0x0052403b` | __VEC_memzero | — | 0.0 % (0) | 27.0 % (615) | — | — |
| `0x00511f77` | FUN_00511f77 | — | 0.0 % (0) | 2.8 % (63) | — | — |
| `0x0050e1b0` | _free | — | 0.0 % (0) | 1.9 % (44) | — | — |
| `0x004b4f80` | FUN_004b4f80 | — | 0.0 % (0) | 1.8 % (40) | — | — |
| `0x0051da61` | __VEC_memcpy | — | 0.0 % (0) | 1.4 % (31) | — | — |
| `0x004bcb60` | FUN_004bcb60 | — | 0.0 % (0) | 0.9 % (20) | — | — |
| `0x004c4750` | FUN_004c4750 | — | 0.0 % (0) | 0.7 % (15) | — | — |
| `0x004bc680` | FUN_004bc680 | mesh preparation: adjacency / clean / optimise | 0.0 % (0) | 0.5 % (11) | — | `ID3DXMesh::GenerateAdjacency` 5 calls / 0.000 s; `D3DXCleanMesh` 5 calls / 0.000 s; `ID3DXMesh::OptimizeInplace` 5 calls / 0.000 s |
| `0x004bfd40` | FUN_004bfd40 | local view-projection multiply | 0.0 % (0) | 0.4 % (8) | — | — |
| `0x0051f464` | FUN_0051f464 | — | 0.0 % (0) | 0.3 % (7) | — | — |
| `0x004bf960` | FUN_004bf960 | — | 0.0 % (0) | 0.3 % (7) | — | — |
| `0x004bccc0` | FUN_004bccc0 | — | 0.0 % (0) | 0.3 % (6) | — | — |
| `0x004cf460` | FUN_004cf460 | — | 0.0 % (0) | 0.2 % (4) | — | — |
| `0x004dd2c0` | FUN_004dd2c0 | — | 0.0 % (0) | 0.2 % (4) | — | — |
| `0x004e9210` | FUN_004e9210 | read dispatcher (fread / gzread / XOR-0x33 slice) | 0.0 % (0) | 0.2 % (4) | — | `ReadFile` 12,371 calls / 0.084 s |
| `0x004a84f0` | FUN_004a84f0 | — | 0.0 % (0) | 0.1 % (3) | — | — |
| `0x004efcc0` | FUN_004efcc0 | handle map insert | 0.0 % (0) | 0.1 % (3) | — | — |
| `0x005112c4` | _malloc | — | 0.0 % (0) | 0.1 % (2) | — | — |
| (no main-module frame) | | | | 95.8 % (51,307) | | |

**Top caller pairs**

| Slot | Frame | Caller | Samples |
| ---: | --- | --- | ---: |
| 0 | `0x1240bd` __VEC_memzero | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 561 |
| 0 | `0x11204f` FUN_00511f77 | `0xe8da9` FUN_004e8880 (resource read) | 61 |
| 0 | `0x124071` __VEC_memzero | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 54 |
| 0 | `0x10e21e` _free | `0x10e23d` _free | 33 |
| 0 | `0x11dab3` __VEC_memcpy | `0x11204f` FUN_00511f77 | 31 |
| 0 | `0xb4f99` FUN_004b4f80 | `0xc403e` FUN_004c0150 (effect state manager: matrix uploads per draw) | 27 |
| 0 | `0xbcc30` FUN_004bcb60 | `0xbb9ac` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 20 |
| 0 | `0xc4c8e` FUN_004c4750 | `0xc4c8e` FUN_004c4750 | 15 |
| 0 | `0xb4fad` FUN_004b4f80 | `0xb4f99` FUN_004b4f80 | 13 |
| 0 | `0xb0eef` FUN_004b0e00 | `0x3af5` FUN_00403840 (main/menu loop) | 10 |
| 0 | `0xbc66d` FUN_004bc1c0 | `0xbb94b` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 10 |
| 0 | `0x1244b` FUN_00412440 | `0xe011b` FUN_004dfef0 | 8 |

**Candidates**

Hooked calls account for 0.713 s of the 6.830 s interval (10.4 %); 6.117 s is unexplained by any hook. The sampler recorded 53,584 samples over 15.009 s of report coverage (3570 samples/s, 152 ticks/s per thread); the dominant leaf module is ntdll at 83.0 %. 2 of 3 delta blocks straddle the interval boundary and carry samples from outside it. Function `0x00412440` (FUN_00412440) holds 2.3 % of the engine thread's samples as leaf (2.3 % of slot 0's samples) and 2.3 % as first main-module frame; it is not a known routine. Function `0x004e1220` (FUN_004e1220) holds 2.2 % of the engine thread's samples as leaf (2.2 % of slot 0's samples) and 2.6 % as first main-module frame; it is not a known routine. Function `0x004dfef0` (FUN_004dfef0) holds 1.0 % of the engine thread's samples as leaf (1.0 % of slot 0's samples) and 1.4 % as first main-module frame; it is not a known routine.

### Gap 4 stall 2: report stall 4.521 s

Interval 392.173–396.694 s, length 4.521 s; hooked exclusive 4.330 s over 1 report windows (0 straddling); unexplained 0.191 s.
Samples 36,072 in 2 delta blocks (2 straddling), 10.006 s covered, 3605 samples/s, 150 ticks/s per thread, 0 dropped; sampler tick 2814 µs mean / 5048 µs max, busy 45.5 % of the covered time.

**Hooked time**

| Operation | Calls | Incl. s | Excl. s | Mean ms | Bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `ID3DXMesh::GenerateAdjacency` | 28 | 4.307 | 4.307 | 153.831 | 0 |
| `ID3DXMesh::OptimizeInplace` | 27 | 0.015 | 0.015 | 0.559 | 0 |
| `D3DXCleanMesh` | 27 | 0.007 | 0.007 | 0.269 | 0 |
| `D3DXCreateMesh` | 28 | 0.001 | 0.001 | 0.027 | 0 |

**Sampled attribution: per-thread module split**

| Slot | TID | Samples | x3ap | ntdll | wine | d3dx | zlib | xml | proxy | other | Start |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 0 | 1260 | 1,503 | 267 | 29 | 6 | 1,186 | 8 | 0 | 7 | 0 | 0:0x112ead |
| 1 | 2984 | 1,503 | 0 | 1,503 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xb220 |
| 2 | 1432 | 1,503 | 0 | 0 | 1,503 | 0 | 0 | 0 | 0 | 0 | 53:0xc780 |
| 3 | 844 | 1,503 | 0 | 1,497 | 6 | 0 | 0 | 0 | 0 | 0 | 49:0x38490 |
| 4 | 1172 | 1,503 | 0 | 0 | 1,503 | 0 | 0 | 0 | 0 | 0 | 4:0xd0f0 |
| 5 | 2332 | 1,503 | 0 | 1,503 | 0 | 0 | 0 | 0 | 0 | 0 | 20:0xe740 |
| 6 | 1864 | 1,503 | 0 | 0 | 1,503 | 0 | 0 | 0 | 0 | 0 | 38:0x2400 |
| 7 | 1332 | 1,503 | 0 | 1,503 | 0 | 0 | 0 | 0 | 0 | 0 | 7:0x9900 |
| 8 | 688 | 1,503 | 0 | 1,503 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 9 | 3040 | 1,503 | 0 | 1,503 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 10 | 860 | 1,503 | 0 | 1,503 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 11 | 1716 | 1,503 | 0 | 1,503 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 12 | 1512 | 1,503 | 0 | 1,503 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 13 | 1824 | 1,503 | 0 | 1,503 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 14 | 1080 | 1,503 | 0 | 1,503 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 15 | 1336 | 1,503 | 0 | 1,503 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 16 | 3024 | 1,503 | 0 | 1,503 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 17 | 2644 | 1,503 | 0 | 1,503 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 18 | 3072 | 1,503 | 0 | 1,503 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 19 | 2488 | 1,503 | 0 | 1,503 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 20 | 1408 | 1,503 | 0 | 1,503 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 21 | 980 | 1,503 | 0 | 1,503 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 23 | 2016 | 1,503 | 0 | 1,503 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 24 | 1100 | 1,503 | 0 | 1,503 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |

All threads: x3ap 0.7 %, ntdll 83.4 %, wine 12.5 %, d3dx 3.3 %, zlib 0.0 %, xml 0.0 %, proxy 0.0 %, other 0.0 %.

**Top functions (leaf samples by containing function)**

Shares are of the engine thread (slot 0, 1,503 samples); the other sampled threads are idle Wine/audio/input waiters.

| Function start | Ghidra name | Label | Self (engine) | Incl. frame (engine) | Busiest slot | Hooked APIs in interval |
| --- | --- | --- | ---: | ---: | ---: | --- |
| `0x004bc1c0` | FUN_004bc1c0 | — | 2.7 % (41) | 5.7 % (85) | slot 0 2.7 % | — |
| `0x00412440` | FUN_00412440 | — | 0.3 % (5) | 0.3 % (5) | slot 0 0.3 % | — |
| `0x0052403b` | __VEC_memzero | — | 0.0 % (0) | 73.8 % (1,109) | — | — |
| `0x004bcb60` | FUN_004bcb60 | — | 0.0 % (0) | 3.7 % (56) | — | — |
| `0x0050e1b0` | _free | — | 0.0 % (0) | 1.5 % (22) | — | — |
| `0x004bc680` | FUN_004bc680 | mesh preparation: adjacency / clean / optimise | 0.0 % (0) | 1.3 % (19) | — | `ID3DXMesh::GenerateAdjacency` 28 calls / 4.307 s; `D3DXCleanMesh` 27 calls / 0.007 s; `ID3DXMesh::OptimizeInplace` 27 calls / 0.015 s |
| `0x004bbb10` | FUN_004bbb10 | — | 0.0 % (0) | 0.6 % (9) | — | — |
| `0x004dfef0` | FUN_004dfef0 | — | 0.0 % (0) | 0.6 % (9) | — | — |
| `0x004e1220` | FUN_004e1220 | — | 0.0 % (0) | 0.5 % (8) | — | — |
| `0x00511f77` | FUN_00511f77 | — | 0.0 % (0) | 0.3 % (4) | — | — |
| `0x0051da61` | __VEC_memcpy | — | 0.0 % (0) | 0.3 % (4) | — | — |
| `0x004bb470` | FUN_004bb470 | mesh build: D3DXCreateMesh + vertex/index fill | 0.0 % (0) | 0.2 % (3) | — | `D3DXCreateMesh` 28 calls / 0.001 s |
| `0x004efcc0` | FUN_004efcc0 | handle map insert | 0.0 % (0) | 0.2 % (3) | — | — |
| `0x005112c4` | _malloc | — | 0.0 % (0) | 0.1 % (2) | — | — |
| (no main-module frame) | | | | 95.8 % (34,569) | | |

**Top caller pairs**

| Slot | Frame | Caller | Samples |
| ---: | --- | --- | ---: |
| 0 | `0x1240bd` __VEC_memzero | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 970 |
| 0 | `0x124071` __VEC_memzero | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 139 |
| 0 | `0xbcc30` FUN_004bcb60 | `0xbb9ac` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 56 |
| 0 | `0x10e21e` _free | `0x10e23d` _free | 20 |
| 0 | `0xbc66d` FUN_004bc1c0 | `0xbb94b` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 19 |
| 0 | `0xbc878` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | `0xbc878` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 18 |
| 0 | `0xbc299` FUN_004bc1c0 | `0xbb94b` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 15 |
| 0 | `0xbc317` FUN_004bc1c0 | `0xbb94b` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 8 |
| 0 | `0xbc46d` FUN_004bc1c0 | `0xbb94b` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 7 |
| 0 | `0xbc2f6` FUN_004bc1c0 | `0xbb94b` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 4 |
| 0 | `0xbc3a2` FUN_004bc1c0 | `0xbb94b` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 4 |
| 0 | `0xbc3b7` FUN_004bc1c0 | `0xbb94b` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 4 |

**Candidates**

Hooked calls account for 4.330 s of the 4.521 s interval (95.8 %); 0.191 s is unexplained by any hook. The sampler recorded 36,072 samples over 10.006 s of report coverage (3605 samples/s, 150 ticks/s per thread); the dominant leaf module is ntdll at 83.4 %. 2 of 2 delta blocks straddle the interval boundary and carry samples from outside it. Function `0x004bc1c0` (FUN_004bc1c0) holds 2.7 % of the engine thread's samples as leaf (2.7 % of slot 0's samples) and 5.7 % as first main-module frame; it is not a known routine. Function `0x00412440` (FUN_00412440) holds 0.3 % of the engine thread's samples as leaf (0.3 % of slot 0's samples) and 0.3 % as first main-module frame; it is not a known routine. Function `0x0052403b` (__VEC_memzero) holds 0.0 % of the engine thread's samples as leaf and 73.8 % as first main-module frame; it is not a known routine.

### Gap 4 stall 3: report stall 2.261 s

Interval 397.694–399.955 s, length 2.261 s; hooked exclusive 1.828 s over 1 report windows (0 straddling); unexplained 0.433 s.
Samples 18,024 in 1 delta blocks (1 straddling), 5.000 s covered, 3604 samples/s, 150 ticks/s per thread, 0 dropped; sampler tick 2816 µs mean / 5048 µs max, busy 45.5 % of the covered time.

**Hooked time**

| Operation | Calls | Incl. s | Excl. s | Mean ms | Bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `ID3DXMesh::GenerateAdjacency` | 155 | 1.751 | 1.751 | 11.297 | 0 |
| `ID3DXMesh::OptimizeInplace` | 154 | 0.048 | 0.048 | 0.311 | 0 |
| `D3DXCleanMesh` | 154 | 0.025 | 0.025 | 0.165 | 0 |
| `D3DXCreateMesh` | 155 | 0.004 | 0.004 | 0.023 | 0 |

**Sampled attribution: per-thread module split**

| Slot | TID | Samples | x3ap | ntdll | wine | d3dx | zlib | xml | proxy | other | Start |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 0 | 1260 | 751 | 110 | 16 | 4 | 615 | 1 | 0 | 5 | 0 | 0:0x112ead |
| 1 | 2984 | 751 | 0 | 751 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xb220 |
| 2 | 1432 | 751 | 0 | 0 | 751 | 0 | 0 | 0 | 0 | 0 | 53:0xc780 |
| 3 | 844 | 751 | 0 | 746 | 5 | 0 | 0 | 0 | 0 | 0 | 49:0x38490 |
| 4 | 1172 | 751 | 0 | 0 | 751 | 0 | 0 | 0 | 0 | 0 | 4:0xd0f0 |
| 5 | 2332 | 751 | 0 | 751 | 0 | 0 | 0 | 0 | 0 | 0 | 20:0xe740 |
| 6 | 1864 | 751 | 0 | 0 | 751 | 0 | 0 | 0 | 0 | 0 | 38:0x2400 |
| 7 | 1332 | 751 | 0 | 751 | 0 | 0 | 0 | 0 | 0 | 0 | 7:0x9900 |
| 8 | 688 | 751 | 0 | 751 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 9 | 3040 | 751 | 0 | 751 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 10 | 860 | 751 | 0 | 751 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 11 | 1716 | 751 | 0 | 751 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 12 | 1512 | 751 | 0 | 751 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 13 | 1824 | 751 | 0 | 751 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 14 | 1080 | 751 | 0 | 751 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 15 | 1336 | 751 | 0 | 751 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 16 | 3024 | 751 | 0 | 751 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 17 | 2644 | 751 | 0 | 751 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 18 | 3072 | 751 | 0 | 751 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 19 | 2488 | 751 | 0 | 751 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 20 | 1408 | 751 | 0 | 751 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 21 | 980 | 751 | 0 | 751 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 23 | 2016 | 751 | 0 | 751 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 24 | 1100 | 751 | 0 | 751 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |

All threads: x3ap 0.6 %, ntdll 83.4 %, wine 12.5 %, d3dx 3.4 %, zlib 0.0 %, xml 0.0 %, proxy 0.0 %, other 0.0 %.

**Top functions (leaf samples by containing function)**

Shares are of the engine thread (slot 0, 751 samples); the other sampled threads are idle Wine/audio/input waiters.

| Function start | Ghidra name | Label | Self (engine) | Incl. frame (engine) | Busiest slot | Hooked APIs in interval |
| --- | --- | --- | ---: | ---: | ---: | --- |
| `0x004bc1c0` | FUN_004bc1c0 | — | 3.3 % (25) | 7.7 % (58) | slot 0 3.3 % | — |
| `0x0052403b` | __VEC_memzero | — | 0.0 % (0) | 74.3 % (558) | — | — |
| `0x004bcb60` | FUN_004bcb60 | — | 0.0 % (0) | 5.3 % (40) | — | — |
| `0x0050e1b0` | _free | — | 0.0 % (0) | 2.4 % (18) | — | — |
| `0x004bc680` | FUN_004bc680 | mesh preparation: adjacency / clean / optimise | 0.0 % (0) | 1.9 % (14) | — | `ID3DXMesh::GenerateAdjacency` 155 calls / 1.751 s; `D3DXCleanMesh` 154 calls / 0.025 s; `ID3DXMesh::OptimizeInplace` 154 calls / 0.048 s |
| `0x004bbb10` | FUN_004bbb10 | — | 0.0 % (0) | 1.2 % (9) | — | — |
| `0x004bb470` | FUN_004bb470 | mesh build: D3DXCreateMesh + vertex/index fill | 0.0 % (0) | 0.4 % (3) | — | `D3DXCreateMesh` 155 calls / 0.004 s |
| (no main-module frame) | | | | 95.8 % (17,273) | | |

**Top caller pairs**

| Slot | Frame | Caller | Samples |
| ---: | --- | --- | ---: |
| 0 | `0x1240bd` __VEC_memzero | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 473 |
| 0 | `0x124071` __VEC_memzero | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 85 |
| 0 | `0xbcc30` FUN_004bcb60 | `0xbb9ac` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 40 |
| 0 | `0x10e21e` _free | `0x10e23d` _free | 16 |
| 0 | `0xbc878` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | `0xbc878` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 13 |
| 0 | `0xbc299` FUN_004bc1c0 | `0xbb94b` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 9 |
| 0 | `0xbc66d` FUN_004bc1c0 | `0xbb94b` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 9 |
| 0 | `0xbc46d` FUN_004bc1c0 | `0xbb94b` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 7 |
| 0 | `0xbc317` FUN_004bc1c0 | `0xbb94b` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 5 |
| 0 | `0xbc3b7` FUN_004bc1c0 | `0xbb94b` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 4 |
| 0 | `0xbb72f` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | `0xbb72f` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 3 |
| 0 | `0xbbcc2` FUN_004bbb10 | `0xbc1ae` FUN_004bc080 | 3 |

**Candidates**

Hooked calls account for 1.828 s of the 2.261 s interval (80.9 %); 0.433 s is unexplained by any hook. The sampler recorded 18,024 samples over 5.000 s of report coverage (3604 samples/s, 150 ticks/s per thread); the dominant leaf module is ntdll at 83.4 %. 1 of 1 delta blocks straddle the interval boundary and carry samples from outside it. Function `0x004bc1c0` (FUN_004bc1c0) holds 3.3 % of the engine thread's samples as leaf (3.3 % of slot 0's samples) and 7.7 % as first main-module frame; it is not a known routine. Function `0x0052403b` (__VEC_memzero) holds 0.0 % of the engine thread's samples as leaf and 74.3 % as first main-module frame; it is not a known routine. Function `0x004bcb60` (FUN_004bcb60) holds 0.0 % of the engine thread's samples as leaf and 5.3 % as first main-module frame; it is not a known routine.

## Gap 5: menu load (32.826 s, ends at frame 6384)

Label evidence: 1017 GenerateAdjacency calls and 298,138,122 B of 2D texture-helper input match the main-menu work vector.
Interval 437.990–471.572 s, length 32.826 s; hooked exclusive 8.912 s over 14 report windows (2 straddling); unexplained 23.914 s.
Samples 121,334 in 7 delta blocks (1 straddling), 35.150 s covered, 3452 samples/s, 119 ticks/s per thread, 0 dropped; sampler tick 3391 µs mean / 30179 µs max, busy 43.6 % of the covered time.

**Hooked time**

| Operation | Calls | Incl. s | Excl. s | Mean ms | Bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `ID3DXMesh::GenerateAdjacency` | 1,017 | 3.587 | 3.587 | 3.527 | 0 |
| `D3DXCreateTextureFromFileInMemoryEx` | 459 | 2.676 | 2.676 | 5.830 | 298,138,122 |
| `inflate` | 166,017 | 1.828 | 1.828 | 0.011 | 0 |
| `ReadFile` | 43,377 | 0.278 | 0.278 | 0.006 | 183,504,009 |
| `ID3DXMesh::OptimizeInplace` | 1,017 | 0.219 | 0.219 | 0.216 | 0 |
| `D3DXCleanMesh` | 1,023 | 0.096 | 0.096 | 0.094 | 0 |
| `CreateFileA` | 583 | 0.088 | 0.088 | 0.152 | 0 |
| `D3DXLoadSurfaceFromFileInMemory` | 3 | 0.058 | 0.058 | 19.414 | 2,480,817 |
| `xmlReadMemory` | 2 | 0.024 | 0.024 | 12.034 | 3,688,819 |
| `FindFirstFileA` | 685 | 0.021 | 0.021 | 0.030 | 0 |
| `D3DXCreateMesh` | 1,017 | 0.019 | 0.019 | 0.019 | 0 |
| `D3DXCreateCubeTextureFromFileInMemoryEx` | 9 | 0.015 | 0.015 | 1.675 | 1,509,024 |

**Sampled attribution: per-thread module split**

| Slot | TID | Samples | x3ap | ntdll | wine | d3dx | zlib | xml | proxy | other | Start |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 0 | 1260 | 4,193 | 2,812 | 304 | 75 | 773 | 189 | 2 | 35 | 3 | 0:0x112ead |
| 1 | 2984 | 4,193 | 0 | 4,193 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xb220 |
| 2 | 1432 | 4,193 | 0 | 0 | 4,193 | 0 | 0 | 0 | 0 | 0 | 53:0xc780 |
| 3 | 844 | 4,193 | 0 | 4,140 | 52 | 0 | 0 | 0 | 0 | 1 | 49:0x38490 |
| 4 | 1172 | 4,193 | 0 | 0 | 4,193 | 0 | 0 | 0 | 0 | 0 | 4:0xd0f0 |
| 5 | 2332 | 4,193 | 0 | 4,192 | 1 | 0 | 0 | 0 | 0 | 0 | 20:0xe740 |
| 6 | 1864 | 4,193 | 0 | 0 | 4,193 | 0 | 0 | 0 | 0 | 0 | 38:0x2400 |
| 7 | 1332 | 4,193 | 0 | 4,193 | 0 | 0 | 0 | 0 | 0 | 0 | 7:0x9900 |
| 8 | 688 | 4,193 | 0 | 4,193 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 9 | 3040 | 4,193 | 0 | 4,193 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 10 | 860 | 4,193 | 0 | 4,193 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 11 | 1716 | 4,193 | 0 | 4,193 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 12 | 1512 | 4,193 | 0 | 4,193 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 13 | 1824 | 4,193 | 0 | 4,193 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 14 | 1080 | 4,193 | 0 | 4,193 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 15 | 1336 | 4,193 | 0 | 4,193 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 16 | 3024 | 4,193 | 0 | 4,193 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 17 | 2644 | 4,193 | 0 | 4,193 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 18 | 3072 | 4,193 | 0 | 4,193 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 19 | 2488 | 4,193 | 0 | 4,193 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 20 | 1408 | 4,193 | 0 | 4,193 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 21 | 980 | 4,193 | 0 | 4,193 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 22 | 2416 | 4,193 | 0 | 4,193 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 23 | 2016 | 4,193 | 0 | 4,193 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 24 | 1100 | 4,193 | 0 | 4,193 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 25 | 1072 | 3,438 | 0 | 3,438 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 25 | 3044 | 374 | 0 | 0 | 374 | 0 | 0 | 0 | 0 | 0 | 57:0x9f90 |
| 26 | 2656 | 374 | 0 | 374 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 27 | 1352 | 374 | 0 | 0 | 374 | 0 | 0 | 0 | 0 | 0 | 59:0x13a60 |
| 28 | 2280 | 374 | 0 | 373 | 1 | 0 | 0 | 0 | 0 | 0 | 59:0x12e30 |
| 29 | 2944 | 4,193 | 0 | 4,193 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 30 | 2780 | 3,691 | 0 | 3,691 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 31 | 2296 | 3,691 | 0 | 3,691 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |

All threads: x3ap 2.3 %, ntdll 85.8 %, wine 11.1 %, d3dx 0.6 %, zlib 0.2 %, xml 0.0 %, proxy 0.0 %, other 0.0 %.

**Top functions (leaf samples by containing function)**

Shares are of the engine thread (slot 0, 4,193 samples); the other sampled threads are idle Wine/audio/input waiters.

| Function start | Ghidra name | Label | Self (engine) | Incl. frame (engine) | Busiest slot | Hooked APIs in interval |
| --- | --- | --- | ---: | ---: | ---: | --- |
| `0x004e1220` | FUN_004e1220 | — | 4.8 % (201) | 5.2 % (220) | slot 0 4.8 % | — |
| `0x00412440` | FUN_00412440 | — | 1.8 % (75) | 1.9 % (79) | slot 0 1.8 % | — |
| `0x004e0770` | FUN_004e0770 | — | 1.5 % (64) | 2.0 % (84) | slot 0 1.5 % | — |
| `0x004dfef0` | FUN_004dfef0 | — | 1.1 % (48) | 1.4 % (60) | slot 0 1.1 % | — |
| `0x00523fe4` | fastzero_I | — | 0.4 % (18) | 0.4 % (18) | slot 0 0.4 % | — |
| `0x004bc680` | FUN_004bc680 | mesh preparation: adjacency / clean / optimise | 0.0 % (0) | 7.1 % (299) | — | `ID3DXMesh::GenerateAdjacency` 1,017 calls / 3.587 s; `D3DXCleanMesh` 1,023 calls / 0.096 s; `ID3DXMesh::OptimizeInplace` 1,017 calls / 0.219 s |
| `0x00524aa7` | ___lock_fhandle | — | 0.0 % (0) | 4.4 % (186) | — | — |
| `0x0050e1b0` | _free | — | 0.0 % (0) | 4.2 % (177) | — | — |
| `0x00511f77` | FUN_00511f77 | — | 0.0 % (0) | 4.1 % (173) | — | — |
| `0x0052403b` | __VEC_memzero | — | 0.0 % (0) | 3.1 % (131) | — | — |
| `0x004dd2c0` | FUN_004dd2c0 | — | 0.0 % (0) | 1.4 % (59) | — | — |
| `0x004bcb60` | FUN_004bcb60 | — | 0.0 % (0) | 1.3 % (54) | — | — |
| `0x0051f464` | FUN_0051f464 | — | 0.0 % (0) | 1.0 % (43) | — | — |
| `0x0051da61` | __VEC_memcpy | — | 0.0 % (0) | 0.7 % (31) | — | — |
| `0x004cf460` | FUN_004cf460 | — | 0.0 % (0) | 0.7 % (30) | — | — |
| `0x004b4f80` | FUN_004b4f80 | — | 0.0 % (0) | 0.6 % (27) | — | — |
| `0x004dc540` | FUN_004dc540 | texture loader (pck dds; image info, cube, 2D helpers) | 0.0 % (0) | 0.6 % (25) | — | `D3DXCreateTextureFromFileInMemoryEx` 459 calls / 2.676 s; `D3DXCreateCubeTextureFromFileInMemoryEx` 9 calls / 0.015 s |
| `0x004b4600` | FUN_004b4600 | — | 0.0 % (0) | 0.2 % (10) | — | — |
| `0x004bc1c0` | FUN_004bc1c0 | — | 0.0 % (0) | 0.2 % (8) | — | — |
| `0x00517d8f` | FUN_00517d8f | — | 0.0 % (0) | 0.2 % (7) | — | — |
| `0x004bbb10` | FUN_004bbb10 | — | 0.0 % (0) | 0.1 % (5) | — | — |
| `0x00527869` | FUN_00527869 | — | 0.0 % (0) | 0.1 % (4) | — | — |
| `0x004c0150` | FUN_004c0150 | effect state manager: matrix uploads per draw | 0.0 % (0) | 0.1 % (4) | — | — |
| `0x004c4750` | FUN_004c4750 | — | 0.0 % (0) | 0.1 % (4) | — | — |
| `0x004e9210` | FUN_004e9210 | read dispatcher (fread / gzread / XOR-0x33 slice) | 0.0 % (0) | 0.1 % (4) | — | `ReadFile` 43,377 calls / 0.278 s |
| `0x004e9e20` | FUN_004e9e20 | — | 0.0 % (0) | 0.1 % (4) | — | — |
| `0x004efcc0` | FUN_004efcc0 | handle map insert | 0.0 % (0) | 0.1 % (4) | — | — |
| `0x005112c4` | _malloc | — | 0.0 % (0) | 0.1 % (3) | — | — |
| `0x004b8920` | FUN_004b8920 | — | 0.0 % (0) | 0.1 % (3) | — | — |
| `0x004c7ad0` | FUN_004c7ad0 | — | 0.0 % (0) | 0.1 % (3) | — | — |
| (no main-module frame) | | | | 96.5 % (117,141) | | |

**Top caller pairs**

| Slot | Frame | Caller | Samples |
| ---: | --- | --- | ---: |
| 0 | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 289 |
| 0 | `0x124b30` ___lock_fhandle | `0x124b38` ___lock_fhandle | 186 |
| 0 | `0x11204f` FUN_00511f77 | `0x1121d1` _fread_s | 124 |
| 0 | `0x1240bd` __VEC_memzero | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 91 |
| 0 | `0x10e21e` _free | `0x10e23d` _free | 77 |
| 0 | `0xbcc30` FUN_004bcb60 | `0xbb9ac` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 54 |
| 0 | `0xdd6a2` FUN_004dd2c0 | `0xdd6a2` FUN_004dd2c0 | 53 |
| 0 | `0x11204f` FUN_00511f77 | `0xe8da9` FUN_004e8880 (resource read) | 49 |
| 0 | `0x124071` __VEC_memzero | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 42 |
| 0 | `0x11f665` FUN_0051f464 | `0x11f665` FUN_0051f464 | 41 |
| 0 | `0x11dab3` __VEC_memcpy | `0x11204f` FUN_00511f77 | 30 |
| 0 | `0xb4f99` FUN_004b4f80 | `0xc403e` FUN_004c0150 (effect state manager: matrix uploads per draw) | 27 |

**Candidates**

Hooked calls account for 8.912 s of the 32.826 s interval (27.1 %); 23.914 s is unexplained by any hook. The sampler recorded 121,334 samples over 35.150 s of report coverage (3452 samples/s, 119 ticks/s per thread); the dominant leaf module is ntdll at 85.8 %. 1 of 7 delta blocks straddle the interval boundary and carry samples from outside it. Function `0x004e1220` (FUN_004e1220) holds 4.8 % of the engine thread's samples as leaf (4.8 % of slot 0's samples) and 5.2 % as first main-module frame; it is not a known routine. Function `0x00412440` (FUN_00412440) holds 1.8 % of the engine thread's samples as leaf (1.8 % of slot 0's samples) and 1.9 % as first main-module frame; it is not a known routine. Function `0x004e0770` (FUN_004e0770) holds 1.5 % of the engine thread's samples as leaf (1.5 % of slot 0's samples) and 2.0 % as first main-module frame; it is not a known routine.

### Gap 5 stall 1: report stall 18.209 s

Interval 438.561–456.770 s, length 18.209 s; hooked exclusive 0.469 s over 1 report windows (0 straddling); unexplained 17.740 s.
Samples 86,940 in 5 delta blocks (2 straddling), 25.097 s covered, 3464 samples/s, 120 ticks/s per thread, 0 dropped; sampler tick 3372 µs mean / 30179 µs max, busy 43.6 % of the covered time.

**Hooked time**

| Operation | Calls | Incl. s | Excl. s | Mean ms | Bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `inflate` | 24,325 | 0.290 | 0.290 | 0.012 | 0 |
| `D3DXLoadSurfaceFromFileInMemory` | 3 | 0.058 | 0.058 | 19.414 | 2,480,817 |
| `ReadFile` | 6,280 | 0.040 | 0.040 | 0.006 | 34,774,665 |
| `D3DXCreateTextureFromFileInMemoryEx` | 16 | 0.033 | 0.033 | 2.092 | 11,805,774 |
| `xmlReadMemory` | 2 | 0.024 | 0.024 | 12.034 | 3,688,819 |
| `CreateFileA` | 64 | 0.006 | 0.006 | 0.099 | 0 |
| `ID3DXMesh::GenerateAdjacency` | 59 | 0.006 | 0.006 | 0.099 | 0 |
| `FindFirstFileA` | 103 | 0.005 | 0.005 | 0.047 | 0 |
| `ID3DXMesh::OptimizeInplace` | 59 | 0.003 | 0.003 | 0.053 | 0 |
| `D3DXCleanMesh` | 59 | 0.001 | 0.001 | 0.022 | 0 |
| `D3DXCreateMesh` | 59 | 0.001 | 0.001 | 0.015 | 0 |
| `SetFilePointer` | 191 | 0.000 | 0.000 | 0.001 | 0 |

**Sampled attribution: per-thread module split**

| Slot | TID | Samples | x3ap | ntdll | wine | d3dx | zlib | xml | proxy | other | Start |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 0 | 1260 | 3,007 | 2,248 | 216 | 66 | 396 | 54 | 2 | 24 | 1 | 0:0x112ead |
| 1 | 2984 | 3,007 | 0 | 3,007 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xb220 |
| 2 | 1432 | 3,007 | 0 | 0 | 3,007 | 0 | 0 | 0 | 0 | 0 | 53:0xc780 |
| 3 | 844 | 3,007 | 0 | 2,961 | 45 | 0 | 0 | 0 | 0 | 1 | 49:0x38490 |
| 4 | 1172 | 3,007 | 0 | 0 | 3,007 | 0 | 0 | 0 | 0 | 0 | 4:0xd0f0 |
| 5 | 2332 | 3,007 | 0 | 3,007 | 0 | 0 | 0 | 0 | 0 | 0 | 20:0xe740 |
| 6 | 1864 | 3,007 | 0 | 0 | 3,007 | 0 | 0 | 0 | 0 | 0 | 38:0x2400 |
| 7 | 1332 | 3,007 | 0 | 3,007 | 0 | 0 | 0 | 0 | 0 | 0 | 7:0x9900 |
| 8 | 688 | 3,007 | 0 | 3,007 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 9 | 3040 | 3,007 | 0 | 3,007 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 10 | 860 | 3,007 | 0 | 3,007 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 11 | 1716 | 3,007 | 0 | 3,007 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 12 | 1512 | 3,007 | 0 | 3,007 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 13 | 1824 | 3,007 | 0 | 3,007 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 14 | 1080 | 3,007 | 0 | 3,007 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 15 | 1336 | 3,007 | 0 | 3,007 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 16 | 3024 | 3,007 | 0 | 3,007 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 17 | 2644 | 3,007 | 0 | 3,007 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 18 | 3072 | 3,007 | 0 | 3,007 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 19 | 2488 | 3,007 | 0 | 3,007 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 20 | 1408 | 3,007 | 0 | 3,007 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 21 | 980 | 3,007 | 0 | 3,007 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 22 | 2416 | 3,007 | 0 | 3,007 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 23 | 2016 | 3,007 | 0 | 3,007 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 24 | 1100 | 3,007 | 0 | 3,007 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 25 | 1072 | 2,252 | 0 | 2,252 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 25 | 3044 | 374 | 0 | 0 | 374 | 0 | 0 | 0 | 0 | 0 | 57:0x9f90 |
| 26 | 2656 | 374 | 0 | 374 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 27 | 1352 | 374 | 0 | 0 | 374 | 0 | 0 | 0 | 0 | 0 | 59:0x13a60 |
| 28 | 2280 | 374 | 0 | 373 | 1 | 0 | 0 | 0 | 0 | 0 | 59:0x12e30 |
| 29 | 2944 | 3,007 | 0 | 3,007 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 30 | 2780 | 2,505 | 0 | 2,505 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 31 | 2296 | 2,505 | 0 | 2,505 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |

All threads: x3ap 2.6 %, ntdll 85.5 %, wine 11.4 %, d3dx 0.5 %, zlib 0.1 %, xml 0.0 %, proxy 0.0 %, other 0.0 %.

**Top functions (leaf samples by containing function)**

Shares are of the engine thread (slot 0, 3,007 samples); the other sampled threads are idle Wine/audio/input waiters.

| Function start | Ghidra name | Label | Self (engine) | Incl. frame (engine) | Busiest slot | Hooked APIs in interval |
| --- | --- | --- | ---: | ---: | ---: | --- |
| `0x004e1220` | FUN_004e1220 | — | 6.4 % (193) | 6.9 % (206) | slot 0 6.4 % | — |
| `0x004e0770` | FUN_004e0770 | — | 2.1 % (64) | 2.2 % (67) | slot 0 2.1 % | — |
| `0x00412440` | FUN_00412440 | — | 1.9 % (57) | 2.0 % (61) | slot 0 1.9 % | — |
| `0x004dfef0` | FUN_004dfef0 | — | 1.6 % (48) | 1.8 % (54) | slot 0 1.6 % | — |
| `0x00523fe4` | fastzero_I | — | 0.4 % (11) | 0.4 % (11) | slot 0 0.4 % | — |
| `0x004bc680` | FUN_004bc680 | mesh preparation: adjacency / clean / optimise | 0.0 % (0) | 5.6 % (167) | — | `ID3DXMesh::GenerateAdjacency` 59 calls / 0.006 s; `D3DXCleanMesh` 59 calls / 0.001 s; `ID3DXMesh::OptimizeInplace` 59 calls / 0.003 s |
| `0x0050e1b0` | _free | — | 0.0 % (0) | 5.4 % (161) | — | — |
| `0x00524aa7` | ___lock_fhandle | — | 0.0 % (0) | 4.2 % (125) | — | — |
| `0x00511f77` | FUN_00511f77 | — | 0.0 % (0) | 1.8 % (54) | — | — |
| `0x0052403b` | __VEC_memzero | — | 0.0 % (0) | 1.7 % (50) | — | — |
| `0x004cf460` | FUN_004cf460 | — | 0.0 % (0) | 1.0 % (30) | — | — |
| `0x004bcb60` | FUN_004bcb60 | — | 0.0 % (0) | 1.0 % (29) | — | — |
| `0x004b4f80` | FUN_004b4f80 | — | 0.0 % (0) | 0.9 % (27) | — | — |
| `0x004b4600` | FUN_004b4600 | — | 0.0 % (0) | 0.3 % (10) | — | — |
| `0x00517d8f` | FUN_00517d8f | — | 0.0 % (0) | 0.2 % (7) | — | — |
| `0x0051f464` | FUN_0051f464 | — | 0.0 % (0) | 0.2 % (7) | — | — |
| `0x0051da61` | __VEC_memcpy | — | 0.0 % (0) | 0.2 % (5) | — | — |
| `0x004bbb10` | FUN_004bbb10 | — | 0.0 % (0) | 0.2 % (5) | — | — |
| `0x004c0150` | FUN_004c0150 | effect state manager: matrix uploads per draw | 0.0 % (0) | 0.1 % (4) | — | — |
| `0x004c4750` | FUN_004c4750 | — | 0.0 % (0) | 0.1 % (4) | — | — |
| `0x004e9e20` | FUN_004e9e20 | — | 0.0 % (0) | 0.1 % (4) | — | — |
| `0x004efcc0` | FUN_004efcc0 | handle map insert | 0.0 % (0) | 0.1 % (4) | — | — |
| `0x004b8920` | FUN_004b8920 | — | 0.0 % (0) | 0.1 % (3) | — | — |
| `0x004c7ad0` | FUN_004c7ad0 | — | 0.0 % (0) | 0.1 % (3) | — | — |
| `0x004dc540` | FUN_004dc540 | texture loader (pck dds; image info, cube, 2D helpers) | 0.0 % (0) | 0.1 % (3) | — | `D3DXCreateTextureFromFileInMemoryEx` 16 calls / 0.033 s |
| `0x004bc1c0` | FUN_004bc1c0 | — | 0.0 % (0) | 0.1 % (2) | — | — |
| (no main-module frame) | | | | 96.5 % (83,933) | | |

**Top caller pairs**

| Slot | Frame | Caller | Samples |
| ---: | --- | --- | ---: |
| 0 | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 161 |
| 0 | `0x124b30` ___lock_fhandle | `0x124b38` ___lock_fhandle | 125 |
| 0 | `0x10e21e` _free | `0x10e23d` _free | 73 |
| 0 | `0x1240bd` __VEC_memzero | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 50 |
| 0 | `0x11204f` FUN_00511f77 | `0xe8da9` FUN_004e8880 (resource read) | 31 |
| 0 | `0xbcc30` FUN_004bcb60 | `0xbb9ac` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 29 |
| 0 | `0xb4f99` FUN_004b4f80 | `0xc403e` FUN_004c0150 (effect state manager: matrix uploads per draw) | 27 |
| 0 | `0xcf589` FUN_004cf460 | `0x981d8` FUN_00498140 | 25 |
| 0 | `0x11204f` FUN_00511f77 | `0x1121d1` _fread_s | 23 |
| 0 | `0x10e23d` _free | `0x10e23d` _free | 22 |
| 0 | `0x10e23d` _free | `0x10e21e` _free | 21 |
| 0 | `0xe00ed` FUN_004dfef0 | `0xe0e7d` FUN_004e0e70 | 18 |

**Candidates**

Hooked calls account for 0.469 s of the 18.209 s interval (2.6 %); 17.740 s is unexplained by any hook. The sampler recorded 86,940 samples over 25.097 s of report coverage (3464 samples/s, 120 ticks/s per thread); the dominant leaf module is ntdll at 85.5 %. 2 of 5 delta blocks straddle the interval boundary and carry samples from outside it. Function `0x004e1220` (FUN_004e1220) holds 6.4 % of the engine thread's samples as leaf (6.4 % of slot 0's samples) and 6.9 % as first main-module frame; it is not a known routine. Function `0x004e0770` (FUN_004e0770) holds 2.1 % of the engine thread's samples as leaf (2.1 % of slot 0's samples) and 2.2 % as first main-module frame; it is not a known routine. Function `0x00412440` (FUN_00412440) holds 1.9 % of the engine thread's samples as leaf (1.9 % of slot 0's samples) and 2.0 % as first main-module frame; it is not a known routine.

### Gap 5 stall 2: report stall 2.220 s

Interval 458.771–460.991 s, length 2.220 s; hooked exclusive 0.489 s over 1 report windows (0 straddling); unexplained 1.731 s.
Samples 34,452 in 2 delta blocks (2 straddling), 10.045 s covered, 3430 samples/s, 118 ticks/s per thread, 0 dropped; sampler tick 3431 µs mean / 6361 µs max, busy 43.7 % of the covered time.

**Hooked time**

| Operation | Calls | Incl. s | Excl. s | Mean ms | Bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `ID3DXMesh::GenerateAdjacency` | 95 | 0.415 | 0.415 | 4.365 | 0 |
| `inflate` | 3,469 | 0.042 | 0.042 | 0.012 | 0 |
| `ID3DXMesh::OptimizeInplace` | 95 | 0.017 | 0.017 | 0.184 | 0 |
| `ReadFile` | 871 | 0.007 | 0.007 | 0.007 | 3,560,448 |
| `D3DXCleanMesh` | 96 | 0.006 | 0.006 | 0.067 | 0 |
| `D3DXCreateMesh` | 95 | 0.002 | 0.002 | 0.017 | 0 |
| `CreateFileA` | 1 | 0.000 | 0.000 | 0.249 | 0 |
| `FindFirstFileA` | 1 | 0.000 | 0.000 | 0.062 | 0 |
| `SetFilePointer` | 3 | 0.000 | 0.000 | 0.002 | 0 |

**Sampled attribution: per-thread module split**

| Slot | TID | Samples | x3ap | ntdll | wine | d3dx | zlib | xml | proxy | other | Start |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 0 | 1260 | 1,188 | 438 | 55 | 10 | 594 | 79 | 0 | 11 | 1 | 0:0x112ead |
| 1 | 2984 | 1,188 | 0 | 1,188 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xb220 |
| 2 | 1432 | 1,188 | 0 | 0 | 1,188 | 0 | 0 | 0 | 0 | 0 | 53:0xc780 |
| 3 | 844 | 1,188 | 0 | 1,179 | 9 | 0 | 0 | 0 | 0 | 0 | 49:0x38490 |
| 4 | 1172 | 1,188 | 0 | 0 | 1,188 | 0 | 0 | 0 | 0 | 0 | 4:0xd0f0 |
| 5 | 2332 | 1,188 | 0 | 1,187 | 1 | 0 | 0 | 0 | 0 | 0 | 20:0xe740 |
| 6 | 1864 | 1,188 | 0 | 0 | 1,188 | 0 | 0 | 0 | 0 | 0 | 38:0x2400 |
| 7 | 1332 | 1,188 | 0 | 1,188 | 0 | 0 | 0 | 0 | 0 | 0 | 7:0x9900 |
| 8 | 688 | 1,188 | 0 | 1,188 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 9 | 3040 | 1,188 | 0 | 1,188 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 10 | 860 | 1,188 | 0 | 1,188 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 11 | 1716 | 1,188 | 0 | 1,188 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 12 | 1512 | 1,188 | 0 | 1,188 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 13 | 1824 | 1,188 | 0 | 1,188 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 14 | 1080 | 1,188 | 0 | 1,188 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 15 | 1336 | 1,188 | 0 | 1,188 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 16 | 3024 | 1,188 | 0 | 1,188 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 17 | 2644 | 1,188 | 0 | 1,188 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 18 | 3072 | 1,188 | 0 | 1,188 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 19 | 2488 | 1,188 | 0 | 1,188 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 20 | 1408 | 1,188 | 0 | 1,188 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 21 | 980 | 1,188 | 0 | 1,188 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 22 | 2416 | 1,188 | 0 | 1,188 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 23 | 2016 | 1,188 | 0 | 1,188 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 24 | 1100 | 1,188 | 0 | 1,188 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 25 | 1072 | 1,188 | 0 | 1,188 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 29 | 2944 | 1,188 | 0 | 1,188 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 30 | 2780 | 1,188 | 0 | 1,188 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 31 | 2296 | 1,188 | 0 | 1,188 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |

All threads: x3ap 1.3 %, ntdll 86.3 %, wine 10.4 %, d3dx 1.7 %, zlib 0.2 %, xml 0.0 %, proxy 0.0 %, other 0.0 %.

**Top functions (leaf samples by containing function)**

Shares are of the engine thread (slot 0, 1,188 samples); the other sampled threads are idle Wine/audio/input waiters.

| Function start | Ghidra name | Label | Self (engine) | Incl. frame (engine) | Busiest slot | Hooked APIs in interval |
| --- | --- | --- | ---: | ---: | ---: | --- |
| `0x004e1220` | FUN_004e1220 | — | 0.7 % (8) | 1.8 % (21) | slot 0 0.7 % | — |
| `0x00412440` | FUN_00412440 | — | 0.5 % (6) | 0.8 % (10) | slot 0 0.5 % | — |
| `0x004bc680` | FUN_004bc680 | mesh preparation: adjacency / clean / optimise | 0.0 % (0) | 15.7 % (187) | — | `ID3DXMesh::GenerateAdjacency` 95 calls / 0.415 s; `D3DXCleanMesh` 96 calls / 0.006 s; `ID3DXMesh::OptimizeInplace` 95 calls / 0.017 s |
| `0x00524aa7` | ___lock_fhandle | — | 0.0 % (0) | 15.7 % (186) | — | — |
| `0x0052403b` | __VEC_memzero | — | 0.0 % (0) | 9.3 % (111) | — | — |
| `0x00511f77` | FUN_00511f77 | — | 0.0 % (0) | 5.0 % (59) | — | — |
| `0x004dd2c0` | FUN_004dd2c0 | — | 0.0 % (0) | 4.7 % (56) | — | — |
| `0x004bcb60` | FUN_004bcb60 | — | 0.0 % (0) | 3.8 % (45) | — | — |
| `0x0051da61` | __VEC_memcpy | — | 0.0 % (0) | 2.3 % (27) | — | — |
| `0x0051f464` | FUN_0051f464 | — | 0.0 % (0) | 1.8 % (21) | — | — |
| `0x0050e1b0` | _free | — | 0.0 % (0) | 1.6 % (19) | — | — |
| `0x004dc540` | FUN_004dc540 | texture loader (pck dds; image info, cube, 2D helpers) | 0.0 % (0) | 1.2 % (14) | — | — |
| `0x004e0770` | FUN_004e0770 | — | 0.0 % (0) | 0.8 % (9) | — | — |
| `0x004dfef0` | FUN_004dfef0 | — | 0.0 % (0) | 0.5 % (6) | — | — |
| `0x004bbb10` | FUN_004bbb10 | — | 0.0 % (0) | 0.4 % (5) | — | — |
| `0x004bc1c0` | FUN_004bc1c0 | — | 0.0 % (0) | 0.4 % (5) | — | — |
| `0x004e9210` | FUN_004e9210 | read dispatcher (fread / gzread / XOR-0x33 slice) | 0.0 % (0) | 0.3 % (4) | — | `ReadFile` 871 calls / 0.007 s |
| `0x005112c4` | _malloc | — | 0.0 % (0) | 0.3 % (3) | — | — |
| (no main-module frame) | | | | 96.6 % (33,264) | | |

**Top caller pairs**

| Slot | Frame | Caller | Samples |
| ---: | --- | --- | ---: |
| 0 | `0x124b30` ___lock_fhandle | `0x124b38` ___lock_fhandle | 186 |
| 0 | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 177 |
| 0 | `0x1240bd` __VEC_memzero | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 71 |
| 0 | `0x11204f` FUN_00511f77 | `0x1121d1` _fread_s | 53 |
| 0 | `0xdd6a2` FUN_004dd2c0 | `0xdd6a2` FUN_004dd2c0 | 50 |
| 0 | `0xbcc30` FUN_004bcb60 | `0xbb9ac` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 45 |
| 0 | `0x124071` __VEC_memzero | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 39 |
| 0 | `0x11dab3` __VEC_memcpy | `0x11204f` FUN_00511f77 | 26 |
| 0 | `0x11f665` FUN_0051f464 | `0x11f665` FUN_0051f464 | 20 |
| 0 | `0xdc846` FUN_004dc540 (texture loader (pck dds; image info, cube, 2D helpers)) | `0xdc846` FUN_004dc540 (texture loader (pck dds; image info, cube, 2D helpers)) | 14 |
| 0 | `0x10e21e` _free | `0xbc90a` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 13 |
| 0 | `0xbc878` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | `0xbc878` FUN_004bc680 (mesh preparation: adjacency / clean / optimise) | 10 |

**Candidates**

Hooked calls account for 0.489 s of the 2.220 s interval (22.0 %); 1.731 s is unexplained by any hook. The sampler recorded 34,452 samples over 10.045 s of report coverage (3430 samples/s, 118 ticks/s per thread); the dominant leaf module is ntdll at 86.3 %. 2 of 2 delta blocks straddle the interval boundary and carry samples from outside it. Function `0x004e1220` (FUN_004e1220) holds 0.7 % of the engine thread's samples as leaf (0.7 % of slot 0's samples) and 1.8 % as first main-module frame; it is not a known routine. Function `0x00412440` (FUN_00412440) holds 0.5 % of the engine thread's samples as leaf (0.5 % of slot 0's samples) and 0.8 % as first main-module frame; it is not a known routine. Function `0x004bc680` (FUN_004bc680) holds 0.0 % of the engine thread's samples as leaf and 15.7 % as first main-module frame; it is a known routine (mesh preparation: adjacency / clean / optimise).

## Limits

- Hooked seconds are completion deltas of report windows overlapping the interval; only the exclusive column may be added and it is a lower bound.
- Profiler delta blocks are attributed by overlap of their report interval; a block straddling a boundary counts in both neighbours and is reported as straddling.
- Per-block leaf/frame/pair tables are truncated to the top 48/48/32 rows, so function sums are lower bounds; per-thread module splits and sample totals are exact.
- Frame RVAs are return addresses; inclusive-by-frame shares attribute DLL and wait time to the first main-executable frame, not to a full call stack.
- Gap labels are heuristics over hooked counts (label_gap); no phase marker exists in the log.
- Function labels come from hand-maintained notes; an unlabelled function is merely undocumented, not unimportant.
