# Carrier dock-port parts: the unnamed single-record nodes under capital ships

2026-09-29. Read-only study of the `body=-` nodes that dominate the run385 burst A draw count
([run385 attribution](../../verification/results/run385-draw-attribution/README.md): 186 of 642 draws
under the Split Raptor and Ocelot). Installed `X3AP.exe` SHA-256 `fdbf3418…f8ab` (preferred VAs, base
`0x00400000`); a fresh Ghidra 12.1.3 import in the session scratchpad (the `/tmp/x3-ghidra-research`
project had been emptied) with `tools/analysis/X3DecompileFunctions.java`, and an
`i686-w64-mingw32-objdump -M intel` listing; the Mayhem 3 install and its stock layers
(`STOCK_AP_CATALOGUES`) read through `tools/analysis/sector_fog_census.Assets`; bounded queries of the
run385 session log. No game launch, no Wine, no build. Raw decompiler output, the listing and extracted
game files stay untracked. Scripts and small outputs:
[`verification/results/ship-scene-parts/`](../../verification/results/ship-scene-parts/); the inline-body
resolver is `verification/results/run341-draw-calls/name_model_id.py` (first identification of
`35ba45c3` in [draw-calls.md](../verification/draw-calls.md), 2026-09-26).

**Answer.** The nodes are the **carrier dock ports**: every ship scene part whose body is a
`types/Dummies` `SDTYPE_ANIMATED` dock-port body (`19026` quick-launch tube, `19027` hangar; `19098`/`19099`
for the M6 variants) is replaced at object construction by the cut scene the dummy names
(`types/CutData` 9013 `DockCarrier_quicklaunch_scene`, 9014 `DockCarrier_scene`), and that scene's
**inline** text bodies (`P n; B 1000nn; … L { … }`) become render nodes with model id
`local + (cut − 1)·100000`. That id has no body-table slot, hence `body=-`; the inline text load gives one
record with `+0x34 = 10000`, hence `lods=1 thr=10000`. The Raptor scene declares 10 × `19026` and 1 × `19027`,
which is 33 drawn nodes and exactly 117 draws; the Ocelot 6 + 1, 69 draws. The mechanism, the dummies, the
cut table rows and the dock scenes are **stock** (identical bytes in the stock layers); Mayhem only adds more
carriers that use them. No engine flag ties them to the hull's LOD: they are not the hull's children but
children of an always-hidden dummy node that is the hull's sibling. The cheapest effective lever is a
dock-port-scoped size threshold in the existing `cull_small_parts` pass stub (§4).

## 1. Loader chain and model ids [inferred from disassembly, confirmed by data]

| address | role |
| --- | --- |
| `0x0043ce30` | type-table object construction: root node, then per scene part `0x0043d1d0` |
| `0x0043d1d0` | scene-part builder (recursive). Part record stride `0x58` at `scene+0x50`, body id at `+0x30`. Allocates the node (`0x00486d10`), then `0x0043c980(body id, &type)` |
| `0x0043c980` | dummy lookup: scans the five `types/Dummies` tables `0x006070c4[type]` (counts `0x006070d8[type]`, record stride `0x60`), returns the record and the type index (file section order: 0 ANIMATED, 1 DOCK, 2 DOORWAY, 3 GUN, 4 CONNECTION) |
| `0x0043d1d0` (cont.) | a dummy record with states and a cut id → `0x00492970(cut id)`; for type 0 with a non-zero `ANIMATEDF_*` word the dummy node gets `+0x12c \|= 0x100000`; then each cut-scene part is built by the recursive call at `0x0043d67a` **as a child of the dummy node** |
| `0x0043d68d..0x0043d6aa` | on the returned child: type 3 (GUN) → `+0x1d8 = 10`, type 1 (DOCK) → `+0x1d8 = 8`; **type 0 (ANIMATED) writes nothing**. `0x0043d855..0x0043d886` does the same on the dummy node itself |
| `0x00492970` | cut-scene loader: cached by cut id; name from CutData or `cut\%05d`; tries `pbb bob pbd bod`; binary `CUT1` → `0x00490ec0`, text → `0x00491b10` |
| `0x00491b10` case `'L'` | `id = 0x0046e400(name) − 100000 + cut·100000` (`0x004920ec..0x00492105`, into `0x00609988[part]`); the name is the digit string `1000nn`, which `0x0046e400` `sscanf`s without registering a slot; the inline text between `{` and `}` is loaded by `0x00486310(id)` → text body loader `0x00483f20` ([body-text-loader.md](body-text-loader.md)); a non-zero `0x00480790` result skips the load (read as "already loaded") |
| `0x00491521..0x00491534` | the same formula for binary `CUT1` embedded bodies |

Because the scene and its models are cached per cut id, all ten quick-launch tubes of a Raptor share the
three model ids `901300000/1/3`, and every carrier in the sector shares them too. `0x0046df60`'s id → slot
rule puts `901300003` at slot `11000 + 901280003`, beyond `g+0xb4 + g+0xb8` (about 13,200), so the census
prints `body=-` for every such id (`cull_census_core.h`, `body_slot`). The `B 535` parts of the dock scenes
(`0x217`, compared at `0x0043d6xx` into the dock-position table) produce no census row and no draw in
frame 4827 (measured).

**Frame 4827 (run385 burst A), measured** (`dock_part_rules_out.txt`, `object_context` draws per node):

| model id | cut / part | inline body | radius (= body scale) | groups = draws per node | Raptor nodes | Ocelot nodes |
| --- | --- | --- | --- | --- | --- | --- |
| `35b8bf20` 901300000 | 9013 P 0 | `Ehangar_door_upper_1` | 8,992 | 1 | 10 | 6 (all `culled_small`) |
| `35b8bf21` 901300001 | 9013 P 1 | `Ehangar_door_lower_1` | 8,992 | 1 | 10 | 6 (all `culled_small`) |
| `35b8bf23` 901300003 | 9013 P 3 | `Ehangar_small` (quick-launch tube, 432 faces) | 16,354 | 7 | 10 | 6 |
| `35ba45c3` 901400003 | 9014 P 3 | `EObject01` (hangar interior, 3,994 faces) | 39,157 | 25 | 1 | 1 |
| `35ba45c4`/`c5` 901400004/5 | 9014 P 4/5 | hangar doors | 25,270 / 25,178 | 1 | 1+1 | 1+1 |

Raptor: 10 × (1 + 1 + 7) + (25 + 1 + 1) = **117** draws on 33 nodes; Ocelot: 6 × 7 + 27 = **69** on 9 kept
nodes (its 12 tube doors are `culled_small` at s = 1). Groups per inline body from
`name_model_id.py` (the engine's grouping, one draw per group); draws per node from `object_context`.
Ancestry (measured, `object_ancestor`): part → dummy node (e.g. `287a46d8`) → ship root `286016f8`;
the hull `28601978` is another child of the same root. The dummy nodes have no census row (their bit 2
is cleared at `0x0047d030` by the `0x100000` test at pass entry, so the pass jumps to their child walk at
`0x0047d085`; inferred from the code, consistent with the absence). The hull's census row precedes the 33
part rows, which precede the turret props (row order = pass visit order, measured in this frame).

Scene data (measured, `dock_port_data_out.txt`): the installed Raptor scene (`addon/06.cat`
`split_m1_raptor_scene.pbd`) has 35 parts: the hull, 10 × `19026`, 1 × `19027`, 10 turret dummies,
7 camera dummies, 6 engine effects. The dock cut scenes have 4 parts (3 inline) and 6 parts (3 inline).

## 2. Stock or mod [measured]

- The stock Split M1 (`TShips` `SG_SH_M1` row, scene `ships\split\split_M1_scene`, the vanilla Raptor;
  `06.cat`) carries the same 10 × `19026` + 1 × `19027`. The Mayhem `split_m1_raptor_scene` is a new file
  (turrets and engines differ) with the same dock-port set. `split_m2p_ocelot` has no stock counterpart.
- `types/Dummies` rows `19026 → 9013`, `19027 → 9014`, `19098 → 9098`, `19099 → 9099` are identical in the
  stock (`addon/03.cat`) and installed (`addon/07.cat`) tables; `types/CutData` 9010/9013/9014/9098/9099
  are identical too.
- The dock cut scenes are the stock `02.cat` members with the same decoded SHA-256 in both views
  (`dockCarrier_quicklaunch_scene.pbd` `34f58000…`, `dockCarrier_scene.pbd` `ebad8942…`,
  `M6dockCarrier_scene.pbd` `1eaa10b5…`); `M6dockCarrier_quicklaunch_scene` (`addon/03.cat`) references
  named bodies, not inline ones.
- Text ship scenes referencing a dock-port dummy: stock 36 of 399 (249 × `19026`, 33 × `19027`,
  10 × `19098`, 2 × `19099`), installed 95 of 875 (609 / 83 / 51 / 10). Binary `CUT1` ship scenes were not
  scanned.

So the parts are vanilla engine behaviour on every carrier-class ship; Mayhem multiplies the number of
carriers, not the parts per carrier.

## 3. Which engine paths can hide them [inferred from disassembly; flags measured]

Census rows of the parts (all three bursts, measured): `flags_in = 08001002`, `thr_1d8 = 0`, `limit = 0`,
`lods = 1`, `thr = 10000`. No `0x40000`, no `0x8000`.

| path | why it does not tie the parts to the hull's LOD |
| --- | --- |
| `0x40000` child gate `0x0047d055..0x0047d076` ([lod-child-hide.md](lod-child-hide.md) §2) | It reads the **immediate parent**, which is the dock dummy node, not the hull. The dummy is never renderable (`+0x12c & 0x100000` → bit 2 cleared at `0x0047d030` every view) and its `+0x14c` is 0 (`0x0047d001`), so a part carrying `0x40000` would be hidden **always**, never by distance. No scene-file field writes `+0x12c`; the bit is set by no EXE code (lod-child-hide.md §2.1). |
| `0x8000` hide at the coarsest record (`0x0047d4d7..0x0047d502`) | Needs final index `> 0` and `== n−1`; with one record `n−1 = 0`, never. |
| per-node size limit `max(+0x1d8, parent+0x1d8)` (`0x0047d294..0x0047d2c1`) | Written only for GUN (10) and DOCK (8) dummy children; ANIMATED children keep 0 (measured `limit=0` on every dock-port row; the `dock5ports_arm` `Object01` rows carry `limit=8`, cause not traced). |
| engine `measure < 1` (`culled_min`) | `measure` is 4·s at 5120 wide here (57 vs s = 14); the parts reach it only far beyond 18 km. |
| `+0x130 & 0x100000/0x80000` (below 20 px) | Technique switch only (lod-child-hide.md §4); `object_context` shows `flags130 = 00180040` on some part draws. |

The brief's "none at 18 km, the engine's own pixel cull" is the proxy: every dock-port row at 17–18 km in
burst B is `culled_small`, i.e. `--cull-small-parts` 4 px (threshold s < 3 at `m00 = 0.5`, width 5120), not an
engine verdict (measured). At 13.6 km (burst C) the hangar interior is still kept at s = 3 (25 draws).

So **no flag makes the engine hide them when the hull is on its coarse record**, and none of the flags the
engine does test is set on them.

## 4. Options, ranked

Implemented 2026-09-29 as option 1: `X3M_CULL_DOCK_PARTS_PX` / `--cull-dock-parts` in the `cull_small_parts` stub
(`src/proxy/cull_small_parts_core.h`; [cull-small-parts.md](../verification/cull-small-parts.md), "Dock ports"); the
8 px default is `s < 5` at run385's projection, 12 px is the table's `s < 8`.

Replay on the run385 rows (`dock_part_rules.py`, measured counts; dock-port parts = model id `// 100000` in
{9013, 9014, 9098, 9099}; draws per node from `object_context`):

| rule | A 4827 (Raptor 3.3 km, Ocelot 6.2 km) | B 8142 (Raptor 7.8 km) | C 10210 (Raptor 13.6 km, freighter dock 16 km) |
| --- | --- | --- | --- |
| kept dock-port draws today | 186 | 27 | 52 |
| hide while the hull draws record ≥ 1 | 0 (both hulls at LOD 0) | 27 | 25 (the freighter base is at LOD 0) |
| s < 6 | 64 | 2 | 25 |
| s < 8 | 159 | 27 | 27 |
| s < 10 | 161 | 27 | 52 |
| s < 16 | 186 | 27 | 52 |

(`s` is the pass's `r·640/D`; px radius = s · m00 · width / 1280 = 2·s in these frames. A hull-LOD tie
corresponds to s < 120 · r_part / r_hull for the Raptor, i.e. 8.7 for the hangar interior, 3.6 for a tube.)

1. **Recommended: a dock-port-scoped threshold in the `cull_small_parts` pass stub.** At the existing site
   `0x0047d2a2` (verified window `0x0047d294..0x0047d2cc`, lod-selection.md "Cull small parts site"), a second,
   larger threshold applies when `[edi+0x140]` lies in `[901300000, 901499999]` or `[909800000, 909999999]`
   (two unsigned range compares after `lea`; `+0x140` is dereferenced by the pass itself at `0x0047d19b`
   before the site, so it is live and valid). Below it the stub takes the existing cull path to `0x0047d2c3`.
   Site facts are unchanged: EAX/ECX/EFLAGS dead, EDX untouched, x87 empty, no call; the stub already
   saves EAX around its compare; the pass recurses (`0x0047d53c`) and the stub stays stateless apart from
   its per-frame counter, so reentrancy is as today. Cost: one load and two compares only for nodes below
   the larger threshold. Being a pass-time cull, it also removes the engine's own per-draw work (the
   21–24 µs/draw of run384's `views` phase), which the draw-time `cull_small_props` skip does not; and
   unlike the props (node radius about nine times the mesh), these nodes' radius equals their body scale, so
   `s` measures them honestly. Risk: the parts pop at a fixed size, independent of the hull; the hangar
   interior sits inside the hull (its 39,157 radius against the hull's 539,786) and how much of it shows
   through the open bay is **not known**. Portable (engine bytes only, no Wine dependency).
2. **Engine creation-time limit.** One byte at `0x0043d6a1` (`75 0a` `jne` → `77 0a` `ja`) makes ANIMATED
   children take the DOCK value `+0x1d8 = 8`; the immediate at `0x0043d6a3` sets the value for both types.
   Crude: `limit` compares against `measure` (resolution-dependent, 4·s here), so 8 is s < 2, below today's
   stub; a useful value also changes station SDTYPE_DOCK children; only nodes built after the patch are
   affected (the savegame node loader `0x0047a005` restores whole words; whether it restores `+0x1d8` was
   not traced). Not recommended.
3. **Data: give the inline bodies a coarse record.** The cut scenes are text; a record `C` (collapsed
   materials, 25 → 1–2 groups for the interior, 7 → 1–2 for a tube) appended inside `L { … }` would be
   selected by the parts' own `s`, keeping part indices, door animation keys and the `535` markers. It
   needs tooling that does not exist: `bob1.parse_text` refuses scenes and there is no text or `CUT1`
   writer (a binary `.pbb` `CUT1` would be read first by `0x00492970`'s `pbb bob pbd bod` order only if §7
   precedence of body-format-bob1.md holds for scenes, not checked). Collision is unaffected (the parts
   carry `0x8000000`, so `0x0043d1d0` builds no collision tree from their records; inferred). It changes
   every carrier in both stock and Mayhem data. Merging the parts into the **hull's** merged record, as
   the brief's option (c) proposes, does not help on its own: the part nodes would still draw unless hidden,
   and in burst A both hulls are at LOD 0 anyway.
4. **Hull-LOD tie by patch** (hide a dock-port part when its grandparent's hull sibling draws record ≥ 1).
   The gate would need a stub that walks dummy → root → child list to find the hull; the hull is visited
   first in this frame, but that is the scene part order, not a contract. It saves nothing in burst A and 27 / 25
   draws in B / C. Not recommended.

**What a fixture can prove without a flight.** (a) The existing CPU fixture
`verification/probe/cull_small_parts_fixture.cpp` with synthetic nodes carrying `901300003`, `901400003`,
`909900005` and the neighbours `901299999`, `901500000`, `909799999`, a body-table id and the bullet
marker: exact selection, the unchanged native path above the thresholds, registers, flags, LastError,
rollback. (b) A replay of the tracked run385 rows (as `run131-cull-census-rows.json` for the stub) giving
159 / 27 / 27 at s < 8. (c) The visibility question offline: rasterise the Raptor hull plus the dock-port
parts at their scene transforms (ship scene `P` positions and quaternions, then the cut-scene part keys)
from a sphere of directions at 3–8 km and count unoccluded part pixels; zero or near-zero visible pixels
would make even an unconditional hide safe (186 / 27 / 52). The raster is not built; `tools/analysis/lod_raster.py`
was not checked for scene composition.

## Unknown

- How much of the hangar interior and the tube interiors is visible through the open bay from outside,
  and whether the door parts animate open in flight (the quick-launch tubes are `STARTONLY`).
- Whether a binary `CUT1` member outranks the text `.pbd` for a cut scene in `0x00492970` (it tries
  `pbb bob pbd bod`; the §7 resolver order was measured for bodies, not scenes).
- The `limit=8` of the `dock5ports_arm` `Object01` parts (a station dock, cut 9010).
- Whether the savegame loader restores `+0x1d8`.

## Reproduce

```sh
python3 verification/results/ship-scene-parts/dock_port_data.py \
  > verification/results/ship-scene-parts/dock_port_data_out.txt           # about 1 s, bottle X3, read-only
python3 verification/results/ship-scene-parts/dock_part_rules.py \
  /tmp/x3-bottleX3-run385/session-20260929-173412-212.log \
  > verification/results/ship-scene-parts/dock_part_rules_out.txt          # about 1 s
python3 verification/results/run341-draw-calls/name_model_id.py 35b8bf20 35b8bf21 35b8bf23 35ba45c3 35ba45c4 35ba45c5
# Ghidra (raw output stays local): -import X3AP.exe into a scratch project, then
#   -postScript X3DecompileFunctions.java <out> 0043d1d0 0043ce30 00492970 00491b10 0043c980 00486310
# objdump windows: 0x0043d670..0x0043d6c0, 0x0043d850..0x0043d890, 0x004920c0..0x00492130,
#   0x00491500..0x00491540, 0x0047cfe0..0x0047d1c0
```
