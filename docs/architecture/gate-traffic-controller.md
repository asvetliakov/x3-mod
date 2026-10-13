# Gate traffic controller (queued design, after amendment 17)

Status: queued 2026-10-13 by user decision, to be designed after Run160 (amendment 17) flies. Not implemented.

Level-1 design added 2026-10-13 after the Run160 crowd flight (§Level 1 design below), for ratification by the main session.
Not implemented.

## The problem it replaces

Gate transit is the KC story script's state machine (`docs/reverse-engineering/gate-queue-script.md`): a capital is
sent to a parking point on a 666 m grid, waits, rotates on the spot, then flies through along the axis. Our layer
(`capital-gate-transit.md`, amendments 1–17) owns only the point the ship flies to and its speed, and every amendment
so far has worked around the grid, the rotate and the single pass point. Crowds at one gate (Run158, Run159) are where
that breaks.

## Sketch

1. **Half-disc lanes, defined in the gate's own frame.** Outgoing ships pass through one half of the disc, arriving
   ships are placed and spread into the other half. Because each gate carries its own frame, the rule is identical on
   both sides of a link and needs no state shared between sectors (the far sector is not simulated in detail, so no
   controller can know an arrival in advance). Small ships: the pass point on the disc and the exit spread (site W)
   are already ours. Capitals: depends on §Open questions 1–2.
2. **Outgoing sequencer per gate, in the DLL.** Capitals committed to a gate get a sequence by arrival and a single
   funnel ending at the disc; while waiting they fly a slow wide holding loop (never a stop, never a rotate in place).
   The KC's grid point stays the nominal target; the funnel end is where the ship is when the KC issues the
   fly-through. Level 1: no script change, no new engine calls.
3. **Time rule only where space cannot separate.** If capitals must pass through the centre, or arrivals must appear
   there, a "runway clear" hold: an appearing ship or a capital inside the ring closes the funnel until it is one hull
   length off the axis; arrivals have priority (they cannot wait, a departing ship can loop once more). With off-centre
   passes and placements it shrinks to the rare two-Titan coincidence (disc 2 r_gate ≈ 2.2 km across, two hulls
   ≈ 1.2 km wide combined).
4. **Fast jump-in spread for arriving capitals** (scenario backlog item 2): move the appearing capital off the axis
   quickly.
5. **Level 2, if the script's rotate and wait are still the visible clunk after level 1:** the DLL issues the
   fly-through order itself once the ship is lined up, with a one-line clearance patch in a loose `L\x3story.obj`
   (`gate-queue-script.md` §6) so the script hands over instead of fighting. Needs the order-5 entry and the script's
   reaction disassembled first.

Rejected: teleporting capitals to or through the gate (visible, breaks the engine's warp-in placement and the script's
transit state); a full replacement of the gate script (the sector change, the far-side placement and the slot
bookkeeping are deep in script and engine; nothing gained over level 2).

## Open questions (disassembly dispatched 2026-10-13, to land in `ship-navigation-ai.md` §10.10)

1. What ends order 5 and triggers the transfer: distance to the order point, to the gate origin, a plane crossing, or
   a volume test? Can a capital pass 0.5 r_gate off the axis and still transfer?
2. Who places an arriving ship (engine 0x21 handler vs KC `GetWarpInPos`), and can a capital be placed off the axis at
   site W without breaking orientation, the order chain, the slot bookkeeping or the +z launch?
3. Is there any signal in the current sector that a ship is about to arrive, and if not, what is the earliest
   observable event?

## Answers (disassembly 2026-10-13, `ship-navigation-ai.md` §10.10)

1. **Capital off-axis pass: yes, bounded.** Order 5 ends when the ship's centre is within (|v|/10 + r_ship)·(1 + (tf−1)/4)
   of the stored order point `f+0x240` (the gate origin for a capital), checked every frame at `0x00454138..0x004546e3`;
   the leaf only gets a stack copy, so our redirected point does not move the trigger. A capital can therefore pass up to
   about r_ship + |v|/10 off the axis: 0.5 r_gate (552 m) holds for the M2/M1/TL medians and fails for hulls under 552 m.
   Small ships must pass within ≈ r_ship + |v|/10 of their point B on the 0.55 r arc (≈ 40 m for an M3), so their lane is
   the stored point the KC sets, so a small-ship lane split needs the KC's B or a write to `f+0x240`. "CanWarp" is player-only.
2. **Arrival placement: the engine's, and off-axis is possible at a new site.** The sector post pass launch
   `0x00444170` computes the exit point P (non-capital: a 15° fan by exit slot on the upper 0.55 r arc; capital: on the
   axis, 22 m up; both r_ship in front of the gate plane), sets the ship facing out, starts order 0x21 (900 ms, speed
   zeroed, ends at P at rest), then order 0x1e pushes it straight out at 120 m/s (large) / 240 m/s keeping its lateral
   offset until r_gate (large) / r_gate/4 clear. Nothing re-centres it. Site W never runs on an arrival (the old exit
   spread was inert). An offset of up to 0.6 r_gate written at `0x00444606` (`8b 43 70 | 8b 4b 50`) carries through;
   the only geometric limit is Terran rings (offset + r_ship ≤ 0.663 r). The arrival-side half-disc lane is therefore
   feasible for capitals and small ships alike.
3. **No advance signal for capitals** (their gate-pair slot is script data only). Non-capitals poll
   `SA_IsWarpOutSlotAvailable` and give lead time only when all 6 exit slots are taken. The earliest per-ship event is the
   `SA_StartInHangar` core `0x00443920`, 0–1 main-loop iterations before the launch; the ship reaches P 900 ms after it
   appears. So the runway-clear rule works from the appearance event, and a `ship_gate_launch` row at `0x00444606` settles
   the remaining inferred facts in one flight.

Consequence for the design: both halves of the lane split are feasible (outgoing capitals through an off-axis point, since
the trigger tolerates ≈ r_ship; arrivals placed off-axis at the new site), so the time rule reduces to the appearance
moment plus the two-Titan coincidence. Small-ship outgoing lanes need the stored order point, not only the leaf point.

## Level 1 design (2026-10-13, for ratification)

Design note for the main session. Level 1 means no script change and no new engine order call. The DLL shapes only what
it already shapes: the leaf point at site A, the speed at site B, the P2/P4/P5 hand-over, and site E's verdict. It adds one
new site (the launch window `0x00444606`) and one field rewrite (`f+0x240` of a small ship's order-5 leg, from site A).
This design replaces the capital gate layer of amendments 13–17 (`capital-gate-transit.md` §0–3, §15–17) for capitals at
a gate. Sources:

* engine facts from `ship-navigation-ai.md` §3.1, §10.1–10.5 and §10.10, and `gate-queue-script.md` §2–3;
* the steering law and slip from amendments 13–14 of `ship-steering-and-avoidance.md`;
* radii from `game-scale-and-ship-classes.md` §4, and widths from `capital-widths/` and the Run159/Run160 census;
* flight evidence from [`run160b-triage/`](../../verification/results/ship-steering/run160b-triage/) **[m]**.

Three scripts were made for this design under
[`gate-traffic-controller/`](../../verification/results/ship-steering/gate-traffic-controller/):

* [`design_numbers.py`](../../verification/results/ship-steering/gate-traffic-controller/design_numbers.py) →
  [`.txt`](../../verification/results/ship-steering/gate-traffic-controller/design_numbers.txt): the geometry, arithmetic
  only **[i]**.
* [`pair_cost.cpp`](../../verification/results/ship-steering/gate-traffic-controller/pair_cost.cpp) →
  [`.txt`](../../verification/results/ship-steering/gate-traffic-controller/pair_cost.txt): the cost of the pair test,
  measured on the host.
* [`ring_demand.sh`](../../verification/results/ship-steering/gate-traffic-controller/ring_demand.sh) →
  [`.txt`](../../verification/results/ship-steering/gate-traffic-controller/ring_demand.txt): the per-frame row demand in
  the Run160b crowd window, measured.

Gate frame: R, U, F are rows 0, 1, 2 of the gate. Departing ships come from −F. The "lower half" is U < 0.

**Ratified 2026-10-13 (main session), staged in two builds.** Stage A (Run161): D.1 in full (member board, capsule
sweep on the predictor's plant, `suppress_ctl` at E, member pairs dropped from the sphere avoidance; the yield is a slowdown
never below 10 m/s on the lower-priority member, else the pair goes back to the engine; sideways shifts and petals are
stage B), D.4 in full (placement at `0x00444606`, the arrival-gate row, the small side spawn, `ship_gate_launch`), the
diagnostic ring changes of D.7, the key `ship_steering_gates_traffic`, and the obstacle snapshot capacity 1024 → 2048
(orchestrator's call: 112 KB static, fails closed as before, measured maximum 186). The lane board becomes the member
table; the lane verdicts and certificates are deleted; slow join and the aim-at-A fallback stay until stage B. Stage B
(Run162): D.2 funnel, pass point Q, sequencer and petals, the capital order-5 both-direction write, D.3 escort petals, D.5
the small-ship `f+0x240` rewrite, and the deletion of the join fallback paths; it waits for the D.11 disassembly items
(the `0x00454897` bearing, `f+0x230` readers, the escort switch and the KC's reaction, the order-0 stale remainder).
Everything else as recommended; the rollout drift k and the 200 s span stay placeholders until the fixture measures them.

### Recommendation

* **One controller per gate owns every capital–capital pair among its members.** The members are:
  * capitals committed to the gate;
  * their capital escorts;
  * capitals that arrived through the gate in the last 60 s.

  For a member pair, our sphere avoidance is off. Site E suppresses the engine's warn only while a capsule sweep over the
  next 30 s on the predictor's plant certifies clearance. When the sweep finds a conflict, the lower-priority ship yields:
  it slows (never below 10 m/s), shifts sideways, or goes around. If nothing clears, the pair goes back to the engine.
* **Disc layout.** Teleported arrivals cannot avoid anything, so they get the upper half:
  * capital arrivals are offset +0.6 r_gate up at `0x00444606`;
  * small arrivals keep the engine's upper fan, and move to a side spawn only when a capital hull is at the plane.

  Departures get the lower half:
  * each capital passes through its own pass point Q, at most r_s + v/10 − 100 m off the axis;
  * small ships keep the engine's lower arc, and are moved to the free side by rewriting `f+0x240` while a capital is in
    the funnel.
* **One funnel per gate and a sequencer.** Holders fly slow racetracks ("petals") at 25–60 m/s, at least 8 km from the
  gate. The sequencer releases one ship at a time, so that it crosses its grid sphere aligned (P2 keeps the speed, 0x1f
  is a one-frame no-op) at the moment a model of the KC's through slot says the slot is free. The model uses only
  engine-observable events: our holder's order 5/8, our release, and a capital launch at the gate.
* **Escorts.** Capital escorts (M7 and KC 2019) are steered on 0x14 into petals behind their leader's column. They hold a
  reserved sequence number right after the leader, so a convoy transits together and no escort is ever past X. Small
  escorts keep the formation law.
* **One key, `ship_steering_gates_traffic`.** Off means amendment 17 exactly.

### D.0 The residual this replaces (Run160b crowd, game seconds) [m]

| Residual | Measured | Source |
| --- | --- | --- |
| Crossings at X | 4 of 9 at 79.0°, 104.6°, 100.2°, 103.8°. The two M7 escorts got order 4 4.2–4.5 s after their leader's 25503.9 s launch (on 0x14 until then), so they started order 4 past X. | `crowd.txt`, `cap_orders.txt` |
| Rotates on 0x1f | 20.5 + 28.8 + 20.6 + 14.2 = 84.1 s | `cap_orders.txt` |
| Capital 0x12 | 559.7 s in 24 episodes: arrival 158.4 s (9 episodes), escort/transit 179.7 s (8), exit approach 221.6 s (7) | `episodes12.txt` |
| The chain | E passed it (`pass_not_member` 6,142 of 8,469 capital–capital E rows, `pass_not_transient` 34). The leader M1 warned on its own M7 on order 0 with the leader as target, which is the 3 s order-0 gap the KC leaves every ~33 s between 0x14 re-issues. 2 latches followed (`gate_capital_fallback_pred` 2). | `capcap.txt`, `ecc_window.txt`, `counters.txt` |
| Arrival-side warns | 5 of 9 against the arrival gate itself, at 1,121–1,599 m, right after 0x1e. That is the close-range test d ≤ 1.2 (r_A + r_gate). | `near.txt` (Q4 warn list) |
| Escorts of convoy A | 0x15 to the gate for 92–145 s, then order 4 100.5 s and 187.3 s after their leader's launch. In flight 1 the same leg ended at 10.0 km from the gate. | `cap_orders.txt`, `capital_phases.txt` |
| Departure span | first crossing 25104.2 to last launch 25588.0 = **484 s for 6 departures** | `gate_sequence.txt` |
| Lost rows | `gate_rows_dropped` 4,085 in frames 52900–61300. The E-verdict ring sat at its cap of 8 rows in 401 of 2,829 frames, and every other ring stayed at ≤ 2 rows per frame, so the E ring is the drop source **[i]** (the counter is a single sum). | `counters.txt`, `ring_demand.txt` |

### D.1 Capital–capital protection owned by the controller (question 1)

**Members of the board for gate Γ.** Up to 24 entries per gate; ships are added and removed per decision.

| Role | Entry condition | Engine order |
| --- | --- | --- |
| committed | §16.1 unchanged: a fresh decision, validated KC grid geometry for Γ, order 4 to Γ, or the 0/0x1f hand-over, or order 5/8 to Γ | 4, 0, 0x1f, 5, 8 |
| escort | capital code (`0x0043fd50`), formation membership current (the 20 s memory) with a committed leader on this board | 0x14, 0x15, 0 with the leader as target |
| arrival | capital code, launched at Γ (the `0x00444606` event) less than 60 s ago, still in front of the plane | 0x21, 0x1e, then any |
| evicted | a member that went to 0x12. Not a member for pair purposes. Its sequence number is kept for 30 s. | 0x12 |

**Hull.** Each hull is a capsule: the segment c ± (L − w)·f̂ with radius w.

* w is the LOD read's lateral half-extent. It is already a radial bound (max over parts of √(e_x² + e_y²)), so roll does
  not matter and only the **live forward row** f̂ is needed. That row is `f+kFlightBasis+0x20`, which the trace and E
  already read.
* L = max(r, l), with l the forward half-extent from the same LOD walk. l/r is up to 1.40 in `capital_widths.txt`, so the
  census row gains l.
* Fail-safes:
  * a failed width read gives w = r and L = r, which is a sphere;
  * an unreadable or non-unit forward row certifies nothing.

**Trajectory.** One rollout per member per decision (4 Hz), from the live state: position, live forward row, and velocity
u = v f̂ + slip. It runs on the production predictor's plant (the one `join_predict_grade` steps: controller `0x0043ad20`,
servo, 2 s slip lag, linear acceleration), following the member's current plan. T = 30 s: the slowest member (M1, 5 m/s²)
needs 20.6 s to brake from 113 to 10 m/s, plus 2 s of slip lag. The rollout steps at 0.25 s and stores 61 samples at
0.5 s. Roles the engine moves are rolled out with the engine's own kinematics:

* 0x21 and order 8: the kinematic path;
* 0x1e: axis-parallel at 120 m/s;
* order 0: braking at a along the nose;
* 0x1f: in place.

**Pair test (capsule sweep).** For each sample i, with margin m_i = 50 m + k·t_i + |u_A − u_B|·Δt/2 (k = 5 m/s until the
fixture measures the rollout's 95th-percentile drift):

* a broad test: |c_A − c_B| ≥ L_A + L_B + m_i means clear;
* otherwise a narrow test: segment–segment distance ≥ w_A + w_B + m_i (Ericson 5.1.9).

The verdict is *clear* (all 61 samples) or *conflict at t_c*. It is recomputed at every decision of either ship and
stored in a 24 × 24 matrix per gate (verdict bit and stamp).

**Site E for a member pair.** E suppresses the engine's warn (`suppress_ctl`) iff:

* the pair's verdict is clear;
* it is at most 250 ms old;
* both live forward rows are readable;
* neither ship is on 0x12.

Otherwise E passes and the engine's warn protects the pair. `excluded()` drops every member pair from the sphere
avoidance, because the controller's resolution replaces it. This covers both links that started Run160's chain:

* the leader against its own escort in the 3 s order-0 gap: the escort keeps its membership;
* two committed capitals without a certificate.

**When the test fails.** Priority, highest first:

1. arrival;
2. hand-over, order 5 or order 8 (the slot holder);
3. funnel;
4. holding;
5. escort holding.

Within a role, the higher sequence number yields. The yielding ship Y tries up to three candidates in this order. Each is
a new rollout plus sweeps against every member, and the first that clears every pair is taken:

1. **Speed.** Y's current path at 0.6·v, then 25 m/s, then the 10 m/s floor (holding loops: 25 m/s floor).
2. **Lateral shift.** A funnel moves its final line within its admissible Q window (D.2.2). A petal lengthens its straight.
3. **Go-around.** A funnel ship turns back into its petal, if it is still at least R_turn + 829 m before its grid sphere.

If none clears, the pair is handed back: E passes, our sphere avoidance applies, and `ctl_unresolved` and a pair row are
written. The guarantee is therefore conditional. Members never touch while every member pair is certified. An uncertified
pair is exactly today's protection, never less.

**Cost.** Measured on the host (x86-64, g++ 15.2 -O2 SSE2 scalar), `pair_cost.txt`:

| Case | Cost per pair sweep (61 samples) |
| --- | --- |
| broad test only | 60–62 ns |
| 61 narrow samples, worst | 462–465 ns |

Estimated from that **[i]**:

* A rollout is about 2.4 µs: 120 steps at the predictor's ~20 ns per step (its measured worst is 81 µs for 4,096 steps,
  `pred_cost16.txt`).
* A decision with 23 partners costs 3.8 µs typically (rollout + 23 broad sweeps) and 13 µs at worst (all narrow).
* A conflict decision with three candidates costs at most about 45 µs.
* The pair bound is 20 committed (the KC's warp-in slots) plus arrivals and escorts, at most 24 per board. That is 276
  pairs per board, but a decision touches only its own ≤ 23.
* At 4 Hz per member, staggered, 24 members make about 1.6 decisions per frame at 60 fps: about 6 µs per frame typical
  and about 70 µs in a frame with two conflict decisions. That is inside the existing `join_eval_us_max` budget (≤ 100 µs).
* E adds one matrix lookup per call. The i686 DLL figures are not measured.

**What stays the engine's.** Every pair with a non-member:

* uncommitted capitals: a capital passing on other business, one on 0x12, one on another gate's board, or one with
  unvalidated geometry;
* stations, asteroids, and the gate (except the arrival-gate row of D.4 and the existing gate row);
* ships of r ≥ r_A/3, under the user's rule (an M6 against a 216 m M7);
* the player's ship.

These pairs keep today's sphere avoidance, E's resolved set and the engine's warn. Small ships under r/3 keep the
amendment-17 rule.

### D.2 Outgoing funnel and sequencer (question 2)

#### D.2.1 Disc layout

Separation at the disc is spatial only for what cannot avoid. Ships arrive teleported, appear 2.7 km behind the plane,
are placed by order 0x21 and pushed out by 0x1e. So:

* **Upper half (+U): arrivals.**
  * Capitals are offset +ℓ_a = 0.6 r_gate (663 m) at `0x00444606`. On a Terran ring, ℓ_a + w ≤ 0.663 r − 50 m.
  * Small ships keep the engine's 0.55 r fan (φ within ±45° of up).
* **Lower half (−U): departures.**
  * Capitals use their own pass point Q.
  * Small ships use the engine's lower arc B, or a rewrite of it (D.5).
* **Capital departures against capital arrivals are separated in time by the script.**
  * The pair has one through slot. A departure holds it from 0x1f until its order 8 ends, so no partner-side capital can
    leave, and hence none can arrive, during our order-5 leg.
  * After a partner-side holder releases, our side is refused for 60 s, because the direction changed and the cooldown
    `m16 = release + 60` applies.
  * Measured: the arrivals came 61–72 s apart (`cap_orders.txt` first rows: 24274.3, 24335.5, 24397.3, 24458.2, 24523.2,
    24595.7) **[m]**. The rule itself is **[i]** from §10.2.
  * So the half-disc split protects:
    * departing capitals against small ships spawning into their hull;
    * departing small ships against a capital arrival's 0x21 path.

  It does not protect capital against capital at the plane, and that protection is not needed.

Evidence for the split (`design_numbers.txt` §2, §3):

* The arrival offset keeps a census Titan's hull bottom at U +80 m. That clears the small-departure arc (U ≤ −430 m) by
  369 m beyond r_small.
* The engine's on-axis P overlaps that arc for every hull with w > 290 m.
* A capital departure at the lower Q keeps its cross-section below the upper fan's lowest point (U 430 m).

#### D.2.2 The pass point per ship

The order-5 trigger is the centre distance to the stored point, the gate origin, within (|v|/10 + r_s)(1 + (tf−1)/4)
(§10.10, measured with tf = SETA in Run156). So Q lies in the lower half with |Q| ≤ ℓ_max = r_s + v/10 − 100 m (tf 1,
100 m tracking allowance; SETA only widens it). Preferably Q_U ≤ −ℓ_min, where ℓ_min keeps the hull's cross-section
(w + (L − w) sin 3°) below the upper fan by r_small,max + 50 m. Among those points, Q is the one nearest the slot's grid
lateral A_lat.

| Class (r min / median / max m) | ℓ window at w 0.5 r | ℓ window at w 0.6 r | Terran ring ℓ + w ≤ 2,076 m |
| --- | --- | --- | --- |
| M7 (216 / 560 / 1,431) | 0–126 / 55–470 / 513–1,341 | 0–126 / 108–470 / 649–1,341 | ok |
| M1 (227 / 1,170 / 4,583) | 0–137 / 376–1,080 / 2,172–4,493 | 0–137 / 487–1,080 / 2,606–4,493 | the 4,583 m row (an OWP) does not fit |
| M2 (307 / 1,337 / 2,998) | 0–217 / 464–1,247 / 1,338–2,908 | 0–217 / 591–1,247 / 1,622–2,908 | 2,998 m: ℓ ≤ 578 |
| TL (745 / 1,135 / 1,837) | 152–655 / 358–1,045 / 727–1,747 | 223–655 / 465–1,045 / 901–1,747 | ok |
| Run160 census | Titan r 1,092 w 583: 370–1,002; Colossus 1,022/546: 331–932; M7 760/382: 162–670; M7 778/352: 135–688 | | |

All windows are non-empty **[i]**. A hull under about 300 m passes within 100–200 m of the axis. With ℓ < trigger − 100 m
the stop happens √(t² − ℓ²) before the plane (Titan median 1,265 m at ℓ_min), and order 8 then launches it along F at
lateral ℓ.

**One straight final line where possible.** The funnel's final line Λ is F-parallel through Q. It must also cross the
slot's order-4 sphere (0.75 r_gate = 828 m) as a chord, with |Λ − A_lat| ≤ 678 m. For the census Titan that holds for
**13 of the 20 KC slots** (0–12, `design_numbers.txt` §4). The ship then flies one straight line from the funnel entry
through X, P2 and order 5 to the trigger.

Slots 13–19 cross the sphere on the line nearest the axis and do the rest on order 5: a lateral S of 203–721 m, finished
at least 1.5 km before the trigger. That means the capital order-5 leg gets a both-direction site-B write, as small ships
already have (`kShipFlagGateDock`). The engine's 1 m/s misalignment creep would otherwise fire **[i]**: with Q off the
axis, the bearing to the stored origin exceeds 18.2° within 3 ℓ of the plane, if `0x00454897` measures that bearing (D.11).

#### D.2.3 The funnel

The funnel is the path from the holding exit to the trigger, flown by **one ship at a time per gate**:

* an S-curve at the cornering cap onto Λ (amendment 13's capital lead-in, with its line moved to Λ, window and carrot
  look-ahead unchanged; the join predictor checks feasibility);
* the straight through X with the nose ≤ 8.11°, so P2 keeps the speed and 0x1f ends on its first frame;
* order 5 on Λ to the trigger.

The speed lies between the 10 m/s floor and vmax, and is shaped so that X falls at the release time (D.2.5). There is no
aim-at-A fallback for a member. A funnel whose predictor finds no feasible candidate goes around into its petal.

**Occupancy.** The funnel is occupied from a release until that ship's order 8 ends (its hull leaves the sector), plus a
2 s gap for the KC hand-over frames. The next ship's predicted X time must be at or after that end. The two ships overlap
in the funnel in time, but are about 4 km apart along it. The pair test confirms each case.

#### D.2.4 Holding: petals, never a stop, never a rotate in place

* A **petal** is a racetrack in the plane spanned by F and a radial direction φ around the gate axis:
  * the inner straight heads +F at the ring radius ρ_ring;
  * the outer straight heads −F at ρ_ring + 2R_h;
  * R_h is 1 km, and the straight S_h is 0–2 km (the phase knob);
  * the speed is 25–60 m/s, a turn rate of 1.4–3.4°/s;
  * the lap takes 105–411 s.
* **Where.** The petal's front lies at z_H = −(z_X + L_lead), with L_lead ≥ 2.5 km: about −8 km for a Titan, whose X is at
  −5,221 m **[m]**. That keeps the petal ≥ 2 r_gate from the gate and > 2.5 km from every grid sphere, so **order 4
  cannot end in a petal** (a guard checks the own sphere + 300 m at every decision). Inside d_slow + 3 r + 2 r_s the engine
  creeps at 1 m/s whenever the ship is 45.6° off A; the site-B write therefore covers every member on order 4
  (both directions), not only the 6 r_gate window.
* **Layout.** Petals are spaced Δφ apart around the axis, on the lower 240°. The top 120° stay free for the arrival exit.
  * Two petals' capsules never meet, whatever their phase. A capsule's extent normal to its petal's plane is exactly w,
    so ρ_ring·Δφ ≥ 2 w_max + 100 m is enough.
  * For w_max 583 / 700 / 800 m at Δφ = 30°: ρ_ring ≥ 2.4 / 2.9 / 3.2 km, and 9 petals per ring
    (`design_numbers.txt` §5).
  * More holders go into columns: ring k + 1 at the same angles, D_ring = S_h + 2R_h + 2L_max + 300 m further back.
  * Only a ring-0 petal exits. A column advances when its front empties. The exit path runs inward of ρ_ring and toward
    +F, away from every other petal.
* **Assignment.** On commit, a ship goes to the back of the shortest column nearest its bearing. The KC's maximum of 20
  holders needs 3 rings.
* **Exit at the right phase.** From any phase, the time to the inner straight's front plus the funnel time is known (the
  predictor). The holder changes its loop speed (25–60 m/s) and its straight length so that it reaches the front at the
  release time. The funnel's speed shaping (≥ 10 m/s) absorbs the rest.

#### D.2.5 The sequencer and the through-slot model

**Sequence numbers.** A ship gets one when it enters the committed state (FIFO). Escorts get a reserved number (D.3). The
release order is the lowest number among the ring-0 petals.

**Through-slot model per gate pair.** It is fed only by engine events. Its rules are **[i]** from the KC listing (§10.2):

| Event | Model state |
| --- | --- |
| our member (or any capital) on order 5 or order 8 to Γ | held by ours until it is removed |
| our holder removed after order 8, at t_r | ours exclusively until t_r + 60 (a same-direction request in-sector is exempt from the cooldown; the partner's needs `m16 ≤ now`) |
| a capital launch at Γ (`0x00444606`), at t_a | blocked for ours until t_a + 60 + 1 s |
| more than 60 s since the last event | contested: a partner request can take it at any time |

**Release.** Release the next ship when its earliest X time is at or after max(funnel free, slot free) + 2 s. The
funnel's speed makes up any difference.

* While our side keeps a stream (each request within 60 s of the previous release), the partner side is locked out and
  no ship is refused at X.
* The cadence is one departure per 17–18 s: the kept X→launch time, 15.3–15.5 s **[m]**, plus the hand-over and the gap.
* Six departures therefore need about 6 × 18 s plus one funnel (about 40 s), ≈ 150 s, against **484 s** measured **[i]**.
* A capital launch at Γ while a ship is in the funnel moves the slot-free time 60 s later. The funnel ship slows (at the
  10 m/s floor that absorbs 60 s with 600 m left before X) or goes around.

#### D.2.6 The KC's grid point, rotate and through slot, without a script change

* **Grid.** The warp-in slot is granted before order 4. Its point is only the centre of the 828 m sphere that the funnel
  crosses as a chord. While the ship holds in its petal on order 4 the point is untouched, and order 4 ends only at X.
* **Rotate.** Crossing at X with the nose ≤ 8.11° means P2 keeps the speed and 0x1f ends on its first frame (amendment
  13, as built).
* **Through slot.** The KC grants it to the first requester. Requests are made only after 0x1f, i.e. after X. So the
  sequencer's X order **is** the request order for every member. A mismatch needs one of:
  * a capital outside the controller polling at its point (a fallback, unvalidated geometry);
  * a hidden partner-side holder while the model is contested;
  * the 1–11 s poll of a ship already waiting.
* **Decision: on refusal the ship holds at X stopped, as vanilla does, facing the gate.**
  * After 0x1f the ship is on order 0 inside `TI_WaitRandomForInterrupt`. There is no order to steer and no point to fly
    to.
  * Keeping it moving would mean writing a desired speed on order 0 with no heading control. It would drift along F
    toward the disc, through the order-5 lane of whoever holds the slot, for up to 60 s (600 m at 10 m/s). Level 1
    cannot give it a loop.
  * The stop costs at most v²/2a of coasting (500 m M2, 1 km M1), aligned. When granted, order 5 starts dead ahead, and
    the site-B write removes the creep.
  * The funnel stays occupied, so the ship behind keeps holding.
  * Counted as `ctl_x_refused`, with its wait time.

### D.3 Escorts (question 3)

**Under the script [m]:**

* escorts fly 0x14 (re-issued about every 33 s with a 3 s order-0 gap);
* when the leader launches, they get order 4 once they are near enough: the M7s after 4.2–4.5 s;
* otherwise they first fly 0x15 to the gate for 92–145 s (flight 1: until 10.0 km from the gate) **[i]** threshold.

**Level 1.** 0x14 and 0x15 are steered orders, so the escorts' holding is ours.

* **Capital escorts** (capital code `0x0043fd50`, and KC 2019; the Run160 M7s took the capital path with 0x1f rows,
  **[m]**):
  * When their leader commits, they become escort members.
  * Their 0x14/0x15 leaf goes to petals **in the leader's column, rings 1..k, behind the leader's funnel entry**, in place
    of the formation slot.
  * Their sequence numbers are reserved right after the leader (seq_L + 0.1·i), valid until 30 s after the leader's
    launch.
  * They get order 4 from the KC while they sit in a petal 8–12 km from the gate. That is inside the inferred 10 km
    switch, so there is no 0x15 leg. They then exit as soon as the funnel is free. Our side holds the exclusive slot for
    60 s after the leader's release, so the convoy transits together.
  * No escort is past X when it gets order 4, so no 100° crossing and no rotate.
  * While they are on order 0 with the leader as target, membership holds and E's controller row covers the pair.
* **Small escorts** (M6, M8, fighters; outside the capital code and KC 2019, so they take the small path):
  * They keep the formation law.
  * While their leader is in the funnel, a formation target inside the leader's swept capsule plus 100 m is projected
    out of the funnel corridor.
  * Their gate transit is D.5.
  * An M6 at or above r/3 of a small M7 stays an obstacle for that M7, as today.

### D.4 Arrival side (question 4)

* **Placement at `0x00444606`** (6 B: `8b 43 70 | 8b 4b 50`, contract in §10.10; claimed only with the key on):
  * Capital: P += ℓ_a·U, with ℓ_a = 0.6 r_gate, or on a Terran ring min(0.6 r, h r − w − 50 m).
  * Small ship: unchanged (the engine's upper fan), except when a capital hull is at the plane. That is a capital arrival
    in the 6 s after its launch: 0.9 s of 0x21, then 0x1e at 120 m/s until its rear is 466 m clear. It is also a departing
    capital on order 8, or within 3 s of its trigger. Then the small ship goes to the side spawn (±0.87 r, +0.2 r), the
    side alternating with its exit slot index. That point is 0.89 r from the axis, inside the bubble. The surface
    clearance is 335 / 372 / 536 / 566 m against the four census hulls, and 115 m against an M2 median at w 0.6 r. Above
    w ≈ 0.68 r there is no free spawn point: placement is vanilla and `arr_small_unseparated` counts it.
  * One `ship_gate_launch` row per launch: ship, code, exit slot index, P, offset.
* **The first 60 s.** Site E gets an arrival-gate row: `suppress_arrival_gate` for the pair (A, its arrival gate B) when
  all of these hold:
  * A launched at B within the last 60 s;
  * A is in front of the plane, (x_A − G)·F < 0, and not closing on it, u_A·F ≤ 0;
  * the gate has no collision mesh (the installed ordinary rows 0–3 and 5–14, §10.9), or A's lateral ℓ + w ≤ h r − 50 m.

  The installed ordinary gates cannot be hit at all, so the warn there is a phantom. This removes the 5 gate-triggered
  episodes of Run160b. E's small-ship branch accepts an arrival member as "fresh", because its path is the engine's known
  0x21/0x1e line, so the r/3 rule applies to the escort M6s too (3 more episodes).
* **Keep arriving capitals off the funnel.**
  * The upper offset and the axis-parallel 0x1e keep the arrival laterally clear of Λ. A census Titan departing on Λ
    needs ℓ_o ≥ 580 m against a Titan arriving at +663 m.
  * After 0x1e the arrival is a member with the top priority until its window ends.
* **Priority rule: arrivals win.**
  * From the launch event, the funnel's order-4 part is closed whenever the arrival's rollout capsule enters the funnel
    corridor. It opens again when the arrival is one hull length (L_a + ρ_funnel) off the funnel line, or its window
    ends.
  * Holders yield by rephasing.
  * The order-5 part needs no rule: the KC blocks our side for 60 s after an arrival.
  * Arrivals are never steered off their own order by this rule.

### D.5 Small ships at the same gate (question 5)

* **Which half.** Departures stay in the lower half: the engine's A = G + (0, −0.8, −1.0) r and its B on the lower
  0.55 r arc. Arrivals stay in the upper fan.
* **Splitting the lower half while a capital passes.** A Titan's footprint (ρ ≈ 0.64 r_s) covers the whole ±45° arc
  around (0, −0.55 r). So the capital's Q is biased to the side of its slot's a sign (default +R), and small ships take
  the other side.
* **The order-4 part needs only the leaf.** The engine ends order 4 inside 0.75 r = 828 m of A, so the existing small
  lead-in (site A, amendment 8) can cross the sphere on the free side. Example: (−600, −884) is 1,214 m from a capital
  line at (+600, −700).
* **The order-5 part needs the stored point.** The trigger is ≈ r_s + |v|/10 ≈ 40 m (M3) around `f+0x240` = B, so the leaf
  cannot move the pass point. **Write `f+0x240`, and its rendezvous copy `f+0x230`, from site A.**
  * When: on the first order-5 call of a small ship to Γ, and again whenever the field differs from our last write (the
    KC draws a new θ on each issue).
  * Why it is safe: the handler reads `f+0x240` every frame (`0x0045413e`), and the first frame cannot trigger
    (|AB| ≥ 814 m).
  * Guards: order 5, a class-0x12 target, a non-capital, and a field equal to the arc point the engine would compute.
  * The new point: θ' = the engine's θ mirrored into the free side, so the engine's own spread survives.
  * Only while Γ's funnel is active (a member on the final straight, the hand-over, or order 5/8). Otherwise vanilla.
  * **No new hook.** The consumers of `f+0x230` are not traced (D.11).

### D.6 What of amendments 13–17 is deleted, kept or folded (question 6)

Under the key; with the key off, everything stays as built.

| Item | Fate | Why |
| --- | --- | --- |
| Capital lead-in (13: 6 r_gate window, carrot, both-direction write) | **folded**: the funnel's final straight, moved to Λ; the write extended to every member on order 4 and to capital order 5 | same law, new line |
| P2 keep-speed, live hand-over nose (13, 17.1), P4/P5 | **kept** | the funnel end depends on them |
| Join predictor and its candidates (13, 15) | **folded**: funnel feasibility, plus the plant for member rollouts | already the calibrated plant in production |
| Slow join (16, 17) and the aim-at-A latch | **deleted** for members; replaced by funnel speed shaping and the go-around. The latch row becomes `ctl` go-around rows | the latch produced the 79–148° crossings |
| Lane board (16) | **folded**: it becomes the per-gate member table (identity, lane point = grid slot, role) | the same commit test (§16.1) |
| Verdicts (a)/(b)/(s)/(c), certificates, (b) follow cap (16, 16b, 17) | **deleted** for member pairs; the capsule sweep is the certificate | spheres, footprints and grid intent replaced by the hulls' predicted paths |
| Gate warn row at E for validated approaches (15) | **kept**; the arrival-gate row is added | |
| Width read (16 addendum), census | **kept**; L = max(r, l) added to the read and the census row | capsule length |
| Formation membership memory (15) | **kept** and used for escort members | the 3 s order-0 gaps |
| Small-ship rule r/3 on every order (11, 13, 17) | **kept** | |
| Site W | **deleted** (never reached on a KC arrival, §10.10); `0x00444606` replaces it | |
| Rows: `ship_trace`, warn/warn_pre | **kept** | |
| Rows: E rows | kept for non-member pairs; member pairs log a `ship_gate_ctl_pair` row on a verdict change instead of one per call | the saturated ring |
| Rows: `ship_capital_lane` | becomes `ship_gate_ctl` (member role and sequence transitions) | |

### D.7 Verification (question 7)

**Fixture scenario T** (host, `ship_steering_host.cpp`, the calibrated plant with slip). It reuses scenario M's
machinery: the engine sequence after P2, the warn test, E, and the box hulls. It adds:

* the KC model of §10.2: first-free warp-in slots, one through slot per pair with the 60 s cooldown and the in-sector
  same-direction exemption, partner-side requests as arrivals, and 1–11 s polls;
* the escort script: 0x14 re-issued every 33 s with 3 s order-0 gaps; after the leader launches, 0x15 to the gate until
  10 km away, then order 4;
* the capsule and the box (w, w, L) contact checks.

Cast, the Run160b census:

* convoy A: M2 1,092 leader, M1 1,022 and M2 1,092 capital escorts, three M6s;
* convoy B: M1 1,022 leader, M7 760 and M7 778 escorts, two M6s;
* one lone M1;
* w/r 0.53 / 0.53 / 0.50 / 0.45, and 0.6 r as a variant.

Starts come from the Run160b trace at frame 52947 for the traced ships and from the E-row distances for the rest. An
arrival variant launches six capitals 61–72 s apart, with M6 escorts in the exit slots. A mixed variant interleaves one
arrival into a departure stream.

The oracle is Run160b (D.0). Assertions:

1. Every capital crosses X with the nose ≤ 8° (oracle: 4 of 9 at 79–105°). 0x1f lasts ≤ 1 frame, except after a KC
   refusal.
2. Zero 0x12 between members (oracle: the 195 s chain). Zero 0x12 against the arrival gate within the window (oracle:
   5 episodes).
3. Zero capsule contact and zero box contact between capitals. The minimum separation is reported.
4. Every capital launches within 30 s of its sequence time t_seq(k) = t_launch(k−1) + 18 s, or + 60 s after a direction
   change. `ctl_x_refused` = 0 in the variants without a hidden holder.
5. Escorts start order 4 from a petal: their along-F position is behind X − L_lead and their nose within 30° of F.
6. Every member on order 4 keeps v ≥ 10 m/s (≥ 25 m/s in a petal). No order 4 ends outside X.
7. The span from the first commit to the last launch is ≤ 200 s for the 7 capitals (oracle: 484 s for 6) **[i]** target.
8. Key off: the scenario-M outputs are identical to amendment 17. Unit checks:
   * the capsule distance against brute force;
   * the slot model against the §10.2 rules on random event sequences;
   * the `0x00444606` offset arithmetic, including the Terran bound;
   * the `f+0x240` rewrite: arc radius and side.

**Flight rows and counters** (the next crowd flight, `--debug`):

* rows: `ship_gate_ctl`, `ship_gate_ctl_pair`, `ship_gate_launch`;
* counters: `e_suppressed_ctl`, `e_suppressed_arrival_gate`, `ctl_unresolved`, `ctl_go_around`, `ctl_x_refused` (with
  its seconds), `ctl_evicted`, `arr_offset_capital`, `arr_small_side`, `arr_small_unseparated`, `small_b_rewrites`, and
  per-source drop counters.

Flight acceptance: assertions 1, 2 and 5 on the traced capitals, `ctl_unresolved` attributable row by row, and
`gate_rows_dropped` 0.

**Ring sizes.** In the Run160b crowd window the E-verdict ring hit its cap of 8 rows per frame in 401 of 2,829 frames.
Every other ring stayed at ≤ 2 rows per frame (`ring_demand.txt`, measured). So:

* member pairs write no per-call E row (an edge-triggered pair row instead, at most 1 per pair per second);
* the E ring grows to 32 per frame and its pair table from 32 to 128;
* the warn-watch slots grow from 16 to 32;
* new rings: `ctl` 32, `ctl_pair` 64, `launch` 8;
* the single `gate_rows_dropped` becomes per-source counters.

Check: `ring_demand.sh` run on the next crowd log shows no kind above its new cap, and every drop counter is 0.

### D.8 Cost, option-off, native Windows, risks (question 8)

**Cost.** D.1 gives the decision work: about 6 µs per frame typical and about 70 µs worst **[i]**, from the measured
sweep costs. Per frame otherwise:

* site B: unchanged, one write;
* E: one matrix lookup;
* `0x00444606`: once per launch;
* the `f+0x240` rewrite: once per small order-5 leg.

Memory, all static: 4 boards × 24 members × 61 samples × 24 B ≈ 141 KB, plus the 24 × 24 matrices. No allocation, no lock,
game thread only, SSE2 scalar. The performance pass is the host bench, extended to the full decision. The i686 numbers are
not measured.

**Option-off.** One key, `ship_steering_gates_traffic` (`X3M_…`), needing `ship_steering_gates_capital`. Off is
amendment 17 exactly:

* no claim on `0x00444606`;
* no E controller or arrival rows;
* no rewrite;
* the lane board in its amendment-17 role.

The fixture asserts this (D.7 item 8). Rollback in flight is the key.

**Native Windows.** The game now runs natively at `C:\X3`, so this is the flight platform itself. The hooks are in-process
patches of the same `X3AP.exe`. The window bytes are re-verified by `arrival_sites.py` (121 checks) and claimed through
the existing `engine_patch::verify_bytes`/`claim` path. The design uses no OS or Wine API beyond what the module already
uses (QPC for diagnostic timing only).

**Risks.**

* **The script's slot order disagrees with the sequence** (a non-member polling, a hidden partner holder, a poll in
  progress).
  * The released ship holds at X, stopped and aligned, as vanilla does.
  * The funnel stays occupied, and the sequence is never reordered.
  * Bounded by the KC's 1–11 s poll once the slot frees, or 60 s after a hidden arrival.
  * Measured in flight by `ctl_x_refused` against the `ship_gate_launch` times.
* **A member goes to 0x12 against an uncommitted ship**, the engine's warn for a non-member pair.
  * It becomes evicted: pairs fall back to today's rules, and its sequence number is kept for 30 s.
  * The funnel opens again once its capsule clears the corridor.
  * When the KC re-issues order 4 to the same grid point, it re-enters as a holder.
  * Order 5 cannot be interrupted, because the KC ignores CollisionWarn on that leg (`m45 = 0xA0001`).
* **Save or load mid-loop.** The board is in-process only.
  * On load, entries fail the pointer and id check and are dropped. Members are rebuilt from the engine state at their
    next decision, with sequence numbers by distance to X.
  * A holder on order 4 just continues.
  * A ship waiting at X is vanilla.
  * An arrival in flight at save time misses its launch event: one vanilla gate warn.
* **The KC may react to a capital escort far from its formation slot.** Not traced (D.11). The petals keep escorts within
  a few km of the leader's column.
* **Long order-4 legs.** Holders stay on order 4 for minutes. Run160b shows 803 s legs with no KC reaction **[m]**.
* **The off-axis pass is visible.** The hull passes 0.4–1.0 km below the bubble centre, partly outside the bubble's
  0.91 r section. This is a user check at the first flight.

### D.9 Level 2 (later)

The DLL would issue order 5 itself once a ship is lined up. A one-line clearance patch in a loose `L\x3story.obj`
(`gate-queue-script.md` §6) would make the script hand over instead of re-requesting. That removes:

* the remaining stop at X on a refused slot;
* the 0x1f step;
* the KC's poll randomness.

It also lets the sequencer own the through-slot order outright. Prerequisites: the order-5 entry and the script's reaction
to an externally started order 5, both disassembled; and level 1's petals and funnel, which level 2 reuses unchanged.

### D.10 Alternatives considered

* **Amendment 17 plus E fixes only.** Suppress the order-0 escort gap and certify committed pairs by boxes. This loses
  because it leaves the escorts past X (the 100° crossings), the rotates, the 92–145 s follow legs and the 484 s span.
  The engine's sphere-and-cone warn stays the only protection.
* **One shared holding track (a train on one racetrack).** This loses on three counts:
  * Every ship's final line differs, because each must cross its own grid sphere.
  * Joining a moving train is itself the conflict to solve.
  * The exit order is fixed by train position, not by sequence.

  Petals are independent, and only the petal joins need the pair test.
* **Time-only sequencing on the axis**, with the funnel closed on every arrival. This loses because:
  * a small arrival spawns into a departing capital on the axis (fan at 607 m against w 350–800 m);
  * a capital's 0x21 path on the axis crosses departing small ships near B.

  The KC already supplies the capital–capital time rule.
* **Vanilla stops at the grid points.** This breaks the user's "never a stop". Parked hulls 666 m apart also overlap
  laterally (two census Titans need 1,166 m).
* **Rewriting a capital's `f+0x240` to Q.** It would work, but it is not needed: the origin trigger already tolerates
  ℓ ≈ r_s. It is one more write into a field the order handler reads.
* **An oriented-box SAT test.** It needs the full basis and 15 axes per sample for no gain. The LOD w is already a radial
  bound, and the capsule is roll-free.

### D.11 Unknown, and what settles it

| Unknown | Settled by |
| --- | --- |
| The through-slot timing is **[i]** for partner-side out-of-sector requests: release before the appearance, 60 s blocked | flight: `ship_gate_launch` times against `ctl_x_refused`; in-sector: a KC trace of `__requestWarpThroughSlot` on the next run |
| When escorts switch from 0x15 (to the gate) to order 4 (flight 1: at 10.0 km); whether the KC reacts to an escort far from its slot | a KC walk of the leader-gone branch of `__EscortObject` / `__FlyToNextSector` (`kc_gate_flow.py` style) |
| Which bearing `0x00454897` tests (the stored point or the leaf copy), and what the gate flag 0x10000000 does | one listing of `0x00454880..0x004548ac` and a sweep for readers of the flag |
| Readers of `f+0x230` (the order-5 rendezvous) before the small B rewrite | a `.text` sweep for `+0x230` readers in the order handlers |
| Whether order 0 keeps integrating a stale remainder (17.1) at X | a listing of the order-0 path under autopilot |
| The rollout drift k (5 m/s assumed) | fixture: the 95th percentile of the |rollout − plant| per second of horizon |
| The forward half-extent l per installed capital | the census row with l (flight) and `capital_widths.py` extended (static) |
| The i686 cost | the DLL's `join_eval_us_max` / `handler_us_p99` in the first flight |
