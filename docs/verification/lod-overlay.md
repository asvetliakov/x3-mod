# Merged-LOD overlay baker: verification ledger

Baker `tools/analysis/lod_overlay.py`, atlas `tools/analysis/lod_atlas.py`, census
`tools/analysis/lod_batch_census.py`; design and history in
[merged-lod-feasibility.md](../architecture/merged-lod-feasibility.md). Append new outcomes below.
Nothing in this ledger ran in game or under Wine unless it says so.

## 2026-09-24: engine texture lookup and texture animations in the resolver

Change. `lod_atlas.lookup` and `texture_source` now follow
[texture-lookup.md](../reverse-engineering/texture-lookup.md) §9–§11:

- The `tex/<stem>` jpg/tga/bmp step is replaced.
- A file-less name bakes its suffix placeholder instead of being refused.
- A `-N` name is `types/Animations` row N: `parse_animations` and `animation_frame` bake the row's
  starting frame and never load `dds/-N`.
- A face group with index `-N` is mapped onto the first effect material whose diffuse is `-N`,
  else material 0 (`animated_record`); hidden parts keep `-N`.
- New refusal `texture_animation_unsupported`: TAT_MOVIE (-81), TAT_TAGSINGLESTEP (-67), a
  non-zero start UV offset, an animated group that would stay out of the atlas (alpha or kept),
  and a material-0 fallback that is not an effect material.
- New refusal `texture_generated`: a `MPF_GENERATED` Materials row, whose surface is drawn at run
  time.
- An id past the Materials rows is refused `texture_unresolved`.
- Name → id follows §2 exactly: the extension drop recurses, so the NULL test runs again (`NULL.dds` is no
  texture) and a double extension drops twice (`strip_texture_name`).
- The Animations row of every mapped `-N` group is validated, on the material-0 fallback too
  (review F1): a movie, single-step, offset, missing or past-table row refuses instead of baking material 0.
- `sector_fog_census.Assets` also scans loose `tex/` and `textures/`.

The start offset is refused, not applied: how the instance UV matrix reaches the draw is not
traced (texture-lookup.md, Unknown). In vanilla only -81 has a non-zero start, and -81 is refused
as a movie anyway.

Bottle X3 was read only. There was no install and no fleet rebake, and the game was not running
during any run.

| check | command | result |
|---|---|---|
| unit tests (measured) | `PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_lod_overlay_batch verification.analysis.test_lod_batch_census verification.analysis.test_bob1` | 91 OK, 1 skipped (after the review fixes). New cases include `NULL.dds` / double extension and the row check on the material-0 fallback (-81, -67, -5 offset, -200 past the table). `TextureLookup`: null names, numbered id / named row / tex/true, chain order, slot and loose precedence, placeholders, `\Desktop\`, generated and past-rows refusals. `TextureAnimations`: parser, start frames, -79 → fx_engine_blue1_diff, -81 / -67 / offset refused, group resolution incl. material-0 fallback and hidden parts. `RefusalClasses`: 25_spec/25_bump bind the diffuse; a -79 group bakes b_diff's pixels, not dds/-79's; -81 refused |
| other importers (measured) | `… test_fog_family_file test_fog_families`; `sector_fog_census.py --self-test` | OK (run with test_bob1 before the review fixes: 61 OK, 1 skipped); 29 checks passed |
| RE expectations (measured) | `texture_lookup_rows_check.py`, `_out.txt`: lookup of the 70 slot names in `texture_lookup_rows_out.txt` | 70 / 70 match (37 loads, 33 placeholders) |
| Animations parser (measured) | `animation_start_check.py`, `_out.txt`, against `texture-lookup-animations/animation_rows_out.txt` | 106 rows, no trailing tokens; all 26 referenced rows match on type, frame count and start frame; only -81 has a non-zero start offset (0.25, 0.25) |
| full census (measured) | `lod_batch_census.py --out <scratch> --jobs 8`, before (ff70bca8) and after the review fixes, about 135 s each | see below (`texture_lookup_census_compare.py`, `_out.txt`) |
| names old → new (measured) | `texture_lookup_old_new.py`, `_out.txt` | 3,452 distinct slot names: 2,940 same, 485 refused before, 27 loaded before and changed |
| changed names in atlases (measured) | `changed_names_in_atlas.py <census>`, `_out.txt` | no changed diffuse / light / bump / specular name reaches an eligible body's record-0 atlas |
| Khaak atlas (measured) | `lod_overlay.py --batch --only <Khaak_M6Main> --out <scratch> --jobs 1` | specular tiles that share the diffuse source: mean \|spec − diff\| RGB 0.00 (controls 62.2 / 80.4); same-source bump tiles 2.12 / 1.99 (controls 27.7–80.7) (`khaak_spec_bump_check.py`, `_out.txt`) |

**Census, before → after (2,453 rows).**

| reason | before | after |
|---|---|---|
| texture_unresolved | 44 | 0 |
| material_outside_table | 185 | 16 |
| texture_animation_unsupported | 0 | 168 |
| texture_generated | 0 | 1 (hud_icon_text: `340`) |
| all others (ambiguous_body_ext 47, excluded_effect 32, mat3 158, no_diffuse 5, no_opaque 651, non_effect_material 295, occlusion_mismatch 4, parse_error 1, text_parse_error 6) | same | same |
| eligible | 616 | 622 |

The six newly eligible bodies are Argon_hybrid, Khaak_M6Main, Khaak_M6Sec, khaak_M3 (its only
negative group sits in a hidden part), atf_m3 and atf_m3p. No body lost eligibility.

The 13 committed `texture_unresolved` rows:

| rows | after |
|---|---|
| Argon_hybrid, Khaak_M6Main, Khaak_M6Sec, atf_m3, atf_m3p | eligible |
| the four XTC_terran_dock_quicklaunch doors | refuse=- filter=no_draw_gain |
| xenon_m4 | refuse=ambiguous_body_ext |
| stations/docks/test | refuse=ambiguous_body_ext filter=no_draw_gain |
| hud_icon_text_x | refuse=- filter=category_other,no_draw_gain |
| asteroid_B_ClassPleasureComplex | refuse=- filter=category_other |

**Texture animations.**

- The 130 census rows with a `-79` material were 124 `material_outside_table` + category_other, 2
  ambiguous_body_ext, 2 eligible, 1 no_draw_gain and 1 `material_outside_table`. They are now:
  - 124 `texture_animation_unsupported` + category_other: the fx_engine glow group is alpha, so it
    would leave the atlas and lose its animation;
  - 2 ambiguous_body_ext with the same animation refusal;
  - 3 eligible (khaak_M3 added);
  - 1 no_draw_gain.

  126 rows have one animated record-0 group each; none falls back to material 0.
- The 39 rows with a `-81` material: 28 eligible (their `-81` material is not in the record-0
  atlas) and 11 refused `texture_animation_unsupported` as TAT_MOVIE. Before, those 11 were
  `material_outside_table`. Since F1 the row is checked before the material mapping, so these rows
  carry no `anim_*` columns.
- The 16 remaining `material_outside_table` rows have a positive group index past the table.

**Regressions against the committed census** (`texture_lookup_old_new_out.txt`,
`changed_names_in_atlas_out.txt`). No name that loaded a file before now bakes black.

- The negative ids now load their start frames, for example `-79` → `fx_engine_blue1_diff`,
  `-73` → `exp_sparks1_diff`. `-81` is an explicit refusal.
- `18.jpg`, `61` and `61.jpg` (MPF_GENERATED rows) are refused `texture_generated`.
- One name changes in a diffuse slot: the `…\Desktop\…\exp_impact_glow_diff` diffuse becomes
  NONE_GRAY, which is what the engine draws (§11). It sits in 2 bodies, in neither's record-0 atlas
  set, so it is baked nowhere.
- The other Desktop names are occlusion decals and one cube map. The atlas material keeps them by
  name, so they are not baked.

`texture_unresolved` now remains only for a numbered name without a readable Materials or
Animations table, an id past the Materials or Animations rows, and a placeholder that is in no
catalogue. Vanilla has none. `numbered_names_scan.py` / `_out.txt` checked the 2,346 bodies bob1
parses: 934 numbered slot names load; the others are 208 negative (now animations), 1,063 `0` and
101 MPF_GENERATED (now refused, e.g. `340`).

All census, old/new and atlas figures above are measured on the bottle's vanilla catalogues. That the
engine draws the start frame, the placeholders and nothing for a texture-id-0 row is inferred from
the static study (texture-lookup.md); nothing ran in game.

**Tool change (2026-09-24).** `lod_atlas.py`, `lod_overlay.py` and `lod_batch_census.py` are in
`lod_overlay.TOOL_FILES` (with `bob1.py`), so `tool_sha256` changes with this commit. The next
`lod_overlay.py --batch --sync` therefore rebakes every overlay body instead of reusing it. That rebake
and its install are a user decision and were not run here (inferred from `tool_sha256`; not run).

**Open issues.**

- F6: `sector_fog_census.Assets.candidates` returns the `addon/` key's entries alone when any exist.
  A mod catalogue rebased into `addon/` therefore hides a base-game loose file of the same path,
  including the loose `tex/` and `textures/` entries this change adds. The engine's loose-first rule
  would let the loose file win. Vanilla has no loose texture folders (measured, texture-lookup.md
  §6), so no census row is affected.
- The fx_engine glow groups (alpha) stay refused: keeping `-N` on an unatlased group would need a
  separate output group per animated material.
- A light-bleed keep of an animated material now refuses the body.
- The TexAnim UV animation of effect materials (flag 0x8000000) is not modelled.

## 2026-09-24 Run 79 A: the coarse Terran record loses its red (run299/300/302)

Triage `verification/results/run299-303-run79a/terran-colour/` (measured unless marked). The Orbital Defence Station is
the five slot-06 bodies `usc_dock_e_{tower,upper_core,lower_core,left_rings,right_rings}`. Record 0 carries the red as
its own texture (`terran_platesheet_red_diff`, material 1, 10.8 % of the tower's area); materials 1-4 (red, dark, tech,
window plates) have alpha test and blend on with a 255 diffuse alpha and `NONE_WHITE` as alpha texture, so the baker's
alpha rule (`lod_overlay.py` `alpha_materials`) puts them into the single alpha group drawn with the dominant material's
textures (`terran_techsheet_diff`, grey): the red faces keep their UVs and sample the techsheet. Wrong-texture share:
tower 23.1 %, left_rings 12.4 %, upper_core 4.8 %. Pixel-shader constants identical fine vs coarse; palette branch off
(b1 = 0); no owner colour. The "circle" (left_rings, run300) and the run302 part (upper_core, inferred) are the same
mechanism; no atlas bleed (each Terran atlas has one tile). Second loss: coarse draws bind the `NONE_OCCL_DECAL`
placeholder (32x32 DXT5, id 9186) at s5 where the fine draws bind the station's 2048^2 occlusion map (id 11498), so
the coarse record also loses its ambient-occlusion darkening (cause: see the follow-up below). Fix (baker only): count a material as alpha only when its alpha can drop below 1 (a real
`t_AlphaTexture` or diffuse alpha < 255); materials 1-4 then join the opaque atlas as tiles; draw count unchanged; atlas
likely 1024 -> 2048 for these bodies (inferred); full rebake required (the resolver change already changed the tool hash).

Follow-up, static study ([texture-lookup.md](../reverse-engineering/texture-lookup.md) §12): the occlusion loss is
engine behaviour, not baker data. `0x004c0150` binds a material's `t_OcclusionTexture` only when the node's LOD index
`node+0x14c` is 0 (`0x004c34ea`/`0x004c34f7`); every other record gets `NONE_OCCL_DECAL` (`*0x00606f74`; 32x32 DXT5,
RGB 0, alpha 255, i.e. no occlusion). Measured over the whole run299/300/302 logs (`occl_lod_census.py`): 1,174 / 768
/ 592 XT draws with LOD > 0 all bind that one texture, 48 / 48 / 32 of them on vanilla bodies outside the overlay; no
LOD-0 draw binds it. The coarse material's occlusion name, SPTYPE and strength equal material 0's, and the coarse
record's UV2 equals record 0's unwrap at every point (`occl_material_bytes.py`, `occl_uv2_match.py`). No material
change can bind the real map at record 1. The options are a unique-unwrap bake of the coarse textures with the
occlusion folded in, or a 6-byte EXE patch of the `jne` at `0x004c34f7` (§12.3).

## 2026-09-24: alpha rule fix (flagged materials with alpha 1 are atlased)

Change. `lod_overlay.alpha_materials(mats, assets, record=...)` keeps the flag rule (alpha test or blend on) and adds
a second condition: the flags must be able to change the image. A flagged material is alpha when:
- its alpha can drop below 1 (`alpha_can_drop`): `g_AlphaValue` < 1.0; a diffuse whose alpha is not 255 at every
  mip; with a non-zero `g_EnableGlow` parameter, a light map whose alpha is not 255 at every mip; a
  `t_AlphaTexture` the engine binds (file or suffix placeholder) that is not 255 in all four channels at every mip;
  a texture that does not resolve or decode (incl. a Pillow `OSError`) counts as varying; results cached per
  decoded texture;
- or its blend is not neutral at alpha 1 (`blend_neutral_at_one`: blend on with anything but `g_BlendOp` ADD,
  SRCALPHA/ONE over INVSRCALPHA/ZERO), or `g_ZWriteEnable` is 0;
- or (with the record; `occlusion_outliers`, coordinator decision) its occlusion map is not its effect's atlas
  occlusion map, the maps of the effect's unflagged opaque materials. When an effect has no unflagged opaque
  material, its flagged candidates are atlased only if they all carry one occlusion map, else all stay alpha (the
  body stays `no_opaque` as under the flag rule). No candidate is picked to set the map, so no plate of another
  map starts borrowing a diffuse (the toruswreck bodies).

The rest join the opaque atlas as their own tiles. When the atlas dominant is such a flagged material,
`lod_atlas.atlas_material` writes the atlas material with `g_AlphaBlendEnable` and `g_ALPHATESTENABLE` 0 (and
`t_AlphaTexture` NULL like every atlas material), so a tile of another opaque material whose diffuse alpha is below
255 is neither blended nor cut out. `lod_batch_census.py`, `body_materials.census` and `atlas_census.py` pass the
assets and the record. Without assets `alpha_materials` is the flag rule (the earlier verification scripts that call
it so are unchanged).

Basis and what is inferred. The effects' output alpha is `AlphaValue x (EnableGlow ? LightMap.a : Diffuse.a)`, the
alpha map is not in it, and 200 of 240 passes set blend ADD and alpha test GREATEREQUAL ref 1
([station-material-distance.md](../reverse-engineering/station-material-distance.md), "Shader and effect contracts":
the 88-effect SM3 archive scan). At alpha 1 the test passes and source-over returns the source colour. Not verified:
the `xt_standard_lighting_damage.fx` family and the 40 suffixed passes that omit the fixed states; the run-time
`g_EnableGlow` value, which the engine sets per draw from the glow option (`0x004c36a5..0x004c380d`) while the rule
reads only the material parameter (the Terran plates draw visibly with a `NONE_BLACK` light map, alpha 0, so their
blended draws cannot use the light-map alpha: inferred). The alpha-map check is kept as a conservative extra.
Bottle X3 read only; scratch bakes under `--out` only; no install, no `--sync`; the game was not running.

| check | command | result |
|---|---|---|
| unit tests (measured) | `PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_lod_overlay_batch verification.analysis.test_lod_batch_census verification.analysis.test_bob1` | 99 OK, 1 skipped. New `AlphaRule`: NONE_WHITE alpha map + 255 diffuse -> own atlas tile, no alpha group; a flagged atlas dominant over a cut-out tile -> atlas material with blend and test 0, alpha map NULL; glow on with a light map alpha below 255 -> alpha (glow off: opaque); Pillow `OSError` -> alpha; real alpha map -> alpha group; diffuse alpha < 255 -> alpha group; `g_AlphaValue` 0.5, unresolved alpha map, additive blend, REVSUBTRACT, no depth write -> alpha; NULL alpha map -> opaque; occlusion map other than the unflagged material's -> alpha and the collapse does not refuse; same map (case-insensitive) -> atlased; all flagged with two maps -> all alpha (either face count); all flagged with one map -> atlased |
| 16 Terran bodies, bake before/after (measured) | `lod_overlay.py --batch --only terran-colour/terran16.txt --out <scratch>` at 1266cf4e and with the fix (76 s / 84 s; list generated from the pinned census, `terran-colour/commands.txt`); re-baked after the review fixes: all 80 members byte-identical to the earlier after-bake; `terran-colour/alpha_rule_bake_compare.py`, `_compare.txt` | alpha group now material 5 alone (terran_alphasheet) on the 15 dock/small/spp bodies; terran_spp_panel unchanged; atlas side 1024 on all 16 before and after; tiles per atlas 1 -> 4 or 5; min texels/px at 1800 wide e.g. tower 4.08 -> 2.02, spp_center 4.39 -> 2.00 (all >= 2.0); draws below T_pad 41 -> 45 |
| draws | same bake, marker `split_groups` | the four extra draws (core_bottom 3 -> 4, c_right_arm 2 -> 3, e_upper_core 2 -> 3, spp_center 3 -> 4) are the 60,000-point split of the larger atlas group (`MAX_GROUP_POINTS`), not new materials |
| sizes (measured) | same | stored atlas bytes 19.42 -> 17.24 MB (gzip DDS; more, smaller-scale tiles compress better); body members 83.21 -> 83.43 MB; dat 102,626,033 -> 100,670,359 B; census uncompressed atlas estimate unchanged (4.89 MB per 1024 body, four slots) |
| red tile (measured) | `terran-colour/atlas_tile_check.py <after bake>`, `atlas_tile_check.txt`: written diffuse atlas decoded, each tile vs its own source area-resampled over its span, other tiles' sources as controls | 80 tiles; own source closest on 78 (the other 2 are spp_panel tiles of one shared texture); max own mean \|RGB diff\| 5.95; the 11 `terran_platesheet_red_diff` tiles 1.92-2.40 against >= 30.25 for any other source (tower: red 1.92, techsheet 41.71) |
| full census (measured) | `lod_batch_census.py --out <scratch> --jobs 8` at 1266cf4e and with the fix after the review fixes, 145 s / 156 s; `lod-overlay-batch/alpha_rule_census_compare.py`, `_out.txt`; reason counts from `texture_lookup_census_compare.py` | 2,453 rows. alpha set shrinks on 104 rows (85 eligible after, 82 eligible before and after); grows on none. Eligible 622 -> 625: +3 (toruswreck_middle_front_antennas, toruswreck_ring_outer, toruswreck_tower_left: every material flagged, one occlusion map), none lost; terran_TL_atmolifter's row is identical to 1266cf4e (its four flagged materials carry the other occlusion map and stay alpha). no_opaque 651 -> 648, occlusion_mismatch 4 -> 4 (the same four bodies); every other reason unchanged. The 16 Terran rows are identical to the census before the occlusion condition; against the previous after-census only the nine outlier toruswreck rows differ (back to no_opaque). Atlas side at 1920 unchanged on 621 of 622 bodies eligible in both; XTC_terran_tp_plus 1024 -> 2048 (+14.68 MB uncompressed); all eligible +29.35 MB (census estimate, uncompressed four slots). C draws summed over bodies eligible in both 1510 -> 1518 (18 bodies more, 10 fewer) |
| no correct diffuse lost (measured) | `lod-overlay-batch/alpha_rule_diffuse_check.py <before> <after>`, `_out.txt` (resolved member each record-0 group samples in C, old vs new rule; for a newly eligible body "before" is the vanilla LOD, every group on its own diffuse; 20 s) | no body gains new borrowing (0 groups own -> other in either class). Eligible before and after, 82 bodies: 230 groups / 336,810 faces regain their own diffuse, 3 groups / 66 faces still borrow (remaining alpha groups), 211 groups unchanged own; newly eligible, 3 bodies: 28 groups / 39,686 faces, all on their own diffuse |

Open. The coarse draws still bind
`NONE_OCCL_DECAL` at s5 (previous section; not addressed here). The fix reaches the game only with a full rebake
(`tool_sha256` changes), which is the user's decision.

## 2026-09-24: fleet rebake installed (install-fleet3)

Full `--sync` rebake at 776d1c52 (the three changes above), installed over install-fleet2 into addon/05 + 06; the
game was not running (pgrep before, during and after; the baker's guard). Record, bake summary and checks:
[`install-fleet3/`](../../verification/results/lod-overlay-batch/install-fleet3/install.json).

| check | command | result |
|---|---|---|
| rebake + install (measured) | `python3 tools/analysis/lod_overlay.py --batch --sync --install --replace --jobs 2`; `installed_checks.py` (reads installed members by seek) | 48.5 min wall. Eligible/built 611 -> 620 (census 625 minus the same 5 bake-time texel_floor bodies); the 9 new bodies are the 6 from the resolver fix and the 3 toruswreck bodies. Refusal counts as the census predicted: texture_unresolved 44 -> 0, texture_animation_unsupported 168, texture_generated 1, material_outside_table 185 -> 16, no_opaque 651 -> 648. addon/05 493 bodies 1,992,076,927 B, addon/06 127 bodies 732,092,975 B (both below the 2e9 cap and 2^31-1); installed cat/dat sha256 equal to the markers' overlay_sha256; EXE sha256 unchanged. usc_dock_e_tower: 4 tiles, red plate its own tile at 1.92 (controls >= 41.71); 54 installed Terran bodies: 35 red tiles 1.81-2.40 (controls >= 30.25), own source closest on 231/231 tiles. Khaak_M6Main spec = diffuse 0.00 on the 5 same-source tiles, bump 2.12/1.99 on the 2 same-source tiles (matches the scratch bake). Not flown |

## 2026-09-24 Run 80 A: the merged material's constants

The coarse record of `usc_dock_e_tower` (record 1, material 6 on atlas 2149, draws #11/#12 = 60,928 faces =
groups m0+m1+m3+m4 of record 0) draws its red plates 1.41x brighter than record 0 (red RGB 0.344 -> 0.485; non-red
0.93x; measured on the run305 bursts 12714 fine / 13309 coarse, different views so indicative;
`run304-305-run80a/terran-colour/red_plate_stats_run305.txt`). The atlas tiles are right (all 20 ODS tiles closest to
their own source, red tiles within 1.88-1.96; `atlas_tile_check_dock_e_installed.txt`) and the occlusion map is the
same at both records. The cause is the synthesised material: `is_light_param` (`lod_overlay.py:824`) and the
area-weighted loop (`:846-860`) average only `g_Mat*` floats and take everything else from the dominant material, so the
red/tech/window tiles get white's constants: `g_MatSpecularStrength` 2.517 (source 1.0), `g_MatSpecularPower` 7.47 (10),
`g_MatDiffuseStrength` 1.126 (1.0), `p_TexTiling` 100 (10), `t_CubeMapTexture` the envmap (NONE_ENVI) and alpha test
off (on). Fix in progress: shading classes (one merged draw per class of like constants; section below when it lands),
then a full rebake.

**Decision 2026-09-24 (user):** no shading classes. A per-class merged draw would bring back several draws per
coarse record, which defeats the single-draw goal; the user accepts the small colour change of the one synthesised
material instead. The shading-classes change was stopped before it landed; the finding above stays as the
explanation of the coarse/fine brightness difference. No rebake for this.

## 2026-09-25: baker recipe: Terran solar-plant louvre weld (host bake, not installed)

Implements [lattice-baker-fix.md](../architecture/lattice-baker-fix.md) section 5 (ratified 2026-09-25):
`tools/analysis/lod_recipes.py` holds the per-body recipe (`stations/x3tc/terran_spp_panel`: C from vanilla
record 1, `weld_strips` material 21 at y = 105 about z, `expect` 132 strips / 7,851 x 1,372 / pitch 1,222.6 /
tilt 14.6 deg / tol 0.02). The census applies the self-check and censuses the body on the welded record 1
(row keys and batch-record fields `recipe`, `source_record`, `recipe_ops`, not printed in census.txt; r0_*,
aspect_k and T_pad stay from record 0; a mismatch or any exception in an op gives `recipe_skipped` and a plain
record-0 bake); `bake_body` passes the row's source record and recipe to `plan_body` (the batch no longer hard-codes
record 0 for such a row); the marker body carries `recipe`. The recipe and the `lod_recipes.py` source join
only that body's `inputs_sha256`; every other body keeps its hash formula. `lod_recipes.py` is not in
`TOOL_FILES`, so a later recipe-only change rebuilds only recipe bodies under `--sync`; this change itself edits
`lod_overlay.py` and `lod_batch_census.py`, so `tool_sha256` changes (installed a8dde023 -> bd0855e6 on the
worktree, measured) and install-fleet4 rebuilds all 620 bodies.

| check | command | result |
|---|---|---|
| unit + batch tests (measured) | `PYTHONPATH=verification/probe:verification/analysis:. python3 -m unittest test_lod_overlay_batch test_lod_recipes` | 55 OK (49 + 6); `test_lod_batch_census test_bob1` 50 OK, 1 skipped. Synthetic louvre (12 strips over box girders): poke-through share > 0.01 before, 0 cells after (max 5 units below the plate); every strip point at y = 105, shared edges bit-identical, widths within 2 of the pitch; faces, parts, tangent records, z, UV, normal and flags unchanged; serialise/parse equal; strip count, pitch, tilt, length, flat body and a shared point each refuse; batch: the recipe body bakes from record 1 with the recipe digest in its hash, a mismatching body and a body whose op raises (KeyError) get `recipe_skipped`, source record 0 and the plain hash, a plain body's marker entry is identical with and without recipes |
| one-body bake (measured) | `lod_overlay.py --batch --only <terran_spp_panel> --out <scratch> --jobs 1` (game not running) | exit 0, 9 s wall (census 1.9 s, bake 5.8 s); T_pad 266, atlas 1024, min texels/px 2.04; draws below T_pad 22 -> 3 |
| geometry self-check (measured) | [`bake_check.py`](../../verification/results/lattice-baker-fix/bake_check.py) on the scratch bake, [`_out.txt`](../../verification/results/lattice-baker-fix/bake_check_out.txt) | PASS. C before (installed fleet3, from record 0): 128,336 points, 54,412 faces, 4 groups; after: 25,816 points, 14,284 faces, 3 groups (vanilla record 1: 25,678 / 14,284). Pane: 132 strips, 4,752 faces, every point at y = 105, 0 faces below 104.5; widths 1,220-1,225, 110 bit-identical shared edges, 16 segment gaps (min 1,204 units); poke-through over the pane footprint (20-unit cells) 142,538 / 2,912,102 = 4.9 % (max 234.7 units) on vanilla record 1 -> 0 / 2,836,023 (max -5.0); split_groups empty, widest group 13,732 points; pad record = vanilla record 3 apart from its threshold |
| face areas and winding (measured, reviewer) | [`weldgeo.py`](../../verification/results/lattice-baker-fix/weldgeo.py), [`_out.txt`](../../verification/results/lattice-baker-fix/weldgeo_out.txt) on the welded record 1 | 4,752 pane faces: 0 zero-area (min area 939 after, 1,083 before), 0 winding flips (ny > 0 on all before and after), 0 positions split; with `bake_check.py`: 110 bit-identical shared edges |

Free edges: a strip without a neighbour on one side (the 16 segment gaps and the row ends) ends at centre ±
pitch / 2, about 53 units inboard of vanilla's projected half-width (1,372 cos 14.6 deg / 2 = 664 vs 611;
inferred arithmetic); whether that narrowing is visible is part of the flight check.

Fleet install (not run; install-fleet4, the orchestrator's): `python3 tools/analysis/lod_overlay.py --batch --sync
--install --replace --jobs 2` with the game closed; `reuse_previous` rebuilds every body because the settings'
`tool_sha256` differs, about 48-50 min wall (install-fleet3 took 48.5 min; inferred). Expected result: 620 bodies
as in fleet3, only `terran_spp_panel` differs in geometry (inferred: the plain path is unchanged code). Not flown: the flight check of design section 5 is open.

## 2026-09-25 run321: the louvre recipe in flight (install-fleet4, Run82 DLL)

User report: the Terran solar-plant lattice crawl is gone, no issues seen. Log evidence
([`run321-lattice-fix/`](../../verification/results/run321-lattice-fix/), measured): `terran_spp_panel` draws at lod 1
with 3 draws per node (48 per plant, was 64), s 108-202; on the rest burst 8932-8939 (jitter-only motion 0.17-0.59 px)
the share of plant-region pixels changing class between frames is 0.326 (run315 before the fix: 0.497), the
always-line per-pixel std 11.7 (was 26.6) and the plant's frame-to-frame luma rms 18-29 (was 29-42); not an exact A/B
(the plant is closer and larger than in run315). The LOD switch, the brightness step at s 250/266 and the free-edge
narrowing were not captured (no lod-0 census frames; `cull_census_lod_switch state=off`): the flight report is the only
evidence, and the user noticed nothing. The session still carried the shell's `X3M_TAA_THIN_REGION_SOURCE=vote` and
`X3M_TAA_SENTINEL_STABILISER=0` exports.
