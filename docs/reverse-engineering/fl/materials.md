# Farnham's Legacy material shader contracts

Host-only derived study, 2026-10-27. The scope is the **34 new FL ps_3_0
programs**, against the effective AP/FL inventories in
[the shader audit](shaders.md). No production edit, game launch,
Wine run, installation or commit occurred. All addresses below are **original
DWORD offsets**, not executable virtual addresses or transformed offsets.

## Established results and proof boundary

All 34 are hull material programs: **22 conventional and 12 XT**, comprising
14 DEFAULT, 14 BUMPMAP and six BUMPMAP_LOW pairs. None is a new glass or standalone
effect shader. The 30 lost AP hull registrations become 34 FL identities; count
changes must not be mistaken for four non-hull programs. Existing AP/unchanged FL
rows remain necessary. Pair selection uses the same VS, effect basename and
technique, not the audit's heuristic nearest instruction string. For example,
`c17114ca9752ab62` is XT DEFAULT versus AP `fffdabd910793aba`, and
`ba08f36b160527a5` is XT LOW versus AP `6733b119142c8d42`.

The analyzer independently checks complete FNV/SHA-256/length identities, walks
actual instruction boundaries, reads CTAB register names, tracks scalar lane
versions through both IF arms, and compares derived expression hashes. It keeps
comments/preshaders opaque. It then compiles a temporary host driver including
the current `linear_material.cpp` and calls **lightmap_term**, **xt_fill_site** /
**linear_material_fill_sum**, and **original_sun_plan** with independently derived
sites. The driver builds the instruction Structure directly, deliberately
bypassing exact registries, `structure()` and public transform admission. This
proves those internal algorithms accept the native dataflow; it does **not**
prove complete Pixel/XtPixel profiles or emitted shader combinations work.

Measured on this corpus:

- 34/34 satisfy the new pre-affine quality-colour equation below.
- 34/34 prove a distinct 2D lightmap term, a fill location, native sun propagation,
  and sun propagation with the fill sum tracked.
- 34/34 retain the AP output-alpha expression, including sampling coordinates,
  glow interpolation and vertex alpha. No undefined temporary reaches oC0.
- 29/34 have identical normalized directional scalar-expression DAGs to their
  exact AP counterparts. The five remaining differences are explicitly retained
  below; preserving FL's native scalar code is required in every case.
- All 34 specular fetches consume **only red** before their other sampled lanes
  are overwritten. All derived diffuse joins are unconditional RGB MADs,
  making them structural candidates for the existing alpha-carried LOD strength
  twin. This is a proof of the original, not of an enhanced parent variant.
- Original temporaries end at **r6**, float constants at **c29**. No original
  reads/defines c52–c223. Every lightmap fetch is outside native control flow.

The lane DAG preserves saturation, swizzles, source modifiers, scalar/vector
operation semantics and branch merges, but ignores partial-precision hints and
models MAD algebraically. DAG equality is a **dataflow identity**, not bitwise
GPU equivalence or native Windows execution evidence. Sampler dimensionality
limits texture coordinate dependencies to xy or xyz; unused register lanes do
not enter the proof.

## Changed colour law and constant ABI

Let T be encoded diffuse RGB, Q = `g_MatQualityColor.xyz`, and K = GPU c0.xyz.
Using the actual binary32 literals represented by the decimals below:

```
w = saturate(10 * (dot(T, (0.299, 0.587, 0.114)) - 0.45))
M = T * Q
G = 2*M + w * (1 - K*(1-T) - 2*M)
A.x = dot((G,1), c1)
A.y = dot((G,1), c2)
A.z = dot((G,1), c3)
```

This equation is lane-proved for **every** new program, including XT before its
native decal/palette processing. The three final matrix rows can appear in
z/x/y order; the analyzer proves their lane mapping rather than shifting an AP
site by a constant. K's preshader relationship to Q was established on 2026-11-03
(K = 2(1 - Q), see [the grading preshader](#grading-preshader-palette-gate-and-merged-lod-visibility-2026-11-03));
still do not evaluate a newly invented CPU colour formula. Preserve original
comments, GPU operations and uploads.

For a conventional converted material, decode the completed A after the last
matrix row; do not decode T before this encoded-space law. XT conversion decodes
the completed native base (after its decal branch when present), using the
per-row `linear_profile.base_before/base_register` cut. The two notions of
boundary differ: conventional `affine_end` is **after** an instruction, whereas
XT `base_at` is **before** an instruction. Moving a cut across the branch or a
partial xyz matrix result is invalid.

| Role | AP conventional base | FL conventional base | AP XT | FL XT |
| --- | --- | --- | --- | --- |
| First / second directional colour | c5 / c7 | **c6 / c8** | c6 / c8 | **c7 / c9** |
| First / second directional direction | c4 / c6 | **c5 / c7** | c5 / c7 | **c6 / c8** |
| Glow-alpha control | c3 | **c4** | c4 | **c5** |
| Hue matrix rows | c0–c2 | **c1–c3** | c0–c2 | **c1–c3** |
| Palette colours (DEFAULT) | n/a | n/a | c14–c18 | **c16–c20** |
| Palette colours (BUMP/LOW) | n/a | n/a | c17–c21 | **c19–c23** |

Other scalar/DEF registers do not follow one uniform shift. The JSON records
**every CTAB register change**, each colour operand DWORD, each palette use and
all resource sets. Conventional Q varies with family; XT Q is c13. The new Q is
an application colour input, **not** one of the Boron/Paranid immutable palette
literals in `PaletteProgram`. No new Boron/Paranid `PaletteProgram` rows are
needed for this 34-program set. XT's five native palette uses must be updated
with their actual registers, not aliases to AP hashes.

## Grading preshader, palette gate and merged-LOD visibility (2026-11-03)

Derived from the D3DX disassembly (`tools/analysis/disassemble_shaders.cpp` through `C:\X3FL\d3dx9_37.dll`) of the
43 FL and 37 AP `ps_*.bin` in the F8 capture directories, read with the run-27 green-station constants
(`verification/results/fl-lod-overlay/green_station/`). The preshader closes the open K question above.

**Preshader (measured, all 20 FL quality-colour programs).** The effect evaluates the preshader on the CPU and
uploads its results: K = c0.xyz = `2*(1 - g_MatQualityColor.xyz)` (the four captured b2d8f40c draws satisfy
c0 = 2(1 - c13) to the logged precision), and the rows c1–c3 are `Hue x (g_Brightness*g_Contrast*Sat)` with the
fourth column `0.5*(1 - g_Contrast)`. Sat is the Haeberli matrix with luminance weights (0.3086, 0.6094, 0.082) and
`g_Saturation`; Hue is the rotation about the grey axis by `g_Hue` degrees (wrapped to [-180, 180)), circulant rows
`((1-cos)/3 + sin/sqrt3, (1-cos)/3 - sin/sqrt3, cos + (1-cos)/3)`. Every row of Sat and Hue sums to 1, so the matrix
maps grey to grey (scaled by B*C, offset by the fourth column); it amplifies and rotates only chroma. The same
literal set (hue scale, 2*pi, 1/sqrt3, the three luminance weights) is in every graded program of both editions; FL `ps_3_0` hull programs write
c1–c3 + K, AP hull programs and FL `18d37296`/`9d27e7ba` (no `g_MatQualityColor`) write the same matrix to c0–c2.
In XT programs the preshader also writes c4.x = `1 - g_Color_Weighting` (AP: c3.x).

**Stage (measured, b2d8f40c).** `dp4 r1.z/x/y, (G,1), c3/c1/c2` runs immediately after the quality overlay G of the
s0 diffuse texel, before the decal branch (b0, s5 RGB), before lighting, palette and reflection. A therefore scales
the directional lobes and the vertex RGB, and the reflection term (`A * v7.x * g_MatReflectionStrength * spec.r *
cube`); only the s3 light-map RGB (added last) and the alpha (`lerp(diffuse.a, light.a, g_EnableGlow) * v0.w`) are
ungraded.

**Palette gate (measured).** c19–c23 (`g_Color_1/2/3/Lines/Highlight`), c24 (`g_Color_LinesPower`) and c25
(`g_Color_Weighting`) are read only inside `if b1` (`g_bColor_Mixing`): `P = v6.x*c19 + v6.y*c20 + v6.z*c21 +
v7.y*c23 + pow(1 - |V.R|, c24)*c22`, then `A' = A*((1-W) + W*P)`. The weights are not texture masks: VS
`37c34a74` writes v6 = |reflection vector| per axis, v7.y = `(N.V)^g_Color_HighlightPower`; the Lines term is a
band where N.V is near 0.707, thin at power 50. The same gate and expression hold in all six FL XT programs
(`6f745daa`, `b2d8f40c`, `ba08f36b`, `c17114ca`, `e57c48fc`, `f06b6267`) and the six AP XT programs (`496049ce`,
`5f82ecac`, `6733b119`, `e6794b6e`, `f1b0e820`, `fffdabd9`, palette c17–c22). The proxy's per-draw bool snapshot
shows b1 = 0 on every draw of run-27 captures 2 and 3 (44 + 74 draws, any program; `ps_bools.py`), so
the palette registers had no effect on any captured draw.

**Green station arithmetic (measured constants, inferred image).** Source vent draw 48 (LOD 0) and merged draw 16
(LOD 1) of `0x5583` carry the same rows (row sums 1.032, offset -0.001; fit B 1.030, C 1.002, S 7.2, hue -142 deg
= 218 deg) and the node's Q = c13 = (0.955, 0.637, 0.538); source draw 50 carries identity rows and the same Q.
A grey texel g = 0.5 becomes G = Q, then A = (-1.08, 2.15, 0.79) with the vent rows against (0.955, 0.637, 0.538)
with identity; g = 0.2 gives (-0.43, 0.86, 0.32). The green comes from the node's warm Q (chroma) pushed through
the vent's saturation x7.2 and hue 218 deg; a grey texel with neutral Q (0.5) stays grey (x1.03). With the other
node's Q (0.972, 0.96, 0.915, draw 14) the same rows give a cyan-grey (0.65, 0.99, 1.08). `g_MatQualityColor` is a
node value (`0x4e4f7e..5191`, above), identical on the source and merged draws, so the baker controls only the
rows. Script: `colormatrix_math.py` (output `colormatrix_math_out.txt`).

Consequence for merged-LOD keys: a differing colour grade (`g_Brightness/Contrast/Saturation/Hue`) is visible on
every lit texel and in the reflection, except the light-map addition; a differing `g_Color_*` palette is invisible
while `g_bColor_Mixing` is 0 and, when it is 1, visible over the whole material (view-dependent weights), not only
where a light map or emissive mask is non-zero. `g_bColor_Mixing` itself belongs in the key whenever palettes differ.
Not established: whether any shipped FL/AP material sets `g_bColor_Mixing = 1`; the run-27 captures never did.

## Lightmap, lobe sums and sun share

Physical sampler roles survive: DEFAULT s0 diffuse, s1 specular, s2 lightmap,
s3 cube; BUMP/LOW adds s1 normal and moves specular/lightmap/cube to s2/s3/s4.
XT adds its occlusion and detail samples. There are **14 s2** and **20 s3**
lightmap terms. All are declared 2D samples, with native v1 coordinates, and
all distinct final lightmap additions pass the production `lightmap_term`
checker. The checker includes channel lifetime and refusal of intervening
control flow; terraformer admits its one same-register MAD combining the lit
term with the lightmap before the final occlusion-RGB ADD.

Conventional total RGB remains `A * (directional lobes + clamped vertex RGB)
+ tinted reflection + lightmap`. The material-specific powers, gloss gating,
strengths and normal reconstruction remain native. XT uses its executed
palette/base branch for tint, attenuates material plus reflection by the native
occlusion-alpha power, then adds lightmap; terraformer also adds occlusion RGB.
The new quality law changes A, not the identity of the lightmap term.

There are **68 sun seed operands** (two per shader). CTAB plus exact consumer
shape identifies sun by LightDir_Color0, not instruction order. Each native sun
consumer is a MAD with another contribution already in its addend. Its seed is
only the scalar-times-colour product. Keep FL's native diffuse and gloss scalars,
then propagate parallel RGB through MOV/ADD/MUL/MAD, tint and XT occlusion.
Do not seed from secondary light, vertex RGB, reflection, lightmap, palette or
injected fill. The current `original_sun_plan` accepts all 34 original programs,
both with no fill and with the derived fill site/sum; it also checks nonlinear
sun dependency and branch state as described in
[the sun-share contract](../sun-share-material-contract.md).

The probe emits **144–181 DWORDs**, at **9–20 edit positions**, for each standalone
original sun plan. These are measured host-generated plan sizes, **not** final
combined shader lengths, slots or GPU time. The generated words remain ephemeral.
No new texture read or varying is needed by the contribution plan itself.

The machine report contains exact RGB precision destinations (maximum 13 for
conventional and 27 for XT), both XT clamp sites, scalar transport operand
sites, lightmap register lifetimes and fill sites. Full-precision conversion
must retain the new encoded grading law's native precision until its decode
boundary; changing all colour-looking arithmetic to linear math is incorrect.

## Profile and implementation extensions still required

1. **Pixel and pair contracts:** add 22 exact FL rows and the appropriate pair
   bindings. Record the actual `light0/light1`, colour operands, textures,
   clamp, final RGB, RGB destinations and affine end. Extend the fixed validator
   with a **glow register** and **first affine-matrix register** (or equally
   explicit bounded FL layout), since it currently assumes c3 and c0–c2 for all
   affine hulls. Preserve its exact native alpha/lifetime checks. No texture
   register relocation is needed for these conventional tails.
2. **Temporal ABI:** `temporal_temporary_base()` assumes every conventional
   DEFAULT uses r0–r4 and the converted path requires class A. FL DEFAULT
   `0d5848c8489b6630`, `33826e49b53b88dc`, `5fb88b63dd609443`,
   `ced7edef48059e84` use r5 and require base **6 / relocated class B**. Use their
   explicit validated motion plans. BUMP uses original r0–r6/base7. Scratch
   r9/r10 and motion-tail registers can overlap only with the existing sequential
   lifetime proof, not by claiming global disjointness.
3. **XtPixel:** add 12 exact rows with each `base_at/base_reg`, final/alpha,
   texture destination, five palette uses, four light operands, two clamps,
   two scalar uses and RGB sites. The existing fixed-size arrays fit (26 or 27
   RGB sites). Add explicit light0/light1 and palette-register origin/roles;
   `xt_pixel_sites`, light decode, `u.value==6` sun selection, original-fill light
   selection and the converted share seed path currently hardcode AP's c6/c8.
   DEFAULT reflection policy also hardcodes c11, while FL uses c12. Prefer the
   explicit CTAB-derived reflection scalar role. FL terraformer DEFAULT can
   complete its affine base in **r1**, so do not inherit AP's r4 base blindly.
4. **PaletteProgram:** no extension for the immutable Boron/Paranid family;
   XT palette records and application Q must remain distinct. Existing palette
   linearization can use the recorded five FL inputs after the ABI corrections.
5. **Strength / selective exposure:** add **34 ExposureSeed rows**. The report's
   `diffuse_join.at/lanes/product` specifies the diffuse MAD term; Terran's four
   changed rows use `product=false`, the other 30 use `true`. Add the exact
   material-strength stage lookups. Prove the enhanced parent still contains one
   exact join and one native specular fetch, retains unused alpha, remains outside
   flow control at both sites and has a spare temporary before emitting a twin.
   The current transformation allocates one extra temporary and adds nine DWORDs.
6. **Bounds:** 18/22 conventional programs exceed the converted/fade `transform`
   **1392-DWORD** guard. Eight XT programs exceed the **1791-DWORD** dispatcher,
   XT DEFAULT wrapper, original-fill/share and strength limits. Maximum input is
   **1911 DWORDs**. Raising only one guard or only adding rows is insufficient.
   Inspect every dispatcher, complete transformed structure/slot cap, caller
   bytecode retrieval capacity and combined growth; vector `reserve()` is not a
   correctness cap. The shader slot minimum of 512 is not an admission limit.
7. **Other active routes:** HDR additive hull gain has its own exact
   `linear_emission.cpp::HullProfile` set for six standard and six XT programs.
   The material rows above do not extend it. `first_definition` and `final_rgb`
   in this report are useful candidate offsets, but its `hull_structure` /
   `hull_tail`, transformed gain/clamp and runtime ONE/ONE admission still require
   their own validation. Pixel-coverage and shadow-replay population/lookup gates
   are also separate. These gaps belong in the full FL feature matrix, including
   default-off linear material/fade routes; this note makes no fog or engine-hook
   compatibility claim.

The scalar-DAG comparison differs for `67f738c3b080164a`, `cca466f41a0de457`,
`db375f72fe3cdf5e`, `df146f0ed69e1acf`, and `e57c48fc06198e20`. Their FL sun paths,
lightmap terms and alpha were independently proved; equality of their entire
normal/gloss scalar laws to AP remains unestablished. Do not transplant AP
angular instructions or use these hashes as semantic aliases. An exact match
for the other 29 likewise does not justify aliasing their changed albedo law.

## Distance-fade pair and stage locations

The missing pair is VS **4944d81dfe531b37** / FL PS **740346590fe7bff5**, replacing
AP PS **64bac8bb307eb896**. The 556-DWORD VS is byte-identical in AP/FL. Its native
alpha path uses b0 at 510, distance length at 512/516/519, the c41.xy distance
MAD at **522**, COLOR0.w multiply by c39.x at **527**, ELSE at 531, unfaded
c39.x MOV at **532**, ENDIF at 535. FL has not moved these VS sites.

The 1518-DWORD FL PS sites are:

| Role | DWORD / register |
| --- | --- |
| Normal / specular / cube / diffuse fetch | 1234 / 1381 / 1414 / 1418 |
| Sun diffuse / gloss colour operand | 1374 / 1379 (c6) |
| Diffuse/specular join | 1397, r5.xyz, diffuse in product |
| Vertex RGB clamp | 1406, r6.xyz |
| Completed graded albedo | 1475, r4.xyz, matrix c1–c3 |
| Final pre-albedo lobe-sum use / fill | 1495, r1.xyz |
| Lightmap fetch | **1500**, s3 into r0.xyzw |
| Glow alpha LRP | **1504**, c4.x, lightmap r0.w, diffuse r1.w -> r2.w |
| Final RGB | **1509**, oC0.xyz |
| Final alpha | **1513**, r2.w * v0.w -> oC0.w |

The native PS alpha equals AP's expression for all inputs:
`(glow * lightmap.a + (1-glow) * diffuse.a) * vertex_alpha`.
Diffuse alpha survives its RGB grading; lightmap alpha survives the final RGB
addition. There is no new TEXKILL or depth write. This supplies shader-stage
proof for extending `station_fade_ps`, `distance_fade_rows` and the material
profile, but the 1392 guard, dual replay, detached outputs, sRGB states, rollback,
combined fill and resource bounds still need host/GPU acceptance. Engine draw
state eligibility and actual FL source-over use were not observed.

## Per-program sites

`P` is the conventional Pixel family; `X` is XtPixel. `J` is the diffuse join,
`L` the lightmap fetch and `F` the final RGB instruction. All 34 require their
own exact row and the ABI changes above. Detailed operand positions, registers,
branches, constants and explicit blockers are in the derived JSON; this table is
a compact witness, not sufficient input for an unvalidated table generator.

| FL PS | AP counterpart | Kind / technique | Words | J / L / F |
| --- | --- | --- | ---: | --- |
| `0d5848c8489b6630` | `e70adc744a38ca59` | P / DEFAULT | 1450 | 1387 / 1432 / 1441 |
| `1455079b3f18aa88` | `63f96eba9eea7880` | P / DEFAULT | 1412 | 1312 / 1394 / 1403 |
| `16d4af171035acd1` | `ca6bfa4a6cca7e2a` | P / BUMPMAP | 1448 | 1331 / 1430 / 1439 |
| `1835f5e1ba07b1aa` | `8759c7838bbc86c2` | P / DEFAULT | 1386 | 1286 / 1368 / 1377 |
| `1886829a571cf2db` | `ef2bf556f207b8bd` | P / DEFAULT | 1376 | 1276 / 1358 / 1367 |
| `1b54cbfa2155b61c` | `0c1f3f0f440e4a0c` | P / BUMPMAP | 1486 | 1365 / 1468 / 1477 |
| `20c3851a4cac4213` | `1f26d41bcb7dac1e` | P / BUMPMAP | 1452 | 1331 / 1434 / 1443 |
| `2a7f1c93d4fd081c` | `8e58ac79b59b02b1` | P / BUMPMAP | 1476 | 1359 / 1458 / 1467 |
| `33826e49b53b88dc` | `3b94320087e81945` | P / DEFAULT | 1390 | 1327 / 1372 / 1381 |
| `3c39d45636d95744` | `99153c144030c396` | P / BUMPMAP_LOW | 1475 | 1354 / 1457 / 1466 |
| `3ff64d33ff2782ab` | `3006f8030a467739` | P / BUMPMAP | 1432 | 1315 / 1414 / 1423 |
| `5831fb1d488e089e` | `91b6c09eb47f8555` | P / DEFAULT | 1408 | 1308 / 1390 / 1399 |
| `5fb88b63dd609443` | `e3b7acc16da9932d` | P / DEFAULT | 1416 | 1353 / 1398 / 1407 |
| `67f738c3b080164a` | `bdcdb3ab996ae4e0` | P / BUMPMAP | 1484 | 1363 / 1466 / 1475 |
| `6f745daaf928a297` | `496049cec2066ed3` | X / BUMPMAP_LOW | 1900 | 1771 / 1881 / 1890 |
| `740346590fe7bff5` | `64bac8bb307eb896` | P / BUMPMAP | 1518 | 1397 / 1500 / 1509 |
| `7d11d532dedaf1d9` | `d22f2ce2c740e6a7` | X / BUMPMAP | 1804 | 1602 / 1781 / 1795 |
| `87ccd863f19254b2` | `fd58e6b7e8cf969c` | X / DEFAULT | 1681 | 1544 / 1658 / 1672 |
| `96238a8b2cee4f6e` | `d6e8bdde0e4c515f` | P / BUMPMAP | 1464 | 1347 / 1446 / 1455 |
| `9db5eebae86ce142` | `75fb9c6b05e28ea2` | X / BUMPMAP_LOW | 1793 | 1591 / 1770 / 1784 |
| `ac429ac9cd658e64` | `3602b05ce11ca6ff` | P / BUMPMAP | 1444 | 1327 / 1426 / 1435 |
| `b2d8f40cc7a03904` | `5f82ecacd39529cd` | X / BUMPMAP | 1879 | 1750 / 1860 / 1869 |
| `b5af46aebb231ba2` | `462342e3e5781384` | P / DEFAULT | 1370 | 1270 / 1352 / 1361 |
| `ba08f36b160527a5` | `6733b119142c8d42` | X / BUMPMAP_LOW | 1868 | 1739 / 1849 / 1858 |
| `c17114ca9752ab62` | `fffdabd910793aba` | X / DEFAULT | 1756 | 1627 / 1737 / 1746 |
| `c79c3b2c6749df10` | `5e0a10fe752b6140` | P / BUMPMAP | 1480 | 1363 / 1462 / 1471 |
| `cca466f41a0de457` | `dd87737d697c6764` | X / DEFAULT | 1713 | 1568 / 1690 / 1704 |
| `ced7edef48059e84` | `7c83ed50c9894e44` | P / DEFAULT | 1418 | 1355 / 1400 / 1409 |
| `db375f72fe3cdf5e` | `827d8d2d617bedce` | P / DEFAULT | 1396 | 1296 / 1378 / 1387 |
| `df146f0ed69e1acf` | `edaef099780fcafe` | X / BUMPMAP_LOW | 1825 | 1627 / 1802 / 1816 |
| `e326b2b1363c9886` | `c1452981fd0bff64` | P / BUMPMAP_LOW | 1507 | 1386 / 1489 / 1498 |
| `e57c48fc06198e20` | `f1b0e820c7b488c3` | X / BUMPMAP | 1911 | 1782 / 1892 / 1901 |
| `f06b626708db3c7e` | `e6794b6ec37ff71a` | X / DEFAULT | 1788 | 1659 / 1769 / 1778 |
| `f083a2be99470f83` | `1de3d2dde345a7e3` | X / BUMPMAP | 1836 | 1638 / 1813 / 1827 |

## Lifetime, boundaries and performance

The bytecode changes contemplated here are shader-local. Native temporary lanes
remain live until their actual overwrite; the specular red-only proof and
lightmap-term proof scan those intervals, and the sun planner processes edits
before each original destination write. Branch union and definite initialization
are required for XT; an unconditional post-branch tail does not justify hoisting
its tint or palette work. All recorded insertion/cut positions are checked
instruction boundaries. Invocation-local registers have no CPU calling
convention, flags, LastError, reentrancy or host-shared lifetime. This shader
study establishes **no** engine hook boundary or host exception contract.

Do all identity, CTAB/SSA and profile derivation offline; production should use
immutable reviewed fields and bounded create-time validation. Do not add this
Python/DAG analysis to a per-draw path. Existing per-draw lookup/constant policy
can remain bounded. Production transform allocation failures must retain their
current rollback/false-applied behavior; generated shader state must be swapped
only after complete validation. No benchmark or GPU timing was performed.

The next implementation needs scoped corpus tests of gained/ungained, original
and converted, depth/fill/share, lightmap widening, LOD strength and faded dual
variants, mutation rejection and unchanged AP results, followed by D3D creation
and execution qualification. Original shader register freedom does not prove
combined motion/depth/engine-light/strength scratch freedom. Native Windows and
CrossOver remain separate runtime validation targets.

## Reproduction and evidence

```sh
python3 tools/analysis/inspect_fl_materials.py
python3 verification/results/fl-port/materials/validate.py
```

The script reads local raw programs from `/tmp/x3-fl-port-shaders/{ap,fl}` and
exact identities from `verification/results/fl-port/shaders/{ap,fl}.json` and
`new-program-analysis.json`. Output:
[contracts.json](../../../verification/results/fl-port/materials/contracts.json).
Its producing-script copy and validation script live beside it. JSON contains
hashes, roles, proof counts and offsets only. Raw shader bytes, the temporary
compiled C++ probe and generated edit words are never written to tracked paths.
The source contains only authored analysis/probe code, not original instruction
streams. Three deliberate semantic mutations are refused: changed grading
literal, final lightmap ADD changed to MUL, and lightmap sampler changed to cube.
The report records the host compiler flags and direct analysis/C++ include
provenance hashes, including the concurrently reviewed motion-profile extension.

Recorded host run: analyzer **2.84 s, exit 0**, including compilation and all 34
C++ invocations; no Wine lock wait. The JSON validator passed 34 rows and rejected
four deliberately corrupted reports (count, sun constant, alpha claim and cut
boundary). Local command output is `verification/results/fl-port/materials/check.log`.
The results directory follows the repository's existing ignored `fl-port/**`
policy; only derived data/helpers are present there, ready for selective retention.

### Affine-proof review correction

The independent review found that the initial grading check proved G at the
first DP4 and checked matrix row/lane identities, but did not prove each later
DP4's complete result or common destination. Changes to a later matrix-source
swizzle, destination saturation or destination register could therefore escape
that check. The 34 original equations were independently confirmed correct;
the defect was incomplete rejection of modified inputs.

`grading()` now checks **all 102 affine row results** against the expected
four-component dot products, the live graded source SSA and coefficient SSA,
identity swizzles, unmodified/nonrelative sources, common temporary destination,
exact lane mapping and destination precision/saturation/shift. Each row's result
hash is retained in the report and recomputed by the validator. Both later rows
have seven isolated mutation witnesses: matrix swizzle, destination saturation,
destination register, graded-source swizzle, graded-source modifier, matrix
modifier and destination shift. All **14** are refused, alongside the original
three semantic mutations (**17 total**).

Review-fix run on the checkout containing `b4b2db83`: analyzer **2.76 s, exit 0**;
34 report rows validated and **five** corrupted reports refused, including a
changed affine-result hash. AP alpha matches remain **34/34** and scalar-lobe
matches **29/34**. Commands are unchanged; output is in
`verification/results/fl-port/materials/review-fix-check.log`. Public admission,
GPU rounding and runtime limitations above remain unchanged. No production or
GPU work was performed for this correction.
