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
the coarse record also loses its ambient-occlusion darkening (cause open: disassembly of the occlusion parameter
binding from `0x004baa30`). Fix (baker only): count a material as alpha only when its alpha can drop below 1 (a real
`t_AlphaTexture` or diffuse alpha < 255); materials 1-4 then join the opaque atlas as tiles; draw count unchanged; atlas
likely 1024 -> 2048 for these bodies (inferred); full rebake required (the resolver change already changed the tool hash).
