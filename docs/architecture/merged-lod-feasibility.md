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
shader samples a light map at all is untraced. The flight must also check that the outpost's alpha group
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
