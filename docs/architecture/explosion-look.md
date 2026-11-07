# Death explosions: the look per tier (step 3)

Ratified 2026-11-07 (main session); the death-row disassembly ([spawn note §10](../reverse-engineering/weapon-effect-spawn.md#10-death-row-element-layout-and-the-mask-seam-2026-11-07)) folded in the same day: the mask is a clone row R′ selected by a death-column swap, no new patch site. Section 13 (the fire ring, the composed body) added the same day from two user requests; not built, not ratified.

Design note for step 3 of the weapon-effects pass, amending [weapon-look.md](weapon-look.md) 3.2 with the research of
[explosion-references.md](../reference/explosion-references.md) and the burst body of [hull-burst-look.md](hull-burst-look.md)
(`src/effects/weapon_fireball_ps.hlsl`, its built constants in section 10 there). Marks: **[C]** chosen, **[S]** from a
source, **[M]** measured, **[I]** inferred, **[A]** assumed, **[U]** unknown. Where this note is silent, 3.2 stands.
Sizes: `verification/results/explosion-look/size_derivation.py` → `size_derivation_out.txt` (every pixel figure below).

## Outcome

1. **Pre-roll: yes for capitals and stations, on the engine's own hull copy.** The native spawn `0x00413610` runs
   for every big-tier death with the type record's death column pointing, for that one call, at a clone row R′ (every
   sprite and scene element `DISTANT`, `PROPAGATE` dropped, `FADEOUTBODY` retimed to our body's birth P, a `TIMEOUT`
   holding the carrier to P + 1 s): the dying root body is the picture through the pre-roll and the body eats it as
   it fades over the engine's fixed 1 s. Corvettes take R′ with P = 0; fighters and missiles skip the spawn.
2. **One body law, the burst's**: the built three-octave eroding lobed impostor at R_f = f R_obj clamped per tier:
   fighter 2.0 [12, 50] m, corvette 1.5 [30, 160], capital 0.9 [80, 600], station 0.6 [200, 1,800], missile 3.5 [6, 25].
   Born at R_0, rising e-folded over the tier's rise, hanging to 0.8 L while it erodes and cools, never stopping, gone
   at L: 2.0 / 3.0 / 4.0 / 6.0 / 0.8 game s plus the pre-roll 1.5 / 3.0 s. Section 13.2 composes it of 3-5 sub-bursts.
3. **Flash**: the burst's 40 ms four-arm star, arms 0.4 R_f, core 0.08 R_f, no halo, ≤ 0.25 H, on the body's frame.
4. **Shock**: the sphere shell was rejected from the frames (D14, `shock_tiers` 0); capitals and stations take the
   planar fire ring of section 13.1 instead.
5. **Embers and fragments** random per event from 4-6 seed cones at clamp(1.5 R_f / s, 40, 300) m/s plus the hull's
   velocity, three lifetimes to 1.2 L, cooling; one in eight a fragment; big tiers launch from the hull box.
6. **Light**: the body's glow on nearby hulls and the fading copy; the dying hull lit from t = 0 through the pre-roll.
7. **Smoke on in the first flight**: the burst's surface-bound multiply puff at 1.1 R_f from 0.5 L to 1.6 L; never the
   sky; the first thing shed on a close capital.
8. **Budget**: the close capital's worst frame 361.4 µs as built [M] against the 450 µs fence; the body, the flash and
   the embers are never shed (section 7).
9. **Eye inside, own death, nested kills**: the chord starts at the near plane; a coverage dimming if the meter gate
   fails; fighters in one blast share the cluster saturation.
10. **Proof before the flight**: dump cases per tier, the close capital, the nested kill and the eye inside at 1080p
    and 5120x1440 by `measure.py`'s 10 %-of-peak method; kill criteria in sections 10 and 13. A flight settles only the
    drift unit, whether the carrier reads as the hull, the exposure and whether the pre-roll reads as suspense.

## 1. The hull copy decides the pre-roll

What the engine does at a death [S, spawn note §4 and §10]: `0x00413610` allocates a class-`0x1b` carrier whose
subtype is the dying root body (not the part hierarchy), copies position, rotation and velocity, and spawns the row on
it; no ship or station death row has `HIDEOBJECT`, so **the carrier shows the hull**. The row's elements are sprites and
scenes with flags: `CUT` a multi-part 2D scene, `FADEOUTBODY` a fade of the carrier's root node over a fixed 1,000 ms
from the element's start, `CUTKEEPBODY` the 22 s debris scene, `WRECK` only when the kill's third argument is non-zero,
which every direct native caller passes as 0: **no wreck appears on a native death**. The capital rows 37 / 201 (stock)
and 602 (Mayhem) carry `EDF_PROPAGATE`: a second carrier (a scene copy) and 30 (40) children of row 149, the main t0
their last start (1.8 / 1.6 / 6.0-6.4 s), the hull copy fading over the second after it; corvette rows 40 / 601 keep the hull to 9.03 s and fighter rows 35 / 600 to 3.11 s, no fade.
Skipping the spawn removes the hull at t = 0; forwarding it unmasked plays the sprites beside ours. Elements are never
copied per instance (`inst+8` is the static row, re-read every update [S, §10.5]), so the mask is a clone row R′ of R
in a free Effects slot, selected for one call by a LIFO swap of the type record's death column (TShips `rec+0xbc`,
docks and factories `rec+0x60`): every sprite and scene element `DISTANT`, `PROPAGATE` dropped, `FADEOUTBODY` on an
empty node at P (capital 1,500 ms, station 3,000, corvette 0), a `TIMEOUT` node to P + 1,000 ms; no new patch site.
Fighters and missiles (rows 35 / 600, 39, 158-161) skip the spawn as the shield hit does (`xor eax,eax; ret`): the
body born at R_0 = 0.5 R_obj hides the hull's absence. The contract as built is the big-tiers table at the end. The
pre-roll is therefore the engine's own hull-copy time used for our secondaries, P 1.5 / 3.0 s against the stock 1.8 s;
whether the root-body carrier alone reads as the hull is section 11, item 1.

## 2. The elements

### 2.1 Body [C; constants S, the built burst]

The burst program unchanged, one quad per body, eye-biased 0.5 R(t), occlusion class 2 at SOFT 1.0, the lane's soft
cut through a hull plane. Per tier: R_f, L, the rise fraction ρ, R_0.

    R(t) = R_0 + (R_f − R_0) (1 − e^(−t / (ρ L))) + 0.1 R_f t / L       (t from the body's birth t_b)
    T(t) = 1 − 0.7 t / (0.8 L) for t ≤ 0.8 L, then → 0 at L;  E_b = 1.5 (body_max), x = t / L drives the erosion

ρ = 0.12 fighter, 0.15 corvette, 0.15 capital, 0.20 station, 0.10 missile (the big thing grows slowly: 0.24 / 0.45 /
0.6 / 1.2 / 0.08 s). R_0 = 0.5 R_obj for the tiers born at t = 0 (fighter 7 m, corvette 44 m, missile 1.5 m), 0.5 R_f
for the capital and the station (the size of the last two secondaries: continuity, no pop). The erosion's age term
(0.30 x) opens holes from about 0.4 L and the clumps go dark red then black (the ramp carries the cooling; no end pop).
No gradient shading now: the lobes and the first octave are the low-frequency structure; `death_body_low_frequency`
says whether it is enough, the one-tap normal (two slots per sample) the fix if not.

### 2.2 Flash [C]

The star as built (`star_arm_len` {1.0, 0.55, 0.8, 0.45} x A, width 0.10 A (1 − s), core 0.12 of the arm → here A =
0.4 R_f, core 0.08 R_f, ≥ 3 px, arms ≥ `star_floor_px` 6), white E 4 core, arms heat 0.9 at E 4 falling, 40 ms game,
three-frame floor, no halo, capped at 0.25 H on screen, born on the body's first frame. A secondary's star is the same
at A = 0.4 R_s. Pixels at the tier's distance: fighter arms 18 px / core 3.6; corvette 33 / 6.7; capital 154 / 31;
station 138 / 28; missile 13 / 3 [M arithmetic].

### 2.3 Shock [C → rejected, D14]

The sphere-shell chord law (`weapon_shock_ps`, r = 4 R_f (t / 0.3 L)^0.5, limb-bright, alpha (1 − t / τ)²) read as a
grey disc and then as a flat ring in the frames: `boom::shock_tiers` 0 on every tier (the big-tiers table), the
program kept for its law gate only. Capitals and stations get the planar fire ring of section 13.1; corvettes,
fighters and missiles have no shock element.

### 2.4 Embers and fragments [C]

The sprite batch's streak quads with the shared vertex law plus a linear branch (τ ≤ 0: p(t) = p0 + v t; the Expanse
shell "never slows"). Per death n embers: p0 fighter and missile at the centre ± 0.3 R_f hashed; corvette, capital and
station on the surface of the hull box `BubbleCache` holds (else the axis capsule of 2.5): big ships eject from their
hull. v = v_e u + v_hull, u from 4-6 seed cones (axis and half-angle 25° hashed per event), v_e = clamp(1.5 R_f / s,
40, 300) m/s: fighter 42, corvette 196, capital and station 300, missile 40; reach at 1.2 L 3.6 / 5.4 / 2.4 / 2.0 /
3.7 R_f [M arithmetic]. Lives 0.4 / 0.8 / 1.2 L by thirds
x (1 ± stagger / 2); E e^(−t / τ_e), τ_e = 0.5 L; colour heat(T), T 1 → 0 over the ember's own life; width 0.08 U,
the shared streak rule. Counts 32 / 64 / 128 / 160 / 12; the capital's and station's half from the secondaries, half
at the body's birth. Fragments n / 8: thick (0.25 U) dark streaks at 0.5 v_e, life 1.5 L, tumbling by a hashed phase,
drawn in the scorch pass (ZERO / INVSRCCOLOR, darkening 0.6, surface-bound) and additively as a 0.3-length hot tip at
heat(0.6) cooling.

### 2.5 Secondaries and the pre-roll [C; cadence M arithmetic]

A secondary is the burst body at R_s = 0.35 R_f (the last two of a chain 0.5 R_f; two of the station's 0.6 R_f), life
L_s = 0.3 L, ρ 0.1, E 1.5, with its own star (2.2), glow (1.6 R(t), 0.33 E) and 8 embers, at a hashed point in the
hull box (BubbleCache; `weapon_death bubble_cached=` says how often it is there) or, without a box, in an axis capsule
of length 1.4 R_obj and radius 0.25 R_obj along the node's forward axis (not 3.2's 0.7 R_obj sphere: empty space on a 1.8 km hull). Cadence: geometric, ratio 0.7 capital (8 bursts over 1.5 s: gaps 0.49 0.34 0.24
0.17 0.12 0.08 0.06 s), ratio 0.8 station (12 over 3.0 s: 0.66 … 0.07 s), every gap x (1 ± 0.2) by the seed: the chain
accelerates into the body (EVE's "somewhat exponentially faster", FS2's pre-death cluster [S]). Corvettes get none (an
M6 sits inside its own 1.5 R_obj body; a pre-roll would be two pops on an 83 px hull), fighters and missiles none.

### 2.6 Light on hulls [C]

The glow primitive (one lane fetch, `(1 − d² / R²)²`, back faces) at R = 5 R_f, E_g = 0.33 E_b x T(t), drawn while the
body is drawn: on neighbours and on the fading hull copy alike. During the
pre-roll the dying hull carries a glow of R = R_obj at the hull's centre, E 0.4 → 0.8 linearly over the pre-roll, T
0.6 (orange: burning before death), and each secondary its own 1.6 R(t) glow; the burst's per-owner cluster saturation
(s(N) = 1 / (1 + (N − 1) / 2.5)) over the live secondaries' glows, so twelve on a station sum to under 2.5 E_g.
Nothing on sky. The three-frame floor applies to the glow as to every element.

### 2.7 Smoke [C]

The burst's law: the same impostor with the smoke flag in the scorch pass, surface-bound, R_sm = 1.1 R_f, y = (t −
0.5 L) / (1.1 L), strength s0 0.45 x min(1, y / 0.27) x (1 − y) / 0.73, the body's seed and rotation, the erosion over
y. On a space death it darkens only what the lane holds: the hull copy while it lasts, neighbours, a station behind; the sky is untouched,
which is the vacuum look the references ask for. Life to 1.6 L is the record's slowest element (fragments 1.5 L).

### 2.8 Drift, placement, occlusion, time

As 3.2: the damped drift from the node's velocity (unit [U], unknown 1; v0 = 0 until the observe flight logs it);
world placement from the death record; occlusion class 2 (SOFT 1.0 body, shock and secondaries, 0.15 embers and the
star), class 1 glow and smoke; every life in game ms, max(life, 3 frames), fades linear, no fade-in. Under SETA 6 a
fighter death is 0.33 s of wall time and 20 frames at 60 fps: seen; the star keeps its three frames.

## 3. Per tier

| Tier (`patch_tier_of_ship`; station classes 5/6; missile class 10) | R_obj typ. m [M] | dist. m [A] | R_f law [C] → m | body px radius at dist., 1440 / 1080 [M arith.] | hull px | L s + pre-roll [C] | Phases | Embers / fragments | Shock → fire ring (13.1) | Secondaries | Composed body (13.2) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| fighter (M3-M5) | 14 (M5 12, M3 19) | 600 | 2.0 R, [12, 50] → 28 | 45 / 34 | 35 | 2.0 | star, body from 0.5 R_obj, embers, glow, smoke 1.0-3.2 s | 32 / 4 | none | none | 3 sub-bursts |
| corvette (M6, M8, TS, TP, TM) | 87 (TS 50) | 1,500 | 1.5 R, [30, 160] → 130 | 84 / 63 | 83 | 3.0 | star, body from 0.5 R_obj, embers, glow, smoke | 64 / 8 | none (D14) | none | 3-4 |
| capital (M1, M2, M7, TL) | 1,288 (M7 618) | 1,500 | 0.9 R, [80, 600] → 600 (M7 556) | 384 / 288 (0.53 H diameter) | 1,183 | 4.0 + 1.5 | hull glow from 0; 8 secondaries 0-1.5 s; star + body at 1.5 s; embers to 6.3 s; smoke 3.5-7.9 s; the hull copy fades 1.5-2.5 s | 128 / 16 | fire ring 924 m → 4.2 km over 1.0 s, gone by 1.6 s | 8 (134 px) | 4-5 + 1-2 jets |
| station (docks, factories) | 1,800 (0.5-52 km, loose spheres) | 3,000 | 0.6 R, [200, 1,800] → 1,080 | 346 / 259 | 1,152 | 6.0 + 3.0 | hull glow; 12 secondaries 0-3 s; body at 3 s, rise 1.2 s; embers to 10.2 s; smoke 6-12.6 s | 160 / 20 | fire ring 1.8 → 7.56 km over 1.5 s, gone by 2.4 s | 12 (121 px) | 5 + 1-2 jets |
| missile | 3 [A] | 300 | 3.5 R, [6, 25] → 10.5 | 34 / 25 | 19 | 0.8 | star, body, 12 embers | 12 / 0 | none (D14) | none | 3 |

Against the references [S]: the fighter body's diameter is 2.6 hull lengths (film and Everspace 2-4), the corvette's
2.0, the capital's 0.65 (HW3 / BSG 0.5-1: the hull is the picture), the station's 0.6 of its loose sphere. Floors: a
body never under 12 px of radius at 1440 rows (9 at 1080), the star core 3 px and arms 6 px, an ember 24 px; floors
raise size, never radiance; the ceiling is the budget. Vanilla for scale [M]: rows end at 3.1 s (fighters), 9 s (M6),
22 s (capitals; the hull copy fades at 1.8 s stock, 6.0-6.4 s Mayhem); ours end at 7.9 s with the hull gone at P + 1 s.

## 4. The capital and station timeline

Capital at 1.5 km, L 4.0, pre-roll P 1.5 (station in brackets, P 3.0, L 6.0):

| Game time | What is drawn |
| --- | --- |
| 0 | the carrier shows the root body (R′ spawn, no children); our hull glow E 0.4 at T 0.6 over R_obj; secondary 1 (0.35 R_f, 134 px) with its star and 8 embers at a hashed hull-box point |
| 0 … P | secondaries 2-8 (2-12) at the geometric cadence, the hull glow rising to E 0.8, the last two at 0.5 R_f; half the embers launched; the hull lit from inside and never hidden |
| P (t_b) | the star (arms 154 px; 138 px), the composed body born at 0.5 R_f (sub-bursts over 0 … 0.15 L, a jet), the fire ring at r_0 924 m (1,800), the other half of the embers and all fragments from the hull box at 300 m/s, the glow on hulls; the hull copy starts its 1 s fade inside the body |
| t_b + 0.6 (1.2) | the body at R_f (rise ρ L); the ring past the frame's edges at 1.5 km; holes from t_b + 1.6 (2.4) |
| t_b + 1.0 (1.5) | the hull copy gone, the carrier removed (the `TIMEOUT` element ends); the ring at r_max 4.2 (7.56) km |
| t_b + 1.6 (2.4) | the ring gone (L_w 0.4 L) |
| t_b + 2.0 (3.0) | smoke begins over neighbours; the body at T 0.5, orange, sparse clumps by 0.8 L |
| t_b + 3.2 … 4.0 (4.8 … 6.0) | final fade; embers dark by t_b + 4.8 (7.2), fragments by t_b + 6 (9), smoke by t_b + 6.4 (9.6); nothing native remains |

Corvette: the same R′ with P = 0, so the hull fades over the first second of the body and the carrier goes at 1 s;
vanilla keeps it to 9 s. The engine's 1,000 ms fade is fixed; P is ours per tier and the only timing in R′ we set.

## 5. Eye inside, own death, nested kills

- **Eye inside** (a ram, the own ship's death, the close capital after t_b + 0.3 s at 720 m): the box contains the
  eye and rasterises the screen; the chord runs from the near plane to the far side or the lane; the screen is hot
  clumps and dark holes at the body's current T (D2: a wash early), the glow lights the cockpit and the own hull, the
  composed body in its shared-noise mode (13.2). Charge 259-281 µs [I]. If `death_exposure_meter_shift` fails (> 0.5 EV), E_b scales by 1 / (1 + 0.5 max(0, cov − 0.5) / 0.5) with cov
  the screen coverage (1.5 → 1.0 at a full frame); the flash carries the brightness. This replaces 3.2's "capped at
  E 2", which was above `body_max` and did nothing.
- **Own death**: no own-ship dimming; the chase camera 24 m behind a fighter is inside its 28 m body, so the chase view
  is the eye-inside case too; drawn until the engine's death screen replaces the view.
- **Nested kills** (three fighters in one area blast, spawn note §6): three records, three stubs, three bodies within
  a frame; the burst's cluster rule over death bodies of the same frame (`cluster_n0_burst` 1.6) keeps three
  overlapping bodies bounded; each keeps its own seed, cones and lobes.

## 6. Hull light

Section 2.6 is the law; what it buys per tier: a fighter lights a hull within 140 m (5 R_f) for 2 s at a third of the
body; a capital lights 3 km around it for 4 s, and its hull copy from inside for 1.5 s before; a station 5.4 km. The glow's
box fills the screen in any capital death within 3 km, at 0.40 of the decal per screen: 71 µs always, and it is shed
at step 4 on a close capital only.

## 7. LOD, budget and the shed order

Charges by projected box area x weight [M, 5120x1440]: fireball 1.18 (three octaves) / 1.03 (two), smoke 1.20, star
0.40, glow and halo 0.40, shock 0.36 (off), the fire ring 0.50 + 0.10 (13.1); ember and fragment quads negligible [I]. At the tier's distance a death is cheap (a capital at 1.5 km: 0.12
screens, 25 µs of body); the cost is the close case: the close capital (eye at 720 m) charged 373 µs at t_b + 0.1 s with the
shock, 302 without [I]; as built the worst frame is 361.4 µs at P + 700 ms (D11); the fence `death_budget_capital_close_5120` 450 µs, the family's own cap `budget_screens` 2.5 (D10). Shed order (the
family's items in the shared 2.11 order): (1) the third octave of every fireball with the built hysteresis (enter past
the budget for 3 frames, leave under 0.8, blended over 48-64 px), then the composed body's steps of 13.2; (2) death
smoke, largest first; (3) the fire ring; (4) death glows and halos of owners other than the own ship, largest first;
(5) secondaries, protected only through the pre-roll (D11); never shed: the body, the star, the embers and fragments,
anything of the own ship. Caps: 32 fireballs + 64 secondaries, 2,048 sprite quads, 2 waves. CPU per death: one
record, the seed, the cone and sphere tables, ≤ 180 quads written once; the pre-roll is a per-frame cadence check.

## 8. Lab scenarios (`boom/…`, `weapon_effects_lab.html`, the vanilla row beside ours)

`fighter-m5-600m-side`, `fighter-m3-2km-floor`, `corvette-m6-1500m`, `freighter-ts-1500m`, `capital-m2-1500m-preroll`
(the hull a 2 R_obj x 0.5 R_obj box), `capital-m7-far-5km`,
`capital-m2-close-720m` (the eye inside from t_b + 0.3), `station-factory-3km-preroll`, `missile-300m`,
`missile-near-own-ship-40m`, `nested-3-fighters-area`, `own-death-chase`, `own-death-cockpit`, `seta6`; section 13 adds
the wave and compose scenarios and controls. Controls: per tier f / clamp / L / pre-roll / embers / secondaries, the
cadence ratio, ρ, R_0, v_e, `smoke`, the ramp stops, the coverage dimming. The lab plays the same laws.

## 9. Fixture gates (`EXPLOSION_*`; dump cases at 1080p and 5120x1440 split into parts `-flash`, `-body`, `-shock`, `-ember`, `-fragment`, `-glow`, `-smoke`, `-secondary`; `measure.py`'s 10 %-of-peak method; section 13's gates there)

| Gate | Case, time | Must hold |
| --- | --- | --- |
| `death_tier_sizes` | each tier at its distance, 0.1 / 0.3 (0.6 / 0.8 / 1.0 recorded, D1) L after t_b | body 10 % radius within 5 % of R(t) f / z; the clamps on an M7 (556 m) and a shipyard (1,800 m); the floors on `fighter-m3-2km-floor` (≥ 12 px) |
| `death_body_contrast`, `_silhouette_lobed`, `_erodes_outside_in`, `_colour_core_rim` | the burst's four gates on the fighter and capital bodies, median of 8 seeds | the burst's bounds (dark share ≥ 0.15, high-pass std / peak ≥ 0.10; max / min ≥ 1.25; lit share ≥ 0.8 / 0.35-0.75 / ≤ 0.20 (D1) / 0; core T ≥ 0.75, rim ≤ 0.5) |
| `death_body_low_frequency` | capital body at 0.3 L, 1080p | the R/2 low-pass minus its radial mean: std / peak ≥ 0.08 |
| `death_no_pop_at_birth` | each tier, frames t_b − 1, t_b, t_b + 1 | the first frame ≥ R_0 f / z; the star under 0.25 H; no element's radiance rises > 2x between frames except the star's birth |
| `death_flash_star_3_frames` | every tier, 16 and 100 ms steps | lit on exactly max(3, 40 ms) frames; second-moment ratio ≥ 2; ≤ 0.02 at 0.5 A off the arms |
| `death_embers_random_and_cooling` | fighter, two seeds | cone directions differ between seeds (36-bin correlation < 0.5); mean hue at 0.3 / 0.7 / 1.1 L on T ≥ 0.75 / 0.5 / ≤ 0.3; the farthest lit ember at 1.2 L within 10 % of v_e 1.2 L f / z; a fragment darkens a 0.5 hull by ≥ 0.3, invisible over sky |
| `death_preroll_cadence_and_hull_light` | capital 1.5 km | 8 births at the cadence ± 20 % inside the hull box; the hull glow's peak at 0 / P in 0.4 / 0.8 x 0.33 E_b ± 10 %; the body absent before P, born at 0.5 R_f |
| `death_glow_is_the_bodys_light` | fighter, capital at 0.1 / 0.3 L | glow peak / body peak ≤ 0.4; none on sky; none after L |
| `death_smoke_darkens_surfaces_only` | capital over a 0.5 hull and sky at 0.8 / 1.6 L | min ≤ 0.5 (1 − 0.6 s0) over the hull; sky unchanged; nothing after 1.6 L; ≤ 1 code residual through `TemporalPass` |
| `death_cools_through_ramp`, `_fades_to_zero_at_life` | each tier | mean hue at 0.2 / 0.6 / 1.0 L on T ≥ 0.75 / 0.5 / ≤ 0.25; every part 0 at its life (smoke 1.6 L) |
| `death_eye_inside_bounded`, `death_exposure_meter_shift` | `capital-m2-close-720m`, `own-death-chase` | D2's bounds; ≤ 300 µs fenced; ≤ 0.5 EV through the production meter, else the coverage dimming on |
| `death_nested_three_drawn` | `nested-3-fighters-area` | three bodies with distinct lobe seeds (silhouette correlation < 0.8 pairwise); peak ≤ 1.4 x one death's (D8) |
| `death_budget_capital_close_5120`, `death_octave_lod_hysteresis` | the close capital over t_b … t_b + 2 s | ≤ 450 µs fenced every frame, ≤ 300 with every step shed; the step enters after 3 frames past the budget, leaves under 0.8 |
| `death_temporal_energy`, `death_seta6_three_frame_floor` | fighter at 600 m; every tier at SETA 6 | energy ≥ 0.9 of the stage's; the star 3 frames, game lives unchanged, wall time 1/6 |
| hook record | `death_skips_native_fighter_missile`, `death_swaps_death_column_big_tiers`, `death_forwards_asteroid_and_rule_b_missile`, `death_nested_lifo_restore`, `frame_kept` | skipped (fighter, missile); entered with the column reading R′ and restored after (big tiers); byte-exact for asteroids and the rule-(b) missile; nested LIFO; the impact stub's register, flag and LastError contract; R′ rebuilt after a reload |
| budget rows; Reset, fault, refusal, slots | `*_alone_fills_frame` per primitive; as every family | weights pinned in the header; programs created at the latch; `MaxPixelShader30InstructionSlots` logged; the native validator on every ps_3_0 |

The user judges the dumped frames (both sizes) of `fighter-m5-600m-side`, `capital-m2-1500m-preroll`,
`capital-m2-close-720m`, `nested-3-fighters-area` and `own-death-chase` before the flight.

## 10. Kill criteria per tier (a flight observation flips a header, never adds a setting)

| Tier | Observation | Next build |
| --- | --- | --- |
| any | the body reads as a disc, a lamp, a balloon or a bubble | the burst's steps first (erode_t 0.25, then the star's arms x 0.6); then `boom::mode` simple: the same body, secondaries off, embers halved, smoke off (never a smoother ball); "too round" → 13.2's steps |
| any | the wrong size; too short / too long | that tier's (f, clamp) only; that tier's L only, the pre-roll separately |
| fighter | sparks unseen at 600 m | ember width 0.08 → 0.12 U, then count x 1.5 |
| corvette | the shock reads as a ring or slow motion | **applied 2026-11-07 (D14): the shell is off on every tier; the fire ring of 13.1 replaces it on capitals and stations, with its own kill row there** |
| capital | the pre-roll reads as a wait | P 1.5 → 0.75; then 0 with the secondaries spread over 0 … 0.4 L beside the body |
| capital | the hull is hidden by the body | f 0.9 → 0.7 |
| station | the view is covered for more than a second | clamp 1,800 → 1,200, then `station_secondaries` 12 → 6 |
| missile | unseen | f 3.5 → 5 |
| any | the exposure pumps | the coverage dimming on (section 5), then E_b 1.5 → 1.2 |
| a tier that still reads wrong | | `boom::tier_native` for that tier |

## 11. Unknowns and what settles each

| # | Unknown | Settles it |
| --- | --- | --- |
| 1 | Whether the root-body carrier (no parts) reads as the hull in the pre-roll, or whether the second carrier's scene copy is what the player sees [U, spawn note §10.4] | the observe flight beside the first R′ build; if the root body lacks the silhouette, R′ keeps `EDF_PROPAGATE` with a child row R″ of one empty `TIMEOUT` element and the fade applies to the scene copy |
| 2 | The hull copy's native timing: **answered** [S, §10.4]: `FADEOUTBODY` a fixed 1,000 ms from its start, t0 1.8 / 1.6 / 6.0-6.4 s | — |
| 2a | Whether the script death path (`"KilledBy"`) passes a non-zero third argument (a native `WRECK`) | the observe flight logs `arg4`; if ever non-zero R′ needs the empty `WRECK` timer node |
| 2c | R′ past a device reset and the Effects reload | the fixture's Reset and reload cases (built: rebuilt when the slot reads 0) |
| 3 | The node velocity unit (drift) and the forward-axis row | the observe flight's `weapon_death` rows |
| 4 | Whether a dying owner is in the bubble cache | `weapon_death bubble_cached=`; under 0.5 the capsule is the default |
| 5 | The shock's and fragment's weights; the three-octave body at a full screen natively | the `*_alone_fills_frame` rows (shock 0.36 built) |
| 6 | The exposure meter under a frame-filling body | `death_exposure_meter_shift`, then the flight |
| 7 | Whether the lobes and first octave carry enough low frequency at 800 px | `death_body_low_frequency`; gradient shading if not; 13.2's sub-bursts add the low frequency |
| 8 | Whether the pre-roll reads as suspense or as a delay, the 1 s fade as coming apart or a vanish | the flight (kill rows in 10) |
| 9 | Which deaths the user meets most | the observe rows' reason histogram |
| 10 | Section 13's five unknowns | there |

## 12. Options considered and why they lose

The spawn skipped with the pre-roll in empty space (the ship blinks out); a hulk stand-in (a dark blob where the
ship was); the spawn forwarded unmasked (native sprites beside ours); a shipped Effects table override (collides with Mayhem's rows 600-602); a
per-instance phase flip after the call (cannot hold the carrier to P + 1 s); a redirect of `0x0041367d` (a second,
non-atomic patch site); the engine's 30 children kept as the pre-roll (native sprites); 3.2's secondaries after the
body (every reference puts the chain first; the capital's second kill step); damped embers (they stop inside a
capital's body); embers from the centre on every tier (2 s inside the fireball); a flat annulus mesh for the shock (a
Praxis disc by construction); smoke off in the first flight (a fighter death ends in nothing); gradient shading now
(50 % more hashes before the gate asks); a noise atlas (the burst's fallback, not a design change); a smooth
blackbody sphere as the simple mode (the balloon). Section 13's options are at its end.

## 13. The fire ring and the composed body (2026-11-07, two user requests after the big tiers' frames; ratified by the main session the same day; not built)

User: "very wide fire ring coming with final explosion, like reactor boom producing a large heat wave" (capital and
station deaths); "explosion effect is a little clunky, too round". D14 stands (no shell on any tier): the ring is a
thick planar fire wave on two tiers; the body stops being one ball on every tier. Figures: the section-13 block of
`size_derivation.py` (`size_derivation_out.txt`); constants `boom::wave_*`, `compose_*`.

### 13.1 The fire ring (capital and station) [C]

- **Plane, birth, radius.** Born on the body's frame at P in the plane of the node's long axis (row 2) and up axis
  (row 2 x row 0), normal row 0, from the rows the record carries for the hull box; rows invalid: the view plane
  (logged `wave_plane=`). In-plane distance stretched 1.25 along the long axis (the hull ring's rule). r(t) = r_0 +
  (r_max − r_0)(1 − (1 − t / τ)³), then + 0.05 (r_max − r_0) per τ (never stops): r_0 the hull's half length (924 /
  1,800 m [M]), r_max `wave_r_x` 7 R_f (4,200 / 7,560 m), τ 0.25 L (1.0 / 1.5 s; 9.8 / 11.5 km/s at birth, 3.3 / 3.8
  mean), gone at L_w 0.4 L (1.6 / 2.4 s) by (1 − t / L_w)² [M arithmetic].
- **Profile.** The hull shock front's law (hull-impact-classes 3.5) in z = (d' − r) / w_e, w_e 0.1 R_f: a white-hot
  edge e^(−z²) at heat(T_w), a trailing band 4 b (1 − b) over 3 w_e at `wave_trail_gain` 0.7 heat(0.5 T_w), the wash
  0.25 inside; the band is `wave_band_x` 0.3 R_f (180 / 324 m). Across the plane a slab `wave_slab_x` 0.3 R_f thick,
  (1 − (2 y / h)²), its chord normalised by h as the body's column: edge-on a bar 0.3 R_f thick, never a line. T_w =
  1 − 0.5 t / L_w (edge white → orange, band orange → red), E_w `wave_e` 1.0, under the body's 1.5. The circumference
  broken as the hull ring's (m = 0.1 + 0.9 saturate(0.5 + 2.5 (n − 0.5))) with n = 0.7 vn1(9 φ / 2π + s) + 0.3
  vn1(23 φ / 2π + s') periodic (odd cells), seeded per record: arcs about 0.7 r long, lit share 0.7-0.8 expected [I].
- **Refraction ahead** through the engine shimmer pass: a plane mode in one of its two ring slots (c68-c75: the normal
  in the heading register, the mode in a free w [S, engine_shimmer_core.h]); a pixel whose ray meets the plane in
  [r, r + 2 w_e] is displaced outward from the centre's pixel by `wave_refract_px` 6 px at 1440 rows x (1 − t / L_w)²,
  sky included, a hull in front of the plane point excluded by the loop's lane test. Rects: the band's eight octagon
  strips (oriented; the script has the geometry) after the engines' 4 and the hull's 2 (14 ≤ 16), the newest wave only, the cap `weapon_ring_max_screens` 0.25 over the strips' sum, charged x 2.85. The single box rect
  refracts nothing (at 1.5 km it is past the cap at birth: r_0 592 px, 0.27 screens); the strips are 0.10 screens at
  r_0, 0.17 at 1,000 px [M arithmetic]. The pass copies one StretchRect over the union's bounding rect and draws the
  rect quads [S, engine_shimmer_pass.h]: the strips save the draw, not the copy; `wave_shimmer_strips_timing` decides.
- **Light on swept hulls**, in the same program: a lane point inside the slab's front window adds the front profile at
  0.33 E_w heat(T_w): a 0.3 R_f stripe crossing neighbours and the own hull, never the sky; no second draw.
- **Primitive and cost.** One oriented box quad (the slab's 8 corners, the hull ring's capsule-box code), program
  `weapon_wave_ps` (slab chord, stretched in-plane distance, the front, two 1-D hashes, the ramp, one lane fetch,
  early-out outside −3 w_e < z < 2 w_e): about 260 slots [I; the hull ring 180]; occlusion class 2; drawn with the eye
  inside (the wall recedes). Charged as the annulus's clipped area x `budget_weight_wave` 0.50 (the ring's) + the rest
  of the box x `budget_weight_wave_miss` 0.10 [I; `wave_alone_fills_frame`, `wave_miss_alone`]. At 1.5 km the band and bar are 115 / 86 px
  (1440 / 1080 rows) and the front crosses the frame's half width by 606 / 124 ms; at 5 km the whole ring fits (806
  px). Capital 1.5 km 24 µs; the close capital (eye 720 m, inside r_0) 70 µs at
  birth, 34 at 50 ms, 21 from 0.3 s: the measured worst frame 361.4 + 21 = 382 µs, the design birth frame 302 (shock
  gone) + 28 = 330 [I], within the 450 fence unshed. Shed order: (1) third octaves, (2) smoke, **(3) the ring** (the
  shock's old step), (4) others' glows and halos, (5) secondaries past P; the strips shed with the hull rects at (0).
  `max_waves` 2. Native Windows: ps_3_0 and the pass's own rect list.

| Gate (parts `-wave`; capital 1.5 km face-on and `-above` edge-on, 5 km, station 3 km, the close capital) | Must hold |
| --- | --- |
| `wave_radius_law`, `_never_brightest` | 10 %-of-peak outer radius within 5 % of r(t) f / z at 0.1 / 0.5 / 1.0 τ; none after L_w or on other tiers; peak ≤ the body's |
| `wave_thick_not_a_line` | face-on the band's radial 10 % width and edge-on the bar ≥ 0.2 R_f f / z at 0.5 τ |
| `wave_broken_circumference`, `_fire_not_grey` | of 64 angular bins at 0.5 τ the share with peak ≥ 10 % of the ring's in 0.40-0.85, max / min ≥ 2; edge peak ≥ 1.3 x the band's, band R / G ≥ 1.3, hue T ≥ 0.5 at 0.2 L_w and ≤ 0.35 at 0.9 L_w, chroma ≥ 0.2 (grey fails) |
| `wave_refraction_strips_present_and_capped`, `_shimmer_strips_timing` | 8 strips for the newest wave at 1.5 km, band pixels moved outward (sky included), 0 outside; past 0.25 screens drawn without a band; strips ≤ 0.6 x the box at r 1,000 px, else the box under the cap |
| `wave_lights_swept_hulls`; `death_budget_capital_close_5120` re-run | a neighbour 2 R_f away lit ≥ 0.1 only while the front crosses it, 0 on sky; ≤ 450 every frame with the wave drawn on ≥ 80 % of P … P + L_w |

Kill: a hoop, a Praxis ring or a line → `wave_band_x` 0.3 → 0.5 and depth 0.9 → 1.0, then `wave_tiers` 0 (no shell
returns); a flash of a ring → τ 0.25 → 0.4 L; the sun → `wave_e` 1.0 → 0.6; hides the body → r_0 0.5 → 0.7 H; the
refraction double-images → `wave_refract_px` 0; wrong from above → the view-plane fallback for every death.

### 13.2 The composed body (every tier) [C]

- **Law.** A death body is N = `compose_n` sub-bursts (hashed: fighter, missile 3; corvette 3-4; capital 4-5; station 5)
  of the burst impostor, seeds s + 7.3 i, radii (0.45 + 0.35 h_i) R_f, offsets 0.5 R_f h'_i along unit(lerp(u_i, long
  axis, `compose_axis_blend` 0.5)) (u_i hashed; alone without rows), births t_b + 0.15 L h''_i with
  sub-burst 0 on the star's frame (no delayed fireball), clocks x_i = (t − t_i) / (L − (t_i − t_b)) so all end at L,
  each its own erosion, lobes and white-hot core, R_i(t) by the tier's rise law. One glow and one smoke about the
  cluster's centre as today. Capital and station add `compose_jets` 1-2 jets: a 2:1 sub-burst (long 0.8 R_f, short
  0.4) along a hashed axis within 45° of the surface normal at a hull-box (or capsule) point, its centre half a long
  semi-axis out, born t_b + 0.05-0.15 L.
- **One quad, N spheres in the program** (decided). The quad is the cluster's box (the CPU derives the same table, 10 %
  margin); the program derives the table from the seed (`hash2`, mirrored by the lab), loops the spheres rolled
  (charged once), composes the densities as the union 1 − Π(1 − d_i) and takes the hottest sphere's heat and core:
  overlaps never add, so the sum stays under one sun by construction; the burst's cluster rule still acts across
  bodies. The jet: the ray scaled 1 / k along its axis before the sphere test (affine: the chord holds). Vertex:
  TEXCOORD6 (long axis, N), TEXCOORD7 (jets, ratio, spread, offset max), stride 80 → 112. Slots [I] 356 + table ~40 +
  ellipsoid ~12 + loop and union ~15 ≈ 420 rolled (~1,500 unrolled; the validator decides). One lane fetch, one batch.
  One quad per sub-burst loses: N lane fetches and boxes, additive overlap (every sub-burst dimmed), the eye inside
  at N screens (628-1,257 µs).
- **Cost** [I, the script]: a lit pixel covered by about 1.6 spheres, a miss 0.07 of a sample: factor 1.70 (N 3) … 1.98
  (5 + 2 jets) over a box 1.3-1.4 x the single's: capital 1.5 km 63 µs (was 25), station 55 (20), fighter 1. The eye
  inside the cluster: the **shared-noise mode**, chosen per draw: one noise sample at
  the sphere holding the eye, the others by chord and lobes (~0.15 each): 236 / 263 / 289 µs at N 2 / 3 / 4 against
  419 / 628 / 838 sampled; the eye inside also caps N at 4 (jets first) for the 300 µs eye-inside fence.
- **LOD.** Under the family's cap: (1a) third octaves as built; (1b) the shared-noise mode; (1c) N down one at a time,
  smallest first, jets last, never under `compose_n_min` 2; the octave step's hysteresis; under 24 px of radius N 2
  from birth. **Hull bursts** with R_b ≥ `compose_hull_r_m` 60 m [A] (capital calibre) compose at N 3, no jets (for
  hull-burst-look 10 when built; the fighter burst stays one sphere).

| Gate (`-body` the cluster, `-jet`; fighter, capital, station; median of 8 seeds) | Must hold |
| --- | --- |
| `compose_silhouette_cv`, `_changes` | the 10 %-of-peak outline's angular radius profile (36 bins): coefficient of variation ≥ 0.18 at 0.3 L and 0.6 L (the lobed ball 0.11 [M, the ember row]); the two ages correlate ≤ 0.7 or differ ≥ 0.15 RMS |
| `compose_not_grapes`, `_under_one_sun` | at 0.3 L one 4-connected blob holds ≥ 80 % of the lit pixels; peak ≤ 1.4 x the single body's at 0.1 L, mean lit radiance 0.7-1.3 x it |
| `compose_births_spread`, `_jets_capital` | sub-burst 0 on the star's frame, the last within 0.15 L, every part 0 at L; 1-2 jets from the hull box, length / width 2 ± 0.3, none elsewhere |
| `compose_eye_inside_shared`, `_lod_floor`, `_box_holds_spheres` | the shared mode on own-death chase and the close capital, D2's bounds, ≤ 300 µs; N never under 2, jets first; 0 sphere pixels clipped at the box over 8 seeds |
| the body gates of 9 re-run; `compose_alone_fills_frame` (N 2 … 5, overlap, shared) | bounds unchanged on the cluster; the factors pinned |

Kill: still a ball → offsets 0.5 → 0.7 R_f, radii 0.35-0.7, then jets on every tier; grapes → the shared mode always
and radii 0.6-0.9; pops → spread 0.15 → 0.05 L; dimmer → the hottest-sphere rule → the sum capped at 1.4; past the
fence in flight → `compose_n` 3 everywhere (never 1: the ball).

**Lab and dumps.** `boom/`: `capital-m2-1500m-wave-broadside`, `-wave-above` (the bar), `capital-m7-far-5km`,
`station-factory-3km-preroll`, `capital-m2-close-720m` (the wall), `fighter-m5-600m-side`, `capital-m2-1500m-preroll`
(a jet), `nested-3-fighters-area`, `own-death-chase`, `seta6`; a `single body` toggle, controls `wave_*` and
`compose_*`; the refraction only in the dump. Judge at 1080p and 5120x1440:
`boom_capital_1500m_broadside` at P + 40 / 120 / 400 / 1,200 ms, `_above`, `_far_5km`, `boom_station_3km`,
`boom_fighter_600m` at 0.1 / 0.3 / 0.6 L, `boom_capital_close_720m` at P + 40 ms, `boom_own_death_chase`.

**Unknowns.** Valid rows on every big-tier record (`wave_plane=` counts fallbacks); strips against the box in the pass
(`wave_shimmer_strips_timing`); the two programs' slots and weights (the `*_alone` rows, the validator); the planar
wave from above (the `-above` frames); CPU / GPU hash agreement (`compose_box_holds_spheres`). Other options lost: a
sphere-shell heat wave (D14), a flat ring sprite (the Praxis disc), the box rect alone, a separate surface-ring draw
for swept hulls (a second 0.50 box), composing the big tiers only (the fighter is "too round" too), N up to 8.

## Reproduce

```sh
python3 verification/results/explosion-look/size_derivation.py          # the per-tier table, cadences, the close-capital charge, the section-13 figures
# the burst's dump and measurement the gates extend (native host, the lock serialises it)
X3M_WEAPON_EFFECTS_DUMP_MS=40,120,296,696 X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py \
  python3 verification/probe/run_weapon_effects.py --host native --dump <dir>
python3 verification/results/weapon-effects-burst/measure.py <dir>
```

## Built numbers (step 3, 2026-11-07)

Built for fighters and missiles (the big tiers native then). Constants `namespace boom` in `weapon_effects_core.h`
(the lab reads them by name); the stage `weapon_effects_boom.h`, the shock `weapon_shock_ps.hlsl`, the halo
`weapon_halo_ps.hlsl`, the hook `core::death` with `weapon_effects_death_sites.h`. Measurements: the ledger's
"Explosions (step 3)". The coordinator's decisions D1-D8 after the first frames override the sections above:

| Item | Above | Built | Why |
| --- | --- | --- | --- |
| Seam | "one function" | `core::boom::decide(tier)`: ours when not in `tier_native` | no R′ in this step |
| Drawn sphere | 1.25 R (the burst's) | `body_r_sphere` 1.33 | mean outline 0.97 / 0.91 R(t) at 0.1 / 0.3 L at 1.25; 1.04 / 0.97 (fighter), 1.03 / 0.96 (missile) at 1.33 [M] |
| Size vs erosion (D1) | sizes within 5 % at five ages; lit share ≤ 0.15 at 0.9 L | sizes gated at 0.1 / 0.3 L (0.6 / 0.8 recorded); erosion ≤ 0.20 at 0.9 L | the sphere that meets the size lifts the lit share to 0.156 [M] |
| Body floor, bias | 12 px; 0.5 R(t) to the eye | `body_floor_px` 12 at the centre's depth; the bias eased in over the first 2 R of the eye's distance | a fighter at 3 km: law 7.7 px, drawn 12.1 [M] |
| White-hot core (D5) | the star and embers carry the birth | `core_gain` 2.5 (1 − x / `core_x` 0.35): radiance x (1 + g), whitened inside 0.3 R, 0 at 0.5 R (a vertex float, stride 80; 0 for hull bursts and smoke) | the body carries E ≥ 3 in its core at 0.1 L |
| Pinhole (D4) | program unchanged | within 0.08 of the centre the drift's direction blends to a seed-hashed one | a dark dot at every centre from 0.5 L. A ring and a bright centre (the "doughnut") remain from 0.5 L (the constant radial shift collapses each octave on r = advect / freq); the radial-zoom fix was tried and reverted (`weapon-effects-burst/zoom-attempt-2026-11-07.txt`: the hull burst lost 28 % of lit area) and needs the erosion retuned with it |
| Eye inside (D2) | dark share ≥ 0.15 at t_b + 0.5 s | at 0.25 L lit ≥ 90 % and std ≥ 0.05 of the mean; dark ≥ 0.15 at 0.6 L | a hot wash is correct inside a fireball |
| Light on hulls (D3) | R 5 R_f | R = min(1.6 R(t), 2 R_f), 0.33 E_b T heat(T) | the 5 R_f disc read as a lamp |
| Halo in air (D3) | none | `weapon_halo_ps`: R 1.6 R(t), 0.33 E_b T heat(T), x (1 − smoothstep(0.4, 0.8, x)), class 1, not with the eye inside, weight 0.40 | a body on black read as a cut-out; kept full, a balloon |
| Embers (D5) | U-wide streaks at v_e | launched inside the newborn body (never past 0.8 R_0), hidden within 0.8 R(t) (fading 0.5-0.9 L), widths 1.5-2.5 px at 1080 (pixel mode), length 0.25-1 of 0.35 R_f, a bright head, speeds 0.5-1.5 v_e, E x T | white sticks at birth, uniform rods later |
| Fragments | 0.25 U streak + tip | 3 px wide, half an ember's length, tip heat(0.6), a 1 m dark box under it, hidden as the embers | |
| Shock (D6, D7) | the shell 0.17 inside, to 0.3 L | limb only (interior 0), heat(T(t)), gone at `shock_end` 0.25 L; `missile_shock` false; `budget_weight_shock` 0.36 (measured 0.356 / 0.318 a screen at 5120x1440, 0.118 at 1080p; the earlier 0.76 divided a fence-dominated time by a small area); a refused program leaves the deaths drawn | a grey disc, then a flat ring (v2) |
| Nested (D8) | peak ≤ 2 E_b | three fighters 12 m apart: the three's peak ≤ 1.4 x one death's, the cluster rule engaged | the core makes one body's peak 4+ |
| Counts, records | 32 bodies, 2,048 quads | `max_bodies` 32, `max_quads` 2,048, 32 shock and halo boxes; life = pre-roll + 1.6 L + 1 ms | |

Shed order as built: (1) every body's third octave (the burst's hysteresis); (2) smoke, largest first; (3) shocks,
largest first (0.36); (4) other owners' glows and halos, largest first (0.40); never: the body with its core, the star,
the embers and fragments, the own ship.

## Built numbers (the big tiers, 2026-11-07)

Built on `effects` `da8e8bcac` (`boom::tier_native` 0, `boom::tier_swap` 14); measurements: the ledger's "Explosions:
the big tiers"; the coordinator's decisions D9-D15 after the big tiers' frames:

| Item | Above | Built | Why |
| --- | --- | --- | --- |
| The seam | section 1 | `decide` answers `swap` for the big tiers; `core::death` writes R′ into the type record's column only when the frame is the stage's, a ready clone matches the column's row and the tier's pre-roll (or is the column's own slot), the slot still holds the clone's count and array, and the swap stack (8) has room; else byte-exact forward with `cause=clone_not_ready` / `swap_full` / `no_record` | fail closed per death |
| The stub | "forwards, restores after" | handler result 2: the stub calls `0x00413610` itself with the three arguments copied, then `x3m_weapon_death_restore` (LIFO, integer only); an unwind through the call skips the restore and Present restores what the stack holds (`event=leak_restored`) | the tail jump cannot restore |
| R′ | section 1 | built at Present (`clone_upkeep`), never while the scope loads: a scan (the first valid Present, every 300 frames, after a drop) takes the (row, pre-roll) pairs of corvette and capital TShips classes and of TDocks / TFactories and the map of referenced rows; a vanished or changed source drops its pair and abandons its slot (at most 16, reclaimed by a reload), R′ built again elsewhere, never refilled or regrown in place; a zeroed slot refilled only with the same count. Copies DISTANT, FADEOUTBODY and WRECK cleared and clamped to P, PROPAGATE dropped, an empty FADEOUTBODY + TIMEOUT node at P for 1,000 ms and a TIMEOUT 0 .. P + 1,000; the array from the EXE malloc after its five prologue bytes match; the highest empty unreferenced slot 999 → 203; at most 16 pairs of 32 elements; the scan 9.4 µs median, 446 cold, over 1,553 records [M] | fail-closed (review 2026-11-07, A-C and L) |
| Unknown ships | the radius bands | a class id outside `patch_tier_of_ship` adds no pair: it swaps only when a known class shares its (row, pre-roll), else `clone_not_ready` | |
| Secondaries | "the last two 0.5 R_f; two of the station's 0.6" | `secondary_scale`: the last two 0.5, the station's two before them 0.6, the rest 0.35; R_0 0, rise 0.1 x 0.3 L, the core; 8 embers each | |
| Hull light (D9) | 0.4 → 0.8 to P | then falling to 0 with the copy's fade over P .. P + 1 s | the lit hull dropped to grey in one frame [M] |
| Hull shape | the box, else the capsule | `bubble_peek` (no read); the box to world by the node's rows 0 and 2 the record carries (row 1 = row 2 x row 0); the capsule 1.4 R_obj x 0.25; points 0.85 out; `bubble_cached=` logged | |
| Budget cap (D10) | the 450 µs fence | the family's own cap `budget_screens` 2.5 | six live secondaries near the eye charged 6.6 screens, 907 µs [M] |
| Secondaries' protection (D11) | past the first two live | the two newest kept from the shed only through the pre-roll; from P every one may go | kept past P: 448 µs; now 361.4 µs worst, every step shed 82.8 [M] |
| Drawn sphere (D12) | 1.33 | 1.33 to 64 px of radius, linearly to `body_r_sphere_big` 1.21 at 320 px; the eye inside keeps 1.33 | the outline grew with pixel size (capital 1.10 / 1.14 R(t)); at 1.21 within 5 % [M] |
| CPU gate (D13) | 50 µs for 32 fighters | `death_big_stage_cpu`: 4 capitals, 2 stations, 26 fighters ≤ 0.08 µs a quad and 100 µs | 0.065-0.068 µs a quad (791 quads, 53 µs) [M] |
| Shock (D14) | corvette and up by the law | `boom::shock_tiers` 0: none; the program optional, forced only for `death_big_shock_is_a_shell` | the corvette's grey ring at 100-600 ms and the capital's and station's white ring at birth were the soap bubble; the replacement is section 13.1's fire ring |
| Halo of corvettes and up (D15) | 1.6 R(t), faded 0.4-0.8 L | `halo_big_r` 1.15 R(t), ≤ `halo_big_e` 0.2 E_b, out over 0.3-0.6 L; `death_big_halo_capped` | a balloon about the body from 0.3 L |
| Scene model | the lab's box | the fixture draws the carrier as a box (2 R_obj along row 2, 0.5 across) at FADEOUTBODY's alpha, its depth in the lane; the lab the same | the worst case for hiding the secondaries |
