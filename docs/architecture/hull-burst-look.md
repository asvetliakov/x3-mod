# Hull burst look: the fireball body, its glow and its flash

Design note, 2026-11-07, ratified 2026-11-07 (main session). Amends the shared body laws of [weapon-look.md](weapon-look.md) 3.1 (the hot-gas burst, the
glow, the flash) and [hull-impact-classes.md](hull-impact-classes.md) 3.1 / 4 (the per-class anatomy keeps its
structure; the numbers of its puff, glow and flash rows change here). It does not edit either note. Context: two
flights rejected the staged hull hit as a lamp ("need good modern effects, not lazy flashes, that's a downgrade";
"sizes always start too small"). Ground truth is the native fixture's own stage dump (`stage_dump`,
`run_weapon_effects.py --host native --dump`, linear, clamped, 1080p; measured by `verification/results/weapon-effects-burst/measure.py`: lit pixels
above 10 % of the element's own peak, equivalent radius). Markers: [M] measured from that dump; [S] in the source on
`effects` `a66967eeb`; [R] the reference study [impact-effect-references.md](../reference/impact-effect-references.md);
[C] chosen here; [I] inferred. r = r_hit = 2 U (0.35 + 0.65 k) [S]; x = t / L_b the burst's age fraction.

## Outcome

**The body is a thresholded three-octave noise ball with lobed silhouette, eroded from the rim inward; everything else
is sized and lit from it.** The burst doubles (energy R_b 2 r, every class x 2), is drawn at two octaves at least (the
one-octave LOD under 96 px is abolished: it was active in every measured frame, section 1), and gets the standard
VFX alpha-erosion law (density = step((fbm − threshold(age, radius)) / softness)) so it has a sharp ragged edge, holes
that open mid-life and clumps that are the last to go. The glow becomes the light *of* the body: R_g = 1.5 R_b, peak
1/3 of the body's, the body's own heat and life, scaled per owner cluster by 1 / (1 + (N − 1) / 2.5) so 20 hits do not
add to a sun. The flash becomes a 40 ms four-arm star of unequal arms with a 0.12 r core and no halo. A dark smoke
puff (the same impostor in the scorch pass's multiply state, over the hull only) runs from 0.5 L_b to 1.6 L_b. No
noise atlas now; it is the fallback if the three-octave program fails its budget row. Note: `docs/architecture/hull-burst-look.md`.

## 1. What the dump shows and why it reads as a lamp

| Case, element [M] | 40 ms | 120 ms | 296 ms | 696 ms |
| --- | --- | --- | --- | --- |
| energy_single burst, 10 % radius px / peak | 29 / 0.49 | 58 / 0.46 | 69 / 0.11 | 0 |
| energy_single glow | 200 / 1.16 | 182 / 0.73 | 57 / 0.40 (the ember) | 57 / 0.23 |
| heavy_single burst | 33 / 0.54 | 54 / 0.40 | 57 / 0.04 | 0 |
| heavy_single glow | 199 / 1.98 | 190 / 1.45 | 36 / 0.69 | 36 / 0.40 |
| energy_fighter60_20 burst / glow peak | 0.80 / 10.4 | 1.05 / 4.05 | 0.28 / 0.91 | 0 / 0.45 |

Three causes, each in the source [S]:

1. **One octave.** `puff_lod_px` 96 scales to 72 px at 1080 rows; the single hit's radius is 58–69 px, so every
   measured burst was drawn with one octave of value noise (`look.w < 0`). Value noise at 2.2 cells per radius with
   amplitude 0.9 on `1 + 0.9 (2 n − 1)` saturated at 1 gives a mottle of about ±25 %, multiplied by the smooth chord
   column: a blob. The erosion `− 0.6 (1 − T)` is uniform over the ball, so it dims rather than dissolves.
2. **The glow outshines the body.** R_g 3 r (10 % radius 0.83 R_g = 200 px, so r ≈ 80 px [I]) against a body of 58 px:
   3.4x wider at 120 ms, 7x at 40 ms, and 2.4x (energy) to 3.6x (heavy) brighter at its peak. The brightest, largest
   thing in the frame is a smooth `(1 − d²/R²)²` light: the lamp.
3. **The flash is a disc in a halo** (`weapon_sprite_ps` flash kind): a white disc of 0.25 r and an e-folded halo of
   1.0 r; the halo joins the glow into one soft tan field at 40 ms.

## 2. The burst body

### 2.1 Frame and inputs

Per hit the CPU gives the impostor (one quad, `weapon_fireball_vs/ps`) the sphere (view centre biased 0.5 r to the
eye, R_s(t) = R(t) (1 + lobe_amp)), its heat T(t) = 1 − x, E_b, the advection x, and three new per-hit values: the
disc rotation θ = 2π hash(seq, 8), the noise seed o = 64 hash(seq, 4), the lobe seed hash(seq, 10). The pixel program
keeps the ray–sphere chord of today (column, the lane clip, the near plane); the shading point is the chord's middle
q (view space, normalised by R_s), p = rot(θ) q.xy the disc coordinate, rq = |q|, u = p / |p|, φ = atan2(p.y, p.x).
The pattern is 2-D in the view plane (the impostor is flat; the third noise axis of today bought nothing) and stretches
with R(t) because q is normalised: the ball reads as expanding, with the pattern advected outward on top.

### 2.2 Noise [C]

Three octaves of 2-D value noise (the program's `hash`, lab `valueNoise`; 4 hashes per octave), summed, mean 0.5:

    n = 0.50 vn(p f0 − u a x + o (1.7, 3.1)) + 0.30 vn(p f1 − u a x + o (3.1, 1.7) + 5.3) + 0.20 vn(p f2 − u a x + o (0.7, 2.3) + 11.1)
    f = (1.6, 3.4, 7.0) cells per R_s, a = fireball_advect 1.5 cells over the life

`look::fireball_octave_freq[3]`, `fireball_octave_w[3]`, `fireball_advect`. The three frequencies are not multiples
(no visible lattice); 1.6 cells per radius puts three bumps across the diameter, 7.0 gives the fine crease.

### 2.3 Erosion: a threshold rising with age and radius [C]

    thr = erode_0 + erode_t x + erode_r rq²        erode_0 0.25, erode_t 0.30, erode_r 0.20
    d   = saturate((n − thr) / erode_soft)         erode_soft 0.12
    density = d × saturate(column)^column_pow × lobe   column_pow 0.35

With an fbm spread of about 0.1 about its mean 0.5 [I; the gate measures the real share]: at birth the centre passes
at thr 0.25 (nearly every pixel lit: a full body, the user's standard), the rim at 0.45 (about two thirds: a ragged edge
from frame one). At x 0.5 the centre is at 0.40 (about 15 % holes), the outer body at 0.50 (half), the rim gone: the
body is holed and dissolving from the outside in. At x 1 the centre is at 0.55 (a quarter still dense) but T(t) is 0,
so the last clumps go dark red then black on the ramp: no pop at the end. `column^0.35` keeps the chord's limb
fall-off only as a thin anti-aliased edge, not as a radial gradient (the gradient is what made a disc). This is alpha
erosion, the standard real-time dissolve (the reference's "stepped alpha gradient for punch" [R]); the lab draws the
identical formula.

### 2.4 Silhouette: angular lobes [C]

A 1-D periodic value noise around the disc: n_L(φ) = vn1(lobes φ / 2π + lobe seed) with `fireball_lobes` 5 cells (odd:
never symmetric); the lobed radius in R_s units ρ(φ) = (1 + lobe_amp (2 n_L − 1)) / (1 + lobe_amp), `fireball_lobe_amp`
0.25, so ρ runs 0.6–1.0; lobe = saturate((ρ − rq) / `fireball_lobe_soft` 0.06). Per hit, stable over the life, rotating
with θ; two hashes per pixel. No concentric structure anywhere (the user's standing rejection).

### 2.5 Colour [C]

    m = saturate(column) × saturate((n − thr) / heat_soft)        heat_soft 0.35
    T_pix = T(t) × (rim_t + (1 − rim_t) m)                           rim_t 0.45
    radiance = heat(T_pix) × E_b × density                          E_b = the class's body E (≤ body_max 1.5)

The densest clumps are the hottest: white-yellow (T ≥ 0.75) where n is well above the threshold near the centre,
orange over the body, deep red (T ≤ 0.45 × T(t)) at every edge and hole rim, on the existing `heat_ramp`. The extra
`× T` of today's radiance is dropped (it is why the measured peak was 0.49 against a body cap of 1.5 [M]); the ramp's
own darkening with T carries the cooling. Over the last third (x > 0.67, T(t) < 0.33) the body is sparse red clumps over
the smoke of section 5. The plasma tint mix (`tint_mix (1 − x)` when saturated [S]) stays, applied after `heat()`.

### 2.6 Size and life per class [C; today's values S]

R(t) = R_b (1 − e^(−t / (rise L_b))) × the plasma shrink term, unchanged. R_b doubles everywhere (the user: sizes start
too small; the lamp hid that the body was half the glow): `class_puff` rb_r / rb_u, rb_uk x 2, lives and E unchanged.

| Class | R_b (was) | L_b ms | E_b | Smoke (5) R / s0 |
| --- | --- | --- | --- | --- |
| energy | 2.0 r (1.0) | 350 + 150 k | 1.5 | 1.3 R_b / 0.45 |
| ballistic (dust) | 0.8 r (0.4) | 200 | 0.8, T0 0.6 | 1.0 R_b / 0.30 |
| plasma (globe) | 3.0 r (1.5) | 600 + 400 k, shrink 0.5 | 1.5, tint mix 0.5 | 1.2 R_b / 0.50 |
| heavy | 1.6 r (0.8) | 300 + 200 k, rise 0.1 | 1.5 | 1.3 R_b / 0.50 |
| beam (boiling spot) | 1.2 r (0.6) | 300 | 1.5 | 1.2 R_b / 0.40 |
| missile | U (2 + 2 k) (1 + k) | 500 + 700 k | 1.5 | 1.3 R_b / 0.50 |
| asteroid (dust) | 2.4 r (1.2) | 800 + 400 k | 0.5, T0 0.3 | none (its puff is the dust) |
| ion, repair, tug | none | | | none |

On the dump's energy single hit (r ≈ 80 px at 1080p [I]) the body becomes 160 px of radius, the glow's 10 % radius
about 200 px as today: the absolute footprint of the hit is unchanged; the body fills it instead of the lamp.

### 2.7 Small screen sizes and the LOD [C]

The size LOD becomes "drop the finest octave": a burst whose projected radius is under `fireball_fine_px` 48 (at 1440
rows, scaled with the target's height) draws octaves 0 and 1 only (features 9 and 19 px at a 30 px radius: still
structure); the lobes and the erosion are never dropped; **never one octave**. `puff_lod_px` 96 and the negative-seed
convention go; the octave count travels as before (`screen.w`, 2 or 3). The budget's shed step (1) drops the third
octave of every burst, never the second.

### 2.8 Noise atlas: a later option, not now [C]

Procedural noise is already mirrored by the lab, is deterministic per hit and needs no asset, sampler, texture
creation path or Windows load path. Cost at hull sizes is negligible (a 160 px burst is 0.03 screens). If
`burst_alone_fills_frame` at three octaves exceeds about 1.6 x the decal (section 6), or the explosions of step 3
(the same program at full screen) blow the budget, the fallback is a 256 x 256 RGBA8 tileable tile baked offline
(`tools/effects/bake_noise_tile.py`, four value-noise octaves in R, G, B, A at 4 / 8 / 16 / 32 cells, shipped under
`x3m/`), sampled once per pixel with the per-hit 2 x 2 rotation and the same advection; the lab loads the same PNG. One
fetch replaces 12 hashes; the laws of 2.3–2.5 are unchanged.

## 3. The glow: the body's light [C]

- **One law for every class with a burst** (`hull::glow_r_x` 1.5, `glow_e_x` 0.33): R_g = 1.5 R_b (10 % radius 0.83 R_g
  = 1.24 R_b; against the body's lit radius of about 0.75 R_b at x 0.3 the ratio is 1.65), peak E_g = 0.33 E_b (energy
  0.5). `class_glow` stays only for repair and tug (no burst); the ion class keeps none.
- **Falloff** unchanged: `(1 − d² / R_g²)²`, one lane fetch, the glow primitive as built.
- **Life and heat are the burst's**: drawn while the burst is drawn, with its stagger hash (element 3) and its
  T(t); colour = heat(T(t)) × E_g × T(t). The product is front-loaded (at x 0.5 the glow is orange at a quarter of
  its peak), so the light dies with the body and the ember (unchanged) carries the post-hit.
- **Overlap: per-owner cluster saturation.** For record i of owner O, N_c(i) = the number of O's live burst records j
  (i included) with |P_i − P_j| ≤ `cluster_r_x` 2.0 R_b,i in the owner's frame; s(N) = 1 / (1 + (N − 1) / N0),
  `cluster_n0_glow` 2.5 for the glow's E_g and `cluster_n0_burst` 3.0 for the body's E_b. A lone hit is untouched
  (s(1) = 1); a 20-hit cluster sums its glows to at most 20 / 8.6 = 2.3 E_g = 1.16 and its bodies, if every clump
  overlapped, to 2.7 E_b = 4.1, in practice under 3 because erosion lits about 60 % of a disc at x 0.3. The measured
  20-hit frame (glow 4.05 + body 1.05 = 5.1 at 120 ms [M]) becomes about 1.2 + 1.4 [I]. CPU: a per-owner O(N²)
  distance count over live records, 64 hits about 2 k subtractions, under 3 µs [I], computed once per frame per record.
  A per-pixel cap was the alternative (section 10).

## 4. The flash: a 40 ms star, no halo [C]

- **Shape**: a four-arm cross (two crossed spikes) at angle θ_f = (π / 2) hash(seq, 9), arms of unequal length
  `star_arm_len[4]` = {1.0, 0.55, 0.8, 0.45} x A, A the class's arm size; each arm's profile along s ∈ [0, 1]:
  (1 − s)² across a width `star_width` 0.10 A (1 − s) + a 2 px floor; a disc core of 0.12 r (≥ `floor_flash_px` 3);
  white at `core_e` 4; the arms the class's halo colour source (heat 0.9, the plasma tint, ion colour, missile tint)
  at E 4 falling along the arm. A new sprite kind (depth.y 3) in `weapon_sprite_ps`; the quad is the star's square.
- **Size**: A = `class_flash` arm r: energy 1.2, ballistic 0.6, plasma 2.0, heavy 1.0 (beside the entry streak; the
  streak stays), ion 1.2, beam 1.2 (pulse start), missile 1.5; none for asteroid, repair, tug.
- **Duration**: 40 ms game for every class, under the three-frame floor (3 frames at SETA; `min_frames`); linear
  fade. No white longer than 80 ms (the standing rule).
- **Halo: none.** The body's white core and the glow are the surround. `halo_max` / `halo_fold` stay for the muzzle.
- Occlusion class 2, SOFT 0.15, biased 0.5 r to the eye, as the disc was.

## 5. Smoke: part of the body now [C]

The same impostor program with a smoke flag (tint.a < 0), drawn in the scorch pass (ZERO / INVSRCCOLOR, first in the
draw order, before every additive element), **over the lane's surface only** (surface-bound, class 1: no darkening of
sky; a vacuum puff rides the owner [R, the Homeworld 3 / Expanse rule]). Its age y = (t − 0.5 L_b) / (1.1 L_b) ∈
[0, 1]: radius R_sm = smoke_r × R_b (constant), the noise of 2.2 with the same seed and rotation, the erosion of 2.3
over y (so the smoke dissolves as the body did, one life later), strength s(y) = s0 × min(1, y / 0.27) × (1 − y)
(peak at 0.8 L_b, 0 at 1.6 L_b), output darkening = s(y) × density × `scorch_colour` (0.75, 0.80, 0.85) (warm, the
scorch's own; no new colour). `class_smoke[10][2]` = (R over R_b, s0) per section 2.6; at s0 0.5 the hull under the
densest smoke reads at 50 %. Classes: energy, ballistic (thin), plasma, heavy, beam, missile; none for ion (nothing
post-hit), asteroid (its dust puff is the smoke), repair, tug. Lives: the record's slowest element includes 1.6 L_b.

## 6. Budget and shed order [C; weights to measure]

| Element | Weight | Note |
| --- | --- | --- |
| burst, 3 octaves / 2 octaves | measure (`burst_alone_fills_frame`); [I] 1.1–1.3 / 0.9 | 12 + 2 hashes per pixel against today's 16 `hash3`: about even |
| smoke | measure (`smoke_alone_fills_frame`); [I] = the burst's | charged in the scorch pass at its own weight |
| glow | 0.40 [M, unchanged] | same area as today for energy (3 r) |
| flash star | measure (`flash_alone_fills_frame`); [I] ≤ 0.49 | no `exp` |

Shed order over 8 screens: (0) shimmer rects of other owners; (1) every burst's third octave; (2) other owners'
smoke, oldest first (new); (3) their scorches; (4) their rings and filament glows; (5) their glows and embers, then
their remaining bursts past two per owner; (6) shield patches; (7) secondaries; last, their flashes. Never shed:
sparks, streaks, chips, spot cores, the own ship. Targets unchanged: `hull_budget_20_hits_5120_<class>` ≤ 50 µs;
the smoke adds one impostor at 1.3 R_b over 1.1 L_b per hit, about 1.7 x the body's pixel-life [I], trivial at
fighter range and budget-bounded on a close capital.

## 7. Lab parity

The generated header injects every `hull::*` and `look::*` by name; the formulas are hand-mirrored in `renderHull`:
`puffAt` takes the 2-D rotated disc, `valueNoise2` x 3 with `fireball_octave_freq/w`, the threshold of 2.3, the lobe
noise `noise1Periodic` of 2.4, the colour of 2.5; `flashAt` draws the star (arms, `star_arm_len`, `star_width`, hash
element 9); a `smokeAt` multiplies the float buffer by (1 − s density colour) before the additive layers; the cluster
count s(N) for glows and bursts; `fireball_fine_px` for the octave count. Hashes stay the stage's (`elementHash(seq,
8 / 9 / 10)`). Scenarios that prove the look: `hull/energy-ire-fighter-side` (a fighter at 200 m, the dump's single
case: body, holes, star, smoke), `hull/heavy-gke-capital-300m` (a capital far: the lobed body inside the ring, the glow
not a lamp), new `hull/energy-fighter60-20hits` (the dump's `energy_fighter60_20`: 20 energy hits on a fighter at 60 m
within 120 ms, cleared black: the saturation rule), and the existing `far-2km` for the two-octave small case.

## 8. Fixture gates (dump cases at 1080p, `measure.py`'s method: 10 % of the element's own peak) [C]

| Gate | Case, time | Must hold (today [M]) |
| --- | --- | --- |
| `hull_burst_contrast` | energy_single, heavy_single at 0.3 L_b | within the disc of R(t): share of pixels under 0.1 peak ≥ 0.15; std of the R/4 high-pass over peak ≥ 0.10 (one blob, no holes) |
| `hull_burst_silhouette_lobed` | same at 0.3 L_b | outermost lit radius over 36 angular bins: max / min ≥ 1.25, std / mean ≥ 0.06 (a disc: 1.0 / 0) |
| `hull_burst_erodes_outside_in` | energy_single at 0.1 / 0.5 / 0.9 / 1.0 L_b | lit share inside R(t) ≥ 0.8 / 0.35–0.75 / ≤ 0.15 / 0; mean normalised lit radius at 0.8 L_b ≤ 0.8 x that at 0.2 L_b |
| `hull_burst_colour_core_rim` | energy_single at 0.2 L_b | nearest ramp stop of the mean colour: within 0.3 R ≥ T 0.75; in 0.7–1.0 R ≤ T 0.5 |
| `hull_glow_follows_burst` | energy_single, heavy_single at 0.1 and 0.3 L_b | glow 10 % radius / body 10 % radius in 1.3–2.0 (3.4–7 [M]); glow peak / body peak ≤ 0.4 (2.4–3.6 [M]); glow lit pixels 0 after L_b (the ember separate) |
| `hull_20_hits_no_white_sun` | energy_fighter60_20 at 120 ms | peak (burst + glow + ember) ≤ 3.0 linear (5.1 [M]); the single hit's peak within 1 % with the rule on |
| `hull_flash_star_3_frames` | every class with a flash, flash alone | lit on exactly max(3 frames, 40 ms) frames; principal second-moment ratio of the lit set ≥ 2 (a disc 1); ≤ 0.02 at 0.5 r off the arms (no halo) |
| `hull_smoke_darkens_hull_only` | energy_single over a 0.5 grey hull at 0.8 L_b, 1.6 L_b, sky | min pixel within 0.5 R_b ≤ 0.5 (1 − 0.6 s0); sky unchanged; nothing after 1.6 L_b; ≤ 1 code residual 8 frames after it ends through `TemporalPass` (the scorch gate's method) |
| `hull_burst_small_has_structure` | a 30 px-radius burst (the far case) at 0.4 L_b | hole share ≥ 0.10, max / min ≥ 1.2; `octaves=` row reads 2, never 1 |
| budget rows | `*_alone_fills_frame` for burst (3 and 2 octaves), smoke, star; `hull_budget_20_hits_5120_energy` | weights pinned; ≤ 50 µs |

Sizes, lives and the per-class rows of hull-impact-classes.md section 8 keep their gates with the doubled R_b.

## 9. Cost, native Windows, unknowns

**Hot path.** CPU per hit: the cluster count (per owner O(N²), under 3 µs at 64 hits [I]), one more impostor quad
(smoke) over 1.1 L_b, three more floats on the fireball vertex and four on the star's. GPU per burst pixel: 14 hashes
and two divides against today's 16 `hash3`, about even [I]; the smoke adds its pixel-life; at fighter range a hit is
about 0.03 screens per element. The program stays in the low hundreds of slots [I, about 350]; it must pass the
native D3D9 validator harness (`verification/results/hull-widen-native-validator/`) like every generated ps_3_0.

**Native Windows.** ps_3_0 / vs_3_0, `VPOS`, documented blend states (ZERO / INVSRCCOLOR is the scorch's, in use);
no texture, no new API, no Wine dependency. Cross-compiled and fixture-verified, not natively flown by an agent.

| # | Unknown | Settles it |
| --- | --- | --- |
| 1 | The three-octave program's slot count and full-screen µs (the weights of section 6) | the `*_alone_fills_frame` rows at 5120x1440 |
| 2 | Whether the view-plane pattern reads as pinned to the screen on a rolling owner | `capital-close-12hz` with the owner rolling in the lab; if so, add the owner's roll about the view axis to θ (one angle from its rows) |
| 3 | Smoke on a dark hull (a multiply over near-black shows nothing) | the lineup over a 0.2 hull; the red clumps and the ember carry the post-hit there, by design |
| 4 | Whether the user reads the eroded body as hot gas, and the star as a hit rather than a glint | the flight; kill steps: erode_t 0.25 (fuller, longer), then star arms x 0.6 |
| 5 | The 20-hit bound of 3.0 against the AgX exposure (unknown 7 of weapon-look.md) | the `energy_fighter60_20` dump through the production tonemap |

## 10. Options considered and why they lose

- **Noise atlas now.** An asset, a sampler, a Windows texture path and a lab loader for a cost that is negligible at
  hull sizes; kept as the fallback of 2.8 for the full-screen explosions.
- **Flipbook puff.** Needs motion-vector blending against cross-fade ghosting [R], an asset, and is not exact under
  SETA; the closed-form impostor is. **Turbulence (|2n − 1|)** reads fibrous (wool, lightning) at three octaves; plain
  fbm with a threshold gives clumps and holes; a later tweak if the user asks for fibre.
- **Keep the disc flash with a smaller halo.** Still a disc in a halo; the user named the flash.
- **Keep R_g 3 r at E 1.0 and only double the body.** The lamp stays 2.4x brighter than the body. **A fixed 150 ms
  glow life** outlived short bursts and died under long ones; the coupled law needs no number.
- **Per-pixel cap for overlap.** A scratch target, a second pass and a min() composite for one scenario; the
  cluster rule is a CPU count, deterministic and lab-mirrorable.
- **Smoke as an additive dark-red element or a drifting world-space cloud.** Additive cannot darken; a drifting cloud
  reads as atmosphere, not vacuum [R]. **Keeping the one-octave LOD under 96 px** removed the structure exactly at the
  range the user flies.

## Reproduce

```sh
# the stage dump the figures come from (native fixture, the lock serialises it), then the scratch measurement
python3 verification/probe/wine_lock.py python3 verification/probe/run_weapon_effects.py --host native --dump verification/results/stage-dump
python3 -I /tmp/claude-1000/-home-asvetl-x3-mod/d5061608-f60a-47d6-b9b2-30b0b574851f/scratchpad/measure.py verification/results/stage-dump
```

## 10. Built numbers (ratified 2026-11-07, main session)

The implementation's own gates rejected five of the chosen numbers above; the built values stand and this
section overrides the tables where they differ (ledger: "Burst body, glow, flash, smoke").

| Item | Section above | Built | Why (measured) |
| --- | --- | --- | --- |
| Erosion constants erode_0 / soft / heat_soft / advect | 0.25 / 0.12 / 0.35 / 1.5 | 0.10 / 0.20 / 0.20 / 1.0 | the erosion and colour gates failed on most hits; a numpy model of the law matches the fixture (lit share 0.76 vs 0.768) |
| Radial erosion term | constant over the life | grows with age, x(1 + 3x) | without it the rim did not go before the centre |
| Glow radius | 1.5 R_b | 1.6 R(t), growing with the body | glow/body radius ratio was 1.18 at 0.1 L_b, below the 1.3 bound |
| cluster_n0_burst | 3.0 | 2.0 | the 20-hit peak was 3.29 against the 3.0 bound (2.89 built) |
| Smoke strength | peaks at 0.73 s0 | peaks at s0 | darkest pixel 0.39 against the 0.366 bound (0.346 built) |
| Third octave (review) | on / off at 48 px | weighted in over 48-64 px; the budget's step with hysteresis (enter past the budget, 3 frames, leave under 0.8) | the hard step's own image change 191 against 86 blended |
| Cluster N (review) | a hard count | neighbours by their remaining life fraction; cluster_n0_burst 1.6 | the survivor stepped 62 % at a neighbour's end, 2.2 % now; 20-hit peak 2.73 |
| Ring classes' glow (review) | 1.6 R(t) | out to the ring's drawn radius while it lives | the annulus at 0.21 of the rim at 40 ms (heavy), 0.90 now |
| Ember (review) | a disc | the burst's lobes, same seed | lobe std / mean 0.11 |
| Drift law (2026-11-07) | 2.2: every octave shifted by u a x along p / |p| | octave i at p (f_i − g_i), g_i = (f_i / 2) / sqrt(1 + (|p| f_i / 2a)²), a = advect x: the radius u = |p| f_i cells moves to u − a y / sqrt(1 + y²), y = u / 2a (strictly increasing, slope ½ at the centre, the full shift a far out) | the constant shift collapsed each octave's circle |p| = a / f_i onto one noise point: a dot and a ring at the centre from 0.5 L_b (`hull_burst_no_centre_ring`: worst local ring prominence 0.14–0.26 before, 0.08–0.09 after, bound 0.15) |
| Erosion with the drift law | erode_t 0.30, erode_soft 0.20, column_pow 0.35 | erode_t 0.28, erode_soft 0.28, column_pow 0.25 (erode_0 0.10, erode_r 0.20, erode_rt 3.0 unchanged) | the collapse had kept a hot dot alive late: without it at least 3 of 5 plasma globes were dark at 0.95 L (the median; the gate needs 3 lit) and the death body's lit share at 0.9 L rose to 0.20–0.26 (bound 0.20); built: globes 3 of 5 lit, death 0.186, sizes at 0.3 L ≥ 0.953 (bound 0.95), hull lit share 0.868 / 0.522 / 0.114, 20-hit peak 2.65 (2.73 before) |

Body gates 1–4 take the median over 8 hits (one hit's coverage depends on its seed). Additions: `star_floor_px` 6
(minimum arm length on screen); asteroid smoke uses the ballistic values (1.0 R_b / 0.30); the asteroid dust body,
rock spatter and the repair/tug spot per the coordinator's addendum (asteroid dust 2.14 r at peak 0.18 on black;
repair glow 0.83 r at 0.30, no pixel warmer than blue). Size derivation kept the doubled R_b: the fighter target band
is 6.3–7.2 m (at least 10 px at 1440 rows at the typical distance, at most a third of the hull) and 6.77 m sits inside it.
