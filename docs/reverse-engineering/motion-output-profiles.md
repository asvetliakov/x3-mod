# Per-program motion-output profiles

Static review, 2026-09-12. The [candidate review](motion-output-candidate.md)
derived the splice facts for one Argon SM3 pair (VS 335 / 450 / 466, PS 1047 /
1074, appending before the END at 1259; the original literal DEFs sit at 302
and 1041), which the first
[`material_motion.cpp`](../../src/renderer/material_motion.cpp) hard-coded.
This document derives the same facts for the other SM3 material programs the
captured session draws; the transformer is now driven by the generated table
below (see [material-motion-prototype.md](../architecture/material-motion-prototype.md))
so the [live route](../architecture/live-motion-route.md) covers the class A
and B pairs without a second hand-written transformer.

Everything here is derived structure: hashes, DWORD offsets, register numbers
and counts. No shader words, literals or disassembly are reproduced. Original
bytecode and D3DX text stay untracked under `/tmp/x3-shader-sweep/`.

## Method and provenance

[`inspect_motion_output_profiles.py`](../../tools/analysis/inspect_motion_output_profiles.py)
walks complete SM1-3 instruction boundaries from the version token, keeping
comments as opaque data, and splits each instruction into destination and source
operands. SM2+ counts a relative-address token inside the instruction length, so
operands are walked rather than read positionally; a naive positional read would
mistake `c0[a0.w]`'s address token for an ordinary operand. Every parsed program's
instruction count was independently reconciled against its local D3DX
disassembly line count: 40 of 40 agree.

Offsets are zero-based DWORD indices from the version token, including all
comments, matching the candidate review and the existing transformer. The tool
reproduces every documented Argon number (`reference_check.passed`), and both the
tool and [`test_motion_output_profiles.py`](../../verification/analysis/test_motion_output_profiles.py)
fail if it stops doing so.

```sh
python3 tools/analysis/inspect_motion_output_profiles.py \
  --inventory verification/results/shader-sweep-inventory.json \
  --raw-directory /tmp/x3-shader-sweep/programs \
  --capture-log /tmp/x3-iteration05-completed-snapshot.log \
  --output verification/results/motion-output-profiles.json
python3 -m unittest verification.analysis.test_motion_output_profiles
```

Draw counts come from a read-only pass over
`/tmp/x3-iteration05-completed-snapshot.log` (216,605,445 bytes, SHA-256
`e5beaa861d04659fe9c7df05a01845bd05d656a33c643f4b484ff379cf3ccaf8`). The capture
has no per-draw phase field, so the pass splits each frame at its `Clear` calls:
segment 1 is the span between the first depth-only Clear and the next Clear, which
is the material span the selector calls Scene. That is a derived approximation of
the phase, not the selector's own decision. Across 28 frames the capture holds
12,957 draws; **11,493** fall in the Scene segment across **25 distinct VS/PS
pairs**. These counts describe one captured session, not the shipped game.

## Coverage

| Class | Pairs | Scene draws | Share |
| --- | ---: | ---: | ---: |
| A — reference registers | 6 | 4,025 | 35.02% |
| B — relocated registers | 6 | 2,517 | 21.90% |
| C — relocated registers, static branches in PS | 4 | 4,680 | 40.72% |
| **A + B + C (transformable)** | **16** | **11,222** | **97.64%** |
| X — position not a row dot | 4 | 112 | 0.97% |
| X — not an SM3 material pair | 5 | 159 | 1.38% |

Three classes therefore cover 97.64% of Scene-segment draws, and the top
**13** transformable pairs alone reach 95.30%. The one pair the current
transformer handles covers 18.97%.

## Pairs by Scene draw count

`Insert` columns give the DWORD offsets a table-driven transformer needs:
VS *declaration* insert (original header end) / VS *arithmetic* insert (after the
last position DP4), then PS *definition* insert / PS *declaration* insert (header
end) / PS *append* point (the original END index). `Link` gives the free VS
output and TEXCOORD index, the PS input register, and the three PS temporaries
for the relocated motion fragment. Every transformable VS writes clip position
with four contiguous `dp4 o0.<lane>, r<n>, c<24+lane>` in XYZW order from
matrix register **c24**, and every one of them has the relative light loop.

| Scene draws | Share | VS | PS | PS effect | Class | Link | Insert (VS decl / VS arith) | Insert (PS def / decl / append) |
| ---: | ---: | --- | --- | --- | :-: | --- | --- | --- |
| 2232 | 19.42% | `494fe349b8bc12ec` | `fffdabd910793aba` | xt_standard_lighting(+damage) | C | o6/TC4 → v6, r6–8 | 335 / 466 | 1280 / 1313 / 1647 |
| 2180 | 18.97% | `53a0a641107ed76c` | `8759c7838bbc86c2` | argon | **A** | o6/TC4 → v5, r5–7 | 335 / 466 | 1047 / 1074 / 1259 |
| 1804 | 15.70% | `37c34a7478544c14` | `5f82ecacd39529cd` | xt_standard_lighting | C | o9/TC7 → v8, r6–8 | 500 / 649 | 1325 / 1370 / 1764 |
| 1440 | 12.53% | `4944d81dfe531b37` | `ca6bfa4a6cca7e2a` | argon | B | o7/TC5 → v6, r7–9 | 344 / 493 | 1062 / 1095 / 1327 |
| 940 | 8.18% | `b0602757fce6e870` | `517540ae6d5e5410` | asteroid | **A** | o6/TC4 → v5, r5–7 | 335 / 457 | 233 / 257 / 396 |
| 456 | 3.97% | `4944d81dfe531b37` | `5e0a10fe752b6140` | argon2s | B | o7/TC5 → v6, r7–9 | 344 / 493 | 1062 / 1098 / 1353 |
| 348 | 3.03% | `37c34a7478544c14` | `f1b0e820c7b488c3` | xt_standard_lighting2s | C | o9/TC7 → v8, r6–8 | 500 / 649 | 1325 / 1373 / 1790 |
| 332 | 2.89% | `53a0a641107ed76c` | `63f96eba9eea7880` | argon2s | **A** | o6/TC4 → v5, r5–7 | 335 / 466 | 1053 / 1083 / 1291 |
| 308 | 2.68% | `167eb2d5629ab9d3` | `d44db87778a43b61` | asteroid | B | o8/TC6 → v7, r5–7 | 347 / 487 | 241 / 274 / 447 |
| 296 | 2.58% | `494fe349b8bc12ec` | `e6794b6ec37ff71a` | xt_standard_lighting2s | C | o6/TC4 → v6, r6–8 | 335 / 466 | 1280 / 1316 / 1673 |
| 236 | 2.05% | `53a0a641107ed76c` | `3b94320087e81945` | khaak/teladi/xenon | **A** | o6/TC4 → v5, r5–7 | 335 / 466 | 1047 / 1074 / 1263 |
| 233 | 2.03% | `53a0a641107ed76c` | `462342e3e5781384` | split | **A** | o6/TC4 → v5, r5–7 | 335 / 466 | 1053 / 1080 / 1249 |
| 148 | 1.29% | `4944d81dfe531b37` | `64bac8bb307eb896` | standard_lighting2s | B | o7/TC5 → v6, r7–9 | 344 / 493 | 1112 / 1148 / 1391 |
| 104 | 0.90% | `494fe349b8bc12ec` | `7c83ed50c9894e44` | standard_lighting | **A** | o6/TC4 → v5, r5–7 | 335 / 466 | 1097 / 1124 / 1297 |
| 103 | 0.90% | `d5e1c75351ed3f04` | `8360f422de08b5bd` | effects/engine (SM2) | X | — | — | — |
| 88 | 0.77% | `4944d81dfe531b37` | `0c1f3f0f440e4a0c` | standard_lighting | B | o7/TC5 → v6, r7–9 | 344 / 493 | 1112 / 1145 / 1365 |
| 77 | 0.67% | `c30104cb0efb6675` | `a66fb1981ba755b2` | glass | B | o7/TC4 → v6, r5–7 | 338 / 469 | 122 / 149 / 308 |
| 28 | 0.24% | `1279d081455f5815` | `ff6eed5a5ddf3a3a` | bloom | X | — | — | — |
| 28 | 0.24% | `6059306306203243` | `241c3fa33270f58e` | bloom | X | — | — | — |
| 28 | 0.24% | `6059306306203243` | `f3172baa8dd19a40` | bloom | X | — | — | — |
| 28 | 0.24% | `cbbf26102694c961` | `1c90e79667bdaddf` | bloom | X | — | — | — |
| 24 | 0.21% | `5e484a06672e28fb` | `0a523f33ac47ae05` | gui2d/stardust (SM1) | X | — | — | — |
| 20 | 0.17% | `36f98d151fd6b0c6` | `222bee0defcb1852` | particles (SM1) | X | — | — | — |
| 8 | 0.07% | `ac2319bc3953efc6` | `03a16e5c63daa6e8` | adeffects (SM2) | X | — | — | — |
| 4 | 0.03% | `be199829a9bb78db` | `cd6d6eb4b3d99443` | planet_haze (SM1) | X | — | — | — |

## Transformation classes

All three transformable classes share one offset table per pair — VS declaration
insert, VS arithmetic insert, matrix register, position temporary, PS definition
insert, PS declaration insert, PS append point — plus four register choices. They
differ only in what a transformer must additionally check or substitute.

**A — reference registers (6 pairs, 35.02%).** Identical to the reviewed Argon
splice: one new VS output **o6/TEXCOORD4** fed by four DP4s from the position
temporary and **c252–255**, one new PS input **v5**, the relocated fragment in
**r5–7**, **c216–220** and **oC1**. Only the five insertion offsets and the
two position register numbers change between pairs; the emitted instruction
words are the same as today's hard-coded fragment. One of
these PS programs (`63f96eba9eea7880`) additionally declares `vFace`, which the
fragment does not touch. No class A or B pixel program contains `texkill`,
predication or an oDepth write (`texkill_dwords`, `predicated_or_coissued_dwords`
and `depth_output_dwords` are empty for all twelve), and every class A or B
vertex program declares `o0` as POSITION0; the transformer refuses programs
that violate any of these, so the classes are defined as straight-line
programs whose depth is the rasterized depth.

**B — relocated registers (6 pairs, 21.90%).** Same shape and same offset table,
but the reference indices are occupied and the transformer must substitute:
`4944d81dfe531b37` already uses o6 and TEXCOORD4 (free: o7/TEXCOORD5) and its
pixel shaders use v5 and r5–6 (free: v6, r7–9); `167eb2d5629ab9d3` uses o6–o7
(free: o8/TEXCOORD6, v7); `c30104cb0efb6675` uses o6 for COLOR1 (free: o7) and
glass uses v5 (free: v6). This needs the relocation the existing module already
performs, parameterised by the per-program free registers instead of the fixed
`r5–7 / v5 / c216–220 / oC1` mapping. `c216–220` and `oC1` are free in every
case; `c252–255` is free in every SM3 material VS (highest direct constant is
c42 or c47).

**C — relocated registers with static branches in the PS (4 pairs, 40.72%).**
As B, plus the pixel shader contains two balanced `if`/`else`/`endif` blocks on
boolean constants `b0`/`b1` (maximum nesting depth 1, depth 0 at END). The append point is
still at nesting depth 0 immediately before END, so the appended fragment is
unconditional, but a table-driven transformer must verify balance and append
depth rather than assume straight-line code as the current module does. These are
the largest pairs by draw count, so this check cannot be deferred indefinitely.

Because the PS side determines two of the four register choices, eligibility
must be keyed by the **pair**, not by the VS program. In this capture every
pair sharing a VS happens to agree on the VS choice; the consumer turns that
from an incidental fact into a compile-time requirement (`static_assert` in
`motion_output_profiles.h`) so it can keep one variant per original program,
see the live route's "Pair keying" section.

## Generated table

`--emit-header` writes [`src/renderer/motion_output_profiles_inc.h`](../../src/renderer/motion_output_profiles_inc.h),
the transformer's input: a bare constexpr row list in the style of
[`rigid_position_profiles_inc.h`](../../src/renderer/rigid_position_profiles_inc.h).
The consumer defines the row struct and the `MotionOutputClass` enum; the file
carries only derived numbers, plus the version tokens.

```sh
python3 tools/analysis/inspect_motion_output_profiles.py \
  --from-profiles verification/results/motion-output-profiles.json \
  --emit-header src/renderer/motion_output_profiles_inc.h
```

**12 rows** are emitted today: the six class-A and six class-B pairs. Class C
is deliberately left out until the transformer validates control-flow balance
and append depth, but its enumerator name
(`MotionOutputClass::RelocatedRegistersWithBranches`) is already reserved in the
banner and defined by the consumer, so adding those four rows will not change
the schema (the transformer currently refuses that class with
`UnsupportedShader`).

Rows are ordered by descending captured Scene draws, then by vertex and pixel
fingerprint, so regeneration is byte-reproducible. Field order, one row:

| Field | Meaning |
| --- | --- |
| `vertex_fingerprint`, `vertex_dword_count`, `vertex_version` | Exact original VS identity and length; `0xfffe0300`. |
| `pixel_fingerprint`, `pixel_dword_count`, `pixel_version` | Exact original PS identity and length; `0xffff0300`. |
| `transformation_class` | `ReferenceRegisters` (A) or `RelocatedRegisters` (B). |
| `matrix_register`, `position_temporary` | The `c24` row base and the temporary the position dots read. |
| `position_dp4_dwords[4]`, `position_lane_masks[4]` | The four original DP4 offsets in XYZW order and their single-lane destination masks, for revalidation before splicing. |
| `vertex_declaration_insert_dword` | Original VS header end; the new output declaration goes here. |
| `vertex_arithmetic_insert_dword` | One past the last position DP4; the four previous-row dots go here. |
| `pixel_definition_insert_dword` | One past the original PS literal DEFs; the relocated definitions go here. |
| `pixel_declaration_insert_dword` | Original PS header end; the new input declaration goes here. |
| `pixel_append_dword` | The original END index; executable work is appended before it. |
| `vertex_output_register`, `texcoord_index` | Free VS output and TEXCOORD index for previous clip, free in both programs. |
| `pixel_input_register`, `pixel_temporary_base`, `pixel_output_register` | The matching PS input, the first of three consecutive free temporaries, and `oC1`. |
| `vertex_constant_base`, `pixel_constant_base` | `252` (four previous rows) and `216` (five ABI constants). |
| `light_loop_bound_required`, `light_loop_max_count` | True wherever the VS reads constants relatively: refuse the variant unless `i0.x` is checked in `[0, 8]` at draw time. |

Register choices are derived from the measured free lists, preferring the
reference pair's own indices whenever they are still free, so a row that differs
from `o6 / TEXCOORD4 / v5 / r5 / c216 / c252` differs because the reference
register is genuinely occupied. The offsets and registers are transformer
*input*: the transformer must still revalidate the program it is handed. A row
is not an eligibility decision.

## Programs that cannot host the transformation

| Pairs | Scene draws | Reason |
| --- | ---: | --- |
| `1279d081455f5815`, `6059306306203243` (×2), `cbbf26102694c961` — bloom | 112 | The VS writes `o0`/POSITION0 with `mov`, not four row dots. These are fullscreen/post passes with no world transform; there is no previous WVP to apply and no per-object correspondence. |
| `d5e1c75351ed3f04` + `8360f422de08b5bd` (effects/engine, SM2), `ac2319bc3953efc6` + `03a16e5c63daa6e8` (adeffects, SM2) | 111 | Not SM3. SM2 uses the fixed `oPos`/`oD#`/`oT#` outputs and `v#`/`t#` pixel inputs rather than declared `o#`/`v#` registers, so the linkage is a different declaration model, not a different offset; and the SM2 position quad is not contiguous (`d5e1c75351ed3f04` also issues WXYZ, not XYZW). |
| `5e484a06672e28fb` + `0a523f33ac47ae05` (gui2d/stardust), `36f98d151fd6b0c6` + `222bee0defcb1852` (particles), `be199829a9bb78db` + `cd6d6eb4b3d99443` (planet_haze) | 48 | SM1.1. No SM3 output/input registers, the PS writes `r0` rather than a color-output register, and each PS contains a coissued (`+`) instruction. |

The SM1/SM2 and bloom draws total 271 of 11,493 Scene-segment draws (2.36%).
They keep the sentinel, exactly as the live route already specifies for
particles, stardust and overlays. Nothing here changes that: extending to them
would need a different position contract, not a different offset table.

## Runtime checks this table does not replace

- **Relative light loop.** Every transformable VS reads `c0`, `c1` and `c2` at
  `a0.w` inside a `rep i0` block, with one `mova`. A base register is a
  syntactic reference, not a bound: the JSON records
  `relative_addressing.bounded_by_static_analysis: false`. As the candidate
  review requires, `c252–255` may only be written when the integer count `i0.x`
  is checked in `[0, 8]` at draw time, which bounds the reads to c0–23. Refuse
  the substitution otherwise. The live route applies exactly this bound and
  shadows exactly `c24–27`; `motion_output.cpp` asserts at compile time that
  every row's `matrix_register` and light-loop fields agree with it.
- **Application constants.** Save and restore application `c252–255` (VS) and
  `c216–220` (PS) around the transformed draw; D3DX/effect uploads do not know
  about the extension.
- **Draw state, MRT and history.** Unchanged from the live route: alpha
  test/blend/sRGB off, Z on, COLORWRITEENABLE 15, `D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS`
  and a mixed-format MRT self test, object/camera lifetime and a matched history
  entry. A profile row is evidence about program structure only.
- **Centroid.** Several VS programs declare centroid on some TEXCOORD outputs
  (`53a0a641107ed76c` on three, `b0602757fce6e870` on two, most others on one).
  The new interpolator must not inherit those modifiers, and the new PS
  declaration must not use `_pp` or centroid even where neighbouring
  declarations do.
- **Pair-and-state eligibility.** Aliases are broad: `53a0a641107ed76c` alone
  appears under argon, khaak, split, teladi and xenon effect paths. Eligibility
  stays keyed to the exact pair and draw state, never to a VS alias.

The complete derived table, including per-program declarations, opcode
histograms, free-register lists and the Argon reference check, is in
[`motion-output-profiles.json`](../../verification/results/motion-output-profiles.json).
