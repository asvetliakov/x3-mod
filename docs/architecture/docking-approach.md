# Docking approach (amendment 18): ships evading their own dock station or carrier

Orchestrator design, 2026-10-13, from [ship-navigation-ai.md §11](../reverse-engineering/ship-navigation-ai.md) (docking
disassembly) and the flights: Run157 (a traced TP took six engine 0x12 legs, 56.5 s, against its own dock station on
order 4), Run160 crowd flight (a TP circling at a station, 444 rows of 0x12). Scenario audit row 1. Ratified by the main
session as written; implemented after stage A of the gate traffic controller, in the same install.

## What the engine does [m unless marked]

* The approach point of `SA_FlyObjectBeforeContainer` (order 4) lies on the port node's −z axis (tilted 30°/60° for
  some port types), stepped out by 2 r_ship + 500 m until it is at least T = r_station + 2 r_ship + 500 m from the
  station centre; it is rebuilt from the container's live pose at every re-plan (carriers too, no velocity lead).
* There is **no own-container exemption** of the collision warn on orders 4 and 5 (only during undock 0x1e). On
  order 5 the engine still sets the warn but the KC ignores it (mode 0xA0001). On order 4 the KC reacts.
* The station warn fires when all hold: centre distance ≤ 7.2 km; the station inside a forward cone (66° at 4 km); the
  ship inside, or its forward ray within (1 s of travel + braking distance) hitting, a **part box enlarged to
  1.2 × (half-extent + r_ship)**. Stations and capitals take this box test instead of the close-range warn (§3.1
  corrected).
* On the warn the KC stops the ship, flies an avoid leg to an escape point 8.88 km out heading away, and then
  **re-issues the same approach from scratch**. That loop is the repeated-leg mechanism [i for the Run157/160 link].
* A ship queued for a port with no free slot idles on order 0x1d, which cruises ≥ 2.2 km along its current heading,
  which points at the station [i]: the queue itself walks ships into the warn.
* The final zone 2 r_ship + 50 m is kinematic (no leaf call, the engine moves the ship to the node at ≤ 40 m/s); order 5
  ends 3 m from the node.
* Carriers: same port code; a dock-capable carrier with bit 0x80000 gets speed 0 and no turning while any ship is on
  order 5 to it; a carrier never warns on non-large ships. Undock: fixed world exit point, tube ports launch at 100 m/s
  along +z inheriting the carrier's velocity; warns against the carrier are skipped and all warns within 0.8 r_carrier;
  the KC ignores warns for the whole undock.

## Decision

1. **Own-container row at site E.** For a non-capital A on order 4 whose `f+0x224` is its order's container B
   (station class 5/6 or a carrier), E returns `suppress_dock` iff:
   * B has a valid part model (not `part_fallback`), and the model is current (generation and stamp as the part set);
   * A's swept path over the engine's reach (v·1 s + v²/2a, along u = v f̂ + slip) clears every part sphere of B by
     1.2 r_ship + 50 m, **and** A's current forward ray over the same reach clears them likewise (the engine tests the
     ray, our avoidance steers the path: both must be clear);
   * A is outside the final zone 2 r_ship + 50 m of the port node (there the engine owns the ship and the leaves are
     not called), and A is not on 0x12;
   * the manual player ship, docked/attached and masked classes are excepted as in §15.
   Otherwise E passes (engine warn live). Part spheres contain the part boxes, so a path clear of the enlarged spheres
   is clear of the engine's enlarged boxes [i: sphere ⊇ box per node, the node radius `+0xa0` bounds the part].
   Capital A docking at a station or shipyard: the same row (the capital r/3 rule already ignores small B; the station
   is B here).
2. **Avoidance on the dock approach keeps the target's parts as obstacles.** Today `exclude_target` drops the order's
   target container for the whole of orders 4 and 5 (audit row 1). Change: on order 4 to a container with a valid part
   model, the target's **parts** stay obstacles (sphere detours around them toward the approach point); the root sphere
   is not an obstacle (the approach point is inside it by construction only for far-side ports). On order 5 unchanged
   (the engine owns the port path; the KC ignores the warn).
3. **Queued ships on 0x1d near their target container** are steered by the module for avoidance only (no point change,
   no speed change): detours around the container's parts and other ships, so the idle cruise does not walk into the
   station. 0x1d enters the steer set with `exclude_target=false` and the part obstacles of item 2.
4. **Carriers.** Items 1–3 apply with the carrier as B; the part model of a ship container is its hull parts via the
   same walk (`hull_width` already walks them); the carrier's velocity is added to the predicted part positions over
   the reach (the engine rebuilds the point from the live pose, so our prediction must move the parts with it).
   Undocking ships (0x1e) and ships inside 0.8 r_carrier are untouched (the engine skips their warns already).
5. **Diagnostics.** `ship_avoid_dock` row (1 Hz per ship on order 4/0x1d to a container, traced or not, bounded ring):
   container, class, part model valid, path clearance min, ray clearance min, verdict, zone; counters
   `e_suppressed_dock`, `e_pass_dock_*` by reason, `dock_part_obstacles`, `idle_steered`.
6. **Key** `ship_steering_dock` (default on); off = today.

## Fixture

Scenario N on the calibrated plant: a 3 km station with a 12-part model and a port facing (a) straight out, (b) 60°
tilted, (c) on the far side; a TP (r 81 m, 160 m/s) and an M6 approaching on order 4 from three bearings; the engine's
warn model extended with the box+ray+cone test of §11; a queue of three ships on 0x1d; a moving carrier (M1 at 60 m/s)
with a tube port and two docking fighters plus one undocking. Assertions: zero own-container engine warns on order 4
for ports (a)/(b) (the row is `suppress_dock` whenever the engine's test would fire and our path is clear); for (c) the
warn stays live only while the straight path crosses a part; no part contact (sphere) in any case; the 0x1d queue never
closes within 1.2 r_ship + 50 m of a part; key-off identical to today; cost ≤ 1 µs per decision for the row.

## Flight

Rows to read in the next flight: `ship_avoid_dock` per TP/TS approaching stations (Run157 TP shape); 0x12 against own
container on order 4 (target 0); `idle_steered`; carriers: any 0x12 of a fighter against its own carrier on order 4.

## Open

* Whether AI carriers carry bit 0x80000 (a row at `0x00459533`).
* How often the forward ray grazes real part boxes (the warn row filtered to own container measures it).
* The queue itself (0x1d walking into the station) is vanilla; item 3 only stops the contact, a holding loop is the
  gate controller's petal pattern applied to stations (later).

## Implementation

Built 2026-10-13 (Opus 5.5) on the stage-A branch of the gate traffic controller (`23184932`, with main's docs merged); not flown,
not installed. Ledger: [ship-steering.md](../verification/ship-steering.md), "Docking approach, amendment 18".

**Sources.**

* `ship_avoidance_core.h`: the row (`dock_reach_m`, `dock_clearance`, `dock_verdict`, `dock_container`, `DockE`), five E verdicts
  (`suppress_dock`, `pass_dock_parts|path|ray|zone`), `SiteE.dock`, and in the avoidance `Self.dock_target / dock_set / dock_cap_P`,
  `dock_part_clearance` and the body detour below. Every new field defaults to today's behaviour.
* `ship_steering.cpp`: the key, `dock_e` in site E, `dock_bind` in `avoid_adjust`, the carrier model, the row, the counters.
* The key in `schema.py` (generated `config_schema_inc.h`, `x3m.ini`); `ship_trace_rows.py` and its test; fixture scenario N.
* No new hook site (the verifier stays at 19 checks); reads only; game thread; no allocation.

**Key.** `ship_steering_dock` (`X3M_SHIP_STEERING_DOCK`), default 1, needs `ship_avoidance`. Refusals: `invalid_setting`,
`needs_avoidance` (set explicitly without the avoidance). Status `active`, or `active_no_e` when site E is not chained (the row is
then inert, items 2-4 still run). Off: the target excluded on order 4 as today, no row at E (key-off identity below).

**Site E (item 1).** The row sits after the controller rows, the gate rows, the lane row and both freshness checks, and before the
capital small-ship and member rows and the resolved set; the existing rows keep their order. It applies when A is on order 4, B is
A's `f+0x224`, B is a station (class 5/6) or a ship, A's decision is fresh, and, for a carrier, A is outside 0.8 r_C (inside: today's
rules). The exceptions of section 15 follow from the freshness test: the manual player ship, docked or attached ships and masked
classes have no steering decision, so the row never applies to them; a deleted or attached B gives `pass_dock_parts`. The DLL reads A live (position, flight basis row 2, speed, slip; the slip is dropped beyond 2 x the current speed because E has
no v_max), takes `a` from A's last decision (`Ship.a_lin`) and r from `obj+0xa4`. B's model comes from the station part walk over B's
phys node, keyed on object, root radius and pose (a moved carrier rebuilds; a missing or invalid model gives `pass_dock_parts`).
B's velocity is `p+0x100 + obj+0x60` for a ship, 0 for a station.

* Reach: the mesh test's `|v| + min(v_close^2/2a, 600 m)` (760 m for the TP). For a station v_close = |v|, which is not below the
  engine's. For a ship container it is the engine's closing speed, `|v| cos θ` plus the container's speed toward A when A heads at it
  within cos 0.8 (section 3.1), never below |v| (`dock_closing_speed`).
* A ship counts as a container only when it is dock-capable by the engine's own test, `[obj+0x40] & 0x100000` ("has a hangar or
  dock", section 5; the carrier speed-zero rule `0x0045951f..0x00459558` tests it together with the autopilot bit `0x80000`, which is
  a different bit). Any other ship target on order 4 keeps stage A: the target excluded, no E row, the absolute ramp.
* Path: `x + (u - w) t` over `reach / |u|`, against the current part centres. Ray: `x + f s`, s in [0, reach], against the current
  centres (the engine's instantaneous slab test). Both must clear every part sphere by `1.2 r + 50 m`; a NaN never suppresses.
* Final zone: never on order 4 (section 11.5: the zone is an order-5 phase, and order 4 ends at P, at least T/2 from the node). The DLL
  passes "outside"; the core keeps `pass_dock_zone` for a caller that knows the node. Scenario N asserts that no order-4 frame comes
  within 2 r + 50 m of the node.

**Avoidance (items 2-4).** `dock_bind` sets `Self.dock_target` on order 4 (a station; a carrier with a model, outside 0.8 r_C) and
on 0x1d (a station). In `resolve` the target is then kept by its parts (stations from the snapshot's model, a carrier from
`dock_set`); without a model the order-4 exclusion of today stands. A carrier's parts move at `v + ω × (c_p − C)`, with ω its world
angular velocity from its turn rates `obj+0x24..` and its flight basis (`core::ship_angular_velocity`, checked against the engine
model's rotation in the unit checks), capped at 0.5 rad/s; E's path test uses the same part velocities. A carrier's model is walked
at most every 250 ms into its own record (8 records) and extrapolated in between by the velocity and ω read at the walk; a failed walk
of a station or a carrier is not retried for 5 s. The station cache pins every slot the current snapshot references (by the snapshot's
epoch); with every slot pinned a new model gets none (the fallback) and `dock_parts_evicted` counts it. Choices beyond the note, each needed by scenario N:

* **The dock clearance.** A target part is kept at `max(C, r_p + 1.2 r + 50 m + 25 m)`. The plain C (`r_p + 1.5 r` for a TP) is below
  E's margin, so a path the avoidance accepts would fail E's row.
* **The horizon cap on order 4.** The target's parts count only up to the order's point (time to P along u), and only while u points
  within 10° of P: the ship stops at P, and the swept path beyond it runs into the station behind a straight-out port. Measured: the M6 head-on at port (a) takes 58.0 s
  with the cap and 91.2 s without it.
* **The body detour.** The first candidate being a target part, the detour point is built around the whole body: the sphere about
  B's centre enclosing every part, with the dock clearance, and the side from the path's closest approach to that centre. The
  conflict test stays per part. Per-part tangents aimed into the pockets between parts and the ships hit the station (far-side port:
  part contact -315 m before the change, +78 m after). The detour side is the approach point's side of the body (the note's "toward
  the approach point"). Heading straight at the centre, the plane of the nose and the centre is undefined and the detour axis would
  fall to the vertical; only then the plane runs through the approach point (the fighter dead ahead of a carrier: 1.65 x the engine's
  time without it, 0.99 x with it).
* **The line skip.** On order 4 to a stationary container, when the straight line to the approach point clears the whole body (the
  enclosing sphere) by the dock clearance, a part conflict on the current velocity later than the turn onto that line plus 1 s
  plus the velocity's 2 s lag is ignored: the engine flies at the point. A moving or turning container never gets it.

**The relative order-4 ramp (coordinator's fix, 2026-10-13).** For order 4 to a ship container under the key, the arrival ramp is
relative to the moving approach point:

* `decide()`: `v_ref = clamp(v_C · f, 0, v_max)`, with v_C the container's velocity (`p+0x100 + obj+0x60`) and f the ship's nose
  (`core::container_ref_speed`). The ramp written is `v_ref + sqrt(a (d - d_arr))`, lowered only and so never above the engine's own
  value (at most v_max).
* Site B: the ramp's distance is to the live point, the decision's point moved by v_C over the decision's age (`dock_w_` per table
  slot, at most 250 ms); the 10 m/s floor becomes a closing speed (`v_ref + 10 m/s`). Without it the ship held station at the carrier's
  speed just outside the 222 m arrival radius.
* A stationary container (v_C = 0, or the key off) gives today's clamp bit for bit (keyoff_identity.txt: 0 / 200,000).
* Order 5 is unchanged: the module writes no ramp there (the order has none), only the cornering cap, which only lowers, and amendment
  12's port hold, which writes angles. The engine owns the port path.

0x1d was already in the steer set with avoidance and without the target exclusion (stage A), so item 3 needed no steer-set change.
With the key, a 0x1d ship at a station gets the body detour and the dock margin; nothing changes for a ship whose station lies beyond
the avoidance's reach toward it (`keyoff_identity.txt`, question 3). Carriers' hull parts come from the same station walk over the
ship's phys node; that this node layout validates on real carriers is **[i]** (the row's `parts=`/`n_parts=` settle it).

**Diagnostics (item 5).** `ship_avoid_dock` (--debug, 1 Hz per ship on order 4/0x1d to a container, ring 32, drops counted):
`ship cont class order parts n_parts path_m ray_m margin_m reach_m e zone d_c_m r_c_m v_ms kept detour`. `e` is what E's row would
return now, `-` off order 4 or inside 0.8 r_C. `zone` is out/near/in/inner (beyond 2 r_C, beyond r_C, within r_C, within 0.8 r_C of
a carrier); the note's zone has no order-4 meaning (above). Summary: `dock= e_suppressed_dock e_pass_dock_parts|path|ray|zone
dock_part_obstacles dock_detours idle_steered (0x1d within 5 r_C) dock_models_built dock_no_model dock_rows_dropped dock_e_us_max`.

**Scenario N** (fixture; 9 runs per line, 30/60/144 fps x 3 seeds; key on vs key off on the same starts):
[`scenario_n.txt`](../../verification/results/ship-steering/docking-approach/scenario_n.txt). The parts are boxes; each sphere is
its box's diagonal, so it contains the box. Bearings 0/60/120 deg from +z (the port side of a and b, the far side of c).

| Case | Key on: own-container warns fired / suppressed / live, 0x12 legs, closest part gap | Key off: live warns = 0x12 legs, closest gap |
| --- | --- | --- |
| (a) TP, M6 at 0 and 60 deg; M2 at all three | 0 / 0 / 0, 0, >= 526 m | 0, same paths |
| (a) TP / M6 at 120 deg | 0 / 0 / 0, 0, 357 / 704 m | 9 / 9, -79 / -77 m (contact) |
| (b) all | 0 / 0 / 0, 0, >= 1,062 m | 0, same paths |
| (c) TP at 0 deg | 51 / 47 / 4 (all `pass_dock_path`), 4, 78 m | 9, -210 m |
| (c) M6 at 0 deg; TP / M6 at 60 deg | 0 / 0 / 0, 0, 344 / 357 / 704 m | 9 each, -246 / -79 / -77 m |
| (c) TP, M6 at 120 deg | 0 / 0 / 0, 0, >= 1,626 m | 0, same paths |
| queue 0x1d (TS, TP, M6, 180 s) | margin left 335 / 449 / 740 m, no 0x12 | -645 / -646 / -47 m, 26 legs |
| carrier 60 m/s (M3 tube port, M4 side port, M5 launches) | 18/18 arrive, worst 0.99 x the engine's time, 0 fired, closest part 726 m | 0/18 arrive (stage A's ramp), 582 m; engine alone 18/18 in 37.0 s |
| carrier 120 m/s | 18/18, worst 0.99 x, 0 fired, closest 482 m | 0/18; engine alone 18/18 in 73.4 s |
| carrier 60 m/s turning 9 deg/s (its cap) for 180 deg | 18/18, worst 0.90 x, 0 fired, closest 118 m | 0/18, 22 legs, -367 m; engine alone 27 legs, -137 m |
| carrier without the dock bit (key on vs off) | identical in 9 of 9 runs | |

* Asserted (all pass): every station-docking ship reaches its approach point; E suppresses whenever the engine fires and the path and the ray are
  clear (live-with-clear frames 0 everywhere); zero live own-container warns for (a)/(b); no part contact for the key on; no order-4
  frame in the final zone; the 0x1d queue never inside 1.2 r + 50 m of a part. Carriers (each fighter holds its own port, as the KC
  reserves one per ship): every fighter reaches its moving approach point within the engine's own time + 10 % (the same start flown
  on the engine's leaf and speed law alone), no part contact, no live warn with a clear path.
* The carrier's E row never fired with the key on (the detour kept the fighters clear), so the carrier row is covered by the unit
  checks only (parts moving at the carrier's velocity or swept by its turn close the path, the inner 0.8 r_C exception).

**Cost** (host x86-64 -O2, measured; i686 not measured): the row's arithmetic (clearance + verdict + `site_e_verdict`) 64 ns on 12
parts and 282 ns on 64; the model's cache lookup (stamp hash + find over 64 slots) 51 ns. The DLL adds about 30 reads. A moving
carrier's model is walked at most every 250 ms and extrapolated in between (not measured here; `dock_e_us_max` reports E in
flight).

**Open.**

* Sphere contains box is **[i]** for the real walk: its part radius is the per-axis subtree radius, not a box diagonal. A part whose hull
  pokes beyond its sphere could be suppressed against. The row's `path_m`/`ray_m` beside the engine warn row of an own-container
  0x12 in flight would show it.
* The carrier part walk on a ship's node tree, and how often a real carrier's model validates (`dock_no_model`).
