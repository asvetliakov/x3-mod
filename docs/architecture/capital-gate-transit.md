# Capital gate transit: arrive aligned, keep the speed, leave small ships to the small ships

Design note, 2026-10-12, for ratification by the main session. Engine side only: the sites the module already
claims (A, B, E, P2/P4/P5, W) plus policy; the KC slot queue is a separate bytecode task (its interface is §5).
Sources: `verification/results/ship-steering/run156-triage/{rows_out,capital_timeline}.txt` (Run156 flight,
measured, **[m]**), [ship-navigation-ai.md](../reverse-engineering/ship-navigation-ai.md) §3, §10,
[ship-movement.md](../reverse-engineering/ship-movement.md) §3–4, §13,
[game-scale-and-ship-classes.md](../reverse-engineering/game-scale-and-ship-classes.md) §4–5, and amendments 7–11 of
[ship-steering-and-avoidance.md](ship-steering-and-avoidance.md). The phase model is
[`capital-gate-transit/budget.py`](../../verification/results/ship-steering/capital-gate-transit/budget.py)
([output](../../verification/results/ship-steering/capital-gate-transit/budget.txt)); its numbers are inferred
**[i]** unless a measured row is quoted next to them.

**Ratified 2026-10-12 (main session)** with two amendments: (1) the pair rule for a steered capital is the user's ratio (ignore ships with r_B < r_A/3) with the engine's "large" list no longer overriding it; site E suppresses the engine's warn for an ignored pair only when the small ship is under the module's steering that frame, exactly as §1 says, and the engine keeps every other pair. (2) The restored capital lead-in and the keep-speed-through-0x1f path ship behind the existing `ship_steering_gates` key with a capital-specific kill switch (`ship_steering_gates_capital`, default on), so a regression in flight can be switched off without losing the small-ship gate policy. The amendment-10 withdrawal rationale (222 m arrival) is corrected as this note states: the capital arrival sphere is 0.75 r_gate.

## Recommendation

Restore the amendment-8 capital lead-in along the gate axis (withdrawn by amendment 10 for a reason Run155/156
showed to be the formation bug, not the lead-in), make P2 keep a capital's speed when it crosses its arrival sphere
aligned, and let site E drop the engine's `CollisionWarn` for a steered capital against a small ship that is itself
under the module's steering. Concretely:

1. **Pair policy.** A steered capital avoids stations, asteroids, gates, every capital-coded ship (0x0043fd50) and
   any ship with `r_B >= r_A / 3`; it ignores every other ship (M6/M8/TM escorts included). For an ignored pair site
   E suppresses the engine's warn **only when B is under the module's steering this frame** (class 7, autopilot bit,
   order in the steer set, class not masked, not deleted/attached, not the player's manual ship); anything else
   stays with the engine. The capital's own predictive layer already skips these ships (amendment 11); the change is
   on the reactive side, which produced the 13 avoid legs of Run156.
2. **Arrive aligned** = option (b) + (d): the lead-in line through the grid point with direction F (the gate axis),
   a capital window of 6 r_gate, the module's speed in both directions (large cap 100 m/s, cornering term, emergency
   ramp), the crossing of the engine's own arrival sphere at full speed; P2 keeps `[obj+0x10]` for a capital whose
   crossing error is within 8.1° on a validated approach, so 0x1f ends on its first frame and order 5 starts at the
   crossing speed. Option (c) is moot: the arrival sphere a capital must hit is **0.75 r_gate = 829 m**, not 222 m
   (three of three Run156 stops at 823–828 m, §0).
3. **Rotate.** Nothing in the nominal path (0x1f is a one-frame no-op when aligned). For the misaligned fallback, the
   shaped remainder on order 0x1f is worth 15 %, and raising `f+0x2b0` from the KC's 0.5 to the engine's own 1.0
   during 0x1f another 30 %; both are optional and gated on a measured misalignment, not part of the first step.
4. **Spreads.** No exit offset for capitals (their radius covers the whole 0.3 r ring; their spread is temporal through
   the single through slot). Site W keeps excluding capitals.
5. **Queue interface.** The engine side assumes the 20-point grid geometry (validated, fails closed), one capital in
   the 6 r window per gate at a time, and a through-slot grant that arrives at or before the crossing. A
   clearance test instead of the 60 s cooldown changes no engine-side rule; it must still mean "the previous holder
   has launched (order 8 done)", not "is on its order-5 leg".
6. **Verification.** Three fixture scenarios (§6) with the crossing error, crossing speed, phase times and the
   site-E verdict per pair as assertions; in flight, `capital_timeline.py` with per-phase targets (one order-4
   segment per approach, no 0x12 segment, crossing error ≤ 8.1°, ≤ 2 rows of 0x1f, order-5 start speed ≥ 90 m/s).

Budget for the installed ships, from 3 r_gate before the crossing to the end of the launch (model, SETA 1): M2
**72 s** continuous (93 s with the stop kept), M1 **70 s** (122 s); at SETA 10, 49 s / 50 s. The measured 191 s / 134 s
of Run156 are not the same anchor (they start at the first entry into 3 r_gate of the gate *origin*, which happened
during the avoid churn, and include 29–45 s of rotate and 7–28 s of 0x12 legs). Per phase, §2.4.

## 0. What Run156 measured, and two corrections to the brief's premise

Three capitals (M2 leader `0x8c71c728`, M2 `0x8cb4e7c8`, M1 `0x8c7228a8`), gate `0x8cb546d8`, r_gate 1105 m,
grid depth 4392 m (M2) / 4253 m (M1), so r_ship = 1091 / 1022 m by the KC's depth rule **[m]**:

| Ship | order 4 | order 0x12 | 0x1f (angle, time, peak yaw) | order 5 | order 8 | stop distance from the grid point |
| --- | --- | --- | --- | --- | --- | --- |
| M2 leader | 356 s, 14 segments | 257 s, 13 segments, heading swings 15–167° | 98.8 → 8.2°, 29.2 s, 1062 binary/s | 13.6 s, 0 → 108 m/s | 0.8 s | 823 m |
| M2 second | 59 s, 4 | 110 s, 3 | 152.1 → 8.1°, 34.4 s, 1314 | 6.9 s | 0.9 s | 826 m |
| M1 | 102 s, 1 (v 1–21 m/s the whole leg) | 0 | 109.3 → 8.2°, 44.9 s, 1329 | 16.0 s | 0.9 s | 828 m |

* **The order-4 arrival sphere for a capital at its grid point is 0.75 r_gate (829 m), not clamp(r_own, 22200,
  111000) = 222 m.** All three stops sit at 823–828 m from the grid point (`dP` at the last order-4 row), which is
  the gate-target branch of the test (`0x00454b1a`, §10.3: the container is the gate for `FlyObjectBeforeContainerPos`
  too). The 222 m clamp is the ship-target / non-gate branch (`0x00454b48/5d`). This removes the brief's option (c) and
  the rationale amendment 10 gave for withdrawing the capital lead-in; Run154's 263 s orbit was "453 detours against
  its own wingman" (ledger, Run154 follow-up), fixed by amendment 11's formation exclusion.
* **The rotate is slow because the angular acceleration is 0.5 % of the cap per second**: the trace rows carry
  `rot=0x00008000` (f+0x2b0 = 0.5, the KC's `SetShipRotAccel` value; 0xb333 = 0.7 during the engine's 0x12),
  `rot_pct=10`, W = 1843 binary/s (10.1°/s, M2) / 1638 (9.0°/s, M1) **[m]**, so a = W × 0.5 × 0.10 = 0.51 / 0.45°/s².
  The controller model (brake when e < w²/a) gives 24.8 s for the leader's 90.6° (measured 29.2), 34.4 s for 152.1°
  (measured 34.4), 29.7 s for the M1's 109.3° (measured 44.9: two axes engaged, the model is single-axis) **[i]**.
* **The order-5 arrival law is confirmed with tf = the SETA factor 10**: (|v|/10 + r_ship)(1 + (tf − 1)/4) = 3581 /
  3568 / 3346 m against measured 3584 / 3569 / 3346 m **[m]**. At SETA 1 a capital's order-5 leg ends r_ship + 11 m
  from the origin; at SETA 10, 3.25× that.
* **The churn is the engine's reactive loop**, not the module's: every 0x12 segment follows one order-0 row (the
  `SA_StopObject` of `CollisionWarn`), has no module detour (`obst=0`), and ends with the KC re-issuing order 4 to the
  same grid point (`given==point` alternates because the KC re-reads `GetWarpInPos`). 381 of the capitals' 640
  predictive detours were against the five M6/M8 ships `0x359b8988`, `0x359d5020`, `0x359d2a58`, `0x359d6fd0`,
  `0x8c7213f0` **[m]**; which ships filled the engine's list at each 0x12 start is not in the trace (§8).
* Site E today suppresses only pairs the capital resolved with a flyable detour; a pair the capital *skips* is never
  resolved, so the engine's warn always runs for it. Amendment 11 therefore moved the detours off the capital but
  left the engine's loop intact: `avoid_skipped_small` 71,934 and 13 avoid legs in the same run.

## 1. Pair policy for a steered capital (question 1)

**Obstacle set of a capital** (the predictive layer; amendment 11 kept): stations, asteroids, gates, class-7 ships
whose record code is capital (TL, M0, M1, M2, TL_P, M7: pinned by the engine's filter, never ignored) and any other
ship with `r_B >= r_A / 3`. Everything else is skipped (`avoid_skipped_small`). For every installed M1/M2/TL
(r ≥ 745 m, so r/3 ≥ 248 m) that ignores every M6 (≤ 140 m), M8 (≤ 83 m), TM (≤ 128 m), TS/TP and fighter; an
M2 of 307 m keeps an M6 of 140 m. The ratio replaces the "large" predicate for non-capital-coded B; the engine's
predicate still decides on its own side (it keeps M6/M8/TM), which is why site E has to act.

**Site E rule, extended** (`x3m_ship_avoid_suppress(A, B)`; today: A in the table, A not on 0x12, B in A's fresh
resolved set). Add a second branch, evaluated only when A's record code is capital:

| Condition on B | Reason |
| --- | --- |
| class word 7, record code not capital, `r_B < r_A / 3` | the pair is "ignored" by the policy above |
| `[B+0x40] & 0x80000` (autopilot) | the integrator consumes the module's angles only under this bit; without it B cannot be steered by us (the player under manual control is `0x40000` without `0x80000` **[m]**) |
| B's order in the steer set (0x19, 0x23, 0x14, 0x15, 4, 5, 0x10/0x11/0x18, 0x22, 0x24, 0x1d, 6) | a ship on order 0 (stopped, waiting), 0x12 (the engine already owns its evasion), 0x1e/0x1f/0x20/8/0x21 (pass-through) is not dodging under our law |
| B's class not in `class_mask_`, `[B+0x40]` neither deleted (0x8000000) nor attached (0x200) (the module's `kFlagDeleted/kFlagAttached`), `[B+0xf8] == 0` (not docked) | the module never touches such ships |
| B is not A's escort/follow partner | the engine's filter (0x0044d385..) already drops partners before the warn test, so the pair never reaches E **[m]**; no check needed, the row is here for the reader |

All true → return 1 (suppress, counter `e_suppressed_small`); any false → 0, the engine's warn test runs and the
capital reacts as vanilla does. The check is per call and per frame: a small ship that stops or falls out of the steer
set is handed back to the engine on the next collision pass. Nothing is written; B's fields are read through the same
bounded reader the snapshot walk uses.

**When the small ship is not under our control.** Player's ship under manual control: the engine's warn stands on
the capital's side (today's behaviour: a capital warns on an M6 player, not on an M3 player, per the engine's own
"large" filter; unchanged). A ship under the engine's own 0x12: not suppressed, the capital also reacts, as today.
Docked/attached ships: excluded from the pair loop or the integrator by the engine (`[obj+0x44] & 0x20000`,
attached `0x200`, `[obj+0xf8]`); if a pair still reaches E, it is not suppressed. The capital's own escorts inside
or outside the formation envelope: never reach E (partner filter) and are excluded in every direction by the
predictive layer (amendment 11a). Wingmates of another leader crossing the capital's path: ordinary small ships,
covered by the rule.

**Ramming.** The engine's contact response is unchanged: the narrow phase and the impulse `slip += w (v_other −
v_own)`, `w = 2 m_other / (m_own + m_other)` ([sector-collide.md](../reverse-engineering/sector-collide.md) §13,
**[s]**), then the `"MakeDamage"` notification to the KC. The damage law is script side and **not decoded**: no note
in this repository quantifies collision damage; it is **[i]** that it scales with the relative speed and the masses,
as the impulse does. What bounds the risk: (1) the policy only extends what vanilla already does for fighters and
freighters (a capital ignores non-large ships natively) to M6/M8/TM; (2) a small ship under our steering carries both
the predictive detour and, when unresolved, the engine's own warn (never suppressed on the small side unless it
resolved the pair with a flyable detour); (3) the hand-back is immediate when the small ship stops being steered;
(4) the capital's emergency clamp stays for every obstacle it does keep. What settles the damage figure: a KC walk of
the `MakeDamage` handler in `x3story.obj` (class 2004) with the same symbolic-stack tool as `kc_gate_flow.py`.

**Fail-safe kept:** option off, class masked, A not in the table, A on 0x12, any field unreadable → 0.

## 2. Arrive aligned (question 2)

### 2.1 Geometry (the amendment-8 capital lead-in, re-instated)

`core::gate_geometry_capital` already exists and validates the given point against the KC grid: depth
−2 (r_gate + r_ship) within 10 %, pitch 666 m within 20 m, a ∈ [−2, 2], b ∈ [−3, 0]; direction h = F = gate row 2;
X = A − 0.75 r_gate F. With the corrected arrival sphere (0.75 r_gate, §0) X *is* the crossing point of the engine's
own test, as it is for small ships: the line enters the sphere head-on at X, 829 m before the grid point, and the
order ends there whatever the ship's speed. No spread (the grid is the spread). `f+0x240` is untouched; the carrot
goes through the existing ring-buffer redirect at site A.

**Window and look-ahead, capital values [i]** (the fixture decides the constants): the join window is
−6 r_gate ≤ s ≤ 0 along F from X (6.6 km; between 6 r and 5 r the carrot slides from A to the near carrot, as the
3 r / 2 r rule does for small ships), because a 90° turn of an installed capital takes 2 √(90 / a) ≈ 27–28 s of
controller time, 2.7–2.8 km of path at 100 m/s, before any convergence on the line; the carrot look-ahead is
max(0.75 s × v, 2 v / W, 0.3 r) ≈ 1.1–1.3 km, at least the turning radius at the cap (566 m M2, 637 m M1), so the
pursuit converges instead of weaving. Beyond the window the ship aims at A as the engine does.

### 2.2 Speed (site B, both directions, as amendment 8 had it)

`gate_speed()` for a large hull: 100 m/s (the engine's order-4 cap, `0x004550ed`), lowered by the cornering term
(chord rule to the carrot) and by the avoidance emergency ramp, written in both directions throughout the window.
The engine's alignment creep (1 m/s beyond 45.6° off A inside d_slow + 3 r + 2 r_ship ≈ 7.5 km) is the reason a
lateral join needs the write: under the lower-of rule the Run156 M1 flew its whole 102 s leg at 1–21 m/s **[m]**.
No arrival ramp (kShipFlagRamp off for the capital gate approach): the crossing is geometric, the stop (when it
happens) is P2's. Outside the window, or when the validation fails, the engine's law stands (lower-of), which is the
vanilla recovery: aim at A, creep, stop at the sphere, rotate.

### 2.3 P2: keep the speed when aligned (option d)

P2 today: `[obj+0x14] = 0` always; `[obj+0x10] = 0` for a capital (`gate_p2_capital`). New rule for a capital: keep
`[obj+0x10]` when the approach is validated **and** the last decision's heading error to F is ≤ 8.11°
(`gate_capital_aligned`, already computed at P2); otherwise the vanilla stop. What follows is safe in every branch
**[m] for the laws, [i] for the timing**:

* Order 0 under autopilot zeroes the desired speed, so the ship decelerates at a (10 m/s² M2, 5 m/s² M1) during the
  KC hand-over frames: ≤ 1 m/s per frame.
* 0x1f with the ship within 8.1°: ends on its first handler frame (`forward · target >= 0xfd70` is the first test;
  no speed is read or written). If the ship is 8–15° off because the join was short, it rotates while coasting to a
  stop within v²/2a = 500 m (M2) / 1 km (M1) — never past the grid point by more than that, 3.9 km in front of the gate.
* The through-slot wait (`TI_WaitRandomForInterrupt`, no order): the ship coasts to rest as above, exactly where the
  vanilla ship sits. Nothing in the KC loop reads the speed.
* Order 5 from slot 0 is dead ahead; from the outer slots the origin is up to 24.7° off (slot 19: lateral 2402 m at
  depth 5221 m), so the engine's misaligned branch (500 units/s) runs until within 18.2°: the ship brakes at a while
  turning the excess 6.5° (≈ 7 s), then re-accelerates; the cornering override of amendment 8(4) extended to capitals
  (B = the gate origin, chord rule: ω d / (2 sin θ) = 1.1 km/s at 25°, no limit) removes that penalty and is a
  follow-up, not part of this step.
* P4/P5 for capitals: keep `[obj+0x10]` at the order-5 arrival and launch with v0 (the same group); the gain is 90 m of
  continuity in the 0.9 s launch, the point is the visible non-stop.

### 2.4 Budget per phase (model, `budget.py`; measured anchors in §0)

From 3 r_gate (3315 m) before X to the end of the launch; "stop" = the engine ramp (vmax beyond d_slow, ramp to
10 m/s at A, 28 m/s at X) with the P2 stop and order 5 from rest; "continuous" = this note.

| Ship | SETA | approach stop / continuous | crossing speed | 0x1f | order 5 stop / continuous | launch | total stop / continuous |
| --- | --- | --- | --- | --- | --- | --- | --- |
| M2 (110 m/s, 10 m/s², 10.1°/s, r 1091 m) | 1 | 48.8 / 33.2 s | 28 / 100 m/s | 1 frame | 42.9 / 37.5 s (sphere 1102 m) | 0.93 s | 92.7 / **71.6 s** |
| M2 | 10 | 48.8 / 33.2 | 28 / 100 | 1 frame | 20.4 / 15.0 (3582 m) | 0.93 | 70.1 / **49.1** |
| M1 (113 m/s, 5 m/s², 9.0°/s, r 1022 m) | 1 | 73.6 / 33.2 | 21 / 100 | 1 frame | 47.1 / 36.0 (1032 m) | 0.93 | 121.7 / **70.1** |
| M1 | 10 | 73.6 / 33.2 | 21 / 100 | 1 frame | 26.6 / 15.4 (3357 m) | 0.93 | 101.1 / **49.5** |

Run156 measured, same ships: M2 leader 191 s from the first row within 3 r_gate of the *origin* to the warp (that row
is inside an 0x12 leg; the clean path never enters 3 r_gate of the origin before order 5), M2 second 134 s, M1 0.8 s
(it entered 3 r_gate only on order 8). The like-for-like comparison is the last order-4 segment + 0x1f + order 5 +
order 8: leader 33.9 + 29.2 + 13.6 + 0.8 = 77.5 s at SETA 10 against 49.1 s modelled, and that leader arrived 99° off
after 13 avoid legs; the M1 102.3 + 44.9 + 16.0 + 0.9 = 164 s against 49.5 s. The through-slot wait (0–60 s, script
side) is outside both.

**Options weighed.** (a) hand over to vanilla aim-at-A inside some radius: with the 829 m sphere there is no radius
at which the creep is needed, and the hand-over is a second kink; loses. (b) the lead-in to the point with a straight
axial last stretch: adopted (it is the amendment-8 geometry; the "low speed from dead ahead" part is unnecessary,
the sphere is hit at the cap). (c) patch `0x00454b5d` 111000 → larger: the constant is not on the capital's gate path
(§0), and it is the arrival radius of every ship-target order 4 (escort join, attack approach) for every class; loses
on both counts. (d) keep the speed through 0x1f: adopted on top of (b), gated on the measured alignment.

## 3. The rotate step (question 3)

**What the engine expects at the end of 0x1f [m]:** `forward · F >= 0xfd70` (cos 8.1°) on the forward row only
(sites.h (m)); with three arguments there is no roll target, so bank is free; no speed is read; the order ends with
result 1 on the first frame the test passes. Residual rates are consumed by the integrator through the remaining
angles (order 0 under autopilot does not damp them); the module's servo never overshoots, so nothing to add.

**Nominal path:** aligned at the crossing → one frame. The fixture's acceptance for the lead-in is therefore the
crossing error ≤ 8.1° from every start in the window, and `gate_capital_aligned` = the number of capital approaches
in flight.

**Fallback, when the crossing is 8–90° off** (a join cut short by a detour, or the vanilla path after a failed
validation). Two levers, both optional and only worth building if the flight rows show misaligned arrivals:

| Lever | 90.6° M2 | 152.1° M2 | 109.3° M1 | What it needs |
| --- | --- | --- | --- | --- |
| engine (today, pass-through) | 24.8 s model / 29.2 measured | 34.4 / 34.4 | 29.7 / 44.9 | — |
| steer 0x1f at site A with the shaped remainder (write max(e, w²/a + 1°) until the true stopping point w²/2a, so the controller's early brake never fires) | 21.2 | 29.1 | 25.2 | the point scale of `0x0045920e`'s `position + direction` (unit vector or scaled; one listing) and 0x1f in the steer set with roll 0 |
| raise `f+0x2b0` 0.5 → 1.0 for the duration of 0x1f (the value the engine's own 0x12 writes with a must-turn-away obstacle and leaves at exit), restore at the end | 17.5 | 24.3 | 21.0 | the listing of `SA_SetShipRotAccel` (the KC rewrites the field before every `__MovePos`, so a restore is belt and braces) and the 0x12 exit values at `0x00457b05/4e/84` **[m]** already |
| both | 15.0 | 21.1 | 18.0 | both |

The model is single-axis and under-reads the M1 by a third; the ratios, not the seconds, are the claim **[i]**.
Neither lever changes the 0xfd70 end test; patching it (`0x004592d9`) would make the ship enter order 5 further off
the axis and is not proposed.

## 4. Approach spread and exit spread (question 4)

* **Entry:** the grid is the capital's spread; the lead-in uses no ring offset (amendment 8 (3) as built).
* **Exit (site W):** today `gate_warp_in_offset` requires a non-capital record; a capital warps in on the axis. Keep
  it: an exit ring of 0.3 r_gate (331 m) is inside every capital's own radius (745–4583 m), so an offset separates
  nothing; the Terran ring hole (0.663 r = 2.1 km) bounds a lateral offset of a 1.1 km ship to ≈ 1 km, which still
  overlaps the axis path. A capital's exit is spread in time by the single through slot. The one real exit conflict,
  a small ship exiting 331 m off-axis in the same second as a capital, is the script queue's (the small ship takes no
  slot today); the engine side cannot see the other side's traffic at W.
* W's rule for a capital stays "no write", and the counter `gate_warp_skipped` keeps counting it.

## 5. Interface to the script-side queue (question 5)

What this design reads from the KC and assumes about its timing:

1. **The grid geometry** `G − 2 (r_gate + r_ship) F + 333000 (a R + b U)` units, 20 points: validated per approach,
   fails closed to vanilla (`gate_lead_fallback`). A patch that changes the pitch or the depth must change
   `kGateGridPitchM` / the depth rule in lockstep (one constant each) or every capital approach silently reverts.
2. **One capital in the 6 r window per gate at a time.** Two capitals in motion toward adjacent grid points (666 m
   apart, radii 1.1–1.3 km) are inside the engine's close-range warn (`d <= 1.2 (rA + rB)` ≈ 2.6–3.1 km for
   `rB >= 0.75 rA`), which no flyable detour resolves near the grid, so the engine's 0x12 returns for the pair as
   capital-vs-capital; the pair policy of §1 leaves it with the engine by design. The queue should hand out the next
   warp-in slot (not only the through slot) when the previous capital has crossed, or keep the holder of a warp-in
   slot more than 3 km behind the window until then; the `SA_Idle` wander (order 0x1d, steered by the module) is an
   acceptable hold if its point is kept out of the window.
3. **The through-slot grant at or before the crossing.** The KC requests it after 0x1f (loop head, `slot_type 1`),
   so with an aligned arrival the request comes one frame after the crossing; a grant then means order 5 starts at the
   crossing speed. A refusal costs nothing on the engine side (the ship coasts to rest where vanilla parks it) but is
   the remaining visible wait.
4. **Cooldown → clearance.** No engine-side rule changes. The clearance must be "the previous holder has launched"
   (its order 8 ran; this is where `ReleaseSlot` is called today, minus the 60 s). Granting on "the previous holder is
   on order 5" would put two capitals on the same leg: `m45 = 0xA0001` silences the follower's `CollisionWarn` on that
   leg **[m]**, the module's avoidance would then brake the follower behind the leader (unresolved inside C), which is
   acceptable but slower than waiting at the grid.
5. **Nothing here depends on `SA_Idle`'s wander, the 1–10 s polls or `m3`.** The design is indifferent to how the
   script waits; it only needs the waits to be short.

## 6. Verification (question 6)

**Fixture** (`ship_steering_fixture`, the gate scenario machinery of steps 3–4; the Boreas type already exists, add
the Run156 M2/M1 records: W 1843/1638, rot 0.5, rot_pct 10, vmax 55000/56500, acc 5000/2500 units, r 1091/1022 m):

| Scenario | Assertions |
| --- | --- |
| A. capital from 6 km off-axis (lateral 3 r, 6 r before X, heading toward the gate; slots 0 and 19; M2 and M1) | crossing error to F ≤ 8.1°; crossing speed ≥ 90 m/s; time from 3 r before X to the crossing ≤ 36 s; P2 keeps the speed (`gate_p2_capital_kept` 1, `gate_capital_aligned` 1); 0x1f ends on its first call; order-5 start speed ≥ 85 m/s; order-5 time within 20 % of `budget.py`; no row with `des` below the cornering value while on the line |
| B. A plus two M6 escorts of another leader crossing the capital's line at 2 km and 1 km ahead, one under autopilot on 0x19, one flagged non-autopilot | the capital's obstacle set skips both (`skipped_small` 2 per call); site-E verdict for (capital, M6-steered) = 1 every frame the M6 is in the steer set and 0 the frame it is switched to order 0; (capital, M6-manual) = 0 always; the steered M6's own detour against the capital resolves (its warn suppressed) and its miss ≥ C |
| C. A plus a second capital converging on the adjacent slot from the other side | the pair is never suppressed by the small-ship branch; the conflict is reported unresolved inside C (the known limit handed to the queue task, §5.2); no NaN, no write outside the window |

Plus the existing site verifier (byte strings of the P2/P4/P5/W sites unchanged, nothing new to claim: the whole
design uses the claimed sites) and the x87/SSE checks of the module's fixture.

**Flight rows that decide it** (`capital_timeline.py` on the next trace; targets per traced capital):

| Phase | Row | Target |
| --- | --- | --- |
| approach | order-4 segments per approach | 1 (no 0x12 segment, no order-0 row with the target cleared) |
| crossing | `hdg_err_deg` at the last order-4 row; `v` at that row | ≤ 8.1°; ≥ 90 m/s |
| stop | `gate_p2_capital_kept` / `gate_capital_aligned` in the summary | = the number of capital approaches |
| rotate | rows on 0x1f | ≤ 2 |
| order 5 | `v` at the first order-5 row; segment time | ≥ 85 m/s; within 20 % of `budget.py` for the SETA of the run |
| pairs | `e_suppressed_small`; 0x12 rows for traced capitals | > 0 when escorts were near; 0 |

One trace addition (cheap, `--debug` tier): at a capital's first 0x12 row, the engine's obstacle list (`f+0x1e0`
count and `f+0x1e4[0..4]`), so a remaining avoid leg names its pair.

**Cost.** Site E: for a capital A, one record-code read of B, one radius compare, two flag reads and one order read
through the bounded reader, on the warn-test path (per pair with a class-7 side inside the broad-phase box, ~10²
per frame); no allocation, no formatting, no lock. Site A: the capital lead-in is the small-ship lead-in's arithmetic
with other constants, already measured at 0.8 µs p50 per handled call (Run156 `handler_us_p50`). P2: one flag.
Nothing on the per-object site B beyond today's clamp.

## Hot-path and option-off behaviour

Option off (`ship_steering_gates` off, or a new `ship_steering_gates_capital` sub-key off): the capital path is
exactly amendment 10 (vanilla steps, diagnostics only); no byte differs. The sites are the already-claimed ones; no
new site, no immediate patch. Every branch fails closed to the engine's own behaviour (vanilla stop, vanilla warn).

## Native Windows behaviour

All of it is x86 code in the proxy DLL on documented game-memory contracts the module already validates; nothing
here touches Wine, D3D or the window. The game runs natively (C:\X3) since 2026-10-11 and the Run156 rows above are
native rows; the design adds no Windows-specific gap.

## Alternatives considered

| Alternative | Why it loses |
| --- | --- |
| Suppress the engine's warn for every capital-vs-small pair unconditionally (bit `0x8000000`, or E returning 1 on the class pair alone) | removes the fallback for small ships that cannot dodge (stopped, manual, engine-owned); the fail-safe rule in the brief |
| Patch the proximity filter `0x0044d2f0` so the engine's "large" set excludes M6/M8/TM for capitals | an immediate patch on a shared predicate used by the avoid scoring and the mesh test too; changes vanilla behaviour for ships we do not steer; site E does the same per pair with the control test |
| A lead-in with a low-speed straight final stretch (option b as worded) | the sphere is 829 m and hit at the cap; the slow stretch only adds 20–40 s |
| Patch the arrival clamp (option c) | not on the path (§0); global side effects on ship-target approaches |
| Hand over to the vanilla aim inside a radius (option a) | second kink; the creep stalls a lateral join (Run156 M1: 102 s at 1–21 m/s) |
| Steer 0x1f and raise `f+0x2b0` as part of this step | 15–40 % of a phase that the nominal path makes a one-frame no-op; build it only on evidence of misaligned arrivals |
| Replace the KC's grid with a module-chosen approach point (write `f+0x240`) | the order layer owns that field; the queue's grid is the capital spread and is the script task's to change |
| Exit offset for capitals at W | geometrically void (their radius exceeds the ring); the temporal spread is the slot |

## Unknown, and what settles it

| Unknown | Settles it |
| --- | --- |
| Which ships filled the engine's obstacle list at the 13 avoid starts (M6 escorts, the other M2, or both) | the one-row trace addition of §6, or re-reading the Run156 log is not enough (the list is not in the rows) |
| The collision damage law behind `"MakeDamage"` | KC walk of the handler in `x3story.obj` (class 2004), `kc_gate_flow.py` style |
| Whether a 6 r window and a 2 v / W look-ahead give ≤ 8.1° at the crossing from 3 r lateral for a 0.5°/s² ship | fixture scenario A; the constants are the knobs |
| The point scale in the 0x1f handler's `position + direction` and the first-frame end test's placement relative to the re-plan gate | capstone listing of `0x0045920e..0x004592f0` (≈ 230 B) |
| `SA_SetShipRotAccel`'s write and whether anything but the KC and the 0x12 exits write `f+0x2b0` | listing of the SA_ case (name table index of `SetShipRotAccel`) and a write-xref sweep of `+0x2b0` on the flight block |
| The order-0 under-autopilot speed zeroing is **[m]** for the idle handler; that the KC hand-over is ≤ 2 frames between 0x1f and the through-slot request is **[i]** | the next trace: rows between the last 0x1f row and the first order-5 row |
| The two-axis rotate time (the M1's 44.9 s against 29.7 s modelled) | not needed for the nominal path; `axis_controller_sim.py` with both axes if the fallback levers are built |

## Amendment 15 (Run157 corrections)

**Ratified 2026-10-12 (main session)**: the unconditional small-ship rule for capitals (any order; manual player ship, deleted/attached/docked and masked classes excepted), the 20 s formation memory keyed on the object id with same-frame drops, the amendment-13 join with the speed predictor and the slow-entry fallback, the gate-warn rule at site E, and the three diagnostic rows. Amendment: the escort-capital suppression (membership current, escort in 0x12/0, hulls not touching) is accepted; the open "escort already in the leader's list" case is left to the flight rows and must not be worked around by a new site.

Design note, 2026-10-12, for ratification by the main session. Run157 (unstripped DLL `ecd05046…`, source `af5ec4cf`,
native C:\X3, SETA mostly on) flew amendment 13 and made the traced M2 leader worse: 524 s in the engine's avoid order
0x12 in 20 legs against 257 s / 13 in Run156, `gate_capital_aligned` 0. Sources:
[`run157-triage/rows_out.txt`](../../verification/results/ship-steering/run157-triage/rows_out.txt) and
[`capital_timeline.txt`](../../verification/results/ship-steering/run157-triage/capital_timeline.txt) **[m]**; three
evidence scripts made for this amendment under
[`capital-gate-transit/`](../../verification/results/ship-steering/capital-gate-transit/):
`signed_lateral.py` → `signed_lateral.txt` (where the leader crossed the gate axis, from the Run157 log) **[m]**,
`a13_replay.py` → `a13_replay.txt` (the join model replayed against the measured lead-in leg) **[m]** for the check,
`join_budget.py` → `join_budget.txt`, `a13_feasibility.py` → `a13_feasibility.txt` and `predictor_dt.py` →
`predictor_dt.txt` (the 2-D join model: the controller `0x0043ad20`, the module's servo, the 2 s slip lag, the
hull's linear acceleration) **[i]**. Engine facts: [ship-navigation-ai.md](../reverse-engineering/ship-navigation-ai.md)
§3.1–3.3 and §10.1–10.4, [ship-movement.md](../reverse-engineering/ship-movement.md) §4 and §13.2. Sections 0–6 above
stay as written; this section supersedes them where it says so.

### 15.0 What each amendment-13 assumption got wrong

| Amendment-13 assumption | Run157 **[m]** | Correction |
| --- | --- | --- |
| §1: suppress the capital's warn for a small B only while B is under the module's steering (order in the steer set, fresh decision) | 15 of the leader's 20 warns named its own M6/M8 escorts (r 27–86 m, far below r_A/3 = 364 m); site E passed 17,932 small pairs for reason `order` and 68 for `stale`, suppressed 37,784 | The escorts that trigger the cascade are by construction not on a steered order when the warn fires: they are in their own 0x12 or in the order-0 hand-over. The control test excluded exactly the pairs that matter. §15.1 drops it. |
| §1: the capital's escorts never reach E, the engine's partner filter (`0x0044d385..`) drops them | the escort pairs did reach the warn | The partner filter keys on `f+0x224`, which 0x12 and order 0 clear (0x12 rows carry `tgt=0`). Partners are exempt only while they are on 0x14/0x15. |
| Amendment 11: the formation exclusion reads `f+0x224` | 210 leader detours, 195 of them against its escort capitals M1/M2 on 0x12 or order 0 (194 with leader 0) | The exclusion needs a memory of membership that survives the engine's transient orders (§15.2). |
| §1 fail-safe: never suppress while A is on 0x12 | 20 legs of 12.7–93.3 s; 8 of 20 legs end at 15.1–15.7 s | The 0x12 handler re-scores its list every 200 ms through the same warn test `0x0044dd90`, so the small escorts it recorded keep the leg alive. Site E now answers on that path too (§15.1). |
| §2.1: the lead-in geometry, carrot max(0.75 v, 2 v/W, 0.3 r) = 1132 m, assumes W | Measured join rate 2.3°/s against the 10.1°/s cap (frame 18240, `rate=418` binary/s). The brief read "crossed the axis at 32°" as a lead-in failure. | **The lead-in did not fail.** The leg 17778–18242 converged (e 1838 → 421 m, nose 71.1° → 31.9° toward the axis) and was cut by the engine's warn at 18243. The axis crossing (frame 18350, nose 33.9°, 110 m/s, s −3.55 r) happened inside 0x12 (`signed_lateral.txt`). The model replays that leg to within 2 % (e 422 vs 430 m, nose 31.7 vs 32.3°, rate 2.4 vs 2.3°/s). Continued without the 0x12, it reaches X with the nose 0.2° off and a 64 m excursion (`a13_replay.txt`). The pursuit's √(αθ) rate shaping is what makes the 1132 m carrot work at a 2.3°/s effective rate. |
| §2.2: 100 m/s written in both directions throughout the window | The escort M2 started order 4 only 0.70 r before X, 1645 m lateral, nose 91° off. It weaved for 41.6 s at 85–100 m/s and hit the sphere at 100 m/s, 85° off. P2 then stopped it instantly (`[obj+0x10] = 0`, `0x00454b91`). | Nothing checked whether a state could still converge. §15.3 adds a predictor and a fallback that enters the sphere slowly. |
| §2.1: outside the window, or past X (s > 0), the engine's law stands | The M1 started order 4 at s +2.05 r, 3.3 km lateral. It spent 103.8 s at 1–21 m/s in the engine's alignment creep, then rotated 92° in 24.5 s. | The fallback of §15.3 also covers s > 0. |
| §2.3, §5: an aligned approach is warn-free | One warn against the gate itself at d 6898 m on order 4 (frame 19947) | **New.** Inside `b + L + 2 (rA + rB)` of the gate a moving capital that heads at it warns: 6983 m for the M2 and 6874 m for the M1 at 100 m/s; X sits at 5221 / 5082 m **[i]** (§3.1 formula; `join_budget.txt`). So every aligned slot-0 approach spends its last ≈ 1.8 km, ≈ 17.7 s, inside the gate's warn (model). In phase 0 the KC turns that warn into 0x12, with the gate as a must-turn-away obstacle of weight 150. Run157 hit it only once because no approach got aligned. §15.1 adds a gate branch. |
| §6: capital transitions are logged | no `ship_gate_transition` row for a capital | §15.4. |

### 15.1 Pair policy, corrected (question 1)

Site E (`x3m_ship_avoid_suppress(A, B)`, the entry of the warn test `0x0044dd90`) evaluates the **capital branch**
when A is a class-7 ship whose record code is capital and A's last decision is at most `kSteeredFreshMs` (250 ms) old.
Since 0x12 is in the steer set, a capital on 0x12 stays fresh, and the branch now also runs while A is on 0x12, i.e.
on the 200 ms re-score. On that path only the small-ship row acts. The handler sends large and capital-type obstacles
straight to the mesh test `0x0044d9c0` (§3.3 **[m]**), past E. So an escort capital already in the leader's 0x12 list
leaves it only geometrically; the member row prevents the next warn. The first branch (B in A's resolved set) keeps "A not on 0x12". Any read failure returns 0.

| B | Verdict | Counter |
| --- | --- | --- |
| class 7, readable non-capital code, `r_B < r_A / 3`, any order (0x12, 0, 0x1e, 0x1f, …) | **suppress**, except in the three rows below | `e_suppressed_small` |
| … without `[B+0x40] & 0x80000` (the manually flown player ship, or any ship the integrator does not drive) | pass | `e_small_pass_no_autopilot` |
| … deleted (0x8000000), attached (0x200) or docked (`[B+0xf8]`) | pass | `e_small_pass_not_steerable` |
| … class in `ship_steering_classes`' mask | pass (the mask means "leave this class's pairs vanilla") | `e_small_pass_class` |
| not small, a remembered member of A's formation (§15.2, fresh, identity matching), currently in the engine's transient (order 0x12, or order 0 with `f+0x224 == 0`), and `d > r_A + r_B` | **suppress** | `e_suppressed_member` |
| A's order target (`f+0x224 == B`), class 0x12 (a gate), while A is on its validated capital approach in join mode (§15.3): order 4, or the 0x1f / order-0 hand-over after P2 kept the speed, until order 5 starts | **suppress** | `e_suppressed_gate` |
| anything else | pass (the engine's warn test runs) | — |

**Why unconditional for small B.** A small ship near a capital is in one of two states. Either the module steers it,
and it carries the predictive detour against the capital, with the engine's own warn on its side when unresolved. Or it
is already evading under the engine. Either way the capital reacting adds nothing but the cascade: escort evades →
the capital warns against the evading escort → the capital evades → the escorts follow. The user's rule ("a capital
ignores ships smaller than a third of its radius") is the policy. `e_small_pass_order` and `_stale` disappear.

**The escort cascade.** On the capital's side the small escorts fall under the row above whatever their order, and
the escort capitals under the member row. On the escort's side (escort A on 0x12 or 0, leader B) **E does not
suppress**. While A is on 0x12 the KC sends no `CollisionWarn` (L3 skips order 0x12, §3.2), so a suppression there
would only drop the leader from the escort's re-score. Keeping it means the follower gives way: with the leader no
longer reacting, the evasion is one-sided and the loop is open. Suppressing both sides would leave a pair of 1 km
hulls in which nobody avoids, while the escort is off its slot.

**Residual risk.** A small ship that neither dodges nor evades: parked under autopilot on order 0 (zero speed, so its
own warn test never fires), on a pass-through order (0x1e undock, 0x20 glide, 8/0x21 warp), or a class-masked ship.
A steered capital no longer reacts to an M6/M8/TM in that state and can ram it. Vanilla capitals already ignore every
non-"large" ship (fighters, TS, TP), so the new exposure is limited to M6 (≤ 140 m), M8 (≤ 83 m) and TM (≤ 128 m)
hulls that are stationary in a capital's path. The contact response is the elastic impulse of `0x0045e130`
([ship-movement.md](../reverse-engineering/ship-movement.md) §13.2). The small ship takes weight
`2 m_cap / (m_cap + m_small)` ≈ 2 of the relative velocity, so it is pushed aside **[i]**. The damage law behind
`"MakeDamage"` is script-side and not decoded **[i]**.

**Fail-safe.** Option off, A not fresh, A's code unreadable, B's code unreadable, any field unreadable: return 0, and
the engine's warn test runs as vanilla.

### 15.2 Formation membership memory (question 2)

**Store.** A fixed open-addressing table keyed by the follower pointer, 256 entries, no allocation. It is filled by
the sector snapshot walk, which already reads every ship's order (`f+0x258`) and partner (`f+0x224`) each snapshot.
Entry: `follower, leader, leader_id, follower_id, confirmed_ms` (game time). The ids are the engine's native object
id `[obj+0x08]`. The same field `chase_transition_identity_core.h` resolves through the native registry (`+0x14`
table) for `SA_GetEventObject`, so a freed pointer re-used by a new object carries a different id **[i]** (monotonic
assignment not listed).

**Update per snapshot, per ship F with entry E:**

1. F on 0x14/0x15 with `f+0x224 = L != 0`, L a class-7 ship in the snapshot: set or refresh `(L, id(L), id(F), now)`.
   A different L replaces the entry.
2. F on any other order with `f+0x224 != 0` (order 4 to a gate, an attack target, a dock): clear immediately.
3. F on 0x12, or order 0 with `f+0x224 == 0`: keep, no refresh.
4. Clear the same frame when:
   * L is absent from this sector's snapshot, or `[L+0x08] != leader_id`, or `[F+0x08] != follower_id`;
   * L is seen on 0x14/0x15 with `f+0x224 == F` (role reversal);
   * either ship lacks the autopilot bit or is the player's ship (the player has taken it over);
   * `now − confirmed_ms > 20 s` of game time.

**Expiry 20 s.** The engine's avoid leg ends only once the list is empty and the ship is more than `f+0x260` from
its start. The leader's legs cluster at 15.1–15.7 s (8 of 20 **[m]**), so the floor for these hulls is about 15.5 s
**[i]**. Any expiry under about 16 s would lapse in the middle of every escort 0x12 leg. 20 s is the top of the
allowed range: it covers the floor plus the order-0 hand-over and the KC's re-issue of 0x14. A longer leg (10 of 20
are above 20 s) loses the membership, and the pair returns to vanilla reactive behaviour.

**Uses.**

* `av::excluded`: `own_follower` and the wingmate test accept "o is a remembered member of s" (or "of s's
  leader") in addition to o on 0x14/0x15. The capital's obstacle filter therefore keeps escort capitals on 0x12/0
  excluded, which removes the 195 detours. The snapshot item carries the effective leader (the live `f+0x224` on
  0x14/0x15, else the remembered one) and a remembered bit.
* E's member row (§15.1).

**Fail-safe direction.** Membership alone never suppresses the engine's warn. The member row also needs the live
transient order on B and `d > r_A + r_B`. A stale or wrong membership costs at most 20 s of "the leader's predictive
layer ignores that escort", with the escort's own 0x12 and the engine's warn on the escort's side intact.

**Cost.** One hash probe and three compares per ship per snapshot (37 class-7 ships among 152 objects in Run157's census), plus
one 4-byte read of `[obj+0x08]` per ship **[i]**: well under a microsecond per snapshot. One probe per capital-capital
pair at E.

### 15.3 The capital join (question 3)

**Keep the amendment-13 pursuit; add a predictor that chooses the speed, and a fallback.** The replay above shows the
pursuit was not the failure. A carrot rebuilt from the effective rate is worse (alternatives, §15.7).

1. **Geometry (unchanged).** The carrot is on the gate axis through the validated grid point,
   `max(0.75 v, 2 v/W, 0.3 r)` ahead of the projection, never beyond A. The window is 6 r_gate before X, blended
   from A between 6 r and 5 r. Approaches into the window with the nose at A converge for any lateral offset up to
   8 km within 4.0 km (M2) / 4.4 km (M1) of X at 100 m/s (`a13_feasibility.txt` **[i]**). The window (6.6 km) covers
   that.
2. **Predictor (new).** Once per second of game time, for a capital in the window on a validated approach and in join
   mode, the predictor simulates the same pursuit in the plane of the lateral offset at each candidate speed
   {100, 60, 40, 25 m/s}. It starts from the last decision's state (s, e, nose angle and rate, speed and slip), uses
   the live W and α = W · `f+0x2b0` · rot_pct/100, and steps 0.25 s with the speed slewing at the hull's
   acceleration. The written speed is the highest candidate whose prediction reaches X with the nose ≤ 4°, the
   velocity ≤ 6°, |e| ≤ 50 m and no excursion beyond 50 m. That is a margin over the 8.11° acceptance, so a
   re-acceleration cannot spend it. Speed is lowered at once and raised only when the higher candidate passes. This is
   the cornering rule: it slows a capital only when the turn it still needs does not fit at the cap. The 0.25 s step
   gives the same verdict as 1/30 s in 56 of 56 cases (`predictor_dt.txt` **[i]**).
3. **Infeasible** = no candidate passes, or the first decision on the approach finds the ship past the crossing plane
   (s > 0). Fallback is then latched until the order or its given point changes: aim at A; write
   `min(100, √(25² + 2 a (d − 0.75 r)), ω_eff d / (2 sin θ))`, floor 10 m/s, the floor itself when the nose is 90° or
   more off A, with ω_eff = 2.3°/s (the measured join rate). This is written in both directions because the engine's
   alignment creep (1 m/s beyond 45.6°) is what stalled the M1. The ship enters the sphere at ≈ 25 m/s, the vanilla
   crossing speed (21–28 m/s), not at 100 m/s. P2 takes the vanilla stop and 0x1f rotates. In the model the fallback
   costs 4–5 s against arriving at 100 m/s and stopping instantly (S7). What it buys is that the instant
   100 → 0 m/s stop is gone.
4. **P2 keeps the speed** only when all hold at the crossing:
   * the approach is in join mode (not fallback);
   * the nose is within 8.11° of F (the engine's 0x1f test, `0xfd70`);
   * the velocity (nose plus slip) is within 10° of F;
   * `v ≤ v_bound = √(2 a (|X − G| − (r_ship + v/10)(1 + (tf − 1)/4)))`. The coast under order 0 must end before
     the order-5 arrival sphere. That gives 181 m/s (M2) and 132 m/s (M1) at SETA 10, 287 / 201 m/s at SETA 1, so
     the bound does not bind at the 100 m/s cap **[i]**.

   The order-4 arrival test itself fires at 100 m/s: the escort M2's leg ended at 830 → 825 m from P, within ≈ 5 m of
   the sphere **[m]**. The coast while the KC waits for the through slot is v²/2a = 500 m (M2) or 1 km (M1). That
   leaves the bodies ≥ 2.5 km (M2) and ≥ 1.9 km (M1) apart from the gate.
5. **The gate branch at E** (§15.1) covers the last 1.8 km of order 4 and the coast. Without it, every successful join
   would turn into an 0x12 away from the gate at 5–7 km.

**Budget** (model, `join_budget.txt` **[i]**; total = to X + order 5 + launch, SETA 1 / SETA 10; a fallback adds the
rotate and order 5 from rest):

| Start | M2 to X | M2 total | M1 to X | M1 total | Path |
| --- | --- | --- | --- | --- | --- |
| 6.0 km lateral, 5 r before X, nose at A (brief) | 90.5 s, nose 1.9°, 100 m/s, excursion 47 m | 128.9 / 106.4 s | 89.0 s, 1.3° | 125.9 / 105.3 s | join at 100 |
| 6.4 km lateral, 5.58 r, nose at A (fixture; Run157 frame 17272) | 100.3 s, 1.0° | 138.7 / 116.2 | 98.7 s, 0.9° | 135.6 / 115.0 | join at 100 |
| on axis, 5 r, aligned | 55.3 s | 93.7 / 71.2 | 55.3 s | 92.2 / 71.6 | join at 100 |
| 6.4 km, 5 r, nose 60° away (after an 0x12 leg) | 111.9 s, 2.3°, slows to 60 | 150.3 / 127.8 | 133.8 s, 3.3° | 170.8 / 150.2 | join, slowed (A13: 16.6° / 26.8° at 100, stop) |
| Run157 frame 18472: 627 m, 2.97 r, nose 68° away | 67.2 s, 1.0° | 105.7 / 83.2 | 68.9 s, 3.0° | 105.8 / 85.2 | join, slowed (A13: 31.6° / 37.8°) |
| Run157 escort M2: 1645 m, 0.70 r, nose 91° | 26.2 s to the sphere at 25 m/s, 17.8°, rotate 6.5 s | 76.5 / 54.0 | 29.1 s, 13.5°, 4.9 s | 82.1 / 61.6 | fallback |
| Run157 M1: s +2.05 r, 3.3 km, nose 135° | 52.8 s at 25 m/s, 81°, rotate 23.0 s | 119.6 / 97.1 | 59.1 s, 75°, 23.1 s | 130.3 / 109.8 | fallback (Run157 measured: 103.8 + 24.5 + 18.7 s) |

The model under-reads the escort M2's state: it predicts 19° at 100 m/s where the flight weaved to 85°. The fallback
rule is the safer side of that error.

The turn radius at the measured join rate is v/ω_eff: 2.5 km at 100 m/s and 1.5 km at 60 m/s. At the cap rate it is
566 m (M2) / 637 m (M1). The predictor's slow-down is what makes the radius fit, not a fixed schedule.

### 15.4 Diagnostics (question 4)

All rows are `--debug` tier, written to fixed rings in the handlers and formatted at the frame end.

* **`ship_gate_capital_join`**, per traced capital on a gate approach: at 1 Hz, on every mode change, and at the
  crossing.
  * Fields: `frame obj class slot mode=join|fallback|out reason=pred_none|past_x|grid_invalid|latched s_m e_m
    nose_deg vel_deg rate_dps v_ms v_cmd carrot_m pred_nose_deg pred_e_m`.
  * At the crossing, also `cross_nose_deg cross_vel_deg cross_v kept=0|1 why_not=mode|nose|vel|bound` and the
    seconds since the window entry.
  * This is the capital `ship_gate_transition` that Run157 lacked.
* **`ship_avoid_e_verdict`**, for trace slot 0 as A: one row per (A, B) pair per second of game time, deduplicated by
  a 16-entry last-logged table.
  * Fields: `frame A A_order B B_class B_code B_order B_leader B_flags (autopilot, deleted, attached bits) member_leader member_age_ms
    d_m r_B A_age_ms B_age_ms verdict=suppress_small|suppress_member|suppress_gate|suppress_resolved|pass_<reason>`.
  * Every pair with a "pass" verdict whose warn then fires is visible.
* **`ship_avoid_engine_warn_pre`**, for every traced ship, at the frame end. It is written when
  `[obj+0x40] & 0x3000 == 0x3000` and the order is not 0x12. That is the frame the KC's `CollisionWarn` ran, before
  `SA_StopObject` and 0x12; L0 of the next pass clears the bits (§3.2).
  * Fields: `order tgt count=f+0x1e0` and, for each of up to five entries, `other class code order leader member d_m
    (f+0x1f8[i]) r_m e_verdict` (the last verdict E gave for that pair this frame).
  * The existing first-0x12-frame row stays.
* **Counters** (summary): `e_suppressed_member`, `e_suppressed_gate`, `member_{set,refresh,replace,expire,
  leader_gone,id_mismatch,reversed,player}`, `avoid_excluded_member`, `gate_capital_join`, `gate_capital_slowed`,
  `gate_capital_fallback_{pred,past_x}`, `gate_capital_kept` (`gate_capital_stop_kept` renamed),
  `gate_capital_not_kept_{mode,nose,vel,bound}`.
* **Trace slots.** Trace the capital and its escorts (max_ships 4: the leader, two escorts, one escort capital), so
  the escort side of the cascade gets `warn_pre` rows too.

### 15.5 Verification (question 5)

**Host fixture** (`verification/probe/ship_steering_host.cpp`, `--capital`, the calibrated plant with slip; no Wine):

| Scenario | Assertions |
| --- | --- |
| D. Cascade: M2 leader on order 4 with two M6 escorts and one M1 escort on 0x14. At t1 one M6 is forced into 0x12 (target 0) inside the leader's warn cone; at t2 the M1 goes 0x12 → 0 → 0x14 across 30 s. Variants: the M6 docked; the M6 without the autopilot bit; the M1 switched to order 4 (gate); the M1 despawned and a new object placed at its pointer | Verdicts: (leader, M6 on 0x12 / order 0) = `suppress_small` on every call; docked and manual variants = pass. (leader, M1) = `suppress_member` while 0x12/0 and ≤ 20 s since the last 0x14, then pass. `excluded()` true for the M1 over the same span, so the leader's `ship_avoid_detour` rows against it = 0. Order-4 switch, despawn and id swap: membership cleared the same snapshot. The leader's `warn` flag 0x1000 is never set for an escort pair. (escort M1 on 0x12, leader) = pass. |
| E. Join from 6.4 km lateral at 5.58 r and at 5 r, nose at A; slots 0 and 19; M2 and M1 | nose at X ≤ 8.1° (model ≤ 1.9°); velocity ≤ 10°; \|e\| ≤ 100 m; excursion past the axis ≤ 100 m (model 47 m); `v_cmd` 100 throughout; crossing ≥ 90 m/s; P2 keeps (`gate_capital_kept` 1); 0x1f one frame; `e_suppressed_gate` > 0 and the gate pair's warn flag never set; time to X within 15 % of `join_budget.txt` |
| F. Late join: 627 m, 2.97 r, nose 68° away; and 6.4 km, 5 r, nose 60° away | at least one `v_cmd` = 60 row; converges with the same assertions as E; no instant stop |
| G. Infeasible: escort state (1645 m, 0.70 r, 91°) and past X (s +2.05 r) | `mode=fallback` latched, reason `pred_none` / `past_x`; sphere entry ≤ 30 m/s; P2 vanilla stop; rotate within 20 % of `rotate_time`; no write above 100 m/s |
| A/B/C of §6 | unchanged, re-run (B's `order` and `stale` pass reasons now read 0; its manual M6 still passes) |

**Flight targets** (next run with the capital and its escorts traced; `capital_timeline.py`, `signed_lateral.py`
and a new `e_verdict` tally):

| Phase | Target |
| --- | --- |
| cascade | 0x12 legs of the leader: 0. Any remaining leg must have a `warn_pre` row naming a non-small, non-member obstacle. `e_small_pass_order` absent. Leader detours against members: 0. |
| membership | `member_expire` ≪ `member_set`; `member_id_mismatch` 0 unless a ship died |
| join | one order-4 segment per approach; `ship_gate_capital_join` mode=join at the window entry; no axis crossing in order 4 (`signed_lateral.py`); at the crossing nose ≤ 8.1°, v ≥ 90 m/s, `kept=1` |
| gate | `e_suppressed_gate` > 0 per aligned approach; no `warn_pre` row naming the gate |
| hand-over | 0x1f ≤ 2 rows; order-5 start ≥ 85 m/s when the through slot is granted at once |
| fallback | every fallback row has entry ≤ 30 m/s and a rotate within 20 % of the model |
| small ships | detours against capitals are reported, not targeted: small ships dodging is the intended side. A predicted miss < C after the capitals stop evading should fall from 321. |

### 15.6 Cost, option-off, native Windows

* **Site E.** The small branch gets cheaper: B's table probe and order read are gone, and flags plus docked are read
  as before. The member branch is one table probe, for capital–capital pairs only. The gate branch is one compare
  against A's `f+0x224`, which is already read.
* **Snapshot.** See §15.2.
* **Predictor.** At most about 4k steps of a few tens of flops per evaluation: tens of microseconds **[i]**. It runs
  at the frame-end hook, not in the steering handler: one capital per frame, each at 1 Hz. Site A reads the chosen
  speed and mode from the ship entry, so the handler's p99 (Run157: 6.4 µs median, 29 µs max) does not change.
* **Option-off.** The join, the predictor, the fallback, P2's keep rule and the capital gate row sit behind
  `ship_steering_gates_capital`. Site E as a whole, so the small-ship row, the membership memory and the member rows, sit
  behind `ship_avoidance` (corrected at implementation: an earlier wording put the small-ship row behind the capital key);
  the small-ship gate row of the scenario audit (G1) behind `ship_steering_gates`. Off means amendment 13's or vanilla
  behaviour respectively.
* **Native Windows.** Everything is in-process x86 code on game-memory contracts the module already validates, and
  the build is SSE2. Nothing touches Wine, D3D or windowing. Run157 itself was native.

### 15.7 Alternatives considered (amendment 15)

| Alternative | Why it loses |
| --- | --- |
| The brief's carrot `max(2 v/ω_eff, k_join e)` with ω_eff = 2.3°/s (k 1.5) | A 5 km carrot inside a 6.6 km window aims at A, so the ship never converges. Model: 19–62° at X from every lateral start, against 0.3–3.3° for the kept pursuit with the predictor (`join_budget.txt`, ALT and A15 rows). |
| Bounded-intercept corridor from far out (carrot max(15 s v, 1.5 e), no window) | No gain from far: the M2 from Run157's frame-761 start takes 442.5 s against 431.8 s. It also fails from every window-entry start: 43° at X from 6 km at 5 r, because a bounded intercept needs about k·e of along distance (`join_budget.txt`, CORRIDOR rows). |
| Keep amendment 13's control test and add "B on 0x12 or 0" to the suppress set | It covers the measured cascade, but a parked small ship (order 0, zero speed) would still be the only case left with the engine. The user's rule is unconditional, and the residual risk is the same. |
| Suppress (escort, leader) on the escort side too | Nobody would avoid within a pair of 1 km hulls while the escort is off its slot (§15.1). |
| Patch the partner filter `0x0044d385..` to ignore the order | An immediate patch on a shared engine predicate: it changes vanilla behaviour for every formation, including ones the module does not steer, and needs a memory the engine does not have. |
| Lookup table for feasibility instead of the predictor | T(e, ψ) depends on v, α and the start rate jointly (`a13_feasibility.txt`: 600 m + v·T with T 9–50 s). A table per class is calibration debt; the predictor uses the live caps. |
| Fallback at 100 m/s (A13's arrival plus the engine's instant stop) | 4–5 s faster in the model, but it keeps the visible 100 → 0 m/s snap the design exists to remove. |

### 15.8 Unknown, and what settles it

| Unknown | Settles it |
| --- | --- |
| Whether `[obj+0x08]` ids are never reused within a session | listing of the id assignment at object creation (the registry insert reached from `SA_GetEventObject` `0x004607f4`) |
| What fires the escorts' own 0x12 legs (warns against the leader in the order-0 frames, against each other, against third ships) | `warn_pre` rows for traced escorts (§15.4) |
| Whether the 0x12 re-score's mesh-test branch for large obstacles really bypasses the `0x0044dd90` entry (so E cannot drop an escort capital already listed) | listing of the re-score loop in `0x00456722` (§3.3) |
| Whether the 0x12 re-score on A also adds new obstacles (the "only refreshes the list" path of `0x0044e450`) | listing of `0x0044e450`'s 0x12 branch; with §15.1 it no longer changes a verdict |
| The capital's slot spacing in formation vs `1.2 (rA + rB)` (do escort capitals warn on each other as wingmates?) | `warn_pre` rows; if they do, a wingmate row like the member row |
| The predictor against the engine's two-axis turn (the model is yaw only; Run156's M1 two-axis rotate took 44.9 s against 29.7 s modelled) | fixture scenarios E–G with both axes; flight `pred_nose_deg` against `cross_nose_deg` |
| The collision damage law (`"MakeDamage"`) | KC walk of the handler in `x3story.obj` (class 2004) |
| Whether `SA_StopObject` from a gate warn during the through-slot wait zeroes the speed | moot with the gate branch; the `warn_pre` row would show it |

## Amendment 16 (Run158: capital crowd at one gate)

Design note, 2026-10-12, for ratification by the main session. Run158 (unstripped DLL `011b348c…`, source `aee6d75b`,
native C:\X3) flew amendment 15. Gate 1 with three capitals worked: all three joined at 100 m/s with 0 s in 0x12. Gate 2
with five capitals (plus one untraced M1) stalled. Sources **[m]**:
[`run158-triage/`](../../verification/results/ship-steering/run158-triage/) (`rows_out.txt`, `capital_timeline.txt`,
`gate2.py`) and three scripts made for this amendment under
[`capital-crowd/`](../../verification/results/ship-steering/capital-crowd/):

* `latch_states.py` → `latch_states.txt`: when and from which state the join fallback latched.
* `lane_pairs.py` → `lane_pairs.txt`: the capital-vs-capital detour rows classified by the lane footprint of §16.1.
* `warn_subjects.py` → `warn_subjects.txt`: the 45 engine evasions by subject, other and verdict.

Engine facts: [ship-navigation-ai.md](../reverse-engineering/ship-navigation-ai.md) §3.1 (the warn test) and §10.2
(the grid), [render-node-bounds.md](../reverse-engineering/render-node-bounds.md) (per-axis extents of LOD 0). Sections
0–15 stay as written; this section supersedes them where it says so.

**Ratified 2026-10-12 (main session)** with these corrections. (1) The hull half-width comes from the LOD-0 record (`w_src=lod0`) in production; the inferred 0.25 r is not shipped as a constant, and any read failure or an unconfirmed chain means w = r (today's behaviour). A disassembly confirms the ship node → model → LOD-0 chain and a static read of the installed capital bodies records the real widths before the next flight. (2) The §16.4 small-ship extension of G1 (own target gate on order 4 with the path guard) is implemented with this amendment behind `ship_steering_gates`; the station analogue stays deferred to the docking design. (3) The through-slot model and scenario M as written; the calibrated plant with slip is the only plant. Everything else as recommended.

**Recommendation.**

* **Capital pairs: a lane rule.** Two capitals committed to the same gate are classified by the lateral footprint of
  their hulls in the plane across the gate axis.
  * (a) Footprints apart: different lanes. No detour between the two, and site E suppresses the engine's warn for the
    pair, with no condition on speed or along-axis spacing.
  * (b) Both in their lanes but the footprints overlap: one is behind the other. The trailing ship holds a following
    distance outside the engine's warn reach. The leading ship does nothing, and the engine's warn stays live.
  * (c) Anything else (a crossing): full avoidance, as today.
* **Join fallback: slow join instead of aiming at A.** When no speed converges within P2's acceptance but the slowest
  candidate still reaches the crossing plane, the pursuit continues at that speed ("slow join"). It is re-evaluated
  every second and goes back to join as soon as a candidate passes. The aim-at-A fallback stays only for states the
  pursuit cannot reach.
* **Capital gate row.** Extended to every validated capital approach on order 4 (join, slow join or fallback) and to
  the hand-over, under a path-clearance guard.
* **Latch diagnostics.** One `ship_gate_capital_latch` row per latch for every capital, traced or not.

**Ratification addendum 2026-10-13 (main session), after the width disassembly**
([render-node-bounds.md §6](../reverse-engineering/render-node-bounds.md)). The installed capitals are 0.38–0.45 r wide
(box, median; exact vertices 0.31–0.35 r), not 0.25 r, and a Titan pair sums to 926 m against the 666 m grid pitch. So
footprint disjointness alone never exempts adjacent lanes for the 1.1 km hulls, while the KC itself parks them there.
Corrections: (4) **Lane corridor exemption.** Two ships committed to the same gate with *different* lane points are
(a) exempt whenever each is inside its own corridor: settled (in lane as §16.1 defines it), or joining with a current
prediction whose e_max and excursion are ≤ 100 m and predicted nose ≤ 8.11° at the plane. The grid pitch is the engine's
own parking layout; its lateral overlap is vanilla's accepted risk, and the engine's warn (cone and closing speed) stays
the protection as E suppresses only the lane row for these pairs. (b) follow applies to the **same** lane point only.
Footprint disjointness at 50 m stays as a second route to (a) (two lanes apart, or small hulls). Everything else (c).
(5) **Width read.** The hull and every other part are child nodes of the ship's root `[obj+0x70]` (the root carries
`testbody`); per child, body id `+0x140` > 0, skip `+0x12c & 0x8000000`, model via the body cache hash (never through
`0x004863c0`), lateral half-extent = max over **all** LOD records of sqrt(e_x² + e_y²) × the node's scale, bounded by
|e| per axis for rotated parts, maxed with the part's lateral offset. Cache only w per ship (radius generation); the
model may be freed. Census row per type. The fail-safe w = r stays for any guard failure.

### 16.0 What Run158 shows

| Finding | Measured | Consequence |
| --- | --- | --- |
| Capital-vs-capital conflicts are our layer's, not the engine's | 1,700 detour rows among the gate-2 capitals (513 with miss < C, minimum miss 1,132 m). Only **1 of the 45** engine evasions is capital vs capital: an M2 against the M1 on order 4. One more involves a capital, a TS against an M2 on order 5 (`warn_subjects.txt`). | The fix is mainly in `excluded()`. E's lane row covers the same pairs so the engine does not take over once our detour is gone. |
| The grid cannot satisfy the sphere clearance | Lanes 666 m apart, r 755–1,092 m, so r_A + r_B = 1,510–2,184 m > 666 m. | No clearance margin can make adjacent lanes conflict-free while the hulls are modelled as spheres (§16.1). |
| The detours bent the joins | `0x3a2c2c30` flew 917 of its 1,503 order-4 frames under a detour (749 against `0x8f893d48`, 168 against the M1). Between frames 70694 and 70875 its nose swung from 1.7° to 46.4° while the predictor still forecast 0.1–2.5° at X. It latched at frame 70925 (s −984 m, e 1,023 m, nose 42.5°, 46.7 m/s). `0x8f88cbf0` had 397 order-4 detour rows against the slot-0 M1 from frame 61753; it was untraced until 64828, when it showed up already latched at e 2,762 m ≈ C (2,659 m). `0x8f893d48` had 240 detour rows against three capitals from frame 68994, also untraced until it showed up latched (`latch_states.txt`). | §16.2 |
| The fallback made the crossing worse, not the predictor's verdict | The slowest candidate (25 m/s) missed P2's acceptance only narrowly: 9.13°/29 m, 9.61°/41 m, and 7.88°/34 m (this one failed on velocity or excursion, which the row does not log). Aiming at A from 1.0–2.8 km lateral then crossed at 99.0°, 30.3° and 111.4°, followed by rotates of 25.1, 8.3 and 30.9 s. | Slow join (§16.2) |
| Five latches, three traced | `gate_capital_fallback_pred` stepped 0 → 5 in the summary intervals ending at frames 64694, 65985, 67477, 71070 and 71867, all in the gate-2 window. The rows at 64829 and 71912 are trace starts that came after the latch. | Latch row for every capital (§16.2) |
| The M1 pair was a crossing, and the M1 was on approach | Frame 66141: the warn named M1 `0x3a2b1b30` on **order 4 to the same gate** (`f+0x224` = the gate), d 2,167 m, r 1,022 m. `0xa4c06c10` was then 1,847 m off its lane, nose 77° off F, at 1 m/s (our emergency ramp). | Case (c); no new rule (§16.1) |
| The KC warp-in queue never waits at five capitals | 20 slots per gate; it only waits when all 20 are taken or stamped (§10.2). | A crowd of up to 20 capitals is normal vanilla traffic. §5 item 2 ("one capital in the window") is not something the KC provides. |

### 16.1 Capital pairs: the lane rule (question 1)

**Committed to the approach.** A capital S is committed to gate Γ when all of the following hold:

* S has a module table entry with a fresh decision (`fresh_window_ms`).
* Its `GateState` is S's own (object and id match) and holds validated capital grid geometry for Γ:
  `gate_geometry_capital` passed on the last order-4 decision.
* `f+0x224 == Γ`, and Γ is class 0x12.
* S's order is one of:
  * 4;
  * 0x1f or 0 within the P2 hand-over (`kHandoverMaxMs`);
  * 5.

Order 0 with `f+0x224 == 0` (the warn's `SA_StopObject`) and 0x12 are not committed. They are the engine's transient,
and today's rules apply to them. Both ships of a pair must be committed to the same Γ. Otherwise the pair keeps today's
rules (§15.1 member row, resolved set, sphere detours).

**Lane of each ship.** All coordinates are in the gate frame: G, R = row 0, U = row 1, F = row 2.

* Lateral position: q_S = ((x_S − G)·R, (x_S − G)·U).
* On orders 4/0x1f/0, the lane point is ℓ_S = 666 m · (a, b), the grid slot of its validated `given`. It is already
  computed as `A_local` / `grid_slot_of`.
* On order 5 the `given` is the gate origin, so the lane is the ship's current lateral position q_S, and its leg runs
  toward (0, 0).
* No validated grid point means the ship is not committed. There is no guessed lane.

**Hull reach across the axis.** For a hull of half-length ≤ r and lateral half-width w, with its nose θ off F, the
projection onto the R–U plane lies inside a disc of radius ρ(θ) = min(r, w + r sin θ) around the centre's projection.
This is exact for a box of half-dimensions (w, w, r) and conservative for any hull inside it. **No note gives a
capital's width.** The "hull m" column of [game-scale §4](../reverse-engineering/game-scale-and-ship-classes.md) is
the largest part's L∞ radius, i.e. the half-length, and equals r for every installed capital. The width is therefore:

* `w_src=lod0`: the LOD-0 record's per-axis half-extents (`+0x20/+0x24/+0x28`, scaled by the node's `+0x70` and
  `+0x80..+0x88`; [render-node-bounds.md](../reverse-engineering/render-node-bounds.md) §1). The lateral half-width is
  max(e_x, e_y), maxed with every child part's |offset_xy| + child radius from the existing part walk. The read path
  ship → node → model → LOD 0 must be confirmed first (§16.6).
* `w_src=inferred`: w = 0.25 r (a 1:4 beam-to-length hull). This is a stated assumption **[i]**, used until the LOD-0
  read validates.
* Any read failure: w = r. Adjacent lanes are then never exempt, which is today's behaviour.

**Footprint Φ_S** (2-D, in the R–U plane):

| State of S | Φ_S |
| --- | --- |
| **In lane**: committed on 4 (join mode) or the 0x1f/0 hand-over, \|q_S − ℓ_S\| ≤ 100 m, θ_S ≤ 8.11° | disc(ℓ_S, \|q_S − ℓ_S\| + ρ(θ_S) + 25 m). The 25 m covers one re-classification interval (≤ 250 ms) of lateral motion at ≤ 100 m/s × sin 8.11° **[i]** |
| **Joining**: order 4 in join or slow-join mode, with a prediction that reaches the crossing plane | The lateral segment from ℓ_S − over·n to ℓ_S + e_max·n, inflated by r_S. n is the unit lateral direction from ℓ_S to q_S. e_max and over are the predicted path's largest offset and its excursion past the line; the predictor already tracks the excursion, and e_max costs one compare per step. The attitude along the turn is not bounded, so the sphere is used. |
| **Fallback (aim at A), order 5, or no prediction yet** | The segment from q_S to its destination's lateral point (ℓ_S for A; (0, 0) for order 5), inflated by r_S |

**Verdict per pair (A, B), recomputed at every decision of either ship:**

* **(a) Exempt:** dist(Φ_A, Φ_B) ≥ 50 m. Why this is enough: if the projections of two bodies onto a plane are
  disjoint, the bodies are disjoint, whatever their positions along F. The footprints contain those projections for
  as long as the classification holds, and it is recomputed within 250 ms of any change. Effects:
  * `excluded()` drops the pair in both directions (no detour);
  * site E returns `suppress_lane` for (A, B) and (B, A);
  * there is no condition on speed or along-axis spacing.
* **(b) Follow:** both in lane and the footprints overlap. That is the same lane, or adjacent lanes whose hulls are too
  wide for 666 m.
  * Neither side detours sideways (`excluded()` true).
  * The trailing ship T (the smaller (x − G)·F) gets a speed cap at site B (lower-of with the join speed):
    v_T ≤ max(0, v_L·F) + √(2 a_T max(0, Δs − D)).
  * Δs = (x_L − x_T)·F. D is the engine's warn reach for the pair, b + clamp(r_T + r_L/10, 30 m, 1 km) + 2 (r_T + r_L)
    (§3.1, with b = v_close²/2a), plus 100 m. That is ≈ 5.3 km for an M2/M1 pair at matched speed **[i]**.
  * The leader does nothing. E does **not** suppress the pair, so the engine's warn remains the protection. Holding
    outside its reach is what keeps it from firing.
* **(c) Avoid:** anything else, for example one ship joining across the other's lane, or a fallback or order-5 leg
  whose segment crosses the other's footprint. Today's behaviour applies: sphere detours, the resolved-set branch at E,
  the emergency ramp, and the engine's warn.

**Adjacent lanes, in numbers** (the w_A + w_B that still gives (a) with a 50 m margin; arithmetic **[i]**):

| Pair (r m) | settled, e 20 m, θ 2° (measured settled joins: e 0–17 m, 0.3–1.4°) | e 50 m, θ 4° | at the in-lane limit, e 100 m, θ 8.11° |
| --- | --- | --- | --- |
| M2 1,092 + M2 1,092 | ≤ 500 m (w ≤ 0.23 r) | ≤ 364 m (0.17 r) | ≤ 108 m (0.05 r) |
| M2 1,092 + M1 1,022 | ≤ 502 m (0.24 r) | ≤ 369 m (0.17 r) | ≤ 118 m |
| M2 755 + M2 755 | ≤ 523 m (0.35 r) | ≤ 411 m (0.27 r) | ≤ 203 m |

So with the inferred w = 0.25 r, the 755 m M2s are exempt in adjacent lanes once settled. The 1.1 km pairs fall to (b)
in adjacent lanes and to (a) two lanes apart. The real widths decide how much of the crowd runs in parallel.

**Run158 replayed against the footprints** (`lane_pairs.txt`):

* Only 2 of the 1,700 detour rows have both sides traced. Both have the own ship off its lane, and the footprint gap
  is 1,237–1,442 m, so both would be (a).
* 1,165 rows have one side traced and the other side's lane known. With the traced side's footprint taken as its
  sphere-swept segment to its lane, and the other side assumed in its lane, **761 of 1,165 (65 %) would be (a)** at
  w = 0.25 r. At 0.15 r the figure is 776, at 0.35 r it is 497 **[m positions, i widths]**. This is an upper bound:
  364 of the 761 are the in-lane slot-0 M1 against `0x8f88cbf0` assumed in its lane, which it was not by frame 64829.
  The rest are real crossings and keep avoidance.
* 533 rows are against the untraced M1 or between two untraced ships and cannot be classified.

**Residual risk.**

* Case (a) is exact for box hulls inside (w, w, r). The exposure is a real hull wider than the w used. With `w_src=lod0`
  that is limited to geometry outside the LOD-0 box, which is none for the root body and bounded by the child spheres.
  With `w_src=inferred` it is unknown **[i]**.
* A contact between in-lane neighbours would be a graze at up to the along-axis speed difference (≤ 100 m/s). The
  contact impulse is elastic in the relative velocity
  ([ship-movement.md](../reverse-engineering/ship-movement.md) §13.2). The damage law behind `"MakeDamage"` is not
  decoded **[i]**.
* Vanilla parks capitals of the same size side by side at adjacent grid points for the through-slot wait. That relies
  on the same widths, but at zero relative speed.
* **Fail-safe:** any read failure, a stale state, an identity mismatch or an uncommitted ship drops the pair to (c).
  That is today's behaviour: our detour and the engine's warn, as in Run158.

**Capital on approach vs a capital not on approach** (Run158's 31 s 0x12). The premise was wrong: the M1 was committed
(order 4 to the same gate). `0xa4c06c10` was 1.85 km off its lane with the nose 77° off F, which is case (c). The
engine's warn at a 2,167 m centre distance between 1,092 m and 1,022 m spheres (a 53 m gap) is protection, not clunk.
**No new rule.** The lane rule removes the upstream cause (joiners pushed off-lane by in-lane neighbours). A capital not
committed (escort on 0x14/0x15, 0x12, order 0 without a target) keeps §15.1's member row and today's resolved-set rule.

### 16.2 The join fallback in a crowd (question 2)

**Why it latched.** The predictor simulates the pursuit alone, without obstacles. The detours did not bend the
*predicted* path. They bent the *actual* path, and so the state handed to the predictor each second:

* `0x3a2c2c30` was forecast to converge at every evaluation up to frame 70892, while its nose was swung to 46° by
  detours against its slot neighbour.
* `0x8f88cbf0` was pushed out to e ≈ C beside the slot-0 M1 **[i]**: untraced before 64828, but 397 detour rows.

The window geometry is the second factor, not the first. The latches came at s = −0.66 to −0.93 r_gate only because
the bent state had run out of room there. Both untraced latches were found displaced **upward**, not sideways:

* `0x8f88cbf0` at frame 64829: 54 m across, 2,692 m up from the slot-0 axis; its own lane is (666, 0).
* `0x8f893d48` at frame 71912: 3 m across, 1,288 m up; its own lane is also (666, 0).

That is the capital detour's vertical family (world up, `lateral_axis`) **[m positions, i cause]**. If they were in
lane when the detours began (unknown: untraced), the lane rule makes those pairs (a) or (b) and nothing pushes them
out. Once a joiner is off its lane, its sphere-swept footprint keeps it in (c) against an adjacent lane until it is
back in lane (§16.8: the tighter joiner reach).

**Slow join (replaces the latched aim-at-A as the first fallback).**

* At an evaluation where no candidate grades ≥ 1, the mode becomes `slow_join` instead of `fallback`, provided some
  candidate's prediction reaches the crossing plane (x ≥ 0) with excursion ≤ 100 m and predicted nose ≤ 20°.
* The written speed is the candidate with the smallest predicted nose error, normally 25 m/s.
* The carrot pursuit continues, P2 takes the vanilla stop (`why_not=mode`), and 0x1f only rotates the residual above
  8.11°.
* For Run158's three latches the residual was about 1–1.5° at 25 m/s: 9.13/9.61° predicted. That is ≈ 3–3.5 s of
  rotate at a = 0.5°/s² (t = 2√(Δθ/a), §0) against the measured 25.1/30.9 s **[i]**. A failure on velocity alone
  (7.88°) needs no rotate.
* The aim-at-A fallback stays for `past_x` and for states where no candidate reaches the plane.

**Re-evaluation and un-latch.**

* Ships in `slow_join` and in the aim-at-A fallback stay in the `JoinRegistry`. Today `fell_back` removes them; there
  are 8 slots, and the peak was 3 **[m]**.
* They are evaluated at 1 Hz like join ships.
* Any candidate at grade ≥ 1 returns the ship to `join` at that speed (counter `gate_capital_unlatch`). `past_x` is
  final, because the pursuit needs s < 0.
* Cost: as today, one capital per frame. `join_eval_us_max` was 46 µs **[m]** (bound 81 µs).

**Diagnostics.** `ship_gate_capital_latch`, `--debug` tier, written for **every** capital at each `pred_none` /
`slow_join` / `unlatch` transition, not only for traced ships. It carries:

* the predictor's input: s, e, ψ, ψ_u, ω, v, W, α, a_lin, r_gate;
* per candidate: grade, nose, vel, |e|, over, e_max, reached_plane;
* A's current detour obstacle (`ShipAvoid.obstacle`) and its lane verdicts against every committed neighbour.

Run158 lost the state of three of five latches to the trace window. The 7.88° case cannot be explained without the
velocity and excursion fields.

### 16.3 The gate row for any validated capital approach (question 3)

`site_e_verdict`'s capital gate branch becomes `suppress_gate` when all of the following hold:

* B == A's `f+0x224`, B is class 0x12, and `GateState.gate == B`.
* The state is A's own (object and id match) and holds validated capital geometry for this approach.
* `capital_policy` is on.
* Either:
  * A is on order 4 with a fresh decision, in join, `slow_join` or fallback mode, and inside the window extended past
    X (s ≥ −6 r_gate along F from X; mode `out` passes); or
  * A is on 0x1f/0 within the hand-over after P2, kept or not.
* **Path guard:** dist(G, segment[x_A, P_A]) ≥ r_gate + r_A + 0.25 r_gate. P_A is A's grid point, which lies
  2 (r_gate + r_A) in front of G. A ship heading to it from in front of the grid therefore keeps ≥ 1.4 km of sphere
  clearance from the gate. A ship that has been pushed between the grid and the gate (`0x8f893d48` reached
  dG 2,346 m) gets the row only when its path back to P still clears the gate.

Order 5 needs no row: the KC sets `m45 = 0xA0001` for the gate leg (§10.1).

Run158: the three capital-vs-gate evasions (all `0x8f893d48` in fallback, at d 5,160, 3,675 and 2,346 m; 82 s of
0x12) pass the guard. They become `suppress_gate` **[i]**.

### 16.4 The remaining evasions (question 4)

The brief's premise needs one correction: **41 of the 45 subjects are small ships** (M4/M6/TS/TP), and A is small in
all of those pairs. The capital branch never ran for them. The "other's order 0" in 25 entries is the order word of
17 gates, 6 stations and 2 ships. Capitals had 0 evasions against small ships, and every one of the 8,544
small-pair passes was `b_dock_leg` **[m]**. The hand-over frame is covered: with amendment 15 a small B on order 0 gets
`suppress_small` (there is no `order_steered` test any more).

| Group (`warn_subjects.txt`) | n | Rule |
| --- | --- | --- |
| Capital vs its own target gate, in fallback | 3 | §16.3 |
| Capital vs committed M1, crossing | 1 | (c): keep |
| Small ship vs its own target gate on order 4, outside the G1 lead window | 13 (one `pass_not_fresh`), at d 1,440–3,051 m | Extend G1 (`suppress_gate_small`) from "inside the lead-in window" to "order 4 to its own target gate, with dist(G, segment[x, A_small]) ≥ r_gate + r_B + 0.1 r_gate". A_small sits 1.28 r_gate from G, so paths from in front clear it; a ship behind or beside the ring keeps the engine's warn. This belongs to the small-ship gate policy (`ship_steering_gates`), not this amendment's capital key. |
| Small ship vs a gate that is not its target | 1 | keep |
| TP vs its own target station on order 4 (d 3.7–5.5 km inside a 4.6 km sphere) | 6 | Same own-target shape, against the station's part model: suppress only when the path to the order's point clears every part sphere, and pass on `part_fallback`. Docking design, deferred. |
| Small vs small (M4 vs M4 on 0x12 ×9, M4 vs TS on 0x15 ×3, M6 pairs ×7, M4 vs M4 on order 0 ×1) | 20 | keep: vanilla protection; not a capital-crowd item |
| TS vs M2 on order 5 | 1 | keep (G4: pairs with B on order 5 stay with the engine) |

### 16.5 Verification (question 5)

**Host fixture, scenario M (the brief's "H"; letters H–L are taken by the scenario audit).** It runs in
`ship_steering_host.cpp --capital` with the calibrated plant with slip, the engine warn model (`0x0044dd90`: cone and
close range) for every pair, a box hull (w, w, r) per ship for a contact check, and a through-slot model with the
engine's rules (one at a time, the same-direction exemption).

Setup:

* Five capitals (M2 1,092, M1 1,022, M2 755 ×2, M2 1,092) on slots 0, 1, 2, 3, 4 (a = 0, 1, −1, 2, −2), arriving
  within 60 s.
* Starts:
  * two on axis at 5 r;
  * one 2.7 km lateral on the far side of slot 1 (Run158's `0x8f88cbf0`);
  * one joining across a neighbour's lane;
  * one 1.4 km lateral at s −1.0 km (`0x8f893d48`).
* Variants: w = 0.15 r, 0.25 r, 0.45 r (the last forces (b) in adjacent lanes), and `w_src` unreadable (w = r).

| Assertion | Target |
| --- | --- |
| Capital-vs-capital `ship_avoid_detour` rows between (a) pairs | 0 |
| Engine warn flag 0x1000 set for an (a) pair | 0; E's verdict `suppress_lane` on every call |
| (b) pairs | no sideways detour; Δs ≥ D on every frame after the cap engages; no warn for the pair; the leader's speed untouched |
| (c) pairs | detours present; behaviour equal to amendment 15 |
| Contact (box hulls) | min hull distance > 0 in every variant; in w = r, verdicts equal to amendment 15 |
| Joins | every capital crosses in `join` (≤ 8.11°, kept) or `slow_join` (P2 stop, rotate ≤ 5 s); aim-at-A fallback 0 in the nominal variant |
| Un-latch | a variant where a neighbour leaves the joiner's path: at least one `slow_join → join` transition |
| Gate row | `suppress_gate` on join, `slow_join` and fallback approaches; no gate warn; the path guard passes a ship placed between the grid and the gate on an inward path |
| Launches | order 5 one at a time by the through-slot model; no capital on order 5 while the previous holder has not warped |
| Cost | lane verdict ≤ 2 µs per decision (host); E's capital-pair branch unchanged in order of magnitude |

Existing scenarios A–G, G1 and K are re-run.

**Flight targets** (the next run with a capital crowd at one gate). These need new rows:

* `ship_capital_lane`: pair, verdict a/b/c, gap_m, w_A, w_B, w_src, e, θ, Δs, v_cap; at 1 Hz per traced pair and on
  every verdict change.
* `ship_gate_capital_latch` (§16.2).
* Counters `lane_exempt`, `lane_follow`, `lane_cross`, `e_suppressed_lane`, `gate_capital_slow_join`,
  `gate_capital_unlatch`.

| Phase | Target (Run158 gate 2 in brackets) |
| --- | --- |
| capital-vs-capital detours | 0 rows between (a) pairs; the remaining ones carry verdict (c) (1,700 rows) |
| engine evasions with a capital subject | 0 against the target gate (3) and against (a) pairs; any against a (c) pair carries a `ship_capital_lane` row with verdict c |
| joins | crossings: aim-at-A fallbacks ≤ 1 per gate (3 of 5 traced; 5 latches in total); rotate after `slow_join` ≤ 5 s (25.1 / 8.3 / 30.9 s) |
| crowd | unresolved episodes in the gate window ≤ 2× the gate-1 rate per capital (93 vs 16); emergencies (621 vs 144) likewise |
| widths | `w_src=lod0` on every capital row once the read lands; the census of (class, r, w) logged once per type |

### 16.6 Cost, option-off, native Windows

* **Lane board.** A fixed table per gate (8 gates × 20 entries), filled from the `GateState`s at each capital
  decision: lane point, footprint segment and radius, in-lane bit, s, v·F, w. One pair verdict is one 2-D
  segment–segment distance, about 30 flops. `excluded()` looks the pair up only for a capital s and a capital-coded o
  (≤ 20 compares). E does one lookup per capital–capital warn-test call. The predictor adds one compare per step
  (e_max). Hot-path cost is well under 1 µs per capital decision **[i]**; the steering handler's p99 (6.4 µs median)
  should not move.
* **Width read.** Once per capital per lifetime (cached with the radius generation, like the station parts).
* **Option-off.** The lane rule, slow join, un-latch and the extended gate row sit behind
  `ship_steering_gates_capital`; off means amendment 15. The small-ship G1 extension of §16.4 sits behind
  `ship_steering_gates`.
* **Native Windows.** In-process x86 code on game-memory contracts the module already validates (the gate state, the
  table, the node walk), built with SSE2. No Wine, D3D or window dependence. Run158 itself was native.

### 16.7 Alternatives considered (amendment 16)

| Alternative | Why it loses |
| --- | --- |
| The brief's convoy guard: both on approach, parallel axes, along-axis separation, both speeds bounded | It requires matched speeds or flying abreast, which rules out the case that matters: a 100 m/s join passing a neighbour waiting at its point. It also proves nothing about a joiner. The lateral footprint gives the same proof without any along-axis condition. |
| Bounding-sphere clearance with a smaller margin for capital pairs | No margin works: r_A + r_B = 1.5–2.2 km is greater than the 666 m lane pitch, and the minimum measured miss was 1,132 m. Any sphere rule keeps detouring at the grid. |
| Treat other capitals like small ships (ignore them) | Removes protection for 1 km hulls crossing lanes, which is the real case (c), and the M1 pair at a 53 m sphere gap. |
| Un-latch the aim-at-A fallback only (the brief's option) | Aiming at A from 1–2.8 km lateral worsens the state every second: `0x8f88cbf0`'s nose went from 7° to 99°. Nothing ever converges again; it measures as the same 25–31 s rotate. |
| KC queue patch: one capital in the window per gate (§5 item 2) | It serialises a five-capital crowd into about 5 × 100 s **[i]**. It is a bytecode task with its own risk, and the measured stall is our layer's detours, not the engine's queue. It stays compatible if wanted later. |
| A fifth predictor candidate at 10–15 m/s | It would turn the 9.1–9.6° near misses into kept crossings, but at 10 m/s the 1024-step bound covers only 2.6 km and the stop is the same. Slow join with a 1–1.5° rotate costs ≈ 3 s **[i]** without a new constant. |

### 16.8 Unknown, and what settles it

| Unknown | Settles it |
| --- | --- |
| Capital hull half-widths (the whole (a)/(b) split in adjacent lanes depends on them) | Static: extend `scene_radius.py` to the per-axis LOD-0 extents of the installed capital bodies ([body-format-bob1.md](../reverse-engineering/body-format-bob1.md); no launch). Runtime: confirm the chain ship node `+0x1c` → model → `+0x0c`[0] → LOD record `+0x20..+0x28` by disassembly of `0x004c6c56..0x004c6d5d`, then one census row per type |
| Why the 7.88° candidate failed (velocity or excursion) | the `ship_gate_capital_latch` row (§16.2) |
| Which lanes `0x8f88cbf0` and `0x8f893d48` were in when they latched, and where the M1 was | the latch row and `ship_capital_lane`; trace slots 5–6 for a crowd run |
| Whether the joining footprint (sphere-swept segment) is too conservative to exempt joiners next to an in-lane neighbour | `ship_capital_lane` gap rows; if so, bound the joiner's reach by the predicted ψ along the path (e(t) − r sin ψ(t) − w cos ψ(t), one more min per step) |
| The collision damage law | KC walk of `"MakeDamage"` (class 2004), as in §15.8 |
| Whether vanilla busy gates show contacts between parked neighbours (evidence for the widths) | a contact counter on the narrow phase for capital pairs at the grid (diagnostic build) |

### 16.9 Implementation

Implemented 2026-10-13 on `453d12ed` (worktree, not committed, not installed); evidence and numbers in the ledger
[ship-steering.md](../verification/ship-steering.md), "Amendment 16". Where the code reads this section:

* **Lane core** `src/proxy/ship_capital_lane_core.h` (pure, shared by the DLL and the fixture): the board (8 gates × 20
  entries keyed on pointer and native id), commitment from live reads (E, the avoidance resolve over the snapshot's order,
  f+0x224 and id) or from the stored board (decide() never dereferences another ship), footprints, the verdict with the
  addendum's corridor route (different lane points, both settled in lane or joining with e_max, excursion ≤ 100 m and
  predicted nose ≤ 8.11°: (a); the same lane point, both settled: (b); 50 m footprint gap: (a)), the follow cap, the path
  guards. Lane points closer than 333 m are the same lane; an order-5 leg has no lane point (footprint route only).
* **Hand-over in the board:** P2 (kept or not) records the state at the crossing and the speed P2 leaves; on 0x1f / order 0
  the entry's along-axis state is extrapolated with the hull's deceleration (desired 0), no other ship is read.
* **D** includes the warn test's r_A·cos θ term at its bound r_T (the §16.1 sum omits it; without it the engine fires
  inside D): ≈ 6.4 km for an M2 trailing an M1 at matched speed. **The cap** is the lower of §16.1's
  `max(0, v_L) + √(2a_T(Δs − D))` and the stopping-distance form `√(v_L²·a_T/a_L + 2a_T(Δs − D))`: the sum alone let Δs
  fall 167 m below D when the leader braked at the trailer's own deceleration (fixture M-b), and inside D it only matched the
  leader. Floor 1 unit/s (0 is "no write" at site B).
* **Slow join** `core::join_evaluate16`: join mode decides exactly as `join_choose` (lazy, highest first); none accepted →
  slow_join at the smallest predicted nose among candidates that reach the plane with excursion ≤ 100 m and nose ≤ 20°,
  else the fallback. slow_join and the pred_none fallback stay registered and re-evaluate at 1 Hz; any grade ≥ 1 → join
  (the highest grade-2 speed, else the highest grade-1). A slow join past X keeps its pursuit (no past_x switch; it crosses
  unconverged by design). `e_max` costs one compare per step; the predictor's worst evaluation is unchanged (81.2–81.6 µs
  against 81.4 µs for amendment 15, host, same sweep).
* **Hull width** (addendum (5), [render-node-bounds.md §6.1](../reverse-engineering/render-node-bounds.md)):
  `walk::hull_width` over the root's descendants (depth 3, 64 nodes), the body-cache hash read-only (power-of-two size,
  64 chain steps), the max over all LOD records, the box bounded by |e| when any basis on the path is not the identity,
  w = max √((|o_x| + e_x)² + (|o_y| + e_y)²) ≤ r; any guard → w = r (`fail`). Cached per ship on its gate state with the
  radius bits and the root pointer; one `ship_capital_census` row per type.
* **E:** `suppress_lane` sits after the gate rows and before the freshness test (the hand-over has no decision); the capital
  gate row is §16.3's for every input (its amendment-15 inputs keep their meaning, the new ones default to amendment 15);
  the small-ship row is G1 *or* the §16.4 guard on a fresh validated order-4 decision.
* **Option-off:** with `ship_steering_gates_capital` off the board is never filled, the resolve is unbound, E's new inputs
  keep their defaults and the predictor does not run: E's verdict equals amendment 15's on 400,000 random inputs (fixture).
  The §16.4 extension follows `ship_steering_gates`.

* **Speed on a detour** (main-session fix, 2026-10-13): a capital in join or slow join on order 4 with a validated gate state
  keeps the join's site-B write (both ways) while the avoidance hands the leaf a detour point: min(the gate speed with the
  cornering term, the predictor's candidate or the slow-join speed, the (b) follow cap), lowered at site B to the emergency
  ramp (`clamp_for`). Before it, a detour took the lead-in away and the engine's order-4 law held the capital at its 1 m/s
  alignment creep (two capitals 2.9 km apart at 1.0 m/s for > 80 s in the fixture). P2's hand-over is unchanged; the aim-at-A
  fallback keeps its own write rule. Counter `gate_capital_detour_write` (decisions).

**What the fixture shows about the design** (scenario M, measured): the lane rule holds its invariants in every variant
(no detour against an (a) or (b) pair, `suppress_lane` on every E call for an (a) pair, one launch at a time). With the five
starts of §16.5 at the same time, the stall is gone (arrivals 83–167 s against 93–334 s) but the join targets are still not
met: a lateral joiner within the sphere clearance (r_A + r_B + 0.5 r_A ≈ 2.6 km) of an occupied lane stays (c) until it is
within 100 m of its lane, and its sphere detours, now flown at the join's speed, bend the joins of in-lane neighbours too.
Entered one approach at a time near the grid, every §16.5 target passes. The §16.8 row "joining footprint too conservative"
is the open design question.

**Ratified deviations** (main session, 2026-10-13):

1. D includes the warn test's r_A·cos θ term at its bound r_T.
2. The follow cap is the lower of §16.1's sum and the stopping-distance form.
3. The box-contact check exempts two ships at adjacent grid points (within r + 1.5 km of their A) and corridor pairs; every
   other pair is asserted.
4. Interpretations: `suppress_lane` for every (a) pair, corridor pairs included; "in w = r, verdicts equal amendment 15" no
   longer applies (the corridor route does not depend on w); a slow join past X keeps its pursuit; the hand-over is committed
   only with f+0x224 = the gate on 0x1f / order 0.

### 16.10 Joiner corridor (amendment 16b)

**Ratified 2026-10-13 (main session)** with one correction that also settles review finding F1 on §16.9: site E
suppresses the lane warn only under a geometric certificate (footprint-disjoint (a), or (s) by (i) lateral or (ii)
along-axis separation). A corridor-only (a) pair (addendum rule 4, no certificate) keeps the engine's warn live; our
avoidance still does not detour it. Everything else as recommended; `ship_steering_gates_capital_seq` defaults on with
the capital key.

Design note, 2026-10-13, for ratification by the main session. Question: which pair rule exempts a lateral joiner from a
neighbour whose lane it must cross when the two hulls cannot meet, and keeps (c) when they can. Inputs: §16.1 with the
addendum, §16.9, the ledger entry "Capital gate transit amendment 16" (27 simultaneous TARGET MISS rows and their causes) and
[`detour_write_ab.txt`](../../verification/results/ship-steering/capital-crowd/detour_write_ab.txt). Two scripts were made for
this section under [`capital-crowd/`](../../verification/results/ship-steering/capital-crowd/):

* `seq_geometry.py` → `seq_geometry.txt`: the grid arithmetic and the scenario-M starts **[arithmetic]**.
* `seq_cap_undershoot.py` → `seq_cap_undershoot.txt`: a kinematic sweep of the trailing cap under leader braking **[m, model]**.

**Recommendation.** This is the brief's option 3 (sequencing) in a form that can be proved. It replaces the swept-footprint
verdict only for the pairs that amendment 16 leaves at (c). For such a pair the verdict is decided at every classification by
an *instantaneous separating plane*, and the trailing ship's speed cap is what makes the plane along F appear and stay.

* **New verdict (s), "sequenced".** It applies to a pair that amendment 16 classifies (c) on different lane points (or on an
  order-5 leg), when at this classification either:
  * (i) the hulls' current lateral projections are apart by a margin that covers the motion until the next classification; or
  * (ii) their projections onto F are apart, with the trailer held behind.

  Effects are those of (a): no sideways detour, and E returns `suppress_lane`. If neither (i) nor (ii) holds, the verdict
  stays (c).
* **Sequencing cap.** In every such pair where a lateral mover's path comes within reach of the other ship, the trailing ship
  (smaller s) gets a site-B speed cap that holds its centre at least D_x = h_T + h_L + 100 m behind the leader. The cap uses
  h = √(r² + w²), the hull's largest half-extent along F, and is robust to the leader braking at any moment, to latency and
  to stale board state.
* **No prediction is involved.** The predictor, its bound and the corridor route of the addendum are unchanged. Option-off is
  amendment 16 as built.

#### 16.10.1 Why the joining footprint is not the thing to fix

* **The lateral footprint cannot exempt adjacent lanes.** The joining footprint is conservative, but that is not the core
  problem. A hull in its own lane overlaps the adjacent lane's hull in projection: the half-widths alone sum to
  0.45 (r_A + r_B) = 680–983 m at w = 0.45 r, against the 666 m pitch, before any tilt. Only the ratified corridor route
  exempts such a pair. A joiner between 100 m off its lane and lateral disjointness (≈ 450 m off on the far side at 20°
  **[i]**, more on the near side) therefore really can touch an abreast neighbour. Shrinking the footprint with the predicted
  ψ (§16.8) changes the verdict only for pairs two lanes apart.
* **What separates such a pair is distance along F.** Two hulls whose projections onto F are disjoint cannot meet, whatever
  their lateral positions. This is the same theorem as §16.1's, on the other axis.
* **The brief's premise about X needs a correction.** The joiner's crossing plane X_J does lie ahead of the grid. But a parked
  neighbour's grid point is only 0.75 r_gate + 2 (r_J − r_N) = 155–1,503 m beyond X_J along F, while the along-axis
  separation needed is h_J + h_N + 50 m = 1,706–2,445 m. A joiner therefore has to be inside its corridor 573–1,921 m before
  its own X to pass a parked adjacent neighbour without waiting (`seq_geometry.txt`, w = 0.45 r). Otherwise the cap holds it
  until that neighbour launches. This holds by geometry; it is not a defect of the rule.
* **The along-F extent is not r.** r is the half-length (§16.1), and a tilted cylinder of radius w and half-length r reaches
  r cos θ + w sin θ ≤ √(r² + w²) along F: 1.097 r at w = 0.45 r, and √2 r when w = r. The rule uses h = √(r² + w²) at every
  tilt, so it needs no attitude prediction.

#### 16.10.2 The rule

**Watch pair.** Both ships are committed to the same gate (§16.1), amendment 16's verdict is (c), and the pair is not on the
same lane point. Same-lane pairs keep §16.1: (b) when both are settled, otherwise (c). For a watch pair:

```
verdict16b(A, B):                                   // after pair_verdict(A, B) == cross, sequencing on
  if same lane point (neither on order 5): return cross
  if lat_now(A, B) >= kSeqLatGapM + mot(A) + mot(B): return seq            // route "lat"
  (T, L) = trailer and leader by s (along(): the hand-over extrapolated)
  if along_ok(T, L): return seq                                             // route "along"
  return cross

lat_now(A, B) = |q_A - q_B| - rho_A - rho_B          // the current projections; rho = reach(r, w, cos_t) of section 16.1
mot(S)        = tau_S * (v_perp_S + r_S * (|omega_S| + alpha_S * tau_S)),  tau_S = min(1 s, age_S + fresh_ms)
along_ok(T, L):
  L: cos_t >= 0.5 and vF >= 0                       // the leader moves forward over the next interval
  T on order 4: s_T* = s_T;  T in the hand-over: s_T* = s_T + vF_T^2 / (2 a_T);  T on order 5: false
  s_L - s_T* >= h_T + h_L + kSeqAlongM + tau_T * max(0, vF_T - vF_L)
```

* **Who trails.** The smaller s trails. When |Δs| < 1 m, the ship not in its corridor trails, then the higher pointer. The
  order is total, so trailing creates no cycle: the frontmost ship of any chain is never capped (the liveness argument).
* **Sequencing obligation.** A watch pair carries the cap when at least one ship is a lateral mover and the movers' lateral
  paths come within reach of each other.
  * A lateral mover is a ship on order 4 not in lane, or on order 5. Its path is the segment from q to its lane point (order
    5: to (0, 0)).
  * A settled ship (in lane or in the hand-over) is its point q with reach ρ(θ) + 25 m.
  * A mover's reach is its corridor reach ρ_c = 100 m + w + r sin 8.11°, the reach it will have once in its corridor.
  * The pair is obliged when dist(path_A, path_B) < reach_A + reach_B + kSeqLatGapM.

  So a joiner sliding into the lane beside a neighbour, or across its lane, is obliged. A joiner whose lane lies two lanes
  away is not: M2 s3 against Titan s0 clears by 1,332 − 546 − 516 = 270 m. Two settled ships cannot close laterally, so they
  are never obliged.

  This trigger is a liveness choice, not part of the proof. A pair whose (i) lapses without a cap falls to (c), which is
  today's behaviour. It uses only geometry (lane points and q), never speed, so the cap cannot switch itself off by slowing
  the ship.
* **Sequencing cap.** It is computed at the trailer's site B for every obliged pair in which the trailer is on order 4. It
  applies in (s) and in (c). It only ever lowers the speed (lower-of with the join, slow-join, fallback and detour writes,
  the (b) follow cap and the emergency ramp):

```
D_x   = h_T + h_L + kSeqCapExtraM
v_c   = (L in lane or in the hand-over, a_L > 0) ? max(0, vF_L - a_L * age_L) : 0   // the leader's along-F credit
room  = s_L - s_T - D_x - vF_T * (fresh_ms + age_L)                               // the latency
cap   = room <= 0 ? floor : min(v_c + sqrt(2 a_T room), sqrt(v_c^2 a_T / a_L + 2 a_T room))
if a_T > a_L and v_c > 0: v_rel = sqrt(2 (a_T - a_L) room); if v_rel * a_L <= v_c * (a_T - a_L): cap = min(cap, v_c + v_rel)
cap   = max(cap, 1 unit/s)
```

* **The three forms.** The sum form covers a leader holding its speed and the stopping form a leader braking to a stop. The
  relative form covers a leader braking at a_L while it is still faster than the trailer can follow, when a_T > a_L. Without
  it, the minimum of the gap falls between the two stopping points.
* **Leader credit.** A leader that is neither in lane nor in the hand-over gets v_c = 0: the trailer stops behind the
  leader's current s, as if the leader stopped dead. A joiner turning at up to W may lose v·F faster than a_lin, so it earns
  no stopping credit.
* **Fail-closed.** All of these drop the route and the pair stays (c): a non-finite field, a_T ≤ 0, an unknown v⊥ / ω / α on
  either entry, a leader with cos_t < 0.5 or vF < 0, or a trailer on order 5 for (ii).
* **One pass.** The pair verdicts and the caps are computed once per decision, in one pass over the gate's entries. That
  pass is shared by `excluded()` and the cap; today `lane_pair` and `follow_cap_for` each walk the board.

**The proof per instant.**

* **Route (i).** The projections onto the R–U plane are disjoint now, by kSeqLatGapM plus the most they can close before the
  next classification. The centre moves at v⊥, and the reach grows by at most r (ωτ + ½ατ²). The pair is reclassified within
  fresh_ms of either ship's decision or E call.
* **Route (ii).** The projections onto F are disjoint by kSeqAlongM plus the closing within τ. Under the cap the gap never
  falls more than 5.3 m below D_x **[m, model]**, which is 50 m above the certificate threshold, so a capped pair keeps (ii)
  once it has it.
* **What it proves.** Either route means the cylinders (w, r) cannot meet before the next classification. The engine's warn
  is then redundant, and E's `suppress_lane` is as justified as for (a).
* **When neither route holds.** The hulls *can* meet: the pair stays (c), with our detour and the engine's warn as today.

**Board.** Each `Entry` gains `h`, computed once per refresh as √(r² + w²) (with w = r when unread). It also gains `v_perp`
(|u − (u·F)F|), `omega` (|body rate|, the slower of yaw and pitch) and `alpha` (W k, `slower_axis_alpha`). All four come from
the deciding ship's own decision, with no new memory read: 16 B per entry, 2.5 KB for the board.

#### 16.10.3 Constants

| Constant | Value | Derivation |
| --- | --- | --- |
| h | √(r² + w²) | The largest projection onto F of a cylinder (radius w, half-length r) at any tilt. With w = r (unread or failed): √2 r. |
| kSeqLatGapM | 50 m | = kExemptGapM, the clearance between projections, kept separate from motion. |
| mot(S) | τ (v⊥ + r (\|ω\| + α τ)), τ ≤ 1 s | Lateral centre speed plus reach growth r·Δθ ≤ r(ωτ + ½ατ²). The slip lag changes v⊥ by about v·ω·τ ≈ 2 m in 0.5 s, which the 50 m absorbs. Example **[i]**: a joining M2 at 100 m/s, 30° and 2.3°/s plus an in-lane M1 at τ = 0.5 s gives 42 + 12 = 54 m, so the route needs a gap ≥ 104 m. |
| kSeqAlongM | 50 m | The certificate's slack over h_T + h_L. τ·(closing) is added explicitly. |
| kSeqCapExtraM | 100 m | D_x − certificate = 50 m ≥ 10 × the cap's worst kinematic undershoot. That undershoot is 5.3 m over a_T, a_L ∈ {2, 5, 10} m/s², τ ∈ {0.25, 0.5, 1} s, leader stops at any time (`seq_cap_undershoot.txt`, cap 16b). The as-built (b) cap in the same sweep undershoots by up to 142.9 m: it has no latency, staleness or relative-braking term, which is why it is not reused unchanged. |
| leader nose | cos_t ≥ 0.5 | Within τ ≤ 1 s at the capital's W ≈ 10°/s, the nose stays below 70°, so v·F ≥ 0 until the next classification **[i]**. |
| v_c credit | in lane or hand-over only | θ ≤ 8.11° keeps the loss of v·F from turning ≤ v (1 − cos 8.11°) ≈ 1 m/s. |
| ρ_c (obligation reach of a mover) | 100 m + w + r sin 8.11° | the reach of a ship at the corridor limit (in-lane bound of §16.1): 546 / 745 / 704 m for the M2 755 / Titan / Colossus at w = 0.45 r |
| tie | 1 m | below one frame of motion |
| floor | 1 unit/s | = kFollowFloorMs (0 = "no write" at site B) |

Values for the scenario-M hulls at w = 0.45 r (`seq_geometry.txt`): h = 1,197 / 1,121 / 828 m (Titan / Colossus / M2 755);
D_x = 1,756–2,495 m; at w = r, D_x = 2,235–3,189 m.

#### 16.10.4 Scenario M simultaneous, walked through (t = 0 from `seq_geometry.txt`; the dynamics **[i]**)

| Pair | Δs (m) | Route at t = 0 | Then |
| --- | --- | --- | --- |
| Titan s0 – Colossus s1 | 140 | corridor (a), unchanged | both stay in lane: nothing pushes them any more |
| Colossus s1 – M2 s3 (2.7 km beyond) | M2 trails by 18 | (s) lat: gap 1,900 m at θ 0, about 1,690 m at the start's 16° | Its cap is at the floor from frame 1 (room < 0): the M2 brakes (10 s) while the Colossus runs on. (ii) holds once Δs ≥ 1,999 m, ≈ 25–30 s. (i) lapses near 450 m off its lane, later than that because braking also slows its lateral convergence. |
| Titan s0 – M2 s2 (crossing slot 0) | M2 trails by 2,641 | (s) along (≥ 2,075) | capped behind the Titan until it is in its corridor; must be in it 1.9 km before its X if the Titan is parked |
| Colossus s1 – M2 s2 | M2 trails by 2,781 | (s) along (≥ 1,999) | as above |
| Titan s0 – Titan s4 (1.4 km out, 50°) | s0 trails by 4,525 | (s) lat (1,749 m at θ 0) or along (≥ 2,445) | s4 settles in lane, then the footprint gives (a) (gap 300 m) |
| the other five | any | (a) footprint or (s) lat (lateral gap ≥ 1,986 m at θ 0) | — |

No pair starts in (c). Obliged pairs (the trailer capped): M2 s3 behind the Colossus; M2 s2 behind the Titan s0, the
Colossus and the Titan s4 (the cap binds only once Δs nears D_x plus braking). Not obliged: Titan s0 / M2 s3 (path clearance
270 m) and Titan s0 / Titan s4 (1,332 − 745 − 516 = 71 m ≥ 50 m). The Titan s0 is in no detour, so the chain behind the
"rotate 16.6 s" and "box −1,095 m" misses cannot start. The cost is sequencing: the M2 s3 waits about 25 s behind the
Colossus. A joiner that is not in its corridor by the distance given in §16.10.1 (573–1,921 m before its X) waits behind
a parked neighbour until that neighbour launches.

#### 16.10.5 Verification (host fixture, scenario M)

**Simultaneous variant** (`kCrowd`, 3 fps × 3 seeds; w 0.45 r is nominal, w 0.25 r is also asserted, w = r is reported). In
the w = r variant the Titan s0 / M2 s2 pair needs 2,662 m and starts at 2,641 m, so (c) is expected there.

| Assertion | Target |
| --- | --- |
| Invariants (as now) plus (s) | 0 detours against (a), (b) and (s) pairs; `suppress_lane` on every E call for an (s) pair; no unsuppressed engine warn for an (s) pair |
| Certificate, recomputed from the fixture's true state (not the board) on every (s) frame | route lat: \|Δq\| > ρ_A(θ_true) + ρ_B(θ_true); route along: s_L − s_T > h_T(θ_T) + h_L(θ_L), with h(θ) = r cos θ + w sin θ. Violations: 0 |
| Cap invariant | for every obliged pair after (ii) first holds, while the trailer is on order 4: min(Δs − D_x) ≥ −10 m |
| (c) pair frames between committed capitals | 0 at w 0.45 r and 0.25 r (on a miss: the pair, frame, lat_now − margin, Δs − threshold) |
| Joins (the 27 TARGET MISS rows become assertions) | kept + slow_cross = 5; aim-at-A fallback crossings 0 and frames 0; rotate after a slow join ≤ 5 s; join error ≤ 8.11° |
| Contact | the existing box check (outside the adjacent-grid and corridor exemptions) min separation > 0 |
| Liveness | 5 arrive and 5 launch; no capital at the cap floor for > 60 s outside the hand-over; the per-ship floor seconds and the last arrival are reported (expected ≤ 300 s **[i]**) |

**New sub-scenarios:**

* **M-c ((c) kept when they can meet).** The M2 755 of slot 2 (lane −666) starts abreast (Δs 0) of the in-lane slot-0
  Titan, at q = −1,250 (584 m beyond its own lane) with the nose 30° toward it. Both fly at 100 m/s. The lateral gap is
  1,250 − 717 − 491 = 42 m, below 50 m plus the margin, and Δs = 0 is below 2,075 m. Assert:
  * verdict (c) on the first classification, with detours and the engine warn unsuppressed on the (c) frames;
  * the trailer's cap from frame 1;
  * no (s) frame without its certificate.
* **M-d (the leader brakes).**
  * Ratio a_T > a_L: the simultaneous M2 s3 (a 10) behind the Colossus (a 5). At the first frame after the cap binds with
    Δs − D_x < 100 m, the Colossus's desired speed is forced to 0.
  * Ratio a_T < a_L: a Colossus joiner starting 1 km beyond its lane and 300 m behind the slot-0 Titan (a 10), with the
    Titan forced to 0 at the same point.
  * Assert, on every frame: Δs − D_x ≥ −10 m and the along certificate.
* **Option-off.** With `ship_steering_gates_capital_seq` off, the whole fixture's stdout equals today's (timing removed, as
  `detour_write_ab.py` compares), including the 27 TARGET MISS rows. `pair_verdict` equals the as-built one on 400,000 random
  side pairs, and E equals amendment 16 on 400,000 random inputs.
* **Staggered and M-b.** Every assertion stays, with one restatement: "the adjacent 1.1 km pair (c) out of its corridors"
  becomes "(c) or (s), certificate held". M-b (same lane) is untouched by construction.
* **Cost (host).** E's capital-pair work mean ≤ 2 µs per call. A decision's lane pass (refresh, verdicts, caps, 19
  neighbours) is reported, expected ≤ 1 µs **[i]** against 0.345 µs today. `pred_cost16` is unchanged because the predictor
  is not modified.

#### 16.10.6 Flight rows (the next crowd run; `--debug`, `ship_trace_max` ≥ 6)

* **Install row.** `ship_steering_gates_capital … seq=1 seq_lat_gap_m=50 seq_along_m=50 seq_cap_extra_m=100`.
* **`ship_capital_lane` gains** `route=foot|corr|lat|along|-`, `watch=`, `lat_gap=`, `mot=`, `ds=`, `h_T=`, `h_L=`, `Dx=`,
  `trail=`, `seq_cap=` and `bind=`, at 1 Hz per traced pair and on every verdict or route change.
* **`ship_gate_capital_latch` gains** `seq_cap=` and `seq_leader=`. They show whether a latch happened under a binding cap
  (the predictor grades speeds that a capped ship does not fly).
* **`ship_capital_census` gains** `l_m=`: the max over parts of |o_z| + e_z, scaled. This settles h ≥ the real along-axis
  half-extent.
* **New counters:** `lane_seq_lat`, `lane_seq_along`, `lane_seq_cross` (watch pairs left at (c)), `lane_seq_capped`,
  `lane_seq_floor_ms`, and `lane_seq_lapse` (an along-route pair that turned (c) while the trailer's cap was applied: the
  invariant's flight check).

| Assumption or target | Row that settles it | Target |
| --- | --- | --- |
| Capital-vs-capital detours | `ship_avoid_detour` against a pair whose last lane row is a or s | 0 |
| The cap invariant in the real plant | `lane_seq_lapse` | 0 |
| A leader's along-F deceleration ≤ a_lin (the stopping credit) | consecutive `ship_capital_lane` rows of capped pairs: −ΔvF_L/Δt of in-lane or hand-over leaders | ≤ a_lin + 0.5 m/s² |
| h covers the hull | `ship_capital_census l_m` against √(r² + w²) | l_m ≤ h for every type; otherwise h takes max(h, l_m) |
| Sequencing cost | `lane_seq_floor_ms` per capital, and the rows with `bind=1` | no capital at the floor for > 60 s outside the hand-over |
| Joins and the gate | as in §16.5 and the amendment-16 ledger | aim-at-A ≤ 1 per gate; rotate after a slow join ≤ 5 s |

#### 16.10.7 Cost, option-off, native Windows

* **Hot path.** The predictor is not touched, so its 81.6 µs worst holds as measured. Per watch pair the verdict adds three
  `sqrtss` (two reaches and one distance) and about 50 flops: about 30 ns **[i]**. The obligation test (one segment–segment
  distance, as §16.1 already has) and the cap add up to seven more `sqrtss`. Only watch pairs pay; pairs settled by the
  footprint or the corridor return as today. With the single pass of §16.10.2, a decision's lane work stays ≤ 1 µs **[i]**
  against the 2 µs bound, and E adds one route check per watch-pair call.
* **Option-off.** `ship_steering_gates_capital_seq` (it requires `ship_steering_gates_capital`). Off means no (s), no
  sequencing cap, no new fields written and the new board fields unused: amendment 16 as built.
* **Native Windows.** Portable header code on the existing board, in SSE2 scalar float with `sqrt_ss`. The float-returning
  helpers are `always_inline` (the `check_no_x87` finding of the amendment-16 ledger). No new hook or game read: v⊥, ω and α
  come from the deciding ship's own decision inputs. Nothing depends on Wine.

#### 16.10.8 Alternatives considered (amendment 16b)

| Alternative | Why it loses |
| --- | --- |
| (1) Time-resolved crossing: exempt when \|s_cross − s_N(t)\| > r_A + r_B + margin along the joiner's predicted path | Its soundness rests on the neighbour's future speed, which our own P2 (keep or stop), the follow caps and the engine's stops change after the verdict. The only physical bound (N brakes now) credits N 0.5–1 km of advance at a = 10 / 5 m/s², which exempts none of scenario M's crossings: the M2 s2 advances about 4 km before its corridor **[i]**. It does nothing about the abreast pair (Colossus / M2 s3, Δs 18 m), so that pair stays (c) and the cascade stays. It uses r where the along-F extent is up to 1.1 r. It adds per-neighbour work to each of 4 × 1,024 predictor steps, about +40 %, so ≈ 115 µs **[i]** over the 81 µs bound. And it ties a per-decision verdict to a prediction up to 1 s old. |
| (2) Swept box (w + r sin ψ(t)) per predictor step against the ≤ 4 lanes in reach | A tighter lateral reach does not help, because the end state in an adjacent lane overlaps anyway (half-widths 680–983 m against the 666 m pitch). The joiner stays (c) until its corridor unless the along-F dimension is added, and then it inherits (1)'s dependence on N's future. An SAT test per step per lane also costs more than (1). |
| (3) as stated: the joiner always yields speed | When the joiner is ahead (Titan s4 is 4.5 km ahead of Titan s0), yielding means falling 7 km back. Two joiners can each yield to the other (no total order). And without a certificate the pair is neither exempt from E nor provable while the yield is under way. "The trailer yields, and the exemption holds only under a certificate" is the provable form of (3) and is the recommendation. |
| Corridor-equivalent intrusion: exempt a joiner whose reach toward N does not exceed what a corridor ship's may (100 m + w + r sin 8.11°) | It would remove the M2 s3's wait, because a far-side joiner's reach toward the Colossus stays below the corridor's. But it is not a certificate: the hulls can touch, and it extends vanilla's parked-grid risk to moving pairs at join speed, which the brief excludes. It can be ratified separately if the measured sequencing waits are too long. |
| Reuse the as-built (b) cap with D = D_x | It undershoots D by up to 142.9 m under leader braking with τ = 0.25–1 s (`seq_cap_undershoot.txt`, cap a16), which is more than the 50 m slack. The 16b cap undershoots by at most 5.3 m. (b) itself keeps its cap: its D is the warn reach, and E is not suppressed there. |

#### 16.10.9 Unknown, and what settles it

| Unknown | Settles it |
| --- | --- |
| Whether every installed capital's hull fits along F inside √(r² + w²). This needs each part's \|o_z\| + e_z ≤ that bound; r is the largest part's L∞ radius, not a bound over offset parts. | Static: extend `capital_widths.py` (render-node-bounds.md §6) with l = max over parts of (\|o_z\| + e_z) × scale over the 131 hulls; no launch. In flight, `l_m` on the census row. |
| A leader's along-F deceleration bound (a_lin, the engine's speed slew) under engine orders and contact impulses | the leader rows of capped pairs (§16.10.6); [ship-movement.md](../reverse-engineering/ship-movement.md) §13.2 for the impulse |
| Whether a joiner in a real crowd reaches its corridor before the parked neighbour's D_x line (573–1,921 m before its X), or waits for that neighbour's launch, and whether the 120 s hand-over expiry then drops the pair to today's rules | fixture: the per-ship floor seconds and the expiry count; flight: `lane_seq_floor_ms` and `ship_capital_lane` rows with `bind=1` |
| Whether the predictor latches more while a seq cap binds (it grades speeds the capped ship does not fly) | the `seq_cap` field of `ship_gate_capital_latch`; the fixture's latch count under a binding cap |
| The fixture's contact box (w, w, r) is √2 wider in its corners than the production cylinder (w is a radius, §16.9). The existing box check is therefore more conservative than the rule's hull model; the certificate assertion uses the cylinder. | none needed for flight; a cylinder distance in the fixture would align the two if a box-only miss appears |

### 16.11 Implementation of 16b

**Ratified 2026-10-13 (main session), as built:** the six fixture-driven changes listed below (lateral reach capped at
h, hand-over drift and the ≥ 8.11° nose assumption, the wider obligation with the 100 m allowance and settled–settled
obligations, the 1 m/s backward-drift leader, half-length max(r, l), the cap on every write path); the 60 s liveness
row is reported, not asserted (67.0 s worst floor span in the simultaneous variant, a wait chain behind a parked
neighbour; the flight row `lane_seq_floor_ms` decides whether it needs a rule); the box-contact exemption extends to the
launch leg from an adjacent grid point (vanilla's own layout). Flight expectation accepted: the engine's warn may fire
between neighbours parked side by side, as in vanilla (Run158: 1 capital–capital evasion in 45).

Implemented 2026-10-13 on `2952a324` (worktree, not committed, not installed) together with the review fixes F1–F7, F10, F12 on
§16.9; evidence and numbers in the ledger [ship-steering.md](../verification/ship-steering.md), "Amendment 16b and the review fixes".

**Where the code reads §16.10.**

* `ship_capital_lane_core.h`: `Verdict::seq`, `Route` (foot / corr / lat / along); the board's `h`, `len`, `v_perp`, `omega`, `alpha`
  per entry from the deciding ship's own decision (`seq_rates`: ω the yaw-pitch body rate, α = W k of the faster of yaw and pitch, equal
  on the capitals); `pair_verdict16b` (amendment 16's verdict, then (i) and (ii) for a watch pair); `verdict_sym` (stored sides, both
  directions, the more severe: review F6); `certified` (E's input: review F1); `seq_obliged`, `seq_cap` (the three forms; the latency
  counts both stored ages); `run_pass` (one pass per order-4 capital decision, before the avoidance: the verdicts `lane_pair` returns
  and the (b) follow cap and the sequencing cap site B applies: review F2); `seq_lapses`; `apply_cap`; `width_reread` (review F4).
* `ship_steering.cpp` `decide()`: the pass under `ship_steering_gates_capital` on order 4, bound to the avoidance (`lane_bind`), its
  cap through `apply_cap` after site B's fields are set; the entry refreshed afterwards (`lane_decision`). E (`e_verdict`): live
  commitment of both, `verdict_sym` with the key, `certified`. Key `X3M_SHIP_STEERING_GATES_CAPITAL_SEQ` (default on, only under
  the capital key); off = §16.9 with the review fixes.
* Rows and counters as §16.10.6, plus `retries=` and `h_m=` on the census row, `width_retry_ms` / `width_retries` on the install row.

**Implementation decisions** (each forced by a measured failure of the rule as written; scenario M, the calibrated plant):

1. *The 16b lateral reach is the cylinder's.* `reach()` of §16.1 caps at r, which a tilted cylinder (w, r) exceeds beyond atan(r / w)
   (its bounding radius is h). The lateral route uses min(h, w + len sin θ), and the reach growth is capped at h too. With the r cap the
   true-state certificate failed 219–14,058 frames at w = r (worst −396 m).
2. *The hand-over's state is minutes old.* τ ≤ 1 s covers a fresh decision, not P2's sample: a hand-over side's lateral drift is
   bounded over the whole time since P2 (v⊥ (T − T²/(2 t_stop)), the speed slewing down at a_lin) and its nose taken at no less than
   8.11° (the 0x1f rotate's end bound; order 0 holds no attitude [i]). Without it the true-state certificate missed by 2.3 m (a
   waiting Titan's nose drifted 0° → 3.7° in 4 s).
3. *The obligation is wider.* With the corridor reach ρ_c a joiner's lateral route lapsed mid-join (the Titan s4 at 42.6° reaches
   1,230 m, ρ_c = 745 m) and a hand-over off its corridor next to an in-lane trailer was never obliged (two settled ships), giving
   1,681–8,653 (c) frames with an approach per run at w 0.45 r and box overlaps down to −321 m. The mover's reach is h (its nose along the join is not bounded), the paths need 50 m + 100 m
   (`kSeqObligeM`, the lateral motion) beyond the reaches, and two settled ships of a watch pair are tested the same way. Still
   geometry only.
4. *The leader may be stopped.* "vF ≥ 0" failed on a stopped leader's −0.1 m/s slip residue; the route accepts vF ≥ −1 m/s, which the
   closing term τ (vF_T − vF_L) carries.
5. *h covers the measured length.* The static census found one installed capital longer than √(r² + w²) (Stellaris `machineFlagship`,
   l 6,417 m against h 4,847 m), so the half-length is max(r, l) with l from the same width read (§16.10.9's "otherwise h takes
   max(h, l_m)").
6. *Site B (F2), lowering only.* On the module's gate write the cap lowers v_ref when v_ref is a write; under an avoidance emergency
   the gate write is the emergency ramp floored at 10 m/s and the cap is not applied (accepted). On the lowering clamp path the cap
   lowers the cornering term, so site B's own computation (ramp with v_slip, v_corner, emergency) runs under it, without only the final
   10 m/s floor (`kShipFlagLower`); on the engine's law with no module write the cap is a lowering-only write of the engine's speed.
   The pass runs on order 4 only (order 5 has neither avoidance nor a cap).

**Ratified addition (main session, 2026-10-13): hysteresis on the in-lane classification.** A ship enters in lane at |q − ℓ| ≤
100 m and the nose ≤ 8.11° off F (order 4 in join mode, or the hand-over) and stays in lane until |q − ℓ| > 150 m or the nose >
12°, so a ship at the threshold does not flip verdicts. The per-ship bit (`Entry.in_lane`) lives on the lane board and is cleared when
the ship is no longer committed (another handled order, order 5, a refresh more than 1 s after the previous one: longer than any
fresh window). A state held only by the hysteresis uses the exit values wherever a margin assumed the entry bound: the footprint's
re-classification margin 37 m (25 m × sin 12° / sin 8.11°), a hand-over's nose floor of 12° in the lateral certificate and the
obligation's settled reach, and the leader's along-F credit less v (1 − cos 12°). The footprint itself (radius |q − ℓ| + reach + margin)
uses the actual offset and nose, so the (a) certificate stays geometric. Rows: `ship_capital_lane held=`, counter
`lane_hysteresis_hold`.

**Measured against §16.10.5** (9 runs per variant): at w 0.45 r and 0.25 r every target holds — 5/5 arrive and launch, kept + slow =
5, aim-at-A fallback 0, rotate after a slow join ≤ 4.0 s, join error ≤ 0.89°, box ≥ 138 m, 0 certificate violations, trailer
Δs − D_x ≥ −0.0 m, 0 (c) frames with a ship on its approach — except the liveness row (below). w = r is (c) as the note expects.

**What the rule does not cover, measured:**

* *Launch transients.* A capital in its corridor approaching its grid point next to a waiting neighbour is a corridor (a) pair; when
  the neighbour gets the through slot (order 5) the pair is a watch pair abreast, and only the along route could certify it. Those
  (c) frames (704 over the 9 runs at w 0.45 r, 0 at 0.25 r) and one box overlap of −217 m during such a launch (60 fps s7) are the KC
  grid's own lateral overlap at these widths (adjacent hulls 831 m against the 666 m pitch); the fixture counts them apart, with the
  grid-point exemption extended to the launch leg from an adjacent grid point. Remedies (holding approaching capitals D_x behind any
  adjacent waiting neighbour) would sequence the grid itself.
* *Liveness.* The longest continuous span at the cap floor is 67.0 s at w 0.45 r in the 30 and 60 fps runs (55.0 s at 144 fps): the
  slot-0 Titan waits behind the M2 s3, which waits behind the parked Colossus until it launches (the chain of §16.10.4 with decision
  3's wider obligation; with the note's ρ_c a capital's total floor time was 73–80 s per run and the (c) frames returned). The 60 s
  row is reported, not asserted. 26.6 s at 0.25 r.
* *§16.1's footprint certificate* (E's footprint route of (a)) still uses reach() and spheres of radius r; at large noses or w = r a
  hull can reach h > r. Not changed here (pre-existing).
