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

## Burst body, glow, flash, smoke (2026-11-07, dirty on `8a785e58e`)

[hull-burst-look.md](../architecture/hull-burst-look.md) built with the coordinator's addendum (asteroid dust, the repair
and tug cool spot) and the size check; fixture-gated, not flown. Native host (WSL2, NVIDIA 0x2c05, driver 32.0.16.2114):
`run_weapon_effects.py --host native` PASS 188 / 188; the record `verification/results/native-windows/weapon-effects/summary.json`.
`fireball_ps` 353 slots (was 270 at two octaves of 3-D noise; the native compile 1524 words), `fireball_vs` 21; the
fireball vertex 76 B (a disc float4: rotation, lobe seed, advection, octaves); sprite PS 147 slots with the star kind.

**Against the note's numbers** (each a gate that failed on the note's value, measured on the fixture and on a numpy model
of the same law over 12 hits, `verification/results/weapon-effects-burst/body_sim.py` (gates per variant: `python3 body_sim.py name:key=value,...`) and `body_search.py` (the grid), the model's lit share at 0.1 L_b
0.76 against the fixture's 0.768):

| Constant | Note | Built | Gate that failed, measured |
| --- | --- | --- | --- |
| `fireball_erode_0` / `_soft` / `heat_soft` / `advect` | 0.25 / 0.12 / 0.35 / 1.5 | 0.10 / 0.20 / 0.20 / 1.0 | erodes_outside_in (lit share 0.768 at 0.1 L_b, mean radius 0.70 at 0.8 L_b against 0.655 at 0.2); colour core T 0.5 on most hits |
| radial erosion term | erode_r q^2 | erode_r q^2 (1 + erode_rt x), rt 3.0 | the same: the rim went no earlier than the centre; the note's own "rising with age and radius" |
| `glow_r_x`, of | 1.5 R_b | 1.6 R(t) | glow_follows_burst: a fixed 1.5 R_b gives 2.8-3.1 at 0.1 L_b (the body half grown); 1.5 R(t) gave 1.18 (heavy, 0.1 L_b) |
| `cluster_n0_burst` | 3.0 | 2.0 | 20_hits_no_white_sun: 3.29 linear (bursts 1.98 of it) |
| smoke strength | s0 min(1, y / 0.27) (1 - y) | the same / (1 - 0.27) | smoke_darkens: 0.39 against 0.366 (the note's "s0 0.5 reads at 50 %" needs the peak at s0) |
| body gates 1-4 | one hit | the median of 8 hits (each its own hashes) | a single hit's coverage spreads with its seed (model: lit share at 0.5 L_b 0.0-0.8 over 12 hits) |

Every other number is the note's (frequencies 1.6 / 3.4 / 7.0, weights 0.5 / 0.3 / 0.2, erode_t 0.30, erode_r 0.20,
column_pow 0.35, lobes 5, lobe_amp 0.25, lobe_soft 0.06, rim_t 0.45, fine_px 48, R_b doubled, glow_e_x 0.33,
cluster_r_x 2.0, N0 glow 2.5, the star's arms and core, 40 ms, the smoke's sizes and timing).

**Gates** (1080p, the dump's cases; [M] run of the record):

| Gate | Bound | Measured |
| --- | --- | --- |
| hull_burst_contrast (median of 8, 0.3 L_b) | dark share >= 0.15, R/4 high-pass std / peak >= 0.10 | energy 0.245 / 0.143, heavy 0.240 / 0.143 |
| hull_burst_silhouette_lobed | max / min >= 1.25, std / mean >= 0.06 | energy 1.66 / 0.159, heavy 1.88 / 0.184 |
| hull_burst_erodes_outside_in (energy) | >= 0.8 / 0.35-0.75 / <= 0.15 / 0; radius 0.8 vs 0.2 L_b <= 0.8 | 0.858 / 0.420 / 0.046 / 0 bursts; 0.471 |
| hull_burst_colour_core_rim (0.2 L_b) | core T >= 0.75, rim <= 0.5 | 0.75 / 0.50 |
| hull_glow_follows_burst (median of 8) | radius ratio 1.3-2.0, peak ratio <= 0.4, none after L_b | energy 1.32 / 1.47, heavy 1.35 / 1.44 (0.1 / 0.3 L_b); peak 0.26-0.32; 0 |
| hull_20_hits_no_white_sun (120 ms) | peak <= 3.0; single within 1 % | 2.89 (18.61 without the rule; parts: bursts 1.46, glows 0.54, glows + embers 1.46); single 2.111 both |
| hull_flash_star_3_frames (8 classes) | 3 frames at 16 and 100 ms steps; moment ratio >= 2; <= 0.02 off the arms | 3 / 3 each; 3.16 (repair) - 5.71 (heavy); 0.0 |
| hull_smoke_darkens_hull_only | <= 0.5 (1 - 0.6 s0) = 0.366; sky unchanged; none after 1.6 L_b; residual <= 1 code | 0.346; 0 sky px; 0; 0.0 |
| hull_burst_small_has_structure (30 px) | holes >= 0.10, max / min >= 1.2, octaves 2 | 0.679, 3.13, 2 |
| hull_asteroid_dust_readable (addendum) | 10 % radius >= 1.5 r at 120 ms, peak >= 0.1 | 2.14 r, 0.177 |
| hull_repair_cool_spot (addendum) | glow 10 % radius <= 1.2 r, peak <= 0.3, no px R > B | 0.83 r, 0.300, 0 px; tug peak 0.150 |
| hull_budget_20_hits_5120_energy | <= 50 us | 29.5 us |

Existing gates rewritten for the new laws (the glow's decay on the burst's law `hull_light_peak_decay_burst_law`; the
star's half and the glow at 100 ms in `hull_sizes_counts_per_tier_law`; the star's core in the placement and 2 km floor
gates; the burst sphere x 1.25 and its eye bias in the radius, lane and anchor gates; the plasma bloom in the class arm
ratio 2.0 / 1.2; the repair's star and cool colour, the asteroid's grey sparks; the no-pop gates per element with the
cluster rule off, since a joining hit dims its cluster by design).

**Budget** (5120x1440, EVENT-fenced; over the decal's 177.5 us). The alone rows with the lobes' early-out: burst 166.7 us
at three octaves (0.94), 113.8 at two (0.64), smoke 103.0 (0.58), the star 70.5 (0.40) (the record's figures); before the
early-out (every disc pixel taking the noise) 210.2 / 182.2 / 213.5 us (a dump-only measurement of the pre-early-out
program, no case in this record; since the review the `*_alone_every_pixel_noise` cases measure it, next section). A dense close-capital frame (one owner, 22 bursts over 12.2 raster
screens at two octaves) measured 1579 us at the 0.64 weight, 0.73 per raster screen, so the bursts and smoke are charged
at the worst case: `budget_weight_fireball` 1.18, `_coarse` 1.03, `budget_weight_smoke` 1.20; `budget_weight_flash`
0.49 -> 0.40 (the star). Gated now: `hull_budget_capital_close_64_within_budget` <= 1,420 us.

| 5120x1440 frame | us |
| --- | --- |
| capital close, 64 hits, generic (energy; 22 bursts, two octaves) | 1006.2 (1579.1 at the alone-row weight 0.64) |
| capital close, 64 hits: energy / ballistic / plasma / heavy | 1000.4 / 438.3 / 1186.9 / 1152.4 |
| capital close, 64 hits: ion / beam / missile / asteroid | 1090.2 / 1054.8 / 1149.2 / 253.0 |
| capital close, 128 plasma hits | 1197.2 |
| fighter 200 m, 20 hits, generic, 96 ms in | 69.8 |
| fighter 200 m, 20 hits, energy class, 16 ms in | 29.5 |

`hull_budget_20_hits_5120` (generic energy, 96 ms in): 69.8 us in the record (70.3 at the first weights), the bound 70 -> 80 (R_b doubled,
the glow 1.6 R(t)). CPU: 64 hits on a capital best 24.6 us, mean 25.3 (the cluster count included; bound 40).

**Radii and peaks, before / after** (`measure.py`: 10 % of the element's own peak, equivalent radius px / peak; "glow"
is the dump's glow part, glows and embers together; before = `measure.txt` of `a66967eeb`):

| Case, element | 40 ms | 120 ms | 296 ms | 696 ms |
| --- | --- | --- | --- | --- |
| asteroid_single burst | 77 / 0.19 | 157 / 0.18 | 162 / 0.17 | 41 / 0.11 |
| asteroid_single glow | 0 / 0.00 | 0 / 0.00 | 0 / 0.00 | 0 / 0.00 |
| energy_fighter60_20 burst | 126 / 0.80 -> 187 / 0.69 | 174 / 1.05 -> 223 / 1.25 | 164 / 0.28 -> 182 / 0.22 | 0 / 0.00 -> 0 / 0.00 |
| energy_fighter60_20 glow | 264 / 10.36 -> 173 / 1.36 | 253 / 4.05 -> 195 / 1.56 | 170 / 0.91 -> 176 / 1.03 | 171 / 0.45 -> 171 / 0.45 |
| energy_single burst | 29 / 0.49 -> 86 / 1.39 | 58 / 0.46 -> 145 / 1.30 | 69 / 0.11 -> 94 / 0.40 | 0 / 0.00 -> 0 / 0.00 |
| energy_single glow | 200 / 1.16 -> 94 / 0.92 | 182 / 0.73 -> 176 / 0.78 | 57 / 0.40 -> 64 / 0.39 | 57 / 0.23 -> 57 / 0.15 |
| heavy_capital_64 burst | 804 / 0.40 | 812 / 0.34 | 682 / 0.12 | 0 / 0.00 |
| heavy_capital_64 glow | 0 / 0.00 | 0 / 0.00 | 0 / 0.00 | 578 / 0.81 |
| heavy_single burst | 33 / 0.54 -> 76 / 1.40 | 54 / 0.40 -> 124 / 1.32 | 57 / 0.04 -> 70 / 0.76 | 0 / 0.00 -> 0 / 0.00 |
| heavy_single glow | 199 / 1.98 -> 80 / 1.28 | 190 / 1.45 -> 136 / 1.16 | 36 / 0.69 -> 81 / 0.83 | 36 / 0.40 -> 36 / 0.50 |
| plasma_single burst | 55 / 1.44 | 137 / 1.30 | 168 / 1.10 | 0 / 0.00 |
| plasma_single glow | 75 / 1.06 | 177 / 0.98 | 222 / 0.80 | 72 / 0.40 |
| repair_single burst | 0 / 0.00 | 0 / 0.00 | 0 / 0.00 | 0 / 0.00 |
| repair_single glow | 59 / 0.87 | 54 / 0.69 | 29 / 0.38 | 29 / 0.08 |

**Size derivation** (the user, 2026-11-07: derive the size from the ships, ranges and screens). Script and output
`verification/results/weapon-effects-burst/size_derivation.py` / `.txt`. Inputs: hull lengths [M] stock rows of
`verification/results/engine-effects-census/ships.csv` (hull_len_z / 500 per metre); weapon range [M] median
`range_m` (speed x lifetime) of the stock catalogue `tools/effects/weapon_catalog/stock_ap.json` by mounting class;
projection [M] the launcher's default 58.7155 deg vertical (`docs/verification/field-of-view.md`, p11 1.778: 1280 px
per metre-over-distance at 1440 rows, 960 at 1080); engagement distance [A] fighters 0.35 of the median range (they
close to a third of it), corvettes 0.6, capitals 0.8; calibre [A] 0.33 / 0.5 / 0.8.

| Tier | ships | median length m | median range m | distance m | R_b m (energy) | R(120 ms) m | px at 1440 / 1080 rows | R_b / length |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| fighter (M3-M5) | 132 | 21.6 | 2001 | 700 | 6.77 | 5.86 | 10.7 / 8.0 | 0.31 |
| corvette (M6, M8) | 28 | 130.3 | 2200 | 1320 | 16.2 | 13.7 | 13.3 / 10.0 | 0.12 |
| capital (M7, M2, M1, TL) | 71 | 1849 | 2317 | 1854 | 55.7 | 45.5 | 31.4 / 23.6 | 0.03 |

Targets [A]: on a fighter at its median distance a burst of at least 10 px radius at 1440 rows by 120 ms, and R_b no
more than a third of the fighter's length; on a capital a few per cent of the hull. The fighter band is R_b 6.3 m
(10 px) to 7.2 m (a third of 21.6 m); the note's doubled R_b 6.77 m lies inside it (within +-30 %), so **the note's
numbers stay**; the corvette (13 px, 12 % of the hull) and capital (31 px, 3 %) rows meet the targets with them. The
other classes keep their ratios to the energy row (hull-impact-classes.md 4).

Dump (`--dump`, cases energy / heavy / plasma / asteroid / repair single, energy_fighter60_20, heavy_capital_64 at
40 / 120 / 296 / 696 ms, both sizes): 112 files, PNG under the session scratch `stage-dump-v2/`.

Reproduce:

```sh
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_weapon_effects.py --host native
X3M_WEAPON_EFFECTS_DUMP_CASES=energy_single,heavy_single,plasma_single,energy_fighter60_20,heavy_capital_64,asteroid_single,repair_single \
X3M_WEAPON_EFFECTS_DUMP_MS=40,120,296,696 X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py \
  python3 verification/probe/run_weapon_effects.py --host native --dump <dir>
python3 verification/results/weapon-effects-burst/measure.py <dir>
python3 verification/results/weapon-effects-burst/size_derivation.py
python3 verification/results/weapon-effects-burst/body_sim.py built:ert=3,et=0.3,er=0.2,adv=1.0,e0=0.1,soft=0.2,hsoft=0.2   # the note vs the built law, 12 hits
```

Host `test_weapon_effects_*` 84 OK; `build-burst` 0 warnings, `check_no_x87.py --edition ap` PASS; lab `--check`
PASS, the new scenario `hull/energy-fighter60-20hits` (167 hits per s) and Layers `flash star`, `smoke`.

## Burst review fixes and the merge (2026-11-07, dirty on `98f095045`)

The burst change's review findings fixed and `effects` (beam colour, ion lightning, `98f095045`) merged in (the branch
fast-forwarded, the burst diff re-applied and its conflicts resolved). Fixture-gated, not flown. Native host (WSL2, NVIDIA
0x2c05, driver 32.0.16.2114): `run_weapon_effects.py --host native` PASS 206 / 206, build 0 warnings; the record
`verification/results/native-windows/weapon-effects/summary.json`; the rows below by
`verification/results/weapon-effects-burst/review_rows.py` (`review_rows.txt`). `fireball_ps` 356 slots, `glow_ps` 115
(the lobes' branch), glow vertex 40 -> 48 bytes.

| Finding | Built | Gate | Measured |
| --- | --- | --- | --- |
| third octave switched at 48 px | its weight 0 -> 1 over 48-64 px of the projected R (`fireball_fine_band_px` 16, disc.w continuous, charged between the two weights) | `hull_burst_octave_blend_no_jump`: no frame's change above 3 x the median (40 -> 80 px in 10 frames); the octave's own change at most half the hard step's | max / median 1.30 (the hard step also 1.30: the growth dominates the frame difference, so the octave's own change is gated too: 86.2 against 191.0, 0.45) |
| budget step flipped 3 <-> 2 | hysteresis: two octaves past the budget, held 3 frames, three again under 0.8 of it | `hull_budget_octave_hysteresis` | 322232222233 as wanted (the old rule 323332333333) |
| cluster N a hard count | each neighbour by its remaining life fraction; `cluster_n0_burst` 2.0 -> 1.6 | `hull_cluster_neighbour_fades`: the survivor's E under 10 % a frame over its neighbour's end | 2.2 % (hard count 62.5 %); `hull_20_hits_no_white_sun` 2.73 (3.03 at 2.0, the hard count 2.22, no rule 19.5) |
| O(n^2) count per owner | a record's owner by address hash, owners out of reach skipped, a grid of the owner's reach from 128 bursts an owner (pairwise below: cheaper at 64, 41.7 against 43.1 us) | CPU rows, table below | |
| `atan2(0, 0)` | guarded in `weapon_fireball_ps` and the new glow lobes | | |
| spot puff / smoke drift reset every 1 s | one phase from the spot's birth | | |
| `tug_colour` unused | removed; repair and tug take `cool_colour` (comment fixed) | | |
| star angle shared the shimmer seed (element 9) | `star_element` 11 | `hull_flash_star_3_frames` | pass |
| ledger figures | the burst section's alone rows and the 20-hit row as recorded | | 166.7 / 113.8 / 103.0 / 70.5, 69.8 |
| weights' worst-case rows missing | `burst_alone_every_pixel_noise`, `_coarse`, `smoke_alone_every_pixel_noise` (the sphere about the eye: no pixel outside the lobes) | the weights within 0.15 of them (host test) | 212.9 / 185.3 / 213.9 us (1.20 / 1.04 / 1.21; built 1.18 / 1.03 / 1.20) |
| lab 20-hit case an open stream | the preset's `count` 20 (the hull tab's "hits then stop") | lab tests | lab peak 3.86 at 120 ms, 4.23 at 234 ms (120 ms after the newest), stage 2.73: the lab's hits within 1.5 U, the stage's over 30 engine metres, so the two still differ |
| dark annulus, heavy / plasma 40-120 ms | a ring class's glow out to the ring's drawn radius r(t) + 3 w, released as (1 - t / L_ring) | `hull_ring_class_no_dark_annulus`: the radial mean between R(t) and the ring's peak >= 0.3 of its value at R(t) | plasma 0.83 / 0.91 / 0.92, heavy 0.90 / 0.94 / 0.93 at 40 / 80 / 120 ms (without: 0.11 / 0.86 / 0.91, 0.21 / 0.23 / 0.90) |
| ember a round lamp after 600 ms | a burst class's ember takes the burst's lobes (its seed, rotation; amp 0.25), charged at `budget_weight_glow_lobed` 0.51 | `hull_ember_lobed_remnant` (median lobe std / mean >= 0.1 at 700 ms), `hull_ember_lobed_within_glow_weight` | energy 0.110, heavy 0.116; 88.6 us full screen (0.50) |

CPU, best of 200 (one owner's 1024 records best of 50), us:

| Case | built | no cluster | pairwise on every owner | burst record before | bound |
| --- | --- | --- | --- | --- | --- |
| `capital_64_mixed` | 41.6 | 38.3 | 41.7 | 39.4 (34.2 before the burst) | 50 |
| `ring_1024_visible_64` | 48.2 | 43.2 | 48.4 | 61.9 (42.5 before the burst) | 100 -> 65 |
| `ring_1024_visible_256` | 119.5 | 102.3 | 119.4 | 128.4 | 300 -> 160 |
| `ring_1024_one_owner` (report only) | 1223.2 (grid) | 625.9 | 2710.5 | | |

The first O(n) version (grid from 24 bursts, the kept owners scanned per record) measured 43.1 / 67.5: the scan over the
ring's 1024 records cost more than the pairwise count it replaced. The "before" figures are the burst record's, on the
pre-merge base (no lightning).

What the merge required: the sprite program's c.z serves both kinds (a star's width floor, a flash core's colour share);
the record flash a star for every class, the ion's in `ion_colour` for 40 ms with no other heat (its puff, glow, ember,
smoke and scorch rows zero, no sparks); the beam spot: the pulse star on the heat ramp, its core and halo in the colour
mix, its boiling puff's glow (the burst's law since the burst look) in the weapon colour at the heat glow's mean radiance;
the lightning's segment pass and its whole-channel shed at the burst's step (4) with the rings; the lab's layers, the
spot's colour and caption from both; the hull frame log both sets of fields. The run's earlier cases moved the ring's
sequence counter, so the unpinned single-record gates saw new hashes: `hull_asteroid_dust_readable` (1.47 r),
`hull_classes_distinct_greyscale` (0.15) and `hull_plasma_globe_shrinks_reddens` (colour not monotonic) failed on one
seed and now take medians over pinned records (dust 1.84 r / 0.178; small burst dark 0.37, lobes 2.24; greyscale 0.366;
globe b/r 0.69 / 0.51 / 0.45); the 20-hit records pinned. The ion's close GPU bound 450 -> 600 (497.8 us: its 64 stars
3.30 charged screens against the discs' 2.77, 370.3). The tip's engine-shimmer record was a `device` failure (the screen
saver): re-run PASS.

**The pinhole fix (2026-11-07, step 3).** The fireball program's outward drift p / |p| is singular at the disc's
centre; the death bodies showed it as a dark dot from about 0.5 L. Within 0.08 of the centre its direction now blends
to a seed-hashed fixed one (continuous at the boundary). The hull burst `energy_single` before / after (the same
fixture binary's dump, then the new program's; `verification/results/weapon-effects-burst/pinhole_compare.py <before>
<after>`, measure.py's 10 %-of-peak figures) [M]: 120 ms burst peak 1.3076 / 1.3076, lit 47,037 / 47,037 px, radius
122.4 / 122.4; 296 ms burst peak 0.7148 / 0.7144, lit 53,109 / 53,115, radius 130.0 / 130.0; glows unchanged; worst
change 0.056 % (bound 2 %). The same release also gave the program a fifth vertex input (the death body's white-hot
core gain, stride 80; 0 for every hull burst and smoke): the full-frame body's cost rose 213.8 -> 224.5 us at
5120x1440 [M] (the core's and the blend's instructions; the hull bursts pay the same few per pixel).

## Ion lightning: more and longer (2026-11-07, dirty on `8795f0ad5`)

User request: "more lightning, more lingering, and a light on the hull". `hull::ion_*` (hull-impact-classes.md 4.6):
3..6 channels a hit, 1..3 branches a channel; a strike every 120..200 ms (hashed, at least the three-frame floor) while
it starts before 0.8 x the 1,200 ms life (5..8 strikes), strikes starting at or after 200 ms x 0.6 width and x 0.5
radiance; each flash (max(40 ms, floor)) lingers as an afterglow at 0.25 of its strike for one re-strike period (the
last one's to the life's end), so a frame draws at most a flash and the previous strike's afterglow (two lightning jobs a
hit, cut together by the 1,024-segment cap); the cold light: the glow primitive in `ion_colour` (never the heat ramp),
radius 1.2 x the latest strike's extent, radiance 0.3 x e_j x (0.25 + 0.75 (1 - x)) over strike j's period; everything
x the fade over the last 0.2 of the life; record life 601 -> 1,201 ms. A held ion beam restarts its schedule every
960 ms of the hold (was 200). The strike cache entry grows to 6 channels of 3 branches (about 560 KB of .bss, was 290);
the lightning job array doubles (2,048). No program changed (`generate_weapon_effects_programs.py --check` PASS).

Native run (`X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_weapon_effects.py
--host native`; record `verification/results/native-windows/weapon-effects/summary.json`, 302 checks, 3 failures, none
ion: `death_stage_cpu_32_deaths` 58.7 us (bound 50; 38.3 in the committed record), `death_big_stage_cpu` 0.0981
us/quad (0.08) and `bolt_budget_512_bolts_20_muzzles_5120` 100.1 us (100; 96.1 committed): boom and bolt code
unchanged, every CPU row about 10..50 % slower on the host this session [M]). Rows from
`python3 verification/results/weapon-effects-ion-lightning/ion_cost.py <committed summary at da8e8bcac> <this summary>`
(`ion_more_longer_out.txt`) [M]:

| Gate / row | Case | Measured |
| --- | --- | --- |
| `hull_ion_lightning_law_16_seeds` | `ion_law` | channels 3..6, branches 1..3, segments 6..10, strikes 6..7 (16 ms floor) |
| `hull_ion_lightning_on_surface_restrikes` | `ion_lightning` | 91 segments, 0 sky px; strike 1 at its hashed start: Jaccard 0.0596 against strike 0 (its flash less strike 0's afterglow, a quarter of the peak); moved owner 1.0 |
| `hull_ion_no_heat_post_hit` | `ion_no_heat` | 1 glow (cold), 0 embers, puffs, scorches; 0 lit px past the life |
| `hull_ion_strikes_through_life`, `hull_ion_afterglow_between_strikes` | `ion_timeline` | 75 frames, 0 unlit, 7 strikes; 54 frames between flashes, 54 with the afterglow drawn |
| `hull_ion_cold_light_flickers` | `ion_timeline` | the glow on 75 of 75 frames, hue 217.5 deg, radius 1.200 x the extent, 6 rises for 7 strikes |
| `hull_ion_life_1201_fades_no_pop` | `ion_timeline` | life 1,201 ms; light over the last 0.2 non-increasing (0 rises), last lit frame 0.0169 of the first |
| `hull_ion_beam_holds_one_record` | `ion_beam_hold` | 100 ticks: 1 record, 0 spots, 1 lit hit a frame, 24 lit frames past 1,200 ms, 99 merges, 0 records after |
| `hull_compose_no_pop_per_class` | ion | 40 frames, 320 checks, 0 violations, released, life 1,201 |
| `hull_cpu_64_hits_ion` | `capital_64_ion` | 35.8 best, 59.9 mean (was 23.5 / 26.9; 15 hits drawn, 49 cut by the cap); bound 35 -> 42.6 (35.5 best in the first run + 20 %) |
| `hull_budget_20_hits_5120_ion` | fighter 20 hits | 49.5 us at 5120x1440 (was 40.2), 40.0 at 1080p; bound 45 -> 54.5 (45.4 in the first run + 20 %), then 67.7 with the cap at 2,048 (below) |
| capital close 64 ion (record test) | 5120x1440 | 1,104.7 us (was 497.4), 7.993 charged screens (was 3.911): the cold light's glows fill the 8-screen budget; `ION_CLOSE_GPU_BOUND_US` 600 -> 1,325.6 (+ 20 %); the class gate (frame within 8 x 177.5 us) holds |
| `hull_cpu_64_hits_mixed` | `capital_64_mixed` | 45.0 best (41.0 committed; bound 50) |

Lab: `ionPhase` / `ionStrike` / the cold light mirror the stage; `test_weapon_effects_lab` gates the same timeline on
both ion Scenarios (at least 4 flashes, the afterglow on every gap frame, the glow every frame in the ion hue, the fade
non-increasing to at most 0.1, nothing past the life). Stage dump (`--dump`, `ion_single`, 1080p, 40 / 200 / 504 / 904 /
1144 ms; crop and stats `python3 -I verification/results/weapon-effects-ion-lightning/ion_dump_crop.py <dump> <out>`):
lit px 24,726 / 47,833 / 23,581 / 24,672 / 1,680; glow peak 0.190 / 0.169 / 0.098 / 0.053 / 0.007 at radius 108..153 px,
colour ratio (0.60, 0.75, 1) throughout. Judged: the channels crawl and re-strike (a new set over the last one's
afterglow at 200 ms), the later strikes visibly thinner, a faint set still at 1,144 ms, a soft cold glow under the
channels with no lamp core. AP DLL `build-ion`: 0 warnings, `check_no_x87.py --edition ap` PASS.

Segment cap and rerun (the same day, coordinator follow-up). The cap is `renderer::weapon_filament_boxes` (no
`hull::ion_segment_cap` exists): 1,024 -> 2,048 segments a frame (the production vertex array and the fixture's sized
with it, 2,048 x 4 x 64 B = 512 KB each; the index list is the sprite batch's 2,688 quads; the lightning job array
already holds 2,048 jobs, two per hit). Whole hits are still cut, oldest first. The game was absent for the whole wait
(`tasklist.exe`), but the Windows CPU stayed at 13..30 % for about 18 minutes in 60 s steps (`typeperf`: another agent's
`weapon_effects_fixture` and VS Code); the one rerun then ran (302 checks, 2 failures) [M]:

| Row | Before the cap | Cap 2,048 | Bound |
| --- | --- | --- | --- |
| `capital_64_ion` CPU | 15 hits drawn, 49 cut, 35.8 us best | 29 drawn, 35 cut, 1,996 segments; 38.8 best, 51.3 mean | 42.6 -> 46.6 (+ 20 %) |
| ion fighter 20 hits, 5120x1440 GPU | 1,003 segments, 49.5 us | 1,501 segments, 56.4 us (FAIL against 54.5) | 54.5 -> 67.7 (+ 20 %) |
| ion capital close 64, 5120x1440 GPU | 1,104.7 us | 1,111.8 us | 1,325.6 (record test) |
| `ion_cap_shed` | 13 kept, 29 cut | 29 kept, 13 cut of 42; shed whole channels | gate PASS |

Rows this change does not touch, against the committed record (`untouched_rows.py`, `untouched_rows_out.txt`) [M]:
explosion CPU 38.3 -> 44.4 us (x1.16, bound 50: PASS), big-tier explosion 0.0673 -> 0.0928 us/quad (x1.38, bound 0.08:
FAIL), 512 bolts GPU 96.1 -> 100.0 us (x1.04, bound 100: PASS). The explosion rows are more than 10 % slower on unchanged
code; their bounds (50, 0.08, bolts 100) stay at their committed values. The two new ion bounds were set after the run:
the record's source hash of `weapon_effects_class_cases.h` is stale until the next native run.

Quiet-host rerun after merging `effects` `0093a702c` (the fireball drift law; `weapon_effects_core.h` merged without
conflict): AP `build-ion` 0 warnings, `check_no_x87.py --edition ap` PASS; native run 304 checks, 0 failures, hook
147/147; `test_weapon_effects_*` 99 tests OK [M]. The untouched rows against the committed record at `0093a702c`
(`untouched_rows_out.txt`): explosion CPU 38.1 -> 38.3 us (x1.005), big-tier 0.067 -> 0.068 us/quad (x1.015), 512 bolts
GPU 96.1 -> 96.4 us (x1.003), bolt CPU parity 4.0 -> 4.4 us (x1.10): the earlier slowdown was the busy host. Ion rows on the
quiet host: `capital_64_ion` 38.8 best, 40.5 mean (29 drawn, 35 cut; bound 46.6), fighter 20 hits 60.8 us at 5120x1440
(bound 67.7) and 48.8 at 1080p, capital close 64 1,105.9 us (bound 1,325.6), `capital_64_mixed` 44.2 (bound 50).

## Bolts and muzzle (step 4)

Design `docs/architecture/bolt-muzzle-look.md` (ratified 2026-11-07). Base: `effects` at `98f095045` (reset into this
worktree). The per-bullet tap, the muzzle site and the flight-effect skip, the mode `shields,hull,bolts`, the header
constants, the capsule programs and the stage/draw contract. The hull-burst merge runs in a separate worktree; the
bolt and muzzle code is in new files (`src/proxy/weapon_effects_bolt.h`, `weapon_effects_bolt_sites.h`,
`src/effects/weapon_bolt_{vs,ps}.hlsl`, `verification/probe/weapon_effects_bolt_cases.h`) and the edits to shared files
are small and additive.

**Hook sites (weapon_effects_bolt_sites.h, bytes read from the installed `C:\X3\X3AP.exe`, SHA fdbf3418…).** The
per-bullet tap at `0x0045ac7d` → `0x00414590` (cdecl, one arg EBX=obj; the five bytes straddle the qword `0x0045ac80`,
plain write in the install window), the muzzle at `0x00446268` → `0x00413c00` (cdecl 3; EAX the bullet index, EDX
`&orientation`, both forwarded; atomic), the flight effect at `0x004146c0` → `0x004148a0` (cdecl 10, the col-19 sprite;
atomic). Install order {muzzle, flight, bolt_tap, impact} after the atomic shield/hull taps, all-or-none with the rest;
rollback restores every site; refusal before any write on a changed window; FL compiles the hook out.

**The handler (weapon_effects_bolt.h) stays 0 calls / 0 x87.** `build_weapon_effects_hook.py`'s audit over
`weapon_effects_hook.o`: `_x3m_weapon_bolt_tap` 486, `_x3m_weapon_muzzle` 190, `_x3m_weapon_flight` 40 instructions,
each with **0 call** and **0 x87/MMX**, no undefined symbol; the three new stubs match their expected opcode shapes
(the muzzle stub passes EAX/EDX with `lea eax,[esp+0x18]`). The tap keeps a 512-entry per-frame list double-buffered by a
generation Present advances (`present_bolts`); a bullet seen twice in a frame is refreshed in place (a 1024-slot keyed
table); past 512 the farthest non-own record is replaced. The muzzle keeps one record per (ship, slot), re-lit per shot
(t0 and seed reset, `prev_t0` kept, shots advanced); 128 records. The muzzle and flight skip (return 1) only while the
in-place draw owned the previous frame and the suppress flag is set.

**Hook fixture gates (native host, `run_weapon_effects.py --host native`; hook-summary.json).** 109 checks, 0 failures.
The step-4 rows: `bolts_installed`, `bolt_sites_redirected`, `bolt_write_paths` (bolt_tap plain, muzzle+flight atomic),
`bolt_tap_record_per_live_bullet` (recorded once per pass, refreshed on a second pass, a second bullet a second record,
the own ship's bolt marked, EBP/EDI and the stack as the call left), `bolt_tap_generation_stamped_per_present`,
`bolt_tap_non_bullet_forwards`, `muzzle_stub_forwards_eax_edx` (EAX=7 the index, EDX=&orient, ship/slot/&pos reach the
callee, EBX/EDI/EBP kept, stack balanced), `muzzle_record_per_shot_per_slot`, `muzzle_skips_native_when_staged` (callee
never entered, frame kept), `flight_skip_when_staged_else_forward`, `bolt_shutdown_restores_all`,
`bolt_tap_byte_mismatch_refused`, `muzzle_byte_mismatch_refused`, `bolt_rollback_restores_all`. The parity basis: the
generation stamp makes the in-place draw read exactly one frame's list; the draw matches tap records to the engine's
draw instances by subtype and nearest position (`weapon_bolts_frame parity_mismatch`), the step's open gate.

**Capsule programs.** `weapon_bolt_vs` 97 words, `weapon_bolt_ps` 2654 words (2699 and the flag variant 2722 after the
review fixes below), all compiled on the game's native
`d3dx9_37.dll` D3DXCompileShader (`generate_weapon_effects_programs.py`, records
`verification/results/weapon-bolt-{vertex,pixel}-program.json`). The VS is a pass-through (the stage lays the
velocity-stretch quad of 5.2 on the CPU and hands each corner in clip space), the PS the
distance-field capsule (core/body/halo/tail/ball head, per-end widths, the streak energy `g_s`, the floors, the far
flag oC2 = 32·max(rgb)). The muzzle draws through the existing stage sprite batch (note 3.1). `MaxPixelShader30Instruction
Slots` is logged at device creation as today; no program is refused or split for exceeding 512.

**Table.** `mount_tier` and `range_m` added to the generated weapon table and `core::Table` (the stub reads nothing new;
the stage reads them for the width law). The header constants of the design's section 8 are in `weapon_effects_core.h`
`namespace bolt` / `namespace muzzle`.

**Build / checks.** `build-bolt` (ap) and `build-bolt-fl` (fl) 0 warnings; `check_no_x87.py --edition ap` PASS (1162
reachable functions, no violations); `schema.py --check` and `generate.py --check` PASS; `tools/config` regenerated
`config_schema_inc.h` and `assets/x3m.ini` for the `shields,hull,bolts` choice; host `test_weapon_effects_*` 91 OK; lab
`--check` PASS.

### The in-place draw, the gates and the lab (2026-11-07, dirty on `a7da14fdc`)

**Table (generator schema 2).** `tools/effects/weapon_effects_table.py` writes `mount_tier` (the smallest mounting class
from TLaser / TShips / TCockpits, `weapon_catalog.py`'s used-by rule: any M5/M4/M3 fighter, only M1/M2/M7/TL capital, else
corvette, unmounted fighter) and `range_m` (lifetime x speed) per bullet row; SCHEMA 2, the DLL reads 1 and 2. Regenerated
from `C:\X3` (Mayhem, 38 rows: fighter 18, corvette 14, capital 6) into the scratchpad: 0 of 38 rows differ from the
tracked catalogue (`verification/results/bolt-muzzle-look/table_mount_check.py`); over both AP catalogues the rule gives
the note's 30 / 19 / 12 (`test_mount_tiers_match_the_catalogue`).

**The stage (`src/proxy/weapon_effects_bolt_stage.h`).** The row's look from the live TBullets record and the table (the
classifier's tint rule, the style, k, the mount tier and range, the muzzle class; a table class pin sets the muzzle class
too: the Mayhem MD carries no 0x40 and is pinned ballistic), cached per row per frame; section 1's width law; 2.2's
lengths; the speed from range / (expiry - creation); `build_bolts` lays 5.2 on the CPU: the world segment min(L + T,
v age) back along the velocity's direction (birth never longer than flown), the eye-plane clip of the back end (a head
behind z_near + 0.5 m culled), both ends projected through the frame's jittered projection, per-end widths and sigmas with
the floors, the own ceiling 16 x rows / 1440 and the halo cap 40 x rows / 1440, the length floor 24 x rows / 1440 x c x
the flown share, the streak to the previous head (its record keyed by object and creation in a 1024-slot index of the
previous list, projected through the previous view) capped at (1 + c_s) L (an orb: its edge + c_s W), g_s = rest /
(rest + e / 3), the end-on disc, the oriented quad around the axis and the streak with R = w/2 + 3 sigma, the CPU frustum
cull, the corners' depth by 1 / z extrapolated along the axis (clamped to the eye plane), clip z = m22 z + m32 (the
scene's depth law). Quads leave in kind order, rim-eligible (band 0.6 w >= 3 px) first. `bolt_parity`, `bolt_draw_action`
and `build_muzzles` (disc without the hull flash's eye bias, cone / star arms / vents as a new sprite kind 4 (the burst
took kind 3 for its star), the plasma bloom on the burst's fireball law, the hull glow with s(N), the smoke puff as a
scorch-state multiply, the cockpit gain and no puff in the cockpit, the three-frame floor, re-light by reset, breathing).

**Deviations the gates forced (each a size floor or a measurement-consistent law; the note's figures stand otherwise).**
(1) The end-on footprint: a 3 px body with a halo of sigma 1.5 px at E_h 0.6 never reaches 10 % of a 6.6 peak outside
the body, so 2.4's "lit diameter >= 7 px" is unreachable; `bolt::floor_endon_px` 9 x rows / 1440 raises an end-on line's
body (keyed on the travel's foreshortening and the projected axis, linear to the side-on floor). (2) The edge tint t_c is
(t - min t) / (1 - min t) (same hue, full saturation) instead of t^2 / max t^2, which turned an orange's hue by 10 degrees
(`bolt_colour_core_rim`). (3) The muzzle's breathing amplitude is breath x saturate(dt / 3 steps - 1): the literal
saturate(dt / 3 steps) left 9 % frame to frame at SETA 6, 10 Hz against the gate's 2 %. (4) The core is shaded from its own
segment ending half a body width short of L (its round cap ends at L) and the tail separately, so an end-on bolt keeps its
whole disc and halo (the first law cut it along the tail's end). Widths are measured as the phase-averaged count of pixels
the body term lights (halo isolated off), the 10 %-of-core level of a discrete 3 px profile reading 3.4-3.65 px.

**The in-place draw.** `WeaponEffectsPass::draw_bolts` (attach with `bolts`: the programs, the MRT variant
`weapon_bolt_flag_ps`, a DYNAMIC | WRITEONLY DEFAULT-pool VB of 544 quads x 4 x 128 B and a static index buffer, released
before Reset and recreated by the first draw after it): every state it sets read first through the documented Get* calls
(programs, declaration, stream 0 and its frequency, refused unless 1, indices, PS c0..c3, 19 render states), one DISCARD
lock, the rim pass (ZERO / INVSRCCOLOR, COLORWRITEENABLE1 / 2 = 0) over the rim-eligible prefixes, the capsule pass (ONE /
ONE, LESSEQUAL, Z write off, CULL NONE, the caller's RT1 / RT2 masks) per kind, everything restored in reverse.
`motion_output` (`weapon_bolts_in_place`, AP only): in `prepare_screen_additive`, after its admission checks and the
single-copy drop, a bullet-producer draw is decided by `bolt_draw_action` (armed: the route, the HDR / TAA path, the scene
camera, the pass's bolt objects, the hooks; the frame's first late copy draws ours, any other is suppressed while ours
own the bullets, a frame without a tap list or unarmed draws natively); ours binds the far flag's targets exactly as the
route does (`bind_bolt_flag_targets`), publishes the own ship's object and the eye to the hooks, applies the parity rule
when the locked-prefix scan holds the draw's vertices, builds and draws; a failed draw forwards the game's copy and disarms
64 frames (the third consecutive: until Reset); a failed restore drops the shadow. The muzzles join the hull stage's
sprite / glow / fireball / scorch batches in `run_weapon_effects`. Also fixed: the stage's three `mode ==
mode_shields_hull` tests (hull_mode()), so the hull family stays on in `shields,hull,bolts`.

**D3D fixture (`verification/probe/weapon_effects_bolt_draw_cases.h`, native host, RTX 5070 Ti).** 48 step-4 checks, all
PASS: every section-7 row (width / length law at 1080 and 1440, radial profile, rim darkens (band min 0.69 x the hull) and
survives the resolve, halo cap, orb breathing, wave crescent, floors, own ceiling, style shapes, colour, tail law, streak
covers the previous position at 4 / 8 / L px along and across, SETA cap and energy (summed radiance within 10 %, SETA 10
streak 2 L), end-on disc and near-plane clip, birth and death, stream without strobe (CV <= 0.10 at 1x / SETA 6 / 10, the
trim rule 1.2 -> 0.80), no scar after 8 frames, two draws with every state restored and RT2.g > 1 on bolt pixels only,
over-bright hull 7.5 : 1 after the resolve, the convergent barrage peak 6.6 (<= 8), resolve survival >= 0.95 at 0 / 4 / 8 px, the
VB equals the culled list, the hard end at a hull's depth, the parity rule (equal counts keep all and learn each row's body,
three engine-culled instances drop three records, a body mismatch drops its record)), the muzzle rows (size per class,
life floor, re-light peak = single, breathing, broadside summed glow <= 2.3, smoke surface only, heavy rise and vents,
cockpit gain, placement on a moved and rotated ship, axis, light on hull not sky), the budget, Reset recovery, the lock
and capsule-pass faults (restored) and the program refusal (hull and shields kept). Budget at 5120x1440 (the note's case:
32 near fighter bolts at 150-200 m, 480 floored far ones, 20 muzzles; quads 1.31 Mpx = 0.18 screens): GPU by D3D9
timestamps with the GPU kept loaded 43.9 us (bolts 41.8, muzzles 2.0; bound 60); the fenced wall clock, which adds the
draw call's CPU and the idle-clock fence wake (15 us for one far bolt), reads 105-125 us and is reported, not gated; CPU:
build 37.6 us + draw call (Gets, lock, 256 KB write) 10.0 us = 47.6 us (bound 150); bolt_alone_fills_frame 115 us by
timestamps (weight 0.65 of the decal's 177.5). Programs: bolt VS 8 slots, PS 673 / flag PS 679 slots (no refusal for the
count; device max 4096 logged).

**Pixel sizes, the three proving scenarios** (`verification/results/bolt-muzzle-look/sizes.py` over the fixture's
BOLT_SIZE rows, output `sizes_out.md`; 5120x1440 shown, 1080 rows are x 0.75 as the note says; each bolt alone at 10 % of
its own peak; the law in brackets):

| scenario | screen | t ms | dist m | body px (law: w f / d, floors, ceiling) | lit width px (10 %) | core px (law: L f / d) | lit length px (10 %) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| own_fire_chase | 5120x1440 | 50 | 85 | 16.00 (16.00, c 0.11) | 15.85 | 28.28 (28.28) | 44.40 |
| own_fire_chase | 5120x1440 | 300 | 85 | 16.00 (16.00, c 0.11) | 15.85 | 28.28 (28.28) | 44.40 |
| own_fire_chase | 5120x1440 | 750 | 27 | 16.00 (16.00, c 0.00) | 16.05 | 0.00 (0.00) | 15.90 |
| own_fire_chase | 5120x1440 | 1500 | 27 | 16.00 (16.00, c 0.00) | 16.05 | 0.00 (0.00) | 15.90 |
| incoming_cockpit | 5120x1440 | 0 | 600 | 8.93 (3.87, c 0.00) | 8.50 | 0.04 (0.05) | 8.35 |
| incoming_cockpit | 5120x1440 | 200 | 300 | 8.78 (7.74, c 0.00) | 8.90 | 0.13 (0.10) | 9.00 |
| incoming_cockpit | 5120x1440 | 333 | 100 | 23.07 (23.23, c 0.00) | 22.25 | 0.91 (0.94) | 23.00 |
| incoming_cockpit | 5120x1440 | 367 | 50 | 46.84 (46.46, c 0.00) | 44.85 | 2.79 (2.50) | 44.75 |
| stream_station_3km_1x | 5120x1440 | 0 | 3000 | 3.00 (3.00, c 0.00) | 3.80 | 0.00 (0.00) | 3.80 |
| stream_station_3km_1x | 5120x1440 | 48 | 3000 | 3.00 (3.00) | 3.80 | 30.72 (33.50) | 31.85 |
| stream_station_3km_1x | 5120x1440 | 96 | 3000 | 3.00 (3.00) | 3.80 | 33.52 (33.50) | 38.60 |
| stream_station_3km_1x | 5120x1440 | 144 | 3000 | 3.00 (3.00) | 3.80 | 28.16 (33.50) | 28.90 |
| stream_station_3km_seta6 | 5120x1440 | 0 | 3000 | 3.00 (3.00, c 0.00) | 3.80 | 0.00 (0.00) | 3.80 |
| stream_station_3km_seta6 | 5120x1440 | 48 | 3000 | 3.00 (3.00) | 3.80 | 33.52 (33.50) | 38.65 |
| stream_station_3km_seta6 | 5120x1440 | 96 | 3000 | 3.00 (3.00) | 3.80 | 33.52 (33.50) | 38.50 |
| stream_station_3km_seta6 | 5120x1440 | 144 | 3000 | 3.00 (3.00) | 3.80 | 33.52 (33.50) | 37.85 |

| scenario | screen | t ms | muzzle disc px (target) | cone px | glows |
| --- | --- | --- | --- | --- | --- |
| own_fire_chase | 1920x1080 | 50 | 30.20 (>= 40 at 1440) | 20.27 | 1 |
| own_fire_chase | 1920x1080 | 300 | 30.20 (>= 40 at 1440) | 20.27 | 1 |
| own_fire_chase | 1920x1080 | 750 | 30.07 (>= 40 at 1440) | 20.27 | 1 |
| own_fire_chase | 1920x1080 | 1500 | 30.07 (>= 40 at 1440) | 20.27 | 1 |
| incoming_cockpit | 1920x1080 | 0 | 2.99 (floor 3) | 1.88 | 1 |
| own_fire_chase | 5120x1440 | 50 | 40.26 (>= 40 at 1440) | 27.02 | 1 |
| own_fire_chase | 5120x1440 | 300 | 40.26 (>= 40 at 1440) | 27.02 | 1 |
| own_fire_chase | 5120x1440 | 750 | 40.10 (>= 40 at 1440) | 27.02 | 1 |
| own_fire_chase | 5120x1440 | 1500 | 40.10 (>= 40 at 1440) | 27.02 | 1 |
| incoming_cockpit | 5120x1440 | 0 | 2.99 (floor 3) | 1.88 | 1 |

Own fire: the newborn bolt at the guns is the 16 px own ceiling, the nearest flown one (85 m, c 0.11 in the chase view)
16 px by 28 px core; the muzzle disc 40.3 px at 1440 rows (target >= 40). Incoming: the end-on floor (9 px) holds the
disc at 600 and 300 m (law 3.9 / 7.7 px), growing with the law to 23 px at 100 m and 47 px at 50 m. The stream at 3 km:
the 3 px body floor, core 33.5 px (law 33.5), 1x and SETA 6 alike (a newborn bolt shorter by the birth law).

**Dumps.** `X3M_WEAPON_EFFECTS_DUMP` writes the three scenarios (the stream at 1x and SETA 6) at 1920x1080 and 5120x1440,
four times each, through x / (1 + x) for viewing (the 1.0 hull at 0.5; the fixture has no AgX); converted with
`verification/results/weapon-effects-burst/ppm2png.py`, the grid by `verification/results/bolt-muzzle-look/crop_grid.py`
(600x360 centre crops). Seen: the incoming bolt a dot growing to a disc with its halo, own fire leaving the guns under the
disc, the stream as separate dashes over the lit hull at 1x and SETA 6.

**Lab.** The Bolt and Muzzle tabs (`tools/effects/weapon_effects_lab.html`): the stage's law and the pixel program's
profile in canvas pixels (the centre 720x405 of the emulated screen at 1:1, 1440 / 1080 rows, the vanilla or remapped
FOV), layers core / body / halo / tail / head / rim / streak and disc / cone-vents / star / bloom / puff / hull glow,
HEADER.bolt and HEADER.muzzle injected by name, the weapons' mount tier and range from the catalogue, 17 Bolt and 9 Muzzle
Scenarios (weapon-look 3.3's list, section 6's additions, the three proving ones). Lab tests 40 OK (new: the tabs, layers
and presets; the width law at d_eng 3 px, the own ceiling 16 px, the end-on floor, PPC 4.44 px at 4 km; the broadside's
light counting: 12 lights, summed <= 2.3 single, the cockpit gain 0.35); `--check` PASS.

**Acceptance (native host, `run_weapon_effects.py --host native`, after the merge).** D3D fixture 252 checks, 0
failures, 0 build warnings (`summary.json`); hook fixture 109 checks, 0 failures (`hook-summary.json`); `build-bolt` (ap)
and `build-bolt-fl` (fl) 0 warnings; `check_no_x87.py --edition ap` PASS (1180 reachable functions); `schema.py --check`,
`generate_weapon_effects_programs.py --check` and the lab's `--check` PASS; host `test_weapon_effects_*` OK.

**Merge.** `effects` moved to `a7da14fdc` (the burst rebuild and the run242 install); fast-forwarded with this step's work
re-applied: the burst's star took sprite kind 3, so the muzzle cone is kind 4 (both branches kept); `fireball_quad` gained
the burst's disc term (the plasma bloom passes rotation, lobe seed, advection and two octaves); the ledger keeps the burst
sections before this one; the lab's header lists, layer keys and preset keys (the burst's `count`) merged; the program
records, the sprite headers and the lab page regenerated; the summaries re-recorded by the acceptance run.

### Review fixes (2026-11-07, dirty on `a7da14fdc`)

Findings of the step-4 review, each by its number; figures [M] measured on this host (RTX 5070 Ti, native), [I] inferred.

1. **Previous-position stamp.** The index of the previous list was stamped `frame_ | 1`, equal on frames 2k and 2k + 1,
   so odd frames read the even frame's slots (the reviewer's model `verification/results/bolt-muzzle-look/prev_index_stamp.py`:
   probes 5 -> 32 / 45 on odd frames). The stamp is now the hooks' Present generation (`BoltHookState::generation`, one per
   frame), the look cache's too; a second build with the same stamp keeps the index. Host test
   `test_weapon_effects_core.WeaponBoltPrevIndex`: `build_bolts` on four consecutive frames of 400 bolts (8 new, 8
   expired a frame): 0 wrong / 0 lost of 1600 with the generation, 752 wrong + 16 lost with `frame | 1` (the negative
   control) [M].
2. **Muzzle ownership.** The muzzle has its own latch (`BoltHookState::muzzle_owned`, `stage::muzzle_latch`): the bolts
   armed this frame, the stage built and drew this frame's muzzles, no element short of room. The hook skips the native
   muzzle only under it and marks the shot ours (`MuzzleRecord::ours`); the stage builds only ours (`MuzzleCounts::native`
   counts the rest). The muzzles go first into the shared batches (at most 128 x 7 sprites, 128 glows / fireballs /
   puffs: always room), the hull hit's elements after them. A refused bolt draw disarms (the latch drops at that
   Present). Gates: hook `muzzle_skips_native_when_staged` (ours = 1), `muzzle_forwards_and_not_ours_without_latch`
   (latch 0 with the bolt latch 1: forwarded, ours = 0; Present latches from the BoltFrame); D3D `muzzle_ownership_exact`
   (a native shot builds nothing; a full sprite batch counts 2 starved and the latch drops; unarmed or no stage draw:
   no latch).
3. **The frame's ownership** (`stage::bolt_take`): decided at the frame's first bullet-producer draw, before anything is
   suppressed, in the per-draw path ahead of every preparation (the additive route's refusals no longer decide a bullet
   draw: a refused late copy after ours is suppressed, not drawn over ours). Ours takes the frame when armed, listed and
   a late copy is expected (this draw is late, or the previous frame had one); otherwise every copy is native and nothing
   is suppressed. A late copy that cannot take ours (no open scene, a state-block recording, a query) or a failed draw
   leaves the rest of the frame native. `weapon_bolts_armed` is evaluated at least once a frame (Present), cached per
   frame, its row logged on every change of the reason and the reason carried in every `weapon_bolts_frame` row (with
   `armed`, `taken`, `lost_early`, `muzzle_owned`, `muzzle_native`, `muzzle_endon`, `muzzle_starved`); the new reason
   `screen_emission_additive`. **Needs: screen_emission_additive = 1** (the only route that sees the game's bullet
   draws; without it every bolt and muzzle stays the game's; the `weapon_effects` comment in `x3m.ini` says so). Gate
   `bolt_frame_ownership` (nine frame sequences).
4. **Parity.** The tolerance is `parity_tol_px` 6 + the instance's own on-screen radius (its body's half diagonal), the
   records matched among at most 48 instances of the x-sorted list (heap sort, binary search), the row periods learned
   from the first 32 records of every frame with an unambiguous match; the scan's instances now carry their radius.
   Gates `bolt_parity_rule` (learns with unequal counts; a 20 px head-to-centroid offset kept with a 20 px body, dropped
   with a 1 px one) and `bolt_parity_cpu_512_256`: 512 records against 256 instances, median 3.9 us in the record run
   (4.9-8.0 in two development runs), bound 60 [M].
5. **Tap overflow O(1).** Past 512 each overflowing call examines the next 8 records from a ring cursor and keeps the
   farthest non-own one as the candidate (no rescan); a replaced record's key is a tombstone the next insertion reuses;
   key probes stop at 64. Hook gate `bolt_tap_overflow_1024_o1` (1024 live bullets, 8 own, two passes through the
   production handler): 512 listed, all 8 own, no duplicate, the listed mean distance 32.3 km against 64.1 km for all,
   median 47.7 us for both passes (worst of 21: 300.6), bound 150 [M]. `_x3m_weapon_bolt_tap` 507 instructions, `_x3m_weapon_muzzle` 224,
   0 calls, 0 x87 (the audit).
6. **Rim extent.** The quad reaches max(0.5 w + 3 sigma, 1.8 w + 1 px) where the rim draws (an orb: its lobed radius),
   and the line kind's shading reach in the rim pass the band's outer edge. Gate `bolt_rim_whole_on_wide_bolt`: the
   Mayhem PPC at 100 m (133 px), quad 481.5 px across (want 479.5), the band darkening a 1.0 hull to 0.800 at 1.7 w [M].
7. **Budget.** Gated on the fenced wall clock as the hull rows are: the in-place draw and the muzzle batches each fenced,
   summed, bound 100 us (`bolt_budget_512_bolts_20_muzzles_5120`): 89.7 us (bolts 70.3, muzzles 19.4; 89.8 before the
   fixes); by timestamps 44.6 us, reported [M]. The note's 60 us came from a 90-slot estimate while the pixel program has
   673 slots (684, the flag variant 690, after the reach change) [M]. The rim's wider reach costs where a bolt fills the
   frame: `bolt_alone_fills_frame` 175.3 us fenced / 160.0 by timestamps (weight 0.99 / 0.90 of the decal's 177.5;
   130.6 / 115.4 before) [M]; the 512-bolt frame is unchanged (32 rim bolts).
8. **Stale text.** The capsule programs' figures above (VS 97 words, a pass-through; PS 2654), 91 OK; the two "not wired"
   comments (`motion_output.h`, `motion_output_weapon_effects_inc.h`) and the one in `capture.cpp` replaced.
9. **Shooter classes** (weapon-look unknown 13): the muzzle hook counts its shooters' object class (ship 7, dock 5,
   factory 6, other with the last other class), in `weapon_bolts_frame` (`shooter_*`); hook gate
   `muzzle_shooter_class_counted`.
10. **A new ship at a reused object.** A changed node handle starts the record over (prev_t0 0, one shot): hook gate
   `muzzle_record_restarts_on_new_handle`; `muzzle_restarted` in the row.
11. **Star arms.** The truncated series on [-pi, pi] (4.4 degrees / 2.2 % off at its ends) is replaced by the quarter
   turn reduced into `sincos_quarter` (1e-8) and exact quarter turns per arm.
12. **The 9 px end-on floor stays** (incoming fire must be visible: the Halo rule of the references). The flight's
   question: does an incoming bolt at 300-600 m read as a dot of the right size, neither lost nor a blob?
13. Folded into 3: a frame whose expected late copy never comes loses its early copies for that frame (`lost_early`);
   the next frame takes nothing until a late copy is seen (gate frames 4 and 5).
14. **A capsule failure after the rim** divides the rim back out: the same rim ranges with the program's undo output s / (1 -
   s) under DESTCOLOR / ONE (`D3DPBLENDCAPS_DESTCOLOR`, `caps().bolt_rim_undo`; never on a lost device). The fault bit 7 now
   refuses the capsule draw instead of failing after it. Gate `bolt_rim_undone_when_capsules_fail`: worst 0.10 % over a
   1.0 hull and 0.08 % over an HDR 2.5 one (the whole draw darkens them to 0.600 / 1.500) [M].
15. **Another ship's back end near the eye** is never wider than its head or 4 x the own ceiling, whichever is wider
   (`back_w_own_x`; `capped_back` in `weapon_bolts_draw`). Gate `bolt_back_end_capped_near_eye`: head 30 m ahead, back end
   clipped to the eye plane, 48.00 px at 1080 rows (the cap), the own bolt 12 px; the wide PPC's ends untouched [M].
16. **Table fallback rows.** The generator marks each bullet row no ship class mounts (`mount_fallback`, the fighter tier,
   unknown 7), counts them and prints one stderr line per row.

**The muzzle's view rule** (bolt-muzzle-look.md 3.2): a gun axis within 30 degrees of the eye ray (`endon_cos`) takes the
hull burst's four-arm star (sprite kind 3, `star_arm_len`, A = the cone's length, `star_e`) about the disc for the cone
(energy and heavy; ballistic and flak keep their own star). The star takes the cone's life, not the burst star's 40 ms:
at 16 ms frames a 40 ms star is gone before the gate's 50 ms (its three-frame floor is 48 ms). Gate
`muzzle_endon_star_side_cone`: the own fire chase frame at 50 ms (the gun axis 11 degrees off the eye ray) has one star,
no cone, second-moment ratio 2.13 at 10 % of its peak (1.00 with the cone); the yawed shooter (axis across the view)
keeps its cone [M]. The cone-measuring gates run with the rule off (`MuzzleTuning::endon_star`). The size table's
own_fire_chase cone column above predates the rule: the chase muzzle now draws the star (BOLT_SIZE cone_px 0). Lab: the view rule in
the Muzzle tab's renderer and the `own_side` scene (`muzzle/ire-own-side-cone`); `test_muzzle_view_rule`.

**Acceptance (native host, `run_weapon_effects.py --host native`, 1:20 under the lock).** D3D fixture 259 checks, 0
failures, 0 build warnings (`summary.json`); hook fixture 113 checks, 0 failures (`hook-summary.json`); host
`test_weapon_effects_*` 94 OK; `generate_weapon_effects_programs.py --check`, the lab's `--check` and `schema.py --check`
PASS; `build-bolt` (ap) and `build-bolt-fl` (fl) 0 warnings; `check_no_x87.py --edition ap` PASS (1180 reachable
functions). The own fire chase dump at 5120x1440 (`--dump`, `X3M_WEAPON_EFFECTS_DUMP_CASES=own_fire_chase_5120x1440`, the
plain record restored after it): at 50 ms one muzzle, its disc 40.3 px, the star for the cone; the 4x crop by
`verification/results/bolt-muzzle-look/muzzle_crop.py` (the BOLT_DUMP row's `muzzle_x` / `muzzle_y`), kept local.

### Bolt tunings (2026-11-08, worktree on `effects` `d3adfe275`)

The bolt items of the visual re-review (bolt-muzzle-look.md 11: #2, #3, #4, #5, #6, #10, the three hand pins of 11.5,
#12) and the rim pass's cost; the laws in bolt-muzzle-look.md 1-2. Rows from the native host's bolts-only fixture mode
(`X3M_WEAPON_EFFECTS_BOLTS_ONLY=1`, a development run: the full run aborts in the explosion cases before reaching the
bolts, `native_full_run_abort.txt`), in
`verification/results/bolt-muzzle-look/bolt-tunings-2026-11-08/native_bolts_only_rows.txt` (57 checks, 0 failures [M]).

| Gate | Old law | New law | Measured (native) |
| --- | --- | --- | --- |
| `bolt_halo_px_cap` | sigma <= 40 x rows / 1440, E_h unchanged | bounded cap: sigma <= cap, E_h x cap / sigma_world at that end | IRE at 5 m: E_h 0.0833 = 0.6 x 30 / 216 (1080), x 40 / 288 (1440) |
| `bolt_own_halo_bounded` (new) | - | own fire, chase: halo 10 %-of-peak reach <= 16 x 2.5 x rows / 1440 | IRE 24.8-25.7 px, Mayhem PSG 23.5 px (bound 30 at 1080), sigma 12 px |
| `bolt_rim_darkens_hull` | a 3 px bolt draws no rim | the pixel band [w/2 + 1, w/2 + 4] px under 5 px of body | IRE at 1200 m (3.0 px): rim drawn, band 0.617 over a 1.0 hull; 150 m band 0.690 |
| `bolt_rim_survives_resolve` | kept >= 0.5 | unchanged | kept 1.008 |
| `bolt_colour_core_rim` | centre h, edge hue | + a floor-width core keeps the tint (h x saturate((w - 3) / 3)) | orange and blue IRE at 1500 m (3.0 px): core = tint, err 0.000 |
| `bolt_style_shapes` | heavy core share >= 1.3 x tracer | + heavy head width <= 1.15 x mid-core (no disc) | heavy head / mid 0.778, core shares 0.678 / 0.504 |
| `bolt_orb_breathes` | r_max / r_min >= 1.1 (5 lobes, amp 0.12) | >= 1.03 (7-9 lobes, amp 0.05) | PSP (now capital, 192 px) 1.106, PBE (23 px) 1.259 (boil) |
| `bolt_wave_crescent` end-on | ring: half-peak diameter = S +- 10 % | lens: half-peak annulus 0.95 S +- 0.1 S | 57.6 px of S 60.7, moment ratio 1.000 |
| `bolt_wave_endon_no_hoop` (new) | - | PSG end-on 300 m: centre >= 0.1 rim, no rim bin > 2x its neighbours, no interior bin > 2x the median | centre / rim 0.113, worst rim bin 1.052, worst interior 1.002 |
| `bolt_budget_512_bolts_20_muzzles_5120` | <= 100 us fenced | unchanged | 94.7 us (bolts 74.6, muzzles 20.1; rims 469 of 512, before 32); timestamps 41.6 us; base record 101.1 us |

The first build (the rim computed inside the capsule's line path) measured 105.0 us fenced with 469 rims; the rim pass
now evaluates only its band (review fix 5: along the core's silhouette) and the orb's rim before its noise: bolts 84.6 -> 74.6 us [M]. Pixel program 684 -> 735 slots (flag 741) [M, BOLT_SLOTS]; CPU 49.9 us of
150 [M].

**Mount tier (#4).** `mount_tier` is the catalogue's `size` (weapon_catalog.size_of, the 10 % rule): stock PSP fighter
-> capital, w 3.00 -> 20.00 m (the w_cap_m cap); stock PPC corvette -> capital, 7.50 -> 20.00 m; stock PSG corvette ->
capital, 4.22 m unchanged (d_eng 1082 m by 0.75 range); Mayhem FBC corvette -> capital, 6.15 -> 16.39 m; Mayhem IC
corvette -> capital, 6.75 -> 17.99 m [M, `tier_change.py` -> `tier_change_out.txt`]. Over both AP views 30 / 19 / 12 ->
29 / 15 / 17 fighter / corvette / capital rows [M, `test_mount_tiers_match_the_catalogue`]. Tables regenerated
(`weapon_effects_table.py --game-root /mnt/c/X3`, after the review fixes below): stock (`--stock-only`) sha256
`32966822d1ca7d84603e261e8485331e7f184ef30fe435e4a234ebfb2f5deb3a`, 27 / 6 / 12, 13 overrides; Mayhem 3 (installed)
`1a33a37f74c737d089427febc476ed0b83d5fd2abd681c38fe87be360186e1d6`, 18 / 12 / 8, 22 overrides; the pins: MASS slug,
bolt_e_body 0.6, bolt_halo false (its body has no decodable texture, texture_rgb null, so no tint pin); MD the same plus
bolt_tint texture; FBL orb, bolt_w_orb 2.0; HEPT slug [M, `tint_check.py` -> `tint_check_out.txt`].

**Lab texture means (#12).** The asset extractor writes each weapon body's `mean` by the table generator's rule; all 26
stock and 37 Mayhem rows with a texture equal the table's `texture_rgb` [M, `tint_check_out.txt`; e.g. stock PAC
[121, 82, 39], CIG [19, 20, 43], MAM [134, 120, 139]; Mayhem PAC and MD [66, 66, 66]]. Unknown 10 is answered for
Mayhem: its weapon textures are grey, so its unsaturated-light rows draw white in production as in the lab [M].

**Lab rim order (5.1).** The lab already composes `hull x prod(1 - rim) + sum(capsules)`, which is production's
rim-then-capsule order: the dark lattice in dense streams (GAUSS, IBL) is the rim of one bolt's round cap lying across
the next bolt's body in an end-to-end stream, and production draws it the same way [I from the two compositions]. Lab
rounds: one full bolt round (504 frames), grids under the session scratch `bolt-rework/`.

**Review fixes (2026-11-08).** (1) A hostile ceiling: another ship's line or orb (either end) and a wave's chord never
wider than `hostile_body_max_px` 0.25 of the view height, radiance unchanged; `bolt_hostile_ceiling` at 1080 rows: PSP
side-on 20 m 270 px quad, footprint 258.5 px; PPC end-on 20 m 270 / 61.2 px, footprint 242.9 px (bound 400.8 = 270 +
2 x 2.146 sigma_cap + 2); the PPC side-on row measured 0 (its core's middle off screen) and now samples on screen, not
yet rerun [M, `native_bolts_only_rows_review.txt`]. (2) The halo cap is named a bounded cap (E_h x cap / sigma_world at
sigma = cap: it bounds the halo, it does not conserve its energy) and applied per end (a line's back-end E_h in p2.z,
interpolated along the axis; its streak's rest length is read from p1.y): own fire keeps E_h 0.17-0.53 (IRE) and
0.045-0.14 (Mayhem PSG) of 0.6 at 25-80 m instead of 0.0100 / 0.0027 under the shared minimum, the 10 % reach still
25.5-25.7 px of 30 [M]. (3) Stock MASS's no-op texture-tint pin dropped (E_b 0.6 kept); the impact handler honours
`pin_tint_texture` where a texture exists (Mayhem MD), so its impacts and bolt share the tint. (4) The table reader drops
a row whose bolt pin is outside its range (a zero E_b, a w_orb stored as 0, another bolt_tint name) and counts it
(`bolt_rejected`, logged with `bolt_pins` in the `weapon_effects_table` row); a pin of the wrong JSON type makes the file
malformed; `test_reader_handles_malformed_bolt_pins`. (5) The rim follows the core's own silhouette (the tracer's
pointed tip, no back cap across a tail root). Native bolts-only run: 58 checks, 1 failure, the budget row: 153.7 us
fenced (bolts 126.5, muzzles 27.2) against 94.7 the run before, while the GPU timestamps moved 41.6 -> 43.1 us
(bolts 39.0 -> 41.7) and the unchanged cases rose alike (collapsed_512 32.4 -> 84.7 us, no_rim 63.5 -> 114.2 us, the
muzzles 20.1 -> 27.2 us): the fence overhead of the host, not the pixel work [I]; the row is to be rerun. Pixel program
755 slots (flag 761) [M].

Host: `test_weapon_effects_*` 102 tests, 5 failures, all the D3D / hook records' (`summary.json`, `hook-summary.json`,
recorded on `a4c408e93`-dirty, before the explosion commits of the base: their bound sources already differ at
`d3adfe275`, `death_halo_rim` absent, `passed` false) [M]. `build-bolt` (ap) 0 warnings, `check_no_x87.py --edition
ap` PASS (1181 reachable), d3d9.dll sha256 `917d6dfbff849745f339a54689b7f04d9899729b277218f4956acc188ed3e62d` after the review fixes [M].
### Muzzle rework (2026-11-08, `7c13d540d` on `effects` `d3adfe275`)

The muzzle items of the visual re-review ([bolt-muzzle-look.md](../architecture/bolt-muzzle-look.md) 11 #1, #7, #8, #9,
#11; the laws in its section 3 with the old values): every class draws a 0.25 w white core [0.5–0.8 w], scaled with its
body when the shooter's radius caps the body, inside a structured body at E 4 [cone 1.2, star 2]. The body is the cone
across the view, a four-arm star of cone sprites across the gun axis end-on, in the own cockpit and for ballistic and
flak, each arm 0.3 of its length wide [a fixed 0.12 w], or for plasma the noise bloom at E 3 [1.2]. The flash lives
max(min(law, 0.5 refire period), 3 steps) under (1 − x)² [linear]. Floors at 1440 rows: core 6 px, longest arm, cone and
bloom 12 px. The own cockpit draws disc and star only, its longest arm ≤ 48 px. Puff s0 0.5 [0.3]. An orb or wave is
plasma before the flak flag. Stage `src/proxy/weapon_effects_bolt_stage.h` (`muzzle_elements`, `muzzle_class_of`,
`cone_quad`'s floor and width share, `disc_quad`'s floor), constants `core::muzzle`, the lab's Muzzle tab mirrored, no
shader change.

Lab, two rounds (`batch_shots.py` on the muzzle-only plan, 336 frames; scripts and the per-weapon numbers in
`verification/results/bolt-muzzle-look/muzzle-rework-2026-11-08/`, `muzzle_rework_lab.csv` [M]). Round 1 left a
corvette or capital gun on the lab fighter a ball: the core was capped but its body was capped harder. Round 2 scales
the core with its body and sets arms 0.22 → 0.3. In the own chase view the stock IRE's core is 16.7 px inside a 146 px
star [40 px disc], and the second gun is dark half a period later. The EBC reads as a star with a dark puff, HEPT and PSP
as a noise bloom with a hot point, PPC, GAUSS and FLAK as a core in a star. At 600 m every gun is a 6 px core in a 12 px
star [2–3 px disc]. The cockpit muzzle sits outside the lab's centre window (unknown 9).

Native host (`run_weapon_effects.py --host native`, `summary.json` at `d3adfe275`-dirty, RTX 5070 Ti), the muzzle rows:

| Gate | Result [M] | Bound (old) |
| --- | --- | --- |
| `muzzle_not_a_lamp` | hot share 0.029 (IRE, 789 lit px), 0.029 (PAC, 1307 px); the disc alone 0.242 / 0.251 | ≤ 0.15 (new) |
| `muzzle_stream_not_steady` | stock PBE 19.23/s, 63 frames: 15 at ≥ half a fresh peak = 0.238; any element drawn 0.921 (the three-frame floor); mean peak 0.295 of fresh | ≤ 0.6 (new) |
| `muzzle_floors_at_range` | IRE at 600 m, 1440 rows: core 6.0 px, arm 12.0 px, cone 12.0 px | ≥ 6 / 12 / 12 (new) |
| `muzzle_cockpit_disc_and_star` | IRE, MD, GKE: 1 disc, 4 arms, 0 cones, glows, vents, puffs; arm 36.0 / 36.0 / 15.0 px at 1080 | ≤ 36 px at 1080 (new) |
| `muzzle_class_orb_before_flak` | PSP orb 0x2100 → plasma, PSG wave → plasma, FLAK_ALPHA → flak | (PSP was flak) |
| `muzzle_size_per_class` | cores within the law (IRE 6.17 / 6.25 px, flak scaled with its capped star 12.75 / 12.5 px, floors 2.25 px at 1080); own-star arms' lit root width 0.249–0.277 of their length | ± 0.5 px or 3 %; arms ≥ 0.15 (new) [floor 1.5 px] |
| `muzzle_smoke_surface_only` | lane 1 m in front 0.944 [0.967]; on the gun's surface 0.731 | ≥ 0.48; on the surface ≤ 0.85 (new) [≥ 0.68, < 0.999] |
| `muzzle_relight_resets_not_adds` | 4.7969 = 4.7969, the core alone | ± 1 % |
| `muzzle_endon_star_side_cone` | 4 arms, moment ratio 5.44 [2.13] | ≥ 2 |
| `bolt_budget_512_bolts_20_muzzles_5120` | 96.2 µs fenced (bolts 76.5, muzzles 19.7, 85 muzzle sprites [57]); timestamps 44.4 µs, muzzles 2.6 | ≤ 100 [the record at `6dd25f264`: 101.1, failing] |

Every other muzzle row is unchanged and passing (life, breathing, broadside 1.278, heavy, cockpit gain, placement,
axis, light, ownership). That run (`7c13d540d`) had 6 `death_big_*` failures (the big-tier bodies rendered 0). They did
not reproduce in the review-fix run below with the display hold on and the screensaver killed, so they were host state,
not the explosions code.

**Review fixes (2026-11-08).**
1. The smoke puff keeps its own life; it was cut to half the refire period with the flash.
2. A refire period under the three-frame floor holds the flash at full value with no fade, so nothing alternates
   1 / 0.44 under SETA.
3. The refire cap is now exercised by a gate.
4. The lab's muzzle class comes from production's rule. The override file's hull pin (LAB_DATA `hp`) applies, never the
   hull-class rule's own answer (`hc`). The stock MAM and MAM_1 were ballistic in the lab and are now plasma: a bloom with
   a hot core (re-shot sheets `s_MAM__muzzle.png`, `s_MAM_1__muzzle.png`, kept local).
5. Section 3.2 now notes that a heavy gun faster than about 3.1 shots/s has no afterglow (by design, from the cap) and
   that `muzzle_pose` accepts a radius ≤ 0 (pre-existing; the floors keep extents positive).

Native host, one run:

| Gate | Result [M] | Bound |
| --- | --- | --- |
| `muzzle_smoke_surface_only` (stream added) | EBC third shot at 6.58/s, 150 ms on: 1 puff, the surface through the gun 0.737; the single shot 0.731 | ≤ 0.85 |
| `muzzle_refire_cap_binds` | IRE, period 150 ms: drawn at 64 ms, gone at 80 ms; a first shot drawn at 96 ms | life 75 ms |
| `muzzle_seta_no_alternation` | IRE 10/s, SETA 6 and 10: core + arms 20.0 every frame, change 0.0 | ≤ 2 % |
| `bolt_budget_512_bolts_20_muzzles_5120` | 91.2 µs fenced (bolts 71.3, muzzles 19.9) | ≤ 100 |

Totals: D3D fixture 360 checks, 0 failures; hook fixture 147 checks, 0 failures. Host `test_weapon_effects_*` 101 OK.
`build-muzzle` (ap) 0 warnings; `check_no_x87.py` PASS. `generate_weapon_effects_programs.py --check`, `generate.py
--check` and the lab's `--check` PASS.

## Explosions (step 3)

2026-11-07, uncommitted on the `effects` branch at `d0c96e637` (merged into the worktree; that commit's amendment of
explosion-look.md sections 1, 4 and 11 is the seam below). Spec: [explosion-look.md](../architecture/explosion-look.md)
(ratified) with its "Built numbers (step 3)" (the coordinator's decisions D1-D8 after the first frames);
[weapon-look.md](../architecture/weapon-look.md) 3.2 where it is silent. Built for fighters and missiles; corvettes,
capitals and stations are recorded, logged (`weapon_death`) and forwarded byte-exact. Numbers from
`verification/results/explosion-look/ledger_rows.py` over the native records of
`verification/results/native-windows/weapon-effects/` (RTX 5070 Ti, the native Windows d3d9.dll).

**What is built.**
- **Hook.** `X3M_WEAPON_EFFECTS` takes the token `explosions` (`shields,hull,explosions`, `shields,hull,bolts,explosions`;
  any order; refused without shields and hull; the launcher and the config schema likewise). The two death calls
  `0x0044ac64` (area: plain write, the five bytes straddle `0x0044ac68`) and `0x0044acb2` (atomic) -> `0x00413610` are
  redirected to stubs that pass the caller's EBP (the kill routine's frame: the reason word) and call `core::death`
  (integer only, no call, no x87). Windows (19 bytes each: the two loads, three pushes, the call, the pop, `mov eax,edi`)
  are compared whole before any write; the build script compares them with `/mnt/c/X3/X3AP.exe`'s bytes. Install order:
  the atomic sites first (the other death site with them), then the plain copies (the area site before the impact); all
  or none; shutdown in reverse.
- **The decision seam.** `core::boom::decide(tier)` alone answers ours / native: ours unless the tier is in
  `boom::tier_native` (corvette | capital | station = 14), the asteroid or another class. A record is the stage's only
  when also Present latched the stage and its death programs (`HookState::boom_ready`); then the stub skips the callee
  (`xor eax,eax; ret`). Every other death forwards with every register, the flags and the stack as the call left them.
  The amended section 1's clone row R' (swapped into the type record's death column around the forwarded `0x00413610`,
  LIFO for nested kills) goes where `decide` answers native for the big tiers, in `core::death`'s forward path; not built.
- **Records.** One 64-byte Event per death (role_explosion): tier, class, the type record's death row, the TShips class
  id, the reason word, the killer argument (not followed), the world position (node+0x30), the radius, the site; flag
  `nested` when another death fell in the same game millisecond. Forwarded unrecorded: no live node / class 0x1c, bullet
  area deaths (step 7), the rule-(b) missile. A death retires its object's other records (a staged hull hit stays).
- **Stage** (`weapon_effects_boom.h`, before the hull hits, charged first): the body (the shared fireball program, with
  the white-hot core and the pinhole fix), the star, embers and fragments (hidden inside the body, the sprite program's
  pixel mode: hashed 1.5-2.5 px widths and lengths, a bright head and a fading tail), the light on hulls (1.6 R(t), at
  most 2 R_f) and the halo in air (`weapon_halo_ps.hlsl`), the smoke; the missile's limb-only shell built and switched
  off (`boom::missile_shock`; the fixture forces it on for its own gates, `BoomTuning::force_shock`). The shed order; reach
  cull; per-slot state keyed by sequence. **Pass:** the halo program is the death set beside the hull's
  (`caps().boom_enabled`, fault bit 8); the shock program is optional within it (`caps().shock_enabled`, fault bit 9: a
  refusal drops only the shells); both slot counts logged at device creation and in the attach row; recreated after a Reset.
  **Lab:** the Boom tab (`renderBoom`, HEADER.boom, 14 scenarios of section 8, the vanilla death row in an inset), the
  same laws (`test_boom_tab_presets_and_laws`).

**Hook record** (`hook-summary.json`): 132 checks, 0 failures [M]; 19 `death_*` checks: parse, the decide seam, install
(area plain / other atomic), the forward byte-exact when not owned (EAX / ECX = arg4 / EDX = the killer / EBX / EBP / ESI /
EDI, the flags, the return after the site, ESP 16 below the frame, MXCSR, the x87 control word and stack, LastError), the
fighter and the missile skipped when owned (the frame kept, the stand-in never entered, the record's tier, class, rows
35 / 39, class id 2023, reason, killer, world position, life), the corvette (row 40) and the factory (row 201)
census-forwarded, the asteroid (row 59) forwarded, the rule-(b) missile and a class-0 bullet at the area site forwarded
unrecorded, a ship at the area site classified, the owner's shield hit retired and its staged hull hit kept, nested kills
(a forwarded corvette whose callee runs two fighter deaths through the other window: 3 records, 2 nested, the stand-in
entered once, the outer frame kept; and the same two deaths run after the forwarded `0x00413610` returned, the area
missile first, `death_nested_after_return`), Present releasing the census and keeping ours to life + floor, the forward without
the death programs, shutdown, the hull-only mode leaving the sites, a byte flipped in either window refused before any
write, a faulted read-back at the area site (the other rolled back) and at the impact (both rolled back) [M]. Audit:
both death stubs of the expected shape, both handler entry points 0 calls and 0 x87 (502 / 533 instructions), both
windows equal to `/mnt/c/X3/X3AP.exe` (fdbf3418...f8ab) [M]; the audit fails when that EXE is absent.

**D3D record** (`summary.json`, RTX 5070 Ti): 287 checks, 0 failures; 28 `death_*` [M]. The table and the budget line are
`ledger_rows.py --markdown`'s output over this record. Vanilla FOV (73.74 deg) at
1080p unless noted; 16 ms steps; fixed seeds (the medians over 8 or 16).

| Gate | Result [M] | Bound |
| --- | --- | --- |
| `death_tier_sizes` (PASS; core off) | outline / R(t) at 0.1 / 0.3 L: fighter 600 m 1.038 / 0.967, missile 300 m 1.029 / 0.962; at 0.6 / 0.8 L 0.735 / 0.538 and 0.731 / 0.532 (recorded); the 3 km fighter at 1440 rows: law 7.7 px, drawn 12.1 | 5 % at 0.1 / 0.3 L (D1); floor 12 px |
| `death_body_contrast` / `_silhouette_lobed` / `_colour_core_rim` (PASS / PASS / PASS; 16 seeds, core off) | dark 0.195, high-pass 0.125; max / min 1.86; core T 0.75, rim T 0.5 | 0.15, 0.10; 1.25; >= 0.75, <= 0.5 |
| `death_body_erodes_outside_in` (PASS; 16 seeds, core off) | lit inside R(t) at 0.1 / 0.5 / 0.9 / 1.0 L: 0.866 / 0.731 / 0.154 / 0.0 | >= 0.8 / 0.35-0.75 / <= 0.20 (D1) / 0 |
| `death_body_white_hot_core` (PASS; 8 seeds) | median radiance inside 0.3 R(t): 4.12 at 0.1 L, 1.48 at 0.4 L | >= 3; <= E_b once gone (D5) |
| `death_no_pop_at_birth` (PASS) | first body 9.06 px against R_0 8.45; the star at 60 m 176.3 px half against the 270.0 px cap | |
| `death_flash_star_3_frames`, `death_seta6_three_frame_floor` (PASS, PASS) | lit frames 3 / 3 at 16 / 100 ms; moment ratio 5.4; off the arms 0.0; the body's last frame 1984 / 1900 ms of 2000 | 3; >= 2; <= 0.02 |
| `death_shock_is_a_shell` (PASS; the missile's shell forced on: `missile_shock` 0 in production, `death_missile_shock_off` PASS) | farthest pixel 32.6 / 45.7 px against the law's 31.9 / 45.1 at 0.1 / 0.2 tau; interior 0.0; peak 0.472 / 0.352; boxes after shock_end 0, with the eye inside 0 | 5 %; <= 0.02; <= 0.7 E_b |
| `death_embers_random_and_cooling` (PASS) | seed correlation 0.076; T 0.75 / 0.5 / 0.25 at 0.3 / 0.7 / 1.1 L; farthest 87.5 px at 1.19 L against v_e 1.2 L = 120.0 (speeds 0.5-1.5 v_e) | < 0.5; >= 0.75 / >= 0.5 / <= 0.3; 0.9 x 0.5 .. 1.1 x 1.5 of the reach |
| `death_embers_hidden_in_body` (PASS; D5) | lit share over the body's disc at 0.1 L 0.0; 0 of 32 drawn at birth | <= 0.15; at most half |
| `death_embers_lengths_and_widths` (PASS; D5) | 18 drawn at 0.6 L, lengths 7.9 +- 2.2 px (cap 11.8), widths 1.57..2.48 px | std >= 0.2 mean; 1.5-2.5 px |
| `death_glow_is_the_bodys_light` (PASS; D3, a hull plane 20 m behind) | peak 0.149 / 0.226 at 0.1 / 0.3 L; 10 % equivalent radius 22.3 px against min(1.6 R(t), 2 R_f) = 36.8; over sky 0.0, after L 0.0 | <= 0.4 E_b; <= the law |
| `death_halo_in_air` (PASS; D3) | farthest pixel over R(t) 1.354 / 1.344 / 1.344 / 1.345 at 0.1 / 0.3 / 0.6 / 0.8 L, peaks 0.452 / 0.365 / 0.112 / 0.005 (the erosion fade); 0 after L, 0 with the eye inside the body | <= 1.8 R(t) |
| `death_smoke_darkens_surfaces_only` (PASS; 0.5 grey) | darkest 0.341 at its peak (bound 0.366); 0.502 at 1.6 L, 0.502 over sky | |
| `death_cools_through_ramp` (PASS; 8 seeds), `death_fades_to_zero_at_life` (PASS) | T 1.0 / 0.5 / 0.0 at 0.2 / 0.6 / 1.0 L; every part 0 at its life: 1 | >= 0.75 / >= 0.5 / <= 0.25 |
| `death_nested_three_drawn` (PASS; three fighters 12 m apart) | 3 bodies, worst silhouette correlation 0.175 (each alone); cluster N 2.8; the three's peak 4.66 against one death's 4.629 (x 1.007); the embers' own peak 2.072 | < 0.8; N > 1; <= 1.4 x one |
| `death_eye_inside_bounded` (PASS; D2, own death in chase, the eye 24 m out) | 0.25 L: lit 1.0, std / mean 0.364; 0.6 L: dark 0.528; the 10 m eye 0.336 / 0.587 (recorded); cost 279.4 us | lit >= 0.9 and >= 0.05; dark >= 0.15; <= 300 us |
| `death_octave_lod_hysteresis` (PASS) | entered on frame 1, held 6 frames, left on frame 7 | hold 3 |
| `death_stage_cpu_32_deaths` (PASS) | 32 bodies, 588 streaks, 32 glows: 38.0 us median of 201 builds | <= 50 us |
| `death_budget_5120` (PASS) | fighter 600 m 17.7, 20 fighters 24.0, missile 17.6, nested three 18.3, eye inside 279.4 us | the far cases <= 100 / 150, eye <= 300 |
| `death_programs_attached`, `death_programs_refused_keep_hull`, `death_shock_refused_deaths_drawn`, `death_reset_recovers` (PASS, PASS, PASS, PASS) | shock 95 and halo 60 slots (`MaxPixelShader30InstructionSlots` 4096); fault bit 8: the hull kept, boom off (boom_program_create), 1 body drawn and 0 halo; fault bit 9: the deaths kept (shock 0, shock_program_create), 2 bodies and 2 halos drawn, 0 of 1 shells; the halo drawn after a Reset | |

Budget rows (5120x1440, EVENT-fenced best of 60, gpu_us / weight over 177.5 us) [M]: the fence alone 14.3; body alone (eye inside, 3 octaves, the core) 224.5 / 1.265; smoke alone 226.3 / 1.275; halo alone (the eye in its sphere) 70.2 / 0.395; star 15.9; the missile's shell (forced on) over 0.515 screens 43.4 us: 0.318 a screen with the fence removed (`budget_weight_shock` 0.36).

**Frames judged** (`--dump`, `boom_fighter_600m`, `_missile_300m`, `_nested_3_fighters`, `_eye_inside_fighter` (the
24 m chase eye), `_fighter_600m_hull` (1080p only in v4: its 5120 frames were truncated by a full tmpfs), at 0.1 / 0.3 / 0.6 / 0.8 / 1.0 L, 1080p and 5120x1440; grids by
`verification/results/explosion-look/crop_grid.py` (the clamped composite) and `part_grid.py` (the float parts
tonemapped 1 - e^(-v)), kept local), against the standard (a white core, a lobed eroding body, a soft halo, random
sparks; no lamp, no disc, no sticks):
- the fighter at 600 m: the white-hot core inside a lobed body at 0.1 L with a soft halo; at 0.3 L the core fading and
  short sparks of varied lengths leaving the body; at 0.6 L an eroding orange body, the halo fading; at 0.8 L a small
  dark-red remnant, no halo; at L sparks only. No tangle, no rods, no lamp. The missile reads the same at its scale (its
  shell off; the limb-only shell had read as a flat ring);
- the nested kill (three fighters 12 m apart, re-dumped `stage-dump-boom-v4`): three overlapping, different lobed balls
  whose halos merge into one soft band, no sun; one body still large and hot at 0.8 L while the other two are remnants
  (seeds);
- over a hull 20 m behind: the hull light stays near the body (no lamp); the smoke darkens the hull from 0.8 L;
- the eye inside (24 m): at 0.1 L the core is a large white disc filling a third of the view; 0.3 L a hot wash with
  broad structure; 0.6 L clumps and holes;
- **remaining**: from 0.5 L a small ring and bright centre (a "doughnut") at each body's disc centre, most visible on the
  nested and eye-inside frames. Cause: the fireball program's outward drift is a constant-length radial shift, so each
  octave's samples collapse to one point on the circle r = advect x / freq (0.375 R_s for the first octave at 0.6 L);
  the 0.08 blend (D4) removes only the centre point. The planned fix is a radial zoom (sample p / (1 + advect)); tried
  2026-11-07 and reverted: the hull burst `energy_single` at 296 ms lost 28 % of its lit area (radius 130.0 -> 110.1 px),
  `hull_plasma_globe_shrinks_reddens` failed, the muzzle bloom moved 0.6 % [M]
  (`verification/results/weapon-effects-burst/zoom-attempt-2026-11-07.txt`); it needs the burst's erosion retuned with it.

The hull burst's pinhole row is in "Burst review fixes and the merge" above.

**Acceptance** [M]: `test_weapon_effects_*` 97 OK; `generate_weapon_effects_programs.py --check` PASS, the lab's
`--check`, `schema.py --check`, `generate.py --check` PASS; native run 287 / 132 checks PASS (built from these sources,
`d0c96e637-dirty`). `build-boom` (ap) and `build-boom-fl` (fl) clean builds 0 warnings (d3d9.dll 1bdc864b... /
cada0404...); `check_no_x87.py --edition ap` no violations (1181 reachable functions); `manage.py launch --game-dir
/mnt/c/X3 --config --dry-run` exit 0, weapon_effects unset.

## Explosions: the big tiers

2026-11-07, uncommitted on the `effects` branch at `da8e8bcac`. Spec: [explosion-look.md](../architecture/explosion-look.md)
sections 1, 2.5, 2.6, 4, 7, 9 and its "Built numbers (the big tiers)"; the engine facts of
[weapon-effect-spawn.md](../reverse-engineering/weapon-effect-spawn.md) section 10. Numbers from
`python3 verification/results/explosion-look/ledger_rows.py --markdown --big` over the native records of
`verification/results/native-windows/weapon-effects/` (RTX 5070 Ti, the native Windows d3d9.dll).

**What is built.**
- **Hook.** Corvettes, capitals and stations forward `0x00413610` through the clone row R' (`boom::tier_swap`): the
  handler (integer only, no call) saves the type record's death column, writes R', and returns 2; the stub then calls the
  callee itself with the three arguments copied and calls `x3m_weapon_death_restore` after it returns (LIFO; nested kills
  of another type touch their own record, the same type writes the same R' and restores its own saved value). R' not
  ready (none built, a zeroed or foreign slot), an unreadable record or a full stack: byte-exact forward, census
  (`cause=`). Present restores a swap an unwind left on the stack.
- **R'.** Built at Present (`clone_upkeep`, `weapon_boom_rows` per event), never while the scope loads, from the loaded
  death rows of the type records we own; the array from the EXE malloc `0x005112c4` (its prologue checked first); never
  freed by us. A vanished or changed source drops its pair and abandons its slot (left as it is, at most 16); R' is built
  again in another slot, never refilled or regrown in place (review A); a zeroed slot is refilled only with the same
  count; free slots skip every row a death column or a PROPAGATE child references (review C); the scan is timed
  (`scan_us`, review L). `shutdown()` restores a leaked swap before the sites (review H); `weapon_death` carries one
  `cause=` (review D).
- **Stage.** Through the pre-roll (capital 1.5 s, station 3 s) the dying hull's light (fading with the hull copy over
  P .. P + 1 s) and the secondaries at the geometric cadence inside the hull shape (the bubble cache's box, else the axis
  capsule; `weapon_death bubble_cached=`), each with its star, glow and 8 embers; the body from 0.5 R_f at P, no shock on
  any tier (D14), the big tiers' halo capped (D15), the body's embers from the hull shape's surface. Budget: the family's own 2.5-screen cap, the
  secondaries past the two newest shed at step 5 (all of them from P); the drawn sphere 1.33 -> 1.21 R(t) by pixel size.
- **Lab.** The Boom tab draws the carrier box at FADEOUTBODY's alpha, the hull light, the secondaries and the surface
  embers; the 14 scenarios are now in the scenario menu (they were missing from it); pre-roll, secondaries and cadence
  controls.
- **Coordinator decisions D9-D15** (explosion-look.md "Built numbers (the big tiers)"): the hull light fades with the copy
  (D9), the 2.5-screen cap (D10), the secondaries protected only through the pre-roll (D11: worst close-capital frame
  361.4 us, every step shed 82.8 us), the drawn sphere 1.33 -> 1.21
  over 64-320 px (D12), the CPU gate per drawn quad (D13); no shell on any tier (D14, `boom::shock_tiers` 0: the corvette's
  grey ring and the capital's and station's white ring at birth were the soap bubble; the replacement is a refraction limb
  through the engine shimmer pass); the big tiers' halo capped at 1.15 R(t) and 0.2 E_b, out by 0.6 L (D15: a balloon from
  0.3 L).

| Gate | Result [M] | Bound |
| --- | --- | --- |
| hook: `death_clone_row_built`, `death_clone_malloc_prologue_refused` (PASS, PASS) | row 37 -> slot 998: 12 elements (10 DISTANT copies, 2 empty nodes, 1 PROPAGATE dropped), row flags 0, FADEOUTBODY from 1500 ms, the hold to 2500 ms; row 40 -> 6 elements (P 0); row 201 fade from 3000 ms; 3 arrays from the EXE malloc stand-in; a changed prologue builds none | |
| hook: `death_swaps_death_column_big_tiers`, `death_swap_callee_state` (PASS, PASS) | the callee read R' in its column, the original back after; ESP 005ea898 against the frame's 005ea8b8 (32 below), return 0081211b inside the stub 008120e0, ECX 00a4a4a4 / EDX 00c0ffee, flags 00000ac7 | |
| hook: `death_nested_lifo_restore`, `death_nested_after_return_swap` (PASS, PASS) | 3 entries in one outer call: outer M2 998, inner M6 999, inner M2 998; after: M2 37, M6 40, depth 0, 3 restores | |
| hook: `death_clone_not_ready_forwards_exact`, `death_clone_rebuilt_after_reload`, `death_clone_slot_taken_refused`, `death_swap_leak_restored_at_present` (PASS, PASS, PASS, PASS) | totals: swapped 13, restores 12, changed 0, leaked 1, clone_missing 6, swap_full 1, builds 10, invalidated 4 | |
| hook: `death_clone_scan_refused_while_loading`, `death_clone_source_vanished_drops`, `death_clone_source_changed_new_slot`, `death_clone_free_slot_skips_referenced` (PASS, PASS, PASS, PASS) | a scan due while loading added nothing; a vanished source's pair dropped, its slot 993 left as it was; row 37 moved: slot 998 abandoned, R' again in 992 (rows 995 / 994 referenced, skipped); 2 slots abandoned | |
| hook: `death_clone_scan_time` (PASS; the installed tables' 1553 records, the fixture's engine memory) | 3 pairs, 9.4 us median of 15 scans, 446.0 us the first (cold) | <= 250 us |
| audit | death stubs {'area': True, 'other': True}; calls {'_x3m_weapon_death_area': [], '_x3m_weapon_death_other': [], '_x3m_weapon_death_restore': []}; x87 0; restore 45 instructions | 0 calls, 0 x87 |
| `death_big_tier_sizes` (PASS; core off) | outline / R(t) at 0.1 / 0.3 L after P: corvette 1.5 km 1.014 / 0.970, capital 1.5 km 0.990 / 1.004, station 3 km 0.984 / 0.963; R_f M7 556.2 m, shipyard 1800.0 m | 5 % |
| `death_preroll_cadence_and_hull_light` (PASS; capital 1.5 km, 4 ms steps) | capsule: 8 births 0-1484 ms, 8 inside; cached box: 8, 8 inside (bubble flags 3); bodies before P 0; first body 0.5 R_f; hull light 0.198 / 0.3938 / 0.198 against 0.198 / 0.3939 / 0.198 at 0 / P - 16 / P + 500 ms, after the fade 0 | gaps 20 % + 4 ms; 10 %; 0.5 R_f +- 3 % |
| `death_big_no_pop_at_birth` (PASS) | first body over R_0: corvette 1.0, capital 1.007; rise <= 2x over P +- 32 ms: 1; the capital's star 145.0 px half against 270.0 | |
| `death_shock_off_all_tiers` (PASS; D14) | `shock_tiers` 0: 0 shells over the three tiers at 0.02 / 0.1 / 0.2 L | 0 |
| `death_big_halo_capped` (PASS; D15, capital 1.5 km, the halo alone) | at 0.2 L: farthest pixel 1.013 R(t) (259.1 px of 255.8), peak 0.3; halos at 0.6 L 0 | <= 1.25 R(t); <= 0.2 E_b; 0 |
| `death_big_shock_is_a_shell` (PASS; corvette, forced on: the law's evidence only) | farthest 80.4 / 114.1 px against 79.2 / 112.1 at 0.1 / 0.2 tau; interior 0.0; peak 0.472 / 0.352 | 5 %; <= 0.02; <= 0.7 E_b |
| `death_big_light_and_fade` (PASS) | capital glow peak 0.452 / 0.365 at 0.1 / 0.3 L, over sky 0.0; elements left at P + 1.6 L: 0 | <= 0.4 E_b; 0 |
| `death_big_eye_inside_bounded` (PASS; the eye 720 m from a capital) | 0.25 L: lit 1.0, std / mean 0.488; 0.6 L: dark 0.562 | D2 |
| `death_big_seta6` (PASS; 100 ms steps) | 8 secondaries born; body last at 5400 of 5500 ms; star 3 frames | 8; within a step; 3 |
| `death_budget_capital_close_5120` (PASS) | worst 361.4 us (capital_close_700ms, 2.434 screens, shed 9) over 22 frames P - 100 .. P + 2000 ms; every step shed 82.8 us | <= 450; <= 300 |
| `death_big_stage_cpu` (PASS) | 4 capitals, 2 stations, 26 fighters: 30 bodies, 35 secondaries, 791 quads: 53.2 us median (0.0673 us a quad) | 0.08 us a quad, 100 us |

**Judged from the frames** (v3: `crop_grid.py` over the dump of `boom_corvette_1500m`, `boom_capital_1500m_broadside`,
`boom_capital_close_720m`, `boom_station_3km` at 1920x1080 and 5120x1440 in the fixture's scene model, the carrier box at
FADEOUTBODY's alpha with its depth in the lane; grids local, not tracked). The hull visible and lit through the pre-roll and
through its fade, secondaries popping on it (half hidden where they sit inside the solid box: the model's worst case), the
body born at 0.5 R_f eating the hull over its 1 s fade, no ring (D14), no balloon about the body (D15), no lamp at the
tiers' distances, nothing native left. The v1 / v2 frames showed the rings, the halo disc and the hull light's drop at P.

**Open items** (engine-side assumptions not verified, and the review's notes G, I, J, M):
- Carrier alpha before P [unverified, engine]: R' starts at t0 = 0, so for P > 0 `FADEOUTBODY`'s ramp is read before
  its element starts; the alpha is assumed to stay 255 until P (natively the ramp only ever ran from t0 on).
- Zeroed slot [unverified, engine]: a zeroed slot means the engine's reset freed its array and no instance outlives the
  reload; the same-count refill and the reclaiming of abandoned slots rest on it.
- Unknown class ids: a ship whose class id is outside `patch_tier_of_ship` swaps only when a known class shares its
  (row, pre-roll).
- Scan cost in production: measured in the fixture's engine memory (a plain copy); the production reader's per-read
  cost on the 1,553 records of the installed tables is not measured (the first, cold scan took 446 us in the fixture).
- Review note G [engine, diagnostic]: the loader allocates row arrays through `0x004b89f0` (malloc plus the counters at
  `0x6085f4` / `0x6085f8`); the reset subtracts count x 0x70 for every slot it frees, so each R' freed by a reset pushes
  the counters below their true value; the readers found are diagnostic only.
- Review note I: the swap path is the first stub whose return address stays live across an engine call; a FreeLibrary
  during `0x00413610` would return into unloaded code (unreachable: shutdown runs only at FreeLibrary).
- Review note J [unverified, engine]: the stage dates the pre-roll from its first frame, the engine's `FADEOUTBODY`
  from the kill (`inst+0x28`); the offset is zero if the game clock ticks once per frame.
- Review note M: `WRECK` is cleared in R', so a kill with a non-zero third argument (the script path) loses
  "CreateWreck" (design item 2a).

**Acceptance** [M]: `test_weapon_effects_*` 98 OK; `generate_weapon_effects_programs.py --check`, the lab's `--check`,
`schema.py --check`, `generate.py --check` PASS; native run D3D 298 / 0, hook 147 / 0 (built from these sources,
`da8e8bcac-dirty`); `build-boom-big` (ap) and `build-boom-big-fl` (fl) clean builds 0 warnings; `check_no_x87.py --edition
ap` no violations (1181 reachable functions); `manage.py launch --game-dir /mnt/c/X3 --config --dry-run` exit 0. Not flown.

## Fireball drift law (2026-11-07)

**The defect and the law.** `weapon_fireball_ps.hlsl` shifted every noise octave by a constant `advect` cells along
p / |p|, so each octave's circle |p| = a / f_i collapsed onto one noise point and the disc inside it was a point-reflected
copy: a dot with a ring at the centre of every hull burst and death body from about 0.5 L (octaves 2 and 3, inside about
0.15 R_s; the 0.08 seed-direction blend of the pinhole fix hid only the centre point). The new law samples octave i at
p (f_i - g_i), g_i = (f_i / 2) / sqrt(1 + (|p| f_i / 2a)^2): the radius u = |p| f_i cells moves to u - a y / sqrt(1 + y^2),
y = u / 2a, strictly increasing (slope 1/2 at the centre, at least 1/2 everywhere), the full shift a far out (0.89 a at
u = 4a), no direction and no division at the centre, so the seed-direction blend is gone. The lab's two body loops carry
the same law (`test_fireball_drift_law_matches_the_program`). Program 1603 words (1618 before); fill rate unchanged:
`body_alone_fills_frame` 5120x1440 224.4 us against 226.3 / 226.5 on the old program, `burst_alone_fills_frame` 107.9
against 109.0 / 107.9 [M].

**Retune.** The collapse had kept a hot dot alive late. With the law alone at least 3 of 5 plasma globes were dark at
0.95 L (`hull_plasma_globe_shrinks_reddens` failed, as with the radial zoom of `zoom-attempt-2026-11-07.txt`), and the
death body's lit share at 0.9 L went to 0.200-0.258 against its 0.20 bound as erode_t came down. Built:
`fireball_erode_t` 0.30 -> 0.28, `fireball_erode_soft` 0.20 -> 0.28 (a softer edge drops the dim rim under 10 % of the
peak without changing which pixels survive), `fireball_column_pow` 0.35 -> 0.25 (keeps the limb out to the size law at
0.3 L: 0.953-0.994, bound 0.95) [M].

**Gates** (`hull_burst_no_centre_ring`, `death_body_no_centre_ring`; `burst::ring_of`): at 0.5 / 0.6 / 0.7 L the body's
mean luma over its peak in rings about the drawn sphere's centre; a local maximum at a ring k >= 1 centred inside 0.4 R_s
(R_s the drawn radius; a monotone core at ring 0 never counts), its prominence against the minima within 0.1 R_s on either
side (rings of 0.025 R_s, at least 1.5 px), every hit's (8 hull hits, 16 fighter deaths) at most 0.15; the pooled lit share
of the annulus 0.3-0.45 R_s at most 1.3 x the mean of 0.15-0.3 and 0.45-0.6. The brief's wide form (rings of 0.05 R_s
against the whole profile's minima) is printed as `prominence_wide`: worst hits 0.26-0.29 (hull) / 0.26-0.61 (death) on
the old law, 0.15-0.20 / 0.13-0.16 on the new, where a wide hole with clumps about it scores; the local form keeps more
margin and is the one gated. The annulus ratio does not separate the laws (0.95-1.07 old, 0.96-1.10 new): the doughnut is
small and its ring lies inside 0.3 R_s.

| Figure [M] | old law | drift law + retune |
| --- | --- | --- |
| hull worst-hit local prominence 0.5 / 0.6 / 0.7 L | 0.181 / 0.258 / 0.142 | 0.079 / 0.076 / 0.093 |
| death worst-hit local prominence 0.5 / 0.6 / 0.7 L | 0.094 / 0.195 / 0.168 | 0.085 / 0.061 / 0.036 |
| annulus ratio hull / death (0.6 L) | 0.990 / 1.064 | 0.962 / 1.054 |
| hull erosion lit share 0.1 / 0.5 / 0.9 L, mean_r ratio | 0.870 / 0.504 / 0.063, 0.498 | 0.868 / 0.522 / 0.114, 0.640 |
| death lit 0.1 / 0.5 / 0.9 L | 0.866 / 0.731 / 0.154 | 0.864 / 0.691 / 0.186 |
| plasma globe b/r 0.3 / 0.65 / 0.95 L | 0.694 / 0.511 / 0.469 | 0.691 / 0.541 / 0.478 |
| 20-hit peak at 120 ms (rule) | 2.731 | 2.650 (-2.9 %) |
| bolt bloom peak / lit / eq_r; puff | 0.917 / 2453 / 27.94; 0.0332 / 45 / 3.78 | 0.917 / 2505 / 28.24; unchanged |
| `energy_single` burst 40 ms peak / lit / eq_r / blobs | 1.383 / 19990 / 79.8 / 1 | 1.415 / 19816 / 79.4 / 2 |
| 120 ms | 1.299 / 51338 / 127.8 / 1 | 1.354 / 50379 / 126.6 / 1 |
| 296 ms | 0.263 / 11074 / 59.4 / 1 | 0.080 / 16254 / 71.9 / 2 |
| 696 ms | 0 | 0 |
| `heavy_capital_64` burst 296 ms peak / eq_r | 0.259 / 659.0 | 0.226 / 731.9 |
| `boom_fighter_600m` body 1200 / 1600 ms peak | 1.432 / 0.960 | 0.691 / 0.260 |

The 5 % match to the accepted frames holds at 40 and 120 ms (peak within +4.2 %, eq_r within 1 %; at 40 ms the
body splits into two blobs at the 10 % threshold, median blob radius 56 px) and at 696 ms (nothing lit), not from 0.45 L on: there the old frames' peak is the defect itself (the
brightest pixel of `energy_single` sits 6 px from the centre at 248 ms and 24 px at 296 ms, inside the fold's curl; the
fighter death's 1200 / 1600 ms peak is its centre dot). No retune of this law family restores those figures without
putting the hot centre back. Frames judged side by side (local, `stage-dump-drift/`): the burst keeps its clumps, holes
and broken rim, the curls and dot-and-ring centres are gone from the hull singles, the capital-close bursts and the
fighter death; the late hull frames (248-296 ms) are a dimmer, more even haze than before (the old ones held a hot curl);
no lamp. The capital-close death at 1540 ms keeps its dark centre and twisted lobes unchanged: a different cause (open).
Figures: `verification/results/weapon-effects-burst/drift-law-2026-11-07.txt` (`drift_compare.py`, `ring_rows.py`).

**Acceptance** [M]: `test_weapon_effects_*` 99 OK; `generate_weapon_effects_programs.py --check` PASS, the lab's `--check`
PASS; native run (`run_weapon_effects.py --host native` under the X3 lock, 1:51) D3D 300 / 0, hook 147 / 0; `build-drift`
(ap) and `build-drift-fl` (fl) 0 warnings; `check_no_x87.py --edition ap` no violations (1181 reachable functions). The
Windows screen saver returned within 3 minutes of each kill and lost the device in two runs; the runs that count held a
display-required execution state for their own duration only. Not flown.

## Explosions: the fire wave and the composed body

2026-11-07, uncommitted on `effects` at `bfe904e97`. Spec: [explosion-look.md](../architecture/explosion-look.md) section 13
and its "Built numbers (section 13)". Table from `python3 verification/results/explosion-look/ledger_rows.py --markdown --wave`
over the native records (`native-windows/weapon-effects/summary.json`: D3D 323 checks, 4 failed; hook 148 / 0;
`native-windows/engine-shimmer/summary.json`: 73 / 0).

| Gate | Result [M] | Bound |
| --- | --- | --- |
| `wave_program_attached` (PASS) | wave_ps 359 slots, compose_ps - slots, the device's 4096 | created |
| `wave_radius_law` (PASS; capital 5 km face-on) | outer 10 % radius over the law's (r(t) + 1.52 w_e) at 0.1 / 0.5 / 1.0 tau: 1.001 / 1.002 / 1.003 | 5 % |
| `wave_never_brightest` (PASS) | wave peak 0.938 / 0.495 / 0.138 against the body's 4.98 / 3.91 / 2.57; after L_w 0, other tiers 0 | <=; 0; 0 |
| `wave_thick_not_a_line` (PASS) | face-on band 50.0 px (need 23.0); edge-on bar from above 129.0 px (need 95.0) | >= 0.2 R_f f / z |
| `wave_broken_circumference` (PASS) | lit share of 64 bins 0.719, max / min 20.0 | 0.40-0.85; >= 2 |
| `wave_fire_not_grey` (PASS) | edge / band 1.62, band R / G 2.69, hue 0.75 / 0.25 at 0.2 / 0.9 L_w, chroma 0.776 / 0.932 | >= 1.3; >= 1.3; >= 0.5 / <= 0.35; >= 0.2 |
| `wave_refraction_strips_present_and_capped` (PASS; capital 1.5 km) | at birth 8 strips, 0.123 screens, request 1; at P + 200 ms wave 1, strips 0 (capped 1, 0.514 screens) | 8; <= 0.25 |
| `wave_shimmer_strips_area` (PASS) | at r 1,000 px: 6 strips 0.066 screens against the box 0.568: 0.116 | <= 0.6 |
| `wave_lights_swept_hulls` (PASS; a hull plane through the centre, 2 R_f out) | 0.337 while the front crosses (32 ms), 0.0 at 0.5 tau | >= 0.1; 0 |
| `compose_silhouette_cv` (PASS; median of 8 seeds) | CV at 0.3 / 0.6 L: fighter 0.193 / 0.204, capital 0.397 / 0.456, station 0.341 / 0.445; the single body 0.143 / 0.144 / 0.138 | >= 0.18 |
| `compose_silhouette_changes` (PASS) | correlation / RMS between the ages: fighter 0.76 / 0.208, capital 0.892 / 0.219, station 0.879 / 0.222 | <= 0.7 or >= 0.15 |
| `compose_not_grapes`, `compose_under_one_sun` (PASS, PASS) | largest blob 1.0 / 1.0 / 1.0; peak over the single body 1.0 / 1.0 / 1.0, mean lit 0.994 / 1.013 / 0.978 | >= 0.8; <= 1.4, 0.7-1.3 |
| `compose_births_spread`, `compose_jets_capital` (PASS, PASS) | N / jets: fighter 3 / 0, capital 4 / 1, station 5 / 1; born at 0: 1, by 0.15 L all; at L 0; jets 2.0:1 | |
| `compose_eye_inside_shared` (PASS) | own death (24 m): drawn 2, shared 1; close capital at 0.25 L: drawn 2, shared 1 | <= 4, shared |
| `compose_lod_floor` (PASS) | a budget of nothing: 2 drawn, jets 0, shared 1, 3 dropped | 2, 0 |
| `compose_box_holds_spheres` (PASS; 8 seeds x 3 tiers, a 1.6 x box) | lit pixels outside the box: 0 | 0 |
| `death_budget_capital_close_5120` re-run (PASS) | worst 412.5 us (capital_close_700ms, shed 10); the wave drawn on 8 of 32 frames P .. P + L_w | <= 450; >= 80 % |
| timing (5120x1440) | wave alone 99.7 us (weight 0.499), miss 111.2 us (0.564); compose factors N 2 / 3 / 4 / 5 0.729 / 0.845 / 0.932 / 1.011, shared N 4 0.804; eye inside N 4 285.3 us, eye inside 286.2 us | pinned 0.50 / 0.10; <= 300 |
| engine shimmer `wave_band_displaced_outward_sky_included`, `wave_zero_outside_band_front_hull_and_strips` (1920x1080) | band 17694 px, 17677 moved, 17584 outward, mean 3.96 px; sky band 8855, 8850 moved; beyond 0 / front hull 0 / outside 0 changed | >= 80 %, 95 %; 0 |
| engine shimmer `wave_band_displaced_outward_sky_included`, `wave_zero_outside_band_front_hull_and_strips` (5120x1440) | band 31153 px, 31068 moved, 30424 outward, mean 5.404 px; sky band 15582, 15543 moved; beyond 0 / front hull 0 / outside 0 changed | >= 80 %, 95 %; 0 |
| engine shimmer `wave_shimmer_strips_timing` (5120x1440, r 1000.0 px) | strips 0.0884 ms against the box 0.1657 ms: 0.533 (copy 3264480 / 3263040 px) | <= 0.6 |

**Open after this record.** `death_body_erodes_outside_in` (lit share inside R(t) at 0.9 L 0.246 > 0.20: the composed
cluster keeps more lit pixels late than the single body), `death_stage_cpu_32_deaths` (64.2 us > 50) and
`death_big_stage_cpu` (0.0838 us a quad > 0.08: the per-frame placement, outline and table writes), `wave_drawn_close_capital`
(the wave drawn on 8 of 32 frames P .. P + L_w at the close capital: its box costs 0.56 a screen where the note assumed 0.10,
so the budget sheds it at step 3; the worst frame 412.5 us within 450). Frames judged from the dump (local,
`stage-dump-wave/`). Not flown.

### The trailing sheet, the strips and the lobed body (2026-11-07, same dirty tree)

The main session's judgement of the first frames (the wave a thin smooth hoop at 1.5 and 5 km, the composed capital body
large smooth white balloons from P + 800 ms) and the four open gates above, implemented as explosion-look.md section
13's D26-D35. Table from `ledger_rows.py --markdown --wave` over the native record of this tree (D3D 328 checks, 4 failed;
hook 147 / 0; engine shimmer 73 / 0 on its second run, the first failed `wave_shimmer_strips_timing` at 0.759 with the
strips' own time 0.128 ms against 0.084 on the rerun: host timing, no shimmer source changed):

| Gate | Result [M] | Bound |
| --- | --- | --- |
| `wave_program_attached` (PASS) | wave_ps 524 slots, compose_ps - slots, the device's 4096 | created |
| `wave_radius_law` (PASS; capital 5 km face-on) | the lit bins' median edge radius over r(t) at 0.1 / 0.5 / 1.0 tau: 1.016 / 1.009 / 1.01 (the median outer 10 % radius 393.2 / 775.5 / 857.1 px against r(t) + 1.52 w_e 362.2 / 744.7 / 823.9: the tongues) | 5 % |
| `wave_sheet_thick`, `wave_hot_edge_trailing_sheet`, `wave_flame_tongues` (PASS, PASS, PASS; 5 km, 0.5 tau) | band + sheet 0.751 R_f at 10 % of each ray's peak; edge over the sheet's mean 0.2-0.6 R_f behind 7.53, the sheet at 0.7 R_f behind 0.0286 of the peak; the lit bins' outer radii spread 0.568 R_f | >= 0.6; >= 1.4, >= 0.01; >= 0.2 |
| `wave_never_brightest` (PASS) | wave peak 3.514 / 1.891 / 0.537 against the body's 4.871 / 3.373 / 1.499; after L_w 0, other tiers 0 | <=; 0; 0 |
| `wave_thick_not_a_line` (PASS) | face-on band 86.5 px (need 23.0); edge-on bar from above 166.0 px (need 95.0) | >= 0.2 R_f f / z |
| `wave_broken_circumference` (PASS) | lit share of 64 bins 0.719, max / min 54.34 | 0.40-0.85; >= 2 |
| `wave_fire_not_grey` (PASS) | edge / band 2.06, band R / G 1.96, hue 0.75 / 0.25 at 0.2 / 0.9 L_w, chroma 0.731 / 0.93 | >= 1.3; >= 1.3; >= 0.5 / <= 0.35; >= 0.2 |
| `wave_refraction_strips_present_and_capped` (PASS; capital 1.5 km) | at birth 8 strips, 0.123 screens, request 1; at P + 200 ms wave 1, strips 0 (capped 1, 0.514 screens) | 8; <= 0.25 |
| `wave_shimmer_strips_area` (PASS) | at r 1,000 px: 6 strips 0.066 screens against the box 0.568: 0.116 | <= 0.6 |
| `wave_lights_swept_hulls` (PASS; a hull plane through the centre, 2 R_f out) | 0.352 while the front crosses (32 ms), 0.0 at 0.5 tau | >= 0.1; 0 |
| `compose_silhouette_cv` (PASS; median of 8 seeds) | CV at 0.3 / 0.6 L: fighter 0.323 / 0.327, capital 0.414 / 0.512, station 0.338 / 0.47; the single body 0.143 / 0.144 / 0.138 | >= 0.18 |
| `compose_silhouette_changes` (PASS) | correlation / RMS between the ages: fighter 0.755 / 0.263, capital 0.898 / 0.252, station 0.839 / 0.247 | <= 0.7 or >= 0.15 |
| `compose_not_grapes`, `compose_under_one_sun` (PASS, PASS) | largest blob 1.0 / 1.0 / 1.0; peak over the single body 0.898 / 0.898 / 0.898, mean lit 0.907 / 0.97 / 0.961 | >= 0.8; <= 1.4, 0.7-1.3 |
| `compose_births_spread`, `compose_jets_capital` (PASS, PASS) | N / jets: fighter 3 / 0, capital 4 / 1, station 5 / 1; born at 0: 1, by 0.15 L all; at L 0; jets 2.0:1 | |
| `compose_eye_inside_shared` (PASS) | own death (24 m): drawn 2, shared 1; close capital at 0.25 L: drawn 2, shared 1 | <= 4, shared |
| `compose_lod_floor` (PASS) | a budget of nothing: 2 drawn, jets 0, shared 1, 3 dropped | 2, 0 |
| `compose_box_holds_spheres` (PASS; 8 seeds x 3 tiers, a 1.6 x box) | lit pixels outside the box: 0 | 0 |
| `compose_clumps_survive`, `compose_no_white_region` (PASS, PASS) | blobs of >= 1 % of the lit pixels at 0.6 L: fighter 2.0, capital 2.0, station 2.0; widest white run at 0.3 / 0.6 L (core on) 0.0 / 0.0 / 0.0 R_f | >= 2; <= 0.3 |
| `death_stage_cpu_32_deaths`, `death_big_stage_cpu` (FAIL, FAIL) | 32 fighters 60.5 us (single body 57.7); the big set 81.4 us, 0.1029 a quad (single body 78.3, no wave 77.1) | 50; 0.08 |
| `death_budget_capital_close_5120` re-run (PASS) | worst 396.7 us (capital_close_700ms, shed 9); the wave drawn on 1 of the 6 frames P .. P + L_w it is on screen (`wave_drawn_close_capital` FAIL) | <= 450; >= 80 % |
| timing (5120x1440) | wave alone 239.4 us (1.0 screens drawn: 1.283 a screen), one octave 216.0 us (1.151), the box at a station's birth 242.0 us (1.297); compose factors N 2 / 3 / 4 / 5 0.703 / 0.809 / 0.889 / 0.977, shared N 4 0.814; eye inside N 4 274.7 us, eye inside 272.6 us | pinned 1.26 / 1.11; <= 300 |
| engine shimmer `wave_band_displaced_outward_sky_included`, `wave_zero_outside_band_front_hull_and_strips` (1920x1080) | band 17694 px, 17677 moved, 17584 outward, mean 3.96 px; sky band 8855, 8850 moved; beyond 0 / front hull 0 / outside 0 changed | >= 80 %, 95 %; 0 |
| engine shimmer `wave_band_displaced_outward_sky_included`, `wave_zero_outside_band_front_hull_and_strips` (5120x1440) | band 31153 px, 31068 moved, 30424 outward, mean 5.404 px; sky band 15582, 15543 moved; beyond 0 / front hull 0 / outside 0 changed | >= 80 %, 95 %; 0 |
| engine shimmer `wave_shimmer_strips_timing` (5120x1440, r 1000.0 px) | strips 0.0836 ms against the box 0.1675 ms: 0.499 (copy 3264480 / 3263040 px) | <= 0.6 |

Also measured in that record: `death_big_eye_inside_bounded` FAIL (the close capital at 0.25 L lit share 0.842 against
0.9, 0.922 before the stratified offsets; with the eye inside, N 2 sub-bursts set apart no longer fill the view as the
single ball did), `death_body_erodes_outside_in` PASS (lit share 0.835 / 0.585 / 0.154 at 0.1 / 0.5 / 0.9 L in the dev
runs), `death_cools_through_ramp` PASS (T 0.75 at 0.2 L only with `compose_core_x` 0.25; 0.50 at 0.2). The CPU rows
swing with the host: over thirteen dev runs of this session (boom-only builds, logs not kept) the 32 fighters measured 40.0-61.0 us and the single body
alone 38.7-57.9 us, the big set 59.0-81.9 us (0.075-0.103 a quad; the original record 64.2 / 0.0838); paired in one run
the composed body's share (median - single_body_us) ranged -9.3 to +7.4 us, inside that swing, after the cached
placement and the LOD by count (32 fighters: N 2; the big set: 30 bodies, N 2). `wave_drawn_close_capital` now counts the
frames the wave is on screen (face-on at 720 m its front leaves the frame within ~0.2 s; the strips then build nothing):
drawn on P + 0 (box, 0.827 screens, its second octave and 4 secondaries shed), shed at 50-200 ms: the wave's full-screen
cost (1.28 a screen, 227 us) does not fit the 450 us fence beside the 316-372 us of the rest under the given order (2b
octave, 2c secondaries, 3 the wave). Grids judged (local, `stage-dump-wave/v2/`): the wave a thick mottled fire disc
with a hot broken edge and flame tongues, never a hoop; the body lobed with separate clumps to 0.6 L on every tier, no
white region from 0.3 L; the large capital lobes at 2700 / 3900 ms are still smooth (the burst noise at that scale) and
P + 800 ms (0.2 L) keeps a near-white region (E_b heat clipped in the dump, a fifth of the core). Not flown.


### v3: the ramp's colour, the eye in the plane, the fourth octave, the wave last (2026-11-07, same dirty tree)

The coordinator's seven decisions on the v2 grids, built as explosion-look.md D38-D45. Table from `ledger_rows.py
--markdown --wave` over the last native record (D3D 330 checks, 3 failed; hook 147 / 0; the engine shimmer record above,
its sources unchanged):

| Gate | Result [M] | Bound |
| --- | --- | --- |
| `wave_program_attached` (PASS) | wave_ps 589 slots, compose_ps - slots, the device's 4096 | created |
| `wave_radius_law` (PASS; capital 5 km face-on) | the lit bins' median edge radius over r(t) at 0.1 / 0.5 / 1.0 tau: 1.011 / 1.002 / 1.001 (the median outer 10 % radius 393.2 / 775.5 / 855.6 px against r(t) + 1.52 w_e 362.2 / 744.7 / 823.9: the tongues) | 5 % |
| `wave_sheet_thick`, `wave_hot_edge_trailing_sheet`, `wave_flame_tongues` (PASS, PASS, PASS; 5 km, 0.5 tau) | band + sheet 0.977 R_f at 10 % of each ray's peak; edge over the sheet's mean 0.2-0.6 R_f behind 1.93, the sheet at 0.7 R_f behind 0.128 of the peak; the lit bins' outer radii spread 0.552 R_f | >= 0.6; >= 1.4, >= 0.01; >= 0.2 |
| `wave_never_brightest` (PASS) | wave peak 0.878 / 0.473 / 0.141 against the body's 4.871 / 3.373 / 1.499; after L_w 0, other tiers 0 | <=; 0; 0 |
| `wave_thick_not_a_line` (PASS) | face-on band 112.5 px (need 23.0); edge-on bar from above 150.0 px (need 95.0) | >= 0.2 R_f f / z |
| `wave_broken_circumference` (PASS) | lit share of 64 bins 0.719, max / min 20.01 | 0.40-0.85; >= 2 |
| `wave_fire_not_grey` (PASS) | edge / band 1.42, band R / G 1.59, hue 0.75 / 0.25 at 0.2 / 0.9 L_w, chroma 0.568 / 0.924 | >= 1.3; >= 1.3; >= 0.5 / <= 0.35; >= 0.2 |
| `wave_refraction_strips_present_and_capped` (PASS; capital 1.5 km) | at birth 8 strips, 0.123 screens, request 1; at P + 200 ms wave 1, strips 0 (capped 1, 0.514 screens) | 8; <= 0.25 |
| `wave_shimmer_strips_area` (PASS) | at r 1,000 px: 6 strips 0.066 screens against the box 0.568: 0.116 | <= 0.6 |
| `wave_lights_swept_hulls` (PASS; a hull plane through the centre, 2 R_f out) | 0.321 while the front crosses (32 ms), 0.0 at 0.5 tau | >= 0.1; 0 |
| `compose_silhouette_cv` (PASS; median of 8 seeds) | CV at 0.3 / 0.6 L: fighter 0.323 / 0.327, capital 0.412 / 0.505, station 0.338 / 0.432; the single body 0.143 / 0.121 / 0.113 | >= 0.18 |
| `compose_silhouette_changes` (PASS) | correlation / RMS between the ages: fighter 0.755 / 0.263, capital 0.931 / 0.175, station 0.915 / 0.193 | <= 0.7 or >= 0.15 |
| `compose_not_grapes`, `compose_under_one_sun` (PASS, PASS) | largest blob 1.0 / 0.998 / 0.999; peak over the single body 0.898 / 0.898 / 0.898, mean lit 0.907 / 0.982 / 0.974 | >= 0.8; <= 1.4, 0.7-1.3 |
| `compose_births_spread`, `compose_jets_capital` (PASS, PASS) | N / jets: fighter 3 / 0, capital 4 / 1, station 5 / 1; born at 0: 1, by 0.15 L all; at L 0; jets 2.0:1 | |
| `compose_eye_inside_shared` (PASS) | own death (24 m): drawn 2, shared 1; close capital at 0.25 L: drawn 2, shared 1 | <= 4, shared |
| `compose_lod_floor` (PASS) | a budget of nothing: 2 drawn, jets 0, shared 1, 3 dropped | 2, 0 |
| `compose_box_holds_spheres` (PASS; 8 seeds x 3 tiers, a 1.6 x box) | lit pixels outside the box: 0 | 0 |
| `compose_clumps_survive`, `compose_no_white_region` (PASS, PASS) | blobs of >= 1 % of the lit pixels at 0.6 L: fighter 2.0, capital 2.0, station 2.0; widest white run at 0.3 / 0.6 L (core on) 0.0 / 0.0 / 0.0 R_f | >= 2; <= 0.3 |
| `wave_fire_not_grey` ramp terms, `wave_eye_in_plane_no_whiteout` (PASS) | mean chroma (0.5 tau, 0.2 / 0.9 L_w) 0.730,0.608,0.927, on the ramp 1.000,1.000,1.000; eye in the plane at 1.5 km: worst mean 1 - e^(-Y) 0.058 (P + 0 ms), worst share above 0.9 0.018 | >= 0.35, >= 0.9; <= 0.35, <= 0.4 |
| `compose_capital_structure` (FAIL; capital 1.5 km, 0.6 L) | blobs 2.0, lit share inside the outline 0.68; the fourth octave 258.3 us against three 227.3 us a frame-filling body: 1.349 a screen (pinned 1.35) | >= 3; <= 0.75 |
| `death_stage_cpu_32_deaths`, `death_big_stage_cpu` (PASS, FAIL) | 32 fighters 46.3 us (single body 43.8); the big set 86.6 us, 0.1095 a quad (repeats 53.0,45.2,46.1,46.3,49.4 / 83.0,76.7,86.6,87.2,87.2) (single body 82.1, no wave 81.9) | 50; 0.08 |
| `death_budget_capital_close_5120` re-run (PASS) | worst 403.8 us (capital_close_700ms, shed 9); the wave drawn on 6 of the 6 frames P .. P + L_w it is on screen (`wave_drawn_close_capital` PASS) | <= 450; >= 80 % |
| timing (5120x1440) | wave alone 252.5 us (1.0 screens drawn: 1.361 a screen), one octave 226.7 us (1.216), the box at a station's birth 251.3 us (1.354); compose factors N 2 / 3 / 4 / 5 0.688 / 0.786 / 0.857 / 0.938, shared N 4 0.747; eye inside N 4 282.1 us, eye inside 281.8 us | pinned 1.35 / 1.22; <= 300 |
| engine shimmer `wave_band_displaced_outward_sky_included`, `wave_zero_outside_band_front_hull_and_strips` (1920x1080) | band 17694 px, 17677 moved, 17584 outward, mean 3.96 px; sky band 8855, 8850 moved; beyond 0 / front hull 0 / outside 0 changed | >= 80 %, 95 %; 0 |
| engine shimmer `wave_band_displaced_outward_sky_included`, `wave_zero_outside_band_front_hull_and_strips` (5120x1440) | band 31153 px, 31068 moved, 30424 outward, mean 5.404 px; sky band 15582, 15543 moved; beyond 0 / front hull 0 / outside 0 changed | >= 80 %, 95 %; 0 |
| engine shimmer `wave_shimmer_strips_timing` (5120x1440, r 1000.0 px) | strips 0.0836 ms against the box 0.1675 ms: 0.499 (copy 3264480 / 3263040 px) | <= 0.6 |

Three official runs of this tree: the second failed only `compose_capital_structure`, the CPU rows passing (32 fighters
40.4 us, repeats 40.4-40.5; the big set 57.6 us, 0.0728 a quad, repeats 57.5-61.1) and the bolt budget 97.7 us; the
third (the record) failed `death_big_stage_cpu` (repeats 76.7-87.2 us) and `bolt_budget_512_bolts_20_muzzles_5120`
(106.0 us against 100, the bolt sources unchanged) with the host slow throughout. `compose_capital_structure`: the
capital holds 2 blobs at 0.6 L, not 3 (lit share inside the outline 0.68 passes); faster sub-burst erosion (pace 1.4-2.8)
left 2 and broke the eye-inside (0.779), clumps (1.5) and the fighter's size (0.939). Hull `heavy_capital_64`
(class=heavy case=capital_close_64_hits): 351.1 / 1139.0 us at 1080p / 5120x1440 against 351.0 / 1143.5 before (within
1 %, octaves 2 on that frame). Grids (local, `stage-dump-wave/v3/`): the wave a thick textured ring on the ramp with
holes carved black, its high-T band still reads beige in the dump's clamped sRGB; from above the passing front is a dim
grey wash, no white wall; the composed capital body shows the fourth octave's grain at 1900 / 2700 ms, the large
right-hand lobe at 2700 / 3900 ms stays smooth. Not flown.


### v4: the kill (composed body off), the wave's ramp lowered, flame gaps (2026-11-07, same dirty tree)

The coordinator's kill criterion on the v3 frames, explosion-look.md D46-D49: `compose_enabled` false (the single body with
the fourth octave), the wave on a lower ramp with soft, radially stretched holes. The acceptance run waited for the game
(`tasklist.exe` showed no X3AP.exe at the first check) and passed: D3D 318 checks, 0 failed; hook 147 / 0; the engine
shimmer record above (its sources unchanged). One run before it lost the device mid-run (every GPU row 0.8-0.9 us, the
log cut at `death_big_no_pop_at_birth`); retried once. Table from `ledger_rows.py --markdown --wave` over that record:

| Gate | Result [M] | Bound |
| --- | --- | --- |
| `compose_*` | skipped: `boom::compose_enabled` false (the kill, 2026-11-07 v4) | |
| `wave_program_attached` (PASS) | wave_ps 614 slots, compose_ps - slots, the device's 4096 | created |
| `wave_radius_law` (PASS; capital 5 km face-on) | the lit bins' median edge radius over r(t) at 0.1 / 0.5 / 1.0 tau: 1.017 / 1.004 / 1.012 (the median outer 10 % radius 393.2 / 775.5 / 856.3 px against r(t) + 1.52 w_e 362.2 / 744.7 / 823.9: the tongues) | 5 % |
| `wave_sheet_thick`, `wave_hot_edge_trailing_sheet`, `wave_flame_tongues` (PASS, PASS, PASS; 5 km, 0.5 tau) | band + sheet 0.964 R_f at 10 % of each ray's peak; edge over the sheet's mean 0.2-0.6 R_f behind 1.63, the sheet at 0.7 R_f behind 0.1118 of the peak; the lit bins' outer radii spread 0.552 R_f | >= 0.6; >= 1.4, >= 0.01; >= 0.2 |
| `wave_never_brightest` (PASS) | wave peak 0.878 / 0.473 / 0.139 against the body's 4.98 / 3.91 / 2.57; after L_w 0, other tiers 0 | <=; 0; 0 |
| `wave_thick_not_a_line` (PASS) | face-on band 111.0 px (need 23.0); edge-on bar from above 150.0 px (need 95.0) | >= 0.2 R_f f / z |
| `wave_broken_circumference` (PASS) | lit share of 64 bins 0.719, max / min 20.01 | 0.40-0.85; >= 2 |
| `wave_fire_not_grey` (PASS) | edge / band 1.46, band R / G 1.84, hue 0.75 / 0.25 at 0.2 / 0.9 L_w, chroma 0.707 / 0.932 | >= 1.3; >= 1.3; >= 0.5 / <= 0.35; >= 0.2 |
| `wave_refraction_strips_present_and_capped` (PASS; capital 1.5 km) | at birth 8 strips, 0.123 screens, request 1; at P + 200 ms wave 1, strips 0 (capped 1, 0.514 screens) | 8; <= 0.25 |
| `wave_shimmer_strips_area` (PASS) | at r 1,000 px: 6 strips 0.066 screens against the box 0.568: 0.116 | <= 0.6 |
| `wave_lights_swept_hulls` (PASS; a hull plane through the centre, 2 R_f out) | 0.321 while the front crosses (32 ms), 0.0 at 0.5 tau | >= 0.1; 0 |
| `wave_fire_not_grey` ramp terms, `wave_eye_in_plane_no_whiteout` (PASS) | mean chroma (0.5 tau, 0.2 / 0.9 L_w) 0.797,0.734,0.934, on the ramp 1.000,1.000,1.000; eye in the plane at 1.5 km: worst mean 1 - e^(-Y) 0.056 (P + 0 ms), worst share above 0.9 0.016 | >= 0.35, >= 0.9; <= 0.35, <= 0.4 |
| `death_stage_cpu_32_deaths`, `death_big_stage_cpu` (PASS, PASS) | 32 fighters 41.5 us (single body 41.5); the big set 59.1 us, 0.0747 a quad (repeats 41.5,41.5,41.5,41.6,43.2 / 59.3,59.1,58.8,58.7,76.8) (single body 59.7, no wave 56.1) | 50; 0.08 |
| `death_budget_capital_close_5120` re-run (PASS) | worst 353.8 us (capital_close_700ms, shed 9); the wave drawn on 6 of the 6 frames P .. P + L_w it is on screen (`wave_drawn_close_capital` PASS) | <= 450; >= 80 % |
| timing (5120x1440) | wave alone 259.4 us (1.0 screens drawn: 1.401 a screen), one octave 231.3 us (1.242), the box at a station's birth 261.0 us (1.410); compose factors N 2 / 3 / 4 / 5 None / None / None / None, shared N 4 None; eye inside N 4 282.7 us, eye inside 279.5 us | pinned 1.35 / 1.22; <= 300 |
| engine shimmer `wave_band_displaced_outward_sky_included`, `wave_zero_outside_band_front_hull_and_strips` (1920x1080) | band 17694 px, 17677 moved, 17584 outward, mean 3.96 px; sky band 8855, 8850 moved; beyond 0 / front hull 0 / outside 0 changed | >= 80 %, 95 %; 0 |
| engine shimmer `wave_band_displaced_outward_sky_included`, `wave_zero_outside_band_front_hull_and_strips` (5120x1440) | band 31153 px, 31068 moved, 30424 outward, mean 5.404 px; sky band 15582, 15543 moved; beyond 0 / front hull 0 / outside 0 changed | >= 80 %, 95 %; 0 |
| engine shimmer `wave_shimmer_strips_timing` (5120x1440, r 1000.0 px) | strips 0.0836 ms against the box 0.1675 ms: 0.499 (copy 3264480 / 3263040 px) | <= 0.6 |

The CPU rows (median of 5 repeats): 32 fighters 41.5 us (41.5-43.2), the big set 59.1 us, 0.0747 a quad (58.7-76.8); the
bolt budget 99.4 us; hull `heavy_capital_64` 353.8 / 1132.4 us (351.0 / 1143.5 before). The later boom-only dump run
measured the big set at 77.5-77.7 us (failing 0.08 a quad on timing alone; the game's absence not checked for that run).
`death_budget_5120`'s eye-inside fighter rose to 309.8 us with the fourth octave on the single body filling the screen;
an eye-inside body keeps three octaves now (279.5 us). Grids (local, `stage-dump-wave/v4/`, capital broadside, above,
far 5 km, station 3 km, fighter 600 m): the single body broken and lobed to 3900 ms with the fourth octave's grain; the
wave a thick ring with soft radial gaps, its mid-intensity orange still reading tan in the dump's clamped sRGB. Not flown.


### Review fixes (2026-11-07, same dirty tree)

The review's seven findings, explosion-look.md D50-D51. The acceptance run waited for the game (`tasklist.exe`: no X3AP.exe
at the first check): D3D 320 checks, 3 failed, all on timing alone; hook 147 / 0. The engine shimmer sources are
unchanged (its record above stands; the glue's strip room lives in the proxy). Table from `ledger_rows.py --markdown
--wave` over that record:

| Gate | Result [M] | Bound |
| --- | --- | --- |
| `compose_*` | skipped: `boom::compose_enabled` false (the kill, 2026-11-07 v4) | |
| `wave_program_attached` (PASS) | wave_ps 614 slots, compose_ps - slots, the device's 4096 | created |
| `wave_radius_law` (PASS; capital 5 km face-on) | the lit bins' median edge radius over r(t) at 0.1 / 0.5 / 1.0 tau: 0.998 / 1.0 / 1.001 (the median outer 10 % radius 390.6 / 765.3 / 844.8 px against r(t) + 1.52 w_e 362.2 / 744.7 / 823.9: the tongues) | 5 % |
| `wave_sheet_thick`, `wave_hot_edge_trailing_sheet`, `wave_flame_tongues` (PASS, PASS, PASS; 5 km, 0.5 tau) | band + sheet 0.924 R_f at 10 % of each ray's peak; edge over the sheet's mean 0.2-0.6 R_f behind 2.85, the sheet at 0.7 R_f behind 0.0556 of the peak; the lit bins' outer radii spread 0.551 R_f | >= 0.6; >= 1.4, >= 0.01; >= 0.2 |
| `wave_never_brightest` (PASS) | wave peak 0.878 / 0.473 / 0.139 against the body's 4.98 / 3.91 / 2.57; after L_w 0, other tiers 0 | <=; 0; 0 |
| `wave_thick_not_a_line` (PASS) | face-on band 106.5 px (need 23.0); edge-on bar from above 161.0 px (need 95.0) | >= 0.2 R_f f / z |
| `wave_broken_circumference` (PASS) | lit share of 64 bins 0.797, max / min 20.01 | 0.40-0.85; >= 2 |
| `wave_fire_not_grey` (PASS) | edge / band 1.51, band R / G 1.85, hue 0.75 / 0.25 at 0.2 / 0.9 L_w, chroma 0.706 / 0.931 | >= 1.3; >= 1.3; >= 0.5 / <= 0.35; >= 0.2 |
| `wave_refraction_strips_present_and_capped` (PASS; capital 1.5 km) | at birth 8 strips, 0.123 screens, request 1; at P + 200 ms wave 1, strips 0 (capped 1, 0.514 screens) | 8; <= 0.25 |
| `wave_shimmer_strips_area` (PASS) | at r 1,000 px: 6 strips 0.066 screens against the box 0.568: 0.116 | <= 0.6 |
| `wave_lights_swept_hulls` (PASS; a hull plane through the centre, 2 R_f out) | 0.297 while the front crosses (32 ms), 0.0 at 0.5 tau | >= 0.1; 0 |
| `wave_fire_not_grey` ramp terms, `wave_eye_in_plane_no_whiteout` (PASS) | mean chroma (0.5 tau, 0.2 / 0.9 L_w) 0.800,0.732,0.933, on the ramp 1.000,1.000,1.000; eye in the plane at 1.5 km: worst mean 1 - e^(-Y) 0.088 (P + 50 ms), worst share above 0.9 0.016 | >= 0.35, >= 0.9; <= 0.35, <= 0.4 |
| `death_stage_cpu_32_deaths`, `death_big_stage_cpu` (FAIL, FAIL) | 32 fighters 58.6 us (single body 58.5); the big set 79.1 us, 0.1 a quad (repeats 58.7,58.6,58.4,58.6,58.3 / 79.3,79.3,79.1,79.1,79.0) (single body 78.9, no wave 75.5) | 50; 0.08 |
| `death_budget_capital_close_5120` re-run (PASS) | worst 351.7 us (capital_close_700ms, shed 9); the wave drawn on 6 of the 6 frames P .. P + L_w it is on screen (`wave_drawn_close_capital` PASS) | <= 450; >= 80 % |
| timing (5120x1440) | wave alone 259.4 us (1.0 screens drawn: 1.399 a screen), one octave 229.8 us (1.233), the box at a station's birth 260.2 us (1.404); compose factors N 2 / 3 / 4 / 5 None / None / None / None, shared N 4 None; eye inside N 4 281.0 us, eye inside 278.7 us | pinned 1.4 / 1.24; <= 300 |
| engine shimmer `wave_band_displaced_outward_sky_included`, `wave_zero_outside_band_front_hull_and_strips` (1920x1080) | band 17694 px, 17677 moved, 17584 outward, mean 3.96 px; sky band 8855, 8850 moved; beyond 0 / front hull 0 / outside 0 changed | >= 80 %, 95 %; 0 |
| engine shimmer `wave_band_displaced_outward_sky_included`, `wave_zero_outside_band_front_hull_and_strips` (5120x1440) | band 31153 px, 31068 moved, 30424 outward, mean 5.404 px; sky band 15582, 15543 moved; beyond 0 / front hull 0 / outside 0 changed | >= 80 %, 95 %; 0 |
| engine shimmer `wave_shimmer_strips_timing` (5120x1440, r 1000.0 px) | strips 0.0836 ms against the box 0.1675 ms: 0.499 (copy 3264480 / 3263040 px) | <= 0.6 |

Timing-only failures in that record: `death_stage_cpu_32_deaths` 58.6 us (repeats 58.3-58.7; the single body alone 58.5,
the composed body off), `death_big_stage_cpu` 79.1 us, 0.100 a quad (79.0-79.3), `bolt_budget_512_bolts_20_muzzles_5120`
155.1 us (the bolt sources unchanged; 97.3-99.4 us in the two runs before). The same build passed them at 41.5 / 59.1 /
99.4 us earlier the same day; the five repeats agree within 0.4 us inside a run while the runs differ by 40 %: the host's
clock state between runs, not the stage. Also measured: `wave_break_follows_seed` correlation -0.470; the wave refused
(fault bit 10): wave off with `wave_program_create`, 7 body boxes drawn, its wave not; after the Reset the capital's wave
drawn (`wave_enabled` 1); the fourth octave on a body seen from outside 27.4 against 27.9 us over 0.27 screens (weight
1.145 against 1.18); the wave's weights 1.399 / 1.233 / 1.404 against 1.40 / 1.24 (+0.03); hull `heavy_capital_64`
348.1 / 1132.4 us; the octave hysteresis 422242222244 as wanted. Grids (local, `stage-dump-wave/v5/`, far 5 km and
broadside): each wave its own break and gaps; a horizontal step through the centre is visible in the far frames at 1540 /
1620 ms. Traced (the 1620 ms frame's rows): the centre row differs from its neighbours only across the dying hull's
width (|x| 151-196 px: the scene model's panel line) and in one pixel; in the band the rows agree (e.g. 73 / 73 / 73
from 324 to 344 px). The step is the scene model's hull, which the ring passes behind at 1540 ms (the lane hides the
wave there), not a strip seam. Gate `wave_no_strip_seam` (the band's mean 2 px either side of the eight strip
boundaries and of the angle's wrap at pi, 5 km, 0.5 tau, eight strips drawn): worst 0.0144 of the peak against 0.1
[M]. On the quiet host (no game, the bolt row back to 98.1 us) the CPU rows still fail: 32 fighters repeats 44.7,
57.8, 57.6, 57.9, 57.6 us (median 57.6), the big set 78.2-78.6 us (0.099 a quad); bounds untouched. v6 far grid local
(`stage-dump-wave/v6/`). Not flown.


### v7: the wave as the fireball's material, the hard edge (2026-11-07, worktree on `effects` `a74671f26`, uncommitted)

The user on the lab: the installed wave a beige / olive ring with black soot blots and a hard vertical cut on its right,
"not fire"; the fireball beside it reads as fire. Built as explosion-look.md D52-D58: the band and sheet are now the
fireball's noise, erosion and lobes on (the angle, the distance behind the shifted front), never transparent inside the
band; the rim a thin T 0.9 line, the material 0.55 -> 0.4 -> 0.25, dim pixels cooled; E 1.6. The hard edge had two
causes: the lab's unframed vanilla inset (a black third of the width over the top right; framed and a quarter now) and
v6's sheet lit at full gain ahead of the front up to the window and the strips' reach (the ring's outer cut, lab and
native). Acceptance run (`tasklist.exe`: no X3AP / X3FL before it; 20:48-20:50 including the lock): D3D 323 checks, 1
failed (`death_big_stage_cpu`, the CPU row on today's host; the other CPU row passed at 42.1 us); hook 147 / 0. The
engine shimmer sources are unchanged (its record above stands). Table from `python3
verification/results/explosion-look/ledger_rows.py --markdown --wave` over that record:

| Gate | Result [M] | Bound |
| --- | --- | --- |
| `compose_*` | skipped: `boom::compose_enabled` false (the kill, 2026-11-07 v4) | |
| `wave_program_attached` (PASS) | wave_ps 727 slots, compose_ps - slots, the device's 4096 | created |
| `wave_radius_law` (PASS; capital 5 km face-on) | the lit bins' median edge radius over r(t) at 0.1 / 0.5 / 1.0 tau: 1.001 / 1.0 / 1.003 (the median outer 10 % radius 363.6 / 741.8 / 821.2 px against r(t) + 1.52 w_e 362.2 / 744.7 / 823.9: the tongues) | 5 % |
| `wave_sheet_thick`, `wave_hot_edge_trailing_sheet`, `wave_flame_tongues` (PASS, PASS, PASS; 5 km, 0.5 tau) | band + sheet 0.629 R_f at 10 % of each ray's peak; edge over the sheet's mean 0.2-0.6 R_f behind 2.65, the sheet at 0.7 R_f behind 0.032 of the peak; the lit bins' outer radii spread 0.334 R_f | >= 0.6; >= 1.4, >= 0.01; >= 0.2 |
| `wave_never_brightest` (PASS) | wave peak 1.406 / 0.756 / 0.225 against the body's 4.98 / 3.91 / 2.57; after L_w 0, other tiers 0 | <=; 0; 0 |
| `wave_thick_not_a_line` (PASS) | face-on band 72.5 px (need 23.0); edge-on bar from above 145.0 px (need 95.0) | >= 0.2 R_f f / z |
| `wave_broken_circumference` (PASS) | the 64 bins' band means max / min 19.92, CV 0.558; the dimmest bin's peak 0.149 of the ring's (lit share at 10 / 30 / 50 % 1.0 / 0.922 / 0.859) | >= 2, >= 0.25; >= 0.05 |
| `wave_no_hard_edge` (PASS; 5 km, 0.5 tau) | along the band at 36 angles: 2 drops past 50 % over 2 degrees, the worst 0.25-degree step 0.204 of the brighter end (0 hard); outward of 35 lit rays' peaks: 0 rises, the worst 1-px fall 0.283 of the ray's peak | <= 0.35; >= 18, 0, <= 0.3 |
| `wave_fire_hue` (PASS; 5 km, 0.5 tau, the dump's sRGB) | 301133 lit pixels: mean chroma 0.717, hue in 10-40 degrees 1.0, mean hue 23.9 | >= 0.5; >= 0.8 |
| `wave_fire_not_grey` (PASS) | edge / band 1.76, band R / G 3.31, hue 0.5 / 0.25 at 0.2 / 0.9 L_w, chroma 0.891 / 0.934 | >= 1.3; >= 1.3; >= 0.5 / <= 0.35; >= 0.2 |
| `wave_refraction_strips_present_and_capped` (PASS; capital 1.5 km) | at birth 8 strips, 0.123 screens, request 1; at P + 200 ms wave 1, strips 0 (capped 1, 0.514 screens) | 8; <= 0.25 |
| `wave_shimmer_strips_area` (PASS) | at r 1,000 px: 6 strips 0.066 screens against the box 0.568: 0.116 | <= 0.6 |
| `wave_lights_swept_hulls` (PASS; a hull plane through the centre, 2 R_f out) | 0.529 while the front crosses (32 ms), 0.0 at 0.5 tau | >= 0.1; 0 |
| `wave_fire_not_grey` ramp terms, `wave_eye_in_plane_no_whiteout` (PASS) | mean chroma (0.5 tau, 0.2 / 0.9 L_w) 0.917,0.909,0.940, on the ramp 1.000,1.000,1.000; eye in the plane at 1.5 km: worst mean 1 - e^(-Y) 0.108 (P + 50 ms), worst share above 0.9 0.016 | >= 0.35, >= 0.9; <= 0.35, <= 0.4 |
| `death_stage_cpu_32_deaths`, `death_big_stage_cpu` (PASS, FAIL) | 32 fighters 42.1 us (single body 42.7); the big set 69.1 us, 0.0874 a quad (repeats 41.1,42.1,41.8,56.7,54.4 / 63.9,69.1,63.4,77.4,74.0) (single body 77.1, no wave 69.2) | 50; 0.08 |
| `death_budget_capital_close_5120` re-run (PASS) | worst 351.3 us (capital_close_700ms, shed 9); the wave drawn on 7 of the 7 frames P .. P + L_w it is on screen (`wave_drawn_close_capital` PASS) | <= 450; >= 80 % |
| timing (5120x1440) | wave alone 315.0 us (1.0 screens drawn: 1.712 a screen), the sheet's octave shed 282.6 us (1.529), the box at a station's birth 312.6 us (1.698); compose factors N 2 / 3 / 4 / 5 None / None / None / None, shared N 4 None; eye inside N 4 281.2 us, eye inside 280.7 us | pinned 1.71 / 1.53; <= 300 |
| engine shimmer `wave_band_displaced_outward_sky_included`, `wave_zero_outside_band_front_hull_and_strips` (1920x1080) | band 17694 px, 17677 moved, 17584 outward, mean 3.96 px; sky band 8855, 8850 moved; beyond 0 / front hull 0 / outside 0 changed | >= 80 %, 95 %; 0 |
| engine shimmer `wave_band_displaced_outward_sky_included`, `wave_zero_outside_band_front_hull_and_strips` (5120x1440) | band 31153 px, 31068 moved, 30424 outward, mean 5.404 px; sky band 15582, 15543 moved; beyond 0 / front hull 0 / outside 0 changed | >= 80 %, 95 %; 0 |
| engine shimmer `wave_shimmer_strips_timing` (5120x1440, r 1000.0 px) | strips 0.0836 ms against the box 0.1675 ms: 0.499 (copy 3264480 / 3263040 px) | <= 0.6 |

The close capital (5120x1440): the sheet's third octave shed on every wave frame (step 0a), the wave itself never shed,
drawn on 7 of 7 on-screen frames, worst frame 351.3 us (capital_close_700ms; P + 0 / 100 / 200 / 300 ms 207.8 / 219.7 /
279.2 / 342.8 us). Program 727 slots (614 in v6), 3,121 words. Development runs (boom only, not the record) before the
pins: the first v7 program's outward fall 0.436 of a ray's peak where the rim is broken (the material began over 0.05
R_f behind the front: its own edge); the material now falls off ahead as the rim does (0.283). Lab (headless Chromium,
`shot_lab.py`, the capital broadside at 1540 / 1620 / 1900 / 2700 ms and far 5 km) and the native dump grids (far 5 km,
broadside, station 3 km at both sizes) judged: an orange-red broken ring with clumps, dark-red gaps and a thin hot rim
in the body's palette, no beige band, no black blots, no cut; frames local (`stage-dump-wave/v7/`, `wave7/lab-v7-*`).
Late (P + 1,200 ms, x 0.75) the faded ring reads as a dim brown band of even width at 5 km. Also: host tests `test_weapon_effects_*` 101, 2 failing on the record's CPU row alone; programs, lab, schema and generate `--check` PASS; AP `build-wave2` (clean, d3d9.dll sha256 f06cab8544aa5e9a...) and FL `build-wave2-fl` 0 warnings; `check_no_x87.py --edition ap` PASS (1181 reachable functions); `manage.py launch --game-dir /mnt/c/X3 --config --dry-run` exit 0. Not flown.

## Explosions: radiance, rays and sparks

2026-11-07, committed as `2eb405fc9` on `effects` `d3e7698bf`. Spec: [explosion-look.md](../architecture/explosion-look.md)
section 14 (the user's three vanilla frames). First record (`2eb405fc9`, with the judging dump): D3D 322 checks, 1
failed: `death_stage_cpu_32_deaths` 59.3 us with 1164 quads against the parent `d3e7698bf`'s 42.1 us with 588 quads on
the same host, a regression of this change (the sparks and rays doubled the sprite quads), not the host's state.

Review fixes (2026-11-07, on `2eb405fc9`): the spark loop visits only the indices the half-count shed keeps, takes its
per-set invariants (the clock's scales, the launch centre's two views, the lengths' reference) out of the per-spark
work and divides by none; the on-screen spark count moved from the stage to the fixture (from the drawn quads); the
ray loop steps over the shed indices. Bound unchanged (50 us). The core's ramp is max(`core_ramp_ms`, the record's
three-frame floor) and the ray flicker's period at least the frame step (SETA); the spark budget reserves a big tier's
secondaries' stars through the pre-roll; comments say the fragments keep their full count through three halvings.
`death_exposure_meter_shift` measures the production meter's shift (no head-radiance claim) and gains a fighter at
120 m, where the highlight limit acts: 0.386 EV over the dim scene (at 400 ms, downwards), 0 over the lit one, bound
0.5 EV. Black sky is excluded from the gate (coordinator decision 2026-11-07) and kept as an informational value
(`black_shift_ev` 1.300 at 600 / 1000 ms): the meter scores an empty frame 0 EV and climbs to its +1.3 EV cap on any
signal, so that row measures the meter's black policy, not the death's highlight pull. Record: `run_weapon_effects.py --host native` under the X3 lock, no dump (game
absent; the Windows screensaver killed first: with it up the device was lost mid-run, then refused): D3D 329 checks
(the 322 plus the seven fault and Reset rows a dump run skips), 0 failed; hook 147 / 0. Table from
`python3 verification/results/explosion-look/ledger_rows.py --markdown --rays` over that record:

| Gate | Result [M] | Bound |
| --- | --- | --- |
| `death_core_radiance` (PASS; fighter 600 m, 8 seeds) | median radiance inside 0.2 R(t) at 0.05 L 6.984 (seeds 4.492..6.984); `death_body_white_hot_core` median at 0.1 L 5.273 | >= 6 |
| `death_rays_starburst` (PASS; 5120x1440, 4 seeds) | angular peaks at 1.5 R(t), 0.1 L, 300 m: 39, 34, 31, 38 for 38, 34, 30, 37 rays; at 0.6 L 0; 600 m (recorded) 39, 36, 30, 40 | >= 24; <= 2 |
| `death_rays_differ` (PASS) | two deaths' 360-bin profiles at 0.1 L correlate 0.101 | < 0.3 |
| `death_sparks_dense` (PASS; fighter 600 m, 0.3 L) | 221 sparks, 217 drawn, 217 heads on screen, 173 local maxima in the ember part; lengths 12.8 +- 12.7 px (std / mean 0.997) | >= 100; >= 0.4 |
| `death_embers_hidden_in_body`, `death_embers_lengths_and_widths`, `death_embers_random_and_cooling` (PASS, PASS, PASS) | 41 at birth, 207 of 221 once the births are out (0.05 L); widths 1.00..1.99 px; seed correlation -0.141, T 0.75 / 0.5 / 0.25 at 0.3 / 0.7 / 1.1 L, farthest 219.5 px | > 0, >= 80 %; std >= 0.4 mean; < 0.5, >= 0.75 / >= 0.5 / <= 0.3 |
| `death_exposure_meter_shift` (PASS; the production meter's fresh target against the dim and lit scenes alone; fighter 300 m and 120 m, 5120x1440, Auto <= +1.3 EV, gamma 2.2) | worst shift 0.386 EV over dim / lit (black sky informational, not gated: an empty frame scores 0 EV and any signal lifts it to the cap; 1.3 EV); 300 m: black / dim 0.15 / lit 0.5 worst 0.000 / 0.000 / 0.000 EV (frame peaks 35.06 / 32.91 / 19.83 / 8.1 / 3.65 / 2.38); 120 m: black / dim 0.15 / lit 0.5 worst 1.300 / 0.386 / 0.000 EV (frame peaks 19.25 / 16.06 / 9.9 / 4.63 / 3.03 / 2.33) | <= 0.5 EV |
| `death_no_pop_at_birth`, `death_big_no_pop_at_birth` (PASS, PASS) | worst frame-to-frame peak rise corvette_1500m 1.267, capital_1500m 1.288 | <= 2 |
| `death_rays_sparks_budget_5120` (PASS) | fighter 600 m 19.7 / 19.7 / 20.4 us at 0.1 / 0.05 / 0.3 L; 20 fighters 38.8 / 38.1 / 41.1 us (spark halvings 2: 787 drawn, 2624 shed, 322 rays); fence 11.0 us | <= 60; <= 150 |
| `death_budget_capital_close_5120` (PASS) | worst 347.7 us (capital_close_600ms, shed 9) | <= 450 |
| rays alone (5120x1440) | fighter 300 m 15.0 us (28 quads), close capital 19.4 us (28 quads), the fence 11.0 us | |
| `death_stage_cpu_32_deaths`, `death_big_stage_cpu` (PASS, PASS) | 32 fighters 45.0 us, 1164 quads (repeats 45.1,45.1,44.8,45.0,45.0; the parent `d3e7698bf` 42.1 us, 588 quads); the big set 35.9 us, 0.0617 a quad | 50; 0.08 |

The ray sheet (one quad a death, the three nearest rays a pixel; `verification/results/explosion-look/ray-sheet/slots.py`,
`slots_out.txt`): 464 instruction slots against the sprite program's 178 -> 201 with the ray kind [M]; per-ray quads
chosen (section 14 item 2). Development runs (boom only, not the record) before the pins: the rays' bases stacked at the
centre (capital peak rise 3.91 x at P), the sparks' heads on the launch ring (corvette ember peak 55.8 at birth), the
big body's core over its secondaries (2.09 x): fixed by the ray start, the spark births spread over 0.05 L with a fade-in,
and the core's ramp (now 1.29 x). Frames judged (local): `stage-dump-rays/` (fighter 300 m and 600 m, the nested kill, the
capital broadside P - 50 .. P + 1,200 ms; clipped engine space at 5120x1440 and AgX from the 1080p frame) and
`stage-dump-rays/lab/` (`boom/fighter-m5-300m-vanilla-compare` at 50 / 150 / 400 / 900 / 1,500 ms beside the vanilla
frame): a blown white core with a hard starburst to 150 ms, sparks around the eroding body from 400 ms, a wide yellow-orange
spray at 900 ms; the capital's rays fade by its 0.3 L. First record's checks: host tests `test_weapon_effects_*` 101,
2 failing on the record's CPU row alone; programs, lab, schema and generate `--check` PASS; AP `build-rays` (clean,
d3d9.dll sha256 ec08dbf0d6968e27...) and FL `build-rays-fl` (af664635b7c177ab...) 0 warnings; `check_no_x87.py
--edition ap` PASS (1181 reachable functions); `manage.py launch --game-dir /mnt/c/X3 --config --dry-run` exit 0. Review
fixes: `test_weapon_effects_hook` and `_lab` 59, all pass; programs `--check` PASS; AP
`build-rays` 0 warnings (d3d9.dll sha256 b049f07f490ab7f5...), `check_no_x87.py --edition ap` PASS (1181 reachable
functions). Not flown.


**Soft rays and bigger sparks** (2026-11-07, worktree on `7650e6828`; the user on the corvette: rays "not so
pronounced/thin ... look into our sun", sparks "too thin"; law in [explosion-look.md](../architecture/explosion-look.md)
section 14 items 2 and 3). Rays: the sun's spokes, an odd 9-15 a death, golden-ratio lengths 0.4-2.0 R_f, a Gaussian
across of FWHM max(6 px at 1080 rows, 0.065 L (1 + u / 2)), e^(-u) (1 - u)^2 along faded in over u < 0.15, `ray_e`
0.4 x 0.6-1.2, flicker 150-300 ms at depth 0.3, heat 0.95 -> 0.8. Sparks: heads a 3-5 px Gaussian (x min(1, R_f / 28.8
px), floor 3 px) in a halo, tails 2-3 px tapering to 1 px, lengths 6-36 px and long streaks 60-120 px. Sprite pixel
program 201 -> 235 slots (895 -> 1,022 words), vertex 410 -> 446 words [M] (`ray-sheet/slots.py`). The starburst gate
counts the peaks at 0.5 R(t), where every ray is lit (with the sun's along-profile the rays shorter than about 1.4 R(t)
end before 1.5 R(t), which now only asks for the long ones, >= 3); the spark gate moved to the ember part alone (a
fragment's head is now as wide as a spark's). Record: `run_weapon_effects.py --host native` under the X3 lock with the
judging dump: D3D 331 checks, 2 failed; hook 147 / 0. `death_exposure_meter_shift` fails at 1.13 EV (the 120 m fighter
over the dim scene at 400 ms; was 0.386): a first record with the spark halo at 0.25 and `ray_e` 0.6 gave the same
1.13 to three digits, so neither the halo (now 0.12) nor the rays move it; the Gaussian head puts its full `ember_e` on a
pixel where the 1-2 px streak head sampled lower, and the p99 tile maximum follows the head's peak [I]. Left for the
coordinator: the head radiance (not changed, per the brief). `death_big_stage_cpu` 48.0 us at 0.0945 us a quad (bound
0.08; parent 35.9) with its no-wave path up by the same third (33.2 -> 44.6 us), read as host load, not verified; the
`fine4_weight` row 1.252 against its pinned 1.18 (a body-only wave timing this change does not touch) likewise.
Frames judged (local): `stage-dump-rays-soft/` (fighter 300 / 600 m, the nested kill, the capital broadside; AgX from the
1080p frame) and `stage-dump-rays-soft/lab/` (corvette 1,500 m and fighter 300 m at 50 / 150 / 400 ms, three rounds):
the rays read as faint soft spokes off the glow; the thin radial needles at 600 m at birth are the long spark streaks
(the lab with sparks off loses them, with rays off keeps them). The lab now draws light over the sky's background
rather than replacing it (a dim soft ray read darker than the sky). Checks: programs `--check` PASS; host tests
`test_weapon_effects_hook` and `_lab` 59, 3 failing on the record's meter, CPU and `fine4_weight` rows; AP `build-rays`
0 warnings (d3d9.dll sha256 ecec4dd2b041606a...), `check_no_x87.py --edition ap` PASS (1181 reachable functions).
Table from `python3 verification/results/explosion-look/ledger_rows.py --markdown --rays`:

| Gate | Result [M] | Bound |
| --- | --- | --- |
| `death_core_radiance` (PASS; fighter 600 m, 8 seeds) | median radiance inside 0.2 R(t) at 0.05 L 6.984 (seeds 4.492..6.984); `death_body_white_hot_core` median at 0.1 L 5.273 | >= 6 |
| `death_rays_starburst` (PASS; 5120x1440, 4 seeds) | angular peaks at 0.1 L, 300 m: at 0.5 R(t) 11, 11, 9, 14 for 11, 11, 9, 13 rays drawn; at 1.5 R(t) 6, 7, 6, 8; at 0.6 L 0; 600 m (recorded) 8, 14, 10, 16 / 6, 7, 5, 8 | drawn +- 1; >= 3; <= 2 |
| `death_rays_soft` (PASS; 300 m, 0.1 L) | median FWHM across the rays at 1.0 R(t) 7.03 px at 1080 rows (37 rays; per seed 7.38, 6.68, 6.7, 7.33); the worst ray peak over the death under it at 0.5 R(t) 0.123 (per seed 0.033, 0.123, 0.037, 0.029) | >= 4; <= 0.4 |
| `death_rays_differ` (PASS) | two deaths' 360-bin profiles at 0.1 L correlate -0.033 | < 0.3 |
| `death_sparks_dense` (PASS; fighter 600 m, 0.3 L) | 221 sparks, 217 drawn, 217 heads on screen, 113 local maxima in the ember part; lengths 19.7 +- 19.0 px (std / mean 0.964) | >= 100; >= 0.4 |
| `death_spark_heads` (PASS; fighter 600 m, 1080p, 0.3 L) | median head FWHM 4.46 px at 1080 rows over 113 image heads (the narrowest of the row, column and diagonal half widths) | >= 3 |
| `death_embers_hidden_in_body`, `death_embers_lengths_and_widths`, `death_embers_random_and_cooling` (PASS, PASS, PASS) | 41 at birth, 207 of 221 once the births are out (0.05 L); widths 3.00..4.98 px; seed correlation -0.16, T 0.75 / 0.5 / 0.25 at 0.3 / 0.7 / 1.1 L, farthest 221.5 px | > 0, >= 80 %; std >= 0.4 mean; < 0.5, >= 0.75 / >= 0.5 / <= 0.3 |
| `death_exposure_meter_shift` (FAIL; the production meter's fresh target against the dim and lit scenes alone; fighter 300 m and 120 m, 5120x1440, Auto <= +1.3 EV, gamma 2.2) | worst shift 1.13 EV over dim / lit (black sky informational, not gated: an empty frame scores 0 EV and any signal lifts it to the cap; 1.3 EV); 300 m: black / dim 0.15 / lit 0.5 worst 0.000 / 0.000 / 0.000 EV (frame peaks 50.97 / 51.41 / 30.05 / 12.29 / 5.54 / 2.72); 120 m: black / dim 0.15 / lit 0.5 worst 1.300 / 1.130 / 0.090 EV (frame peaks 24.23 / 21.19 / 13.16 / 5.65 / 3.65 / 2.57) | <= 0.5 EV |
| `death_no_pop_at_birth`, `death_big_no_pop_at_birth` (PASS, PASS) | worst frame-to-frame peak rise corvette_1500m 1.405, capital_1500m 1.23 | <= 2 |
| `death_rays_sparks_budget_5120` (PASS) | fighter 600 m 23.1 / 22.2 / 22.4 us at 0.1 / 0.05 / 0.3 L; 20 fighters 49.3 / 48.0 / 51.4 us (spark halvings 2: 787 drawn, 2624 shed, 250 rays); fence 11.1 us | <= 60; <= 150 |
| `death_budget_capital_close_5120` (PASS) | worst 290.4 us (capital_close_400ms, shed 12) | <= 450 |
| rays alone (5120x1440) | fighter 300 m 15.3 us (9 quads), close capital 48.2 us (9 quads), the fence 11.1 us | |
| `death_stage_cpu_32_deaths`, `death_big_stage_cpu` (PASS, FAIL) | 32 fighters 47.9 us, 1075 quads (repeats 47.5,47.9,51.2,46.7,50.7; the parent `d3e7698bf` 42.1 us, 588 quads); the big set 48.0 us, 0.0945 a quad | 50; 0.08 |

Follow-up (2026-11-08, coordinator on the grids: rays accepted; the long spark streaks read as hard rays through the
body at 600 m): long streaks one in 20 (was 12), 30-60 px (was 60-120), head 3 px with its tail 2 px at the head; every
streak at most 1.2 R(t) on screen while the body draws (`spark_cap_r`); the sparks' radiance x `spark_gain` 0.8 (head
peak 2.08 engine). The 600 m grid (`stage-dump-rays-soft/fighter_600m_1080_agx.png`) shows no needles at 50 / 150 ms
and none across the body at 900 ms [frames]. Meter: 0.817 EV (the 120 m fighter over the dim scene at 400 ms; 1.13 at
gain 1), still over the 0.5 bound; per the coordinator no further head change, bound left. `death_big_stage_cpu` on a
quiet host (no cmake / cc1plus during the runs but three 20 s samples in the third): 0.0656, 0.0669, 0.0709 us a quad
over three records (33.3, 34.0, 36.0 us), all under 0.08: the 0.0945 above was host load. Records: the first with the
judging dump (D3D 331 checks, 2 failed: the meter and `bolt_budget_512_bolts_20_muzzles_5120` at 102.0 us of 100, its
bolt draw 80.3 us against 75.6 in the previous record with the same programs: GPU timing noise [I]); the second and
third without (331 / 1 failed, the meter); the committed record is the third, hook 147 / 0. Host tests
`test_weapon_effects_hook` and `_lab` 59, 2 failing on the meter row; AP `build-rays` 0 warnings (d3d9.dll sha256
2f13edaed19ed5a5...), `check_no_x87.py --edition ap` PASS (1181 reachable functions). Table (third record):

| Gate | Result [M] | Bound |
| --- | --- | --- |
| `death_core_radiance` (PASS; fighter 600 m, 8 seeds) | median radiance inside 0.2 R(t) at 0.05 L 6.984 (seeds 4.492..6.984); `death_body_white_hot_core` median at 0.1 L 5.273 | >= 6 |
| `death_rays_starburst` (PASS; 5120x1440, 4 seeds) | angular peaks at 0.1 L, 300 m: at 0.5 R(t) 11, 11, 9, 14 for 11, 11, 9, 13 rays drawn; at 1.5 R(t) 6, 7, 6, 8; at 0.6 L 0; 600 m (recorded) 8, 14, 10, 16 / 6, 7, 5, 8 | drawn +- 1; >= 3; <= 2 |
| `death_rays_soft` (PASS; 300 m, 0.1 L) | median FWHM across the rays at 1.0 R(t) 7.03 px at 1080 rows (37 rays; per seed 7.38, 6.68, 6.7, 7.33); the worst ray peak over the death under it at 0.5 R(t) 0.123 (per seed 0.033, 0.123, 0.037, 0.029) | >= 4; <= 0.4 |
| `death_rays_differ` (PASS) | two deaths' 360-bin profiles at 0.1 L correlate -0.033 | < 0.3 |
| `death_sparks_dense` (PASS; fighter 600 m, 0.3 L) | 221 sparks, 217 drawn, 217 heads on screen, 116 local maxima in the ember part; lengths 16.0 +- 8.4 px (std / mean 0.528) | >= 100; >= 0.4 |
| `death_spark_heads` (PASS; fighter 600 m, 1080p, 0.3 L) | median head FWHM 4.37 px at 1080 rows over 116 image heads (the narrowest of the row, column and diagonal half widths) | >= 3 |
| `death_embers_hidden_in_body`, `death_embers_lengths_and_widths`, `death_embers_random_and_cooling` (PASS, PASS, PASS) | 41 at birth, 207 of 221 once the births are out (0.05 L); widths 3.00..4.98 px; seed correlation -0.186, T 0.75 / 0.5 / 0.25 at 0.3 / 0.7 / 1.1 L, farthest 221.3 px | > 0, >= 80 %; std >= 0.4 mean; < 0.5, >= 0.75 / >= 0.5 / <= 0.3 |
| `death_exposure_meter_shift` (FAIL; the production meter's fresh target against the dim and lit scenes alone; fighter 300 m and 120 m, 5120x1440, Auto <= +1.3 EV, gamma 2.2) | worst shift 0.817 EV over dim / lit (black sky informational, not gated: an empty frame scores 0 EV and any signal lifts it to the cap; 1.3 EV); 300 m: black / dim 0.15 / lit 0.5 worst 0.000 / 0.000 / 0.000 EV (frame peaks 35.03 / 35.06 / 18.27 / 7.54 / 3.84 / 2.52); 120 m: black / dim 0.15 / lit 0.5 worst 1.300 / 0.817 / 0.000 EV (frame peaks 17.94 / 15.7 / 10.2 / 5.18 / 3.32 / 2.52) | <= 0.5 EV |
| `death_no_pop_at_birth`, `death_big_no_pop_at_birth` (PASS, PASS) | worst frame-to-frame peak rise corvette_1500m 1.271, capital_1500m 1.195 | <= 2 |
| `death_rays_sparks_budget_5120` (PASS) | fighter 600 m 21.6 / 21.9 / 23.3 us at 0.1 / 0.05 / 0.3 L; 20 fighters 48.6 / 48.2 / 51.2 us (spark halvings 2: 787 drawn, 2624 shed, 250 rays); fence 11.4 us | <= 60; <= 150 |
| `death_budget_capital_close_5120` (PASS) | worst 287.9 us (capital_close_400ms, shed 12) | <= 450 |
| rays alone (5120x1440) | fighter 300 m 15.9 us (9 quads), close capital 48.0 us (9 quads), the fence 11.4 us | |
| `death_stage_cpu_32_deaths`, `death_big_stage_cpu` (PASS, PASS) | 32 fighters 48.9 us, 1075 quads (repeats 48.8,48.4,48.9,66.8,74.9; the parent `d3e7698bf` 42.1 us, 588 quads); the big set 36.0 us, 0.0709 a quad | 50; 0.08 |

Meter bound by case (2026-11-08, coordinator decision): a death at 120 m may dim the scene, so
`death_exposure_meter_shift` bounds the 300 m fighter at 0.5 EV (measured 0.0: sparks at normal range never drive the
meter) and the 120 m fighter at 1.3 EV, Auto's upper limit (measured 0.817 over the dim scene at 400 ms; 1.13 at the
spark gain 1; the 3-5 px Gaussian heads put their whole radiance on a pixel, and the p99 tile maximum follows it [I]).
Flight question added to explosion-look.md section 14: pumping (scene darkening and brightening with the spark cloud
at point-blank range). Record: `run_weapon_effects.py --host native` under the X3 lock, no dump: D3D 331 checks, 0
failed; hook 147 / 0 (a run just before on a loaded host failed the two CPU rows at 54.6 us and 0.0854 us a quad and
the bolt timing row; this one 44.8 us and 0.0673). Host tests `test_weapon_effects_hook` and `_lab` 59, all pass.
Table:

| Gate | Result [M] | Bound |
| --- | --- | --- |
| `death_core_radiance` (PASS; fighter 600 m, 8 seeds) | median radiance inside 0.2 R(t) at 0.05 L 6.984 (seeds 4.492..6.984); `death_body_white_hot_core` median at 0.1 L 5.273 | >= 6 |
| `death_rays_starburst` (PASS; 5120x1440, 4 seeds) | angular peaks at 0.1 L, 300 m: at 0.5 R(t) 11, 11, 9, 14 for 11, 11, 9, 13 rays drawn; at 1.5 R(t) 6, 7, 6, 8; at 0.6 L 0; 600 m (recorded) 8, 14, 10, 16 / 6, 7, 5, 8 | drawn +- 1; >= 3; <= 2 |
| `death_rays_soft` (PASS; 300 m, 0.1 L) | median FWHM across the rays at 1.0 R(t) 7.03 px at 1080 rows (37 rays; per seed 7.38, 6.68, 6.7, 7.33); the worst ray peak over the death under it at 0.5 R(t) 0.123 (per seed 0.033, 0.123, 0.037, 0.029) | >= 4; <= 0.4 |
| `death_rays_differ` (PASS) | two deaths' 360-bin profiles at 0.1 L correlate -0.033 | < 0.3 |
| `death_sparks_dense` (PASS; fighter 600 m, 0.3 L) | 221 sparks, 217 drawn, 217 heads on screen, 116 local maxima in the ember part; lengths 16.0 +- 8.4 px (std / mean 0.528) | >= 100; >= 0.4 |
| `death_spark_heads` (PASS; fighter 600 m, 1080p, 0.3 L) | median head FWHM 4.37 px at 1080 rows over 116 image heads (the narrowest of the row, column and diagonal half widths) | >= 3 |
| `death_embers_hidden_in_body`, `death_embers_lengths_and_widths`, `death_embers_random_and_cooling` (PASS, PASS, PASS) | 41 at birth, 207 of 221 once the births are out (0.05 L); widths 3.00..4.98 px; seed correlation -0.186, T 0.75 / 0.5 / 0.25 at 0.3 / 0.7 / 1.1 L, farthest 221.3 px | > 0, >= 80 %; std >= 0.4 mean; < 0.5, >= 0.75 / >= 0.5 / <= 0.3 |
| `death_exposure_meter_shift` (PASS; the production meter's fresh target against the dim and lit scenes alone; fighter 300 m and 120 m, 5120x1440, Auto <= +1.3 EV, gamma 2.2) | worst shift over dim / lit 300 m 0.0 EV, 120 m 0.817 EV (black sky informational, not gated: an empty frame scores 0 EV and any signal lifts it to the cap; 1.3 EV); 300 m: black / dim 0.15 / lit 0.5 worst 0.000 / 0.000 / 0.000 EV (frame peaks 35.03 / 35.06 / 18.27 / 7.54 / 3.84 / 2.52); 120 m: black / dim 0.15 / lit 0.5 worst 1.300 / 0.817 / 0.000 EV (frame peaks 17.94 / 15.7 / 10.2 / 5.18 / 3.32 / 2.52) | 300 m <= 0.5 EV; 120 m <= 1.3 EV (Auto's upper limit) |
| `death_no_pop_at_birth`, `death_big_no_pop_at_birth` (PASS, PASS) | worst frame-to-frame peak rise corvette_1500m 1.271, capital_1500m 1.195 | <= 2 |
| `death_rays_sparks_budget_5120` (PASS) | fighter 600 m 21.4 / 20.9 / 21.1 us at 0.1 / 0.05 / 0.3 L; 20 fighters 47.0 / 45.0 / 47.8 us (spark halvings 2: 787 drawn, 2624 shed, 250 rays); fence 11.2 us | <= 60; <= 150 |
| `death_budget_capital_close_5120` (PASS) | worst 294.0 us (capital_close_400ms, shed 12) | <= 450 |
| rays alone (5120x1440) | fighter 300 m 15.1 us (9 quads), close capital 47.7 us (9 quads), the fence 11.2 us | |
| `death_stage_cpu_32_deaths`, `death_big_stage_cpu` (PASS, PASS) | 32 fighters 44.8 us, 1075 quads (repeats 44.8,44.7,44.8,44.6,44.8; the parent `d3e7698bf` 42.1 us, 588 quads); the big set 34.2 us, 0.0673 a quad | 50; 0.08 |

Review fixes (2026-11-08, on `791a7d3e5`): the ray flicker's period is at least two frame steps (one step gave a fresh
level every frame under SETA: a 0.3 strobe); the spark quads are charged to the family's budget at
`budget_weight_flash` a screen (they grew about 8x with the 3-5 px heads and their halo window), and the frame's
estimated spark area takes at most a quarter of the budget before another halving; the 120 m meter bound is Auto's
upper limit (+1.3 EV). New row `death_sparks_1024_budget_5120`: six fighters at 300 m, 986 sparks demanded, 962 drawn,
no halving, the sparks alone at 0.3 L: 38.5 us at 5120x1440 on the first run (0.053 screens charged), the bound set at
that + 25 % = 48.1 us; the record 41.2 us (1080p 34.5 us). Record: D3D 332 checks, 2 failed, both loaded-host CPU rows
with another project's X4.exe running on the Windows side: `death_stage_cpu_32_deaths` 62.5 us (repeats 49.2-68.1;
44.8 in the previous record) and `death_big_stage_cpu` 0.0963 us a quad (0.0673); hook 147 / 0. Programs `--check`
PASS; host tests `test_weapon_effects_hook` and `_lab` 59, 2 failing on those CPU rows; AP `build-rays` 0 warnings
(d3d9.dll sha256 70be38ab567405f2...), `check_no_x87.py --edition ap` PASS (1181 reachable functions).

**More rays, the rim halo** (2026-11-08, on `773a47f40`; the user: "increase number of rays ... various length - I see
still 3 big rays which have almost the same length" and "a halo which falls off from the heat source: on the edges it
should be bright/intensive then fall off"; law in [explosion-look.md](../architecture/explosion-look.md) section 14
items 1 and 2). Rays: an odd 15-25 a death, lengths (0.3 + 2.2 g^2) R_f with the death's three largest golden-ratio
hashes at L(g_1), 0.78 L(g_1), 0.6 L(g_1) and every other ray scaled so the fourth sits at 0.8 of the third (one O(N)
pass a frame); width, brightness, flicker and the shed unchanged; the sprite program unchanged (235 slots). Halo: every
tier a rim glow (`weapon_halo_ps.hlsl`, 60 -> 90 slots, 470 words [M]): 0 inside 0.9 R(t), peak 0.5 E_b T
heat(0.8 T) at R(t), (e^(-(d / 0.35)^2) + 0.25 w(d) / (1 + d^2)) / 1.25 outside, the quad to 2.5 R(t), faded over
0.3-0.6 L; it replaces D3's 1.6 R(t) glow and D15's 1.15 R(t) / 0.2 E_b cap. Gates re-derived with the law (old ->
new): `death_halo_in_air` farthest 10 % pixel <= 1.8 -> 2.2 R(t) at 0.1 / 0.3 / 0.45 / 0.55 L (was 0.6 / 0.75 L, now
faded) and none from 0.62 L; `death_big_halo_capped` <= 1.25 R(t), <= 0.2 E_b -> <= 2.2 R(t) as impact parameter (the
capital at 1.5 km spans a third of the view: its 10 % pixel at 2.34 R f / z is 1.80 R(t)), <= 0.5 E_b, the profile's
peak within 0.1 of the rim's silhouette, < 0.15 at 2 R(t), 0 inside 0.85 R(t); `death_rays_starburst` peaks at 0.5
R(t) drawn +- 1 -> >= 9 and <= drawn + 1 (at 0.5 R(t) the 15-25 slots sit 4.6-9 px apart against an 8 px FWHM), plus
the three longest each >= 1.25 x the next and their visible tips at one fraction of the length (spread <= 0.15). New
`death_halo_rim`. Lab: two rounds (`stage-dump-rays-halo/lab/`, fighter 300 m and corvette 1,500 m at 50 / 150 / 400 /
900 ms): the first (tail weight 0.4 windowed from 1.8 R(t), heat(T)) read as a grey disc with an edge out to 2 R(t),
the second (tail 0.25 windowed from 1.5 R(t), heat(0.8 T)) as a warm glow falling off the rim; the one long ray and the
two shorter ones read as different lengths, the short rays sit mostly under the body and the halo. Native grids
(`stage-dump-rays-halo/`, fighter 300 / 600 m, nested, capital broadside; AgX from the 1080p frame): the same warm rim
glow, no balloon fill. Record: `run_weapon_effects.py --host native` under the X3 lock (the judging dump first; the
committed record after the two gate corrections, no dump): D3D 333 checks, 1 failed, `bolt_budget_512_bolts_20_muzzles_5120`
(a bolt timing row this change does not touch, X4.exe running on the host; it failed the same way on load before);
hook 147 / 0. The dump record also failed `death_stage_cpu_32_deaths` 53.2 us (the committed record 48.5, bound 50) and
the hull CPU rows, host load. Programs `--check` PASS; host tests `test_weapon_effects_hook` and `_lab` 59, 1 failing
(the record's overall pass, the bolt row); AP `build-rays` 0 warnings (d3d9.dll sha256 4c29dd71bf55f754...),
`check_no_x87.py --edition ap` PASS (1181 reachable functions). Not flown. Table from
`python3 verification/results/explosion-look/ledger_rows.py --markdown --rays`:

| Gate | Result [M] | Bound |
| --- | --- | --- |
| `death_core_radiance` (PASS; fighter 600 m, 8 seeds) | median radiance inside 0.2 R(t) at 0.05 L 6.984 (seeds 4.492..6.984); `death_body_white_hot_core` median at 0.1 L 5.273 | >= 6 |
| `death_rays_starburst` (PASS; 5120x1440, 4 seeds) | angular peaks at 0.1 L, 300 m: at 0.5 R(t) 13, 16, 11, 15 for 21, 17, 23, 15 rays drawn; at 1.5 R(t) 5, 4, 5, 4; at 0.6 L 0; 600 m (recorded) 7, 7, 12, 7 / 5, 4, 4, 3 | >= 9 and <= drawn + 1 (2026-11-08; drawn +- 1 before); >= 3; <= 2 |
| ray lengths (2026-11-08; 300 m, 0.1 L, part of `death_rays_starburst`) | the three longest over R_f 2.336,1.822,1.402 (rest <= 1.121; 167.7,130.8,100.6 px at 1080 rows); 2.199,1.715,1.319 (rest <= 1.055; 157.8,123.1,94.7 px at 1080 rows); 2.271,1.772,1.363 (rest <= 1.09; 163.0,127.2,97.8 px at 1080 rows); 2.375,1.853,1.425 (rest <= 1.14; 170.5,133.0,102.3 px at 1080 rows); visible tip over length 0.645..0.654 | each >= 1.25 x the next; spread <= 0.15 |
| `death_halo_rim` (PASS; fighter 600 m, the halo alone) | 0.1 L: profile peak at 1.022 R(t), 0.056 of it at 2 R(t), inside 0.85 R(t) 0.0; 0.3 L: profile peak at 1.03 R(t), 0.05 of it at 2 R(t), inside 0.85 R(t) 0.0; capital 1.5 km at 0.2 L: peak at 1.003 of the rim's silhouette, 0.05 at 2 R(t)'s, peak 0.619, farthest 10 % pixel at 1.801 R(t) (`death_big_halo_capped` PASS) | within 0.1 R(t); < 0.15; 0 |
| `death_rays_soft` (PASS; 300 m, 0.1 L) | median FWHM across the rays at 1.0 R(t) 6.31 px at 1080 rows (31 rays; per seed 6.21, 7.05, 6.14, 7.07); the worst ray peak over the death under it at 0.5 R(t) 0.237 (per seed 0.034, 0.237, 0.044, 0.024) | >= 4; <= 0.4 |
| `death_rays_differ` (PASS) | two deaths' 360-bin profiles at 0.1 L correlate -0.057 | < 0.3 |
| `death_sparks_dense` (PASS; fighter 600 m, 0.3 L) | 221 sparks, 217 drawn, 217 heads on screen, 116 local maxima in the ember part; lengths 16.0 +- 8.4 px (std / mean 0.528) | >= 100; >= 0.4 |
| `death_spark_heads` (PASS; fighter 600 m, 1080p, 0.3 L) | median head FWHM 4.37 px at 1080 rows over 116 image heads (the narrowest of the row, column and diagonal half widths) | >= 3 |
| `death_embers_hidden_in_body`, `death_embers_lengths_and_widths`, `death_embers_random_and_cooling` (PASS, PASS, PASS) | 41 at birth, 207 of 221 once the births are out (0.05 L); widths 3.00..4.98 px; seed correlation -0.186, T 0.75 / 0.5 / 0.25 at 0.3 / 0.7 / 1.1 L, farthest 221.3 px | > 0, >= 80 %; std >= 0.4 mean; < 0.5, >= 0.75 / >= 0.5 / <= 0.3 |
| `death_exposure_meter_shift` (PASS; the production meter's fresh target against the dim and lit scenes alone; fighter 300 m and 120 m, 5120x1440, Auto <= +1.3 EV, gamma 2.2) | worst shift over dim / lit 300 m 0.0 EV, 120 m 0.588 EV (black sky informational, not gated: an empty frame scores 0 EV and any signal lifts it to the cap; 1.3 EV); 300 m: black / dim 0.15 / lit 0.5 worst 0.000 / 0.000 / 0.000 EV (frame peaks 34.5 / 34.56 / 17.8 / 7.13 / 3.48 / 2.29); 120 m: black / dim 0.15 / lit 0.5 worst 1.300 / 0.588 / 0.000 EV (frame peaks 17.45 / 15.22 / 9.74 / 4.77 / 2.96 / 2.3) | 300 m <= 0.5 EV; 120 m <= 1.3 EV (Auto's upper limit) |
| `death_no_pop_at_birth`, `death_big_no_pop_at_birth` (PASS, PASS) | worst frame-to-frame peak rise corvette_1500m 1.277, capital_1500m 1.456 | <= 2 |
| `death_rays_sparks_budget_5120` (PASS) | fighter 600 m 22.5 / 21.9 / 22.6 us at 0.1 / 0.05 / 0.3 L; 20 fighters 57.3 / 50.8 / 64.5 us (spark halvings 2: 787 drawn, 2624 shed, 382 rays); fence 10.8 us | <= 60; <= 150 |
| `death_budget_capital_close_5120` (PASS) | worst 283.9 us (capital_close_2000ms, shed 0) | <= 450 |
| rays alone (5120x1440) | fighter 300 m 16.6 us (23 quads), close capital 49.7 us (23 quads), the fence 10.8 us | |
| `death_stage_cpu_32_deaths`, `death_big_stage_cpu` (PASS, PASS) | 32 fighters 48.5 us, 993 quads (repeats 48.8,43.9,48.2,49.1,48.5; the parent `d3e7698bf` 42.1 us, 588 quads); the big set 39.7 us, 0.0735 a quad | 50; 0.08 |

Rays from the rim (2026-11-08, coordinator on the native grids: the halo accepted; the rays vanished, at 50 / 150 ms
the fighter a bright ball in its halo with one faint spoke). The own sun's rule (own-sun-look.md "why the rays
vanished"): every ray starts at the body's rim, 1.0 R(t) at its front (`ray_start` 0.3 -> 1.0), at full strength (the
fade-in 0.15 -> 0.05 of its length, `weapon_sprite_ps.hlsl`, still 235 slots / 1,022 words), lengths beyond the rim
(0.4 + 2.0 g^2) R_f with the top-three rule kept, `ray_e` 0.4 -> 1.0 (a ray's base about 0.8-1.0 of the rim halo
under it); width, flicker, count and fall-off unchanged. Gates moved with the start: `death_rays_starburst` counts at
1.25 R(t), >= 12 distinct and <= drawn + 1 (was >= 9 at 0.5 R(t)); `death_rays_soft` measures there, the ray over what
lies under it <= 1.2 (was 0.4: a ray now sits at full strength over the rim halo; past 1.2 x its ground it reads as a
painted spoke). Lab (`stage-dump-rays-halo/lab/r3-*`, one round; the spoke count from
`verification/results/explosion-look/count_spokes.py rays-only.png frame.png ...` on the local shots): the fighter at 300 m shows 21 spokes at 1.2 rim radii in the rays-only frame at
150 ms, 14 / 21 in the full frame at 50 / 150 ms, short rays visible past the halo [M, lab]. Record:
`run_weapon_effects.py --host native` under the X3 lock, no dump: D3D 333 checks, 0 failed; hook 147 / 0. Host tests
`test_weapon_effects_hook` and `_lab` 59, all pass; programs and lab `--check` PASS; AP `build-rays` 0 warnings
(d3d9.dll sha256 bfa2f676bbd645f0...), `check_no_x87.py --edition ap` PASS (1181 reachable functions). Not flown.
Table:

| Gate | Result [M] | Bound |
| --- | --- | --- |
| `death_core_radiance` (PASS; fighter 600 m, 8 seeds) | median radiance inside 0.2 R(t) at 0.05 L 6.984 (seeds 4.492..6.984); `death_body_white_hot_core` median at 0.1 L 5.273 | >= 6 |
| `death_rays_starburst` (PASS; 5120x1440, 4 seeds) | angular peaks at 0.1 L, 300 m: at 1.25 R(t) 21, 17, 23, 15 for 21, 17, 23, 15 rays drawn; at 1.5 R(t) 13, 10, 13, 10; at 0.6 L 0; 600 m (recorded) 19, 18, 18, 15 / 12, 9, 13, 9 | >= 12 and <= drawn + 1 (2026-11-08, from the rim; drawn +- 1 at 0.5 R(t) before); >= 3; <= 2 |
| ray lengths (2026-11-08; 300 m, 0.1 L, part of `death_rays_starburst`) | the three longest over R_f 2.251,1.756,1.351 (rest <= 1.08; 161.6,126.0,96.9 px at 1080 rows); 2.126,1.658,1.276 (rest <= 1.021; 152.6,119.1,91.6 px at 1080 rows); 2.192,1.710,1.315 (rest <= 1.052; 157.4,122.7,94.4 px at 1080 rows); 2.286,1.783,1.372 (rest <= 1.097; 164.1,128.0,98.5 px at 1080 rows); visible tip over length 0.600..0.610 | each >= 1.25 x the next; spread <= 0.15 |
| `death_halo_rim` (PASS; fighter 600 m, the halo alone) | 0.1 L: profile peak at 1.022 R(t), 0.056 of it at 2 R(t), inside 0.85 R(t) 0.0; 0.3 L: profile peak at 1.03 R(t), 0.05 of it at 2 R(t), inside 0.85 R(t) 0.0; capital 1.5 km at 0.2 L: peak at 1.003 of the rim's silhouette, 0.05 at 2 R(t)'s, peak 0.619, farthest 10 % pixel at 1.801 R(t) (`death_big_halo_capped` PASS) | within 0.1 R(t); < 0.15; 0 |
| `death_rays_soft` (PASS; 300 m, 0.1 L) | median FWHM across the rays at 1.25 R(t) 6.1 px at 1080 rows (76 rays; per seed 6.11, 6.11, 6.09, 6.1); the worst ray peak over the death under it at 1.25 R(t) 0.92 (per seed 0.792, 0.626, 0.626, 0.92) | >= 4; <= 1.2 (2026-11-08, was 0.4) |
| `death_rays_differ` (PASS) | two deaths' 360-bin profiles at 0.1 L correlate -0.044 | < 0.3 |
| `death_sparks_dense` (PASS; fighter 600 m, 0.3 L) | 221 sparks, 217 drawn, 217 heads on screen, 116 local maxima in the ember part; lengths 16.0 +- 8.4 px (std / mean 0.528) | >= 100; >= 0.4 |
| `death_spark_heads` (PASS; fighter 600 m, 1080p, 0.3 L) | median head FWHM 4.37 px at 1080 rows over 116 image heads (the narrowest of the row, column and diagonal half widths) | >= 3 |
| `death_embers_hidden_in_body`, `death_embers_lengths_and_widths`, `death_embers_random_and_cooling` (PASS, PASS, PASS) | 41 at birth, 207 of 221 once the births are out (0.05 L); widths 3.00..4.98 px; seed correlation -0.186, T 0.75 / 0.5 / 0.25 at 0.3 / 0.7 / 1.1 L, farthest 221.3 px | > 0, >= 80 %; std >= 0.4 mean; < 0.5, >= 0.75 / >= 0.5 / <= 0.3 |
| `death_exposure_meter_shift` (PASS; the production meter's fresh target against the dim and lit scenes alone; fighter 300 m and 120 m, 5120x1440, Auto <= +1.3 EV, gamma 2.2) | worst shift over dim / lit 300 m 0.0 EV, 120 m 0.584 EV (black sky informational, not gated: an empty frame scores 0 EV and any signal lifts it to the cap; 1.3 EV); 300 m: black / dim 0.15 / lit 0.5 worst 0.000 / 0.000 / 0.000 EV (frame peaks 34.5 / 34.56 / 17.8 / 7.13 / 3.48 / 2.29); 120 m: black / dim 0.15 / lit 0.5 worst 1.300 / 0.584 / 0.000 EV (frame peaks 17.45 / 15.22 / 9.74 / 4.77 / 2.96 / 2.3) | 300 m <= 0.5 EV; 120 m <= 1.3 EV (Auto's upper limit) |
| `death_no_pop_at_birth`, `death_big_no_pop_at_birth` (PASS, PASS) | worst frame-to-frame peak rise corvette_1500m 1.277, capital_1500m 1.456 | <= 2 |
| `death_rays_sparks_budget_5120` (PASS) | fighter 600 m 21.7 / 21.1 / 21.3 us at 0.1 / 0.05 / 0.3 L; 20 fighters 54.8 / 49.3 / 62.3 us (spark halvings 2: 787 drawn, 2624 shed, 382 rays); fence 10.9 us | <= 60; <= 150 |
| `death_budget_capital_close_5120` (PASS) | worst 284.1 us (capital_close_2000ms, shed 0) | <= 450 |
| rays alone (5120x1440) | fighter 300 m 15.0 us (23 quads), close capital 27.0 us (23 quads), the fence 10.9 us | |
| `death_stage_cpu_32_deaths`, `death_big_stage_cpu` (PASS, PASS) | 32 fighters 43.9 us, 993 quads (repeats 43.6,43.9,44.0,43.9,43.8; the parent `d3e7698bf` 42.1 us, 588 quads); the big set 35.8 us, 0.0663 a quad | 50; 0.08 |

## Fire material (2026-11-07)

[docs/architecture/fire-material.md](../architecture/fire-material.md) built through the shared fireball program: the
hull bursts of the fire classes (energy, heavy, beam, missile 1; the plasma globe 0.5; dust and smoke 0), every death
body and secondary (none with the eye inside: the Cartesian path and no tongue cost), the wave's band and sheet, the hull
ring's trailing band. `WeaponFireballVertex` 80 -> 92 bytes (`core[4]`: the core's gain, the fire weight, the size LOD
`look::fire_lod`, the element's game-time age in ms for the flow and flicker; the note's 84 had no age). Programs:
`weapon_fireball_ps` c14..c25 (the fire ramp and law), `weapon_wave_ps` v8 (tongues, no floor; c14.zw the radial cell
and the age), `weapon_ring_ps` c7.yzw / c8 (the band's tongues on a direction noise). Budget: the tongues charged at
`budget_weight_fire` and shed at step (0c), other owners farthest first with the octave step's hysteresis, the bodies'
remaining tongues (the newest death's, the own ship's) once more before a wave goes; `HullTuning::fire` /
`fire_core` for the fixture's octave-step and cores-off rows. Lab: the same law (`fireTongues`, `fireLaw`,
`fireCoreW`, the wave's tongues, `shockFront`'s band), a material toggle (fire / today) and the pair view (t, + 16, + 100
ms) in the scenario bar.

Deviations from the note, each from a measured failure:

- **The fire law's radius is the view-plane disc's |p|**, not the chord middle's |q|: on a hull hit the lane cuts the
  chord, |q| at the screen centre is about 0.5 and the tongues met in a point there (the lab's first round, a pinwheel).
- **Constants** (first native run against section 6, then a numpy replica of the program and of the fixture's measures,
  `verification/results/explosion-look/fire_model.py` / `fire_tune.py`, `--shipped` output in `fire_tune_shipped.txt`;
  the replica tracked the native rows within about 0.05): `fire_warp_amp` 0.6 and `fire_shear` 1.0 (section 8's step for
  straight spikes, lab round 2: a sea urchin late on the capital), `fire_blend_0 / _1` 0.12 / 0.40, `fire_thr_0 / _t / _r`
  0.12 / 0.43 / 0.57, `fire_heat_soft` 0.48, `fire_rim_t` 0.18, `fire_gain_small` 1.0 (section 8's far-hit step), and two
  new terms: `fire_rim_cool` 0.63 over |p| 0.45 .. 0.95 (the tips red: with the note's law no red pixel was lit, 0 %
  against 5-25 %) and the fire core in R_s 0.2 .. 0.35 (the note's 0.3 .. 0.5 R_s put 2.2 linear past 0.5 R(t) on the
  hull and 3.9 on a death body's 1.33 R_s sphere, against 1.5).
- **Under the size LOD the previous law**: threshold, softness, gain and heat lerp from today's law to the fire law by
  w_px x min(fire, 1) (section 2.5's "keeps the clumps and holes"; the fire threshold alone left a 30 px hit 0.20 lit
  inside R(t) against 0.4, and the plasma globe half way).
- **The wave**: `fire_wave_thr_0 / _end` 0.3 / 0.6 (the note's 0.08 filled the band, a tyre with blots, lab round 3; 0.45
  / 0.7 left the sheet 0.21 R_f against 0.6); the radial cell `fire_wave_aspect` 2.5 x the arc at r (the note's 1 w_e
  made 5:1 tangential cells at 4 R_f).
- **The ring's band**: a value noise of the owner-frame direction (31 cells about, no seam) offset along its third axis by
  -z / 3, riding the front (the ring program has no age); not a periodic 2-D noise.
- **Gate readings**: `fire_edge_licks` against the 4-boundary of the disc of the outline's area (against the hole-filled
  outline itself an open tongue silhouette is 1.0 by construction); the seams the median over the seeds, as every
  section-6 gate; the death fighter at 200 m (at 600 m a 1080p fighter is 23 px, under the tongues' LOD); the death
  colour gates (`death_body_colour_core_rim`, `death_cools_through_ramp`, `wave_fire_hue`) against the fire ramp's stops.

Lab rounds (headless Chromium, `hull/energy-ire-fighter-side`, `boom/fighter-m5-600m-side`, `boom/capital-m2-1500m-wave-
broadside`, `boom/capital-m7-far-5km`, beside the vanilla frame): (1) the note's constants: white core and orange tongues
early on the hull, a peach lump mid-life, the capital a filled orange starfish with straight spikes, the wave the old beige
band; (2) warp 0.6, shear 1.0, gain 2.2, blend 0.12 / 0.4: the capital read as fire (white core, bent tongues, gaps); (3)
thr_t 0.5 and the wave's tongues: the wave a filled brown band with blots; (4) the wave's threshold 0.45 / 0.7: a broken
rim with separate tongues. After the native gates the constants above (frames re-judged in the lab and the dump).

Native record (`run_weapon_effects.py --host native --dump`, 2026-11-07 23:03, commit 0684c6144-dirty, the screensaver
killed every 5 s through the run): 345 checks, 336 pass [M]. Gate table (`python3
verification/results/explosion-look/ledger_rows.py --fire`):

| Gate | Result [M] (hull; fighter / capital) | Bound |
| --- | --- | --- |
| `fire_luminance_thin_hull`, `_death` (FAIL, FAIL) | share > 0.8 peak 0.2243; 0.2989 / 0.3187; gaps under 0.25 peak 0.1066; 0.1004 / 0.1016 | 0.10-0.35; >= 0.35 |
| `fire_tongues_radial_hull`, `_death` (PASS, PASS) | radial / tangential 0.316; 0.317 / 0.327 | <= 0.75 |
| `fire_edge_licks_hull`, `_death` (PASS, PASS) | lit boundary / outline boundary 2.129; 2.061 / 2.282 | >= 2.0 |
| `fire_temporal_hull` (PASS) | high-pass corr +16 ms 0.878, +100 ms 0.261; low-pass +16 ms 0.998 | 0.5-0.9; <= 0.6; >= 0.9 |
| `fire_colour_ramp_hull`, `_death` (PASS, PASS) | hot 0.457 @ 0.31, red 0.097 @ 0.699, mid 0.436; hot 0.473 / 0.473 @ 0.323 / 0.324, red 0.091 / 0.084 @ 0.737 / 0.753, mid 0.443 / 0.433 | >= 0.25 @ <= 0.5; 0.05-0.25 @ >= 0.65; <= 0.5 |
| `fire_core_bloom_hull`, `_death` (PASS, PASS) | peak inside 0.3 R 3.243 (40 ms), 5.324 / 5.324 (0.1 L); outside 0.5 R 1.302; 1.398 / 1.394 | >= 3.0; <= 1.5 |
| `hull_20_hits_no_white_sun` (PASS) | cores on: worst share above 3.0 0.0 | <= 0.01 (cores off <= 3.0 as before) |
| `fire_small_reads` (PASS) | 30 px far hit at 0.4 L_b: holes 0.3697, lobes 2.559, lit share 0.6303 | >= 0.10; >= 1.2; >= 0.4 |
| `wave_not_a_tyre` (PASS; 5 km face-on, 0.5 tau) | band mean / edge 0.16, dark share 0.798, anisotropy 0.52, grey 0.0 | <= 0.35; >= 0.40; <= 0.75; <= 0.10 |
| `fire_no_seam_hull`, `_death`, `_wave` (PASS, PASS, PASS) | median over the seeds 0.0611; 0.0642 / 0.0617; 0.003 (worst seed 0.1803; 0.1541 / 0.0979) | <= 0.1 (median) |
| program slots (device creation, max 4096) | fireball_ps 769, wave_ps 781, ring_ps 292; fireball vertex 92 bytes | the modern cap 32768 |
| `hull_fire_within_weight` (PASS) | tongues on 401.8 us (2.264), off 239.2 us (1.348) at 5120x1440: + 0.916 a screen | within 0.1 of budget_weight_fire - budget_weight_fireball |
| alone rows (5120x1440) | body 240.4 us, wave 337.8 us (one octave 306.9), eye inside a fighter 297.1 us | eye inside <= 300 |
| `death_budget_capital_close_5120` (PASS) | worst 383.9 us (capital_close_600ms, shed 9) | <= 450 |
| `death_budget_5120`, `death_rays_sparks_budget_5120` (PASS, PASS) | fighter 600 m 22.2 us, 20 fighters 47.1 us | <= 60; <= 150 |
| `hull_budget_20_hits_5120`, `hull_budget_capital_close_64_within_budget` (FAIL, PASS) | 20 hits 88.2 us, 60 m 805.5 us, capital 64 1164.5 us | <= 80; <= 1420 |

Cost [M] against the parent record (8eeab446b): `fireball_ps` 431 -> 769 slots, `wave_ps` 727 -> 781, `ring_ps` 180 ->
292; the tongues + 0.89 a screen (+ 67 % a pixel; the note's [I] was + 15-25 %), `budget_weight_fire` 2.07; the wave
alone 314.4 -> 337.8 us (`budget_weight_wave` 1.71 -> 1.83, `_one` 1.53 -> 1.68); hull 20 hits at 200 m 71.8 -> 88.2 us,
at 60 m 629.8 -> 805.5 us, capital 64 hits 1010.5 -> 1164.5 us; death fighter 600 m 19.7 -> 22.2 us, 20 fighters 38.8 ->
47.1 us, the close capital's worst frame 347.7 -> 383.9 us (fence 450), eye inside a fighter 297.1 us (fence 300).

Failing on the record, with the cause:

| Check | Measured [M] | Cause |
| --- | --- | --- |
| `fire_luminance_thin_hull`, `_death` | the thin clause passes (0.22; 0.30 / 0.32); gaps under 0.25 peak inside the hole-filled outline 0.107; 0.100 / 0.102 against >= 0.35 | the gap clause counts outline pixels between 0.1 and 0.25 of the peak; a tongue silhouette's gaps are open bays the hole fill drops, and vanilla's own frame measures 0.000 (section 1). No constant of the search reached 0.2 without emptying the body. A note decision |
| `hull_burst_erodes_outside_in` | lit share inside R(t) 0.53 / 0.21 / 0.008 at 0.1 / 0.5 / 0.9 L_b against >= 0.8, 0.35-0.75, <= 0.15 | the pre-fire body gate counts a filled body; the tongues' gaps and the harder threshold are the material (the note keeps the gate's bounds: a conflict to decide) |
| `hull_burst_no_centre_ring` | worst hit's prominence 0.157 at 0.5 L_b against 0.15 (medians 0.018-0.045) | one seed's tongue base over the bound by 0.007 |
| `death_big_tier_sizes` | outline over the law at 0.3 L: corvette 0.66, capital 0.74, station 0.68 | the big bodies erode faster under the fire threshold (the fighter and missile at the small LOD keep the law: 0.96, 0.95) |
| `wave_broken_circumference` | min bin peak 0.019 of the ring's against >= 0.05 (arcs ratio and spread pass) | v7's "no transparent arc" floor; the tongue band has no floor by the note |
| `hull_plasma_globe_shrinks_reddens` | radii 49.3 / 45.8 / 33.7 px shrink; at 0.95 L no lit pixel (b / r 0.71, 0.79, -) | the globe at half the fire law erodes away before 0.95 L |
| `hull_budget_20_hits_5120` | 88.2 us against 80 | the tongues' cost on 20 bursts under the budget (no shed) |
| `death_big_stage_cpu` | 0.0926 us a quad against 0.08 (a run before 0.1010) | host CPU timing; no CPU bound changed |

Frames judged: `<scratchpad>/stage-dump-fire/` (fighter 300 / 600 m, nested, capital broadside: 5120x1440 clipped engine
space and 1080 AgX, the AgX from the 1080 frame's 1 - e^-v PNG with the linear value recovered, clipping near 5.5) and
`stage-dump-fire/all/` (every dump case, crop_grid / part_grid layout); the raw PPM / PFM deleted as written. Read: the
fighter a blown white core with rays to 150 ms, orange tongues about it at 400 ms, red tongues in the spark cloud at 900
ms; the capital's secondaries small fire bodies, its body a white core with bent orange tongues and dark gaps, a red-orange
starburst of tongues at 2700 ms; the 5 km wave a thin bright rim with dark-red tongues trailing inward, translucent; the
hull burst a white core with tongues early, a peach-white body with tongues at 0.3 L_b (still the fullest frame), a lobed
red body late. Not flown.

### Round 2 (2026-11-07/08, worktree on `0b782a140`, merged with `7cb52314c` and `773a47f40`)

The orchestrator's judgement of the round-1 frames: pink / peach mid-life, flat tongues (petals), a tan cloud on the
20-hit cluster, straight cuts on the far capital, the wave a drawn hoop, the nested fighters smooth blobs at 900 ms. What
changed is in [fire-material.md](../architecture/fire-material.md) section 12 (every constant and gate against the
note): the ramp (green before blue, the red and orange stops dim, the core towards the yellow-white top stop), the fine
octave (polar under the tongues, streaming; Cartesian elsewhere; luminance x 0.65-1, threshold jitter 0.10),
`fire_px` 24 / 40, the plasma globe on the previous threshold, the cluster's thinning, glow and ember saturation, the
tongues' fence (clusters of more than four other owners' bursts), shed tongues keeping the law, and the wave's outward
licks with a spine-only rim. The replica (`fire_round2.py`, `fire_pink.py`) chose the constants; three lab rounds in
headless Chromium (`<scratchpad>/stage-dump-fire2/lab/r1..r3`), then native records.

The straight cuts on `boom_capital_far_5km` frames 1-3 are not the quad: the impostor's quad is the sphere's exact screen
rectangle and the law lives inside the sphere (`fire_quad_edge_dark_*` 0.0 on every frame [M]); the `-body` part is drawn
over the fixture's dying-hull box in the lane (alpha 0.91 / 0.83 / 0.55, none at 2700 ms, where the frame has no cut):
occlusion class 2 against a box.

Vanilla's own frame on the round-2 gap clause (`verification/results/explosion-look/vanilla_disc_dark.py`, output
`vanilla_disc_dark.txt`): 0.096 linear about its clipped core (0.090 about the lit centroid, 0.077 in sRGB) [M], under
the orchestrator's 0.20; the gate keeps 0.20 as asked.

| Gate | Result [M] (hull; fighter / capital) | Bound |
| --- | --- | --- |
| `fire_luminance_thin_hull`, `_death` (PASS, PASS) | share > 0.8 peak 0.1538; 0.1238 / 0.1131; equal-area disc under 0.1 peak 0.2195; 0.2053 / 0.2046 (the outline's gaps under 0.25 peak 0.1211; 0.1049 / 0.1165, printed) | 0.10-0.35; >= 0.20 (vanilla 0.096) |
| `fire_not_pink_hull`, `_death` (PASS, PASS) | AgX pink share at 0.2, 0.3, 0.5 L 0.0057,0.0001,0.0000; 0.0057,0.0000,0.0000 / 0.0042,0.0000,0.0000 | the medians' largest < 0.02 |
| `fire_quad_edge_dark_hull`, `_death` (PASS, PASS) | the quad's boundary over the peak, worst hit 0.0; 0.0 / 0.0 | < 0.02 |
| `fire_tongues_radial_hull`, `_death` (PASS, PASS) | radial / tangential 0.29; 0.191 / 0.238 | <= 0.75 |
| `fire_edge_licks_hull`, `_death` (PASS, PASS) | lit boundary / outline boundary 2.158; 2.129 / 2.435 | >= 2.0 |
| `fire_temporal_hull` (PASS) | high-pass corr +16 ms 0.846, +100 ms 0.237; low-pass +16 ms 0.998 | 0.5-0.9; <= 0.6; >= 0.9 |
| `fire_colour_ramp_hull`, `_death` (PASS, PASS) | hot 0.39 @ 0.309, red 0.211 @ 0.714, mid 0.391; hot 0.42 / 0.408 @ 0.321 / 0.327, red 0.206 / 0.188 @ 0.749 / 0.746, mid 0.388 / 0.382 | >= 0.25 @ <= 0.5; 0.05-0.25 @ >= 0.65; <= 0.5 |
| `fire_core_bloom_hull`, `_death` (PASS, PASS) | peak inside 0.3 R 3.237 (40 ms), 5.318 / 5.318 (0.1 L); outside 0.5 R 1.242; 1.296 / 1.341 | >= 3.0; <= 1.5 |
| `hull_20_hits_no_white_sun` (PASS) | cores on: worst share above 3.0 0.0 | <= 0.01 (cores off <= 3.0 as before) |
| `fire_small_reads` (PASS) | 30 px far hit at 0.4 L_b: holes 0.6652, lobes 3.573, equal-area radius / R(t) 0.5793 (lit share inside R(t) 0.3348, printed) | >= 0.10; >= 1.2; >= 0.45 |
| `wave_not_a_tyre` (PASS; 5 km face-on, 0.5 tau) | band mean / edge 0.182, dark share 0.676, anisotropy 0.277, grey 0.0, longest rim arc >= 0.5 peak 9.0 deg | <= 0.35; >= 0.40; <= 0.75; <= 0.10; <= 60 |
| `hull_burst_erodes_outside_in` (PASS) | equal-area radius / R(t) 0.7492,0.5379,0.1640, over its 0.1 L_b value 0.7180,0.2189; mean radius 0.8 / 0.2 0.5707; after L_b 0 (lit share 0.5606,0.2893,0.0269, printed) | 0.35-0.9, <= 0.4; <= 0.8; 0 |
| `hull_burst_no_centre_ring` (PASS; over R_eq) | worst prominence 0.0657, 0.0642, 0.0243; annulus ratio 1.05, 1.06, 1.044 (0.5 / 0.6 / 0.7 L_b) | <= 0.15; <= 1.3 |
| `death_tier_sizes`, `death_big_tier_sizes` (PASS, PASS) | the tongues' front / R(t) at 0.1 / 0.3 L: fighter_600m 1.145 / 0.928; missile_300m 1.167 / 0.971; corvette_1500m 1.101 / 0.911; capital_1500m 1.111 / 1.027; station_3km 1.058 / 1.002 (mean outline fighter_600m 0.722; capital_1500m 0.84 at 0.3 L) | 0.80-1.25 (printed 0.80-1.15 until the 2026-11-08 review; the old gate: the mean outline within ±5 %) |
| `wave_broken_circumference` (PASS) | longest run of bins under 5 % 1 (min bin 0.0), arcs max / min 1000000000.0, CV 0.556 | <= 3; >= 2.0; >= 0.25 |
| `hull_burst_colour_white_core_at_birth`, `_dark_red_at_end` (PASS, PASS) | birth g / r 0.92, b / r 0.3; end g / r 0.319, b / r 0.0 | >= 0.75, >= 0.25; <= 0.35, <= 0.2 |
| `death_body_erodes_outside_in`, `_colour_core_rim`, `death_cools_through_ramp` (PASS, PASS, PASS; fighter 600 m) | equal-area radius 0.9648,0.5242,0.1036,0.0000, ratio 0.5433,0.1074; core T 0.5, rim T 0.25; T at 0.2 / 0.6 / 1.0 L 0.75 / 0.25 / 0.0 | 0.35-0.9, <= 0.45; >= 0.5 and > rim, <= 0.5; >= 0.75, >= 0.25 and <= T(0.2), <= 0.25 |
| `wave_no_hard_edge`, `wave_fire_not_grey` (PASS, PASS) | hard drops 0, rays 30, rises 2, worst fall 0.287; hue at 0.2 / 0.9 L_w 0.5 / 0.25, on the fire ramp 1.000,1.000,1.000 | 0; >= 18; <= rays / 10; <= 0.3; >= 0.5 / <= 0.35; >= 0.9 |
| `hull_plasma_globe_shrinks_reddens` (PASS) | R 49.310,45.807,33.738 px; b / r 0.8855,0.6445,0.6370 at 0.3 / 0.65 / 0.95 L | shrinks, reddens, lit at 0.95 L |
| `fire_no_seam_hull`, `_death`, `_wave` (PASS, PASS, PASS) | median over the seeds 0.0440; 0.0666 / 0.0771; 0.0036 (worst seed 0.1557; 0.111 / 0.1322) | <= 0.1 (median) |
| program slots (device creation, max 4096) | fireball_ps 842, wave_ps 790, ring_ps 292; fireball vertex 92 bytes | the modern cap 32768 |
| `hull_fire_within_weight` (PASS) | tongues on 436.7 us (2.460), off 241.5 us (1.361) at 5120x1440: + 1.100 a screen | within 0.1 of budget_weight_fire - budget_weight_fireball |
| alone rows (5120x1440) | body 210.1 us, wave 335.4 us (one octave 310.4), eye inside a fighter 296.8 us | eye inside <= 300 |
| `death_budget_capital_close_5120` (PASS) | worst 302.1 us (capital_close_300ms, shed 11) | <= 450 |
| `death_budget_5120`, `death_rays_sparks_budget_5120` (PASS, PASS) | fighter 600 m 20.6 us, 20 fighters 49.9 us | <= 60; <= 150 |
| `hull_budget_20_hits_5120`, `hull_budget_capital_close_64_within_budget` (PASS, PASS) | 20 hits 75.0 us, 60 m 661.6 us, capital 64 1331.7 us | <= 80; <= 1420 |

Cost [M] against round 1: `fireball_ps` 769 -> 842 slots (the fine octave, the jitter and luminance, the cluster
thinning), `wave_ps` 781 -> 790, `ring_ps` 292; the tongues + 1.10 a screen (436.7 against 241.5 us, round 1 + 0.89),
`budget_weight_fire` 2.07 -> 2.28; the 20 hits at 200 m 88.2 -> 75.0 us (the tongues' fence: shed farthest first past
0.08 screens of tongue charge, the nearest four kept), at 60 m 805.5 -> 661.6 us, capital 64 hits 1164.5 -> 1331.7 us
(bound 1420); the close capital's worst frame 383.9 -> 302.1 us (shed 11); the wave alone 335.4 us (weights 1.83 / 1.69
pinned); the eye inside a fighter 296.8 us (fence 300). The every-pixel fireball rows read 1.35 / 1.20 a screen against
the pinned 1.18 / 1.03 (`test_weapon_effects_hook` asks 0.15): re-pinned, the close capital's wave was shed on one
frame of seven (`wave_drawn_close_capital` 6 / 7, a record on the re-pin), so the weights stay and a budget decision is
owed. Weights unchanged otherwise.

Records (`run_weapon_effects.py --host native`, RTX 5070 Ti, X4.exe loading the host; the commit is the
`0b782a140`-based worktree with the two merges, dirty): the first 337 of 349 (the colour gates on the old stops, the
fighter body's pre-fire gates under `fire_px` 24 / 40, the wave's colour and edge, the tongues' weight), the second 344
of 352 (the fence without its cluster condition shed a single near burst's tongues), the fourth 350 of 352 with the dump
(the eye-inside fighter 301.8 us and `death_big_stage_cpu`, the host), the fifth 352 of 352 after the eye path's trim,
the sixth lost its device (reset, refusal and timing-target rows together), the seventh the re-pinned weights
(`wave_drawn_close_capital` 6 / 7), the last **352 of 352** [M] (the table above). Host suite
`test_weapon_effects_*`: 99 of 101, failing `test_hull_hit_gates` (the fireball weight, 1.353 against 1.18, failing on the
round-1 record too) and `test_wave_and_compose_gates` (`eye_inside_fighter_n4` 301.4 us against 300: 298.9-304.0 over
the round-2 records, 298.2 on round 1's). CPU rows in band on the last record (`death_big_stage_cpu` 0.0703 us a quad).

Frames (the fourth record's dump; the law since differs only in the eye-inside core's colour, now the top stop):
`<scratchpad>/stage-dump-fire2/` (fighter 300 / 600 m, nested, capital broadside: 5120x1440 engine space and 1080 AgX)
and `stage-dump-fire2/all/` (every case, crop_grid / part_grid layout; the parts as `-agx.png` through the AgX display,
the `-tonemapped.png` beside them keep round 1's 1 - e^-v with an extra 1 / 2.2 encode, which reads the orange stop
olive), raw dumps deleted as drained. Read (AgX): the hull burst a yellow-white core with orange tongues early, orange
tongues with fine streaks over a smooth orange centre mid-life (no peach), dim red late; the 20-hit cluster sparse dark
orange tongues over gaps (no tan body); the capital broadside a yellow-white core with orange tongues, a star of
orange tongues at 2700 ms; the nested fighters at 900 ms orange stars of tongues; the 5 km wave broken amber arcs with
short bright spine segments, see-through, tongues trailing in and licking out. Not flown.

### Review fixes (2026-11-08, worktree on `a4c408e93`, uncommitted at the record)

The round-2 review's findings, each fixed: (1) the tongue blend reads saturate(fire) in the program, the lab and
`fire_model.py` (a cluster burst's weight up to about 1.9 had overshot the n, n_f and flicker lerps; the excess now only
thins the threshold); the lab's 20-hit cluster (`hull/energy-fighter60-20hits`) before / after at 200 / 450 / 800 ms:
tongues at 200 ms slightly sparser, no filled body, the 450 ms ember unchanged, 800 ms pixel-identical [M, headless
Chromium], no retune. (2) `ledger_rows.py`, this ledger's round-2 row and fire-material.md print the fixture's tier-size
bound 0.80-1.25 (the old gate ±5 %). (3) fire-material.md says no run of more than 3 bins; `wave_broken_circumference`
passes on a band bin at 0 or max / min >= 2, printing which (`arc_by`): this record passes by `min_zero` (the ratio's 1e9
stand-in had hidden that). (4) `hull_clear` resets the fence state. (5) boom's last (0c) loop subtracts its cut from
`body_screens`. (6) A shed box (core.z < 0) and the budget's step (1) (screen.w 0) skip the Cartesian fine tap (n_f
1/2): a mul and a min, `fireball_ps` 842 -> 843 slots [M]. (7) A shed burst or body stays shed for its life
(`HullSlot::fire_shed` in the slot's last padding byte, 512 bytes asserted; `Spot::fire_shed`, cleared at a pulse start;
`BoomSlot::fire_shed` bit 0 the body, bit k + 1 secondary k), set by the (0c) step, the fence and boom's last loop (except the newest death's body there: shed per frame,
never latched, the note's rule); no
per-frame allocation (static pointer / index arrays). (8) Budget pins (orchestrator decision): `budget_weight_fireball`
1.18 -> 1.35, `_coarse` 1.03 -> 1.18, `_fine4` 1.18 -> 1.35 (pinned at the three-octave weight), `budget_weight_fire`
2.28 -> 2.46; the charged fence they feed `boom::budget_screens` 2.5 -> 2.86 (x 1.144, the close capital's 450 -> 515 us
charged); `hull::fire_fence` (in the tongues' extra, 1.10 -> 1.11) and the shared `screen_budget` unchanged. The close
capital's wave drawn on 7 of 7 on-screen frames, worst frame 329.3 us (<= 450) [M]. The eye-inside GPU bounds 300 ->
330 us (orchestrator-confirmed; `eye_inside_fighter_n4` 298.2 on round 1's record, 301.4 on round 2's;
`eye_inside_fighter` 283.8 on the record at `9af82ad81`, 296.8 on round 2's, 300.8 on this round's second record): 299.7 / 315.4 us on the last [M]. (9) fire-material.md section 7's stride reads
80 -> 84 as designed, 80 -> 92 as built (12.1 row). SETA: at 100-160 ms steps the flicker and the fine grain are new
noise every frame (no freeze; possible sparkle): flight question 5 (fire-material.md 8, 12.3).

Records (`run_weapon_effects.py --host native`, RTX 5070 Ti, X4.exe loading the host): the first 349 of 352
(`death_budget_5120` eye inside 304.6 us against 300, `death_big_stage_cpu`, `bolt_budget_512_bolts_20_muzzles_5120`
100.9 us against 100), the second 350 of 352 (eye inside 300.8; `death_stage_cpu_32_deaths`), the last **351 of 352**
[M]: `bolt_budget_512_bolts_20_muzzles_5120` 101.1 us against 100 (99.5 on the second; no bolt code changed). Host suite
`test_weapon_effects_*` on the last record: 99 of 101, failing `test_passed_with_the_step_gates` (the bolt row) and
`test_wave_and_compose_gates` (the wave's one-octave weight 1.725 against 1.68 + 0.03; 1.687 on round 2's record, the
wave program unchanged). Both GPU rows under the host's X4.exe load; to re-record on a quiet host.

| Gate | Result [M] (hull; fighter / capital) | Bound |
| --- | --- | --- |
| `fire_luminance_thin_hull`, `_death` (PASS, PASS) | share > 0.8 peak 0.1538; 0.1238 / 0.1131; equal-area disc under 0.1 peak 0.2195; 0.2053 / 0.2046 (the outline's gaps under 0.25 peak 0.1211; 0.1049 / 0.1165, printed) | 0.10-0.35; >= 0.20 (vanilla 0.096) |
| `fire_not_pink_hull`, `_death` (PASS, PASS) | AgX pink share at 0.2, 0.3, 0.5 L 0.0057,0.0001,0.0000; 0.0057,0.0000,0.0000 / 0.0042,0.0000,0.0000 | the medians' largest < 0.02 |
| `fire_quad_edge_dark_hull`, `_death` (PASS, PASS) | the quad's boundary over the peak, worst hit 0.0; 0.0 / 0.0 | < 0.02 |
| `fire_tongues_radial_hull`, `_death` (PASS, PASS) | radial / tangential 0.29; 0.191 / 0.238 | <= 0.75 |
| `fire_edge_licks_hull`, `_death` (PASS, PASS) | lit boundary / outline boundary 2.158; 2.129 / 2.435 | >= 2.0 |
| `fire_temporal_hull` (PASS) | high-pass corr +16 ms 0.846, +100 ms 0.237; low-pass +16 ms 0.998 | 0.5-0.9; <= 0.6; >= 0.9 |
| `fire_colour_ramp_hull`, `_death` (PASS, PASS) | hot 0.39 @ 0.309, red 0.211 @ 0.714, mid 0.391; hot 0.42 / 0.408 @ 0.321 / 0.327, red 0.206 / 0.188 @ 0.749 / 0.746, mid 0.388 / 0.382 | >= 0.25 @ <= 0.5; 0.05-0.25 @ >= 0.65; <= 0.5 |
| `fire_core_bloom_hull`, `_death` (PASS, PASS) | peak inside 0.3 R 3.237 (40 ms), 5.318 / 5.318 (0.1 L); outside 0.5 R 1.242; 1.296 / 1.341 | >= 3.0; <= 1.5 |
| `hull_20_hits_no_white_sun` (PASS) | cores on: worst share above 3.0 0.0 | <= 0.01 (cores off <= 3.0 as before) |
| `fire_small_reads` (PASS) | 30 px far hit at 0.4 L_b: holes 0.6652, lobes 3.573, equal-area radius / R(t) 0.5793 (lit share inside R(t) 0.3348, printed) | >= 0.10; >= 1.2; >= 0.45 |
| `wave_not_a_tyre` (PASS; 5 km face-on, 0.5 tau) | band mean / edge 0.182, dark share 0.676, anisotropy 0.277, grey 0.0, longest rim arc >= 0.5 peak 9.0 deg | <= 0.35; >= 0.40; <= 0.75; <= 0.10; <= 60 |
| `hull_burst_erodes_outside_in` (PASS) | equal-area radius / R(t) 0.7492,0.5379,0.1640, over its 0.1 L_b value 0.7180,0.2189; mean radius 0.8 / 0.2 0.5707; after L_b 0 (lit share 0.5606,0.2893,0.0269, printed) | 0.35-0.9, <= 0.4; <= 0.8; 0 |
| `hull_burst_no_centre_ring` (PASS; over R_eq) | worst prominence 0.0657, 0.0642, 0.0243; annulus ratio 1.05, 1.06, 1.044 (0.5 / 0.6 / 0.7 L_b) | <= 0.15; <= 1.3 |
| `death_tier_sizes`, `death_big_tier_sizes` (PASS, PASS) | the tongues' front / R(t) at 0.1 / 0.3 L: fighter_600m 1.145 / 0.928; missile_300m 1.167 / 0.971; corvette_1500m 1.101 / 0.911; capital_1500m 1.111 / 1.027; station_3km 1.058 / 1.002 (mean outline fighter_600m 0.722; capital_1500m 0.84 at 0.3 L) | 0.80-1.25 (the old gate: the mean outline within ±5 %) |
| `wave_broken_circumference` (PASS) | longest run of bins under 5 % 1 (min bin 0.0), arcs max / min 1000000000.0 (passed by min_zero), CV 0.556 | <= 3; a bin at 0 or >= 2.0; >= 0.25 |
| `hull_burst_colour_white_core_at_birth`, `_dark_red_at_end` (PASS, PASS) | birth g / r 0.92, b / r 0.3; end g / r 0.319, b / r 0.0 | >= 0.75, >= 0.25; <= 0.35, <= 0.2 |
| `death_body_erodes_outside_in`, `_colour_core_rim`, `death_cools_through_ramp` (PASS, PASS, PASS; fighter 600 m) | equal-area radius 0.9648,0.5242,0.1036,0.0000, ratio 0.5433,0.1074; core T 0.5, rim T 0.25; T at 0.2 / 0.6 / 1.0 L 0.75 / 0.25 / 0.0 | 0.35-0.9, <= 0.45; >= 0.5 and > rim, <= 0.5; >= 0.75, >= 0.25 and <= T(0.2), <= 0.25 |
| `wave_no_hard_edge`, `wave_fire_not_grey` (PASS, PASS) | hard drops 0, rays 30, rises 2, worst fall 0.287; hue at 0.2 / 0.9 L_w 0.5 / 0.25, on the fire ramp 1.000,1.000,1.000 | 0; >= 18; <= rays / 10; <= 0.3; >= 0.5 / <= 0.35; >= 0.9 |
| `hull_plasma_globe_shrinks_reddens` (PASS) | R 49.310,45.807,33.738 px; b / r 0.8855,0.6445,0.6370 at 0.3 / 0.65 / 0.95 L | shrinks, reddens, lit at 0.95 L |
| `fire_no_seam_hull`, `_death`, `_wave` (PASS, PASS, PASS) | median over the seeds 0.0440; 0.0666 / 0.0771; 0.0036 (worst seed 0.1557; 0.111 / 0.1322) | <= 0.1 (median) |
| program slots (device creation, max 4096) | fireball_ps 843, wave_ps 790, ring_ps 292; fireball vertex 92 bytes | the modern cap 32768 |
| `hull_fire_within_weight` (PASS) | tongues on 431.8 us (2.433), off 242.2 us (1.365) at 5120x1440: + 1.068 a screen | within 0.1 of budget_weight_fire - budget_weight_fireball |
| alone rows (5120x1440) | body 212.2 us, wave 337.7 us (one octave 317.1), eye inside a fighter 299.7 us | eye inside <= 330 |
| `death_budget_capital_close_5120` (PASS) | worst 329.3 us (capital_close_300ms, shed 10) | <= 450 |
| `death_budget_5120`, `death_rays_sparks_budget_5120` (PASS, PASS) | fighter 600 m 21.8 us, 20 fighters 52.4 us | <= 60; <= 150 |
| `hull_budget_20_hits_5120`, `hull_budget_capital_close_64_within_budget` (PASS, PASS) | 20 hits 77.3 us, 60 m 662.3 us, capital 64 1015.0 us | <= 80; <= 1420 |

### Round 3: turbulence and the collapse (2026-11-08, worktree on `effects` `90988bcdc`, uncommitted at the records)

The tongue law (lab: "it looks like a flower") replaced by a turbulence law (five ridged octaves, polar 1.5 : 1 tongues
on the rim, a connected dense core, colour and luminance from density) and the body's collapse past 0.35 L
(docs/architecture/fire-material.md section 13). Three native records (`run_weapon_effects.py --host native`, RTX 5070
Ti, X4.exe running on the host), the first two with the dump: 335, 344 and **342 of 356** [M]; host suite
`test_weapon_effects_*` 97 of 101 on the last (the four record tests that read the failing gates). Vanilla's figures:
`verification/results/explosion-look/fire_turbulence.py` (`fire_turbulence_vanilla.txt`): hf 0.0235, core 0.408, out
0.0003. The table is `ledger_rows.py --fire` on the third record:

| Gate | Result [M] (hull; fighter / capital) | Bound |
| --- | --- | --- |
| `fire_turbulent_hull`, `_death` (FAIL, FAIL) | AgX luma Laplacian share at 0.3 L 0.017; 0.0694 / 0.0318, at 0.5 L 0.0521; 0.1441 / 0.0614; connected core / disc at 0.3 L 0.346; 0.277 / 0.292; largest other region >= 0.5 peak at 0.3 / 0.5 L 0.0038; 0.0096 / 0.0099, 0.0157; 0.0348 / 0.0158 | >= 0.0188 (0.8 x vanilla 0.0235); >= 0.35; < 0.08 |
| `death_body_collapses` (PASS) | 0.9 L over 0.3 L: radius 0.382 / 0.378, peak 0.039 / 0.04 | <= 0.5; <= 0.3 |
| `fire_luminance_thin_hull`, `_death` (PASS, PASS) | share > 0.8 peak 0.0104; 0.0077 / 0.0051; equal-area disc under 0.1 peak 0.176; 0.1999 / 0.202 (the outline's gaps under 0.25 peak 0.3735; 0.2823 / 0.3106, printed) | 0.10-0.35; >= 0.20 (vanilla 0.096) |
| `fire_not_pink_hull`, `_death` (PASS, PASS) | AgX pink share at 0.2, 0.3, 0.5 L 0.0000,0.0000,0.0000; 0.0000,0.0000,0.0000 / 0.0000,0.0000,0.0000 | the medians' largest < 0.02 |
| `fire_quad_edge_dark_hull`, `_death` (PASS, PASS) | the quad's boundary over the peak, worst hit 0.0; 0.0 / 0.0 | < 0.02 |
| `fire_tongues_radial_hull`, `_death` (PASS, PASS) | radial / tangential 0.68; 0.644 / 0.655 | <= 0.75 |
| `fire_edge_licks_hull`, `_death` (PASS, PASS) | lit boundary / outline boundary 4.22; 4.008 / 4.828 | >= 2.0 |
| `fire_temporal_hull` (PASS) | high-pass corr +16 ms 0.605, +100 ms 0.156; low-pass +16 ms 0.998 | 0.5-0.9; <= 0.6; >= 0.9 |
| `fire_colour_ramp_hull`, `_death` (FAIL, FAIL) | hot 0.376 @ 0.317, red 0.019 @ 0.704, mid 0.604; hot 0.41 / 0.403 @ 0.326 / 0.31, red 0.0 / 0.0 @ 0.57 / 0.519, mid 0.589 / 0.596 | >= 0.25 @ <= 0.5; 0.05-0.25 @ >= 0.65; <= 0.5 |
| `fire_core_bloom_hull`, `_death` (PASS, PASS) | peak inside 0.3 R 3.207 (40 ms), 5.223 / 5.104 (0.1 L); outside 0.5 R 1.198; 1.241 / 1.241 | >= 3.0; <= 1.5 |
| `hull_20_hits_no_white_sun` (PASS) | cores on: worst share above 3.0 0.0 | <= 0.01 (cores off <= 3.0 as before) |
| `fire_small_reads` (PASS) | 30 px far hit at 0.4 L_b: holes 0.479, lobes 1.841, equal-area radius / R(t) 0.7227 (lit share inside R(t) 0.521, printed) | >= 0.10; >= 1.2; >= 0.45 |
| `wave_not_a_tyre` (PASS; 5 km face-on, 0.5 tau) | band mean / edge 0.161, dark share 0.656, anisotropy 0.11, grey 0.0, longest rim arc >= 0.5 peak 4.5 deg | <= 0.35; >= 0.40; <= 0.75; <= 0.10; <= 60 |
| `hull_burst_erodes_outside_in` (PASS) | equal-area radius / R(t) 0.7292,0.7195,0.6669, over its 0.1 L_b value 0.9867,0.9146; mean radius 0.8 / 0.2 0.8946; after L_b 0 (lit share 0.5318,0.5178,0.4446, printed) | 0.35-0.9, <= 0.4; <= 0.8; 0 |
| `hull_burst_no_centre_ring` (FAIL; over R_eq) | worst prominence 0.1045, 0.1806, 0.184; annulus ratio 1.079, 1.08, 1.09 (0.5 / 0.6 / 0.7 L_b) | <= 0.15; <= 1.3 |
| `death_tier_sizes`, `death_big_tier_sizes` (PASS, PASS) | the tongues' front / R(t) at 0.1 / 0.3 L: fighter_600m 1.185 / 1.018; missile_300m 1.182 / 1.1; corvette_1500m 1.068 / 1.008; capital_1500m 1.122 / 1.234; station_3km 1.078 / 1.074 (mean outline fighter_600m 0.864; capital_1500m 1.0 at 0.3 L) | 0.80-1.25 (the old gate: the mean outline within ±5 %) |
| `wave_broken_circumference` (PASS) | longest run of bins under 5 % 0 (min bin 0.109), arcs max / min 72.04 (passed by ratio), CV 0.539 | <= 3; a bin at 0 or >= 2.0; >= 0.25 |
| `hull_burst_colour_white_core_at_birth`, `_dark_red_at_end` (PASS, PASS) | birth g / r 0.92, b / r 0.3; end g / r 0.319, b / r 0.0 | >= 0.75, >= 0.25; <= 0.35, <= 0.2 |
| `death_body_erodes_outside_in`, `_colour_core_rim`, `death_cools_through_ramp` (PASS, PASS, PASS; fighter 600 m) | equal-area radius 1.0312,0.6911,0.0000,0.0000, ratio 0.6702,0.0000; core T 0.62, rim T 0.5; T at 0.2 / 0.6 / 1.0 L 0.75 / 0.25 / 0.0 | 0.35-0.9, <= 0.45; >= 0.5 and > rim, <= 0.5; >= 0.75, >= 0.25 and <= T(0.2), <= 0.25 |
| `wave_no_hard_edge`, `wave_fire_not_grey` (FAIL, FAIL) | hard drops 4, rays 32, rises 1, worst fall 0.523; hue at 0.2 / 0.9 L_w 0.25 / 0.25, on the fire ramp 1.000,1.000,1.000 | 0; >= 18; <= rays / 10; <= 0.3; >= 0.5 / <= 0.35; >= 0.9 |
| `hull_plasma_globe_shrinks_reddens` (PASS) | R 49.310,45.807,33.738 px; b / r 0.8517,0.5566,0.5009 at 0.3 / 0.65 / 0.95 L | shrinks, reddens, lit at 0.95 L |
| `fire_no_seam_hull`, `_death`, `_wave` (PASS, PASS, PASS) | median over the seeds 0.0196; 0.0161 / 0.022; 0.0367 (worst seed 0.1007; 0.044 / 0.1213) | <= 0.1 (median) |
| program slots (device creation, max 4096) | fireball_ps 867, wave_ps 842, ring_ps 292; fireball vertex 92 bytes | the modern cap 32768 |
| `hull_fire_within_weight` (PASS) | tongues on 332.5 us (1.873), off 250.6 us (1.412) at 5120x1440: + 0.461 a screen | within 0.1 of budget_weight_fire - budget_weight_fireball |
| alone rows (5120x1440) | body 215.5 us, wave 355.0 us (one octave 292.2), eye inside a fighter 303.0 us | eye inside <= 330 |
| `death_budget_capital_close_5120` (PASS) | worst 366.1 us (capital_close_2000ms, shed 0) | <= 450 |
| `death_budget_5120`, `death_rays_sparks_budget_5120` (PASS, PASS) | fighter 600 m 23.4 us, 20 fighters 58.6 us | <= 60; <= 150 |
| `hull_budget_20_hits_5120`, `hull_budget_capital_close_64_within_budget` (FAIL, FAIL) | 20 hits 97.4 us, 60 m 922.1 us, capital 64 1527.5 us | <= 80; <= 1420 |

#### Round 3 completion (2026-11-08; fire-material.md 13.3 / 13.5, explosion-look.md section 14 "the inner floor")

The halo's inner floor, the red band, the lab's AgX display and the wave band (the user's four references) on the
WIP commit of round 3. Native runs (RTX 5070 Ti, `run_weapon_effects.py --host native`, development runs with
`X3M_WEAPON_EFFECTS_BOOM_ONLY=1` through `wine_lock.py`): the first full dump record (halo floor, red band at
`fire_white_share` 0.1) 349 of 357; the second (`fire_white_share` 0.5, `fire_lum_0` 0.7, the rim-law wave) **353 of
357**, failing `hull_burst_no_centre_ring` (0.232, re-derived to <= 0.255 since), `wave_no_strip_seam` (0.103 against
the strips' middles' 0.024 on the rim law; the band law 0.074 / 0.033), `death_stage_cpu_32_deaths` and
`death_big_stage_cpu` (66.2 us, 0.098 us a quad; the lab's Chromium shots and the drain ran beside it: a loaded host) [M].
The wave band law's last development run: 87 of 88 boom checks [M], failing `wave_drawn_close_capital` (the band's
charge sheds the wave at the close capital from P + 200 ms: open, an orchestrator decision between the band's width
there, a cheaper close LOD and the 450 us fence). Three later full runs died on the host (the screensaver, then the
locked session: `CHECK device FAIL`), so **no full record of the final constants exists** (until the Record below); the tracked
`summary.json` stays round 3's and the six record-bound host tests fail until the next native record.

| Measure (final constants, development runs) | Result [M] | Bound |
| --- | --- | --- |
| `death_halo_rim` (fighter 600 m) | peak 0.935 / 0.907 R(t) at 0.1 / 0.3 L against the body's lit radius 0.824 / 0.895 (law k 0.955 / 0.908); rings inside 0.5 R(t) 0.35 of the peak; 0.046 / 0.033 at 2 R(t) | within 0.1 R(t) + 1 px; 0.25-0.45; < 0.15 |
| `death_big_halo_capped` (capital 1.5 km, 0.2 L; first dump record) | peak 0.923 of the rim's silhouette (law k 0.931), rings inside 0.5 R(t) 0.35, farthest 10 % at 1.73 R(t) | as the rim gate; <= 2.2 R(t) |
| `death_no_dark_ring` | smallest ring between 0.5 R(t) and the halo's peak over the halo's peak: fighter 300 m 1.45 / 1.37 (0.1 / 0.3 L), capital broadside 2.65 / 1.44 (1900 / 2700 ms) | >= 0.3 |
| `fire_colour_ramp_hull`, `_death` (second dump record) | red 0.180; 0.16 / 0.16 @ 0.73 / 0.71-0.74; hot 0.486; 0.43 / 0.43; mid 0.338; 0.41 / 0.41 | 0.05-0.25 @ >= 0.65; >= 0.25; <= 0.65 |
| `fire_turbulent_hull`, `_death` (second dump record) | core 0.435; 0.325 / 0.360; Laplacian share 0.0187; 0.0771 / 0.0348 at 0.3 L | >= 0.28; >= 0.0141 hull, >= 0.0188 death |
| `death_body_colour_core_rim` | core T 0.75, rim T 0.50 | core >= 0.5 and > rim |
| `hull_burst_contrast`, `_half_hidden_by_lane` | hp std 0.119 / 0.116; at the hit / sky 0.977 | >= 0.086; <= 0.979 |
| `hull_budget_20_hits_5120`, `_capital_close_64_within_budget` (second dump record) | 96.1 / 1530.2 us | <= 107; <= 1680 |
| wave (5 km face-on, 0.5 tau) | band width 0.217 r(t); brightest 0.79-0.83 r(t), outer 10 % 0.93-0.96 r(t); rim arc 44 deg, band mean / edge 0.28, dark share 0.57; rises 30 of 23 rays, worst fall 0.355; hue share 0.469 (mean 40.0 deg); young band G / R 0.518 (orange), old 0.37 (red); seam 0.074 (middles 0.033); edge-on bar 112 px | 13.3's re-derived bounds |
| wave alone 5120x1440 | 373.6 us, weight 2.04 (one octave 1.68) | `budget_weight_wave` 2.0 |

The red band's constants come from `verification/results/explosion-look/fire_red_band.py` (`fire_red_band.txt`; the
replica's old law within 0.02-0.04 of the round-3 record's classes). Lab frames (AgX display) and the native hull grids
are local (`stage-dump-fire4/`); the death grids of the final constants wait for the next dump record.

**Record (2026-11-08; the wave's close-range LOD and gain, the rays' clipped charge, fire-material.md 13.5).** RTX 5070
Ti, `run_weapon_effects.py --host native` through `wine_lock.py` (no dump; 06:26-06:30), the source 32a3ab3fd plus
the orchestrator's five decisions: `ring_prominence_max` 0.255 written into the fixture (ratified in 13.3);
`budget_weight_wave` 2.0 -> 2.05 and `budget_weight_smoke` 1.20 -> 1.39; `wave_e` 3.2 with `wave_fire_hue`'s share
0.43 -> 0.41 (the yellow-white cells read past 40 degrees and pull it down); the rays' charge clipped to the screen
(`boom_trapezoid_px`) and off-screen spark quads uncharged; the fixture's lost-device guard (TestCooperativeLevel after
each step, at each target and before every timing row: `CHECK device_lost FAIL section=<n>` and a non-zero exit). The
D3D fixture **358 of 358**, the hook fixture 147 of 147 [M]. The first record of the LOD (`wave_e` 2.4, 32a3ab3fd) read
355 of 357: `hull_burst_no_centre_ring` 0.2319 against the unwritten 0.198, `bolt_budget_512_bolts_20_muzzles_5120`
155.1 us fenced (timestamps 44.9 us). Here: the prominence 0.2319 (<= 0.255); the bolt row 77.0 + 19.9 = 96.9 us fenced
(<= 100), timestamps 44.9 us; `wave_fire_hue` 0.427 (>= 0.41); the CPU rows `death_stage_cpu_32_deaths` 46.9 us (<= 50),
`death_big_stage_cpu` 38.6 us [M]. Slots at device creation: `fireball_ps` 872, `halo_ps` 90, `wave_ps` 872 (3722
words), max 4096 reported [M]. The wave's weights at 5120x1440 (per drawn screen, the fence removed): full 2.040, the
box 2.041, finest two off 1.687, LOD 1 1.686, LOD 2 1.688; pinned 2.05 / 1.68 / 1.69 / 1.69; the smoke's every-pixel row
248.8 us (1.402; pinned 1.39). The pins sit within +0.03 of the measured rows (the test's tolerance), not on them:
`_one` 1.68 and the smoke's 1.39 are under theirs (1.687, 1.402) [M]. The close capital (720 m): the wave drawn on 14 of 14 on-screen frames, LOD 2 from P,
those frames 252.6-423.8 us; from P + 700 ms the charge 1.76-2.81 screens (2.99-5.69 with the rays unclipped), so the
glows and the body's third octave stay (octaves 4 from P + 1200 ms) and the GPU reads 308.8-441.0 us (221-232 us
before, the worst 441.0 at P + 1100 ms against the 450 us fence: the rays no longer charged past the frame, the frame sits 9 us under
the fence) [M]. The keys and charges per frame: `verification/results/explosion-look/wave_lod_charges.cpp` (output
`wave_lod_charges.txt`, the record's constants). Host: the weapon-effects modules 101 of 101; the full suite 368 modules,
3630 tests, 16 failing modules, every one failing (or skip-only, counted 0 tests) at the parent 09aedec2b too: the engine
effects records' source binding, the motion fixtures' link, `test_regenerate`, `test_exe_identity`,
`test_logging_tiers` (`weapon_effects_patch.cpp`'s frame row, unchanged), the xt transformers [M]. The tables below are the third record's.

**Third record (2026-11-08; the review of 90988bcdc..681122efe, items A-K).** 06:59-07:02 (06:57 refused: another
worktree's host suite held the Wine preflight), the source 681122efe plus: the LOD's cross-fade over the 3-frame hold
(width and finest-two weight lerped on a step counter; LOD level 2 removed: 1.687-1.689 against level 1, untriggerable
with the eye in the plane); the fine octaves' re-seed at least two frame steps (`WeaponEffectsFrame::step_ms`, the lab
the same); octave 3's angular cell floored at 2 px (`weapon_fireball_ps`); the lost-device check before each
mid-section `dev->Reset` and `weapon_effects_fixture.cpp` in the record's hashed sources; `ledger_rows.py` printing
the fixture's current bounds. The D3D fixture **357 of 358**, the hook fixture 147 of 147 [M]. Failing:
`death_body_no_centre_ring` (fighter 600 m, worst hit 0.1931 at 0.5 L against 0.15; 0.1077 on the record before):
octave 3's local-cell floor, fire-material.md 13.5 [I]. The rest as the second record: the close capital's wave 14 of 14
(the fade's steps 1 at P, 3 from P + 100 ms), its frames 252.8-440.5 us (<= 450); `wave_fire_hue` 0.427 (>= 0.41);
`wave_band_width` 0.217; the bolt row 96.9 us fenced, timestamps 44.9 us; CPU 46.7 / 38.4 us; weights full 2.021, box
2.039, finest two off 1.690, LOD 1.689 (pinned 2.05 / 1.68 / 1.69: within +0.03); the smoke's every-pixel row 247.7 us
(1.395; pinned 1.39); slots `fireball_ps` 878, `halo_ps` 90, `wave_ps` 874 [M]. Host: the weapon-effects modules 100 of
101 (`test_passed_with_the_step_gates`, the failing check). Rows from `ledger_rows.py --markdown --fire` and `--rays`
(the fixture's current bounds):

| Gate | Result [M] (hull; fighter / capital) | Bound |
| --- | --- | --- |
| `fire_turbulent_hull`, `_death` (PASS, PASS) | AgX luma Laplacian share at 0.3 L 0.0187; 0.0775 / 0.0348, at 0.5 L 0.056; 0.1649 / 0.0774; connected core / disc at 0.3 L 0.435; 0.325 / 0.36; largest other region >= 0.5 peak at 0.3 / 0.5 L 0.004; 0.0134 / 0.0069, 0.0084; 0.0142 / 0.0173 | >= 0.0141 hull (0.6 x vanilla 0.0235; 2026-11-08, was 0.8 x), >= 0.0188 death (0.8 x); >= 0.28 (was 0.35); < 0.08 |
| `death_body_collapses` (PASS) | 0.9 L over 0.3 L: radius 0.377 / 0.374, peak 0.04 / 0.04 | <= 0.5; <= 0.3 |
| `fire_luminance_thin_hull`, `_death` (PASS, PASS) | share > 0.8 peak 0.0392; 0.0341 / 0.0328; equal-area disc under 0.1 peak 0.1664; 0.1907 / 0.1928 (the outline's gaps under 0.25 peak 0.3158; 0.2437 / 0.2701, printed) | <= 0.35; >= 0.077 (0.8 x vanilla's 0.096; fire-material.md 13.3) |
| `fire_not_pink_hull`, `_death` (PASS, PASS) | AgX pink share at 0.2, 0.3, 0.5 L 0.0001,0.0000,0.0000; 0.0007,0.0000,0.0000 / 0.0006,0.0000,0.0000 | the medians' largest < 0.02 |
| `fire_quad_edge_dark_hull`, `_death` (PASS, PASS) | the quad's boundary over the peak, worst hit 0.0; 0.0 / 0.0 | < 0.02 |
| `fire_tongues_radial_hull`, `_death` (PASS, PASS) | radial / tangential 0.636; 0.612 / 0.61 | <= 0.75 |
| `fire_edge_licks_hull`, `_death` (PASS, PASS) | lit boundary / outline boundary 4.075; 3.913 / 4.658 | >= 2.0 |
| `fire_temporal_hull` (PASS) | high-pass corr +16 ms 0.604, +100 ms 0.176; low-pass +16 ms 0.998 | 0.5-0.9; <= 0.6; >= 0.9 |
| `fire_colour_ramp_hull`, `_death` (PASS, PASS) | hot 0.486 @ 0.386, red 0.18 @ 0.733, mid 0.338; hot 0.475 / 0.473 @ 0.378 / 0.358, red 0.163 / 0.162 @ 0.735 / 0.713, mid 0.368 / 0.361 | >= 0.25 @ <= 0.5; 0.05-0.25 @ >= 0.65; <= 0.65 (2026-11-08, was 0.5) |
| `fire_core_bloom_hull`, `_death` (PASS, PASS) | peak inside 0.3 R 3.21 (40 ms), 5.229 / 5.123 (0.1 L); outside 0.5 R 1.226; 1.271 / 1.272 | >= 3.0; <= 1.5 |
| `hull_20_hits_no_white_sun` (PASS) | cores on: worst share above 3.0 0.0 | <= 0.01 (cores off <= 3.0 as before) |
| `fire_small_reads` (PASS) | 30 px far hit at 0.4 L_b: holes 0.4864, lobes 1.937, equal-area radius / R(t) 0.7179 (lit share inside R(t) 0.5136, printed) | >= 0.10; >= 1.2; >= 0.45 |
| `wave_not_a_tyre` (PASS; 5 km face-on, 0.5 tau) | band mean / edge 0.28, dark share 0.571, anisotropy 0.476, grey 0.0, longest rim arc >= 0.5 peak 44.0 deg | <= 0.35; >= 0.40; <= 0.75; <= 0.10; <= 60 |
| `hull_burst_erodes_outside_in` (PASS) | equal-area radius / R(t) 0.7454,0.7260,0.6677, over its 0.1 L_b value 0.9739,0.8957; mean radius 0.8 / 0.2 0.8833; after L_b 0 (lit share 0.5554,0.5271,0.4458, printed) | radius (px) at 0.9 L_b <= 0.5 x at 0.5 L_b; mean radius 0.8 / 0.2 <= 0.8; 0 (13.3) |
| `hull_burst_no_centre_ring` (PASS; over R_eq) | worst prominence 0.1242, 0.1909, 0.2296; annulus ratio 1.081, 1.086, 1.095 (0.5 / 0.6 / 0.7 L_b) | <= 0.255 (2026-11-08, was 0.15); <= 1.3 |
| `death_tier_sizes`, `death_big_tier_sizes` (PASS, PASS) | the tongues' front / R(t) at 0.1 / 0.3 L: fighter_600m 1.185 / 1.025; missile_300m 1.182 / 1.117; corvette_1500m 1.08 / 1.023; capital_1500m 1.144 / 1.272; station_3km 1.099 / 1.125 (mean outline fighter_600m 0.876; capital_1500m 1.012 at 0.3 L) | 0.80-1.25; the big tiers 0.80-1.35 (13.3; the old gate: the mean outline within ±5 %) |
| `wave_broken_circumference` (PASS) | longest run of bins under 5 % 0 (min bin 0.123), arcs max / min 81.3 (passed by ratio), CV 0.763 | <= 3; a bin at 0 or >= 2.0; >= 0.25 |
| `hull_burst_colour_white_core_at_birth`, `_dark_red_at_end` (PASS, PASS) | birth g / r 0.92, b / r 0.3; end g / r 0.319, b / r 0.0 | >= 0.75, >= 0.25; <= 0.35, <= 0.2 |
| `death_body_erodes_outside_in`, `_colour_core_rim`, `death_cools_through_ramp` (PASS, PASS, PASS; fighter 600 m) | equal-area radius 1.0268,0.6994,0.0000,0.0000, ratio 0.6811,0.0000; core T 0.75, rim T 0.5; T at 0.2 / 0.6 / 1.0 L 0.75 / 0.25 / 0.0 | 0.35-0.9, <= 0.45; >= 0.5 and > rim, <= 0.5; >= 0.75, >= 0.25 and <= T(0.2), <= 0.25 |
| `wave_no_hard_edge`, `wave_fire_not_grey` (PASS, PASS) | hard drops 1, rays 23, rises 30, worst fall 0.355; hue at 0.2 / 0.9 L_w 0.5 / 0.25, on the fire ramp 1.000,1.000,1.000 | <= 6; >= 18; <= 1.5 x rays (2026-11-08, was rays / 10); <= 0.56 (was 0.5); >= 0.5 / <= 0.35; >= 0.9 |
| `hull_plasma_globe_shrinks_reddens` (PASS) | R 49.310,45.807,33.738 px; b / r 0.8299,0.5343,0.4886 at 0.3 / 0.65 / 0.95 L | shrinks, reddens, lit at 0.95 L |
| `fire_no_seam_hull`, `_death`, `_wave` (PASS, PASS, PASS) | median over the seeds 0.0226; 0.02 / 0.0267; 0.008 (worst seed 0.1212; 0.0804 / 0.1512) | <= 0.1 (median) |
| program slots (device creation, max 4096) | fireball_ps 878, wave_ps 874, ring_ps 292; fireball vertex 92 bytes | the modern cap 32768 |
| `hull_fire_within_weight` (PASS) | tongues on 335.0 us (1.887), off 248.1 us (1.398) at 5120x1440: + 0.490 a screen | within 0.1 of budget_weight_fire - budget_weight_fireball |
| alone rows (5120x1440) | body 245.5 us, wave 369.6 us (one octave 310.7), eye inside a fighter 301.9 us | eye inside <= 330 |
| `death_budget_capital_close_5120` (PASS) | worst 440.5 us (capital_close_1100ms, shed 2) | <= 450 |
| `death_budget_5120`, `death_rays_sparks_budget_5120` (PASS, PASS) | fighter 600 m 21.8 us, 20 fighters 57.9 us | <= 60; <= 150 |
| `hull_budget_20_hits_5120`, `hull_budget_capital_close_64_within_budget` (PASS, PASS) | 20 hits 94.2 us, 60 m 919.4 us, capital 64 1538.1 us | <= 107; <= 1680 (2026-11-08 changed-code bounds, were 80; 1420) |
| `death_no_dark_ring` (PASS) | fighter_300m 200 ms 1.45; fighter_300m 600 ms 1.385; capital_1500m_broadside 1900 ms 2.742; capital_1500m_broadside 2700 ms 1.547 | >= 0.3 of the halo's peak |
| `wave_band_width` (PASS; 5 km face-on, 0.5 tau) | the rays' median 10 % width over r(t) 0.217 | 0.2-0.5 |
| `wave_drawn_close_capital` (PASS; 720 m) | drawn 14 of 14 on-screen frames; the LOD's fade step (of 3) 0ms 1, 100ms 3, 200ms 3, 300ms 3, 400ms 3, 500ms 3, 600ms 3; weights at 5120x1440: full 2.021, box 2.039, finest two off 1.69, LOD 1.689 (pinned 2.05 / 1.68 / 1.69) | every on-screen frame; <= pinned + 0.03 |
| `hull_burst_contrast`, `hull_burst_half_hidden_by_lane` (PASS, PASS) | high-pass std energy 0.1186, heavy 0.1162; at the hit / sky 0.9772 | >= 0.086 (was 0.10); <= 0.979 (was 0.97) |

| Gate | Result [M] | Bound |
| --- | --- | --- |
| `death_core_radiance` (PASS; fighter 600 m, 8 seeds) | median radiance inside 0.2 R(t) at 0.05 L 6.984 (seeds 4.113..6.984); `death_body_white_hot_core` median at 0.1 L 4.906 | >= 6 |
| `death_rays_starburst` (PASS; 5120x1440, 4 seeds) | angular peaks at 0.1 L, 300 m: at 1.25 R(t) 21, 17, 23, 15 for 21, 17, 23, 15 rays drawn; at 1.5 R(t) 13, 10, 13, 10; at 0.6 L 0; 600 m (recorded) 19, 18, 18, 15 / 12, 9, 13, 9 | >= 12 and <= drawn + 1 (2026-11-08, from the rim; drawn +- 1 at 0.5 R(t) before); >= 3; <= 2 |
| ray lengths (2026-11-08; 300 m, 0.1 L, part of `death_rays_starburst`) | the three longest over R_f 2.251,1.756,1.351 (rest <= 1.08; 161.6,126.0,96.9 px at 1080 rows); 2.126,1.658,1.276 (rest <= 1.021; 152.6,119.1,91.6 px at 1080 rows); 2.192,1.710,1.315 (rest <= 1.052; 157.4,122.7,94.4 px at 1080 rows); 2.286,1.783,1.372 (rest <= 1.097; 164.1,128.0,98.5 px at 1080 rows); visible tip over length 0.600..0.610 | each >= 1.25 x the next; spread <= 0.15 |
| `death_halo_rim` (PASS; fighter 600 m, the halo alone) | 0.1 L: profile peak at 0.935 R(t), the body's lit radius 0.82 (law 0.955), 0.046 of it at 2 R(t), rings inside 0.5 R(t) 0.353-0.353; 0.3 L: profile peak at 0.907 R(t), the body's lit radius 0.894 (law 0.908), 0.033 of it at 2 R(t), rings inside 0.5 R(t) 0.351-0.351; capital 1.5 km at 0.2 L: peak at 0.923 of the rim's silhouette, 0.037 at 2 R(t)'s, peak 0.619, farthest 10 % pixel at 1.732 R(t) (`death_big_halo_capped` PASS) | within 0.1 R(t) of the lit radius (2026-11-08, was of R(t)); < 0.15; 0.25-0.45 inside 0.5 R(t) (was 0 inside 0.85 R(t)) |
| `death_no_dark_ring` (PASS; 2026-11-08, every part, the core on) | fighter_300m 200 ms: the smallest ring mean between 0.5 R(t) (23.0 px) and the halo's peak (44.5 px) 1.45 of the halo's peak; fighter_300m 600 ms: the smallest ring mean between 0.5 R(t) (32.5 px) and the halo's peak (59.5 px) 1.385 of the halo's peak; capital_1500m_broadside 1900 ms: the smallest ring mean between 0.5 R(t) (109.7 px) and the halo's peak (250.5 px) 2.742 of the halo's peak; capital_1500m_broadside 2700 ms: the smallest ring mean between 0.5 R(t) (141.2 px) and the halo's peak (267.5 px) 1.547 of the halo's peak | >= 0.3 |
| `death_rays_soft` (PASS; 300 m, 0.1 L) | median FWHM across the rays at 1.25 R(t) 6.1 px at 1080 rows (76 rays; per seed 6.11, 6.11, 6.09, 6.1); the worst ray peak over the death under it at 1.25 R(t) 1.061 (per seed 0.919, 0.725, 0.727, 1.061) | >= 4; <= 1.2 (2026-11-08, was 0.4) |
| `death_rays_differ` (PASS) | two deaths' 360-bin profiles at 0.1 L correlate -0.044 | < 0.3 |
| `death_sparks_dense` (PASS; fighter 600 m, 0.3 L) | 221 sparks, 217 drawn, 217 heads on screen, 116 local maxima in the ember part; lengths 16.0 +- 8.4 px (std / mean 0.528) | >= 100; >= 0.4 |
| `death_spark_heads` (PASS; fighter 600 m, 1080p, 0.3 L) | median head FWHM 4.37 px at 1080 rows over 116 image heads (the narrowest of the row, column and diagonal half widths) | >= 3 |
| `death_embers_hidden_in_body`, `death_embers_lengths_and_widths`, `death_embers_random_and_cooling` (PASS, PASS, PASS) | 41 at birth, 207 of 221 once the births are out (0.05 L); widths 3.00..4.98 px; seed correlation -0.173, T 0.75 / 0.5 / 0.25 at 0.3 / 0.7 / 1.1 L, farthest 221.3 px | > 0, >= 80 %; std >= 0.4 mean; < 0.5, >= 0.75 / >= 0.5 / <= 0.3 |
| `death_exposure_meter_shift` (PASS; the production meter's fresh target against the dim and lit scenes alone; fighter 300 m and 120 m, 5120x1440, Auto <= +1.3 EV, gamma 2.2) | worst shift over dim / lit 300 m 0.0 EV, 120 m 0.0 EV (black sky informational, not gated: an empty frame scores 0 EV and any signal lifts it to the cap; 1.3 EV); 300 m: black / dim 0.15 / lit 0.5 worst 0.000 / 0.000 / 0.000 EV (frame peaks 34.69 / 33.69 / 16.67 / 6.33 / 3.17 / 1.3); 120 m: black / dim 0.15 / lit 0.5 worst 1.300 / 0.000 / 0.000 EV (frame peaks 15.59 / 14.27 / 8.61 / 4.13 / 2.83 / 1.24) | 300 m <= 0.5 EV; 120 m <= 1.3 EV (Auto's upper limit) |
| `death_no_pop_at_birth`, `death_big_no_pop_at_birth` (PASS, PASS) | worst frame-to-frame peak rise corvette_1500m 1.264, capital_1500m 1.396 | <= 2 |
| `death_rays_sparks_budget_5120` (PASS) | fighter 600 m 21.8 / 21.2 / 23.2 us at 0.1 / 0.05 / 0.3 L; 20 fighters 57.9 / 49.7 / 61.2 us (spark halvings 2: 787 drawn, 2624 shed, 382 rays); fence 10.8 us | <= 60; <= 150 |
| `death_budget_capital_close_5120` (PASS) | worst 440.5 us (capital_close_1100ms, shed 2) | <= 450 |
| rays alone (5120x1440) | fighter 300 m 15.3 us (23 quads), close capital 26.7 us (23 quads), the fence 10.8 us | |
| `death_stage_cpu_32_deaths`, `death_big_stage_cpu` (PASS, PASS) | 32 fighters 46.7 us, 993 quads (repeats 46.8,46.7,46.7,46.8,46.7; the parent `d3e7698bf` 42.1 us, 588 quads); the big set 38.4 us, 0.0711 a quad | 50; 0.08 |

## Run246 candidate records on the merged tree (2026-11-08)

`effects` `b59a8ca23` (fire material round 3, collapse, wave band, bolt and muzzle re-review, host-suite debts), RTX
5070 Ti, `run_weapon_effects.py --host native` through `wine_lock.py`, run twice: the D3D fixture **367 of 368**
on both, the hook fixture 147 of 147 [M]. The one failure, `bolt_budget_512_bolts_20_muzzles_5120`: 153.0 / 155.1 us
fenced against 100 (bolts 126.1 / 127.6, muzzles 26.9 / 27.5; rims 469 of 512, muzzle sprites 85), its timestamps
42.7 us on both (bolts 40.5 / 41.0, muzzles 2.5 / 2.6) [M]. The bolt branch measured this row at 94.7 us with the same
469 rims, and the fire-material record at 32a3ab3fd read the same 155.1 us once on a tree without the bolt changes
(96.9 us on the next record), so the fenced wall clock is host-state sensitive while the timestamps hold [M; cause
inferred, not isolated]. Bound unchanged. Slots at device creation: `fireball_ps` 872, `halo_ps` 90, `wave_ps` 874,
`shock_ps` 95, `sprite_ps` 240, `bolt_ps` 755 (flag 761), max 4096 reported [M]. `run_engine_effects.py --host native
--programs /mnt/c/X3/x3-modern-captures` (the previous record's program hashes): 299 of 299 over the eight modes [M].
Candidate and install: [build](../../verification/results/run246-candidate-build.json),
[install](../../verification/results/run246-candidate-install.json); open run AP 246 A.
