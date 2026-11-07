# Bolt and muzzle look: sizes, anatomy and laws for projectiles and the muzzle flash

Ratified 2026-11-07 (main session): parallel Fable/Opus design, the Opus note chosen, the Fable note folded in and archived at `docs/archive/bolt-muzzle-look-fable-2026-11-07.md`.

Design note, 2026-11-07 (design agent, Opus 5.5), chosen by the main session over the parallel Fable note
(`bolt-muzzle-look-fable.md`, to be archived), whose stronger points are folded in (section 10 lists them). Amends the look of
[weapon-look.md](weapon-look.md) 3.3; its records, hooks, in-place draw, presets and gates stand unless a line here
replaces them. Reuses [hull-burst-look.md](hull-burst-look.md) (star, eroded noise body, cluster rule, scorch-state smoke).
Not implemented. Inputs: [bolt-muzzle-references.md](../reference/bolt-muzzle-references.md) [R],
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
muzzles at 5120x1440: 60 µs [I]; CPU 150 µs [I]. If the lab or the flight rejects the capital widths, `w_cap_m` and then
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
star ≤ 0.25 R_s, flame and vents ≤ 1.0 R_s. Life is in game ms, effective max(life, 3 step_game), linear fade with the
±20 % stagger; m columns are fighter (w 2.2) / capital (w 18.5).

| Class (cue) | Core | Flame / shape | Secondary | Smoke (scorch state, surface only) | Hull glow (2.8 primitive) | Life |
| --- | --- | --- | --- | --- | --- | --- |
| energy (default, ion) | disc 0.6 w (1.3 / 11 m), E 4, h 0.7; halo σ 0.4 w at 0.6 | forward cone along the gun axis, length 2 w (4.4 / 37 m), base 0.6 w, radius linear to 0, tint E 1.2 x (1 − s)^1.5 | — | — | R 3 w, E 0.6, tint | 120 ms |
| ballistic (ammunition `0x40`) | disc 0.5 w, lerp(t, white, 0.7) | the hull-burst star (4 unequal arms {1, 0.55, 0.8, 0.45} x 1.2 w, rotated per shot) + cone 1.2 w | — | puff 0.6 → 1.2 w over 400 ms, s0 0.3, drifting 0.5 w along the gun axis | R 2.5 w, E 0.6, tint | 60 ms |
| plasma (orb / wave body) | disc 0.8 w, h 0.6 | a noise bloom: the hull-burst body law (two octaves, erosion, lobes) in the tint, R 0.5 → 1.0 w | — | — | R 3.5 w, E 0.8, tint | 180 ms |
| heavy (k ≥ 0.91) | disc 0.8 w, white, then afterglow: E ≤ 1.2 fading from white to t to 250 ms | cone 3 w, base 0.8 w | two vent jets 1.2 w across the gun axis (the orientation's side row, ±), 150 ms | puff 0.8 → 1.6 w over 600 ms, s0 0.4 | R 4 w, E 1.0, tint | 80 ms flash, 250 ms afterglow |
| flak (bullet flag `0x100` / `0x1000`) | disc 0.5 w, lerp(t, white, 0.5) | star 0.8 w | — | puff 0.6 → 1.2 w over 400 ms, s0 0.3 | R 2 w, E 0.5, tint | 80 ms |
| missile launch | not built: missiles do not fire through `0x00445170` (only class 7 lasers do [S, spawn §2]); the launch is carried by the missile's own plume until a launch site is traced (unknown 8) | | | | | |

Own fighter in the chase view: an energy disc of 1.3 m at 27 m is 46 px, its cone 156 px (target ≥ 40 px met [M arithmetic]);
an enemy fighter's muzzle at 600 m is the 3 px floor plus halo, with the glow on its nose.

### 3.2 Laws [C]

- **Re-light:** one record per (ship, gun slot k), the shield clock rule: a new shot resets t0 and the seed, never adds.
- **Breathing:** flame and star length x (1 + a (2 h − 1)), h = hash(ship, k, shot count), a = 0.25 x
  saturate(Δt_shot / (3 step_game)). At 1x a 10 Hz gun pulses visibly per shot; when shots arrive faster than three
  frames (SETA 6 at 10 Hz: one per frame) the flame is steady, so nothing alternates frame to frame.
- **Own cockpit:** flash, flame and glow x `own_cockpit_gain` 0.35 (weapon-look 2.9); the smoke puff is not drawn in
  the cockpit (it would sit on the camera).
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
under 10 µs; the rim draw (about 20 slots, where its band is ≥ 3 px: near bolts only) under 5 µs. Bound: **60 µs GPU**
for 512 bolts + 20 muzzles; CPU (tap, keys, projection of prev, VB 128 KB) **150 µs**.

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
| `muzzle_size_per_class` | each class on a fighter (F3 camera) and a capital at 600 m | disc / star / cone extents within 0.5 px or 3 % of the table; ≤ the R_s caps |
| `muzzle_life_min_frames` | each class at 1x, SETA 6 and 10 | lit on max(life, 3 frames) frames; heavy afterglow ≤ 1.2 after 80 ms |
| `muzzle_relight_resets_not_adds` | two shots 30 ms apart | peak = a single shot's (± 1 %) |
| `muzzle_breathing_suppressed` | 10 Hz at 1x and SETA 6 | 1x: flame length varies per shot within ± 25 %; SETA 6: frame-to-frame flame length change ≤ 2 % |
| `muzzle_broadside_cluster` | 12 turrets along a capital hull | summed glow peak ≤ 2.3 x one glow; core count 12 |
| `muzzle_smoke_surface_only` | ballistic over hull and sky; the own cockpit | sky unchanged; hull min ≥ 1 − s0 − 0.02; no puff in the cockpit view |
| `muzzle_heavy_rise_and_vents` | heavy on a capital at 600 m | cone reaches 3 w at 40 ms (± 1 frame); two vents ⊥ the gun axis within 5°; afterglow fades to t, ≤ 1.2 after 80 ms |
| 3.3's muzzle rows (cockpit gain, placement, light on hull, stub records, 20 guns ≤ 30 µs); budget rows | `bolt_alone_fills_frame`, `muzzle_alone_fills_frame`, 512 bolts + 20 muzzles at 5120x1440 | weights pinned; ≤ 60 µs GPU; CPU ≤ 150 µs (i686 fixture) |

## 8. Header constants, kill criteria and the flight

`bolt::` `mode`, `d_eng_m[3]` {600, 1500, 4000}, `d_eng_range` 0.75, `w_px_target` 3, `w_k` 0.6, `rest_w[4]`
{12, 4, 1.6, 0.5}, `sv_ref_mps` 2000, `tail_tau_ms[5]` {15, 30, 10, 0, 40}, `tail_max_w[5]` {8, 12, 4, 0, 20},
`tail_root_w[5]` {0.6, 0.35, 0.8, 0, 0.8}, `core_r` 0.4 / 0.55, `halo_sigma_w[5]` {0.8, 0.7, 0.9, 0.6, 1.0}, `e_body` 1.2,
`e_halo` 0.6 / 0.8, `h[4]` {0.75, 0.85, 0.7, 0.5}, `head_gain` 0.5, `head_ball_w` 0.6, `streak_c[4]` {1, 1, 0.5, 0},
`end_ms` 60, `wave_span_w` 6, `wave_sagitta` 0.25, `trim_rs` 0.8, `w_cap_m` 20, `rim_dark` 0.4, `rim_band_w` {1.2, 1.8}, `rim_min_px` 3, `halo_px_max` 40, `floor_core_px` 2, `floor_body_px` 3, `floor_halo_px` 1.5, `floor_len_px` 24,
`own_body_max_px` 16, `cockpit_native` false. `muzzle::` `mode`, per class {disc, flame, star, vent, smoke r0/r1/s0/life,
glow R/E, life}, `cap_core_rs` 0.25, `cap_flame_rs` 1.0, `breath` 0.25, `cluster_n0` 2.5. Shared: `core_e`, `min_frames`,
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

## Reproduce

```sh
S=tools/effects/weapon_catalog
python3 verification/results/bolt-muzzle-look/size_derivation.py $S/stock_ap.json $S/mayhem3.json              # section 1, 2.7
python3 verification/results/bolt-muzzle-look/size_derivation.py --mode reach $S/stock_ap.json $S/mayhem3.json # the fallback law
```
