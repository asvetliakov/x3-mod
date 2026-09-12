# The archive reader 0x004e8880 and the fast resource reader

Decompiled on 2026-09-12 (`X3LoadingOrchestration.java decompile 004e9210 004e9360
004e90f0 004e6fa0 004e6f20 004e8780 004e73f0 004e8880 …`, listings and callers;
output kept under `/tmp/x3-probe-study`, plus the earlier `/tmp/x3-script-study`
disassembly of `0x004e8880`, 483 instructions). Preferred VAs, image base
`0x00400000`, X3AP.exe SHA-256 `fdbf3418…`. This is the contract that
`src/proxy/resource_reader_core.cpp` implements and
[docs/verification/resource-reader.md](../verification/resource-reader.md) verifies.

## The file object

Shared by open (`0x004e8780`), read (`0x004e8880`), the dispatcher (`0x004e9210`),
the one-byte reader (`0x004e91b0`), close (`0x004e9360`) and the destructor
(`0x004e73f0`); passed in `EAX` (read, one-byte reader), `ESI` (open,
dispatcher) or `ECX` (close).

| Offset | Field | Notes |
| --- | --- | --- |
| `+0x00` | `FILE*` or `gzFile` | the game's static VC8 CRT stream, or zlib's handle in mode 5 |
| `+0x04` | flags | `1` open, `2` catalogue-resident, `4` gz handle (`0x004e90f0` sets `|5`), `0x10` progress callback (`0x004e9530(bytes)` after plain and gz reads, not catalogue reads) |
| `+0x08` | record offset | catalogue mode: byte offset of the record inside the `.dat` |
| `+0x0c` | record length | catalogue mode |
| `+0x10` | record cursor | catalogue mode: bytes consumed; the dispatcher clamps every request to `length − cursor` and advances it; the seek sites of `0x004e8880` set it to the clamped target |
| `+0x18`, `+0x34`, `+0x58` | three `std::string`s | freed by `0x004e73f0` |
| `+0x4c` | catalogue index | set by the resolver |

## Call contract of 0x004e8880

`EAX` = file object in, `EAX` = buffer (or 0) out, plain `ret`, EBX/EBP/ESI/EDI
preserved, ECX/EDX clobbered. Two callers: `0x004e8ead` in the resource load
`0x004e8e10` and `0x004f7a71` in the font loader `0x004f7830`. Side effects on
success: `DAT_00596988 = DAT_0059698c = size`; `DAT_006085f4`, `DAT_006085f8`,
`_DAT_006089f8` `+= size`; `DAT_006089fc += 1` (allocation counters). The buffer
comes from `_malloc` (`0x005112c4`), retried once after `0x004b8b60` (the
out-of-memory purge callback) and fatal through `printf(PTR_s_Out_of_memory…)`.

Sequence (numbers are the dispatcher's arguments `ECX` = element size,
`EAX` = count, stack = buffer):

1. `read(magic, 1, 3)`; not 3 → return 0 (no allocation).
2. If `magic != 1f 8b`: `key = magic[0] ^ 0xC8`; if `magic[1]^key, magic[2]^key ==
   1f 8b` the stream is **scrambled** (a `.pck`: byte 0 is the key byte, every
   later byte is XORed with `key`), else **plain copy**.
3. Plain copy: catalogue → `cursor = length`, `fseek(offset+length)`, size =
   `length`; loose → `fseek(0, END)`, `_ftell`; gz handle → `gzseek`/`gztell`.
   Then `DAT_00596988 = size`, `_malloc(size)`, counters, `_memset(0, size)`,
   seek back to the start (catalogue `cursor = 0`), one `read(buf, 1, size)`;
   size mismatch → `_free`, counters `DAT_006085f4/8 −= size`, return 0.
4. Gzip: seek to `length − 8` (catalogue: `cursor = length − 8`) or
   `fseek(−8, END)`; `read(crc, 1, 4)`, `read(isize, 1, 4)` (both XORed with
   `key` when scrambled); `_malloc(isize)`, counters, **`_memset(0, isize)`**;
   seek to 3 (scrambled) or 2 (plain gzip) = the CM byte; then one locked
   `fgetc` (`0x0050fff5`) per header byte through `0x004e91b0` (catalogue:
   `cursor++`, `^0x33`): CM must be 8, FLG must have no reserved bit, skip 6,
   FEXTRA (2 + XLEN), FNAME and FCOMMENT (to NUL), FHCRC (2). Failure → `_free`,
   `DAT_006085f4/8 −= isize`, return 0 (the size globals keep their old value).
5. `inflateInit2_(&z, −15, "1.2.3", 0x38)` (failure → as above); loop:
   `read(chunk, 1, 1024)`, XOR the chunk bytewise with `key`, `inflate(&z,
   Z_NO_FLUSH)` with `next_out = buf + produced`, `avail_out = isize − produced`,
   until `inflate` returns `Z_STREAM_END` or the read returns 0. **An inflate
   error does not fail the call**: the loop keeps reading until EOF and the
   partially filled, zeroed buffer is returned. `inflateEnd`,
   `DAT_00596988 = DAT_0059698c = isize`, return the buffer.

The dispatcher `0x004e9210` (`ret 4`): flags `&1 == 0` → 0; `&3 == 3` →
`n = min(size·count, length − cursor)`, `fread(buf, 1, n)`, `buf[i] ^= 0x33`
bytewise, `cursor += n`; `&5 == 5` → `gzread(size·count)` (+ progress callback);
else `fread(buf, size, count)` (+ progress callback). It never seeks: the stream
position is whatever the last operation left.

Open `0x004e8780` (`ret 4`, ESI = object, EAX = extension list, stack = flag):
resolver `0x004e7590` (`ret 0xc`), then loose → `_fopen(path, "rb")` at
`0x004e8856`, flags `|= 1`; catalogue → `0x004e6fa0` (second index search),
**`_fopen(<NN>.dat, "rb")` at `0x004e87ff`**, flags `|= 3`, offset/length from the
12-byte entry, `cursor = 0`, `_fseek(offset)`. Close `0x004e9360` (`ECX` =
object, 66 bytes): clears bit 0, `gzclose` for mode 5, else **`_fclose` at
`0x004e9392`**; six callers (`0x004e73f0`, `0x004e8f0b`, `0x004e87ad` re-open,
`0x00404a6f`, `0x004e90fd`, `0x004f7b0d`) and no other path closes the stream.

## What the fast reader does (`X3M_RESOURCE_READ=fast`)

Chained at the entry of `0x004e8880` (`engine_patch`, behind the probe stub when
both are on). `decode()` in `resource_reader_core.cpp` (no SSE, no x87):

* Accepts only `flags & 1`, not `& 5 == 5`, not `& 0x10`; `_ftell` must equal the
  record offset with `cursor == 0` (catalogue) or 0 (loose) — the state the open
  body leaves. Loose length by `_fseek(0, END)` + `_ftell`.
* One `fread` of the whole extent into a `HeapAlloc` scratch, `^0x33` word-wide
  for catalogue records, the magic test of step 2, `^key` word-wide over bytes
  1…; the gzip header walked in memory with the same rules; `isize` from the
  trailer; `_malloc(isize)` (no retry, no memset); one `inflateInit2_`/`inflate`
  (all input, `avail_out = isize`)/`inflateEnd`.
* Success requires `Z_STREAM_END` **and** `produced == isize`; then the three byte
  counters and the allocation count, both size globals, `cursor = length`
  (catalogue) exactly as step 5, and the buffer in `EAX`. The stream is left at
  the end of the extent, as the original leaves it.
* Any other outcome (unopened, gz handle, progress flag, tell failure, state,
  short extent, scratch, short read, not gzip, method, reserved bits, header
  overrun, `isize == 0`, allocation, init, inflate status, size mismatch):
  `_free` what was allocated, `_fseek` back to the entry position (the cursor was
  never touched) and continue into the original body, which reproduces its own
  behaviour (including the partial-buffer return on inflate errors). Fallback
  reasons are counted in `resource_reader_metric`.
* `verify` mode decodes into a scratch buffer, restores the stream, runs the
  original through the chain and compares bytes, `DAT_00596988/c`, the four
  counter deltas and the cursor; unequal files are logged (≤ 64 lines) and
  counted; the caller always receives the original's buffer.

## The catalogue `.dat` handle pool (`X3M_DAT_HANDLES=1`)

The open/close pair **is** cleanly interceptable: both are plain `E8` call
sites, the `.dat` `_fopen` is the only `_fopen` of the catalogue branch and
`_fclose` in `0x004e9360` is the only closer of every file object. The pool
(`x3m_pool_fopen`/`x3m_pool_fclose`, cdecl, same signatures) replaces those two
calls: a close keeps the `FILE*` (state "kept") unless `FILE::_flag & _IOERR`
(offset `0x0c`, VC8 layout, checked so a stream in error is closed for real); an
open of the same path in `"rb"` hands a kept handle back, and the original's
`_fseek(offset)` positions it as a fresh open would (MSVC `fseek` clears EOF and
the buffer). Handles are never shared between two open objects (nested opens of
one `.dat` each get their own), the table holds 32 entries, longer paths or a
full table fall through to real `_fopen`/`_fclose`, and `pool_drain` closes the
kept handles at shutdown. Not affected: loose files (never pooled) and gz
handles (`gzclose`). Statistics: `dat_handle_pool_metric opens= reused=
real_opens= closes= kept= real_closes= errors= full= held=`.
