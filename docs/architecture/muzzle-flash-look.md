# Muzzle flash: the game's own flash, re-shaded in place

Design note, 2026-11-09 (user decision of the day; implementation by the implement-deep agent). Edition: FL
(`C:\X3FL`) first; since 2026-11-13 AP too (section 5; one `weapon_effects_reshade edition=ap runs=reshade` row). Inputs:
[fl/muzzle-flash-draw.md](../reverse-engineering/fl/muzzle-flash-draw.md) (the flash body, its draw and state),
[fl/classic-materials.md](../reverse-engineering/fl/classic-materials.md) (the standard_lighting pair),
[fl/bullet-draw-batch.md](../reverse-engineering/fl/bullet-draw-batch.md) section 6 (the bullet re-shade this mirrors).
Marks: [M] measured, [I] inferred, [S] static (code, data or note read), [D] user decision, [U] unknown. The custom
flash this replaced (its anchor, birth match, hull pulse and five keys; removed 2026-11-13) is archived in
[muzzle-flash-look-custom.md](../archive/effects-look-2026-11-13-custom-projectile/muzzle-flash-look-custom.md).

**Mode.** `weapon_effects = reshade` covers the bullet and the muzzle flash; there is no new mode. In that mode the
game spawns, places, animates and times its own flash; only the flash draw's pixel program changes.

## 1. What the engine owns [S]

The muzzle site spawns Effects row 22 (IRE, PD, PSG, PBC, the fragments; also rows 157, 52, 190, 195, 202, 26, 54),
whose CUT element is scene 04251 with one part, **body 445**: 50 textured quads fanned forward from the gun (100
triangles), every quad mapping the whole top-left cell of `tex/true/384` (an orange lightning streak, no alpha). The
engine re-places the scene from the ship every update, scales it on the parabola `b − (b − a) t²` (a 0.001, b 0.031,
peak at 75 ms of 150) and destroys it at 150 ms. It draws the part through the traversal and the material submit
`0x0e7142 → 0x0e0f30`, inside the FL object-trace scope, with `standard_lighting(2s)` DEFAULT (VS `494fe349b8bc12ec`,
PS `0d5848c8489b6630` for the two-sided material, `ced7edef48059e84` one-sided), ADD ONE / ONE, Z test on, Z write off,
no cull. The vanilla colour is the material's emissive (Materials row 384: diffuse 150 × self illumination 100 / 25500
= 0.588 [I]) times the texel; there is no fade and no vertex colour. Position, life, scale, geometry, texture, blend and
depth stay the engine's.

The vanilla flash is a lamp because 50 quads add at the gun: on the fixture's real-mesh frame the vanilla peak is
**9.1 × scene white** at 30 m and 4.8 at 300 m, with a white region of 31.6 px equivalent radius at 30 m [M, dump below].

## 2. What we change

**Recognition** (one bool test per draw otherwise). In reshade mode, while the bound VS is `494fe349b8bc12ec`
(`shadow_.vs_muzzle`, set at SetVertexShader: the ABI gate, since our program reads that VS's interpolators and the
draw's origin comes from its constants) and our program is ready (after a creation or restore failure the pass is
off: no match, no count), outside state-block recording, a draw with ALPHABLENDENABLE on and SRCBLEND ONE, DESTBLEND
ONE, BLENDOP ADD (the shadowed states; blend-off classic bodies end here) and the VS rows it reads (c28..c30,
c34..c36) known asks the object-trace scope
for its node (TLS, no read) and reads node `+0x144` (4 bytes through `engine_memory::read`). A body id in
`muzzle_reshade_core.h bodies[]` marks the draw. The list is constant, every classic standard_lighting flash body of
the census (`verification/results/fl-muzzle-flash-draw/fl_muzzle_flash_census_out.txt`; MATERIAL3/5, Materials flags
DESTINATIONBLEND | 2SIDED, so the two-sided pair), most frequent first: **445** (scene 04251; IRE, PD, PSG, PBC, flak,
fragments, the secondary of PPC/PSP/MAM/CIG), **446** (04253; EMP, Mining, IPG, IonC, IBL, SSC, the primary of
PPC/PSP/MAM/CIG), **444** (04252; HEPT, PAL, Tri-Beam, Fusion Beam, EBC, Gauss), **450** (04260; PAC, Mass Driver),
**447** (04258; FBL), **314** (04261; Ion Disruptor), **10463** (04437; PBE), then **12009** (08001; PRG), the one
effects.fx flash body, drawn with the effects DEFAULT pair and re-shaded on its own path ("The effects-pair body"
below; its row carries `pair_effects` and DESTBLEND INVSRCCOLOR, and the classic route refuses it); scene 04265 is
absent from the catalogues. The route checks the VS hash
only, never the PS, so the one-sided and two-sided DEFAULT programs both pass. The table generator stops at the
Effects element's scene id (`tools/effects/weapon_effects_census.py`), so the Effects → CUT scene → part body chain is
a TODO in the ledger, not hidden. The shader alone never identifies the flash (the pair is every opaque classic body's).

**Substitution.** Our ps_3_0 program (`src/effects/weapon_muzzle_reshade_ps.hlsl`, 22 slots, 208 words) replaces the
game's program for that draw only, with the game's VS, geometry, texture, sampler and ONE / ONE blend, through one
device sequence (`src/proxy/muzzle_reshade_route.h`, used by MotionOutput and driven as is by the native fixture):
begin writes c192..c195 then binds our program, all or nothing; end binds the game's program (the shadowed binding)
and writes back the application's c192..c195 (the SetPixelShaderConstantF hook's shadow, read at resync). Documented
`IDirect3DDevice9` calls only; no allocation; the program is created once per device at the VS's registration (or its
first bind) and survives Reset. A failed restore stops the re-shade for the device with one row.

**The gun's origin.** The VS writes TEXCOORD1 = camera − world position (`o3 = c34..36.w − g_mWorld · pos` [S, the
VS's disassembly]). The draw's c192 holds camera − origin and the inverse glow radius, computed per draw on the CPU
(`draw_constants`) from the same VS's constants as the application wrote them: the origin is g_mWorld's translation
(c28..c30 .w), the camera g_mViewInverse's (c34..c36 .w), the world scale the length of g_mWorld's z axis (the node's
scale, here in the world units the VS uses). So P − origin = (camera − origin) − TEXCOORD1 needs no engine read and is
in exactly the VS's space. (The node's `+0x30` position and `+0x70` scale were not used: their units relative to the
VS's world space are not established, and the matrix is what the draw uses.)

**Route.** Under HDR the hull-emission route (`prepare_hull_gain`) would take this ONE / ONE draw of a standard_lighting
program and, at the default `emission_source_clamp` 0.7, draw it with the saturating clamp (SRCBLEND BLENDFACTOR 0.7,
DESTBLEND INVSRCCOLOR): every quad's output capped at 0.7, the sum saturating at 0.7, so no core could reach scene
white. The sequence's begin for a matched draw therefore runs before that route, and a draw it substituted skips the
route and is drawn under the game's own ONE / ONE into the FP16 scene; the frame row counts these draws (`hull`). A
refused begin (an unusable matrix, a failed step, all undone) leaves the draw to the hull route as without the
re-shade, never to the unclamped ONE / ONE. Without HDR the target is 8-bit and the core
saturates at 1. The screen-emission additive route does not take this pair (nine SM1 pairs only), so there is no
far-flag variant. In game at the defaults the vanilla flash itself goes through the clamp (it never exceeds 0.7); the
fixture's vanilla frames are the unclamped ONE / ONE sum.

**The law** (one glow around the gun in world space; the fan quads and the texture only modulate it):

| Term | Form |
| --- | --- |
| d | `|P − origin|` over the glow radius `lit_radius × world scale × mz_size` |
| glow g | `(1 − d²)²` for d < 1, else 0 |
| modulator t | `lerp(1, texel luminance / the cell's centre luminance, 1 − mz_smooth)`: grain and the streak's rays |
| radiance | `hue × (mz_gain × g × t + mz_core × g⁴ × lerp(1, white / hue, 0.5))` / the fan's overlap |
| alpha | vanilla's: `lerp(texel.a, light.a, g_EnableGlow) × COLOR0.a` |

The hue is the cell's at luminance 1, so `mz_gain` and `mz_core` are luminances; the core is white-shifted 50 %.
Every quad adds its share, so both terms are divided by the fan's overlap N, per draw: N = 8.89 + 1.31 cos², cos the
angle between the view ray to the gun and the flash axis (g_mWorld's z column; cos² = (v·z)² / (|v|²|z|²), no square
root), evaluated on the CPU with c192 and uploaded in c194.xy (the keys stay in c194.zw).
The lighting, specular, reflection and light-map colour terms of the vanilla program are not drawn (the material is
emissive, diffuse strength 0 [S]); the emissive itself and the quality-colour grading are replaced by the keys. The
rays come from the mesh: behind the gun the glow is cut by the quads' base edges into wedges.

**Body constants** [M, `verification/results/muzzle-reshade/muzzle_inputs_out.txt`]: vanilla's lit extent from the
gun, the radius holding 90 % of the emitted energy (every textured triangle sampled 24 × 24, energy = texel luminance
× area): **45618 model units** (50 % 19268, 95 % 60306; the vertices reach 101004); the fan's overlap at the gun, the
summed glow a ray through the origin crosses at the default size, mean over 500 directions: **9.14** (p10 5.17, p90
14.11); the cell's centre luminance 0.166; the lit texels' hue (1, 0.485, 0.166), at luminance 1 (1.7507, 0.8485,
0.2903). The cell (u 0.002853..0.411944, v −0.001845..0.511156) and the radial profile stay recorded there.

**The other bodies** (`verification/results/muzzle-reshade/muzzle_bodies.py`, `muzzle_bodies_out.txt`: the same
functions on each body's own mesh and atlas cell). The texel modulator carries the grain and the overlap the energy.
Lit radius and hue are measured as 445's [M]. `mean` is the body's own glow-weighted texel luminance at the baked size
[M], so its glow-weighted modulator is 1 and the grain stays at full strength (445's radial-fit method fails on the
strip cells of 444, 446, 447, 314 and 10463, whose centre is dark or whose fit sits at its r0 bound). The overlap is
inferred [I, no real-mesh render of these bodies]: 445's rendered fit × the body's ray sum through the gun over 445's
9.14 × 1 / 0.2547, 445's glow-weighted modulator at `mz_smooth` 0.1 over the body's 1, so gain and core over N sum to
445's luminance at the gun at the baked keys. At `mz_smooth` 1 these bodies draw 3.93× dimmer than 445, at 0 1.48×
brighter (445's modulator moves with the smooth, theirs does not).

| Body | Atlas | Quads | Lit radius | mean (radial fit) | Hue K | Ray sum (445: 9.14) | N side / axis [I] | Modulator at the brightest texel (445: 3.75) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 446 | 383 | 26 | 45865 | 0.3702 (0.052) | 1.076, 0.968, 1.091 | 23.44 | 89.51 / 102.71 | 2.49 |
| 444 | 376 | 4 | 59102 | 0.2187 (0.174) | 0.787, 1.113, 0.507 | 4.00 | 15.27 / 17.52 | 3.19 |
| 450 | 384, top-right | 50 | 35567 | 0.0473 (0.158) | 0.850, 1.036, 1.088 | 7.49 | 28.62 / 32.84 | 19.11 |
| 447 | 383 | 26 | 44959 | 0.3804 (0.046) | 1.115, 0.960, 1.060 | 23.41 | 89.41 / 102.60 | 2.43 |
| 314 | 384, bottom | 9 | 51481 | 0.9699 (0.118) | 0.872, 1.031, 1.067 | 8.09 | 30.90 / 35.46 | 1.03 |
| 10463 | 380 | 56 | 63918 | 0.0385 (0.022) | 1.304, 0.937, 0.727 | 5.11 | 19.51 / 22.39 | 2.68 |
| 12009 | fx_bullets2_diff | 50 | 48104 | 0.0264 (0.104) | 3.181, 0.409, 0.437 | 9.88 | 37.75 / 43.31 | 18.50 |

**The effects-pair body (12009, PRG; 2026-11-13).** FL loads `addon2/01.cat` `objects/v/12009.pbb`, the 01.cat text
record normalised (same geometry to 0.8 model units after the scale back [M]). MATERIAL6 `effects.fx`:
`fx_bullets2_diff`, cell u 0.406..0.570, v 0.241..0.447 (a red streak); `g_SrcBlend` 2 / `g_DestBlend` 4, so ADD
ONE / INVSRCCOLOR; `g_CullMode` 1; Z write off [M, `muzzle_bodies.py` `effect_material`]. Its 50 quads form three
cones: 8 along the axis, 18 at 41.6°, 24 at 77.9°. Its row was measured with 445's functions, as above [M]; the
overlap is inferred as for the other bodies: 445's fit × 9.88 / 9.14 × 3.926 [I].
- **Why a vertex shader too.** The game draws 12009 with the effects DEFAULT pair, VS `d5e1c753…` and PS
  `8360f422…`. That VS outputs only oPos, oT0.xy (the UV after `g_TexMatrix`) and oD0.x (the fade: `g_AlphaValue` ×
  the fog clip) ([muzzle-flash-draw.md](../reverse-engineering/fl/muzzle-flash-draw.md) section 6). A pixel program
  beside it cannot know a pixel's distance from the gun. A fit from UV to that distance misses 12009's glow-weighted
  sum by about 100 % at the baked size, because the quads' gun edges sit 2–10 thousand model units from the origin
  [M, a scratch fit, not kept].
- **Our vertex shader.** The substitution binds our vs_2_0 (`src/effects/weapon_muzzle_reshade_effects_vs.hlsl`, 28
  instructions, 218 words). It computes the game VS's oPos, oT0 and oD0 with the same operations on the same
  application constants, and adds oT1 = the model position (`effects_vs_read_out.txt`: the instruction multisets
  differ only in register lanes). The fixture draws both on the same mesh: 4420 covered pixels each, 0 coverage
  mismatches, and an absolute difference of 0 in COLOR0.x and TEXCOORD0.xy, fog off and on [M].
- **Our pixel program.** ps_2_0, `weapon_muzzle_reshade_effects_ps.hlsl`, 172 words. It runs the classic program's law
  through the shared `weapon_muzzle_reshade.hlsl`; the ps_3_0 bytecode is unchanged by the refactor (sha256
  `d1f7d545…`). Here d = |model position| / (lit radius × `mz_size`): the node's scale is uniform, so this equals the
  classic route's world distance over the lit radius × the scale. rgb is saturated, then multiplied by COLOR0.x, the
  fade the lone effects route uses. The saturate keeps the screen blend's 1 − src factor in [0, 1] on a float target,
  where a fixed-point target would clamp it. Alpha is the texel's, as in vanilla. The colour matrix c0..c2 is not
  applied.
- **Registers.** c29 = (1 − smooth, 1 / mean, 0, 1 / (lit radius × size)), c30 = (gain / N, core / N, gain, core),
  c31 = hue. c30.xy is set per draw from the effects VS's `g_mWorld` and `g_mViewInverse` rows c4..c9 (shadowed),
  which give the same overlap N as the classic route. These are the lone bullet block's registers (c29..c31); the hull
  hit effects program uses c30..c31 of the same block. All three routes put the registers back from the one
  SetPixelShaderConstantF shadow. They never share a draw, so sharing the registers is safe. The resync now also
  reads c29..c31 when only the muzzle re-shade is requested.
- **Order: one claimant.** The order in `before_draw` is: the lone classic and lone effects bullet routes (a body in
  the bullet map), the classic muzzle route (classic rows only), this route, then the hull hit route (which leaves
  every muzzle row alone).
  - This route runs while the effects DEFAULT VS is bound (`shadow_.vs_muzzle_effects`), on a draw no earlier route
    took and whose VS no route replaced (`!route.vs_set`).
  - The draw needs blend on, ADD, SRCBLEND ONE and a DESTBLEND some effects-pair row admits. Only those draws read the
    node.
  - Then `effects_match` leaves a mapped body to the lone route and takes a listed `pair_effects` row on its own
    DESTBLEND.
  - The fixture checks 41 ids under both blends: 0 multiple claimants and 0 mismatches. 12009 is refused off its blend,
    off its VS, when not ready, with blend off and when mapped [M].
- **Sequence.** `muzzle_reshade_route.h` `begin_pair` writes c29..c31, binds our VS, then our PS, all or nothing;
  rollback undoes the latest step first. `end_pair` binds the game's PS and VS (the shadowed bindings), then writes
  back the application's c29..c31. A failed restore stops both pairs for the device.
- **Counters.** The `muzzle_reshade_frame` row keeps its totals over both pairs and adds `effects_candidates=`,
  `effects_matched=` and `effects_draws=`. Creation logs `muzzle_reshade_program … pair=effects`.
- **AP.** The same code builds for AP. Mayhem replaces scene 08001's body with 12011 (MATERIAL6, not measured,
  unlisted), so nothing matches there. Stock AP keeps 12009 and would match it with FL's row [I].

**The per-draw overlap** [M, `verification/results/muzzle-reshade/overlap_by_angle_out.txt`, review F1]. The ray sum
through the gun depends strongly on the view angle: 5.9 side-on, 7.0 at the 25° pose, 27.3 along the axis (20000
directions). The rendered real mesh does not follow it: with the constant 9.14 the dump's sweep (yaw 0..90°, 30 m,
the scale peak) kept the peak at 1.37..1.58, the white radius at 7.3..8.6 px and the energy ratio at 0.133..0.158;
a normaliser fitted to the ray sum left no white core from 70° to the axis (peak 0.46 axis-on, energy 0.053) and put
side-on over the energy gate (0.204). N is therefore fitted to the rendered energy (energy ratio × the draw's N,
scaled to 9.14 at the 25° calibration pose): linear in cos², 8.89 side-on to 10.20 axis-on, worst 1.8 %; a sweep at
the fitted N refits to 8.888 + 1.300 cos² (converged). At the fitted N the sweep's energy ratio stays 0.137..0.142,
the peak 1.22..1.55 and the white radius 6.3..8.8 px.

## 3. Keys

Dev tuning file (`<game>\x3m\projectile_tune.json`, the same reader as `rs_*`, X3M_DEV_TUNING builds; a clamped key
logs one `projectile_tune clamped` row); release builds use the baked values (calibrated on the real mesh, section 4).

| Key | Default (baked FL 65 A, 2026-11-10) | Bounds | Meaning |
| --- | --- | --- | --- |
| `mz_size` | 0.2 | 0.05–1.0 | glow radius over vanilla's lit extent from the gun |
| `mz_core` | 0 | 0–4 | the white-shifted core's luminance at the gun (summed over the fan) |
| `mz_gain` | 0.15 | 0–2 | the glow's luminance at the gun (summed over the fan) |
| `mz_smooth` | 0.1 | 0–1 | share by which the texel's grain is flattened (1: a plain glow) |

The defaults are what the user accepted on FL 65 A. The run's floor for `mz_size` was 0.2, so the reader clamped the
file's 0.1 (and an earlier 0.15) to 0.2: the flight drew 0.2 (`projectile_tune clamped key=mz_size value=0.1
applied=0.2` in that run's log) [M], and 0.2 is baked [D, coordinator 2026-11-10]. The floor is now 0.05, so a file's
0.1 is drawn as written (about a tenth of the baked flash's peak on the real mesh, scratch dump). The calibration
below is of the first defaults (0.5 / 0.5 / 0.2 / 0.6).

The coordinator expected about gain 1 and core 2; on the real mesh those give a peak of 7.1, a white region of 27.3 px
(0.86 of vanilla's) and 0.63 of vanilla's energy at 30 m [M, sweep]. The 25° pose is below the ray-sum mean, not above
(7.0 against 9.14, overlap_by_angle_out.txt); the excess comes from the rendered overlap at the gun's pixels, about
twice the ray-sum mean at every view angle [I, from the peak with gain + core at the gun], and the texels lifting the
modulator. The sweep (size 0.5, smooth 0.6; near frame; the constant 9.14 of the time) [M]:

| gain, core | white r px (vanilla 31.6) | peak | energy ratio |
| --- | --- | --- | --- |
| 1, 2 | 27.3 | 7.08 | 0.634 |
| 0.5, 2 | 23.3 | 4.79 | 0.443 |
| 0.5, 1 | 19.4 | 3.54 | 0.317 |
| 0.4, 1.5 | 20.5 | 3.68 | 0.342 |
| 0.3, 0.8 | 15.0 | 2.35 | 0.215 |
| 0.25, 0.6 | 12.0 | 1.88 | 0.171 |
| **0.2, 0.5** | **8.5** | **1.53** | **0.139** |

0.2 / 0.5 puts the white core at 0.27 of vanilla's white radius (the target was about 0.25).

## 4. Gates and evidence

Gates (fixture and dump, since the FL 65 A bake on 2026-11-10): the **additive energy** (sum of luminance over the
crop) ≤ `mz_gain` × vanilla's; the **extent at 0.1 linear** (equivalent radius of the pixels ≥ 0.1) ≤ `mz_size` ×
vanilla's + 1 px; the flash lit (fixture: peak ≥ `mz_gain` / 4 on the stand-ins of 445 and 444, three poses each,
444's gun 3 m beside the view line since its four planes contain the axis; dump: peak > 0 on the real mesh of 445).
At the baked keys 444's stand-in peaks below 0.1 linear, so the extent at 0.1 binds nothing for it; the fixture also
gates the extent at a tenth of each image's own peak at no more than vanilla's for both stand-ins, and that binds.
The dump also gates the extent at a tenth of each image's own peak at no more than vanilla's (the fixture too, since
2026-11-13). With `mz_core` 0 there is no white core, so the extent at scene white is reported, not gated.

**12009 (effects pair, 2026-11-13) [M, native fixture, MUZZLE_EFFECTS rows].** The stand-in is
`standin_12009.py`. It uses the real quads' cone layout, rounded, and a red streak with the real cell's profiles and
spread. Against the real body: mean +2.3 %, lit radius +4.6 %, glow-weighted luminance +3.3 %, ray sum +4.9 %, p99
equal; the brightest texel is 0.328 against 0.539. The same gates at the same three poses, 30 m, under the game's
ONE / INVSRCCOLOR:

| Pose | N | Energy ratio (≤ 0.15) | Peak re-shade / vanilla (lit bound) | Tenth-of-peak r px re-shade / vanilla |
| --- | --- | --- | --- | --- |
| tilt 25° | 38.40 | 0.002 | 0.048 / 0.936 (≥ 0.0375) | 16.8 / 92.4 |
| axis | 43.31 | 0.002 | 0.021 / 0.908 (≥ 0.01875) | 21.3 / 90.2 |
| side | 37.79 | 0.002 | 0.054 / 0.936 (≥ 0.0375) | 15.6 / 92.3 |

**Lit bound per pose (coordinator decision, 2026-11-13).** The bound is `mz_gain` / 4 at tilt and side and `mz_gain`
/ 8 axis-on. This states the fan's own axis-on dimness; it does not loosen the tolerance. Viewed down the axis, the
eight axial quads are edge-on, and the inferred N (43.3, 4.2× 445's) spreads the gun's summed luminance (by
construction 445's mean) over the cones at 42° and 78°. The real 445 mesh's re-shade, at the baked keys, also peaks at
0.025 axis-on (dump, above). The inferred normalisation is kept. A first run with narrower texel noise (p99 0.204)
peaked at 0.044 / 0.018 / 0.050. All 11 `muzzle_effects_*` checks pass (134 / 134 in the record).

**Frames** (`verification/results/muzzle-reshade/fixture-muzzle-effects-{tilt,axis,side}.png`, written by the native
fixture run: four 320 px crops about the screen centre, vanilla on black, vanilla on linear 0.6 grey, re-shaded on
black, re-shaded on grey; stand-in assets) [viewed]:

| Frame | What it shows |
| --- | --- |
| tilt | Vanilla: a big pink-red fan with a white-hot centre, washing pale over the grey. Re-shaded: a small dim red dot at the gun on black, nearly invisible on grey. |
| axis | Vanilla: an eight-petal pink star with a white centre. Re-shaded: a faint small dark-red rosette on black, invisible on grey. |
| side | Vanilla: a fan plus a long horizontal streak. Re-shaded: a small red half-disc at the gun on black, barely visible on grey. |

## 5. Windows and AP

Shared production code: documented D3D9 calls only (Set/GetPixelShaderConstantF, SetPixelShader, the VS constants
read at resync through GetVertexShaderConstantF), a ps_3_0 program beside the game's vs_3_0; for 12009 also
CreateVertexShader / SetVertexShader and a vs_2_0 + ps_2_0 pair of our own, both created through the native slots and
released at teardown. Native Windows: the fixture runs on the native D3D9 runtime
(below); in-game behaviour is unverified on either platform until flown. AP (2026-11-13): compiled for both
editions. The flash VS `494fe349…` is the same bytes on AP stock, AP Mayhem and FL, the scope node's body id is AP node
`+0x140` (the object-trace scope `0x004c5228 → 0x004c0150`, default on), and `reshade` logs
`weapon_effects_reshade edition=ap runs=reshade … muzzle=reshade` once its sites are live
([ap-bullet-batch-site.md](../reverse-engineering/ap-bullet-batch-site.md) sections 2 and 3). Stock AP's flash bodies
are FL's seven. Mayhem's flashes use 445, 444, 446 and 314 and six Mayhem bodies, 12012, 12013, 12014, 12016, 12017 and
12018. Those six were added to `bodies[]`, measured as the FL six [M]
([`muzzle_bodies_ap_out.txt`](../../verification/results/muzzle-reshade/muzzle_bodies_ap_out.txt)). 445 runs first in
that output and reproduces the FL row, so the overlaps are normalised to it [I]. 12013 and 12014 have grey cells, so
they draw a white flash. 12011 is MATERIAL6 `effects.fx` (like 12009) and stays vanilla (not measured). Not flown on AP. [U] The classic submit swaps `standard_lighting` for `effects` when node +0x1c
is the cockpit view (muzzle-flash-draw.md section 3); a flash drawn that way has another VS and stays vanilla. Whether
the own guns' flash in first person takes that path is not established.

## 6. User decisions

0. (2026-11-09, the custom flash's brief) The vanilla muzzle flash is a big splash; the flash must be "more accurate":
   small, in proportion to the bolt; a brief light pulse on the ship's own hull around the mount; a short streak from
   the muzzle in the weapon's colour.
1. (2026-11-09) First person: the user does not fire in first person, but others may; the own-gun flash stays drawn
   there as designed (a spot at the screen edge plus the hull pulse); no cockpit exception.
2. (2026-11-09) SETA: keep the one-frame flash per shot for now; judge in flight.
3. (2026-11-09) Hull pulse on every ship, own and NPC.
4. (2026-11-09) The look is undecided until flown: "let's build and look"; the first flight (FL run 62) judges it.
5. (2026-11-09, FL 62 A) The first flash was invisible; life moved to wall-clock ms.
6. (2026-11-09, after FL 63 A) Re-shade the game's own muzzle flash in place, the way the bullet draw was re-shaded:
   "make it less noisy/bright, with better falloff, maybe with short rays/lens like our sun; just a flash, more
   beautiful, smaller than vanilla". No new mode: `reshade` covers both. Decisions 0–5 belonged to the custom flash
   (superseded, [archived](../archive/effects-look-2026-11-13-custom-projectile/muzzle-flash-look-custom.md)).
7. (2026-11-09, coordinator on the first re-shade's dumps) Rejected: a per-quad falloff in the cell's UV drew one beige
   lobe per quad with no hot centre ("lava, not fire"). The falloff is in world space around the gun, the texel only
   modulates, and a white-shifted core reaches scene white within about a quarter of vanilla's white radius.
8. (2026-11-09, FL 65 A accepted; 2026-11-10 coordinator) The flown values are baked: `mz_size` 0.2 (the value drawn:
   the file's 0.1 was clamped to the then floor 0.2; the floor is now 0.05), `mz_core` 0 (no white core), `mz_gain`
   0.15, `mz_smooth` 0.1. With no core the gate at scene white no longer applies; the extent is gated at 0.1
   linear (section 4); the extent at a tenth of each image's own peak is reported only.

## Reproduce

```sh
python3 -I verification/results/muzzle-reshade/muzzle_inputs.py \
  --texture verification/results/weapon-effects-lab/fl/tex/384-bd79416d.png > verification/results/muzzle-reshade/muzzle_inputs_out.txt
python3 -I verification/results/muzzle-reshade/muzzle_bodies.py > verification/results/muzzle-reshade/muzzle_bodies_out.txt
python3 -I verification/results/muzzle-reshade/standin_444.py > verification/results/muzzle-reshade/standin_444_out.txt
python3 -I verification/results/muzzle-reshade/standin_12009.py > verification/results/muzzle-reshade/standin_12009_out.txt
python3 -I verification/results/muzzle-reshade/effects_vs_read.py > verification/results/muzzle-reshade/effects_vs_read_out.txt
python3 tools/shaders/generate_muzzle_reshade_program.py --check
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_weapon_effects.py --host native \
  --dump verification/results/muzzle-reshade --dump-case muzzle --lab-assets verification/results/weapon-effects-lab/fl
python3 -I verification/results/muzzle-reshade/overlap_by_angle.py --rendered verification/results/muzzle-reshade/dump_out.txt \
  > verification/results/muzzle-reshade/overlap_by_angle_out.txt
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_weapon_effects.py --host native
```

`--muzzle-keys size,core,gain,smooth` (into a scratch `--dump` directory) reproduces a row of the sweep. The dump reads
body 445 from the FL catalogues (`--game`, default `/mnt/c/X3FL`) and the atlas from the untracked lab
assets into the untracked build directory; without them it draws procedural stand-ins and says `assets=standin`.
