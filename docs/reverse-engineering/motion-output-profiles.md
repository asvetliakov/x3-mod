# Per-program motion-output profiles

Static review, 2026-09-12; archive-wide since the same day. The
[candidate review](motion-output-candidate.md) derived the splice facts for one
Argon SM3 pair (VS 335 / 450 / 466, PS 1047 / 1074, appending before the END at
1259; the original literal DEFs sit at 302 and 1041), which the first
[`material_motion.cpp`](../../src/renderer/material_motion.cpp) hard-coded.
This document derives the same facts for **every vertex/pixel pairing that a
technique pass of the installed compiled effects binds**, not only the pairs
one captured session happened to draw. The transformer is driven by the
generated table below (see
[material-motion-prototype.md](../architecture/material-motion-prototype.md));
the [live route](../architecture/live-motion-route.md) therefore covers every
transformable SM3 material pair of the shipped archives without a user visiting
each sector and race.

Everything here is derived structure: hashes, DWORD offsets, register numbers,
counts, technique and pass names. No shader words, literals or disassembly are
reproduced. Original bytecode and D3DX text stay untracked under
`/tmp/x3-shader-sweep/`; the effect containers are read from the CAT/DAT
archives in memory and nothing is copied.

## Method and provenance

[`effect_passes.py`](../../tools/analysis/effect_passes.py) walks every
`shader/**/*.fb` entry of the 17 numbered archives (the same decode as the
[shader sweep](shader-sweep.md)) and parses the compiled D3DX effect container
(`0xfeff0901`: parameter, technique, pass and state records, then object data
and per-state resources; the VertexShader and PixelShader state operations are
146 and 147 of the D3DX state table). Each pass yields the FNV-1a 64 identity
and length of its vertex and pixel program, or `null` for a state without a
resource. The count identity confirms the parse: the 6,752 passes of the 3,480
effects carry 13,408 shader resources, the 13,407 token streams the sweep
indexed plus one resource that is not a complete program (the vertex state of
the pass holding the anomalous `d66dd16fc0a6c3a3`, see the sweep note).

[`inspect_motion_output_profiles.py`](../../tools/analysis/inspect_motion_output_profiles.py)
walks complete SM1-3 instruction boundaries from the version token, keeping
comments as opaque data, and splits each instruction into destination and source
operands. SM2+ counts a relative-address token inside the instruction length, so
operands are walked rather than read positionally. Every program bound by a
complete pass is profiled (748 of the 751 archive programs; the two z-only
vertex programs of the pixel-less `Z_Only_Fast` technique and the anomalous
pixel program's partner are never paired with a complete counterpart).

Offsets are zero-based DWORD indices from the version token, including all
comments, matching the candidate review and the transformer. The tool
reproduces every documented Argon number (`reference_check.passed`), and both the
tool and [`test_motion_output_profiles.py`](../../verification/analysis/test_motion_output_profiles.py)
fail if it stops doing so.

```sh
python3 tools/analysis/effect_passes.py \
  "$HOME/Library/Application Support/CrossOver/Bottles/Steam/drive_c/X3" \
  --output /tmp/x3-effect-passes.json          # derived names/hashes only
python3 tools/analysis/inspect_motion_output_profiles.py \
  --inventory verification/results/shader-sweep-inventory.json \
  --raw-directory /tmp/x3-shader-sweep/programs \
  --pass-table /tmp/x3-effect-passes.json \
  --capture-log /tmp/x3-iteration05-completed-snapshot.log \
  --output verification/results/motion-output-profiles.json \
  --emit-header src/renderer/motion_output_profiles_inc.h
python3 -m unittest verification.analysis.test_motion_output_profiles
```

`--game <root>` enumerates the passes directly instead of `--pass-table`;
`--capture-log` is optional. The JSON is compact (2.5 MB); query it with a
script or the paired tests rather than reading it whole.

### What "observed" means

Draw counts come from a read-only pass over
`/tmp/x3-iteration05-completed-snapshot.log` (216,605,445 bytes, SHA-256
`e5beaa861d04659fe9c7df05a01845bd05d656a33c643f4b484ff379cf3ccaf8`), split at
each frame's `Clear` calls as before: segment 1 is the material span the
selector calls Scene. Across 28 frames the capture holds 12,957 draws; 11,493
fall in the Scene segment across 25 distinct VS/PS pairs, all of which are
archive pass pairings (`captured_pairs_outside_archive` is empty). These counts
are **metadata only**: `observed_draws` is zero for the pairs the session never
drew, and its sole uses are the row order (observed rows first, so the sixteen
rows of the capture-derived table keep their relative order) and the choice of
the exhaustive single-bit sweep in the structural fixture. Coverage itself is
decided by the archive, not by the capture.

## Coverage

The 6,752 passes bind **817 distinct pairings**: 180 SM3, 466 SM2, 168 SM1 and
3 without a complete counterpart. Techniques are `DEFAULT` (3,600 passes),
`BUMPMAP` (1,776), `INSTANCE` (480), `BUMPMAP_LOW` (384), `INSTANCE_BULLETS`
(288), `Z_Only_Alpha` and `Z_Only_Fast` (96 each) and `HDR` (32, bloom); every
material pass is `P0`.

| Group | Pairs | Pass occurrences | Captured Scene draws | Share of capture |
| --- | ---: | ---: | ---: | ---: |
| SM3 class A — reference registers | 56 | 224 | 4,025 | 35.02% |
| SM3 class B — relocated registers | 101 | 352 | 2,517 | 21.90% |
| SM3 class C — relocated registers, static branches in PS | 12 | 112 | 4,680 | 40.72% |
| SM3 class D — bounded damage IFC | 2 | 8 | 0 | 0 |
| **A + B + C + D (table rows)** | **171** | **696** | **11,222** | **97.64%** |
| SM3 X — position not a row dot | 9 | 80 | 112 | 0.97% |
| SM2 (feasibility only, no rows) | 466 | 2,911 | 111 | 0.97% |
| SM1 (unsupported) | 168 | 2,968 | 48 | 0.42% |
| Passes without a VS or PS | 3 | 97 | 0 | 0 |

**171 of the 180 SM3 pairings are rows**: 32 distinct vertex programs, 110
distinct pixel programs, all in the `3_0` effect directory, from the
`DEFAULT` (82 rows), `BUMPMAP` (75) and `BUMPMAP_LOW` (14) techniques. By
family (a row counts once per family it occurs in): standard_lighting 30,
argon / khaak / teladi / teladi_nodiff / xenon / split / terran / paranid 20
each, boron 12, asteroid 10, xt_standard_lighting 6, xt_terraformer 6,
glass 6, xt_standard_lighting_damage 6, moon 4, planet_haze 2. Sixteen rows
were observed by the capture (the previous table); 155 were classified from
the archive alone and have never been drawn in a captured session.

## Two clip-row families

Every row's vertex program writes clip position with four
`dp4 o0.<lane>, r<n>, c<m+lane>` issued in XYZW order (adjacent in 165 rows,
spaced in six, see below), but the archive holds two families:

| Family | Rows | Matrix register | Relative light loop | Effects |
| --- | ---: | :-: | :-: | --- |
| Point-light programs | 109 (A 33, B 62, C 12, D 2) | **c24** | yes: `rep i0` reading c0/c1/c2 at `a0.w`; the draw-time bound `i0.x` in [0, 8] keeps the reads in c0–23 | base and `2s` effects, all class C |
| Loop-free variants | 62 (A 23, B 39) | **c0** | none: no relative addressing at all | the `_0000` / `_0001` effects (argon, khaak, boron, glass, moon, paranid, ...) |

Here “loop-free” means no relative constant addressing; it does not mean
unlit. Targeted material inspection found a fixed single point light in
Argon VS `badefd5143b3024f`, using c4–6 before adding material emissive.
See the [material slice findings](../architecture/scene-linear-materials.md#targeted-shader-findings).

The consumer is generalized per row rather than pinned to c24: the route's
shadow captures every distinct clip-row window the table names (derived from
the rows at compile time; c24–27 and c0–3 today) and gate 4 applies
`light_loop_bound_required` / `light_loop_max_count` of the bound VS row, so a
loop-free row needs known rows in its window but no `i0.x` bound. A
`static_assert` still requires that a bounded row's clip rows lie above the
c0–23 light block the bound protects (see
[material-motion-prototype.md](../architecture/material-motion-prototype.md)).
`c252–255` is free in every row VS and `c216–220` / `oC1` in every row PS;
`c40–43` is *not* read by every VS any more (the loop-free variants keep
their material constants elsewhere), which only the structural fixture's
perturbation choice depended on.

## Transformation classes

All three transformable classes share one offset table per pair — VS declaration
insert, VS arithmetic insert, matrix register, position temporary, PS definition
insert, PS declaration insert, PS append point — plus four register choices. They
differ only in what a transformer must additionally check or substitute.

**A — reference registers (56 pairs).** Identical to the reviewed Argon splice:
one new VS output **o6/TEXCOORD4**, one new PS input **v5**, the relocated
fragment in **r5–7**, **c216–220** and **oC1**. Only the insertion offsets and
the position registers change between pairs. No class A, B or C pixel program
contains `texkill`, predication, relative addressing or an oDepth write, and
every row VS declares `o0` as POSITION0; the transformer refuses programs that
violate any of these.

**B — relocated registers (101 pairs).** Same shape, but a reference index is
occupied and the row names free substitutes: VS output/TEXCOORD pairs o6/4
(60 rows), o7/5 (62), o8/6 (17), o9/7 (14), o7/4 (6), o9/5 (10); PS inputs
v5 (56), v6 (72), v7 (17), v8 (24); PS temporaries from r5 (99), r6 (52) or
r7 (18). Register choices are derived from the measured free lists,
preferring the reference indices whenever they are still free, so a row that
differs from `o6 / TEXCOORD4 / v5 / r5` differs because the reference register
is genuinely occupied. Rows sharing a program always agree on that program's
side of the splice (the generator's choices are functions of the program's own
free lists, and the TEXCOORD index is free in both programs of every pair that
shares them); the consumer's `static_assert` requires it and the tests mirror it.

**C — relocated registers with static branches in the PS (12 pairs).** As B,
plus the pixel program contains balanced `if`/`else`/`endif` blocks on boolean
constants (maximum nesting depth 1, depth 0 at END). The archive has two
shapes: six programs with two sequential blocks on `b0` and `b1` (the captured
ones among them) and six with a single block on `b0`. All twelve are `xt_*`
materials: xt_standard_lighting, xt_standard_lighting_damage and
xt_terraformer with their `2s` variants. The transformer revalidates the
shape from the words and refuses `ifc`, `rep`/`loop`, `break*`, `call`/`ret`
and predication in every class.

**Spaced position quads (six rows, classes A and B).** The loop-free
asteroid_0000/0001 (four pairs, VS `0c223ad11bce02d5`, `12b8a13f13fe8cfe`,
`233d17d26ce0c1fc`, `330ceb9dd874ede2`), moon_0000/0001 (`8198903322dd82fb`)
and planet_haze_0000/0001 (`d706d31100be1be9`) vertex programs issue the
four position dots in XYZW order but interleave other instructions between
the Z and W dots (planet_haze: 58 DWORDs of them). The row records the four
offsets individually (`position_dp4_dwords`), the arithmetic insert follows
the last dot, and the JSON marks the pair `position_quad_contiguous: false`.
The generator admits such a quad only when the span from the first dot to the
insert rewrites no position temporary (`position_temporary_rewritten_inside_quad`)
and holds no control-flow instruction (`position_flow_inside_quad`); the
transformer revalidates both from the words (any write to the temporary,
whatever its mask, and any `rep`/`loop`/`if`/`ifc`/`else`/`endrep`/`endloop`/
`endif` in the span refuse, so a balanced block inside the span refuses even
though the depth is zero at every dot). The register choices fall out as for
any other pair: four class A rows and two class B rows.

Because the PS side determines two of the four register choices, eligibility
is keyed by the **pair**: the route substitutes variants only when the bound VS
and PS fingerprints appear together in one row (a binary search over the
sorted pair index, see the prototype note), never by a VS alias.

## SM3 pairs that cannot host the transformation

Nine SM3 pairings (80 pass occurrences, 112 captured draws) have no
row; `unsupported_pairs` in the JSON lists each with its reasons.

| Pairs | Effects | Reason |
| ---: | --- | --- |
| 9 | bloom, bloom_0000 (HDR technique passes) | `position_write_is_mov`: the VS writes `o0` with `mov`. Fullscreen/post passes with no world transform; no previous WVP to apply. Two of them also have no free PS input register. |
The bloom passes keep the sentinel by design. The former damage refusals
now have a separate [bounded motion contract](../architecture/damage-motion.md),
with host proof and detached X3 R2 GPU qualification complete; no generic IFC
acceptance was added.

## SM2 remainder: feasibility, no rows

The 466 SM2 pairings (vs_2_0 / vs_2_x with ps_2_0 / ps_2_x; 2,911 pass
occurrences; 111 captured draws) receive a `sm2_pairs` feasibility record
against the documented profile limits — ps_2_0: 64 arithmetic and 32 texture
slots, 12 temporaries, 32 float constants; ps_2_x (`2_a` / `2_b`
directories, version token 2.1): up to 512 slots (device caps decide), 22 or
32 temporaries, 32 constants; vs_2_0 / vs_2_x: 256 slots. The authored motion
fragment is 25 arithmetic instructions (mov, add, mad, mul, rcp, dp4, max,
cmp — all present in ps_2_0), no texture instruction, three DEFs, one input
declaration, three temporaries and five constants; the vertex side adds four
DP4s. SM2 links `oT<n>` to `t<n>` by index and clamps `oD#`/`v#` colors, so
previous clip needs one index free in both programs.

| Group | Pairs | Pass occurrences | Note |
| --- | ---: | ---: | --- |
| Hostable with a ps_2_0 fragment | 115 | 822 | Row-dot quad (adjacent or spaced under the same span rule as SM3), free oT/t link, free oC1, ≥ 3 free temporaries and ≥ 5 free constants within the limits, arithmetic + 25 ≤ 64 (maximum 88 − 25 = 63 used). |
| Hostable with a ps_2_x fragment | 267 | 1,263 | 106 in `2_a`, 161 in `2_b`; the largest program would reach 125 of 512 slots. |
| Exceeds limits | 16 | 122 | All ps_2_0: arithmetic over the 64-slot budget by 2 (standard_lighting_0001 ×2, standard_lighting2s), 3–24 (xt_standard_lighting, xt_standard_lighting_damage, xt_terraformer and their `2s`); one of them is also one temporary short. |
| Structurally unsupported | 68 | 704 | 32 with no free oT/t index (paranid and boron: all eight `t#` in use), 16 WXYZ issue order (effects, engine; the captured effects/engine pair), 20 `mov` positions (bloom). 30 SM2 pairs have spaced quads; the 14 asteroid_0000, moon_0000 and adeffects ones are hostable, the 16 effects/engine ones fail on the issue order. |

275 of the SM2 vertex programs read constants relatively (the same light loop),
191 do not. **No code path handles SM2**: this record quantifies what a
separate ps_2_0 / ps_2_x fragment and an `oT`/`t` linkage would face. Of the
two captured SM2 pairs, effects/engine (103 draws) is structurally unsupported
(WXYZ) and adeffects (8 draws) would be hostable with a ps_2_0 fragment.

## SM1 remainder

168 pairings (105 vs_1_1 + ps_1_1, 63 vs_1_1 + ps_1_4; 2,968 pass
occurrences; 48 captured draws: gui2d/stardust, particles, planet_haze) are
unsupported: ps_1_x has no second color target (`oC1`) and no declared
`o#`/`v#` linkage, so there is no motion output to add. They keep the sentinel,
exactly as the live route specifies for particles, stardust and overlays.

## Generated table

`--emit-header` writes [`src/renderer/motion_output_profiles_inc.h`](../../src/renderer/motion_output_profiles_inc.h),
the transformer's input: a bare constexpr row list. The consumer defines the
row struct and the `MotionOutputClass` enum; the file carries only derived
numbers, plus the version tokens.

```sh
python3 tools/analysis/inspect_motion_output_profiles.py \
  --from-profiles verification/results/motion-output-profiles.json \
  --emit-header src/renderer/motion_output_profiles_inc.h
```

**171 rows** are emitted: 56 class A, 101 class B, 12 class C and 2 class D. Rows are
ordered by descending observed Scene draws, then by vertex and pixel
fingerprint, so regeneration is byte-reproducible and the sixteen observed
rows come first in their previous order with every previous field unchanged
(`OBSERVED_ROWS` in the tests). Field order, one row:

| Field | Meaning |
| --- | --- |
| `vertex_fingerprint`, `vertex_dword_count`, `vertex_version` | Exact original VS identity and length; `0xfffe0300`. |
| `pixel_fingerprint`, `pixel_dword_count`, `pixel_version` | Exact original PS identity and length; `0xffff0300`. |
| `transformation_class` | `ReferenceRegisters` (A), `RelocatedRegisters` (B) or `RelocatedRegistersWithBranches` (C). |
| `matrix_register`, `position_temporary` | The clip-row base (c24 or c0) and the temporary the position dots read. |
| `position_dp4_dwords[4]`, `position_lane_masks[4]` | The four original DP4 offsets in XYZW order (not necessarily adjacent) and their single-lane destination masks, for revalidation before splicing. |
| `vertex_declaration_insert_dword` | Original VS header end; the new output declaration goes here. |
| `vertex_arithmetic_insert_dword` | One past the last position DP4 (the W dot); the four previous-row dots go here. |
| `pixel_definition_insert_dword` | One past the original PS literal DEFs; the relocated definitions go here. |
| `pixel_declaration_insert_dword` | Original PS header end; the new input declaration goes here. |
| `pixel_append_dword` | The original END index; executable work is appended before it. |
| `vertex_output_register`, `texcoord_index` | Free VS output and TEXCOORD index for previous clip, free in both programs. |
| `pixel_input_register`, `pixel_temporary_base`, `pixel_output_register` | The matching PS input, the first of three consecutive free temporaries, and `oC1`. |
| `vertex_constant_base`, `pixel_constant_base` | `252` (four previous rows) and `216` (five ABI constants). |
| `light_loop_bound_required`, `light_loop_max_count` | True where the VS reads constants relatively: refuse the variant unless `i0.x` is checked in `[0, 8]` at draw time. False for the loop-free rows. |
| `observed_scene_draws` | Metadata: Scene draws of the pair in the captured session, zero when never observed. Orders the rows and selects the fixtures' exhaustive sweep; no runtime meaning. |

The offsets and registers are transformer *input*: the transformer must still
revalidate the program it is handed. A row is not an eligibility decision.

## Runtime checks this table does not replace

- **Relative light loop.** Every c24 row VS reads `c0`, `c1` and `c2` at
  `a0.w` inside a `rep i0` block. A base register is a syntactic reference,
  not a bound: the JSON records
  `relative_addressing.bounded_by_static_analysis: false`. `c252–255` may only
  be written when the integer count `i0.x` is checked in `[0, 8]` at draw time,
  which bounds the reads to c0–23. The live route applies exactly the row's
  bound and shadows the row's window; `motion_output.cpp` asserts at compile
  time that every row names a shadowed window and, when bounded, uses the
  8-light bound below its rows.
- **Application constants.** Save and restore application `c252–255` (VS) and
  `c216–220` (PS) around the transformed draw; D3DX/effect uploads do not know
  about the extension.
- **Draw state, MRT and history.** Unchanged from the live route: alpha
  test/blend/sRGB off, Z on, COLORWRITEENABLE 15, `D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS`
  and a mixed-format MRT self test, object/camera lifetime and a matched history
  entry. A profile row is evidence about program structure only.
- **Centroid.** Several VS programs declare centroid on some TEXCOORD outputs.
  The new interpolator must not inherit those modifiers, and the new PS
  declaration must not use `_pp` or centroid even where neighbouring
  declarations do.
- **Pair-and-state eligibility.** Aliases are broad: `53a0a641107ed76c` alone
  appears under argon, khaak, split, teladi and xenon effect paths, and the 32
  row vertex programs serve 110 pixel programs. Eligibility stays keyed to the
  exact pair and draw state, never to a VS alias.

The complete derived table — per-pair effect names, techniques and pass
occurrences, per-program declarations, opcode histograms, free-register lists,
the SM2 feasibility records, the SM1 list and the Argon reference check — is in
[`motion-output-profiles.json`](../../verification/results/motion-output-profiles.json).
