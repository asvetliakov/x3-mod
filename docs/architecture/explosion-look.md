# Death explosions: the look per tier (step 3)

Ratified 2026-11-07 (main session); the death-row disassembly ([spawn note §10](../reverse-engineering/weapon-effect-spawn.md#10-death-row-element-layout-and-the-mask-seam-2026-11-07)) folded in the same day: the mask is a clone row R′ selected by a death-column swap, no new patch site. Section 13 (the fire ring, the composed body) added the same day from two user requests; built 2026-11-07 (its Built numbers at the end), not flown.

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

- **Plane, birth, radius.** Born on the body's frame at P in the ship's deck plane: the node's long axis (row 2) and
  sideways axis (row 0), normal the up axis (row 1 = row 2 x row 0), from the rows the record carries for the hull box;
  rows invalid: the view plane (logged `wave_plane=`). Until 2026-11-08 the plane was the long and up axes (normal row
  0), the hull ring's rule, never a choice: a capital's widest extent is its deck, and the vertical plane showed a thin
  bar from above and ahead. The user chose the deck plane (2026-11-08); flight question: the wave from above and ahead. In-plane distance stretched 1.25 along the long axis (the hull ring's rule). r(t) = r_0 +
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

## 14. Radiance, rays and sparks (2026-11-07, the user's vanilla comparison; built the same day, not flown)

The user set our fighter death (Run245: a soft mid-tone clumped body, a white core only to about 0.25 L, 32 embers hidden
inside the body, no rays) beside three vanilla frames: a blown white-yellow core with hard radial rays and a thin
orange-red fringe; mid-life a dense spray of 100-300 small yellow sparks and short streaks; late a dim core in a wide
spark cloud about three fireball radii across. Four new elements of the death stage, every tier, scaled by R_f
(constants `namespace boom` in `weapon_effects_core.h`, the stage `weapon_effects_boom.h`, the lab's `renderBoom`):

1. **Radiant core [C, M].** The body's core gain 2.5 -> `core_gain` 5, decaying as (1 - x / `core_x`)^2 to 0.35 L
   (linear kept the capital's core a white blob at 0.3 L in the frames): at 0.05 L E_b (1 + 3.67) = 7.0 x the column.
   A big tier's body is born over its burning secondaries, so its gain eases in over `core_ramp_ms` 48 (three frames;
   the record's latched three-frame floor when longer, so it still acts under SETA; without it the capital's peak rose 2.09 x in one frame at P [M]). A **halo spike** at birth: the sprite batch's flash
   kind at the body's front, its white core `spike_rf` 0.15 R_f on screen clamped to 4-8 px at 1080 rows, halo 4 core
   radii, E `spike_e` 6 (1 - x / 0.12)^2, gone at 0.12 L.
   **The rim halo** (2026-11-08, the user: "a halo which falls off from the heat source: on the edges it should be
   bright/intensive then fall off"; the own sun's corona, [own-sun-look.md](own-sun-look.md) step 1d), every tier,
   replacing the filled glow in air (D3's 1.6 R(t) and D15's 1.15 R(t) at <= 0.2 E_b on the big tiers, both read as a
   balloon): `weapon_halo_ps.hlsl` with the glow's vertex program, the quad to `halo_r_x` 2.5 R(t), the rim R = R(t)
   (the body's 10 %-of-peak outline, 0.91-1.14 R(t) measured) in the glow's lobe slot. With r the pixel ray's impact
   parameter over R and d = max(r - 1, 0): p = (e^(-(d / 0.35)^2) + 0.25 w(d) / (1 + d^2)) / 1.25, w = 1 -
   smoothstep(0.5, 1.5, d) (0 at 2.5 R(t)), x smoothstep(0.9, 1, r): 0 inside 0.9 R(t) (no fill; the rise sits under
   the body's rim), the peak 1 at the rim, 0.050 at 2 R(t), its 10 % radius 1.80 R(t) [I, the law]. Radiance `halo_e`
   0.5 E_b T(t) x the cluster's saturation, colour heat(`halo_t` 0.8 T(t)) (the body's orange fringe), faded out over
   0.3-0.6 L (smoothstep, with the core), occlusion class 1, not with the eye inside the body. The first lab round
   (tail weight 0.4 windowed from 1.8 R(t), heat(T)) read as a grey disc with an edge out to 2 R(t) under AgX; the
   second (these constants) as a warm glow falling off the rim. Budget: the quad's box at `budget_weight_halo` 0.40,
   shed at step (2) with a wave and (4) without, as before.
2. **Rays [C, M].** A sprite kind (`weapon_sprite_ps.hlsl` depth.y 5; the vertex program passes it through). Since
   2026-11-07 (the user on the corvette: "not so pronounced/thin ... they shouldn't read as some decal ... look into our
   sun") the law is the own sun's spokes ([own-sun-look.md](own-sun-look.md) step 1d): an odd 15-25 rays a death (15, 17,
   ..., 25, hashed; 9-15 until 2026-11-08, the user: "can we increase number of rays ... make them various length - I see
   still 3 big rays which have almost the same length"), at stratified angles turned per death with the slot offsets
   hashed in 0.375-0.625 (two rays at least a quarter slot off opposite), each one screen-space trapezoid from the body's
   rim, 1.0 R(t) at its front (from 0.3 R(t), the white core's edge, until the 2026-11-08 grids: the body and the rim
   halo swamped every ray but the longest, the own sun's "why the rays vanished"), its length beyond the rim skewed:
   L(g) = (0.4 + 2.0 g^2) R_f on screen, g = frac(0.618 (i + o)) the
   golden-ratio hash with o hashed per death; the death's three largest g (their rank fixed per death, one O(N) pass a
   frame, no sort) at L(g_1), 0.78 L(g_1) and 0.6 L(g_1); every other ray at L(g) x 0.8 x 0.6 L(g_1) / L(g_4) (the fourth
   at 0.8 of the third: without it the fourth, unscaled, outgrows the scaled second and third), so the three longest
   differ by 28 % and 30 % and most rays are short (`ray_len_lo`, `ray_len_k`, `ray_top2`, `ray_top3`, `ray_rest`; was
   (0.4 + 1.6 g) R_f, evenly spread, whose top three sat within a few per cent); growing from 0.7 over the first 0.06 L;
   the along-profile is in u = along / length, so each ray's visible tip is the same fraction of its length; across a Gaussian (2^(-4 (y / FWHM)^2), no edge) of FWHM
   max(6 px at 1080 rows, 0.065 x its length x (1 + u / 2)), u 0 at the start to 1 at the tip (the spoke widening
   outwards, mean 0.08 of the length), the trapezoid 1.4 FWHM each side at either end (the tail there under 0.5 %);
   along the sun's e^(-u) (1 - u)^2 at full strength from the rim, faded in over the first 0.05 of u (0.15 before);
   brightness `ray_e` 1.0 x (0.6 + 0.6 g2) (0.4 before: a ray's base now about 0.8-1.0 of the rim halo under it), g2 = frac(0.755 (i + o) + 0.31), x (1 - x / 0.5)^3 x a per-ray flicker (period 150-300 game ms,
   at least two frame steps, so under SETA a flicker level holds at least two frames; the hash of the period's index blended smoothly into the next:
   1 - 0.3 level), colour heat(0.95 -> 0.8) (the lower end read as brown smoke shafts under AgX in the lab), SOFT R_f
   against the lane, gone at 0.5 L. The 2026-11-07 starburst (24-40 rays of 1-3 px, length 1.2-2.0 R_f, `ray_e` 2.4,
   flicker 30-60 ms at depth 0.55) read as a painted decal and is superseded.
   **Per-ray quads, not a ray sheet:** the sprite program grows 178 -> 201 slots (806 -> 895 words) [M], the starburst
   alone costs 4.2 us on the 300 m fighter and 8.8 us on the close capital over the fence (28 quads) [M]; the sheet
   variant (`verification/results/explosion-look/ray-sheet/`, three nearest rays a pixel) compiles to 464 slots [M] and
   would charge its whole box, about 0.025 screens on the 300 m fighter and the full screen on the close capital, about
   464 us by the 1 us a slot a screen rule [I].
3. **Spark spray [C, M].** The embers become 120-240 sparks a fighter death (`tier_sparks_lo/hi`: corvette 160-300,
   capital and station 240-400, missile 40-80; hashed per death; the secondaries' 8 each inside the count; the fragments
   keep their count from the first `tier_embers`). Each leaves at a hashed delay within 0.05 L (15 % at once: 200 heads
   on one ring stacked to E 50+), its radiance from 0.25 to whole over its first 0.02 L, from 0.55-1.0 R_0 out along its
   own direction (visible at birth), 60 % in the seed cones and 40 % in uniform directions, speeds 0.5-2.0 v_e leaning
   fast (sqrt of the hash), hidden only within 0.5 R(t) (soft over its inner fifth, the share fading with the erosion),
   heads (since 2026-11-07, the user's "too thin": 1-2 px before, lengths 4-24 / 40-80 px) a Gaussian of FWHM 3-5 px at
   1080 rows (hashed) x min(1, R_f on screen / 28.8 px), never under 3 px, in a halo of 0.12 of its radiance to 2 head
   radii (2^(-(d / 2r)^2), windowed to 4r), the tail behind it s^2 along (1 at the head, 0 at its end) across a Gaussian
   of FWHM 0.5 + 0.5 x the head's (2-3 px) at the head tapering to 1 px, the brightest of the three a pixel (the head's
   peak `ember_e` x `spark_gain` 0.8 = 2.08, for the meter); lengths 6-36 px (h^2) and one in 20 a long streak of 30-60
   px (its head 3 px, its tail 2 px at the head; 60-120 px one in 12 read as hard rays through the body in the 600 m
   grid), the lengths x min(1, R_f on screen / 28.8 px), at most 1.2 R(t) on screen while the body draws (no streak
   crosses the body), never under two head widths; a fragment's 3 px head takes the same profile; E `ember_e` 2.6 x 0.8 e^(-t / 0.8 L) x (1 - t / life), T 1 - (t / life)^1.5 (yellow late), lives to 1.2 L
   (x 1 +- 0.2) by thirds. `ember_e` 8 -> 2.6 is the meter's limit, not a look choice: at engine 2.6 a head decodes to 8.2
   linear, past the tonemap's white at the Auto cap. **Shed by half-counts:** a frame draws at most 512 ray quads and
   1,024 sparks within the sprite room the rays, stars and spikes leave (4 a death, and a big tier's secondaries' stars
   through the pre-roll); past either, every second index of every death
   is left out, then every fourth (at most 3 / 4 halvings; an index is drawn when its low bits are all set, so the long
   streaks keep their share and the fragments, indices 7 mod 8, their full count through three halvings and half of it
   at the fourth). 20 fighters at 600 m: 2 halvings, 787 sparks drawn, 2,624 shed [M].
   Since 2026-11-08 (review) the spark quads (2 FWHM + 0.5 px about head and tail) are charged to the family's budget
   at `budget_weight_flash` a screen, as the rays and stars, and the frame's spark area, estimated before the build per
   death from its mean quad (a 4 px head, a 16 px length, both x min(1, R_f on screen / 28.8 px)), takes at most a
   quarter of that budget: past it another halving (within the four).
4. **Contrast [C].** No change: the body's rim erosion is the shared program's (not this step's), and the frames show
   the blown core over the orange fringe with black around it; the glow and halo weights stay (the halo's law changed
   in item 1, 2026-11-08).

The meter: `death_exposure_meter_shift` runs the production meter (`src/renderer/exposure.cpp`, Auto capped at +1.3 EV,
gamma 2.2 decode) at 5120x1440 and measures the fresh target's shift against the scene alone, not a head's radiance.
The 300 m fighter never moves it (0.0 EV at 0.025-0.5 L over black, a dim 0.15 and a lit 0.5 scene) [M]: the bright
pixels (frame peaks 35 / 33 / 20 at 50 / 100 / 200 ms) fill fewer than the 19 tiles of 1,840 the p99 tile maximum
needs. The 120 m fighter (added at review) does: over the dim scene the highlight limit pulls the target down 0.31 /
0.39 EV at 200 / 400 ms, and over black sky the late dim cloud (frame peaks 3.0 / 2.3 at 600 / 1,000 ms) lifts the
target from the empty frame's 0 to the +1.3 EV cap [M]. The gate therefore compares against the dim and lit scenes
only (worst 0.39 EV, bound 0.5); the black-sky shift is recorded, not gated: the meter scores an empty frame 0 EV and
climbs to its cap on any signal, its black policy rather than the death's highlight pull (coordinator decision
2026-11-07). At engine 2.6 the limit is +1.0 EV, at 3.4 0 EV
[I, the meter's arithmetic].
Since the 3-5 px Gaussian spark heads (2026-11-08) the 120 m fighter pulls the dim scene's target down 0.817 EV at
400 ms (1.13 at the spark gain 1; the head puts its whole radiance on a pixel where the 1-2 px streak head sampled
lower, and the p99 tile maximum follows it [I]); the 300 m fighter still moves nothing [M]. Coordinator decision
2026-11-08: a death at 120 m may dim the scene, so the bound splits by case: the 300 m fighter <= 0.5 EV (sparks at
normal range never drive the meter), the 120 m fighter <= 1.3 EV (Auto's upper limit, +1.3 EV).

| Gate (fixture, `EXPLOSION case=...` rows) | Built [M] | Bound |
| --- | --- | --- |
| `death_core_radiance` (fighter 600 m, 8 seeds) | median inside 0.2 R(t) at 0.05 L 6.98 | >= 6 |
| `death_rays_starburst` (300 m, 5120x1440, 4 seeds; 15-25 rays from the rim since 2026-11-08) | 21, 17, 23, 15 rays drawn; angular peaks at 0.1 L at 1.25 R(t) 21, 17, 23, 15 (every ray), at 1.5 R(t) 10-13; 0 at 0.6 L | >= 12 and <= drawn + 1 (was drawn +- 1 at 0.5 R(t)); >= 3; <= 2 |
| ray lengths beyond the rim (part of `death_rays_starburst`; 300 m, 0.1 L) | the three longest 2.13-2.29 / 1.66-1.78 / 1.28-1.37 R_f (seed 0: 162 / 126 / 97 px at 1080 rows), every other ray <= 1.02-1.10 R_f; each visible tip (0.1 of its axis' maximum) at 0.600-0.610 of its length | each >= 1.25 x the next; spread <= 0.15 |
| `death_rays_soft` (300 m, 0.1 L) | median FWHM at 1.25 R(t) 6.10 px at 1080 rows; worst ray peak over the death under it (the rim halo) there 0.92 | >= 4; <= 1.2 (was <= 0.4 at 0.5 R(t): a ray now starts at full strength over the halo, `ray_e` 1.0) |
| `death_rays_differ` | two deaths' 360-bin profiles correlate -0.04 | < 0.3 |
| `death_halo_rim` (fighter 600 m, the halo alone; 2026-11-08) | radial profile peak at 1.022 / 1.030 R(t) at 0.1 / 0.3 L, 0.056 / 0.050 of it at 2 R(t), 0 inside 0.85 R(t) | within 0.1 R(t); < 0.15; 0 |
| `death_halo_in_air` (fighter 600 m) | farthest 10 % pixel 1.83 R(t) at 0.1-0.55 L; none from 0.62 L, after L or with the eye inside | <= 2.2 R(t) (1.8 before; the law's 1.80) |
| `death_big_halo_capped` (capital 1.5 km, 0.2 L; re-derived to the rim law) | farthest 10 % pixel at impact parameter 1.80 R(t) (599 px; R(t)'s silhouette 274 px), peak 0.62, the profile's peak at 1.003 of the rim's silhouette, 0.050 at 2 R(t), 0 inside 0.85 R(t), none at 0.62 L | <= 2.2 R(t) (D15 1.25 R(t)); <= 0.5 E_b (D15 0.2 E_b); within 0.1, < 0.15, 0; 0 |
| `death_sparks_dense` (600 m, 0.3 L) | 217 heads on screen (173 local maxima); lengths std / mean 1.0 | >= 100; >= 0.4 |
| `death_spark_heads` (600 m, 1080p, 0.3 L) | median head FWHM 4.37 px at 1080 rows (116 heads) | >= 3 |
| `death_exposure_meter_shift` (300 m and 120 m, 5120x1440) | 300 m 0.0 EV; 120 m 0.82 EV over dim since the 3-5 px spark heads (0.39 before), 0 over lit (black sky 1.30, informational) | 300 m <= 0.5, 120 m <= 1.3 over dim / lit |
| `death_rays_sparks_budget_5120` | fighter 600 m 21.7 us at 0.1 L, 20 fighters 54.8 us; rays alone fighter 300 m 15.0 us, close capital 27.0 us (2026-11-08: 15-25 rays from the rim, `ray_e` 1.0, the 2.5 R(t) halo) | <= 60; <= 150 |
| `death_budget_capital_close_5120` | worst 284.1 us | <= 450 |
| halo alone (the eye in its sphere, 5120x1440) | 71.8 us, 0.405 a screen (halo program 60 -> 90 slots, 470 words) | `budget_weight_halo` 0.40 |

Gates changed with the law: `death_embers_hidden_in_body` (D5: at most half drawn at birth, none over the young body) now
asks for some drawn at birth and >= 80 % once the births are out (41, then 207 of 221); `death_embers_lengths_and_widths`
std >= 0.4 of the mean (was 0.2, the mean under 0.35 R_f); `death_no_pop_at_birth` and `death_big_no_pop_at_birth` leave
the spike out with the star (worst rise 1.27 / 1.29). Frames: the lab (`boom/fighter-m5-300m-vanilla-compare` at 50 /
150 / 400 / 900 / 1,500 ms beside the vanilla frame) and the native dump grids (fighter 300 / 600 m, the nested kill,
the capital broadside through the main blast; clipped engine space and AgX from the 1080p frame) local. Kill: rays read
as a lamp or scratches in flight -> `ray_x` 0.5 -> 0.25; the spark cloud reads as noise -> `tier_sparks_hi` halved. Flight
questions: do the rays read as soft glow spokes, not a decal; pumping (scene darkening and brightening with the spark
cloud at point-blank range).

## Reproduce

```sh
python3 verification/results/explosion-look/size_derivation.py          # the per-tier table, cadences, the close-capital charge, the section-13 figures
# the burst's dump and measurement the gates extend (native host, the lock serialises it)
X3M_WEAPON_EFFECTS_DUMP_MS=40,120,296,696 X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py \
  python3 verification/probe/run_weapon_effects.py --host native --dump <dir>
python3 verification/results/weapon-effects-burst/measure.py <dir>
python3 verification/results/explosion-look/ledger_rows.py --markdown --rays  # section 14's table from the native record
python3 verification/results/explosion-look/ray-sheet/slots.py .             # the ray sheet's and the sprite program's slots
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
| Pinhole and doughnut (D4) | the burst program unchanged | the drift law (2026-11-07): octave i samples at p (f_i - g_i), g_i = (f_i / 2) / sqrt(1 + (|p| f_i / 2a)^2), the radius u = |p| f_i cells moved to u - a y / sqrt(1 + y^2), y = u / 2a: strictly increasing (slope 1/2 at the centre), the full shift a far out; no direction at the centre, so the 0.08 seed-direction blend is gone. Retuned with it: `fireball_erode_t` 0.28, `_erode_soft` 0.28, `_column_pow` 0.25 (hull-burst-look section 10) | the constant shift a p / |p| collapsed each octave's circle r = a / f_i onto one noise point: a dot with a ring at every body's centre from 0.5 L (octaves 2 and 3, inside about 0.15 R_s). Gates `hull_burst_no_centre_ring` / `death_body_no_centre_ring`: worst local ring prominence over the three ages 0.14-0.26 / 0.09-0.19 before, 0.08-0.09 / 0.04-0.09 after (bound 0.15) [M]; the radial zoom tried first lost 28 % of the burst's lit area (`verification/results/weapon-effects-burst/zoom-attempt-2026-11-07.txt`); ledger "Fireball drift law" |
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

## Built numbers (section 13, 2026-11-07)

Built on `effects` `bfe904e97`, uncommitted; measurements: the ledger's "Explosions: the fire wave and the composed body".
Decisions D16 on, from the native frames and rows:

| Item | Above | Built | Why |
| --- | --- | --- | --- |
| D16 the composed program | the fireball program, the table derived in it from the seed, one batch | a separate `weapon_compose_ps.hlsl` (2,318 words, a rolled loop of at most 7), one draw per composed body with the CPU's table in c14..c42 (`renderer::WeaponComposeConstants`); `weapon_fireball_ps.hlsl` and its bytecode unchanged then (D41 later added the fourth octave there); the vertex stays 80 bytes | in the fireball program the hull burst alone rose 108 -> 154 us full-screen and the eye inside a fighter 281 -> 654 us [M]; the CPU table is the only table (no hash agreement to trust) |
| D17 union colour | the hottest sphere's heat and core | the density-weighted mean heat and core | rim T 0.75 against <= 0.5 with the hottest rule [M] |
| D18 sphere 0, sizes | offsets for all; unit unspecified | sphere 0 at the centre; the unit = R(t) / the CPU's 16-ray projected outline of the table (drawn radius / the drawn-sphere factor x the single body's loss by age, `compose_erode_*`) | outline / R(t) at 0.1 / 0.3 L within 5 % on every tier (fighter 1.01 / 0.98, capital 0.98 / 1.05, station 1.02 / 1.03 [M]) |
| D19 the outline changes | one clock per sub-burst | each sub-burst's erosion paced 1 - (1 - x_i)^k, k 1.0-2.4 (`compose_pace_*`); heat and core on the body's clock | correlation 0.88-0.91 between 0.3 and 0.6 L with one clock; a late sub-burst's own core drew a white lamp [M, frames] |
| D20 "still a ball" | offsets 0.5, radii 0.45-0.8 | the kill step applied: offsets 0.7, radii 0.35-0.7 | fighter CV 0.144 at 0.3 L against 0.18 [M] |
| D21 the eye inside | N <= 4 shared | N <= 2 (`compose_eye_n`), shared: one noise sample, two octaves, the chord alone past the first sphere | the eye-inside frame 336.5 us at N 3 against the 300 us fence; 286 us now [M] |
| D22 charge factors | 1.70-1.98 | `compose_cost_*`: 0.47 + 0.115 N, shared 0.70 + 0.03 N | the compose_factor rows: N 2 / 3 / 4 / 5 0.74 / 0.84 / 0.95 / 1.03, shared N 4 0.81 [M] |
| D23 the wave's colour and break | T_w 1 - 0.5 t / L_w; trail 0.7, wash 0.25; the ring's break | T_w 0.85 - 0.6 t / L_w; trail 0.45, wash 0.15, both broken; m bias 0.55, contrast 4, floor 0.05; the mid-plane's point when the chord's in-plane span is short | a white-grey tyre at birth, edge / band 0.84, all 64 bins lit [M, frames]; now edge / band 1.62, lit share 0.72 |
| D24 the wave's weights | 0.50 + 0.10 miss | 0.50 + 0.56 miss | `wave_alone_fills_frame` 0.497, `wave_miss_alone` 0.563 a screen [M]; the close capital sheds the wave on 24 of 32 frames (open) |
| D25 the strips | the cap over the strips' boxes | the cap over the drawn quads; the strips' GPU timing in the engine shimmer fixture | strips 0.088 ms against the box's 0.166 at r 1,000 px: 0.53 <= 0.6 [M] |
| Hull bursts | compose at R_b >= 60 m | deferred (hull-burst-look 10) | |
| D26 the wave's band and sheet | band 0.3 R_f, edge + band + wash | the kill step: `wave_band_x` 0.5 (`wave_trail_span` 5); a trailing sheet over `wave_sheet_x` 1.0 R_f behind the front, (1 - s)^2 at `wave_sheet_gain` 3.0, heat(T_w) -> heat(0.5 T_w), x a two-octave value noise of the in-plane point (cell `wave_sheet_cell` 0.15 R_f x (r / R_f)^0.5: it spreads with the front; floor 0.2, contrast 3), the band under the same noise; the wash at its old 3 w_e; window 12 / 3.5 w_e | a thin smooth hoop at 1.5 / 5 km [frames]; at gain 0.8 and 0.35 R_f cells the sheet unseen and the band a tyre (edge / sheet 7.4) [M, frames]; now band + sheet 0.751 R_f, the sheet 0.029 of the peak 0.7 R_f behind [M] |
| D27 flame tongues | brightness break only | the front's radius shifted +-0.3 x the band (0.15 R_f) by w_1 n_0 - w_0 n_1 + w_0 of the break's own two 1-D noises (uncorrelated with the break's mix; no new hash), contrast 2 | separate tongue noises cost 4 hashes a pixel; the break's finer noise alone moved the lit bins' fronts out by 2 w_e (radius gate 1.066) [M]; spread now 0.568 R_f [M] |
| D28 the radius gate | the outer 10 % radius | the lit bins' median edge (each bin's brightest pixel) against r(t), 5 % | the outer 10 % radius of a bin picks its widest tongue (1.04-1.08 of r + 1.52 w_e) [M] |
| D29 the wave drawn as strips | the box | the annulus [r - 12 w_e, r + 3.5 w_e] as eight oriented strip quads (near-plane cut, screen-clipped area), each shading only its own sector (disc.w; the strips overlap and the blend adds: seams in the first frames), the box when the strips sum past it, nothing when no strip is on screen; `WeaponWaveConstants::quads`, 32 vertices a wave | the box's early-out pixels cost 0.56 a screen (D24) [M]; seams where the strips overlapped [frames] |
| D30 the wave's weights | 0.50 + 0.56 miss | `budget_weight_wave` 1.26 a drawn screen, `_one` 1.11 with the sheet's first octave alone, `_miss` 0.56 (the early-out path, unchanged) | 1.283 / 1.151 a screen, the box at a station's birth 1.297 (5120x1440) [M] |
| D31 the shed order with a wave | 3: the wave | (2b) the sheet's second octave (per wave, c14.w), (2c) the secondaries, then (3) the wave | the brief; the close capital still sheds its wave at P + 50-200 ms (open: 227 us a screen beside 316-372 us) [M] |
| D32 the union | 1 - prod(1 - d_i) | min(1, d_max + `compose_soft_add` 0.25 (sum - d_max)) | the union filled every sub-burst's holes: smooth balloons [frames] |
| D33 the cores | one per sub-burst to 0.35 L on the body's clock | core_gain (1 - x / `compose_core_x`), gone at 0.25 L | a per-sub-burst age window drew a late sub-burst's lamp at 0.2 L; at 0.2 the fighter's hue at 0.2 L fell to 0.50 against 0.75 [M] |
| D34 erosion compensation | the union's unit grown by the sub-bursts' loss | per sub-burst: its radius x loss(x) / loss(x_e) (capped at `compose_erode_cap`), the unit from the uncompensated outline | the brief; lit share at 0.9 L 0.233 with it alone [M] |
| D35 offsets and heat | offsets 0.7 h1 | sphere i >= 1 at 0.85 (0.7 + 0.3 h1); heat x (1 - 0.3 offset / 0.85); the capital's unit x 0.97 (`compose_size[5]`) | at 0.6 L one blob on every tier, lit 0.233 at 0.9 L [M]; offsets 0.75-1.0 broke the rim hue (0.62) and lit at 0.1 L (0.789) [M]; 0.51-0.85 left the station 1.5 blobs [M]; capital outline 1.051-1.053 at 0.3 L [M]; now 2 blobs, lit 0.835 / 0.585 / 0.154, CV 0.32-0.51 [M]; the close capital's eye-inside lit 0.842 against 0.9 (open) |
| D36 the CPU pass | placement, outline and table every frame | each record's placement at unit 1 with its slopes in the age refreshed on a 2 px move, every 4th build, past 0.05 of the age, at a birth or a new set; per frame a linear advance (no exp / log) and the table write; LOD by count: N <= 3 above 16 live bodies, <= 2 above 24 | 32 deaths 64.2 us with the old per-frame placement [M]; the gates now swing with the host (single body alone 38.7-57.9 us) [M] |
| D37 the eye's spheres | the first sphere holding the eye, then the next | the one holding it deepest, then the non-jet sphere filling most of the view | no change measured on the close capital (0.865) [M] |
| D38 the wave's colour (v3) | edge heat(T_w), band and sheet colours on the CPU | the whole wave on the body's heat ramp in the program (c16..c20): T 1.0 at the front, 0.85 at the band's centre, 0.7 at its end, 0.45 at the sheet's end, x (1 - `wave_t_cool` 0.72 t / L_w); the sheet's noise carves holes (smoothstep at `wave_hole_t` 0.3 -+ 0.08), never darker material | the coordinator: beige / olive with soot; mean chroma 0.73 / 0.61 / 0.93, all lit pixels on the ramp [M] |
| D39 the slab's cap | the column x the summed profile | intensity edge + holes (`wave_band_gain` 0.15 band + `wave_sheet_gain` 0.8 sheet) + wash capped at 1, x the column (<= 1): never past the face-on brightness, E_w at most | edge / band 1.42, band + sheet 0.98 R_f, edge / sheet 1.93 (dev run) [M] |
| D40 the eye near the plane | none | within 2 R_f of the plane (blended over 0.5 R_f) the wave x smoothstep(0, R_f, the eye's in-plane distance from the band) | from above at 1.5 km: worst mean 1 - e^(-Y) 0.058, 1.8 % of pixels above 0.9 [M] |
| D41 the fourth octave | three octaves | `fireball_octaves` 4: a fourth (14 cells per R_s, weight 0.25) past 160 px of projected radius (1440 rows), blended over 60 px (disc.w 1 .. 2), in both programs and the hull bursts; smokes keep three; charged at `budget_weight_fireball_fine4` 1.35 | 258.3 / 227.3 us a frame-filling body (1.349 a screen) [M]; weight 0.12 left the capital's blobs at 2 [M]; still 2 at 0.25 (open) |
| D42 the shed order | the wave at step 3 | (0) strips, (0a) the sheet's second octave, (0b) the fourth octave, (1) the third (hysteresis), (1b, 1c) compose, (2) glows and halos, (3) secondaries, (4) smoke, (5) shocks, (6) the waves last; past 16 boxes one heap sort, not a scan a box | the close capital's wave drawn on 6 of 6 on-screen frames, worst frame within 450 us [M]; with the wave kept the big set sheds its glows: 64.9 us with a scan a shed box, 57.3 with the sort (dev runs) [M] |
| D43 the CPU rows | one median of 201 builds | the median of 5 such repeats, all five recorded | repeats spread 40.4-53.0 (32 fighters) and 57.5-87.2 us (the big set) across runs [M] |
| D44 the eye-inside bound | lit >= 0.9 | lit >= 0.8 (the coordinator) | two sub-bursts set apart light 0.842 of the view [M] |
| D45 the kill switches | none | `wave_sheet_enabled`, `compose_enabled` (header constants): the ring without its sheet; the single body with the fourth octave | the coordinator's kill criterion; `compose_enabled` false since D46 (v4), `wave_sheet_enabled` true |
| D46 the kill (v4) | the composed body on every tier | `compose_enabled` false for the flight: every death body the single lobed ball with the fourth octave (the compose program built, its gates and rows skipped with a logged reason) | the v3 frames: the capital's smooth late lobe at 2700 / 3900 ms and a white blob at 0.2 L; `compose_capital_structure` 2 blobs against 3 [M]. Flight question: does the single body with the fourth octave read broken and irregular (not a ball) on a capital and a station? |
| D47 the wave's ramp (v4) | T 1.0 / 0.7 / 0.45 | T 0.9 at the front, 0.6 at the band's end (0.75 at its centre; the coordinator's "band 0.7"), 0.6 -> 0.4 over the sheet | the v3 ring read pale beige [frames]; mean chroma 0.80 / 0.73 / 0.93, hue 0.75 / 0.25 at 0.2 / 0.9 L_w (the hue band unchanged) [M] |
| D48 flame gaps (v4) | round holes, smoothstep 0.08 | the hole noise periodic in the angle (48 cells about) and stretched 3:1 radially, the edge smoothstep +-0.15 of the noise | round black holes read as soot [frames] |
| D49 the eye inside a body | the fourth octave everywhere past 160 px | an eye-inside body (the box the whole screen) keeps three octaves | the eye-inside fighter 309.8 us with four against the 300 us fence; 279.5 with three [M] |
| D50 the wave's weights, history | 0.50 + 0.56 miss (D24) | 1.26 / 1.11 (the sheet, D30) -> 1.35 / 1.22 (v3: the ramp and the capped intensity, 1.354 / 1.215 [M]) -> 1.40 / 1.24 (v4: the lower ramp and the flame gaps, 1.401 / 1.242 [M]); the record's tolerance +0.03 | review 2026-11-07 |
| D51 review fixes | - | the wave's first seed read from disc.z (disc.y is the sine of a zero rotation: every wave had one break; the lab used the seeds), gate `wave_break_follows_seed` < 0.5; the strips shed first (0), the glows / halos before the smoke only with a wave in the frame; hull bursts with the eye inside keep three octaves (D49); the fourth octave's row a body seen from outside (2 R): 27.3 against 27.2 us with three over 0.27 screens [M], `budget_weight_fireball_fine4` repinned 1.35 -> 1.18 (the third's); the wave's band and sheet gains 0.15 / 0.8 -> 0.1 / 0.6 (with the seeds read right the edge over the band read 1.09-1.14 [M], now 1.51 [M, dev run]); the compose program not created while off (reason `off`), a body under compose_n_min gets an N 1 table; the eye-inside bound 0.9 with compose off; the wave refused (fault bit 10) and the Reset exercised with a capital's wave; strips dropped past the shimmer pass's room logged (`wave_strips_dropped`) and not charged | the review's findings |
| D52 the wave's material (v7) | edge + band + sheet profiles, the sheet's noise carving holes (D26, D38, D48) | an annulus of the fireball's material: s = (r_t - d') / band (r_t the front shifted by the tongues; 0 the leading edge, 1 the band's end, the sheet to s_end 2 as the eroded outer shell), rq = s / s_end the body's radial coordinate; three octaves (0.5 / 0.3 / 0.2) of a value noise periodic in the angle (`wave_noise_cells` 40 / 86 / 175 about the circumference) over s - `wave_advect` x at 0.8 / 1.7 / 3.5 cells a band; the body's erosion thr = 0.2 + 0.2 x + 0.5 rq^2 (1 + x) (`wave_erode_*`, x = t / L_w), soft 0.3; lobes from the break's mix (amp 0.45, soft 0.2); the density never under `wave_floor_a` 0.2 inside the band (fading over the sheet), x (0.6 + 0.4 m) of the break; the rim e^(-(z / 0.7)^2) ahead, over 0.05 R_f behind, x m | the user on the lab: a beige / olive ring with black soot blots, "not fire" [frames]; lab and dump frames now a broken orange-red ring with clumps and dark-red gaps beside the body's palette [frames, `stage-dump-wave/v7/`] |
| D53 the wave's ramp (v7) | T 0.9 / 0.6 / 0.4 on the profile (D47), cooling 0.72 | the rim T 0.9 over its 0.05 R_f; the material 0.55 -> 0.4 over the band, -> 0.25 over the sheet, the noise drawing it to the floor's 0.25 below the threshold (heat soft 0.3); the pixel's T the intensity-weighted mean of rim and material, its excess over 0.25 x sqrt(intensity) (a dim yellow-white reads grey or olive); cooling `wave_t_cool` 0.72 -> 0.5; `wave_e` 1.0 -> 1.6 | at E 1.0 the orange read brown in the lab; the rim's fall-off at T 0.9 read as a grey halo [frames]; mean chroma (dump sRGB) 0.717, hue 10-40 degrees for 1.000 of the lit pixels, mean 23.9 degrees [M] |
| D54 the hard edge | - | two causes: (a) the lab's vanilla inset, an unframed black square of a third of the width over the top right, cut the ring with a vertical edge (the user's "strip end"): framed, a quarter of the width; (b) v6's sheet (1 - s)^2 had s clamped at 0 ahead of the front, so it lit at full gain to the window r + 3.5 w_e and the strips' reach while the tongues lit to r + 5 w_e: the ring's outer side ended in a cut. v7: nothing ahead of the shifted front but the rim's (and, where the rim is broken, the material's) e^(-(z / 0.7)^2); the window 2.5 w_e about the shifted front; the quads reach r - (window_in + tongue) w_e .. r + (window_out + tongue) w_e (`wave_reach_in` / `_out`). Gate `wave_no_hard_edge` | the lab frames [frames]; worst 0.25-degree step along the band 0.204 of the brighter end (2 drops past 50 % over 2 degrees, both soft), outward 0 rises and the worst 1-px fall 0.283 of the ray's peak (v6's sheet fell by its whole value) [M] |
| D55 the broken circumference, measured | the share of 64 bins with a peak >= 10 % of the ring's in 0.40-0.85 | the bins' band means (0 .. 5 w_e behind each bin's edge): max / min >= 2 and CV >= 0.25, every bin's peak >= 5 % of the ring's | the band is never transparent (D52), so every bin passes 10 %: lit share 1.0 / 0.922 / 0.859 at 10 / 30 / 50 % [M]; arcs max / min 19.9, CV 0.558, the dimmest bin 0.149 [M] |
| D56 the cells' growth | - | the radial cells x sqrt(4 R_f / r) (`wave_noise_r_ref` 4, c14.z): the angular cells grow as r, the radial as sqrt(r), square at 4 R_f | the fixed angular count streaked the young ring radially 2:1 at 2 R_f [frames] |
| D57 the wave's weights and shed (v7) | 1.40 / 1.24 (D50); step 0a the sheet's second octave | 1.71 a drawn screen, 1.53 with the sheet's third octave shed (step 0a: the sheet loses its octave first, the band keeps three); program 727 slots (614 before) | wave alone 315.0 us, 1.712 a screen; shed 282.6 us, 1.529; the box 1.698 (5120x1440) [M]; the close capital: the sheet's octave shed on every wave frame, the wave drawn on 7 of 7, worst 351.3 us [M] |
| D58 the colour gate | `wave_fire_not_grey` on linear values | kept, plus `wave_fire_hue` in the dump's display encoding (sRGB of the clamped linear value): mean chroma >= 0.5, hue 10-40 degrees for >= 80 % of the lit pixels | linear chroma passed the beige v3 / v4 rings (0.71-0.80); beige is low chroma in display space [frames] |
