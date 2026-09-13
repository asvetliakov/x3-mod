# Complete scene color-writer coverage

Current coverage ledger, 2026-09-13. This is the accounting boundary for the
user's requirement to convert **all necessary scene shaders**, rather than only
the 110 exact pairs in the installed material route. It uses the complete archive
sweep and pass-pair inventory. It does not authorize a shader from its filename,
copy game bytecode into the repository, or make UI, depth and post-process
programs material shaders.

## Counting contract

The installed archives contain 3,480 compiled effects, 6,752 technique passes,
751 distinct complete shader programs (256 VS and 495 PS), and 817 distinct
pass identities: 814 complete VS/PS pairs plus three passes without a complete
counterpart. The complete pairs divide into 180 SM3, 466 SM2/2.x and 168 SM1
pairs. These are exact full-program identities; the 642 comment-stripped GPU
token variants are useful similarity evidence but are not replacement keys.

The installed linear-material implementation admits exactly **110 pairs / 73
programs**: 20 Argon DEFAULT/BUMPMAP, 20 shared Khaak/Teladi/Teladi_nodiff/Xenon
DEFAULT/BUMPMAP, 20 Split DEFAULT/BUMPMAP, 20 Terran DEFAULT/BUMPMAP and 30
standard-lighting DEFAULT/BUMPMAP/LOW. Shared hull identities count once.
The [Split/standard group](linear-standard-materials.md) and
[remaining conventional hull group](linear-hull-materials.md) retain the complete
source, detached GPU and live-route qualification. Installation is complete;
gameplay acceptance remains separate. The five high-quality PS2 effects/engine
emission profiles are also installed behind their own default-off flag. Their
producer/component corpus and focused one-pair live integration are qualified;
gameplay and complete scene composition remain open.

A fixed-width 16-hex join of the five production profiles against the complete
pair inventory finds all five in `sm2_pairs`; none is outside the archive. The
leading-zero VS identity `089091aab2d5eb13` matches both `effects_0000/_0001`
pairs. Family incidence is therefore three for `effects` and three for `engine`:
the base pair is shared, two more are effects-only and two are engine-only,
giving five deduplicated pairs. The shared pair and both engine-only pairs carry
the motion inventory's `vs_position_issue_order_wxyz` feasibility refusal; the
two effects-only pairs are hostable by its proposed PS2 fragment. That
classification concerns the existing motion-output splice and does not remove
the pairs from archive coverage or invalidate the separately qualified emission
producer.

Every one of the 28 normalized families has aliases in all six effect profile
directories (`1_1`, `1_4`, `2_0`, `2_a`, `2_b`, `3_0`) and all four toggle
directories (base, `hueshift_off`, `hue_lights_off`, `v_lights_off`). The actual
token model, not the directory name, defines the shader contract. The matrix's
alias forms are:

- **A:** base, `2s`, `_0000`, `_0001`
- **B:** base, `_0000`, `_0001`
- **C:** base, `_0000`
- **D:** base, `2s`

The directory axes are fully represented rather than inferred from the `3_0`
programs already captured or converted:

| Alias axis | Effect aliases | Distinct effect bytes | Referenced complete programs | Families |
| --- | ---: | ---: | ---: | ---: |
| profile `1_1` | 580 | 403 | 146 | 28 |
| profile `1_4` | 580 | 458 | 126 | 28 |
| profile `2_0` | 580 | 468 | 185 | 28 |
| profile `2_a` | 580 | 460 | 186 | 28 |
| profile `2_b` | 580 | 448 | 211 | 28 |
| profile `3_0` | 580 | 454 | 211 | 28 |
| toggle base | 870 | 647 | 590 | 28 |
| toggle `hue_lights_off` | 870 | 648 | 590 | 28 |
| toggle `hueshift_off` | 870 | 650 | 590 | 28 |
| toggle `v_lights_off` | 870 | 649 | 590 | 28 |

Counts on different axes overlap by design. Technique coverage is likewise
explicit; `Passes` counts archive occurrences and `Pair memberships` counts
exact identities used by that technique:

| Technique | Passes | Pair memberships |
| --- | ---: | ---: |
| `DEFAULT` | 3,600 | 416 |
| `BUMPMAP` | 1,776 | 320 |
| `INSTANCE` | 480 | 17 |
| `BUMPMAP_LOW` | 384 | 68 |
| `INSTANCE_BULLETS` | 288 | 3 |
| `Z_Only_Alpha` | 96 | 2 |
| `Z_Only_Fast` | 96 | 2 |
| `HDR` | 32 | 4 |

These pair memberships overlap across techniques; the deduplicated global
denominator remains 817 pass identities.

`Programs` is the number of distinct complete VS/PS definitions referenced by a
family's aliases. `Pairs 3/2/1/+I` is the number of distinct SM3, SM2/2.x, SM1,
and incomplete pass identities associated with that family. Family rows overlap
when exact programs or pairs are shared, so neither column may be added down the
table. Technique counts are pair memberships and can overlap when one exact pair
is reused by more than one technique. They account for `DEFAULT` (D), `BUMPMAP`
(B), `BUMPMAP_LOW` (BL), `INSTANCE` (I), `INSTANCE_BULLETS` (IB), stock bloom
`HDR` (H), and `Z_Only_Alpha/Fast` (ZA/ZF).

## All-family matrix

| Family | Alias | Programs VS/PS | Pairs 3/2/1/+I | Technique membership | Current disposition |
| --- | :-: | ---: | ---: | --- | --- |
| `adeffects` | A | 4 / 5 | 0 / 3 / 2 / 0 | D 5 | Textured color with constant alpha; all 5 require scene-owner and blend-state proof before transparent/emissive conversion. |
| `argon` | A | 45 / 56 | 20 / 60 / 24 / 0 | D 52, B 52 | **20 converted**: all SM3 D and B. The 84 SM2/SM1 pairs remain. |
| `asteroid` | B | 30 / 24 | 6 / 18 / 12 / 0 | D 18, B 18 | Opaque/material candidate; all 36 remain, with detail-texture semantics retained. |
| `bloom` | C | 18 / 29 | 9 / 20 / 0 / 0 | D 28, H 4 | Post-process contract. Do not apply material transfer to highlight masks, blur alpha or compositor math; handle as a whole under the existing bloom boundary. |
| `boron` | A | 45 / 40 | 12 / 36 / 24 / 0 | D 36, B 36 | Opaque/material candidate; all 72 remain. Extra color varyings require a family profile. |
| `effects` | A | 12 / 12 | 0 / 16 / 6 / 0 | D 10, I 10, IB 2 | Emissive/transparent. The shared base pair plus two effects-only pairs are 3 of the deduplicated five-pair high-quality D work; these 3 incidences are implemented by the installed five-profile live route; 19 family pairs remain, and gameplay acceptance is pending. |
| `engine` | A | 10 / 7 | 0 / 6 / 6 / 0 | D 5, I 5, IB 2 | Emissive/transparent. The shared base pair plus two engine-only pairs are the other 3 incidences in that five-pair work; these 3 incidences are implemented by the installed five-profile live route; 9 family pairs remain, and gameplay acceptance is pending. |
| `glass` | A | 18 / 16 | 6 / 12 / 12 / 0 | D 30 | Transparent lit material; all 30 require linear destination/blend policy. The SM3 COLOR0 clamp is a candidate, not authorization. |
| `gui2d` | B | 4 / 4 | 0 / 0 / 4 / 0 | D 2, I 2 | UI instances require no scene-material conversion. Two exact pairs are also `nebula`; exclude only from proved UI ownership, never by hash alone. |
| `khaak` | A | 45 / 56 | 20 / 60 / 24 / 0 | D 52, B 52 | **20 converted**: SM3 D/B, shared with the next three listed shared families. The other 84 family pairs remain. |
| `moon` | B | 11 / 7 | 2 / 3 / 6 / 0 | D 11 | Background/surface material; all 11 remain. Its older COLOR varyings need their own HDR-preservation contract. |
| `nebula` | A | 2 / 2 | 0 / 0 / 2 / 0 | D 2 | Background/transparent; both pairs remain and are exact `gui2d` pair identities, so pass ownership is mandatory. |
| `nebulafog` | A | 2 / 2 | 0 / 0 / 2 / 0 | D 2 | Background/transparent lookup material; both remain. Volumetric meaning is unproved. |
| `paranid` | A | 45 / 56 | 20 / 60 / 24 / 0 | D 52, B 52 | Opaque/material candidate; all 104 remain. Additional color/view mixing needs a family profile. |
| `particles` | A | 2 / 2 | 0 / 0 / 2 / 0 | D 2 | Transparent SM1 billboards; both remain. Define radiance/coverage, composition and stable temporal identity together. |
| `planet_haze` | B | 3 / 3 | 1 / 0 / 2 / 0 | D 3 | Background/transparent lookup material; all 3 remain. Fixed 0.05 alpha is coverage, not emissive strength. |
| `planet_v` | A | 6 / 4 | 0 / 3 / 3 / 0 | D 6 | Background/surface material; all 6 remain. Over-one lighting is limited in older COLOR varyings, not by an SM3 PS clamp. |
| `split` | A | 45 / 56 | 20 / 60 / 24 / 0 | D 52, B 52 | **20 converted and installed**: SM3 D/B; 84 older-profile identities remain. |
| `standard_lighting` | A | 45 / 81 | 30 / 89 / 24 / 1 | D 52, B 52, BL 52 | **30 converted and installed**: SM3 D/B/BL; 113 complete older-profile pairs plus the incomplete pass remain. |
| `stardust` | B | 4 / 2 | 0 / 0 / 4 / 0 | D 2, I 2 | Background/transparent; all 4 remain. A shared GUI pixel program prevents PS-hash ownership decisions. |
| `teladi` | A | 45 / 56 | 20 / 60 / 24 / 0 | D 52, B 52 | Same complete 104-pair identity set as `khaak`; **20 shared SM3 D/B pairs converted**, 84 remain. |
| `teladi_nodiff` | A | 45 / 56 | 20 / 60 / 24 / 0 | D 52, B 52 | Same complete set and coverage as `teladi`; the name does not prove diffuse is absent. |
| `terran` | A | 45 / 56 | 20 / 60 / 24 / 0 | D 52, B 52 | **20 converted and installed**: SM3 D/B; 84 older-profile identities remain. |
| `xenon` | A | 45 / 56 | 20 / 60 / 24 / 0 | D 52, B 52 | Same complete set and coverage as `khaak`; **20 shared SM3 D/B pairs converted**, 84 remain. |
| `xt_standard_lighting` | D | 13 / 28 | 6 / 18 / 8 / 0 | D 12, B 12, BL 12 | Extended opaque material; all 32 remain. Bump, occlusion/detail and static branches need an explicit contract. |
| `xt_standard_lighting_damage` | D | 13 / 28 | 6 / 18 / 8 / 0 | D 12, B 12, BL 12 | Extended opaque material; all 32 remain. Two SM3 dynamic-`ifc` pairs also lack the present temporal route. |
| `xt_terraformer` | D | 13 / 28 | 6 / 18 / 8 / 0 | D 12, B 12, BL 12 | Extended opaque material; all 32 remain. Additional sampled RGB needs a separately proved role. |
| `z_only` | B | 4 / 2 | 0 / 0 / 2 / 2 | ZA 2, ZF 2 | Provisional depth/alpha-test scope. Apply no color conversion only when draw state proves color is irrelevant; otherwise retain as unresolved. |

The family matrix accounts for every exact alias family, lower profile and
toggle directory, shader model and technique. It deliberately does not call all
family uses scene materials: race/object names remain hints until a draw or
resource link establishes the runtime role.

## Deduplicated work groups

These unions provide implementation-sized totals without adding overlapping
family rows:

| Group | Families | Unique programs VS/PS | Exact pass identities 3/2/1/+I | Status and boundary |
| --- | ---: | ---: | ---: | --- |
| Opaque/material | 14 | 169 / 405 | 162 / 405 / 120 / 1 | 688 identities total; 110 are installed. **578 await conversion**: 52 SM3 and 526 SM2/SM1/incomplete. Gameplay acceptance remains separate. |
| Emissive/transparent | 5 | 41 / 39 | 6 / 35 / 25 / 0 | 66 identities total. The five-profile `effects`/`engine` high-quality D implementation is installed; 61 identities still need conversion/composition, and gameplay is pending; glass, particles and `adeffects` need distinct blend/coverage policies. |
| Background | 6 | 26 / 20 | 3 / 6 / 19 / 0 | 28 identities total. Convert surface/light math or compose transparency by role; two `nebula` identities are also UI identities. |
| Post/UI/depth | 3 | 26 / 35 | 9 / 20 / 6 / 2 | 37 identities total. Keep stock bloom under its post boundary, UI unconverted under proved ownership, and z-only conditional on state. |

The four group rows contain all 751 distinct programs and all 817 pass
identities, but they are not arithmetically disjoint: background and post/UI
share two exact `nebula`/`gui2d` pair identities. Eight program definitions are
shared across groups, producing ten extra group memberships, without necessarily
sharing the complete pair. The global totals above are therefore the
authoritative denominator.

Implementation should proceed in five bounded groups:

1. Finish the [52 remaining SM3 opaque identities](../reverse-engineering/remaining-sm3-opaque-materials.md) through exact per-program
   profiles, grouped by common algebra and ABI. Preserve separate DEFAULT,
   BUMPMAP and BUMPMAP_LOW contracts, and add a temporal answer for the two
   dynamic-branch damage pairs before calling them complete.
2. Validate gameplay for the installed ordered linear-composition and temporal
   boundary of the five high-quality `effects`/`engine` profiles, then extend it to
   the remaining 24 exact `effects`/`engine` identities and the 37 other
   transparent/emissive identities only after their blend and alpha roles are
   proved.
3. Add the separate older-profile opaque path for 405 SM2/2.x, 120 SM1 and one
   incomplete identity. This requires model-specific varying/output and TAA
   handling; absence from captures is not an exclusion.
4. Resolve the 28 background identities by resource/draw ownership, then apply
   opaque conversion or transparent composition as appropriate. Shared GUI
   programs require a pass capability/owner boundary.
5. Close the 37 post/UI/depth identities with explicit non-material decisions:
   bloom as a post-process unit, UI after proved UI ownership, and z-only after
   color-write/alpha-test state proof. Reclassify any contrary runtime use into
   the relevant scene group rather than blanket-patching or blanket-excluding its
   shader hash.

Completion means every one of the 817 pass identities has one proved outcome:
converted opaque material, converted ordered transparency/emission, converted
background, or a justified post/UI/depth exclusion. It does not mean copying all
751 programs into production. Shared identities require runtime capability and
ownership gates; uncertain roles remain explicitly open until targeted resource
linking or draw-state reverse engineering resolves them.

Sources: [complete shader sweep](../reverse-engineering/shader-sweep.md),
[family review](../reverse-engineering/shader-family-review.md),
[motion pair inventory](../reverse-engineering/motion-output-profiles.md),
[current DEFAULT material design](scene-linear-materials.md),
[current BUMPMAP material design](linear-bump-materials.md), and
[emission composition brief](material-next-slice.md).
