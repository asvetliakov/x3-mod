# Gate traffic controller (queued design, after amendment 17)

Status: queued 2026-10-13 by user decision, to be designed after Run160 (amendment 17) flies. Not implemented.

Level-1 design added 2026-10-13 after the Run160 crowd flight (§Level 1 design below), for ratification by the main session.
Stage A implemented 2026-10-13 (§A.1), not flown and not installed; stage B not implemented.

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
(orchestrator's call: fails closed as before, measured maximum 186; the static cost is +213,072 B over 1,024, 4 snapshots of
106,516 B, measured in §A.1, not the 112 KB first written here). The lane board becomes the member
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

Memory, all static: 4 boards × 24 members × 61 samples × 24 B ≈ 141 KB, plus the 24 × 24 matrices (as built: 191,088 B
measured, §A.1). No allocation, no lock,
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

### D.12 Corrections from the stage-B disassembly (ship-navigation-ai.md §10.11, 2026-10-13) [m]

* **D.2.2:** the order-5 misalignment test compares the nose (flight basis row 2) with the direction to the live stored
  point `f+0x240` (the gate origin for a capital), after the leaf call in the same frame, threshold 18.195°. The 1 m/s
  creep therefore starts before the trigger whenever ℓ > 0.312 t, i.e. on practically every off-axis Q (Titan creep
  distance 88–2,590 m). The capital order-5 leg to Q needs the both-direction site-B speed write on **every** off-axis
  Q, not only slots 13–19; site B runs after the handler's 500 write and nothing but the slew caps touches `[obj+0x14]`
  afterwards, so the write holds every frame. Exception: a capital carrier being approached that frame (order 5 or close
  order 4) has its speed and turn zeroed after site B.
* **D.5:** only the order-5 handler reads `f+0x240` on a gate leg, every frame, uncached, and never rewrites it for a
  gate; a rewrite from site A is seen by that frame's misalignment test and by the trigger test from the next frame.
  `f+0x230` is unread on a gate leg (its readers belong to 0x12 and 0x1e): writing it is unnecessary.
* **D.3:** the escort switch is Mayhem script, not KC: while the leader is in the sector the escort loops "escort ship"
  (0x14, 30–34 s legs, 2–3 s waits); once the leader's sector differs, beyond 10 km from the gate it issues "follow gate"
  (0x15, 5 s legs, precision 5 km), within 10 km "fly to sector" → KC order 4 to its own grid point. No escort gets order
  4 before the leader has left the sector; nothing reacts to an escort far from its slot. Petals for escorts therefore
  live on 0x14 (leader present) and 0x15 (leader gone, > 10 km); the switch to order 4 happens at 10 km from the gate.
* **17.1 / D.8:** order 0 never zeroes the turn remainders `obj+0x30/34/38` and the integrator keeps consuming them under
  autopilot; a first-frame 0x1f zeroes only roll. A capital handed over with a pitch remainder keeps turning through the
  through-slot wait. Stage B: the P2 stub zeroes the three remainders on a kept hand-over (within its contract), so the
  ship holds the attitude the funnel delivered.

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

### A.1 Implementation (stage A)

Built 2026-10-13 (Opus 5.5) on main `77ddda08`; not flown, not installed. Stage A as ratified: D.1 with the speed yield only,
D.4, the ring changes of D.7, the key, and the snapshot capacity 2048. D.2, D.3 and D.5 are not built. Ledger:
[ship-steering.md](../verification/ship-steering.md), "Gate traffic controller, stage A".

**Sources.**

* `src/proxy/ship_gate_ctl_core.h` (new, portable, pure). The boards, the members, the rollout, the capsule sweep, a decision,
  E's certificate and the arrival arithmetic. The DLL and the fixture share it, including the commit and escort fills and
  the role check.
* `ship_steering_core.h`: the predictor's step is now `plant_step()`. `join_predict_grade` calls it with the same arithmetic
  in the same order, and every fixture line is unchanged.
* `ship_avoidance_core.h`: `kSnapshotCapacity` 2048, `Self.ctl_ptr/ctl_id/n_ctl` with `ctl_excluded()`, and three verdicts
  (`suppress_ctl`, `pass_ctl`, `suppress_arrival_gate`). It adds four `SiteE` fields whose defaults are amendment 17.
* `ship_steering.cpp`: the key and the claim, the stub and handler at `0x00444606`, the commit and escort refreshes in
  `decide()`, the role check at site B, the decisions in `frame_boundary`, E, the avoidance exclusion, the yield cap, the rows
  and the counters.
* `ship_steering_sites.h` (n) with `verify_ship_steering_sites.py` check `arrival_launch_site`, and the config key.

**Key.** `ship_steering_gates_traffic` (`X3M_SHIP_STEERING_GATES_TRAFFIC`) defaults to 1 and needs
`ship_steering_gates_capital` active. Refusal reasons: `invalid_setting`, `needs_gates_capital` (set explicitly),
`late_claim`, `bytes_mismatch`, a claim status, `stub_chain_failed`, and `rollback_failed_live_site_passes_through`.

* `0x00444606` is claimed alone, after the gate group. All or nothing: a refusal rolls the site back and leaves the
  controller off, and the gate group stays.
* The stub tests a live byte, saves EAX/ECX/EDX and XMM0–7, runs `cld`, and calls `x3m_ship_gate_arrival(EBX, EDI)`. It then
  continues into the claim tail, which replays the window and jumps to `0x0044460c`. ESP and EBX/EDI/EBP/ESI are untouched,
  and no x87 instruction runs. The handler runs under `Envelope` (MXCSR and LastError).
* Off is amendment 17. The site is not claimed, no board is fed, there is no E row or exclusion, and the lane board keeps
  its amendment-17 role. `keyoff_identity.sh` shows this (below).

**Members (D.1).** Up to 4 boards × 24 members (`kGates` = 4), and one board per ship. The table below gives each role's entry and its validity, which site B
checks every 100 ms (`gc::validity`). 0x12 evicts a member; its sequence number is kept 30 s.

| Role | Entry | Holds while |
| --- | --- | --- |
| committed | an order-4 or order-5 decision on validated capital grid geometry. This is the lane board's commit test, at the points where `lane_decision` ran, which it replaces | order 4 or 5 to its gate with a commit within 1 s; order 0 or 0x1f within 120 s of P2; order 8 |
| escort | a capital on 0x14 or 0x15 whose leader is a committed live member (`decide()`) | 0x14 or 0x15 to that leader; order 0 while the formation memory holds the pair (the KC's 3 s gaps) |
| arrival | a capital launched at the gate (the `0x00444606` hook) | 60 s, and in front of the plane once 0x21 ends |

* The board slot of a dead member is reused after 60 s without a state read.
* A full board or a fifth busy gate refuses the ship, and the refusal is counted (`ctl_full`). The ship's pairs then keep
  amendment 17's rules.
* Save and load (D.8): an entry left from before the load fails the id check at site B, E or `decide()` and leaves. Members
  re-enter at their next decision with sequence numbers in entry order, not by distance to X as D.8 says.

**Rollout (61 samples at 0.5 s from 0.25 s steps).** Each member gets one plan:

| Member | Plan |
| --- | --- |
| order 4 (`line`) | the predictor's plant along its join line. Past the crossing plane (P2) it brakes at `a` on its last velocity. |
| order 5 (`dock`) | straight to `f+0x240` until the order's radius `|v|/10 + r_s` |
| order 0 (`brake`) | brakes at `a` along the nose |
| 0x1f (`hold`) | in place |
| 0x21 / 0x1e (`warp_in` / `exit`) | the engine's kinematic path, then axis-parallel at 120 or 240 m/s |
| order 8 (`launch`) | 0.9 s along +F, then gone |
| escort on 0x14 / 0x15 (`follow`) | its leader's rollout at its current offset |
| anything else (`drift`) | its velocity |

* The noses are normalised once per rollout.
* A rollout older than another is read on the shared 0.5 s grid. The residual offset (≤ 250 ms) × the other ship's
  largest speed is added to the margin.

**Sweep.** Broad: the centres against `L_A + L_B + m_i`, with the per-pair bound `(v_A,max + v_B,max)·Δt/2` for
`|u_A − u_B|·Δt/2`. Narrow: the capsule axes' distance (Ericson 5.1.9 specialised to unit noses, one division) against
`w_A + w_B + 50 m + k·t + |du|₁·Δt/2`. The L1 norm of the sampled relative motion overestimates by at most √3, so the test
runs on squares. Hulls are w = the LOD width (w = r unread) and L = max(r, l); an arrival takes w and l from the last read
of its type.

* **k = 10 m/s** (`gc::kDriftMs`), decided 2026-10-13 by the coordinator over D.1's 5 m/s placeholder, which was not
  evidence-based. 10 m/s keeps the certificate useful and doubles the margin at 30 s.
  * The measured drift with k = 5 is far larger: p95 436 m at 5 s and 2,230 m at 30 s, for steered plans over horizons
    without an order change.
  * Its sources are avoidance detours, escort catch-up and the yields' own speed changes. Stage B models the yields.
  * The certificate stays sound by refresh, margin and failing closed on staleness (the soundness argument below), not by
    the k·t term bounding the drift.

**Decisions.**

* Decisions run in `frame_boundary`, never in a handler. Each member decides at most every 200 ms and only on a newer
  state.
* The frame bound is 6 decisions and 14 rollouts. A decision with every yield takes 7 rollouts.
* The verdicts sit in a 24 × 24 matrix with stamps.
* Yield (stage A): the lower-priority ship tries its plan at 0.6·v, then 25 m/s, then the 10 m/s floor, each only below
  its plan speed. Priority is arrival > hand-over/5/8 > order 4 > escort, and within a role the higher sequence yields.
  * Only a committed ship on order 4, or an escort on 0x14 or 0x15, can yield.
  * The first candidate that clears every pair is taken. Its speed becomes the ship's cap, which `ln::apply_cap` applies in
    `decide()` (lowering only). The cap lapses when the uncapped rollout clears, or 1 s after it was set.
  * At most 2 yield resolutions per decision. Any other conflict is handed back (`unresolved`).

**Site E.** Arrival-gate row first, then member pairs, then amendment 17 unchanged.

* The member pairs (both ships live on one board, ids checked) are decided before the steering-table check: `suppress_ctl`
  iff the verdict is clear and ≤ 250 ms old, both live forward rows are unit vectors, and neither ship is on 0x12. Otherwise
  `pass_ctl`, and no other row applies.
* `a_arrival` makes the small-ship row accept a capital arrival without a steering decision. It applies to that row only.
* With the key on, the lane certificates and E's lane verdict are not computed.

**Avoidance.** A member pair with a clear verdict ≤ 500 ms old is dropped in the deciding ship's resolve. A handed-back,
unknown or stale pair keeps today's sphere avoidance.

**Arrivals (D.4).** At `0x00444606`, skipping the player's object and manual control:

* A capital gets P += 0.6 r_gate U. On a Terran row (21–24) the offset is min(0.6 r, 0.663 r − w − 50 m), clamped at 0. An
  unknown row gets no offset.
* A small ship goes to the side spawn (±0.87 r by its exit slot's parity, +0.2 r) while a capital hull is at the plane: a
  launch within 6 s, or a member's P4 or order-8 frame within 3 s.
  * Above w > 0.68 r it is placed vanilla and counted in `arr_small_unseparated`.
* The int32 write is range-guarded.
* Every launch is recorded (64 records, 60 s) for the arrival-gate row and gets one `ship_gate_launch` row.

**Rows and rings (D.7).**

* E: 32 rows per frame and a 128-pair table, with no per-call row for a member pair. The warn watch has 32 slots.
* New rings: `ship_gate_ctl` 32 (role transitions), `ship_gate_ctl_pair` 64, and `ship_gate_launch` 8.
* A pair row is written on an edge into or out of a hand-back, or on the loss of a certificate. A first certificate is
  silent. At most one row per pair per second.
* The summary has `rows_dropped=` per source (gate_event, join, latch, lane, gate_trans, warn_pre, engine_warn, warn_watch,
  e_table, e_ring, ctl, ctl_pair, launch), and `gate_rows_dropped` stays their sum.
* The traffic counters: `ctl_*`, `e_suppressed_ctl`, `e_pass_ctl`, `e_suppressed_arrival_gate`, `avoid_ctl_excluded`,
  `arr_*`, `ctl_decide_us_max`, `ctl_frame_us_max` and `ctl_arrival_us_max`.
* Stage B's `ctl_go_around`, `ctl_x_refused` and `small_b_rewrites` are not emitted.
* The census row gains `l_r=` and `capsule_L_m=`. Flight census so far (Run160b, measured): l_m 1092/1022/763/778 for r
  1092/1022/760/778, so l/r 1.000–1.004. Static l/r 0.416–1.400 (`capital_widths.txt`).

**Kept as built under the key:** slow join and the aim-at-A fallback (stage B deletes them), site W, the gate rows of E, the
width read and the formation memory.

**Scenario T** (fixture, stage A, 9 runs per variant). Producer:
[`scenario_t_summary.py`](../../verification/results/ship-steering/gate-traffic-controller/scenario_t_summary.py) →
[`.txt`](../../verification/results/ship-steering/gate-traffic-controller/scenario_t_summary.txt), measured.

* The model is the KC's first-free grid slots and the through slot. That slot has a 60 s cooldown on a change of direction
  and none for the same direction, and polls every 1–11 s.
* Escorts take the 3 s order-0 gaps every 33 s, hold 10 km out on 0x15 after their leader's launch, and take order 4 after
  the Run160b delays (100.5 s and 187.3 s; 4.2 s and 4.5 s).
* The engine's partner filter is modelled; for two escorts of one leader it is **[i]**.
* Arrivals are 61–72 s apart, with M6s in the exit slots.

The table is after the review fixes (below), at k = 10 m/s.

| Variant | 0x12 episodes per run (grid-bound / other) | min capsule (all / not grid-bound) | certified frames, contact (closest) | member warns E suppressed | yields / hand-backs |
| --- | --- | --- | --- | --- | --- |
| departures (census w) | 24–149 (194 / 330) | −686 / −685 m | 967,331, **0** (74 m) | 57.8 % | 3,888 / 17,892 |
| departures w 0.6 r | 24–161 (357 / 257) | −803 / −803 m | 924,445, **0** (77 m) | 46.7 % | 3,644 / 25,157 |
| arrivals (both widths) | no member pair in range | 2,558 m | 0 | — | 0 / 0 |
| mixed | 2–7 (6 / 36) | **233 m** | 288,444, **0** (233 m) | 82.4 % | 1,419 / 1,801 |

* The worst frame reached 3 decisions and 12 rollouts; the bound is 6 and 14.
* Coordinator's decisions (2026-10-13):
  * the departure residual (parked neighbours at 666 m, crossings, escort order-0 gaps) and the save/load sequence order
    are stage B's;
  * site W stays, and the key defaults on;
  * the capsule is the ratified hull bound; the box gap is a bound of a bound and is reported only.
* **Asserted, all pass:**
  * no capsule contact on any frame a pair is certified clear (all variants);
  * no capsule contact in the mixed variant;
  * D.7 assertion 6: a committed member on order 4, once at 10 m/s, is never written below 10 m/s by the module or the
    controller (the desired speed after site B);
  * every capital arrival placed 0.6 r up and kept to the end of 0x1e (6/6 per run);
  * the arrival-gate row suppresses every arrival warn against its gate inside the window (0 unsuppressed);
  * small arrivals with a capital at the plane go to the side spawn (min clearance 554 m to the capital capsule).
* **Not asserted:**
  * The arrival variants hold no member pair in range (the capitals arrive 61–72 s apart), so nothing about member pairs
    is asserted there, and each run prints "no member pair in range".
* **Reported, not met (54 TARGET MISS rows):**
  * "No engine evasion between members" in the departure and mixed variants.
    * In the departures, 194 of the 524 episodes are pairs whose KC grid points are closer than w_A + w_B + 50 m
      (adjacent points 666 m apart, census w + w = 1,129–1,166 m).
    * The rest are crossings near a waiting ship and escorts in the KC's order-0 gap. Slowing to 10 m/s does not clear
      these within 30 s.
  * "No capsule contact" in the departures. Stage A hands such pairs back by design, and the fixture does not model the
    engine's 0x12 evasion.
  * The actual speed below 10 m/s on order 4 under the engine's own law (the aim-at-A fallback on a detour gets the engine's
    1 m/s creep for a misaligned capital, and its slew back up): 50–2,041 frames in 9 of the 18 departure runs, lowest
    1.0 m/s. This is amendment 16's path, which stage B deletes.
* **Rollout drift** (p95 of |rollout − flown| over horizons without an order change at or after the read; the worst run):
  * steered plans 537 / 1,056 / 1,889 / 2,442 m at 5 / 10 / 20 / 30 s (k = 5 build: 436 m at 5 s, 2,230 m at 30 s);
  * engine-moved plans 15 / 62 / 132 / 139 m.
* **The engine-moved jump.** After k = 10 the engine-moved figure rose from 10 to 305 m at 5 s. The cause was not k: the
  plans the engine moves never yield. There were two causes:
  * The fixture read site B's state before each ship's order step. At the frame where the KC's 3 s order-0 gap of an
    escort ended, the decision therefore rolled out a braking plan for a ship that was back on 0x14 at 130–143 m/s.
  * The drift filter compared the order change strictly after the read (`>`), so a change in the read's own frame
    counted.
  * k changed the decision cadence, so more decisions landed on those frames. Now the read runs after the order step, as
    the engine's command pass does, and the filter is `>=`. Engine-moved drift is back to 15 m at 5 s.

**Review fixes (two reviews of `d619f4de`, 2026-10-13).**

* **One board per ship.** `gc::enter` removes the ship from every other board, whatever its role. A leave to none clears
  the slot, so no pointer is left behind for `locate` or the bitmaps. An eviction keeps ship and id for its 30 s sequence
  memory. `locate` returns a pair-role entry first, an evicted one only when none exists, and never a none entry. The DLL
  rebuilds the bitmap on every role change.
  * Fixture (unit scenario, two boards): an arrival at gate A that then commits to gate B leaves exactly one entry, B's.
  * Site B's role check, E's lookup, the avoidance's exclusion and the cap block all read that entry.
  * A none leave leaves nothing for `locate` to find.
* **Fail closed.** No certificate (verdict none: the engine's warn and the sphere avoidance, not a conflict to resolve)
  when:
  * either rollout has no samples (a refused plane, a NaN state);
  * the two rollouts' t0 lie more than 1 s apart;
  * fewer samples pair up than the engine's warn reach in time: 1 s plus min(600 m / v, v / 2a) for the faster of the two,
    and at least 4 samples.
  * The drift term charges each side from its own t0: `k·(t + |t0_A − t0_B|)`.
  * Unit checks: a NaN body rate, a partner rollout 30 s older, an overlap below 4 samples or below the warn reach, and a
    starved decision each give `e_certified = 0`.
* **Freshness at E** (and the exclusion): a clear verdict counts only while it is ≤ 250 ms old and both members decided
  within 1 s. A yielder whose decisions starve loses its certificate.
* **A real certificate bug, found by the fail-closed rerun at 144 fps and fixed.** The order-4 rollout took its nose from
  the plant's in-plane angle, which drops the live nose's component out of the plane of the line. Two M7s at adjacent grid
  points were therefore certified while their true capsules overlapped by 29 m (34 and 625 frames in two 144 fps runs).
  The rollout now turns the live nose about the plane's normal by the plant's in-plane turn, so sample 0 is the true hull.
  Since the fix: 0 contacts in 2.18 M certified frames.
* The avoidance exclusion, `ctl_depart` and the fixture's exclusion check the ship's native id against the entry.
* An escort's yield candidate follows its leader's rollout (`Plan::follow`). It falls back along the leader's path by the
  leader's speed above the candidate, instead of coasting straight.
* `gate_no_mesh` takes rows 0–20, per §10.9: row 4 (Xenon, a blended disc only) and rows 15–20 (inactive hubs, an empty
  body stub) have no collision mesh either. Terran rows 21–24 keep the 0.663 r bound. A row above 24 gets no offset and no
  arrival-gate row.
* Key off: `type_width_put` (the per-type width cache for the arrival placement) now runs only under the key. Before, it
  ran with the key off as a diagnostic-only write that no path read.
* **The arrival handler's reads.** The ship and gate pointers are null-checked and their class words checked before any
  other read, as in the other handlers. The gate's phys pointer and its flight block are null-checked, and the frame must
  be orthonormal. No handler can detect an unmapped page (no SEH), so a valid EDI from the engine is the premise.
* **The summary row.** Every append to the 4,096-byte gates buffer is bounded (snprintf truncates and the used length is
  clamped). The worst case is 3,614 characters with every conversion at its widest: producer
  [`summary_bound.py`](../../verification/results/ship-steering/gate-traffic-controller/summary_bound.py) →
  [`.txt`](../../verification/results/ship-steering/gate-traffic-controller/summary_bound.txt).
* **Boards.** `kGates` = 4 in the spec and the code, with 24 members each. A fifth gate with live members finds no board:
  `acquire_gate` refuses and counts `ctl_full` (gates), and its capitals stay non-members under amendment 17's rules. A
  small launch there keeps the vanilla placement. A board without a live member is reused.
* **Static memory, measured** (`sizeof`, i686 = host layout): producer
  [`static_sizes.cpp`](../../verification/results/ship-steering/gate-traffic-controller/static_sizes.cpp) →
  [`.txt`](../../verification/results/ship-steering/gate-traffic-controller/static_sizes.txt).
  * Board 188,520 B (Member 1,696 B, Gate 47,128 B), arrivals 1,544 B and two bitmaps of 512 B: the controller is 191,088 B.
  * Obstacle snapshot at 2,048: 106,516 B each × 4 slots = 426,064 B, which is +213,072 B over 1,024.
  * The ratification paragraph's "112 KB" was wrong.

**Soundness argument.** A certificate is the claim that two hulls will not touch before the next decision refreshes it.
It rests on three things.

1. **Refresh.** Every verdict that E or the exclusion uses is at most 250 ms old, and both members decided within 1 s. These ages are
   game time; B.3 (review F5) adds the last frame step to each, so a record fresh before a long SETA step stays fresh across it and a
   stale one stays stale, and caps the verdict class at 2 s.
   Each decision starts from a state read at most about 100 ms earlier, so the near end of the rollout is the true hull. The
   fixture's sample-0 capsule equals the hull since the nose fix.
2. **Margin.** 50 m, plus `k·(t + |Δt0|)`, plus the sampled relative motion over half an interval.
3. **Fail closed on staleness.** No samples, rollouts more than 1 s apart, an overlap shorter than the warn reach, a stale
   verdict or a starved member all give no certificate. The engine's warn then protects the pair.

The `k·t` term does **not** bound the measured drift: at 30 s the drift p95 is ~2.4 km against k·t = 300 m. The 30 s
horizon is a conflict detector, and safety comes from refreshing faster than the hulls can close the margin. The fixture
measures this, without proving it: 0 contacts in 2.18 M certified frames across 45 runs, closest 74 m.

**Key-off identity.** Producer:
[`keyoff_identity.sh`](../../verification/results/ship-steering/gate-traffic-controller/keyoff_identity.sh) →
[`.txt`](../../verification/results/ship-steering/gate-traffic-controller/keyoff_identity.txt). It compares against
amendment 17's sources extracted from `77ddda08`.

* E: 0 / 400,000 differences.
* Resolve: 0 / 60,000 (24,499 adjusted), floats bitwise.
* Every pre-existing fixture line is unchanged apart from host timings, including every scenario-M line.

**Cost** (host x86-64 -O2, measured; i686 not measured):

* pair sweep 61 ns broad only, 444 ns at 61 narrow samples;
* a decision on a full board (24 members, 23 pairs), after the review fixes: 4.2 µs spread, 7.0 µs packed (1.6 km apart: narrow
  and clear), 29.9 µs in conflict with yields (4.0 / 6.9 / 28.6 µs before them; the overlap and freshness tests add little);
* the frame bound's worst admitted frame (5 plain + 1 in conflict) 65.1 µs;
* site E's member lookup 7 ns (without the game reads);
* scenario M's E unchanged (key off);
* the predictor unchanged: 16.1–16.2 / 18.8–19.5 µs, host noise.

**Ring replay.** Producer:
[`ring_replay.py`](../../verification/results/ship-steering/gate-traffic-controller/ring_replay.py) →
[`.txt`](../../verification/results/ship-steering/gate-traffic-controller/ring_replay.txt), on the Run160b crowd window.
It finds 0 drops at the new sizes under the retry model: the worst E frame is 16.3 rows (cap 32), the pair table 50 (cap
128), and the warn watch 9 (cap 32). **[i]**: the log's drop counter counts retries, so the true demand is inferred, and
the naive bound (every drop its own row) is printed beside it.

### B.1 Implementation (stage B)

Built 2026-10-13 (Opus 5.5) on main `ad0e131a` (stage A + amendment 18, Run161) with `c604ed0a` merged; not flown, not installed.
Stage B as ratified (D.2, D.3, D.5, D.6, D.8 with the D.12 corrections) plus the Run161 additions (items 1–5 below). Ledger:
[ship-steering.md](../verification/ship-steering.md), "Gate traffic controller, stage B". Evidence directory:
[`gate-traffic-controller/`](../../verification/results/ship-steering/gate-traffic-controller/) (`*_b.*`).

**Sources.**

* `ship_gate_ctl_core.h` (shared by the DLL and the fixture): the stage of a member (`Sb`: none, hold, funnel, handover, dock,
  launch), Q and the final lines, the petals and their pursuit (`plant_step_to`, the predictor's plant toward a given carrot), the
  rollout plans `petal` and `line5`, the sequencer `sb_frame` (once per gate per frame), the per-decision steering `sb_steer`, the
  small-ship B mirror, E's pass reasons, and the order-0 gap rules.
* `ship_steering.cpp`: `decide()` enters a committed capital first and skips the join modes for a member; the controller's point
  and speed replace the leaf point and site B's speed (`kShipFlagGate`, both directions); `sb_frame_all` before the decisions; P2,
  the site-W handler, the small-ship rewrite, the rows and the counters.
* `ship_avoidance_core.h`: `SiteE.small_gap_ms` (Run161 item 5; 0 = amendment 17).
* `ship_steering_sites.h` (h): the P2 contract; `verify_ship_steering_sites.py` gains `p2_remainder_contract` (20 checks).

**Q and the final lines (D.2.2).** `q_point`: the point nearest the grid lateral A_lat in {|Q| ≤ ℓ_max, Q_U ≤ −ℓ_min},
ℓ_max = r_s + (v_max / 2) / 10 − 100 m (the trigger at the aligned ramp's floor), ℓ_min = w + (L − w) sin 3° − (0.389 r_gate −
140 − 50 m). A grid point on the axis column is biased toward +R by half the disc's chord at Q_U, within the chord limit (D.5:
small ships take −R). Λ4 (order 4) = A_lat + (Q − A_lat) clamped to 0.75 r_gate − 150 m; Λ5 (order 5) passes through Q along F.
Census Titan: the axis column gives Q = Λ4 = (463, −370) m; slot 19 gives |Λ4 − A| = 679 m, with the rest flown as the order-5
lateral S.

**The axis variant, measured and rejected (2026-10-13).** `q_axis` puts Q on the axis (ℓ = 0) and moves Λ4 toward it within the
chord limit. It is kept behind the same selection for a later experiment, off by default (`kQAxisDefault`). The default is the offset
Q above.

Measured in scenario T (departures, 9 runs) against the offset Q:

| | Cross→launch per ship (min / mean / max) | First crossing to last launch |
| --- | --- | --- |
| Axis | 27.5 / 34.8–35.3 / 41.5 s | 779–794 s |
| Offset Q | 30.1 / 37.4–38.1 / 43.8 s | 700–714 s |

* The axis saves about 2.6 s per ship. The order-5 leg runs 4.4 km from X to the trigger at no more than 110 m/s, and the axis moves
  the trigger only about 170 m.
* It costs about 80 s of span: convoy B's leader lost its backfill ahead of convoy A's waiting escorts.
* Run160b's 15.3–15.5 s cross→launch is taken to be game time under SETA **[i]**.

**Petals (D.2.4).** The ring has 7 petals 40° apart on the lower 240° (the design had 9 at 30°). The reason: the sweep certifies
only with its 30 s margin (50 m + k·30 s), so adjacent petals need 2 w_max + 800 m between their planes at the ring radius.

* ρ = (2 w_max + 800) / (2 sin 20°), at least 2.4 km. It is 2,874 m for the census and 3,085 m at w 0.6 r, and it only grows while
  a petal is held.
* The front turn's foremost point is X − 2.5 km (L_lead at its minimum, so an escort's release window stays inside the script's
  10 km: 9.2 km measured for the Titan). R_h = 1 km, S_h = 1.5 km, and a lap is 9,283 m at 25–60 m/s.
* Rings 1–2 lie D_ring = 6.8 km further back.
* A holder takes the free ring-0 petal nearest its bearing; a petal next to a held one costs 0.35 rad. An escort aims two petals
  from its leader's.
* The leaf is the predictor's carrot on the racetrack, max(0.75 v, 2 v / W, 0.3 r_gate) beyond the nearest point.
* The lap speed brings the next holder to the middle of its release window one nominal funnel (50 m/s) before the funnel frees.
  An escort is brought to s = 300 m of its inner straight (beyond 10 km) at its leader's predicted launch + 4.5 s; others fly 40 m/s.
* A committed holder closing on its grid sphere within 0.75 r + 300 m + v²/2a + 200 m is written 10 m/s (the guard; order 4 never
  ended outside X in the fixture).

**The sequencer and the funnel (D.2.3, D.2.5).**

* Keys: seq << 4. An escort's key is its leader's + 1 + its rank, reserved once. Committed members that entered within 2 s of
  each other without a reservation are ordered by their distance to X (D.8: a load re-enters the board in one burst).
* Release of the first in sequence when all of these hold:
  * it is behind its petal's straights with the nose within 60° of F;
  * the funnel's order-4 part is free: the last released ship is within 2.5 km along and 150 m laterally of its X, or past P2;
  * t_free − now ≤ d_X / 25 m/s;
  * the join predictor on Λ4 (candidates 100/60/40/25 m/s) finds a candidate: the highest at grade 2, else at grade 1.
* t_free = the predecessor's predicted end of order 8 (its predicted path time to X, 1 s hand-over, the order-5 leg at the aligned
  ramp, 0.9 s) + 2 s, never before a blocked slot (a capital launch at the gate + 61 s).
* Backfill: while the first in sequence is an escort still waiting for its order 4, the first committed holder goes ahead if its
  predicted launch + 2 s comes before the escort's earliest X.
* Funnel speed: min(d_X / (t_free − now), the feasible candidate), at least 10 m/s. The feasibility is re-predicted once per
  second.
* Go-around (back to a petal): no feasible candidate, or the slot frees more than 10 s later than 10 m/s can absorb.
* The plan at the release is X = max(t_free, the predicted path time) and launch = X + 1 s + the order-5 leg. A capital launch at
  the gate after the release shifts it (D.7 item 4's "+60 s after a direction change").
* The capital order-5 leg flies Λ5's carrot. Its speed is the handler's aligned ramp (v_max at r_gate, v_max / 2 at r_gate / 5),
  written both ways on every off-axis Q (D.12).
* P2's keep verdict takes the funnel in place of the join mode. A kept stage-B hand-over zeroes `obj+0x30/34/38` (D.12; the stub
  bytes are unchanged).
* A rollout that ends by leaving (order 8, `line5` at its trigger) certifies with one paired sample: the ship is gone afterwards.

**Escorts (D.3 with D.12).**

* Petals on 0x14.
* At the leader's order 8 the reservation runs 60 s (the design had 30 s for an order 4 at 4.2–4.5 s; beyond 10 km the script
  first flies 5 s 0x15 legs).
* On 0x15 beyond 10 km the escort pursues its inner straight toward the window, so the order 4 at the script's next pass inside
  10 km starts on the straight with the nose along F.
* An escort on 0x15 to the gate or on order 0 keeps its role while the reservation holds.

**D.5.** While a member of the gate is released and not yet launched, a small ship's order-5 `f+0x240` that is the engine's arc
point (radius 0.55 r or 0.05 r, height −0.01 r, lower ±45°) on the capital's side is mirrored across U, so θ becomes −θ and the
radius, height and spread are kept. The rewrite comes from site A on the decision that finds it, and the leaf steers at the new B
from that call. `f+0x230` is not written. A field still holding our last write is left alone; a KC re-issue is rewritten. With no
capital in the funnel nothing is written (`small_b_rewrites` counts).

**D.6.** For a committed member the join state machine (`join_on_decision`, the slow join, the aim-at-A latch and its rows) does not
run. The lane verdicts and certificates and the (b) follow cap were already off under the key. Site W's handler returns at once
while the controller is live, counted with `gate_warp_skipped`; the launch window places arrivals. A ship the board refuses keeps
amendment 17's path.

**Run161 additions (in scope 2026-10-13).**

1. *Membership through the order-0 gap.* An escort stays while its formation memory is current, whatever the live order, and on
   order 0 within 5 s of its last 0x14 / 0x15 read. A committed member stays on order 0 within 5 s of its last order-4 / 5 read
   (`f+0x224` = 0 or its gate), through the P2 hand-over with the target zeroed, and on order 5 right after the hand-over before its
   first leaf call commits. Eviction is on 0x12 only.
2. *The order-0 rollout rule.* In the P2 hand-over a committed member brakes along its nose (order 0 sets the desired speed 0;
   0x1f holds). In the script's re-issue gap (≤ 5 s after its last steered read) a member continues its last steered plan at its
   current speed: an escort its petal, else following its leader; a committed ship its petal or its funnel line. Otherwise it brakes.
3. *E's pass reasons* (`gc::EWhy`): none, unresolved, stale, starved, on_0x12, rows, order0, not_member. They are counted
   (`e_pass_ctl_why=` in the summary, in that order) and logged as `ship_gate_ctl_pair kind=e_pass why=` at most once per pair per
   second.
   * The 13,700 uncertified frames reproduce: two census Titans parked at grid points 666 m apart have a capsule gap of −500 m and
     are never certified (`sb parked neighbours`).
   * Stage B parks nobody. Asserted in scenario T: the closest two petal holders keep a capsule gap of 302 m (> 50 m), and at most
     one committed member is past its X depth at a time.
4. *Trace.*
   * A capital takes any free trace slot.
   * An escort of any traced non-escort capital is claimed.
   * `ship_trace` carries `cap_ms` / `cap_age_ms`, and `ship_trace_rows.py` parses them.
   * Under `--debug` every order-0 frame of a member writes `ship_gate_ctl_pair kind=order0` with each partner's E reason
     (partners within 5 km).
5. *Small ships.* Under the key the gate row (`suppress_gate_small`) also covers order 0, with `f+0x224` zeroed, while the order-4
   decision or the lead-in is at most 5 s old. It is counted as `e_gate_small_gap`. The M6 escorts evading each other near the gate
   are not addressed (reported in Run161, left open).

**Rows and counters.**

* `ship_gate_ctl` gains `key=`, `stage=<from>><to>` and `petal=`; the stage transitions are rows with why = hold / release /
  go_around / dock / launch.
* `ship_gate_ctl_pair` gains `kind=` and `why=`.
* New counters: `ctl_releases`, `ctl_go_around`, `ctl_x_refused` (with `ctl_x_wait_s`), `ctl_sb_steered`, `ctl_sb_pred`,
  `ctl_guarded`, `ctl_p2_zeroed`, `small_b_rewrites`, `ctl_sb_us_max`, `e_pass_ctl_why`, `e_gate_small_gap`.
* The summary's worst case is 4,007 of 4,096 characters (`summary_bound.py`).

**Scenario T, stage B** (9 runs per variant, the key on; producer
[`scenario_t_b.py`](../../verification/results/ship-steering/gate-traffic-controller/scenario_t_b.py) →
[`.txt`](../../verification/results/ship-steering/gate-traffic-controller/scenario_t_b.txt), measured).

Fixture changes:

* The escort script follows D.12: the first pass 4.2–4.5 s after the leader's launch, then 5 s 0x15 legs while beyond 10 km.
  Convoy A's 100.5 / 187.3 s delays now come from that rule.
* The 0x14 legs last 30–34 s with 2–3 s order-0 waits.
* After P2 there is a 2–3 s order-0 re-issue gap.
* The formation memory is taken as lapsed, so only the gap rule keeps an escort.
* A departure enters at its Run160b start once no live capital is within 1.2 (r_A + r_B) + 200 m of it or its escorts' slots,
  because the holders are no longer where Run160b had them.
* The run lasts 1,200 s.

Hard checks, all pass. The oracle is Run160b; values are the worst over the 9 runs.

| Check | departures (w census) | dep w 0.6 r | mixed | oracle |
| --- | --- | --- | --- | --- |
| crossings at X, nose ≤ 8° (all kept) | 5.05° | 4.02° | 4.83° | 79–105° |
| 0x1f frames | 1 | 1 | 1 | rotates 84 s |
| 0x12 episodes between members | 0 | 0 | 0 | 195 s chain |
| member warns unsuppressed in the order-0 gap | 0 of 30,032 | 0 of 18,029 | 0 of 24,434 | Run161: all 104 |
| capsule contact; closest gap | 0; 302 m | 0; 302 m | 0; 573 m | |
| launch after its sequence time (≤ 30 s, +60 after a direction change) | 5.1 s | 5.3 s | 10.1 s | |
| cross→launch per ship (reported) | 30.1–43.8 s | 30.0–43.0 s | 37.3–41.8 s | 15.3–15.5 s (SETA [i]) |
| `ctl_x_refused` | 0 | 0 | 0 | |
| escorts' order 4 from a petal | 36 / 36 | 36 / 36 | 18 / 18 | 0x15 92–145 s |
| order 4 ending outside X; frames < 25 m/s on a petal; < 10 m/s on order 4 | 0; 0; 0 | 0; 0; 0 | 0; 0; 0 | |
| every departure launched | 7 / 7 | 7 / 7 | 4 / 4 | |

The arrival variants hold no member pair in range, as in stage A. A small arrival's spawn against a capital at the plane keeps a
surface clearance of at least 554 m (census) and 514 m (w 0.6 r).

These are the values with the offset Q (the default) before the review fixes; the post-fix values are under "Review fixes" below and in the ledger.

Reported, not met:

* **D.7 item 7, the span.** First crossing to last launch is 714 s for 7 departures (719 s at w 0.6 r) before the review fixes, and
  564.7 s (578.9 s at w 0.6 r) after them, against the 200 s target, Run160b's 484 s for 6, and 565 s for 7 scaled. The axis variant
  gave 794 s. The gain came with the fixes (the busy rule, the caps, the gap braking) and was not tuned for. The cadence is about 120 s per ship: the funnel from the petal takes about 75 s at the plant's cornering cap,
  the order-5 leg about 38 s, and the escorts follow serially because their order 4 cannot come before their leader's launch (D.12).
* **D.7's literal t_seq.** Against t_launch(k−1) + 18 s the worst launch is 152 s late. The 18 s assumed Run160b's 15.3–15.5 s
  X→launch, which the fixture's tf = 1 legs do not reach (about 38 s). Item 4 is therefore asserted against the sequencer's plan.

**Cost** (host x86-64 -O2, measured; i686 not measured):

* `sb_frame` on a full board is 15.7 µs per frame with a funnel feasibility prediction every frame (one prediction of 4 candidates:
  14.2 µs);
* `sb_steer` is 20 ns per decision;
* a decision on a full board with 21 petal plans is 7.2 µs best and 44.3 µs worst (stage A: 30.1 µs in conflict);
* the frame bound and the predictor are unchanged: 16.2 / 19.3 µs (stage A 16.1–16.2 / 18.8–19.5).
* The handler hot path adds one `ctl_lookup` (the bitmap, two loads for a non-member) and `sb_steer` for a capital member. Site B adds
  only `--debug` rows; E adds `e_reason` on a pass.
* Static memory: Member 1,792 B, Gate 51,852 B, board 207,416 B (stage A 188,520 B); the controller is 209,984 B
  ([`static_sizes_b.txt`](../../verification/results/ship-steering/gate-traffic-controller/static_sizes_b.txt)).

**Key off** (`ship_steering_gates_traffic = 0`).

* [`keyoff_identity_b.sh`](../../verification/results/ship-steering/gate-traffic-controller/keyoff_identity_b.sh) →
  [`.txt`](../../verification/results/ship-steering/gate-traffic-controller/keyoff_identity_b.txt) against main `ad0e131a`: E 0 /
  400,000; resolve 0 / 60,000 (floats bitwise); steer set 0 / 65,536; site B clamp 0 / 200,000; ship-container flags 0 / 100,000.
* [`fixture_identity_b.sh`](../../verification/results/ship-steering/gate-traffic-controller/fixture_identity_b.sh) →
  [`fixture_lines_b.txt`](../../verification/results/ship-steering/gate-traffic-controller/fixture_lines_b.txt): every line outside
  scenario T and the stage-B unit and cost lines is unchanged in all three modes (326 / 149 / 326 lines, 0 differing).

**Not done, or deviating from the ratified text.**

* The petals are 7 at 40°, not 9 at 30°.
* The reservation lasts 60 s, not 30 s.
* D.5's order-4 small lead-in on the free side is not built; only the order-5 `f+0x240` rewrite is.
* D.4's "funnel closed while an arrival's capsule is in the corridor" is not a rule of its own. The slot block, the sweep's yields
  and the shifted plan cover it, and the mixed variant has 0 episodes.
* `ctl_x_refused` in the DLL counts a hand-over waiting more than 5 s, not a KC refusal as such.
* The small-ship M6 escort pairs (Run161, 164 s) are reported only.

**Review fixes (two reviews of `5c4e85d4`, Opus and Fable, 2026-10-13).**

1. *Tie-break.* A head of the sequence is skipped when, with the funnel free and its slot due, it either stays in its release window
   with no feasible candidate for more than 60 s, or its window never opens within a slowest lap (371 s) plus 60 s. It then re-queues
   at the tail (`ctl_sb_misc` [0]). Unit: a never-feasible head yields at 60.3 s and the other two are released.
2. *Funnel busy.* No release while any committed member waits at X (the hand-over) or is on order 5 or within order 8, besides the
   final-straight rule.
   * The `dep refused` variant adds a 45 s refusal of the first departure at X: 6–11 refused polls; the follower held (no release while
     a ship ahead was busy); 0 unresolved pairs from the refusal to its launch; 0 go-arounds within 1 km of X.
   * In that variant the escorts' order 4 from a petal (2 of 4) and the plan lateness (≤ 58 s) are reported, not asserted.
3. `sb_steer` checks the live target first: order 4 / 5 to the gate; 0x14 to the member's leader; 0x15 to the leader, or to the gate
   once the reservation runs.
4. Every stage-B speed write is min(the controller's speed, `gate_speed(v_max, large, v_corner)`): the cornering cap and the 100 m/s
   large cap.
5. `sb_managed`: stage B owns a committed member only with Q known and a petal held or a released stage. Otherwise the member keeps the
   stage-A / amendment-17 join (join on, P2 kept when aligned).
6. Terran rings: `q_point` keeps ℓ + w ≤ 0.663 r_gate − 50 m and falls back to the axis when no ℓ fits. Unit: the M2 2,998 m row gives
   |Q| = 535 m (2,034 ≤ 2,077 m); the OWP 4,583 m row goes to the axis.
7. The player's ship (`[obj+0x40] & 2`) is never entered, sequenced, steered or given a B rewrite.
8. A backward game-clock jump (a load) resets the gate's slot state (`ctl_sb_misc` [2]). A gate entry re-created by `acquire_gate`
   starts from zero. A new hand-over clears `x_ms`, `refused` and `o8_ms`; a new order 4 clears them too.
9. 0x12 evicts any member, whatever its formation memory (`validity` returns `evict`).
   * The launch and `line5` rollouts no longer end at the launch. They hold the launch's end point beyond the plane for the rest of the
     horizon, so a pair's overlap is never short, and the sweep keeps its 4-sample and warn-reach minimums (F13).
10. The small-ship B decision is `gc::small_b_decide`, which the DLL calls and the fixture unit-checks.
    * No rewrite within 0.55 r_gate of B, on a detour frame, or for the player.
    * Only the funnel members that reach the plane before the small ship's ETA count.
    * B stays when it is clear of them (w + r_small + 50 m), is mirrored when only the mirror is clear, and is left alone when both
      sides are taken (`ctl_sb_misc` [3]).
    * `small_b_ours`: the DLL's last write is left alone; a KC re-issue is evaluated again. A go-around (no funnel member) ends it.
11. The `kind=order0` pair rows are limited to one per pair per second. The trace-slot change (any slot for a capital) applies under the
    traffic key only; with the key off amendment 17's rule stands.
12. Verifier `p2_remainder_contract`: order 0's `jmp +0x133` resolves to the pinned tail 0x004590e0; 0x1f's aligned test, its
    no-roll-target branch and the roll-only zeroing are pinned (20 checks).

*Opus question 5, the order-0 gap against the margin* **[i]** for the arithmetic, measured for the fixture.

| Case | Gap | Braking a | Error vs the 50 m + 10 m/s·t margin |
| --- | --- | --- | --- |
| Titan, rollout continuing at its speed | 5 s | 10 m/s² | ½ a T² = 125 m at 5 s (margin 100 m); a T² ≈ 250 m by about 10 s (margin 150 m): exceeded |
| M1 Colossus | 5 s | 5 m/s² | 62.5 m at 5 s; about 125 m by 10 s: within |

* Raising k to 25 m/s for gap pairs was tried and rejected. The certificates failed: 84–2,591 unsuppressed gap warns per departure run.
* Instead the rollout brakes for the gap's expected rest (`gap_brake_s`: 3 s for an escort, from the template's 2–3 s wait; 5 s for a
  committed ship), then resumes the last plan at its speed. The residual is the gap-length mismatch, at most 2 s, about 40–60 m.
* Measured with the 2–3 s waits and the 2–3 s re-issue gap: 0 of 149,928 member warns in the gap unsuppressed across the four
  departure variants. k stays 10 m/s.

**Open: span tuning (after Run162, from flight data).** The span is set by three things:

* the funnel from the petal to X: 60–125 s from release to P2, at the plant's cornering cap from ρ ≈ 2.9 km and X − 3.5 km;
* the order-5 leg: 30–44 s;
* the escorts going one after another under D.12.

The knobs are the petal radius and spacing, the release lead (L_lead, the release window, the final-straight overlap of two funnels),
and pipelining the escorts. Tune them against flown cadences, not the fixture alone.

### B.2 Run162 fixes

Built 2026-10-13 (Opus 5.5) on main `5b3b11ed`, with two review rounds folded in (B1, N1–N8; F1, F2); not flown, not installed.
Triage and witnesses: [`run162-triage/`](../../verification/results/ship-steering/run162-triage/) (`sb_timeline.txt`, `sb_reasons.txt`,
`sb_gate_e.txt`, `sb_laps.txt`); the reviews' witnesses
[`review_b2_escort_head_witness.cpp`](../../verification/results/ship-steering/gate-traffic-controller/review_b2_escort_head_witness.cpp)
→ [`.txt`](../../verification/results/ship-steering/gate-traffic-controller/review_b2_escort_head_witness.txt) and
[`review_b2_sibling_skip_witness.cpp`](../../verification/results/ship-steering/gate-traffic-controller/review_b2_sibling_skip_witness.cpp)
→ [`.txt`](../../verification/results/ship-steering/gate-traffic-controller/review_b2_sibling_skip_witness.txt) (each on the state it was
taken on; both are fixture units now).
Ledger: [ship-steering.md](../verification/ship-steering.md), "B.2 Run162 fixes". Sources: `ship_gate_ctl_core.h` (the order, the
sequencer, the stage `inbound`, `inbound_carrot`, `ring_extent`, `past_last_stretch`, `occupant`, `sb_gate_row` / `e_gate_row`,
`SbSeq`), `ship_avoidance_core.h` (`SiteE.ctl_gate` and its clause), `ship_steering.cpp` (E's fields, the `ship_gate_ctl_seq` row, the
interval maximum).

**What Run162 measured** (gate `bdb0`, `sb_reasons.txt`). The M2 leader committed 47.5 km out was head-skipped after 60 s: its window had
no distance bound while the predictor's horizon is 256 s, so it was infeasible by distance alone. The skip re-keyed it 16 → 32 behind
its reserved escorts (17, 18), which cannot be released before their leader: 2,524 game s of waits until a 0x12 broke it. The holders
cruised at the petal speed of 40 m/s from 47 km. Three of four funnel releases were evicted at 6.6–6.9 km by the engine's 0x12
against their own gate (E: `pass_not_resolved`; the gate clause covered only the join / slow-join / fallback approaches). A re-commit at
X (z −4.8 km) put a ship back on hold into a U-turn.

**1. The sequence and the head skip.**

* The order is a strict total order (review B1; the B.1 comparator mixed the burst's distance order with the key order and was not
  transitive). Committed members without a reservation that entered within 2 s of each other, chained, form a burst; `burst_keys` gives
  each the burst's lowest key. Each member then has the tuple (group, distance, key, ship, sub, own ship), compared lexicographically:
  * a committed member: group = its burst key or its key; distance = its entry distance inside a burst (a NaN ranks last), else 0; sub 0;
  * a reserved member (an escort, or one already committed on its own order 4) whose leader is on the board: its leader's tuple with
    sub = 1 + its offset behind the leader's key, so it is never reordered relative to its leader or its sibling escorts (the leader is
    found by `leader`, or by the key class `key & ~15` once `commit_fill` has cleared it);
  * any other member: its own key as the group.
* An escort is never a release candidate while its committed leader holds, is inbound or is not yet staged, whatever the keys (review
  B1). The review's witness (slots L, N, E; L 16, N 32, E 17; L and N one burst, N nearer X) now releases N first.
* The skip timers run only once the head has reached its petal ring (`ringed`: within the petal's extent from G) and only while the
  funnel is free.
* A due skip applies only when another committed non-escort holder (hold, order 4, Q known, within the horizon, outside the head's key
  class) is feasible: its funnel prediction at most 1 s old, at most one new prediction per frame while the skip waits. A sibling escort
  already committed on its own order 4 is the head's convoy, not an alternative (review F1).
* The leader and its convoy move together: the leader takes the next multiple of 16 past the highest key, and every reserved member of
  its key class (its escorts and a sibling already committed on its own order 4, review F1) the leader's key + 1 + its rank. The skip
  keeps `enter_ms` (so it creates no burst) and takes the member out of its entry burst (`unburst`). The skip is a `ship_gate_ctl` row
  with `why=skip`.

**2. Arrival to the ring (`inbound`).**

* A committed member or a capital escort farther than its petal's extent + 2 km from G is `inbound` (back to inbound from hold only
  beyond + 3 km). The extent is the distance of the petal's farthest point, |back-turn centre| + R_h: 11.9 km for the census Titan's
  ring-0 petal, so `inbound` begins at 13.9 km.
* It keeps its petal (assigned as for a holder) and flies toward the petal's inner line (+F at the ring radius). Farther than 1 km from
  that line it aims at the line's point R_h + 1.5 km behind the petal's back turn; on the line it aims at the inner straight's back end,
  then along the straight. A holder that has not reached its ring yet keeps the same carrot.
* The rollout uses the same in-plane carrot as `sb_steer`; the out-of-plane offset decays with the in-plane distance flown toward it
  (review N4), so the sweep predicts the flown path. This is an approximation: each 0.25 s step shrinks the offset by the step's travel
  over the 3-D distance to that step's carrot, i.e. a straight line to a carrot held fixed for the step; the plant's turn, the moving
  carrot and the slip are not modelled out of plane, and the nose is turned only in the petal's plane. The plan stays `petal`, so the
  yield floor stays 25 m/s.
* Speed: min(v_max, √(60² + 2 a (d − boundary))) m/s, so it arrives at the boundary at the petals' top speed; site B applies the
  cornering cap and the 100 m/s large cap. A 110 m/s Titan cruises at 100 m/s: 47.5 km in about 7 min instead of 20 min at 40 m/s **[i]**.
* It is no release candidate and runs no skip timer. The release window adds dX ≤ v_max · 256 s, the predictor's horizon.
* **Crossing (review N5, corrected).** The point behind the back turn keeps an inbound ship clear of its own ring's other petals only
  when it comes from behind (beyond that point along −F). Coming from the side or from ahead of the ring, it flies laterally toward its
  line at its own depth and can cross other petals of its ring. From any direction its path can cross the deeper rings (1, 2: 6.8 km and
  13.6 km further back) and the space of another holder still entering; nothing geometric keeps it out. Only the pair sweep's yield cap protects those crossings. Scenario T asserts the closest capsule gap of
  an inbound member and a holder ≥ 100 m: worst 132 m (w 0.6 r, 144 fps), 145 m census at 144 fps, 180–237 m at 30 / 60 fps, 544 m in
  `run162` (measured).
* Applying it to escorts is an assumption beyond the brief's "committed ship": an escort held at 40 m/s from 47 km would arrive about
  13 min after its leader, past its 60 s reservation **[i]**.

**3. Site E's gate row for a stage-B member** (`gc::e_gate_row` → `gc::sb_gate_row`, `SiteE.ctl_gate`, checked right after the
arrival-gate row, so it needs no steering decision of A). The DLL's `ctl_e_fields` and the fixture's own-gate model call the same function
(review N8). The verdict is `suppress_gate` when all of these hold:

* the traffic key is live (E reads the controller only then);
* A is in the membership bitmap, on the board of gate B (`locate`), with the same object id;
* A is committed with Q known and live (its state read ≤ 1 s old), in stage funnel, handover, dock or launch;
* A's `f+0x224` is B, or it is 0 on order 0 or 0x1f (the script's re-issue gap, the P2 hand-over), whatever the order (4, 0, 0x1f, 5);
* the position read succeeded, is finite and lies within (v_max + 50 m/s) · (the state's age) + 500 m of the member's last state read;
  the hull radius is the member's own and positive (review N6: fail closed otherwise);
* dist(G, segment[x_A, P]) ≥ r_gate + r_A + 0.25 r_gate, with P = Λ4's point at X's depth (the funnel's point; the capital rows' guard).

Everything else keeps today's rule (a holder, another target, gate or id, order 5 with the target zeroed, the guard failing near the
plane). E's counter is `e_suppressed_gate`. With the key off the field is never set (key-off identity below).

**4. No re-hold past the funnel's last stretch.**

* A member that was released and re-enters order 4 past the last stretch goes back to the funnel (`why=rejoin`, a new plan from its
  speed), never to hold. Past the last stretch means within 1.2 r_gate of Λ4 laterally and either within 1.2 r_gate of Λ4's point at X's
  depth or beyond it along F (review N1).
* `enter()` clears `release_ms` when the eviction lapsed (more than 30 s), so only a re-commit within the 30 s window rejoins (review N1).
* The rejoin checks the funnel as a release would (review N2): with another member occupying it (`occupant`) or the slot blocked, the
  ship holds (its petal by its bearing), so two ships never converge on X.
* A funnel member past the last stretch never goes around (it keeps its shaped speed). A first commit there still holds (scenario T's
  convoy A leader starts 1 km before X).
* The funnel-busy test counts only members that are live and decided within 5 s (review N3: a dead, deleted or departed member no longer
  blocks it), and every committed funnel member that is not on its final straight, not only the last released one.
* The funnel's occupants (committed in funnel, hand-over, order 5, order 8) are decided first in each frame, before the round robin
  (`gc::decide_order`, used by the DLL's `ctl_frame` and the fixture; review F2). The round robin of a full board under SETA (96 members at
  18 decisions per game second) takes over 5 s, so their stamps could otherwise lapse and free the funnel under a ship that holds it.

**5. Diagnostics.**

* `ship_gate_ctl_seq` (`--debug`, ring 8, drops counted as `ctl`): one row per gate per game second while any member holds or is
  inbound, with `holders`, `inbound`, the candidate (`cand`, `key`, `role`, `stage`, `ringed`), `busy_by` (the member keeping the
  funnel busy), `window` / `timing` (`-` when not evaluated), `free_ms` (t_free − now), `dx_m`, `v_feas`, `block_ms` (the slot block's
  rest), the gate's `skips` and `releases`. `SbSeq` is filled only when the caller asks (`SbStats.seq_on`: the DLL's debug tier; review N7).
* `ctl_sb_us_max` is the summary interval's maximum (reset after each summary row); the other maxima stay session-lifetime.

**Scenario T** (producer [`scenario_t_b2.py`](../../verification/results/ship-steering/gate-traffic-controller/scenario_t_b2.py) →
[`.txt`](../../verification/results/ship-steering/gate-traffic-controller/scenario_t_b2.txt); 9 runs per variant, measured).

The `run162` variant: a Titan leader with an M1 and a Titan escort committed 47.5 km out; an M1 with two M7s committed 90 km out 30 s
later; the order-4 legs get the script's order-0 gaps (2–3 s every 30–34 s, `f+0x224` zeroed); 2,400 s. Every variant models the
engine's warn of a stage-B member against its own gate (`gate_warn_cone`) on order 4 and on order 0 / 0x1f before order 5, with E's
verdict through `gc::e_gate_row`.

| Check (worst of 9 runs) | run162 |
| --- | --- |
| launched (no deadlock) | 6 / 6, last launch ≤ 1,318.3 s |
| head skips; skips short of the ring | 0; 0 |
| frames a leader was keyed behind its escort | 0 |
| inbound cruise, 110 m/s class (steered, ≥ 10 s after an order change, outside a yield cap) | min 100.0 m/s, 0 frames < 99.5 |
| own-gate warns: funnel on order 4 / gap and hand-over; left to the engine | 67,707 / 14,602; 0 |
| re-holds past 1.2 r_gate of X | 0 |
| first ship commit → launch vs transit at v_max + 120 s | 486.6–496.1 s ≤ 551.9 s (431.9 + 120) |
| inbound vs holder, closest capsule gap | 544 m |
| first crossing to last launch | 872.2 s for 6 |

The yield cap between the two M7 escorts of the second convoy (≤ 6,916 frames per run, excluded from the cruise figure) is the pair's
separation, not the inbound speed. Under the old clause every one of the gap and hand-over warns would have passed E **[i]**: the clause
did not cover a stage-B member.

The new units:

* the first review's witness (both burst orders: the nearer releases first, the escort never heads);
* the second review's sibling witness: the skip moves the committed sibling with its leader (keys 64, 65), and the other holder releases
  first;
* 200 randomized boards (slot orders, bursts, entry distances, roles, a never-feasible head, releases that launch and leave, escorts
  committing on their own order 4 while their leader holds or after its launch): 1,019 releases, 70 skips, 318 siblings committed while
  their leader held; 0 frames an escort headed and 0 siblings released while their leader held; 0 stalls; the longest wait with the
  funnel free and a feasible holder 61.3 s (the skip's 60 s plus a prediction; bound 62 s);
* the funnel's occupancy under SETA: a funnel ship among 90 members at 18 decisions per game second keeps a decision at most one frame
  old and stays the occupant on every frame (the plain round robin: up to 7.0 s, 38 frames lapsed);
* the convoy skip; no skip short of the ring; the re-commit at X (a first commit holds, a re-commit keeps the funnel, beyond 1.2 r_gate
  laterally or with the funnel occupied it holds); E's gate row with its fail-closed cases.

Existing variants (all their checks pass; the B.2 checks added to each): span 564.8 / 578.2 / 429.2 s (B.1 564.7 / 578.9 / 429.2), nose
5.04 / 4.74 / 5.39°. The closest capsule gap fell from 302 m to 145 m (w 0.6 r: 132 m): the second convoy's two M7s spawn 302 m apart;
the rear one, beyond the boundary, flies inbound at 100 m/s and closes on the front one until the yield cap holds it. The closest two
petal holders: 312 / 201 / 674 m (B.1 302 / 302 / 674 m).

**Cost** (host x86-64 -O2, measured; i686 not measured): `sb_frame` on a full board 16.4 µs per frame with a feasibility prediction
every frame (B.1: 15.7 µs; `burst_keys` is a 24 × 24 pass); `sb_steer` 20 ns; one funnel feasibility 14.2 µs; a decision with 21 petal
plans 7.7–45.0 µs (B.1: 7.2–44.3 µs); `decide_order` two passes over 96 slots per frame **[i]**; site E's stage-B gate row through `e_gate_row` 11 ns per call (the DLL adds four game-memory
reads, only for a gate warn of a ship in the bitmap). A pending skip adds at most one funnel prediction per frame (14.2 µs) **[i]**;
`inbound` adds one `ring_extent` (a sincos and two square roots) per holder per frame **[i]**. Static: Member 1,808 B (+8), Gate
54,556 B (+200), board 218,232 B (+800), `SbStats` 280 B on the stack (host sizes; the fields are 4-byte types, so i686 matches **[i]**).

**Key off.** `SiteE.ctl_gate` is set only inside `ctl_e_fields`, which runs only while the controller is live; the order, the new stage,
the skip, the rejoin and the row live in the controller. Key-off identity against `5b3b11ed`: 0 differences (E 0 / 400,000, resolve
0 / 60,000, steer set 0 / 65,536, site B 0 / 200,000, ship containers 0 / 100,000); fixture lines outside scenario T: 0 differing in all
three modes (ledger).

### B.3 Run163 fixes

Built 2026-10-13 (Opus 5.5) on main `f2c01be2`, with the orchestrator's decisions of the same day folded in (the 1 ms ceiling, the
frame-step reader, the own-gate-only warn, one pass at the SETA TARGET rows, the escort bound); not reviewed, not flown, not installed.
Triage:
[`run163-triage/`](../../verification/results/ship-steering/run163-triage/) (`sb_wait.txt`, `sb_gate_e.txt`, `game_steps.txt`,
`ctl_rows.txt`, `sb_seq_f4f0.txt`). Ledger: [ship-steering.md](../verification/ship-steering.md), "B.3 Run163 fixes". Sources:
`ship_gate_ctl_core.h` (`age_bound`, `note_step`, `live` and every freshness test, `sweep_impl` / `sweep_seta`, `sb_gate_why`,
`note_warn` / `keep_through_avoid`, the rejoin in `sb_frame`, `res_o8_ms` and the refreshed `res_until`, `frame_budget`),
`ship_steering.cpp` (`frame_step_ms`, `ctl_note_warn`, the 0x12 branch of `ctl_observe`, `ctl_frame`'s budget and ceiling, the E
row's tail, the summary counters).

**What Run163 measured** (gate `f4f0`, SETA on 98 % of the escorts' window). The funnel escort (M1 4510) was evicted twice by the
engine's 0x12 against its own gate: site E's gate row went from `suppress_gate` to `pass_not_fresh` on the two frames whose game step
was 2,000 and 1,540 ms (f 68942, f 74116), beyond the 1 s state age of `live()`. After each eviction it flew a 200 s petal lap while its
sibling (M2 4956) waited behind it in strict order (362 s as head with window 0; 205 s behind it). The reservation of 4956 lapsed at
+60 s (f 66931) while the script gave order 4 at +53 s and +81 s, so 4956 re-entered with a new key (64). `x_refused` rose after
launches (`launch>handover` at f 65820 from the stale `handover_ms`). The one rejoin went around 2 s after it started (item 3).

**1. SETA-aware bounds** (with review F5). Every liveness and freshness bound of the controller is
`age_bound(g, nominal) = nominal + last step`. The last step is the larger of this and the previous game-time frame's step, clamped to
5 s: a state read before a long step is two frames old on the frame after it. `note_step` takes the frame's step from `[root+0x714]`
(`frame_step_ms`, now the real step clamped to 5 s: item 8) at site B, site E and the frame end, once per game-time frame, and copies
it to the four gates. A state written this frame (age 0) is always live.

Soundness (review F5). A record within its nominal age before a step stays within the bound across it; one already stale before the
step stays stale (the bound grows by exactly the time that passed). The verdict class (nominal below 2 s: the state, the certificate,
the decisions, the rollouts' offset, the commit, the cap) is capped at 2 s. Nothing older than 2 s is accepted: a hitch beyond that
fails closed until the next read and decision. On a 2,000 ms step a state read on the frame before it is exactly at the cap, and one
read earlier is not. The windows of 2 s and more (the order-0 gap, the occupancy's decision) take the step on top. The first B.3
build's max(nominal, 2 x step, 4 x dt) let a pair stale before the step through and had no cap.

* Covered: the state age (`live`, 1 s), the certificate (250 ms), both members' decisions (1 s), the rollouts' time offset (1 s,
  `sweep_seta`), the exclusion (500 ms, through `pair_verdict`), the funnel occupancy's decision (5 s), the commit refresh (1 s), the
  order-0 gap (5 s, both roles), the yield cap at site B and the plan speed's cap age (1 s).
* Not covered: `gap_plan`'s 5 s (the rollout's model of the gap, not a gate), the 120 s hand-over, the 60 s arrival window and the
  30 s eviction memory (windows, not freshness).

* The constant-bound sweep stays the normal path: compiled for a runtime bound it was 57 ns slower per pair (73 → 130 ns broad-only,
  measured). `sweep_all` checks the exact bound itself and runs the sweep compiled for 1,250 ms while the bound is within it (steps up to
  250 ms); a longer step takes `sweep_seta`.

The own-gate row has its own liveness bound (re-review R1): 1 s + the last step + site B's 100 ms read interval, uncapped. The 2 s cap
stays on the certificates and the exclusion, and the read's reach check bounds the position. Unit: the funnel row keeps `suppress_gate`
on a 2,000 ms step's frame, on the frame after, and with a state read 2,100 ms before. It fails closed (`stale`) two 16 ms frames later
and at the same age under 16 ms steps. The certificate and the commit refresh follow. A certificate
300 ms old before a 1,000 ms step stays stale across it; one 200 ms old is kept. Ablation
([`seta_ablation_b3.sh`](../../verification/results/ship-steering/gate-traffic-controller/seta_ablation_b3.sh)): the SETA variant
below with the nominal bounds launches none of its 6 ships in any of its 9 runs. That run also overlapped capsules down to −1,148 m
(the reviewer's point). Failing closed under 2 s steps is not by itself safe; the scaled bounds are the protection. A flight row will
show whether any frame still fails closed: `ctl_budget_cut` and the own-gate `pass_not_fresh` count per interval, and the E row's
`gate_row=stale`.

**2. The own-gate clause and the 0x12 it should have suppressed.**

* `sb_gate_why` (the reason behind `sb_gate_row`) also covers a committed member in `hold` or `inbound` within 6 r_gate of its gate (a
  ship re-committed near the gate), on the same orders and under the same path guard. For the census Titan X lies 5.2 km out, so the
  clause reaches about 1.4 km behind X.
* At site B, on the frame after the KC's CollisionWarn ran on a member (the warn bits set, the order not yet 0x12), `ctl_note_warn`
  records whether the obstacle list held the member's own gate and nothing else, and the member's stage.
* A 0x12 that comes within the SETA-aware 1 s of a warn naming the own gate, while the member was in funnel or hand-over, is not an
  eviction (`keep_through_avoid`). The member keeps its role and funnel state through the episode (at most 60 s, then it is evicted),
  `ctl_evict_ignored_gate` counts the episode once, and the KC's order 0 after it keeps the member as the script's gap does (`keep_ms`,
  validity).
* A warn whose list names any other object, a holder, or an episode beyond 60 s evicts as before.

The engine still flies its 0x12; the board only keeps the funnel reserved and the sequence intact. On 0x12 the member's own-gate row
always passes (review F3: the suppression is never kept through), and the avoidance's exclusion of a certified partner requires neither
ship to be on 0x12 (review F4: A's live order, B's last read). When the episode ends, the commit stamp is refreshed, so the KC's order 4
after it does not expire the member (review F6). An eviction and a re-entry clear the episode (review F9). When order 4 returns, the funnel's
go-around rule decides as for any funnel ship. The decision asked for the warn's unsuppressed list to hold only the own gate. The row
carries E's per-entry verdicts only for traced ships, and whether the collision pass lists an obstacle E suppressed is not established
**[i]**. The rule therefore requires the whole list to be the own gate alone: if suppressed entries are listed, it is stricter than
asked (such a 0x12 evicts, as under B.2).

**Review F1: a reserved escort is a key, not a queue slot.** An escort that has not committed is never a release candidate and never
blocks a holder. Its reservation is its place in the sequence once its own order 4 commits it. B.2's waiting escort and its backfill are
removed. The backfill's earliest X came from the leader's fixed launch time and, once that was past, blocked every holder while the escort
loitered. With B.3's refreshed reservation that could have lasted indefinitely. The escort's lap target is taken from live time once
the leader's launch + the script's switch is past. Unit: a holder behind an uncommitted escort loitering on order 4 to a station
within 15 km (its reservation kept) is released at once (bound 62 s). The price (re-review R2, accepted): holders now pass a convoy whose
escorts have not committed, and the escorts then wait for those transits. In the wide-hull variant `dep w0.6r` the first crossing to
last launch grows from 578.2 s to 702.5 s, in `mixed` from 429.2 s to 437.7 s (measured).

**Review F2: release and rejoin are serialized within a frame.** A rejoin dates `decided_ms` 200 ms back (re-review R3). `occupant()`
sees it in the same `sb_frame`, so a later rejoin and this frame's release find the funnel taken. `due()` still lets its first real
decision run at once. Unit: a feasible head and a rejoiner from the release window
in one frame: exactly one in the funnel (the rejoiner; the head holds).

**3. The rejoin, and why Run163's failed.** At f 69421 the re-commit (after 26 s on 0x12 at 113 m/s) was admitted by B.2's
position-only test (`past_last_stretch`: within 1.2 r_gate of Λ4, or beyond X along F). The ship was 2,234 m in front of the gate plane
and 1,608 m off the axis, its nose 37° off F, drifting outward at about 65 m/s (trace f 69421–69455: 1,608 → 1,735 m). Its order-4 point
A lay on the axis 4,254 m out, so X was about 5.08 km out and the ship about 2.85 km past X and 2.0 km past A **[i]** (from the logged
order point and X = A − 0.75 r_gate F). From there P2 (within 0.75 r_gate of A) cannot come forward. The funnel's carrot is clamped at A
and turns the ship back, and the first prediction after the rejoin (B.2 reset `feas_ms`) had no feasible candidate. The go-around is
withheld only inside the last stretch; the lateral drift took the ship out of it, and the go-around fired 2 s (34 frames) later,
at f 69455. The slot-late branch did not apply: there was no ship ahead and no slot block **[i]**.

Fix: a rejoin (released before, within the 30 s window, the funnel free and no slot block, as in B.2) is admitted:

* past the last stretch only while P2's sphere is still ahead (along F not beyond A: `dot(x − X, F) < 0.75 r_gate`); a ship past A
  holds (its petal by its bearing), and the extended gate row covers it within 6 r_gate;
* from the release window (behind the petals' front, the nose within 60° of F, within the 256 s horizon) when the predictor finds a
  candidate now. It goes straight into the funnel, with no petal lap and no wait for its turn in the sequence (it was released before),
  and that prediction is kept as its feasibility (no reset).

Units: the Run163 geometry (2,850 m past X, 37° off F) holds; a rejoin from the release window behind another head goes straight into
the funnel (no petal); a first commit there holds while the head goes first; with the funnel occupied it holds.

**4. The escort reservation.** `kReserveMs` is now 300 s instead of 60 s.

* `res_until` is refreshed to now + 300 s while the escort is on 0x15 to the gate or within 15 km of it.
* Within `res_until` the escort keeps its reserved number on any order except a dock or launch leg to another target (docking
  elsewhere, leaving the sector); its own order 4 commits it with the key.
* The leader's launch time is kept apart (`res_o8_ms`) for the escort's lap target (live time once past; the backfill is gone: F1), which used to read
  `res_until − kReserveMs`.
* A ship that leaves the sector unseen stops its state and frees its slot after 60 s, as before.

Unit (the Run163 timeline): the leader launches and leaves the board; the escorts fly 5 s 0x15 legs and 3 s order-0 waits 12 km out.
Order 4 at +53 s keeps key 17 and at +81 s key 18 (under B.2 the reservation lapsed at +60 s). 20 km out on 0x14 the reservation holds
to its 300 s and lapses after; a dock leg elsewhere ends it.

**5. The hand-over's bookkeeping.** `handover_ms`, `x_ms` and `refused` are cleared when the stage reads order 5 or order 8, so an
order 0 after the launch is neither a hand-over nor a refusal. Order 5's validity then needs the commit refresh, which is now
SETA-aware; every order-5 decision of a capital commits (`ship_steering.cpp` `decide`). Unit: hand-over → 5 (cleared) → 8 → 0 stays
`launch`, with 0 refusals.

**6. Diagnostics.** The `ship_avoid_e_verdict` row of a member A against a gate carries:

* `gate_row=`: suppress, not_member, stale, unreadable, reach, stage, order or guard;
* `ctl_age_ms=`: A's member state age;
* `step_ms=`: the frame's game step (clamped to 5 s, as `frame_step_ms` reads it).

The tail is written only with the trace on (the extra `locate` is skipped otherwise). The summary gains `ctl_evict_ignored_gate=` and
`ctl_budget_cut=`.

**7. The frame budget follows game time** (user decision, 2026-10-13).

* `frame_budget`: decisions = max(6, ceil(members on all boards x 4 per game second x this frame's step)); rollouts in the same
  proportion (14 per 6). That gives 6 / 14 at normal play, 32 / 75 at SETA x10 and 30 fps with 24 members, and 192 / 448 on a 2 s step.
* The funnel's occupants are still decided first (`decide_order`), and each member at most once per frame (`due`).
* The DLL's `ctl_frame` stops at 1 ms of measured time (decision 2026-10-13: 3 % of a 33 ms SETA frame; one QPC read per decision);
  `ctl_budget_cut` counts the frames it cut.

Unit: a full board (24 Titans on petal plans) at SETA x10 and 30 fps, with 2 % of frames at 1.5–2 s. It asserts that no decision
record is older than 250 ms + one frame step at the frame end, both with the budget alone and under the 1 ms ceiling on this host's
clock.

* The 24 decisions per frame take 523 µs on average and 901 µs at most (no ceiling; 504 / 1,050 µs before the review)
  **[m, host x86-64]**.
* Under the ceiling 0 of 600 frames were cut and 0 records were over **[m, host x86-64]**.
* A frame cut once leaves its skipped members one step old, still within the bound **[i]**.

i686 in game is not measured; the first 300 µs ceiling cut every frame on this host (oldest record 1,724 ms over).

**8. The frame-step reader** (decision 2026-10-13). `frame_step_ms()` returned 0 for steps above 1 s, which dropped amendment 15's
window scaling on exactly the long frames. It now returns the real step clamped to 5 s (`av::frame_step_clamp`, shared with
`note_step`). Its consumers:

* E's capital window and the lane pass's freshness use `fresh_window_ms(250, step, 200)`;
* `join_evaluate16` uses `fresh_window_ms(250, step)` (no re-plan term);
* the resolved set uses `fresh_window_ms(100, step)`.

`fresh_window_ms` still caps its own term at 250 ms, so on a long frame every one of these windows is 1 s: as on a 250–1,000 ms frame,
never more. Before the fix such a frame left the windows at their 250 / 100 ms bases. Unaffected:

* the servo tick, which reads `[root+0x714]` itself and keeps its 1..1,000 ms clamp;
* the formation membership's 20 s;
* the 120 s hand-over bound.

Unit: the clamp (16 → 16, 2,000 → 2,000, 7,000 → 5,000 ms); the E window 250 ms at 16 ms and 1,000 ms at 2 s (the old reader: 250 ms);
the resolved window 100 / 1,000 ms; a capital's gate row 900 ms after its decision on a 2 s step now `suppress_gate` (with the old
reader it passed). This changes key-off behaviour only on frames longer than 1 s: the core comparison, `keyoff_identity_b.sh`, is
unchanged at 0 differences.

**Scenario T, `run162 seta`** is the Run162 timeline with game steps of 1,500–2,000 ms on 2 % of frames (Run163 had 6 steps above 1 s
in 82k frames). It keeps every existing check of the variant, three of them as TARGET rows (below), and adds the B.3 checks:

* 0 own-gate warns left to the engine;
* 0 `pass_not_fresh` of a funnel ship;
* every escort launching within 120 s of the launch ahead of it in its convoy, plus its own funnel time. For the first escort that is
  its leader's launch, for the second its sibling's: the funnel takes one ship at a time, so the second escort's release waits for the
  first's launch even without SETA. This is the bound as accepted on 2026-10-13.

The figures are in the ledger. The TARGET rows of the variant:

* D.7 item 4 (a launch within 30 s of the sequence plan): 34.9–42.1 s in 6 of 9 runs. The released leader's S-curve overshoots on the
  long steps; its predicted feasible speed falls to 25–40 m/s (no yield cap), and the funnel takes about 30 s longer than planned.
* D.7 item 2 (no engine evasion between members): 2 and 1 episodes in 2 of 9 runs. Run161's order-0 gap: 29 of 6,743 warns in one
  run. Each is a pair of sibling escorts handed back (`unresolved`) at a capsule gap of 677 m or 911 m, after a 1.9 s step put the
  second escort's order 4 within 0.1 s of the first's release. There was no capsule contact: D.7 item 3 holds in every run.

One bounded attempt to make them hard checks (decision 3, reverted, measured). (a) After a long step the funnel's carrot led by the
step's travel beyond 250 ms. (b) A reserved holder whose released sibling was within the sweep's 30 s margin (L_a + L_b + 50 m +
10 m/s x 30 s) fell back at the petals' 25 m/s. The variant still had 9 TARGET rows:

* D.7 item 4 in 5 runs, 30.6–37.5 s;
* D.7 item 2 in 3 runs, 1–2 episodes;
* the order-0 gap in 1 run, 29 of 6,913.

(b) also changed 81 scenario T lines of the existing variants. So both were reverted, and the rows stay reported with the causes above.
The long step's overshoot is integrated by the plant after the module's last command, and a lead from the last step cannot anticipate
the next one **[i]**.

**Cost** (host x86-64 -O2, measured against `f2c01be2` on the same idle host, after the decisions): unchanged within noise.

| Measure | Host time |
| --- | --- |
| Pair sweep, broad-only | 73 ns |
| Pair sweep, worst clear | 448–457 ns |
| Decision on a full board: spread / packed / in conflict | 4.5 µs / 6.5–7.3 µs / 26.0–31.8 µs |
| Decision with 21 petal plans, best / worst (noisy in both builds) | 7.5–8.3 µs / 37–43 µs |
| `sb_frame` | 16.5 µs |
| E's gate row | 9–11 ns |
| Member-pair lookup | 7 ns |

Added per frame: `note_step` (one compare after the first call of a frame, four stores on it), `members_live` (96 slots) and one QPC
read per decision **[i]**. Static: Member 1,828 B (+20), Gate 55,044 B (+488), board 220,196 B (+1,964).

**Key off.** Everything above runs only while the controller is live (`traffic_live_`): `note_step`, `ctl_note_warn`, the 0x12 branch,
the budget. The E row's tail is written only for a member's gate verdict. Key-off identity against `f2c01be2`: 0 differences. Fixture
lines outside scenario T and the stage-B lines: 0 differing in all three modes (ledger).
