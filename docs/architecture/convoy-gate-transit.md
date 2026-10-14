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

State 2026-10-14 (fourth implementer, the final review's items 1–6, "Final review fixes" below): scenario C green (`--lane` 1122
checks, 0 failures; 14 casts and the parked cast's key-off baseline), included in the full and `--capital` runs. Before that (third
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
* **Join angle** 20° (kLaneJoinK 2.75), steeper when needed so it completes 1.5 km before d_end, at most 45°.
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

### Measured (scenario C, 3 seeds × SETA tf 1/6/10; scenario_c.txt, ship_steering_host_lane.txt) [m]

After the review fixes (λ drawn from 4.1–16.9 s, the KC's slot rule with held slots, merges completing on the raw gap; the second
implementer's figures are in the ledger), refreshed after the final review fixes (the strict order moved abreast 1+4 tf 1 from
234.6 to 235.0 s at most; every other figure unchanged; the constants' failure counts below unchanged):

| Variant | tf 1 (s) | tf 6 (s) | tf 10 (s) | merge completion max (s) | closest capsule (m) |
| --- | --- | --- | --- | --- | --- |
| abreast 1+2 | 173.3–187.8 | 153.4–157.1 | 154.3–154.6 | 81.4 | 509 |
| abreast 1+4 | 227.0–235.0 | 214.0–214.1 | 208.0–208.2 | 87.2 | 454 |
| formed M2/M1/M2 | 203.0–216.6 | 184.9–186.2 | 185.4–185.6 | — | 511 |
| formed M1/M7/M7 | 198.9–210.0 | 180.1–181.7 | 174.4 | 7.0 | 349 |
| stragglers | 200.9–209.9 | 186.7–186.8 | 181.0–182.2 | 42.7 | 512 |
| stragglers far | 307.9 | 295.8–295.9 | 289.9–291.4 | — | 4024 |
| re-departure | 237.1–240.2 | 212.2–213.2 | 207.2–210.4 | 7.0 | 349 |
| crossing | 329.6–333.2 | 317.8–317.9 | 311.4–311.6 | 124.9 | 515 |
| partner block | 235.8–240.3 | 218.9–221.7 | 216.9–220.7 | — | 454 |
| formed, stage 2 | 201.9 | 184.7–186.2 | 185.4–185.6 | — | 647 |
| slow hull | 279.0–282.4 | 246.2–247.2 | 247.0–248.1 | 147.1 | 216 |
| small escort | 201.5–210.7 | 183.7–185.0 | 179.0–179.4 | 9.0 | 327 |
| parked (all launched) | — | — | — | — | 465 |
| leader killed (all launched) | — | — | — | — | 682 |

Spans are 15 km → last launch. 0 member 0x12 episodes, 0 uncertified frames, 0 brief stops in 126 runs; T_slot 16.7–31.4 s at tf 1,
1.4–14.6 s under SETA; the pre-lane core fails the span assertion in 27 of 27 control runs. Against the second implementer the tf 1 spans
grew by up to 16 s (the λ draw up to 16.9 s and the reserve: item 1), abreast 1+4 under SETA by 8 s (item 6). Constants, one changed
value each (`scenario_c.py`, 1114 checks per build): g_req 400 m → 13 failures (6 member 0x12 episodes); τ_m 8 s → 1; τ_f 5 s → 4;
T_stop 10 s → 0 (no brief stop occurs); λ_max 10 s (the hold's horizon) → 29; crawl 0 → 12 (stops at d_end); drift bound 100 m → 6
(the order-0 hold slows); d_end 1.8 km → 3; escort join k 0 → 14, 0.5 → 10.

Second implementer, for comparison (its spans are in the ledger): 0 member 0x12 episodes, 0 uncertified frames, 0 brief stops in 90 runs. T_slot 16.8–21.1 s at tf 1,
1.4–10.0 s under SETA. The pre-lane core fails the span assertion in 24 of 24 control runs. Constants, one changed value each
(`scenario_c.py`): g_req 400 m → 19 failures (the passer capped; 1 safety); τ_m 8 s → 0 failures, merges 50.3 s; τ_f 5 s → 2 (the
passer capped); T_stop 10 s → 0 (no stop occurs); λ_max 6 s → 18 (escort gate, short final, spans); d_end 1.8 km → 7 (the law units
fixed at 2.3 km, 2 runs passer capped, 1 order); escort join k 0 → 6, 0.5 → 4. Cost (host x86-64, the 1+4 cast, the frame's
controller block): lane 2.1 µs mean / 44–52 µs worst frame at tf 1, 21.9–23.1 / 45–54 µs at tf 10; stage B 1.9–2.2 / 73–102 µs and
20.7–22.2 / 78–99 µs; ceiling 1 ms (i686 figures come from the first flight's `ctl_sb_us_max`).

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
* The brief stop (T_stop) never occurs in scenario C; its law is covered by the units only.
* Next: the arrival fan (§6.7), the cross-board certificate (§6.6).
