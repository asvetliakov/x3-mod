# Boron and Paranid SM3 material conversion contract

Read-only derivation, 2026-09-13, research worktree from `e34a5b2`. This covers the complete **32 pairs / 12 VS / 20 PS / 96 archive pass occurrences**: Boron 12 pairs/48 occurrences and Paranid 20/48. No implementation or GPU acceptance is claimed. Local inputs are `/tmp/x3-shader-sweep/programs`, matching NUL-stripped `disassembly`, and `verification/results/motion-output-profiles.json`; bounded decoded queries remain under `/tmp/x3-boron-paranid-study-data`. No original game payload is included here.

## Complete identity mapping

Each comma-separated VS set pairs with **each** PS in its row; PS order is front then two-sided. `L` is the native bounded point-light loop, `F` fixed single point light. “Single” below describes directional count, not point-loop count. Boron has no affine-hue PS variant. Paranid affine rows retain the complete preshader-derived three-row transform; nonaffine rows omit it.

| Family/technique/shape | VS | PS front, two-sided | Pairs |
| --- | --- | --- | ---: |
| Boron DEFAULT base, two directional | `29d7c575396ed280` L | `39eb3c2258a516e1`, `57acf59d19c73791` | 2 |
| Boron DEFAULT single | `a420a010b0271479` L, `ea3d15b287892410` F | `f917d48ee826da1f`, `77a5b2d62fb3be48` | 4 |
| Boron BUMPMAP base, two directional | `57392213f62fef19` L | `a910daef935891ce`, `62c180abe017e239` | 2 |
| Boron BUMPMAP single | `5c17a381b149b3b9` L, `a804f173f693944a` F | `ed44232013f67072`, `f286856c3f400377` | 4 |
| Paranid DEFAULT base, affine | `37e6956afd8b8d76` L | `9d27e7ba242f3831`, `e1acf8a03850acaf` | 2 |
| Paranid DEFAULT single, affine | `2e0254dd999841c2` L, `a7cddf2c98d61117` F | `f646f03be5a8708d`, `ebf41e1ace7af45b` | 4 |
| Paranid DEFAULT single, nonaffine | same preceding two VS | `c997a37560e266df`, `675f9077d8fd21c4` | 4 |
| Paranid BUMPMAP base, affine | `33388c8897d428a5` L | `18d372968af4a480`, `188c5ab9dbb98393` | 2 |
| Paranid BUMPMAP single, affine | `b4059ab6af8fc529` L, `2a560f246c90fa64` F | `7e5e41276b3d7514`, `43c9405568d2226f` | 4 |
| Paranid BUMPMAP single, nonaffine | same preceding two VS | `5e056627e9ff3a8d`, `fce465befff2f623` | 4 |

Archive aliases are `boron`, `boron2s`, `boron_0000`, `boron_0001`, and the corresponding four `paranid` names, across `01.cat`/`addon/01.cat` and `(base)`, `hue_lights_off`, `hueshift_off`, `v_lights_off`. The table is an exact program/pair contract; neither basename nor toggle name substitutes for original fingerprint validation. Paranid base and affine-single PS executable schedules match, with differing declaration centroid metadata; retain every original declaration flag.

## Mathematics and conversion requirements

The equations below specify the new color interpretation of the observed native schedule; they are **not** evidence that native constants/textures were scene-linear. `C(x)` means the already selected safe gamma-2.2 color decode, applied before color-weight accumulation. Existing direct/material-emissive/lightmap gains and final compatibility encode remain the policy in [scene-linear materials](../architecture/scene-linear-materials.md). Do not decode scalar masks, weights, exponent, alpha, geometry, or the already-scaled material-emissive constant.

VS geometry is materially different from simply reusing an old hull reference: world geometric normal `Ng` is not normalized in VS; view `V=normalize(camera-worldPosition)` **is** normalized before interpolation. Both families construct `Rg=2*dot(V,Ng)*Ng-V`, `u=sat(dot(V,Ng))`, and `w=abs(Rg)`. Boron supplies `J=2*((1-u)^1.2+0.1)` and `u^11`; Paranid supplies `J=(1-u)^1.2+0.1`. Preserve original POW versus LOG/multiply/EXP schedules and float32 DEF values (`1.2000000476837158`, `0.10000000149011612`), including operand ABS semantics. These are native artistic weights, not a new physical Fresnel model.

VS point lighting retains native attenuation/angular accumulation and loop/fixed behavior; decode each point RGB **before** its original multiply/accumulate, then add scaled material-emissive RGB with its separate gain. Loop uses relative `c1[a0.w]` and material `c40`; fixed uses `c5` and `c19`. Fog alpha remains native loop `c39.x` / fixed `c18.x`, including both branches. Decode of a summed COLOR0 is insufficient.

PS DEFAULT normalizes interpolated geometric N and V but uses interpolated `Rg` directly for cube direction/palette angle. BUMP samples AG from `s1`: A maps to binormal, G to tangent, reconstructed Z uses native RSQ/RCP of `q=1-x²-y²` (finite nonzero domain gives `sqrt(abs(q))`), then normalizes the mixed vector. Its reflection `R=2*dot(V,N)*N-V` uses bumped N and normalized interpolated V. Two-sided variants apply their original face sign once. **VS palette weights/J remain geometric and are not recomputed from bumped N.** Preserve zero/near-zero/degenerate native geometry instructions; analytical equivalence excludes undefined normals/views and separately qualifies those paths on GPU.

Directional response per light is `C(lightRGB) * (d*sat(N·L) + k*mask*sat(3*sat(N·L))*pow(sat(reflect(-L,N)·V),10))`, in original instruction/accumulation order. Boron has `d=0.4000000059604645`, `k=3`, and two directions only in base PS; Paranid has `d=0.5`, `k=6`, **one direction in every PS**, including base. Boron constants are glow `c0.x`, direction/color `c1/c2`, and base second `c3/c4`. Paranid affine uses hue rows `c0..2`, glow `c3.x`, direction/color `c4/c5`; nonaffine uses glow `c0.x`, direction/color `c1/c2`. These fixed lobes are not existing shared-family `.5/pow6/cube.5` aliases.

Let `D=C(diffuseRGB)` (after Paranid affine completion where present). Boron palette is `P=w.x*C(Px)+w.y*C(Py)+w.z*C(Pz)+u^11*C(Pu)+F*C(Pf)`, with `F=(1-abs(dot(normalize(interpolatedV),R)))^5`; native fifth power is multiply/square/multiply. Paranid omits `Pu`, uses power **9** through native POW for `F`, and otherwise the same weighted palette. There is no SAT on this grazing operand; do not introduce a clamp or assume unit interpolated R. Both use effective albedo `A=.5*D+.5*(D*P)`.

Boron reflection is `C(cube(R))*mask*D*J*C(Pc)`; Paranid reflection omits `Pc`. Neither has an additional reflection factor `.5`. Final linear RGB is `A*(VSpointAndMaterial + directionalResponse) + reflection + C(lightmapRGB)*lightmapGain`, followed by existing safe compatibility encoding. Original alpha is `(glow*lightmap.a+(1-glow)*diffuse.a)*vertexAlpha`; affine RGB and every palette/light gain must leave it untouched. No new application lobe-strength inputs are present.

**Boron single variants already mix palette RGB in VS**, unlike Boron base/Paranid which interpolate scalar weights. Decode `Px/Py/Pz/Pu` there before the original weighted accumulation; preserve full RGB precision through its existing palette varying (DEFAULT `o6/v5`, BUMP `o7/v6`), independently of alpha and scalar J. Decoding the interpolated palette sum in PS would be incorrect.

## Palette constants and exact boundaries

These numerator triples identify native DEF colors (divide by 255, then use the actual float32 DEF words as authority): Boron `Px=(38,111,117)`, `Py=(190,103,25)`, `Pz=(136,141,117)`, `Pu=(81,253,240)`, `Pf=(151,187,74)`, `Pc=(112,164,183)`; Paranid `Px=(137,151,177)`, `Py=(73,97,103)`, `Pz=(110,59,25)`, `Pf=(104,128,164)`. Some constants share a vector with native scalar exponents/factors: decode only the selected three lanes into reserved constants/temporaries, not the whole vector. Immutable decoded palette constants are possible; reference/GPU comparison must use the same float32 source values and declared transfer accuracy.

| Palette use | Px, Py, Pz, Pu, Pf, Pc bindings (omitted roles absent) |
| --- | --- |
| Boron DEFAULT base front | `c6.xyz,c7.xyz,c8.xyz,c10.xyz,c11.xyz,c9.xyz` |
| Boron DEFAULT base face | `c7.xyz,c8.xyz,c9.xyz,c11.xyz,c12.xyz,c10.xyz` |
| Boron BUMP base both | same as preceding row |
| Boron single loop VS | `c43.yzw,c44.xyz,c45.xyz,c46.xyz` (first four roles) |
| Boron single fixed VS | `c21.xyz,c22.xyz,c23.xyz,c25.xyz` (first four roles) |
| Boron DEFAULT single front / face PS | `Pf/Pc=c5.xyz/c4.xyz` / `c6.xyz/c5.xyz` |
| Boron BUMP single both PS | `Pf/Pc=c6.xyz/c5.xyz` |
| Paranid DEFAULT affine both | `Px/Py/Pz/Pf=c6.xyz/c7.xyz/c8.xyz/c10.xyz` |
| Paranid DEFAULT nonaffine both | `c4.xyz/c5.xyz/c6.xyz/c8.xyz` |
| Paranid BUMP affine both | `c6.xyz/c7.yzw/c8.xyz/c10.xyz` |
| Paranid BUMP nonaffine front / face | `c4.xyz/c5.xyz/c6.xyz/c8.xyz` / `c3.xyz/c4.yzw/c5.xyz/c8.xyz` |

Boron VS palette source-use instruction DWORDs (Py,Px,Pz,Pu order): `a420…:566,570,579,588`; `ea3d…:521,525,534,543`; `5c17…:568,576,581,591`; `a804…:520,528,533,543`. Scalar reads of the same DEF vector remain byte-preserved.

DEFAULT samplers: `s0` diffuse, `s1` specular mask X, `s2` lightmap, `s3` cube. BUMP: `s0` diffuse, `s1` normal AG, `s2` mask X, `s3` lightmap, `s4` cube. Admission masks remain **0x0f / 0x1f**, with known-false sRGB only for required stages. The table gives original insertion boundaries **after** completed D/cube/lightmap RGB production; clamp/alpha are instruction starts. Decode texture RGB only, retain alpha and mask/data sampling behavior. Remove PP only from derived linear RGB arithmetic and palette transfer; mixed RGB/data destinations require a channel proof.

| PS | D / cube / lightmap decode boundary | COLOR0 RGB clamp / final RGB / alpha | Original DWORDs / weighted slots |
| --- | --- | --- | --- |
| `188c5ab9dbb98393` | 1299 / 1251 / 1333 | 1230 / 1338 / 1342 | 1347 / 69 |
| `18d372968af4a480` | 1273 / 1225 / 1307 | 1204 / 1312 / 1316 | 1321 / 64 |
| `39eb3c2258a516e1` | 376 / 405 / 418 | 325 / 423 / 427 | 432 / 64 |
| `43c9405568d2226f` | 1299 / 1251 / 1333 | 1230 / 1338 / 1342 | 1347 / 69 |
| `57acf59d19c73791` | 408 / 437 / 450 | 357 / 455 / 459 | 464 / 69 |
| `5e056627e9ff3a8d` | 354 / 323 / 388 | 302 / 393 / 397 | 402 / 60 |
| `62c180abe017e239` | 474 / 430 / 512 | 410 / 517 / 521 | 526 / 81 |
| `675f9077d8fd21c4` | 314 / 339 / 352 | 275 / 357 / 361 | 366 / 53 |
| `77a5b2d62fb3be48` | 296 / 325 / 338 | 275 / 343 / 347 | 352 / 51 |
| `7e5e41276b3d7514` | 1273 / 1225 / 1307 | 1204 / 1312 / 1316 | 1321 / 64 |
| `9d27e7ba242f3831` | 1202 / 1232 / 1245 | 1151 / 1250 / 1254 | 1259 / 52 |
| `a910daef935891ce` | 448 / 404 / 486 | 384 / 491 / 495 | 500 / 76 |
| `c997a37560e266df` | 288 / 313 / 326 | 249 / 331 / 335 | 340 / 48 |
| `e1acf8a03850acaf` | 1228 / 1258 / 1271 | 1177 / 1276 / 1280 | 1285 / 57 |
| `ebf41e1ace7af45b` | 1228 / 1258 / 1271 | 1177 / 1276 / 1280 | 1285 / 57 |
| `ed44232013f67072` | 336 / 311 / 374 | 290 / 379 / 383 | 388 / 58 |
| `f286856c3f400377` | 362 / 337 / 400 | 316 / 405 / 409 | 414 / 63 |
| `f646f03be5a8708d` | 1202 / 1232 / 1245 | 1151 / 1250 / 1254 | 1259 / 52 |
| `f917d48ee826da1f` | 264 / 293 / 306 | 243 / 311 / 315 | 320 / 46 |
| `fce465befff2f623` | 380 / 349 / 414 | 328 / 419 / 423 | 428 / 65 |

All 20 PS have exactly one RGB-only COLOR0 saturated PP move and one final independent COLOR0.w alpha read; no other COLOR0 read, TEXKILL, oDepth or secondary color output occurs. All 12 VS have one COLOR0.xyz writer and two mutually exclusive alpha writers. Exact per-VS sites follow; new RGB routing must preserve the alpha sites and fog control flow.

| VS | Point-color multiply / material+point RGB / fog alpha / plain alpha | Original DWORDs / weighted slots |
| --- | --- | --- |
| `29d7c575396ed280` | 440 / 455 / 512 / 517 | 577 / 72 |
| `2a560f246c90fa64` | 404 / 412 / 488 / 493 | 552 / 64 |
| `2e0254dd999841c2` | 440 / 455 / 512 / 517 | 563 / 68 |
| `33388c8897d428a5` | 449 / 464 / 539 / 544 | 603 / 75 |
| `37e6956afd8b8d76` | 440 / 455 / 512 / 517 | 563 / 68 |
| `57392213f62fef19` | 449 / 464 / 539 / 544 | 617 / 79 |
| `5c17a381b149b3b9` | 487 / 514 / 539 / 544 | 648 / 83 |
| `a420a010b0271479` | 458 / 473 / 530 / 535 | 605 / 75 |
| `a7cddf2c98d61117` | 395 / 403 / 461 / 466 | 512 / 57 |
| `a804f173f693944a` | 452 / 465 / 491 / 496 | 600 / 71 |
| `b4059ab6af8fc529` | 449 / 464 / 539 / 544 | 603 / 75 |
| `ea3d15b287892410` | 419 / 427 / 485 / 490 | 560 / 64 |

## Linkage, limits, and unresolved acceptance

Existing same-draw motion/depth ABI must remain intact: both DEFAULT families use motion `o8/v7 TEX6`, depth `o9/v8 TEX7`; Boron BUMP uses motion `o9/v8 TEX7`, depth `o10/v9 TEX8`; Paranid BUMP uses motion `o9/v8 TEX5`, depth `o10/v9 TEX8`. DEFAULT therefore has one spare whole PS input `v9` and VS output `o10`; BUMP has no spare whole PS input. Original constants stop at VS c46 / PS c12 and temporaries at VS r6 / PS r5. Existing reserved transfer constants VS c248–249, PS c212–213, motion VS c252–255/PS c216, and dedicated RGB scratch registers need explicit combined-plan noncollision proof; none is an original-data collision here. Additional immutable palette DEFs can use an explicitly bounded reserved range, not a general allocator.

The budget table counts **documented weighted static slots**, not disassembler “approximately” counts or loop-expanded execution: NRM/POW=3, LRP/DP2ADD=2, cube TEXLD=4, ordinary 2D TEXLD=1, ABS/LOG/EXP=1, and native flow-control costs. Original PS range 46–81 and VS 57–83, safely below 512; final transformed limits remain an implementation proof gate, including all conversion/temporal instructions. Large Paranid byte counts include metadata, not a 1000-slot GPU body. See Microsoft's [PS3 instruction costs](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx9-graphics-reference-asm-ps-instructions-ps-3-0).

The previous candidate split COLOR0.w PP and COLOR1.xyz full precision into the same physical register. Microsoft permits [disjoint semantic declarations](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dcl-usage---ps), and [PS3 inputs](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx9-graphics-reference-asm-ps-registers-ps-3-0) do not impose the old color clamp. **It is not qualified:** root reports the first X3 synthetic case produced NaN alpha, despite all 28 shader creations succeeding; same-register TEX9 alternatives failed similarly, while the separate-register reference was correct. Fixture/driver diagnosis remains owned by that qualification task.

A whole-register fallback candidate avoids mixed DCL: DEFAULT can use spare `o10/v9` as COLOR1 RGB, leaving original COLOR0 alpha entirely intact. For BUMP, move native scalar `J` (and Boron-base `u^11`) from `o8.x/y` to unused W lanes of view `o3` and geometric normal `o4`, redirect the corresponding `v7.x/y` reads to `v2.w/v3.w`, extend those existing single TEX1/TEX2 declarations to XYZW, and reuse vacated `o8/v7` as whole COLOR1 RGB. In all six BUMP VS/ten PS, these carriers are noncentroid and PS PP, matching the old scalar carrier. This preserves computed scalar values rather than reconstructing nonlinear values after interpolation. It still changes semantic wrap-component assignments: require the relevant old/new WRAP components zero, prove exact producer/read swizzles and lane isolation, and qualify interpolation/alpha/GOURAUD/FLAT behavior. It is a candidate, not authorization to weaken alpha precision or silently reroute motion/depth.

## Bounded implementation and proof gates

Implement all 32 exact pairs together after transport selection; retain old 110 contracts unchanged. Reuse the existing original-bytecode combined transform and exact guards, with family-specific palette/geometry schedules; do not feed a separately patched shader into original-fingerprint motion validation. Independent reference needs Boron/Paranid palette terms, original normalized-V interpolation, geometric versus bumped reflection separation, exponent 5/9 grazing terms, 0.4/3 versus 0.5/6 directional discriminators, and original alpha. Test each palette channel with asymmetric weights, mixed DEF scalar lanes, nonunit/nonorthogonal geometry, affine/no-affine, face signs, zero/one/eight/fixed point lights, HDR gain and finite-domain endpoints. Add direction-dependent cube samples; constant cubes cannot detect a reflection error.

Pure structural proof must cover every source/consumer conversion and RGB PP removal, preserve scalar/geometry/alpha/control-flow operands, and certify transformed instruction/register limits. The detached GPU batch must then cover all pairs, transport gradients/subpixel edges and temporal depth/alpha, source-failure/native fallback, and separate undefined-geometry qualification. No new scene-wide lighting or native-Windows runtime claim follows from local disassembly or X3 alone. No new gameplay capture is needed to derive these contracts; actual driver transport acceptance is the decisive pending evidence.
