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
