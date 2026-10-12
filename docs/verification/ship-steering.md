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
| Host fixture | `PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_ship_steering` ([output](../../verification/results/ship-steering/ship_steering_host.txt)) | **checks PASS, 3365/3365** (2203 before the Run151 follow-up, 2341 before step 3, 3020 before its review): step 1's 1164 plus the step-2 unit checks (the snapshot and part walks over a synthetic sector: bucket table pointer, intrusive lists with the 0-dword sentinel, scaled and unscaled children, a dirty child, a sentinel-only list, a part outside the root sphere, a > 64-child station, the walk limit, no table; the cache by object/radius/stamp and its invalidation; the review's head-on case never suppressed inside D_stop) (the §3.3 formula reproduces the design's L numbers at the design's 0.5 s reaction and the implemented L at 1 s; closest approach on head-on, crossing and clear cases; part validation; E aging; the exclusion rules; overflow pass-through; a constant-side detour with the resolved set; the station fallback never suppressed) and 5 ships × 3 fps × 3 seeds × {flyby at 600 m, flyby at 150 m, crossing, formation + asteroid} |
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
