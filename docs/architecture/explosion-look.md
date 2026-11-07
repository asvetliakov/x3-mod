# Death explosions: the look per tier (step 3)

Ratified 2026-11-07 (main session); the death-row disassembly ([spawn note §10](../reverse-engineering/weapon-effect-spawn.md#10-death-row-element-layout-and-the-mask-seam-2026-11-07)) folded in the same day: the mask is a clone row R′ selected by a death-column swap, no new patch site.

Design note for step 3 of the weapon-effects pass, amending [weapon-look.md](weapon-look.md) 3.2 (the explosion plan)
with the research of [explosion-references.md](../reference/explosion-references.md) and the burst body of
[hull-burst-look.md](hull-burst-look.md) (the program family explosions share, `src/effects/weapon_fireball_ps.hlsl`,
and its built constants, section 10 there). Written 2026-11-07 from the stage's frames and the notes, not from a
flight; nothing here is built. Marks: **[C]** chosen, **[S]** from a source (a note, the data, the source), **[M]**
measured, **[I]** inferred, **[A]** assumed, **[U]** unknown. Where this note is silent, 3.2 stands (records, the
asteroid tier native, the missile rule-(b) forward, drift, occlusion classes, the `weapon_effects` token, sound).
Sizes: `verification/results/explosion-look/size_derivation.py` → `size_derivation_out.txt` (every pixel figure below).

## Outcome

1. **Pre-roll: yes for capitals and stations, on the engine's own hull copy.** The native spawn `0x00413610` runs
   for every big-tier death we own with the type record's death column pointing, for that one call, at a clone row R′
   of its row: every sprite and scene element `DISTANT`, the `PROPAGATE` element dropped (our secondaries replace the
   engine's 30 children), `FADEOUTBODY` kept and retimed to our body's birth P, one empty `TIMEOUT` element holding the
   carrier to P + 1 s. The carrier (the dying root body) is the picture through the pre-roll; the body then eats it as
   it fades over the engine's fixed 1 s. No wreck ever appears natively (the kill's third argument is 0 on every native
   caller), so none is lost. Corvettes take the same R′ with P = 0; fighters and missiles skip the spawn.
2. **One body law, the burst's**: the built three-octave eroding lobed impostor (erode 0.10 / 0.20, heat_soft 0.20,
   advect 1.0, radial term x (1 + 3x), lobes 5 at 0.25, rim_t 0.45) at R_f = f R_obj clamped per tier: fighter 2.0
   [12, 50] m, corvette 1.5 [30, 160], capital 0.9 [80, 600], station 0.6 [200, 1,800], missile 3.5 [6, 25]. Born at
   R_0 (not 0: the hull it replaces has a size), rising e-folded over the tier's rise, hanging to 0.8 L while it erodes
   and cools, growing 0.1 R_f more so it never stops, gone at L. Lives 2.0 / 3.0 / 4.0 / 6.0 / 0.8 game s plus the
   pre-roll 1.5 / 3.0 s for capital / station.
3. **Flash**: the burst's 40 ms four-arm star, arms 0.4 R_f, core 0.08 R_f, no halo, capped at 0.25 H, born on the body's
   frame (the pre-roll's secondaries each carry their own small one).
4. **Shock**: corvette and up, and missiles: a thin sphere shell drawn by its chord law (limb about six times the
   interior: a sphere, not a Praxis disc), r = 4 R_f (t / 0.3 L)^0.5, alpha (1 − t / 0.3 L)², never the brightest element,
   not drawn once it has passed the eye.
5. **Embers and fragments** random per event: n from 4-6 seed cones at a constant speed clamp(1.5 R_f / s, 40, 300) m/s
   plus the hull's velocity, three lifetimes to 1.2 L, cooling on the ramp; one in eight a fragment (a dark multiply
   streak with a hot tip, 0.5 v_e, to 1.5 L). Big tiers launch them from the hull box's surface, not the centre.
6. **Light**: the body's glow on nearby hulls and the fading hull copy, R 5 R_f, 0.33 E_b, the body's heat and life; during the
   pre-roll the dying hull is lit from t = 0 (E 0.4 → 0.8, T 0.6) plus each secondary's own glow.
7. **Smoke on in the first flight** (amending 3.2): the burst's surface-bound multiply puff at 1.1 R_f from 0.5 L to
   1.6 L; it darkens the hull copy and neighbours, never the sky; it is the first thing shed on a close capital.
8. **Budget**: a close capital at the body's birth charges 373 µs with nothing shed, 346 with the third octave shed, 259
   with the shock and the secondaries shed [I from the measured weights]; the fence stays 450 µs. Shed order and the
   built hysteresis in section 7; the body, the flash and the embers are never shed.
9. **Eye inside, own death, nested kills** need no special law: the chord starts at the near plane; a coverage-dependent
   body dimming replaces 3.2's E 2 cap if the meter gate fails; three fighters in one blast share the burst's cluster
   saturation so they do not sum to a sun.
10. **Proof before the flight**: dump cases per tier at the tier's distance, the close capital, the nested kill and the
    eye inside, at 1080p and 5120x1440, measured by `measure.py`'s 10 %-of-peak method with the dump split into parts;
    kill criteria per tier in section 10. A flight settles only the drift unit, whether the root-body carrier reads as the hull, the
    exposure in play and whether the pre-roll reads as suspense or as a wait.

## 1. The hull copy decides the pre-roll

What the engine does at a death [S, spawn note §4 and §10]: `0x00413610` allocates a class-`0x1b` carrier whose
subtype is the dying root body (the root body only, not the part hierarchy), copies position, rotation and velocity,
and spawns the row on it; no ship or station death row has `HIDEOBJECT`, so **the carrier shows the hull**. The row's
elements are sprites and scenes with flags, not body operations: `CUT` makes a multi-part 2D scene (it does not cut
the hull), `FADEOUTBODY` fades the carrier's root node over a fixed 1,000 ms from the element's start, `CUTKEEPBODY`
is the 22 s debris scene, and `WRECK` fires only when the kill's third argument is non-zero, which every direct native
caller (collision, area damage, five more sites, the KC native) passes as 0: **no wreck appears on a native death**.
The capital rows 37 / 201 (stock) and 602 (Mayhem) carry `EDF_PROPAGATE`: before the main spawn the engine makes a
second carrier (a full scene copy) and 30 (40) children of row 149 (CUT scene sprites, 3,840 ms) at staggered starts
on its surface, and the main t0 is their last start: 1.8 s (stock 37), 1.6 s (201), 6.0-6.4 s (Mayhem 602). So the
engine already has a pre-roll, and the hull copy fades over the second after it, not over 22 s; the corvette rows 40 /
601 keep the hull to 9.03 s and the fighter row 35 / 600 to 3.11 s with no fade. The original object is removed by the
kill path [I] (`0x0044aab0` frees every effect instance of a removed object [S]). Skipping the spawn removes the hull at
t = 0; forwarding it unmasked plays the sprites beside ours.

Elements are never copied per instance (`inst+8` is the static row, re-read every update [S, §10.5]), so a per-instance
mask is impossible. The hook contract **[C, ratified]**:

- The death stub (the `claim_call` pattern of the impact and muzzle sites, spawn note §6: registers, SEH frame,
  LastError and the x87 state preserved, the stack untouched) classifies the object (class 7 / 5 / 6 / 10 by the reads
  of §8.1, the tier by `patch_tier_of_ship`), records the death, and for the corvette, capital and station tiers
  **writes R′ into the type record's death column** (TShips `rec+0xbc`, docks and factories `rec+0x60`), forwards
  `0x00413610`, and restores the saved original after it returns. The write and restore are LIFO so that nested kills
  inside one outer call (area deaths, §6) each save and restore their own column. No new patch site.
- **R′** is a copy of row R in a free Effects slot (installed 901-999, stock 203-999; count, flags, `+8`, `+0xc`
  copied, `EDF_PROPAGATE` cleared so no second carrier is made), its element array allocated with the EXE's own
  `malloc` `0x005112c4` so the engine's reset frees it with its matching free `0x0050e1b0`. Per element: every sprite
  and scene element `+0x3c |= DISTANT` (gone at its start, never drawn); the `PROPAGATE` element dropped; `FADEOUTBODY`
  kept on an empty node with its start `+0x40` = P (capital 1,500 ms, station 3,000 ms, corvette 0); one empty
  `TIMEOUT` element ending at P + 1,000 ms holds the instance and the carrier alive to the fade's end; nothing else
  (no `WRECK` timer: it never ran natively). Built once per row at the latch or on first use, rebuilt when slot R′
  reads count 0 (a reload zeroes it) or R's `+0x10` has changed; never freed by us (instances keep `&slot[R′]` to
  their end).
- Rollback is the unswapped forward (`explosions` absent from the token, `boom::tier_native`, the asteroid tier, the
  rule-(b) missile), byte-exact as today's observe forward; the clone slots are inert data the engine's reset frees.
- **Fighters and missiles** (rows 35 / 600, 39, 158-161) **skip** the spawn as the shield hit does (`xor eax,eax; ret`
  into the caller's `add esp`): the carrier is never made and the body goes at t = 0; the body born at R_0 = 0.5 R_obj
  hides it at 600 m (a 7 m ball plus a star over a 22 m hull for three frames, then 22 m by 0.3 s).

The pre-roll is therefore not a delay of the native death (the node dies at the site) but the engine's own hull-copy
time, used for our secondaries instead of its children, with P 1.5 / 3.0 s close to the stock 1.8 s and well under
Mayhem's 6 s; the user's "too short" history is on the side of the pre-roll, not against it. Whether the root-body
carrier alone (no parts) reads as the hull is section 11, item 1.

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
No gradient shading now: the lobes (5 cells, amp 0.25) and the first octave (1.6 cells per R_s) are the low-frequency
structure; gate `death_body_low_frequency` (section 9) says whether it is enough, and the one-tap normal of the
reference (two slots per sample) is the fix if not.

### 2.2 Flash [C]

The star as built (`star_arm_len` {1.0, 0.55, 0.8, 0.45} x A, width 0.10 A (1 − s), core 0.12 of the arm → here A =
0.4 R_f, core 0.08 R_f, ≥ 3 px, arms ≥ `star_floor_px` 6), white E 4 core, arms heat 0.9 at E 4 falling, 40 ms game,
three-frame floor, no halo, capped at 0.25 H on screen, born on the body's first frame. A secondary's star is the same
at A = 0.4 R_s. Pixels at the tier's distance: fighter arms 18 px / core 3.6; corvette 33 / 6.7; capital 154 / 31;
station 138 / 28; missile 13 / 3 [M arithmetic].

### 2.3 Shock [C]

Corvette, capital, station, missile; none for fighters (one body that snaps to size). A second ray-sphere quad,
`weapon_shell_ps` family, density by the chord through a shell of radius r and width w = 0.06 r:

    ρ(b) = 2 w r / sqrt(max(r² − b², (2 w)²)) / (2 sqrt(2 r w))      b the impact parameter; 1 at the limb
    r(t) = 4 R_f (t / τ)^0.5, τ = 0.3 L (0.4 L station);  alpha = (1 − t / τ)²;  colour heat(0.9) x 1.0 x alpha

The interior is 1 / sqrt(r / (2 w)) ≈ 0.17 of the limb (a sphere seen through, never a flat ring, never empty), early
out beyond the limb. Not drawn once |eye − c| < r (the shell has passed the eye); the box is the full sphere, charged
at the shell's own weight (`shock_alone_fills_frame`, [I] ≤ 0.40: no noise). Missile: τ = 0.3 L = 0.24 s, the shell
is the read at distance.

### 2.4 Embers and fragments [C]

The sprite batch's streak quads with the shared vertex law, with one addition: τ ≤ 0 selects the linear branch
p(t) = p0 + v t (a 1e6 s τ loses 6 % to float precision; the Expanse shell "never slows"). Per death n embers:

- p0: fighter and missile at the centre ± 0.3 R_f hashed; corvette, capital and station on the surface of the hull box
  `BubbleCache` holds (else the axis capsule of 2.6) — big ships eject from their hull, not from a point 600 m inside
  the fireball.
- v = v_e u + v_hull, u from 4-6 seed cones (axis and half-angle 25° hashed per event, embers hashed within), v_e =
  clamp(1.5 R_f / s, 40, 300) m/s: fighter 42, corvette 196, capital and station 300, missile 40. Reach at 1.2 L:
  3.6 / 5.4 / 2.4 / 2.0 / 3.7 R_f [M arithmetic]. Speed in m/s does not grow with the tier past the cap; reach does.
- lives 0.4 / 0.8 / 1.2 L by thirds, each x (1 ± stagger / 2); E e^(−t / τ_e) with τ_e = 0.5 L from `spark_e` 8;
  colour heat(T), T from 1 to 0 over the ember's own life (white → orange → dark red); width 0.08 U, the shared
  streak rule (previous to current screen position, cap 6 rests, `floor_len_px` 24).
- counts 32 / 64 / 128 / 160 / 12; the capital's and station's split: half from the secondaries during the pre-roll
  (8 per secondary), half at the body's birth.
- fragments n / 8: thick (0.25 U) dark streaks at 0.5 v_e, life 1.5 L, tumbling by a hashed phase in the width; drawn
  twice, in the scorch pass (ZERO / INVSRCCOLOR, darkening 0.6, surface-bound: over the hull copy and neighbours, nothing
  over sky, where a dark thing is invisible anyway) and additively as a 0.3-length hot tip at heat(0.6) cooling.

### 2.5 Secondaries and the pre-roll [C; cadence M arithmetic]

A secondary is the burst body at R_s = 0.35 R_f (the last two of a chain 0.5 R_f; two of the station's 0.6 R_f), life
L_s = 0.3 L, ρ 0.1, E 1.5, with its own star (2.2), glow (1.6 R(t), 0.33 E) and 8 embers, at a hashed point in the
hull box (BubbleCache; `weapon_death bubble_cached=` says how often it is there, unknown 16 of weapon-look) or, without
a box, in an axis capsule of length 1.4 R_obj and radius 0.25 R_obj along the node's forward axis (the carrier copies
the rotation, so it is at the site; the row offset is [U], the observe flight's `weapon_death` logs it). A sphere of
0.7 R_obj (3.2) is rejected: on a 1.8 km hull it puts bursts in empty space.

Cadence: geometric, ratio 0.7 capital (8 bursts over 1.5 s: gaps 0.49 0.34 0.24 0.17 0.12 0.08 0.06 s), ratio 0.8
station (12 over 3.0 s: 0.66 0.53 0.42 0.34 0.27 0.22 0.17 0.14 0.11 0.09 0.07 s), every gap x (1 ± 0.2) by the seed.
The chain accelerates into the body (EVE's "somewhat exponentially faster", FS2's pre-death cluster [S, references]).
Corvettes get none (an M6's hull sits inside its own R_f = 1.5 R_obj body; a pre-roll would be two pops on an 83 px
hull and a 0.75 s wait: the delay failure at small scale), fighters and missiles none.

### 2.6 Light on hulls [C]

The glow primitive (one lane fetch, `(1 − d² / R²)²`, back faces) at R = 5 R_f, E_g = 0.33 E_b x T(t), drawn while the
body is drawn: on neighbours and on the fading hull copy alike (surface-bound: whatever the lane holds is lit). During the
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

| Tier (`patch_tier_of_ship`; station classes 5/6; missile class 10) | R_obj typ. m [M] | dist. m [A] | R_f law [C] → m | body px radius at dist., 1440 / 1080 [M arith.] | hull px | L s + pre-roll [C] | Phases | Embers / fragments | Shock | Secondaries |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| fighter (M3-M5) | 14 (M5 12, M3 19) | 600 | 2.0 R, [12, 50] → 28 | 45 / 34 | 35 | 2.0 | star, body from 0.5 R_obj, embers, glow, smoke 1.0-3.2 s | 32 / 4 | none | none |
| corvette (M6, M8, TS, TP, TM) | 87 (TS 50) | 1,500 | 1.5 R, [30, 160] → 130 | 84 / 63 | 83 | 3.0 | star, body from 0.5 R_obj, shock 0.9 s, embers, glow, smoke | 64 / 8 | yes | none |
| capital (M1, M2, M7, TL) | 1,288 (M7 618) | 1,500 | 0.9 R, [80, 600] → 600 (M7 556) | 384 / 288 (0.53 H diameter) | 1,183 | 4.0 + 1.5 | hull glow from 0; 8 secondaries 0-1.5 s; star + body from 0.5 R_f at 1.5 s; shock to 2.7 s; embers to 6.3 s; smoke 3.5-7.9 s; the hull copy fades 1.5-2.5 s | 128 / 16 | yes, largest | 8 (134 px each) |
| station (docks, factories) | 1,800 (0.5-52 km, loose spheres) | 3,000 | 0.6 R, [200, 1,800] → 1,080 | 346 / 259 | 1,152 | 6.0 + 3.0 | hull glow; 12 secondaries 0-3 s (two of 0.6 R_f); body from 0.5 R_f at 3 s, rise 1.2 s; shock to 5.4 s; embers to 10.2 s; smoke 6-12.6 s | 160 / 20 | yes, 0.4 L | 12 (121 px) |
| missile | 3 [A] | 300 | 3.5 R, [6, 25] → 10.5 | 34 / 25 | 19 | 0.8 | star, body, shell 0.24 s, 12 embers | 12 / 0 | fast, thin | none |

Against the references [S]: the fighter body's diameter is 2.6 hull lengths (film and Everspace 2-4), the corvette's
2.0, the capital's 0.65 (HW3 / BSG 0.5-1: the hull is the picture, the ends stick out 325 m), the station's 0.6 of its
loose sphere. Floors: a body never under 12 px of radius at 1440 rows (9 at 1080; the 30 px case held structure at
two octaves [M]; a fighter reaches the floor at 2.2 km, a missile at 840 m), the star core 3 px and arms 6 px, an
ember 24 px; floors raise size, never radiance. The ceiling is the budget (section 7), not a pixel count: a station
fills the view only with the eye inside it. Vanilla for scale [M]: rows end at 3.1 s (fighters), 9 s (M6), 22 s
(capitals, the debris scene; the hull copy itself fades at 1.8 s stock, 6.0-6.4 s Mayhem); ours end at 7.9 s with the
hull gone at P + 1 s, and the fragments and smoke are what is left behind.

## 4. The capital and station timeline

Capital at 1.5 km, L 4.0, pre-roll P 1.5 (station in brackets, P 3.0, L 6.0):

| Game time | What is drawn |
| --- | --- |
| 0 | the carrier shows the root body (R′ spawn, no children, no second carrier); our hull glow E 0.4 at T 0.6 over R_obj; secondary 1 (0.35 R_f, 134 px) with its star and 8 embers at a hashed hull-box point |
| 0 … P | secondaries 2-8 (2-12) at the geometric cadence, the hull glow rising to E 0.8, each secondary lighting the hull locally; the last two at 0.5 R_f (station: two of 0.6 R_f among them); half the embers launched; the hull is lit from inside and never hidden (the biggest secondary is a third of the body) |
| P (t_b) | the star (arms 240 m / 154 px; 432 m / 138 px), the body born at 0.5 R_f, the shock from 0, the other half of the embers and all fragments from the hull box surface at 300 m/s, the glow on hulls at R 3 km (5.4 km); the hull copy starts its 1 s fade (`FADEOUTBODY` retimed to P) inside the growing body |
| t_b + 0.6 (1.2) | the body at R_f (rise ρ L); holes from t_b + 1.6 (2.4) |
| t_b + 1.0 | the hull copy gone, the carrier removed (the `TIMEOUT` element ends); the body is 0.8 R_f and covers where it was |
| t_b + 1.2 (2.4) | the shock at 4 R_f and gone |
| t_b + 2.0 (3.0) | smoke begins over neighbours (the hull copy is gone); the body at T 0.5, orange, sparse clumps by 0.8 L |
| t_b + 3.2 … 4.0 (4.8 … 6.0) | final fade; embers dark by t_b + 4.8 (7.2), fragments by t_b + 6 (9), smoke by t_b + 6.4 (9.6); nothing native remains (no wreck, no debris scene) |

Corvette: the same R′ with P = 0, so the hull fades over the first second of the body (R_0 = 0.5 R_obj at t = 0, R_f by
0.45 s) and the carrier goes at 1 s; vanilla keeps it to 9 s. The engine's 1,000 ms fade is fixed (read from the static
record every update); P is ours per tier and is the only timing in R′ we set.

## 5. Eye inside, own death, nested kills

- **Eye inside** (a ram, the own ship's death, the close capital after t_b + 0.3 s at 720 m): the impostor's box
  contains the eye and rasterises the screen; the chord runs from the near plane (`u0 = max(−half_chord, (near −
  zm) l)` as built) to the far side or the lane; the density is the view-plane noise at small |q|, so the screen is
  hot clumps and dark holes at the body's current T, not a flat colour. The shock is behind the eye (not drawn), the
  glow lights the cockpit and the own hull (surface-bound), secondaries are around. Charge: one screen of body plus
  the glow, 259-281 µs [I]. If `death_exposure_meter_shift` fails (> 0.5 EV through the production meter), the body's
  E_b scales by 1 / (1 + 0.5 max(0, cov − 0.5) / 0.5) with cov its screen coverage (1.5 → 1.0 when it fills the frame;
  untouched under half a screen); the flash, small in area, carries the brightness. This replaces 3.2's "capped at
  E 2", which is above `body_max` and so did nothing.
- **Own death**: no own-ship dimming (2.9: explosions are not dimmed); the chase camera 24 m behind a fighter is inside
  its 28 m body, so the chase view is the eye-inside case too; the cockpit sees the same. Drawn until the engine's own
  death screen replaces the view; nothing special.
- **Nested kills** (three fighters in one area blast, spawn note §6): three records, three stubs, three bodies within
  a frame; the burst's cluster rule over death bodies of the same event frame (N by remaining life, `cluster_n0_burst`
  1.6) keeps three overlapping bodies under 2 E_b; three stars in one frame are fine (40 ms). Each keeps its own seed,
  cones and lobes: three different balls, not one in triplicate.

## 6. Hull light

Section 2.6 is the law; what it buys per tier: a fighter lights a hull within 140 m (5 R_f) for 2 s at a third of the
body; a capital lights 3 km around it for 4 s, and its hull copy from inside for 1.5 s before; a station 5.4 km. The glow's
box fills the screen in any capital death within 3 km, at 0.40 of the decal per screen: 71 µs always, and it is shed
at step 4 on a close capital only.

## 7. LOD, budget and the shed order

Charges by projected box area x weight [M, 5120x1440]: fireball 1.18 (three octaves) / 1.03 (two), smoke 1.20, star
0.40, glow 0.40; shock [I] ≤ 0.40 (pinned by `shock_alone_fills_frame`); ember and fragment quads negligible (≤ 2,048
quads, each ≤ 1 % of a screen) [I]. At the tier's distance a death is cheap (a capital at 1.5 km: 0.08 screens, 17 µs
of body); the whole cost is the close case:

| Close capital (eye at 1.2 R_f = 720 m; body 800 px radius: the screen) at t_b + 0.1 s | µs [I from M weights] |
| --- | --- |
| body 3 octaves + shock + glow + two secondaries + star | 209 + 71 + 71 + 16 + 5 = 373 |
| third octave shed | 346 |
| shock and the secondaries shed | 259 |
| smoke at 0.5 L (one screen x 1.20) | + 213 → shed first |
| fence (`death_budget_capital_close_5120`) | ≤ 450; the 3-screen ceiling 532 |

Shed order within the shared 2.11 order (the family's items only): (1) the third octave of every fireball, with the
built hysteresis (enter past the budget for 3 frames, leave under 0.8, weighted in over 48-64 px of projected
radius); (2) death smoke, largest box first; (3) the shock; (4) death glows of owners other than the own ship, largest
first; (5) secondaries past the first two live (they are the body's prelude, not its substance); never shed: the body,
the star, the embers and fragments, anything of the own ship. Count caps: 32 fireballs + 64 secondaries, 2,048 sprite
quads (a nested kill of ten fighters and a capital is 450). CPU per death: one record, the seed, the cone table, ≤ 180
quads written once (p0, v, t0, seed); the pre-roll is a per-frame cadence check, no pool and no simulation.

## 8. Lab scenarios (`boom/…`, `weapon_effects_lab.html`, the vanilla row beside ours)

`fighter-m5-600m-side`, `fighter-m3-2km-floor`, `corvette-m6-1500m`, `freighter-ts-1500m`, `capital-m2-1500m-preroll`
(the hull drawn by the lab as a box of 2 R_obj x 0.5 R_obj so the pre-roll has something to light), `capital-m7-far-5km`,
`capital-m2-close-720m` (the eye inside from t_b + 0.3), `station-factory-3km-preroll`, `missile-300m`,
`missile-near-own-ship-40m`, `nested-3-fighters-area` (three deaths in one frame, 60 m apart), `own-death-chase`,
`own-death-cockpit`, `seta6` (every preset at the SETA rate). Controls: per tier f / clamp / L / pre-roll / embers /
secondaries, the cadence ratio, the rise ρ, R_0, v_e, the shock's τ and width, `smoke`, the ramp stops, the coverage
dimming. The lab plays the same laws (the burst's noise is already mirrored).

## 9. Fixture gates (`EXPLOSION_*`; dump cases at 1080p and 5120x1440; the parts `-flash`, `-body`, `-shock`, `-ember`, `-fragment`, `-glow`, `-smoke`, `-secondary` written as the hull dump's `-burst` / `-glow` are; `verification/results/weapon-effects-burst/measure.py` extended to those parts: 10 % of the part's own peak, equivalent radius, blobs, peak)

| Gate | Case, time | Must hold |
| --- | --- | --- |
| `death_tier_sizes` | each tier at its distance, 0.1 / 0.3 / 0.6 / 0.8 / 1.0 L after t_b | body 10 % radius within 5 % of R(t) f / z over the five ages; the clamps on an M7 (556 m) and a shipyard (1,800 m); the floors on `fighter-m3-2km-floor` (≥ 12 px) |
| `death_body_contrast`, `_silhouette_lobed`, `_erodes_outside_in`, `_colour_core_rim` | the burst's four gates on the fighter and capital bodies, median of 8 seeds | the burst's bounds (dark share ≥ 0.15, high-pass std / peak ≥ 0.10; max / min ≥ 1.25; lit share ≥ 0.8 / 0.35-0.75 / ≤ 0.15 / 0; core T ≥ 0.75, rim ≤ 0.5) |
| `death_body_low_frequency` | capital body at 0.3 L, 1080p | the R/2 low-pass of the body minus its radial mean: std / peak ≥ 0.08 (a flat ball 0) |
| `death_no_pop_at_birth` | each tier, frames t_b − 1, t_b, t_b + 1 | the body's first frame ≥ R_0 f / z; the star under 0.25 H; no element's radiance rises more than 2x between consecutive frames except the star's birth |
| `death_flash_star_3_frames` | every tier, 16 and 100 ms steps | lit on exactly max(3, 40 ms) frames; second-moment ratio ≥ 2; ≤ 0.02 at 0.5 A off the arms |
| `death_shock_is_a_shell` | corvette and missile at 0.1 / 0.2 τ | radius within 5 % of the law; interior / limb in 0.10-0.30; the shock's peak ≤ 0.7 of the body's; nothing drawn after it passes the eye (close capital) |
| `death_embers_random_and_cooling` | fighter, two seeds | the cone directions differ between seeds (angular histogram over 36 bins: correlation < 0.5); mean ember hue at 0.3 / 0.7 / 1.1 L on the ramp T ≥ 0.75 / 0.5 / ≤ 0.3; the farthest lit ember at 1.2 L within 10 % of v_e 1.2 L f / z; a fragment darkens a 0.5 hull by ≥ 0.3 at its tip's neighbour and is invisible over sky |
| `death_preroll_cadence_and_hull_light` | capital 1.5 km | 8 secondary births at the cadence ± 20 %; each inside the hull box; the hull glow's peak at 0 / P in 0.4 / 0.8 x 0.33 E_b ± 10 %; the body absent before P and born at 0.5 R_f at P |
| `death_glow_is_the_bodys_light` | fighter, capital at 0.1 / 0.3 L | glow peak / body peak ≤ 0.4; glow 10 % radius ≤ 5 R_f f / z; none on sky; none after L |
| `death_smoke_darkens_surfaces_only` | capital over a 0.5 hull (a neighbour) and sky at 0.8 / 1.6 L | min ≤ 0.5 (1 − 0.6 s0) over the hull; sky unchanged; nothing after 1.6 L; ≤ 1 code residual 8 frames later through `TemporalPass` |
| `death_cools_through_ramp`, `_fades_to_zero_at_life` | each tier | mean hue at 0.2 / 0.6 / 1.0 L on T ≥ 0.75 / 0.5 / ≤ 0.25; every part 0 at its life (smoke 1.6 L) |
| `death_eye_inside_bounded` | `capital-m2-close-720m` at t_b + 0.5, `own-death-chase` | the frame lit over ≥ 90 % of its area with dark share ≥ 0.15 (clumps, not a wash); ≤ 300 µs fenced at 5120x1440 |
| `death_exposure_meter_shift` | the same frames through the production meter | ≤ 0.5 EV, else the coverage dimming on and the gate re-run |
| `death_nested_three_drawn` | `nested-3-fighters-area` | three records, three bodies with distinct lobe seeds (silhouette correlation < 0.8 pairwise); peak ≤ 2 E_b |
| `death_budget_capital_close_5120` | the close capital over t_b … t_b + 2 s | ≤ 450 µs fenced every frame; the shed state logged per frame; with every step shed ≤ 300 |
| `death_octave_lod_hysteresis` | the close capital crossing the budget | enters after 3 frames past it, leaves under 0.8; no frame-to-frame body image change > 2x the blended step's |
| `death_temporal_energy` | fighter at 600 m through `TemporalPass` | energy ≥ 0.9 of the stage's |
| `death_seta6_three_frame_floor` | every tier at SETA 6 | the star 3 frames; every element's game life unchanged; wall time 1/6 |
| hook record | `death_skips_native_fighter_missile`, `death_swaps_death_column_big_tiers`, `death_forwards_asteroid_and_rule_b_missile`, `death_nested_lifo_restore`, `frame_kept` | callee skipped (fighter, missile); callee entered with the death column reading R′ and the original restored after return (corvette, capital, station), R′'s elements `DISTANT` except the `FADEOUTBODY` node at P and the `TIMEOUT` node at P + 1 s, `EDF_PROPAGATE` clear; forwarded byte-exact for asteroids and the rule-(b) missile; nested kills each save and restore their own column (LIFO); EBX/ESI/EDI/EBP, DF, MXCSR, x87 CW, LastError, ESP as the impact stub's; R′ rebuilt after a reload (slot count 0) |
| budget rows | `body_alone_fills_frame` (3 / 2 octaves), `shock_alone_fills_frame`, `smoke_alone`, `star_alone` | weights pinned in the header |
| Reset, fault, refusal, slots | as every family | programs created at the latch; `MaxPixelShader30InstructionSlots` logged; the native validator harness on every generated ps_3_0 |

The user judges the look from the dumped frames (both sizes) of `fighter-m5-600m-side`, `capital-m2-1500m-preroll`,
`capital-m2-close-720m`, `nested-3-fighters-area` and `own-death-chase` before the flight, as for the burst.

## 10. Kill criteria per tier (a flight observation flips a header, never adds a setting)

| Tier | Observation | Next build |
| --- | --- | --- |
| any | the body reads as a disc, a lamp, a balloon or a bubble | the burst's steps first (erode_t 0.25, then the star's arms x 0.6); then `boom::mode` simple: the same body, shock and secondaries off, embers halved, smoke off (never a smoother ball) |
| any | the wrong size | that tier's (f, clamp) only; the pixel floors untouched |
| any | too short / too long | that tier's L only; the pre-roll separately |
| fighter | sparks unseen at 600 m | ember width 0.08 → 0.12 U, then count x 1.5 |
| corvette | the shock reads as a ring or as slow motion | width 0.06 → 0.04 r and τ 0.3 → 0.2 L; then the shock off for the tier. **Applied 2026-11-07 from the frames (D14): the shell law as drawn is rejected for every tier (`boom::shock_tiers` 0); the planned replacement is a refraction limb through the engine shimmer pass, not a thinner ring** |
| capital | the pre-roll reads as a wait ("why does it hang") | P 1.5 → 0.75; then 0 with the secondaries spread over 0 … 0.4 L beside the body (3.2's shape) |
| capital | the hull is hidden by the body | f 0.9 → 0.7 |
| station | the view is covered for more than a second | clamp 1,800 → 1,200, then `station_secondaries` 12 → 6 |
| missile | unseen | f 3.5 → 5; the shell's E 1.0 → 1.5 |
| any | the exposure pumps | the coverage dimming on (section 5), then the body's E_b 1.5 → 1.2 |
| a tier that still reads wrong | | `boom::tier_native` for that tier; the others keep theirs |

## 11. Unknowns and what settles each

| # | Unknown | Settles it |
| --- | --- | --- |
| 1 | Whether the main carrier (the root body only, no part hierarchy) reads as the hull during the pre-roll, or whether the second carrier's full scene copy is what the player sees natively (both carriers drawn? [U, spawn note §10.4]) | the observe flight (native) beside the first R′ build: if the root body lacks the parts that make the silhouette, R′ keeps `EDF_PROPAGATE` with a child row R″ of one empty `TIMEOUT` element so the scene copy exists and the fade is applied to it instead |
| 2 | The hull copy's native timing: **answered** [S, §10.4, census timelines]: `FADEOUTBODY` is a fixed 1,000 ms from the element's start, t0 = 1.8 s (37), 1.6 s (201), 6.0-6.4 s (602); rows 40 / 601 hold the hull to 9.03 s, 35 / 600 to 3.11 s, no fade. R′ sets the start to P | — |
| 2a | Whether the combat-death script path (`"KilledBy"`) passes a non-zero third kill argument, which would have run `WRECK` natively | the observe flight logs `arg4` at the death stub; if ever non-zero, R′ needs the empty `WRECK` timer node of §10.5 (b) to keep the native wreck for that path |
| 2b | When a scene part counts as finished (part `+0x1f4` bit 0) and so the real life of the 22 s debris element on native rows | not needed by R′ (every scene element is `DISTANT`); it bounds only the vanilla comparison in the lab |
| 2c | R′'s lifetime past a device reset and across the engine's Effects reload (the reset frees the element arrays with `0x0050e1b0`, a reload zeroes the slot) | the fixture's Reset case plus a reload case: R′ rebuilt when the slot reads count 0, never freed by us; the hook record counts rebuilds |
| 3 | The node velocity unit (drift) and the forward-axis row for the axis capsule | the observe flight's `weapon_death` rows (unknown 1 of weapon-look) |
| 4 | Whether a dying owner is in the bubble cache (secondaries and ember launch points) | `weapon_death bubble_cached=`; under 0.5 the axis capsule is the default and the cache the bonus |
| 5 | The shock's and the fragment's weights; the three-octave body at a full screen natively (the alone row measured 166.7 µs at 5120x1440, the dense-frame charge 1.18 = 209 µs [M]) | the `*_alone_fills_frame` rows |
| 6 | The exposure meter under a frame-filling body (unknown 7 of weapon-look) | `death_exposure_meter_shift`, then the flight |
| 7 | Whether the lobes and first octave carry enough low-frequency structure at 800 px | `death_body_low_frequency`; gradient shading (two slots per sample) if not |
| 8 | Whether the pre-roll reads as suspense or as a delay, and whether the 1 s hull fade inside the body reads as the ship coming apart or as a vanish | only the flight (kill row in section 10; the fade's kill step is P + 1 → P + 0.5 by moving the `TIMEOUT` end, the fade itself is fixed) |
| 9 | Which deaths the user meets most (area kills, collisions, missiles) | the observe rows' reason-word histogram from the flight |

## 12. Options considered and why they lose

- **The reference's pre-roll with the spawn skipped** (hull gone at t = 0, 1.5 s of small bursts in empty space, then
  the body): the "delayed fireball" failure and "nothing left"; the user would see a 1.8 km ship blink out. Lost to the
  R′ forward, which keeps the engine's own hull copy through the pre-roll.
- **A hulk stand-in** (an ellipsoid variant of the impostor, dark, eroding, over the hull box during the pre-roll):
  a new program variant and a dark blob where the ship was, a soap bubble in black; lost to the native carrier, which
  is the real body.
- **Forward the spawn unmasked and draw ours over it**: vanilla sprites, 30 children and the debris scene for 22 s beside ours; the acceptance ("no
  native explosion sprite") forbids it.
- **A shipped Effects table override with sprite-less rows**: a data install footprint that conflicts with Mayhem's
  own rows 600-602 and FL's; lost to R′ in a free slot, built in memory behind the existing stub.
- **A per-instance phase flip after the call** (spawn note §10.5 (a)): no allocation, but it cannot hold the carrier
  alive to P + 1 s (a done element does not count for liveness) and must free start-0 nodes; lost to R′.
- **A redirect of `0x0041367d`** (the row-choice call inside `0x00413610`): a second patch site that is not atomic
  (it crosses `0x00413680`); the death-column swap needs none.
- **Keeping the engine's 30 children as the pre-roll** (R′ with `PROPAGATE` kept): 3,840 ms 2D scene sprites at the
  vanilla size and look beside our secondaries; the acceptance forbids native sprites, and our cadence is the design.
- **3.2's secondaries after the body** (spread over 0 … 0.4 L beside a body born at t = 0): every reference puts the
  chain before the blast; kept as the capital's second kill step, not the first version.
- **Damped embers** (the shared τ law at τ 1 s): they stop inside a capital's body and never form the shell that
  outlasts it; the linear branch costs one vertex-program branch.
- **Embers from the centre for every tier**: on a capital they spend 2 s inside the fireball; the hull-box surface is
  where a hull comes apart.
- **A flat annulus mesh for the shock** (cheaper than the sphere box): a Praxis disc by construction; the chord law
  through a sphere box with an early-out costs ≤ 0.40 of a decal per screen and reads as a sphere.
- **Smoke off in the first flight** (3.2): the burst's smoke passed its residual gate; off, a fighter death ends in
  nothing and a neighbour's hull is undarkened; the shed order handles the close case.
- **Gradient shading now**: 50 % more hashes for a quality the lobes and first octave may already give; the gate
  decides.
- **A noise atlas**: the burst's fallback (hull-burst-look 2.8) if `body_alone_fills_frame` at three octaves breaks
  the close-capital fence; not a design change.
- **A smooth blackbody sphere as the simple mode** (3.2): that is the balloon the user names; simple mode drops
  elements, never structure.

## Reproduce

```sh
python3 verification/results/explosion-look/size_derivation.py          # the per-tier table, cadences, the close-capital charge
# the burst's dump and measurement the gates extend (native host, the lock serialises it)
X3M_WEAPON_EFFECTS_DUMP_MS=40,120,296,696 X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py \
  python3 verification/probe/run_weapon_effects.py --host native --dump <dir>
python3 verification/results/weapon-effects-burst/measure.py <dir>
```

## Built numbers (step 3, 2026-11-07)

Built for the first flight's tiers (fighter, missile); the corvette, capital and station records are census-only and
forwarded byte-exact (`boom::tier_native` = 14). The header constants are `namespace boom` in
`src/proxy/weapon_effects_core.h` (the lab reads them by name); the stage is `src/proxy/weapon_effects_boom.h`, the shock
`src/effects/weapon_shock_ps.hlsl` (drawn with the fireball's vertex program), the halo `src/effects/weapon_halo_ps.hlsl`
(with the glow's), the hook `core::death` with the sites of `src/proxy/weapon_effects_death_sites.h`. Gates and
measurements: the ledger's "Explosions (step 3)". The coordinator's decisions of 2026-11-07 after the first frames
(marked D1-D7) override the sections above where they differ:

| Item | Above | Built | Why |
| --- | --- | --- | --- |
| The seam | "one function" | `core::boom::decide(tier)`: ours when not in `tier_native` (fighter, missile); section 1's clone-row swap replaces its `native` answer for the big tiers later | no R' or swap in this step |
| Drawn sphere | R(t) x (1 + lobe_amp) = 1.25 R (the burst's rule) | `body_r_sphere` 1.33 | with 1.25 the body's mean outline (36 bins, 10 % of its peak) was 0.97 / 0.91 R(t) at 0.1 / 0.3 L [M]; with 1.33, 1.04 / 0.97 (fighter), 1.03 / 0.96 (missile) [M] |
| Size vs erosion (D1) | `death_tier_sizes` within 5 % at five ages; `_erodes_outside_in` lit share <= 0.15 at 0.9 L | sizes gated at 0.1 / 0.3 L (0.6 / 0.8 L recorded: the erosion shrinks the outline from 0.4 L); erosion at 0.9 L <= **0.20** | the pair cannot both hold: the larger sphere that meets the derived size lifts the lit share inside R(t) to 0.156 at 0.9 L [M] |
| Body floor | 12 px at 1440 rows | `body_floor_px` 12, at the centre's depth | a fighter at 3 km: law 7.7 px, drawn 12.1 [M] |
| Body bias | 0.5 R(t) towards the eye | 0.5 R(t), eased in over the first 2 R of the eye's distance | an eye inside the body would otherwise move the centre onto it |
| White-hot core (D5) | the star and white embers carry the birth | the fireball program's core gain g = `core_gain` 2.5 (1 - x / `core_x` 0.35): radiance x (1 + g) and whitened inside 0.3 R, to 0 at 0.5 R (a new vertex float, stride 80; 0 for the hull bursts and smokes) | the embers' white tangle over the young body; the body carries E >= 3 in its core at 0.1 L |
| Pinhole and doughnut (D4) | the burst program unchanged | the drift law (2026-11-07): octave i samples at p (f_i - g_i), g_i = (f_i / 2) / sqrt(1 + (|p| f_i / 2a)^2), the radius u = |p| f_i cells moved to u - a y / sqrt(1 + y^2), y = u / 2a: strictly increasing (slope 1/2 at the centre), the full shift a far out; no direction at the centre, so the 0.08 seed-direction blend is gone. Retuned with it: `fireball_erode_t` 0.28, `_erode_soft` 0.28, `_column_pow` 0.25 (hull-burst-look section 10) | the constant shift a p / |p| collapsed each octave's circle r = a / f_i onto one noise point: a dot with a ring at every body's centre from 0.5 L (octaves 2 and 3, inside about 0.15 R_s). Gates `hull_burst_no_centre_ring` / `death_body_no_centre_ring`: worst local ring prominence over the three ages 0.14-0.26 / 0.09-0.19 before, 0.08-0.09 / 0.04-0.09 after (bound 0.15) [M]; the radial zoom tried first lost 28 % of the burst's lit area (`verification/results/weapon-effects-burst/zoom-attempt-2026-11-07.txt`); ledger "Fireball drift law" |
| Eye inside (D2) | dark share >= 0.15 at t_b + 0.5 s | the wash is the picture: at 0.25 L lit >= 90 % and not uniform (std >= 0.05 of the mean); dark share >= 0.15 at 0.6 L | a hot wash is correct inside a fireball; no extra noise sample (slots) |
| Light on hulls (D3) | R 5 R_f | R = min(1.6 R(t), 2 R_f) at 0.33 E_b T heat(T) (hull-burst-look section 3) | the 5 R_f disc read as a lamp over a hull |
| Halo in air (D3) | none | the glow primitive's light in air about the body (`weapon_halo_ps`, occlusion class 1, its soft cut 0.5 R_h): R 1.6 R(t), 0.33 E_b T heat(T), x (1 - smoothstep(0.4, 0.8, x)) (`halo_fade_x0` / `_x1`); not drawn with the eye inside the body; weight 0.40 | a body on black read as a cut-out blob; its 10 % radius <= 1.8 R(t) (a lamp fails); the erosion fade added after the v2 frames: kept full to the end, the halo read as a balloon around the eroded body |
| Embers (D5) | U-wide streaks at v_e, the streak rule's floors | launched inside the newborn body (the offset within the unit ball, never past 0.8 R_0: none drawn at birth); hidden inside the body (fully within 0.8 R(t), visible past R(t), the hiding fading with the erosion from 0.5 to 0.9 L); widths 1.5-2.5 px at 1080 (hashed, the sprite program's new pixel mode: no floors); length a hashed 0.25-1 of 0.35 R_f on screen (never under two widths), a bright head and a fading tail; speeds 0.5-1.5 v_e; E x T | the tangle of white sticks at birth and the uniform rods later |
| Fragments | 0.25 U dark streak + tip | 3 px wide, half an ember's length, the tip at heat(0.6) cooling, a dark box of 1 m (x U / 3) under it, hidden as the embers | |
| Shock (D6) | a shell 0.17 inside, heat(0.9), alpha to 0 at 0.3 L | the limb only (the chord's density x a ramp from 0.15 r inside the limb: interior 0), heat(T(t)) of the body's age, gone at `shock_end` 0.25 L (r(t) keeps tau 0.3 L); **`missile_shock` false: off for the first flight** | the filled shell read as a grey disc; the limb-only shell (interior 0, outline 32.6 px against the law's 31.9 [M]) then read as a flat ring in the v2 frames, so decision 6's fallback applies; corvettes and up keep the law for when they are ours |
| Shock weight (D7) | <= 0.40 | `budget_weight_shock` **0.36** (D7 said 0.76) | the review's reproducible row (`shock_weight`, the missile's shell forced on by the fixture's `BoomTuning::force_shock`) measures 0.356 and 0.318 a screen at 5120x1440 in two runs (0.318 in the record) and 0.118 at 1080p once the fence's 14 us is removed [M]; 0.36 is the larger; the earlier 0.76 divided a fence-dominated time by a smaller area. The shell is optional: a refused shock program leaves the deaths drawn (`death_shock_refused_deaths_drawn`) |
| Nested (D8) | peak <= 2 E_b | three fighters 12 m apart (overlapping): the three's peak of body, light and halo at 0.1 L <= 1.4 x one death's, the cluster rule engaged (max N > 1); silhouettes judged per body rendered alone | the core makes one body's own peak 4+, so the bound is relative |
| Pre-roll, secondaries, hull light | sections 2.5, 2.6, 4 | not built (the big tiers are native) | the masked forward's step |
| Counts | 32 bodies, 2,048 quads | `max_bodies` 32, `max_quads` 2,048, 32 shock and 32 halo boxes | |
| Records | | life = pre-roll + 1.6 L + 1 ms (fighter 3,201, missile 1,281 game ms); Present keeps one the stage's latched floor longer | |

Shed order (section 7 as built): (1) every body's third octave (the burst's hysteresis); (2) smoke, largest first; (3)
shocks, largest first, charged 0.36 a screen; (4) other owners' glows and halos, largest first (0.40); never shed: the
body with its core, the star, the embers and fragments, anything of the own ship.

## Built numbers (the big tiers, 2026-11-07)

Built on `effects` `da8e8bcac`: corvettes, capitals and stations are the stage's (`boom::tier_native` 0, `boom::tier_swap`
14). Gates and measurements: the ledger's "Explosions: the big tiers". Where this table differs from the sections above, it
is what is built; the coordinator's decisions after the big tiers' frames are D9-D15 (2026-11-07).

| Item | Above | Built | Why |
| --- | --- | --- | --- |
| The seam | section 1 | `core::boom::decide` answers `swap` for the big tiers; `core::death` writes R' into the type record's column only when the frame is the stage's (the latch, `boom_ready`, the family), a ready clone matches the column's row and the tier's pre-roll (or is the column's own slot: the same type nested), the slot still holds the clone's count and array, and the swap stack (8) has room; else byte-exact forward, census, `cause=clone_not_ready` / `swap_full` / `no_record` in `weapon_death` (`cause=tier_native` only for a tier in `boom::tier_native`) | fail closed per death |
| The stub | "forwards, restores after" | handler result 2: the stub calls `0x00413610` itself with the three arguments copied (registers and flags as the call left them; the callee sees the stub's return and ESP 16 bytes deeper), then `x3m_weapon_death_restore` (LIFO, integer only, no call; a column the call changed is left as is). An unwind through the call skips the restore: Present restores what the stack still holds (`event=leak_restored`) | the tail jump cannot restore |
| R' | section 1 | built at Present by `weapon_effects_patch.cpp` (`clone_upkeep`), never while the scope loads. The scan (the first Present with valid type tables, every 300 frames, and after a drop or an invalidation) takes the (row, pre-roll) pairs of TShips records whose class id is a corvette or capital class and of the TDocks / TFactories records, only rows that are loaded and not our slots, and the map of referenced rows (every death column of TShips, TDocks, TFactories, TMissiles, TAsteroids and every PROPAGATE child +0x44 of a loaded row); pairs no longer found drop. A pair whose source row vanished or changed drops: its slot is abandoned (left as it is, never written again, never freed; at most 16, then no builds; reclaimed when a reload zeroes it) and the next scan builds R' again in another slot, never refilling or regrowing a slot in place (a live instance re-reads the count against a state array sized at its spawn); a built slot that reads zeroed is refilled only with the same count from a fresh allocation. Copies DISTANT with FADEOUTBODY and WRECK cleared and their start clamped to P, PROPAGATE dropped, then an empty FADEOUTBODY + TIMEOUT node at P for 1,000 ms and an empty TIMEOUT node 0 .. P + 1,000; the row's flags without EDF_PROPAGATE; the array from the EXE malloc after its five prologue bytes match; the highest slot from 999 down to 203 that reads empty and no row references; at most 16 pairs of at most 32 elements. The scan over the installed tables' record counts (1,553 records): 9.4 us median in the fixture's engine memory, 446 us the first, cold-memory scan [M]; logged as `scan_us` | the note's contract, made fail-closed (review 2026-11-07, A-C and L) |
| Unknown ships | the radius bands | a TShips class id outside `patch_tier_of_ship` adds no pair: such a ship's big-tier death (tier by its radius) swaps only when a known class shares its (row, pre-roll), otherwise it forwards native (`clone_not_ready`) | pairs come from known classes |
| Secondaries' sizes | "the last two 0.5 R_f; two of the station's 0.6 R_f" | `secondary_scale`: the last two 0.5 R_f; the station's two before them 0.6 R_f; the rest 0.35 R_f; R_0 = 0, rise 0.1 x 0.3 L, the body's white-hot core | the reading of section 2.5 |
| Secondaries' embers | half the tier's count | 8 per secondary (capital 64 + 64 at the body's birth, station 96 + 64) | 2.5's "8 embers" |
| Hull light (D9) | 0.4 -> 0.8 to P | the same, then falling to 0 with the hull copy's fade over P .. P + 1 s | ended at P, the lit hull dropped to grey in one frame (the v2 dump, 1,450 -> 1,540 ms) [M] |
| Hull shape | the bubble cache's box, else the capsule | the cache is looked up without a read (`bubble_peek`); the box (bubble half-extents / `bubble_scale`) taken to world by the node's rows 0 and 2 the record carries (x 127 in `dir`, `alt_rgb332` / `detail` / `calibre_q`; row 1 = row 2 x row 0); the capsule 1.4 R_obj long, 0.25 R_obj in radius; points 0.85 of the way out; `weapon_death bubble_cached=` (unknown 4) | |
| Budget cap (D10) | the shared order, the 450 us fence | the family's own cap `budget_screens` 2.5 below the shared remainder | six live secondaries near the eye (L_s = 0.3 L keeps the last ones through the body's first second) charged 6.6 screens, 907 us at 5120x1440 [M] |
| Secondaries' protection (D11) | step (5): past the first two live | the two newest live secondaries of a death kept from the shed only through its pre-roll; from P every secondary may go | kept past P, two secondaries stayed by the eye: 2.9 screens, 448 us; protected to P only, the worst frame is 361.4 us (2.4 screens), every step shed 82.8 us [M] (the record of the review run) |
| Drawn sphere (D12) | `body_r_sphere` 1.33 | 1.33 up to 64 px of projected radius (1,440 rows), falling linearly to `body_r_sphere_big` 1.21 at 320 px and past; the eye inside keeps 1.33 | at 1.33 the 10 %-of-peak outline grew with the pixel size: corvette 1.01 / 0.98, station 1.07 / 1.08, capital 1.10 / 1.14 R(t) at 0.1 / 0.3 L; at 1.21 every one within 5 % [M] |
| CPU gate (D13) | 50 us for 32 fighters | `death_big_stage_cpu`: 4 capitals, 2 stations, 26 fighters at most 0.08 us a drawn quad and 100 us | the stage's cost follows the quads it writes: 0.065 us a quad for the 32 fighters, 0.067-0.068 for the mix (791 quads, 52.7-53.9 us) [M] |
| Shock (D14) | corvette and up: the shell by its law | `boom::shock_tiers` 0: no shell on any tier; the program stays built and optional, the fixture forces the shell only for its law gate (`death_big_shock_is_a_shell`, evidence) | the corvette's expanding grey ring at 100-600 ms and the white ring at the capital's and station's birth are section 10's soap bubble; the replacement is a refraction limb through the engine shimmer pass (the hull shock front's), not a thinner ring |
| Halo of corvettes and up (D15) | 1.6 R(t), 0.33 E_b T, faded 0.4 .. 0.8 L (D3) | `halo_big_r` 1.15 R(t), at most `halo_big_e` 0.2 E_b, faded out over 0.3 .. 0.6 L; fighters and missiles unchanged; gate `death_big_halo_capped` (10 %-of-peak radius <= 1.25 R(t) on the capital at 0.2 L, none at 0.6 L) | at 1.6 R(t) it read as a smooth balloon about the body from 0.3 L in the frames |
| Scene model | the lab's box | the fixture draws the carrier as a box (2 R_obj along row 2, 0.5 R_obj across) at FADEOUTBODY's alpha into the scene and, while drawn, its depth into the lane; the lab the same; a solid box is the worst case for hiding the secondaries inside it | |
