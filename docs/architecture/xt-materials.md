# XT material contracts and linear extension design

Source implementation, 2026-09-13, based on `608e0c0`, following the bounded
study from `09be35b`. This owns the XT material slice; [the earlier linkage study](../reverse-engineering/xt-material-linkage.md)
retains the original capture and archive evidence. Damage IFC motion is already
implemented; its original arithmetic must remain intact under the material
extension. The separate 148-pair palette qualification is outside this study.
The full 14-pair source is implemented and host-qualified below. No game/Wine/GPU
run, DLL build, installation or commit occurred for this source checkpoint.

## Decision and complete scope

**Ten BUMPMAP/BUMPMAP_LOW pairs have a concrete compatible extension. The four
DEFAULT pairs have a separate, explicitly authored shader-pair repair below.**
Whole COLOR1 plus two scalar relocations resolves the ten-pair register issue,
using the [reviewed Boron/Paranid transport design](linear-palette-materials.md).
It does not itself repair DEFAULT's seven missing native input lanes. The ten-pair
slice covers every standard, damage and terraformer BUMP/LOW archive variant;
complete XT support also requires implementing and qualifying the four authored
repairs. Original DEFAULT equivalence is not a meaningful acceptance criterion.

The union is 40 effects, 120 P0 passes and 14 exact VS/PS pairs. All effects have
DEFAULT, BUMPMAP and BUMPMAP_LOW. Every filename below occurs in all four
`shader/3_0/` directories: the base directory, `hue_lights_off/`, `hueshift_off/`
and `v_lights_off/`. `xt_standard_lighting[2s].fb` and
`xt_terraformer[2s].fb` occur in root `01.cat` and `addon/01.cat`;
`xt_standard_lighting_damage[2s].fb` occurs only in `addon/01.cat`.
This Cartesian expansion specifies every alias, including unobserved variants;
the toggle directories create no additional pair identities.

`D` = VS `494fe349b8bc12ec` (526 DWORDs); `B` = VS
`37c34a7478544c14` (768 DWORDs). Counts include every alias above.

| Family | Technique | VS | PS, ordinary / 2s | Occurrences each | Native PS DWORDs, ordinary / 2s |
|---|---|---|---|---:|---:|
| Standard + damage | DEFAULT | D | `fffdabd910793aba` / `e6794b6ec37ff71a` | 12 | 1648 / 1674 |
| Standard | BUMPMAP | B | `5f82ecacd39529cd` / `f1b0e820c7b488c3` | 8 | 1765 / 1791 |
| Standard + damage | BUMPMAP_LOW | B | `6733b119142c8d42` / `496049cec2066ed3` | 12 | 1754 / 1780 |
| Damage | BUMPMAP | B | `d51cf763125cb85a` / `31445adb0a62d134` | 4 | 1720 / 1746 |
| Terraformer | DEFAULT | D | `fd58e6b7e8cf969c` / `dd87737d697c6764` | 8 | 1561 / 1587 |
| Terraformer | BUMPMAP | B | `d22f2ce2c740e6a7` / `1de3d2dde345a7e3` | 8 | 1684 / 1710 |
| Terraformer | BUMPMAP_LOW | B | `75fb9c6b05e28ea2` / `edaef099780fcafe` | 8 | 1673 / 1699 |

The 2s programs additionally consume vFace. Damage DEFAULT/LOW are exactly the
standard programs, not damaged-normal variants.

## Native sample, branch and alpha contracts

These are consumed channel roles, not a claim about runtime texture formats or
authoring transfer functions. Every sample is outside divergent control flow.
The original preshader's affine transform stays before the material color
conversion: `D.rgb = (dot((Diffuse.rgb,1),c0), dot(...,c1), dot(...,c2))`.
Its c3.x is `1-W`, where W is `g_Color_Weighting`.

| Sample | DEFAULT / BUMP-LOW sampler | Live roles |
|---|---|---|
| Diffuse | s0 / s0 | RGB through the affine transform; A retained for alpha |
| Bump | absent / s1 | Full BUMP uses A,G for normal XY; LOW uses RGB for signed XYZ. No bump color channel |
| Specular | s1 / s2 | R masks direct specular and reflection; other channels unused |
| Lightmap | s2 / s3 | RGB additive color; A selects glow alpha |
| Cube | s3 / s4 | RGB reflection color; A unused |
| Occlusion O | s4 / s5 | A gives RGB occlusion exponent. Standard RGB is optional decal color. Damage BUMP RGB optionally multiplies D; R also controls detail damage and B scales the normal. Terraformer RGB is unconditional additive color |
| Detail T | absent / s6 | RG normal detail, A direct-specular candidate; B unused |

Diffuse, bump, specular and lightmap use TEX0.xy. Detail uses XY times
`g_pTexTiling`; occlusion uses TEX0.zw; cube uses the computed reflected vector.
All declared booleans default false. Standard and damage use b0 for the
following diffuse branch and b1 for palette mixing. Terraformer has no diffuse
branch and uses b0 for palette mixing.

For standard DEFAULT/BUMP/LOW, b0 false gives `Base=D`; b0 true applies this
encoded-color decal schedule componentwise (the two dot expressions are scalar):

```
h = saturate(10*(dot(D,(0.298999995,0.587000012,0.114))-0.449999988))
x = 1 - 2*(1-O.rgb)*(1-D) - 2*D*O.rgb
y = h*x + 2*D*O.rgb
t = saturate(2*(O.r+O.g+O.b))
Base = D + t*(y-D)
```

Damage BUMP instead gives `Base=D*O.rgb` when b0 is true, else D. Independently
of b0, `damage=max(1-2*O.r,0)` for finite values attenuates detail XY. Preserve
its actual CMP/NE IFC/one-MOV/join sequence, including precision and nonfinite
behavior; do not replace it with MAX. Terraformer always has `Base=D`.

Let `G=v3`, `Y=v4`, `X=v5`, and detail offsets
`dx=(2*T.r-1)*NormalDetailStr`, `dy=(2*T.g-1)*NormalDetailStr`.
Full BUMP forms `x=(2*Bump.a-1)*BumpStrength+dx`,
`y=(2*Bump.g-1)*BumpStrength+dy`, and
`z=rcp(rsq(1-x*x-y*y))`, without a clamp. LOW substitutes Bump.r for A and
`z=2*Bump.b-1`. Both normalize `y*Y+x*X+z*G`. Damage BUMP multiplies dx/dy by
`damage` first, then multiplies the normalized vector by **raw O.b**. This is
occlusion blue, not bump blue: its register lane survives earlier XY-only
writes. DEFAULT normalizes v3. Call the resulting unflipped normal N.
For 2s, direct lighting uses `Nlit=(vFace>=0 ? +1 : -1)*N`; cube and palette-line
math continue to use N. Do not add normalization after Damage's O.b multiply.

With `V=normalize(v2)`, light directions c5/c7 and colors K0=c6/K1=c8:

```
di = saturate(dot(Nlit,Li))
Hi = -Li - 2*dot(-Li,Nlit)*Nlit
qi = pow(saturate(dot(Hi,V)),SpecularPower)*saturate(3*di)
Lit = DiffuseStrength*(d0*K0+d1*K1)
    + SpecularStrength*Ms*(q0*K0+q1*K1)
```

Ms is Specular.r for DEFAULT and `max(Detail.a,Specular.r)` for BUMP/LOW;
reflection always uses Specular.r. Preserve scalar PP/saturations and the exact
normal/reflection schedule; there is no outer factor three on the specular lobe.

```
R = -V - 2*dot(-V,N)*N
line = pow(1-abs(dot(V,R)),LinesPower)
P = wx*Color1 + wy*Color2 + wz*Color3 + wh*Highlight + line*Lines
Tint = Base*((1-W)+W*P) if palette enabled, else Base
C = Tint*(Lit+saturate(v0.rgb))
  + Base*vFresnel*ReflectionStrength*Specular.r*Cube(R).rgb
out.rgb = C*pow(O.a,OcclusionStrength) + Lightmap.rgb
```

Terraformer additionally adds O.rgb after that expression; its O.rgb and
lightmap RGB are not attenuated by occlusion. DEFAULT palette weights are
v4.xyz, highlight v5.y and Fresnel v5.x; BUMP/LOW uses v6.xyz, v7.y and v7.x.
DEFAULT colors Color1/2/3, Lines, Highlight occupy c14–18, LinesPower c19,
W c20. BUMP/LOW uses c17–21, LinesPower c22, W c23. These are **runtime effect
uniforms**, unlike Boron/Paranid immutable palette DEF colors. The exact source
parameter/default records and consumed registers establish their identities,
not a color-space tag. The 14 PS CTABs show the same RGB defaults: Color1
(0.31,0.30,0.07), Color2 (0.30,0.28,0.09), Color3 (0.10,0.28,0.30), Lines
(0.34,0.29,0), Highlight (0.60,0.10,0.10), expressed here as human-readable
coefficients rather than replacement float32 bits. Runtime effect/material
uploads can override them. Treating every uploaded RGB value for these five
parameters as gamma22-authored color is an **explicit conversion policy**, not
recovered source color-space metadata. Apply the established finite gamma22
transfer to RGB only; retain alpha/unused lanes, W, LinesPower, HighlightPower,
Fresnel controls and geometric weights as raw scalar/data values. A runtime
value already authored in linear space would need a separate declared policy;
its numerical value alone cannot identify its color space.

Every pair has exactly the same native alpha:
`out.a = v0.a*(EnableGlow*Lightmap.a + (1-EnableGlow)*Diffuse.a)`.
EnableGlow is float c4.x and LRP is numeric, not a boolean or saturation.
The alpha fetch/LRP/multiply remain PP. Neither O.a, palette weights, detail A,
bump channels nor cube A feeds output alpha. Geometry, this alpha schedule and
all native color-branch decisions remain unchanged in the proposed extension.

## Ten-pair COLOR1 and resource plan

B's TEX6.xy occupies o8/v7. Its only consumers are v7.y in the palette-highlight
MAD and v7.x in the reflection MUL. Relocate X to TEX1.w (o3.w/v2.w), Y to
TEX2.w (o4.w/v3.w), and use the vacated whole o8/v7 for COLOR1.xyz without PP.
Original TEX1/TEX2 are XYZ-only; all ten PS declarations share the scalar
carrier's PP permission and noncentroid TEXCOORD mode. Extend those declarations
to XYZW while retaining original XYZ math. Keep the whole native COLOR0
declaration and its alpha path; the consumed-RGB distinction is recorded below.
COLOR1 introduces the same centroid/flat policy already addressed by the reviewed
palette design; do not reuse an RGB/alpha split COLOR declaration in one register.

All sites below are zero-based original DWORDs. The common VS has declarations
TEX1/TEX2/TEX6 at 482/485/497; scalar producers are o8.y EXP at 733 and o8.x MAD
at 743. Change only their destinations and the following exact source operands:

| PS | Highlight Y read | Fresnel X read | Last native palette ENDIF |
|---|---:|---:|---:|
| `5f82ecacd39529cd` | 1674 | 1721 | 1720 |
| `f1b0e820c7b488c3` | 1700 | 1747 | 1746 |
| `6733b119142c8d42` | 1663 | 1710 | 1709 |
| `496049cec2066ed3` | 1689 | 1736 | 1735 |
| `d51cf763125cb85a` | 1629 | 1676 | 1675 |
| `31445adb0a62d134` | 1655 | 1702 | 1701 |
| `d22f2ce2c740e6a7` | 1585 | 1632 | 1631 |
| `1de3d2dde345a7e3` | 1611 | 1658 | 1657 |
| `75fb9c6b05e28ea2` | 1574 | 1621 | 1620 |
| `edaef099780fcafe` | 1600 | 1647 | 1646 |

The existing component WRAP transaction must carry original WRAP6.X to WRAP1.W
and WRAP6.Y to WRAP2.W while retaining destination XYZ bits. Motion TEX7 and depth
TEX8 retain their independent generated-lane zero-WRAP contract. Restoration,
stateblock/shadow-off reads and failure handling use the reviewed transaction;
no global native WRAP clear and no per-draw bytecode scan are needed. Microsoft's
[SM3 semantic and wrapping rules](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/shader-model-3)
make this part of the transport ABI, not an optional visual adjustment.

The material VS can reuse r7–r9 and transfer constants c248–249. Decode each
relative point-light RGB c1[a0.w] before the point product at DWORD 593, and
sanitize the already strength-scaled emissive c41 before the RGB addition at
608, with gain but no POW; retain attenuation, loop,
geometry, native COLOR0 declaration and alpha at 683/688. Reuse the original
point accumulator with decoded source colors, redirect its final RGB export to
full-precision COLOR1, and replace both PS saturated COLOR0 RGB reads. The only
remaining COLOR0 read is alpha; do not duplicate point math to preserve unused
COLOR0 RGB values. Repaired ordinary DEFAULT retains its original COLOR0 RGB.
B's geometric palette weights and Fresnel/highlight remain scalar data unchanged.
The native double ReflectionStrength factor (VS scalar and PS multiply) remains.

All original PS temporaries are r0–r5, or r0–r6 for terraformer; existing motion
and depth reach at most r9. The concrete allocation is r10 transfer scratch,
r11 final encode, r12/r13 decoded directional colors and reusable r14 for each
runtime palette RGB conversion (and terraformer additive O.rgb). No five-color
constant mirror or per-draw palette upload is needed: decode each original
uniform into r14 immediately before its single weighted RGB use inside the
existing palette branch, then reuse r14. PS c212–213 and temporal c216–220 stay
disjoint; no immutable palette DEF slots are required. Motion remains o9/v8
TEX7, depth o10/v9 TEX8, within ten PS inputs.

Preserve the encoded diffuse affine and native decal/damage artistic operation,
then decode completed Base at its proved branch join before palette/lighting.
This chooses the same conversion boundary as the existing affine hull route;
it does not claim that moving decode across the affine/decal operation is
numerically equivalent. Decode each palette uniform before weighting, each
light color before its product, cube RGB before reflection, and lightmap RGB
before addition. Terraformer decodes a separate copy of O.rgb only for its final
add. Preserve the original raw O.r/O.b/O.a for damage and scalar occlusion.
Remove PP/saturation only at enumerated radiance destinations, never normal,
mask, alpha or scalar-weight operations. Finish with the existing finite encode.

Use the cached sampler contract mask `0x39` (s0,s3,s4,s5) for all ten: those
color-bearing samples require known SRGBTEXTURE=false under the existing
explicit gamma22 admission. This is especially necessary for O's mixed color
and data channels. Do not enable hardware sRGB on O or mutate its data channels.
Bump/specular/detail sampler state retains native behavior. Unknown/enabled
color-sampler sRGB state refuses material enhancement through the existing
ordinary-motion fallback. This is a source design, not proof of runtime texture
transfer authorship or of universal admission in gameplay.

Five runtime palette decodes add 35 straightforward instructions (including
15 POW instructions) on the enabled palette path using the current seven-
instruction PS transfer helper. Unlike immutable palettes, this has GPU cost.
Scalar relocation itself adds no shader arithmetic. Exact transformed weighted
slot counts, temporary initialization, native shader creation and performance
still need proof; original largest PS is 1791 DWORDs / approximately 120 slots.
Do not claim a transformed budget or measured GPU speed before implementation.

## DEFAULT: native host gates and correction prerequisite

D exports TEX0.xy and TEX1–3.xyz; all four DEFAULT PS request TEX0.xyzw,
TEX1–2.xyz, TEX5.xyz and TEX6.xy. Missing TEX0.zw supplies unconditional occlusion
UV, TEX6.x unconditional reflection, and TEX5.xyz/TEX6.y palette weights. False
palette/decal booleans do not remove the first two dependencies. Effect defaults
ReflectionStrength=1 and OcclusionStrength=1 keep them live. The earlier run19
contains 226 exact D/fffd device bindings; those observations do not certify
portable/native-Windows successful rendering of the invalid original linkage.

Targeted native-host inspection strengthens this beyond archive inference:

| Native X3AP site | Proven behavior |
|---|---|
| `004c0996–004c09d2` in material setup `004c0150` | A nonpositive bump-resource local selects DEFAULT; a positive value selects BUMPMAP only with object +0x130 bit0x100000 clear, otherwise DEFAULT. The local is populated by the material bump-texture record/loader path |
| `004c0b4b–004c0b85` | The later low-material path chooses BUMPMAP_LOW from its texture locals with the same flag clear, otherwise DEFAULT |
| `004c0bef–004c0c09` | Resolves the selected technique name through GetTechniqueByName; absent handles enter FindNextValidTechnique |
| `004c1336` and helper `004b93a0` | Generic material parameter records dispatch typed setters, including scalar/vector effect parameters |
| `004b9010–004b9059` | Float setter calls GetParameterByName, then GetCurrentTechnique and IsParameterUsed; only a used parameter reaches SetFloat |

The last mapping is verified from the public ID3DXEffect vtable offsets and
native call arguments: +0x24/+0xec/+0xf8/+0x78 respectively. These are documented
[effect operations](https://learn.microsoft.com/en-us/windows/win32/direct3d9/id3dxeffect),
not a backend-private layout dependency. The EXE gate shows a deliberate DEFAULT
selection path; it supplies no alternate vertex producer. A generic material
parameter can therefore be skipped when unused by the current technique. The
DEFAULT VS does not bind FresnelExponent, MinFresnel or HighlightPower. Their
presence elsewhere in the effect, or stale higher device registers, does not
prove current-material values for a new DEFAULT producer.

B is evidence for a repair policy, not a drop-in replacement: it requires
tangent/binormal, changes material/fog register layout, and normalizes the view
before geometric weight calculations where D does not. The smallest correction
can nevertheless use D's own inputs, with no new engine parameter ownership:

| Missing input | Owned source / proposed authored policy |
|---|---|
| TEX0.zw | Copy original vertex v1.zw; leave existing transformed XY untouched |
| TEX5.xyz palette axes | At D's tail, r0.xyz is world view delta and r2.xyz is transformed world normal. Normalize separate copies V/N, compute `R=-V-2*dot(-V,N)*N`, emit abs(R) |
| TEX6.y highlight | With `q=saturate(dot(V,N))`, emit q^12 using a fixed multiply chain |
| TEX6.x Fresnel | Emit shape `(1-q)^2`; immediately before the PS Fresnel use, form `F=0.1+0.9*shape*c11`, then retain the original later c11 reflection multiply |

A bounded query of all 226 existing D/fffd draw snapshots found one declaration,
`96b83ce555c1cf64`: stream0 TEXCOORD0 at offset8, type16, method0. Type16 is
[documented FLOAT16_4](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3ddecltype),
so UV ZW comes from four-component vertex data in every observed DEFAULT draw.
The recorded vertex bytes were unavailable; this proves component ownership,
not texture alignment or values. The other three DEFAULT PS were not observed.

The smallest authored UV policy is **copy the API-delivered v1.zw for every legal
TEXCOORD0 declaration**. D3D explicitly expands FLOAT2/FLOAT16_2/SHORT2 and the
normalized two-component forms to `(u,v,0,1)`; this policy consequently samples
secondary UV `(0,1)` for those layouts. These are defined vertex-input values,
not invented values for an unproduced VS/PS semantic. No new per-draw declaration
gate is necessary merely to make the corrected linkage valid. Its visual result
on unobserved two-component meshes remains unqualified; primary-UV reuse can be
a future explicit policy if mesh evidence warrants it. Do not silently switch
between these choices or claim the documented expansion recovers a missing
secondary texture map. Existing original-input/declaration validity requirements
still apply. The chosen copy policy covers all four shader pairs without making
the observed FLOAT16_4 layout a permanent exclusion of other layouts.

The three fixed settings—Fresnel exponent2, minimum0.1, highlight power12—are
**new DEFAULT policy choices** informed by the effect defaults. They do not
claim recovered native intent or promise to honor otherwise-unused per-material
overrides. PS c11 is already owned and live as ReflectionStrength in every
DEFAULT program. Using it for F preserves the BUMP-shaped two strength factors
without reading a stale or absent VS uniform. Normalizing separate V/N copies
is likewise explicit policy; preserve D's original outputs and lighting inputs.
This adds no tangent/binormal requirement. Zero-length/nonfinite normalization
needs the same explicit shader-reference qualification as the existing NRM
paths; do not silently alter native geometry or invent a per-draw CPU fix.

The concrete DEFAULT layout has room without scalar relocation:

| Purpose | VS output / PS input | Semantic |
|---|---|---|
| Repaired UV | existing o2.zw / v1.zw | extend existing centroid TEX0 to XYZW |
| Palette axes | new o8.xyz / existing v4.xyz | TEX5 |
| Highlight and Fresnel shape | new o9.xy / existing v5.xy | TEX6 |
| Full-precision point/emissive RGB | new o10.xyz / new v8.xyz | whole COLOR1 |
| Existing motion | o6 / v6 | TEX4 |
| Existing depth | o7 / v7 | TEX7 |

Keep original o5/TEX3 even though these PS do not read it. Nine physical PS
inputs suffice. New TEX5/TEX6 producer modes must match the existing PP,
noncentroid PS inputs; COLOR1 is independent full precision. Preserve app WRAP0
(including the now-authored UV ZW), WRAP5 and WRAP6 under this policy; only the
already generated motion/depth semantics follow their existing zero transaction.
Do not classify the original PS's TEX5/TEX6 declarations as unused lanes.

Use VS r10–r12 for the extra tail calculations, disjoint from original r0–r6
and material r7–r9; take r0/r2 sources before any new writes. The material VS
uses c40 emissive, c39 alpha and c41 fog exactly as D, not B's layout. A local
VS DEF c244 can supply 1/2 and any fixed-policy factors; local PS DEF c210 can
supply 0.1/0.9. Existing transfer/temporal constants remain disjoint. PS r15.x
can hold F through its immediate use, outside native/motion and r10–r14 material
scratch. As with the ten-pair plan, concrete instruction and lifetime validation
must prove this allocation before implementation admission. The DEFAULT color
sampler mask is `0x1d` (s0,s2,s3,s4).

Integrate the correction as an exact-pair contract for these four pairs,
including the **ordinary-motion fallback** selected when linear material
admission refuses. That fallback must use the same authored UV/weights/Fresnel
repair in encoded color, rather than reentering invalid native DEFAULT linkage.
Both VS and PS therefore need repaired ordinary and linear variants; a global
D replacement would affect unrelated valid PS sharing that VS. Reuse the
existing pair-bound variant selection and rollback discipline; the shared-VS
negative witness must compare against this repaired ordinary baseline for these
four pairs.

Publish corrected DEFAULT availability atomically only after **both stages of
both repaired ordinary and linear pairs** have been created and validated for
the selected depth mode. A lone successful VS or PS is never an eligible route.
For an admitted pair, a draw-time linear admission refusal selects the complete
repaired ordinary pair. Bind each selected pair transactionally; never submit
with a repaired stage paired with an original or differently repaired mate.

If any required corrected variant fails creation, keep this pair unavailable
and leave application bindings untouched. The live route then uses the existing
unenhanced native-forward path exactly once, with no repaired stage bound and
no claim of portable enhanced DEFAULT support. The native draw may itself fail
on the invalid original linkage; propagate that native result. Log corrected-
pair unavailability and its creation failure separately from the native draw
result; do not substitute the internal compilation/creation HRESULT for an
application API result. This is graceful feature refusal, not qualification of
the malformed original as a Windows fallback. A partial cache entry cannot
become ready until all required variants exist; retry only under the existing
creation/reset lifecycle policy, never through draw-time compilation.

If a pair bind fails after an attempted setter may have mutated state, restore
every attempted shader/state slot before trying a complete repaired ordinary
fallback or forwarding the original native draw. Retain the chronologically
first internal preparation/bind/rollback failure for diagnostics and state-loss
bookkeeping; cleanup must not overwrite it. This retained diagnostic HRESULT
does not replace the API result: return the native draw HRESULT when the draw
executes, otherwise the established suppressed/state-loss HRESULT. Unknown
restored state invokes the existing state-loss latch/submission
suppression policy, so no native draw is issued against an unknown mixed pair.
Use the existing hook/CPU boundary to preserve incoming LastError across internal
preparation and the native call's resulting LastError across cleanup. Keep the
native source call at most once and propagate its HRESULT when it executes;
use the established state-loss return policy when it must be suppressed. Focused
failure injection must cover either stage failing creation, either attempted
bind mutating then failing, rollback failure and Reset recovery.

This fixed policy avoids effect-container replacement or new engine hooks. Only
honoring custom unused Fresnel/highlight material overrides would require tracing
and changing effect parameter ownership/IsParameterUsed or capturing typed
material records explicitly. That is a separate enhancement, not a prerequisite
for the proposed authored DEFAULT policy. The effect/default distinction must
remain visible in configuration/design evidence and future reference fixtures.

## Bounded implementation and evidence boundary

Implement the ten valid rows and the four explicitly repaired DEFAULT rows as
separable, reviewable stages after the 148-pair checkpoint; complete both before
claiming all XT families. Reuse its whole-COLOR1/scalar transaction and the installed
[damage IFC motion proof](damage-motion.md); preserve every native branch token
and keep the motion epilogue after all joins. Validate exact original instruction
sites/masks/source identities, shader-resource disjointness, and complete alias
coverage before admission. Retain existing output goldens for all earlier pairs.

Focused witnesses need both standard decal and palette choices, all damage
b0/b1 combinations plus IFC outcomes/threshold, 2s face signs, unequal palette
axes and nontrivial runtime colors, O.r/O.b/O.a isolation, direction-dependent cube,
lightmap/glow alpha, both depth modes and poisoned new temporaries. DEFAULT
additionally needs distinct UV XY/ZW, fixed-policy q endpoints/grazing angles,
nonunit normals, current c11 values including zero, and poisoned unused source
constant registers proving no accidental ownership. Compare its ordinary and
linear variants against the authored reference, not undefined native output. Test relocated
scalar WRAP components and untouched native XYZ components with state recovery.
A native-color identity/reference path must preserve the original branch and
alpha math; the linear reference must separately distinguish per-source decode
from decode-after-palette-sum.

Before GPU admission, extend the existing varying and WRAP fixtures with a small
set of discriminating triangles: affine and perspective gradients, clipped
triangles, both winding/face modes and effective FLAT/GOURAUD interpolation.
Read back repaired TEX0.zw/TEX5/TEX6, COLOR1, and the relocated PP scalar lanes
independently. Check original scalar PP permission and the new whole-COLOR1
precision/centroid behavior; matching DCL masks alone is insufficient. Combine
these targeted cases with hostile WRAP bits and restoration/failure witnesses,
including unchanged native components. Reuse existing fixture state/Reset
coverage rather than building a broad Cartesian expansion.

The performance gate must time DEFAULT repaired ordinary draws separately:
the new normalization/reflection/weight calculations add unconditional VS work
even when linear material admission refuses. Also measure the BUMP linear path
with palette mixing enabled (all five runtime RGB decodes), the same path with
the branch disabled, and its ordinary control at matched draw/vertex counts.
Inspect creation-time caching and draw-time lookup to confirm no new allocation,
hashing, shader rebuild, palette upload or repeated validation per draw. Use the
existing bounded QPC/EVENT measurement method to distinguish CPU submission
cost from synchronized GPU completion observations; report query/synchronization
limits and counts. These diagnostic timings are not gameplay FPS, and a CPU-
only disabled-branch result cannot establish the enabled palette GPU cost.

Native Windows GPU qualification remains pending; source inspection and future
cross-compilation alone cannot supply it.

Local reproducible evidence is intentionally untracked: run
`python3 /tmp/x3-xt-linkage/query.py` from the repository for archive aliases and
`python3 /tmp/x3-xt-pixel-contracts.py` for exact PS instruction-site/liveness
inspection. `/tmp/x3-xt-contracts/transport-aliases.json` records all pair aliases,
declarations, scalar reads and temporary sets. The 226-draw UV declaration query
is `/tmp/x3-xt-contracts/query_default_uv.py`. Native string-xref enumeration is
`/tmp/x3-xt-contracts/host_refs.py`; bounded objdump ranges above and read-only
Ghidra decompilation of `004c0150`, `004b93a0`, `004f52e0` are under that same
local directory. Decompiled text/raw shader bytes are not copied into this note.
The owning report contains derived contracts and addresses only.

## Implemented source checkpoint

The material registry now covers **162 exact pairs**: the previous 148 plus all
14 XT rows. XT contributes 14 new PS and one new VS; DEFAULT's original VS was
already shared, so there are 130 unique original stages overall. The source
uses a bounded XT include and derived profile table inside the existing pure
transformer. Its independent source proof is
[inspect_xt_materials.py](../../tools/analysis/inspect_xt_materials.py), with
[derived profiles](../reverse-engineering/xt-material-profiles.json). Neither
contains original shader bytes. The existing 148 source tables and generated
outputs remain unchanged.

Two corrections to the initial design are deliberate. Material emissive follows
the established [native amplitude/tint policy](../reverse-engineering/material-color-inputs.md#material-emissive-is-sometimes-color--strength):
`004c1399–004c13fd` and `004c1533–004c159b` multiply color by strength before
uploading the named effect value. Both XT VS bind that same parameter (D c40,
B c41). Sanitize it and apply emissive gain without POW; only point/directional
RGB and texture/palette colors get the explicit gamma22 transfer. Also, native
COLOR0 RGB is unused after replacing both PS clamp reads; the linear VS reuses
the original accumulator and redirects its RGB export, preserving the whole
COLOR0 declaration and its independently computed alpha. This avoids a second
point loop solely for an unconsumed RGB invariant.

The live source owns additional DEFAULT repaired ordinary/linear VS objects
and an ordinary PS object, while retaining the generic shared-D variants.
Binding/registration refreshes exact-pair four-object readiness; draws consume
cached pointers. Object inventory and retirement include the extras, and
registration invalidates eligibility before reentrant Release callbacks.
Sampler s5 now participates in cached material sRGB tracking. Availability and
ordinary fallback follow the atomic contract above.

The same transaction work fixes attempted-write rollback for shader setters,
RT1/RT2, color-write masks and reserved VS/PS constants, including setters that
mutate before returning failure. Failed quiet/nonquiet lazy flushes and checked
restorations latch motion state loss immediately, including deferred/mip errors;
consuming a deferred error cannot make an unknown device state usable. Successful
Reset clears the latch only after the existing resynchronization readiness proof.
These are bounded shared state-correctness fixes, not XT shader optimizations.

Current host evidence:

- The XT driver covers 16 XT original stages, 14 exact pairs and 168
  ordinary/linear/depth/gain variants: **823 checks**, including aliasing,
  invalid gains, corruption and unchanged-output refusal. Release and
  ASan/UBSan runs pass. Seven source tests independently check every original
  instruction against the allowed edits, exact alpha/sample/control flow,
  complete repaired linkage, scalar PP/carrier masks, runtime palette decode
  placement, resource bounds and the authored DEFAULT geometry equations.
- The independent analytical reference passes **21 tests** for all 14 material
  contracts and the authored repair, with finite float64 limits explicit.
  It does not simulate GPU PP, texture filtering, rasterization or exceptional
  native RSQ/POW behavior. A separate finite float64 instruction executor also
  passes **3 tests / 188 PS executions**: 82 original, 82 linear transformed,
  and 24 repaired ordinary DEFAULT cases. It independently checks all 14
  contracts, static/damage branches, threshold values, faces, sampled
  coordinates, runtime palettes, data channels, cube direction and authored
  Fresnel endpoints against those equations. No source/oracle mismatch was
  found; temporal output and GPU PP/interpolation remain separate gates.
- All **920 previous material outputs** and **230 ordinary-motion controls**
  are byte-identical to exact `608e0c0` sources. Both previous-corpus drivers
  pass 28,774 checks; zero files are missing, extra or changed. Material framed
  SHA-256 is `f5038b0f7d05c73cd79a85c625de78df55f5714e0ed94c3661e78136664692e7`
  using sorted filename, big-endian u64 name length, UTF-8 name, big-endian u64
  content length and bytes. The still-uncovered structural negative moved from
  now-supported XT DEFAULT to `c30104cb0efb6675/a66fb1981ba755b2`.
- The focused live seam passes **13,053 checks**, including 8,809 XT checks
  and 76 attempted-state checks; the existing WRAP seam passes **34,773**.
  This includes four-object readiness, either-stage failures, mutation before
  failure, native-once behavior, lazy/void restore quarantine, reentrant
  retirement and actual successful/failed Reset recovery. These are host
  seams, not live GPU draws.
- Strict x86/SSE2 cross-compilation passes for the renderer and motion proxy
  translation units. No DLL or game artifact was built or installed.

XT maximum static weighted slots with depth off/on are: repaired ordinary VS
79/81 and PS 136/138; linear VS 104/106 and PS 296/298. All stay below 512, with
at most r15 in the PS and the planned nine/ten input registers. These are static
budgets, not GPU timing; per-vertex cost must still include the zero/one/eight
point-light loop cases. Host transformation timings include I/O and diagnostics
and do not establish draw speed.

Independent source and evidence review passed with no open findings for the
complete 19-file checkpoint, including the separately reviewed cache/lifetime
slice. The reviewer independently reproduced the prior-corpus byte comparison.
Production source is frozen; fixture preparation and qualification are separate.

GPU/native-Windows creation, interpolation/WRAP readbacks, live alpha/branch and
Reset behavior, and the matched DEFAULT-ordinary/enabled-palette GPU performance
measurements described above remain pending. Host success does not turn the
authored DEFAULT policy into recovered native behavior.

### XT fixture extension and detached X3 qualification

The separate fixture worktree retains the reviewed production source unchanged.
The existing detached material fixture now contains **3,923 cases / 162 pairs**:
the prior 3,549 records remain byte-identical, followed by 374 XT cases. Their
240-byte payload prefix SHA-256 (excluding the count header) is
`f762b2ec933ad969315be0d6cfbdee4cccd866c5b13c08e682695539d87feac3`.
The original eight C++ table prefixes also remain unchanged. Both facts have
host checks; they do not replace the earlier generated-program byte comparison.

The added cases upload actual seven-sampler BUMP/LOW or five-sampler DEFAULT
layouts, distinct spatial occlusion/detail channels, runtime palette colors,
both branch controls and the original B vertex constant layout. They cover
all 14 depth/face pairs, gains, zero/one/eight point lights, fog alpha, history
refusal, damage threshold sides, affine color, independent primary/secondary
UVs and the authored FLOAT2 expansion. Affine, perspective and near-clipped
geometry vary the native producers before interpolation. A separate native
COLOR0 ramp classifies effective FLAT/GOURAUD behavior; requested-FLAT XT cases
use that measured classification for their COLOR transport oracle.

The four DEFAULT cases always use complete repaired ordinary or repaired linear
pairs. The detached fixture never submits the invalid original DEFAULT pair.
Ordinary readbacks are required as independent numerical baselines, alongside
whole-target alpha/motion/depth identity. The ten valid BUMP/LOW pairs also
require native-to-ordinary identity. The report parser rejects missing repaired
stage creation, ordinary samples, interpolation classification or XT timing
windows. Matched timing rows separately label DEFAULT repaired ordinary and
BUMP palette enabled/disabled at zero/one/eight lights; QPC through EVENT
completion is neither GPU timestamp timing nor game FPS.

The affected detached/report/reference host checks pass: 65 tests in the
selected unittest modules, with one legacy palette-only selection narrowed to
its original pair range and that affected test rerun successfully. Review found
a timing flag serialization mismatch (Boolean 0/1 versus flag 0/128); the C++
producer now preserves the flag value, with a focused protocol check and strict
translation-unit recompilation passing. A further
host token-execution test passes **1,384 actual PS executions** using the newly
authored case inputs at two sample positions, ordinary and linear programs,
and uniform zero/one/sixteen gains. Nonuniform gains remain covered by the
detached equations and preceding pure source tests. This finite float64 test
does not simulate rasterization, interpolation precision or shader creation.
Strict i686/SSE2 fixture translation-unit compilation passes. No Windows GPU
fixture executable, production DLL, Wine run or install was produced for this
extension; the host token checks build and run their native structural driver.

The live `materialxt` mode adds **92 planned samples**: six steps for every
pair, four shared-D alternations, two valid unknown-PS controls and two
perspective transport diagnostics. Material on executes all 92; material off
executes 65 and explicitly skips 27 invalid original DEFAULT submissions.
Those skipped rows supply no portability or visual evidence. DEFAULT ordinary
comparisons instead use the complete repaired fallback after a required-color
sampler refusal. Existing corpus inputs and frame IDs remain unchanged; its
formerly uncovered XT frames 9/19 now expect covered admission.

The live matrix is depth off/on, per-draw/lazy target restoration and material
off/on. It observes all 16 WRAP states, reserved constant restoration, two native
submissions, StateBlock/Reset, readiness and retirement. The two added
perspective pairs separately compare ordinary then linear readbacks, with
diagnostic getters between those two draws; the other rows retain consecutive
lazy submissions. Calibrated 0/1 source colors, zero emissive/lightmap RGB and
direct gain one make the linear result `pow(ordinary, 1/2.2)` within the existing
numerical envelope. B highlight endpoints are `(1, 17^-6, 17^-6)` with WRAP6.Y
enabled, forcing a real interpolation seam carried to TEX2.W. Both diagnostics
require nonconstant RGB, exact alpha/motion/depth twins and matched state
observations. The report parser rejects missing or corrupted paired evidence
and requires more than 1,024 calibrated pixels, matching the executable's
64×64 quarter-target bound. Review caught and corrected the parser's weaker
initial coverage bound; zero and exactly 1,024 now have rejection witnesses.
All **15 focused live report tests** and strict i686/SSE2 live fixture
translation-unit compilation pass. Runtime shader creation, readbacks, Reset,
state restoration and completion timings for this live mode still require its
authorized X3 GPU run; native Windows remains separately untested.

Independent review of the complete ten-file fixture delta passed after those
two verification-only fixes, with no open finding. The separate reviewed
19-file production/source-proof checkpoint remains unchanged. Fixture approval
authorizes preparation for qualification; it is not a GPU, gameplay or native
Windows result.

The retained detached fixture subsequently passed on **X3 / CrossOver Preview**:
**3,923 cases, 162 pairs, 130 original stages and 35,307 samples**, including
3,366 independent XT ordinary samples. Every alpha/motion/depth identity check
passed. Maximum RGB error used **0.1614340509615012** of the allowed tolerance
(unchanged from the prior corpus); the XT ordinary maximum was
**0.1591217475431564**. The measured
programmable COLOR path remained Gouraud when FLAT was requested, matching the
previous X3 limitation rather than establishing native Windows behavior. The
[canonical detached result](../../verification/results/bottle-X3/linear-material-gpu.json)
records the exact retained executable, source and local-original hashes; raw
output remains in `/tmp/x3-xt162-detached/`.

The same independent reviewer approved this detached evidence: the consume-side
validator reproduced all 17 compact derived fields, and the 15 recorded source
hashes, 130 original hashes, retained fixture hash and unchanged 3,549-record
payload prefix match. The retained raw report SHA-256 is
`5e31b2fc2a66c74a4958da4c5e61019ce56d404b066a791e65beb5d4d3d393b6`.
There are no open detached findings; this closure does not include the pending
live route.

The eight-light timing medians below cover four managed-buffer draws, 98,304
vertices at 256×256, measured by QPC through EVENT completion with six retained
samples per mode. Zero/one-light rows remain in the same canonical result.

| Pair / palette branch | Original or repaired ordinary | Motion | Combined |
| --- | ---: | ---: | ---: |
| DEFAULT / disabled | 0.678550 ms (repaired) | 0.790750 ms | 0.793500 ms |
| DEFAULT / enabled | 0.681800 ms (repaired) | 0.793750 ms | 0.897700 ms |
| BUMP / disabled | 0.790100 ms | 0.910250 ms | 0.919450 ms |
| BUMP / enabled | 0.791900 ms | 0.921900 ms | 0.914500 ms |

These small completion-window differences include CPU/driver work and noise;
they are not GPU timestamps, per-game-frame cost or evidence of improved FPS.
DEFAULT never uses its invalid original pair as a performance control. The
detached gate now covers shader creation and the recorded numerical,
interpolation, clipping and alpha/temporal behavior on X3. The separate 162-pair
live route, physical hostile-WRAP diagnostics, actual live Reset/retirement,
native Windows and gameplay qualification remain pending.
