# Engine-side occlusion cull: skipping hidden ship parts at the cull/LOD pass

2026-10-08. Feasibility study for applying the batched occlusion cull's "hidden last frame" answer
([occlusion-cull.md](../architecture/occlusion-cull.md)) at the engine's own scene pass, the way the dock-port and
lens-flare culls skip nodes before the engine's per-object work. Offline only: `i686-w64-mingw32-objdump -d -M intel`
of the bottle-X3 `X3AP.exe` (SHA-256 `fdbf3418…f8ab`, re-checked this session; listing local and untracked), Python
over that listing, and bounded queries of the run14/run15 capture logs and their occlusion-estimate extracts (DXVK,
bottle X3). No game launch, no Wine command, no build. Scripts and outputs:
[`verification/results/engine-side-occlusion-cull/`](../../verification/results/engine-side-occlusion-cull/). Marks:
**[m]** measured in a named run or log, **[s]** static reading of the image, **[i]** inference, **[e]** estimate with
no measurement behind it.

**Answer.** Feasible, and the hook already exists: a third stub chained on the shared `cull_small_parts` claim at
`0x0047d2a2` (the dock-port and lens-flare stubs' site) can send a node to the engine's own size-cull instruction
`0x0047d2c3` when a published table says the node was hidden. The node pointer there (`EDI`) is the same pointer the
draw path logs as `object_context node=` (100 % of joined draws, [m]), and the view argument `[ESP+0x28]` equals the
draw path's `camera=` (100 %, [m]), so the lookup is one hash probe per visit, restricted to the main sector view by one
pointer compare. A ship part is visited once per frame in that view ([m]). A culled node skips the rest of the pass
(LOD selection), the whole render visit (model lookup, world matrix, light selection, texture step, submit, material
setup, D3DX parameters, technique, passes) and is re-armed for the next frame by the per-view transform walk. Expected
saving at the run14/run15 close-capital views: **0.7–1.9 ms per frame [i]**, because only nodes whose every draw is
hidden can be skipped (29–41 nodes carrying 29–58 of the 32–91 hidden part draws, [m] offline); the rest of the hidden
draws sit on partially hidden nodes. The proxy side is the larger change: the decision must be made before the hull
draws, so the "hull drew this frame" and rectangle-stability guards move to engine data (view-space position) and
engine-skipped parts must keep being tested without drawing.

## 1. The site and what a skip removes

### 1.1 Call structure per view [s]

Per ordinary view the frame routine `0x00471f50` runs (call sites `0x00472256..0x004722b5`; frame-loop-phases.md §1):
`0x0047bc20` per-view state build (calls the transform walk `0x0047b800` over the scene), `0x0047c840`
camera/viewport/**Clear** (`0x00472260 e8 db a5 00 00`), `0x0047e780` the per-scene root walker
(`0x0047226b e8 10 c5 00 00`) which calls the **cull/LOD pass `0x0047cfe0`** on every root (the pass recurses over
children from its exit `0x0047d528`), then per layer `0x0047e920` → the **render visit `0x0047d9c0`** (recursive),
the queue sort `0x0047e620` and the drain `0x0047e6e0`. So the pass runs over the whole scene of a view before any
draw of that view. Direct callers ([`call_sites_out.txt`](../../verification/results/engine-side-occlusion-cull/call_sites_out.txt)):
`0x0047cfe0` ← `0x0047d53c` (self), `0x0047e7a5` (walker); walker ← `0x0047226b` (ordinary views), `0x00472471` (lens
scene), `0x0047e8c2` (env-map `Clear(3)` helper); `0x0047d9c0` ← `0x0047e5e5`, `0x0047e600` (self), `0x0047eb32`,
`0x0047eb63`; `0x004c4fc0` ← `0x0047e076` (immediate), `0x0047e769` (deferred queue); `0x004c0150` ← `0x004c5228` only.

### 1.2 The hook site (existing claim) [s]

| item | value |
| --- | --- |
| site | `0x0047d2a2` `8b 4f 18 85 c9` (`mov ecx,[edi+0x18]; test ecx,ecx`), the shared `cull_small_parts` / `lens_flare_cull` claim (lod-selection.md "Cull small parts site"); verified window `0x0047d294..0x0047d2cc` |
| cull target | `0x0047d2c3` `83 a7 2c 01 00 00 fd` (`and dword [edi+0x12c],~2`), then `eb 05` → `0x0047d2d1` → (no `0x4000000`, impossible here: `0x0047d0a9` already exits such nodes) store and `jmp 0x0047d528` (child walk); LOD selection `0x0047d2ec..0x0047d51e` is skipped |
| reached by | nodes with bit 2 of `+0x12c` set that passed the behind-eye (`0x0047d091`), view-distance (`0x0047d0c5`), frustum (`0x004c6aa0` at `0x0047d0ff`) and distance-band tests, with model id `+0x140 >= 0` (`0x0047d19b`) |
| registers | `EDI` = node, `ESI` = the small-object measure, `[ESP+0x28]` = the view (the pass's first stack argument; the site is a `jmp`, ESP is the pass's), `[ESP+0x2c]` = `s = r·640/D`; EAX, ECX, EFLAGS dead; EDX must be preserved (reaches the caller on the cull path's return); x87 stack empty |
| reentrancy | the pass recurses (`0x0047d53c`); a stub that only reads a table and bumps a counter is safe at every depth; render thread only, the same thread that would publish the table (no concurrency) |

The chain mechanism (`cull_small_parts::chain_stub`, push-front, one restore) already carries two stubs; a third
needs no new engine bytes, window or verifier site.

### 1.3 Node and view identity [m]

- `object_trace` scopes `0x004c0150` and reports `node = args[1]`; `0x004c0150` reads `[ebp+0xc]+0x12c` (bit 2,
  `0x4000000`, `0x100000`, `0x004c0197..0x004c01b5`), the same node word the pass tests [s]. Joined by pointer in the
  same frame, 192/199, 220/227 and 231/238 scoped draws of run14 frames 4220, 6115 and 6293 have a `cull_census` row
  (the pass's `EDI`); the unjoined 7 are read as scopes the pass did not measure (early rejects or other scenes) [i]
  ([`node_level_out.txt`](../../verification/results/engine-side-occlusion-cull/node_level_out.txt), all 32 frames of
  run14/run15 agree).
- For every joined draw `object_context camera=` equals the census `view=` of that node (192/192, 220/220, 231/231;
  [`view_identity_out.txt`](../../verification/results/engine-side-occlusion-cull/view_identity_out.txt)). The main sector
  view is one pointer (`41c68200` in run14, the same over frames 4220–6293) holding 174–217 of the drawn nodes; the
  other 6–7 views with census rows hold 1–5 each.
- No mapping offset is needed: the occlusion table's key node (`scope_node`) is the pass's `EDI`.

### 1.4 Visit frequency [m]

Run14/run15 capture frames: 7–8 views carry census rows (`frame_phases views=8` in run17), 210–396 census rows per
frame, and **every sub-part node has exactly one census row per frame** (visits min = max = 1 over 82–97 part nodes per
frame). So the probe runs once per part per frame, in the sector view; nodes of other views pay one pointer compare.
(Census rows are written at the measure site `0x0047d258`; a node rejected earlier in another view leaves no row.)

### 1.5 What runs after the site for a kept part [s]

| step | where | per | content |
| --- | --- | --- | --- |
| LOD selection | `0x0047d2ec..0x0047d51e` | node | `0x004863c0` model lookup, threshold loop with `_ftol2`, adjustments |
| render visit | `0x0047d9c0` (864 insns) | node and layer | bit-2 test `0x0047d9d0` (`f6 83 2c 01 00 00 02`, `je 0x0047e5b6` = straight to the children), model lookup, fixed-point basis, the per-node cache walk (view `+0x780`/`+0x2a0` list, R4 of view-submit-hot-path.md), light selection `0x0047d5e0` (+ `qsort`, gated), `0x004bdea0`/`0x004bdee0` world matrix, `0x004f66e0` texture step, then `0x004c4fc0` or the deferred queue `0x0047b2e0` |
| submit | `0x004c4fc0` (187) → `0x004c0150` (4,691) | draw (sub-mesh) | per sub-mesh (`mesh+0x8` count, stride `0x1a8` at `mesh+0xc`, index `[esp+0xa0]`, loop `0x004c0223..0x004c4082`): `GetTechniqueByName` + `SetTechnique`, `Begin`, ~75 parameter setters, 4 `D3DXMatrixMultiply`, 2 `Inverse`, `Transpose`, 3 `sqrtf`, `BeginPass` → `DrawIndexedPrimitive` → `EndPass`, `End` |

About 2,500–3,500 engine instructions and 136 out-of-line calls per draw, plus the D3D state calls D3DX makes on the
engine's behalf (68.6 hooked calls per draw in run162) (view-submit-hot-path.md §2–3). The proxy's draw-level skip
(`MotionOutput::evaluate_draw`) removes only the native draw call and the backend's work for it; everything in the
table runs. The engine skip removes the whole table for that node.

### 1.6 What the existing engine culls skip

`cull_small_parts` (projected size; dock-port rule by model-id range) and `lens_flare_cull` (body bitmap) both replay
`0x0047d2a2..0x0047d2b9` and jump to `0x0047d2c3`: renderable bit cleared for that view and frame, LOD selection
skipped, the node's render visit reduced to the bit test, children still visited. An occlusion stub would end the same
way.

## 2. Cost and expected saving

| quantity | value | mark |
| --- | --- | --- |
| `view_submit` slope per issued draw, DXVK, no cull (run14 / run15 window fits) | 23.6 / 32.0 µs | [m] (`run138-dxvk-triage/cost_model_out.txt`) |
| same, run16 (Run137 cull) window fit | 24.5 µs per issued draw | [m] |
| saving of a proxy-skipped draw, DXVK | ~0 (Run 138 A same-build A/B, 236 → 126 issued, `view_submit` 6.63 vs 6.54 ms) | [m] |
| engine-side calibration: dock-port cull, Run 110 A (wined3d) | 2.4–3.7 ms for 27–33 culled nodes at a Raptor at 4 km; the Raptor's 33 dock-port nodes are 117 draws in run385, so ~95–117 draws → 20–38 µs per removed draw | [m] A/B, draw count [i] |
| hidden part draws, run14/run15 close-capital frames (tol 0.01) | 32–91 per frame | [m] offline estimate |
| nodes with every draw hidden / their draws | 29–41 nodes / 29–58 draws (frame 6293: 38 / 49 of 87 hidden) | [m] offline estimate |
| class of those nodes | turret parts (`split_m1turretA/B_base`, `_socket`, `weapondummy`): 28–38 nodes; dock-port 0–2 (already culled by size) | [m] (`node_level_classes_out.txt`) |
| expected saving at those frames | 29–58 draws × 24–32 µs = **0.7–1.9 ms per frame** | [i] |
| scaled to Run 138 A's 110 skipped draws | node-level share 0.56 (49/87) → ~62 draws → 1.5–2.0 ms | [i] |
| stub cost | ~6 instructions per pass visit outside the sector view, ~25–40 on a probe; 210–400 visits per frame → < 10 µs | [e] |
| forced refresh (§4) at K = 8 | −1/8 of the saving | [i] |

Hidden draws on partially hidden nodes (38 of 87 at frame 6293) are out of reach of a node-level skip. They could
only be skipped per sub-mesh inside `0x004c0150`'s loop (head `0x004c0223`, index `[esp+0xa0]`), which saves the
per-draw setup but not the node's traversal; that site was not studied further (the loop body has no recorded
liveness, and the mapping sub-mesh index → the proxy's draw key would have to be learned from draw order inside a
scope).

## 3. Ordering and reveal

- **Transforms at the site [s].** The node's world position `+0xb0..+0xb8` and its **view-space position
  `+0xf0..+0xf8`** for this frame and view are final: `0x0047b800` writes them (`0x0047b8de`, `0x0047b96a`,
  `0x0047b9f7`) from the camera's position `view+0x30` and rows `view+0x40..` before the pass. The D3D world matrix
  (`0x004bdee0`, called at `0x0047e002` / `0x0047e70c`) and therefore the clip rows the proxy sees are built later, in
  the render visit. The hull has not drawn yet in this frame (the pass precedes every draw of the view). So the
  decision at the site can use only: the latest ready test result, engine fields of the node, and the previous
  frames' proxy data. "Hull drew this frame" cannot be checked; the stability guard must be re-expressed in engine
  units, e.g. a per-entry integer window around the view-space position the node had when its tested rectangle was
  computed (six compares on `[edi+0xf0..0xf8]`). A camera cut, teleport or fast turn moves `+0xf0..` out of the window
  and the node is kept.
- **Publish point [s].** The main view's Clear (`0x00472260`) precedes its pass (`0x0047226b`); the motion route's
  scene-phase Clear is already observed (`note_scene_projection`). Polling the queries there (instead of at the first
  part draw) and publishing the node table there puts the verdict in place before the pass. Whether frame N−1's
  results are ready that much earlier in frame N is **not measured** (the gate measured readiness at the first part
  draw); an unready result falls back to age 2 or to drawing.
- **Re-visit [s].** `0x0047b800` ORs bit 2 back into every node of the scene per view (`0x0047b84b 83 8f 2c 01 00 00
  02`; also `0x0047190b` for the lens scene), so a node skipped in frame N is evaluated afresh in N+1 and drawn as soon
  as the table no longer lists it. Immediate `0x2` operations on `+0x12c` in the image: only these two `or`s and the
  tests `0x0047d062`, `0x0047d1a8`, `0x0047d9d0` (`call_sites_out.txt`).
- **Testing skipped parts.** A part skipped in the engine produces no draw, so the batcher never lists it, the block
  never tests it, and the verdict would expire after one or two frames. Full design: carry the node's entries (draw
  keys, extent boxes, `rel` to the hull) from the persistent per-draw table into the frame's list when the stub skipped
  it, and trigger the ship's block even when none of its parts draws (at the hull's draw or at the end of the sector
  view). Its `rel` stays as last drawn: rigid parts are exact, a turret that turns while skipped is tested at its old
  angle. The hidden-node set is almost entirely turret parts (§2), so a forced draw every K frames (refreshing rows
  and `rel`) is required, not optional.
- **Reveal latency.** Draw-level cull: a part revealed while the rectangle is stable is missing for one frame (two
  with an age-2 result). Duty-cycle probe (as built): one frame when the reveal lands on an engine-skip frame, two on
  a proxy-skip frame, three with an age-2 result (§5). The engine-level skip removes the
  draw-level "unstable → draw this frame" protection unless the view-space window replaces it. Firing is unaffected:
  muzzle positions come from the Components table and the node/ship transforms the scene update keeps
  (lod-child-hide.md §3); no fire-control path reads bit 2. A turret revealed after a skip shows its current
  orientation (its node transform is updated every frame by `0x0047b800` regardless of the skip).

## 4. Risks (dock-port and small-parts precedent)

| consumer | effect of an engine skip | source |
| --- | --- | --- |
| render visit, driver `0x0047e98e`, per-draw entry `0x004c01a8`, instanced batch `0x0046ce20` | node not drawn / batched that frame (intended) | lod-child-hide.md §2.2 [s] |
| lens-flare occluder list `0x00488a70` (cap 255) | a skipped part stops occluding the sun; it is behind its hull, which stays a candidate | [i] |
| `0x40000` child gate `0x0047d055..0x0047d076` | a flagged child of a skipped node is hidden too; no flagged node in runs 255/257 | lod-child-hide.md §2.3 [m] |
| collision | built at creation from record n−1; independent of bit 2 | lod-child-hide.md §3 [s] |
| targeting, brackets, fire control, muzzles | object/node transforms and Components offsets; no bit-2 reader | lod-child-hide.md §3 [s] |
| texture animation `0x004f66e0`, light selection `0x0047d5e0` | not stepped / not selected for the skipped node; resumes on reveal | [s] |
| LOD index `+0x14c` | stays 0 for the skipped frame (`0x0047d001`); read only by the child gate | [s] |
| shadows | the proxy's shadow replay sees no draw: a hidden part casts no sun shadow while skipped (same as the draw-level cull) | occlusion-cull.md [i] |
| env-map and other views | excluded by the view-pointer compare | §1.3 |
| node address reuse | a freed node's address reused by a new node within the entry's life: the entry should carry the model id (`+0x140`, dereferenced by the pass before the site) and a frame stamp; a false hit skips one frame | [i] |
| sound | no audio path reads `+0x12c`; not traced beyond the bit-reader scan | [i] |

Run 110 A flew the dock-port engine cull without a reported side effect beyond the intended pop (cull-small-parts.md
"Dock ports: flight Run 110 A"); the small-parts cull has flown as the default since Run 43 B.

## 5. Verdict and the smallest test build

Feasible. Engine side: one stub of about 120–160 bytes chained on the `0x0047d2a2` claim, integer only, no call, no
Win32 (`cmp [armed],0; je next`; `[esp+0x28]` against the published sector-view pointer; multiplicative hash of `EDI`
into a power-of-two open-addressed table, at most 8 probes; on a hit compare model id, frame stamp and the six-word
view-space window; then the usual replay of `0x0047d2a2..0x0047d2b9`, `inc [count]`, `jmp 0x0047d2c3`; EAX/ECX only,
EDX untouched). Proxy side: node-level verdicts (a node is listed only when every one of its draw entries read hidden,
ready, and its hull drew in the tested frame), the table published at the sector view's Clear, carry-over of skipped
nodes into the blocks, a block trigger that does not need a part draw, and a forced refresh draw every K frames.

Smallest build that tests the premise (the saving), without the carry-over: **duty-cycle probe**. The stub skips a
node in frame N only when the proxy skipped all of that node's draws in frame N−1 (and its view-space position is in
the window). The node then draws in N+1 (the table is rebuilt from N's proxy skips, which did not include it), is
decided there by the existing draw-level logic from the N−1-list test, and is engine-skipped again in N+2: every hidden
node alternates proxy-skip and engine-skip, so the existing listing and testing keep running unchanged, reveal latency
is one frame when the reveal lands on an engine-skip frame and two when it lands on a proxy-skip frame (that frame's
skip rests on a test issued before the hull changed and the next table is built from it; three with an age-2 result;
fixture-observed 2026-10-08, occlusion-cull.md "Engine-side skip"), and the measured saving is about half the full
design's (0.35–1.0 ms at the run14/run15 views [i]).
The alternation needs the ship's block in the engine-skip frame, i.e. at least one part of that ship drawn then; a
ship whose hidden parts all phase-lock gets no block in that frame, its next decision has no result for them and they
draw until a test lands (a saving loss, not an artefact; the probe's `ready_age` and per-ship block counts show it).

A fixture can prove the stub (registers, EDX, flags, x87, LastError, the replay, refusals, the chain with the other two
stubs on the synthetic pass of `cull_small_parts_fixture.cpp`), not the saving. The flight must log, per frame under
`--debug` and as 300-frame session totals: nodes published, nodes and draws the stub skipped (draws from the table's
per-node draw count), window rejections, table overflow, the sector-view pointer changes, `ready_age` of the
decisions, and `drawn_late`; with `frame_phases` (`view_submit_p50`, `views_p50`, `dt_p50`) and the draw/issued counts.
Shape as Run 138 A: one build, engine probe on vs off with the draw-level cull on in both, same save and close-capital
orbit, compared at matched app-draw windows; the per-skipped-draw saving is Δ`view_submit` / Δ(engine-skipped draws),
expected 20–38 µs (wined3d calibration) or 24–32 µs (DXVK slope). The user watches turret edges while orbiting for one-frame pops.

## Unknown

- Whether frame N−1's query results are ready at frame N's sector-view Clear (readiness was measured only at the first
  part draw).
- The DXVK per-draw saving of an engine skip itself: 20–38 µs is the wined3d Run 110 A calibration and 24–32 µs the
  DXVK `view_submit` slope, not a DXVK A/B of an engine cull.
- The units of `+0xf0..+0xf8` (fixed-point scale) for the window; written by `0x0047b800` from `view+0x30`/`+0x40`,
  scale not traced.
- How far a turret turns in K frames relative to its rectangle (sets K).
- The sub-mesh site inside `0x004c0150` (liveness, key mapping) for partially hidden nodes.
- Views other than the sector view that draw ship parts (cockpit `*0x00608518+0x64` scene, env-map faces) were not
  sampled with ships in them.

## Reproduce

```sh
EXE="$HOME/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/X3AP.exe"
i686-w64-mingw32-objdump -d -M intel "$EXE" > <scratch>/full.s                       # local, untracked
python3 verification/results/engine-side-occlusion-cull/call_sites.py <scratch>/full.s \
  > verification/results/engine-side-occlusion-cull/call_sites_out.txt
# run14/run15 extracts: verification/results/occlusion-cull-estimate/extract.py + analyze.py (local outputs)
python3 verification/results/engine-side-occlusion-cull/node_level.py <run14.json> <run14_draws.jsonl> 0.01 [--classes]   # -> node_level_out.txt, node_level_classes_out.txt
python3 verification/results/engine-side-occlusion-cull/view_identity.py \
  /tmp/x3-bottleX3-run14/session-20261008-142814-244.log 4220 6115 6293
# objdump windows read: 0x0047cfe0..0x0047d551, 0x0047d9c0..0x0047e617, 0x0047e6e0..0x0047e77c,
#   0x0047b800..0x0047bc1c, 0x004721a0..0x004722d0, 0x004c0150..0x004c0260, 0x004c4060..0x004c40a0, 0x00469ab0
```
