# Engine nozzles from the ship's jet nodes

Design note, 2026-10-09, for the user's decision "option 2 with live throttle from jet nodes": the hull engine light
([engine-light.md](engine-light.md)) and the plumes ([engine-effects-modern.md](engine-effects-modern.md)) stop
depending on whether the game draws a nozzle's glow. Not implemented; the main session ratifies or rejects. Marks:
**[m]** measured in this session or a named run, **[s]** static reading of the installed EXE, **[i]** inference,
**[e]** estimate.

**Symptom.** A record exists only for a glow draw the game issues. When a nozzle leaves the frustum while the hull
stays on screen, the pass clears its renderable bit and the nozzle's record, plate, light and plume vanish in one
frame. Run 139 A (run20): 27 frames with a falling light count, 23 of them with the ship count unchanged, i.e. one
nozzle's record lost while the ship kept its others [m, `verification/results/run139-triage/run139_out.txt` Q3]. The
per-ship hold of Run139 covers only a ship with no record at all.

## Recommendation

**Walk the ship root's child list from the proxy, once per drawn ship per frame, at the plume stage's append point;
no new engine hook, no stub change.** The jet nodes are direct children of the root (`root+0xc`, the list the jet
drive `0x004596e0`, the emitter site `0x00414590` and the pass itself walk), their world transform and throttle are
final before the sector view's pass, and the far-jet path already turns exactly these node fields into a record
(`engine_effects_core.h far_record` from `engine_far_jets_core.h Raw`). A node-sourced record is a far record built
by the proxy instead of the stub, flagged `flag_node`, deduplicated against the frame's draw and far records by node
handle, and appended to the same ring before the stage runs and before the light's next boundary. Ships whose root
was walked are never held; the Run139 hold stays as the fallback for roots that were not.

### 1. Where the list is gathered

The claim `0x0047d2a2` does **not** see the nozzle that matters. The pass `0x0047cfe0` tests a node before the site
and a rejected node jumps straight to the child walk at `0x0047d528`, bypassing the measure site and the claim [s,
`verification/results/engine-nozzle-source/pass_exits_out.txt`]:

| test | site | reject target |
| --- | --- | --- |
| renderable bit clear (`+0x12c & 2`, e.g. the drive's hidden flag `0x100000`) | `0x0047d085` | `0x0047d528` |
| behind the eye: `+0xf8 + +0xa0 < 0` | `0x0047d091..0x0047d0a4` | `0x0047d528` |
| view distance (`view+0x270 & 0x400`) | `0x0047d0c5..0x0047d0ea` | `0x0047d528` |
| **frustum** `0x004c6aa0` | `0x0047d0ff..0x0047d112` (`and [edi+0x12c],~2; jmp`) | `0x0047d528` |
| measure, dock-port, small-parts, lens, occlusion stubs | `0x0047d258`, `0x0047d2a2` | (reached only after the four above) |

So the frustum-culled nozzle enters the pass (its parent passed; the root walker `0x0047e780` and the recursion
visit every child of a visited node) but never reaches the claim. A stub there would need the ring entries for the
kept jets only, which are the ones that draw anyway. A new hook at the pass entry or at `0x0047b800` (the transform
walk, which does touch every node) would see them, but that is a new site with untraced liveness; the brief's
premise "every jet node is visited at the claim" is false for the case being fixed.

**Child list layout [s, the same listing].** `edi = [node+0xc]; while ([edi] != 0) { pass(edi); edi = [edi]; }`:
`+0xc` points at the first child, `+0` of a child is the next, the list ends at an element whose `+0` is 0 (a
sentinel, not a node). The root walker uses the same shape on `scene+8`. The drive walks `root+0xc` for the jets
[m, engine-effects.md §2], so every main jet is a direct child; nested parts are not jets (every engine part lacks
`F`, 4,399 / 705 parts [m, engine-effects.md §4]).

**Where the roots come from.** The route's object scope already carries, for every scoped draw, the node's block
(`object_trace` Scope: `parent` = `+0x18`, `position` = `+0xb0`, `basis`, `scale[0..3]` = `+0x70/+0x80/+0x84/+0x88`,
`flags12c/130`, `model`, `node_handle`, `camera_handle`) with no extra read. A drawn ship is any scene-phase draw
whose `parent` is non-zero (the root) or whose node is parentless (the root itself); its root goes into a per-frame
open-addressed set of 512 slots (generation-stamped like `engine_far_jets_core.h Seen`, no clear per frame) with the
draw's camera handle and the context scale derived from that draw (below). One hash probe per routed scene draw.

**Fields read per child, their validity.**

| field | read | guard |
| --- | --- | --- |
| `+0` next | 4 B | bound 256 elements (installed max 108 direct parts, p50 28, p99 76; stock max 87; jets at most 68, main 66 [m, `parts_per_scene_out.txt`]); overflow counted, the ship's partial list used |
| `+0x12c`, `+0x130` | 8 B | `+0x130 & 0x4000001 == 0x4000001` (the JET pair, else skip); `+0x12c & 0x100000` set = hidden by the drive (alternate nozzle set, not shown): skip; |
| `+0x18` | 4 B | must equal the root (back-pointer check against a reused or foreign block) |
| `+0x28` handle, `+0x140` model, `+0x260` C | 4 B each | model resolves in the body table (else `unknown_body` as the draw path); `v/00566` and SMALLJET entries skipped (RCS puffs do not need an off-screen source) |
| `+0x70`, `+0x80..+0x88` | 16 B | `+0x80 == +0x84 == 0x10000`, `0x147 <= +0x88 <= 0x90000` (idle 0.25 to brake 9.0: engine-effects.md §4) |
| `+0xb0..+0xb8`, `+0xc0..+0xe8` | 48 B | non-zero basis rows (far_record's own checks) |

One `engine_memory::read` of the node's first 0x150 bytes per child (the seam fixture's node size) is simpler than
six reads and costs the same region check; a child that is not a jet costs that one read. The reads are fault-free
(VirtualQuery-cached regions), as the far append's context and parent-radius reads are.

**Lifetime and freshness.** The drive (`0x004596e0`, under the per-sector update driver `0x0043a360`) and the
deferred-delete sweep `0x0045b660` run in the main loop's input region before the frame routine `0x00471f50`
[m, main-loop-input-region.md §1-2]; the transform walk `0x0047bc20`/`0x0047b800` writes `+0xb0` and the basis for
every node of the scene before the view's pass [s, engine-side-occlusion-cull.md §3]; the jet drive writes `+0x88`
for every root child whether or not it is visible (its walk tests the JET bit and the mode word, and the only
visibility it touches is the hidden flag it sets itself [m, §2 table; i that no test was missed]). So at any point inside
the frame routine the jet's throttle and world transform are this frame's, and no node is freed between the hull
draw that named the root and the stage append that walks it (single render thread, no free path in the frame
routine [i]). `+0xf0` (view space) is not needed; its units stay untraced.

**Rejected gather points.** (a) The claim stub appending every visited jet: misses the frustum-culled nozzle, the
case being fixed [s]. (b) A walk at the hull draw itself: inside a D3D call on the route's hot path, repeated per
part draw of the same ship unless memoised, and the ring order would put node records before later draw records,
complicating the dedupe; the stage append sees the whole frame. (c) The update-site hook `0x0045b09a`: fires only
when `z` changes, so it cannot enumerate per frame, and gives no world transform [m, engine-effects.md §4].
(d) The transform-walk or pass-entry hook: a new site, untraced liveness, for data the proxy can read directly.

### 2. Flow into the ring, dedupe, where the plume is submitted

- **Append point.** `engine_far_append` runs in `run_engine_plumes` after the frame's draws and before the scene view
  is chosen (`motion_output_engine_plumes_inc.h`); the node append follows it in the same function. The stage draws
  every ring record of the scene view in one `DrawIndexedPrimitive` pair (plume quads + discs, ribbons), so **a
  node-sourced plume needs no draw to piggyback on**: it is drawn where every plume is drawn today. The light builds
  its ship table at the next frame boundary from the whole ring (`engine_light_frame`, before the ring is cleared),
  so node records feed plates and lights with the same one-frame latency as draw records.
- **Record.** `far_record(raw, context_scale, body, entry, frame, &r)` from a `Raw` the proxy fills from the node
  block (the same construction as the stub's copy; `view_handle` = the hull draw's camera handle, `context` unused),
  then `flags |= flag_node` (a new bit) with `flag_far` clear; `camera[slot]` = that handle, `scene[slot] = 1`,
  `parent[slot]` = root, `parent_radius` through the existing memo, `own[slot]` from `engine_record_own`. Identity:
  `serial` from `object_lifetime::current(registry, node, handle, camera, camera_handle, &life)` exactly as the
  draw path (line 231 of the effects inc), so a ribbon keyed on the serial continues across draw → node → draw
  frames; without it the (handle, model) key as far records.
- **Dedupe, draw wins.** The `Seen` set (node handle, view handle) is primed from the frame's draw records (their
  `node_handle` and `camera[]`, ≤ 1,024 inserts), then the far copies insert as today, then the node walk inserts;
  a node whose handle is already in the set adds nothing (`node_duplicates`). The draw record carries the draw's
  own c4-6 rows and serial; the node record carries the same numbers by construction (flight A: 117,442 records
  matched order a), so which one wins only matters for the counters.
- **Engine-culled jets.** The far handler drops a jet the engine would cull by size (`engine_culls`, counted
  `far_engine`); the node walk cannot compute the measure. The handler keeps those (handle, view) pairs in a second
  per-frame list (≤ 1,024, integer, no other change to the stub), and the node append inserts them into `Seen`
  before walking, so a tiny jet the engine refuses is not resurrected as a plume. An off-screen nozzle is never in
  that list (it never reached the stub), which is the point.
- **Per-root result.** The walk publishes, per root, `walked`, jets found, records appended, and `live == 0` when
  every jet was hidden or refused; the light's hold reads this (§7).
- **Optional skip.** Per root a cached main-jet count from the last complete walk (root pointer + root handle +
  count); when the frame's draw records for that root already number the cached count, the walk is skipped. In
  ordinary frames every nozzle draws and no list is read at all. First build without it; add if `node_walk_us` shows
  in the stage row.

### 3. Throttle

`z = +0x88 / 65536`, `s = clamp((z − 0.25) / 1.75)`, the same law as the draw and far paths. The drive writes `+0x88`
every game tick for every root child, culled or not, rate-limited at 0.004 per ms (0.25 → 2.0 in 437 ms) [m,
engine-effects.md §2], so a throttled-down engine dims at the engine's own ramp from the next frame on, and a hidden
nozzle set (travel-mode swap, `+0x12c & 0x100000`) drops its record the frame the drive hides it. That is "dark at
once" relative to the hold, which froze the last brightness for 60 frames. `obj+0x10` (speed) adds nothing: the node
does not reach the object (no node → object field [m, §4]); for the own ship it would give the target `z` ahead of
the ramp, which the native glow does not follow either; and the plume replaces that glow. Not used.

### 4. Far jets and the view rule

A far jet (stub-culled by projected size) and a node record for the same nozzle meet in `Seen`; the far copy, inserted
first, wins and the node is a duplicate; the content is identical. Once node records exist, the far block is
redundant for every ship whose hull draws (a hull culled whole takes its jets with it, so the far path never had those
either); it stays because it carries the engine's size verdict and costs nothing when nothing is culled. It can be
retired after a flight shows `far_records` ⊆ `node_duplicates`.

View: node records are tagged with the hull draw's camera handle and `scene = 1`, and are excluded from the scene-view
tally exactly as far records are (`pick[i] = 0`), so the rule (own ship, else majority of scene-phase draws, else the
far handles) is unchanged; a node record whose handle is not the scene view's counts `skipped_other_view`, and
`build_ships` drops it as `other_view`. To avoid walking target-monitor ships, a root is gathered only from draws
whose camera handle equals the previous frame's scene camera when one is known (one frame of lag after a view
change; a frame without a known view gathers all). Sector view only, as today.

### 5. Cost

| item | per | cost | mark |
| --- | --- | --- | --- |
| root-set probe | routed scene draw | one multiplicative hash + ≤ 2 compares, ~10-20 ns; 250 draws → < 5 µs | [e] |
| child walk | drawn ship | p50 28 / max 108 reads of 0x150 B (region cache hit + memcpy), ~50-100 ns each → 1.5-3 µs typical, ~10 µs for the Xenon M2 V | [e]; the host test measures it |
| record build | jet found | `far_record` + body lookup + lifetime probe, ~0.3 µs | [e], the far append's own cost |
| frame total | 2-5 drawn ships (Run 139 A: p50 2, max 5 [m]) | 5-30 µs at the stage append, nothing per hull draw beyond the probe, no allocation (the set, the cached counts and the walk scratch are fixed arrays) | [i] |
| plume draws added | off-screen nozzle | 8 vertices + a disc in the existing batch: one or two nozzles per ship at the frame edge | [i] |

No per-draw cost beyond the probe; no device call; the stage's fixed cost is unchanged.

### 6. Native Windows

Shared code only: `engine_memory::read` (VirtualQuery + memcpy, documented Win32), the existing ring, stage and light
(D3D9). No stub bytes change for the walk; the engine-culled list is a handler-side addition in the integer-only far
handler, behind the same `jet_writer` byte pin. x86 arithmetic in the proxy under SSE2 as everywhere. Nothing
Wine-specific; the layout dependence (`+0xc`, `+0x18`, `+0x28`, `+0x70..+0x88`, `+0xb0..+0xe8`, `+0x12c/+0x130`,
`+0x140`, `+0x260`) is on the game, identical on both, and pinned by the existing site verifiers plus the back-pointer
and range guards above. Not verifiable by the user on Windows, as the rest of the feature.

### 7. The Run139 hold

Keep the code, change the rule: `hold_ships` skips any ship whose root was walked this frame (the walk result is
authoritative: no live nozzle means dark, not held), and holds only a bound ship whose root was **not** walked (its
hull not drawn in the scene view this frame, a refused or overflowing walk). Expected `held=` ≈ 0 in flight; the
option and window stay for one or two flights, then the hold is removed if `held` and `hold_expired` stay 0 with
`node_walk_failed = 0`. Removing it now would leave a ship whose list read is refused (a region not yet cached at
the first frame, or a guard rejection) to the one-frame cut.

### 8. Verification

- **Host test** (`verification/analysis/test_engine_nozzle_source.py`, over a new portable `engine_nozzle_core.h`):
  a synthetic memory image through a reader callback: root + sentinel list with a hull, a main jet that also has a
  draw record (duplicate, draw wins), a main jet without one (node record, `s` from `+0x88`), a hidden jet
  (`0x100000`), an RCS `v/00566`, a child with a wrong back-pointer, a jet in the engine-culled list; the 256 bound and
  a list whose next pointer is unreadable (stop, counted); the serial lookup path; the hold rule (walked root never
  held, unwalked root held); `walk.cost` timing over 256 roots of 108 children (ledger figure).
- **Wine fixture**: the `seam-engine-light` case of `run_motion_output.py` extended: the synthetic root gets a child
  list (hull node A, the drawn jet, a second main jet never drawn, the sentinel). Expected `engine_stage`
  `nozzles=2 node_records=1 node_duplicates=1`, `engine_light_frame` `plates` for the ship = 2, the image shows the
  second plate's light, then Reset. The cull fixture's far section gains one row: an engine-culled JET row appears in
  the handler's culled list and the stage counts it `node_engine_culled`. No change to the synthetic pass is needed
  because the stub is unchanged.
- **Flight** (one build, ini `engine_nozzle_source = node | off` for a same-save A/B if wanted): close orbit of a
  capital so nozzles cross the frame edge, as Run 139 A's run20. Pass: the Run139 Q3 script rerun shows light-count
  drops only with `ships_drawn` drops; `held = 0`; `node_records > 0` on edge frames; plumes keep their tail at the
  edge; `--perf` stage row `node_walk_us` under 30 µs p50; the user sees no cut.

### 9. Risks

| risk | handling |
| --- | --- |
| node pointer reuse | roots and children are read in the frame that drew them; frees happen in the input region [m]; the back-pointer, flag-pair and range guards reject a reused block; identity for ribbons is the lifetime serial |
| LOD meshes without jet nodes | jets are scene parts, independent of the hull's LOD records; a scene with no jet parts (71 stock TShips rows) yields `live == 0`, as today (no glow either) |
| docked or merged ships | docked ships are not drawn, so no root is gathered; a docked ship's jets are not direct children of the carrier's root [i], and the drive would not animate them either |
| own ship, first person | the hull is not drawn, so no walk, no record, as today (the pass's behind-eye test culls the own glow); the light has no hull to light. If plumes behind the camera are wanted later, the own root is reachable through the cockpit object (`cockpit+0xc → obj+0x70`) without a draw |
| more than 72 nozzles | the plate cap drops the dimmest as today; the ring cap 1,024 total; the walk bound 256 against an observed maximum of 108 children [m] |
| alternate nozzle set, travel mode | honoured through the drive's hidden flag on the node; no C-word logic re-implemented |
| a jet the engine culls by size | kept out through the handler's culled list (§2); an off-screen one is drawn, which is the requirement |
| view change | one frame of lag in the root filter; the consumers' handle filter catches the rest |
| the context scale | derived per gathered root from the hull draw's rows against its node block (`|row x| / (+0x70 · +0x80 / 65536 · |basis row 0| / 65536)`, the far construction inverted), window (0, 1), expected 0.01 [m, run406]; a draw without shadowed rows falls back to the far memo's last value, else the root is skipped and counted |

## Alternatives considered and why they lose

- **Per-plate hold** (hold each plate's last position and brightness, fading as the ship hold does): the light still
  fades late instead of following the throttle, the brightness is the last drawn one, and the plume gets nothing
  (the stage draws records, not plates). It also keeps reading the previous frame's rows.
- **Body-table positions under the hull transform** (the ship's scene part keys baked offline per TShips scene and
  transformed by the drawn hull's world rows): needs a per-ship table regenerated for every mod, the part rotation
  keys, the dynamic jet scale and the context scale, and still has no `z` (the throttle lives only in the node), so
  it would animate nothing; the live node has all of it for one bounded read.
- **Claim-stub ring of visited jets**: the frustum-culled jet never reaches the claim (§1) [s].
- **Pass-entry or transform-walk hook**: new engine site, untraced liveness, no fixture; the proxy reads the same
  memory itself.
- **Update-site hook `0x0045b09a`**: fires only on a `z` change, no transform, no per-frame enumeration.

## Unknown

- Whether any path frees or re-links a scene node inside the frame routine (none found; the sweep and the drive are
  in the input region [m]); a `scene_graph_census`-style check of `engine_nodes` before and after the stage append
  over a flight would settle it.
- Whether docked ships are attached under the carrier root as direct children (not traced; a walk count per root in
  `--debug`, `node_children_max`, against the offline 108 would show it).
- The behind-eye test's effect on the own ship's plumes in chase view with a wide FOV (the nozzle behind the camera
  plane while the hull is drawn): the walk would supply the record; whether the stage's `culled_behind` then drops it
  is to be observed.
