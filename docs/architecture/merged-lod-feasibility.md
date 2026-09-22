# Merged far-LOD bodies: offline "optimize" tool or run-time batching?

Read-only feasibility study, 2026-09-22. No game launch, no Wine command; the
bottle was read only. Inputs: the installed archives (`01..13.cat` root,
`addon/01..04.cat`), the installed `X3AP.exe`
(`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`, base
`0x00400000`, decoded locally with capstone from the PE section table), and the
run240/run242 logs (`/tmp/x3-bottleX3-run240/session-20260922-185208-216.log`,
`/tmp/x3-bottleX3-run242/session-20260922-190129-216.log`). Extracted asset
bytes and decoded instruction dumps stay in the session scratchpad, untracked.

**Recommendation: neither of the two proposals in the brief. Both are dead for
the reason the numbers below establish — the stand's draws are not far bodies
and are not redundant. The lever that the same evidence opens is an asset-side
*coarse-LOD* pilot (add LOD records to the offending bodies), and it is gated on
one free measurement (`--lod-scale 0.25`) plus one cheap diagnostic (log the
per-model LOD count).**

## 1. What the engine actually does with the selected LOD index

The chain from `node+0x14c` to `DrawIndexedPrimitive`, decoded this session:

| site | instruction | meaning |
|---|---|---|
| `0047d2ec..0047d303` | `mov eax,[edi+0x140]` / `call 0x4863c0` / `mov ebx,eax` | node's model id → model pointer (`ebx`, also `[esp+0x14]`) |
| `0047d321` | `movsx ebp, word ptr [ebx+0x10]` | **LOD count is a signed word at `model+0x10`** |
| `0047d42f..0047d440` | `mov ecx,[esp+0x14]` / `mov edx,[ecx+0xc]` / `lea ebx,[edx+ebp*4]` / `mov eax,[ebx]` | **`model+0x0c` is an array of LOD-record pointers, stride 4, indexed by LOD** |
| `0047d442..0047d44b` | `fild [eax+0x34]` / `fmul [ecx+0x760]` | the threshold `LODrec+0x34` (int) times the global scale |
| `0047dfe0..0047dfed` | `mov ecx,[ebx+0x14c]` / `mov eax,[edx+0xc]` / `mov esi,[eax+ecx*4]` / `mov [esp+0x1c],esi` | the **selected LOD record** is picked here and handed to the world-matrix builder `0x004bdee0` and into the draw-queue entry |
| `0x004c4fc0` `+0x64` → `0x4c5228` | `call 0x4c0150` with arg1 = queue entry `+0x64` | submission gets that record |
| `004c0207` | `cmp word ptr [edi+8], bx` … `jle 0x4c4088` | **subset count is a word at `LODrec+0x08`** |
| `004c022a..004c023d` | `mov ecx,[ebp+8]` / `imul edx,edx,0x1a8` / `mov ecx,[ecx+0xc]` / `lea edi,[edx+ecx]` | subset array at `LODrec+0x0c`, **stride `0x1a8`**, one iteration per subset |

One `DrawIndexedPrimitive` per subset (`mesh-buffer-rewrite.md` §2: the draw does
`SetStreamSource(0, record+0x0c)` → `SetVertexDeclaration(record+0x18)` →
`DrawIndexedPrimitive`, and `0x004bb470` creates one `D3DXCreateMesh` VB+IB per
subset). **So both the geometry and the number of draws are per-LOD-record
fields**: a coarser LOD is not only a smaller mesh, it is a different, usually
shorter, subset list.

**Selection rule (corrected 2026-09-23, [lod-selection.md](../reverse-engineering/lod-selection.md)
"What the selection really does, end to end").** The loop `0047d429..0047d46e`
takes the highest `i` in `1..n-1` with `s < trunc(T_i·f)` (else 0), but that is
not the drawn index: a shared tail adds `+1` in a view with `view+0x270 &
0x1000000`, otherwise `-1` when View Distance is Very High (`cfg+0x768 >= 3`, the X3
bottle's setting), forces 0 at `cfg+0x768 > 3`, and clamps to `[0, n-1]`. So in the
main view at Very High the last record of every body is never drawn, and a
two-LOD body always draws LOD 0. No record flag, group count or "far record"
marker is consulted.

Side effects that touch the *node set*: at `0047d4d7..0047d51e`, a node whose
*final* LOD (after the tail above) equals `count-1` (its coarsest) **and** which carries
`node+0x12c & 0x8000` has its renderable bit cleared — the asset can mark a
detail part to disappear at the coarsest level, and `0047d055..0047d076` then
takes its `0x40000`-flagged children with it. The recursion into `node+0xc` at
`0047d528` is otherwise unconditional: **a coarse LOD never substitutes a merged
body for several child nodes.** Each node keeps its own model and picks its own
LOD independently.

Measured confirmation, run240 frame 5783, model `000053ab` on two nodes in the
same frame: LOD 0 → **17 draws / 16 011 primitives**; LOD 3 → **1 draw / 467
primitives** (matches the capture joins already tabulated in
`lod-selection.md`).

## 2. Asset side: how LOD is stored

Bodies live in the numbered archives as `.pbd` (text `.bod`) and `.pbb` (binary
`.bob`); archive members are XOR `0x33`, then gzip (PCK members additionally
carry a one-byte key, `data[0] ^ 0xc8`, already implemented in
`tools/analysis/sector_fog_census.py`). Census of the installed archives:
**1958 `.pbd` + 1726 `.pbb`** members (1763 + 1635 distinct paths), under
`objects/ships` 1152, `objects/stations` 809, `objects/environments` 640,
`objects/effects` 369, `objects/v` 324, `objects/cut` 218.

`.pbb` is a tagged container with explicit close tags:

```
BOB1 INFO … MAT6 <u32 material count> … /MAT
  BODY <u16 LOD count> <u32 object size> <u32 flags>
    POIN <u32 points> …  /POI  PART <u32 1> <part payload> /PAR
    <u32 threshold> <u32 0>          <- LOD switch value for the LOD that follows
    POIN … /POI PART … /PAR   (repeated, one pair per LOD)
  /BOD
/BOB
```

Parsed examples (values are the file's, big-endian):

| body | materials | LODs | points per LOD | thresholds |
|---|---|---|---|---|
| `objects/stations/station_scenes/others/argon_L_solarpowerplant.pbb` | 49 | 5 | 224132 / 122190 / 57127 / 9029 / 14094 | 250, 150, 80, 30 |
| `objects/stations/x3ap/others/xtc_teladi_eqd_ring1b.pbb` | 29 | 4 | 5008 / 2989 / 1747 / 1129 | 50, 24, 13 |
| `objects/stations/x3tc/torus_backgroundtraffic_dots.pbb` | 1 | 1 | 12 | — |

The `u16` after `BODY` is exactly the `word [model+0x10]` the pass reads, and the
inter-LOD `u32`s are the `LODrec+0x34` values the ladder multiplies by
`*(0x606f34)+0x760`. This is the first time actual `LODrec+0x34` values have
been read (`lod-selection.md` listed them as unknown). The solarpowerplant's
per-LOD part-payload header also falls from 25 → 18 → 11 → 3 → 1, consistent
with the measured 15-draws-to-1-draw collapse at the coarsest level.

**The engine already supports exactly what the brief proposes.** A body's LOD
ladder is a per-body list of independent meshes with their own material/subset
lists and their own switch thresholds; the shipped station bodies already
collapse to one merged subset at the coarsest level. There is nothing to add to
the engine; the question is only whether particular assets use it.

Scene files (`*_scene.pbd`) are the node trees: `P <idx>; B <body>; [C <slot>;]
N <name>;` followed by one transform line per animation key,
`{ flags; x; y; z; angle; ax; ay; az; time; ?; } // parent`. Census of the 221
station scenes: median **7 parts**, median 60 % of parts with a single key
(static); the extremes are `x3tc/terran_torus_scene.pbd` (554 parts, 27 distinct
bodies, 501 parts animated) and `x3ap/others/xtc_teladi_eqd_scene.pbd` (145
parts, 139 static, 8 bodies). Parts carrying `C <slot>` are component
attachment points (turret/weapon bodies substituted at run time from
`types/components.pck`) and can never be merged; heavy instancing is normal
(439 of the torus's 554 parts are the same 12-point body).

Overlay mechanism: the EXE's format strings are `%02d.cat` and
`addon\%02d.cat`, plus `addon\mods\%s.cat` and an `addon\mods\*.cat` scan. Root
has `01..13.cat` (next free `14`), `addon` has `01..04.cat` (next free `05`);
`addon/mods/` does not exist. `Assets` in `tools/analysis/sector_fog_census.py`
already models the precedence the game uses (later layer wins, `addon/` key
beats the base key, `.pbd`↔`.bod` and `.pbb`↔`.bob` are one logical resource, a
plain uncompressed payload passes through `unpack()`). So an overlay is
feasible with originals untouched, and a pilot may ship **plain uncompressed
`.bob`** members.

## 3. Where the run240 stand's 448 draws really are

Joining `object_context` (node, model, lod) with `draw` (primitives) and
`cull_census` (`s`, `d`, radius) on `(frame, index)`, run240 frame 5783 (the
same numbers in all 8 census frames):

- **448 draws come from 58 nodes**, not 87 (`kept=87` counts census rows, some
  of which never reach a main-view draw). Draws per node are contiguous
  (61 index runs for 58 nodes).
- **400 of the 448 draws are at LOD 0** on 54 nodes; 46 draws at LOD 2 on 2
  nodes; 2 draws at LOD 3 on 2 nodes. run242 (px 4) reproduces it: 397 draws,
  **381 at LOD 0** on 56 nodes, 15 at LOD 2, 1 at LOD 3.
- The distribution is extremely skewed: 29 nodes draw once (29 draws total),
  while the top nine draw 36, 35, 33, 32, 32, 32, 31, 31, 25 = **287 draws, 64 %
  of the frame**.
- Those heavy nodes are *not* close. With `px = s·0.8` at this stand: node
  `31689c60` 36 draws at `s=33` (26 px), `316899e0` 32 draws at `s=21` (17 px),
  `50451d20` 25 draws at `s=3` (2.4 px), `381294c0` 10 draws at `s=4` — **all at
  LOD 0**.
- Cumulative, over the 48 drawn nodes with a real `s` (427 draws; the other 21
  draws are on `s=0x7000000` camera-local nodes): `s≤5` → 72 draws / 9 nodes;
  `s≤20` → 115 / 24; `s≤40` → **269 / 32 (60 % of the frame under 32 px)**;
  `s≤80` → 332 / 36. run242 after the px-4 cull still has 261 of 373 draws on
  nodes ≤ 32 px.

So the premise "far stations and ships set the draw count, so merge their far
LOD" is false at this stand in a specific way: the objects *are* far and small,
but the engine is drawing them at **LOD 0 with 25–36 subsets each**. Either
those models have one LOD record (`word [model+0x10] == 1`, so `ebp = 0` and the
threshold loop never runs) or their finest threshold is smaller than `s ≈ 3`. A
laddered body would not be here: `xtc_teladi_eqd_ring1b` has `T_1 = 50`, so any
`s < 50` already selects LOD ≥ 1. Nothing in this evidence separates the two
cases — that is the one measurement the next step needs.

Not every heavy node is a missing ladder. Node `39ba7a20` (model `00005592`,
`s = 4`) is at **LOD 2** and still emits 31 draws / 2852 primitives, and
`31d5ed38` (model `0000547a`) is at LOD 2 with 15 draws. Those are bodies whose
coarse levels were authored without collapsing the material set — the same
defect, needing the same fix (replace the coarse LOD records), but not detectable
by a LOD-count check alone.

Related correction to run240's own conclusion: the `--cull-small-parts-px 3/4`
estimate there counted *census rows* (3 and 5 per frame) and multiplied by a
per-draw cost. Rows are nodes, and nodes carry 1–36 draws. The nine nodes at
`s≤5` in run240 carry **72 draws**, and run242 (threshold 6) indeed leaves no
drawn node below `s=6`. Draw counts per culled node, not row counts, are the
right unit for that lever.

## 4. Run-time batching: measured dead

`DrawBatch`/`DrawKey` in `src/proxy/frame_timing.h` define batchability as
consecutive draws that agree on VS, PS, textures 0–3, stream 0, index buffer and
vertex declaration; `same_mesh` additionally requires the identical primitive
range (only constants differ — the instancing candidate), `same_mesh_any_range`
the same buffers with another range, `same_material` the same shaders and
textures across different buffers. Run 91 busy window:
`same_mesh=14677 same_mesh_any_range=0 same_material=17644 draws=292841`
= 5.0 % / 0 % / 6.0 % (`sampling-profiler.md`).

What breaks it, from run240 frame 5783:

- **Shaders do not break it.** 334 of 447 consecutive draw pairs (74.7 %) share
  both VS and PS. Each heavy node uses exactly 1 VS and 1–2 PS for all of its
  31–36 draws.
- **Textures break it.** Every draw binds **7 texture stages**. Node `31689c60`
  (36 draws) uses 35 distinct stage-0, 29 stage-1, 29 stage-2 and 25 stage-3
  textures; `33ba6b80` (35 draws) 34/28/28/24; `3241e198` (17 draws)
  17/15/15/13. Within a node, distinct texture tuples = distinct draws.
- **Buffers break it.** Distinct stream-0 vertex buffers within a node = number
  of draws (36 of 36, 35 of 35, …), which is exactly what `0x004bb470` builds:
  one `D3DXCreateMesh` VB+IB per subset. `same_mesh_any_range = 0` is a
  structural consequence, not a coincidence — the engine never draws two ranges
  of one buffer.
- Collapsing all 448 draws by `(vs, ps, textures 0..3)` *within* each node gives
  **444** draws. Across the whole frame it gives 188, but that requires merging
  geometry across different objects with different world matrices.

So there is no run-time merge, instancing or constant remap available: the
engine's draws are already one per unique material, each with its own buffer and
its own four textures. A proxy-side merge would have to (a) build and keep
merged VB/IB per LOD record, and (b) atlas 25–35 distinct texture sets per body
and rewrite UVs — i.e. re-author the material set, not batch draws. Instancing
through the already-transformed vertex programs does not help either: the
instancing candidate is `same_mesh` (identical buffers, only constants differ),
measured at 5.0 % with a ceiling of 1.1 ms (`engine-frame-time.md` §2.7), and
the 36 draws of one node are 36 different meshes, not 36 instances of one.
**§2.7 stays closed; this study adds the reason, not a new bound.**

## 5. Offline merge of static parts: small and expensive here

If every static part of a station section collapsed into one body per material
at the far LOD, the ceiling at the run240 stand is the number of *nodes* removed,
not draws: 29 of 58 drawn nodes draw exactly once (29 draws, 6.5 %), and merging
those into their parents removes at most 29 draws minus the merged bodies'
own subsets. The 287 draws of the top nine nodes are *inside* single bodies and
are untouched by part merging — they need either a coarse LOD (§6) or a texture
atlas. Merging also costs the engine's per-node culling: at this stand the
cull/LOD pass considers ~497 nodes per frame and keeps 87
(`culled_size` ~211, `culled_min` 154, `culled_small` 46), and a merged section
is one node with one radius, one LOD decision, one frustum test and no
per-part small-object cull. Per-part animation (median 40 % of station-scene
parts have more than one key), `C <slot>` component parts, damage and turret
state are all per-node and cannot be baked.

## 6. What the evidence actually recommends

1. **Free first measurement, no code:** fly the same stand with
   `--lod-scale 0.25` (already implemented, `docs/architecture/lod-scale.md`;
   factor < 1 multiplies every `LODrec+0x34` up, biasing coarser) and compare
   `draws_p50` and the per-node join above. If the heavy LOD-0 nodes move off
   LOD 0, they have a ladder with tiny thresholds and the fix is a threshold
   change; if `draws_p50` barely moves, they are single-LOD bodies and only new
   LOD records can help. This distinguishes the two cases in one flight.
2. **Cheap diagnostic:** extend the cull census with `word [ebx+0x10]` (LOD
   count) and the `LODrec_i+0x34` values, read at `0047d321` where `ebx` is
   already the model pointer and `[esp+0x14]` holds it. This is inside the
   existing `0x0047cfe0` claim window, integer-only, no new call into the
   engine, and it turns "model `000053a0` draws 36 times at 26 px" into a named
   asset defect. It also closes the model-id-to-asset gap indirectly.
3. **Then, if (1)/(2) name the defective bodies** (missing ladder, tiny
   thresholds, or a coarse level that never collapses its material set): an
   `addon/05.cat`/`.dat` overlay (or `addon/mods/<name>.cat`) that adds coarse
   LOD records to those specific bodies. Upper bound at this stand: the eight
   heavy nodes at `s ≤ 90` carry 252 draws; shipped station bodies collapse to
   1–3 subsets at their coarsest level, so 252 → roughly 8–24 draws, i.e.
   **−230…−244 draws, ~52–54 % of the frame** — the same order as the LOD-0
   share (400 of 448). This is an upper bound: it assumes every one of those
   bodies is fixable and that a coarse level is visually acceptable at 2–90 `s`.

### Ladder diagnostic (built 2026-09-22)

Item 2 is built. The census rows end in ` lods=<n|-> thr=<t0,t1,…|->`: the
count word `model+0x10` and `LODrec_i+0x34` for `i < min(lods, 8)`, where `t1`
is the LOD 0 → 1 switch value and `t0` is never compared. The model pointer is
not read at `0047d321`, which is not a census site. The exit stub passes EBX and
the frame slots `[esp+0x14]`/`[esp+0x10]`, and the handler keeps EBX only when
it equals the model slot and differs from D. The ladder is then read at Present
through the bounded `engine_memory::read`. Rows the pass culled before the
model lookup (`0047d2e7`) or that had no model carry `lods=- thr=-`. Details,
the pinned writer sets and the verifier checks are in
[lod-selection.md](../reverse-engineering/lod-selection.md), "LOD ladder
fields". The per-model report is `python3 tools/analysis/draw_accounting.py
<run dir> --ladder [--frame F] [--view V] [--json]` (no depth readback needed).
It lists the models of one census view sorted by draws, each with `lods`,
`thr`, the selected LODs `lod:nodes`, nodes, kept, draws and the kept nodes'
`s` range. It flags `no_ladder` (lods == 1) and `lod0_below_t1` (a kept node at
LOD 0 with `s < t1`: a view-distance or `--lod-scale` bias, or the adaptive
rescale). A flight with `--cull-census` at the run240 stand therefore separates
the two cases in "Unknown" below per model. Status: site verifier 21/21, host
tests and the fixture build pass. The Wine CPU fixture and a flight have not
run yet.

### Overlay tooling (2026-09-23)

`tools/analysis/bob1.py` is the production BOB1 reader/writer (layout of
[body-format-bob1.md](../reverse-engineering/body-format-bob1.md); `CUT1` scene
members are recognised and skipped). Over every installed `.pbb` it round-trips
1634 of 1635 `BOB1` bodies byte for byte and rejects the truncated khaak hive
body explicitly (`verification/results/bob1-format/bob1_module_roundtrip.py`).
`tools/analysis/lod_overlay.py` builds the pilot overlay: for each named body it
copies the coarsest LOD (points, part flags, per-group 7-int records and the 10
part ints copied, nothing recomputed), collapses each part's groups into one
group with the part's dominant material by face count, and appends it as a new
LOD with threshold `T` (default: coarsest threshold // 2; single-LOD bodies need
`--threshold`, and `T` must stay below the current coarsest threshold). The
output is `addon/NN.cat`/`.dat` with `NN` one past the highest installed addon
slot (`addon/05` today; `--slot` must equal it unless `--force-slot`). The
engine's resolver is now traced ([body-format-bob1.md
§7](../reverse-engineering/body-format-bob1.md#7-which-file-wins-extension-and-archive-precedence)):
a loose file wins, otherwise the highest-numbered catalogue holding the name
under any body extension, with extension order applying only within that layer.
So the new slot overrides the shipped member without any mod selection; the
member keeps the winning member's exact archive path, and a body whose winning
resource is loose is refused. `addon/mods/` is not used because a mod package is
searched only when selected in the launcher.

**Placement correction (2026-09-23).** At View Distance Very High (this bottle)
the main view never draws the record at index `count-1`, so an appended record is
never shown there: appending only lets the old coarsest record draw below
`T_new·f`, and the "index one past the shipped ladder" acceptance below cannot be
met. The new record must sit at index `<= count-2`: inserted before the last
record, or appended together with a pad copy after it
([lod-selection.md](../reverse-engineering/lod-selection.md) §4 of the 2026-09-23
section gives both layouts' effects). The paragraph below holds only at View
Distance Low..High; at Very High the hide never fires in the main view.

Hide-at-coarsest (§1, `0047d4d7`): a node with `node+0x12c & 0x8000` is not
rendered at its body's coarsest LOD. The appended record becomes the coarsest, so
flagged nodes hide only below the new threshold and the old coarsest record now
draws them in the band between the new and old thresholds, where they used to be
hidden. `--keep-coarsest-hidden` instead gives the new record the old coarsest
threshold: it covers exactly the old coarsest range (the old record is never
selected), so hidden ranges are unchanged. Either way the pilot flight must check
flagged nodes. Every installed CAT/DAT is hashed before and
after a real run (outputs are removed if any changed), and an
`addon/NN.x3m-lod.json` manifest records the source and overlay hashes; the
tool refuses to run while such a manifest is installed.

`python3 tools/analysis/bob1.py audit [--summary] [--json OUT]` classifies every
installed body's ladder (`verification/results/bob1-format/bob1_audit_out.txt`,
measured over 1634 parsed `BOB1` bodies; classes overlap): **684 single-LOD**,
**15 non-monotonic** (a later record's threshold ≥ an earlier one's, so under the
`0047d429` walk 37 records are never *selected*; 10 of the 15 are stations, e.g.
`argon_dock_center` 30/10/3/30 never selects LOD 1–3 and pops from LOD 0 to a
1-group record wherever LOD 1 would apply: record 4 at View Distance Low..High,
record 3 at Very High. Correction 2026-09-23: "shadowed" must be taken per
setting after the `+1`/`-1` tail; 11 of the 15 lose records 1–2 in every
main-view setting, the other 4 are drawable at Very High, and at Very High the
last record of all 950 multi-LOD bodies is never drawn in the main view —
`verification/results/bob1-format/lod_drawn_sets_out.txt`), and **777 whose coarsest LOD draws more than one group** (551
of them multi-LOD). 392 multi-LOD bodies fall in none of the three classes.

```sh
python3 tools/analysis/bob1.py info objects/stations/station_scenes/others/argon_L_solarpowerplant
python3 tools/analysis/lod_overlay.py --dry-run <body> [--threshold T]
python3 tools/analysis/lod_overlay.py --out <scratch dir> <body> [--threshold T]
python3 tools/analysis/lod_overlay.py --install <body> [--threshold T]   # game dir; never overwrites
PYTHONPATH=verification/probe /usr/bin/python3 -m unittest verification.analysis.test_bob1
```

Removing `addon/NN.cat`, `.dat` and `.x3m-lod.json` reverts the install. The
pilot flight must show, at the run240 stand with `--cull-census`, the overlaid
body's nodes reporting the new `lod` index (one past the shipped ladder) and
their per-node draw count dropping to the part count of that record, with
`draws_p50` compared at the same stand. A file that loads without a `lod` or
draw change is not acceptance. Not yet possible: the heavy stand bodies are
unnamed until the census logs model id to body name, and both worked examples
(`argon_L_solarpowerplant`, `argon_dock_center`) already end in a one-group
LOD, so collapsing them saves no draws.

### Effort against existing machinery

- Archive read/write: mostly present. `tools/analysis/inspect_x3.py`
  `read_catalogue` decodes the CAT directory; `sector_fog_census.py` has
  `unpack()` (XOR `0x33`, gzip, PCK key), `Assets` with the real overlay
  precedence, `body_metadata()` for text `.bod`, and a CAT/DAT **writer**
  (`catalogue()`, currently inside `self_test`, six lines to promote). No
  `.pbb`/`.bob` parser exists (`sector_fog_census.py` returns
  `unsupported_binary_body=True`).
- Missing: a BOB1 reader/writer. The container is simple (4-char tags, explicit
  `/TAG` closers, big-endian counts), but the `POIN` and `PART` payloads were
  not decoded in this study — `POIN` is ~38 bytes per point with a 2-byte
  record prefix (`00 1b`), and the `PART` payload's group/face layout is not
  established. Decoding the two payloads well enough to *emit* a valid coarse
  LOD is the real work: estimate **12–20 h** for a read/round-trip-verified
  parser (round-trip byte equality on a sample of the 1635 `.pbb` paths is the
  acceptance), plus 6–10 h for decimation + material collapse, plus the overlay
  build. Writing text `.bod` instead is cheaper to emit but the affected
  station/ship bodies ship as `.pbb`, and mixing formats is safe only because
  `canonical()` treats them as one resource — the engine-side equivalence was
  not verified this session.
- Alternative with no new format work: reuse an *existing* coarse LOD. If a
  defective body has a sibling body that is already a low-poly variant, the
  overlay can point the scene's `B` reference at it. Cheap, asset-specific, no
  BOB writer, but it changes silhouettes at all ranges.

### Fixture that would prove the first step

`--lod-scale 0.25` at the run240 stand, `--cull-census --frame-timing`, F8
burst; accept on `draws_p50` at the same stand and on the per-node join
(`object_context` × `draw` × `cull_census`) showing the LOD-0 nodes with ≥25
draws moving to a non-zero LOD. No build, no install, one flight. The asset
pilot's own fixture, later, is one merged/extended body in `addon/05.cat` with
the original archives byte-identical (hash before/after) and `draws_p50` at the
same stand; the engine accepting the overlay is proven by the node's `lod` and
draw count changing, not by the file loading.

## Unknown

**Update 2026-09-23:** the `POIN` and `PART` layouts, the tag dispatch and the LOD/subset population path are now established in [body-format-bob1.md](../reverse-engineering/body-format-bob1.md) (parser `0x00481aa0`; a reader round-trips 1634 of 1635 installed BOB1 bodies byte for byte, `verification/results/bob1-format/bob1_roundtrip.py`). Of 1635 bodies, 684 ship a single LOD record; among the 950 multi-LOD bodies the coarsest record has fewer groups in 511 and the same count in 438. The items below that concern those layouts are closed; the remaining unknowns for emitting a coarse LOD are listed in that note.

**Update 2026-09-23 (later):** the model-id-to-asset mapping and the `.pbb`/`.bod` override order are closed in [body-format-bob1.md](../reverse-engineering/body-format-bob1.md) §6–§7: ids ≥ 20000 are dynamic body-table slots (`0x0046e400`, registration order, persisted in the savegame), the name is a `char*` at slot `+0x0c` of `*(0x00608518)+0xbc`, readable at Present; the resolver `0x004e7590` takes a loose file first, else the highest catalogue holding the stem, with the extension rank applying only inside that catalogue, so the pilot overlay goes into `addon/05.cat` as `.pbb`.

- Whether the heavy LOD-0 nodes are single-LOD bodies or bodies with tiny
  thresholds. Everything in §6 depends on this and nothing here settles it.
- The model-id-to-asset mapping. The EXE builds body names with `v\%05d` and
  `cut\%05d` (`0x15fc08`); `objects/v` holds 318 numeric members with ids
  0…12010 and `objects/cut` 205 with ids 641…8410, while the captured model ids
  are 20463…22161. `types/bodydata.pck` (419 rows) and `types/cutdata.pck`
  (43/45 rows) name only the exceptions and contain none of the captured ids.
  Inference, not established: named bodies referenced from scene files
  (`B stations\docks\argon_dock_center`) are registered into the same
  `0x004863c0` hash with ids assigned above the numeric range at load time, so
  the mapping is runtime-only and has to be logged, not computed.
- The `POIN` and `PART` payload layouts (see the effort note above).
- Whether `arg1` to `0x004c0150` can ever be a record other than
  `model+0x0c[node+0x14c]`. The write at `0047dfe9` and the queue field
  `+0x64` are the only path traced; the queue-entry writer for `+0x64` was not
  disassembled.
- Whether the engine accepts a text `.bod` override for a body that ships as
  `.pbb` (the offline tools treat them as one resource; the engine side was not
  checked).
- No patch, overlay, tool or flight proposed here has been built or measured.

## Reproduce

Archive and body structure (decoded members stay local):

```sh
python3 - <<'PY'
import sys; sys.path.insert(0,'tools/analysis')
from inspect_x3 import read_catalogue   # CAT directory; members are XOR 0x33 then gzip
PY
```

Per-model LOD ladder joined with the draws (a `--cull-census` run, streamed):

```sh
python3 tools/analysis/draw_accounting.py <run dir> --ladder
```

Per-node join on a session log (no file over 50 KB is read whole):

```sh
python3 - <<'PY'
# object_context (node, model, lod) x draw (primitives) x cull_census (s, d, radius)
# keyed on (frame, index); see section 3 for the resulting tables.
PY
```

Instruction windows were decoded with capstone against the installed EXE using
the PE section table (base `0x00400000`): `0x0047d2d1`, `0x0047d400`,
`0x0047d4d7`, `0x0047dfd0`, `0x004c0150`, `0x004c0200`, `0x004c4fc0`,
`0x004c51c0`.
