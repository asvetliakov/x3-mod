# Savegame gz stream: the read dispatcher and its zlib callers

Facts from a read-only Ghidra pass over `X3AP.exe` (2026-09-12, project
`/tmp/x3-ghidra-research`, `X3DecompileFunctions.java`; decompiler text stays
untracked under `/tmp/x3-gz-study/`). They ground the read-ahead buffer of
[docs/verification/gz-buffer.md](../verification/gz-buffer.md) and extend the
loading-orchestration notes. The bundled `zlib1.dll` is zlib 1.2.3
(`zlibVersion()`; SHA-256
`27b95a87be89090df67f5f1e7fc88437da400b7c3b6728418d97eca71882cad5`, identical in
the `X3` and `Steam` bottles); the `gzio.c` semantics that follow from that are
listed in the verification note.

## The stream object

Every function below takes the same object in a register (`this` in `EAX`/`ESI`):

| offset | meaning |
|---|---|
| `+0` | handle: `FILE*` or the `gzFile` from `gzopen` |
| `+4` | flags: bit 0 open; bits 0+1 (`3`) CAT-slice mode (an XOR-0x33 slice of a `.cat`/`.dat` file); bits 0+2 (`5`) gz mode; bit 4 progress accounting (`0x4e9530` receives the byte count) |
| `+8` | slice base offset (CAT mode) |
| `+12` | slice size (CAT mode) |
| `+16` | slice position (CAT mode) |

Only the mode bits decide which library the dispatchers call; there is no
separate savegame reader class. A handle is opened with exactly one mode and
the object holds one handle at a time (the gz open wrapper closes any open
handle first), so **no handle is ever both written and read**.

## Functions

* **`0x004e9210` read dispatcher** (`gzread` thunk `0x4fae40`, IAT `0x5323e4`;
  35 callers, among them `0x4e8880`, `0x4a0880`, `0x4e9420`, `0x4e9470`,
  `0x4e93f0` and the savegame field readers). Arguments are fread-like
  `(element size, count, buffer)`. Gz mode issues **one** `gzread(handle,
  buffer, size * count)` and returns its result; there is no loop inside the
  dispatcher, so the 3.11-byte average of the profile
  ([loading-profile-bottle-x3.md](loading-profile-bottle-x3.md)) is the callers'
  request size, not a chunking artefact: the savegame readers pull one field
  at a time. CAT mode does an `fread` clamped to the slice, XORs every byte
  with `0x33` and advances `+16`; plain mode is `fread`. Bit 4 forwards the
  byte count to `0x4e9530`.
* **`0x004e90f0` gz open wrapper** (`gzopen` thunk `0x4fae64`, IAT `0x5323d8`).
  Closes an open handle through `0x4e9360`, clears bits 0 and 4, resolves the
  path with `0x4d2890`, calls `gzopen(path, mode)` with the **caller's mode
  string**, frees the path and sets flags `|= 5`. The six callers pass one of
  two literals: `"wb"` at `0x555838` (`0x404530`, `0x472610`, `0x4ab880`: the
  savegame writers) and `"rb"` at `0x55585c` (`0x475b10`, `0x405280`,
  `0x404cc0`: the savegame readers). No other mode string reaches `gzopen`;
  the compression level is zlib's default.
* **`0x004e8880` archive member reader** (`gzseek` thunk `0x4fae2e`, `gztell`
  thunk `0x4fae58`; callers `0x4f7830` font loader and `0x4e8e10` resource
  load). Reads 3 header bytes through `0x4e9210`, tests for the gzip magic
  `1f 8b` directly and after XOR with `byte0 ^ 0xc8` (obfuscated members).
  Non-gzip data: seek to the end (`gzseek(h, 0, SEEK_END)` in gz mode),
  size = tell (`gztell`), seek to the start (`gzseek(h, 0, SEEK_SET)`), one
  full read. Gzip data: `gzseek(h, -8, SEEK_END)`, read 4 (CRC) + 4 (ISIZE),
  allocate ISIZE, `gzseek(h, 2 or 3, SEEK_SET)` (backward, to just after the
  magic), then `0x4e91b0` (gzgetc) for the method/flags and the optional
  extra/name/comment/header-CRC fields, `inflateInit2(-15)` and a loop of
  `0x4e9210` reads into a 1028-byte local (1 KiB per `inflate` call, as in
  [loading-orchestration.md](loading-orchestration.md)) until `Z_STREAM_END`.
  So the seeks are **absolute (`SEEK_SET`) and end-relative (`SEEK_END`)**,
  never `SEEK_CUR`; the `SEEK_SET` targets are backward from the 3-byte read
  position; and every `gztell` result is used as a size (allocation length and
  read length), not as a position to seek back to. In gz mode the `SEEK_END`
  seeks are refused by zlib 1.2.3 (`-1`, no movement); in practice the
  callers reach this function with CAT-slice or plain handles (the fixture
  and mod files), so the seek/tell/getc pattern is not on the savegame path.
* **`0x004e91b0` getc dispatcher** (`gzgetc` thunk `0x4fae3a`, IAT
  `0x5323f4`; only caller `0x4e8880`). Gz mode: `gzgetc`. CAT mode: `fgetc`
  XOR `0x33`, bounded by the slice size (returns `-1` past it). Plain:
  `fgetc`. Returns `-1` when the object is not open.
* **`0x004e9360` close dispatcher** (`gzclose` thunk `0x4fae52`, IAT
  `0x5323f0`; callers `0x404530`, `0x4f7830`, `0x4e90f0`, `0x4e8780`,
  `0x4e73f0`, `0x4e8ed0`). Clears bit 0, `gzclose` in gz mode else `fclose`,
  returns success as a bool.
* **`0x004e92e0` write dispatcher** (`gzwrite` thunk `0x4fae46`, IAT
  `0x5323e0`; about 55 callers). Refuses CAT mode, forwards `size * count`
  to `0x4e9530` under bit 4, then `gzwrite(handle, buffer, size * count)` in
  gz mode else `fwrite`.

## Call graph of the savegame path

```
savegame reader (0x475b10 | 0x405280 | 0x404cc0)
  -> 0x4e90f0  gzopen(path, "rb")
  -> 0x4e9210  gzread(h, field, size*count)     x 13.9 M per load, 3.11 B average
  -> 0x4e9360  gzclose(h)
savegame writer (0x404530 | 0x472610 | 0x4ab880)
  -> 0x4e90f0  gzopen(path, "wb")
  -> 0x4e92e0  gzwrite(h, field, size*count)
  -> 0x4e9360  gzclose(h)
archive member reader 0x4e8880 (fonts 0x4f7830, resources 0x4e8e10)
  -> 0x4e9210 / 0x4e91b0 / gzseek / gztell on CAT-slice or plain handles
```

Consequences for the buffer: the savegame handle sees only `gzread` (all
sizes small, occasionally a block) between open and close; `gzgetc`, `gztell`
and `gzseek` must stay exact because the dispatchers route every mode through
the same code, but they are not on the hot path. The IAT slots and thunks
above are the complete set of gz imports of the main module (`gzeof`,
`gzrewind`, `gzungetc` are not imported; `gzrewind` is exported by the DLL and
the buffer resolves it with `GetProcAddress` for one error-path case).
