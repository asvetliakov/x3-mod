# Bolt and muzzle look: sizes, anatomy and laws for projectiles and the muzzle flash

Ratified 2026-11-07 (main session): parallel Fable/Opus design, the Opus note chosen, the Fable note folded in and archived at `docs/archive/bolt-muzzle-look-fable-2026-11-07.md`.

Design note, 2026-11-07 (design agent, Opus 5.5), chosen by the main session over the parallel Fable note
(`bolt-muzzle-look-fable.md`, to be archived), whose stronger points are folded in (section 10 lists them). Amends the look of
[weapon-look.md](weapon-look.md) 3.3; its records, hooks, in-place draw, presets and gates stand unless a line here
replaces them. Reuses [hull-burst-look.md](hull-burst-look.md) (star, eroded noise body, cluster rule, scorch-state smoke).
Implemented 2026-11-07 (unflown): the in-place draw, the muzzle, the fixture gates and the lab; state and the
deviations the gates forced in [weapon-effects.md](../verification/weapon-effects.md), "Bolts and muzzle (step 4)". Inputs: [bolt-muzzle-references.md](../reference/bolt-muzzle-references.md) [R],
[weapon-effect-spawn.md](../reverse-engineering/weapon-effect-spawn.md) §2, 5, 6, 8.2, [weapon-effects.md](weapon-effects.md)
4.1–4.4, [hull-impact-classes.md](hull-impact-classes.md) (class rule),
[game-scale-and-ship-classes.md](../reverse-engineering/game-scale-and-ship-classes.md) §4 (ship sizes),
[field-of-view.md](../reverse-engineering/field-of-view.md) (projection). Size figures:
[`size_derivation.py`](../../verification/results/bolt-muzzle-look/size_derivation.py) →
[`size_derivation_out.txt`](../../verification/results/bolt-muzzle-look/size_derivation_out.txt) over the catalogue.
Marks: [M] measured or read from data, [S] static, [R] reference, [C] chosen, [A] assumed, [I] inferred. w is the bolt
width in metres (section 1); E is linear radiance before the TAA and AgX.

## Outcome

**Bolts are sized for where they are fought, not doubled.** Each weapon's engagement distance follows from who mounts
it (fighter 600 m, corvette 1,500 m, capital 4,000 m, never past 0.75 of its range); its width w is what puts a 3 px body
on a 1440-row screen at that distance, times 1 + 0.6 k: 1.9–3.0 m for fighter guns, 4.7–7.5 m for corvette guns,
12.5–20 m for capital guns. One capsule program draws every bolt in place of the late bullet draw: a white-hot core
inside a tinted body with a cooler, more saturated edge, a tight halo, a ball head, an analytic tail from speed, and a
screen streak to the previous position capped at one rest length whose falling stretch keeps the rest image's total
radiance (so SETA never makes a stick), a length never longer than flown, and a thin multiply rim over lit hulls where it
is wide enough to survive the resolve. Tracer, slug, orb and
wave differ by length, tip, tail and boil; the heavy class adds a fatter white core, a head ball and a long tail. End-on a
bolt is a disc with a halo by construction. The pixel floors (core 2, body 3, halo σ 1.5, length 24 px at 1440 rows)
carry bolts past their engagement distance; own bolts never draw a body wider than 16 px. The muzzle is a per-class
event (energy disc and cone, ballistic star and puff, plasma noise bloom, heavy flash with vents and afterglow, flak star
and puff) sized in w and capped by the shooter's radius, 60–250 ms game with the three-frame floor, re-lit by reset, with
a glow on the shooter's hull, 0.35 in the own cockpit, cluster-saturated on broadsides; station guns get none until
their fire routine is traced, missile launches none until a launch site is claimed. GPU bound for 512 bolts and 20
muzzles at 5120x1440: 100 µs fenced wall clock [M 89.8 µs, 2026-11-07; the first 60 µs came from a 90-slot estimate,
the pixel program has 673 slots]; CPU 150 µs [I]. If the lab or the flight rejects the capital widths, `w_cap_m` and then
the parallel note's reach-scaled width are the fallback (section 8).

## 1. Size derivation

| Input | Value | Mark |
| --- | --- | --- |
| Pixels per metre at 1 m, f = (rows / 2) / tan(vfov / 2) | 960 (1440 rows, vanilla vfov 73.74°), 1,280 (58.72°, the 90°-on-16:9 remap); 720 / 960 at 1080 rows. The width (5120 or 1920) changes only the horizontal FOV (Hor+), not f | [S] field-of-view.md §1, arithmetic |
| Ship radius, median (Mayhem / stock) | M5 10 / 12, M4 11 / 14, M3 15 / 19, M6 90 / 87, M7 560 / 618, M2 1,337 / 1,288, M1 1,170 / 1,064, TL 1,135 / 1,403 m | [M] game-scale §4 |
| Turn radius v / ω at full speed | Mayhem M3 267 m/s / 92.8°/s = 165 m, M5 541 / 147.7 = 210 m; stock M3 66 / 127 = 30 m | [M] game-scale §4, arithmetic |
| Weapon range (speed x lifetime), median by mount tier | stock fighter 2,000 m (p10 977, p90 3,627), corvette 3,676, capital 4,957; Mayhem fighter 3,900, corvette 5,600, capital 18,725 | [M] size_derivation_out |
| Mount tier | the smallest class that mounts the weapon (TShips compatibility in the catalogue): any of M5/M4/M3 → fighter; only M1/M2/M7/TL → capital; else corvette; a bullet no laser fires (fragments) → fighter. 30 / 19 / 12 rows over both views | [M] catalogue `used_by` |
| Engagement distance d_eng | min(600 / 1,500 / 4,000 m, 0.75 range). Fighters: about three turn radii of a Mayhem M3/M5 (500–630 m), 0.30 of stock's and 0.15 of Mayhem's median fighter range (dogfights close inside a fraction of range). Corvettes: between. Capitals: hull-to-hull of two Mayhem M2 is at least 2.7 km; 0.81 of stock's capital median range. The 0.75 range cap keeps short-range capital guns (stock flak, range 2,115 m) at a fighter-fight distance | [A] no engagement-distance census exists (unknown 1) |
| Readability target at d_eng | body 3 px wide at 1440 rows and f 960 (the smallest blob whose interior survives the resolve's 3x3 clamp, bolt-footprint §1); tracer ≥ 36 px long; own muzzle disc ≥ 40 px on a fighter in the chase view (guns about 27 m from the eye: the F3 camera sits 24 m behind the hull centre); an enemy muzzle ≥ the floors at d_eng | [C], [S] bolt-footprint §1, [M] ledger F3 camera |

**Law.** `w = (3 px x d_eng / 960) x (1 + 0.6 k)` m: the mount tier sets the base (1.875 / 4.69 / 12.5 m) and calibre
adds up to 60 % within a tier (Mayhem mounts k 0.3–1.0 guns on fighters, so k alone cannot set the engagement scale).
The generator writes `mount_tier` and `range_m` into the weapon table; the stub reads nothing new.

| Weapon (view, style, tier, k) | w m | core L / tail m | px at d_eng (1440) | px at range end | px passing at 60 m |
| --- | --- | --- | --- | --- | --- |
| IRE (stock, tracer, fighter, 0) | 1.88 | 23.4 / 15.0 | 3.0 x 37.5 | 1.3 x 16.1 (floored) | 30 x 375 |
| PAC (Mayhem, tracer, fighter, 0.48) | 2.42 | 32.6 / 19.3 | 3.9 x 52.2 | 0.6 x 7.7 (floored) | 39 x 522 |
| IRE (Mayhem, slug, fighter, 0.33) | 2.24 | 10.1 / 26.9 | 3.6 x 16.2 + tail | 0.6 x 2.5 (floored) | 36 x 162 |
| GKE (Mayhem, tracer, fighter, 1.0, heavy) | 3.00 | 45.0 / 60.0 | 4.8 x 72 | 0.2 x 2.6 (floored) | 48 x 720 |
| PSG (Mayhem, tracer, corvette, 0.82) | 6.98 | 104.7 / 30.0 | 4.5 x 67 | 1.2 x 17 | 112 x 1,676 |
| FLAK_ALPHA (stock, tracer, capital, 0.83) | 7.43 | 111.5 / 59.5 | 4.5 x 67.5 | 3.4 x 51 | 119 x 1,784 |
| PPC (Mayhem, slug, capital, 0.80) | 18.5 | 92.5 / 105 | 4.4 x 22 + tail | 0.9 x 4.6 | 296 x 1,480 |

All [M] arithmetic from the catalogue (`size_derivation_out.txt`, 61 bolt weapons). At 1080 rows every pixel figure is
x 0.75; at the remapped FOV x 1.33. Against the target: a fighter bolt is 0.16 of a 12 m fighter's length (the film
fighter bolt is about 0.08 [R], the arcade games 0.15–0.2 [R]: prominent, as the user asked of the shields); a capital
bolt is under 1 % of an M2's diameter. Weapon-look 3.3's `0.5 m x (1 + 2 k)` would draw the IRE at 0.8 px at 600 m, so
the floors would be the design; here they are the net past d_eng (at 2.5 d_eng the world size is 1.2 px).

## 2. Bolt anatomy

### 2.1 The shared profile [C]

Per pixel of a bolt (screen quantities from the vertex law of 5.2): `ppm` = f / z_view, evaluated at each end of the
segment and interpolated along it (a long bolt is thinner at its far end); `w_px` = max(w ppm, `floor_body` 3), and for
an own-ship bolt min(…, `own_body_max_px` 16) (12 at 1080); `r` = the pixel's distance to the core polyline (axis
segment ∪ streak segment, 2.3) in px; ρ = r / (w_px / 2), so ρ = 1 is the body's edge.

    core   c = 1 − smoothstep(0.75 c_r, 1.25 c_r, ρ)        c_r 0.4 (heavy 0.55); core radius ≥ 1 px
    body   m = 1 − smoothstep(0.8, 1.0, ρ)
    halo   g = exp(−(ρ w_px / 2)² / (2 σ_px²))              σ = 0.8 w (slug 0.7, heavy 1.0, orb 0.9 w_orb);
                                                              1.5 ≤ σ_px ≤ `halo_px_max` 40 (30 at 1080), every bolt
    rim    multiply by 1 − rim_dark x saturate((ρ − 2.4) / 0.4) x saturate((3.6 − ρ) / 0.4), rim_dark 0.4,
           in a first draw before the additive one (5.1), only while the band (1.2–1.8 w off the axis) is ≥ 3 px wide
    L      = a(s) x fade x [E_c c C_core + E_b max(m − c, 0) C_body + E_h g C_edge]
             E_c 4 (core_e), E_b 1.2 (inside body_max), E_h 0.6 (0.15 of the core, under halo_max 0.2; heavy 0.8)

Colour: t = the classifier tint normalised to max 1; t_c = t² / max(t²) (the same hue, deeper and more saturated: a
cooler edge for warm tints, a deeper blue for blue ones; t_c = t when the tint's saturation is under 0.25);
C_core = lerp(t, white, h), h per style (2.2) and 1 for heavy; C_body = lerp(t, t_c, smoothstep(0.5, 1, ρ)); C_edge = t_c.
So the centre clips white, the body holds the tint at 1.2 where AgX keeps the hue, the rim and halo are the saturated
tint. Axial a(s), s measured from the head backward in w: the **head** is a ball, the core x (1 + 0.5 (1 − saturate(s)))
over the front 1 w; heavy adds a disc of radius 0.6 w at E_c on the head. No star heads (the lens-flare family the user
cut, Run 127/128 [M]). The **tip** is the capsule's round cap, pointed for tracers (radius x sqrt(saturate(−s_f / 1 w))
over the front 1 w). The **tail** (2.2) continues behind the core with a linear radius taper to 0 and radiance
E_b x tail gain x (1 − s_t / T), s_t from the tail root. Under the streak's energy law (2.3) body and halo scale by g_s,
the core by max(g_s E_c, 1.2) / E_c. The halo cap is the rule against walls of light from any bolt in any view (an
enemy fighter bolt 5 m from the cockpit camera would light 360 px); E_h is unchanged by it.

**The rim** [C, from the parallel note]: over a lit hull an additive bolt has no edge; a thin multiply band just outside
the halo's bright part gives one. Over black space it multiplies black and does nothing. It sits at 1.2–1.8 w from the
axis, where the halo has fallen to 0.32–0.08 of its peak, so the net over a 1.0 hull is 0.6 + 0.10 = 0.70 at its centre
against 1.0 beside it: a dark line between the bolt and the hull. Whether it survives the TAA resolve is the same
question as for the core: the resolve's 3x3 clamp keeps a feature whose 3x3 neighbourhood is all inside it (bolt-footprint
§1 [S]), bright or dark alike, so a rim band ≥ 3 px wide survives and a narrower one is erased, exactly as a 1 px core
would be; hence the 3 px condition, and the gate `bolt_rim_survives_resolve` (≥ 0.5 of its darkening after the
production resolve). The rim is a full-mode element behind that gate: if the gate fails, it is off.

### 2.2 Per style [C; style from the classifier 4.1, heavy = class heavy of hull-impact-classes rule 5/7, k ≥ 0.91]

| Style | Core length L | Tip / head | Tail T = min(v τ, T_max) | Halo | h | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| tracer (e ≥ 4) | 12 w x s_v, s_v = 0.75 + 0.5 min(v / 2,000 m/s, 1) | pointed 1 w; ball head; core tapers over its back 2 w into the tail root | τ 15 ms, T_max 8 w, root 0.6 w, tint, gain 0.5 | σ 0.8 w | 0.75 | the film proportion: 9–15 w long [R] |
| slug (1.5 ≤ e < 4) | 4 w x s_v | round; ball head | τ 30 ms, T_max 12 w, thin root 0.35 w, colour white → t (at 0.6 of its length) → 0.4 t at the end (a burning tracer in the weapon's own colour: the weapon owns no heat orange, weapon-look 2.1), gain 0.6 | σ 0.7 w | 0.85 | reads by its tail at range |
| orb (e < 1.5) | a disc of diameter w_orb = 1.6 w | sphere; the whole orb is the head | wisp: τ 10 ms, T_max 4 w, root 0.8 w_orb, broken by x saturate((n − 0.35) / 0.2) | σ 0.9 w_orb, stretched 1.3 : 1 along the travel | 0.5–0.9, breathing with n | boil: body x (0.75 + 0.5 n), n = two octaves of the burst's value noise at 3 and 6.4 cells per w_orb, advected at 3 Hz game; silhouette lobes of hull-burst 2.4 (5 lobes, amp 0.12, 0.5 turn/s); streak cap 0.5 |
| wave (flag `0x8`) | a crescent across the travel: an arc of chord S, sagitta 0.25 S bowing forward, thickness 0.5 w, a sheet behind the arc at E 0.15; S = col 18 in metres once its unit and axis are pinned (stock PSG 1.0, PD 0.5, TERRAN_FRAG 4.0 [M census]; the axis, across or along the travel, is assumed across [A]), until then 6 w | — | none | σ 0.6 w | 0.5 | the arc lies in the plane of the travel and the screen's perpendicular to it, so side-on it is the crescent; foreshortened (c < 0.4) it blends to the ring of diameter S, end-on a glowing ring; no streak (it moves along its normal) |
| heavy (modifies tracer or slug) | as the style | head ball 0.6 w at E_c | τ 40 ms, T_max 20 w, root 0.8 w, tint, gain 0.6 | σ 1.0 w, E_h 0.8 | 1 | c_r 0.55: a fat white core; heavy orbs (PSP) keep the orb law at their w |

### 2.3 Streak and SETA [C]

The tap keys each bolt by (object, creation `ext+8`); when last frame's list held the key, its head is projected through
last frame's view-projection (`camera_reprojection.h` [S]) to `prev_px`. The streak segment runs from the head along
Δ = prev_px − head_px, to min(|Δ|, (1 + c_s) L_px), c_s 1.0 (tracer, slug, heavy), 0.5 (orb), 0 (wave): over its first
L_px it lies on the rest image, the remaining e = max(min(|Δ|, (1 + c_s) L_px) − L_px, 0) is the stretch. The distance
field is the minimum over the axis and the streak segment, so a camera turn smears along the screen motion and a
straight flight along the axis. **Energy is conserved** [from the parallel note]: along the stretch the radiance falls as
(1 − u)², u from 0 where it leaves the rest image to 1 at its far end (never uniform), whose integral is e / 3, and the
whole bolt scales by g_s = L_px / (L_px + e / 3), so the stretched image keeps the rest image's summed radiance and a fast
bolt dims as it lengthens; the core is held at max(g_s E_c, 1.2). **A stick** is a uniform rod: the gate requires the
radiance at the streak's far end ≤ 0.25 of the head's and the head at full. Under SETA 10 a tracer is at most 2 L long
(24 w, still a bolt), and the frame-to-frame jump past the cap is left uncovered: honest motion, no rod.

### 2.4 End-on and the degenerate cases [C]

The projected axis shrinks with c = |b_px − h_px| / ((L + T) ppm) (the sine of the angle to the view ray). The length
floor applies as `floor_len` x c, so an end-on bolt gets none and the distance-to-segment field becomes a distance to a
point: a disc of w_px with its halo and head ball. Minimum end-on footprint at 1440 rows: body 3 px, halo σ 1.5 px, lit
diameter at 10 % of peak ≥ 7 px. Near-zero screen length (|h − b| < 0.5 px) takes u = (1, 0) for the quad. An incoming
bolt grows from that dot to a disc of w_px; nothing special-cases it.

### 2.5 Birth and death [C]

Age a = now_game − `ext+8`. **Birth** [the parallel note's rule]: a bolt is never longer than it has flown: P_back =
P_head − û min(L + T, v a), so it grows out of the muzzle over (L + T) / v (IRE 33 ms) with no birth constant; at age 0
it is the end-on disc of w_px (drawn on its first frame). It is geometry, not a life, so the three-frame floor does not
stretch it; under SETA a bolt is born full, which is right. **Expiry:** x saturate((t_exp − now) / max(60 ms, 3 step_game)) with t_exp = `ext+0`
(one more dword the tap records). **Hit:** the object ends that pass, the bolt ends with it, the impact families take
over in the same frame (their flash covers the cut).

### 2.6 Floors and ceiling [C]

| Floor (render px) | 1440 rows | 1080 rows | Applies |
| --- | --- | --- | --- |
| core diameter | 2 | 2 | always |
| body width | 3 | 3 | always (the TAA survival width, not row-scaled) |
| halo σ | 1.5 | 1.5 | always |
| length (core + tail) | 24 x c | 18 x c | scaled by the foreshortening c |
| own-bolt body ceiling | 16 | 12 | own ship's bolts, both views |
| halo σ ceiling `halo_px_max` | 40 | 30 | every bolt, every view (radiance unchanged) |

Floors raise size, never radiance. The floor is the net past d_eng; the ceiling keeps own fire from building walls in
the cockpit and right behind the chase camera (a fighter bolt reaches 16 px inside 113 m).

### 2.7 A stream, not a strobe [M ratios, C rules]

One gun's bolts never merge: the stream ratio r_s = (L + T / 2) f / v (f shots per second) has median 0.113 on stock,
top five ESR 0.517, MASS 0.444, HEPT 0.299, GAUSS 0.281, PRG 0.250, and median 0.011 on Mayhem, top TBC 0.117, PAA 0.083,
GC 0.078 [M, size_derivation_out]; a stream is a dashed line of separate bolts at equal spacing. **Trim rule** [from the
parallel note, where 60 ms tracers gave PBE 1.15]: when r_s > 0.8 the core is cut to 0.8 v / f − T / 2, so bolts stay
separate. Under this note's lengths no stock or Mayhem weapon reaches it; it binds under the reach-scaled fallback law
of section 8 (MASS 0.821 [M, size_derivation_reach_out]) and on mods with faster guns. Strobing is frame-to-frame luminance flicker: a bolt missing from a frame, a fade-in, or a re-lit muzzle
alternating. The laws exclude each: every live bolt is drawn on every frame from its first, no fade-in, the core
survives the resolve (E ≥ 1.2, 3 px), g_s keeps a bolt's integrated radiance within 10 % from 1x to SETA 10, and the
muzzle's per-shot variation is suppressed when shots arrive faster than three frames (3.2). Under SETA the dash
phase can step by most of a spacing per frame (a crawl); that is motion, and covering it would turn bolts into a beam.

## 3. The muzzle flash

### 3.1 Per class [C; class = hull-impact-classes' rule on the fire record; w as section 1]

Every element is in the shooter's frame (`&pos`, orientation from EDX, re-posed through the owner's rows each frame),
in the stage sprite batch, occlusion class 2. Sizes are capped by the shooter's radius R_s = `obj+0xa4` / 500: core and
star ≤ 0.25 R_s, flame and vents ≤ 1.0 R_s; when a cap shortens the body (the star's arm, the cone, the bloom), the
core shrinks by the same factor, so core / body stays the law's ratio. Life is in game ms; the flash (core, cone or star,
bloom, glow) lives max(min(life, 0.5 x the refire period), 3 step_game) and fades as (1 − x)², x = age / life, with the
±20 % stagger; vents, afterglow and smoke keep their own lives (the first two also capped by the refire period); m
columns are fighter (w 2.2) / capital (w 18.5).

**Rework of 2026-11-08** (section 11 #1, #7, #8, #9, #11; built, see the ledger's "Muzzle rework (2026-11-08)"): every
class draws a small hot white core inside a structured body that carries the size at the core's radiance. Old values in
brackets.

| Class (cue) | Core | Flame / shape | Secondary | Smoke (scorch state, surface only) | Hull glow (2.8 primitive) | Life |
| --- | --- | --- | --- | --- | --- | --- |
| energy (default, ion) | disc 0.25 w, white (h 1), E 4 [0.6 w, h 0.7]; halo 0.4 w at 0.6 | forward cone along the gun axis, length 2 w (4.4 / 37 m), base 0.6 w, radius linear to 0, tint E 4 x (1 − s)^1.5 [E 1.2]; end-on and in the own cockpit the four-arm star of the cone's length | — | — | R 3 w, E 0.6, tint | 120 ms |
| ballistic (ammunition `0x40`) | disc 0.25 w, white [0.5 w, lerp(t, white, 0.7)] | the four-arm star ({1, 0.55, 0.8, 0.45} x 1.2 w, rotated per shot, E 4 [2], each arm 0.3 of its length wide at its root [a fixed 0.12 w]) + cone 1.2 w | — | puff 0.6 → 1.2 w over 400 ms, s0 0.5 [0.3], drifting 0.5 w along the gun axis | R 2.5 w, E 0.6, tint | 60 ms |
| plasma (orb / wave body, before the flak flag [after it]) | disc 0.25 w, white [0.8 w, h 0.6] | a noise bloom: the hull-burst body law (two octaves, erosion, lobes) in the tint, R 0.5 → 1.0 w, E 3 [1.2] | — | — | R 3.5 w, E 0.8, tint | 180 ms |
| heavy (k ≥ 0.91) | disc 0.25 w, white [0.8 w], then afterglow: E ≤ 1.2 fading from white to t to 250 ms | cone 3 w, base 0.8 w, E 4 [1.2]; end-on and in the cockpit the star | two vent jets 1.2 w across the gun axis (the orientation's side row, ±), 150 ms | puff 0.8 → 1.6 w over 600 ms, s0 0.4 | R 4 w, E 1.0, tint | 80 ms flash, 250 ms afterglow |
| flak (bullet flag `0x100` / `0x1000`, not an orb or wave) | disc 0.25 w, white [0.5 w, lerp(t, white, 0.5)] | star 0.8 w, E 4, arms 0.3 wide [2, 0.12 w] | — | puff 0.6 → 1.2 w over 400 ms, s0 0.5 [0.3] | R 2 w, E 0.5, tint | 80 ms |
| missile launch | not built: missiles do not fire through `0x00445170` (only class 7 lasers do [S, spawn §2]); the launch is carried by the missile's own plume until a launch site is traced (unknown 8) | | | | | |

Every star is four cone sprites (kind 4) across the gun axis in the plane of the gun's side and up rows [the energy and
heavy end-on star was one hull-burst star sprite, kind 3, at its 0.10 width]. **Floors** at 1440 rows (scaled by the
rows): the core's diameter ≥ 6 px [3 px], the star's longest arm, the cone and the bloom's diameter ≥ 12 px [none; the
arms' 6 px star floor of the kind-3 star end-on only]. Own fighter in the chase view: the energy core of 0.47 m at 27 m
is 17 px, its end-on star 146 px [disc 46 px, star 100 px] [M, lab detail rows]; an enemy fighter's muzzle at 600 m is a
6 px core in a 12 px star [a 2–3 px disc].

### 3.2 Laws [C]

- **Re-light:** one record per (ship, gun slot k), the shield clock rule: a new shot resets t0 and the seed, never adds.
- **Breathing:** flame and star length x (1 + a (2 h − 1)), h = hash(ship, k, shot count), a = 0.25 x
  saturate(Δt_shot / (3 step_game)). At 1x a 10 Hz gun pulses visibly per shot; when shots arrive faster than three
  frames (SETA 6 at 10 Hz: one per frame) the flame is steady, so nothing alternates frame to frame.
- **View rule (2026-11-07, the chase frames):** a gun axis within 30° of the eye ray (`endon_cos`) collapses the cone
  into its disc, so energy and heavy muzzles draw the four-arm star (since 2026-11-08 four cone arms across the gun axis,
  `star_arms`, A = the cone's length, `star_e`, the cone's life; before, the hull burst's star sprite kind 3) about the
  disc instead; ballistic and flak keep their own star. An own muzzle in the chase view keeps structure; a gun across the view keeps its cone.
- **Own cockpit (2026-11-08 #7):** disc and star only at `own_cockpit_gain` 0.35 (weapon-look 2.9): no cone (energy and
  heavy draw the star of the cone's length), no glow, no vents, no smoke (plasma keeps its bloom as its body); the whole
  muzzle scaled so its longest arm (the bloom's radius) is at most 48 px at 1440 rows (`cockpit_body_px`) [flash, flame
  and glow x 0.35, the cone a 700 px streak and the glow colouring the view].
- **A stream, not a steady light (2026-11-08 #1):** the flash lives at most 0.5 of the refire period (the record's
  t0 − prev_t0, game ms) above the three-frame floor, under the (1 − x)² envelope; a 19 shots/s gun at 1x (period 52 ms,
  life 48 ms by the floor) is at half its fresh peak on about a quarter of the frames [linear fade over 120 ms: lit on
  every frame, IRE 0.80 to PBE 1.0 of the time].
- **Heavy rise:** the heavy cone grows to its length over its first 40 ms game (three-frame floor); there is no
  pre-glow, since nothing precedes the shot at the site.
- **Capital broadside:** per shooter, a muzzle's glow and halo take s(N) = 1 / (1 + (N − 1) / 2.5), N = the shooter's
  live muzzles within 2 R_glow of it (hull-burst 3's rule and N0); cores and flames are untouched, so 12 turrets read
  as 12 guns and their light sums to at most 12 / 5.4 = 2.2 glows.
- **Station and platform guns:** no muzzle until their fire routine is traced (weapon-look unknown 13); they keep bolts.
- A live beam's refresh does not respawn the muzzle [I, spawn §2]; beams are step 5.

## 4. Light, overlap and budget

**Bolts light nothing and have no cluster rule [C].** The engine's bullet light node (cols 13–15) stays native
(weapon-look 2.8). The burst's cluster rule answers many records about one point on one owner; bolts are separate
positions in flight, one gun's never overlap (2.7), the core clips white by design, and dimming a stream to bound its
overlap would cost exactly the readability the floors buy. A per-pixel cap needs a second target in the in-place draw.
Overlap is therefore bounded by count (512) and checked by a gate (convergent barrage, section 6); its kill step adds the
cluster rule to halos only. **Muzzles** use the cluster rule of 3.2.

**Budget [I until the weight rows].** Bolts are never shed (weapon-look 2.11); past 512 the tap list drops the farthest
non-own bolts first (floored dots). Muzzle shed order inside the shared budget: (1) other owners' muzzle smoke, (2)
other owners' plasma blooms to their disc, (3) other owners' glows past four per owner, (4) other owners' bolt rims;
never the own ship. Weights to pin: `bolt_alone_fills_frame` (capsule per kind, rim), `muzzle_alone_fills_frame`
(disc + cone, star, bloom), smoke as the burst's. **Frame bound at 5120x1440:** 512 bolts, 32 near (≤ 200 m, ≤ 60 x 400 px quads) and 480 floored (about
15 x 40 px) cover about 1.06 Mpx = 0.14 screens; at about 90 PS slots and 1 µs per slot per screen (AGENTS budget) that
is 13 µs, plus the VS; 20 muzzles (one own in chase about 0.1 Mpx, 19 small, their glows at the measured 0.40 weight)
under 10 µs; the rim draw (about 20 slots, where its band is ≥ 3 px: near bolts only) under 5 µs. Bound: **100 µs GPU**
fenced wall clock for 512 bolts + 20 muzzles (measured 89.8 µs fenced, 43.9 µs by timestamps, 2026-11-07 [M]: the 60 µs
first set here came from the 90-slot estimate above, while the pixel program has 673 slots); CPU (tap, keys,
projection of prev, VB 128 KB) **150 µs**.

## 5. Drawing

### 5.1 The in-place draw stands [C]

The ratified rule holds: at the frame's late bullet draw our VB, VS and PS replace the game's vertices; every other bullet
draw is suppressed; the route's ONE/ONE, LESSEQUAL with Z write off, oC0, oC1 = oC0 and oC2 = 32 max(rgb) (the B' far
flag) are kept and restored after. **Two passes over the same VB** [the parallel note's amendment]: first the rim in the
scorch's ZERO / INVSRCCOLOR state with `D3DRS_COLORWRITEENABLE1` and `2` = 0 (RT2's flag lane must not be multiplied),
then the additive capsule; 3.3's "one draw a frame" gate becomes "rim, then capsule". One dynamic VB (DISCARD), a static
index buffer of 512 quads, **one capsule program**
with an analytic distance field and up to three draws by kind (line: tracer/slug/heavy; orb; wave) with the kind as a
uniform bool, so no pixel pays another kind's branch. Vertex 64 B: head (world xyz, age), axis (unit xyz, L + T),
prev (NDC xy, valid, seed), params (w, T, fade, packed tint/style/own); 4 per bolt, 128 KB per frame at 512.

### 5.2 The velocity-stretch vertex law [C]

1. World segment: head P_h (node `+0x30`), back P_b = P_h − û min(L + T, v a) (2.5); û = normalize(node `+0x100`) (direction only:
   its unit is unknown 1 of weapon-look, the direction is not), else the node rotation's forward row (unknown 2).
2. **Degenerate case A, the eye plane:** clip P_b towards P_h to z_view ≥ z_near + 0.5 m; a head behind it is culled on
   the CPU (an incoming bolt that passed the eye, an own bolt not yet in front of the cockpit).
3. Project both; ppm per end, w_px and σ_px per end with the floors, the own ceiling and the halo cap, interpolated along
   the segment; apply the length floor x c along the
   projected axis; streak end from prev (2.3).
4. **Degenerate case B, end-on or still** (|h − b| and |Δ| under 0.5 px, or v = 0 for an area shell): u = (1, 0) and a
   square quad; the field is a disc.
5. Quad: along u = normalize(head − the farther of back and streak end), from −R to that length + R across ±(R + the
   streak's perpendicular extent), R = w_px / 2 + 3 σ_px; front vertices take the head's depth, back ones the back's.
   The segment ends in px reach the PS as constant interpolants; the PS uses VPOS.

### 5.3 TAA, depth and the far flag

Bolts are current-only pixels (no motion write); at ≤ the streak cap the streak covers the previous position, so the
3x3 clamp sees the bolt in both frames; past the cap the old position's history clamps to its dark neighbourhood within
one frame [S, bolt-footprint §1]: no scar. Z write off: nothing occludes later draws. Over far stations the B' flag
composites the bolt after the resolve (built under Wine 2026-09-26, native unverified: weapon-look unknown 15).
Readability over bright hulls comes from E_c 4 against lit hulls of about 0.2–1.5 linear (3 : 1 or more), the
saturated edge colour and the multiply rim of 2.1 where its band is wide enough to survive the resolve.

**Cost and native Windows.** CPU per bolt: the tap's record (≈ 20 ns [I]), one key lookup in last frame's list (open-addressed, 1,024 slots), two
projections, 256 B of VB: about 0.2 µs, 100 µs at 512 [I]. GPU: section 4's bound. Programs: VS about 120 slots, PS about 90 for
the line kind and 140 for the orb (two octaves) [I]; both pass the native D3D9 validator harness
(`verification/results/hull-widen-native-validator/`). Documented D3D9 only (vs_3_0 / ps_3_0, VPOS, MRT with independent
write masks as the route already requires); no texture, no Wine dependency. Cross-compiled and fixture-verified; native
behaviour of the far flag is the open item.

## 6. Lab

The 3.3 preset list stands; add `bolt/far-10km-floor` (IRE and PPC at 10 km, both FOVs), `bolt/convergent-barrage`
(eight capital guns on one fighter at 2 km), `bolt/mayhem-gke-fighter-heavy`, `bolt/psg-wave-endon`,
`muzzle/md-ballistic-chase` (Mayhem MD, star and puff), `muzzle/gke-heavy-chase`, `muzzle/capital-broadside-12`. The
generated header injects every `bolt::*` and `muzzle::*` constant (section 8) and the table's `mount_tier`, `range_m`;
the lab computes d_eng and w with the same formulas (`renderBolt`, `renderMuzzle`), emulates f for 1440 / 1080 rows and
both FOVs, and draws the floors at canvas scale. **The three scenarios that prove the look:**

1. **Own fire from chase** (`bolt/own-fire-chase`): stock IRE and Mayhem PAC, two wing guns at 4 shots/s, the F3
   camera, 1.5 s: the muzzle disc and cone ≥ 40 px, bolts leaving under the 16 px ceiling, readable as dashes to 1 km.
2. **Incoming end-on in the cockpit** (`bolt/incoming-cockpit`): an enemy PAC at 600 m firing at the eye, then at
   50 m: a disc with a halo growing to a fat capsule, never a smear; the hit families take over.
3. **A stream over a station at range** (`bolt/stream-over-station-3km`): a corvette's PSG stream at 10 shots per game
   second crossing a 1.0-linear lit station hull at 3 km, at 1x and SETA 6: separate dashes, core ≥ 3 : 1 over the
   hull, no flicker, no stick.

## 7. Fixture gates

On 1080p dumps of the in-place draw through the production bolt program into the FP16 scene (the `stage_dump` writer;
`measure.py`'s 10 %-of-own-peak method). Muzzles are stage sprites and dump as today.

| Gate | Case | Bound |
| --- | --- | --- |
| `bolt_width_law`, `bolt_length_law` | IRE, PAC, PPC, Mayhem IRE / PPC side-on at their d_eng, 1440 and 1080 | lit body width (10 % of core peak, perpendicular profile) = w ppm ± 0.5 px or the floor; core length within 10 % of L, tail within 10 % of T; the generated table's `mount_tier` per row = the catalogue's |
| `bolt_radial_profile` | IRE side-on at 300 m | across the axis: peak ≥ 3.5; at ρ 0.9 within 0.4–0.7 of the body level; halo at d = w/2 + σ within 20 % of E_h e^(−(w/2 + σ)² / 2σ²); 10 % footprint within 15 % of the law |
| `bolt_rim_darkens_hull`, `bolt_rim_survives_resolve` | IRE at 150 m side-on over a 1.0 grey hull and over sky; then through the production resolve, 8 frames | min in the 1.2–1.8 w band ≤ 0.75 of the hull before the resolve and ≥ 0.5 of that darkening after it; sky unchanged; no rim where the band is under 3 px (else the rim is off) |
| `bolt_halo_px_cap` | an enemy IRE passing 5 m from the cockpit camera | σ_px ≤ 40 (30 at 1080); E_h unchanged |
| `bolt_orb_breathes` | PSP and PBE at 2 km, 0–333 ms | centre h amplitude 0.5–0.9 over one boil period; lit disc std of the R/4 high-pass over peak ≥ 0.08; max / min outer radius over 36 bins ≥ 1.1 |
| `bolt_wave_crescent` | PSG side-on, 45° and end-on | side-on: an arc, chord S ± 10 %, sagitta 0.25 S ± 20 % bowing forward; end-on: a ring of diameter S ± 10 % (second-moment ratio ≤ 1.2) |
| `bolt_floors` | the same at 10 km, end-on and side-on | core ≥ 2 px, body ≥ 3 px, lit length ≥ 24 x c (18 x c at 1080) px; halo σ fitted ≥ 1.5 px |
| `bolt_own_ceiling` | own IRE at 20, 60, 200 m, cockpit and chase | body ≤ 16 px (12 at 1080); other ships' bolt at 20 m is not capped |
| `bolt_style_shapes` | tracer / slug / orb / wave / heavy side-on | lit length / width: tracer 9–15 + tail, slug 3–5 + tail, orb 1.0–1.4, wave ring diameter / thickness ≥ 8; heavy core share ≥ 1.3 x tracer's |
| `bolt_colour_core_rim` | each style, tint orange and blue | centre within 0.1 of lerp(t, 1, h); at 0.85 ρ hue = t's ± 10°, saturation ≥ t's; nothing white beyond ρ 0.6; halo peak / core ≤ 0.2 |
| `bolt_tail_law` | IRE, Mayhem IRE slug, GKE at their v | lit tail length = min(v τ, T_max w) ± 1 px; monotone fall; slug tail: min channel ≥ 0.8 of max in its first 10 %, mean colour within 0.1 of 0.4 t at its end |
| `bolt_streak_covers_previous_position` | 4, 8 px and L_px per frame | 0 pixels under 10 % of peak between the two head positions |
| `bolt_seta_cap_and_energy` | IRE side-on at 1x, SETA 6, 10; orb and wave at SETA 10 | lit length ≤ (1 + c_s) L_px + T_px (SETA 10: 2 L ± 5 %); summed radiance within ± 10 % of the rest frame; core peak ≥ 1.2; far-end radiance ≤ 0.25 of the head, head at full (no stick); orb ≤ 1.5 L, wave unstretched |
| `bolt_endon_disc`, `bolt_near_plane_clip` | axis on the view ray at 10 km and 600 m; a bolt crossing z_near | second-moment ratio ≤ 1.2, 10 % diameter ≥ 7 px (1440); no inverted pixel, 0 bolt pixels once it passed |
| `bolt_birth_and_death` | IRE at ages 0, 10, 20, 33 ms; expiry at 1x and SETA 6; a hull hit | lit length = min(L + T, v a) ± 1 px, a disc at age 0, nothing behind the muzzle; expiry: lit every frame to t_exp, peak linear over max(60 ms, 3 steps); a hit: 0 bolt pixels the next frame |
| `bolt_stream_no_strobe` | PSG 10 Hz over the station, 1x and SETA 6 and 10, 32 frames | per-frame lit-sum coefficient of variation ≤ 0.10; every tracked bolt on every frame of its life; a synthetic gun at r_s 1.2 trimmed to r_s 0.8 ± 0.02 |
| `bolt_no_scar_8_frames`, `bolt_two_draws_state_restored` | IRE at 200 px per frame and an orb at SETA 10 through `TemporalPass`; the in-place draw | ≤ 1 code residual at the previous position after 8 frames; rim (ZERO / INVSRCCOLOR, RT0 only) then capsule (ONE/ONE, oC2.g > 1 on bolt pixels only), every state restored, other bullet draws suppressed |
| `bolt_over_bright_hull_contrast` | the stream over a 1.0 hull, through the production resolve | core / hull ≥ 3 : 1 after the resolve |
| `bolt_convergent_barrage` | eight capital guns on one point at 2 km | peak ≤ 8 linear before AgX (else the halo cluster kill step) |
| weapon-look 3.3's bolt rows | resolve survival, far flag, tap, col 19 skip, VB, cockpit gate | as 3.3 (≥ 0.95 at 0 / 4 / 8 px; ≤ 1 code after 8 frames; ≥ 0.9 over a far hull) |
| `muzzle_size_per_class` | each class on a fighter (F3 camera) and a capital at 600 m | disc / star / cone extents within 0.5 px or 3 % of the table (the core scaled with a capped body; floor 6 px at 1440 rows [3 px]); ≤ the R_s caps; each own-star arm's lit root width (0.83 of its base) ≥ 0.15 of its length (2026-11-08) |
| `muzzle_life_min_frames` | each class at 1x, SETA 6 and 10 | lit on max(life, 3 frames) frames; heavy afterglow ≤ 1.2 after 80 ms |
| `muzzle_relight_resets_not_adds` | two shots 30 ms apart, the core alone (2026-11-08: the star's rotation and breathing change per shot) | peak = a single shot's (± 1 %) |
| `muzzle_breathing_suppressed` | 10 Hz at 1x and SETA 6 | 1x: flame length varies per shot within ± 25 %; SETA 6: frame-to-frame flame length change ≤ 2 % |
| `muzzle_broadside_cluster` | 12 turrets along a capital hull | summed glow peak ≤ 2.3 x one glow; core count 12 |
| `muzzle_smoke_surface_only` | ballistic over hull and sky; the own cockpit; (2026-11-08) the hull surface through the gun at 150 ms | sky unchanged; hull min ≥ 1 − s0 − 0.02 (s0 0.5 [0.3]); on the gun's surface ≤ 1 − 0.3 s0 (darker than the hull) [any darkening]; no puff in the cockpit view |
| `muzzle_not_a_lamp` (2026-11-08 #1) | own chase IRE and PAC, the wing gun 27 m from the eye, 20 ms after a shot, over black | of the lit area (≥ 10 % of its peak, max channel) the share ≥ 0.8 of the peak ≤ 0.15; the disc alone printed as the control |
| `muzzle_stream_not_steady` (#1) | stock PBE (energy, 19.23 shots/s) in the own chase view, 1x, one game second of 16 ms frames | frames at ≥ half a fresh shot's peak ≤ 0.6 of the frames |
| `muzzle_floors_at_range` (#8) | an enemy fighter's IRE at 600 m, 1440 rows, end-on and across the view | core diameter ≥ 6 px, longest star arm ≥ 12 px, cone ≥ 12 px |
| `muzzle_cockpit_disc_and_star` (#7) | own cockpit, IRE, MD, GKE, the gun 3 m beside and 2 m ahead of a 6 m eye | one disc, four arms, no cone, glow, vents or puff; longest arm ≤ 48 px at 1440 rows (36 at 1080) |
| `muzzle_class_orb_before_flak` (#11) | the stock PSP orb (flags 0x2100), the PSG wave, FLAK_ALPHA | plasma, plasma, flak |
| `muzzle_heavy_rise_and_vents` | heavy on a capital at 600 m | cone reaches 3 w at 40 ms (± 1 frame); two vents ⊥ the gun axis within 5°; afterglow fades to t, ≤ 1.2 after 80 ms |
| 3.3's muzzle rows (cockpit gain, placement, light on hull, stub records, 20 guns ≤ 30 µs); budget rows | `bolt_alone_fills_frame`, `muzzle_alone_fills_frame`, 512 bolts + 20 muzzles at 5120x1440 | weights pinned; ≤ 100 µs GPU fenced (measured 89.8); CPU ≤ 150 µs (i686 fixture) |

## 8. Header constants, kill criteria and the flight

`bolt::` `mode`, `d_eng_m[3]` {600, 1500, 4000}, `d_eng_range` 0.75, `w_px_target` 3, `w_k` 0.6, `rest_w[4]`
{12, 4, 1.6, 0.5}, `sv_ref_mps` 2000, `tail_tau_ms[5]` {15, 30, 10, 0, 40}, `tail_max_w[5]` {8, 12, 4, 0, 20},
`tail_root_w[5]` {0.6, 0.35, 0.8, 0, 0.8}, `core_r` 0.4 / 0.55, `halo_sigma_w[5]` {0.8, 0.7, 0.9, 0.6, 1.0}, `e_body` 1.2,
`e_halo` 0.6 / 0.8, `h[4]` {0.75, 0.85, 0.7, 0.5}, `head_gain` 0.5, `head_ball_w` 0.6, `streak_c[4]` {1, 1, 0.5, 0},
`end_ms` 60, `wave_span_w` 6, `wave_sagitta` 0.25, `trim_rs` 0.8, `w_cap_m` 20, `rim_dark` 0.4, `rim_band_w` {1.2, 1.8}, `rim_min_px` 3, `halo_px_max` 40, `floor_core_px` 2, `floor_body_px` 3, `floor_halo_px` 1.5, `floor_len_px` 24,
`own_body_max_px` 16, `cockpit_native` false. `muzzle::` `mode`, per class {disc, flame, star, vent, smoke r0/r1/s0/life,
glow R/E, life}, `cap_core_rs` 0.25, `cap_flame_rs` 1.0, `breath` 0.25, `cluster_n0` 2.5, (2026-11-08) `arm_width` 0.3, `star_e` 4, `bloom_e` 3, `refire_share` 0.5, `flash_pow` 2, `floor_disc_px` 6, `floor_arm_px` 12, `cockpit_body_px` 48. Shared: `core_e`, `min_frames`,
`stagger`, `own_cockpit_gain`.

| Element | Kill criterion (observed in flight) | Step (once), then simple mode, then off |
| --- | --- | --- |
| width law | bolts read as bars or beams next to fighters | `w_px_target` 3 → 2.5 |
| capital widths | the 12–20 m capital bolts read absurd beside their ships | `w_cap_m` 20 → 7.5 (no wider than a corvette's top); then the **fallback law**: the parallel note's reach-scaled width w = 1.2 m (1 + 2 k) clamp(reach / 2 km, 1, `range_cap` 8), engagement 0.3 / 0.7 reach, with its own kill step `range_cap` 8 → 4 (`size_derivation.py --mode reach` reproduces it) |
| rim | a dark ring over space or a grey halo on hulls; or `bolt_rim_survives_resolve` fails | `rim_dark` 0.4 → 0.2; then rim off |
| far reading | a bolt invisible at range where native was visible | `floor_body_px` 3 → 4, E_c x 1.5 |
| tail | bolts read as sticks or trails | tails off (simple); streak c_s 0.5 |
| SETA | sticks or strobing under SETA | `streak_c` 0.5; then no streak |
| cockpit | own bolts read as walls; close passes light the view | `own_body_max_px` 16 → 10 and `halo_px_max` 40 → 24; then `cockpit_native` |
| over stations | bolts vanish or wash out over far stations | the far-flag path is broken: family off (native + footprint) |
| barrage | a convergent barrage reads as a white sun | the cluster rule on halos per target point |
| orb | the boil reads as noise or flicker | orb without noise (smooth sphere, lobes off) |
| muzzle | blinking lights rather than fire | flame x 1.5, life x 1.5; strobing or a filled cockpit: half size without glow; then disc only |
| smoke | dirty patches on hulls | muzzle smoke off |

Simple modes: `bolt` = core + body + halo, no tail, no rim, c_s 0.5, orb smooth; `muzzle` = disc + cone, no star, smoke, vents or
bloom. **The flight (`…,bolts` on top of the accepted families) must judge**, in chase and cockpit, own fire and
incoming, a fight around a station, SETA 6: each bolt reads at a glance as a hot pale capsule in its weapon's colour
with a direction; fighter guns look like guns and capital guns like heavy weapons; at the edge of a dogfight every bolt
is still a dash; incoming fire end-on is a growing dot, not a flare; nothing is a stick under SETA; streams are streams;
muzzles read as fire on the gun, not as lamps on the hull; the own cockpit stays clear; no native bolt sprite remains.

## 9. Unknowns and what settles each

| # | Unknown | Settles it |
| --- | --- | --- |
| 1 | The real engagement distances (d_eng is assumed) | the observe flight logs per bolt the eye distance at birth and at its end, by mount tier (`bolt_dist p50/p90`); re-pin `d_eng_m` |
| 2 | Whether node `+0x100`'s direction (or the rotation `+0x40`, which row) is the travel axis | the tap fixture: axis vs the position delta over two passes within 2° |
| 3 | The unit and axis of col 18 (the wave chord, assumed across the travel; stock PSG 1.0, PD 0.5) | the observe `bolt_size` rows against the drawn instance (weapon-look unknown 12), the lab's `psg-wave-200m` |
| 4 | Tap passes per Present under SETA; tap vs draw instance parity; the far flag natively | weapon-look unknowns 2, 14, 15 |
| 5 | The FOV the user flies (pixel sizes x 1.33 under the 16:9 remap) | the device-creation log's projection row; the law is set on the vanilla, smaller case |
| 6 | Which row of the muzzle orientation (EDX, 12 dwords) is the gun axis and which the side (vents) | `muzzle_streak_along_orientation` on a recorded fire record |
| 7 | Mount data for weapons absent from the catalogue (FL, new mods) | the generator falls back to fighter and logs the row |
| 8 | A missile launch site | one `disassemble` task on the missile launch routine; until then no launch flash |

## 10. Options considered and why they lose

- **Doubling 3.3's w** (1.0 m (1 + 2 k)). Not derived: 1 m at 600 m is 1.6 px, so the floor would be the design;
  the derived law puts 3 px at the engagement distance by construction.
- **Size by calibre alone** (w = 1.9 m x (20/3)^k). Mayhem mounts k 0.3–1.0 guns on fighters (GKE, RG, BKE), which
  would fire 12 m bolts in dogfights; the mount sets the scale, calibre modulates.
- **Constant screen size.** Loses the depth cue and makes a 50 m pass equal to a 5 km shot; it is the floor's job only.
- **No rim** (this note's first draft, which argued the resolve erases it). The resolve's history blend treats a dark
  band and the additive core alike: either survives where its 3x3 neighbourhood is inside it and is erased where it is
  1–2 px wide; so the rim is kept, drawn only at ≥ 3 px and gated (2.1). **A premultiplied single-draw rim** (ONE / INVSRCALPHA) would blend the RT2 flag lane by the same factors; per-target
  alpha semantics are unverified natively.
- **Stacked quads** for core, halo and tail: three times the vertices and overdraw, ordering between them, and an end-on
  bolt needs a special case; one distance-field capsule degrades to a disc for free.
- **Star heads** and lens glints on bolts: the lens-flare family the user cut; the ball head carries the front.
- **The shared streak cap of 6** under SETA: 72 w sticks; bolts take their own cap of 1.
- **Tracer every Nth round** (dim slugs between): one gun's stream never merges (ratio ≤ 0.52), so nothing to thin.
- **Heat distortion around orbs**: a colour copy and a pass for one style; later, after the flight.
- **Flipbook muzzles**: an asset and frame variety where a per-shot seed on analytic shapes gives the same.
- **Where the parallel note chose other numbers, this note's stand:** tracer length 60 ms of flight (IRE 70 m, GKE 346 m:
  the vanilla proportion; 12 w x s_v already gives ≥ 36 px at d_eng and keeps the film capsule); streak cap 6 L (24 w is
  the most a tracer may be before it reads as a rod, energy law or not); floors 4 px core / 16 px length (the 3 px body is
  the resolve's survival width, the length floor 24 is weapon-look's amended `floor_len_px`); halo σ 1.5 w (the burst
  lesson: the glow must not outgrow the body; 0.8 w stays inside halo_max); death by absence from the tap list (`ext+0`
  gives the exact expiry for one dword); a seed varying the SETA muzzle up to 50 % frame to frame (that is the alternation
  the strobe rule forbids; per-shot variation stays at 1x); the muzzle disc of radius 1.5 w_m (3.6 m on an IRE fighter, a
  third of its hull: the shooter-radius caps keep it on the gun); 250 µs GPU (the derived areas give 60).
- **Taken from the parallel note:** the energy-conserved, falling streak and the stick definition (2.3), birth never
  longer than flown (2.5), the rim and its two-pass draw (2.1, 5.1), per-end widths and the halo pixel cap (2.1), the trim
  rule (2.7), the wave crescent from col 18 (2.2), no heat colours on weapon-owned elements (the slug tail and the
  ballistic, heavy and flak muzzles take the tint), no puff in the cockpit, the heavy cone's 40 ms rise, and its gate rows
  (radial profile, rim, orb, halo cap, no scar, two draws, heavy rise).
- **Width by reach** (the parallel note's law). One rule from two live columns, but the reach spread is not the
  engagement spread: Mayhem PPC 25 m wide at 1.8 px at its 13 km trading distance, GKE 29 m from a fighter. It is the
  fallback law of section 8, reproducible as `size_derivation.py --mode reach`.

## 11. Visual re-review (2026-11-08)

**Outcome.** Reviewed from 840 lab frames (56 weapons x 15 views, stock AP and Mayhem 3, headless Chromium at the lab's
1:1 canvas), judged from the pictures. The bolt laws hold up side-on at the engagement distance: a tinted capsule with a
white core, a pointed tip and a tail reads as a bolt for every tracer, the slug reads as a comet, the PBE as a needle,
the wave as a crescent, and SETA 6 makes a longer, dimmer bolt rather than a stick. The muzzle does not hold up: on every
weapon it is a flat white ball. Recommended changes in priority order (cost; evidence frame; all frames under
`/tmp/claude-1000/-home-asvetl-x3-mod/d5061608-f60a-47d6-b9b2-30b0b574851f/scratchpad/bolt-muzzle-review/`, regenerable
with the scripts in `verification/results/bolt-muzzle-look/visual-review-2026-11-08/`):

| # | Change | Cost | Evidence |
| --- | --- | --- | --- |
| 1 | **Muzzle core is a lamp.** Every class draws a flat white disc with a soft edge (40–85 px on a fighter in the chase view, 120–213 px for corvette and capital guns) and a hairline star; stock guns re-light it 63–100 % of the time (IRE 0.80, HEPT 0.85, PRG and PBE 1.0 [M, `weapons.csv` lit_fraction]), so the nose carries a steady ping-pong ball, not a flash. Rework the core: a hot core of 0.25 w (h 1) inside a structured body that carries the size (the four-arm star or the plasma bloom at full E, arms ≥ 0.15 of their length wide), and life ≤ 0.5 of the refire period above the three-frame floor. **Built** in `7c13d540d` (2026-11-08): a 0.25 w white core (scaled with a capped body) in the cone, the four-arm star (cone arms 0.3 of their length wide) or the bloom at E 4 / 3, (1 − x)² envelope; `muzzle_not_a_lamp` 0.029, `muzzle_stream_not_steady` 0.238 (3.1, 3.2) | design change (3.1 core law) | `grid_stock_ap_muzzle_chase.png`, `s_IRE__muzzle.png`, `m_GKE__muzzle.png` |
| 2 | **Wave end-on is a neon hoop; own fire is a hoop tunnel.** PSG/PD foreshortened draw a thin bright ring of diameter S with a seam line across it (the edge-on arc), and the chase view's eight live bolts nest into a tunnel. Rework the foreshortened wave as a soft lens: a filled disc whose E falls from a thin rim to 0.15 at the centre, the arc hidden below c 0.4 (no seam), and the ring's screen diameter under the own-bolt ceiling x 6 | design change (2.2 wave kind) | `s_PSG__bolt.png` rows 3–4, `grid_stock_ap_b_in60.png` (PD, PSG) |
| 3 | **Own-fire halo fog.** The 40 px σ cap keeps full radiance, so own bolts near the chase camera (σ_world 100+ px for corvette guns) light a dome over half the frame; fighter bolts carry a 30 px σ blob (HEPT, PAC). Tune: scale E_h by σ_cap / σ_world when the cap binds (energy-conserving cap), and cap σ at the own body ceiling (16 px) for own bolts | small tuning (2.1, 2.6) | `grid_stock_ap_bolt_own.png`, `grid_mayhem3_bolt_own.png` |
| 4 | **Mount tier by the catalogue's 10 % rule.** "Any M3 mounts it → fighter" puts the stock PSP (M3:1 of 12 rows) at w 3.0 m (a 3 px dot at 2 km, 1.5 px long) and the stock PPC (M6:2 of 36) at 7.5 m; the catalogue's `size` column already says big. Tune the generator: `mount_tier` from `size` | small tuning (1) | `grid_stock_ap_bolt_side.png` row 4 (PSP, PPC vs GAUSS) |
| 5 | **Fighter bolts vanish over a lit hull.** At the 3 px floor the rim band (1.2–1.8 w = 0.9 px) is off and the core clips white, so a stream over a 1.0 hull is a row of pale dashes (IRE, PAC, HEPT, PRG, EBC). Tune: the rim band in pixels when w_px < 5 ([w_px/2 + 1, w_px/2 + 4] px, so it is 3 px wide at the floor), and the core's h x saturate((w_px − 3) / 3) so a floor-width bolt keeps its tint | small tuning (2.1, 2.6) | `grid_stock_ap_b_stream.png` rows 1–2, `s_IRE__bolt.png` row 2 |
| 6 | **Heavy head ball is a bead on a stick.** GKE (168 px needle with a bead), Mayhem PSP and stock SSC (a thermometer), GAUSS/IBL (a lollipop). Tune: no separate E_c disc; the head is the core's round cap at c_r 0.55 with the 1.5x front boost only | small tuning (2.1 heavy) | `grid_mayhem3_bolt_side.png` (GKE, PSP), `grid_stock_ap_bolt_side.png` (TERANTIFIGHTER, GAUSS, IBL) |
| 7 | **Cockpit muzzle is a beam and a wash.** The cone from the lab's gun offsets (3 m beside, 2 m ahead of the eye) is a 700 px diagonal streak and the glow (R 3 w at 6 m) colours the whole frame even at gain 0.35. Tune: in the own cockpit no cone (disc and star only) and no glow; the real gun offsets are unknown 9. **Built** in `7c13d540d` (2026-11-08): disc and star only, no glow, vents or smoke, the longest arm ≤ 48 px at 1440 rows; `muzzle_cockpit_disc_and_star` (3.2) | small tuning (3.2) | `grid_stock_ap_m_cockpit.png`, `s_PAC__muzzle.png` row 2 |
| 8 | **An enemy fighter's muzzle at 600 m is invisible.** 2–3 px disc plus a 9 px glow at E 0.6 on a lit hull reads as nothing (IRE, PAC, EBC, MASS, PBE, PRG, every Mayhem fighter gun). Tune: muzzle floors of 6 px for the disc and 12 px for the star arms (the star is what reads at range: PPC, PAA, TBC at 600 m). **Built** in `7c13d540d` (2026-11-08): core 6 px, longest arm, cone and bloom 12 px at 1440 rows; `muzzle_floors_at_range` 6.0 / 12.0 / 12.0 px | small tuning (3.1 floors) | `grid_stock_ap_m_range.png`, `grid_mayhem3_m_range.png` |
| 9 | **Ballistic and flak star and puff do not read.** The star arms are hairlines beside a 40 px disc and the grey puff is invisible on the grey hull (MD, EBC, MASS, FAA, CFA, FLAK). Tune: arm width ≥ 0.15 of length (shared with #1), puff s0 0.3 → 0.5 and darker than the hull. **Built** in `7c13d540d` (2026-11-08): arms 0.3 of their length (lit root 0.25–0.28), s0 0.5, the hull through the gun at 0.73 at 150 ms (`muzzle_smoke_surface_only`) | small tuning (3.1) | `m_MD__muzzle.png`, `s_EBC__muzzle.png`, `s_FLAK_ALPHA__muzzle.png` |
| 10 | **Orb silhouette lobes read as a pentagon decal** near the eye (stock PSP and MAM at 60 m: a five-point blob). Tune: lobe amp 0.12 → 0.05, 7–9 lobes, or noise-only erosion | small tuning (2.2 orb) | `grid_stock_ap_b_in60.png` (PSP, MAM) |
| 11 | **The flak flag out-ranks the orb style**: stock PSP and MAM (area_flak) get the flak star-and-puff muzzle on a singularity gun. Tune: style orb/wave → plasma before the flag test. **Built** in `7c13d540d` (2026-11-08): `muzzle_class_of` tests orb / wave first (a hull pin still wins); `muzzle_class_orb_before_flak` | small tuning (3.1 class rule) | `weapons.csv` mclass |
| 12 | **The lab draws every Mayhem weapon white** (0 of 103 bodies carry a texture `mean` in `assets.js`; production reads the table's texture mean, 4.2, which the lab cannot show), so Mayhem colour and the white stock weapons (FLAK, CIG, MAM, SSC) could not be judged: flak, concussion and anti-matter are identical white rods. Fix the asset extractor's means before the Mayhem colour review | tooling (lab) | `grid_mayhem3_bolt_side.png`, `grid_stock_ap_bolt_side.png` row 5 |

Also found, not ranked: the lab multiplies the rim into the summed bolt light (a dark lattice through dense streams,
`grid_stock_ap_b_stream.png` GAUSS/IBL), which production's rim-then-capsule order (5.1) cannot do: a lab fix. The
rim's dark outline around corvette and capital pills over a bright hull (CIG, FLAK_CLUSTER streams) is the design and
reads as a sticker edge at `rim_dark` 0.4; section 8's kill criterion (0.4 → 0.2) should be applied on the first flight
that shows it, not after.

### 11.1 Method

`batch_shots.py` drives the lab page once per run (view, weapon, tab, scene, distance, angle, SETA, hull, own, shots/s),
draws at a chosen time and screenshots the 720x405 canvas (the centre of a 1440-row screen at 1:1 [S, lab]). Per weapon:
bolt side-on at d_eng (600 / 1,500 / 2,000 m by catalogue size; two times), over a 1.0 lit hull, at SETA 6, own fire
from the chase camera (two times), incoming in the cockpit at 300 m and 60 m, a 10-per-second stream over a lit hull at
1 km; muzzle in the own chase view 20 ms after a shot and half a refire period later, own cockpit, guns across the view,
a capital at 600 m, SETA 6. Shots per second are the catalogue's `shots_per_min` / 60 (stock IRE 6.67, Mayhem IRE 0.5).
Side-on cells in the sheets are 3x centre crops (a 3 px bolt is otherwise unjudgeable); the lab's numbers in each caption
are the lab's `detail` row [M]. Contact sheets: `<tag>__bolt.png` (10 cells) and `<tag>__muzzle.png` (6 cells) per
weapon, tag `s_<ID>` stock, `m_<ID>` Mayhem; cross-weapon grids `grid_<view>_<shot>.png`. Numbers: `weapons.csv`.
Limits: the lab's muzzle scenes mount every weapon on a 12 m fighter (corvette and capital guns are capped by R_s there and
their chase frames are not a real case; the `capital at 600 m` cell is); the own-fire scene fires 4 per second per gun
regardless of the weapon; the lab has no texture means (#12).

### 11.2 Stock AP, weapon by weapon

Columns: (1) does the style, size, colour and tail make sense for the weapon; (2) motion (two times, SETA 6); (3) blend
(space, lit hull, cockpit); (M) the muzzle: class, disc px in the chase view / at 600 m, lit fraction at the fire rate,
what it reads as. Sizes [M, `weapons.csv`]; judgements from the frames.

| Weapon | Style, w, tint | (1) sense | (2) motion | (3) blend | (M) muzzle | Verdict |
| --- | --- | --- | --- | --- | --- | --- |
| IRE | tracer, 1.88 m, blue | A short blue dart for the lightest gun: right. Core white, halo blue: the hue sits only in the halo | 61 px at 600 m, tail fades; SETA 6 a 136 px line at g_s 0.75, still a bolt | over black good; over the lit hull a white dash with no blue; in the chase view 16 x 21 px pills (the bolts fly away end-on) | energy, 40 px / 2 px, lit 0.80: a steady lamp pair with a faint star | tune #1, #5, #8 |
| PAC | tracer, 2.20 m, violet | right: a heavier IRE | 56 px; SETA 6 fine | as IRE; own fire 16 x 47 px capsule with a 21 px σ halo blob | energy, 47 px, lit 0.63: lamp | tune #1, #3, #5 |
| HEPT | tracer, 2.34 m, green | a plasma thrower drawn as a laser tracer with a 6.5 m tail: the vanilla HEPT is a fat green blob; a slug (short core, long tail) would read closer | 49 px; fine | own fire: a 16 x 86 px green bar inside a 30 px σ green cloud (fog) | plasma, 66 px, lit 0.85: a white ball with a faint green bloom, no structure | tune #1, #3; consider style slug |
| EBC | slug, 2.24 m, yellow | a chaingun comet: short white core, long yellow tail. Right | 42 px; the tail reads | fine; own fire a pointed teardrop | ballistic, 40 px / 2 px, lit 0.39: a true flash, but a flat disc; star hairline, puff invisible | keep bolt; tune #9 |
| PBE | tracer, 2.16 m, grey-blue | a 79 px needle at 6,980 m/s, 19 shots/s: right for a pulsed beam | long thin needle; SETA 6 a line | own fire: already 551 m out in 80 ms (a dot) | energy, 46 px, lit 1.0: a permanent lamp at 19/s | keep bolt; #1 is for it |
| MASS | tracer, 1.88 m, white (light #303030 under the saturation test, texture mean unknown in the lab) | a kinetic slug drawn as a bright white laser rod: wrong cue; it should be the dimmest, shortest tracer with no halo | 51 px line | over the hull a white dash | ballistic, 33 px, lit 0.74 | rework: style slug, E_b 0.6, halo off (option, 11.5) |
| PRG | tracer, 2.07 m, yellow | right: a repeater's long yellow dart | 72 px; fine | own fire 14 x 15 px dots (bolts leave fast) | energy, 44 px, lit 1.0: lamp | tune #1 |
| PD (Terran PBG) | wave, 1.95 m, lavender, S 0.5 (6 w until pinned) | a crescent 12 m across: plausible for a burst generator side-on | side-on fine | end-on and own fire: hoops and a tunnel, a seam line | plasma, 56 px, lit 0.16: a lavender fog | rework #2 |
| PSG | wave, 4.22 m (corvette tier), lavender | the shockwave's crescent reads at 2 km side-on | a chain of ")" at 10/s | hoop tunnel in own fire; a 33 px hoop at 60 m | plasma, 120 px, lit 0.23: a big lavender fog | rework #2 |
| PSP (Terran) | orb, 3.0 m (fighter tier: the M3:1 row), yellow | the biggest gun in the game is a 3 px dot at 2 km and a 7 px blob at 600 m: wrong size; the orb itself is right | SETA 6 fine | at 60 m a 77 px pentagon-lobed blob | flak (area_flak), 53 px, lit 0.01 | tune #4, #10, #11 |
| PPC | heavy slug, 7.5 m (corvette tier: the M6:2 rows), yellow | half the size it should be; a fat yellow pill with a white core | 24 px pill at 2 km | at 60 m a 120 px flat white disc | heavy, 213 px (capped by the fighter R_s) / 11 px at 600 m with a yellow fog | tune #4, #6 |
| FLAK_ALPHA | tracer, 7.43 m, white, L 112 m | a flak shell as a 112 m white neon tube: the length comes from 7,051 m/s x 15 ms; the vanilla cue is a fast white streak that bursts, so the length is right, the uniform white rod is not (no tint to shape it) | an 82 px tapered rod; fine | own fire tiny (fast) | flak, 132 px / 6.6 px, lit 0.07: on a capital at 600 m a dot with a faint puff | keep; #12 for colour |
| FLAK_CLUSTER | tracer, 6.86 m, white | identical to FLAK_ALPHA | same | same | flak, 122 px / 6 px | keep; #12 |
| CIG | tracer, 6.68 m, white | a concussion gun as a white rod identical to flak | 48 px | own fire (M6): a 16 x 96 px bar in a white fog dome; the stream over a hull: outlined white pills (sticker edge) | energy, 143 px / 7 px, lit 0.15 | #12, #3; rim 0.4 → 0.2 |
| ESR (ion shard) | tracer, 6.02 m, green | an ion railgun as a green tracer: fine | 45 px | own fire fog dome | energy (hull class ion), 129 px / 6 px, lit 0.46 | tune #3 |
| EMP (Terran EMPC) | slug, 2.34 m, yellow | a comet like EBC: fine | 42 px | fine | plasma, 66 px, lit 0.47: ball | tune #1 |
| MAM (Terran M/AM) | orb, 2.69 m, white | an anti-matter orb as a 7 px white cotton ball | 7 px blob | at 60 m a 69 px lobed pentagon in a white fog | flak (area_flak), 48 px | tune #10, #11, #12 |
| TERANTIFIGHTER (Terran SSC) | heavy slug, 2.91 m, white, T 58 m | a 116 px white rod with a ball head and a long thin tail: a thermometer | same | own fire: a white ball with a bead | plasma, 83 px, lit 0.11 | tune #6, #12 |
| GAUSS | heavy slug, 17.9 m, yellow | a lollipop: fat rod, white ball head, yellow glow blob | 36 px pill at 2 km | at 60 m a 286 px flat white disc | heavy, 25 px at 600 m with a 130 px yellow glow: a soft lamp on the hull | tune #6 |
| IBL | heavy slug, 19.3 m, yellow | as GAUSS (class plasma from the hull pin) | same | same | plasma, 28 px at 600 m | tune #6 |
| IPG | tracer, 6.97 m, green | fine | 48 px | fog dome in own fire | flak (fragments flag), 124 px / 6 px | tune #3, #11 |

### 11.3 Mayhem 3, weapon by weapon

All white in the lab (#12); style, size and tail only. Mayhem guns fire at 0.05–1.5 shots/s, so every muzzle is a true
flash there (lit ≤ 0.18) and the lamp of #1 is a per-shot ball, not a steady one.

| Weapon | Style, w | (1) sense | (2) motion | (3) blend | (M) muzzle | Verdict |
| --- | --- | --- | --- | --- | --- | --- |
| IRE | slug, 2.24 m, T 27 m | a short core and a long tail: a comet, right for Mayhem's IRE | 59 px | own fire 15 x 12 px dots | energy, 48 px / 2.4 px, lit 0.06 | keep; #8 |
| PAC | tracer, 2.42 m | an 83 px dart | fine | own fire 16 x 20 px | energy, 52 px, lit 0.03 | keep; #8 |
| HEPT | orb, 2.59 m | a 7 px cotton ball at 600 m; the boil cannot read under 10 px | blob | at 60 m a 66 px blob | plasma, 74 px: ball | keep; #10 |
| EBC | tracer, 2.25 m | a 77 px dart (Mayhem's EBC is a laser) | fine | fine | energy, 48 px | keep |
| PBE | orb, 2.41 m | a pulsed beam emitter as an orb (the catalogue's style): a 6 px blob | blob | 62 px blob at 60 m | plasma, 68 px | keep (style is the census's) |
| MD | tracer, 2.30 m, ballistic | a mass driver as an 85 px white laser dart: wrong cue (as stock MASS) | fine | fine | ballistic, 41 px, lit 0.02: a flash with hairline spikes, puff invisible | rework as MASS (11.5); #9 |
| PRG | slug, 2.48 m, T 30 m | comet: fine | 65 px | fine | energy, 53 px | keep |
| PBG | tracer, 2.59 m | plasma burst generator as a tracer; the census says so | 89 px | fine | plasma, 74 px | keep |
| PSG | tracer, 6.98 m (corvette), L 105 m | Mayhem's PSG is a tracer, not a wave: a 86 px rod | fine | own fire (M6) a 16 x 87 px bar in fog | plasma, 199 px / 10 px | #3 |
| PSP | heavy slug, 20 m (capital), T 140 m | a thermometer: a thick rod with a ball head and a long faint tail, 115 px at 2 km | SETA 6 a 211 px rod | at 60 m a 320 px white disc | plasma, 28 px at 600 m with a soft fog | tune #6 |
| PPC | slug, 18.5 m, T 105 m | fat rod with a long tail (not heavy: class energy from the hull pin) | 95 px | 296 px disc at 60 m | energy, 20 px at 600 m with a visible four-arm star: reads as a flash | keep muzzle; #6 n/a |
| GKE | heavy tracer, 3.0 m, L 45 + T 60 m | a 168 px needle with a bead for a 94k-damage fighter gun: the length is right (3,500 m/s), the bead is not; a fat white core would carry the weight | SETA 6 a 312 px line, g_s 0.75 | own fire 10 x 9 px dots at 290 m | heavy, 85 px, lit 0.00: a white ball with a hairline star and a white fog; the vents are not visible in the chase view | tune #6, #1 |
| ISR | slug, 6.9 m, T 60 m | an ion shard railgun as a comet with a long tail: fine | 61 px | fog dome in own fire | energy (ion), 147 px / 7 px | #3 |
| FAA | slug, 6.88 m | flak as a comet | 37 px | fog | ballistic, 122 px / 6 px | #9 |
| CFA | tracer, 7.07 m | cluster flak as an 64 px rod | fine | fog | ballistic, 126 px / 6 px | #9 |
| CIG | tracer, 6.94 m, L 104 m | an 86 px rod | fine | fog | energy, 148 px / 7 px | #3 |
| EMPC | tracer, 2.57 m | an 88 px dart | fine | fine | plasma, 73 px | keep |
| SSC | tracer, 6.78 m | an 84 px rod | fine | fog | plasma, 193 px / 10 px | #3 |
| MAML | tracer, 6.25 m | a 79 px rod | fine | fog | energy, 133 px / 7 px | #3 |
| PAA | tracer, 17.7 m, L 266 m | a 146 px fat white rod at 2 km: right for a photon artillery shell | fine | 283 px disc at 60 m; own fire (M2) a 16 x 155 px bar in fog | energy, 19 px at 600 m with a four-arm star: reads | keep |
| GC | tracer, 16.5 m, ballistic | a 144 px rod | fine | fog | ballistic, 15 px at 600 m | keep; #9 |
| IBL | tracer, 19.1 m, L 286 m | a 162 px rod (not heavy here) | fine | fog | plasma, 27 px at 600 m, soft fog | keep |
| APC, AKE | slug, 2.2–2.4 m | comets | 59–63 px | fine | energy, 48–51 px | keep |
| PG | orb, 2.41 m | a 6 px blob | blob | 62 px blob | plasma, 69 px | keep; #10 |
| ID | tracer, 2.59 m | an 89 px dart for the ion disruptor | fine | fine | energy (ion), 55 px | keep |
| GRC | tracer, 6.51 m | an 82 px rod | fine | fog | energy, 139 px / 7 px | #3 |
| FBL | slug, 2.68 m, T 15 m | a fragmentation bomb as a 39 px comet: a slower, fatter orb would cue the bomb | fine | fine | ballistic, 48 px | keep (minor) |
| BKE, RG | orb, 2.9–3.0 m, heavy | 7.5 px blobs with a bright centre for 21k–26k-damage guns: under-sized at 600 m (fighter tier, k 1.0) | blob | 75 px blob at 60 m | heavy, 83–84 px white ball, lit 0.01–0.02 | tune #4 (tier) does not apply; consider orb w_orb 1.6 → 2.2 w for heavy orbs |
| TBC | tracer, 16.5 m, L 247 m | a 144 px rod | fine | fog | energy, 18 px at 600 m with a star: reads | keep |
| FBC | tracer, 6.15 m, blue (#0a0aff, the one saturated Mayhem light) | a blue rod: the only coloured Mayhem bolt; 3 px body at 1.5 km | fine | fine | energy, 131 px / 7 px, blue fog | keep |
| IC | tracer, 6.75 m | a 70 px rod | fine | fog | energy (ion), 144 px / 7 px | #3 |
| PALC | orb, 6.88 m | a 7 px blob at 1.5 km | blob | 176 px blob at 60 m | plasma, 196 px / 10 px | keep; #10 |
| IPG | tracer, 7.03 m | a 75 px rod | fine | fog | energy, 150 px / 7.5 px | #3 |

### 11.4 Cross-weapon findings

- **A lamp**: every muzzle core (the flat white disc, h 0.6–1 at E 4, 0.5–0.8 w) on every class and view; the star and
  cone are hairlines beside it. The one element that reads as a flash is the four-arm star of capital guns at 600 m.
- **A balloon**: own-fire halos under the 40 px cap (fog domes for corvette guns, a 30 px σ cloud on a fighter bolt);
  the end-on heavy at 60 m (a 286–320 px flat disc); orbs within 100 m (a cotton ball, lobed into a pentagon).
- **A stick**: not under SETA 6 (the energy law keeps the far end dim), but the heavy head ball makes a bead on a stick
  (GKE, SSC, PSP, GAUSS, IBL), and the cockpit cone is a 700 px diagonal stick.
- **A disc**: an incoming fighter bolt at 60 m (30–37 px, by construction, acceptable with its halo); the wave end-on is a
  hoop, not a disc, and should become one (#2).
- **A decal**: the rim's dark outline around corvette and capital pills over a bright hull; the orb's five lobes.
- **The same on weapons that should differ**: all white lights (stock FLAK, FLAK_CLUSTER, CIG, MAM, SSC; every Mayhem
  weapon in the lab) draw the same white rod or ball; IRE, PAC, PRG, PBE differ only by hue and length; MASS/MD (kinetic)
  draw as the brightest laser; HEPT (plasma) draws as a laser. The style census is the catalogue's elongation reading, so
  the cues are mechanical; the three mis-cues worth a hand pin are MASS/MD, HEPT and FBL (11.5).
- **Stream or strobe**: stock guns at 5–19 shots/s with 60–180 ms lives are a steady lamp (lit 0.63–1.0), the ballistic
  60 ms is a strobe at 6.6/s (EBC: lit 0.39, which is right for a chaingun); Mayhem guns are single flashes.
- **Blend**: over black every bolt reads; over a 1.0 hull fighter bolts are pale white dashes (#5) and bigger bolts are
  outlined pills; in the cockpit the glow colours the frame (#7).

### 11.5 Options considered for the reworks

- **#1 muzzle core.** (a) Keep the disc, cut E to 1.5 and h to 0.5: still a ball, just a tinted one (the ball is the
  shape, not the brightness). (b) The hull burst's noise body for every class: the plasma bloom at 66 px already reads as
  a ball because the disc sits on top of it at E 4. (c) Chosen: a small hot core (0.25 w) with the star or bloom carrying
  the size at full E, and life capped at 0.5 of the refire period so a flash is a flash; fast guns (PBE 19/s, PRG 10/s)
  then show a steady small ember, which is honest. (d) A flipbook sprite per class: an asset, and the same per-shot seed
  gives variety analytically (section 10 stands).
- **#2 wave end-on.** (a) Keep the ring, thin it to 0.25 w: still hoops, still a tunnel. (b) Draw the wave as a billboard
  disc always (vanilla's cue): loses the side-on crescent that reads well at 2 km. (c) Chosen: the crescent side-on, a
  soft filled lens below c 0.4 with the rim at E 1.2 and the fill at 0.15, no arc below c 0.4 (the seam), and the lens
  diameter capped at 6 x `own_body_max_px` for own bolts so eight live PSG bolts cannot tile the view. (d) No end-on
  draw: the wave moves along its normal, so an end-on wave is the common case for the target; it must draw.
- **MASS/MD, HEPT, FBL cues.** (a) Leave the census (mechanical, documented): the mass driver stays the brightest laser.
  (b) Chosen: three hand pins in `weapon_overrides.json` (MASS/MD: style slug, E_b 0.6, halo off, tint from the texture
  mean; HEPT: style slug with the 30 ms tail; FBL: style orb at w_orb 2.0 w), each a documented `by_id` row, no new law.
- **Own halo cap.** (a) `halo_px_max` 40 → 24 (section 8's kill step): still full radiance in a smaller blob. (b)
  Chosen: energy-conserving cap (E_h x σ_cap / σ_world) plus the own-bolt σ ≤ body ceiling: the fog goes, the halo at
  range is unchanged.

### 11.6 Unknowns this review adds

| # | Unknown | Settles it |
| --- | --- | --- |
| 9 | The own guns' offsets from the cockpit eye and from the F3 camera per ship class (the lab assumes ±3 m, 2 m ahead in the cockpit, 24 m behind for the chase camera) | the observe flight logs the first own muzzle's eye distance and screen position in both views |
| 10 | The production tint of white-light weapons (the table's texture means, 36 of 38 Mayhem rows): whether they are grey (all alike) or carry a hue | dump `tint_rgb` per row from the loaded table (one log row at table load) and feed the means into the lab assets |
| 11 | Whether the muzzle disc at 40 px reads as a lamp after the TAA resolve and bloom in the game (the lab has neither) | the first flight's own-fire frames in the chase view |

Evidence: `verification/results/bolt-muzzle-look/visual-review-2026-11-08/weapons.csv` (56 rows: style, tier, w, L, T,
tint, muzzle class, shots/s, the lab's pixel sizes per view, lit fraction) and its scripts (`make_plan.py`,
`batch_shots.py`, `make_sheets.py`, `make_grid.py`, `summarize.py`); frames and sheets in the scratch directory named
above (regenerable: `python3 batch_shots.py tools/effects/weapon_effects_lab.html <outdir> plan.json`).

## Reproduce

```sh
S=tools/effects/weapon_catalog
python3 verification/results/bolt-muzzle-look/size_derivation.py $S/stock_ap.json $S/mayhem3.json              # section 1, 2.7
python3 verification/results/bolt-muzzle-look/size_derivation.py --mode reach $S/stock_ap.json $S/mayhem3.json # the fallback law
```
