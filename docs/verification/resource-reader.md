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
| `verify` | our decode into a scratch buffer, stream restored, then the original through the chain; bytes, `DAT_00596988/c`, the four counter deltas, the record cursor and the stream position compared; `resource_reader verify equal=0 … cursor_ok= position_ok= cursor= expected_cursor= position= expected_position=` per unequal file (≤ 64), `resource_reader_metric` summary (`cursor_short=` counts the records whose chunk loop stops before the record end, `fast_read_us= fast_scan_us= fast_alloc_us= fast_inflate_us=` decompose `fast_us`); the caller always gets the original's buffer |
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
   position (both against the reference object's state after its read, and
   the reference itself against the chunk-loop formula `original_final()` of
   the fixture); text sources fall back with `not_gzip`.
   **1b. the record-cursor class** (`RR_CASE name=cursor`): twenty sources
   `rr_cursor_<q>_<k>.pck` whose `(length − first) mod 1024` is `k` = 0…9 at
   `q` = 1 and 3 chunks (payload sizes searched; alternating scrambled/plain,
   every fifth with an FNAME header so `first ≠ 10`), each read loose and as a
   record of `rr_cursor.dat`, whose last record (no padding after it) is a
   class record. Per source: the reference stops at `length − k` for
   `k` = 1…8 and at `length` otherwise (formula), fast mode leaves the same
   cursor and position, the dispatcher's next clamped read from both states
   returns the same bytes; through the verify stub (case 4) every one is
   `equal` with `cursor_ok=1 position_ok=1` and `cursor_short` counts 32
   (16 loose + 16 records); in the pool case a kept `.dat` handle runs the
   game's sequence (open, class record, close, reopen, `fseek`, next record,
   three times) against the reference on its own pooled handle.
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
   timing of the 3 MB file (loose and as a catalogue record) and the 31 KB
   file, fast core vs reference, with the core's own phase stamps
   (`RR_PHASES … read_us= scan_us= alloc_us= inflate_us= total_us=`).
6. **`.dat` pool**: first open real, a concurrent open of the same path gets a
   distinct handle, close keeps, reopen reuses (and the reference reads the
   right record after the caller's `fseek`), write modes pass through, other
   paths pooled separately, an errored stream is closed for real, missing files
   return null, `pool_drain` closes everything, unknown handles close for real.

Results: `verification/results/bottle-X3/resource-reader-summary.json` /
`resource-reader-fixture.txt` — see the numbers section below (filled from the
recorded run). `verification/analysis/test_resource_reader.py` re-parses the
recorded summary.

## Review 27 notes

* `statistics()` and `pool_statistics()` now snapshot with `load64` (a
  `cmpxchg8b` of zero against zero) instead of `exchange64(p, *p)`: the old
  form re-wrote a plain, possibly stale read over the counter and could drop an
  `add64` racing from the loader thread. `held` is read under the pool lock,
  where it is maintained (the reuse path decremented it after releasing the
  lock).
* The reader's site claim and the two pool call-site redirects obey the
  `engine_patch` install window and atomic write described in
  [loading-probes.md](../reverse-engineering/loading-probes.md) ("Install
  window"); `resource_reader … status=late_claim` / `open_status=late_claim`
  name a refused late install.
* `x3m_resource_read_entry` dereferences EAX without a plausibility check: the
  site has exactly two callers (`0x004e8ead`, `0x004f7a71`), both passing the
  heap file object, and the handler is a functional replacement of the body
  that dereferences the same object first thing; a probe-style guard would
  only protect a caller that the original would crash on as well.

## Recorded numbers (bottle X3, 2026-09-13, after the cursor fix)

`run_resource_reader.py` (X3M_FIXTURE_BOTTLE=X3, arm64 Wine + FEX, zlib 1.2.3):
**4,707 checks, 0 failures** (the count grew from 405 because the payload
search of the cursor case checks every `gzopen` it makes; the 20 sources
took ~4,200 attempts).

* cursor class: `RR_CASE name=cursor sources=20 class_records=16
  loose_and_record_handled=20 last_record_class=1`; the reference stops at
  `length − k` for `k` = 1…8 and at `length` for 0 and 9 (formula and
  reference agree on all 20 loose and 20 record reads), fast mode leaves the
  reference's cursor and position in every case and the dispatcher's next
  read from both states returns the same bytes; through the verify stub
  `RR_STATS_CURSOR verify_files=40 cursor_short=32 verify_equal=60
  verify_mismatched=1` (the one mismatch is the deliberate tamper case); the
  pooled sequence on `rr_cursor.dat` (class record, previous record, class
  record; close/reopen between) reads equal to the reference on its own
  pooled handle.
* phases (the core's own stamps, msvcrt, warm cache, means over the rounds):
  3 MB payload / 1.97 MB extent loose `read_us=462.8 scan_us=72.9
  alloc_us=9.6 inflate_us=28328.8 total_us=29013.4`; the same as a catalogue
  record `read_us=527.5 scan_us=141.5 alloc_us=10.8 inflate_us=28391.1
  total_us=29240.6` (the XOR 0x33 pass costs ~70 µs per 2 MB); 31 KB payload
  / 19.7 KB extent `read_us=6.7 scan_us=0.1 alloc_us=0.2 inflate_us=305.9
  total_us=318.9`. **`inflate` is 96–98 % of our path**; ratios against the
  reference 1.11× on all three. What the game run adds to this is the static
  CRT's per-call cost (the reference's 10–40 `fgetc`, per-KiB `fread`) and
  the cold read, both absent here.

* fast vs reference: 10 loose sources handled (the text file falls back with
  `not_gzip`), 20 catalogue records handled over two shuffled passes; bytes,
  size globals, counters, cursor and position equal to the reference object's
  state in every case (none of these sources is in the cursor class).
* fallbacks (reason): gz_handle, progress, transparent → `not_gzip`, position
  and cursor → `state`, method, reserved, length, invalid block → `inflate`,
  wrong ISIZE → `size`, truncated → `inflate`, empty, header, alloc — each with
  the stream and cursor restored (and the reference decoding correctly
  afterwards for the transparent and alloc cases).
* verify mode through the chained stubs: `calls=22 handled=20 fallbacks=2
  verify_files=20 verify_equal=20 verify_mismatched=0`, 4,044,806 B in /
  6,162,400 B out, ours 63.2 ms vs the reference 68.1 ms over the 20 files; the
  tampered original is counted as one mismatch and still returned.
* fast mode through the stubs: `handled=31 fallbacks=3` after the run, results
  equal to the payloads.
* timing (hot cache, `decode` vs the reference on **msvcrt**, which has none of
  the game CRT's per-call locking): 3 MB file 29.0 ms vs 32.2 ms (1.11×);
  31 KB file 318.9 µs vs 355.3 µs (1.11×). In the fixture both sides are
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

## Run C (2026-09-12, bottle X3, review-29 build): the cursor class

`--telemetry --resource-read verify --dat-handles`, log
`/tmp/x3-bottleX3-run13/session-20260912-234442-216.log` (52 MB, grepped):
`resource_reader_metric … calls=4210 handled=4084 fallbacks=126
verify_files=4084 verify_equal=4064 verify_mismatched=20 verify_original_null=0
catalogue=3384 scrambled=0 fallback_not_gzip=126` (every other reason 0),
`bytes_in=789,471,900 bytes_out=2,101,058,340`, `fast_us=11,908,988.7
fast_max_us=193,504.9 original_us=13,529,960.1`; `dat_handle_pool …
installed=1 open_status=active close_status=active`, `dat_handle_pool_metric
opens=594 reused=582 real_opens=12 … errors=0 full=0 held=12`.

* **The 20 mismatches** are all `mismatches=0 globals_ok=1 counters_ok=1
  cursor_ok=0` and carry six sizes (6170, 4948, 6226, 5959, 87536 ×9,
  278325 ×3). Root cause and the record table:
  [resource-reader.md "Record cursor after the chunk loop"](../reverse-engineering/resource-reader.md):
  the original's 1 KiB loop (`0x004e8d50`–`0x004e8db7`) exits on
  `Z_STREAM_END` and never requests the trailer, so for records whose
  `(length − first) mod 1024` is 1…8 the cursor stops `rem` bytes short of
  the record end; we set it to `length`. Confirmed offline against the 17
  catalogues (42 of 7,180 gzip records are in the class; the six loaded ones
  are exactly the logged sizes, each unique) and not related to the pool,
  the record's position in the `.dat` or the comparison order (fresh handles
  reproduce it in the fixture). Fix: the core predicts the loop's end from
  the bytes `inflate` consumed, sets the cursor to it and seeks the stream
  there in fast mode; verify compares cursor and `_ftell`.
* **Speed, honestly**: 11.9 s ours vs 13.5 s the original over 4,084 files
  is 2.9 ms/file, 66 MB/s in / 176 MB/s out, and biased against us: verify
  runs our path first, so ours pays the cold page-cache read of every extent
  and the original re-reads warm. The 193 ms maximum is one file's cold
  read. The fixture's phase split (below) is on msvcrt with a warm cache; the
  game-side split comes from the new `fast_read_us`/`fast_scan_us`/
  `fast_alloc_us`/`fast_inflate_us` fields of the next run. The structural
  gain that remains is bounded by what the original spends outside
  `inflate` (memset, 10–40 locked `fgetc`, the trailer pair, one `fread` +
  XOR + `inflate` call per KiB, `_fopen`/`_fclose` per resource); under FEX
  `inflate` itself is the same zlib1.dll on both sides. The whole-extent
  `fread` already bypasses the CRT's 4 KiB buffer for the multiple of the
  buffer size (VC8 `_fread_nolock` reads `count − count % bufsize` straight
  into the caller's buffer, one `_read`, plus one buffered `_read` for the
  remainder), so a `setvbuf` or a direct `ReadFile` on `_get_osfhandle` would
  save at most one syscall per file and was not done.

## What the next game run must show

1. `tools/manage.py launch --direct --telemetry --resource-read verify
   --dat-handles` (X3 bottle), the run-C save and path: `resource_reader
   mode=verify installed=1 status=active`, `dat_handle_pool … installed=1`,
   then `resource_reader_metric … verify_files=N verify_equal=N
   verify_mismatched=0` with **`cursor_short` ≈ 20** (the six class records,
   each counted per read: run C read them 20 times) and no `resource_reader
   verify equal=0` line at all; `fallback_*` counts adding up to `calls −
   handled` (`fallback_not_gzip` for plain records, everything else 0);
   `fast_read_us + fast_scan_us + fast_alloc_us + fast_inflate_us ≤ fast_us`
   with the split reported in this file; `dat_handle_pool_metric reused ≈
   opens − <number of distinct .dat files>` with `errors=0 full=0`.
2. Then `--resource-read fast --dat-handles` with `--telemetry --loading-probes`:
   `resource_read` probe calls unchanged, its inclusive time and
   `crt_fgetc` calls collapsing, `CreateFileA`/`CloseHandle`/`GetFileType`
   counts down by the pool's `reused`, and the stall shorter by the
   difference — the acceptance number for §6 items 1 and 2 of the stall study.
3. A stopwatch comparison against the plain `--direct` load (≈ 40 s on X3)
   with `frame_end dt_ms` as the log-side witness.

## Review 31 reader corrections (2026-09-13, full X3 fixture passed)

The independent reader review found three issues outside the successful cursor
fixture cases: the mismatch format omitted `cursor_ok`, fast mode ignored failure
of the short-class final rewind, and verify instrumentation did not restore
incoming LastError before the original call. All three are corrected and independently reviewed in source;
[review 31](../archive/review-31-adjacency-reader.md#resource-reader-review-2026-09-13) records
the findings and evidence limits. The old 4,707-check result above predates these
fixes and its deliberate-mismatch line has shifted fields.

New controls fail the final rewind once for both loose and catalogue inputs and
check allocation release, untouched bookkeeping, restored entry state and a
successful original retry. The existing deliberate mismatch also verifies
incoming/outgoing LastError through the generated stub. The runner rejects
missing/duplicate records and fields, checks the exact cursor inventory and typed
mismatch line, and hashes every compiled local dependency plus its runtime
helpers and parser tests. The standalone Werror build and one full X3 fixture run pass **4,721 checks,
zero failures**, followed by all ten host tests. The 40/32/60/1 cursor inventory
is unchanged; both rewind failures and the original error sentinels pass, and
the deliberate mismatch line now has correctly aligned fields. All 21 source
hashes and the current native/copy/executable/stdout hashes match. Summary SHA-256:
`43fc03ab8b57a53cb067bd249ca46b8521d57e6c593f338d2fa7a2989c602f33`.
Current warm-cache fast/reference ratios are 1.11x loose-large, 1.11x medium,
1.12x catalogue-large. See review 31 for full provenance and independent closure.
This does not establish game parity or a native-Windows test result.

**Removed from the launcher 2026-09-26** (user decision): the `verify` choice of `--resource-read` (choices `native` / `fast`). The DLL parse of `X3M_RESOURCE_READ=verify` and the verify mode stay; `resource_reader_fixture.cpp` binds the mode directly (`fixture_bind`).
