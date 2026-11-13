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
**447** (04258; FBL), **314** (04261; Ion Disruptor), **10463** (04437; PBE). Body 12009 (scene 08001, PRG) draws with
effects.fx, not this pair, and stays unlisted; scene 04265 is absent from the catalogues. The route checks the VS hash
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

**At the baked values (2026-11-10) [M, `dump_out.txt`]** the real mesh's re-shaded flash peaks at 0.025–0.081 linear
(near 0.074 against vanilla 9.10; side 0.081, axis 0.025; energy ratio 0.001–0.003), so it never reaches 0.1: the
extent at 0.1 is 0 and every gate passes. Its extent at a tenth of its own peak is 14.9 px against vanilla's 33.6
(near), 18.1 / 26.9 (axis), 1.6 / 4.6 (far). At `mz_size` 0.1 the peak would be 0.0015–0.015 (scratch dump). The
view-angle overlap N (section 2) was fitted on the first defaults' sweep; `overlap_by_angle_out.txt` is that fit and
is **superseded as a record of the current `dump_out.txt`**, whose sweep rows are now at the baked keys (N itself is
unchanged: it describes the fan, not the keys).

The first defaults' frames (0.5 / 0.5 / 0.2 / 0.6, superseded):

Real mesh and atlas cell (`assets=game`), the scale peak unless stated, 1920x1080 FP16, m00 1.6, the axis 25° out of
the screen plane unless stated, dark background (nebula beside it in each PNG), the defaults, the per-draw N [M,
`verification/results/muzzle-reshade/dump_out.txt`]:

| Frame | N | Energy ratio (gate ≤ 0.2) | Peak reshade / vanilla | White r px reshade / vanilla (gate ≤ 0.5 ×) | 0.1 r ratio |
| --- | --- | --- | --- | --- | --- |
| near, 30 m | 9.05 | 0.141 | 1.55 / 9.10 | 8.6 / 31.6 | 0.494 |
| far, 300 m | 9.12 | 0.132 | 1.31 / 4.83 | 0.8 / 3.2 | 0.439 |
| grow, 30 m, 20 % of the peak scale | 9.11 | 0.135 | 1.40 / 7.08 | 1.7 / 6.1 | 0.468 |
| axis, 30 m, the axis along the view ray | 10.20 | 0.142 | 1.22 / 8.84 | 6.3 / 24.8 | 0.505 |
| side, 30 m, the axis in the screen plane | 8.90 | 0.137 | 1.53 / 9.15 | 8.7 / 31.5 | 0.475 |

All gates pass at every frame; the core reaches scene white at all five, the growing flash included. Frames:
`fixture-muzzle-{vanilla,reshade}-{near,far,grow,axis,side}.png` in `verification/results/muzzle-reshade/`; the
dump's 14 `DUMP_MUZZLE_SWEEP` rows are the view-angle sweep the overlap is fitted on. World scale is
inferred: body units × node scale / 500 engine units per metre (the flash about 7 m long at its peak) [I].

## 5. Windows and AP

Shared production code: documented D3D9 calls only (Set/GetPixelShaderConstantF, SetPixelShader, the VS constants
read at resync through GetVertexShaderConstantF), a ps_3_0 program beside the game's vs_3_0. Native Windows: the fixture runs on the native D3D9 runtime
(below); in-game behaviour is unverified on either platform until flown. AP (2026-11-13): compiled for both
editions. The flash VS `494fe349…` is the same bytes on AP stock, AP Mayhem and FL, the scope node's body id is AP node
`+0x140` (the object-trace scope `0x004c5228 → 0x004c0150`, default on), and `reshade` logs
`weapon_effects_reshade edition=ap runs=reshade … muzzle=reshade` once its sites are live
([ap-bullet-batch-site.md](../reverse-engineering/ap-bullet-batch-site.md) sections 2 and 3). Stock AP's flash bodies
are FL's seven. Mayhem's flashes use 445, 444, 446 and 314 and six Mayhem bodies, 12012, 12013, 12014, 12016, 12017 and
12018. Those six were added to `bodies[]`, measured as the FL six [M]
([`muzzle_bodies_ap_out.txt`](../../verification/results/muzzle-reshade/muzzle_bodies_ap_out.txt)). 445 runs first in
that output and reproduces the FL row, so the overlaps are normalised to it [I]. 12013 and 12014 have grey cells, so
they draw a white flash. 12011 is MATERIAL6 `effects.fx` (like 12009) and stays vanilla. Not flown on AP. [U] The classic submit swaps `standard_lighting` for `effects` when node +0x1c
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
