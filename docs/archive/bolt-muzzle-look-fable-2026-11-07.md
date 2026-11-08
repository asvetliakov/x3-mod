# Bolt and muzzle look: projectiles in flight and the event that launches them

Design note, 2026-11-07 (design agent, Fable 5.1), for step 4 of [weapon-look.md](effects-look-2026-11-08/weapon-look.md) 3.3 (ratified, unbuilt).
It fixes the laws of every projectile the proxy draws (tracer, slug, orb, wave, the heavy band) and of the muzzle flash,
with numbers an implementer builds and the lab previews. Hooks, records and the in-place draw of 3.3 stand (section 5
says where they are amended); nothing here edits source. Inputs: [bolt-muzzle-references.md](effects-look-2026-11-08/bolt-muzzle-references.md)
(games, films, techniques, its section 4 anatomy), weapon-look.md 2.1–2.14 and 3.3, [hull-burst-look.md](effects-look-2026-11-08/hull-burst-look.md)
(the vocabulary reused: erosion, cluster saturation, the star, multiply smoke), [weapon-effect-spawn.md](../reverse-engineering/weapon-effect-spawn.md)
2, 5, 6, 8.2 and [weapon-effects.md](weapon-effects.md) 4.1–4.4. Marks: **[M]** measured (file named), **[S]** read in
code or a note, **[R]** the reference study, **[C]** chosen here, **[I]** inferred, **[A]** assumed (an observe row settles it).
The sizes are derived in section 1, not inherited from "double"; the pixel floors stay a safety net. User standards
binding here: full body in the first flight, simple rules and rich visuals, no configuration, nothing keyed on names,
analytic shapes and noise only, readable at range / end-on / over bright stations / in dark space, no walls of light in the
cockpit, SETA to 10x without sticks or a strobing muzzle.

## Outcome

**Every bolt is a world-space segment drawn by one capsule program: a white-hot head inside a tinted body with a cooler
rim, a Gaussian halo, a style-specific tail, stretched to its previous screen position with its energy conserved; its
width is 1.2 m x (1 + 2 k) x the weapon's reach over 2 km, its length is 60 ms of its own flight.** Tracers are long
(60 ms of flight, 12–60 w), slugs a short white head with a thin cooling tail, orbs a boiling sphere with a wisp, waves a
crescent; the heavy band widens the halo and adds a tail. A thin multiply rim keeps the capsule readable over a lit hull.
The muzzle is a disc core, a forward cone (a star for ballistic guns), a glow on the shooter's hull and, for ballistic and
flak guns, a multiply puff riding the ship; 80–250 ms by class, reset on every shot, breathing by a per-shot seed so a
SETA stream reads as a living flame, never a strobe. Muzzle glows saturate per owner by the burst note's cluster rule;
bolts need no saturation (their halos cannot overlap except at the muzzle). The in-place late bullet draw stands, with one
extra multiply draw for the rim. Sizes at the median engagement (0.3 of reach for fighters, 0.7 for capital guns): core
2.5–4 px, halo 18–31 px, length 42–160 px for stock fighter guns at 1440 rows without any floor (section 1).

## 1. Size derivation

| Input | Value | Mark |
| --- | --- | --- |
| Vertical FOV | 73.74° (tan half 0.75), fixed by the engine; F = 960 px per unit tan at 1440 rows, 720 at 1080; px = F x metres / z | [M, field-of-view.md, camera-numerics.md] |
| Screen | 5120x1440 (the user), 1080p for the dump; at 32:9 the horizontal FOV is 139° if the vertical holds [S, own-sun-pass.md] | [S] |
| Ship hull, median per class | M5 8–9 m, M4 10, M3 15, M6 86–88, M7 560–618, M2 1,225–1,337, M1 1,064–1,135 (stock / Mayhem) | [M, game-scale-and-ship-classes.md §4] |
| Reach (lifetime x speed) | stock fighter guns 1.39 (IRE), 1.93 (PAC), 2.31 (HEPT), 2.81 km (EBC); stock PPC 6.6 km, PSP 6.4; Mayhem fighters 3.0–4.5 km, Mayhem capital guns 16.8–21.7 km | [M, weapon-catalog.md] |
| Speed, rate | IRE 1,162 m/s at 6.7 Hz, MASS 739 / 12.3 Hz, PBE 6,980 / 19.2 Hz (the fastest stock), PPC 333 / 0.73 Hz; Mayhem 1,500–3,500 m/s at 0.05–1.5 Hz | [M, weapon-catalog.md] |
| Engagement distance | fighters 0.3 x reach (the AI opens at reach and closes; the player's hits land inside a third), capital guns 0.7 x reach (they trade at most of it) | [A; the observe row `bolt_hit_range` (impact distance from the owner, p50/p90 per style) replaces it] |
| Vanilla bolt length | 0.1 s x speed (`0x004411f7`): IRE 116 m, Mayhem PPC 350 m | [S, spawn note §1 col 11] |

Targets [C]: at the median engagement, without any floor, a bolt core ≥ 2 px wide and ≥ 36 px long and its halo footprint
(10 % of peak) ≥ 15 px at 1440 rows; on a dogfight pass (150 m stock, 300 m Mayhem) the core ≥ 6 px; the own muzzle in
the chase view (guns about 30 m from the F3 camera, the hull 480 px) ≥ 100 px across; another fighter's muzzle at the median
engagement ≥ 6 px; a capital's gun at its trading range ≥ 6 px of glow. Solving 2 px at 0.3 x 2 km gives w0 = 1.25 m; with
k = clamp(log10(d / 300) / 2, 0, 1) [S, 4.2] and a reach factor so Mayhem's 2–10x reach keeps the same pixel size:

    w   = w0 (1 + 2 k) clamp(reach / range_ref, 1, 8)      w0 1.2 m, range_ref 2,000 m, reach = col 10 x col 11 / 500 [S]
    w_m = w0 (1 + 2 k) clamp(reach / range_ref, 1, 3)      the muzzle's width (it sits on the ship: capped lower)

| Weapon | k | w (m) | w_m (m) | z_med (m) | core px | L (m) / px | halo px | muzzle disc / cone / glow R px |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| stock IRE (tracer) | 0 | 1.2 | 1.2 | 418 | 2.8 | 70 / 160 | 21 | 8 / 17 / 22 |
| stock PAC (tracer) | 0.29 | 1.9 | 1.9 | 578 | 3.2 | 42 / 70 | 24 | 10 / 19 / 25 |
| stock HEPT (tracer) | 0.41 | 2.5 | 2.5 | 693 | 3.5 | 30 / 42 | 26 | 11 / 21 / 28 |
| stock EBC (slug) | 0.33 | 2.8 | 2.8 | 844 | 3.2 | head 11 / 13 | 24 | 10 / 19 / 25 |
| stock PPC (slug, heavy) | 1 | 11.9 | 10.8 | 4,636 | 2.5 | head 48 / 10 | 18 | 7 / 13 / 18 |
| Mayhem APC (slug) | 0.32 | 2.9 | 2.9 | 900 | 3.1 | head 12 / 13 | 23 | 9 / 19 / 25 |
| Mayhem PAC (tracer) | 0.58 | 5.3 | 5.3 | 1,215 | 4.2 | 90 / 71 | 31 | 13 / 25 / 33 |
| Mayhem PPC (slug, heavy) | 1 | 28.8 | 10.8 | 13,475 | 2.1 | head 115 / 8 | 15 | 2 / 5 / 6 (floors) |
| Mayhem GKE (tracer, 3/min) | 1 | 28.8 | 10.8 | 5,040 | 5.5 | 346 / 66 | 41 | 6 / 12 / 17 |

All computed by [`size_derivation.py`](../../verification/results/bolt-muzzle-look/size_derivation.py) →
[`_out.txt`](../../verification/results/bolt-muzzle-look/size_derivation_out.txt) [M of the law]. Chase view: the IRE disc
(3.6 m) is 115 px and the cone (7.2 m) 230 px against a 480 px hull. The widths are large next to the hull (IRE 1.2 m on a
15 m fighter, Mayhem GKE 29 m: a lance from a fighter, judged in flight, section 8) because the game's own bolts are
already 116–350 m long [S]; readability at reach, not the gun's bore, sets them. The floors (section 2.6) then only catch
the far end (a PPC at 13 km) and the end-on case.

## 2. Bolt anatomy (question 1)

### 2.1 The shared radial and axial law [C]

A bolt is the segment from P_tail to P_head (world) with width w; d is the pixel's distance to the projected segment, ρ =
2 d / w, s ∈ [0, 1] its parameter (0 tail, 1 head). Radiance, linear, before the resolve:

    core  = core_e 4 x white x k_c(style) x exp(−ρ² / (2 x 0.5²)) x h(s)
    body  = E_b x tint x (1 − rim_cool 0.4 ρ²) x aa(ρ ≤ 1)            the rim at 0.6 tint: the film's saturated edge [R]
    halo  = E_h x tint x exp(−max(d − w/2, 0)² / (2 σ_h²))           E_h ≤ halo_max 0.2 x core_e = 0.8 [S, 2.3]
    rim   = multiply (1 − rim_dark 0.3 x saturate((ρ − 1) / 0.5) x saturate((2.5 − ρ) / 0.5)) on the scene first

`aa` is a 1 px anti-aliased step; h(s) the axial profile: 1 over the head (s ≥ taper), (s / taper)² below it. The tint is
the classifier's (light colour, else texture mean, else white [S, 4.2]); k_c sets how white the head is (a flat colour is
a UI line, a white core on a weak gun is a glint [R]). Radiance at the axis: tracer 2.4 + 1.0 + 0.6 = 4.0, heavy 6.3
(clips white as 2.3 allows). The rim is the readability device over a lit hull [R]: a 0.3 darkening from 1.0 to 2.5 w
outside the body, drawn in the multiply state before the additive capsule (section 5), only where the projected w ≥ 2 px
(below that a dark smear); over black space it does nothing, which is correct. Halo σ in pixels is capped at
`halo_px_max` 40 at 1440 rows (30 at 1080): the one rule against walls of light in every view (a bolt passing 5 m from the
cockpit camera would otherwise light 400 px); E_h is unchanged by the cap.

### 2.2 Per style [C; starting sizes from section 1, proportions from [R] §4]

| Style (classifier) | Head / core | k_c | E_b | Halo σ_h, E_h | Tail | Streak cap |
| --- | --- | --- | --- | --- | --- | --- |
| tracer (`z/xy ≥ 4`) | capsule, L = clamp(v x `len_s` 0.06, 12 w, 60 w), taper 0.3 | 0.6 | 1.0 | 1.5 w, 0.6 | none (the taper is the tail); heavy band adds the slug's | 6 L |
| slug (`1.5 ≤ z/xy < 4`) | capsule 4 w, taper 0.25 | 0.9 | 1.2 | 1.5 w, 0.4, head only | thin 0.6 w, L_t = clamp(v x `tail_s` 0.08, 8 w, 40 w), width tapering to 0.2 w, colour white → tint (s 0.6) → tint x 0.4 at the end, E 1.2 → 0.3 | 6 L (L = head + tail) |
| orb (`z/xy < 1.5`) | sphere radius 1.5 w: chord density sqrt(1 − ρ_o²) x (1 + 0.35 (2 n − 1)), n = two octaves of 2-D value noise at 2.5 / 5.5 cells per radius advected at `boil_hz` 3 (8 hashes, the burst note's `vn`) | 0.7 breathing 0.5–0.9 with n at the centre | 1.2 | 3 w, 0.8, elongated 1.3 : 1 along v | comet wisp 8 w behind, width 1 w → 0, density x (1 − s) x n (noise-broken), E 0.3 | 1 L |
| wave (`flags & 0x8`) | crescent across the travel: chord C = col 18 / 500 m [S, length; axis **[A]**, fallback 8 w], sagitta 0.25 C, thickness 1 w, bowing forward | 0.3 | 0.6 | 1 w, 0.3 | none | 1 L |
| heavy band (d ≥ 20,000, k ≥ 0.91 [S, 4.2]) | the style's, x 1.5 width is already in w | 1.0 | 1.5 | 2.5 w, 0.8 | tracer gains the slug tail at 20 w; slug tail 40 w | 6 L |

Colour: tracers and the heavy band hold the tint to the tail end; the slug tail cools white → tint → tint x 0.4 (no
heat ramp: the weapon owns no orange; three owners [S, 2.1]); the orb breathes. The wave and orb take a 1 L streak so
they never stretch; under SETA the TAA clamp covers their previous position (gate 7.14).

### 2.3 Streak, SETA and the stick [C]

The head is drawn from its previous Present's screen position to its current one (both through their frames'
view-projections, the motes' precedent [S, 2.5]); the drawn segment is P_prev … P_tail … P_head with the streak part
(P_prev to P_tail) shaped by the tail law: radiance (1 − u)² from the tail end (u 0) to the previous position (u 1),
never uniform. Energy law: the whole segment's radiance x L / (L + streak), the core held at E ≥ `streak_core_min` 1.2
[S]. Cap `streak_cap` 6 L (tracer, slug), 1 L (orb, wave). Arithmetic at 60 fps [M, size_derivation_out.txt]: the
displacement per frame is 0.28 L at SETA 1, 1.67 L at SETA 6, 2.78 L at SETA 10 and reaches the cap at SETA 25, so up to
SETA 10 the streak always covers the previous position and the bolt is 3.8 L long at most (IRE: 265 m of a 1,162 m/s
flight). **A stick** is a uniform rod: the gate measures radiance at the streak's far end ≤ 0.25 of the head's and the
head at full. A frame whose game step is 0 (pause) draws the rest shape.

### 2.4 End-on, birth, death [C]

- **End-on** (projected segment < 1 px, or |v_screen| under 1 px per frame): the program sees d from a point: a disc of
  diameter w_px floored at 4 px (1440) / 3 px (1080) with its halo σ floored at 4 / 3 px, so the 10 % footprint is ≥ 17 /
  13 px: a dot with a visible halo that grows as it comes [R, Squadrons]. No special case in the program.
- **Birth**: P_tail = P_head − v̂ x min(L, v x age): a bolt is never longer than it has flown, so it grows out of the
  muzzle over L / v (IRE 60 ms) and no 30 ms constant is needed; under SETA it is born full, which is right.
- **Death**: a record absent from this pass's tap list and not named by an impact record this frame fades over `end_ms`
  60 (≥ three frames) at its last pose extrapolated by v; a record named by an impact (shield or hull, the same bullet id)
  ends at once at the hull (gate 7.11): the hit families take over. Lifetime from `ext+0` is not needed.

### 2.5 Stream against strobe [C]

Stream ratio r_s = L f / v = 0.06 f (tracer): IRE 0.40, MASS 0.74, PRG 0.64, PBE 1.15, Mayhem ≤ 0.09 [M, catalogue
rates]; when r_s > 1 the core is trimmed to 0.8 v / f so bolts stay separate (PBE only). A **stream** is coherent:
each bolt's successive images overlap (2.3) and every record in the tap list is drawn every frame it is listed, so at
10 shots per game second the eye sees a chain of bolts 100–150 m apart moving together; under SETA 10 their streaks
overlap into a dashed line, which is motion, not a defect. **Strobing** is a bolt whose lit pixel count alternates
between frames (the engine's visibility cull admitting it on alternate frames, unknown 14 of weapon-look.md) or drops
to 0 before its end; gate 7.12 counts it. The three-frame floor applies to the death fade, not to a hit (the hull flash
is the three-frame element there).

### 2.6 Pixel floors (safety net, raise size never radiance [S, 2.2])

| Floor | 1080 rows | 1440 rows | Reader |
| --- | --- | --- | --- |
| core width `floor_w_px` | 3 | 4 | the capsule's w_px, the end-on disc |
| length `floor_len_px` | 12 | 16 | the projected head length (streak excluded) |
| halo σ `floor_halo_px` | 3 | 4 | σ_h in px |

Scaled linearly with rows between and beyond (render pixels at the presented resolution).

## 3. The muzzle flash (question 2) [C]

Class = the weapon's hull anatomy class [S, hull-impact-classes.md rule table: ballistic = ammunition flag `0x40`, heavy =
d ≥ 20,000, plasma = an orb body or the wave flag `0x8`, beam = `0x2`/`0x4`; ion, repair and tug take the energy flash]
plus one cue of this note [C]: flak = `flags & 0x1100` (area death or fragments) before the class rule; missile launches
have no claimed site (unknown 4). Record per (ship, gun slot) from `0x00446268` (EAX = bullet index, EDX = &orientation, `&pos`
in the shooter's frame [M, spawn §6]); re-light **resets** age to 0 and re-rolls the seed = hash(ship, slot, now_game);
never adds. Sizes in w_m (section 1); every element the three-frame floor and a linear fade; occlusion class 2, SOFT 0.15.

| Class | Core | Flame | Secondary | Life ms | Hull glow (R, E tint, life) | IRE / PPC metres (disc dia, flame, R) |
| --- | --- | --- | --- | --- | --- | --- |
| energy | disc r 1.5 w_m, white k_c 0.8, E 4 | cone 6 w_m x seed(0.7–1.3) along the gun axis, half-angle 15°, tint E_b 1.2, (1 − s)² along it | none | 120 | 8 w_m, 0.8, 120 | 3.6 / 7.2 / 9.6; 32 / 65 / 86 |
| ballistic | disc r 1.0 w_m, white-yellow (tint lerp white 0.7) | 5-arm star of 2.5 w_m (the burst note's star law, arms {1, 0.6, 0.85, 0.5, 0.7}, rotated by the seed) + cone 4 w_m | multiply puff r 3 w_m → 5 w_m over 300 ms, s0 0.35, the burst note's smoke law, parented to the shooter, over the lane's surface only | 80 | 5 w_m, 0.6, 60 | 2.4 / 3 + 4.8 / 6 |
| plasma | disc r 2 w_m, k_c 0.6 | sphere bloom r 3 w_m with the orb's boil, E_b 1.2 | none | 180 | 10 w_m, 1.0, 180 | 4.8 / 7.2 / 12 |
| heavy | disc r 2.5 w_m, k_c 1.0 | cone 12 w_m, half-angle 10°, rising over the first 40 ms (no pre-glow: there is no event before the shot) | two side vents 8 w_m long, 0.5 w_m wide, ± 90° in the gun's plane, 150 ms, E 0.8 | 250 | 15 w_m, 1.2, 250 | PPC 54 / 130 / 162 |
| flak | disc r 1.0 w_m, tint lerp white 0.5 | star 1.5 w_m | puff r 4 w_m, 400 ms, s0 0.35 | 80 | 4 w_m, 0.5, 80 | — |
| beam (pulse start) | the energy disc | none | none | 120 | 6 w_m, 0.8, 120 | — |

Light on the hull: the glow primitive (`E = tint x E_g saturate(1 − d² / R²)²`, back faces, lane surface [S, 2.8]), life
= the flash's, **per-owner cluster saturation** as the burst note's: s(N) = 1 / (1 + (N − 1) / 2.5), N the owner's live
muzzle records within 2 R of this one. A 20-gun broadside at 60 m spacing clusters by 2–4 (R 54–162 m): overlapping glows sum
to ≤ 2.3 E_g; the flames do not saturate (they do not overlap). Own cockpit: flash and glow x `own_cockpit_gain` 0.35 [S,
2.9]; the puff is not drawn in the cockpit (it would sit on the camera). Capital broadside: every gun of a group fires
through `0x00445170` [S, spawn §2], one record per slot; the user sees a rolling broadside because each slot's seed differs.
Stations and platforms: no muzzle until their fire routine is traced [M, spawn Unknown]; their bolts come from the tap.
SETA at 10 shots per game second: the energy flash (120 ms game, 12 ms wall at SETA 10) is re-lit every frame; the seed
changes its cone length per shot (0.7–1.3) and the star's rotation, so the muzzle is a living flame whose lit area
varies 30–40 % frame to frame and never drops to 0 inside a burst; strobing (a 0 frame, or > 50 % alternation) is the gate.

## 4. Light, overlap and the budget (question 3) [C; weights [I] until measured]

Bolts: no saturation rule. Two halos overlap only within 2 x 2.15 σ_h = 6.5 w (IRE 8 m); one gun's bolts are v / f ≥ 60 m
apart (MASS) and a wing pair's guns 4–8 m, so N ≤ 2 anywhere but the first frames at the muzzle, where the flash is the
light anyway: the summed peak is ≤ 2 x 0.8 halo + cores, under the burst note's 3.0 bound. Muzzle glows take the cluster
rule (section 3). Weights (full-screen µs over the decal's 177.5): capsule 0.25, orb 0.6 (8 hashes), rim 0.15, muzzle
sprite 0.49 [M, the hull flash], glow 0.40 [M], puff 1.0 (the burst's smoke, to measure). Frame bound at 5120x1440:
**512 bolts + 20 muzzles ≤ 250 µs GPU** (bolts ≤ 150: 512 at the median engagement are 0.2 screens; ten bolts at 50 m
with the 40 px halo cap are 1.5 screens x 0.25 ≈ 66 µs), muzzles ≤ 60 µs at 600 m (`muzzle_budget_20_guns_5120`, raised
from 3.3's 30 for the larger sizes) and ≤ 100 µs for two own guns in the cockpit; **CPU ≤ 100 µs** (tap, cull, two VBs).
Shed order over the shared 8-screen budget: (1) the orb's second octave; (2) other owners' puffs; (3) other owners' muzzle
glows past 4 per owner, oldest first; (4) other owners' rims; (5) other owners' halos narrowed to σ 1 w. Never shed: cores,
streaks, flames, anything of the own ship. Count caps as 2.11 (bolts 512, muzzles 128).

## 5. Drawing (question 4)

The ratified in-place draw stands [S, 3.3; bolts-through-taa.md]: at the frame's late bullet draw our VB, VS and PS replace
the game's vertices, the route's ONE/ONE and depth state (LESSEQUAL, Z write off [S, 2.7]) kept, oC1 = oC0, oC2.g = K x
max(rgb) (the B' far flag, K 32 [M, bolt-far-flag]), write-back composite at W 0.5 over far stations, every other bullet
draw suppressed, state restored. Amendments [C]: **two draws, not one**: first the rim in the scorch's ZERO / INVSRCCOLOR
state with `D3DRS_COLORWRITEENABLE1/2` = 0 (documented states; RT2 must not be multiplied), then the additive capsule; the
fixture's "one draw a frame" gate becomes "two, rim then capsule". One program handles every style: the style, k_c, E_b,
E_h, σ_h, taper and tail parameters ride the vertex (96 B x 4 x 512 = 192 KB, about 50 µs CPU [I]); the orb's noise sits
behind a per-quad `if` (ps_3_0 dynamic branch, coherent per quad). Halo and tail are not stacked quads: one quad per bolt
covers the union (streak + tail + head + 2.5 σ_h + rim), and the pixel program shades by analytic distance to the segment.

**Vertex law.** Inputs P_head, P_tail (= head − v̂ min(L + L_t, v age)), P_prev (the previous Present's head), w, this
frame's and the previous frame's view-projection (c0–c3 shadowed [S], c4–c7 the previous). (1) Clip the world segment
P_back … P_head to the near plane (parametric t where w_clip = near) before projecting: the first degenerate case, a bolt
passing the camera. (2) Project; the screen segment is from the farthest back of (P_tail, P_prev capped at the streak cap)
to P_head; per-vertex widths in px from each end's own depth (w_px = F w / z, floored), interpolated along s, so a 70 m
IRE bolt is thinner at its tail. (3) If the screen length < 1 px (end-on or stationary: the second degenerate case), the
quad is a square of half-side w_px / 2 + 2.5 σ_px around P_head and the pixel program measures d from a point. (4)
Extrude ± (w_px / 2 + 2.5 σ_px) across and along the ends; depth per vertex = its end's depth. Pixel program: d, s, ρ, the
law of 2.1 with h(s), the slug tail when s < s_tail, the orb's chord and noise; `vis` by the hardware test only.

**TAA.** The streak covers the previous position up to SETA 10 (2.3), so the resolve's clip box sees the bolt where the
history has it: no scar; beyond the cap the neighbourhood clamp fades the uncovered image (gate 7.14 at 200 px per frame).
RT2.g flagged pixels keep ≥ 0.9 through the resolve and write-back [M under Wine, 0.99; native unverified, unknown 15 of
weapon-look.md]; cores at E ≥ 1.2 survive (≥ 0.95 at 0 / 4 / 8 px per frame [M, motes]).

**Cost.** CPU: the tap 10 µs, cull + two VBs 60 µs [I]; GPU ≤ 250 µs at the section-4 bound, typically 20–40 µs; the
program about 200 slots (the orb branch about 120 more) [I], validated by the native D3D9 validator harness like every
generated ps_3_0. Native Windows: vs_3_0 / ps_3_0, documented blend, depth and colour-write states, no texture, no Wine
dependency; cross-compiled and fixture-verified, not flown by an agent.

## 6. The lab (question 5)

Presets reuse 3.3's list: `bolt/ire-chase-ahead-600m`, `ire-mayhem-slug-vs-tracer`, `pac-cockpit-own`,
`ppc-slug-capital-broadside`, `psp-orb`, `psg-wave-200m`, `own-fire-chase`, `incoming-cockpit`, `over-station-hull-20km`,
`seta6-stream`; `muzzle/ire-own-chase`, `pac-own-cockpit`, `ppc-capital-broadside-600m`, `flak-alpha-turret`,
`mayhem-white-light-texture-tint`, `seta6-10hz`. Added [C]: `bolt/seta10-stream` (the user's SETA, 10 shots per game
second at 1,500 m/s), `bolt/mayhem-ppc-13km` (the reach factor at the floor), `bolt/gke-lance` (the 29 m width from a
fighter, the judgement of section 8), `bolt/end-on-growing` (an incoming IRE from 1,400 m to 0 in the cockpit),
`muzzle/capital-broadside-cluster` (20 PPC slots at 60 m spacing, the s(N) rule), `muzzle/ballistic-puff-chase` (the
multiply puff over the own nose). Through the generated header, by name: `bolt::w0_m` 1.2, `range_ref_m` 2000,
`range_cap` 8, `len_s` 0.06, `tail_s` 0.08, per style `len_min_w` / `len_max_w` / `head_w` / `taper` / `k_c` / `e_body` /
`halo_sigma_w` / `e_halo` / `tail_min_w` / `tail_max_w` / `streak_cap`, `rim_cool` 0.4, `rim_dark` 0.3, `rim_in_w` 1.0,
`rim_out_w` 2.5, `halo_px_max` 40, `orb_octave_freq[2]`, `boil_hz` 3, `wave_sagitta` 0.25, `end_ms` 60, the three floors,
`cockpit_native` false; `muzzle::range_cap` 3, per class `disc_r_w` / `cone_w` / `cone_half_deg` / `star_w` /
`star_arm_len[5]` / `life_ms` / `rise_ms` / `glow_r_w` / `glow_e` / `puff_r_w` / `puff_r1_w` / `puff_ms` / `puff_s0` /
`vent_w`, `seed_breathe` 0.3, `cluster_n0` 2.5, `cluster_r_x` 2.0; shared `look::core_e`, `halo_max`, `streak_core_min`,
`own_cockpit_gain`, `min_frames`. The three scenarios that prove the look: **`bolt/own-fire-chase`** (two wing IRE guns
from the F3 camera at 4 shots/s: the capsule grows from the muzzle, the flame and the nose glow, bolts flying away end-on
into dots), **`bolt/incoming-cockpit`** (a PAC stream end-on from 1,900 m: dots with halos growing to discs, the rim over
the cockpit frame, no wall of light), **`bolt/over-station-hull-20km`** (an IRE stream across a lit station at 20 km: the
core, rim and far-flag composite). Each at SETA 1, 6 and 10.

## 7. Fixture gates (question 6; dump cases at 1080p through `stage_dump`, `measure.py`'s method, the bolt program
executed in the fixture's FP16 target with the lane) [C]

| # | Gate | Case | Must hold |
| --- | --- | --- | --- |
| 1 | `bolt_width_law` | IRE, PAC, PPC, Mayhem APC / PPC side-on at z_med | w_px within 10 % of section 1's table (1080: x 0.75); the floor not engaged except Mayhem PPC |
| 2 | `bolt_length_law` | same | head length within 10 % of L; the slug tail within 10 % of L_t; HEPT at exactly 12 w |
| 3 | `bolt_radial_profile` | IRE side-on at 300 m | across the axis: peak ≥ 3.5 (core), at ρ 1 within 0.5–0.7 of the body level (the rim cooling), halo at d = w/2 + σ_h ≈ 0.6 x e^(−0.5) ± 20 %, 10 % footprint 2.15 σ ± 15 % |
| 4 | `bolt_rim_darkens_hull` | IRE over a 0.8 grey hull, side-on | min pixel in 1.0–2.5 w off the axis ≤ 0.8 x 0.72; sky unchanged; none when w_px < 2 |
| 5 | `bolt_style_shapes` | tracer / slug / orb / wave | second-moment aspect: tracer ≥ 8, slug head 3–5 with a tail ≥ 2 x head, orb 1.0–1.4 (elongation 1.3), wave: lit extent across travel ≥ 4 x along |
| 6 | `bolt_slug_tail_cools` | EBC at 400 m | nearest stop of the mean colour: head white (min channel ≥ 0.8 of max), tail end tint x 0.4 ± 0.1; E monotone along the tail |
| 7 | `bolt_orb_breathes` | PSP at 2 km, frames 0–333 ms | centre k_c amplitude 0.5–0.9 over one `boil_hz` period; lit disc std of the R/4 high-pass over peak ≥ 0.08 |
| 8 | `bolt_streak_energy` | IRE at 4, 8, 32, 200 px per frame | 0 dark pixels between previous and current positions up to the cap; summed radiance within 10 % of the rest frame; core ≥ 1.2; far-end radiance ≤ 0.25 of the head (no stick) |
| 9 | `bolt_seta_cap` | IRE at SETA 25 (6.95 L) | streak length = 6 L ± 5 %; orb and wave at SETA 10 exactly 1 L |
| 10 | `bolt_end_on_disc` | IRE incoming at 1,400 / 400 / 50 m | a disc (aspect ≤ 1.2); core diameter ≥ 3 px (1080) / 4 px (1440); halo 10 % diameter ≥ 13 / 17 px; grows monotonically |
| 11 | `bolt_birth_and_death` | IRE frames at 10, 30, 60 ms; expiry; a hit | length = v age until L; expiry fade linear over 60 ms (≥ 3 frames); a hit frame: 0 lit pixels beyond the hull depth the next frame |
| 12 | `bolt_stream_no_strobe` | 10 shots per game second at SETA 1 and 10, 2 s | every listed record drawn every frame; no record's lit count falls to 0 before its end; PBE trimmed to 0.8 v / f |
| 13 | `bolt_halo_px_cap` | IRE passing 5 m from the camera | σ_px ≤ 40 (1440) / 30; E_h unchanged |
| 14 | `bolt_no_scar_8_frames` | IRE at 200 px per frame, orb at SETA 10, through `TemporalPass` | ≤ 1 code residual at the previous position after 8 frames |
| 15 | `bolt_resolve_core_survival`, `bolt_far_hull_composite_flagged` | as 3.3 | ≥ 0.95 at 0 / 4 / 8 px; ≥ 0.9 over the far-band hull on the native host |
| 16 | `bolt_two_draws_state_restored` | the in-place draw | rim draw (ZERO / INVSRCCOLOR, colour write RT0 only) then capsule (ONE/ONE, oC2.g > 1 on bolt pixels only), every state restored, other bullet draws suppressed |
| 17 | `muzzle_class_shapes` | each class on a fighter at 30 m | energy: cone aspect ≥ 3 along the axis, disc diameter 3 w_m ± 10 %; ballistic: 5 lobes in the angular lit profile; heavy: cone 12 w_m, rise over 40 ms; flak star; beam: disc only |
| 18 | `muzzle_relight_resets_not_adds` | two shots 30 ms apart | peak = single ± 2 %; cone length differs between the two (seed) |
| 19 | `muzzle_no_strobe_seta10` | 10 shots per game second at SETA 10, 1 s | lit area never 0 inside the burst; frame-to-frame area change ≤ 50 %; ≥ 3 frames per flash |
| 20 | `muzzle_glow_cluster` | 20 PPC slots at 60 m spacing | peak summed glow ≤ 2.3 x E_g = 2.8; a single slot within 1 % of the rule off |
| 21 | `muzzle_puff_hull_only` | ballistic at 150 / 300 ms over a 0.5 hull and sky | min pixel ≤ 0.5 (1 − 0.6 x 0.35) at 150 ms; sky unchanged; nothing after 300 ms; none in the cockpit case |
| 22 | `muzzle_cockpit_gain`, `muzzle_at_recorded_gun_pos`, `muzzle_streak_along_orientation`, `muzzle_light_on_own_hull_not_sky` | as 3.3 | 0.35; centroid within 0.25 R on a moving, rotating ship; ≤ 2°; glow on lane surface only |
| 23 | budget rows | `bolt_alone_fills_frame` (capsule, orb, rim), `muzzle_*_alone_fills_frame`, `bolt_512_muzzle_20_5120` | weights pinned; ≤ 250 µs GPU, ≤ 100 µs CPU |

Hook and draw contract gates of 3.3 (`bolt_tap_record_per_live_bullet`, the col 19 skip, `bolt_vb_bytes_and_instances`,
`muzzle_stub_forwards_eax_edx`, Reset, fault, timings) stand unchanged.

## 8. Kill criteria, simple modes, the flight (question 7) [C]

| Element | Kill observation in flight | First step (one build) | Simple mode |
| --- | --- | --- | --- |
| bolt width / halo | invisible from the chase camera where the native was visible | `floor_w_px` 4 → 5 at 1440 and E_h x 1.5, once | — |
| bolt width | the GKE / Mayhem capital lance reads absurd next to its ship | `range_cap` 8 → 4 | `range_cap` 1 (k only) |
| streak | bolts read as sticks under SETA | `streak_cap` 6 → 3 | 2, the slug tail off |
| rim | a dark ring visible on bolts over space or a grey halo on the hull | `rim_dark` 0.3 → 0.15 | rim off |
| tail (slug, heavy) | smearing, or a stream merging into a beam | `tail_s` 0.08 → 0.05 | no tails |
| orb | the boil reads as noise or flicker | `boil_hz` 3 → 1.5 | smooth sphere |
| own bolts in the cockpit | walls of light | `halo_px_max` 40 → 24 | `cockpit_native` true (the footprint path) |
| whole family | vanishes over a far station, scars, or still wrong after simple mode | — | the family off: native bullet draw + footprint |
| muzzle flame | blinking lights, not fire | cone x 1.5 and `life_ms` x 1.5 | disc + glow only |
| muzzle in the cockpit | fills the view or strobes | half size, no glow | native muzzle in the cockpit |
| muzzle glow | a capital broadside reads as a lamp | `glow_e` x 0.5 | glows off |
| puff / vents | reads as dirt on the lens or a grey smear | puff off | — |

Simple mode in one line: no tails, no rim, no orb boil, no puffs or vents, streak cap 2, muzzle disc + glow. The flight
(`… ,bolts`, chase and cockpit, own fire and incoming, a stream over a distant station, SETA 1 / 6 / 10) judges: (1) is
every bolt a bright capsule in its weapon's colour, readable at range, end-on, over the station and in space; (2) does the
width look right beside the gun (IRE 1.2 m, PPC 12 m, the Mayhem lances); (3) under SETA 10 does the stream read as fire,
not sticks; (4) is the muzzle fire, with the nose lit, and alive under SETA; (5) is the cockpit free of walls of light;
(6) does the slug tail read as a tracer round and the orb as plasma. Nothing is a user setting; a kill flips the header.

## 9. Unknowns and what settles each

| # | Unknown | Settles it |
| --- | --- | --- |
| 1 | The engagement distance (0.3 / 0.7 x reach assumed) | observe row `bolt_hit_range` p50 / p90 per style and tier; `range_ref` and the factors re-derived once |
| 2 | Col 18's axis for the wave (across or along travel) and col 16/17's unit [weapon-look unknown 12] | observe `bolt_size` rows against the instance extents; the lab preset `psg-wave-200m` |
| 3 | Whether the tap sees each bullet once per Present under SETA (the previous position's frame) | observe `bolt_tap passes=` per Present [weapon-look unknown 2] |
| 4 | A missile launch site (no muzzle for missiles now) and the station / platform fire routine (unknown 13) | one `disassemble` task on class-10 creation and on class 5/6 firing |
| 5 | Cull parity between the tap list and the draw's instances (strobing risk) | the step-4 fixture against a recorded `INSTANCE_BULLETS` draw [unknown 14] |
| 6 | The far flag on native Windows, the program's slot count and the weights of section 4 | the native fixture rows of section 7 |
| 7 | Whether the vertical FOV holds at 32:9 (the 139° horizontal) | one F8 capture's projection matrix row; only the edge stretch depends on it |

## 10. Options considered and why they lose

- **Sizes by "double 3.3"** (w0 1 m, lengths x 2): not grounded; section 1 derives them from reach and the screen and
  lands near it for stock fighters while scaling Mayhem's 2–10x reach, which doubling would not.
- **Width by the mounting tier** (small / medium / big from the catalogue's ship masks) instead of reach: another table
  per view and a joint over names in the catalogue; reach is two columns of the live record.
- **A short film capsule (10–15 w) regardless of speed**: a 12 m IRE bolt at 1,162 m/s is 1/10 of the vanilla length
  and reads as X4's downgrade; 60 ms of flight keeps the game's proportion with a head the film look needs.
- **A ribbon trail (history) for the tail**: X3 bullets fly straight [S]; a history pool, SETA inexactness and the
  Niagara spawn-lag problems [R] for nothing the analytic tail does not give.
- **Stacked quads (core, halo, tail) or a geometry stage**: three draws and three overlapping blends per bolt against one
  analytic quad; the distance-to-segment program handles end-on for free [R].
- **Rim as an additive dark colour or no rim**: additive cannot darken; without it a pale core over a sun-lit hull is a
  line in a line. **Premultiplied alpha for the whole capsule**: the late draw's route is ONE/ONE and the far flag needs
  it; the rim as a separate multiply draw keeps the route.
- **Per-pixel saturation for bolts** (the burst note's rejected option): no overlap case exists (section 4).
- **A flipbook muzzle**: an asset, not exact under SETA, no variety without frames [R]; the seed breathes the analytic one.
- **A 60 ms pre-glow for heavy guns** (the Expanse railgun [R]): the site fires at the shot; nothing precedes it.
- **A 1–2 frame muzzle** (the practitioner rule [R]): invisible at 3–90 shots a minute and under SETA; 80–250 ms with the
  three-frame floor is the fastest that is seen.
- **Dimming own bolts in the cockpit**: costs the visibility the floors exist for; the halo pixel cap removes the wall
  without touching the core, in every view.

## Reproduce

```sh
python3 verification/results/bolt-muzzle-look/size_derivation.py          # section 1's table
# the catalogue columns behind it (bullet, tier, style, m/s, range m, shots/min)
awk -F'|' 'NR>=19 && NR<=66 {print $2,$4,$6,$7,$8,$9}' docs/reference/weapon-catalog.md
# once built: the dump and measurement of section 7 (native fixture, the lock serialises it)
python3 verification/probe/wine_lock.py python3 verification/probe/run_weapon_effects.py --host native --dump verification/results/stage-dump
```
