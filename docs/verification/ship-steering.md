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

Step 2 (avoidance, site E, the warn suppression) follows the first accepted flight.
