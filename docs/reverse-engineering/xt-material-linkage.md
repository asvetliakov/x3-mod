# XT material linkage and bounded damage control flow

Read-only study, 2026-09-13, completing the XT linkage question in
[remaining opaque SM3 contracts](remaining-sm3-opaque-materials.md). No production,
fixture, installed DLL, game process or Wine invocation changed.

## Decision

The four XT DEFAULT pairs have a **real, invalid native VS/PS semantic linkage**.
The archive table is correct and the standard DEFAULT pair is also present in
actual device bindings in an existing user capture. Neither static boolean
branch setting supplies the missing inputs. Treating backend-provided missing
values as portable defaults would violate the documented D3D9 linkage contract.
The ten BUMPMAP/BUMPMAP_LOW pairs have matching original semantic masks; their
material extension still needs the input-register packing/precision proof.

The two damage BUMPMAP pixel shaders have a very small, bounded dynamic branch:
one `IFC`, one assignment, one join, without nesting, early exit or sampling
inside that branch. A motion epilogue after all native control flow is a
reasonable next implementation, subject to the specific proofs below. This
finding does not admit arbitrary dynamic shader control flow.

## Complete union

Freshly parsed from the X3 bottle's root `01.cat` and `addon/01.cat`: **40 XT SM3
effect entries, 120 passes, 240 direct shader-program resource states, 14 unique
pairs**. Each effect contains P0 in DEFAULT, BUMPMAP and BUMPMAP_LOW. All four
toggle directories—base, `hue_lights_off`, `hueshift_off`, `v_lights_off`—are
included. Standard and terraformer have root/addon entries; damage has addon
entries only. Toggle directories do not introduce additional XT identities.
All shader states have resource usage zero and complete program streams; none
is a runtime shader-selection expression or null shader.

`D` denotes VS `494fe349b8bc12ec` (526 DWORDs); `B` denotes VS
`37c34a7478544c14` (768 DWORDs). Standard = `xt_standard_lighting`; damage =
`xt_standard_lighting_damage`; terraformer = `xt_terraformer`. A `2s` row denotes
that suffix, not an additional pair. Slots below are the original disassembler's
approximate weighted count, not the transformed shader budget.

| Family | Technique | VS | PS | Pass occurrences | PS DWORDs / slots | Original linkage / motion |
| --- | --- | --- | --- | ---: | ---: | --- |
| Standard + damage | DEFAULT | D | `fffdabd910793aba` | 12 | 1648 / 100 | Invalid / class C |
| Standard + damage 2s | DEFAULT | D | `e6794b6ec37ff71a` | 12 | 1674 / 105 | Invalid / class C |
| Standard | BUMPMAP | B | `5f82ecacd39529cd` | 8 | 1765 / 115 | Matching / class C |
| Standard 2s | BUMPMAP | B | `f1b0e820c7b488c3` | 8 | 1791 / 120 | Matching / class C |
| Standard + damage | BUMPMAP_LOW | B | `6733b119142c8d42` | 12 | 1754 / 111 | Matching / class C |
| Standard + damage 2s | BUMPMAP_LOW | B | `496049cec2066ed3` | 12 | 1780 / 116 | Matching / class C |
| Damage | BUMPMAP | B | `d51cf763125cb85a` | 4 | 1720 / 108 | Matching / IFC refused |
| Damage 2s | BUMPMAP | B | `31445adb0a62d134` | 4 | 1746 / 113 | Matching / IFC refused |
| Terraformer | DEFAULT | D | `fd58e6b7e8cf969c` | 8 | 1561 / 80 | Invalid / class C |
| Terraformer 2s | DEFAULT | D | `dd87737d697c6764` | 8 | 1587 / 85 | Invalid / class C |
| Terraformer | BUMPMAP | B | `d22f2ce2c740e6a7` | 8 | 1684 / 95 | Matching / class C |
| Terraformer 2s | BUMPMAP | B | `1de3d2dde345a7e3` | 8 | 1710 / 100 | Matching / class C |
| Terraformer | BUMPMAP_LOW | B | `75fb9c6b05e28ea2` | 8 | 1673 / 91 | Matching / class C |
| Terraformer 2s | BUMPMAP_LOW | B | `edaef099780fcafe` | 8 | 1699 / 96 | Matching / class C |

Class C records the existing motion transformer's structural classification;
it does **not** certify the original linkage or material math. Damage DEFAULT
and LOW reuse standard programs; only damage BUMPMAP introduces the two IFC
programs. The 2s pixels additionally consume vFace.

## DEFAULT producer mismatch is not hidden by branches

Original VS D declares COLOR0.xyzw, TEXCOORD0.xy and TEXCOORD1–3.xyz, besides
POSITION. Its instructions write only TEXCOORD0.xy; no relative output write or
alternate branch produces extra components. All four DEFAULT PS instead declare
COLOR0.xyzw, TEXCOORD0.xyzw, TEXCOORD1–2.xyz, TEXCOORD5.xyz and TEXCOORD6.xy.
Thus the same seven requested lanes are missing in every DEFAULT pair:
TEXCOORD0.zw, TEXCOORD5.xyz and TEXCOORD6.xy.

Microsoft's [SM3 semantic matching rules](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/shader-model-3#match-semantics-on-vs_3_0-and-ps_3_0-shaders)
explicitly classify both insufficient output masks and absent requested
semantics as invalid pairings. The same reference states that TEXCOORDINDEX is
ignored with PS3; texture-stage state cannot supply an alternate producer.
This is stronger than merely saying some native input values are unspecified.

For PS `fffdabd910793aba`, original token offsets establish the branch boundary:

| Original DWORD site | Missing input | Reachability / role |
| ---: | --- | --- |
| 1334 | TEXCOORD0.zw (`v1.zw`) | Unconditional occlusion/decal texture sample s4, before b0 |
| 1531, 1539, 1548 | TEXCOORD5.xyz (`v4`) | Palette weights inside b1 |
| 1557 | TEXCOORD6.y (`v5.y`) | Palette highlight weight inside b1 |
| 1604 | TEXCOORD6.x (`v5.x`) | Unconditional reflection scaling, after the final b1 join |

Here PS b0 is `g_bIsDecalMap`; b1 is `g_bColor_Mixing`. Both default false in
all 40 effects. Disabling b0 bypasses decal RGB composition but leaves the
occlusion sample's alpha live: it is raised to `g_MatOcclStr` and multiplies
final lit RGB. Disabling b1 removes palette reads, but cannot remove the
unconditional TEXCOORD6.x reflection factor. All effects default
`g_MatReflectionStrength = 1` and `g_MatOcclStr = 1`, so defaults do not mask
those dependencies. The 2s standard program has the same missing-input roles.
Terraformer uses b0 for color mixing and has no decal branch; it still samples
TEXCOORD0.zw and uses TEXCOORD6.x unconditionally. Its occlusion RGB also enters
its final additive color, making a fabricated UV still less defensible.

The effect parameter records do contain the BUMP producer's extra controls:
`g_FresnelExpon = 2`, `g_Color_HighlightPower = 12`,
`g_MatMinFresnel = 0.1`, plus the texture matrix (identity by default). Those
parameters do not change the directly bound DEFAULT VS program. The DEFAULT
VS CTAB does not bind these extra Fresnel/highlight controls. Observing values
in higher VS constant registers after a previous BUMP draw would not prove they
belong to the current DEFAULT material.

## Existing actual-device evidence

The user-provided run 19 capture contains **226 draw snapshots** with the exact
D/`fffdabd910793aba` pair. The first is frame 120, draw 72, 712 triangles, in
`/tmp/x3-bottleX3-run19/session-20260913-184230-212.log:23767`. Every such snapshot
records PS b0/b1 false. At that first draw, PS c11 and c13 are both 1, and the
occlusion sampler s4 has a bound 32×32 2D texture. This is a real game binding
under the captured settings, not just a dormant effect identity.

The authority is [capture.cpp](../../src/proxy/capture.cpp): `snapshot()` calls
`GetVertexShader`/`GetPixelShader` on the device, and `shader_id()` hashes the
returned object's `GetFunction` bytecode. The run's dumped VS and PS bytes are
exactly equal to the local archive programs, not just name matches. SHA-256s:

- VS D: `8a7049b1fab64b40e5a667350c21c55c8155d614bd5b7a3465bba6713594c2c7`.
- PS fffd: `8fc110cf9cf4631ac0f7052b8f61ec6c5908bcaa7be830853570404ddfa95b72`.

These are binding observations, not proof of a successful native-Windows draw
or of correct missing-lane values. No new game run was requested. The capture
supports ruling out an engine shader override for those observed draws; it does
not establish the behavior of every other setting or uncaptured XT variant.

## BUMP identifies candidate intended inputs, not an automatic fix

VS B does produce the missing DEFAULT semantics, with all ten BUMP/LOW PS
semantic masks matching. Its TEXCOORD0.zw copies the original vertex texture
coordinate's ZW, while XY uses the material texture matrix. TEXCOORD5.xyz is
the absolute reflected normalized view vector based on the transformed normal.
TEXCOORD6.y is the powered saturated view/normal dot controlled by
`g_Color_HighlightPower`. TEXCOORD6.x combines a minimum Fresnel value with the
view-dependent term, `g_FresnelExpon` and `g_MatReflectionStrength`.
These are data weights, not authored RGB. PS also multiplies reflection by its
own material reflection strength; do not accidentally remove a native factor.

This is a strong **candidate** for intended DEFAULT semantics because it is
paired with the related XT BUMP/LOW PS, but it is not a proof of the DEFAULT
author's intent. Simply binding B in place of D is incorrect: B requires tangent
and binormal inputs and has a different material/fog constant layout (for
example c39 is reflection strength in B but alpha in D). A deliberate DEFAULT
producer repair would need to reproduce only the needed calculations using
proved per-draw parameter ownership, preserve D's geometry/color/alpha, and
validate full semantic masks on documented D3D9. An identity-transform repair
must be described as a game-material correction, not equivalence to the
malformed original pair. Do not adopt zero/one, stale-register or backend
missing-varying values as the design contract.

## Bounded damage motion extension

Both damage BUMPMAP PS have two top-level static boolean branches and one
top-level IFC. Maximum depth is 1; depth at END is 0. Neither program contains
loop, call, return, break, texkill, predicate/coissue tokens or output writes
inside any branch. The IFC has comparison control 5 (NE), two direct temporary
scalar operands with modifiers none/negate, and exactly one full-precision MOV
in its body. Original arithmetic initializes the tested scalar and assigned
scalar before the branch. The branch's effect, for finite sampled values, is
to clamp the detail-normal weight `1 - 2 * occlusion.red` at zero when negative.
Keep the native IFC byte-for-byte; an algebraic replacement would need separate
NaN/precision proof and is unnecessary for motion insertion.

| Original site | Damage 2s `31445adb0a62d134` | Damage `d51cf763125cb85a` |
| --- | ---: | ---: |
| IFC | 1436 | 1418 |
| Sole body MOV | 1439 | 1421 |
| IFC ENDIF | 1442 | 1424 |
| Final static ENDIF | 1701 | 1675 |
| Native RGB output | 1736 | 1710 |
| Native alpha output | 1741 | 1715 |
| END / proposed epilogue insertion boundary | 1745 | 1719 |

The original PS uses r0–r5 and v0–v7; motion can use r6–r8 and v8/TEXCOORD7,
with current depth in v9/TEXCOORD8 under the existing allocation. That leaves
no whole PS input for added material RGB. This motion-only feasibility does
not resolve material precision/lane packing.

A bounded implementation should prove exact hashes plus complete instruction
boundaries, the NE modifier/operand contract, its one-instruction body, balanced
joins, absence of other dynamic flow and unconditional epilogue reachability.
Keep all original instructions and output masks intact; append independently
initialized motion/depth temporaries after native alpha at END. No derivative
or implicit-LOD sample is introduced inside divergent control flow. This
respects Microsoft's [flow-control and gradient rules](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx9-graphics-reference-asm-ps-instructions-flow-control),
which require valid initialization on every path.

Qualification needs both IFC outcomes and a threshold boundary in adjacent
pixels of the same draw, all native b0/b1 combinations, 2s front/back faces,
alpha/occlusion/detail preservation, both depth modes and poisoned scratch
registers. Compare native RGB/alpha exactly where the original pair is valid;
verify motion/depth against the existing reference. Retain native shader
creation/budget checks, lifecycle/Reset/state recovery and a relevant performance
pass. Because all new motion executes after joins and does not sample textures,
there is no reason to broaden acceptance to arbitrary dynamic shader programs.

## Consequence for the existing negative material witness

The 110-pair live fixture uses D/fffd as the shared-VS, unsupported-material
negative. It remains useful as an X3 backend diagnostic, but its success cannot
establish portable/native-Windows fallback behavior for an invalid native pair.
A bounded query of **all six archive pairs sharing D** found only two valid
pairs: standard DEFAULT `7c83ed50c9894e44` and `e70adc744a38ca59`, both already
covered. The other four are precisely the invalid XT DEFAULT pairs above.
There is no valid uncovered class-C same-D witness in this archive inventory.

Keep the pending user A/B candidate unchanged. In the next scoped qualification
change, use a separate valid ordinary-motion family for the portable fallback
witness and retain the shared-VS pair-mask property as a host structural proof;
label D/fffd specifically as an X3 diagnostic. A DEFAULT repair should be a
separate reviewed contract, rather than quietly changing the meaning of native
fallback or claiming all 14 XT pairs complete from the ten matching pairs.

## Reproduction and limits

The bounded local analysis is `/tmp/x3-xt-linkage/query.py`, with derived effect
records `/tmp/x3-xt-linkage/effects.json`. It reuses the checked-in
[effect parser](../../tools/analysis/effect_passes.py), reads only XT SM3 archive
entries, and inspects the exact programs under `/tmp/x3-shader-sweep/programs/`.
Run it from the repository root with `python3 /tmp/x3-xt-linkage/query.py`.
The complete union is also reproducible from
`verification/results/motion-output-profiles.json` by selecting basenames
starting with `xt_` and deduplicating `(vs, ps)`. Original instructions can be
queried through `inspect_motion_output_profiles.instructions()`;
`profile()['declarations']` supplies semantic/index/mask fields for the subset
check. Original disassembly remains only under `/tmp/x3-shader-sweep/disassembly/`.

This study supplies exact linkage/branch findings and a next implementation
boundary. It does not establish a portable DEFAULT repair, new material
numerical contracts, transformed bytecode validity, native Windows behavior,
or gameplay/GPU cost.
