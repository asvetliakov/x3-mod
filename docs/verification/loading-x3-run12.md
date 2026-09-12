# Loading run 12 (bottle X3, review-29 build): light hooks + the twelve engine probes

Log `/tmp/x3-bottleX3-run12/session-20260912-234117-216.log` (46,661,564 B, sha256
`57a5c14a7970c13b6…`), `tools/manage.py launch --direct --telemetry --loading-probes`
on commit `a34c389`, bottle `X3` (arm64 Wine + FEX). Path: start → menu → the usual
save → flight → sector change → exit; last Present 150.558 s. QPC 10 MHz,
120 loading report windows, 40 import hooks, **`loading_probes installed=12`, all
twelve `status=active`**, 537 `loading_probe` windows, 60 `loading_probe_caller`,
2 `loading_probe_path`, `desync=0 overflow=0` on every site all run. No `--gz-buffer`,
no resource reader, no `--profile` (so no sampled attribution: the probes *are* the
measurement, as §7 of the stall study asked).

Records: `verification/results/loading-x3-run12/loading-profile.{json,md}` (per-gap and
per-stall tables) and `verification/results/loading-x3-run12.json` (whole-run totals).
`analyze_loading_profile.py` ran unchanged on this log and already carried the
"Engine probes" table; three tables were missing and were added (§6).

## 1. Load phases

| Phase | run 12 | run 9 | run 8 (gz buffer) | run 5 | `frame_end` dt of the same phase |
| --- | ---: | ---: | ---: | ---: | ---: |
| menu load | **7.621 s** (2.735–11.413) | 8.386 | 8.159 | 9.726 | 10.777 s (frame 120) |
| save load | **38.528 s** (21.966–60.631) | 35.707 | 33.363 | 65.460 | 41.645 s (frame 1200) |
| sector change | **5.368 s** (120.283–125.951) | 5.944 | 5.411 | 7.976 | 8.711 s (frame 7500) |
| menu return | **7.344 s** (139.199–147.131) | 7.947 | 7.448 | 8.784 | 10.391 s (frame 9000) |

The new `frame_end … elapsed_ms= dt_ms=` stamps bracket every phase correctly and
consistently loose by 3.05–3.34 s — exactly the ~300 ordinary frames each 300-frame
line also spans — so they are a usable fallback but never a substitute for the
presentation gap. Menu, sector and menu return are all **0.1–0.8 s faster than run 8**
despite the missing read-ahead buffer; only the save load is slower (+5.2 s), and that
is the savegame decode stall coming back (12.107 s here vs 5.13 s in run 8).

**gzread with the light envelope.** 14,461,839 calls / 45,755,384 B whole run
(run 5: 13,970,478 / 43,542,947 B), **2.117 s inclusive = 0.146 µs per call**, plus
1.158 s of wrapper tail = **0.227 µs all-in**. Run 5 measured 9.162 s / **0.656 µs**
inside the span plus 1.282 s of tail. The light row is therefore **4.5× cheaper inside
the span and 2.9× cheaper all-in**, worth **≈7.2 s** of the save load; the fixture's
32.5 ns native cost puts the game's own share at 0.47 s, so 2.8 s of the 3.28 s that
gzread now costs is still envelope. `--gz-buffer` is worth about 2 s more, not the 16 s
it was worth against the old `CpuCallBoundary` envelope.

## 2. The save load decomposed by probe

Save gap, 38.528 s, probe inclusive (exclusive where the documented nesting allows):

| Probe | Calls | Incl. s | Excl. s | Share | Extras |
| --- | ---: | ---: | ---: | ---: | --- |
| `signature_check` 0x004cabc0 | 846 | **12.876** | 12.876 | 33.4 % | — |
| `resource_load` 0x004e8e10 | 2,358 | 7.928 | 0.075 | 20.6 % | — |
| `resource_read` 0x004e8880 | 2,268 | 6.706 | 6.706 | 17.4 % | plain 700 / catalogue 1,568 |
| `texture_loader` 0x004dc540 | 706 | 4.241 | — | 11.0 % | — |
| `mesh_body` 0x004bc680 | 3,342 | 2.875 | 2.875 | 7.5 % | — |
| `resource_open` 0x004e8780 | 2,358 | 1.146 | 0.013 | 3.0 % | loose 700 / catalogue 1,568 / failed 90 |
| `name_resolve` 0x004e7590 | 2,358 | 0.667 | 0.055 | 1.7 % | hit 2,268 / miss 90 |
| `find_wrapper` 0x004d2950 | 2,362 | 0.612 | 0.612 | 1.6 % | 0x004e7947 2,358 + 3 script-VM callers |
| `sopen_helper` 0x00527869 | 2,269 | 0.466 | 0.466 | 1.2 % | — |
| `texture_body` 0x004dd2c0 | 29 | 0.370 | — | 1.0 % | — |
| `read_dispatch` 0x004e9210 | 14,828,441 | count only | | | plain 4,634 / catalogue 362,004 / **gzhandle 14,461,803** |
| `crt_fgetc` 0x0050fff5 | 17,816 | count only | | | 7.9 per resource read |

Non-overlapping top level (`signature_check` + `resource_load` + `mesh_body` + the
savegame `gzread` span) = **25.80 s = 67.0 %** of the gap; the texture sites add at most
4.61 s more, and 3.33 s is instrumentation (§4), leaving **≈5–9 s** of engine-side work
(savegame record parsing and asset construction) with no probe on it.

### The §7 questions, answered on the script/XML stall (stall 2, 16.758 s, 42.575–59.333 s)

This is stall B of the study (it predicted 17–25 s; measured **16.758 s**, and 89.3 % of
it is inside hooks).

| §7 item | Measurement | Share of the 16.758 s |
| --- | --- | ---: |
| **(d) signature check** | `signature_check` **844 calls / 12.835 s incl**, 15.21 ms mean, 27.6 ms max. CryptoAPI imports inside it total **12.825 s = 99.9 % of the site**: `CryptAcquireContextA` **2,532 calls (exactly 3.00 per check, 844 failing) / 10.346 s / 4.086 ms each**, `CryptReleaseContext` 844 / 0.964 s, `CryptImportKey` 844 / 0.610 s, `CryptVerifySignatureA` 844 / 0.546 s, `CryptHashData` 844 / 0.200 s over 24,994,790 B, `CryptCreateHash` 0.092 s, `CryptDestroyHash` 0.042 s, `CryptDestroyKey` 0.024 s, `CryptGetHashParam` 1,688 / 0.001 s | **76.6 %** (`CryptAcquireContextA` alone 61.7 %) |
| **(a) resource opens** | `resource_open` 1,412 calls — **loose 699 / catalogue 701 / failed 12** (§7 expected 1,376 with 790/586) / 1.024 s incl, 0.007 s excl. CRT open helper `sopen_helper` 1,400 / **0.390 s**; `CreateFileA` 1,400 / 0.243 s (173 µs), `GetFileType` 1,400 / 0.144 s, `CloseHandle` 1,400 / 0.033 s | 6.1 % |
| **(f) name probes** | `find_wrapper` 1,415 / **0.582 s** (callers: resolver 0x004e7947 ×1,412, script VM 0x004adea1 ×2, 0x004adf29 ×1); `FindFirstFileA` 1,422 calls, **712 failures (50.1 %)**, 0.578 s at **406 µs**; `FindNextFileA` 2,184 / 0.043 s; `name_resolve` hit 1,400 / miss 12, 0.626 s incl / 0.044 s excl | 3.5 % |
| **(b) archive reader** | `resource_read` 1,400 / **0.611 s**, 113,425,778 B (uncompressed payload from `DAT_00596988`, not the compressed extent). **`inflateInit2_` 1,393 = `inflateEnd` 1,393 ≈ one inflate stream per file/read (1,400), never per 1 KiB chunk**; `inflate` 27,190 / 0.405 s → **19.5 chunks per stream**. `read_dispatch` 141,952 (plain 4,631 / catalogue 26,752 / gzhandle 110,569), 31,790,339 B | 3.6 % |
| **(e) texture/mesh bodies** | `texture_loader` 47 / 0.494 s, `texture_body` 3 / 0.012 s, `mesh_body` 301 / 0.197 s — the non-script half is **small in this stall** (it moved into stall 1 and the rest of the gap); `xmlReadMemory` 790 / 0.204 s / 22,531,669 B is the script parse itself | 4.2 % (+1.2 % XML) |
| **(c) locked fgetc header parsing** | `crt_fgetc` **11,144 calls = 7.96 per resource read** (§7 expected 10–40 per gz file); count only, so its own cost is ≤1.6 ms at the count-only envelope | ≈0 % |
| **(g) unattributed remainder** | top level `signature_check` + `resource_load` + `mesh_body` (+ texture exclusive) = **14.70–15.21 s (87.7–90.8 %)**; remainder **1.55–2.06 s**, of which instrumentation is only **0.040 s** | 9–12 % |

**So the ~17–25 s script/XML stall is, in one line: three quarters CryptoAPI signature
verification, one tenth the open/resolve path, and only 5 % assets.** The study's own
candidate list had this backwards in weight: the four per-file candidates together —
the per-resource `_fopen` (6.1 %), the redundant `memset` + 1 KiB inflate chunks + byte
XOR inside `resource_read` (3.6 %) and the locked `fgetc` (≈0 %) — are **9.7 %** of the
stall, **13.2 %** once the negative name-probe cache is counted with them.

### The savegame decode stall (stall 1, 12.107 s, 21.421–33.528 s)

`gzread` 14,351,234 / 2.101 s span (+1.149 s tail); `read_dispatch` gzhandle 14,351,234
— the savegame stream never enters `resource_read` (`x2=0`), it is dispatched directly.
`resource_load` 204 / 2.974 s with `resource_read` 192 / 2.925 s (of which `inflate`
153,159 / 2.535 s) is the concurrent asset traffic. Instrumentation here is **3.245 s =
26.8 % of the stall** (2.080 s of count-only `read_dispatch` stubs, 1.164 s of tails), so
the engine's own per-field loop is **≈3.8 s**: 12.107 − 2.101 (gzread) − 2.974
(resource_load) − 3.245 (instrumentation). A read-ahead buffer removes the
instrumentation, not those 3.8 s.

### Menu load (7.621 s) and menu return (7.344 s)

`resource_load` 1,280 / 3.215 s (42.2 %) → `resource_read` 1,174 / **3.012 s (39.5 %)**,
of which `inflate` 167,720 / **2.414 s (31.7 %)** at 14.40 µs per 1 KiB chunk;
`inflateInit2_`/`inflateEnd` 1,138 each — again one stream per read, **147 chunks per
stream**. `mesh_body` 1,017 / 0.710 s. `texture_loader` 503 / 2.716 s and `texture_body`
13 / 0.157 s sit above `resource_load`. Opens are nearly free: `resource_open` 1,284
(**loose 1 / catalogue 1,175 / failed 108**) / 0.167 s, `sopen_helper` 1,176 / 0.095 s,
`CreateFileA` 1,176 / 0.070 s. Name probes 0.031 s (`FindFirstFileA` 1,289 with **1,282
failures, 99.5 %**). `signature_check` **zero calls** — no script verification in a menu
load. `crt_fgetc` 9,104 = 7.75 per read. Remainder after the top level (3.925–6.798 s
covered): 0.8–3.7 s. The menu return repeats this within 4 % on every line
(`resource_read` 584 / 2.939 s, `inflate` 166,017 / 2.433 s, 99.3 % `FindFirstFileA`
failures).

### Sector change (5.368 s)

`resource_read` 156 / **1.480 s (27.6 %)** with `inflate` 80,643 / 1.242 s (23.1 %);
`mesh_body` 2,232 / **1.406 s (26.2 %)**; `texture_loader` 124 / 0.681 s; `texture_body`
9 / 0.133 s. Opens 183 (catalogue 156, failed 27) / 0.021 s, `sopen_helper` 0.013 s,
name probes 0.005 s (`FindFirstFileA` 184, **183 failures**), `crt_fgetc` 1,152 = 7.4 per
read, `signature_check` zero. Decompression and mesh preparation are the whole phase;
the open/resolve path is 0.5 %.

## 3. Write-side APIs during loading: none

Whole run, over the 537 probe windows: **`DeleteFileA` 0, `MoveFileA` 0, `WriteFile` 0,
`SetEndOfFile` 0** calls; `MoveFileExA` is not in the main module's IAT
(`loading_hook name=MoveFileExA installed=0`). The only write-side calls are
**`CreateDirectoryA` ×2, both failing** (`failures=2`, i.e. the directories exist), at
20.421 s, and the two `loading_probe_path` rows name them:
`C:\users\crossover\Documents\Egosoft` and `…\Egosoft\X3AP` — the savegame directory,
outside the catalogue tree, and **1.5 s before the save-load gap opens (21.966 s)**.

Inside the four load gaps there is **not one write-side call**, against 1,173
(menu) / 1,568 (save) / 156 (sector) / 583 (menu return) catalogue reads. **The catalogue handle pool and a negative
name-probe cache are therefore safe without in-load invalidation**; they still need the
invalidation rules of §5 of the stall study for the rest of the session (a save, a
mod install), because this evidence only covers the loads. Caveat: these are
main-module IAT hooks, so a write issued from inside a DLL would be invisible — the
game's CRT is statically linked, so its `fwrite`/`_write` reach the same hooked
`WriteFile`, and that count is zero.

## 4. Instrumentation cost

| Item | Calls | Seconds | Share of the 150.6 s run |
| --- | ---: | ---: | ---: |
| import wrapper tails (measured) | 15,528,231 | **1.245** | 0.83 % |
| timed probe stubs (bound, 282 ns/call) | 37,100 | 0.011 | 0.01 % |
| count-only probe stubs (bound, 143 ns/call) | 15,281,392 | 2.191 | 1.46 % |
| **total** | 30,846,723 | **3.447** | **2.29 %** |

The tail is 3.15 % of the 39.568 s of hooked inclusive time, and **93 % of it is
`gzread` alone** (1.158 s of 1.245 s); run 8, with the read-ahead buffer, paid 0.133 s of
tail on 26.034 s. The count-only probe bound is likewise `read_dispatch`'s 14.46 M
gz-handle dispatches. Per interval: the savegame decode stall carries **26.8 %**
instrumentation, the save gap 8.7 %, the **script/XML stall only 0.2 %** (0.040 s), the
menu load 0.6 %, the sector change 0.4 %. So every number in §2 above except the decode
stall is measured on a nearly clean clock.

**Did the light rows change the hooked per-call costs?** No — within ±12 %, and in the
direction of cheaper:

| Operation | run 12 calls | run 12 µs/call | run 8 µs/call | Δ |
| --- | ---: | ---: | ---: | ---: |
| `CreateFileA` | 4,264 (2 fail) | **106.4** | 104.7 | +1.6 % |
| `FindFirstFileA` | 4,615 (3,850 fail, 83.4 %) | **145.8** | 157.0 | −7.1 % |
| `ReadFile` | 208,254 | **7.21** | 8.23 | −12.4 % |
| `inflate` | 774,275 | **15.05** | 15.73 | −4.3 % |
| `gzread` | 14,461,839 | **0.146** | (buffered away) | run 5: 0.656 |

These four rows were already dominated by real Wine/FEX work — 106 µs for a
`CreateFileA` is three orders of magnitude above any envelope — so the light change is
invisible on them and decisive only where the call itself is tiny (`gzread`,
`read_dispatch`). The probes themselves add nothing measurable to the timed sites
(37,100 calls, 11 ms).

## 5. Ranked recommendation (≤25 lines)

1. **Cache the crypto provider at `0x004cabc0` (new #1, was §6 item 4 "measure first").**
   844 calls cost **12.876 s of the 38.5 s save load (33 %) and 12.835 s of the 16.758 s
   script/XML stall (77 %)**; 99.9 % of that is CryptoAPI and **10.346 s of it is the
   three `CryptAcquireContextA("X2EgosoftCSPContainer", …)` per call at 4.09 ms each**
   (named key containers are registry/keystore work under Wine). Holding one
   `HCRYPTPROV` (and one imported key) for the process removes the 2,532 acquires and
   844 releases: **−11.3 s**. Memoising the whole verification by script-text hash
   removes **−12.8 s**. Expected save load **38.5 → 26–28 s**; no other phase changes
   (zero signature checks in menu/sector).
2. **The reader rewrite (§6 item 2: one `fread`, word-wide XOR, one `inflate`, no
   `memset`).** `inflate` is 11.656 s whole run at **15.05 µs per 1 KiB chunk**
   (774,275 chunks, **189 per stream**, one stream per file), and `resource_read` holds
   a further **2.539 s** of `fread`/XOR/`memset` above it (menu 0.598, save 1.140,
   sector 0.238, menu return 0.506). Bound: **−2.5 s whole run measured**, plus whatever
   share of `inflate`'s 11.656 s is per-call entry cost rather than decompression —
   189 calls collapse to 1 per file, so that share is the whole upside and this run
   cannot size it. In the script/XML stall the reachable part is only 0.21 s.
3. **Catalogue `.dat` handle pool (§6 item 1).** 3,524 of 4,557 opens are
   catalogue-resolved; `resource_open` 1.474 s whole run, `sopen_helper` 0.674 s,
   `CreateFileA` 0.454 s. Bound **≈ −0.7 s whole run / −0.5 s in the stall** — a quarter
   of the study's expectation, because `CreateFileA` fell from 591.7 µs (run 5) to
   106.4 µs.
4. **Negative name-probe cache (§6 item 3).** `FindFirstFileA` 4,615 calls with
   **3,850 failures (83.4 %)** / 0.673 s; the menu loads fail 99.5 % of their probes.
   Bound **≈ −0.6 s whole run**, −0.58 s of it inside the script/XML stall.
5. **Savegame decode** stays last: the 14.46 M `gzread` round trips are only 0.47 s of
   real zlib; the **≈3.8 s** is the engine's own per-field loop, reachable only by a
   bulk-field decoder, and `--gz-buffer` buys ≈2 s of *instrumentation* back.

**Does run C (`--resource-read fast --dat-handles`) attack the top item? No.** It
attacks items 2 and 3 — **≥3.2 s whole run measured (2.5 + 0.7) and ≈0.7 s of the
16.758 s script/XML stall**, plus the unsized `inflate` per-call share — and leaves the
12.8 s signature check untouched. Run C should carry
the provider cache as well (or run the provider cache first): it is a one-function
change with a larger measured target than every other item combined.

## 6. Tool changes (`analyze_loading_profile.py`, tests)

The tool handled this log unchanged and already printed the Engine-probes table, but
three tables that §2–§4 above need did not exist and were added:

* **Probe exclusive column.** `analyze_iteration08_loading.PROBE_CHILDREN` encodes the
  documented nesting (`resource_load` → open + read; `resource_open` → `name_resolve` +
  `sopen_helper`; `name_resolve` → `find_wrapper`) and `probe_exclusive()` subtracts it,
  clamped at zero for a straddling window. The two texture sites get no exclusive value
  (`—`): their only probed child, `resource_load`, has eleven callers.
* **Write-side file APIs section** per gap and per stall, with `loading_probe_path` now
  kept by the scanner (`WRITE_OPS`), and an explicit negative statement plus the
  catalogue-read count when the interval has no write.
* **Instrumentation cost section**: measured wrapper tails next to a bound for the probe
  stubs, from the fixture's envelope components (`LIGHT_ENVELOPE_NS` 351,
  `QPC_READ_NS` 69.2 → timed 282 ns, count-only 143 ns).

Tests: four new cases in `verification/analysis/test_loading_profile.py`
(exclusive nesting, the zero clamp, the write-side table with and without a write, the
instrumentation split). `test_loading_profile.py` 19 tests, `test_profile_summary.py` 11,
`test_iteration08_loading.py` 10, `test_loading_phases.py` 6 — all pass. No production
source was touched and nothing was committed.
