# Remaining effects/engine pass identities

Bounded archive study at commit `511e682`, 2026-09-13. This note accounts for
the deduplicated `effects`/`engine` pass identities outside the five exact
profiles already admitted by `linear_emission.cpp`. It derives shader behavior
from the existing complete sweep and targeted inspection of the local extracted
programs. It does not promote a pair from its family or technique name, and it
does not establish a live draw owner or blend state for an uncaptured pair.

## Count and exact inventory

The two families have **29 unique complete pairs** in union. Removing the five
installed `DEFAULT` pairs leaves **24 pairs**: 15 SM2/2.x and 9 SM1. Their
technique split is 8 `DEFAULT`, 13 `INSTANCE`, and 3 `INSTANCE_BULLETS`. They
reference 15 exact VS and 16 exact PS definitions (31 total), which collapse by
comment-stripped GPU-token identity to 6 VS and 8 PS executable bodies. The 24
rows represent **712 archive pass occurrences** across catalogues, aliases,
profile directories, and toggles. `effects` has 19 pair memberships and
`engine` has 9; four pairs are shared, so those family memberships must not be
added to obtain the 24-pair union.

The table is the complete remaining union. `E` means `effects`/`effects2s`,
`E0` means `effects_0000`/`effects_0001`, `G0` means
`engine_0000`/`engine_0001`, and `EG` means the four base aliases
`effects`, `effects2s`, `engine`, and `engine2s`. `all` toggles means base,
`hue_lights_off`, `hueshift_off`, and `v_lights_off`; `B/V` means base and
`v_lights_off`; `H` means `hue_lights_off` and `hueshift_off`. Every row is pass
`P0`, and every row has zero observed draws in the sweep's associated capture.

| # | VS / PS | Models | Technique | Aliases | Profiles | Toggles | Occ. | Contract |
| ---: | --- | --- | --- | --- | --- | --- | ---: | --- |
| 1 | `5b7a3ccd9e7df00a` / `9975b706e5a1c999` | 2_0 / 2_0 | INSTANCE | G0 | 2_0, 2_a, 2_b, 3_0 | B/V | 16 | AF |
| 2 | `5b7a3ccd9e7df00a` / `ff2473e73a6bdfa1` | 2_0 / 2_0 | INSTANCE | G0 | 2_0, 2_a, 2_b, 3_0 | H | 16 | RF |
| 3 | `6435a84d8ac5908e` / `39f3b4d5b6a5aaed` | 2_0 / 2_0 | INSTANCE | E0 | 2_0, 2_a | H | 8 | RF |
| 4 | `6435a84d8ac5908e` / `47e15e20d63b0e93` | 2_0 / 2_1 | INSTANCE | E0 | 2_b | H | 4 | RF |
| 5 | `6435a84d8ac5908e` / `846c5c1a549f9491` | 2_0 / 2_0 | INSTANCE | E0 | 2_0, 2_a | B/V | 8 | AF |
| 6 | `6435a84d8ac5908e` / `c6dacb8f74b65c97` | 2_0 / 2_1 | INSTANCE | E0 | 2_b | B/V | 4 | AF |
| 7 | `89193868c61c3846` / `8360f422de08b5bd` | 2_0 / 2_0 | INSTANCE | EG | 2_0, 2_a, 2_b, 3_0 | all | 112 | AF |
| 8 | `89193868c61c3846` / `f0c91793a75e1203` | 2_0 / 2_1 | INSTANCE | E | 2_b | all | 16 | AF |
| 9 | `a520be365951c9dc` / `8559522220507d5e` | 2_0 / 2_0 | INSTANCE | E0 | 3_0 | B/V | 4 | AN |
| 10 | `a520be365951c9dc` / `875e780adb131b16` | 2_0 / 2_0 | INSTANCE | E0 | 3_0 | H | 4 | RN |
| 11 | `cfb2c31707d545bc` / `39f3b4d5b6a5aaed` | 2_0 / 2_0 | DEFAULT | E0 | 2_0, 2_a | H | 8 | RF |
| 12 | `cfb2c31707d545bc` / `47e15e20d63b0e93` | 2_0 / 2_1 | DEFAULT | E0 | 2_b | H | 4 | RF |
| 13 | `cfb2c31707d545bc` / `846c5c1a549f9491` | 2_0 / 2_0 | DEFAULT | E0 | 2_0, 2_a | B/V | 8 | AF |
| 14 | `cfb2c31707d545bc` / `c6dacb8f74b65c97` | 2_0 / 2_1 | DEFAULT | E0 | 2_b | B/V | 4 | AF |
| 15 | `d5e1c75351ed3f04` / `f0c91793a75e1203` | 2_0 / 2_1 | DEFAULT | E | 2_b | all | 16 | AF |
| 16 | `0d44b36d48d24f7a` / `078494828322bcca` | 1_1 / 1_1 | DEFAULT | EG | 1_1, 1_4 | all | 64 | S1 |
| 17 | `1b6863a088a177af` / `84d3de8887c963c5` | 1_1 / 1_1 | INSTANCE_BULLETS | G0 | 1_1, 1_4, 2_0, 2_a, 2_b, 3_0 | all | 48 | B1 |
| 18 | `21a2c13be7f989c3` / `d4a26efb7c603931` | 1_1 / 1_1 | INSTANCE_BULLETS | E0 | 1_1, 1_4, 2_0, 2_a, 2_b, 3_0 | all | 48 | B1 |
| 19 | `5e484a06672e28fb` / `ec1f5c4a2f4e1445` | 1_1 / 1_1 | INSTANCE_BULLETS | EG | 1_1, 1_4, 2_0, 2_a, 2_b, 3_0 | all | 192 | B1 |
| 20 | `637dadcb5efa3288` / `078494828322bcca` | 1_1 / 1_1 | INSTANCE | EG | 1_1, 1_4 | all | 64 | S1 |
| 21 | `6da1b1b6ed63ec82` / `2ea025492d370c8e` | 1_1 / 1_1 | DEFAULT | E0 | 1_1, 1_4 | all | 16 | S1 |
| 22 | `88620f88d6e0a00e` / `a5c3495e27270b4a` | 1_1 / 1_1 | DEFAULT | G0 | 1_1, 1_4 | all | 16 | S1 |
| 23 | `ed42e0742e47dca4` / `2ea025492d370c8e` | 1_1 / 1_1 | INSTANCE | E0 | 1_1, 1_4 | all | 16 | S1 |
| 24 | `f9755e1154244f58` / `a5c3495e27270b4a` | 1_1 / 1_1 | INSTANCE | G0 | 1_1, 1_4 | all | 16 | S1 |

The six contracts account for every row:

| Contract | Pairs | Exact source behavior | Vertex/instance behavior |
| --- | ---: | --- | --- |
| AF | 8 (4 PS2.0, 4 PS2.1) | Sample `DiffuseTexSampler` s0; apply the native affine RGB transform from c0-c2 with homogeneous 1; multiply RGB by COLOR0.x; copy sampled alpha unchanged | Five INSTANCE and three DEFAULT pairs use an interpolated fade |
| RF | 5 (3 PS2.0, 2 PS2.1) | Sample s0; multiply sampled RGB by COLOR0.x; copy sampled alpha unchanged | Three INSTANCE and two DEFAULT pairs use an interpolated fade |
| AN | 1 PS2.0 | Apply the affine transform to sampled RGB; copy sampled alpha unchanged | INSTANCE passes direct UV and no fade value |
| RN | 1 PS2.0 | Copy sampled RGBA unchanged | INSTANCE passes direct UV and no fade value |
| S1 | 6 PS1.1 | Sample the same named diffuse texture; multiply RGB by COLOR0.x (`def c0=(1,0,0,0); dp3 c0,v0`); copy sampled alpha unchanged | DEFAULT applies `g_TexMatrix`; INSTANCE passes direct UV. Both derive COLOR0.x from `g_AlphaValue` and optional distance fog |
| B1 | 3 PS1.1 | Sample the same named diffuse texture; multiply RGB by vertex COLOR0.a; copy sampled alpha unchanged | The VS passes direct UV and the complete packed vertex color |

The texture is therefore consumed as artistic/color RGB plus independent
coverage alpha in all six contracts; none of these programs interprets s0 RGB
as a normal, lookup, or other data vector. This is program evidence. The
archive itself supplies no bound asset, texture format, hardware-sRGB state,
filter, mip bias, or effective material override. The source conversion keeps
the original sample and sampler state, so those uncaptured bindings do not need
an asset whitelist or a historical draw before implementation. The live route
must preserve them and continue to require hardware-sRGB sampling to be off.

The 31 exact definitions share these executable bodies:

- VS1 DEFAULT: `0d44b36d48d24f7a`, `6da1b1b6ed63ec82`,
  `88620f88d6e0a00e`; VS1 INSTANCE: `637dadcb5efa3288`,
  `ed42e0742e47dca4`, `f9755e1154244f58`; VS1 bullets:
  `1b6863a088a177af`, `21a2c13be7f989c3`, `5e484a06672e28fb`.
- VS2 DEFAULT: `cfb2c31707d545bc`, `d5e1c75351ed3f04`; VS2 faded
  INSTANCE: `5b7a3ccd9e7df00a`, `6435a84d8ac5908e`,
  `89193868c61c3846`; VS2 no-fade INSTANCE: `a520be365951c9dc`.
- PS1 scalar: `078494828322bcca`, `2ea025492d370c8e`,
  `a5c3495e27270b4a`; PS1 bullets: `84d3de8887c963c5`,
  `d4a26efb7c603931`, `ec1f5c4a2f4e1445`.
- PS2.0 raw-fade: `39f3b4d5b6a5aaed`, `ff2473e73a6bdfa1`;
  PS2.0 affine-fade: `8360f422de08b5bd`, `846c5c1a549f9491`,
  `9975b706e5a1c999`; the no-fade bodies are `8559522220507d5e`
  and `875e780adb131b16`. PS2.1 raw-fade is `47e15e20d63b0e93`;
  PS2.1 affine-fade is shared by `c6dacb8f74b65c97` and
  `f0c91793a75e1203`.

These equivalences are implementation evidence, not authorization to key a
runtime policy on a stripped-body hash. `DEFAULT`, `INSTANCE`, and
`INSTANCE_BULLETS` are archive technique labels. The VS code shows that the
INSTANCE forms use `g_mViewProjection` with direct input UV rather than the
DEFAULT texture matrix, and bullets forward packed vertex color. It does not
prove hardware instancing, batching lifetime, draw ownership, object type, or
blend class.

## Bounded runtime evidence

The sweep-associated capture records zero draws for every remaining row, but a
targeted query of the existing compact capture/motion summaries finds one later
exception: row 19, `5e484a06672e28fb` / `ec1f5c4a2f4e1445`, occurs in **54
main-scene draws across 12 iteration-06 frames**. No other remaining exact pair
appears in those summaries. All 54 row-19 draws use RGB ADD/ONE/INVSRCCOLOR,
Z test on, Z writes off, color mask 15, separate-alpha blending off, cull NONE,
and hardware sRGB texture/write conversion off. Alpha test is off for 35 draws
and on for 19; its captured function/reference are GREATEREQUAL/1. Thus this
observed bullet program is a screen-blend source, not an additive-source
admission candidate, and its stored alpha follows inherited non-separate blend
behavior despite the shader copying sampled alpha.

All 54 draws bind the same captured 512x512 DXT5 stage-0 resource with linear
minification, magnification, and mip filtering. The trace does not record a mip
bias field. The shader reads only stage 0; other still-bound stages are not
program inputs. Object context is unscoped/invalid for every one of these draws,
so the capture proves scene phase, geometry submission, texture/state, and the
exact program pair, but not an owning game object, selected archive technique,
or resource-to-effect alias. This distinction also prevents applying row 19's
screen state to the other two bullet identities.

The other 23 rows being absent from these historical summaries is a recorded
unknown population, not an implementation or qualification barrier. It neither
excludes the variants from the complete archive requirement nor requires a new
user capture for each alias before their exact shader support can be added.

## What the current route can reuse

The **15 SM2 pairs** have exactly the four RGB/fade shapes already implemented
for the installed five profiles. They can reuse the native-oC0-preserving source
split, sanitize/decode/gain calculation, oC1 emission, constant oC2 coverage,
owned A/B/E/C targets, ordered publication, recovery rules, and supplemental
temporal-mask consumer. Coverage must be written for every enhanced submission,
independently of RGB, fade, sampled alpha, and gain. This lets the existing
temporal policy reject current and disappearing effect footprints without
pretending the effect has stable object motion. The separate motion inventory
currently rejects 13 of these 15 SM2 pairs for its VS position-splice order;
only the two `a520be365951c9dc` pairs are hostable by that proposed motion
fragment. That does not block the conservative supplemental mask.

Reuse still needs exact profiles and native qualification. The current parser
admits only a PS2.0 version token and only the installed five exact pairs; six
remaining pairs are PS2.1. Each proposed pair needs a collision-free insertion
site and weighted budget, original-oC0 parity with added outputs, and the same
MRT capability qualification on both target platforms.

After that shader work, admission can use the existing five-profile live
boundary without stable object identity or an earlier observation of the pair.
At `BeginScene`, the route positively reads the qualified compositor/device
owner and records the scene thread. A draw-local permission then requires that
same frame and thread, no Reset/compositor/bloom/reentrancy conflict, and the
exact reviewed VS/PS pair with a successfully created augmented PS. Preparation
checks the actual submitted draw and current resources: indexed non-user-memory
geometry, active owned FP16 scene target, no MSAA or competing RT attachments,
compatible depth, idle queries/state blocks/readers, RGB ADD/ONE/ONE, Z test on,
Z writes off, alpha test/stencil/fog/dither/sRGB write off, full color mask, and
stage-0 hardware-sRGB sampling off. Capability and restoration failures refuse
or recover the transaction and invalidate temporal history as already defined.
Supplemental coverage is produced by the same accepted submission, so it needs
neither object motion nor replay identity.

Historical state remains useful for finding populations: the base pair's
scene-additive, screen-blend, and late-overlay observations explain why the
runtime state/scene gates are necessary, while row 19 proves one bullet screen
population. Those observations do not assign a blend class by family and do not
transfer to another pair. The lens-flare family/resource link likewise does not
select an exact permutation. Conversely, absence from capture does not block an
exact variant whose submitted draw satisfies every live gate.

This reuse covers the additive submissions selected by that gate. A screen,
alpha, or other blend population encountered under the same exact shader remains
outside the ordered additive route and needs its own radiance/coverage equation.
That separate composition work is a missing blend contract, while a historical
capture of every exact alias is not.

The **nine SM1 pairs** cannot reuse the same-draw producer: their native result
is PS1 `r0`, and they have no oC1/oC2 publication contract. The archive proves
their source math but supplies no way to retain native B while producing E and
coverage from the same submission. A later design must prove a portable extra
output or geometry-replay/alternate-writer boundary, with failure recovery and
ordering preserved. The three bullet pairs additionally require
blend-class-specific radiance/coverage contracts, preservation of the native
vertex-alpha multiplier, and a temporal coverage policy for their submitted
geometry; row 19 supplies one exact screen-blend population, while the other two
remain uncaptured and have no assigned blend class. Historical capture provides
no stable game-object identity or submission lifetime for any of the 24. Apart
from row 19, effective RGB/separate-alpha blend, depth/stencil, color mask, and
bound texture also remain unobserved. These are recorded unknowns. The SM1
output limitation is the actual implementation blocker; once a replacement
producer exists, its own live state and ownership predicates can admit
submissions without a stable game-object identity.

## Actionable next group

The next implementation group is **all 15 remaining SM2 pairs**, rows 1-15, in
one qualification cycle: 232 archive occurrences, six exact VS plus ten exact
PS definitions. They reduce to three VS and six PS executable bodies. The pixel
models are nine PS2.0 and six PS2.1; the technique axis is five `DEFAULT` and ten
`INSTANCE`. Together they cover all four already established source shapes: 8
affine-fade, 5 raw-fade, 1 affine/no-fade, and 1 raw/no-fade.

`INSTANCE` does not create a separate implementation or identity contract in
this batch. Its native VS uses direct input UV and either the existing fade or
no fade, while DEFAULT applies `g_TexMatrix`; the route preserves those vertex
programs unchanged, keys each exact VS/PS pair, and transforms only the proven
pixel source shape. The current draw route admits `DrawIndexedPrimitive` by its
actual arguments and live capability/state boundary. It neither needs the
archive technique name at runtime nor assumes hardware instancing, object
identity, or a batching lifetime. A future observed non-indexed or user-memory
population would require a draw-path extension, but the uncaptured INSTANCE
label alone is not evidence that such a path is used.

For the 15-pair batch, derive all exact transformer profiles, add a separately
reviewed PS2.1 structural path rather than weakening the PS2.0 validator, and
qualify every source shape's instruction budget plus native oC0/oC1/oC2
behavior. Admission continues to use the current per-submission scene owner,
target/capability, sampler-sRGB and complete additive blend/depth predicates; it
does not wait for each exact pair or alias to appear in a historical user run.
An implemented pair remains dormant on draws that fail those predicates. The
remaining structural groups are six SM1 DEFAULT/INSTANCE scalar-fade pairs and
three SM1 bullet pairs. Nonadditive screen/alpha blend populations remain under
their separate composition-contract work regardless of shader model.

Sources: [complete coverage ledger](../architecture/material-coverage.md),
[installed composition contract](../architecture/linear-emission-composition.md),
[earlier material slice](../architecture/material-next-slice.md),
[complete sweep](shader-sweep.md), [family review](shader-family-review.md),
[motion inventory](motion-output-profiles.md),
[iteration-06 capture analysis](../verification/iteration-06.md),
`verification/results/shader-sweep-inventory.json`,
`verification/results/shader-sweep-aliases.json`,
`verification/results/shader-sweep-families.json`, and
`verification/results/motion-output-profiles.json`. Targeted instruction facts
were derived locally from `/tmp/x3-shader-sweep/programs`; no game program or
decompiler output is tracked.
