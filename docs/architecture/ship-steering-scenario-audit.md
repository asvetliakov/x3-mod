# Ship steering, avoidance and capital gate transit: scenario audit before the next build

Design note, 2026-10-12, for the main session. Decision: which scenarios the three ratified designs
([ship-steering-and-avoidance.md](ship-steering-and-avoidance.md) amendments 1–13,
[capital-gate-transit.md](capital-gate-transit.md) with amendment 15, [slip-aware-steering.md](slip-aware-steering.md)
= amendment 14) leave uncovered or cover wrongly, and what to fix before the next build versus later. Nothing is
implemented here and nothing was launched.

"Installed" means Run157 (`af5ec4cf`, DLL `ecd05046…`). `git diff af5ec4cf HEAD -- src` is empty, so the source equals
the installed code. "Next build" means installed code plus amendment 15, which is ratified but not in source (no
`e_suppressed_member`, `e_suppressed_gate` or `capital_join` symbol exists yet). Amendment 14 (slip-aware) is ratified
and not implemented.

Markers: **[m]** = a measured flight row, a listing or code read in this session. **[i]** = inferred. **[p]** = proposed.

Evidence produced for this note, all under
[`verification/results/ship-steering/scenario-audit/`](../../verification/results/ship-steering/scenario-audit/):

* [`engine_warn_tally.py`](../../verification/results/ship-steering/scenario-audit/engine_warn_tally.py) →
  [`engine_warn_tally_run157.txt`](../../verification/results/ship-steering/scenario-audit/engine_warn_tally_run157.txt).
  For every traced ship that entered the engine's 0x12, the obstacle the warn named, and how long the 0x12 leg lasted.
* [`seta_freshness.py`](../../verification/results/ship-steering/scenario-audit/seta_freshness.py) →
  [`seta_freshness_run157.txt`](../../verification/results/ship-steering/scenario-audit/seta_freshness_run157.txt).
  The game-time frame step with and without SETA, compared with the module's freshness windows.
* [`part_model_tally.sh`](../../verification/results/ship-steering/scenario-audit/part_model_tally.sh) →
  [`part_model_tally_run157_run156.txt`](../../verification/results/ship-steering/scenario-audit/part_model_tally_run157_run156.txt).
  How often stations got a part model and how often the snapshot overflowed.

## Recommendation

Before the next build, fold three corrections into the amendment-15 implementation, add four fixture cases and run
them. Ship nothing else new. Each of the three corrections is a compare at site E or a constant. None needs a new site.

1. **Gate-warn row for small ships (gap G1).** Site E suppresses the engine's warn for a non-capital A against its
   own order-4 gate target. This applies only while A is on the validated lead-in, inside its window
   (−3 r ≤ s ≤ 0 along h). Run157 measured the problem: 29 of the 39 first-0x12 rows of traced small ships name the
   gate itself as the obstacle. These are TS (16 legs, 125.3 s) and M4 (13 legs, 26.6 s), at 1.5–3.1 km from the
   gate centre **[m]**. They are the stop → 0x12 → re-issue legs on the small-ship approach (the Run155 "gap before
   order 5"). Amendment 15's gate row covers only capitals in join mode.
2. **Freshness counted in cadence, not fixed milliseconds (gaps G2, G3).**
   * The capital branch accepts A when its last decision is younger than `max(250 ms, 200 ms + 2 × frame dt)`. The
     0x12 leaf runs every 200 ms, and site E runs before the interpreter in the frame.
   * The gate row's lifetime is keyed on the gate state: kept speed at P2, order 4, 0x1f or 0, until order 5 starts.
     It is not keyed on A's decision stamp, because 0x1f and order 0 make no decisions.
   * Under SETA the measured frame step is p50 60 ms and p90 70 ms (69 % of frames) **[m]**. That puts the age of
     A's 0x12 decision at site E at 240–280 ms, above the 250 ms window, so amendment 15's 0x12 fix would flicker
     off about one frame in four **[i]**.
3. **Small-ship row: keep the engine's warn for B on order 5 (gap G4).** Order 5 is in the steer set, but the module
   turns avoidance off on it (`core::order_policy(5) = {steered, no ramp, roll 0, no avoidance}`).
   * On gate legs the KC also ignores B's own warn (`m45 = 0xA0001`).
   * The installed amendment-13 verdict already counts B on order 5 as "steered" and suppresses. Amendment 15's
     unconditional row does the same.
   * The result is a pair (a capital and an M6/M8/TM docking or on a gate order-5 leg) in which neither side avoids.
   * This narrows the user's ratified rule by one order. The main session decides. If the rule is to stay as it is,
     ship the fixture case K and the `ship_avoid_e_verdict` tally by `B_order` instead.
4. **Fixture cases to add and run before the build** (no code change unless they fail). The full list is under
   Verification below:
   * H: the small-ship gate warn;
   * I: a capital approaching a grid slot next to an occupied one;
   * J: SETA frame steps of 100–167 ms;
   * K: a capital passing an M6 that is on order 5 to a station.

Everything else in the table is either covered or waits for a flight row.

**Hot-path cost of the three corrections** **[i]**:

* G1: at site E for a non-capital A, one compare of B against A's `f+0x224` (already read), one class-word read and
  one gate-state lookup. The window flag is already in the table slot.
* G2: one multiply-add with the frame dt (already read for `servo_tick`) and one gate-state flag test.
* G3/G4: one order compare.

No allocation, lock or formatting is added, and site E stays at ≈ 10 ns per pair.

## Scenario table

Severity: **S** = safety (hull contact or damage that vanilla would have avoided), **R** = behaviour regression
against vanilla, **C** = cosmetic or diagnostic only, **—** = no gap. Cheapest check: **fx** = a host-fixture case
(`verification/probe/ship_steering_host.cpp`, letters H–L are new), **row** = a flight row that already exists or that
amendment 15 adds.

| # | Scenario | Installed code does (file:function) | Design intends | Gap | Sev. | Cheapest check |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | Capital docking at a station or shipyard (order 4/5 with a port) | The lead-in is gate-only: `decide` builds the gate state only when `f+0x224` has class 0x12, so a capital at a port flies the generic law. Order 4 gets the arrival ramp to clamp(r, 22200, 111000) units and roll 0. Order 5 gets roll 0, the port hold and no avoidance (`core::order_policy`). The final zone (`2r + 25000`) is engine-owned: the leaves are not called. `av::excluded` drops the order's target station for the whole of orders 4 and 5 (`exclude_target`), not only inside 2 r_container as §3.1 says. | The engine owns the port path. Our law only shapes the approach. | Code and design diverge on the target exclusion, but this matches vanilla: the engine's container detour (< 2 r) and its mesh warn stay, and the target is never in the resolved set. Measured side effect: a traced TP took 6 engine 0x12 legs (56.5 s) against its own dock station on order 4 **[m]**. There is no vanilla baseline to compare. | R? (baseline unknown) | row: the `ship_avoid_engine_warn` tally (`engine_warn_tally.py`) in a steering-off flight of the same save |
| 2 | Capital near a dock with small traffic | Amendment 13 suppresses the capital's warn against a small B whenever B's order is in the steer set. That set includes **order 5**, on which the module does not avoid. | Amendment 15: suppress for every small B on any order, except the manual player, deleted/attached/docked ships and masked classes. | **G4.** Small ships on dock or gate order-5 legs, undocking ships (0x1e, and under Mayhem SafeUndock not even in the pair loop), parked ships (order 0) and gliding ships (0x20) do not dodge under the module. Whether the KC lets them react on dock legs is not established. A capital now ignores them where vanilla reacted for M6/M8/TM. Amendment 15 lists 0, 0x1e, 0x20, 8, 0x21 and masked classes as residual risk, but not order 5. | S (M6/M8/TM only; vanilla capitals already ignore fighters, TS and TP) | fx K: an M2 on order 4 past a station while an M6 on order 5 crosses its path at 1 km. Assert the verdict for (M2, M6 on order 5) and the minimum distance. row: `ship_avoid_e_verdict` tallied by `B_order` |
| 3 | Capital on attack 0x10/0x11 against a capital | The target is excluded (`exclude_target` includes 0x10/0x11/0x18). The ring point is min(rA + rB, 200 m) from the target's centre, so it lies inside any capital's hull, as in vanilla. Speed: no ramp, cornering only (lower only). Site E: the target is never resolved, so the engine's warn (close range d ≤ rA + rB) runs. | Same as vanilla. | None beyond vanilla. The module's speed is never above the engine's. | — | none needed |
| 4 | Capital on attack against a fighter or M6 | The target is excluded and small. Amendment 13 at E: suppress only if B is steered. | Amendment 15: always suppress for a small B, the attacked target included. | The capital never reacts to the M6/M8/TM it attacks. Its ring point is ≤ 200 m from that ship. Vanilla reacted for "large" targets. | R (S only if the target stops) | row: `ship_avoid_e_verdict` with A on 0x10/0x11 and B == A's target |
| 5 | Jumpdrive arrival (0x21) | 0x21 passes through at A. Site W offsets non-capitals 0.3 r sideways and never offsets capitals (`gate_warp_in_offset`). The order-8 handler builds `p+0x100 = 0` **[m]**; that 0x21 does the same is **[i]**. So for 0.9 s the snapshot sees a 6 km/s ship as stationary. After that, 0x1e passes through. The capital branch at E is stale on 0x21/0x1e, so the warn behaves as in vanilla. | As built. | A small ship near the exit predicts a stationary arriving ship for 0.9 s. | C | none needed (only the engine's warn acts in this window) |
| 6 | Two or three capitals at one gate (slot queue, adjacent grid points) | Adjacent grid points are 666 m apart. For two M2s (r 1091 m) C = 2.7 km, so neighbours sit **inside each other's C** by construction **[m]** (KC grid, `av::clearance`). A capital approaching slot 1 while slot 0 is held sees a conflict at miss 666 m < C. It then either takes a 1.75 C (4.7 km) detour, which suspends the lead-in because the lead-in needs `d_order < 0`, or has no detour, in which case the emergency ramp brakes it to 10 m/s as d falls to C and stops braking inside C (`resolve`: emergency only for d > C). The engine's close-range warn (1.2 (rA + rB) = 2.6 km) then gives 0x12 as in vanilla. | Capital transit §5.2: "one capital in the 6 r window per gate", handed to the queue. Fixture C covers only two capitals *converging*. | Unmeasured: a slot approach next to a *stationary* neighbour, the most common case at a busy gate. The module adds a detour or a brake dip ahead of the vanilla churn **[i]**. Whether the real hulls touch at 666 m is unknown. | R | fx I: slot 0 held by a stationary M2, a second M2 joins slot 1 from 6 r. Assert the time to the sphere and the minimum speed; compare with the same run without the neighbour |
| 7 | Capital arriving from behind the gate, or beside it at A's depth | Outside the window (s > 0, or more than 6 r out): the engine's law with the lower-only clamp, then the alignment creep. Run157's M1 spent 103.8 s at 1–21 m/s **[m]**. From behind, the engine's container detour changes the point before the leaf, so validation fails on those calls (validated = 2). | Amendment 15: latched fallback (`past_x` / `pred_none`), slow entry at about 25 m/s, P2 vanilla stop. The gate row applies only in join mode. | Covered by amendment 15. From behind, the gate warn stays (fallback mode), so the engine's must-turn-away 0x12 can still fire, as in vanilla. | — (after A15) | fx G (amendment 15) already has `past_x`. Add a start behind the gate plane |
| 8 | TL (slow turn), stock records | Live getters. The capital window and look-ahead come from the live W, the predictor (amendment 15) from the live α. | All parameters come from the live getters. | Every capital fixture and feasibility number uses Mayhem records (9–10 °/s, k 0.05) **[m]**. Stock TL/M1/M2 turn at 1.2–3.6 °/s; for them the 6 r window and the predictor are unverified and would fall back **[i]**. | C (fails closed) | fx E/G with one stock TL record (2.1 °/s), asserting fallback or join, never a stall |
| 9 | M7: "large"/capital for the engine, under r/3 for an M2 | M7 is capital-coded (`kCapitalMask`), so an M2 keeps it as an obstacle and it keeps the M2. An M7 (median 560 m) ignores ships under 187 m: every M6. Whether the M7 takes the KC capital queue (class 2019) is not established. If it does not, it flies to the shared point A: grid validation fails, so vanilla order 4, a P2 stop at A, then order 5 to the gate **origin** (native capital predicate). | Not addressed. | A stopped 560 m M7 at the small ships' shared point A sits on their lead-in lines. Their detours there cut the lead-in. Frequency unknown. | R | listing: KC class chain of M7 (`kc_gate_flow.py` style); row: `ship_gate_transition`/trace with `class=M7` |
| 10 | TM / M8 | Not capital-coded, so they avoid everything like small ships. For an M2 they are under r/3 and ignored, with E acting. | As built. | Same exposure as row 2 for TM/M8 B (they are "large" to the engine). | see 2 | see 2 |
| 11 | Player's own capital under autopilot | Steered like an AI capital (autopilot bit). The E small rows suppress its warns against small ships. In vanilla its warn went down the KC default branch, because the player branch needs autopilot off. | Same. | None beyond the AI capital. For the player's own transit (`this == g9` → `150::StartGateWarp`) the grid validation probably fails, so vanilla. | — | row: the player's ship in `ship_gate_capital_join` (reason `grid_invalid` expected) |
| 12 | Player's manual small ship near an AI capital | E: `small_pair_verdict` → `no_autopilot` → pass (amendment 13). Amendment 15 → `e_small_pass_no_autopilot`. The capital's **predictive** filter `small_for_capital` has no autopilot test, so the capital does not avoid the player predictively. | "Manual player excepted everywhere" (amendment 15 §15.1). | The reactive warn is kept, as required. The predictive layer still skips the manual player, so the capital's answer is the engine's 0x12, as in vanilla for an M6 player. It is not excepted "everywhere". | C | fx: the existing scenario B's manual M6 (verdict `no_autopilot`). Add the capital's `skipped_small` for that ship |
| 13 | Capital leader with fighter escorts crossing a station field | The leader's detours prefer the vertical family. The slots turn with the leader's yaw and pitch. Escorts use L = max(L_own, L_leader) and avoid stations themselves. The formation is excluded in every direction (`av::excluded`), but only through the live `f+0x224` on 0x14/0x15. | Amendment 15: a 20 s membership memory. | Installed: membership is lost on the engine's transients. Run157 measured 195 leader detours against escort capitals with leader 0 **[m]**. Fixed by amendment 15. | R (installed), — (after A15) | fx D (amendment 15) |
| 14 | An escort that is itself a capital; escort slots 4 × size for a 1 km leader | Wingmates and leader are excluded from each other. The slot is 4 r (`GetFormationDist`) away, which is more than C (2.7 km) for 1 km hulls. | Same, plus the membership memory. | While on 0x14/0x15, none. When membership lapses (legs over 20 s: 10 of Run157's 20 legs) the pair returns to vanilla reactive behaviour, which is accepted. | — | row: `member_expire` against `member_set` |
| 15 | SETA ×10 | Everything runs on game time (`[*0x00606f34+0x718]`). The resolved set expires after 100 ms, the capital branch after 250 ms, the site-B clamp after 250 ms, the servo horizon is clamped to 50 ms, P5's v0 to 5 s, a table entry is stale after 5 s. Measured: with SETA the frame step is p50 60 / p90 70 / max 1230 ms; without it p50 5 ms **[m]**. | All figures are game time (amendment 15 §15.2). | **G2/G3.** (a) The 0x12 leaf runs every 200 ms and site E runs before the interpreter, so the age at E reaches 240–280 ms with 60–70 ms frames: the capital branch flickers off on 0x12 **[i]**. (b) At 60 fps real under SETA ×10 (167 ms steps) the 100 ms resolved-set window never covers the frame, so the first branch never fires; at 30 fps (333 ms) the capital branch never fires either **[i]**. Both fail closed to vanilla. (c) When the frame step exceeds 50 ms the servo's rate surplus covers only part of the frame, so the rate sags by `a × (dt − 50 ms)` per frame **[i]**. | R (all fail closed) | unit: the freshness predicate at dt = 70/167/333 ms with the 200 ms re-plan; fx J: 6 fps steps (167 ms) on the M5/TS turn and the capital join |
| 16 | Out-of-sector ships | Hooks run only for native objects. Outside the player's sector there are none (`m3 == 0` branch, navigation §10.1). Each sector gets one snapshot per frame (4 slots). Run157: one sector census, 152 objects **[m]**. | Same. | none | — | — |
| 17 | Mayhem 4–5× speeds against the 30 s horizon and the 30 km look-ahead | `av::lookahead` = 1 s · v + v · 2/k + 1.25 min(d_turn, d_stop), clamped to 30 km. Horizon = min(L/v, 30 s). M5 at 766 m/s, k 0.5: L ≈ 4 km, 5 s **[i]**. TM at 448 m/s, k ≈ 0.1 (assumed): ≈ 9 km, 20 s **[i]**. | Same. | The 30 km clamp binds only at very low k and high speed. A fast, slow-ramping TM sweeps 9–13 km, so it is over-cautious **[i]**. | C | fx: the TM record in the crossing scenario (detour count) |
| 18 | Asteroid field with 200+ rocks | Snapshot capacity 1024 per sector. On overflow `resolve` returns the point unchanged for **every** ship in the sector. At most 8 candidates (earliest t_cpa) per call; a detour point is checked for "blocked" only against those 8. | Same (§3.2). | Mayhem has up to 732 asteroids per sector **[m]** (game-scale §6), plus ships, debris (class 28) and stations, so the worst sectors could overflow and turn avoidance off sector-wide. Run156/157: `snapshot_max` 142/152, overflow 0 **[m]**. A 9th rock is not tested, but its pair is never suppressed, so the engine's warn stays. | R (fails closed) | row: `ship_avoid_census` / `snapshot_max` in the densest asteroid sector the user flies |
| 19 | Stations near a gate; 52 km hollow bases | Part model: depth 3, 64 nodes, replicated subtree radii. Otherwise the fallback body max(0.35 r, 500 m), never suppressed at E. Run156 and Run157: 9 of 10 stations fell back on the first snapshot and all 10 had models later (`part_models` 10, `part_fallback` 9, `part_invalid` 9) **[m]**. | Fallback as the backstop (§3.5). | A Lost-Colony base (52.5 km sphere) that fails the walk gets an **18.4 km** fallback body. A ship approaching it brakes to 10 m/s at d = C (`resolve`: emergency only for d > C), and ships inside it report unresolved on every call **[i]**. Not flown. | R (possibly severe locally) | row: `ship_avoid_station` with `root_r_m > 20000` and its `model=`; fx L: a ship approaching a 52 km fallback body, minimum speed and detour length |
| 20 | Gate as an obstacle on the final leg; order 4 of a capital | Order 5: target excluded, avoidance off; the engine's pair filter skips ship–gate contact; the KC ignores warns (`m45`). Order 4: the gate is excluded from the module's set, but the **engine's** cone warn against the gate stays. Run157: one capital warn at 6898 m **[m]**. | Amendment 15 gate row for a capital in join mode, "covering the last 1.8 km of order 4 and the coast". | **G3.** The row needs A's decision to be at most 250 ms old. 0x1f and order 0 are not steered, so the row lapses 250 ms after P2: the "coast" claim of §15.3 item 5 does not hold. The KC effect of a warn during the through-slot wait is a `SA_StopObject` that interrupts the wait (a re-poll, not 0x12) **[i]**: low, but the note claims otherwise. | C/R | `warn_pre` rows (amendment 15) between P2 and order 5 |
| 21 | Gate as an obstacle on order 4 of a **small** ship | The gate (the order's target) is excluded from the module's set. The engine's cone warn (B = gate, not a mesh-test class) runs at E. **Run157: 29 of 39 small-ship first-0x12 rows name the gate**: TS 16 legs, 125.3 s; M4 13 legs, 26.6 s; at 1537–3108 m from the gate centre **[m]**. | Not addressed: amendment 15's gate row is capital-only. | **G1.** The continuous small-ship approach is cut by stop → 0x12 → re-issue. At module speeds b = v²/2a grows (143 m/s, a 20 → 511 m), so the warn fires earlier than in vanilla **[i]**. The order-4 leg ends at X, 1.7–2.2 r from the gate centre (lead-in geometry, spread ±0.25 r), so it never enters the 1 r sphere: suppressing on the validated lead-in is geometrically safe **[i]**. | R (the main remaining small-ship clunk) | fx H: a TS and an M4 on the gate lead-in with the engine warn model (`0x0044dd90` cone with B = gate). Assert no warn while in the window and the verdict outside it. row: `engine_warn_tally.py` |
| 22 | Small ship's heading at the order-5 start | Amendment 10 lead-in; carrot 0.75 s of flight. Run155/156/157: 49/38/38 of 49/38/38 transitions start order 5 more than 18.2° off B (median 55.5°/55.8°/48.4°) **[m]**. | Amendment 14 (slip-aware gate carrot `τ + t_10`). | Known and designed; not in source. | R | the ratified amendment-14 fixture thresholds |
| 23 | Ship with a zero angular-acceleration row (12 TShips rows) | `decide`: `rot_pct <= 0` → whole-ship pass-through (`no_angular_accel`). A cap of 0 on a single axis drops that axis in the core. | §6.2. | None for steering. As B under amendment 15, such a ship (if small, autopilot) is suppressed although it can neither dodge nor turn. | S (rare) | listing: which 12 rows and their codes (`tships_columns.py`) |
| 24 | Damaged ship (the hull ramp lowers max speed) | The getters are called on every decision (`0x00450980` includes the hull ramp). | Same. | none | — | — |
| 25 | Order changes in the middle of a detour | The site-B clamp needs `s.order == order` (`clamp_for`). GateState resets on a target change. The stuck witness resets on a new order or point. `ShipAvoid.sides` survives: the remembered side and `kCommitFactor` 2 C stay until the obstacle is behind or absent. | §3.4 hysteresis. | After an order change the old side can be held against a new goal for one obstacle **[i]**. The "behind/past P" tests keep it from steering away from the new point. | C | fx: the crossing scenario with an order switch at t_cpa/2 (side flips allowed once) |
| 26 | Save/load in the middle of an approach | Tables are keyed on the object pointer. Every age test is `now − stamp` in uint32, so a load to an earlier game time wraps to "stale". A later time ages normally. The snapshot epoch equals `now`. The part cache is keyed on pointer + root radius + position stamp. | "flushed on load" (§3.5). | No explicit flush, but every time test fails safe. A recycled pointer inherits only entries younger than 5 s (table) or 100/250 ms (E) **[i]**. | C | none needed; optional unit: wrap-around ages |
| 27 | Ship destroyed while traced | The trace slot is freed after 5 s game time idle (`kTraceIdleMs`) or at launch. Table and avoid slots are reused when stale (5 s) or when the object differs. | Same. | A pointer reused within 5 s continues the old slot's rows. | C | none |
| 28 | Player takes manual control during a gate approach | `decide` → `no_autopilot`. The site-B asm filter needs the autopilot bit, so there is no clamp and no speed write. P2/P4 keep `[obj+0x10]` if the earlier autopilot decisions validated the approach (`gate_stop_keeps_speed` has no autopilot test). | Manual flight untouched (§1.4). | P2/P4 skip the engine's zeroing of the manual player's speed, which vanilla would do. That is benign or better; there is no new risk. | C | none |
| 29 | Docking computer | Not traced. Autopilot docking = orders 4/5 under the autopilot bit, steered like AI. The instant dock is not an order **[i]**. | — | unknown | C | row: player docking with `--debug` |
| 30 | Script `SA_StopObject` (order 0 while the table still holds the old order) | The site-B clamp is off at once (order mismatch). Site E: the first branch can still suppress for ≤ 100 ms, the capital branch for ≤ 250 ms. | Same. | harmless: order 0 zeroes the desired speed | — | — |
| 31 | Formation leader destroyed | Installed: the exclusion follows the live `f+0x224`, so it changes at once. Amendment 15: the leader is missing from the snapshot, so the membership is cleared that frame. | Same. | none | — | fx D variant "despawn" (amendment 15) |

## Ranked list

**Fix before the next build** (inside the amendment-15 implementation; compares and constants only):

| Rank | Gap | Fix | Proof |
| --- | --- | --- | --- |
| 1 | G1: small ships stop for the gate on order 4 (29/39 traced 0x12 legs, 152 s **[m]**) | Gate row at E for a non-capital A: B == A's `f+0x224`, class 0x12, A on order 4 with a validated lead-in this call and inside the window (`gate_lead_in` returned true). Count it in `e_suppressed_gate`. | fx H; flight: `engine_warn_tally.py` shows no class-18 rows for small subjects in the window |
| 2 | G2: the capital branch flickers off on 0x12 under SETA **[i]** | Freshness `max(250, 200 + 2 · dt_frame)` ms, from the frame dt already read in `decide`. The same rule for `kResolvedAgeMs` (`max(100, 2 · dt_frame)`). | unit test of the predicate at 70/167/333 ms; fx J |
| 3 | G3: the gate row lapses 250 ms after P2 | Key the gate row on the gate state (`kept`, A's order ∈ {4, 0x1f, 0}, same gate, cleared at order 5), not on A's decision stamp. | fx E extended through a 10 s through-slot wait with the engine warn model; `warn_pre` rows in flight |
| 4 | G4: B on order 5 is suppressed although it does not avoid (installed and A15) | Main session's call: pass for B on order 5 (and 0x1e/0x20). Otherwise keep the user's rule and ship fx K plus the `B_order` tally. | fx K |
| 5 | Row 6: slot neighbour inside C | No code. Add fx I and record the time and minimum speed. If the dip exceeds the vanilla churn, a design follow-up is needed (see Unknown). | fx I |

**Later** (flight rows decide; fail closed today):

1. Amendment 14 (slip-aware), already ratified: the order-5 start error (row 22).
2. A 52 km base on the fallback body (row 19). Check the first `ship_avoid_station` row with `root_r_m > 20000`.
3. Snapshot overflow in dense asteroid sectors (row 18).
4. The M7 gate path (row 9).
5. TP against its own dock station (row 1): needs a steering-off baseline.
6. Stock records (row 8).
7. The capital's predictive layer keeping the manual player (row 12): cheap, cosmetic. One autopilot bit in the
   snapshot item, read by `small_for_capital`.
8. The servo surplus beyond 50 ms frames (row 15c).
9. The side memory across an order change (row 25).
10. Box or capsule hulls for capital-to-capital pairs (2026-10-13, after amendment 16). The lane corridor exemption
    trusts the KC grid (hulls 0.38–0.45 r wide overlap on paper by ≈ 260 m per Titan pair, `render-node-bounds.md` §6);
    a geometry-true pair test on the measured hull boxes would replace it and handle crossings. Trigger: `ship_capital_lane`
    rows showing grazes or neighbours bouncing at the grid.
11. Capitals jumping in together (exit spread covers non-capitals only); combat flight; save/load mid-approach;
    the "held below des" speed-law gap (replay 61eeb790).

## Verification

* Before the build: fixture cases H, I, J and K (above) in `ship_steering_host.cpp` beside amendment 15's D–G, and the
  freshness unit test. Acceptance is `test_ship_steering` with checks PASS, plus case I's time and minimum speed
  recorded in the ledger.
* In flight, on the next trace: `engine_warn_tally.py` (class-18 rows for small subjects → 0 inside the window) and
  `seta_freshness.py` (frame-step bands against the E counters). Add a tally of `ship_avoid_e_verdict` by
  `B_order` and by A's order (rows 2 and 4).

## Native Windows behaviour

Everything above is in-process x86 code on game-memory contracts the module already validates; nothing touches Wine,
D3D or windowing. Runs 151–157 were native.

## Alternatives considered

| Alternative | Why it loses |
| --- | --- |
| Ship amendment 15 as ratified and audit after the flight | G1 alone accounts for most of the traced small-ship 0x12 time in Run157, and G2 would partly undo amendment 15's own fix under SETA, which the user flies 69 % of the time. Both are one-compare corrections, so another flight cycle to rediscover them costs more. |
| Exclude the order's gate from the engine's warn for every ship on order 4 (no validated-lead-in condition) | It would also drop the warn for ships aiming at A from behind or beside the gate, where the engine's must-turn-away 0x12 is the only thing between the ship and the ring. |
| Fix the slot-neighbour dip now (row 6) by ignoring held grid points of the same gate | The real hull clearance at 666 m pitch and the vanilla cost of that churn are both unmeasured. The fixture case comes first. |

## Unknown, and what settles it

| Unknown | Settles it |
| --- | --- |
| Whether small ships on a **dock** order-5 leg react to their own warn (KC `m45` in `__LandInHangar`) | KC walk of `__LandInHangar` (`kc_gate_flow.py` style) |
| The vanilla rate of gate warns on small ships' order 4 (row 21) and of dock-station warns (row 1) | one steering-off flight of the same save with `ship_trace` on (the tally script runs as is) |
| Whether adjacent grid-slot hulls touch at 666 m pitch | mesh extents of the installed M1/M2/TL bodies across the slot axis (`scene_radius.py`), or fx I with hull boxes |
| Whether M7 derives from KC class 2019 | the class chain in `x3story.obj` |
| The 12 zero-angular-acceleration rows | `tships_columns.py` column `+0xd0` |
| Whether 0x21 builds `p+0x100 = 0` like order 8 | listing of `0x0045533b` |
| The collision damage law (`"MakeDamage"`), which scales every S row | KC walk of the handler (class 2004), already open in capital-gate-transit.md |
