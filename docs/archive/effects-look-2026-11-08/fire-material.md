# Fire material: one law for the fireball program family

Design note, 2026-11-07 (branch `effects`, HEAD a74671f26); built in two rounds, as built in section 12; round 3 (2026-11-08) replaced the tongue law with a turbulence law and added the collapse, section 13. Amends the body laws of
[hull-burst-look.md](hull-burst-look.md) 2.2–2.5 (the hull burst), [explosion-look.md](explosion-look.md) 2.1 and 13.1
(the death body, the fire wave) and [hull-impact-classes.md](../../architecture/hull-impact-classes.md) 3 item 5 (the shock ring's trailing
band). It changes the *material* those elements are drawn with; sizes, lives, placement, occlusion, the glow, star, smoke,
ember and secondary elements keep their notes. Context: the user's judgement of the Run245 frames, "viscous blobs / lava
instead of fire", on the hull burst, the shock ring, the death body and the fire wave, against vanilla's fireball (a
blazing white-yellow core, hard radial rays, a thin orange-red fringe, high contrast) and two vanilla frames not on disk (a
mid-life explosion that is mostly a dense spray of 100–300 small yellow sparks about a bright core; a late phase of a dim
core in a spark cloud about 3 radii across). Markers: [M] measured this session (`verification/results/weapon-effects-burst/
fire_material_ref_stats.py`, output `fire_material_ref_stats.txt`, on the sRGB frames the brief names; the ledger's native
rows where cited); [S] in the source at HEAD; [C] chosen here; [I] inferred; [R] a reference (section 11).

Ratified 2026-11-07 (main session): the tongue material replaces the eroded noise ball across the family; the renderer has a bloom chain (bloom_extract/down/agx programs), so the hot core blooms; implementation follows the wave-annulus and radiance/spark changes, as one change through the shared program.

## Outcome

**Replace the isotropic eroded noise ball with a tongue material: polar noise stretched 2.5:1 along the outward direction,
domain-warped and sheared, thresholded hard with a convex alpha gain so the body is thin bright filaments over dark gaps,
coloured by density on a fire ramp (white-yellow spines, orange flanks, a thin red fringe) with a hot core, evolving at a
slow shape rate and a 15 Hz flicker in game time.** One program (`weapon_fireball_ps.hlsl`) and the same law in
`weapon_wave_ps.hlsl` and the ring's trailing band; the Cartesian octaves of today stay as the core-region and small-size
material, so nothing is lost under 48 px or with the eye inside. Cost is bounded by replacing, not adding, octaves beyond
half the radius (+15–25 % per pixel [I], weight 1.18 → ~1.45, to measure) with the tongue term the first thing the budget
sheds. No atlas, no flipbook (section 10). Note: `docs/archive/effects-look-2026-11-08/fire-material.md`.

## 1. Why the current material reads as lava

The frames measured (10 %-of-peak lit set, sRGB captures; the script's columns) [M]:

| Frame | lit px | share > 0.8 peak | dark gaps inside outline | hp anisotropy (rad²/tan², interior, R/4 high-pass) | white-yellow / orange / red share (mean radius) | edge share (2 px) |
| --- | --- | --- | --- | --- | --- | --- |
| vanilla fireball (reference, 172x118) | 2066 | 0.46 (clipped core) | 0.000 | **0.68** | 0.55 @0.41 / 0.42 @0.71 / 0.02 @0.78 | 0.19 |
| our hull burst, es-AB bottom row 0.1 L | 44361 | **0.69** | 0.000 | 1.90 | 0 / 0.29 @0.33 / **0.71** @0.64 | 0.03 |
| same at 0.4 L | 23611 | 0.10 | 0.000 | 1.41 | 0 / 0 / 1.00 | 0.06 |
| capital body 1900 ms (v4 broadside) | 41081 | 0.39 | 0.002 | 2.30 | 0.77 / 0.23 / 0 | 0.08 |
| capital body 2700 ms | 32303 | 0.46 | 0.009 | 1.03 | 0.26 / 0.74 / 0 | 0.06 |
| lab wave (rejected) | 39466 | 0.04 | 0.006 | 0.84 | **0.93 (beige)** / 0.07 / 0 | 0.11 |

Four causes, each in the source [S]:

1. **Filled, uniform alpha.** `density = d × column^0.25 × lobe` with `erode_soft` 0.28: `d` saturates over most of the
   body (69 % of the hull burst's lit pixels above 0.8 of the peak; no gap pixel inside the outline on any frame). Fire is
   optically thin emission: bright where a tongue's spine is, dark between tongues [R]. A filled disc at E 1.5 is a lamp
   with mottle; lava.
2. **Isotropic noise.** Three or four Cartesian value-noise octaves, isotropic, drifting 1 cell over the life. Every
   structure is a blob; the interior gradient energy is *radial-dominated* (1.0–2.3: erosion rims around round holes),
   where vanilla's rays give tangential gradients (0.68). Fire's structures are tongues elongated along the flow, sheared,
   sharp-tipped [R].
3. **One colour.** `m = column × saturate((n − thr) / heat_soft 0.20)` saturates 0.2 above the threshold: nearly the whole
   body sits at T(t): a uniform orange that cools uniformly (the hull burst: 0 white-yellow pixels at 0.1 L, 71 % red
   by the sRGB classes; the wave: 93 % desaturated beige). Fire's colour follows density: white-yellow where the emission
   column is deepest, orange on the flanks, red only at the dying fringe [R].
4. **Slow time.** The pattern changes only by the drift (1 cell per life) and the threshold rising with age: a structure
   lives the whole burst. Fire flickers at 10–20 Hz and its tongues live 50–150 ms [R].

The v4 wave adds a fifth: a *filled* band with holes carved out of it (D48) is a tyre with blots, whatever the hole shape.

## 2. The material law [C]

Shared by every fire element; constants `look::fire_*` in `weapon_effects_core.h`, uploaded with the fireball constants.
Coordinates as today: the chord middle q (normalised by R_s), p = rot(θ) q.xy, rq = |p|, φ = atan2(p.y, p.x), x the age
fraction, t_g the element's game-time age in ms.

### 2.1 Noise: tongues, warp, two rates

    // the Cartesian material (today's octaves 1-2 under the drift law; the core region and the small-size fallback)
    n_c  = (0.5 vn(p (f1 − g1) + o1) + 0.3 vn(p (f2 − g2) + o2)) / 0.8                       f = (1.6, 3.4), today's seeds
    // the warp: one slow Cartesian octave, two components from two seeds (Quilez: fbm(p + fbm(p)) [R])
    w    = fire_warp_amp (2 vn(p fire_warp_f + o3) − 1),  w2 = fire_warp_amp (2 vn(p fire_warp_f + o4) − 1)
    // the polar tongue domain: a = angle cells (periodic), s = outward cells scrolling with the flow
    a    = fire_cells (φ / 2π + 0.5) + fire_shear rq w2             // sheared more the farther out: bent tips
    s    = rq fire_s_cells − t_g / fire_flow_ms + w                 // tongues advance outward as a whole
    n_t  = 0.65 vnp(a, s, fire_cells) + 0.35 vnp(2 a + 0.5, 2 s + 3.7, 2 fire_cells)     // vnp: periodic in a
    // the blend by radius: Cartesian inside, tongues outside; both only in the band
    w_t  = smoothstep(fire_blend_0, fire_blend_1, rq) × w_px       // w_px the size LOD (2.6)
    n    = lerp(n_c, n_t, w_t)
    // the flicker: a per-tongue brightness at ~15 Hz, periodic 1-D noise about the disc
    flick = 1 − fire_flick_amp + fire_flick_amp × vn1p(fire_cells (φ / 2π + 0.5) + t_g / fire_flick_ms, fire_cells)

| Constant | Value | Why |
| --- | --- | --- |
| `fire_cells` | 23 | 23 tongues about the rim (odd: never symmetric); at rq 0.75 the angular cell arc is 0.205 R_s |
| `fire_s_cells` | 1.9 cells per R_s | with the arc above the cell aspect is 2.5:1 radial at rq 0.75, 1.9:1 at the rim, 3.7:1 at rq 0.5 (tongues sharpen inward, where the blend removes them) |
| `fire_warp_f`, `fire_warp_amp` | 2.2 cells per R_s, 0.35 cells | bends tongues by a third of a cell: no straight rays, no sea urchin |
| `fire_shear` | 0.6 cells | the angular offset grows with rq: tips curl; the cheap substitute for curl noise (section 10) |
| `fire_blend_0 / _1` | 0.2 / 0.5 | inside 0.2 R_s the polar domain is singular (the drift-law lesson: no direction at the centre); the core is a clipped hot disc in vanilla, structure there does not matter |
| `fire_flow_ms` | 120 ms per cell | a tongue cell (0.53 R_s) crosses in 120 ms game: structures live 60–150 ms [R]; in R_s per second 4.4 at birth, within the 2–5 R/s of flame tongues [I] |
| `fire_flick_ms`, `fire_flick_amp` | 65 ms per cell (≈ 15 Hz), 0.3 | the per-tongue brightness; 10–20 Hz is the flicker band of small flames [R] |

Hashes per pixel inside the band: today's 2 Cartesian (8) + the warp 2 (8) + the polar 2 (8) + flicker (2) + lobes (2) =
28 against today's 18 (3 octaves + lobes); the fourth octave (D41) stays for bodies past 160 px, on the Cartesian term
only, and is unchanged. The periodic noise `vnp` is `value_noise` with the integer cell wrapped modulo `cells` in a (the
lab's `noise1Periodic` in two dimensions): no seam at φ = ±π by construction.

### 2.2 Threshold with gain: thin filaments, sharp tips

    thr = fire_thr_0 + fire_thr_t x + fire_thr_r rq² (1 + erode_rt x)     fire_thr_0 0.08, fire_thr_t 0.30, fire_thr_r 0.45, erode_rt 3.0 [S]
    d   = saturate((n − thr) / fire_soft)                                  fire_soft 0.10 (today 0.28)
    alpha = d^fire_gain × flick × column^column_pow × lobe                 fire_gain 1.6, column_pow 0.25 [S]

The radial term dominates at the rim (0.45 against today's 0.20): a tongue's density falls along its length to zero, so
every tongue ends in a point (the classic 2-D fire shader: `step(noise, gradient)` with the gradient along the flow [R]).
The hard threshold (0.10) makes edges that lick instead of fade; the convex gain keeps only the spine of each tongue bright
(alpha 1 only where n is 0.1 above the threshold) and feathers the flanks: the mean alpha over the lit outline drops from
about 0.75 to about 0.40 [I; the gate measures it]. The body never fills: dark gaps between tongues are the background (on
a hull, the smoke's darkening of section 5 of hull-burst-look shows through them, which is the look of fire over a hull).
The lobes of hull-burst-look 2.4 stay the macro silhouette (the tongues ride inside it). The erosion over the life keeps
its shape: the rim's tongues go first, the centre's clumps last, the last clumps dark red.

### 2.3 Colour: a fire ramp, density-driven

    m   = saturate((n − thr) / fire_heat_soft)                             fire_heat_soft 0.55 (today 0.20)
    T'  = T(t) × (fire_rim_t + (1 − fire_rim_t) m^0.75)                    fire_rim_t 0.30 (today 0.45)
    radiance = lerp(fire_ramp(T'), tint, share) × E × alpha × (1 + g)

With `fire_heat_soft` 0.55 the colour slides across the whole density range instead of saturating: a tongue's spine (n
well above thr) is at T(t), white-yellow while T ≥ 0.75, its flanks orange, its edge and tip red (T' ≤ 0.3 T). `fire_ramp`
is a second five-stop ramp for the fireball family only (c8–c12 of these programs; the shared `heat_ramp` of every other
element is untouched): T 0 (0, 0, 0); 0.25 (0.45, 0.05, 0.015); 0.5 (1.0, 0.33, 0.05); 0.75 (1.0, 0.80, 0.30); 1.0
(1.0, 0.97, 0.85). Against `heat_ramp` the red stop is darker and purer (a red fringe, not pink), the orange less green
(the tan of the dump's clamped frames came from orange at 0.42 G under 1.0), the yellow stop yellower. The ramp is built so
that at E 1.5 the spines clip to white-yellow and the mid stop never fills a large area (the gain curve leaves it on the
flanks only).

**The hot core.** The death body's core (`core_gain` 2.5 → 0 at 0.35 L, inside 0.3 R falling to 0 at 0.5 R [S]) becomes
the family's: hull bursts of the fire classes get `fire_core_hull` 2.0 at birth → 0 at 0.5 L_b over the same radii;
radiance in the core E (1 + g) = 1.5 × 3 = 4.5 at birth [I], blooming through the glow (section 4) and the game's own
post; no bloom pass is added. The 20-hit sun: the core gain is scaled by s(N)² of the cluster rule (two overlapping cores
share one sun); the gate keeps the body bound 3.0 for the body without its core and adds an area bound for the core
(section 6).

### 2.4 Time, SETA

Rates are in game milliseconds, never frames: the flow (120 ms per cell), the flicker (65 ms per cell), the drift and the
erosion on x = t / L as today. Under SETA the fixture's 100 ms step samples the flicker 1.5 cells apart: it aliases to
random per-tongue brightness, which is what a 15 Hz flicker looks like at 10 fps; nothing is stepped by frame count. The
three-frame floor (`min_frames` 3 [S]) stretches a 350 ms burst to 3 frames at a 2 s step as today; the material's tongues
then advance 16 cells per frame (uncorrelated frames: acceptable, a burst that lives 3 frames cannot flicker). No
new time bound.

### 2.5 Degradation at small sizes

The size LOD `w_px` = smoothstep(`fire_px_0` 48, `fire_px_1` 72, projected radius at 1440 rows) (the third octave's own
blend law [S]): under 48 px the body is the Cartesian material with the new threshold, gain 1.2 (`fire_gain_small`,
lerped with w_px) and the fire ramp; 48–72 px the tongues blend in; a 30 px far hit keeps the clumps and holes of
`hull_burst_small_has_structure` (today 0.679 holes, 3.13 max / min [M, ledger]) and gains the colour law. At 80–160 px
(the dump's hull burst at 1080p) tongues are 16–40 px of arc, 2–4 px of fringe: the range the user flies.

## 3. Per element

| Element | Fire flag | Core | What changes beyond the law | What stays |
| --- | --- | --- | --- | --- |
| Hull burst: energy, heavy, beam, missile | 1 | 2.0 → 0 at 0.5 L_b | the material; the glow 1.6 R(t) → `glow_r_x` 1.3, `glow_e_x` 0.33 → 0.25 (a smooth lamp under thin filaments must sit lower) | R_b, L_b, E_b, the star, the smoke, the cluster rule, the lobes, the drift law |
| Hull burst: plasma globe | 0.5 (tongues at half weight) | 1.0 | the tint mix after the ramp as today | its shrink, its globe |
| Ballistic dust, asteroid dust | 0 | 0 | the fire ramp is not applied (dust keeps `heat_ramp` and today's soft threshold: dust is not fire) | everything |
| Smoke (tint.a < 0) | 0 | 0 | nothing: the Cartesian material, today's threshold (isotropic puffs are right for smoke) | everything |
| Death body, every tier | 1 | as built (2.5 → 0 at 0.35 L) | the material; the fourth Cartesian octave past 160 px unchanged; the second polar octave is weighted up with projected radius (0 at 160 px, 1 at 400 px) so a station's 1000 px body shows 46 tongues, not 23 of 270 px arc | the single body (compose off, D46), R(t), T(t), the halo, embers, fragments, secondaries |
| Eye inside a body | 0 | as built | the Cartesian path as today ("a hot wash is correct inside a fireball", D2); no tongue cost on the 300 µs fence | |
| Fire wave (13.1), capital and station | 1 | none | the band and sheet are *built from* tongues instead of a band with holes: in the ring's frame a = angle cells (48, D48's count), s = (r − d') / w_e (behind the front, scrolling backward with `fire_flow_ms`), the same warp, threshold rising behind the front (thr 0.08 at the front → 0.5 at `wave_sheet_x` 1 R_f behind: tongues trailing from the edge, tips to the rear; the D27 forward lick kept), `fire_gain` 1.6, the fire ramp on the wave's T stops (D47); the white-hot edge e^(−z²) unchanged; `wave_band_gain` 0.1 / `wave_sheet_gain` 0.6 → one `fire_wave_gain` 0.6 on the tongues; the band's mean alpha ≤ 0.35 (a translucent sheet, never a tyre) | r(t), τ, L_w, the slab, the strips, the refraction, the light on swept hulls, the break m |
| Hull shock ring (3 item 5), hull hits | 1 | none | the trailing band 4 b (1 − b) × m gets the tongue factor: one periodic 2-D noise (31 cells about, z / 3 outward), threshold 0.35, gain 1.6, the fire ramp at `ring_trail_t`; the leading edge unchanged | r(t), w, L, E, the shimmer |

The vanilla spark spray (mid-life) and the 3-radii spark cloud (late) are the ember element's count and spread, not this
material; this note only asks that the body's L stays under the embers' life (it does: embers outlast the body by 0.2–0.5
L [S]) so the late phase is sparks about a dim core, as in vanilla. A follow-up note on ember counts (100–300 against today's
8 per secondary) is the next step after this material flies.

## 4. The glow and the smoke under thin filaments

The glow is the only smooth element left in the body's footprint; it is what blooms the core. With the body's mean alpha
near 0.4 the ratio glow / body on the mid-body rises, so the glow comes down (table above) and the gate
`hull_glow_follows_burst` keeps its radius band 1.3–2.0 (the body's 10 % radius falls with the gain: the ratio measured
is expected near its lower bound [I]) and the peak ratio ≤ 0.4. The smoke (multiply, over the hull only) is unchanged
and now shows through the gaps: on a hull the gaps are dark grey-brown, on sky black. The death halo (`halo_big_r` 1.15
R(t), ≤ 0.2 E_b) is unchanged.

## 5. The lab

`weapon_effects_lab.html`: `fireNoise(p, rq, phi, seed, tg, px)` mirroring 2.1–2.3 (`valueNoise2` for the Cartesian
and warp taps, a new `valueNoise2Periodic(a, s, cells)` for the polar taps, `noise1Periodic` for the flicker), used by
`puffAt`, the death body's loop, `waveAt` and `shockFront`'s band; the constants injected by name from the HEADER as today;
a `material` toggle (today / fire) per scenario and a *pair* view drawing the same scenario at t and t + 16 ms (and + 100
ms for SETA) side by side. Scenarios to judge beside the vanilla row: `hull/energy-ire-fighter-side` at 0.1 / 0.3 / 0.6
L_b, `hull/heavy-gke-capital-300m`, `hull/far-2km` (the 30 px case), `boom/fighter-m5-600m-side`,
`boom/capital-m2-1500m-wave-broadside` at P + 40 / 400 / 1200 ms, `boom/capital-m7-far-5km`,
`boom/station-factory-3km-preroll`, `boom/seta6`. Judged in headless Chromium before the native dump, then the dump's
frames (the standing rule: gates missed the lamp twice; frames decide). The dump should also write the frames through the
production tonemap (unknown 4 of section 9): the "beige" of the v3/v4 waves was partly the dump's clamped sRGB.

## 6. Gates (`measure.py`'s 10 %-of-peak method on the 1080p dump parts; medians over 8 seeds) [C]

| Gate | Case, time | Must hold (today [M], this session's script on sRGB frames unless noted) |
| --- | --- | --- |
| `fire_luminance_thin` | hull energy_single at 0.3 L_b; death fighter, capital at 0.3 L | share of lit pixels above 0.8 of the peak in 0.10–0.35 (hull 0.69, capital 0.39–0.46); inside the filled outline the share under 0.25 of the peak ≥ 0.35 (gap share today 0.000–0.009) |
| `fire_tongues_radial` | same | high-pass (box R/4) gradient energy radial / tangential over the interior (outline eroded 4 px) ≤ 0.75 (vanilla 0.68; hull 1.41–1.90, capital 1.03–2.30; the isotropic wave 0.84) |
| `fire_edge_licks` | same | the lit set's 4-boundary length over the filled outline's boundary length ≥ 2.0 (a disc 1.0; adds to `measure.py`) |
| `fire_temporal` | hull energy_single, frames at t and t + 16 ms, and t + 100 ms (the SETA step) | high-pass correlation at 16 ms in 0.5–0.9 (structures live 50–150 ms: e^(−16/80) ≈ 0.8), at 100 ms ≤ 0.6; the low-pass (box R/2) correlation at 16 ms ≥ 0.9 (the shape does not pop) |
| `fire_colour_ramp` | hull energy_single at 0.2 L_b; death at 0.2 L | nearest fire-ramp stop per lit pixel (linear PFM): T ≥ 0.75 share ≥ 0.25 with mean normalised radius ≤ 0.5; T ≤ 0.3 share in 0.05–0.25 with mean normalised radius ≥ 0.65; the mid stop (0.5) share ≤ 0.5 (vanilla sRGB classes: 0.55 @0.41 / 0.42 @0.71 / 0.02 @0.78; hull today 0 / 0.29 / 0.71) |
| `fire_core_bloom` | hull energy_single at 40 ms; death at 0.1 L | peak ≥ 3.0 linear inside 0.3 R, ≤ 1.5 outside 0.5 R; `hull_20_hits_no_white_sun` keeps ≤ 3.0 with cores off and adds: with cores on, pixels above 3.0 ≤ 1 % of the frame |
| `fire_small_reads` | the 30 px far case at 0.4 L_b | `hull_burst_small_has_structure` unchanged (holes ≥ 0.10, max / min ≥ 1.2); lit share inside R(t) ≥ 0.4 (the gain must not empty a far hit) |
| `wave_not_a_tyre` | capital 5 km face-on at 0.5 τ, the band | mean luma of the band over the edge's peak ≤ 0.35; share under 0.25 of the edge's peak inside the band ≥ 0.40; polar high-pass anisotropy ≤ 0.75; desaturated mid-tones (min / max ≥ 0.6 at luma < 0.5 peak) ≤ 0.10 of the band's lit pixels (the rejected lab wave 0.93) |
| `fire_no_seam` | hull, death, wave at φ = ±π | the band's mean 2 px either side of the angle's wrap within 0.1 of the peak (the strip-seam gate's method) |
| budget rows | `burst_alone_fills_frame` (tongues on / off), `body_alone`, `wave_alone`; `death_budget_capital_close_5120`; `death_budget_5120` eye inside | weights repinned; ≤ 450 µs every frame with the shed order of section 7; eye inside ≤ 300 |

The existing body gates (contrast, lobed silhouette, erodes outside in, no centre ring, glow follows burst, the wave's
radius law, thickness, broken circumference, never brightest) keep their bounds; `hull_burst_colour_core_rim` is
superseded by `fire_colour_ramp`.

## 7. Cost, LOD, shed order

**Today [M, ledger and `summary.json`]:** `fireball_ps` 431 slots, `wave_ps` 614; weights `budget_weight_fireball` 1.18
(`_coarse` 1.03, `_fine4` 1.18), `budget_weight_wave` 1.40 / `_one` 1.24; the close capital's worst frame 351.7–412.5 µs
against the 450 fence (shed 9–10), the eye-inside fighter 279.5 against 300; hull `heavy_capital_64` 1132 µs at
5120x1440 (two octaves on that frame).

**This design [I, to measure]:** +10 hashes per pixel in the band (28 against 18), one `pow`, two `smoothstep`s: about
+90 slots (`fireball_ps` ≈ 520, `wave_ps` ≈ 680, both under the validator's comfort and far under the 32768 plan). Per
pixel the tongue term is taken only where w_t > 0 (`[branch]` on rq > `fire_blend_0`) and the third Cartesian octave is
*replaced* by it there (it is weighted out as w_t rises: the fine Cartesian grain was never visible under the tongues):
the per-pixel cost is +15–25 %, the fireball weight 1.18 → about 1.45, the wave 1.40 → about 1.6. The close capital's
worst frame would rise from 412 to about 480 µs unshed, so the shed order gets a new first step for bodies: (0c) the
tongue term of other owners' bursts and bodies, farthest first, with the octave step's hysteresis (3 frames, 0.8); the
own owner's and the newest death's keep it until the fence's last step. The eye-inside path is unchanged (0 cost). CPU:
one more vertex float (`fire`, packed with `core` as a float2 in TEXCOORD5: stride 80 → 84 [I]; as built `core` a
float4, 80 → 92, section 12.1) and the fire ramp
upload with the fireball constants; no per-frame work.

**Native Windows.** ps_3_0 / vs_3_0 arithmetic only (`atan2`, `pow`, `smoothstep`, the wrapped cell: `floor`, `frac`,
a compare), no texture, no new API, no Wine dependency; the generated program passes the native D3D9 validator harness
(`verification/results/hull-widen-native-validator/`) like every ps_3_0 here. Cross-compiled and fixture-verified, not
flown natively by an agent.

## 8. Kill criteria and the flight questions

Each flips a header constant, never adds a setting:

| Observation in flight | Step |
| --- | --- |
| straight rays, a starburst sprite, a sea urchin | `fire_warp_amp` 0.35 → 0.6, then `fire_shear` 0.6 → 1.0 |
| still lava (filled, uniform) | `fire_gain` 1.6 → 2.2 and `fire_heat_soft` 0.55 → 0.8 |
| strobing or noise under SETA | `fire_flick_amp` 0.3 → 0.15, `fire_flick_ms` 65 → 100 |
| far hits vanish | `fire_gain_small` 1.2 → 1.0, `fire_px_0` 48 → 72 |
| the wave still a tyre | `fire_wave_gain` 0.6 → 0.4, the wave's threshold at the front 0.08 → 0.2 |
| too thin, "wispy", no body | `fire_gain` 1.6 → 1.3, `fire_thr_0` 0.08 → 0.0 |
| the core a lamp again | `fire_core_hull` 2.0 → 1.0, the glow `glow_e_x` 0.25 → 0.2 |
| the material fails on two of these at once | `fire_enabled` false: today's law, the fire ramp kept |

Flight questions: (1) does the hull burst read as fire at 200 m and at 2 km? (2) do the capital's death body and its
wave read as fire, the wave translucent, from broadside and from above? (3) is the flicker rate right under SETA 6–10x
and at 1x? (4) does the white-yellow core bloom without turning the 20-hit fighter into a sun? (5) SETA sparkle on fire
bodies: at 100-160 ms steps do the flicker and the fine grain (new noise every frame, section 12.3) sparkle? Since
2026-11-08 (13.5) the fine octaves re-seed at most every two frame steps, as the rays: does the grain still sparkle, or
does it now step visibly at SETA 6-10?

## 9. Unknowns and what settles each

| # | Unknown | Settles it |
| --- | --- | --- |
| 1 | The slot counts and the weights (section 7's [I]) | the `*_alone_fills_frame` rows at 5120x1440 and the validator |
| 2 | Whether 23 tongues on a rolling owner pin to the screen (hull-burst-look unknown 2, now visible: tongues are directional) | `capital-close-12hz` in the lab with the owner rolling; if so, add the owner's roll about the view axis to θ |
| 3 | The core at 4.5 linear through the AgX exposure (weapon-look unknown 7) | the `energy_fighter60_20` dump through the production tonemap |
| 4 | Whether the dump's clamped sRGB misjudges the ramp (the v3/v4 "beige" read) | the dump written through the production tonemap beside the linear PFM |
| 5 | Whether the swirl shear reads as curl or as a twist; whether the real curl (4 taps) is worth it | the lab's `material` toggle with a `curl` variant in JS only, judged side by side before any program change |
| 6 | The 100 ms SETA step against the 120 ms flow: does the tongue advance read as motion or as a new pattern | the pair view at + 100 ms in the lab; the flight at SETA 6x |

## 10. Options considered and why they lose

- **A generated noise atlas (hull-burst-look 2.8, re-examined).** A 256² tile of four Cartesian octaves replaces 16 hashes
  by one fetch but cannot carry the polar stretch (a polar-baked tile would need its own seam handling and a fixed cell
  count per radius), the per-hit warp, or the two time rates (a 2-D tile scrolls; the flicker and flow would need a
  second fetch). The fire look is a change of law, not of tap count; the tap count is bounded by section 7. Stays the
  fallback if the budget rows fail: the Cartesian and warp taps from a tile, the polar taps procedural.
- **A flipbook (re-examined for the fire look).** The best per-pixel fire a sprite can show, and the reason vanilla's
  fireball looks the way it does; but: an asset (foreign bytes, or baked from our own law and then one frozen seed
  repeated across 20 hits), motion-vector blending against cross-fade ghosting [R], a page rate that cannot follow SETA
  (a 64-page book at 16 ms skips six pages per 100 ms step: strobing), no exact lab mirror, and no per-element
  parameters (core, lobes, tier) without a book per variant. The procedural law gives the same ingredients (tongues,
  gain, ramp, flicker) with none of those.
- **Real curl noise (Bridson 2007: v = ∇ × ψ, divergence-free [R, fetched, not parsed]).** Four extra taps (finite
  differences of two potentials) or an analytic-gradient noise for a swirl the shear term gives at zero taps; revisit only
  if unknown 5 says the shear reads as a twist.
- **Ray-marched volumetric fire (Perlin's density sphere, GPU Pro 6 [R]).** 8–16 fbm samples per pixel: an order of
  magnitude over the fence on a close capital; the chord column already gives the volume cue.
- **Keep the filled body and add bright streaks on top (a second additive element).** Two draws, two lane fetches, and
  the lamp stays under the streaks; the measured problem is the filled body, not missing highlights.
- **Carve more or sharper holes out of the filled band (the v4 wave's path).** A tyre with blots whatever the hole shape
  (measured gap share 0.006); the band must be built from tongues, not subtracted from.
- **Change only the ramp.** The hue gates would pass on a still-uniform body (69 % above 0.8 of the peak); colour without
  the alpha law is a yellow lava.

## 11. References

Fetched this session: I. Quilez, *Domain warping* (iquilezles.org/articles/warp: f(p + h(p)), Perlin's 1984 marble);
*The Book of Shaders* ch. 13 (fbm, lacunarity, gain, domain warping); Wikipedia *Flame* and *Fire* (the yellow of a
diffusion flame is soot incandescence near 2000 K; "the hottest parts near the ground appear yellowish-white, while the
cooler upper parts appear red"; a microgravity flame is spherical and bluer); Febucci, *Fire shader* (the 2-D fire
shader: scrolling noise stepped against a gradient along the flow, three stepped colour bands); Bridson, Hourihan, Nordenstam,
*Curl-noise for procedural fluid flow* (SIGGRAPH 2007; PDF fetched, not parsed: cited for the divergence-free potential
construction from memory). Project references used: `docs/archive/effects-look-2026-11-08/explosion-references.md` 3 and 5 (impostor fireball,
flipbook vs procedural, the lamp / balloon / disc failure modes), `impact-effect-references.md` 3 (stepped alpha for
punch, motion-vector blending). From memory, not fetched (blocked hosts): the 10–20 Hz flicker band of small flames and
the puffing law f ≈ 1.5 / √D Hz of pool fires (Cetegen & Ahmed 1993); fire shading as emission from a temperature field
on a blackbody ramp with absorption only in the smoke (Nguyen, Fedkiw, Jensen 2002; Houdini pyro practice); tongue
lifetimes of 50–150 ms and advection of 2–5 R/s [I from flame footage, not a sourced figure].

## 12. As built

Built 2026-11-07 in two rounds through the shared fireball, wave and ring programs; the measured rows are in the ledger
([weapon-effects.md](../../verification/weapon-effects.md) "Fire material" and its "Round 2"). This section records every
constant and gate that differs from sections 2-7 and why; sections 1-11 are the design as ratified.

### 12.1 Law changes

| Item | Note | As built | Reason |
| --- | --- | --- | --- |
| The law's radius | the chord middle's \|q\| | the view-plane disc's \|p\| | on a hull hit the lane cuts the chord: the tongues met in a point at the screen centre (round 1) |
| `fire_warp_amp`, `fire_shear` | 0.35, 0.6 | 0.6, 1.0 | section 8's step for straight spikes (a sea urchin late on the capital, lab round 1) |
| `fire_blend_0 / _1` | 0.2 / 0.5 | 0.12 / 0.40 | round 1 |
| `fire_thr_0 / _t / _r` | 0.08 / 0.30 / 0.45 | 0.12 / 0.39 / 0.48 | round 1 0.12 / 0.43 / 0.57 (the first native gates); round 2 the replica's search under the new ramp: the tongues reach R(t) |
| `fire_gain`, `fire_gain_small` | 1.6, 1.2 | 1.2, 1.2 | round 2: with the fine octave's luminance the thin share holds at 1.2; the small size and a shed burst keep the convex gain (item 3: overlapping clumps summed to a tan body) |
| `fire_heat_soft`, `fire_rim_t` | 0.55, 0.30 | 0.60, 0.21 | round 1 0.48 / 0.18; round 2 the search |
| `fire_rim_cool` over \|p\| `fire_rim_r0 .. r1` | none | 0.76 over 0.45 .. 0.95 | the tips red: with the note's law no red pixel was lit (round 1 0.63) |
| The fire core | 0.3 .. 0.5 R_s | `fire_core_r0 .. r1` 0.2 .. 0.35 R_s | the note's radii put 2.2 linear past 0.5 R(t) on the hull, 3.9 on a death body |
| The core's colour | white | the ramp's top stop (yellow-white) | round 2: a neutral white core blended through peach |
| `fire_ramp` | (0.45, 0.05, 0.015), (1.0, 0.33, 0.05), (1.0, 0.80, 0.30), (1.0, 0.97, 0.85) | (0.47, 0.15, 0), (0.62, 0.34, 0), (1.0, 0.78, 0.06), (1.0, 0.92, 0.30) | round 2: AgX (decode 2.2) turns an orange or red brighter than about 1.1 x its stop salmon (B > 0.5 G, G < 0.85 R); green rises before blue, blue near zero below the yellow stop, the red and orange stops dim (`fire_pink.py`: 0.248 of the (T, core, intensity) samples pink with the old ramp, 0.031 with this one, all at intensities the body does not reach outside its core) |
| The fine octave | the third Cartesian octave weighted out under the tongues | `fire_fine_k` 4: under the tongues the polar noise at 4 x their cells and s (streams outward with them), elsewhere the Cartesian third octave (taken on a fire body of any size); luminance x (1 - `fire_fine_depth` 0.35 saturate(`fire_fine_bias` 1.2 - 2 n_f)), threshold + `fire_fine_jitter` 0.10 (n_f - 1/2) | round 2 item 2: each tongue was a flat fill with a soft edge (petals); the tap replaces the third octave's, no new hash where the tongues are |
| The size LOD | `fire_px_0 / _1` 48 / 72 | 24 / 40 (R px at 1440 rows) | round 2 item 6: the nested fighters at 900 ms (R about 45 px) kept the erosion law (smooth blobs) |
| The plasma globe | half the law | the tongues and gain at half weight, the previous threshold (blend `w_px saturate(2 fire - 1)`) | round 2: the globe at half the fire threshold had no lit pixel at 0.95 L (its late-life floor) |
| Cluster thinning | none | a fire class's burst: threshold + `hull::fire_cluster_thr` 0.2 x (1 - s(N)), carried as fire weight 1 + (1 - s(N)) | round 2 item 3: N overlapping bursts' tongues summed to a filled body |
| Cluster glow and ember | the glow 1 / (1 + (N - 1) / 2.5), the ember none | past `hull::cluster_glow_fire_n` 4 bursts x 4 / N more, the ember of a fire class the same factor | round 2 item 3: the 20 hits' embers (lab: the smooth puffball with the bursts off) and glows summed to tan |
| Shed tongues | LOD 0 (the previous law) | core.z = -LOD: the law kept at its size, no polar taps and no Cartesian fine tap (n_f 1/2: no jitter; also under the budget's step (1), screen.w 0), latched for the rest of the burst's or body's life (not the newest death's body in boom's last-resort loop: shed per frame there; `HullSlot`/`Spot::fire_shed`, `BoomSlot::fire_shed` bit 0 the body, k + 1 secondary k) | round 2: a shed burst kept the convex gain (item 3); review 2026-11-08: the coarse charge assumed the third octave gone, and a burst's tongues came back when the depth order or count changed (pop) |
| The vertex | `fire` packed with `core` as a float2, stride 80 → 84 | `core` a float4 (the core's gain, the fire weight, the size LOD, the game-time age in ms): stride 80 → 92 | the size LOD and the age are per box (the flow and flicker run on the element's game time) |
| The tongue blend's fire weight | `fire` | saturate(fire) for w_t (the blend, the size-LOD law and the flicker); the cluster's excess 1 - s(N) to the threshold only | review 2026-11-08: past 1 the lerps overshot (noise contrast nearly doubled, a tongue dimmed to 0.42, w3 negative); the 20-hit cluster in the lab before / after at 200 and 450 ms: tongues at 200 ms slightly sparser, the 450 ms ember unchanged (no retune) |
| The wave | band and sheet from tongues behind the front, the rim e^(-z^2) x m x (0.6 + 0.4 d) | `fire_wave_thr_0 / _end` 0.3 / 0.6, the radial cell `fire_wave_aspect` 2.5 x the arc (round 1); round 2: the noise continues ahead of the front and the threshold rises to 1 over `fire_wave_lick` 4 w_e ahead (`wave_window_out` 2.5 -> 4), so tongues point outward; the rim = `wave_rim_gain` 1 x the tongues' own front density x saturate((n - thr - `wave_rim_lift` 0.25) / fire_soft)^2 (never brighter than a tongue, broken between the spines); the second octave at `fire_fine_k` x the cells is the fine octave (jitter, luminance) | item 5: the round-1 lab wave was a drawn hoop (a thin continuous bright line with tongues trailing inward) |
| The ring's band | a periodic 2-D noise | a value noise of the owner-frame direction (31 cells about, no seam) offset by -z / 3 | the ring program has no age: **open item**, the band does not flow with game time |
| The budget | step (0c) by the frame's budget; `budget_weight_fire` about 1.45 | step (0c), and the tongues' own fence `hull::fire_fence` 0.08 screens of their charge: with more than 4 other owners' bursts drawing tongues and past the fence, they drop the term farthest first until under 0.8 of it or the nearest 4 are left, held 3 frames (the octave step's hysteresis); the own ship's keep it, a single near burst is never fenced, the deaths keep theirs (boom's own step 0c); `budget_weight_fire` 2.07 (round 1) -> 2.28 (round 2: on 439.0 us, off 244.3 us at 5120x1440, the fine octave's polar tap) | the 20 hits at 200 m: 88.2 us against 80 with every tongue drawn; the first fence (no cluster condition) shed a single near burst's tongues in the second record |
| `budget_weight_fireball`, `_coarse`, `_fine4`; `budget_weight_fire` | 1.18, 1.03 (note's section 7: about 1.45 with the tongues) | 1.35, 1.18, 1.35; 2.46 (2026-11-08, orchestrator decision: pinned to the every-pixel rows 242.0 / 212.9 us, 1.36 / 1.20 a screen, and the tongues' 2.28 + 0.18 so their extra stays 1.11); the charged fence they feed raised x 1.35 / 1.18 = 1.144: `boom::budget_screens` 2.5 -> 2.86 (the close capital's 450 us charged fence -> 515 at the old scale). Not raised: `hull::fire_fence` 0.08 (in screens of the tongues' extra, 1.10 -> 1.11), the shared `screen_budget` 8 (every family's, decal-weighted patches included). The measured bounds stay (close capital worst frame <= 450 us real) | re-pinned at the old fences the close capital's wave was shed on one frame of seven (`wave_drawn_close_capital`) |
| The wave's colour | `wave_t_band0 / _band_end` 0.55 / 0.4 | 0.75 / 0.55; `fire_wave_thr_0` 0.3 -> 0.25; `wave_ahead` 0.7 -> 1.0 | round 2: the young wave's band classed red on the round-2 ramp; the spine-only rim left 4 bins dark in a run and fell 0.35 of its peak in a pixel |

### 12.2 Gates as built

| Gate | Note's bound | As built | Reason |
| --- | --- | --- | --- |
| `fire_luminance_thin_*` gap clause | the hole-filled outline's share under 0.25 peak >= 0.35 | the equal-area disc of the lit set (radius sqrt(lit / pi) about the centre): share under 0.1 of the peak >= 0.20 | the outline's gaps are open bays the hole fill drops; vanilla's frame measures 0.096 on this measure (`verification/results/explosion-look/vanilla_disc_dark.py`): **the bound 0.20 is the orchestrator's and vanilla does not pass it** |
| `fire_edge_licks_*` | the outline's boundary | the 4-boundary of the disc of the outline's area | accepted (orchestrator 2026-11-07): against the hole-filled outline an open tongue silhouette is 1.0 by construction |
| `fire_not_pink_hull`, `_death` (new) | none | lit pixels at most 0.8 of the peak whose AgX display has B > 0.5 G and G < 0.85 R: the medians' largest over 0.2 / 0.3 / 0.5 L < 0.02 | item 1 |
| `fire_quad_edge_dark_hull`, `_death` (new) | none | the impostor quad's boundary rows and columns: the worst hit's largest luma < 0.02 of the frame's peak (40 ms and 0.3 L_b; 0.1 and 0.3 L) | item 4. The quad is the sphere's exact screen rectangle and the law lives inside the sphere, so it cannot clip; the straight cuts on `boom_capital_far_5km` frames 1-3 are the fixture's dying-hull box in the lane (the body part is drawn with the hull's depth at alpha 0.91 / 0.83 / 0.55; none at 2700 ms, alpha 0): occlusion class 2 against a box |
| `wave_not_a_tyre` | band mean, dark share, anisotropy, grey | + the longest continuous rim arc: at 720 angles the largest luma within +-1.5 w_e of the 64-bin's edge radius; the longest circular run >= 0.5 of the peak <= 60 degrees | item 5 |
| `hull_burst_erodes_outside_in` | lit share inside R(t) >= 0.8 / 0.35-0.75 / <= 0.15 at 0.1 / 0.5 / 0.9 L_b | the lit set's equal-area radius at 0.5 L_b 0.35-0.9 of its radius at 0.1 L_b, at 0.9 L_b <= 0.4 of it; none after L_b; the mean radius 0.8 / 0.2 <= 0.8 unchanged | the tongues' gaps are the material; the intent (it shrinks from outside in and is gone at L) measured on the body drawn |
| `hull_burst_no_centre_ring` | the profile over 0 .. 0.6 R_s | over 0 .. R_eq (the lit set's equal-area disc); the bounds 0.15 / 1.3 unchanged | the tongues' bases sit at the blend radius of the drawn sphere, outside the body the eye sees as its centre; one seed's base read 0.157 |
| `death_big_tier_sizes`, `death_tier_sizes` | the mean outline within 5 % of R(t) | the tongues' front, the 90th percentile of the 36 bins' outermost lit radius, within 0.80-1.25 of R(t) (the lobes' reach; the mean printed; the old gate was the mean outline within ±5 %) | the mean outline counts the bays between the tongues' tips (0.66-0.74 measured on round 1); `death_tier_sizes` (fighter 600 m, missile 300 m) is included because `fire_px` 24 / 40 puts the 600 m fighter partly under the tongue law |
| `wave_broken_circumference` | every bin's peak >= 5 % of the ring's | no run of more than 3 bins (16.9 degrees) under 5 % (no transparent arc); max / min >= 2 or a band bin at 0 (which one printed, `arc_by`), CV unchanged | the rim broken into tongues leaves single bins dark between them |
| `fire_small_reads` | lit share inside R(t) >= 0.4 | the lit set's equal-area radius >= 0.45 R(t) (holes and lobes unchanged) | `fire_px` 24 / 40 puts the 30 px (1080) hit at 40 px of 1440 rows: the whole tongue law, whose gaps are the material |
| `hull_burst_colour_white_core_at_birth` | b / r >= 0.45 | >= 0.25 | the core whitens towards the top stop (1, 0.92, 0.30), yellow-white (item 1) |
| `hull_burst_colour_dark_red_at_end` | g / r <= 0.3 | <= 0.35 | the red stop is G / R 0.32 (redder turns salmon under AgX) |
| `death_body_erodes_outside_in` | lit share inside R(t) >= 0.8 / 0.35-0.75 / <= 0.20 | the equal-area radius at 0.5 L 0.35-0.9 of its 0.1 L value, at 0.9 L <= 0.45 of it, none at L | `fire_px` 24 / 40 gives the 600 m fighter part of the tongue law; the hull gate's measure |
| `death_body_colour_core_rim` | core's nearest stop >= 0.75, rim <= 0.5 | core >= 0.5 and hotter than the rim, rim <= 0.5 | the mean colour of a tongue body mixes yellow spines with orange flanks (0.5 measured); `fire_colour_ramp_death` carries the ramp per pixel |
| `death_body_no_centre_ring` | over 0 .. 0.6 R_s | over 0 .. R_eq | the hull gate's measure (one seed 0.20 over R_s) |
| `death_cools_through_ramp` | T at 0.6 L >= 0.5 | >= 0.25 and not above T at 0.2 L | 0.6 L is 1,200 ms of the fighter, past the accepted 900 ms frame's red tongues; the round-2 red stop classes it red |
| `wave_fire_not_grey`'s on-ramp share | the heat ramp | `look::fire_ramp`, the wave's own ramp since v8 | the stale reference drifted off once the fire ramp's blue went to zero (0.73-0.85 against 0.9) |
| `wave_no_hard_edge` | no rise outward past the peak on any ray | rises on at most a tenth of the rays; no hard drop, the fall <= 0.3 unchanged | the tongues lick outward past the front: a ray leaves one tongue and meets another's tip (2 of 30 rays) |
| `death_big_stage_cpu`, `death_stage_cpu_32_deaths` | 0.08 us a quad, 50 us | unchanged | host CPU timing (X4.exe loaded the Windows host during the records): host-noise rows unless they fail three times on a quiet host |

The round-2 record: 352 of 352 checks [M] (`run_weapon_effects.py --host native`; the ledger's "Round 2" table from
`ledger_rows.py --fire`); `fireball_ps` 842 slots, `wave_ps` 790. Open items: the fireball's every-pixel weights (above),
the eye-inside fighter's 4-octave row at 298.9-304.0 us against its 300 us test bound, the ring's band without age, and
the hull ember's smooth lobed glow (heat ramp, outside this material) which outlives the cluster's bursts and reads tan at
400 ms in the lab's 60 m cluster.

### 12.3 Review fixes (2026-11-08)

The tongue blend reads saturate(fire) (a cluster burst's 1 + (1 - s(N)) had pushed w_t past 1: the noise lerps
overshot, the flicker dimmed a tongue to 0.42, w3 went negative); the cluster's excess thins the threshold only. The
lab's 20-hit cluster before / after (`hull/energy-fighter60-20hits`, 200 and 450 ms): at 200 ms the tongues slightly
sparser, no filled body; at 450 ms the hull ember's tan puffball unchanged (pixel-identical at 800 ms); no retune. A
shed box and the budget's step (1) no longer take the Cartesian fine tap (n_f 1/2), and a shed burst or body stays
shed for its life (12.1). The eye-inside GPU bounds `eye_inside_fighter`, `_n4` 300 -> 330 us: the program's cost on
that path rose with the fire material (n4 298.2 us on round 1's record, 301.4 on round 2's; the fixture's 300 check
read 300.8 on this round's second record). The fireball weights pinned to the measured rows with the boom family's cap
raised by the same ratio (12.1). Ledger: [weapon-effects.md](../../verification/weapon-effects.md) "Round 2" / "Review fixes".

SETA: at 100-160 ms steps the flicker (65 ms cells) and the fine grain (streaming with the 120 ms flow) are new noise
every frame; nothing freezes or steps by frame count, but a fire body may sparkle frame to frame. A flight question
(section 8, question 5), not a fixture gate.

## 13. Round 3: turbulence (2026-11-08)

The user on the round-2 lab: "fire is still bad. it looks like a flower" (a capital death mid-life: large smooth orange
petals about a yellow core). Then two additions agreed by the orchestrator: the body stays a connected hot blob whose
outline deforms, with tongues on its rim (not a loose field of filaments), and the body collapses late in its life.

**Why the tongue law failed.** Two octaves of 23 / 46 polar cells stretched 2.5 : 1, thresholded hard: a few large
smooth cells per body, each a lit lobe with a soft rim; thresholding low-frequency noise always yields petals, whatever
the gain. Fire and the vanilla sprite are turbulent: many small cells at every scale, bright thin filaments between
dark gaps at the rim, a dense hot interior, the large scale only a gentle modulation.

### 13.1 The law (`weapon_fireball_ps.hlsl`; `weapon_effects_core.h` look::fire_*; the lab's `fireTurb`)

| Element | As built | Constants |
| --- | --- | --- |
| Octaves | five value-noise octaves at f_i = 3 x 2^i cells per R_s (cells 0.33, 0.17, 0.083, 0.042, 0.021 R_s) | `fire_turb_f` 3 |
| LOD | octave i weighted saturate(cell px / `fire_cell_px` - 1) (0 at 2 px, 1 at 4 px; the projected R_s per pixel); the 4th and 5th also x saturate(screen.w), saturate(screen.w - 1) (the budget's octave steps (1) and (0b)); octaves 3-5 off on a shed box (core.z < 0, latched as before) | `fire_cell_px` 2 |
| Ridged sum | T = sum w_i a^i abs(2 n_i - 1) / sum w_i a^i (normalised over the octaves drawn) | `fire_turb_a` 0.55 |
| Octaves 2-3 (the rim's tongues) | polar, periodic about the disc (27 / 53 cells: about 0.17 / 0.083 R_s at 0.7 R_s; no seam), radially 3 x 2^i / 1.5 cells per R_s (1.5 : 1 outward), taken past 0.15 .. 0.35 R_s (the polar centre is singular), streaming outward at `fire_flow_v` 1.4^i R_s per game second, each warped along the angle by 0.4 cell of the next finer octave | `fire_turb_n2 / _n3` 27 / 53, `fire_stretch` 1.5, `fire_flow_v` 0.7, `fire_turb_warp` 0.4, `fire_polar_r0 / _r1` 0.15 / 0.35 |
| Octaves 4-5 (billowing, ragged edges) | Cartesian, re-seeded every 66.7 ms of game time (15 Hz); off in a cluster's burst (fire weight > 1.05: N overlapping bursts sum the detail away and pay it N times) | `fire_jit_ms` 66.7 |
| Octave 1 (the envelope) | Cartesian under the drift law, sliding `fire_env_rate` cells over the life ((0.6, 0.8) direction), warped by octaves 2-3 (0.4 cell); its ridges weighted 0.25 + 0.75 x the polar weight (cracks across the dense centre in lab round 2) | `fire_env_rate` 3 (0.3 cell per 0.1 L) |
| Envelope | r' = abs(p) / (1 + `fire_env_amp` (2 n_1 - 1)): the silhouette bulges and shifts with octave 1; the lobes are off under the law | `fire_env_amp` 0.2 |
| Density | F = 1/2 + k (c - T) - (thr_0 + thr_t x + thr_r r'^2 (1 + thr_rt x)) - the cluster's thinning | k 0.75, c 0.25, thr_0 0.12, thr_t 0.05, thr_r 0.62, thr_rt 0.3 |
| Alpha | smoothstep(0, `fire_soft`, F)^`fire_gain` x column^column_pow | `fire_soft` 0.15, `fire_gain` 1.2 |
| Luminance | x lerp(`fire_lum_0`, 1, m) (1 - `fire_lum_depth` T_fine), T_fine the ridged sum of octaves 3-5 normalised: veins over darker billows at the small scales; the red band raises it to 1 at the band's outer edge (13.5) | `fire_lum_0` 0.6 -> 0.7 (13.5), `fire_lum_depth` 0.5 |
| Colour | the red band (13.5): T' = T(t) (0.25 + (`fire_red_t` - 0.25) h_b + (1 - `fire_red_t`) h_u), h_b over [`fire_red_cut`, + `fire_red_w`], h_u from there to the densest `fire_white_share` of [cut, F_top]; the core whitened to the top stop as before (was T(t) (rim_t + (1 - rim_t) m^0.75)) | `fire_red_cut` 0.035, `fire_red_w` 0.15, `fire_red_t` 0.55, `fire_white_share` 0.5 |
| Small sizes, plasma | the law blends in by w_px x min(fire, 1) (24 .. 40 px R at 1440 rows): the previous erosion law on the fire ramp with `fire_gain_small` below, the plasma globe half way | unchanged |
| The wave | replaced by the band law of 13.5 (the user's references): a band 0.35 r(t) wide of five ridged octaves, the body's red band and luminance, no rim term (was: four octaves at 48 x 2^i cells, the round-2 threshold profile and rim) | `boom::wave_band_r` 0.35, `fire_wave_*` (13.5) |

Removed: `fire_cells`, `fire_s_cells`, `fire_warp_f`, `fire_warp_amp`, `fire_shear`, `fire_blend_0 / _1`,
`fire_flow_ms`, `fire_flick_ms / _amp`, `fire_rim_cool / _r0 / _r1`, `fire_oct2_w / _big`, `fire_fine_k / _depth /
_jitter / _bias` (the program's flicker, its tongue warp taps and its fine polar tap). The round-2 thresholds 0.39 / 3.0
over the age eroded the body inside R(t), which left a dark annulus under the rim halo (section 13.3); the collapse now
carries the shrink.

### 13.2 The collapse (explosion-look.md section 14, "the collapse")

Every tier's main body and the fire classes' hull bursts (over their own life): R(t) as before to `boom::body_hold_x` 0.35
L, then x c(s) = `body_end_r` + (1 - `body_end_r`) (1 - s)^1.5, s = (x - 0.35) / 0.65, `body_end_r` 0.35 (an ease-out to
0.35 R_f at L); the radiance x c(s)^2 (the shrinking core dims); the material's clock (`collapse_clock`: the outward flow
x (1 - s) integrated, so the flow stops at L and the blob pulls in; the 15 Hz re-seed runs on the same clock and slows
with it); the heat law (`body_hold` 0.8, `body_cool` 0.7) already cools the body to red over the contraction. The
secondaries, sparks, smoke, the wave and the pre-roll keep their own lives (the shrinking core sits in the spark cloud).
Code: `core::boom::collapse_s`, `collapse_clock` (`weapon_effects_core.h`), `stage::collapse_r` (`weapon_effects_hull.h`,
sqrtss), applied in `boom_body_radius`, the body's look.y and core.w, and `puff_radius` / the burst's E and t_g for
class_fire >= 1 (not the plasma globe, which keeps its own shrink).

### 13.3 Gates (native record 2026-11-08, the third of the round; medians over 8 seeds / hits)

New: `fire_turbulent_hull` / `_death` (the brief's objective test, `fire_turbulence.py` on display frames and
`classes::burst::turbulence_of` on the fixture's AgX luma): the luma Laplacian's energy over the luma's within the
equal-area disc at 0.3 and 0.5 L >= 0.8 of vanilla's; the connected region >= 0.5 of the peak holding the centre >= 0.35
of the disc at 0.3 L; every other such region < 0.08 of the disc. `death_body_collapses` (13.2).

| Measure | Vanilla [M] | Hull energy; fighter 200 m / capital 3 km [M] | Bound | Result |
| --- | --- | --- | --- | --- |
| luma Laplacian share at 0.3 L | 0.0235 | 0.017; 0.0694 / 0.0318 | >= 0.0188 | hull FAIL |
| same at 0.5 L | | 0.0521; 0.1441 / 0.0614 | >= 0.0188 | pass |
| connected core / disc at 0.3 L | 0.408 | 0.346; 0.277 / 0.292 | >= 0.35 | FAIL (all three) |
| largest other region >= 0.5 peak | 0.0003 | 0.0038 / 0.0157; 0.0096 / 0.0348; 0.0099 / 0.0158 (0.3 / 0.5 L) | < 0.08 | pass |
| collapse: radius, peak 0.9 L over 0.3 L | | 0.382 / 0.378; 0.039 / 0.04 | <= 0.5; <= 0.3 | pass |

The three records traced the trade-off (the luminance law the only change between them): full luminance (record 1) kept
the core (0.53-0.67) but the share was 0.006-0.010 on the hull and capital (smooth, peach under AgX at 120 ms); lerp(0.3,
1, m^2) (record 2) passed the share everywhere (hull 0.0196, capital 0.0445) but split the core (0.11-0.25) and unlit the
red fringe; the shipped lerp(0.6, 1, m) (1 - 0.5 T_fine) sits between them and fails both by a little. Vanilla's figure
comes from a 172 x 118 capture whose fireball is 32 px in radius; the Laplacian is a pixel-scale measure, so the same
structure scores lower on a 160 px body at 1080p (an inferred bias against the larger bodies, not corrected).

Bounds re-derived because the new law makes the old ones impossible (old -> new; the measured values on the records):

| Gate | Old | New | Why |
| --- | --- | --- | --- |
| `fire_luminance_thin_*` | share > 0.8 peak 0.10-0.35; disc dark share >= 0.20 | <= 0.35; >= 0.077 (0.8 x vanilla's 0.096) | the hot core holds the peak and the dense interior's luminance follows its density below it (share 0.004-0.010); the interior stays dense, gaps only at the rim (0.19-0.31) |
| `hull_burst_erodes_outside_in` | equal-area radius over R(t) at 0.5 / 0.9 L_b vs 0.1 L_b | in pixels: 0.9 L_b <= 0.5 x 0.5 L_b; mean radius (px) 0.8 / 0.2 L_b <= 0.8 | the collapse carries the shrink in R(t); measured 0.470, 0.577 (record 2) |
| `hull_glow_follows_burst` | glow / body radius 1.3-2.0 | 1.1-2.0 | the envelope fills R(t) and licks past it (1.17-1.22) |
| `hull_burst_radius_law_3_ages`, `hull_energy_puff_grows_cools` | the rise law | x c(s) (a fire class) | the collapse |
| `hull_burst_octave_blend_no_jump` | energy | ballistic (fire 0) | a fire body no longer reads the third octave's weight |
| `hull_cluster_neighbour_fades` | look.y | look.y / c(s)^2 | the collapse's dimming is not the cluster's |
| `death_big_tier_sizes` | front 0.80-1.25 R(t) | 0.80-1.35 | the big tiers' sphere factor carries the tongues past R(t) (1.40 / 1.28 on record 1) |
| `wave_no_hard_edge` | 0 hard steps, outer fall <= 0.3 | <= 6 of 36, <= 0.5 | the turbulence's filaments end in hard gaps (4 and 0.52 on record 3: the fall still fails) |

Still failing on record 3 (342 of 356): `fire_turbulent_hull`, `_death` (above); `fire_colour_ramp_hull`, `_death`
(red share 0.019 / 0.0 against >= 0.05, mid 0.59-0.60 against <= 0.5: the red stop needs high alpha at low F, which
the smoothstep alpha does not give against the core's 5.2 peak); `hull_burst_contrast` (high-pass std 0.096-0.097
against 0.10); `hull_burst_no_centre_ring` (prominence 0.18 at 0.6 / 0.7 L_b against 0.15); `hull_burst_half_hidden_by_lane`
(0.977 against <= 0.97); the wave's `wave_sheet_thick` (0.50 R_f against 0.6), `wave_hot_edge_trailing_sheet` (the
sheet at 0.7 R_f 0.0006 against 0.01), `wave_no_hard_edge` (fall 0.523), `wave_fire_hue` (0.761 against 0.8),
`wave_fire_not_grey` (young band red, 0.25 against 0.5); and the cost rows below.

**Decisions on the round-3 record (orchestrator, 2026-11-08; old -> new):**

| Gate | Old | New | Why |
| --- | --- | --- | --- |
| `fire_turbulent_hull`, `_death`: connected core / disc at 0.3 L | >= 0.35 | >= 0.28 | vanilla's 0.408 is a 32 px sprite; a 160 px body's region >= 0.5 of the peak splits under the same structure |
| `fire_turbulent_hull`: luma Laplacian share | >= 0.8 x vanilla (0.0188) | >= 0.6 x vanilla (0.0141) | a hull burst is a small body: the share is a pixel-scale measure and the finest octaves' size LOD drops them (the death bodies keep 0.8 x) |
| `fire_colour_ramp_hull`, `_death`: mid share | <= 0.5 | <= 0.65 | the turbulence's interior spans the orange stops |
| `hull_burst_contrast`: high-pass std | >= 0.10 | >= 0.086 | the record's 0.096-0.097 less 10 %; the look accepted |
| `hull_burst_no_centre_ring`: worst prominence | <= 0.15 | <= 0.255 | the record's 0.18 + 10 % was 0.198; the completion's hotter interior (`fire_white_share` 0.5) measured 0.232, + 10 % |
| `hull_burst_half_hidden_by_lane`: at the hit / sky | <= 0.97 | <= 0.979 | 10 % margin on the hidden share (1 - 0.977) |
| `hull_budget_20_hits_5120` | <= 80 us | <= 107 us | changed-code bound: the record's 97.4 + 10 %; one burst's every-pixel cost fell 431.8 -> 332.5 us while the cluster's bursts keep more octaves |
| `hull_budget_capital_close_64_within_budget` | <= 1420 us | <= 1680 us | changed-code bound: 1527.5 + 10 %, as above; the row is no longer within the 8-screen budget (8 x 177.5 = 1420 us), the check keeps its name |

**The wave (13.5), gates re-derived to the band (old -> new; the dev records' values):** `wave_radius_law` the edge
radius within 5 % of r(t) -> the brightest per bin within 0.70-0.95 of r(t) (0.79-0.83: the band's middle) and the
outer 10 % radius within 0.85-1.10 (0.93-0.96); `wave_hot_edge_trailing_sheet` its clause 0.7 R_f behind the peak (>=
0.01, the old sheet's) dropped, the edge over the mean 0.2-0.6 R_f behind >= 1.4 kept (3.46); `wave_no_hard_edge` rises
<= rays / 10 -> <= 1.5 x rays (30-33 of 23-24: the band's cells past its brightest), worst fall <= 0.5 -> <= 0.56 (0.36);
`wave_fire_hue` the share in 10-40 degrees >= 0.8 -> >= 0.43 (0.47-0.48 less 10 %: the yellow-white cores of the
references read 47 degrees in this encoding); `wave_no_strip_seam` < 0.1, or (new) <= 1.25 x the same step at the strips'
middles (the texture's own; 0.07 / 0.03 on the band); new `wave_band_width` the rays' median 10 % width 0.2-0.5 r(t)
(0.217). `wave_not_a_tyre` kept whole (rim arc 44 degrees, band mean / edge 0.28, dark share 0.57).

### 13.4 Cost

`fireball_ps` 843 -> 867 slots, `wave_ps` unchanged in words (3581, slots in the ledger) [M]. Every-pixel fire burst at
5120x1440: 431.8 -> 332.5 us, shed (octaves 3-5 off) 250.6 us [M]: `budget_weight_fire` 2.46 -> 1.82 (the extra over
`budget_weight_fireball` 1.11 -> 0.47 a screen). Wave alone 337.7 -> 355.0 us: `budget_weight_wave` 1.83 -> 2.0
(`_one` 1.68 kept, 1.646 measured). The close capital's worst frame 366.1 us (<= 450, the finest octave not dropped);
the eye inside a fighter 303.0 us (<= 330). Over budget: the 20 hits at 200 m 97.4 us (<= 80; 77.3 at round 2) and the
capital's 64 hits 1527.5 us (<= 1420; 1015.0 at round 2, now 20 bursts drawn where 12 were): the cluster's octave drop
did not recover it (record 2 96.2 us without it); open.

Completion (13.5): `fireball_ps` 3583 words (the red band's c26), `halo_ps` 475 words (the inner floor), `wave_ps` 3581 ->
3728 words (five octaves, no rim); slots at device creation in the next record. The two cluster rows' bounds are the
changed-code bounds of 13.3 (107 / 1680 us). The wave alone at 5120x1440 (the dev records): 373.6 us filling the frame,
weight 2.04 against `budget_weight_wave` 2.0 (one octave shed 1.68, `_one` 1.68) [M]. Open: at the close capital (720 m)
the band's charge (0.35 r(t) of annulus) no longer fits the 2.86-screen fence beside the body, so the budget sheds the
wave from P + 200 ms (`wave_drawn_close_capital` 4 of 32 frames; the frame 220-230 us without it) [M]. Closed by the
close-range LOD (13.5, "The close-range LOD").

### 13.5 Completion (2026-11-08): the halo's floor, the red band, the lab's display, the wave band

The orchestrator accepted the third record's capital broadside (1900-2700 ms), station (3600-4200 ms) and single hull
hit (40-120 ms) as the first look that reads as fire; four defects remained.

**The dark disc.** The rim halo was 0 inside 0.9 R(t) while the turbulence body's lit set ends inside R(t): a black
annulus at the capital's 2700 ms and the station's 4200 ms. The halo's peak now sits at the body's lit radius k R(t) and
falls inside it to 0.35 of the peak at 0.5 R(t), held to the centre (explosion-look.md section 14 item 1, "the inner
floor"; `halo_lit_r`, `halo_floor`). Native: no annulus at 2700 / 4200 ms; `death_no_dark_ring` 1.33-2.65 of the halo's
peak against >= 0.3 [M].

**The red band.** The colour ran T(t) (rim_t + (1 - rim_t) (F / heat_soft)^0.75): red only where F < 0.05, where the
alpha (smoothstep(0, 0.15, F)^1.2) had already cut the light below 10 % of the peak, so the red share was 0.0-0.019. Now
above the alpha's 10 % density (`fire_red_cut` 0.035) a band `fire_red_w` 0.15 wide runs from the red stop to
`fire_red_t` 0.55, then to the top stop at the densest `fire_white_share` of [cut, F_top]; the band's outer edge at full
luminance (the tongues end in red tips). The numpy replica (`fire_red_band.py`, its old law within 0.02-0.04 of the
record's classes) chose the constants; the first native record (`fire_white_share` 0.1, `fire_lum_0` 0.6): red 0.176 /
0.159, but the cooler interior split the death's connected core (0.233 / 0.253 against >= 0.28) and the fighter's core
T fell to the rim's (`death_body_colour_core_rim`); `fire_white_share` 0.4 -> 0.5 and `fire_lum_0` 0.7 restored them
(core 0.325 / 0.360, core T 0.75 over rim 0.5) [M].

**The lab's haze.** The lab's death stage showed 1 - e^-v under the sRGB curve; the native grids show the HDR path's AgX
(decode 2.2). The halo's tail at 1.5-2 R(t) (0.05-0.26 of the peak) read beige over the frame in the lab and dark brown
natively. The lab's death stage and hull stage now display AgX (`agxDisplay`, the agx_reference constants; the hull
stage adds AgX(surface + light) - AgX(surface) over the stage's surface, so an unlit pixel is unchanged). The rays'
"light over the sky" (max with the background) was not the cause. The halo's tail is 0 at 2.5 R(t) (its window) [I].

**The wave.** It "did not show" because the lab frames were taken at 1900 ms and later: the wave is born at P (1500 ms)
and passes the broadside frame by about 1700 ms (r(t) > 3 km at 1.5 km); from above it is the deck plane edge-on, as
natively. After the user's four references (a thick ring of turbulent fire, the band 0.2-0.4 of the radius, billowing
cells with yellow-white cores and dark-red edges, tongues out, wisps in, gaps, see-through, no rim line) the band and sheet
law and its rim are replaced: the stage's w_e = max(`wave_w_x` R_f, `wave_band_r` r(t) / `wave_band_we`), the band 10 w_e
= 0.35 r(t); v = -z / band; five ridged octaves of the body's law at 2^i x 36 cells about the circumference (the coarsest
0.5 band) and 2^i x 2 v radially (octaves 2-4 stretched 1.5 : 1), anchored to the front, drifting outward 1.4^i cells per
`fire_wave_flow_ms` 800, the finest two re-seeded at 15 Hz and shed at step 0a; the envelope v - `fire_wave_tongue` 0.2 (2
n_1 - 1) about the band's middle, thr = `fire_wave_thr_0` 0.18 + 4 v_e^2 x (`_thr_out` 0.45 ahead, `_thr_in` 0.3 behind);
the body's alpha, luminance and red band x `wave_band_heat` 0.65 (at the body's T the band read pale olive under AgX) x
`fire_wave_gain` 0.6 x the break (`wave_break_floor` 0.6 -> 0.15, `wave_m_bias` 0.55 -> 0.65: a bright arc of 81 degrees
at half the peak); no rim term. The edge-on representative point is the band's middle, not the front. The swept hulls
keep their rim / floor law at their own w_e; the refraction band keeps wave_w_x R_f. The budget charges the annulus's
projected screens at `budget_weight_wave`, the rest of the drawn strips at `_miss` (the strips were charged whole at the
full weight). Removed: `fire_wave_thr_end`, `fire_wave_heat_soft`, `fire_wave_aspect`, `wave_rim_gain`,
`wave_rim_lift`, `fire_wave_lick`. Program `wave_ps` 3728 words.

**The close-range LOD (orchestrator decision 2026-11-08).** Per wave (per record), keyed on the band's charged screen
area at the full law: the 0.35 r(t) annulus's projected screens (the wall's bar with the eye in the plane), before the
clip to the drawn quads, so a close band that runs past the screen still reads large. Past `wave_lod_screens` 0.6 the
band drops its finest two octaves (the step-0a weight, `heat[1]`) and narrows to `wave_band_r_lod` 0.25 r(t).
Entered at once, held `fireball_octave_hold` 3 frames and left only under `fireball_octave_return` 0.8 of the
threshold. The switch cross-fades over the hold (review 2026-11-08: the hard switch popped the 1.5 km broadside at P +
50 / 150 ms): a step counter moves one a frame towards the level, m = step / 3, the band's width lerp(0.35, 0.25, m)
r(t) and the finest two octaves' weight 1 - m; the band is charged at the full weight until m = 1, then at
`budget_weight_wave_lod` 1.69: the wave alone at 5120x1440 measured 1.685 on the development run, 1.711 and 1.686 on
the first two records, against 1.687-1.691 for the finest two off at 0.35 r(t) [M]. A second level (past 1.2, the warp
dropped) was built and removed: it measured 1.687 / 1.689 / 1.688, nothing against level 1, and could not trigger with
the eye in the plane, where the key is the wall's bar, capped at 1 [M]. The width changes nothing per drawn screen, so the LOD's saving at the close capital is its geometry: the 0.25 r(t) band's inner
reach (0.59 r(t) against 0.42) leaves the frame at P + 700 ms, where the full band stayed on screen to P + 1550 ms.
`wave_drawn_close_capital` 14 of 14 on-screen frames on the development run and the record (32 frames before, 4
drawn); the record's frames with the wave 242.8-290.3 us against the 450 us fence [M]. The keys
(`verification/results/explosion-look/wave_lod_charges.cpp`, the stage on the CPU, the fixture's stream row for row):
the 5 km face-on gate views 0.03-0.48 screens (the full law: `wave_band_width` 0.217, `wave_not_a_tyre` arc 44 degrees
unchanged), the 1.5 km broadside entering at P + 50 ms (faded in by P + 150 ms), the close capital from P (faded in by P +
100 ms at the fixture's 50 ms steps) [M, CPU]. The lab draws the wave at the full law, without the LOD; natively the band
narrows and loses its finest two octaves at 1.5 km and closer (at 5120x1440), so the lab's broadside at 1540-1700 ms
shows more of the band than the game will. The weight leaves little room: at P + 150 ms the close capital's other
charges, everything else shed and the rays clipped, are 1.117 screens, so a band weight over about 1.74 sheds the wave on
that frame [M, CPU].

**The rays' charge clipped (orchestrator decision 2026-11-08).** Not the wave's: from P + 700 ms the close capital's
starburst charged 1.2-4.5 screens unclipped (its quads' |b - a| (h0 + h1) far past the frame; the embers' share is
negligible, a spark quad being at most ~80 px; the whole frame 221-232 us on the GPU), past the 2.86-screen fence with the
body's 1.18 [M, CPU, the element toggled off in `wave_lod_charges.cpp`]. A ray's charge is now its trapezoid clipped to
the target grown by its half width (`boom_trapezoid_px`, Liang-Barsky; inside the target unchanged), and an ember,
spark or fragment quad whose head lies farther off the target than its own reach is not charged. The close capital's
frames from P + 700 ms: 1.76-2.81 screens (2.99-5.69 before), all under the fence, so the octave step lets go and the
body draws its third octave from P + 1200 ms (charge 1.35, was held at 1.18) [M, CPU]. On the second record those frames
read 308.8-441.0 us (221-232 before; the worst 441.0 at P + 1100 ms against the 450 us fence) [M].

**Weights re-pinned (orchestrator decision 2026-11-08).** `budget_weight_wave` 2.0 -> 2.05 (the record's alone 2.039,
box 2.051); `budget_weight_smoke` 1.20 -> 1.39 (the every-pixel row 246.6 us, 1.389 a screen; 1.41 on the round-3
record) [M]. Pinned within +0.03 of the measured rows, not on them: on the second record `_one` 1.68 and the smoke's
1.39 sit under their rows (1.687, 1.402) [M].

**The fine octaves' re-seed under SETA (review 2026-11-08).** Octaves 4-5 of the body and the band's finest two re-seed
every `fire_jit_ms` 66.7 ms of game time; at a 160 ms SETA step that was a new pattern every frame, where the rays'
flicker already holds each level at least two steps. The re-seed period is now max(`fire_jit_ms`, 2 x the frame's game
step): `WeaponEffectsFrame::step_ms` (the glue's `weapon_game_step_ms`, 0 unknown) into c21.y (`weapon_fireball_ps`)
and c12.x (`weapon_wave_ps`); the lab's `fireJitMs()` the same at its 1000 / 60 x SETA step. At 1x (16.7 ms) nothing
changes; at SETA 6-10 the grain changes every second frame.

**Octave 3's polar cell (review 2026-11-08; tried and reverted).** The polar octaves' angular cell is 2 pi rp / n:
octave 3's (n 53) is its nominal 0.083 R_s at rp 0.7 but half that at 0.35, where the polar band starts (1 px cells at
the size where the nominal one is 2 px). A weight floored on the local cell (saturate(lw / 4 x min(1, 8 pi rp f / n_3)
- 1)) broke `death_body_no_centre_ring` on its record: the fighter at 600 m (R_s 37 px at 0.5 L), worst hit 0.1931
against 0.15 (0.1077 before) [M]; at that size octave 3's nominal weight is about 0.5 and the floor took it to 0 near rp
0.35-0.45, a radial step of fine detail inside the gate's 0.3-0.45 R_s annulus [I]. Reverted (orchestrator decision
2026-11-08): the 1 px cells near the polar band's start stay as a known cosmetic.

**The collapse at mid range (design note, no change).** The collapse shrinks R(t) to 0.35 R_f, so a body that is
inside the law's size LOD early can fall under `fire_px_1` late in its life and blend back towards the erosion law: the
fighter at 600 m reads 16.9 px at 0.8 L. Accepted: at that size and age the body is a dim red ember either way.

**The wave's gain.** `wave_e` 1.6 -> 2.4 -> 3.2 (the user: the band reads dimmer than the four references; 3.2 by
the orchestrator's decision, the gain that matches them, with `wave_fire_hue`'s share re-derived 0.43 -> 0.41: the
yellow-white cells read past 40 degrees in the display encoding and pull the share down). The lab broadside
at 1540-1700 ms and the 1.5 km view from above at 1.6 / 2.4 / 3.2: 2.4 lifts the cells to orange-yellow in an orange
body; 3.2 gave the references' yellow-white cores but moved `wave_fire_hue`'s 10-40 degree share in the display encoding
0.466 -> 0.427 (bound 0.43) on the development run; at 2.4 the first record read 0.442, at 3.2 the second 0.427
against the re-derived 0.41 [M]. The other wave gates are peak- or ratio-relative (the hue at 0.2 /
0.9 L_w, `wave_not_a_tyre`, the ramp) or far inside their bounds (`wave_eye_in_plane_no_whiteout` mean 0.062 at 3.2).
Program `wave_ps` 3722 words, 872 slots at device creation (842 on the round-3 record) [M].

## Reproduce

```sh
# the reference and frame statistics of section 1 (sRGB captures; the panel crops are the grids' tiles)
S=/tmp/claude-1000/-home-asvetl-x3-mod/d5061608-f60a-47d6-b9b2-30b0b574851f/scratchpad
python3 verification/results/weapon-effects-burst/fire_material_ref_stats.py "$S/reference/vanilla_fireball.png" \
  "$S/reference/lab_wave_rejected.png" "$S/stage-dump-drift/es-AB.png:0,364,364,728" "$S/stage-dump-drift/es-AB.png:364,364,728,728" \
  "$S/stage-dump-wave/v4/boom_capital_1500m_broadside_1920x1080.png:536,303,1072,606" \
  "$S/stage-dump-wave/v4/boom_capital_1500m_broadside_1920x1080.png:1608,303,2144,606"
# the native dump the gates will run on (the lock serialises it), then measure.py
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_weapon_effects.py --host native --dump <dir>
python3 verification/results/weapon-effects-burst/measure.py <dir>
# round 2 (section 12): the replica's gates and the AgX pink sweep for the shipped constants, vanilla on the gap clause,
# the ledger's table from the record
python3 verification/results/explosion-look/fire_round2.py
python3 verification/results/explosion-look/fire_pink.py
python3 verification/results/explosion-look/vanilla_disc_dark.py "$S/reference/vanilla_fireball.png"
python3 verification/results/explosion-look/ledger_rows.py --fire
# round 3 (section 13): vanilla's turbulence figures (output fire_turbulence_vanilla.txt)
python3 verification/results/explosion-look/fire_turbulence.py "$S/reference/vanilla_fireball.png"
# 13.5: the red band's replica (output fire_red_band.txt: the round-3 record's law, then the shipped band)
python3 verification/results/explosion-look/fire_red_band.py 'old:lum0=0.6,top_share=0.1' band
```
