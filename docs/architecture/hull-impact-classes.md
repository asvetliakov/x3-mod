# Hull impact classes: one anatomy per weapon class

Design note, 2026-11-06. Refines [weapon-look.md](../archive/effects-look-2026-11-08/weapon-look.md) 3.1 (the hull hit, step 2) against the reference
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

> **Look layer scratched (2026-11-08).** The hull-hit anatomies this note specified were built and removed after Run246
> ([archive](../archive/effects-look-2026-11-08/index.md)). The class decision of sections 1 and 2 stays in production:
> the hook writes the class nibble of `Event::detail` (`core::hull_class_from`, the override's `hull` pins in the weapon
> table) and the census rows carry it. Sections 3 to 12 are pruned to one line each.

## Outcome

**Nine classes, one language.** The hull hit keeps 3.1's skeleton (flash, sparks, glow, ember, the burst the rebuild
adds) and gives each class a different *second layer* and a different *post-hit*, which is where the references say
the identity of a hit lives: energy hits puff and glow, ballistic hits spray two spark lifetimes and chips and leave a
pit, plasma hits bloom into a tinted globe with a surface ring and a heavy scorch, heavy (railgun-like) hits streak
along the shot axis with a back-jet, an exit jet on thin hulls, a ring, a shimmer and a glowing hole, beams hold a
boiling spot in the weapon's colour that crawls and leaves a scorch line, ion hits throw re-striking lightning and
leave nothing, missiles add
debris to the burst of 3.5, repair and tug stay the tinted glows of 3.1, and an asteroid victim takes a dim dust puff
and rock chips. Two primitives are new and both are the glow primitive with a different profile (a **surface ring**:
the expanding sphere shell intersected with the lane, so no normal is needed; the ion's **lightning segments**: one
quad per polyline segment, the core and corona profile drawn on the lane under it (2026-11-07, replacing the noise
filament glow), one is a blend mode (the **scorch**: a multiply decal, the only
non-additive element, 10–20 s), and one is a reuse (the engine shimmer pass's rect list, post-resolve). The class is
decided at the hook from the live record (flags, damage band, body shape) into the free nibble of `Event::detail` [S],
and the override file's `hull` key pins it per `SS_BULLET_*` id: 12 pins on stock AP, 22 on Mayhem 3, whose flag
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
| stock AP (45) | EMP, HEPT, IBL, PSG, TERANTIFIGHTER → plasma; FBL → ballistic; IPG, IOND → ion (IOND's beam bit `0x4` would win over its on-hit `0x200`; review 2026-11-07); REPAIR, TUG (existing) | ballistic 5, beam 8, energy 13, heavy 3 (GAUSS, PPC, TERRANANTIFIGHTER_1), ion 5, plasma 9, repair 1, tug 1; 12 pins |
| Mayhem 3 (38) | MD, GC, FBL, CFA, FAA → ballistic; ID, IC, ISR, IPG → ion; EMPC, PBG, SSC, PSG, PSP → plasma; RG, BKE → heavy; PALC, PBE, MDS → energy (orb bodies that are not plasma); RL → repair (existing) | ballistic 5, beam 1 (PBC), energy 16, heavy 3 (GKE, RG, BKE), ion 4, plasma 8, repair 1; 22 pins |
| FL (45) | the stock ids (the script gives FL the stock counts row for row [M, `hull_class_census_out.txt`]) | as stock |

The Mayhem pins are my reading of the weapon names (question 3 of section 12 asks the user to confirm them); a
wrong pin costs one line of JSON and a regenerate. A mod with unknown ids gets the rule above, which on a flag-less
table degrades to energy / plasma / heavy by body and damage, never to nothing.

## 3. What this note adds to the shared vocabulary

Removed with the look layer on 2026-11-08 (the full section at `10880db5e:docs/architecture/hull-impact-classes.md` and on branch `effects-look-archive-2026-11-08`): [archive](../archive/effects-look-2026-11-08/index.md).

## 4. The anatomy per class

Removed with the look layer on 2026-11-08 (the full section at `10880db5e:docs/architecture/hull-impact-classes.md` and on branch `effects-look-archive-2026-11-08`): [archive](../archive/effects-look-2026-11-08/index.md).

## 5. Colour sources

Removed with the look layer on 2026-11-08 (the full section at `10880db5e:docs/architecture/hull-impact-classes.md` and on branch `effects-look-archive-2026-11-08`): [archive](../archive/effects-look-2026-11-08/index.md).

## 6. What stays, what it costs, what is shed

Removed with the look layer on 2026-11-08 (the full section at `10880db5e:docs/architecture/hull-impact-classes.md` and on branch `effects-look-archive-2026-11-08`): [archive](../archive/effects-look-2026-11-08/index.md).

## 7. Lab presets

Removed with the look layer on 2026-11-08 (the full section at `10880db5e:docs/architecture/hull-impact-classes.md` and on branch `effects-look-archive-2026-11-08`): [archive](../archive/effects-look-2026-11-08/index.md).

## 8. Fixture gates

Removed with the look layer on 2026-11-08 (the full section at `10880db5e:docs/architecture/hull-impact-classes.md` and on branch `effects-look-archive-2026-11-08`): [archive](../archive/effects-look-2026-11-08/index.md).

## 9. Kill criteria

Removed with the look layer on 2026-11-08 (the full section at `10880db5e:docs/architecture/hull-impact-classes.md` and on branch `effects-look-archive-2026-11-08`): [archive](../archive/effects-look-2026-11-08/index.md).

## 10. Unknowns and what settles each

Removed with the look layer on 2026-11-08 (the full section at `10880db5e:docs/architecture/hull-impact-classes.md` and on branch `effects-look-archive-2026-11-08`): [archive](../archive/effects-look-2026-11-08/index.md).

## 11. Options considered and why they lose

Removed with the look layer on 2026-11-08 (the full section at `10880db5e:docs/architecture/hull-impact-classes.md` and on branch `effects-look-archive-2026-11-08`): [archive](../archive/effects-look-2026-11-08/index.md).

## 12. Questions only the user can answer

Removed with the look layer on 2026-11-08 (the full section at `10880db5e:docs/architecture/hull-impact-classes.md` and on branch `effects-look-archive-2026-11-08`): [archive](../archive/effects-look-2026-11-08/index.md).

## Reproduce

```sh
# the class rule and the pin list over the offline catalogues (no game bytes, no Wine)
python3 verification/results/weapon-effects/hull_class_census.py > verification/results/weapon-effects/hull_class_census_out.txt
# per-weapon vanilla impact rows and flags, the catalogue (tools/effects/weapon_catalog/*.json, field effects.impact)
```
