# Slip-aware steering: the law, the schedule, the slot approach and the avoidance under the engine's slip term

2026-10-12, Fable 5.1 (design agent), for the main session to ratify as amendment 13 of
[ship-steering-and-avoidance.md](ship-steering-and-avoidance.md). Decision: how the steering law, the speed
schedule, the formation slot approach and the predictive avoidance account for the integrator's slip term
([ship-movement.md](../reverse-engineering/ship-movement.md) section 13.2), now pinned and modelled in the host
fixture, where it turned 131 of the law's margins into gaps
([ship-steering.md](../verification/ship-steering.md) "Model calibration"). Markers: **[m]** measured on the
fixture's calibrated integrator or its Python port (below), **[t]** measured on a flight trace, **[i]** inferred,
**[p]** proposed. Nothing here is implemented; no production source changes.

Evidence produced for this note (both under `verification/results/ship-steering/`):

* [`slip_sweep.py`](../../verification/results/ship-steering/slip_sweep.py) → [`slip_sweep.txt`](../../verification/results/ship-steering/slip_sweep.txt):
  a port of the fixture's integrator in its fixed point (`fx`, the slip increment, the truncating position step, the
  `(2000 − dt)/2000` decay) and of the controller `0x0043ad20`, driven at constant rate (steady turns) and by one
  command through the controller (turn-ins); the five fixture ships, 60 fps with the fractional clock carry.
* [`slip_law_proto.py`](../../verification/results/ship-steering/slip_law_proto.py) → [`slip_law_proto.txt`](../../verification/results/ship-steering/slip_law_proto.txt):
  a 2-D replica of the fixture's formation scenario (leader at 0.9 v on a 5 km circle, the engine's catch-up /
  close-in branches, the think tick, site B lowering only) and of a carrot line follow, with the law's rate servo
  through the same controller, for the law variants compared below. The replica reproduces the fixture's M5 slot
  gap (195 m RMS, 14 window increases against the fixture's 185.9 m, 12) **[m]**, so its comparisons are trusted
  for ranking, not for the final figures: those come from the fixture after implementation.

**Ratified 2026-10-12 (main session)** as amendment 13 of the ship steering design, full scope (avoidance on the velocity vector, the lateral-velocity lead, the schedule for the closing speed, velocity-matching formation, the lag-scaled gate carrot), with one amendment: the attack-order lead is capped at min(τ, t_go) as §6 proposes, and the fixture thresholds that move back from "gap" to hard assertions are set from the implemented law's measured numbers, not from this note's targets, and are recorded in the ledger with the before/after table.

**Implemented 2026-10-12** as amendment 14 of [ship-steering-and-avoidance.md](ship-steering-and-avoidance.md). It is
numbered 14 because the capital gate transit took 13. Three additions found on the calibrated fixture are not in this note:
the lateral-velocity lead is not applied to a detour point; the gate carrot is shortened by half the lateral offset; and a
moving obstacle's near-centre miss takes its side from the relative velocity. Two further points differ from the note: the
slip is 0 on order 5 rather than a `min(τ, t_go)` lead, and the formation law feeds forward the leader's own lag. Numbers are
in the ledger's "Slip-aware law" section.

## Recommendation

Treat the ship's **velocity vector `u = v·f̂ + slip`** (`slip` = `obj+0x60..+0x68`, three more dwords read per call)
as the steered state, and the nose as the actuator with a known lag: measured on the calibrated integrator, the
velocity obeys **`u̇ = v̇·f̂ + (v·f̂ − u)/τ`, τ = 2 s**, exactly (within 1–3 % of the fixed point, §1). Four changes,
all inside the existing seams (no new hook site, no change to what the handler writes):

1. **Avoidance on the velocity** (§3): the swept sphere, the push-ahead and the "behind" tests use `û`, obstacles
   carry their actual velocity (`p+0x100 + obj+0x60`), the detour point sits at the slip-aware turn-in distance
   `d_turn,slip(C)` instead of 1 s of flight, and a pair is suppressed at site E only when the *velocity's* swept
   path already clears the body by C.
2. **Pursuit on the velocity** (§2.1): the aim direction is `(P − x) − T_d·u_⊥` with `u_⊥` the velocity's
   component perpendicular to the line of sight and `T_d = τ`: the nose leads so that the velocity, not the nose,
   ends on the line (variant L4 below).
3. **Speed schedule in the closing speed** (§2.3): the ramp and the cornering clamp are written for `u·t̂`; the
   nose speed handed to site B is `(v_sched − slip·t̂)/max(f̂·t̂, 0.5)`, capped at `v_max`; the cornering rate is
   `min(ω, ω_u)` with `ω_u = sin 60°/τ = 0.43 rad/s`, the fastest the velocity can rotate whatever the nose does.
4. **Formation by velocity matching** (§2.5): a follower's desired velocity is the leader's actual velocity plus
   `(slot − x)/T_f`; the nose velocity command is `N = u + G·(u_des − u)` (G = 2, T_f = 2 s), the nose along `N̂`
   at speed `|N|` (written at site B, lower only).

Plus the gate lead-in's carrot distance scaled to the ship's total direction lag (§2.4), and the fixture gaps
turned back into assertions with the expected numbers of §5 (the M5 slot threshold re-based, §5 and "Unknown").

Hot-path cost: 3 dword reads per steering call and 3 per snapshot obstacle; one 3-vector projection, one
division and one `1 − e^{−t/τ}` (a rational approximation, no CRT) per call; measured against the existing
0.63 µs per call budget this is under 5 % **[i]**. Native Windows: plain reads of fields the integrator writes
(ship-movement.md section 13.2 **[m]** listing), the same bytes on both runtimes.

## 1. Physics (question 1)

**The rule** (section 13.2, fixed point): per frame, `p+0x100 = fx(row2, speed)`; off order 5 and with speed ≠ 0
the part of last frame's `p+0x100` perpendicular to the new row 2 is added to the slip; `pos += (slip + p+0x100)·dt/1000`;
`slip ×= (2000 − dt)/2000`. Writing `u = v·f̂ + slip` for the velocity the position integrates:

* the increment is `v·(f̂_prev − cos Δθ·f̂)` = the rotational change of the nose velocity, so without the decay `u`
  would never change direction at all (full momentum); the decay returns `u` toward `v·f̂` with τ = 2000 ms;
* speed changes act at once (`p+0x100` carries this frame's speed; nothing of a speed step enters the slip);
* hence **`u̇ = v̇·f̂ + (v·f̂ − u)/τ`**: the velocity *direction* is a first-order lag of the nose with τ = 2 s;
  the velocity *magnitude along the nose* is immediate **[i]** derivation, **[m]** check below.

**Steady constant-rate turn** at rate ω (closed form from the lag, then the port at the yaw caps, 60 fps,
`slip_sweep.txt` table 1):

```
|u|/v = 1/√(1+(ωτ)²)      lag angle λ = atan(ωτ) (velocity behind the nose, toward the outside)
|slip|/v = ωτ/√(1+(ωτ)²)  R_eff = |u|/ω = R/√(1+(ωτ)²)   (R = v/ω, the nose circle)
```

| Ship | v m/s | ω °/s | ωτ | \|u\|/v port (pred) | \|slip\|/v port (pred) | λ port (pred) | R m | R_eff m |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| M5 Discoverer | 532 | 147.7 | 5.15 | 0.210 (0.190) | 0.978 (0.982) | 77.9° (79.0°) | 206 | 43 |
| M3 Nova | 247.5 | 92.8 | 3.24 | 0.308 (0.295) | 0.948 (0.956) | 71.5° (72.8°) | 153 | 47 |
| M6 Centaur | 180 | 33.75 | 1.18 | 0.654 (0.647) | 0.755 (0.762) | 49.1° (49.7°) | 306 | 200 |
| TS Mercury | 124 | 21.1 | 0.74 | 0.807 (0.805) | 0.589 (0.593) | 36.1° (36.4°) | 337 | 272 |
| M2 Boreas | 70 | 13.5 | 0.47 | 0.907 (0.905) | 0.422 (0.426) | 25.0° (25.2°) | 297 | 269 |

All **[m]** on the port; the prediction is the closed form. The §13.2 estimate `ωτ/√(1+(ωτ)²)` (0.59 for a TS)
is confirmed. Two readings: (a) in a *sustained* turn the circle is **tighter** than `v/ω`, not wider: an M5 at
its cap circles at 43 m radius at 21 % of its nose speed, 78° sideways; (b) the circle is reached only after
~τ, and the brief's "larger radius" is the **transient**: entering a turn the velocity keeps its old direction
for the first seconds. The transient is what breaks the margins.

**Turn-in** (table 2: one 90° command through the controller from straight full-speed flight; `d_turn(C)` = the
forward distance at which the flown path's lateral offset reaches C; the design's `√(C(C+2R))` for comparison):

| Ship | C_ship (crossing) | d_turn nose / with slip | √(C(C+2R)) | C_sta (flyby part) | d_turn nose / with slip | √(C(C+2R)) | peak lag | nose 90° done |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| M5 | 43.4 m | 385 / **735** m | 141 m | 234 m | 632 / **1153** m | 389 m | 53.0° | 2.55 s |
| M3 | 48.8 | 323 / 540 | 132 | 237 | 501 / 807 | 358 | 42.6° | 4.0 s |
| M6 | 161 | 795 / 1045 | 353 | 297 | 947 / 1218 | 519 | 21.2° | 12.0 s |
| TS | 102 | 493 / 659 | 281 | 264 | 639 / 832 | 497 | 23.0° | 10.7 s |
| M2 | 1764 | — | 2039 | 1258 | 1077 / 1212 | 1527 | 10.9° | 27.0 s |

**[m]**. The nose column already exceeds the design's closed form because the angular ramp (`α = ω·k`: 2 s to
full rate for the M5, 10 s for the M6, 20 s for the M2) was never in it (the implemented lookahead adds `2v/k`
for that); the slip roughly doubles the fighters' turn-in again. **Path offset after a turn** (table 3; the
asymptotic offset of the flown path from the nose path once the slip has decayed, closed form `τ·v·sin β`
outward and `τ·v·(cos β − 1)` behind for a turn much faster than τ): M5 10° → 181 m outward (pred 185), 30° →
523 m (532), 90° → 1059 m outward and 1048 m behind; M3 84 / 244 / 492; M6 61 / 178 / 357; TS 41 / 121 / 245;
M2 22 / 67 / 136 **[m]**. These are the slot overshoots and the gate heading errors of the gap list: a fighter
that turns 10° toward its slot ends 181 m outside the line its nose drew.

**Formation circle** (table 4, the fixture's leader at 0.9 v on 5 km): the follower's steady lag is 10.8° (M5),
5.1° (M3), 3.7° (M6), 2.6° (TS), 0.3° (M2), and the nose speed it needs for the leader's ground speed is
`0.9 v/cos λ` = 487.5 m/s for the M5 (cap 532): holding the slot is feasible for every class **[m]**; the
difficulty is the approach, §2.5.

## 2. The law (question 2)

Which parts must use the velocity, and how (formulas in the core's terms: `x`, rows `r̂, û_row, f̂`, `v`,
`slip` in m/s, the point `P`, `d⃗ = P − x`, `t̂ = d⃗/d`, `u = v·f̂ + slip`):

### 2.1 Pursuit: steer the velocity, not the nose

The aim direction becomes

```
u_⊥ = u − (u·t̂)·t̂                     the velocity's component across the line of sight
P_aim = P − T_d·u_⊥,  T_d = τ = 2 s     (variant L4:2)
θ, n̂ from P_aim as today (sections 2.2–2.4 unchanged: shortest arc, rate command, servo)
```

The nose leads the velocity by the angle that cancels the lateral drift the slip would otherwise carry: for a
far point this is the lag angle itself (`atan(T_d·|u_⊥|/d)` → λ in a steady turn); for a near point it is
bounded by the geometry. `T_d = τ` is not a gain: it is the slip's own memory, so the correction is the
distance the lateral velocity will actually travel. The aim stays clamped to 60° from `t̂` (an aim farther off
would fly away from the point). Replica evidence (`slip_law_proto.txt` A and B, L0 → L4:2): formation slot
RMS M3 20.6 → 6.9 m, M6 40.5 → 19.8, TS 15.8 → 7.3 **[m]**; the carrot line follow (start 1 km off the line, a
step the fixture never applies) at a 2 s carrot: M5 lateral −65 → −27 m with the velocity 3.5° off the line
(nose 6.6°), M3 −9 → 2 m / 0.2°, TS 30 → −17 m / 0.6° **[m]**. It does **not** rescue the M5 slot (§2.5) and it
does not help the slow noses on a step (M6, M2: their nose lag, 3.5 s and 7.9 s to turn 10°, exceeds the slip
lag; §2.4 handles them by distance, not lead).

### 2.2 Curvature limit

The velocity direction rotates at most at `ω_u = v·sin β/(τ·|u|)` for a nose lead β; with β capped at 60°,
`ω_u ≈ 0.43 rad/s = 25°/s` **[i]** from the lag equation. For every ship whose cap exceeds that (M5 2.58 rad/s,
M3 1.62, M6 0.59) the slip, not the nose, bounds how fast the path can bend, and the cornering clamp of
section 2.5 must use

```
ω_c = min(ω_min_axis, 0.43 rad/s);   v_corner = ω_c·d/(2 sin θ)
```

Example: an M5 approaching a point 100 m away at 90° is limited to 21.6 m/s instead of 129 m/s **[i]**. TS and
M2 are unchanged (their caps are below ω_u). The effective radius in the sense of the brief is therefore
`R_eff,transient = |u|/ω_c`: M5 1237 m at full speed (nose model 206 m), M3 575 m (153), M6 419 m (306), TS
337 m, M2 297 m **[i]**.

### 2.3 Arrival schedule and the hold

The ship arrives along `u`, and the closing speed is `u·t̂ = v·(f̂·t̂) + slip·t̂`, not `v`. The schedule is
written for the closing speed and solved for the nose speed:

```
v_sched = v_ref + √(a·max(0, d − d_arr))  (as today, d to the order's point)      then
v_cmd = clamp( (v_sched − slip·t̂) / max(f̂·t̂, 0.5),  10 m/s,  v_max )               at site B
```

After a turn toward the point the slip points away from it (`slip·t̂ < 0`), the formula raises the nose speed
so the ship still closes at `v_sched`; after a turn away from it the nose speed is lowered. The hold inside
`d_arr` is unchanged (the engine's own arrival test stays on `f+0x240`), but a held ship still carries `slip`,
which decays over 2 s at the arrival hold's zero rate; the engine's MovePos arrival (`[obj+0x10] = 0`) zeroes
`p+0x100` while the slip still moves the ship: `τ·|slip|` metres, up to 181 m for an M5 that turned 10° in its
last second (table 3) **[m]** on the port. The arrival clamp therefore uses the closing speed, which brakes the
nose early enough that the residual slip at the boundary is the ramp's own `v_ref` **[i]**; the fixture's
arrival assertion decides (§5).

### 2.4 Gate lead-in: align the velocity, carry the nose within the engine's test

The engine's 18.2° misaligned test on order 5 is on the nose; the disc is crossed by the velocity. Both must
hold: the lead-in aims the *velocity* along the line (§2.1), which puts the nose within the lag of the line, and
the lag on a straight lead-in decays to zero (`slip → 0` in 2 s on a straight segment). The remaining cause of
the 27–42° crossing errors is the carrot: at 0.75 s of flight it is shorter than the velocity's 2 s memory, so
the line follow is under-damped (damping ratio `≈ 0.5·√(T_carrot/τ_total)` with `τ_total` = slip lag + nose
lag **[i]**). Proposal **[p]**: the carrot sits `T_p = max(0.75 s, τ + t_10)` of flight ahead with `t_10` the
nose's time to turn 10° under its own controller (`2·√(10°/α)` from the live caps: M5 0.75 s, M3 1.2 s, M6
3.5 s, TS 3.1 s, M2 7.9 s **[m]** table 3), floored at 0.3 r as today: M5 2.75 s = 1.46 km, M3 3.2 s = 0.8 km,
M6 5.5 s = 1 km, TS 5.1 s = 0.63 km. The window (3 r = 3.3 km before the crossing) is long enough for these.
Replica step test at a 2 s carrot with the velocity lead: M5 velocity 3.5° off the line, M3 0.2°, TS 0.6°; at
4 s the M6 reaches 7.8° with 59 m lateral where the 0.75 s carrot gave 31° and 363 m of overshoot **[m]**. The
crossing assertion stays on the nose (the engine's test) and gains the velocity angle (§5). Capitals keep
every vanilla step (amendment 10).

### 2.5 Formation slot: velocity matching, not pursuit

The slot error 8 → 186 m is not an overshoot of one approach but a permanent catch-up: the replica's M5
follower sits in the engine's catch-up branch at full nose speed for the whole run with a 160–220 m error
(trace in the session: nose swings of 5–21° every 2–3 s, `|slip|` 40–320 m/s, lag up to 40°) **[m]**. Each
swing of a pursuit aimed at a point 0.4 s ahead costs ground speed through the slip, and the 53 m/s closing
margin is spent on it. No pursuit variant fixes it (L1, L2, L4 at every gain: 173–1160 m RMS, `slip_law_proto.txt` A).
Velocity matching does, partly:

```
u_lead = v_lead·f̂_lead + slip_lead            the leader's actual velocity (its p+0x100 + obj+0x60, via f+0x224)
u_des  = u_lead + (slot − x)/T_f, |u_des| ≤ v_max          T_f = 2 s
N      = u + G·(u_des − u),  G = τ/τ_c = 2 (τ_c = 1 s)      the nose velocity that makes u reach u_des in τ_c
nose along N̂ (through the servo), nose speed |N| clamped to [10, v_max] at site B (lower only)
```

Replica (A2, L5:2 T_f = 2): slot RMS M5 195 → **92 m**, M3 20.6 → 17.1, M6 40.5 → **6.1**, TS 15.8 → **2.6**,
window increases 0 everywhere **[m]**. The M5 stays above its 2 r = 18.4 m threshold (it never enters 4 r in
the replica); its floor under this law is open (§"Unknown"), and the fixture's M5 threshold must be re-based
on what the implementation measures rather than on the nose model's 8 m. The engine's close-in branch
(`20 r` ahead, its own speed ramp) still owns `[obj+0x14]` upward; our write only lowers, as everywhere.

### 2.6 Roll, port legs, order 5

Unchanged. On order 5 the integrator skips the increment (section 13.2), so the slip only decays: the port-leg
hold and the final zone keep their amendment-12 behaviour, and the velocity lead of §2.1 is applied with
`T_d = min(τ, t_go)` there (a near port, a decaying slip: the correction shrinks with the remaining time).

## 3. Avoidance (question 3)

* **Relative motion on the velocity.** `Δv = v_obs − u` with `u = v·f̂ + slip` for the own ship and
  `v_obs = p+0x100 + obj+0x60` for ship obstacles (stations, asteroids and gates stay at `p+0x100`, 0). The
  fixture's snapshot (`World::fill`) and the DLL's walk (`ship_avoidance_walk_core.h`, `kPhysVelocity`) each gain
  the three slip reads. `Self.f` stays the nose for the detour plane; `Self` gains `u`.
* **The predicted path includes the lag.** The swept segment is `x + u·t` for `t ≤ T`, which is what the ship
  will do if the nose does nothing; the turn-in distance the detour needs becomes the slip-aware one. Closed
  form **[p]**, fitted to table 2 within 10 %: `d_turn,slip(C) = d_turn,nose(C) + v·τ·(1 − e^{−t_nose(C)/τ})`
  where `t_nose(C)` is the nose time to the lateral offset C under the ramp; in practice the port's table 2 and
  3b numbers for the fixture ships, and in the DLL the simpler bound `d_turn,slip ≤ d_turn,nose + v·τ`.
* **L and C.** `L` adds the lag distance `v·τ` (M5 +1064 m, M3 +495, M6 +360, TS +248, M2 +140): the swept
  length must cover a detour that starts 2 s later than the nose model assumed. `C` itself is unchanged (it is a
  clearance, not a path), but the **detour point** moves: `P' = x_cpa + ê'·C·f` is pushed ahead along `û` (not
  `f̂`) by `max(1 s·|u|, d_turn,slip(C·f))`, so the point is one the flown path can reach. For the M5 crossing
  (C = 43.4 m, offset 1.75 C = 76 m): the push becomes 855 m instead of 532 m (1 s) or 458 m (the nose path),
  table 3b **[m]**; the TS flyby part (1.3 C = 343 m): 879 m instead of 124 m / 680 m. The "behind" tests
  (`toPp·f̂ > −0.5|toPp|`, `toP·toPp > 0`) are evaluated against `û`; a committed detour whose point drops behind
  the *velocity* reports unresolved as today.
* **Why the M5 crossing fell to 0.8 m [i]:** both ships detoured (343/361 detours, 4/5 unresolved ticks), both
  to the same handedness, but with a point 1 s ahead along their noses while their velocities lagged 20–50°:
  each turned its nose toward a point its path could not reach for another 700 m, held its old velocity
  through the CPA, and the suppression at E stood because the *nose* segment cleared by C. The flown paths
  crossed where the nose paths did not.
* **Resolved set / site E.** A pair is suppressed only when the velocity's swept segment (`x + u·t`, the lag
  included) clears the body by C, i.e. `closest_approach` on `u` misses by ≥ C *and* the straight segment to
  `P'` clears as today; otherwise the engine's reactive warn stays, which is itself on the nose ray (§4), so a
  conflict the velocity is already carrying the ship into is never masked by a nose that has turned away.
* **Expected numbers** (fixture after the change, **[i]** targets, not simulated): TS flyby@150 min clearance ≥
  C = 263.5 m (the detour starts `v·τ` = 248 m earlier and the point sits 879 m ahead; before the slip model it
  was 319 m); M5 crossing ≥ 43.4 m with unresolved ticks ≤ 5 + detours/100; M2 flyby unresolved ticks: the
  Boreas's "unresolved" rows are `behind`/`blocked` refusals of a point its 11° lag made unreachable at 1 s of
  flight (70 m); with the push along `û` and `d_turn,slip` (1212 m for C_sta; the 1.3 C_sta offset is not
  reached within the sweep's window, table 3b) the refusals should fall back under the limit; if they do not, the reason counter says which refusal remains.

## 4. What the engine itself does (question 4)

* The vanilla leaf aims the nose at the point and nothing in the command pass reads `obj+0x60`
  (section 13.2's writer list: the integrator, the contact response, undock, glide, warp) **[m]**. The engine's
  reactive warn `0x0044dd90` uses `[A+0x10]` and A's forward axis (`flight+0x870` row 2) for the closing speed
  and the heading-ray cone ([ship-navigation-ai.md](../reverse-engineering/ship-navigation-ai.md) §3.1) **[m]**:
  the engine's own avoidance is as blind to the slip as our layer was. Its margins (`L` 30 m–1 km plus the
  braking distance, the 200 ms re-score, `SA_StopObject`) cover it by size, not by modelling.
* Visible in the fixture's vanilla baseline **[m]** (`ship_steering_host.txt`, `ship_steering_host_gaps.txt`):
  the Boreas's vanilla leaf no longer settles a diagonal 179° turn at 144 fps (GAP rows "the vanilla leaf settles
  too (−1 ms)"); vanilla's gate θ+45 crossing error for the M5 is 13.7° at 209.6 m/s against ours 27.0° at
  532 m/s (vanilla ramps to 10 m/s at A, so its slip at the crossing is a fifth of ours: slower, not better); the
  vanilla gate lateral 3 r approach is 22.4° (ours 22.0°). Vanilla arrival times are within 34 ms of ours
  (M5 66,249 vs 66,283).
* **So yes: the fixture's thresholds were tuned to a model without the slip, and the real flights ran on the
  margins the engine's sizes give, not the ones the fixture claimed.** The flight evidence (Run151–156: capitals
  through the gates, conflict episodes 593 → 86) was produced with the real slip, so it stands as flight
  evidence; what does not stand is the fixture's 319 m / 101 m / 8 m as predictions of it. No in-flight minimum
  clearance was logged, so the actual flight margins are **[i]** unknown; the `ship_avoid_detour` rows carry
  `miss`, which a triage of Run156 could aggregate.

## 5. Verification (question 5)

Checks that turn back from GAP to assertions, with the expected number (fixture, 60 fps seed 1; "replica" =
measured in `slip_law_proto.txt`, otherwise a target):

| Family | Assertion | Expected after |
| --- | --- | --- |
| M3 / M6 / TS formation | slot RMS ≤ 2 r; 2 s window maxima non-increasing | 17 / 6 / 3 m (replica, L5), 0 increases |
| M5 formation | slot RMS ≤ threshold | 92 m in the replica; threshold re-based on the implementation's measurement (open), windows 0 |
| M5 crossing | min distance ≥ C = 43.4 m | ≥ C (target) |
| TS flyby@150 | min clearance ≥ C = 263.5 m; unresolved ≤ 5 + detours/100 | ≥ C (target); within the limit |
| M2 flyby@600/150 | unresolved ticks within the limit | within the limit, else the reason counter names the refusal |
| gate θ+45 (TS, M5, M3, M6), M3 θ 0 | nose error to B at the crossing ≤ 22°, plus a new field: the velocity's angle to B | nose ≤ 22°, velocity ≤ 13° on the line / 20.7° with the spread (the amendment-10 bound) |
| M5 gate far/rotated, lateral 3 r | ≤ 22° | ≤ 22° (replica at the 2 s carrot: velocity 3.5°, nose 6.6°) |
| M2 diag turn 144 fps "vanilla settles too" | stays a gap (vanilla's own deficit), not an assertion on us | — |

New fixture facts: the trace row gains `slip=` (three fields) so the replay can check the read, and the gate
rows the velocity angle. **In flight:** `ship_trace` already carries `pos/fwd/v` per frame; `u` is the
per-frame position delta divided by dt and the lag is `acos(û·fwd)`: the replay can report the lag per order
class (expected p50 ≈ 6°, p95 ≈ 25° on order-4 turns from §13.2's `|slip|/v` 0.105 / 0.457 **[i]**), and the
`ship_steering_decision` row gains `lag_deg` and the aim offset, so a wrong sign in the lead shows as a lag that
grows instead of shrinking after a turn. Cost: self 3 reads + obstacles 3 reads each (420 × 3 × ~5 ns ≈ 6 µs per
sector per frame, inferred); the law's extra work is a projection, a division and the exponential
approximation per call, under 5 % of the 0.63 µs budget **[i]**; the formation law adds the leader's two
velocity reads per follower call.

## 6. Scope and risk (question 6)

**Minimal set** (fixes the avoidance gaps and the line follow, no new inputs beyond the own slip): §3 (velocity in
the swept test, obstacle actual velocity, push along `û`, `d_turn,slip`, E on the velocity), §2.1 (the lateral
velocity lead), §2.3 (the schedule in the closing speed), §2.2 (`ω_c`). **Full set** adds §2.5 (the leader's
velocity: two more reads through `f+0x224`, a new formation branch of the law) and §2.4 (the class-scaled carrot).
Recommend the full set in one step: the formation gap is the largest, and its law is the same inversion as §2.1.

What could regress:

* **Fighters in combat orders** use the same law with `d_arr = 0`. The lead `T_d·u_⊥` against a manoeuvring
  target is a lead *away* from where the target is now; cap `T_d` at `min(τ, t_go)` for orders without an
  arrival distance so a near target is aimed at directly, and watch the attack-order rows' bearing in the first
  flight. The cornering rate `ω_c` lowers a fighter's speed on a side approach far more than today (the M5
  example in §2.2): correct for the physics, but a visible behaviour change in dogfights; it applies only where
  `v_corner` already applied (sin θ > 0.05 toward a point).
* **Docking:** the final zone is engine-owned and the slip is zeroed there (`0x004543f3` **[m]**); the port-leg
  hold is untouched; the only change on order 5 is the shrinking lead. The engine's docking arrival creep
  (1 m/s) cannot be affected.
* **Gates:** a longer carrot moves the join earlier in the window; P2/P4/P5 and the spread are untouched; the
  Terran ring bound (amendment 9) is a diagnostic. The capital path is vanilla.
* **The formation law** writes a nose speed from `|N|` instead of the ramp; a leader that decelerates sharply
  makes `u_des` shrink and the follower brakes at `a` — the slew's limit, as the engine's own match does.

## Alternatives considered

| Alternative | Why it loses |
| --- | --- |
| Lead compensation on the nose with a fixed gain (`N = u + G(v·t̂ − u)`, L2) | G = 2 helps M3/M6/TS slots (11 / 13.5 / 4.5 m) but the step line follow diverges for slow noses (M2 495 m, M6 176 m lateral) and the M5 slot gets worse (285 m) **[m]**; the gain must track the nose bandwidth, which the lateral-velocity lead does implicitly |
| Aim-point feed-forward on the slip only (`P − slip·τ(1 − e^{−t_go/τ})`, L1) | helps the slow classes' slots (M3 5.2, M6 21, TS 6.8 m) but the M5 slot goes to 668 m and the M5 crosses the gate line with its velocity 33° off **[m]**: it corrects the drift, not the loop's damping |
| A longer carrot alone (L0 at 4 s) | the slow noses improve (M6 −124 m / 6.6°) but fighters no longer converge inside the window (M5 278 m lateral at 3 r) **[m]** |
| Re-base the fixture thresholds to the nose model's numbers | the margins would be fiction; the flown path, not the nose path, meets the body (§4) |
| Per-class tables of lag and lead | the project's rule (formulas on the live getters) and the physics has one constant, τ = 2 s |
| Patch the integrator to remove or shorten the slip | changes the engine's physics for every ship, the player's included, and the contact response that reads the same field; outside the renderer-style app-local contract |
| Site C (per-frame steering at the integrator) to counter the slip each frame | per-object per-frame cost and no benefit: the lag is in the plant, not in the cadence |

## Unknown, and what settles it

| Open point | Settles it |
| --- | --- |
| The M5 slot floor under velocity matching (92 m in the 2-D replica, never inside 4 r) | the fixture's formation scenario with L5 implemented; if the floor stays above 2 r, the threshold becomes the measured floor × 1.2 and the engine's catch-up branch (`10 × prec` = 23 m for an M5) is the suspect: it re-aims at full speed each time the error crosses 23 m |
| Whether the slip's lateral lead is right in 3-D (the port and the replica are yaw-only) | the host fixture's diagonal cases after implementation; the sign is fixed by the row convention of section 3 |
| The in-flight margins of Run151–156 (never logged) | a triage of `ship_avoid_detour` `miss` and the trace's per-frame lag; not needed for the decision |
| The leader's velocity read for order 0x14/0x15: `f+0x224` → `[obj+0x70]+0x100` and `obj+0x60` of the leader | the same reads the snapshot already does for every ship; a sanity check that the leader is class 7 and not deleted |
| `ω_u`'s cap angle (60°) | the fixture's cornering cases; a larger cap only raises `v_corner` |
