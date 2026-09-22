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
| `0x00483f20` | Non-`BOB` (text `.bod`) parser, same model structure (inferred from the magic branch; not decompiled). When `*(0x606f34)+0x108 & 0x800` the text-loaded model is immediately re-written as binary by **`0x00482fb0`** (`"%05d.bob"`/`"%s.bob"`), i.e. the EXE contains a BOB1 *writer* using the same tag table. Not studied further. |
| `0x00482fb0` | BOB1 writer (`0x004e9f70(stream, tagIndex, open/close)` emits tag/closer, `0x004eb3f0` emits `u16`). Only its material part was read. |
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
`i = nLOD-1 … 1` and takes the first `i` with `s < T_i·f`
([lod-selection.md](lod-selection.md)). A body with `nLOD = 1` never enters the loop.
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

## Unknown

- **What a coarse LOD must contain to be accepted and drawn correctly** beyond the
  grammar: whether a written part may drop `0x10000000` (letting `0x00481140`/
  `0x0047f3f0`/`0x00481010` compute the 10 bounds ints) — the 21 `0x00000001` parts say
  the loader accepts it, but not that the result is identical; how the per-group
  tangent records must be generated for a decimated mesh (the two vectors' order and
  handedness are inferred); and what the point `u32` (flag `0x10`) and the face word
  (always `1` in the samples) control in `0x00481310`.
- **LOD `+0x14`** (LOD 0's value, copied to every LOD): the normalised-position reading
  is inferred from sample ranges; its consumer was not traced. A coarse LOD appended to
  a body inherits it automatically, so this matters only for a new body.
- **Material index 0 vs record `u16`**: groups index the material array by position;
  whether the `u16` field is ever used for lookup was not traced.
- **`.bod` override order**: `0x004863c0` tries `"pbb bob pbd bod"`, but the resolver
  (`0x004e8e10` → `0x004e8780`/`0x004e8880`) was not followed, so whether a loose text
  `.bod` or a later-layer `.pbb` wins for the same stem is still not established from
  code.
- `MAT1`/`MAT2`, `MAT3`, `BONE`, `WEIG` and effect types 3/4/6/7: decoded from code
  only; no installed member exercises them. The LOD flag `0x40` (half the bodies) is
  unexplained.
- The writer `0x00482fb0` and the flag `*(0x606f34)+0x108 & 0x800` that triggers it were
  not studied; whether it could produce reference `.bob` output is open.

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
```
