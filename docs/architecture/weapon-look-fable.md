# Weapon look: one visual language for every weapon-effect family

Design note, 2026-11-06 (Fable 5.1, design agent). Not ratified; nothing here is built. It fixes the look of every
family the proxy will draw through its D3D9 proxy for X3: Albion Prelude (FL later), so that no further design
round is needed per family: hull impacts (step 2, next), bolts and their trails, muzzle flashes, beams, flak and
area bursts, missile impacts and deaths, and the death tiers (fighter / corvette / capital, stations, asteroids).
Shield hits are done and accepted (Run239 Flight F) and set the vocabulary; this note stays consistent with them.

Inputs: [weapon-effects.md](weapon-effects.md) (the ratified hooks, `Event`, classifier, steps; 5.1.1 the accepted
shield look), [weapon-effects.md ledger](../verification/weapon-effects.md) (what the six placement and look rounds
tried and what the user rejected), [weapon-effect-spawn.md](../reverse-engineering/weapon-effect-spawn.md) (what each
site provides), [weapon-catalog.md](../reference/weapon-catalog.md) and `tools/effects/weapon_catalog/*.json`, the
census `verification/results/weapon-effects-census/stock_ap.json` (Effects rows with element kinds and durations),
`tools/effects/weapon_effects_lab.html`, `src/proxy/weapon_effects_core.h` (the step-1 constants), the dropped
[effects-modernisation-opus.md](effects-modernisation-opus.md) sections 2.2–2.5 and 3.1–3.5 (the stage model and
per-class sketches the ratified note keeps), and the engine-effect work the user accepted:
[engine-effects-modern.md](engine-effects-modern.md), [engine-exhaust-look-critique.md](engine-exhaust-look-critique.md),
[engine-light.md](engine-light.md), [bolts-through-taa.md](bolts-through-taa.md), [bolt-footprint.md](bolt-footprint.md)
and the [engine-effects ledger](../verification/engine-effects.md).

Marks: **[M]** measured (a cited ledger row, listing or census number), **[I]** inferred, **[S]** static (code or
archive read), **[U]** unknown. Costs are for 5120x1440 unless a line says otherwise. Metres: 500 engine units = 1 m
[M, game-scale §1].

## Outcome

One language, six rules, seven families, five flights. Every family is **analytic geometry drawn by our shaders over
data the engine already records at the hook site** (positions, rows, the live TBullets / TMissiles record, the victim's
class and radius), coloured from **three colour sources that never mix inside a family** (the weapon tint for anything
the weapon itself is made of, the shield-state ramp for the shield patch, the fire ramp for anything that burns), sized
by **one tier table** (fighter / corvette / capital / station / asteroid, the shield cells' 3 / 6 / 16 m as the unit)
times **one calibre factor** (1 + k, k the classifier's 0..1), aged in **game time with a three-frame floor**, drawn
**additively in FP16 with a core above the stabiliser bound and a halo gained for the resolve**, occluded **per pixel
by the depth lane** (stage families) or **by the hardware depth test** (the two in-scene families: bolts and beams, which
keep the engine's bullet-draw slot and its far-hull composite flag). Placement is always the recorded engine position
carried on the owner's live rows; nothing is projected through a centre or chosen from the bolt's travel (both failed,
ledger "Placement"). Each family ships with header constants (no user setting), named lab presets, fixture gates for
placement, size, no-pop, SETA and cost, and a kill criterion that drops it to its simpler mode by one header constant.

| Family | Slot | Colour source | Size driver | Flight | Depends on |
| --- | --- | --- | --- | --- | --- |
| Shield hit (done) | stage | shield-state ramp | victim tier | Run239 accepted | — |
| Hull hit (+ repair, mining, missile hit) | stage | weapon tint | victim tier × calibre | 2 | step 1's impact site |
| Explosions: ship tiers, station, asteroid, missile; flak / area burst | stage | fire ramp (burst: weapon tint) | victim radius by tier; burst: calibre | 3 | hull hit's light and spark primitives |
| Bolts (tracer / slug / orb / wave / pulsed beam) | in-scene (bullet draw) | weapon tint | style × calibre, 3 px / 12 px floors | 4 | the flight tap, the substitute VB |
| Muzzle flash | stage | weapon tint | calibre | 4 (same flight) | the muzzle site |
| Beams (sustained) + beam end | stage ribbon, end = hull hit | weapon tint | calibre | 5 | muzzle record (shooter, slot), hull hit |
| Missile trails | engine-effects plumes, not a weapon family | race tint | nozzle | separate (plumes ledger) | class-10 forwarding today |

Hot path, whole language [I unless marked]: hooks stay as step 1 (one 64 B ring write per event, 17.3 µs median for
50 events [M]); the stage shares one 8-screen area budget with the shield patch (today 8 × 177.5 µs = 1.42 ms worst
[M]) and the families below add at most 2 screens of boxes in a normal fight (hit lights and sparks are small; one
close fireball is the exception and is budgeted as 1–3 screens); the in-scene bolt slot costs CPU only
(≤ 0.1 ms per firing frame, the footprint's 60–100 µs today [M]). Native Windows: documented D3D9 only, the stage
and hook fixtures already run natively on the RTX 5070 Ti host [M, ledger header]; the in-scene bolt slot's
far-composite flag was built under Wine (2026-09-26) and is not verified native [U].

## 1. The language (six rules every family obeys)

### 1.1 Colour: three sources, never mixed within a family

| Source | Used by | Definition |
| --- | --- | --- |
| **Weapon tint** `tint_rgb` | bolts, muzzle, beams, hull-hit sparks and light, bursts, pulsed beams | the classifier's tint (4.2 of the ratified note: the light colour when saturated, else the body texture mean, else white; missiles `missile_tint_rgb` (255, 170, 90)). Core = white-hot above the bound, halo = the tint |
| **Shield-state ramp** | shield patch only | `colour_full` / `colour_half` / `colour_empty` at ≥ 75 % / 50 % / ≤ 35 % (5.1.1, accepted) |
| **Fire ramp** | explosions (all tiers), station and asteroid deaths, the hull hit's cooling spark tail | `fire_white` (1.0, 0.95, 0.85) → `fire_orange` (1.0, 0.45, 0.10) → `fire_red` (0.40, 0.05, 0.02) → `fire_smoke` (0.08, 0.08, 0.08, non-additive); position on the ramp = age / lifetime per element |

Why three and not one: the shield hex told the user something true (shield state) and the HUD agreed; a hull hit or
explosion carrying the shield ramp would lie. Weapon tint on the projectile family keeps mod weapons recognisable
(Mayhem's white lights fall to the texture mean, 36 of 38 rows [M]). Asteroids burn nothing: their death uses the
ramp's last two stops only (dark red to dust).

### 1.2 Size: one tier table, one calibre factor

Tier as step 1 resolves it [S, `core::tier_*`]: ships by TShips class id (fighter M5/M4/M3/TM/drones, corvette
M6/M8/TS/TP, capital M1/M2/M7/TL/M0), stations (class 5/6) `station`, asteroids (17) `asteroid`, missiles (10)
`missile`, else radius bands (< 30 m fighter, < 150 m corvette, else capital; `tier_corvette_min_u` 15000,
`tier_capital_min_u` 75000 [S]).

| Tier | unit `u` (m) | source | note |
| --- | --- | --- | --- |
| fighter | 3 | `cell_m[0]` | the accepted shield cell |
| corvette | 6 | `cell_m[1]` | |
| capital | 16 | `cell_m[2]` | |
| station | 16 | `cell_m[2]` | stations keep the capital cell (accepted) |
| asteroid | by radius band | | mining hits: radius band → 3 / 6 / 16 |
| missile | 3 | | a missile is fighter-sized |

Calibre factor `c = 1 + k`, k = `clamp(log10(d / 300) / 2, 0, 1)` from the record (4.2) [S]: IRE 1.0, HEPT 1.41,
CIG 1.71, PPC 2.0. Everything the weapon is made of (bolt width, muzzle radius, spark count and reach, hit-light
radius, burst radius) scales by `c`; everything the victim is made of (fireball, shockwave, debris) scales by the
victim's radius clamped per tier. The two never multiply except in the hull hit's light radius `u · c` (a PPC on a
fighter and an IRE on a capital both read).

### 1.3 Time: game time, three-frame floor, SETA

`age = now_game − t0_game_ms` (`cfg+0x718`, the accepted rule). New rule, from the SETA lesson: **no element lives
fewer than `min_frames` (3) frames**: its lifetime is `max(lifetime_ms, min_frames × step_game)` with `step_game`
the frame's game step (`cfg+0x714` [M, engine-effects.md §8]). At SETA 6 a 60 ms flash is 10 ms of wall time, under
one frame; the floor keeps it on screen for three frames and lets everything longer shorten with SETA as the engine's
own effects do. Every fade is a per-element clock as the shield cells' (a re-hit resets, never adds).

### 1.4 Blend, bound, halo

ONE/ONE on the FP16 scene inside the TAA resolve's step-0 bracket, alpha never written, the non-additive smoke layer
(if enabled) premultiplied and drawn first [S, dropped 2.3; the shield pass as built]. Every family's **core sits above
the stabiliser bound E 1** (kept at 1.00 through the resolve at 0 / 4 / 8 px per frame [M]) and every **halo below it is
gained** for the 0.37–0.66 it keeps [M]. The capped peaks: `core_peak` 6 for bolts, muzzle and sparks, 3 for hit
lights, 4 for fireballs (the exposure meter reads the resolved image; a full-screen fireball at 4 is the fixture's
exposure case, 2.6). Nothing depends on bloom settings; bloom and AgX add the bleed.

### 1.5 Placement and occlusion

**Placement.** Only recorded engine positions, carried on live rows read at scene end (impact: owner-local `pos` +
the owner's `node+0x30/0x40`; muzzle: ship-local `pos` + `&orientation`; death: world `pos` + velocity × age; bolts:
the bullet's own node each frame). Never a projection through the owner's centre (Run235 B: 0.15–0.85 R off [M]) and
never a face or direction chosen from the bolt's travel (Run236 C: on edge from above [M]). Where a family needs a
surface normal it does not have one; the rules below are written so none is needed (isotropic sparks, lane-fitted
lights, bubble-mapped decals).

**Occlusion.** Stage families: the lane, `vis = saturate((d_lane − z) / (SOFT · r))`, the sky never hides, an
invalid texel hides [S, the plumes' and the bubble's rule]; SOFT per element: 0.15 for cores, 1.0 for halos and
lights (the plumes' pair [M]). The two in-scene families (bolts, beams' bolt-slot variant) use the hardware depth test
of the engine's bullet draw (the footprint path [M]) and have no soft end: the hull hit covers the end. The own ship
in the cockpit view keeps `own_cockpit_gain` 0.35 for anything attached to the own hull (the patch, hull hits,
muzzle); explosions and bolts are not dimmed.

### 1.6 Thin bright moving things through the TAA

The bolt-footprint lessons [M]: a moving core narrower than 3 render px or shorter than 12 px along its motion is
averaged to ≈ 0.1 by the 3x3 clamp; a 3 px-wide core whose streak covers its previous position keeps 1.00. So every
streak (bolt, spark, beam, debris) is `max(world width, 3 px)` wide and `max(world length, 12 px, |v| · Δt_game)`
long, built in the vertex program from the projected extents (the footprint's rule moved into the VS, dropped 3.1).
Over a far or thin station hull the resolve's hold averages even a 3 px core away (keep 0.99 [M, bolts-through-taa])
unless the pixel is flagged for the post-resolve composite; **only the in-scene bullet-draw slot can write that flag**
(RT2.g under a GREEN mask while the lane is not sampled [S]). That is why bolts and beams stay in-scene (2.3, 2.5),
and why the stage families accept the hold over a far hull (a hit light or a spark over a station 60 km away is not
what the user judges; the kill criteria say what is).

### 1.7 One light primitive

The **hit light**: a camera-facing quad of radius R at point P whose pixel program adds
`L · tint · exp(−|z_lane − z_P| / (SOFT · R)) · (1 − d² / R²)²` to pixels that have lane depth near P (dropped 3.4,
no normal needed) [S]. It is the only light any family draws: hull hit (R = u · c), muzzle (R = 2 r_m on the
shooter), burst (R = 2 r_b), explosion (R = 2 R_fire, flash). The engine's `EEDF_LIGHT` nodes go with the skipped
spawn (16 of 26 stock impact rows, 13 of 21 muzzle rows carry one or a flare [M]); the hull-shader engine-light term
(72 plates, per-draw constant upload [M, engine-light.md]) is not reused (section 4).

## 2. Families

Each family: look, engine data and override pins, placement and occlusion, cost and kill criterion, lab presets,
fixture gates. Constants are header values (`weapon_effects_core.h`, one namespace per family); the lab's controls
default to them and `--check` pins them (the step-1 contract [S]).

### 2.1 Hull hit (step 2, next)

**Look.** No sprite, no hard flash. Two primitives: **sparks** and the **hit light**.

- *Sparks.* `n = 12 + 12 k` (IRE 12, PPC 24) CPU particles born at the recorded hit point, **isotropic** initial
  velocity `v = owner velocity + random direction × (8 + 16 k) m/s`, drag `exp(−age / 200 ms)`, life 250 + 250 k ms
  game (floor 1.3), each a streak from `p` to `p − v · Δt_game` with the 3 px / 12 px floors, width 0.15 m · c
  (3 px floor), core white above E cooling through the **fire ramp** by `age / life` (white → orange → dark red; the
  spark is the one hull-hit element on the fire ramp: it is hull metal, not weapon energy). The half that flies into
  the hull is hidden by the lane: an isotropic spray always reads as a surface spray without a normal.
- *Hit light.* R = u · c (fighter IRE 3 m, capital PPC 32 m), peak L 3 falling as `(1 − age / 150 ms)²` (floor 1.3),
  weapon tint, SOFT 1.0: the first frames are the flash the user sees from behind on a nose hit; nothing else flashes.
- *Variants by override only:* `repair` (flag 0x80 rows: REPAIR, Mayhem RL): no sparks, the light in the override's
  tint (green) at half peak for 100 ms, re-lit per tick; `mining` (MINING, MDS, by id): sparks ×2 on the ramp's last
  stops (rock dust, grey-red), light as hull; `missile_hit` (class-10 source): sparks ×2, light R ×2, missile tint.
- *Drained shield.* Stays a hull hit (the accepted rule): the HUD at 0 % agrees with sparks.
- Sound untouched: the impact sound is played by the caller after the effect call (`0x00417250`) [M, spawn §3/§6].

**Engine data.** The impact record as built: `pos` (owner frame), `tier`, `calibre` (the running total `[esp+0x1c]`,
4.2), `tint_rgb`, `table/row`, `radius`, `absorbed`, `detail`. The owner's velocity is the node's frame delta of
`node+0x30` read at scene end (two frames of rows, the bubble cache's key already tracks the owner). The override
pins `style` (`repair`, `mining`), `tint`, `calibre`.

**Placement, occlusion.** The recorded `pos` on the owner's rows; sparks are world-space after birth (they leave the
hull). Lane occlusion for both. A hull hit on an owner without live rows draws nothing (the retired rule).

**Cost.** Sparks: ≤ 64 live hits × 24 = 1,536 particles, one dynamic VB write (≤ 74 KB), one draw, ~15 µs CPU
integration per frame [I]; GPU negligible (streaks of a few hundred px). Hit light: one quad per live hit, each
`(2R)²` projected; a capital PPC hit at 100 m is ~0.02 screen [I]; budgeted inside the shared 8 screens. Pixel
programs ≤ 60 slots each [I]. Target: a 20-hit frame ≤ 0.05 ms GPU, ≤ 30 µs CPU.

**Kill criterion.** The user cannot see a nose hit on the own ship from the chase camera at 24 m (the Flight F
reference case) → `hull::light_peak` ×2 and `hull::light_r_mult` ×2 once; if still unseen, the family keeps sparks
and forwards the native sprite (`hull::native_sprite = true`, the step-1 shape of suppression). Sparks that read as
"confetti" (the user's words decide) → `hull::spark_count` halves, then sparks off, light alone.

**Lab presets** (`c_hull_*` controls, preset ids): `hull_fighter_ire_chase_nose` (the Run239 reference camera: 4 × 1.6
× 12 m hull 24 m ahead, pitched 0.175 rad), `hull_fighter_ire_cockpit_own` (own ship, cockpit, eight hits/s),
`hull_capital_ppc_flank_300m`, `hull_station_ppc_1km`, `hull_repair_corvette`, `hull_mining_asteroid_60m`,
`hull_missile_hit_corvette`, `hull_seta6_12hps`. Controls: spark count / speed / life / width, light peak / radius /
duration, the three ramp stops, the `min_frames` floor, SETA.

**Fixture gates** (D3D record, the step-1 naming): `hull_sparks_born_at_recorded_pos` (the sparks' first-frame
centroid within 0.25 u of P on the hull lane), `hull_half_of_sparks_hidden_by_flat_lane` (0.45–0.55 of the streak
pixels survive on a flat hull lane facing the eye), `hull_light_on_hull_not_sky` (0 lit sky pixels, ≥ 0.9 of the
disc on the lane), `hull_light_peak_decay_150ms`, `hull_spark_cooling_ramp` (hue at 0 / 0.5 / 1 life), `hull_streak_min_footprint_3px_12px`,
`hull_resolve_core_survival` (≥ 0.95 at 0 / 4 / 8 px per frame), `hull_min_frames_under_seta6` (a 60 ms element
drawn on ≥ 3 frames at SETA 6), `hull_no_pop_12hps` (per-pixel frame drop ≤ one fade step), `hull_repair_no_sparks`,
`hull_missile_tier`, `hull_cockpit_dim_own_ship`, `hull_budget_20_hits_5120` (≤ 50 µs fenced), `hull_cpu_64_hits`
(≤ 30 µs), reset / fault / program refusal as step 1. Hook record: `hull_hit_skips_native_when_staged`,
`drained_forwards_as_hull` stays, `velocity_from_frame_delta`.

### 2.2 Explosions (step 3): ship tiers, station, asteroid, missile, and the flak / area burst

**Look, shared.** A **fireball** (camera-facing noise-sphere impostor: radial density × 3 octaves of volume noise
advected by age, the fire ramp by `age / life` from white through orange to dark red, soft lane fade, dropped 3.5),
a **flash** (the hit light at R = 2 R_fire, peak 4, 120 ms), **sparks** (the hull-hit particle at ×4..×8 count,
isotropic, life ×2) and, for capital and station, a **shockwave** (one thin additive ring expanding to 3 R_fire over
400 ms, width 0.1 R, peak 2) and **secondary bursts** (3–6 fighter-tier fireballs at random points inside the dying
hull's box, 150–900 ms after t0, each 600 ms). **Smoke** (premultiplied billboards, drawn first) is a header switch
`death::smoke`, **off in the first flight**: a fireball that cools to dark red and goes is the simpler mode; smoke
ghosts a little over flickering sky [M, motes] and is added only if the user asks for lingering debris.

| Tier | R_fire = clamp(f × radius, lo, hi) | life (ms game) | elements |
| --- | --- | --- | --- |
| fighter | 1.5 × r, 8–40 m | 900 | fireball, flash, sparks 48 |
| corvette | 1.2 × r, 40–160 m | 1,500 | fireball, flash, sparks 96, 2 secondaries |
| capital | 0.8 × r, 160–800 m | 2,500 | fireball, flash, shockwave, sparks 160, 6 secondaries over 1.2 s, debris streaks 24 (fire ramp, drag 0) |
| station | 0.4 × r, 200–1,500 m | 3,000 | as capital; secondaries spread over 2 s (a station radius is a loose sphere up to 52 km [M, game-scale §6], hence the 0.4 and the cap) |
| asteroid | 0.6 × r, 10–800 m | 1,200 | no fireball: a dust puff (ramp's last two stops, premultiplied if smoke is on, else a dim additive dark-red sphere at 0.5) + rock streaks 32 (grey, drag 0) |
| missile | 6–12 m (r × 1.5) | 600 | fireball, flash, sparks 24 |
| burst (the bullet's own area death, flag 0x100 / 0x1000) | r_b = (2 + 6 k) m | 400 | a core flash (weapon tint, peak 6, 2 frames floor), 8 + 8 k sparks in the **weapon tint** (not the ramp: weapon energy, not burning metal), a thin ring to 2 r_b over 300 ms, the hit light at 2 r_b |

The vanilla rows for scale: fighter row 35 is a 3.1 s sprite, capital row 37 a 3.8 s sprite with 7–10 s smoke and
a 30-prop `PROPAGATE` carrier, row 40 (M6/M8) 8–9 s [M, census]. Ours are shorter and hotter; the smoke switch
restores the lingering half if the user misses it.

**Engine data.** The death record (2.4 of the ratified note): the dying object's class and subtype (tier by class id
with col 47 as cross-check and `tier_mismatch` logged), `radius` (`obj+0xa4`), world `pos` and velocity (the node's
`+0x30`, `+0x100`), `t0`, the reason word; for a burst the TBullets record (calibre, tint). For capital and station the
handler takes the bubble cache's hull box if the owner is cached (`stage::BubbleCache`, no walk at the death site; a
miss uses the sphere): the secondaries' positions. Nested kills (an area missile's blast) each carry their own record;
the ring tolerates them (2.7). An area missile (`obj+0x40 & 0x1000000` with a killer) gets no explosion from the
engine and none from us [M, spawn §4]. Sound: the death sound follows the callee at the caller [M, spawn §4].
Override pins: a class's tier (`by_id` on the ship id is not available — the death record has no `SS_SHIP` name; the
pin is by TShips row index in `by_table`), `death::smoke`, a burst's radius.

**Placement, occlusion.** World position + velocity × age; the node is not read again (it dies). Lane occlusion
with SOFT 1.0 on the fireball (a fireball half inside a station fades at the hull), 0.15 on sparks and the ring.
The eye inside a fireball: the impostor's box contains the eye and rasterises the screen; the shared budget bounds it
(the oldest owners' draws drop first), and the fireball's density is evaluated along the ray from the eye, so the
view fills with the ramp's colour at that age (acceptable: the user is dying or ramming).

**Cost.** A fighter at 500 m ~0.02 screen × 2 layers × 300 slots → 0.01 ms [I]; a capital at close range 0.5
screen × 3 layers → 0.2–0.45 ms for 1–2 s, 0.22 ms with the octave LOD above 20 % coverage [I, dropped 3.5]. Budget:
one close capital or station death may take up to 3 of the 8 screens; the fixture measures it fenced. Sparks and
streaks share the hull-hit pool (2,048 particles, the dropped figure). Exposure (unknown 8): the fixture measures the
meter's EV shift under a full-screen fireball at peak 4; if above 0.5 EV the fireball's peak is capped at 2 and the
flash carries the brightness (a small area).

**Kill criterion.** The fireball reads as "a balloon" or pumps the exposure in flight → `death::octaves` 2 and
`death::peak` 2; a tier whose size the user calls wrong moves one tier's (f, lo, hi) only; a station death that covers
the view for more than a second → `death::station_secondaries` 0. Falling back: the family forwards the native death
spawn (`death::native = true`) and keeps only the burst tier (the smallest, least contentious).

**Lab presets.** `death_m5_300m_chase`, `death_m6_600m`, `death_m2_1500m_broadside`, `death_station_factory_3km`,
`death_asteroid_mining_80m`, `death_missile_near_own_ship_40m`, `death_burst_flak_alpha_200m`, `death_burst_psp`,
`death_nested_missile_area_kill` (three fighters in one blast), `death_eye_inside_capital`. Controls: per tier f / lo
/ hi / life / spark count / secondaries, octaves, peak, shockwave width and speed, smoke on/off, the ramp stops, SETA.

**Fixture gates.** `death_tier_sizes_per_class` (R_fire per tier from radius, the clamps), `death_pos_follows_velocity`
(centroid at age 0.5 s within 0.25 R of `pos + v · age`), `death_fireball_cools_through_ramp` (mean hue at 0.2 / 0.6 /
1.0 life), `death_fireball_fades_to_zero_at_life`, `death_secondaries_inside_hull_box`, `death_shockwave_radius_vs_age`,
`death_asteroid_no_fireball`, `death_burst_weapon_tint_not_ramp`, `death_missile_area_no_explosion` (hook record: the
class-10 flagged object records nothing), `death_nested_three_records_one_kill` (hook), `death_exposure_meter_shift`
(≤ 0.5 EV under a full-screen fireball through the production meter), `death_octave_lod_above_20pct`,
`death_budget_capital_close_5120` (≤ 450 µs fenced, the LOD ≤ 250), `death_eye_inside_bounded_by_budget`,
`death_resolve_core_survival`, `death_seta6_age_law`, reset / fault.

### 2.3 Bolts (step 4): tracer, slug, orb, wave, pulsed beam; and trails

**Look.** An HDR **capsule**: a white-hot core (peak 6) of width `w = w0[style] · c` with the 3 px floor, a halo
3 × w at 0.25 in the weapon tint, a tail: length `max(L0, 12 px, |v| · Δt_game · 1.0)` where `L0 = 0.1 × speed`
in metres is the engine's own rest length [I, spawn §1 col 11] — the streak always covers the previous position
(rule 1.6). Styles (the classifier's, 4.1): **tracer** `w0` 0.5 m, tail 1.0 × L0; **slug** `w0` 1.2 m, tail 0.5 × L0,
a brighter rounder head; **orb** `w0` 2.0 m, no tail, a soft sphere with a 2 × w halo; **wave** (flag 0x8, PSG / PD /
the frag) a camera-facing soft disc of radius `col 18 / 2` with a brighter rim ring (the rim above E, the disc at 0.5:
a sheet, not a capsule); **pulsed beam** (flag 0x4 kind B: IOND) a tracer whose length is `col 18` and whose width is
0.3 m · c. `+burst` changes nothing in flight (the burst is the death record, 2.2). The core never shrinks below the
footprint; a bolt ending in a hull ends hard (hardware depth), the hull hit covers it. Lens flares: none. Chase and
cockpit views both get the look (the dropped open question): the user judges per view and the kill criterion below
restores the first-person native draw if the own bolts read wrong in the cockpit.

**Trails** (TBullets col 23, a Particles3 id: one stock row, FLAK_CLUSTER; Mayhem none [M, spawn §7]) stay the
engine's: the trail call `0x004146fa` is forwarded, nothing drawn by us. A proxy ribbon trail is not a family (one
weapon, and the plumes' ribbons exist if ever wanted).

**Engine data.** The **flight tap `0x0045ac7d`** sees every live bullet once per update (`[esp+4]` = obj, filter
`word [obj+0x48] == 0`) [M, spawn §8.2]: per bullet the node's `+0x30` position, `+0x40` rows (row 2 = heading),
`+0x100` velocity, `obj+0x4a` subtype → the live TBullets record (style, calibre, tint, speed, col 16/17 size, col 18
length), `ext+0` expiry, `ext+4` owner. That is the per-bolt per-frame record with **no matching at all**; the
muzzle record is not needed to draw a bolt. The engine's bullet draw (`INSTANCE_BULLETS`, the two draws per firing
frame of 792–840 primitives [M, bolt-footprint §1]) is recorded and suppressed as the footprint path does today: the
substitute VB at stream 0 carries our capsule quads instead of the game's crossed cards, 12 vertices per bolt (the
dropped 8.3), under our vs_3_0 / ps_3_0 pair with the flag variant (`oC2` GREEN, K 32) so the far-hull composite keeps
a bolt visible over a station [M, bolts-through-taa]. The quad size cols 16/17 **[U unit]** scale `w0` once the
observe rows (`bolt_size`) pin the unit; until then `w0` stands alone. Override pins: `style` (Mayhem IRE back to a
tracer if the flight prefers it), `tint`, `w0`, `calibre`.

**Placement, occlusion.** The bullet's own node position each frame (the tap's record, matched to the draw's
instance by subtype + nearest position, or the draw replaced entirely by the tap list when the counts agree: the
fixture decides which, 3.1 of the ratified note's open item). Hardware depth of the bullet draw; no lane, no soft end.

**Cost.** CPU: the tap stub ~0.3 µs per live bullet (reads and a ring write; 200 bullets 60 µs [I]) plus the VB
write (12 × 24 B per bolt, ≤ 1024 bolts = 295 KB, one DISCARD lock, the footprint's 60–100 µs replaced [M]); GPU:
60 bolts × ~2,000 px × 45 slots → < 0.01 ms [I]. Target ≤ 0.1 ms CPU on a firing frame, no stage work.

**Kill criterion.** A bolt invisible from the chase camera at any range where the native was visible → `bolt::w_min_px`
3 → 4 and `bolt::core_peak` ×1.5; still invisible over stations → the flag path is broken: forward the native draw
(`bolt::native_draw = true`, the footprint rule stays). The own bolts in the cockpit read as "walls" → the cockpit
view keeps the native draw per view (`bolt::cockpit_native = true`), as the footprint already gates by view [M].

**Lab presets.** `bolt_ire_chase_ahead_600m`, `bolt_pac_cockpit_own`, `bolt_ppc_slug_capital_broadside`,
`bolt_psg_wave_200m`, `bolt_iond_pulsed_beam`, `bolt_mayhem_ire_slug_vs_tracer`, `bolt_over_station_hull_20km`
(the composite case), `bolt_seta6_stream` (10 shots/s at 1,000 m/s). Controls: per style w0 / tail / peak / halo,
the px floors, k_stretch, tint source, SETA.

**Fixture gates.** `bolt_capsule_width_floor_3px` and `bolt_length_floor_12px` at 1080 and 1440 lines,
`bolt_streak_covers_previous_position` (0 dark pixels between the two positions at 4 and 8 px per frame),
`bolt_resolve_core_survival` (≥ 0.95 over space), `bolt_far_hull_composite_flagged` (through the production resolve
and write-back: the bolt over a far-band hull keeps ≥ 0.9 of its scene value), `bolt_style_shapes` (tracer / slug /
orb / wave / pulsed extents), `bolt_hard_end_at_hull_depth`, `bolt_vb_bytes_and_instances` (12 vertices per bolt, the
count equals the tap's live list), `bolt_tap_record_per_live_bullet` (hook record: every class-0 object once per
pass, other classes forwarded, EBX/EBP/EDI preserved, the straddled 5 bytes written in the install window),
`bolt_cpu_200_bullets` (≤ 100 µs), `bolt_cockpit_view_gate`, reset / fault.

### 2.4 Muzzle flash (step 4, same flight as bolts)

**Look.** A **radial glow** at the gun: a camera-facing quad of radius `r_m = (1 + 2 k) m` (IRE 1 m, PPC 3 m) with
a core (peak 6, white) and a tint halo, plus a **forward streak** 3 r_m long along the gun axis (the orientation the
site hands over) with the 3 px / 12 px floors, and the **hit light** on the shooter's own hull at R = 2 r_m (the
light of a firing gun on the own ship's nose in chase view is the point of the family). Lifetime 120 ms game, floor
3 frames, fade `(1 − age / life)²`. No lens flare (13 of 21 stock rows carry one; none drawn). Every shot re-arms the
same instance natively; ours re-lights the same record (the shield cells' clock rule: reset, never add). Beams do
not respawn the muzzle on refresh [I, spawn §2]; ours lights once per fire tick that reaches the site.

**Engine data.** The muzzle record: ship, gun slot k, `&pos` (ship frame), EAX = TBullets index, EDX = `&orientation`
(12 dwords) [M, spawn §6]. The record carries `shooter` (the ship), `slot`, `pos`, `axis` (row 2 of the orientation),
style and calibre from the record. Override pins: `tint`, `r_m`, a per-id `muzzle_gain` (a weapon whose native muzzle
is a long 925 ms plume, PPC row 52 [M], may want 1.5).

**Placement, occlusion.** `pos` on the shooter's live rows at scene end (the own ship: static on screen). Lane
occlusion SOFT 0.15 on the core (a gun under the hull's silhouette hides), 1.0 on the halo; the cockpit's own guns are
ahead of the eye and are not dimmed unless the kill criterion says so.

**Cost.** ≤ 64 live muzzle records, one quad + one streak + one light each; a capital broadside 20 guns ~0.05 screen
[I]; ≤ 0.02 ms GPU, CPU ~5 µs. The muzzle stub: 12 reads and a ring write, the fire routine's EH region untouched
(no unwind path).

**Kill criterion.** The own ship's flashes flood the cockpit → `muzzle::own_cockpit_gain` 0.35 (the patch's number).
Flashes read as "blinking lights" rather than fire → the streak length ×2 and life 180 ms once; then the family forwards
the native muzzle (`muzzle::native = true`) and keeps the hit light only.

**Lab presets.** `muzzle_ire_own_chase` (the Run239 camera, two wing guns at 4 shots/s), `muzzle_pac_own_cockpit`,
`muzzle_ppc_capital_broadside_600m`, `muzzle_flak_alpha_turret`, `muzzle_mayhem_white_light_texture_tint`,
`muzzle_seta6_10hps`. Controls: r_m law, streak length, peak, life, hull light radius / peak, cockpit gain, SETA.

**Fixture gates.** `muzzle_at_recorded_gun_pos` (centroid within 0.25 r_m of the gun on the shooter's rows),
`muzzle_streak_along_orientation` (axis angle ≤ 2°), `muzzle_light_on_own_hull_not_sky`, `muzzle_relight_resets_not_adds`
(two shots 30 ms apart: peak = single), `muzzle_min_frames_seta6`, `muzzle_cockpit_gain`, `muzzle_budget_20_guns_5120`
(≤ 30 µs), hook record: `muzzle_stub_forwards_eax_edx` (EAX index and EDX pointer reach the callee, EBX/EBP/EDI kept,
the qword claim atomic), `muzzle_skips_native_when_staged`, `muzzle_record_per_shot_per_slot`.

### 2.5 Beams (step 5): sustained, and the beam end

**Look.** A camera-facing **ribbon** from the gun to the beam's end: a core 0.3 m · c wide (3 px floor, peak 6,
white) inside a **sheath** 3 × wide at 0.3 in the weapon tint, **scrolling noise** along the length (one octave,
phase = age × 4 lengths/s, the plumes' flow vocabulary), width pulsing ± 10 % at 8 Hz game time, a soft **start
glow** = the muzzle flash held while the beam lives (2.4, re-lit per fire tick), and the **beam end** = the hull-hit
family at the impact record (2.1: sparks at 10/s while the beam holds, the hit light held at peak, in the weapon tint)
— a beam on a shield gets the shield patch (done) at its end. Sustained = flag 0x2 (gun-tied: PAL, PBC, TRIBEAM,
FUSIONBEAM, KH_*, REPAIR, TUG, DUMMY2; Mayhem PBC, RL [M]); the 0x2000 "continuous" bolts (PPC, IONC, IBL, PSP, the
flak) are bolts (4.1), pulsed kind-B beams are bolts (2.3). Overrides: REPAIR / RL green soft sheath at 0.5, no core
above E, no sparks (the `repair` hull variant); TUG a blue-white sheath, no end effect.

**Engine data.** The flight tap record of the beam's bullet each frame: node position (the muzzle offset
`ext+0x10..` keeps it at the gun), row 2 heading, `ext+0x78` the length (gun-tied `0x00444bd0(obj)/2` refreshed per
tick, or col 18) [M, spawn §1]; the muzzle record gives shooter and slot; the impact record (the end) arrives at the
impact site as any hit. The beam's own draw (its body pair **[U]**) is recognised at draw time only to suppress it: the
flight-tap list says which bullets are beams, and the observe flight logs the draw that moves with them (`beam_draw`
rows: VS/PS pair, primitive count, instance extent vs `ext+0x78`).

**Placement, occlusion.** The ribbon lives in the **stage** (lane occlusion, SOFT 0.15 core, 1.0 sheath): a beam is
wide and bright, and over a far station hull the resolve's hold is the accepted risk (rule 1.6) with the kill
criterion as the check. If Flight 5 shows a PAL on a capital dimmed to a thread, the ribbon moves to the in-scene slot
(the beam draw's substitute VB, hardware depth, the flag) as the bolts: the vertex layout is the same 12 vertices per
segment, so the move is a slot change, not a redesign.

**Cost.** ≤ 16 live beams × ~50k px × 60 slots → < 0.01 ms [I]; the end effects are hull hits; CPU ~2 µs per beam.

**Kill criterion.** A beam dimmer over a hull than over space in the same frame → the slot move above. A beam that
reads as a "solid tube" → `beam::sheath_gain` 0.3 → 0.15 and noise ×2; then native (`beam::native = true`): the beam
draw is not suppressed and only the end effect stays.

**Lab presets.** `beam_pal_capital_800m_chase`, `beam_pbc_own_cockpit`, `beam_khaak_alpha_fighter_dogfight`,
`beam_repair_corvette_200m`, `beam_tug_ts`, `beam_over_station_hull_10km`, `beam_seta6`. Controls: core / sheath
widths and gains, noise speed and octave, pulse, end spark rate, the repair / tug overrides.

**Fixture gates.** `beam_ribbon_from_gun_to_end` (both ends within 0.25 m of the tap's position and position + length ×
heading), `beam_length_follows_ext_0x78` (two frames, two lengths), `beam_core_floor_3px`, `beam_noise_scrolls`
(phase at 0 / 100 ms), `beam_end_sparks_rate`, `beam_repair_no_core_above_e`, `beam_resolve_core_survival`,
`beam_held_over_far_hull_measured` (the figure recorded, gated only after the slot decision), `beam_budget_16_5120`,
hook record: `beam_tap_length_read`, `beam_draw_suppressed_only_when_identified`.

### 2.6 Missiles

Not a family of its own; three existing families with the `missile` tier and tint:

- **Trail and engine glow**: engine-effects, class 10 forwarded at both redirect sites today and promised plumes
  "like ships" in the plumes' phase 2 [S, engine-effects-modern.md]. A separate flight under the engine-effects ledger
  (the plumes' look with the missile's own body tint; ribbon on). Nothing in this note.
- **Impact** (class-10 source at the impact site, TMissiles col 20 row): the hull hit's `missile_hit` variant (2.1) or
  the shield patch (done) at the missile tint. The missile extension layout (owner, launcher) is **[U]** and not needed.
- **Death** (class 10 at the death site, TMissiles col 21; area missiles with `0x1000000` + killer have no explosion):
  the `missile` tier of 2.2. A missile killed by flak near the own ship is the case the user sees most: fireball 6–12 m,
  600 ms, sparks 24.

### 2.7 Shield hit (done; the vocabulary)

Unchanged, for reference: the hex patch on the ship bubble / station hull decal, cells 3 / 6 / 16 m, rings 1 / 2 / 2,
the shield-state ramp, the marker cell at 2 ×, ring over 0.3 s, fade 2.2 s ± 20 % per cell, game time, drained below
0.5 % as a hull hit, own-ship cockpit gain 0.35, the shared 8-screen budget, 64 draws, the lane occlusion with sky
never hiding [M, 5.1.1 and ledger]. What the other families inherit from it: the tier cells, the clock-reset rule, the
ramp as a colour source, the budget, the cockpit gain, the bubble cache (secondaries, 2.2), the gate names.

## 3. Flights: order and dependencies

One family per flight, each armed by its own `family_*` bit so a later step cannot regress an accepted one (the
ratified rule). Before each flight: the lab presets of the family reviewed by the user, the D3D and hook records
PASS natively, `--check` on lab and programs, the AP DLL clean and no-x87.

| Flight | Family | Mode | What the user must see | Depends on |
| --- | --- | --- | --- | --- |
| 2 | hull hit (+ repair, mining, missile hit, drained) | `hull` | sparks and a brief glow on an unshielded hull, visible on the own nose from behind; the patch unchanged on shields; the repair laser without sparks | step 1 as flown |
| 3 | explosions (ship tiers, station, asteroid, missile, burst) | `deaths` | an M5, an M6 and a freighter kill, one asteroid, one missile, flak bursts; fireballs cooling over 1–2.5 s, no native sprite, nested kills each with a fireball | the hit-light and spark primitives of flight 2; the death stubs |
| 4 | bolts + muzzle | `bolts` | own bolts readable ahead in chase and cockpit, over a station too; muzzle glow on the own nose; Mayhem tints | the flight tap, the substitute VB with the flag variant, the muzzle stub |
| 5 | beams | `beams` | PAL / PBC on a capital, the Khaak beams, repair and tug; a beam not dimmed over a hull | the muzzle record, the beam draw identified in flight 4's observe rows, the hull-hit end |
| 6 (optional) | missile trails | engine-effects `plumes` | a missile's plume and ribbon | the plumes' phase 2, outside this note |

Flight 4 carries two families because neither reads without the other (a flash with native bolts, or bolts from a
native flash, is what the user already has) and both arm at the same site pair; if the flight rejects one, its kill
criterion runs alone. Flights 2 and 3 are independent of 4 and 5; 5 needs 4. The observe mode of each step logs the
rows the next step needs (`bolt_size`, `beam_draw`, `weapon_death` class / row / radius triples).

## 4. Options considered and why they lose

- **One colour source (the weapon tint everywhere, or the shield ramp on hull hits too).** The shield ramp on a hull
  lies against the HUD (the 0 % hex "reads as a bug" [M, Flight D]); weapon tint on a burning hull reads as a
  coloured balloon. Three sources with a fixed mapping lose nothing and never contradict the picture.
- **Physically placed sparks (a cone about the surface normal).** The record has no normal; estimating one from the
  centre (failed for placement) or the bolt's travel (failed on shallow hits) brings the two rejected rules back
  through a side door. Isotropic sparks hidden by the lane need no normal and always read as a surface spray.
- **A flash sprite on hull hits.** The user removed the shield patch's hard flash; one primitive (the light's first
  frames) does the job and has one clock.
- **Fireball size from the Effects row tier (35 / 37 / 40).** Per mod and inconsistent in stock (M6 split 27 / 2
  [M]); the class id with the row as cross-check was ratified; this note only adds the radius clamp per tier.
- **Smoke in the first explosion flight.** It is the ghosting-prone layer [M, motes] and the only non-additive one;
  the fireball alone is the simpler mode, and the switch exists.
- **Bolts in the stage (lane-occluded, soft ends).** The stage cannot write the far-hull composite flag while it
  samples the lane, so bolts over stations fall back to keep 0.99 of history [M, bolts-through-taa]; the in-scene slot
  already has the flag, the hardware depth and the substitute VB. Soft ends are not missed: the hull hit covers them.
- **Bolts by matching draw instances to muzzle records** (the ratified fallback). The flight tap sees every live
  bullet with its subtype and node, so there is nothing to match; the fallback stays only if the tap's claim fails
  the hook fixture (a straddled 5-byte write in the install window).
- **Beams in the in-scene slot from the start.** Their draw is unidentified [U]; the stage draws them from the tap
  alone and the slot move is held in reserve with a measured trigger.
- **Lens flares on muzzles and impacts.** Screen-space sprites the engine draws for 13 of 21 muzzle rows [M];
  they are the look the user replaced; the analytic halo covers the glow.
- **The hull-shader engine-light term for weapon lights.** 72 plates per ship, per-draw constant upload, main hull
  only, unlit turrets [M, engine-light.md]; a weapon light is short and many; the lane glow costs one small quad.
- **Per-family user settings.** Ruled out by the user; everything is a header constant with a lab control and a
  fixture pin.
- **Wall-clock lifetimes under SETA.** The engine's own effects run on game time and the accepted patch does; the
  three-frame floor is the only wall-clock concession, so that nothing disappears between frames at SETA 6.
- **A separate "pulsed beam" family.** IOND is a fixed-length rod at 1,013 m/s [M]; the tracer law with `col 18`
  as its length is the same program.

## 5. Unknowns and what settles each

| Unknown | Settles it |
| --- | --- |
| The unit of the quad size cols 16/17 against the drawn width (bolt `w0`) | the observe flight's `bolt_size` rows (col 16 against the instance extents the footprint path derives) |
| Whether the flight tap's per-bullet list can replace the draw's instances one for one (counts, order) | the step-4 fixture against a recorded `INSTANCE_BULLETS` draw and a synthetic object list; else nearest-position matching by subtype |
| The beam draw's body pair | flight 4's observe `beam_draw` rows (the draw whose instance extent tracks `ext+0x78`) |
| The far-hull composite flag on native Windows (built under Wine, 2026-09-26) | the step-4 fixture's `bolt_far_hull_composite_flagged` through the production resolve on the native host |
| The exposure meter under a full-screen fireball | `death_exposure_meter_shift` through the production meter (`tools/analysis/exposure_reference.py` as the oracle) |
| The bubble cache at the death site (is the dying owner usually cached?) | `weapon_death` rows log `bubble_cached=`; below 0.5 the handler reads the sphere only |
| Which death-site reasons the user sees most (missile area kills, collisions) | the observe rows' reason-word histogram from flight 3's log |
| The R32F lane form (untested since step 1) | still the step-1 open item; the first fixture of step 2 runs one case with `four_channel` false |
| Stations' and turret platforms' fire routine (class 5/6 do not fire through `0x00445170`) | flight 4's observe: muzzle records with a class-5/6 shooter, or none; if none, station guns have bolts and no muzzle flash until that routine is traced |
| The numeric `OBJ_SHIP_*` id 2061 (FL) | the FL note; AP has no such row |

## 6. Choices only the user can make

Listed in the report; recorded here so a later session does not reopen them: the first-person bolt look (modern in
both views, or the native draw in the cockpit); smoke after explosions (off in the first flight); the cockpit gain
for the own muzzle flashes (full, or the patch's 0.35); whether asteroid deaths get any fire at all (dust and rock as
designed, or a small flash).

## Reproduce

```sh
# Effects rows with element kinds and durations (the tables of 2.2 and the vanilla scales)
python3 -I -c "import json; d=json.load(open('verification/results/weapon-effects-census/stock_ap.json')); print(d['effects']['37'])"
# Catalogue per weapon with style, calibre and effect summaries
python3 tools/effects/weapon_catalog.py --check
# The step-1 constants every family's header block follows
grep -n constexpr src/proxy/weapon_effects_core.h
# Per step, once built (the lock serialises the native fixture queue)
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_weapon_effects.py --host native
python3 tools/effects/generate_weapon_effects_lab.py --check
```
