# Field of view (`--fov`): verification ledger

Feature: `--fov <vertical degrees 36..120>|game` / `X3M_FOV`, `src/proxy/fov.cpp`, site core
`src/proxy/fov_sites.h` ([field-of-view.md](../reverse-engineering/field-of-view.md) §5, as built §6.1).
The imm32 of the registry constructor's `MOV dword [ESI+0x24],0x4000` at `0x0041c9d9` becomes
`F = round(65536/π·atan(tan(v/2)/0.75))`. The write is one `lock cmpxchg8b` into the aligned qword
`0x0041c9d8..0x0041c9df`, where the imm32 is `0x0041c9dc..0x0041c9df`. If the registry already exists,
`+0x24` gets one validated `InterlockedCompareExchange`. The launcher default is 58.7155° (`F = 0x3470`),
which is 90° horizontal on 16:9, by user decision 2026-09-24. `game` and 73.74 patch nothing. The
small-parts cull threshold carries `F/0x4000`, with F the view's own FOV (base divided by the zoom, the
camera's `+0x298`) taken from the latched projection, `cot(F/2) = max(0.75·m11, m00)`. That needs no
engine read. `registry+0x24` is only the fallback when P[0] is valid but P[5] is not.
`tools/analysis/cull_census.py` buckets with the same factor.

| Date | Check | Command | Result |
| --- | --- | --- | --- |
| 2026-09-24 | Site qualification on the installed EXE (structural identity; hash INFO). The window `0x0041c9cc..0x0041c9e7` holds seven whole instructions. The site decodes as `mov DWORD PTR [esi+0x24],0x4000`. ESI is loaded from `[esp+0x38]` at `0x0041c97b` and is not written again before the site. The constructor `0x0041c960..0x0041cc14` (201 instructions) ends in `ret 4`. Its direct callers are `0x00403a26` and `0x004050f4`. No direct branch lands in the window, no raw rel8/rel32 encoding lands in it (0 hits), and no dword points into it (0). The patched image (`0x3470`) keeps every boundary. The reader contract holds (`8b1504856000 8b7224` at `0x00421148`, `894a24` at `0x0042dc04`). The conversion table and bounds check out. No overlap with the 162 other claims. The header matches its Python twin. | `python3 verification/probe/verify_fov_site.py` | PASS 18/18 (measured) |
| 2026-09-24 | Identity variants with the new verifier, which adds the FOV corrupt case `0x0041c9dd` (`40 → 41`) | `python3 verification/results/executable-identity/run_verifiers.py --output verification/results/executable-identity/verifiers.json` | PASS. Shipped, laa_cleared, ntcore_4gb and unknown_hash pass 26/26 each. different_build fails 26/26 with identity false. Five site-corrupt cases FAIL with identity true. 41 anchors (measured). |
| 2026-09-24 | Host tests. `test_fov_site` (9 tests) compiles the header on the host and covers the bytes, 11 changed window bytes and an already patched window refused, and the `X3M_FOV` parser with 16 rejected spellings. It checks bounds 36..120, plausible 0x106..0x8000, and the conversion table: 36 → `0x2150`, 58.7155 → `0x3470`, 58.72 → `0x3471`, 59 → `0x34aa`, 73.74 → `0x4000`, 120 → `0x5eb4`, and 2·atan(9/16) → `0x3470`. It runs install, read-back, rollback and restore on a copied window at the engine's qword offset 4: `invalid_value` for 0x4000/0x105/0x8001, `late_claim`, `invalid_site`, `unreadable`, `patch_rolled_back`, `protect_failed`, `rollback_failed` live, and restore only over our own value. It also covers the line parsers, the wiring and the launcher (`--dry-run` only). `test_cull_small_parts` (15) adds the 0x3470 thresholds. `test_fov_patch_result` (7) checks the fixture record, including the recorded install sequence and INS_SetFocus override; `test_cull_census` adds the focus-factor case. | `PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_fov_site verification.analysis.test_fov_patch_result verification.analysis.test_cull_small_parts verification.analysis.test_cull_census` | 46 tests OK on the final tree. The 77 modules that touch the changed files (manage.py, capture.cpp, loader.cpp, CMake, the cull core) ran 814 tests, 0 failing, before the record test existed (measured). |
| 2026-09-24 | DLL build (worktree) and the no-x87 walk | `cmake --build build --target d3d9 -j8`; `python3 verification/probe/check_no_x87.py build/d3d9.dll` | builds under `-Werror`; PASS, 683 reachable functions, no violations (measured) |
| 2026-09-24 | Real write path under Wine. `verification/probe/fov_patch_fixture.cpp` links the unchanged `fov.cpp` (through a force-included fault seam), `engine_patch.cpp` and `engine_memory.cpp` into an EXE based at `0x00300000`. Image sections sit at the engine's pages: the constructor stub at `0x0041c000` (window at `0x0041c9cc`, MEM_IMAGE `PAGE_EXECUTE_READ`), the executed reader at `0x00421148`, the executed INS_SetFocus store at `0x0042dc04`, and the registry slot at `0x00608504`. The constructor stored `0x4000` before, `0x3470` after `initialize()`, `0x4000` after restore and after every rollback, and `0x3471` after the injected `rollback_failed`. That case is reported `patched_unverified` with `value=0x3471`, the immediate read back, and `configured_focus()` returns the same value. The protection went `0x20 → 0x80 (WRITECOPY) → 0x20` (record `protect`). The install sequence was 2 protects, 4 reads, 1 atomic write and 1 flush (record `install_sequence`). Refusals leave the page untouched: off `game` and `engine_value`, `out_of_range` for 35.9 and 120.5, `invalid_setting`, `too_long`, `executable_mismatch`, `reader_mismatch`, `bytes_mismatch` ×2, `protect_failed`, `late_claim`. The registry cases give `written` for 0x4000 and 0x3d34, and `skipped` for an implausible value, a misaligned pointer, a read-only page (value kept) and an uncommitted page. The `fov_confirm` rows came out as absent → present with `match=1`, then silent. After install, the executed INS_SetFocus store of `0x471c` still overrides the base: the reader and `current_focus()` both return `0x471c`, and the imm32 stays `70340000` (record `setfocus_override`). Restore acts only over our value. The two `restore_not_owned` rows with bytes both read `71340000`, once as the failed rollback's bytes and once as a foreign `0x3471` written after install; nothing was written either time. A third `restore_not_owned` row is the unreadable span (`found=--`). The private page was patched and restored with `0x5eb4`. LastError was preserved at every call. | `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_fov_patch.py` | 134/134, exit 0, bottle X3 arm64 (FEX_X87REDUCEDPRECISION=1, WINEMSYNC=1). Record `elapsed_s` ≈ 5 s (Wine run only; the build before it is not timed in the record). Install ≈ 360 µs, restore ≈ 200 µs (fixture-inclusive, not game FPS). Record `verification/results/bottle-X3/fov-patch.json`, rerun on the final tree after review (measured). |
| 2026-09-24 | Small-parts cull CPU fixture with the projection and registry seams. The run131 expectations at F = 0x4000 are unchanged: 2/4/8 px give thresholds 3/6/11, flipping 97/403, 128/458 and 139/479 nodes/draws. F = 0x3470 at the same m00 gives thresholds 4/7/13. The replay at 2 px and threshold 4 flips exactly the s < 4 class (98 nodes / 428 draws, other changes 0). `focus_from_projection` gives 0x4000, 0x3470 (m00 0.5 / m11 1.7778), 0x1a38 (zoom ×2) and 0x4000 on a 5:4 plane, and 0 for unusable terms. `begin_frame` takes F from P[5] over the registry base, with no registry read. At zoom ×2 the view's 0x1a38 raises the threshold to 7. The record's `replay_lines` carry `REPLAY zoom=2 px=2 threshold=7 focus=0x1a38` together with the value row naming `focus=0x1a38`. Without P[5] it makes one registry read. | `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_cull_small_parts.py` | 142/142 (132 before the option), exit 0, fixture rebuilt from the final source; record `verification/results/cull-small-parts-cpu.json` (measured) |
| 2026-09-24 | Factor effect at real projections | `python3 verification/results/field-of-view/small_parts_factor.py` (output `small_parts_factor.txt` beside it) | At 5120×1440 the default 2 px keeps threshold 2 at both 0x4000 (m00 0.375) and 0x3470 (m00 0.5), and 8 px goes from 6 to 5. With the run131 projection scaled to 0x3470 (m00 1.067, 1280 wide), 2 px gives 3 with the factor and would give 2 without it. The factor keeps the cull at the same true pixel size (inferred from the projection law). |
| 2026-09-24 | Launcher. A modded launch sets `X3M_FOV=58.7155`, and a stale shell value never travels. `--fov game`, `--fov 73.74` → `73.7400`. `--vanilla` drops it and refuses `--fov`. 35.99, 120.01, `Game`, `nan`, `inf` and `90deg` are refused with the bound's reason. The help lists 90 / 106 (21:9 2560×1080; 107 on 3440×1440) / 127 (32:9) horizontal at the default, and explains the game's central-4:3 convention (its 90 = 73.74° vertical). | `python3 tools/manage.py launch --bottle X3 --dry-run` (and `--vanilla`) | exit 0; `X3M_FOV=58.7155`, vanilla none (measured) |

**Failure-only residue and a race judged nil.** `rollback_unprotected` puts the engine's bytes back but
leaves the code page `PAGE_EXECUTE_WRITECOPY` for the rest of the process; it happens only when both
re-protect calls fail. The registry write checks the page with `VirtualQuery` and then does the
`InterlockedCompareExchange`, which leaves a check-to-use window. That window is judged nil: the write
runs once at install, on the backend-load path before the device exists, when nothing frees the registry
(inferred).

**Census summariser.** `tools/analysis/cull_census.py` scales px per s by F/0x4000, taking F from the frame's
`object_matrix role=projection` rows 0 and 1, else the last `cull_small_parts_value … focus=` row, else
0x4000. `--focus` overrides. The frame line prints `focus=`. `test_cull_census` pins the 0x3470 case: for
every s, `s < t` holds exactly when `s·px_per_s < 2`. Logs without row 1 and without a focus row bucket as
before.

**Which value each row reports.** `fov_confirm` reports the base, `registry+0x24`: unzoomed, and the
value the in-game FOV menu writes. `cull_small_parts_value … focus=` reports the view's F derived from the
projection, zoom included, which is the value the threshold uses.

**Record binding.** Both fixture records were rerun on the final worktree tree after the review fixes.
`fov-patch.json` names commit a0a5e645 with `production_sources_dirty=true`: `fov.cpp`, `fov.h` and
`fov_sites.h` are new files. Until the merge commit the record is bound to the worktree diff through the
SHA-256 of each linked source, and `test_fov_patch_result` fails if any of them changes.

**Not rerun.** `run_motion_output.py` was not rerun, because no routed-draw code changed. The
camera reader is untouched. `capture.cpp` gains two calls (`fov::initialize()` on the load path,
`fov::present()` after the first-Present window close), and the seam DLL links every CMake object,
`fov.cpp.obj` included (link inferred, not run).

**Not covered.** Native Windows execution was not run. The source uses documented Win32 only
(`VirtualProtect`, `FlushInstructionCache`, `VirtualQuery`, `InterlockedCompareExchange`,
`GetEnvironmentVariableW`, `WriteFile`) and cross-compiles. A thread running the constructor during the
write was not exercised: the claim rests on the one aligned 8-byte store and the unchanged boundaries
(inferred). The order of the proxy install relative to the first registry creation is still not traced:
the `registry=` field of the install row answers it in flight. Displays narrower than 4:3 are not
corrected (H fixed at 0.75). Not flown.

**Run 81 check (open).** Launch with the default at 5120×1440; no option is needed. The session log
must carry these rows:

- `fov site=0041c9dc status=patched reason=ok value=0x3470 vertical_deg=58.72 setting=58.7155 write=atomic`,
  with `registry=absent` (the constructor path) or `registry=written registry_before=0x4000` (the registry
  already existed).
- `fov_confirm … focus=0x3470 expected=0x3470 match=1` (`camera=skipped`: the sector camera's `+0x298` is
  not read).
- `cull_small_parts_value … m00≈0.5 width=5120 threshold=2 focus=0x3470`, where vanilla has m00 0.375. In
  a zoomed cockpit view a new value row shows the zoomed F (for example `0x1a38` at ×2).

With `X3M_CAMERA_LOG` rows, m00 0.5 and m11 1.778 confirm the projection in flight. The user's look: a
vertical of about 58.7° against vanilla 73.7°, and a horizontal of about 127° against 139°. Check that
target brackets, the lead reticle and mouse aim stay on their objects, that zoom still works, that fog
and sun shadow look unchanged, and whether LOD switches look different. Note whether the in-game FOV menu
overrides the value; that is expected.
