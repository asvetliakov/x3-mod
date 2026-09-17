# Sun-shadow casters the engine did not submit: retention by node

Design note for ratification, written 2026-09-17 after the run 38 A diagnosis
([../verification/directional-shadows.md](../verification/directional-shadows.md), "Run 38 A
(run111) diagnosis", cause 3). Extends [shadow-cascades.md](shadow-cascades.md) §1 (one record
list, per-cascade masks and caps) and [shadow-replay-gates.md](shadow-replay-gates.md)
("Casters by bounds", the geometry lease). Nothing here is implemented. It assumes the
diagnosis' fixes 1 and 2 are in (one validated frame sun from the bound program's own
`LightDir_Dir0`; an extent cache that does not thrash) and the N-cascade replay exists;
retention is built on those, not instead of them.

## Problem

The maps are replayed at scene end from the frame's recorded z-writing managed draws. The
engine culls per node against the view: 7.1° of camera turn removes 64 of 127 submitted
nodes, 24° removes 89 of 126. A caster outside the view (a station arm behind the camera,
between the sun and the ship) is never drawn, so its shadow disappears when the camera turns.

## Decision

**Proxy-side retention keyed by the verified node lifetime, no engine trampoline** (option C:
A seeded by what was once visible). The unit of retention is the **node**, not the draw:

- Identity: `(load_epoch, registry_epoch, node_serial)` from the lifetime observer
  ([object-lifetimes.md](../reverse-engineering/object-lifetimes.md)); the route key already
  carries it on every routed draw. Payload per draw of the node: VB/IB/declaration references,
  the draw range, cull mode, the object-space AABB (extent cache) and the **object → world
  rows** `W·A` that `shadow_replay_light_rows` already computes in double from the submitted
  clip rows and the frame's camera latch. VB/IB/range is *not* an identity (measured below:
  90 of 297 keys are shared by up to 14 nodes); world rows are payload, not key.
- **A node submitted this frame supersedes its whole retained set** with this frame's draws.
  A LOD swap (same node, other mesh) therefore replaces, never adds: no double caster by
  construction. A node not submitted this frame replays its retained set with
  `light_rows = S(sun basis, cascade) · W_retained`; no camera enters that product.
- Only **static** nodes are retained beyond the frame they were last seen (stage 2). Static =
  recovered world rows of every draw of the node agree with the previous sighting within
  `eps` (0.05 units at the AABB corner) for 8 consecutive sightings. Moving nodes are dropped
  when unseen (staleness 0, shadow pops as today) until stage 3 decides otherwise.
- A retained node leaves the store on: node retirement or any epoch change (lifetime
  observer; checked only when `mutation_revision` moved); a Lock with a revision change,
  pending or in-flight Lock on one of its buffers (buffer-lock observation,
  [replay-native-callbacks.md](../reverse-engineering/replay-native-callbacks.md)); **expected
  visible but absent** (its world AABB lies well inside the current view frustum, beyond the
  near plane, yet the engine has not submitted it for 8 frames: the engine hid or removed it
  for a reason that is not the view); exit from 2 × the outermost cascade box; capacity
  eviction (farthest unseen first); Reset, device loss, teardown, observer loss (flush all).
- Capability boundary: retention needs a *known* lifetime snapshot. Without the observer
  (unknown executable, observer disabled) the feature is off and the replay is today's
  same-frame set. Missing lifetime evidence never becomes implied stability.

### Why not the engine side (B)

- **B1, relax the cull so the engine draws sun-ward nodes.** Every extra node goes through
  the full colour path at ≈ 22 µs per draw. A 24° turn already hides 253 draws; the surround
  of a complex is several times the visible set (union of three views here: 833 draws against
  517–706 per view). +500 to +1,500 draws is +11 to +33 ms on a 22 ms busy frame. It also
  runs the predicate's side effects for nodes the game considers invisible (`node[0x12c] |=
  0x2000`; `0x004f66e0` calls the track/emitter play helper `0x004f65f0` at `0x004f6836`, a
  media-object creation path per voice-startup-sequence.md §3). Rejected on cost alone.
- **B2, a cull-site stamp (node, world matrix) and a proxy replay of the node's mesh.**
  Removes the colour cost and would cover never-seen casters, but needs the private chain
  node → mesh → sub-mesh → buffer wrapper → `IDirect3DVertexBuffer9`/`IndexBuffer9`/
  declaration/range, the opaque-versus-blended sub-mesh decision and the LOD choice for a
  culled node, none of which is documented (the draw goes through vtable `+0x148` of an
  unidentified class), plus proof that a never-drawn mesh has device buffers at all. The
  proxy learns every one of those facts for free from a real submitted draw. B2 buys only
  the never-seen case (below) at the price of four private layouts; it stays the fallback if
  that case proves to matter. A trampoline on a per-traversed-node predicate would also cost
  ≤ 0.33 µs per node per frame (route-cost-run1 stamp bound) for every frame, used or not.

### The failure case of C: a caster never seen in this load epoch

A caster casts only after it has been submitted once while inside the outermost cascade
(25,000 units). It fails after a load or gate arrival facing away from a structure that is
already in range, until the first look at it; the artefact is one pop-in per node per load,
against today's pop on every turn. Approaches are flown facing the target and the far
cascade reaches 5 km, so a caster is normally seen long before its shadow can land on
anything visible. Accepted; the census (stage 1) counts `first_seen_in_range` so the claim is
measured, not assumed.

## Evidence (run111, build `e575136`, per-draw `motion_route` lines of ten F8 frames)

| Question | Measurement |
| --- | --- |
| Are (node, VB/IB/range) keys stable frame to frame at a frozen camera? | 16633–16640: 517 routed z-writing draws, 76 nodes, 297 distinct buffer/range keys; the `(node_handle, node_serial, key)` set is **identical in all 8 frames**; 0 duplicate `(node, key)` within a frame; 0 handles with a changed pointer or serial |
| Moving bursts | 15565–15572 (camera moving ≈ 30 units): 624 draws / 139 nodes, union = intersection = 624; 16050–16057 (camera moving, forward turning ≈ 4°): 706 draws ×7, then 743 (+1 node, +37 draws). 973 + 966 adjacent common-node pairs, **0 key-set differences** |
| Turning pairs | 15572↔16635: 71 common nodes, 70 same key set, 71 same serial/pointer, 68 + 5 not common. 16051↔16635: 43 common, 42 same, 95 + 33 not common. 15572↔16051: 110 common, 110 same. Submission is all-or-nothing per node |
| LOD swap | The one differing node in both pairs is node 52765, model `53a1`, same serial: `lod` 0 → 1 with a disjoint VB set (18 VBs each, 0 shared; vb 2197… → 2161…, primitives 50/16/120 → 25/9/80). First live LOD swap on a stable serial (motion-history-key.md §4 had none). A draw-keyed store would double this caster; a node-keyed store replaces it |
| Is VB/IB/range an identity? | No: 90 of 297 keys are drawn by more than one node, up to 14 nodes per key (instanced station modules) |
| Static versus moving | Frozen burst, submitted rows hash per `(node, key)`: 412 draws constant over 8 frames, 105 changing; by node **66 static, 10 moving, 0 mixed** (moving models `4a88` ×3, `4f79`, `4f76`, `4f73`, `5529`, `35ba45c3..c5`). The rows hash is unjittered (constant while `jitter_index` and the offsets change every frame) |
| Do the engine's caster buffers get locked after load? | `shadow_replay_candidates`, all 34,529 frames: `serial_changed`, `readonly_after`, `writable_after`, `pending`, `in_flight`, `stale`, `dynamic`, `default_pool`, `unknown`, `shadow_mismatch` are 0 in every frame (leased records only, ≤ 52 per frame) |
| Registry churn | `mutation_revision` 99714 (frame 15572) → 99715 (16051) → 99753 (16633–16640, constant): 39 mutations in 1,061 frames. Revalidating retained nodes only when the revision moved is ≈ 4 % of frames |
| Store size | Union over the three views 15572 / 16051 / 16635: 833 `(node, key)` records, 172 nodes, 390 distinct VBs; one load/registry epoch (2, 2) throughout |
| World offset | `camera_state` `t` = (55962, 20286, 55517), \|t\| ≈ 81,400 units; `p00` 0.8, `p11` 1.3333 |

**World-row recovery precision** (numerical model, 4,000 random poses per case at the run111
world offset, engine product in float32, recovery by the production formula in double from
the float32 camera latch; not an engine measurement): origin error median 0.0033, p99 0.010,
max 0.015 units; a vertex 3,000 units from the node origin the same; the disagreement between
two recoveries of one static node from two unrelated cameras, at the AABB corner: median
0.005, max 0.030 units. It does not grow with caster distance (250 and 25,000 units agree):
the error is float32 spacing at the ≈ 81,000-unit world offset (0.004–0.008), not the view
distance. Against the texels: 0.03 units is 0.25 texel of C0 at 4096² (0.122 units), 0.06
of C0 at 1024² (0.49), 0.002 of the 25,000 cascade (≈ 12 units), and well below run 38's
0.54-unit constant bias.
`eps = 0.05` separates static from moving with margin in the model; the census replaces the
model with the real drift histogram before `eps` is fixed.

## Design

**Store.** Fixed storage, allocated at attach: 1,024 nodes (open-addressed on `node_serial`),
4,096 draw records in per-node runs. Record ≈ 280 B (today's ≈ 150 B lease fields + 12 double
world rows + world AABB centre/half-extent): ≈ 1.2 MiB. No allocation after attach.

**Seen path (per z-writing managed draw, hot).** Node-table probe on the serial the route
key already holds, a 64-byte copy of the submitted rows, a seen stamp: ≈ 30–50 ns, no device
call. For a known `(node, key)` with unchanged buffer revision the retained references are
reused, which removes today's per-frame `GetVertexDeclaration` + three `AddRef` + three
`Release` per leased draw (references are taken once per record, not once per frame). New or
changed draws take the lease as today. World rows are computed at scene end only for records
that are new, moving-class, or due for the 1-in-16 static re-verification (≈ 0.15 µs each).

**Unseen path (scene end).** Per retained unseen node: frustum-expected test and the
per-cascade test of the precomputed world AABB against the shared sun basis (≈ 20–40 ns per
record; 3,000 records ≈ 0.1 ms worst). Admitted records are issued like live ones at the
measured 1.3 µs per draw per cascade. The cascade note's caps and the `B = 640` issue budget
are unchanged and remain the hard bound (1,664 issues, 2.16 ms); live records take priority
under a cap, retained ones fill by distance. Expected effect on the busy frame: C0/C1 grow by
the off-screen static parts inside 250 / 1,500 units (tens of draws, ≈ 0.05–0.1 ms); C2/C3
move from "the visible share of the pool" towards "the pool", which the cascade note's
realistic worst (930 draws, alternate frames, ≈ 1.06 ms averaged) already budgets.
Buffer-quiet checks for unseen records run per distinct buffer, one eighth of the store per
frame (content staleness ≤ 8 frames against a measured Lock rate of 0).

**References.** A retained record holds `AddRef` on the application's VB, IB and declaration
across frames (today: within the frame). Managed buffers survive Reset, but the existing
contract stands: `before_reset`, device loss and teardown release every reference and flush
the store (off-screen shadows return as nodes are seen again). Releases happen only where
leases retire today (scene end, under the capture mutex, the replay token released first),
because a final native Release can call back into the application synchronously.

**Follow-on, not part of this decision.** With retention the far cascades' caster set stops
depending on the view direction, so a static-only far map becomes reusable until its snapped
centre shifts or the set changes (the cached far cascade of the survey). Without retention
that cache can never be valid.

## Native Windows

Shared code uses documented D3D9 only: `AddRef`/`Release` on application resources,
`SetStreamSource`/`SetIndices`/`SetVertexDeclaration` with them, managed-pool persistence.
Node identity comes from patches in the game EXE (identical on Windows) behind the existing
executable-hash gate; with the gate closed the feature is off, not degraded. No Wine export,
lock or layout is involved. Windows-compatible source only; native behaviour unverified. On
ratification add the line to [platform-portability.md](platform-portability.md).

## Staged plan

1. **Census (diagnostic only, next consolidated build).** The store without references and
   without replay: keys, world rows, classes, would-be expiry reasons. Line
   `shadow_retention_frame`: `nodes_live nodes_unseen records static moving would_replay_c0..c3
   drift_max drift_p99 lod_replaced retired lock_changed expected_absent box_exit evicted
   first_seen_in_range mutation_checks us`. Settles: the real drift (hence `eps`), store
   size, how many unseen casters fall in each cascade, how much of the unseen set is moving,
   how often "expected visible but absent" fires on healthy static nodes (false positives).
2. **Static retention live** (`--shadow-caster-retention`, default off until a user run).
   References held, retained records replayed in every cascade, all expiry rules, counters
   split `replayed_live` / `replayed_retained` per cascade on `shadow_replay_depth`. Blocked
   on RE questions 3, 5, 6.
3. **Moving nodes.** Decide from the census. Preferred, if RE questions 1–2 allow: a
   read-only refresh of the unseen node's engine world transform through `engine_memory`,
   applied as the rigid delta `W_node(now) · W_node(then)⁻¹ · W_draw(then)` (no sub-mesh
   layout needed), self-validated every frame a node *is* visible by comparing the predicted
   rows with the recovered ones (mismatch > `eps` disables the path). No trampoline. If the
   engine does not update culled nodes, moving nodes stay unretained.
4. Optional: cached far cascade (own note).

## Fixture and twin cases

The motion-output fixture seam (`MotionOutputFixtureScope`: serials, epochs, model, lod)
drives synthetic nodes; "culled" = the fixture omits the node's draws and turns its camera;
the map readback is compared with the CPU twin. Each case has a retention-off control.

| Case | Must hold |
| --- | --- |
| a. camera turns away from a static caster | its blob stays at the twin's texels (≤ 1 texel) for 600 frames; control: blob absent on the first culled frame |
| b. caster moves while off screen | moving-class node: blob gone on the first unseen frame (staleness 0). Static-class node moved while unseen: exactly one blob, at the new place, on the frame it is resubmitted. Stage 3: blob tracks the fixture's engine-side matrix within `eps` |
| c. object destroyed | serial retired (or epoch bumped) while unseen: blob gone next frame; no lifetime signal but in frustum and unsubmitted: gone within 8 frames |
| d. LOD swap | same serial, `lod` 0 → 1, other VB: covered texels equal the twin's LOD-1-only count on the swap frame; swap while unseen is impossible by construction (retained set persists until resubmission) |
| e. shared mesh | two serials, one VB/IB/range, different world rows: two blobs |
| f. buffer Lock | writable Lock on a retained unseen buffer: record dropped before the next replay, `lock_changed` = 1 |
| g. Reset, teardown, expiry | fixture-owned buffers return to their baseline reference count; store empty after Reset; retention resumes on resubmission |
| h. capacity | 1,025th node evicts the farthest unseen node, `evicted` = 1, no overflow write |
| i. lifetime unknown | snapshot `known = 0`: nothing retained, replay identical to retention-off |
| j. precision twin | retained rows from camera 1 against live rows from camera 2 at an 81,000-unit offset: ≤ 0.05 units at the AABB corner |

F8: on capture frames one `shadow_retention_caster` line per retained record (handle, serial,
model, lod, class, frames unseen, world AABB centre, cascade mask, reason kept); the existing
map dumps then show which blobs are retained. User run: station arm behind the camera,
turn through 180°, shadow on the hull persists; fly away until box exit; destroy a target.

## RE questions (disassembly task; no layouts are assumed here)

1. `0x004f66e0`: ABI (arguments, `this`, return), what it tests (which node fields: bounds,
   planes, distance or size thresholds, per-view flags), every side effect (the
   `node[0x12c] |= 0x2000` write, the call at `0x004f6836` to `0x004f65f0`, any LOD or fade
   computation), and whether `0x0047d9c0` recurses into the children of a node that failed it.
2. World transform: is `0x004bdee0` called before `0x004f66e0` on both paths (queue walk
   `0x0047e6e0`, traversal `0x0047d9c0`); where is its result stored (node field or caller
   temporary); is the stored transform current for a node that fails the cull, or whose
   ancestor failed it? (Stage 3.)
3. Which gates other than the view cull suppress a node's submission between traversal and
   `0x004c4fc0` (distance or size cull, fade `+0x13c` reaching zero, hidden or docked bits in
   `+0x12c`/`+0x130`), so that "expected visible but absent" is known to be safe for healthy
   static nodes and sufficient for hidden ones. (Stage 2.)
4. LOD: where the value reported as `lod` is chosen (per node, per view, by distance or
   projected size), whether a culled node keeps its last LOD, and whether all LOD meshes own
   device buffers after load or on first draw.
5. VB/IB release: does the engine use the return value of `Release` on mesh buffers (loop to
   zero, assert), so that a foreign long-lived reference is observable to it; where are mesh
   buffers released (model unload, sector change), and is every node of a model retired
   from the registry before that release? (Stage 2; if the reference is observable, the
   fallback is an owned position/index copy filled by the existing extent read, ≈ 8 B per
   vertex, rejected for now for its memory and the index reads.)
6. Sector change or gate jump: does the renderer-load epoch (`0x0040508d`) or the bulk map
   destruction fire, so that retained nodes of the previous sector flush?
   (object-lifetimes.md establishes no universal sector-transition hook.) (Stage 2.)
7. Deferred, only if the never-seen case matters: the node → mesh → sub-mesh → buffer/range
   mapping and the class behind vtable `+0x148` (option B2).

## Unknown, and what settles it

- Real world-row drift of static nodes under camera motion (model says ≤ 0.03 units): the
  census histogram.
- False-positive rate of "expected visible but absent" (engine bounds larger or smaller than
  the vertex AABB; occlusion or size culls): census counter plus RE question 3.
- Share of the unseen in-range caster set that is moving (rotating station sections):
  census; decides whether stage 3 is worth its RE.
- Cost of `get_buffer_lock_view` per distinct buffer at store scale (today ≤ 52 per frame):
  measure in the census build; the one-eighth round-robin is the mitigation already assumed.
- Lock activity on buffers of *unleased* casters was not observable in run111 (only leased
  records are compared); the census watches every retained buffer.
