# Loading attribution: `session-20260912-171241-212.log`

Source 295,274,646 bytes, sha256 `c0fe801ccff79874c5be6c0e9386c8430a235eb618f676071989fbbee6963e3d`; QPC 10,000,000 Hz; 311 loading report windows; 19 import hooks; last Present at 372.702 s.

Profiler: 74 delta blocks from 5.038 to 370.307 s, interval 2000 µs, report period 5 s, 75 modules, main base 0x00400000. Symbols: 147/148 RVAs resolved (verification/results/loading-profile-x3-run6/symbols.json); 118 labelled function starts.

## Gaps over 2.0 s

| # | Label | Gap | Interval | Hooked excl. | Unexplained | Samples | Stalls |
| ---: | --- | ---: | --- | ---: | ---: | ---: | ---: |
| 1 | menu load | 8.740 s | 4.181–13.445 s | 4.979 s | 3.761 s | 38,412 | 0 |
| 2 | save load | 44.539 s | 23.867–68.752 s | 21.859 s | 22.680 s | 196,517 | 2 |
| 3 | menu load | 7.380 s | 82.604–90.391 s | 4.615 s | 2.765 s | 85,191 | 2 |
| 4 | unlabelled | 3.124 s | 121.082–125.374 s | 2.418 s | 0.706 s | 58,174 | 1 |
| 5 | menu load | 24.697 s | 123.920–148.920 s | 12.854 s | 11.843 s | 212,584 | 2 |
| 6 | sector change | 6.175 s | 331.432–338.263 s | 3.854 s | 2.321 s | 76,096 | 1 |
| 7 | menu load | 8.400 s | 362.300–371.158 s | 4.765 s | 3.635 s | 74,080 | 0 |

## Gap 1: menu load (8.740 s, ends at frame 8)

Label evidence: 1017 GenerateAdjacency calls and 299,196,640 B of 2D texture-helper input match the main-menu work vector.
Interval 4.181–13.445 s, length 8.740 s; hooked exclusive 4.979 s over 9 report windows (2 straddling); unexplained 3.761 s.
Samples 38,412 in 3 delta blocks (2 straddling), 15.033 s covered, 2555 samples/s, 332 ticks/s per thread, 0 dropped; sampler tick 572 µs mean / 30462 µs max, busy 19.8 % of the covered time.

**Hooked time**

| Operation | Calls | Incl. s | Excl. s | Mean ms | Bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `inflate` | 167,720 | 2.660 | 2.660 | 0.016 | 0 |
| `D3DXCreateTextureFromFileInMemoryEx` | 467 | 0.507 | 0.507 | 1.086 | 299,196,640 |
| `ReadFile` | 45,855 | 0.429 | 0.429 | 0.009 | 193,204,112 |
| `ID3DXMesh::GenerateAdjacency` | 1,017 | 0.423 | 0.423 | 0.416 | 0 |
| `D3DXCreateMesh` | 1,017 | 0.251 | 0.251 | 0.246 | 0 |
| `ID3DXMesh::OptimizeInplace` | 1,017 | 0.213 | 0.213 | 0.210 | 0 |
| `D3DXCreateEffect` | 10 | 0.131 | 0.131 | 13.051 | 309,928 |
| `D3DXCleanMesh` | 1,023 | 0.118 | 0.118 | 0.116 | 0 |
| `D3DXLoadSurfaceFromFileInMemory` | 4 | 0.096 | 0.096 | 24.057 | 4,918,784 |
| `CreateFileA` | 1,176 | 0.073 | 0.073 | 0.062 | 0 |
| `FindFirstFileA` | 1,287 | 0.030 | 0.030 | 0.024 | 0 |
| `xmlReadMemory` | 2 | 0.024 | 0.024 | 11.818 | 3,688,819 |

**Sampled attribution: per-thread module split**

| Slot | TID | Samples | x3ap | ntdll | wine | d3dx | zlib | xml | proxy | other | Start |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 0 | 216 | 4,997 | 0 | 111 | 0 | 0 | 0 | 0 | 0 | 4,886 | 0:0x112ead |
| 1 | 300 | 4,162 | 0 | 4,162 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xb220 |
| 2 | 304 | 3,757 | 0 | 3,757 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xc780 |
| 3 | 316 | 3,757 | 0 | 3,757 | 0 | 0 | 0 | 0 | 0 | 0 | 49:0x38490 |
| 4 | 320 | 3,394 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 3,394 | 4:0xd0f0 |
| 5 | 328 | 3,394 | 0 | 3,394 | 0 | 0 | 0 | 0 | 0 | 0 | 20:0xe740 |
| 6 | 352 | 3,058 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 3,058 | 38:0x2400 |
| 7 | 356 | 3,058 | 0 | 3,058 | 0 | 0 | 0 | 0 | 0 | 0 | 7:0x9900 |
| 8 | 368 | 26 | 0 | 26 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x9f90 |
| 9 | 372 | 3,058 | 0 | 3,058 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 10 | 408 | 2,974 | 0 | 2,974 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 11 | 440 | 38 | 0 | 38 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x9f90 |
| 12 | 444 | 38 | 0 | 38 | 0 | 0 | 0 | 0 | 0 | 0 | 49:0x38490 |
| 13 | 448 | 2,701 | 0 | 2,701 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |

All threads: x3ap 0.0 %, ntdll 70.5 %, wine 0.0 %, d3dx 0.0 %, zlib 0.0 %, xml 0.0 %, proxy 0.0 %, other 29.5 %.

**Top functions (leaf samples by containing function)**

Shares are of the engine thread (slot 0, 4,997 samples); the other sampled threads are idle Wine/audio/input waiters.

| Function start | Ghidra name | Label | Self (engine) | Incl. frame (engine) | Busiest slot | Hooked APIs in interval |
| --- | --- | --- | ---: | ---: | ---: | --- |
| `0x004e8e10` | FUN_004e8e10 | resource load = open + read + close | 0.0 % (0) | 18.1 % (903) | — | — |
| `0x004b4f80` | FUN_004b4f80 | CPureDeviceStateManager::SetRenderState (filters via 0x004b5620, then device vtable+0xe4) | 0.0 % (0) | 5.8 % (289) | — | — |
| `0x0043ce30` | FUN_0043ce30 | type-table object construction: type lookup 0x00492970, node create 0x00486d10, per-part loop (+0x50, stride 0x44) via 0x0043d1d0 | 0.0 % (0) | 4.5 % (226) | — | — |
| `0x004e1220` | FUN_004e1220 | collision tree: recursive split body (self-recursive, 2,267 B) - top loading hot spot | 0.0 % (0) | 4.2 % (208) | — | — |
| `0x005112c4` | _malloc | — | 0.0 % (0) | 3.8 % (190) | — | — |
| `0x004bcb60` | FUN_004bcb60 | mesh build: CloneMesh into final options (D3DXMESH_32BIT above 64k vertices), two attempts, OOM callback, then install via 0x004bc9c0 | 0.0 % (0) | 2.7 % (133) | — | — |
| `0x004bc680` | FUN_004bc680 | mesh preparation: adjacency / clean / optimise; calls ID3DXMesh::GenerateAdjacency(1e-6f from 0x00565600) at 0x004bc76a | 0.0 % (0) | 2.5 % (125) | — | `ID3DXMesh::GenerateAdjacency` 1,017 calls / 0.423 s; `D3DXCleanMesh` 1,023 calls / 0.118 s; `ID3DXMesh::OptimizeInplace` 1,017 calls / 0.213 s |
| `0x004dc540` | FUN_004dc540 | texture loader (pck dds; image info, cube, 2D helpers) | 0.0 % (0) | 2.4 % (119) | — | `D3DXCreateTextureFromFileInMemoryEx` 467 calls / 0.507 s; `D3DXCreateCubeTextureFromFileInMemoryEx` 11 calls / 0.020 s |
| `0x004e8880` | FUN_004e8880 | resource read | 0.0 % (0) | 2.3 % (117) | — | `ReadFile` 45,855 calls / 0.429 s |
| `0x0050e1b0` | _free | — | 0.0 % (0) | 2.1 % (103) | — | — |
| `0x004b3860` | FUN_004b3860 | — | 0.0 % (0) | 1.9 % (97) | — | — |
| `0x0052403b` | __VEC_memzero | — | 0.0 % (0) | 1.9 % (96) | — | — |
| `0x0051da61` | __VEC_memcpy | — | 0.0 % (0) | 1.8 % (92) | — | — |
| `0x004bb470` | FUN_004bb470 | mesh build: D3DXCreateMesh + vertex/index fill | 0.0 % (0) | 1.7 % (86) | — | `D3DXCreateMesh` 1,017 calls / 0.251 s |
| `0x00510430` | FID_conflict:_sscanf | — | 0.0 % (0) | 1.6 % (79) | — | — |
| `0x0042fe10` | FUN_0042fe10 | — | 0.0 % (0) | 1.4 % (71) | — | — |
| `0x00511f77` | FUN_00511f77 | CRT fread body (from _fread_s, uses __VEC_memcpy) | 0.0 % (0) | 1.4 % (69) | — | — |
| `0x004efa70` | FUN_004efa70 | overlay/glyph state helper (0x004dce20 -> 0x004ee6e0 == 0x18 -> 0x004b2730) | 0.0 % (0) | 1.2 % (58) | — | — |
| `0x00528060` | FUN_00528060 | — | 0.0 % (0) | 1.1 % (57) | — | — |
| `0x00524aa7` | ___lock_fhandle | — | 0.0 % (0) | 1.1 % (56) | — | — |
| `0x004b89f0` | FUN_004b89f0 | — | 0.0 % (0) | 0.8 % (40) | — | — |
| `0x0050f786` | _memcpy_s | — | 0.0 % (0) | 0.8 % (39) | — | — |
| `0x004bc1c0` | FUN_004bc1c0 | mesh build: vertex positions (int16 x 1/16384) and normals (D3DXVec3Normalize x3) | 0.0 % (0) | 0.8 % (39) | — | — |
| `0x004cf460` | FUN_004cf460 | DirectShow movie playback setup (CoCreateInstance of the X MPEG filter graph, Data\mov\%s, %05d.dat) | 0.0 % (0) | 0.8 % (39) | — | — |
| `0x0040b050` | FUN_0040b050 | — | 0.0 % (0) | 0.7 % (34) | — | — |
| `0x004b50a0` | FUN_004b50a0 | — | 0.0 % (0) | 0.7 % (33) | — | — |
| `0x004b51b0` | FUN_004b51b0 | — | 0.0 % (0) | 0.6 % (28) | — | — |
| `0x004bb240` | FUN_004bb240 | — | 0.0 % (0) | 0.5 % (27) | — | — |
| `0x0051f464` | FUN_0051f464 | CRT _read text-mode body (ReadFile, MultiByteToWideChar, __malloc_crt) | 0.0 % (0) | 0.5 % (26) | — | — |
| `0x004d34b0` | FUN_004d34b0 | Win32 message pump (PeekMessageA / GetMessageA) | 0.0 % (0) | 0.5 % (25) | — | — |
| `0x004d8f10` | FUN_004d8f10 | — | 0.0 % (0) | 0.4 % (22) | — | — |
| `0x004b9110` | FUN_004b9110 | — | 0.0 % (0) | 0.4 % (20) | — | — |
| `0x00517d8f` | FUN_00517d8f | CRT file-lock release tail (LeaveCriticalSection, from 0x00524cfa) | 0.0 % (0) | 0.4 % (19) | — | — |
| `0x00519434` | FUN_00519434 | — | 0.0 % (0) | 0.4 % (19) | — | — |
| `0x004d3620` | FUN_004d3620 | — | 0.0 % (0) | 0.4 % (18) | — | — |
| `0x00527fc2` | FID_conflict:__sopen_helper | — | 0.0 % (0) | 0.3 % (17) | — | — |
| `0x004dd2c0` | FUN_004dd2c0 | jpg/tga texture load body: resource load 0x004e8e10 + D3DXGetImageInfo + 2D helper + LoadSurface (callee of 0x004f3510) | 0.0 % (0) | 0.3 % (17) | — | `D3DXCreateTextureFromFileInMemoryEx` 467 calls / 0.507 s; `D3DXLoadSurfaceFromFileInMemory` 4 calls / 0.096 s |
| `0x004b4600` | FUN_004b4600 | — | 0.0 % (0) | 0.3 % (16) | — | — |
| `0x00470490` | FUN_00470490 | — | 0.0 % (0) | 0.3 % (15) | — | — |
| `0x004b5720` | FUN_004b5720 | — | 0.0 % (0) | 0.3 % (15) | — | — |
| (no main-module frame) | | | | 89.9 % (34,532) | | |

**Top caller pairs**

| Slot | Frame | Caller | Samples |
| ---: | --- | --- | ---: |
| 0 | `0xe8e8d` FUN_004e8e10 (resource load = open + read + close) | `0xe8eb2` FUN_004e8e10 (resource load = open + read + close) | 903 |
| 0 | `0x3ce6d` FUN_0043ce30 (type-table object construction: type lookup 0x00492970, node create 0x00486d10, per-part loop (+0x50, stride 0x44) via 0x0043d1d0) | `0x11133e` _malloc | 193 |
| 0 | `0xbcc30` FUN_004bcb60 (mesh build: CloneMesh into final options (D3DXMESH_32BIT above 64k vertices), two attempts, OOM callback, then install via 0x004bc9c0) | `0xbb9ac` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 133 |
| 0 | `0x11133e` _malloc | `0xe6a75` FUN_004e6a40 (malloc + memset allocation wrapper (via 0x004b8b60)) | 130 |
| 0 | `0xdc846` FUN_004dc540 (texture loader (pck dds; image info, cube, 2D helpers)) | `0xdc846` FUN_004dc540 (texture loader (pck dds; image info, cube, 2D helpers)) | 115 |
| 0 | `0xb4f99` FUN_004b4f80 (CPureDeviceStateManager::SetRenderState (filters via 0x004b5620, then device vtable+0xe4)) | `0x114891` __expandlocale | 106 |
| 0 | `0xb4f99` FUN_004b4f80 (CPureDeviceStateManager::SetRenderState (filters via 0x004b5620, then device vtable+0xe4)) | `0xc403e` FUN_004c0150 (effect state manager: matrix uploads per draw) | 95 |
| 0 | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise; calls ID3DXMesh::GenerateAdjacency(1e-6f from 0x00565600) at 0x004bc76a) | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise; calls ID3DXMesh::GenerateAdjacency(1e-6f from 0x00565600) at 0x004bc76a) | 87 |
| 0 | `0xbb72f` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | `0xbb72f` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 86 |
| 0 | `0xe8da9` FUN_004e8880 (resource read) | `0xe8da9` FUN_004e8880 (resource read) | 83 |
| 0 | `0x11044a` FID_conflict:_sscanf | `0x6e735` FUN_0046e690 | 79 |
| 0 | `0xb4fad` FUN_004b4f80 (CPureDeviceStateManager::SetRenderState (filters via 0x004b5620, then device vtable+0xe4)) | `0xb4f99` FUN_004b4f80 (CPureDeviceStateManager::SetRenderState (filters via 0x004b5620, then device vtable+0xe4)) | 70 |

**Candidates**

Hooked calls account for 4.979 s of the 8.740 s interval (57.0 %); 3.761 s is unexplained by any hook. The sampler recorded 38,412 samples over 15.033 s of report coverage (2555 samples/s, 332 ticks/s per thread); the dominant leaf module is ntdll at 70.5 %. 2 of 3 delta blocks straddle the interval boundary and carry samples from outside it. Function `0x004e8e10` (FUN_004e8e10) holds 0.0 % of the engine thread's samples as leaf and 18.1 % as first main-module frame; it is a known routine (resource load = open + read + close). Function `0x004b4f80` (FUN_004b4f80) holds 0.0 % of the engine thread's samples as leaf and 5.8 % as first main-module frame; it is a known routine (CPureDeviceStateManager::SetRenderState (filters via 0x004b5620, then device vtable+0xe4)). Function `0x0043ce30` (FUN_0043ce30) holds 0.0 % of the engine thread's samples as leaf and 4.5 % as first main-module frame; it is a known routine (type-table object construction: type lookup 0x00492970, node create 0x00486d10, per-part loop (+0x50, stride 0x44) via 0x0043d1d0).

## Gap 2: save load (44.539 s, ends at frame 842)

Label evidence: 14430515 gzread calls and 1 successful gzopen inside the gap (gzip save stream).
Interval 23.867–68.752 s, length 44.539 s; hooked exclusive 21.859 s over 10 report windows (2 straddling); unexplained 22.680 s.
Samples 196,517 in 10 delta blocks (2 straddling), 50.018 s covered, 3929 samples/s, 317 ticks/s per thread, 0 dropped; sampler tick 745 µs mean / 52058 µs max, busy 24.4 % of the covered time.

**Hooked time**

| Operation | Calls | Incl. s | Excl. s | Mean ms | Bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `gzread` | 14,430,515 | 9.719 | 9.719 | 0.001 | 43,601,615 |
| `inflate` | 254,376 | 4.190 | 4.190 | 0.016 | 0 |
| `CreateFileA` | 1,953 | 3.545 | 3.545 | 1.815 | 0 |
| `ID3DXMesh::GenerateAdjacency` | 749 | 0.909 | 0.909 | 1.214 | 0 |
| `D3DXCreateTextureFromFileInMemoryEx` | 425 | 0.821 | 0.821 | 1.932 | 486,501,788 |
| `FindFirstFileA` | 2,053 | 0.736 | 0.736 | 0.359 | 0 |
| `ReadFile` | 70,808 | 0.616 | 0.616 | 0.009 | 280,510,501 |
| `ID3DXMesh::OptimizeInplace` | 749 | 0.460 | 0.460 | 0.614 | 0 |
| `D3DXCleanMesh` | 749 | 0.276 | 0.276 | 0.369 | 0 |
| `xmlReadMemory` | 790 | 0.219 | 0.219 | 0.277 | 22,531,669 |
| `D3DXCreateMesh` | 749 | 0.181 | 0.181 | 0.242 | 0 |
| `D3DXCreateEffect` | 5 | 0.072 | 0.072 | 14.390 | 181,852 |

**Sampled attribution: per-thread module split**

| Slot | TID | Samples | x3ap | ntdll | wine | d3dx | zlib | xml | proxy | other | Start |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 0 | 216 | 15,879 | 0 | 64 | 0 | 0 | 0 | 0 | 0 | 15,815 | 0:0x112ead |
| 1 | 300 | 15,879 | 0 | 15,879 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xb220 |
| 2 | 304 | 15,879 | 0 | 15,879 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xc780 |
| 3 | 316 | 15,879 | 0 | 15,879 | 0 | 0 | 0 | 0 | 0 | 0 | 49:0x38490 |
| 4 | 320 | 15,879 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 15,879 | 4:0xd0f0 |
| 5 | 328 | 15,879 | 0 | 15,879 | 0 | 0 | 0 | 0 | 0 | 0 | 20:0xe740 |
| 6 | 352 | 15,879 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 15,879 | 38:0x2400 |
| 7 | 356 | 15,879 | 0 | 15,879 | 0 | 0 | 0 | 0 | 0 | 0 | 7:0x9900 |
| 8 | 472 | 1,284 | 0 | 1,284 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x9f90 |
| 8 | 488 | 2,899 | 0 | 2,899 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x9f90 |
| 9 | 372 | 15,879 | 0 | 15,879 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 10 | 408 | 15,879 | 0 | 15,879 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 11 | 476 | 1,284 | 0 | 1,284 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 11 | 492 | 2,899 | 0 | 2,899 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 12 | 480 | 1,283 | 0 | 1,283 | 0 | 0 | 0 | 0 | 0 | 0 | 59:0x13a60 |
| 12 | 496 | 2,899 | 0 | 2,899 | 0 | 0 | 0 | 0 | 0 | 0 | 59:0x13a60 |
| 13 | 448 | 15,879 | 0 | 15,879 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 14 | 484 | 1,283 | 0 | 1,283 | 0 | 0 | 0 | 0 | 0 | 0 | 59:0x12e30 |
| 14 | 500 | 2,899 | 0 | 2,899 | 0 | 0 | 0 | 0 | 0 | 0 | 59:0x12e30 |
| 15 | 504 | 2,572 | 0 | 2,572 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 16 | 520 | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 16 | 568 | 272 | 0 | 272 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 17 | 536 | 2,273 | 0 | 2,273 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |

All threads: x3ap 0.0 %, ntdll 75.8 %, wine 0.0 %, d3dx 0.0 %, zlib 0.0 %, xml 0.0 %, proxy 0.0 %, other 24.2 %.

**Top functions (leaf samples by containing function)**

Shares are of the engine thread (slot 0, 15,879 samples); the other sampled threads are idle Wine/audio/input waiters.

| Function start | Ghidra name | Label | Self (engine) | Incl. frame (engine) | Busiest slot | Hooked APIs in interval |
| --- | --- | --- | ---: | ---: | ---: | --- |
| `0x004c4fc0` | FUN_004c4fc0 | material submission wrapper | 0.0 % (0) | 6.6 % (1,042) | — | — |
| `0x004e8e10` | FUN_004e8e10 | resource load = open + read + close | 0.0 % (0) | 6.0 % (959) | — | — |
| `0x00511f77` | FUN_00511f77 | CRT fread body (from _fread_s, uses __VEC_memcpy) | 0.0 % (0) | 5.7 % (913) | — | — |
| `0x005112c4` | _malloc | — | 0.0 % (0) | 3.7 % (581) | — | — |
| `0x00517d8f` | FUN_00517d8f | CRT file-lock release tail (LeaveCriticalSection, from 0x00524cfa) | 0.0 % (0) | 3.5 % (552) | — | — |
| `0x0051da61` | __VEC_memcpy | — | 0.0 % (0) | 2.1 % (336) | — | — |
| `0x004bc680` | FUN_004bc680 | mesh preparation: adjacency / clean / optimise; calls ID3DXMesh::GenerateAdjacency(1e-6f from 0x00565600) at 0x004bc76a | 0.0 % (0) | 1.7 % (276) | — | `ID3DXMesh::GenerateAdjacency` 749 calls / 0.909 s; `D3DXCleanMesh` 749 calls / 0.276 s; `ID3DXMesh::OptimizeInplace` 749 calls / 0.460 s |
| `0x004e1220` | FUN_004e1220 | collision tree: recursive split body (self-recursive, 2,267 B) - top loading hot spot | 0.0 % (0) | 1.7 % (273) | — | — |
| `0x0050e1b0` | _free | — | 0.0 % (0) | 1.4 % (228) | — | — |
| `0x004bcb60` | FUN_004bcb60 | mesh build: CloneMesh into final options (D3DXMESH_32BIT above 64k vertices), two attempts, OOM callback, then install via 0x004bc9c0 | 0.0 % (0) | 1.2 % (198) | — | — |
| `0x0051f464` | FUN_0051f464 | CRT _read text-mode body (ReadFile, MultiByteToWideChar, __malloc_crt) | 0.0 % (0) | 1.0 % (151) | — | — |
| `0x0052403b` | __VEC_memzero | — | 0.0 % (0) | 0.8 % (134) | — | — |
| `0x004dd2c0` | FUN_004dd2c0 | jpg/tga texture load body: resource load 0x004e8e10 + D3DXGetImageInfo + 2D helper + LoadSurface (callee of 0x004f3510) | 0.0 % (0) | 0.8 % (132) | — | `D3DXCreateTextureFromFileInMemoryEx` 425 calls / 0.821 s; `D3DXLoadSurfaceFromFileInMemory` 3 calls / 0.033 s |
| `0x004e29f0` | FUN_004e29f0 | collision helper wrapper (0x004e2780, 0x0052b5d0) | 0.0 % (0) | 0.7 % (114) | — | — |
| `0x004bc1c0` | FUN_004bc1c0 | mesh build: vertex positions (int16 x 1/16384) and normals (D3DXVec3Normalize x3) | 0.0 % (0) | 0.7 % (110) | — | — |
| `0x004dc540` | FUN_004dc540 | texture loader (pck dds; image info, cube, 2D helpers) | 0.0 % (0) | 0.7 % (110) | — | `D3DXCreateTextureFromFileInMemoryEx` 425 calls / 0.821 s; `D3DXCreateCubeTextureFromFileInMemoryEx` 9 calls / 0.015 s |
| `0x004eeab0` | FUN_004eeab0 | thin wrapper over 0x004db520 | 0.0 % (0) | 0.5 % (76) | — | — |
| `0x0047cfe0` | FUN_0047cfe0 | — | 0.0 % (0) | 0.4 % (58) | — | — |
| `0x004efcc0` | FUN_004efcc0 | handle map insert | 0.0 % (0) | 0.4 % (57) | — | — |
| `0x004a7f60` | FUN_004a7f60 | — | 0.0 % (0) | 0.3 % (51) | — | — |
| `0x00528060` | FUN_00528060 | — | 0.0 % (0) | 0.3 % (50) | — | — |
| `0x004bb470` | FUN_004bb470 | mesh build: D3DXCreateMesh + vertex/index fill | 0.0 % (0) | 0.3 % (44) | — | `D3DXCreateMesh` 749 calls / 0.181 s |
| `0x00479d10` | FUN_00479d10 | node deserialization (load path) | 0.0 % (0) | 0.3 % (40) | — | — |
| `0x00517bcb` | __isleadbyte_l | — | 0.0 % (0) | 0.2 % (38) | — | — |
| `0x00519434` | FUN_00519434 | — | 0.0 % (0) | 0.2 % (33) | — | — |
| `0x004e8880` | FUN_004e8880 | resource read | 0.0 % (0) | 0.2 % (33) | — | `ReadFile` 70,808 calls / 0.616 s |
| `0x0048a890` | FUN_0048a890 | — | 0.0 % (0) | 0.2 % (32) | — | — |
| `0x00524aa7` | ___lock_fhandle | — | 0.0 % (0) | 0.2 % (31) | — | — |
| `0x004cabc0` | FUN_004cabc0 | — | 0.0 % (0) | 0.2 % (25) | — | — |
| `0x0040b050` | FUN_0040b050 | — | 0.0 % (0) | 0.2 % (24) | — | — |
| `0x004bb240` | FUN_004bb240 | — | 0.0 % (0) | 0.2 % (24) | — | — |
| `0x0047d9c0` | FUN_0047d9c0 | render-node visit (model lookup, LOD choice) | 0.0 % (0) | 0.1 % (23) | — | — |
| `0x004c0150` | FUN_004c0150 | effect state manager: matrix uploads per draw | 0.0 % (0) | 0.1 % (23) | — | — |
| `0x00527fc2` | FID_conflict:__sopen_helper | — | 0.0 % (0) | 0.1 % (22) | — | — |
| `0x004a47f0` | FUN_004a47f0 | — | 0.0 % (0) | 0.1 % (22) | — | — |
| `0x0040b0ed` | FUN_0040b0ed | — | 0.0 % (0) | 0.1 % (22) | — | — |
| `0x004bbb10` | FUN_004bbb10 | mesh build: vertex writer (int16 x 1/16384 from 0x005655d4) | 0.0 % (0) | 0.1 % (20) | — | — |
| `0x004e6a40` | FUN_004e6a40 | malloc + memset allocation wrapper (via 0x004b8b60) | 0.0 % (0) | 0.1 % (17) | — | — |
| `0x004e1b00` | FUN_004e1b00 | — | 0.0 % (0) | 0.1 % (15) | — | — |
| `0x0044d9c0` | FUN_0044d9c0 | — | 0.0 % (0) | 0.1 % (13) | — | — |
| (no main-module frame) | | | | 96.4 % (189,387) | | |

**Top caller pairs**

| Slot | Frame | Caller | Samples |
| ---: | --- | --- | ---: |
| 0 | `0xe8e8d` FUN_004e8e10 (resource load = open + read + close) | `0xe8eb2` FUN_004e8e10 (resource load = open + read + close) | 959 |
| 0 | `0xc522d` FUN_004c4fc0 (material submission wrapper) | `0x7e07b` FUN_0047d9c0 (render-node visit (model lookup, LOD choice)) | 896 |
| 0 | `0x117da2` FUN_00517d8f (CRT file-lock release tail (LeaveCriticalSection, from 0x00524cfa)) | `0x124d01` FUN_00524cfa | 552 |
| 0 | `0x11204f` FUN_00511f77 (CRT fread body (from _fread_s, uses __VEC_memcpy)) | `0xe8dc3` FUN_004e8880 (resource read) | 320 |
| 0 | `0x11204f` FUN_00511f77 (CRT fread body (from _fread_s, uses __VEC_memcpy)) | `0xe8da9` FUN_004e8880 (resource read) | 310 |
| 0 | `0x11dab3` __VEC_memcpy | `0x11204f` FUN_00511f77 (CRT fread body (from _fread_s, uses __VEC_memcpy)) | 308 |
| 0 | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise; calls ID3DXMesh::GenerateAdjacency(1e-6f from 0x00565600) at 0x004bc76a) | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise; calls ID3DXMesh::GenerateAdjacency(1e-6f from 0x00565600) at 0x004bc76a) | 201 |
| 0 | `0x11204f` FUN_00511f77 (CRT fread body (from _fread_s, uses __VEC_memcpy)) | `0x11133e` _malloc | 200 |
| 0 | `0xbcc30` FUN_004bcb60 (mesh build: CloneMesh into final options (D3DXMESH_32BIT above 64k vertices), two attempts, OOM callback, then install via 0x004bc9c0) | `0xbb9ac` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 198 |
| 0 | `0xc522d` FUN_004c4fc0 (material submission wrapper) | `0x7e76e` FUN_0047e6e0 (deferred draw list drain) | 143 |
| 0 | `0xdd6a2` FUN_004dd2c0 (jpg/tga texture load body: resource load 0x004e8e10 + D3DXGetImageInfo + 2D helper + LoadSurface (callee of 0x004f3510)) | `0xdd6a2` FUN_004dd2c0 (jpg/tga texture load body: resource load 0x004e8e10 + D3DXGetImageInfo + 2D helper + LoadSurface (callee of 0x004f3510)) | 132 |
| 0 | `0x1240bd` __VEC_memzero | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise; calls ID3DXMesh::GenerateAdjacency(1e-6f from 0x00565600) at 0x004bc76a) | 129 |

**Candidates**

Hooked calls account for 21.859 s of the 44.539 s interval (49.1 %); 22.680 s is unexplained by any hook. The sampler recorded 196,517 samples over 50.018 s of report coverage (3929 samples/s, 317 ticks/s per thread); the dominant leaf module is ntdll at 75.8 %. 2 of 10 delta blocks straddle the interval boundary and carry samples from outside it. Function `0x004c4fc0` (FUN_004c4fc0) holds 0.0 % of the engine thread's samples as leaf and 6.6 % as first main-module frame; it is a known routine (material submission wrapper). Function `0x004e8e10` (FUN_004e8e10) holds 0.0 % of the engine thread's samples as leaf and 6.0 % as first main-module frame; it is a known routine (resource load = open + read + close). Function `0x00511f77` (FUN_00511f77) holds 0.0 % of the engine thread's samples as leaf and 5.7 % as first main-module frame; it is a known routine (CRT fread body (from _fread_s, uses __VEC_memcpy)).

### Gap 2 stall 1: report stall 26.616 s

Interval 23.919–50.535 s, length 26.616 s; hooked exclusive 11.660 s over 1 report windows (0 straddling); unexplained 14.956 s.
Samples 139,983 in 7 delta blocks (2 straddling), 35.008 s covered, 3999 samples/s, 350 ticks/s per thread, 0 dropped; sampler tick 570 µs mean / 52058 µs max, busy 20.6 % of the covered time.

**Hooked time**

| Operation | Calls | Incl. s | Excl. s | Mean ms | Bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `gzread` | 14,370,976 | 9.679 | 9.679 | 0.001 | 43,358,245 |
| `inflate` | 101,234 | 1.753 | 1.753 | 0.017 | 0 |
| `ReadFile` | 25,593 | 0.165 | 0.165 | 0.006 | 108,906,862 |
| `D3DXLoadSurfaceFromFileInMemory` | 3 | 0.033 | 0.033 | 10.902 | 1,196,466 |
| `CreateFileA` | 87 | 0.016 | 0.016 | 0.187 | 0 |
| `D3DXCreateTextureFromFileInMemoryEx` | 1 | 0.008 | 0.008 | 7.888 | 1,180,058 |
| `FindFirstFileA` | 100 | 0.006 | 0.006 | 0.057 | 0 |
| `SetFilePointer` | 258 | 0.001 | 0.001 | 0.002 | 0 |
| `FindNextFileA` | 15 | 0.000 | 0.000 | 0.018 | 0 |
| `gzopen` | 1 | 0.000 | 0.000 | 0.176 | 0 |
| `FindClose` | 6 | 0.000 | 0.000 | 0.002 | 0 |

**Sampled attribution: per-thread module split**

| Slot | TID | Samples | x3ap | ntdll | wine | d3dx | zlib | xml | proxy | other | Start |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 0 | 216 | 12,259 | 0 | 44 | 0 | 0 | 0 | 0 | 0 | 12,215 | 0:0x112ead |
| 1 | 300 | 12,259 | 0 | 12,259 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xb220 |
| 2 | 304 | 12,259 | 0 | 12,259 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xc780 |
| 3 | 316 | 12,259 | 0 | 12,259 | 0 | 0 | 0 | 0 | 0 | 0 | 49:0x38490 |
| 4 | 320 | 12,259 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 12,259 | 4:0xd0f0 |
| 5 | 328 | 12,259 | 0 | 12,259 | 0 | 0 | 0 | 0 | 0 | 0 | 20:0xe740 |
| 6 | 352 | 12,259 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 12,259 | 38:0x2400 |
| 7 | 356 | 12,259 | 0 | 12,259 | 0 | 0 | 0 | 0 | 0 | 0 | 7:0x9900 |
| 8 | 472 | 1,284 | 0 | 1,284 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x9f90 |
| 9 | 372 | 12,259 | 0 | 12,259 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 10 | 408 | 12,259 | 0 | 12,259 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 11 | 476 | 1,284 | 0 | 1,284 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 12 | 480 | 1,283 | 0 | 1,283 | 0 | 0 | 0 | 0 | 0 | 0 | 59:0x13a60 |
| 13 | 448 | 12,259 | 0 | 12,259 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 14 | 484 | 1,283 | 0 | 1,283 | 0 | 0 | 0 | 0 | 0 | 0 | 59:0x12e30 |

All threads: x3ap 0.0 %, ntdll 73.8 %, wine 0.0 %, d3dx 0.0 %, zlib 0.0 %, xml 0.0 %, proxy 0.0 %, other 26.2 %.

**Top functions (leaf samples by containing function)**

Shares are of the engine thread (slot 0, 12,259 samples); the other sampled threads are idle Wine/audio/input waiters.

| Function start | Ghidra name | Label | Self (engine) | Incl. frame (engine) | Busiest slot | Hooked APIs in interval |
| --- | --- | --- | ---: | ---: | ---: | --- |
| `0x004c4fc0` | FUN_004c4fc0 | material submission wrapper | 0.0 % (0) | 6.9 % (850) | — | — |
| `0x004e8e10` | FUN_004e8e10 | resource load = open + read + close | 0.0 % (0) | 4.7 % (571) | — | — |
| `0x00511f77` | FUN_00511f77 | CRT fread body (from _fread_s, uses __VEC_memcpy) | 0.0 % (0) | 2.9 % (358) | — | — |
| `0x0051da61` | __VEC_memcpy | — | 0.0 % (0) | 2.7 % (332) | — | — |
| `0x004e1220` | FUN_004e1220 | collision tree: recursive split body (self-recursive, 2,267 B) - top loading hot spot | 0.0 % (0) | 2.2 % (273) | — | — |
| `0x004bc680` | FUN_004bc680 | mesh preparation: adjacency / clean / optimise; calls ID3DXMesh::GenerateAdjacency(1e-6f from 0x00565600) at 0x004bc76a | 0.0 % (0) | 1.9 % (227) | — | — |
| `0x004bcb60` | FUN_004bcb60 | mesh build: CloneMesh into final options (D3DXMESH_32BIT above 64k vertices), two attempts, OOM callback, then install via 0x004bc9c0 | 0.0 % (0) | 1.3 % (160) | — | — |
| `0x0052403b` | __VEC_memzero | — | 0.0 % (0) | 0.9 % (115) | — | — |
| `0x004e29f0` | FUN_004e29f0 | collision helper wrapper (0x004e2780, 0x0052b5d0) | 0.0 % (0) | 0.9 % (114) | — | — |
| `0x0051f464` | FUN_0051f464 | CRT _read text-mode body (ReadFile, MultiByteToWideChar, __malloc_crt) | 0.0 % (0) | 0.8 % (100) | — | — |
| `0x004bc1c0` | FUN_004bc1c0 | mesh build: vertex positions (int16 x 1/16384) and normals (D3DXVec3Normalize x3) | 0.0 % (0) | 0.8 % (95) | — | — |
| `0x0050e1b0` | _free | — | 0.0 % (0) | 0.7 % (88) | — | — |
| `0x004eeab0` | FUN_004eeab0 | thin wrapper over 0x004db520 | 0.0 % (0) | 0.6 % (76) | — | — |
| `0x004dd2c0` | FUN_004dd2c0 | jpg/tga texture load body: resource load 0x004e8e10 + D3DXGetImageInfo + 2D helper + LoadSurface (callee of 0x004f3510) | 0.0 % (0) | 0.5 % (62) | — | `D3DXCreateTextureFromFileInMemoryEx` 1 calls / 0.008 s; `D3DXLoadSurfaceFromFileInMemory` 3 calls / 0.033 s |
| `0x0047cfe0` | FUN_0047cfe0 | — | 0.0 % (0) | 0.5 % (58) | — | — |
| `0x004dc540` | FUN_004dc540 | texture loader (pck dds; image info, cube, 2D helpers) | 0.0 % (0) | 0.4 % (53) | — | `D3DXCreateTextureFromFileInMemoryEx` 1 calls / 0.008 s |
| `0x00479d10` | FUN_00479d10 | node deserialization (load path) | 0.0 % (0) | 0.3 % (40) | — | — |
| `0x0048a890` | FUN_0048a890 | — | 0.0 % (0) | 0.3 % (32) | — | — |
| `0x004bb470` | FUN_004bb470 | mesh build: D3DXCreateMesh + vertex/index fill | 0.0 % (0) | 0.3 % (31) | — | — |
| `0x00524aa7` | ___lock_fhandle | — | 0.0 % (0) | 0.2 % (28) | — | — |
| `0x00528060` | FUN_00528060 | — | 0.0 % (0) | 0.2 % (27) | — | — |
| `0x0047d9c0` | FUN_0047d9c0 | render-node visit (model lookup, LOD choice) | 0.0 % (0) | 0.2 % (23) | — | — |
| `0x004a47f0` | FUN_004a47f0 | — | 0.0 % (0) | 0.2 % (22) | — | — |
| `0x005112c4` | _malloc | — | 0.0 % (0) | 0.2 % (20) | — | — |
| `0x00527fc2` | FID_conflict:__sopen_helper | — | 0.0 % (0) | 0.1 % (18) | — | — |
| `0x004bb240` | FUN_004bb240 | — | 0.0 % (0) | 0.1 % (17) | — | — |
| `0x004bbb10` | FUN_004bbb10 | mesh build: vertex writer (int16 x 1/16384 from 0x005655d4) | 0.0 % (0) | 0.1 % (17) | — | — |
| `0x004e1b00` | FUN_004e1b00 | — | 0.0 % (0) | 0.1 % (15) | — | — |
| `0x00519434` | FUN_00519434 | — | 0.0 % (0) | 0.1 % (12) | — | — |
| `0x0044d9c0` | FUN_0044d9c0 | — | 0.0 % (0) | 0.1 % (10) | — | — |
| `0x00516ee7` | FUN_00516ee7 | — | 0.0 % (0) | 0.1 % (9) | — | — |
| `0x0047d5e0` | FUN_0047d5e0 | — | 0.0 % (0) | 0.1 % (8) | — | — |
| `0x00486310` | FUN_00486310 | — | 0.0 % (0) | 0.0 % (6) | — | — |
| `0x004bc080` | FUN_004bc080 | — | 0.0 % (0) | 0.0 % (6) | — | — |
| `0x004be7d0` | FUN_004be7d0 | — | 0.0 % (0) | 0.0 % (6) | — | — |
| `0x004bccc0` | FUN_004bccc0 | — | 0.0 % (0) | 0.0 % (5) | — | — |
| `0x004db520` | FUN_004db520 | — | 0.0 % (0) | 0.0 % (5) | — | — |
| `0x0052b960` | FUN_0052b960 | — | 0.0 % (0) | 0.0 % (4) | — | — |
| `0x004d2fc0` | FUN_004d2fc0 | — | 0.0 % (0) | 0.0 % (4) | — | — |
| `0x004d34b0` | FUN_004d34b0 | Win32 message pump (PeekMessageA / GetMessageA) | 0.0 % (0) | 0.0 % (4) | — | — |
| (no main-module frame) | | | | 97.2 % (136,028) | | |

**Top caller pairs**

| Slot | Frame | Caller | Samples |
| ---: | --- | --- | ---: |
| 0 | `0xc522d` FUN_004c4fc0 (material submission wrapper) | `0x7e07b` FUN_0047d9c0 (render-node visit (model lookup, LOD choice)) | 705 |
| 0 | `0xe8e8d` FUN_004e8e10 (resource load = open + read + close) | `0xe8eb2` FUN_004e8e10 (resource load = open + read + close) | 571 |
| 0 | `0x11204f` FUN_00511f77 (CRT fread body (from _fread_s, uses __VEC_memcpy)) | `0xe8da9` FUN_004e8880 (resource read) | 310 |
| 0 | `0x11dab3` __VEC_memcpy | `0x11204f` FUN_00511f77 (CRT fread body (from _fread_s, uses __VEC_memcpy)) | 308 |
| 0 | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise; calls ID3DXMesh::GenerateAdjacency(1e-6f from 0x00565600) at 0x004bc76a) | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise; calls ID3DXMesh::GenerateAdjacency(1e-6f from 0x00565600) at 0x004bc76a) | 168 |
| 0 | `0xbcc30` FUN_004bcb60 (mesh build: CloneMesh into final options (D3DXMESH_32BIT above 64k vertices), two attempts, OOM callback, then install via 0x004bc9c0) | `0xbb9ac` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 160 |
| 0 | `0xc522d` FUN_004c4fc0 (material submission wrapper) | `0x7e76e` FUN_0047e6e0 (deferred draw list drain) | 143 |
| 0 | `0xe2a4b` FUN_004e29f0 (collision helper wrapper (0x004e2780, 0x0052b5d0)) | `0x7f32e` FUN_0047f1b0 | 110 |
| 0 | `0x1240bd` __VEC_memzero | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise; calls ID3DXMesh::GenerateAdjacency(1e-6f from 0x00565600) at 0x004bc76a) | 110 |
| 0 | `0x11f665` FUN_0051f464 (CRT _read text-mode body (ReadFile, MultiByteToWideChar, __malloc_crt)) | `0x11f665` FUN_0051f464 (CRT _read text-mode body (ReadFile, MultiByteToWideChar, __malloc_crt)) | 99 |
| 0 | `0xeeaf9` FUN_004eeab0 (thin wrapper over 0x004db520) | `0x8b2b0` FUN_0048b0b0 | 75 |
| 0 | `0xdd6a2` FUN_004dd2c0 (jpg/tga texture load body: resource load 0x004e8e10 + D3DXGetImageInfo + 2D helper + LoadSurface (callee of 0x004f3510)) | `0xdd6a2` FUN_004dd2c0 (jpg/tga texture load body: resource load 0x004e8e10 + D3DXGetImageInfo + 2D helper + LoadSurface (callee of 0x004f3510)) | 62 |

**Candidates**

Hooked calls account for 11.660 s of the 26.616 s interval (43.8 %); 14.956 s is unexplained by any hook. The sampler recorded 139,983 samples over 35.008 s of report coverage (3999 samples/s, 350 ticks/s per thread); the dominant leaf module is ntdll at 73.8 %. 2 of 7 delta blocks straddle the interval boundary and carry samples from outside it. Function `0x004c4fc0` (FUN_004c4fc0) holds 0.0 % of the engine thread's samples as leaf and 6.9 % as first main-module frame; it is a known routine (material submission wrapper). Function `0x004e8e10` (FUN_004e8e10) holds 0.0 % of the engine thread's samples as leaf and 4.7 % as first main-module frame; it is a known routine (resource load = open + read + close). Function `0x00511f77` (FUN_00511f77) holds 0.0 % of the engine thread's samples as leaf and 2.9 % as first main-module frame; it is a known routine (CRT fread body (from _fread_s, uses __VEC_memcpy)).

### Gap 2 stall 2: report stall 11.803 s

Interval 55.594–67.396 s, length 11.803 s; hooked exclusive 5.378 s over 1 report windows (0 straddling); unexplained 6.425 s.
Samples 56,534 in 3 delta blocks (2 straddling), 15.010 s covered, 3766 samples/s, 241 ticks/s per thread, 0 dropped; sampler tick 1339 µs mean / 28529 µs max, busy 33.5 % of the covered time.

**Hooked time**

| Operation | Calls | Incl. s | Excl. s | Mean ms | Bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `CreateFileA` | 1,457 | 3.487 | 3.487 | 2.393 | 0 |
| `FindFirstFileA` | 1,479 | 0.715 | 0.715 | 0.484 | 0 |
| `inflate` | 24,497 | 0.396 | 0.396 | 0.016 | 0 |
| `xmlReadMemory` | 790 | 0.219 | 0.219 | 0.277 | 22,531,669 |
| `ReadFile` | 11,753 | 0.164 | 0.164 | 0.014 | 35,025,591 |
| `ID3DXMesh::GenerateAdjacency` | 108 | 0.108 | 0.108 | 1.004 | 0 |
| `ID3DXMesh::OptimizeInplace` | 108 | 0.061 | 0.061 | 0.567 | 0 |
| `D3DXCreateTextureFromFileInMemoryEx` | 52 | 0.060 | 0.060 | 1.152 | 52,959,436 |
| `FindNextFileA` | 2,184 | 0.053 | 0.053 | 0.024 | 0 |
| `gzread` | 59,539 | 0.040 | 0.040 | 0.001 | 243,370 |
| `D3DXCleanMesh` | 108 | 0.035 | 0.035 | 0.323 | 0 |
| `D3DXCreateMesh` | 108 | 0.026 | 0.026 | 0.239 | 0 |

**Sampled attribution: per-thread module split**

| Slot | TID | Samples | x3ap | ntdll | wine | d3dx | zlib | xml | proxy | other | Start |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 0 | 216 | 3,620 | 0 | 20 | 0 | 0 | 0 | 0 | 0 | 3,600 | 0:0x112ead |
| 1 | 300 | 3,620 | 0 | 3,620 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xb220 |
| 2 | 304 | 3,620 | 0 | 3,620 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xc780 |
| 3 | 316 | 3,620 | 0 | 3,620 | 0 | 0 | 0 | 0 | 0 | 0 | 49:0x38490 |
| 4 | 320 | 3,620 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 3,620 | 4:0xd0f0 |
| 5 | 328 | 3,620 | 0 | 3,620 | 0 | 0 | 0 | 0 | 0 | 0 | 20:0xe740 |
| 6 | 352 | 3,620 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 3,620 | 38:0x2400 |
| 7 | 356 | 3,620 | 0 | 3,620 | 0 | 0 | 0 | 0 | 0 | 0 | 7:0x9900 |
| 8 | 488 | 2,899 | 0 | 2,899 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x9f90 |
| 9 | 372 | 3,620 | 0 | 3,620 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 10 | 408 | 3,620 | 0 | 3,620 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 11 | 492 | 2,899 | 0 | 2,899 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 12 | 496 | 2,899 | 0 | 2,899 | 0 | 0 | 0 | 0 | 0 | 0 | 59:0x13a60 |
| 13 | 448 | 3,620 | 0 | 3,620 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 14 | 500 | 2,899 | 0 | 2,899 | 0 | 0 | 0 | 0 | 0 | 0 | 59:0x12e30 |
| 15 | 504 | 2,572 | 0 | 2,572 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 16 | 520 | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 16 | 568 | 272 | 0 | 272 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 17 | 536 | 2,273 | 0 | 2,273 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |

All threads: x3ap 0.0 %, ntdll 80.8 %, wine 0.0 %, d3dx 0.0 %, zlib 0.0 %, xml 0.0 %, proxy 0.0 %, other 19.2 %.

**Top functions (leaf samples by containing function)**

Shares are of the engine thread (slot 0, 3,620 samples); the other sampled threads are idle Wine/audio/input waiters.

| Function start | Ghidra name | Label | Self (engine) | Incl. frame (engine) | Busiest slot | Hooked APIs in interval |
| --- | --- | --- | ---: | ---: | ---: | --- |
| `0x005112c4` | _malloc | — | 0.0 % (0) | 15.5 % (561) | — | — |
| `0x00511f77` | FUN_00511f77 | CRT fread body (from _fread_s, uses __VEC_memcpy) | 0.0 % (0) | 15.3 % (555) | — | — |
| `0x00517d8f` | FUN_00517d8f | CRT file-lock release tail (LeaveCriticalSection, from 0x00524cfa) | 0.0 % (0) | 15.2 % (552) | — | — |
| `0x004e8e10` | FUN_004e8e10 | resource load = open + read + close | 0.0 % (0) | 10.7 % (388) | — | — |
| `0x004c4fc0` | FUN_004c4fc0 | material submission wrapper | 0.0 % (0) | 5.3 % (192) | — | — |
| `0x0050e1b0` | _free | — | 0.0 % (0) | 3.9 % (140) | — | — |
| `0x004dd2c0` | FUN_004dd2c0 | jpg/tga texture load body: resource load 0x004e8e10 + D3DXGetImageInfo + 2D helper + LoadSurface (callee of 0x004f3510) | 0.0 % (0) | 1.9 % (70) | — | `D3DXCreateTextureFromFileInMemoryEx` 52 calls / 0.060 s |
| `0x004dc540` | FUN_004dc540 | texture loader (pck dds; image info, cube, 2D helpers) | 0.0 % (0) | 1.6 % (57) | — | `D3DXCreateTextureFromFileInMemoryEx` 52 calls / 0.060 s; `D3DXCreateCubeTextureFromFileInMemoryEx` 1 calls / 0.001 s |
| `0x004efcc0` | FUN_004efcc0 | handle map insert | 0.0 % (0) | 1.5 % (56) | — | — |
| `0x0051f464` | FUN_0051f464 | CRT _read text-mode body (ReadFile, MultiByteToWideChar, __malloc_crt) | 0.0 % (0) | 1.4 % (51) | — | — |
| `0x004a7f60` | FUN_004a7f60 | — | 0.0 % (0) | 1.4 % (51) | — | — |
| `0x004bc680` | FUN_004bc680 | mesh preparation: adjacency / clean / optimise; calls ID3DXMesh::GenerateAdjacency(1e-6f from 0x00565600) at 0x004bc76a | 0.0 % (0) | 1.4 % (49) | — | `ID3DXMesh::GenerateAdjacency` 108 calls / 0.108 s; `D3DXCleanMesh` 108 calls / 0.035 s; `ID3DXMesh::OptimizeInplace` 108 calls / 0.061 s |
| `0x00517bcb` | __isleadbyte_l | — | 0.0 % (0) | 1.0 % (38) | — | — |
| `0x004bcb60` | FUN_004bcb60 | mesh build: CloneMesh into final options (D3DXMESH_32BIT above 64k vertices), two attempts, OOM callback, then install via 0x004bc9c0 | 0.0 % (0) | 1.0 % (38) | — | — |
| `0x004e8880` | FUN_004e8880 | resource read | 0.0 % (0) | 0.9 % (33) | — | `ReadFile` 11,753 calls / 0.164 s |
| `0x004cabc0` | FUN_004cabc0 | — | 0.0 % (0) | 0.7 % (25) | — | — |
| `0x0040b050` | FUN_0040b050 | — | 0.0 % (0) | 0.7 % (24) | — | — |
| `0x00528060` | FUN_00528060 | — | 0.0 % (0) | 0.6 % (23) | — | — |
| `0x004c0150` | FUN_004c0150 | effect state manager: matrix uploads per draw | 0.0 % (0) | 0.6 % (23) | — | — |
| `0x0040b0ed` | FUN_0040b0ed | — | 0.0 % (0) | 0.6 % (22) | — | — |
| `0x00519434` | FUN_00519434 | — | 0.0 % (0) | 0.6 % (21) | — | — |
| `0x0052403b` | __VEC_memzero | — | 0.0 % (0) | 0.5 % (19) | — | — |
| `0x004e6a40` | FUN_004e6a40 | malloc + memset allocation wrapper (via 0x004b8b60) | 0.0 % (0) | 0.5 % (17) | — | — |
| `0x004bc1c0` | FUN_004bc1c0 | mesh build: vertex positions (int16 x 1/16384) and normals (D3DXVec3Normalize x3) | 0.0 % (0) | 0.4 % (15) | — | — |
| `0x004bb470` | FUN_004bb470 | mesh build: D3DXCreateMesh + vertex/index fill | 0.0 % (0) | 0.4 % (13) | — | `D3DXCreateMesh` 108 calls / 0.026 s |
| `0x005117fa` | __findfirst64i32 | — | 0.0 % (0) | 0.3 % (12) | — | — |
| `0x004efa70` | FUN_004efa70 | overlay/glyph state helper (0x004dce20 -> 0x004ee6e0 == 0x18 -> 0x004b2730) | 0.0 % (0) | 0.3 % (12) | — | — |
| `0x004bb240` | FUN_004bb240 | — | 0.0 % (0) | 0.2 % (7) | — | — |
| `0x0042fe10` | FUN_0042fe10 | — | 0.0 % (0) | 0.2 % (6) | — | — |
| `0x0045ffe0` | FUN_0045ffe0 | — | 0.0 % (0) | 0.1 % (5) | — | — |
| `0x0051da61` | __VEC_memcpy | — | 0.0 % (0) | 0.1 % (4) | — | — |
| `0x00527fc2` | FID_conflict:__sopen_helper | — | 0.0 % (0) | 0.1 % (4) | — | — |
| `0x0051192a` | __findnext64i32 | — | 0.0 % (0) | 0.1 % (3) | — | — |
| `0x0051c81b` | FUN_0051c81b | — | 0.0 % (0) | 0.1 % (3) | — | — |
| `0x00524aa7` | ___lock_fhandle | — | 0.0 % (0) | 0.1 % (3) | — | — |
| `0x0044d9c0` | FUN_0044d9c0 | — | 0.0 % (0) | 0.1 % (3) | — | — |
| `0x004bbb10` | FUN_004bbb10 | mesh build: vertex writer (int16 x 1/16384 from 0x005655d4) | 0.0 % (0) | 0.1 % (3) | — | — |
| `0x0046d080` | FUN_0046d080 | — | 0.0 % (0) | 0.1 % (2) | — | — |
| `0x0047ca40` | FUN_0047ca40 | — | 0.0 % (0) | 0.1 % (2) | — | — |
| `0x004b9110` | FUN_004b9110 | — | 0.0 % (0) | 0.1 % (2) | — | — |
| (no main-module frame) | | | | 94.4 % (53,359) | | |

**Top caller pairs**

| Slot | Frame | Caller | Samples |
| ---: | --- | --- | ---: |
| 0 | `0x117da2` FUN_00517d8f (CRT file-lock release tail (LeaveCriticalSection, from 0x00524cfa)) | `0x124d01` FUN_00524cfa | 552 |
| 0 | `0xe8e8d` FUN_004e8e10 (resource load = open + read + close) | `0xe8eb2` FUN_004e8e10 (resource load = open + read + close) | 388 |
| 0 | `0x11204f` FUN_00511f77 (CRT fread body (from _fread_s, uses __VEC_memcpy)) | `0xe8dc3` FUN_004e8880 (resource read) | 314 |
| 0 | `0x11204f` FUN_00511f77 (CRT fread body (from _fread_s, uses __VEC_memcpy)) | `0x11133e` _malloc | 198 |
| 0 | `0xc522d` FUN_004c4fc0 (material submission wrapper) | `0x7e07b` FUN_0047d9c0 (render-node visit (model lookup, LOD choice)) | 191 |
| 0 | `0x11133e` _malloc | `0xdb69b` FUN_004db520 | 114 |
| 0 | `0x11133e` _malloc | `0xe6a75` FUN_004e6a40 (malloc + memset allocation wrapper (via 0x004b8b60)) | 113 |
| 0 | `0x10e21e` _free | `0x10e23d` _free | 85 |
| 0 | `0xdd6a2` FUN_004dd2c0 (jpg/tga texture load body: resource load 0x004e8e10 + D3DXGetImageInfo + 2D helper + LoadSurface (callee of 0x004f3510)) | `0xdd6a2` FUN_004dd2c0 (jpg/tga texture load body: resource load 0x004e8e10 + D3DXGetImageInfo + 2D helper + LoadSurface (callee of 0x004f3510)) | 70 |
| 0 | `0xdc846` FUN_004dc540 (texture loader (pck dds; image info, cube, 2D helpers)) | `0xdc846` FUN_004dc540 (texture loader (pck dds; image info, cube, 2D helpers)) | 57 |
| 0 | `0xbcc30` FUN_004bcb60 (mesh build: CloneMesh into final options (D3DXMESH_32BIT above 64k vertices), two attempts, OOM callback, then install via 0x004bc9c0) | `0xbb9ac` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 38 |
| 0 | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise; calls ID3DXMesh::GenerateAdjacency(1e-6f from 0x00565600) at 0x004bc76a) | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise; calls ID3DXMesh::GenerateAdjacency(1e-6f from 0x00565600) at 0x004bc76a) | 33 |

**Candidates**

Hooked calls account for 5.378 s of the 11.803 s interval (45.6 %); 6.425 s is unexplained by any hook. The sampler recorded 56,534 samples over 15.010 s of report coverage (3766 samples/s, 241 ticks/s per thread); the dominant leaf module is ntdll at 80.8 %. 2 of 3 delta blocks straddle the interval boundary and carry samples from outside it. Function `0x005112c4` (_malloc) holds 0.0 % of the engine thread's samples as leaf and 15.5 % as first main-module frame; it is not a known routine. Function `0x00511f77` (FUN_00511f77) holds 0.0 % of the engine thread's samples as leaf and 15.3 % as first main-module frame; it is a known routine (CRT fread body (from _fread_s, uses __VEC_memcpy)). Function `0x00517d8f` (FUN_00517d8f) holds 0.0 % of the engine thread's samples as leaf and 15.2 % as first main-module frame; it is a known routine (CRT file-lock release tail (LeaveCriticalSection, from 0x00524cfa)).

## Gap 3: menu load (7.380 s, ends at frame 1539)

Label evidence: 1017 GenerateAdjacency calls and 298,138,122 B of 2D texture-helper input match the main-menu work vector.
Interval 82.604–90.391 s, length 7.380 s; hooked exclusive 4.615 s over 8 report windows (2 straddling); unexplained 2.765 s.
Samples 85,191 in 3 delta blocks (2 straddling), 15.005 s covered, 5678 samples/s, 300 ticks/s per thread, 0 dropped; sampler tick 1014 µs mean / 29208 µs max, busy 31.2 % of the covered time.

**Hooked time**

| Operation | Calls | Incl. s | Excl. s | Mean ms | Bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `inflate` | 166,017 | 2.638 | 2.638 | 0.016 | 0 |
| `D3DXCreateTextureFromFileInMemoryEx` | 459 | 0.502 | 0.502 | 1.093 | 298,138,122 |
| `ID3DXMesh::GenerateAdjacency` | 1,017 | 0.412 | 0.412 | 0.405 | 0 |
| `ReadFile` | 43,380 | 0.321 | 0.321 | 0.007 | 183,541,385 |
| `D3DXCreateMesh` | 1,017 | 0.246 | 0.246 | 0.241 | 0 |
| `ID3DXMesh::OptimizeInplace` | 1,017 | 0.208 | 0.208 | 0.204 | 0 |
| `D3DXCleanMesh` | 1,023 | 0.119 | 0.119 | 0.116 | 0 |
| `CreateFileA` | 584 | 0.062 | 0.062 | 0.107 | 0 |
| `D3DXLoadSurfaceFromFileInMemory` | 3 | 0.050 | 0.050 | 16.582 | 2,480,817 |
| `FindFirstFileA` | 686 | 0.020 | 0.020 | 0.029 | 0 |
| `xmlReadMemory` | 2 | 0.020 | 0.020 | 9.943 | 3,688,819 |
| `D3DXCreateCubeTextureFromFileInMemoryEx` | 9 | 0.016 | 0.016 | 1.738 | 1,509,024 |

**Sampled attribution: per-thread module split**

| Slot | TID | Samples | x3ap | ntdll | wine | d3dx | zlib | xml | proxy | other | Start |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 0 | 216 | 4,503 | 0 | 78 | 0 | 0 | 0 | 0 | 0 | 4,425 | 0:0x112ead |
| 1 | 300 | 4,503 | 0 | 4,503 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xb220 |
| 2 | 304 | 4,503 | 0 | 4,503 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xc780 |
| 3 | 316 | 4,503 | 0 | 4,503 | 0 | 0 | 0 | 0 | 0 | 0 | 49:0x38490 |
| 4 | 320 | 4,503 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4,503 | 4:0xd0f0 |
| 5 | 328 | 4,503 | 0 | 4,503 | 0 | 0 | 0 | 0 | 0 | 0 | 20:0xe740 |
| 6 | 352 | 4,503 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4,503 | 38:0x2400 |
| 7 | 356 | 4,503 | 0 | 4,503 | 0 | 0 | 0 | 0 | 0 | 0 | 7:0x9900 |
| 8 | 668 | 7 | 0 | 7 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x9f90 |
| 8 | 772 | 794 | 0 | 794 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x9f90 |
| 9 | 372 | 4,503 | 0 | 4,503 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 10 | 408 | 4,503 | 0 | 4,503 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 11 | 672 | 3,512 | 0 | 3,512 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 12 | 708 | 3,224 | 0 | 3,224 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 13 | 448 | 4,503 | 0 | 4,503 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 14 | 748 | 3,224 | 0 | 3,224 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 15 | 504 | 4,503 | 0 | 4,503 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 16 | 568 | 4,503 | 0 | 4,503 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 17 | 536 | 4,503 | 0 | 4,503 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 18 | 600 | 4,503 | 0 | 4,503 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 19 | 636 | 4,503 | 0 | 4,503 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 20 | 776 | 794 | 0 | 794 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 21 | 780 | 794 | 0 | 794 | 0 | 0 | 0 | 0 | 0 | 0 | 59:0x13a60 |
| 22 | 784 | 794 | 0 | 794 | 0 | 0 | 0 | 0 | 0 | 0 | 59:0x12e30 |

All threads: x3ap 0.0 %, ntdll 84.2 %, wine 0.0 %, d3dx 0.0 %, zlib 0.0 %, xml 0.0 %, proxy 0.0 %, other 15.8 %.

**Top functions (leaf samples by containing function)**

Shares are of the engine thread (slot 0, 4,503 samples); the other sampled threads are idle Wine/audio/input waiters.

| Function start | Ghidra name | Label | Self (engine) | Incl. frame (engine) | Busiest slot | Hooked APIs in interval |
| --- | --- | --- | ---: | ---: | ---: | --- |
| `0x004b4f80` | FUN_004b4f80 | CPureDeviceStateManager::SetRenderState (filters via 0x004b5620, then device vtable+0xe4) | 0.0 % (0) | 19.2 % (864) | — | — |
| `0x004e8e10` | FUN_004e8e10 | resource load = open + read + close | 0.0 % (0) | 17.9 % (804) | — | — |
| `0x004e3280` | FUN_004e3280 | collision: OBB/OBB separating-axis test (15 axes, fabsf 0x0040e710, 1e-6 margin from 0x00565600; returns failing axis index) | 0.0 % (0) | 9.1 % (411) | — | — |
| `0x004efa70` | FUN_004efa70 | overlay/glyph state helper (0x004dce20 -> 0x004ee6e0 == 0x18 -> 0x004b2730) | 0.0 % (0) | 4.1 % (184) | — | — |
| `0x004e1220` | FUN_004e1220 | collision tree: recursive split body (self-recursive, 2,267 B) - top loading hot spot | 0.0 % (0) | 3.9 % (177) | — | — |
| `0x004bcb60` | FUN_004bcb60 | mesh build: CloneMesh into final options (D3DXMESH_32BIT above 64k vertices), two attempts, OOM callback, then install via 0x004bc9c0 | 0.0 % (0) | 3.0 % (134) | — | — |
| `0x004b51b0` | FUN_004b51b0 | — | 0.0 % (0) | 2.3 % (102) | — | — |
| `0x004bc680` | FUN_004bc680 | mesh preparation: adjacency / clean / optimise; calls ID3DXMesh::GenerateAdjacency(1e-6f from 0x00565600) at 0x004bc76a | 0.0 % (0) | 2.2 % (99) | — | `ID3DXMesh::GenerateAdjacency` 1,017 calls / 0.412 s; `D3DXCleanMesh` 1,023 calls / 0.119 s; `ID3DXMesh::OptimizeInplace` 1,017 calls / 0.208 s |
| `0x0050e1b0` | _free | — | 0.0 % (0) | 2.2 % (98) | — | — |
| `0x004dc540` | FUN_004dc540 | texture loader (pck dds; image info, cube, 2D helpers) | 0.0 % (0) | 2.2 % (97) | — | `D3DXCreateTextureFromFileInMemoryEx` 459 calls / 0.502 s; `D3DXCreateCubeTextureFromFileInMemoryEx` 9 calls / 0.016 s |
| `0x004b50a0` | FUN_004b50a0 | — | 0.0 % (0) | 2.0 % (92) | — | — |
| `0x0052403b` | __VEC_memzero | — | 0.0 % (0) | 1.7 % (76) | — | — |
| `0x005112c4` | _malloc | — | 0.0 % (0) | 1.6 % (71) | — | — |
| `0x004e8880` | FUN_004e8880 | resource read | 0.0 % (0) | 1.5 % (68) | — | `ReadFile` 43,380 calls / 0.321 s |
| `0x0051da61` | __VEC_memcpy | — | 0.0 % (0) | 1.5 % (66) | — | — |
| `0x004b5720` | FUN_004b5720 | — | 0.0 % (0) | 1.3 % (59) | — | — |
| `0x0040b050` | FUN_0040b050 | — | 0.0 % (0) | 1.2 % (52) | — | — |
| `0x004bb470` | FUN_004bb470 | mesh build: D3DXCreateMesh + vertex/index fill | 0.0 % (0) | 1.1 % (50) | — | `D3DXCreateMesh` 1,017 calls / 0.246 s |
| `0x004b9110` | FUN_004b9110 | — | 0.0 % (0) | 1.1 % (49) | — | — |
| `0x00511f77` | FUN_00511f77 | CRT fread body (from _fread_s, uses __VEC_memcpy) | 0.0 % (0) | 1.0 % (46) | — | — |
| `0x004bc1c0` | FUN_004bc1c0 | mesh build: vertex positions (int16 x 1/16384) and normals (D3DXVec3Normalize x3) | 0.0 % (0) | 1.0 % (44) | — | — |
| `0x00519434` | FUN_00519434 | — | 0.0 % (0) | 0.8 % (38) | — | — |
| `0x00528060` | FUN_00528060 | — | 0.0 % (0) | 0.8 % (35) | — | — |
| `0x00452ad0` | FUN_00452ad0 | — | 0.0 % (0) | 0.8 % (35) | — | — |
| `0x004c0150` | FUN_004c0150 | effect state manager: matrix uploads per draw | 0.0 % (0) | 0.8 % (34) | — | — |
| `0x00524aa7` | ___lock_fhandle | — | 0.0 % (0) | 0.7 % (32) | — | — |
| `0x004b5340` | FUN_004b5340 | — | 0.0 % (0) | 0.7 % (31) | — | — |
| `0x004cf460` | FUN_004cf460 | DirectShow movie playback setup (CoCreateInstance of the X MPEG filter graph, Data\mov\%s, %05d.dat) | 0.0 % (0) | 0.7 % (30) | — | — |
| `0x004bb240` | FUN_004bb240 | — | 0.0 % (0) | 0.6 % (29) | — | — |
| `0x0050f786` | _memcpy_s | — | 0.0 % (0) | 0.6 % (28) | — | — |
| `0x004b5160` | FUN_004b5160 | — | 0.0 % (0) | 0.5 % (22) | — | — |
| `0x004b52a0` | FUN_004b52a0 | — | 0.0 % (0) | 0.4 % (20) | — | — |
| `0x0042fe10` | FUN_0042fe10 | — | 0.0 % (0) | 0.4 % (19) | — | — |
| `0x004b9ed0` | FUN_004b9ed0 | — | 0.0 % (0) | 0.4 % (18) | — | — |
| `0x00470490` | FUN_00470490 | — | 0.0 % (0) | 0.4 % (16) | — | — |
| `0x00517d8f` | FUN_00517d8f | CRT file-lock release tail (LeaveCriticalSection, from 0x00524cfa) | 0.0 % (0) | 0.3 % (15) | — | — |
| `0x0051f464` | FUN_0051f464 | CRT _read text-mode body (ReadFile, MultiByteToWideChar, __malloc_crt) | 0.0 % (0) | 0.3 % (14) | — | — |
| `0x00527fc2` | FID_conflict:__sopen_helper | — | 0.0 % (0) | 0.3 % (13) | — | — |
| `0x004b4600` | FUN_004b4600 | — | 0.0 % (0) | 0.2 % (10) | — | — |
| `0x004dd2c0` | FUN_004dd2c0 | jpg/tga texture load body: resource load 0x004e8e10 + D3DXGetImageInfo + 2D helper + LoadSurface (callee of 0x004f3510) | 0.0 % (0) | 0.2 % (10) | — | `D3DXCreateTextureFromFileInMemoryEx` 459 calls / 0.502 s; `D3DXLoadSurfaceFromFileInMemory` 3 calls / 0.050 s |
| (no main-module frame) | | | | 95.0 % (80,909) | | |

**Top caller pairs**

| Slot | Frame | Caller | Samples |
| ---: | --- | --- | ---: |
| 0 | `0xe8e8d` FUN_004e8e10 (resource load = open + read + close) | `0xe8eb2` FUN_004e8e10 (resource load = open + read + close) | 804 |
| 0 | `0xe3355` FUN_004e3280 (collision: OBB/OBB separating-axis test (15 axes, fabsf 0x0040e710, 1e-6 margin from 0x00565600; returns failing axis index)) | `0xe25a8` FUN_004e2530 (collision query: recursive OBB-tree pair descent (children +0x3c/+0x40, half-extents +0x30..+0x38, leaf test 0x004e2190, budget DAT_0060854c)) | 411 |
| 0 | `0xb4f99` FUN_004b4f80 (CPureDeviceStateManager::SetRenderState (filters via 0x004b5620, then device vtable+0xe4)) | `0xc403e` FUN_004c0150 (effect state manager: matrix uploads per draw) | 406 |
| 0 | `0xb4fad` FUN_004b4f80 (CPureDeviceStateManager::SetRenderState (filters via 0x004b5620, then device vtable+0xe4)) | `0xb4f99` FUN_004b4f80 (CPureDeviceStateManager::SetRenderState (filters via 0x004b5620, then device vtable+0xe4)) | 268 |
| 0 | `0xefab8` FUN_004efa70 (overlay/glyph state helper (0x004dce20 -> 0x004ee6e0 == 0x18 -> 0x004b2730)) | `0xf8dfe` FUN_004f8600 (text/overlay layout and string formatting (calls 0x004efa70)) | 184 |
| 0 | `0xbcc30` FUN_004bcb60 (mesh build: CloneMesh into final options (D3DXMESH_32BIT above 64k vertices), two attempts, OOM callback, then install via 0x004bc9c0) | `0xbb9ac` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 134 |
| 0 | `0xdc846` FUN_004dc540 (texture loader (pck dds; image info, cube, 2D helpers)) | `0xdc846` FUN_004dc540 (texture loader (pck dds; image info, cube, 2D helpers)) | 97 |
| 0 | `0xb5207` FUN_004b51b0 | `0xb5207` FUN_004b51b0 | 94 |
| 0 | `0xb50c1` FUN_004b50a0 | `0xb50c1` FUN_004b50a0 | 92 |
| 0 | `0xe8da9` FUN_004e8880 (resource read) | `0xe8da9` FUN_004e8880 (resource read) | 68 |
| 0 | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise; calls ID3DXMesh::GenerateAdjacency(1e-6f from 0x00565600) at 0x004bc76a) | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise; calls ID3DXMesh::GenerateAdjacency(1e-6f from 0x00565600) at 0x004bc76a) | 63 |
| 0 | `0x11dab3` __VEC_memcpy | `0x11204f` FUN_00511f77 (CRT fread body (from _fread_s, uses __VEC_memcpy)) | 62 |

**Candidates**

Hooked calls account for 4.615 s of the 7.380 s interval (62.5 %); 2.765 s is unexplained by any hook. The sampler recorded 85,191 samples over 15.005 s of report coverage (5678 samples/s, 300 ticks/s per thread); the dominant leaf module is ntdll at 84.2 %. 2 of 3 delta blocks straddle the interval boundary and carry samples from outside it. Function `0x004b4f80` (FUN_004b4f80) holds 0.0 % of the engine thread's samples as leaf and 19.2 % as first main-module frame; it is a known routine (CPureDeviceStateManager::SetRenderState (filters via 0x004b5620, then device vtable+0xe4)). Function `0x004e8e10` (FUN_004e8e10) holds 0.0 % of the engine thread's samples as leaf and 17.9 % as first main-module frame; it is a known routine (resource load = open + read + close). Function `0x004e3280` (FUN_004e3280) holds 0.0 % of the engine thread's samples as leaf and 9.1 % as first main-module frame; it is a known routine (collision: OBB/OBB separating-axis test (15 axes, fabsf 0x0040e710, 1e-6 margin from 0x00565600; returns failing axis index)).

### Gap 3 stall 1: report stall 2.482 s

Interval 80.405–82.888 s, length 2.482 s; hooked exclusive 0.003 s over 1 report windows (0 straddling); unexplained 2.479 s.
Samples 25,984 in 1 delta blocks (1 straddling), 5.002 s covered, 5194 samples/s, 311 ticks/s per thread, 0 dropped; sampler tick 893 µs mean / 29208 µs max, busy 28.4 % of the covered time.

**Hooked time**

| Operation | Calls | Incl. s | Excl. s | Mean ms | Bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `ReadFile` | 3 | 0.003 | 0.003 | 0.868 | 37,376 |
| `CreateFileA` | 1 | 0.000 | 0.000 | 0.114 | 0 |
| `FindFirstFileA` | 1 | 0.000 | 0.000 | 0.080 | 0 |
| `SetFilePointer` | 3 | 0.000 | 0.000 | 0.003 | 0 |

**Sampled attribution: per-thread module split**

| Slot | TID | Samples | x3ap | ntdll | wine | d3dx | zlib | xml | proxy | other | Start |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 0 | 216 | 1,554 | 0 | 7 | 0 | 0 | 0 | 0 | 0 | 1,547 | 0:0x112ead |
| 1 | 300 | 1,554 | 0 | 1,554 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xb220 |
| 2 | 304 | 1,554 | 0 | 1,554 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xc780 |
| 3 | 316 | 1,554 | 0 | 1,554 | 0 | 0 | 0 | 0 | 0 | 0 | 49:0x38490 |
| 4 | 320 | 1,554 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 1,554 | 4:0xd0f0 |
| 5 | 328 | 1,554 | 0 | 1,554 | 0 | 0 | 0 | 0 | 0 | 0 | 20:0xe740 |
| 6 | 352 | 1,554 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 1,554 | 38:0x2400 |
| 7 | 356 | 1,554 | 0 | 1,554 | 0 | 0 | 0 | 0 | 0 | 0 | 7:0x9900 |
| 8 | 668 | 7 | 0 | 7 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x9f90 |
| 9 | 372 | 1,554 | 0 | 1,554 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 10 | 408 | 1,554 | 0 | 1,554 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 11 | 672 | 563 | 0 | 563 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 12 | 708 | 275 | 0 | 275 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 13 | 448 | 1,554 | 0 | 1,554 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 14 | 748 | 275 | 0 | 275 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 15 | 504 | 1,554 | 0 | 1,554 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 16 | 568 | 1,554 | 0 | 1,554 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 17 | 536 | 1,554 | 0 | 1,554 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 18 | 600 | 1,554 | 0 | 1,554 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 19 | 636 | 1,554 | 0 | 1,554 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |

All threads: x3ap 0.0 %, ntdll 82.1 %, wine 0.0 %, d3dx 0.0 %, zlib 0.0 %, xml 0.0 %, proxy 0.0 %, other 17.9 %.

**Top functions (leaf samples by containing function)**

Shares are of the engine thread (slot 0, 1,554 samples); the other sampled threads are idle Wine/audio/input waiters.

| Function start | Ghidra name | Label | Self (engine) | Incl. frame (engine) | Busiest slot | Hooked APIs in interval |
| --- | --- | --- | ---: | ---: | ---: | --- |
| `0x004e3280` | FUN_004e3280 | collision: OBB/OBB separating-axis test (15 axes, fabsf 0x0040e710, 1e-6 margin from 0x00565600; returns failing axis index) | 0.0 % (0) | 26.4 % (411) | — | — |
| `0x004b4f80` | FUN_004b4f80 | CPureDeviceStateManager::SetRenderState (filters via 0x004b5620, then device vtable+0xe4) | 0.0 % (0) | 14.9 % (232) | — | — |
| `0x004e1220` | FUN_004e1220 | collision tree: recursive split body (self-recursive, 2,267 B) - top loading hot spot | 0.0 % (0) | 9.7 % (150) | — | — |
| `0x004e8e10` | FUN_004e8e10 | resource load = open + read + close | 0.0 % (0) | 5.0 % (77) | — | — |
| `0x004e8880` | FUN_004e8880 | resource read | 0.0 % (0) | 4.4 % (68) | — | `ReadFile` 3 calls / 0.003 s |
| `0x005112c4` | _malloc | — | 0.0 % (0) | 3.0 % (46) | — | — |
| `0x0050e1b0` | _free | — | 0.0 % (0) | 2.8 % (44) | — | — |
| `0x004cf460` | FUN_004cf460 | DirectShow movie playback setup (CoCreateInstance of the X MPEG filter graph, Data\mov\%s, %05d.dat) | 0.0 % (0) | 1.9 % (30) | — | — |
| `0x0050f786` | _memcpy_s | — | 0.0 % (0) | 1.8 % (28) | — | — |
| `0x00511f77` | FUN_00511f77 | CRT fread body (from _fread_s, uses __VEC_memcpy) | 0.0 % (0) | 1.5 % (24) | — | — |
| `0x0042fe10` | FUN_0042fe10 | — | 0.0 % (0) | 1.2 % (19) | — | — |
| `0x00470490` | FUN_00470490 | — | 0.0 % (0) | 1.0 % (16) | — | — |
| `0x00517d8f` | FUN_00517d8f | CRT file-lock release tail (LeaveCriticalSection, from 0x00524cfa) | 0.0 % (0) | 1.0 % (15) | — | — |
| `0x004b51b0` | FUN_004b51b0 | — | 0.0 % (0) | 0.8 % (13) | — | — |
| `0x004b4600` | FUN_004b4600 | — | 0.0 % (0) | 0.6 % (10) | — | — |
| `0x004bc680` | FUN_004bc680 | mesh preparation: adjacency / clean / optimise; calls ID3DXMesh::GenerateAdjacency(1e-6f from 0x00565600) at 0x004bc76a | 0.0 % (0) | 0.6 % (10) | — | — |
| `0x004bcb60` | FUN_004bcb60 | mesh build: CloneMesh into final options (D3DXMESH_32BIT above 64k vertices), two attempts, OOM callback, then install via 0x004bc9c0 | 0.0 % (0) | 0.6 % (10) | — | — |
| `0x004dc540` | FUN_004dc540 | texture loader (pck dds; image info, cube, 2D helpers) | 0.0 % (0) | 0.6 % (10) | — | — |
| `0x00512213` | FUN_00512213 | — | 0.0 % (0) | 0.6 % (9) | — | — |
| `0x00519434` | FUN_00519434 | — | 0.0 % (0) | 0.6 % (9) | — | — |
| `0x004b50a0` | FUN_004b50a0 | — | 0.0 % (0) | 0.6 % (9) | — | — |
| `0x0052403b` | __VEC_memzero | — | 0.0 % (0) | 0.5 % (8) | — | — |
| `0x004b5720` | FUN_004b5720 | — | 0.0 % (0) | 0.5 % (8) | — | — |
| `0x004c0150` | FUN_004c0150 | effect state manager: matrix uploads per draw | 0.0 % (0) | 0.5 % (8) | — | — |
| `0x00528060` | FUN_00528060 | — | 0.0 % (0) | 0.4 % (6) | — | — |
| (no main-module frame) | | | | 94.8 % (24,630) | | |

**Top caller pairs**

| Slot | Frame | Caller | Samples |
| ---: | --- | --- | ---: |
| 0 | `0xe3355` FUN_004e3280 (collision: OBB/OBB separating-axis test (15 axes, fabsf 0x0040e710, 1e-6 margin from 0x00565600; returns failing axis index)) | `0xe25a8` FUN_004e2530 (collision query: recursive OBB-tree pair descent (children +0x3c/+0x40, half-extents +0x30..+0x38, leaf test 0x004e2190, budget DAT_0060854c)) | 411 |
| 0 | `0xb4f99` FUN_004b4f80 (CPureDeviceStateManager::SetRenderState (filters via 0x004b5620, then device vtable+0xe4)) | `0xc403e` FUN_004c0150 (effect state manager: matrix uploads per draw) | 97 |
| 0 | `0xe8e8d` FUN_004e8e10 (resource load = open + read + close) | `0xe8eb2` FUN_004e8e10 (resource load = open + read + close) | 77 |
| 0 | `0xe8da9` FUN_004e8880 (resource read) | `0xe8da9` FUN_004e8880 (resource read) | 68 |
| 0 | `0xb4fad` FUN_004b4f80 (CPureDeviceStateManager::SetRenderState (filters via 0x004b5620, then device vtable+0xe4)) | `0xb4f99` FUN_004b4f80 (CPureDeviceStateManager::SetRenderState (filters via 0x004b5620, then device vtable+0xe4)) | 46 |
| 0 | `0xe1538` FUN_004e1220 (collision tree: recursive split body (self-recursive, 2,267 B) - top loading hot spot) | `0xe1538` FUN_004e1220 (collision tree: recursive split body (self-recursive, 2,267 B) - top loading hot spot) | 42 |
| 0 | `0xe1a18` FUN_004e1220 (collision tree: recursive split body (self-recursive, 2,267 B) - top loading hot spot) | `0xe1538` FUN_004e1220 (collision tree: recursive split body (self-recursive, 2,267 B) - top loading hot spot) | 41 |
| 0 | `0xe1538` FUN_004e1220 (collision tree: recursive split body (self-recursive, 2,267 B) - top loading hot spot) | `0xe1a18` FUN_004e1220 (collision tree: recursive split body (self-recursive, 2,267 B) - top loading hot spot) | 35 |
| 0 | `0xb4f99` FUN_004b4f80 (CPureDeviceStateManager::SetRenderState (filters via 0x004b5620, then device vtable+0xe4)) | `0x11133e` _malloc | 30 |
| 0 | `0xcf589` FUN_004cf460 (DirectShow movie playback setup (CoCreateInstance of the X MPEG filter graph, Data\mov\%s, %05d.dat)) | `0x981d8` FUN_00498140 (movie playback driver (allocates, then 0x004cf460)) | 30 |
| 0 | `0xe1a18` FUN_004e1220 (collision tree: recursive split body (self-recursive, 2,267 B) - top loading hot spot) | `0xe1a18` FUN_004e1220 (collision tree: recursive split body (self-recursive, 2,267 B) - top loading hot spot) | 28 |
| 0 | `0x11133e` _malloc | `0xbaaa` FUN_0040ba30 | 24 |

**Candidates**

Hooked calls account for 0.003 s of the 2.482 s interval (0.1 %); 2.479 s is unexplained by any hook. The sampler recorded 25,984 samples over 5.002 s of report coverage (5194 samples/s, 311 ticks/s per thread); the dominant leaf module is ntdll at 82.1 %. 1 of 1 delta blocks straddle the interval boundary and carry samples from outside it. Function `0x004e3280` (FUN_004e3280) holds 0.0 % of the engine thread's samples as leaf and 26.4 % as first main-module frame; it is a known routine (collision: OBB/OBB separating-axis test (15 axes, fabsf 0x0040e710, 1e-6 margin from 0x00565600; returns failing axis index)). Function `0x004b4f80` (FUN_004b4f80) holds 0.0 % of the engine thread's samples as leaf and 14.9 % as first main-module frame; it is a known routine (CPureDeviceStateManager::SetRenderState (filters via 0x004b5620, then device vtable+0xe4)). Function `0x004e1220` (FUN_004e1220) holds 0.0 % of the engine thread's samples as leaf and 9.7 % as first main-module frame; it is a known routine (collision tree: recursive split body (self-recursive, 2,267 B) - top loading hot spot).

### Gap 3 stall 2: report stall 2.054 s

Interval 82.888–84.941 s, length 2.054 s; hooked exclusive 0.621 s over 1 report windows (0 straddling); unexplained 1.432 s.
Samples 25,984 in 1 delta blocks (1 straddling), 5.002 s covered, 5194 samples/s, 311 ticks/s per thread, 0 dropped; sampler tick 893 µs mean / 29208 µs max, busy 28.4 % of the covered time.

**Hooked time**

| Operation | Calls | Incl. s | Excl. s | Mean ms | Bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `inflate` | 24,325 | 0.417 | 0.417 | 0.017 | 0 |
| `D3DXLoadSurfaceFromFileInMemory` | 3 | 0.050 | 0.050 | 16.582 | 2,480,817 |
| `ReadFile` | 6,280 | 0.044 | 0.044 | 0.007 | 34,774,665 |
| `D3DXCreateTextureFromFileInMemoryEx` | 16 | 0.042 | 0.042 | 2.619 | 11,805,774 |
| `xmlReadMemory` | 2 | 0.020 | 0.020 | 9.943 | 3,688,819 |
| `CreateFileA` | 64 | 0.016 | 0.016 | 0.257 | 0 |
| `D3DXCreateMesh` | 59 | 0.014 | 0.014 | 0.242 | 0 |
| `ID3DXMesh::GenerateAdjacency` | 59 | 0.006 | 0.006 | 0.101 | 0 |
| `FindFirstFileA` | 103 | 0.006 | 0.006 | 0.058 | 0 |
| `ID3DXMesh::OptimizeInplace` | 59 | 0.004 | 0.004 | 0.059 | 0 |
| `D3DXCleanMesh` | 59 | 0.002 | 0.002 | 0.029 | 0 |
| `SetFilePointer` | 191 | 0.000 | 0.000 | 0.001 | 0 |

**Sampled attribution: per-thread module split**

| Slot | TID | Samples | x3ap | ntdll | wine | d3dx | zlib | xml | proxy | other | Start |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 0 | 216 | 1,554 | 0 | 7 | 0 | 0 | 0 | 0 | 0 | 1,547 | 0:0x112ead |
| 1 | 300 | 1,554 | 0 | 1,554 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xb220 |
| 2 | 304 | 1,554 | 0 | 1,554 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xc780 |
| 3 | 316 | 1,554 | 0 | 1,554 | 0 | 0 | 0 | 0 | 0 | 0 | 49:0x38490 |
| 4 | 320 | 1,554 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 1,554 | 4:0xd0f0 |
| 5 | 328 | 1,554 | 0 | 1,554 | 0 | 0 | 0 | 0 | 0 | 0 | 20:0xe740 |
| 6 | 352 | 1,554 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 1,554 | 38:0x2400 |
| 7 | 356 | 1,554 | 0 | 1,554 | 0 | 0 | 0 | 0 | 0 | 0 | 7:0x9900 |
| 8 | 668 | 7 | 0 | 7 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x9f90 |
| 9 | 372 | 1,554 | 0 | 1,554 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 10 | 408 | 1,554 | 0 | 1,554 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 11 | 672 | 563 | 0 | 563 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 12 | 708 | 275 | 0 | 275 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 13 | 448 | 1,554 | 0 | 1,554 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 14 | 748 | 275 | 0 | 275 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 15 | 504 | 1,554 | 0 | 1,554 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 16 | 568 | 1,554 | 0 | 1,554 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 17 | 536 | 1,554 | 0 | 1,554 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 18 | 600 | 1,554 | 0 | 1,554 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 19 | 636 | 1,554 | 0 | 1,554 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |

All threads: x3ap 0.0 %, ntdll 82.1 %, wine 0.0 %, d3dx 0.0 %, zlib 0.0 %, xml 0.0 %, proxy 0.0 %, other 17.9 %.

**Top functions (leaf samples by containing function)**

Shares are of the engine thread (slot 0, 1,554 samples); the other sampled threads are idle Wine/audio/input waiters.

| Function start | Ghidra name | Label | Self (engine) | Incl. frame (engine) | Busiest slot | Hooked APIs in interval |
| --- | --- | --- | ---: | ---: | ---: | --- |
| `0x004e3280` | FUN_004e3280 | collision: OBB/OBB separating-axis test (15 axes, fabsf 0x0040e710, 1e-6 margin from 0x00565600; returns failing axis index) | 0.0 % (0) | 26.4 % (411) | — | — |
| `0x004b4f80` | FUN_004b4f80 | CPureDeviceStateManager::SetRenderState (filters via 0x004b5620, then device vtable+0xe4) | 0.0 % (0) | 14.9 % (232) | — | — |
| `0x004e1220` | FUN_004e1220 | collision tree: recursive split body (self-recursive, 2,267 B) - top loading hot spot | 0.0 % (0) | 9.7 % (150) | — | — |
| `0x004e8e10` | FUN_004e8e10 | resource load = open + read + close | 0.0 % (0) | 5.0 % (77) | — | — |
| `0x004e8880` | FUN_004e8880 | resource read | 0.0 % (0) | 4.4 % (68) | — | `ReadFile` 6,280 calls / 0.044 s |
| `0x005112c4` | _malloc | — | 0.0 % (0) | 3.0 % (46) | — | — |
| `0x0050e1b0` | _free | — | 0.0 % (0) | 2.8 % (44) | — | — |
| `0x004cf460` | FUN_004cf460 | DirectShow movie playback setup (CoCreateInstance of the X MPEG filter graph, Data\mov\%s, %05d.dat) | 0.0 % (0) | 1.9 % (30) | — | — |
| `0x0050f786` | _memcpy_s | — | 0.0 % (0) | 1.8 % (28) | — | — |
| `0x00511f77` | FUN_00511f77 | CRT fread body (from _fread_s, uses __VEC_memcpy) | 0.0 % (0) | 1.5 % (24) | — | — |
| `0x0042fe10` | FUN_0042fe10 | — | 0.0 % (0) | 1.2 % (19) | — | — |
| `0x00470490` | FUN_00470490 | — | 0.0 % (0) | 1.0 % (16) | — | — |
| `0x00517d8f` | FUN_00517d8f | CRT file-lock release tail (LeaveCriticalSection, from 0x00524cfa) | 0.0 % (0) | 1.0 % (15) | — | — |
| `0x004b51b0` | FUN_004b51b0 | — | 0.0 % (0) | 0.8 % (13) | — | — |
| `0x004b4600` | FUN_004b4600 | — | 0.0 % (0) | 0.6 % (10) | — | — |
| `0x004bc680` | FUN_004bc680 | mesh preparation: adjacency / clean / optimise; calls ID3DXMesh::GenerateAdjacency(1e-6f from 0x00565600) at 0x004bc76a | 0.0 % (0) | 0.6 % (10) | — | `ID3DXMesh::GenerateAdjacency` 59 calls / 0.006 s; `D3DXCleanMesh` 59 calls / 0.002 s; `ID3DXMesh::OptimizeInplace` 59 calls / 0.004 s |
| `0x004bcb60` | FUN_004bcb60 | mesh build: CloneMesh into final options (D3DXMESH_32BIT above 64k vertices), two attempts, OOM callback, then install via 0x004bc9c0 | 0.0 % (0) | 0.6 % (10) | — | — |
| `0x004dc540` | FUN_004dc540 | texture loader (pck dds; image info, cube, 2D helpers) | 0.0 % (0) | 0.6 % (10) | — | `D3DXCreateTextureFromFileInMemoryEx` 16 calls / 0.042 s |
| `0x00512213` | FUN_00512213 | — | 0.0 % (0) | 0.6 % (9) | — | — |
| `0x00519434` | FUN_00519434 | — | 0.0 % (0) | 0.6 % (9) | — | — |
| `0x004b50a0` | FUN_004b50a0 | — | 0.0 % (0) | 0.6 % (9) | — | — |
| `0x0052403b` | __VEC_memzero | — | 0.0 % (0) | 0.5 % (8) | — | — |
| `0x004b5720` | FUN_004b5720 | — | 0.0 % (0) | 0.5 % (8) | — | — |
| `0x004c0150` | FUN_004c0150 | effect state manager: matrix uploads per draw | 0.0 % (0) | 0.5 % (8) | — | — |
| `0x00528060` | FUN_00528060 | — | 0.0 % (0) | 0.4 % (6) | — | — |
| (no main-module frame) | | | | 94.8 % (24,630) | | |

**Top caller pairs**

| Slot | Frame | Caller | Samples |
| ---: | --- | --- | ---: |
| 0 | `0xe3355` FUN_004e3280 (collision: OBB/OBB separating-axis test (15 axes, fabsf 0x0040e710, 1e-6 margin from 0x00565600; returns failing axis index)) | `0xe25a8` FUN_004e2530 (collision query: recursive OBB-tree pair descent (children +0x3c/+0x40, half-extents +0x30..+0x38, leaf test 0x004e2190, budget DAT_0060854c)) | 411 |
| 0 | `0xb4f99` FUN_004b4f80 (CPureDeviceStateManager::SetRenderState (filters via 0x004b5620, then device vtable+0xe4)) | `0xc403e` FUN_004c0150 (effect state manager: matrix uploads per draw) | 97 |
| 0 | `0xe8e8d` FUN_004e8e10 (resource load = open + read + close) | `0xe8eb2` FUN_004e8e10 (resource load = open + read + close) | 77 |
| 0 | `0xe8da9` FUN_004e8880 (resource read) | `0xe8da9` FUN_004e8880 (resource read) | 68 |
| 0 | `0xb4fad` FUN_004b4f80 (CPureDeviceStateManager::SetRenderState (filters via 0x004b5620, then device vtable+0xe4)) | `0xb4f99` FUN_004b4f80 (CPureDeviceStateManager::SetRenderState (filters via 0x004b5620, then device vtable+0xe4)) | 46 |
| 0 | `0xe1538` FUN_004e1220 (collision tree: recursive split body (self-recursive, 2,267 B) - top loading hot spot) | `0xe1538` FUN_004e1220 (collision tree: recursive split body (self-recursive, 2,267 B) - top loading hot spot) | 42 |
| 0 | `0xe1a18` FUN_004e1220 (collision tree: recursive split body (self-recursive, 2,267 B) - top loading hot spot) | `0xe1538` FUN_004e1220 (collision tree: recursive split body (self-recursive, 2,267 B) - top loading hot spot) | 41 |
| 0 | `0xe1538` FUN_004e1220 (collision tree: recursive split body (self-recursive, 2,267 B) - top loading hot spot) | `0xe1a18` FUN_004e1220 (collision tree: recursive split body (self-recursive, 2,267 B) - top loading hot spot) | 35 |
| 0 | `0xb4f99` FUN_004b4f80 (CPureDeviceStateManager::SetRenderState (filters via 0x004b5620, then device vtable+0xe4)) | `0x11133e` _malloc | 30 |
| 0 | `0xcf589` FUN_004cf460 (DirectShow movie playback setup (CoCreateInstance of the X MPEG filter graph, Data\mov\%s, %05d.dat)) | `0x981d8` FUN_00498140 (movie playback driver (allocates, then 0x004cf460)) | 30 |
| 0 | `0xe1a18` FUN_004e1220 (collision tree: recursive split body (self-recursive, 2,267 B) - top loading hot spot) | `0xe1a18` FUN_004e1220 (collision tree: recursive split body (self-recursive, 2,267 B) - top loading hot spot) | 28 |
| 0 | `0x11133e` _malloc | `0xbaaa` FUN_0040ba30 | 24 |

**Candidates**

Hooked calls account for 0.621 s of the 2.054 s interval (30.3 %); 1.432 s is unexplained by any hook. The sampler recorded 25,984 samples over 5.002 s of report coverage (5194 samples/s, 311 ticks/s per thread); the dominant leaf module is ntdll at 82.1 %. 1 of 1 delta blocks straddle the interval boundary and carry samples from outside it. Function `0x004e3280` (FUN_004e3280) holds 0.0 % of the engine thread's samples as leaf and 26.4 % as first main-module frame; it is a known routine (collision: OBB/OBB separating-axis test (15 axes, fabsf 0x0040e710, 1e-6 margin from 0x00565600; returns failing axis index)). Function `0x004b4f80` (FUN_004b4f80) holds 0.0 % of the engine thread's samples as leaf and 14.9 % as first main-module frame; it is a known routine (CPureDeviceStateManager::SetRenderState (filters via 0x004b5620, then device vtable+0xe4)). Function `0x004e1220` (FUN_004e1220) holds 0.0 % of the engine thread's samples as leaf and 9.7 % as first main-module frame; it is a known routine (collision tree: recursive split body (self-recursive, 2,267 B) - top loading hot spot).

## Gap 4: unlabelled (3.124 s, ends at frame 4368)

Label evidence: 78 GenerateAdjacency calls, 234,403,550 B texture input, 0 gzread calls: no rule matches.
Interval 121.082–125.374 s, length 3.124 s; hooked exclusive 2.418 s over 5 report windows (2 straddling); unexplained 0.706 s.
Samples 58,174 in 2 delta blocks (2 straddling), 10.006 s covered, 5814 samples/s, 213 ticks/s per thread, 0 dropped; sampler tick 1798 µs mean / 30792 µs max, busy 40.4 % of the covered time.

**Hooked time**

| Operation | Calls | Incl. s | Excl. s | Mean ms | Bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `inflate` | 50,662 | 0.818 | 0.818 | 0.016 | 0 |
| `FindFirstFileA` | 1,014 | 0.558 | 0.558 | 0.550 | 0 |
| `D3DXCreateTextureFromFileInMemoryEx` | 177 | 0.400 | 0.400 | 2.259 | 234,403,550 |
| `CreateFileA` | 965 | 0.193 | 0.193 | 0.200 | 0 |
| `xmlReadMemory` | 706 | 0.173 | 0.173 | 0.245 | 21,100,734 |
| `ReadFile` | 16,560 | 0.141 | 0.141 | 0.008 | 64,338,204 |
| `FindNextFileA` | 2,200 | 0.049 | 0.049 | 0.022 | 0 |
| `D3DXLoadSurfaceFromFileInMemory` | 3 | 0.038 | 0.038 | 12.692 | 1,681,191 |
| `D3DXCreateMesh` | 78 | 0.019 | 0.019 | 0.240 | 0 |
| `ID3DXMesh::GenerateAdjacency` | 78 | 0.008 | 0.008 | 0.100 | 0 |
| `D3DXCreateCubeTextureFromFileInMemoryEx` | 4 | 0.006 | 0.006 | 1.612 | 262,976 |
| `SetFilePointer` | 2,193 | 0.006 | 0.006 | 0.003 | 0 |

**Sampled attribution: per-thread module split**

| Slot | TID | Samples | x3ap | ntdll | wine | d3dx | zlib | xml | proxy | other | Start |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 0 | 216 | 2,132 | 0 | 14 | 0 | 0 | 0 | 0 | 0 | 2,118 | 0:0x112ead |
| 1 | 300 | 2,132 | 0 | 2,132 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xb220 |
| 2 | 304 | 2,132 | 0 | 2,132 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xc780 |
| 3 | 316 | 2,132 | 0 | 2,132 | 0 | 0 | 0 | 0 | 0 | 0 | 49:0x38490 |
| 4 | 320 | 2,132 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 2,132 | 4:0xd0f0 |
| 5 | 328 | 2,132 | 0 | 2,132 | 0 | 0 | 0 | 0 | 0 | 0 | 20:0xe740 |
| 6 | 352 | 2,132 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 2,132 | 38:0x2400 |
| 7 | 356 | 2,132 | 0 | 2,132 | 0 | 0 | 0 | 0 | 0 | 0 | 7:0x9900 |
| 8 | 896 | 1,512 | 0 | 1,512 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 9 | 372 | 2,132 | 0 | 2,132 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 10 | 408 | 2,132 | 0 | 2,132 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 11 | 672 | 2,132 | 0 | 2,132 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 12 | 708 | 2,132 | 0 | 2,132 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 13 | 448 | 2,132 | 0 | 2,132 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 14 | 748 | 2,132 | 0 | 2,132 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 15 | 504 | 2,132 | 0 | 2,132 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 16 | 568 | 2,132 | 0 | 2,132 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 17 | 536 | 2,132 | 0 | 2,132 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 18 | 600 | 2,132 | 0 | 2,132 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 19 | 636 | 2,132 | 0 | 2,132 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 20 | 932 | 1,512 | 0 | 1,512 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 21 | 972 | 1,512 | 0 | 1,512 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 22 | 996 | 50 | 0 | 50 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x9f90 |
| 22 | 1160 | 1,251 | 0 | 1,251 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 23 | 792 | 2,132 | 0 | 2,132 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 24 | 828 | 2,132 | 0 | 2,132 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 25 | 868 | 2,132 | 0 | 2,132 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 26 | 1012 | 1,434 | 0 | 1,434 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 27 | 1076 | 22 | 0 | 22 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x9f90 |
| 27 | 1196 | 1,251 | 0 | 1,251 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 28 | 1080 | 22 | 0 | 22 | 0 | 0 | 0 | 0 | 0 | 0 | 49:0x38490 |
| 28 | 1228 | 2 | 0 | 2 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x9f90 |
| 29 | 1088 | 15 | 0 | 15 | 0 | 0 | 0 | 0 | 0 | 0 | 20:0xe740 |
| 29 | 1232 | 2 | 0 | 2 | 0 | 0 | 0 | 0 | 0 | 0 | 49:0x38490 |
| 30 | 1092 | 1,434 | 0 | 1,434 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 31 | 1236 | 1,251 | 0 | 1,251 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |

All threads: x3ap 0.0 %, ntdll 89.0 %, wine 0.0 %, d3dx 0.0 %, zlib 0.0 %, xml 0.0 %, proxy 0.0 %, other 11.0 %.

**Top functions (leaf samples by containing function)**

Shares are of the engine thread (slot 0, 2,132 samples); the other sampled threads are idle Wine/audio/input waiters.

| Function start | Ghidra name | Label | Self (engine) | Incl. frame (engine) | Busiest slot | Hooked APIs in interval |
| --- | --- | --- | ---: | ---: | ---: | --- |
| `0x004d5a90` | FUN_004d5a90 | Win32 input / message handling (GetKeyboardLayout, MapVirtualKeyExA; helper 0x004d6850) | 0.0 % (0) | 17.3 % (368) | — | — |
| `0x00511f77` | FUN_00511f77 | CRT fread body (from _fread_s, uses __VEC_memcpy) | 0.0 % (0) | 13.3 % (283) | — | — |
| `0x004e8e10` | FUN_004e8e10 | resource load = open + read + close | 0.0 % (0) | 12.2 % (260) | — | — |
| `0x005112c4` | _malloc | — | 0.0 % (0) | 10.9 % (232) | — | — |
| `0x00517bcb` | __isleadbyte_l | — | 0.0 % (0) | 6.5 % (139) | — | — |
| `0x004cf460` | FUN_004cf460 | DirectShow movie playback setup (CoCreateInstance of the X MPEG filter graph, Data\mov\%s, %05d.dat) | 0.0 % (0) | 4.8 % (103) | — | — |
| `0x0050e1b0` | _free | — | 0.0 % (0) | 3.8 % (80) | — | — |
| `0x004dd2c0` | FUN_004dd2c0 | jpg/tga texture load body: resource load 0x004e8e10 + D3DXGetImageInfo + 2D helper + LoadSurface (callee of 0x004f3510) | 0.0 % (0) | 2.7 % (58) | — | `D3DXCreateTextureFromFileInMemoryEx` 177 calls / 0.400 s; `D3DXLoadSurfaceFromFileInMemory` 3 calls / 0.038 s |
| `0x004e8880` | FUN_004e8880 | resource read | 0.0 % (0) | 2.3 % (50) | — | `ReadFile` 16,560 calls / 0.141 s |
| `0x0042fe10` | FUN_0042fe10 | — | 0.0 % (0) | 1.8 % (39) | — | — |
| `0x004dc540` | FUN_004dc540 | texture loader (pck dds; image info, cube, 2D helpers) | 0.0 % (0) | 1.7 % (37) | — | `D3DXCreateTextureFromFileInMemoryEx` 177 calls / 0.400 s; `D3DXCreateCubeTextureFromFileInMemoryEx` 4 calls / 0.006 s |
| `0x00528060` | FUN_00528060 | — | 0.0 % (0) | 1.2 % (25) | — | — |
| `0x004b4600` | FUN_004b4600 | — | 0.0 % (0) | 1.0 % (22) | — | — |
| `0x004efcc0` | FUN_004efcc0 | handle map insert | 0.0 % (0) | 0.9 % (19) | — | — |
| `0x004d1d40` | FUN_004d1d40 | — | 0.0 % (0) | 0.8 % (17) | — | — |
| `0x00517d8f` | FUN_00517d8f | CRT file-lock release tail (LeaveCriticalSection, from 0x00524cfa) | 0.0 % (0) | 0.7 % (15) | — | — |
| `0x0051f464` | FUN_0051f464 | CRT _read text-mode body (ReadFile, MultiByteToWideChar, __malloc_crt) | 0.0 % (0) | 0.7 % (14) | — | — |
| `0x004bcb60` | FUN_004bcb60 | mesh build: CloneMesh into final options (D3DXMESH_32BIT above 64k vertices), two attempts, OOM callback, then install via 0x004bc9c0 | 0.0 % (0) | 0.4 % (9) | — | — |
| `0x004bb240` | FUN_004bb240 | — | 0.0 % (0) | 0.4 % (8) | — | — |
| `0x004f3510` | FUN_004f3510 | texture-file wrapper (.jpg, .tga) | 0.0 % (0) | 0.4 % (8) | — | `D3DXLoadSurfaceFromFileInMemory` 3 calls / 0.038 s |
| `0x00519434` | FUN_00519434 | — | 0.0 % (0) | 0.3 % (7) | — | — |
| `0x004a7630` | FUN_004a7630 | — | 0.0 % (0) | 0.3 % (7) | — | — |
| `0x004a7f60` | FUN_004a7f60 | — | 0.0 % (0) | 0.2 % (5) | — | — |
| `0x004d1c20` | FUN_004d1c20 | — | 0.0 % (0) | 0.2 % (5) | — | — |
| `0x004e6a40` | FUN_004e6a40 | malloc + memset allocation wrapper (via 0x004b8b60) | 0.0 % (0) | 0.2 % (5) | — | — |
| (no main-module frame) | | | | 96.8 % (56,290) | | |

**Top caller pairs**

| Slot | Frame | Caller | Samples |
| ---: | --- | --- | ---: |
| 0 | `0xe8e8d` FUN_004e8e10 (resource load = open + read + close) | `0xe8eb2` FUN_004e8e10 (resource load = open + read + close) | 260 |
| 0 | `0x11204f` FUN_00511f77 (CRT fread body (from _fread_s, uses __VEC_memcpy)) | `0xe8dc3` FUN_004e8880 (resource read) | 259 |
| 0 | `0x117bdc` __isleadbyte_l | `0x119da0` FUN_00519434 | 139 |
| 0 | `0xd6509` FUN_004d5a90 (Win32 input / message handling (GetKeyboardLayout, MapVirtualKeyExA; helper 0x004d6850)) | `0x11133e` _malloc | 68 |
| 0 | `0x10e21e` _free | `0x10e23d` _free | 66 |
| 0 | `0xdd6a2` FUN_004dd2c0 (jpg/tga texture load body: resource load 0x004e8e10 + D3DXGetImageInfo + 2D helper + LoadSurface (callee of 0x004f3510)) | `0xdd6a2` FUN_004dd2c0 (jpg/tga texture load body: resource load 0x004e8e10 + D3DXGetImageInfo + 2D helper + LoadSurface (callee of 0x004f3510)) | 58 |
| 0 | `0x11133e` _malloc | `0x11133e` _malloc | 56 |
| 0 | `0x11133e` _malloc | `0xe6a75` FUN_004e6a40 (malloc + memset allocation wrapper (via 0x004b8b60)) | 51 |
| 0 | `0xcf589` FUN_004cf460 (DirectShow movie playback setup (CoCreateInstance of the X MPEG filter graph, Data\mov\%s, %05d.dat)) | `0x981d8` FUN_00498140 (movie playback driver (allocates, then 0x004cf460)) | 49 |
| 0 | `0xe8dc3` FUN_004e8880 (resource read) | `0x11133e` _malloc | 46 |
| 0 | `0xdc846` FUN_004dc540 (texture loader (pck dds; image info, cube, 2D helpers)) | `0xdc846` FUN_004dc540 (texture loader (pck dds; image info, cube, 2D helpers)) | 37 |
| 0 | `0xd6509` FUN_004d5a90 (Win32 input / message handling (GetKeyboardLayout, MapVirtualKeyExA; helper 0x004d6850)) | `0x10e21e` _free | 28 |

**Candidates**

Hooked calls account for 2.418 s of the 3.124 s interval (77.4 %); 0.706 s is unexplained by any hook. The sampler recorded 58,174 samples over 10.006 s of report coverage (5814 samples/s, 213 ticks/s per thread); the dominant leaf module is ntdll at 89.0 %. 2 of 2 delta blocks straddle the interval boundary and carry samples from outside it. Function `0x004d5a90` (FUN_004d5a90) holds 0.0 % of the engine thread's samples as leaf and 17.3 % as first main-module frame; it is a known routine (Win32 input / message handling (GetKeyboardLayout, MapVirtualKeyExA; helper 0x004d6850)). Function `0x00511f77` (FUN_00511f77) holds 0.0 % of the engine thread's samples as leaf and 13.3 % as first main-module frame; it is a known routine (CRT fread body (from _fread_s, uses __VEC_memcpy)). Function `0x004e8e10` (FUN_004e8e10) holds 0.0 % of the engine thread's samples as leaf and 12.2 % as first main-module frame; it is a known routine (resource load = open + read + close).

### Gap 4 stall 1: report stall 3.074 s

Interval 125.373–128.447 s, length 3.074 s; hooked exclusive 1.791 s over 1 report windows (0 straddling); unexplained 1.283 s.
Samples 37,939 in 1 delta blocks (1 straddling), 5.004 s covered, 7582 samples/s, 253 ticks/s per thread, 0 dropped; sampler tick 1567 µs mean / 27851 µs max, busy 40.3 % of the covered time.

**Hooked time**

| Operation | Calls | Incl. s | Excl. s | Mean ms | Bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `FindFirstFileA` | 855 | 0.549 | 0.549 | 0.642 | 0 |
| `inflate` | 30,618 | 0.495 | 0.495 | 0.016 | 0 |
| `D3DXCreateTextureFromFileInMemoryEx` | 79 | 0.309 | 0.309 | 3.906 | 190,086,684 |
| `CreateFileA` | 838 | 0.170 | 0.170 | 0.203 | 0 |
| `xmlReadMemory` | 700 | 0.134 | 0.134 | 0.191 | 13,712,061 |
| `ReadFile` | 11,141 | 0.079 | 0.079 | 0.007 | 36,609,635 |
| `FindNextFileA` | 2,183 | 0.048 | 0.048 | 0.022 | 0 |
| `SetFilePointer` | 1,817 | 0.006 | 0.006 | 0.003 | 0 |
| `FindClose` | 700 | 0.001 | 0.001 | 0.001 | 0 |

**Sampled attribution: per-thread module split**

| Slot | TID | Samples | x3ap | ntdll | wine | d3dx | zlib | xml | proxy | other | Start |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 0 | 216 | 1,266 | 0 | 3 | 0 | 0 | 0 | 0 | 0 | 1,263 | 0:0x112ead |
| 1 | 300 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xb220 |
| 2 | 304 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xc780 |
| 3 | 316 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 49:0x38490 |
| 4 | 320 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 1,266 | 4:0xd0f0 |
| 5 | 328 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 20:0xe740 |
| 6 | 352 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 1,266 | 38:0x2400 |
| 7 | 356 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 7:0x9900 |
| 8 | 896 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 9 | 372 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 10 | 408 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 11 | 672 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 12 | 708 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 13 | 448 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 14 | 748 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 15 | 504 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 16 | 568 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 17 | 536 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 18 | 600 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 19 | 636 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 20 | 932 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 21 | 972 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 22 | 1160 | 1,251 | 0 | 1,251 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 23 | 792 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 24 | 828 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 25 | 868 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 26 | 1012 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 27 | 1196 | 1,251 | 0 | 1,251 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 28 | 1228 | 2 | 0 | 2 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x9f90 |
| 29 | 1232 | 2 | 0 | 2 | 0 | 0 | 0 | 0 | 0 | 0 | 49:0x38490 |
| 30 | 1092 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 31 | 1236 | 1,251 | 0 | 1,251 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |

All threads: x3ap 0.0 %, ntdll 90.0 %, wine 0.0 %, d3dx 0.0 %, zlib 0.0 %, xml 0.0 %, proxy 0.0 %, other 10.0 %.

**Top functions (leaf samples by containing function)**

Shares are of the engine thread (slot 0, 1,266 samples); the other sampled threads are idle Wine/audio/input waiters.

| Function start | Ghidra name | Label | Self (engine) | Incl. frame (engine) | Busiest slot | Hooked APIs in interval |
| --- | --- | --- | ---: | ---: | ---: | --- |
| `0x00511f77` | FUN_00511f77 | CRT fread body (from _fread_s, uses __VEC_memcpy) | 0.0 % (0) | 21.6 % (274) | — | — |
| `0x005112c4` | _malloc | — | 0.0 % (0) | 17.0 % (215) | — | — |
| `0x004e8e10` | FUN_004e8e10 | resource load = open + read + close | 0.0 % (0) | 12.0 % (152) | — | — |
| `0x00517bcb` | __isleadbyte_l | — | 0.0 % (0) | 11.0 % (139) | — | — |
| `0x004dd2c0` | FUN_004dd2c0 | jpg/tga texture load body: resource load 0x004e8e10 + D3DXGetImageInfo + 2D helper + LoadSurface (callee of 0x004f3510) | 0.0 % (0) | 4.6 % (58) | — | `D3DXCreateTextureFromFileInMemoryEx` 79 calls / 0.309 s |
| `0x004e8880` | FUN_004e8880 | resource read | 0.0 % (0) | 3.9 % (50) | — | `ReadFile` 11,141 calls / 0.079 s |
| `0x004efcc0` | FUN_004efcc0 | handle map insert | 0.0 % (0) | 1.5 % (19) | — | — |
| `0x004dc540` | FUN_004dc540 | texture loader (pck dds; image info, cube, 2D helpers) | 0.0 % (0) | 1.4 % (18) | — | `D3DXCreateTextureFromFileInMemoryEx` 79 calls / 0.309 s |
| `0x00517d8f` | FUN_00517d8f | CRT file-lock release tail (LeaveCriticalSection, from 0x00524cfa) | 0.0 % (0) | 1.2 % (15) | — | — |
| `0x0051f464` | FUN_0051f464 | CRT _read text-mode body (ReadFile, MultiByteToWideChar, __malloc_crt) | 0.0 % (0) | 1.1 % (14) | — | — |
| `0x00528060` | FUN_00528060 | — | 0.0 % (0) | 1.0 % (13) | — | — |
| `0x004b4600` | FUN_004b4600 | — | 0.0 % (0) | 0.8 % (10) | — | — |
| `0x004bb240` | FUN_004bb240 | — | 0.0 % (0) | 0.6 % (8) | — | — |
| `0x00519434` | FUN_00519434 | — | 0.0 % (0) | 0.6 % (7) | — | — |
| `0x004a7630` | FUN_004a7630 | — | 0.0 % (0) | 0.6 % (7) | — | — |
| `0x004a7f60` | FUN_004a7f60 | — | 0.0 % (0) | 0.4 % (5) | — | — |
| `0x004e6a40` | FUN_004e6a40 | malloc + memset allocation wrapper (via 0x004b8b60) | 0.0 % (0) | 0.4 % (5) | — | — |
| `0x0050e1b0` | _free | — | 0.0 % (0) | 0.3 % (4) | — | — |
| (no main-module frame) | | | | 97.2 % (36,895) | | |

**Top caller pairs**

| Slot | Frame | Caller | Samples |
| ---: | --- | --- | ---: |
| 0 | `0x11204f` FUN_00511f77 (CRT fread body (from _fread_s, uses __VEC_memcpy)) | `0xe8dc3` FUN_004e8880 (resource read) | 254 |
| 0 | `0xe8e8d` FUN_004e8e10 (resource load = open + read + close) | `0xe8eb2` FUN_004e8e10 (resource load = open + read + close) | 152 |
| 0 | `0x117bdc` __isleadbyte_l | `0x119da0` FUN_00519434 | 139 |
| 0 | `0xdd6a2` FUN_004dd2c0 (jpg/tga texture load body: resource load 0x004e8e10 + D3DXGetImageInfo + 2D helper + LoadSurface (callee of 0x004f3510)) | `0xdd6a2` FUN_004dd2c0 (jpg/tga texture load body: resource load 0x004e8e10 + D3DXGetImageInfo + 2D helper + LoadSurface (callee of 0x004f3510)) | 58 |
| 0 | `0x11133e` _malloc | `0x11133e` _malloc | 56 |
| 0 | `0xe8dc3` FUN_004e8880 (resource read) | `0x11133e` _malloc | 46 |
| 0 | `0x11133e` _malloc | `0xe6a75` FUN_004e6a40 (malloc + memset allocation wrapper (via 0x004b8b60)) | 43 |
| 0 | `0xdc846` FUN_004dc540 (texture loader (pck dds; image info, cube, 2D helpers)) | `0xdc846` FUN_004dc540 (texture loader (pck dds; image info, cube, 2D helpers)) | 18 |
| 0 | `0xefd0e` FUN_004efcc0 (handle map insert) | `0xa86ff` FUN_004a8670 | 18 |
| 0 | `0x11133e` _malloc | `0xefd0e` FUN_004efcc0 (handle map insert) | 18 |
| 0 | `0x11204f` FUN_00511f77 (CRT fread body (from _fread_s, uses __VEC_memcpy)) | `0x1121d1` _fread_s | 17 |
| 0 | `0x11133e` _malloc | `0x10e21e` _free | 13 |

**Candidates**

Hooked calls account for 1.791 s of the 3.074 s interval (58.3 %); 1.283 s is unexplained by any hook. The sampler recorded 37,939 samples over 5.004 s of report coverage (7582 samples/s, 253 ticks/s per thread); the dominant leaf module is ntdll at 90.0 %. 1 of 1 delta blocks straddle the interval boundary and carry samples from outside it. Function `0x00511f77` (FUN_00511f77) holds 0.0 % of the engine thread's samples as leaf and 21.6 % as first main-module frame; it is a known routine (CRT fread body (from _fread_s, uses __VEC_memcpy)). Function `0x005112c4` (_malloc) holds 0.0 % of the engine thread's samples as leaf and 17.0 % as first main-module frame; it is not a known routine. Function `0x004e8e10` (FUN_004e8e10) holds 0.0 % of the engine thread's samples as leaf and 12.0 % as first main-module frame; it is a known routine (resource load = open + read + close).

## Gap 5: menu load (24.697 s, ends at frame 4381)

Label evidence: 3347 GenerateAdjacency calls and 651,670,507 B of 2D texture-helper input match the main-menu work vector.
Interval 123.920–148.920 s, length 24.697 s; hooked exclusive 12.854 s over 16 report windows (2 straddling); unexplained 11.843 s.
Samples 212,584 in 6 delta blocks (2 straddling), 30.013 s covered, 7083 samples/s, 243 ticks/s per thread, 0 dropped; sampler tick 1599 µs mean / 32110 µs max, busy 40.0 % of the covered time.

**Hooked time**

| Operation | Calls | Incl. s | Excl. s | Mean ms | Bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `inflate` | 360,409 | 5.953 | 5.953 | 0.017 | 0 |
| `ID3DXMesh::GenerateAdjacency` | 3,347 | 1.610 | 1.610 | 0.481 | 0 |
| `D3DXCreateTextureFromFileInMemoryEx` | 668 | 0.934 | 0.934 | 1.398 | 651,670,507 |
| `D3DXCreateMesh` | 3,347 | 0.896 | 0.896 | 0.268 | 0 |
| `ID3DXMesh::OptimizeInplace` | 3,347 | 0.843 | 0.843 | 0.252 | 0 |
| `ReadFile` | 96,654 | 0.772 | 0.772 | 0.008 | 386,337,217 |
| `D3DXCleanMesh` | 3,348 | 0.637 | 0.637 | 0.190 | 0 |
| `FindFirstFileA` | 1,965 | 0.582 | 0.582 | 0.296 | 0 |
| `CreateFileA` | 1,792 | 0.276 | 0.276 | 0.154 | 0 |
| `xmlReadMemory` | 798 | 0.213 | 0.213 | 0.267 | 25,059,221 |
| `FindNextFileA` | 2,199 | 0.049 | 0.049 | 0.022 | 0 |
| `D3DXLoadSurfaceFromFileInMemory` | 3 | 0.038 | 0.038 | 12.692 | 1,681,191 |

**Sampled attribution: per-thread module split**

| Slot | TID | Samples | x3ap | ntdll | wine | d3dx | zlib | xml | proxy | other | Start |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 0 | 216 | 7,279 | 0 | 163 | 0 | 0 | 0 | 0 | 0 | 7,116 | 0:0x112ead |
| 1 | 300 | 7,279 | 0 | 7,279 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xb220 |
| 2 | 304 | 7,279 | 0 | 7,279 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xc780 |
| 3 | 316 | 7,279 | 0 | 7,279 | 0 | 0 | 0 | 0 | 0 | 0 | 49:0x38490 |
| 4 | 320 | 7,279 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 7,279 | 4:0xd0f0 |
| 5 | 328 | 7,279 | 0 | 7,279 | 0 | 0 | 0 | 0 | 0 | 0 | 20:0xe740 |
| 6 | 352 | 7,279 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 7,279 | 38:0x2400 |
| 7 | 356 | 7,279 | 0 | 7,279 | 0 | 0 | 0 | 0 | 0 | 0 | 7:0x9900 |
| 8 | 896 | 6,659 | 0 | 6,659 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 9 | 372 | 7,279 | 0 | 7,279 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 10 | 408 | 7,279 | 0 | 7,279 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 11 | 672 | 7,279 | 0 | 7,279 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 12 | 708 | 7,279 | 0 | 7,279 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 13 | 448 | 7,279 | 0 | 7,279 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 14 | 748 | 7,279 | 0 | 7,279 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 15 | 504 | 7,279 | 0 | 7,279 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 16 | 568 | 7,279 | 0 | 7,279 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 17 | 536 | 7,279 | 0 | 7,279 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 18 | 600 | 7,279 | 0 | 7,279 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 19 | 636 | 7,279 | 0 | 7,279 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 20 | 932 | 6,659 | 0 | 6,659 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 21 | 972 | 6,659 | 0 | 6,659 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 22 | 996 | 50 | 0 | 50 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x9f90 |
| 22 | 1160 | 6,398 | 0 | 6,398 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 23 | 792 | 7,279 | 0 | 7,279 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 24 | 828 | 7,279 | 0 | 7,279 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 25 | 868 | 7,279 | 0 | 7,279 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 26 | 1012 | 6,581 | 0 | 6,581 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 27 | 1076 | 22 | 0 | 22 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x9f90 |
| 27 | 1196 | 6,398 | 0 | 6,398 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 28 | 1080 | 22 | 0 | 22 | 0 | 0 | 0 | 0 | 0 | 0 | 49:0x38490 |
| 28 | 1228 | 2 | 0 | 2 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x9f90 |
| 29 | 1088 | 15 | 0 | 15 | 0 | 0 | 0 | 0 | 0 | 0 | 20:0xe740 |
| 29 | 1232 | 2 | 0 | 2 | 0 | 0 | 0 | 0 | 0 | 0 | 49:0x38490 |
| 30 | 1092 | 6,581 | 0 | 6,581 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 31 | 1236 | 6,398 | 0 | 6,398 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |

All threads: x3ap 0.0 %, ntdll 89.8 %, wine 0.0 %, d3dx 0.0 %, zlib 0.0 %, xml 0.0 %, proxy 0.0 %, other 10.2 %.

**Top functions (leaf samples by containing function)**

Shares are of the engine thread (slot 0, 7,279 samples); the other sampled threads are idle Wine/audio/input waiters.

| Function start | Ghidra name | Label | Self (engine) | Incl. frame (engine) | Busiest slot | Hooked APIs in interval |
| --- | --- | --- | ---: | ---: | ---: | --- |
| `0x004e8e10` | FUN_004e8e10 | resource load = open + read + close | 0.0 % (0) | 26.7 % (1,944) | — | — |
| `0x005112c4` | _malloc | — | 0.0 % (0) | 13.8 % (1,007) | — | — |
| `0x0052403b` | __VEC_memzero | — | 0.0 % (0) | 6.2 % (451) | — | — |
| `0x004bcb60` | FUN_004bcb60 | mesh build: CloneMesh into final options (D3DXMESH_32BIT above 64k vertices), two attempts, OOM callback, then install via 0x004bc9c0 | 0.0 % (0) | 5.4 % (394) | — | — |
| `0x004d5a90` | FUN_004d5a90 | Win32 input / message handling (GetKeyboardLayout, MapVirtualKeyExA; helper 0x004d6850) | 0.0 % (0) | 5.2 % (382) | — | — |
| `0x00511f77` | FUN_00511f77 | CRT fread body (from _fread_s, uses __VEC_memcpy) | 0.0 % (0) | 4.4 % (321) | — | — |
| `0x0050e1b0` | _free | — | 0.0 % (0) | 3.4 % (249) | — | — |
| `0x004bb470` | FUN_004bb470 | mesh build: D3DXCreateMesh + vertex/index fill | 0.0 % (0) | 2.6 % (190) | — | `D3DXCreateMesh` 3,347 calls / 0.896 s |
| `0x004e1220` | FUN_004e1220 | collision tree: recursive split body (self-recursive, 2,267 B) - top loading hot spot | 0.0 % (0) | 2.5 % (185) | — | — |
| `0x004bc1c0` | FUN_004bc1c0 | mesh build: vertex positions (int16 x 1/16384) and normals (D3DXVec3Normalize x3) | 0.0 % (0) | 2.3 % (171) | — | — |
| `0x00517bcb` | __isleadbyte_l | — | 0.0 % (0) | 2.0 % (142) | — | — |
| `0x004bc680` | FUN_004bc680 | mesh preparation: adjacency / clean / optimise; calls ID3DXMesh::GenerateAdjacency(1e-6f from 0x00565600) at 0x004bc76a | 0.0 % (0) | 1.9 % (140) | — | `ID3DXMesh::GenerateAdjacency` 3,347 calls / 1.610 s; `D3DXCleanMesh` 3,348 calls / 0.637 s; `ID3DXMesh::OptimizeInplace` 3,347 calls / 0.843 s |
| `0x00524aa7` | ___lock_fhandle | — | 0.0 % (0) | 1.5 % (109) | — | — |
| `0x004cf460` | FUN_004cf460 | DirectShow movie playback setup (CoCreateInstance of the X MPEG filter graph, Data\mov\%s, %05d.dat) | 0.0 % (0) | 1.4 % (103) | — | — |
| `0x004b4f80` | FUN_004b4f80 | CPureDeviceStateManager::SetRenderState (filters via 0x004b5620, then device vtable+0xe4) | 0.0 % (0) | 1.4 % (99) | — | — |
| `0x00527fc2` | FID_conflict:__sopen_helper | — | 0.0 % (0) | 1.3 % (94) | — | — |
| `0x004dd2c0` | FUN_004dd2c0 | jpg/tga texture load body: resource load 0x004e8e10 + D3DXGetImageInfo + 2D helper + LoadSurface (callee of 0x004f3510) | 0.0 % (0) | 0.9 % (68) | — | `D3DXCreateTextureFromFileInMemoryEx` 668 calls / 0.934 s; `D3DXLoadSurfaceFromFileInMemory` 3 calls / 0.038 s |
| `0x0042fe10` | FUN_0042fe10 | — | 0.0 % (0) | 0.7 % (53) | — | — |
| `0x004e8880` | FUN_004e8880 | resource read | 0.0 % (0) | 0.7 % (50) | — | `ReadFile` 96,654 calls / 0.772 s |
| `0x004efcc0` | FUN_004efcc0 | handle map insert | 0.0 % (0) | 0.6 % (46) | — | — |
| `0x004bb240` | FUN_004bb240 | — | 0.0 % (0) | 0.6 % (43) | — | — |
| `0x00528060` | FUN_00528060 | — | 0.0 % (0) | 0.5 % (37) | — | — |
| `0x004dc540` | FUN_004dc540 | texture loader (pck dds; image info, cube, 2D helpers) | 0.0 % (0) | 0.5 % (37) | — | `D3DXCreateTextureFromFileInMemoryEx` 668 calls / 0.934 s; `D3DXCreateCubeTextureFromFileInMemoryEx` 13 calls / 0.024 s |
| `0x00519434` | FUN_00519434 | — | 0.0 % (0) | 0.3 % (23) | — | — |
| `0x004b4600` | FUN_004b4600 | — | 0.0 % (0) | 0.3 % (22) | — | — |
| `0x004a7630` | FUN_004a7630 | — | 0.0 % (0) | 0.3 % (19) | — | — |
| `0x004d1d40` | FUN_004d1d40 | — | 0.0 % (0) | 0.2 % (17) | — | — |
| `0x00517d8f` | FUN_00517d8f | CRT file-lock release tail (LeaveCriticalSection, from 0x00524cfa) | 0.0 % (0) | 0.2 % (15) | — | — |
| `0x0051f464` | FUN_0051f464 | CRT _read text-mode body (ReadFile, MultiByteToWideChar, __malloc_crt) | 0.0 % (0) | 0.2 % (14) | — | — |
| `0x004bc9c0` | FUN_004bc9c0 | installs the cleaned mesh | 0.0 % (0) | 0.2 % (11) | — | — |
| `0x004eb930` | FUN_004eb930 | — | 0.0 % (0) | 0.2 % (11) | — | — |
| `0x004bc080` | FUN_004bc080 | — | 0.0 % (0) | 0.1 % (9) | — | — |
| `0x004f3510` | FUN_004f3510 | texture-file wrapper (.jpg, .tga) | 0.0 % (0) | 0.1 % (8) | — | `D3DXLoadSurfaceFromFileInMemory` 3 calls / 0.038 s |
| `0x00409a90` | FUN_00409a90 | — | 0.0 % (0) | 0.1 % (7) | — | — |
| `0x004bbb10` | FUN_004bbb10 | mesh build: vertex writer (int16 x 1/16384 from 0x005655d4) | 0.0 % (0) | 0.1 % (7) | — | — |
| `0x004a7f60` | FUN_004a7f60 | — | 0.0 % (0) | 0.1 % (5) | — | — |
| `0x004d1c20` | FUN_004d1c20 | — | 0.0 % (0) | 0.1 % (5) | — | — |
| `0x004e6a40` | FUN_004e6a40 | malloc + memset allocation wrapper (via 0x004b8b60) | 0.0 % (0) | 0.1 % (5) | — | — |
| `0x0047eb90` | FUN_0047eb90 | collision-tree build driver (body triangles int16x1/16384 -> 0x004e0ce0 per triangle, then build 0x004e0c80; sets obj flag 0x1000000) | 0.0 % (0) | 0.1 % (4) | — | — |
| `0x00408a80` | FUN_00408a80 | — | 0.0 % (0) | 0.1 % (4) | — | — |
| (no main-module frame) | | | | 96.8 % (205,749) | | |

**Top caller pairs**

| Slot | Frame | Caller | Samples |
| ---: | --- | --- | ---: |
| 0 | `0xe8e8d` FUN_004e8e10 (resource load = open + read + close) | `0xe8eb2` FUN_004e8e10 (resource load = open + read + close) | 1,944 |
| 0 | `0xbcc30` FUN_004bcb60 (mesh build: CloneMesh into final options (D3DXMESH_32BIT above 64k vertices), two attempts, OOM callback, then install via 0x004bc9c0) | `0xbb9ac` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 393 |
| 0 | `0x11133e` _malloc | `0x11133e` _malloc | 334 |
| 0 | `0x11133e` _malloc | `0x10e21e` _free | 323 |
| 0 | `0x11204f` FUN_00511f77 (CRT fread body (from _fread_s, uses __VEC_memcpy)) | `0xe8dc3` FUN_004e8880 (resource read) | 297 |
| 0 | `0x10e21e` _free | `0x10e23d` _free | 220 |
| 0 | `0x124071` __VEC_memzero | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise; calls ID3DXMesh::GenerateAdjacency(1e-6f from 0x00565600) at 0x004bc76a) | 210 |
| 0 | `0x1240bd` __VEC_memzero | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise; calls ID3DXMesh::GenerateAdjacency(1e-6f from 0x00565600) at 0x004bc76a) | 208 |
| 0 | `0xbb72f` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | `0xbb72f` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 193 |
| 0 | `0x117bdc` __isleadbyte_l | `0x119da0` FUN_00519434 | 139 |
| 0 | `0xbc49b` FUN_004bc1c0 (mesh build: vertex positions (int16 x 1/16384) and normals (D3DXVec3Normalize x3)) | `0xbb94b` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 121 |
| 0 | `0x124b38` ___lock_fhandle | `0xdc846` FUN_004dc540 (texture loader (pck dds; image info, cube, 2D helpers)) | 103 |

**Candidates**

Hooked calls account for 12.854 s of the 24.697 s interval (52.0 %); 11.843 s is unexplained by any hook. The sampler recorded 212,584 samples over 30.013 s of report coverage (7083 samples/s, 243 ticks/s per thread); the dominant leaf module is ntdll at 89.8 %. 2 of 6 delta blocks straddle the interval boundary and carry samples from outside it. Function `0x004e8e10` (FUN_004e8e10) holds 0.0 % of the engine thread's samples as leaf and 26.7 % as first main-module frame; it is a known routine (resource load = open + read + close). Function `0x005112c4` (_malloc) holds 0.0 % of the engine thread's samples as leaf and 13.8 % as first main-module frame; it is not a known routine. Function `0x0052403b` (__VEC_memzero) holds 0.0 % of the engine thread's samples as leaf and 6.2 % as first main-module frame; it is not a known routine.

### Gap 5 stall 1: report stall 3.074 s

Interval 125.373–128.447 s, length 3.074 s; hooked exclusive 1.791 s over 1 report windows (0 straddling); unexplained 1.283 s.
Samples 37,939 in 1 delta blocks (1 straddling), 5.004 s covered, 7582 samples/s, 253 ticks/s per thread, 0 dropped; sampler tick 1567 µs mean / 27851 µs max, busy 40.3 % of the covered time.

**Hooked time**

| Operation | Calls | Incl. s | Excl. s | Mean ms | Bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `FindFirstFileA` | 855 | 0.549 | 0.549 | 0.642 | 0 |
| `inflate` | 30,618 | 0.495 | 0.495 | 0.016 | 0 |
| `D3DXCreateTextureFromFileInMemoryEx` | 79 | 0.309 | 0.309 | 3.906 | 190,086,684 |
| `CreateFileA` | 838 | 0.170 | 0.170 | 0.203 | 0 |
| `xmlReadMemory` | 700 | 0.134 | 0.134 | 0.191 | 13,712,061 |
| `ReadFile` | 11,141 | 0.079 | 0.079 | 0.007 | 36,609,635 |
| `FindNextFileA` | 2,183 | 0.048 | 0.048 | 0.022 | 0 |
| `SetFilePointer` | 1,817 | 0.006 | 0.006 | 0.003 | 0 |
| `FindClose` | 700 | 0.001 | 0.001 | 0.001 | 0 |

**Sampled attribution: per-thread module split**

| Slot | TID | Samples | x3ap | ntdll | wine | d3dx | zlib | xml | proxy | other | Start |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 0 | 216 | 1,266 | 0 | 3 | 0 | 0 | 0 | 0 | 0 | 1,263 | 0:0x112ead |
| 1 | 300 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xb220 |
| 2 | 304 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xc780 |
| 3 | 316 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 49:0x38490 |
| 4 | 320 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 1,266 | 4:0xd0f0 |
| 5 | 328 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 20:0xe740 |
| 6 | 352 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 1,266 | 38:0x2400 |
| 7 | 356 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 7:0x9900 |
| 8 | 896 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 9 | 372 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 10 | 408 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 11 | 672 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 12 | 708 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 13 | 448 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 14 | 748 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 15 | 504 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 16 | 568 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 17 | 536 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 18 | 600 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 19 | 636 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 20 | 932 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 21 | 972 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 22 | 1160 | 1,251 | 0 | 1,251 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 23 | 792 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 24 | 828 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 25 | 868 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 26 | 1012 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 27 | 1196 | 1,251 | 0 | 1,251 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 28 | 1228 | 2 | 0 | 2 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x9f90 |
| 29 | 1232 | 2 | 0 | 2 | 0 | 0 | 0 | 0 | 0 | 0 | 49:0x38490 |
| 30 | 1092 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 31 | 1236 | 1,251 | 0 | 1,251 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |

All threads: x3ap 0.0 %, ntdll 90.0 %, wine 0.0 %, d3dx 0.0 %, zlib 0.0 %, xml 0.0 %, proxy 0.0 %, other 10.0 %.

**Top functions (leaf samples by containing function)**

Shares are of the engine thread (slot 0, 1,266 samples); the other sampled threads are idle Wine/audio/input waiters.

| Function start | Ghidra name | Label | Self (engine) | Incl. frame (engine) | Busiest slot | Hooked APIs in interval |
| --- | --- | --- | ---: | ---: | ---: | --- |
| `0x00511f77` | FUN_00511f77 | CRT fread body (from _fread_s, uses __VEC_memcpy) | 0.0 % (0) | 21.6 % (274) | — | — |
| `0x005112c4` | _malloc | — | 0.0 % (0) | 17.0 % (215) | — | — |
| `0x004e8e10` | FUN_004e8e10 | resource load = open + read + close | 0.0 % (0) | 12.0 % (152) | — | — |
| `0x00517bcb` | __isleadbyte_l | — | 0.0 % (0) | 11.0 % (139) | — | — |
| `0x004dd2c0` | FUN_004dd2c0 | jpg/tga texture load body: resource load 0x004e8e10 + D3DXGetImageInfo + 2D helper + LoadSurface (callee of 0x004f3510) | 0.0 % (0) | 4.6 % (58) | — | `D3DXCreateTextureFromFileInMemoryEx` 79 calls / 0.309 s |
| `0x004e8880` | FUN_004e8880 | resource read | 0.0 % (0) | 3.9 % (50) | — | `ReadFile` 11,141 calls / 0.079 s |
| `0x004efcc0` | FUN_004efcc0 | handle map insert | 0.0 % (0) | 1.5 % (19) | — | — |
| `0x004dc540` | FUN_004dc540 | texture loader (pck dds; image info, cube, 2D helpers) | 0.0 % (0) | 1.4 % (18) | — | `D3DXCreateTextureFromFileInMemoryEx` 79 calls / 0.309 s |
| `0x00517d8f` | FUN_00517d8f | CRT file-lock release tail (LeaveCriticalSection, from 0x00524cfa) | 0.0 % (0) | 1.2 % (15) | — | — |
| `0x0051f464` | FUN_0051f464 | CRT _read text-mode body (ReadFile, MultiByteToWideChar, __malloc_crt) | 0.0 % (0) | 1.1 % (14) | — | — |
| `0x00528060` | FUN_00528060 | — | 0.0 % (0) | 1.0 % (13) | — | — |
| `0x004b4600` | FUN_004b4600 | — | 0.0 % (0) | 0.8 % (10) | — | — |
| `0x004bb240` | FUN_004bb240 | — | 0.0 % (0) | 0.6 % (8) | — | — |
| `0x00519434` | FUN_00519434 | — | 0.0 % (0) | 0.6 % (7) | — | — |
| `0x004a7630` | FUN_004a7630 | — | 0.0 % (0) | 0.6 % (7) | — | — |
| `0x004a7f60` | FUN_004a7f60 | — | 0.0 % (0) | 0.4 % (5) | — | — |
| `0x004e6a40` | FUN_004e6a40 | malloc + memset allocation wrapper (via 0x004b8b60) | 0.0 % (0) | 0.4 % (5) | — | — |
| `0x0050e1b0` | _free | — | 0.0 % (0) | 0.3 % (4) | — | — |
| (no main-module frame) | | | | 97.2 % (36,895) | | |

**Top caller pairs**

| Slot | Frame | Caller | Samples |
| ---: | --- | --- | ---: |
| 0 | `0x11204f` FUN_00511f77 (CRT fread body (from _fread_s, uses __VEC_memcpy)) | `0xe8dc3` FUN_004e8880 (resource read) | 254 |
| 0 | `0xe8e8d` FUN_004e8e10 (resource load = open + read + close) | `0xe8eb2` FUN_004e8e10 (resource load = open + read + close) | 152 |
| 0 | `0x117bdc` __isleadbyte_l | `0x119da0` FUN_00519434 | 139 |
| 0 | `0xdd6a2` FUN_004dd2c0 (jpg/tga texture load body: resource load 0x004e8e10 + D3DXGetImageInfo + 2D helper + LoadSurface (callee of 0x004f3510)) | `0xdd6a2` FUN_004dd2c0 (jpg/tga texture load body: resource load 0x004e8e10 + D3DXGetImageInfo + 2D helper + LoadSurface (callee of 0x004f3510)) | 58 |
| 0 | `0x11133e` _malloc | `0x11133e` _malloc | 56 |
| 0 | `0xe8dc3` FUN_004e8880 (resource read) | `0x11133e` _malloc | 46 |
| 0 | `0x11133e` _malloc | `0xe6a75` FUN_004e6a40 (malloc + memset allocation wrapper (via 0x004b8b60)) | 43 |
| 0 | `0xdc846` FUN_004dc540 (texture loader (pck dds; image info, cube, 2D helpers)) | `0xdc846` FUN_004dc540 (texture loader (pck dds; image info, cube, 2D helpers)) | 18 |
| 0 | `0xefd0e` FUN_004efcc0 (handle map insert) | `0xa86ff` FUN_004a8670 | 18 |
| 0 | `0x11133e` _malloc | `0xefd0e` FUN_004efcc0 (handle map insert) | 18 |
| 0 | `0x11204f` FUN_00511f77 (CRT fread body (from _fread_s, uses __VEC_memcpy)) | `0x1121d1` _fread_s | 17 |
| 0 | `0x11133e` _malloc | `0x10e21e` _free | 13 |

**Candidates**

Hooked calls account for 1.791 s of the 3.074 s interval (58.3 %); 1.283 s is unexplained by any hook. The sampler recorded 37,939 samples over 5.004 s of report coverage (7582 samples/s, 253 ticks/s per thread); the dominant leaf module is ntdll at 90.0 %. 1 of 1 delta blocks straddle the interval boundary and carry samples from outside it. Function `0x00511f77` (FUN_00511f77) holds 0.0 % of the engine thread's samples as leaf and 21.6 % as first main-module frame; it is a known routine (CRT fread body (from _fread_s, uses __VEC_memcpy)). Function `0x005112c4` (_malloc) holds 0.0 % of the engine thread's samples as leaf and 17.0 % as first main-module frame; it is not a known routine. Function `0x004e8e10` (FUN_004e8e10) holds 0.0 % of the engine thread's samples as leaf and 12.0 % as first main-module frame; it is a known routine (resource load = open + read + close).

### Gap 5 stall 2: report stall 9.119 s

Interval 128.447–137.566 s, length 9.119 s; hooked exclusive 3.119 s over 1 report windows (0 straddling); unexplained 6.001 s.
Samples 116,989 in 3 delta blocks (2 straddling), 15.007 s covered, 7795 samples/s, 260 ticks/s per thread, 0 dropped; sampler tick 1494 µs mean / 27851 µs max, busy 39.5 % of the covered time.

**Hooked time**

| Operation | Calls | Incl. s | Excl. s | Mean ms | Bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `inflate` | 153,761 | 2.688 | 2.688 | 0.017 | 0 |
| `ReadFile` | 39,505 | 0.306 | 0.306 | 0.008 | 159,567,132 |
| `xmlReadMemory` | 94 | 0.059 | 0.059 | 0.630 | 7,647,306 |
| `CreateFileA` | 324 | 0.051 | 0.051 | 0.158 | 0 |
| `FindFirstFileA` | 409 | 0.011 | 0.011 | 0.027 | 0 |
| `SetFilePointer` | 972 | 0.001 | 0.001 | 0.001 | 0 |
| `D3DXCreateMesh` | 4 | 0.001 | 0.001 | 0.271 | 0 |
| `ID3DXMesh::GenerateAdjacency` | 4 | 0.000 | 0.000 | 0.081 | 0 |
| `ID3DXMesh::OptimizeInplace` | 4 | 0.000 | 0.000 | 0.046 | 0 |
| `D3DXCleanMesh` | 4 | 0.000 | 0.000 | 0.029 | 0 |

**Sampled attribution: per-thread module split**

| Slot | TID | Samples | x3ap | ntdll | wine | d3dx | zlib | xml | proxy | other | Start |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 0 | 216 | 3,901 | 0 | 40 | 0 | 0 | 0 | 0 | 0 | 3,861 | 0:0x112ead |
| 1 | 300 | 3,901 | 0 | 3,901 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xb220 |
| 2 | 304 | 3,901 | 0 | 3,901 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xc780 |
| 3 | 316 | 3,901 | 0 | 3,901 | 0 | 0 | 0 | 0 | 0 | 0 | 49:0x38490 |
| 4 | 320 | 3,901 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 3,901 | 4:0xd0f0 |
| 5 | 328 | 3,901 | 0 | 3,901 | 0 | 0 | 0 | 0 | 0 | 0 | 20:0xe740 |
| 6 | 352 | 3,901 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 3,901 | 38:0x2400 |
| 7 | 356 | 3,901 | 0 | 3,901 | 0 | 0 | 0 | 0 | 0 | 0 | 7:0x9900 |
| 8 | 896 | 3,901 | 0 | 3,901 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 9 | 372 | 3,901 | 0 | 3,901 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 10 | 408 | 3,901 | 0 | 3,901 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 11 | 672 | 3,901 | 0 | 3,901 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 12 | 708 | 3,901 | 0 | 3,901 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 13 | 448 | 3,901 | 0 | 3,901 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 14 | 748 | 3,901 | 0 | 3,901 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 15 | 504 | 3,901 | 0 | 3,901 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 16 | 568 | 3,901 | 0 | 3,901 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 17 | 536 | 3,901 | 0 | 3,901 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 18 | 600 | 3,901 | 0 | 3,901 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 19 | 636 | 3,901 | 0 | 3,901 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 20 | 932 | 3,901 | 0 | 3,901 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 21 | 972 | 3,901 | 0 | 3,901 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 22 | 1160 | 3,886 | 0 | 3,886 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 23 | 792 | 3,901 | 0 | 3,901 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 24 | 828 | 3,901 | 0 | 3,901 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 25 | 868 | 3,901 | 0 | 3,901 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 26 | 1012 | 3,901 | 0 | 3,901 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 27 | 1196 | 3,886 | 0 | 3,886 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 28 | 1228 | 2 | 0 | 2 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x9f90 |
| 29 | 1232 | 2 | 0 | 2 | 0 | 0 | 0 | 0 | 0 | 0 | 49:0x38490 |
| 30 | 1092 | 3,901 | 0 | 3,901 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 31 | 1236 | 3,886 | 0 | 3,886 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |

All threads: x3ap 0.0 %, ntdll 90.0 %, wine 0.0 %, d3dx 0.0 %, zlib 0.0 %, xml 0.0 %, proxy 0.0 %, other 10.0 %.

**Top functions (leaf samples by containing function)**

Shares are of the engine thread (slot 0, 3,901 samples); the other sampled threads are idle Wine/audio/input waiters.

| Function start | Ghidra name | Label | Self (engine) | Incl. frame (engine) | Busiest slot | Hooked APIs in interval |
| --- | --- | --- | ---: | ---: | ---: | --- |
| `0x004e8e10` | FUN_004e8e10 | resource load = open + read + close | 0.0 % (0) | 29.5 % (1,149) | — | — |
| `0x005112c4` | _malloc | — | 0.0 % (0) | 24.6 % (958) | — | — |
| `0x00511f77` | FUN_00511f77 | CRT fread body (from _fread_s, uses __VEC_memcpy) | 0.0 % (0) | 8.0 % (312) | — | — |
| `0x004e1220` | FUN_004e1220 | collision tree: recursive split body (self-recursive, 2,267 B) - top loading hot spot | 0.0 % (0) | 4.7 % (185) | — | — |
| `0x00517bcb` | __isleadbyte_l | — | 0.0 % (0) | 3.6 % (142) | — | — |
| `0x0052403b` | __VEC_memzero | — | 0.0 % (0) | 2.9 % (112) | — | — |
| `0x004bcb60` | FUN_004bcb60 | mesh build: CloneMesh into final options (D3DXMESH_32BIT above 64k vertices), two attempts, OOM callback, then install via 0x004bc9c0 | 0.0 % (0) | 2.2 % (84) | — | — |
| `0x004dd2c0` | FUN_004dd2c0 | jpg/tga texture load body: resource load 0x004e8e10 + D3DXGetImageInfo + 2D helper + LoadSurface (callee of 0x004f3510) | 0.0 % (0) | 1.7 % (68) | — | — |
| `0x00527fc2` | FID_conflict:__sopen_helper | — | 0.0 % (0) | 1.4 % (54) | — | — |
| `0x004e8880` | FUN_004e8880 | resource read | 0.0 % (0) | 1.3 % (50) | — | `ReadFile` 39,505 calls / 0.306 s |
| `0x004efcc0` | FUN_004efcc0 | handle map insert | 0.0 % (0) | 1.2 % (46) | — | — |
| `0x004bc1c0` | FUN_004bc1c0 | mesh build: vertex positions (int16 x 1/16384) and normals (D3DXVec3Normalize x3) | 0.0 % (0) | 1.1 % (44) | — | — |
| `0x0050e1b0` | _free | — | 0.0 % (0) | 1.1 % (42) | — | — |
| `0x004bb470` | FUN_004bb470 | mesh build: D3DXCreateMesh + vertex/index fill | 0.0 % (0) | 1.0 % (40) | — | `D3DXCreateMesh` 4 calls / 0.001 s |
| `0x00524aa7` | ___lock_fhandle | — | 0.0 % (0) | 1.0 % (38) | — | — |
| `0x004bc680` | FUN_004bc680 | mesh preparation: adjacency / clean / optimise; calls ID3DXMesh::GenerateAdjacency(1e-6f from 0x00565600) at 0x004bc76a | 0.0 % (0) | 0.7 % (29) | — | `ID3DXMesh::GenerateAdjacency` 4 calls / 0.000 s; `D3DXCleanMesh` 4 calls / 0.000 s; `ID3DXMesh::OptimizeInplace` 4 calls / 0.000 s |
| `0x00528060` | FUN_00528060 | — | 0.0 % (0) | 0.6 % (25) | — | — |
| `0x004a7630` | FUN_004a7630 | — | 0.0 % (0) | 0.5 % (19) | — | — |
| `0x004bb240` | FUN_004bb240 | — | 0.0 % (0) | 0.5 % (18) | — | — |
| `0x004dc540` | FUN_004dc540 | texture loader (pck dds; image info, cube, 2D helpers) | 0.0 % (0) | 0.5 % (18) | — | — |
| `0x00517d8f` | FUN_00517d8f | CRT file-lock release tail (LeaveCriticalSection, from 0x00524cfa) | 0.0 % (0) | 0.4 % (15) | — | — |
| `0x0051f464` | FUN_0051f464 | CRT _read text-mode body (ReadFile, MultiByteToWideChar, __malloc_crt) | 0.0 % (0) | 0.4 % (14) | — | — |
| `0x004eb930` | FUN_004eb930 | — | 0.0 % (0) | 0.3 % (11) | — | — |
| `0x004b4600` | FUN_004b4600 | — | 0.0 % (0) | 0.3 % (10) | — | — |
| `0x00519434` | FUN_00519434 | — | 0.0 % (0) | 0.2 % (7) | — | — |
| `0x00409a90` | FUN_00409a90 | — | 0.0 % (0) | 0.2 % (7) | — | — |
| `0x004a7f60` | FUN_004a7f60 | — | 0.0 % (0) | 0.1 % (5) | — | — |
| `0x004e6a40` | FUN_004e6a40 | malloc + memset allocation wrapper (via 0x004b8b60) | 0.0 % (0) | 0.1 % (5) | — | — |
| `0x0047eb90` | FUN_0047eb90 | collision-tree build driver (body triangles int16x1/16384 -> 0x004e0ce0 per triangle, then build 0x004e0c80; sets obj flag 0x1000000) | 0.0 % (0) | 0.1 % (4) | — | — |
| `0x00408a80` | FUN_00408a80 | — | 0.0 % (0) | 0.1 % (4) | — | — |
| `0x00483f20` | FUN_00483f20 | text body parse | 0.0 % (0) | 0.1 % (3) | — | — |
| (no main-module frame) | | | | 96.9 % (113,353) | | |

**Top caller pairs**

| Slot | Frame | Caller | Samples |
| ---: | --- | --- | ---: |
| 0 | `0xe8e8d` FUN_004e8e10 (resource load = open + read + close) | `0xe8eb2` FUN_004e8e10 (resource load = open + read + close) | 1,149 |
| 0 | `0x11133e` _malloc | `0x11133e` _malloc | 334 |
| 0 | `0x11133e` _malloc | `0x10e21e` _free | 323 |
| 0 | `0x11204f` FUN_00511f77 (CRT fread body (from _fread_s, uses __VEC_memcpy)) | `0xe8dc3` FUN_004e8880 (resource read) | 292 |
| 0 | `0x117bdc` __isleadbyte_l | `0x119da0` FUN_00519434 | 139 |
| 0 | `0xbcc30` FUN_004bcb60 (mesh build: CloneMesh into final options (D3DXMESH_32BIT above 64k vertices), two attempts, OOM callback, then install via 0x004bc9c0) | `0xbb9ac` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 84 |
| 0 | `0xdd6a2` FUN_004dd2c0 (jpg/tga texture load body: resource load 0x004e8e10 + D3DXGetImageInfo + 2D helper + LoadSurface (callee of 0x004f3510)) | `0xdd6a2` FUN_004dd2c0 (jpg/tga texture load body: resource load 0x004e8e10 + D3DXGetImageInfo + 2D helper + LoadSurface (callee of 0x004f3510)) | 68 |
| 0 | `0x11133e` _malloc | `0xe6a75` FUN_004e6a40 (malloc + memset allocation wrapper (via 0x004b8b60)) | 59 |
| 0 | `0x124071` __VEC_memzero | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise; calls ID3DXMesh::GenerateAdjacency(1e-6f from 0x00565600) at 0x004bc76a) | 54 |
| 0 | `0x11133e` _malloc | `0xefd0e` FUN_004efcc0 (handle map insert) | 53 |
| 0 | `0xe1538` FUN_004e1220 (collision tree: recursive split body (self-recursive, 2,267 B) - top loading hot spot) | `0xe1538` FUN_004e1220 (collision tree: recursive split body (self-recursive, 2,267 B) - top loading hot spot) | 47 |
| 0 | `0x1240bd` __VEC_memzero | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise; calls ID3DXMesh::GenerateAdjacency(1e-6f from 0x00565600) at 0x004bc76a) | 47 |

**Candidates**

Hooked calls account for 3.119 s of the 9.119 s interval (34.2 %); 6.001 s is unexplained by any hook. The sampler recorded 116,989 samples over 15.007 s of report coverage (7795 samples/s, 260 ticks/s per thread); the dominant leaf module is ntdll at 90.0 %. 2 of 3 delta blocks straddle the interval boundary and carry samples from outside it. Function `0x004e8e10` (FUN_004e8e10) holds 0.0 % of the engine thread's samples as leaf and 29.5 % as first main-module frame; it is a known routine (resource load = open + read + close). Function `0x005112c4` (_malloc) holds 0.0 % of the engine thread's samples as leaf and 24.6 % as first main-module frame; it is not a known routine. Function `0x00511f77` (FUN_00511f77) holds 0.0 % of the engine thread's samples as leaf and 8.0 % as first main-module frame; it is a known routine (CRT fread body (from _fread_s, uses __VEC_memcpy)).

## Gap 6: sector change (6.175 s, ends at frame 12672)

Label evidence: 2228 GenerateAdjacency calls with 59,921,613 B of texture input after an earlier labelled phase.
Interval 331.432–338.263 s, length 6.175 s; hooked exclusive 3.854 s over 7 report windows (2 straddling); unexplained 2.321 s.
Samples 76,096 in 2 delta blocks (2 straddling), 10.001 s covered, 7609 samples/s, 238 ticks/s per thread, 0 dropped; sampler tick 1737 µs mean / 28850 µs max, busy 42.2 % of the covered time.

**Hooked time**

| Operation | Calls | Incl. s | Excl. s | Mean ms | Bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `inflate` | 80,095 | 1.326 | 1.326 | 0.017 | 0 |
| `ID3DXMesh::GenerateAdjacency` | 2,228 | 0.813 | 0.813 | 0.365 | 0 |
| `D3DXCreateMesh` | 2,228 | 0.585 | 0.585 | 0.263 | 0 |
| `ID3DXMesh::OptimizeInplace` | 2,228 | 0.434 | 0.434 | 0.195 | 0 |
| `D3DXCleanMesh` | 2,229 | 0.326 | 0.326 | 0.146 | 0 |
| `ReadFile` | 20,566 | 0.212 | 0.212 | 0.010 | 83,547,136 |
| `D3DXCreateTextureFromFileInMemoryEx` | 106 | 0.097 | 0.097 | 0.917 | 59,921,613 |
| `D3DXCreateEffect` | 2 | 0.039 | 0.039 | 19.640 | 28,932 |
| `CreateFileA` | 165 | 0.016 | 0.016 | 0.098 | 0 |
| `FindFirstFileA` | 171 | 0.005 | 0.005 | 0.029 | 0 |
| `SetFilePointer` | 495 | 0.001 | 0.001 | 0.001 | 0 |

**Sampled attribution: per-thread module split**

| Slot | TID | Samples | x3ap | ntdll | wine | d3dx | zlib | xml | proxy | other | Start |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 0 | 216 | 2,378 | 0 | 94 | 0 | 0 | 0 | 0 | 0 | 2,284 | 0:0x112ead |
| 1 | 300 | 2,378 | 0 | 2,378 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xb220 |
| 2 | 304 | 2,378 | 0 | 2,378 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xc780 |
| 3 | 316 | 2,378 | 0 | 2,378 | 0 | 0 | 0 | 0 | 0 | 0 | 49:0x38490 |
| 4 | 320 | 2,378 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 2,378 | 4:0xd0f0 |
| 5 | 328 | 2,378 | 0 | 2,378 | 0 | 0 | 0 | 0 | 0 | 0 | 20:0xe740 |
| 6 | 352 | 2,378 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 2,378 | 38:0x2400 |
| 7 | 356 | 2,378 | 0 | 2,378 | 0 | 0 | 0 | 0 | 0 | 0 | 7:0x9900 |
| 8 | 896 | 2,378 | 0 | 2,378 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 9 | 372 | 2,378 | 0 | 2,378 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 10 | 408 | 2,378 | 0 | 2,378 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 11 | 672 | 2,378 | 0 | 2,378 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 12 | 708 | 2,378 | 0 | 2,378 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 13 | 448 | 2,378 | 0 | 2,378 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 14 | 748 | 2,378 | 0 | 2,378 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 15 | 504 | 2,378 | 0 | 2,378 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 16 | 568 | 2,378 | 0 | 2,378 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 17 | 536 | 2,378 | 0 | 2,378 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 18 | 600 | 2,378 | 0 | 2,378 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 19 | 636 | 2,378 | 0 | 2,378 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 20 | 932 | 2,378 | 0 | 2,378 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 21 | 972 | 2,378 | 0 | 2,378 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 22 | 1160 | 2,378 | 0 | 2,378 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 23 | 792 | 2,378 | 0 | 2,378 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 24 | 828 | 2,378 | 0 | 2,378 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 25 | 868 | 2,378 | 0 | 2,378 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 26 | 1012 | 2,378 | 0 | 2,378 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 27 | 1196 | 2,378 | 0 | 2,378 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 28 | 1264 | 2,378 | 0 | 2,378 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 29 | 1300 | 2,378 | 0 | 2,378 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 30 | 1092 | 2,378 | 0 | 2,378 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 31 | 1236 | 2,378 | 0 | 2,378 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |

All threads: x3ap 0.0 %, ntdll 90.7 %, wine 0.0 %, d3dx 0.0 %, zlib 0.0 %, xml 0.0 %, proxy 0.0 %, other 9.3 %.

**Top functions (leaf samples by containing function)**

Shares are of the engine thread (slot 0, 2,378 samples); the other sampled threads are idle Wine/audio/input waiters.

| Function start | Ghidra name | Label | Self (engine) | Incl. frame (engine) | Busiest slot | Hooked APIs in interval |
| --- | --- | --- | ---: | ---: | ---: | --- |
| `0x004b4f80` | FUN_004b4f80 | CPureDeviceStateManager::SetRenderState (filters via 0x004b5620, then device vtable+0xe4) | 0.0 % (0) | 15.6 % (370) | — | — |
| `0x004bcb60` | FUN_004bcb60 | mesh build: CloneMesh into final options (D3DXMESH_32BIT above 64k vertices), two attempts, OOM callback, then install via 0x004bc9c0 | 0.0 % (0) | 10.1 % (239) | — | — |
| `0x0052403b` | __VEC_memzero | — | 0.0 % (0) | 9.7 % (230) | — | — |
| `0x004d5a90` | FUN_004d5a90 | Win32 input / message handling (GetKeyboardLayout, MapVirtualKeyExA; helper 0x004d6850) | 0.0 % (0) | 7.4 % (176) | — | — |
| `0x004e8e10` | FUN_004e8e10 | resource load = open + read + close | 0.0 % (0) | 6.5 % (155) | — | — |
| `0x00511f77` | FUN_00511f77 | CRT fread body (from _fread_s, uses __VEC_memcpy) | 0.0 % (0) | 5.7 % (136) | — | — |
| `0x0051da61` | __VEC_memcpy | — | 0.0 % (0) | 5.5 % (131) | — | — |
| `0x004bb470` | FUN_004bb470 | mesh build: D3DXCreateMesh + vertex/index fill | 0.0 % (0) | 4.5 % (107) | — | `D3DXCreateMesh` 2,228 calls / 0.585 s |
| `0x0050e1b0` | _free | — | 0.0 % (0) | 4.5 % (106) | — | — |
| `0x004bc680` | FUN_004bc680 | mesh preparation: adjacency / clean / optimise; calls ID3DXMesh::GenerateAdjacency(1e-6f from 0x00565600) at 0x004bc76a | 0.0 % (0) | 3.0 % (71) | — | `ID3DXMesh::GenerateAdjacency` 2,228 calls / 0.813 s; `D3DXCleanMesh` 2,229 calls / 0.326 s; `ID3DXMesh::OptimizeInplace` 2,228 calls / 0.434 s |
| `0x004bc1c0` | FUN_004bc1c0 | mesh build: vertex positions (int16 x 1/16384) and normals (D3DXVec3Normalize x3) | 0.0 % (0) | 2.1 % (49) | — | — |
| `0x005112c4` | _malloc | — | 0.0 % (0) | 1.6 % (37) | — | — |
| `0x0044ccc0` | FUN_0044ccc0 | orientation/rotation math variant (fsin/fcos/fpatan; from 0x0042fb20) | 0.0 % (0) | 1.6 % (37) | — | — |
| `0x0051f464` | FUN_0051f464 | CRT _read text-mode body (ReadFile, MultiByteToWideChar, __malloc_crt) | 0.0 % (0) | 1.4 % (33) | — | — |
| `0x004e1220` | FUN_004e1220 | collision tree: recursive split body (self-recursive, 2,267 B) - top loading hot spot | 0.0 % (0) | 1.3 % (32) | — | — |
| `0x0044c620` | FUN_0044c620 | orientation/rotation math (fsin/fcos/fpatan, helper 0x0052b5d0; from 0x0042fb20) | 0.0 % (0) | 0.8 % (20) | — | — |
| `0x00524aa7` | ___lock_fhandle | — | 0.0 % (0) | 0.8 % (18) | — | — |
| `0x004b51b0` | FUN_004b51b0 | — | 0.0 % (0) | 0.7 % (17) | — | — |
| `0x004b50a0` | FUN_004b50a0 | — | 0.0 % (0) | 0.7 % (16) | — | — |
| `0x00527fc2` | FID_conflict:__sopen_helper | — | 0.0 % (0) | 0.5 % (13) | — | — |
| (no main-module frame) | | | | 97.0 % (73,839) | | |

**Top caller pairs**

| Slot | Frame | Caller | Samples |
| ---: | --- | --- | ---: |
| 0 | `0xbcc30` FUN_004bcb60 (mesh build: CloneMesh into final options (D3DXMESH_32BIT above 64k vertices), two attempts, OOM callback, then install via 0x004bc9c0) | `0xbb9ac` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 239 |
| 0 | `0xb4f99` FUN_004b4f80 (CPureDeviceStateManager::SetRenderState (filters via 0x004b5620, then device vtable+0xe4)) | `0xc403e` FUN_004c0150 (effect state manager: matrix uploads per draw) | 169 |
| 0 | `0xe8e8d` FUN_004e8e10 (resource load = open + read + close) | `0xe8eb2` FUN_004e8e10 (resource load = open + read + close) | 155 |
| 0 | `0x11dab3` __VEC_memcpy | `0x11204f` FUN_00511f77 (CRT fread body (from _fread_s, uses __VEC_memcpy)) | 127 |
| 0 | `0x124071` __VEC_memzero | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise; calls ID3DXMesh::GenerateAdjacency(1e-6f from 0x00565600) at 0x004bc76a) | 112 |
| 0 | `0x11204f` FUN_00511f77 (CRT fread body (from _fread_s, uses __VEC_memcpy)) | `0xe8da9` FUN_004e8880 (resource read) | 109 |
| 0 | `0x1240bd` __VEC_memzero | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise; calls ID3DXMesh::GenerateAdjacency(1e-6f from 0x00565600) at 0x004bc76a) | 108 |
| 0 | `0xbb72f` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | `0xbb72f` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 107 |
| 0 | `0x10e21e` _free | `0x10e23d` _free | 99 |
| 0 | `0xd6509` FUN_004d5a90 (Win32 input / message handling (GetKeyboardLayout, MapVirtualKeyExA; helper 0x004d6850)) | `0xf22ec` FUN_004f22c0 (fpatan angle helper) | 72 |
| 0 | `0xb4f99` FUN_004b4f80 (CPureDeviceStateManager::SetRenderState (filters via 0x004b5620, then device vtable+0xe4)) | `0x11133e` _malloc | 64 |
| 0 | `0xb4fad` FUN_004b4f80 (CPureDeviceStateManager::SetRenderState (filters via 0x004b5620, then device vtable+0xe4)) | `0xb4f99` FUN_004b4f80 (CPureDeviceStateManager::SetRenderState (filters via 0x004b5620, then device vtable+0xe4)) | 57 |

**Candidates**

Hooked calls account for 3.854 s of the 6.175 s interval (62.4 %); 2.321 s is unexplained by any hook. The sampler recorded 76,096 samples over 10.001 s of report coverage (7609 samples/s, 238 ticks/s per thread); the dominant leaf module is ntdll at 90.7 %. 2 of 2 delta blocks straddle the interval boundary and carry samples from outside it. Function `0x004b4f80` (FUN_004b4f80) holds 0.0 % of the engine thread's samples as leaf and 15.6 % as first main-module frame; it is a known routine (CPureDeviceStateManager::SetRenderState (filters via 0x004b5620, then device vtable+0xe4)). Function `0x004bcb60` (FUN_004bcb60) holds 0.0 % of the engine thread's samples as leaf and 10.1 % as first main-module frame; it is a known routine (mesh build: CloneMesh into final options (D3DXMESH_32BIT above 64k vertices), two attempts, OOM callback, then install via 0x004bc9c0). Function `0x0052403b` (__VEC_memzero) holds 0.0 % of the engine thread's samples as leaf and 9.7 % as first main-module frame; it is not a known routine.

### Gap 6 stall 1: report stall 2.152 s

Interval 331.441–333.593 s, length 2.152 s; hooked exclusive 0.923 s over 1 report windows (0 straddling); unexplained 1.229 s.
Samples 40,512 in 1 delta blocks (1 straddling), 5.000 s covered, 8102 samples/s, 253 ticks/s per thread, 0 dropped; sampler tick 1611 µs mean / 5280 µs max, busy 41.7 % of the covered time.

**Hooked time**

| Operation | Calls | Incl. s | Excl. s | Mean ms | Bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `inflate` | 46,161 | 0.811 | 0.811 | 0.018 | 0 |
| `ReadFile` | 11,689 | 0.104 | 0.104 | 0.009 | 47,562,752 |
| `CreateFileA` | 44 | 0.004 | 0.004 | 0.080 | 0 |
| `D3DXCreateMesh` | 5 | 0.002 | 0.002 | 0.321 | 0 |
| `FindFirstFileA` | 44 | 0.002 | 0.002 | 0.035 | 0 |
| `ID3DXMesh::GenerateAdjacency` | 5 | 0.000 | 0.000 | 0.088 | 0 |
| `ID3DXMesh::OptimizeInplace` | 5 | 0.000 | 0.000 | 0.043 | 0 |
| `SetFilePointer` | 132 | 0.000 | 0.000 | 0.002 | 0 |
| `D3DXCleanMesh` | 5 | 0.000 | 0.000 | 0.023 | 0 |

**Sampled attribution: per-thread module split**

| Slot | TID | Samples | x3ap | ntdll | wine | d3dx | zlib | xml | proxy | other | Start |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 0 | 216 | 1,266 | 0 | 34 | 0 | 0 | 0 | 0 | 0 | 1,232 | 0:0x112ead |
| 1 | 300 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xb220 |
| 2 | 304 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xc780 |
| 3 | 316 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 49:0x38490 |
| 4 | 320 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 1,266 | 4:0xd0f0 |
| 5 | 328 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 20:0xe740 |
| 6 | 352 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 1,266 | 38:0x2400 |
| 7 | 356 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 7:0x9900 |
| 8 | 896 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 9 | 372 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 10 | 408 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 11 | 672 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 12 | 708 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 13 | 448 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 14 | 748 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 15 | 504 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 16 | 568 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 17 | 536 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 18 | 600 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 19 | 636 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 20 | 932 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 21 | 972 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 22 | 1160 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 23 | 792 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 24 | 828 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 25 | 868 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 26 | 1012 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 27 | 1196 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 28 | 1264 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 29 | 1300 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 30 | 1092 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 31 | 1236 | 1,266 | 0 | 1,266 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |

All threads: x3ap 0.0 %, ntdll 90.7 %, wine 0.0 %, d3dx 0.0 %, zlib 0.0 %, xml 0.0 %, proxy 0.0 %, other 9.3 %.

**Top functions (leaf samples by containing function)**

Shares are of the engine thread (slot 0, 1,266 samples); the other sampled threads are idle Wine/audio/input waiters.

| Function start | Ghidra name | Label | Self (engine) | Incl. frame (engine) | Busiest slot | Hooked APIs in interval |
| --- | --- | --- | ---: | ---: | ---: | --- |
| `0x004d5a90` | FUN_004d5a90 | Win32 input / message handling (GetKeyboardLayout, MapVirtualKeyExA; helper 0x004d6850) | 0.0 % (0) | 13.9 % (176) | — | — |
| `0x004b4f80` | FUN_004b4f80 | CPureDeviceStateManager::SetRenderState (filters via 0x004b5620, then device vtable+0xe4) | 0.0 % (0) | 12.8 % (162) | — | — |
| `0x00511f77` | FUN_00511f77 | CRT fread body (from _fread_s, uses __VEC_memcpy) | 0.0 % (0) | 10.7 % (136) | — | — |
| `0x0051da61` | __VEC_memcpy | — | 0.0 % (0) | 10.3 % (131) | — | — |
| `0x004bcb60` | FUN_004bcb60 | mesh build: CloneMesh into final options (D3DXMESH_32BIT above 64k vertices), two attempts, OOM callback, then install via 0x004bc9c0 | 0.0 % (0) | 8.3 % (105) | — | — |
| `0x0052403b` | __VEC_memzero | — | 0.0 % (0) | 8.2 % (104) | — | — |
| `0x004bb470` | FUN_004bb470 | mesh build: D3DXCreateMesh + vertex/index fill | 0.0 % (0) | 3.3 % (42) | — | `D3DXCreateMesh` 5 calls / 0.002 s |
| `0x0044ccc0` | FUN_0044ccc0 | orientation/rotation math variant (fsin/fcos/fpatan; from 0x0042fb20) | 0.0 % (0) | 2.9 % (37) | — | — |
| `0x0050e1b0` | _free | — | 0.0 % (0) | 2.8 % (36) | — | — |
| `0x0051f464` | FUN_0051f464 | CRT _read text-mode body (ReadFile, MultiByteToWideChar, __malloc_crt) | 0.0 % (0) | 2.6 % (33) | — | — |
| `0x004e1220` | FUN_004e1220 | collision tree: recursive split body (self-recursive, 2,267 B) - top loading hot spot | 0.0 % (0) | 2.5 % (32) | — | — |
| `0x004bc1c0` | FUN_004bc1c0 | mesh build: vertex positions (int16 x 1/16384) and normals (D3DXVec3Normalize x3) | 0.0 % (0) | 2.4 % (30) | — | — |
| `0x004bc680` | FUN_004bc680 | mesh preparation: adjacency / clean / optimise; calls ID3DXMesh::GenerateAdjacency(1e-6f from 0x00565600) at 0x004bc76a | 0.0 % (0) | 2.4 % (30) | — | `ID3DXMesh::GenerateAdjacency` 5 calls / 0.000 s; `D3DXCleanMesh` 5 calls / 0.000 s; `ID3DXMesh::OptimizeInplace` 5 calls / 0.000 s |
| `0x005112c4` | _malloc | — | 0.0 % (0) | 1.3 % (17) | — | — |
| (no main-module frame) | | | | 97.0 % (39,313) | | |

**Top caller pairs**

| Slot | Frame | Caller | Samples |
| ---: | --- | --- | ---: |
| 0 | `0x11dab3` __VEC_memcpy | `0x11204f` FUN_00511f77 (CRT fread body (from _fread_s, uses __VEC_memcpy)) | 127 |
| 0 | `0x11204f` FUN_00511f77 (CRT fread body (from _fread_s, uses __VEC_memcpy)) | `0xe8da9` FUN_004e8880 (resource read) | 109 |
| 0 | `0xbcc30` FUN_004bcb60 (mesh build: CloneMesh into final options (D3DXMESH_32BIT above 64k vertices), two attempts, OOM callback, then install via 0x004bc9c0) | `0xbb9ac` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 105 |
| 0 | `0xd6509` FUN_004d5a90 (Win32 input / message handling (GetKeyboardLayout, MapVirtualKeyExA; helper 0x004d6850)) | `0xf22ec` FUN_004f22c0 (fpatan angle helper) | 72 |
| 0 | `0x1240bd` __VEC_memzero | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise; calls ID3DXMesh::GenerateAdjacency(1e-6f from 0x00565600) at 0x004bc76a) | 59 |
| 0 | `0xb4f99` FUN_004b4f80 (CPureDeviceStateManager::SetRenderState (filters via 0x004b5620, then device vtable+0xe4)) | `0x11133e` _malloc | 50 |
| 0 | `0xb4f99` FUN_004b4f80 (CPureDeviceStateManager::SetRenderState (filters via 0x004b5620, then device vtable+0xe4)) | `0xc403e` FUN_004c0150 (effect state manager: matrix uploads per draw) | 44 |
| 0 | `0x124071` __VEC_memzero | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise; calls ID3DXMesh::GenerateAdjacency(1e-6f from 0x00565600) at 0x004bc76a) | 43 |
| 0 | `0xbb72f` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | `0xbb72f` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 42 |
| 0 | `0x10e21e` _free | `0x10e23d` _free | 33 |
| 0 | `0x11f665` FUN_0051f464 (CRT _read text-mode body (ReadFile, MultiByteToWideChar, __malloc_crt)) | `0x11f665` FUN_0051f464 (CRT _read text-mode body (ReadFile, MultiByteToWideChar, __malloc_crt)) | 33 |
| 0 | `0x4cfc1` FUN_0044ccc0 (orientation/rotation math variant (fsin/fcos/fpatan; from 0x0042fb20)) | `0x50a79` FUN_00450980 | 31 |

**Candidates**

Hooked calls account for 0.923 s of the 2.152 s interval (42.9 %); 1.229 s is unexplained by any hook. The sampler recorded 40,512 samples over 5.000 s of report coverage (8102 samples/s, 253 ticks/s per thread); the dominant leaf module is ntdll at 90.7 %. 1 of 1 delta blocks straddle the interval boundary and carry samples from outside it. Function `0x004d5a90` (FUN_004d5a90) holds 0.0 % of the engine thread's samples as leaf and 13.9 % as first main-module frame; it is a known routine (Win32 input / message handling (GetKeyboardLayout, MapVirtualKeyExA; helper 0x004d6850)). Function `0x004b4f80` (FUN_004b4f80) holds 0.0 % of the engine thread's samples as leaf and 12.8 % as first main-module frame; it is a known routine (CPureDeviceStateManager::SetRenderState (filters via 0x004b5620, then device vtable+0xe4)). Function `0x00511f77` (FUN_00511f77) holds 0.0 % of the engine thread's samples as leaf and 10.7 % as first main-module frame; it is a known routine (CRT fread body (from _fread_s, uses __VEC_memcpy)).

## Gap 7: menu load (8.400 s, ends at frame 14450)

Label evidence: 1017 GenerateAdjacency calls and 298,138,122 B of 2D texture-helper input match the main-menu work vector.
Interval 362.300–371.158 s, length 8.400 s; hooked exclusive 4.765 s over 9 report windows (2 straddling); unexplained 3.635 s.
Samples 74,080 in 2 delta blocks (1 straddling), 10.002 s covered, 7406 samples/s, 231 ticks/s per thread, 0 dropped; sampler tick 1745 µs mean / 27474 µs max, busy 41.9 % of the covered time.

**Hooked time**

| Operation | Calls | Incl. s | Excl. s | Mean ms | Bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `inflate` | 166,017 | 2.637 | 2.637 | 0.016 | 0 |
| `D3DXCreateTextureFromFileInMemoryEx` | 459 | 0.529 | 0.529 | 1.153 | 298,138,122 |
| `ReadFile` | 43,380 | 0.453 | 0.453 | 0.010 | 183,521,929 |
| `ID3DXMesh::GenerateAdjacency` | 1,017 | 0.409 | 0.409 | 0.402 | 0 |
| `D3DXCreateMesh` | 1,017 | 0.266 | 0.266 | 0.262 | 0 |
| `ID3DXMesh::OptimizeInplace` | 1,017 | 0.206 | 0.206 | 0.203 | 0 |
| `D3DXCleanMesh` | 1,023 | 0.119 | 0.119 | 0.117 | 0 |
| `D3DXLoadSurfaceFromFileInMemory` | 3 | 0.051 | 0.051 | 17.033 | 2,480,817 |
| `CreateFileA` | 584 | 0.037 | 0.037 | 0.063 | 0 |
| `xmlReadMemory` | 2 | 0.020 | 0.020 | 9.849 | 3,688,819 |
| `FindFirstFileA` | 686 | 0.019 | 0.019 | 0.028 | 0 |
| `D3DXCreateCubeTextureFromFileInMemoryEx` | 9 | 0.016 | 0.016 | 1.777 | 1,509,024 |

**Sampled attribution: per-thread module split**

| Slot | TID | Samples | x3ap | ntdll | wine | d3dx | zlib | xml | proxy | other | Start |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 0 | 216 | 2,315 | 0 | 68 | 0 | 0 | 0 | 0 | 0 | 2,247 | 0:0x112ead |
| 1 | 300 | 2,315 | 0 | 2,315 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xb220 |
| 2 | 304 | 2,315 | 0 | 2,315 | 0 | 0 | 0 | 0 | 0 | 0 | 53:0xc780 |
| 3 | 316 | 2,315 | 0 | 2,315 | 0 | 0 | 0 | 0 | 0 | 0 | 49:0x38490 |
| 4 | 320 | 2,315 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 2,315 | 4:0xd0f0 |
| 5 | 328 | 2,315 | 0 | 2,315 | 0 | 0 | 0 | 0 | 0 | 0 | 20:0xe740 |
| 6 | 352 | 2,315 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 2,315 | 38:0x2400 |
| 7 | 356 | 2,315 | 0 | 2,315 | 0 | 0 | 0 | 0 | 0 | 0 | 7:0x9900 |
| 8 | 896 | 2,315 | 0 | 2,315 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 9 | 372 | 2,315 | 0 | 2,315 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 10 | 408 | 2,315 | 0 | 2,315 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 11 | 672 | 2,315 | 0 | 2,315 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 12 | 708 | 2,315 | 0 | 2,315 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 13 | 448 | 2,315 | 0 | 2,315 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 14 | 748 | 2,315 | 0 | 2,315 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 15 | 504 | 2,315 | 0 | 2,315 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 16 | 568 | 2,315 | 0 | 2,315 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 17 | 536 | 2,315 | 0 | 2,315 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 18 | 600 | 2,315 | 0 | 2,315 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 19 | 636 | 2,315 | 0 | 2,315 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 20 | 932 | 2,315 | 0 | 2,315 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 21 | 972 | 2,315 | 0 | 2,315 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 22 | 1160 | 2,315 | 0 | 2,315 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 23 | 792 | 2,315 | 0 | 2,315 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 24 | 828 | 2,315 | 0 | 2,315 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 25 | 868 | 2,315 | 0 | 2,315 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 26 | 1012 | 2,315 | 0 | 2,315 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 27 | 1196 | 2,315 | 0 | 2,315 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 28 | 1264 | 2,315 | 0 | 2,315 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 29 | 1300 | 2,315 | 0 | 2,315 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 30 | 1092 | 2,315 | 0 | 2,315 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |
| 31 | 1236 | 2,315 | 0 | 2,315 | 0 | 0 | 0 | 0 | 0 | 0 | 57:0x8550 |

All threads: x3ap 0.0 %, ntdll 90.7 %, wine 0.0 %, d3dx 0.0 %, zlib 0.0 %, xml 0.0 %, proxy 0.0 %, other 9.3 %.

**Top functions (leaf samples by containing function)**

Shares are of the engine thread (slot 0, 2,315 samples); the other sampled threads are idle Wine/audio/input waiters.

| Function start | Ghidra name | Label | Self (engine) | Incl. frame (engine) | Busiest slot | Hooked APIs in interval |
| --- | --- | --- | ---: | ---: | ---: | --- |
| `0x004e8e10` | FUN_004e8e10 | resource load = open + read + close | 0.0 % (0) | 24.3 % (562) | — | — |
| `0x0044c620` | FUN_0044c620 | orientation/rotation math (fsin/fcos/fpatan, helper 0x0052b5d0; from 0x0042fb20) | 0.0 % (0) | 12.1 % (281) | — | — |
| `0x004e1220` | FUN_004e1220 | collision tree: recursive split body (self-recursive, 2,267 B) - top loading hot spot | 0.0 % (0) | 6.3 % (147) | — | — |
| `0x004bcb60` | FUN_004bcb60 | mesh build: CloneMesh into final options (D3DXMESH_32BIT above 64k vertices), two attempts, OOM callback, then install via 0x004bc9c0 | 0.0 % (0) | 3.6 % (84) | — | — |
| `0x004dc540` | FUN_004dc540 | texture loader (pck dds; image info, cube, 2D helpers) | 0.0 % (0) | 3.6 % (84) | — | `D3DXCreateTextureFromFileInMemoryEx` 459 calls / 0.529 s; `D3DXCreateCubeTextureFromFileInMemoryEx` 9 calls / 0.016 s |
| `0x0050e1b0` | _free | — | 0.0 % (0) | 3.5 % (82) | — | — |
| `0x004bb470` | FUN_004bb470 | mesh build: D3DXCreateMesh + vertex/index fill | 0.0 % (0) | 2.8 % (64) | — | `D3DXCreateMesh` 1,017 calls / 0.266 s |
| `0x0052403b` | __VEC_memzero | — | 0.0 % (0) | 2.7 % (62) | — | — |
| `0x004bc680` | FUN_004bc680 | mesh preparation: adjacency / clean / optimise; calls ID3DXMesh::GenerateAdjacency(1e-6f from 0x00565600) at 0x004bc76a | 0.0 % (0) | 2.6 % (61) | — | `ID3DXMesh::GenerateAdjacency` 1,017 calls / 0.409 s; `D3DXCleanMesh` 1,023 calls / 0.119 s; `ID3DXMesh::OptimizeInplace` 1,017 calls / 0.206 s |
| `0x00511f77` | FUN_00511f77 | CRT fread body (from _fread_s, uses __VEC_memcpy) | 0.0 % (0) | 2.5 % (57) | — | — |
| `0x004e8880` | FUN_004e8880 | resource read | 0.0 % (0) | 2.4 % (56) | — | `ReadFile` 43,380 calls / 0.453 s |
| `0x0044ccc0` | FUN_0044ccc0 | orientation/rotation math variant (fsin/fcos/fpatan; from 0x0042fb20) | 0.0 % (0) | 2.4 % (55) | — | — |
| `0x004b4f80` | FUN_004b4f80 | CPureDeviceStateManager::SetRenderState (filters via 0x004b5620, then device vtable+0xe4) | 0.0 % (0) | 2.0 % (47) | — | — |
| `0x005112c4` | _malloc | — | 0.0 % (0) | 1.6 % (37) | — | — |
| `0x0051da61` | __VEC_memcpy | — | 0.0 % (0) | 1.3 % (29) | — | — |
| `0x0050f786` | _memcpy_s | — | 0.0 % (0) | 1.2 % (28) | — | — |
| `0x004bb240` | FUN_004bb240 | — | 0.0 % (0) | 1.2 % (27) | — | — |
| `0x00528060` | FUN_00528060 | — | 0.0 % (0) | 1.1 % (26) | — | — |
| `0x004cf460` | FUN_004cf460 | DirectShow movie playback setup (CoCreateInstance of the X MPEG filter graph, Data\mov\%s, %05d.dat) | 0.0 % (0) | 1.1 % (25) | — | — |
| `0x0042fe10` | FUN_0042fe10 | — | 0.0 % (0) | 1.0 % (24) | — | — |
| `0x00519434` | FUN_00519434 | — | 0.0 % (0) | 1.0 % (22) | — | — |
| `0x0044c970` | FUN_0044c970 | — | 0.0 % (0) | 0.9 % (21) | — | — |
| `0x004bc1c0` | FUN_004bc1c0 | mesh build: vertex positions (int16 x 1/16384) and normals (D3DXVec3Normalize x3) | 0.0 % (0) | 0.8 % (19) | — | — |
| `0x00517d8f` | FUN_00517d8f | CRT file-lock release tail (LeaveCriticalSection, from 0x00524cfa) | 0.0 % (0) | 0.4 % (10) | — | — |
| (no main-module frame) | | | | 97.1 % (71,920) | | |

**Top caller pairs**

| Slot | Frame | Caller | Samples |
| ---: | --- | --- | ---: |
| 0 | `0xe8e8d` FUN_004e8e10 (resource load = open + read + close) | `0xe8eb2` FUN_004e8e10 (resource load = open + read + close) | 562 |
| 0 | `0x4c90e` FUN_0044c620 (orientation/rotation math (fsin/fcos/fpatan, helper 0x0052b5d0; from 0x0042fb20)) | `0x2fb7c` FUN_0042fb20 | 277 |
| 0 | `0xbcc30` FUN_004bcb60 (mesh build: CloneMesh into final options (D3DXMESH_32BIT above 64k vertices), two attempts, OOM callback, then install via 0x004bc9c0) | `0xbb9ac` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 90 |
| 0 | `0xdc846` FUN_004dc540 (texture loader (pck dds; image info, cube, 2D helpers)) | `0xdc846` FUN_004dc540 (texture loader (pck dds; image info, cube, 2D helpers)) | 84 |
| 0 | `0xbb72f` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | `0xbb72f` FUN_004bb470 (mesh build: D3DXCreateMesh + vertex/index fill) | 68 |
| 0 | `0xe8da9` FUN_004e8880 (resource read) | `0xe8da9` FUN_004e8880 (resource read) | 56 |
| 0 | `0x10e21e` _free | `0x10e23d` _free | 38 |
| 0 | `0xe1538` FUN_004e1220 (collision tree: recursive split body (self-recursive, 2,267 B) - top loading hot spot) | `0xe1538` FUN_004e1220 (collision tree: recursive split body (self-recursive, 2,267 B) - top loading hot spot) | 37 |
| 0 | `0xe1a18` FUN_004e1220 (collision tree: recursive split body (self-recursive, 2,267 B) - top loading hot spot) | `0xe1538` FUN_004e1220 (collision tree: recursive split body (self-recursive, 2,267 B) - top loading hot spot) | 37 |
| 0 | `0x4cfc1` FUN_0044ccc0 (orientation/rotation math variant (fsin/fcos/fpatan; from 0x0042fb20)) | `0x2fb7c` FUN_0042fb20 | 35 |
| 0 | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise; calls ID3DXMesh::GenerateAdjacency(1e-6f from 0x00565600) at 0x004bc76a) | `0xbc76c` FUN_004bc680 (mesh preparation: adjacency / clean / optimise; calls ID3DXMesh::GenerateAdjacency(1e-6f from 0x00565600) at 0x004bc76a) | 35 |
| 0 | `0x11204f` FUN_00511f77 (CRT fread body (from _fread_s, uses __VEC_memcpy)) | `0xe8da9` FUN_004e8880 (resource read) | 34 |

**Candidates**

Hooked calls account for 4.765 s of the 8.400 s interval (56.7 %); 3.635 s is unexplained by any hook. The sampler recorded 74,080 samples over 10.002 s of report coverage (7406 samples/s, 231 ticks/s per thread); the dominant leaf module is ntdll at 90.7 %. 1 of 2 delta blocks straddle the interval boundary and carry samples from outside it. Function `0x004e8e10` (FUN_004e8e10) holds 0.0 % of the engine thread's samples as leaf and 24.3 % as first main-module frame; it is a known routine (resource load = open + read + close). Function `0x0044c620` (FUN_0044c620) holds 0.0 % of the engine thread's samples as leaf and 12.1 % as first main-module frame; it is a known routine (orientation/rotation math (fsin/fcos/fpatan, helper 0x0052b5d0; from 0x0042fb20)). Function `0x004e1220` (FUN_004e1220) holds 0.0 % of the engine thread's samples as leaf and 6.3 % as first main-module frame; it is a known routine (collision tree: recursive split body (self-recursive, 2,267 B) - top loading hot spot).

## Limits

- Hooked seconds are completion deltas of report windows overlapping the interval; only the exclusive column may be added and it is a lower bound.
- Profiler delta blocks are attributed by overlap of their report interval; a block straddling a boundary counts in both neighbours and is reported as straddling.
- Per-block leaf/frame/pair tables are truncated to the top 48/48/32 rows, so function sums are lower bounds; per-thread module splits and sample totals are exact.
- Frame RVAs are return addresses; inclusive-by-frame shares attribute DLL and wait time to the first main-executable frame, not to a full call stack.
- Gap labels are heuristics over hooked counts (label_gap); no phase marker exists in the log.
- Function labels come from hand-maintained notes; an unlabelled function is merely undocumented, not unimportant.
