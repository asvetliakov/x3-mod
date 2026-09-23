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
part ints copied, nothing recomputed), collapses each part's groups into an
opaque and an alpha group (collapse below), and places it as a new record
(placement below). The output is `addon/NN.cat`/`.dat` with `NN` one past the highest installed addon
slot (`addon/05` today; `--slot` must equal it unless `--force-slot`). The
engine's resolver is now traced ([body-format-bob1.md
§7](../reverse-engineering/body-format-bob1.md#7-which-file-wins-extension-and-archive-precedence)):
a loose file wins, otherwise the highest-numbered catalogue holding the name
under any body extension, with extension order applying only within that layer.
So the new slot overrides the shipped member without any mod selection; the
member keeps the winning member's exact archive path, and a body whose winning
resource is loose is refused. `addon/mods/` is not used because a mod package is
searched only when selected in the launcher.

**Placement (corrected 2026-09-23 after Run 68 B).** The engine draws
`final = clamp(sel - 1, 0, n-1)` at View Distance Very High (this bottle), where
`sel` is the highest record with `s < trunc(T·f)` walking from the last record
([lod-selection.md](../reverse-engineering/lod-selection.md), "What the selection
really does, end to end"). Record `k` is drawn exactly when record `k+1` is the
first hit. The first placement rule (coarse record before the last one with
`T = T_last`) therefore drew the new record only below `T_last·f`: 5 px for
`argon_TL`/`M2`/`M1` (ladder 30/15/5), 15 for `military_outpost_middleb`, never
at the run240 stand (`s` 21–95, run255, measured). `lod_overlay.py` then defaulted
to **pad** (now `--placement pad`; the default is compact, below):
`[T_0 … T_last, C:T_last, pad:T_pad]`, where `C` is the collapsed
coarse record and the pad a copy of it that is never drawn at Very High. `s <
T_pad·f` selects the pad and the `-1` draws `C`; `s >= T_pad·f` falls through
every original threshold to record 0. `T_pad` comes from `--threshold T` or
`NAME=T` per body, must exceed every original threshold of records 1..n-1
(refused otherwise unless `--force-threshold`) and be `>= 2`; original records
are untouched. At Low..High the pad (same mesh) draws below `T_pad·f`, and
`0x8000`-flagged nodes now hide below `T_pad·f` there (hide-at-coarsest fires at
final index `n-1`; never at Very High).

LOD 1..n-1 of the original ladder become unreachable in the main view: none of
their thresholds exceeds `T_pad`, so none is the first hit from the top (the
`0x1000000` env-map view and the distance branch can still pick them). That is
intended: at the stand they carry 23–33 draws each (measured, `bob1.py info`),
and the body goes from LOD 0 straight to the collapsed `C`. `--placement
before-last` and `--placement append-pad` (`C:T`, `pad:T-1`) remain for the
record. The `--dry-run`/`--out` report prints, per body, the old and new ladder
with record bytes, the drawable set per View Distance setting and the drawn
record per `s` band before and after (`bob1.selection_bands`).

**Compact placement (the default since 2026-09-23).** Under pad, `C` sits at
index 4 (ships) or 3 (outpost), and a final index `>= 3` makes the engine set
`node+0x130 |= 0x100000`, which switches the materials from `BUMPMAP` to the
`DEFAULT` technique and drops a texture slot
([lod-child-hide.md](../reverse-engineering/lod-child-hide.md) §4; whether
`DEFAULT` samples the light map is untraced, so the glow groups were at risk).
And the original records 1..n-1 are dead weight once `T_pad` exceeds their
thresholds. `--placement compact` therefore writes `[record 0, C:T_1, pad:T_pad]`:
record 0 unchanged, `C` at index 1 with the original record 1's threshold (never
the first hit, since `T_1 < T_pad`), the pad a copy of `C` at index 2. The
original records 1..n-1 are dropped. At Very High `C` draws below `T_pad·f` and
record 0 above; at Low..High the pad does. The final index never exceeds 2, so
the index rule never sets `0x100000` (the size- and view-based sets remain).
`T_pad >= 2` and `T_pad >= T_1` are required (`--force-threshold` admits a
smaller `T_pad`). The collision mesh is built from the last record, now the pad,
with the original coarsest record's points and faces, so it is unchanged. The
`0x1000000` env-map view draws record 1, now `C`, for `s >= T_pad·f` (before:
the original record 1). The manifest records each body's `source_thresholds`
and written `thresholds` besides `new_lod`/`pad_lod`. Details:
[lod-selection.md](../reverse-engineering/lod-selection.md), "Overlay
placement".

**Collapse (rule e, `--collapse glow`, the default since 2026-09-23).** Run 69 A
showed that the first collapse (`two`: everything opaque onto the dominant
material) drops every other material's light map (texture stage 3, the emissive
map), so the ships lost their engine glow below `T_pad`. The material census
(`tools/analysis/body_materials.py`, output
`verification/results/lod-overlay-pilot/materials_<body>.txt`) found:

- A NULL `t_LightMapTexture` is the engine's 32x32 placeholder (id 196). argon_TL
  LOD 0 part 0 has 31 groups, 22 with a real light map and 9 NULL. That is the
  same split as run255's stage-3 textures (22 real, 9 × id 196 32x32; measured,
  `verification/results/run257-pilot/tex_run255_3351_TL.txt`). `NONE_*` names
  (`NONE_BLACK`, `NONE_WHITE`) are shipped 32x32 textures in `dds/`. The census
  resolves them like any texture: a `NONE_WHITE` light map counts as all bright
  (glow) and `NONE_BLACK` as dark. Rule d below does not keep them.
- The engine glows are the light maps of the `exhaust` materials
  (`metal_argon_exhaust_source_01/02`, `exhaust_trims_02/03`). For each, at least
  39 % of texels are above luma 0.5 (mean luma 0.37–0.65). Their faces are
  1–3 % of the ships' coarse-record area and 0.5 % of the outpost's (material 41).
  `exhaust_trims_01` has no bright light map (NULL on the ships, `NONE_BLACK` on the outpost) and does not glow.
- Every other real light map is mostly dark. Window lights are sparse spots on
  trim and apartment maps (99th-percentile luma up to 0.83, bright share
  0–10 %, the top being the outpost's material 5 at 0.10), and 7–17 of the 14–21 real maps per body have a 99th-percentile luma
  below 0.2.

Per part, `glow` keeps every material whose light map is mostly bright (share of
texels with Rec.601 luma above `--glow-luma`, default 0.5, at least
`--glow-share`, default 0.25; measured on the smallest mip with a side of 64 or
more) as its own group. Everything else collapses onto the dominant opaque
material, plus one alpha group when alpha faces remain. Window lights on
near-black maps are dropped.

**Alpha rule (2026-09-23).** A material is alpha when it alpha-tests or
alpha-blends (`g_AlphaTestEnable` or `g_AlphaBlendEnable` non-zero). An alpha
texture alone no longer counts: the ships' lattice materials carry
`t_AlphaTexture` with both flags off, so they merge. In run257 the ships'
coarse alpha-group draws (20 / 34 / 12 primitives for M1 / M2 / TL) logged
`alpha_tested=0` (measured, `verification/results/run257-pilot/ship_draws_run257_3615.txt`).
The outpost's alpha materials 9, 36 and 11 (124 / 83 / 45 faces, test and blend
on) keep one alpha group of 252 faces under the dominant material 9.

Faces of merged materials are drawn with the dominant material's textures over
their own UVs, a look limit of the pilot. `--collapse two` (opaque + alpha) and
`--collapse one` (one group, alpha faces turn solid) stay selectable. Draws of
one coarse record by rule (measured, `materials_<body>.txt`, current alpha rule):

| body (record) | original | two (c) | keep real light maps (d) | glow (e) |
|---|---|---|---|---|
| argon_TL (LOD3) | 23 | 1 | 15 | 5 |
| argon_M2 (LOD3) | 25 | 1 | 17 | 5 |
| argon_M1 (LOD3) | 24 | 1 | 16 | 5 |
| military_outpost_middleb (LOD2) | 30 | 2 | 22 | 3 |

At the run257 stand (one TL, one M2, two M1 and one outpost in `C`, from the
ladder accounting) that comes to 23 draws for `glow`, 86 for d and 6 for `two`
under the current alpha rule; the installed pilot (old alpha rule) draws 10.
MAT3 bodies are refused unless `--force-mat3`: the loader gives the coarsest
record of such a body with more than 3 LODs material `0x485`, which would be the
pad.

**Rule f, `--collapse glow-area P` (2026-09-23, after the Run 69 C lighting
triage).** Rule f keeps the glow set (as `glow`). It also keeps the smallest set of
the other real-light-map materials whose summed face area in the coarsest record
covers `P` % of their total area. A real light map is one that resolves and is not
`NONE_*`. The candidates are ranked by face area, and ties go to the lower index.
Every kept material has its own group; the rest collapse as in `glow`, with the
alpha rule unchanged. `P = 0` is `glow`. `body_materials.py` reports rule f at
`--area-percent` (default 50, 70, 90, 100). It prints the ranked candidates with
their light-map 99th-percentile luma, and per rule the diffuse substitution: the
share of record area that keeps its own material, is the merge target itself, or
is merged onto a target with the same or a different diffuse stem. Draws of `C`
(measured, `materials_<body>.txt`; "other diffuse" = share of record area drawn
with a different diffuse texture):

| body | c | e | f50 | f70 | f90 | f100 = d | other diffuse c / e / f70 / d | f70 kept light maps, p99 luma |
|---|---|---|---|---|---|---|---|---|
| argon_TL | 1 | 5 | 8 | 9 | 12 | 15 | 0.56 / 0.54 / 0.33 / 0.26 | 35, 14, 22, 23: 0.03–0.16 |
| argon_M2 | 1 | 5 | 7 | 8 | 11 | 17 | 0.55 / 0.53 / 0.33 / 0.25 | 14, 35, 22: 0.03–0.11 |
| argon_M1 | 1 | 5 | 7 | 9 | 10 | 16 | 0.56 / 0.55 / 0.33 / 0.28 | 8, 25, 16, 17: 0.03–0.16 |
| military_outpost_middleb | 2 | 3 | 8 | 10 | 13 | 22 | 0.74 / 0.73 / 0.42 / 0.30 | 36, 9, 23, 21, 6, 30, 5: 0.05–0.83 |

At the run257 stand that is 23 (e), 37 (f50), 45 (f70), 56 (f90) and 86 (d)
draws. No merged material shares its target's diffuse texture: the "same diffuse"
share is 0 in every body and rule. The ships' target is
`metal_argon_simple_plating_v2` (44–45 % of the record). The largest faces
repainted with it are `simple_plating`, `base_metal_plating(_v2)` and
`simple_plating_v3`; on the outpost they are `exhaust_trims_01` and
`simple_plating_v4`. The area ranking picks dim maps first on the ships. Their
largest light maps (`trims_09`, `misc_tech_v2`, `trims_02/03`) have mean luma ≤ 0.01
and p99 ≤ 0.16, i.e. dark fields with sparse window spots. The bright window maps
(`apartments_labs` p99 0.80, `trims_08` 0.68, `pipes_tech` 0.45) cover 0.1–3 % of
the record each. Weighted by area × mean luma (a proxy: it ignores how much of each
texture the faces' UVs actually map)
(`verification/results/lod-overlay-pilot/emission_share_out.txt`), the glow set
already carries 82–86 % of the ships' light-map emission. f70 recovers 25 / 30 /
47 % of the rest (TL / M2 / M1) and f90 51–80 %. On the outpost the two
`apartments_labs` materials (5, 6) are 84 % of the non-glow emission and rank 5th
and 7th by area, so f70 recovers 96 % there. The ×4 light-map gain scales every
map alike and does not change these shares.

**Synthesized merge material (2026-09-23; default, `--no-synth-material` turns it
off).** Run 69 C found the collapsed outpost's sun term about 23 % weaker. Its
dominant material 17 has `g_MatDiffuseStrength` 0.40, against a prim-weighted
0.56 on the fine outpost. For each merge target, `lod_overlay.py` averages every
`FLOAT` effect parameter named `g_Mat*` over the materials merged onto it (across
all parts), weighted by face area. If the rounded 16.16 mean differs from the
target's value, a copy of the target with those parameters replaced is appended
to the `MAT6` table, and the merged groups point at it. Textures, colour grading
(`g_Brightness`, `g_Color_*`) and every other parameter stay the target's. The
copy's record index is its position. Existing indices do not move, so record 0 is
unchanged. `plan_body` refuses unless the written body parses back with the same
table and every group index of every record is inside it (MAT5/MAT6 bodies
only, since MAT3 indexes the global table). The manifest lists each synthesized
material with its dominant, the absorbed materials and dominant/mean/written
values. On the four pilot bodies at f70 only the outpost gets one: `mat51` (a copy
of 17) over 21 merged materials. It has diffuse strength 0.40 → 0.499, specular
strength 2.4 → 2.81, specular power 5 → 5.67, reflection strength 0.9 → 0.967,
and min fresnel 0 → 6e-5. The ships' coarse materials all carry identical `g_Mat*`
values (diffuse 0.5, specular 3.0, power 6, reflection 0.5), so none is
synthesized for them. The mean excludes kept materials, so it is 0.499, not the
fine outpost's 0.56.

Rule-f pilot build (2026-09-23, `--replace --collapse glow-area 70 --out
<worktree>/build-overlay`, ships `T_pad` 80, outpost 150, compact, not
installed). `C` has 9 / 8 / 9 / 10 draws (TL / M2 / M1 / outpost). The outpost's
opaque group uses `mat51`, and its alpha group is material 11 alone, because the
alpha materials 9 and 36 have real light maps and are kept. `05.cat` is 189 bytes,
sha256 `87bf16cd…`. `05.dat` is 7 789 979 bytes, sha256 `c2f0abd6…`. Every
`pilot_check` field is true (`pilot_check_area70_out.txt`): record 0 bytes (shifted
by the appended material), `C` recomputed from the source, the table prefix, the
synthesized material, the indices in the table, the kept set recomputed, and the
highest final index 2. The build log is `build_area70_out.txt`.

**Atlas collapse, `--collapse atlas` (2026-09-23; `tools/analysis/lod_atlas.py`).**
It keeps the lit areas and the right diffuse texture at one draw. `C` gets one
opaque group per part with one appended material per body. That material is a
copy of the dominant opaque material with every `g_Mat*` FLOAT set to the
face-area-weighted mean over all atlased materials, exhausts included. Its
`t_DiffuseTexture`, `t_LightMapTexture` and `t_BumpTexture` point to per-body
atlases (`--no-atlas-bump` leaves the bump slot NULL). `t_AlphaTexture` is NULL.
`t_SpecularTexture` is NULL, which has shipped precedent: 120 of 7198 `argon.fx`
materials have it (`null_slots_out.txt`); `--atlas-specular` bakes a specular
atlas the same way. A NULL bump has almost none (1 of 7198 `argon.fx`, 0 of 8838
`XT_standard_lighting.fx`), hence the bump atlas. Alpha-tested or alpha-blended
faces keep one group per part, as with `two`.

- **Layout.** One span tile per distinct (diffuse, light map, bump) set. Each face
  moves by its integer UV shift, floor of min u and min v with 0.01 slack. A tile
  holds the whole shifted span, so a tiling texture repeats inside it, and faces
  are never split. A point used with different shifts, tiles or by an alpha face
  is duplicated with its 7-int record. The UV map is a positive per-axis affine
  map, so the tangent frames are unchanged.
- **Records.** Records are regenerated per output group as one record per
  referenced point, listed in the order the group's faces first use the points.
  That is the shipped convention: in all 459 groups of every record of the four
  pilot bodies the records are exactly the distinct face points in first-use
  order (`record_order.py`, `record_order_out.txt`; measured). The other
  collapses (`glow`, `glow-area`, `two`, `one`) now order merged groups' records
  the same way, one record per point. Their `C` bytes are unchanged on the four
  pilot bodies (the merged groups share no points). The atlas `C` records did
  change: the duplicated points' records used to be appended at the end. The
  textures and checks stayed identical.
- **Refusals.** A record whose points carry a second UV set (point flag `0x04`)
  is refused unless `--force-uv2`, because only the first UV pair is rewritten.
  Opaque materials from more than one `.fx` file are refused unless
  `--force-mixed-effects`. The pilot bodies have neither (all points `0x1b`; one
  effect per body).
- **Size.** (`T` is in the 1280-wide reference; `--screen-width`, below.) Tile content
  is source texels over the span times one uniform
  scale ≤ 1, rounded to 4 texels, with an 8-texel gutter (4 before 2026-09-23,
  see "Tile-aware mips" below). Shelf packing picks the
  largest scale that fits. The atlas side is the first of `--atlas-size` (1024),
  2×, … up to `--atlas-max-size` (2048) that gives at least 2 atlas texels per
  screen pixel on every tile at the switch size `T`. `T` is the `NAME=T` value,
  and the need per tile is inferred as √(face area / UV area) · T / radius. When no size
  reaches 2 with one scale, the clamped layout (span-clamped outlier faces, per-tile cap at
  2 texels/px; "Batch mode", clamped layout) is tried at each size.
- **Baking.** The tile, gutter included, is area-resampled from the repeating
  source (DXT1/3/5 or uncompressed), so the gutter holds the true neighbouring
  texels. A NULL light map becomes constant black with alpha 0. `NONE_*` maps are
  resampled. There is a tile-aware mip chain to 1×1 (below; a box filter of the
  whole atlas before 2026-09-23), and each level is encoded as DXT1,
  or DXT5 when the slot's alpha is not uniformly 255 (`--atlas-format a8r8g8b8`
  for debugging). The shipped light maps carry an alpha that follows their RGB
  (correlation with RGB 0.2–0.99, at least 0.81 on all but two of the 24 light
  maps of argon_TL and the outpost; measured, `light_alpha_out.txt`), so the light atlases are DXT5.
- **Bump.** The shipped bump maps are swizzled tangent-space normal maps, not
  height maps and not blue (128, 128, 255) maps. All 25 distinct maps of the
  pilot records are DXT5 with R = G = B (correlation ≥ 0.991) carrying y and
  alpha carrying x. x and y are nearly uncorrelated (|r| ≤ 0.115), x² + y² ≤ 1
  on ≥ 99.94 % of texels, and the mean is about (128, 128, 128, 108–129)
  (`bump_maps.py`, `bump_maps_out.txt`; measured). Which channels the shaders
  read is inferred from this layout, not traced. The bump atlas decodes them to unit vectors
  (z = √(1 − x² − y²)), resamples vectors and renormalises, at every mip level.
  A NULL bump becomes the flat normal.
  It is re-encoded in the same swizzle, so it is DXT5. The UV map scales u and v
  by positive factors, so the tangent-space vectors stay valid in the unchanged
  tangent frames.
- **Texture names.** The atlases are the members `dds/x3m_lod_<body>_{diffuse,light,bump}.pck`
  (gzip DDS, like every shipped texture). The material names them
  `x3m_lod\x3m_lod_<body>_<slot>.tga`.

The engine finds a material texture by its stem under `dds\`. All 199 distinct
texture names of the four pilot bodies carry a directory, and none matches a
member with that directory. 184 resolve as `dds/<stem>.pck`; the other 15 are
`*_alpha` maps that do not resolve at all. No shipped layer holds an `x3m_lod_`
stem (`texture_paths_out.txt`), so the new names shadow nothing and exist only in
the overlay.

Pilot build (`--replace --collapse atlas`, `T` 80/80/80/150, compact, not
installed; measured, `build_atlas_out.txt`, `pilot_check_atlas_out.txt`):

| body | atlas | tiles | texels/px at T (1024 / chosen) | dup. points | C bytes | draws | diffuse vs source mip 0, mean / p95 | vs prefiltered | light RGB / A mean | bump angle vs source, mean / p95 (°) | vs prefiltered (°) |
|---|---|---|---|---|---|---|---|---|---|---|---|
| argon_TL | 2048² | 23 | 1.997 / 4.04 | 119 | 103 450 | 1 | 2.43 / 7.4 | 1.52 / 4.2 | 0.48 / 0.57 | 3.9 / 18.2 | 2.35 / 9.5 |
| argon_M2 | 1024² | 25 | 2.23 | 118 | 143 064 | 1 | 3.25 / 12.6 | 1.80 / 5.0 | 0.60 / 0.84 | 5.4 / 28.7 | 2.32 / 8.5 |
| argon_M1 | 1024² | 24 | 3.82 | 131 | 123 808 | 1 | 3.12 / 9.9 | 1.92 / 5.5 | 0.66 / 0.79 | 5.8 / 29.0 | 2.48 / 9.2 |
| military_outpost_middleb | 2048² | 27 | 1.47 / 2.99 | 1 786 | 1 422 114 | 2 | 3.43 / 13.0 | 2.03 / 6.4 | 0.57 / 0.92 | 5.5 / 24.8 | 2.95 / 10.4 |

The errors are per-face centroid means in 0..255 (bump: the angle between the
decoded normals). "vs source mip 0" compares the atlas bilinear at the rewritten
UV with the source bilinear at the original UV, with wrap. "vs prefiltered"
compares against the source box-filtered over one atlas texel (bump: vectors
averaged, then renormalised), i.e. without the downsampling loss. The bump
error against mip 0 is the largest because the normal maps carry the most
high-frequency detail; the remaining prefiltered error (2.3–3.0° mean) is DXT
quantisation of y in RGB565 plus bilinear against box.

- **UVs and shape.** Every rewritten vertex UV lies inside its tile. The inverse
  map agrees with the original UV to 0.09 source texels or better. `C` has 1432 /
  1983 / 1715 / 19 254 points.
- **Encoding.** DXT1 compression error is mean 1.4–2.0 and p95 5–6 per channel.
  DXT5 light is mean ≤ 0.8 and p95 ≤ 2. DXT5 bump is mean 2.3–3.7 and p95 6–8 on
  RGB (y), and mean ≤ 0.73 and p95 4 on alpha (x).
- **Sizes.** A DDS is 699 192 B (1024² DXT1), 1 398 256 B (1024² DXT5),
  2 796 344 B (2048² DXT1) or 5 592 560 B (2048² DXT5). An uncompressed 2048²
  atlas would be 22.4 MB. The built atlases total 34 954 336 B (33.3 MiB) of
  texture data for the four bodies: diffuse 6 991 072, light 13 981 632 and bump
  13 981 632 (20 972 704 B without bump). `05.cat` is 700 B, sha256
  `f3f607fa…`, and `05.dat` is 15 525 012 B, sha256 `54acfc47…`.
- **`pilot_check`.** Every field is true except `C_equals_source_last` and
  `last_equals_source_last`, which are expected to be false: the UVs are
  rewritten and points are added. Their UV-free replacements
  `C_positions_equal` and `last_positions_equal` are true, so the collision
  source keeps its geometry. `C` and the table recompute equal, and all three
  textures per body check out by hash, format, full mip chain, material name and
  resolution in the overlay root.

PNG previews (`atlas_preview_<body>_{diffuse,light,bump}.png`, ≤ 512 px, tiles
outlined) are written with `--atlas-preview` and kept local (downscaled game
art).

What is dropped:

- Specular maps (slot NULL, precedented on `argon.fx`, not on
  `XT_standard_lighting.fx`: 0 of 8839).
- Coarse-mip bleed (fixed 2026-09-23, "Tile-aware mips" below): with the 4-texel
  gutter and a box-filtered chain, neighbouring tiles and the unused area mixed
  into tile borders from mip 3 on.
- Known limits of the tile-aware chain and its check:
  - The DXT1/DXT5 choice per slot is made from the level-0 alpha only.
  - Where two tiles' footprints share a texel at a coarse level, the later tile's gutter
    wins. From about mip 5, where the 16-texel gap between contents falls below one texel,
    this can also affect the contents themselves.
  - `atlas_mip_bleed.py` compares the atlas against its own definition of the correct
    level (the tiles' maps resampled per level texel), which is the definition the new
    chain implements. It skips texels covered by two footprints.
- The alpha materials' own textures beyond their dominant, as in `two`.

**Source record, `--source-record N` or `NAME=T@N` (2026-09-23).** `C` is built from
record `N`'s geometry (points, part flags and part ints, groups, 7-int records) instead of
the coarsest record, which stays the default. `--source-record` sets it for every body, and
`NAME=T@N` (or `NAME=T,N`) sets it for one. Every collapse, including the glow and area
selections and the synthesized materials, reads that record. `N = 0` gives `C` the fine
silhouette. With `--collapse atlas`, the switch at `T_pad` then changes only the texture
resampling (atlas instead of the source maps) and the material constants. There is no
geometric transition.

The trade: `C` has one atlas draw per part (more if the group is split, below), plus the
alpha group when record 0 has alpha faces. It keeps the full silhouette, and its vertex
data is record 0's plus the duplicated points: 2.2–2.9 MB per record in the file for the
ships and 9.9 MB for the outpost, against 0.10–1.3 MB from the coarsest record.

**The pad.** When `N` is not the coarsest record, the pad is a byte copy of the original
coarsest record (its own points, faces, groups and materials, threshold `T_pad`), not a
copy of `C`. At Very High the pad is never drawn. The collision tree is built at creation
from the last record ([lod-child-hide.md](../reverse-engineering/lod-child-hide.md) §3),
so the collision geometry stays vanilla's exactly (1 313 / 1 865 / 1 584 / 17 468
points). Its material indices point into the original table, which is a prefix of the
written one, and `plan_body` and `pilot_check` (`indices_in_table`) check every group
index of every record against the written table. At Low..High the pad is what draws
below `T_pad·f`. There the body shows the original coarsest record (23–30 draws), not `C`.
This bottle runs at Very High. With `N` equal to the coarsest record, the pad is a copy of
`C` as before.

**Group size.** An output group referencing more than 60 000 distinct points
(`lod_atlas.MAX_GROUP_POINTS`) is split. The engine clones a subgroup above 65 535
vertices with a 32-bit index buffer (`0x004bcb60`,
[mesh-buffer-rewrite.md](../reverse-engineering/mesh-buffer-rewrite.md)), a path not
qualified here, and it indexes per group. The shipped outpost record 0 has 125 867 points
over 34 groups of at most 35 210 (measured). The limit leaves headroom for
D3DXCleanMesh: `0x004bc680` welds adjacency at ε ≈ 1e-6 and cleans with flags 3
(back-facing and bowtie), and each extra fan around a vertex or back-facing twin adds a
vertex before the mesh is cloned
([loading-observations.md](../reverse-engineering/loading-observations.md)).

`fan_estimate.py` (`fan_estimate_out.txt`) estimates those additions per group. It is my
reconstruction of the reviewer's method, not a copy of their script, and D3DX's exact
rules are not traced.

| split | groups (points + estimated additions) |
|---|---|
| earlier 65 535 split | 63 993 + 147 and 65 524 + 79 = **65 603**, above 65 535 |
| 60 000 split (current) | 9 530 + 35, 59 993 + 117 and 59 994 + 74 |
| ships (unsplit) | + 45 / 46 / 45 |

`lod_atlas.split_faces` packs whole source groups first-fit in decreasing size into groups
of the same material, and cuts a source group above the limit in face order. A point used
by an earlier split group is duplicated with its tangent record, so the split groups share
no point. For the outpost the packing needs no duplicates. The other collapses refuse a
group above the same 60 000. A greedy split in plain face order (at 65 535) gave one group
more than the packing, because the face order interleaves source groups. Record 1 of the
outpost would split to [6 548, 59 963] (`outpost_groups_out.txt`).

Record 0 of all three ships has a second part with flags `0x30008001`, which the
coarse records do not have. It is a 24-point, 12-face axis-aligned box (6 quads)
behind the hull, for example at z 43 295–43 798 on argon_TL, and covers about 0.01 % of the record's face area. On argon_TL and
M2 each quad has its own material (0–5: lattice and antenna alpha materials and
`apartments_labs`). On M1 it is one group with material 32, a classic material
without an effect or textures (measured, `bob1.materials`). Its `0x8000` bit is not the
hide-at-final-index rule of [lod-selection.md](../reverse-engineering/lod-selection.md).
That rule tests the node word `node+0x12c & 0x8000`. The part flags are stored at part
`+0x60` ([body-format-bob1.md](../reverse-engineering/body-format-bob1.md) §3), and
the collection helper `0x0047d9c0` skips a submesh with `[+0x60] & 0x8000`
([emission-draw-order.md](../reverse-engineering/emission-draw-order.md)). So the part
is never collected for drawing, whatever the LOD index (from those notes; not
re-traced here). The atlas collapse therefore does not atlas such a part
(`lod_atlas.HIDDEN_PART`). It copies the part's groups with their own materials and UVs
and leaves its materials out of the tiles, the effect check and the `g_Mat*` mean.
Without that, M1's material 32 could not be atlased.

Pilot build (`--replace --collapse atlas --atlas-specular`, `ships/argon/argon_TL=80@0`,
`argon_M2=80@0`, `argon_M1=80@0`, `military_outpost_middleb=150@0`, compact, tile-aware
mips with an 8-texel gutter, not installed; measured, `build_atlas_lod0_out.txt`,
`pilot_check_atlas_lod0_out.txt`). "Groups" counts `C`'s groups, with the ones the
collection draws in brackets. The errors are defined as in the atlas table above; "spec"
is the specular atlas against source mip 0 and prefiltered.

| body | source faces / points | `C` points (dup.) | pad points | atlas (scale) | min texels/px at `T_pad` | tiles | groups (drawn) | `C` bytes | diffuse vs mip 0 / prefiltered, mean / p95 | light RGB / A mean | bump angle vs mip 0 / prefiltered, mean / p95 (°) | spec vs mip 0 / prefiltered, mean |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| argon_TL | 15 296 / 28 982 | 30 064 (1 082) | 1 313 | 1024² (0.115) | 6.21 | 27 | 8 (2) | 2 229 180 | 4.19 / 16.3, 2.17 / 6.4 | 1.52 / 1.91 | 5.14 / 26.2, 2.09 / 7.5 | 11.4 / 4.5 |
| argon_M2 | 21 780 / 37 706 | 39 226 (1 520) | 1 865 | 1024² (0.117) | 7.27 | 28 | 8 (2) | 2 937 616 | 3.87 / 15.1, 2.04 / 6.0 | 1.19 / 1.51 | 5.02 / 25.9, 2.08 / 7.6 | 11.2 / 4.4 |
| argon_M1 | 18 087 / 32 878 | 34 148 (1 270) | 1 584 | 1024² (0.122) | 7.01 | 28 | 3 (2) | 2 543 320 | 3.96 / 15.4, 2.09 / 6.1 | 1.36 / 1.68 | 5.00 / 25.7, 2.12 / 8.0 | 11.0 / 4.3 |
| outpost | 73 749 / 125 867 | 131 787 (5 920) | 17 468 | 2048² (0.099) | 2.08 (1024²: 1.13) | 28 | 4 (4) | 9 878 052 | 4.29 / 16.8, 2.22 / 6.4 | 0.87 / 1.31 | 6.53 / 31.0, 2.57 / 9.0 | 12.0 / 4.5 |

On the ships the drawn groups are the atlas group (15 156 / 21 610 / 17 937 faces) and one
alpha group (128 / 158 / 138 faces), because record 0 has alpha-tested faces that the
coarsest records lack. So `C` draws 2 per ship, where the coarsest-source atlas drew 1.
The outpost draws 4 against 34 at record 0: three atlas groups (6 232, 35 811 and 30 282
faces; 9 530 / 59 993 / 59 994 points) and the alpha group (1 424 faces, synthesized material
52 over material 11).

Record 0's material UV spans are larger (tile span areas up to 19.6 periods²), so the
tiles are stored at 10–12 % of source density. The ≥ 2 texels/px rule at `T_pad` is met at
1024² on the ships (6.2–7.3). The inferred reason is that record 0's faces carry more UV
area per unit of surface, which lowers the per-tile need. The outpost needs 2048².

`T` is in the 1280-wide reference: real px = s · m00 · width / 1280. The rule therefore
holds at 1280 pixels wide. At 2560 wide the outpost's 2.08 becomes about 1.04 and the
ships' 6.2–7.3 about 3.1–3.6 (inferred, linear in width). `--screen-width W` (default 1280)
applies the rule at `T · W / 1280`, so a wider display gets the larger atlas, up to
`--atlas-max-size`. The outpost at 2560 wide would need 4096² with `--atlas-max-size 4096`
(not built).

Every vertex UV lies inside its tile, and the inverse-map error is ≤ 0.153 source texels.
Every `pilot_check` field is true except `C_equals_source_record`, which is expected false
because the UVs are rewritten. `C_positions_equal` (against record `N`),
`pad_equals_source_LOD<n>`, `last_equals_source_last` and `last_positions_equal` are true,
so the collision source is vanilla's.

`05.cat` is 876 B, sha256 `d29b0c88…`. `05.dat` is 19 466 361 B, sha256 `5fd877c1…`,
against 19 321 288 B for the installed four-body build. Per body (body member / textures,
`dat_members_atlas_lod0_out.txt`): TL 1.73 / 1.28 MB, M2 2.34 / 1.35, M1 1.86 / 1.24,
outpost 7.77 / 1.90. The installed build had TL 0.93 / 4.30, M2 1.26 / 1.34, M1 1.00 / 1.18
and outpost 4.69 / 4.62.

**Tile-aware mips (2026-09-23, after the Run 268 triage).** The mip chain was a box
filter of the whole level-0 atlas with a 4-texel gutter. Every level is now baked
like level 0 at its own density. Each tile's footprint (content and gutter, rounded
out to whole level texels) is area-resampled from the tile's own repeating source. The
texels that overlap a tile's content are written last, so content beats a neighbour's
gutter, and the unused area keeps its background at every level. The gutter is now 8
texels, so a whole gutter texel survives to mip 3. All four atlases use it (the bump
atlas renormalises at every level). Build time for the three ships is 29 s (24 s before;
measured). `atlas_mip_bleed.py` compares the light atlas at mips 0–4 with a reference
built independently: every tile's own light map resampled to the same density, and
black elsewhere. `build_box_mips.py` rebuilds the old chain byte for byte (`05.dat`
sha256 `4151f925…`, equal to the earlier source-0 build) for the "before". Before and
after on the same bodies and source record (`atlas_mip_bleed_compare_out.txt`; measured):

- Exhaust tiles' edge and gutter texels (the texels bilinear reads at the content
  edge): at mip 3, mean / max error 8.6–34 / 82–146 → 7.5–13 / 29–35; at mip 4,
  12–35 / 59–173 → 4.6–10.5 / 14–32. Mips 0–2 are unchanged (4–13 / 27–58, the DXT5
  error).
- Exhaust content: the mean error is 5–14 at every level, mip 0 included, so it is
  DXT5 encoding of the high-contrast exhaust maps, not a mip effect. The tile's mean
  brightness equals the reference within about 1 % (installed argon_TL build, exhaust tiles
  17 and 18, mips 0–3).
- The neighbours' content and the unused ring around the exhaust tiles show a mean
  luma excess of |≤ 0.9| before and after. Their maxima (up to 42) are already present
  at mip 0, so they are DXT error.

So the box chain did bleed, but only into tile borders from mip 3 on. This offline
measurement does not reproduce a ×1.5 brightness over a ×1.56 area: the measured
exhaust tiles hold the same mean light as their maps at mips 0–3. The Run 268 step
may have another cause (inferred, untested). The installed build draws the coarsest
record's exhaust geometry, which a `--source-record 0` build replaces with record 0's.
The exhaust materials' `g_Mat*` values equal the dominant's (M2: measured), so the
atlas material does not change their constants. The test `TileAwareMips` in `test_bob1.py`
checks the property: a bright tile beside a black one, with the gutter off the
coarse texel grid, stays black outside its footprint at every level. The black tile's
content stays black wherever the two contents share no texel. The old box chain fails
it at mip 4.

**Fleet batch census (2026-09-23; `tools/analysis/lod_batch_census.py`, read-only).** Over
all 1635 winning `BOB1` bodies (installed `addon/05` skipped) it runs the atlas collapse's own
checks and layout on record 0 without baking (`verification/results/lod-overlay-batch/`:
`summary.txt`, `census.txt`, `sectors.txt`, `eligible_bodies.txt`; measured). The texel rule's
reference is `--screen-width 1920` (the user's display); 1280 and 2560 are extra columns.
Proposed switch rule: ships `T_pad = min(200, max(80, 2.5·T_1))`, stations and `others/` 150,
other top directories excluded (117 bodies). 339 bodies are eligible (220 ships, 119 stations).
Refusals (a body can carry several): no opaque faces 298, opaque materials from more than one
`.fx` 238, a second UV set 164, a group material index outside the body's table 156 (for
visible parts `lod_atlas.collapse` raises `IndexError` there instead of refusing), `T_pad`
below `T_1` 139 (the stations with `T_1` = 250, 97 of them with 250/150/80/30 by `census.txt`,
among them `argon_spacedock`), a dominant material without a light-map slot 113, a non-effect
material 49, both `.pbb` and `.pbd` present 47 (`bob1.resolve_body` raises; 10 of them otherwise
eligible, `ambiguous_ext_out.txt`), and 10 otherwise eligible bodies whose file stems collide
(the atlas names use the stem only, e.g. `ships/M6/Terran_M6` and `ships/usc/terran_m6`;
`stem_collisions_out.txt`). 186 accepted bodies save no draw. Below `T_pad` the eligible bodies
save 4358 draws per instance against record 0 and 3082 against their coarsest record (summed
over bodies; 26 save none against the coarsest). At 1920 wide the ≥ 2 texels/px rule picks
1024² for 169, 2048² for 79 and 4096² for 91 eligible bodies; 77 miss it even at 4096² (e.g.
`ships/M7/Split_M7s` 0.02 by `census.txt`), 113 at the tool's 2048 cap. The added atlas
residency of the eligible bodies of the flown sets at 1920 wide, 2048 cap / uncapped: run255
burst 2 (10 bodies) 122.34 / 415.94 MB, run257 burst 1 (10) 122.34 / 415.94 MB, run260 (8)
112.55 / 406.15 MB; at 1280 wide uncapped 229.99–298.50 MB. The byte figures are a lower bound:
diffuse is counted DXT1, but `lod_atlas.encode` picks DXT5 for a slot whose level-0 alpha is not
all 255, and the slot set assumes `--atlas-specular`.

### Batch mode (2026-09-23; `lod_overlay.py --batch`)

One command builds and installs an overlay over every eligible ship and station of the
installed game, vanilla plus every numbered addon catalogue a mod adds
(`python3 tools/analysis/lod_overlay.py --batch [--sync] --install`). The census module
enumerates the bodies and the tool bakes them in worker processes (`--jobs`, default cpu-2);
tests: `verification/analysis/test_lod_overlay_batch.py` on synthetic catalogues, dry-run
evidence under `verification/results/lod-overlay-batch/batch-dryrun/`.

- **Rule and guard.** Ships `T_class = min(200, max(80, 2.5·T_1))` (80 for a single-record body),
  stations and `others/` 150, then `T_pad = round(T_class · clamp(k, 1, K_max))` (aspect factor,
  below; `--no-aspect` keeps T_class), source record 0, compact placement, `--collapse atlas` with the
  specular atlas on; `--include-other` adds the remaining top directories under the station
  rule. The compact guard "T_pad not below T_1" is waived automatically when the source record
  is 0: C is then the full LOD 0 geometry, so C drawing at Low..High in the band
  `T_pad·f <= s < T_1·f` (the case the guard refuses) shows the same mesh with the atlas
  textures, which is harmless. The guard stays for a decimated source (a coarser source
  record). The 139 `t_pad_below_t1` refusals of the census (the 250/150/80/30 stations) are
  therefore eligible; `census.txt` marks them `T_pad=150<T_1`.
- **Enumeration.** Unpacked `.bob` members are bodies (`canonical()` maps `.pbb` to `.bob`, so
  they share a key; the census's `.pbb`-only filter had dropped 1,271 of 1,438 mod bodies). A
  winning text body (`.pbd`/`.bod`) is compiled by `bob1.parse_text`
  ([body-format-bob1.md](../reverse-engineering/body-format-bob1.md) §8) by the rules of the
  engine's own text loader `0x00483f20` ([body-text-loader.md](../reverse-engineering/body-text-loader.md)):
  `/!` blocks ignored, each record normalised to 65536 with truncation, engine normals and point
  merging, no tangent records (as in vanilla, where a bump-mapped text body draws with a zero
  tangent basis), effect parameters as written. Because the overlay replaces every record of the
  body, the written member loads into the model the game already draws for that text body. It is
  censused like a binary one (column `text`; a text scene is skipped like `CUT1`; a MATERIAL3 text
  body is `mat3`; any other body outside the grammar is `text_parse_error`); the overlay writes it
  as the binary member of the same stem (`.pbd` → gzip `.pbb`, `.bod` → `.bob`: our slot is the
  highest catalogue and binary beats text inside a layer) and the marker records its
  `source_member`; `--binary-only` leaves text bodies out of the census and the batch. A stem with both a binary and a text member stays
  `ambiguous_body_ext` (engine order unverified). Stray bytes after `/BOB`:
  `bob1.parse(data, max_trailing)` tolerates up to `lod_overlay.MAX_TRAILING` (8) and records
  them; the engine parser `0x00481aa0` returns the model at the `/BOB` closer and never reads
  past it (body-format-bob1.md §1), so they are inert. Of the 94 mod bodies that failed to
  parse, 86 carry 1–2 stray closer bytes (`B`, `OB`; measured over `/tmp/x3-mod1`,
  `/tmp/x3-mod2`) and are tolerated with a warning; 8 carry 502–7956 bytes (a duplicated tail)
  and are refused as `trailing_bytes`. A negative group material index (156 vanilla bodies,
  one ad-sign group each) is refused as `material_outside_table` before the collapse instead
  of the former `IndexError`.
- **Mixed effects.** `lod_atlas.collapse` groups the opaque materials by effect file and emits
  one merged material per effect (a copy of that effect's dominant material with the atlas
  textures and that effect's area-weighted `g_Mat*` means), all sharing the one atlas set
  (same tiles, same UV rewrite); each part gets one output group per effect, so a body draws
  one atlas group per effect plus the alpha group. The 238 vanilla `mixed_effects` refusals
  are no longer refused for this reason (`StockmarketBoardXL`, `pirate_TL_var1/2`,
  `planet_close_waterworld` under `--include-other`; other reasons may still apply, e.g.
  `StockmarketBoardXL` is `dominant_slot_missing`).
- **Second UV set.** The fleet's second set is the per-body occlusion decal unwrap
  (`t_OcclusionTexture` in `[0,1]`, one texture per body, `XT_standard_lighting.fx`;
  `classes/classes_out.txt`). The record rewrite copies the second pair through unchanged
  (`with_uv` rewrites only the first pair), the merged material keeps the dominant material's
  `t_OcclusionTexture` and the `g_Mat*` mean covers `g_MatOcclStr`. A body is refused as
  `occlusion_mismatch` when the opaque materials of one merged group carry more than one
  occlusion texture (`NULL`/`NONE_*`/absent count as none; none mixed with a decal refuses).
  The 164 vanilla `uv2` refusals (25 heavy: `Argon_m7m`, Pirate M1/M2/M7, the `argon_M2_OCC_*`
  variants, `terraformer_hub_A`) are no longer refused for this reason.
- **Names and textures.** Atlas members are `dds/x3m_lod_<stem>_<hash6>_<slot>.pck` with the
  hash from the lower-case member path (`lod_overlay.qualified_stem`), so colliding stems
  (`ships/terran/terran_M3` vs `ships/usc/terran_m3`; 369 collisions in the mod trees) get
  distinct names that are stable across runs; the batch refuses a duplicate name. Source
  textures resolve as the engine's loader does: `dds/<stem>.pck|.dds` (`0x004dc540`, extension
  list "pck dds", path table `+0x7c` `dds\%s`), then `tex/<stem>.jpg|.tga|.bmp` (the wrapper
  `0x004f3510`, `+0x74` `tex\%s`); the dds-before-tex order is inferred from the path table and
  the wrapper's role, not traced, and no enumerated mod body reads a `tex/` member (0 affected,
  `mod_enumeration_out.txt`); jpg/tga/bmp are decoded with Pillow (imported lazily; a
  missing Pillow refuses the body as `pil_missing`), and the manifest records the member each
  tile's textures came from. Vanilla ships 2,550 `dds/*.pck`, 255 `tex/*.jpg` and 11
  `tex/*.tga` members (measured); the mods add `dds/*.dds`, `tex/*.jpg` and `tex/*.tga`.
- **Screen reference.** `--display WxH` (default 1920x1080) derives the reference width as
  `H·1280/768` (1800 for 1080 lines), on the assumption that the projection keeps the vertical
  field of view, so the 768-line reference height maps onto H lines; `--screen-width` overrides.
  The atlas cap defaults to 2048 (`--atlas-max-size`); the texel rule never refuses below 2
  texels/px, the ratio is reported per body and the summary counts the bodies below 1.0.
- **Markers, slots, sync.** The overlay slot is the next contiguous free addon number. Every
  run validates every `addon/NN.x3m-lod.json` by hash: the marker records the overlay cat/dat
  sha256, the display/width, the rule and per body its source catalogue, member, member sha256
  and an `inputs_sha256` over the decoded body and every texture its tiles read. A marker whose
  hashes do not match the files beside it is orphaned (a mod overwrote the slot): it is
  reported, the catalogue is read as a mod source, and `--install` removes the marker. A legacy
  marker without overlay hashes (the installed pilot's shape) is trusted only when every body it
  names is present in the catalogue beside it with the recorded `overlay_decoded_sha256`
  (`legacy_verified`); a legacy marker with no bodies, a missing member or a different sha is
  orphaned, so a mod that overwrote that slot is neither skipped as a source nor retired or
  replaced (`review/legacy_orphan.py`, and the test). Batch
  mode supersedes the live previous overlay without `--replace`: if its slot is still the
  highest addon number the new overlay takes it (move-aside/rollback path); otherwise the new
  overlay goes to the next slot and the old slot's cat/dat become a retired catalogue holding
  one inert text member (`x3m_lod/retired_NN.txt`; never a zero-entry CAT or a 0-byte DAT) with
  a marker recording it as retired, so the numbering stays contiguous (the mount loop stops at
  the first gap); engine acceptance of a retired slot is not yet verified and needs a launch.
  `--sync` rebuilds only bodies whose `inputs_sha256` changed or that are new and copies the
  other bodies' members from the previous overlay dat (each verified by sha256), provided the
  previous overlay used the same rule, width, atlas options and tool sources (`tool_sha256` over
  `lod_atlas.py`, `lod_overlay.py`, `bob1.py` and `lod_batch_census.py`, which decides T_pad, in the
  settings, so a tool change rebuilds).
  `addon/mods/*.cat` are detected and a warning gives how many overlay bodies a selected mod
  overrides. The before/after archive check is a cat sha256 plus dat size and mtime by default
  (`--hash-archives` hashes every dat; the mod trees are gigabytes); the marker records the mode.
- **Texel floor (`--min-texels F`, default 0.5; `--texel-floor-share S`, default 0.10;
  area-weighted since 2026-09-23).** Each tile carries its *share*: the mesh-space area of its
  faces over the atlased surface (opaque faces outside hidden parts; measured). That the share of
  the projected screen area follows it is inferred (orientation-averaged projection is
  proportional to area; view direction and occlusion ignored). Tiles below F atlas texels per
  screen pixel at the display reference, plus the span-clamped faces below (counted at ratio 0),
  are *starved*; a body is refused as `texel_floor` only when the starved parts cover more than S
  of that surface. Otherwise it is built, and the starved tiles are listed as `texel_clamped`
  (tile, ratio, share, starved share, span-clamped faces) in the body's atlas summary, the
  describe text and the batch record (`bodies[].texel`, `ratio.texel_clamped`). The *weighted
  ratio* is the tile ratio at the S area quantile, so refuse <=> weighted < F; S = 0 is the old
  per-tile minimum rule and F = 0 disables the floor. The census and the batch summary print per
  body the aspect factor k, T_class → T_pad, `r_body` (layout radius in normalised body units:
  every record's largest coordinate is 65,536, so it is not a size), `r_world` (the flown radius,
  below) and the switch distance in km, the thresholds, px at the batch width, min and weighted
  ratio and the starved share, lowest weighted first.
- **Aspect-aware switch threshold (batch default since 2026-09-23; `--aspect-cap SHIPS,STATIONS`,
  default 1.5,2.0; `--no-aspect`).** The engine compares s = r·640/D with the bounding-sphere
  radius r, which overstates a flat or elongated body's visible size, so its merged record
  appeared too far out. `lod_batch_census.aspect_k`: half-extents e = (max − min)/2 per axis of
  record 0's position-carrying points (flag 1), r_box = |e|, r_eq = (ex·ey·ez)^(1/3),
  k = (r_box / r_eq)/√3 (a cube is 1; a zero extent takes r_eq over the nonzero extents with an
  `aspect_note`, none at all gives 1). T_pad = round(T_class · clamp(k, 1, K_max)), K_max 1.5 for
  ships and 2.0 for stations and others; T_class is the class rule above. T_pad drives the pad
  record, the T_1 guard, the atlas size and the texel ratio (sized for the larger T, so the ratio
  falls). k is scale invariant, so a text body (per-record normalisation) gives the binary twin's k
  (test). The world radius is not derivable from the body; it comes from flight censuses
  (`--radius-log`, default `run272-batch-busy/burst_draws_out.txt` when present: `r=`/`radius=`
  with `body=`), and km use ~505 units per metre, an inference of
  [sector-collide.md](../reverse-engineering/sector-collide.md). Record keys `aspect_k`,
  `t_class`, `threshold_aspect`, `radius_body`, `radius_world`, `switch_km`, `switch_km_class`.
  Reference bodies (`--dry-run`, 1920x1080; measured; km inferred):

  | body | k | T_class → T | switch km | min texels/px (`--no-aspect` → aspect) |
  | --- | --- | --- | --- | --- |
  | argon_equipmentdock | 1.44 | 150 → 216 | 9.57 → 6.65 | 2.461 → 2.034 |
  | argon_spacedock | 1.86 | 150 → 278 | 16.51 → 8.91 | 3.089 → 2.053 |
  | argon_trading_station_partA | 1.02 | 150 → 153 | 3.40 → 3.34 | 2.001 → 2.005 |
  | argon_trading_station_partB | 1.10 | 150 → 165 | 5.31 → 4.82 | 2.016 → 2.004 |
  | argon_M2 | 1.40 | 80 → 112 | 8.37 → 5.98 | 5.168 → 3.691 |
  | argon_TL | 1.59 (cap 1.5) | 80 → 120 | 9.61 → 6.40 | 4.416 → 2.944 |

  All six build at 1024. Over the 339 bodies of `eligible_bodies.txt` (measured,
  `lod-overlay-batch/aspect_compare_out.txt`): T changes for 325 (213 ships, 112 stations/others;
  84 at the cap); atlas size changes on 10 bodies, eligible-body atlas bytes (census estimate)
  1,918,399,632 → 2,065,200,272; `t_pad_below_t1` 0 → 0; `texel_floor` 1 → 2 (`teladi/teladi_M1`, k 1.49,
  T 80 → 119, weighted 0.468); min ratio below 2.0 on 34 → 36 bodies (`XTC_boron_drone` 2.004 →
  1.608, `XTC_boron_m8` 2.009 → 1.413).
- **Texel fallback (batch default since 2026-09-23; `--texel-fallback W`, default 1.0; 0 restores
  the refusal).** A body the texel floor would refuse at T_pad is built at a lower switch size T_fb
  instead, so its merged record appears farther out, where the sparse atlas does not show. The rule
  is in screen size because the baker has no world radius for unflown bodies: atlas texels per
  screen pixel scale as 1/T, so T_fb = round(T_pad · weighted / W) (W taken as max(W, `--min-texels`)),
  the layout is rebuilt at T_fb (the atlas size can change with px) and the step repeats, at most 3
  layouts, until weighted ≥ W and the starved share ≤ `--texel-floor-share`. T_fb must stay
  ≥ max(T_1, T_pad/4, 2) (record 1's threshold; a relative floor, so a body is not pushed out to a
  few pixels; the engine's s = 1 minimum); a step below it, or W not reached, leaves the `texel_floor`
  refusal (record `texel_fallback.guard` `T_1`/`relative`/`min_2`, the binding floor). The census
  (`lod_batch_census.texel_fallback`) does the search, so the row's `t_pad` becomes T_fb
  (`threshold_aspect` keeps the rule's T_pad) and the bake, the pad record and the atlas follow it.
  Records carry `bodies[].texel_fallback` (T_pad → T_fb, weighted and starved share before/after,
  atlas sizes, per-step T and weighted, km with a flown radius) and `ratio.texel_fallback` (with
  `built`, or the bake refusal), `texel_fallback_guard` {body: guard} and `texel_fallback_not_reached`;
  the summary lists the fallback bodies built and refused at bake, and splits the `texel_floor`
  refusals into guard + W not reached + no fallback; the option is in the batch settings, so a change makes `--sync`
  rebuild. The single-body mode keeps the threshold it is given.
  Over the 339 bodies of `eligible_bodies.txt` at 1920x1080 (measured,
  `lod-overlay-batch/aspect_compare_out.txt`): 1 body takes the fallback, 1 is refused at the
  relative guard; eligible after the texel rule 336 → 337. `teladi/teladi_M1` (T_1 30, floor 30):
  T 119 → 55 in two steps (56: weighted 0.995, 55: 1.013), weighted 0.468 → 1.013, starved
  34.0 % → 0.0 %, atlas 2048 both; draws 17 → 2 (`r0_drawn`/`C_drawn`, `lod-overlay-batch/census.txt`);
  no flown radius, so km unknown (at a given radius D scales by 119/55 = 2.16×).
  `x3tc/torus_barrier_node` (single record, no T_1; draws 4 → 2 in `census.txt`) stays refused
  `texel_floor`: the first step, T 300 → 8 (weighted 0.028), falls below the relative floor 75.
- **Clamped layout (`lod_atlas.plan_layout`, 2026-09-23).** The starved tiles of the old
  refusals were not the wide ones: a few faces with saturated UVs (±32768 periods, the 16.16
  limit; in `argon_tech_L_laser_bb` 3,672 of the 23,452 `trims_02`/`trims_03` faces are wider
  than 256 periods, the widest 59,914 and 64,892) or a legitimately long repeat (`argon_gate`
  plating, widest faces 79 and 131 periods) made one tile's full size tens of thousands of
  texels, so the uniform scale fell towards zero and starved every other tile (measured,
  `lod-overlay-batch/uv_outlier_faces_out.txt`). When the uniform layout misses 2 texels/px at a
  size, the clamped layout is tried at the same size: a face whose own UV extent exceeds
  `OUTLIER_SPAN` (256) periods is span-clamped (left out of the tile's span and need, its UVs
  clamped into the tile, so it samples a stretch of one period instead of the repeat's mip-tail
  average: an approximation, inferred acceptable at ≤ a few % of the surface), and every tile's
  scale is capped at 2 / k (k = its texels per pixel at scale 1: no tile holds more than 2 texels
  per pixel, which is invisible at and beyond the switch). Sizes are tried in order, uniform then
  clamped at each size, so a body whose uniform layout reaches 2 only at 2048 now builds clamped at
  1024 (deliberate: a 1024 atlas at 2 texels/px is the fleet budget; 2048 costs four times the
  bytes). Only bodies whose uniform layout passes at 1024 (or reaches scale 1 there) are unchanged.
  Over the 339 flown-sector bodies 153 build clamped: 55 had reached ≥ 2 at 2048 (all now 1024,
  e.g. `Boron_M7` 3.752 at 2048 -> 2.017 at 1024), 71 had a uniform ratio of 0.5–2 and 27 were
  below 0.5. The inverse-map gate now counts a face only
  when it is off by more than 1 source texel *and* 0.1 atlas texel: a capped tile holding 1/300
  of its source density turns the 16.16 rounding of the atlas UV (≤ size/131,072 atlas texels)
  into 2.5–7.9 source texels without any mapping error (`argon_gate`, the tech stations).
  Over the 339 bodies of `eligible_bodies.txt` at 1920x1080 (measured,
  `texel_share_compare.py`): `texel_floor` 28 -> 1 (`x3tc/torus_barrier_node`, 21.4 % starved at
  full source density), 27 bodies newly eligible and none lost, 153 clamped layouts, 21 bodies with
  `texel_clamped` tiles, atlas sizes 1024/2048 177/162 -> 321/18, atlas bytes (census estimate: DDS
  with mips) 4,037,222,808 -> 1,923,293,592 for the 339 (per body in the output). Flown-sector dry run
  (`--only` the three bodies, measured): `argon_tech_L_laser_bb` min 0.046 -> 2.007 at 1024
  (2.69 % starved, 3,672 span-clamped faces in 2 tiles, 356,095 stored atlas bytes, draws 38 -> 6),
  `argon_tech_M_laser_cc` 0.044 -> 2.003 at 1024 (1.30 %, 2,212 faces in 3 tiles, 283,381 bytes,
  36 -> 4), `argon_gate` 0.228 -> 2.025 at 1024 (0 %, 231,042 bytes, 16 -> 2); all three build and
  pass the gate. How span-clamped faces look in game is not verified.
- **Workers.** `--jobs` defaults to min(cpu−2, 6, RAM // 7 GiB − 1), at least 1 (2 on this
  24 GiB host): a worker baking one of the biggest stations (4096² source textures decoded,
  2048² atlases with full mip chains) reaches ~7 GB RSS (measured on the flown-sector dry run),
  and the pool replaces every worker process after one body (`maxtasksperchild=1`).
- **Build gate.** `lod_atlas.build` refuses a body when its own check finds a rewritten vertex UV
  outside its tile content, a face whose inverse-mapped centroid is off by more than 1 source texel
  and 0.1 atlas texel (span-clamped faces excepted), or a face whose material
  is not in its tile's material list; the specular atlas is baked only when an atlased material
  carries `t_SpecularTexture`.
- **Baking cost.** `lod_atlas.level_weights` (the per-axis area-resampling matrix of a tile at
  one mip level) was a per-texel Python loop whose cost scaled with the tile's span in source
  texels: a tiling texture spanning hundreds of UV periods (e.g. `Pirate_M2`'s u range
  −13.6..491.8) cost ~1e9 iterations, and the first flown-sector dry run had two bodies still
  baking after 34 minutes. It is now vectorised (whole periods, the partial interior run and the
  two edge texels folded onto `t mod n` with numpy; weights equal to float32 rounding, max
  |old − new| 1.2e-7 over 307 random and edge cases and 8 of 16 pilot atlases differ from the
  installed ones by 1–18 decoded bytes, measured: `review/lw_check.py`, `review/pilot_atlas_cmp.py`),
  so a 2048-row, 505-period matrix takes 0.01 s.
  The second hot spot was `lod_atlas.check` (b), the per-face sampling diagnostic, whose box
  reference gathers span/content source texels per face and axis: on the same bodies it ran
  for another hour. It now samples at most `CHECK_FACES` (4096) faces, evenly spaced in face
  order, thinned further so that the box reference gathers at most `CHECK_TEXELS` (2e7) source
  texels per slot (`sampled_faces` in the manifest); the UV-inside and inverse-map checks (c)
  still cover every face.
- **Reporting.** Each run prints and records (`x3m-lod-batch.json`, `-summary.txt`,
  `-bodies.txt` under `--out` or `--record`) the counts by refusal reason, atlas sizes and
  bytes, the texels/px ratio, draws before/after, the per-sector resident estimate through
  `lod_batch_census.sector_report` over its census files with a `--budget-mb` warning (default
  512), timings and the extrapolated full-set wall time.
- **Dry runs (2026-09-23, `batch-dryrun/`, measured).** Bottle, flown-sector bodies (`--only
  sectors.txt`, 49 enumerated): 22 overlay bodies (12 at 1024², 10 at 2048²; 37.75 MB of atlases,
  112.38 MB of body members), drawn groups 651 → 67 summed over them; Titan (`argon_M2`) 2
  draws, `military_outpost_middleb` 4, `Argon_m7m` 2 with its 32,187 second-UV points passed
  through, the six 250/150/80/30 stations under the waived guard 3–4 each; refused
  `dominant_slot_missing` 9 (`StockmarketBoardXL` among them, so no flown mixed-effects body
  was built), `text_body` 9, `material_outside_table` 3 (`argon_adsign_C`, two engine
  effects), `ambiguous_body_ext` 1 (`weapondummy`), `no_opaque` 1; four bodies below 1.0
  texel/px (`argon_tech_M_laser_cc` 0.04, `argon_tech_L_laser_bb` 0.05, `argon_gate` 0.23,
  `owp_large` 0.72; that run predates the texel floor, which now refuses the first three); resident atlas
  estimate 21–24 MB per flown sector; census 8.5 s, baking 46.7 s for 22 bodies with 6 jobs.
  Vanilla+mod root (`modroot`, `only_mods.txt`, 3 `.bob` bodies from `addon/06.cat`):
  `teladi_m2_cormorant/hull` 39 → 4 draws with three atlas materials (three effect files),
  `boron_m7turretB_weapon` with 1 tolerated stray byte 7 → 1, `supply_base` 38 → 3; the
  census-level enumeration of that root (`mod_enumeration_out.txt`) finds 2,974 mod-catalogue
  winners (1,274 `.bob`, 167 `.pbb`, 1,482 `.pbd`, 51 `.bod`), 444 of them eligible (all `.bob`;
  435 ships, 9 stations; 82 in the `.pbb`-only census), 86 with tolerated trailing bytes (68
  eligible), 8 refused `trailing_bytes`, 57 eligible mixed-effect bodies, 4 `occlusion_mismatch`,
  and 1,020 eligible bodies overall (593 at 1024², 427 at 2048², ~11.3 GB of atlases estimated).
- **Text bodies (2026-09-23, `verification/results/lod-overlay-batch/text-bodies/`, measured).**
  With `bob1.parse_text` on the engine rule the census of the same vanilla+mod root reads 4,613
  bodies instead of 2,990: 1,623 winning text bodies without a binary twin (1,015 from the mod
  layers, 608 vanilla; text scenes skipped). Eligible bodies go from 1,020 to **1,025**, none
  lost: `khaak_M5` (4 → 2 draws), `KG_Split_turret_frame` (2 → 1) and
  `paranid_stealth_generator` (12 → 1) from vanilla, `stations/others/HQdock` (4 → 1) and
  `HQexitramp` (19 → 2, a 4096² atlas wanted, below 1 texel/px at the batch's 2048² cap) from
  the mod; all five are bump-mapped and now carry no tangent records, as their text load does in
  vanilla. The text rows are mostly not overlay material: 1,405 `category_other`, `no_opaque`
  1,230, `mat3` 164 (MATERIAL3 text bodies index the global material table by nearest match,
  which needs the running game), `non_effect_material` 151, `text_parse_error` 6. A
  `--batch --dry-run --only` over the five builds all of them (41 → 7 draws, 11.21 MB of
  atlases, 14.7 s) and writes each as `<stem>.pbb` with its `source_member` recorded. The
  Mayhem 3 figure of 1,533 text winners includes its text scenes (the root's 1,536 text scenes
  are skipped like `CUT1`); its ships and stations reach the census as `.bob` bodies (inferred
  from the layer counts above). Residuals against the engine's in-memory model (classic-MAT6
  switch bits after a texture overwrite, the part centre of a part without `0x10000000`) are in
  body-format-bob1.md §8.

**Node side effects.** Two node-set side effects change at Very High (objdump of `0047cfe0..`,
`/tmp/x3-lod/f47cfe0.s`). A child node flagged `node+0x12c & 0x40000` is hidden
when its parent (`node+0x18`) is not renderable or has `+0x14c > 0`
(`0047d055..0047d076`); with the pad or compact placement the parent's final
index is `> 0` below `T_pad` (before: below 15 px for these ladders), so attached
children of the pilot bodies disappear at the stand and their draws count in the
saving. And a final index `>= 3` sets `node+0x130 |= 0x100000` (`0047d519`, the
`BUMPMAP` → `DEFAULT` switch): with pad `C` sits at index 4 (ships) or 3
(outpost), so the flag is set at Very High where these bodies never reached
index 3 before; compact keeps `C` at index 1.

Pilot overlay (written with `--out` to an untracked build directory, not
installed; 2026-09-23): `argon_TL`, `argon_M2`, `argon_M1` with `T_pad = 50` and
`military_outpost_middleb` with `T_pad = 100`; at Very High each draws `C` (2
groups: opaque + alpha) for `s < T_pad` and LOD 0 above. Per-body numbers, file hashes and the
round-trip check are in `verification/results/lod-overlay-pilot/`.

Every installed CAT/DAT is hashed before and
after a real run (outputs are removed if any changed), and an
`addon/NN.x3m-lod.json` manifest records the source and overlay hashes (and,
since rule e, the collapse, glow thresholds and per-body glow materials). Source
bodies are always read with every marker-carrying catalogue skipped. While a
manifest is installed the tool refuses unless `--replace`. With `--replace`, the
manifest's originals hash must match the installed archives other than its slot,
and the new overlay takes that slot. `--replace --install` moves the old three
files aside (`*.x3m-replaced`), writes the new ones and deletes the asides
last. If moving aside, writing or the originals check fails, every file that was
actually moved goes back over any new output at its path. The remaining new
outputs are removed only once all of them are back. A file that cannot be put
back stays as `*.x3m-replaced` and is reported, and a leftover aside blocks the
next `--replace`. A failure to delete an aside after success is only a warning:
the new overlay stays installed. `--replace --out` only builds the replacement.
`--install` refuses while `X3AP.exe` runs, or while the process table cannot be
read (`verification/probe/game_guard.py`), unless `--force-running`: the running
game keeps the old CAT/DAT open with the catalogue index in memory. On native
Windows the check has no `ps` and so refuses. Renaming a CAT/DAT that the game
has open fails there unless it was opened with `FILE_SHARE_DELETE`, and the
restore path then runs.

Rule-e rebuild (2026-09-23, `--replace --out <worktree>/build-overlay`, not
installed): `C` is 5 draws for argon_TL, M2 and M1 (opaque + 4 glow) and 3 for
the outpost (opaque + glow material 41 + alpha group under material 9). The build gives
`05.cat` 191 bytes, sha256 `def76feb…`, and `05.dat` 12 471 651 bytes, sha256
`b3fbf984…`. The check is
`verification/results/lod-overlay-pilot/pilot_check_glow_out.txt`.

Compact rebuild (2026-09-23, `--replace --out <worktree>/build-overlay`, glow
collapse, same four bodies and `T_pad`, not installed): each body has 3 records,
`[T_0, 30, 50]` for the ships and `[T_0, 30, 100]` for the outpost; at Very High
`s < T_pad` draws `C` (LOD1), at Low..High the pad (LOD2); the highest main-view
final index is 2. `C`'s groups are unchanged from the glow build (5 / 5 / 5 / 3
draws). Record 0 is byte-identical to the source, the pad is a copy of `C`, and
the last record equals the source's coarsest in geometry. `05.cat` is 190 bytes,
sha256 `cc9553c1…`, and `05.dat` 7 789 651 bytes (12 471 651 with pad), sha256
`0833efa3…`. The check is
`verification/results/lod-overlay-pilot/pilot_check_compact_out.txt`.

`python3 tools/analysis/bob1.py audit [--summary] [--json OUT] [--view-distance
{low,medium,high,very-high}] [--factor F]` reports each body's main-view drawable
records at the chosen setting (default very-high) and classes
(`verification/results/bob1-format/bob1_audit_out.txt`, measured over 1634
parsed `BOB1` bodies, 950 multi-LOD; classes overlap). At Very High with f = 2:
**684 single-LOD**; **950 whose last record is never drawn**; **75 that can
only draw LOD 0**; **12 with a record drawable at no main-view setting** (23
records; 13 bodies and 24 records at f = 1), e.g. `argon_dock_center` 30/10/3/30
with records 1–2 dead; and 807 bodies (581 multi-LOD) whose coarsest *drawable*
record draws more than one group. These agree with
`verification/results/bob1-format/lod_drawn_sets_out.txt`.

```sh
python3 tools/analysis/bob1.py info objects/stations/station_scenes/others/argon_L_solarpowerplant
python3 tools/analysis/bob1.py audit --summary [--view-distance very-high] [--factor 2]
python3 tools/analysis/lod_overlay.py --dry-run <body>[=T_pad] ... [--threshold T] [--placement P]
python3 tools/analysis/lod_overlay.py --out <scratch dir> <body>[=T_pad] ... [--threshold T] [--placement P]
python3 tools/analysis/lod_overlay.py --install <body>[=T_pad] ... [--threshold T] [--placement P]   # game dir; never overwrites
python3 tools/analysis/lod_overlay.py --install --replace <body>[=T_pad] ...   # swap the installed overlay (originals hash checked)
python3 tools/analysis/body_materials.py <body> [--lod N] [--area-percent 50,70,90,100]   # census, draws per rule, rule f, diffuse substitution
python3 tools/analysis/lod_overlay.py --out <scratch dir> --collapse glow-area 70 [--no-synth-material] <body>[=T_pad] ...
python3 tools/analysis/lod_overlay.py --out <scratch dir> --collapse atlas [--atlas-size 1024] [--atlas-max-size 2048] [--atlas-format dxt|a8r8g8b8] [--atlas-specular] [--atlas-preview DIR] [--source-record N] <body>=T_pad[@N] ...
python3 verification/results/lod-overlay-pilot/atlas_mip_bleed.py <overlay root> ...   # light-atlas mips 0-4 against the tiles' own maps
python3 tools/analysis/lod_batch_census.py --out <dir> [--jobs N] [--screen-width 1920] [--ship-min 80 --ship-factor 2.5 --station-t 150 --t-cap 200] [--atlas-max-size 4096]   # fleet eligibility, atlas sizes, costs, flown sectors
PYTHONPATH=verification/probe /usr/bin/python3 -m unittest verification.analysis.test_bob1
```

Removing `addon/NN.cat`, `.dat` and `.x3m-lod.json` reverts the install. The
pilot flight must show, at the run240 stand with `--cull-census`, the overlaid
body's nodes reporting the new record's `lod` index (1 for compact; `n-2` of the
new ladder, i.e. the old record count, for pad and append-pad) wherever `s < T_pad`, where
they reported LOD 0 before, and their per-node draw count dropping to the group
count of that record (2 in the installed pilot; 5 for the ships and 3 for the
outpost with `glow`), with `draws_p50` compared at the same stand. It must
also check that the bodies' `0x40000` children vanish below `T_pad` (their draws
belong to the saving; any child that should stay visible is a regression), that
the engine glows stay lit below `T_pad` (`glow`). This is not a given: in run257
the ships' coarse draws ran pixel shader `8759c7838bbc86c2`, not the LOD 0
shaders `5e0a10fe…`/`ca6b…`, with stage 3 bound to id 722 and no size logged
(`verification/results/run257-pilot/tex_run257_3615.txt`), and whether that
shader samples a light map at all is untraced. For an atlas build (`--collapse
atlas`) the flight must also check that `C` shows the per-material diffuse, the
lit windows and exhaust glows, and bump relief (the bump atlas). On the outpost
it must check whether the specular highlight is lost or wrong: its atlas material
has `t_SpecularTexture` NULL, and `XT_standard_lighting.fx` has no shipped
material with that slot NULL (0 of 8839). The ships' `argon.fx` has 120 of 7198.
The flight must also check that the outpost's alpha group
still renders as a cut-out rather than solid, and whether
the `node+0x130 & 0x100000` flag changes anything visible (with compact it must
no longer be set by the index rule on these nodes at Very High). A file that loads without a `lod` or
draw change is not acceptance. The run255 census names the heavy stand bodies,
which the pilot overlay targets; the two earlier worked examples
(`argon_L_solarpowerplant`, `argon_dock_center`) already end in a one-group LOD,
so collapsing them saves no draws.

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

**Pilot installed 2026-09-23:** `addon/05.cat` (`c5737a0a…`) / `05.dat` (`66f4f7a6…`) with
argon_TL, argon_M2, argon_M1 at T_pad 50 and military_outpost_middleb at T_pad 100, two
groups per coarse record; [install record](../../verification/results/lod-overlay-pilot/install.json);
flight queued as Run 69 A ([user-runs.md](../verification/user-runs.md)).
