# Sector-level path planning: is it worth it, and what it would be

2026-10-12. Design note for item 3 of the ship-navigation work (a planner that routes an AI or autopilot
ship around stations, asteroid fields and other large obstacles before it flies). Items 1 and 2 (smooth
steering at the steering leaves, predictive per-call avoidance that detours the target point) are a
separate note; this note assumes that layer exists at hook site A of
[ship-movement.md](../reverse-engineering/ship-movement.md) §11 and can accept a short list of waypoints.
Inputs: [ship-navigation-ai.md](../reverse-engineering/ship-navigation-ai.md) (order model, reactive loop),
[ship-movement.md](../reverse-engineering/ship-movement.md) (steering leaves, re-plan cadence),
[game-scale-and-ship-classes.md](../reverse-engineering/game-scale-and-ship-classes.md) (500 units = 1 m,
sizes, dynamics), [sector-collide.md](../reverse-engineering/sector-collide.md) §1–2 (bucket table
`[sector+0x50]`), `src/proxy/engine_patch.h`. New evidence: the line-blockage computation over the stock
map, [`line_blockage.py`](../../verification/results/sector-path-planning/line_blockage.py) (outputs beside it;
derived numbers only, 1 min 42 s). Nothing was launched. **[m]** measured (from the cited note or this
script), **[i]** inferred.

**Verdict.** Not worth building a sector planner now, and a visibility-graph, grid or sampling planner
is not worth building at all on these numbers. In the stock map a straight in-sector leg passes within
clearance of a station body on 4–12 % of legs (oriented-box bodies, clearance 100–1000 m) **[m]**, but a
one-waypoint tangent detour, which is exactly what the predictive local layer of item 2 produces, resolves
all but 0.2–1.5 % of legs, and the full route is only 35–115 m longer than the straight line at the median
(p90 0.25–0.8 km, under 2.5 % of the leg) **[m]**. The cases that remain are two-obstacle chains and the
14 giant Terran/Aldrin station types. The right shape for them is a *leg check* inside item 2's layer: test the
whole remaining leg against the static bodies once per order, insert at most two or three tangent
waypoints, keep the chosen side (§3). It needs no new patch site, costs tens of microseconds per order
**[i]**, and should be built only after items 1 and 2 are measured in flight and the diagnostic of §4.4
still shows station entanglement.

## 1. Where ships get stuck or route badly today (Q1)

| Case | Mechanism today **[m]** unless marked | Planner? | Local predictive layer (item 2)? |
| --- | --- | --- | --- |
| **Single-point order plus reactive avoidance** | One goal point per order (`flight+0x240`), steered straight at every re-plan (0–50 ms). The warn test fires at braking distance plus a margin of at most 1 km plus the two radii; `CollisionWarn` stops the order, the KC wrapper issues the native avoid order (0x12), which samples escape directions every 200 ms and ends once the obstacle list is empty and the ship is 300 m (`rA + 150000`) from where it started; the wrapper then re-issues the *same* straight goal. If the obstacle still lies on the line the loop repeats: a stop/avoid/resume cycle whose progress depends on the random escape yaw (`+0x264`) and the escape scoring, with the desired speed dropped to the obstacle's speed or the distance map each time **[i]** (consequence of the mechanism; not observed in flight). | No. For one convex obstacle the planned route *is* the tangent detour (§2: 96–99 % of blocked legs need one waypoint). | **Yes.** A detour of the target point that persists until the obstacle is passed, with the warn suppressed at `0x0044dd90` for the resolved pair, removes the cycle. |
| **Station bounding spheres vs physical shapes** | `[obj+0xa4]` is a loose sphere (L∞ offsets plus child radii): factories 0.5–2.4 km, shipyards 1.8–52 km, bases up to 52 km. The engine's warn test sends class 5/6 obstacles to the mesh test (`0x0044d9c0`) instead of the cone (whether the close-range sphere rule precedes it is open, §8), but the avoid order's escape scoring and must-turn-away rule use the sphere inflated by 1.2, and Mayhem's `Bounce` pushes a warned ship away by `SA_Distance` vs `GetSize + 500 m`. The box model of §2 shows the sphere over-blocks straight legs by 1.5–1.8× (typical stations, 250 m clearance: 9.8 % vs 5.3 % of gate-to-gate legs). | No: this is an obstacle *representation* problem, identical for a planner and for the local layer. | **Yes, provided it uses the body** (child-node spheres or part boxes, §3.1), never `[obj+0xa4]`, for stations above ~1.5 km. |
| **Asteroid fields** | Stock: 6 per sector, 2 km radius, 10 km spacing; a gate-to-gate leg meets one on 5.8 % of legs at 250 m clearance, one-waypoint detour extra 32 m median **[m]**. Mayhem: 194 per sector (max 732), 60 m median radius (p90 1.1 km), 1.6 km nearest-neighbour; a gate-to-gate leg meets one on 3.8 % of legs at 250 m, 4.7 % at 1 km; two or more waypoints on 1.1–2.7 %; extra 67–118 m median, 0.5–0.9 km p90 **[m]**. | Marginal: the chained cases are 1.1–2.7 % of legs and cost under a kilometre. | **Yes.** Rocks are small convex spheres spaced 8× the clearance; the local layer passes between them. A density grid is unnecessary at 200–700 rocks (§3.1). |
| **Capital ships whose turning circle is smaller than their length** | M2 radius 0.7–1.3 km against a turn radius of 0.3–0.6 km (Mayhem) or 0.5–1.5 km (stock); 180° takes 13–33 s (Mayhem) or 37–115 s (stock) **[m]**. The engine warns early enough by centre distance (margin up to 1 km plus `rA + rB`), but the avoid order re-samples the escape direction every 200 ms while the ship turns at 3–13°/s, so a capital can be re-aimed before it has turned **[i]**. The hazard is late or flip-flopping *side* selection, not path length. | Partly: choosing the side once at order time, with the whole body known, is what a planner does. | **Yes, if the layer evaluates the whole remaining leg once and keeps the side** (hysteresis). That is the leg check of §3, which belongs in the local layer rather than in a separate planner. |
| **Formations** | Followers fly escort order 0x14 to a slot in the leader's frame (`4·r` spacing: fighters 37–60 m, M6 258 m, an M2 2.8 km) re-rotated every re-plan; a leader's detour drags the slots through the obstacle when the slot offset exceeds the clearance. Escort pairs halve the braking distance in the warn test, so they do not warn on each other early. | Only through clearance inflation: the leader's clearance grows by the largest follower slot offset. | **Same inflation, same cost**: followers' slot points can be detoured individually, but they leave formation while doing so; inflating the leader's clearance (scan the bucket for escorts whose `flight+0x224` is the leader, take max \|`flight+0x240..`\|) is cheaper and keeps the formation. |

So four of the five cases are solved by item 2 with the right obstacle representation; the fifth (side
commitment for capitals) is solved by a whole-leg check that is a few hundred lines inside item 2, not a
planner.

## 2. How often a planned route differs from "straight line plus local detour" (Q2)

### 2.1 What was computed **[m]**

`line_blockage.py` reads the stock AP universe map (`addon/02.cat`, 239 sectors, 1794 placed stations of
352 types, 557 gates) and the installed Mayhem 4 map (247 sectors, 54,990 asteroids, no static stations)
and, per sector, forms three families of straight legs: **gate→gate** (transit, 469 stock / 529 Mayhem),
**gate→station** (arrival, 4476) and **station→station** (trade runs, 8059). The endpoints' own stations
are not obstacles for their leg. Three station models, all from the scene files and the body records that
`scene_radius.py` already resolves:

* **sphere**: the engine's `[obj+0xa4]` estimate (validated to the unit on one station in the game-scale note);
* **body**: one sphere per scene part, radius = the part body's LOD-0 L∞ half-extent, centred at the
  part's accumulated local position rotated by the map's `a/b/g` angles (the engine's angle-to-matrix
  order is not traced; a no-rotation variant differs by at most 0.3 points, so the reading does not depend on it);
* **box**: one axis-aligned box per part in station space from the LOD-0 vertex extents (bob1 points ×
  record value / 65536), rotated into the world: an oriented box. Part rotation inside the scene is not
  modelled. Evaluated only for leg/station pairs the sphere blocks, by sampling the sub-segment inside the
  sphere at ≤ 50 m steps.

Asteroids are spheres of the root body value. Clearance C is added to every obstacle; a leg is *blocked*
when the segment passes within C of a model. For blocked legs the script applies one tangent waypoint
around the deepest blocker (the one-step local detour) and then the same step recursively on any still
blocked sub-leg (depth ≤ 6): the waypoint count a local detour would have to grow into, and the extra
length of the resulting route. "Typical" drops the 14 station types with a bounding sphere above 15 km
(Aldrin bases 52 km, Military Base 30 km, Orbital Defence Stations 18–22 km, Terran docks 16–22 km; 36
placements) as obstacles and endpoints; they are one or two monolithic 10–52 km bodies whose L∞ sphere is
not a usable physical model either way (§8).

### 2.2 Results **[m]** (percent of legs; stock map, typical stations)

| C | legs | sphere-blocked | body-blocked | **box-blocked** | needs 2+ waypoints | second station blocks the one-step detour | recursion fails | route extra, median / p90 / max | extra as % of leg, median / p90 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 100 m | gate→gate 469 | 9.0 | 7.0 | **5.1** | 0.2 | 0.2 | 0 | 46 m / 238 m / 421 m | 0.1 / 0.4 |
| 100 m | gate→station 4370 | 7.4 | 6.4 | **4.6** | 0.4 | 0.3 | 0.05 | 55 m / 376 m / 948 m | 0.1 / 0.8 |
| 100 m | station→station 7822 | 6.6 | 5.8 | **4.0** | 0.5 | 0.2 | 0.03 | 69 m / 560 m / 1.6 km | 0.2 / 1.8 |
| 250 m | gate→gate | 9.8 | 7.9 | **5.3** | 0.2 | 0.2 | 0 | 39 m / 242 m / 461 m | 0.1 / 0.4 |
| 250 m | gate→station | 8.4 | 7.5 | **5.3** | 0.5 | 0.4 | 0.09 | 49 m / 404 m / 1.0 km | 0.1 / 0.8 |
| 250 m | station→station | 7.3 | 6.4 | **4.7** | 0.6 | 0.2 | 0.05 | 78 m / 600 m / 1.8 km | 0.2 / 1.9 |
| 500 m | gate→gate | 12.4 | 10.7 | **8.1** | 0.4 | 0.4 | 0 | 35 m / 254 m / 533 m | 0.1 / 0.5 |
| 500 m | gate→station | 9.8 | 8.8 | **6.8** | 0.7 | 0.6 | 0.09 | 55 m / 437 m / 1.1 km | 0.1 / 1.0 |
| 500 m | station→station | 8.5 | 7.5 | **6.0** | 0.8 | 0.4 | 0.09 | 85 m / 642 m / 2.0 km | 0.2 / 2.1 |
| 1000 m | gate→gate | 16.4 | 14.9 | **12.2** | 0.9 | 0.9 | 0 | 44 m / 350 m / 700 m | 0.1 / 0.6 |
| 1000 m | gate→station | 13.5 | 12.5 | **10.1** | 1.3 | 0.9 | 0.09 | 65 m / 544 m / 1.4 km | 0.2 / 1.2 |
| 1000 m | station→station | 11.1 | 10.0 | **8.7** | 1.5 | 0.8 | 0.14 | 114 m / 768 m / 2.4 km | 0.3 / 2.3 |

C = 100–250 m covers fighters and freighters (own radius 10–100 m plus margin), 500 m corvettes and
frigates, 1000 m capitals (own radius 0.7–1.3 km, margin `0.5·r_own`). The waypoint and extra-length
columns use the body model (conservative: it blocks 1.1–1.5× more legs than the box).

With the giant types included (all stations): box-blocked rises by about 2 points (gate→gate 7.5 % at
250 m), recursion fails on 0.8–2.1 % of legs, and the one-step extra-length tail reaches 12–34 km. Those tails are
the Aldrin bases and the Earth Torus modelled as solid 37–52 km spheres; they say the *model* is wrong
for hollow or ring-shaped bodies, not that routes there are 30 km longer.

Asteroids, gate→gate legs: stock 5.5 / 5.8 / 6.8 / 10.0 % blocked at C = 100 / 250 / 500 / 1000 m, 2+
waypoints 0.6–2.4 %, extra median 21–50 m; Mayhem 3.6 / 3.8 / 4.0 / 4.7 % blocked, 2+ waypoints
1.1–2.7 %, extra median 67–118 m, p90 0.46–0.91 km.

### 2.3 Reading

* A planned route differs from the **straight line** on 4–12 % of legs (box model), so a layer that does
  nothing until the warn fires is wrong a tenth of the time; that is item 2's job.
* A planned route differs from **straight line plus one local tangent detour** on 0.2–1.5 % of legs
  (two or more waypoints needed, or a second station blocking the detour), and even then by hundreds of
  metres on legs of 20–60 km. Over a 58 km median gate-to-gate leg that is seconds of flight at any class.
* Mayhem's asteroid density does not change the picture: 194 rocks of 60 m spaced 1.6 km leave 8× the
  clearance between them, and only 4 % of transit legs touch one.
* Mayhem station spacing cannot be computed statically (stations are placed at game start). The stock
  spacing (closest pair per sector median 10.2 km, 9 stations per sector) is the stand-in **[i]**; Mayhem
  sectors are 15 % larger (46 vs 40 km) and the Outpost model likely places fewer stations per sector, so
  blockage there is expected at or below the stock figures **[i]**.

**Conclusion:** worth building *later*, in the small form of §3, after items 1 and 2 are measured in
flight; the full planner is not worth building.

## 3. The design, if and when it is built (Q3)

The recommended form is a **leg check**: at the first steering call of a new point order the layer tests the
whole remaining leg against the sector's static bodies and, when blocked, inserts tangent waypoints by the
recursion of §2.1 (depth ≤ 3, so at most 7 waypoints, normally 1–2), then hands the current waypoint to
the steering leaves. It is the one-step detour of item 2 applied to the whole leg instead of the lookahead
window, with side hysteresis.

### 3.1 Obstacle representation

Taken from the **live sector** at plan time: the bucket lists at `[sector+0x50]` (32 classes, stride
`0xc`; next pointer at `[obj+0]`, class word `[obj+0x48]`, position `[[obj+0x70]+0x30..]`, radius
`[obj+0xa4]`) **[m]**. Classes: 5 and 6 (stations), 17 (asteroids), 18 (gates other than the leg's own
target), 28 (debris) **[m]** classes, **[i]** that the set is complete for "static". Ships are not planned
around; they move and belong to item 2.

* **Stations: union of child-node spheres**, not the root sphere. The render node of an object is
  `[obj+0x70]`; its child list head is `node+0xc` with intrusive `+0`/`+4` links; each child carries a
  local offset `+0x30/+0x34/+0x38` and the cached subtree radius `+0xa4` (`0x00488170`) **[m]** fields,
  **[i]** that children's `+0xa4` is populated after the root's lazy computation. World centre = station
  position + station basis (`p+0x40`, three 16.16 rows) × child offset. The stock scenes have a median of
  4 parts per placed station (p90 15, max 140) **[m]**, one child node per part **[i]**. Oriented boxes from the RAPID BVH root
  (`[model+0]`, centre `+0x24`, half-extents `+0x30`, rotation `+0`) are the refinement if the child
  spheres prove too fat for a specific family: §2.2 says the box model blocks 13–32 % fewer legs than the
  part-sphere body, so the refinement is worth a measurement, not a first version. Use the root sphere
  only as the broad-phase candidate filter.
* **Asteroids: individual spheres** (`[obj+0xa4]`). At 200–700 rocks a segment test costs 2–7 µs **[i]**
  (§4), so a density grid buys nothing and would hide the 1 km rocks that matter.
* **Gates**: spheres (1.1–3.2 km) except the leg's target gate.
* Clearance per ship: `C = r_own + m`, `m = max(25 m, 0.5·r_own)` (game-scale §10), inflated for a
  formation leader by the largest escort slot offset among followers in the bucket (§1).

### 3.2 Planner

Recursive tangent insertion: `plan(P, Q)`: find the deepest blocker of segment PQ among the obstacle
set (segment–sphere distance, SSE2 scalar); none → done; else `T = centre + m·(ρ + C)·1.05` with `m` the
unit vector from the centre to the segment's foot point (the near side), recurse on PT and TQ to depth 3.
Deterministic, no allocation, no graph. It does not find the shortest route around a concave cluster; §2
says that costs hundreds of metres on under 1 % of legs. Failure (still blocked at depth 3) leaves the
original point in place and the local layer and the engine's reactive loop take over: fail closed to the
present behaviour.

### 3.3 When it runs

Lazily at site A, not at the `SA_` case. The steering hook already sees `(obj, point)` with the order word
`flight+0x258` at every re-plan; when the `(obj, flight, order, point, radius)` key differs from the
cached entry it is a new order and the leg is planned on that call. Advantages over a hook at
`0x0046232c` (the MovePos case): no new patch site, it also catches the goal re-issued by the KC wrapper
after an avoid order, and it needs no save-game state (after a load the first steering call re-plans)
**[i]** design reasoning. Orders planned: 0x19 MovePos and 0x23 MovePosContinue only. Not planned: attack
0x10/0x11/0x18, defensive 0x22, escort 0x14, follow 0x15/0x16, docking 4/5, undock 0x1e, gate 8/0x21,
avoid 0x12, idle 0x1d, turn/rotate 6/0x1f (per ship-navigation-ai §6 and ship-movement §11). The far
docking legs that `__LandInHangar` issues as MovePos are planned like any other; the final approach is
order 4/5 and untouched.

Re-plan: when the local layer's detour of the current waypoint exceeds a threshold (2·C, meaning a moving
obstacle or a station built since the plan) or every 10 s per ship, whichever first. Both are cheap (§4).

### 3.4 Where the route lives

A proxy-side table in DLL globals (never in the `engine_patch` arena, which is `PAGE_EXECUTE_READ` on
native Windows, [platform-portability.md](platform-portability.md)): 2048 open-addressed entries keyed
by the object pointer, each holding the key above, ≤ 7 waypoints (12 B each) and the current index, about
192 B per entry, 384 KB total, allocated once **[i]**. The flight block cannot grow and the engine has no
waypoint list, so nothing is written into engine memory. A reused object pointer cannot alias: the key
includes the target point and order word, so a stale entry mismatches and is replaced. Entries are
overwritten on collision (the ship simply re-plans) and the table is reset on sector change of the
player's focus and at device reset.

### 3.5 How the steering layer consumes it, and why scripts never notice

At each site A call for a planned order the hook replaces the point passed in `EDX` (its own stack copy;
never `flight+0x240`, which the order layer reuses as state) by the current waypoint, and advances the
index when the ship is within `max(2·r_own, R/4)` of it (game-scale §10 arrival rule) or has passed its
plane. The **last leg uses the original point**, so the engine's own arrival test
(`dist <= flight+0x260` at `0x00456576`), its speed ramp (distance to the original point, so the ship
cruises through waypoints at full speed) and the completion callback (`0x00460560`, result 1) are
untouched. The order word, `[obj+0x7c]/[obj+0x80]` and the KC wrapper's loop see exactly what they see
today: the order ends only when the engine says it has arrived. Scripts cannot observe the route.

### 3.6 Native Windows behaviour

Shared code only: the planner reads engine memory on the game thread and writes DLL globals; the hook is
item 1/2's byte-verified entry trampolines (fail closed, restorable), with no additional site. SSE2 math
compiled `-msse2 -mfpmath=sse`; the leaves use x87 and the stub must leave the x87 stack and control word
as found, which C++ compiled with SSE math does unless it calls a CRT routine that uses x87 (none needed:
sqrt via `_mm_sqrt_ss`). No Windows API on the hot path. The arena-write rule of
platform-portability.md holds (counters in globals). Native execution is unverified until the user runs
it; the fixture of §4.4 is what distinguishes cross-compiled from verified.

## 4. Cost (Q4)

| Item | Estimate | Basis |
| --- | --- | --- |
| Segment–sphere test | ~10–15 ns | ~20 scalar flops, SSE2 **[i]** |
| Obstacle set, stock sector | ~9 stations × 4 child spheres (p90 15) + 6 asteroids + 2–6 gates ≈ 50–150 spheres | map statistics **[m]** |
| Obstacle set, Mayhem sector | same stations (unknown count) + 194 asteroids (max 732) ≈ 250–900 spheres | map statistics **[m]**, station count **[i]** |
| One segment check | 1–15 µs | product of the two rows **[i]** |
| One leg plan, typical (1–3 segments) | 3–45 µs | **[i]** |
| One leg plan, worst (depth 3, 15 segments, 900 spheres) | ~0.2 ms | **[i]** |
| Plans per second | tens: an order lasts 1–10 min of flight (gate-to-gate 2–10 min by class); 200 ships re-ordered within a few seconds after a load is the burst case | **[i]** |
| Waypoint advance | 200 ships × ~40 steering calls/s (re-plan 0–50 ms, reaction delay 1 on 521/521 TShips rows) = 8000 lookups/s × ~60 ns ≈ 0.5 ms/s ≈ **8 µs per frame** at 60 fps | **[m]** cadence, **[i]** cost |
| Memory | 192 B per entry, 384 KB static | §3.4 **[i]** |

Complexity: O(S·2^d) segment tests with S = obstacle count and d ≤ 3, so O(8·S) worst; no graph. For
reference the whole command pass `0x00452ad0` was 32–46 µs per frame and the collision pass 0.4–0.7 ms
off its plateau in run 129 (sector-collide §11), so a 0.2 ms worst-case plan is one frame's worth of
command pass, once per order. **Run it on the game thread, synchronously, with a per-frame budget of 4
plans**; ships beyond the budget fly straight for one more frame and plan on their next steering call. No
worker thread: the obstacle set lives in engine memory that the game thread mutates, and copying it for a
worker would cost more than the plan. No amortisation across frames is needed below the budget.

### 4.4 Verification that would prove it

1. Host test: the C++ planner on the stock-map legs of `line_blockage.py` reproduces the blocked-leg and
   waypoint-count columns of §2.2 within rounding (golden file; derived numbers only).
2. Fixture: synthetic sectors of random spheres and legs; every produced route keeps ≥ C from every
   obstacle on every segment, ends on the original point, and the planner never exceeds the stated time
   per plan at 900 obstacles (measured, not assumed).
3. Flight, one consolidated opt-in diagnostic on the existing SiteSpec infrastructure (counters and
   thresholded rows, no per-call logging): per ship, orders planned and waypoint counts; `CollisionWarn`
   dispatches per ship-minute at `0x0045e089`; avoid-order entries (order word 0x12 at site A); stuck time
   (speed < 10 % of `0x00450980` with an active point order); route extra length. Compared A/B on the
   same save with the leg check off and on, after items 1 and 2 are already in. The leg check is worth
   keeping if the warn rate or stuck time falls beyond item 2 alone; the §2 numbers predict a small
   difference, which is why the measurement comes first.

## 5. Risks (Q5)

| Risk | Assessment |
| --- | --- |
| AI jobs that rely on straight-line timing (trade run timers, mission timeouts) | Route extension is 35–115 m median, ≤ 2.3 % of the leg at p90 (§2.2): seconds per leg. Out-of-focus ships use the KC `__galaxyFlightStep` integrator and are never planned, so galaxy-wide timing is unchanged. Escort/follow/defensive timeouts (`+0x290`) are on orders that are not planned. |
| Combat orders | Excluded by order word (§3.3). Attack runs keep their ring offsets and random manoeuvres. |
| Player autopilot | Same MovePos orders, so the autopilot would follow planned legs; a visible deviation of a few hundred metres around a station is the intended behaviour, but it must be a launcher option (default on for AI, selectable for the player's ship via `[obj+0x40] & 0x80000` plus the player-ship test) so a user can compare. |
| Gate approach alignment | `__EnterHyperspace` uses orders 4 (approach point), 0x1f (align) and 5 (gate axis), none planned; the gate itself is excluded from the obstacle set for its own leg. The MovePos leg before the gate ends at the engine's own arrival radius, as today. |
| Formations following a planned leader | Followers keep slot-keeping (order 0x14, unplanned); the leader's clearance is inflated by the widest slot (§3.1). Residual: followers on the inside of a tight turn still cut the corner; item 2's per-follower detour is the backstop. |
| Interaction with the reactive `CollisionWarn` loop | A planned waypoint keeps the ship ≥ C from the body, and the engine's station warn is the mesh test along `|v| + b` (braking distance ≤ 600 m), so a tangent route rarely warns **[i]**. If it does, the loop runs as today and the KC wrapper re-issues the goal, which the lazy plan recognises (same key) and continues. Never set `Flags2 0x20000` or `[obj+0x40] 0x8000000` to silence it. |
| Mayhem places stations at game start; stations are built during play | The obstacle set is read from the live buckets at plan time and refreshed by the 10 s re-plan; no static map data is used anywhere (§3.1). |
| Giant hollow bodies (Earth Torus, Aldrin bases, Terran orbital stations) | Child-sphere bodies are wrong for a 37 km ring; the leg check treats them as the engine does today (no plan when the recursion fails) until an OBB or mesh body exists for those few types. |
| `SA_GetSafePos` (called by `__MovePos` before every MovePos) | Semantics unread; if it already nudges the target off a station the plan's final point is that nudged point, which is harmless. Open in §8. |
| Save games | No state is persisted; routes are rebuilt from live orders after a load. |

## 6. The decision set (Q6)

1. **Recommended: defer.** Build items 1 and 2 with the body representation of §3.1; add the §4.4
   diagnostic; decide on the leg check (§3) from the A/B numbers. Cost now: zero. Cost later if needed:
   a few hundred lines behind the existing hook, no new patch site.
2. **Build the leg check together with item 2.** Same code, one less flight cycle, but it ships before
   its need is measured; acceptable if the user prefers fewer launch cycles over evidence.
3. **Full planner** (visibility graph, octree A* or RRT, worker thread, per-ship routes): not
   recommended. Benefit on 0.2–1.5 % of legs, cost of weeks and of a second obstacle representation; §7.
4. **Nothing beyond items 1 and 2.** Defensible on the §2 numbers; loses the side-commitment case for
   capitals and the chained cases, both rare.

## 7. Alternatives considered and why they lose

* **Visibility graph over obstacle tangents.** Exact in 2D; in 3D sphere tangents are circles, so the
  graph needs sampled nodes (8–14 per sphere, 50–150 spheres → ~1000 nodes, ~500k candidate edges, each a
  segment test against the set): 0.1–1 s per plan on the game thread **[i]**, or lazy A* with the same
  code size as the whole leg check. The shortest-path optimality it buys is worth hundreds of metres on
  under 1 % of legs.
* **Sparse grid / octree A\*.** A 46 km sector at 250 m cells is 6×10⁶ cells; an octree over 50–150
  bodies is small but needs rebuilding when a station appears and makes diagonal routes staircase without
  smoothing. Cost and code for no measured benefit.
* **RRT.** Non-deterministic routes (two ships with the same order fly different paths), needs smoothing,
  and its strength (high-dimensional or cluttered spaces) is absent: the free fraction of a sector is above
  99 %.
* **Asteroid density grid.** Replaces 200–700 cheap sphere tests with a grid that cannot distinguish a
  60 m rock from a 1.1 km one; the only expensive case (732 rocks, 7 µs per segment) is still cheap.
* **Hook at the `SA_MovePos` case (`0x0046232c`).** A new patch site that sees the command once and
  misses nothing the lazy key does not; it would need its own re-plan trigger for the goal re-issued
  after an avoid order and would plan before `SA_GetSafePos`'s result is known.
* **Worker thread with a hand-off.** The obstacle set is engine memory mutated by the game thread; a
  snapshot per plan costs more than the plan (§4), and the hand-off adds a frame of latency and a
  lifetime hazard on object pointers.
* **Routes stored in the flight block.** It cannot grow; the engine's save writer (`0x00478690`) would
  not persist them; and `flight+0x240` is reused as order state.

## 8. Not established, and what settles it

| Open point | Settles it |
| --- | --- |
| Whether stations warn by the close-range sphere rule `d <= 1.2·(rA+rB)` or by the mesh test when both apply in `0x0044dd90` (ship-navigation-ai §3.1 lists both for class 5/6). If the sphere rule wins at close range, any route tangent to a 4.5 km shipyard body still warns and the clearance must be the sphere's. | One Ghidra pass over `0x0044dd90` (branch order before `0x0044ded9`), or the §4.4 diagnostic row (warn with `d`, `rA`, `rB`, class). |
| `0x0044d9c0` (mesh warn test) internals: what "mesh" it tests and with which margin. | Decompile `0x0044d9c0`; its answer bounds the clearance of §3.1. |
| `SA_GetSafePos` semantics. | Its `SA_` case from the name table at `0x0057a7f0` and the dispatcher jump table `0x004690f0`. |
| Child-node `+0xa4` population on stations before any collision, and the node basis convention for child offsets. | One-shot log of `(obj, child offset, child +0xa0/+0xa4)` per station in the player's sector (same census as game-scale §11). |
| Real in-flight frequency of the §2 cases (the map says legs; jobs decide which legs are flown, and `GetSafePos` may move endpoints). | The §4.4 diagnostic, row 1 (orders planned, blocked fraction, waypoints). |
| Mayhem station counts per sector. | Runtime census of class 5/6 per sector, or a save parser. |
| Engine angle-to-matrix order for the map's `a/b/g` (the body model's rotation). | `0x004f0270` argument order; the no-rotation variant shows the §2 numbers do not depend on it. |

## Reproduce

```sh
cd /home/asvetl/x3-mod
python3 verification/results/sector-path-planning/line_blockage.py   # line_blockage.{txt,json}; about 100 s
python3 verification/results/game-scale/map_stats.py                 # the map statistics it builds on
```

The script imports `map_stats`, `scene_radius` and `tools/analysis/bob1`; it reads `/mnt/c/X3` through
`sector_fog_census.Assets` (stock view `STOCK_AP_CATALOGUES`). No game bytes are written.
