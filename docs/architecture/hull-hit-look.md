# Hull hit: the game's own impact effect, re-shaded in place

Design note, 2026-11-13, for the next FL weapon family under `weapon_effects = reshade`: the impact effect the engine
spawns at a hit the shield stage did not take (hull, drained shield, unowned frame, missile impact). Inputs:
[fl/hull-hit-draw.md](../reverse-engineering/fl/hull-hit-draw.md) (rows, bodies, spawn, draw, constants), the bullet
and muzzle re-shades ([projectile-look.md](projectile-look.md), [muzzle-flash-look.md](muzzle-flash-look.md),
`src/proxy/bullet_reshade_*.h`, `muzzle_reshade_*.h`), the lab textures under
`verification/results/weapon-effects-lab/fl/tex/` (untracked). Marks: [M] measured in this session, [S] static (code,
data or note read), [I] inferred, [D] user decision, [U] unknown. Implemented as step 1 (section "Implementation"); nothing here has run in the game.

**Decision.** One luminance law in the pixel shader, the bullets' accepted fire law extended by a visible fade and a
cooling term, applied by body id to 25 of the 28 impact bodies through two thin programs (ps_2_0 behind the effects
DEFAULT pair, ps_3_0 behind the standard_lighting DEFAULT pair) and the muzzle's two-call device sequence. The three
bodies that are also death explosions (v/10656, 10664, 10670) stay vanilla until the explosions family. Four dev keys
`hh_gain`, `hh_edge`, `hh_core`, `hh_cool`. The first flown step is the whole set at gain 1.0, edge 0.4, core 1.0,
cool 0.3 (the gain and core raised from the bullets' 0.75 / 0.5 after the fixture sweep, section "Implementation").

## 1. What vanilla draws and what reads wrong (question 1)

The impact is the weapon's Effects row, one row for every target, each element a separate draw of a small body at the
contact point, riding the hit object [S]. Three body classes carry 22 of the 45 weapons on their own:

| Class | Bodies | What the texels are [M, lab textures] | Vanilla on screen [S, I] |
| --- | --- | --- | --- |
| Sparks flipbook | v/10663 (`exp_sparks1/2_diff`, 4 × 4 cells, 32 × 25 ms) | sparse deep-orange points, hue (0.435, 0.168, 0.018); cell mean luminance 0.010–0.017, cell peak 0.94 in frames 1–3 falling to 0.26 by frame 15; alpha mean 0.01 (the points only) | a compact orange ball for 75 ms, then orange dust spreading over the cell; GROW 0.04 → 0.06, random roll; the FADEOUT is invisible (standard_lighting pair, ONE / ONE), so it ends at the atlas's last, dimmest frame |
| Glow quads | v/10658 `exp_impact_glow` (neutral, peak 0.97, 31 % of texels lit); v/10661 `exp_glow` (orange-brown, **peak luminance 0.157**); v/10664 `exp_glow2` (peak 0.083) | a soft disc with a flat plateau core; a haze; a fainter haze | 10658: a flat grey-white disc that flickers (rnd 0.3) and pops out at 500 ms; 10661 (the IRE's second element): invisible in practice; both fades invisible |
| Classic meshes | v/00587 and 00469 on `tex/true/380`, 00564 on 383, 00201 on 376, 00589 on 131 | 380 is an atlas of lightning streaks, plasma, nebula blobs, a starburst and white flames (mean 0.14, p99 0.996, near-neutral hue); which cells 587's ten quads map is [U] | crossed quads aligned to the hit object's axes (not the camera), ONE / ONE, 260–560 ms, size flicker 0.1; FADEOUT invisible |
| Effects flipbooks | v/12000–12008, 10656, 10670 (effects DEFAULT pair) | explosion atlases (`fx_blueexplosion`, `fx_electric`, `fx_exp_A/B`, …) | the fade reaches the frame here (RGB × g_AlphaValue); the colour matrix c0..c2 applies |

Under HDR today every ONE / ONE draw of both pairs goes through the hull-emission route's saturating clamp
(`emission_source_clamp` 0.7: the muzzle note's finding for the same pair, [I] for the effects pair from FL 67's white
lone bolts), so no impact sprite exceeds 0.7 of scene white and the sum of overlapping elements saturates flat: the
glow disc is a grey cap, the sparks' hot first frames are capped, nothing blooms. The standard_lighting bodies end by
popping, not fading. The sparks are one or two texels wide beyond 100 m, which the TAA's history blend thins further
[I, the bolts' experience]. The ledger holds no user judgement of the vanilla FL hull hit itself [S, grep]; the user's
words on hull splashes were about our custom stage ("lazy flashes … a downgrade", "sparks read as the same decals
appearing and disappearing", Run240; "wrong in kind", Run246) and about fire in general ("blazing core + rays + dense
sparks, not lava/beige"). Vanilla's own weaknesses are therefore inferred from the textures and the draw, not flown.

**The law.** One program body, the bullets' accepted law (`weapon_bullet_reshade.hlsl`, FL 64/68 A) with two terms
added, per texel t of the game's own texture, l = saturate(luminance(t) × normaliser):

| Term | Form | Why |
| --- | --- | --- |
| body | `hh_gain × t.rgb × smoothstep(0, hh_edge, l)` | the dim fringe fades smoothly to black: no cap, no grey plateau; the texel's own hue, so the sparks stay orange, the HEPT green, the glow disc white |
| hot core | `hh_core × l⁴ × lerp(white, hue(t), 0.35)` | the plateau of the glow disc, the first sparks frames and the atlas's bright cells reach scene white and bloom; dust and haze do not |
| normaliser | per body, `luminance_reference / the texture's p99` (the bullets' rule, `texture_scale`) | the dim `exp_glow` (p99 0.092) is lifted to the same 0..1 the sparks (p99 0.202) and the glow disc (0.937) work in: one value set for every body |
| fade | `× f`, f the node fade (COLOR0.x on the effects pair, COLOR0.a on standard_lighting) | the engine's 254 → 1 ramp becomes visible on the ONE / ONE standard_lighting sprites: they fade instead of popping; the effects pair already did this |
| cooling | `rgb = lerp(rgb, rgb × (1, 0.35, 0.1), hh_cool × (1 − f))` on the body term only | FADEOUT elements cool white → orange → red over their life, the fire reference; a non-FADEOUT element has f = 1 and is untouched |
| cell envelope | flipbook bodies only: `× (1 − smoothstep(0.85, 1, 2·max(|q − ½|)))`, q = frac(4·uv) | the boosted late sparks reach the cell's edge; the box metric keeps the corners, so the pattern is the game's |
| alpha | vanilla's: the texel's (effects), `lerp(t.a, light.a, g_EnableGlow) × COLOR0.a` (standard_lighting) | the game's alpha test and destination alpha see what they saw |

Grain stays the texture's: no smoothing key (the muzzle's `mz_smooth` existed because its texel was a modulator of a
procedural glow; here the texel is the shape). The vanilla vertex light sum (COLOR0.rgb on standard_lighting, point
lights + white emissive) is not drawn, as in the lone classic program: the LIGHT elements still light the hull.

Two caveats on f. On FADEOUT elements f is also multiplied by the fog factor when the material's fog is on [S], so in
a fogged sector every hit would read as late-life and cooled; whether FL's impact materials enable fog is [U] (the
flight's log counts `cool_active=` draws whose f < 1 against FADEOUT bodies). Under SETA the ramp runs in game time,
so an 800 ms element lives 5–8 rendered frames at SETA 6–10 [D, the user flies with SETA]; the law has no temporal
term, so nothing depends on frame continuity.

## 2. Recognition and the shared bodies (question 2)

Recognition is the muzzle's, by body id in the FL object-trace scope's node (`+0x144`), gated by the bound VS hash
(the ABI) and the blend (ADD, SRCBLEND ONE, DESTBLEND ONE or INVSRCCOLOR). No new hook: the seam `0x0e7142` and its
contract are already in use [S]. The body scan runs after the existing claimants on the same node read, so one draw
has one claimant: on the standard_lighting VS the lone classic bullet map, then the muzzle's seven, then the hull-hit
list; on the effects VS the lone bullet map (which holds `bullet_ElectroSpike`, the ESR's impact body), then the list.

**Shared bodies.** The law is a luminance law: it keeps the texel's hue and pattern and the engine's size, life and
blend, so a shared body drawn by another row gets a brighter core, a softer fringe and a visible fade, not a
different effect. Three kinds of sharing [S, census]:

| Bodies | Shared with | Verdict for the first step |
| --- | --- | --- |
| v/10661 | missile impact / death row 158; unnamed rows 36, 41, 142 | **in**: the missile impact is this family (the same impact call, TMissiles col 20) |
| v/00587, 00564 | the IPG and IONC muzzle rows 179, 181; unnamed 187 | **in, accepted collision**: these muzzles are vanilla today (the muzzle route's seven bodies hold neither), so they gain the hull-hit law at the gun and will draw brighter than the re-shaded IRE flash; two Terran weapons the user rarely flies; the flight log names their draws |
| v/10656, 10664, 10670 | ship and station deaths 37, 40, 48, 201 | **out**: a death explosion re-shaded under the hull-hit step would be judged with the wrong family; the explosions step claims them with its own law |
| v/00589, 10388, 10667 | unnamed rows 55, 46, 187, 153 (spawner [U]) | in: no known non-impact spawner; a body row in the log shows a count that disagrees with the hits |

No per-draw discriminator survives the facts: g_AlphaValue is the same ramp on any FADEOUT element, the drawn scale
(g_mWorld's z column, which the muzzle route already reads) is the element's size curve, which a death row may share,
and the node's parent is the same `+0x1c` sibling slot for impacts and, presumably, deaths [U]. So the scope is the
body id, and the split is by family ownership, not by draw. That leaves **25 bodies** and covers 39 of the 45 weapons
fully; the five flak rows (155, 156 → 147) and the IBL (198) keep one vanilla element (10664 / 10656 / 10670) beside
their re-shaded ones. The two ONE / INVSRCCOLOR bodies (v/10659, the HEPT's splash; v/12007) are re-shaded under their
own blend (the screen sum cannot exceed scene white, which suits a green splash); v/10653's blend is [U] and is
admitted only when the shadow shows ONE / ONE.

## 3. Program, constants and the table (question 3)

**Two programs, one law, forced by the ABI** [S]: the effects DEFAULT VS `d5e1c753…` is vs_2_0 (253 dwords), so its
partner must be ps_2_0 (as the lone bullet program); the standard_lighting VS `494fe349…` is vs_3_0, so its partner is
ps_3_0 (as the muzzle and lone classic programs). The law lives in one include (`weapon_hull_reshade.hlsl`) with two
wrappers: `weapon_hull_reshade_effects_ps.hlsl` (COLOR0.x fade, TEXCOORD0 UV, texel alpha) and
`weapon_hull_reshade_classic_ps.hlsl` (COLOR0.a fade, centroid TEXCOORD0, the light-map alpha lane), exactly the
split `weapon_bullet_reshade_lone_ps` / `_classic_ps` already has. Both read the UV after `g_TexMatrix`, so the cell
coordinate is the same in both. Each is created once per device at its VS's registration and survives Reset; a
creation or restore failure turns that pair's re-shade off with one row, the other pair unaffected.

**Constant block**, the same two registers' content in both, at the registers each pair's shadow already keeps so no
new shadow is needed: c30..c31 on ps_2_0 (inside the lone bullet block c29..c31, put back from `ps_c29` after the
draw) and c192..c193 on ps_3_0 (inside the muzzle block, put back from `ps_c192`):

| Register | x | y | z | w |
| --- | --- | --- | --- | --- |
| first | `hh_gain` | `hh_edge` | `hh_core` | the body's luminance normaliser |
| second | `hh_cool` | cells (4 for a flipbook body, 0: no envelope) | 0 | 0 |

The device sequence is `muzzle_reshade_route.h`'s shape (constants then program, all or nothing; end: the game's
program, the application's block): no state writes in the first step. The engine already sets Z write off for every
fading node; the few non-FADEOUT elements keep whatever vanilla had, and the frame row counts substituted draws seen
with Z write on (`zwrite_on=`) so the ledger can say whether the lone path's normalisation is needed. A substituted
ONE / ONE draw skips the hull-emission route as the muzzle's does (the clamp would cap the core at 0.7); a refused
begin leaves the draw to it. No far flag in the first step: the sprites ride the hull, which is in the depth lane; if a
far station's hits dim as FL 67's bolts did, the bullet route's `flag_bind` and lone flag program are the remedy.

**Per-body table.** What a body needs is its texture's p99 and whether its UV is a 4 × 4 cell: two numbers, fixed per
edition for 25 bodies. A constexpr `hull_reshade_core.h bodies[]` (id, normaliser, cells, blend admitted), emitted by
`verification/results/fl-hull-hit-draw/hull_hit_bodies.py` from the census and the lab textures (the sparks pages
differ by 2 % in p99, so the first page's value serves), its output text tracked and a host test holding the header to
it. The muzzle precedent, not the bullets' JSON table: the bullets needed the live TBullets rows to map mod bodies and a
per-texture hash match at texture creation; impact bodies are not named by TBullets, and a mod's own impact body simply
stays vanilla. The table generator TODO already on the ledger (Effects → CUT scene → part bodies) would later fold both
the muzzle's and this list into `weapon_effects.json`; not a prerequisite.

Measured inputs for the table so far [M, this session's one-liner over the lab PNGs, luminance 0.2126/0.7152/0.0722,
p99 over all level-0 texels]: 380 0.996, 383 0.704, 131 0.970, 376 0.721, `exp_sparks1` 0.202 (`exp_sparks2` 0.207),
`exp_glow` 0.092, `exp_glow2` 0.076, `exp_impact_glow` 0.937, `exp_PL_imp` 0.468, `exp_small` 0.973. The effects
flipbooks (`fx_blueexplosion_01..03`, `fx_electric_01..03`, `fx_exp_A_p1..`) are in the lab set and get theirs from the
script. With `luminance_reference` 1.696 (the bullets' anchor) the sparks' normaliser is 8.4 and `exp_glow`'s 18.4:
every spark point saturates l and takes the core term, which is the intended "dense, hot" reading of a sparse texture.

**Hot-path cost** [I from the measured siblings]: a blended candidate draw on either VS pays the node read it already
pays for the bullet or muzzle match plus a scan of at most 25 ids after their misses (the lone scan of 30 ids measured
0.013–0.015 µs); a substituted draw pays the two-call sequence (the muzzle's measured 0.195–0.35 µs per draw) and the
one-row block copy. An impact is 1–5 draws per frame while alive; a busy fight with 40 live hits is under 60 µs of
calls per frame. No allocation, no per-frame branch, nothing on an opaque draw.

**Native Windows.** Documented `IDirect3DDevice9` calls only (SetPixelShader, Set/GetPixelShaderConstantF); the
programs pass the native runtime's validator in the fixture, which runs on this host's Windows D3D9. In-game behaviour
is unverified on any platform until flown; AP keeps the impact vanilla (`edition=ap`, as the muzzle).

## 4. Fixture and judging (question 4)

A `hull` dump case beside `reshade` and `muzzle` in `weapon_effects_projectile_dump.cpp` /
`run_weapon_effects.py --dump-case hull`, real assets where the lab and the catalogues have them (`assets=game`:
v/10663's quad with `exp_sparks1/2`, v/10658 with `exp_impact_glow`, v/10661 with `exp_glow`, v/00587's mesh through
the census reader with 380, v/12000 with `fx_blueexplosion_01`), procedural stand-ins otherwise. Frames, vanilla
beside re-shade, 1920 × 1080 FP16, the game's blend, a hull plate behind the sprite at 60 % grey so the fade and the
screen blend read:

| Frame | Element, state | What it must show |
| --- | --- | --- |
| `sparks-{f02,f08,f16,f28}` at 50 m | 10663, row 15's GROW, cells 2 / 8 / 16 (page switch) / 28, f = 0.94 / 0.75 / 0.5 / 0.12 | a white-orange ball at f02, hot points at f08, cooled thin sparks fading at f28, no cell edge |
| `sparks-far` at 300 m, cell 8 | the same | still visible points, not a blur |
| `ire-{100ms,400ms}` | 10663 + 10661 together, row 15's two elements, f per element | the composite the user sees on an IRE hit |
| `glow-peak`, `glow-late` | 10658 at the GROWANDSHRINK peak and at 450 ms (f = 1: non-FADEOUT), flicker off | a white core that blooms, a soft edge, no grey cap |
| `mesh-{090,050,010}` at 50 m | 587 on 380, row 53's size at 25° to the hull axes, f = 0.9 / 0.5 / 0.1 | the PPC's starburst with a hot centre, visibly fading and reddening |
| `hept` | 10659 under ONE / INVSRCCOLOR over the plate | a green splash that reads as light, not a decal |

Binding gates (fixture `hull_reshade_*` rows and the dump's `DUMP_HULL_GATE`): both programs equal the CPU model
(`hull_reshade_core.h model()`) within the fixture's tolerance at 11 points per variant including cell edges and
f ∈ {1, 0.5, 0.1}; recognition takes the 25 bodies on their pair and refuses the muzzle's seven, a bullet body and the
three death bodies (one claimant); the sequence restores program and block (readback); **fade visible**: a
standard_lighting body's dark sum at f = 0.1 is at most 0.15 × its sum at f = 1 (vanilla: 1.0); **no cell edge**: the
outer 2 % of the sparks cell at frame 28 holds no texel above 0.02 of the frame's peak; **hot core present**: the
sparks' f02 and the glow peak reach scene white at 50 m at the defaults (peak ≥ 1.0 linear); cost under 10 µs per
substituted draw as the lone classic's gate. Reported, not gated: the energy ratio to vanilla per frame (the user sets
brightness in flight) and the extent at 0.1 linear. The frames are viewed (the Read tool on the PNGs, and the lab's
Hull tab in headless Chromium once a stage view exists for them) before the flight is dispatched; a gate pass alone has
missed the lamp before [D].

## 5. The first flown step (question 5)

**Bodies**: the 25 of section 2 (every census impact body but v/10656, 10664, 10670; `bullet_ElectroSpike` stays the
bullet route's). **Keys** in `projectile_tune.json` (dev builds; the same reader, bounds clamped and logged):

| Key | Default | Bounds | Meaning |
| --- | --- | --- | --- |
| `hh_gain` | 1.0 | 0–2 | the body term's gain over the game's radiance (designed at the bullets' 0.75; see "Implementation") |
| `hh_edge` | 0.4 | 0.05–1 | the luminance smoothstep's upper edge |
| `hh_core` | 1.0 | 0–4 | the hot core's gain (l⁴) (designed at 0.5) |
| `hh_cool` | 0.3 | 0–1 | how far a FADEOUT element reddens by the end of its life (0: fade only) |

Designed to start at the bolts' values because the user accepted that law on the same textures' kind of fire; the
fixture sweep moved gain and core to 1.0 (section "Implementation"); the flight tunes.
**What the user judges** (SETA on as usual): an IRE burst on a fighter's hull at 50–200 m (hot sparks that fade, no
popping, an orange glow around them); a PPC on a capital's hull (the 587 starburst and the 564 flash with a white
centre, no lamp, no beige); a HEPT splash; a Kyon or Repair beam's white disc plus sparks; one hit on a station at
1–2 km (does it dim); and whether the IPG or IONC muzzle, if seen, looks wrong. **Log rows**: `hull_reshade_program`
at creation per pair (`created= words= bodies=`); `hull_reshade_frame` every 300 frames (`candidates= matched=
effects= classic= failures= zwrite_on= cool_active= shared= us_per_draw=`); `hull_reshade_body body= pair= blend=
draws=` for the first 16 bodies seen; `projectile_tune reloaded` carrying the four `hh_*` keys. Acceptance of the step:
the user's verdict plus the frame rows showing substitutions on both pairs with `failures=0`; then the values bake into
`weapon_projectile_family.h` and the keys stay for the next family.

## 6. Options considered and why they lose

- **Procedural sparks inside the quad** (replace the texel by noise points from `frac(4·uv)` and the frame index):
  denser sparks than the atlas offers, but it is a new effect wearing the game's quad, judged on the fixture's taste;
  the user's rule is re-shade, not replace, and the first step must be the game's own pattern. Open as a later step if
  the flown sparks read as too few.
- **A world-space law like the muzzle's** (a glow around the contact point from g_mWorld): the impact bodies are
  single quads and small crossed meshes whose texel already is the shape; a radial envelope would erase the sparks and
  the starburst into the "beige lobe" of FL 64. The envelope survives only as the flipbook's edge guard.
- **One ps_3_0 program for both pairs**: not legal; the effects VS is vs_2_0.
- **Per-body hue and radius constants** (the muzzle's table): nothing to measure; the texel's hue is the game's and
  the quad's extent is the engine's size curve. The table stays two numbers per body.
- **The JSON table with live-row matching**: no TBullets name for impact bodies, so no mod mapping to gain; the
  constexpr list ships the same facts without the texture-hash path.
- **Excluding v/00587 and 00564 for the muzzle collision**: drops the main element of 11 weapons (EMP, PAL, PPC,
  CIG, IONC, …); the collision costs two rare muzzles a brighter flash.
- **Including the death bodies**: would re-shade station and ship explosions under a hull-hit flight; the explosions
  family owns them.
- **The additive route's conversion for ONE / INVSRCCOLOR** (the lone bullets' DESTBLEND ONE + emission gain and
  state normalisation): more machinery than two bodies justify; a screen-blended splash is correct at scene white.

## 7. Unknown, and what settles it

- Which cells of `tex/true/380` v/00587's quads map (the starburst, the flames or a streak): the census reader on
  `objects/v/00587.bod` as `muzzle_assets` does for 445; needed for the dump frame, not for the law.
- Whether FL's impact materials enable fog (f = fade × fog): `cool_active=` against FADEOUT draws in a clear sector,
  or the material files' `g_EnableFog` in the catalogues.
- The depth state of NOZTEST elements and the blend of v/10653: the flight's `zwrite_on=` and body rows.
- Whether a far station's hits are dimmed by the TAA far hold: one station under fire at 1–2 km in the flight.
- The IPG/IONC muzzle collision's visual cost: only in flight.
- Runtime confirmation of the whole draw contract (nothing here has run in the game).

## Implementation (2026-11-13, step 1, not flown)

What shipped, against sections 2 to 5 [M unless marked]:

- **Table** (`src/proxy/hull_reshade_core.h` `bodies[]`, emitted by
  [`hull_hit_bodies.py`](../../verification/results/fl-hull-hit-draw/hull_hit_bodies.py), output beside it, held to the
  header by `verification/analysis/test_weapon_hull_reshade.py`): **24 rows**, the 28 census bodies minus v/10656, 10664,
  10670 and `bullet_ElectroSpike` (the note's 25 counted the ESR bolt, which stays the bullet route's), most frequent first
  (587, 10663, 469, 564, 12000, 10658, ...). The script reproduces every texture figure of section 3 (380 0.996 ... exp_small
  0.973). **One deviation from section 3: the normaliser's statistic.** The design's p99 over every texel drew the sparks
  f02 frame as a flat beige disc: on a sparse texture (8 % of exp_sparks texels lit) that p99 (0.202) lies in the orange
  haze, which then saturates l and takes the whitish core term. The table uses the p99 of the **lit** texels (luminance
  > 0.02; sparks 0.558, normaliser 3.04 instead of 8.4; the other textures move +0 to +4 % where the all-texel p99 was
  already >= 0.93, +10 % on 376, +15 % on the shockwave and the green explosion, +28.5 % on 383, and 1.8-4x on the sparse
  sparks, electric and implosion pages; per body in the generator's output), **floored at 0.5**: the IRE's
  `exp_glow` (lit p99 0.133) lifted to 0..1 still drew a grey-beige banded disc around the sparks in the ire-100ms frame;
  floored (normaliser 3.39, also v/10666, 10667, 10668, 10659) a haze keeps its hue under the body term and the fade.
  Flipbook flag `cells` 4 on the twelve 4 x 4 bodies; `blend` per census (10659, 12007 ONE / INVSRCCOLOR; 10653 ONE / ONE
  only); `shared` on 587, 564, 582, 589, 10388, 10661, 10667, 12001. AP: `body_count(edition_ap)` is 0, so the route is not
  requested there (edition-independent code; the FL ids never match on AP).
- **Defaults** (`hh_gain` 1.0, `hh_core` 1.0; `hh_edge` 0.4 and `hh_cool` 0.3 as designed): the dump was swept at
  gain / core 0.75 / 0.5, 1.0 / 1.0 and 1.25 / 1.5 and the frames viewed. At 0.75 / 0.5 the PPC mesh at f 0.5 has no hot
  centre and the IRE haze reads brown; at 1.0 / 1.0 the mesh keeps white-hot areas inside its rays, the IRE sparks are
  white-hot in an orange halo and the glow's white core stays moderate; at 1.25 / 1.5 the mesh and the glow fill into
  flat white over the plate. Keys alone do not make the IRE haze a saturated orange (it is the dim `exp_glow` under the
  0.5 floor) nor bring the HEPT ring back to green (the core whitens it): both are the flight's call. The glow's plateau
  clips (linear peak > 1) at every setting; in game it is bloom input, the dump has no tonemap.
- **The effects pair's colour matrix** (PS `8360f422…` applies a preshader grade in c0..c2 to the texel; our ps_2_0 does
  not): every effects-pair impact material (v/10659, 12000-12008) sets `Brightness` 1, `contrast` 1, `saturation` 1, `hue`
  0, `colormatrix` 0 [S data,
  [`effects_material_params.py`](../../verification/results/hull-reshade/effects_material_params.py)], a neutral grade,
  so the matrix the preshader builds is the identity [I: the preshader's output itself was not read]; the program keeps
  the texel ungraded, as the lone bullet program does.
- **Match** (`hull_reshade_core.h match`, the production and fixture decision): the VS (`d5e1c753…` effects pair, `494fe349…`
  standard_lighting pair) with that pair's program ready, ADD ONE / ONE or ONE / INVSRCCOLOR (a candidate from here), the
  node body id (`core::layout::node_body`), then refused for a bullet-map body (`bullet_body_layout`, the lone routes'
  claim) and a muzzle body, then the list, the body's pair and blend, and on the classic pair the edition's
  standard_lighting DEFAULT PS. MotionOutput runs it after the lone bullet and muzzle routes and only on draws none of
  them substituted (`route.bullet_reshade`, `route.muzzle_reshade`, and every earlier route flag).
- **Programs**: `weapon_hull_reshade.hlsl` (the law, registers by macro), `weapon_hull_reshade_effects_ps.hlsl` ps_2_0 258
  dwords (c30..c31, COLOR0.x), `weapon_hull_reshade_classic_ps.hlsl` ps_3_0 286 dwords (c192..c193, COLOR0.a, centroid UV,
  vanilla alpha lane); `tools/shaders/generate_hull_reshade_program.py`, records
  `verification/results/weapon-hull-reshade-{effects,classic}-pixel-program.json` (`--check` PASS). Created once per pair
  at its VS's registration, released at device loss; a creation failure leaves that pair vanilla with one
  `hull_reshade_program` row.
- **Sequence**: `muzzle_reshade_route.h` begin / end with the hull register per pair, the application's values from the
  existing `ps_c29` (+ one row) and `ps_c192` shadows; no state write, no far flag. A substituted draw skips the
  hull-emission route. The request needs the muzzle re-shade's (classic pair) and, for the effects pair, the bullet
  re-shade's (its `vs_lone` gate and `ps_c29` resync read).
- **Rows**: `hull_reshade_program` per pair (`reason=ok`, `create_failed`, or `bullet_reshade_unarmed` for the effects pair
  when the bullet re-shade is not armed); `hull_reshade_frame` every 300 frames in reshade mode (`candidates matched
  effects classic failures zwrite_on cool_active shared timed us_per_draw`; `cool_active` reads the application's
  g_AlphaValue, VS c12.x / c39.x, shadowed for this); `hull_reshade_body body= pair= blend= shared= draws=` once per body at
  the end of its first drawn frame (16 a device); `projectile_tune reloaded` carries `hh_gain hh_edge hh_core hh_cool`.
- **Fixture**: D3D `hull_reshade_*` (8 checks, `weapon_hull_reshade_cases.h`) and the dump `--dump-case hull`
  (`weapon_hull_reshade_dump.h`, frames and `dump_out.txt` under `verification/results/hull-reshade/`). Departures from
  section 4: the frames' sizes follow the muzzle dump's 500 units per metre over the bodies' +-100000 units [I], which
  makes the 587 mesh 10-90 m and the 10658 glow 40 m across, so the mesh frames sit at 300 m and the glow frames at 200 m;
  the vanilla classic panel draws the texel law (the game PS drew black under the fixture's material constants); added
  `blue-f05/f20` (v/12000, effects pair) and `fade-f100/f010` (v/10661, the fade gate's pair). The ledger
  ([weapon-effects.md](../verification/weapon-effects.md) "Hull hit re-shade, step 1") holds the numbers and what the
  frames show.

## Reproduce

```sh
# texture statistics for the table (lab assets untracked; PIL + numpy)
python3 -I - <<'EOF'
from PIL import Image; import numpy as np
d="verification/results/weapon-effects-lab/fl/tex/"
for n in ["380-64016930","exp_sparks1_diff-6d1ad779","exp_glow_diff-e6349fea","exp_impact_glow_diff-34ecebb7"]:
    a=np.asarray(Image.open(d+n+".png").convert("RGBA")).astype(np.float32)/255
    l=(a[...,:3]*[0.2126,0.7152,0.0722]).sum(-1); print(n, round(float(np.percentile(l,99)),4), round(float(l.max()),4))
EOF
python3 -I verification/results/fl-hull-hit-draw/fl_hull_hit_census.py   # the bodies, elements and flags
# once built: the hull dump and the fixture (native host, the Wine queue's lock)
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_weapon_effects.py --host native \
  --dump verification/results/hull-reshade --dump-case hull --lab-assets verification/results/weapon-effects-lab/fl
```
