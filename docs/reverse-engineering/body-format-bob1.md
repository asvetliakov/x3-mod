# Binary body format `BOB1` (`.pbb` / `.bob`) and how it becomes LOD records

Read-only study, 2026-09-22, for the merged-LOD asset pilot
([merged-lod-feasibility.md](../architecture/merged-lod-feasibility.md) §2, §6).
Installed `X3AP.exe` SHA-256 `fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`,
preferred VAs, base `0x00400000`. Ghidra 12.1.3 headless, freshly re-imported
`/tmp/x3-ghidra-research/X3Render` (the earlier project had been emptied by the
`/tmp` cleaner), `-readOnly -noanalysis` decompilation of the functions named
below, and capstone windows of the same EXE for the instruction-level checks.
Raw decompiler output and extracted bodies stay in the session scratchpad,
untracked. No game launch, no Wine command; the bottle was read only.

**Result.** The engine's binary body parser is **`0x00481aa0`**. It reads a
flat tagged stream against the tag table at **`0x0054ed50`**, and the layout it
implies was checked by a parser/serialiser that round-trips **1634 of the 1635
installed `BOB1` members byte for byte**; the one failure is a shipped body that
is truncated right after `BODY 00 01` (§5). A LOD record's subsets are exactly the **groups of its `PART`
block**: one group = one material index + one face list = one `0x1a8` subset
record = one `DrawIndexedPrimitive`. A coarse LOD is therefore "one more
`<threshold> <flags> POIN … /POI PART … /PAR` block inside `BODY`, whose `PART`
has fewer groups".

## 1. Loader entry and tag dispatch

| address | role |
|---|---|
| `0x004863c0` | Body registry get-or-load (hash keyed on `id + 1`). On a miss it builds the name (`"v\%05"` at `0x00561164`, or the registered name), opens a stream with the extension list **`"pbb bob pbd bod"`** (`0x00561180`) via `0x004e9840(table 0x0054ed50, 0x12)`, then: payload starts with `"BOB"` → **`0x00481aa0`** (binary); otherwise → `0x00483f20` (text `.bod` parser). |
| `0x00481aa0` | **BOB1 parser.** Allocates the `0x70`-byte model, loops `tag = 0x004e9b70(1)` and dispatches on the returned table index (switch at `0x00481d62`); returns the model when the `/BOB` closer arrives. Other caller: `0x00482f00` (same parser over an in-memory buffer; its caller `0x00491541` is presumably the `CUT1` "Embedded body" path, inferred from the `CUT1` table, not traced). |
| `0x00483f20` | Non-`BOB` (text `.bod`/`.pbd`) parser, decompiled in [body-text-loader.md](body-text-loader.md): same model structure; normalises each record, ignores `/! … !/` blocks, computes its own normals, builds no tangent records. When `*(0x606f34)+0x108 & 0x800` (command line `-convertbodies`, `0x004ed03c`) the text-loaded model is re-written by **`0x00482fb0`**. |
| `0x00482fb0` | `-convertbodies` BOB1 writer to `vc\%05d.bob` / `vc\%s.bob` (`0x004e9f70(stream, tagIndex, open/close)` emits tag/closer, `0x004eb3f0` emits `u16`); writes no effect-material data, tangent records or bounds ints ([body-text-loader.md](body-text-loader.md) §9). |
| `0x0054ed50` | Tag table, 18 entries × `0x14`: `{+0 description, +4 tag, +8 closer, +0xc text label, +0x10 id}`. Strings live at `0x00560d60..0x00560f6c`. |
| `0x0054eed0` | The `CUT1` scene table (same shape; `BOB1` appears there as "Embedded body"). 53 `.pbb` members are `CUT1`, not `BOB1`. |

Tag table (`index` is what `0x004e9b70` returns; `0x12` means "the expected closer was read"):

| index | tag | closer | handled by `0x00481aa0` |
|---|---|---|---|
| 0 | `DATA` | `/DAT` | error |
| 1 | `BOB1` | `/BOB` | container; `/BOB` → finalise and return |
| 2 | `NAME` | `/NAM` | cstring → model `+0x4c` |
| 3 | `VERS` | `/VER` | one `u32`, discarded |
| 4 | `SND1` | `/SND` | one `u32`, discarded |
| 5 | `FLAG` | `/FLA` | error |
| 6, 7 | `MAT1`, `MAT2` | `/MAT` | not parsed (falls through without consuming; unsupported) |
| 8, 9, 10 | `MAT3`, `MAT5`, `MAT6` | `/MAT` | materials, version 3 / 5 / 6 |
| 11 | `BODY` | `/BOD` | LOD records |
| 12 | `BONE` | `/BON` | inside `BODY`, per LOD, optional |
| 13 | `POIN` | `/POI` | inside `BODY`, per LOD |
| 14 | `WEIG` | `/WEI` | inside `BODY`, per LOD, optional |
| 15 | `PART` | `/PAR` | inside `BODY`, per LOD |
| 16 | `INFO` | `/INF` | cstring, discarded |

**Stream primitives** (stream object: `+0x08` cursor, `+0x14` table, `+0x18`
entry count, `+0x1c` binary flag, `+0x20` depth, `+0x24` error flag). In
binary mode every value is **big-endian**:

| function | binary read | notes |
|---|---|---|
| `0x004e9b70(expectedId)` | 4-byte tag | matches tag or closer; a closer is accepted only for `expectedId`; anything else sets `+0x24` (parse aborts) |
| `0x004ea360`, `0x004ea6b0` | `u16` | `0x004ea6b0` takes a name table for the text form |
| `0x004ea610`, `0x004ea440` | `i32` | |
| `0x004ea660`, `0x004ea9d0` | `u32` | `0x004ea9d0` takes a flag-name table for the text form |
| `0x004eafc0` | `i32` raw | used for coordinates, normals, UVs |
| `0x004eaf50` | `i32` 16.16 fixed → float | scale `1/65536` (float at `0x005654e0`) |
| `0x004ea1b0` | NUL-terminated string | cursor advances `len + 1` |

File grammar (all counts big-endian; `[ ]` optional; sections at the top level
are processed in file order, each closed by its own closer):

```
BOB1  { INFO cstr /INF | NAME cstr /NAM | VERS u32 /VER | SND1 u32 /SND
      | MAT3|MAT5|MAT6  i32 n  material×n  /MAT
      | BODY  u16 nLOD  lod×nLOD  /BOD }                 /BOB
lod  := i32 value  u32 flags  [BONE i32 n cstr×n /BON]
        POIN i32 n point×n /POI  [WEIG i32 n weights×n /WEI]  PART i32 n part×n /PAR
```

Observed section order in all parsed members: `INFO MAT6 BODY` 1630, `MAT6 BODY` 3,
`INFO MAT5 BODY` 1 (measured).

## 2. Materials (`MAT6`, `MAT5`/`MAT3`)

Model `+0x54` = count, `+0x58` = array of `0x60`-byte records. A group's material
index is the **position in this array** (record `i` at `+0x58 + i*0x60`), not the
record's own `u16`. `MAT5`/`MAT6` set model `+0x50 |= 0x10` (use per-body materials);
without it the index goes to the global table `0x00608db0` (stride `0x3c`).

| field (file order) | MAT3/MAT5 | MAT6 | stored at |
|---|---|---|---|
| index | `u16` | `u16` | `+0x00` |
| flags | — | `u32` | `+0x28` |
| **if MAT6 and `flags & 0x02000000`: effect material, `0x00470490`** | | `u16` technique, cstring effect (`.fx`), `u16` param count, then per param: cstring name, `u16` type, value | object at `+0x5c` |
| texture | `u16` id | cstring | `+0x02` |
| 12 × colour/intensity words | `u16`×12 | `u16`×12 | `+0x04..+0x1a` |
| two words | `u16`, `u16` | `u16`, `u16` | `+0x24`, `+0x26` |
| flag word | `u16` (OR-ed into `+0x28`) | — | `+0x28` |
| word | `u16` | `u16` | `+0x2c` |
| three (map, value) pairs | `u16`,`u16` ×3 | cstring,`u16` ×3 | `+0x2e/+0x30`, `+0x32/+0x34`, `+0x36/+0x38` |
| two more (map, value) pairs | — | cstring,`u16` ×2 | `+0x1c/+0x1e`, `+0x20/+0x22` (`+0x28 \|= 0x8000`) |

Effect parameter types (name table `0x0054f0a0`, `SPTYPE_*`): 0 `LONG` i32, 1 `BOOL`
i32, 2 `FLOAT` 1×16.16, 3 `FLOAT2` 2×, 4 `FLOAT3` 3×, 5 `FLOAT4` 4×, 6 `MATRIX3` 9×,
7 `MATRIX4` 16×, 8 `STRING` cstring. Any other type sets the error flag.
Census over all 1634 complete `BOB1` bodies (`material_forms_out.txt`, measured):
19 051 `MAT6` effect materials, 160 `MAT6` classic (non-effect) materials, 1 `MAT5`
material; all round-trip with this layout. Effect parameter types in use: `LONG`
366 404, `BOOL` 143 458, `FLOAT` 514 333, `FLOAT4` 128 876, `STRING` 141 791; types
3, 4, 6, 7 and the `MAT3` form are decoded from code only.

## 3. `BODY` → LOD records

`0x004823c7`: `u16 nLOD` → model **`+0x10`** (word); `+0x0c` = array of `nLOD`
pointers to **`0x60`-byte LOD records** (allocation `0x0048242a`). Per LOD `i`:

| file field | code | LOD record |
|---|---|---|
| `i32` value, **LOD 0** | `0x0048249b` | `+0x14` = value (object scale); `+0x34` = **`100000`** constant (`0x004824a3`) |
| `i32` value, **LOD ≥ 1** | `0x00482486` | `+0x34` = value = **switch threshold** `T_i`; `+0x14` copied from LOD 0 |
| `u32` flags | `0x004824af` | `+0x30`; LOD 0 carries `0x0` in 816 bodies and `0x40` in 818 (measured; meaning of `0x40` not traced) |
| `BONE i32 n, cstr×n /BON` | `0x004824c0` | `+0x40` n, `+0x44` names; model `+0x50 \|= 8` |
| `POIN …` | `0x004825e2` | `+0x00` point count, `+0x08` points (`0x18` stride), `+0x0c` second UV set (`8` stride; `+0x30 \|= 0x20`) |
| `WEIG i32 n (= point count), per point u16 k, k×(u16 bone, i32 weight) /WEI` | `0x0048279e` | `+0x48` per-point counts, `+0x50` per-point arrays, `+0x4c` max |
| `PART …` | `0x004828c1` | `+0x04` part count (word, read as `i32`), `+0x10` part pointer array |
| — | `0x00482da9` → `0x0047f350` | `+0x20/+0x24/+0x28/+0x2c` extents (see [render-node-bounds.md](render-node-bounds.md)) |
| — | — | `+0x54` = owning model |

The threshold of LOD 0 is never read by the ladder: `0047d429..0047d46e` walks
`i = nLOD-1 … 1` and takes the first `i` with `s < T_i·f`; the drawn index is that
choice `+1` in the env-map view or `-1` at View Distance Very High, clamped
([lod-selection.md](lod-selection.md), "What the selection really does, end to
end"). The flags word at `+0x30` is not read by the selection. A body with `nLOD = 1` never enters the loop.
Across the 950 multi-LOD bodies, 935 have strictly decreasing `T_1 > T_2 > …` and 15
do not (measured; the engine does not require ordering).

### `POIN` point record (`0x00482640..0x0048277b`)

The record is **variable length**, selected by a leading `u16` flag word. The "38
bytes with prefix `00 1b`" of the feasibility study is the `0x1b` case.

| flag bit | file | in memory (`0x18`-byte point) |
|---|---|---|
| — | `u16` flags | not stored |
| `0x01` | 3 × `i32` position | `+0x00/+0x02/+0x04` int16 = value `>> 2` |
| `0x02` | 2 × `i32` UV, 16.16 | `+0x0c/+0x10` int32 |
| `0x04` (only with `0x02`) | 2 × `i32` second UV, 16.16 | LOD `+0x0c[i]` |
| `0x08` | 3 × `i32` normal, 16.16 | `+0x06/+0x08/+0x0a` int16 = value `>> 2` (absent → zero; `0x00481310` then derives a face normal) |
| `0x10` | `u32` | `+0x14` (read by the normal builder `0x00481310`; values `1/2/4/8` in samples, smoothing-group-like) |

Sizes: `0x1b` = 2+12+8+12+4 = **38 bytes**, `0x1f` = **46 bytes**. Histogram over all
parsed members: `0x1b` 67 391 381 points, `0x1f` 14 424 994, no other value (measured).
In the two bodies checked (`torus_backgroundtraffic_dots`, `xtc_teladi_eqd_ring1b`)
every LOD's max `|position|` is 65 536 (63 907 for ring1b LOD 3), i.e. 16.16 of 1.0,
while their LOD-0 values are 123 924 and 570 692 — so positions look **normalised**
and the LOD-0 value scales them (inferred; the consumer of LOD `+0x14` was not traced).
Normals are unit vectors in 16.16 (e.g. `(-32768, 0, 56755)`), UVs are 16.16 (measured
samples).

### `PART` layout (`0x004828c1..0x00482d97`)

`i32 nParts` → LOD `+0x04`; per part a **`0x70`-byte part** (allocation `0x00482935`):

| file field | code | stored |
|---|---|---|
| `u32` part flags | `0x00482989` | part `+0x60` (then `\|= 0x20`) |
| `u16` group count | `0x00482994` | part `+0x00`; `+0x04` = group array, **`0x20` stride** |
| per group: `i32` material index | `0x004829dd` | group `+0x08` (negative → part `+0x60 \|= 0x40000`) |
| per group: `i32` face count, then faces `4 × i32` (`a, b, c, word`) | `0x004829f2..0x00482aa4` | group `+0x00` count, `+0x04` faces (`0x10` stride, word stored int16 at `+0x0c`); `0x00481310` runs per face vertex |
| per group, **only if part flags `& 0x10000000`**: `i32` n, then n × `7 × i32` | `0x00482beb..0x00482ca9` | group `+0x18` n, `+0x1c` array (`0x10` stride: `i32` point index + 6 int16 `>> 2`) |
| per part, **only if `& 0x10000000`**: `10 × i32` | `0x00482d00..0x00482d5a` | part `+0x10/+0x14/+0x18`, `+0x30`, `+0x40/+0x44/+0x48`, `+0x50/+0x54/+0x58` (pivot, L∞ radius, AABB centre ×4, AABB half-extent ×4 per [render-node-bounds.md](render-node-bounds.md)) |
| otherwise | `0x00482d66..0x00482d77` | computed by `0x00481140`, `0x0047f3f0`, `0x00481010` |

The 7-int group records hold a point index followed by two 16.16 unit vectors (e.g.
`[3582, 33225, -56475, 1238, -51693, -39443, 8183]`); they match the tangent/binormal
data that `0x004bbb10` writes into vertex `+0x28..+0x3c` when the subgroup's `+0x18`
count is non-zero ([mesh-buffer-rewrite.md](mesh-buffer-rewrite.md)). Their count is
per group, not per face (122 records for 120 faces). Tangent/binormal semantics are
inferred from the values and that consumer; the ordering of the two vectors is unverified.

Part flags over all parsed LOD records: `0x30000001` 3327, `0x10000001` 981,
`0x30008001` 46, `0x00000001` 21, `0x10008001` 20, `0x30000201` 10, `0x30006001` 2,
`0x10000201` 2 (measured). No shipped body carries `BONE` or `WEIG` (0 of 4227 LOD
records), so those two layouts are from code only.

## 4. From file to draws: how LOD subsets are populated

1. `0x00481aa0` fills, per LOD record, `+0x04` parts × part `+0x00` groups, each group
   with its own material index and face list (§3).
2. `0x004bd830(model)` builds each LOD once (only when LOD `+0x3c == 0`). For every part
   it creates a descriptor at part `+0x64` whose subset count (`+0x08`) **equals the
   part's group count**, and `0x004bb470` builds one `0x1a8`-byte subset per group, each
   with its own `D3DXCreateMesh` VB/IB ([render-node-bounds.md](render-node-bounds.md),
   [mesh-buffer-rewrite.md](mesh-buffer-rewrite.md)).
3. Submission (`0x004c0150`) iterates the descriptor's subsets: one draw per subset.

So **draws for a node at LOD `i` = Σ over the parts of LOD `i` of their `PART` group
counts**, and the file controls it directly. Worked example,
`argon_L_solarpowerplant.pbb` (1 part per LOD, measured): groups 25 / 18 / 11 / 3 / 1,
thresholds — / 250 / 150 / 80 / 30, points 224 132 / 122 190 / 57 127 / 9 029 / 14 094,
faces 152 900 / 77 128 / 32 972 / 4 438 / 7 388; the coarsest LOD is one group with
material 1 of 49. Over all 950 multi-LOD bodies, the coarsest LOD has fewer groups
than LOD 0 in 511, the same number in 438 and more in 1 (measured) — the "coarse level
that never collapses its material set" of the feasibility study is common.

A post-load adjustment in `0x004863c0` (after `0x00481aa0` returns): if the model has
no per-body materials (`+0x50 & 0x10` clear, i.e. `MAT3`), more than 3 LODs and more
than one material, every group of the coarsest LOD gets material index `0x485`. It does
not apply to `MAT5`/`MAT6` bodies.

Other per-LOD side effects already documented: `0047d4d7..0047d51e` clears a node's
renderable bit at its coarsest LOD when node `+0x12c & 0x8000`
([merged-lod-feasibility.md](../architecture/merged-lod-feasibility.md) §1).

## 5. Parser acceptance

`verification/results/bob1-format/bob1_roundtrip.py` (read-only; uses
`tools/analysis/sector_fog_census.Assets` with the game's overlay precedence) parses
every `.pbb` logical resource and requires, per `BOB1` member:

- the parse consumes the buffer exactly to the final `/BOB` (no trailing bytes), and
- `serialise(parse(data)) == data`, byte for byte.

Result (`bob1_roundtrip_out.txt`, measured): 1688 `.pbb` resources, 53 are `CUT1`
(skipped), **1634 of 1635 `BOB1` members round-trip byte-identical**. The failure,
`01.cat:objects/others/Bothers_khaak_hive_stations.pbb` (the only candidate for that
stem), is a complete gzip stream whose 4413-byte payload ends right after `BODY 00 01`
with no `POIN`, `/BOD` or `/BOB`: a truncated shipped asset, not a layout variant
(measured). The scalar readers do not bounds-check, but the next tag read in `0x004e9b70`
compares the cursor with the buffer bounds and sets the error flag, so `0x00481aa0`
would abort and return no model for it (inferred from the decompiled check, not run).
LOD-count histogram `{1: 684, 2: 74, 3: 229, 4: 532, 5: 113, 6: 1, 9: 1}`.

A production reader/writer for the pilot must meet the same acceptance over the whole
installed set (not a sample), keep every opaque field (material words, point `u32`,
face word, the 7-int group records, the 10 part ints) as read, and add the negative
checks the engine applies: unknown tag, wrong closer, `WEIG` count ≠ point count.

## 6. Body id → name (for logging `body=<name>` per model)

Follow-up, 2026-09-23 (same EXE, same Ghidra project; helpers `X3XrefsTo.java`,
`X3ListRange.java`, `X3GrepInsns.java` under `tools/analysis/`).

**Result.** The model does not carry its scene name. The name lives in the engine's
**body table**, indexed by the body id; the id is model **`+0x08`** (stored at
`0x00481c7a` from `0x00481aa0`'s first argument) and node **`+0x140`** (what the
cull census and `object_context` already log as `model=`). Model `+0x4c` is the
`BOB1` `NAME` tag (`0x00481e62`), and no installed body carries `NAME` (§1 section
census), so it is null in practice. The registry hash entry holds only
`{next, key = id + 1, model}`.

| address | role |
|---|---|
| `0x00608518` | image global → body manager `g` |
| `g+0x04 / +0x08` | loaded-model list (model `+0x00` next, `+0x04` prev; linked at `/BOB`) |
| `g+0x14` | model hash `{+0 buckets, +4 bucket count (power of 2), +0xc entries}`; lookup `0x004863c0`, insert `0x004efbf0` |
| `g+0xb4` | fixed slot count, **11000** (`0x0046d910`) |
| `g+0xb8` | dynamic slot count (incremented at `0x0046e51b`) |
| `g+0xbc` | slot array, **`0x1c`** per slot |
| `g+0xc0 / +0xc4 / +0xc8` | the same scheme for `cut\%05d` scenes (ids ≥ 50000; `0x0046e260`, 8-byte slots) |

**Id → slot** (repeated verbatim in `0x0046df60`, `0x0046e040`, `0x0046e0a0`,
`0x0046e100`, `0x0046ed00..0x0046edc0`): `id < 1000` → `id`; `1000 ≤ id < 20000` →
`id − 9000` (so 1000…8999 are invalid); `id ≥ 20000` → `g+0xb4 + id − 20000`; valid
when `0 ≤ slot < g+0xb4 + g+0xb8`.

**Slot** (`0x1c` bytes): `+0x00`, `+0x04` (init −2) and `+0x08` (init −1) are the
`BodyData` columns; **`+0x0c` = `char*` name** (NUL-terminated heap copy) or 0;
`+0x10` load-failed flag (set by `0x0046e100` when `0x004863c0` fails, tested by
`0x0046e0a0` before a load); `+0x14` flag copied into model `+0x50 |= 0x40`
(`0x0046e040`); `+0x18` zeroed.

**How ids are assigned.** Fixed slots (ids 0…999 and 9000…19999) come from
`types\BodyData` (`0x0046e860`: per row id, two `u32`, one `i32`, name). Every other
name goes through **`0x0046e400`** (get-or-register, 46 call sites incl. the scene
loaders `0x00490ec0`, `0x00491b10`, `0x00434e40`): a string starting with a digit or
`-` is `sscanf("%d")` and used as the id; otherwise `_stricmp` against every slot's
name, fixed then dynamic, returns the existing id; a miss **appends a dynamic slot**,
id = `20000 + (g+0xb8 − 1)`, copies the name into `+0x0c` (`0x0046deb0`) and grows the
array by 1000 slots (`0x004b8920`, a `realloc`) on every 1000th registration. So the
captured ids 20463…22161 are dynamic slots: **20000 + registration order**. The
compare folds case but not `/` vs `\`, so a name spelled both ways gets two ids
(3 such pairs in each save, measured).

**Name → file.** `0x0046df60` yields the slot name, or `v\%05d` when `+0x0c` is null;
`0x004863c0` formats it with `objects\%s` (path table `0x0057c008 + 0x1c` →
`0x0054d520`), so the request is `objects\<name>` without extension (§7 picks the
extension).

**Lifetime.** The name is written once at registration and is never freed or
rewritten during play. It is bulk-freed by `0x0046dc20` (first call of the savegame
loader `0x0046ee20`, which is reached from `0x00475b10`, `0x00476140`, `0x0047a720`)
and by the teardown `0x0046d9b0` (from `0x004710f0`). The **slot array moves**:
`0x0046e400` (every 1000th registration), `0x0046dc20` and `0x0046ee20` reallocate it.
`0x0046f1c0` writes the dynamic names into the savegame (`u32` count, then per slot
`u8 length` + bytes) and `0x0046ee20` restores them into slots `g+0xb4 + i`, so **ids
are stable within a save lineage** and re-bound when a game is loaded.

**Read recipe** (Present time, `engine_memory::read`, 32-bit little-endian):

```
g    = u32 [0x00608518]                      ; 0 → body system not up
nfix = i32 [g+0xb4]  ndyn = i32 [g+0xb8]  tab = u32 [g+0xbc]
require nfix == 11000, 0 <= ndyn < 1000000, tab != 0
slot = id < 1000 ? id : id < 20000 ? id - 9000 : nfix + id - 20000
require 0 <= slot < nfix + ndyn               ; rejects 1000..8999
p    = u32 [tab + slot*0x1c + 0x0c]
p == 0 → name = "v\%05d" % id
else   → read ≤ 256 bytes at p up to NUL (names ≤ 255 by the save format); no NUL → reject
file   = "objects\" + name                   ; extension per §7
```

Cache the string by id and drop the cache when `tab` changes or `ndyn` decreases
(game load / new game re-binds ids). Cost is 5 reads for a new id, none for a cached
one. No hook is involved: it is a pure data read, so instruction boundaries, flags and
reentrancy do not apply; the only hazard is a read racing `0x0046dc20`/`0x0046ee20`
(free then refill during a load), which `engine_memory::read` turns into a failed or
stale read, not a fault, except in the decommit window documented in
`engine_memory.h`. Which thread runs `0x0046e400` relative to Present was not traced.

**Verification against live data** (`verification/results/bob1-format/body_id_names.py`,
output `body_id_names_out.txt`, measured). No dump of the live table exists, but the
savegames carry it. The three saves in the X3 bottle hold 2174 / 2179 / 2180 dynamic
names (ids up to 22179, covering the captured range), and the first 2174 are
identical in all three; `stations\docks\argon_dock_center` is id **21411** in all three.
Mapping the model ids of the measured draw joins in [lod-selection.md](lod-selection.md)
through the table, resolving with §7 and parsing the body gives **exact
(draws, primitives) equality on 8 of 8 LOD rows of 4 models**: `53ab` (21419) →
`stations\living_sections\argon_livingsection` (LOD 0 17 / 16 011, LOD 3 1 / 467,
run240), `5411` → `stations\station_scenes\others\argon_spacedock` (35 / 107 397,
20 / 54 709), `546d` → `…\argon_L_solarpowerplant` (25 / 152 900, 18 / 77 128),
`5530` → `ships\owp\owp_large` (20 / 90 006, 19 / 44 395). The six older station
rows (`542a`, `5427`, `5436`, `543f`, `542b`, `546b`, run36–run49) match **no**
installed `.pbb` by whole-LOD sums (1688 stems searched), so they cannot confirm or
refute the mapping; what those rows counted was not re-examined.

## 7. Which file wins: extension and archive precedence

**Rule** (for the `objects\<name>` request of §6):

1. **A loose file wins over every catalogue.** Among loose files in that directory,
   the lowest index in `"pbb bob pbd bod"` wins, and `<name>-L<lang>.<ext>` beats
   `<name>.<ext>`.
2. Otherwise the **catalogue slots are searched from the highest slot down**, and the
   search **stops at the first catalogue holding the stem under any accepted
   extension**. Slot order, highest first: `addon\mods\<mod>.cat` (only when a mod is
   selected) → `addon\NN.cat` from high NN to 01 → `NN.cat` from high NN to 01.
3. **Extension order applies only inside the winning layer.** A `.bod`/`.pbd` in a
   higher catalogue beats a `.pbb` in a lower one; within one catalogue `.pbb` >
   `.bob` > `.pbd` > `.bod`, with the `-L<lang>` variant preferred.

The extension does not select the parser: `0x004e8880` inflates gzip (plain, or
single-byte XOR-keyed with key `first byte ^ 0xC8`) and returns raw bytes otherwise,
and `0x004863c0` sends a payload starting with `BOB` to `0x00481aa0`, anything else to
the text parser `0x00483f20`.

| address | what it establishes |
|---|---|
| `0x004e9840` | stream open; pushes **0** as the "user directory" flag to `0x004e8e10` (so loose paths are relative to the working directory, i.e. the game folder, and catalogues are allowed) |
| `0x004e8e10` → `0x004e8780` | resolve (`0x004e7590`, `thiscall`, `ECX` = extension list, args file object, path, flag); then a loose hit → `fopen`, a catalogue hit → `0x004e6fa0` (exact-name search, slots top-down, sets `G+0xc6`) and `fseek` into the `.dat` |
| `0x004e7590` | splits the extension list at spaces (≤ 10); builds `-L%03d` from `G+0x76c` (`0x004e78ba`); **loose phase** `0x004d2950` = `_findfirst("<dir>\*.*")`, each name ranked by `0x004e7470`; if found, `0x004e7ce1` jumps to the exit **without the catalogue phase**; **catalogue phase** `0x004e7cec..0x004e7d47`: requires `G+0xc0` and `G+0xbc & 1`, `i = word G+0xc8 − 1` down to 0, skips empty slots, loop condition includes "nothing found yet" |
| `0x004e7470` | rank = lowest `i` with `_stricmp(found, base + "." + ext[i]) == 0` (separator `0x005559b0` `"."`), else `0x7fffffff` |
| `0x004e6ee0` | `bsearch` comparator: **prefix** match (key exhausted → equal); the resolver then walks both neighbours while `strncmp` on the key length matches, ranking each |
| `0x004ec960`, `0x004ed750` | key and every catalogue entry are upper-cased with `/` → `\`; entries `{offset, size, name}` (`0xc`) sorted by `0x004ec9a0` |
| `0x004ec9e0` (`0x004ed3d2..0x004ed6cf`) | mounting: counts `%02d.cat` from 01 **until the first missing number**, then `addon\%02d.cat` the same way; slots `0..nb−1` = base, `nb..nb+na−1` = addon, one extra slot; `G+0xc8 = nb + na + 1` |
| `0x004ede00` | loads `addon\mods\%s.cat` (path table `+0x40`) into slot `G+0xc8 − 1` when `G+0x79c` (mod name) is set |

`G` is `*(0x00606f34)`; the path table is `0x0057c008` (`+0x34` `%02d.cat`,
`+0x38` `addon\%02d.cat`, `+0x40` `addon\mods\%s.cat`, `+0x1c` `objects\%s`).

**Installed tree** (`body_id_names_out.txt`, measured): search order after loose
files is `addon/04 … addon/01, 13 … 01`; no `addon\mods` folder and no loose
`objects` folder (in the game folder or under `addon`). Catalogue body members are
1726 `.pbb` and 1958 `.pbd`. No `addon\` catalogue
contains an `addon/objects/…` member (their `addon/` members are `director`, `types`,
`t`, `maps`, `cutscenes`), so bodies share one `objects\` namespace across all 17
catalogues. Of 3398 body stems, 245 occur in more than one layer or extension; 53 mix
`.pbb` and `.pbd` (no `.bob`/`.bod` members at all), 16 of them inside one catalogue.
One stem exercises rule 3: `objects\effects\engines\fx_engine_boron_m3` is `.pbd` in
`addon/01.cat` and `.pbb` in `01.cat`, and the code picks the `addon/01` `.pbd`
(inferred from the code; not observed in game).

**For the overlay builder.** Write the merged body as `objects\<name>.pbb` into a new
**`addon\05.cat`/`05.dat`**: numbering must stay contiguous (the mount loop stops at
the first gap), 05 is the next free number, and it becomes the highest non-mod slot, so
it overrides the shipped body whatever extension that uses. Do not also ship
`<name>.bod`/`-Lnnn` variants in the same or a higher layer. A loose
`objects\…\<name>.pbb` in the game folder overrides every catalogue (a development
shortcut), and a selected `addon\mods` catalogue would override `addon\05`.
`sector_fog_census.Assets` agrees on the layer order (loose > later catalogue) but
does not apply the in-layer extension rank (`logical()` rejects mixed formats).

## 8. Text form (`.bod` / `.pbd`)

2026-09-23. `bob1.parse_text` (`parse()` dispatches on the `BOB` magic like `0x004863c0`)
compiles a text body into the tree `parse_binary` returns, so `serialise` writes a BOB1 body.
**It follows the engine's own text loader `0x00483f20`** ([body-text-loader.md](body-text-loader.md)),
not the dbox2/x2bc compiler: the overlay replaces every record of a text winner with this
compile, and it must load into the model the game already draws for that body. Oracle: the
reference port `verification/results/bob1-format/text_loader_reference.py`. Sources: the
vanilla+mod root (Mayhem 3 `/tmp/x3-mod1` addon 05–11 over the bottle, `/tmp/x3-mod2` addon 12)
and the bottle. Evidence and scripts: `verification/results/lod-overlay-batch/text-bodies/`.

**Lexing** (measured). Values end at `;`; `/` starts a comment to the end of the line
(`0x004e98a0`), so every `/! … !/` block is ignored: `N:` (460,523 in the root),
`COLLISION_BOX:` (160) and `PART_VALUES_RAW:` (65) never reach the model; no block spans lines
and none has data after `!/` (body-text-loader.md §2). `parse_text` refuses a `/!` without `!/`
on its line (the engine would read the following lines as data; none ships). Integers take the
engine's forms `-`, `%binary`, `0x` hex, decimal (`-?[0-9]+`-strict otherwise: no `+`, `_`);
16.16 values are decimals without exponent, `trunc(v × 65536)`; a value outside its field
(32-bit ints and flag strings, 16-bit words, 16.16) is a `FormatError` (`text_parse_error` in
the census). The first `/#` comment becomes the binary `INFO` string (discarded by the loader).
A text **scene** (`VER:` or `P n;` lines, the text twin of `CUT1`) is not a body: 1,536 of the
root's 3,219 winning text members, 898 of the bottle's 1,763. 1,505 of the root's 1,683 bodies
are CRLF.

```
body     := material* record+
material := MATERIAL5: idx; texid; rgb×3; transparency; selfillum; shininess; strength;
                       blend; twosided; wire; texvalue; (mapid; value)×3
          | MATERIAL6: idx; flags; texture|NULL; rgb×3; transparency; selfillum; shininess;
                       strength; blend; twosided; wire; texvalue; (name|NULL; value)×5
          | MATERIAL6: idx; flags (0x2000000); technique; effect; n; (name; SPTYPE_x; value…)×n
record   := value; (x; y; z;)* -1; -1; -1; part* -99; lodflags;
part     := face+ -99; partflags;
face     := mat; v0; v1; v2; [v3;] -flags; [smooth if flags & 0x10] [(u; v)×k if flags & 8]
            [(u2; v2)×k if flags & 0x80]
```

**Mapping to the model** (engine rule of body-text-loader.md, reproduced exactly on the bottle):

| text | model written |
|---|---|
| vertex integers | per record `m = max |x|,|y|,|z|`, position `trunc((v << 16) / m)` (so every record reaches 65536; an all-zero record is refused like the engine's load failure) |
| uv, `SPTYPE_FLOAT*` | `trunc(v × 65536)` |
| faces | up to 4 vertices, fan `(v0, vi-1, vi)`; face word `(-flags) & ~1` (24 for -25, 8 for -9); groups per part in order of first material; 5+ vertices refused (engine error) |
| points | smoothing 0: every corner a new point; smoothing `s`: the first earlier point of the record with the same `s`, the same normalised position and `|Δu|,|Δv|,|Δu2|,|Δv2| < 2`; point flags `0x1b` (`0x1f` with a second UV set); `u32` = `s` |
| normals | engine fixed-point face normals (`0x00469c20`), corner-angle weighted for smoothed points (asin table `0x004f0110`), every point renormalised at the record end; written as the 16.16 value the binary loader shifts to the same int16 |
| part flags | the digit string only: no `0x10000000`, no bounds, no tangent records (the engine's text load builds none; bump materials draw with a zero tangent basis in vanilla, body-text-loader.md §6) |
| LOD flags | the digit string, `| 1` when a part has a negative material, `| 0x20` with a second UV set (as the engine sets them) |
| MATERIAL5 / classic MATERIAL6 | colours `rgb×3`, then `+0x16` = `0xffff` (the engine's `-1`), transparency `+0x18`, self-illumination `+0x1a`; `blend/twosided/wire` → `0x2/0x10/0x8` OR-ed into the MAT5 flag word or the MAT6 flags; maps as written, `NULL` → empty |
| effect MATERIAL6 | parameters as written (no compiler transforms) |

Refused as `text_parse_error`: `MATERIAL3` (the engine maps each record to the nearest global
material `0x004f71f0`, which needs the running game; the census reports these as `mat3`, 151 in
the root, 143 in the bottle), `MATERIAL:` (MAT1, 6; the binary format has no MAT1), a
MATERIAL6 in the MATERIAL3 layout (4 `extern_*_dock` props), mixed MATERIALn kinds, part flag 4
(a pivot vertex BOB1 cannot carry; none ships), `WEIGHTS:` (none ships), a comment-only file
(`argon_gate.pbd` of a no-ad-sign mod). Coverage (`text_coverage_*_out.txt`, measured): root
1,521 of 1,683 bodies compile, bottle 712 of 865; the X2 quad faces that the compiler reading
refused now compile.

**Oracle** (`engine_oracle.py`, measured, and `test_bob1.Installed.test_text_bodies_match_engine_reference`):
every bottle text body `parse_text` accepts, 712 bodies / 758 records, equals the reference port:
470,816 points with 0 position, uv or smoothing differences and every int16 normal identical
(the test allows 4 units), 274,019 triangles with identical indices and face words, 0 group-material
or part-flag differences.

**Residuals against the engine's in-memory model** (inferred from the loader note; not
measurable offline):
- Classic MATERIAL6 flags: the engine overwrites `+0x28` with the resolved texture's table
  flags and then ORs the switch bits; the binary parser overwrites too but has no switches, so
  a written `flags | switches` loses the switch bits whenever the texture resolves (the texture
  table is not available offline). Effect materials are unaffected.
- Part centre and radius: the engine's text load takes the integer mean of the face corners; a
  binary part without `0x10000000` gets them from `0x00481140` (not compared). AABB and LOD
  extents come from `0x0047f3f0` / `0x0047f350` in both paths.
- A point whose engine normal is zero (degenerate faces) is re-derived by the binary parser's
  `0x00481310`.
- 16.16 values are parsed with Python `float` like the reference; the engine accumulates the
  decimal digits in a double (`0x004ec490`), which can differ in the last bit before truncation.
- LOD 0 `+0x34` is `10000` after a text load and `100000` after a binary one; the ladder never
  reads it.

**Compiled twins** (`text_pairs.py`, measured). 53 vanilla stems ship both a text and a binary
member (6 scenes). The binaries are dbox2 1.13 / x2bc compiles, not the engine's text load: with
the engine rule, 0 of the 65 (text, binary) combinations are ordered-equal, 1 is the same
geometry after scaling (`v/11994`), 59 differ (merged flat corners, `N:` normals, rounding,
face word 1, compiler material transforms) and 5 are MATERIAL3 (`v/10678`, `11012`–`11015`).
Under the compiler reading (the previous `parse_text`, commit `46b14ec3`) 21 were equal, 21 the
same geometry, 2 scaled and reordered (`argon_owpmain`, `argon_owpsec` against `addon/01.cat`)
and 21 other revisions; that is how dbox2/x2bc compile, not what the game shows for a text
winner. `material_flags_out.txt`: 208 classic MATERIAL6 records with a named texture in the
bottle, `flags` = `0x2·blend | 0x10·twosided` in 203 and an extra `0x1` in 5; the only binary
MAT5, `01.cat:v/00586.pbb`, has flag word `0x12` and no text twin. `record_extent_out.txt`:
1,255 of the 4,201 installed binary records are not at max |position| 65535–65536, so the binary
loader does not require the normalisation the text load applies.

The census and the batch no longer refuse text bodies for bump maps, smoothed faces without
`N:` or `COLLISION_BOX` (the refusals of `46b14ec3`): the compile carries exactly what the
engine keeps. `--binary-only` (census and batch) still leaves text bodies out.

## Unknown

- **Text parser `0x00483f20`**: resolved in [body-text-loader.md](body-text-loader.md) and
  followed by `bob1.parse_text` (§8); open there: the shader result of a zero tangent basis.
  Open for the compile: the classic-MAT6 switch bits after a texture overwrite and the part
  centre rule of a binary part without `0x10000000` (§8 residuals).
- **What a coarse LOD must contain to be accepted and drawn correctly** beyond the
  grammar: whether a written part may drop `0x10000000` (letting `0x00481140`/
  `0x0047f3f0`/`0x00481010` compute the 10 bounds ints) — the 21 `0x00000001` parts say
  the loader accepts it, but not that the result is identical, and in a model with a bump
  texture such a part gets zero tangents (the generator `0x004bbb10` needs group records,
  `0x004bb97c`; [body-text-loader.md](body-text-loader.md) §6); how the per-group
  tangent records must be generated for a decimated mesh (the two vectors' order and
  handedness are inferred); and what the point `u32` (flag `0x10`) and the face word
  (always `1` in the samples) control in `0x00481310`.
- **LOD `+0x14`** (LOD 0's value, copied to every LOD): the normalised-position reading
  is inferred from sample ranges; its consumer was not traced. A coarse LOD appended to
  a body inherits it automatically, so this matters only for a new body.
- **Material index 0 vs record `u16`**: groups index the material array by position;
  whether the `u16` field is ever used for lookup was not traced.
- **`.bod` override order**: resolved in §7 (loose > highest catalogue > extension
  rank inside that layer).
- **Body table threading and model flush**: which thread runs `0x0046e400` relative to
  Present, and whether models cached under an id are dropped when a game load re-binds
  ids (`0x004802b0`'s callers), were not traced. Where `G+0x79c` (the mod name) is set
  was not traced.
- `MAT1`/`MAT2`, `MAT3`, `BONE`, `WEIG` and effect types 3/4/6/7: decoded from code
  only; no installed member exercises them. The LOD flag `0x40` (half the bodies) is
  unexplained.
- The writer `0x00482fb0` (`-convertbodies`): resolved in
  [body-text-loader.md](body-text-loader.md) §9; it writes no effect-material data, tangent
  records or bounds ints, so it cannot produce reference `.bob` output for effect bodies.

## Reproduce

```sh
# Ghidra (raw output stays local)
JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home \
  /opt/homebrew/Cellar/ghidra/12.1.3/libexec/support/analyzeHeadless \
  /tmp/x3-ghidra-research X3Render \
  -import "$HOME/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/X3AP.exe" \
  -analysisTimeoutPerFile 900
# then -process X3AP.exe -noanalysis -readOnly -postScript X3DecompileFunctions.java <out> \
#   00481aa0 004863c0 00470490 004e9b70 004ea610 004ea6b0 004eafc0 004eaf50 004ea1b0

# Round-trip over every installed .pbb (about 8 min, prints counts only)
PYTHONPATH=tools/analysis python3 verification/results/bob1-format/bob1_roundtrip.py \
  > verification/results/bob1-format/bob1_roundtrip_out.txt
# Material-form / LOD-0 flag census (about 1 min)
python3 verification/results/bob1-format/material_forms.py \
  > verification/results/bob1-format/material_forms_out.txt
# §6/§7: id -> name from the savegames, resolver order, draw-join check (about 25 s)
PYTHONPATH=tools/analysis python3 verification/results/bob1-format/body_id_names.py \
  > verification/results/bob1-format/body_id_names_out.txt
# §6/§7 Ghidra: -postScript X3DecompileFunctions.java <out> 0046e400 0046deb0 0046df60 \
#   0046dc20 0046ee20 0046f1c0 004e7590 004e7470 004e8780 004ec9e0 004ed750 004ede00
# plus X3ListRange.java <out> 004e7cc0:004e7d90 004e6ea0:004e6f1f and
# X3XrefsTo.java <out> 0046e400 004ed750 004ede00
```
