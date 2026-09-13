# Complete scene color-writer coverage

Current coverage ledger, 2026-09-14. This is the accounting boundary for the
user's requirement to convert **all necessary scene shaders**, including the
identities beyond the 162 exact pairs in the installed material route. It uses the
complete archive sweep and pass-pair inventory. It does not authorize a shader from its filename,
copy game bytecode into the repository, or make UI, depth and post-process
programs material shaders.

## Counting contract

The installed archives contain 3,480 compiled effects, 6,752 technique passes,
751 distinct complete shader programs (256 VS and 495 PS), and 817 distinct
pass identities: 814 complete VS/PS pairs plus three passes without a complete
counterpart. The complete pairs divide into 180 SM3, 466 SM2/2.x and 168 SM1
pairs. These are exact full-program identities; the 642 comment-stripped GPU
token variants are useful similarity evidence but are not replacement keys.

The installed linear-material implementation admits exactly
**162 pairs / 130 original shader stages**. It contains 20 Argon
DEFAULT/BUMPMAP, 20 shared Khaak/Teladi/Teladi_nodiff/Xenon DEFAULT/BUMPMAP,
20 Split DEFAULT/BUMPMAP, 20 Terran DEFAULT/BUMPMAP, 30 standard-lighting
DEFAULT/BUMPMAP/LOW, six Asteroid DEFAULT/BUMPMAP, 32 Boron/Paranid and 14 XT
DEFAULT/BUMPMAP/LOW pairs. Shared identities count once. This is the complete
162-pair conventional SM3 opaque/material union in the ledger; the six mixed-state
SM3 glass identities remain separate. Reviewed main source additionally contains
those six [glass conversions](glass-materials.md), bringing the source candidate
to **168 pairs / 137 originals**; GPU/live qualification and installation remain
pending for the glass extension. The
[Split/standard group](linear-standard-materials.md),
[remaining conventional hull group](linear-hull-materials.md),
[palette group](linear-palette-materials.md), [Asteroid group](linear-asteroid-materials.md)
and [XT group](xt-materials.md) retain the source and qualification boundaries;
gameplay appearance remains separate.

The installed default-off emission registry separately admits **20 exact SM2
pairs / 18 original shader stages**: eight native VS and ten PS. It covers all
384 SM2/2.x `effects`/`engine` archive occurrences across DEFAULT and INSTANCE.
Detached and live qualification cover all 20 pairs; gameplay appearance and
performance remain open, and actual submissions still need the qualified
scene/target/additive-state gates. The separately qualified nine-pair SM1 helper
is not linked into production and does not yet solve nonadditive ordered
composition.

Within that 20-pair registry, a fixed-width 16-hex join of the original five
high-quality DEFAULT profiles against the complete pair inventory finds all five
in `sm2_pairs`; none is outside the archive. The
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
| `argon` | A | 45 / 56 | 20 / 60 / 24 / 0 | D 52, B 52 | **20 converted and installed**: all SM3 D and B. The 84 SM2/SM1 pairs remain. |
| `asteroid` | B | 30 / 24 | 6 / 18 / 12 / 0 | D 18, B 18 | **6 SM3 pairs converted and installed** with base/detail semantics retained; 30 older-profile identities remain. Native blended far-fog draws remain outside opaque admission. |
| `bloom` | C | 18 / 29 | 9 / 20 / 0 / 0 | D 28, H 4 | Post-process contract. Do not apply material transfer to highlight masks, blur alpha or compositor math; handle as a whole under the existing bloom boundary. |
| `boron` | A | 45 / 40 | 12 / 36 / 24 / 0 | D 36, B 36 | **12 SM3 pairs converted and installed**; 60 older-profile identities remain. |
| `effects` | A | 12 / 12 | 0 / 16 / 6 / 0 | D 10, I 10, IB 2 | Emissive/transparent. **All 16 SM2 family pairs are in the installed default-off producer**; actual draws require the qualified additive state gate. The six SM1 family incidences are in the qualified deduplicated nine-pair helper but remain outside production composition/admission. |
| `engine` | A | 10 / 7 | 0 / 6 / 6 / 0 | D 5, I 5, IB 2 | Emissive/transparent. **All six SM2 family pairs are in the installed default-off producer**; actual draws require the qualified additive state gate. The six SM1 family incidences are in the qualified deduplicated nine-pair helper but remain outside production composition/admission. |
| `glass` | A | 18 / 16 | 6 / 12 / 12 / 0 | D 30 | [Reflective lit scene material](glass-materials.md); six SM3 pairs are implemented and reviewed in main source, with GPU/live/install pending; 24 older pairs remain unimplemented. Run 27 proves an opaque population for one SM3 pair and all effect defaults are opaque, but actual draw state remains authoritative. The six SM3 pairs can use opaque admission when its gates pass; any blended use needs the matching ordered-composition contract. |
| `gui2d` | B | 4 / 4 | 0 / 0 / 4 / 0 | D 2, I 2 | UI instances require no scene-material conversion. Two exact pairs are also `nebula`; exclude only from proved UI ownership, never by hash alone. |
| `khaak` | A | 45 / 56 | 20 / 60 / 24 / 0 | D 52, B 52 | **20 converted and installed**: SM3 D/B, shared with the next three listed shared families. The other 84 family pairs remain. |
| `moon` | B | 11 / 7 | 2 / 3 / 6 / 0 | D 11 | Background/surface material; all 11 remain. Its older COLOR varyings need their own HDR-preservation contract. |
| `nebula` | A | 2 / 2 | 0 / 0 / 2 / 0 | D 2 | Background/transparent; both pairs remain and are exact `gui2d` pair identities, so pass ownership is mandatory. |
| `nebulafog` | A | 2 / 2 | 0 / 0 / 2 / 0 | D 2 | Background/transparent lookup material; both remain. Volumetric meaning is unproved. |
| `paranid` | A | 45 / 56 | 20 / 60 / 24 / 0 | D 52, B 52 | **20 SM3 pairs converted and installed**; 84 older-profile identities remain. |
| `particles` | A | 2 / 2 | 0 / 0 / 2 / 0 | D 2 | Transparent SM1 billboards; both remain. Define radiance/coverage, composition and stable temporal identity together. |
| `planet_haze` | B | 3 / 3 | 1 / 0 / 2 / 0 | D 3 | Background/transparent lookup material; all 3 remain. Fixed 0.05 alpha is coverage, not emissive strength. |
| `planet_v` | A | 6 / 4 | 0 / 3 / 3 / 0 | D 6 | Background/surface material; all 6 remain. Over-one lighting is limited in older COLOR varyings, not by an SM3 PS clamp. |
| `split` | A | 45 / 56 | 20 / 60 / 24 / 0 | D 52, B 52 | **20 converted and installed**: SM3 D/B; 84 older-profile identities remain. |
| `standard_lighting` | A | 45 / 81 | 30 / 89 / 24 / 1 | D 52, B 52, BL 52 | **30 converted and installed**: SM3 D/B/BL; 113 complete older-profile pairs plus the incomplete pass remain. |
| `stardust` | B | 4 / 2 | 0 / 0 / 4 / 0 | D 2, I 2 | Background/transparent; all 4 remain. A shared GUI pixel program prevents PS-hash ownership decisions. |
| `teladi` | A | 45 / 56 | 20 / 60 / 24 / 0 | D 52, B 52 | Same complete 104-pair identity set as `khaak`; **20 shared SM3 D/B pairs converted and installed**, 84 remain. |
| `teladi_nodiff` | A | 45 / 56 | 20 / 60 / 24 / 0 | D 52, B 52 | Same complete set and installed coverage as `teladi`; the name does not prove diffuse is absent. |
| `terran` | A | 45 / 56 | 20 / 60 / 24 / 0 | D 52, B 52 | **20 converted and installed**: SM3 D/B; 84 older-profile identities remain. |
| `xenon` | A | 45 / 56 | 20 / 60 / 24 / 0 | D 52, B 52 | Same complete set and installed coverage as `khaak`; **20 shared SM3 D/B pairs converted and installed**, 84 remain. |
| `xt_standard_lighting` | D | 13 / 28 | 6 / 18 / 8 / 0 | D 12, B 12, BL 12 | **All six SM3 family pairs are converted and installed**, including authored DEFAULT linkage repair; 26 older-profile identities remain. |
| `xt_standard_lighting_damage` | D | 13 / 28 | 6 / 18 / 8 / 0 | D 12, B 12, BL 12 | **All six SM3 family pairs are converted and installed**, with damage `ifc` and shared DEFAULT/LOW identities preserved; 26 older-profile identities remain. |
| `xt_terraformer` | D | 13 / 28 | 6 / 18 / 8 / 0 | D 12, B 12, BL 12 | **All six SM3 family pairs are converted and installed**, including authored DEFAULT linkage repair and proved additional RGB roles; 26 older-profile identities remain. |
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
| Conventional opaque/material | 14 | 169 / 405 | 162 / 405 / 120 / 1 | 688 identities total; all 162 SM3 identities have installed material support. **526 remain outside the installed conversion**: 405 SM2/2.x, 120 SM1 and one incomplete pass. Gameplay appearance remains separate. |
| Mixed scene color/emission | 5 | 41 / 39 | 6 / 35 / 25 / 0 | 66 identities total. All 20 deduplicated SM2 `effects`/`engine` pairs have an installed default-off additive producer; **46 identities remain outside the installed route**; main source additionally implements six opaque glass pairs pending qualification. Nine SM1 `effects`/`engine` pairs have an isolated qualified producer but still need live composition/admission. The other 37 identities include opaque or blended glass plus particles and `adeffects`, so actual state and ownership select their conversion contract. |
| Background | 6 | 26 / 20 | 3 / 6 / 19 / 0 | 28 identities total. Convert surface/light math or compose transparency by role; two `nebula` identities are also UI identities. |
| Post/UI/depth | 3 | 26 / 35 | 9 / 20 / 6 / 2 | 37 identities total. Keep stock bloom under its post boundary, UI unconverted under proved ownership, and z-only conditional on state. |

The four group rows contain all 751 distinct programs and all 817 pass
identities, but they are not arithmetically disjoint: background and post/UI
share two exact `nebula`/`gui2d` pair identities. Eight program definitions are
shared across groups, producing ten extra group memberships, without necessarily
sharing the complete pair. The global totals above are therefore the
authoritative denominator.

Coverage is being closed in five bounded groups:

1. **Conventional SM3 opaque/material complete in production.** The installed 162-pair union includes Asteroid, Boron/Paranid and all [14 XT pairs](xt-materials.md), with
   separate DEFAULT, BUMPMAP and BUMPMAP_LOW contracts. Runtime appearance and
   state-gated cases such as [glass](glass-materials.md) remain separate evidence.
2. Continue the installed ordered linear-composition and temporal boundary for
   all 20 SM2 `effects`/`engine` profiles. Integrate the qualified
   [nine-pair SM1 producer](linear-emission-sm1.md) only after completing its
   required output/blend contract, including nonadditive populations. The 37 other mixed scene-color identities need their
   own ownership, blend and alpha decisions. Historical capture of every alias is
   not required: qualified shaders still need actual per-submission scene, target
   and state gates.
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
[current BUMPMAP material design](linear-bump-materials.md),
[emission composition contract](linear-emission-composition.md), and the
[glass material study](glass-materials.md).
