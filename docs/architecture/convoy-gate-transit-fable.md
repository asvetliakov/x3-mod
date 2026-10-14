> Input note (Fable 5.1), 2026-10-14. Superseded by the ratified merge [convoy-gate-transit.md](convoy-gate-transit.md); kept as one of its two sources.

# Convoy gate transit: one trail line into the gate, a lane-merge rule, no holding

Design note for the main session, 2026-10-14 (Fable 5.1, high). Question: how a capital convoy (a leader plus 2–4 capital
escorts) goes through a gate smoothly instead of the holding loops and waits of the nine level-1 builds (Run161–169).
Teleporting is rejected (user). Binding user decision received during the design (coordinator, 2026-10-14): *no waiting*;
the leader goes, then the escorts go; a ship that conflicts with another yields only enough to fall in behind it, the passing
ship never slows; no holding pattern, no racetrack, no waiting at 10 km; a brief stop to let another ship pass is allowed.
Sources: [gate-traffic-controller.md](gate-traffic-controller.md) (D.0–D.12, A.1, B.1, B.6–B.9),
[ship-navigation-ai.md](../reverse-engineering/ship-navigation-ai.md) §1, §4, §10.1–10.11,
[gate-queue-script.md](../reverse-engineering/gate-queue-script.md) §3–6, [capital-gate-transit.md](capital-gate-transit.md) §5,
and the Run166–169 triage under `verification/results/ship-steering/run16[6-9]-triage/` **[m]** where quoted.

**Outcome.** Recommend option (b′), *the trail line*: every capital committed to a gate, leaders and escorts alike, flies one
trail line into the funnel at hull spacing, escorts in trail behind their leader from the leader's commit on; a car-merge
rule settles every conflict (the ship farther back yields until it is one spacing behind, the passer never slows); each
member's order-4 point is rewritten so its X lies where the trail puts it, so the script's own order 4 → 0x1f → through slot →
order 5 → order 8 runs with the first through-slot request granted; no petal, no racetrack, no stage point, no escort wait.
The escorts' gate order is the script's, issued 2–5 s after the leader has left the sector, and that latency is spent flying
at trail speed, not waiting. The launch cadence is the hull spacing over the speed, about 25 s per capital in line (20 s for an
M7-size hull); the "3 s per escort" ideal is reachable only with hulls abreast in the 1.1 km gate, which fits an M7 beside an
M2 but never two M2-size hulls, and it needs the concurrent-order-5 disassembly named in §8 before it is designed.

## 1. Recommendation

1. **One line per gate.** The funnel line Λ (D.2.2, through the member's own Q, F-parallel) is extended backwards as far as
   the member is: from its commit the member flies the lead-in to Λ and then Λ. Members on the line are ordered by along-axis
   position; the first is the head. There is no holding geometry at all: when the gate is blocked the head slows to the 10 m/s
   floor toward X and stops there only if the block is longer than the floor absorbs; the line behind it keeps spacing.
2. **Escorts in trail from the leader's commit.** An escort's 0x14/0x15 leaf carrot is the point one trail spacing behind
   its predecessor (the leader for rank 1, the previous escort otherwise) on the predecessor's recorded path; its speed is the
   predecessor's plus a bounded spacing correction. The formation slot is replaced only for capital escorts of a committed
   leader (D.3's role); small escorts keep the formation law.
3. **Trail spacing** `s = L_pred + L_self + 300 m + 1 s · v` (capsule half-lengths, D.1): 2.68 km for two census Titans,
   2.37 km for an M7 behind a Titan, 2.0 km for two M7s **[i]** arithmetic. That is outside the engine's close-range warn
   `1.2 (r_A + r_B)` (2.62 km Titan–Titan) **[m]** rule, so a trail pair at constant spacing is outside the close-range
   warn even in an uncertified frame; the closing-pair cone test (§3.1) stays covered by the certificate **[i]**.
4. **The merge rule** (§6.3): between two members whose capsules the sweep brings within margin, the one farther back along
   the gate axis yields; abreast (within 300 m along the axis) the higher sequence key yields (an escort's key follows its
   leader's, so a leader never yields to its own escorts and a convoy stays together). The yielder slows, never below 25 m/s
   except when the gap cannot be made before the join point, then to a stop of at most 15 s; it resumes as the follower at
   spacing. The passer's plan is untouched.
5. **X by the order-4 point rewrite.** On the first order-4 frame of a member (and on every KC re-issue) the DLL writes
   `f+0x240` to the point on Λ 828 m past the planned X, with D.5's guards (order 4, class-0x12 target, capital, field equal to
   the KC's validated grid point or our last write). Order 4 then ends at X (0.75 r_gate before the point), P2 keeps the speed,
   0x1f ends on its first frame, the loop head requests the through slot. X is placed so that the request comes one frame after
   the predecessor's release: `z_X = z_trig,pred + s − v·(0.9 s + 2 frames)`. The slot is free, order 5 runs the leg `≈ s`, order 8
   launches with P5's kept speed.
6. **Speed hold on order 0.** Site B writes the trail speed on a member's order-0 frames (the 0x14 re-issue gaps, the frames
   between the leader's departure and the script's order 4, the 0x1f/order-0 hand-over, a refused-slot wait) while its nose is
   within 8° of F; on a refused slot it brakes so that the longest poll (11 s) keeps the ship outside its trigger sphere + 300 m.
7. **No order of ours.** The DLL issues no order and patches no script in this stage. The script's checks are satisfied:
   leader gone → the escort is 2.4–11 km from the gate, inside 10 km it gets `fly to sector` at the next template pass; beyond,
   a 0x15 leg (ours) until 10 km. Two optional later stages (§6.6): the x3story.obj constants (polls, cooldowns) and an MSCI
   edit of `!move.follow.template`; neither is needed for the acceptance behaviour.
8. **Kept from level 1:** the board and roles, the capsule sweep and site-E certificate, Λ/Q and the lead-in, P2/P4/P5, the
   `0x00444606` arrival placement and the arrival-gate row, D.5's small-ship split, the counters and rings, the key. **Deleted:**
   petals, the lap law, the release window, the head/escort targets, stage points, `esc_in10`/`escort_settled`, the join
   profile for escorts, the escort petal rollout plan (D.2.4, D.2.5's window logic, D.3's petals, B.6–B.8 wholesale). B.9's
   rollout corrections survive as the follow law's rollout. Details in §6.8.

## 2. What Run169 measured [m]

`run169-triage/convoy_summary.txt`, `funnel_serial.txt`, `hold_totals.txt`, `escort_wait.txt`, `transits.txt`; game seconds,
SETA share 0.82 (`seta.txt`), v_max 110–113 m/s with the module's 100 m/s large cap flown (median 100).

| Convoy (leader, gate, escorts) | Span 15 km → last launch | Coordinator's ideal (d/v + 10.5 s per escort) | Where it went |
| --- | --- | --- | --- |
| 24965 f478 (M2; 25287, 25763) | 681 | 157 | escorts inbound from 33–36 km: order 4 at +278.6 / +301.0 s after the launch; funnel approach 251 |
| 3097 0450 (M1; 3861, 4120) | 2,170 | 153 | escorts leader-first 1,216; re-targets to new M1s; 4120 never got order 4 from this leader |
| 4673 f478 (M2; 5001, 5476) | 769 (from 1.3 km) | 34 | head slot block 246, head out of window 123, funnel approach 207; escorts launched +80.6 / +162 |
| 5727 de88 (M1; 6185, 6439) | 361 | 154 | funnel approach 201, order-5 ramp 56; escorts order 4 at +56.3 / +110.1 (13–17 km out) |
| 8846 f478 (M1; 3861, 4120) | 930 | 153 | funnel approach 328, leader-first 119 |
| 12699 f478 (M1; 3861, 4120) | 592 (from 1.6 km) | 36 | head out of window 176, funnel approach 194 |

* Hold seconds by reason over the flight (`hold_totals.txt`): escort waiting for order 4, leader not launched **2,416.8**;
  leader launched **506.6**; head out of window 378.9; head slot block 310.0; funnel busy 237.8; total 3,919.5 game s
  (478.5 real s).
* Per release (`funnel_serial.txt`): release 8.3–14.5 km out, release → X 55–205 s, X → launch 14.6–33.6 s; the funnel ran
  two-deep only 3 times in 15 releases.
* Escort order 4 after the leader's launch (`escort_wait.txt`): parked inside 10 km +4.1…+16.9 s (8 escorts); 13–17 km out
  +56…+110 s; inbound from 33–36 km +278…+301 s. The script's first pass inside 10 km always ordered at once.
* Run166–168 (`run166-triage/hold_wait.txt`, `run167/168-triage/escort_wait.txt`, `hold_totals.txt`): 721 / 869+543 /
  396+491 game s of escort waiting per flight; every amendment B.6–B.9 moved the escort's waiting place, not its length.

**Reading.** The user's "loops" are three things, in order of size: escorts circling while their leader holds or transits
(script leader-first rule plus our petals: 2,924 s in Run169); the serial funnel flight per escort after its release from a
petal 8–12 km out (55–205 s each, one at a time); and the head's own petal phase and slot blocks (689 s). None of these is the
engine's one-at-a-time at the gate (X → launch 15–34 s). A design that keeps escorts within one hull spacing of their leader
removes the first two outright and leaves the third to the line's head.

## 3. The cadence floor

* The engine passes one capital per order-5 leg (the KC's single through slot per gate pair, granted after 0x1f, released
  after order 8 **[m]** §10.2). A member's X → launch is the leg from X to its trigger `t = (r_s + |v|/10)(1 + (tf − 1)/4)`
  **[m]**, 14.6–33.6 s measured. The next member's leg can start one frame after the predecessor's release.
* In trail the leg equals the spacing: two Titan hulls in line occupy `L_A + L_B = 2.18 km` **[m]** census; with the 300 m gap
  and the 1 s allowance, 2.68 km at 100 m/s = **27 s**; an M7 behind a Titan 24 s; M7 behind M7 20 s **[i]**.
* **3 s per escort** needs hulls abreast, laterally separated by `w_A + w_B + 100 m` inside the trigger window `|Q| ≤ r_s +
  v/10 − 100 m`: a Titan (w 583) beside an M7 (w 350–380) at lateral 1.1 km fits (gap 137 m); two Titans need 1.27 km in a
  window of ±1.0 km and do not **[i]** from D.2.2's windows. It also needs two capitals on order 5 at once, which only a DLL-issued
  order 5 (the 4 → 5 order-word swap while the task is blocked in order 4, §7 item 6) or a KC patch (A5, rejected in
  gate-queue-script §6) gives, and whether the engine itself serialises order 5/8 per gate is not established (§8 item 1).
* So the convoy time from 15 km at SETA 1 is the leader's straight flight (15 km at 100 m/s: 139 s to the trigger, launch at
  140 s; the straight line to the plane is 150 s) plus 20–27 s per escort: **≈ 190 s for two escorts, ≈ 240 s for four**
  **[i]**. Under SETA the trigger inflates (×3.25 at tf 10) and the leg shortens; game seconds scale with the step.

## 4. Option matrix

| | (a) Level 2 as named | (b) Convoy-as-unit, the DLL hands each member to order 5 | **(b′) Trail line + merge, script satisfied** | (c) Own transit state machine | (d) MSCI/KC script patches alone |
| --- | --- | --- | --- | --- | --- |
| Player sees | escorts still parked until the leader is gone; saves the 1–11 s poll and the stop at X; loops stay | trail, one launch per hull spacing | trail, one launch per hull spacing; a yield is a visible slow-down, a block a brief stop at X | same as (b) at best | polls and cooldowns shorter; escorts still leader-first unless the template is edited |
| Time from 15 km (2 escorts) | ≈ 600–900 s today minus ~20 s | ≈ 190 s | **≈ 190 s** | ≈ 190 s | ≈ 400+ s |
| Engine surfaces | 4 → 5 order-word swap, result protocol `0x00460560` | swap + f+0x240 writes | f+0x240 write on order 4 (D.5's precedent), site-B policy on order 0 | swap, order-0 steering, own trigger | none |
| Script surfaces | loose x3story.obj (clearance B) | none | none | none | x3story.obj A1/A4/A6 or B; `!move.follow.template` |
| Save / Mayhem | B is save-safe (§6 of gate-queue-script); x3story.obj replaced | none | none | none | A-series constants save-safe; the MSCI edit is overwritten by a Mayhem update |
| Script fights? | the task blocked in 0x14 gets a result it did not ask for: `__EscortObject` re-issues 0x14; no transit without `__EnterHyperspace` | the swap only works inside `__EnterHyperspace` phase 0; the task still has to get there (leader gone) | no: every script check is met on the move | as (b) | no |
| Cost | medium | medium | **low–medium** (deletes B.6–B.9's machinery) | high | low |
| Verdict | loses: the transfer is KC-driven (`Remove()`, `__LeaveHyperspace`); a DLL order cannot transit a ship, and the escort's order 4 already comes 4–9 s after the leader leaves when it is inside 10 km | loses to (b′): the swap buys nothing in trail, since X is already ours by the point rewrite; it is the abreast refinement's tool, kept for §8 item 1 | **recommended** | loses: nothing of order 4/5 is worth replacing (the steering, speed, point and X are already ours); the engine's "transfer" is a 0.9 s push the KC starts | complementary, later: shortens blocks (A1/A6) and polls (A4); alone it leaves the loops |

Why the transfer cannot be ours **[m]**: order 5's result 1 wakes the task inside `2004::__EnterHyperspace`, which issues
`SA_WarpObjectAway`, then `ReleaseFromSlot`, `Remove()` and, in `__FlyToNextSector`, `__LeaveHyperspace` in the destination
(`Activate`, `SA_StartInHangar`, the launch `0x00444170`). The engine's order 8 is 900 ms of kinematics; the sector change is
KC state. A ship whose task is in `__EscortObject` when a DLL-issued order 5 ends is re-ordered 0x14 at the trigger point
(§10.11 Q3, §1.1).

## 5. What the player sees (b′), SETA 1, v 100 m/s [i] from the measured laws

| Moment | Leader | Escort 1 (Titan) | Escort 2 (M7) | Today (Run169, measured) |
| --- | --- | --- | --- | --- |
| commit (30–40 km) | lead-in to Λ at v_max | falls in 2.7 km behind | 2.4 km behind escort 1 | escorts in formation, then separate petals |
| 15 km | on Λ, 100 m/s | in trail | in trail | leader may hold in a petal; escorts circle at 25–60 m/s |
| X (3.6 km), t = 114 s | order 4 ends, speed kept, 0x1f 1 frame, slot granted | 27 s behind | 51 s behind | release → X 55–205 s per ship |
| trigger, t = 139 s; launch 140 s | order 5 leg 25 s, order 8 | at its X − 2 frames | | X → launch 15–34 s |
| leader gone +2–5 s | | `fly to sector`, order 4 ends at once (already past X), 0x1f, slot free, order 5 | 0x15/0x14 steered in trail | order 4 at +4…+301 s |
| t ≈ 167 s | | launch | at its X | escort 1 launch +80…+390 s |
| t ≈ 190 s | | | launch | escort 2 launch +162…+547 s |

A blocked gate (an arrival's 61 s block, a convoy ahead) shows as the whole line slowing together and, if the block is long,
stopping at spacing with the head at X, noses on the gate. A merge shows as one ship easing off until it is a hull spacing
behind the other, then following. Nothing circles and nothing turns away from the gate.

## 6. Design of the recommended option

### 6.1 Members, the line, the order

* Board and roles as D.1 (committed, escort, arrival, evicted), the capsule hulls and the 4 Hz sweep unchanged. The escort
  role is entered at the leader's commit (as now) and carries `pred` = the member it trails (the leader for rank 1, else the
  previous escort by rank), `rank`, and the recorded path of its predecessor.
* **The line** of a member is its lead-in (amendment 13's S-curve onto Λ at the cornering cap, the join predictor deciding
  feasibility) followed by Λ to its Q. Every member has an along-axis coordinate `z` (distance to its trigger point along F,
  negative before it). The **head** is the member with the largest z among those on order 4/0/0x1f/5/8; the line order is by z.
* **Recorded path** per member: a ring of the last 6 km of its positions and speeds at 100 m steps (60 × 16 B, static, per
  board entry). A follower's carrot is the path point `s` behind its predecessor's current along-path position; beyond the
  buffer (a predecessor more than 6 km ahead, which only happens at the inbound capture) the carrot is the straight line to
  the buffer's oldest point.

### 6.2 The trail law

* Carrot: the predecessor's path point at arc distance `s` behind it, plus the usual look-ahead (max(0.75 v, 2 v/W, 0.3 r_gate)).
* Speed: `v = clamp(v_pred(at that point) + K·(d_actual − s), 0, v_law)` with K = 0.05 s⁻¹ and `v_law` = site B's gate speed
  law for the member (the large cap, the cornering cap, the funnel's shaping for the head). A 500 m spacing error is a
  25 m/s correction; the follower never exceeds its own law.
* Where the predecessor stops (a block), the follower stops at spacing (allowed by the user's rule). Where the predecessor
  launches (order 8, 2.7 km in 0.9 s), the follower's carrot continues along F from the predecessor's trigger point at the
  speed the follower had; its own X is ahead on that line (§6.4).
* Orders: 0x14 and 0x15 (escorts, steered leaves), 4 (committed, steered), 0 (the 0x14 re-issue gaps and the hand-over, speed
  only, §6.5). The engine's 0x14 handler computes its own slot point and the leader's speed (floor 5000); both are overridden by
  site A and site B as today for members.

### 6.3 The merge rule

A conflict is a sweep verdict other than clear for a member pair (D.1's test, unchanged). The resolution replaces D.1's
priority list and three-candidate ladder for members:

1. **Who yields.** The arrival role never yields (engine-moved, D.4). Otherwise the member farther back along the gate
   axis (smaller z) yields. Abreast, |Δz| ≤ 300 m: the higher sequence key yields. Keys are D.2.5's (`seq << 4`, an escort's
   = its leader's + 1 + rank), so a leader passes its own escorts and an earlier convoy passes a later one when abreast; a
   later convoy's leader that is genuinely ahead along the axis passes, and the earlier convoy's members fall in behind it
   (the cars rule; convoys may interleave).
2. **How much.** The yielder Y gets a new predecessor P (the ship it conflicts with) and the trail law toward the point `s`
   behind P. Its speed candidates, each rolled out and swept against every member, the first that clears taken: the trail
   law's value; 25 m/s; a stop (0 m/s) held at most 15 s, allowed only when the along-axis distance to the join point is under
   `s + v²/2a`. If none clears, the pair goes to the engine (E passes) as in D.1, counted in `ctl_unresolved`.
3. **The passer.** Its plan is never changed by a conflict in which it is the passer. The sweep's rollout of the passer is its
   own plan (the gate law, or its own trail law behind *its* predecessor).
4. **Lateral merges.** A member joining Λ from the side (an inbound convoy off the axis, a re-departure's U-turn, a go-around's
   rejoin) is slotted by its predicted arrival time at its join point against the line's predicted positions: it goes behind
   the member that will be ahead of it there, and yields on its S-curve to make the gap. The join predictor's candidates
   (100/60/40/25 m/s) already express this; the stop is the added last candidate.
5. **The certificate's view.** A trail pair's rollouts are the predecessor's plan and the follower's trail law on the recorded
   path, so the predicted capsule gap is `≈ 300 m + 1 s·v` throughout and the sweep certifies with D.1's margin (50 m + 5 m/s·t,
   200 m at 30 s). A yield is a plan change of the yielder only, re-swept on the yielder's next decision; `cause=yield`. The
   stop candidate is `cause=stop` (new). Freshness bounds unchanged.

### 6.4 The gate end: X by the point rewrite, the slot, the leg

* **X.** For each member on order 4 to Γ the DLL writes `f+0x240 = Λ(z_X + 828 m)` so that the engine's own test
  (`dist ≤ 0.75 r_gate` at every re-plan tick, `0x00454b1a/0x00454b70` **[m]**) ends order 4 at X on Λ. Guards (D.5's shape):
  order 4, target class 0x12, capital, `f+0x224 == Γ`, the field equal to the KC's grid point (validated geometry, §16.1) or
  to our last write; written at the first site-A call of the leg and again when the field differs (a KC re-issue after a 0x12
  interrupt). The field is read only by the order-4 handler, uncached, and never by the KC (§10.11 Q2 for the range; the
  handler's own reads are §8 item 2). The grid geometry stays the commit test; the grid point itself no longer constrains Λ,
  which removes D.2.2's 13-of-20-slots case and the lateral S on order 5.
* **Placement.** The head's X is where level 1 put it (the chord into the order-4 sphere is gone: X = Λ at
  `z = z_trig − 2.5 km`, so the order-5 leg stays about 25 s and the hand-over happens well before the bubble). A follower's X is
  `z_X = z_trig,pred + s − v·(0.9 s + 2·step)`: it reaches X about two frames after the predecessor's order 8 ends, the P2 frame
  keeps its speed and zeroes the remainders (D.12), 0x1f ends on the next frame, and the loop head's `RequestWarpThroughSlot`
  finds the slot released (same side, in sector: no cooldown **[m]** §10.2). Order 5 then runs `s − v·1 s` to its own trigger.
  SETA: `step` is the SETA-aware last game step (`Gate::step_ms`), and `z_trig` uses the inflated `t`.
* **A refused request** (the predecessor's release later than predicted, a hidden holder, an arrival's block): the ship is on
  order 0 after 0x1f, nose on F. Site B holds the trail speed only while `z_X + 11 s·v < z_trig − 300 m` (the longest poll kept
  outside the trigger sphere), else brakes at `a` toward the stop point `z_trig − 300 m`. When granted, order 5 starts from that
  speed (P2's kept speed or from rest). Counted in `ctl_x_refused` with seconds, per role.
* **Trigger and launch** as built: P4 records |v|, P5 launches with `v0·t + 1.665 t²`.

### 6.5 The escorts' gate order after the leader's launch

The script path is fixed **[m]** (§10.11 Q3): the leader's `Remove()` ends the escort's 0x14 (lost leader), `__EscortObject`
returns 8, the template waits 2–3 s, `CheckLeader` → `!move.jump` → `Mayhem.Task.Move` → `FlyThroughNextGate`: inside 10 km
`fly to sector` → `__EnterHyperspace` → warp-in slot → order 4; beyond 10 km a 5 s `follow object gate` leg first. Measured
+4.1…+16.9 s for parked escorts, and the first pass inside 10 km always ordered.

In the trail the escort is 2.4–11 km from the gate at the leader's `Remove()` (rank × spacing, plus the leader's 1.1–3.6 km
trigger distance), so rank 1–3 are inside 10 km and rank 4 of a Titan convoy may be just outside. During the 2–5 s of script
latency the escort is on order 0: site B holds its trail speed (§6.4), the remainders keep the last leaf's turn (§10.11 Q4),
so the ship neither slows nor drifts. Order 4 arrives when the escort is already at or past its planned X (rank 1) or still
approaching it (rank ≥ 2): in the first case the rewritten point is placed 828 m ahead of the ship so order 4 ends at its
next re-plan tick; in the second the point is `Λ(z_X + 828 m)` as in §6.4. The user's "no script wait" is met in the sense that
matters: the ship never waits; the 2–5 s are flown. Removing them outright needs the MSCI edit of §6.6 (saves 2–5 s per convoy).

### 6.6 Blocks, re-departures, crossing traffic, optional script patches

* **Arrival blocks.** A capital arrival at Γ blocks our side for 60 s (KC cooldown on a direction change, `m16`) **[m]** rule.
  The head's funnel speed shaping (D.2.3, kept) brings it to X at the block's end at ≥ 10 m/s; beyond what the floor absorbs it
  stops at X. The line stops behind it at spacing. The arrival's own path (0x21 up the +U half, 0x1e axis-parallel) is kept off Λ
  by D.4's placement; the sweep gives the arrival priority.
* **Re-departing ships** (an arrival leaving by the gate it came through; 591 s in Run169 with the nose turned away). After
  its 0x1e the ship is 1.1 km in front of the gate facing −F. It becomes committed at its order 4 and merges by a U-turn on the
  departure side (−U, away from the arrival lane) at the cornering cap, joining Λ 3–4 km in front; being nearest the gate it is
  the head and the line behind yields (§6.3). About π·R_turn/v + the leg: 90–120 s at 25–40 m/s **[i]**, against 591 s.
* **Crossing convoys** bound for different gates stay with the engine's warn and our sphere avoidance (different boards);
  a cross-board certificate is a later item.
* **Optional stage 2, the x3story.obj constants** (gate-queue-script §6, loose `L\x3story.obj`, build id unchanged, save-safe):
  A4 (through-slot polls 1–11 s → 0.25–1.25 s, 2 × 4 B) removes the refused-request randomness; A1 (`pushb 60` → N) or B
  (clearance by distance) shortens the arrival block; A6 lets out-of-sector same-direction traffic skip the 60 s, which is what
  spaces an *arriving* convoy one capital per 60–72 s in the player's sector (Run160 24274…24596 s **[m]**). None is needed for
  the departure behaviour above.
* **Optional stage 3, the MSCI edit** of `!move.follow.template` (Mayhem's, `addon/scripts`): issue `fly to sector` to a capital
  escort when its leader is on its gate run (KC command move-sector and within N km of the gate) instead of after the leader's
  sector change. Saves the 2–5 s latency per convoy and lets rank ≥ 2 commit before the leader is gone. Costs Mayhem-file
  compatibility (overwritten by a Mayhem update). Not recommended until the trail has flown.

### 6.7 Small ships, the player, combat, a lost leader, saves, platform

* **Small ships** at the gate: D.5 unchanged (the lower-half split, the `f+0x240` mirror for small order-5 legs while a capital
  is in the funnel). Small escorts keep the formation law, their targets projected out of the funnel corridor (D.3).
* **The player's ship** is a non-member: the engine's warn plus our sphere avoidance for the capital, as today. A player ship
  parked on Λ provokes an engine 0x12 on the member (evicted, 23 s rejoin measured in Run168); the line re-forms by the merge rule.
* **Combat.** `__EnterHyperspace` returns 7 when attacked; the template's attack branches take over; the member leaves the
  board when its order leaves the steer set, and its followers re-trail to the next predecessor ahead (the trail law's `pred`
  is re-resolved at every decision).
* **Leader destroyed or re-targeted mid-transit.** The escort's 0x14 ends (lost leader) or is re-issued to the new leader; the
  board drops the escort role when `f+0x224` no longer names a committed leader; a ship already on order 4 to Γ stays
  committed and keeps its line position; one already in trail with no predecessor becomes the line's tail by z. Mayhem's
  re-target to a newly arrived M1 (Run167–169) is script-owned; in trail the window for it is the 2–5 s after the leader's
  departure, not minutes (§8 item 7 names the script read that bounds it).
* **Save/load.** The board is in-process (D.8): on load, members are rebuilt from engine state; a ship on order 4 flies to the
  saved `f+0x240`, which is our point on Λ in front of the gate, and ends there; its line position follows from z. No KC state is
  touched.
* **Mayhem, native Windows.** No Mayhem file is changed in the recommended stage. The hooks are the installed in-process
  patches of `X3AP.exe` (site A, B, E, P2/P4/P5, `0x00444606`), verified by `verify_ship_steering_sites.py`; no new site. The
  game runs natively at `C:\X3`, which is the flight platform; no OS API beyond the module's.

### 6.8 What of level 1 is kept, deleted or folded

| Item | Fate |
| --- | --- |
| Board, roles, hulls, sweep, 24×24 verdict matrix, site-E member suppression, freshness bounds (D.1, A.1) | **kept** |
| Λ, Q, the lead-in S-curve and join predictor, the funnel speed shaping (D.2.2–D.2.3, B.1) | **kept**; Λ no longer tied to the grid sphere |
| Through-slot model, launch events, `0x00444606` placement, arrival-gate row, small side spawn (D.2.5, D.4) | **kept** |
| P2 keep-speed + remainder zeroing, P4/P5 (13, 17.1, D.12) | **kept** |
| D.5 small-ship mirror, r/3 rule | **kept** |
| B.9 rollout corrections (in-plane advance, inbound law, gap resume, cap in force) | **folded** into the trail law's rollout (the inbound law stays for heads) |
| Petals, rings, columns, petal assignment, lap law, release window, head skip, go-around into a petal (D.2.4, D.2.5's window, B.6) | **deleted**; the go-around becomes a lateral merge (§6.3 item 4) |
| Escort petals, stage point, `escort_target`, `esc_in10`, `escort_settled`, the 0x15 approach at 60 m/s, the join profile for escorts (D.3, B.6 amendment, B.7, B.8) | **deleted**; replaced by the trail law |
| D.1's priority list and yield ladder | **replaced** by the merge rule (§6.3); the candidates' rollout/sweep machinery kept |
| "On refusal the ship holds at X stopped" (D.2.6) | **replaced** by §6.4's speed hold / brake |
| Order-0 "nothing written" (B.9) | **replaced** by §6.5's speed hold for members with the nose within 8° of F |
| `ship_gate_ctl_member` row fields `rho s stage_s ringed` | replaced by `pred rank z s_err v_trail` |
| Key `ship_steering_gates_traffic` off = amendment 17 | **kept** |

### 6.9 Cost on the hot path

* Site A, member: the trail carrot is a ring-buffer lookup (≤ 60 entries, index cached per follower) and the speed law:
  about 30–60 ns per call **[i]** (the lookup is a few compares; the petal pursuit it replaces ran `plant_step_to`).
* Decisions (4 Hz per member): the follower's rollout is the predecessor's path shifted (61 samples copied with a time
  offset), cheaper than the petal plan's 120 plant steps (≈ 2.4 µs, D.1). The sweep and matrix unchanged (6 µs typical, 70 µs
  worst per frame, D.1 **[i]** from measured sweeps). A merge adds at most one extra candidate rollout (the stop).
* Site B, order 0: one branch per member frame. The `f+0x240` write: once per order-4 issue.
* Memory: 60 × 16 B path ring per board entry, 24 entries × 4 boards ≈ 92 KB static; the petal tables go. No allocation, no
  lock, game thread only, SSE2 scalar. The i686 numbers come from `ctl_sb_us_max` / `join_eval_us_max` in the first flight.

### 6.10 Verification

**Fixture, scenario C (convoy trail)** on `ship_steering_host.cpp` with scenario T's KC model (slots, one through slot,
60 s cooldown, in-sector exemption, 1–11 s polls, the escort template's leader-first rule with its 2–3 s wait and the 10 km
pass every 5 s), the capsule hulls and E. Cast from Run169: M2 1092 + M1 1022 + M2 1092; M1 1022 + M7 760 + M7 778; a
4-escort variant; starts at 15 and 40 km with the escorts in lateral formation slots (±1 km), at 60/144 fps and SETA steps 1,
6 and 10. Variants: free gate; a second convoy 60 s behind; an arrival stream at 61 s; a re-departure; the leader destroyed
2 km before X; a non-member parked on Λ. Assertions:

1. No member below 25 m/s except as a merge yielder (≤ 15 s stop) or behind a blocked head; zero petal laps, zero frames with
   the nose more than 90° off the gate while the line is free (oracle: 606 + 313 + 278 game s, `convoy_summary.txt`).
2. Trail spacing error ≤ 150 m at every member's X; capsule gap ≥ 50 m everywhere; zero engine warns and zero 0x12 among members.
3. Every order 4 ends within one re-plan interval of its planned X (the rewritten point); P2 keeps the speed; 0x1f ≤ 1 frame.
4. First through-slot request granted for every member in the free-gate variants (`ctl_x_refused` = 0); with the poll model,
   refusals ≤ 1 per member and ≤ 11 s each in the arrival variant.
5. Escort launches at the leader's launch + rank × (s/v) within ±5 s + the script latency; last launch ≤ d15/v + 30 s × n.
6. The passer's written speed never drops during a conflict it wins; every yield is attributed (`cause=yield|stop`).
7. Leader destroyed: followers re-trail within one decision; no contact; the line re-forms by z.
8. Key off: scenario-M and scenario-T outputs identical to the current build; units: the trail carrot on a curve, the spacing
   law, the merge order (z, abreast → key), the `f+0x240` rewrite guards, the order-0 speed hold's brake bound.

**Flight oracle (Run170)**, `--debug`: `convoy_summary.py` span per convoy ≤ d15/v + 30 s × n + the measured block seconds;
`hold_totals.py` "escort waiting" buckets → 0; `funnel_serial.py` release → X = 0 for followers, X → launch ≤ 30 s;
`escort_wait.py` order 4 at +2…+10 s for every escort inside 10 km at the launch; new row `ship_gate_ctl_trail` (member, pred,
rank, z, spacing error, written speed, cause) at 1 Hz; counters `ctl_merge_yield`, `ctl_merge_stop`, `ctl_x_refused` (+ s),
`ctl_point_rewrites`; `gate_rows_dropped` 0. Acceptance is the user's: no loop, no turn-away, escorts through within one
spacing each of their leader.

## 7. The script fighting our steering, item by item

| Script behaviour | Effect on the trail | Settled by |
| --- | --- | --- |
| 0x14 re-issued every ~33 s with 2–3 s order-0 gaps (template lines 261, 270) **[m]** | gaps flown at speed (§6.5); the integrator keeps the last turn (§10.11 Q4) | §8 item 4 |
| `escort ship` ends only on timeout/lost leader; `fly to sector` only after the leader's sector change and inside 10 km **[m]** | 2–5 s latency flown; rank 4 beyond 10 km flies a 0x15 leg (ours) | §8 item 3; the MSCI edit removes it |
| `__EnterHyperspace` phase 0: `SA_CollisionObject` → 0x12 instead of order 4 when the warn bit is set **[m]** | only for an uncertified or non-member pair (player, crossing traffic); eviction then merge | as level 1 |
| `RequestWarpInSlot` refused (20 slots full): `SA_SetSpeedLimit(m3, m93)`, `SA_Idle` wander (0x1d, steered) **[m]** | the leaf stays ours; the speed limit `f+0x278` may cap site B's write | §8 item 9 |
| through-slot poll 1–11 s after a refusal **[m]** | brake bound of §6.4; A4 optional | §8 item 5 |
| Mayhem re-target of an orphaned escort to a new M1 **[m]** flights | window reduced to seconds | §8 item 7 |
| `JumpToSector` (bd7a3) also takes the through slot **[m]** | a refusal the model does not predict; counted | flight `ctl_x_refused` rows |

## 8. Unknown, and what settles it (before implementation)

| # | Unknown | Settles | Listing or fixture |
| --- | --- | --- | --- |
| 1 | Whether two capitals can be on order 5 / order 8 at one gate at the same time in the engine (per-gate state in the `SA_WarpObjectAway` case's "gate flags", in order 8, in the aligned branch's `0x2000000`, and whether `[gate.f+0x180]` (written by the launch) is read before a second launch) | the abreast refinement's 3 s spacing; not the trail | `0x0046126a` (case 0x0e), `0x004551fe..0x00455330`, `0x004548cf..0x00454996`, readers of `[gate.f+0x180]` in `0x0045b720..0x0045b7f9` and `0x00444170..0x00444a32`, `0x0044b0c0`; the pair filter `0x0045c7b3..0x0045c7e3` is per ship, no count **[m]** |
| 2 | The order-4 handler's reads of `f+0x240` for a class-0x12 target with port −1: uncached at every re-plan tick, no copy taken at the issue, and the container-detour operands (§3.4) do not use the point against the gate inside 2 r_gate | the rewrite's safety at every X | `0x004549c2..0x00454b97` (point read `0x00454a20`, test `0x00454b1a/0x00454b70`, detour `0x0042f970` operands); extend `stageb_sites.py`'s `+0x240` sweep to the handler |
| 3 | The 0x14 handler's lost-leader end: the test, the frame it fires, the result value; `__EscortObject`'s return 8 path | the script latency bound (measured +4.1…+16.9 s parked) | `0x0045820d` listing; KC `__EscortObject` stock `b8b2a` / Mayhem `d10f7` |
| 4 | Order 0 under autopilot: `[obj+0x44] & 4`, and that site B's `[obj+0x14]` write stands through the slew loop every order-0 frame | the order-0 speed hold | `0x00458f8e..0x004590e0`, `0x004594b0..0x004595b1` |
| 5 | The KC task's resume latency after order 8 → `ReleaseFromSlot` (frames between the engine's order-8 end and the slot's release) | the X placement margin (2 frames assumed) | the scheduler's runnable path after `0x004604d0` (open in script-task-scheduler.md); flight: `ctl_x_refused` for followers in Run170 |
| 6 | The SA result protocol for a DLL-ended order: `[obj+0x7c]` lifetime during order 4, `0x00460560 → 0x004604d0` with the task's cell | only the abreast variant's 4 → 5 swap | `0x00460560..0x004605d0`, `0x004604d0..0x00460530` |
| 7 | Which Mayhem script re-targets an escort to a newly arrived M1 and on what condition | the residual re-target risk | `Mayhem.Squadron.*`, `!move.follow.template` CheckLeader (`kc_escort_flow.py` style) |
| 8 | The time factor `[*0x606f34+0xcc]` read (trigger inflation under SETA) | X placement under SETA | a `ship_gate_trigger` row at P4 with `t` and `tf` |
| 9 | `m93`/`m96` passed to `SA_SetSpeedLimit` on a refused warp-in slot | whether `f+0x278` caps the trail speed then | KC `__EnterHyperspace` bcc55 operand; `0x00465d1a` |

Items 2–5 gate the implementation; 1 and 6 gate only the later abreast refinement; 7–9 are flight-row items.

## 9. Alternatives considered

* **Level 2 as named** (DLL-issued order 4/5 at the leader's launch plus a clearance patch in x3story.obj): loses as in §4; the
  escort's order comes from the script 4–9 s after the leader leaves when the escort is inside 10 km, and no DLL order transits
  a ship. The clearance patch (B) remains useful against arrival blocks and is stage 2.
* **Convoy-as-unit with the DLL handing each member to order 5** (the 4 → 5 swap at each member's X): identical timeline to
  (b′) in trail, one more engine protocol to validate; kept as the tool for the abreast refinement.
* **Own transit state machine** (c): the engine's part of a transit is a 0.9 s push; order 4/5 are already steered and timed by
  the module; nothing to replace.
* **Keep the petals as a fallback for a blocked gate**: contradicts the user's rule (no holding pattern); the line's stop at
  spacing is what they asked for, and it keeps noses on the gate.
* **Trail along the funnel line only (no recorded path)**: fails on the lead-in S-curves and the re-departure U-turn, where a
  follower cutting the corner closes on its predecessor's hull; the 6 km ring is 1 KB per member.
* **Hold the time gap instead of the distance gap**: a follower behind a slowing head would close to contact; distance is the
  safe invariant.
* **MSCI edit now** (escorts commit before the leader leaves): 2–5 s per convoy for a Mayhem-file dependency; later, measured.
* **Lanes abreast now** (3 s per escort): needs §8 items 1 and 6, fits only M7-size hulls beside an M2, and places two hulls in
  the bubble at once; after the trail has flown.
