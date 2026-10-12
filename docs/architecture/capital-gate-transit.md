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
