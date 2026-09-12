# Fast resource reader and catalogue handle pool (X3M_RESOURCE_READ, X3M_DAT_HANDLES)

Contract and decompile: [docs/reverse-engineering/resource-reader.md](../reverse-engineering/resource-reader.md).
Code: `src/proxy/resource_reader_core.cpp` (decode, verify comparison, entry
handler, `.dat` pool; compiled without SSE/MMX, no floating point),
`src/proxy/resource_reader.cpp` (switches, executable and site verification,
stub generation, logging, reports), `src/proxy/engine_patch.cpp` (byte-verified
patches, arena, call-site redirection).

## Switches

| Switch | Effect |
| --- | --- |
| `X3M_RESOURCE_READ=native` (`--resource-read native`, default) | nothing patched |
| `verify` | our decode into a scratch buffer, stream restored, then the original through the chain; bytes, `DAT_00596988/c`, the four counter deltas and the record cursor compared; `resource_reader verify equal=0 …` per unequal file (≤ 64), `resource_reader_metric` summary; the caller always gets the original's buffer |
| `fast` | our decode returns; any deviation falls back to the original (18 counted reasons) |
| `X3M_DAT_HANDLES=1` (`--dat-handles`) | catalogue `.dat` handles kept between resources (independent of the mode) |

All of it is exact-executable only (`object_trace::executable_verified()`),
the site `0x004e8880` (`81 ec 54 04 00 00`), the five CRT callees and the two
pool call sites are byte-verified, and a failure of any check leaves the game
untouched with a `status=` in the session log (`resource_reader mode= installed=
status= site= chained_after_probe=`, `dat_handle_pool requested=1 installed=
open_status= close_status=`).

## Fixture (`run_resource_reader.py`, bottle X3, real `zlib1.dll` 1.2.3)

`verification/probe/resource_reader_fixture.cpp` links the production units and
uses the game's `zlib1.dll` (copied by the runner, SHA-256 `27b95a87…`) to
build the inputs with its own `gzwrite`, rewriting the header for the FNAME,
FEXTRA, FCOMMENT and FHCRC variants. Sources: eight scrambled `.pck` files
(700 B … 3 MB, keys `0x5a 0x00 0x91 0x33 0xc8 0x7e 0x10 0x4b`), two plain
`.gz`, one plain text; a catalogue `.dat` with every source as a record (XOR
0x33, two stray bytes between records). The reference decoder is the original
algorithm re-implemented from the disassembly (3-byte probe, trailer seek,
memset, byte-wise header `getc`, 1 KiB `fread` + byte XOR + inflate loop,
dispatcher clamp and cursor). Cases:

1. **fast vs reference**, loose and catalogue records (two passes in a shuffled
   order): bytes, size globals, the four counters, the cursor and the stream
   position; text sources fall back with `not_gzip`.
2. **fallbacks with restoration**: unopened, gz handle, progress flag,
   transparent file (and the reference still decodes it afterwards), position
   and cursor state, wrong CM, reserved FLG bits, short extent, invalid deflate
   block, wrong ISIZE, truncated file, empty payload, FNAME without NUL,
   allocation failure — each restores the stream and cursor exactly.
3. **probe machinery** on fixture sites (see [loading-probes.md](loading-probes.md)).
4. **verify mode through the generated stub** chained behind the probe stub on
   a site with the real prologue: every source and record compared equal
   (`verify_mismatched=0`); a tampered "original" (first byte flipped) is
   reported as a mismatch and still returned to the caller.
5. **fast mode through the stub**: the reference never runs for gzip sources;
   timing of the 3 MB file, fast core vs reference.
6. **`.dat` pool**: first open real, a concurrent open of the same path gets a
   distinct handle, close keeps, reopen reuses (and the reference reads the
   right record after the caller's `fseek`), write modes pass through, other
   paths pooled separately, an errored stream is closed for real, missing files
   return null, `pool_drain` closes everything, unknown handles close for real.

Results: `verification/results/bottle-X3/resource-reader-summary.json` /
`resource-reader-fixture.txt` — see the numbers section below (filled from the
recorded run). `verification/analysis/test_resource_reader.py` re-parses the
recorded summary.

## Recorded numbers (bottle X3, 2026-09-12)

`run_resource_reader.py` (X3M_FIXTURE_BOTTLE=X3, arm64 Wine + FEX, zlib 1.2.3):
**405 checks, 0 failures**.

* fast vs reference: 10 loose sources handled (the text file falls back with
  `not_gzip`), 20 catalogue records handled over two shuffled passes; bytes,
  size globals, counters, cursor and position equal in every case.
* fallbacks (reason): gz_handle, progress, transparent → `not_gzip`, position
  and cursor → `state`, method, reserved, length, invalid block → `inflate`,
  wrong ISIZE → `size`, truncated → `inflate`, empty, header, alloc — each with
  the stream and cursor restored (and the reference decoding correctly
  afterwards for the transparent and alloc cases).
* verify mode through the chained stubs: `calls=22 handled=20 fallbacks=2
  verify_files=20 verify_equal=20 verify_mismatched=0`, 4,044,806 B in /
  6,162,400 B out, ours 60.2 ms vs the reference 66.2 ms over the 20 files; the
  tampered original is counted as one mismatch and still returned.
* fast mode through the stubs: `handled=31 fallbacks=3` after the run, results
  equal to the payloads.
* timing (hot cache, `decode` vs the reference on **msvcrt**, which has none of
  the game CRT's per-call locking): 3 MB file 28.9 ms vs 32.1 ms (1.11×);
  31 KB file 327.9 µs vs 345.2 µs (1.05×). In the fixture both sides are
  inflate-bound; the removed work per file (memset of the output, 10–40 locked
  `fgetc`, trailer seek/read pair, one `fread` + byte XOR + `inflate` per
  kilobyte) is what the game's static CRT under FEX makes expensive, so the
  game-side number comes from the `resource_read` / `crt_fgetc` probes of the
  next run, not from this fixture.
* probes: 4 fixture sites active, one refused (`bytes_mismatch`), nested and
  `ret 4` frames matched, garbage ESI left unclassified without a dereference,
  longjmp desync recovered (`calls=4 exits=2 desync=2`), 1,600 calls on two
  threads with `desync=0 overflow=0`, restoration verified.
* pool: `opens=7 reused=2 real_opens=5 kept=4 real_closes=2`, one errored
  stream closed for real, drain closes the rest.

## What the next game run must show

1. `tools/manage.py launch --direct --resource-read verify --dat-handles` (X3
   bottle), one savegame load: `resource_reader mode=verify installed=1
   status=active`, `dat_handle_pool … installed=1`, then at device destroy (or
   every telemetry summary with `--telemetry`) `resource_reader_metric …
   verify_files=N verify_equal=N verify_mismatched=0` with `fallback_*` counts
   that add up to `calls − handled` (expected: `fallback_not_gzip` for plain
   records, `fallback_gz_handle` for the savegame stream, everything else 0), and
   `dat_handle_pool_metric reused ≈ opens − <number of distinct .dat files>`
   with `errors=0 full=0`.
2. Then `--resource-read fast --dat-handles` with `--telemetry --loading-probes`:
   `resource_read` probe calls unchanged, its inclusive time and
   `crt_fgetc` calls collapsing, `CreateFileA`/`CloseHandle`/`GetFileType`
   counts down by the pool's `reused`, and the stall shorter by the
   difference — the acceptance number for §6 items 1 and 2 of the stall study.
3. A stopwatch comparison against the plain `--direct` load (≈ 40 s on X3)
   with `frame_end dt_ms` as the log-side witness.
