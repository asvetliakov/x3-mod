# Convoy gate transit: one lane, merge like cars, no holding

Design note, 2026-10-14, for ratification by the main session (opus variant). Question: how capital convoys (a leader
plus 2–4 capital escorts) go through a gate smoothly instead of the holding loops and waits of the level-1 controller
([gate-traffic-controller.md](gate-traffic-controller.md), stages A/B, B.1–B.9). Teleporting is out. Binding user
decision of 2026-10-14 (via the coordinator) is the acceptance behaviour: *no waiting; the leader goes, then the escorts
go; when two ships are level, the second one slows or stops briefly so the first gets ahead, the first does not slow at
all, and the second gives only that space, it does not wait for the first to pass the gate.*

**Outcome: replace the petals, the release window and the one-ship funnel by one departure lane per gate. Capitals
merge onto it like cars at a lane merge (the ship behind yields until its nose is one trail gap behind the other's tail,
the ship ahead keeps its speed) and follow each other down it at hull spacing. Each capital's order 4 ends at a point we
write on the lane about 2.3 km before the gate (the short final). The script's own steps are kept. A constants-only
patch of two Mayhem scripts removes the escorts' 4–9 s script wait and their 2–3 s order-0 gaps. A one-byte patch of the
KC's 60 s through-slot cooldown removes the opposite-direction block. A formed 1+2 convoy then takes ≈ 185 game s from
15 km to its last launch, against 330–770 s measured; 1+4 takes ≈ 235 s **[i]**.**

Sources. Engine and script facts: [ship-navigation-ai.md](../reverse-engineering/ship-navigation-ai.md) §4, §10.1–10.11;
[gate-queue-script.md](../reverse-engineering/gate-queue-script.md) §2, §6, §8. Flight data: Run166–169 triage
directories under `verification/results/ship-steering/`. New measurement for this note:
[`convoy-gate-transit/convoy_spans.py`](../../verification/results/ship-steering/convoy-gate-transit/convoy_spans.py) →
[`convoy_spans.txt`](../../verification/results/ship-steering/convoy-gate-transit/convoy_spans.txt). It streams the
`ship_gate_ctl_member` rows of a triage log and gives, per convoy: the leader's first moment within 15 km, the launches,
the escorts' distances at that moment, and the no-wait lane floor. Game seconds throughout. SETA was on for 69–82 % of
each convoy's frames (`seta.txt`).

## 1. What the player sees today [m]

| Convoy (run) | Leader 15 km → its launch | Escort launches after the leader | 15 km → last launch | Escorts' distance when the leader was at 15 km |
| --- | --- | --- | --- | --- |
| 4900 + 2 M7 (168) | 131.5 | +123.9, +198.5 | 330.0 | 17.8, 18.6 km |
| 10107 + 2 (168) | 130.7 | +144.7, +206.6 | 337.3 | 26.0, 29.4 km |
| 5727 + 2 M7 (169) | 136.0 | +179.4, +224.4 | 360.4 | 24.9, 30.3 km |
| 24965 + 2 (168, 169) | 134.0, 134.6 | +390, +547 | 681.3, 681.5 | 41.7, 49.3 km |
| 4673 + 2 (169) | 607.2 (slot block, out of window) | +80.6, +162.0 | 769.2 | inside 15 km |
| 3097 / 8846 / 12699 + 3861, 4120 (169) | 214.6 / – / – | escorts re-targeted twice by the script | 2,170 (coordinator's `convoy_summary.txt`) | 25.9, 30.3 km |

* **The leader's straight-line time** from 15 km to its launch at 100 m/s is 113.6–116.4 s; the order-5 trigger fired at
  3.36–3.64 km under SETA. Unobstructed leaders took 130.7–142.3 s **[m]**.
* **Per escort, the serial funnel costs about 100 s**: release 8–14 km out, 55–116 s release → X, 15–30 s X → launch. The
  funnel ran two-deep 3 times (`run169-triage/funnel_serial.txt`).
* **Holding.** Escorts held 2,417 s in Run169 with their leader not yet launched and 507 s after its launch
  (`hold_totals.txt`). Heads held 379 s out of their window and 310 s on a slot block. Escort nose-away-from-gate time
  was 606 s (the coordinator's critical-path table).
* **The escorts straggle in.** At the leader's 15 km moment they were 17.8–49.3 km from the gate, i.e. 3–34 km behind
  their leader. This is a consequence of today's design, not of the escorts **[i]**: the previous gate let them out 80–550 s
  after their leader at the same v_max, so they arrive that far behind at the next gate. A convoy that leaves in trail
  arrives in trail.

## 2. Recommendation

1. **One departure lane per gate (stage C1, DLL only, existing sites A/B/E plus one field write).**
   * The lane Λ is an F-parallel line in the lower half of the disc (D.2.1), from the gate back to about 15 km. In its
     last 4 km each hull's final moves within its own D.2.2 pass window (for example 370–670 m below the axis for the
     Run160 census).
   * Every capital committed to the gate joins Λ at the first lane point it can reach aligned. That is behind the
     current tail, or at the merge point a merge decides (§4.2).
   * It then flies Λ at v_lane = the large cap (100 m/s), followed at hull spacing.
   * No petal, no release window, no lap law.
2. **The merge rule**, the user's: the ship behind yields, the ship ahead never changes speed for the pair (§4.3).
3. **The short final** (§4.5). At a capital's first order-4 frame toward the gate, site A rewrites the order point
   `f+0x240` from the KC grid point to the hull's lane point L_i, which lies d_end = 2.3 km from the plane minus
   0.75 r_gate.
   * Order 4 then ends on the lane at about 2.3 km.
   * 0x1f is a one-frame no-op because the ship is aligned.
   * Order 5's leg shrinks to (2.3 − t_trig) km: about 1.1 km with SETA off, and a first-frame trigger under SETA.
   * This makes the KC's through-slot occupancy shorter than the hull spacing, so no ship ever paces on the previous
     ship's gate passage (§3, T_slot).
4. **The escorts' gate order** follows the leader's launch with no visible wait in C1. The trail spacing (≥ 19 s)
   exceeds the leader's slot occupancy plus the measured script latency (§4.6). **Stage C2** removes the script wait
   itself with a constants-only patch of two Mayhem MSCI scripts:
   * `!move.follow.template` line 97: the 2–3 s wait → about 0.1–0.3 s;
   * `Mayhem.Task.Move` line 221: the 10 km follow-gate threshold → beyond the sector, so `fly to sector` is called at
     once;
   * the same patch shrinks the 2–3 s order-0 gaps in which the engine slows a passing escort.
5. **The opposite-direction block** (61 s after a capital arrives through the gate pair) shrinks to N ≈ 10 s with the
   KC's 1-byte option A1 ([gate-queue-script.md](../reverse-engineering/gate-queue-script.md) §6, `pushb 60` → `pushb N`
   in `ReleaseSlot`), stage C2. The half-disc split (D.2.1, D.4) already separates the two directions in space.
6. **Arrivals of one convoy** get a fan at `0x00444606`: the second capital arrival within 30 s goes −0.6 r sideways
   instead of +0.6 r up, the third +0.6 r sideways. A trail at ≈ 20–25 s cadence would otherwise put two hulls on the same
   P (§4.8).
7. **One key** `ship_steering_gates_lane`, which needs `ship_steering_gates_traffic`. Off means stage B as installed
   (Run169). The C2 files are separate, reversible loose files with SHA-256 guards on the Mayhem originals.

## 3. Option matrix

Times are for a formed convoy of census hulls (M2/M1, L ≈ r ≈ 1.0–1.1 km) with SETA on unless marked, and are **[i]**
except the "today" column. Symbols:

* c = the trail cadence = max((L_prev + L_next + g_req) / v_lane, T_slot);
* g_req = 400 m;
* T_slot = the KC through-slot occupancy, from the grant at X to the release after order 8.

Measured X → launch is 14.6–21.3 s for leaders and escorts in Run169, plus 0.9 s of order 8 and about 1 s of hand-over,
so T_slot ≈ 17–23 s with SETA on. With SETA off it is ≈ 41–45 s from today's X, or ≈ 14 s with the short final.

| | Today (level 1, B.9) | (a) Level 2 as named | (b) Convoy unit as briefed | (c) Own transit state machine | **(d1) One lane + merge (recommended)** | (d2) Two-lane echelon + concurrent order 5 |
| --- | --- | --- | --- | --- | --- | --- |
| 15 km → last launch, 1+2 | 330–770 s [m] | ≈ 320–760 s (saves λ ≈ 4–9 s once) | not reachable as stated (2–3 s launches); its reachable form is (d1) | ≈ (d1) | **≈ 185 s** (135 + 2 × 25) | ≈ 150–160 s |
| 1+4 | – | – | – | ≈ (d1) | **≈ 235 s** | ≈ 160–175 s |
| SETA off, 1+2 | – | – | – | – | ≈ 200 s with the short final; ≈ 225 s without it, pacing on the gate passage | ≈ 175 s |
| What the player sees | petals 8–14 km out, escorts circling 270–520 s, the funnel one at a time | the same petals, the escort's order a few s earlier | a column, then gaps forced by the KC | a column | a column flying straight in; level ships merge; nobody circles | two columns side by side |
| Engine and script surfaces | sites A/B/E/P2/P4/P5, `0x00444606` | + DLL-started orders, + KC clearance patch | + interception of script order writes | + new order handlers or full order-4/5 emulation, + KC patch | sites as today, + the `f+0x240` write on capital order 4, + MSCI constants (C2), + KC A1 1 B (C2) | (d1) + KC slot rewrite (B or A5) + arrival fan for 2–3 hulls at once |
| Script-fight risk | none (level 1) | high: a DLL order wakes the script's blocked call with a false result, and the KC never runs the sector change | high, same | high, same | low: the script's orders are kept and steered; constants only | medium: two holders per gate pair is new KC state |
| Level 1 | – | kept in full, petals included | petals deleted, sweep kept | most deleted | §4.10 | as (d1) |
| Cost | – | medium, small gain | – | very high | medium (C1), small (C2) | high, after (d1) |

## 4. The recommended design in detail

### 4.1 Lane geometry

* **Λ** is in the gate frame (R, U, F as in D.2.1). It is the line `G + ℓ_c U·(−1) + s F` for s ∈ [−15 km, −d_end], at
  lateral ℓ_c = the middle of the members' common pass window (D.2.2). Inside s > −4 km each hull's final moves to its own
  ℓ_i within its window, a lateral change of at most 500 m over ≥ 2 km (≤ 15°). Hulls under ≈ 500 m, whose window is
  0–137 m, take ℓ_i near the axis. This happens after the last merge, so finals never cross.
* **Joining.** A member flies its inbound carrot as today (B.2, B.8's braking profile) toward its **merge point** M_i on
  Λ. M_i is the lane point where its join curve (cornering cap, the existing join predictor) becomes tangent to Λ. For
  inbound ships it lies 8–15 km out. A **re-departing ship** (one that arrived through this gate, faces −F at P and leaves
  through it again) flies out along −F, makes a U-turn at its cornering cap and joins Λ 3–5 km out. Today it is sent to
  an 11 km petal and back: 591 s with the nose away from the gate (coordinator, Run169) **[m]**.
* **Speed on Λ:** v_lane = 100 m/s (the large cap, written both directions by site B) unless the follow law (§4.4)
  or the order-5 ramp says less.
* **d_end = 2.3 km** with SETA off. It is the end of order 4, beyond the order-5 trigger t = (|v|/10 + r_s) of a 1.2 km
  hull plus 1 km, and it keeps a stopped ship at a refusal (D.2.6) 2.3 km out, on the lower half, clear of the upper
  arrival lane. Under SETA the trigger (3.4–3.6 km [m]) lies beyond d_end, so order 5 ends on its first frame and order
  8 starts at d_end. Arrivals already come out on the other half.

### 4.2 Who yields (merge rank)

The gate keeps one ordered list, the **lane order**, of its members on or joining Λ. Rank by **ETA to the merge
point** of the later-merging ship of the pair, i.e. who would be there first at their present plan speeds. For two ships
both on Λ, that is simply their along-lane position.

* The ship with the **later ETA yields**. Within a tie band of 2 s, as with two escorts abreast at the same distance (the
  user's example), the **higher sequence number** yields.
* **Exception, the user's "the leader goes, then the escorts go":** a convoy's leader is always ahead of its own escorts.
  An escort that is ahead of its leader yields to it, whatever the ETAs. An escort cannot start order 4 before the
  leader has left the sector anyway (§10.11 Q3), so an escort past its leader on the lane would have to wait at d_end.
* A foreign capital may merge between a leader and its escorts if its ETA is earlier. That costs the escorts one cadence
  c, not a wait. Convoys are not reserved as blocks.
* Once a yielder has fallen in behind, the pair is a **follow pair** (§4.4). The lane order changes only at a merge,
  never by overtaking on Λ.

This replaces stage A's `yields_to` (sequence order, then the B.6 release order). The B.9 open item "the front, holding
the higher sequence number, is the designated yielder and cannot open a gap closing from behind" disappears, because the
front ship is never the yielder.

### 4.3 How much the yielder gives (merge law)

Let s be the along-lane coordinate (toward the gate), for the ship ahead P and the yielder Y:

* tail(P) = s_P − L_P and nose(Y) = s_Y + L_Y, with L = max(r, l), the capsule's half-length (D.6);
* the merge is complete when **gap = tail(P) − nose(Y) ≥ g_req = 400 m**: 350 m is the sweep's 30 s margin (50 m +
  10 m/s × 30 s), plus 50 m.

Until then:

* **P's written speed is its plan, exactly.** No pair cap is ever applied to the ship ahead (the user's "does not
  reduce speed at all"). Its lateral path is its own join.
* **Y's speed:** `v_Y = clamp(v_P,along − (g_req − gap) / τ_m, v_floor, v_plan,Y)`, with τ_m = 8 s, re-evaluated at the
  decision cadence (200 ms). With census hulls level at the merge (gap ≈ −2.1 km), Y drops to v_floor and opens the gap
  in (2.1 + 0.4) km / (100 − 10) m/s ≈ 28 s. It is then about one cadence c behind P, which is exactly its place in the
  trail. So the merge costs the yielder nothing beyond the cadence it owes anyway **[i]**.
* **Lateral:** Y keeps its own join line and moves onto Λ only once gap ≥ g_req − 100 m. So the convergence happens
  behind P, never beside it.
* **v_floor = 10 m/s** (the existing yield floor). **A brief stop** (the user allows "stops briefly") applies only when
  the rollout at 10 m/s still predicts a capsule conflict. Y is then written 0 for at most T_stop = 20 game s plus one
  game step (SETA-aware: the bound is in game time and widened by the last step `Gate::step_ms`). If that does not
  certify either, the pair goes back to the engine as today (`cause=contact`).
* **On order 0** (the script's re-issue gap) nothing is written. B.9's `gap_yield` stays: the cap applies on the first
  steered frame, and the rollout brakes through the gap.

### 4.4 Following and slot pacing (the trail)

After a merge, Y follows P down Λ:

* **Follow law:** `v_Y = min(v_plan, v_P + (gap − g_req) / τ_f)`, τ_f = 10 s, floor 10 m/s. The trail holds
  centre-to-centre spacing L_P + L_Y + g_req, which is 2.5 km for census M2/M1 and 1.9 km for the M7 pair.
* **Slot pacing, the head only.** The lane's first ship that has not yet reached d_end aims to reach it no earlier than
  the slot model's free time (D.2.5: predecessor's predicted release + 2 s, a partner-side block). This uses the same
  speed shaping as today's funnel, down to 10 m/s, and then the brief-stop rule.
  * By construction (§3) T_slot < c with the short final. So pacing binds only behind a partner-side block or a KC
    refusal, and never on a convoy's own predecessor (the user's "does not wait until that ship has passed the gate").
  * Followers pace only on their predecessor's hull, through the follow law.
* **Escort gate (C1 only, without the C2 patch).** An escort without order 4 must not pass its d_end. It is written
  `min(follow, v reaching d_end at t_L,launch + λ_max)`, with λ_max = 10 s (measured 4.1–8.8 s, fixture 4.3–4.5 s).
  With the short final this binds only if the leader's launch is late:
  * SETA off: the leader's launch is at X_L + ≈ 13 s, E1 is at d_end at X_L + c ≈ 25 s, and λ ≤ 10 s fits;
  * SETA on: the launch is at X_L + ≈ 2 s.

### 4.5 The short final: writing the capital's order-4 point

* **What.** On a capital's order-4 frames toward its gate (site A, class 0x12 target, capital code `0x0043fd50`), when
  `f+0x240` equals the KC grid point `GetWarpInPos(slot)` for some slot 0–19, site A writes the hull's lane point L_i. L_i
  is the point on its final at 2.3 km + 0.75 r_gate before the plane, so order 4's 0.75 r_gate arrival test fires at
  d_end.
  * The grid formula is closed-form (§10.2): `G − 2(r_gate + r_s)F + 666 m (a R + b U)`.
  * The guard compares against all 20 grid points within 1 m.
  * A re-issue after 0x12 or the result-7 path writes the grid point again, and the guard catches it again.
* **Why it is safe as far as known.** The KC passes the grid point once per blocking call and never reads it back.
  No SA case returns `f+0x240` to a script (§10.11 Q2 sweep) **[m]**. The serializer saves it **[i]**, so a saved and
  reloaded ship flies to L_i, a valid point in front of the gate, with or without the key.
* **What it buys.**
  * SETA off: T_slot drops from ≈ 41–45 s to ≈ 14 s, below the hull cadence, so the trail never paces on a gate passage.
  * Every capital's X is on its own final whatever slot the KC handed out. That removes D.2.2's chord constraint (13 of
    20 slots) and the late jog of escorts that get slots ±2 (1,332 m lateral).
  * The 828 m chord precision of X goes away.
* **Not yet established** (§6 items 1–2): that the capital variant `SA_FlyObjectBeforeContainerPos` (`0x0046533c`)
  stores pos in `f+0x240`, and that the order-4 handler's arrival test (`0x00454b1a`/`0x00454b70`) and goal read
  (`0x00454a20`) take it from there every re-plan with no cached copy. Also whether the container detour (§3.4) engages
  for an order point within 2 r_gate of the gate. **Fallback if the disassembly says no:** keep the grid points.
  * Run the lane through the slot-0 column. Slots 0, 1, 2 and 5 are chords of it (666 m < 828 m); slots 3, 4, 6, 7 need
    a 114–504 m jog, done ≥ 4 km out.
  * SETA-off cadence is then T_slot ≈ 41–45 s, and the trail paces on the gate passage with SETA off. That is a
    documented deviation from the user's rule.

### 4.6 The escorts' gate order with no script wait

* **Why the DLL cannot issue it.** The sector change belongs to the KC. `__EnterHyperspace` issues order 8 and then runs
  `ReleaseFromSlot`, `Remove()` and `__LeaveHyperspace` in the destination (§10.10 Q1). An escort's script is blocked in
  `escort ship` (0x14) or `follow object` (0x15). A DLL-started order 4/5/8 on that ship would:
  * wake that call with result 1 as if its leg had timed out;
  * leave the ship on order 0 at the far side of the plane after order 8, still in the sector;
  * then the script's `fly to sector` would send it back to a grid point.

  Option (a) loses here.
* **C1 (geometry):** with §4.4's escort gate the 4–9 s latency is never visible. The escort keeps flying the lane at the
  follow speed, steered on 0x14/0x15 (both steered orders; leaf and speed are ours), and its order 4 arrives before
  d_end.
* **C2 (removes the wait).** Constants-only patch of the installed Mayhem loose scripts, SHA-256 in
  `kc_escort_flow.txt`.

  | Script | Line | Change | Effect |
  | --- | --- | --- | --- |
  | `!move.follow.template.xml` | 97 | `wait randomly from 2000 to 3000 ms` → 100 to 300 ms | `CheckLeader` runs within ≈ 0.3 s of the 0x14 leg ending on the lost leader; the 2–3 s order-0 gap between 0x14 legs shrinks to ≤ 0.3 s, so a passing escort is no longer slowed by its own script |
  | `Mayhem.Task.Move.xml` | 221 | `$distance > 10000` → `> 10000000` | the escort calls `fly to sector` at once, wherever it is; KC order 4 to its grid point, rewritten by §4.5 |

  * Expected λ: the KC scheduler resume plus `!move.jump`/`Task.Move`'s non-wait statements, target ≤ 1 s **[i]**.
  * The patcher, like the KC one (gate-queue-script.md §8), reads the originals, checks their SHA-256, rewrites the
    constant values in the `<codearray>` only, keeps instruction positions, and writes the result in place with a
    backup. A Mayhem update changes the hash; the patcher then refuses, and C1 still hides the latency.
  * Raising the 10 km threshold is the larger behaviour change: every Mayhem NPC escort in the universe enters the KC
    transit from wherever it is once its leader has left. That is stock X3's behaviour for `fly to sector`. Mayhem's
    reason for the 10 km follow leg is not documented (§6 item 6). If the main session prefers the smaller change,
    patch line 97 only. Escorts beyond 10 km then keep 5 s 0x15 legs with 0.1 s gaps (measured) and get order 4 at
    their 10 km crossing, while still flying the lane.

### 4.7 The opposite-direction block

The KC refuses our side for 60 s after a partner-side holder releases (§10.2). That is the 310 s "head, slot block" of
Run169 **[m]**, which includes re-departing ships blocked by their own convoy's arrival.

* **C2:** option A1, `pushb 60` → `pushb 10` in `ReleaseSlot`. It is one byte in the loose `L\x3story.obj` built from
  Mayhem's object. It is save-compatible both ways (build id unchanged; gate-queue-script.md §6) **[m]** static.
* 10 s covers the arrival's 0x21 (0.9 s) and its 0x1e until r_gate clear (≤ 9.2 s at 120 m/s for a hull under r_gate).
  The disc split keeps the departure lane in the other half.
* It applies to every gate pair, in and out of sector. Out of sector there are no engine objects to collide.
* A1 does not change same-direction serialisation. The KC still grants one through slot per gate pair; the lane design
  does not need more (§4.4).

### 4.8 Arrival side

* D.4 is kept (capital +0.6 r up at `0x00444606`, small-ship side spawn, the 60 s arrival-gate row).
* With trail cadence c ≈ 19–25 s, a convoy's next capital appears at the same P while the previous one may still be
  there. A hull of r_s ≥ 0.99 r_gate ends 0x1e on its first frame and sits at P at rest until its script moves it
  (§10.10).
* So the launch site keeps a per-gate count of capital launches in the last 30 s. The k-th (k = 0, 1, 2) gets the offset
  +0.6 r U, −0.6 r R or +0.6 r R, with the Terran bound unchanged. This only matters when the player is in the
  destination sector.

### 4.9 Edge cases

* **The player's ship.** It is not a member and is never ranked. It stays an obstacle under the existing rules (the
  manual player ship is excepted from the r/3 ignore), so the engine warns a capital that would hit it, and that member is
  evicted as today. Player-owned convoys teleport by beacon in Mayhem (`!move.jump` line 83) and never fly the lane. The
  member guard needs a way to recognise player ownership (§6 item 8).
* **Small ships.** D.5 is unchanged. "Funnel active" becomes "a member on the lane's last 6 km or on order 5/8". Small
  departures use the free side of the lower arc. Small escorts keep the formation law with the corridor projection.
* **Combat.** `__EnterHyperspace` returns 7 when attacked, and the escort template's attack checks break off. The
  member leaves the lane. Its follower's follow law then sees the next ship ahead (or none) and speeds up to its plan.
  Nothing is held for it.
* **Leader destroyed mid-transit.** The template's `while $leader exists` ends. An escort already in its own
  `__EnterHyperspace` (order 4 and later) transits as an ordinary lane member. One still on 0x14 leaves the lane when its
  next order arrives. The leader-first rule lapses with the leader.
* **The script re-targets escorts to a newly arriving M1** (Run167–169: 3861 and 4120 three leaders, 2,170 s). This is
  script-owned and not prevented. Ranking is per gate, not per convoy, so a re-targeted escort simply becomes another
  ship's escort or leaves. §6 item 7 asks where it comes from.
* **Save/load.** Board state is in-process. On load the lane order is rebuilt by ETA, and §4.5's written points persist
  harmlessly. A backward clock jump clears caps (B.9).
* **Key off:** no rewrite, no merge law, stage B unchanged. C2 files are removed by the installer's restore. A save made
  with A1 loads without it (§6 of gate-queue-script.md).

### 4.10 What of level 1 is kept, deleted or folded (under the new key)

| Item | Fate |
| --- | --- |
| D.1 member board, capsule sweep, site E suppression, eviction, pointer liveness (B.5) | **kept**. The pair priority becomes the merge rank (§4.2). The rollouts fly the merge, follow and stop laws, with each follower's rollout reading its predecessor's rolled-out samples, so the front is rolled out first |
| B.9 rollout fixes (in-plane split, inbound law, `gap_yield`, signed age, cornering cap, clock-jump reset) | **kept**. `hold_law` is replaced by `lane_law` (join → lane → follow) |
| D.2.1 disc halves, D.2.2 pass windows, D.12 both-direction write on order 5, P2/P4/P5 | **kept** |
| D.2.3 funnel, one ship at a time | **replaced** by the lane, many ships at hull spacing |
| D.2.4 petals, B.6 lap law and window, B.7 escort stage, B.8 join-to-track and dwell, the head skip, the go-around | **deleted** |
| B.8 inbound braking profile on the join | **folded**: brakes to v_lane at the merge point, not to 60 m/s at the ring |
| D.2.5 sequencer and through-slot model | **kept** as the head's pacing input and the partner-block clock. Sequence numbers are only tie-breakers now |
| D.2.6 grid point untouched; refusal = vanilla stop at X | the grid point is **replaced** by §4.5. The refusal stop is **kept**, now at d_end |
| D.3 escort petals, reserved sequence numbers, `esc_in10`, `escort_settled` | **deleted**. Escorts are lane members under the leader-first rule |
| D.4 arrivals | **kept**, plus the fan (§4.8) |
| D.5 small ships | **kept** |
| Diagnostics | `ship_gate_ctl_member` keeps its fields, minus the petal ones, plus `lane_s`, `rank`, `gap_m`, `law=join/merge/follow/stop/pace`. The pair row gains `cause=merge`. A `ship_gate_ctl_merge` row is written on each merge start and finish |

### 4.11 Hot path, cost, native Windows

* **Per frame:**
  * the lane order is an insertion sort of ≤ 24 members by ETA per gate;
  * the merge, follow and pace laws are a few flops per member at the 200 ms decision cadence;
  * the coupled rollout adds one predecessor sample lookup per sample (61 per rollout) to the existing ≈ 6 µs typical /
    70 µs worst decision work **[i]** (D.8);
  * deleting the petal geometry and lap law removes more than this adds **[i]**.
* **§4.5:** one class/code test per capital order-4 site-A call. The 20-point grid compare runs only when `f+0x240`
  differs from the member's last written value. The write is one 12-byte store.
* No allocation, no lock, game thread only, SSE2 scalar.
* C2 has no DLL cost. Script CPU: the template's loop head runs more often only when a leg ends early, about once per
  30–34 s leg **[i]**.
* **Native Windows:** the game runs natively at `C:\X3`. Everything is an in-process patch of the same `X3AP.exe`
  through `engine_patch::verify_bytes`/`claim`. The loose script files are data for the game's own loader, identical on
  every platform. No OS API is added.

### 4.12 Verification

**Fixture: scenario T gains lane variants** (host, calibrated plant with slip, the KC and escort-script models, the
sibling warn).

* Variants:
  * **abreast** (the user's case): leader + 2 escorts level at 12 km, and + 4;
  * **formed column**;
  * **stragglers**, with the Run169 15 km offsets from `convoy_spans.txt`;
  * **re-departure**;
  * **crossing convoy**, a second convoy merging;
  * **partner block**;
  * script models with and without C2 (λ 4.3–9 s vs ≤ 1 s, gaps 2–3 s vs 0.3 s);
  * SETA on and off, 60 and 144 fps, 12 seeds.
* Assertions:
  1. **The passer:** in every frame of a merge, the ship ahead is written exactly its plan speed (no pair cap). Measured
     as the count of capped frames on the ahead side = 0.
  2. **The yielder:** gap ≥ g_req − 50 m within 40 s of the merge start, speed ≥ 10 m/s except stops, each stop
     ≤ T_stop + one step, lateral convergence only after gap ≥ g_req − 100 m.
  3. **No holding:** 0 petal frames. A member's cumulative turn after its merge point ≤ 30°, except the re-departure
     U-turn, which happens before its merge point.
  4. **Order:** the leader launches before its escorts; consecutive launches are ≥ T_slot apart and, for a formed
     convoy, ≤ c + 5 s apart.
  5. **Escort gate:** no escort passes d_end without order 4. Order 4 comes ≤ λ_model after the leader's launch. No
     stop at d_end except a modelled KC refusal (counted).
  6. **Safety:** 0 0x12 between members, 0 capsule and 0 box contact, certified on every frame from a merge's first
     certificate to its completion + 30 s.
  7. **Span:** 15 km → last launch ≤ the leader's 15 km → launch + n·c + 10 s for a formed convoy, and ≤ the lane floor
     of `convoy_spans.py` + 10 s for stragglers.
  8. **Key off:** stage B outputs identical (as D.7 item 8).
* Units: merge rank (tie band, leader-first), merge, follow and pace laws, lane geometry and pass windows, the §4.5 guard
  against all 20 grid points and its re-application after a re-issue, and the arrival fan.

**Flight oracle** (the next convoy flight, `--debug`):

* `convoy_spans.py`: 15 km → last launch ≤ the lane floor + 30 s per closed convoy.
* The coordinator's `convoy_summary.txt` buckets: "head out of window" 0, "funnel one at a time" 0, "escort before
  leader launch" nose-away seconds 0; "slot block" only after a partner arrival, ≤ 11 s with A1.
* Per escort: turns > 30° ≤ 2; stops only as a yielder, each ≤ T_stop.
* X nose ≤ 8°; `x12_pairs.py` 0 member episodes; `ship_gate_ctl_merge` rows paired.
* With C2: escort order 4 ≤ 1.5 s after the leader's launch (`escort_wait.py`).

## 5. Options considered, and why they lose

* **(a) Level 2 as named** (the DLL issues the escort's order 4/5 at the leader's launch, plus a KC clearance patch).
  * An order is not a transit: the sector change is KC code that runs only inside the escort's own `__EnterHyperspace`.
    A DLL order would wake the escort's blocked 0x14/0x15 call with a false result and strand the ship on order 0 past
    the plane (§4.6).
  * Its gain would be the first escort's 4–9 s latency. C1 hides that geometrically and C2 removes it with constants.
  * The measured cost it does not touch is ours: petals, the serial funnel and the release window, ≈ 100 s per escort.
* **(b) Convoy unit at 15 km with 2–3 s between launches.**
  * One lane cannot hold two 1 km-radius hulls 300 m apart: in line they need ≥ L_A + L_B + 400 m ≈ 2.5 km, i.e. 25 s at
    100 m/s.
  * The KC grants one through slot per gate pair from X to the end of order 8 (§10.2 **[m]**), so even overlapping
    hulls would launch ≥ T_slot apart.
  * Taking the whole convoy at 15 km also means either waiting for stragglers (the escorts were 3–34 km behind,
    against the user's "the leader goes") or leaving them; per-gate merge ranking does the second naturally.
  * "Neutralising the script's follow orders" is unnecessary: 0x14/0x15 are steered orders and the lane is flown on
    them.
* **(c) Our own transit state machine replacing order 4/5.**
  * The KC still owns the slot grant and the sector change. The engine's order-5 trigger is trivially met from the lane.
  * So it would re-implement steering we already own through sites A/B, plus a KC patch, for no time gained over (d1).
* **(d2) Two-lane echelon with concurrent order 5** (the coordinator's 3 s spacing).
  * Side by side, census hulls need ≥ w_A + w_B + margin ≈ 1.2 km laterally. Two lower-half lanes at ±600 m fit the
    order-5 trigger tolerance (t ≈ 1.1 km SETA off) and give ≈ 150–175 s for 1+2 to 1+4.
  * It needs, all unestablished: the KC slot rewritten to two holders per pair (option B or A5; A5 alone lets any number
    fly order 5 at once with CollisionWarn ignored), a three-way arrival fan for hulls appearing seconds apart, and the
    concurrency disassembly (§6 item 3).
  * It is a later step on top of (d1), whose lanes it reuses; it does not replace it.
* **Form-up before the gate** (the leader slows until its escorts close up). It shortens the convoy's span only when
  escorts are within reach. It makes the leader wait, against the binding rule. Cruising the leader at 0.9 v_max
  between gates so that convoys arrive formed is a separate question for the user; it is not part of this design.
* **Keep petals as a fallback for crowds.** Speed shaping down to 10 m/s, then a brief stop, absorbs a partner block
  (≤ 11 s with A1, 61 s without) on the final. Upstream, slower joining absorbs a lane queue: 15 km at 25 m/s instead
  of 100 m/s absorbs 450 s.
  * Measured crowds: at most 3–4 members at once per gate in Run169 (`transits.txt`).
  * Loops are what the user rejected.

## 6. To disassemble or settle before implementation

| # | Unknown | Settled by |
| --- | --- | --- |
| 1 | Where `SA_FlyObjectBeforeContainerPos` stores pos for a capital, and whether order 4 reads `f+0x240` every re-plan for both the goal and the 0.75 r arrival test, with no other copy | listing of the SA case `0x0046533c` and the order-4 handler `0x00454a20..0x00454b97` (arrival compare `0x00454b70`); a sweep of `+0x240` readers under order 4, extending `stageb_sites.py` |
| 2 | Whether the order-4 container detour (§3.4) engages for an order point within 2 r_gate of a gate, and what the re-plan interval `rand(50·type[+0x50])` is for capitals (the precision of d_end) | the same handler listing around the detour branch; the `type[+0x50]` values of the installed capital types |
| 3 | **Whether two capitals can be on order 5 / order 8 at one gate at once** (needed by d2, not by d1). KC: no, the through slot is held from the grant to `ReleaseSlot` after order 8 **[m]**. Engine: small ships already do so with no queue **[m]**, and the per-ship order-5/8 handlers write only per-frame OR flags on the gate (`0x2000000`, `0x10000000`, §10.11 Q1) **[i]** | listing of the order-8 handler `0x004551fe..0x00455330` and the gate post-pass update `0x0044b0c0` (`0x0044b124`, any single-occupant state), and of `ReleaseSlot` (`0x90c9c`) with two holders under option B |
| 4 | Whether site B (`0x004593dd`) is reached on a capital's order-0 frames, and whether order 0 writes `[obj+0x14] = 0` every frame or once (`0x00458f8e..0x00458fa8`). If written once and B is reached, the 2–3 s gap can be coasted in C1 without C2 | listing of the order-0 path and the object loop's continue edge |
| 5 | The 0x14 handler's end on a lost leader (taken from the handler table in §1.3, never listed); this is λ's first component | listing of the 0x14 handler's leader test |
| 6 | MSCI: how `wait randomly` and the `> 10000` compare are encoded in `<codearray>`; whether the loader prefers `.pck` over `.xml` in `addon/scripts`; whether saves store MSCI instruction positions (constants-only patch save-safe); Mayhem's reason for the 10 km follow leg | `tools/X3ScriptLoadStudy.java`-style read of the two installed files and the script loader; the Mayhem changelog or forum notes for `Mayhem.Task.Move` |
| 7 | Which Mayhem script re-targets waiting escorts to a newly arriving M1, and on what trigger | text sweep of `addon/scripts` for `escort ship`/leader assignment on arrival or commander change (`Mayhem.Squadron.*`, `!ship.cmd.protect.std`) |
| 8 | How the DLL recognises a player-owned ship (owner/race field) for the lane guard | the object's owner field in `game-scale-and-ship-classes.md`'s record layout or the `SA_GetOwner` case |
| 9 | The time-factor reading `[*0x606f34+0xcc]` (still **[i]**). It sets t_trig, and hence d_end versus the trigger and T_slot under SETA | one listing of its writer (the SETA toggle) and the fixture's tf variant |
| 10 | What an arrived capital does after its first-frame 0x1e (speed, next order) before the next convoy hull appears | a `ship_gate_launch` + member-row trace with the player in the destination sector (flight, user-run) |

Items 1, 2, 4 and 5 gate C1. Item 6 gates C2's MSCI half. Item 3 gates only (d2). The rest are flight checks.

## 7. Unknown, and what settles it

* The capsule half-length l per installed capital (D.11) sets c. With l < r the cadence is shorter. Settled by the census
  row with l.
* T_slot with SETA off and the short final (≈ 14 s) is a model. Settled by the fixture and by a SETA-off segment in the
  flight.
* The no-wait lane floors in `convoy_spans.txt` assume 134 s from 15 km to launch for every ship and c = 25 s. They are
  **[i]**.
