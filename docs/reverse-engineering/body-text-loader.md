# Text body loader `0x00483f20` (`.pbd` / `.bod`): what the engine builds from a text body

Read-only study, 2026-09-23, for the merged-LOD asset pilot and `tools/analysis/bob1.py`
([body-format-bob1.md](body-format-bob1.md) §8 is the shipped-file reading this note checks).
Installed `X3AP.exe` SHA-256 `fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`,
preferred VAs, base `0x00400000`. Ghidra 12.1.3 headless on `/tmp/x3-ghidra-research/X3Render`
(`-readOnly -noanalysis`, `X3DecompileFunctions.java`, `X3XrefsTo.java`) plus capstone windows
of the same EXE for every instruction-level claim. Raw decompiler output stays in the session
scratchpad, untracked. No game launch, no Wine command. Ghidra drops the group-copy block
`0x004857b7..0x0048589d` of `0x00483f20` as "unreachable"; that range was read as disassembly.

**Result.**

1. **Tangents.** The text loader never builds tangent records: it zeroes every group's record
   count and pointer (`0x004858ad`, `0x004858b0`) and never sets `0x10000000` itself (the part
   flags are exactly the text's digit string plus `0x20`/`0x40000`). A model with a bump-textured
   material still gets the tangent vertex declaration and the `BUMPMAP` / `BUMPMAP_LOW`
   technique, but its `TANGENT0`/`BINORMAL0` are **zero**: `0x004bc1c0` writes zeros, and the
   tangent generator `0x004bbb10` is gated on the group's record count (`0x004bb97c`), so it
   never runs for a text-loaded part. `DEFAULT` is used only through the node flag
   `0x100000` ([lod-child-hide.md](lod-child-hide.md) §4) or when the material has no bump texture.
2. **Normals.** `N:` blocks are **ignored**: `/` starts a comment to the end of the line in the
   lexer (`0x004e98a0`, `0x004eb930`, `0x004ec490`). Every normal comes from the geometry:
   smoothing group 0 → the fixed-point face normal on three new points; group `s ≠ 0` → an
   **angle-weighted** (not area-weighted) sum of unit face normals over the faces whose corners
   merge into the same point, renormalised in 16.16 by `0x00469c20`. The exact rule is §5.
3. **Position scale.** Each record is **normalised** so its largest `|coordinate|` becomes
   65536: `v' = trunc((v << 16) / m)`, `m` = the record's max of `|x|,|y|,|z|`
   (`0x00484fc0..0x0048505d`). There is no fixed `100000/65536` scale.
4. **`COLLISION_BOX`** is inside a `/` comment, so the engine reads nothing from it; no model,
   LOD or part field receives it. Part bounds are computed from the vertices (§6).
5. **Materials.** None of the compiler's transforms happen at load: effect parameters are read
   verbatim by `0x00470490`, `MATERIAL3` stays a global-material body (no `MAT6`), and a classic
   `MATERIAL6` keeps (and ORs in) its `blend/two-sided/wire` bits. The `-convertbodies` writer
   `0x00482fb0` does not produce them either: it writes `vc\<name>.bob` without effect data,
   tangent records or bounds (§9).

## 1. Call graph

| address | role |
|---|---|
| `0x004863c0` | body get-or-load: payload not starting with `BOB` → `0x00483f20` (`0x004867e0`); then, when `*(0x00606f34)+0x108 & 0x800`, `0x00482fb0` (`0x00486801`) |
| `0x00486310` | second caller (`0x0048638f`, `0x004863a2`): wraps an in-memory buffer in a text stream (`+0x1c = 0`, table `0x0054ed50`) and calls `0x00483f20` |
| `0x00483f20` | text parser: header tags, materials, records (§2–§8); returns the `0x70`-byte model |
| `0x00480830` | per face of a part: group lookup, corner → point dedup, fan triangulation, face normal and smoothing accumulation (§5) |
| `0x00469c20` | fixed-point normalise of a 3-vector (`EDI` = middle component); callers `0x00480d13`, `0x00485c42` (record end), `0x0048150d` (binary `0x00481310`) |
| `0x00469a50` | 16.16 square root (bit-by-bit, §5) |
| `0x004f0110` | builds the asin table `0x00596990` (BSS) used for the corner angle |
| `0x0047f3f0`, `0x0047f350` | part AABB and LOD extents at the end of the load (`0x00486176`, `0x0048619b`) |
| `0x00470490`, `0x00481780` | effect material read and post-scan, shared with the binary parser |
| `0x004f71f0` | `MATERIAL3`: nearest global material (`0x00608db0`, stride `0x3c`) for a per-file record |
| `0x00482fb0` | `-convertbodies` BOB1 writer (§9); flag set at `0x004ed03c` from the command line, loop `0x004869e0` over loose `v\*.pbd` from `0x0048afa6` |

## 2. Lexer (text mode of the shared stream)

The stream is the one `0x004e9840` opens for every body with table `0x0054ed50`; `0x00483f20`
clears `+0x1c` (text mode). Tags are matched against the table's text labels (`NAME:`, `VER:`,
`SOUND:`, `MATERIAL:`…`MATERIAL6:`, `BODIES:`, `BONES:`, `POINTS:`, `WEIGHTS:`, `PARTS:`, `INFO:`);
no match returns 0 without consuming. Values:

| reader | text behaviour |
|---|---|
| `0x004e98a0` (skip) | skips `\t`, `\n`, `\r`, space; `/` skips to the end of the line |
| `0x004eb930` (`i32`, via `0x004ea610`) | optional `-`, `%` binary, `0x` hex, decimal; ends at `;` (consumed) or NUL; `/` inside a value skips to the end of the line; `#…;` yields 0 |
| `0x004ec490` (16.16, via `0x004eafc0`) | decimal digits accumulated in double (`v·10 + d`, fraction `scale·0.1`), sign, then `× 65536.0` (`0x005654c0`) and `_ftol` `0x0052b5d0` = **truncation** |
| `0x004ebe00(radix)` (via `0x004ea9d0`, `0x004ea660`) | unsigned; radix 2 for part and LOD flags (digit strings) |
| `0x004ea6b0` | `u16`, or a flag-name table lookup when one is passed |

Consequences: every `/! … !/` block is a comment when it sits on one line, and anything after
`!/` on that line is lost. In the X3 bottle's 865 winning text bodies there are 140,689 blocks,
**0** span lines and **0** have data after `!/` (measured, `text_loader_reference_out.txt`), so
no shipped body loses data this way.

## 3. Grammar as the engine reads it

```
[NAME: str;]  [SOUND: u16; flags; i32; i32; fx; fx; fx; u16; u16; i32; i32;]   -> model +0x4c / +0x18..+0x4a
material*                                     MATERIAL:/2:/3:/5:/6: (kind fixed by the first record), <= 100
record+ :=  value;  (x; y; z;)* -1; -1; -1;  [WEIGHTS: per vertex (bone; weight;)* -1;]
            part* -99; lodflags(binary digits);
part    :=  face+ -99; partflags(binary digits); [pivot vertex index; if partflags & 4]
face    :=  mat; v0; v1; v2; [v3;] -flags; [smooth; if flags&0x10] [(u; v;)×k if flags&8] [(u2; v2;)×k if flags&0x80]
```

A face has up to 4 vertex indices (more → error); smoothing and UVs are read only when it has at
least 3. The part list ends when a part would start with `-99`. Limits: 100 materials (static
array `0x0058d6b0`, `0x2580` bytes), 200 parts per record, fewer than 200,000 faces per part
(`0x004856ad`). The first record's value is LOD `+0x14` (scale, copied to every LOD as in
`BODY`); later values are the thresholds `+0x34`; LOD 0's `+0x34` is **10000** here
(`0x00484bf6`) against 100000 in the binary parser — never read by the ladder. LOD flags: the
digit string, `| 1` when a part has a negative material, `| 0x20` when a face carried a second
UV set (`0x00485b95`, `0x00485bad`), `| 4` with `WEIGHTS:` (`0x00485e31`).

## 4. Positions

After the `-1; -1; -1` terminator the loader tracks `max |x|`, `max |y|`, `max |z|` over the
record's vertices, takes `m` = their maximum, and rewrites every coordinate as
`(v << 16) / m` with a 64-bit signed `idiv` (truncation toward zero; `shrd/sar/idiv` at
`0x00484ff5`, `0x00485021`, `0x0048504e`). An all-zero record returns no model. The point array
stores `int16 = v' >> 2` (as in `POIN`). Each record is normalised on its own, so a coarse
record whose extent differs from record 0 is still scaled to 65536; the world size follows from
the LOD `+0x14` value alone (consumer of `+0x14` still untraced, [body-format-bob1.md](body-format-bob1.md)).
Of the bottle's 964 text records, 462 have `m = 100000` (measured); only those agree with a
fixed `100000 → 65536` scale, and then only up to truncation vs rounding.

## 5. Points and normals (exact rule)

Notation: positions are the normalised 16.16 values of §4; `fm(a,b) = low32((int64(a)·b + 0x8000) >> 16)`.

**Faces.** A face of `k` vertices becomes the fan `(v0, v(i-1), v(i))`, `i = 2..k-1`. Each
triangle is stored `{a, b, c, word}` with `word = (−flags) & ~1` (so `24` for `-25`, `8` for
`-9`; the binary compiles carry 1) in the group of its material; groups are created per part in
order of first material appearance (`0x00480830` prologue); a group is flagged (`+0x0c = 1`)
when its material's flags have any of `0x486`, and the part keeps `0x20` only if all do.

**Corners → points** (per record; the point list spans all parts of the record):
- smoothing value `s = 0`: every corner makes a **new** point (never shared, even with an
  identical corner).
- `s ≠ 0`: reuse the **first earlier point** with the same `s` (whole `u32`, `0x004808f4`), the
  same normalised position (exact, compared by coordinates, not vertex index) and
  `|Δu|, |Δv|, |Δu2|, |Δv2| < 2` (16.16 units); part flag `2` masks `u, v` to their low 16 bits
  first (no shipped text part has it). Otherwise a new point. A new point stores position, `u,v`,
  `u2,v2` (copies of `u,v` without flag `0x80`), `s`, and a zero normal.

**Face normal** of each fan triangle `(a,b,c)` (points' normalised positions):
`n = (fm(Δby,Δcz) − fm(Δbz,Δcy), fm(Δbz,Δcx) − fm(Δbx,Δcz), fm(Δbx,Δcy) − fm(Δby,Δcx))`
with `Δb = b − a`, `Δc = c − a`, then `n = N(n)` where `N` is `0x00469c20`:
1. zero vector → unchanged;
2. while any `|c| ≥ 0x600000`: every component `c = c / 4` (C truncation);
3. while all `|c| ≤ 0x17ffff`: every component `c = c · 4`;
4. `len = S(fm(x,x) + fm(y,y) + fm(z,z))`, `S` = `0x00469a50`: unsigned bit-by-bit integer
   square root of the 32-bit input, then 8 more result bits from `remainder << 16` with
   `bit = 0x4000` (all 32-bit registers) — `≈ floor(sqrt(v)·256)`;
5. `c = (c << 16) / len` (`idiv`), or 0 when `len = 0`. Result: unit vector in 16.16.

**Accumulation** (`0x00480d1e` branches on `s`):
- `s = 0`: the three new points get `n` (`0x00480d40..0x00480d5d`).
- `s ≠ 0`: for each corner `P` with the other corners `Q`, `R` of the triangle,
  `l1 = trunc(sqrt(float32(|P−Q|²)))`, `l2` likewise for `P−R` (x87, squares summed then stored
  as `float32`, `fsqrt`, `_ftol`); `d = fm(ΔQx,ΔRx) + fm(ΔQy,ΔRy) + fm(ΔQz,ΔRz)`;
  `den = fm(l1, l2)`; `c = den ? (d << 16) / den : 0`; the corner angle in 1/65536 turns is
  `w = 0x4000 − T[c]` for `0 ≤ c ≤ 0x10000`, `T[−c] + 0x4000` for `−0x10000 ≤ c < 0`, `0` above,
  `0x8000` below; `T[i] = trunc(asin(i/65536) / (4·asin(1)) · 65536)` (`0x004f0110`, `fistp`
  with chop). Then `normal(P) += (fm(n.x,w), fm(n.y,w), fm(n.z,w))` (`0x00480d65..0x00480fd7`).
- At the record end every point normal (group 0 included) is renormalised by `N`
  (`0x00485c42`) and stored as `int16 = n >> 2`.

The binary parser's own normal builder `0x00481310` (points with a zero `POIN` normal) uses the
same face normal and angle weight but adds `>> 2` terms straight into the int16 normal without a
final renormalise.

## 6. Part bounds and tangents

Per part (`0x00485784..0x004859d0`): centre `+0x10/+0x14/+0x18` = integer mean of all face
corners (3 per triangle), or the vertex named after the part flags when `flags & 4`
(`0x004859a3`); L∞ radius `+0x30` = max corner distance from it. At the end, `0x0047f3f0`
fills the part AABB `+0x40..+0x58` and `0x0047f350` the LOD extents `+0x20..+0x2c`. The
`PART_VALUES_RAW` ints are never read.

Tangent chain for a text-loaded model:

| step | address | text-loaded result |
|---|---|---|
| group records | `0x004858ad`, `0x004858b0` | `+0x18 = 0`, `+0x1c = 0` for every group |
| model flag `+0x50 & 4` | `0x0048625f` | set when an effect material has flag `0x2000` (a `t_BumpTexture` parameter, semantic id 2 at `0x004ba500`, resolved to a texture id by `0x00470320`; flag set in `0x00481780`), a classic material has a second map (`+0x32 ≠ 0`), or a `MATERIAL3` record matched a global material with `+0x28 ≠ 0` |
| declaration | `0x004bd830` → `0x004bb470` | `+0x50 & 4` → `0x0054e210` (POSITION, TEXCOORD0 float4, NORMAL, TANGENT0, BINORMAL0, stride `0x40`), cloned to `0x00608d7c/0x00608d84`; else `0x0054e1d0` (stride `0x28`) — model-wide, every subset |
| vertex fill | `0x004bc1c0` | tangent/binormal from the group records only when part flags `& 0x30000000` and group `+0x18 ≠ 0` (`0x004bc3dd..0x004bc3fe`); otherwise **zero**; `0x800` is raised when the part lacks `0x30000000` (`0x004bc523`) |
| generator | `0x004bb96a..0x004bb980` → `0x004bbb10` | requires `flags & 2`, `flags & 0x800` **and** group `+0x18 ≠ 0` → skipped |
| technique | `0x004c0150`: `0x004c04bb` / `0x004c06ce..0x004c06f2`, `0x004c09a0`, `0x004c09b6`, `0x004c0b69` | bump texture present (`+0x32` or parameter id 2) and node `+0x130 & 0x100000` clear → `BUMPMAP` (effects) / `BUMPMAP_LOW` (`standard_lighting`); else `DEFAULT` |

So a text body with a bump-textured material is drawn with the bump technique and zero
`TANGENT0`/`BINORMAL0`. What the `BUMPMAP` shaders make of a zero basis (bump detail lost, or
the tangent-space light vector collapsed to its normal component) was not examined. The same
chain applies to a binary part without `0x10000000` in a bump model (the 21 shipped
`0x00000001` parts): `0x800` is raised, no records, generator skipped, zero tangents (inferred
from the same gates; no such model was checked). 97 of the bottle's 842 parsable winning text
bodies carry a bump texture (773 `t_BumpTexture` string parameters, 68 classic second maps;
measured, `text_bump_census_out.txt`; that the textures resolve is assumed).

## 7. `COLLISION_BOX`, `PART_VALUES_RAW`, `N:`

All three are `/! … !/` blocks and therefore comments to the lexer (§2); the engine stores
nothing from them. There is no collision-box field on the text-loaded model, LOD or part. The
bounds the engine keeps are the computed ones of §6; the collider is RAPID over mesh triangles
([sector-collide.md](sector-collide.md) §12.2), and which LOD feeds it was not traced here.

## 8. Materials

Classic record (`0x0048447c..0x00484a30`; offsets in the `0x60`-byte record):

| text field | engine | binary parser for comparison |
|---|---|---|
| index | `+0x00` | `+0x00` |
| MAT6 flags | `+0x28`, then **overwritten** by the texture table entry `+0x10` when the texture resolves (`0x0048469b..0x004846be`) | same overwrite |
| texture | `+0x02` (MAT6 name via `0x004f4cb0`) | same |
| `rgb × 3` | `+0x04..+0x14` (9 words) | 12 words `+0x04..+0x1a` |
| transparency | `+0x18` (`u16`) | from the 12 words |
| self-illumination | `+0x1a` | from the 12 words |
| shininess, strength | `+0x24`, `+0x26` | same |
| blend, two-sided, wire | non-zero → `+0x28 |= 2` / `0x10` / `8` (`0x004847b1`, `0x004847d0`, `0x004847e0`), **also for MAT6** | MAT3/MAT5 flag word OR-ed; MAT6 has no such fields |
| texture value | `+0x2c` | same |
| maps | `+0x2e/+0x30`, `+0x32/+0x34`; `+0x36/+0x38` only for MAT5/6 (MAT3 leaves them 0 from the `memset`); MAT6 `+0x1c/+0x1e`, `+0x20/+0x22`, `+0x28 |= 0x8000` | three pairs for every version |
| `+0x16` | MAT5/6: `-1` (`0x00484a07`); MAT3: nearest global material `0x004f71f0` | file word |

Faces reference materials by **array position** for `MATERIAL5/6` and by the record's **index
field** for `MATERIAL3` (searched, then replaced by that record's global index; an unknown index
is a load error); a body without materials indexes the global table directly. `MATERIAL3`
leaves `+0x50 & 0x10` clear, so `0x004863c0`'s coarsest-LOD material `0x485` rewrite applies to
such a body with more than 3 LODs and more than one material. Effect materials go through
`0x00470490` and `0x00481780` exactly as in the binary parser: parameter lists are kept as
written; a `STRING` parameter that names a texture becomes a `LONG` texture id (`0x00470320`).

Against the compiled twins of [body-format-bob1.md](body-format-bob1.md) §8: the "classic MAT6
flags written as 0 where the text says 2 or 18" is a compiler change (the engine's text load
keeps 2 / 18, or the texture's flags OR-ed with the switch bits); "MATERIAL3 converted to MAT6"
and "added parameters (`diffcompression` …, `.tga` → `.dds`)" do not happen at load (whether
texture lookup itself falls back from `.tga` to `.dds` was not examined).

## 9. The `-convertbodies` writer `0x00482fb0`

`-convertbodies` (`0x00564acc`, flag set at `0x004ed03c`) makes `0x004863c0` call the writer
after every successful text load, and `0x004869e0` loads every loose `v\<n>.pbd` at start-up.
The writer opens a **binary** stream (`0x004e9750(…, 1, 0x0054ed50, 0x12)`, `+0x1c = 1`) on
`vc\%05d.bob` / `vc\%s.bob` (path table `+0xbc`) and emits `BOB1`, `NAME` if set, `VERS 1`,
`MAT3` or `MAT6`, `BODY`, `/BOB`:

- each material record starts with the word at **`+0x16`** (`0x004832be`), not `+0x00`: `0xffff`
  for a text-loaded MAT5/6 body;
- an effect material writes only that word and its flags, then skips to the next record
  (`0x004832de..0x004832eb` → `0x004837e4`): no technique, effect or parameters, so the result
  is not readable by `0x00481aa0` (inferred from the layouts);
- points always with flag `0x1b` (position `×4`, UV, normal `×4`, the `u32`), parts as flags,
  group count, groups of (material, face count, 4 ints per face); no tangent records, no bounds
  ints, no second UV set.

It is an X2-era dump of the in-memory model, not the compiler: it produces none of the compiled
twins' transforms and cannot serve as a reference `.bob` writer for effect-material bodies.

## 10. Engine reference against `bob1.parse_text`

`verification/results/bob1-format/text_loader_reference.py` implements §2–§5 and compares it
with `bob1.parse_text` over the bottle's winning text bodies
(`text_loader_reference_out.txt`, measured): 865 bodies, 855 parsed by the reference (the 10
misses are bodies `bob1` refuses before their geometry), 842 compared, 951 records.

| quantity | result |
|---|---|
| faces | 305,494 triangles, 13 quads; triangle index lists identical wherever the point lists match |
| points | 516,994 (engine) vs 503,521 (`bob1`); counts differ in 127 of 951 records |
| positions | differ in 674 records (61,782 points): per-record normalisation and truncation vs a fixed scale with rounding |
| UV | 77,711 points differ (truncation vs rounding, inferred) |
| normals (int16, 16384 = 1), largest component difference | 0: 26,828; 1: 9,094; 2–16: 20,293; 17–1024: 41,453; > 1024: 24,282 of 121,950 points in records with equal point counts (engine geometry normals vs `bob1`'s `N:` blocks and area weights); in the records whose positions also agree: 1,236 / 170 / 0 / 0 / 66 |
| face word (compared records) | engine 24 (83,732), 8 (1,815), 0 (1) where `bob1` writes 1 |

## Consequences for the pilot

A `.pbb` compiled by `bob1.parse_text` is **not** what the engine builds from the same text
body: it keeps the `N:` normals, a fixed position scale, rounded UVs, merged flat corners and
face word 1, and it has no tangent records either. Where the goal is "look like the shipped text
body in game", the engine rule of §4–§5 is the reference; where the goal is "look like the
dbox2/x2bc compile", `bob1` is. Either way a bump-textured part needs `0x10000000` tangent
records (or it is drawn with zero tangents, §6).

No hook is proposed; nothing here is a hook site.

## Unknown

- What the `BUMPMAP` / `BUMPMAP_LOW` shaders output with a zero tangent basis.
- Which LOD's triangles the RAPID collision model is built from, and whether it reads the part
  bounds of §6.
- The `SOUND:` fields (`+0x18..+0x4a`) and `NAME:`; no shipped text body was checked for them.
- ~~`0x004f4cb0`'s texture-name resolution (extension fallback) and `0x004baa30`'s STRING → id.~~
  Resolved in [texture-lookup.md](texture-lookup.md).
- Whether the x87 precision in effect during a body load is 53/64-bit (the reference assumes
  one `float32` rounding of the squared length); only the corner weight depends on it.
- Where `0x004d2950("v")` resolves the loose `v` folder for `-convertbodies`.

## Reproduce

```sh
# Ghidra (raw output stays local; project as in body-format-bob1.md)
# -process X3AP.exe -noanalysis -readOnly -postScript X3DecompileFunctions.java <out> \
#   00483f20 00480830 00469c20 00469a50 004f0110 00482fb0 004869e0 00486310 004bb470 \
#   004bc1c0 004bbb10 004bd830 00481780 00470490 00470320 004ba500 004e98a0 004eb930 004ec490
# -postScript X3XrefsTo.java <out> 00596990 00483f20 00482fb0 00480830 00469c20
# Capstone windows: 0x004857b7..0x004858bd (group copy), 0x00484fb0..0x00485070,
#   0x004bb900..0x004bb9b0, 0x004bc3a0..0x004bc630, 0x004c0620..0x004c0bb0, 0x004ecfd0..0x004ed070
# Engine reference vs bob1 (about 20 s) and bump census (about 15 s), bottle X3
PYTHONPATH=tools/analysis python3 verification/results/bob1-format/text_loader_reference.py \
  > verification/results/bob1-format/text_loader_reference_out.txt
PYTHONPATH=tools/analysis python3 verification/results/bob1-format/text_bump_census.py \
  > verification/results/bob1-format/text_bump_census_out.txt
```
