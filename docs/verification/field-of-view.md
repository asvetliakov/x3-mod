# Field of view (`--fov`): verification ledger

Feature: `--fov <vertical degrees 36..120>|game` / `X3M_FOV`, `src/proxy/fov.cpp`, site core
`src/proxy/fov_sites.h` ([field-of-view.md](../reverse-engineering/field-of-view.md) §5, as built §6.1).
The imm32 of the registry constructor's `MOV dword [ESI+0x24],0x4000` at `0x0041c9d9` becomes
`F = round(65536/π·atan(tan(v/2)/0.75))`. The write is one `lock cmpxchg8b` into the aligned qword
`0x0041c9d8..0x0041c9df`, where the imm32 is `0x0041c9dc..0x0041c9df`. If the registry already exists,
`+0x24` gets one validated `InterlockedCompareExchange`. The launcher default is 58.7155° (`F = 0x3470`),
which is 90° horizontal on 16:9, by user decision 2026-09-24. `game` and 73.74 patch nothing. The
small-parts cull threshold carries `F/0x4000`, with F the view's own FOV (base divided by the zoom, the
camera's `+0x298`) taken from the scene view's projection the motion route latches at the scene Clear,
`cot(F/2) = max(0.75·m11, m00)`. That needs no engine read. Without that latch (motion output off, a
multisampled main target, after a Reset, or more than 8 frames without a scene Clear), `registry+0x24` is
the fallback (2026-09-24 entry at the end).
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

**2026-09-24 Run 81 A launch 1 (run309, defaults + `--gpu-sync-timing --shadow-alpha-casters on`): first flight of `--fov`.**
`fov status=patched value=0x3470 registry=absent`, `fov_confirm` frame 4 `focus=0x3470 match=1`; the projection reads
m00 = 0.5 / p11 = 1.7778 (58.71 deg vertical) from frame 318, so the default was in effect (measured,
`verification/results/run309-run81a-launch1/fov_timeline_out.txt`). The user then opened the in-game FOV menu: at frame
1678 F stepped to 91 (0x40b6) and walked 1 deg at a time to 100, down to 70 and back to 100 (0x471c), where it stayed to
the end; the menu starts from its own 90, not from our value, and our value was never restored (measured; the menu path
is under disassembly, §7 when it lands). At F = 0x471c the engine stops submitting the sun's post-HDR draw group (vs
d5e1c753 / ps 8360f422) whenever the sun is within about 30 deg of the view centre (24 present / 2,544 absent frames),
while at 0x3470 (149 / 0) and vanilla 0x4000 the group is present from 2.5 deg outward: the "sun disappears" report is an
engine visibility test failing at the menu maximum, not the proxy (inferred from the correlation; §9 pending).
Chase camera: the boom distance is fixed (about 90,717 units at every F), so the ship is 1.333x larger on screen
(`half_vfov_tan` 0.5625 vs 0.75); compensation goes into the proxy's chase camera (`--chase-distance-scale` exists,
not FOV-aware). Defect: `cull_small_parts` `begin_frame` latched a projection at F = 0x4000 (m00 0.375, `focus=0x4000`)
on every row while `camera_state` had p00 0.3147: the cull reads a HUD/cockpit projection, so the FOV factor never took
effect (harmless here: threshold 3 either way). `fov_confirm` is logged only at frames 0 and 4; nothing tracks later
changes. Alpha casters: 235,343 tested, every `refused_*` 0 incl. `refused_pool`; no shadow_depth cost difference over
matching windows; defaults rows all `default=1`.

**Chase camera compensation (2026-09-24, after run309).** `--chase-fov-compensate on|off` /
`X3M_CHASE_FOV_COMPENSATE` (`1`/`0`; unset = on, anything else is `invalid_tunables`) multiplies the chase boom by
`chase::fov_compensation() = clamp(0.75 / half_vfov_tan, 0.5, 2.0)` (`src/proxy/chase_camera_math.h`), where
`half_vfov_tan` is the per-frame tangent the handler already derives from the camera's `+0x298` and view plane, so the
factor follows `--fov`, the in-game FOV menu and zoom without a restart: 1.0 at vanilla F 0x4000 (0.75), 1.333 at the
58.7155° default (0.5625), 2.0 below 0.375 and 0.5 above 1.5. `--chase-distance-scale` (1.05) multiplies on top; the
position lag clamp is relative to the compensated boom. A factor change moves the boom target without a snap, but
`pos_lag_clamp` (0.10 of the target) bounds the lag, so a step that changes the boom by more than about 10 % jumps in
its first frame to the clamp edge and the spring carries the rest (review, 0.5625 → 0.75: 285.55 → 234.52 the next
frame, target 214.16; 0.5625 → 0.62, a 10.2 % boom change, just reaches the clamp, 285.55 → 284.06, a 1.5-unit
jump; 0.5625 → 0.60 stays inside it, 285.55 → 285.53, lag 18.46 of 26.77; measured by
`verification/results/field-of-view/chase_fov_step.py`, output beside it; all three encoded in
`test_fov_step_at_the_default_lag_clamp`). The
factor follows the live projection: a view that narrows the projection (zoom) moves the boom out, so the ship keeps
its size and only the background zooms. The vanilla external zoom changes the distance, not the FOV
([RE note](../reverse-engineering/field-of-view.md) §8), so the factor is expected to be inert there (unverified). Default on; the launcher sends the variable on every modded launch (inert unless
`X3M_CAMERA=chase`), drops it under `--vanilla` and refuses an explicit value there. The install row carries
`fov_compensate=`; `chase_fov_compensate frame= handler_frame= factor= half_vfov_tan= enabled= distance_scale=
boom_scale= changes= row=n/32` is logged at most once per report window in which the applied factor changed by more
than 1e-4, capped at 32 rows, then one `chase_fov_compensate frame= suppressed=1 changes=` row. An explicit
`--chase-fov-compensate` requires `--camera chase`. Host: `test_chase_camera` 56 tests OK, including the factor at 0.75 / 0.5625, both clamp
bounds, off, non-finite input, the boom distance in the elevated and legacy geometry, and the unsnapped settle after a
0.5625 → 0.75 change; launcher: default on, off, stale value replaced, `--vanilla` sends nothing and refuses an
explicit value (measured); `launch --dry-run --direct --camera chase` sends `X3M_CHASE_FOV_COMPENSATE=1`,
`--chase-fov-compensate off` sends `0`, `--vanilla` none; d3d9 build 0 warnings, no-x87 684/0; Wine
`run_chase_fire` (includes the changed `chase_camera.h`) 4 cases / 307 checks passed (measured). Not yet flown.

**2026-09-24 cull_small_parts FOV source fix (not flown; worktree build, not installed).** The cull's P[0] and F now
come from the scene view's projection. The motion route latches it when it reads the camera at the Background → Scene
Clear (the read behind the `camera_state` rows; `cull_small_parts::note_scene_projection` from `MotionOutput::read_camera`).
That works only while the motion output is on and its selector reaches the scene phase; otherwise (motion output off, or a
multisampled main target) the cull uses the registry fallback. `begin_frame` runs in the Present hook, where the live
buffer holds the frame's last view (the HUD in run309). The latch is used for up to 8 frames (`core::SceneLatch`,
`scene_max_age`), is dropped on Reset, and never replaces the gate: a frame without a valid live projection stays vanilla.
Without a usable latch the registry F (`fov::current_focus()`) is the fallback, with the live projection rescaled to that F
(`core::fallback_m00`: only the aspect is taken from the live view). `cull_small_parts_value` is logged on each change of
threshold, focus, width, source or fallback reason (cap 128, was 16 and also on any m00 change). Both it and
`cull_small_parts_frame` end in `focus=0x.... source=scene|registry fallback=none|no_scene|reset|aged`. `no_scene` means
nothing has been latched since install; `reset` means nothing since a Reset; `aged` means no scene Clear for more than
8 frames.

`focus_from_projection` snaps to 0x4000 within 2 units, because the engine's own projection is about 1e-4 off: run309
vanilla m00 0.3750374 / m11 1.333461 is 16383.0 unrounded. Other F values keep their nearest integer, and the menu's
1-degree steps are 182 units apart. `tools/analysis/cull_census.py` takes each frame's F from its own
`cull_small_parts_frame focus=` row first, then the projection rows, then the last value row.

Per draw: nothing added. Per frame: the unchanged live read, plus one registry read on the fallback path only.

Evidence (all measured):

- Host `test_cull_small_parts` + `test_cull_census`: 31 tests OK. They include:
  - the run309 case: the HUD view (m00 0.375, F 0x4000) is live, then the scene view (0.5, 0x3470) is latched, and the
    scene wins (8 px at 5120 gives 5, not 6);
  - the fallback reasons;
  - the snap (0x3ffc and 0x4003 stay);
  - frame-row focus precedence in the census tool;
  - a source-text check that `read_camera` hands the scene projection over on both camera paths and only at the
    Background → Scene Clear; removing the TAA-path call makes it fail.
- Cull CPU fixture: 153/0 (was 142). It covers HUD-then-scene, `fallback=no_scene|reset|aged`, zoom ×2 on the scene
  (F 0x1a38, 8 px → 5), Reset, an unusable P[5] and an invalid live projection.
- Motion case `seam-taa-camera-on`: exit 0, 177 checks, 85 s including the runner's clean rebuild. It covers the scene
  camera read. The fixture cannot patch the cull site, so it cannot show `source=scene`.
- Worktree build: 0 warnings (incremental and the runner's clean rebuild); no-x87: 684 functions, 0 violations on both
  DLLs (sha256 `a2a1da89…` incremental, `f7d18934…` after the clean rebuild).

The run309 re-evaluation (`verification/results/run309-run81a-launch1/cull_factor_by_fov.py` →
`cull_factor_by_fov_out.txt`, measured) covers 72 scene-projection runs:

- At run309's px 4, every threshold stays 3.
- F 0x3470 (m00 0.5): factor 0.8193, thresholds 2/3/5 at px 2/4/8 against the 2/3/6 applied.
- F 0x471c (m00 0.3147): factor 1.1111, thresholds 2/3/6, as applied.
- F 0x4000 (the vanilla frames 191..316): factor 1.0000.
- Only F ≤ 0x3777 changes the 8 px threshold, in 18 runs.
- All 40 `cull_small_parts_frame` rows applied m00 0.375 at 0x4000.

## 2026-09-24: remap model (`N` horizontal on 16:9, INS_SetFocus site)

The feature changes meaning here: `--fov N|game` / `X3M_FOV` now carry the game's own number `N` (70..100,
launcher default 90), read as "N degrees horizontal on 16:9"; the engine gets `F'` with
`tan(F'/2) = 0.75·tan(F/2)`, `F = (N<<16)/360`, at two sites, both or neither: the constructor imm32
(`F'(N)`, default `0x3470`, the same value as the first build's 58.7155° default) and INS_SetFocus at
`0x0042dbf8`, whose stub remaps the menu's incoming `F` through an 81-entry table (`N` 50..130, user decision;
other values pass through). The first-build rows above keep their vertical-degree wording. Evidence below is
on a worktree based on 9eaeb180 with the change uncommitted: `fov-patch.json` has
`production_sources_dirty=true` and binds `fov.cpp`, `fov.h`, `fov_sites.h`, `engine_patch.*`,
`engine_memory.*` by SHA-256 (`test_fov_patch_result` fails if any changes).

| Date | Check | Command | Result |
| --- | --- | --- | --- |
| 2026-09-24 | Site qualification, both sites. New for `0x0042dbf8`: the 31-byte case window `0x0042dbed..0x0042dc0b` and the 29-byte callee prefix at `0x004a47f0` match. The case body decodes as ten whole instructions (`0x0042dbed … 0x0042dc0c`), with the site one `mov edx,[0x608504]`. The site is qword-aligned (`0x0042dbf8 % 8 = 0`, five jmp bytes in one qword). Jump table `0x0042f064` entry `0x21` is `0x0042dbed`; no entry, raw rel8/rel32 encoding (0 hits) or dword (0) lands inside the case. PUSH/PUSH/MOV/MOV/CALL follow the store, and the callee's mnemonics are `push mov push lea mov cmp jb mov call mov`, so EFLAGS and ECX die there. No overlap with the 162 other claims. The remap table checks out (81 entries; N → F' for 70..100 step 5, 16:9 horizontal = N within 0.01° for 70..100, rounding recovers N for 0..180, range `0x2334..0x5ccc`). The header matches its Python twin. | `python3 verification/probe/verify_fov_site.py` | PASS 29/29 (measured) |
| 2026-09-24 | Identity matrix with the new corrupt case `verify_fov_site@setfocus` (byte `0x0042dbf8` 8b → 8a) | `python3 verification/results/executable-identity/run_verifiers.py --output verification/results/executable-identity/verifiers.json` | PASS: 26/26 on shipped, laa_cleared, ntcore_4gb and unknown_hash; different_build 26/26 FAIL with identity false; six site_corrupt cases FAIL with identity true (measured) |
| 2026-09-24 | Wine fixture on real image pages. `.x3mfvs` at `0x0042d000` now carries the verified case body, entered from a frame builder with sentinels in EBX/ESI/EDI/EDX. `.x3mfvx` at `0x004a4000` carries the verified callee prefix (executed), which records the pushed VM pointer, the task, EDX and the pushed 0. Through `initialize()` at the production VAs: INS_SetFocus with the script's F stores 70 `0x31c7` → `0x2768`, 90 `0x4000` → `0x3470`, 100 `0x471c` → `0x3b6f`, 91 → `0x351f`, non-canonical `0x4001` → `0x3470`. The table edges: N 50 `0x238e` → `0x1b6a` and N 130 `0x5c71` → `0x52ab` remap, N 49 `0x22d8` and N 131 `0x5d27` pass through; the range ends `0x2334`/`0x5ccc` map to the N 50/130 values, and `0x2333`, `0x5ccd`, `0x2000`, `0x71c7` and `0xfffffff0` pass through. Every N 50..130 matches the formula (0 mismatches). Registers, stack balance and LastError are kept on all 24 recorded calls; the 81-entry sweep checks values only. The install writes the jmp to a MEM_PRIVATE `PAGE_EXECUTE_READ` arena address with the rest of the page unchanged and the protection restored; seam reads 7, protects 2, writes 1 atomic, flushes 1. Refusals with nothing written: case, site and callee byte mismatches (`setfocus_mismatch`), 69.9, 100.5 and the old 58.7155 (`out_of_range`), plus the earlier set. A failed jmp read-back restores the site and rolls the immediate back (`refused setfocus_failed`, vanilla store `0x471c` afterwards). The same with the immediate's rollback store dropped stays registered as `patched_unverified` and is restored at shutdown. A foreign jmp over ours gives `restore_not_owned`: the site stays registered and untouched, and restores after our bytes return. Settings 70 / 100 / 72.5 put `0x2768` / `0x3b6f` / `0x28f8` in the constructor, and the menu's 90 still gives `0x3470`. `late_claim` leaves the vanilla store. | `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_fov_patch.py` (record `verification/results/bottle-X3/fov-patch.json`) | 194/194, exit 0, 5.3 s; bottle X3 arm64, FEX_X87REDUCEDPRECISION=1, WINEMSYNC=1; install 875 µs, restore 319 µs, fixture-inclusive one-off costs (measured) |
| 2026-09-24 | Host tests. `test_fov_site` (10) covers the header compiled on the host and the table vs. the Python twin for all 81 entries (rising with N). It checks that every F in `0x2334..0x5ccc` rounds to its nearest N, N 49/131 pass through and 50/130 remap, pass-through at 0, 1, the range edges, `0x8000` and `0xfffffff0`/`0xffffffff`, and the stub bytes and their objdump decode (9 instructions). It also covers the bounds 70..100, the new log-row shapes, the wiring and a verifier FAIL on a copy with `0x0042dbf9` flipped. The launcher cases: default `90`, stale value replaced, `game`, 70, 100, 72.5, `90.0` → `90`, 85.12345 → `85.1235`; 69.99, 100.01 and 58.7155 refused; `--vanilla` drops it and refuses `--fov`; the help lists 43.0/50.5/58.7/67.7 vertical for 70/80/90/100 and 106/107/127 at 90 on wider screens, and says the in-game menu starts from its own 90 whatever `--fov` is (first press = the value of 91 or 89) and then works in the same units. `test_fov_patch_result` (8) checks the new record. | `/usr/bin/python3 verification/probe/run_host_suite.py --modules <the 79 modules that mention manage.py, capture.cpp, fov or the identity matrix>` | 79 modules, 946 tests; 1 failing module, `test_hull_emissive_widening` (`test_dll_plumbing` expects a `motion_output.cpp` line that main's 5a81df27 changed). It fails the same way in the main checkout and this change touches neither file (measured) |
| 2026-09-24 | Small-parts cull CPU fixture: not rerun here. It does not link `fov.cpp`, and main's scene-projection fix (5a81df27) recorded it at 153 checks, 0 failures; this change touches only `fov.h` declarations it includes | — | reused from 5a81df27 (`verification/results/cull-small-parts-cpu.json`, main's record) |
| 2026-09-24 | DLL build (worktree) and the no-x87 walk | `cmake --build build --target d3d9 -j8`; `python3 verification/probe/check_no_x87.py build/d3d9.dll` | 0 warnings (`-Wall -Wextra`; the fixture compiles `fov.cpp` with `-Werror`); PASS, 684 reachable functions, 0 violations (measured) |

**Per-call cost.** The stub runs only on INS_SetFocus: once per menu step, on the script VM thread. It is
nine instructions with no call, lock or memory write, so there is no per-frame work (static). The arena
takes about 240 B per install: a 24 B tail block and a 216 B stub, slot and table block (inferred from the
emitter sizes).

**Not covered.** Native Windows execution was not run; the new code uses only `engine_patch` (documented
`VirtualAlloc`, `VirtualProtect`, `FlushInstructionCache`, `InterlockedCompareExchange64`,
`ReadProcessMemory`) and cross-compiles. A thread executing `0x0042dbf8` during the jmp write was not
exercised; that rests on the one aligned 8-byte store and the install window. Not flown: whether the menu
shows 90 at start and steps in the new model is the next user run.
