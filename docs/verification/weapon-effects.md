# Weapon effects: verification ledger

Feature: `X3M_WEAPON_EFFECTS=native|observe|shields` (unset = `native`; config key `weapon_effects`, launcher
`--weapon-effects`), the steps of [weapon-effects.md](../architecture/weapon-effects.md) section 5. Step 1 (the shield
hit): the impact redirect at `0x00449d85` and the observe taps `0x00413da8` / `0x00413e79`
(`src/proxy/weapon_effects_{sites.h,patch.cpp,hook.cpp,core.h}`), the stage builder `src/proxy/weapon_effects_stage.h`,
the `WeaponEffectsPass` (`src/renderer/weapon_effects_pass.cpp`), the table `<game>/x3m/weapon_effects.json`
(`tools/effects/weapon_effects_table.py`, reader `src/proxy/weapon_effects_table.h`) and the lab
(`tools/effects/weapon_effects_lab.html`). AP only; the FL edition compiles the families out (one
`edition_unsupported` row). Every figure below is measured unless marked otherwise. Records:
`verification/results/native-windows/weapon-effects/{hook-summary,summary}.json` (host block: Windows 10.0.26300.9818,
NVIDIA GeForce RTX 5070 Ti, driver 32.0.16.2114, the system d3d9.dll and `C:\X3\d3dx9_37.dll`); the quoted rows come
from `verification/results/native-windows/weapon-effects/ledger_rows.py`.

| Date | Change | Command | Result |
| --- | --- | --- | --- |
| 2026-11-05 | Step 1 implemented (commit 2fc8762ce): impact hook (claim_call, integer handler, 256-event ring, modes), classifier and generated table, `WeaponEffectsPass` (shell and decal programs revived from 9375a1e7) chained after the plumes callback, lab `renderOurs`, launcher option. | `python3 verification/probe/wine_lock.py python3 verification/probe/run_weapon_effects.py --host native` | D3D fixture 13/13 PASS natively: rim 0.686 at 70° vs 0.101 at 45°, rings on 16.05 / off 13.39, hull cut 0, decal 19.95 on hull / 0 over sky, retired draws nothing, fault and Reset; 2 shells 80.9 µs at 1080p and 271.6 µs at 5120x1440 (`CULLMODE NONE`, fenced). Hook fixture 33/33 natively but built and run by hand: no tracked record. Not flown. |
| 2026-11-05 | Review of step 1 and its fixes. **F1** the hook fixture is run by `run_weapon_effects.py --host native` (build + audit via `build_weapon_effects_hook.py`, `native_host.run_native`, every CHECK and the RESULT parsed into `hook-summary.json` with commit, source hashes, audit and host block; `test_weapon_effects_hook.py` binds both records). **F7** forward path: EAX/ECX/EDX/EBX/EBP sentinels, return address, ESP and EFLAGS (CF PF ZF SF OF DF) at the callee; the tap's ESI (`[ebx+8]`), EBX, EDI and return; witness and prologue mismatch, tap byte mismatch, observe rollback over three sites and over two, the no-projectile class. **F4** shells cull the inward winding per shell from det A (the icosphere's faces are outward-CCW; `CULLCW` / `CULLCCW` / `CULLNONE` required at attach; decals stay `NONE`); fixture fault bit 4 keeps the former `NONE` draw for a same-run A/B. **F3** `stage::build` host-tested; the D3D retired case goes through the builder with a dying owner. **F8** ownership latched by `core::stage_owned` (armed, resolve completed, callback ran and returned `S_OK`/`S_FALSE`). **F9** `[[source+0x50]+4]` no longer read (`shooter` = 0). **F10** `core::impact` / `core::tap` `static inline always_inline`; audit: no call in either entry point. **F11** `flag_nested` from the dying bit of either object (a dying hit object forwards); ring scope clears on a load, a sector-key change or a load-epoch change (`core::ScopeLatch`, `weapon_effects_scope` row); the table reader refuses trailing bytes and duplicate rows. **F12** the FL build writes `weapon_effects_patch … state=edition_unsupported edition=fl` once. **F5** SETA 6 measured through the builder and the pass. **F6** core through the production `TemporalPass`, full-screen shell timing, program-create refusal (fault bit 3). **F2** this ledger and the native-cost line in platform-portability.md. Lab slot labelled `impact: shield`. Found while fixing: the hook fixture's synthetic frame left 0x80 bytes of stack below it; the handler (frame 140 B + stub 36 B) overran it into the fixture's own `.bss` and the process exited 5 after `RESULT PASS`; the margin is now 0x600 and a canary check measures the depth. | `python3 verification/probe/wine_lock.py python3 verification/probe/run_weapon_effects.py --host native` (6.5 s); `PYTHONPATH=verification/probe python3 -m unittest discover -s verification/analysis -p 'test_weapon_effects_*.py'`; `cmake --build build --clean-first -j4 --target d3d9`; FL `cmake -DX3M_EDITION=fl` build; `python3 verification/probe/check_no_x87.py --edition ap build/d3d9.dll`; `python3 verification/results/weapon-effects/x87_objects.py build`; `python3 tools/effects/generate_weapon_effects_lab.py --check` | Hook fixture 52/52 PASS natively, exit 0; stack depth 176 B of a 1536 B margin; audit: handler x87 0, undefined 0, calls 0 in `_x3m_weapon_event` (607 instructions) and `_x3m_weapon_tap` (58). D3D fixture 20/20 PASS natively: rim, rings, cut and decal unchanged (0.686 / 0.101, 16.05 / 13.39, 0, 19.95 / 0); a mirrored shell 35.98 = the unmirrored 35.98; resolve core 0.997 / 1.000 / 0.996 at 0 / 4 / 8 px per frame (gate 0.95; the dropped Wine ledger had 1.000); SETA 6: drawn until 100.15 ms, nothing at 100.18 ms of wall time (3748 frames, longest 1.11 ms); retired: live owner 1 shell drawn (16.04), dying owner 0 shells, 1 retired, ring empty, `S_FALSE`, nothing drawn; program create refused `E_FAIL`, reason `program_create`, 0 references, run `E_INVALIDARG` with 0 calls. Timing, best of 60 fenced (µs; empty fence 10.8 / 10.2): 1080p two shells 61.3 (`NONE` 80.8), full-screen 43.7 (57.4); 5120x1440 two shells **201.8** (`NONE` 269.2; first record 271.6), full-screen 151.9 (197.2). The two-shell case's 400-unit shell at z 500 covers the whole 5120x1440 frame, so it is a full-screen shell plus a 150-unit one. **The 0.2 ms flight target at 5120x1440 is still not met by the fenced figure (201.8 µs; 191.6 µs net of the empty fence, inferred by subtraction)**; the cull saves 23–25 % in every same-run pair. Host tests 56 OK. AP DLL clean, 0 warnings (1:10 at -j4), sha256 ff15c61d…; FL DLL 0 warnings. x87: check_no_x87 PASS (1161 reachable, 0 violations); objects: hook x87 0 undefined 0, patch x87 0, pass x87 0, motion_output weapon functions x87 0. Lab check PASS. Dry run: `test_weapon_effects_launch` (observe / shields forward `X3M_WEAPON_EFFECTS` and the table path, `--vanilla` refuses); a real `--dry-run --game-dir /mnt/c/X3` refuses with "Install the proxy before launch" (no proxy installed there; not changed). Not verified: F8 and the scope sampling in the proxy (no motion-route fixture drives the weapon stage; the rules are host-tested as `core::stage_owned` / `core::ScopeLatch` and the hook fixture drives `present()`), not flown. |
| 2026-11-06 | Shield-hit look of 5.1.1 (dirty on effects `604a742c1`): the whole-ship shell becomes a local hex patch on the owner's ellipsoid. Hook: the hit object's shield fraction (spawn note §9: class-9 list to its sentinel, ≤ 64 nodes, TShields index bounded by `*0x0060705c`, energy `[node+0x50]+4`, capacity `+0x48`, capacity-weighted like the HUD bar, any bad node refuses; 64-bit sums, one 32-bit division, no `__udivdi3`) and the patch tier (TShips class id `+0xc8`, docks/factories capital, else radius bands) in the four bytes the unread `shooter` held; `flag_stage` (the stage draws) separated from `flag_forwarded` (the native spawn ran). **User decision 2026-11-06:** a stage-owned shield hit forwards the native impact sprite and the patch is drawn over it; the skip stays behind `core::suppress_native_shield_hit` (default `false`). Stage: per owner the 4 newest hits, one draw per icosahedron anchor region (stereographic lattice fixed on the owner), CPU cap per draw (8 x 32 polar mesh bent by the VS), decal fallback for an owner without an ellipsoid; colour from the newest hit's shield fraction (blue ≥ 0.75, yellow 0.5, red ≤ 0.35, smoothstep), weapon tint an override at 0. Shaders: `weapon_hex_patch.hlsl` shared by the patch and decal PS. Lab: the impact slot renders the same law in JS with the 5.1.1 controls. | `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_weapon_effects.py --host native` (11 s); `PYTHONPATH=verification/probe python3 -m unittest discover -s verification/analysis -p 'test_weapon_effects_*.py'`; `python3 tools/effects/generate_weapon_effects_lab.py --check`; `cmake --build build --clean-first -j4 --target d3d9` (RelWithDebInfo) and the FL build; `python3 verification/probe/check_no_x87.py --edition ap build/d3d9.dll`; `python3 verification/results/weapon-effects/x87_objects.py build`; figures from `verification/results/native-windows/weapon-effects/ledger_rows.py` | Hook fixture 68/68 PASS natively (exit 0): forward by default to the callee with registers, flags, ESP and return intact; skip under `suppress_native` keeps the frame; shield read 0.309865 against the HUD formula's 0.309867 (mean of the fractions 0.541), 2×2e9-point shields 0.624979 (0.625), clamped energy, sentinel, index out of range / negative, null extensions, 64-node bound, unshielded class, bit 20, tier by class id / station / radius; 50 events on a six-shield ship 17.7 µs median (best 17.6, worst 34.1) through the whole window against the 30 µs target; stack 204 B of 1536; audit: x87 0, undefined 0, calls 0 (`_x3m_weapon_event` 904 instructions, frame 168 B). D3D fixture 27/27 PASS natively: programs patch VS 51 / PS 291 slots, decal VS 21 / PS 143; per tier (fighter / corvette / capital) the lit radius 428.3 / 447.5 / 461.4 px against the patch radius 428.2 / 447.2 / 461.0 px, 0 outside, edges 12 / 16 / 20 (2R/cell 12 / 16.7 / 20); core 3.574 at 0.05 s against 0.576 without it, 0.3799 = 0.3799 at 0.2 s; ring peak 157.5 / 303.5 px against 157.9 / 303.7 expected at 0.1 / 0.2 s, cells lit to 119.5 / 268.5 px; the patch's radiance falls 274101 → 231743 → … → 1.9 at 1.7 s → 0 at the 1741 ms lifetime; colours at shield 0.9 / 0.5 / 0.2 (0.10, 0.17, 0.23) / (0.23, 0.20, 0.10) / (0.23, 0.09, 0.08); re-light 0.3298 = max(0.1299, 0.3298); four hits light 963593 px against 489283 for the largest single, fourth slot 0.183; the cap (0.723 rad) against a 0.25 rad wider one 0.19 % apart; decal: 0 patches, 1 decal, 0 over sky, 79297 on the hull; SETA 6: the 1741 ms record drawn until 289.68 ms, nothing at 290.69 ms (expected 290.17); retired owner 0 draws, ring empty, `S_FALSE`; resolve core 0.998 / 0.999 / 0.997 at 0 / 4 / 8 px per frame; program-create refusal, fault, Reset and a draw after it. The lab's JS law at the hit point equals the GPU readbacks within FP16 (3.5761 / 0.5761 / 0.3800 / 0.1300 / 0.3300). Timing, best of 60 fenced (µs, 1080p / 5120x1440; empty fence 11.2 / 11.1): two patches on a fighter at 100 m 18.8 / 33.0 (one region, one draw) and 18.1 / 26.9 (two regions); two on a capital at 100 m from its surface (each over the whole frame) 50.0 / 129.5 (one region) and 68.7 / 196.1 (two); the worst one owner can submit (four regions, four full-frame draws) 116.4 / 374.2. **The fighter-fight target (0.2 ms at 5120x1440) is met with margin (33.0 µs fenced, about 22 µs net of the fence, inferred);** a capital at close range costs a full-frame pass per region (the full shell's full-screen case was 151.9 µs) and the four-region worst case exceeds the target. Host tests 60 OK; lab check PASS; AP DLL 0 warnings (1:08 at -j4, sha256 f1432a78…), FL DLL 0 warnings; check_no_x87 PASS (1161 reachable, 0 violations); objects: hook x87 0 undefined 0, patch x87 0, pass x87 0, motion_output weapon functions x87 0. Not verified: the shield read and the tier against a live game (observe flight), not flown. |
| 2026-11-06 | Cell-map shield patch of 5.1.1 (user decisions 2026-11-06; dirty on `728ff2f0e`, effects `06179f0c2` merged): the per-hit disc and the four-hit slots are replaced by a stable hex grid on the owner (six axis-centred anchor regions) and a per-cell fade clock; a hit lights its cell and `rings` = 1 / 2 / 2 neighbour rings (fighter / corvette / capital, 7 / 19 / 19 cells), a shared cell resets to the newest clock, never adds; the stage rebuilds each region's map (up to 64 hits per owner, 64 cells per draw) from the live hits every frame; the boundary is hexagon edges; ring and core stay per hit (newest four in their ring); fade 1200 -> 2200 ms (lifetime 1741 -> 2941 ms); the pixel program intersects the eye ray with the ellipsoid per pixel (the affinely interpolated facet point made border pixels change cells as the cap changed: the first 12 hits/s run dropped 1309 pixels by up to 1.86) at the pixel's snapped position; `feather_cells` and `radius_m` removed. Lab: the half circle was the preview's ray-sphere intersection (`b = -D rd.z` took the root behind the camera, so pixels mapped off the unit sphere; the lit set came out as the lower half, centroid y 173 of 270); fixed, the patch centred and whole, rings controls, the 12 hits/s map, the automatic loop now max(longest vanilla slot, our lifetime / SETA + 250 ms) (before: the vanilla slots only, at least 500 ms). Doc 2.2 table: no `shooter`, its bytes carry `shield_q` and the tier. | `python3 verification/probe/wine_lock.py python3 verification/probe/run_weapon_effects.py --host native`; `PYTHONPATH=verification/probe python3 -m unittest discover -s verification/analysis -p 'test_weapon_effects_*.py'`; `python3 tools/effects/generate_weapon_effects_lab.py --check`; AP DLL `cmake --build <dir> -j4` + `check_no_x87.py`; figures: `python3 verification/results/native-windows/weapon-effects/ledger_rows.py` | Hook record PASS 68/0 (unchanged sources); D3D record PASS 32/0 [M]: patch program 317 slots (was 291), decal 135; per tier 7 / 19 / 19 cells, lit extent 121.6 / 144.5 / 120.9 px inside the cell-edge bound 125.1 / 147.4 / 123.4 px and past the outermost centres 79.7 / 114.7 / 95.8 px, 0 outside; both sides in both axes (fighter left/right/up/down 19065 / 19248 / 19037 / 19276 px; before the change, old law: 268561 / 269396 / 268580 / 269377, the GPU patch was never a half); adjacent hit: 10 cells, the first footprint's own cell keeps 0.3831, the shared cells take the second hit's 0.3979 / 0.3809, peak 2.0039 = the single hit's; grid identical on a moved and rotated owner (22 cells); 12 hits/s for 4 s (36 live hits, 36 cells): largest per-pixel frame drop 0.0215 against one fade step 0.0227, 0 pixels over; one cell's fade 1.7596 s against its hash's 1.7600 s (range 1.76 to 2.64 s); SETA 6: the last lit cell (2840 game ms) ends at 473.5 ms wall (expected 473.3); cap = wider = whole sphere (146254 px). Timing at 5120x1440, best of 60 fenced: fence 10.7, fighter two hits 18.1 us (was 32.3), fighter two regions 18.4, capital close two hits 106.9 (was 128.5), two regions 113.3 (was 192.9), worst four regions 173.8 (was 360.8), fighter 12 hits/s map 21.8, capital close full 64-cell map 233.6 (new; the map loop over a frame-filling cap); 1080p 15.5 / 39.0 / 54.8 / 78.5. 62 host tests OK; lab check PASS; AP DLL 0 warnings, no-x87 PASS (1161 functions). Not flown. |
| 2026-11-06 | Review of the cell-map patch (`e0e81b8a6`) F1-F11 and the user's marker decision (dirty on `e0e81b8a6`). **F1** a region's lit cells are gathered once (open-addressed table, newest clock and its hit per cell), kept whole when they fit its draws, else the newest clocks (the oldest, dimmest, dropped: `cells_dropped`); 128 cells per map (pixel c2..c91 of 224: head c2..c18, projection/A^-1/eye c19..c23, seam c24..c27, keys c28..c59, clocks c60..c91, literals c92..c98); a region past one map spills into further draws (`region_draws` 8, from the frame's 32 after every region's first, Morton chunks with their own caps); live hits past `owner_hits` are `hits_dropped`; `weapon_effects_frame` adds `dropped= cells_dropped= hits_dropped= spills=`. The map loop: this d3dx compiles a rolled `keys[k]` into 64 compares per iteration (no `aL`), now eight unrolled groups behind a uniform branch, all 32 key registers uploaded (a stale group lit cells twice in the first run: capital spread 383717 px over the gate), clocks per used group. **F2** capital spread (400 m owner, 12 hits/s within 35 degrees of the front, 4 s): 608 cells in one region, 5 draws, 0 dropped, largest drop 0.0234 vs one fade step 0.0227 + the FP16 allowance, 0 pixels over; witness with one draw per region: 64247 cells evicted over the run, 312386 pixel-frames over (worst 1.870 to 0), recorded, not gated. **F3** seam share saturate((s_own - s_seam) S / (sqrt 2 cell) + 1/2) over overlapping caps of an owner's other regions (`link_seams`): two fighter hits half a cell either side of the -z/-x edge peak 2.7207 = the brighter single (2.7207 / 2.4805), 4.4219 with the share off. **F4** timing rows add `raster_share` (occlusion query): the old 233.6 us case is `capital_close_full_map`, raster 1.00 at 5120x1440 now 271.1 us for 128 cells; `capital_fill_full_map` (camera 30 m from the hull, raster 1.00) 266.6 us, 1080p 88.7; spread spill 5 draws raster 2.26, 604.4 us (1080p 184.7). **F5** summary.json carries the SYMMETRY rows (fighter 19065 / 19248 / 19037 / 19276 px), the three NOPOP rows with their worst pixel, SEAM, STAGE_CPU and CENTRE, pinned by `test_weapon_effects_hook.py` (the old-law 268561 ... figures of the previous row are untracked). **F6** Flight B text: 1.76 to 2.64 s. **F7** a region whose cells have all faded is no drop (host case at 2.9 s: 0 patches, 0 dropped). **F8** `ws::build` in the i686 fixture, best / mean of 200: one capital, 64 hits: 22.9 / 26.6 us (8 draws, 1024 cells, 80 dropped); 16 capitals x 16 hits: 66.1 / 89.2 us (32 draws, 3360 cells, 1305 dropped) vs the 0.2 ms target. **F11** the lab takes CELL_KEY_BIAS, PATCH_CELLS, PATCH_HITS, OWNER_HITS, REGION_DRAWS from the headers (pinned), mirrors eviction and the seam share, hits per game second, SETA default 1. **Marker** (user decision 2026-11-06): no core flash; the hit's own cell at centre_gain 2, fill 0.6 (ring cells 0.3), same fade, a re-hit cell takes the newest hit's level; hit cell / next cell at 0.5 s 3.2142 vs the law's 3.2146; the hit point at 0.05 s 1.9014 vs cells-only 1.8652 (ring only, no core); marker through the resolve 0.986 / 0.972 / 0.966 at 0 / 4 / 8 px; lab caption per slot, centre gain control. | `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_weapon_effects.py --host native`; `PYTHONPATH=verification/probe python3 -m unittest discover -s verification/analysis -p 'test_weapon_effects_*.py'`; `python3 tools/effects/generate_weapon_effects_lab.py --check`; `tools/shaders/generate_weapon_effects_programs.py --check`; AP DLL `cmake --build <dir> -j4` + `check_no_x87.py`; figures: `python3 verification/results/native-windows/weapon-effects/ledger_rows.py` | Hook record PASS 68/0; D3D record PASS 36/0 [M] (RTX 5070 Ti, driver 32.0.16.2114): patch program 360 slots (was 317), decal 129; per tier 7 / 19 / 19 cells, bounds unchanged; fighter 12 hits/s 36 cells, one draw, 0 over; 63 host tests OK; lab check PASS; programs check PASS; AP DLL 0 warnings, no-x87 PASS (1161 functions). Corner limit: within a cell of a cube corner three regions' shares sum up to 3/2 [I]. Not flown. |

## Run235 Flight A (observe), flown 2026-11-06

Log `C:\X3\x3m.log` (DLL `bcb3dd38…`, commit `1c8f3371f`), triaged [M]: impact site and both taps `state=active` (impact write=plain, taps atomic); table `loaded rows=38 missile_rows=36 entries=38 textures=37`; 1,165 impact calls, every one forwarded, nested 0; `weapon_pos_check` max_error 0, unmatched 0 over compared=1165 taps=1165 (the +0x180 contact point is the tap's point); 64 `weapon_event` rows (the per-run row cap): 46 shield_hit absorbed=1, 18 hull_hit absorbed=0, tiers capital 42 / corvette 18 / fighter 4; the shield fraction falls about 0.030 per 12,030-damage hit on fighters and only rises after gaps over 1,400 frames (recharge); frame counters shield_read 296 / shield_unread 869 / no_projectile 2, overflow and stale_table 0; stage_us 0 (nothing staged in observe); no perf rows, frame_end windows 4.2-5.5 ms after loading; `session_end frames=103196 exception=0 resets=0`. No `weapon_death` rows (no death hook in step 1). No F8 captures were taken. Outcome: the hook contract holds in flight; Flight B (`weapon_effects = shields`) proceeds on the same DLL.

## Run235 Flight B (shields), flown 2026-11-06

Log `C:\X3\x3m.log` session `20261106-025218` (DLL of commit `1c8f3371f`), triaged [M]. User findings: cells too
small, the patch on the wrong side, never right on stations, the own ship's patch intermittent in chase view. Cause
(`verification/results/weapon-effects/run235-flight-b/shield_hit_geometry.py`): the stage drew on the owner's
bounding sphere and placed each hit at R normalize(pos); the logged contact points lie at |pos| / R 0.151 to 0.845
on three corvettes (R 56 / 88 / 110 m; 13 of 16 hits on one of them in the +-z regions) and 0.455 to 0.680 on a
station of R 1304 m (34 hits, 33 in +z, radial gap 417 to 710 m); only outward faces drew, so far-half hits (the own
ship's nose in chase view) never showed. Cost (`stage_cost.py`, 183 of 315 `weapon_effects_frame` rows drew):
stage_us (the period mean) median 13.9 / p90 34.4 / max 59.2 us, stage_us_max median 38.5 / p90 121.1 / max 1741.5
us (4 rows over 200 us), spills up to 1298 per 300-frame window (median 0, at most 4.33 per drawn frame), hits_dropped
82 in total (45 once), cells_dropped and dropped 0. The 64-row `weapon_event` cap ended at frame 22324 of 94355.

## Placement fix: the per-face lattice at the hit point (dirty on `77ec76056`, 2026-11-06)

Architecture 5.1.1 "Placement". Cells doubled to 3 / 6 / 16 m (rings unchanged, so the footprint radius doubles);
`soft` 1 cell (biased fade, whole where the hull lies behind); `weapon_event` rows: the first 64 of the run, then up
to 8 per 300-frame period (`sample=start|period|capture`), each with `pos_r` and `region`; a `weapon_patch` row per
staged draw on capture frames (owner, node, `is_view_object` = the own ship's root, region, cells, lit, ring hits,
seams, depth range and eye distance over R, cell and soft in metres). Commands: `X3M_FIXTURE_BOTTLE=X3 python3
verification/probe/wine_lock.py python3 verification/probe/run_weapon_effects.py --host native` (33 s);
`PYTHONPATH=verification/probe python3 -m unittest discover -s verification/analysis -p 'test_weapon_effects_*.py'`;
`python3 tools/effects/generate_weapon_effects_lab.py --check`; `tools/shaders/generate_weapon_effects_programs.py
--check`; AP DLL `cmake --build build-ap -j4` and `check_no_x87.py --edition ap`; figures:
`python3 verification/results/weapon-effects/run235-flight-b/placement_fix_rows.py`, `narrow_hull_regions.py` and `lab_narrow_hull.py`.

Results [M]: hook record PASS 68/0 (handler calls 0, x87 0, frame 168 B); D3D record PASS 40/0 (RTX 5070 Ti): patch
VS 17 / PS 188 slots (was 360), decal PS 129. Placement through the production builder (cells' centroid against the
hit point, its drawn screen centroid; the old projection's error for the record): narrow hull 1:1:6 flank hit at
0.15 R (corvette, cell 6 m) 3.08 m = 0.51 cell (screen 3.09 m), was 42.5 m; Flight B station contact point 0.676 R
(capital, cell 16 m) 6.74 m = 0.42 cell (screen 7.06 m), was 422.0 m; far-side hit 0.00 m, drawn (15277 px); eye at
0.5 R inside the sphere 0.00 m, drawn; the residual is the hit's offset from its cell centre (at most 0.58 cell).
Witness, the narrow hull's flank 40 % aft: region +z, its plane 85.8 degrees from the hull normal (architecture
"Placement", the paragraph's "Why the travel rule"). Unchanged gates pass: per tier 7 / 19 / 19 cells inside the cell-edge bound, both sides in both axes,
marker ratio 3.2142 vs 3.2146, soft fade over a hull lane (new) whole 2 units behind (162043.6 = the sky's), half at
half a soft radius in front (81021.8), 0 a soft radius in front, ring by whole cells, fade to 0 at 2941 ms, colour bands, re-light, adjacent hit 10
cells (peak 2.5391 = single), grid identical on a moved and rotated owner (23 cells), seam pair peak 3.1973 = the
brighter single (5.2578 unshared), decal, SETA 6 473.52 ms wall vs 473.34, retired owner, resolve core 0.994 /
0.986 / 0.986 at 0 / 4 / 8 px, program refusal, fault and Reset. No pop at 12 hits/s on a flat face (front at 0.6 R):
fighter 19 cells, capital 409 cells in 4 draws (433 spills, 0 dropped), 0 pixels over; witnesses: one draw per
region 1226153 pixel-frames over; the same fire on a sphere's front (each re-lit cell takes the newer hit's depth)
23415 (fighter) and 864202 (capital) pixel-frames over, worst frame drops 2.17 / 2.01. Stage CPU (`ws::build`, best /
mean of 200, i686 SSE2): one capital 64 hits 16.1 / 22.5 us (7 draws, 784 cells, 0 dropped; was 22.9 / 26.6 with 80
dropped), 16 capitals x 16 hits 59.0 / 90.2 us (32 draws, 1299 dropped), the Flight B station cluster 64 hits 11.1 /
12.4 us (1 draw, 82 cells, 0 spills), all under the 0.2 ms target; per region at most 8 draws, 32 per frame. Timing,
best of 60 fenced (us, 1080p / 5120x1440; fence 11.3 / 11.0): fighter two hits 15.3 / 18.7, capital close two hits
35.3 / 86.6 (two regions 37.1 / 95.4), worst four regions 44.4 / 108.2 (was 62.4 / 198.5), fighter 12 hits/s 17.5 /
23.0, capital 128 cells 24.5 / 36.4, 128 cells filling the frame 37.5 / 86.4 (raster 1.00), capital spread 5 draws
46.1 / 88.4 (was 184.7 / 604.4). Lab: the narrow hull (mid flank: region -x, 9925 lit px; aft flank: region +z,
224 px, edge-on) [M, node probe]. 63 host tests OK; lab and programs checks PASS; AP DLL 0 warnings (sha256
`91273006…`), no-x87 PASS (1161 reachable, 0 violations). Not flown.

## Placement follow-up: region by the bolt's travel, held depth (dirty on `77ec76056`, 2026-11-06)

Coordinator decisions on the fix above. (1) The region faces the bolt: `Event::dir` (3 × i8, world, the projectile
root node's row 2 × 127 rounded; the other tint candidate narrowed from u32 to `alt_rgb332` to free the bytes, Event
still 64 B), in this round taken into the owner frame at scene end with the owner's then rows; since review F1 (below)
the hook stores it owner-local (`core::travel_region`: the owner axis with the largest |component| of the travel,
signed against it); the radial rule when the record has no travel, counted per period as
`dir_fallback=` in `weapon_effects_frame`; `weapon_event` adds `dir=` and `travel_region=`. (2) A lit cell keeps the
depth it was first lit at until it has fully faded out (`stage::DepthCache`, per owner, handle, region and cell;
until = its newest clock's own fade end); a re-light only resets the clock. (3) `is_view_object` in `weapon_patch` is
the own ship (`resolve_own_ship`'s root node), not the cockpit's view object: in an external view of another ship it
reads 0 for the viewed ship (diagnostics limitation). (4) `seam_block`'s float -> unsigned through int (the x87
`fistpll` in `WeaponEffectsPass::run` is gone; also `link_seams` and the `weapon_patch` row).

Results [M] (same commands as the fix above, figures by `placement_fix_rows.py`):
hook record PASS 70/0 (new `travel_dir_from_bullet_row2`: identity rows give 0, 0, 127, the row (39322, -52429, 0)
gives 76, -102, 0 = the independent rounding; `travel_dir_fallback_zero_row_dead_node`: a zero row records 0, 0, 0
and counts one `dir_fallback`; a dead bullet node records nothing, `no_node` +1, forwarded: the callee's own test
returns before any effect, so in flight the fallback is the zero row only), handler 0 calls / 0 x87, 977
instructions (was 904), frame 196 B (was 168), stack 232 B of 1536, 50 events 17.0 us median against 30. D3D record
PASS 42/0: the narrow hull's aft flank hit by a bolt travelling along the owner's -x takes region +x, its plane 4.2
degrees from the hull normal, 110907 lit px against the mid-flank hit's 107535 (ratio 1.03), 0.79 m from the hit;
radially (the witness) region +z, 85.8 degrees, 19360 px. Lab (`lab_narrow_hull.py`, corvette, 4 hits/s at 1.2 s,
patch frame): mid flank 9916 px, aft flank 10709 px, both region +x (radially the aft flank lit 224 px). Held depth
(`no_pop_sphere_front_depth_held`, gated; the no-pop bound per pixel is one fade step per lit cell over it, counted
by the same draws at a constant level): fighter sphere front max drop 0.0391 under 2 overlapping cells, capital 0.0566
under 3, 0 pixels over (were 23415 / 864202 with the newest hit's depth); flat faces unchanged (0 over, overlap 1).
Host core cases: travel regions -x / +z / radial -z / turned +x; a re-light keeps 200 (without the cache 150), after
the fade a new hit lights at 150. Stage CPU without the cache (`ws::build` called without one; the production path with it is in the review section below): 15.8 / 17.7, 57.6 / 76.8 and 10.7 / 13.4 us (best / mean),
under 0.2 ms. Timing at 5120x1440 unchanged within noise (fighter 17.2, worst four regions 108.4, spread 87.6 us).
63 host tests OK; lab and programs checks PASS; AP DLL 0 warnings (sha256 `d441c038…`), no-x87 PASS (1161, 0); the
pass, stage, Present and handler functions in the DLL carry no x87 instruction (objdump scan, 10 functions). [I] the
bullet row 2 as its travel; not flown.

## Native shield-hit sprite skipped (dirty on `77ec76056`, 2026-11-06)

Coordinator decision, superseding the user decision of 2026-11-06 to keep the native impact under the patch:
`core::suppress_native_shield_hit` = `true`, so in `shields` mode a shield hit the stage owns (absorbed, not the
hull-through-shield class, the stage armed in the previous frame, the hit object not dying) records its Event and
skips `0x00413c40`: the stub returns `xor eax,eax; ret` to `0x00449d8a`, where the caller's `add esp,8` pops the two
arguments and `mov eax,esi` overwrites EAX. Observe, hull hits, a dying hit object, a frame the stage did not own and
every non-projectile call forward unchanged. Same commands as above. Results [M]: hook record PASS 71/0 (new
`shields_default_skips_native` and `shield_hit_skips_callee_hull_forwards`: the shield hit's callee not entered,
forwarded count +0, skipped +1, record `stage` and not `forwarded`, `frame_kept` (EBX/ESI/EDI/EBP, DF, MXCSR, x87
control word and stack, LastError, ESP); the hull hit in the same mode enters the callee, forwarded +1; the forward
path with `suppress_native` cleared and the observe forward checks unchanged), handler 0 calls / 0 x87, 977
instructions, frame 196 B, stack 232 B, 50 events 17.3 us median. D3D record PASS 42/0. 63 host tests OK; lab check
PASS; AP DLL 0 warnings (sha256 `24601f60…`, superseded by the review build below), no-x87 PASS (1161, 0). Not flown: the flight must confirm no native
shield sprite in `shields` and the vanilla sprite in `observe`.

## Review of the placement diff (F1-F6, F9, F10; dirty on `77ec76056`, 2026-11-06)

**F1** `Event::dir` is owner-local: the hook rotates the bullet's row 2 into the hit object's frame with the rows it
already read for `pos` (`rotate_in`), so the region no longer depends on the owner's rows at scene end (a turning owner
kept moving a live patch across a region boundary and losing its held depth, the region being in the cache key);
`core::travel_region` takes the owner-local bytes alone. **F2** the seam share follows the cells, not the radial
direction: per draw up to four seam neighbours (other regions' draws whose cell bounds meet its own), the light
shared by the signed distance to the radical plane of the two bounds over a cell-wide band (182 ps_3_0 slots, was
188). **F3** the stage CPU rows are `ws::build` with the depth cache (the production path). **F4** the cache is
looked up only for drawn lit cells; a full probe window counts `depth_full=` in `weapon_effects_frame`; the cache grew
to 8192 slots (192 KB; at most half full under 32 x 128 drawn cells) and stores each cell's own fade, so a hit skips
the hash. **F5** the sphere-front no-pop without the cache stays in the record as a witness. **F6** stale text fixed
(5.1 item 3, the cell-map bullet's state and slots, the travel conversion, the architecture reference). **F9** the soft
radius changed with the placement fix: 0.5 R of the bounding sphere for the patch and 0.5 x the footprint radius (0.75
to 1.25 cells) for the decal, now 1 cell for both. The suppression flip of the section above (`suppress_native_shield_hit`
= `true`) is part of this diff. **F10** the cache is cleared on a Reset and when the ring's scope changed (a load, a
sector change, a new load epoch: `weapon_effects::scope_changes()`). **F3** label: STAGE_CPU `depth_cache=1`.

Results [M] (same commands; `placement_fix_rows.py`): hook record PASS 71/0: the direction check with the victim
turned (local x -> world -z, z -> world +x): the bullet's identity row gives (-127, 0, 0), the row (39322, -52429, 0)
gives (0, -102, 76), the zero-row fallback and the dead node as before; handler 0 calls / 0 x87, 1036 instructions (the rotation into the owner frame; 977 before),
frame 196 B, 50 events 17.3 us median. D3D record PASS 43/0 (two runs hit `CHECK device FAIL`, the host's display
unavailable for CreateDevice for about 7 minutes; the run after the wait passed, unchanged inputs): new
`seam_travel_regions_keep_their_peaks`: on the 1:1:6 hull an aft-flank hit by a bolt along -x (region +x) next to a
hit 0.62 R aft by a bolt along -z (region +z), two draws naming each other (0x05); each keeps its single-hit peak over
the pixels only it lights (3.2559 = 3.2559, 2.0957 = 2.0957), the pair's peak 3.2559 = the brighter single, and its
area (recheck): of the 119788 pixels either single lights the pair leaves 170 dark (0.14 %, gate at most 1 %; 119618
lit, the share band at the radical plane); the
radial seam pair 3.1973 = the brighter single, 5.2578 with the share cleared (the witness, now clearing the
neighbours). Held depth gated (sphere fronts 0 pixels over at overlap 2 / 3); witnesses without the cache: fighter
18150, capital 119246 pixel-frames over (worst drops 2.20 / 2.04). Stage CPU with the cache (best / mean): one capital
64 hits 19.0 / 24.5 us, 16 capitals x 16 hits 70.1 / 100.2 us (depth_full 0), Flight B station cluster 11.6 / 15.0
us; under 0.2 ms. Placement and timing unchanged (aft flank 110907 px vs mid 107535; 5120x1440 fighter 17.5, worst
four regions 103.1, spread 83.5 us). Figures from the recheck run (D3D record PASS 43/0; the hook record re-ran with
unchanged sources, PASS 71/0). Lab (`lab_narrow_hull.py`): mid flank 9916 px, aft flank 10709 px, both +x. 63
host tests OK; lab and programs checks PASS; AP DLL 0 warnings (sha256 `525ee864…`, the build of this diff), no-x87
PASS (1161, 0), objdump: no x87 instruction in the 11 weapon pass, stage, Present and handler functions.
Diagnostics limitation: `weapon_patch is_view_object` is the own ship (`resolve_own_ship`), not the cockpit's view
object. Left as notes (F7, F8, F11, F13). [I] the bullet's row 2 as its travel; not flown.

## Run236 Flight C: the +-y regions and the own ship in the cockpit (dirty on `1f9c06598`, 2026-11-06)

Flight C (Run236, DLL `9f35b335…`), `C:\X3\x3m.log`, triaged [M] with
`verification/results/weapon-effects-run236/flight_c_rows.py`: 321 `weapon_effects_frame` rows, `stage_us_max` max
1542.5 us (frame 6900; 1245.0 at 6600, the first drawn windows; 760.1 at 23100; median 20.8, p90 121.4 over the 177
non-zero rows), totals `dropped` 0, `cells_dropped` 0, `hits_dropped` 0, `spills` 0, `depth_full` 0, `dir_fallback`
0; no `weapon_effects_stage` or `weapon_effects_failed` row; `session_end frames=96100 elapsed_ms=515469 devices=1
resets=0 exception=0`. User: flank and nose/tail hits right, a hit from above or below (the target above or below
the player) draws the patch standing on edge.

Finding. The +-y path places and draws like the flanks (below), so the defect is not in `anchor_frame`, the pass,
the programs, the per-cell depth or the seams: it is the region rule. `core::travel_region` takes the axis of the
bolt's largest component, not the face the bolt hit; on a flat hull an oblique bolt from above (under 45 degrees off
the hull's plane) has its largest component along z or x, so its hit takes a z or x region whose plane stands across
the top. [I] in the log (box per owner = the hits' own largest |pos| per axis, owners with at least 8 hits): 225 of
501 hits lie on the y face, 79 of them took a +-y region (the logged `travel_region`); the travel normalised by that box (argmax |dir_k| / e_k)
would give 176 of 225 (364 of 501 faces overall against 304 now). Not fixed here: it needs the owner's per-axis hull
half-extents, which the stage does not read (the chain of render-node-bounds.md 6.1, ship_avoidance_walk_core.h
`hull_children` for capitals); superseded: an extent-scaled rule (travel over the walked hull box) was built and
discarded by user decision for the screen-space patch below, which chooses no face from the bolt.

The planar +-y coverage table and the first cockpit-dim figures measured on that build are superseded (review F3):
the planar patch was replaced by the screen-space patch below, whose final figures come from the record through
`verification/results/weapon-effects-run236/ledger_figures.py`.

### The screen-space patch (user decision 2026-11-06, same dirty tree; 5.1.1 "Placement")

The patch is a decal on the hull from the depth lane; the travel region, the per-cell depth, the depth cache, the
soft fade and the seam planes are gone; the cockpit dim stays. Per owner six faces, every hit on both faces of every
axis (review F1); per cell its clock (the newest hit) and its anchor (the covering hit nearest its centre); a draw is
one face's cells as one box (the cells' hexagons, the anchors -+ the extent along the axis; back faces only); per pixel
the lane gives p (owner frame), p must lie in the box, the normal (ddx / ddy of p) gives the face's triplanar share
(sharpness 8, floor 0.3, signed by the outward normal) and only where it is positive the pixel's cell is looked up in
the draw's constant map (128 keys heap-sorted, clocks, eight group ranges). A face whose box points away from the eye
past the cut (`face_cull_sin` 0.841) is culled on the CPU, first on the hits' grown box before its cells are built
(`faces_culled=`); past 8 screens of boxes per frame the oldest owners' draws go (`area_dropped=`); 64 draws per frame;
the cockpit dim only on a fresh chase-camera visit (review F2). Two earlier builds are superseded: one column per cell
(2.5 to 9 x the planar patch's GPU time) and the per-face lighting by the side of the owner's origin (review F1).
Figures [M] from the final record (`verification/results/weapon-effects-run236/ledger_figures.py`; D3D record PASS
46/0, hook record PASS 71/0, handler 0 calls / 0 x87 / 1036 instructions, 50 events 17.2 us median; shell vs 8 / ps
457 slots, decal ps 129; `sizeof(WeaponPatch)` 1944 B, the frame's 64: 124416 B):

| placement case (hull lane) | draws | lit px | on sky | past the footprint | coverage near the hit | centroid | vs reference |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1:1:6 mid flank | 5 | 71979 | 0 | 0 | 1.0 | 0.28 cells | 1 (ref) |
| 1:1:6 aft flank / top / bottom | 5 | 67016 / 69075 / 69068 | 0 | 0 | 1.0 | 0.26 / 0.33 / 0.36 | 0.93 / 0.96 / 0.96 |
| 6:1:4 flank | 5 | 9324 | 0 | 0 | 1.0 | 0.45 | 1 (ref) |
| 6:1:4 top / bottom / top from 42 deg down | 5 | 21342 / 21367 / 21342 | 0 | 0 | 1.0 | 0.45 / 0.42 / 0.45 | 2.29 (the 2 m flank holds less of the footprint) |
| 6:1:4 top, hull centred 2 cells below the origin (F1) | 5 | 18624 | 0 | 0 | 1.0 | 0.43 | 0.87 of the centred top |
| Flight B station 0.68 R | 5 | 2452 | 0 | 0 | 1.0 | 0.48 | 1 |
| far side | 5 | 0 (hidden by its hull) | 0 | 0 | - | - | - |
| eye inside the bounding sphere, 4 m from the nose | 6 | 1258427 | 0 | 0 | 1.0 | 0.12 | 1 |

Draws per hit (F1): one front hit on a fighter 5 before and after (before: -z and both faces of the two axes whose
plane held it; now all six, +z culled); the aft flank, the narrow top and bottom 4 -> 5, the station 3 -> 5, the eye
inside 5 -> 6. Surface: on the plane 162043.8 luma, sky 0, half an extent nearer drawn, past the extent 0, the
left-half lane nothing right of the hit, the left half equal. No-pop 0 popped pixels on the fighter and capital flat
faces and sphere fronts. Face edge 45 degrees: pair peak 2.035, singles 2.604 / 3.531. Cull: a back face (corner dots
-0.998) culled; turned 32 degrees across the cut (-0.916 .. -0.840 against -0.841) drawn. Cockpit (`COCKPIT`): own ship
in the chase view 42293.21 luma (dimmed 0), in the cockpit 14803.46 (ratio 0.350, dimmed 5), another ship and no own
ship 42293.21, the own ship with a stale visit (no visit for two frames) 42293.21 (dimmed 0); 35289 lit pixels each.
Sort (`SORT`, 128 keys, best of 2000, us): heap 0.7 / 0.9 / 0.6 against insertion 1.7 / 0.1 / 3.4 (shuffled /
ascending / descending). Normals (`NORMALS`): ddx / ddy mean 0.14 to 0.44 deg, p99 1.27 to 4.68; wrong dominant axis on
silhouettes 1134 / 719 / 1869 of 2191 / 1188 / 3106 against 0 / 63 / 122 for the 4-neighbourhood; ddx kept. Stage CPU
(best / mean): one capital 64 hits 61.6 / 65.8 us (19 draws), 16 capitals x 16 hits 179.5 / 202.9 us (64 draws, 24
dropped), the Flight B station cluster 39.6 / 41.2 us. GPU (EVENT-fenced, best of 60, us):

| case | 3ee53bf04 (planar) 1080p / 5120 | now 1080p / 5120 | raster share now at 5120 |
| --- | --- | --- | --- |
| fighter two hits | 15.8 / 17.5 | 16.0 / 21.0 | 0.035 |
| capital close two hits | 33.9 / 81.9 | 60.8 / 176.4 | 1.0 |
| worst four regions | 42.8 / 103.1 | 98.6 / 365.1 | 3.11 |
| capital spread 12 hits/s (14 draws) | 44.5 / 83.5 | 198.6 / 680.1 | 7.92 |

Moving the cell and border work behind the share test (F4) changed nothing measurable (capital close 177.9 -> 176.4
us). The screen-area budget bounds a frame near 8 full-screen boxes: 8 x 177.5 us (the full-screen box at 5120x1440)
= 1.42 ms. Accepted for the flight with this cost (coordinator, kill criterion recorded with the run). 64
`test_weapon_effects_*` host tests OK; lab `--check` PASS; programs `--check` PASS; AP DLL 0 warnings, sha256
`8ceaa0d4…`, no-x87 PASS (1161 reachable, 0 violations).

Open: F6 the R32F lane form (`four_channel` false, device z/w inverted with m32 / (d - m22)) is untested (the fixture
lanes are four-channel); F7, F8 notes as filed; F12 (review note, as filed); the 16-capital stage CPU mean 202.9 us
crosses the 0.2 ms target (best 179.5); not verified in flight (the hull's real depth, faceted normals, part
overlaps).

## Stand-off and drained shield (dirty on `efce60957`, 2026-11-06; 5.1.1 "Stand-off", "Drained shield")

Two changes after Run237 Flight D. **Stand-off**: the patch floats `core::standoff_m` (1.5 / 3 / 8 m) above the hull
with one parallax step per pixel (one extra lane fetch); the box rasterizes grown by `renderer::raster_box`, which the
face cull and the screen budget also test. **Drained shield**: an absorbed hit whose read post-hit fraction is below
`core::drained_shield_q` (327 / 65535) forwards as a hull hit (`detail_drained`, `drained=` in `weapon_event` and
`weapon_effects_frame`). Evidence for the rule [M], `verification/results/weapon-effects/drained_rows.py` on Flight D's
`C:\X3\x3m.log`: of 776 `shield_hit` rows with a read shield, 10 have a post-hit fraction of 0.0000 to 0.0005 (TBullets
row 29, damage 428) and 1 has 0.0034 (row 11, damage 12030), on two owners (9 / 2 rows). The 11 rows, the summary and
the per-owner counts are kept in `verification/results/weapon-effects/drained_rows_run237_flight_d.txt` (the script
reads it back). Figures [M] come from `verification/results/weapon-effects/standoff_figures.py` on the final record
(review fixes F1-F14 applied). Both records pass: D3D PASS 48/0 and hook PASS 75/0. The handler makes 0 calls, uses 0
x87 and has 1065 instructions (1036 before). 50 events take a 17.3 us median against the 30 us target. Shell ps is 548
slots (457 before); `sizeof(WeaponPatch)` is 1948 B and the frame's 64 patches take 124672 B.

Drained (hook fixture `DRAINED`):

- q 199: `hull_hit`, `detail_drained`, absorbed, forwarded, callee entered, `drained` +1. The frame is kept.
- q 399: shield hit, staged, skipped.
- Unread shield: shield hit, staged, skipped.
- Boundary: q 326 is drained (forwarded); q 327 is a shield hit (staged, skipped).

Tilted plate (`STANDOFF`): a fighter's flat top, its normal 61.7 degrees off the eye, one hit. The parallax shift is
the lit centroid against the stand-off-0 centroid, against the floated image of the hit:

| stand-off | lit px | on sky | shift / predicted (view units) | shift error | implied stand-off |
| --- | --- | --- | --- | --- | --- |
| 0 | 17656 | 0 | 0 / 0 | 0 | 0 |
| 1.5 m (default) | 17365 | 0 | 14.63 / 14.71 | 0.006 cells | 1.491 m (0.003 cells off) |
| 6 m (2 cells) | 16265 | 0 | 70.81 / 71.09 | 0.020 cells | 5.981 m (0.006 cells off) |

The implied stand-off is the d' whose floated image of the hit lies the measured shift from it (the floated image
inverted by bisection). Both gates are 0.1 cell; without parallax the default stand-off would miss them by about a
cell (a shift of 0 against 14.7 view units).

Placement (the metrics about the hit's floated image; image shift = image against hit):

| case | lit px | on sky | past the footprint | coverage | centroid | image shift | vs reference |
| --- | --- | --- | --- | --- | --- | --- | --- |
| narrow mid flank / aft / top / bottom | 75523 / 69420 / 75309 / 75374 | 0 | 0 | 1.0 | 0.30 / 0.25 / 0.34 / 0.39 | 0.03 / 0.13 / 0.13 / 0.13 | 1 / 0.92 / 1.00 / 1.00 |
| flat flank / top / bottom / top 42 deg | 9663 / 24538 / 24571 / 24538 | 0 | 0 | 1.0 | 0.44 / 0.42 / 0.40 / 0.42 | 0.27 / 0.08 / 0.08 / 0.08 | 1 / 2.54 / 2.54 / 2.54 |
| top, hull 2 cells below the origin | 21332 | 0 | 0 | 1.0 | 0.42 | 0.08 | 0.87 |
| Flight B station | 2520 | 0 | 0 | 1.0 | 0.48 | 0.26 | 1 |
| far side | 0 | 0 | 0 | - | - | 0 (hidden: the hit itself) | - |
| eye inside, 4 m from the nose | 480222 | 0 | 0 | 0.497 | 0.03 | 0 | 1 |

The eye-inside coverage is the stand-off's screen-space limit. The hull points under the outer pixels lie off the
screen or beyond the outline, so its gate is 0.45. The fixture's CPU twin of this lane (`STANDOFF_REFUSALS`) draws
0.4967 of the 573535 pixels within half a reach of the floated image. It refuses the rest: 0.2192 off the screen,
0.1883 on the sky, 0.0493 behind the eye and 0.0466 at the plane test. The program now refuses an off-screen H1 and an
H1 at or behind the eye plane itself; before the review they fell to the texture clamp and the sky / plane tests.

Parallax steps (`STANDOFF_STEPS`, the CPU twin on spheres against the exact offset sphere, 1920x1080). The figure is
the screen error of the drawn hull point in px at p99, all pixels / silhouette (normal over 60 degrees off the eye):

| sphere | 0 steps | 1 step (built) | 2 steps | rejected at 1 step | no normal (refused) |
| --- | --- | --- | --- | --- | --- |
| fighter 12 m at 68 m | 14.6 / 21.8 | 3.95 / 4.08 | 4.46 / 5.80 | 126 of 170605 | 822 |
| fighter 12 m at 37 m | 32.7 / 59.5 | 7.13 / 7.16 | 8.45 / 10.67 | 2134 of 477057 | 1401 |
| corvette 50 m at 70 m | 11.4 / 32.1 | 3.61 / 3.63 | 3.85 / 4.96 | 3066 of 1354434 | 1379 |
| capital 400 m at 400 m | 0.96 / 2.36 | 0.56 / 0.72 | 0.57 / 0.82 | 319 of 1789450 | 1296 |

One step suffices: a second step lowers the world error (cells p99 0.26 -> 0.17 on the far fighter) but raises the
screen error, because the fixed normal bounds it. The 1-step maxima (12.6 / 7.2 / 4.2 / 5.0 px) are those of pixels
with a normal; pixels whose quad touches the sky are refused.

GPU timing at 5120x1440 (EVENT-fenced, best of 60, us), against efce60957's ledger rows:

| case | efce60957 | now | ratio | raster share (efce60957 -> now) |
| --- | --- | --- | --- | --- |
| fighter two hits | 21.0 | 26.3 | 1.25 | 0.035 -> 0.054 |
| capital close two hits | 176.4 | 223.9 | 1.27 | 1.0 -> 1.0 |
| worst one owner (four regions) | 365.1 | 542.5 | 1.49 | 3.11 -> 3.62 |
| capital spread 12 hits/s | 680.1 | 1014.3 | 1.49 | 7.92 -> 8.33 |

Per rasterized pixel the step costs about 1.27x (the full-screen capital case, its raster share unchanged). The two
stacked-box cases also pay the larger raster box (+16 % / +5 %): they miss the 1.3x target. The pre-test margins stay
4d / 2d; narrower margins clip the parallax's far band on oblique views. Reusing the pixel's own depth when the shift
rounds to its own texel did not make the run faster, so it was dropped (no record kept).

Other checks:

- The stand-off is static per tier, so the time-based cases were not affected and still pass: no-pop 0 popped pixels
  in the four gated runs, the cockpit dims 5 / 0 / 0 / 0, fade, SETA 6, Reset and faults.
- Stage CPU best / mean: 64.6 / 86.9 us, 185.9 / 256.9 us and 39.8 / 48.3 us. The means are above the earlier
  65.8 / 202.9 / 41.2 us; the best figures are unchanged.
- 65 `test_weapon_effects_*` host tests OK. The core test's y-face case now draws 6 faces (+z within the cut once its
  box grows).
- Lab `--check` PASS, with a stand-off control per tier. Programs `--check` PASS.
- AP DLL: 0 warnings, sha256 `6f7aa8f0…`, no-x87 PASS (1161 reachable, 0 violations).

Open:

- The 1.3x target on the stacked-box cases (above).
- The eye-near clipping (above).
- The thin-part angle limit: across a part thinner than about 2 d, the hull under a floated point is the part's
  other side or nothing, so the plane test clips the floated patch at an angle-dependent band.
- The occluder-outline hole: where another surface covers H1 (a part or a ship in front), the plane test refuses the
  pixel, so a ring about the occluder's outline stays dark on the floated patch.
- Review notes 5-9 and 12 as filed.
- The R32F lane form is still untested (F6).
- Not verified in flight.

## Ship bubble, station decal (dirty on `71545aa39`, 2026-11-06; 5.1.1 "Placement")

User decision after Run238 Flight E: the stand-off of `21ad87836` is reverted (its constants, the parallax step and
its refusals, the grown raster box); ships get a smooth shield bubble, stations keep the hull decal of `6720724d3`
unchanged; the drained-shield rule and the cockpit dim stay. Selection by the newest record's `TierSource`
(`tier_by_station`: the decal). The bubble: the hull bounds from the walk of render-node-bounds.md 6.1
(`stage::hull_bounds`, `src/proxy/weapon_effects_bubble.h`) x `core::bubble_scale` 1.15, floored per axis at 1/16 of
the largest and cut to `bubble_scale` x the object radius, else a sphere of the object radius (`bubble_fallback=`);
the walk cached per
owner (`stage::BubbleCache`, 64 entries keyed by owner, root node, handle and the root's first child with its body id;
a radius, child or ring scope change invalidates, a failed walk retried after 2000 game ms, LRU) and bounded to two
walks per stage call (`bubble_deferred=`); the hit mapped radially in the bubble's normalised space (B), lighting both
faces of every axis; per pixel (`weapon_bubble_ps.hlsl`) the ray's near and far bubble points, each drawn in front of
the lane and inside the draw's box, the far side at `core::bubble_far_gain` 0.6.

Review fixes (same dirty tree): **F1** both faces of every axis for every hit (the sign rule is gone); `bubble_box`
drops the opposite sheet (its cells lie outside their anchor's depth window), and a cost filter feeds a face only the
hits whose window meets its sheet. **F2** the axis-plane pair (fixture `AXIS_PLANE`, host `axis_plane`). **F3** the
chase case's camera from a CPU sweep (`chase_sweep.py` in the session scratchpad, not kept: the near side lit from a
pitch of 0.2 rad, the far side's count rising with it), gated on no near pixel. **F4** two walks per stage call, the
walk timed through the production reader. **F5** the radius cut (`bubble_clamped=`), at `bubble_scale` x the radius
(coordinator, last change: the bare radius pulled the bubble onto the nose and tail of a hull that reaches its radius,
and the chase camera's nose hit needs the cap past the outline; the guard is against bogus parts). **F6** the eye-inside and
cockpit timing (not culled: below). **F7** the child key and the walk counters in `weapon_effects_frame`
(`bubble_walks=`, `bubble_walk_failures=`, `bubble_held=`, `bubble_deferred=`, `bubble_clamped=`). **F9** the 5.1.1
frame-buffer line (126 KB). **F10** `model_lod_count` removed. **F11** `manage.py --weapon-effects` help text (the
Fresnel shell). `node_sentinel` stays: the walk's child key and the fixture's hull use it. **F8** and **F12**: notes as
filed.

Figures [M] from `verification/results/weapon-effects/bubble_figures.py` on the final records: D3D PASS 50/0, hook
PASS 75/0 (handler 0 calls, 0 x87, 1065 instructions, unchanged; 50 events 17.3 us median). Slots: hull decal ps 457,
bubble ps 873, decal ps 129; `sizeof(WeaponPatch)` 1972 B, the frame's 64 126208 B.

Placement (the near side only; centroid and footprint about B; the station on its hull lane; every face fed, the
opposite sheet's draws dropped; cap = the bubble past the hull's nose / tail along z):

| case | surface | draws | lit px | over sky | past the footprint | coverage | centroid | B from the contact | cap | vs reference |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| narrow mid flank / aft / top / bottom | bubble | 4 | 83463 / 76739 / 77195 / 77195 | 10060 / 11344 / 13414 / 13414 | 0 | 1.0 | 0.28 / 0.42 / 0.31 / 0.29 cells | 0.24 / 0.54 / 0.54 / 0.54 | 7.5 m | 1 / 0.92 / 0.93 / 0.93 |
| flat flank / top / bottom / top 42 deg | bubble | 5 | 11784 / 21373 / 21305 / 21373 | 172 / 1066 / 1066 / 1066 | 0 | 1.0 | 0.49 / 0.39 / 0.37 / 0.39 | 0.59 / 0.24 / 0.24 / 0.24 | 1.2 m | 1 / 1.81 / 1.81 / 1.81 |
| flat top, hull 2 cells below the origin | bubble | 5 | 18480 | 926 | 0 | 1.0 | 0.40 | 0.23 | 1.2 m | 0.87 of the top |
| Flight B station 0.68 R | hull decal | 5 | 2452 | 0 | 0 | 1.0 | 0.48 | - | - | 1 |

No lit pixel lies where the bubble's CPU twin hides the near point. "Over sky" is the bubble past the hull's outline
(by design). The 42-degree bolt lights the face-on count (dir unused). With the cut at 1.15 x the radius no case is
cut (the hulls reach their radius: the cap is 0.15 of the half-length past nose and tail, gated at 0.1 on the narrow
hull, `bubble_cap_past_the_narrow_hull_nose_and_tail`); with the bare radius the cap was 0 and B lay 0.04 to 0.43 cells
from the contact (the previous record).

Axis plane (F2): the flat fighter's top hit at x = +1, -1 and 0 engine units gives 4 draws each and identical images
(21253 lit px, peak 3.1816, luma 23845.03 all three); the host twin lights the same cells with the same clocks, none
twice.

The two sides (images with one side's gain 0):

- Chase camera behind and above a fighter (4 x 1.6 x 12 m hull, object radius 13.2 m, 24 m ahead, pitched 0.175 rad
  nose down), a hit on its nose: the bubble's cap 1.8 m past the nose (gated at 1.2 m); near side 0 px; far side 1212
  px around the outline (all over sky), 0 the twin hides, nothing past the footprint; the patch's core within half a
  reach of B is hidden by the hull (5915 px, share 1.0), the visible far-side centroid 0.89 cells from B. The default
  image is 0.600 of the far side's. (With the bare-radius cut the cap was 1.2 m and the far side 2969 px: the shorter
  bubble's cap lay further out of the hull's shadow at this pitch; the CPU sweep with the 1.15 x cut keeps the near side
  unlit to 0.2 rad.)
- Eye inside the bubble (the fallback sphere, R 20 m, the eye 10 m behind its centre, sky lane, the hit on the front
  pole): near side 0 px, far side 147479 px (coverage 1.0, centroid 0.09 cells); the default image 0.6001 of it.

Hull walk (127 parts, 894 reads per walk; best / mean, us): the fixture's reader 8.8 / 9.1; the production
`engine_memory::read` (`src/proxy/engine_memory.cpp` compiled without SSE as the DLL builds it, over the same fixture
blocks) 12.0 / 15.4 with the region cache invalidated before each walk (a new frame) and 11.6 / 14.5 warm. The
fixture's blocks are one committed region; in the game a walk's nodes, models and LOD records span several heap
regions, each validated by one VirtualQuery per frame on its first touch, so the production cost is these figures
plus one VirtualQuery per distinct region [I], at most two walks per frame. One capital's 64-hit frame: 39.6 / 52.1 us
warm (199 of 200 frames from the cache), 48.5 / 61.8 us walking every frame. Stage CPU: `one_capital_64_hits` 28.9 /
38.9 (11 draws), `sixteen_capitals_16_hits` 88.8 / 110.7 (64 draws), the Flight B station cluster 13.5 / 14.6; before the F1 cost filter the sixteen capitals took 351 / 471 (all six faces fed every hit; recorded in the
pre-filter run, not kept).

Precision of the bubble program's float32 (`verification/results/weapon-effects/bubble_prec.py`, the reviewer's
script: a 1 km capital 10 km away at 5120x1440, 20000 samples across its image, 409 on it): 0 hit/miss mismatches
against float64, the normalised point within 4.7e-5 (2.4 cm), the view depth within 2.2e-6 relative (2.3 cm) [M].

GPU at 5120x1440 (EVENT-fenced, best of 60, us) against `6720724d3`'s rows:

| case | 6720724d3 (hull decal) | bubble | ratio | raster share |
| --- | --- | --- | --- | --- |
| fighter two hits | 21.0 | 21.5 | 1.02 | 0.031 |
| capital close two hits | 176.4 | 178.0 | 1.01 | 0.92 |
| worst one owner (four draws) | 365.1 | 340.9 | 0.93 | 2.01 |
| capital spread 12 hits/s (builder) | 680.1 | 290.9 | 0.43 | 1.89 |
| eye inside, front hit (builder, sky lane) | - | 54.2 | - | 0.22 |
| own ship in the cockpit, eight hits (builder, sky lane) | - | 257.9 | - | 2.24 |

At 1080p the bubble rows are 17.4 / 60.2 / 96.0, the spread 95.4, the eye inside 21.8 and the cockpit 74.3 us. The hull
decal's own rows are unchanged (fighter 21.4, capital close 178.4, worst four regions 366.8 at 5120x1440).

F6, the cockpit: its draws whose box contains the eye rasterize the screen, 258 us against the close capital's 178 us.
Not culled: the cull (drop the own ship's eye-containing boxes in the cockpit view) would hide the patch about the eye
that the user decided to keep at `own_cockpit_gain` 0.35 (Run236 Flight C), and the 8-screen budget already bounds a
frame; it is an option for the user, not applied.

Other checks: no-pop 0 popped pixels on the bubble (fighter and capital spread on the flat lane, both sphere fronts:
draws 1 / 9) and on the station's hull decal (`station_flat_12hps`, 10 draws); the one-draw witness still evicts. Seam
pair peak 2.033 against singles 2.604 / 3.535. Cockpit dim: the own ship's one draw dimmed in the cockpit (ratio 0.350),
nothing else; colour, SETA 6, Reset and fault (a decal and a bubble draw: both draw after the fault and after Reset),
the retired owner and the program refusal pass. 67 `test_weapon_effects_*` host tests OK (the walk, its cache with the
child key, the budget, the radius cut, the mapping, the axis-plane pair and the fallback through the host harness; the
lab's bubble presets under node). Lab `--check` PASS. Programs `--check` PASS (the bubble program without warnings; the
hull decal's program keeps its three X4121 gradient warnings of 6720724d3). AP DLL clean at -j4, 0 warnings, sha256
`2dcc549e80f9a775…`, no-x87 PASS (1161 reachable, 0 violations); no x87 instruction in the stage, the walk or the pass
(objdump of their objects). The native host could not create a D3D device for four attempts (the session locked,
`LockApp.exe` up; one earlier run lost its device mid-run); the record is the run after the last change (first
attempt).

Open:

- The chase view's patch core lies behind the hull from every camera that keeps the near side unlit; what shows is the
  bubble's nose cap past the outline.
- Hull parts reaching past the bubble (a box corner lies at sqrt 3 of the half-extents) hide it there. Not measured on
  a real hull.
- The walk's bounds are the union over every collision part and LOD record; the census of 6.3 (animated or rotated
  parts, unresolved bodies) applies.
- The cockpit cull (F6) is the user's call.
- The lab feeds every hit to every face (no cost filter): the same draws, more work.
- `standoff_figures.py` reads the stand-off record and no longer runs against this record (kept with its section).
- Review notes F8 and F12 as filed.
- Not verified in flight.

## Run239 Flight F (ship bubble), flown 2026-11-06

The user accepted the look ("good now"): ships on the ellipsoid bubble, stations on the hull decal, no stand-off, hex only on shield hits, drained shields as hull hits, own-ship cockpit dim. Shield-hit step 1 closes on Run239 (`77a8ba3e5`, DLL `5735959d…`). Open items carried to step 2: the cockpit cull decision (drawn at 0.35), the untested R32F lane form, the bubble on winged or thin hulls (no complaint in Flight F), the 8-screen area budget as the GPU bound. [M] none: the flight was judged by eye, no log triage requested.

## Hull hit, step 2 (dirty on `9ae410d6d`, 2026-11-06; [weapon-look.md](../architecture/weapon-look.md) 3.1)

Built as the look note specifies, not flown. **Mode** `weapon_effects = shields,hull` (ini key, schema,
`manage.py --weapon-effects`; `observe` and `shields` unchanged). `core::parse_mode` also takes `hull,shields` and
spaces around the tokens; "hull" alone, a repeated, empty or unknown token is refused. The schema lists `shields,hull`
and `hull,shields`; the launcher normalises to `shields,hull` and its dry run prints `weapon effects: <mode>` and a
`weapon_effects` field. `install_at` arms `family_shields | family_hull`; a hull hit (absorbed 0, the
hull-through-shield rows, a drained shield, a missile source) while the stage owned the previous frame *and* the pass
holds the hull programs (Present's `HookState::hull_ready`) is staged and skips the native spawn
(`core::suppress_native_hull_hit`, the outer site); a dying hit object, an unowned frame and a pass without the hull
programs forward. Variants on `Event::detail`: asteroid by the hit object's class 17, repair / tug
by the override's new `hull` key (`weapon_overrides.json` by_id: REPAIR and RL repair, TUG tug with calibre 0.1; the
installed `x3m/weapon_effects.json` must be regenerated to carry them; a table without any `hull` key, or none, logs
`table_hull_key_missing=1` with the `weapon_effects_table` row and in every `weapon_effects_frame` row). Record life
`core::hull_lifetime_ms` 1441; Present keeps a staged hull hit `HookState::life_floor_ms` longer (the largest floor a
live record latched, or this step's, plus a step). Ring overflow evicts the oldest census-only record first, then the
oldest stage-owned record of any role (a staged hull hit's native spawn was skipped, so it is no cheaper than a shield
hit's).

**Stage** `src/proxy/weapon_effects_hull.h` (`stage::build_hull`, after the patches): per ring slot the record's first
stage frame (game ms, the world contact point, the owner's velocity, and the three-frame floor of that frame's step,
kept for the record's life: a SETA change never lengthens a faded element back into view), keyed by sequence. Every
owner of a stage-owned record (a shield hit's too) is tracked each frame, so a first hull hit on an owner drawn in the
previous stage frame takes the node's `+0x30` delta since then (0 otherwise). Past 32 owners a frame those whose
newest hull hit is the oldest are not drawn (ties by address: the same choice frame to frame), counted
`owners_dropped=`; flash (disc core + heat halo, centre biased 0.5 r_hit to the eye),
sparks (antipodal Fibonacci pairs turned per record, closed form in game time, tail through the previous frame's view
`camera_previous_` when it is the previous frame's), glow and ember on the lane; three-frame floor from `cfg+0x714`; a
record whose owner went after its first frame keeps its sparks (orphan), one never seen leaves; own ship in the cockpit
at 0.35; the shared budget: the glows of other owners shed oldest first past the screens the patches left
(`stage::Counts::screens`), sparks and flashes never. **Pass**: two batched `DrawIndexedPrimitiveUP` draws (glows first,
sprites last), one static quad index list, no DEFAULT-pool resource; the hull programs and the batch limits are
requested at attach only in shields,hull mode, and their refusal leaves the pass enabled for the patches
(`caps().hull_enabled` false, `hull_reason`); the sparks' quad expansion runs in
`weapon_sprite_vs.hlsl` (CPU twin `stage::streak_corners`), the glow rasterizes the light sphere's exact screen
rectangle. Programs (slots): sprite vs 69 / ps 85, glow vs 6 / ps 47. Rows: `weapon_effects_frame` gains `hull_staged=
hull_hits= flashes= sparks= glows= embers= budget_shed= orphans= floored= owners_dropped= capped= life_floor_ms=
table_hull_key_missing=`; `weapon_hull_frame` on capture frames; `weapon_effects_device` the four slot counts,
`hull=` and `hull_reason=`.

Deltas against the look note (and 5.2 of the architecture, which now points here):

- Reach: `v0 tau` = 1.98 U is the asymptote; at its mean speed a spark has gone 1.05 U by the end of a 250 ms life (k 0)
  and 1.54 U by 500 ms (k 1) [computed from the law; the drawn means at 200 ms are 0.80–0.96 U, the law rows]. The
  note's "reach ≈ 2 U" and the chase-nose "2 U past the nose" are the asymptote. The chase gate measures the sparks
  alone 200 ms in: 405 px outside the hull's outline (all of the 405 lit; the earlier figure, 3277 px, was the
  first frame with the flash and glow).
- The hidden half is measured by radiance over eight records per calibre: a pair grazing the plane is hidden on both
  sides by the soft term (record seq 438, k 0.5, alone: 0.425); the lit-pixel ratio reads 0.52–0.54 (a pair along the
  view axis overlaps itself on the sky).
- Unspecified by the note, chosen here: spark radiance `spark_e` 4 (the core's) x ramp(T) x T, rest length 3 widths
  floored at 12 px (the gate's name applies the bolt floor to sparks), the halo's e-fold 1.5 core radii.
- `look::budget_weight_glow` re-pinned 0.40 (the note's [I] 0.25): one screen of glow 71.4 µs at 5120x1440 / 177.5.
  Four of the ten runs that measured that row read 109–110 µs, six 71.0–72.3 (see Open).
- The resolve scar test runs on a plane lane 100 view units behind the spark, not over the held far lane of
  bolts-through-taa.
- Kill switches as header constants: `hull::mode` (simple), `hull::spark_count_x`, `hull::glow`.

Figures [M], `verification/results/weapon-effects/hull_figures.py` → `hull_figures_out.txt` (the records of this
directory's native run after the review fixes, RTX 5070 Ti 32.0.16.2114): hook PASS 90/0 (15 step-2 checks: the
shields,hull block and the ring's eviction order; handler 0 calls, 0 x87, 1122 instructions, frame 200 B; 50 events
17.7 µs median); D3D PASS 96/0 (46 step-2 gates). AP DLL clean at -j4, 0 warnings, sha256 `14874a92…`, no-x87 PASS
(1162 reachable, 0 violations). 74 `test_weapon_effects_*` OK; lab, programs and config `--check` PASS. Acceptance
run: 44 s (lock to exit), after six attempts in which the device could not be created while the Windows session was
locked (LockApp.exe up) and one that failed its 30 µs event-cost check, its short draws slow, under another
session's CPU-bound process; the hull timing rows now take 120 untimed draws before their best of 60.

| gate / row | measured | law / bound |
| --- | --- | --- |
| sparks' first-frame centroid / flash centroid | 0.006 U / 0.000 r_hit | ≤ 0.25 U / ≤ 0.1 r_hit |
| hidden share k 0 / 0.5 / 1 (radiance, 8 records) | 0.506 / 0.505 / 0.501 | 0.45–0.55 |
| glow on sky / centroid | 0 px / 0.000 R | 0 / ≤ 0.3 R |
| glow peak, half life, end | 0.8496, 0.4265, 0 | 0.85, 0.4266, 0 |
| cooling hue at half life (g / r), ramp error | 0.4234 vs 0.4239, 1.6e-3 | ±0.03, ≤ 2.5/255 |
| count / flash r / glow R / ember R / width, 3 tiers x k 0, 0.5, 1 | all exact (8 / 16 / 24) | ±5 % |
| mean reach at 200 ms (fighter k 0 / 0.5 / 1) | 0.90 / 0.95 / 0.96 U | the law, ±5 % (exact) |
| floors at 2 km: width, length, flash core | 3.000, 12.000, 3.012 px | ≥ 3, 12, 3 |
| flash frames at SETA 6 / 10 | 3 / 3 | 3 |
| SETA engaging mid-life (16 → 167 ms steps), elements back | 0 (floor latched 48 ms) | 0 |
| no pop at 12 hits/s (60 frames, image vs the same records aged) | 0 px | 0 |
| no pop across a step change (6 records, 40 frames, 16 → 100 ms; each flash, glow, ember against its law) | 0 of 720 | 0 |
| chase nose: sparks alone at 200 ms outside the outline | 405 px (of 405 lit) | ≥ 20 |
| first hull hit on an owner drawn the frame before: velocity, mean spark head at 100 ms | 1250.0 / 127.838 view units | 1250 / 127.838 |
| hull programs refused: patch draws, sprites, glows | 1, 0, 0 (hull_reason hull_program_create) | 1, 0, 0 |
| far-side hit / near flash on its hull | 0.0 / 0.9999 of the sky's | 0 / ≥ 0.9 |
| cockpit own / other | 0.350 / 1.000 | 0.35 / 1 |
| R32F lane / four-channel | 1.0000 | ±1 % |
| resolve core 0 / 4 / 8 px, residual after 8 frames | 1.000 each, 0 | ≥ 0.95, ≤ 1/255 |
| CPU 64 hits on a capital (1344 sparks, 128 rects) | 26.6 µs best, 41.9 mean | ≤ 30 µs |
| CPU 20 hits on a fighter | 9.8 µs best | — |

Host checks (`test_weapon_effects_core`): 40 owners, one hull hit each: 32 drawn, owners 1–8 (the oldest newest hits)
dropped, the same set the next frame; a new hit on owner 1 brings it in. The hook fixture: `hull,shields`,
` shields , hull ` and `hull, shields` parse to shields,hull; "hull", `shields,shields`, `shields,` refused; a staged
hull hit forwards while `hull_ready` is 0; with only staged records the oldest goes whatever its role.

GPU, EVENT-fenced best of 60 (µs; 1080p / 5120x1440): 20 hits on a fighter 200 m away (the 3.1 budget) 21.3 / 24.3
(≤ 50 at 5120); the same 60 m away (its hull a fifth of the frame, raster 1.38 screens) 28.6 / 96.2; 64 hits on a
capital close 38.9 / 58.5; one glow filling the frame 31.0 / 71.5 (the 0.40 weight). Before the performance pass: CPU
63.8 µs (CPU expansion, 36 B vertices), GPU 108.3 µs for the 60 m row (view-aligned boxes, 1.61 screens rasterized).

Open:

- Not flown. Flight per 3.1: `weapon_effects = shields,hull`, with the regenerated table for the repair variant.
- The 60 m stress row exceeds 50 µs at 5120x1440 (96.2): 20 overlapping glows at 3 r_hit on a hull a fifth of the
  frame; the budget sheds them at 8 screens, not at that cost.
- The CPU mean (41.9 µs) is above the 30 µs bound the best meets.
- The full-screen glow row read 109–110 µs in four runs against 71–72 in six; the warm-up brought it back, the cause
  (GPU clock after the CPU-only cases, as far as the evidence goes) is not established.
- The lab renders the law on its preview hulls; its sparks are not the stage's (their own rotation and speeds).
- The budget's shed loop is O(glows x shed) (a selection per shed glow), only while over budget.
- Missile-victim classing: an impact whose victim is class 10 takes the projectile branch (the handler's victim-first
  rule), so a missile shot down by a bullet is not a hull hit; not reviewed against the engine.
- `table_hull_key_missing` also reads 1 for a mod table that carries none of REPAIR, RL, TUG.

### Hull sparks unseen in the lab; visibility and r_hit retune (2026-11-06, on `a78c8c2f5`)

User report on the lab's hull presets (`hull/capital-ppc-flank-300m`, `hull/fighter-ire-side`): one bright blob that
fades to a small red mark, no sparks. **Cause: a lab bug.** `renderHull` drew every spark quad (13 / 21 per frame, the
law's count) but sampled its width from `w = -hw` in 0.5 px steps; below a 1 px streak (the 5120 floors scaled to the
480 px canvas: 0.4–0.7 px wide, 1.2–2.1 px long) the only sample was the profile's edge, `(1 - v^2)^2 = 0`, so the
sparks deposited no light (0 lit px on the capital at every age, at most 11 px of peak 0.004 on the fighter side). The
stage drew them: before the change the fixture's sparks alone were 3.3 px wide on the capital and 5.1 px on the
chase-nose fighter at 1080p. Fix: the lab samples midpoints across a footprint of at least one canvas pixel and keeps a
thinner streak's energy (x lit / footprint); with the old constants the capital then lights 34–38 px at 100–300 ms
(peak 0.36–0.38 after `1 - e^-v`), the fighter side 20–23 px.

Visibility raised in the header, read by the stage and injected into the lab: `look::floor_len_px` 12 → 24
(`floor_lit_px` stays 3; the hull sparks are the floor's only reader), `hull::spark_width_u` 0.04 → 0.08 U,
`hull::spark_e` 4 → 8 (additive; the hold `streak_core_min` 1.2 unchanged), `hull::spark_tau_s` 0.33 → 0.45 s (reach
asymptote 2.7 U; τ shapes the deceleration, the lives stay 250 + 250 k ms), and the new `hull::r_scale` 2: r_hit =
2 U (0.35 + 0.65 k), so the flash, glow and ember double; the sparks' reach and width stay in U. Gates renamed with the
law: `hull_spark_reach_asymptote_2_7u` (±20 %), `hull_streak_min_footprint_3px_24px`. The inward-half occlusion and
the three-frame floor are unchanged (`hull_half_of_sparks_hidden_by_flat_lane` 0.498 / 0.507 / 0.500 after, 0.506 / 0.505 / 0.501 before).

Fixture [M] (`verification/results/weapon-effects/hull_spark_rows.py` → `hull_spark_rows_out.txt`, native RTX 5070 Ti;
the before run is this change's new rows on the old constants; two before runs lost the device at Reset while the game
was up on the host, so before timings are the section's earlier table):

| sparks alone, 1080p | width px | rest px | lit px 100 ms | lit px 200 ms | peak luma 100 / 200 ms |
| --- | --- | --- | --- | --- | --- |
| fighter chase nose (IRE k 0.33), before | 5.14 | 15.4 | 208 | 502 | 2.07 / 0.88 |
| fighter chase nose, after | 10.27 | 30.8 | 722 | 1794 | 5.39 / 2.25 |
| capital flat face 300 m (PPC k 0.8), before | 3.28 | 12.0 | 499 | 564 | 2.84 / 2.02 |
| capital flat face 300 m, after | 6.55 | 24.0 | 1901 | 2264 | 10.23 / 4.35 |

Chase-nose gate 405 → 1546 px outside the outline; floors at 2 km 3.000 / 24.000 px, flash core 3.012 px.

Lab [M] (`verification/results/weapon-effects/hull_spark_lab_probe.py` → `hull_spark_lab_probe_out.txt`, 480x270):
r_hit fighter IRE 1.69 → 3.39 m, capital PPC 13.92 → 27.84 m (glow R 5.1 → 10.2 m and 41.8 → 83.5 m). Sparks alone
after: fighter side 31 / 33 lit px at 100 / 200 ms (peak 0.95 / 0.72), capital 35 / 55 px (0.98 / 0.75), chase nose 31
/ 91 px; the side camera frames 4 U + 2 glow R, so the doubled r_hit draws the hull farther and the sparks smaller on
the canvas (0.68 / 0.51 px wide, the 24 px floor 2.25 px).

GPU, EVENT-fenced best of 60 (µs, 1080p / 5120x1440), after: fighter 200 m 20 hits 20.5 / 45.6 (`hull_budget_20_hits_5120`
≤ 50 holds, 24.3 before), fighter 60 m 61.6 / 293.6 (96.2 before), capital close 64 hits 61.4 / 132.8 (58.5 before),
one glow filling the frame 30.4 / 71.0. The rise is the glows' area (2 x R, 4 x the area). AP DLL clean at -j4, 0
warnings, sha256 `99b4fbf2…`, no-x87 PASS (1162 reachable, 0 violations); D3D PASS 96/0, hook PASS 90/0; 74
`test_weapon_effects_*` OK; lab and programs `--check` PASS.

Open: the 60 m stress row is 3 x its earlier figure (293.6 µs at 5120x1440) and the 3.1 budget case has 4.4 µs of
margin; the budget sheds glows by screens (8), not by this cost. The lab's spark test counts quads only
(`test_hull_presets_render_their_elements`), which is why the zero-light sparks passed it.

### Burst, randomised sparks, heavy ring (2026-11-06, dirty on `826d5ebc6`)

After Run240 Flight G (the user: "need good modern effects, not lazy flashes"; "sparks read as the same decals
appearing and disappearing"). The look is [weapon-look.md](../architecture/weapon-look.md) 3.1's dated paragraph.
Built, not flown. **Stage** (`weapon_effects_hull.h`): the burst (the fireball impostor, `renderer::
WeaponFireballVertex`, 44 B, at most 256 a frame) on every hull hit but repair, tug and asteroid; the flash halo 0.5
r_hit; sparks with a hashed count (x 1 ± 0.4), hashed pair directions (antipodal, rejection from the cube), per-spark
speed / life / length / width factors from one mix's bytes, a quarter curling (`spark_curl_x` 0.3, undamped);
the heavy ring (`hull_heavy`: missile source, `style_burst`, k ≥ 0.6) as a sprite-batch quad of a third kind (c[0] 2,
its own draw after the sprites, at most 64), its normal latched at the first frame (`ring_normal_local`: the bubble
through the shield stage's `BubbleCache`, `ring_walks_per_frame` 1; a station's face axis). The shared budget charges
the bursts (`budget_weight_fireball` 1.04, `_lod` 0.76, from the measured full-screen cost); over it: (1) one octave,
(2) other owners' glows oldest first, (3) other owners' bursts oldest first. **Pass**: `weapon_fireball_{vs,ps}.hlsl`
(slots VS 6 / PS 309) created with the hull programs (one refusal turns the hull off as before), drawn after the
patches and decals and before the sprites; `WeaponEffectsStep::Fireballs` / `Rings`; the sprite VS reads the length
factor from |c.x| and passes the ring kind (sprite VS 72, PS 103 slots, were 69 / 85). **Rows**:
`weapon_effects_frame` gains `bursts= rings= bursts_shed= burst_lod=`; `weapon_hull_frame` `bursts= rings=
bursts_shed= octaves= burst_screens= ring_walks= ring_fallback=`; `weapon_effects_device` `fireball_vs_slots=
fireball_ps_slots=`. `HullState` 131 KB (the cached pair directions, 36 a record; was 18 KB). The engine shimmer puff
is not built (the pass lives with the plumes: 3.1's paragraph).

Deltas against the brief, chosen here: no "plasma" style exists in the classifier (tracer / slug / orb / wave / beam),
so the plasma weapons are heavy by k ≥ 0.6; "flak/area flag 0x100" is the record's `style_burst` (`flags & 0x1100`); the
spark length factor applies after the 24 px floor (every streak ≥ 24 px x its factor, ≥ 14.4 px); the asteroid gets no
burst (rock dust); the burst's first frame has R 0 (the flash carries the birth).

Figures [M], `verification/results/weapon-effects/hull_burst_rows.py` → `hull_burst_rows_out.txt` (the acceptance run,
RTX 5070 Ti 32.0.16.2114, 39 s lock to exit): D3D PASS 114/0, hook PASS 90/0 (handler 0 calls, 0 x87, 1122
instructions, frame 196 B; 50 events 17.0 µs median).

| gate / row | measured | law / bound |
| --- | --- | --- |
| burst R at 0.1 / 0.3 / 0.6 L (IRE fighter, L 285.9 ms) | 12.485 / 21.983 / 24.941 view = the law; lit extent 1.011 / 1.007 / 1.004 R_px | ±0.1 %; 0.75–1.03 |
| burst colour, brightest pixel at 16 ms / 0.9 L | g/r 0.872, b/r 0.679 / g/r 0.182, b/r 0.055, 0.026 of birth | white ≥ 0.75 / 0.45; dark red ≤ 0.3 / 0.2 |
| burst against the lane: at the hit / in front / behind | 0.582 / 0 / 1.000 of the sky's | 0.35–0.65 / ≤ 1e-3 / ≥ 0.99 |
| composition, 40 frames (30 x 16 ms, 10 x 100 ms), 6 elements | 0 of 240 law violations; image = sum of parts within 1e-5 | 0 |
| spark randomness: seq 828 / 831 | 21 / 13 sparks; worst best-match cos 0.612; same frame identical; straight sparks cos 1.000000 frame to frame | differ / identical |
| curled share (6400 sparks) / bend 100 → 250 ms | 0.259 / ≥ 2.86° | 0.25 ± 0.02 |
| hidden half k 0 / 0.5 / 1 (radiance, 8 records) | 0.519 / 0.511 / 0.481 | 0.45–0.55 |
| count / sizes / reach / lives, 3 tiers x 3 k | count = `spark_count` within its spread; flash halo 0.5 r_hit; widths per factor; reach = the hashed law exactly; all alive at 0.7 law, none at 1.3 | ±5 % |
| floors at 2 km | width 3.000 px, length / its factor 24.000 px, shortest 14.447 px, flash core 3.012 px | 3 / 24 / 14.4 / 3 |
| ring on the narrow corvette's mid flank, 0.2 / 0.5 / 0.9 of its life | ρ = the law, cos to the bubble normal 1.000000, centre error 0 r_hit; one walk, no fallback; the bubble normal 27.6° off the radial | ±0.5 %, ≥ 0.999 |
| ring families: light slug / burst bit / k 0.8 / missile; station | 0 / 1 / 1 / 1; station plane on its face axis (corner z spread 0) | — |
| budget, 12 capital hits, 0.5 screens left | octaves 2 → 1, 11 of 12 bursts shed (other owner), rings kept; own ship 0 shed | LOD, then shed |
| burst through the production TemporalPass / after 8 frames | 1.000 / 0 | ≥ 0.9 / ≤ 1/255 |
| fireball draw fault / Reset / refusal | reported at `Fireballs`, next frame draws; recreated after Reset; refused with the hull programs | — |

CPU, best of 200 (i686, µs): 64 heavy hits on a capital (1328 sparks, 64 bursts, 64 rings) **33.9** (sparks alone
26.0, all but the sparks 8.8; before the performance pass 61.3); 20 hits on a fighter 6.9. `hull_cpu_64_hits` re-set
30 → **40 µs** (26.6 before this change for the flash, sparks, glow and ember). The pass: one mix per spark split into
its factors' bytes, the pair directions cached at the first frame, the direction rotated once a pair, the heat ramp
packed in a 1025-entry compile-time table (host perf: the ramp and the pack were 25 % of the spark loop).

GPU, EVENT-fenced best of 60 (µs; 1080p / 5120x1440): 20 hits on a fighter 200 m away **35.5 / 60.9**
(`hull_budget_20_hits_5120` re-set 50 → **70 µs**; 45.6 before the bursts); the same 60 m away 90.0 / 453.5 (bursts
1.16 screens; 293.6 before); 64 hits on a capital close 338.1 / 1001.5 (the budget at work: 39 bursts at one octave,
every glow shed, 7.56 screens; 132.8 before); one glow filling the frame 34.5 / 70.7; one burst filling the frame
62.3 / 184.9 (weight 1.04), at one octave 47.4 / 136.1 (0.77).

Lab: `renderHull` draws the burst (the shader's chord, noise and ramp ported), the hashed sparks (the stage's `mix32`
/ `element_hash` / factors / directions; `test_spark_hashes_match_the_stage` compares them with a host build of the
header for three sequences) and the ring; the variant `flak`; presets `hull/heavy-plasma-corvette` (HEPT, k 0.63),
`hull/heavy-flak-fighter` (stock FLAK_ALPHA, k 0.83), `hull/heavy-missile-hit`. The "ours" canvas is **720 x 405**
(1.5 x, both families; the vanilla stays 480 x 270), the side camera framed on 4 U + one glow radius (was + two).
`test_hull_presets_render_their_elements` now counts each element's light and lit pixels, not its quads. Lit pixels at
40 ms [M, `hull_burst_lab_probe.py` → `hull_burst_lab_probe_out.txt`]: IRE fighter side flash 324, burst 1253, sparks
46, glow 11458, ember 452; PPC capital 300 m flash 502, burst 1246, sparks 36, ring 996; heavy flak fighter ring 986;
the chase nose's sparks 104 px at 200 ms; the cockpit preset's hit lies below the canvas (its flash, sparks and ember
unseen by geometry).

Open:

- Not flown. Flight per 3.1 with heavy weapons (PPC / HEPT on a corvette, flak, missiles) and a capital fight.
- GPU: the 60 m stress row 453.5 µs and a close capital fight at the full 8-screen budget (1.0 ms at 5120x1440): the
  burst (1.5 r_hit, 42 m on a capital) is the largest element; the budget bounds it, the flight decides its size.
- The burst shader is 309 slots, 185 µs a full screen at 5120x1440; its octave LOD is per frame, not per burst.
- The ring normal of an unshielded ship walks once (the cache holds the shield stage's owners); a deferred walk
  latches the radius sphere's normal for that record.
- `hull_spark_lab_probe.py` (the previous section's lab probe) no longer runs on the page (its regexes name the 480 x
  270 renderer); its figures stand for that commit.

### Cockpit: the own ship's patch on the eye sphere (2026-11-06, dirty on `826d5ebc6`)

Run240 Flight G: in the cockpit the own-ship shield patch read "cut at the screen's horizontal centre, only the bottom
half shows". Triage (CPU model of `fill_bubble` and the bubble pixel program, `verification/results/weapon-effects/
cockpit-half/` in the main checkout) found no rasterisation, budget, cull or mapping defect: with the eye inside the
hull-centred bubble (a corvette, half-extents (200, 150, 632), the eye at (0, 10, 300)) and the hits on the lower nose
(18 of 20 own-ship hits came from below), the lit far side lies at or below the eye's horizontal plane, which projects
to the centre line: the late set lit top 3502 / bottom 28521 px [M, the model, 1024 x 288]. **Fix** (coordinator
decision): in the cockpit view the own ship's bubble is a sphere centred on the eye, radius its smallest bubble
half-extent (never under `eye_sphere_min_m` 15 m), each hit mapped by its direction from the eye; cells on that
sphere's face lattices as before; its far side draws; the 0.35 dim stays; chase view and other ships unchanged
(`stage::Tuning::own_node / cockpit`, the glue resolves the own ship and the view before the build;
`weapon_patch ... eye_sphere= eye_r_m=`).

Fixture [M] (the same run, `EYE_SPHERE` rows): the triage's scenario at 2048 x 576 (32:9, half-height tangent 0.5625),
level and pitched ±10°, sky lane: radius 150 (the bubble's smallest half-extent); every set's light on its hits' side
(6 of 6: late lit row 362.8 / 316.8 / 413.8 against the centre 288, the hits' 418.4 / 326.0 / 519.5; early 254.7 /
206.1 / 293.5, hits 234.7 / 139.6 / 326.2, the last within 5 % of the centre, not gated); the rows either side of the
centre line continuous (late level 5504 above / 5780 below in the 8 rows each side); 48 single hits all lit, each patch's
centroid on the sphere (luma x solid angle) **1.08 cells** from its hit's eye-sphere point at worst, gated at 1 + 1/√3
cells (the footprint is centred on the hit's cell, up to 1/√3 cell off the hit, and the planar lattice stretches on a
sphere of five cells' radius); 0 lit pixels outside the draws' boxes; the chase view of the same records identical
with the own ship known or not (0 changed). The cockpit dim gate unchanged (0.350).

Open: a cell of 6 m on a 30 m sphere makes each corvette hit's footprint ((rings + 0.5) cells, 75 view units) a cap of about 29° radius (the late set lights
362004 of 1179648 px at level); the flight decides whether the eye sphere wants larger radius or smaller cells. Not
flown.

### Hull impact classes (2026-11-07, dirty on `926026c19`; [hull-impact-classes.md](../architecture/hull-impact-classes.md))

The ratified design built on the burst rebuild; not flown. **Hook**: `core::hull_class_from` (the rule of section 2, integer
only, `always_inline`) and the table's pin write the class into `Event::detail` bits 4-7 (`HullClass` energy 0 ..
beam 5); the rule also sets the repair (0x80) and tug (beam bits, damage 0) variants; a class pin replaces the rule's
class and variant; missile sources keep the nibble 0. **Table**: the override's `hull` key takes `energy | ballistic |
plasma | heavy | ion` besides `repair | tug` (`beam` not pinnable; schema 2), `row_hull_class` 0x80 with
`Table::hull_class`; `weapon_overrides.json` carries the census script's 26 `by_id` pins (stock 11 used, Mayhem 3 22, FL
= stock). **Stage** (`weapon_effects_hull.h`): one row per `Anatomy` of the `hull::class_*` grids (the asteroid victim,
repair, tug and a missile source first, then the nibble): flash, puff (the fireball impostor riding the owner; the
plasma globe shrinking, its tint mixed in while hot), three spark sets with the shot-heading bias `normalize(+-u + b
a)` (a = `Event::dir` through the owner's rows at the first frame; b 0 without it) and the flicker duty (ion, the
missile's slow debris), glow, ember, the surface ring (the sphere shell cut by the lane; the tangent-plane ring and its
bubble walk are gone), the ion filament glow, the heavy entry streak, the shimmer request; the scorch store (128 marks
of 36 B, 16 per owner, laid at a record's first frame, cleared on a scope change); the hold rule's spots (64 of 100 B:
beam, repair, tug records leave the ring; a pulse restart after a gap flashes; scorch marks every 0.5 r of a crawl);
the amended shed order. `HullState` 142,096 B. Record life: per anatomy since the review (below); the bound
`hull_lifetime_ms` 3,601 (the plasma hot spot's 3 s x 1.2; was 1,441). **Pass**: `weapon_shape_vs` (10 slots), `weapon_ring_ps` (51), `weapon_filament_ps` (247), the
fireball's tint input (VS 7, PS 311; vertex 60 B) and per-puff octave (a negative seed); scorches drawn first with the
glow programs under ZERO / INVSRCCOLOR (`caps().scorch_enabled` from the blend caps), then glows, rings, filaments,
patches, decals, puffs, sprites; steps `Rings`, `Filaments`, `Scorches`. **Shimmer**: the engine shimmer pass is
requested with the plumes or in shields,hull mode; `run_engine_shimmer` appends the frame's hull rects
(`engine_shimmer::weapon_rect`, at most 2) after the engines'; the program takes a per-rect strength (c52..c67; an
engine rect 1). **Rows**: `weapon_event ... class=`; `weapon_effects_frame` gains `streaks= chips= filaments= scorches=
spots= merges= shimmer= jet_no_dir=`; `weapon_hull_frame` the class counts per anatomy; `engine_shimmer ...
weapon_rects=`; `engine_shimmer_config ... weapon_hull= weapon_max=`.

Deltas against the design, chosen here: the record life is the anatomy's slowest element (the design kept 1,441, but
the plasma hot spot, the heavy hole and the missile ember outlive it); the splash-back and back-jet gates read at 100 ms and 30 deg
(the design's 200 ms leaves only the slow set and chips, whose b -0.3 gives 2 b / 3 = 0.2 of the reach; b -2 bounds
the cone at asin(1/2) = 30 deg, not 25); the chips' gate compares their speed fraction at 600 ms with the slow sparks'
(the design's "centroid beyond" contradicts its own speeds 2 vs 4 U/s); the greyscale gate normalises each age's
profile and takes the larger L1 of the two ages; the flashes are charged to the budget at their measured weight and shed
last (the plasma bloom's 2 r halo on a close capital was 2.3 ms uncharged); a puff under 96 px (1440 rows) draws one
octave at any budget (the design's fighter-range LOD); the beam's trail ember is the spot's own (marks are scorches
only); the lab previews the heading as the hull's inward normal.

Figures [M], `verification/results/weapon-effects/hull_class_rows.py` -> `hull_class_rows_out.txt` (the acceptance run,
RTX 5070 Ti 32.0.16.2114): D3D PASS 152/0, hook PASS 92/0 (handler 0 calls, 0 x87, 1,237 instructions (1,122 before), frame 200 B; 50 events 18.1 µs median), engine shimmer PASS 51/0 (43 + 8 weapon-rect checks).

| gate / row | measured | law / bound |
| --- | --- | --- |
| class nibble at the hook (16 stock cues: tug, repair, beams, ESR, IPG_FRAG, IONC, MASS, MAM, GAUSS, PSG, PPC, IRE, flak, the 20,000 edge) / the table (orb body, MD pin, pin over 0x40, over 0x80, out of range, missile) | 16 of 16 / 7 of 7 | all |
| anatomy round trip (10 anatomies, one frame each) | 10 of 10 | all |
| scorch on a half-hull white scene: sky changed / hull pixel error | 0 px / 2.0e-4 (s 0.380) | 0 / ≤ 4e-3 |
| scorch store: record released, at 6 s / at L_s / 17 marks on one owner | 0.16875 = law / 0, mark freed / 16 kept, the first gone | law ±1e-4 / 0 / 16 |
| scorch through the production TemporalPass, 0 / 8 px a frame | centre 1.000 / 1.000, residual after 8 frames 0 / 0 | ≥ 0.5, ≤ 1/255 |
| greyscale lineup, 9 classes, 8-bin radial profile at 100 and 400 ms | min pair L1 0.216 (plasma, missile) | ≥ 0.2 |
| energy puff R at 0.2 / 0.5 / 0.95 L; g/r of its brightest pixel; owner moved 20 | radius error 0; 0.614 / 0.362 / 0.182; dx 20.000 | ±5 %, falling, 20 |
| energy spatter n at k 0 / 0.5 / 1 (law 4 / 6 / 8) / ember at 300 ms, 1,500 ms, after (flash 50 ms) | 3 / 6 / 9 / 0.78 of peak, lit, 0 | within ±40 % / ≥ 0.5, > 0, 0 |
| ballistic lit sparks fast 100 / 300 / 500 ms; fast at 250; slow at 450 | 8 / 0 / 0; 0; 4 | gone by 250, alive at 450 |
| ballistic splash back at 100 ms along -dir / isotropic without dir (32 records) | 0.346 / 0.016 of the reach | ≥ 0.3 / ≤ 0.05 |
| ballistic speed fraction at 600 ms: slow sparks / chips (law 0.264 / 0.549) | 0.289 / 0.612 | chips ≥ 1.5 x |
| ballistic dust puff lit beyond 0.5 r | 0 px (4,912 lit) | 0 |
| plasma bloom halo / energy halo; lit radius ratio | 2.000; 1.81 | 2 ±5 % |
| plasma globe R at 0.3 / 0.65 / 0.95 L; b/r (a blue tint) | 19.7 / 18.3 / 13.5 view; 0.654 / 0.420 / 0.368 | falling, reddening |
| plasma ring on half a hull: sky / lane; radius at 0.1 / 0.2 / 0.5 L | 0 / 47,119 px; 135.5 / 187.5 / 215.7 px (law 134.7 / 186.6 / 215.3) | 0; ±5 % |
| heavy entry streak angle to the projected shot; its inside share on a flat lane | 0.000 deg; 0.039 | ≤ 2 deg; ≤ 0.1 |
| heavy back-jet launch within 30 / 25 deg of -dir (248 sparks) | 0.891 / 0.468 | ≥ 0.75 within 30 |
| heavy exit jet lit beyond a 1 U plate's edge at 0 / 150 / 400 ms; 10 U | 0 / 1,217 / 1,211 px; 0 / 0 / 0 | 0, > 0; none |
| heavy shimmer: at 100 ms strength / at 401 ms / of 3 / over budget (other, own) | 0.750 / none / 2 / 0, 2 | 1 - t/400, 0, 2, own only |
| beam: 60 ticks at one point: spots / ring records left / later pulses / core, glow | 1 / 0 / 0 / 4.000 = 4.000, 0.850 = 0.850 | 1, 0, 0, equal |
| beam sweep 0.3 r a tick, 20 ticks: marks / gaps / pulses after the first | 10 / 0.599-0.601 r / 0 | 0.5-0.6 r, 0 |
| beam pulse: core at hold end / 81 ms after / restart after 1,322 ms: pulses, flash / next tick | 1 / 0 / 1, 1 / 0 | |
| ion filaments: sky / lane px; Jaccard 0-30 ms / 0-100 ms / owner moved 128 px | 0 / 5,741; 0.999 / 0.078 / 1.000 | 0; ≥ 0.9 / ≤ 0.5 / ≥ 0.9 |
| ion: glows, embers, puffs, scorches, marks / at 600 ms everything | 0 / 0 | 0 |
| missile debris 2 x ballistic counts; slow set lit share 1.1-1.4 s; ring at 50 ms | yes (27 + 13); 0.219; 1 | 0.3 ±0.1 |
| repair 60 ticks: one glow a frame, colour | yes, 1.000 = 1.000 | |
| asteroid: glows, embers, marks, flashes / puff peak | 0 / 0.022 | 0 / ≤ 0.5 |
| composition, 7 record classes x 40 frames (30 x 16 ms, 10 x 100 ms), 8 element counts | 0 of 2,240 violations; image = sum of parts within 2e-5 | 0 |
| three-frame floor at SETA 6: flash (heavy: and streak) per class | 3 frames each | 3 |
| faults at Scorches / Rings / Filaments; Reset (the scorch mark draws again) | reported at the step, the next frame draws; recovered | |
| weights at 5120x1440 alone filling the frame: ring / filament / scorch / flash | 71.4 / 152.7 / 86.8 / 88.3 µs = 0.40 / 0.86 / 0.49 / 0.50 | pinned 0.40 / 0.86 / 0.49 / 0.49 |

Open:

- Not flown. Flight per section 9's kill criteria, a lineup at the user's eye (the lab presets `hull/class-lineup-*`).
- The per-class 20-hit fighter bounds at 5120x1440 are the measured ones with margin, not the design's 50 µs: energy,
  plasma, heavy and missile miss 50 (the glows of 3-4 r_hit dominate: a 4 r glow at 200 m is 0.85 screens for 20 hits).
- The CPU bound stays 40 µs (64 mixed hits 33.7 best, 1,066 sparks); the design's 30 µs (1,600 quads, [I]) is not met.
- The heat shimmer is not previewable in the lab and the hull rects are not driven through `MotionOutput` by a fixture:
  `run_engine_effects.py` needs the local shader-sweep programs (`/tmp/x3-shader-sweep/programs`, absent on this host),
  so its record stays bound to the previous sources.
- Unknowns 1 (the `dir_fallback` rate: `jet_no_dir=` counts it) and 2 (one beam record per frame: `merges=`) are read
  in flight.

CPU, best of 200 (i686, µs): 64 mixed hits on a capital (the classes in turn: 1,066 sparks, 56 puffs, 24 rings, 8
filament glows, 16 scorches, 8 streaks) **33.4** (sparks only 25.9, all but the sparks 9.1), mean 44.9; the burst
rebuild's 64-hit row (now energy) 18.8, 20 fighter hits 6.1. `hull_cpu_64_hits_mixed` bound 40 µs (the design's 30 is
missed by 3.4 µs).

GPU, EVENT-fenced best of 60 (µs; 1080p / 5120x1440), 20 hits on a fighter 200 m away per class, against the design's
section 6 estimate (20 x its per-hit µs) and the new bound at 5120x1440:

| class | 1080p | 5120x1440 | design estimate | bound |
| --- | --- | --- | --- | --- |
| energy | 25.0 | 53.6 | ~60 | 60 |
| ballistic | 26.7 | 29.6 | ~60 | 40 |
| plasma | 31.1 | 91.0 | ~80 | 105 |
| heavy | 28.5 | 60.9 | ~100 | 70 |
| ion | 21.4 | 37.1 | ~60 | 45 |
| beam (20 spots) | 21.8 | 45.4 | ~80 | 55 |
| missile | 32.4 | 88.4 | ~120 | 100 |
| asteroid | 22.4 | 23.3 | ~60 | 30 |

64 hits on a capital at close range under the 8-screen budget, 5120x1440 (µs): energy 1,061, ballistic 777, plasma
1,247, heavy 1,131, ion 949, beam 1,015, missile 1,078, asteroid 62, every one within 8 x 177.5 = 1,420 (plasma was
2,334 before the flashes were charged: its 64 bloom halos alone charge 10.7 screens, so every sheddable box goes).
`hull_budget_capital_close_within_8_screens`: the sheddable screens within 8 (or none left) and the frame within 1,420 µs.

Reproduce:

```sh
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_weapon_effects.py --host native
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_engine_shimmer.py --host native
python3 verification/results/weapon-effects/hull_class_rows.py > verification/results/weapon-effects/hull_class_rows_out.txt
```

#### Review fixes (2026-11-07, same dirty tree)

- **Heavy band from the shot, not the applied total.** `core::hull_class_from` takes the bullet record's own per-shot
  damage, max(`+0x44` shield, `+0x80` hull) as the census script does; the applied total at `[esp+0x1c]` is a
  shields-down hit's hull damage alone, so stock PPC read energy and GAUSS ballistic. The tug is a beam whose per-shot
  damage is 0 (a damaging beam on a target that takes none stays a beam). Hook cases [M]: `heavy_gauss_hull_total`
  (shot 28,540 / 7,135, total 7,135), `heavy_ppc_hull_total` (63,778 / 15,944, total 15,944), `heavy_band_edge_hull`
  and `beam_invulnerable_target` (shot 3,969, total 0) all pass, 20 of 20 cues; the shield-hit and drained cases assert
  the low nibble and the class nibble explicitly. Handler 0 calls, 0 x87, 1,216 instructions, frame 204 B; 50 events
  17.9 µs median.
- **The hold rule for every beam.** `stage::holds` reads the class nibble and the repair / tug bits, not the anatomy, so a
  beam on an asteroid keeps its one spot (the asteroid's dust puff and rock spatter, no flash). `hull_beam_on_asteroid_holds`:
  60 ticks leave 0 records in the ring, 1 spot, 59 puff frames, 1,052 sparks, 0 flashes [M].
- **Record life per anatomy** (`core::hull_lives`: the slowest of ember, puff, glow, ring, flash, streak, filament at k 1
  x 1.2 and the spark sets x 1.3): energy 2,401, ballistic 1,041, plasma 3,601, heavy 2,401, ion 601, beam 1,801,
  missile 2,401, asteroid 1,441, repair 961, tug 181 ms. The stage releases a record past its anatomy's life (or its
  floor; `weapon_hull_frame ... done=`); Present keeps the 3,601 bound for frames the stage does not draw. The ring of
  256 overflows past 107 energy hits a second sustained (178 before the classes, at 1,441 ms), 246 ballistic, 71 plasma.
  The per-class composition gate now runs past a released record (ballistic, ion and asteroid released within its 40
  frames, 0 violations).
- **Flashes shed last.** Other owners' flashes, oldest first, after every other step (`flashes_shed=`). 128 plasma hits on
  a close capital: 80 of 128 flashes shed, 7.96 screens, **950.1 µs** at 5120x1440 (957.3 the run before) (286.6 at 1080p) within 1,420
  (`hull_budget_128_plasma_close_within_budget`); 64 plasma hits 941.6 µs (was 1,246.7).
- **Sprite cap** 2,048 sparks + 2 x 256 (a flash and a heavy streak per record) + 2 x 64 (a spot's pulse flash and core)
  = 2,688 quads: the sparks' own cap binds first.
- **One seed at both octaves**: a puff's seed lies in (0, 64], its sign alone selects the one-octave LOD.
- The scorch store's unreachable trail-ember branch removed (marks are scorches; the beam's trail ember is its spot's).
- The surface ring's box is rr + 3 w (the shell at its edge e^-9; 1.8 % at rr + 2 w), in the lab too.
- Rows: `engine_shimmer_scratch ... recreated=1` when the 59 MB scratch comes back after its idle release;
  `engine_shimmer_weapon ... weapon_rects=` on a family-row frame in shields,hull mode without the engine census;
  `weapon_hull_frame ... done= flashes_shed= pulses=`. Beam, repair and tug records leave the ring at the stage, before
  Present logs them, so no `weapon_event ... class=beam` row appears; the flight reads `spots=` and `merges=`
  (`weapon_effects_frame`).
- Host modules importable under the documented `PYTHONPATH=verification/probe` (`test_engine_shimmer`, `test_weapon_effects_lab`).

Figures [M], the same rows script, after the fixes (RTX 5070 Ti): D3D PASS 154/0, hook PASS 92/0. 20 fighter hits at
200 m, 5120x1440 (µs): energy 54.4, ballistic 27.7, plasma 94.7, heavy 61.8, ion 38.0, beam 46.2, missile 90.7,
asteroid 23.3 (the bounds unchanged); close capital, 64 hits: 64.2-1,135.9, all within 1,420. CPU 64 mixed hits 33.0
best (sparks only 25.9, all but the sparks 14.1); one run of the same build read 50.2 (a transient host load: the
burst rebuild's 64-hit row of that run was its usual 19.0); bound 40. Greyscale lineup min pair 0.211.

## Ring 1024 and the reach cull (2026-11-07, dirty on `41fd2a4fb`)

[M] measured, [I] inferred. Rows: `verification/results/weapon-effects-ring/ring_rows_out.txt` (from
`ring_rows.py` over the two native fixture logs); sizes: `ring_sizes_out.json` (from `ring_sizes.py`: the TU sizes of
`41fd2a4fb` and of the tree, `objdump -h` of the Run241 DLL and of `build-ring/d3d9.dll`); records:
`verification/results/native-windows/weapon-effects/{summary,hook-summary}.json`.

- **Capacity and sizes [M].** `core::ring_capacity` 256 → 1024 (a u16 free list, asserted). `Ring` 16,912 → 68,628 B,
  `HookState` 17,068 → 68,784 B, `HullSlot` 504 → 512 B (the cached reach; its floor in the padding before `p0`),
  `HullState` 142,096 → 537,360 B. Stack frames: `build_hull` 38,744 → 26,796 B, `build` 10,852 → 10,984 B. DLL `.data`
  3,285,392 → 3,337,104 B, `.bss` 22,898,184 → 23,464,872 B: static +618,400 B against the Run241 DLL. The build before
  the review fixes read `.data` 3,426,328 / `.bss` 23,301,928 (+544,680 B); since then the ring-sized scratch moved from
  `.data$` to `.bss` (89,216 B of file size gone [M]: `build` and `build_hull` have internal linkage, so their statics
  are no comdat sections) and the frame's located table adds 73,728 B (2048 × 36 B [I], the rest of the 73,720 B
  difference).
- **The two-bit stamp and the eviction order.** After both builds Present stamps every live record (`Ring::hidden`):
  `hidden_undrawn` unless its owner was drawn, `hidden_off_screen` unless in view. The handler's one victim scan evicts
  census-only records of an undrawn owner, then other census-only, then stage-owned of an undrawn owner, then other
  stage-owned, oldest first within each, with integer compares only. `evicted_visible=` counts evictions of records not
  stamped undrawn, per row period beside `overflow=` (it was cumulative before the review). A record acquired since the
  last stamp carries 0 (counts as drawn).
- **Handler [M]** (hook audit): 0 calls, 0 x87, 1,231 instructions, frame 204 B (stack depth 240 B, margin 1,536). 50
  events: median 17.5 µs on an empty ring (bound 30; 17.4 in the run before the review fixes), 62.6 µs on the full
  1024 ring (bound 150; 59.8 before; worst 103.4 in this run). `OVERFLOW`: 16 stamped-hidden records evicted first, then
  the oldest drawn one, overflow 17, evicted_visible 1.
- **Visible first [M].** Both stages order owners with no record stamped off screen first, then the newest hit (the hull
  stage: an owner with a hull hit first), then the address. `HULL case=visible_first`: 40 owners, 20 on screen with
  older hits, 20 behind with newer ones, cap 32: the first frame (nothing stamped) draws 12 on-screen owners, the next
  draws all 20, 20 in view. The shield stage (cap 16) host case: 0 then 16 of 16 on screen.
- **The reach rule.** A kept owner with a hull hit is out of reach when the sphere about its node position of 2 r + the
  records' largest reach, grown by the pixel floors at its far depth, misses the view volume (near, far plane + 5 %,
  four sides). Three terms: the attached elements' reach, the spark sphere's reach and the pixel-floor margin
  (`reach_floor_px()` 318.3 px). Ballistic fighter-tier hit at calibre 1, r 5 at z 1000: margin 449.3 = 10 (2 r) + 66.0
  attached + 124.6 spark + 248.8 floor view units [M]. Heavy: 614.1 = 10 + 124.8 + 202.2 + 277.1 [M]. The reach is
  computed at a record's first stage frame and again at its latch if the latched floor differs (review 3). The beam,
  repair and tug spots and the scorch marks are never culled.
- **Edge sweep [M]** (`HULL case=reach`, reworked in review 6): the lit range is scanned first, then six steps across
  it and three past the margin, each built with and without the cull and compared pixel for pixel. Ballistic at 100 ms:
  lit to 19.4 view units past the plane (2 r 10.0), lit pixels 505 → 24 over the six steps (471 in the last 16 columns
  at step 0, with the 2 r sphere already outside), 9 of 9 steps identical, 0 elements past the margin (unculled 274
  quads). Heavy at 30 ms with its streak: lit to 35.6, 1,317 → 247 px, streak built on every lit step, 9 of 9
  identical, 0 elements past the margin (unculled 284). In the run before the review only step 0 had lit pixels in the
  edge columns (steps 1-5 sat past the lit range). Lit to 19.4 of a 449.3 margin: the cull takes only owners well off
  screen.
- **Moving owner [M]** (`HULL case=reach_moving`): an owner closing at 15,000 view units/s is behind the camera 100 ms
  after its hits latched (its own sphere out of reach: `owners_out_of_reach=1`); the records' spark spheres (the
  segment p0 .. p0 + v t) reach the view, so all 9 build (`out_of_reach=0`), 36 sparks with and without the cull, 1,157
  lit px, identical images, and the stamp counts the owner drawn and in view (review 2: the stamp takes the cull's
  verdict, not the 2 r sphere).
- **CPU [M]** (best of 200): 1024 live / 64 visible 41.4 µs (bound 100; 41.2 before the review fixes; 136.9 before the
  cull), 496 records of 31 owners out of reach; 256 visible 124.5 µs (bound 300; 122.0 before), the shed order engaged
  (168 shed, 7.978 of 8 screens); 64 hits alone 19.3 µs (19.6 before; bound 40). Means are 25-40 % above the bests in
  this run (host load).
- **Owners past the caps (review 5) [M]** (`STAGE_OWNERS`, one frame of both stages and the stamp): each owner is read
  at most once a frame for both stages (`core::VisibleOwners::located`; five reads, no rows). 512 owners with a shield
  and a hull hit each: 496 owners located, 2,866 reads per frame, 94.7 µs through the fixture reader, 122.8 µs through
  `engine_memory::read` (9.8 ns per read). 1024 owners with one hull hit each: 992 located, 5,248 reads, 122.8 / 210.4 µs
  (16.7 ns per read). Before the cache each stage read its own past-cap owners in full (six reads each) [I]. The flight
  reads `owners_located=` on `weapon_hull_frame`.
- **Judgement calls.** The shield stage keeps the 2 r sphere (`visible_radius_scale`, past the bubble) for its owners;
  the hull stage uses its reach sphere. A record dropped by the budget still counts as drawn (its owner was kept and in
  reach; the stamp judges owners, not elements).
- **Accepted: a camera cut [I].** For one frame after a cut, records stamped hidden in the previous frame are evicted
  first or sorted last while on screen; the next stamp corrects it. No code change.
- **Fixture note.** Later cases hash their record sequences, so a case that acquires records on the shared ring
  restores `stage_ring.next_sequence` (the first native run of the cull failed the energy ember's 1,500 ms probe on
  shifted lives until the reach case restored it).
- **Rows.** `weapon_hull_frame ... out_of_reach= owners_out_of_reach= owners_located=` (capture frames);
  `weapon_effects_frame ... overflow= evicted_visible=` (both per period).

Figures [M], one native run after the review fixes (`run_weapon_effects.py --host native`, 64 s under the lock): D3D
PASS 167/0, hook PASS 95/0; host `test_weapon_effects_*` 77 OK; `build-ring` 0 warnings, `check_no_x87.py --edition ap`
PASS.

## Shock front (2026-11-07, dirty on `ade4de383`)

The heavy, plasma and missile surface ring as a shock front ([hull-impact-classes.md](../architecture/hull-impact-classes.md)
3.5): noise arcs, white-hot edge over a redder trailing band, the wash inside, the stretch along the shot's heading,
and the plasma / heavy refraction band through the engine shimmer pass. Lab first (`ring: noise`, `ring: refraction`,
`ring: wash` sub-layers; the four shock-front Scenarios set a 60 degree shot incidence), one set of constants
(`hull::ring_*`, including the seed axis, scale and element and the profile's windows) read by the stage, the ring
program (c4-c7 from the pass) and the lab (its generated HEADER block).

Gates [M], the tracked record `verification/results/native-windows/weapon-effects/summary.json`
(`run_weapon_effects.py --host native`: D3D PASS 176/0, hook PASS 95/0), the plasma ring at 0.3 L on the flat lane:

| Gate | Row | Result |
| --- | --- | --- |
| `hull_ring_front_noise_arcs_2to1_no_sky` | `ring_front_noise` | brightest pixel of 64 angular bins within 1.5 w of r, max / min per record sequence: 0x5eed 2.40, 7000 4.03, 7001 4.03, 7002 3.08 (each >= 2); 0 sky pixels on the half lane |
| `hull_ring_front_leading_edge_over_trailing_band` | `ring_front_bands` | per-circumference peak 0.482 at the edge over 0.270 in the band; R/G 1.13 at the edge, 2.48 in the band |
| `hull_ring_front_wash_inside_only` | `ring_front_wash` | wash on - off: 132,116 px inside the front, 0 px outside |
| `hull_ring_front_stretch_along_heading` | `ring_front_stretch` | heading in the lane's plane (sin phi 1): 1.2500 (want 1.25 +- 5 %) |
| `hull_ring_front_stretch_45_degrees` | `ring_front_stretch` | Event::dir (90, 0, 90), sin phi 0.707: 1.1043 (the 3-D law 1 / sqrt(1 - (1 - 1/s^2) sin^2 phi) = 1.1043) |
| `hull_ring_front_round_without_heading` | `ring_front_stretch` | Event::dir 0: 1.0000 |
| `hull_ring_front_newborn_finite` | `ring_front_newborn` | r 0 at the first frame: 0 non-finite pixels (the wash exponent clamped) |
| `hull_ring_front_shimmer_request` | `ring_front_shimmer` | heavy hit at view z 3,900, 40 ms: ring_r 64.513, strength 0.683, stretch 1.25; 0 with the ring off |
| `hull_ring_front_shimmer_capped_and_charged` | `ring_front_shimmer` | at view z 300 the ring's rect passes 0.25 screens: drawn, no band; the frame's screens 0.3893 with the kept shimmer rect, 0.1685 without |
| `hull_plasma_ring_on_surface_not_sky` (wash off now) | `plasma_ring_surface` | worst radius error 0.0266 (was 0.0055: the trailing band pulls the per-circumference peak inward) |

The ring's box is the capsule of radius r + 3 w about the hit +- (s - 1)(r + 3 w) along the heading (the union of the end
spheres' rects): it holds the whole stretched front and stays the round box for a heading along the eye's ray.

Noise gate seed dependence: the arcs' max / min depends on the record's sequence (the seed is ring_seed_scale
unit(element_hash(seq, ring_seed_element)) x ring_seed_axis). The gate checks the fixture's fixed 0x5eed (the on / off
pairs' record) and the lab's first three hit sequences 7000-7002; the weakest, 0x5eed, gives 2.40 against the 2.0 line.
The lab test checks 7000-7003 on `shockFront` alone (each >= 2).

Shimmer ring [M], the tracked record `verification/results/native-windows/engine-shimmer/summary.json`
(`run_engine_shimmer.py --host native`, PASS 63/0): `weapon_rect_plumes_byte_identical_to_previous_program` 0 differing
pixels at 1920x1080 and 5120x1440 with a valid lane behind the plume (c1.z 0, the rings' branch uniform on it);
`WEAPON_RING`: band pixels 840 / 2,099, all moved and all outward (mean 0.97 / 1.29 px at amplitude 1.125 / 1.5), 0
changed beyond the band, on the sky or outside the grown quad; the state case's untouched constant now c80 (past the
program's c0-c75). Cap: `engine_shimmer::weapon_ring_max_screens` 0.25 of the target per ring rect (its grown box),
refused past it by `weapon_ring` and never armed by the stage. `WEAPON_RING_TIMING` (two round rings at the cap's
reach on a lane plane, EVENT-fenced): 5120x1440 ring 425 px, boxes 0.486 of the screen, 0.2454 ms; 1920x1080 0.0602 ms.
The stage charges every kept hull shimmer rect its drawn rect (the square, or the ring's rect) x `budget_weight_shimmer`
2.85 (245.4 us / 0.486 screens / 177.5 us). Shimmer program 6,489 -> 6,805 words.

Unresolved: the plume timing with no ring. The record's single-rect frames read 0.0197 ms (1920x1080) and 0.0442 ms
(5120x1440) against 0.0191 / 0.0342 ms in the earlier record from another session; the change's own cost on a plume
frame (one uniform `c1.z > 0` branch per pixel) is not separated from run-to-run spread. Settling it needs both
programs timed in one run (the fixture's previous-program slot, `set_program_words`).

Ring cost [M], EVENT-fenced best of 60, before (`55dc676bd`, `before-summary.json`) against the tracked record
(`python3 verification/results/weapon-effects-shock-front/ring_cost.py verification/results/weapon-effects-shock-front/before-summary.json
verification/results/native-windows/weapon-effects/summary.json`, output `ring_cost_out.txt`; ring only = the ring
draws alone of the same build, about 11 us of it the pass's base):

| 5120x1440 | before us | after us | ratio |
| --- | --- | --- | --- |
| heavy, fighter 200 m, 20 hits, ring only | 25.2 | 30.3 | 1.20 |
| plasma, fighter 200 m, 20 hits, ring only | 23.1 | 27.4 | 1.19 |
| missile, fighter 200 m, 20 hits, ring only | 24.9 | 29.6 | 1.19 |
| heavy / plasma / missile whole class, 20 hits | 63.3 / 93.3 / 88.4 | 66.1 / 97.7 / 95.1 | 1.04 / 1.05 / 1.08 |
| ring alone filling the frame | 71.4 | 89.6 | 1.25 |

Net of the base the 20-hit rings cost 1.3-1.4x; under the 1.5x line, so the noise stays. Ring PS 51 -> 180 slots.

Budget weights (2.11 of [weapon-look.md](../architecture/weapon-look.md), over the decal's 177.5 us at 5120x1440): the
frame-filling ring 89.6 us in the record (0.505), so `budget_weight_ring` 0.40 -> 0.50 (decision 2026-11-07); the shed
order is unchanged; the hull shimmer rects are charged from now on.

| Primitive | Weight |
| --- | --- |
| ring (shock front) | 0.50 (was 0.40) |
| hull shimmer rect | 2.85 (new) |
| filament glow | 0.86 |
| scorch | 0.49 |
| flash | 0.49 |

Host `test_weapon_effects_*` 82 OK, `test_engine_shimmer*` OK; `build-shock` 0 warnings, `check_no_x87.py --edition ap`
PASS; lab `--check` PASS, lab tests 34 OK.

## Beam colour and ion lightning (2026-11-07, dirty on `d41a60f01`)

Two look changes to the hull classes ([hull-impact-classes.md](../architecture/hull-impact-classes.md) 4.5, 4.6),
lab first, with the review's fixes folded in. Beam: where the weapon has a colour (`stage::beam_coloured`: the tint from
the light or the texture average, 8-bit saturation above `hull::beam_colour_sat_min` 0.15) the held spot's halo and
glow are white mixed `hull::beam_glow_mix` 0.6 towards the record's tint (`core::event_tint`), its core
`beam_core_mix` 0.3 (the sprite programs' flash core takes `lerp(white, halo, c.z)`), the glow at the heat glow's mean
radiance; a white or grey beam keeps the heat halo and glow; pulse flash, puff, spatter, trail ember and scorch line
unchanged. Ion: the noise filament glow replaced by lightning (`hull::ion_*`; `stage::ion_strike`, `ion_phase`):
2..4 channels of 6..10 segments with 1..2 branches each, 3 or 4 strikes over 200 ms, a residual flicker to 400 ms, no
sparks (the arc sparks dropped: less dense, less chaotic; kill switch for a web `ion_branches_lo` = `ion_branches_hi` =
0); one quad per segment through `weapon_lightning_vs.hlsl` (`renderer::WeaponLightningVertex`, 64 bytes: the contact,
the plane's axes, the segment's plane ends) and `weapon_filament_ps.hlsl` (64 PS slots, was 247), drawn on the lane under
the segment within `ion_depth_r` r of its depth. The vertex program lays the plane on the lane's normal at the contact
pixel (vertex texture fetch, `CheckDeviceFormat` `D3DUSAGE_QUERY_VERTEXTEXTURE`; the stage's origin-to-contact plane
where the lane cannot give one). Stock `SS_BULLET_IOND` is pinned ion (its beam bit `0x4` won over its on-hit `0x200`):
stock AP 12 pins (was 11), beam 8 (was 9), ion 5 (was 4); Mayhem 3 unchanged, 22 pins
(`verification/results/weapon-effects/hull_class_census_out.txt`). The lightning's cap and shed are per hit and per
channel: past 1,024 segments whole hits are cut, the oldest first (`capped` counts hits); the budget sheds whole channels
of other owners' oldest hits in one pass. The strike cache is 64 sets x 4 ways (was 32 direct-mapped slots). Device row
`weapon_effects_device` logs `filament_ps_slots`, `lightning_vs_slots` and `lightning_vtf`; the F8 row
`weapon_hull_frame` logs `ion_segments`. Lab: Layers "ion: channels", "ion: branches", "ion: corona", "beam: weapon
colour"; the page's `ionStrike` / `ionPhase` mirror the stage's, `beamColoured` the gate, `ionPlane` the vertex
program's plane on the preview ellipsoid; the ion's spark row is the stage's (zero) through the generated HEADER block.

Ion beam hold (2026-11-07, follow-up): `stage::holds` also takes any record whose style carries `shape_beam` (the bullet's
beam bits), so stock IOND (pinned ion) holds; a holding anatomy the spots do not draw (`stage::spot_anatomy`: not beam,
asteroid, repair, tug) merges each tick into its held ring record (pos, dir, damage, shield, t0 refreshed; sequence and
first stage frame kept) and the ion strike timer (`stage::ion_hold_age`, `ion_cycle_strike`) restarts its 200 ms window
every 200 ms of the hold, the 400 ms end after the last tick. Fixture gate `hull_ion_beam_holds_one_record`
(`ion_beam_hold`: 60 ion ticks with the beam shape on a fighter: 1 ring record a frame, the first tick's sequence, 0 spots,
at most 1 lightning set a frame, lit frames past 400 ms, 59 merges, 0 records 464 ms after the hold) added; the native run
(`run_weapon_effects.py --host native`, two attempts) failed at `CreateDevice` before any pass code both times (D3D 1/1
`device`, hook PASS 95/0), so the gate is not yet measured and the tracked `summary.json` now holds that failed run.

Beams by tint on the two tables (`python3 -I verification/results/weapon-effects-ion-lightning/beam_tint.py <stock table>
<mayhem table>`, tables from `tools/effects/weapon_effects_table.py --game-root /mnt/c/X3 --edition ap [--stock-only]`,
output `beam_tint_out.txt`) [M]:

| Table | Weapon | Class | Light | Texture | Source | Halo | Spot |
| --- | --- | --- | --- | --- | --- | --- | --- |
| stock AP | KH_ALPHA | beam | 128, 0, 255 | — | light | (0.53, 0.40, 1.00) | colour (violet) |
| stock AP | KH_BETA | beam | 255, 255, 255 | — | white | (1, 1, 1) | heat |
| stock AP | KH_GAMMA | beam | 255, 255, 0 | — | light | (1.00, 1.00, 0.40) | colour (yellow) |
| stock AP | IOND | pinned ion | 50, 50, 255 | — | light | (0.42, 0.42, 1.00) | — (lightning) |
| stock AP | PAL, FUSIONBEAM, TRIBEAM | beam | 192, 192, 192 | — | white | (1, 1, 1) | heat |
| stock AP | PBC | beam | 192, 192, 192 | 121, 82, 39 | texture | (1.00, 0.66, 0.46) | colour (orange) |
| stock AP | DUMMY2 (spare) | beam | 128, 0, 255 | — | light | (0.53, 0.40, 1.00) | colour |
| stock AP | REPAIR, TUG | repair, tug | | | | | their own (unchanged) |
| Mayhem 3 | PBC | beam | 255, 255, 255 | — | white | (1, 1, 1) | heat |
| Mayhem 3 | RL | repair | | | | | its own (unchanged) |

Gates [M], the tracked record `verification/results/native-windows/weapon-effects/summary.json`
(`run_weapon_effects.py --host native`: D3D PASS 187/0, hook PASS 95/0; `lightning_vtf` 1,1 on this host, an RTX 5070 Ti;
the third of four runs failed at `CreateDevice` before any pass code, environment, and its retry passed):

| Gate | Row | Result |
| --- | --- | --- |
| `hull_beam_spot_weapon_colour` | `beam_colour` | light colour (64, 255, 96): weapon hue 124.19, glow annulus (0.6..2.0 r) hue 124.19 (0.00 deg); saturation annulus 0.571, core (< 0.15 r) 0.374 (nearer white), weapon 0.951 |
| `hull_beam_ember_scorch_stay_heat` | `beam_colour` | the fall's trail ember (1 box) and scorch (1) vertices bit for bit those with `beam_colour` off |
| `hull_beam_white_keeps_heat` | `beam_colour_gate` | white (tint_white), grey 192 (tint_white) and white read as a light: sprite and glow vertices bit for bit those with `beam_colour` off (1, 1, 1) |
| `hull_beam_stock_pbc_orange_mix` | `beam_colour_gate` | texture 121, 82, 39: glow (1, 0.6605, 0.4552) against the 0.6 mix's (1, 0.6605, 0.4552), ratio error 0.0; core share 0.5 (0.3 / 0.6) |
| `hull_ion_lightning_law_16_seeds` | `ion_law` | channels 2..4 (all three), branches 1..2 per channel, segments 6..10, strikes 3..4 |
| `hull_ion_lightning_on_surface_restrikes` | `ion_lightning` | half lane: 0 sky px, 889 lane px; age 0 vs 16 ms max abs diff 0.0; strike 1 at 70 ms, Jaccard 0.0086 against strike 0; owner moved 128 px: Jaccard 1.0 |
| `hull_ion_lightning_within_filament_charge` | `ion_lightning` | one hit (28 segments) charged 0.00181 screens against the old filament box's 0.02826 (1.5 r x 0.86) |
| `hull_ion_core_floor_corona_4x` | `ion_profile` | one segment at 1440 rows: core FWHM 2.293 px (floor 2), corona extent 3.75 x the core half width, corona level 0.1865 of the peak |
| `hull_ion_no_heat_post_hit` | `ion_no_heat` | 0 glows, embers, puffs, scorches, marks; 1 flash, 1 strike; at 400 ms 0 flashes, sparks, strikes, boxes and 0 lit px |
| `hull_ion_plane_on_narrow_hull` | `ion_plane` | below |
| `hull_ion_cap_cuts_whole_hits_oldest` | `ion_cap_shed` | 42 hits, 1,638 segments: 27 newest kept (1,020 segments), 15 cut whole (`capped` 15) |
| `hull_ion_shed_whole_channels_oldest` | `ion_cap_shed` | budget 0.0291 (half of 0.0582): 509 segments shed, a run of whole channels from the oldest kept hit; 511 kept |
| `hull_cpu_64_hits_ion` | `class_cpu` | 64 lit ion hits on a capital: 23.1 us best, 23.5 mean (bound 35; 23.2 / 29.3 and 23.5 / 26.9 in the earlier runs); 25 hits drawn, 987 segments, 39 cut |
| `hull_cpu_64_hits_mixed` | `class_cpu` | 37.6 us best, 38.7 mean (37.1 / 46.7 and 38.0 / 46.4 in the earlier runs): still above 36 after the arc sparks went, so the bound is 50 (was 40) |

The plane on the narrow 1:1:6 hull (half-extents 6 b, b, b, b = 4 r, one strike; lit = pixels above 1e-3, the corona's
fringe included; channel = pixels at a third of the first strike's core radiance and above, the gated count) [M]:

| Hit | Plane | Sky px | Peak / straight | Lit / straight | Channel / straight |
| --- | --- | --- | --- | --- | --- |
| straight on (front centre) | lane normal | 0 | 1.000 | 1.000 (985 px) | 1.000 (489 px) |
| end, seen edge-on (x 0.85 x 6 b) | lane normal | 0 | 1.046 | 0.889 | 0.896 |
| near the silhouette (x 3 b, 65 deg) | lane normal | 0 | 1.181 | 0.481 | 0.544 |
| end, seen edge-on | stage plane (fault bit 5) | 0 | 1.021 | 0.660 | 0.736 |
| near the silhouette | stage plane (fault bit 5) | 0 | 1.223 | 0.230 | 0.254 |

The silhouette hit's lane plane is grazing (65 deg from the view axis), so its channels foreshorten: an offline model of
line length on the same geometry (300 random strikes, scratch only) gave 0.70 of the straight-on length at 65 deg [I];
the lit count falls further where the foreshortened channels' corona fringes overlap. No hot knot appeared with either
plane (the depth window already removed the stage plane's off-surface segments); the stage plane's loss is coverage.

Ion cost [M], EVENT-fenced best of 60, before (the record committed at `d41a60f01`, produced on `ade4de383`-dirty)
against the tracked record (`python3 verification/results/weapon-effects-ion-lightning/ion_cost.py before-summary.json
verification/results/native-windows/weapon-effects/summary.json`, output `ion_cost_out.txt`):

| Case | 1920x1080 before / after us | 5120x1440 before / after us |
| --- | --- | --- |
| ion, fighter 200 m, 20 hits (20 filament boxes -> 874 / 718 segments) | 21.4 / 34.6 | 37.3 / 35.1 |
| ion, capital close, 64 hits (charged screens 7.67 -> 3.13 / 3.38; the cap binds; GPU bound 450 at 5120x1440) | 285.2 / 129.8 | 951.6 / 370.8 |
| the program on every pixel (`filament_alone_fills_frame`) | 53.0 / 31.8 | 151.7 / 87.5 (0.49 of the decal; 87.9, the figure `budget_weight_filament`'s comment cites, and 89.4 in earlier runs) |
| beam, fighter 200 m, 20 hits | 21.5 / 21.1 | 46.1 / 42.9 |
| beam, capital close, 64 hits | 307.7 / 304.0 | 1019.9 / 1010.9 |

At 1080p the 20-hit fighter frame costs more (the per-quad and vertex share of 874 small quads; the lane-normal vertex
program, 301 slots with five lane fetches a vertex, and the 64-byte vertex took it from 29.4 to 34.6 us; at 5120x1440
34.9 then, 35.1 now); at 5120x1440 and on the close capital it costs less than the filament glow (thin segments charge
and shade a fraction of the old box). Performance-pass steps, measured on this host: the 92-byte shape vertex -> the
48-byte lightning vertex (5120 fighter 43.6 -> 32.2 us, 1080p 39.8 -> 29.5 us; CPU 44.4 -> 40.5 us); the off-screen
strike cull, the strike memo and SSE vertex stores (CPU 42.5 -> 38.8 us); then the review's two-pass emission (the
first pass counts each hit's visible segments by the contact sphere's bound, no per-point projection; the second writes
prepared rows, four stores a vertex), the 256-entry strike cache and no ion sparks (64 mixed hits 38.8 -> 37.6 us; 64
lit ion hits 23.1 us). `budget_weight_filament` 0.86 stays the lightning's bound (measured 0.50; the hook test asserts
it as an upper bound); a segment is charged at its plane length x r in pixels (the turned plane's foreshortening is not
known on the CPU: an upper bound).

Host `test_weapon_effects_*` 84 OK; `build-ion` 0 warnings, `check_no_x87.py --edition ap` PASS; lab `--check` PASS;
`generate_weapon_effects_programs.py --check` PASS (every other record changes only in this tool's digest).
