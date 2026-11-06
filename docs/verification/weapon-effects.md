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
