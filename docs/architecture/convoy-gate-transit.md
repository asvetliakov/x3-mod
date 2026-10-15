# Convoy gate transit: one departure lane, a car-merge rule, no holding

Design note, ratified 2026-10-14. Merged from the two independent designs
[convoy-gate-transit-fable.md](convoy-gate-transit-fable.md) (Fable 5.1, the base) and
[convoy-gate-transit-opus.md](convoy-gate-transit-opus.md) (Opus 5.5); where they differ, §9 says which law was taken and
why. Question: how a capital convoy (a leader plus 2–4 capital escorts) goes through a gate smoothly instead of the holding
loops and waits of the nine level-1 builds (Run161–169). Sources: [gate-traffic-controller.md](gate-traffic-controller.md)
(D.0–D.12, A.1, B.1, B.6–B.9), [ship-navigation-ai.md](../reverse-engineering/ship-navigation-ai.md) §1, §3.4, §4,
§10.1–10.11, [gate-queue-script.md](../reverse-engineering/gate-queue-script.md) §3–6, §8,
[capital-gate-transit.md](capital-gate-transit.md) §5, the Run166–169 triage under
`verification/results/ship-steering/run16[6-9]-triage/` and
[`convoy-gate-transit/convoy_spans.py`](../../verification/results/ship-steering/convoy-gate-transit/convoy_spans.py) →
[`convoy_spans.txt`](../../verification/results/ship-steering/convoy-gate-transit/convoy_spans.txt) (Opus; the leader's 15 km
moment, the launches, the escorts' distances then, the no-wait lane floor). **[m]** measured, **[i]** inferred, as marked.

## 0. Ratification (user decisions, 2026-10-14, via the coordinator)

1. **The trail line.** Every capital committed to a gate flies one departure lane into the funnel at hull spacing; no
   holding pattern, no racetrack, no waiting at 10 km.
2. **The merge rule, as the user stated it:** "no waiting: the leader goes, then the escorts go. If two or more escorts are in
   the same position (for example abreast in a line at the same distance from the gate), the second ship just reduces speed
   or stops briefly to let the next ship pass, and the passing ship does not reduce speed at all; the second ship only gives
   a little space so the passing ship gets ahead of it, it does not wait until that ship has passed the gate." A brief
   yielder stop is accepted ("reduce speed/stop is fine").
3. **Teleporting** the convoy is rejected.
4. **No DLL-issued gate orders.** The transfer is script-driven (`2004::__EnterHyperspace` → `SA_WarpObjectAway` →
   `ReleaseFromSlot`, `Remove()`, `__LeaveHyperspace`); the module steers, times and places the script's own orders.
5. **Stage 1 without script patches first.** Stage 2 (the x3story.obj and MSCI constants, §6.6) only if the flown script
   latency is still visible after stage 1.
6. **Player-owned ships are members like any other** (2026-10-14, later the same day): lane membership, the merge rule and
   the point rewrite apply to every AI-controlled capital whatever its owner; the owner field is not a criterion (this
   supersedes §6.7's "player-owned convoys never fly the lane" and §8 item 8's owner guard). The only exclusion is the ship
   the player is flying manually (the existing player-object flag), which stays an obstacle under the existing rules.
7. **No cruise-speed change between gates** in stage 1 (§10): stragglers join the lane when they arrive; revisit after Run171.

**Outcome.** One departure lane per gate: capitals join it like cars at a lane merge (the ship that would reach the merge point
later yields until its nose is 400 m behind the other's tail; the ship ahead never slows; a leader is always ahead of its own
escorts), then follow each other down it at hull spacing. Each capital's order 4 ends at a point the module writes on the lane
2.3 km before the gate (the short final), so the KC's through-slot occupancy (≈ 14 s with SETA off, ≈ 2 s on) is shorter than
the hull cadence (20–27 s) and nobody paces on the ship ahead's gate passage. The script's own steps are kept and satisfied on
the move; the escorts' order 4 comes 2–9 s after the leader has left and that latency is flown, not waited. A formed 1+2
convoy takes ≈ 185–200 game s from 15 km to its last launch against 330–2,170 s measured; 1+4 ≈ 235–250 s **[i]**. Petals,
the lap law, the release window, stage points and every escort wait are deleted.

## 1. Recommendation

1. **One departure lane Λ per gate** (stage 1, DLL only: sites A/B/E/P2/P4/P5 and one field write; key
   `ship_steering_gates_lane`, needing `ship_steering_gates_traffic`; off = stage B as installed in Run169).
   * Λ is F-parallel in the lower half of the disc (D.2.1), from about 15 km to d_end = 2.3 km, at the lateral middle of
     the members' common pass window; inside 4 km each hull's final moves to its own Q within its D.2.2 window (≤ 500 m
     over ≥ 2 km). Hulls under ≈ 500 m take the axis.
   * A member joins Λ at its **merge point** M_i, where its join curve (the cornering cap, the existing join predictor)
     becomes tangent to Λ: 8–15 km out for an inbound ship, 3–5 km for a re-departing one after a U-turn.
   * On Λ: v_lane = the large cap (100 m/s, site B both directions) unless the follow law or a block says less.
2. **The merge rank** (§6.2): rank by ETA to the merge point; the later ship yields; within a 2 s tie band the higher
   sequence key yields; a leader is always ahead of its own escorts; convoys are not reserved as blocks.
3. **The merge and follow laws** (§6.3): the yielder's speed drops until the gap tail-to-nose is g_req = 400 m, floor
   10 m/s, a stop of at most 20 game s + one step when 10 m/s does not certify; then the follow law holds
   `L_P + L_Y + 400 m` of centre spacing (2.5 km census M2/M1, 1.9 km M7–M7). The passer's plan is never changed.
4. **The short final** (§6.4): site A rewrites a capital's order-4 point `f+0x240` from the KC grid point to its lane
   point L_i = (2.3 km + 0.75 r_gate) before the plane, so order 4 ends at d_end on the lane; P2 keeps the speed, 0x1f is a
   one-frame no-op, the first through-slot request finds the slot free, order 5 is a 1.0–1.2 km leg (SETA off) or a first-frame
   trigger (SETA on), order 8 launches with P5's kept speed.
5. **The escorts' gate order** (§6.5) is the script's: `fly to sector` 2–9 s after the leader's `Remove()`. The trail cadence
   exceeds the leader's slot occupancy plus that latency, and an escort without order 4 is paced not to pass d_end; site B holds
   the trail speed on its order-0 frames (the latency and the 0x14 re-issue gaps), so nothing slows.
6. **Blocks** (§6.6): the lane's head paces to the slot's free time down to 10 m/s and then stops briefly at d_end; the line
   stops behind it at spacing. Stage 2 would cut the 61 s own-arrival block to 10 s (one byte) and the escorts' script wait to
   ≤ 0.3 s (constants in two Mayhem scripts), with the caveats in §6.6.
7. **Kept from level 1:** the board, hulls, sweep, site-E certificate, B.9's rollout corrections, Λ/Q and the join predictor,
   the through-slot model, P2/P4/P5, `0x00444606` placement, D.5. **Deleted:** petals, lap law, release window, head skip,
   escort stage/targets/`esc_in10`/`escort_settled`, the escort petal rollout (D.2.4, D.2.5's window, D.3, B.6–B.8). §6.9.

## 2. What Run166–169 measured [m]

| Convoy (run) | Leader 15 km → launch | Escort launches after the leader | 15 km → last launch | Escorts' distance when the leader was at 15 km |
| --- | --- | --- | --- | --- |
| 4900 + 2 M7 (168) | 131.5 | +123.9, +198.5 | 330.0 | 17.8, 18.6 km |
| 10107 + 2 (168) | 130.7 | +144.7, +206.6 | 337.3 | 26.0, 29.4 km |
| 5727 + 2 M7 (169) | 136.0 | +179.4, +224.4 | 360.4 | 24.9, 30.3 km |
| 24965 + 2 (169) | 134.6 | +390, +547 | 681.5 | 41.7, 49.3 km |
| 4673 + 2 (169) | 607.2 (slot block, out of window) | +80.6, +162.0 | 769.2 | inside 15 km |
| 3097 / 8846 / 12699 + 3861, 4120 (169) | 214.6 / – / – | re-targeted twice by the script | 2,170 | 25.9, 30.3 km |

* Unobstructed leaders took 130.7–142.3 s from 15 km; the straight line at 100 m/s is 113.6–116.4 s to the launch; the order-5
  trigger fired at 3.36–3.64 km under SETA (`convoy_spans.txt`).
* Per escort the serial funnel cost ≈ 100 s: released 8.3–14.5 km out, release → X 55–205 s, X → launch 14.6–33.6 s
  (typically 15–21); two-deep 3 times in 15 releases (`run169-triage/funnel_serial.txt`).
* Hold seconds by reason, Run169 (`hold_totals.txt`): escort waiting for order 4 with the leader not launched **2,416.8**,
  launched **506.6**, head out of window 378.9, head slot block 310.0, funnel busy 237.8; 3,919.5 game s in 478.5 real s,
  SETA share 0.82. Nose turned away from the gate: 606 s escorts leader-first, 313 s petal phase, 278 s slot block
  (`convoy_summary.txt`).
* Escort order 4 after the leader's launch (`escort_wait.txt`): parked inside 10 km +4.1…+16.9 s (8 escorts); 13–17 km out
  +56…+110 s; inbound from 33–36 km +278…+301 s. The script's first pass inside 10 km always ordered at once.
* Run166–168: 721 / 869+543 / 396+491 game s of escort waiting per flight; B.6–B.9 moved the waiting place, not its length.
* The escorts straggle in 3–34 km behind their leader because the previous gate let them out 80–550 s after it **[i]**. A
  convoy that leaves in trail arrives in trail.

**Reading.** The loops are, by size: escorts circling while their leader holds or transits (2,924 s in Run169); the serial
funnel flight from a petal (≈ 100 s per escort); the head's petal phase and slot blocks (689 s). None is the engine's gate
passage itself.

## 3. The cadence floor

* The KC grants one through slot per gate pair from the grant after 0x1f to `ReleaseSlot` after order 8 **[m]** §10.2. Its
  occupancy T_slot is the order-5 leg plus 0.9 s plus about 1 s of hand-over: 17–23 s measured from today's X with SETA on;
  ≈ 41–45 s with SETA off from today's X; **≈ 14 s with the short final** (SETA off) and ≈ 2 s under SETA, when the inflated
  trigger `t = (r_s + |v|/10)(1 + (tf − 1)/4)` **[m]** lies beyond d_end.
* In line two census hulls need `L_P + L_Y + 400 m`: 2.5 km (M2/M1, 25 s at 100 m/s), 2.4 km (M7 behind an M2), 1.9 km
  (M7–M7, 19 s) **[i]**. That is just outside the engine's close-range warn `1.2 (r_A + r_B)` (2.62 km Titan–Titan) **[m]**
  rule; the closing-pair cone test stays covered by the certificate **[i]**.
* So the cadence is c = max(hull spacing / v_lane, T_slot) = the hull spacing, and the trail never paces on a gate passage
  (the user's "does not wait until that ship has passed the gate") once the short final holds. Without it (the §8 item 1
  fallback) the SETA-off cadence is T_slot ≈ 41–45 s, a documented deviation.
* **3 s per escort** needs hulls abreast, laterally `w_A + w_B + 100 m` apart inside the trigger window `|Q| ≤ r_s + v/10 −
  100 m`: an M7 (w 350–380) beside a Titan (w 583) at 1.1 km fits (gap 137 m); two Titans need 1.27 km in a ±1.0 km window and
  do not **[i]**; it also needs two capitals on order 5 at once, which the KC's single slot forbids until option B/A5 and the
  engine side is settled (§8 item 1). It is the later (d2) step of §4/§9.
* Convoy time from 15 km: the leader's ≈ 135 s plus c per escort: **≈ 185–200 s for 1+2, ≈ 235–250 s for 1+4** **[i]**
  (SETA on / off; game seconds scale with the step).

## 4. Option matrix

| | Today (B.9) | (a) Level 2 as named | (b) Convoy unit as briefed | (c) Own transit state machine | **(d1) One lane + merge, script satisfied** | (d2) Two-lane echelon, concurrent order 5 |
| --- | --- | --- | --- | --- | --- | --- |
| 15 km → last launch, 1+2 | 330–770 s [m] | ≈ 320–760 s (saves 4–9 s once) | not reachable as stated (2–3 s launches); its reachable form is (d1) | ≈ (d1) | **≈ 185–200 s** | ≈ 150–175 s |
| 1+4 | – | – | – | ≈ (d1) | **≈ 235–250 s** | ≈ 160–175 s |
| Player sees | petals 8–14 km out, escorts circling 270–520 s, the funnel one at a time | the same petals, the order a few s earlier | a column, then KC-forced gaps | a column | a column flying straight in; level ships merge; nobody circles | two columns side by side |
| Engine / script surfaces | sites A/B/E/P2/P4/P5, `0x00444606` | + DLL-started orders, + KC clearance patch | + interception of script order writes | + order-4/5 emulation, + KC patch | sites as today + the `f+0x240` write on capital order 4 + site-B policy on order 0 (stage 1); constants only (stage 2) | (d1) + KC slot rewrite (B or A5) + arrival fan |
| Script fights? | none | high: a DLL order wakes the blocked 0x14 call with a false result and the KC never runs the sector change | high, same | high, same | low: the script's orders are kept and steered | medium: two holders per pair is new KC state |
| Save / Mayhem | – | x3story.obj replaced (B is save-safe) | none | none | stage 1 none; stage 2 A1 save-safe, MSCI edit overwritten by a Mayhem update | KC rewrite |
| Cost | – | medium, small gain | – | very high | **medium (stage 1), small (stage 2)** | high, after (d1) |
| Verdict | | loses (§9) | loses (§9) | loses (§9) | **recommended** | later, on top of (d1) |

Why the transfer cannot be ours **[m]**: order 5's result 1 wakes the task inside `__EnterHyperspace`, which issues
`SA_WarpObjectAway`, then `ReleaseFromSlot`, `Remove()` and, in `__FlyToNextSector`, `__LeaveHyperspace` in the destination
(`Activate`, `SA_StartInHangar`, the launch `0x00444170`). The engine's order 8 is 900 ms of kinematics; the sector change is
KC state. A DLL-started order on a ship whose task is blocked in `escort ship` wakes that call with a false result, and the ship
ends on order 0 past the plane, still in the sector (§10.11 Q3, §1.1).

## 5. What the player sees (d1) [i] from the measured laws, SETA off, v 100 m/s

| Moment | Leader | Escort 1 (M2) | Escort 2 (M7) | Today (Run169, measured) |
| --- | --- | --- | --- | --- |
| commit, 30–40 km | join curve to Λ, merge point ≈ 12 km | formation slot beside or behind; merges behind the leader (yields if level) | merges behind escort 1 | escorts to separate petals |
| 15 km | on Λ, 100 m/s | 2.5 km behind | 2.4 km behind E1 | leader may hold; escorts circle |
| d_end 2.3 km, t ≈ 127 s | order 4 ends (the written point), speed kept, 0x1f 1 frame, slot granted, order 5 | 25 s behind | 49 s behind | release → X 55–205 s per ship |
| t ≈ 140 s | launch (order 8 from the 1.1–1.3 km trigger) | | | X → launch 15–34 s |
| leader gone + 2–9 s | | `fly to sector`; order 4 to its written point, flown at speed on order 0 meanwhile | 0x14/0x15 steered in trail | order 4 at +4…+301 s |
| t ≈ 165 s | | launch | at d_end | escort 1 +80…+390 s |
| t ≈ 190 s | | | launch | escort 2 +162…+547 s |

A blocked gate shows as the line slowing together and, if the block is long, stopping at spacing with the head at d_end,
noses on the gate. A merge shows as one ship easing off until it is a hull spacing behind the other, then following. Nothing
circles and nothing turns away from the gate.

## 6. Design

### 6.1 Lane geometry and joining

* Λ = `G − ℓ_c U + s F`, s ∈ [−15 km, −d_end], ℓ_c the middle of the members' common D.2.2 window. Inside s > −4 km each hull's
  final moves to its own ℓ_i (the Q window), a lateral change ≤ 500 m over ≥ 2 km (≤ 15°), after the last merge, so finals
  never cross. Hulls under ≈ 500 m (window 0–137 m) take the axis.
* **Joining.** A member flies its inbound carrot (B.2, B.8's braking profile folded: it brakes to v_lane at the merge point,
  not to 60 m/s at a ring) toward M_i, the lane point where its join curve at the cornering cap becomes tangent to Λ. A
  **re-departing ship** (arrived through this gate, faces −F at P) flies out along −F, U-turns on the departure side (−U, away
  from the arrival lane) at its cornering cap and joins Λ 3–5 km out; being nearest the gate it is the head and the line
  behind yields: ≈ 90–120 s **[i]** against 591 s measured with the nose away.
* **Escorts** are lane members like any capital: their 0x14/0x15 leaf and speed are ours (D.3), their merge point is their own.
  An escort beside its leader at the commit merges level and yields by rank (§6.2); one behind falls in.
* The lane keeps the member's recorded last 6 km of positions only as a diagnostic row input; the follow law uses the
  predecessor's live along-lane position (§6.3), since everything after M_i is straight.

### 6.2 Who yields (merge rank)

The gate keeps one ordered list, the lane order, of members on or joining Λ, ranked by **ETA to the merge point** of the
later-merging ship of a pair at present plan speeds (for two ships on Λ that is their along-lane position).

* The ship with the **later ETA yields**. Within a 2 s tie band (two escorts abreast at the same distance, the user's
  example) the **higher sequence key** yields (keys as D.2.5: an escort's = its leader's + 1 + rank).
* **A leader is always ahead of its own escorts**: an escort ahead of its leader yields to it whatever the ETAs (it could
  not start order 4 before the leader has left anyway, §10.11 Q3, and would have to wait at d_end).
* A foreign capital with an earlier ETA may merge between a leader and its escorts; that costs the escorts one cadence, not a
  wait. Convoys are not reserved as blocks.
* After a merge the pair is a follow pair; the lane order changes only at a merge, never by overtaking on Λ.
* The arrival role (engine-moved, D.4) never yields.

This replaces stage A's `yields_to` (sequence, then B.6's release order) and D.1's priority list. B.9's open item (the front
ship designated yielder cannot open a gap closing from behind) disappears: the front is never the yielder.

### 6.3 How much the yielder gives: merge, follow, stop

With s along the lane toward the gate, P ahead and Y the yielder: tail(P) = s_P − L_P, nose(Y) = s_Y + L_Y, L = max(r, l)
(D.1's capsule half-length). The merge is complete when **gap = tail(P) − nose(Y) ≥ g_req = 400 m** (the sweep's 30 s margin
350 m plus 50 m).

* **P's written speed is its plan, exactly.** No pair cap is ever applied to the ship ahead; its lateral path is its own join.
* **Merge law:** `v_Y = clamp(v_P,along − (g_req − gap)/τ_m, v_floor, v_plan,Y)`, τ_m = 8 s, at the 200 ms decision cadence.
  Census hulls level at the merge (gap ≈ −2.1 km): Y drops to the floor and opens the gap in ≈ 28 s, which is about one cadence,
  its place in the trail anyway **[i]**.
* **Lateral:** Y keeps its own join line and converges onto Λ only once gap ≥ g_req − 100 m: behind P, never beside it.
* **v_floor = 10 m/s** (the existing yield floor). **A brief stop** (accepted, §0) only when the 10 m/s rollout still predicts
  a capsule conflict: Y is written 0 for at most T_stop = 20 game s plus one game step (`Gate::step_ms`, SETA-aware). If that
  does not certify either, the pair goes back to the engine (`cause=contact`), as today.
* **Follow law** after the merge: `v_Y = min(v_plan, v_P + (gap − g_req)/τ_f)`, τ_f = 10 s, floor 10 m/s. Where P stops (a
  block) Y stops at spacing; where P launches (order 8, 2.7 km in 0.9 s) Y's law reverts to its plan toward d_end.
* **The certificate's view.** Rollouts fly the join, merge, follow and stop laws, **front ship first**: a follower's rollout
  reads its predecessor's rolled-out samples, so the predicted gap is ≈ g_req throughout and the sweep certifies with D.1's
  margin. A yield is a plan change of the yielder only, re-swept at its next decision; `cause=merge|stop` (new). B.9's
  `gap_yield` and cap-in-force rules stay for the steered frames; freshness bounds unchanged.
* τ_m, τ_f, g_req and T_stop are design values; the fixture measures them (§6.11).

### 6.4 The short final: writing the capital's order-4 point

* **What.** On a capital's order-4 frames toward its gate (site A: class-0x12 target, capital code `0x0043fd50`,
  `f+0x224 == Γ`), when `f+0x240` equals one of the KC's 20 grid points `G − 2(r_gate + r_s)F + 666 m (a R + b U)` within 1 m
  (closed form, §10.2), site A writes the hull's lane point L_i = the point on its final (2.3 km + 0.75 r_gate) before the plane.
  Order 4's own test (`dist ≤ 0.75 r_gate` at every re-plan tick, `0x00454b1a/0x00454b70` **[m]**) then fires at d_end = 2.3 km.
  A KC re-issue after 0x12 or the result-7 path writes the grid point again and the guard catches it again; the write is
  repeated only when the field differs from our last value.
* **Why 2.3 km.** Beyond the order-5 trigger of a 1.2 km hull (t ≈ 1.3 km, SETA off) plus 1 km; a ship stopped there on a
  refusal (D.2.6, kept) is 2.3 km out on the lower half, clear of the arrival lane. Under SETA the trigger (3.4–3.6 km **[m]**)
  lies beyond d_end: order 5 ends on its first frame and order 8 starts at d_end, ending ≈ 0.4 km past the plane (vanilla under
  SETA ends 0.9 km in front).
* **Why it is safe as far as known.** The KC passes the point once per blocking call and never reads it back; no SA case
  returns `f+0x240` to a script (§10.11 Q2 sweep) **[m]**; only the order-4 handler reads it on that leg (§8 item 2 confirms
  the handler's own reads and the detour). The serializer saves it **[i]**: a reloaded ship flies to L_i, a valid point in
  front of the gate, with or without the key.
* **What it buys.** T_slot ≈ 14 s with SETA off (41–45 s from today's X), below the hull cadence; every capital's X is on its
  own final whatever slot the KC handed out (D.2.2's 13-of-20-slots case and the ±2-slot jog go away); the 828 m chord
  precision of X goes away.
* **The hand-over.** P2 keeps `[obj+0x10]` and zeroes the remainders (D.12) for a member crossing d_end with the nose ≤ 8.1°
  off F; 0x1f ends on its first frame; the loop head's `RequestWarpThroughSlot` finds the slot released by the predecessor
  (same side, in sector: no cooldown **[m]**). Site B holds the lane speed through the order-0/0x1f frames (§6.5).
* **A refused request** (a partner-side block, a hidden holder, `JumpToSector`'s use of the slot): the ship is on order 0 after
  0x1f, nose on F. Site B holds the speed only while the longest poll (11 s) keeps it outside its trigger sphere + 300 m, else
  brakes at `a` to a stop there (at d_end under SETA, since the sphere already contains it). When granted, order 5 starts from
  that speed. Counted in `ctl_x_refused` with seconds, per role.
* **Fallback** if §8 item 2 says the point is not re-read: keep the grid points, run Λ through the slot-0 column (slots 0, 1, 2,
  5 are chords; 3, 4, 6, 7 need a 114–504 m jog ≥ 4 km out) and accept SETA-off pacing on T_slot ≈ 41–45 s.

### 6.5 The escorts' gate order after the leader's launch

The script path is fixed **[m]** (§10.11 Q3): the leader's `Remove()` ends the escort's 0x14 (lost leader), `__EscortObject`
returns 8, the template waits 2–3 s, `CheckLeader` → `!move.jump` → `Mayhem.Task.Move` → `FlyThroughNextGate`: inside 10 km
`fly to sector` → `__EnterHyperspace` → warp-in slot → order 4; beyond 10 km a 5 s `follow object gate` leg first. Measured
+4.1…+16.9 s for parked escorts; the first pass inside 10 km always ordered.

* **Geometry hides it (stage 1).** The escort is c ≈ 19–25 s behind its leader; the leader's launch is at X_L + T_slot
  (≈ 14 s SETA off, ≈ 2 s on); the escort reaches d_end at X_L + c, so λ ≤ c − T_slot ≈ 11 s (off) / 23 s (on) fits the
  measured 4–9 s. **Escort gate:** an escort without order 4 must not pass d_end; it is written `min(follow, the speed that
  reaches d_end at t_L,launch + λ_max)`, λ_max = 10 s. With the short final this binds only when the leader's launch is late.
* **Nothing slows during λ.** The escort is on order 0 after the lost-leader end: site B writes its trail speed on a member's
  order-0 frames while its nose is within 8° of F (the latency, the 2–3 s 0x14 re-issue gaps, the 0x1f/order-0 hand-over); the
  integrator keeps the last leaf's turn through order 0 (§10.11 Q4). Order 4 then arrives with the escort on the lane short
  of d_end; its point is rewritten (§6.4) and the leg continues. Rank 4 of a Titan convoy may be just outside 10 km at the
  leader's `Remove()` and flies one 5 s 0x15 leg (ours) first: no wait either.
* **Stage 2 removes λ itself** (§6.6) if the flown latency is still visible.

### 6.6 Blocks, re-departures, crossing traffic, and the stage-2 script patches

* **Head pacing.** The lane's first ship that has not reached d_end aims to reach it no earlier than the slot model's free
  time (D.2.5: the predecessor's predicted release + 2 s, or a partner-side block), with today's funnel speed shaping down to
  10 m/s, then the brief-stop rule at d_end. By construction T_slot < c, so pacing binds only behind a partner block or a KC
  refusal, never on a convoy's own predecessor. Followers pace only on the hull ahead through the follow law.
* **The opposite-direction block.** The KC refuses our side for 60 s after a partner-side holder releases **[m]** §10.2: the
  310 s "head, slot block" of Run169, including re-departing ships blocked by their own convoy's arrival. The arrival's own path
  (0x21 up the +U half, 0x1e axis-parallel) is kept off Λ by D.4; the sweep gives the arrival priority.
* **Crossing convoys** bound for different gates stay with the engine's warn and our sphere avoidance (different boards); a
  cross-board certificate is a later item.
* **Stage 2, only if the flown latency is still visible (§0 item 5).** Constants only, reversible loose files with SHA-256
  guards on the originals; the patcher refuses on a hash mismatch (a Mayhem update) and stage 1 still hides the latency.

  | Patch | Site | Change | Effect | Caveat |
  | --- | --- | --- | --- | --- |
  | A1 | `ReleaseSlot`, stock 90cfb / Mayhem a6b66, `pushb 60` → `pushb 10` (1 B) in a loose `L\x3story.obj` | the own-arrival block 61 s → 10 s: covers the arrival's 0x21 (0.9 s) and 0x1e until r_gate clear (≤ 9.2 s at 120 m/s) | save-compatible both ways, build id unchanged (gate-queue-script §6) **[m]** static; applies to every gate pair, in and out of sector | does not change same-direction serialisation (one slot per pair stays) |
  | wait | `!move.follow.template.xml` line 97, `wait randomly 2000–3000 ms` → 100–300 ms | `CheckLeader` within ≈ 0.3 s of the lost-leader end; the 0x14 re-issue gaps shrink to ≤ 0.3 s | a Mayhem file; overwritten by a Mayhem update | the smaller change; recommended first if stage 2 is needed |
  | 10 km | `Mayhem.Task.Move.xml` line 221, `$distance > 10000` → `> 10000000` | `fly to sector` at once wherever the escort is (stock X3's behaviour) | every Mayhem NPC escort in the universe enters the KC transit from wherever it is; Mayhem's reason for the 10 km leg is undocumented (§8 item 6) | the larger change; "patch the wait only" is the default, the 10 km lift only if measured stragglers beyond 10 km still show a visible leg |
  | A6 (optional) | `jf` at 90c3b / a6aa6 (4 B) | out-of-sector same-direction traffic exempt from the 60 s | an arriving convoy into the player's sector today comes one capital per 60–72 s (Run160 **[m]**) because the departure side is out of sector | needs the arrival fan of §6.7 |

  Expected λ with the wait patch ≤ 1 s **[i]** (the KC resume plus `!move.jump`/`Task.Move`'s non-wait statements). MSCI
  encoding, `.pck` precedence and the save safety of a constants-only MSCI edit are §8 item 6.

### 6.7 Arrival side

* D.4 kept: capital arrivals +0.6 r up at `0x00444606`, the small side spawn, the 60 s arrival-gate row.
* **Arrival fan** (Opus): with trail cadence c ≈ 19–25 s a convoy's next capital would appear at the same P while the previous
  one may still sit there (a hull of r_s ≥ 0.99 r_gate ends 0x1e on its first frame and rests at P until its script moves it,
  §10.10). The launch site keeps a per-gate count of capital launches in the last 30 s; the k-th (k = 0, 1, 2) gets +0.6 r U,
  −0.6 r R, +0.6 r R, Terran bound unchanged. **Hull-fit guard:** the fan points are 937 m apart, so it separates hulls only
  when `w_k + w_prev + 50 m ≤ 937 m` (two M7s, an M7 beside an M2; not two Titans, w 583 each) **[i]**; otherwise the placement
  stays D.4's and `arr_capital_unseparated` counts it. The fan matters only with the player in the destination sector and
  only once arrivals come closer than 60 s apart, i.e. with A6 or for a convoy departing from the player's sector into it.

### 6.8 Edge cases

* **The player's ship** is never a member or ranked; it stays an obstacle under the existing rules (the manual player ship
  is excepted from the r/3 ignore), so the engine warns a capital that would hit it and that member is evicted as today, then
  re-merges. Player-owned convoys teleport by beacon in Mayhem (`!move.jump` line 83) and never fly the lane; the member guard
  must recognise player ownership (§8 item 8).
* **Small ships.** D.5 unchanged; "funnel active" becomes "a member on the lane's last 6 km or on order 5/8". Small escorts
  keep the formation law with the corridor projection.
* **Combat.** `__EnterHyperspace` returns 7 when attacked; the template's attack checks break off; the member leaves the lane
  and its follower's law sees the next ship ahead (or none). Nothing is held for it.
* **Leader destroyed mid-transit.** `while $leader exists` ends; an escort already in `__EnterHyperspace` transits as an
  ordinary lane member; one still on 0x14 leaves the lane when its next order arrives; the leader-first rule lapses.
* **The script re-targets escorts to a newly arriving M1** (Run167–169; 3861/4120 had three leaders, 2,170 s). Script-owned;
  ranking is per gate, so a re-targeted escort becomes another ship's escort or leaves. The window shrinks from minutes to
  the seconds after the leader's departure. §8 item 7 locates it.
* **Save/load.** Board state is in-process: on load the lane order is rebuilt by ETA; written points persist harmlessly; a
  backward clock jump clears caps (B.9).
* **Key off:** no rewrite, no merge law, stage B unchanged. Stage-2 files are removed by the installer's restore; a save made
  with A1 loads without it.

### 6.9 What of level 1 is kept, deleted or folded (under the new key)

| Item | Fate |
| --- | --- |
| D.1 member board, hulls, capsule sweep, site-E suppression, eviction, pointer liveness (B.5) | **kept**; the pair priority becomes the merge rank; rollouts fly the join/merge/follow/stop laws front first |
| B.9 rollout fixes (in-plane split, inbound law, `gap_yield`, signed age, cornering cap, clock-jump reset) | **kept**; `hold_law` becomes `lane_law` (join → lane → follow) |
| D.2.1 disc halves, D.2.2 pass windows, D.12 both-direction write on order 5, P2/P4/P5 | **kept** |
| D.2.3 funnel, one ship at a time | **replaced** by the lane, many ships at hull spacing |
| D.2.4 petals, rings, columns; B.6 lap law, window, head skip; B.7 escort stage; B.8 join-to-track and dwell; the go-around | **deleted**; the go-around becomes a re-merge |
| B.8 inbound braking profile on the join | **folded**: brakes to v_lane at the merge point |
| D.2.5 sequencer and through-slot model | **kept** as the head's pacing input and the partner-block clock; sequence keys are tie-breakers and the leader-first rule |
| D.2.6 grid point untouched; refusal = stop at X | the grid point **replaced** by §6.4; the refusal stop **kept**, now at d_end with the speed hold/brake of §6.4 |
| D.3 escort petals, reserved keys, `esc_in10`, `escort_settled`, `escort_target`, the 60 m/s 0x15 approach | **deleted**; escorts are lane members under the leader-first rule; keys kept as tie-breakers |
| D.4 arrivals | **kept**, plus the fan (§6.7) |
| D.5 small ships, r/3 rule | **kept** |
| B.9 "nothing written on order 0" | **replaced** by §6.5's speed hold for members with the nose within 8° of F |
| Diagnostics | `ship_gate_ctl_member` minus the petal fields (`rho s stage_s ringed`), plus `lane_s rank gap_m law=join/merge/follow/stop/pace`; pair row `cause=merge|stop`; new `ship_gate_ctl_merge` row at each merge start and finish; counters `ctl_merge_yield`, `ctl_merge_stop`, `ctl_point_rewrites`, `ctl_x_refused` (+ s), `arr_capital_unseparated` |
| Key `ship_steering_gates_traffic` off = amendment 17 | **kept**; `ship_steering_gates_lane` off = stage B as installed |

### 6.10 Hot path, cost, native Windows

* Per frame: the lane order is an insertion sort of ≤ 24 members by ETA per gate; the merge, follow and pace laws are a few
  flops per member at the 200 ms decision cadence; the coupled rollout adds one predecessor-sample lookup per sample (61) to
  the existing ≈ 6 µs typical / 70 µs worst decision work (D.8 **[i]** from measured sweeps); a merge adds at most one extra
  candidate rollout (the stop). Deleting the petal geometry and lap law removes more than this adds **[i]**.
* §6.4: one class/code test per capital order-4 site-A call; the 20-point compare only when `f+0x240` differs from the last
  written value; one 12-byte store. Site B on order 0: one branch per member frame.
* No allocation, no lock, game thread only, SSE2 scalar. The i686 figures come from `ctl_sb_us_max` / `join_eval_us_max` in
  the first flight. Stage 2 has no DLL cost.
* **Native Windows:** the game runs natively at `C:\X3`; everything is an in-process patch of the same `X3AP.exe` through
  `engine_patch::verify_bytes`/`claim`, verified by `verify_ship_steering_sites.py` (no new site). Stage-2 files are data for
  the game's own loader. No OS API is added.

### 6.11 Verification

**Fixture: scenario T gains lane variants** (`ship_steering_host.cpp`: the calibrated plant with slip, the KC model with one
through slot, the 60 s cooldown, the in-sector exemption and 1–11 s polls, the escort template's leader-first rule with its 2–3 s
wait and the 10 km pass every 5 s, the box hulls and E). Cast from Run169 (M2 1092 + M1 1022 + M2 1092; M1 + M7 760 + M7 778;
a 1+4 variant). Variants: **abreast** (leader + 2 escorts level at 12 km, and + 4: the user's case); **formed column**;
**stragglers** with the Run169 15 km offsets from `convoy_spans.txt`; **re-departure**; **crossing convoy** merging; **partner
block**; a non-member parked on Λ; the leader destroyed 2 km before d_end; script models with and without stage 2 (λ 4.3–9 s vs
≤ 1 s, gaps 2–3 s vs 0.3 s); SETA steps 1, 6 and 10; 60 and 144 fps; 12 seeds. Assertions:

1. **The passer:** in every frame of a merge the ship ahead is written exactly its plan speed; capped frames on the ahead
   side = 0.
2. **The yielder:** gap ≥ g_req − 50 m within 40 s of the merge start; speed ≥ 10 m/s except stops; each stop ≤ T_stop + one
   step; lateral convergence only after gap ≥ g_req − 100 m.
3. **No holding:** 0 petal frames; a member's cumulative turn after its merge point ≤ 30° (the re-departure U-turn lies before
   its merge point); 0 frames with the nose > 90° off the gate while the lane is free (oracle 606 + 313 + 278 s).
4. **Order:** the leader launches before its escorts; consecutive launches ≥ T_slot apart and, for a formed convoy, ≤ c + 5 s.
5. **Escort gate:** no escort passes d_end without order 4; order 4 ≤ λ_model after the leader's launch; no stop at d_end
   except a modelled refusal (counted); written speed on order-0 frames ≥ the trail speed − 2 m/s.
6. **The short final:** every order 4 ends within one re-plan interval of d_end; P2 keeps the speed; 0x1f ≤ 1 frame; the first
   through-slot request granted in the free-gate variants (`ctl_x_refused` = 0); with polls, refusals ≤ 1 per member, ≤ 11 s each.
7. **Safety:** 0 0x12 between members, 0 capsule and 0 box contact, certified every frame from a merge's first certificate to
   its completion + 30 s.
8. **Span:** 15 km → last launch ≤ the leader's 15 km → launch + n·c + 10 s for a formed convoy, and ≤ the lane floor of
   `convoy_spans.py` + 10 s for stragglers.
9. **Key off:** stage-B outputs identical (D.7 item 8). Units: merge rank (tie band, leader-first), the three laws, lane
   geometry and pass windows, the §6.4 guard against all 20 grid points and its re-application after a re-issue, the order-0
   speed hold's brake bound, the arrival fan and its hull-fit guard.

**Flight oracle** (the next convoy flight, `--debug`): `convoy_spans.py` 15 km → last launch ≤ the lane floor + 30 s per closed
convoy; `convoy_summary.txt` buckets "head out of window" 0, "funnel one at a time" 0, "escort before leader launch" nose-away
0, "slot block" only after a partner arrival (≤ 11 s with A1); `funnel_serial.py` release → X = 0 for followers, X → launch
≤ T_slot + 5 s; `escort_wait.py` order 4 at +2…+10 s (stage 1) or ≤ 1.5 s (stage 2); per escort turns > 30° ≤ 2, stops only as a
yielder ≤ T_stop; X nose ≤ 8°; `x12_pairs.py` 0 member episodes; `ship_gate_ctl_merge` rows paired; `gate_rows_dropped` 0.
Acceptance is the user's: no loop, no turn-away, escorts through within one cadence each of their leader.

## 7. The script fighting our steering, item by item

| Script behaviour | Effect on the lane | Settled by |
| --- | --- | --- |
| 0x14 re-issued every ~33 s with 2–3 s order-0 gaps **[m]** | gaps flown at speed (§6.5); the integrator keeps the last turn (§10.11 Q4) | §8 item 4; stage 2 shrinks the gaps |
| `escort ship` ends on timeout/lost leader; `fly to sector` only after the leader's sector change and inside 10 km **[m]** | λ 2–9 s flown; rank 4 beyond 10 km flies one 0x15 leg (ours) | §8 item 3; stage 2 |
| `__EnterHyperspace` phase 0: `SA_CollisionObject` → 0x12 instead of order 4 when the warn bit is set **[m]** | only for an uncertified or non-member pair (player, crossing traffic): eviction, then re-merge | as level 1 |
| `RequestWarpInSlot` refused (20 slots full): `SA_SetSpeedLimit(m3, m93)`, `SA_Idle` wander (0x1d, steered) **[m]** | the leaf stays ours; the limit `f+0x278` may cap site B's write | §8 item 9 |
| through-slot poll 1–11 s after a refusal **[m]** | brake bound of §6.4 | §8 item 5 |
| Mayhem re-target of an orphaned escort to a new M1 **[m]** flights | window reduced to seconds | §8 item 7 |
| `JumpToSector` (bd7a3) also takes the through slot **[m]** | an unpredicted refusal; counted | flight `ctl_x_refused` rows |

## 8. To disassemble or settle before implementation (merged)

| # | Unknown | Gates | Listing or fixture |
| --- | --- | --- | --- |
| 1 | Whether two capitals can be on order 5 / order 8 at one gate at the same time in the engine. KC: no, one through slot per pair from the grant to `ReleaseSlot` **[m]**. Engine: small ships already do so with no queue **[m]**; the order-5/8 handlers write only per-frame OR flags on the gate (`0x2000000`, `0x10000000`) **[i]**; the `SA_WarpObjectAway` case's "gate flags" and `[gate.f+0x180]` (written by the launch, 10 s) are unlisted | (d2) only | `0x0046126a` (case 0x0e), `0x004551fe..0x00455330`, `0x004548cf..0x00454996`, the gate post-pass update `0x0044b0c0` (`0x0044b124`), readers of `[gate.f+0x180]` in `0x0045b720..0x0045b7f9` and `0x00444170..0x00444a32`; the pair filter `0x0045c7b3..0x0045c7e3` is per ship, no count **[m]**; `ReleaseSlot` 0x90c9c with two holders under option B |
| 2 | Where `SA_FlyObjectBeforeContainerPos` stores pos for a capital, and whether order 4 reads `f+0x240` every re-plan for both the goal and the 0.75 r test with no cached copy; whether the container detour (§3.4) engages for an order point within 2 r_gate of a gate; the re-plan interval `rand(50·type[+0x50])` for capitals (the precision of d_end) | stage 1 | SA case `0x0046533c`; handler `0x004549c2..0x00454b97` (point read `0x00454a20`, compare `0x00454b70`, detour `0x0042f970` operands); extend `stageb_sites.py`'s `+0x240` sweep; the installed capital types' `+0x50` |
| 3 | How 0x14 ends when the leader leaves (the test, the frame, the result; taken from the handler table in §1.3, never listed) and `__EscortObject`'s return-8 path: λ's first component | stage 1 | `0x0045820d` listing; KC `__EscortObject` stock `b8b2a` / Mayhem `d10f7` |
| 4 | Order 0 under autopilot: whether it writes `[obj+0x14] = 0` every frame or once, whether site B (`0x004593dd`) is reached on a capital's order-0 frames and its write stands through the slew loop, and `[obj+0x44] & 4` | stage 1 (the speed hold) | `0x00458f8e..0x004590e0`, `0x004594b0..0x004595b1` |
| 5 | The KC task's resume latency after order 8 → `ReleaseFromSlot` (frames between the engine's order-8 end and the slot's release) | the X margin (T_slot's hand-over term) | the scheduler's runnable path after `0x004604d0` (open in script-task-scheduler.md); flight `ctl_x_refused` for followers |
| 6 | MSCI: how `wait randomly` and the `> 10000` compare are encoded in `<codearray>`; `.pck` vs `.xml` precedence in `addon/scripts`; whether saves store MSCI instruction positions; Mayhem's reason for the 10 km leg | stage 2 | a read of the two installed files and the script loader (`tools/X3ScriptLoadStudy.java` style); Mayhem changelog/forum for `Mayhem.Task.Move` |
| 7 | Which Mayhem script re-targets waiting escorts to a newly arrived M1, and its trigger | residual risk | text sweep of `addon/scripts` for leader assignment on arrival or commander change (`Mayhem.Squadron.*`, `!ship.cmd.protect.std`, the template's `CheckLeader`) |
| 8 | How the DLL recognises a player-owned ship (owner/race field) for the lane guard | stage 1 | the object's owner field in `game-scale-and-ship-classes.md`'s record layout or the `SA_GetOwner` case |
| 9 | `m93`/`m96` passed to `SA_SetSpeedLimit` on a refused warp-in slot | flight check | KC `__EnterHyperspace` bcc55 operand; `0x00465d1a` |
| 10 | The time-factor reading `[*0x606f34+0xcc]` (sets t_trig, d_end versus the trigger, T_slot under SETA) | flight check | its writer (the SETA toggle); a `ship_gate_trigger` row at P4 with `t` and `tf` |
| 11 | What an arrived capital does after its first-frame 0x1e (speed, next order) before the next convoy hull appears | the arrival fan | `ship_gate_launch` + member rows with the player in the destination sector (user run) |
| 12 | The SA result protocol for a DLL-ended order: `[obj+0x7c]` lifetime during order 4, `0x00460560 → 0x004604d0` with the task's cell | (d2) only (the 4 → 5 swap) | `0x00460560..0x004605d0`, `0x004604d0..0x00460530` |

Items 2, 3, 4 and 8 gate stage 1; 6 gates stage 2's MSCI half; 1 and 12 gate only (d2); the rest are flight checks. Still
**[i]**: the capsule half-length l per installed capital (sets c; the census row with l settles it), T_slot ≈ 14 s with SETA off
and the short final (fixture and a SETA-off flight segment), the lane floors in `convoy_spans.txt` (134 s to launch, c = 25 s).

## 9. Options considered, and why they lose

* **(a) Level 2 as named** (DLL-issued order 4/5 at the leader's launch plus a KC clearance patch). An order is not a transit:
  the sector change is KC code inside the escort's own `__EnterHyperspace`; a DLL order wakes the blocked 0x14/0x15 call with a
  false result and strands the ship on order 0 past the plane (§4). Its gain would be the first escort's 4–9 s; stage 1 hides
  that geometrically and stage 2 removes it with constants. The measured cost it does not touch is ours (petals, the serial
  funnel, the release window, ≈ 100 s per escort). The clearance patch (B) is kept as a stage-2 candidate against blocks.
* **(b) Convoy unit at 15 km with 2–3 s between launches.** One lane cannot hold two 1 km hulls 300 m apart (≥ 2.5 km in line,
  25 s); the KC grants one slot per pair from X to the end of order 8; taking the whole convoy at 15 km means waiting for
  stragglers 3–34 km behind (against "the leader goes") or leaving them, which per-gate merge ranking does naturally;
  "neutralising the script's follow orders" is unnecessary, 0x14/0x15 are steered orders and the lane is flown on them. The
  DLL hand-over to order 5 (the 4 → 5 order-word swap inside `__EnterHyperspace` phase 0) is identical in time to (d1) in trail
  and is kept only as (d2)'s tool.
* **(c) Own transit state machine replacing order 4/5.** The KC owns the slot grant and the sector change; the engine's order-5
  trigger is trivially met from the lane; it would re-implement steering we own through sites A/B plus a KC patch for no gain.
* **(d2) Two-lane echelon with concurrent order 5** (3 s per escort). Side by side, census hulls need ≥ w_A + w_B + 100 m
  laterally: an M7 beside an M2 fits the lower-half window, two M2-size hulls do not (§3). It needs the KC slot rewritten to two
  holders per pair (B or A5; A5 alone lets any number fly order 5 with `CollisionWarn` ignored), a three-way arrival fan, and
  §8 items 1 and 12. A later step on top of (d1), reusing its lane.
* **X placed by trail timing instead of a fixed d_end** (the Fable base: X at `z_trig,pred + s − v·(0.9 s + 2 steps)`, order 5
  leg ≈ the spacing). Rejected for Opus's short final: with the leg equal to the spacing, T_slot ≈ c and the follower's first
  request lands within a frame of the predecessor's release, so any mis-predicted release or KC resume latency (§8 item 5)
  becomes a refusal, a poll and a coast; with d_end = 2.3 km, T_slot ≈ 14 s leaves an 11 s margin at SETA off and 23 s under
  SETA, and no X needs timing at all. The fixed point also keeps a refused ship 2.3 km out instead of at the trigger edge.
* **Trail on the leader's recorded path from the commit** (the Fable base: a 6 km path ring per member, the escort's carrot
  the path point one spacing behind its predecessor). Rejected for the lane join: after the merge point everything is
  straight, and before it the join predictor with the "converge only behind" rule covers the curves; the ring is kept only as
  a row input. It would be the tool if the fixture shows join curves cutting into a predecessor on the re-departure U-turn.
* **25 m/s yield floor with a 15 s stop** (Fable) against **10 m/s with a 20 s stop** (Opus): the 10 m/s floor is the
  existing yield floor and opens a census gap in ≈ 28 s, one cadence; taken, with the stop bound in game seconds plus a step.
* **Along-axis position as the merge rank** (Fable) against **ETA to the merge point** (Opus): equal on the lane; ETA is
  defined for lateral joins where axis position is ambiguous; taken.
* **Nothing written on order 0** (Opus, relying on B.9's `gap_yield`): an M2 at 100 m/s loses 10–25 m/s in a 2–5 s order-0
  gap at a = 5 m/s², which opens the trail gap and costs a re-close every 33 s; the site-B hold on order 0 for an aligned
  member is one branch and is kept, pending §8 item 4.
* **Form-up before the gate** (the leader slows until its escorts close up): makes the leader wait, against the binding
  rule; see §10 for the between-gates cruise question instead.
* **Keep petals as a fallback for crowds:** speed shaping to 10 m/s and a brief stop absorb a partner block (≤ 11 s with A1,
  61 s without) on the final; upstream, slower joining absorbs a lane queue (15 km at 25 m/s absorbs 450 s); crowds were at
  most 3–4 members at once per gate in Run169 (`transits.txt`); loops are what the user rejected.
* **MSCI patch now** (escorts commit before the leader leaves): 2–5 s per convoy for a Mayhem-file dependency; stage 2 by
  decision (§0 item 5).
* **Lanes abreast now:** see (d2).

## 10. Open questions for the user

1. **Cruise between gates.** Escorts arrive 3–34 km behind their leader because the previous gate released them 80–550 s later
   (§2); after stage 1 they will leave one cadence apart and arrive formed. If the user still wants convoys formed on arrival at
   the next gate regardless of what happened upstream, the leader would cruise at 0.9 v_max between gates (a formation-law
   change outside this note, costing 10 % of the leader's transit time). Not part of this design; the user decides after the
   first flight.
2. **A brief yielder stop** (≤ 20 game s + one step) is accepted (§0 item 2). Recorded; no further question.

## 11. Implementation (stage 1)

**Stage B deletion (2026-10-15)** ([ship-steering-holistic-review.md](ship-steering-holistic-review.md) rank 2): petals, the sequencer, the lap and hold laws, the amendment-16 lane board, the seq key, site W and the slow join are gone. The key off is now the stage-A certificate alone, not stage B. The lane-on output of every lane cast is identical to Run174 (`laneon_diff.py`, which replaces `keyoff_diff.py`), except that a capital the lane does not fly (a board refusal, a recommit hold, a fresh commit before its first frame-end stage) loses amendment 16's slow join and un-latch with the key on too and takes amendment 15's latched aim-at-A fallback. Run173's log counted `gate_capital_slow_join=0` and `gate_capital_unlatch=0` (2 `pred_none` fallback rows), so no flown effect ([`latch_counts.py`](../../verification/results/ship-steering/run173-triage/latch_counts.py)). The mentions of stage B, `sb_frame` and `keyoff_diff` below are historical. Ledger: [ship-steering.md](../verification/ship-steering.md) "Stage B deletion".

**Source split (2026-10-15)** (rank 3): the lane's code now lives in `ship_gate_lane_core.h` (laws, the obstacle and solid rows, `lane_frame`, `lane_site_b`, `lane_steer`) and `ship_gate_geometry_core.h` (Λ, the lane points, the join, the carrot) behind the umbrella `ship_gate_ctl_core.h`; the DLL side is in `ship_steering_decide.cpp` (`gate_decide`: the order-4 point rewrite), `ship_steering_clamp.cpp` (`lane_clamp`) and `ship_gate_ctl_glue.cpp` (`lane_frame_all`). The lane-on output is identical (`laneon_diff.py --base bf12aaa2`: 0 of 397 / 170 / 226 / 118 lines). The DLL's own default of `ship_steering_gates_lane` is 1 since then. Layout [ship-steering-and-avoidance.md](ship-steering-and-avoidance.md) §8.1, ledger "Source split".

State 2026-10-15 (sixth implementer, lane fix round 2, "Lane fix round 2" below: items 1, 2, 4 done, 3 / 5 in part, 6 open; `--lane` 2,851 checks, 0 failures). Before that (fifth implementer, lane fix round 1 after Run173, "Lane fix round 1" below): scenario C green (`--lane` 1902
checks, 0 functional failures; 18 casts with the engine's own gate warn in the engine model), included in the full and `--capital`
runs. Before that (fourth implementer, the final review's items 1–6, "Final review fixes" below): `--lane` 1122 checks, 0 failures;
14 casts and the parked cast's key-off baseline. Before that (third
implementer, the two reviews' items 1–10, "Review fixes" below): `--lane` 1114 checks, 0 failures. Before that (second implementer): `--lane` 830 checks, 0 failures;
the arrival fan (§6.7) is deferred to a later stage by the orchestrator (not implemented). Code: `src/proxy/ship_gate_ctl_core.h` (lane constants, geometry and laws after
`line_of`; `Plan::lane` rollout; `lane_frame`, `lane_site_b` after `sb_frame`; `lane_steer` before `sb_steer`, which dispatches to it
on a lane gate), `src/proxy/ship_steering.cpp` (key, f+0x240 rewrite, site-B lane write, P2 bound, rows, counters), config schema and
template, fixture `verification/probe/ship_steering_host.cpp` (`namespace lanec`, mode `--lane`), results
`verification/results/ship-steering/convoy-gate-transit/scenario_c.{py,txt}`, ledger
[ship-steering.md](../verification/ship-steering.md) "Lane stage 1". Stage B is untouched under the key off (`Board::lane` = 0): the
petal code still exists and runs only without the key; key-off stdout of the full, `--capital`, `X3M_SEQ_OFF=1` and `--b9` runs is
identical to the pre-lane build apart from host timing fields (the full and `--capital` runs end with scenario C; `X3M_LANE_OFF=1`
skips it for that comparison, `keyoff_diff.py`).

### Deviations and decisions (each measured in scenario C)

* **L_i depth** = d_end − 0.75 r_gate (§6.4 says "2.3 km + 0.75 r_gate", which would end order 4 at 2.3 km + 1.5 r_gate); order 4's
  0.75 r_gate test then fires at d_end (P2 depth error 1–20 m when no script gap intervenes).
* **g_req = 500 m** (design 400 m): at 400 m the sweep's own margin at its 30 s horizon (50 m + 10 m/s × 30 s + offset terms,
  360–410 m) leaves a follower at exactly g_req unresolved and site E passes the pair (partner block: 4 episodes at 400 m, 2 at 500 m).
* **Merge and follow laws on the settling gap**: gap + (v_P − v_Y)|v_P − v_Y|/2a + the join loss still to come, the yielder's less
  its predecessor's (`lane_join_loss`: y·tan(θ/2) at the carrot's own angle θ; the first version used 0.176 y, a 20° join, for the
  yielder alone). Without the speed term the yielder re-accelerates late and the gap overshoots for good (both ships share the 100 m/s
  cap); without the join term the lateral join after the merge loses y·tan(θ/2) of along-track distance that is never recovered.
  The two-sided, angle-true term: abreast 1+4 tf 1 224.2–224.9 → 220.8–221.5 s.
* **τ_m = 4 s** (design 8 s): a level M1 (a = 5 m/s²) behind a Titan opens 2.37 km in no less than ≈ 44 s (decelerate 18 s, 8 s at
  the floor, accelerate 18 s) **[i]**; τ_m 4 s merges in 42.7 s, 8 s in 50.3 s, spans equal **[m]**. §6.11 item 2's 40 s is below that
  limit; the fixture's bound is max(40 s, the yielder's bang-bang minimum for the opening at the merge start + 5 s).
* (superseded: Lane fix round 1 item 1, the tangent join) **Join angle** 20° (kLaneJoinK 2.75), steeper when needed so it completes
  1.5 km before d_end, at most 45°.
* **A merge behind a still-merging ship** starts (the 40 s bound, the merge row) when the ship ahead settles.
* **Holding yielder lateral**: closes to w_P + w_Y + 300 m (never into the passer's footprint) while gap < g_req − 100 m, onto Λ
  after; merge point (`lane_on`) = within 100 m of Λ with the nose within 3°.
* **ℓ_c** tracks the members' middle at ≤ 5 m/s and lies ≥ 0.75 r_gate + 100 m below the axis; members that entered as escorts fly
  ℓ_c to d_end (L_i on ℓ_c) and order 5's Λ5 takes them to Q: an escort's order 4 arrives near d_end, its first tick tests the KC grid
  point before the rewrite takes effect (ship-navigation-ai.md §10.12 item 2), and an M7 final lies inside slot 0's grid sphere at
  3.2–4.2 km depth (measured before this rule: P2 at 3.85 km and a 23 s order-5 leg).
* **Escort gate** (review item 1, superseding "at d_end no earlier than t_L + λ_max", which let an escort pass d_end when the script's
  latency exceeded λ_max): an escort without its order 4 always flies the braking reserve to d_end (`lane_reserve`: the speed from which it
  stops 50 m short, keeping one frame step of travel before the braking), through a crawl at the 10 m/s floor as long as the time left
  until its leader's launch + λ_max + the step + 2 s allows (at most 200 m); no pacing on a predicted moment. λ_max = 17 s (the measured
  +4.1…+16.9 s) is only where the crawl runs out and the stop binds, and the order-0 hold's horizon. The reservation is set at the
  leader's order 5 as well (a SETA step can hide the 0.9 s of order 8 from every frame).
* **Launching ship's rollout under the lane**: straight on along +F at its speed (order 8's 3 km/s inflated every follower's sweep
  margin to a conflict; a frozen ghost at the trigger made followers stop).
* **Head predecessor**: the rearmost committed member at the hand-over / order 5 / order 8 (ahead on the same final).
* **Front ship first**: `decide_order` puts each lane gate's members first, in lane order; a lane yielder's candidate is swept against
  everyone except the members behind it in the lane order (their rollouts follow its old plan; they re-plan at their next decision,
  their live follow law reacts in the same frame).
* **Re-departure**: out along −F 1 km below Λ to d_end + 3 km, then a turn up onto Λ (carrot 1 km ahead and above), then the join; the
  first version (out along Λ itself) flew head-on into the inbound trail.
* **P2**: a lane member on its final (on Λ or converging) is the funnel's equivalent; the coast bound is replaced by site B's hold and
  the refusal's brake bound (`lane_p2_bound`). P4's one order-0 frame rolls out as the launch.
* **Scenario C KC model**: P2 → 0x1f in one frame (§10.12 item 5), not scenario T's 2–3 s; the escort's order 4 is committed in the
  frame the script issues it (its handler runs in the same sector update).
* (superseded: Review fixes item 7) **Assertion 4/8 bounds**: per consecutive launch pair c = (L_P + L_Y + g_req)/v_lane + max(0, T_slot,Y − T_slot,P) (a smaller hull's
  trigger sphere is smaller, its order-5 leg longer); the formed span bound is the leader's 15 km → launch + Σc + 10 s.
* **Player**: only the flown ship (`kFlagPlayerObject`) is excluded; no owner read (user decision 2026-10-14, supersedes §6.8's
  player-owned clause and §8 item 8's owner guard).
* **The lost-leader wait's rollout**: an escort on order 0 with the reservation rolls out the lane plan (site B holds it) for the whole
  wait, not only kGapMs; where the hold does not apply it brakes over the whole horizon. Cause of the partner-block episodes: both
  escorts on order 0 rolled out `brake`, the M1 ahead braking harder than the Titan behind; the pair was unresolved (`noyield`).
* **The ship at the gate ahead of the lane**: a lane member yields to a committed member past d_end (hand-over, order 5, order 8),
  and `decide_order` decides those first, front first. Cause of the re-departure episodes: the re-departer on order 8 and the head
  in its order-0 gap had equal stage-A rank, the launching ship was named yielder and could not yield (`noyield`); the head's rollout
  read the predecessor's samples of the previous decision.
* **No in-gap cap keep for a lane plan**: B.9's rule (a cap the order-0 gap's rollout cleared stays in force) does not apply to the
  lane rollout, which flies its law, not the cap; the head carried a 10 m/s cap after its predecessor launched.
* **Pacing on the path**: the head's pacing and the escort gate divide the straight join path (√(dist² + y²)), not the along-F
  distance; a joining escort was paced at 83–99 m/s for 180 s (crossing tf 1 span 351–357 → 330 s).
* (superseded: Review fixes item 5) **Order-0 hold on the join line** (`lane_hold_o0`): besides the nose within 8° of F, a member on its join line (nose within 45°,
  closing on Λ) is held at the highest speed ≤ v_sb whose straight run over the hold's remaining horizon (the lost-leader wait to
  t_L + λ_max, else the re-issue gap) stays ≥ 100 m off Λ (the clearance band while level) and ends before its join end. On order 0
  the engine consumes the last leaf's turn remainder, then flies straight (ship-navigation-ai.md §10.11 Q4); the join carrot is a
  straight line. The rollout flies the same cap. Cause: abreast escorts on 34° joins braked to 0–5 m/s in the lost-leader wait and
  the 0x14 re-issue gaps (abreast 1+4 tf 1 232 → 222 s). The hold tests the speed it writes (a test on the live speed flipped every
  frame as the ship braked).
* **An escort's join completes λ_max × v_lane earlier** (`lane_join_end`, `X3M_LANE_ESC_JOIN_K` = 1): its lost-leader coast starts on
  Λ. Cause: a crossing escort joining at 35° reached P2 75–100 m off Λ with the nose beyond 8.1° (P2 not kept, tf 6/10). Measured on
  the final build: k 0 → 6 failures (short final, a span), 0.5 → 4 (spans, order), 1 → 0, 1.5 → 2, 2 → 2; abreast 1+4 at tf 6
  206.4–209.7 s with k 0, 205.6–205.8 s with k 1.
* **`Plan::follow`'s leader liveness** is signed as the lane rollout's (orchestrator; key-off output unchanged).
* **Fixture bounds** (orchestrator-accepted earlier: assertions 4 and 8 add the follower's order-5 leg; 0x12 episodes between board
  members after the first 2 s). New:
  * (superseded by Review fixes items 1 and 7) assertion 4/8, an escort right behind its own leader: the pair's cadence is max(c,
    λ_max + T_slot,Y). The escort gate (§6.5)
    holds it short of d_end to t_L + λ_max; §6.5 assumed T_slot ≈ 14 s so that λ ≤ c − T_slot, measured T_slot is 17–21 s at tf 1,
    so the gate binds on every leader→escort pair at tf 1 (gap 29–31 s against c 23–26 s; formed M7 31.0 s against c + 5 = 30.4 s),
    never under SETA (T_slot 1.4–10 s). Stage 2 (λ ≤ 1 s, then a smaller λ_max) or a shorter order-5 leg removes it; λ_max 6 s
    fails 18 checks (escorts at d_end before order 4);
  * assertion 2: the merge bound above;
  * assertion 6: the P2 depth tolerance uses the larger of this and the previous frame's step (order 4's test sees the position after
    the previous step's move; a 1.5–2 s SETA step put P2 113–193 m past d_end);
  * (superseded by Review fixes item 7) assertion 5: the order-0 speed loss is taken against the lowest written speed of the episode (the follow law's own dip,
    93.8 → 85.7 m/s, then 100 after the predecessor's launch, counted as an 8 m/s loss);
  * assertion 8's lane floor: each ship no earlier than its straight line from its 15 km point to Λ's d_end point at 100 m/s allows,
    relative to the leader's (the crossing convoy's leader enters the 15 km sphere 6.6 km off Λ: ≈ 5 s).
* **Not implemented**: the arrival fan (§6.7) and `arr_capital_unseparated` (deferred to a later stage by the orchestrator); the
  cross-board certificate (§6.6, a later item).

### Review fixes (third implementer, 2026-10-14; each with a check that fails on the reviewed HEAD 7fb54627)

Evidence under `verification/results/ship-steering/convoy-gate-transit/`: `review_head/run.sh` → `run.txt` (items 3–6 built on the
HEAD core and on the tree), `escort_gap_cost.py` → `escort_gap_cost.txt` (item 1), `keyoff_diff.txt`, `scenario_c.txt`,
`ship_steering_host_lane.txt`.

1. **Escort gate safe for any latency** (above). The fixture draws λ from U(4.1, 16.9) s. HEAD with that draw: 36 failures of 830 and
   1959 frames of escorts past d_end without order 4 in the seed-1 runs **[m]**; now 0 such frames in 126 runs **[m]**. Cost
   (`escort_gap_cost.txt`, the casts whose escorts trail their leader) **[m]**: the leader → escort launch gap over the hull spacing,
   each run's largest, median 11.4 s at tf 1 (2.5–19.6), 3.0 s at tf 6 (0.5–15.6), 1.6 s at tf 10 (−1.1–6.6); leader → escort gaps
   28.6–32.6 s for λ < 9 s and 37.5–45.7 s for λ ≥ 9 s at tf 1 (spacing ≈ 26 s). At tf 1 an M1 escort (5 m/s²) meets the reserve
   before its leader launches (its d_end arrival ≈ 6.6 s after the launch, the reserve from 100 m/s 1 km long) **[i]**; stage 2's
   λ ≤ 1 s would remove most of the cost.
2. **All 20 KC grid slots.** No lane lateral clears them: within 2.2 km of the axis the best clearance from the 20 grid points is 719 m
   against the 829 m sphere (rows 666 m apart, sphere diameter 1658 m; a unit) **[m]**, and a lane below the grid (−2.93 km) cannot blend
   to a final within 2 km. Instead the **tick deferral** (`lane_defer_step`, site B): on a lane escort's order-0 frame in its lost-leader
   wait site B adds 2.5 s + the step to `f+0x210`, the re-plan stamp order 0's tail has just set to the engine's now; on the next order's
   first frame (its handler skipped the tick and jumped to site B) site B rewrites a grid point in `f+0x240` to L_i and puts `f+0x210`
   back, so the order's first tick tests L_i one frame later (ship-navigation-ai.md §10.11 Q4; §10.12 item 2: the SA issue does not
   write `f+0x210`). A new engine write (`sites::kFlightReplan`); (superseded: Final review fixes item 2, the base and the restore are
   the game clock) clock-free (only the engine's own stamp plus a difference). The
   fixture's warp-in slots follow the KC (the first empty slot whose 60 s release stamp has passed) with 0 / 5 / 10 slots held by other
   traffic per seed (slots up to 16 handed out, slot 5 on the axis column), and the issue frame's tick tests the KC point: without the
   deferral (`X3M_C_NODEFER=1`, HEAD's behaviour) 15 failures (P2 at the grid depth), with it 0 **[m]**.
3. (superseded: Final review fixes item 1) **A strict lane order.** One score per member, the ETA to d_end at the common 100 m/s (on Λ the position); from the previous order,
   adjacent swaps by score with the 5 s hysteresis, the 2 s tie band (lower key first) and strict position on Λ (each swap settles one
   pair: at most n(n−1)/2); then the leader-first move, each escort at most once (O(n²); HEAD O(n³)). Unit: A (v_max 60) on Λ 7.7 km
   from d_end, B (100) 2.5 km behind, joiner C between them at the common speed: HEAD ranks C, A, B and A merges at 10 m/s; now A, C, B
   with A at its own 60 m/s, whatever the slot order **[m]**. Cast `slow_hull` (a 60 m/s Titan ahead of a 1+1 convoy and an M7 joiner):
   green. The old unit "the tie band and the leader first" put the escort 1 km ahead of its leader, where the foreign capital's ETA is
   6 s earlier than the leader's and now rightly goes first; the unit now places the leader 1 km ahead.
4. **Points after a load or a slot reset.** Without a lane member, a field equal to the hull's own L_i (its Q from its hull and the gate
   frame, `lane_point_own`) is adopted: the order's geometry is the KC slot-0 grid point and the ship commits on the lane
   (`ctl_point_adopts`, row `site=adopt`). `lane_point_ours` matches by the validator's tolerances (depth within 10 %, lateral within
   20 m of Q or of l_c), not the live l_c within 1 m: an escort's L_i read back after a 15 m l_c shift is ours (HEAD: not) **[m]**. A
   lane member's field that is neither a grid point nor ours counts `ctl_grid_miss` and rows `site=miss`, once per value. The adoption
   reads the type's last width (`type_width_get`); a type never width-read since the load gives w = r and may miss (then counted).
5. **The rollout flies what the engine does.** On an order-0 hold the plant coasts straight for the hold's horizon (HEAD steered it to
   the join carrot: 94 m off the straight line within 10 s; now 0) **[m]**; Λ's lateral follows the l_c → Q blend in the plane through
   the blend's end (HEAD kept the member's present lateral: 0 % of the way to Q at d_end; now 48 %, the pursuit's lag as the live leaf)
   **[m]**. The hold itself (found by item 7's assertion): with the nose within 45° of F the straight run may cross Λ by up to
   `kLaneDriftM` = 300 m (the 8° rule drifts 237 m over λ_max); the clearance band applies beside the predecessor only, not behind it on
   Λ; the lost-leader horizon starts at the leader's order 8 (a 0x14 re-issue gap before it keeps kGapMs). A residual past the join's end
   aims at Λ's d_end point (HEAD's 45° carrot turned an M7 9° off F at P2 under SETA).
6. **Merges complete on the raw gap.** The speed law settles (merge → follow, the lateral released onto Λ) only once the yielder is
   behind (raw gap ≥ 0) and the settling gap is g_req − 50 m; while merging, the join loss is credited up to g_req − 50 m. The merge's
   completion (the finish row, `merges_done`, the merge bound, the certificate window) is the raw gap ≥ g_req − 50 m (the follow law
   settles a few metres short of g_req at the shared cap). HEAD settled at a raw gap of −85 m on the speed term of a yielder at the
   floor **[m]**. The fixture's lateral check reads positions and hulls: while the raw gap is < 0 the separation never drops more than
   100 m below min(start, the clearance band): 0 in 126 runs **[m]**. Measured and rejected: completion and lateral release both on the
   raw gap ≥ g_req − 100 m (§6.3 literally) held escorts at the band to d_end (175 failures); a band hold while beside after the law
   settled deadlocked at a raw gap of −87 m. Cost: a yielder starting further out than its credited loss ends that far back (the 1+4's
   outer M7 at 5 km: ≈ 4 s); merge completions take up to 147 s (`slow_hull`, the join flown at 60 m/s) **[m]**.
7. **Fixture bounds.** Assertion 5 compares every order-0 / 0x1f frame of a trail member (nose within 45° of F; the hand-over excluded)
   with the law's speed site B read, frames written 0 included (on the item-1 core before the hold fix: 70 failing runs, drops up to
   100 m/s) **[m]**. Assertions 4/8, pair by pair, with no term from the run's own T_slot: c = (L_P + L_Y + g_req)/v_lane + the follower's
   longer order-5 leg from d_end at the lane speed; an escort behind its launched leader max(c, t_L − t_prev + λ_drawn + T_slot,ref 21 s
   + 2 s + its reserve recovery (v_lane − 10)²/(2 a v_lane)); behind an escort that flew the reserve, + that escort's recovery; a merge's
   yielder + its uncredited join loss (item 6). `escort_o4_late` compares with the escort's own draw (HEAD compared with the distribution's
   maximum and could not fire). New casts: `parked` (an M7 parked on Λ 7 km out: only the launches, the passer and safety are asserted;
   the convoy detours 185–191° under the engine's warn, P2 not kept, capsule ≥ 465 m), `leader_killed` (the leader destroyed 2 km before
   d_end: the escorts transit as lane members), `slow_hull`, `small_escort`. The brief stop's bound is T_stop + the step + 1 s (the cap's
   lifetime). (Parked: Final review fixes item 5 adds the key-off baseline.) Deferred: 144 fps and 12 seeds (§6.11): the scenario's host time (126 runs, ≈ 25 s) grows with each factor and the 60 fps
   and SETA steps already cover the frame-step paths; a later checkpoint.
8. **Clocks and cost.** The refusal at the hand-over is a signed game-time bound (kLaneRefuseMs + the step); `decide_order` flags its
   lane entries (HEAD scanned the list per member, O(96²)). Bench, the worst frame of 3 runs, the check on each run's third-worst
   (single host-noise frames reached 1.3–1.5 ms in repeated runs) (superseded: Final review fixes item 6; these figures did not match
   the committed files) **[m]**: 1+4 lane 2.2 µs mean, 58–68 µs worst at tf 1, 22.7–22.9 / 66–76 µs at tf 10; 24 members (six 1+3)
   10.6–10.7 µs / 163–1301 µs at tf 1, 131.5–139.4 / 558–1519 µs at tf 10, third-worst ≤ 474 µs. Under a budget cut (`ctl_budget_cut`, 1 ms) a pair's verdict stamp may age past the 250 ms refresh before its members are
   decided again; the certificate's freshness bound then passes the pair to the engine rather than certify stale samples **[i]**.
9. **Flight rows.** `ship_gate_ctl_lane` gains `lat_m lc_m v_law v_w w=none|stop|kept|hold on merged`; new `ship_gate_ctl_rewrite`
   (`site=a|b|adopt|miss slot from_m to_m`); `ship_steering_lane_summary` gains `ctl_grid_miss ctl_point_adopts ctl_lane_defers
   ctl_defer_rewrites`; `ship_trace_rows.py` and its test extended.
10. **A small escort's final.** Cast `small_escort` (an r 500 m escort; l_max window 121–688 m against l_c 929 m below the axis): Λ5
    brings it inside its trigger sphere before the plane in all 9 runs, no fix needed **[m]**.

### Final review fixes (fourth implementer, 2026-10-14; each with a check that fails on the reviewed 69d5b2ee)

Evidence under `verification/results/ship-steering/convoy-gate-transit/`: `final_head/run.sh` → `run.txt` (items 1, 2 and 4 built on
the 69d5b2ee core and on the tree), `ship_steering_host_lane.txt` (units, the parked baseline, the bench), `keyoff_diff.txt`.

1. **A strict lane order every frame** (supersedes Review fixes item 3's swap pass). The members on Λ are ordered by position (the
   exact score; no hysteresis ever reorders two of them); the joiners by a sticky score (`lane_ord_ms`: the absolute moment of the
   score, re-taken when the live one drifts more than kLaneKeepS / 2 = 2.5 s from it, so two joiners keep their order unless their
   ETAs differ by up to 5 s), within a tie cluster (members within 2 s of the cluster's first) the lower key first; the two chains are
   merged (a joiner before an on-lane ship when its score is earlier beyond the tie band or, within it, its key is lower); then the
   leader-first move. Ties fall to the key, then the ship: a total order, independent of the slot order. Cost: insertion sorts, the
   clusters and the leader move, O(n²) comparisons worst case (n ≤ 24; O(n) on an ordered lane); the previous swap pass was O(n³)
   worst case (each of up to n(n−1)/2 swaps could take its own O(n) pass). The sticky moments are cleared at a clock reset. HEAD's
   probe (previous order C, B, A; A and C on Λ, A 300 m ahead; B joining between them by ETA): C, B, A with A merging at 10 m/s behind
   B; now A, B, C with A at its plan speed and C behind B **[m]**. 400 random boards of 8 members with a random previous order: 23
   on-lane pairs ranked against their positions on HEAD, 0 now, no rank missing **[m]** (fixture units and `final1.cpp`). The one
   exception kept: an escort on Λ ahead of its own leader yields to it (§6.2). `slow_hull` green.
2. **The tick deferral on the game clock** (ship-navigation-ai.md §10.13 items 3, 4 and 6; supersedes Review fixes item 2's "the
   engine's own stamp plus a difference"). `lane_defer_step` takes `now` from `lane_clamp` (`game_time_ms()`, the clock the order-0
   tail and the handlers read): every order-0 frame writes `f+0x210 = now + 2.5 s + step`, the next order's first frame restores `now`,
   and both clock-reset branches (`sb_frame`, `lane_frame`) clear `defer_w`. Three order-0 frames without the tail (another order
   ended into order 0 for site B, the stamp still ours): HEAD stacked 7533 ms ahead of the last frame and its late restore wrote a
   stamp 4934 ms in the future; now 2533 ms (one deferral) and the restore writes now **[m]** (unit, `final2.cpp`). A lost restore
   (the next order's first frame never reaches site B's lane write) leaves `f+0x210` at most one deferral, 2.5 s + the step, after the
   last order-0 frame: the order's first tick comes at most that late (the ship coasts at the last written speed), then ticks on the
   KC's point as before the deferral existed; a later site-B frame that finds the stamp changed drops the deferral without a write.
3. **Site B's rewrite recorded.** The deferred order-4 rewrite stores the written field and the grid point it replaced in the ship's
   gate state (`lane_w`, `lane_grid`), as site A's rewrite does, beginning the slot's state for this ship and gate when needed: site A
   finds the field ours directly (no tolerance match) and keeps the replaced slot's geometry (HEAD: slot 0's after a tolerance match).
   Check: `test_ship_steering.ShipSteeringCore.test_site_b_rewrite_recorded` (the three stores absent on HEAD) **[m]**.
4. **The adoption memo and an escort's point after a load.** A capital on order 4 with no lane member reaches the adoption on every
   call; a negative result is memoised per field value, the type-width table's generation and whether the board had an l_c
   (`gc::lane_adopt_memo`: one compare, no width lookup, hull, plant or `lane_q`; a positive is never memoised) **[m]** (unit: 1
   evaluation in 10 calls, re-evaluated on each key change). With no board l_c (a load) an escort's L_i on l_c is now recognised by
   `lane_point_lc_env`: L_i's depth (within 10 %, never the grid depth) and a lateral inside the disc at least 0.75 r_gate + 100 m − 20 m
   below the axis (l_c's clamp); HEAD adopted only the hull's own Q, so an escort's point after a load was a miss **[m]** (`final4.cpp`).
   A non-lane field at L_i's depth inside that window would be adopted as ours (the KC writes grid points at 2 (r_gate + r_s)) **[i]**.
5. **The parked cast's key-off baseline.** The same cast under stage B (seed 1, `scenario C parked baseline` rows): stage B turns
   874–994° from the 15 km point (holding laps excluded), reaches 4.9 km off the axis and launches 0 of 3 within the cast's 1000 s;
   the lane run turns 187–193° from 15 km, 1.9–2.0 km off the axis, and launches all 3 **[m]**. The turn-around is not lane-caused:
   the module's obstacle avoidance (`avoid_adjust`) holds a detour around the parked M7 from 36 s to 116 s (the leader's nose 45° off
   F at 60–80 s) while the lane law only keeps 100 m/s **[m]** (seed 1, tf 1: `parked_detour.sh` → `parked_detour.txt`); it runs
   under either key. A lane-aware detour or
   eviction for non-members stays open (below). The cast keeps its launches, passer and safety assertions; the key-off run asserts
   safety (0 episodes, 0 uncertified frames, capsule > 0).
6. **Bench figures and the ceiling** (supersedes Review fixes item 8's bench sentence, which did not match its files). From
   the committed outputs (`bench_figures.py` → `bench_figures.txt`; each row 3 runs per key: the worst frame of the 3, and the largest
   of the 3 runs' third-worst frames, the checked figure; host x86-64 -O2, the frame block = `lane_frame` + the leaves + the decisions)
   **[m]**: 24 members (six 1+3), the three design-build rows (`ship_steering_host_lane.txt`, `_capital.txt`, `../ship_steering_host.txt`):
   tf 1 10.6–10.7 µs mean, worst 136–149 µs, third-worst 108–142 µs; tf 10 131.5–132.0 µs mean, worst 551–807 µs, third-worst
   450–480 µs. Over all 14 rows (with `scenario_c.txt`'s 11 constant builds): tf 1 worst 134–1805 µs, third-worst 108–175 µs; tf 10
   worst 492–1083 µs, third-worst 436–519 µs. The 1+4 cast: tf 1 2.2–2.3 µs / 64–124 / 55–75 µs, tf 10 22.9–23.1 µs / 50–71 / 46–50 µs
   (design rows). **The ceiling's margin at tf 10**: a 24-member frame (the budget ⌈24 × 4/s × 333 ms⌉ = 32 decisions covers every
   member once **[i]**) costs 551–807 µs at its worst on this host, 193–449 µs under the 1 ms ceiling; one row of 14 reached 1083 µs
   and tf 1 rows single frames of up to 1805 µs (host noise: the third-worst stays ≤ 519 µs); i686 under FEX and native 32-bit
   Windows are unmeasured (the first flight's `ctl_sb_us_max`). **What the ceiling cuts** (`ctl_frame`, `ctl_budget_cut`): only the
   decisions after the first of the frame, in `decide_order`'s order (the funnel's occupants and the lane gates' members first);
   `sb_frame_all` (with `lane_frame`), the live proof and the leaves run before the ceiling's clock starts and are never cut. A cut
   member keeps its last plan and verdicts until its next decision (the certificate's freshness bound then passes a stale pair to the
   engine, Review fixes item 8) **[i]**.
   §1.2 of ship-navigation-ai.md now marks the 15 SA cases that write `f+0x210` at issue (§10.13); it listed 0x1c and 0x119 only.

### Lane fix round 1 (Run173; fifth implementer, 2026-10-15; each item with a check that fails on dd07e95e)

Run173 (one convoy, M2 leader, M1 and M2 escorts, SETA share 1.0) measured a span of 785 game s against 185–235 s: the join flew every
ship ~20° off the axis from 45–76 km, the engine's own 0x12 against the gate fired 6.85–6.98 km out (6 evictions, turn-aways up to
172°), E1 overshot the plane and was ranked behind E2, and site B held 12.2 m/s through a 116 s refusal after a P2 that did not keep
(`verification/results/ship-steering/run173-triage/`). Evidence under `verification/results/ship-steering/convoy-gate-transit/`:
`round1_head/run.sh` → `run.txt` (the round-1 fixture on the dd07e95e core: 165 failures of 1896 checks; the round-1 units need the
new core and are left out of that build), `round1_head/keyoff.sh` → `keyoff_diff.txt`, `scenario_c.py` → `scenario_c.txt`,
`ship_steering_host_lane.txt`.

**The engine model** (fixture, all casts): the engine's warn against the gate, `gate_warn_cone` (ship-navigation-ai.md §3.1: the
reach b + L + 2 (r_A + r_gate), the cone of the gate's sphere inflated by 40 m from the braking point; class 18 takes the cone, not
the mesh test), for a committed member on order 4, 0 or 0x1f before order 5 (the KC's ignore mode starts with order 5), with site E's
own-gate row as the DLL calls it (`gc::e_gate_row`); a run of unsuppressed frames counts as one own-gate 0x12 (counted, not flown).
On dd07e95e it reproduces the flight: cast `run173` own-gate 0x12 at 6.86–6.98 km (`gate_row=stage`), and check `7b` fails in all
17 asserted casts, 45 of their 51 cast × time-factor groups, on joiners at 6.81–6.98 km (`stage`) and at the hand-over 2.47 km out
(`guard`: the short final puts P2 inside the stage-B guard's sphere for an M2) **[m]** (`round1_head/run.txt`; distances from the seed-1
rows).

**A finding that orders the fixes:** a lane member warns against its own gate at ~7 km whatever its join. Flying parallel to F on Λ
(0.93–1.0 km below the axis) at d = 6.9 km its heading is sin θ = ℓ / d ≈ 0.14 off the gate's centre against the cone's
(r_A + r_gate + 40 m) / (d − b) ≈ 0.35, inside the reach of 7.09 km (Titan) / 6.88 km (M1) **[i]** (the §3.1 formula) **[m]** (the
fixture: every lane member's approach shows the warn). The join of item 1 does not remove the 0x12 by itself; the lane row of item 2
does. Item 1 removes the 20° approach and puts the members on Λ (stage funnel) well before the warn's reach.

1. **The join is a tangent onto Λ** (`lane_join_span`, `lane_carrot`; the rollout flies the same function). The reference path from
   the ship (s, y) is the parabola y_ref(s') = y ((s_m − s') / (s_m − s))², tangent to Λ at its merge point s_m; the carrot lies on it
   a pursuit look-ahead ahead (`pursuit_look`: max(0.75 v, 2 v / W, 0.3 r_gate), sized by the hull's turn radius and speed). The span
   D = s_m − s needs D ≥ 2 y / tan 30° (the steepest angle, at the start, 30°) and D ≥ √(2 y R) (the vertex within the turn radius
   R = v / W). s_m is Λ's far end (15 km) where that fits, else 8 km, else the join's end with the room it leaves (at most 45° at the
   start), past it the room to d_end. The parabola through any later point of itself with the same s_m is the same curve, so the
   stateless rule re-planned every frame flies one path; a ship far out and wide starts on the join's end and moves to 8 or 15 km
   once that fits (a step of a few degrees). `lane_join_loss` is the parabola's arc length less its span (y (m/3 − m³/20 + m⁵/56),
   m = 2 y / D). Measured **[m]**: cast `run173` (Run173's ships from their first rows, 45–76 km out at ~20°): the nose off F at the
   7 km crossing 0.4–1.0° (dd07e95e: 22.5°; check `1b`, ≤ 5°); every ship on Λ before the warn's reach, 0 own-gate 0x12 with item 2.
   The l_c → Q blend now ends the hull's settling distance (`lane_blend_settle`, the pursuit look-ahead at the lane speed: 1.1–1.3 km
   for M1/M2) before d_end (item 3).
2. **Site E's own-gate row for lane members** (`lane_gate_why` under `sb_gate_why`, `Board::lane` and the member's `ln`; key off:
   stage B's row unchanged). The gate's solid lies within its bounding sphere and within `kRingZR` = 0.66 r_gate of the plane
   (gate_hole.txt: the deepest front extent of the measured ring bodies 0.651 r; the installed ordinary gates have no collision body,
   §10.9): a point's clearance is the larger of |p − G| − r_gate and −s − 0.66 r_gate. Joining or on Λ (inbound / funnel; order 4 or the
   script's order-0 gap) at any distance: the segment from x to its Λ point at d_end clears by r_A + 0.25 r_gate (the stage-B guard's
   margin), and beyond 6 r_gate the member's rollout, fresh within the row's state bound, clears to d_end (else `roll`, a pass; for
   hulls up to 1.29 km every point before d_end clears the slab, so this reduces to the rollout's freshness). On its final past d_end
   (the hand-over, and an order-4 / order-0 frame before P2's tick): within 200 m of its final line with the nose within 8° of F, the
   lane geometry's own path to order 5 (the final's window certifies the ring's hole). Else the stage-B guard. The suppression
   reports `gate_row=lane` (`roll` for the rollout pass). Measured **[m]**: 0 own-gate 0x12 in every cast but `parked` (dd07e95e: all 17
   asserted casts), whose detoured ships reach d_end 26–27° off their final (P2 not kept) and are left to the engine by design (1–2
   episodes per run at 2.12–2.40 km, `why=guard`; the cast asserts launches, the passer and safety only). Units: 6.9 km out without a rollout
   `roll`, with a clear one `lane`, a stale one `roll`, key off `stage`; the hand-over on its final `lane`, 300 m off it `guard`.
3. **Site B's hand-over** (`lane_site_b`; `Member::p2_kept` written by the DLL's P2 hook and the fixture's P2). P2 kept: the live
   speed stands (the refusal's brake bound), as before. P2 not kept: never the live speed; the lane speed under the refusal's brake
   bound (`lane_refused_hold`) until the board flags the refusal (5 s + the step), then the brake to a stop at d_end (0 past it);
   `LaneWrite::unkept` (`w=unkept`). The blend ends before d_end (item 1's last line). `ctl_x_wait_s` accrues every frame from the
   refusal (`lane_x_wait`, the wait beyond 3 s, what is counted kept in `x_acc_ms`), the rest at order 5. Measured **[m]**: P2's nose
   ≤ 8° in every cast but `parked` (check `6b`; `scenario_c.txt`'s p2_nose_max); cast `p2_miss` (Run173's leader: P2 forced not kept
   with 12.2 m/s left, a refusal of 112–122 s unknown to the board; Run173: 116 s): 30 s into the refusal the leader's speed ≤ 1 m/s
   (dd07e95e: the latched
   12.2 m/s; check `3c`), `ctl_x_wait_s` at the grant within a frame of the wait beyond 3 s (dd07e95e: 0 until order 5; check `3d`).
   Units: kept 12.2 m/s, not kept 100 m/s (`unkept`), refused 0, refused 500 m before d_end the brake curve √(2 a 450 m); a 20 s
   refusal without order 5 accrues 16.8–17.0 s.
4. **The overshoot, the stop law, the re-commit hysteresis.** A lane member past the plane without launching (`Member::over`, until
   back on Λ) never ranks behind a member farther from the gate (`lane_frame`: moved before the first one, O(n) per overshoot; its own
   leader still first, §6.2). It turns back as a re-departure with its own geometry: the turn point d_end + its settling distance + R
   (R = v / W), the out-leg 2.5 R below Λ (the U-turn's measured lateral extent: it then ends on Λ; with 2 R it ended R / 2 above and
   P2 came 8.5° off F in 2 of 9 runs), the out-leg carrot R beyond the turn point; its follower's gap is taken to the U-turn's apex
   (`kLaneOverApexR` = 3 R beyond the turn point; measured 1.53–1.69 km for an M1, 2.4–2.7 R) and an overshoot turning back is a block
   (the follower stops at spacing behind the apex until it has turned in). The law's stop (`lane_law`) ends after T_stop + the step and
   releases to the floor unless the ship ahead is physically stopped or an overshoot turning back (a block: the design's stop at
   spacing); a stop episode (ending 2 s + the step after its condition) counts in `lane_stops` (`ctl_merge_stop`). Re-commit
   hysteresis: a ship that left on `Why::target` re-commits at most once per 2 s + the step (`note_target_leave`, `recommit_ok`; the
   DLL's `ctl_commit`, counted `ctl_recommit_held`). Measured **[m]**: cast `overshoot` (Run173's E1 state at f 18901, s = +703 m,
   3.8 km off the axis, the nose 80° off F; a Titan on Λ 11 km out): the overshoot is the head from its first lane frame, 0 frames
   ranked behind a ship farther from the gate (dd07e95e: ranked behind; check `4b`), its outermost s 5.74–5.90 km against the bound
   d_end + settle + 4 R = 6.12 km (dd07e95e: beyond; check `4c`), P2 2.4–5.8° kept, 0 episodes; the Titan waits 15.0–21.5 s (a
   block), all launched (9 runs). Law stops beyond T_stop + the step behind a ship that is not stopped: 0 in every cast (check `2b`). Units: the stop
   law 0, after T_stop + the step the floor (`merge`), a block 0; re-commit 1 s after a target leave refused, after 2 s + the step
   allowed, a member's refresh never held.
5. **Rows.** `ship_gate_ctl_defer` (per ship: `order`, `kind=defer|restore|rewrite`, `f210_read`, `f210_written`, `ahead_ms`; a
   deferral episode's first write, then one per ship per game second, every restore); `ship_avoid_engine_warn_pre ... gate_row=` (the
   own-gate row's reason of the first gate entry's last site E verdict; `EVerdictSeen::gate_row`); `ship_steering_lane_summary ...
   ctl_recommit_held= gates_gate:F:G:r=<gate>:<F>:<G>:<r_gate>|...` (each lane gate's frame); `ship_gate_ctl_lane w=unkept`.
   `ship_trace_rows.py` and `test_ship_trace_format` extended.

Cost **[m]** (host x86-64 -O2, `X3M_C_ONLY=none --lane`, the pre-change binary and the round-1 binary alternated twice on the same
host): bench24 mean 12.1–12.4 vs 12.6 µs at tf 1, 144–152 vs 150–161 µs at tf 10; worst and third-worst frames within the host's
noise (another process at 100 % CPU on the host: single frames of 1.0–11.8 ms in either binary). The bench check (third-worst under
1 ms) failed in the full, `X3M_SEQ_OFF=1` and `--lane` acceptance runs during that load and passed on rerun (ledger).

### Lane fix round 2 (sixth implementer, 2026-10-15; each item with a check that fails on 77f9e88e)

User-decided scope (items 1–3) and the independent review of round 1 (items 4–6). Evidence under
`verification/results/ship-steering/convoy-gate-transit/round2_head/` (`run.sh` → `run.txt`: the round-2 fixture built on the 77f9e88e
core with `-DX3M_LANE_HEAD`, which leaves out the lines that need the new core).

1. **Site E's lane obstacle row** (`gc::lane_obst_why`, `gc::e_obst_row`; the DLL's `ctl_e_obst`; `av::EVerdict::suppress_lane_obst`).
   The engine's own warn (ship-navigation-ai.md §3.1) fires on a lane member beside a station whose bounding sphere is far wider than
   its parts (the close-range rule d ≤ 1.2 (r_A + r_B) holds for any station), on asteroids its join passes (the cone), on a mid-size
   ship (an M7 beside an M2: the box test 0x0044d9c0), and on the order-0 frames of the script's gaps where A has no fresh decision
   (`pass_not_fresh`); the resolved set covers a detour only once the velocity has swung clear. The row suppresses that warn when the
   member's path as site A flies it clears the obstacle's bound by the sweep margin (kMarginM + k (t + the rollout's age); a moving
   ship's extrapolation charged k t again, its centre swept over the leg's time ± 0.5 s + 10 %):
   * **The path** (`gc::lane_path`): the live position and velocity (E's reads of A), the live velocity up to the board's rollout's first
     sample at or after now, the rollout. With a fresh avoidance detour (A's `ShipAvoid` call within the state bound, its new field
     `P`: the detour point) the turn site A's leaf flies onto it instead (`gc::lane_turn`: one frame step and a think tick straight, then
     every 0.5 s the heading turned toward P_d at the hull's W reached at alpha and braked on √(2 alpha angle), until P_d or its closest
     approach), P_d, then the rollout from P_d's lateral offset / tan 30° beyond it along F (the join's steepest angle). The horizon
     reaches at least 1 s + v / a (a shorter path runs on straight along its last leg): the engine's own warn looks no further than its
     braking distance, so when a conflict enters the horizon later the row passes and the engine's warn still has its room. **Its bound
     is the rollout's horizon, 30 s** (61 samples at 0.5 s): the row certifies clearance over those 30 s only; a conflict near its end
     refuses the suppression (`roll`) however early the warn comes (the `parked` cast, review fixes item 11).
   * **The bound** (`gc::ObstBound`): a station's part spheres from the walk's part cache when they lie within its live bounding sphere
     (else the sphere), an asteroid's sphere, a ship's hull capsule (the type widths; its velocity from its flight basis and slip). A's
     hull: its capsule along each leg (the leg extended by L − w, radius w).
   * **Fail closed**: not a lane member (`Board::lane`, `ln`), committed or escort; the player's ship or manual control (`[obj+0x40]`
   0x2 / 0x40000, or no autopilot bit); an order the lane does not fly (only 4 to its gate, 0, 0x1f, 0x14 / 0x15 to its leader); the
   live state or the rollout older than 1 s + the step + 100 ms or from the future (two-sided); the read out of the state's reach:
   `obst_row=no` (today's rows decide). A path that does not clear: `roll` (today's rows: the engine's warn as before).
   * **Rows**: `ship_avoid_e_verdict ... verdict=suppress_lane_obst obst_row=no|lane|roll`, `warn_pre ... obst_row=`, the lane summary's
   `e_lane_obst=` / `e_lane_obst_roll=`. B.5: E reads A and B (the engine's pointers of the call) and their flight and physics blocks,
   the type table, the module's part cache and A's avoidance state; no other object.
   * **The engine model** (fixture, every lane cast): the warn of a lane member against the world's statics and a parked ship on the
   orders CollisionWarn acts on (not 0x12, not order 5's ignore mode, not 8): the close-range rule, the box test for a station or a
   capital-type ship (0x0044d9c0, measured in ship-navigation-ai.md §11.2: per part an oriented box of half-extents 1.2 (e_i + r_A),
   hit with A inside it or within |v_A| + b of it on A's forward ray; modelled as the part's sphere within 1.2 (r_part + √3 r_A) +
   |v_A| + b, which holds the box's corners, without the ray's condition: review fixes item 11), the cone; site E's verdict as the DLL
   computes it (`c_obst_e`); an unsuppressed run of frames counts one 0x12 (counted, not flown). **The oracle**: the flown path over the
   30 s after each warn frame (the 0x12 is not flown, so it is the lane plan and its detours) against the row's capsule and margin: an
   eviction is *avoidable* when the flown path cleared by the margin + 200 m + the run's longest step at 100 m/s (the row flies one step
   straight before a turn); a suppression is *unsafe* when the flown path touched the bound.
   * **Casts**: `station` (a station 3 km beside Λ at 12 km, bounding sphere 2.5 km, four 300 m parts), `join_field` (an M1 joining
   from 5 km off Λ 30 km out past four asteroids 1.5 km beside its join), `join_block` (one asteroid dead ahead of that M1: its path runs
   through it until the detour), `m7_cross` (an M7 at 143 m/s crossing Λ 2.6 km ahead of the leader). Asserted in every obstacle cast
   (and `parked`): 0 avoidable evictions, 0 unsafe suppressions, no contact; `station` and `m7_cross` 0 evictions (with suppressions);
   `join_block` the path through the asteroid passes (row `roll`, a 0x12 counted). Measured **[m]** (seed 1, tf 1 / 6 / 10; the committed
   `ship_steering_host_lane.txt` of the review fixes): `station` 11,009 / 853 / 539 warns, all `lane`, 0 0x12, closest 2,121 m (77f9e88e:
   all passed, 3 0x12 from 4.2 km, 3 avoidable); `m7_cross` 1,684 / 137 / 80 warns with the box's corners (19 / 2 / 1 without), all
   `lane` (77f9e88e: a 0x12 at 2.6 km, avoidable); `join_field` tf 1: 1,857 warns, 970 `lane` (all on a detour),
   887 `roll`, 3 0x12 at 1.09–1.10 km where the flown path came within 170 m of the asteroid's surface (slack −91..+33 m against the
   margin: not avoidable; 77f9e88e: 1 avoidable); `join_block` 1,351 warns, 1,062 `lane`, 289 `roll`, 2 0x12; `parked` 12,898 / 1,024 /
   637 warns against the parked M7 with the box's corners (0 / 135 / 75 without, all `lane`), 2 / 2 / 3 avoidable 0x12 (1–3 per run over
   9 runs, bounded there at 3: review fixes item 11). 0 unsafe in every run, 0 avoidable in every other cast (`--lane` 3,344 checks, 0
   failures). Units: a station's
   parts 3 km beside Λ `lane`, its bounding sphere over Λ `roll`; an asteroid 4 km ahead `roll`, its detour 2.6 km up `lane`, the same
   detour onto a second asteroid `roll`; a stale or future rollout `roll`; manual, 0x12, order 5, the key off `no`; order 0 `lane`; an M7
   crossing Λ 2 km ahead when the member gets there `roll`, crossed behind it `lane`. Cost **[m]** (host x86-64 -O2): 2.3 µs per call (a
   station's four parts over the 61-sample rollout, or a detour turn), only on the engine's warn calls of a lane member.


2. **An escort handed to another leader leaves the lane as a member** (Run167/168 `why=leader`; `Member::exit_ms`, `exit_tgt`,
   `exit_clear`, `kLaneExitMs` 60 s; `validity`, `lane_frame`, `lane_steer`'s `SbSteer::keep_point`, `plan_of`). On 77f9e88e the board
   dropped the escort at the hand: its sphere avoidance then met its own convoy ahead (no longer a certified pair) and dived 2.2 km below
   Λ, so its turn to the new leader came to 105–119° beyond the bearing, a loop (cast `retarget`). Now the escort stays a member while it
   leaves: validity keeps it on 0x14 / 0x15 to the new target until it is clear of Λ's footprint (its lateral ≥ kLaneOffM + w),
   kLaneExitMs + the step after the hand, or its target changes again (key off: it leaves at once, as before); lane_frame keeps its place in
   the order (its moment frozen at the hand), so the ship behind keeps following it, and its laws run as before; site A flies the engine's
   leg to the new leader (lane_steer returns only the law's speed, `keep_point`; the DLL's and the fixture's callers keep the order's point);
   its rollout drifts at that speed (`Plan::drift`), so its pairs keep their certificates and no sphere avoidance acts on its convoy.
   Measured **[m]** (cast `retarget`: the formed convoy's M1, 11–12 km out, handed at 70 s to a Titan 20 km away flying out at 40 m/s; 9
   runs): it leaves the lane 43.2–44.3 s after the hand, its lowest speed on 0x14 86.0–90.0 m/s, its turn 2.8–3.4° beyond the 42.8°
   bearing, it closes to 2.86 km of the new leader (its formation slot), 0 member 0x12 episodes; its follower, one follow-law time
   constant + the step after the leave, follows the ship ahead and its law's speed is not below its speed at the leave. Units: the hand
   kept (not yet stamped, stamped and not clear), released when clear, past the bound, handed again, key off; lane_steer speed only.
3. / 5. **The overshoot, the follow gap, the release** (items 3a, 3b, 3d with review 2, 3, 8, 9; partly: see Open). Core changes, all
   green on the existing casts:
   * **A real overshoot only** (review 3; `Member::front_seen`): `over` needs the member seen in front of the plane since it entered (an
     evicted member's slot keeps it 30 s, so Run173's E1, which crossed the plane as a member before its 0x12, is one); a ship that
     commits from behind the gate is a re-departure, not an overshoot ranked as the head. The fixture's `overshoot` cast marks its ship
     as seen in front (E1's history).
   * **The return zone** (3a, review 2): when an overshoot begins (`over_new`), the members inside its return zone, nearer the gate than
     its U-turn's apex less g_req, both half-lengths and that member's own stop, are stamped `over_ahead_of` and stay ahead of it in the
     lane order (decided once: a member braking toward the apex would otherwise flip in and out); the others rank behind it as before. An
     overshoot behind such a member runs its law from its apex (`s_self`). The overshoot's insertion now re-places it in either direction.
   * **The block bounded** (review 8; `over_ret_ms`): an overshoot turning back blocks its follower until its predicted return (the out-leg
     to its turn point, a U-turn of π R and the rejoin to its apex at the lane speed, taken when it begins) + T_stop + the step; past it the
     law's stop is bounded by T_stop as any non-block.
   * **No release into a closing predecessor** (review 9; `LaneCtx::closing`): a predecessor flying back toward the follower along F
     (below −kLaneStopV, not an overshoot inside its bounded block) inside g_req is a stop with no release to the floor.
   * **The follow gap behind a final** (3b; `LaneCtx::pred_final`): behind a predecessor inside its final (past d_end − its settling
     distance, or in the hand-over) the follow law holds g_req + the follower's own stop at its current speed (v² / 2a: 1 km for an M1 at
     100 m/s), so a P2 that drops the speed at d_end leaves a pair the follower can resolve.
   Units: the follow law at 600 m behind a final 10 m/s (100 m/s otherwise), a predecessor flying back inside g_req past T_stop a stop.
   Measured **[m]**: `--lane` 2,851 checks, 0 failures; `overshoot` passes with the seen-in-front history (4b, 4c, safety). 3d and 3c:
   items 5 and 6 below; the cast `behind_start`: item 7.
4. **One geometric clearance test for every own-gate lane suppression** (review 1, 4, 5, 6, 7, 10; `gc::gate_solid`, `gc::lane_end`,
   `lane_gate_why`). The round-1 row assumed the gate's solid inside its bounding sphere (the Terran ring's outer radius is 1.026 r),
   never discriminated before d_end, returned `guard` on the Terran ring (Λ's d_end point inside the sphere) and certified the hand-over
   200 m off its final with the board's up to 1.1 s old nose and no clearance test. Now:
   * **The solid** by the gate's TGates row (gate_hole.txt, ship-navigation-ai.md §10.9): the installed Terran rows 21–24 a ring, radial
     [0.663, 1.026] r over z [−0.498, +0.179] r; the installed ordinary rows 0–20 none (no collision body: a ship can never touch them);
     an unknown row (`0xffff`) the most conservative measured ring (hole 0.605 r, outer 1.026 r, z −0.651..+0.655 r). Distance: the 2-D
     distance in (radial, axial) to that rectangle; the hull capsule swept along a leg sampled every 50 m (less 25 m).
   * **d_end per gate** (`lane_end`, `Member::d_end`, `dend`; every lane function takes it): kLaneEndM (2.3 km) on a gate without a solid;
     on a ring its front + the longest hull (1.3 km) + a hand-over's stop run-out at the lane speed (100 m/s at 5 m/s², 1 s: 1.1 km) +
     150 m: **4,147 m on the installed Terran ring** (r 3,207.5 m; L_i's depth d_end − 0.75 r = 1,741 m). A ship before d_end cannot reach
     the ring within a stop whatever its heading **if its half-length is at most 1.3 km**. That is the design hull, not the installed
     roster's longest (review fixes item 10; `round2_review/long_hulls.py` → `long_hulls.txt` **[m]**): 40 of the 131 installed capitals
     exceed 1,300 m in radius or hull half-extent (M2 to 2,998 m, Forseti at 139 m/s; M1 to 4,583 m; TL 1,837 m; M7 1,431 m; medians
     M1 / TL 1,135 m, M2 1,337 m); covering the longest flyable hull would put d_end at 5.8 km (6.8 km for the 4,583 m M1). d_end stays:
     the row tests each member's own hull (L, w) against the solid on its path and over the hand-over's run-out, so on the Terran ring a
     long hull is **guarded** where its own geometry reaches the ring (the engine's warn and its 0x12: an eviction from the lane), never
     suppressed into it; the casts' hulls are within 1.3 km. **Λ's lateral on a ring**: l_c is the middle of the finals, not clamped to 0.75 r + 100 m
     below the axis (that clamp kept escorts out of the grid's spheres; on the ring it put them 2.5 km below the axis, outside the
     2,127 m hole, and their hand-over and launch through the ring's body: measured in development, ring contact −542 m at tf 6); the
     grid's first row lies at 2 (r + r_s) ≈ 8.6 km, beyond d_end, and site B's tick deferral covers an order 4's first tick.
     `lane_point_lc_env` takes the row: on a ring L_i's envelope is inside the hole.
   * **The row**: before d_end (inbound / funnel) the member's path (`lane_path`: E's live position and velocity, the avoidance's detour,
     the rollout, two-sided fresh, else `roll`) clears the solid by the sweep margin on every leg of its horizon, past d_end included
     (`guard` otherwise; the legs past d_end, the hand-over and the launch about Q, are where it discriminates; order 5 is exempt from
     contact by the engine's pair filter, §10.9 Q2). The hand-over: within 200 m of its final with the **live** direction of travel (the
     velocity, or the live nose below 1 m/s; the slip included) within 8° of F, and its hull, a capsule along the **live nose**, swept
     along that direction over its stop's run-out clear of the solid by kMarginM (`solid_gap_sweep`, review fixes item 8); no solid: the
     window alone. The path's bound is the rollout's 30 s horizon, as item 1's. E passes A's live velocity, nose and fresh detour (`e_live_motion`, `e_detour`).
   * **Freshness** (review 7): `roll_fresh` is two-sided (a stamp ahead of the clock is stale); lane_frame's clock reset clears `roll_ms` and
     the samples.
   * **Casts** `terran` (formed M2/M1/M2) and `terran_abreast` on the installed Terran ring (row 21, r 3,207.5 m), every assertion as the
     1105 m casts (the slot reference T_slot,ref 50 s there, measured 43.2–49.3 s at tf 1; the cadence floor of every pair is now the slot
     reference too: 21 s on the 1105 m gates, which an M7 pair's 20.4 s spacing fell under) but the escort's stop at d_end (reported:
     on the ring the escort waits for its order 4 behind the leader's longer slot and its 200 m crawl runs out); the fixture's own ring
     oracle (`ring_gap`, independent of the core) asserts no hull reaches the ring on any order but 5 in every cast. Measured **[m]**
     (`--lane` 2,696 checks, 0 failures): `terran` 0 own-gate 0x12 (7,643 / 558 / 403 warns at tf 1 / 6 / 10 in the committed output of the
     review fixes, all `lane`), P2 kept 3/3,
     P2's nose ≤ 1.5°, the closest hull to the ring 550 m, spans 260.0–266.2 / 191.9–201.8 / 174.8–176.4 s; `terran_abreast` 0 own-gate
     0x12, P2 ≤ 5.2°, ring ≥ 550 m, spans 227.9–240.2 / 163.3–174.7 / 143.6–147.0 s; an escort's stop at d_end 72 and 285 frames at tf 1
     (seed 1). On 77f9e88e (`round2_head/run.txt`): `terran` 1 of 3 launched at tf 1, own-gate 0x12 from 3.4 to 11.1 km, a hull 340 m
     into the ring (47,473 frames), 122 failures in all. The 1105 m casts: unchanged (d_end, l_c and the row's suppressions there are as
     before: no solid). Units: Terran d_end 4,147 m; before d_end inside the hole `lane`, the same path 2.3 km below the axis through the
     ring past d_end `guard`, on an ordinary row `lane`, an unknown row `guard`; the hand-over inside the hole `lane`, outside `guard`, with
     the board's nose along F but the live velocity 20° off `guard`. The sample test's cost is bounded by its far-before-the-front reject
     (a leg more than 1 km + w before the ring's front is not sampled).

5. **Item 3d, the queue hold behind a refused leader** (seventh implementer, 2026-10-15; checks `3d2` and `3d` fail on 5d5c5e69,
   `round2_close/run.sh` → `run.txt`). **The finding [m]**: the 0x12 in `p2_miss_near` came from the leader's rollout: a hand-over
   member's rollout ran one plant step of its lane law before its hand-over branch, so the refused leader, stopped at d_end, crept
   2.5 m/s in its rollout; the follower's rollout on the follow law's 10 m/s floor (v_pred above kLaneStopV: no block) ran into it at the
   horizon's end (t_c 57–60), the pair was unresolved, the yield's cap 0 (the car-merge brief stop) lapsed at T_stop + the step (21 s)
   and the engine's warn fired (1 episode in 6 of 9 runs). The refused leader also left the board at kHandoverMaxMs, 120 s after P2,
   before its grant (refusals 112–122 s): its escort's hold ended with it 3 s before the grant and it flew the engine's 0x14 at the
   stopped leader. **The law**: a follower queued behind a predecessor stopped in its hand-over is a queue hold at spacing (the law's
   stop, a block), not the brief stop: it may last the refusal; the predecessor's grant (order 5 or 8 seen, `Sb::dock` / `launch`) ends
   the block and the stop rule at once (`LaneCtx::granted`), so site B's stop write ends within the step of the grant (the rollout keeps
   the stop: a granted predecessor's rollout shows its run, and at a SETA trigger order 8's lane rollout holds its point). The hand-over's
   rollout flies site B's write from the state: P2 kept the held speed (the refusal's brake bound), not kept the lane speed under that
   bound, refused the brake to a stop at d_end, reached at a_lin. A refused hand-over stays a member up to `kLaneRefusedMaxMs` = 300 s
   after P2 under the lane key (`kHandoverMaxMs`, shared with the capital gate row and the key-off board, unchanged). **Check `3d2`**
   (the p2_miss casts): site B's stop-write episodes behind a predecessor that reached P2 last at most its refusal (P2 to the grant) +
   the step, and the law's stop ends within the step of the grant; no brief stop beyond T_stop + the step. Measured **[m]**
   (`p2_miss_near`, 9 runs): 0 episodes, 0 uncertified frames, capsule ≥ 468 m; the queue hold 77.4–102.6 s (0.25–0.67 s shorter than
   the refusal measured from P2), its last stop write 0–17 ms after the grant (one frame at tf 1); brief stops ≤ 2.7 s (the pair's
   yield when the leader drops its speed at P2). `p2_miss`: brief stops 0 (were up to 35.0 s at tf 6). 5d5c5e69: 12 failures in
   `p2_miss_near` (7 safety 7, `3d2` 5), 1 in `p2_miss` (`3d2`) and 4 of the per-ship `3d` (item 6).
6. **Item 3c, the overshoot's rollout and out-leg** (check: `overshoot_near`'s `7 safety` fails on 5d5c5e69, 9 runs). **The finding
   [m]**: the overshoot's rollout took the hand-over's straight run past d_end (x ≥ 0 after its first plant step), so it flew straight off
   at its velocity while the ship turned back: 10–443 uncertified frames and 1 episode; as the head behind the Titan in its hand-over its
   law ran from its own s past d_end, where the pacing's stop at d_end held it 19 s with the nose −F. **Now**: an overshoot that has not
   turned in (`Member::over_in`, set when its re-departure's turn is done) rolls out its re-departure (the out-leg below Λ and the U-turn
   as `lane_steer` flies them; never the hand-over's branch while re-departing) and its law runs from its U-turn's apex until it turns
   in, the head's too. **The out-leg's length**: kept at d_end + settle + R; P2's nose needs all of the settling distance **[m]**
   (`round2_close/out_leg.sh` → `out_leg.txt`, both overshoot casts, 9 runs each): 0.5 × settle 6.8–11.1° and 22–24 failed checks,
   0 × settle 13.8–21.1°, 27–36 failed checks; 1 × settle 2.4–5.8°, 0. **Fixture**: a cast's ship on an order 4 already in flight
   (issued before the run) has its first tick after the board's first frame (1 s) when it starts inside its grid slot's sphere (the
   Titan of `overshoot_near` P2'd at 4.6 km at t = 0 in seeds 7 / 1234567); the overshoot is seen in front before its first lane frame (on
   its second frame it ranked behind the Titan once). Measured **[m]**: `overshoot_near` 0 episodes, 0 uncertified frames, P2 2.5–5.7°
   kept, capsule ≥ 943 m, every short-final check; `overshoot` P2 2.4–5.8°, outermost 5.74–5.89 km (as before).
7. **Item C, cast `behind_start` (review 3): not closed, parked.** Two attempts, neither committed to the core. (a) Geometry
   (`round2_close/behind_start_attempt.diff`): the overshoot's re-departure for the ship, see Open. (b) **Exclusion**
   (`round2_close/behind_start_exclusion.diff`, the coordinator's rule): a ship whose commit is attempted while it is behind the plane is
   no lane member (`gc::lane_entry_ok` in the DLL's and the fixture's `ctl_commit`: no rewrite, no own-gate or obstacle suppression, no
   lane law; the engine's own order 4 and the module's non-member path) until it is in front of the plane by its stop run-out
   (v² / 2a) + its half-length and its lateral from the gate's axis is at least r_gate + w + 50 m; a committed member's refresh is never
   held. Measured **[m]** (9 runs; cast flags `back`, checks `Cb`): the held ship is never a member behind the plane (0 frames), 0 member
   0x12 episodes, all launched (spans 185.3–185.6 / 159.8–166.5 / 145.0–148.7 s), but **a contact at tf 1** (capsule −11 / −340 /
   −260 m, box −349..−639 m; under SETA 737–1013 m) and **the Titan's P2 not kept in 9 of 9 runs**: the Titan's sphere avoidance
   detours it 160–260 m off Λ (nose 27° off F at 10 s) against the head-on non-member; the held ship, slowed near the plane by its own
   avoidance (s ≈ +20 m, 200 m above Λ at 30–40 s), sits in the Titan's order-5 / launch path; its own P2 (reported) comes at the KC's
   grid point 3.5 km out with the nose 140–174° off F, then 0x1f's rotation. The cast stays parked with these numbers (`Cb`'s checks
   kept in the fixture for its re-enable).
8. **Item 6, assertions and metrics** (`1b6`, `7b6`, `3d`): **the nose at 7 km in every cast** (`1b6`; measured only on the inbound
   crossing of a ship that starts outside 7 km, each such ship once): 5°, or the cast's bound where its geometry cannot reach 5° (the
   measured maximum over 9 runs + ~1.5°) **[m]**: formed, formed_m7, stragglers, partner_block, formed_stage2, leader_killed,
   small_escort, station, m7_cross, p2_miss / p2_miss_near, retarget, redeparture's convoy, overshoot's Titan ≤ 0.2°, terran 0.8°,
   run173 1.8–4.4° (5°); abreast3 14.4–17.7° (19.5°), abreast5 27.6–36.2° (38°), terran_abreast 16.6–17.4° (19°), crossing
   12.8–14.3° (16°), slow_hull 11.2–11.6° (13°): a merging yielder holds its lateral while level with its predecessor (the car-merge
   rule), still joining at 7 km; stragglers_far 6.5–8.4° (10°), run173_15km 12.9–16.8° (18.5°): a join from far out and wide starts at
   the steepest 45° and merges at its join's end inside 7 km (run173_15km's leader 5.3 km off Λ 15.9 km out: the parabola itself
   13° at 7 km **[i]**; an 8 km merge point at 45° tried in development: no cast under 5°, run173_15km 11.6–16.4°); parked
   14.3–23.5° (25°), join_field 19.5–24.8° (27.5°), join_block 7.5–10.7° (12.5°): the obstacle detours. **Item 6's 5° target is met
   only by the formed casts** (the list above to run173); the abreast, crossing, slow-hull, far-join, join and parked casts reach
   6.5–36.2° at 7 km, and their per-cast bounds are regression guards at the measured maximum, not the target (Run173's complaint was
   ~20° at 7 km). **The parked cast** (`7b6`):
   own-gate 0x12 ≤ 3 per run and P2's nose ≤ 33° (measured 1–3 at 2.08–2.39 km, `gate_row=guard`, and 26.2–32.1°): the detour around
   the non-member on Λ (§6.6, left to the engine's warn and the sphere avoidance) brings the ships to d_end off their final. **The per-ship
   wait** (`3d`): a ship's own counted wait (`x_acc_ms`) against its refusal at the grant, not the run's sum over every ship (fails on
   5d5c5e69 in 4 runs: the refused leader left the board at 120 s). **`lane_join_loss` takes R** (`lane_turn_r`, as `lane_carrot`'s span):
   no measured change (the turn term binds below R / 6 ≈ 106 m of lateral, where the loss is 0 under kLaneOnM). **Rows**:
   `test_ship_trace_format` parses `ship_steering_lane_summary` (the round-1/2 counters, `gates_gate:F:G:r`) and
   `ship_avoid_engine_warn_pre` (`entries_…`, `gate_row=`, `obst_row=`); `ship_trace_rows.py` takes both (a vector list item a tuple).
   Not done (Open): the 15/8 km merge-point hysteresis, the before-checks for the blend's end, the stop law's release, the re-commit
   hysteresis and the unkept write, the 3c/3d comment.

**Round 2 spans** **[m]** (15 km → last launch, 3 seeds; `ship_steering_host_lane.txt`, `scenario_c.txt`; — : launches asserted, no span):
against round 1 the existing casts are 0–6 s longer at tf 1 (abreast 1+4 +6 s, formed +6 s, partner block +4–6 s, slow hull +6–10 s):
item 3b's follow gap behind a predecessor inside its final (g_req + the own stop) spaces the last pairs further apart. Constants, one changed
value each (2,851 checks per build): g_req 400 m → 14 failures, τ_m 8 s → 11, τ_f 5 s → 5, T_stop 10 s → 0, λ_max 10 s → 39, crawl 0 → 10,
drift 100 m → 2, d_end 1.8 km → 5 (round 1: 162; the lane row's before-d_end test no longer rests on d_end on a gate without a solid),
escort join k 0 → 1, 0.5 → 0. Cost **[m]** (bench24, the 24-member frame's controller block): 10.8–11.0 µs mean at tf 1, 133–134 µs at
tf 10 (round 1 12.1–12.4 / 144–152); the bench check failed once (1,027 µs third-worst at tf 10, `X3M_SEQ_OFF=1`) and passed on rerun
(524 µs).

| Cast | tf 1 (s) | tf 6 (s) | tf 10 (s) |
| --- | --- | --- | --- |
| abreast3 | 179.1–187.6 | 158.3–158.7 | 159.3–159.4 |
| abreast5 | 230.9–238.1 | 213.4–213.6 | 207.4–207.8 |
| formed | 209.3–217.0 | 188.8–189.3 | 189.9–192.1 |
| formed_m7 | 198.5–210.4 | 183.5–185.0 | 177.4–179.0 |
| stragglers | 200.6–209.7 | 188.0–188.2 | 181.7–184.9 |
| stragglers_far | 307.5–307.9 | 295.6–295.9 | 289.5–290.4 |
| redeparture | 235.2–240.4 | 218.2–220.0 | 212.4–212.8 |
| crossing | 335.7–340.9 | 323.7–325.1 | 317.3–317.5 |
| partner_block | 241.5–244.2 | 222.5–223.7 | 223.0–226.0 |
| formed_stage2 | 209.2–209.2 | 188.8–189.1 | 189.7–192.1 |
| slow_hull | 284.8–292.5 | 253.6–254.8 | 253.0–253.4 |
| parked | — | — | — |
| leader_killed | — | — | — |
| small_escort | 200.7–212.1 | 187.4–187.9 | 182.4–183.9 |
| run173 | 487.5–487.7 | 470.4–471.5 | 470.1–471.6 |
| run173_15km | 487.0–487.1 | 469.2–471.7 | 469.6–471.0 |
| overshoot | 186.5–187.1 | 163.0–163.9 | 164.7–166.7 |
| p2_miss | 296.9–299.6 | 273.3–285.9 | 274.3–275.7 |
| station | 209.3–217.0 | 188.8–189.3 | 189.9–192.1 |
| join_field | 320.7–322.6 | 301.9–304.0 | 301.5–303.2 |
| join_block | 306.3–306.3 | 287.4–287.7 | 287.0–288.2 |
| m7_cross | 209.3–217.0 | 188.8–189.3 | 189.9–192.1 |
| terran | 259.7–266.1 | 193.9–201.6 | 178.7–179.4 |
| terran_abreast | 227.8–240.0 | 164.5–173.9 | 148.0–150.5 |
| retarget | — | — | — |
| p2_miss_near | 297.0–299.5 | 273.3–285.9 | 274.0–275.7 |
| overshoot_near | 155.0–155.2 | 133.1–134.8 | 134.6–139.0 |

Closing items (items 5–8, `scenario_c.txt` refreshed on 209ca671, 3,337 checks per build) **[m]**: the rows above are unchanged but
`overshoot` (26 s shorter at tf 1 and under SETA; with item 6's rollout and law from the apex **[i]**, the cause not traced), `p2_miss` (tf 6's upper
end 13 s shorter: no brief stops) and the two re-enabled casts. Constants, one changed value each: g_req 400 m → 14 failures, τ_m 8 s →
28, τ_f 5 s → 5, T_stop 10 s → 0, λ_max 10 s → 41, crawl 0 → 10, drift 100 m → 3, d_end 1.8 km → 9, escort join k 0 → 5, 0.5 → 7.

### Lane fix round 2 review fixes (2026-10-15; each with a check that fails on 63b181c4 where one is possible)

Two independent reviews of the round (Opus, Fable) found no blocker. Evidence under
`verification/results/ship-steering/convoy-gate-transit/round2_review/` (`run.sh` → `run.txt`: this fixture on the 63b181c4 core with
`-DX3M_LANE_63B1`, which leaves out the units of the new core functions, items 3 and 7: 5 failures of 3,342 checks, the units of items 1,
2, 5, 6 and 8 **[m]**). Units: `lane round 2 review units (item N)` in `ship_steering_host_lane.txt`.

1. **The overshoot's return across a backward clock jump**: lane_frame's clock reset clears `over_ret_ms`, restamps `over_ms` and sets
   `over_new` for an overshoot, so its return (and its zone) is re-taken on the new clock; before, `now − over_ret_ms` stayed negative and
   the follower's block held for the rest of the out-leg (63b181c4: the old 200 s stamp kept at 5 s).
2. **A hand to another leader ends with the escort membership**: `exit_ms` / `exit_tgt` / `exit_clear` are cleared on eviction and on a
   re-entry in another role (`enter`), and honoured only while the role is escort (`plan_of`'s `Plan::drift`, the frozen lane order,
   `exit_clear`, lane_frame clears a non-escort's). 63b181c4: kept across the eviction and the re-commit; the committed member flew `drift`.
3. **A ship obstacle's hull** (`gc::obst_hull`, the DLL's `ctl_e_obst`): L = the census half-length when it exceeds w, else r (as
   `hull_from`); never w, which made a stopped ship whose half-length exceeds its radius a sphere of radius w < r. DLL-only before: the
   unit pins the helper (r 700, w 200: l 900 → 900, l 500 → 500, l 150 → 700).
4. **Never the player's ship as B** in site E's obstacle row (`[b+0x40] & kFlagPlayerObject`, the member exclusion's test, before any
   bound): `test_ctl_boundary_reads.test_e_obst_never_the_player_ship` (fails on 63b181c4's source).
5. **The refused hand-over's bound** is `kLaneRefusedMaxMs` + the step (`validity`): kept at 300 s + 16 ms with a 33 ms step (63b181c4:
   `target`), ended at + 34 ms.
6. **The solid test's leg** (`solid_gap_seg`): a non-finite length returns −1 (fail closed; 63b181c4: NaN for a NaN end), at most 256
   samples (`kSolidMaxN`), the wider spacing charged: a 1,000 km leg 4,656 m against 6,609 m exact (63b181c4: 20,001 samples, 6,584 m).
7. **No path built on a gate without a solid** (`lane_path_exists`: lane_path's fail-closed tests, two-sided freshness, a sample left,
   a finite speed, a detour's turn rate and finite reach): agrees with `lane_path` in 8 of 8 cases; host cost 2 ns against 1,853 ns
   for the path with a detour **[m]**. The casts' rows are unchanged.
8. **The hand-over's hull along the nose** (`solid_gap_sweep`): the capsule along the live nose swept along the live direction of travel
   (the window's 8° is the velocity's); lines along the travel through the hull at a spacing that keeps the charged gap ≤ 25 m (≤ 32
   lines). A 2,998 m hull on its Terran final at 100 m/s along F: nose along F `lane`, 5° off `lane`, 20° off `guard` (its front 2,404 m
   from the axis; 63b181c4: `lane`). Host cost 0.56 µs (nose along the velocity, one line) / 2.9 µs (20° off) per call, only on a ring's
   hand-over window **[m]**. The `terran` casts' rows are unchanged.
9. **Check 5's Terran waiver bounded**: the escort's stop at d_end is summed in game time (`dend_stop_ms`) and bounded by the measured
   maximum + ~0.5 s + the step (`terran` 1.70 s → 2.2 s, `terran_abreast` 4.90 s → 5.4 s, seed 1 at tf 1; 0 under SETA); every other cast
   0. Passes on both cores (no failing-before witness: it tightens a waiver).
10. **kLaneHullMaxM** is the design hull, not the roster's longest (item 4 above, `round2_review/long_hulls.txt`); d_end unchanged.
11. **The engine model's box corners**: the part sphere's reach 1.2 (r_part + √3 r_A) + |v_A| + b holds the inflated box's corners
   (ship-navigation-ai.md §11.2, measured). `m7_cross` 19 → 1,684 warns at tf 1, all `lane`; `parked` 0–268 → 12,603–12,898 at tf 1
   with 1–3 avoidable 0x12 per run (9 runs; the row's horizon, Open), bounded at 3 for that cast; station, join_field, join_block
   unchanged **[m]**.

Acceptance **[m]**: `c++ -std=c++17 -O2 -Wall -Wextra -Werror`: `--lane` 3,344, full 13,865, `--capital` 8,207, `X3M_SEQ_OFF=1` 13,775,
`--b9` 45: 0 failures (`--lane` and full rerun alone after the bench24 third-worst frame reached 1.1–1.2 ms with nine runs in parallel);
key-off 0 differing lines of 1,162 / 991 / 1,162 / 201 (`round2_review/keyoff_diff.txt`; `round1_head/keyoff.sh` now takes the label);
the four unittest modules 39 tests OK; i686 MinGW `-fsyntax-only` clean; x87 per function in `ship_steering.cpp` unchanged, 5
functions, 42 instructions (`round2_review/x87_per_fn.py`). `scenario_c.txt` refreshed (3,344 checks per build): the constants as at
209ca671 but λ_max 10 s → 45 failures (41) and crawl 0 → 15 (10), the new failures item 9's Terran d_end stop bound; escort join k 0 →
8 (5), the new ones `parked`'s avoidable bound (item 11) **[m]**.

### Measured (scenario C, 3 seeds × SETA tf 1/6/10; scenario_c.txt, ship_steering_host_lane.txt) [m]

Refreshed after lane fix round 1 (the fourth implementer's table is in the ledger, "Lane stage 1"; against it: abreast 1+4 3 s
shorter at tf 1 and 4 s under SETA, crossing 6 s longer, the rest within 1.5 s):

| Variant | tf 1 (s) | tf 6 (s) | tf 10 (s) | merge completion max (s) | closest capsule (m) | nose at 7 km max (°) |
| --- | --- | --- | --- | --- | --- | --- |
| abreast 1+2 | 172.9–188.1 | 151.6–153.4 | 152.3–153.5 | 79.7 | 448 | 17.7 |
| abreast 1+4 | 224.0–234.9 | 209.6–210.0 | 203.8–204.0 | 91.1 | 448 | 28.4 |
| formed M2/M1/M2 | 203.0–216.6 | 184.9–186.0 | 185.4–185.6 | — | 511 | 0.1 |
| formed M1/M7/M7 | 199.6–209.7 | 180.1–181.7 | 174.4 | 7.0 | 349 | 0.1 |
| stragglers | 200.2–209.2 | 186.2–186.8 | 180.4 | 28.7 | 510 | 0.2 |
| stragglers far | 307.3–307.6 | 295.4–295.5 | 289.2–290.4 | — | 4260 | 0.5 |
| re-departure | 234.0–239.2 | 210.8–211.3 | 205.1–207.0 | 7.0 | 349 | — |
| crossing | 335.4–340.9 | 323.7–325.1 | 317.3–317.5 | 189.7 | 515 | 14.3 |
| partner block | 235.9–240.4 | 218.9–221.7 | 216.9–220.7 | — | 454 | 0.1 |
| formed, stage 2 | 201.9 | 184.7–186.0 | 185.4–185.6 | — | 646 | 0.1 |
| slow hull | 279.0–282.4 | 246.4–247.4 | 247.0–248.1 | 116.5 | 216 | 11.6 |
| small escort | 200.5–210.7 | 183.7–185.0 | 179.0–179.4 | 7.7 | 327 | 0.1 |
| Run173 (45–76 km) | 488.5 | 470.8–471.8 | 471.5–472.6 | — | 4928 | 1.0 |
| Run173 at 15 km | 491.2 | 473.7–473.8 | 474.3–475.8 | — | 5191 | 11.8 |
| overshoot | 212.4–212.9 | 189.5–190.4 | 191.4–193.7 | — | 1030 | — |
| P2 not kept, refused | 297.1–299.7 | 274.1–298.7 | 274.7–276.0 | — | 535 | 0.1 |
| parked (all launched) | — | — | — | — | 506 | 23.0 |
| leader killed (all launched) | — | — | — | — | 678 | 0.1 |

Spans are 15 km → last launch (the Run173 casts at their lane floor: the escorts 24 and 32 km behind the leader at its 15 km; flown
785 s). 0 member 0x12 episodes, 0 uncertified frames in 162 runs; 0 own-gate 0x12 but `parked` (11 in its 9 runs); P2's nose
≤ 5.8° but `parked` (32.1°); brief stops only in `p2_miss` (10, up to 35.0 s at tf 6 behind the refused leader: Open); law stops in
`overshoot` (9, ≤ 21.5 s, behind the overshoot's turn back, a block), `re-departure` (9, ≤ 9.0 s), `partner block` (3, ≤ 0.3 s);
T_slot 17.6–31.4 s at tf 1, 1.4–36.5 s under SETA (the 36.5 s: `p2_miss`'s refusal); the pre-lane core fails the span assertion in 33
of 33 control runs. The nose at 7 km is asserted for `run173` only (≤ 5°); the abreast casts' escorts start 12 km out and 2.6–5 km
off Λ, a merging yielder holds its lateral, so they are still joining at 7 km. Constants, one changed value each (`scenario_c.py`, 1902
checks per build): g_req 400 m → 8 failures (the passer capped, the escort gate); τ_m 8 s → 2 (1 the bench under host load); τ_f 5 s → 8
(`p2_miss` safety 6); T_stop 10 s → 3 (`p2_miss` safety); λ_max 10 s → 35; crawl 0 → 13; drift bound 100 m → 5; d_end 1.8 km → 162
(the lane row's slab bound needs d_end ≥ 0.66 r_gate + r_A + 0.25 r_gate, 2.1 km for an M2: every lane cast's own-gate 0x12 returns);
escort join k 0 → 0 (fourth implementer: 14; the tangent join completes on Λ before the lost-leader wait), 0.5 → 1 (the bench).

Fourth implementer, for comparison: 0 member 0x12 episodes, 0 uncertified frames, 0 brief stops in 126 runs; constants g_req 400 m → 13
failures, τ_m 8 s → 1, τ_f 5 s → 4, T_stop 10 s → 0, λ_max 10 s → 29, crawl 0 → 12, drift 100 m → 6, d_end 1.8 km → 3, escort join
k 0 → 14, 0.5 → 10 (1114 checks per build; the engine's own gate warn was not modelled).

Second implementer, for comparison (its spans are in the ledger): 0 member 0x12 episodes, 0 uncertified frames, 0 brief stops in 90 runs. T_slot 16.8–21.1 s at tf 1,
1.4–10.0 s under SETA. The pre-lane core fails the span assertion in 24 of 24 control runs. Constants, one changed value each
(`scenario_c.py`): g_req 400 m → 19 failures (the passer capped; 1 safety); τ_m 8 s → 0 failures, merges 50.3 s; τ_f 5 s → 2 (the
passer capped); T_stop 10 s → 0 (no stop occurs); λ_max 6 s → 18 (escort gate, short final, spans); d_end 1.8 km → 7 (the law units
fixed at 2.3 km, 2 runs passer capped, 1 order); escort join k 0 → 6, 0.5 → 4. Cost (host x86-64, the 1+4 cast, the frame's
controller block): lane 2.1 µs mean / 44–52 µs worst frame at tf 1, 21.9–23.1 / 45–54 µs at tf 10; stage B 1.9–2.2 / 73–102 µs and
20.7–22.2 / 78–99 µs; ceiling 1 ms (i686 figures come from the first flight's `ctl_sb_us_max`).

### Fixture engine paths (2026-10-15; holistic review rank 5, ship-navigation-ai.md §10.14)

The host fixture's engine model gains the paths a green scenario C could not speak for ([ship-steering-holistic-review.md](ship-steering-holistic-review.md) §4 items 1, 3, 4, 6) and the corrections of [ship-navigation-ai.md](../reverse-engineering/ship-navigation-ai.md) §10.14. Fixture only (`verification/probe/ship_steering_host.cpp`); `src/proxy` unchanged, no row changed. Every new cast fails on this controller and is parked in `kCSetups` (`// PARKED:` lines, each with its numbers). Evidence: [`engine_paths/run.sh`](../../verification/results/ship-steering/convoy-gate-transit/engine_paths/run.sh) → `item_a.txt` and one `<cast>.txt` per parked cast; `summary.py` → `summary.txt` (per cast and time factor, three seeds). Switches: `X3M_C_OLDWARN=1` restores the old warn model, `X3M_C_NOFLY=1` counts the 0x12 without flying it (the round-2 oracle), `X3M_C_ENGALL=1` prints every seed's engine-path row.

**A. The engine's warn** (`eng_warn`, which replaces `gate_warn_cone`, `obst_warn_engine` and `crowd_pair_warn` in scenario C):

* A capital A ignores only small ships (TS/M3/M4/M5/TP) and expendables. It tests M6/TM/M8 (Q2).
* Close range, d ≤ 1.2 rr, applies to a non-mesh B with r_B ≥ 0.75 r_A, a rock or a B that cannot move. The gate qualifies; a station or a capital (mesh) does not.
* The margin L is followed by B-behind, the B term w (doubled for a large approaching B; for cos < −0.8 against a ship), b = (w + v cos)² / 2a for cos > 0, else b = w (a speed used as a distance), capped at 600 m and halved for escort pairs. Then the range gate and the cone.
* A ship at speed 0 with a non-zero desired speed is tested.
* Mesh B (a station, a capital) takes the box test. A station's parts use the radius superset 1.2 (r_part + √3 r_A) + |v_A| + b, with r_part ≥ the box's half-diagonal by construction. A ship's hull, whose box (w, w, L) and rows the fixture knows, takes the per-axis test itself. The sphere superset would reach 3.8 km for a Titan and warn an escort on its leader at the 2.8 km trail spacing.
* The pair filter runs the way Q2 reads it: X ignores a Y that escorts or follows X. X tests the leader it escorts, and a co-escort, with b halved. Before this, the escort's warn on its leader was skipped.

Measured **[m]** (`item_a.txt`, seed 1, old → corrected, the 0x12 counted):

| Cast | Change | Cause |
| --- | --- | --- |
| `station` | warns 11,009 / 853 / 539 → 0 | A mesh station has no close range. Its parts, 3 km beside Λ, are beyond the box's reach |
| `parked` | warns 12,898 → 859; 0x12 2–3 → 0; avoidable 2–3 → 0 | The range gate and cone come before the box |
| `join_field` / `join_block` | warns +35 % / +38 %; `join_block` avoidable 0 → 1–3 (within its bound of 3) | Rocks take the close range |
| `m7_cross` | 1,684 → 1,306 | b uses v cos |
| `redeparture` | gate warns 7,229 → 7,865 | The close range at P |
| own-gate warns, every other cast | −0.1 % | b uses v cos |
| `retarget` | non-member warns 2,195 → 26,573 (counted only) | The handed escort now tests its new leader |

0 unsafe suppressions in every cast. One asserted check changed: `station`'s 1r2 failed with no warns to suppress. It now asserts no 0x12 for both casts and the suppressions for the M7 alone.

**B. The 0x12 flown** (`X12`, `x12_begin`, phase 31). An unsuppressed warn opens the KC's CollisionWarn, then 0x12 from the next frame. It starts only where the script loop issues it (§10.1 / §10.14 Q3):

* order 4 (`__EnterHyperspace` phase 0, end radius f+0x260 = r_s + 300 m);
* 0x14 / 0x15 (r_s + 100 m);
* not after P2, where a warn re-runs the rotate.

The handler (§3.3):

* **Re-score.** Every 200 ms the list is every gate, static or ship that the warn test still flags (L + r_A on 0x12), at most 5.
* **Escape direction [i].** It starts from a basis perpendicular to the nearest obstacle and steps 2.8° up to 169° at a random elevation. A must-turn-away obstacle blocks a candidate unless it points away (dot ≤ −0.8, −0.9 for a gate); this reproduces Run173's 166–172° turn-aways at 6.9 km. Any other obstacle blocks inside its cone. The first unblocked candidate wins, else the one with the highest weight. Site A steers the escape point.
* **Speed.** The slowest listed obstacle's speed, floor 10 m/s; with the list empty, the cruise speed **[i]**.
* **End.** The list is empty and the ship is beyond the end radius of the start point (refreshed while the list stands **[i]**).
* **After the end.** The KC re-issues order 4 at the grid slot the ship still holds. §10.1's loop releases no slot across a 0x12, so this is the held slot, not "a free slot". A member's site-A rewrite to L_i follows. An escort gets 0x14 again.
* **DLL model.** The fixture's `t_observe` now models the DLL's `keep_through_avoid` / `avoid_over` (B.3 item 2: a member whose 0x12 list held only its own gate in funnel or hand-over is kept).

Flights only under the lane key: the key-off controls keep the counted model. With flights, `crossing` tf 10's key-off control launches 4 of 5 in 1000 s (Open). Round 2's "avoidable" oracle does not judge a flown eviction, because its track is the 0x12, not the lane plan; `X3M_C_NOFLY=1` keeps that oracle. In the active casts the flown 0x12 are `join_field`'s escort (turn 93–129°, 64–68 s) and `join_block`'s (turn 104–111°, 22–26 s). `join_block`'s 1b6 bound was 12.5°; it becomes 24°, since the measured range is now 6.7–22.3° (counted: 7.5–10.7°).

**C. Stations and rocks on Λ** (`add_obstacles` 5–7):

* a station on Λ 8 km out (sphere 1.5 km, four 300 m parts);
* a 43 km hollow base. The module walks it to its fallback body, 15 km radius (30 km across) on Λ, enclosing Λ. The engine sees eight 1.5 km parts on a 12 km ring; contact and the oracle are measured against the parts;
* four 250 m rocks across the column 10 km out.

The cast asserts launches, no contact, the formed span, and no lane braked (a member at ≤ 15 m/s on Λ for at most 10 s + the step). Member speeds are reported.

**D. Small ships at the gate** (`kSmEsc`, `kSmTrade`, phases 40–46):

* TS escorts fly 0x14 to a slot 1.2 km beside the leader's axis **[i]**. After the leader's launch they wait λ, then fly their own order 4 to A = G + (0, −0.8, −1) r with the module's lead-in and spread, P2 keeping the speed, and order 5 to B on the lower arc (θ within ±45° **[i]**).
* Site A applies `small_b_decide` (D.5) at their order 5, then P4 and order 8.
* A TS trader flies the same legs from t0 = 100 s.
* Small ships take their own engine warn against every ship (site E: the resolved set) and fly their 0x12.
* The members' warn against a small ship goes through the obstacle row: a TS is ignored, an M6 is tested and suppressed by the small-ship row.

The cast asserts members' launches and span, no contact with a small ship, and no member 0x12 against one. It reports the small ships' stops, 0x12 seconds and D.5 verdicts.

**E. Arrivals at P** (`kArrive`, phases 50–54; §10.10, §10.14 Q4/Q5):

* The partner side's through-slot request (polls 1–11 s; our side refused 60 s after).
* P = (0, 22.2 m + `capital_offset`, −0.01 r − r_s).
* The board's arrival member, as the DLL's launch hook enters it.
* 0x21 for 900 ms from 2697 m behind P, then one order-0 frame.
* 0x1e at 120 m/s along −F until |z| > r. A hull with r_s > 0.99 r ends it on its first frame at speed 0 and rests at P.
* Order 0 braking at a for `hold_ms`, then MovePos 9 km out.

The cast asserts no departing hull on order 8 within 500 m (capsule) of a parked arrival, no two arrivals overlapping, and no departing hull in contact with a parked arrival.

**The parked casts** **[m]** (`summary.txt`; 3 seeds × tf 1/6/10; each fails on this controller for the path it models):

| Cast | Item | Fails (of 9 runs) | What the controller does wrong |
| --- | --- | --- | --- |
| `x12_near` (Run173 E1 at f 18193, 1.7 km out, the engine's gate warn left to the KC) | B | P2 not kept / > 8°: 2 (P2 4.2–8.7°) | After the flown eviction (turn 53–71°, 17–19 s, re-committed at once) the re-departure reaches its final short of its settling distance |
| `x12_far` (Run173 E1 at f 15537, the warn at 6.95 km) | B | P2 not kept / > 8°: 5 (to 27.6°); member 0x12: 2 | After turn-aways of 98–128° (25–33 s) the re-issued member rejoins ahead of its follower: the pair is uncertified, and a second eviction is flown |
| `lane_station` (station on Λ 8 km) | C | span: 2 (to 759.6 s); member 0x12: 2, contact: 1 (tf 10: 8 flown, −918 m, P2 165.8°) | The module's sphere detour around a station on Λ (no lane-aware shift); lane v_min 2–71.8 m/s |
| `lane_fallback` (30 km fallback body) | C | span: 9; launches: 2 (tf 1) | No brake (v_min 70–73 m/s), but the fallback-sphere detour carries members ~5 km off Λ. P2 is never kept (19.7–31.3°). An M1's order 5 passes the plane outside its trigger and stays 2.1 km behind the gate at 10 m/s, its follower held at d_end |
| `lane_belt` (rock belt 10 km) | C | span: 1 (tf 6, 245.8 s) | One run brakes a member to 2 m/s at the belt; no contact, P2 0.3–2.3° |
| `small_gate` (two TS escorts, a TS trader) | D | small-ship contact: 4 (to −506 m) | A TS on its order-5 lower-arc leg meets a capital on order 5 (the engine's ignore mode on both). D.5 never mirrors: both sides are taken or clear, because the lane's final at l_c lies within w + r + 50 m of the arc's middle. Small ships: 1–5 stops and 4–84 s on 0x12 per run |
| `small_gate_m6` (one escort an M6) | D | small-ship contact: 4 (to −501 m) | As `small_gate`; the members' 663–18,282 warns per run against the M6 are all suppressed (small-ship row), 0 member 0x12 |
| `arrival_parked` (M1 r_s 1135 m resting at P, Titan 60 s later) | E | arrivals overlap: 9 (capsule −1180 m); member 0x12 and contact: 9; launches: 5 | No arrival fan (§6.7): the second hull lands in the first. The departing leader near d_end is warned by the arriving hull's box during 0x21. The board's pair is uncertified (contact predicted), the eviction is flown and the column collides |
| `arrival_pass` (two Titans 60 s apart, 0x1e 120 m/s, ~5 s order 0, 476–512 m out) | E | member 0x12 and contact: 8 (to −1157 m); launches: 2 | No overlap (3.5 km). The leader's eviction by the arriving hull, as `arrival_parked` |

The order-8 bound (500 m) is never reached: in `arrival_parked` no departing hull is on order 8 while an arrival is parked, except one tf-6 run at 1,218 m. The closest departing hull on 4 / 0 / 0x1f / 5 to the parked M1 is 58–297 m.

### Round A (rank 4 + arrival pair) (2026-10-15, Fable 5.1; §12 ratification, §12.1a, §12.5; holistic review §2 b–d, §5 rank 4)

Worktree `agent-a62ac571bfdfe5b38` on main `71b1536d`. Units: `ship_gate_lane_core.h` (the laws, the hand-over), `ship_gate_certificate_core.h`
(the sweep, the yield), `ship_gate_board_core.h` (the fields), `ship_gate_ctl_glue.cpp` (the launch hook's first decision), the fixture.
Every figure **[m]** from `round_a/run.sh` (`lane.txt`, `rows.txt`, `spans_diff.md`, `arrival_pass.txt`, `arrival_parked.txt`, `bench.txt`)
unless marked **[i]**.

**1. One gap law + lateral band** (`lane_law`, `lane_band_free`, `Member::band`). The merge and follow regimes, τ_m, the `merged` latch,
the credit cap and `kLaneBesideM` are gone: `v = min(v_lane, v_P + (gap − g_req) / τ)`, τ = τ_f 10 s, floor 10 m/s, with
gap = the raw gap + the joins' along-track cost − the own closing distance (v_self − v_P)² / 2a when this member is the faster. "merge" is
the row label while the raw gap is short of g_req − 50 m, "follow" after; the `ship_gate_ctl_merge` rows and `merge_done` keep the merge's
start/finish as before. The lateral rule is a per-pair latch (`band`): the clearance band holds until the yielder is behind (raw gap ≥ 0
with the law's gap ≥ 0) or the raw gap is g_req − 100 m, and never re-engages for that pair. Deviations from §2 b's wording, each
measured on the way: (a) the design's `gap_settle` with the full speed-difference term let a yielder at the floor re-accelerate 1.2 km
before it was behind, so it joined at the lane speed from 1.4 km beside — abreast3's nose at 7 km 17.7 → 21.8°, terran_abreast's
17.4 → 25.2° and, on the Terran ring, a hand-over nose of 7.4° whose hull sweep reached the ring's hole (an own-gate 0x12); the law keeps
only the closing half of the term (a follower behind a stopped or returning ship still brakes on its stopping distance: without it
`redeparture` tf 6 passed at a box gap of −10 m); (b) a band that releases only at g_req − 100 m held every abreast yielder off Λ to
P2 (6/6b failed in 9 of 9 abreast runs); the release at "behind and staying behind" is the moment the merge law released before.
Result: merge completion maxima abreast3 79.7 → 77.4 s, abreast5 100.8 → 77.0 s, slow_hull 117.8 → 125.8 s, crossing 189.7 → 191.7 s (the
design expected ≤ +8 s per merge); assertion 2's bound holds in every run; 0 member 0x12 episodes and 0 uncertified frames in 243 lane
runs; the nose at 7 km abreast3 15.7°, terran_abreast 16.9° (the bounds unchanged); every span within 7.8 s of the table before
(`spans_diff.md`: 24 of 27 casts within 3.3 s; `p2_miss` / `p2_miss_near` +7.8 s at tf 6, the from-rest launch of item 3; `parked`
+3.2 s at tf 6, the engine's detour).

**2. One yield candidate** (`try_yield(g, y, partner, …)`). One rollout per conflict: under the lane the law at its floor (10 m/s, the
cap site B applies) while the plan is above the floor, else the brief stop (cap 0; T_stop + the step from its start, or against an
arrival the arrival window + the step from its launch); key off the floor. `kYieldFrac` / `kYieldMidMs` and the three-rung ladder are
gone; `kMaxRolloutsPerDecision` 7 → 3, `kRolloutsPerFrame` 14 → 6. Deviation from §2 c: the design's "law with the gap target raised by
the sweep's margin" is the lane speed for any yielder whose gap is wide (measured in `overshoot_near`: a conflict 26 s out behind a
returning ship, the law at v_lane under any raise; 432 uncertified frames at tf 1 in 3 of 3 runs), so the candidate is the law's floor,
the raise taken past the horizon. Cost (`ctl cost (host)`, the 24-member ring in conflict): a decision 25.9 → 11.9 µs mean, 31.1 → 13.5 µs
worst, 165 → 71 rollouts per 24 decisions; the worst admitted frame 67.6 → 36.1 µs; bench24 (the lane cost bench, 5 runs) mean
tf 1 10.0–10.3 µs (before 10.3–11.4), tf 10 99.6–111.6 µs (before 128.9–139.8); third-worst frames tf 10 426–1,011 µs against
406–609 before on a host whose runs also gave 10,772 µs under load; a quiet alternation (base and this tree three times each) tf 10
mean 128.5–130.9 → 100.3–102.8 µs, third-worst 513–580 → 379–473 µs.

**3. The hand-over** (`lane_site_b`, `lane_hand_bound`, the rollout's hand-over branch). P2 kept: the live speed under the brake bound
from P2 (the poll horizon's stop 300 m outside the trigger sphere) on every hand-over frame; P2 not kept: no write (`LaneWrite::unkept`
is gone; the vanilla stop, 0x1f, order 5 from rest); the `refused` timer is a counter (`x_counted`: `ctl_x_refused` once per hand-over
beyond 5 s + the step, `ctl_x_wait_s` accrued from the hand-over's 3 s). The lane's hand-over bound is `kLaneRefusedMaxMs` for every lane
hand-over (no flag). Two consequences measured and handled: (a) the design's "the bound cannot bind on a granted slot" does not hold at
d_end (room 898 m for a Titan against 1,100 m of poll travel: 94.8 m/s written at d_end, the curve below as it coasts); a rollout that
followed the bound's stop made every follower behind a hand-over yield at the floor and brake 30 m/s for a launch that came 10 s later
(`formed_stage2`: 0.2 s brief stops, order-0 losses of 100 m/s), so the rollout keeps the kept hand-over at its held speed (the grant
predicted; a refusal shows in the live state the next rollout starts from) while site B writes the bound; (b) with the instant stop of
a P2 not kept, a follower on the 3b gap law ran at 69 m/s 667 m behind and its stop at spacing left 177 m, under the sweep's margin
(`p2_miss_near`: the escort's 0x12 in 9 of 9 runs) — a follower behind a predecessor on its final before its P2 now stays on the braking
curve to a stop g_req behind the predecessor's tail at d_end (`LaneCtx::stop_room`, lifted at the predecessor's P2; in the rollout at its
sample's d_end, since a bound kept past d_end rolled every follower of a ship in its final out to a stop at spacing, `slow_hull`'s joiner
into a conflict). Checks: `3e no site-B write on the hand-over of a P2 not kept` (0 writes in 18 of 18 runs; before: the unit asserted
the write, `v_unkept > 50`), `3e order 5 from rest: the launch within T_slot + 25 s + the step` (18 of 18), the queue hold `3d2` as
before (release within the step of the grant), `7 safety` in `p2_miss_near` (capsule ≥ 369 m).

**4. The arrival pair** (§12.1a). The sweep's axial mode (`sweep_impl(..., axial)`, `axial_plan`): for a pair with an arrival on a
drift-free plan (0x21, 0x1e, the order-0 brake, 0x1f) each axial side's terms — the drift k (t + offset), B's time-offset term, half its
own inter-sample motion — extend its capsule's segment along its own axis instead of the pair's isotropic margin, which keeps kMarginM and
the other side's share; a lane member on Λ within kLaneOnM or past d_end is axial too, a joiner or an arrival on a steered order is not
(fail closed). The 0x1e rollout brakes at a to rest after |dz| > r_gate (r_gate / 4 for a small hull; an unknown radius: straight on).
The DLL's launch hook decides the arrival at once (its rollout before any E verdict against it: at a SETA step the leader's verdict came
one frame before the arrival's first decision, cause=zero, and the leader was evicted 0.4 s after the launch); the fixture's launch models
it and fills the member's state (point P, nose −F) as the hook does — before, the fixture's arrival rolled out along the axis at U 0.
E's verdict for the arrival as A needs no change (`e_certified` has no role filter). The hull-fit fallback is item 2's stop with the
arrival-window bound. `arrival_pass` un-parked: 9 of 9 green (checks: `Ex` contact, member 0x12, `no member-arrival pair handed back on
contact`, `a stop against an arrival within the arrival window + the step`): 0 member 0x12, 0 pair hand-backs, departing capsule ≥ 481 m,
3/3 launched, spans 298.2–299.0 / 279.0–279.5 / 281.6–282.0 s (before: 8 of 9 runs with member 0x12 and contact to −1,157 m, 2 with
launches missing). `arrival_parked` stays parked (the fan, §12.1b): with the change still 3 of 3 runs per tf with the two arrivals
overlapping (−1,180 m), member 0x12 (3–12 flown per run), contact (−809…−1,116 m), launches 0–3 of 3; `pair_contact` 0 in 9 of 9 (the
hull-fit pair is never handed back; the overlap and the resting M1 at P are the fan's class).

**5. Retired** (the ledger lists them): `Member::merged`, `Member::refused` (→ `x_counted`, a counter's state), `LaneWrite::unkept`,
`lane_refused_hold` (→ `lane_hand_bound`), `kLaneTauMergeS` / `X3M_LANE_TAU_MERGE_S`, `kLaneBesideM`, `kYieldFrac`, `kYieldMidMs`,
`lane_law`'s `merged` parameter; new: `Member::band`, `LaneCtx::stop_room`, `lane_band_free`, `axial_plan`, the sweep's `axial`
argument, `try_yield`'s partner, the launch hook's decision. Rows: `ship_gate_ctl_lane` unchanged in form (`w=` without `unkept`,
`merged=` the merge's completion), `ship_gate_ctl_merge` and `ship_gate_ctl_pair` unchanged; the install row's `yield=` carries the one
floor.

**Fixture bounds re-taken.** `parked`'s nose at 7 km 25 → 27° (the engine's sphere detour around the M7 parked at the 7 km point:
12.9–25.5° over 9 runs, 19.8–23.5° before). The stage-A unit `ctl decide: the lower-priority approach yields ... (cap = the floor)`.

### Round B (small ships, statics on Λ) (2026-10-15, Opus 5.5; §12.2 and §12.3, each item with a check that fails on 71b1536d)

Worktree `agent-ad06636d182ac36ce` on main 71b1536d, in parallel with round A. Units: `ship_gate_arrival_core.h` (§12.2), `ship_steering_decide.cpp`
(the small-ship lead-in, D.5, the hold's clamp, the avoidance seam), `ship_gate_geometry_core.h` (`lane_lat_at`, the profile), `ship_gate_board_core.h`
(`LaneProfile` in `Gate`), `ship_gate_ctl_live_core.h` (`lane_bodies`), `ship_gate_ctl_glue.cpp` (`lane_profile_gate`, 1 Hz), `ship_avoidance_core.h`
(`Self::skip_static`), rows (`ctl_small=`, `ctl_prof=`, `ctl_prof_us_max=`). The key off is unchanged by construction (every new path tests the gate's
lane key): the key-off control line of `--lane` is byte-identical to the base's (45 runs, 660,747 certified frames, contact on them 0).

**Item 1, small ships (§12.2) [m].** `small_gate` and `small_gate_m6` un-parked: no small-ship contact in 9 of 9 each (71b1536d: 4 of 9 each, to −506 m),
the closest small-ship pair 145–328 m; the members' launches 3/3 and spans unchanged to the decisecond (217.0 / 215.9 / 209.3 s at tf 1, 188.8–192.1 s
under SETA), member 0x12 0, P2 nose ≤ 1.1°. The small ships' 0x12: 0 per run in `small_gate` (71b1536d: 1–3, 4–57 s), 1–2 per run and 1.4–11.6 s in
`small_gate_m6` (71b1536d: 2–5, 10–84 s); D.5 `both_taken` 0 on the lane; holds 3 per run, the longest 55.2–60.0 s (the bound 60 s + the step; 61.8 s in one earlier run at tf 6).
Unit checks: `round B small units` (the free side, the line's end ≤ 772 m from A against 829 m, ≥ 753 m from the column for every spread index,
the latch, D.5's `hold` / `edge` / key-off `both_taken` / own-side mirror, the bound at the step 2 s, the stagger). Deviations from §12.2, each a fixture witness:

| Design | Implemented | Why (fixture) |
| --- | --- | --- |
| side lead-in while the funnel is active | from any lane member of the gate (committed or escort), side latched per approach | a lead-in that went back to the spread line between two capitals ran the TS into the column |
| X_s = A + 0.7 r R | A + 0.68 r R + one of four U levels (0.107 r apart, ≤ 0.16 r) | two TS on one side converged on one line, warned each other, their 0x12 met the column; 0.68 keeps the end 57 m inside the arrival sphere |
| free side away from the capital's Q | the same off the axis; with Q on the axis (the lane's finals) the ship's own side | no column crossing (both halves are free) |
| D.5: B, mirror, edge (points) | the lateral path from the ship to the point with a 200 m margin; the hull's front counted ((L + r + 50 m) / v before its passage) and unreleased lane members; B kept on the latched side | the order-5 leg's lag carried TS/M6 170–360 m inside; a capital whose centre was 1 km out was not counted |
| hold: v ≤ d / (t_launch + 2.9 s − now), floor 10 m/s | held where it is at 1 m/s while X_s lies in a counted capital's warn zone (1.2 (w + r) + 150 m) or the order-5 path is taken; nose along the line; no sphere detour against the lane's capitals while held; ETA at v_max + the acceleration distance, a 3 s gap margin; release staggered 2.5 s by spread bit 2; 60 s + the step per approach | at 10 m/s a TS drifted 600 m past its line's end and turned into the column; paced to a park it circled, nose at the next capital (doubled closing term, 600 m reach), and its 0x12 met it; a slow M6 released 2 s ahead of a Titan's hull overshot 260 m |

**Item 2, statics on Λ (§12.3) [m].** The profile is implemented and unit-checked, but its **shift is off** (`gc::kLaneProfShiftOn = false`); only the
enclosing-fallback rule is active.

* `lane_fallback` un-parked: 9 of 9 green (71b1536d: span 9 of 9, launches 2 of 9). Spans 209.3–217.0 s at tf 1, 188.8–192.1 s under SETA (the formed
  bound), P2 kept (nose ≤ 1.1°), lane v_min 40.1–80.6 m/s, member 0x12 0, avoidable 0, launches 3/3. The 30 km fallback sphere encloses Λ's d_end point:
  listed, neither shifted nor detoured; the engine's parts are 12 km off Λ (no warn).
* `lane_station` stays parked: **no feasible shift** under §12.3's rules. The four 300 m parts on Λ need 929 m of clearance (w 579 + 300 + 50); a 15° ramp
  tangent to the last part's clearance circle reaches 0 at s = −3,299 m against the blend's start at −5,432 m (−3,432 m even with the blend zone given up),
  133 m short at the 50 m margin; at margin 0 it is 60 m inside. `X3M_C_PROFSHIFT=1` builds no shift there.
* `lane_belt` stays parked: with the shift on (`X3M_C_PROFSHIFT=1`) the profile shifts −U 898–932 m at the 50 m margin (four knots, ramps at tan 15°,
  0 from −5,432 m on) and **regresses** the cast: at tf 1 a member passes a rock at the 50 m margin and touches it (3 of 3), under SETA site E's suppression
  of the profiled rocks fails closed (`pass_not_fresh`, and `pass_not_resolved` for escorts on 0x14 before the skip covered them), the members fly the
  engine's 0x12 (2–6 per run in round_b/shift_lane_belt.txt) and meet each other (to −879 m). With the sphere detour (the shift off) the cast stays at 71b1536d's 1 of 9.
* Cost **[m]**: one build of 64 bodies with the shift 5.6 µs host (the unit), 0.2–2.8 µs mean per build in the casts (max 17.6 µs, one host hitch);
  `lane_lat_at` 1.1 ns without a profile, 3.0–5.4 ns with a four-knot one (host, 2 × 10⁶ calls). bench24 tf 10 (no statics): mean 130.7–139.9 µs,
  third-worst 497–689 µs over three runs against 71b1536d's 130.2 µs / 673 µs (one run; within the run-to-run spread).

**Item 3, the key off.** Unchanged by construction; the control line above is byte-identical to the base's. The fixture runs no key-off control for an
engine cast (`su.eng`), as before.

**Acceptance [m]** (host x86-64, `c++ -std=c++17 -O2 -Wall -Wextra -Werror`): `--lane` 3,634 / 0, full 10,979 / 0, `--capital` 5,357 / 0;
`test_ship_trace_format`, `test_ctl_boundary_reads`, `test_ship_steering` 21 tests OK; i686 RelWithDebInfo 0 warnings, `check_no_x87.py` PASS (999
reachable, 0 violations), `verify_ship_steering_sites.py --exe /mnt/c/X3/X3AP.exe` PASS 20. Evidence: [`round_b/run.sh`](../../verification/results/ship-steering/convoy-gate-transit/round_b/run.sh)
→ `lane.txt`, `<cast>.txt` (un-parked), `parked_<cast>.txt`, `shift_<cast>.txt`; `summary.py` → `summary.txt`.

### Open

* The escort gate's cost at tf 1 (Review fixes item 1: a median 11.4 s per leader → escort pair over the hull spacing, up to 19.6 s
  for λ near 17 s) stays until stage 2 (λ ≤ 1 s) or a shorter order-5 leg; the flight decides whether it is visible.
* The tick deferral (item 2) writes `f+0x210`; its premises are static RE facts (§10.11 Q4, §10.12 item 2, §10.13), not yet seen in flight: the
  first flight's `ship_gate_ctl_rewrite site=b` rows and `ctl_lane_defers` / `ctl_defer_rewrites` should show escorts' order-4 points
  rewritten before P2, and no `site=miss` rows.
* A non-member parked on Λ (cast `parked`) is left to the module's obstacle avoidance on the engine's warn: the convoy detours
  185–191° and P2 is not kept; the key-off baseline is worse (stage B turns 874–994° and launches none in 1000 s; Final review fixes
  item 5). A lane-aware detour (or eviction rule) for non-members is a later item, with the cross-board certificate.
* The order-0 straight-run hold relies on §10.11 Q4 (remainder consumed, then straight); the fixture's plant models it, the flight's
  `ship_gate_ctl_lane` rows on order 0 should confirm the held speed and lateral.
* Crossing tf 6 passes with 1.9 s of margin against the floor + 10 s.
* (lane fix round 1) The own-gate row's hand-over branch trusts the final's window through a ring's hole (q_point's Terran case); the
  installed ordinary gates have no collision body (§10.9), the stock rings (0.605 r hole, Argon) are not certified for an M2 final.
  The engine-model warn covers committed members on order 4 / 0 / 0x1f; whether `CollisionWarn` acts on escort orders 0x14 / 0x15 is
  not modelled (Run173: no own-gate 0x12 on 0x14).
* (lane fix round 1) The bench check (third-worst frame under 1 ms) is host-timing; it failed under another process's load (ledger).
* (lane fix round 2) **A lane entry from behind the plane stays a later item** (cast `behind_start` parked: §11 item 7). A ship
  committed 2 km behind the plane on Λ (nose −F) while a Titan departs: as a member (the tree) its law stops it at +1.0 km until the
  Titan launched, then it flies Λ through its own order point's sphere (P2 653 m out, nose 172°, capsule −825 m at tf 1); with the
  overshoot's geometry (`behind_start_attempt.diff`) no contact but 2–11 member episodes (an M1's pitch builds slowly: ~1 km of
  descent in 30 s, while the departing Titan's rollout occupies the corridor within ~1.5 km of its final); held out of the lane
  (`behind_start_exclusion.diff`) 0 member episodes but a contact at tf 1 and the Titan's P2 not kept (its avoidance against the
  head-on non-member). Next: a hold for such a ship well clear of the corridor (behind the gate ≥ ~3 km, or ≥ 1.5 km off it) before it
  crosses, with the departing member's avoidance aware of it (the cross-board certificate).
* (lane fix round 2) **Item 6 remainder**: the 15/8 km merge-point hysteresis (the merge point is stateless; a ship near a candidate's
  boundary can step between 15 and 8 km), the before-checks for round 1's blend end, stop-law release, re-commit hysteresis and unkept
  write (their round-1 units need the round-1 core; a scenario check failing on dd07e95e exists only for the unkept write, `3c`), and
  the 3c/3d comment. The nose at 7 km is bounded per cast above 5° where the geometry cannot reach it (item 8).
* (lane fix round 2) The escort's stop at d_end on the Terran ring (item 4: the crawl's 200 m budget behind the leader's 43–49 s slot) is
  bounded at the measured maximum (review fixes item 9), not removed; the order-5 leg from the Terran d_end (4,147 m) costs ~25 s per
  launch against the 1105 m gates.
* (lane fix round 2) The obstacle row (item 1) against the engine's box test 0x0044d9c0 (measured, ship-navigation-ai.md §11.2) is
  modelled with the box's corners (review fixes item 11); the flight's `ship_avoid_e_verdict ... obst_row=` rows and `e_lane_obst` should
  show stations and asteroids beside the lane suppressed and none of `roll` turning into a contact.
* (round 2 review fixes) **The row's 30 s horizon refuses early warns against a parked non-member on Λ**: with the box's corners the
  `parked` cast's members are warned from ~3.2 km, where the row's path (the detour, then the rollout back onto Λ beside the parked ship)
  misses its margin near the horizon's end (−20 m against 335 m at 28 s) while the flown, re-planned path clears by 0.8–1.5 km: 1–3
  avoidable 0x12 per run, bounded at 3. A horizon cut at the engine's warn reach, or a lane-aware detour for non-members (above), is a
  later design item.
* (round 2 review fixes) **Long hulls on the Terran ring** (review fixes item 10): d_end's 1.3 km hull is exceeded by 40 of 131 installed
  capitals (to 2,998 m for a flyable M2); such a member is guarded (an eviction), not suppressed. A per-member d_end or a cast with a
  2–3 km hull on the ring is a later item.
* (round 2 review fixes) Later casts and checks, not done: a refusal longer than 300 s (`kLaneRefusedMaxMs`); the leader killed during the
  queue hold; an exit member stopped until the 60 s bound (`kLaneExitMs`); check 2r2 asserting `exit_clear`; a cast with a non-member
  ship changing course. Cost: a per-ship per-frame path cache at site E (the obstacle row builds the member's path on every warn call:
  1.9 µs host for a detour turn).
* Next: the arrival fan (§6.7), the cross-board certificate (§6.6).
* (fixture engine paths, "Fixture engine paths" above) **Nine parked casts, each a controller item** (`// PARKED:` in `kCSetups`, re-run by
  `engine_paths/run.sh`):
  * **B** (`x12_near`, `x12_far`): after a flown eviction the re-issued member's re-departure reaches its final off it (P2 not kept or above
    8° in 2 and 5 of 9 runs), and its rejoin ahead of a follower leaves that pair uncertified (a second, member 0x12 in 2 of 9).
  * **C** (`lane_station`, `lane_fallback`, `lane_belt`): the module's sphere detour (§1 rows 5 / 11 of the holistic review) — spans over the
    formed bound, a fallback body detouring the lane ~5 km off Λ with P2 never kept and an order-5 pass outside the trigger; the lane-aware
    lateral shift (holistic rank 7) is the fix to try.
  * **D** (`small_gate`, `small_gate_m6`): small ships' order-5 lower-arc legs cross the capitals' order-5 line (contact in 4 of 9 runs);
    D.5 finds both sides taken on the lane's final.
  * **E** (`arrival_parked`, `arrival_pass`): no arrival fan (two arrivals overlap at P) and an arriving hull's box warns the departing
    leader at d_end while the board's pair is uncertified (eviction, then the column's contacts).
* (fixture engine paths) Model items left **[i]**: the avoid order's escape-direction rule (a must-turn-away obstacle blocks unless the
  candidate points away), the start-point refresh, the 0x0042fb20 speed map (taken as the cruise speed), the small escorts' formation slot,
  the lower arc's θ draw, a capital's hull box (w, w, L) as its first model record; the small ships' own-gate warn is not modelled; the
  key-off controls stay on the counted 0x12 (with flights `crossing` tf 10's key-off control launches 4 of 5 in 1000 s).

## 12. Engine-path fixes (design)

Ratified (orchestrator, 2026-10-15, under the user's standing overnight instruction): round A = rank 4 (one gap law + lateral band, one yield candidate, no-write hand-over) with §12.1a (the arrival pair) on one worktree; round B in parallel = §12.2 (small ships: lead-in on the free-side edge, D.5 `edge`, bounded hold) and §12.3 (the lateral profile for statics on Λ) on another; round C = §12.4 (the loop-back join after a flown 0x12) after A; the arrival fan (§12.1b), behind_start and the cross-board certificate stay deferred. Each round: fail-before checks on the parked casts, two reviews for the law and the certificate changes, no install before the user's Run174 flight is triaged.

Design note for the nine parked casts of §11 "Fixture engine paths" (2026-10-15, HEAD 305aa2b6; read-only except this section; no
Wine, nothing launched). Each class below gives the engine mechanism ([ship-navigation-ai.md](../reverse-engineering/ship-navigation-ai.md)
§3.3, §10.3, §10.10, §10.12, §10.14), the controller change (law, geometry, bound; every time bound in game time + the SETA step through
`age_bound`), its hot-path cost, the fail-before check in the cast, the flight risk and what stays open. Units named as in
[ship-steering-and-avoidance.md](ship-steering-and-avoidance.md) §8.1. Binding user decisions (§0 and the holistic review's ratification)
are taken as constraints: capitals ignore obstacles under 33 % of their hull; leader goes, escorts go, no waiting; the car-merge rule;
player-owned AI ships are members; SETA-aware bounds; stage 2 is script constants only; no second member class for small ships.

**Ranked plan.** The brief's order 1–4 stands, with class 1 split: the arriving hull against the departing member (1a) is the top item and
does **not** need the arrival fan; the fan (1b) is last, because it cannot separate the hulls the cast overlaps. Rank 4 (holistic §2 b–d)
goes first, with 1a in the same round; 2 and 3 are law-independent and run in parallel with it; 4 follows rank 4.

| Round | Classes | Units touched | Why this order | Hot-path cost |
| --- | --- | --- | --- | --- |
| A | rank 4 (one gap law + lateral band, one yield candidate, no-write hand-over, brake bound from P2) **+ 1a** (the arrival pair) | `ship_gate_lane_core.h` (`lane_law`, `lane_site_b`, `lane_refused_hold`), `ship_gate_certificate_core.h` (`try_yield`, `rollout`/`sweep` for `Role::arrival`), `ship_steering_warn.cpp` (E's verdict on the arrival's frames) | 1a's fallback is the yield's stop candidate, which rank 4 collapses to one; 1a's main part (the pair's certification) is a sweep branch independent of the laws. The user's complaint (the turn-away at d_end) is this class | removes work (one law, one candidate, no `refused` timer); + one branch per arrival sample in the sweep |
| B (∥ A) | **2** small ships, **3** statics on Λ | 2: `ship_gate_arrival_core.h` (D.5), `ship_steering_decide.cpp` (the small lead-in), `ship_steering_clamp.cpp` (the small hold); 3: `ship_gate_geometry_core.h` (`lane_lat_at`), `ship_gate_lane_core.h` (`lane_frame`'s profile), the avoidance seam in `gate_decide` | Neither touches a speed law or the hand-over; different files from A (worktrees) | 2: one `funnel_active` scan (≤ 24 members) per small-ship order-4 decision; 3: a 1 Hz static scan per gate (~20 µs **[i]**) on the frame boundary, 4 compares per `lane_lat_at` |
| C | **4** the flown 0x12 (the loop-back join, rank by ETA) | `ship_gate_geometry_core.h` (`lane_carrot`'s redep branch), `ship_gate_lane_core.h` (`lane_frame`, `lane_eta`), `ship_gate_board_core.h` (the re-commit flag) | Needs rank 4's hand-over semantics (the near case falls back on "no write") | none per frame; a flag at re-commit |
| D (deferred) | **1b** the arrival fan (§6.7, hull-fit guarded), the `behind_start` hold, the cross-board certificate | the launch handler `x3m_ship_gate_arrival` | Rare (in-sector arrivals ≥ 60 s apart unless A6) and it cannot separate hulls wider than 468 m (below) | once per launch |

One install candidate per round (A+B together if both review clean), the next flight after A+B.

### 12.1 Class 1: arrivals during a departure (`arrival_parked`, `arrival_pass`)

**Mechanism [m]** (§10.10, §10.14 Q1/Q5; the fixture's E). The partner side's through slot grants; the launch `0x00444170` puts the hull
at P + 2697 m F on order 0x21, which writes its position each frame for 900 ms and zeroes `[obj+0x10]` / `[obj+0x14]`, ending at P at rest
facing −F; `__LeaveHyperspaceWarpIn` then issues 0x1e: 120 m/s (large set) along the hull's own axis-parallel line until |dz| > r, with no
speed write at the end, so order 0 brakes it at `a` (a Titan: 12 s, 720 m) until the script's next order (transit: 0x15 / order 4 one pass
later; a squadron escort at its destination: 4–7 s **[i]**). A hull with r_s > 0.99 r ends 0x1e on its first frame and rests at P. P is
+0.6 r U on an ordinary gate (`capital_offset`; the Terran bound only on rings), i.e. +663 m; Λ lies at l_c ≈ −0.84…−0.93 r (`lane_q`,
≥ 0.75 r + 100 m below the axis): the lateral gap is ≈ 1.6–1.7 km against w + w = 1,166 m for two Titans, ≈ 450–550 m clear. The
fixture confirms it: the closest departing hull on 4 / 0 / 0x1f / 5 to the parked M1 is 58–297 m (§11), no contact. What evicts the leader
is the engine's **mesh warn**: the arriving hull is a capital B, so the box test `0x0044d9c0` runs with per-axis half-extents
H_i = 1.2 (e_i + r_A): for a Titan A (r_A 1,092 m) the box around the arrival reaches ≈ 2.0 km laterally and ≈ 2.9 km along F from P,
i.e. over d_end and the whole hand-over; the box is a distance test (the slab never rejects), so the departing member's heading does not
matter. Our rows do not suppress it: from the launch the arrival is a board member (`Role::arrival`, the launch hook), so the pair goes
through the certificate, whose sweep charges the arrival the rollout drift k (t + age) = 10 m/s per second (350 m at the 30 s horizon) in
every direction, which eats the 450–550 m and refuses the pair (`cause=contact`); during 0x21 the modelled B velocity (3 km/s) does the
same. The eviction is then flown (turn 128–179°, 93–328 s on 0x12) into the followers: member contacts to −1,120 m, fewer than 3 of 3 launched in
1000 s in 5 of 9 runs. Two arrivals 60 s apart overlap at P (−1,180 m) because the second launches into the resting first.

**Controller change (1a).**

* **The arrival's plan has no lateral freedom.** An arrival's rollout (`Plan::warp_in`, the 0x1e leg, the order-0 brake, the rest at P)
  is engine-written geometry: the sweep's drift for a `Role::arrival` member applies **along F only** (its lateral is P's, exact), and the
  order-0 phase brakes at `a` to rest instead of "straight on at 120 m/s". With that the (departing member, arrival) pair certifies on the
  real 450–550 m clearance for every census pair with w_arr + w_dep + kMarginM + k·t_pass ≤ 0.6 r + 0.75 r + 100 m (t_pass the member's
  time to pass the arrival's line, ≤ 10 s: two Titans need 1,166 + 150 m against 1,591 m).
* **E's certified verdict covers the arrival's own frames** (A = the arrival on 0x21 / 0x1e / 0 against a certified member B): after 0x1e
  the KC issues 0x12 if the warn flag still stands (Q5 item 1), a 170° turn-away of the arriving hull at the gate. `lane_obst_why` is
  committed/escort only; the certified-pair verdict (`e_certified`) must be reachable for `Role::arrival` as A. Nothing is written to the
  arrival (the role never yields, §6.2).
* **Fallback when the hull-fit fails** (the sum above exceeds the corridor): the departing member is the yielder and takes the single
  yield candidate's **stop at d_end** (rank 4's one candidate: the law with the raised gap target, else the stop) until the arrival has
  left the board or its line: bound = the arrival window `kArrivalWindowMs` (60 s) + the step; after it the arrival is a non-member
  obstacle (the obstacle row + the sphere avoidance, the `parked` behaviour). No eviction of the departing member is ever the controller's
  own choice for this pair.
* **0x21 (900 ms)**: the pair is certified or not on the same rule; no special case. If the EXE's warn cannot fire during 0x21 at all
  (below, open), nothing is lost.

**Hot path.** One branch per sample in the sweep for an arrival member (the drift vector), on the 200 ms decision cadence; E's verdict
lookup for the arrival's frames is the existing pair lookup. No per-frame work.

**Fail-before.** `arrival_pass`: "Ex no member 0x12 episode between members" and "Ex no contact between the departing capitals" fail in
8 of 9 runs today; add "no departing member leaves on `cause=contact` with an arrival as B while their hull-fit holds" (0) and "a
hull-fit stop at d_end lasts ≤ 60 s + the step" (a third arrival cast with two 700 m hulls). `arrival_parked`: the member contacts
(−807…−1,120 m) → 0 and launches 3/3 in 9 runs; the "no two arrivals overlapping" check is re-scoped to hull-fit pairs (12.5's 1b).

**Flight risk.** The arrival's script order after 0x1e (0x15 / order 4 toward the next gate, possibly across Λ) comes 1–7 s after the
brake begins **[i]** timing; the member keeps the role 60 s with the rollout on the live order, so a turn across Λ is predicted from the
live velocity with the full drift (fail closed: `roll`, the engine's warn stands). A suppressed warn on the arrival's own frames whose
certification was stale (> 250 ms + step) falls back to the engine by `validity`.

**Open.** (i) Whether the EXE warns during 0x21 at all: 0x21 zeroes `[obj+0x10]`, but the "B behind" rule also needs the physics
velocity `[B+0x60..0x68]` to be 0, and what the kinematic `p+0x30` writes leave there is **[i]**; a `ship_avoid_engine_warn_pre` row with
B on 0x21, or a read of the 0x21 handler's velocity writes, settles it. (ii) Q5's hold times in flight (`ship_gate_ctl` rows with
`role=arrival` and the orders seen after 0x1e). (iii) The fan (1b, 12.5).

**Implemented (round A, 2026-10-15; §11 "Round A" item 4).** The sweep's axial mode (the arrival's drift, time-offset and inter-sample
terms along its own axis; a lane member on Λ or past d_end axial too; a joiner or an arrival on a steered order isotropic, fail closed),
the 0x1e brake-to-rest after |dz| > r_gate, the hull-fit fallback as the single yield candidate's stop with the arrival-window bound;
`arrival_pass` 9 of 9 green, `arrival_parked` parked for the fan. Deviations: (a) E's verdict for the arrival as A needed no change
(`e_certified` has no role filter); (b) the DLL's launch hook decides the arrival at once (one rollout and the sweeps per capital launch,
~12 µs **[i]** from the 24-member bench's conflict decision) — without it a departing member's verdict against the arrival on the launch
frame found no rollout (cause=zero) at a SETA step and the leader was evicted 0.4 s after the launch; (c) the fixture's arrival model
gained the hook's state fill (the point P, the nose −F; before, its warp-in rolled out along the gate axis at U 0, 685 m off P) and the
launch-frame decision. The DLL's arrival member has no a_lin (`JoinParams{}`: the brake models 1 m/s², 7.2 km of coast along −F, which
keeps the lateral exact and the along-F reach conservative); open (iv).

### 12.2 Class 2: small ships at the same gate (`small_gate`, `small_gate_m6`)

**Key-off check first [m]** (this session; the committed `engine_paths/run.sh` never runs the control for an `eng` cast, so the stored
evidence has no key-off row). A scratch copy of the fixture with the `// PARKED:` lines enabled and the control branch opened for
engine casts (`sed 's#if (su.span_form && !su.eng) {#if (su.span_form && (!su.eng || std::getenv("X3M_C_CTLENG"))) {#'` plus a
`lane_c_print_eng` call after the control's `lane_c_print`; `X3M_C_ONLY=<cast> X3M_C_ENGALL=1 X3M_C_CTLENG=1 ./parked --lane`), seed 1:

| Cast, tf | lane on: small capsule, small stops, 0x12 s | key off (stage A, the counted model): small capsule, small stops | key off: members' capsule |
| --- | --- | --- | --- |
| `small_gate` 1 / 6 / 10 | −314 / +54 / +15 m; 1 / 1 / 1; 15.8 / 4.0 / 10.2 s | +75 / +303 / +85 m; 0 / 25 / 11; 0 (counted) | −254 / +87 / +83 m |
| `small_gate_m6` 1 / 6 / 10 | −312 / +218 / +15 m; 3 / 3 / 3; 76.8 / 12.4 / 10.0 s | +156 / +172 / +85 m; 347 / 87 / 12; 0 (counted) | −254 / +87 / +83 m |

The contact is **not pre-existing**: stage A keeps the small ships clear (the capitals fly the KC grid points and order 5 from X, not the
column), at the price of 11–347 clamp stops per run; stage A's own capital contact at tf 1 (−254 m) is the known baseline weakness.
Caveat: one seed, and the key-off controls keep the counted 0x12.

**Mechanism [m]** (§10.3, §10.5, §10.14 Q2, the fixture's D). Every non-capital's order-4 point is A = G + (0, −0.8, −1) r (884 m
below the axis, 1.1 km in front; the arrival test at 0.75 r), its order-5 pass point B on the lower arc (radius 0.55 r, θ within ±45° of
−U, z = −0.01 r), then P4 and order 8. The module's lead-in line is shifted by the 0.25 r spread, A is not. The lane's column at
l_c ≈ −0.84…−0.93 r therefore runs **through A** and ends at Q just below the arc's middle. A capital ignores a TS (Q2: not in the large
set) and, on order 5, every warn (the KC's ignore mode); the TS on its own order 5 is in ignore mode too; its own warn against the capital
(close range d ≤ 1.2 rr, since r_B ≥ 0.75 r_A; then the box) fires late. D.5 mirrors B across U only when the mirror is clear of the
counted capitals' Q by w + r_small + 50 m: for a Titan Q (w 583 m) the nearest arc point is 0.40 r (θ = 0) to 0.68 r (θ = 45°) = 442–751 m
away against 583 + 38.5 + 50 = 672 m for a TS (r 38.5 m) — both halves taken except at the arc's edge, which the engine's θ draw rarely
hits: D.5 `both_taken` 4–1,453 per run, `mirrored` 0. The capital will not dodge (binding); the small ship must.

**Controller change** (the small-ship clamp and D.5; no member class, no new engine write).

* **The side lead-in.** While the funnel is active (`funnel_active`: a member on Λ's last 6 km or on order 5 / 8; its `side` = the side
  away from that capital's Q), a small ship's order-4 leaf (site A, `gate_lead_in`) ends on the **free-side edge of A's arrival sphere**:
  X_s = A + side · 0.7 r R, 773 m from the column against the 672 m a TS needs beside a Titan (an M6, r 64 m: 697 m). The engine's
  arrival test (0.75 r from A, re-tested every tick) fires there; A itself is not moved, so nothing new is written to the engine.
* **D.5's third verdict `edge`.** When B's mirror is taken, B is rotated to the free side's arc edge, |θ| = 45° (the arc radius and height
  kept, the engine's spread given up): 751 m from a Titan's Q, clear for a TS (672 m) and an M6 (697 m). The order-5 leg from X_s to B_edge
  runs down the side, ≥ 0.68 r from the column.
* **The hold** (the car-merge rule: the later-arriving small ship yields briefly): when the edge is taken too (a capital with w > 0.68 r
  − r_small − 50 m, i.e. wider than 663 m beside a TS: no census hull; an M8/M6 escort beside a Titan can be), site B paces the small ship's
  order-4 / order-5 speed so that it reaches the plane after the last counted capital's predicted launch: v ≤ d / (t_launch + 0.9 s + 2 s
  − now), floor 10 m/s; bound: t_launch is `eta_launch` (game time), the hold ends at that time + 5 s + the step, or at 60 s + the step
  after it began (then the small ship goes and the engine's warn is its backstop). Small escorts hold where they are (1.2 km beside the
  column), never on the column.
* Site E's small-ship row (the members' warns against an M6, 663–18,282 per run, all suppressed, 0 member 0x12) stays.

**Hot path.** `funnel_active` (≤ 24 members) once per small-ship order-4 decision at the think cadence, where D.5 already calls it on
order 5; the edge rotation is two multiplies. Nothing per frame.

**Fail-before.** "Dx no contact with a small ship" fails 4 of 9 in both casts; add: D.5 `edge` + `hold` > 0 per run and `both_taken` 0
on order-5 decisions, the small ships' stops ≤ today's (1–5), and the small ships' launches within the members' span + 60 s + the step.
A third cast with the TS escorts on the funnel's own side (their 0x14 slot 1.2 km on the capital's Q side) exercises the crossing.

**Flight risk.** A small escort whose 0x14 slot lies on the funnel side crosses the column once on its way to X_s: the sphere avoidance
(1.75 C, moving) handles it as today. The engine's container detour (√5 r) may replace the leaf point inside 2.47 km (§10.12 item 2): the
arrival test still fires at 0.75 r from A. Small arrivals keep the upper fan and the side spawn (D.4): the +U half is untouched.

**Open.** The 20–100 m margins at the arc's edge are formula values; the fixture measures the closest approach per hull pair. The
alternative of writing A itself to the side (below, 12.6) stays in reserve if the edge margins fail for Titan-wide capitals.

**Implemented (round B, 2026-10-15; §11 "Round B").** The side lead-in, D.5's edge and the hold, with deviations each measured in the fixture: the
lead-in starts with any lane member and keeps its side, ends 0.68 r beside A at one of four U levels; D.5 tests the ship's lateral path (200 m margin,
the hull's front and unreleased lane members counted) and keeps B on the ship's side; the hold holds the ship where it is at 1 m/s (not the paced
10 m/s) while a counted capital's warn zone covers X_s or its order-5 path is taken, nose along the line, no sphere detour against the lane's capitals,
released in gaps and staggered, 60 s + the step per approach. `small_gate` / `small_gate_m6` 9 of 9 each.

### 12.3 Class 3: a station, a fallback body and a rock belt on Λ (`lane_station`, `lane_fallback`, `lane_belt`)

**Mechanism [m]** (§10.14 Q1; ship-steering-and-avoidance.md §3.3–3.5). The engine warns a member against a station through the part
boxes (1.2 (e + r_A) per axis, reach |v_A| + b) and against a rock through the close range (a B that cannot move: d ≤ 1.2 rr) and the
cone; site E's obstacle row suppresses that when the member's path clears the parts (or the sphere) by the margin, and none of the three
casts shows a member 0x12 from the warn alone (`lane_fallback` x12 0: the engine sees the real parts, none on Λ). The damage is the
**module's own detour**: the swept sphere C = r_own + r_eff + max(25 m, 0.5 r_own) along L, the detour point x_obs + ê' · 1.3 C (1.75 moving)
in the plane of least turn; r_eff is the hit part's far face when the part walk succeeded, else 0.35 × the bounding sphere floored at
500 m. For the 15 km fallback body r_eff = 5.25 km and C ≈ 6.1 km: every member is carried ~5 km off Λ, passes `kLaneOffM` = 1.5 km and
becomes a joiner again (the 45° residual join), so P2 is never kept (19.7–31.3°), the spans run 30–50 s over, and an M1's order 5 passes
the plane outside its trigger (2.1 km behind the gate at 10 m/s, its follower held at d_end). `lane_station` is the same with a 1.5 km
sphere (lane v_min 2–72 m/s, one 8-episode cascade at tf 10); `lane_belt` brakes one member to 2 m/s at the rocks.

**Controller change** (holistic rank 7, made concrete).

* **Statics on Λ become a lateral profile of the lane, not a per-ship detour.** Once per game second in `lane_frame` (and on a static
  census change), the board scans the module's static snapshot for bodies (station parts from the part cache, rocks; a station without
  parts: its fallback body) that intersect the column: a corridor of half-width w_max + r_body + kMarginM around Λ over
  s ∈ [−15 km, −(d_end + settle + 2 km)] (the blend zone and the finals are never shifted). For each it takes the smallest lateral shift δ
  in the (R, U) plane that clears the body — **−U first** (the departure side; +U is the arrival corridor), then ±R, never +U within 6 km of
  the gate — and builds ℓ(s) = l_c + δ(s): piecewise linear, ≤ 4 segments, ramps ≤ tan 15° (500 m per 2 km, the finals' rule), rounded
  to the window `lane_q` allows. `lane_lat_at` reads the profile; every member, every rollout and the obstacle row's path then fly the
  same shifted line, `lane_on` measures against ℓ(s) (no drop), the sweep certifies one plan, the lane speed is kept (no brake).
* **The sphere detour is off for a lane member against a body the profile clears** (the candidate is dropped at the avoidance seam in
  `gate_decide`, where `lane_ours` already gates the leaf point); it stays on for everything else (moving ships, parked non-members,
  bodies outside the scan) and the engine's warn stays the backstop in all cases.
* **The fallback body.** Without parts the profile cannot be computed against the real shape. Rule: a fallback sphere that contains Λ's
  d_end point or the gate (the lane runs inside it: `lane_fallback`) is **neither detoured nor shifted** by a lane member — the detour has
  nowhere to go (d < C everywhere) and the engine tests the real parts; a real part on Λ then costs a vanilla 0x12, the backstop
  ship-steering-and-avoidance.md §3.5 item 3 accepts. A fallback sphere that merely crosses the column is a body of radius r_eff
  (0.35 r, floor 500 m) in the profile, shifted only if the window has the room, else left to the engine.
* **Rocks** are bodies like parts; a belt denser than the corridor takes the widest gap inside the window, else no shift (the emergency
  ramp at site B, √(a (d − C)), stays as today).

**Hot path.** The scan: ≈ 23 stations × 64 parts + 194 rocks ≈ 1.7 k segment–corridor tests per gate per game second (≈ 20 µs host
**[i]**) on the frame boundary, under the 1 ms ceiling, not per decision; `lane_lat_at`: ≤ 4 compares and one lerp per call; the
avoidance seam: one flag test per candidate. i686 cost unmeasured (holistic row 13).

**Fail-before.** "Cx the span" (2 / 9 / 1 of 9), "every ship launched" (`lane_fallback` tf 1: 2 of 3), the tf-10 cascade in
`lane_station`; add: P2 nose ≤ 5° (today 4.1–31.3°), `lane_on` never dropped by a static, 0 module detours against a profiled body, lane
v_min ≥ 60 m/s in `lane_station` / `lane_belt`, and a unit for the −U-first choice and the +U refusal within 6 km.

**Flight risk.** A part the walk missed (parts with model id < 0, or extents beyond the first record's: §10.14 Q1 uses only the first
record) sits on the shifted line: the engine's warn → a 0x12, not a contact. A shift that leaves no room for a yielder's lateral band
(kLaneSideM beside its predecessor) at the same s: the band is measured from ℓ(s), unchanged.

**Open.** Why the part walk fails for Mayhem's hollow bases (> 64 parts, or an unresolved root: audit row 19): a part census of those
bases (the module's part-cache rows on a flight, or a static read of their scenes) decides whether raising the cap (64 → 256: ≤ 8 µs per
candidate pair in the narrow phase) removes the fallback case altogether, which is the better fix than any rule above.

**Implemented (round B, 2026-10-15; §11 "Round B"), the shift off.** The profile (one plateau, two ramps at tan 15° tangent to the clearance circles,
−U first, +U refused within 6 km, 0 from the blend's start, the margin lowered from 50 m to 0 when the ramps do not fit, 1 Hz on the frame boundary,
5.6 µs host for 64 bodies), the avoidance seam (`av::Self::skip_static`: no detour against a listed body, the cleared ones entered in the resolved set)
and the enclosing-fallback rule are in; the shift itself is off (`gc::kLaneProfShiftOn`): `lane_station` has no feasible shift under these rules
(133 m short of the blend), and with it `lane_belt` regressed (site E fails closed on SETA frames for the profiled rocks, the members' 0x12 meet;
a rock touched at the 50 m margin at tf 1). `lane_fallback` 9 of 9 on the enclosing-fallback rule; `lane_station` and `lane_belt` stay parked.

### 12.4 Class 4: the flown 0x12 (`x12_near`, `x12_far`)

**Mechanism [m]** (§3.3, §10.1, §10.14 Q3). The KC's `CollisionWarn` stops the ship and issues 0x12 (end radius r_s + 300 m on order 4).
Every 200 ms the handler re-scores the list; the escape direction starts perpendicular to the nearest obstacle and steps 2.8° up to 169°;
a must-turn-away obstacle (the gate, a capital ≥ 0.75 r_A) blocks a candidate unless it points away (dot ≤ −0.8, −0.9 for a gate): the
98–179° turn-aways; the speed is the slowest listed obstacle's (floor 10 m/s). The order ends when the list is empty and the ship is
beyond the end radius of the start point; the KC re-issues order 4 **at the held slot** (no slot release across a 0x12), site A rewrites
L_i, the member re-commits (hysteresis 2 s + the step). Measured: near (1.7 km out) turn 53–71°, 17–19 s, 1.66 km off Λ; far (6.95 km)
98–128°, 25–33 s, 1.7–2.0 km off, the follower's lane v_min 0–27 m/s (slowed or stopped 2–22 s).

**Why the controller fails.** The rejoin from y = 1.7–2.0 km needs D ≥ 2y / tan 30° = 5.8–6.9 km of room before the join's end
(d_end + 1.5 km = 3.8 km), or 2y = 3.4–4 km at the 45° residual. Near: the ship is already inside its join's end, `lane_join_span` returns
the residual and the ship reaches d_end still turning (P2 4.2–8.7°, on the 8° bound). Far: the room (7 − 3.8 = 3.2 km) is short of 2y,
P2 comes 13–28° off (the refusal brake, a rotate), and, **ranked ahead by position** while it is still 2 km off the line, the re-issued
ship cuts across its follower's path: the pair is uncertified, the follower stops or is evicted in turn.

**Controller change.**

* **The loop-back join.** A member that re-commits after `Why::evict` (the board keeps `evict_ms`; an evicted id re-enters within 30 s
  with its sequence) and whose join does not fit (the residual branch of `lane_join_span`, room < 2y + settle) is flagged `redep` with its
  turn point at its **current** s: it flies the existing re-departure geometry (`lane_carrot`'s redep branch: out along −F on the
  departure side 2.5 R below Λ, the measured U-turn extent) to s_turn = −(d_end + settle + R + D_need(y_side)), then the tangent join.
  `redep_out` = max(kLaneRedepM, D_need) instead of the constant. No new geometry; one more entry condition.
* **Rank by ETA including the loop.** `lane_eta` for a redep member counts the out-leg, the U-turn (π R / v) and the join; by §6.2 the
  later ETA yields, so the re-issued ship merges **behind** a follower that is nearer in time (the car-merge rule: the passer never slows,
  the follower's law never sees a predecessor cutting in from 2 km off). The follower's block behind a turning-back ship (`over_block`,
  `over_ret_ms` in game time) applies as for an overshoot.
* **The near case** keeps the lane when the residual fits the settle distance; where P2 then misses by < 8.1° nothing is written (rank
  4's no-write hand-over: the vanilla rotate and order 5 from rest, ≈ 20 s once **[i]**) instead of the accelerate-then-brake.

**Bounds.** Geometry, not timers: the loop's length / v_lane (an M1, R 637 m, far case: ≈ 1 km out + 2 km U-turn + 4–5.7 km join ≈
80–90 s **[i]**, against today's residual join + a 20–60 s refusal and a second 25–33 s eviction); the re-commit hysteresis 2 s + the step;
T_stop 20 s + the step for the law's stop. The fixture sets the span bound from the measurement.

**Hot path.** Nothing per frame: `lane_join_span` is already computed per decision; the flag is set once at re-commit.

**Fail-before.** `x12_near`: "Bx P2 kept within 8 deg after the re-merge" (2 of 9); `x12_far`: the same (5 of 9), "Bx no further eviction
after the re-issue" and "Ex no member 0x12" (2 of 9 each); add: the re-issued ship's rank after re-commit equals its ETA rank (a unit:
2 km off at 7 km out behind a Titan at 8.4 km → it yields), the follower's lane v_min ≥ 10 m/s (today 0 for 4–22 s), the loop's span
≤ the measured maximum + 5 s + the step.

**Flight risk.** The loop's out-leg points away from the gate (cos < 0: no own-gate warn); the U-turn's end is on the tangent join the
own-gate row certifies from any distance. A 0x12 that ends with the ship **past** the plane is the overshoot path, unchanged. A loop
2.5 R below Λ at 7 km passes nothing at the gate; a static there is 12.3's profile or the sphere avoidance.

**Open.** The Run173 evictions were own-gate warns the lane row now suppresses; a flown 0x12 in the next flight should name a
non-gate B (`x12_pairs.py`), else the row's reach is the first thing to check, not this geometry.

### 12.5 Class 5: rank 4 and the order; the arrival fan (1b)

* **Rank 4 goes first (round A)**, not after 2–3: it is ratified, it removes the latches that produced the crawl class, and 1a's fallback
  and 4's near case are written on its hand-over and yield; writing them on the three-candidate ladder and the `refused` timer first would
  be work done twice. Its fail-before checks are the holistic review's (the merge bound, `p2_miss` / `3c`, the stop law), expected cost
  ≤ 8 s per merge **[i]**. **Implemented (round A, 2026-10-15; §11 "Round A" items 1–3, 5)** with four deviations, each measured there:
  the one law reads the raw gap + the joins' cost − the own closing distance (the full settling term re-accelerated the yielder beside its
  passer); the lateral band is a per-pair latch released once behind, not the raw g_req − 100 m alone; the single yield candidate is the
  law's floor (the raised-target law is the lane speed for a wide gap), else the stop; the brake bound from P2 binds at d_end (room 898 m
  for a Titan against 11 s of poll travel), is written by site B but not rolled out, and the no-write hand-over needs the follower on a
  braking curve to g_req behind the predecessor's tail at d_end before its P2 (`stop_room`). Merges cost −2 to +8 s, spans ≤ 7.8 s.
* **2 and 3 are independent of the laws and of each other** (different units) and run in parallel with A in worktrees; the brief's lean
  "2–3 first" is kept in the sense that they do not wait for A.
* **4 after A** (it needs the no-write hand-over), **1a inside A** (the sweep branch and E's coverage are law-independent, the fallback
  is A's single candidate).
* **1b, the arrival fan, last.** It separates only hull pairs with w_k + w_prev + 50 m ≤ 937 m (two M7s, an M7 beside an M2, §6.7); two
  Titans or the cast's M1 (r_s 1,135 m) + Titan cannot be placed apart inside a 1,105 m gate on any fan (±0.6 r R: 663 m apart; the side
  spawn's ±0.87 r R, +0.2 r U: 1,058 m from P, both under 1,166 m), and the launch itself (the partner side's grant 60 s earlier) cannot
  be delayed. `arrival_parked`'s overlap is vanilla for those hulls: the engine's own 0x12 of the resting hull (order 0, CollisionWarn
  active) and of the newcomer after its 0x1e. The fan is still worth its once-per-launch cost for M7-class convoys with the player in the
  destination sector; `arr_capital_unseparated` counts the rest, and the cast's check is re-scoped to hull-fit pairs.

### 12.6 Cost, native Windows, verification, alternatives, unknowns

**Hot path, summed.** Round A removes work; B adds one ≤ 24-member scan per small-ship order-4 decision, a 1 Hz static scan per lane
gate (≈ 20 µs host **[i]**, frame boundary) and 4 compares per `lane_lat_at`; C nothing per frame; D once per launch. No new allocation,
lock or site. The bench check (bench24, third-worst frame < 1 ms) and `ctl_frame_us_max` in flight are the gates.

**Native Windows.** Everything here is portable core (`ctl`) plus the existing sites A / B / E / P2 / P4 / P5 / the launch site; no OS
API, no Wine-specific behaviour. The user's flights are native Windows since 2026-10-11 (builds from WSL2); the host fixture is x86-64, the i686 cost of
the static scan is the unmeasured gap.

**What would prove it.** The nine casts un-parked, green over 3 seeds × tf 1 / 6 / 10 with the fail-before checks named above, `--lane`
and the full host run at 0 failures, `laneon_diff.py` 0 for casts the round does not touch, x87 audit and site verifier unchanged; then one
flight at a hub gate with arrivals and small escorts: `ship_gate_ctl` rows with `role=arrival` (the orders after 0x1e: 1a open ii),
`ship_avoid_e_verdict` rows on the arrival's frames, D.5 verdict counts (`edge` / `hold`), the profile's rows (`lane_profile` with δ(s)
per gate), 0 member 0x12 against a static or an arrival, and the span against the formed bound.

**Alternatives considered.**

* *Write the small ship's order-4 point A to the side* (site A's `f+0x240` write, as the capital's L_i): 300 m more margin beside a
  Titan, but a new engine write for a class with the 15° fan, the spread and 0x12 re-issues of its own, and the serializer question
  reopened for small ships; the edge lead-in needs none of it and the hold covers the wide-capital case. Reserve, not first.
* *Hold the small ships for every funnel (no edge, no mirror)*: simplest, but TS escorts would wait 30–90 s beside the gate for every
  convoy; the car-merge rule asks for a brief yield, and the edge is free for every census TS / Titan pair.
* *Per-ship lane-aware detour (each member shifts itself within its window)*: followers would see a predecessor whose `lane_s` is kept
  while its lateral differs, the sweep would certify n different lines, and two members could pick opposite sides of a rock; the one
  profile per gate keeps one plan.
* *Shrink the fallback body (0.35 r → smaller) instead of refusing the detour inside it*: still a detour of several km for any body that
  encloses the lane, and the number is not the engine's; refusing the detour where it cannot help, with the engine's warn as the
  backstop, is the rule the avoidance note already states.
* *Suppress the engine's warn between a member and an arrival by role alone (no certification)*: unsafe for the hull-fit failure
  (Titan-wide hulls on a 1,105 m gate) and for an arrival whose next order crosses Λ; the certified pair with a drift-free arrival
  plan is the same suppression where it is safe.
* *Evict the re-issued 0x12 ship from the lane for good (let the engine fly it to the grid point)*: Run173's behaviour (the 785 s span).
* *Treat the arrival fan as the fix for class 1*: it does not touch the eviction at d_end, which is 8 of 9 runs, and cannot separate
  the hulls that overlap.

**Unknown, and what settles it.** (i) The 0x21 phase's warn in the EXE (12.1 open i): a disassembly of the 0x21 handler's velocity writes
or a flight row. (ii) Q5's hold times (a flight row). (iii) The part-walk failure of Mayhem's hollow bases (a part census). (iv) The arc-edge
margins per census pair (the fixture). (v) The loop-back's span cost (the fixture). (vi) i686 cost of the static scan (the flight's
`ctl_frame_us_max`).
