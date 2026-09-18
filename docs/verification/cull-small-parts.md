# Cull small parts: verification ledger

Feature: `--cull-small-parts <px>` / `X3M_CULL_SMALL_PARTS_PX=<px>`,
`src/proxy/cull_small_parts.cpp`, site `0x0047d2a2` of the cull/LOD pass
`0x0047cfe0` with the engine's own cull instruction `0x0047d2c3` as the
target ([lod-selection.md](../reverse-engineering/lod-selection.md), "Cull
small parts site"; [engine-frame-time.md](../architecture/engine-frame-time.md)
2.3). Default off; nothing is written unless the variable parses to a value in
(0, 64], the executable hash matches and the 56-byte window verifies. The
threshold is recomputed per frame from the projection scale and the
back-buffer width; a frame without a valid projection read runs vanilla.

| Date | Check | Command | Result |
| --- | --- | --- | --- |
| 2026-09-18 | Site qualification on the installed EXE (`fdbf3418…`): the 56-byte window `0x0047d294..0x0047d2cc` as 18 whole instructions, two whole instructions of five bytes at the site with the displaced `test` feeding the `je` after `mov eax,[edi+0x1d8]`, the cull target `and dword [edi+0x12c],0xfffffffd; jmp 0x0047d2d1`, no direct branch into the displaced span, incoming sources exactly `0x0047d28c`/`0x0047d297`, the six in-window branches contained, the claim disjoint from `0x0047d258`/`0x0047d528`/`0x0047d44b`, `ret 8`, source constants, encoder, threshold rule 3/6/11 | `python3 verification/probe/verify_cull_small_parts_site.py` | PASS, 16/16 checks, 373 instructions decoded |
| 2026-09-18 | Host tests: verifier on a synthetic image (nine changed-byte/branch refusals), constants and claim disjointness, encoder, threshold rule and the tracked run131 rows reproducing 403/458/479, line parsers, core and census classification compiled with the host compiler, launcher gate (absent or 0 drops the variable, `2` forwards `2.0000`, out of range and sub-0.0001 refused); the census tests with the new verdict; the lod_scale launcher tests | `PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_cull_small_parts verification.analysis.test_cull_census verification.analysis.test_lod_scale_launch` | 26 tests OK |
| 2026-09-18 | X3 CPU fixture: synthetic pass with the three windows byte-exact, the 1,214 census rows of run131 frame 4991 (main view) replayed as nodes with the recorded radius, D, flags, thresholds and limit; the native pass reproduces the engine's verdict of every row (0 mismatches) and, with the census armed, its `s`/measure/D/radius/thresholds/limit (1,214/1,214); threshold 0 identical to native (nodes, EAX/ECX/EDX/EFLAGS); begin_frame through the camera seam (no latch -> 0, invalid projection -> 0, valid -> 3 at 1280, 2 at 1920 after a Reset, back to 3); 2 px: exactly the 97 kept nodes with `s < 3` flip (403 draws), no other byte of any node changes, count 1,147 (the 97 plus the 1,050 the engine culls itself), one `cull_small_parts_frame` row on a capture frame; 4 px: 128 nodes / 458 draws; 8 px: 139 / 479; census and stub armed together: 97 `culled_small` rows with the renderable bit clear, kept 67, `culled_size` 624, `culled_min` 426 unchanged; stub disarmed beside the armed census: native again; callee-saved registers, ESP, empty x87 and LastError preserved; exact rollback and the native pass back; unset/`0` disabled, `abc`/`65` invalid_px, engine site absent bytes_mismatch, changed window byte, null site and wrong cull target refused; closed window late_claim | `python3 verification/probe/build_cull_small_parts.py` then `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_cull_small_parts.py` | 78 checks, 0 failures; `verification/results/cull-small-parts-cpu.json`; bench per 12-node pass: native 0.235 µs, patched disarmed 0.244 µs, patched armed 0.237 µs with 7 of 12 culled (Wine/FEX, harness included, not game FPS) |
| 2026-09-18 | Clean DLL build and the no-x87 walk | `cmake -S . -B build/clean-csp -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build/clean-csp -j4` (fresh directory); `python3 verification/probe/check_no_x87.py build/clean-csp/d3d9.dll` | 0 warnings; PASS, 539 reachable functions, no violations (the stub is emitted bytes, no handler to walk); `d3d9.dll` sha256 `6530db21639d9e09a0ebc8ddc765b03adf408958d36e792a879c6383507717e0` (worktree build, not a candidate) |
| 2026-09-18 | Launcher dry run | `python3 tools/manage.py launch --cull-small-parts 2 --dry-run` | env carries `X3M_CULL_SMALL_PARTS_PX=2.0000` (fixed-point; the DLL parser takes no exponent); no launch |
| 2026-09-18 | Unaffected fixtures rerun (engine_patch neighbours) | `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_object_lifetime.py`; `… run_ownership.py` | object lifetime 674 checks, 0 failures; ownership wrapped 563 checks / baseline 370 checks, 0 failures (`verification/results/bottle-X3/`) |
| 2026-09-19 | Scope option `--cull-small-parts-scope all\|bodies` (`X3M_CULL_SMALL_PARTS_SCOPE`, default `bodies` = parentless nodes only, `[node+0x18] == 0`; stub bytes 27..46 `jne continue; mov eax,[edi+0x1d8]; jmp cull`). Replay: `all` 97 nodes / 403 draws at 2 px, 128 / 458 at 4 px, 139 / 479 at 8 px (unchanged); `bodies` 89 / 395, 120 / 450, 131 / 471, stub count 806 / 837 / 848 (341 parented nodes below the threshold take the engine's compare), registers/ESP/x87/EFLAGS/LastError as native, census rows `culled_small scope=bodies` 89 and `scope=all` 97, bench tree (every sub-threshold node parented) byte-identical to native, unknown scope `invalid_scope`, rollback exact. The rows carry no parent link: parents are proven for `limit > +0x1d8` (50 rows, none in a flip class) and synthetic for rows without a body flag `0x09000000` (`bodies_basis` in the rows file) | `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_cull_small_parts.py`; `python3 verification/probe/verify_cull_small_parts_site.py`; clean build `build/clean-csp-scope` + `check_no_x87.py`; objdump of the emitted `bodies` stub | fixture 113 checks / 0 failures (bench 0.227 native / 0.237 disarmed / 0.234 armed us per 12-node pass, not game FPS); verifier PASS 19/19 (18/18 before `encoder_bodies`; the "16/16" above predates the review fixes); DLL 0 warnings, 539 reachable functions, no x87 violations; stub integer-only; host 78 tests OK; dry run carries `X3M_CULL_SMALL_PARTS_SCOPE=all`, scope without the cull refused |
| 2026-09-19 | **Default 2026-09-19: 2 px, scope `all`.** Run 43 B decided it: at 2 px scope `all` took the busy view from 884 to 477 draws and ~30 to ~42 fps with no visible pop-in, while scope `bodies` culled only 36 nodes per frame and saved nothing, because nearly every small node has a parent. The launcher now forwards `X3M_CULL_SMALL_PARTS_PX=2.0000` and `X3M_CULL_SMALL_PARTS_SCOPE=all` on every modded launch (`--cull-small-parts 0` is the explicit off, `--vanilla` forwards nothing), and the DLL's scope parser takes an absent/empty `X3M_CULL_SMALL_PARTS_SCOPE` as `all`; `bodies` stays selectable and its stub is unchanged. The DLL's own fallback stays off: without the PX variable nothing is patched | `PYTHONPATH=verification/probe python3 -m unittest discover -s verification/analysis -p 'test_*cull*.py'`; `… -p 'test_*launch*.py'`; `python3 tools/manage.py launch --dry-run --hdr --motion-output --ownership --taa --object-trace --object-lifetime` | 37 tests OK and 30 tests OK; modded dry run carries `X3M_CULL_SMALL_PARTS_PX=2.0000` `X3M_CULL_SMALL_PARTS_SCOPE=all`, `--cull-small-parts 0` and `--vanilla` carry neither |

## Open

- Not yet flown. The first session should use `--cull-small-parts 2
  --cull-census` at the run117 station view: expect one `cull_small_parts`
  install line (`patched=1 reason=ok`, `write=atomic` or `plain`), a
  `cull_small_parts_value px=2 m00=0.799999952 width=1280 threshold=3` line at
  the first frame with a valid projection, `cull_small_parts_frame … culled=`
  on capture frames and `verdict=culled_small` rows in the census; the saving
  is `frame_end draws`/`dt_ms` against 901 / 32 ms. Then 4 px.
- The projection scale is read at frame begin from the engine's live buffer;
  run131 logged `p00=0.8` at every Present, so the last activated view of a
  frame is the scene camera there. If another view's projection is ever the
  one latched, the threshold of that frame is scaled by the wrong `m00`; the
  `cull_small_parts_value` lines (at most 16) show any change.
- The stub's `culled=` count includes nodes the engine's own limit or
  degenerate-size test would have culled (1,050 of 1,147 at 2 px in the
  replay); the census rows are the attribution.
- Culling a node clears bit 2 of `+0x12c`, and `0047d055..0047d076` culls a child carrying flag `0x40000` whose parent's bit 2 is clear, so a culled part can take descendants with it: the 403 / 458 / 479 figures (of the 878 census-attributed draws; 901 in the frame) are lower bounds and popping can cascade. The threshold applies in every view, so small casters also leave the shadow and env maps, and the one main-view `m00` scales every view.
- Thin parts (antennas, clamps) are culled by their radius, like the engine's
  own `+0x1d8` cull: popping is the visual risk to watch at 2 px and 4 px.
- Correction to the thin-parts bullet above: with `+0xa0` a bounding radius a long antenna is kept until its whole
  length is under the threshold; compact parts (clamps, lamps) go first (lod-selection.md, "Units").
- Scope `bodies` is pinned on a flag-derived parent assignment, not on measured `+0x18` values; a census flown with
  the new build records the parent link for the verdict but does not print it.
- With `--shadow-caster-retention` a culled static caster keeps casting from the retention store; the script
  occluder list `0x00488aef`/`0x004886a0` loses culled nodes; `camera_state::reset()` is unreachable with only
  this option on (after_reset disarms until the next begin_frame).
