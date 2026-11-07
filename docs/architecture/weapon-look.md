# Weapon look: one visual language for every weapon effect family

Design note, 2026-11-06 (design agent, Opus 5.5). **Ratified 2026-11-06 by the main session** as the weapon look for
every family; it folds in what the parallel design `weapon-look-fable.md` (Fable 5.1, same brief, archived by the main
session) found that this note lacked: the three-frame age floor, isotropic sparks, the missile trail through the
engine-effects plume path, the explosion's exposure cap and smoke switch, and its more specific gates, presets and
kill criteria; where it argued otherwise and lost, section 8 says why. Not implemented; no source edited. It fixes the
look of every family the proxy will draw through the ratified hook architecture of
[weapon-effects.md](weapon-effects.md) (sites, `Event`, classifier, ring, stage, modes; section 5.1.1 is the accepted
shield look this note keeps word for word), so that steps 2 to 7 are briefed from this note without another design
round. Hooks, the record schema and suppression are not redesigned here; where a family needs a hook the
architecture did not claim (the per-bullet tap of the spawn note 8.2), it is named with its evidence.

Inputs: the architecture and its ledger [weapon-effects.md](../verification/weapon-effects.md) (Run235 A to Run239 F),
[weapon-effect-spawn.md](../reverse-engineering/weapon-effect-spawn.md), the catalogue
[weapon-catalog.md](../reference/weapon-catalog.md) / `tools/effects/weapon_catalog/*.json`, the lab
`tools/effects/weapon_effects_lab.html` (and its generator), the dropped per-class rendering
[effects-modernisation-opus.md](effects-modernisation-opus.md) 2.2 to 3.5, and the accepted engine work
[engine-effects-modern.md](engine-effects-modern.md), [engine-exhaust-look-critique.md](engine-exhaust-look-critique.md),
[engine-light.md](engine-light.md), [bolts-through-taa.md](bolts-through-taa.md), [bolt-footprint.md](bolt-footprint.md)
and the ledger [engine-effects.md](../verification/engine-effects.md). Vanilla timings and beam envelopes quoted below
come from [`vanilla_timings.py`](../../verification/results/weapon-look/vanilla_timings.py) →
[`vanilla_timings_out.txt`](../../verification/results/weapon-look/vanilla_timings_out.txt).

Marks: **[M]** measured (a cited ledger row, census or script output), **[S]** static (code or archive read),
**[I]** inferred. Every number without a mark is a proposed header constant, to be tuned in the lab and pinned by the
fixture before its flight. Costs are GPU at 5120x1440 unless a line says otherwise.

> Ratified 2026-11-06 by the main session over the parallel Fable note (archived as docs/archive/weapon-look-fable-2026-11-06.md, its stronger points folded in per section 8). The section 9 questions are answered with the recommended choice unless the user overrides: bolts in both views with a cockpit kill switch, heat colours for hull hits, vanilla asteroid deaths, momentum-then-stop fireballs, smoke off at first, own muzzle flashes at 0.35 in the cockpit, the listed flight order.

## Outcome

**One language, three colour owners, one size unit, one clock, one pass.** What belongs to the *target* is coloured
by the target's state (the shield ramp of 5.1.1 for shields; a single blackbody **heat ramp** for hull, fire, embers and
explosions); what belongs to the *weapon* (bolt, muzzle, beam, a burst's shock rim) carries the classifier's
**weapon tint**; nothing else carries colour. Everything on a target is sized in that target's **cell unit U** (the
accepted shield cells: 3 / 6 / 16 m for fighter / corvette / capital, stations 16 m) scaled by the hit's calibre k;
everything of the weapon is sized by its calibre in metres with the accepted pixel floors (3 px wide, 12 px long). Every
element is a white-hot core that may clip inside a body held at 0.3 to 1.5 linear (where the hue survives AgX) and a
tight halo, aged in game time from `cfg+0x718`, **never shorter than three frames** (so SETA shortens everything but
nothing falls between frames), fading linearly with the shield's ±20 % stagger. Positions come only from recorded
engine positions (the impact point, the muzzle `&pos`, the bullet and dying-object nodes); nothing is projected through
a centre or placed from the bolt's travel, and no family needs a surface normal (sparks are isotropic; the lane hides
the half that flies into the hull). Particles are closed-form formulas of their age, not a simulation, so SETA is exact.
Light is one screen-space **glow** on whatever surface the depth lane holds. All of it is additive FP16 in the stage
inside the TAA resolve, except bolts (and beams once their draw is identified), which are drawn in place of the game's
late bullet draw so that they keep the single-copy rule, the depth buffer and the far-composite flag the user accepted
for bolts. One frame budget (the shield's 8-screen bound, about 1.4 ms) is shared and shed in a fixed order. Each
family has a simple mode and a kill criterion, lab presets, a fixture with placement / size / no-pop gates and one
flight, in the order **hull hit, explosions, bolts and muzzle, beams, missile impacts, flak bursts**; missile trails
are an engine-effects change flown under that feature, asteroid deaths stay vanilla.

| Family (step) | Look in one line | Driven by | Simple mode (kill fallback) |
| --- | --- | --- | --- |
| Shield hit (1, done) | hex cells on the bubble / station decal, shield ramp | impact site, shield fraction | as accepted (Run239 F) |
| Hull hit (2) | 60 ms white flash, 8 to 24 isotropic cooling sparks, a 150 ms glow on the hull and a 1.2 s ember | impact site `hull_hit`, calibre, victim tier | flash + glow + ember, no sparks |
| Explosions (3) | flash, noise fireball cooling white → red, shock shell, embers, glow on nearby hulls; capitals and stations add secondaries; smoke off | both death sites, class-id tier, radius | smooth fireball (no noise), peak 2; per tier native |
| Bolts and muzzle (4) | white capsule core in a tinted body, screen-space streak, a straight fading tail for heavy shots; 120 ms muzzle flash + own-hull glow | per-bullet tap `0x0045ac7d`, muzzle site, classifier | no tail; cockpit native; then native bullet draw + footprint |
| Beams (5) | white core line in a tinted sheath with scrolling noise; envelope from the beam object's life | tap (beam objects), muzzle records | static sheath; then the in-place slot or native |
| Missile impacts (6) | hull hit + burst on the hull; area missiles a burst | impact and death sites, class-10 source | hull hit only |
| Flak bursts (7) | the explosion law at burst size, weapon-tinted shock rim | death site class 0 flag `0x100`, fragments `0x1000` | flash + shock only |
| Missile trails (engine effects) | the plume the missile already has, plus an ember ribbon in the missile tint | redirects A/B for class 10, the ribbon builder | plume only |

## 1. What the accepted work teaches, as rules

| Lesson | Source | Rule here |
| --- | --- | --- |
| Placement through the owner's centre put patches 42 to 422 m off the hull | Run235 B [M] | Only recorded positions place anything (2.6) |
| A face chosen from the bolt's travel failed on shallow hits (of 225 hits on a ±y face only 79 took it) | Run236 C [M] | The bolt's direction never decides geometry, and no family estimates a normal: sparks are isotropic (3.1) |
| A nose hit seen from behind must show: the bubble's far side draws round the outline | Run239 F accepted; ledger "Ship bubble" [M] | Every family on a target passes a chase-nose gate: lit pixels outside the hull outline (2.9) |
| A hex at 0 % "reads as a bug"; drained hits became hull hits | Run237 D [M] | The picture follows the HUD over the physics: the role decides the look, never the residue of a rule |
| The own patch in the cockpit covers the view: 0.35 | Run236 C | One own-cockpit gain for every effect owned or fired by the own ship (2.9) |
| Cells too small at first; doubled | Run235 B | Target sizes in the target's cell unit U, never per screen (2.2) |
| Every look took two or three flights of tuning | ledger | Lab presets and header constants first; no user settings |
| A plume body at 4 to 8 linear loses its hue in AgX; light must be moved, not added | exhaust critique §2 [M] | Core may clip, body 0.3 to 1.5 linear, halo tight (2.3) |
| Excess end-on glow and lamp-like far discs were rejected | Run 127/128 [M] | No lens flares, halos at most 0.2 of the core, no sheath |
| Bolts vanish over held far pixels unless they bypass the history | bolts-through-taa, Run 94 A | Bolts and beams draw in place of the late bullet draw with the `RT2.g` flag (2.10) |
| Bolts were "barely visible small white balls" | Run 73 B [M] | Floors 3 px wide, 12 px long; the streak covers the previous position (2.5) |
| Per-cell columns cost 2.5 to 9 x the planar patch | ledger [M] | Back-face boxes, one shading per pixel, a shared area budget (2.11) |
| SETA: the user flies with it on | memory, 5.1 | Every age is game time; no element lives fewer than three frames (2.4) |

## 2. The shared language

### 2.1 Colour: three owners

| Owner | Colour | Used by |
| --- | --- | --- |
| The target's shield | the accepted ramp: light blue ≥ 75 %, light yellow near 50 %, light red ≤ 35 % (`colour_full/half/empty`) | shield patch only |
| Heat (target hull, fire) | `look::heat_ramp`, linear, by temperature T: 1 white (1.0, 0.92, 0.80), 0.75 yellow (1.0, 0.72, 0.30), 0.5 orange (1.0, 0.42, 0.10), 0.25 red (0.55, 0.10, 0.03), 0 black; radiance = ramp(T) x E x T, so a cooling element darkens and saturates together | hull flash halo, sparks, glow, ember, fireball, embers, missile ember, burst fire |
| The weapon | the classifier tint (light colour when saturated, else the body's texture mean, else white; architecture 4.2), the override's `tint` where pinned | bolt body and tail, muzzle, beam sheath, burst shock rim, repair and tug glows |

Cores are white in every family. The weapon tint never colours a target: on Mayhem 3, which the user flies (the Flight A
table loaded 38 rows [M]), 35 of 38 lights are white and the tint falls to texture means (36 of 38 [M, census]), so a
weapon-coloured hit would be the texture's average, not information. `weapon_tint` stays the override it is for
shields (5.1.1), per family.

### 2.2 Size: the target unit U and the calibre k

- **U** (metres) = `core::cell_m` of the victim's patch tier: fighter 3, corvette 6, capital 16; docks and factories
  take the capital's 16; anything else the radius bands (below 30 m / 150 m) already in `patch_tier_of_radius` [S]. The
  tier comes from the one function the shield uses (`patch_tier_of_ship`, TShips class id: TM, M8, TS, TP are corvettes
  there [S]); explosions use the same function, so a ship is one tier in every family.
- **k** = `clamp(log10(d / 300) / 2, 0, 1)` (architecture 4.2). Weapon-owned sizes use d per shot of the record
  (max of cols 7 and 22); target-owned sizes use the impact's running total `[esp+0x1c]` (the damage applied).
  Mayhem 3 spans d 428 to 94,130 (k 0.08 to 1) [M].
- **R_obj** (metres) = `obj+0xa4` / 500 (`engine_units_per_metre` [S]) for explosions, clamped per tier (3.2).
- **Pixel floors** (render pixels at the presented resolution): a core never narrower than 2 px, a lit footprint never
  narrower than 3 px, a bolt never shorter than 12 px (the accepted `--bolt-footprint 3,12` defaults, Run 73 B), a hit
  flash core never under 3 px. A floor raises size, never radiance.

### 2.3 Radiance

Linear, engine space, before the TAA and AgX: core `look::core_e` 4 (may clip white), body 0.3 to 1.5, halo peak at
most 0.2 of the core and e-folded within 1.5 core radii (the plume's `hb` 0.20 and its tight halo), shield cells as
accepted (`cell_gain` 2). A frame-filling element (fireball, glow) never exceeds the body level, so a capital explosion
lights the frame without whitening it (the exposure meter's response is unknown 7). No lens flares in any family.

### 2.4 Time

`age = now_game − t0` (`cfg+0x718`), every life in game ms, so SETA shortens everything exactly as it does the engine's
own effects. **Three-frame floor**: an element's effective life is `max(life, look::min_frames x step_game)`, with
`min_frames` 3 and `step_game` the frame's game step (`cfg+0x714` [M, engine-effects.md §8]), and its first drawn
frame uses age 0. At SETA 6 a 60 ms flash is 10 ms of wall time, under one frame: drawn once, it would enter the resolve
at the 0.37 to 0.66 a single bright frame keeps [M, motes] and be gone; over three frames it is seen, while everything
longer than three frames still shortens with SETA exactly as the engine's own effects do. Fades are linear to 0 at the element's life, per element x (1 ± `stagger`/2) by a hash of its
key (the shield's 0.4); heat elements cool T linearly over their own life. No fade-in longer than one frame; no white
flash longer than 80 ms game (5.1.1: "no hard flash").

### 2.5 Motion

A moving element is drawn from its **previous frame's screen position to its current one**, both through their
frames' own view-projections (the motes' streak precedent, `fog_dust_motes_vs.hlsl` c8, `camera_reprojection.h` [S]),
never shorter than its rest shape. This covers the previous position for the resolve's clip box (the TAA lesson) and
stays right in the chase view, where the camera moves with the shooter. Under SETA the streak grows with the game
step; it is capped at `look::streak_cap` 6 rest lengths and its radiance scales by rest / (rest + streak), with the core
held at E ≥ 1.2 so it survives the resolve [M, motes temporal row: cores above E 1 keep 1.00].

**Particles are formulas.** A spark or ember is `p(t) = p0 + v0 τ (1 − e^(−t/τ))`, with p0, v0 fixed on the record's
first stage frame (world space, the owner's velocity added) and its seed from the record key and index. The vertex
program evaluates p(t) and p(t − Δt_game); the CPU writes one quad per particle with (p0, v0, t0, seed) and simulates
nothing. Exact under SETA, deterministic for the fixture, no pool to age.

### 2.6 Placement

| Family | Anchor | Frame |
| --- | --- | --- |
| Shield, hull hit, missile hit | `Event::pos`, the engine's contact point (`weapon_pos_check` max error 0 over 1,165 hits [M, Run235 A]) | owner, re-posed every frame through the owner's rows (validated by handle and serial) |
| Muzzle | the fire routine's `&pos` and orientation | shooter [I, spawn note §2] |
| Bolt, beam | the bullet node `+0x30` (and `+0x40`, `ext+0x78` for beams), read by the tap every pass | world |
| Explosion, burst, missile death | the dying object's node `+0x30` at the death site, plus the damped drift of 3.2 | world |
| Sparks, embers | p0 = the anchor in world at the first stage frame | world (they outlive their owner) |

No family needs a surface normal. Sparks are isotropic and the lane hides the half that flies into the hull, so a
spray always reads as leaving the surface; a normal estimated from the hull box, the centre or the bolt's travel would
bring the two rejected placement rules back through a side door (section 8). **Amended 2026-11-06**
([hull-impact-classes.md](hull-impact-classes.md) 3.4, ratified): the hull hit's ballistic, heavy and missile classes
bias a spark set along the recorded shot heading, v = speed normalize(u + b a) with u the hashed isotropic direction and
a = `Event::dir` through the owner's rows at the record's first frame (data, not an estimate; b < 0 back towards the
shooter, b > 0 through the hull, b 0 when `dir` is zero); the bias moves no anchor and needs no normal. The shock ring
of 3.1's burst rebuild no longer takes a normal either: it is the surface ring (the sphere shell cut by the lane).

### 2.7 Occlusion

1. **Surface-bound** (glow, ember, the station decal): drawn only where the lane holds a surface inside the element's
   sphere; never on sky. Whatever surface is there is lit, an occluder too (physically right: a light lights what
   stands in front of it).
2. **Volumetric** (flash, halo, sparks, fireball, shock, muzzle, burst): analytic depth of the shape (ray–sphere,
   capsule, streak) against the lane, `vis = saturate((d_lane − z) / (SOFT x r))`, the sky never hides, an invalid
   texel hides [S, the plumes' and the bubble's rule]; SOFT is the plumes' measured pair: 0.15 for cores and sparks,
   1.0 for halos, fireballs and shock shells (a fireball half inside a station fades at the hull) [M, plumes]. A
   hit-point emitter is biased towards the eye by 0.5 r_hit, so its own surface does not cut it in half.
3. **Bolts and beams**: the hardware depth test of the late bullet draw (LESSEQUAL, Z write off), which holds the
   whole near pass [S, bolts-through-taa].

### 2.8 Light: one glow primitive

`E = colour x saturate(1 − d² / R²)²`, d the 3D distance from the light point to the scene point the lane rebuilds (the
decal's reconstruction through the jittered projection), the falloff of [engine-light.md](engine-light.md); no N·L, no
normal (one lane fetch per pixel, the decal's code). Drawn as a box of radius R, back faces only, additive. Used for the
hull-hit glow and ember, the muzzle's light on its own hull, the explosion's light on nearby hulls and the burst's. The
engine's bullet light node (cols 13–15, `0x00444a70`) is not a spawn and stays native.

### 2.9 Own ship

- **Cockpit**: every element attached to the own hull (the shield patch, hull hits, the muzzle flash and its glow)
  takes `own_cockpit_gain` 0.35 in the cockpit view, by the shield's test (`resolve_own_ship` root, a fresh
  chase-camera visit with view mode 1) [S]. Own bolts and explosions are not dimmed (they leave the hull; dimming them
  would cost the visibility the bolt floors exist for). The chase view and every other ship are unchanged.
- **Chase-nose gate**: every target family's fixture draws the bubble's F3 camera (behind and above a 4 x 1.6 x 12 m
  hull, 24 m ahead, pitched 0.175 rad, a nose hit [M ledger]) and requires lit pixels outside the hull's outline within
  300 ms of game time. A hull hit meets it with its sparks (reach 2 U past the nose), an explosion trivially.

### 2.10 Pass, TAA, programs

- One `WeaponEffectsPass` callback after the plumes callback (the step-1 chaining [S]), ONE/ONE on the FP16 target,
  alpha untouched, lane occlusion. Draw order within it: glows and embers, shield patches, fireballs and shock shells,
  the sprite batch (flashes, halos, muzzles, sparks, embers: one dynamic VB, one draw), the beam ribbons while beams
  are in the stage (3.4). Missile ribbons stay in the engine-effects stage (3.5).
- **Bolts are not in the stage** (beams join them once their draw is identified, 3.4). The stage samples the lane with RT2 unbound, so it cannot write the
  `RT2.g` far flag of B' in the same draw; drawing bolts there would bring back the averaging over held far pixels that
  bolts-through-taa measured (keep 0.99 over a station [M]). Drawn in place of the late bullet draw they inherit what
  the user accepted: the single copy, the depth buffer, the oC2 flag (oC1 = oC0 for the native validator) and the
  write-back composite at W.
- Programs vs_3_0 / ps_3_0, created at the latch, never at first use; each in the low hundreds of slots (the fireball
  the largest, about 300 [I]); `MaxPixelShader30InstructionSlots` logged as today.

### 2.11 Budget, shared

`core::screen_budget` (8 full-screen patch boxes, 1.42 ms [M]) becomes the stage's one bound: every draw is charged its
projected box area in screens x its family weight w (the family's full-screen µs over the decal's 177.5 µs, measured by
its fixture and pinned in the header). Over budget, shed in this order: (1) the fireball's octave LOD (already a law,
3.2); (2) glows and embers of owners other than the own ship, oldest first; (3) shield patches, oldest owner first (the
existing rule); (4) secondary fireballs past the first two. Never shed: bolts, beams, sparks, flashes and anything owned
by the own ship (thin or small, bounded by count caps). Count caps (CPU, static): live hull hits 256 (the ring), sparks
2,048 quads, bolts 512, beams 64, fireballs 32 + 64 secondaries, bursts 64, muzzles 128. Flight targets: `stage_us`
p90 ≤ 0.3 ms in a normal fight, the budget ≤ 1.5 ms worst.

**Amended 2026-11-06** ([hull-impact-classes.md](hull-impact-classes.md) section 6, ratified; built, not flown). The
hull stage's order over budget: (0) the heat-shimmer rects of owners other than the own ship (past `hull::shimmer_max`
2 always); (1) the puffs' second octave (a puff under `hull::puff_lod_px` 96 px at 1440 rows draws one octave at any
budget), then other owners' puffs past two per owner, oldest first; (2) other owners' scorches, oldest first; (3) their
surface rings and filament glows; (4) their glows and embers, oldest first, then their remaining puffs; (5) shield
patches; (6) secondaries; last, other owners' flashes, oldest first (charged at `hull::budget_weight_flash`; review
2026-11-07: about 80 plasma blooms on a close capital passed the budget otherwise). Never shed: entry streaks, sparks,
chips, arc sparks, beam spot cores and anything of the own ship. A hull record leaves the ring at its anatomy's slowest
element (`core::hull_lives`, 181-3,601 ms). Weights [M,
5120x1440]: ring 0.50 (0.40 until the shock front, 2026-11-07), hull shimmer rect 2.85 (charged from 2026-11-07),
filament glow 0.86, scorch 0.49, flash 0.49 (glow 0.40, puff 1.04 / 0.76). Count caps add:
puffs 256, rings 64, filament glows 32, beam spots 64, scorches 128 (16 per owner, the oldest of an owner retires),
shimmer rects 2.

### 2.12 Modes and kill criteria

Each family has `core::<family>::mode` = `full` | `simple`, a header constant, and its family token in the existing
`weapon_effects` list (`native | observe | shields,hull,explosions,bolts,beams,missiles,bursts`, `missiles` arming the impacts and area deaths only); a family not listed
is native, which stays the user's rollback. A kill criterion is an observation in flight; it flips the header to
`simple` in the next build, and a simple mode that still fails takes the family out of the default list. No user
setting is added by any family.

### 2.13 Sound

Untouched in every family: the impact sound (`0x00417250`), the explosion sound (`0x00417170`) and the fire sound
(`0x00446b5c`) are separate calls after or beside the spawn calls the hooks skip [M, spawn note §3, §4].

### 2.14 The primitives

| Primitive | Program | Families |
| --- | --- | --- |
| Hex patch (bubble / decal) | `weapon_bubble_ps`, `weapon_shell_ps` (built) | shield |
| Sprite batch: disc core + Gaussian halo, streak quads, closed-form particles | one VS / PS, one dynamic VB | hull flash and sparks, muzzle, explosion flash and embers, burst |
| Glow (surface light) | box VS, lane PS (the decal's reconstruction) | hull glow and ember, muzzle light, explosion and burst light |
| Fireball impostor | ray–sphere PS, two fbm evaluations, heat ramp | explosions, bursts, missile hits |
| Shock shell | ray–sphere PS, limb-bright shell | explosions, bursts |
| Capsule / beam ribbon | VS / PS drawn in place of the bullet draw, oC2 flag | bolts, beams |
| Ribbon | the engine ribbon builder (built, engine effects) | missile trail (flown under engine effects) |

## 3. The families

### 3.1 Hull hit (step 2, next)

**Records.** `hull_hit` from the impact site, already recorded and forwarded since step 1: `absorbed == 0`, the
hull-through-shield rows (flag `0x20`: MASS, the repair lasers), drained shields (`detail_drained`) and class-10
sources (`missile_hit`). Step 2 arms `hull` and skips the native spawn for them as step 1 does for shields (the
outer site, so the `PROPAGATE` children and `EEDF_LIGHT` go with it [M, 2.2]); a dying hit object keeps forwarding.

**Look.** r_hit = 2 U x (0.35 + 0.65 k) (`hull::r_scale` 2 since 2026-11-06, the user: too small on ships tens of
metres long, as the shield cells and the plumes): an IRE on a fighter (Mayhem d 1,358, k 0.33) 3.4 m, a PPC on a capital
(d 11,957, k 0.80) 27.8 m. The flash, glow and ember scale with r_hit; the sparks' reach and width stay in U.

| Element | Shape | Colour | Size | Life (game) | Motion / fade |
| --- | --- | --- | --- | --- | --- |
| Flash | disc core + halo | white core E 4, halo heat T 0.9 | core 0.25 r_hit (≥ 3 px), halo r_hit | 60 ms | linear; three-frame floor |
| Sparks | streak quads, closed form | heat T 1 → 0 over life, E 8 at birth | n = round(8 + 16 k); width 0.08 U (≥ 3 px), rest length 3 widths (≥ 24 px); speed 6 U/s x (1 ± 0.3), τ 0.45 s (reach ≈ 2.7 U), **isotropic** directions, plus the owner's velocity | 250 + 250 k ms ± 20 % (the architecture's 250–500) | streak rule 2.5 |
| Glow | surface light | heat T 0.85, E 1.0 | R = 3 r_hit | 150 ms | linear |
| Ember | surface light, small | heat T 0.4 → 0, E 0.6 | R = 0.6 r_hit | 1,200 ms | linear with cooling; the persistent "here" cue, as the shield's marker cell |

Variants. By the victim, no override: an asteroid (class 17, mining) takes the sparks on the ramp's last stops at
2 x the count (rock dust, dark red to grey) and no flash. By override (`by_id`): `REPAIR` and Mayhem `RL` style
`repair` (glow and ember only, weapon tint, no flash, no sparks, re-lit per tick); `TUG` style `tug` (glow only, pale
blue, calibre 0.1). Missile sources (`missile_hit`) take 2 x the sparks and the missile tint on the flash halo; step 6
adds a burst. Drained shields stay hull hits (the accepted rule: the HUD at 0 % agrees with sparks). One look for
every race and class, as the shield.

**2026-11-06, after Run240 Flight G** (the user: "need good modern effects, not lazy flashes; that's a downgrade";
"sparks read as the same decals appearing and disappearing"). Built, not flown; figures in the
[ledger](../verification/weapon-effects.md) "Burst, randomised sparks, heavy ring". (1) **A hot-gas burst on every hull
hit** (not the repair, tug or asteroid variants): the fireball impostor of 3.2 (`weapon_fireball_{vs,ps}.hlsl`, one
program whose radius, life, heat, radiance, advection and seed are per-vertex inputs and the octave count a constant,
so step 3 draws explosions with it at full size) at the contact point, riding the owner: R(t) = R_b (1 − e^(−t /
0.15 L)), R_b = 1.5 r_hit, L = 180 + 220 k ms (x (1 ± stagger/2), the three-frame floor), heat T 1 → 0 over L on the
ramp (white core at birth, dark red at the end), body 1.5, the chord's closed-form density of 1 − r²/R² clipped at the
lane (the half inside the hull is not there), two octaves of value noise advected outward, eroded as it cools; charged
to the shared budget at `budget_weight_fireball` 1.04 (0.76 at one octave, the first shed step). The flash is a short
core inside it: halo 0.5 r_hit, 60 ms. (2) **Randomised sparks**: per hit the count round(n (1 ± 0.4)) hashed from the
record's sequence, antipodal pairs of hashed isotropic directions (the lane still hides one of each pair), per spark
speed x 0.5..1.5, rest length x 0.6..1.6 (after the 24 px floor), width x 0.8..1.2, life x 0.7..1.3; a quarter drift
sideways at 0.3 of their speed (constant, undamped: p(t) = p0 + v0 τ (1 − e^(−t/τ)) + w t bends the path as the
launch speed decays); a given hit is identical frame to frame. (3) **The heavy shock ring** (a missile source, the burst
style bit `flags & 0x1100` (flak and area shots; the record carries no raw flag word, and the classifier has no
"plasma" style: the plasma weapons arrive through k ≥ `heavy_k` 0.6, HEPT 0.63, PBC 0.80 on Mayhem 3)): a thin ring
in the hull's tangent plane at the hit, radius 0 → 4 r_hit over 0.3 L, heat 0.9, brightest at its leading edge and
x 1/|cos| of the view's angle to the plane (at most 3), lifted 0.25 r_hit off the surface. The ring is the one element
that needs a normal (an exception to "no normal" above): a ship's bubble normal at the hit (the shield stage's
`BubbleCache` walk, one walk a frame, the radius sphere otherwise), a station's face-lattice axis; latched at the
record's first frame. The engine shimmer puff is **not built**: `EngineShimmerPass` exists only with the plumes
requested and draws only on frames whose plume stage drew (its scratch and its revert belong to that path), so reusing
it for weapon puffs would need a lifetime change; skipped as the brief allows. The lab draws all three (presets
`hull/heavy-plasma-corvette`, `hull/heavy-flak-fighter`, `hull/heavy-missile-hit`) with the stage's own hashes, on a
720 x 405 preview framed on 4 U and one glow radius.

**2026-11-06, the hull-impact classes** ([hull-impact-classes.md](hull-impact-classes.md), ratified; built, not
flown; figures in the [ledger](../verification/weapon-effects.md) "Hull impact classes"). The hit's anatomy now follows
its class (energy, ballistic, plasma, heavy, ion, beam; missile, asteroid, repair and tug by their own cues), decided at
the hook into `Event::detail` bits 4-7 and pinned per id by the override's `hull` key; the burst becomes each class's
puff, the tangent-plane ring the surface ring, and the classes add the entry streak, two spark lifetimes and chips with
the shot-heading bias, the filament glow, the multiply scorch in its own store, the beam hold rule and the heat-shimmer
rects on the engine shimmer pass. Sections 2.6 and 2.11 carry the two amendments.

**Engine data.** The record (pos, owner, radius, running damage, row), the patch tier and its source (step 1 fields),
the owner's rows at scene end, the owner's velocity as the delta of its node `+0x30` between stage frames (0 on the
first; no new read, no unit question). No hull box and no normal. The override pins `style` only.

**Placement and occlusion.** The flash, glow and ember ride the owner at `pos`; the sparks leave it in world space on
the first stage frame and survive a kill (the death record retires the glow and ember with the owner, 2.4 of the
architecture). Occlusion classes 1 (glow, ember) and 2 (flash and sparks, SOFT 0.15, the flash biased 0.5 r_hit). A
hit on the far side of its owner is hidden by the owner's hull; its sparks clear the outline. An owner without live
rows draws nothing (the retired rule).

**Cost.** CPU: 24 quads x 64 hits ≈ 1,536 quads ≈ 37 KB of VB at 24 B per vertex, 15 to 25 µs [I]; GPU: sparks and
flashes negligible at fighter range, a glow box at 3 r_hit charged to the budget at w ≈ 0.25 [I: about 40 slots and a
lane fetch against the decal's 177.5 µs per full screen]. Targets: a 20-hit frame ≤ 50 µs GPU fenced at 5120x1440,
64 hits ≤ 30 µs CPU, the family ≤ 0.1 ms of `stage_us` p90 in a fighter fight.

**Kill criterion.** (a) Sparks read as confetti, glitter or noise (the user's words decide), trail through the TAA, or
visibly fly through the hull → the spark count halves once; still wrong → simple mode: flash + glow + ember. (b) A glow
seen on the wrong ship or floating in space → the glow is dropped, flash + sparks + ember remain. (c) The own nose hit
is invisible from the chase camera (the Flight F reference case) → the glow's radius and peak x 2 and the spark speed
x 1.5, once; then the family is judged as it stands. Never the native sprite under our sparks: a family draws natively
or by the stage, not both.

**Lab presets** (`hull/…`): `fighter-ire-side` (Mayhem 3, IRE, flat 6:1:4 hull, side camera, one hit),
`fighter-ire-chase-nose` (the Run239 reference camera: own 4 x 1.6 x 12 m hull 24 m ahead, pitched 0.175 rad, nose hit),
`fighter-ire-cockpit-own` (cockpit view, eight hits per game second beside the eye), `corvette-ppc-flank` (narrow 1:1:6,
mid flank), `capital-ppc-flank-300m`, `capital-close-12hz` (capital sphere front, 12 hits per game second),
`station-ppc-1km` (station decal surface), `mining-asteroid-60m`, `missile-hit-corvette`, `seta6-12hz`, `far-2km`
(the floors), `drained` (shield 0 %: the hull look, no hex), `repair-corvette` (RL), `stock-vs-mayhem` (one id in two
views). Controls: spark count / speed / life / width, flash size and life, glow and ember radius / peak / life, the ramp
stops, `min_frames`, SETA.

**Fixture gates** (`HULL_*` in `run_weapon_effects.py --host native`, step-1 naming): `hull_sparks_born_at_recorded_pos`
(the sparks' first-frame centroid within 0.25 U of P) and the flash's lit centroid within 0.1 r_hit of the projected
hit; `hull_half_of_sparks_hidden_by_flat_lane` (0.45 to 0.55 of the streak pixels survive on a flat hull facing the
eye); `hull_light_on_hull_not_sky` (0 lit sky pixels, ≥ 0.9 of the glow on the lane, its centroid within 0.3 R);
`hull_light_peak_decay_150ms`; `hull_spark_cooling_ramp` (hue at 0 / 0.5 / 1 life); sizes, counts, reach (2.7 U ± 20 %)
and lives per tier at k 0 / 0.5 / 1 against the law ±5 %; `hull_streak_min_footprint_3px_24px`;
`hull_min_frames_under_seta6` (a 60 ms flash drawn on ≥ 3 frames at SETA 6) and the SETA 6 wall-time law for the
longer elements; `hull_no_pop_12hps` (no pixel drops by more than one frame's fade step before an element's end); the
far-side hit hidden, the flash not halved by its own surface; the chase-nose gate (2.9); `hull_cockpit_dim_own_ship`
(0.350); `hull_resolve_core_survival` (≥ 0.95 at 0 / 4 / 8 px per frame through the production `TemporalPass`) and no
residual above one code 8 frames after a spark crossed a held far lane (the bolts-through-taa scar test);
`hull_repair_no_sparks`, `hull_missile_tier`, `hull_asteroid_dust`; `hull_budget_20_hits_5120` (≤ 50 µs fenced),
`hull_cpu_64_hits` (≤ 30 µs), timings at 1080p and 5120x1440; one case on the R32F lane form (`four_channel` false,
the step-1 open item); Reset, fault, program refusal; slots. Hook record: `hull_hit_skips_native_when_staged`,
`drained_forwards_as_hull` becomes staged, `velocity_from_frame_delta`.

**Flight.** `weapon_effects = shields,hull`: an unshielded fighter shot down (its shield drained first), hull hits on a
capital, the own ship hit in chase and cockpit, one stretch under SETA. Rows: `weapon_effects_frame` gains `sparks=`,
`glows=`, `embers=`, `budget_shed=`; F8 captures at a hit. The user must see sparks and a brief glow where the hull is
hit, visible on the own nose from behind, an ember for about a second, no native impact sprite, the hex patch
unchanged on shielded hits, the repair laser without sparks.

**2026-11-07, the burst look** ([hull-burst-look.md](hull-burst-look.md); built and fixture-gated, not flown; figures in
the [ledger](../verification/weapon-effects.md) "Burst body, glow, flash, smoke"). Supersedes the flash, glow and burst
rows above: the **burst body** is a lobed three-octave 2-D noise disc alpha-eroded by erode_0 + erode_t x + erode_r
|q|^2 (1 + erode_rt x) (0.10, 0.30, 0.20, rt 3.0, soft 0.20), R_b doubled per class (energy 2 r_hit), two octaves under
48 px at 1440 rows (never one), radiance heat(T') E density (no extra x T); the **glow** is its light, R_g 1.6 R(t),
E_g 0.33 E_b, the burst's heat and life, saturated per owner cluster (N0 2.5 glow / 2.0 body within 2 R_b); the
**flash** a 40 ms four-arm star of unequal arms (A 0.6-2.0 r per class, core 0.12 r, no halo); a dark **smoke** puff
(the impostor in the scorch's multiply state, hull only) from 0.5 to 1.6 L_b. Budget weights: burst 1.18 / 1.03 (two
octaves), smoke 1.20, star 0.40, glow 0.40; shed order (1) third octave, (2) other owners' smoke, then as before with
bursts past two per owner after the glows. Asteroid: an unlit grey dust cloud (E 0.375, rise 0.08) and grey rock
sparks; repair and tug: a cool spot (0.7, 0.8, 1.0) at E 0.3 / 0.15, R 1.0 r, repair's star A 0.6 r.

### 3.2 Explosions (step 3)

**Built 2026-11-07 for fighters and missiles** (corvettes, capitals and stations forwarded native: `boom::tier_native`):
the look, sizes and laws are [explosion-look.md](explosion-look.md) (ratified; it amends the table and the smoke rule
below), its built numbers in that note's "Built numbers (step 3)", the gates and measurements in
[the ledger](../verification/weapon-effects.md) "Explosions (step 3)". The mode token is `explosions`
(`shields,hull,explosions`, `shields,hull,bolts,explosions`). The records, the asteroid and the rule-(b) missile below
stand as built.

**Records.** Both death sites (2.4 of the architecture), `tier` from `patch_tier_of_ship` for class 7, station for
classes 5/6, missile for class 10, burst for class 0 with flag `0x100` (step 7). `weapon_death` logs class id, col 47 row
and radius; `tier_mismatch` stays the cross-check against rows 35 / 40 / 37.

**Asteroids stay native.** Their death rows are cut scenes (row 59: an `AtoCDE_scene_transition` with `CUTKEEPBODY`
and a dust scene, 13 s [M, vanilla_timings]): rock geometry, not a sprite. The outer call cannot drop the dust and keep
the rocks, so the asteroid tier forwards and the stage draws nothing for it (`boom::asteroid_native` = true).

**Look.** One law, tier parameters:

| Element | Shape | Colour | Size | Life | Motion / fade |
| --- | --- | --- | --- | --- | --- |
| Flash | disc core + halo | white E 6 core, halo heat T 1 | 0.5 R_f (halo capped at 0.25 H on screen) | 80 ms | linear, three-frame floor |
| Fireball | ray–sphere impostor, radial density x two fbm octaves advected outward with age | heat ramp, T 1 → 0 over L, body ≤ 1.5 | R(t) = R_f (1 − e^(−t / 0.15 L)) | L | soft edge, cools and darkens |
| Shock shell | thin sphere shell, limb-bright | heat T 0.9 | 0 → 4 R_f over 0.3 L, shell 0.06 R | 0.3 L | linear |
| Embers / debris | closed-form streaks | heat T 1 → 0 | n per tier; speed 1.5 R_f/s, τ 1 s | 0.6 to 1.0 L, hashed | streak rule |
| Glow on nearby hulls | surface light | heat T 0.85, E 1.5 → 0 | R = 5 R_f | 0.3 L | linear |
| Secondaries (capital, station) | fireballs of 0.35 R_f at hashed points inside the hull box | heat ramp | — | 0.5 L each, starts spread over 0 to 0.4 L | as the fireball |

| Tier | R_f = f x R_obj, clamp (m) | L (game s) | Embers | Secondaries |
| --- | --- | --- | --- | --- |
| fighter | 1.6, [8, 40] | 1.6 | 24 | 0 |
| corvette | 1.2, [20, 120] | 2.4 | 48 | 2 |
| capital | 0.7, [60, 500] | 4.0 | 96 | 6 |
| station | 0.5, [150, 1,500] | 6.0 | 128 | 12 |
| missile | 3.0, [4, 20] | 0.8 | 12 | 0 |

Vanilla for scale: row 35 (fighters) ends at 3.1 s, row 37 (capitals, 30 propagated children) at 22 s, row 40 at 9 s
[M]; vanilla sizes are per row, not per body (no `RELATIVESIZE` on any reachable death row [M]), so a large M1 and a
small M7 explode alike. Ours scales by the body and is shorter. **Fireball first, smoke later**: `boom::smoke`
(premultiplied billboards drawn before the additive layers, cooling to `heat_ramp` 0 grey) exists as a header switch
and is **off** in the first flight; a fireball that cools to dark red and goes is the simpler mode, smoke is the
ghosting-prone, non-additive layer [M, motes], and it is added only if the user misses the lingering half after flight
3 (question 5).

**Motion.** Damped drift: `x(t) = x0 + v0 τ (1 − e^(−t/τ))`, τ = 0.4 s, from the dying node's position and velocity
(`node+0x100` at the site, its unit to be confirmed in the observe flight: unknown 1; until then v0 = 0, which is
vanilla's `NODRIFT`). The fireball keeps the ship's momentum for a moment and stops.

**Placement and occlusion.** World, from the death record only (the node dies with the object); secondaries inside the
hull box `BubbleCache` holds for the owner (no new walk at death; a sphere of 0.7 R_obj without one; `weapon_death`
logs `bubble_cached=`). Occlusion class 2 (SOFT 1.0 on the fireball and shock, 0.15 on the embers); the glow class 1.
Nested kills each draw their own; the death record retires the owner's hit records. **The eye inside a fireball** (a
ram, or the own ship's death): the impostor's box contains the eye and rasterises the screen; the density is evaluated
along the ray from the eye, so the view fills with the ramp's colour at that age, and the shared budget bounds the
cost (2.11).

**Cost.** A fighter at 500 m about 0.012 ms; a close capital 0.22 to 0.45 ms for 1 to 2 s with the octave LOD above
20 % coverage (dropped note 3.5 [I]); re-pinned natively by the fixture, charged to the budget (one close capital or
station death may take up to 3 of the 8 screens). **Exposure**: the meter reads the resolved image; the fixture
measures its EV shift under a full-screen fireball, and above 0.5 EV the fireball body is capped at E 2 and the flash
(small in area) carries the brightness.

**Kill criterion.** The fireball reads as a flat disc, a balloon or a soap bubble, or the exposure visibly pumps on a
capital explosion → simple mode: a smooth radial blackbody sphere (no noise octaves), shock and embers, the fireball
body capped at E 2. A tier the user calls the wrong size moves that tier's (f, clamp) only. A station death that covers
the view for more than a second → `boom::station_secondaries` 0. A tier that still reads wrong goes native through
`boom::tier_native` (a mask), the others keep theirs.

**Lab presets** (`boom/…`): `fighter-m5-300m-chase`, `corvette-m6-600m`, `freighter-ts`, `capital-m2-close`,
`capital-m2-1500m-broadside`, `capital-m2-far` (5 km), `station-factory-3km`, `missile-near-own-ship-40m`,
`own-kill-chase` (the target ahead of the own ship in chase view), `nested-area-kill` (three fighters in one blast),
`eye-inside-capital`, `seta6`. The death-tier select the lab has (five tiers, rows by class) plays the vanilla beside
ours. Controls: per tier f / clamp / life / embers / secondaries, octaves, the fireball peak, shock width and speed,
`smoke`, the ramp stops, SETA.

**Fixture gates** (`EXPLOSION_*`): `death_tier_sizes_per_class` (per tier R_f on screen within 5 % of the law at five
ages, the clamps); `death_fireball_cools_through_ramp` (mean hue at 0.2 / 0.6 / 1.0 life) and
`death_fireball_fades_to_zero_at_life`; the shock radius against age; `death_secondaries_inside_hull_box`; the drift
law (the centroid at 0.5 s within 0.25 R_f of the damped path); a hull in front hiding the fireball and the soft cut
through a hull plane; no pop at birth (the flash under 0.25 H); the three-frame floor and the SETA 6 wall-time law; the
fireball's energy through the production `TemporalPass` ≥ 0.9; `death_exposure_meter_shift` (≤ 0.5 EV under a
full-screen fireball through the production meter, else the cap above); `death_octave_lod_above_20pct`;
`death_budget_capital_close_5120` (≤ 450 µs fenced, with the LOD ≤ 250); `death_eye_inside_bounded_by_budget`; nested
deaths each drawn (hook: three records in one kill); `death_missile_area_forwarded` (the rule-(b) object gets no
explosion record, only the `area` event of 3.5, and forwards byte-exact); the asteroid forwarded byte-exact; timings at 1080p and 5120x1440; Reset, fault, refusal, slots.

**Flight.** `weapon_effects = shields,hull,explosions`: kills of an M5, an M6, a TS and a capital (a scripted or
watched one), one missile shot down, an asteroid (native). The user must see a fireball that cools into dark red over
1.6 to 4 s of game time, sized to the ship, no native explosion sprite, each nested kill with its own fireball. The
observe rows' reason-word histogram from this flight says which deaths the user meets most (missile area kills,
collisions) for the tuning that follows.

### 3.3 Bolts, their trails, and the muzzle (step 4)

The look, sizes, anatomy and laws of the bolts and the muzzle are ratified in
[bolt-muzzle-look.md](bolt-muzzle-look.md) (2026-11-07), which amends this section; its numbers, gates and kill
criteria stand where they differ from the sketch below. The hook sites, the in-place late-bullet draw, the records and
the modes of this section stand. Implementation state: [weapon-effects.md](../verification/weapon-effects.md), "Bolts
and muzzle (step 4)".

**Records.** The per-bullet tap at the sector update's call site **`0x0045ac7d`** (`E8` → `0x00414590`, one argument,
EBX/EBP/EDI live, no register inputs, return dead; a plain write in the install window [M, spawn note 8.2]): the stub
records class-0 objects (row `+0x4a`, node `+0x30`, rotation `+0x40`, `ext+0x78`, creation `ext+8`) into a per-frame
bolt list (512 entries, generation-stamped per Present) and **always forwards**. It replaces the architecture's two
candidates (the flight site `0x004146c0`, reached only for bullets with a col 19 effect, 3 of 45 stock and 5 of 38 Mayhem
[M]; and instance matching at the draw). The flight effect (col 19: stock 170, 172; Mayhem 308–310, five of the user's
weapons) is a sprite on the bullet and would double our capsule, so it is skipped at `0x004146c0` for armed rows. The
trail (col 23, a Particles3 id: one stock row, FLAK_CLUSTER; Mayhem none [M, spawn §7]) stays the engine's: `0x004146fa`
is not claimed (one weapon, not the user's mod; a proxy ribbon is not worth a site). The muzzle site `0x00446268` is
claimed as the architecture says (EAX/EDX saved).

**Look** (weapon tint; w = 0.5 m x (1 + 2 k)):

| Element | Shape | Size | Life / motion |
| --- | --- | --- | --- |
| Core | white capsule, E 4 (≥ 1.2 under the streak's energy law) | width w (≥ 2 px), rest length: orb 1 w, slug 3 w, tracer 10 w (≥ 12 px); wave (`0x8`) a flat crescent across the travel, width col 18, depth 0.5 w | the streak rule from the bullet's previous screen position |
| Body / halo | tint at 0.3 to 1.5 linear, Gaussian σ 1.5 w | lit footprint ≥ 3 px | with the core |
| Tail (medium and heavy) | straight tapered extension behind, tint x 0.5, linear to 0 at its end | min(v x 40 ms x k, 30 w) | analytic (bolts fly straight); no history |
| Birth | grows from the muzzle over the first 30 ms game | — | — |
| End | lifetime expiry: fades over 60 ms; a hit: ends with the object (the hit families answer) | — | — |

Muzzle: a disc core + tinted halo of 2.5 w and a forward cone flame of 4 w along the gun axis (the 3 px / 12 px
floors), 120 ms ± 20 % (vanilla muzzle elements run 150 ms at the median stock, 200 ms on Mayhem [M, vanilla_timings];
Mayhem fires 3 to 90 shots a minute, so a shorter flash would go unseen), the three-frame floor, no lens flare (13 of 21
stock muzzle rows carried one [M]); its light is a glow of R = 6 w at E 0.8 tint on the shooter's own hull, 120 ms;
own cockpit gain 0.35 (question 6). Every shot re-lights the same (ship, slot) record: the shield cells' clock rule,
reset, never add. A live beam's refresh does not respawn the muzzle [I, spawn §2]; ours lights once per fire tick that
reaches the site. Station and platform guns do not fire through `0x00445170` [M, spawn Unknown]: they get bolts from
the tap and no muzzle flash until their fire routine is traced (unknown 13).

**Data and overrides.** Shape, tint and calibre from the classifier (architecture 4.1 to 4.4) and the generated table
(elongation, texture mean); speed for the tail from the record (col 11). Overrides pin `style` where the user prefers it
(the Mayhem IRE back to `tracer`, architecture 4.4) and a per-id `muzzle_gain` (a weapon whose vanilla muzzle is a long
plume, PPC row 52 at 925 ms [M], may want 1.5), never a size. The quad size cols 16/17 [U unit] stay unused until the
observe rows (`bolt_size`) pin their unit (unknown 12).

**Drawing.** At the frame's late bullet draw (the single-copy rule's survivor), our capsules from the tap list, frustum
culled on the CPU, replace the game's vertices: our VB, VS (view-projection from the shadowed c0–c3 [S]) and PS (oC0, oC1
= oC0, oC2 = K x max(rgb), the B' flag), the route's ONE/ONE and depth state, restored after; every other bullet draw of
the frame is suppressed at the draw. A frame without a late bullet draw draws no bolts (the engine saw none visible).
Whether the tap's list can replace the draw's instances one for one (the engine's own visibility cull against our
frustum cull) is the step-4 fixture's question (unknown 14); if the counts disagree, the draw keeps only the tap records
whose subtype and nearest position match one of its instances.
The muzzle flash and its glow are stage sprites. `--bolt-footprint`'s floors become the bolt law's header floors; the
footprint path stays only for the native mode.

**Placement and occlusion.** World positions from the tap, never predicted; muzzle in the shooter's frame; occlusion
class 3 for bolts, class 2 for the muzzle flash.

**Cost.** Tap: about 1,000 objects a pass at a class compare each plus 512 records ≈ 10 µs [I]; VB build 512 x 4 x 32 B
= 64 KB ≈ 20 µs [I]; GPU < 0.05 ms; it removes today's footprint vertex rewrite and its staged copies of the game's
2.2 MB bullet buffer (bolt-footprint "Staged Lock" [M]), so a net CPU saving is expected.

**Kill criterion.** A bolt invisible from the chase camera at a range where the native was visible → the width floor
3 → 4 px and the core x 1.5, once. Bolts vanish over a far station (the flag path broken), smear or leave a trail, or
under SETA read as sticks → simple mode: no tail, streak cap 2 rest lengths. The own bolts in the cockpit read as
"walls" → the cockpit view keeps the native draw (`bolt::cockpit_native`, the footprint already gates by view [M]).
Still wrong → the family off (native bullet draw with the footprint). Muzzle: blinking lights rather than fire → the
flame length x 2 and the life 180 ms, once; strobing or filling the cockpit view → half size without its glow.

**Lab presets** (`bolt/…`, `muzzle/…`): `ire-chase-ahead-600m` (Mayhem IRE, a slug), `ire-mayhem-slug-vs-tracer` (the
override A/B), `pac-cockpit-own` (stock tracer), `ppc-slug-capital-broadside` (slug + tail), `psp-orb`,
`psg-wave-200m` (stock), `own-fire-chase` (own guns from the chase camera, bolts flying away), `incoming-cockpit`
(enemy bolts end-on), `over-station-hull-20km` (the composite case), `seta6-stream` (10 shots per game second at
1,000 m/s); `muzzle/ire-own-chase` (the Run239 camera, two wing guns at 4 shots/s), `muzzle/pac-own-cockpit`,
`muzzle/ppc-capital-broadside-600m`, `muzzle/flak-alpha-turret`, `muzzle/mayhem-white-light-texture-tint`,
`muzzle/seta6-10hz`. The lab's projectile slot (body, `travel`, `tint` controls) is the vanilla side. Controls: per
style w, rest length, tail, peak and halo, the px floors, the muzzle size / flame / life / glow, the cockpit gain, SETA.

**Fixture gates** (`BOLT_*`, `MUZZLE_*`): the hook fixture for the tap (`bolt_tap_record_per_live_bullet`: every
class-0 object once per pass, other classes forwarded, EBX/EBP/EDI and the stack as left, the straddled 5 bytes
written in the install window, the generation stamp) and the col 19 skip; `bolt_cpu_200_bullets` (≤ 100 µs, tap and
VB); the in-place draw: one draw a frame, every other bullet draw suppressed, the state restored,
`bolt_vb_bytes_and_instances` (the VB count equals the tap's culled list), `RT2.g > 1` on streak pixels only (the
`seam-bolt-far-flag` shape), `bolt_far_hull_composite_flagged` (through the production resolve and write-back on the
native host, the bolt over a far-band hull keeps ≥ 0.9 of its scene value; the flag was built under Wine and is not yet
verified natively); `bolt_capsule_width_floor_3px` and `bolt_length_floor_12px` at 1080 and 1440 lines per view,
`bolt_streak_covers_previous_position` (0 dark pixels between the two positions at 4, 8 and 32 px per frame),
`bolt_style_shapes` (tracer / slug / orb / wave extents), the end-on bolt a disc, `bolt_hard_end_at_hull_depth`, the
SETA streak cap and energy law, `bolt_resolve_core_survival` (≥ 0.95 at 0 / 4 / 8 px), no scar after 8 frames,
`bolt_cockpit_view_gate`; `muzzle_at_recorded_gun_pos` (centroid within 0.25 of the muzzle radius on a moving and
rotating ship), `muzzle_streak_along_orientation` (≤ 2°), `muzzle_light_on_own_hull_not_sky`,
`muzzle_relight_resets_not_adds` (two shots 30 ms apart: peak = single), `muzzle_min_frames_seta6`,
`muzzle_cockpit_gain`, `muzzle_budget_20_guns_5120` (≤ 30 µs); hook record `muzzle_stub_forwards_eax_edx` (EAX and
EDX reach the callee, EBX/EBP/EDI kept, the qword claim atomic), `muzzle_skips_native_when_staged`,
`muzzle_record_per_shot_per_slot`; Reset, fault; timings.

**Flight.** `… ,bolts`: chase and cockpit views, own fire and incoming, a burst over a distant station, SETA. The user
must see every bolt as a bright capsule in its weapon's colour, readable at range, in front of distant stations, no
native bolt sprite or lens flare.

### 3.4 Beams, sustained and pulsed (step 5)

**Records.** No new site: beam objects (flags `0x2` / `0x4`) come through the tap each pass with their start (node
`+0x30`, the engine's muzzle tracking), direction (node `+0x40` z row) and length (`ext+0x78`: gun-tied
`0x00444bd0(obj)/2` per fire tick, kind B col 18 [M, §1]); the shooter and slot from its muzzle record.

**Envelope from the data, not a class.** Rise 30 ms, hold while the beam object lives, fall 80 ms after it dies. Whether a
beam reads sustained or pulsed follows from lifetime against refire [M, vanilla_timings]: stock PAL 2,490 / 420 ms,
FUSIONBEAM, TRIBEAM, REPAIR, TUG and the Kyon alpha overlap (sustained); KH_BETA, KH_GAMMA, IOND and stock PBC (550 / 600)
pulse; the user's Mayhem PBC (900 / 2,222) and RL (1,000 / 3,000) pulse.

**Look** (weapon tint; w_b = 0.3 m x (1 + 2 k)): a white core line (E 4, ≥ 2 px) inside a tinted sheath of 4 w_b
(Gaussian across, 0.6 to 1.2 linear) modulated by 1D noise scrolling along the beam at 150 m/s game time with a period
of 25 w_b (± 25 %); a start flare = the muzzle primitive at 1.5 x, held while firing; a rounded cap at the contact. The
hit is drawn by the shield and hull families from the beam's own impact records (every contact tick is an impact), so
there is no separate end flare. Repair: soft sheath, no noise; tug: faint pale blue sheath, no core (overrides).

**Placement and occlusion.** Every frame from the beam object; never extrapolated; the ribbon's ends within 0.25 m of
the tap's position and position + length x heading. **Slot.** The beam's own draw is unidentified: the 72-vertex part
batch holds the PlasmaBeam and Repair bodies [M, bolt-footprint Run 73 B], so beams are probably in the bullet draw
[I] (unknown 3). Step 4's observe rows settle it (`beam_draw`: the draw whose instance extent tracks `ext+0x78`, its
VS/PS pair and primitive count). Identified in the bullet draw → class 3 in place, as the bolts (the same 12-vertex
segment layout, so it is a slot choice, not a redesign). Not identified → the stage ribbon with class 2 (SOFT 0.15 core,
1.0 sheath) and the draw suppressed only once identified, with the measured trigger below.

**Cost.** ≤ 16 live beams x ~50k px x 60 slots < 0.01 ms [I]; CPU ~2 µs per beam.

**Kill criterion.** A beam dimmer over a hull than over space in the same frame (the hold averaging a stage ribbon)
→ the ribbon moves to the in-place slot. The noise crawls or flickers through the TAA, or the beam reads as a solid
tube → simple mode: a static sheath at half its gain. Still wrong → native (the beam draw not suppressed; the hit
families still answer its contacts).

**Lab presets** (`beam/…`): `pbc-mayhem-pulsed`, `pbc-own-cockpit`, `pal-capital-800m-chase` (sustained),
`kyon-khaak-dogfight`, `iond-kind-b`, `repair-corvette-200m`, `tug-ts`, `over-station-hull-10km`, `end-on-cockpit`,
`seta6`. Controls: core and sheath widths and gains, noise speed and period, rise / fall, the repair and tug overrides.

**Fixture gates**: `beam_ribbon_from_gun_to_end` (both ends within 0.25 m), `beam_length_follows_ext_0x78` (two frames,
two lengths), the envelope against the object's life (rise, hold, fall), the start tracking a moving and rotating
shooter, `beam_core_floor_3px`, `beam_noise_scrolls` (phase at 0 / 100 ms game), `beam_repair_no_core_above_e`,
`beam_resolve_core_survival` (≥ 0.95), `beam_held_over_far_hull_measured` (recorded, gated only after the slot
decision), the oC2 flag when in place, `beam_budget_16_5120`; hook record `beam_tap_length_read`,
`beam_draw_suppressed_only_when_identified`. **Flight.** PBC and the repair laser (Mayhem); PAL and Kyon on stock if
the user flies it; a beam over a capital's hull against one over space.

### 3.5 Missiles: impacts and area deaths (step 6); the trail is an engine-effects change

**Trail: not a weapon family.** Missile glow jets already draw as our plumes (the draw path has no class gate), while
the emitter sprite and the Particles3 trail forward for class 10 at redirects A `0x004147eb` and B `0x0041482c`, and
the plumes note promises missiles plumes "like ships" in its phase 2 [S, engine-effects-modern 1]. The redirects, the
JET records and the ribbon builder (accepted for ships, Run 172 "trails attached to the plume end") belong to that
feature, its `engine_effects` option and its ledger [engine-effects.md](../verification/engine-effects.md), so the trail
is armed and flown there, not in `weapon_effects`: skip A and B for class 10, feed the missile's JET records to the
ribbon builder. The look this note hands to it, in the language above: the plume the missile already has, plus an
ember core on the ribbon (heat T 0.9 → 0.3 over 0.4 s, the plume's width) and a faint warm ribbon in the missile tint
(`missile_tint_rgb` (255, 170, 90) [S]; weapon-owned), 1.5 s game, widening x 3 over its life; additive only, no grey
smoke (question 5). Kill: the trail reads as a laser line or too long → ember only, 0.4 s; then plume only. Presets
(engine-effects lab and `missile/trail-chase`, `missile/trail-incoming` here); gates: the class-10 skips exact with
class 7 untouched, the ribbon on the plume end, the trail life in game time.

**Impacts.** Shield-absorbed missiles keep the shield patch. Hull: the hull hit's `missile_hit` variant (3.1) plus a
burst (3.6 law) of r_b = U x (1 + k) at the contact, so a missile on a hull reads as a small explosion on it. Missile
death (`tier = missile`, shot down or expired): the explosion law at the missile row of 3.2 (a missile killed by flak
near the own ship is the case the user meets most). Area missiles (rule (b) of the architecture 2.4: the engine spawns
nothing and runs the area damage) record an `area` event and still forward byte-exact; the stage draws a burst at the
missile's position, sized by the area radius once traced (unknown 4) and until then the missile row of 3.2; the
victims' impacts draw their own hits. The missile extension (owner, launcher) is not needed.

**Kill criterion.** The hit burst reads as too big or doubles the hull hit → hull hit only (`missile::hit_burst` off).
**Lab presets** (`missile/…`): `hull-hit-corvette`, `shield-hit-capital`, `shot-down-near-own-40m`, `area-swarm`.
**Fixture gates**: the burst at the contact on the owner's rows, the area event recorded and forwarded byte-exact, the
missile tier sizes, the three-frame floor. **Flight.** Missiles fired at and by the own ship, one area missile.

### 3.6 Flak and area bursts (step 7)

**Records.** The death site for class 0 with flag `0x100` (`tier = burst`, calibre from the record); fragments (`0x1000`)
are bolts (step 4) whose own deaths are bursts, so a cluster flak shell plays shell → fragments → bursts from the
engine's own chain. Stock and FL only: Mayhem 3 has no area or fragment bullets [M, spawn note §7].

**Look.** The explosion law with burst parameters: flash, fireball (no secondaries), shock shell with the weapon tint
on its rim (weapon-owned), 8 + 8 k embers, glow R = 4 r_b; r_b = the area damage radius once traced (unknown 4), until
then 6 m x (1 + 5 k); L = 0.5 + 0.7 k s. PSP takes the same law at k = 1.

**Placement.** The death record's world position (area bullets have their speed zeroed [M, §4]). **Kill criterion.** A
flak barrage exceeds the budget or reads as fireworks → simple mode: flash and shock only. **Lab presets** (`burst/…`):
`flak-alpha`, `flak-cluster-chain`, `fbl`, `mam`, `psp`, `barrage-12` (12 bursts per game second). **Fixture gates**:
the chain timing from the fragment rows, sizes, the budget under the barrage. **Flight.** Stock or FL, a capital's flak
at fighters.

## 4. Order of flights and dependencies

| Step | Family | Needs | Adds for later steps | RE or fixture before the brief |
| --- | --- | --- | --- | --- |
| 1 | shield | — | stage, ring, bubble cache, budget | done (Run239 F) |
| 2 | hull hit | 1 | heat ramp, sprite batch, closed-form particles, glow, streak rule, the shared budget weights | the previous view-projection inside the stage callback (unknown 9) |
| 3 | explosions | 2 | fireball, shock shell, death hooks | `node+0x100` unit (observe row, unknown 1) |
| 4 | bolts and muzzle | 2 (glow, sprites) | tap, in-place capsule draw | tap passes per Present under SETA (unknown 2) |
| 5 | beams | 4 | — | beam bodies in the bullet draw (unknown 3) |
| 6 | missile impacts and area deaths | 2, 3 | area event | area radius (unknown 4) |
| 7 | flak bursts | 3, 4 | — | area radius (unknown 4) |
| (engine effects) | missile trails | the plumes' phase 2, the ribbon builder | — | the class-10 redirect skips (engine-effects fixture) |
| FL | all accepted | an FL note of the sites (architecture 2.10) | — | one `disassemble` task |

Each step flies alone in `observe` first only where it claims a new site (3, 4); steps 2, 5, 6 and 7 add no site and
fly their look directly. The observe flight of each step logs the rows the next one needs (`weapon_death` class / row /
radius / `bubble_cached`, `bolt_size`, `beam_draw`, `bolt_tap passes`). Bolts and muzzle share flight 4 because they arm
at one site pair and neither reads without the other; if the flight rejects one, its kill criterion runs alone. Bursts
come last because the user's Mayhem 3 has none; beams follow bolts because they share the tap and the in-place draw;
the missile trail flies whenever the plumes' phase 2 is scheduled, independent of steps 2 to 7.

## 5. Header constants

All in `src/proxy/weapon_effects_core.h`, injected into the lab by the generator, re-pinned by the fixture; none is a
setting.

| Namespace | Constants |
| --- | --- |
| `look` | `heat_ramp[5]`, `min_frames` 3, `soft_core` 0.15, `soft_halo` 1.0, `core_e` 4, `body_max` 1.5, `halo_max` 0.2, `streak_cap` 6, `streak_core_min` 1.2, `floor_core_px` 2, `floor_lit_px` 3, `floor_len_px` 24 (12 until 2026-11-06; the hull sparks its only reader), `floor_flash_px` 3, `budget_weight[family]` |
| `hull` | `mode`, `r_base` 0.35, `r_k` 0.65, `r_scale` 2, `flash_ms` 60, `spark_n0` 8, `spark_nk` 16, `spark_speed_u` 6, `spark_tau_s` 0.45 (isotropic), `spark_life_ms` 250 + 250 k, `spark_width_u` 0.08, `spark_e` 8, `glow_r` 3, `glow_ms` 150, `ember_r` 0.6, `ember_ms` 1200, `bias` 0.5, `asteroid_dust_x` 2, `missile_spark_x` 2 |
| `boom` | `mode`, per tier `f`, `clamp`, `life_s`, `embers`, `secondaries`; `flash_ms` 80, `grow` 0.15, `shock_r` 4, `shock_life` 0.3, `glow_r` 5, `drift_tau_s` 0.4, `lod_coverage` 0.2, `peak_cap` 2 (above 0.5 EV), `smoke` false, `station_secondaries` 12, `tier_native` mask, `asteroid_native` true |
| `bolt` | `mode`, `w0_m` 0.5, `w_k` 2, rest lengths 1 / 3 / 10, `halo_sigma` 1.5, `tail_ms` 40, `tail_max_w` 30, `birth_ms` 30, `end_ms` 60, `cockpit_native` false |
| `muzzle` | `mode`, `r_w` 2.5, `cone_w` 4, `life_ms` 120, `glow_r_w` 6, `glow_ms` 120 |
| `beam` | `mode`, `w0_m` 0.3, `sheath_w` 4, `scroll_mps` 150, `period_w` 25, `rise_ms` 30, `fall_ms` 80 |
| `missile` | `mode`, `hit_burst` true, `hit_burst_u` 1 (the trail's `ember_ms` 400, `ribbon_ms` 1500, `ribbon_widen` 3 live in the engine-effects header) |
| `burst` | `mode`, `r0_m` 6, `r_k` 5, `life_s` 0.5 + 0.7 k, `embers` 8 + 8 k |
| shared (existing) | `cell_m`, `own_cockpit_gain`, `stagger`, `screen_budget`, `soft`, `weapon_tint` |

## 6. The lab

One page, `weapon_effects_lab.html`, as today: each family fills `renderOurs(record, t)` for its slot (muzzle,
projectile, impact, death) beside the vanilla it already plays, with the same law as the HLSL. Additions:

- a **preset** select listing the presets of section 3 (`<family>/<case>`), each setting view (stock / Mayhem 3 / FL),
  weapon id, victim tier and hull, camera, SETA rate, hits per game second, own ship and cockpit, defined in
  `generate_weapon_effects_lab.py` (`PRESETS`); `--check` fails when a preset names an id missing from its view or a
  family has no preset;
- a **camera** select: side, chase-nose (the F3 camera), cockpit, far 2 km, far 8 km (the floors);
- per family a fieldset of its header constants (section 5) with "reset to header", as the shield's;
- the 5120x1440 pixel floors emulated at the canvas's scale, so a far preset shows the floored footprint.

The user tunes in the lab, the agreed values go into the header, the fixture re-pins them, then the flight.

## 7. Unknowns and what settles each

| # | Unknown | Settles it |
| --- | --- | --- |
| 1 | The unit of the node velocity `+0x100` (death drift, a dying ship's momentum) | Step 3's observe flight logs it beside the node `+0x30` delta per game step |
| 2 | Whether the sector update runs once per bullet per Present under SETA | Step 4's observe row `bolt_tap passes=` per Present; the generation stamp keeps the newest either way |
| 3 | Whether every beam body is drawn by the late bullet draw | One F8 capture with a PBC firing (the bolt draw census already logs the 72-vertex batch) |
| 4 | The area damage radius in `0x0045eeb0` (bursts, area missiles) | One `disassemble` task; until then the calibre law |
| 5 | Whether an asteroid's cut-scene fragments are the new asteroid objects | Disassembly of the class-17 kill path; only matters if the user wants an asteroid look later |
| 6 | The missile extension (owner) | Not needed by any look here |
| 7 | The exposure meter's response to a frame-filling fireball (dropped note unknown 8) | Step 3 fixture, then its flight |
| 8 | Native GPU cost of the fireball, glow and capsule programs | Each step's native fixture (the shield figures are native already) |
| 9 | The previous view-projection inside the stage callback (the streak rule) | Step 2 fixture; the motes and `camera_reprojection.h` have the camera delta [S] |
| 10 | Architecture 4.4 listed speeds at col 11 / 1000 (IRE 581) where the catalogue and the header use 500 units per metre (IRE 1,162) | Corrected in place 2026-11-06 (4.4 and the FL PD speed in 2.10, every value x 2, from the catalogue) |
| 11 | Architecture 4.3 counted TM as a fighter; the header's `patch_tier_of_ship` (2141) makes it a corvette | Corrected in place 2026-11-06; every family uses the header's function |
| 12 | The unit of the quad size cols 16/17 against the drawn width | Step 4's observe `bolt_size` rows (col 16 against the instance extents the footprint path derives); until then `w0` stands alone |
| 13 | Stations' and turret platforms' fire routine (class 5/6 do not fire through `0x00445170`) | Step 4's observe: muzzle records with a class-5/6 shooter, or none; if none, station guns keep bolts without a muzzle flash until the routine is traced |
| 14 | Whether the tap's list replaces the bullet draw's instances one for one (the engine's visibility cull against ours) | The step-4 fixture against a recorded `INSTANCE_BULLETS` draw and a synthetic object list; else subtype + nearest-position filtering |
| 15 | The far-composite flag on native Windows (built under Wine, 2026-09-26) | `bolt_far_hull_composite_flagged` through the production resolve on the native host |
| 16 | Whether a dying owner is usually in the bubble cache (secondaries) | `weapon_death bubble_cached=`; below 0.5 the secondaries take the sphere |
| 17 | The R32F lane form (untested since step 1) | Step 2's first fixture runs one case with `four_channel` false |

## 8. Options considered and why they lose

- **Weapon-tinted hits (the vanilla way).** The shield decision already moved hit colour to the target's state, and on
  Mayhem 3 the tint is a texture average for 36 of 38 rows; two colour meanings on one hull would not read. Kept as the
  `weapon_tint` override per family.
- **A hull-damage colour ramp mirroring the shield's** (hull hits turning red as the hull drops). Needs a hull-fraction
  read nobody has traced, and gives the heat colour a second meaning. A later option, like patterns per race.
- **CPU-integrated particles** (`pos += v · step_game`, the architecture's 5.2). Integration drift under SETA, a pool to
  age, state to clear at Reset and scope changes; the closed form is exact, stateless and cheaper. The closed form wins.
- **Hit lights through the engine-light material route** (hull program twins). Exact N·L and albedo, but per-draw light
  lists for other ships' hulls, 104 twins to extend and a register budget already spent on 72 plates; the screen-space
  glow is one lane fetch on any surface, including stations and asteroids. Loses.
- **Bolts in the stage** with a second, flag-only draw into `RT2.g`. The stage samples the lane, so it cannot write it in
  the same draw, and a flag draw without occlusion would flag hidden bolts. Drawing in place of the late bullet draw
  keeps every accepted bolt rule for free. Loses.
- **Bolts matched from the `INSTANCE_BULLETS` draw by predicted position** (the architecture's fallback). Ambiguous in
  dense fire, no velocity at birth, 20 µs of matching; the tap gives position, row and creation time per object. Loses.
- **Raymarched volumetric fireballs or flipbook explosion textures.** Raymarching costs several times the impostor at
  frame-filling sizes; flipbooks need image assets (the rule is analytic geometry over engine data). Lose.
- **Grey smoke now** (a premultiplied, non-additive layer). Ordering with the additive layers, ghosting through the TAA
  and its own budget; the families read without it. Deferred, not rejected.
- **A stage look for asteroids.** The outer call takes the rock cut scene with the dust; the rocks are geometry the
  stage cannot draw. Native wins.
- **Explosions staying where the ship died** (vanilla `NODRIFT`) or **carried at full velocity** (the architecture's
  "velocity x age"). The first stops a 300 m/s fighter dead; the second carries a 4 s capital fireball hundreds of metres.
  The damped drift is one constant between them.
- **Lens flares on muzzles and explosions.** The user cut excess glow from the plumes twice; the core and a tight halo
  carry the brightness. Lose.
- **Distance dimming of weapon effects** (the plumes' far-disc law). Bolts were rejected for being invisible, not too
  bright; weapon effects keep floors instead.
- **The birth frame only** (this note's first draft: draw once at age 0). A single bright frame enters the resolve at
  0.37 to 0.66 and is gone, so at SETA 6 a 60 ms flash would barely register; the parallel note's three-frame floor
  keeps it visible and changes nothing that lives longer. The floor wins (2.4).
- **Spark cones about a hull-box normal** (this note's first draft: the gradient of the bubble ellipsoid at the hit).
  The parallel note's argument is stronger: any estimated normal is a cousin of the centre projection and the travel
  rule the user rejected, and on a wing or a hull corner the box gradient is wrong; isotropic sparks with the lane hiding
  the inward half always read as a surface spray and need neither the cache nor a gate on a normal. Isotropic wins (3.1).
- **A lane-fitted camera-facing quad as the one light** (the parallel note: `exp(−|z_lane − z_P| / (SOFT R)) (1 −
  d²/R²)²` with d in the quad's plane). Same cost (one lane fetch, no normal), but its reach is a cylinder along the
  view ray with an exponential depth tail: a hull seen edge-on is lit far along the line of sight, a thin wing passes
  the light through, and with the eye inside R (the own ship's hits in the cockpit) the camera-facing quad is clipped by
  the near plane. The 3D sphere through the decal's own point reconstruction (built and measured for the station decal)
  is view-independent and back-face boxes cover the eye inside. The sphere wins (2.8); the quad's first frames as the
  flash are kept as an idea, below.
- **No flash on hull hits, the light's first frames being the flash** (the parallel note). One clock is attractive, but a
  light needs a lit surface in view: a hit at range covers a few pixels of hull, and a nose hit from behind has none.
  The 60 ms, 3 px-floored flash core is what reads at range; at 60 ms it is not the hard flash 5.1.1 removed (that was
  0.1 s over the patch). The flash stays; the kill criterion halves it if the user calls it a flash bulb.
- **Hull-hit sparks and light in the weapon tint** (the parallel note). The weapon tint would put a texture average
  (Mayhem's white lights) on burning metal and give a hull hit a colour the shield ramp does not explain; heat is one
  meaning. Left to the user as question 2, recommending heat.
- **The native impact sprite under our sparks as a fallback** (the parallel note's hull kill). The coordinator's rule is
  native or stage, never both; the fallback is the simple mode, then native.
- **Asteroid deaths as our dust puff and rock streaks** (the parallel note). It would suppress the cut scene of real rock
  geometry (row 59, 13 s) that the stage cannot draw. Native wins (3.2).
- **No burst for an area missile, as the engine** (the parallel note). The engine spawns nothing there and leaves the
  look to the victims' impacts; with no victim in range the missile simply vanishes, which reads as a bug. Recording an
  `area` event and forwarding byte-exact changes no engine state. Ours wins (3.5).
- **Missile trails as a weapon family** (this note's first draft: step 6 skipping redirects A/B). The redirects, the
  JET records and the ribbons belong to the engine-effects feature, its option and its ledger; arming them from
  `weapon_effects` would split one mechanism across two features. The parallel note's ownership wins; this note keeps
  only the look it hands over (3.5).
- **Skipping the col 23 Particles3 trail** (this note's first draft). One stock weapon, none on Mayhem, a site to claim
  for nothing the user flies. Forwarding wins.
- **Shorter explosions** (the parallel note: 0.9 s fighter to 3 s station). Without smoke the fireball is the whole
  event; vanilla runs 3.1 s for fighters and 9 to 22 s for capitals, so 1.6 to 6 s keeps the event's weight. The lab
  decides the final numbers.
- **A shock shell on capitals and stations only** (the parallel note). One law on every tier costs one cheap draw and
  keeps a fighter's death legible at range; a fighter's shell is small.
- **IOND and the Kyon beams as tracer-law bolts** (the parallel note). The ratified classifier makes flags `0x2`/`0x4`
  beams (4.1); the beam envelope already reads them as pulsed from their lifetime against refire, so no rule changes.
- **A beam width pulse (± 10 % at 8 Hz)** (the parallel note). A periodic width change on a long thin bright line is the
  crawl the TAA shows first; the scrolling noise alone carries the motion.
- **A 50 ms muzzle flash** (this note's first draft). Vanilla muzzles run 150 to 200 ms at the median and Mayhem fires
  3 to 90 shots a minute; 120 ms (the parallel note's figure) is seen.
- **Dimming the own bolts in the cockpit** (this note's first draft scoped the gain to everything the own ship fired).
  The gain exists for things attached to the hull about the eye; bolts leave it and need their visibility. Scoped to the
  hull (2.9).
- **Per-family user settings.** The user configures nothing; header constants tuned in the lab.
- **Several families per flight.** The user's rule is one family per flown step; each family's token arms it alone.

## 9. Choices only the user can make

Recorded here so a later session does not reopen them; the report carries the same wording.

1. **Bolts in the cockpit view**: the new look in both views (recommended; the kill criterion returns the cockpit to
   the native draw if the own bolts read as walls), or the native draw in the cockpit and the new look in chase only.
2. **Colour of hull hits**: heat colours, white to orange to dark red, whatever the weapon (recommended: matches the
   shield decision; Mayhem's white lights make the weapon tint a texture average), or the weapon's tint on the glow and
   the sparks, as vanilla does.
3. **Asteroid deaths**: the vanilla rock breakup, untouched (recommended: it is real rock geometry the stage cannot
   draw), or our dust puff and rock streaks, losing the vanilla fragments.
4. **A dying ship's fireball**: carried briefly by the ship's momentum, then stopping (recommended), staying where the
   ship died (vanilla), or carried at full speed.
5. **Smoke** after explosions and on missile trails: off in the first flights, added after flight 3 only if the user
   misses the lingering half (recommended), or in from the start at more cost and some TAA ghosting.
6. **The own ship's muzzle flashes in the cockpit view**: dimmed to 0.35 like the shield patch (recommended), or full.
7. **Flight order**: hull hit, explosions, bolts with muzzle in one flight, beams, missile impacts, flak bursts last
   (Mayhem has none); the missile trail whenever the plumes' phase 2 is flown; FL after AP, one hook-verification flight
   for all accepted families (recommended), or one FL flight per family.

## Reproduce

```sh
python3 -I verification/results/weapon-look/vanilla_timings.py > verification/results/weapon-look/vanilla_timings_out.txt
# per step, once built (the lock serialises the native fixture queue):
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_weapon_effects.py --host native
python3 tools/effects/generate_weapon_effects_lab.py --check
```
