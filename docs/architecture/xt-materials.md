# XT material contracts and linear extension design

Bounded source study, 2026-09-13, based on `09be35b`. This owns the next XT
material slice; [the earlier linkage study](../reverse-engineering/xt-material-linkage.md)
retains the original capture and archive evidence. Damage IFC motion is already
implemented; its original arithmetic must remain intact under the material
extension. The separate 148-pair palette qualification is outside this study.
No implementation, game/Wine/GPU run, DLL build, installation or commit occurred.

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
to XYZW while retaining original XYZ math. Keep whole native COLOR0 including A.
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
emissive c41 before the final RGB addition at 608; retain attenuation, loop,
geometry, native COLOR0 and alpha at 683/688. Generate separate full-precision
COLOR1 RGB and replace only the PS native saturated COLOR0 RGB contribution.
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
