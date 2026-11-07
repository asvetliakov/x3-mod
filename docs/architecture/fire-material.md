# Fire material: one law for the fireball program family

Design note, 2026-11-07 (branch `effects`, HEAD a74671f26; not ratified, not built). Amends the body laws of
[hull-burst-look.md](hull-burst-look.md) 2.2–2.5 (the hull burst), [explosion-look.md](explosion-look.md) 2.1 and 13.1
(the death body, the fire wave) and [hull-impact-classes.md](hull-impact-classes.md) 3 item 5 (the shock ring's trailing
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
sheds. No atlas, no flipbook (section 10). Note: `docs/architecture/fire-material.md`.

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
one more vertex float (`fire`, packed with `core` as a float2 in TEXCOORD5: stride 80 → 84 [I]) and the fire ramp
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
and at 1x? (4) does the white-yellow core bloom without turning the 20-hit fighter into a sun?

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
construction from memory). Project references used: `docs/reference/explosion-references.md` 3 and 5 (impostor fireball,
flipbook vs procedural, the lamp / balloon / disc failure modes), `impact-effect-references.md` 3 (stepped alpha for
punch, motion-vector blending). From memory, not fetched (blocked hosts): the 10–20 Hz flicker band of small flames and
the puffing law f ≈ 1.5 / √D Hz of pool fires (Cetegen & Ahmed 1993); fire shading as emission from a temperature field
on a blackbody ramp with absorption only in the smoke (Nguyen, Fedkiw, Jensen 2002; Houdini pyro practice); tongue
lifetimes of 50–150 ms and advection of 2–5 R/s [I from flame footage, not a sourced figure].

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
```
