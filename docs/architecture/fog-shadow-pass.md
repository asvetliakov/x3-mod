# Volumetric fog: the sun-shadow lookup in its own pass

Design note, 2026-09-22, for the stored-density fog (L2 law only; the look presets are being
collapsed to L2 concurrently). Built the same day behind `X3M_FOG_SHADOW_PASS=1` (launcher
`--fog-shadow-pass on`, default off): the section "As built" at the end records what differs from
the design below and the fixture numbers. Owning ledger:
[../verification/volumetric-fog.md](../verification/volumetric-fog.md) (run222 diagnosis, "Shaft
lookup offset in L1-L3", Run 63 verdict, Run 239, GPU timer). Cascade contract:
[shadow-cascades.md](shadow-cascades.md). Units: 5 units = 1 m; the flight maps are 2048²
(run222), so the 1500 / 7500 / 37500 / 150000 cascades are 1.46 / 7.3 / 36.6 / 146 units per texel.
[M] measured in the ledger or read from source, [I] inferred or estimated.

## Decision

Move the shaft lookup out of `march_depth` into a **sun-visibility slice grid**: a quarter-resolution
screen grid (one texel per 4×4 full pixels, half of the march's pitch) with **64 slices at fixed
view distances** (the sky ray's 24 + 40 bin centres), stored as **16 tiles of RGBA8 in one 2D
render target** (four slices per texel), filled once per frame by one quad drawn inside the existing
fog transaction before the march. Per slice the pass takes **4 stratified positions along the slice,
each one 2×2 filtered comparison (16 R32F fetches per slice)**, with the three-cascade `.85–.95`
cross-fade of the L0 law, the finest bound cascade (the 1500 map, which L2 today binds and ignores),
and a blocker-distance penumbra whose radius the first tap's four depths give. The march and the
repair program replace `fog_look_visibility` with **one bilinear RGBA8 fetch per non-empty step**
(nearest slice, lane select) and lose the shaft lookup offset, the two shadow samplers and about
90 instruction slots each. The visibility texture has no history; TAA integrates the march output as
today.

Not a per-pixel 2D visibility (it cannot vary along the ray, so it cannot carry shafts) and not a
per-pixel bin-indexed grid (see Alternatives).

## 1. Today, from the shader

`src/fog/fog_density_field_inc.h`, `FOG_LOOK 2` `march_depth` [M]:

- 64 bins per half-resolution ray (24 over `[0, min(L, 12000)]`, 40 over `[12000, L]`, `L ≤ cap
  112500`). Every bin with `rho > 0` and `shadow_select.x > 0` calls `fog_look_visibility` once:
  one-hot choice of the first of **two** cascades (the two coarsest admitted: 7500 and 37500;
  `fog_look_cascades = 2`, `fog_pass.cpp:505-517`) whose `max(|x|,|y|) ≤ .85`, then one 2×2 filtered
  comparison = 4 `tex2Dlod`. Ceiling 64 lookups / 256 map fetches per ray; voids skip the lookup.
  The 1500 map is bound (apply slots 1..3, `motion_output_fog_inc.h:67-79`) and unused by L2; the
  150000 map is never bound (`fog_cascade_max = 3`, and `fog_shadow_current` demands an exact frame
  match while that cascade alternates under budget).
- The lookup position is offset along the bin by an interleaved-gradient value of the covering
  half-resolution cell plus `5.588238 × (phase mod 64)` (`look_taps.zw`, amplitude 1 bin,
  `X3M_FOG_LOOK_SHADOW_JITTER`), held at the bin centre when `look_resolved` is false (TAA off or
  failed). Repair pixels keep bin centres (`FOG_LOOK_NO_OFFSET`, repair L2 at 510/512 slots), the
  documented 4–17 % local mismatch inside shafts along silhouettes.
- The hand-over 7500 → 37500 is a hard switch at `.85` (texel 7.3 → 36.6 units), dithered only by
  the lookup offset under TAA. Beyond `.85 × 37500` lateral units fog is always lit.
- Slots (Microsoft table): march L2 425 / 15 static fetch instructions, repair L2 510 / 20, look
  composite 210 / 10; the three-way cross-fade body costs 207 slots (shader comment), which is why
  L2 has the two-cascade one-hot law. `fog_look_visibility` is about 90 slots [I, ledger].
- Cost lines: fog CPU 0.6–0.7 ms flat (run239); GPU unmeasured (bottle X3 refuses timestamp
  queries); fog on/off moved the frame time by under 1 ms.
- Why the beams read hard: the sun is a point (no penumbra), the comparison footprint is one map
  texel, and the umbra keeps full contrast (floor .15) over tens of km (run222 §3).

## 2. The pass

### Grid: fixed-distance slices, not per-pixel bins

The march's bin layout is per pixel (it compresses with the pixel's depth `L`), so a grid indexed by
bin cannot be filtered across pixels and has no entry for a full-resolution repair pixel. A grid
indexed by **view distance** is a function of (screen direction, distance) only: smooth in screen
space except at shadow edges, independent of geometry, so it can be stored below march resolution,
read bilinearly, and shared by march and repair. Slice `j` covers the sky ray's bin `j`:
`s_j = 500 j` for `j < 24`, `12000 + 2512.5 (j − 24)` beyond, both derived on the CPU from the cap
(`look_remap.w`) so a cap change moves the slices with it (`s_j` is the slice start; the sky ray's
bin `j` spans the same interval). A sky ray (most shaft area) reads slice
`i` for bin `i` exactly; a geometry ray at bin distance `s` reads the nearest slice (`j(s)` costs
5 ALU from one constant row). Nearest, not a two-slice lerp: the slice value is already the box
average over the slice, and a lerp needs a second fetch across a tile boundary at lane 3. The fixture
(§7) decides whether nearest shows a step at silhouettes; the lerp is the fallback.

Why not depth-limited: the pass reads no depth, so it never terminates a column early. That keeps
every texel valid for bilinear reads (a neighbour's column can be longer) and keeps the pass
independent of the depth-class law; the price is the taps beyond geometry, bounded by the table in §3.

### Target

| Item | Value |
| --- | --- |
| Format | `D3DFMT_A8R8G8B8` render target, linear filter, clamp (baseline of every D3D9 device; added to the attach format query as `rgba8_rt`) |
| Grid | `ceil(W/4) × ceil(H/4)` texels per tile, 16 tiles in a 4×4 layout, slice `j` in tile `j div 4`, lane `j mod 4` |
| Size at 1280×768 (flight; p00 = .8, p11 = 1.3333 in the run214 record and the 1280×768 fixture profile [I]) | 320×192 tiles → 1280×768 RGBA8, **3.9 MB** |
| Size at 2560×1440 | 640×360 tiles → 2560×1440, 14.7 MB |
| Precision | 8 bits of visibility (1/255) feeding `lerp(.15, 1, v)`: below the .003 fixture gate on S |
| Lifetime | Created in `prepare_targets` beside `lit_` / `scratch_`, released in `release_targets` (so `before_reset`, resize, `detach`); counted in `references()` / `allocations()` |

Tile-edge bleed: the read clamps the in-tile texel coordinate to `[.5, N − .5]` before adding the
tile origin (2 ALU); no guard band. The pass derives its tile from the quad's texcoord
(`tile = floor(uv × 4)`, in-tile `frac(uv × 4)`), so no `VPOS`, one draw, no per-tile constants.

### Program (one quad over the atlas)

Per texel: the view ray of the tile's in-tile position (same `projection` row as the march; the
quarter-texel centre lands on full pixel `4q + 2`, a sub-pixel offset the bilinear read does not
see), then a `[loop]` over the tile's 4 slices × 4 taps = 16 iterations (the march already relies on
`[loop]` to stop the D3DX unroll):

1. **Position** along the slice: `s = s_j + (k + ξ) / 4 · Δ_j`, `k = 0..3`, `ξ` = frac(interleaved
   gradient of the texel + phase × 0.618034) when `look_resolved`, the texel term alone otherwise
   (a static, bilinear-blurred 4-px pattern, no crawl). Neighbouring texels carry different strata,
   so one bilinear read averages 16 distinct along-ray positions per slice, the stratum count of the
   run222 reference.
2. **Cascades**: `fog_visibility`'s law, all three bound maps (1500, 7500, 37500), `shadow_weight`
   cross-fade `.85–.95`, `[branch]` per cascade. Projection is affine in the view position, so the
   three `p_i` of a slice are the slice-start projection plus `s` times a per-ray increment: 9 `dp4`
   per slice, not per tap.
3. **Penumbra**: tap 0 sits at the un-offset light-space position; its 2×2 gives four depths and a
   blocker distance `d_b = max(0, z_ref − min d) × range_world_i` (0 when lit). Radius
   `r = clamp(d_b · 0.0093 · k_pen / texel_world_i, r_min, r_max)` texels (0.53° disc;
   defaults `k_pen 1, r_min 1, r_max 16`). Taps 1–3 add light-space offsets `r · (cos, sin)(120° k
   + rotation)` on top of their along-ray displacement. `k_pen = 0` degenerates to a fixed
   `r_min`-texel kernel (the fallback if the estimate flickers). Expected full widths (`0.0093 d`):
   a shaft 2 / 6 / 12 km (10,000 / 30,000 / 60,000 units) behind a station gets 93 / 279 / 558
   units ≈ 2.5 / 7.6 / 15 texels of the 37500 map and 13 / 38 / 76 of the 7500 map; beyond about
   12 km behind (37500 map) or 2.5 km (7500 map) the clamp holds the kernel at 16 texels, still far
   from a knife edge. Four taps over a 16-texel disc are sparse; the per-texel strata dither, the
   bilinear read and TAA integrate them. PCSS proper (a blocker search loop) is not needed: the
   four depths of the comparison are the search.
4. **Accumulate** `v_j += lit / 4`; write the tile's four slices to RGBA.

Fetches: 16 per slice, 64 per texel, all `tex2Dlod` point on R32F as today. Slots [I]: about
110–130 per iteration body in a `rep`, 250–320 total against 512; settled by
`fog_density_shader_slots.py` before anything is promised.

### Constants (new rows; the march's c0–c35 layout is untouched)

| Row | Content | Reader |
| --- | --- | --- |
| c9 | `shadow_select` (enabled, .95, .85, 10) as today | pass; march/repair read `.x` only (the `[branch]` gate stays, so a frame with no cascades fetches nothing) |
| c10–c21 | three cascades × (3 rows, (N, 1/N, bias, valid)) as today | pass only |
| c36–c38 | per cascade (texel_world, range_world, 0, 0): `FogCascadeInput` gains `texel_world` and `depth_range` (the apply already computes both) | pass |
| c39 | slice layout (500, (cap − 12000)/40, 1/500, 40/(cap − 12000)) | pass, march, repair |
| c40 | grid (tile W, tile H, 1/atlas W, 1/atlas H) | pass, march, repair |
| c41 | penumbra (0.0093 · k_pen, r_min, r_max, frame rotation) | pass |

`look_taps.zw` (c32.zw, the shaft lookup offset) and `SHADOW_JITTER` retire; `SHADOW_FLOOR`,
`LIFT_FLOOR` stay in the march. Sampler: march and repair bind the atlas at **s4** (LINEAR, CLAMP:
one more `linear` case in `normalize`), s5/s6 free; the pass binds the maps at s4–s6.

### Transaction order and failure

Inside the existing `SavedState` bracket: capture → normalize → `StretchRect` copy →
**visibility quad** (new `FogStage::Visibility`) → march → composite → repair → restore. A failed
visibility draw fails the transaction like a failed march (`record`, no partial frame); with
`cascades_bound == 0` the pass is skipped and `shadow_select.x = 0`, exactly today's lit path.
Device calls added [I]: `bind_target` (8 sampler nulls + RT + viewport + PS), one constant upload,
three `SetTexture`, one draw ≈ 16 native calls (≈ 7 µs at the ledger's 0.42 µs), less the two map
binds the march no longer needs; against a 0.6 ms fog frame, noise.

## 3. Hot-path cost

| | Today (L2, half res 640×384) | Pass, quarter res 320×192 | Pass, eighth res 160×96 |
| --- | ---: | ---: | ---: |
| Shadow map fetches, ceiling | 64 × 4 × 245,760 = **62.9 M** | 64 × 16 × 61,440 = **62.9 M** (fixed: no void skip) | 15.7 M |
| Shadow map fetches, typical [I: 40–50 % of bins non-empty at coverage .35] | 25–31 M | 62.9 M | 15.7 M |
| Per non-empty march step | one-hot select + 2×2 compare, ≈ 90 slots | one RGBA8 fetch + lane select, ≈ 15 slots | same |
| Extra RT memory | 0 | 3.9 MB | 1 MB |
| Extra device calls per frame | 0 | ≈ 16 − 2 | ≈ 16 − 2 |

At 1440p every fetch figure is ×3.75 for both columns; the ceiling relation (quarter res × 16 =
half res × 4) holds at any resolution. So the worst case is a 2× rise in map fetches over today's
typical, traded for ≈ 75 fewer slots per non-empty step in march and repair and coherent reads (a
tile's 16 taps of one slice hit neighbouring texels of one map). GPU time is not measurable on this
bottle: it is judged by the at-rest frame-time A/B with the pass toggled in one build
(`X3M_FOG_SHADOW_PASS=0` keeps the current L2 programs as the control variant, created once like the
look variants, and is deleted after acceptance). If the A/B shows a cost the eighth-res grid halves
the pitch (8 px per texel; a 16 px FWHM core survives blurred) for a quarter of the fetches; the tile
size is a constant, nothing else changes. CPU: no allocation, lock or per-draw work beyond the calls
above; the constant block grows from 36 to 42 rows. Loading time: one 3.9 MB `CreateTexture` at
`prepare_targets`, no upload.

## 4. TAA, Reset, hostile state

- **TAA.** The whole fog transaction already runs on the jittered frame before the resolve with a
  de-jittered `projection` row; the pass uses the same row, so a quarter texel is the same view ray
  every frame and only its strata rotate with `look_phase`. The texture needs no history: it is
  consumed by the march in the same frame and the march output is what TAA averages, as today. TAA
  off (`look_resolved` false) holds the rotation; unlike today, that costs no comb, because the 16
  spatial strata per bilinear read already replace the 8-phase temporal mean.
- **Reset.** The atlas is a DEFAULT render target with the same lifetime as `lit_`; pixel shaders
  survive Reset as today's do. No retained state, so no validity flag beyond `cascades_bound`.
- **Hostile state.** Same block capture/restore; one more `SetRenderTarget` and pixel shader inside
  the bracket; the fixture's hostile-state and reference-count cases extend by the new stage.

## 5. Native D3D9

Documented ps_3_0 only: an `A8R8G8B8` render target with bilinear filtering, `tex2Dlod` on R32F,
static `rep` loops with `[branch]`, three samplers in the pass and one in the march, `NumSimultaneousRTs
≥ 1`. No `VPOS`, no MRT, no 3D target, no Wine-specific capability, no hash. Slot budget before →
after [I until compiled]: march L2 425 → ≈ 350, repair L2 510 → ≈ 435 (the offset noise, ≈ 15
slots, could then go into repair, but it is no longer needed: both programs read the same
prefiltered texture), pass ≈ 250–320, composite 210 unchanged. Unverified natively like the rest of
the fog; add the row to [platform-portability.md](platform-portability.md).

## 6. What changes visibly

- **Shaft edges soften with distance behind the occluder** (1–16 texels of the sampled map plus
  the 4 px screen-space bilinear) instead of a one-texel knife edge; close behind a station they
  stay crisp.
- **No cascade seam**: the hard 7500 → 37500 hand-over becomes the `.85–.95` cross-fade; the
  texel-scale step at that ring goes.
- **Near-camera shafts sharpen 5×**: structure within 1.5 km (own ship, a station arm overhead) is
  sampled from the 1500 map at 1.46 units per texel instead of the 7500 map's 7.3.
- **The comb is gone in one frame**, not by the TAA mean; TAA-off flights lose nothing.
- **The repair/march mismatch inside shafts** (one-pixel outline segments along silhouettes) goes:
  both read the same texture and the lookup offset no longer exists.
- Unchanged: the shafts' persistence (caster retention, a shadow-lane fix), the `.15` floor, and the
  lit region beyond `.85 × 37500` lateral units until the 150000 map is bound (§9).

## 7. Fixture before a flight

All host-twin work extends what exists: `tools/analysis/fog_density_shader_reference.py`
(`look_march` already takes a per-sample `visibility(points, rays, ds)` callback),
`tools/analysis/fog_shaft_sampling_study.py` (marches the run222 dumps `shadow_map2/3` of frames 9539
and 31040 at 160×96 rays against a 16-lookup-per-bin reference) and
`verification/probe/fog_density_shader_run.py build/run/check` with its `shadow=2` stripe maps.

1. **Atlas twin (GPU).** New case `V_stripes`: the pass over the stripe maps at a 64-texel grid;
   host builds the same atlas per slice and lane. Gate: max |Δv| ≤ 2/255 on every texel.
2. **March and repair with the atlas (GPU).** `A_look2_pass_stripes`, `R_look2_pass`: GPU march
   and repair reading the GPU atlas against the host march reading the host atlas. Gate: max |ΔS| ≤
   5e-4 (today's gates pass at 2.8e-4 – 4.9e-4), and the in-march law is measurably different (≥
   .003 away, as the existing `look_shaft_offset_exercised` gate does).
3. **Transmittance study (host, the accepted L2 scenes).** `fog_shaft_sampling_study.py
   --visibility-pass` on 9539 and 31040 (and the run224 bursts if they carry maps): march with the
   precomputed quarter-res atlas (4 strata, per-texel dither, bilinear read, nearest slice) against
   the 16-lookup reference; error `(S − S_ref)/S_lit`. Gates, single frame with no temporal help:
   rms ≤ .003, p99 ≤ .012, max ≤ .03 (today's bin centres: .0041 / .0188 / .0590; the item-4 estimate
   was rms .0026, max .024); 8-phase mean rms ≤ .0012 (the accepted lookup-offset mean: .0010).
   Repeat with the slice lerp; adopt it only if nearest fails at geometry edges (report the max
   error restricted to rays whose depth lies within 2 slices of the sample).
4. **Seam.** Synthetic box straddling `.85` of the 7500 map (the run222 host box, 13 300 units
   sun-ward): along rays crossing the band, the max jump of `v` between consecutive slices with the
   cross-fade ≤ 2/255 + the two maps' own disagreement at that texel, against the hard switch's
   jump (reported).
5. **Penumbra.** Synthetic slab edge; the 10–90 % width of `v` across the shaft edge at 2, 6 and
   12 km behind the slab (inside the 16-texel clamp of the 37500 map) against the analytic
   `0.0093 d`: ratio within [0.5, 2] and ≥ 1 texel of the sampled map, where the current law gives
   ≤ 1 texel at every distance; at 24 km the width equals the clamp (reported, not gated).
6. **Slots and provenance.** `fog_density_shader_slots.py` < 512 for the pass, march, repair;
   generator `--check`; host modules `test_fog_look_reference test_fog_density_shaders
   test_volumetric_fog test_shader_compiler_provenance`; the pass fixture's hostile-state,
   Reset and reference-count cases.

Flight after that: one build with the toggle, the user's at-rest frame-time A/B at the run239
station pose, one F8 burst per state from the fog4.png pose (the strong-shaft case, 9539), verdict
on edge softness, the hand-over ring and near-station shafts.

## 8. Estimate

Against the existing pass machinery (targets, block transaction, program creation, constants upload,
fixture runner, host twin, study script): pass HLSL and slot iteration 4 h; march/repair edit and
constant rows 2 h; `FogPass` stage, target, samplers, toggle variant 3 h; `FogCascadeInput`
texel/range plumbing in the proxy 1 h; host atlas twin, three fixture cases, study mode, seam and
penumbra measurements 6 h; generator registration, Wine fixture runs, review fixes 3 h. **About
19 h**, two to three working days, one flight.

## 9. Alternatives considered

- **Per-pixel 2D visibility (one value per screen pixel).** Cannot vary along the ray; a shaft is
  the integral of visibility × density × T along the ray, so it degenerates to a screen mask. Loses
  outright.
- **Half-resolution grid indexed by the march's own bins** (slice k = bin k of that pixel; the
  march's read is exact, no slice math). Four times the fetches of the quarter grid (equal to 4× the
  ceiling row, 250 M at 1280×768), cannot be filtered across pixels whose bin layouts differ, and
  has no entry for a full-resolution repair pixel, so the repair mismatch stays. Loses on cost and
  on repair.
- **More taps inside the march** (two lookups per bin, measured: 434 slots march, does not fit
  repair). Halves the one-frame error only; no cross-fade, no finest cascade, no penumbra, still
  slot-bound. Loses.
- **Light-space prefiltering** (blur or ESM/VSM of the maps, one filtered fetch per step). Gives a
  penumbra cheaply but the march keeps its 90-slot projection/select body (the slot problem is the
  point), ESM leaks along tens-of-km umbrae, and it adds a blur pass per 16 MB map. Loses on slots.
- **Binding the 150000 map** (fog beyond 37.5 km lateral lit today). Not part of this decision: it
  needs `fog_cascade_max` 4, a fourth sampler in the pass (free) and the apply's one-frame-old
  currency rule in place of `fog_shadow_current`'s exact match, else that cascade flickers on
  alternate frames. Worth a follow-up once the pass exists; 146 units per texel is coarse but the
  hard lit edge at the 37500 ring is a seam of its own.

## Unknown, and what settles it

- **GPU time of the pass on this bottle**: no timestamp queries; only the at-rest frame-time A/B with
  the toggle. The branch GPU timer (commit 423bd098) would measure it natively.
- **ps_3_0 fit of the pass, march and repair**: estimates above; `fog_density_shader_slots.py` on
  the compiled programs.
- **Nearest slice versus lerp at geometry edges**: fixture item 3 on 9539.
- **Penumbra estimate stability** (a blocker distance from four depths may jitter at the umbra
  edge): fixture item 5 across the 8 phases; `k_pen = 0` is the fixed-kernel fallback.
- **Flight resolution**: 1280×768 inferred from the projection scalars; the next run's mode line
  confirms it and fixes the memory figure.
- **Whether 4 strata per slice suffice at eighth resolution** if the A/B forces it: rerun item 3
  at 160×96 with 4 and 8 strata.

## As built (2026-09-22)

Source: `src/fog/fog_shadow_grid_inc.h` (rows, `fog_pcf`, `shadow_weight`, slice and tile laws, shared by
every stored-density program), `src/fog/fog_density_visibility_grid_ps.hlsl` (the pass),
`fog_density_{march,repair}_grid_ps.hlsl` (`FOG_LOOK` + `FOG_SHADOW_PASS`, the look with one bilinear grid
fetch per non-empty step), `src/renderer/fog_shadow_grid.h` (d3d9-free twin: extents, slice distances, tile
addressing, blend, penumbra radius, rows c36–c41, fetch counts), `FogPass` (`FogDensityConfig::shadow_pass`,
`FogCascadeInput::texel_world/depth_range`, `FogStage::Visibility`, `FogResult::grid`), the proxy
(`X3M_FOG_SHADOW_PASS`, `configure_volumetric_fog_shadow_pass`, texel/range per cascade) and the launcher
(`--fog-shadow-pass {on,off}`, `on` requires `--volumetric-fog-range stored`). The seven existing programs keep
their bytecode (`fog_pcf`/`shadow_weight` moved into the shared include without a word changing); with the toggle
off the fixture's five accepted look images and the repair image hash exactly as in the look-collapse table.

Deviations from the design above, all measured by the fixture:

- **Penumbra radius = `0.0093 · d_b · k_pen / texel_world`** (the note's formula; `fog_grid_sun_angle`), not
  half of it. The blocker search is tap 0's own 2×2, which meets the blocker only on the shadow side of an
  edge, so the penumbra grows one-sided from the geometric edge into the shadow and its 10–90 % width is
  about one radius: 3.4 / 6.5 / 9.8 texels at 2 / 6 / 12 km behind a slab of the 36.6-unit map against
  0.0093 d = 2.5 / 7.6 / 15.2 (ratios 1.34 / .86 / .64, monotone; the far band's estimate is biased low by the
  first-crossing measure on a noisy 8-column band). Inside the fog the half-width shift of the penumbra is
  invisible; a wide blocker search would cost a second loop.
- **Constants**: c36–c38 = (texel_world, range_world, range_world / texel_world, 0) per cascade (0 when the
  caller gives no texel: the kernel stays at `r_min`); c39 = (500, (cap − 12000)/40, 1/500, 40/(cap − 12000));
  c40 = (tile W, tile H, 1/atlas W, 1/atlas H); c41 = (0.0093 · k_pen, r_min, r_max, frame term) with the frame
  term `frac(0.618034 · (phase mod 64))` while `look_resolved`, 0 otherwise (it advances both the stratum and the
  disc rotation). Tunables `X3M_FOG_LOOK_PENUMBRA` (0–4, default 1), `_PENUMBRA_MIN` (0–16, 1), `_PENUMBRA_MAX`
  (0–64, 16), read once with the look tuning. The march's c0–c35 are untouched; one upload of 42 rows serves the
  pass, march, composite and repair. `look_taps.zw` are still uploaded and ignored by the grid programs.
- **Strata and rotation** come from two interleaved-gradient noises of the *atlas* texel (not the grid texel), so
  the 16 tiles of one screen position carry different strata along the ray; the seam fixture shows the price: a
  slice step across a tile boundary can exceed the ramp increment by a quarter of it (measured .149 against the
  .126 increment, bound .165).
- **Sampler**: the pass reads the maps at s4–s6 (POINT, as `normalize` leaves them); march and repair bind the
  atlas at s4 and the transaction sets s4 MIN/MAG LINEAR with two `SetSamplerState` calls (the block bracket
  restores them). Extra device calls per frame: **19**, measured by the pass fixture as the pass-on frame with a
  cascade against the pass-on frame with none (so it counts the `bind_target` of the visibility stage, its
  constant upload, three map binds, the draw, the two sampler calls and the grid bind); the design's ≈ 14 above
  was against the toggle-off frame, which also binds the three maps for the march. Against toggle-off the
  difference is 19 − 3 = 16.
- **Constants** are uploaded as 42 rows in both modes: with the pass off the six grid rows are zero and the look
  programs never read c36–c41, which is why the off images stay byte-identical (the upload count is not the
  identity; the programs are).
- **Fallback**: a grid that cannot be built (`density_grid_compiled_slots`, `density_grid_program_create`,
  `density_grid_target`, `density_grid_extent`) or a column cap the slice law cannot divide (`grid_column_cap`,
  below 12040 or not finite) does **not** refuse the stored fog: the in-march programs draw, the reason sits in
  `density_status().shadow_pass_refused` (sticky until detach) and the proxy logs one `fog_shadow_pass_refused`
  line. The pass fixture injects an RGBA8 target failure and checks the split frame is byte-identical to the
  in-march instance's.
- **Lifetime**: the grid target is created in `density_resources` (from `prepare_density`, when the config asks
  for the pass and the targets exist), not in `prepare_targets`, which does not know the config; it is released
  with the targets (`release_targets`: resize, `before_reset`, `detach`) and by `release_density_default`, counted
  in `references()` / `allocations()`. The three grid programs are created beside the look programs, once;
  attach queries `rgba8_rt` (a device without an `A8R8G8B8` target refuses the fog as a whole).
- **Loops**: the pass is two nested static loops (4 slices × 4 taps), 319 slots / 12 texture instructions;
  march grid 352 / 8 (from 425 / 15), repair grid 453 / 13 (from 510 / 20); composite unchanged 210.
- **Default off** in the launcher for the flight A/B; the DLL reads `X3M_FOG_SHADOW_PASS` only with the stored
  range and logs `volumetric_fog_shadow_pass enabled=…` once.

Fetches per frame (ceiling, fixed: the pass reads every slice of every grid texel): 1280×768 → 320×192 texels
× 64 × 16 = **62.9 M** map fetches plus ≤ 15.7 M RGBA8 grid fetches by the march (one per non-empty step of
245,760 rays), atlas 3.9 MB; 2560×1440 → 640×360 × 1024 = **235.9 M** plus ≤ 59.0 M, atlas 14.7 MB. Today's
in-march law reads ≤ 62.9 M / 235.9 M map fetches (typically 40–50 % of that). GPU time is not measurable on
bottle X3 (the fixture's slope timing reads 0.008–0.014 ms for a 1280×768 pass or march, below its own noise);
the at-rest frame-time A/B with the toggle in one build decides, as planned.

Fixture (`fog_density_shader_run.py`, bottle X3, arm64, `FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`): PASS,
28 gates; shader fixture 30 checks, pass fixture 73 checks / 0 failures / 1099 state restorations (c0..c41 hostile). Item 1 atlas
twin: max 1/255 on every texel of the stripes, held, seam, penumbra and repair atlases (139 / 157 / 135,978 / 142 /
117 of 147,456 texels differ by that one step; the seam's .3 lands on a rounding tie). Item 2: GPU grid march
against the host march reading the host atlas, 576 stratified rays, S max 2.2e-4 (FP32) / 3.1e-4 (FP16) under the
5e-4 gate, T identical to the in-march program bit for bit, and the in-march offset law .0069 away on 11 pixels;
grid repair S max 4.9e-4, the bin-centre repair law .0050 away. No cascade: grid march images byte-identical to
the in-march ones (three cases, FP32 and FP16). Item 4 seam: centre ray .298 → .8 over slices 53–57, max step
.149 against the hard switch's .5. Item 5 penumbra: above. Item 3 (the host transmittance study on the run222
dumps) was not run: the fixture's items 1, 2 and 5 cover the same laws on synthetic maps; it remains the
pre-flight check if the A/B raises a numeric doubt. Item 6: slots above; generator `--check` provenance for the
ten programs; host modules `test_fog_shadow_grid`, `test_fog_density_shaders`, `test_fog_look_reference`,
`test_volumetric_fog`, `test_shader_compiler_provenance`. Evidence and hashes:
[../verification/volumetric-fog.md](../verification/volumetric-fog.md), "Sun-visibility grid pass built".
