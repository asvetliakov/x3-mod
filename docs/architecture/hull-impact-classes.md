# Hull impact classes: one anatomy per weapon class

Design note, 2026-11-06. Refines [weapon-look.md](weapon-look.md) 3.1 (the hull hit, step 2) against the reference
study [impact-effect-references.md](../reference/impact-effect-references.md); it does not change the shared language
of weapon-look.md 2 except where section 3 below says so explicitly, and it does not edit that note (the orchestrator
folds the amendments in). Context: Run240 (flash + sparks + glow + ember, every weapon alike) was rejected by the user
as lazy flashes and a downgrade, the sparks reading as the same decals on every hit. The standard is a rich modern
effect, ours in style, never less than vanilla's animated fire puff (Mayhem's Kyon and plasma rows play an 800 ms
`animation -73` at 0.04–0.06 plus a 250 ms glow [M, catalogue `effects.impact`]). The rebuild in progress on `effects`
(a hot-gas burst on every hit from 3.2's fireball law, randomised sparks, a shock ring and a heat-shimmer puff for the
heavy families, r_hit = 2 U (0.35 + 0.65 k), sparks 0.08 U wide at the 3 / 24 px floors, radiance x 2, τ 0.45 s) is the
base this note sorts into classes.

Markers: [M] measured or read from the game's tables; [S] in the source as built on `effects` `5867ca29d`; [R] a
figure the reference study quotes; [I] inferred. Every life is game ms (weapon-look 2.4); r = r_hit, U the victim's
cell unit, k the calibre, T the heat ramp's temperature.

> Ratified 2026-11-06 by the main session, with the recommended answers to the user questions unless the user overrides: scorch on from the first flight behind `hull::scorch` with its kill rule; shot-heading asymmetry for ballistic, heavy and missile (the recorded `Event::dir` is data, unlike the rejected use of it for face placement); the Mayhem 3 pins as read, to be corrected per id if the user disagrees; ion in fixed electric blue-white; heat shimmer on at 2 rects. Implemented on top of the burst rebuild (gas burst on every hit, randomised sparks, heavy ring) as the hull-hit classes change; weapon-look.md 2.6 and 2.11 take the two amendments listed here when that change lands.

## Outcome

**Nine classes, one language.** The hull hit keeps 3.1's skeleton (flash, sparks, glow, ember, the burst the rebuild
adds) and gives each class a different *second layer* and a different *post-hit*, which is where the references say
the identity of a hit lives: energy hits puff and glow, ballistic hits spray two spark lifetimes and chips and leave a
pit, plasma hits bloom into a tinted globe with a surface ring and a heavy scorch, heavy (railgun-like) hits streak
along the shot axis with a back-jet, an exit jet on thin hulls, a ring, a shimmer and a glowing hole, beams hold a
boiling spot that crawls and leaves a scorch line, ion hits throw crawling filaments and leave nothing, missiles add
debris to the burst of 3.5, repair and tug stay the tinted glows of 3.1, and an asteroid victim takes a dim dust puff
and rock chips. Two primitives are new and both are the glow primitive with a different profile (a **surface ring**:
the expanding sphere shell intersected with the lane, so no normal is needed; a **filament glow**: a noise mask over
the lane's reconstructed point, for ion arcs), one is a blend mode (the **scorch**: a multiply decal, the only
non-additive element, 10–20 s), and one is a reuse (the engine shimmer pass's rect list, post-resolve). The class is
decided at the hook from the live record (flags, damage band, body shape) into the free nibble of `Event::detail` [S],
and the override file's `hull` key pins it per `SS_BULLET_*` id: 11 pins on stock AP, 22 on Mayhem 3, whose flag
words are all 0 except PBC and RL [M, `hull_class_census_out.txt`]. Cost stays inside the shared 8-screen budget with
three new shed steps (shimmer rects, scorches, rings) ahead of the glows; sparks, flashes and the own ship are never
shed. Each class has a lab preset, a fixture gate on its distinguishing element and a kill criterion that folds it
back into the energy look, so a class that fails costs one build, not the family.

## 1. Facts this note stands on

- The record carries the classifier inputs at the hook: flag word, shield and hull damage per shot, light colour,
  speed, lifetime, the table row; the generated table adds the body's elongation and texture mean
  ([weapon-effects.md](weapon-effects.md) 4). `Event::detail` holds four bits (`drained` 1, `repair` 2, `tug` 4,
  `asteroid` 8) [S, `weapon_effects_core.h`], so a class nibble fits without widening the 64 B record. `Event::dir`
  is the projectile's heading (3 x i8, world, `node+0x40` third row), zero with `dir_fallback` counted when the node
  is not live [S]; its fallback rate in flight is not yet read (section 10).
- Mayhem 3's 38 rows carry flag word 0 except PBC (`0x2`) and RL (`0xa2`) [M, catalogue]; stock AP's 45 rows carry
  ammunition `0x40` on MASS, EBC, GAUSS, MAM, the on-hit bits `0x200`/`0x400` on ESR, IOND, IONC, IPG_FRAG, and
  beams `0x2`/`0x4` on 11 rows [M, weapon-effects.md 4.4]. So on the user's mod the classifier alone yields only
  energy, plasma (orb bodies), heavy (the ≥ 20,000 band), beam and repair; the ballistic and ion classes exist on
  Mayhem only through the override's pins.
- A gun-tied beam's contact damage is scaled by the frame step (`0x0045e321`, flags `& 6`) [M, spawn note §1], which
  implies one contact, hence one impact record, per frame while the beam lives [I; unknown 2]. Mayhem PBC lives 900 ms and
  refires at 2,222 ms, RL 1,000 / 3,000 [M, catalogue], so both pulse; stock PAL (2,490 / 420) sustains.
- The sprite batch draws two kinds, a flash (disc core + e-folded halo) and a streak (head, tail, width; the drawn
  length is the screen motion clamped to [rest, 6 rest]) [S, `weapon_sprite_vs/ps.hlsl`]; the glow is a box of
  radius R, back faces, one lane fetch, `E = colour x saturate(1 − d² / R²)²` with d the 3D distance to the lane's
  reconstructed point [S, 2.8]. Programs: sprite VS 69 / PS 85 slots, glow VS 6 / PS 47, decal PS 135 [M, ledger].
- Cost anchors at 5120x1440, EVENT-fenced best of 60 [M, ledger "Hull sparks unseen"]: 20 hits on a fighter at 200 m
  45.6 µs (the `hull_budget_20_hits_5120` bound 50); the same at 60 m 293.6; 64 hits on a close capital 132.8; one glow
  filling the frame 71.0 (`budget_weight_glow` 0.40 of the decal's 177.5). CPU 20 hits 9.8 µs best, mean 41.9 (the
  30 µs bound is met by the best only).
- The engine shimmer pass copies the resolved image over the union of up to `max_rects` 16 oriented rects (the
  runtime limit `engine_shimmer_max` 4) into a scratch and draws a refraction program scissored to the union, after
  the resolve and before Present, so the TAA never averages or accumulates it [S, `engine_shimmer_pass.h`,
  `engine_shimmer_core.h`]; its CPU submit is 0.090–0.097 ms [M, engine-effects ledger].
- The stage is additive (ONE/ONE on the FP16 target) and holds no copy of the colour buffer [S, 2.10]; a darkening
  element needs a second blend state, and a refracting one needs the shimmer pass's copy.
- The reference study's per-class anatomy (its section 4) and its five rules (structure of three shapes, asymmetry
  along the shot, motion, two-timescale decay, greyscale readability) are inference unless marked [R]; the quoted
  numbers are: the core flash peaks in 0.05–0.1 s and a small hit lasts 0.2–0.4 s [R]; FS2's ring is linear with
  alpha 1 − r/R and its distortion holds to 90 % of life [R]; Star Citizen's hull "lights up for several seconds" and
  burn (peak heat) drives permanent scorching [R]; the frag breakdown staggers 18 layers by 2–3 frames [R].

## 2. The classes: who decides what

**The class nibble.** `Event::detail` bits 4–7 carry `HullClass`: `energy` 0, `ballistic` 1, `plasma` 2, `heavy` 3,
`ion` 4, `beam` 5; `missile` is the existing `style = missile_hit`, `asteroid` the existing bit 8 (the victim wins over
the weapon), `repair` and `tug` the existing bits 2 and 4. The hook computes the class with the style (a few bit tests
on values it already holds; no new read), so a record is self-contained when the bullet dies that frame. The generated
table's per-row `hull` value (from the override) replaces the classifier's class; the fixture's `hull_mode` rows and
the `weapon_event` log row gain `class=`.

**The classifier rule**, first match wins (`hull_class_census.py` [S of this note, not production]):

| # | Cue (live record) | Class | Why |
| --- | --- | --- | --- |
| 1 | beam flags `0x2`/`0x4` and damage 0 | tug | stock TUG [M] |
| 2 | repair `0x80` | repair | REPAIR, RL [M] |
| 3 | beam flags `0x2`/`0x4` | beam | every contact tick is a record; the hold rule of section 3 makes it sustained or pulsed |
| 4 | on-hit `0x200` (slow) or `0x400` (energy drain) | ion | the engine's own "does something to the victim" bits: ESR, IONC, IPG_FRAG [M] |
| 5 | ammunition `0x40` | ballistic; heavy when d ≥ 20,000 | MASS, EBC, MAM; GAUSS heavy [M] |
| 6 | body orb (e < 1.5) or wave (`0x8`) | plasma | a round or sheet body is the engine's own plasma cue: PSP, MINING, PD, PSG [M] |
| 7 | d ≥ 20,000 (the architecture's heavy band, k ≥ 0.91) | heavy | PPC, GKE [M] |
| 8 | else | energy | tracers and slugs of light and medium calibre |

Missile sources (`missile_hit`) and class-17 victims are not weapons in this table. The rule reuses the thresholds
the architecture already pins (elongation cuts, the heavy band); no new constant.

**The override pins** (`tools/effects/weapon_overrides.json`, `by_id`, schema 2: the `hull` key's values become
`energy | ballistic | plasma | heavy | ion | repair | tug`; `beam` is not pinnable, since a bolt pinned "beam" would get
a hold rule with no tick stream). Pins are needed where the flags carry nothing the name does:

| Table | Pins | Result (counts by the script [M]) |
| --- | --- | --- |
| stock AP (45) | EMP, HEPT, IBL, PSG, TERANTIFIGHTER → plasma; FBL → ballistic; IPG → ion; REPAIR, TUG (existing) | ballistic 5, beam 9, energy 13, heavy 3 (GAUSS, PPC, TERRANANTIFIGHTER_1), ion 4, plasma 9, repair 1, tug 1; 11 pins |
| Mayhem 3 (38) | MD, GC, FBL, CFA, FAA → ballistic; ID, IC, ISR, IPG → ion; EMPC, PBG, SSC, PSG, PSP → plasma; RG, BKE → heavy; PALC, PBE, MDS → energy (orb bodies that are not plasma); RL → repair (existing) | ballistic 5, beam 1 (PBC), energy 16, heavy 3 (GKE, RG, BKE), ion 4, plasma 8, repair 1; 22 pins |
| FL (45) | the stock ids (the script gives FL the stock counts row for row [M, `hull_class_census_out.txt`]) | as stock |

The Mayhem pins are my reading of the weapon names (question 3 of section 12 asks the user to confirm them); a
wrong pin costs one line of JSON and a regenerate. A mod with unknown ids gets the rule above, which on a flag-less
table degrades to energy / plasma / heavy by body and damage, never to nothing.

## 3. What this note adds to the shared vocabulary

Everything below is one of the existing primitives with a different profile, or a rule on existing records.

1. **Burst (puff).** The rebuild's hot-gas burst: the 3.2 fireball impostor at r_b, L_b per class, R(t) = r_b (1 −
   e^(−t / 0.15 L_b)), T 1 → 0 over L_b, two fbm octaves advected outward, body ≤ 1.5, occlusion class 2 (SOFT 1.0),
   biased 0.5 r toward the eye like the flash. **Anchored to the owner** (re-posed like the flash), so it inherits the
   hull's velocity and reads as vacuum (reference §2 "smoke / vapour"); the sparks stay world.
2. **Two spark sets.** The rebuild's hashed sparks split into a *fast* set and a *slow* set with their own count,
   speed, life and width (the ArtStation bullet-impact layering [R]: a quick dense burst, a few slower larger sparks),
   both the closed form of 2.5 with τ 0.45 s. Both read the `stagger` 0.4 so they do not die together [R].
3. **Chips.** Slow sparks at low heat: T 0.3 → 0, width 0.12 U, rest length 1.5 widths, τ 1.0 s (they keep their
   velocity longer), E 1.5. In an additive renderer a "dark chip" is hot metal seen dim; it cannot be black.
4. **Shot-axis bias.** A spark set may take v = speed x normalize(u + b a), u an isotropic unit vector from the hash,
   a the record's `dir` (the shot heading, data not an estimate), b the class's bias: b < 0 jets back toward the
   shooter, b > 0 continues through the hull. The lane hides what is inside the hull; with `dir` zero (`dir_fallback`)
   b is 0 and the set is isotropic. This amends 2.6's "sparks are isotropic" for the classes that use it and keeps
   the rejected rules out: no normal, no face choice, placement unchanged.
5. **Surface ring.** The glow primitive with a shell profile: `E = colour x exp(−((d − r(t)) / w)²) x (1 − t/L)²`,
   r(t) = r_max (1 − e^(−t / 0.1 L)) (fast then slow [R]; a linear ring reads as a UI circle), the box radius r(t) +
   2 w. It is the expanding sphere shell intersected with whatever surface the lane holds: a circle on a flat plate,
   a contour on a curved hull, cut at a hull edge, never on sky, and it needs no tangent plane. About +10 PS slots
   over the glow's 47 [I]. This is how the brief's "tangent-plane shock ring" is drawn.
6. **Filament glow (ion arcs).** The glow primitive with a noise mask: the lane's reconstructed point is taken into
   the owner's frame (one 3x3 multiply; the owner's rows are already uploaded), `n = fbm3(p · 6 / r + seed(slot))`,
   mask = saturate(1 − |n| / 0.08), E = colour x mask x saturate(1 − d² / R²), `slot = floor(age / rehash_ms)`. Thin
   bright filaments where the noise crosses zero, crawling on the hull because the pattern re-hashes per slot and is
   fixed to the owner between slots. About +70 PS slots [I: two 3D octaves, the fireball's two cost ~150 of its 300].
7. **Scorch.** A surface-bound decal that darkens: the glow box drawn with blend ZERO / INVSRCCOLOR, output
   `s(t) x w(d) x (0.75, 0.80, 0.85)` so the hull is multiplied by 1 − that (a slightly brown darkening), w(d) the
   glow's window, s(t) = s0 x min(1, t / L_ember) x (1 − t / L_s): it develops while the ember cools [R, SC burn =
   peak heat] and fades linearly over L_s 10–20 s. Drawn **first** in the stage (before every additive layer), never on
   sky (class 1), dimmed by `own_cockpit_gain` on the own ship. Scorches live in their own store, not the ring: when a
   record retires at its ember's end (today's 1,441 ms record life stays), its scorch moves to a per-owner store (16
   per owner, 128 total, 32 B each: owner handle + serial, pos, r, s0, t0, L_s), so a 20 s scorch does not hold a ring
   slot and the ring's life and overflow rules are unchanged. `hull::scorch` is a header switch (question 1).
8. **Shimmer rect.** A heavy or plasma hit submits one oriented rect (3 r about P, along the shot's projection or the
   view's x when `dir` is zero) to the engine shimmer pass's list after the engines' rects, strength (1 − t / 400 ms),
   at most `hull::shimmer_max` 2 rects per frame (the pass's 16 minus the engines' 4 leaves room; the engines keep
   priority). Post-resolve, so no TAA interaction [S]. Not previewable in the lab (canvas has no refraction); judged in
   flight only.
9. **Hold rule (beams).** At scene end, a new beam-class record whose owner and (table, row) match a live *spot* and
   whose pos is within `hold_r` 0.5 r of it re-lights the spot (t_last = its t0, pos = its pos; the record is
   released) instead of becoming one. A spot is *held* while now − t_last ≤ `hold_ms` = max(60, 2 step_game) and
   *falls* over 80 ms after (3.4's envelope); a record with no spot to merge into becomes a new spot, and it flashes
   only if the owner had **no** held spot of that (table, row) (the beam was off: a pulse start), so a sweeping beam
   lays a trail of spots without strobing. At one record per beam per frame [M] the merge is O(new x spots) with
   spots ≤ 64.
10. **Re-light resets, never adds** (the shield cells' rule) holds for every element re-lit by a merge.

## 4. The anatomy per class

Sizes in r (= r_hit = 2 U (0.35 + 0.65 k) [S]) or U; lives in game ms, x (1 ± stagger / 2) by hash as 2.4; the
three-frame floor on everything; the chase-nose gate (2.9) is met by the sparks of every class but repair, tug and
ion (which meet it with the flash and the arcs' sparks). "Post-hit" is what outlasts the fast elements by about 10x
(the two-timescale rule). Reference inferences for the fighter tier are scaled by r, not by a tier factor: the
reference's "capital x 2–2.5 in size, x 1.3 in duration" [R, inference] is already in U (3 / 6 / 16) and in the k
terms of the lives, and k and tier correlate (heavy weapons hit capitals).

### 4.1 Energy bolt (default; Mayhem AKE, APC, IRE, PAC, PRG, EBC, CIG, PPC, GKE below the heavy band …)

The laser look of SC and Elite [R, inference]: a hard flash, vaporised hull as a puff, little spray, a glow that
outlasts everything, a scorch that cools.

| Order | Element | Size | Life | Colour | Motion / fade |
| --- | --- | --- | --- | --- | --- |
| 0 ms | Flash | core 0.25 r (≥ 3 px), halo 1.0 r | 60 | white core E 4, halo T 0.9 | linear |
| 0 ms | Puff | r_b 1.0 r | L_b 350 + 150 k | heat, T 1 → 0, body ≤ 1.5 | grows 0.15 L_b, owner-anchored |
| 0 ms | Spatter (slow set only) | n = 4 + 4 k, width 0.08 U, 3 widths | 150 + 100 k | heat, E 8 → 0 | 3 U/s (1 ± 0.3), isotropic (b 0), τ 0.45 |
| 0 ms | Glow | R 3 r | 150 | T 0.85, E 1.0 | linear |
| 0 ms | Ember | R 0.8 r | 1,500 + 500 k | T 0.6 → 0, E 0.8 | linear cooling (the product ramp(T) x E x T is front-loaded, the reference's exp) |
| post | Scorch | R 0.6 r | L_s 12,000 | s0 0.45 | develops over the ember, fades linearly |

Distinguishing element: the puff with few sparks. What the user sees: a flash that becomes a small fireball sinking
into the hull, a glow, a spot that cools to red and leaves a dark mark.

### 4.2 Ballistic / mass driver (`0x40`; Mayhem MD, GC, FBL, CFA, FAA by pin)

The multicannon / PDC look [R, inference]: pinpoint flash, two spark lifetimes, chips keeping their velocity, little
glow, a pit. No vaporised hull, so the puff is a thin dust puff.

| Order | Element | Size | Life | Colour | Motion / fade |
| --- | --- | --- | --- | --- | --- |
| 0 ms | Flash (pinpoint) | core 0.15 r (≥ 3 px), halo 0.4 r | 40 (floored to 3 frames) | white, halo T 1 | linear |
| 0 ms | Dust puff | r_b 0.4 r | L_b 200 | T 0.6 → 0, body ≤ 0.8 | grows 0.15 L_b |
| 0 ms | Fast sparks | n_f = 8 + 12 k, 0.08 U | 150 + 50 k | heat, E 8 | 8 U/s (1 ± 0.3), bias b −0.6 (splash back toward the shooter), τ 0.45 |
| 0 ms | Slow sparks | n_s = 4 + 6 k, 0.08 U | 450 + 150 k | heat, E 8 | 4 U/s (1 ± 0.3), b −0.3 |
| 0 ms | Chips | n_c = 3 + 3 k, 0.12 U, 1.5 widths | 600 + 200 k | T 0.3 → 0, E 1.5 | 2 U/s (1 ± 0.5), b −0.3, τ 1.0 |
| 0 ms | Glow (pit light) | R 1.5 r | 100 | T 0.9, E 0.6 | linear |
| 0 ms | Ember (pit) | R 0.3 r | 500 | T 0.4 → 0, E 0.5 | linear |
| post | Pit mark | R 0.3 r | L_s 10,000 | s0 0.5 | as the scorch; off with `hull::scorch` |

Distinguishing element: the two spark lifetimes with the splash-back bias and the chips; no fireball. The count at
k 0 is 15 streaks against the energy class's 4.

### 4.3 Plasma (orb and wave bodies; Mayhem EMPC, HEPT, PG, PBG, SSC, PSG, PSP)

The plasma-accelerator look [R, inference]: a bloom twice the laser's, a globe that lingers, shrinks and reddens, a
ring, a heavy scorch. The one class where the weapon tint reaches the target, because the globe is the weapon's own
matter (the bolt-body rule of 2.1), and only while it is hot.

| Order | Element | Size | Life | Colour | Motion / fade |
| --- | --- | --- | --- | --- | --- |
| 0 ms | Bloom (flash) | core 0.35 r, halo 2.0 r | 60 | white core; halo = lerp(heat T 1, tint, 0.5) when the tint is saturated, else heat | linear |
| 0 ms | Globe (puff) | r_b 1.5 r | L_b 600 + 400 k | lerp(heat(T), tint, 0.5 (1 − t / L_b)) when saturated | grows 0.2 L_b then shrinks to 0.5 r_b over the last 0.7 L_b; reddens as T falls |
| 0 ms | Surface ring | r_max 2.5 r, w 0.15 r | L 300 | T 0.9, E 1.0 | fast-then-slow, (1 − t/L)² |
| 0 ms | Slow sparks | n = 3 + 5 k, 0.10 U | 400 + 200 k | heat, E 8 | 3 U/s (1 ± 0.4), isotropic, τ 0.45 |
| 0 ms | Glow | R 4 r | 250 | T 0.8, E 1.2 | linear |
| 0 ms | Hot spot (ember) | R 1.0 r | 3,000 | T 0.6 → 0, E 1.0 | linear cooling |
| 0 ms | Shimmer rect | 3 r | 400 | — | strength 1 − t / 400 |
| post | Scorch | R 1.2 r | L_s 20,000 | s0 0.6 | as the scorch |

Distinguishing element: the globe that shrinks and the ring. At k 1 (PSP) r on a capital is 32 m and the globe 48 m:
the budget's LOD (section 6) applies.

### 4.4 Heavy, railgun-like (d ≥ 20,000 and a tracer / slug body, or ballistic heavy; Mayhem GKE, RG, BKE; stock GAUSS, PPC)

The Elite railgun and Expanse rail look [R, inference]: a thin entry flash along the shot, a jet of glowing debris,
an exit jet on a thin target, a long-lived glowing hole.

| Order | Element | Size | Life | Colour | Motion / fade |
| --- | --- | --- | --- | --- | --- |
| 0 ms | Entry streak | a streak quad from P − 1.5 r a to P + 0.5 r a (a = `dir`; the inside half hidden by the lane), width 0.3 r; plus a disc core 0.2 r | 60 | white E 4 | static (rest length), linear |
| 0 ms | Puff | r_b 0.8 r | L_b 300 + 200 k | T 1 → 0, body ≤ 1.5 | grows 0.1 L_b (fast: vaporised metal) |
| 0 ms | Back-jet | n = 12 + 12 k, 0.08 U | 300 + 200 k | heat, E 8 | 10 U/s (1 ± 0.4), b −2.0 (a cone of about 25° about −a), τ 0.45 |
| 0 ms | Exit jet | n_x = 6 + 6 k, 0.08 U | 400 + 200 k | heat, E 8 | 12 U/s (1 ± 0.3), b +2.0: inside a thick hull it stays hidden; on a wing or a fighter it emerges on the far side (a through-hole for free) |
| 0 ms | Surface ring | r_max 3 r, w 0.12 r | L 250 | T 0.9, E 1.2 | fast-then-slow |
| 0 ms | Shimmer rect | 3 r along a | 400 | — | strength 1 − t / 400 |
| 0 ms | Glow | R 3 r | 200 | T 0.9, E 1.5 | linear |
| 0 ms | Hole (ember) | R 0.5 r | 2,000 | T 0.7 → 0, E 1.2 | linear cooling (the reference's exp(−t / 2 s)) |
| post | Scorch | R 0.6 r | L_s 15,000 | s0 0.7 | as the scorch |

Distinguishing element: the axial streak and the two jets. With `dir` zero (`dir_fallback`) there is no axis: the
entry streak becomes the energy flash at 1.5 r and both jets become one isotropic set of 18 + 18 k; the ring, puff,
shimmer, hole and scorch stay, so the class still reads heavy.

### 4.5 Beam, sustained and pulsed (flags `0x2` / `0x4`; Mayhem PBC; stock PAL, Kyon, PBC, TRIBEAM, FUSIONBEAM)

One class; the look follows the tick stream through the hold rule (section 3.9). A sustained beam (PAL) holds a
boiling spot that crawls where the beam moves and leaves a line of cooling spots; a pulsed beam (Mayhem PBC: 900 ms
on, 1,322 off [M]) flashes at each pulse start, holds for the pulse, falls.

| Order | Element | Size | Life | Colour | Motion / fade |
| --- | --- | --- | --- | --- | --- |
| pulse start | Flash | core 0.25 r, halo 1.0 r | 60 | white, halo T 0.9 | only when no spot of that (owner, row) was held |
| held | Spot | core 0.2 r (≥ 3 px), halo 0.6 r | while held, fall 80 | white E 4, halo T 1 | re-posed each tick (the beam's crawl) |
| held | Boiling puff | r_b 0.6 r | L_b 300, age reset at each tick | T 1 | grows 0.15 L_b; re-lit, never added |
| held | Spatter | 2 per 40 ms game while held (closed form: spark i born at t0 + 40 i), cap 24 live per spot, 0.08 U | 300 | heat, E 8 | 4 U/s (1 ± 0.4), isotropic, τ 0.45 |
| held | Glow | R 2.5 r | while held, fall 150 | T 0.85, E 1.0 | linear fall |
| after | Trail ember (per spot) | R 0.8 r | 1,500 | T 0.6 → 0, E 0.8 | starts at the spot's fall |
| post | Scorch line (per spot) | R 0.8 r | L_s 15,000 | s0 0.5 | the dwell path as scorches 0.5 r apart |

Distinguishing element: the held spot and the trail. Repair and tug (4.8) are beams whose class bits say otherwise:
the hold rule applies, the spot does not.

### 4.6 Ion / distortion (`0x200` / `0x400`; Mayhem ID, IC, ISR, IPG by pin; stock ESR, IONC)

The SC distortion and HW3 ion look [R, inference]: blue-white flash, arcs crawling over the hull with 3–5 flickers
over 0.5 s, no scorch, nothing after.

| Order | Element | Size | Life | Colour | Motion / fade |
| --- | --- | --- | --- | --- | --- |
| 0 ms | Flash | core 0.2 r, halo 1.0 r | 60 | white core; halo `hull::ion_colour` (0.60, 0.75, 1.00) (the tint when saturated: stock IOND's blue) | linear |
| 0 ms | Filament glow (arcs) | R 1.5 r | 500 | ion colour, E 2.0 | re-hash every 100 ms (five flickers), linear fade |
| 0 ms | Arc sparks | n = 4 + 4 k, 0.08 U | 120 | ion colour, E 8 | 6 U/s (1 ± 0.5), isotropic; hash-gated on/off per 40 ms (a flicker) |
| — | no puff, no glow, no ember, no scorch | | | | the class's identity is its absence of heat |

Distinguishing element: the crawling filaments and the cold colour. The 500 ms arcs are the slow element (8x the flash).

### 4.7 Missile (`missile_hit`)

3.5 already gives the hull hit's missile variant plus a burst of r_b = U (1 + k) by the 3.6 law (flash, fireball,
shock shell with the missile tint on its rim, 8 + 8 k embers, glow 4 r_b). This note sets the hull part:

| Order | Element | Size | Life | Colour | Motion / fade |
| --- | --- | --- | --- | --- | --- |
| 0 ms | Flash | as energy, halo in the missile tint (existing) | 60 | | |
| 0 ms | Burst | 3.6 at r_b = U (1 + k) | L 0.5 + 0.7 k s | heat, tint on the shell rim | 3.6 |
| 0 ms | Debris: fast + slow sparks x 2 | n_f = 16 + 24 k, n_s = 8 + 12 k, 0.08 U | 150 + 50 k; 1,500 (the slow set, "ember flickers 1–2 s", hash-gated 0.3 duty) | heat | 8 / 3 U/s, b −0.3, τ 0.45 / 1.0 |
| 0 ms | Surface ring | r_max 3 r, w 0.15 r | 300 | T 0.9 | fast-then-slow |
| 0 ms | Glow | R 4 r | 250 | T 0.85, E 1.5 | linear |
| 0 ms | Ember | R 1.0 r | 2,000 | T 0.6 → 0 | linear |
| post | Scorch | R 1.5 r | L_s 20,000 | s0 0.6 | as the scorch |

Vented vapour (3 s, the reference's post-hit for missiles) is a non-additive layer and goes with smoke (weapon-look
question 5), not here.

### 4.8 Repair and tug (override `hull: repair | tug`)

Unchanged from 3.1: repair = glow R 2 r and ember R 0.4 r in the weapon tint (RL's green light is saturated [M]), no
flash, no sparks, no puff, re-lit per tick by the hold rule, the ember 800 ms after the last tick; tug = glow only,
pale blue (`tug_colour` [S]), calibre 0.1. No scorch (they do not burn).

### 4.9 Asteroid victim (class 17, any weapon)

Rock, not metal: no flash, no glow, no ember, no scorch. A dim dust puff (the burst at T 0.3 → 0, body ≤ 0.5, r_b
1.2 r, L_b 800 + 400 k, grey-red through the ramp's last stops), rock sparks at 2 x the class's slow count on the
ramp's last stops (existing `asteroid_dust_x`, `asteroid_t` 0.3 [S]), chips 2 x (T 0.25, τ 1.0), isotropic. Mining
weapons (MDS) on asteroids take this; the weapon's class does not matter on rock.

## 5. Colour sources

| Source | Where it is allowed | Where it is not |
| --- | --- | --- |
| Heat ramp (T) | every flash halo, puff, spark, chip, glow, ember, ring of every class on a ship; the asteroid's dust at T ≤ 0.3 | — |
| Weapon tint (saturated only; else heat) | the plasma bloom halo and globe at 50 % while hot; the ion flash halo and arcs (stock's blue lights); repair and tug glows (3.1); the missile flash halo and burst rim (3.5 / 3.6) | energy, ballistic, heavy, beam: a texture average (Mayhem's white lights, 36 of 38 [M]) would colour burning metal with nothing |
| Class constants | `ion_colour` (0.60, 0.75, 1.00); `tug_colour` [S]; the scorch's darkening (0.75, 0.80, 0.85) | — |
| White | every core | — |

The greyscale rule (reference rule 5) is why the classes differ in *shape and timing* first: energy's puff vs
ballistic's spray vs heavy's axis vs beam's held spot vs ion's filaments read without colour; colour only confirms.

## 6. What stays, what it costs, what is shed

**Unchanged:** placement from the recorded contact point on the owner's rows (2.6; the shot-axis bias moves no
anchor); occlusion classes 1 and 2 with the plumes' SOFT pair (2.7; the ring, filament glow and scorch are class 1 by
construction, the entry streak class 2 at SOFT 0.15); game time with the three-frame floor and the SETA law (2.4);
one stage callback, one sprite batch, the draw order of 2.10 with the scorches inserted first; no user setting; the
native spawn skipped per record as today; sounds untouched (2.13); the own-ship cockpit gain on every owner-attached
element including the scorch (2.9).

**Cost per class** [I, from the anchors of section 1: a sprite quad is negligible at fighter range (20 hits with
glows 45.6 µs → about 2.3 µs per hit); a glow box costs its screen area x 71 µs; the puff impostor ~300 slots → about
2.2 x the decal per screen (390 µs per screen); the ring ~1.2 x the glow; the filament glow ~2.5 x the glow; the
scorch = a glow box; the shimmer rect's GPU cost is unmeasured]. Two scales: a fighter hit at 200 m (r 3.4 m, every
box a few hundred pixels) and the worst case, a hit on a capital 100 m away (r 28 m: a 3 r glow fills the frame).

| Class | Quads per hit (CPU) | Fighter at 200 m, per hit | Capital at 100 m, per hit, before shedding | Weight terms charged to the budget |
| --- | --- | --- | --- | --- |
| energy | ~12 | ~3 µs | glow 71 + puff ~40 + ember 8 + scorch 5 ≈ 125 µs | glow 0.40, puff 2.2 x area, scorch 0.40 |
| ballistic | ~24 | ~3 µs | glow 18 + dust 10 ≈ 30 µs | glow 0.40 |
| plasma | ~16 | ~4 µs | glow 71 + globe ~120 + ring 40 + ember 20 + scorch 10 ≈ 260 µs + shimmer | + ring 0.5, filament 1.0 |
| heavy | ~40 | ~5 µs | glow 71 + puff 30 + ring 50 + hole 5 + scorch 5 ≈ 160 µs + shimmer | as plasma |
| beam (per spot) | ~30 | ~4 µs | glow 50 + puff 20 + trail embers ≈ 80 µs | glow 0.40 |
| ion | ~10 | ~3 µs | filament 1.5 r ≈ 45 µs | filament 1.0 |
| missile | ~50 | ~6 µs | the 3.6 burst (≤ 3 screens on a close capital) + ring + glow ≈ 300 µs | 3.6's |
| repair / tug | 2 | ~2 µs | glow 40 µs | glow 0.40 |
| asteroid | ~20 | ~3 µs | dust puff 20 µs | puff |

Targets stay 3.1's: a 20-hit fighter frame ≤ 50 µs fenced at 5120x1440 **per class** (the energy class is the
tightest: the puff adds a second impostor per hit; if it misses, the puff's octaves drop to one at fighter range by
the LOD below), 64 mixed hits ≤ 30 µs CPU (about 1,600 quads: inside the 2,048 spark cap), the family ≤ 0.1 ms of
`stage_us` p90 in a fighter fight; a close capital fight is bounded by the budget, not by a per-hit figure.

**Shed order**, extending 2.11 (the stage charges projected box area x weight; over 8 screens): (0) shimmer rects past
`shimmer_max` and every shimmer rect of owners other than the own ship; (1) the puffs' second octave above 20 %
coverage, then puffs past two per owner; (2) scorches of owners other than the own ship, oldest first; (3) rings of
other owners; (4) glows and embers of other owners, oldest first (the existing step 2); (5) shield patches; (6)
secondaries. **Never shed:** flashes, entry streaks, sparks, chips, arc sparks, beam spots, anything of the own ship.
Count caps (static): sparks 2,048 quads (existing), puffs 128, rings 64, filament glows 32, beam spots 64, scorches
128 (16 per owner; the oldest of an owner retires), shimmer rects 2.

## 7. Lab presets

Added to `PRESETS` in `generate_weapon_effects_lab.py` (the existing `hull/…` presets stay; `--check` requires one
preset per class): `hull/energy-ire-fighter-side` (= `fighter-ire-side`), `hull/ballistic-md-fighter-side` (Mayhem MD),
`hull/ballistic-gauss-capital-300m` (stock GAUSS: heavy ballistic), `hull/plasma-hept-corvette-flank` (Mayhem HEPT),
`hull/plasma-psp-capital-close` (k 1: the LOD case), `hull/heavy-gke-capital-300m`, `hull/heavy-rg-chase-nose` (the
own nose: the back-jet toward the camera), `hull/heavy-thin-wing-exit-jet` (a 1 U slab hull: the exit jet emerges),
`hull/beam-pbc-pulsed-corvette` (900 / 2,222 ms ticks), `hull/beam-pal-sustained-sweep` (stock PAL, the contact
moving 0.3 r per tick across a capital flank), `hull/ion-id-fighter-side`, `hull/ion-ionc-capital`,
`hull/missile-hit-corvette` (existing, with the debris), `hull/repair-corvette`, `hull/mining-asteroid-60m` (existing),
`hull/class-lineup-fighter-side` and `hull/class-lineup-capital-300m` (all nine classes at one k on one hull, side by
side, looping: the A/B the kill criteria use), `hull/scorch-12hps-20s` (the per-owner cap and the fade),
`hull/seta6-classes` (the lineup at SETA 6). Each class gets a fieldset of its constants (counts, speeds, lives, radii,
L_b, ring r_max / w / L, `hold_ms` / `hold_r`, `rehash_ms`, scorch s0 / L_s, `shimmer_max`) and a `class` select that
overrides any weapon's class for the preview. The lab cannot preview the shimmer rect (no refraction on the canvas)
and says so in the fieldset.

## 8. Fixture gates

`HULL_*` in `run_weapon_effects.py --host native`, the step-2 naming. Shared: `hull_class_from_record` (hook: every
stock id → the table of section 2; a pinned row → the pin; the nibble round-trips through the ring and the log row),
`hull_scorch_multiplies_hull_only` (over sky nothing changes; over a hull of value 1 the pixel reads 1 − s w), `hull_scorch_store_outlives_record` (the record retires at 1,441 ms, its scorch reads at 6 s, 0 at L_s; 17 scorches on one
owner keep 16), `hull_scorch_through_temporal` (an owner moving 8 px per frame: ≤ 1 code residual 8 frames after the
scorch ends), `hull_classes_distinct_greyscale` (the lineup at 100 ms and 400 ms: the 8-bin radial luminance profile
of every pair of classes differs by ≥ 0.2 L1, normalised; a crude form of the reference's rule 5), the per-class
budget rows (`hull_budget_20_hits_5120_<class>` ≤ 50 µs; the capital-close row per class recorded and the shed
counts asserted at 8 screens), `hull_cpu_64_hits_mixed` ≤ 30 µs, `hull_min_frames_under_seta6` per class, Reset,
fault, refusal, slots per program.

Per class, on the distinguishing element:

| Class | Gate | What it measures |
| --- | --- | --- |
| energy | `hull_energy_puff_grows_cools` | R(t) at 0.2 / 0.5 / 1.0 L_b within 5 % of the law; mean hue at 0.2 / 0.6 / 1.0 L_b down the ramp; the puff's centroid follows a moving owner (owner-anchored) |
| energy | `hull_energy_spatter_4_4k` and `hull_energy_ember_outlasts_flash_10x` | n at k 0 / 0.5 / 1; the ember ≥ 0.5 of its peak at 300 ms, lit at 1,500 ms, 0 after its life |
| ballistic | `hull_ballistic_two_spark_lifetimes` | lit streaks at 100 / 300 / 500 ms: the fast set gone by 250 ms, the slow set alive at 450 |
| ballistic | `hull_ballistic_splash_back_bias` | the lit-spark centroid at 200 ms displaced along −dir by ≥ 0.3 of the reach; within 0.05 of isotropic under `dir_fallback` |
| ballistic | `hull_ballistic_chips_keep_velocity` and `hull_ballistic_no_fireball` | chips' centroid at 600 ms beyond the sparks'; no lit pixel of the dust puff beyond 0.5 r |
| plasma | `hull_plasma_bloom_2x_energy` and `hull_plasma_globe_shrinks_reddens` | the halo's lit radius 2 x the energy preset's; the globe's radius at 0.3 / 0.65 / 1.0 L_b falls; hue reddens |
| plasma | `hull_plasma_ring_on_surface_not_sky` | 0 lit sky pixels; on a flat lane a circle of r(t) at 0.1 / 0.2 / 0.3 s within 5 %; on a sphere lane the contour; cut at a hull edge |
| heavy | `hull_heavy_entry_streak_along_dir` | the streak's orientation within 2° of the projected `dir`; its inside half hidden on a flat lane |
| heavy | `hull_heavy_back_jet_cone` and `hull_heavy_exit_jet_emerges_thin_hull` | ≥ 0.8 of the back-jet within 25° of −dir; on a 1 U slab, lit sparks behind it after 150 ms and none at 0 ms; on a 10 U slab none |
| heavy | `hull_heavy_shimmer_rect_submitted` | the shimmer pass receives ≤ 2 hull rects after the engines', none after 400 ms, none in the resolve's history |
| beam | `hull_beam_hold_relights_not_appends` | 60 ticks at one point over 1 s: one spot, peak = a single tick's, ring occupancy 1 |
| beam | `hull_beam_sweep_trail_without_strobe` and `hull_beam_pulse_flash_after_gap` | a contact moving 0.3 r per tick: spots 0.5 r apart, 0 flashes; a 1,322 ms gap: exactly one flash at the next tick; the fall 80 ms |
| ion | `hull_ion_filaments_on_surface_rehash` | 0 sky pixels, ≥ 0.9 on the lane; the lit set at 0 and 100 ms differs in ≥ 50 % of its pixels, at 0 and 30 ms it is identical; fixed to a moving owner between slots |
| ion | `hull_ion_no_heat_post_hit` | 0 lit pixels of any ember, glow or scorch; nothing after 500 ms |
| missile | `hull_missile_debris_two_sets_and_ring` | counts 2 x the ballistic law; the slow set lit at 1,200 ms with a 0.3 duty; the ring as plasma's |
| repair / tug | existing `hull_repair_no_sparks`; `hull_repair_relit_per_tick` | 60 ticks: one glow, peak = single |
| asteroid | existing `hull_asteroid_dust`; `hull_asteroid_no_glow_no_scorch_dim_puff` | 0 glow / ember / scorch pixels; the dust puff's peak ≤ 0.5 |

## 9. Kill criteria

The user's words decide, per class, in the lineup preset or in flight; each step is taken once, then the next:

| Class | Observation | Step 1 | Step 2 |
| --- | --- | --- | --- |
| energy | the puff reads as a cartoon cloud, balloon or bubble | one octave (smooth) | puff off: the Run240 look is not a fallback the user accepts, so step 2 is the *plasma* globe at r 0.7 (a different shape), then the family's simple mode |
| ballistic | sparks read as confetti / the splash-back reads as sparks "coming from the shooter" | counts halved / b 0 (isotropic) | chips off; the class folds into energy |
| plasma | the globe reads as a soap bubble / the ring as a UI circle | no tint blend, r_b 1.0 r / ring off | the class folds into energy at 1.5 r |
| heavy | the jet reads as a laser line, or sparks appear on the far side of a thick hull | exit jet off | entry streak → the disc flash; the class folds into plasma |
| heavy, plasma | the shimmer double-images or flickers | `shimmer_max` 0 | — |
| beam | spots pile up (a dotted line) or the pulse flashes strobe | `hold_r` 1.0 r, trail embers 500 ms / pulse flash off | the class folds into energy per tick (the hold rule stays, as the ring cap) |
| ion | the arcs read as static or noise | `rehash_ms` 200, filament width 0.05 | the energy look in the ion colour |
| missile | too big or doubles the hull hit | 3.5's rule (`hit_burst` off) | debris at the ballistic counts |
| scorch (all) | ghosting, a mark on the wrong ship, ships "painted black" in a long fight | s0 halved, L_s halved | `hull::scorch` false (the ember's life x 2 keeps a post-hit) |
| identity (all) | two classes the user cannot tell apart in the lineup | the pair's pins move to one class (the louder one) | — |

A class folds, never the family; the family's simple mode (3.1) and the native rollback are unchanged.

## 10. Unknowns and what settles each

| # | Unknown | Settles it |
| --- | --- | --- |
| 1 | The `dir_fallback` rate in flight (the heavy and ballistic asymmetry depends on `dir`) | A `triage` of the Run240 log's `weapon_effects_frame` counters (`dir_fallback` is already counted [S]); above 0.2 the heavy class needs the muzzle record's axis instead |
| 2 | Whether a beam contact is one impact record per frame (inferred from the frame-step damage scaling [M]) | One observe-row count of same-(owner, row) `hull_hit` records per frame with a PBC firing |
| 3 | The puff impostor's native GPU cost at hull-hit sizes (the fireball program is not built; 300 slots is 2.10's estimate) | The rebuild's fixture timing rows; the budget weight pinned from them |
| 4 | Whether the shimmer pass can take hull rects without starving the engines (`engine_shimmer_max` 4 of 16) | `hull_heavy_shimmer_rect_submitted` plus one flight with the engines' shimmer on |
| 5 | The multiply decal through the production `TemporalPass` on a moving hull | `hull_scorch_through_temporal` |
| 6 | Whether the Mayhem pins match the user's reading of his weapons (names only) | Question 3 below |
| 7 | The ion class at k 1 (IONC: arcs over R 1.5 r = 48 m on a capital) | The `ion-ionc-capital` preset; if it reads as a net, R 1.0 r for the heavy band |

## 11. Options considered and why they lose

- **One look, bigger (the rebuild without classes).** The user's objection was identity ("the same decals"), not size;
  a bigger identical hit is a bigger decal. Loses.
- **Classes by the vanilla impact row (col 20).** The mod author's own effect choice is a tempting cue, but one row
  serves unrelated weapons (Mayhem row 202 serves IRE, EBC, HEPT, MD, PAC, CIG, PSG, GC and PPC [M]) and row ids are
  per table, so every mod would need its own map. The id pin wins; the row stays a lab reference.
- **Classes by name substrings** ("ION", "PLASMA", "MASS"). Names are not data the DLL sees (the generator resolves
  ids to rows), a substring rule misfires (Ion Shard *Railgun*, Plasma *Beam* Cannon), and it would be a second
  classifier. The pin list is the same knowledge, explicit.
- **A finer class set** (laser vs pulse, gatling vs cannon, torpedo vs missile). Each needs a cue the record lacks
  and a preset, a gate and a kill rule; nine classes already strain the lineup. Calibre carries the within-class
  variation.
- **Spark asymmetry from an estimated normal** (reflect the shot about the hull-box gradient). Rejected in
  weapon-look 8 for good reasons; the shot axis alone (data) gives the asymmetry the references want, and the lane
  supplies the "surface" by hiding the inside half. The axis wins.
- **A tangent-plane ring from a lane-gradient normal.** The screen-space normal of the depth buffer is noisy at hull
  edges and undefined on sky; the sphere-shell-intersected-with-lane ring needs no normal and is a 10-slot change to
  the glow. The surface ring wins.
- **Arcs as polylines of streak quads.** A polyline floats off the hull between vertices and needs a normal to lie
  on it; the filament glow is on the surface by construction. Loses.
- **A dark scorch by additive means** (none exists) or **no scorch** (the ember only). Every reference keeps a
  10-30 s mark; the additive ember cannot darken. A multiply decal is the smallest non-additive step (no texture,
  no sort, the glow's box), and it is switchable. The scorch wins, behind a switch and a kill rule.
- **The scorch in the ring.** A 20 s record would overflow the 256-ring at 13 hits/s and drop live hits; the
  per-owner store is 4 KB and keeps the ring's life. The store wins.
- **Smoke / vapour puffs now** (the missile's venting, the ballistic's grey puff). Non-additive, ghosting-prone,
  deferred by weapon-look question 5 with the explosions' smoke; the dust puff at low T is the additive stand-in.
- **A flipbook for the puff.** Image assets and motion-vector blending (reference §3); the impostor is the rule.
- **Heat shimmer inside the stage** (a copy of the colour buffer per frame). The shimmer pass already owns a copy,
  post-resolve, with its own rect budget; a second copy would cost a full-screen blit per frame. Reuse wins.
- **Pulsed vs sustained as two classes from lifetime against refire.** The record has no refire (TLasers); the hold
  rule reads the tick stream and gets both without a table.
- **Beam ticks as separate hull hits (no hold rule).** 60 records per second per beam overflow the ring in 4 s and
  add 60 embers; the shield cells' reset rule is the precedent. The hold rule wins.

## 12. Questions only the user can answer

1. **The scorch** (a dark mark that fades over 10–20 s, the only non-additive element): on from the first flight for
   energy, plasma, heavy, beam and missile (recommended: it is the post-hit every reference has, and it is behind a
   switch with a kill rule), or additive only (embers twice as long) until the rest is accepted.
2. **Asymmetry from the shot direction** (ballistic splash-back, the heavy class's jets): use the recorded heading
   (recommended: it is data, and a radial-only spray is what read as a flash), or keep every class isotropic.
3. **The Mayhem 3 pins** of section 2 (which ids are ballistic, ion, plasma, heavy): confirm or correct from your
   knowledge of the mod's weapons; the script prints the full list.
4. **The ion colour**: a fixed electric blue-white (recommended; Mayhem's lights are white so the tint carries
   nothing), or the heat ramp like every other class (then ion differs by shape only).
5. **The shimmer** on heavy and plasma hits: on at 2 rects (recommended; post-resolve, judged in flight only), or off
   until the engines' shimmer has been judged alongside it.

## Reproduce

```sh
# the class rule and the pin list over the offline catalogues (no game bytes, no Wine)
python3 verification/results/weapon-effects/hull_class_census.py > verification/results/weapon-effects/hull_class_census_out.txt
# per-weapon vanilla impact rows and flags, the catalogue (tools/effects/weapon_catalog/*.json, field effects.impact)
```
