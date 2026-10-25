# Overlay material strength: the per-texel diffuse-strength ratio (option B)

Design note, 2026-10-25, for the user-approved direction "option B": keep one merged-LOD overlay draw per
(effect, occlusion map, cull) class and restore each source material's exact `g_MatDiffuseStrength` by baking
the ratio into a spare atlas channel that the proxy's pixel-program rewrite multiplies in at the shader's
diffuse-strength site. Implemented 2026-10-25 (section 12, "As built"); nothing installed. The committed alternative is bef0d269 (classes also
split by diffuse strength, `docs/verification/lod-overlay.md`, "2026-10-25 Run 202"). The user tests both and
decides. Every figure is measured in this session unless marked [i] (inferred); scripts and outputs are in
`verification/results/outpost-lod-material/` (`texel_lanes.py`, `strength_census.py`,
`strength_census_summary.py`, `*_out.txt`).

## 0. Decision in one paragraph

Encode the ratio in the **alpha of the specular atlas** (`x3m_lod_<body>_specular`, already baked for every
`--batch` body and already DXT5), as `a = clamp(s_i / s_merged / 2, 0, 1)` with `s_merged = max(area-weighted
mean, s_max / 2)`; every ratio in the fleet then fits (0 classes over 2.0), the DXT5 alpha block holds a per-tile
constant exactly, and with the DLL absent or the option off the body draws at `s_merged`, which is today's
area-weighted mean for 368 of the 388 affected classes. Every hull pixel program captured so far reads only
lane `.x` of the specular texel (27 of 27 SM3 programs with a diffuse join), so the lane is free in every family
without a per-class channel choice. The proxy adds a **twin of each hull pixel variant** with two instructions:
`add rT.w, rS.w, rS.w` right after the specular `texld` (ratio = 2a, no constant register) and one `mul` of the
diffuse term by `rT.w` at the join that `material_exposure_profiles_inc.h` already locates for all 108 selected
originals (`exposure_join`: product or addend form). The twin is bound only when the texture on the program's
specular stage is an atlas the baker marked: the baker writes a signature into the DDS header's reserved words,
the proxy's existing `D3DXCreateTextureFromFileInMemoryEx` hook (`src/proxy/loading_trace.cpp`, `texture`)
reads it on load and flags the created texture, `set_texture` copies the flag into the sampler shadow, and the
draw reads one byte. Fleet distant draws: 1,999 today -> 3,667 (bef0d269: 4,396), opaque classes 1,404 -> 1,465
(bef0d269: 2,193); 4 bodies fall to `no_draw_gain` instead of 41. Cost per overlay pixel: 2 ALU ops; per draw: one
byte compare; slots +2 per twin.

## 1. Facts this rests on

| Fact | Source | Status |
| --- | --- | --- |
| XT BUMPMAP `5f82ecac` (and 2s `f1b0e820`): `g_MatDiffuseStrength` = c12 scales only the directional diffuse sum, `mad r1.xyz, r1, c12.x, r4` at DWORD 1636 (instruction 84 of 116); the albedo then multiplies diffuse + specular + sat(v0) and the cube term, light map outside | `sm3_rules.py --disasm` on `ps_5f82ecacd39529cd.bin`; lod-overlay.md Run 202 | measured |
| The same join exists in every selected hull original and is already tabulated: `exposure_seeds[] {hash, DWORD, lanes, product}` (38 product joins, 70 addend joins), rewritten today by `exposure_join` when selective exposure is on | `src/renderer/material_exposure_profiles_inc.h`, `docs/reverse-engineering/remaining-hull-materials.md` | measured (table), reused |
| The specular texel: 27 of 27 captured SM3 programs with a join read lane `.x` only (`max(detail.a, spec.r)` gate, reflection `* spec.r`); `.yzw` are never read before the register is overwritten. Diffuse and light-map texels are read `.xyzw`: their `.w` feed `lrp r2.w, c4.x, rL.w, r1.w` -> `oC0.w`, and the bloom extract reads scene alpha | `texel_lanes.py` over the 43 captured `ps_*.bin` (`texel_lanes_out.txt`); hull-emissive-widening.md line 161 | measured for the captured 27; the other ~81 originals are inferred until the corpus check (section 10) |
| The overlay bakes a specular atlas for every `--batch` body (`specular=a.atlas_specular or a.batch`), the merged material names it in `t_SpecularTexture`; it is DXT5 today because the unused area is RGBA 0 (`bake_level`: "light/specular black alpha 0", `encode`: DXT1 only when alpha is uniformly 255); Run 202's distant draw bound four 1024² DXT5 atlases at s0-s3 | `tools/analysis/lod_atlas.py` 2015-2110, `lod_overlay.py` 2150; lod-overlay.md Run 202 | measured |
| Every slot shares one layout (tiles, 8-texel gutter, tile-aware mip chain to 1x1) | `lod_atlas.py` 40-70 | measured |
| The engine creates textures with `D3DXCreateTextureFromFileInMemoryEx` (import `0x00532334`) from the decompressed DDS bytes; the proxy already hooks it (`Hook hooks[]`, `texture()`), and the user's log shows it installed (`loading_hook name=D3DXCreateTextureFromFileInMemoryEx installed=1`, `C:\X3\x3m.log`) | `docs/reverse-engineering/executable.md`, `loading_trace.cpp` 760/890, x3m.log | measured; the install condition is `trace || gz_buffer || crypt_cache || adjacency || music` (loading_trace.cpp ~1590), see section 10 |
| The baker's `dds_header` leaves `dwReserved1[11]` (file bytes 32-75) zero; D3DX ignores those words | `lod_atlas.py` 486-497; DDS format | measured / documented |
| Per-stage sampler shadow (`SamplerShadow`: texture, levels, size, identity, filters) maintained by the `SetTexture` hook; variants are created at `register_pixel_shader` time ("creation-time shader variant: immutable after attach") and chosen per draw in `evaluate_draw` / `bind_variant_pair` | `src/proxy/motion_output.h` 3162, `motion_output.cpp` 1059, 2997, 4760-4790 | measured |

## 2. Q1: programs and the multiply site

Multi-strength classes under the (effect, occlusion, cull) key, fleet (1,155 overlay bodies, 388 classes in 371
bodies, 9.37 M faces): `xt_standard_lighting.fx` 347, `standard_lighting.fx` 25, `terran.fx` 5, `split.fx` 3,
`xt_terraformer.fx` 3, `argon.fx` 2, `khaak.fx` 2, `teladi.fx` 1; every class dominant names a bump texture,
cull 2: 351, cull 1: 37. Programs per family (BUMPMAP technique when the model
declares tangents, DEFAULT otherwise; `2s` for cull 1; hashes from `docs/architecture/xt-materials.md` and the
`hull-emissive-widening.md` family table):

| Family | Programs (ordinary / 2s) | Join form, DWORD (`exposure_seeds`) | Specular stage |
| --- | --- | --- | --- |
| XT standard BUMPMAP | `5f82ecac` / `f1b0e820` | product, 1636 / 1662: `mad r1.xyz, r1, c12.x, r4` | s2 |
| XT standard BUMPMAP_LOW, damage BUMPMAP | `6733b119` / `496049ce`, `d51cf763` / `31445adb` | product (c12) | s2 |
| XT standard DEFAULT | `fffdabd9` / `e6794b6e` | product (c12) | s1 |
| XT terraformer DEFAULT / BUMPMAP / LOW | `fd58e6b7`/`dd87737d`, `d22f2ce2`/`1de3d2dd`, `75fb9c6b`/`edaef099` | all six in the seed table (108 rows); join form and texel lanes not captured here [i] | s1 / s2 |
| standard_lighting BUMPMAP | `99153c14`, `c1452981` (+ 2s/_0000/_0001) | product: `mad r5.xyz, r3, c11.x, r4` | s2 |
| argon / boron / paranid / split / shared (khaak, teladi, xenon) / terran, DEFAULT and BUMPMAP | 6 + 6 each; e.g. argon `8759c783`, `63f96eba`; terran `3602b05c` | product (`c8.w`, `c8.x`, `c8.y`, `c8.z`, `c9.y`) or addend (terran: `mad r5.xyz, r3, r1.w, r4`, diffuse is `r4`) | s1 (DEFAULT) / s2 (BUMPMAP) |

So the site is one uniform MAD per program, already located and already rewritten by one function
(`exposure_join`); the strength multiply is the same edit with a different operand. The only per-program datum the
existing tables lack is the specular `texld` (stage and destination register): the XT profile has it
(`xt_pixels.texture[]`), the race profiles do not. It comes from the CTAB `SpecularTexSampler` row, as
`texel_lanes.py` does; the implementer adds a generated table `{hash, spec_stage, texld_dword, join_dword}` or
reads the CTAB at registration (the transformer already parses the program structure; a CTAB walk is a few
hundred DWORDs once per program).

## 3. Q2: the spare channel and its encoding

**Channel.** Specular atlas alpha. Not the diffuse or light alpha: both reach `oC0.w` and the bloom's per-source
attenuation reads scene alpha (hull-emissive-widening.md, "oC0.w"), and the baker carries real source alpha there
(glow masks). Not the bump atlas `.z` (the swizzled normal uses `.w` and `.y`; `.z` is a 5-bit DXT colour lane
sharing the block palette with `.y`). Not a fifth texture (a new fetch and sampler, +1 slot, +1 texture per body).
The specular lanes `.y` and `.z` stay free for later (section 6). Same channel in every family: no per-class choice.

**Value.** `s_merged = max(mean_area(s_i), s_max / 2)`, written into the merged material as today (16.16);
`a_i = clamp(s_i / s_merged / 2, 0, 1)`; the shader recovers `2 a_i`. Fleet ratios `s_i / mean`: p50 1.15, p90 1.88,
p99 2.81, max 7.02 (`khaak.fx`: 0.0 and 0.8, mean 0.114); 20 classes exceed 2.0 under the plain mean, **0** under
the `max(mean, s_max/2)` rule, so no class is clamped. Minimum ratio 0 occurs (materials with strength 0) and
encodes exactly. For freighter_base the two classes have `s_mean` 0.495 / 0.727, `s_max` 0.9 / 1.1, ratios
0.61-1.82 / 0.55-1.51: `s_merged` = the mean, so the fallback look equals the installed overlay's. The factor 2 is
emitted as `add rT.w, rS.w, rS.w`: no constant register, so the native validator's one-c#-per-instruction rule
cannot be touched by it.

**Precision.** DXT5 alpha stores two 8-bit endpoints and 3-bit indices per 4x4 block; a block whose texels all carry
one value (or two values) is exact (the baker's `_encode_alpha`: min/max endpoints, 8-value palette). Step 2/255 =
0.0078 in ratio units, i.e. <= 0.4 % of the strength at ratio 1.87 (freighter_base material 12: 1.1 ->
238/255 * 2 * 0.589 = 1.0998). RGB565 colour quantisation of the diffuse tiles is coarser (1/31, 1/63).

**Tiles, gutters, mips.** The ratio is a per-tile constant, so `bake_level` writes it over the tile's footprint
(content + gutter) at every level exactly like a constant source texture: the gutter carries the tile's own ratio,
bilinear filtering never mixes two tiles until the level where the 16-texel gap between tiles falls below one texel,
the same level at which colours already bleed (accepted since the tile-aware chain). A 4x4 block that straddles two
tiles' gutters holds two values (exact); three or four values occur only where tile edges are not 4-aligned, with
error <= (max - min)/14 on those gutter texels. The baker's `encode()` already reports per-channel decode error over
`used_mask` (content + gutter): the test asserts alpha p95 error <= 1/255 and max <= 2/255 for the specular slot,
and the baker may 4-align tile origins if a body fails it (gutter 8 is 4-aligned, so content alignment aligns
every edge). **Background** (unused area) alpha becomes 128 (ratio 1.0) instead of 0, so deep-mip bleed from the
background is neutral rather than black; RGB stay 0 (no specular). `NULL` specular sources keep their constant
black RGB and take the tile's ratio in alpha.

**sRGB / linear.** `D3DSAMP_SRGBTEXTURE` converts RGB only; alpha is sampled linearly whatever the proxy's linear
material path sets on the stage, and the linear transform's `transfer`/`gain` helpers write `.xyz` only
(alpha-tested-materials.md). The ratio is read raw.

**Format cost.** The specular atlas is DXT5 today (unused alpha 0), so no size change; a fully used atlas that was
DXT1 would become DXT5 (0.7 -> 1.4 MB at 1024²) [i: not seen in Run 202; count in the rebake log].

## 4. Q3: identifying an overlay draw

**Marker.** The baker writes `dwReserved1[0..2]` of every atlas DDS header (`dds_header`) as `'X3ML'`, a version
word, and a lane descriptor (`slot id | lane | K`), i.e. 12 bytes at file offsets 32-43; the specular atlas's
descriptor says "alpha = diffuse-strength ratio, K = 2". D3DX ignores the reserved words; vanilla and mod DDS files
carry zeros or tool signatures (`NVTT`, GIMP), none equal to the magic. The `.pck` wrapper is decompressed by the
engine before the D3DX call, so the hook sees the plain header.

**Hook.** `loading_trace.cpp::texture` already wraps `D3DXCreateTextureFromFileInMemoryEx` and sees `(data, size,
*out)`. On success, if `size >= 128`, `data[0..3] == 'DDS '` and the magic matches, record `*out` (the
application-visible texture pointer, i.e. the ownership `Texture` node, since D3DX called `CreateTexture` on the
wrapped device) with its descriptor in a small table keyed by pointer and `resource_id` identity (pointer reuse
after release is caught by the identity, as `SamplerShadow.identity` does for widening). Clear the entry when the
node is destroyed (the ownership layer sees the final `Release`). Today the d3dx rows install only when a loading
option is on (`trace || gz_buffer || crypt_cache || adjacency || music`); the strength option must install this one
row unconditionally through the same `Hook` table. Cost: one 12-byte compare per texture load (about 1,700-2,700
calls per run, loading-profile notes), nothing per frame.

**Per SetTexture.** `set_texture` looks the pointer up (hash map, only for 2D textures with `levels > 1`) and stores
the descriptor byte in `SamplerShadow` (`atlas_lane`, 0 = none). **Per draw.** The route reads
`samplers_[spec_stage(program)].atlas_lane == ratio_in_alpha` and picks the twin: one byte load and compare on
draws that already pass the hull-pair gate; no `Get*`, no allocation. A vanilla draw never binds a marked texture,
so the twin is never bound for one: misidentification would double the diffuse of a vanilla material whose
specular alpha is 255, which is why the identification is by the baker's own signature and not by size, format,
level count or constant values.

**Fallbacks.** DLL absent, option off, program without a twin (no join, lanes other than `.x` read, validator
refusal at creation), or the texture unmarked: the engine draws the merged material at `s_merged`, i.e. today's
area-weighted mean except in the 20 classes where `s_max / 2` is larger (those draw brighter than today, never
brighter than their brightest member). The baker records `s_merged`, `mean`, `s_max` and the per-material ratios in
the body's bake summary and the overlay marker (`addon/NN.x3m-lod.json`), so a flight log can be checked against
them. An overlay baked before this change has no header magic and keeps drawing as now; a DLL without the feature
ignores the header.

## 5. Q4: interaction with the existing transforms

The twin is an edit of the **already combined** program (motion output + linear material + fill/share + light-map
gain + widening + exposure, whichever the pair gets), applied last, like `exposure_join` runs on the material-
rewritten join:

1. After the specular `texld rS, v1, sN` (located by stage; the destination register from the instruction):
   `add rT.w, rS.w, rS.w`. `rT` is a temporary the structure proof shows unwritten by the original and the other
   blocks (the widening's `rG = max temp + 1` rule; `r16.w` is free when exposure is on because `exposure_join`
   writes `r16` with `seed.lanes` = `.xyz` or `.w` only at the join). The copy must precede the first overwrite of
   `rS.w`, which in the race programs is 2-6 instructions after the `texld` (`r0` is reused at once) and in 5f82 is
   `dp3 r2.w` two instructions later.
2. At the join (`exposure_seeds` DWORD, revalidated as `exposure_seed_valid` does: a MAD, temp destination, exact
   lanes, no `_sat`, no relative addressing):
   product form `mad d, a, b, c` -> `mul rT.xyz, a, b` ; `mad d, rT, rT.w, c` (`.xyz` lanes; the diffuse product is
   scaled, the specular addend `c` untouched);
   addend form `mad d, a, b, c` -> `mul rT.xyz, c, rT.w` ; `mad d, a, b, rT`.
   With selective exposure on, the exposure's `mul r16, r16, c222.x` stays and the strength `mul` is one more
   instruction (each instruction still reads at most one `c#`; the strength instructions read none).

Slots: +2 per twin (+1 temporary); the largest twin today is 904 slots (`f1b0e820` share + gain + widening,
engine-light.md), far under the 32,768 plan cap. Variant count: one strength twin for each pixel variant a hull pair
can bind (`hull_lightmap_variant`, `hull_lightmap_widen_variant`, `sun_original_lightmap[_widen]_variant`, fill,
share, engine twins) for the 108 originals: the variant objects created at registration roughly double for hull
programs (object creation is cheap; the driver compiles a program at its first draw). Driver compile at first
distant draw per (program, option set): tens of milliseconds cold per 900-slot program by the measured
0.65 s / 4k-slot rate [i], a one-time hitch per program while the shader cache is cold; log the twin creations with
the existing `*_variant device=.. original=.. create=..` rows. Motion output, depth output, the far-fade `c217.w`
MUL and the light-map gain read other registers and lanes and are unchanged; the twin's bytes equal the parent
variant's except for the two insertions, which is the host test's byte-exact oracle.

## 6. Q5: scope

Specular strength (c9 site, `mul r4.xyz, r4, c9.x` at 1624 in 5f82) and reflection (c11, 1725) are linear factors
and could ride in the specular atlas `.y` and `.z` lanes the same way (DXT colour lanes: 6 and 5 bits, a per-tile
constant encodes exactly up to that quantisation because the block's colours lie on a line in RGB). Specular power
(`pow`, c10) and bump strength (c13, before the normalisation) are not plain multipliers of a sum; detail texture,
cube map and `g_bColor_Mixing` are texture identities or a static branch and cannot be folded per texel at all
(census residual: 420 / 407 / 251 / 313 / 335 / 303 / 182 classes). This step: **diffuse only**, lane `.w`, with the
descriptor byte reserving values for `.y`/`.z` so a later step needs no new marker. Alpha groups stay as bef0d269
(one record per alpha material; 2,181 of the 3,667 draws), a separate topic.

## 7. Q6: verification

Host, before any flight:

1. Baker (`test_lod_overlay_batch` additions): `s_merged` rule; `a_i` per tile; specular alpha decode error on
   `used_mask` (p95 <= 1/255, max <= 2/255); background alpha 128; header magic and descriptor at bytes 32-43 of
   every atlas DDS; the other three atlases byte-identical to a bake without the feature except the header words
   (the layout and tiles must not move).
2. Transform (`test_hull_lightmap_gain`-style oracle over the local corpus): for each of the selected originals
   with a specular sampler, twin = parent variant with exactly the two insertions at the predicted DWORDs; the
   programs whose specular texel reads a lane other than `.x` get **no** twin (refusal counted); every twin passes
   `D3DXDisassembleShader` and, in the WSL harness, the native validator
   (`verification/results/hull-widen-native-validator/native_validate.cpp`, one `c#` per instruction already
   proven for the parent).
3. Coverage: `texel_lanes.py` over the restored corpus (`restore_shader_sweep.py`, or `index_shaders.py` on the
   game archives) asserting `SpecularTexSampler` lanes `x` only for every `ps_3_0` hull original; today 27/27 of
   the captured ones.
4. Identification unit test: a DDS header with and without the magic through the hook's parser; a texture flagged,
   released and re-created at the same address is not flagged.

Scratch bake: `lod_overlay.py --game /mnt/c/X3 --batch --only stations\sadiablo\freighter_base --mod none --jobs 1
--out <scratch>`; `check_bake.py` extended to print, per source material, `s_i`, `s_merged`, the tile's alpha and
`2 a s_merged / s_i` (expect 1.000 +- 0.004); expect C with 5 groups (2 opaque classes + 3 alpha), 4 atlases, header
magic on all four, specular DXT5.

Flight (user, after the fleet rebake with `x3m-regenerate --batch --sync`, no `--trust-tool`): F8 pair at the
freighter_base outpost, far (lod 1) and close (lod 0), debug + perf on. Compare with Run 202 (frames 2410 / 5735):
distant draws for the station **2 + 3 = 5** (was 2; bef0d269 would give 12), PS `5f82ecac` with `c12 = 0.4952` on
the cull-2 class and `f1b0e820` with `c12 = 0.7272` on the cull-1 class; `ring_stats.py` on the same HDR box: the
distant ring mean / p95 / CV should move from 0.107 / 0.220 / 0.71 towards the close 0.146 / 0.361 / 0.93 (material
12 trims no longer 0.54x, materials 13 and 8 no longer 1.47x; `material_lum.py` eroded means should order as the
authored strengths 1.1 > 1.0 > 0.5 = 0.5 > 0.4); the log's twin rows (`strength_twin ... create=0`) and a
`strength_draws` counter equal to the number of overlay opaque draws in the frame; frame-time telemetry unchanged
within noise; and a pass by a vanilla-shaded ship to confirm no twin is bound off the overlay (counter stays at
the overlay draw count).

## 8. Q7: draw count and per-draw cost

`strength_census.py` over all 1,155 overlay bodies (0 errors; same model as `class_census.py`: record 0's visible
output groups per part, kept effects and hidden parts the same both ways; widening, light-bleed keeps, recipes and
the 60,000-point split not modelled):

| Key | Distant draws (fleet) | Opaque classes | Alpha groups | `no_draw_gain` bodies |
| --- | ---: | ---: | ---: | ---: |
| K2 (installed overlay: effect + occlusion) | 1,999 | 1,404 | 574 | - |
| K4 (bef0d269: + cull + diffuse strength) | 4,396 | 2,193 | 2,181 | 41 |
| K3 (this design: + cull) | 3,667 | 1,465 | 2,181 | 4 (`m6maingun_static`, `terran_tl_atmolifter`, `terran_tl_atmospheric_lifter/hull`, `torus_decals_fighterdock`) |

Record 0 draws 15,333. The K3 opaque increase over today is 61 classes (cull only); the rest of the +1,668 is the
alpha-group split shared with bef0d269. freighter_base: 2 -> 5 (bef0d269: 12). Per-draw CPU of identification: one
byte load and compare in the route for hull-pair draws, plus one hash-map probe per `SetTexture` of a mip-mapped 2D
texture (the engine's SetTexture rate is already shadowed per stage) [i: not measured; the frame-timing state
counters would show it].

## 9. Native Windows behaviour

Everything used is documented D3D9 / D3DX: the DDS header words are the file format's reserved area, the hook is the
existing IAT hook on the game's `d3dx9_37.dll` import, the twin is a `ps_3_0` program created with
`CreatePixelShader` and checked by the native validator in the WSL harness before any flight, the per-draw choice is
the existing variant selection, and the alpha lane's linear sampling under `SRGBTEXTURE` is specified by D3D9. No
Wine-specific export, layout or hash. The user plays native Windows, so the flight in section 7 is the native
evidence; the gap to record in platform-portability.md is only that the hook's install condition changes.

## 10. Unknown, and what settles it

- The ~81 selected hull originals not among the 43 captured programs: whether all read the specular texel `.x`
  only. Settled by step 3 of section 7 (corpus restore + `texel_lanes.py`); the transformer refuses a twin
  otherwise, so the risk is a body drawn at `s_merged`, not a wrong image.
- Whether every overlay texture reaches the device through `D3DXCreateTextureFromFileInMemoryEx` (the jpg/tga path
  does, loading-profile-run1.md; the `.pck`/`.dds` path is inferred from the import table). Settled by one loading
  trace of a save with overlay bodies: count of `texture()` calls whose header carries the magic versus the number
  of atlas members in the marker file; an alternative would be an observer on the ownership `Texture::UnlockRect`
  shell (plain forwarder today), reading the same words from the locked level 0.
- Whether any shipped DDS carries non-zero `dwReserved1` words that collide with the magic. Settled by scanning the
  catalogues' DDS headers (`inspect_x3.read_catalogue`), one pass, no bytes kept.
- The driver's first-draw compile hitch for the twins on the user's GPU: read from the existing compile telemetry
  in the first flight.

## 12. As built (2026-10-25, implemented in the worktree; not installed, not flown)

Implemented as designed with these deviations and settled unknowns:

- **Alpha materials (scope change by the user, 2026-10-25).** The pre-bef0d269 alpha path is back: one alpha group per
  part on the dominant alpha material (`synth_materials` remap when the members' `g_Mat*` differ), not one record per
  alpha material. freighter_base therefore bakes **3** groups (2 opaque classes + 1 alpha group), not the 5 of section
  7; the ledger's fleet count is K3 with one alpha group per part (section 8's 3,667 assumed the bef0d269 alpha split).
  The pale beam band on freighter_base is accepted against about 1,600 extra draws.
- **Tiles shared by materials of different ratios.** `plan_layout` adds the alpha byte to the tile key when the
  specular slot is baked, so two materials of one texture set but different `s_i / s_merged` get tiles of their own
  (the ratio is a per-tile constant). The other three atlases stay byte-identical for a given class set when only the
  strengths change (host test); a body whose materials share textures across strengths gets more tiles than before.
- **Twin as a post-transform, created lazily.** The twin is built from the parent variant's final words (any of the
  14 forms the pair selection binds: motion, fill, gained, widened, share, share gained, share gained widened, and the
  engine-light twin of each), not inside `original_fill_transform`: the join is located as the seed's five words
  matched byte for byte in the parent (refused unless exactly once, after the texld and at flow depth 0), the specular
  texld by stage (refused unless exactly one, at flow depth 0, writing `.w`), `rT` = the parent's highest temporary
  + 1. Registration keeps only the original's words for a program with a specular stage (about 100 x 6 KB); the twin
  of a (program, kind) is created at its first marked selection in `bind_variant_pair` (the parent form regenerated
  from its options, deterministic; one `CreatePixelShader` on the render thread, where the driver's first-draw
  compile happens anyway), with no retry after a refusal or failure. A session that never binds a marked atlas
  creates nothing. Partial precision on the join destination is kept as the original had it.
- **Marker.** `dwReserved1[0..2]` = `'X3ML'`, version 1, `slot | lane << 8 | K << 16 | payload << 24`; the specular
  atlas carries `(3, 3, 2, 1)`, the other three `(slot, 0, 0, 0)` (counted by the proxy, no lane). `lod_texture_mark.h`
  is the shared parser (host-tested against the baker's bytes).
- **Identification.** The D3DX hook (`loading_trace::set_texture_observer`, the row now patched for this option
  alone, scope `lod_strength`) records marked textures by pointer with the proxy's resource identity in a 4096-entry
  open-addressing table owned by the device's `MotionOutput` (under the capture lock, like the device cache and the
  identity counter it touches); the ownership layer's new `claim_texture_release_observer` drops the entry at the
  texture's final Release (one atomic load per final release, the lock and a probe only while marked textures are
  stored), so dead entries cannot fill the table; a full table logs once at the standard tier and the texture draws
  at the merged strength. The SetTexture hook probes the table on every non-null bind (one count load when it is
  empty; one hash probe otherwise; `GetPrivateData` only on a hit, a stale pointer is dropped and counted) and stores
  the lane byte in `SamplerShadow.atlas_lane`; the draw compares one byte. Option: `lod_material_strength` (ini,
  built-in on).
- **Section 10 settled on the host.** (a) all 100 reviewed hull originals of the 751-program corpus read lane `.x`
  alone of the specular texel (the eight asteroid / glass programs have no specular stage): twins for 100/100 in every
  form, 1,400 twins, native validator S_OK with no message for all 1,400 (`native_validate_twins.json`); (b) 9,386 DDS headers in the 28 catalogues:
  15 with a non-zero reserved word (14 `GIMP`, 1 other), 0 carrying `'X3ML'`; (c) still open for the flight: the
  debug-tier `lod_strength_frame` row (`draws`, `marked`, `no_twin`, `seen` per atlas slot, table size, stale) and the
  first 16 `lod_strength_texture` rows; (d) the compile hitch: open for the flight, now paid per (program, kind) at
  its first marked draw instead of at registration.
- **Performance pass (ledger table "Performance").** Per routed draw: one byte load and branch on
  `shadow_.strength_stage` (0 for every program without twins, including every vanilla draw); on a hull-pair draw of a
  twinned program one more byte load (`samplers_[stage].atlas_lane`), and only on a marked draw at most 7 pointer
  compares; no map, hash, lock or atomic per draw. Per SetTexture: one load of the table count (empty: done), else one
  hash probe inside the lock the hook already holds (`PlainHookGuard`); measured natively 0.27 ns (empty) / 0.9-1.2 ns
  (1,500 entries, throughput, cache-resident). Per D3DX texture load: a 12-byte header compare (1.3-1.5 ns measured);
  the unconditional row itself adds the existing hook shell (a QPC pair and the span bookkeeping) per load, nothing
  when the option is off (the row is not patched then). Per final texture release: one relaxed load; with marked
  textures stored the capture lock, a map lookup and a probe (erase 1.6 ns measured). Twin creation: lazy, 1,400 twins
  of the corpus transform in 0.26 s on the host g++ build including file writes (about 0.2 ms each, inferred for the
  i686 build), plus one `CreatePixelShader` per (program, kind) actually bound. GPU: +2 ALU per overlay pixel (the
  `add` and the `mul`) on marked draws only; every other draw binds the parent program unchanged.
- **Follow-up after Run 203 A (2026-10-25, worktree; not installed, not flown; ledger `lod-overlay.md`, "follow-up
  build after Run 203 A").** The 61,376 no_twin draws of that flight coincide with fade-owner draws on the sun lane
  (8,973 / 8,973 frames, measured; the draw identity is inferred). The fade owner binds `sun_motion_variant`, which
  was none of the 7 kinds. That form is now kind 7, `smotion`: the motion variant plus `def c221` and
  `mov oC2.g, c221.x`. Its twin is the motion twin with the same two additions (host oracle: word for word, 100/100),
  and it has no engine-light twin. The 15 forms give 1,500 twins, all passing the native validator. Still without a
  twin: the material route and XT DEFAULT ordinary (linear materials only), now counted in marked / no_twin. A
  debug-tier `lod_strength_no_twin` row names each distinct (ps, form, reason), up to 64. `lod_strength_frame` is now
  written on change, on no_twin onset or on a 10 s cadence, with interval sums. Replayed on Run 203 A that is 33,405
  -> 28 rows (inferred). `lod_strength_session` is written once, at release or at process exit. Each creation gets two
  `lod_strength_variant_frame` rows (after its frame's Present and after the next frame's) for the first-use compile
  question.

## 11. Alternatives considered

- **bef0d269 class split (committed).** Exact and DLL-independent, but 4,396 draws (x2.20) against 3,667 here, 2,193
  opaque classes against 1,465, and 41 bodies lose their draw gain against 4. Keeps its place as the fallback and
  as what a DLL-less install sees today.
- **Texel pre-scale** (approved earlier, dropped in Run 202): scales specular, ambient and reflection by the same
  factor because the albedo multiplies the whole bracket; 0.36x on freighter_base material 13. Inexact by design.
- **Ratio in the diffuse or light-map alpha.** Both reach `oC0.w` and the bloom's scene-alpha read; the diffuse
  alpha also carries baked glow masks. Would need a second edit of the alpha `lrp` and change bloom on every overlay.
- **Ratio in the bump atlas `.z`.** 5-bit lane sharing the DXT colour palette with the normal's `.y`; perturbs the
  normal encoding and quantises the ratio to 1/31.
- **A separate ratio texture.** New sampler and `texld` (+1 texture slot, a free sampler stage per program, one more
  1024² texture per body); the specular alpha is already fetched.
- **`s_merged = s_max`, ratio <= 1, no factor 2.** Simplest encoding, but a DLL-less or option-off install draws
  every member at the class maximum (freighter_base material 13 at 2.75x), i.e. worse than today; the
  `max(mean, s_max/2)` rule keeps today's look for 368 of 388 classes at the cost of one `add`.
- **Identification by texture size / format / level count or by a marker value in a material constant.** Vanilla
  DXT5 1024² mip-mapped textures exist; constants are per material and would need a dead constant lane in every
  family. A false positive doubles a vanilla material's diffuse. The header signature cannot occur by accident.
- **Identification by hooking the engine's texture-name resolver** (`0x004f4160` / `0x004f3510`,
  texture-lookup.md): an EXE hook with its own site validation, where the D3DX import hook already exists and sees
  the bytes.
- **Per-vertex encoding.** The engine's vertex formats and vertex programs are fixed; the baker cannot add an
  attribute the pixel program would read.
