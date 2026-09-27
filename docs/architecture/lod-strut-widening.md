# Far-LOD strut widening in the merged-LOD baker: no sub-pixel geometry in the coarse record

Status: design 2026-09-27, ratified 2026-09-27 (orchestrator, user "let's try it"); implementation pending. The user's decision to try: remove sub-pixel geometry from
the far station LODs so that distant stations neither shimmer at rest nor need the heavy TAA far history weight
(0.985, `X3M_FAR_STABILIZE`, ramp 60/68) that blurs them under pan. Tags: **[M]** measured this session (host-side
reads of the bottle X3 catalogues through `tools/analysis/bob1.py`, and the run340/run346 logs and captures under
`/tmp/x3-bottleX3-run34x`; scripts and outputs in `verification/results/lod-strut-widening/`), **[I]** inferred
(arithmetic on measured inputs), **[A]** assumed. No Wine, no game launch, no build.

Established context, not re-derived: the resolve has 5-tap Catmull-Rom history, 3x3 variance clip (1.25 sigma),
thin-region weight 0.97 and the far stabiliser 0.985 by depth (`src/temporal/resolve.hlsl:1033-1034`: `farKeep =
keep + stabilise.g * farOpen * (min(ramp, flicker.y) - keep)`, thin `stabilise.b * (min(ramp, history.x) - keep)`);
base weight 0.85; the rotation-aware weight (Run 93 A) is rejected. The baker (`tools/analysis/lod_overlay.py
--batch`, install-fleet4 2026-09-25: 620 bodies, `addon/05`+`06`, 2.72 GB, 50 min wall at 2 jobs) builds one
coarse record `C` per body from record 0, atlas collapse, compact placement `[record 0, C:T_1, pad:T_pad]`; `C`
draws for `s < T_pad` (`s = r·640/D`, f = 1 at Very High), record 0 above. Per-body recipes exist
(`lod_recipes.py`, the Terran louvre weld, [lattice-baker-fix.md](lattice-baker-fix.md)) and apply to the source
record before the alpha split and the atlas.

## 0. Decision in one paragraph

Recommended: a **generic baker op, `widen_thin_patches`**, applied to every eligible body's source record after the
recipes and before the alpha split: decompose the record into smooth patches (faces joined across edges that fold
under 45 deg), measure each patch's width (second PCA extent, or area / length for frames and rings), and for every
patch narrower than one pixel at the record's **design size** `s_d = T_pad / 2` scale it about its own centreline to
exactly one pixel at `s_d` (`W_u = r_raw·640 / (F·s_d)` raw units, F the bake display's focal length in px),
excluding bevel patches (thin patches whose sharp boundary borders wide patches only). The widened faces leave the
opaque atlas group and form **one blended group per part and effect** on a copy of the atlas material (blend on,
source-over, alpha test off, z-write on) whose tiles are duplicates of their source tiles with **alpha = true width /
widened width**, so a widened strut composites exactly the light the thin one carried, over sky and over the
station's own hull alike. Only `C` changes; record 0 (close-up) and the pad (collision tree) are untouched. Per-frame
cost: +1 draw per affected part and effect, no shader or DLL change. First flight at unchanged TAA on the run340
stand; then the far weight comes down (0.985 -> 0.95, thin 0.97 -> 0.9 proposed) in a second flight.

Two findings that shape it (section 1): the user's "adjacent station" (blob A of the pan-blur runs) is the asteroid
rock body `environments/asteroids/asteroid_B_ClassMine` drawn at LOD 0 with 1,712 faces and **no sub-pixel geometry**;
its rest shimmer is not a strut problem and this design does not address it. The "distant station" is
`argon_L_solarpowerplant` at 30.6 km (`s` 65, coarse record `C`, switch `T_pad` 212 at 9.4 km), where 23.7 % of the
record's area lies on patches under one pixel wide; 17 % of the record is on alpha-class materials (the lattice
cards), which take the material-copy path, not the tile path.

## 1. What is sub-pixel, where (Q1)

### 1.1 Which bodies the user looked at [M]

`stations_in_view.py` (cull-census rows of the captured frames) and `station_screen_positions.py` (each node's
logged world/view/projection matrices; validated against the depth lane: projected view depth and lane depth agree
within 1 % at five stations, e.g. spacedock 81,054 vs 80,789, solar plant 187,654 vs 187,107). Run340 frames 1017
(rest) / 1424 (pan) / 4078 (moving), run346 2474 / 2724:

| blob (ledger name) | body | D | r (engine) | s | record drawn | T_pad |
|---|---|---|---|---|---|---|
| A, "adjacent station", right edge, 171 k px, thin 0.000 | `environments/asteroids/asteroid_B_ClassMine` (projected x 4976, y 293; lane depth 51.6-56.2 k view units at three probes vs the rock's 55.0 k) | 8.4-11 km | 1,016,512 (2.0 km) | 74-76 | LOD 0 of a vanilla 5-record ladder 20/5/2/30 (not in the overlay: `dominant_slot_missing`, `category_other`) | - |
| B, "distant station", 30 km, thin 0.12-0.13, farw 1 | `stations/station_scenes/others/argon_L_solarpowerplant` (x 2073, y 542) | 30.4-30.8 km | 1,567,294 | 64-67 | C | 212 |
| E (run346, 11-12 km, thin 0.015) | `stations/station_scenes/tech/argon_tech_L_shield_F` (x 580, y 481; lane 57.8 k vs 57.9 k) | 9.4 km | 711,107 | 94-100 | C | 173 |
| below B, 13.2 km | `argon_spacedock` (x 2303-2361) | 13.2 km | 1,953,631 | 187-192 | C | 278 |
| 25.7 km | `military_outpost_middleb` far node | 25.7 km | 2,246,786 | 110-150 | C | 150 |

Station A's blob is one convex rock surface: 1,002 points, 1,712 faces, one material, six smooth patches, the
largest of which is 120,366 units wide; at s 76 its faces average about 100 px each. Whatever shimmers there at
weight 0.8 is texture or specular response, not geometry (the asteroid BUMP pair is documented in
`docs/reverse-engineering/asteroid-specular-minification.md`); out of scope here and flagged in section 8.

### 1.2 Pixel size of the geometry [M, arithmetic I]

`thin_geometry_census.py`: per face the altitude `h = 2·area / longest edge`; per **smooth patch** (faces joined
across edges folding under 45 deg; an open edge or a sharper fold ends the patch) the width `min(second PCA extent,
area / first extent)`. Pixels per raw unit `k = F·s / (r_raw·640)` with F = 1,280 px at 1,440 rows (vertical FOV
58.72 deg) and 960 at 1,080 rows, `r_raw` = max |position| of record 0 (65,554 for `terran_spp_panel`, where the
script reproduces the lattice-baker-fix table: girder 151 units = 1.23 px at s 266). A face is "thin" when its own
altitude is under 1 px (a sliver of a plate tessellation counts), a "feature" when its patch is under 1 px wide (a
strut side, a rim, a fin), a "bevel" when the feature's sharp boundary borders wide patches on 90 % or more of its
length (a plate chamfer), a "strut" otherwise. Record 0 is the baked source of every `C` (the Terran panel
excepted), so record 0 at `s = T_pad` is `C` at its largest on-screen size; every smaller `s` is worse.

| body | faces | s | k (px/unit) | 1 px (units) | feature area share | strut / bevel share | patches thin / total |
|---|---|---|---|---|---|---|---|
| solar plant (B) | 152,900 | 212 (switch, 9.4 km) | 0.00646 | 155 | 3.3 % | 2.3 / 1.0 % | 7,107 / 13,144 |
| | | 106 (T_pad/2) | 0.00323 | 310 | 12.5 % | 12.1 / 0.5 % | 10,463 |
| | | **65 (stand, 30.6 km)** | 0.00198 | 505 | **23.7 %** (alpha materials 16.9, opaque 6.7) | 23.3 / 0.3 % | 11,838 |
| spacedock | 107,397 | 278 (switch, 8.9 km) | 0.00848 | 118 | 11.3 % | 8.2 / 3.1 % | 7,186 / 9,373 |
| | | **188 (stand, 13.2 km)** | 0.00573 | 174 | **16.4 %** (opaque 15.2, alpha 1.2) | 14.0 / 2.4 % | 8,091 |
| tech_L_shield_F (E) | 96,942 | 173 (switch, 5.3 km) | 0.00516 | 194 | 6.1 % | 3.5 / 2.6 % | 5,219 / 11,801 |
| | | **95 (stand, 9.7 km)** | 0.00283 | 353 | **12.9 %** (opaque 9.5, alpha 3.4) | 10.0 / 3.0 % | 7,718 |
| outpost | 73,749 | 150 = stand (19 km) | 0.00456 | 219 | 1.7 % | 1.2 / 0.6 % | 3,438 / 6,485 |
| asteroid mine rock (A) | 1,712 | 76 | 0.00204 | 490 | 0.01 % (one face) | 0 / 0.01 % | 1 / 6 |

The offending materials (`named_bodies_out.txt`, `pixel_figures_out.txt`): on the solar plant material 11
`metal_argon_lattice_s…` (alpha-tested and blended lattice cards, 17.8 % of the record) has strips 96-247 units wide
(p10/p50) = 0.19-0.49 px at the stand, 0.62-1.6 px at the switch; 73.7 % of its area is sub-pixel at the stand.
Opaque plating/trim materials 17, 7, 20, 23 add 6.7 %. On the spacedock the exhaust trims (39), simple plating (17)
and (16) are 13-103 units wide = 0.08-0.59 px at the stand (0.11-0.87 px even at the switch). On shield_F the pipes
(15) and plating (17) are 76-425 units = 0.21-1.2 px at the stand. At 1,920x1,080 every figure is 0.75x (last
column of `pixel_figures_out.txt`): what is 0.49 px at 5,120x1,440 is 0.37 px there.

### 1.3 Fleet census (617 of the 620 overlay bodies parsed; 3 refused by the script as `ambiguous_body_ext`) [M]

At `s = T_pad`, 5,120x1,440 (`fleet_census_out.txt`, `fleet_summary_out.txt`): 576 of 617 coarse records carry at
least one sub-pixel patch; 398 have more than 1 % of their area on sub-pixel patches, 99 more than 5 %; counting
struts only (bevels excluded) 288 bodies exceed 1 % and 41 exceed 5 %. 5.26 M of 23.9 M faces (22.0 %) lie on
sub-pixel patches (3.85 M strut, 1.41 M bevel); 1.26 M of 2.39 M patches (52.8 %) are sub-pixel. Stations: 195 of
311 with strut area over 1 % (29 over 5 %, median 1.5 %); ships 89 of 301 (12, median 0.3 %). The per-body median
sub-pixel patch width at the switch is 0.37 / 0.56 / 0.74 px (fleet p10/p50/p90): the typical offender is about
half a pixel wide at the largest size the record is ever drawn. At 1,920x1,080: 367 bodies over 1 % strut area,
100 over 5 %. The worst are the Argon mine station scenes (`argon_mineA_*`: 26-32 % strut area at the switch),
`argon_farm_M_factory_A` (18 %), the Teladi shipyard halves (17 %), `usc_mine_a_main` (16 %), `argon_spacedock`
(8 %, 11 % with bevels). Note that the census is at the switch; at the stand-like sizes of section 1.2 the shares
are two to seven times larger.

## 2. The rule (Q2)

### 2.1 Recommended: in-place widening of thin patches with coverage alpha (a + the compensation of b)

`widen_thin_patches(record, k_d, W=1.0, fold=45, bevel=0.9)` in `lod_overlay.py` (a generic op, not a recipe;
`lod_recipes.prepare` runs first so the louvre weld feeds it):

1. Patches as in section 1.2 (numpy label propagation over manifold edges; the census does 617 bodies in 62 s wall
   at 5 jobs, 0.48 s per body single-threaded, so the op adds under 1 s to the 4.2 s per-body bake).
2. Thin = patch width `w < W_u = W / k_d`, `k_d = F·s_d / (r_raw·640)`, `s_d = T_pad / 2` (section 4 on the
   choice); bevels excluded; patches with fewer than 3 points or zero area skipped.
3. For every thin patch, every point of the patch moves across the patch's second PCA axis: `c' = c · (W_u / w)`
   about the patch's centreline (its mean), so the patch becomes exactly `W_u` wide; the long axis, normals, UVs,
   flags and tangent records are unchanged (the texture stretches across a sub-pixel band, invisible). A point on
   the edge between two thin patches (a box strut's corner) receives both displacements, so a box grows in both
   cross-section axes; a rib's base edge moves into the wide surface it stands on (hidden), its top edge outward.
   Displacements are accumulated per point and applied once.
4. The widened faces are tagged. The alpha split and the atlas collapse then treat them as a separate class:
   opaque tagged faces get, per part and effect, **one blended group** on an appended copy of that effect's atlas
   material with `g_AlphaBlendEnable` 1 (ADD, SRCALPHA / INVSRCALPHA, the "source-over" the alpha rule recognises),
   `g_AlphaTestEnable` 0, `g_ZWriteEnable` 1, `g_AlphaValue` 1.0, `g_EnableGlow` as the dominant. Their tiles are
   duplicates of the source tiles keyed by `(textures, alpha class)` with the diffuse (or, under `g_EnableGlow`,
   the light) tile's alpha set to `a = w / W_u`, quantised to 1/32 (error under 2 %); the shipped effect writes
   `oC0.a = lerp(s0.a, lightmap.a, g_EnableGlow) · g_AlphaValue` (station-material-distance.md), so the per-tile
   alpha is the coverage. The group order inside a part stays atlas groups, kept groups, widened group, alpha group
   (`lod_atlas.rewrite_record` already places kept groups "after the atlas groups and before the alpha group").
   Tagged faces on alpha-class materials (the solar plant's lattice cards) keep their group but get a material copy
   with `g_AlphaValue = a` per alpha class (octave classes 1/2 … 1/16; up to 4 more draws per part, the fallback
   path of 2.3); step 1 may leave them unwidened (the census shows they matter on the solar plant, 16.9 % of the
   record, and little elsewhere).
5. The part bounds (the 10 precomputed ints, render-node-bounds.md) are recomputed from the moved points, or the op
   asserts growth under `W_u / 2` and keeps them (the `inside_old_box` precedent inverted); the manifest records
   per body the number of widened patches, faces and points, the area share, the alpha classes and `W_u`.

Why the compensation is alpha, not darkening: over a background B a true strut of width w covers a W-pixel band
with `w·S + (W − w)·B`; a widened opaque strut darkened to `(w/W)·S` gives `w·S` over the strut and loses the
`(W − w)·B` hull light, a dark halo. Run153 established that these lines mostly sit on the station's own hull
(taa-distant-line-fade.md §2), so darkening is wrong exactly where it matters; source-over blending with
`a = w/W` gives `a·S + (1 − a)·B` on each of the W pixels, i.e. the true integral over any background **[I]**.
Blending with z-write on is the shipped pattern for the lattice cards themselves (material 11: test, blend and
z-write all on), so depth, the motion route (`zwrite=1` draws are routed) and later occluders behave as they do
for those cards.

Interaction with the existing machinery:

- **Alpha rule.** The widened material's alpha is below 1 by construction, so `alpha_materials` classes it alpha
  (a copy whose alpha "can drop below 1"); the collapse must be told it is a kept class (like `light_bleed` kept
  materials), not folded into the dominant alpha group, which would draw it with the dominant alpha material's
  textures over the wrong UVs. That is a new `kept` entry, not a rule change.
- **Synthesised merge material.** Untouched: the widened material is a copy of the effect's atlas material and
  carries the same area-weighted `g_Mat*` means, the same atlas textures; only the four state parameters differ.
  `plan_body`'s table checks (parse-back, index range) cover the extra entry.
- **Atlas.** Tile identity gains an alpha class; a widened strut's faces reference the duplicate tile, so the
  atlas grows by the widened faces' UV spans (their area is 1-25 % of a record, their spans are usually small
  strips; the clamped layout handles span outliers as today). The diffuse atlas becomes DXT5 wherever an alpha
  tile exists (the code already switches when alpha is not uniformly 255); the light atlas is DXT5 already. The
  opaque atlas group ignores the alpha channel (blend and test off), so sharing one atlas between both groups is
  safe.

### 2.2 Why (b) as literally stated loses

Replacing strut groups by fresh quads needs a new tessellation, new UVs and new tangent records per strut and a
cross-section model per strut type (box, cylinder, T); the in-place scaling gives the same on-screen result
(a `W_u`-wide band with the original texture stretched across it) with the original faces, normals, UVs and tangent
frames, no new geometry, and the fixture can assert bit-identical everything but positions.

### 2.3 Why (c), textured cards for lattice walls, loses for the first step

The lattice walls in these bodies already are textured cards (material 11's 96-247-unit strips with an alpha-tested
lattice texture whose holes mip-average at distance); their sub-pixel content is the card strips' own width, which
(a) handles. Painting geometry into cards is the 200-300-line painter of lattice-baker-fix option (c); it stays the
follow-up for bodies where widened struts still read as clutter (the mine scenes at 30 % strut area).

### 2.4 Why not a darkened opaque copy (no blending)

Exact only over black, wrong over the hull (above); one fewer draw is its only merit. Kept as the fallback if a
blended group turns out to misclassify in the resolve (section 8).

### 2.5 `g_AlphaValue` classes instead of alpha tiles

No atlas change, but one draw per class per part (up to four) and a 30 % light error inside octave classes; it is
the path for alpha-class materials only (2.1 step 4) and the fallback if the alpha-tile layout proves awkward.

## 3. Detection (Q3)

From BOB1 data alone (record 0 points and faces; `face_geometry` in `thin_geometry_census.py`): merge coincident
positions, build the manifold edge table, join faces across edges whose face-normal fold is under 45 deg, take the
components as patches, measure each patch's width as `min(second PCA extent, area / first extent)` (the second
term catches frames and rings: the Terran rim is 5.5 units wide but its picture-frame patch is 1,382 units across;
the mean width 80 is thin, as it should be). Thresholds: `W = 1 px` at `s_d` on the bake display's focal length
(1,280 px at 1,440 rows; the fleet4 bake was made at `--display 1920x1080`, F = 960, which widens 33 % more than
5,120x1,440 needs, harmless); fold 45 deg (a 6-facet cylinder strut splits into six thin facets, a 30-deg-faceted
hull stays one patch); bevel 90 %. Material class only decides the compensation path (opaque atlased vs alpha
group vs kept glow), not the detection.

False positives and what happens to them:

- Thin hull plates seen edge-on: a plate is a wide patch, never widened; its edge-on silhouette is a
  view-dependent sub-pixel line that no static geometry can fix (TAA's job, unchanged).
- Antennas, masts, cables: thin patches, widened to 1 px with their true alpha; that is the intended behaviour
  (they are the classic distant flicker source), and their light is preserved.
- Glow strips (emissive trims): thin patches on materials with bright light maps; if the material is a glow-kept
  group, its copy uses `g_AlphaValue` (not atlased); the emission scales with the blend, so a 0.3-px glow strip
  becomes a 1-px strip at 30 % (same light, no flicker).
- Bevels and chamfers: excluded by the 90 % rule (1.41 M of the fleet's 5.26 M sub-pixel faces are bevels vs
  3.85 M struts at the switch); widening them would only soften every plate edge.
- Slivers inside a smooth surface (a plate tessellated into 17-unit strips, 88 % of the Terran panel's faces):
  their patch is the plate; not touched.
- Decal strips lying on a hull with their own points: thin patches, widened to a 1-px band at their coverage
  alpha over the hull they already cover; harmless [I].
- Degenerate or two-point patches, zero-area faces: skipped.

## 4. Which records, and the switch (Q4)

Only `C` (index 1 of the compact ladder). Record 0 is drawn for `s >= T_pad` and stays vanilla, so close-up is
unchanged by construction; the pad (index 2) is the vanilla coarsest record kept for the collision tree and is
never drawn at Very High, so it stays vanilla too (at Low..High the pad draws un-widened below `T_pad`; accepted,
those settings are not the user's). No record above the switch is touched even though record 0 also carries
sub-pixel patches just above `T_pad` (spacedock: 8 % strut area at s 278): that is the near-field truss regime the
thin-region weight addresses.

`C` is drawn from `s = T_pad` down to `s = 1`, so a static width cannot be one pixel everywhere. The design size
`s_d = T_pad / 2` guarantees at least one pixel over the record's top octave and two pixels at the switch; below
`s_d` the strut is again sub-pixel but carries alpha `a = w / W_u`, so its coverage flicker has amplitude `a`
instead of 1 (the solar plant at the stand: 0.61 px at a ≈ 0.3-0.8, amplitude roughly halved, not removed; the
spacedock 1.35 px, shield_F 1.10 px, the outpost 1.96 px: solved at their stands; `pixel_figures_out.txt`).
`s_d = T_pad / 3` would put the solar plant at 0.92 px at 30 km at the cost of 3-px, 20-50 %-alpha bands at the
switch. Recommendation: `T_pad / 2` for the first flight with `--widen-design-fraction` as the knob, and, if the
30-km case still needs 0.985, either `T_pad / 3` for stations or a second widened record `[0, C_a:T_pad, C_b:T_pad/3,
pad]` (index budget: the pad moves to index 3, where the `0x100000` DEFAULT-technique flag fires at Low..High only;
body members grow by up to 100 %, a third overlay slot; not for the first step).

The pop at the switch: LOD 0 shows the true `w` px line (0.1-1 px, flickering), `C` shows a 2-px band at alpha
`w·k(T_pad)/2` (0.05-0.5). Bounded by construction to a 2-px-wide, dimmer-than-opaque band replacing a thinner
brighter line, with the same integrated light; it is a softening, not a brightness step (the Terran weld's -10 %
step does not recur). The user judges it at the spacedock's switch (8.9 km) and the solar plant's (9.4 km); the
per-body `NAME=T@N` threshold and the design fraction are the remedies.

## 5. Look and how to measure it (Q5)

Close up (record 0): nothing. At range: struts, antennas, trims and lattice strips at least one pixel wide over the
record's top octave, translucent in proportion to their true width, so a distant lattice reads as a soft grey mesh
instead of a sparkling one; below `s_d` a dimmer version of today's flicker. The station's overall brightness and
colour are unchanged (blend-exact compensation); silhouettes of plates unchanged.

Host-side, before any flight (`verification/results/lod-strut-widening/raster_compare.py`, to write with the op;
about 120 lines on the `above_share` rasteriser pattern): an orthographic raster of `C` at `k(s)` for s in
{stand, s_d, T_pad}, along the three body axes, with the game's 8 jitter phases (0.5-px offsets), before and after
the op, per pixel coverage-weighted colour with the blend applied. Reported: the histogram of per-pixel coverage
(the sub-pixel mass at coverage < 0.5 should move to >= 1 on the widened struts), the frame-to-frame class-flip share
across the 8 phases (the `rest_lattice.py` metric; the Terran weld predicted 0.497 -> under 0.05), and the summed
luminance before / after (within 2 % by the alpha quantisation). Flight check, the run340 stand (spacedock at s
188, solar plant at 65, shield_F at 95): `F8` 8-frame burst at rest, then the same under the 20 px/frame pan of
frame 1424, at unchanged TAA; scripts `rest_flicker.py` (present/current rms ratio on the station boxes, run341
0.129 at 0.85), `station_pixels.py` (thin share on B: 0.12-0.13 today), `sharpness_measured.py` (B pan present
0.294 today); then a second pair of bursts at the spacedock's switch distance for the pop.

## 6. The TAA side after the geometry fix (Q6)

Today: base 0.85, far target 0.985 (age-saturated 0.9846, effective history about 65 frames) on the footprint band
60-68 units/px (15.4-17.4 km at 5,120x1,440), thin-region 0.97, camera gate and 7x7 far clip. The far weight is
what blurs B under pan (present sharpness 0.294 at 5.85 px/frame). With struts at one pixel over the top octave and
dimmed below it, the far shimmer energy that motivated 0.985 (binary sub-pixel facets, taa-distant-line-fade.md
§2) drops by the coverage factor; the proposal, to be measured not assumed: far target 0.985 -> 0.95 (history
about 20 frames, three times less lag) and thin-region 0.97 -> 0.90, with the camera gate and clip unchanged; if
the rest burst is clean at 0.95, a third step tries the far stabiliser off (base 0.85 everywhere). Order of flights:
(1) geometry only, TAA unchanged, to isolate the effect (rest flicker and thin share on B and the spacedock, the
switch pop); (2) far 0.95 / thin 0.90 on the same stand; (3) far off if (2) is clean. One launch each, both stations
in frame, the same bursts as section 5.

## 7. Cost (Q7)

- Baker: patch decomposition 0.48 s per body single-threaded (295 s CPU for 617 bodies, max 2.2 s) on top of 4.2 s
  per body; the tool hash changes (`TOOL_FILES`), so one full rebake (fleet4 took 50 min wall at 2 jobs; expect
  about 55 min) [I]. Bodies whose strut share at `s_d` is under a floor (proposed 0.5 % of area) bake byte-identically
  apart from the manifest; the census says that is roughly half the fleet.
- Overlay size: atlas tiles duplicated for widened faces; bounded by their UV spans, expected +5-15 % of the 533 MB
  atlas bytes [A]; body members grow only by the duplicated points at the group boundary, well under 1 % [I]; the
  2 GB per-slot split absorbs it.
- Per frame: +1 draw per affected part and effect below `T_pad` (the alpha-class path adds up to four on the solar
  plant class); today `C` draws sum to 1,569 over the fleet, so under +300 on a fleet-wide basis and 1-2 per station
  in view [I]; no vertex or pixel work added, the blended band is 1-2 px wide; zero DLL and shader change; the far
  weight reduction is free.
- Effort: the op and tagging (about 250 lines), the kept-class plumbing and the alpha-class tiles in `lod_atlas`
  (about 120), manifest and census columns (40), the raster comparison script (120), the unit test (150): 2-3 days
  of implementation plus one rebake hour and two or three flights [A].

## 8. Risks and the proving fixture (Q8)

- **Resolve classification of the blended band.** A blend-on, z-write-on group with RT2 semantics like the shipped
  lattice cards; the lattice-baker-fix notes the pane group (blend on, z-write off) is a fade-arm owner. Whether a
  1-2-px band at alpha 0.3 casts a thin vote or is treated as a cutout owner is not verified; the run340 `motion_route`
  rows and the seam fixtures decide, and the darkened-opaque fallback (2.4) exists.
- **Softness at the switch** (section 4): a 2-px translucent band where LOD 0 had a thinner line; if the user
  objects, `T_pad / 1.5` or per-body thresholds.
- **Blend order among widened struts and the body's own alpha group**: source-over between overlapping bands is
  order-dependent; at 1-2 px and alpha under 0.5 the error is a few percent of a pixel [I].
- **Bevel and decal false positives** (section 3): 1.4 M bevel faces excluded by rule; residual decal strips get a
  1-px band at their coverage.
- **Mods**: the op is generic and self-gated by the census floor; a mod body with no thin patches is untouched; a
  text body compiles first as today.
- **Station A** is not fixed by this design (section 1.1); if the user's shimmer verdict rests on it, the asteroid
  specular / texture path is the next note.
- **Not verified**: native Windows behaviour is data-only and identical by construction, like the rest of the
  overlay; DXT5 alpha tiles and blend states are documented D3D9; nothing engine-private is added.

Fixture (host-side, `verification/analysis/test_lod_strut_widening.py`, no Wine): a synthetic BOB1 body with (i)
box struts w x w in cross-section at pitch p, (ii) a rib on a plate, (iii) a picture-frame rim, (iv) a plate
tessellated into slivers, (v) a bevelled plate edge, (vi) an antenna, materials opaque with a diffuse texture and
one alpha-tested card. Assert after the op at `k_d` with `w·k_d = 0.3` px: every strut, rib, rim and antenna patch
is `W_u` wide within 1 unit and unchanged along its axis; the sliver plate and the bevel are bit-identical; normals,
UVs, tangent records, faces and group materials unchanged except the widened class; widened faces sit in one blended
group per part with the copied material's four state parameters as specified; their tile alpha equals `w/W_u`
within 1/32; the orthographic raster (section 5) shows no coverage below 1 px on widened patches at `k_d` and a
summed luminance within 2 % of the original over sky and over a hull-coloured background; part bounds enclose every
point; the body serialises and re-parses with the same ladder; a body with no thin patch bakes byte-identically.
Real-body check under `verification/results/lod-strut-widening/`: `argon_spacedock` and `argon_L_solarpowerplant`
scratch bakes, the widened share matching section 1.2 (14 % / 12 % strut area at `s_d`), draws +1 per part, the
raster flip share at the stand sizes before and after.

## 9. Unknown, and what settles it

- Whether the resolve treats the blended band as thin or owner (above): a `motion_route` / RT2 row check on the
  first flight's capture, or the seam fixture with a 2-px alpha band.
- The user's tolerance of the switch softening: flight 1 at the spacedock's 8.9 km.
- Whether the solar plant at 30 km (0.61 px after widening at `T_pad / 2`) still needs 0.985: flight 2; the
  second-record ladder or `T_pad / 3` are the escalations.
- The 3 overlay bodies my census reader refused (`khaak_M5`, `KG_Split_turret_frame`, `paranid_stealth_generator`,
  both binary and text members): the batch's own reader resolves them; the op inherits that path.
- Station A's shimmer source (texture or specular on a 1,712-face rock at LOD 0): a crop spectrum of the run340
  HDR frames on blob A against the mip level of its diffuse and bump maps.

## Reproduce

```
cd verification/results/lod-strut-widening
python3 stations_in_view.py /tmp/x3-bottleX3-run340/session-20260926-170108-212.log 1017 1424 4078
python3 station_screen_positions.py /tmp/x3-bottleX3-run340/session-20260926-170108-212.log 1017
python3 thin_geometry_census.py --jobs 5 --out fleet_census.json > fleet_census_out.txt   # 62 s
python3 fleet_summary.py > fleet_summary_out.txt
python3 thin_geometry_census.py --jobs 1 --only <the four stations> --at <stem>=<stand s>,<T_pad/2> \
    --vanilla environments/asteroids/asteroid_B_ClassMine --at environments/asteroids/asteroid_B_ClassMine=76 > named_bodies_out.txt
python3 pixel_figures.py > pixel_figures_out.txt
```
