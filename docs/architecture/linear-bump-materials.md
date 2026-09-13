# Linear Argon BUMPMAP material slice

Design study, 2026-09-13, after the installed twenty-pair DEFAULT slice.
**Implemented, reviewed, GPU/live qualified and installed; gameplay pending.** Extend the existing opaque
material route to the complete Argon SM3 BUMPMAP contract. Evaluate color in
linear light, then compatibility-encode into the current FP16 engine-space
target. Retain alpha, geometric lighting response and same-draw motion/depth.
This adds material coverage; it does not establish linear transparency
composition, new lighting physics or whole-scene linear rendering.

## Complete bounded inventory

The archive-derived [motion inventory](../../verification/results/motion-output-profiles.json)
contains **ten pairs, three VS, six PS and 24 P0 pass occurrences**, all SM3
BUMPMAP. All nine small local instruction bodies were inspected. Coverage is
derived from the archive, not restricted to captured hashes.

| VS identity (DWORDs) | Allowed PS identities |
| --- | --- |
| `4944d81dfe531b37` (556), loop | `ca6bfa4a6cca7e2a`, `5e0a10fe752b6140` |
| `19a246a56e9d9700` (511), fixed single point | Each of the four remaining PS below |
| `44c4a41ca92ae2e3` (556), loop | Each of the four remaining PS below |

| PS identity | DWORDs | Archive basename | Directional lights | Affine color | Two-sided | D3DX disassembly's approximate instruction slots |
| --- | ---: | --- | ---: | :-: | :-: | ---: |
| `ca6bfa4a6cca7e2a` | 1328 | argon | 2 | yes | no | 61 |
| `5e0a10fe752b6140` | 1354 | argon2s | 2 | yes | yes | 66 |
| `63379470db8d2a86` | 1251 | argon_0000 | 1 | yes | no | 47 |
| `68915563dd0aac9a` | 332 | argon_0000 | 1 | no | no | 43 |
| `d086fde54698070c` | 1277 | argon_0001 | 1 | yes | yes | 52 |
| `f17fffd88d134b04` | 358 | argon_0001 | 1 | no | yes | 48 |

The last column transcribes the local D3DX disassembly footer's approximate
instruction-slot count, including five texture slots. It is not an independently
computed weighted SM3 budget; transformed programs require that separate check.

Base argon/argon2s each appear eight times across root/addon catalogues and
four toggle directories. Each of the other eight pairings appears once:
fixed-point VS uses `v_lights_off` for affine and `hue_lights_off` for non-affine;
loop VS uses base for affine and `hueshift_off` for non-affine. Directory names
do not override the actual lighting instructions. Historical iteration-05
coverage is 1,896 scene draws: 1,440 base and 456 two-sided, of 11,493 total.
This is not current pixel coverage or a performance measurement.

## Sample and normal contract

All six PS use the same five resources. Require known disabled hardware sRGB
sampling for **s0–s4**, including the data samplers: sRGB conversion would alter
normal green and specular red. Use a per-family cached admission mask `0x1f`;
the existing DEFAULT mask remains `0x0f`, with irrelevant s4 state ignored.

| Sampler | Meaning | Treatment |
| --- | --- | --- |
| s0, 2D | Diffuse RGB; diffuse alpha | Affine RGB transform where present, then decode; retain sampled alpha |
| s1, 2D | DXT5nm-style normal in alpha/green | Data only; retain sampling, reconstruction and partial precision |
| s2, 2D | Red scalar specular/reflection mask | Data only; no color decode |
| s3, 2D | Additive lightmap RGB; glow alpha | Decode RGB and apply separate linear gain; retain alpha |
| s4, cube | Reflected environment RGB | Decode RGB before diffuse tint and scalar mask multiplication |

The actual VS declarations establish input v3 as BINORMAL and v4 as TANGENT.
Their transformed outputs are TEXCOORD4 and TEXCOORD3 respectively. With
interpolated transformed tangent T, binormal B and geometric normal Ng, all
six PS reconstruct:

```
x = 2 * normal_sample.a - 1
y = 2 * normal_sample.g - 1
q = 1 - x*x - y*y
z = RCP(RSQ(q))
N = normalize(y*T + x*B + z*Ng)
```

**Alpha/x multiplies binormal; green/y multiplies tangent.** Do not replace this
with the conventional x*T + y*B mapping. The blue and red sample channels are
unused here. No shader-consumed bump-strength constant was found. Preserve the
original DP2ADD, RSQ, RCP and NRM sequence and its `_pp` modifiers.

For finite nonzero q, the mathematical reconstruction is `sqrt(abs(q))`, not
`sqrt(max(q,0))`: D3D9 RSQ takes the absolute source and maps zero to infinity.
Thus q<0 is not by itself an undefined normal input. The exact q=0 reciprocal
chain and near-zero partial-precision behavior need GPU boundary cases.
[Microsoft RSQ contract](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/rsq---ps).
NRM is preserved too; Microsoft's description includes a finite-maximum scale
for a zero-length input, rather than introducing a replacement normal.
[Microsoft NRM contract](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/nrm---ps).
The analytical lighting oracle should require finite nonzero mixed normal and
view vectors and noncoincident active point lights. Separate boundary fixtures
qualify retained legacy instructions and finite final storage; they must not
silently invent a fallback normal or claim full-color equivalence outside the
analytical domain.

Two-sided PS apply the original VFACE sign **after normalizing N**. One-sided
PS ignore face completely. Neither T nor B is independently normalized or
orthogonalized. The VS applies the existing world inverse-transpose rows to
normal, tangent and binormal, using the original homogeneous operand; retain
those operations even for nonorthogonal/nonunit input bases.

## Lighting, alpha and color conversion

The three VS export UV at TEX0, camera-minus-world-position view at TEX1,
geometric normal at TEX2, tangent at TEX3 and binormal at TEX4. These vectors
are not normalized in the VS. Point lighting uses the **unnormalized geometric
normal**, not the sampled bump normal, and has no added point specular lobe.
Retain the existing distance, saturated dot and saturated reciprocal attenuation.

Loop VS read up to eight c0–23 point entries under i0, world rows c28–30,
normal rows c31–33 and material emissive c40. Fixed-point VS uses c4 position,
c5 RGB, c6 attenuation, world c7–9, normal c10–12 and emissive c19. Point RGB
must be decoded separately before its response and accumulation; retain the
native already-scaled material emissive as a separate amplitude with our own
linear gain. The [input-producer study](../reverse-engineering/material-color-inputs.md)
and [DEFAULT numeric policy](scene-linear-materials.md) apply unchanged: this
is a declared gamma-2.2 input convention, not recovered physical calibration.

All six PS retain Argon's exact float32 diffuse coefficient
`0.4000000059604645`, fifth-power specular response, scalar specular multiplier
3, directional `sat(3*NdotL)` factor, and cube coefficient 1. The shader-local
diffuse coefficient is c8.y in the two base PS, c7.y in affine single-light PS
and c4.y in non-affine PS. Direction/color pairs are c4/c5 and c6/c7 in base,
c4/c5 in affine single-light, and c1/c2 in non-affine single-light. Directions
are not normalized by the PS.

Unlike DEFAULT, this family computes cube coordinates per pixel from the
reconstructed normal: with V = normalize(interpolated view), the coordinate is
`2*dot(V,N)*N - V`. Preserve the original order and sampling operation. Decode
the sampled cube RGB, not its coordinates. Directional diffuse/specular also
use N and V. The resulting color equation is:

```
L = (directional_response + linear_point_response + scaled_material_emissive)
      * decoded_affine_diffuse
    + decoded_cube * specular_red * decoded_affine_diffuse
    + decoded_lightmap * lightmap_gain
```

Keep angular/data partial precision and original samples; remove partial
precision only from proven RGB accumulation/multiplication destinations. Affine
completion, reused temporary lanes and RGB/data mixed instructions need exact
per-program conversion-site proof. In particular, the RGB transfer must not
modify diffuse alpha saved in a temporary's w lane. Replace the legacy
COLOR0 RGB saturation/read with a new full-precision radiance varying; do not
decode or clamp the accumulated point/emissive sum. Final L uses the established
ordered finite sanitizer, 65504 ceiling and safe inverse transfer before RT0.

All six retain `lerp(diffuse.a, lightmap.a, glow) * vertex_alpha` as output alpha.
Glow is c3.x in affine PS, c0.x otherwise. VS alpha is material alpha (c39 loop,
c18 fixed), optionally multiplied by the original saturated fog factor from
view distance and c41/c20. Bump alpha is normal data and does not replace
opacity. RGB lightmap emission remains additive regardless of the glow-alpha
choice. No new fog, alpha test, blending, culling or depth behavior is proposed.

## Explicit family ABI and implementation gates

All ten generated [motion rows](../../src/renderer/motion_output_profiles_inc.h)
are class B and already agree on the following varying assignments:

| Purpose | VS output | PS input | Semantic / target |
| --- | --- | --- | --- |
| Original data | o0–o6 | v0–v5 | Position, COLOR0, TEX0–4 |
| Existing motion | o7 | v6 | TEX5 → RT1 |
| Existing current depth | o8 | v7 | TEX6 → RT2 |
| Proposed linear RGB | **o9** | **v8** | **TEX7**, full precision |

All nine originals leave this new varying free. Keep a small explicit BUMPMAP
ABI alongside DEFAULT; do not move existing depth or build a general allocator.
The position temporary is r2 for all three VS; loop clip rows begin c24, fixed
rows c0. Original clip instructions and existing motion/depth insertions must
remain unchanged. The base UV declaration is centroid; toggle UV declarations
are not. Retain these facts and the existing no-MSAA gate.

Original VS use r0–r6; original PS use r0–r6 (base), r0–r5 (affine single) or
r0–r4 (non-affine). Motion PS reserves respectively r7–r9, r6–r8 or r5–r7;
depth reuses the first motion temporary after its last motion use. A simple
disjoint proposal is to retain VS r7/r8/r9 for color work and use **PS r10**
for transfer scratch, r11 for final encoding, r12/r13 for decoded directional
colors. The original PS RGB destinations can remain in their existing registers.
Existing PS transfer scratch r9 overlaps base motion reservations; choosing r10
avoids needing a new cross-fragment lifetime argument. Keep material DEFs
c248–249 VS / c212–213 PS and temporal constants c252–255 / c216–220: every
original leaves them free, including the bounded loop addressing range.

Before production, prove each original fingerprint/count, all new sites and
register exclusions, and actual transformed instruction-slot limits. The
current transformer's **1296-DWORD input ceiling excludes the two base BUMP
PS**; expand only to the new exact maximum 1354 while retaining per-profile
counts. Its hardcoded class-A varying/temporary and four-sampler assumptions
must become explicit family facts. The insertion mechanism must still validate
the motion transform against immutable original bytes, then merge insertions
at original offsets. Never pass material-patched bytecode into original-hash
motion validation or weaken its fingerprint guards.

The existing production and fixture slot counters undercount control flow and
cube sampling. Correct them with the implementation: REP/LOOP use three slots,
ENDREP/ENDLOOP two, IF three, and pixel cube TEXLD four. BUMPMAP also introduces
DP2ADD, which uses two. The previous DEFAULT figures (maximum VS 77 / PS 165)
are estimates from that counter, not exact documented slot totals. Existing GPU
creation evidence is unaffected; calculate corrected totals before extending
the production gate. Use the observed instruction set and sampler declarations,
rejecting unsupported forms rather than guessing a cost.
[PS3 instruction costs](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx9-graphics-reference-asm-ps-instructions-ps-3-0),
[VS3 instruction costs](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx9-graphics-reference-asm-vs-instructions-vs-3-0).

## Bounded verification and remaining questions

Offline proof/reference work can establish every pair, conversion location,
resource reservation and coefficient now. Extend the numerical reference with
explicit tangent/binormal/view inputs and the preserved normal decode; retain
float64 equations and clearly limited endpoint FP16 quantization. Cross-check
the six role tuples against independently derived profiles. New meaningful
cases are:

- Neutral normal, asymmetric A/G perturbations, changed unused R/B, and
  nonunit/nonorthogonal/mirrored bases. Prove the actual A→B/G→T mapping.
- q positive, negative and zero/near-zero; two-sided front/back and one-sided
  ignored face. Separate degenerate primitive qualification from the analytic
  normal/view domain.
- Independently colored one/two directional and zero/one/eight/fixed-single
  point lights. Perturb bump normal while holding geometric point response
  fixed; cover affine toggles and exact fifth-power response.
- An asymmetric direction-dependent cubemap so a wrong per-pixel reflection
  vector cannot pass with a constant cube. Vary specular red as data, normal
  alpha as geometry and diffuse/lightmap alpha as opacity independently.
- Existing emissive-strength linearity, color transfer/finite endpoints and
  gain-independent original alpha; all ten pairs preserve clip, RT1 and RT2
  under both current-depth modes, fog and face variants.
- Live alternating DEFAULT/BUMPMAP families, unavailable combined objects,
  stage-s4 unknown/TRUE refusal, state-block sampler restoration and Reset.
  A BUMP failure falls back to ordinary reviewed motion. Keep shared-VS negative
  witnesses: `4944d81dfe531b37`/`0c1f3f0f440e4a0c` (standard_lighting), and
  either toggle VS with `042c9ae16f41feff` (terran_0000), remain uncovered.

The sampler array and setter already track s4, but `resync_samplers` currently
queries `D3DSAMP_SRGBTEXTURE` only for stages below four. Extend that documented
API resynchronization to the union of required samplers, `0x1f`, at attach,
Reset and state-block recovery. Changing the admission mask alone would leave
s4 unknown after these transitions. DEFAULT must still ignore irrelevant s4
unknown/TRUE state. Keep these queries outside the per-draw path and add no
per-draw allocations.
Keep eligibility and the required mask as a small pair contract. Prefer caching
that resolved contract with the current shader identities on SetShader,
registration and resynchronization, invalidating it with the existing state
lifecycle; the added coverage should not grow repeated per-draw table scans.
Only samplers named by the cached mask must be known FALSE. A failed or absent
contract stays ineligible, and combined-stage availability remains separately
checked so stale cache entries cannot authorize a partial pair.
Store the contract in `Shadow`, so a full resynchronization clears it even when
a shader getter fails. Successful shader setters must clear/recompute it for
null or unknown shaders too. Registration of an already-bound shader clears it
before releasing/replacing variants, including paths that return early or throw;
only the completed registration repopulates it. State-block recording must not
alter the active contract. Keep current HDR readiness and combined-object checks
outside the cache, and avoid contract resolution when materials are disabled.
Inspect generated slot counts and qualify the new varyings/partial-precision
boundaries on GPU; fixture timings are not gameplay FPS. Existing owner,
opaque/no-MSAA, gamma22/AgX, TAA, bind rollback and Reset gates stay in force.
Native Windows behavior remains unverified and requires its own execution.

No uncertain engine input producer required a new EXE hook or fresh game run
for this design. The reviewed per-site conversion/resource audit is now complete.
Remaining qualification includes transformed budgets and GPU treatment of
preserved normal arithmetic, interpolation and
directional cube sampling. Visual quality and actual cost await a later
user-controlled scene run after implementation, not a prerequisite for this
offline contract. Other BUMPMAP families remain separate work.

Local evidence: `/tmp/x3-shader-sweep/programs/{vs,ps}_<hash>.bin` and matching
`/tmp/x3-shader-sweep/disassembly/{vs,ps}_<hash>.bin.txt`; complete derived archive
inventory and generated motion rows linked above. Raw game bytes and compiler
disassembly remain untracked. The offline checkpoint adds proof/reference code
and focused host checks; it changes no production shader or installed build.

## Offline review verdict

Independent review on 2026-09-13 found no blocking defect in this offline
checkpoint. The generated proof binds the nine BUMPMAP originals, ten exact
pairs and 24 archive pass occurrences; it covers the A-to-binormal/G-to-tangent
basis, geometric point response, per-pixel normal/reflection chains, face
handling, opacity, RGB sites and class-B resource exclusions. The prior 15
DEFAULT program records and 20 pair rows remain unchanged apart from the added
accurate original-slot annotation. Independent recounting against the linked
SM3 tables confirmed BUMPMAP VS totals 62/51/62 and PS totals
65/70/51/47/56/52. The focused profile and numerical-reference modules pass
25 and 28 tests respectively. Five affected pure-host transformer tests also
pass, retaining the installed 15-program/20-pair/120-variant corpus and previous
Argon byte-exact baseline; the full focused set is 58 tests. This verdict certifies original-site structure
and the analytical reference only; transformed budgets, GPU boundary behavior,
production admission and native-Windows execution remain future qualification.

## Runtime checkpoint

The implementation now covers all **24 originals / 30 pairs**. The design above
records its contracts; the offline verdict is the earlier checkpoint, not the
current implementation status. Material color stays opt-in and uses the existing
linear evaluation/compatibility encoding policy.

The pure transformer preserves the immutable-original motion merge, exact alpha
and normal operations, and class-B varying/scratch separation. All 120 previous
DEFAULT outputs remain byte-exact. Seven focused structural tests cover 192
variants and 2,888 checks, including the exact pair matrix, cross-family refusal,
input/output aliasing, resource boundaries and temporal chunks. Corrected maximum
weighted slots are VS 82 / PS 168 for DEFAULT and VS 87 / PS 179 for BUMPMAP,
below the 512-slot gate. The diagnostic 192-transform host run took 4.827 ms;
shader creation work adds no per-draw allocations.

MotionOutput caches the exact pair's sampler mask with its shader state. It
checks current combined objects/HDR readiness independently, resynchronizes s0–s4
through public D3D getters, and preserves DEFAULT's independence from s4. Three
focused host tests cover actual extracted registration, setter, state-block and
Reset methods, including 18 registration failure/early-return scenarios. They
also check that repeated draw admission performs no pair lookups or sampler
queries. This does not claim an overall frame-time speedup: pair resolution
still runs when shader bindings change. Bounded refusal logs report sampler
masks, and `bump_routed` counts completed combined BUMPMAP draws per frame.

The [detached X3 GPU result](../../verification/results/bottle-X3/linear-material-gpu.json)
passes **512 cases**, retaining the previous 313-case prefix, with 137 successful
shader creations. It checks 4,473 analytical RGB samples and 135 operational-only
samples; all 131,072 pixels preserve alpha and RT1/RT2 as applicable and store
finite, capped RGB. New-family error uses at most 15.952% of the stated tolerance.
Fifteen q=0/near-zero/zero-normal/zero-view cases deliberately make no float64
full-color equivalence claim. Preserved source operations plus these operational
checks did not require extra instrumented diagnostic shaders. Fifteen focused
GPU parser/oracle tests pass.

For 98,304 vertices in four draws with six measured samples, BUMPMAP's
original/motion/combined median submission-to-completion times were
0.603/0.740/0.861 ms with zero point lights and 0.625/0.736/0.864 ms with eight.
The roughly 0.12 ms combined-over-motion difference belongs to this synthetic
workload; it is neither GPU-only timing nor game FPS. No avoidable per-draw
allocation, repeated bytecode validation or new lock was introduced.

Independent review found no source blocker in the core, detached fixture, live
cache/lifetime changes or the authored live GPU script. The latter appends
class-B positive/negative routing, s4 state-block/recovery, family transitions
and another Reset to the existing prefix: 24 frames in each of eight ownership,
TAA and feature twins. Its x86 syntax check and two parser tests pass. The
[fresh live result](../../verification/results/bottle-X3/linear-material-live.json)
passes all eight twins on the first run: **2,416 checks / 192 frames**. The
schedule verifies actual BUMPMAP route counts, exact feature-on/off alpha and
RT1/RT2, shared-VS negatives, s4 refusal/DEFAULT independence, state blocks,
family history transitions, cached gains and two resets. Five additional
combined objects retire: held-reference twins are 18/23 without TAA, 27/32 with
plain TAA, and 31/36 with ownership plus TAA. Sampler-getter and injected
create/bind/restore failures remain host evidence, not injected GPU failures.

Source checkpoint `df4dc09` was built once, clean, in 6.456 s. The retained DLL
keeps all 194 imports from 15 DLLs and passes the x87 audit (212 reachable
functions, zero violations), one writable load smoke (8 checks / 17 exports),
and one affected launch dry-run. It is now installed in X3; the compact
[install record](../../verification/results/linear-material-install.json) binds
its hash, scope, results and previous DEFAULT rollback pair. EXE and bottle
configuration are unchanged. No game was launched. The existing user run 6
covers the enlarged material slice; no additional launch was added to the queue.
Native Windows execution and gameplay appearance/performance remain unverified.

## Runtime review verdict

Independent review of the completed production and verification checkpoint on
2026-09-13 found no open defect. The transformer keeps all 120 prior DEFAULT
outputs byte-exact, preserves the original BUMPMAP alpha/normal and temporal
streams, and stays within the corrected VS 87 / PS 179 weighted-slot maxima.
The cached `0x0f`/`0x1f` sampler contract is invalidated and rebuilt across
shader registration, state blocks and Reset without draw-time table lookup or
sampler queries; partial creation and getter failures remain ineligible.

The detached X3 run passed 512 cases, 4,473 analytical samples and 15 explicitly
limited boundary cases. The fresh retained candidate then passed eight live
twins with 2,416 checks over 192 frames: BUMPMAP and DEFAULT admission/refusal,
s4 recovery, exact alpha and RT1/RT2 equality, TAA on/off, both ownership modes,
and retirement of all five added shader objects. Failed fixture runs now keep
the accepted result and publish diagnostics only under their raw directories.
This review qualifies the bounded CrossOver X3 route and evidence. Native
Windows execution, gameplay appearance and gameplay performance remain open.
