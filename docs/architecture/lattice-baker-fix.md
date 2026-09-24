# Terran solar-panel lattice crawl: fix in the merged-LOD baker

Status: ratified and implemented 2026-09-25 (section 1); the design text below is unchanged apart from the implementation notes in section 5. Tags: **[M]** measured this
session on the parsed `objects/stations/x3tc/terran_spp_panel.pbb` (bottle X3 vanilla catalogues, read through the
baker's reader; scripts and outputs in `verification/results/lattice-baker-fix/`) or cited from
`verification/results/run315-run82a-lattice-crawl/`; **[I]** inferred (arithmetic from measured inputs); **[A]** assumed.
Constraint (user, 2026-09-25): no per-frame band-aid; the crawl is visible under every TAA setting flown.

## 1. Decision

Ratified 2026-09-25 (orchestrator). Implemented 2026-09-25 (section 5 and host checks 1-2; host bake only,
not installed, not flown): `tools/analysis/lod_recipes.py`, ledger `docs/verification/lod-overlay.md`
("baker recipe: Terran solar-plant louvre weld").

Recommended: a per-body **baker recipe** for `stations/x3tc/terran_spp_panel` that builds the coarse record from vanilla
record 1 (record 0 without the 40,128 frame-rim faces, which Egosoft already dropped) and **welds the 132 tilted solar
louvres into closed flat plates above the support girders**: every pane strip is rotated flat about its long axis, scaled
across to the 1222.6-unit pitch so neighbours share edges, and placed at y = 105 (5 units above the girders' top at
y = 100); vertex normals and UVs are kept, so the shading and the texture pattern do not change. The lines the user sees
are not a grid and not the fade-arm depth race by itself: they are the alpha-tested support girders (material 4, box
girders 151 x 150 units, one per slat pitch) poking 235 units through the lower edge of every tilted pane, and the
see-through louvre gap between adjacent panes; at the stand both are 0.3-0.5 px wide at a 3.4-3.9 px pitch. After the
weld nothing lies above the plate inside its footprint (0.0 % of the plate area, section 3) and the gap is gone, so the
coarse record has no sub-pixel structure left at any s below the switch. Per-frame cost: zero; the record gets lighter
(25,678 points instead of 128,336, so the 60,000-point split disappears and the node draws 3 groups instead of 4).
Rebake: this body only for the host check, then the full `--batch --sync --install --replace` (the tool hash changes)
as install-fleet4. Flight check: the run315 stand, `rest_lattice.py` class-flip share on the plant crop.

## 2. What the lines are (Q1) [M unless tagged]

The body: 22 materials, 2 parts, record 0 = 125,998 points / 54,412 faces; radius 65,554 raw units (the record-0
value 2,169,156 engine units is the census radius, scale 33.1, cancels in every pixel figure). Structure of one panel:

| element | material | geometry | count |
|---|---|---|---|
| solar pane (the "cells") | 21 `metal_argon_solar_panes` (standard_lighting.fx, blend on, z-write off, diffuse alpha 239-255, mean 242) | planar strips 7,851 x 1,372 units, tilted 14.6 deg about z (normal 0.253, 0.967, 0), all centres at y = 38.2, pitch 1,222.6 across x, 6 rows 9,000 apart; the pane texture repeats 2 x 8 per strip | 132 strips of 36 faces (4,752) |
| frame rim | 9 `metal_argon_simple_plating` (opaque) | hollow picture frame round each pane, 5.5 units wide on the long sides, 34 at the ends, 10.6 tall (bevels); covers 1.6 % of the pane footprint (end caps only) | 132 x 304 faces = 40,128 of the record's 43,948 opaque faces |
| support girders (the "lines") | 4 `lattice_support_beams_01` (alpha-tested, texture alpha mean 143/255 = 44 % holes, 5 texture periods per 6,282-unit face) | box girders 151 wide x 150 tall (y -50..100), one under every slat pitch (pitch 1,229), plus cross girders at the row ends; 4 segments x 6 rows | 22 x 172 faces (3,784) |
| louvre gap | none | adjacent panes overlap 105 units in x but step 346 units in y: see-through beyond 17 deg incidence, width 346 sin(theta) - 105 cos(theta) (170 units at 45 deg) | 131 per row set |
| other struts / trims | 6, 18, 14, 16, 17, 7 | under the panes (y centre -280..150), the panel's outer structure | 5,100 faces |

Where the substrate shows: the pane's lower edge sits at y = -135 and the girder top at y = 100, so each girder is above
the pane on a 150-unit band along the slat's lower edge (13 % of the pane footprint, `beams_out.txt`). That band is the
bright staircase (girder diffuse luma 98, pane 67; display codes 138 vs 105 in run315). The frame rim is 0.02 px at
the stand and never rasterises; the "depth race" in the triage is the pane draw (z-write off, drawn after the girders)
losing the depth test on the girder band pixel by pixel as the jitter moves pixel centres across a 0.4-px feature, not a
z-fight of coplanar faces (girder-band height above the pane p50 128-193 units; `flip_depth` p50 relative dw 1.9e-4 is
that band's depth step, not a rounding race) **[I]**. The S-class holes are the girder's 44 % texture holes and the
louvre gap, both looking through to sky.

Pixel sizes (`pixels.py`, arithmetic **[I]** on measured inputs): s = r*640/D (lod-selection.md; r = body radius, D =
node distance; the census rows carry it: s = 90 at D = 15,404,494 for node 29b253e0, 30.5 km at 505 units/m). Pixels
per raw unit k = focal * s / (65,554 * 640); focal = (rows/2)/tan(58.72 deg / 2) = 1,280 px at 1,440 rows and 960 px
at 1,080 rows (run315 `fov` row: vertical 58.72 deg for setting 90). The stand is s 73-104.

| feature (units) | 1440 rows: s 73 / 90 / 104 / 250 / 266 | 1080 rows: s 73 / 90 / 104 / 250 / 266 |
|---|---|---|
| slat pitch 1,222.6 | 2.72 / 3.36 / 3.88 / 9.33 / 9.92 px | 2.04 / 2.52 / 2.91 / 6.99 / 7.44 px |
| girder width 151 (height 150) | 0.34 / 0.41 / 0.48 / 1.15 / 1.23 px | 0.25 / 0.31 / 0.36 / 0.86 / 0.92 px |
| louvre gap at 45 deg (170) | 0.38 / 0.47 / 0.54 / 1.30 / 1.38 px | 0.28 / 0.35 / 0.41 / 0.97 / 1.04 px |
| frame rim 5.5 / end 34 | 0.02 / 0.09 px at s 90 | 0.01 / 0.07 px at s 90 |
| girder texture period 1,256 | 2.80 / 3.45 / 3.99 / 9.58 / 10.19 px | 2.10 / 2.59 / 2.99 / 7.18 / 7.64 px |
| row pitch 9,000 | 20.0 / 24.7 / 28.6 / 68.7 / 73.0 px | 15.0 / 18.5 / 21.4 / 51.5 / 54.8 px |

The slat pitch drops under 2 px at s < 53.6 (1440 rows; D > 25.9 M units, 51 km) and s < 71.5 (1080 rows; D > 19.4 M
units, 38 km). The line itself (girder band and gap) is under 1 px on the whole lod-1 range at 1080 rows and up to
s = 217 at 1440 rows: it is always a binary sub-pixel feature there, and the jitter makes 49.7 % of the panel region
change class between frames (run315). The run315 spectrum's 7.54-px fundamental with a 3.77-px harmonic against a
geometric pitch of 3.4-3.9 px is unexplained (the picker's harmonic ambiguity is noted in taa-lattice-crawl.md section
2; the 1,229 vs 1,222.6 girder/pane beat is 6.4 units per slat and cannot double the period) **[I]**; it does not change
the choice, since every option below removes the feature rather than resampling it.

Vanilla ladder (`records_out.txt`): record 1 (T 250) = record 0 minus the 40,128 rim faces (14,284 faces, panes and
girders unchanged); record 2 (T 150) flattens the panes to 22 plates at y = 0 (one quad set per segment, UV 0..1, so one
texture repeat per 7,481 x 8,212 plate) and keeps the girders, which poke up to 100 units through that plate on 12.1 %
of its area; record 3 (T 80) has no panes or girders. The batch bakes every body from record 0 (`bake_body` passes
source record 0), so the installed coarse record carries the full crawl geometry.

## 3. Options in the baker (Q2)

All options change only the coarse record C (index 1 of the compact ladder; the pad copy at index 2 keeps the original
coarsest record for the collision tree). "Switch" is s = 266, where the panel is about 520 x 290 px at 5120x1440 (the
bounding radius projects to 532 px).

| option | what the user sees at lod-1 (s 73-104) | at the switch from LOD0 | baker change | rule | draws per node | fixture |
|---|---|---|---|---|---|---|
| (a) weld the panes only (flatten + abut, no lift) | gap gone; girders still poke 62 units above a y = 38 plate: the 0.4-px band and its flicker stay | as today minus the parallax | flatten op (about 60 lines) + recipe plumbing | per body | 4 | synthetic louvre body |
| (b) drop the substrate (material 4 girders; rims are invisible anyway) | girder lines gone; louvre gap still shows sky at 0.3-0.5 px per slat: S/O flicker remains at 22-code contrast (cells 105 vs sky 83) and the row-end girders vanish (panel outline darkens) | brightness step (the 36 % L pixels at 138 become 105: about -10 % mean) | material drop list (about 20 lines) + plumbing | per body (a generic "wire under 1 px at T" rule would also hit masts and cables fleet-wide; needs a census first) | 4 (the alpha-tested group keeps mats 0/5/6/18) | synthetic body |
| (c) bake the girder line into the pane tile, drop the geometry | correct mean brightness, no lines | closest match (the line survives as a 1.2-px texture stripe at s 266 and mip-averages below) | new geometry-to-texture painter in lod_atlas (synthetic tile source, UV remap per strip): 200-300 lines | per body | 4 | painter unit test + tile check |
| (d) opaque panes (z-write on, no blend) | nothing: the lines are geometry over geometry, not the blend; the 6 % see-through of the girders under the pane is 2 codes | none | small | generic | still 4: the pane's effect is standard_lighting.fx, the atlas group is argon.fx, so effect classes keep it a separate group | - |
| (e) source record 2 + lift the plate to y = 105 | plates, no lines, no gap | the pane texture goes from 2 x 8 repeats per 9.9-px slat to one repeat per 60-px plate, with its grey border (edge rows luma 105 vs 66) framing each segment: a visible pattern change on a 500-px object | `source_record` per body (parser exists: NAME=T@N; the batch must stop hard-coding 0) + a translate op (about 15 lines) | per body | 3 | records_out / record2 checks |
| **(f) recommended: source record 1 + louvre weld at y = 105** | plates with the LOD0 pane texture at its own scale; no lines, no gap; nothing above the plate | only the 1.2-px girder lines and the 1-px louvre parallax vanish; texture pattern and shading unchanged; mean brightness -10 % on the panel area [I] | flatten op (60 lines) + recipe table with a geometric self-check (40 lines) + batch plumbing (source record and recipe per row, inputs hash) | per body, self-checked | 3 | synthetic louvre body + bake check on the real body |

Why not (c): it is the only option that also preserves the girder lines' mean contribution at the switch, but it is
the largest change and its payoff (a softer brightness step on one station at one distance) can be judged after (f)
flies; it stays the follow-up if the user reports the step. Why not (e): cheaper than (f) by the flatten op, but the
switch look is worse (texture scale x22) and record 2 also drops 4,620 detail faces record 1 keeps. Why not (a), (b)
or (d) alone: each leaves one of the two sub-pixel sources in place.

## 4. LOD0 and the threshold (Q3)

The crawl exists at LOD0: it is the same mesh (C is built from record 0), and the whole lattice history in
taa-lattice-crawl.md sections 2-32 was flown on the plant's 80 alpha-tested draws (materials 4/6/18, section 17).
LOD0 draws at s >= 266, where the girder is >= 1.2 px and its texture holes >= 10 px; that is the near-field truss
regime the TAA thin-region work addresses and is out of scope here. "The coarse record taking over earlier" means a
higher T_pad; it is not needed: the switch stays at 266. Where the welded record is indistinguishable at 5120x1440: the
removed features are under 1 px for s <= 217 (girder 1.00 px, gap 1.13 px); between 217 and 266 the switch removes
1.0-1.2-px lines from a 430-530-px panel. At 1920x1080 the girder is 0.92 px even at 266, so the switch is sub-pixel
there. If the step is objectionable at 5120x1440, this body alone can take `terran_spp_panel=217@1` (per-body
threshold, existing syntax): the cost is LOD0's 80 alpha-tested draws per node in the band s 217-266 (10.3-12.6 km).
No global threshold change: the other 619 overlay bodies are untouched by the recipe (the station T_class 150 x aspect
cap 2.0 rule stands).

## 5. Recommendation (Q4)

**Implemented** as below, with these differences: the recipe table and `weld_strips` live in
`tools/analysis/lod_recipes.py`, which is not in `TOOL_FILES`; its source hash and the recipe enter only the
recipe body's `inputs_sha256`, so a later recipe-only change rebuilds only recipe bodies (this first commit
changes `lod_overlay.py` and `lod_batch_census.py`, so install-fleet4 still rebakes the fleet). The census
runs the self-check and censuses the body on the recipe's record (`recipe`, `source_record`, `recipe_ops`, or
`recipe_skipped` with a plain record-0 bake and an unchanged hash); `strip_size` is checked as (longest strip,
every strip's across width), since 36 of the 132 strips are 6,495 or 6,834 long; the strip edges snap to the
midpoint of neighbouring strip centres (widths 1,220-1,225), and a strip without a neighbour on one side
takes half the measured pitch there. The part bounds are kept (the welded strips stay inside the strips' old
box). An op that fails on a same-stem body in any other way (an exception, not only a mismatch) also gives
`recipe_skipped` and a plain bake. Only `--batch` applies recipes: the single-body path (`NAME=T@N`) bakes
this body plainly. Host checks 1 and 2: `verification/analysis/test_lod_recipes.py`,
`verification/results/lattice-baker-fix/bake_check.py`.

Rule, per body, in a `RECIPES` table keyed by body stem (a small module beside the baker, or a section of
`lod_overlay.py`):

```
'stations/x3tc/terran_spp_panel': dict(source_record=1,
    ops=[('weld_strips', dict(material=21, plane_y=105.0, axis='z'))],
    expect=dict(strips=132, strip_size=(7851, 1372), pitch=1222.6, tilt_deg=14.6, tol=0.02))
```

`weld_strips`: for every connected component of the material's faces (36 faces, 38 points each here), rotate the points
about the component's long axis so its normal becomes +y, scale the across-extent to the pitch measured from the
neighbour centres (1,372 -> 1,222.6, factor 0.891, about the centre), set y = plane_y, snap the across edges to the
pitch grid so adjacent strips carry bit-identical edge coordinates (watertight under the D3D9 top-left rule); keep
each point's normal, UV and tangent inputs (`lod_atlas` regenerates the tangent records per group). `expect` is the
geometric self-check: component count, strip size, pitch and tilt within `tol`; a mismatch (a mod's replacement body,
a future asset change) logs `recipe_skipped` in the batch row and bakes plainly. The recipe's dict and the source record
enter the row's `inputs_sha256` so `--sync` rebuilds the body when the recipe changes.

Batch plumbing: `bake_body` takes `row['source_record']` (default 0) and the recipe; `plan_body` applies the ops to
`source` before `alpha_materials` and the atlas collapse (so the pane group, still alpha, is rewritten with its
existing UVs, and the opaque group shrinks to record 1's 3,820 faces). `parse_only` already accepts NAME=T@N.

Rebake scope: this body only (`--batch --only <file with terran_spp_panel=266@1> --out <scratch>`; the 16-Terran bake
measured 76 s), then the host checks below; the install is the full `python3 tools/analysis/lod_overlay.py --batch
--sync --install --replace --jobs 2` as install-fleet4 because `TOOL_FILES` changes the tool hash (fleet3 took 48.5 min
wall; the other 619 bodies rebake byte-identically apart from the manifest's tool hash, which the fixture below asserts
on a sample). Alternative if the 48 min matter: keep the recipe module out of `TOOL_FILES` and accept one full rebake
for the plumbing commit anyway; not worth a special case.

Host checks (proves the change on the parsed mesh, no Wine):

1. Unit test (new class in `verification/analysis/test_lod_overlay_batch.py` or `test_lod_recipes.py`): a synthetic
   BOB1 body with N tilted strips at a pitch, girders under them and a rim; assert after the op: every strip point at
   y = plane_y, across extents equal to the pitch, adjacent edge coordinates identical, normals and UVs unchanged, the
   poke-through raster (the `beams.py` / `record2.py` method as a helper) reports 0.000 above the plate inside its
   footprint, the body serialises and re-parses with the same ladder and material table, `expect` mismatch refuses.
2. Real-body check, a script under `verification/results/lattice-baker-fix/` run on the scratch bake: read the
   overlay member, record 1: 132 strips at y = 105 abutting at 1,222.6; nothing of materials 4/6/18/9 above y = 105
   inside the plate footprint; about 25,678 points (record 1) plus the atlas's UV-shift duplicates, under 60,000;
   `split_groups` empty; draws 3; the pad record byte-identical to vanilla record 3.
3. Existing suites unchanged: `PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_lod_overlay_batch
   verification.analysis.test_lod_batch_census verification.analysis.test_bob1` (91 OK, 1 skipped today).

Flight check (user launch, one stand): the run315 position (plant at s 73-104), 8-frame F8 burst at rest, then the
same at the switch distance (panel about 500 px wide, s just under and just over 266). Acceptance on the rest burst
with the existing scripts: `rest_lattice.py` flipping share on the plant crop 0.497 -> under 0.05 **[I]**, L class
inside the plates 0, S class inside the plates 0, `line_width.py` no 3-10 px lattice family; `plant_draws.py` 3 draws
per node with the 5e0a10fe group at 5,712 prims and no `split_groups`; the user's verdict on the crawl and on the
switch step. Nothing to measure on the GPU: the change is data.

## 6. Risks (Q5)

- Colour and brightness. The panel area loses the girder band's contribution: about 36 % of the region at display code
  138 becomes pane at 105, roughly -10 % mean luminance on the panels below the switch **[I]**, seen as a step when
  the coarse record takes over at s 266 (panel 500 px wide). The pane's own tone, texture scale and lighting are
  unchanged (normals kept). The atlas is unaffected: the pane is not atlased (alpha group), the opaque atlas holds the
  same materials as before minus nothing (the rims used the same plating tile). Escalation: option (c) or the per-body
  T 217.
- Fade-arm owner and RT2. The pane group keeps its state (blend, z-write off, alpha test off), so it is still the
  fade-arm owner `64bac8bb`: RT2 .a = 1, .g = -1 on the plates, exact motion, no thin vote (as today). What changes is
  that the plate region is uniformly owner class with no L/S islands, so the cutout-owner and stabiliser paths see a
  flat surface. The girder draw (5e0a10fe, alpha-tested, RT2 .a = 1) still runs and is depth-rejected under the plate;
  its remaining visible parts are the row-end cross girders (0.4 px at the stand, 24.7-px pitch) and the panel edges,
  i.e. isolated thin lines of the general kind, not a lattice. If those still read as crawl, drop material 4 from C in
  a second bake (option (b) on top; the panel outline then loses its girders).
- Shadow and env passes. Below T_pad the engine has only C to draw; the plate sits 67 units (0.2 px at s 266) above
  the old pane centre. The env-map view (0x1000000) draws record 1 = C. No shadow-caster behaviour of station bodies
  is documented in the LOD notes; any pass that draws C sees the same 0.2-px change **[A]**.
- Mod overlay rules. The recipe is keyed by stem and gated by `expect`; a mod body of the same name with other geometry
  bakes plainly with `recipe_skipped`; a mod overwriting addon/05 is the installer's existing marker case, unchanged.
  Mod ships and every other body: no recipe, byte-identical bake apart from the tool hash.
- Draw and memory. 3 draws per node instead of 4 (the 60,000-point split goes away), 25,678 points instead of 128,336
  per node, 16 nodes: less vertex work, not more. Overlay bytes shrink for this member.
- Collision and hide flags. The pad record (index 2) is untouched vanilla record 3, so the collision tree and the
  hide-at-coarsest rule are as in install-fleet3.

## 7. Hot path and native Windows

Zero per-frame cost: the change is in the CAT/DAT overlay data; no DLL, no shader, no route change. Native Windows
behaviour is identical by construction (the engine loads the same body from the same catalogue), except that the
watertightness of the abutting strips relies on the D3D9 top-left rasterisation rule with bit-identical shared edge
coordinates, which holds on every conforming D3D9 device; not runtime-verified on Windows, like the rest of the
overlay.

## 8. Unknown, and what settles it

- The 7.54-px spectral fundamental against the 3.4-3.9-px geometric pitch (section 2): a per-node projection of the
  parsed panes with the run315 node transform (the F8 dump's object rows, not read here) would show whether alternate
  slats differ in the raster; irrelevant to the fix.
- Whether the row-end cross girders and the panel edges still read as crawl once the lattice is gone: the flight.
- Whether the -10 % brightness step at s 266 is visible to the user: the flight at the switch distance; remedy (c) or
  T 217.
- Other bodies with the same louvre construction (other solar plants, `terran_spp_center`?): a census pass of the
  `expect` detector over the 620 overlay bodies (`geometry.py`'s component analysis on material groups whose components
  are congruent tilted strips at a pitch under their width) would list candidates; not run.
- The UNITS_PER_M = 505 conversion is the census's inferred constant; every figure above is given in raw or engine
  units first.
