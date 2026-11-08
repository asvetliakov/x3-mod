# Projectile look, family 1: fighter tracer bolts, IRE first

Design note, 2026-11-08 (Fable 5.1, design agent), for the weapon projectile redo after the look layer was scratched
([archive](../archive/effects-look-2026-11-08/index.md): bolts authored from notes and gates, sized and timed by us,
judged on synthetic scenes; the user's verdict "medium-big weapon projectiles are still big blobs", "a little bulky, I
would expect more accurate smaller bolts"). Edition: developed and flown on Farnham's Legacy (`C:\X3FL`, table
`C:\X3FL\x3m\weapon_effects.json`, 45 bullets). Inputs: [weapon-effects.md](weapon-effects.md) (hooks, record,
classifier, 2.10 FL), [weapon-effect-spawn.md](../reverse-engineering/weapon-effect-spawn.md) §1–2, 8.2 and its
[FL counterpart](../reverse-engineering/fl/weapon-effect-spawn.md) §1, 2.3–2.4, 3, the archived
[bolt-muzzle-look.md](../archive/effects-look-2026-11-08/bolt-muzzle-look.md) §1–3, 5 (facts reused, look not),
[weapon-catalog.md](../reference/weapon-catalog.md) (FL), [bolt-footprint.md](bolt-footprint.md) and
[bolts-through-taa.md](bolts-through-taa.md) (the production path of the game's own bolt draw), `src/proxy/weapon_effects_bolt.h`
(the kept bolt tap), the user's "before" screenshot of FL run 58 (local, not in the repository:
`/tmp/claude-1000/-home-asvetl-x3-mod/d5061608-f60a-47d6-b9b2-30b0b574851f/scratchpad/reference/ire_fl_before_run58.png`, 633x564, chase view behind a Boron fighter, two IRE guns firing at a
target about 1.5 km ahead). Marks: [M] measured, [I] inferred, [S] static (code or note read), [D] user decision, [U] unknown.

**Decisions already taken (orchestrator, 2026-11-08) [D].** The look direction: a thin bright core, a soft glow of the
weapon's colour, a SETA-aware motion stretch, no persistent trail on energy guns, a white-ish centre for the brightest
guns, a dot at distance. The dev-only in-flight tuning file (section 4). Both are written here as decisions; the
options considered for the rest are in section 8.

## Outcome

A fighter tracer is **a line of light, not a sprite**: a core of `core_px` (start 2 px) full width at the reference
distance 500 m, whose world width therefore follows from the pixel figure and the projection and shrinks with distance
to a 1 px dot whose radiance falls with its lost area; a Gaussian glow of the weapon's colour at `glow_ratio` x the core
radius (start 3) and `glow_gain` (start 0.2 of scene white), the wide halo left to the bloom chain; a streak from the
previous frame's drawn head to this frame's (the distance the bolt flew this frame, so SETA lengthens it; consecutive
frames meet end to end, FL 59 A), flat along its length, dimmed by `length_cap_m` / length past the cap (section 2);
a hot centre of `hot_share` white (start 0.3 for the IRE, the dimmest gun); radiance 2^`stops` x scene white
(start +2 stops) written into the FP16 scene with the far flag, so the bolt is composited after the TAA resolve and
never enters the history; nothing behind it. Every position, direction, speed, birth and end is the engine's (the live
node's, read per frame); the only authored numbers are the seven tuning values, calibrated in flight against the game's own bolt
and baked after the user accepts them. The stage replaces the game's bullet draw per body (one instanced batch per
bullet body) and draws every tapped bullet of the accepted rows; every other body keeps the game's draw untouched.
Mock **B** (`mock-stretched.png`) is the recommended starting look.

## 1. The family tree

**Shape x mount tier, nine families.** The shape (the table's `shape`, from the vanilla body's elongation
`z_over_xy`: **tracer** at least 4:1, a needle; **slug** 1.5–4:1, a short body with a head; **orb** under 1.5:1, round)
says what kind of thing a bolt is, so it sets the anatomy: a tracer is a line with a stretch, a slug a short body whose
head leads, an orb a ball with no axis. The mount tier (`mount_tier`: the catalogue size of the ships that mount the
gun, fighter / corvette / capital) says at what distance the bolt is fought and against which hull it must read (a
12 m fighter at 500 m, a 1.3 km M2 at 4 km), so it sets the reference distance and the pixel figures. Neither alone
works: a capital flak tracer and the IRE share a shape but not a distance; a fighter slug and a fighter tracer share a
distance but not a kind. Per gun, every family has 45 flights; per shape or per tier alone it mixes distances or kinds.
From the FL table [M, `weapon_effects.json`, 45 rows; fighter 25, corvette 9, capital 11]:

| Family | Rows | Flown order |
| --- | --- | --- |
| fighter tracer (10) | IRE, PAC, MASS, PRG, HEPT, PBE; FBL, FLAK_CLUSTER (x2), FLAK_ALPHA fragments | **1**: IRE, then PAC, then HEPT; the rest take the family's constants |
| fighter slug (10) | EBC, FBL, IOND, EMP, Terran anti-fighter (x2), Kha'ak alpha / beta, IPG fragment, EMP_1 | 2 |
| fighter orb (5) | PD, MAM, Kha'ak gamma, Terran anti-fighter fragment, MAM_1 | 3 |
| corvette tracer (6) | CIG, IPG, ESR, PAL, tri-beam, fusion beam | 4 |
| corvette slug (2) | PBC, tug | 5 |
| corvette orb (1) | mining | 6 |
| capital tracer (3) | flak alpha, flak cluster, repair | 7 |
| capital slug (6) | PPC, IONC, Gauss, IBL, two dummies | 8 |
| capital orb (2) | PSG, PSP | 9 |

The beam-flagged rows in these families (Kha'ak, IOND, PAL, tri-beam, fusion beam, PBC, tug, repair; flags `0x2`/`0x4`
[S, classifier 4.1]) take their family's tier and colour but their body is the beam step's, flown after the bolts; the
waves (PD, PSG, Terran AF fragment, flag `0x8`) likewise keep their flag's body inside their family.

**Shared within a family:** the seven tuning values of section 4 (core px at the reference, glow ratio and strength,
hot-centre share, dot distance, length cap, brightness). **Per gun, from the game:** colour (the
body texture's alpha-weighted mean `texture_rgb`, or the override tint; the IRE's is sRGB (121, 82, 39), normalised to
linear (1.0, 0.44, 0.11) = sRGB (255, 177, 92) at full radiance [M, `render_mocks.py`]), speed (TBullets col 11: IRE
581,000 units/s = 1,162 m/s [M]; the stage reads the live velocity instead), lifetime (col 10: 1,200 ms) and calibre
k = clamp(log10(d / 300) / 2, 0, 1) over the damage per shot (IRE 220 → 0, PRG 0.18, PBE 0.25, PAC 0.29, HEPT 0.41
[M, catalogue]), which within a family scales only the hot-centre share (hot_share x (1 + k)) and the brightness
(+k stops), so the HEPT reads hotter than the IRE without a second constant set. The old look layer's hand pins in the
table (`style: slug` on MASS and HEPT, `bolt_e_body`, `bolt_halo`, `bolt_w_orb`) belong to the scratched look and are
not read; the shape column decides the family.

## 2. The look of a fighter tracer

**Before [M, the screenshot, 633x564 px; whether it is a crop or a downscale of the 5120x1440 frame is unrecorded, so
its pixels are relative].** The vanilla IRE streaks a few hundred metres ahead of the ship: 35 x 12 and 67 x 14 px at
the red-orange threshold, of which the bright centre is 6–8 px wide, a peach centre (232, 144, 89), a red mean
(175, 52, 41) and a dark red rim (121, 10, 10); the shot still at the muzzle 39 x 5 px; the one near the target 1.5 km
away a 6 x 6 px dot. The vanilla muzzle flashes are yellow-orange blobs of 36 x 24 and 34 x 27 px at the wing tips,
larger than any bolt. The user's reading: "a little bulky; I would expect more accurate smaller bolts". The redo's
bolt is therefore narrower than the vanilla streak by about 3x at the core (2 px against 6–8), keeps a length of the
same order from the motion stretch, and loses the dark rim (an additive light has no rim).

**Properties, each measurable [D direction; the numbers are starting values for the calibration of section 4].**
Projection at 1440 rows: f = 1,280 px x m per metre of distance at the launcher default `fov 90` (58.72° vertical),
960 at the vanilla 73.74° [S, field-of-view.md; the archive's §1 arithmetic]. The reference distance d_ref = 500 m
(2.56 px per metre).

| Property | Rule | IRE at 200 m / 500 m / 1 km / 3 km (fov 90, 60 fps, SETA 1) |
| --- | --- | --- |
| core width | **w_px = core_px x 500 / d** (d the view depth in metres), independent of the projection: only the length (and the position) depends on the live projection. (At fov 90, f = 1280, that is a world width of 0.78 m.) Under 1 px it is drawn 1 px wide at radiance x w_px (a far bolt dims, it does not fatten) | 5.0 / 2.0 / 1.0 / 0.33 → 1 px at a third |
| glow | a Gaussian of σ = `glow_ratio` x core_px / 2 in screen px at d_ref, scaled with distance like the core, floor σ 1.5 px; colour the gun's tint; radiance `glow_gain` x scene white. The wide halo is the bloom's: the core's own energy feeds the extract, so the drawn glow is only the near field | σ 7.5 / 3 / 1.5 / 1.5 px |
| motion streak (bridged, FL 59 A) | a segment from the previous frame's drawn head to this frame's head P (the body origin, section 3): **bridged by construction**, so one frame's streak ends exactly where the next one starts at any frame rate and SETA, and the eye's integration of successive frames is one unbroken line. A first frame (a new bullet, a jump, a frame the stage skipped) runs from P − v Δt_game (cfg step: AP `+0x714`, FL `+0x704` [M, fl/seta.md]), never behind the muzzle (|v| x age, ext+8). No multiplier (the former `stretch_k`): the length is the travel. Along the axis the profile is flat with logistic end ramps whose half maximum sits at the ends, Φ(x + h) − Φ(x − h); the previous frame's ramp and this one's telescope, so the seam is neither a gap nor a bead. **Energy-conserving cap:** past `length_cap_m` the streak keeps its full length and its radiance scales by cap / length (motion blur conserves light: a fast or SETA bolt is a longer, fainter streak, never a chopped dash) | at 60 fps (19.4 m): 124 / 50 / 25 / 8 px; at 144 fps (8.1 m): 52 / 21 / 10 / 3 px; under SETA 6 (100 ms, 116 m) the full 116 m at 30 / 116 = 0.26 of the radiance (arithmetic, f = 1280) |
| nothing behind | no smoke, no fading streak, no history beyond the previous frame's head (the streak is one frame's travel): an IRE fires 6.7 shots/s per gun at 1,162 m/s, so a trail would turn a stream into a beam and persist where the TAA would smear it; a short fading streak is acceptable only for a slug or a kinetic round (MASS, EBC: a burning tracer has one) and is that family's decision | — |
| hot centre | the core colour is lerp(tint, white, `hot_share` x (1 + k)); the IRE at 0.3 keeps its orange, the HEPT at 0.42 goes pale | — |
| distance LOD | past `lod_dot_m` the stretch is dropped and the bolt is a 1–2 px dot with the glow at its floor; never a rim, an outline or a flat disc (a decal) | at 3 km a 1 px dot of 8 px length, or a dot past the limit |
| HDR | radiance 2^`stops` x scene white (the lit hull's white, 1.0 linear) into the FP16 scene (ONE/ONE), under the same auto exposure (capped +1.3 EV) and AgX as every light; the bolt is energy in the scene, not a sprite with a glow painted on | +2 stops: 4.0 linear, clips white at the core through AgX |
| TAA | the pixel program writes the far flag (RT2's lane, `oC2 = 32 max(rgb)`, the production B' route of bolts-through-taa.md) so the write-back composites the bolt from the current frame and it never enters the history: no clamp attenuation of a 2 px line, no scar over a hull, no ghost; the Gaussian edges are the anti-aliasing; the half-pixel jitter is nothing against 50 px of motion per frame | — |

End-on (the chase view looks along the flight axis) the segment projects to a point and the bolt is a disc of the core's
width with its glow, by construction (the along profile's half length is at least half a core sigma, normalised to the
core's radiance at its centre). Birth: never longer than flown (head − v̂ min(|v| Δt, |v| x age)), so it grows out of
the muzzle over its first frame. End: absent from the tap list (hit or expiry) means not drawn; the impact family
covers the cut. A camera turn is not smeared (the streak is the bullet's world motion, not the screen's).

**Why bridged (FL 59 A, 2026-11-08).** With the streak at 0.4 of the per-frame travel (capped at 30 m) the user saw each
IRE near the ship as a dashed line, "--- --- ---", in motion only (a single frame shows one dash; `stretch_k` 0 did not
change it): a phantom array, the eye integrating successive frames' short dashes at their new positions with the gaps
between them; the game's fat long mesh overlaps frame to frame and hides it. At `stretch_k` 1.0 and 1.5 the gaps went
(the diagnosis held) but the seams showed hot beads: the round Gaussian end caps of two consecutive dashes overlap and
add. The streak is therefore the bridged travel with complementary end ramps (no multiplier: a k above 1 overlaps, below
1 leaves gaps), and the cap conserves energy instead of cutting the length.

**Static frames (the logic-tick question).** The stage keeps, per pool entry, the read position and the drawn head of
its last stage frame. An entry whose node +0x30 did not change since the previous stage frame counts
`projectile_static` and its head is extrapolated by v x the game time since the change, accumulated per static frame as
the render dt (the proxy's QPC between stage frames, 0 past 0.25 s) x the engine's SETA rate (cfg `+0xbc/+0xc0`, the
plumes' `seta_read`; 1 when refused), bounded at two game steps (a paused game or a stopped bullet holds instead of
flying on); its tail stays the previous head, so it never freezes and never gaps. An entry whose read position is
farther than 1 m + a quarter of the frame's travel from read position + v x (game ms since its change) counts
`projectile_jumped` and draws as a first frame. A head extrapolated past the next read position holds (never
backwards). Both counters are summed in the `weapon_effects_frame` row: FL 59 A's flight settles whether the engine
advances node +0x30 less often than the render frame.

**The three mocks** (`verification/results/projectile-look/render_mocks.py` → `mock-{thin,stretched,glow}.png`,
512x256, one IRE bolt at 500 m on black beside a dim hull edge, linear light exposed and sRGB-encoded; no bloom, AgX or
TAA, so the halo the bloom would add is absent; `mocks_summary.json` holds every figure) [M]:

| Mock | File | Core | Glow | Stretch | Reads as |
| --- | --- | --- | --- | --- | --- |
| A | `mock-thin.png` | 2 px FWHM, +2 stops, hot 0.3 | σ 3 px, 0.2 | none | a point of light: the bolt at rest or end-on |
| **B** | `mock-stretched.png` | as A | as A | 6:1 (12 px) | a short bright dash: the bolt in flight at a quarter of its 1x per-frame motion |
| C | `mock-glow.png` | as A | σ 8 px, 0.4 | none | a soft ball: the glow outgrows the core, the balloon the archive's review named |

**Recommendation: B.** The streak is what makes a 2 px core a bolt rather than a star; at 1x and 60 fps the in-game
streak is 50 px at 500 m (the bridged travel, FL 59 A), so B's 12 px was the conservative end. A is the
dot the far LOD collapses to; C is rejected as the body (section 8), its glow level is what the bloom should supply.

## 3. The engine anchor

**Read live, per frame, from the engine's node** ([bullet-draw-batch.md](../reverse-engineering/fl/bullet-draw-batch.md)
section 4, which corrects this note's first version). The object tap (FL `0x0645bd`, every live class-0 object once per
update pass [M, spawn FL §2.4]) registers a bullet of an enabled body (node `+0x144`) into the stage's pool; each frame
the stage reads node `+0x30` (the **body origin**: the IRE mesh is symmetric in z, so the drawn bolt is centred on it,
not headed by it [i]), node `+0x100` (the velocity in engine units per second, the shooter's velocity included [s]),
`+0x28` (the handle: a recycled node is retired) and the remaining life ext+0 − cfg `+0x708` (game ms). The FL flight
site `0x014eaa` is not the registration: it is reached only for a bullet whose TBullets col 19 (flight effect) is
non-zero, and the IRE's is not [s, `0x014e78..0x014e93`]. **Not read:** the bullet size cols 16/17 and ext `+0x78`.
**The dart length corrected:** ext `+0x78` = col 11 x `0x1999` / node scale is the bullet's **collision hull**, 0.1 s of
flight divided by the node scale (about 116 m in the world for the IRE [i]); no draw path reads it [s, not exhaustive].
The bolt the game draws is the body mesh at the node's scale, with no speed term. So the stage takes no length from the
engine: the dash is the per-frame motion stretch (the user's choice, section 7), capped by `length_cap_m` in metres.

**Replace per body, not draw over [the rule, as built].** The game draws one instanced batch per (bullet body, texture)
through the submit call **`0x097af8` → `0x0e0b40`** (bullet-draw-batch.md section 1); every draw holds exactly one body,
and the batch record carries its body id at `+0x0c`. The hook claims that call: while the stage owned the previous frame,
a batch whose body is enabled is skipped (the callee's own plain `ret`, EAX 1); every other batch is forwarded with every
register, flag and the stack. A bolt the batch path does not take (fewer than two in a layer, or past the `0x78f0`
vertex budget) is drawn through the ordinary material submit, inside the FL object-trace scope: the proxy's draw hook
drops that draw when the scope's node is a pooled node of an enabled body. The IRE's body `bullet_ls_alpha` is shared by
rows 0, 37, 38 and 39 (all fighter tracers [M, table]), so all four are the stage's. Ownership is the existing latch
(Present: the stage drew, or had nothing to draw while armed, in the frame just resolved); an unowned frame forwards
every batch and the game's bolts are back.

Failure modes. *Replace:* a pool miss (the 512-entry cap, or a bullet created after the update pass and drawn in the
same frame) means that bolt is absent for a frame; the overflow is counted (`projectile_overflow` in
`weapon_effects_frame`) and is the kill criterion for raising the capacity to 1,024. A wrong body identification drops a batch nobody redraws:
fail closed, the identification must be positive (below). *Draw over* (rejected, section 8): two bodies, the game's
6–8 px streak under our 2 px core, exactly the bulk the user rejected, and the game's quad near the camera is the blob.

**Bullets the hooks never see.** Col 21 = 0 (no muzzle row) only removes the muzzle event; the bullet still exists and
is tapped; in FL those rows are the five beams (Kha'ak x3, tug, repair) and nothing in family 1 [M, catalogue]. The col
19 flight sprite (PD, both flaks) is not in family 1 either; the flight hook keeps forwarding. Bullets out of view are
tapped (the list is sector-wide) and culled by the stage.

## 4. Calibration in flight [D]

`C:\X3FL\x3m\projectile_tune.json`, dev only, at most seven numbers for the family; the starting values and bounds
(`weapon_projectile_family.h`):

| Key | Start | Bounds | Meaning |
| --- | --- | --- | --- |
| `core_px` | 2.0 | 0.5–8 | core full width (FWHM, px) at d_ref = 500 m, at the live projection |
| `glow_ratio` | 3.0 | 1–12 | glow σ over the core radius |
| `glow_gain` | 0.2 | 0–2 | glow radiance in scene white |
| `hot_share` | 0.3 | 0–1 | white share of the core colour (x (1 + k) per gun) |
| `lod_dot_m` | 3000 | 500–20000 | past this view depth (m), no stretch: a dot |
| `length_cap_m` | 30 | 5–500 | **energy-conserving** since FL 59 A: a streak longer than this (metres) keeps its full length and its radiance scales by cap / length (before: a hard cap that cut the length; 116 at first, 30 baked after the look frames; the engine's own 116 m figure is the collision hull, section 3). The former `stretch_k` key is gone (the streak is the bridged travel); a file still carrying it is read without it and without a row |
| `stops` | 2.0 | −2–6 | core radiance 2^(stops + k) x scene white |

The proxy checks the file's last-write time at most once a second on the Present thread (`GetFileAttributesExW`,
`ftLastWriteTime`), reads a changed file into a fixed 1 KB buffer, parses it once with a flat key-number scanner (no
allocation, unknown keys ignored, **a missing key takes the baked value**: each reload starts from the defaults, so the
file states every deviation), clamps every value to its bounds (one `projectile_tune clamped key=…` row per clamped
key), copies the struct into the stage's constants (same thread as the stage, no lock) and logs one row
`projectile_tune reloaded family=fighter_tracer source=file keys=… values=…` per reload (a key the file did not set is
starred); a malformed file logs one `projectile_tune refused` row and keeps the last values; a removed file returns to
the baked values with one `source=defaults` row; a file that never existed logs nothing.
Nothing per frame but the one-second timer. **It never ships:** the reader (`weapon_projectile_tune.cpp`) compiles only
under the CMake option `X3M_DEV_TUNING` (default ON for a RelWithDebInfo build, OFF otherwise; `tools/release/release.py`
configures OFF), so a release DLL has the constants of `weapon_projectile_family.h` and no file code or path. **Protocol:** the user edits the file, flies, reports the seven
numbers and a screenshot; we bake them as the family's constants in the header, the user deletes the file. One gun at a
time: the IRE's accepted values are the family's; PAC and HEPT are then flown on them and may move only the per-gun
inputs (colour, hot share via k), unless the user asks.

## 5. Verification without synthetic judging

The native D3D fixture (`run_weapon_effects.py --host native`, through the production `TemporalPass`; its frames are
dumped and viewed before any flight, never judged by gates alone) may check what is arithmetic: (1) the core's FWHM
across the axis at 500 m equals `core_px` within 0.25 px and scales as d_ref / d at 200 m and 1 km, and at 3 km the
dot is 1 px with the integrated radiance per bolt within 10 % of the near one (the floor dims, it never fattens);
(2) SETA invariance: at steps 16.7 and 100 ms the stretch equals k v Δt up to the cap and the integrated radiance per
bolt is within 10 % (dims as it lengthens); (3) nothing behind: the pixels 3σ behind the tail end are under 1 % of the
peak; (4) the far-flag seam: a bolt over a far-depth quad survives the write-back at `bolt_far_show` as over space
(the existing seam case, one more draw); (5) the budget of section 6 at 512 bolts; (6) the tuning parser on bounds,
garbage and a missing file; (7) the suppression: a dropped batch is one of an accepted body, an unaccepted body's is
forwarded byte for byte, an unowned frame forwards all. What only the user decides, from a screenshot or F8 capture
in flight: whether the bolt reads as an IRE shot (thin, bright, the colour, the dash length at 1x and under SETA),
how it sits beside the game's muzzle blob (still the game's in this step), the brightness against a lit hull and
against the sun, and whether 2 px is "accurate" or already too thin at 5120x1440.

## 6. Performance budget

5120x1440, 512 bolts (the tap's cap), one draw. Quad per bolt: (stretch + 6σ) x 6σ px: 1.2 kpx at 500 m, 7.6 kpx at
200 m; 32 near + 480 far ≈ 0.8 Mpx = 0.11 screens [I]. Pixel program about 40 slots (distance to a segment, two
exponentials, the colour lerps, the flag write), vertex program about 60 [I]: ≈ 5 µs by the 1 µs per slot per screen rule,
the archive's 673-slot capsule measured 89.8 µs fenced [M]. **Bound: 50 µs GPU fenced wall clock** (draw plus fence,
the timestamp figure recorded beside it). CPU per bolt: one 64 B record read, the view-volume test (six compares), two
projections (head, tail), four vertices of 48 B into the DISCARD buffer (96 KB sequential write), no call out of the
DLL, no allocation, no per-frame key lookup, no previous-frame matching (the stretch comes from v Δt); ≈ 0.15 µs [I],
**bound 100 µs at 512** on `stage_us`. The dropped batch saves the game's draw and the footprint rewrite for that body.
The far-flag composite is already paid by the game's bolts. Nothing in the hook changes: the tap costs what it costs today.

## 7. User decisions (2026-11-08)

The user chose mock **B** (thin core with the motion stretch) and answered the four questions:

1. Colour: the texture's orange (sRGB (255, 177, 92)), not the vanilla streak's red and not the FL light colour.
2. Length at 1x: the per-frame motion stretch only (B); the game's dart length is not used as the body.
3. The game's muzzle blob stays until the bolts are accepted.
4. The flown `fov` is 90-100 with the mod's remapped field of view, so the stage reads the live projection (the
   per-frame view and projection the proxy already captures) and sets the core width in pixels from it; it never
   assumes 90 or any fixed focal length. The 500 m reference is a reference for the tuning numbers, not a constant
   in the shader.

## Implementation (2026-11-08)

FL only (`X3M_EDITION_FL`); the AP build installs no new hook and creates no projectile program. Built as the
**fighter-tracer family's stage with the IRE as its only enabled row**, not as IRE code.

**Where things live.** `src/proxy/weapon_projectile_family.h` (the families: shape, mount tier, the enabled TBullets ids,
the seven-number `Tune`; `resolve_rows`), `weapon_effects_projectile.h` (the game-thread pool and enabled-body set,
integer only), `weapon_projectile_stage.h` (the per-frame builder), `weapon_projectile_tune_core.h` /
`weapon_projectile_tune.{h,cpp}` (the dev file), `src/effects/weapon_projectile_{vs,ps}.hlsl` (one family-generic
program pair: every colour, width and stretch is per vertex), `WeaponEffectsPass::run_projectiles`
(`src/renderer/weapon_effects_pass.cpp`), the FL site row `batch` in `src/engine/fl/weapon_effects_sites_fl.h`, its stub
in `weapon_effects_hook.cpp`, the draw-hook drop and the frame in `motion_output_weapon_effects_inc.h`.

**Enable, family and mod-added weapons.** `families[]` in `weapon_projectile_family.h` holds one entry per family:
`fighter_tracer` = shape tracer (body elongation ≥ 4:1) and mount tier fighter, its `Tune` (the section 4 starting
values) and its enabled list `fighter_tracer_rows` = `{"SS_BULLET_IRE"}`. **That list is the one place a row is
enabled.** At table load (`weapon_effects.json`, now read with each row's `id` and `body` as FNV-1a hashes) a listed id
is enabled only when the table classifies its row into that family (else it is counted `refused` in the
`projectile_rows` row and stays the game's); every other row sharing its body follows (the batch is per body), so the
IRE brings rows 37–39. The first object tap that can read TBullets turns the rows into engine body ids (word `+0`,
movzx; at most 64 bodies, a 65536-bit set the stub and the draw hook test). A **mod-added weapon** (a new TBullets row
with its own body) needs no code: `tools/effects/weapon_effects_table.py` classifies it into a family automatically (shape
from its body's elongation, mount tier from the ship catalogue; `tools/effects/weapon_overrides.json` for exceptions),
and the stage draws it as soon as its id is in its family's list. PAC and HEPT are table rows added to
`fighter_tracer_rows`; a second family (`fighter_slug`) is a second `families[]` entry with its own list and `Tune`.

**Per-row look, resolved once per body.** Colour: the row's override tint, else its `texture_rgb`, sRGB → linear and
normalised to a largest channel of 1 (the IRE's (121, 82, 39) → (1.0, 0.44, 0.11) = sRGB (255, 177, 92)). Calibre k: the
row's override, else clamp(log10(d / 300) / 2, 0, 1) over TBullets `+0x44` (IRE 220 → 0). Speed, position, birth and end
are the live bullet's.

**Constants (the baked values, `Tune` of `fighter_tracer`):** core_px 2, glow_ratio 3, glow_gain 0.2,
hot_share 0.3, lod_dot_m 3000, length_cap_m 30 (energy-conserving since FL 59 A), stops 2; the `stretch_k` multiplier
was removed at FL 59 A (it had been 1.0, then 0.4). stretch_k and length_cap_m were 1.0 and 116 (the section 4
starting values) until the fixture's look frames: a 48 px dash at 500 m at 1x and full-cap beams of 287–857 px under
SETA 6, the bulk the user rejected (coordinator, 2026-11-08). Fixed: reference distance 500 m (`reference_m`, the
tuning numbers' reference, never in the program), 5 view units per metre, glow σ floor 1.5 px (its radiance scaled by
(σ / 1.5)²), the quad's reach where both Gaussians fall below 1/256 of scene white plus 0.5 px, the far flag
K = 32, pool 512 entries / 1024 slots / 32 probes, 64 bodies.

**Per frame.** Present latches `skip_on` from the frame's stage (armed, the resolve completed, the projectile draw ran:
drew or had nothing to draw) and the resolved bodies. The batch stub (`0x097af8`) skips an enabled body's batch under
the latch; the draw hook drops a material-submit draw whose object-trace node is pooled and whose body id (one bounded
read) is enabled; without the scope those draws are counted `projectile_unhandled` (frames). On every 60th frame and
every capture frame a scoped draw whose node is not pooled is read too, and one of an enabled body is counted
`projectile_unpooled` (a child node, or a bullet the pool missed). The object tap stamps each entry with the game ms;
an entry not tapped for max(250 ms, three game steps) retires (absent from the update pass means not drawn), as do an
expired, recycled or unreadable one, and Present empties the pool on a load or a sector change. A table whose TBullets
row count is not the engine's resolves no body (`projectile_rows state=stale`, the stage stays off). At the resolve the
stage walks the pool (one bounded node read per entry), builds one quad
per bolt from the jittered live projection and the eye in engine units (the adjugate inverse of the view basis, so the
per-bolt math is float without the far-from-origin error), and draws all of them in one `DrawIndexedPrimitiveUP`
(48-byte vertices: float32 position and local frame, float4 radiance scales and 1/(2σ²), two D3DCOLOR tints; a
FLOAT16 attribute measured five times the submit cost natively) into RT0 and the lane at RT1 (COLORWRITEENABLE1 GREEN)
with the scene's depth surface captured at the resolve, LESSEQUAL, Z writes off, ONE/ONE. The pixel program is 16 ps_3_0
slots. Capabilities checked at attach with their own verdict (`projectile_device` row; a refusal keeps the shield
effect and the game's bolts): two simultaneous targets, independent write masks, MRT post-pixel-shader blending,
LESSEQUAL, the primitive limits, blending of a four-lane lane format, and per frame the lane's format and the MRT bit-depth
rule, the depth surface's size and multisampling (`projectile_state` row on a change of verdict).

**Rows.** `projectile_site state=claimed|refused|rollback rva=097af8` once per session; `projectile_rows
family=fighter_tracer rows=0,37,38,39 refused= shared=` once; `projectile_device` per attach; `projectile_state` per
change; the `weapon_effects_frame` row (every 300 frames) gains `projectile_owned`, `_skipped` (batch submits),
`_dropped` (traversal draws), `_unhandled`, `_bolts`, `_tracked`, `_retired`, `_overflow`, `_recycled`, `_bodies`,
`_us_max`, `_unpooled` and `_sampled` (the frames that looked for unpooled draws).

**The far flag, what is and is not verified.** The fixture verifies the write: the lane's green lane takes 32 max(rgb)
at the bolt and nothing else changes. It does not run the stage through `TemporalPass` and the write-back, so the
seam case of section 5 item 4 (a bolt over a far-depth quad surviving the write-back at `bolt_far_show`) is not
verified for the stage's bolts. One constraint carries over from the game's bolts unchanged: the resolve reads the flag
only on the camera-gate path, where its s1 is the current lane (`temporal_pass.cpp`, the resolve's `SetTexture(1, camera
? in.current_depth : depths_[next])`); on the other path the lane was copied into `depths_[next]` before the stage
callback ran, so a flag the stage writes is not seen there and the bolt goes through the resolve as scene colour.

**Tuning protocol (dev builds; the user, FL run 59 onwards).** Create `C:\X3FL\x3m\projectile_tune.json` while the
game runs, for example `{"core_px": 2.0, "glow_ratio": 3.0, "glow_gain": 0.2}`; save it; within a second the log shows
`projectile_tune reloaded … values=…` and the next frame uses them. Change one number at a time, fly a burst at 1x and
under SETA, and report the seven values from the last `reloaded` row with a screenshot or an F8 capture. A clamped
value logs which key and its bounds; a malformed file is refused and the last values stay. When the user accepts a
set, it is baked into `families[fighter_tracer].tune` and the file deleted; a release build never reads it.

**Verified (native fixture, ledger "Projectile IRE on FL (2026-11-08)").** The width rule within 0.5 px at 200 m / 500 m /
1 km / 3 km at two fields of view, the stretch rule and SETA invariance from the constants, the far flag's write (not the
write-back, above), the dot, nothing outside the quad, depth occlusion by a partial occluder (300 m drawn, 500 m hidden
behind the 400 m depth), the pool's retire paths (recycled, unseen, expired), Reset, the 512-bolt budget at 5120x1440 (CPU 67.6 µs, GPU fenced 32.0 µs [M]). Not flown.
Look frames (`run_weapon_effects.py --host native --dump DIR`, the fixture's projection, linear light clamped and
sRGB-encoded as the mocks, no bloom, AgX or TAA), cropped to 800 px: [near](../../verification/results/projectile-look/fixture-ire-near.png) (200 m / 500 m / 1 km at a 16.7 ms step: 56 / 19 /
8 px dashes, cores 5 / 2 / 1 px), [seta](../../verification/results/projectile-look/fixture-ire-seta.png) (the same at
100 ms: every stretch at the 30 m cap, 215 / 74 / 31 px), [lod](../../verification/results/projectile-look/fixture-ire-lod.png)
(3 km, exactly at `lod_dot_m` so still a 3.5 px dash at a third of the radiance, beside 300 m at 29 px). The bolts
move at 0.8 of their speed across the view, so the drawn lengths are 0.8 of the stretch's.

## 8. Options considered and why they lose

- **Draw over the game's bolt** (keep the batch, add the core). The vanilla streak stays as the body: two bodies, the
  bulk the user rejected, and no "energy as light" while a screen-blended sprite sits under it.
- **Replace the whole late bullet draw** (the archive's rule). Every body vanishes at once, so PAC, HEPT and the beams
  would need provisional bodies on the IRE's first flight; it breaks "one gun flown and accepted before the next".
- **Per-instance replacement inside the game's buffer** (collapse matched instances in the footprint's substitute
  buffer). Needs instance-to-record matching every frame (centroid search, 50 x 512) and a tolerance for the update
  pass's offset; the per-body rule gets the same result from one draw's identity.
- **A world width from the bullet size columns** (16/17, "accurate" by data). Unit unknown, and a world width floored at
  3 px is the archive's law that made far bolts fat; a pixel figure at a reference distance with a dimming floor is what
  the user asked for.
- **The archive's 3 px body floor and 24 px length floor.** Floors raise size; the user wants smaller, accurate bolts.
  Here the floor is 1 px with the radiance scaled down.
- **Mock C as the body** (a wide drawn glow). The balloon of the archive's re-review; the bloom chain supplies the wide
  halo from the core's energy, so a drawn halo doubles it.
- **A reprojected screen streak to the previous position** (the archive's 2.3). Handles camera turns but needs a
  per-frame key lookup and the previous view-projection; the velocity stretch is the bullet's own motion and costs
  nothing. Camera-turn smear can return if the user misses it.
- **A short fading trail on the IRE.** A 6.7 shots/s stream at 1,162 m/s with a trail is a beam; trails are for slugs
  and kinetic rounds, decided in their family.
- **A multiply rim over lit hulls** (the archive's 2.1). A dark outline is the decal the user named; readability over a
  hull comes from +2 stops against a hull of 0.2–1.5 linear.
- **Shipping the tuning file as a user setting.** The project does not expose look tuning; the values are baked.

## 9. Unknowns and what settles each

| Unknown | Settles it |
| --- | --- |
| The unit of node `+0x100` (velocity) | the observe flight's bolt rows: the position delta over two passes against `vel` x step; or the tap fixture |
| The engine's drawn bolt length and width (0.1 x speed x node scale [I]; cols 16/17's unit) | the footprint path's per-instance half-extents (A, B in px) logged per body for one flight, against the record's distance; or the decompile of `0x004411f7` / `0x0047f0a0` (AP) and the FL counterparts |
| How a bullet draw's body is identified at the hook (the batch record's body id at the draw, or the body's UV set in the staged prefix) | the disassembly of `0x0046d080`'s per-batch loop up to its draw call, FL `view-submit` counterpart; the UV words of `bullet_ls_alpha` from `bullet_uv_periods_out.txt` |
| Whether the game's batch for a body also carries other bodies' rows (a mixed batch) | a capture's bullet draws per frame against the bodies in flight (the single-copy script's pairing) |
| The far-flag composite natively in a fight (built under Wine 2026-09-26, fixture-run natively since) | the IRE flight's F8 capture over a station |
| The screenshot's scale (crop or downscale) | the user's answer; a full-frame F8 capture makes it moot |

## Reproduce

```sh
python3 verification/results/projectile-look/render_mocks.py      # mock-{thin,stretched,glow}.png, mocks_summary.json
python3 -I -c "import json; t=json.load(open('/mnt/c/X3FL/x3m/weapon_effects.json'))['tables']['bullets']['entries']; print(t[0]['texture_rgb'], t[0]['shape'], t[0]['mount_tier'])"
```
