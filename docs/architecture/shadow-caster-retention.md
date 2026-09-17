# Sun-shadow casters the engine did not submit: retention by node

Ratified 2026-09-17 as the direction (option C: proxy-side retention keyed by the verified
node lifetime, no engine trampoline). This revision is the **implementable contract for
stage 1 (census) and stage 2 (static retention)** after the RE answers in
[shadow-caster-lifetime.md](../reverse-engineering/shadow-caster-lifetime.md). Extends
[shadow-cascades.md](shadow-cascades.md) §1 (one record list, per-cascade masks and caps) and
[shadow-replay-gates.md](shadow-replay-gates.md) ("Casters by bounds", the geometry lease);
ledger [../verification/directional-shadows.md](../verification/directional-shadows.md) ("Run 38
A (run111) diagnosis", cause 3). It assumes the diagnosis' fixes 1
and 2 (one validated frame sun from the bound program's own `LightDir_Dir0`; an extent cache
that does not thrash) and the N-cascade replay.

**Status 2026-09-17: stages 1 and 2 are implemented, default off, fixture-qualified on bottle
X3, not installed and not yet run in the game** (see "Implemented" at the end for what was
built, where it differs from the text below, and what is unverified). Stage 3 and the census
calibration (`eps`, `age_cap`, transit behaviour) are open; stage 2 stays off until a census
run settles them.

## Problem

The maps are replayed at scene end from the frame's recorded z-writing managed draws. A 7.1°
camera turn removes 64 of 127 submitted nodes, 24° removes 89 of 126, so a caster outside the
view (a station arm behind the camera, between the sun and the ship) loses its shadow when the
camera turns. The engine pass that decides this is **`0x0047cfe0`** (per-node cull and LOD,
13 suppressors: frustum sphere test, behind-eye, two distance culls, three projected-size
culls, last-LOD fade, hide latches, the attached-node gate), with more gates in `0x0047e920`,
`0x0047d9c0` and `0x004c4fc0`. `0x004f66e0`, which the first revision called the cull, is the
animated-texture stepper (shadow-caster-lifetime.md §0).

## Decision

The unit of retention is the **node**; identity is `(load_epoch, registry_epoch, node_serial)`
from the lifetime observer ([object-lifetimes.md](../reverse-engineering/object-lifetimes.md)),
which the route key already carries on every routed draw. VB/IB/range is not an identity (90
of 297 keys are shared by up to 14 nodes); world rows are payload, not key. Serials are
monotonic and never reused, so a draw-time probe cannot hit a stale record.

1. **Payload per draw of the node**: the draw range and layout, cull mode, the object-space
   AABB (extent cache), the world AABB, and the **object → world rows** `W·A` that
   `shadow_replay_light_rows` already computes in double from the submitted clip rows and the
   frame's camera latch; plus the node's `model` and `lod` and the store's references (§
   References). An unseen node replays with `light_rows = S(sun basis, cascade) · W_retained`;
   no camera enters that product.
2. **A node submitted this frame supersedes its whole retained set** with this frame's draws.
   A LOD or model change observed on resubmission therefore replaces, never adds (counted
   `lod_replaced` / `model_replaced`): no double caster from the node's own history.
3. **Only static nodes outlive the frame they were last seen.** Static = the recovered world
   rows of every draw of the node agree with the previous sighting within `eps` (provisional
   0.05 units at the AABB corner; the census fixes it) for 8 consecutive sightings. A moving
   node is dropped on its first unseen frame (staleness 0, the shadow pops as today); stage 3
   may change that.
4. **Excluded classes, never retained**: camera-facing and screen nodes, whose stored basis the
   engine rewrites only while they pass the renderable gate (`node+0x12c & 0x20`, `& 0x4000`,
   `& 0x10000000`, `& 0x200`, and `node+0x130 & 0x200`; shadow-caster-lifetime.md §2 names the
   `0x200` bit under both words, so both are excluded until that is resolved). The route's
   existing per-draw node read (`object_trace::Snapshot::flags12c/flags130`) supplies the bits:
   one mask test, no new read. Also excluded: a draw without a known lifetime snapshot or node
   scope, and everything the candidate filter refuses today (non-managed pool, dynamic usage,
   multistream, excluded programs).
5. **Capability boundary**: retention needs a *known* lifetime snapshot and the executable
   gate. Otherwise the feature is off and the replay is today's same-frame set. Missing
   lifetime evidence never becomes implied stability.

### Expiry (the complete list)

The first revision's "expected visible but absent" rule is **deleted**. A static node the
engine stops submitting while it is inside the frustum is, in the steady state, culled by
projected size (`node+0x1d8`, inherited parent value, `< 1`), by distance (`view+0x370` with
the detail-level clamp, per node and per root subtree), by the last-LOD fade, by the LOD-0
sphere differing from the vertex AABB, or taken over by the instanced-batch path
(shadow-caster-lifetime.md §3f): the caster still exists, and these are exactly the distant
casters retention is for. **No size, distance, LOD-fade, behind-eye or frustum cull ever
expires a static retained node**; absence, inside or outside the frustum, is not evidence.
A retained node leaves the store only on:

| Reason (counter) | Trigger | Latency |
| --- | --- | --- |
| `retired` | the node's serial is retired in the lifetime observer (removal, overwrite, failed membership read) | before the next replay (see Retirement) |
| `flush_epoch` | `load_epoch`, `registry_epoch` or `observer_epoch` differs from the store's, or the snapshot becomes unknown | whole store, same frame |
| `lod_replaced`, `model_replaced` | resubmission with another `lod` / `model` or another draw set | supersede, same frame |
| `reclassified` | resubmission with world rows beyond `eps` of the retained ones: the node returns to the moving class | same frame |
| `buffer_changed` | buffer-lock observation of any held buffer shows a revision change, a pending or in-flight Lock, or the view names another allocation or generation | ≤ 8 frames unseen (round-robin), same frame when seen |
| `buffer_orphaned` | the store's reference is the last one on a held buffer (§ References): the engine released the mesh, e.g. the unsignalled model change `0x00487e30` on an unseen node | ≤ 8 frames |
| `box_exit` | the node's world AABB lies outside 2 × the outermost cascade box about the current snapped centre | same frame |
| `age` | unseen for more than `age_cap` frames (provisional 7,200; `--shadow-caster-retention-age`; calibrated from `shadow_retention_resight`) | — |
| `evicted` | capacity: farthest unseen node first; a live node is never evicted for an unseen one | same frame |
| `flush_reset`, `flush_device`, `flush_teardown` | every Reset *attempt* (in `before_reset`), device loss, device teardown, feature off, observer loss | whole store, before the native call |
| `flush_sun` | sun re-latch (§ Sector transit) | whole store, same frame |

The age cap is the only bound on a ghost with no signal at all (a static unseen node that the
simulation hides, docks or starts moving without retiring it or releasing its mesh). Its
value trades that ghost's lifetime against the shadow of a parked-behind-the-camera station
popping off; the census measures both sides.

### Why not the engine side (B)

- **B1, make the engine draw sun-ward nodes** (now: force bit `0x2` in `0x0047cfe0`). Every
  extra node goes through the full colour path at ≈ 22 µs per draw; the surround of a complex
  is several times the visible set (union of three views here: 833 draws against 517–706 per
  view). +500 to +1,500 draws is +11 to +33 ms on a 22 ms busy frame. Rejected on cost.
- **B2, a cull-site stamp and a proxy replay of the node's mesh.** `0x004bdee0` writes the
  world matrix to engine-global scratch, not to the node, and only for traversed nodes, so a
  stamp in `0x0047cfe0` has no matrix to record; the mesh chain (node → model → LOD record
  `+0x3c` → subset records, stride `0x1a8`, VB `+0x0c`, IB `+0x10`) is now partly known, but
  the opaque-versus-blended subset decision, the declaration and the LOD choice for a culled
  node are not, and a LOD record can lack a GPU build record altogether (`LODrecord+0x3c == 0`;
  when it is built is not established).
  It buys only the never-seen case below; it stays the fallback if that case proves to matter.

### The failure case of C: a caster never seen in this load epoch

A caster casts only after it has been submitted once inside the outermost cascade (25,000
units). It fails after a load or gate arrival facing away from a structure already in range,
until the first look at it: one pop-in per node per load, against today's pop on every turn.
Accepted; the census counts `first_seen_in_range` so the claim is measured.

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
| Node classes among routed z-writing nodes | 15572 / 16051 / 16635 (139 / 138 / 76 nodes, `object_context` joined by draw index, 0 missing, 0 handle mismatches): **0** nodes with a camera-facing bit (`flags12c & (0x20 \| 0x200 \| 0x4000 \| 0x10000000)`, `flags130 & 0x200`), 0 with `0x800`, 0 attached (`0x40000`), 0 with the last-LOD fade (`0x8000`). Excluding the camera-facing classes costs no coverage in this scene |
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

## Contract

**Store.** Fixed storage allocated at attach, no allocation afterwards: 1,024 nodes
(open-addressed on `node_serial`), 4,096 draw records in per-node runs, 1,024 held-resource
slots. Record ≈ 280 B (today's ≈ 150 B lease fields, 12 double world rows, world AABB centre
and half-extent); ≈ 1.3 MiB in total.

**Seen path (per z-writing managed draw, hot).** Class mask test, node-table probe on the
serial the route key already holds, a 64-byte copy of the submitted rows, a seen stamp:
≈ 30–50 ns, no device call. For a known `(node, key)` with unchanged buffer revision the held
references are reused, which removes today's per-frame `GetVertexDeclaration`, three `AddRef`
and three `Release` per leased draw. World rows are computed at scene end only for records
that are new, moving-class, or due for the 1-in-16 static re-verification (≈ 0.15 µs each).

**Unseen path (scene end, before the replay).** Drain retirements; per unseen retained node
the box-exit test and the per-cascade test of the precomputed world AABB against the shared
sun basis (≈ 20–40 ns per record; 3,000 records ≈ 0.1 ms worst). Admitted records are issued
like live ones at the measured 1.3 µs per draw per cascade. The cascade note's caps and the
`B = 640` issue budget are unchanged and remain the hard bound (1,664 issues, 2.16 ms); live
records take priority under a cap, retained ones fill nearest first. Expected effect: C0/C1
grow by the off-screen static parts inside 250 / 1,500 units (tens of draws, ≈ 0.05–0.1 ms);
C2/C3 move from "the visible share of the pool" towards "the pool", which the cascade note's
realistic worst (930 draws, alternate frames, ≈ 1.06 ms averaged) already budgets.

### References (no record can dangle)

The engine releases a mesh's VB and IB in the node destructor **before** the registry removal
the observer sees (`0x00487cab` then `0x00487d70`), can release them with no lifetime event at
all when a live node changes model (`0x00487e30`), does not pool GPU buffers across sectors (a
released VB address can be re-issued to another mesh), and never reads the return value of
`Release` (0 of 144 sites; shadow-caster-lifetime.md §4). Therefore:

- **Every retained record holds its own native `AddRef` on its VB, its IB and its vertex
  declaration, taken when the record is created (at the draw, while the application's binding
  is live), never at replay time.** Implementation: a held-resource table keyed by the
  wrapper identity, one native reference per distinct resource and a use count of the records
  that name it (390 distinct VBs for 833 records here), so that the store's own reference
  count on a resource is exactly 1. Nothing in the store is keyed on, or dereferences, a
  pointer it does not hold a reference to. A foreign long-lived reference is not observable to
  the engine, so no owned vertex copy is needed.
- A reference is released when the last record naming the resource expires, and all of them in
  every flush. Releases happen only where leases retire today: at scene end or in the flush,
  under the capture mutex, with the replay token already released, because a final native
  `Release` can call back into the application synchronously (replay-native-callbacks.md).
- **Pools.** Only `D3DPOOL_MANAGED`, non-dynamic buffers are admitted (the candidate filter's
  existing `Managed` class; `default_pool = dynamic = 0` in all 34,529 run111 frames). A
  managed buffer survives Reset with its content, and a vertex declaration is not a pool
  resource, so holding them would not fail a Reset. A `D3DPOOL_DEFAULT` buffer must be
  released before Reset or Reset returns `D3DERR_INVALIDCALL`; such a buffer is never
  retained, and the store does not rely on that filter: **`before_reset` releases every held
  reference and empties the store on every Reset attempt**, successful or not (the lock-view
  generation advances on every attempt, which invalidates the revision evidence anyway), as
  does device loss and teardown (an outstanding reference would keep the device alive).
  Off-screen shadows return as nodes are resubmitted.
- **`buffer_orphaned` probe.** For each held resource, one eighth of the table per frame:
  native `AddRef` then `Release`; a returned count of 1 means the store's is the last
  reference. COM documents the return value as the new count but "for test purposes", so this
  is a fail-safe advisory signal, not a correctness gate: a runtime that over-reports only
  delays the drop to `age`/`box_exit`/`retired`, and no runtime can report less than the
  store's own reference. Fixture case g proves the signal on the runtime in use; if it fails
  there, the probe is disabled and logged (`orphan_probe=0`). ≈ 100–130 resources per frame at
  the union size (390 VBs, as many IBs, the declarations), two non-final COM calls each.
- Stage 1 holds **no** references: the census keys on allocation ids and wrapper identities as
  registry keys only, never dereferenced, exactly as `shadow_replay_candidates` does today,
  and counts `buffer_gone` when an unseen record's scene-end view is unavailable or names
  another allocation (the measured rate of the release-before-retirement and model-change
  cases).

### Retirement

A retired node's records are flushed at the retirement signal, which the proxy consumes
before any replay that could use them:

- The lifetime observer gains a bounded **retirement journal**: the removal, overwrite and
  failed-membership paths append the retired node serial to a fixed ring (2,048 entries, 64 KB static, CPU
  append only, no allocation, inside the wrapper's existing envelope). The store drains it at
  scene end before the unseen path and at frame begin: cost O(retired), so the burst of a few
  hundred individual removals of a gate jump is absorbed in one frame.
- The store revalidates fully only on three events: a drain that reports `overflow`, a drain
  that reports `available=false`, and registration or re-registration (a fresh cursor starts
  at the head). A moved `mutation_revision` is not such an event: it moves on every birth. A
  `FlushAll` entry drops the whole store unconditionally; epoch comparisons use the drain's
  fields, never the entry's; `Retired` means the observer dropped the key, not proof of death.
  **Full revalidation** goes through `object_lifetime::current` (0.69 µs per call
  on the direct-read path, route-cost-run1: ≤ 0.71 ms once for a full store), and any node
  whose snapshot is unknown or whose serial differs is flushed. At rest the revision moves on
  ≈ 4 % of frames (39 mutations in 1,061).
- Between the engine's buffer release and the drain the record still holds valid D3D objects
  (its own references), so the window is benign: at worst one frame of a shadow from a node
  that died this frame.
- An epoch change (`0x0040508d` fires once per game or save load, before the registry sweep
  `0x004872c0`) flushes the whole store.

### Sector transit

Per the static call graph nothing bulk fires on a gate jump: no load epoch, no map
destruction, no model unload (shadow-caster-lifetime.md §5b); the old sector retires node by
node through the observed removal helper. What clears the old sector is therefore, in order
of authority: (1) `retired` per node through the journal; (2) `box_exit`; (3)
belt-and-braces, **`flush_sun`**: the frame-sun resolver of diagnosis fix 1 accepts a new sun
only after it persists beyond the plausibility gate; that acceptance event (a change of the
validated world sun direction by more than the gate angle) flushes the whole store, as does
the first validated sun after a period with none. A retained world row is only meaningful
under the sun and the world frame it was recorded in, and a transit changes both. It is not
the primary rule because two sectors can share a sun direction.

**To verify in the census (a user run with one gate jump and one jump-drive or load):**
whether the world origin re-bases on transit. X3 positions are sector-local, so the expectation
is that the camera world position (`−t·Rᵀ` of the camera latch) jumps discontinuously and the
new sector's nodes occupy the same coordinate range as the old one's, in which case `box_exit`
alone is **not** sufficient (an old caster can lie inside the new box) and rules 1 and 3 carry
the transit. The census records, on the frames around a transit: `cam_jump` (camera position
delta above 2 × the outermost half-extent in one frame), `sun_relatch`, the retirement burst
(`retired` per frame and the number of frames it spans), and `transit_survivors` (nodes
recorded before the event that are still in the would-be store 60 frames after it, by reason
they survived: still known to the observer, inside the box). `transit_survivors > 0` with a
known serial means the engine keeps old-sector nodes alive and the age cap or a stronger
transit signal is required before stage 2 ships.

## Stage 1: the census (diagnostic only, next consolidated build)

`X3M_SHADOW_RETENTION_CENSUS=1` (launcher `--shadow-retention-census`). The full store logic
without references and without replay: keys, world rows, classes, every expiry decision taken
as if live. Off: no code runs. One line per frame, all counts per frame unless marked
*(level)*:

```
shadow_retention_frame device frame mode=census|live known
  nodes_live nodes_unseen records records_unseen static moving        (level)
  excluded_class unscoped new_nodes first_seen_in_range promoted
  superseded lod_replaced model_replaced reclassified
  retired journal_overflow revalidated mutation_delta
  buffer_changed buffer_gone buffer_orphaned orphan_probe
  box_exit age evicted flush=<none|epoch|reset|device|teardown|sun|observer>
  unseen_in_frustum unseen_outside                                   (level)
  live_c0..live_c4 would_c0..would_c4 capped_c0..capped_c4           (level)
  drift_n drift_p99 drift_max                                        (units, static nodes re-verified this frame)
  age_max refs_held                                                  (level)
  sun_relatch cam_jump transit_survivors
  us
```

- `known`: lifetime snapshot known this frame. `unscoped`: z-writing managed candidate draws
  with no node scope or unknown lifetime (never retained; also the first evidence on whether
  the instanced-batch path `0x0046d080` reaches the proxy as managed draws).
- `would_cN`: unseen static records meeting cascade N; `live_cN` the live ones; `capped_cN`
  what the cascade cap would cut. These size the stage-2 budget.
- `unseen_in_frustum` / `unseen_outside`: unseen retained nodes whose world AABB is wholly
  inside, or not inside, the current frustum. Informational only (it measures how much of the
  unseen set is size/distance/batch-suppressed rather than view-culled); it drives no expiry.
- `buffer_orphaned`, `orphan_probe`, `refs_held` are 0 in census mode; `buffer_gone` is its
  stand-in. `us`: CPU time of the census/retention work of the frame.

Every 300 frames, cumulative since attach, the age-cap calibration line:

```
shadow_retention_resight device frame
  b0_same b0_moved b0_changed  b1_… b2_… b3_… b4_…   (unseen ages <60, <600, <3600, <14400, ≥14400 frames)
  expired_retired_b0..b4 expired_box_b0..b4 expired_gone_b0..b4
```

`same`: a static node resubmitted with its retained model, LOD, draw set and world rows within
`eps`; `moved`: beyond `eps`; `changed`: other model, LOD or draw set. `age_cap` is set where
`moved + changed` stops being negligible against `same`; `expired_*` shows which authority
ended unseen nodes of each age. On F8 capture frames one `shadow_retention_caster` line per
retained record (handle, serial, model, lod, `flags12c`, class, frames unseen, world AABB
centre, cascade mask, in-frustum).

The census settles: `eps` (real drift against the model's ≤ 0.03 units), store and table
sizes, per-cascade retained counts, the moving share of the unseen in-range set (whether
stage 3 is worth its RE), `age_cap`, `buffer_gone` rate, journal sizing, the transit
behaviour above, and the cost `us`.

## Stage 2: static retention live

`--shadow-caster-retention` (default off until a user run accepts it), `mode=live` on the same
line; `shadow_replay_depth` gains `replayed_live` / `replayed_retained` per cascade. Work
items: the store and held-resource table; the observer's retirement journal (hook-side code:
`implement` on Fable, observer fixture extended); `before_reset` / loss / teardown flush;
`flush_sun` from the frame-sun resolver; the orphan probe; the fixture cases below. Entry
conditions from the census: `transit_survivors = 0` or an added rule that makes it so; an
`age_cap`; `eps`; `us` within the budget above.

Stage 3 (moving nodes) and the cached far cascade stay out of this contract. For stage 3 the
RE result is that a culled node has no stored world matrix, only its fixed-point position
`+0xb0..` and basis `+0xc0..` (already in `object_trace::Snapshot`), whose simulation-side
writer is not located; the rigid-delta refresh with per-frame self-validation on visible
nodes remains the plan, excluded for the camera-facing classes.

## Fixture and twin cases (stage 2)

The motion-output fixture seam (`MotionOutputFixtureScope`: serials, epochs, model, lod) drives
synthetic nodes; "culled" = the fixture omits the node's draws and turns its camera; the map
readback is compared with the CPU twin. Each case has a retention-off control.

| Case | Must hold |
| --- | --- |
| a. camera turns away from a static caster | its blob stays at the twin's texels (≤ 1 texel) for 600 frames, **including while its AABB is inside the frustum and unsubmitted** (the size/distance-cull stand-in); control: blob absent on the first culled frame |
| b. caster moves while off screen | moving-class node: blob gone on the first unseen frame. Static-class node moved while unseen: exactly one blob, at the new place, on the resubmission frame, `reclassified` = 1 |
| c. object destroyed | serial retired through the journal while unseen: blob gone on the next replay, the node's references released (baseline count); journal overflow: same through full revalidation |
| d. LOD / model swap | same serial, `lod` 0 → 1 (or other model), other VB: covered texels equal the twin's new-mesh-only count on the swap frame |
| e. shared mesh | two serials, one VB/IB/range, different world rows: two blobs; one reference per resource (`refs_held` counts resources); retiring one node leaves the other's blob |
| f. buffer Lock | writable Lock on a held unseen buffer: record dropped within 8 frames, `buffer_changed` = 1 |
| g. release before retirement | the fixture releases its VB/IB (the engine order) and retires the serial one frame later: the intervening replay is valid (no stale or recycled object: the fixture immediately creates a same-size VB and asserts a different allocation id), `buffer_orphaned` fires within 8 frames when the retirement never comes (the `0x00487e30` case) |
| h. Reset, failed Reset, device loss, teardown | all references back to baseline **before** the native Reset; store empty; a default-pool control buffer owned by the fixture proves Reset succeeds; retention resumes on resubmission |
| i. capacity | the 1,025th node evicts the farthest unseen node, `evicted` = 1, no overflow write; a burst of 600 retirements in one frame drains in that frame without overflow (ring 2,048; 2,049 between drains is overflow and a full revalidation) |
| j. excluded class and unknown lifetime | a node with `flags12c & 0x20` (and each other excluded bit), and a draw with `known = 0`: never retained, replay identical to retention-off |
| k. age cap and sun re-latch | unseen past `age_cap`: gone, `age` = 1; validated sun changes beyond the gate: store flushed the same frame, `flush=sun` |
| l. precision twin | retained rows from camera 1 against live rows from camera 2 at an 81,000-unit offset: ≤ `eps` at the AABB corner |

User runs: census run (station approach, 180° turns, a parked minute facing away, one gate
jump, one load); stage-2 run (station arm behind the camera: shadow persists through the
turn; fly away to box exit; destroy a target; gate jump: no old-sector shadows).

## Native Windows

Shared code uses documented D3D9 only: `AddRef`/`Release` on application resources,
`SetStreamSource`/`SetIndices`/`SetVertexDeclaration` with them, managed-pool persistence
across Reset, release of everything before Reset. The orphan probe reads a documented return
value in an advisory, fail-safe role behind a fixture-proved capability flag. Node identity
and flags come from patches and reads in the game EXE (identical on Windows) behind the
existing executable-hash gate; with the gate closed the feature is off, not degraded. No Wine
export, lock or layout is involved. Windows-compatible source only; native behaviour
unverified ([platform-portability.md](platform-portability.md) gets the line when stage 2
lands).

## Implemented (2026-09-17)

Files: the store `src/proxy/shadow_retention_core.h` (pure CPU, host-compiled by
`verification/analysis/test_shadow_retention.py`), its per-device state
`src/proxy/shadow_retention.h`, the owner `src/proxy/motion_output_shadow_retention_inc.h`; the
record hook at the end of `note_candidate_draw`, the scene-end call before the capture lines and
the issue loop in `run_shadow_replay_cascades`; the launcher options `--shadow-retention-census`,
`--shadow-caster-retention`, `--shadow-caster-retention-age`, `--shadow-caster-retention-eps`,
`--shadow-retention-timing` (`X3M_SHADOW_RETENTION_CENSUS`, `X3M_SHADOW_CASTER_RETENTION`, `…_AGE`,
`…_EPS`; `X3M_SHADOW_RETENTION_TIMING=1` adds the per-draw cost to the frame line); the parser and census
summary `tools/analysis/shadow_retention.py`. Both modes need `--shadow-cascades` (the single map
is unchanged); `shadow_retention_device … enabled=0 reason=cascades|lifetime|allocation|journal`
names a refusal. Evidence: [directional-shadows.md](../verification/directional-shadows.md),
"Caster retention, stages 1 and 2".

Implemented as written: the node key, the payload with double world rows and the world AABB,
whole-set supersede, the excluded classes, every expiry of the table, the held-resource table
(one native reference per distinct resource, taken at the draw), managed non-dynamic buffers
only (the candidate filter), flush before every Reset attempt, at teardown and on a sun
re-latch, the journal consumer rules (drain at the scene end and the frame begin; full
revalidation on overflow, on `available=false` and at registration; `FlushAll` drops the
store; epochs from the drain), the orphan probe behind the attach self-test
(`orphan_probe=`), the two census lines and the F8 `shadow_retention_caster` line, the
`retained=` flag on `shadow_replay_caster` and `replayed_live<i>` / `replayed_retained<i>` on
`shadow_replay_depth` (both only while the option is on: off is byte-identical), per-frame
cascade masks of retained records against the current boxes, retained issues inside the
per-cascade caps (live first, retained nearest first) and inside the issue budget.

Where the build differs from, or sharpens, the text above:

- **Static class.** Per record: the first sighting stores the rows, each later verified
  sighting within `eps` of the *stored* rows (not of the previous sighting, so slow drift
  accumulates against one reference) raises the record's streak; a node is static when every
  record of it has a known extent and a streak of 8, i.e. from its ninth sighting. A record
  that joins a static node (another draw admitted, a LOD swap) returns the node to the moving
  class until that record has earned its streak. Verification runs on every sighting of a
  moving node, on a resighting after any unseen frame, after a set change, and on 1 sighting in
  16 otherwise.
- **The seen path keeps today's per-frame lease.** The store takes its own references beside
  it; the contract's reuse of the held references for live draws (no per-frame
  `GetVertexDeclaration` / `AddRef` / `Release`) is not built. It is an optimisation of the
  live path, not a retention rule.
- **`unscoped`.** A draw the scope gate refuses is still routed (unmatched) and recorded, so it
  reaches the record hook: it is counted there and never retained. Before this was handled the
  fixture showed such a draw flushing the whole store every frame as an epoch change (its
  epochs read 0).
- **A retained replay needs a scene end.** The route finds the bloom copy only behind a scene
  draw, so a frame with no routed scene draw replays nothing, as today; the fixture submits an
  excluded-class anchor draw on every frame for this reason.
- **Teardown.** A held resource the application already released pins one device reference
  that the device-Release hook's accounting cannot see, so its final-Release probe would never
  match. The hook (`release_device`, `src/proxy/capture.cpp`) flushes the store when the count
  is within the store's references of the final one; a false positive costs only the off-screen
  shadows. The store's Releases run with the reference accounting held busy, as `taa_call` and
  `release_resources` do.
- **Device loss** is a failed `Present` (`flush=device`); the fixture drives the same flush through its seam.
- **`flush_sun`** fires on the latch's `Relatched` verdict. "The first validated sun after a
  period with none" cannot occur on one device: the latch stays valid until attach.
- **Positional sun (merged with main c27e974).** The unseen walk builds each cascade's basis as
  the transaction does (`cascade_sun(k)`, `point_sun_.grid_anchor(k)`) and takes a record's mask
  against each cascade's own axes; the issue loop rebuilds the sun rows for a cascade whose axes
  differ; box exit uses the outermost cascade's own basis. A switch of the sun source
  (point ↔ latch) flushes the store like a re-latch (`flush=sun`).
- **Draws after the frame's scene end** are not sightings, and a frame that never reaches a
  scene end drops its sightings at the next frame begin (`abandoned`): their rows belong to a
  camera latch that is gone.
- **`first_seen_in_range`**: a new node whose world AABB centre lies within 0.8 × the outermost
  half-extent of the eye on its first sighting (it did not enter through the box edge).
- Fields after `us` on the frame line: `refused` (capacity with no unseen node to evict, a
  full resource table, an unleased draw), `moving_dropped`, `abandoned`, `deferred` (draws
  ignored while an epoch flush is pending: nothing is released at a draw), `journal_us`,
  `walk_us`, `draw_us`, `draw_calls`.
- Fixture case l runs on the host (the store's recovery in double from float32 rows, 2,000
  random pairs of unrelated cameras at the run111 offset, 600-unit AABB: worst 0.012 units
  against `eps` 0.05); the Wine script places its camera at the same offset.

Review fix round (2026-09-17, two reviews):

- **Release queue.** The owed references live in the resource table itself (`owed` flag, `pop_owed`),
  so the queue cannot fill and no reference is ever dropped; a re-acquire of an owed resource before
  the owner's Release reuses the held reference (no AddRef, no Release). `release_queue_full` counts
  acquisitions refused while owed slots hold the table (diagnostic).
- **Caps.** The room per cascade is `min(cap, record_capacity) − live`, so retained issues never
  exceed the issue storage and a frame is never refused `issues` because of them.
- **Budget.** Retained issues count into the far-cascade budget; a frame whose live issues alone fit
  the budget but whose total does not moves the far cascade (its live records included) to alternate
  frames: `far_alternate_due_to_retained` counts such frames.
- **Revalidation** confirms each node with its own recorded registry, node and camera identity; an
  observer that cannot answer about the node (camera or registry gone, observer disabled or
  mid-mutation) drops the node under `revalidate_context_lost`, not as `retired` (fail closed
  either way).
- **Issue-time buffer check.** Every retained record that may be issued is checked every frame with
  the live loop's revision compare before it enters the admitted list; a rewritten buffer is never
  replayed with the old range or declaration. The round-robin 1-in-8 check stays for records outside
  every cascade. The registry is asked once per *distinct buffer identity* per scene end
  (`Store::buffer_view`: a direct-mapped per-walk cache of `BufferView`, keyed by identity and the
  walk serial, no allocation); every record naming that buffer is compared against the one view
  (`buffer_verdict`: allocation, generation, revision, pending or in-flight Lock), so the cost is one
  registry lookup (about 0.3 µs under Wine) per distinct buffer plus a few integer compares per
  record. `admitted_checked` still counts records compared; `buffer_views` counts the lookups. The
  Lock hook and the draw path are untouched; the option-off cost stays zero. Correctness is the
  same as the per-record lookups: every lookup of a walk precedes that walk's issue on the game
  thread, so a buffer re-Locked before the scene end is seen by every record naming it, and the
  next scene end looks every buffer up again. Full-store fixture (716 retained records on one
  mesh): scene end 513 → 45 µs median, worst walk 573 → 104 µs.
- **Static class.** Every sighting is verified (the 1-in-16 schedule is gone), so a node that starts
  moving while seen is reclassified on that sighting. A node that starts moving while *unseen* is the
  accepted residual, bounded by `age_cap`; its resighting counts `reclassified_after_unseen`.
- **Capacity.** Eviction moved to the scene end: it keeps `node_reserve` (8) node slots and
  `draw_reserve` (64) record slots free by evicting the farthest unseen nodes once per frame; the draw
  site never scans the table (a sighting that finds the reserve empty is `refused` and recorded from
  its next sighting). `drop_resource` walks one node's records while a resource belongs to one node
  (`single_node`), the draw pool only for a shared mesh. Worst cases: eviction 8 × 1,024 node reads
  per scene end; orphan drop of a shared mesh 4,096 record reads per orphan.
- **Idle watchdog.** `idle_flush_frames` (300) consecutive frame begins without a scene end (menus,
  loading) flush the store (`flush=idle`, `idle_frames` on the line).
- **Orphan probe, precisely.** The `AddRef`/`Release` pair runs on the application's own binding
  identities, which under the ownership layer are its wrappers with exact reference counts (the
  fixture's own counts read through the same pointers): there the signal is exact, and the
  capability self-test decides only whether the runtime beneath reports counts. Where the ownership
  layer is absent the objects are native and the signal is the documented advisory return value.
  The self-test's buffer is created and destroyed natively within attach before any reference probe
  runs, outside `taa_call`: its device reference comes and goes inside one call, so the accounting
  never sees it.
- **Poll.** The fixture's `-live-poll` case runs the script on the positional sun (every draw uploads
  its own direction) with a source switch (context null) before case k: retained records under
  per-cascade bases and the switch flush run through the DLL.

Unverified: native Windows; the game (no census run yet), hence `eps`,
`age_cap`, the transit behaviour and whether `0x0046d080`'s batches arrive as managed draws.

## Open RE (not blocking stages 1–2)

- Whether the instanced-batch draws of `0x0046d080` reach the proxy as managed z-writing draws
  (a same-mesh, same-place double caster is harmless in a depth map but costs issues);
  census `unscoped` is the first evidence, an existing capture's declarations the second.
- Which word carries the `0x200` screen-node bit (`node+0x12c` or `+0x130`).
- LOD: where `lod` is chosen for a culled node and whether `node+0x14c` of an unseen node is
  meaningful (the retained LOD is simply the last one drawn).
- Stage 3: the simulation-side writer of `node+0xb0` / `+0xc0..`.
- Deferred with B2: the per-subset skips in `0x004c0150`'s `0x1a8`-stride loop and the
  declaration source.
