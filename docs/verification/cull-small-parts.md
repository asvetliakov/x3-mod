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
| 2026-09-23 | Projectile exemption (Run 75 B: 30–33 of 51–54 bolts per chase-view frame culled at 4 px). `X3M_CULL_SMALL_PARTS_PROJECTILES=on\|off` / `--cull-small-parts-projectiles` (default on, refused without the cull, unknown value `invalid_projectiles`, nothing patched): stub 64 → 82 bytes, `test dword [edi+0x130],0x20000000; jne exempt` at offset 22 after the threshold compare, `exempt: inc [exempt]` falls into `jmp [next]`; `off` = `jmp 34` + int3; scope `bodies` bytes 39..58. The marker is the engine's class-0 (TBullets) root-node bit (`0x004401ae`/`0x00441242`, read by the engine at `0x00488b00`; `lod-selection.md` "Projectile nodes"); `initialize()` pins both marker instructions and installs the exemption off on a mismatch (`projectiles=marker_mismatch`). Rows: `cull_small_parts … projectiles=`, `cull_small_parts_frame … projectiles= exempt_bullet=`, `cull_census_frame … culled_small_exempt_bullet=`; census `Entry` 64 → 68 bytes (`+0x130`), an exempt row is never `culled_small`. Missiles not exempt (no marker). Expected cost: ~31 more bullet instances (~750 primitives) per firing frame. | scratch build `cmake … -DCMAKE_BUILD_TYPE=RelWithDebInfo` + `check_no_x87.py d3d9.dll`; `PYTHONPATH=verification/probe /usr/bin/python3 -m unittest` test_cull_small_parts test_cull_census test_bolt_footprint test_body_table_exe test_draw_accounting test_lod_batch_census test_lod_scale_launch test_launcher_stderr_tee; `verify_cull_small_parts_site.py`; `build_cull_small_parts.py`; `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_cull_small_parts.py` | 0 warnings; x87 PASS, 673 reachable, no violations; 81 tests OK; site verifier PASS 21/21 on the installed EXE (new: `projectile_marker`, `encoder_projectiles`); X3 CPU fixture 132 checks, 0 failures (113 before): every third replay row marked, at 2 px 381 marked rows below the threshold counted exempt and 766 culled (= 1,147), 32 marked kept rows stay kept so 65 nodes / 296 draws flip instead of 97 / 403, census `culled_small=65` and `culled_small_exempt_bullet=381`; `off` flips the full 97 / 403 with `exempt_bullet=0`; registers, ESP, x87, LastError as native; bench 0.2298 / 0.2363 / 0.2393 µs per 12-node pass native / disarmed / armed (harness-inclusive, not game FPS); lock wait ~21 min behind other runners |

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

## Run 43 B (run135-138)

Same busy-station view (~900 draws baseline), one F8 capture per session, `--cull-small-parts <px> --cull-small-parts-scope <scope>`.

| Session | px | scope | culled (capture frames) | draws (capture frames) | dt_p50 near capture (fps) |
| --- | --- | --- | --- | --- | --- |
| run131 (baseline, cull off) | - | - | - | 901 | 32 ms (~31 fps) |
| run135 | 2 | bodies | 36, 36 | 884, 884 | 33638 us bucket (frame 2400) -> ~29.7 fps |
| run136 | 2 | all | 1212, 1256 | 477, 477 | 23684 us bucket (frame 3600) -> ~42.2 fps |
| run137 | 4 | bodies | 23, 23 | 891, 896 | 31101 us bucket (frame 2400) -> ~32.1 fps |
| run138 | 4 | all | 1244, 1274, 1193, 1240 | 447 (all 4 capture frames) | 21408-22212 us buckets (frames 3900/4200) -> ~45-47 fps |

`cull_small_parts_frame` and `motion_output_frame` lines (grep, not read whole):
`cull_small_parts requested=... px=... patched=1 reason=ok site=0x0047d2a2 cull=0x0047d2c3 scope=bodies|all` is the install line in all four sessions (px 2/4 correctly forwarded, scope correctly forwarded).

Answer to Q2: `bodies` does not fail to save time because draws are unchanged and nodes uncalled - it fails because almost none of the small on-screen nodes are actually parentless. Culled counts in `bodies` scope are 23-36 per frame vs 1193-1274 in `all` scope at the same px in the same view; draws drop only 901->884-896 (bodies) vs 901->447-477 (all). The flag-derived parent guess (predicting 395 of 403 culled draws kept in bodies) was wrong in the opposite direction: it isn't that bodies keeps most of the culled set, it's that almost none of the visible small nodes have `[node+0x18]==0` in this view, so the `bodies` scope filter itself, not the engine's downstream culling, is what discards the saving.

Q3 (2px vs 4px, `all` scope): culled count is flat (1212-1256 at 2px vs 1193-1274 at 4px, same order), draws are flat (477 at 2px vs 447 at 4px, both capture pairs), and the fps gain from 2px->4px is real but modest (~42 fps -> ~45-47 fps) - consistent with a few more marginal nodes crossing the larger threshold, not a step change.

Q4: no `cull_small_parts`/`motion_output_frame` error, warn, mismatch or fail lines in any of the four sessions; `apply_failures`/`restore_failures` are 0 throughout. `incomplete=N>0` frame_phases buckets appear only at session startup (frame 300/600) in all four, plus one late bucket in run137 (frame 3300) outside the analyzed capture window - ordinary settling, not evidence of a cull-path fault.

## Run 44 C (run147)

Preserved session `/tmp/x3-bottleX3-run147/session-20260919-054326-212.log` (112 MB, queried with grep only), one F8 at frame 4672-4679. Defaults confirmed: `cull_small_parts requested=2.0000 px=2 patched=1 reason=ok site=0x0047d2a2 cull=0x0047d2c3 write=atomic stub=0x01d707cc camera=disabled scope=all` (single install line); `motion_direct device=1 enabled=1 admission=0 slots=7` (single line, `X3M_TELEMETRY_DRAW=0` per `proxy_options`).

Steady busy view (nearest 300-frame buckets straddling the capture): `frame_phases frame=4500 dt_p50_us=19824 dt_p95_us=20954` and `frame=4800 dt_p50_us=19873 dt_p95_us=22756`, i.e. dt_p50 ≈ 19.8-19.9 ms → ~50.3-50.6 fps, matching the user's ~50 fps observation and running above run136's ~42.2 fps at nearly the same draw count (477).

At the F8 capture frames (4672-4679): `motion_output_frame` shows `draws=478 routed=453 matched=453` on every one of the 8 frames (routed/matched flat, no pop-in signature); `cull_small_parts_frame` shows `culled` 1133/1189/1189/1101/1099/1098/1096/1094 (px=2 threshold=3 scope=all) against the same 478-draw base, consistent with run136's 477-draw/1212-1256-culled pair at the same px/scope.

`gate_us`, `route_draw_us`, `set_rt_us`, `lazy_flush_us`, `jitter_us` are all `0.0` on every `motion_output_frame` line in this session — not valid, because `X3M_TELEMETRY_DRAW=0` (per-draw timing off); only `draws`/`routed`/`matched`/`gate1..6` counts are valid here. No comparison to run129's 9.7 µs proxy-only per-routed-draw figure is possible from this session's fields; that number remains the reference from run129 A (`docs/architecture/engine-frame-time.md` §2.2, `X3M_TELEMETRY_DRAW=1`).

Shadow cost: `sun_shadow_apply_frame` at 4672-4679 gives `applied=1 skip_reason=none us=` 1323.0, 207.3, 99.2, 98.6, 112.3, 104.7, 101.1, 87.1 (first F8 frame elevated, rest ~90-110 µs, `result=00000000 restore=00000000` throughout — no apply/restore failure). Session-wide `sun_shadow_apply_frame` count 7832, mean `us`≈68.3, max 5226.0 (isolated spike, not at the capture frames).

No anomalies: `motion_direct_loss_code` — 0 occurrences; `apply_failures`/`restore_failures` — 0 on all 154 `motion_output_frame` lines; no `refused`/error lines tied to `motion_direct` or `cull_small_parts` (the 49053 `refused` hits are unrelated frame types — `screen_emission_additive_frame`, `fade_route_frame`, `chase_camera` — all `refused=0`/`refused_*=0` in the sampled lines).

**Removed 2026-09-25** (user decision): `--cull-small-parts-scope` (every node is a candidate; the `bodies` branch went from the stub encoder and the census); `docs/verification/launcher-options-inventory.md`, "4. Removed".

Fixture after the removal (2026-09-25, bottle X3): `run_cull_small_parts.py` 119 checks, 0 failures (153 with the scope section; `verification/results/cull-small-parts-cpu.json`) (measured).

## Small props (`--cull-small-props`, 2026-09-29)

Feature: `--cull-small-props on|off` / `X3M_CULL_SMALL_PROPS` / ini `cull_small_props` (default on since 2026-09-29,
user decision; unset or empty means on, `off` turns it off; earlier flights run375-run383 ran it opt-in; the launcher
sends it only when given; needs `X3M_CULL_SMALL_PARTS_PX` and the motion route, so a bare DLL or `--vanilla` launch,
which sends no pixel setting, stays off with `no_px`), `src/proxy/cull_small_props_core.h`,
`src/proxy/motion_output_cull_small_props_inc.h`. Run375 (Mayhem 3, 5120x1440, frame 4400) drew 82 prop draws of 352,
all census `kept`: the split turret props carry a LOD ladder (`lods=3`, at LOD 1) and a node radius `+0xa0` of 80,000,
about nine times their mesh, so the engine's `s` (5-6) stays above the stub's threshold (3) and `--cull-small-parts`
cannot reach them, while their draw boxes are about 2.5 px wide (`object_bounds`). The prop cull therefore measures the
draw, not the node: a main-scene draw (after gate 2, before the jitter and any binding) whose scope node's body path
starts with `ships\props\` (engine body table, case and slash free) and whose drawn vertex range (its POSITION0
AABB from the shadow-replay extent cache, the box the `object_bounds` rows project; since Run 105 A below, the engine's
mesh part AABB `part+0x40`/`+0x50` before it) projects through the draw's own clip rows to a half-side under
`X3M_CULL_SMALL_PARTS_PX` pixels is not forwarded: the hook returns `D3D_OK`, nothing was bound, nothing is undone.
The first draw of a node in a frame decides for all its draws. A prop on the player's ship or on the current target
(cockpit registry `0x608504`, `object_capture::own_ship`/`target`, roots confirmed by handle; parent walk of at most
16 links, 64 walks per frame, cached per node and handle) is always drawn, and so is every prop when either root
cannot be read (`unresolved`), when its part or rows cannot be read, or when its box reaches behind the eye. Why
turret behaviour cannot change: the skip is a D3D call not made, after the engine's cull/LOD pass has finished; the
only value returned to the game is the `D3D_OK` a drawn call returns; no engine byte is patched and no engine field
is written, so the renderable bit, whose one non-render consumer is the script occluder list `0x00488aef`/`0x004886a0`
(lod-selection.md, "Further consequences"), stays the engine's own. The vanilla engine already leaves distant turrets
undrawn: in run375 frame 4400 it culled 101 prop nodes over 15 bodies by its own size and degenerate tests
(`split_m6turretA_*`, `XTC_split_m6aturret_*`, `m6maingun_*`, `ALDG_*`) plus 6 by `--cull-small-parts`, stronger
than this skip (the bit cleared in the pass), and turret aim, fire and hit detection were never reported to change on
flights with the 2-4 px stub default since 2026-09-19 (inferred from the flown ledger; no dedicated turret test).
Rows: `cull_small_props requested= px= configured= reason=` once on every launch (`requested=unset` when the
variable is absent; reasons `ok`, `off`, `invalid`, `no_px`, `route_off`), one
`cull_small_props_device` at the first scene draw, `cull_small_props_frame` every 300 frames with scene draws (every
tier: `draws culled kept nodes_culled kept_size exempt_own exempt_target unresolved deferred no_bounds unbounded
no_scope resolves walks`), on F8 frames one `cull_small_prop` per skipped node, and census rows of such nodes read
`verdict=culled_prop` (frame row `culled_prop_nodes=`). Predicted at 2 and 4 px on the run375 burst: 76 of the 82 prop
draws skipped (352 -> about 276 draws), before the own-ship/target exemption (inferred from the VB-extent
`object_bounds` boxes; the part AABB is a superset; script `verification/results/cull-small-props/run375_prop_rows.py`,
output beside it).

| Date | Check | Command | Result |
| --- | --- | --- | --- |
| 2026-09-29 | X3 CPU fixture extended: census `culled_prop` (only the reported kept node's row, an engine-culled node keeps its verdict, frame row `culled_prop_nodes=2`, a report outside a captured frame dropped); the decision over a synthetic engine image through the production `engine_memory::read`: far prop 1.28 px skipped (second draw shares it), 12.9 px drawn, hull body drawn, `SHIPS/Props/` matched, null and short names not props, own-ship and target props drawn, released descriptor page `no_bounds`, no rows and a box behind the eye `unbounded`, unreadable registry `unresolved`, target cleared -> its prop skipped, 70 new props -> 64 skipped + 6 deferred then 70 | `python3 verification/probe/build_cull_small_parts.py` then `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_cull_small_parts.py` | 140 checks, 0 failures (119 before); per-draw cost (Wine/FEX, harness included, not game FPS): memo hit 3.0 ns, first draw of a non-prop node 18.6 ns, first prop of a frame skipped 314 ns (includes the once-per-frame own-ship/target resolution), mean of two skipped props in a frame 207 ns (measured); `verification/results/cull-small-parts-cpu.json` |
| 2026-09-29 | Host: core compiled with the host compiler (option parse, prefix, part box, radius, decisions incl. a reused node address re-walked by handle, wrong body-table count, window counts), census `with_prop`/`sorted_contains`, draw-path wiring pinned (after gate 2, before the jitter, `D3D_OK`, no engine patch), launcher (sent only when given, inherited value dropped, refused with `--cull-small-parts 0` and under `--vanilla`), schema, logging tiers | `PYTHONPATH=verification/probe:verification/analysis /usr/bin/python3 -m unittest test_cull_small_parts test_cull_census test_logging_tiers test_config_schema`; `python3 tools/config/generate.py --check` | 60 tests OK; generate PASS 243 settings, 95 in the template |
| 2026-09-29 | DLL build and x87 walk (worktree build, not a candidate) | `cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPython3_EXECUTABLE=/usr/bin/python3 && cmake --build build -j8`; `python3 verification/probe/check_no_x87.py build/d3d9.dll` | 0 warnings (clean build); PASS, 733 reachable functions, 0 violations; `MotionOutput::cull_small_prop` carries no SJLJ registration and no x87 (objdump) |

Open: not flown. The skip covers main-scene draws only (draws outside the latched scene pass, such as the engine's
env-map views, are untouched; a skipped draw is no shadow-replay candidate, so the prop also leaves the cascades); a node is
skipped whole, so a prop with one small and one large part is decided by its first drawn part; the class cache is
flushed every 300 frames (a body id reused after a reload is re-read within that window); the `culled_prop` verdict
applies to every census view of the node although only its main-scene draws were skipped.

### Run 105 A (run376): the engine part box does not bound the drawn turret

run376 (Run105, `X3M_CULL_SMALL_PROPS=on`, 4 px, same battle group) logged `cull_small_props_frame` from frame 2555
on with `draws=24600 culled=0 kept_size=24600` per 300 frames (82 prop draws a frame, all drawn for size); only the
first windows skipped 82-600 draws (measured, `grep cull_small_props_frame`). The F8 burst (frame 3262, script
`verification/results/cull-small-props/run376_prop_draws.py`, output beside it): 82 prop draws, the drawn range's
`object_bounds` half-side 0.85-1.20 px for the `split_m1turretB_socket` (24 draws), 1.35-1.60 for `_base` (24),
1.15-1.35 for the A pair (24), 1.15-2.10 for the m7 pair (4), 32-37 px for the six `weapondummy` (the own/near ship,
s = 51-52); 76 under 4 px (measured). The scope descriptor is one per (body, LOD) over every node (7 descriptors for 82
draws: the per-part descriptor render-node-bounds.md 4 predicts), so the node and its part were found; what the build
projected was the part's `+0x40`/`+0x50` box, and with no socket culled at 0.85 px that box is at least 4 / 0.85 =
4.7 times the drawn range for every turret (inferred from the counts; the box itself was not logged). That box was
established statically for the six asteroid fade pairs (render-node-bounds.md 2, from the load path that recomputes it
from the vertices); the alternate container branch of `0x00481aa0` copies the six fields from the file, and a modded
body's stored box (or its units) need not match its vertices, which the run376 numbers show for Mayhem's turrets.

Fix (2026-09-29): the decision takes the drawn range's own POSITION0 AABB from the shadow-replay extent cache (the same
source the run375/run376 prediction used): key = stream-0 buffer, its bookend revision, offset, stride, position
offset/type and the drawn vertex range, asked for prop draws only; a missing extent is queued for the scene-end read
the shadow replay already runs (so a skipped prop keeps its extent) and the draw is kept that frame (`no_bounds`); a
young extent of an earlier revision stands in, as in the caster verdict. Without the candidate reads (shadow replay
off), a non-managed buffer or an unreadable range nothing is skipped. F8 frames carry one `cull_small_prop_box` row per
prop node (cap 128) with the decision's `extent_px` and the engine part box's `part_px` and both boxes, to close the
gap question on the next flight. `frame_end` appends `issued=` (the draws that reached the device) and the overlay's
DRAWS figure shows issued draws ([draw-calls.md](draw-calls.md)).

| Date | Check | Command | Result |
| --- | --- | --- | --- |
| 2026-09-29 | CPU fixture with the regression case: the engine part box behind the descriptor projects to 11.6 px (above 4) while the draw's extent is 1.28 px, and the prop is skipped; unknown extent drawn (`no_bounds`); the rest as before | `python3 verification/probe/build_cull_small_parts.py` then `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_cull_small_parts.py` | 141 checks, 0 failures; per draw memo hit 3.0 ns, first non-prop 14.2 ns, first skipped prop of a frame 287 ns, pair mean 171 ns (Wine/FEX, harness included; the production extent lookup is not in this figure) |
| 2026-09-29 | Host: harness with the 9x engine box (11.6 px) beside the unit extent, extent asked for prop draws only, draw-path wiring (extent cache find/queue, `cull_small_prop_box`), `frame_end issued=` and the overlay's issued figure pinned | `PYTHONPATH=verification/probe:verification/analysis /usr/bin/python3 -m unittest test_cull_small_parts test_cull_census test_logging_tiers test_config_schema test_fps_overlay test_frame_timing test_exe_identity`; `python3 tools/config/generate.py --check` | all OK; generate PASS 243 settings |
| 2026-09-29 | Clean DLL build, x87 walk | `cmake … -DPython3_EXECUTABLE=/usr/bin/python3 && cmake --build build -j8`; `check_no_x87.py build/d3d9.dll` | 0 warnings; PASS, 734 reachable, 0 violations; `cull_small_prop` and `small_prop_extent` without SJLJ or x87 (objdump) |
| 2026-09-29 | The 0x0047d2a2 claim is shared with the lens-flare cull (`cull_small_parts::chain_stub`, `site_claimed`; `install_at` chains its stub on the same claim, `shutdown()` restores once for both): the fixture's lens section installs the lens stub alone, then both in both chain orders, and the small-parts checks are unchanged | `python3 verification/probe/build_cull_small_parts.py` then `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_cull_small_parts.py` | 200 checks, 0 failures (141 small-parts/props as before + 58 in the lens section + the late-claim refusal of the lens stub); bench native 0.2264 / disarmed 0.2353 / armed 0.2273 us per 12-node pass (Wine/FEX, not game FPS); `install_at` now checks, emits, then claims (an arena refusal leaves the site untouched) and the claimed path re-checks the window and the cull target; [sun-occlusion.md](sun-occlusion.md), "lens-flare gain 0 culls in the engine" |

## Dock ports (`--cull-dock-parts`, 2026-09-29)

User decision 2026-09-29, option 1 of [ship-scene-parts.md](../reverse-engineering/ship-scene-parts.md) §4: a second,
larger threshold in the same stub for the carrier dock-port parts, the inline bodies of the stock dock cut scenes
9013/9014 and 9098/9099 (model id `node+0x140` in `[901300000, 901499999]` or `[909800000, 909999999]`, id = local +
(cut − 1)·100000). `X3M_CULL_DOCK_PARTS_PX` (ini `cull_dock_parts_px`, launcher `--cull-dock-parts <PX>`, default 12
with every non-zero `--cull-small-parts`, 0 = off and not sent, refused without the cull, band (0, 64]) is converted
to the pass's `s` exactly as `X3M_CULL_SMALL_PARTS_PX` (same `threshold_for`, same projection, width and focus, same
frame). The stub compares `s` first against `upper = max(threshold, dock threshold)` (0 in a vanilla frame), so a node
at or above both runs the same eight instructions as before; below the small threshold every node is culled as
before (three more instructions); between the two only a dock-port id is culled, through `mov eax,[edi+0x140]` and two
`sub`/`cmp`/`jb` range tests. The projectile exemption applies to the dock path too (a dock part never carries the
marker; the rule stays consistent with it); there is no own-ship or target exemption in the stub, and none is added
(the player's ship shows no dock ports from inside). Stub 82 → 147 bytes (`cull_small_parts_core.h`), same site,
window, verified bytes and fail-closed install; an invalid dock value refuses the whole install
(`reason=invalid_dock_px`, nothing patched). Rows: `cull_small_parts … dock_px= dock_requested=`,
`cull_small_parts_value … dock_px= dock_threshold=`, `cull_small_parts_frame … dock_px= dock_threshold= dock_culled=`
(F8 frames; `culled=` stays the small-rule count, `dock_culled=` the nodes the dock rule added), census verdict
`culled_dock`. The stub has no per-300-frame row; a flight counts the dock rule on F8 frames under `--debug`.

**Default: 12 px** (set 2026-09-29, first briefed as 8). The note's "s < 8 removes 159/27/27" is in `s` units: at
run385's projection (`m00` 0.5, 5120 wide, focus `0x3470`: 1.639 px per `s`) 12 screen px is the engine's `s < 8` and
reproduces 159/27/27 removed draws (measured replay below); 8 px would be `s < 5` there and remove 64/2/25.

| Date | Check | Command | Result |
| --- | --- | --- | --- |
| 2026-09-29 | X3 CPU fixture: 18-node dock tree (D 64000, W 1280, s = radius/100) at 4 px / 8 px, m00 0.8 at 1280 → thresholds 5 / 10: exactly the 7 in-range nodes with 5 ≤ s < 10 (both bounds of both ranges, 901300003, 901400003, 909900005) and the 3 nodes below 5 flip; 901299999, 901500000, 909799999, 910000000 at s 7, an in-range node at s 10 and a marked in-range node (exempt) as native; culled=3, dock_culled=7, exempt=1; value and frame rows; census `culled_dock` 7 / `culled_small` 3 / kept 8; dock 0 and dock 2 px (below 4 px): the 4 px rule only; after_reset disarms both words; projectiles off: the marked node dock-culled (8); registers, ESP, x87, EAX/ECX/EDX/EFLAGS and LastError as native; after rollback native; the run131 replay classes unchanged (97/403, 128/458, 139/479) | `python3 verification/probe/build_cull_small_parts.py` then `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_cull_small_parts.py` | 162 checks, 0 failures (141 before); bench per 12-node pass native 0.2268, disarmed 0.2366, armed 0.2326, armed with the dock rule (upper 40, no dock ids) 0.2319 µs (Wine/FEX, harness included, not game FPS) |
| 2026-09-29 | Stub instructions per node, before (`c7917cfc`) and after | `python3 verification/results/ship-scene-parts/dock_stub_paths.py --before c7917cfc` ([output](../../verification/results/ship-scene-parts/dock_stub_paths_out.txt)) | disarmed 3 → 3; s ≥ upper 8 → 8; small cull (no parent) 15 → 18; threshold ≤ s < upper, not a dock id 8 → 18; dock cull 23 (first range) / 26 (M6 range) |
| 2026-09-29 | Replay of the rule on the run385 census rows with each frame's own `cull_small_parts_frame` projection | `python3 verification/results/ship-scene-parts/dock_part_rules.py /tmp/x3-bottleX3-run385/session-20260929-173412-212.log` ([output](../../verification/results/ship-scene-parts/dock_part_rules_out.txt)) | 4 px → threshold 3; dock 8 px → `s < 5`: 28/2/1 nodes, 64/2/25 draws removed in frames 4827/8142/10210; dock 12 px → `s < 8`: 39/3/3 nodes, 159/27/27 draws |
| 2026-09-29 | Site verifier on the installed EXE (site checks unchanged; its encoder twin, constants and `encoder_projectiles` updated to the 147-byte stub) | `python3 verification/probe/verify_cull_small_parts_site.py` | PASS 20/20, EXE `fdbf3418…` |
| 2026-09-29 | Host: C++ and Python encoders byte for byte (both marker tests, both range tests, `projectiles off`), `dock_model` bounds, `upper_for`, census `culled_dock`, the row parsers with the dock fields, launcher default/explicit/0/refusals, schema opt-outs, default-launch environment | `PYTHONPATH=verification/probe:verification/analysis python3 -m unittest test_cull_small_parts test_config_schema test_launcher_defaults test_logging_tiers test_cull_census test_body_table_exe test_fov_site test_terran_lod_site`; `python3 tools/config/generate.py --check` | 96 tests OK; generate PASS 245 settings, 97 in the template |
| 2026-09-29 | DLL build and x87 walk; launcher dry run | `cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPython3_EXECUTABLE=/usr/bin/python3 && cmake --build build -j8`; `check_no_x87.py build/d3d9.dll`; `python3 tools/manage.py launch --dry-run [--cull-dock-parts 0]` | 0 warnings; PASS, 734 reachable, 0 violations (worktree build `3f95b47a…`, not a candidate); dry run carries `X3M_CULL_DOCK_PARTS_PX=8.0000` (the first commit; the default became 12 before the merge, dry run then `12.0000`), with `--cull-dock-parts 0` not |
| 2026-09-29 | Merged with the lens-flare cull (`b36e8115`, shared claim, lens stub chained on it; the lens section's direct threshold writes also arm `upper`); default 12 px | `build_cull_small_parts.py`, `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_cull_small_parts.py`; `verify_cull_small_parts_site.py`, `verify_lens_flare_cull_site.py`; `generate.py --check`; `unittest test_cull_small_parts test_lens_flare_cull test_config_schema test_launcher_defaults test_logging_tiers test_cull_census`; DLL build, `check_no_x87.py` | fixture 221 checks, 0 failures (lens 58/0; dock line unchanged: culled=3 dock_culled=7 exempt=1, census culled_dock 7); bench 0.2352 / 0.2363 / 0.2338 / 0.2334 µs native / disarmed / armed / armed dock; verifiers PASS 20/20 and 13/13; generate PASS 245 settings; 80 tests OK; 0 warnings; x87 PASS 734 reachable, 0 violations (worktree build `0d710594…`, not a candidate) |

Open: not flown. Whether the hangar interior shows through the open bay before it is culled (note, "Unknown") is a
flight question; the frame row's `dock_culled=` and the census `culled_dock` rows count it.

### Dock ports: flight Run 110 A (run386/run388, 2026-09-29)

Measured (300-frame windows from the `media_cue_window` clock stamps, one run per setting, same save and carrier view,
no `--perf`; [results](../../verification/results/run386-389-cull-ab/)): defaults (dock cull 12 px) 17.43 / 17.56 / 18.10 ms
per frame against dock cull off 19.77 / 19.82 / 21.23 ms, i.e. 2.4-3.7 ms saved (about 57 fps vs 47-50); right after
load, where no dock part is in range, the runs agree within 0.5 ms. run389 (`--debug --perf`, three F8): `dock_culled=`
equals the census `culled_dock` on every burst frame (27-33 nodes per frame at the Raptor at 4.0 km), the largest culled
part had s=7 (below the threshold 8), one carrier node stayed drawn at s=12 as the rule requires. Accepted; default 12 stays.
