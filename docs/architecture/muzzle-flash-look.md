# Muzzle flash, family 1: a small energy gun's flash, anchored to the bolt it launches

Design note, 2026-11-09 (Fable 5.1, design agent), for the muzzle step of the weapon redo. Edition: developed and flown
on Farnham's Legacy (`C:\X3FL`); IRE first (the fighter-tracer family's only enabled row). Inputs:
[projectile-look.md](projectile-look.md) (the accepted bolt, its tuning protocol, the enabled set),
[fl/weapon-effect-spawn.md](../reverse-engineering/fl/weapon-effect-spawn.md) §1, 2.5 (the FL muzzle site),
[fl/bullet-draw-batch.md](../reverse-engineering/fl/bullet-draw-batch.md) §4 (the bullet record),
[weapon-effects.md](weapon-effects.md) 2.3, 3.1 (the muzzle hook and record), `src/proxy/weapon_effects_bolt.h`
(`MuzzleRecord`, `bolt_muzzle`), `src/proxy/weapon_effects_projectile.h` (the pool), `src/renderer/weapon_effects_pass.h`
and `src/effects/weapon_soft_depth.hlsl` (the lane-depth decal path), the archived
[bolt-muzzle-look.md](../archive/effects-look-2026-11-08/bolt-muzzle-look.md) §3 and §11 item 1 (why the old muzzle
was a lamp) and the user's FL run 61 screenshot (local, not in the repository:
`/tmp/claude-1000/-home-asvetl-x3-mod/d5061608-f60a-47d6-b9b2-30b0b574851f/scratchpad/reference/ire_fl_run61_bolts_vanilla_muzzle.png`,
605x345, chase view behind a Boron fighter; crop or downscale unrecorded, so its pixels are relative). Marks: [M]
measured, [I] inferred, [S] static (code or note read), [D] user decision, [U] unknown.

**User decisions (2026-11-09) [D].** (1) The vanilla muzzle flash is a big splash; the flash must be "more accurate":
small, in proportion to the bolt. (2) A brief light pulse on the ship's own hull around the mount. (3) A short streak
from the muzzle in the weapon's colour.

## Outcome

The muzzle flash is **the bolt's own birth, brightened and held at the gun for one to two frames**: a point of
`flash_core_mult` x the bolt's core width at the gun's depth (start 1.25: never wider than the bolt by more than a
quarter), **width measured at scene white** (the extent that clips, which is what the eye reads as size; a Gaussian of
peak R clips out to σ √(2 ln R), so at +2 stops over the core the flash's Gaussian is 0.88 x the core's FWHM, narrower
than the bolt, and its white extent 1.25 x: the extra stops cannot grow it), a streak of `flash_len_m` metres (start
2.5 m, about three core widths) from the muzzle along the shot's axis in the weapon's colour with the core's white share,
at `flash_stops` (start +2) above the bolt's core so the bloom chain, not a drawn halo, carries the momentary flare; no
disc, no cone, no star, no smoke; past the depth where the point is under a pixel it is the bolt's 1 px dot. Beside it a hull pulse: a point light of radius `hull_pulse_r` (1 + k)
metres (start 3 m) on the shooter's hull around the mount through the lane-depth decal path, falling off with the same
game-time life. Everything is anchored to the engine's data: the position and axis are the launched bullet's (its first
tapped position back-extrapolated to its birth time, its velocity less the ship's), re-posed through the ship's rows so
the flash rides the ship; the life is `flash_life_frac` (start 0.2) of the gun's measured refire gap in game ms (the
existing `MuzzleRecord`'s `t0 − prev_t0`), never over two frames, SETA-aware by being game time. The game's flash is
skipped at the FL muzzle site for the enabled bodies only. **The anti-lamp invariant**, stated as a fixture gate: at
every depth the flash's extent at scene white is at most `flash_core_mult` x the bolt core's there (and at most 48 px
at 1440 rows near the eye), its lit area is a Gaussian's (the share at 80 % of the peak under 0.15 of the area at
10 %), and its life never exceeds two frames or `flash_life_frac` of the gap between shots.

**Before [M, the screenshot].** The two vanilla IRE muzzle flares are 38 x 37 and 38 x 41 px at luminance > 80 (28 px
FWHM across the centre row), yellow-orange with no white centre (peak sRGB (177, 167, 107)); the just-launched IRE
streak beside them is 16 px FWHM across and 40 px long at half peak. The flare is therefore 1.75x the streak's FWHM,
2.5x its extent and as long as it, and round. From the bolt's law (a core of 2 px at 500 m) the core at the gun's depth
(27 m in the chase view, the F3 camera 24 m behind the hull centre [M, archive §1]) is 37 px FWHM at 1440 rows, so the
measured streak at 16 px puts the crop's scale near 1/2.7 if the streak is about 60 m from the eye [I]; at that scale the
vanilla flare is about 2.0x the core's FWHM at the gun and 2.7x its extent. The user's reading: a big splash. The
proposed flash is 1.25x the core, Gaussian, two frames.

## 1. The anchor

| Item | Source | Mark |
| --- | --- | --- |
| muzzle position P0 | the bullet's first tapped node `+0x30` (int32 engine units, 500 per metre) minus v x (t_tap − t_birth), t_birth = ext `+8` (game ms, read at registration today), t_tap the tap's game ms. The first tap may follow the first mover step (age 0 or one step [U], section 9); the back-extrapolation makes P0 the birth point either way; an entry registered older than two steps gets no flash | S, I |
| axis û | normalize(node `+0x100` − v_ship), v_ship the shooter's node `+0x100` (the fire path adds the shooter's velocity to the bullet's [S, bullet-draw-batch §4]); the shooter is ext `+4` (one more bounded read at registration) | S |
| riding the ship | P0 is stored in the ship's frame, Rᵀ(P0 − P_ship) from the shooter's node `+0x30` / `+0x40` rows at the resolve, and re-posed per frame through the same owner read the shield stage already makes (`stage::locate_owner`: `obj+0x70 == node`, `node+0x28 == handle`, not dying); a failed read drops the flash (fail small). Needed: a 100 m/s fighter moves 1.7 m per 60 fps frame, 80 px at 27 m | S, I |
| gun slot k, shot time t0, refire gap | the FL muzzle shim's `MuzzleRecord` (one per (ship, slot), 128 records): `t0`, `prev_t0`, `row`, `ours`. A new pool entry of an enabled body is matched to the record with `ship == ext+4` and `t0 == created` (the fire routine creates the bullet and calls the spawner in the same update, one clock read [I]); the gap is `t0 − prev_t0` (game ms), the IRE's 150 ms at 1x (catalogue rate 400 / min [M]). Linked guns firing the same ms share a gap, so any of the ship's records at t0 serves | S |
| colour, k | the row's `texture_rgb` normalised (the IRE's sRGB (255, 177, 92)) and the calibre k, both already resolved per body for the bolt | S |
| life | life = min(`flash_life_frac` x gap, 2 x step_game); drawn on the entry's first stage frame whatever its age (one frame at least), then while age < life; radiance F(age) = 1 at the first frame, 1 − age / life after. A first shot (`prev_t0` 0) or an unmatched entry: one frame | D, I |

Why the bullet and not the record's `&pos`: `&pos` is ship-relative only by inference and the orientation's units are
the archive's unknown 6, while the bullet's `+0x30` and `+0x100` are measured engine fields the bolt stage already reads,
and the flash then sits exactly where the drawn bolt begins, with no seam. The record supplies what the bullet cannot:
the slot, the fire time and the gap. At the FL muzzle site the bullet's velocity is not yet written (`0x04f639` precedes
the store at `0x04f915` [S]), so the stub cannot anchor; the tap can.

**First person.** The own guns sit a few metres beside the eye [U, the archive's unknown 9], the axis is near the line
of sight, so the streak is end-on and the flash is the bolt's spot (the end-on rule, projectile-look §2) at the screen
edge. The core's pixel law gives 250 px at 4 m; the flash takes the fixed pixel ceiling of section 2 (48 px at 1440
rows, scaled by the rows), so near the eye it is smaller than its bolt, never larger. No own hull is drawn in first
person, so the pulse lights nothing there.

## 2. The look, as measurable properties

Projection as the bolt's: the live view and projection (f = 1,280 px·m at the launcher default fov 90 and 1440 rows;
the user flies 90–100), d_ref 500 m, core FWHM w(d) = `core_px` x 500 / d.

| Property | Rule | IRE at 4 m (cockpit) / 27 m (own gun, chase) / 500 m / 2 km |
| --- | --- | --- |
| point | a Gaussian point at P0 whose **extent at scene white** is min(`flash_core_mult` x the core's, 48 px x rows / 1440): with R_c = 2^(`stops` + k) and R_f = R_c 2^`flash_stops` its σ is `flash_core_mult` σ_core √(ln R_c / ln R_f) (0.88 x the core's FWHM at the start values), drawn with the bolt's core program (a zero-length segment with the end ramps: the spot); colour lerp(tint, white, `hot_share` (1 + k)); radiance R_f x scene white, F(age) | white extent (1,760 / d px at 1440 rows): 48 (capped) / 65 (the core's 52) / 3.5 (the core's 2.8) / 0.9 → the 1 px dot at its lost width; Gaussian FWHM 24 (capped) / 33 / 1.8 / 0.4 px |
| streak | a segment from P0 to P0 + û `flash_len_m`, the point's width and colour, flat along with the bolt's logistic end ramps, the tint only (no head); when its on-screen length is under the point's width it is folded into the point (the spot rule). **Foreshortened in the chase view:** the axis is near the line of sight there, so 2.5 m from 27 to 29.5 m projects to 4 px (the mock); the streak reads across the view (an NPC seen from the side, an outside camera) | 2.5 m across the view: end-on / 118 px / 6.4 px / 1.6 px (folded); in the chase view 4 px |
| brightness | +`flash_stops` over the core: the IRE core is 4.0 linear (+2 stops); the flash 16 linear clips white through AgX at its centre and feeds the bloom extract for its one or two frames; the far-flag route of the bolts (composited after the resolve, never in the history) | — |
| life | section 1: min(0.2 x 150 ms, 2 steps) = 30 ms at 1x: two frames at 60 fps (ages 0 and 17 ms), two at 144 fps (0 and 7 ms, 14 ms capped); under SETA 6 (100 ms steps) one frame per shot, a 40 Hz flicker while the gun fires, as the engine's own flash flickers [I] | — |
| LOD | where the point's white extent is under 1 px (past about 1.8 km at the start values) the point is the bolt's 1 px dot at its radiance x the lost width (still 16 x white x the width: a bright pixel, the bloom's star); the streak folds in; the pulse is dropped under a 3 px radius (past 1.3 km at 3 m) | 2 km: a 1 px dot, no streak, no pulse |
| anti-lamp invariant (the gate) | the extent at 1.0 linear ≤ `flash_core_mult` x the core's at the same depth within 0.5 px and ≤ 48 px at 1440 rows (measured on the linear FP16 readback, so the clipped size is exact); the lit area at ≥ 80 % of the peak ≤ 0.15 of the area at ≥ 10 % (a Gaussian gives 0.10); length ≤ `flash_len_m` on screen within 1 px; life ≤ min(`flash_life_frac` x gap, 2 steps); nothing drawn for an entry without a matched `ours` record | — |

**How it reads.** *Chase, own guns at 27 m (the mock):* the bolt leaves as a streak foreshortened toward the vanishing
point (19.4 m of travel, 21 px in the mock's 256 rows, 120 px at 1440) and a point clipping white over 11.6 px (65 px at
1440 rows; the bolt's own core there would clip over 9.3 / 52 px) stays at the mount for two frames in its orange glow,
the bloom adding its momentary halo, while the hull within 3 m of the mount lifts by up to half a scene white and falls
back: a shot, not a lamp. The vanilla flare measured 2.7 x the core's FWHM in extent, about 100 px at 1440 rows [I].
*Cockpit:* a 48 px spot at the screen edge for two frames, no pulse. *NPC fighter at 500 m:* a point clipping white over 3.5 px (the
bolt's 2.8) with a 6 px dash across the view, so the bloom gives a one-frame star of the gun's colour on the hull: the
cue that the ship fired; the pulse a 7.7 px radius lift on its hull. *At 2 km:* a 1 px dot that
blinks with each shot. Under SETA every shot is one frame; the stream is a flicker at the gun and a row of long, dim
comets.

## 3. The hull pulse [D]

**Path.** The stripped tree keeps no point light; it keeps the lane-depth decal path: `WeaponDecal` (a camera-facing
quad through `DrawPrimitiveUP`, the decal vertex program) whose pixel program reads the lane depth under the pixel,
rebuilds the scene point and fades two-sided about the quad's depth (`surface_affinity`, `weapon_soft_depth.hlsl`) [S].
The pulse is a fourth quad kind, `WeaponPulse`, in the same pass: the decal vertex program unchanged, one new pixel
program of about 25 slots: the scene point from the lane depth and the raster, r = |scene point − P0| in view units,
light = I tint (1 − r / R)² F(age), discarded past R and where the lane holds no occluder (the sentinel), so sky pixels
are never written. No normal (the lane has depth only; the hex patch's normal is the bubble's analytic one): the
pulse is a distance falloff on whatever surface lies within R, a glow around the mount rather than a lit surface,
acceptable for one or two frames.

**Size.** R = `hull_pulse_r` (1 + k) metres (the IRE k 0: 3 m; the HEPT 0.41: 4.2 m), I = 0.5 (1 + k) scene white at
the mount, the flash's F(age) and life. On screen: a 142 px radius at 27 m, 7.7 px at 500 m, dropped under 3 px.
The quad is R's projection plus the soft margin; it is bounded by the same owner read as the flash (the ship's rows), so
a lost owner drops both.

**Cost.** Two own guns at 27 m: two 284 px quads, 0.16 Mpx, about 2 µs GPU [I]; 62 far pulses negligible. CPU: no
extra read (the flash's owner pose), one projection.

## 4. Suppressing the game's flash

The FL muzzle site `0x04f639` (fire routine, ECX 4, EDX the gun slot, the ship at `[esp+8]`, ESI the bullet index x
`0xdb8`) is already claimed by the FL shim and handled by `core::bolt_muzzle`, which records the shot and returns skip
when `muzzle_owned && suppress_muzzle` [S, `weapon_effects_bolt.h`]. The change: **skip iff the projectile latch is on
(the batch stub's `skip_on`, the same frame verdict) and the row's body (TBullets word `+0` at `[0x1a83c8] + ESI`) is
in the stage's enabled body set** (the 65536-bit set the batch stub and the draw hook test); the record's `ours` is the
skip. `muzzle_owned` and `suppress_muzzle` retire. Every other row forwards byte for byte (PAC, HEPT and the rest keep the
game's flash until their rows are enabled, as their bolts do). Cost: one word read and one bit test per shot.

**Rows the engine never flashes.** The FL site is reached only when TBullets col 21 (`+0x7c`) is non-zero; the engine
skips the spawner itself for the five rows with col 21 = 0 (tug, repair, the three Kha'ak beams) [S, FL §2.5]. Those shots
make no `MuzzleRecord`, so a bullet of such a body gets no match and no flash from us either: the guns that never had a
flash keep having none, by construction on FL. None of them is in family 1. On AP the wrapper tests col 21 inside the
callee, so an AP port of this step must read `+0x7c` in the handler; AP installs nothing of this today.

**Failure modes.** *A missed skip (double flash):* the stub forwarded (latch off, body not enabled, or a wrong site)
while the stage drew. By construction the stage draws a flash only for a pool entry matched to a record with
`ours = 1`, so a forwarded shot is never also ours; a forwarded shot of an enabled body under the latch is counted
`muzzle_forwarded_enabled` and must be 0 in flight. The remaining double path is a skip that did not prevent the
spawn, which the hook fixture excludes (the callee not entered). *A missed flash (none):* the first shot of a burst in an
unowned frame (the engine's flash shows, the bolt's own fallback), a pool overflow (`projectile_overflow`), an evicted
record (`muzzle_evicted`, 128 slots), an entry registered over two steps old (`flash_stale`). The flight detects a double
flash in the `weapon_effects_frame` row (`muzzle_skipped`, `muzzle_forwarded_enabled`, `flash_drawn`, `flash_unmatched`,
`flash_stale` per period) and by eye: a vanilla blob beside a thin flash in an F8 capture.

## 5. Calibration through the dev tuning file [D protocol of projectile-look §4]

Five new keys in `C:\X3FL\x3m\projectile_tune.json`, read by the same reader (unknown keys ignored, a missing key the
baked value, clamped with one row per clamped key, never shipped: `X3M_DEV_TUNING`):

| Key | Start | Bounds | Meaning |
| --- | --- | --- | --- |
| `flash_core_mult` | 1.25 | 1–2 | the point's extent at scene white over the bolt core's at the same depth (the invariant's factor; the upper bound is the gate's) |
| `flash_len_m` | 2.5 | 0.5–10 | the streak's world length from the muzzle (under the 1x 60 fps travel of 19.4 m at every bound) |
| `flash_life_frac` | 0.2 | 0.05–0.5 | life as a share of the gun's measured refire gap, capped at two frames |
| `flash_stops` | 2 | 0–4 | radiance over the bolt's core, in stops |
| `hull_pulse_r` | 3 | 0–10 | the pulse radius in metres at k 0 (x (1 + k)); 0 turns the pulse off |

Defaults derive from the bolt: 1.25 and 2.5 m from the core's width (0.78 m world at f 1,280), +2 stops because the core
is +2 and a flash should clip where the bolt does not, 0.2 from the IRE's 150 ms gap and the 60 fps step. Protocol: one
number at a time, a burst at 1x and under SETA in chase view, the five values from the last `projectile_tune reloaded`
row with a screenshot or F8 capture; the accepted set is baked into the family's `Tune` and the file deleted. The IRE's
values are the family's; PAC and HEPT then move only through k.

## 6. Verification

Native fixture (`run_weapon_effects.py --host native`, frames dumped and viewed before the flight), arithmetic only:

| Gate | Case | Must hold |
| --- | --- | --- |
| size | the flash at 4 m, 27 m, 200 m beside its bolt (linear readback) | extent at 1.0 linear = min(`flash_core_mult` x the core's, 48 px x rows / 1440) within 0.5 px; the cap binds at 4 m only; at `flash_stops` 0 the flash's FWHM equals the core's x `flash_core_mult` (the σ law's other end) |
| streak | 27 m and 500 m, the axis across the view | on-screen length = `flash_len_m` projected within 1 px; folded into the point at 2 km |
| life vs refire | gap 150 ms at 17 ms steps, 7 ms steps and 100 ms steps (SETA 6); a first shot | drawn at ages 0 and 17 (not 33); 0 and 7 (not 14); once under SETA; a first shot once; F as the law |
| LOD | 2 km and 3 km | a 1 px dot, integrated radiance ≤ the 500 m flash's, no streak, no pulse |
| anti-lamp | every case above, over black and over a lit hull | the invariant of section 2 (FWHM, the 80 % / 10 % area share ≤ 0.15, length, life); nothing drawn for an unmatched entry |
| pulse | own fighter at 27 m, a capital hull at 500 m, sky under the quad | lit only where the lane holds a surface within R; peak I (1 + k) at the mount within 5 %; sky unchanged; off under 3 px |
| suppression (hook fixture) | the FL muzzle shim on the synthetic frame | enabled body under the latch: the callee not entered, the record `ours`; a disabled body and an unlatched frame: forwarded with every register and the stack as the call left them |
| budget | 512 bolts + 64 flashes + 64 pulses at 5120x1440 (the two own at 27 m, the rest spread) | CPU ≤ 110 µs, GPU fenced ≤ 60 µs (the projectile row today: 88.0 / 46.4 µs [M]; the flashes' estimate +20 / +5 µs [I]) |
| tuning | the five keys at bounds, garbage, missing | clamped rows, refused, defaults |

What only the user judges: whether the flash reads as a shot and not a lamp in chase view at 1x; whether 1.25x the
bolt is "small" or already invisible; the pulse's strength on the own hull; the SETA flicker; the cockpit spot at the
screen edge; whether the two-frame life feels like a flash or a blink.

## 7. User decisions (2026-11-09)

1. First person: the user does not fire in first person, but others may; the own-gun flash stays drawn there as
   designed (a spot at the screen edge plus the hull pulse); no cockpit exception.
2. SETA: keep the one-frame flash per shot for now; judge in flight.
3. Hull pulse on every ship, own and NPC.
4. The look is undecided until flown: "let's build and look"; the first flight (FL run 62) judges it.

## 8. Options considered and why they lose

- **A disc, cone or four-arm star at the gun** (the archive §3). Flat discs of 40–85 px re-lit most of the time were
  the lamp (§11 item 1); a structured body carries size by design. The redo's flash carries no size of its own.
- **Keep the game's flash** (projectile-look §7 item 3 was provisional). The user rejects it; it is also the one effect
  that still sizes itself independently of our bolt.
- **Anchor from the record's `&pos` and orientation.** Both are inferred (frame and units unknown 6); a misread puts a
  flash beside its bolt. The bullet's fields are measured and already read. The record keeps the slot, time and gap.
- **Refire from TLaser col 7 (`+0x44`) through the laser object.** The TLaser table slot and stride are not established
  for FL [U]; the record's `t0 − prev_t0` is the same number measured, with no new engine read.
- **A three-frame minimum or a held flash under fast fire** (the archive's no-alternation rule). Holding is how a flash
  becomes a steady light; a gun firing under SETA is a flicker.
- **Width defined by the Gaussian's FWHM.** The first mock showed it: at +2 stops a Gaussian clips white to 2.0 x its
  FWHM where the bolt clips to 1.41 x, so a flash of 1.25 x the core's FWHM looked 1.8 x the bolt and the vanilla's
  size. Brightness is apparent size; the invariant is therefore stated at scene white and the σ follows from the stops.
- **An energy-conserving pixel cap** (radiance up when the width is capped). Near the eye that pushes a 16 x white
  point higher and the bloom makes the lamp the cap meant to prevent; the cap keeps the peak and shrinks.
- **A drawn halo or a sprite texture.** The bloom chain supplies the halo from the core's energy (the bolt's rule); a
  texture reintroduces an authored size.
- **A lit hull with normals.** Needs a normal lane the renderer does not have; the depth-only falloff is enough for two
  frames and costs one small program.
- **Smoke, vents, afterglow.** Energy guns have none; the family decides for ballistic rows.
- **Flash life as fixed ms.** Not SETA-aware and not proportional; the gap is.

## 9. Unknowns and what settles each

| Unknown | Settles it |
| --- | --- |
| the age of a bullet at its first tap (0 or one mover step) | the observe flight: a `flash_first_age` histogram in the frame row; or the tap fixture's ordering case |
| the record's `&pos` frame and the orientation's units (archive unknown 6) | the observe flight comparing `&pos` through the ship's rows with the back-extrapolated birth point (max error in the frame row); settles whether `&pos` could serve as a cross-check |
| the own guns' offsets from the eye in first person (archive unknown 9) | the same flight in first person: P0 in the ship frame per slot, logged once per slot |
| whether the shooter's node `+0x100` is the velocity the fire path added | the observe flight: û against the ship's heading for a forward gun |
| the screenshot's scale (the before figures are relative) | an F8 capture; the Boron fighter's span against the crop |
| the bloom's actual halo on a 16 x white point for one frame natively | the flight's F8 capture; the fixture has no bloom |
| the TLaser table slot and stride on FL | not needed by this design; a disassembly of the fire routine's `+0x44` read if a per-gun refire is ever wanted before the second shot |

## Reproduce

```sh
python3 verification/results/muzzle-flash-look/render_mock.py   # mock-ire-muzzle.png, mock_summary.json
```

The mock ([`mock-ire-muzzle.png`](../../verification/results/muzzle-flash-look/mock-ire-muzzle.png), 512x256, the
1440-row frame at 256 / 1440, f 227.6 px·m, the eye 24 m behind and 5 m above the hull centre, the mounts 3.2 m out and
2.5 m ahead) shows the two mounts of a fighter from the chase camera (a hull in shadow, 0.03–0.08 linear), each IRE
bolt's first-frame streak leaving (21.6 px, the head at 46 m with a 3.9 px core), the proposed flash at the start values
(Gaussian FWHM 5.8 px against the core's 6.6 at 27 m, white extent 11.6 px against the core's 9.3: ratio 1.25; the streak
4.3 px foreshortened; radiance 16; the pulse 25 px radius) and the vanilla flare's measured size as a dim outline beside
the right mount (extent 17.8 px = 2.7 x the core's FWHM, FWHM 13.2 px = 2.0 x, dotted) [M, `mock_summary.json`]. Linear
light at 0 EV, sRGB-encoded as the projectile mocks; no bloom, AgX or TAA.
