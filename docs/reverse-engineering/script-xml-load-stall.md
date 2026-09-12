# The script/XML load stall: per-file resource path

Scope: the 25.0 s stall at 62.87–87.85 s of the new-bottle savegame load
("stall B" of [loading-profile-bottle-x3.md](loading-profile-bottle-x3.md) §3, the
same phase as run 1's gap-3 stall 3, 193.18–205.89 s, 12.71 s) in which only 3.56 s
is inside hooked APIs. This document decompiles the per-file path, identifies the
CRT functions the sampler names, and states what the next diagnostic run must
measure. Read-only study; decompiler output stayed in `/tmp/x3-script-study`
(untracked). Addresses are preferred VAs, image base `0x00400000`.

Two premises of the earlier note are corrected here:

* The phase is **not only** script XML. It is a mixed asset phase: ~790 script XML
  documents **plus** 20 large textures (23.8 MB through
  `D3DXCreateTextureFromFileInMemoryEx`) and 154 meshes (`GenerateAdjacency` /
  `D3DXCleanMesh` / `OptimizeInplace`). In run 1 — the last run with usable leaf
  PCs — 39 % of the engine thread's leaf samples in this interval were inside
  `d3dx9_37.dll`, and the highest single X3AP frame share was the texture body
  `0x004dd2c0` at 22.5 %, not anything on the script path.
* `0x00517d8f` is not fread's lock. It is the CRT's generic `_unlock`, and the
  caller the sampler records (`0x00524cfa`) belongs to **`__alloc_osfhnd`** — the
  CRT **file-open** path. See §4.

## 1. Call graph

```
0x004ab880  script VM / command dispatch          ─┐
0x004dd2c0  texture body (jpg/tga, D3DX*InMemory)  │ 11 callers total
0x004dc540  texture loader (pck dds)               ├─> 0x004e8e10  resource load
0x0049d030 / 0x004bae10 / 0x004d7750 / …          ─┘                (192 bytes, SEH)
                                                          │
      ┌───────────────────────────────────────────────────┼──────────────────────┐
      v                                                   v                      v
0x004e8780  resource open                        0x004e8880  resource read   0x004e73f0 close
      │                                                   │                      │
      │ 0x004e7590  logical-name resolution               │ 0x004e9210 read      │ 0x004e9360
      │    ├ 0x004d2950  _findfirst64i32 wrapper          │   ├ 0x00512213 fread │   └ _fclose
      │    │    └ __findfirst64i32 -> FindFirstFileA       │   │    └ 0x0051217d _fread_s
      │    ├ __findnext64i32 -> FindNextFileA              │   │         ├ 0x00511d5e _lock_file
      │    ├ 0x004e7470  extension rank (__stricmp)        │   │         ├ 0x00511f77 _fread_nolock
      │    ├ _bsearch over the catalogue directory         │   │         │    ├ 0x0051c6b7 _filbuf
      │    └ 0x004e9590  _findclose + free(0x138)          │   │         │    │    └ __getbuf -> _malloc(4096)
      │                                                    │   │         │    ├ _memcpy_s (__VEC_memcpy)
      ├ 0x00512a18 _fopen(path,"rb")                       │   │         │    └ ___lock_fhandle 0x00524aa7
      │    └ __fsopen -> 0x00527869 __sopen_helper          │   │         └ 0x00512209 -> _unlock_file
      │         ├ CreateFileA (up to 3 attempts)           │   │              0x00511dfe / 0x00511dc8
      │         ├ GetFileType                              │   │                   └ 0x00517d8f _unlock
      │         ├ 0x00524b69 __alloc_osfhnd                │   ├ gzread (mode 5 only)
      │         │    ├ __calloc_crt                        │   └ byte-wise XOR 0x33 at 0x004e9260 (mode 3)
      │         │    └ 0x00524cfa -> 0x00517d8f _unlock    │
      │         └ 0x0051f464 _read text-mode body          ├ 0x004e91b0 one-byte read
      └ _fseek to the catalogue record offset (mode 3)     │    └ 0x0050fff5 CRT fgetc (locked) / gzgetc
                                                           ├ inflateInit2_(-15,"1.2.3",0x38) / inflate / inflateEnd
                                                           └ _malloc(uncompressed size) + _memset(0, size)
0x004e6b70  pck xml resource: xmlCheckVersion(0x508f), xmlSetGenericErrorFunc, strlen, xmlReadMemory
0x004cabc0  CryptoAPI signature verify  <- 0x004ab880+0x3c14 (0x004af494), 0x004cae90 (x2)
```

`0x004e8e10` itself is trivial: 192 bytes, SEH frame, `0x004e8780` (open) →
`0x004e8880` (read whole) → `0x004e73f0` (close), returning the `_malloc`'d buffer.
Its 10.7 % share in run 6 and the 22.5 % of `0x004dd2c0` are **stack-frame** shares,
not leaf time — on the X3/FEX bottle the leaf PC is a constant
(loading-profile-bottle-x3.md §5), so every share below is a ranking hint.

### The file object

`0x004e8780`/`0x004e8880`/`0x004e9210` share a small object in `ESI`/`EAX`:

| Offset | Meaning |
| --- | --- |
| `+0x00` | `FILE*` (modes 1/3) or gz handle (mode 5) |
| `+0x04` | flags: `1` open, `2` catalogue-resident, `3` catalogue-resident-and-open, `5` gz-open, `0x10` progress callback |
| `+0x08` | catalogue record offset in the `.dat` |
| `+0x0c` | catalogue record length |
| `+0x10` | bytes consumed inside the record |
| `+0x18`/`+0x34`/`+0x58` | three `std::string`s (path, name, extension), freed by `0x004e73f0` |

`0x004e9210` is `read(void* buf)` with **`ECX` = element size, `EAX` = count**
(`0x004e8895`: `LEA EAX,[EBP+3]` / `LEA ECX,[EBP+1]` for the 3-byte magic probe).

## 2. What happens per file (answer 1)

Decompiled from `0x004e8880` (`/tmp/x3-script-study/e8880.dis`, 483 instructions).
There is **no realloc growth buffer and no ftell scanning loop**: the size is known
up front, so the design is single-pass with one allocation. Order of calls:

1. `fread(buf, 1, 3, f)` — 3-byte magic probe. On a fresh stream this is the call
   that runs `_filbuf` → `__getbuf` → **`_malloc(4096)`** for the CRT stream buffer
   and the first `_read`/`ReadFile`.
2. If the magic is not `1f 8b`, the first byte is taken as `magic ^ 0xC8` and bytes
   2–3 are unscrambled with it; if that yields `1f 8b` the whole stream is
   XOR-scrambled (the `.pck` case, `bVar1 = true`).
3. **Not gz** (plain file or catalogue record): `fseek` to the end / `ftell`
   (or `+0x0c` for a catalogue record), `_malloc(size)`, `_memset(0, size)`,
   `fseek` back, and **one `fread` of the whole file**. Requests ≥ the 4096 stream
   buffer go straight to `_read`, so this path is efficient.
4. **gz** (`.pck`): `fseek(-8, SEEK_END)`, two 4-byte `fread`s for the gzip CRC and
   ISIZE trailer → uncompressed size; `_malloc(size)`; **`_memset(0, size)`**;
   `fseek` to offset 2 or 3; then the gzip header is walked **one byte at a time**
   through `0x004e91b0` → `0x0050fff5` (a full *locked* CRT `fgetc`: `_lock_file`,
   `__fileno` ×5, `_filbuf`, `_unlock_file`): 2 + 6 fixed bytes, plus 2 + N for
   FEXTRA, plus a NUL-terminated scan each for FNAME and FCOMMENT — 10 to ~40
   locked `fgetc` per file.
5. `inflateInit2_(&z, -15, "1.2.3", 0x38)`, then the decompression loop at
   `0x004e8d50`–`0x004e8db7`:

   ```
   004e8d50  LEA ECX,[ESP + 0x60]      ; 1028-byte stack buffer
   004e8d55  MOV EAX,0x400             ; count = 1024
   004e8d5a  MOV ECX,0x1               ; size  = 1
   004e8d5f  CALL 0x004e9210           ; fread(buf, 1, 1024, f)
   004e8d75  XOR byte ptr [ESP + ECX*1 + 0x60],BL   ; per-byte unscramble, 1024 iterations
   004e8da4  CALL inflate(&z, Z_NO_FLUSH)
   004e8db7  JNZ 0x004e8d50            ; until inflate returns Z_STREAM_END
   ```

   So **one `fread` of exactly 1024 bytes, one 1024-iteration byte-XOR loop and one
   `inflate` per kibibyte of compressed input**, with `avail_out` always the whole
   remaining output buffer.
6. `inflateEnd`; the caller `0x004e6b70` calls `xmlCheckVersion` +
   `xmlSetGenericErrorFunc` + `strlen` + `xmlReadMemory` on the buffer.
7. `0x004e73f0` → `0x004e9360` → `_fclose` (frees the 4096 stream buffer) and frees
   the three `std::string`s; the caller `_free`s the payload and adjusts the two
   allocation counters `DAT_006085f4` / `DAT_006085f8`.

### Which files, and how the counts reconcile

Measured locally from the install (`X3/`, unmodified):

| Directory | Files | Compressed | gz-valid | Decompressed | Avg decompressed |
| --- | ---: | ---: | ---: | ---: | ---: |
| `addon\scripts` | 698 | 2,137,417 | 697 | **22,238,366** | 31,905 |
| `scripts` | 531 | 1,190,782 | 530 | 11,371,482 | 21,455 |
| `addon\t` | 11 | 28,982 | 11 | 83,974 | 7,634 |
| `t` | 11 | 27,463 | 11 | 80,503 | 7,318 |

The stall's hooked `xmlReadMemory` total is **790 calls / 22,531,669 bytes**. That
is `addon\scripts` (697 files / 22,238,366 B) **plus 93 calls / 293,303 B**
(avg 3,154 B — the `t` language files and a few smaller documents). The conclusion
is exact to within the language files: **the 790 XML documents are the loose
`addon\scripts\*.pck` files**, gz-compressed 10.4× and XOR-scrambled, opened as
loose files (their name probe *succeeds*); the base `scripts\` folder is shadowed by
the addon folder and is not read in this phase. `director`, `types` and `mods` do
not exist as loose directories in this install — those names resolve into the
catalogues.

The 1,376 `CreateFileA` therefore split roughly as **~790 loose `.pck` script/language
opens + ~586 catalogue-resolved opens**, and the catalogue ones are
`_fopen(<NN>.dat, "rb")` **reopened per resource** by `0x004e8780` and `_fclose`d by
`0x004e73f0` — the same handful of `.dat` files opened hundreds of times.

The 13,292 `inflate` calls are consistent with 1 KiB chunks: ~3,200 for the 2.1 MB
of compressed script text and ~10,000 for the 20 textures (23.8 MB decompressed)
read through the same loop. **Per-file cost drivers**, in the order the evidence
supports them:

1. **Opening files.** 1,376 `CreateFileA` at 1.43 ms (run 6) / 2.94 ms (run 1) =
   1.97 s / 4.04 s hooked, plus the guest-side CRT open body
   (`__sopen_helper` 0x00527869: up to 3 `CreateFileA`, `GetFileType`,
   `__alloc_osfhnd` with its handle-table lock, two `_read` text-mode probes) and
   `_malloc(4096)`/`free` for the stream buffer. This is where the sampler's
   `_unlock`, `_malloc` and `__sopen_helper` rows come from (§4).
2. **1,398 name probes** at 0.486 ms / 0.668 ms = 0.68 s / 0.93 s hooked, plus
   `0x004e7590`'s own churn: a 10-element `std::string` vector constructed and
   destructed per resolve (`eh_vector_constructor_iterator(buf, 0x1c, 10, …)`),
   ~30 `std::string` construct/destruct calls, 11 `_free`, a `-L%03d` language
   suffix `sprintf`, and `_malloc(0x138)`+`sprintf("%s\\%s")` inside `0x004e8880`'s
   findfirst wrapper `0x004d2950`.
3. **`_memset` of the whole output buffer before `inflate` fills it** — 22.5 MB of
   pointless zeroing for the scripts alone in this phase, ~46 MB counting the
   textures. Corroborated by `__VEC_memzero` holding 5.2 % of the engine thread's
   frames in run 1's interval.
4. **1 KiB granularity**: 13,292 × (`fread` with `_lock_file`/`memcpy_s`/`_unlock_file`
   + a 1024-iteration byte-XOR loop + one `inflate` call). Hooked `inflate` is only
   0.168 s; the guest-side half is unmeasured.
5. **10–40 locked `fgetc`** per file for the gzip header.

## 3. The archive reader `0x004e8880` and `0x004e9210`

`0x004e9210` dispatches on the flags at `+0x04`:

* `& 3 == 3` (catalogue-resident): clamp the request to the record length, then
  `fread(buf, 1, n, f)` via `0x00512213`, then **`XOR byte ptr [ECX+EBX],0x33` one
  byte at a time** over the whole result (`0x004e9260`). For catalogue-resident
  plain text this is a scalar pass over every byte delivered.
* `& 5 == 5`: `gzread(handle, buf, size*count)`. This is the path the existing
  `X3M_GZ_BUFFER` read-ahead covers.
* otherwise: plain `fread`.

**The `.pck` script path uses none of the gz\* imports.** It calls
`inflateInit2_`/`inflate`/`inflateEnd` directly on its own `fread`-fed 1 KiB buffer,
so `gz_buffer` cannot affect this stall, and the stall's 87,746 `gzread` calls
(359,166 bytes, 4.09 B/call) come from other consumers, not from the script files.

## 4. The CRT: static link, and who owns `0x00517d8f` (answer 2)

**The MSVC CRT is statically linked.** `X3AP.exe` imports exactly
`DINPUT8, DSOUND, WINMM, COMCTL32, IMGDLL, zlib1, VERSION, libxml2, XINPUT1_3,
KERNEL32, USER32, GDI32, ADVAPI32, SHELL32, ole32, OLEAUT32, d3d9, d3dx9_37,
dbghelp` — **no `msvcr*.dll`, no `msvcp*.dll`**. `fopen`/`fread`/`fseek`/`fgetc`/
`malloc`/`free` are bodies inside the image, so IAT hooking cannot see them; only
an inline/trampoline patch can. The CRT is VC8/VC9 vintage: `_fread_s`, `__SEH_prolog4`,
20 `_iob` entries of 0x20 bytes at `0x00573200`, lock table at `0x00573d18` stride 8.

Resolved CRT addresses used below: `_fopen 0x00512a18`, `__sopen_helper 0x00527869`,
`_fread_s 0x0051217d`, `fread 0x00512213`, `_fread_nolock 0x00511f77`,
`fgetc 0x0050fff5`, `_filbuf 0x0051c6b7`, `_fseek 0x00510343`, `_ftell 0x005128e6`,
`_malloc 0x005112c4`, `_free 0x0050e1b0`, `_realloc 0x0051142a`,
`_lock_file 0x00511d5e`, `_unlock_file 0x00511dfe` / `0x00511dc8`,
`___lock_fhandle 0x00524aa7`, `__alloc_osfhnd 0x00524b69`.

`0x00517d8f` is 21 bytes and is the CRT's generic **`_unlock(int locknum)`**:

```
push ebp / mov ebp,esp
mov  eax,[ebp+8]                     ; lock index
push dword ptr [0x00573d18 + eax*8]  ; _locktable[locknum].lock
call dword ptr [0x005321fc]          ; LeaveCriticalSection
pop  ebp / ret
```

It has 31 callers, all 9-to-54-byte `__finally` funclets that push a constant index.
The ones that matter here:

| Funclet | Index | Owning function |
| --- | ---: | --- |
| `0x00511dfe` / `0x00511dc8` | `0x10 + (FILE - 0x00573200)/0x20` | **`_unlock_file`** (stream lock; falls back to the `FILE`'s own `CRITICAL_SECTION` at `+0x20` for streams past the static 20) |
| `0x00524b3e` | 10 | **`___lock_fhandle`** (per-fd lowio lock) |
| `0x00524cfa` | 11 | **`__alloc_osfhnd`** (OS-handle-table lock) |
| `0x005112bb`, `0x0050e206`, `0x00511421`, `0x0051156e` | 4 | funclets adjacent to `_malloc` / `_free` / `_realloc` — the heap lock |

So `0x00517d8f` is shared between the stream lock, the lowio handle locks and the
heap lock; **the caller the sampler actually recorded is `0x00524cfa`**, i.e.
`__alloc_osfhnd` — file **open**, not `fread` and not the allocator. Run 1's pair
table for the same interval says the same thing and adds the parent:
`0x00517d8f ← 0x00524cfa` 177 samples (5.5 %) and
`0x00527869 ← __sopen_helper` 115 samples (3.6 %), alongside
`0x00511f77 ← _fread_s` 200 samples (6.4 %). Read together with 1,376 opens at
1.4–2.9 ms each, the per-open path — not the per-byte read path — is the dominant
identified cost in this stall. `_malloc`'s share has two feeders per file:
`__getbuf`'s 4096-byte stream buffer and `0x004e8880`'s payload allocation.

## 5. The 1,398 failed name probes (answer 3)

Every `FindFirstFileA` in the game funnels through one chokepoint:
`__findfirst64i32` (`0x005117fa`) has exactly one caller, the wrapper `0x004d2950`,
which has 8 callers. In this phase two matter: the resolver `0x004e7590`
(`0x004e7942`), called once per open from `0x004e8780` — hence 1,398 probes for
1,376 opens — and the script VM's own directory enumeration (`0x004ade9c`,
`0x004adf24`), which is how the script folder is listed in the first place. The
other callers are startup-only (`0x004ce080` mod enumeration, `0x004869e0` dev-flag
dump, `0x004978a0`, `0x004f3b10`, `0x004cf2c1`).

`0x004e7590` (4,538 bytes) works as follows:

1. Split the caller's space-separated extension-preference list into a fixed
   10-`std::string` vector (`local_17c`, stride `0x1c`); `local_260` = count.
2. Append the language suffix `-L%03d` from `*(DAT_00606f34 + 0x76c)`.
3. `0x004d2950`: `_malloc(0x138)`, `sprintf("%s\\%s", dir, pattern)`,
   **`__findfirst64i32` → `FindFirstFileA`**; then loop `__findnext64i32` over the
   matches, ranking each match's extension with `0x004e7470` (a linear `__stricmp`
   scan over the 10 strings) and keeping the lowest index. `0x004e9590` releases the
   handle (`_findclose` + `free(0x138)`).
4. If `FindFirstFileA` returned `INVALID_HANDLE_VALUE`, fall through to the
   catalogues: walk the mounted catalogue array at `*(DAT_00606f34 + 0xc0)` from the
   **highest index down** (count at `+0xc8`) and `_bsearch` the sorted directory
   (0xc-byte records, comparator `0x004e6ee0`) for the upper-cased name. On a hit the
   flags become `| 2` (catalogue-resident) and `+0x4c` records the catalogue index.

So a probe is a **wildcard directory enumeration used as an existence test with
extension preference**, and a failure means "no loose file of this name" → use the
catalogue. In this stall ~56 % fail (the ~790 loose `addon\scripts` files succeed);
over the whole run 3,851 of 4,611 fail (84 %), including one 1.76 s call.

**Is a negative cache safe?** Keyed on the exact `"%s\\%s"` pattern, yes, *with
invalidation*, because we can observe every creation the game makes: the game's
writes also go through `CreateFileA` (static-CRT `fopen("w")` → `__sopen_helper` →
`CreateFileA`), which we already hook, as do `CreateDirectoryA`, `DeleteFileA`,
`MoveFileA*`. The game does create files in directories this resolver later reads:
savegames (`.sav`), `log0xxxx.txt` in the game directory (the script command
`write to file`, read back by scripts through this same resolver), screenshots and
the in-game script editor's output. A blanket cache is therefore unsafe; a cache
that (a) stores only negative results, (b) is keyed on the full pattern, and
(c) drops every entry whose directory prefix is touched by a hooked create/delete/
rename or by a `CreateFileA` with write access is safe for this process. Value is
bounded and second-order: **0.68 s inside this stall**, 2.56 s + 0.07 s
(`FindFirstFileA` + `FindNextFileA`) over the whole run.

## 6. Recommendation (answer 4)

The four options, judged against the evidence above:

| Option | Verdict |
| --- | --- |
| **(a)** inline hook on the static-CRT `fread`/`fgetc` buffer size | **No.** IAT hooking cannot reach them (§4) and the payload `fread` already bypasses the 4096 buffer for requests ≥ bufsiz. The only buffer-size win left is the per-open `_malloc(4096)`/`free`, which is better removed by opening fewer files. |
| **(b)** hook `0x004e8880` for larger inflate chunks | **Yes, as part of a trampoline** — but note it mostly helps the 20 textures (~10,000 of the 13,292 chunks), not the scripts (~3,200). A byte patch cannot do it: raising the count at `0x004e8d55` overflows the 1,028-byte stack buffer, and the XOR loop at `0x004e8d75` is 4 bytes with no room for an absolute address. |
| **(c)** negative name-probe cache | **Yes, cheap and safe with write-invalidation**, but only 0.68 s of this stall / 2.6 s of the run. Do it for the whole-run win, not for this stall. |
| **(d)** something else | **The primary recommendation:** cut the *number of opens*, which is what the sampler's `_unlock ← __alloc_osfhnd`, `__sopen_helper` and `_malloc` rows point at, and stop the redundant `_memset`. |

Recommended order:

1. **Catalogue-`.dat` handle cache** (trampolines on `0x004e8780` and
   `0x004e73f0`/`0x004e9360`): keep one `FILE*` per `NN.dat` instead of
   `_fopen`/`_fclose` per resource. Evidence: `0x004e8780` re-`_fopen`s
   `*(char**)(catalogue[+0xc6] + 0xc)` on every catalogue-resident read, ~586 of the
   1,376 opens in this stall are catalogue-resolved, and `CreateFileA` costs
   1.43 ms (run 6) / 2.94 ms (run 1). Bound: ~0.8 s in-stall hooked time plus the
   guest-side `__sopen_helper`/`__alloc_osfhnd`/`__getbuf` work behind each one.
   Risk: the game's own concurrent use of the same `.dat` — the object keeps its own
   record offset at `+0x08` and `fseek`s before every read, so a shared handle needs
   a `fseek` on every read (it already does one) or one handle per open.
2. **One trampoline replacing `0x004e8880`** that, for the gz/`.pck` case, reads the
   whole compressed extent with one `fread`, unscrambles it with a word-wide XOR,
   and inflates it in one `inflate` call into a buffer allocated with the game's
   `_malloc` (`0x005112c4`) **without the `_memset`**, then updates
   `DAT_006085f4`/`DAT_006085f8`/`_DAT_006089f8`/`DAT_006089fc` exactly as the
   original does and returns the buffer in `EAX`. This removes, per file: the
   full-size `memset`, the 10–40 locked `fgetc`, the trailer `fseek`/`fread` pair,
   and the 1 KiB `fread`/XOR/`inflate` loop. It is also the only place where the
   catalogue XOR-0x33 scalar loop (`0x004e9260`) can be replaced wholesale.
   Keep the fallback: on any deviation (bad magic, short read, `inflate` error) call
   the original body.
3. **Negative name-probe cache** with the invalidation rules in §5.
4. **Measure `0x004cabc0` before doing anything about it.** It is CryptoAPI
   signature verification — `CryptAcquireContextA("X2EgosoftCSPContainer",
   "Microsoft Base Cryptographic Provider v1.0")` **three times per call**,
   `CryptImportKey`, `CryptCreateHash(CALG_MD5 0x8003)`, `CryptHashData` over the
   script text, base64 decode `0x004c8420`, `CryptVerifySignatureA`,
   `CryptGetHashParam` ×2, `CryptReleaseContext` — called from the script VM
   (`0x004af494`) and twice from `0x004cae90`. It held 5.3 % of run 1's engine
   frames in this interval (171 samples; pair `0x004cabc0 ← 0x004ab880` 58). Named
   key containers are registry/keystore work in Wine and are completely invisible to
   the current hook set. `ADVAPI32.dll` is a normal IAT import, so this costs
   nothing to measure.

## 7. What the next diagnostic run must measure

New IAT hooks (all in existing imported DLLs, no inline patching needed):

| Import | DLL | Counters |
| --- | --- | --- |
| `CryptAcquireContextA`, `CryptReleaseContext`, `CryptImportKey`, `CryptCreateHash`, `CryptHashData`, `CryptVerifySignatureA`, `CryptGetHashParam`, `CryptDestroyHash`, `CryptDestroyKey` | ADVAPI32 | calls, inclusive s, mean ms, bytes hashed. **Highest-value new signal.** |
| `inflateInit2_`, `inflateEnd` | zlib1 | calls; pair with `inflate` to get streams-per-phase and chunks-per-stream (expect ~790 + ~20 streams and ~13,292 chunks) |
| `CreateDirectoryA`, `DeleteFileA`, `MoveFileA`, `MoveFileExA`, `SetEndOfFile`, `WriteFile` | KERNEL32 | calls + path, to prove the negative-cache invalidation set is complete and to confirm nothing is written into `addon\scripts`/`t` during a load |
| `GetFileType`, `CloseHandle`, `FindClose` | KERNEL32 | calls, mean ms — completes the per-open Wine cost next to `CreateFileA` |
| `xmlFreeDoc`, `xmlDocGetRootElement` (if exported and used) | libxml2 | calls — separates parse from DOM walk |

Inline (trampoline) probes, entry/exit timestamps + call counts only, no behaviour
change, all one-time patches at function entry:

| Address | Role | Counters wanted |
| --- | --- | --- |
| `0x004e8e10` | resource load | calls, inclusive ns, and the resolved name (first 64 chars) so the 1,376 opens can be bucketed by directory |
| `0x004e8780` | open | inclusive ns; flag `+0x04` after return (loose vs catalogue-resident) — settles the loose/catalogue split exactly |
| `0x004e7590` | name resolve | inclusive ns, probe hit/miss |
| `0x004d2950` | findfirst wrapper (the single `FindFirstFileA` chokepoint) | calls split by caller return address, so resolver probes and script-VM enumerations are counted apart |
| `0x004e8880` | read | inclusive ns, `DAT_00596988` (size) at exit, gz vs plain vs catalogue branch, chunk count |
| `0x004e9210` | read dispatcher | calls, bytes, branch taken (the XOR-0x33 pass volume) |
| `0x0050fff5` | CRT `fgetc` | calls only (cheap counter; expect 10–40 per gz file) |
| `0x00527869` | `__sopen_helper` | inclusive ns — the guest-side open cost minus `CreateFileA` |
| `0x004cabc0` | signature verify | calls, inclusive ns |
| `0x004dd2c0`, `0x004dc540`, `0x004bc680` | texture/mesh bodies | inclusive ns — needed to subtract the non-script half of the stall |

With those, the 24.98 s decomposes without relying on the sampler: opens
(`0x004e8780` incl.) + resolve (`0x004e7590` incl.) + read/inflate (`0x004e8880`
incl.) + XML parse (`xmlReadMemory`) + signature verify (`0x004cabc0` incl.) +
texture/mesh (`0x004dd2c0`/`0x004dc540`/`0x004bc680` incl.). Only then is it worth
choosing between items 1–4 of §6.

Because the leaf PC is unusable on the X3/FEX bottle, the trampoline counters above
are the measurement; do not spend another run on sampler tuning for this phase.
