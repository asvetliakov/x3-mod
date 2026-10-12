# Ship steering: verification ledger

Feature: `--ship-steering` / `X3M_SHIP_STEERING=1` / config key `ship_steering` (default off), with
`--ship-steering-classes LIST` / `X3M_SHIP_STEERING_CLASSES` / `ship_steering_classes` (an exclusion list of `SG_SH`
names). Design: [ship-steering-and-avoidance.md](../architecture/ship-steering-and-avoidance.md) (step 1 of the
ratified plan; step 2, avoidance and site E, is not built). Engine facts:
[ship-movement.md](../reverse-engineering/ship-movement.md), [ship-navigation-ai.md](../reverse-engineering/ship-navigation-ai.md),
[game-scale-and-ship-classes.md](../reverse-engineering/game-scale-and-ship-classes.md).

**What was built (2026-10-12, Fable 5.1).** `src/proxy/ship_steering_core.h` (portable: the shortest-arc rotation
into the engine's basis rows, the rate command `min(W_n, sqrt(a_n·θ), θ/τ)`, the rate servo through the remaining
angle, the arrival/cornering speed schedule, the bank/level roll policy, the 1024-entry ship table, the stuck witness,
the option parsers), `src/proxy/ship_steering_sites.h` (the four `SiteSpec`s, the pinned facts below as comments),
`src/proxy/ship_steering.cpp`/`.h` (four stubs assembled into the DLL's `.text`, continuation slots in its `.data`,
the A handler and the B clamp on the light envelope, the install transaction, the frame-boundary rows), the schema
keys and launcher flags, the host fixture `verification/probe/ship_steering_host.cpp` +
`verification/analysis/test_ship_steering.py`, the site verifier `verification/probe/verify_ship_steering_sites.py` +
`verification/analysis/test_ship_steering_sites.py`, and the six new roots in `verification/probe/check_no_x87.py`.
Nothing was launched; no flight has run.

Sites claimed (all four or none; one `ship_steering_refused reason=` row otherwise): A1 `0x0044ccc0`, A2 `0x0044c620`,
A3 `0x0044c970` (`83 ec 18 8b 02`, `ret 4`) and B `0x004593dd` (`8b 1b 83 3b 00`, followed by `jne 0x00452b60`).
At A the handler writes exactly `obj+0x30/+0x34/+0x38` and returns through `ret 4`; a pass-through runs the original
leaf (EDX restored from the stub frame: the step-2 seam `adjust_target()` may redirect it to the module's target ring).
At B the stub filters class 7 + autopilot in three instructions and the clamp lowers `[obj+0x14]` to the schedule
recomputed at the current distance (floor 5000 units/s, never raised, compared in float before any conversion so a
schedule beyond 2^31 units/s never converts, decisions older than 250 ms ignored, nothing when the record has no
max speed or acceleration). Both handlers pass through while the module is not installed (a site left live by a
failed rollback), and `shutdown()` retries the restore of every site still registered.

**Review fixes (2026-10-12, two independent reviews; decisions recorded in the design note's amendment 4):** orders
4 and 5 write roll 0 (the docking match needs all three angles < `0x51e`); 0x23 MovePosContinue gets the cornering
clamp only; the speed clamp is compared before conversion (`core::clamp_units`); the max-speed getter's result is
used (`v_max = 0` or `a = 0` → no clamp, section 6.2); the heading is held inside the order's arrival distance; the
rollback-failed state gates both handlers and keeps the restore retryable; τ = 0.15 s; the fixture runs a vanilla-leaf
baseline through the same scenarios and asserts the new law is no slower, continues the arrival 10 s past the first
entry, turns 179° with a diagonal axis and the roll policy on, starts the Boreas outside 2 r, asserts roll 0 for
orders 4/5 and the whole order policy; the verifier scans every section at every alignment for dwords into the spans
(102 incoming edges to B: 100 branches, the order jump table's default entry at `0x00459690` and a second jump table
entry at `0x004595fc`, plus the fall-through). Notes: the stubs' `cld` leaves DF cleared (the state compiled callers
assume; documented in the stub comment); `target_depth_` is a round-robin index; the `--debug` sample row is logged
once per written sample; order 0x12 calls the leaf every 200 ms (`0x00456786` skips the handler otherwise), so the
rate sags ≈ 7.5 % of the cap between calls, accepted; DefensiveMove 0x22 adds `f+0x284` to `+0x38` after the leaf
(`0x00455a93`) and stacks on the bank servo as it stacks on vanilla's ±90° bank, accepted.

## Facts pinned by listing (ratification amendment 3; EXE SHA-256 `fdbf3418…`, measured)

| Fact | Listing | Result |
| --- | --- | --- |
| Getter conventions | `0x00450980` `sub esp,8; push ebx; push esi; mov esi,eax … pop edi; pop esi; pop ebx; add esp,8; ret`; `0x00450b80` `movsx eax,word [ecx+0x48] … ret`; `0x00450d60/cf0/dd0` `movzx edx,word [eax+0x48] … ret`; MovePos call sites `0x00456519 mov eax,ebx; call 0x00450d60`, `0x00456544 mov eax,ebx; call 0x00450980`, `0x00456550 mov ecx,ebx; … call 0x00450b80`; `0x0043a6a6` = the class-7 case of the record max-speed switch (record `+0x44`) | **pinned**: obj in EAX for max speed and the three rate caps, in ECX for the acceleration; result EAX; no stack argument; ECX/EDX clobbered (max speed preserves EBX/ESI/EDI). No fallback needed; the handler calls all five |
| Slew order | the order loop's bucket advance `0x004593fd jl 0x00452b42` falls through at `0x00459403` into a second bucket loop `0x00459490..0x004595d5` whose body is the slew (`0x004594ef call 0x00450980`, back edges `0x004595b9 jne 0x004594b0`, `0x004595cf jne 0x00459490`), then the pass's `ret 4` at `0x004595db`; no branch from outside targets the region | **B runs before the slew for every object in the same frame**: a clamp written at B is consumed by that frame's slew (zero-frame latency, better than the design's ≤ 1 frame) |
| Axis sign conventions | `0x0044cede` `neg esi; … fild [esp+0x24] (−R̂); fild [esp+0x10] (F̂); fpatan` → yaw `+0x30 = atan2(−t0, t2)`; `0x0044cf95` `fild [esp+0x14] (U); fild [esp+0x10] (F′); fpatan` → pitch `+0x34 = atan2(t1, t2′)`; roll leaf `0x0044c823` → roll `+0x38 = atan2(t1, t0)` (t = d·rows 0/1/2 of `f+0x870`) | the core expresses every angle in these rows (yaw positive = nose toward −row0, pitch positive = toward +row1, roll positive = row0 toward row1); whether row 0 is "right" or "left" never enters the written angles. World up = +y for the level roll policy stays [i] (first flight) |
| "Large" predicate | `0x0043fd00`: record `+0x14 ≤ 0xf`, byte table `0x0043fd3c` = {0,1,0,0,0,1,1,1,0,1,1,0,1,0,0,0} → `{true,false}` | mask `0xe91d` = {TL, M0, M1, M2, TL_P, M6, M7, TM, M8} = `core::kLargeMask` (level roll policy) |
| Site B census | decoded `.text` branches into `[0x004593dd, 0x004593e2)`: 100 (jcc/jmp), raw encodings 100; dwords equal to `0x004593dd` anywhere in the image at any alignment: 2 (the order jump table's default entry at `0x00459690`, index 21 of 22, and a second jump table's entry at `0x004595fc`); plus the fall-through from `0x004593da`; every target = `0x004593dd` | **102 incoming edges + the fall-through**; no branch or dword into the interior of any span |
| B flags | the span is `mov ebx,[ebx]` + `cmp dword [ebx],0` (the `cmp` rewrites every arithmetic flag before the `jne`) | the stub's own `cmp`/`test` need no `pushfd` (the design's optional drop, proven) |

## Acceptance (2026-10-12, WSL2 host, MinGW GCC 16.2)

| Check | Command | Result |
| --- | --- | --- |
| Build | `cmake --build build 2>&1 \| grep -c warning` | **0** warnings (measured); an unstripped worktree build (its hash carries the dirty source-commit stamp), not a candidate |
| x87 audit | `python3 verification/probe/check_no_x87.py build/d3d9.dll` | **PASS, 0 violations**, 932 reachable functions; roots `_x3m_ship_steering_a0/a1/a2/b_stub`, `_x3m_ship_steering_enter`, `_x3m_ship_steering_clamp` (measured) |
| Site verifier | `python3 verification/probe/verify_ship_steering_sites.py --exe /mnt/c/X3/X3AP.exe` ([output](../../verification/results/ship-steering/verify_ship_steering_sites_out.json)) | **RESULT PASS, 14 checks** (`checks=14` in the summary: identity, PE, the four spans whole/plain/no interior branch, `ret 4`×3, the `jne`, flags dead, incoming census 100 branches + 2 dword edges all exact, no `.text` branch into any interior, no dword in any section at any alignment into a span interior, header specs = derived, getter conventions, slew after B, sign witnesses, large mask `0xe91d`) |
| Host fixture | `PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_ship_steering` ([output](../../verification/results/ship-steering/ship_steering_host.txt)) | **checks PASS, 1164/1164**: the controller port reproduces all 12 settle times and peaks of `axis_controller_sim.txt`; unit checks (atan2 max error 1.5e-5 rad, parsers, table, schedule, servo, stuck, sign witnesses, the order policy over 0..0x30, roll 0 for orders 4/5 from a rolled basis, the hold inside d_arr, `clamp_units` and the two overflow cases: an M5 at 200 km with sin θ ≈ 0.052 and a 720°/s ship at 35 km never write); 5 ships × 3 fps × 3 seeds × {180° turn, 179° diagonal turn with the roll policy on, arrival continued 10 s past the first entry, formation from outside 2 r} each against a vanilla-leaf baseline (dead band, atan2 yaw/pitch, pitch held while |yaw| > 90°, no clamp) + degraded records |
| Tests | `PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_ship_steering verification.analysis.test_ship_steering_sites verification.analysis.test_config_schema verification.analysis.test_launcher_defaults` | 36 tests OK (new: `test_ship_steering_is_an_opt_in`, `test_ship_steering_keys`) |
| Host suite | `/usr/bin/python3 verification/probe/run_host_suite.py` | 295 modules / 2912 tests / 36 failing modules, 53.0 s (rerun after the review fixes), against a baseline run of the pristine `HEAD` export on the same host: 293 / 2903 / 37 failing; **no module fails that passed in the baseline** (the baseline's extra failure is `test_proxy_identity`, which needs a git checkout); the 36 are this WSL2 host's environment (no CrossOver Wine, no bottle, numpy/shader transformer dependencies, GCC 16 `-Werror=misleading-indentation` in an unrelated fixture driver); `test_ship_steering` and `test_ship_steering_sites` pass (measured) |
| Launcher | `python3 verification/results/ship-steering/dry_run.py` (the hermetic form of `tools/manage.py launch --dry-run --ship-steering`: this host has no CrossOver Wine binary, so `WINE` is patched as `test_launcher_defaults.py` does; [output](../../verification/results/ship-steering/dry_run.txt)) | `X3M_SHIP_STEERING=1` reaches the environment; `--ship-steering-classes m1,M2,tl` → `M1,M2,TL`; the default launch sends neither variable; `--vanilla`, an unknown class name and the list without the switch are refused (exit 2) |

Fixture results at 60 fps, seed 1 (all 45 ship/fps/seed combinations pass the same bounds; `ship_steering_host.txt`):

| Ship | 180° turn settle: ours / vanilla / single-axis oracle (≤ 1.02 × vanilla + 50 ms; ≤ 1.15 × oracle) | 179° diagonal settle ours / vanilla | arrival: ours / vanilla time, entry speed ours / vanilla (floor + 20) | entries over 10 s past the first | formation slot RMS, last 20 s (≤ 2 r) |
| --- | --- | --- | --- | --- | --- |
| M5 Discoverer | 3599 / 3699 / 3664 ms | 3316 / 3699 ms | 65.6 / 65.5 s, 10.0 / 11.9 m/s | 1 | 8.0 m (18.4) |
| M3 Nova | 4449 / 4649 / 4624 ms | 4166 / 4633 ms | 93.6 / 93.5 s, 10.0 / 13.8 m/s | 1 | 3.8 m (23.8) |
| M6 Centaur | 10016 / 10016 / 9920 ms | 8950 / 9983 ms | 122.8 / 122.0 s, 10.0 / 23.5 m/s | 1 | 57.7 m (128.8) |
| TS Mercury | 13116 / 13116 / 13040 ms | 11566 / 13083 ms | 165.4 / 164.6 s, 10.0 / 27.2 m/s | 1 | 19.8 m (77.0) |
| M2 Boreas | 27533 / 27533 / 26960 ms | 24683 / 27500 ms | 271.0 / 267.0 s, 10.0 / 70.0 m/s | 1 | 1365.6 m (1410.8) |

Overshoot past the target direction: 0.000° for both laws in every turn (neither crosses the target); yaw-rate sign
changes: 0; in the diagonal turns both axes stay engaged while the bearing is above 100° (min |w_p|/|w_y| ratio
0.64–0.88 ≥ 0.6) where the vanilla leaf holds the pitch at 0 on 60–642 ticks (the yaw-then-pitch sequencing); the
per-axis finish times differ by 28–43 % because the roll policy re-levels the ship late in the turn (reported, not
asserted); yaw reversals in the 5 s before entry: 0 everywhere; the arrival time is within the braking time of the
ramp (vanilla arrives at up to 70 m/s, ours at the 10 m/s floor); 2-second window maxima of the slot error never
increase after the first entry into 4 r. Degraded records: `W_p = 0` drops the pitch axis (handled, yaw carries the
turn), `W_y = W_p = 0` → `no_rotation`, `k = 0` → `no_angular_accel`, `v_max = a = 0` (or `v_max = 0` alone) →
handled with no clamp at all, a 720°/s record never writes beyond the geometric remainder, a non-finite target →
`not_finite`.

Fixture modelling choices (the design's section 7.1 table left them open): the formation leader flies at 0.9 × the
follower's full speed (an equal ship cannot close any gap at full speed) on a circle of `max(5 km, 40 r)` (the
engine's close-in lookahead of 20 r would otherwise exceed the Boreas's circle); the engine's escort close-in branch
is modelled as "point 20 r ahead of the slot along the leader's heading and a linear speed match over 10 × precision"
with the script's precision taken as r/4 (operand roles and the precision are [i] in ship-navigation-ai.md).

Hot-path cost from the code (inferred from the listing, not measured): the A stub is 35 instructions (3 pushes,
16 `movups`, the call, 16 `movups`, 3 pops); the handler executes roughly 600–900 instructions per steered call
(`_x3m_ship_steering_enter` 765 static, `core::steer` 845 static, one `sqrt_ss` per distance/sine/rate, two polynomial
`atan2`, no CRT call), with **five indirect getter calls** (the engine's own leaf routines), two `GetLastError`/
`SetLastError` pairs and, under `--perf`/`--debug`, two `QueryPerformanceCounter` calls; ≈ 0.3–0.5 µs per call at 3 GHz,
≈ 40 calls/s per ship. Site B: 3 instructions for a non-ship object, 5 for a ship without autopilot, and ≈ 100
instructions plus the envelope (≈ 60–100 ns) for a steered ship per frame (`_x3m_ship_steering_clamp` 180 static). The
summary row's `handler_us_p50/p99/max` fields measure it in flight.

## Step 2: predictive avoidance (2026-10-12, Fable 5.1)

Feature: `--ship-avoidance` / `X3M_SHIP_AVOIDANCE=1` / config key `ship_avoidance` (default off, requires
`ship_steering`). Built: `src/proxy/ship_avoidance_core.h` (portable: the per-sector snapshot type, the L∞ pre-filter,
the swept-sphere closest approach over the game-scale `C`/`L` from the live `v, ω, a, r`, up to 8 candidates by
`t_cpa`, the tangent detour in the plane of least turn with the side kept per (ship, obstacle) as a world direction,
capitals over/under with an identity tie-break, the station part model and its 0.35 × sphere fallback, the resolved
set for site E, the emergency ramp when no detour exists), the snapshot builder, the station part walk and the
detour seam in `src/proxy/ship_steering.cpp` (`snapshot_for`, `part_model_for`, `avoid_adjust`), the site-E stub
`_x3m_ship_avoid_e_stub` + `x3m_ship_avoid_suppress` (claimed separately after the four steering sites; a refusal
there leaves avoidance running without the suppression), the `--debug` rows `ship_avoid_detour` (≤ 32/s),
`ship_avoid_census` and `ship_avoid_station` (once per sector per session), the avoidance fields of
`ship_steering_summary`, the schema key and launcher flag, the fixture scenarios (flyby at the design's 600 m and a
150 m close pass, crossing traffic, a formation crossing an asteroid) and the verifier's site-E checks.

**Site E claimed.** Pinned by listing (`ship_steering_sites.h` (e), verifier check `warn_test_site`): the warn test
`0x0044dd90` is stdcall `(A, B, d, &out)` with four `ret 0x10`; exactly three callers reach it (`0x0044e522`,
`0x0044e540` in the proximity handler `0x0044e450`, and `0x00456c02` in the avoid order's handler); every caller
consumes EAX with `test eax,eax; je` immediately after the call (EFLAGS dead), reloads ECX/EDX before any use, and
the proximity handler initialises its `&out` local (`[esp+0x18] = 0` at `0x0044e460`) and never reads it (the true
path appends `d`; the only other `[esp+0x18]` use is the `lea` that passes its address); the avoid handler reads
`[esp+0x18]` only on a true return. So a false return needs no `&out` write: the stub answers a resolved pair with
`xor eax,eax; ret 0x10`. The stub passes through for a ship under order 0x12 (both `0x0044e540` and `0x00456c02`
are that order's paths), for pairs without a fresh (≤ 100 ms) resolved entry, and whenever the module is off.

**Sector snapshot and station part model: pinned by listing (step-2 review), validated at run time.** The bucket
table: `table = [sector+0x50]`, heads `[table + 0xc·k]`, two dereferences (`0x00452b4d mov eax,[ebp+8]; mov ecx,[eax+0x50];
mov ebx,[ecx+edx*4]`, `0x0045d275`, `0x0045cbf4`; verifier check `snapshot_walk_facts`), each list linked at `[obj+0]`
and ended by the node whose first dword is 0; the bucket index is the class word, so only the buckets 5/6 stations,
7 ships, 17 asteroids, 18 gates, 28 debris are walked (sector-collide.md, sector-path-planning.md §3.1). The station's
render node is `[obj+0x70]` (position `+0x30`, basis `+0x40`, 16.16 scale `+0x70`, own radius `+0xa0`, cached subtree
radius `+0xa4` from `0x00488170`); its child list is `c = [node+0xc]; while [c] != 0; c = [c]` (`0x0048aa46`,
`0x00488248`: the 0-dword node is the sentinel, not a part); a child's offset `+0x30..` is multiplied by the parent's
`+0x70` when `[child+0x12c] & 0x40` (`0x004881a9..0x004881e7`), else used as is — the answer to open fact (g). The walk
(`src/proxy/ship_avoidance_walk_core.h`, portable, driven by the fixture over a synthetic sector) builds one part
sphere per child (world centre = station position + basis × scaled offset, radius = the cached subtree radius, the
root's own sphere included) and keeps the model only when every part sphere lies inside the root sphere
(`|c − pos| + r ≤ root_r`, `av::build_parts`); a dirty radius (−1), an empty list, more than 64 children, more than 256
nodes or a failed validation leaves the station on the fallback body (0.35 × sphere, floor 500 m, never suppressed
at E). The part cache is keyed on (object, root radius, position-and-basis stamp), built into a scratch set and copied
on success; a failed rebuild invalidates the stale model. The `--debug` `ship_avoid_station` row (root radius, scale,
child count, the first four offsets/radii/flags, `model=parts|fallback`) and `ship_avoid_census` (class histogram per
walked bucket) confirm the layout in flight. Snapshot: 4 sector slots × 1024 obstacles (fixed DLL globals,
4 × 45,076 B ≈ 180 KB); beyond 1024 obstacles or 8192 visited objects the snapshot is marked overflow and emptied, and
avoidance passes the point through for that sector and frame (`snapshot_overflow` counted);
the bucket walk stops at 8192 objects.

| Check | Command | Result |
| --- | --- | --- |
| Build | `cmake --build build 2>&1 \| grep -c warning` | **0** warnings (measured) |
| x87 audit | `python3 verification/probe/check_no_x87.py build/d3d9.dll` | **PASS, 0 violations**, 939 reachable; new roots `_x3m_ship_avoid_e_stub`, `_x3m_ship_avoid_suppress` (measured) |
| Site verifier | `python3 verification/probe/verify_ship_steering_sites.py --exe /mnt/c/X3/X3AP.exe` ([output](../../verification/results/ship-steering/verify_ship_steering_sites_out.json)) | **RESULT PASS, 16 checks** (the step-1 fourteen plus `warn_test_site`: the function decodes gap-free to `0x0044e442`, 4 × `ret 0x10`, exactly the 3 callers, each `test eax,eax; je` after the call, the `&out` local initialised and unread; the header's `kWarnSite` equals the derived row; and `snapshot_walk_facts`: the double indirection at the three bucket-table readers, the 0-dword child sentinel at both child loops, the `+0x12c & 0x40` scale test and the parent's `+0x70` multiply) |
| Host fixture | `PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_ship_steering` ([output](../../verification/results/ship-steering/ship_steering_host.txt)) | **checks PASS, 3676/3676** (2203 before the Run151 follow-up, 2341 before step 3, 3020 before its review, 3365 before step 4, 3594 before its review, 3595 before amendment 9, the disc-arc model since amendment 10, 3649 before amendment 11, 3667 before its additions): step 1's 1164 plus the step-2 unit checks (the snapshot and part walks over a synthetic sector: bucket table pointer, intrusive lists with the 0-dword sentinel, scaled and unscaled children, a dirty child, a sentinel-only list, a part outside the root sphere, a > 64-child station, the walk limit, no table; the cache by object/radius/stamp and its invalidation; the review's head-on case never suppressed inside D_stop) (the §3.3 formula reproduces the design's L numbers at the design's 0.5 s reaction and the implemented L at 1 s; closest approach on head-on, crossing and clear cases; part validation; E aging; the exclusion rules; overflow pass-through; a constant-side detour with the resolved set; the station fallback never suppressed) and 5 ships × 3 fps × 3 seeds × {flyby at 600 m, flyby at 150 m, crossing, formation + asteroid} |
| Tests | the four modules | 37 OK (`test_ship_steering_is_an_opt_in` and `test_ship_steering_keys` extended for `ship_avoidance`; `test_close_probe_never_suppresses_inside_the_stopping_distance` runs the reviewer's probe `verification/results/ship-steering/close_probe.cpp`) |
| Host suite | `/usr/bin/python3 verification/probe/run_host_suite.py` | 295 modules / 2913 tests / 36 failing modules, 53.3 s (rerun after the step-2 review fixes): the same 36 host-environment modules as the step-1 run and the pristine-HEAD baseline (37 there); no module fails that passed in the baseline; `test_ship_steering`, `test_ship_steering_sites`, `test_config_schema`, `test_launcher_defaults`, `test_logging_tiers` pass (measured) |
| Launcher | `python3 verification/results/ship-steering/dry_run.py` ([output](../../verification/results/ship-steering/dry_run.txt)) | `--ship-steering --ship-avoidance` → `X3M_SHIP_AVOIDANCE=1` with `X3M_SHIP_STEERING=1`; `--ship-avoidance` alone and under `--vanilla` refused (exit 2); the default launch sends neither |

Fixture results at 60 fps, seed 1 (all 45 combinations pass the same bounds):

| Ship | flyby 600 m (design): detours / min clearance (C) | flyby 150 m: detours / min clearance (C) / path × straight / back on course after passing | crossing: detours a/b, min distance (C), sides | formation + 300 m asteroid: leader detours, min distance of leader/f1/f2 (C), final slot error (bound 10 r + 1) |
| --- | --- | --- | --- | --- |
| M5 Discoverer | 0 / 600 m (234) | 261 / 267 m (234) / 1.001 / 66 ms | 802/844, 101.5 m (43.4), −1/−1 | 211, 434/434/434 m (334), 92/92 m (92) |
| M3 Nova | 0 / 600 m (237) | 377 / 308 m (237) / 0.999 / 0 ms | 839/883, 98.6 m (48.8), −1/−1 | 263, 438/438/438 m (337), 118/118 m (119) |
| M6 Centaur | 0 / 600 m (297) | 610 / 350 m (297) / 0.995 / 0 ms | 164/221, 283 m (161), −1/−1 | 424, 516/575/575 m (397), 543/543 m (644) |
| TS Mercury | 0 / 600 m (264) | 914 / 310 m (264) / 1.007 / 9.9 s | 151/160, 205 m (102), −1/+1 | 385, 473/473/473 m (364), 232/232 m (385) |
| M2 Boreas | 2021 / 1406 m (1258) | 2066 / 1417 m (1258) / 0.956 / 0.8 s | 730/757, 2108 m (1764), +1/−1 | 1582, 1531/3070/3069 m (1358), 225/225 m (7054) |

Reading: at the design's 600 m offset the part model lets every fighter and the TS pass the 1.5 km-sphere station
without a detour (the body's near face is 400 m from the line, outside their C), while the Boreas (C = 1258 m) detours
and clears by 1406 m; at 150 m every class detours, clears by ≥ C, keeps one side and returns to course within L (the
TS, the slowest ramp, 9.9 s after passing); crossing ships both detour with the same handedness (the lateral fallback
direction `f × up` is "turn left" for both; capitals one over, one under) and never flip; the formation leader detours
around the asteroid, the followers never avoid their leader, each ship clears the rock by ≥ C, and the followers end
at the engine model's hand-over boundary (10 × precision = 10 r here, asserted as the measured bound, not the 4 r the
design asks): a follower that reaches that boundary misaligned after its own detour is held at the leader's speed by
the fixture's model of the engine's misaligned close-in case ("otherwise the slot at the leader's speed", operand
roles [i] in ship-navigation-ai.md §1.3) and parks there; inside the aligned branch it closes to the slot (the
formation scenario's 2 r RMS). The flight's slot errors after an obstacle decide.

Deviations from the design, with the fixture finding that forced each: (1) the lookahead's reaction term is 1 s (0.5 s)
and L adds the rate-ramp distance `2·v·ω/α` (the design's formula assumes the full rate at once; the TS/M6/Boreas could
not complete a detour inside it); (2) the detour offset is 1.3 C for static bodies and 1.75 C for ships (1.1 C left
2–8 % shortfalls after the servo lag, and two reacting ships need the wider margin); (3) a detour in progress keeps its
conflict until the predicted miss exceeds 2 C and never changes side (the point-by-point re-solve chattered between
the detour and the order's point and clipped the body); (4) the side memory is the detour direction (world), not a
sign on the (f, obstacle) axis, whose orientation flips as the obstacle passes; (5) the plane of least turn is spanned
by f and the direction to the body's current position (its predicted position lies on our own path on a collision
course); (6) the vertical family uses world up (an up component perpendicular to a steeply pitched heading pointed
backward), and head-on symmetric conflicts break the tie by object identity (one over, one under); (7) the station
broad phase is a reach test (the sphere's centre is abeam while parts are still ahead), conflicts that are already
separating are ignored, an obstacle is "passed" only when half its sphere plus the pair's clearance is behind;
(8) the hit part is the body detoured (the design's "far face along ê" would re-derive the same point from the part),
and the detour point and the resolved test are checked against every part of the body, not only the earliest-hit
part; (9) the escort ramp ends at `max(f+0x260, r_own)` (step 1's rule kept; ending it at 10 × f+0x260 parked
followers at the hand-over); (10) a pair is suppressed at E only when its detour is flyable per §3.4/§3.6 — the body
beyond the turn-in distance `sqrt(C(C + 2R))` and beyond the stopping distance `v²/2a` — and the straight path to the
detour point clears its whole body by C (the straight-segment test alone, as first built, suppressed an M5 at 100 m/s
head-on to a ship 55 m ahead: review finding 2, now the probe `close_probe.cpp`); the segment condition holds for a
single sphere only beyond ≈ 2.4 C ahead, so with D_stop (M5: 250 m at 100 m/s, 2573 m at full speed) the suppression
starts well outside the braking distance. Reference values of section 5 therefore shift: L = design + 0.5 v (+ the
ramp term in flight).

Notes recorded from the step-2 reviews (no code change): vanilla warns both sides of a pair and the layer suppresses
only A's side; a ship whose leaf interval exceeds 100 ms gets vanilla's warn in the gap (fail-open); C is 15 m tighter
than vanilla's 40 m inflation for `rA < 50 m` and has no braking term, so the suppressed geometry is a different model,
not a superset of vanilla's (Run B's `e_suppressed`/`e_calls` and the detour clearances decide); the snapshot skips
`0x8000000 | 0x200` and Flags2 `0x20000` while vanilla's pair reject is `0x8200404` (the difference adds obstacles,
never removes any); `[obj+0xf8]` is read for classes 5/6/17/18/28 with the base object layout inferred from the ship
layout; the resolved test measures the straight course to the detour point and ignores the turn arc; the DLL build
has no `-Werror` (only the fixture), as the rest of the DLL.

Review fixes (step 2, two independent reviews, 2026-10-12): the bucket-table double indirection (the first build
dereferenced sector fields as objects; now `table = [sector+0x50]`, verifier-pinned, only the six obstacle buckets
walked, the walk limit = overflow = pass-through); suppression only for flyable detours (above); the part validation
`|c − pos| + r ≤ root_r`, the child walk ending on the 0-dword sentinel (the sentinel's `+0x30/+0xa4` were read as a
part), the parent-scale flag applied; detour point and resolved test against every part; more than 64 children =
fallback body; the part cache built into scratch and keyed on object/radius/position stamp; `avoid_[slot]` reset on
reuse, `suppressed()` checks the state's own ship, `L_leader` only from a fresh decision, the side memory evicts the
least recently used; the fixture's walk double over a synthetic sector, the measured re-form bound, the head-on
regression case.

Hot-path cost from the code (inferred from the listing, not measured): the steering decision now inlines the
avoidance (`decide` 1820 static instructions, `core::resolve` 1548, `steer` 881); per steering call the pre-filter
costs ≈ 6 instructions per snapshot item (420 objects ≈ 2.5 k instructions ≈ 0.8 µs; the 1024 cap ≈ 2 µs), a
conflict adds one closest approach (≈ 60 instructions) per survivor and ≤ 8 candidates, a station with a part model
≤ 64 closest approaches (≈ 1.5 µs); the snapshot is built once per sector per frame by the first call (≈ 30
instructions per bucket object over the six obstacle buckets: 420 objects ≈ 4 µs, plus one part walk per station per
position/radius generation). At 200
ships × 40 calls/s: ≈ 8 000 calls/s × (0.8–2.5 µs) ≈ 6–20 ms/s ≈ **0.1–0.33 ms per frame at 60 fps**, plus ≈ 4 µs per
active sector per frame. Site E: 36-instruction stub + ≈ 50 executed instructions in the lookup (table probe, resolved
set scan) + the envelope ≈ 60–80 ns per warn-test call. The summary's `handler_us_*` fields measure the whole in
flight.

## Run151 flight (2026-10-12, native Windows, the user) and the follow-up (Fable 5.1)

Flight (measured from `/mnt/c/X3/x3m.log` by `verification/results/ship-steering/run151-triage/rows.sh`, output
`rows_out.txt`): 331.8 s, 57,245 frames, 1 device reset, 0 exceptions; `ship_steering installed=1 sites=4`, the four
sites and site E `active`, 0 refused/late/mismatch; 268 summary rows; `calls` 774,850, `handled` 762,538
(`pass_order_not_steered` 7,605, `pass_not_ship` 4,707, every other pass 0; `pass_no_autopilot` 0 settles the
`0x80000` question: NPC ships carry it); handled by order 0x15 309,462 / 0x14 158,039 / 0x4 132,559 / 0x19 88,889 /
0x5 68,227 / 0x12 5,362; `stuck` 0; `clamp_applied` 14,629; ships median 23 (10-31); `calls_per_ship_s` median 152.6
(design 20-60); `handler_us_p50` median 0.8 (max 3.2), `handler_us_p99` median 6.4 (max 25.6), `handler_us_max`
46.4, 109/268 rows with p99 > max; avoidance `conflicts` 59,253, `detours` 34,601, `unresolved` 5,764 (9.7 % of
conflicts, gate <= 5 %), `emergency` 1,172, `e_calls` 1,278,443 vs `e_suppressed` 16,872, `snapshot_max` 152,
`snapshot_overflow` 0, census `5:1 6:9 7:37 17:102 18:3`; `ship_avoid_station` `root_r_m=4609.8 scale=0x000000a5
children=12 model=parts`, the first children `offset:radius:flags` = `0,0,0:-1:01001000`,
`-292437,-1796844,1320:-1:08300000`, ...; decision rows per class TS 104, M1 67, M8 60, M6 20, M3 7, M2 6, TP 3, M4 1
with `k=` TS 0.200, M8 0.100, M6 0.100, M1 0.050, M2 0.050, TP 0.175, M3 0.320, M4 0.200 (the fixture assumed
0.5/0.3/0.1); the `e=` triple is `cmd.e[0..2]` = the yaw, pitch, roll remainders written to `obj+0x30/+0x34/+0x38`
(ship_steering.cpp `S.e[i] = cmd.e[i]`). Four findings, each fixed below:

1. **Per-frame calls.** The think gates pinned in step 1 (escort 0x00458252, follow 0x00458b2e, fly-before
   0x004549e9: `js` to a timeout-only path) gate the order *step*, not the leaf: the handled orders 0x15/0x14/0x4/0x19/0x5
   call the steering leaf every frame (152.6 calls per ship-second at about 173 fps with 23 ships), so the servo's fixed
   50 ms rate surplus (`e = w^2/a + w*tick`) was about 8 frames too long. Fix: `core::servo_tick(gap, frame, run)`: the
   servo horizon is the measured interval since the ship's previous call (`Ship.last_call_ms`, the frame dt from
   `[root+0x714]`), clamped to [one frame, 50 ms], once the ship's cadence is established (8 consecutive gaps within
   1.5 frames, `Ship.per_frame_run`); a gated order (random 0..50 ms gaps) keeps the 50 ms horizon, because its previous
   gap does not predict the next one and a short horizon makes the engine's controller brake to a stop before the next
   call (the fixture with the raw measured gap: 3-62 yaw reversals per 180 deg turn for the M5/M3 at 144 fps; with the
   cadence rule 0). Fixture: the arrival scenario gets a per-frame-order variant (`every_frame`, the dock approach
   and Run151's follow/escort cadence): arrived, one entry, floor-speed entry, no reversal, within 500 ms of the gated
   run (45 cases, all pass).
2. **Unresolved 9.7 %.** `unresolved` counts calls, not conflicts: a lingering conflict whose detour fails stays
   unresolved every frame until it clears. The summary now carries `conflict_episodes` and `unresolved_episodes`
   (an edge per ship, `ShipAvoid.in_conflict`; the 5 % gate applies to episodes) and the per-call reasons
   `unresolved_inside_stop` (the body inside v^2/2a: the emergency ramp applies), `unresolved_inside_turn` (inside
   sqrt(C(C+2R)): no tangent), `unresolved_blocked` (every detour point inside another part or across another
   candidate), `unresolved_behind` (every point behind the ship or past the order's point), `unresolved_degenerate`
   (no detour axis). At 173 fps with per-frame calls the fixture's crossing and flyby would count `behind` or
   `blocked` per call while the ship draws level with the body (the detour point abeam fails the 120 deg test), which
   is the second fix: the detour point stays at least `kAheadS` = 1 s of flight ahead along the heading (the lateral
   offset unchanged). With the measured `k` (M6 0.10, TS 0.20, M2 0.05 instead of 0.30/0.30/0.10) the abeam point also
   made slow-ramping ships overshoot: the M6 flyby at 150 m cut to 218 m (C 296.6) after a 34 deg pitch-down command
   at the next part's CPA 22 m ahead; with the point ahead the clearances are M6 369 m, TS 319 m, M5 301 m, M3 308 m,
   M2 1505 m (seed 1, 60 fps) and `unresolved` ticks <= 5 everywhere. The fixture's reversal metric now ignores rates
   under 0.1 deg/s (18 units): at k 0.05 the integer controller's brake quantum (acc x dt = 4 units per 30 fps frame)
   leaves a -1/+3 ringing after a cleared remainder, which the vanilla leaf never shows only because it writes a
   non-zero remainder down to 0.7 units.
3. **Station row.** `scale` is the station node's `+0x70` (0xa5), printed regardless of the flag; the sampled children
   carry flags `0x01001000` / `0x08300000` (bit 0x40 clear, so no offset scaling applied) and radius `-1`, which the
   old walk treated as "not a body": Run151's `model=parts` was the hull part only (own radius `+0xa0`), so the part
   model under-approximated every station. Listing (settles it; camera-and-lights.md already names the getter):
   `0x004880e0` (node init / set-scale: `+0x70` = the scale argument, `+0x80/+0x84/+0x88` = 0x10000 axis scales,
   `+0xa0` = `+0x70`, `+0xa4` = -1, children rescaled proportionally), `0x00488270` (per-axis scale: `+0xa0` =
   `+0x70` x max axis >> 16, `+0xa4` = -1, recursion into flagged children), `0x00488170` (the subtree radius getter,
   thiscall ECX = node: returns `+0xa4` when >= 0, else the max over the children of |offset x (flag 0x40 ? parent
   `+0x70` : 1)| + child radius seeded by `+0xa0`, stores `+0xa4`; callers 0x004140d9, 0x0041415f, 0x004141a5,
   0x00441c38, 0x00448aef, 0x004491b8). So `+0xa4 = -1` means "never queried", not "being computed", and a station the
   engine has not queried never gets a part radius from the raw field. Resolution (orchestrator decision, same day):
   the walk replicates the getter read-only (`subtree_radius`: a filled cache as is, else the per-axis max over the
   children of |offset x (flag 0x40 ? parent `+0x70` : 1)| + radius(child) seeded by `+0xa0`, depth 3 and 64 nodes per
   station, never calling 0x00488170 and never writing `+0xa4`); a station beyond that depth or budget uses the
   fallback body and is retried (`part_invalid`); the per-axis radius lets a part sit up to sqrt(3) x the root radius
   out in the Euclidean metric, so `build_parts` admits that slack (`kRootSlack`) and the parts must still reach 0.9 x
   the root radius. The row prints `node_scale_0x70`, `own_r_units`, `unfilled`, `scaled`, `extent_units` and per child
   `offset:radius:cache:own:scale:flags` (`+0x30`, the radius used, `+0xa4` as read, `+0xa0`, `+0x70`, `+0x12c`); the
   summary's `part_unfilled` counts stations with a replicated child radius. Fixture: unfilled caches at two depths
   (child own 50 m, grandchild at 150 m with 100 m: the child's replicated radius 250 m); a flagged child under the
   non-unit scale 0xa5 sits where the engine's rule puts it (the centre) and the model is kept; a chain of unfilled
   caches five deep falls back; a collapsed model falls back; the sentinel-only station keeps its hull.
4. **p99 > max.** The percentile interpolation over the bucketed histogram exceeded the exact max in 109/268 rows:
   `handler_us_p50/p99` are now clamped to `handler_us_max`.

Also: the fixture's per-class `k` is the measured set (M3 0.32, M6 0.10, TS 0.20, M2 0.05; the M5 keeps 0.5, not
sampled). Acceptance rerun (measured): build 0 warnings; x87 PASS 939 reachable / 0 violations (one `fistpll` from the
census's float-to-uint32 extent cast appeared and was routed through int32); fixture **checks PASS 2341/2341**; the
four test modules 37 OK; site verifier PASS 16 (unchanged); dry run `X3M_SHIP_STEERING=1`, `X3M_SHIP_AVOIDANCE=1`;
host suite 295 modules / 2913 tests / 36 failing modules, all in the pristine baseline's set (the baseline's 37th,
`test_proxy_identity`, passes now). Not flown: Run151's successor decides the per-frame servo horizon, the episode
gate and the station fallback in flight.

(The interim fallback, before the replication above, was the 0.35 x sphere body of section 3.5: 1613 m for the
sampled station, not its 4.6 km root sphere as the first report said.)

## Step 3: the gate approach (2026-10-12, Fable 5.1; design amendment 7)

Feature: `ship_steering_gates` (bool, default on, effective only with `ship_steering`; `--no-ship-steering-gates` /
`X3M_SHIP_STEERING_GATES=0` turns it off; the launcher sends the value explicitly with `--ship-steering`). Scope: the
ships that are not capital per the engine predicate 0x0043fd50 (class word 7, record `+0x14` in `kCapitalMask`
0x211d: TL, M0, M1, M2, TL_P, M7); capitals keep every vanilla step. Built from ship-navigation-ai.md section 10.7
P1 + P2 + P4 + P5 with the section 10.8 site facts (ship_steering_sites.h (h)-(k)).

1. **Sites pinned and the stub contracts (measured by the verifier, 18 checks PASS).**

   | Site | Window | Facts verified | Stub (live) |
   | --- | --- | --- | --- |
   | P2 `0x00454b91` (`ship_gate_stop_a`) | `89 43 14` `89 43 10` (6 B, two whole `mov`s), then `call 0x00460560; jmp 0x004593dd` | `mov ecx,[ebx+0x50]; xor eax,eax; mov [ecx+0x258],ax; push 1; mov esi,ebx` precede: EAX = 0, EBX = ESI = obj, ECX = flight, flags dead; no branch into the interior (the raw sweep's 0x00454c08 hit is the 0x70 displacement byte of `mov ecx,[ebx+0x70]`, reported and excluded by instruction-start classification); no data dword into the window | `cmp byte [live],0; je tail`; SAVE (EAX/ECX/EDX, XMM0-7, `cld`); `push ebx; call x3m_ship_gate_stop_a; add esp,4`; RESTORE; `jmp [resume = 0x00454b97]`. The handler writes `[obj+0x14] = 0` and `[obj+0x10] = 0` unless the ship is non-capital with a class-0x12 target. ESP exact, EAX/ECX/EDX/ESI preserved |
   | P4 `0x00454700` (`ship_gate_stop_b`) | the same 6 B, then `mov [ecx+0x250],-1` | `mov ecx,[ebx+0x50]; xor eax,eax` precede: EAX = 0 (reused at 0x00454717), EBX = obj, ECX = flight; the join 0x00454710 (from 0x00459191, the only incoming branch) lies after the resume | as P2 with `x3m_ship_gate_stop_b`: `[obj+0x14] = 0`; `[obj+0x10]` kept and recorded (`v0`, stamped) for a non-capital ship at a gate, else zeroed; `jmp 0x00454706` |
   | P5 `0x0045526f` (`ship_gate_launch`) | `b8 e8 67 19 00` (5 B), then `mov edx,[esp+0x10]; mov ecx,0xf4240; imul edx; idiv ecx` (skipped) and the resume `mov edx,[ebx+0x50]` at 0x00455281 | `or [ebx+0x40],0x400; lea eax,[ecx+0x384]; mov ecx,eax; imul ecx,eax; mov [esp+0x10],ecx` precede: EAX = t ms, ECX = `[esp+0x10]` = t^2 (both rewritten after the window), EBX = obj; EDX overwritten at the resume | SAVE; `push [esp+0x88]` (t), `push ebx`; `call x3m_ship_gate_launch; add esp,8`; the result stored over the saved EAX; RESTORE; `jmp 0x00455281`. EAX = `v0 t / 1000 + t^2 x 1665000 / 1000000` (int64, v0 = the speed kept at the pass point within the last 5 s, else 0 = the engine's own value) |

   All three are claimed as a group after the steering sites and site E (every span preflighted, claims in table
   order, every claimed gate site restored in reverse on the first failure); a refusal logs
   `ship_steering_gates_refused reason=…` and leaves the gate policy off with steering and avoidance untouched; a
   failed rollback leaves the stubs chained but passing through (the live byte stays 0). Shutdown restores the group
   first. The handlers run on the light envelope (MXCSR + LastError), no log, no allocation, no lock; the capital
   test replicates 0x0043fd50 from the record (the verifier derives 0x211d from its byte table). P1 lives in the
   decision: for order 4 with a class-0x12 target the gate frame (`[gate+0x70]+0x30` position, `+0x50/+0x60` rows 1
   and 2), `[gate+0xa4]` and the engine's own point A (f+0x240, read through the leaf's EDX) are validated against
   `A = G + R(0, -0.8, -1) r` within 2 % of r (else the vanilla point); the lead-in direction is
   `h = sin 23.6 deg row1 + cos 23.6 deg row2`, the crossing point `X = A - 0.75 r h`, the carrot
   `X + h (s + max(0.75 s x v, 0.3 r))` with s the ship's projection from X (the ship aims at A itself more than 3 r
   before X); the point goes through the ring-buffer redirect, f+0x240 untouched; no arrival ramp; the speed at site B
   is `gate_speed` = vmax (100 m/s for large hulls, the engine's own order-4 cap 0x004550ed), lowered by the
   cornering term, written in both directions (`Ship.flags` bit 2, `v_ref` carries it).
2. **Fixture numbers** (`verification/results/ship-steering/ship_steering_host.txt`, **checks PASS 3020/3020**; the
   gate scenario: M5/M3/M6/TS x 3 fps x 3 seeds x theta in {-45, 0, +45} deg, from 3 km at 30 deg off the axis, the
   vanilla run beside each; the M2 as the capital case). Seed 1, 60 fps:

   | Class | Crossing speed (vanilla) | Heading error to B at the crossing (vanilla) | Order-5 creep ticks (vanilla) | Min speed crossing to B | v0 kept (vanilla) | Crossing to warp (vanilla) |
   | --- | --- | --- | --- | --- | --- | --- |
   | M5 | 532.0 m/s (209.6) | 4.2-8.4 deg (19.1-22.5) | 0 (9-19) | 409 m/s | 409 (0) | 3.3-4.7 s (8.0-9.9) |
   | M3 | 247.5 (50.0) | 4.0-6.1 (19.1-22.5) | 0 (14-29) | 124 | 124 (0) | 8.7-11.9 (11.7-15.1) |
   | M6 | 100.0 (37.1) | 3.9-6.4 (19.1-22.5) | 0 (40-84) | 90 | 90 (0) | 12.3-16.7 (16.4-21.2) |
   | TS | 100.0 (42.2) | 4.2-5.7 (19.1-22.5) | 0 (35-75) | 62 | 62 (0) | 17.1-23.5 (19.3-26.0) |

   Every case: crossing speed >= 0.5 vmax, heading error <= 18.2 deg, 0 creep ticks, min speed between the crossing
   and the pass point >= 0.25 vmax, v0 > 0 with the first launch frame at >= v0; the M2 stops at A, uses no lead-in
   and launches from rest (27 cases). The engine model: order 4 with the leaf every frame and the arrival test /
   speed law at the think tick, one frame of order 0 per hand-over, order 5 every frame with the 0.95 alignment
   test and the r/5 -> r ramp, the arrival at |v|/10 + r_ship, order 8 for 900 ms along the axis.
3. **Verifier / x87 / tests (measured).** `verify_ship_steering_sites.py --exe /mnt/c/X3/X3AP.exe`: PASS, 18 checks
   (the three gate windows in `sites`, `gate_sites` with 16 byte facts and the three resumes as instruction starts,
   `capital_predicate_mask` 0x211d, `header_specs` over `kGateSites`); `check_no_x87.py build/d3d9.dll`: PASS 946
   reachable / 0 (roots: the three gate stubs and handlers; the float-returning core helpers are `always_inline`,
   an out-of-line float return travels through st(0)); build 0 warnings; the four test modules 37 OK (config schema
   and launcher cases for the key and the flags); host suite: see the acceptance line below.
4. **Deviations.** (a) P1's "vanilla speed passed through" is not implemented as written: the engine's order-4 law
   to a gate ramps the desired speed to 5000 units/s at A (0x00455052; the fixture's vanilla crossing speeds above,
   37-210 m/s), so the acceptance gate "crossing speed >= 0.5 vmax" is unreachable with it; the module writes its
   own approach speed at site B in both directions for this one case (the only raise site B performs; everything
   else stays lowering-only). (b) The heading error is measured to the actual pass point of the run (theta drawn per
   run), not to the 23.6 deg line. (c) Site P3 and the order-0x1f tolerance (optional in the study) are not
   claimed. (d) The `ship_gate_approach` row is written at the launch (when the crossing-to-warp time exists), at
   most 8 per frame boundary; a ship that never launches leaves no row.

5. **Review fixes (2026-10-12, two reviews; measured, fixture 3365/3365).** (1) `gate_lead_in` applies only in the
   window -3 r <= s <= 0 along h (in front of the crossing plane); beside / behind / above / laterally off at A's
   depth give s > 0 and aim at A; the carrot never passes A; the far end is blended between 3 r and 2 r (the aim
   direction's largest per-frame jump in the far-start case: 0.1-1.0 deg). Fixture starts added (each with the
   vanilla run beside it, arrival at A within 1.3 x the vanilla time, no path through the ring [0.55 r, r] during
   the approach): beside 3 r, behind 2 r, above 3 r, laterally off 3 r at A's depth; the far start (6 km, 20 deg,
   through the 3 r switch) in a rotated gate frame (axis +x, up +z): crossing 5.5-5.6 deg, no creep, t_A M5 10.8 s
   (vanilla 37.0), M3 23.3 (31.3), M6 56.2 (61.0), TS 57.6 (56.0, the 100 m/s cap); the lateral approach 3 r off the
   line at the join depth: crossing 5.5-5.6 deg (vanilla 23.1), creep 0 (vanilla 23-105), t_A M5 9.9 s (vanilla 32.5),
   M3 21.4 (28.5), M6 51.4 (54.1), TS 52.8 (49.1); a mismatched A (5 % of r off) refuses the lead-in. The reviewers'
   probes are kept as regression witnesses: `verification/results/ship-steering/gate_starts.cpp` and `lead.cpp`.
   (2) `clamp_for`: the gate branch now feeds the same emergency check as the schedule (unit check: gate speed 532 →
   25 m/s under an emergency), and the decision leaves `v_ref` alone under an emergency. (3) P2 keeps the speed only
   with a gate state from the decision (`core::gate_stop_keeps_speed`), as P4; the predicate is the shared
   `core::gate_target_noncapital` over the memory reader (unit-checked on a synthetic ship / flight / gate / record:
   M5 kept, M2 refused, a non-gate target refused, no table refused). (4) The push-ahead re-checks the segment to
   the moved point against the body it was built for (`clears_body`) and keeps the un-pushed point otherwise.
   (5) The envelope is stated in amendment 7 and above; the fixture covers the lateral approach (in front: joined;
   at A's depth: vanilla-like). (6) The fixture starts before the 3 r switch (far start), uses a rotated frame,
   rejects a mismatched A, and drives P2/P4/P5 through the shared rule and `gate_launch_units`. Notes: a negative t
   at P5 is clamped to 0 (the engine squares it; t is never negative at the site: `lea eax,[ecx+0x384]` with
   ecx >= -900); a negative `+0xa0` seed is clamped to 0 in the replicated getter (the engine's `esi` would carry
   it); the unused site constants were deleted; an empty `X3M_SHIP_STEERING_GATES` means on; `servo_tick`'s
   1.5-frame tolerance can latch a 33 ms horizon for gated orders at <= 45 fps (0 reversals at 30 fps in the
   fixture); the engine's 1 m/s alignment rule on order 4 is intentionally replaced by the cornering term on gate
   approaches; AI ships' bit 2 for CanWarp and KC class 2027's identity remain inferred.
## Run152 follow-up and Step 4: gate crowding and the capital approach (2026-10-12, Fable 5.1; design amendment 8)

**Run152 (measured by `verification/results/ship-steering/run152-triage/rows.sh` in the main checkout):** the gate
sites armed (3 sites active), `gate_p2` 73 / `gate_stops_skipped` 47, `gate_p4` 76 / `gate_v0_kept` 47,
`gate_launch_continued` 47 (v0 46.7-213.9 m/s), `gate_lead_calls` 59,836, `gate_crossings` 0, every
`ship_gate_approach` row `cross_v_ms=-1`; decision samples: order 5 at 1.66-1.68 km from B with 61-70 deg heading
error and 1.0-22.5 m/s (the creep); order-4 samples near the end at d_m 346-368 m (the carrot 0.3 r ahead) and 22 deg
off it; 27 of 54 order-4 samples with d_arr 0 (the gate policy engaged).

1. **What the listing says B is.** The order-5 gate-point construction (`SA_FlyObjectIntoContainer`, 0x004608f7..
   0x00460a4f, measured): theta = rand(0x4000) - 0x2000 (binary angle, +-45 deg); two quadrant-folded lookups in the
   0x10000-entry table at 0x596998 (`T[0x8000 + theta]` for the first, `T[0xc000 - theta]` for the second, negated
   in the far quadrants); the local point is (x0, y0 x second, -(y0 x first)) with (x0, y0, z0) = `[gate+0x50]+0x190`
   = (0, -0.55, -0.01) (0x00440e44..0x00440e68; -0.05 for subtypes 5..8), scaled by `[gate+0xa4]` (0x40e780), rotated
   by the gate frame as rows (0x4f0da0: out = sum of v_i x row_i, 0x00460a31) and added to the gate position. The
   table is filled at run time (its file bytes are not a trig table); the same lookups build a horizontal direction
   vector from a yaw at 0x004222a0 (components (first x L, 0, second x L)), which identifies T[i] = sin((i - 0x8000)
   turn/0x10000) [i]: first = sin theta, second = cos theta, hence **B = G + R (0, -0.55 cos theta, 0.55 sin theta) r**,
   as section 10.5 and the fixture assume; A = G + R (0, -0.8, -1) r through the same rotate and scale (0x00460def..
   0x00460e1e). The geometry model is not the discrepancy. Nor is the missing crossing an explanation of the 61-70 deg
   (the engine's order-4 arrival test runs at the think tick before the frame's leaf call, so the decision never saw
   d <= 0.75 r: that only hides the crossing from the counters): a ship that flew the lead-in crosses within 5.5 deg
   of the pass arc, and even a 22 deg lateral error at the crossing (the samples near A) puts it at most ~28 deg off B.
   61-70 deg means those ships did not fly the lead-in at all (the vanilla point: the container detour of section
   3.4 handing the leaf another point, or a frame that failed validation); the transition row's `validated` and
   `last_dA_r` fields decide that in flight.
2. **Order-5 cornering (fixture, measured, 60 fps seed 1; the start 1.7 km from B, 65 deg off, the vanilla run beside):**

   | Class | theta | min speed (vanilla) | t_B (vanilla) | creep ticks (vanilla) |
   | --- | --- | --- | --- | --- |
   | TS | -45 / 0 | 62.0 m/s (1.0) | 20.2 s (24.7) | 445 (364) |
   | M6 | -45 / 0 | 87.7 / 90.0 (17.0) | 16.3 (21.0) | 745 / 648 (489) |
   | M4 | -45 / 0 | 90.0 (90.0) | 13.2 (14.0) | 155 (151) |
   | TS / M6 / M4 | 0 / +45 (B on or behind the plane) | = vanilla: the ring test keeps the creep | = vanilla | = vanilla |

   (creep ticks count the engine's misaligned frames, which the module overrides each frame; the module's path adds
   no ring sweep the vanilla path does not make.) The theta < 0 cases (B in front of the plane) hold the speed >= 20
   m/s; with B on the plane (theta = 0) the arc's edge is the 0.55 r pass arc itself, so the override stays off.
3. **The diagnostic fields.** `ship_gate_transition frame obj class capital validated=matched|fallback|none crossed
   last_dA_r err_B_deg v_ms desired_ms B_gate_r=x,y,z A_gate_r=x,y,z dock_blocked`; `ship_gate_approach` adds
   `capital ring offset_m=x,y`; the summary adds the counters listed in amendment 8 (6).

**Step 4.** (1) Per-ship offset rule: ring index `(obj x 0x9E3779B1) >> 29` (0..7), offset 0.25 r at 22.5 + 45 k deg in
the gate's x-y plane added to X (the line's start); the heading-error bound 13.9 deg (geometric, 0.25 r;
`spread_bound.py`); fixture (measured, 60 fps seed 1, theta -45): four ships on the same vanilla line cross 191-193 m
apart (>= 2 r_ship for every class), each 826-829 m from A (<= 0.75 r = 829), heading error to the pass point M5
17.7, M3 14.0, M6 14.9, TS 13.4 deg (the M5's servo lag adds to the geometric 13.9; all 36 cases <= 18.2). (2) Exit
write: pinned as (l) above; site W claimed as the fourth of the gate group (verifier: window, registers, the copy
with `test eax,eax` after it, the class-0x12 test, the case's and the handler's writes); fixture: two ships a second
apart keep their 0.3 r_gate ring offsets through the 900 ms animation, >= 2 r_ship apart laterally, the offset in the
gate plane only. (3) Capital alignment angles: (m) above; fixture (measured): the M2 from 6 km at 30 deg off the
axis to grid slot 7 (identity frame) and slot 0 (rotated frame) arrives with 2.4 / 2.1 deg to the axis (vanilla
24.2 / 30.6), speed 0 after A, launch from rest, t_A 80.7 / 87.7 s (vanilla 74.0 / 78.9: the lateral join). (4)
Fixture **checks PASS 3594/3594**: the far start in the rotated frame 8.1-8.2 deg (aim jump 3.1 deg with the spread),
the lateral approach 7.9-8.1 deg (vanilla 23.1). (5) Verifier PASS 18 (site W and the (l)/(m) facts added to
`gate_sites`); x87 PASS 951 / 0 (`clamp_for`, `schedule_speed`, `gate_dock_ramp` `always_inline`); build 0 warnings;
the four test modules 37 OK; dry run unchanged; host suite: see the acceptance line.

**Step 4 review fixes (2026-10-12, two reviews; measured, fixture 3595/3595, x87 PASS 953 / 0, verifier PASS 18).**
(1) The approach verdict is per approach: the gate state carries the target pointer, is reset when the target changes
or the ship leaves the gate's 2 r sphere, and `validated` is recomputed on every order-4 call; P2 and P4 keep the
speed only with `validated == 1` (the fixture's mismatched-A case now asserts the vanilla stops at A and B, no v0).
(2) Order 5 validates B against the pinned pass point (B - G in gate rows within 3 % of (0, -0.55 cos theta, 0.55 sin
theta) r, |theta| <= 45 deg), the rows' orthonormality and r_gate > 1 m; gate subtypes 5..8 and anything else keep the
engine's creep (`gate_dock_invalid`); `kGateHoleFraction` = 0.55 (the pass arc). (3) Site W's rule is the shared
`core::gate_warp_in_offset`: class-7 non-capital, the record class not in `ship_steering_classes`, the autopilot flag
set (never the player's ship), not deleted/attached, not already on order 0x21 (a re-issued `SA_WarpObjectIn`), a
class-0x12 target with an orthonormal frame, the int32 sum guarded (`gate_warp_skipped` counts refusals); all three
rows validated in `gate_geometry` too. (4) The fixture drives that rule on a synthetic ship/gate (the written offset,
the case's copy and write, two ships >= 2 r_ship apart; the player, a deleted ship, a capital, a non-gate, a re-issued
warp-in and an excluded class get nothing), the dead capital block is gone (the 6 km capital cases are the capital
assertions), the P4-fallback case is in. (5) The `ship_steering_decision` sample adds `a_dps2` per axis, `f2b0` (raw
16.16), `rot_pct`, `omega_cmd_dps` per axis (W, the current rates, e and `[obj+0x10]` were already there). Listing:
the rate getters do not scale with speed: 0x00450d60 (yaw) returns record `+0x8 x (1 + f+0x270)` for class 7 (a flight
modifier) and the record field otherwise; pitch 0x00450cf0 and roll 0x00450dd0 have the same shape (section 11 of
ship-movement.md); the integrator at 0x0040eb60.. clamps the current rate `[obj+0x24]` to +- the getter's value and
uses the sum of the three caps x 0.4999 only for its own step, no speed factor; nothing in the order-5 handler scales
them. The 2.6 deg/s turn is therefore not a cap effect; the new sample fields (`a_dps2`, `omega_cmd_dps`, `f2b0`)
show in flight whether the commanded rate or the angular acceleration (f+0x2b0 x +0xd0 / 100) was small; the
cornering override removes the dependence on that turn either way. Notes: the dock floor never exceeds the engine's
aligned value (N1, fixed); the arc's R and atan2 are computed once per call, 8 samples per mode (N2, fixed); "four
gate sites" (N4); `gate_rows_dropped` counts the diagnostic rings' drops (N5); the M5's spread heading error 17.7 deg
sits 0.5 deg under the creep threshold (the transition row's `err_B_deg` decides whether the M5 ring needs 0.20 r);
capitals reach their grid point 9-11 % later than vanilla (the lateral join at the gate speed).

**Amendment 9 (2026-10-12; measured, fixture 3649/3649).** The order-5 override now covers the whole pass arc
with the same speed law everywhere; the chord test against the ring's hole bound 0.60 r (section 10.9: stock
0.605-0.654 r, installed Terran 0.663 r, installed Mayhem gates a collision-free bubble) is a diagnostic only
(`dock_blocked` in the transition row, `gate_dock_blocked`): the pair filter 0x0045c7b3 exempts the ship-gate pair
on order 5, so it never lowers the speed (holding the floor while the chord crossed the band made the theta +45 legs
1.7-2.3 x slower than vanilla: TS 56 s vs 24.7, measured, rejected). The cornering floor is 20 m/s (capped by the
engine's aligned ramp): near B a still-misaligned ship's chord term collapses (the pursuit's singularity) and the
engine's arrival radius |v|/10 + r_ship absorbs the small miss. Subtypes 5..8 validate against their 0.05 r pass arc.
Fixture (60 fps seed 1, the order-5 start 1.7 km from B at 65 deg; min speed / t_B, vanilla in brackets): TS theta
-45 / 0 / +45: 62.0 m/s (1.0), 20.2 s (24.7) for all three; M6: 87.7 m/s (17.0), 16.3 s (21.0); M4: 90.0 (90.0),
13.2 s (14.0); subtype 5 (theta 0, B at 0.05 r): TS 62.0, M6 87.7, M4 90.0 m/s. The fixture's gate model changed
only in the pass vector per subtype (`gate_pass_y0`) and in the dock scenario's assertions (cornering result for
every theta, the ring sweep no longer asserted on the order-5 leg); the approach starts keep their ring check at
0.65 r. Verifier PASS 18, x87 PASS 953 / 0, build 0 warnings, tests 37 OK.

**Run154 follow-up, amendment 10 (2026-10-12; measured).** Run154 (`run154-triage/rows_out.txt`): 91 transitions,
all `validated=matched`, B − G in gate rows = (±0.55 sin θ, −0.55 cos θ, −0.01) r on every one (hypot(x, y)
0.544–0.556 r, z = −0.01 r), so `gate_dock_invalid` 26,423 vs 176 writes; crossing heading 35° off h (median); the
M1 leader 263 s on order 4 at 3.1–11.4 km (`gate_capital_aligned` 0), 453 detours against its own wingman; 8 false
stuck rows; `gate_warp_calls` 0 (no warp-in reached site W in 411 s: noted, unchanged).
1. **B's axes (listing, measured):** at 0x004609d5..0x004609e4 the first `MulFix(y0, sin θ)` is negated and stored
   through `[esp+0x20]` with four pushes live, i.e. the local x; the second `MulFix(y0, cos θ)` lands in the local y;
   z keeps the pass vector's −0.01: B = G + R (0.55 sin θ, −0.55 cos θ, −0.01) r, an arc on the gate disc (section
   10.5 corrected). The sign of x against θ follows the run-time sin table [i]; Run154 shows both signs.
2. **Fixture (measured, `checks PASS 3649/3649`, 60 fps seed 1; vanilla in brackets).** Order-5 legs to the disc
   arc (the start 1.7 km from B at 65° off): TS min 62.0 m/s, t_B 19.8–20.2 s (1.0 m/s, 24.2–24.7 s) at θ −45 / 0 /
   +45 and subtype 5; M6 87.7–88.3 (17.0), 15.6–16.3 s (19.9–21.0); M4 90.0 (90.0), 13.1–13.2 s (13.8–14.0). The
   lead-in crossing error to B with the 14.17° line: θ −45 / 0 / +45 = M5 9.2 / 8.4 / 20.9°, M3 9.9 / 8.0 / 20.5,
   M6 8.6 / 8.8 / 21.3, TS 9.6 / 8.1 / 20.6 (vanilla 33.3 / 19.0 / 13.7); the far start in the rotated frame 8.2°,
   the lateral start 8.0–8.2° (vanilla 22.4); the θ = +45 legs now corner at ≥ 62 m/s where they crept before (21–99
   engine-misaligned frames overridden). Spread: four ships 204–206 m apart, 827–829 m from A, worst error to the
   pass point M5 22.1°, M3 21.2, M6 19.6, TS 20.6 (the bound 20.7° geometric, `spread_bound.py`: 13.0° on the line,
   17.6 / 19.1 / 20.7 / 22.3° at 0.15 / 0.20 / 0.25 / 0.30 r; the fixture allows 24° for the servo lag). Capitals:
   the vanilla path (t_A and the arrival heading equal to the vanilla run's).
3. Verifier PASS 18, x87 PASS 953 / 0, build 0 warnings, the five test modules (with `test_ship_trace_format`) OK.
4. Deviations: the spread's 20.7° (22.1° measured for the M5) exceeds the old 18.2° creep threshold by design (the
   order-5 cornering absorbs the lateral error); the capital alignment is withdrawn (vanilla) until a design that
   respects the 222 m arrival radius; `gate_warp_calls` 0 in Run154 is noted, site W unchanged.
5. **Item 7, `ship_trace` (the user's decision; `--debug` tier).** Schema keys `ship_trace` (bool, default off; the run
   candidate sets it on) and `ship_trace_max` (1..16, default 6). The module follows the first N non-capital class-7
   autopilot ships that enter a gate approach (order 4 with a class-0x12 target) or a dock approach (order 4 with a
   class-5/6 target, or order 5); a slot is freed when the ship launches (P5), docks (`[obj+0xf8]`, checked before
   site B's docked return), takes an order outside 0/4/5/8, or is not seen for 5 s. One row per frame per ship,
   pushed at site B after the module's speed write (before the slew) into a 128-row ring drained at the frame boundary
   (`ship_trace_dropped` counts overflow); the hot path writes the row struct only. Row: `ship_trace frame t_ms obj
   class order tgt tgt_class pos=x,y,z fwd=x,y,z up=x,y,z rate=y,p,r v des des_engine point=x,y,z given=x,y,z
   e=y,p,r|- dt_ms W=y,p,r rot rot_pct vmax acc` (units; fwd/up = the flight basis rows 2/1; des = `[obj+0x14]` as the
   module's write left it, des_engine = as the engine set it that frame; given = the point the engine passed to the
   leaf, point = the one handed back, both = f+0x240 when no leaf call happened that frame and e is `-`;
   W/rot/rot_pct/vmax/acc = the getters and record fields the leaf call read). Up to about 370 bytes per row: 6 ships at
   173 fps ≈ 1,040 rows/s ≈ 385 KB/s (≈ 23 MB/min), bounded by the ring and the summary's `ship_trace_rows`; a bad
   `ship_trace_max` logs `ship_trace_refused reason=invalid_max` and keeps 6. Notes (review): capitals still get the
   avoidance emergency clamp at site B on their gate legs (intended: the emergency is site B's value); `gate_lead_*`
   count non-capital approaches once at their first P2; a subtype-5..8 pass point is validated within 30 % of its
   0.05 r radius; a leader's own far-away follower is never avoided now (section 4 of the design: acceptable, the
   formation's own spacing keeps them apart). The parser `verification/probe/ship_trace_rows.py` and
   `verification/analysis/test_ship_trace_format.py` (5 tests over a synthetic row set) carry the format; the replay
   tool (feeding a row's inputs to the fixture's engine model and comparing with the next row) is the next task.

## Replay conformance (2026-10-12, Opus 5.5; `ship_trace_replay.py`)

**What it checks.** `verification/probe/ship_trace_replay.py` streams the `ship_trace` rows of a trace file or a whole
`x3m.log`, groups them per ship and, for each pair of consecutive frames (n, n+1), runs frame n's inputs (pos, fwd, up,
rate, v, des, e, W, rot, rot_pct, vmax, acc, dt_ms) through a Python port of the fixture's engine model
(`ship_steering_host.cpp`: `controller` 0x0043ad20 in 16.16 integers, `integrate_axis`, the slew of `Model::advance`,
`Model::rotate`, position += row2 × v × dt / 1000) and compares the predicted rate, v, fwd and pos with row n+1.
Residuals per step: rate per axis (binary/s), speed (m/s), heading (deg between predicted and actual fwd), position
(m), and the conditional position `pos_c` (m: pos(n) + fwd(n+1) × v(n+1) × dt / 1000 against pos(n+1), the
translation law alone). On an `e=-` row the model carries its own remainder from the previous step (the integrator
consumes `obj+0x30` across frames). The angular acceleration is `fx(W, fx(rot, rot_pct·0x10000/100))`; with the
fixture's rot = 1.0 this is its `fx(W, k_pct·0x10000/100)`, the engine's operation order for rot ≠ 1.0 is [i].
`--dt-source next` uses row n+1's dt_ms instead of row n's.

**Classification** (a step is divergent when any residual exceeds its tolerance: rate 0.5 binary/s, speed 0.002 m/s,
heading 0.01°, position and pos_c 0.01 m; `--tol-*` overrides); first match wins: *order boundary* (the order differs
between n−1, n or n+1), *position* (pos_c over tolerance: units, dt or the slip term the model lacks), *rate
controller* (a rate residual), *orientation* (a heading residual while the rates match: a sign or axis convention),
*speed law* (a speed residual while the rates match). Consecutive divergent steps form a window; a window is "order
boundary" when all its steps are, otherwise its most frequent other class. The printout per ship gives rows, steps,
skipped pairs (frame gap, unknown remainder), the dt convention count (t_ms delta equal to row n's or row n+1's
dt_ms), p50/p95/max and the five worst frames per quantity, and the step and window counts per class.
`--gate` adds, for ships with a class-0x12 target, each run of equal order: frames, duration, `given` relative to the
previous run's first `given` (B − A at the 4→5 change), the drift of `given` inside the run, v first/last/min, the
distance to `given` and the heading error between fwd and (given − pos) at the run's first and last row, and the
minimum speed on order 5. `--json` writes the summaries and every step's residuals.

```sh
python3 -I verification/probe/ship_trace_replay.py /mnt/c/X3/x3m.log --gate --json /tmp/replay.json   # C:\X3\x3m.log
python3 -I verification/results/ship-steering/replay_aggregate.py /tmp/replay.json   # all ships: percentiles, classes per order
python3 -I verification/results/ship-steering/replay_breakdown.py /mnt/c/X3/x3m.log   # pos_c along/lateral, rate steps by axis
```

**Validation (measured).** `ship_steering_host --emit-trace` writes three fixture gate runs as rows in the DLL's
format (TS 60 fps θ 0 with the module, M5 144 fps θ −45 rotated gate, M6 30 fps θ +45 vanilla order-5 start; 4,756
rows, the order-8 launch frames not emitted); `--emit-controller` writes 4,000 controller cases over every branch.
`test_ship_trace_replay` (7 tests): the port equals the C++ on 4,000/4,000 cases; the replay of the emitted trace has
rate and speed residuals 0, heading max 0.0008°, position max 0.0028 m (the row's 5-decimal fwd and whole-unit pos),
one divergent step (the vanilla P4 stop sets v = 0 outside the slew: order boundary); the remainder and rate sign
flipped classify every divergent step as orientation (rates still exact), dt doubled as position, W halved as rate
controller; `--gate` gives the TS orders 4/0/5/0, B − A = (0, 276.25, 1093.95) m, min v on order 5 62.0 m/s.

**First real result (measured; `x3m.log` session 20261012-181404, proxy 2fb5aef2 = Run155, 295 s, SETA frames with
dt 50–80 ms, rows of 52 ships: 35 TS, 11 M4, 2 TP, 2 M6, 2 M3).** 138,259 rows, 138,123 steps, 84 frame gaps.
1. dt: t_ms(n+1) − t_ms(n) equals row n+1's dt_ms on 89,829 steps (row n's on 48,294 where dt is constant): dt_ms is
   the delta to the previous frame. Row n's dt nevertheless predicts row n+1 (speed p99 0.13 m/s, heading p95
   0.0016°; with row n+1's dt heading p95 0.12°, pos p50 0.40 m): the integrator of frame n uses frame n's dt, as the
   model does.
2. Row n's dt: speed residual p50/p95 0, max 171 m/s; heading p95 0.0016°, p99 0.30°, max 4.5°; rates p95 0–1
   binary/s, p99 41–53, max 65,535 (wraps); pos p50 0.003 m, p95 1.68 m, p99 7.0 m. Divergent steps: position 54,616,
   rate controller 4,696, order boundary 145, speed law 1, orientation 0 (windows: position 900, rate controller 892,
   order boundary 35). No sign or axis convention error appears.
3. Position: pos_c on straight steps (rate 0 at n and n+1) p95 0.005 m along / 0.03 m lateral on order 4; on turning
   steps p95 1.9 m along / 4.4 m lateral on order 4 but 0.74 / 0.38 m on order 5, consistent with the slip term of
   section 3 of the movement note (added only for command ≠ 5, decaying afterwards) that the model omits [i]. Order 8
   (the launch, 36 steps) is not modelled.
4. Rate controller: on order 5 only (4,696 of 4,696 classified steps). The breakdown: 4,454 steps on roll with the
   module's written e_roll = 0 while the engine's roll rate keeps accelerating at about acc × dt, and 2,740 yaw/pitch
   steps on `e=-` frames where the carried remainder reached 0 and the engine still turns: on order 5 something other
   than the module's leaf call writes the remainders (the dock command's own alignment or the attitude offset are the
   candidates) [i].
5. `--gate`: 46 ships with a gate target; of 31 order-4→5 changes, 24 go 4→5 with no order-0 row between (the
   fixture models one order-0 frame) and 7 show 4/0/5. On every 4→5 change one world component of B − A is ±1093.5 m
   (0.99 r: A at −1 r, B at −0.01 r on the axis, the gates' axes along world axes [i]); the heading error at the first
   order-5 row (24 direct changes) is 0–81°, median 46.5°; the minimum speed on order 5 is 46–176 m/s on 33 ships and
   0.0 / 1.0 m/s on the two TS / M4 sequences 5/0/8/0 (traced from order 5 on).

**Amendment 11 (2026-10-12; measured).** Capitals do not avoid small ships. The engine's predicate pinned in the
proximity obstacle filter 0x0044d2f0: EDI = A (the steered ship, `[A+0x48] == 7` checked at 0x0044d2f1), EBP = B (the
candidate); the jump table is on B's class; the class-7 case at 0x0044d326 reads A's record code
`[*0x606fd4 + [A+0x4a] x 0xdb8 + 0x14]`; a code in {0, 2, 3, 4, 8, 0xd} (the capital set) calls 0x0043fd00 (large) on B
and rejects the pair when B is not large (review correction 2026-10-12; the engine additionally ignores a large B when
0x0043fe80(B) holds, a record +0xc8 id check, which the mirror omits on the fail-safe side); 0x0044d385..0x0044d3a5 rejects a
class-7 B on order 0x14/0x15 whose f+0x224 is A. Mirrored as `small_for_capital` (the candidate's `size` from its
record code, read by the snapshot walk; fallback r_candidate < r_own / 3 when the code is unreadable), counted
`avoid_skipped_small`. Fixture (measured, `checks PASS 3667/3667`, 60 fps seed 1): the M2 on a straight 20 km leg
through a wall of 12 M5 fighters on collision courses (crossings 2.0-6.4 km ahead, alternating sides, +-30 m
vertical): heading change 0.00 deg, 0 capital detours, 6,478 small-ship candidates skipped; 12 of 12 fighters detour
(181-343 detour calls each) with minimum clearance 1,305 m (C = 740 m); the M2-vs-M2 crossing still detours on both
sides (837 / 880 detours, minimum distance 2,332 m >= C 1,764 m). Verifier PASS 18, x87 PASS 954 / 0, build 0
warnings, the five test modules OK.

**Amendment 11 additions (2026-10-12, after the Run155 triage; measured unless marked).** (a) Formation exclusion in
every direction (`av::excluded`: leader, own followers, wingmates of the same leader, no envelope); the detour row
gains `leader=` and `obstacle_order=`. Fixture: 8 exclusion unit checks (near and far wingmate, order 0x15, a stale
f+0x224 on another order is not excluded, own follower near and far, the leader's and the follower's view); the TS
formation-with-asteroid run's follower detours 527/511 -> 486/473 (wingmates no longer avoid each other outside the
envelope), min_rock unchanged 473 m. (b) Horizon cap `min(L / v, 30 s)`: the fixture prints
`boreas horizon: L=3074 m v=70 m/s L/v=43.9 s horizon=30.0 s swept=2100 m` and checks an asteroid 200 m beyond the
capped reach (3,408 m, inside the uncapped L + C 4,182 m) is no conflict while one 200 m inside it (3,008 m) is, with
`L_m` = 2,100 m; the Boreas scenarios move with it: flyby@600 min_clear 1,499 -> 1,402 m (C 1,258), flyby@150
1,505 -> 1,414 m, back on course 2,900 -> 1,233 ms after the pass, crossing min_dist 2,332 -> 2,315 m (C 1,764),
formation-with-asteroid leader detours 1,808 -> 1,451, min_rock 1,590 -> 1,484 m (C 1,358): all still above C.
Run155's stalled Boreas (inferred from its detour rows: t_cpa 72-1206 s at a 2.1 km miss, v a few m/s, L on the
floor C + r = 2,737 m) would see a 30 s horizon of tens of metres instead. (c) `ship_trace` slot 0 reserved for a
capital on order 4 to a gate; rows end with `emg= omega_cmd= obst=`; the parser, its test (row <= 440 B, 6 ships at
173 fps <= 460 KB/s; the longest sample row is 421 B) and the fixture's emitter carry the fields. (d) The trace gap
before order 5: [run155_trace_gap_why.py](../../verification/results/ship-steering/run155_trace_gap_why.py) over
the Run155 log ([output](../../verification/results/ship-steering/run155_trace_gap_why.txt)): 31 traced transitions,
every one has exactly one order-0 row (target 0) between the last order-4 row and the first order-5 row, 0 detour
rows in the gap, 2-918 frames without rows; the transition row's `ms_since_p2` is 80 ms at the order-5 start (P2 at
the end of the gap, not its start). Cause: SA_StopObject (order 0, one row) then the engine's avoid order 0x12,
on which `trace_frame` released the slot; re-claimed only at order 5 (inferred: no row is written on the releasing
frame; the 0x12 rows will show in the next run). Fix: the slot stays through 0x12 and 0x1f; `carrot_age_ms` in
`ship_gate_transition` (the carrot was already the last lead-in call's). Build 0 warnings, x87 PASS 954 / 0, fixture
`checks PASS 3676/3676` (9 new checks), verifier PASS 18, five test modules OK (43 tests), dry run 9 cases, host
suite in the ledger's acceptance table.

**Model calibration (2026-10-12, Opus 5.5; ship-movement.md section 13).** The fixture's `Model` and the replay's port
carry the pinned engine facts, kept equal (`--emit-controller` 4,000/4,000; the emitted trace replays exactly but for
2 roll steps of the dock run that are the rows' 5-decimal fwd/up):

* the integrator's axis step: a zero remainder sets the rate to 0 at once (`0x00459a99`, no braking); `W·k == 0`
  skips the axis with rate and remainder kept (`0x004599aa`);
* the slip in the integrator's fixed point: `p+0x100 = fx(row2, speed)`; off order 5 with speed ≠ 0 the part of last
  frame's `p+0x100` perpendicular to the new row 2 is added to the slip; position += `(slip + p+0x100)·dt/1000`
  (truncating); slip `×(2000 − dt)/2000` (toward zero). Positions are integer units;
* the port-roll match after the leaf on order 5 to a port (`Model::port_match`; the replay recovers the port's roll
  per leg from its aligned steps, `port_rolls()`): all three written remainders < `0x51e` → roll = `wrap(Γ − γ)`,
  γ = the floored third Euler angle `atan2(row0.y, row1.y)`;
* the final docking zone as a kinematic phase (scenario 10; in the replay the class `final zone`: the rate fields hold
  the per-frame step, `0x38e`/`0x71c`·dt/1000 by class, or the turn's last smaller step, or 0);
* order 8: `pos = f+0x240 + fx(row2, ⌊u²·1665000/1e6⌋ (+ v0·u/1000, P5))`, u from the order's first frame, speed and
  desired 0, the integrator still running (remainders consumed, the slip decays and is overwritten next frame); the
  fixture emits order-8 rows where the DLL keeps the trace slot (no kept speed).

`--emit-trace` now writes 7,362 rows (the three gate runs, the M6's 27 order-8 rows, and the TS dock run of scenario
10); `replay_classes.py` replaces `replay_breakdown.py` (the pre-calibration translation) for the calibrated split.

Two rounding details the Run156 trace pins **[t]** ([replay_rounding.py](../../verification/results/ship-steering/replay_rounding.py),
[output](../../verification/results/ship-steering/replay_rounding_run156.txt)): the angular acceleration is
`fx(W, ⌊rot·rot_pct/100⌋)` (16.16 k; yaw and pitch exact on 76,188 of 76,190 leaf steps against 75,763 for the previous
`fx(W, fx(rot, pct·0x10000/100))`), and γ is floored (10,137 exact match steps against 9,923 rounded and 9,703
truncated toward zero).

**Replay residuals, before → after** (measured; before: [replay_before_calibration.txt](../../verification/results/ship-steering/replay_before_calibration.txt),
after: [Run155](../../verification/results/ship-steering/replay_classes_run155.txt) (also [warm ≥ 10 s](../../verification/results/ship-steering/replay_classes_run155_warm10s.txt)),
[Run156](../../verification/results/ship-steering/replay_classes_run156.txt); producer
[replay_classes.py](../../verification/results/ship-steering/replay_classes.py) over `ship_trace_replay.py --json`).
Run155 = session 20261012-181404 (proxy 2fb5aef2, now `x3m.prev.log`, 84 frame gaps), Run156 = session
20261012-192902 (proxy b17fb4eb, `x3m.log`, 131,154 rows, 34 ships, 9 gaps: the slot now stays through 0x12/0x1f).

| Class | Run155 before | Run155 after | Run156 before | Run156 after |
| --- | --- | --- | --- | --- |
| pos_c lateral p95, order-4 turning steps (all steps) | 4.35 m | 0.42 m | 2.19 m | 0.0091 m |
| same, 3 s after a run's first row / 10 s | — | 0.043 / 0.0045 m | — | 0.0063 / 0.0048 m |
| pos_c lateral p95, order-5 turning, warm 3 s | 0.38 m (all) | 0.073 m | 0.33 m (all) | 0.0015 m |
| rate-controller steps, order 5 | 4,696 | 852 (roll only: 360 ≤ 1, 305 ≤ 3, 187 > 3 binary/s) | 3,231 | 335 (124 / 97 / 114) |
| order-boundary steps | 145 | 97 | 425 | 104 |
| order 8 (launch) divergent steps | 34 | 0 (pos max 0.002 m) | 48 | 1 (max 0.032 m) |
| order 0x12 divergent steps | — | — | 14,576 | 604 (529 position, 62 rate) |
| final-zone steps (step magnitude off) | (rate controller) | 2,993 (0) | (rate controller) | 1,554 (0) |
| all divergent steps | 59,458 | 16,008 | 65,512 | 11,612 |

**What remains, by cause** (Run156 unless noted; measured counts, causes **[i]** where marked):

1. *The slip at the start of a contiguous run is unknown* (the replay starts it at 0; τ = 2 s leaves 22 % after 3 s).
   It is Run155's remaining position residue (84 gaps, legs re-claimed at order 5): with a 10 s warm-up order-4
   turning lateral p95 is 0.0045 m, order-5 0.0000 m. Run156: 1,095 of its order-4 position steps are within 3 s
   of a run's first row.
2. *Order 0x1f (RotateToAngles, the capital's alignment) writes its own remainders* without a leaf call: 1,881 rate
   steps on three capitals (M1 966, M2 532, M2 383; rate controller before as well). Not modelled.
3. *Order 0x12 (the engine's avoid order)*: 529 position steps, mostly along the nose (speed written in pass A) and
   62 rate steps on `e=-` frames: the order's own writers **[i]**. Not modelled.
4. *The script speed limit f+0x278 is not in the row*: 4,215 speed-law steps, all one M2 (4,208 held at 53,900
   units/s under des 55,000; classified position before the slip fitted).
5. *The port-roll match while yaw/pitch also turn*: 114 roll steps above 3 binary/s (5-10 binary/s, the actual roll
   brakes later than predicted); Γ is stable per leg (spread ≤ 6 binary over 9-2,576 aligned samples), so the
   ship's γ off level is the suspect (the decomposition of `0x004f0640` away from β = 0 **[i]**). The 221 steps of
   ≤ 3 binary/s are γ read from the row's 5-decimal fwd/up (the fixture's dock run: 2 such steps, 0 with 9 decimals).
6. *The final zone's step axis and glide path are not in the row*: heading and position are not judged there
   (`unmodelled` = `final zone`); the step magnitude matches on 1,554 of 1,554 steps; row n+1's Euler step applied to
   row n's basis gives the heading to p95 0.15° (a convention check); glide p50 7.8 m/s, max 79.8 m/s.
7. *Ports without an aligned step*: 157 match steps whose leg never reached the port roll in the trace (`port roll
   unknown`, roll not judged).
8. Order boundaries (104); 26 of the warm position steps above 0.02 m are on frames of 110-762 ms.

**Fixture after the calibration** (measured; [output](../../verification/results/ship-steering/ship_steering_host.txt),
[GAP list](../../verification/results/ship-steering/ship_steering_host_gaps.txt), [60 fps deltas](../../verification/results/ship-steering/fixture_deltas.txt)
by [fixture_deltas.py](../../verification/results/ship-steering/fixture_deltas.py)): **checks PASS, 4,175 checks, 0
failures, 131 slip gaps**. Isolated by switching each change off: the slip term drives 140 of the 141 failures the
calibrated model first produced (slip off: 1 failure, the capital equality below); the rate rule and the smooth stops
change no verdict. The slip breaks margins of the law, which aims the nose while the velocity lags it with a 2 s
memory:

| Family (all 9 fps/seed runs unless noted) | Before (60 fps) | After (60 fps; range over runs) |
| --- | --- | --- |
| TS flyby@150: min clearance to the body (C 263.5 m) | 319.3 m | 21.8 m (18.3-21.8) |
| M5 crossing: min distance (C 43.4 m) | 101.5 m | 0.8 m (0.4-1.6) |
| M5 formation: slot RMS (2r 18.4 m); M3 window increases | 8.0 m | 185.9 m (182.4-187.1); M3 1 |
| gate θ +45 crossing heading error ≤ 22° (TS, M5, M3, M6; M3 also θ 0) | 20.5-21.3° | 27.0-42.3° |
| M5 gate far start (rotated) / lateral 3 r heading error | 8.2 / 8.0° | 33.8 / 22.0° |
| M2 flyby@600/150 and TS flyby@150: unresolved ticks ≤ 5 + detours/100 | met | 266-418 (M2), 64-169 (TS) |
| M2 diagonal turn at 144 fps: the vanilla leaf settles | settles | never (2 runs) |

These assertions are still computed; they print `GAP` and are counted (`slip_gaps=`), not failed, until the law is
made slip-aware (a design decision). Assertions that moved, with the reason: (a) the families above, made gaps (the
other conjuncts of each compound check stay fatal: arrival, launch, no disc, corners, `detours > 0 iff conflict`, the
module's own settle); (b) the turn's "vanilla too" is a gap and `settle ≤ vanilla × 1.02 + 50` is skipped when the
vanilla leaf does not settle; (c) the capital's vanilla-path equality `t_A == vanilla` and |Δ heading| < 0.01° became
≤ 100 ms and < 0.5° (the module's law and the vanilla leaf steer the capital by different laws; the equality held to
the frame by the think-tick discretisation and breaks with the slip: deltas ≤ 67 ms, 0.1°). Unchanged verdicts with
changed numbers: 180° turn settle +16..+117 ms, arrival +667..+984 ms (TS 165.6 → 166.3 s), gate order-5 legs t_B
+0..+17 ms, capital cloud min clearance 1,305 → 1,267 m (C 740), every other family in `fixture_deltas.txt`.

**Amendment 12 (design note; ship-movement.md section 13.4)** in the fixture (measured, 60 fps seed 1; all 9 runs per
class pass the same checks). Scenario 10: order 5 to a station port rolled 30°, the rendezvous point R 1.5 km out, the
ship 2.5 s of flight before R on a line 10° off the port's axis, heading at R; at R the target switches to the port
and the bearing jumps by about 10° (above 7.2°, below the hold's 14.4°); then the final zone (2r + 25,000 units).

| TS / M6 / M3 | Amendment 12 | Previous handler | Vanilla leaf |
| --- | --- | --- | --- |
| match engagements / toggles | 1 / 0 (all three) | 2 / 2 | 2 / 2 |
| one-frame roll cuts with the module's roll standing | 0 | 1 | 1 |
| roll error rises after the engagement (> 2 binary) | 0 | 0 | — |
| roll error at the final zone | 0.00 / 0.01 / 0.00° | 0.01 / 0.01 / 0.00° | 0.01 / 0.01 / 0.00° |
| docked at (ms) | 37,966 / 39,233 / 37,683 | same | 37,966 / 39,233 / 37,683 |

The TS has one stop with the match running (its remainder `Γ − γ` reaches 0, row 8): the engine's own, identical in
vanilla, counted apart (`match_end_stops`). A/B over the existing scenarios with amendment 12 off and on (seed 1,
30/60/144 fps): the gate legs of the 4 non-capital ships (order-4 approach θ +45 and the order-5 leg from 1.7 km) are
identical, asserted field by field; over all 5 ships the 180° turn settle is unchanged in 15 of 15 runs and the
MovePos arrival time in 15 of 15 (the closest distance changed in 1); both held to two frames by the fixture. The smooth stop applies only where the law stops an axis
(angle exactly 0); a first version that also replaced servo angles rounding to 0 moved the order-4 gate approach by up
to 0.12° and 1.5 m at the crossing and was narrowed.

**Amendment-11 review items** (measured): the snapshot's record-code read has a walk test (a type table at
`0x606fd4`: codes 4 and 11 read size 1, code 7 size 0, code 0x40 and a negative type word size 2, an empty slot
size 2 for all six ships; `small_for_capital` skips the 40 m small-coded and the 40 m unreadable ship, keeps the
400 m unreadable one and the large-coded ones: 6 checks); the trace's `obst=` is this frame's obstacle only
(`avoid_[slot].stamp_ms == now`); the unused leader position read in `avoid_adjust` is gone (`Self.leader_x` stays 0,
diagnostics only); the `trace_frame` comment names orders 0/4/5/8/0x12/0x1f.

**Acceptance** (measured): build 0 warnings; x87 audit PASS, 954 reachable functions, 0 violations; site verifier
PASS 18; `test_ship_steering`, `test_ship_steering_sites`, `test_ship_trace_format`, `test_ship_trace_replay` (8
tests: + the dock run), `test_config_schema`, `test_launcher_defaults`: 50 tests OK; host suite 297 modules, 2,926
tests, 36 failing modules (render, capture, fog and installed-game modules; none ship-related, the host's
environment set).

## Capital gate transit (2026-10-12, Fable 5.1; amendment 13, design [capital-gate-transit.md](../architecture/capital-gate-transit.md))

Implemented on main `e81d4d8f` (worktree, not installed): the pair policy (the ratio r_B ≥ r_A / 3 with capital-coded ships
always kept, the engine's large list withdrawn) and site E's small-pair branch, both part of `ship_avoidance`; the capital
lead-in and speed write, P2 keeping an aligned capital's speed and P4/P5 on the validated approach behind
`ship_steering_gates_capital` (default on, needs the gate group; `--no-ship-steering-gates-capital`; off restores the
previous (amendment 10) gate behaviour for capitals while the pair policy keeps following `ship_avoidance`); the
`ship_avoid_engine_warn` trace row; the fixture scenarios of the design's §6. Sources: `src/proxy/ship_avoidance_core.h` (`small_for_capital`, `small_pair`, `small_pair_verdict`),
`ship_avoidance_walk_core.h` (the capital bit), `ship_steering_core.h` (`gate_lead_in_capital`, `gate_target_capital`,
`gate_capital_keeps_speed`), `ship_steering.cpp` (decide, P2, P4, E, trace, summary), `ship_steering_sites.h` (the obstacle
list offsets), the schema/launcher plumbing.

**Fixture** (`verification/probe/ship_steering_host.cpp`, scenario 11 and `capital_unit_checks`; the calibrated engine model
with the slip term; measured, 60 fps seed 1, all 3 fps × 3 seeds pass the same bounds;
[output](../../verification/results/ship-steering/ship_steering_host.txt) **checks PASS 5052/5052, slip_gaps 131**
(4175 before this step; the two `gate capital` vanilla-equality rows are replaced by the scenarios below), the capital
checks alone [fixture_capital.txt](../../verification/results/ship-steering/capital-gate-transit/fixture_capital.txt) 655/655,
without the slip term [fixture_capital_noslip.txt](../../verification/results/ship-steering/capital-gate-transit/fixture_capital_noslip.txt),
both by [run_fixture_capital.sh](../../verification/results/ship-steering/capital-gate-transit/run_fixture_capital.sh)).
The Run156 records (W 1843 / 1638, k 0.05, vmax 110 / 113, a 10 / 5, r 1091 / 1022 m), from 3 r_gate off the line and 6 r
before X heading at A, with two M6 escorts of another leader crossing the line ahead of the capital (one steered, one
manual) and a second capital trailing on the adjacent slot at the queue's spacing (7 km):

| Scenario A | approach 3 r → X | crossing v, error to F, lateral | P2 | 0x1f | order 5 (budget.py) | total 3 r → launch end (design) |
| --- | --- | --- | --- | --- | --- | --- |
| M2 slot 0 | 33.5 s (model 33.1) | 100.0 m/s, 0.62°, 9 m | kept | 1 frame | 37.5 s (37.5) | 71.9 s (71.6; design 72) |
| M2 slot 19, rotated frame | 33.5 s | 100.0 m/s, 0.62°, 9 m | kept | 1 frame | 45.6 s (41.6 aligned; 286 misaligned ticks, min 51.7 m/s) | 80.0 s (75.6) |
| M1 slot 0 | 33.6 s | 100.0 m/s, 1.30°, 19 m | kept | 1 frame | 36.0 s (36.0) | 70.6 s (70.1; design 70) |
| M1 slot 19, rotated frame | 33.6 s | 100.0 m/s, 1.30°, 19 m | kept | 1 frame | 44.9 s (40.1; 407 ticks, min 66.0 m/s) | 79.5 s (74.1) |

Per run: capital detours 0, conflicts 0, `skipped_small` 7,540–8,091 (the M6s), the steered M6 detours (661–764 calls) with a
minimum centre distance of 1,633–1,747 m ≥ C (1,119 / 1,188 m); the manual M6 passes 1,022–1,047 m ahead; the site-E verdict
for (capital, steered M6) is `suppress` on every steered frame (4,245 M2 / 4,172 M1), `order_not_steered` on the 24 frames after its script
stopped it (order 0), `stale` once it is back in the steer set without a decision, `no_autopilot` for the manual M6 on every
frame; the capital pair is never a small pair in either direction and neither skips the other; no NaN. Bounds asserted:
approach ≤ 36 s, crossing speed ≥ 90 m/s, P2 error ≤ 8.11° and the speed kept, 0x1f one frame, order-5 start ≥ 85 m/s and
v0 kept, order 5 within 20 % of `budget.py` (slot 0) or ≤ 1.6× the aligned budget (slot 19: the engine's misaligned
order-5 branch, the cornering override for capitals is a follow-up), total ≤ 1.2× the budget (slot 0). **Slip:** without
the slip term the crossing error is 0.29° / 0.67° (M2 / M1) against 0.62° / 1.30° with it, the lateral offset 3 / 8 m
against 9 / 19 m, the approach 0.1–0.2 s shorter; the order-5 and total times are identical to 0.2 s; the steered M6's
minimum centre distance 1,617–1,789 m without slip against 1,633–1,747 m with it (both ≥ C). Slip costs the capital
alignment under 0.7° of the 8.1° tolerance.

**Scenario B** (misaligned): B1, the engine's point 5 % of r off the grid: the lead-in refused, vanilla crossing at
28.3 / 21.4 m/s 23.5° / 24.0° off F, the vanilla stop (v 0), the engine's 0x1f 467 / 633 frames = 7.8 / 10.6 s (controller
model 8.7 / 9.4 s), order 5 from rest 42.8 / 47.1 s, launch from rest; the converging capital (timed to reach the adjacent
slot 10 s after the first's arrival) registers the pair (4,555 / 2,790 conflict calls, never a small pair, never skipped),
detours (4,498 / 2,790) and reports it unresolved inside C with the emergency ramp (M2 trailing M1: 57 unresolved, 37
emergency, min 2,299 m) — the known limit handed to the queue task (design §5.2). B2, a validated approach 3 r off the line
only 2 r before X: the lead-in runs, the crossing is 32.0° / 37.4° off, P2 takes the vanilla stop, 0x1f 675 / 916 frames,
order 5 from rest, P4 keeps v0 on the validated approach. Policy off: no lead-in, the vanilla stops at X and at the origin.
The Boreas fixture type (70 m/s) runs scenario A alone at its own cap (crossing 70.0 m/s, 0.09°, total 102.5 s against the
model's 102.4).

**Unit checks** (`capital_unit_checks`, `walk_record_code_checks`): the ratio (an M2 of 1,091 m skips 64 and 300 m, keeps
400 m and every capital-coded ship; 307 m vs an M6 of 140 m kept); `small_pair` never for a capital-coded B, a non-capital
A, a non-ship B or a NaN radius; `small_pair_verdict` reasons in the design's order (autopilot, order incl. 0x12 and 0x1f,
class mask, deleted/attached/docked, 251 ms stale); `gate_capital_keeps_speed` (8.1° kept, 8.2° not, −1 not, policy off,
unvalidated, non-capital target); `gate_target_capital` for TL/M0/M1/M2/M7 with site W refusing them (M5 non-capital and
offset, M6 neither); the capital window (refused beyond 6 r, 90 % blended at 5.9 r, the carrot 2 v / ω = 1,136 m ahead on the
line at 4 r with 3 r lateral where the 3 r small-ship window refuses); the walk's size bit (M2 1, M5/M6 0, unreadable 2).

**Acceptance (measured):** `cmake … && cmake --build build 2>&1 | grep -c warning` → 0 (69 objects relinked);
`check_no_x87.py build/d3d9.dll` → PASS, 955 reachable, 0 violations; `verify_ship_steering_sites.py --exe /mnt/c/X3/X3AP.exe`
→ PASS, 18 checks (`snapshot_walk_facts` now also pins the obstacle list: `obstacle_count_clear_L0` 0x0045d298, `obstacle_list_append` 0x0044e54c, `obstacle_dist_append` 0x0044e565), EXE `fdbf3418…`; the six host modules (`test_ship_steering`, `_sites`, `test_ship_trace_format`,
`_replay`, `test_config_schema`, `test_launcher_defaults`) → 50 tests OK; `run_host_suite.py` → 297 modules, 2,926 tests,
36 failing modules (the host-environment set, none of the six above). Schema: 269 settings, 119 in the template.

**Flight rows that decide it (Run C, the next capital trace; `verification/results/ship-steering/run156-triage/capital_timeline.py`
on the `ship_trace` rows of slot 0, targets per traced capital at the run's SETA):**

| Phase | Row | Target |
| --- | --- | --- |
| install | `ship_steering_gates_capital installed=1 status=active` | present (a `ship_steering_gates_capital_refused` row = the policy is off) |
| approach | order-4 segments per approach in the timeline | 1 (no 0x12 segment, no order-0 row with the target cleared); `hdg_err_deg` falling monotonically inside 6 r |
| crossing | `hdg_err_deg` and `v` at the last order-4 row (`dP` 823–829 m) | ≤ 8.1°; ≥ 90 m/s (SETA 1) |
| stop | summary `gate_capital_aligned`, `gate_capital_stop_kept`, `gate_capital_lead_validated` | = the number of capital approaches; `gate_capital_lead_fallback` 0, `gate_p2_capital` 0 |
| rotate | rows on 0x1f | ≤ 2, `v != 0` on them |
| order 5 | `v` at the first order-5 row; the segment's time | ≥ 85 m/s; within 20 % of `budget.py` for the run's SETA (slot 0: 37.5 / 36.0 s at SETA 1, 15.0 / 15.4 s at SETA 10) |
| launch | order 8 rows; `gate_launch_continued` | 0.9 s; = the capital launches |
| pairs | `e_suppressed_small` vs `e_small_pass_*`; 0x12 rows for traced capitals; `ship_avoid_engine_warn` | > 0 when escorts were near; 0 0x12 rows; any remaining avoid leg names its pair (`other=`, `code=`) |

Known limits (open): order 5 from the outer slots takes the engine's misaligned branch (1 m/s until within 18.2°: +8–9 s in
the fixture; the cornering override of amendment 8 (4) extended to capitals is a follow-up); two capitals converging on
adjacent slots inside C are unresolved by design (the queue's spacing); the fixture crosses the escorts over the capital's
line once it is on the line (the predictive layer extrapolates an obstacle's velocity straight, so a crossing during the
capital's join is not covered by this scenario; a question for the slip-aware design).

### Amendment 15 (Run157 corrections; 2026-10-12, Opus 5.5; design §15, scenario audit G1–G4)

Implemented on main `9c173d51` (worktree, not committed, not installed). Sources: `ship_avoidance_core.h` (`small_pair_verdict`
on any order, `MemberPair`, `Members` + `update_members`, `excluded` on `member_of`, `fresh_window_ms`, `SiteE` /
`site_e_verdict`: site E's whole decision as a pure function the DLL and the fixture share), `ship_avoidance_walk_core.h`
(`[obj+0x08]`, the autopilot bit, the live effective leader, the membership pass after the walk), `ship_steering_core.h`
(`join_plane`, `join_predict_grade`, `join_choose`, `join_fallback_speed`, `join_keep_verdict`/`join_coast_bound`,
`JoinState`/`join_on_decision`/`join_evaluate`, `sincos_f`), `ship_steering.cpp` (decide's join and fallback writes, P2's
keep verdict and hand-over flag, E, the frame-end predictor, the three rows, the counters, escort trace slots),
`ship_steering_sites.h` (`kObjId` 0x08, `kFlagWarnBits` 0x3000).

**Site E as built** (order of the tests): gate rows first, for B = A's own gate in A's gate state on a validated approach:
a capital on order 4 in join mode decided within the fresh window, or on 0x1f / order 0 after P2 kept its speed (keyed on
the gate state and the P2 stamp, ≤ 120 s, cleared by any handled order other than 4/0x1f: audit G3) → `suppress_gate`; a
non-capital on order 4 whose validated lead-in was active within the fresh window → `suppress_gate_small` (audit G1).
Then A's last decision within `max(250 ms, 4 × frame step)` (step clamped at 250 ms, audit G2) else pass. A capital-coded A
with readable codes: B < r_A / 3 → suppressed on any order (0x12 and A on 0x12 included) except no autopilot, deleted /
attached / docked, masked class, B on 5 / 0x1e / 0x20 (`b_dock_leg`, audit G4) and B = A's own live or remembered leader;
a non-small B with A not on 0x12 → `suppress_member` when B is a current member of A (entry, both ids, ≤ 20 s), B on 0x12 or
order 0 without a target, centre distance > r_A + r_B. Last the resolved set (A not on 0x12, window `max(100 ms, 4 × step)`).

**Membership table:** 256 entries × 20 B (follower, leader, leader_id, follower_id, confirmed_ms), probe window 8, plus a
4 KB per-snapshot pointer index. Set/refresh on 0x14/0x15 with the leader a ship of the same snapshot; cleared at once by
any other order with a target (`member_cleared`); kept without refresh on 0x12, order 0 without a target or any order
without a target; dropped the same snapshot on leader missing, id change, role reversal, autopilot bit lost on either
ship, 20 s since the last confirmation. Host cost (measured, x86-64 -O2): 208–221 ns per 152-object / 37-ship snapshot.

**Join:** the amendment-13 pursuit; the predictor (frame end, one capital per frame, 1 Hz game time, 0.25 s steps, ≤ 4096
steps) grades each candidate 100/60/40/25: margin (nose ≤ 4°, velocity ≤ 6°, |e| ≤ 50 m, excursion ≤ 50 m) or P2's
acceptance (8.11°, 10°, 100 m, 100 m). A higher speed needs the margin, the current one is kept on the acceptance, a lower
one is the highest margin candidate else the highest accepted one; none accepted → fallback `pred_none`. First decision
past X, or a join-mode ship past X unconverged → fallback `past_x`. Fallback (latched until the point moves, the leg's
previous handled order is not 4, or 2 s without an order-4 decision): aim at A, `min(100, √(25² + 2a(d − 0.75r)),
0.0401·d / (2 sin θ))`, floor 10 m/s and the floor from 90°, written both ways. P2 keeps only in join mode with nose ≤ 8.11°,
velocity ≤ 10° and v ≤ `√(2a(|X−G| − (r + v/10)·3.25))` (SETA ×10; 181 / 132 m/s for the M2 / M1, measured by the unit check).
Predictor cost (measured, host): bounded at 1024 steps (256 s) per candidate after review; the review's sweep over window
states ([pred_cost.cpp](../../verification/results/ship-steering/capital-gate-transit/pred_cost.cpp), s −6..−0.1 r, lateral
100 m–20 km, every nose angle, best of 7) gives a worst evaluation of 80.8–81.3 µs at 19.5 km lateral (155.9 µs with 4096
steps); single scenario evaluations peaked at 42–87 µs (one timing each, noisy); the fixture's scenario ranges are identical
with the cap. Registry (review blocker): `core::JoinRegistry`, 8 slots, swept at every frame end before the evaluation; a slot
leaves when its table entry belongs to another ship, its join mode ended (any handled order other than 4 clears it: order 5,
0x12, a redirect) or its last order-4 decision is older than 2 s (transit done, ship dead or redirected); a capital in join
mode re-registers on every decision until it is in. Counters `join_slots`, `join_slots_dropped`, `join_slots_refused`
(refused calls). Fixture: 12 capitals cycled through transits (three windows overlapping) are all registered, the registry
peaks at 3 of 8 slots and drops all 12; without the sweep the ninth is refused.

**Fixture** ([output](../../verification/results/ship-steering/ship_steering_host.txt) **checks PASS 6148/6148, slip_gaps 0**;
capital checks alone [fixture_capital.txt](../../verification/results/ship-steering/capital-gate-transit/fixture_capital.txt)
**1521/1521** (655 before); model times [a15_fixture_budget.py](../../verification/results/ship-steering/capital-gate-transit/a15_fixture_budget.py)
→ [txt](../../verification/results/ship-steering/capital-gate-transit/a15_fixture_budget.txt), inferred). Ranges over 3 fps ×
3 seeds (× slots 0 and 19 for E), measured:

| Start | Ship | Mode | Nose / velocity at X | \|e\| / excursion past the axis | Crossing v | t_X / model | Lowest v_cmd | P2 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| E1 6435 m, 5.58 r, at A | M2 / M1 | join | ≤ 1.91° / 1.91°; ≤ 3.65° / 3.73° | ≤ 28 / 9 m; ≤ 53 / 0 m | 100 | 0.988–0.994; 0.982–0.998 (100.3 / 98.7 s) | 100 | kept 18/18 each |
| E2 6435 m, 5 r, at A | M2 / M1 | join | ≤ 3.14° / 3.28°; ≤ 6.11° / 6.20° | ≤ 46 / 9 m; ≤ 88 / 0 m | 100 | 0.987–0.994; 0.981–0.992 (94.2 / 92.6 s) | 100 | kept 18/18 each |
| E3 on axis, 5 r | M2 / M1 | join | 0° | 0 | 100 | 0.996–0.999 (55.3 s) | 100 | kept |
| F1 627 m, 2.97 r, 68° away | M2 / M1 | join, slowed | ≤ 0.95° / 2.03°; ≤ 6.01° / 7.08° | ≤ 37 / 37 m; ≤ 82 / 47 m | 100; 60–80.7 | 0.921–0.987; 0.981–1.091 | 60; 40 | kept 9/9 each |
| F2 6435 m, 5 r, 60° away | M2 / M1 | join, slowed | ≤ 5.07° / 6.85°; ≤ 5.99° / 7.46° | ≤ 62 / 0 m; ≤ 82 / 0 m | 60–100 | 1.002–1.087; 1.021–1.189 | 60 | kept 9/9 each |
| G1 1645 m, 0.70 r, 91° toward | M2 / M1 | fallback `pred_none` | ≤ 17.9°; ≤ 12.7° | ≤ 193; ≤ 132 m | 25.0–25.3; 25.0–25.1 | 0.93–1.07 | – | vanilla stop; rotate ≤ 0.65× model |
| G2 s +2.05 r, 3252 m, 135° | M2 / M1 | fallback `past_x` | 82.6°; 74.3° | 821; 797 m | 25.0–25.7; 25.8–29.0 | 0.95–1.07 | – | vanilla stop; rotate ≤ 1.00× / 1.07× model |

E: v_cmd 100 throughout, crossing ≥ 90 m/s, 0x1f one frame; the engine's gate warn cone fires on 1,057–1,080 frames
(17.6–18.0 s at 60 fps, the design's 17.7 s) at slot 0 and is suppressed on every one of them through the crossing and the
hand-over (`e_gate_sup` 5,455–5,946 frames, 3 in the hand-over, 0 unsuppressed). B2 (3 r lateral only 2 r before X,
amendment 13's misaligned case) now joins slowed (M2 40 m/s, crossing 68.5 m/s at 2.9°; M1 25 m/s at 6.3°) and is kept;
the misaligned vanilla stop is G. Scenario A is unchanged against the pre-change fixture at `9c173d51` (M2 0.17° / 3 m,
total 71.8 s; the tracked `fixture_capital.txt` of amendment 13, 0.62°, predated amendment 14's plant).
**D (cascade, 60 fps seed 1; all 9 runs pass):** (leader, M6 on 0x12 / order 0) `suppress_small` 899/899 frames; docked,
manual and order-5 variants pass; (leader, M1 on 0x12 / order 0) `suppress_member` 1195/1195 frames while ≤ 20 s, then 270
frames passed (`member_expire` 1); the M1 is remembered in the snapshot 1200 frames and the leader detours against it 0
times (886 without the memory); order 4 and the id swap each drop the entry in the same snapshot (`member_cleared` 1,
`member_id_mismatch` 1); (M1 on 0x12, its leader) never suppressed (0/1081); M1 never closer than 2,400 m (> r_A + r_B).
**K (audit G4):** (M2, M6 on order 5 crossing 1 km ahead) `pass_b_dock_leg` 3600/3600, min distance 635 m < C 1,701 m (the
engine must handle it; the same pair with B on 0x14 is suppressed 3600/3600). **G1:** (ship, its gate) `suppress_gate_small`
on every lead-in frame (TS 2871/2871, M4 1612/1612), none before the window (18 / 12 frames), the engine's warn cone fires on
281 / 272 lead-in frames and none is unsuppressed. `--capital-noslip` is now a report only (45 failures: the predictor
models the slip lag).

**Deviations from §15 as written:** the speed rule keeps the current speed on P2's acceptance and falls back only when no
candidate reaches the acceptance (the strict "highest within the margin" slowed the fixture's M1 from 6.4 km to 60/25 m/s
for 10 s before X although its 100 m/s prediction, 4.9° / 70 m, matched its flown crossing, 4.6°, and no speed reached the
margin there); P2's bound uses SETA ×10 (no SETA read); "the player takes either ship" is the autopilot bit; the member row
does not run while A is on 0x12; small B = A's own leader passes (`e_small_pass_own_leader`); extra counters
`member_cleared`, `member_full`, `join_evals`, `join_predictions`, `join_eval_us_max`; the `warn_pre` row is taken at site B
of the traced ship (the object is live there) on the first frame the bits stand, not by dereferencing it at the frame end;
the E scenario's excursion bound holds (≤ 9 m) but the M2 crosses the axis once at 30 fps, so "no axis crossing" is
reported, not asserted.

**Review notes (2026-10-12):** the capital branch's window at E is `max(250, 200 + 2·dt, 4·dt)` ms (340 ms at 70 ms steps;
the 4·dt form alone could lapse by a few ms after a jittery frame); the resolved set keeps `max(100, 4·dt)`. The capital
gate row covers join mode and the kept hand-over only: a fallback approach still gets the engine's gate warn (and its
`SA_StopObject`), by design. The hand-over row is keyed on A's gate state; if A's table entry is reused by another ship the
state goes with it and the row lapses (fails closed to the engine's warn). A follower whose target is not a ship of the
snapshot (a station, a ship in another sector) drops its entry as `member_leader_gone` (the index holds ships only), not as
`member_player`. `e_record` (`--debug`) is guarded against a null B. `pass_unreadable` is unreachable in the DLL (an
unreadable A code never makes A a capital). The object id `ship+8` is the native body id of
[chase-view-transition.md](../reverse-engineering/chase-view-transition.md) (the `SA_GetEventObject` row), cited in
`ship_steering_sites.h`. Design §15.6's option-off sentence is corrected in the note: site E's small-ship row follows
`ship_avoidance`, not `ship_steering_gates_capital`.

**Run D targets per phase** (the capital and its escorts traced: `ship_trace_max = 4`; the escorts of slot 0's capital
now take trace slots):

| Phase | Rows / counters | Target |
| --- | --- | --- |
| install | `ship_steering_gates_capital installed=1 … member_capacity=256 member_expiry_ms=20000` | present |
| cascade | the leader's 0x12 rows; `ship_avoid_engine_warn_pre` (`entries_other:class:code:order:leader:member:d_m:r_m:e_verdict`); `ship_avoid_e_verdict` tallied by `verdict` and `B_order` | 0x12 legs 0; any remaining leg's `warn_pre` names a non-small, non-member obstacle; no `e_small_pass_order` (removed); `ship_avoid_detour` rows against members 0, `avoid_excluded_member` > 0 when escorts were in 0x12 |
| membership | `member_set/refresh/replace/expire/leader_gone/id_mismatch/reversed/player/cleared/full` | `member_expire` ≪ `member_set`; `member_id_mismatch` 0 unless a ship died; `member_full` 0 |
| join | `ship_gate_capital_join` (`mode reason s_m e_m nose_deg vel_deg rate_dps v_ms v_cmd carrot_m pred_nose_deg pred_e_m`, 1 Hz and per mode change); `gate_capital_join`, `gate_capital_slowed` | one order-4 segment per approach; `mode=join` at the window entry; excursion past the axis ≤ 100 m (`signed_lateral.py`) |
| crossing | the join row's `cross_nose_deg cross_vel_deg cross_v kept why_not window_s`; `gate_capital_kept`, `gate_capital_not_kept_{mode,nose,vel,bound}` | nose ≤ 8.1°, v ≥ 90 m/s at v_cmd 100, `kept=1`; `gate_capital_kept` = the capital approaches |
| gate | `e_suppressed_gate`, `e_suppressed_gate_small`; `warn_pre` entries of class 18 | > 0 per aligned approach; no `warn_pre` row naming the gate for slot 0; small ships: no class-18 first-0x12 rows inside the lead-in window (`engine_warn_tally.py`) |
| hand-over | 0x1f rows; the first order-5 row | 0x1f ≤ 2 rows; order-5 start ≥ 85 m/s when the slot is granted at once |
| fallback | join rows `mode=fallback reason=pred_none|past_x` and their crossing row; `gate_capital_fallback_{pred,past_x}` | `cross_v` ≤ 30 m/s; rotate within 20 % of `rotate_time` |
| pairs | `e_suppressed_small`, `e_small_pass_{no_autopilot,not_steerable,class,b_dock_leg,own_leader}`, `e_suppressed_member` | `b_dock_leg` > 0 only near docking traffic; small-ship predicted misses < C fall from Run157's 321 |
| cost | `join_eval_us_max`, `join_evals`, `join_slots_dropped`, `join_slots_refused`; `handler_us_p99` | ≤ ~100 µs; `join_slots_refused` 0; p99 unchanged against Run157 (6.4 µs median) |

**Acceptance (measured):** `cmake … && cmake --build build 2>&1 | grep -c warning` → 0; `check_no_x87.py build/d3d9.dll`
→ 958 reachable, 0 violations (`frame_boundary`, not an audit root, keeps its 10 pre-existing 64-bit summary conversions,
identical to the pre-change TU; the predictor and the membership pass have none); `verify_ship_steering_sites.py --exe
/mnt/c/X3/X3AP.exe` → PASS, 18 checks; the six host modules → 52 tests OK; `run_host_suite.py` → 297 modules, 2,928 tests,
36 failing modules (the host-environment set; none ship-related).

## Slip-aware law (2026-10-12, Opus 5.5 / Fable 5.1; amendment 14, design [slip-aware-steering.md](../architecture/slip-aware-steering.md))

The work started as a WIP commit (`ade25410`) and was completed in source; it is not installed. Sources:
`ship_steering_core.h` (`steer`: the velocity state, the lead, `ω_c`, the closing-speed solve, the formation law,
`gate_look_s` with the lateral shortening, `lag_deg`, `one_minus_exp_neg`, `cbrt_f`), `ship_avoidance_core.h`
(`Self.slip`, the swept test, `lookahead` with `v·τ`, `turn_in_slip`, the push and the behind test on `û`, the resolved set on
the velocity's miss, the `kTieMissFrac` tie-break), `ship_avoidance_walk_core.h` (the obstacles' `obj+0x60`), `ship_steering.cpp`
(decide's slip and leader reads, the detour flag, the gate carrot's α, `Ship.v_slip`, the trace's `slip=`, the decision row),
`ship_steering_sites.h` (`kObjSlip`). Design text: amendment 14 in
[ship-steering-and-avoidance.md](../architecture/ship-steering-and-avoidance.md).

**Fixture** (calibrated engine model with the slip term; all values measured): **checks PASS 5282/5282, slip_gaps 0**
(the model calibration left 131 gaps), and the capital checks alone (`--capital`) 655/655. Every gap is now a hard
assertion, four new families check the velocity's angle to the gate line (θ, spread, far/rotated, lateral), and
`slip_unit_checks` adds 13 unit checks: the CRT-free helpers, the gate carrot, the lead's sign, the detour and order-5
exceptions, the attack cap, the closing speed, `ω_c`, velocity matching and the crossing tie-break.
`vanilla_notes=2`: the Boreas's vanilla leaf does not settle the diagonal 179° turn at 144 fps (seeds 1 and 1234567). That
is the vanilla's own deficit (design §5), printed as `NOTE` and no longer counted. Outputs:
[stdout](../../verification/results/ship-steering/ship_steering_host.txt),
[stderr](../../verification/results/ship-steering/ship_steering_host_gaps.txt) (the two notes),
[vanilla rows](../../verification/results/ship-steering/ship_steering_host_vanilla_rows.txt) (`--vanilla-rows`: every ship on
the engine's leaf, no avoidance; the engine's own warn is not modelled). The full before/after table, scenario by class
with each threshold, is [slip_law_table.txt](../../verification/results/ship-steering/slip_law_table.txt), produced by
[slip_law_table.py](../../verification/results/ship-steering/slip_law_table.py) (old = `af5ec4cf`'s output). Condensed
(60 fps, seed 1; V = vanilla leaf, O = old law with the slip, N = slip-aware law):

| Scenario | M5 V / O / N | M3 V / O / N | M6 V / O / N | TS V / O / N | M2 V / O / N | Threshold, all 9 runs |
| --- | --- | --- | --- | --- | --- | --- |
| formation slot RMS, m | 473.5 / 185.9 / **21.0** | 20.3 / 20.4 / 3.0 | 39.6 / 39.6 / 56.3 | 15.4 / 15.4 / 22.5 | 1355.5 / 1355.6 / 1378.7 | ≤ 2 r; M5 ≤ 30 (floor 24.9 × 1.2) |
| formation 2 s window increases | 11 / 12 / 0 | 1 / 1 / 0 | 0 / 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 | 0 |
| flyby@150 min clearance, m (C) | 150 / 284.9 / 293.1 (234.2) | 150 / 242.6 / 306.1 (236.9) | 150 / 339.1 / 384.8 (296.6) | 150 / **21.8 / 342.4** (263.5) | 150 / 1297.2 / 1393.0 (1258.1) | ≥ C; unresolved ≤ 5 + detours/100 |
| crossing min distance, m (C) | 1.8 / **0.8 / 86.9** (43.4) | 0.2 / 87.0 / 97.9 (48.8) | 0.4 / 292.4 / 307.5 (161.0) | 0.1 / 165.6 / 186.9 (102.0) | 0.1 / 2243.2 / 2318.1 (1763.5) | ≥ C, side flips 0 |
| gate θ −45 / 0 / +45 nose to B, ° | V 33.3 / 19.0 / 13.7 all classes; O 7.9 / 14.2 / 27.0; N 6.5 / 13.2 / 25.4 | O 18.8 / 32.0 / 42.0; N 5.6 / 11.3 / 23.5 | O 7.0 / 20.2 / 31.2; N 6.4 / 10.5 / 22.8 | O 4.1 / 16.1 / 27.6; N 7.8 / 9.4 / 21.9 | — | ≤ 22; θ +45: M5 27, M3 24, M6 24 |
| gate velocity to the line at the crossing, ° | 32.9 / – / **20.9** | 32.9 / – / 2.0 | 32.9 / – / 2.9 | 32.9 / – / 2.8 | — | ≤ 13; M5 ≤ 22 |
| gate spread, max nose (velocity), ° | O 18.1; N 18.5 (31.6) | O 21.7; N 15.4 (4.1) | O 9.5; N 15.6 (3.5) | O 8.5; N 17.7 (3.5) | — | nose ≤ 24 (unchanged); velocity ≤ 20.7, M5 ≤ 33 |
| gate far/rotated, lateral 3 r nose, ° | O 33.8 / 22.0; N 13.7 / 3.3 | O 8.0 / 13.1; N 8.4 / 7.7 | O 7.7 / 11.3; N 8.3 / 8.0 | O 7.8 / 10.1; N 8.3 / 8.1 | — | ≤ 22; velocity ≤ 13 (max 4.1) |

The ranges over the nine fps × seed runs behind the re-based thresholds were measured with a temporarily instrumented
build (not retained): M5 slot RMS 20.0–24.9 m; θ +45 nose M5 25.2–26.4°, M3 23.5–23.6°, M6 22.8–23.2°, TS 21.8–21.9°;
velocity to the line M5 20.3–21.1°, every other class ≤ 3.2°; spread velocity M5 31.2–31.8°, others ≤ 4.2°. The
clearance margins over C were at least 58.0 m (M5 flyby@150) and 43 m (M5 crossing, 86.3 m). The fixture's own assertions
bound all of these.

**Findings (no scenario below its clearance C):**

1. *M5 at a gate:* at 532 m/s the velocity turns at most 0.43 rad/s (R_eff 1.24 km), so from the standard start (inside
   the window, 1.3–1.7 r off the line) the M5 crosses with its velocity 20.9° off the line (31.6° from the spread's far
   rings; vanilla 32.9° at 210 m/s). Its nose stays within the order-5 bounds. Only a lower approach speed would close this,
   and that is not in the design.
2. *θ +45 nose above the design's 22°* for the M5, M3 and M6 (25.4 / 23.5 / 22.8°). This is down from the old law's
   27.0 / 42.0 / 31.2°. The `−0.5·e` carrot shortening that the spread needs costs this: with the full carrot θ +45 was
   19.3 / 21.5 / 20.4° but the spread failed at 39.0° (M5) and 26.5° (M6).
3. *Inside their thresholds but worse than the old law:* formation M6 39.6 → 56.3 m and TS 15.4 → 22.5 m (the design's
   2-D replica predicted 6.1 and 2.6 m); spread nose M6 9.5 → 15.6° and TS 8.5 → 17.7°; θ −45 TS 4.1 → 7.8°.
4. *Three changes are not in the design,* and each was needed for a hard check. The lead is off on detour points (M5
   flyby@150 225.8 → 293.1 m). Near-centre sides come from the relative velocity (M5 crossing 0.2–5.2 m with 1–2 side flips
   → 86.3–87.2 m, 0 flips, 0 unresolved). The carrot is shortened by e/2 (spread M5 39.0 → 18.5°, M6 26.5 → 15.6°).

**Review fixes (same day, measured):**

1. *The cornering term caps the nose speed.* The closing-speed solve now divides the arrival ramp alone, never raises it
   with the point more than 60° off the nose, and `v_corner` caps the result (site B the same). A point behind the nose
   takes the tightest circle, `ω_c·d/2`. Measured by `steer()` on an M5 at 300 m (`ade25410` → now): abeam
   129.0 → 64.5 m/s, abeam with a 100 m/s slip away 329.0 → 64.5, behind 248.9 → 64.5. The new unit check also covers a TS
   at 85° and the no-raise rule.
2. *The slip is bounded.* |slip| > 2·v_max is treated as 0 for the ship (`core::slip_usable`, in the DLL's decide and
   in `steer()`) and for the leader (2·max(v_max, v_lead)). The detour push uses at most 3 v and 30 km. The snapshot's
   p+0x100 + obj+0x60 is summed in 64 bits and saturated to int32: a plain int64 → float conversion compiled to x87 `fild`,
   which the x87 audit caught.
3. *A formation follower whose N points behind brakes instead of looping.* New `formation_brake_scenario`, nine runs
   per class. Before the fix, a follower started 6 r ahead of its slot (leader straight at 0.5 v_max) turned round
   370–490° on every class but the Boreas, and after a leader stopped dead the TS and M6 orbited at v_max (329 / 541 m
   from the slot after 90 s). After the fix the follower ahead of its slot rotates 0° and ends 1.0–4.3 m from it. After
   the stop the overshoot is ≤ 2 × the braking distance from 0.9 v_max + 4 r (M5 972, M3 517, M6 1,121, TS 461 m), and the
   follower ends within 4 r of the slot at 0–1.3 m/s. The rotation after the stop (443–969°) is the turn back plus the
   fixture engine model's close-in branch, which hands the leaf a point 20 r ahead of a stopped leader's slot **[i]**.
   **The circle scenario's slot RMS is unchanged** (M6 56.3, TS 22.5 m): these fixes do not recover the old law's
   39.6 / 15.4 m. A slot-closing brake that changes N's direction was tried and made it worse (M6 91.4, TS 30.5 m),
   because the close-in branch's point is not the slot.

**Notes for flight:**

* The resolved set now requires the velocity's miss ≥ C, so site E suppresses fewer pairs than before. Watch the
  summary's `e_calls` against `e_suppressed`, and the `CollisionWarn` dispatches.
* The fixture's gate α now uses the DLL's expression (`core::slower_axis_alpha`: positive caps only, k > 0).
* The cost figures come from [steer_cost_bench.py](../../verification/results/ship-steering/steer_cost_bench.py),
  [output](../../verification/results/ship-steering/steer_cost_bench.txt). On the x86-64 host, `steer()` went
  53.4 → 75.8 ns and `resolve(420)` 179.2 → 200.3 ns. This is not the i686 DLL.
* The M5's θ +45 nose bound is 27° against a measured 26.4°, a 0.6° margin: check it on the first gate trace.

**Replay lag, Run157** (amendment-13 build, `x3m.log`, 35 ships, the old law; its rows carry no `slip=`): from the
replay's per-step `lag_obs`, the angle between the step's displacement and the next row's nose
([replay_lag.py](../../verification/results/ship-steering/replay_lag.py) over `ship_trace_replay.py --json`,
[output](../../verification/results/ship-steering/replay_lag_run157.txt); measured):

| Order / class | steps | p50 | p95 | p99 | max |
| --- | --- | --- | --- | --- | --- |
| all | 116,043 | 0.12° | 15.96° | 32.21° | 153.56° |
| 0x4 | 78,203 | 0.10° | 14.44° | 31.06° | 153.56° |
| 0x5 | 21,224 | 0.03° | 3.22° | 9.35° | 37.81° |
| 0x12 | 16,479 | 7.12° | 24.39° | 42.06° | 93.29° |
| 0x0 | 137 | 7.32° | 34.76° | 48.20° | 70.73° |
| M4 / TP / TS / M2 / M1 | 18,285 / 17,802 / 49,597 / 26,742 / 2,507 | 0.02 / 0.03 / 0.15 / 1.96 / 0.07° | 26.79 / 17.02 / 14.92 / 12.17 / 14.19° | | |

The model's lag fits the flown one (|flown − model| p50 0.00°, p95 0.18°). The design's order-4 expectation (p50 ≈ 6°,
p95 ≈ 25°, inferred from §13.2's |slip|/v) overstated the median: most order-4 frames fly straight.

**Trace and replay.** `ship_trace` rows end in `slip=x,y,z` (units/s at site B); the parser keeps it optional for older
rows. The replay seeds its carried slip from a row's `slip=` and reports `slip_res`, the carried value's distance to the
row's, together with `lag_obs`, `lag_model` and `lag_row` per step and per-order lag statistics (`lag_deg` lines). On the
fixture's emitted trace: `slip_res` max 0.006–0.018 m/s (the rows' 5-decimal fwd), |flown − model| p95 0.000°, M5
order-4 lag p95 31.4°.

**Acceptance (measured):** `cmake … && cmake --build build 2>&1 | grep -c warning` → 0; `check_no_x87.py build/d3d9.dll`
→ PASS, 955 reachable, 0 violations; `verify_ship_steering_sites.py --exe /mnt/c/X3/X3AP.exe` → PASS (EXE `fdbf3418…`); the six
host modules (`test_ship_steering`, `_sites`, `test_ship_trace_format`, `_replay`, `test_config_schema`,
`test_launcher_defaults`) → 52 tests OK; `run_host_suite.py` → 297 modules, 2,928 tests, 36 failing modules (the
host-environment set; none of the six).

**Flight rows that decide it** (the next trace with this build): the `ship_trace` rows carry `slip=`, and
`ship_trace_replay.py` reports `slip_res` p95 below 0.05 m/s (the read is right) and per-order `lag_deg` lines. On
order 4 the lag should shrink after a turn, not grow; a lag that grows means a wrong sign in the lead. The
`ship_steering_decision` rows give `lag_deg`/`aim_off_deg`/`v_slip_ms`. Formation followers hold their slot within
2 r; for an M5 the bound is 30 m. Capital rows follow the amendment-13 targets above unchanged; the fixture's capital
checks are 655/655 with the new law.

## Capital gate transit amendment 16 (2026-10-13, Opus 5.5; design §16 with the ratification addendum, implementation notes §16.9)

Implemented on main `453d12ed` (worktree, not committed, not installed). Sources: `ship_capital_lane_core.h` (new: the lane
board, commitment, footprints, the verdict with the corridor route, D and the follow cap, the path guards),
`ship_avoidance_core.h` (`Self.lane_*`, `lane_bind`, `lane_pair`, `excluded_base` / `excluded`, the resolve's lane counters,
`SiteE`'s amendment-16 inputs and `suppress_lane`), `ship_avoidance_walk_core.h` (`hull_width`: the §6.1 chain of
render-node-bounds.md), `ship_steering_core.h` (`JoinMode::slow_join`, `JoinPred`, `join_evaluate16`,
`join_registry_keep16`), `ship_steering.cpp` (decide's lane entry and follow cap, the order-5 refresh, P2's hand-over, E's
lane and extended gate rows, the frame-end evaluation, `ship_capital_lane` / `ship_gate_capital_latch` / `ship_capital_census`
rows, counters `lane_exempt lane_follow lane_cross lane_capped e_suppressed_lane gate_capital_slow_join gate_capital_unlatch
lane_board_full`), `ship_trace_rows.py` (`parse_kv_rows` for the three rows). No hook site added.

**Acceptance (measured, WSL2 host, MinGW GCC 16.2):** `cmake … && cmake --build build 2>&1 | grep -c warning` → 0;
`check_no_x87.py build/d3d9.dll` → PASS, 963 reachable, 0 violations (four lane helpers returning float were out of line at
first and returned in st(0); now always_inline); `verify_ship_steering_sites.py --exe /mnt/c/X3/X3AP.exe` → PASS, 18 checks;
host fixture ([output](../../verification/results/ship-steering/ship_steering_host.txt)) **6655/6655, slip_gaps 0,
vanilla_notes 2**; `--capital` ([output](../../verification/results/ship-steering/capital-gate-transit/fixture_capital.txt))
**2028/2028** (`--capital-noslip`, a report: 33 failures, 45 before); the four ship host modules plus the config/launcher
tests → 55 tests OK; Run158 replay (`ship_trace_replay.py /mnt/c/X3/x3m.log --json` → `replay_lag.py`) byte-identical to
[replay_lag_run158.txt](../../verification/results/ship-steering/replay_lag_run158.txt), |flown − model| p95 0.00°.

**Cost (host x86-64 -O2, measured):** one capital decision's lane work on a full gate (refresh, follow cap over 19, 19
`lane_pair`) 0.345 µs; E's capital-pair work (lane verdict + `site_e_verdict`) mean 0.12–0.14 µs per call in scenario M;
predictor worst over the 4,032 window states of the review sweep: `join_choose` 81.4 µs, `join_evaluate16` 81.2 µs from join,
81.6 µs from the fallback ([pred_cost16.cpp](../../verification/results/ship-steering/capital-crowd/pred_cost16.cpp) →
[txt](../../verification/results/ship-steering/capital-crowd/pred_cost16.txt)). The DLL's i686 figures are not measured.

**Option-off:** E's verdict with the amendment-16 inputs at their defaults (what the DLL leaves with
`ship_steering_gates_capital` off) equals the verbatim amendment-15 function on 400,000 random inputs (0 differences); with
no board the resolve is the amendment-15 exclusion (unit check). Join mode decides the amendment-15 speed on all 95 accepted
states of 120 random join states (15 → slow_join, 10 → fallback). The §16.4 small-ship extension follows `ship_steering_gates`.

**Hull width:** `hull_width` over a synthetic node tree: hull (2 LODs, the coarse one wider in x) + a rotated turret + a
skipped no-collision jet → w 502 m = the hull's √(e_x² + e_y²) over all LODs, source `lod`, 2 parts; a cache miss, LOD count 0,
an extent > 0x20000, an unscaled node, no root, no collision part, a non-power-of-two hash → w = r (`fail`). Not run against
the game: the census row is a flight target.

**Scenario M** (`crowd_scenarios`, calibrated plant with slip, five capitals on slots 0–4: Titan 1,092 / Colossus 1,022 on
their axes at 5 r, an M2 755 2.7 km lateral beyond slot 1, an M2 755 joining across slot 0's lane, a Titan 1.4 km lateral at
s −1.0 km, 50° toward its lane at 25 m/s; 3 fps × 3 seeds per variant; 60 fps seed 1 figures):

| Variant | Result |
| --- | --- |
| Staggered entry, w 0.45 r (nominal), 0.25 r, w = r | all asserted: 5/5 arrive and launch; 4 kept joins (error ≤ 0.68°), 1 slow join (12.6°, rotate 3.8 s); fallback 0; detours against (a)/(b) pairs 0; E `suppress_lane` 12,808/12,808 (a)-pair calls, 7,059 engine warns of (a) pairs all suppressed; the adjacent Titan–Colossus pair (a) in its corridors 6,618 pair frames, (c) out of them 4,368; gate row join/slow/hand-over 14,697/4,185/4,905 frames, 0 unsuppressed gate warns; one launch at a time; box separation outside the grid zone ≥ 791 / 138 / 627 m |
| Simultaneous (§16.5 as written) | invariants asserted (0 (a)/(b) detours, `suppress_lane` 27,380/27,380, one launch at a time, (c) detours 35,464 present); targets measured, 27 TARGET MISS rows over 9 runs: fallback frames in every run (1,320 at 60 fps s1), rotate after a slow join up to 19.3 s, box overlap −516 m outside the grid zone |
| Un-latch (an uncommitted M1 standing 2.5 km in the slot-3 joiner's path for 30 s) | 8 slow_join/fallback → join transitions in 9 runs (asserted: > 0) |
| M-b (two Titans on one lane point, the slot held until 150 s) | (b) 10,396 pair frames, 0 sideways detours; after the cap binds (5,160 frames) Δs − D ≥ −1 m (D 6,560 m at rest; 2 m tolerance for the 1 unit/s floor), 0 engine warns for the pair; the leader never capped, its arrival equal to the leader alone |

**Findings:** (1) The §16.5 simultaneous starts fail the join targets for a reason outside this change: a lateral joiner
within the sphere clearance of an occupied lane is (c) until it is within 100 m of its lane (the joining corridor needs
e_max ≤ 100 m), and a capital on a detour loses the lead-in (in.d_order ≥ 0), so site B only lowers and the engine's order-4
law holds it at its 1 m/s alignment creep (traced: two capitals 2.9 km apart at 1.0 m/s for > 80 s). (2) The box hulls of
adjacent grid points overlap by the KC layout itself (0.45 r: 926 m against the 666 m pitch); the contact check exempts two
ships at adjacent grid points (within r + 1.5 km of A) and corridor pairs and asserts every other pair. (3) D as §16.1 writes
it omits the warn test's r_A·cos θ term (implemented with it, 6.4–7.1 km for 1.1 km hulls), and the §16.1 cap lets Δs fall
167 m below D when the leader brakes at the trailer's deceleration (implemented as the lower of it and the stopping-distance
form). (4) With D ≈ 7.1 km at 100 m/s closing, a trailer that enters the 6 r window behind a leader already parked at the same
point is inside D from the start.

**Flight targets** (the next run with a capital crowd; `--debug`, `ship_trace_max` ≥ 6):

| Phase | Rows / counters | Target |
| --- | --- | --- |
| install | `ship_steering_gates_capital … lane_gates=8 lane_entries=20 … corridor_m=100 slow_join_nose_deg=20 … width=lod_all` | present |
| widths | `ship_capital_census type= class= r_m= w_m= w_r= w_src=` | `w_src=lod` on every capital type, w/r 0.3–0.5 (render-node-bounds.md §6.2) |
| lanes | `ship_capital_lane` (verdict, gap, foot, e, θ, Δs, v_cap, D); `lane_exempt/follow/cross`, `e_suppressed_lane` | no `ship_avoid_detour` between a pair whose last lane row is a; settled neighbours a |
| joins | `ship_gate_capital_latch event=pred_none|slow_join|unlatch` for every capital; `gate_capital_slow_join`, `gate_capital_unlatch`, `gate_capital_fallback_pred` | aim-at-A fallbacks ≤ 1 per gate; rotate after a slow join ≤ 5 s |
| gate | `e_suppressed_gate`, `warn_pre` entries of class 18 | no capital gate warn on a validated approach outside 1.25 r_gate + r of the gate |
| cost | `join_eval_us_max`, `handler_us_p99` | ≤ ~100 µs; p99 unchanged |

**Ratified 2026-10-13 (main session):** the three deviations and four interpretations above, as built (listed at the end of
design §16.9).

**Detour speed-write fix (2026-10-13):** a capital in join or slow join on order 4 with a validated gate state keeps the join's
site-B write while the avoidance hands the leaf a detour point (`decide()`: `capital_detour`; the fixture's `steer_call` the
same): min(gate speed with the cornering term, the predictor's or slow-join speed, the (b) cap), lowered at site B to the
emergency ramp; P2's hand-over and the fallback's own write unchanged; counter `gate_capital_detour_write`. Before/after over
the whole host fixture ([detour_write_ab.py](../../verification/results/ship-steering/capital-crowd/detour_write_ab.py) →
[txt](../../verification/results/ship-steering/capital-crowd/detour_write_ab.txt); the pre-fix stdout is
[ship_steering_host_before_detour_write.txt](../../verification/results/ship-steering/capital-crowd/ship_steering_host_before_detour_write.txt)),
measured:

| Item | Before | After |
| --- | --- | --- |
| Simultaneous TARGET MISS rows (9 runs) | 27 (fallback 9, rotate 9, box 9) | **27** (fallback 9, rotate 9, box 9) |
| Most fallback in one run (frames summed over ships) | 1,870 at 30 fps = 62.3 s, 2 fallback crossings | 3,824 at 60 fps = 63.7 s, 3 fallback crossings |
| Longest rotate after a slow join | 19.3 s | 26.9 s |
| Minimum box separation outside the grid zone | −990 m | −1,319 m |
| Arrivals, simultaneous 60 fps s1 | 93–334 s | 83–167 s (no 1 m/s stall) |
| Staggered variants (60 fps s1) | (c) detours 2, (c) warns 2,144 | 3, 2,146; gate-warn span frames 5,175 → 5,176 (0 unsuppressed); a-pair frames, E calls, joins and gate-row counts equal; box min 791 → 858 m (grid zone −412 → −452 m) |
| Un-latch variant (obstacle 2.5 km on the joiner's heading, 30 s) | 8 transitions in 9 runs | 0: the joiner keeps join at speed; moved to a stationary M1 on the joiner's lane line 1.5 km before X for 40 s → 9 runs of 9 with one |
| Amendment-13 B1 (converging second capital, no lane board) | cap2 min 2,195 / 2,226 / 3,585 m | 1,659 / 2,226 / 2,852 m; checks pass |
| Every other line of the fixture's stdout | — | equal (timing fields aside) |

The simultaneous misses remaining, one line each: **fallback** — the lateral joiners (slot-1 M1, slot-2 crossing M2) are (c)
with occupied lanes until within 100 m of their own, their sphere detours (now at the join's speed) bend the pursuit until no
candidate reaches the plane within 100 m / 20°; **rotate** — the on-axis slot-0 Titan is detoured off its line by those (c)
joiners and crosses in slow join at 16° (rotate 16.6 s at 60 fps s1); **box** — the same Titan, pushed out of its corridor,
is (c) with its adjacent Colossus and the two fast detours meet (−1,095 m at 42 s, 60 fps s1). All three are the joining
footprint / sphere-clearance question of §16.8, for the design follow-up.

**Acceptance after the fix (measured):** build warnings 0; `check_no_x87.py` PASS, 964 reachable, 0 violations;
`verify_ship_steering_sites.py` PASS, 18 checks; host fixture 6655/6655 (slip_gaps 0, vanilla_notes 2); `--capital`
2028/2028 (`--capital-noslip` report 34 failures); 55 host tests OK; the Run158 replay's `replay_lag.py` output byte-identical
to `replay_lag_run158.txt` (|flown − model| p95 0.00°). DLL sha256 `dc328968…` (worktree build, not a candidate).

**Amendment 16b and the review fixes (2026-10-13, Opus 5.5; design §16.10 with its ratification, implementation §16.11).**
Worktree on `2952a324`, not committed, not installed. Sources: `ship_capital_lane_core.h` (verdict (s), `Route`, the motions on the
board, `pair_verdict16b`, `verdict_sym`, `certified`, `seq_obliged`, `seq_cap`, `Pass` / `run_pass`, `seq_lapses`, `apply_cap`,
`width_reread`), `ship_avoidance_core.h` (`Self.lane` = the pass, `lane_bind(Self&, const Pass*)`, `lane_excludes`; `excluded()`
removed), `ship_avoidance_walk_core.h` (budget 128, scale bound), `ship_steering_core.h` (`kShipFlagLower`), `ship_steering.cpp`
(the pass in `decide()`, the cap at site B, E's symmetric certified verdict, the width retry, rows and counters, key
`X3M_SHIP_STEERING_GATES_CAPITAL_SEQ`), `tools/config/schema.py` (+ generated `config_schema_inc.h`, `assets/x3m.ini`). No hook site.

Review findings on `40383a1e`:

| Item | Fix |
| --- | --- |
| F1 E suppressed corridor-only (a) | E's `lane_exempt` is now `certified(verdict, route)`: footprint (a) or (s) lat / along. Corridor-only (a) pairs pass E (the engine's warn stays live) and are still never detoured; scenario M asserts both halves (`E corr lane=0/6618` and 0 (a) detours at w 0.45 r, 60 fps s1). |
| F2 cap only under `join_write` | One pass per order-4 capital decision (`run_pass`, before the avoidance) feeds the exclusion and site B; `apply_cap` only lowers: the gate write's v_ref when it is a write (> 0; not under an avoidance emergency, where the emergency ramp floored at 10 m/s writes: accepted, exercised by a unit check, 0 such decisions in scenario M), the lowering clamp's cornering term under site B's own computation (ramp with the amendment-14 v_slip, then v_corner; `kShipFlagLower` drops only the final 10 m/s floor), and on the engine's law with no module write a lowering-only write of the cap (`clamp_units`). A sweep of 144 states: the capped write never above the uncapped one. |
| F3 budget 64 | `kHullBudget` 128 (100-node tree reads, 140 fails closed: unit checks). |
| F4 cache miss cached w = r | `width_reread`: a failed read retried every 5 s, at most 3 times, then w = r until the radius bits or the root change; census row once final with `retries=`. |
| F5 `lane_bind` freshness | The pass binds only on the ship's own entry that is fresh (`committed_stored`), on its order, on its live target's board (unit check: stale 300 ms unbound). |
| F6 asymmetric verdict | `verdict_sym`: both sides from the stored board, both directions, the more severe (rank a-foot < s < a-corr < b < c < none). Random check: 400,000 side pairs, 0 asymmetric (the as-built function is symmetric on stored sides, 0 of 400,000: its asymmetry came from the live/stored mix). |
| F7 int64 operands | Node and axis scales outside (0, 16.0] fail the read (unit checks). |
| F10 | the duplicated scenario comment merged, stagger times 180 / 340 s. |
| F12 | `excluded()` dropped; the fixture tests `excluded_base` and `lane_excludes(lane_pair())`, the resolve's path. |
| F8, F11 | no change. |
| F9 (watch) | slow_join / fallback capitals stay in the 8-slot `JoinRegistry`; a crowd of more than 8 capitals refuses registrations (`join_slots_refused`). Watch it in the next crowd run; no change. |

**Acceptance (measured, WSL2, MinGW GCC 16.2):** build warnings 0; `check_no_x87.py build/d3d9.dll` PASS, 968 reachable, 0 violations
(`seq_cap` and `Pass::cap` are `always_inline`: out of line they returned in st(0)); 969 reachable after the second review round;
`verify_ship_steering_sites.py --exe /mnt/c/X3/X3AP.exe` PASS, 18 checks; host fixture
([output](../../verification/results/ship-steering/ship_steering_host.txt)) **7260/7260**, slip_gaps 0, vanilla_notes 2; `--capital`
([output](../../verification/results/ship-steering/capital-gate-transit/fixture_capital.txt)) **2633/2633**; `--capital-noslip` (report) 43 failures (34 before: the 7 new are scenario M without the slip term, 6 rotates of
5.0 s at the 5 s bound and one cap-invariant miss of 185.5 m at 60 fps s7); 55 host tests OK (the six ship / config / launcher
modules); Run158 replay → `replay_lag.py` byte-identical to `replay_lag_run158.txt`; E vs amendment 15 on 400,000 random inputs with
the capital key off: 0 differences. DLL sha256 `0a947cb2…` (worktree build after the second review round, not a candidate). Every fixture line outside scenario M
and the amendment-16 unit block is unchanged (timing aside;
[seq16b_ab.py](../../verification/results/ship-steering/capital-crowd/seq16b_ab.py) →
[seq16b_ab.txt](../../verification/results/ship-steering/capital-crowd/seq16b_ab.txt), A: HEAD → this change).

**Scenario M, simultaneous** (kCrowd, 3 fps × 3 seeds each; the 27 TARGET MISS rows are assertions at w 0.45 r and 0.25 r; w = r
reported), measured over the 9 runs:

| Variant | Arrive / launch | kept + slow | fallback | (c) frames with an approach | certificate violations | trailer Δs − D_x min | box min | rotate max | longest floor span |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| w 0.45 r | 45/45 | 36 + 9 | 0 crossings, 0 frames | 0 (704 against a launch, reported) | 0 | −0.0 m | 249 m | 4.0 s | 67.0 s (reported) |
| w 0.25 r | 45/45 | 36 + 9 | 0, 0 | 0 (0) | 0 | +41.1 m | 138 m | 4.0 s | 26.6 s |
| w = r | 45/45 | 36 + 9 | 0, 0 | 146,035 (11,489) | 0 | 0.0 m, 9 lapses | −333 m | 3.8 s | 145.4 s |

Last arrivals 179 / 174 / 256 s (before: 83–167 s with 3 fallback crossings and a 26.9 s rotate at 60 fps s1). The w = r variant is
(c) where the note expects it (the Titan s0 / M2 s2 pair needs 2,662 m and starts at 2,641 m).

**Staggered, M-b, un-latch** (60 fps s1, before → after): staggered w 0.45 r (c) pair frames 8,772 → 2,930 with (s) 4,406 lat +
1,438 along, (c) detours 3 → 1, the adjacent 1.1 km pair (a) 6,618 / (c) 4,368 → (a) 6,618 / (c) 2,930 / (s) 1,438, E suppress_lane
12,808/12,808 → 6,188/12,806 (the 6,618 corridor-only calls now pass: F1, 6,618 engine warns left live), box min 858 → 1,534 m, rotate
3.8 → 4.0 s, arrivals 67–430 s unchanged; at w 0.25 r (c) 5,194 → 2,774 and every (a) by the footprint; M-b (c) frames 12,508 → 7,900
(stored sides, F6), (b) 10,396 unchanged, Δs − D ≥ −1 m; un-latch 9 transitions in 9 runs (unchanged). All asserted.

**Sub-scenarios** (asserted, 9 runs each): M-c — the first classification (c), (c) detours present (736 at 60 fps s1), every warn on
a (c) frame left to the engine (1,623/1,623), the M2 at the floor from its first bound decision, 0 certificate violations, box −288 m
(they can meet; reported). M-d — a_T > a_L (the M2 s3 behind the Colossus, the leader stopped at the first frame of the along route:
the M2 never closes back to D_x, its join is slower) and a_T < a_L (a Colossus joiner 1 km beyond its lane, 300 m behind the slot-0
Titan, the Titan stopped once the cap binds with Δs − D_x < 100 m): Δs − D_x ≥ −10 m on every obliged frame (min −0.0 / 0.0 m),
0 violations, 0 lapses.

**Cost (host x86-64 -O2, measured):** a decision's lane pass on a full gate, 19 neighbours in both directions: 0.59 µs settled,
1.07–1.10 µs with 19 watch neighbours (16 watch, 16 obliged); E's capital-pair work mean 0.14–0.18 µs per call in scenario M (0.12–0.14
before); predictor ([pred_cost16.txt](../../verification/results/ship-steering/capital-crowd/pred_cost16.txt)) 81.2 µs over 5 sweeps
with each state's best kept (one sweep: 84.7–84.8 µs; separate single sweeps measured 85.9 µs and 148.9 µs, the latter on amendment
15's unchanged `join_choose`, at states that measure 81 µs in every other sweep: host preemption across all 7 back-to-back runs of one
state, not the code; the multi-sweep form bounds it). The predictor is not modified. i686 figures not measured.

**Static along-axis check** ([capital_widths.py](../../verification/results/ship-steering/capital-widths/capital_widths.py) →
txt/json, measured over the 131 installed capitals as the runtime walk sees them, all LOD records per part): l = max over parts of
|o_z| + e_z ≤ h = √(r² + w_rt²) for **130 of 131** (l/h median 0.914, max 1.324); the exception is the unnamed Stellaris M1
`machineFlagship` (TShips 509, r 4,583 m, l 6,417 m, h 4,847 m). Production therefore uses the half-length max(r, l) with l from the
same width read (h = √(max(r, l)² + w²); w = r on a failed read gives √2 r = 6,481 m there).

**Flight targets** (the next crowd run; `--debug`, `ship_trace_max` ≥ 6), added to the amendment-16 table:

| Phase | Rows / counters | Target |
| --- | --- | --- |
| install | `ship_steering_gates_capital … width_retry_ms=5000 width_retries=3 seq=1 seq_lat_gap_m=50 seq_along_m=50 seq_cap_extra_m=100` | present |
| widths | `ship_capital_census … retries= l_m= h_rw_m= len_m= h_m=` (h_rw = √(r² + w²); len = max(r, l), h = √(len² + w²) the rule uses) | `w_src=lod`; l_m ≤ h_rw_m except the Stellaris flagship |
| lanes | `ship_capital_lane … route= watch= lat_gap= mot= h_T= h_L= Dx= trail= seq_cap= bind= held=`; `lane_seq_lat/along/cross/capped`, `lane_capped` (writes the cap lowered, as Run158), `lane_cap_carried`, `lane_hysteresis_hold` | 0 `ship_avoid_detour` against a pair whose last row is a or s |
| invariant | `lane_seq_lapse` | 0 |
| liveness | `lane_seq_floor_ms`, rows with `bind=1` | no capital at the floor > 60 s outside the hand-over (fixture: 67.0 s at w 0.45 r, 30 / 60 fps; see §16.11) |
| latches | `ship_gate_capital_latch … seq_cap= seq_leader=` | latches under a binding cap counted |
| E | engine evasions between corridor neighbours (F1) | expected again (vanilla's own); none for a pair whose last row is s or a-foot |

**Second review round (2026-10-13, on `0c261c96`), all in place above:** `apply_cap` lowers only (above); `lane_capped` counts the
writes the cap lowered again (comparable with Run158), `lane_cap_carried` every decision carrying a cap; the census row labels
`h_rw_m` = √(r² + w²) beside `len_m` and `h_m`; an unbound decision clears the lapse memory and `seq_lapses` reads the objects the
pass captured, never the board after `refresh()`; the fixture's decision mirror has the DLL's `(!v_emergency_bits || capital_detour)`
gate-write condition (no change in any output line); the box classification of the KC launch is narrowed to the launch leg itself
(the ship on order 5 / its P4 stop between r + 1.5 km before its A and the gate, against the adjacent lane point; the −217 m of
60 fps s7 is still in it, every other classified separation unchanged). `box_unclassified` −285 m (simultaneous w 0.45 r, 60 fps
s1; reported) is the start geometry: the slot-0 Titan and the slot-1 Colossus abreast on adjacent lane axes at 5 r (491 + 460 m half-widths
against the 666 m pitch) before the board has classified them; from the first classification they are a corridor (a) pair.

**Hysteresis on the in-lane classification (ratified addition, §16.11):** in at |q − ℓ| ≤ 100 m and ≤ 8.11°, held until > 150 m or
> 12°; a held state uses the exit values in the margins (footprint margin 37 m, a hand-over's nose floor 12° in the lateral
certificate, the leader credit less v (1 − cos 12°)); the bit lives on the board entry and is cleared by another order, order 5 or
a refresh gap > 1 s. Fixture (scenario-M geometry, measured): the Colossus oscillating at 110–140 m / 9–10° next to the in-lane
Titan stays in lane on 40 of 40 decisions, (a) with the Titan, 0 detours (unbound: a conflict on every one); at 13° or 160 m the
pair is (c) / (s); never entered at 110 m / 9°: not in lane; the bit cleared by another order and by a 1,001 ms gap. In the
dynamic runs it holds 0 decisions at w 0.45 r and 0.25 r (115 in the w = r simultaneous run, 83 in M-c at 60 fps s1); every
scenario-M row is unchanged. Row `held=`, counter `lane_hysteresis_hold`, install row `lane_hold_m=150 lane_hold_deg=12`.
X3M_SEQ_OFF stdout tracked ([ship_steering_host_seq_off.txt](../../verification/results/ship-steering/capital-crowd/ship_steering_host_seq_off.txt), 63 KB).

Open from this entry (§16.11): the 60 s liveness row is reported, not met at w 0.45 r (67.0 s in 6 of 9 runs); approach-vs-launch (c) frames and
the launch-transient box overlap are measured apart; the hand-over attitude bound (8.11°) is an assumption the fixture's free drift
(3.7° in 4 s) satisfies; the 16.1 footprint's r cap is not a cylinder bound (a pre-existing gap in the footprint certificate at large
noses).

## Open flight runs (native Windows, the user; the rows that decide them)

1. **Run A, steering baseline.** Same save twice (`ship_steering = 0`, then `1` in `x3m.ini` or `--ship-steering`),
   a busy sector with traffic, `--perf`. Decides: `ship_steering installed=1 sites=4` and four `ship_steering_site …
   status=active` rows (a `ship_steering_refused` row means the module is off: stop); `ship_steering_summary`
   `pass_*` counts other than `pass_order_not_steered`/`pass_class_excluded` ≈ 0 (`pass_no_autopilot` ≈ 0 settles the
   open question whether NPC ships carry `0x80000`); `handler_us_p99 ≤ 5`, `handler_us_max ≤ 50`; `calls_per_ship_s`
   20–60; `ship_steering_stuck` = 0 rows; `clamp_applied` > 0 near arrivals.
2. **Run A, `--debug` sample.** One `ship_steering_decision` row per ship class: `e=` signs against the next frame's
   `obj+0x18..+0x20` delta confirm the pinned sign conventions in flight; `k=` gives the record's `+0xd0` per class
   (the fixture assumed 0.5/0.3/0.1); a ship rolling without settling means the level policy's world-up assumption
   ([i]) is wrong (then `ship_steering_classes` can exclude the large classes until fixed).
3. **The user's report.** Turns without the yaw-then-pitch kink, no orbiting at MovePos arrival, formations holding
   their slots, docking and gate transit as before (a dock and a gate jump by an AI ship observed), the player's
   own autopilot unchanged.
4. **Run B, avoidance.** The same busy sector with `ship_avoidance = 1` (or `--ship-steering --ship-avoidance`),
   `--perf --debug`. Decides: `ship_avoidance installed=1 warn_status=active` (any other `warn_status` = avoidance
   without the suppression: report it); the summary's `conflicts`, `detours`, `unresolved` (≤ 5 % of conflicts),
   `snapshot_overflow` = 0, `snapshot_max` (the sector's obstacle count), `e_suppressed` vs `e_calls` and the collide
   census's `CollisionWarn` dispatches ≥ 50 % below the step-1 baseline; `part_models` > 0 with `part_fallback` ≈ 0 and
   `part_invalid` = 0; the `ship_avoid_census` row's class histogram (17 = asteroids in the expected numbers, 5/6
   stations, 18 gates) and the `ship_avoid_station` row (`model=parts`, scale `0x10000`, child radii below the root)
   confirm the node layout statically assumed; `ship_avoid_detour` samples with `kind=` and `C_m` plausible; the
   user's report: no ship scraping a station, traffic crossing without the stop-and-avoid churn, formations crossing
   obstacles and re-forming, docking and gate transit as before.
5. **Run C, gates (after Run B, or with it).** `ship_steering_gates` on (the default): `ship_steering_gates
   installed=1 status=active` and three `ship_steering_gate_site … status=active` rows (a `ship_steering_gates_refused`
   row = the policy is off, steering stays: report it); the summary's `gate_crossings` > 0 with
   `gate_stops_skipped` ≈ `gate_p2` for non-capital gate arrivals, `gate_v0_kept` ≈ `gate_p4`, `gate_launch_continued`
   ≈ `gate_p5` / 54 (one launch = 54 frames at 60 fps), `gate_speed_writes` > 0; `--debug` `ship_gate_approach` rows
   with `cross_v_ms` ≥ 0.5 vmax of the class, `cross_heading_err_deg` ≤ 18.2 and `cross_to_launch_ms` below the
   vanilla model's (section 10.6: M5 8 s, TS 20 s); the user's report: small ships flow through gates without the
   stop-turn-stop at the approach point, capitals as before. A ship stuck at a gate or a collision with the gate
   ring (the lead-in crosses 0.75 r above the axis, inside the ring's hole only if the model's hole is at least
   0.55 r wide: open in section 10.8) ends the run.
