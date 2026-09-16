# Effect/emission shader users

Who draws with each effect/emissive program the proxy touches, and under what
render state. Derived 2026-09-16 from the installed CAT/DAT archives (material
definitions), the existing archive sweep artifacts, and the preserved session
logs; no game launch, no Wine, no Ghidra run of its own. It extends
[shader-sweep.md](shader-sweep.md), [shader-family-review.md](shader-family-review.md),
[effects-engine-remaining-emission.md](effects-engine-remaining-emission.md) and
[emission-draw-order.md](emission-draw-order.md) rather than repeating them.

## How a draw selects one of these programs

The material, not the object class, names the shader source. Each `MATERIAL6`
record inside a body file (`objects/**.pbb` binary `BOB1`/`MAT6`, `objects/**.pbd`
text `MATERIAL6:`; both are gzip under a bytewise XOR `0x33`, the same encoding
as the `.fb` effects) stores an effect-source name such as `effects.fx`,
`engine.fx`, `argon.fx`, `nebulafog.fx`, plus the fixed-function render state
the pass will use: `g_AlphaBlendEnable`, `g_SrcBlend`, `g_DestBlend`,
`g_BlendOp`, `g_ZEnable`, `g_ZWriteEnable`, `g_ALPHATESTENABLE`,
`g_ColorWriteEnable`, `g_CullMode`, `g_AlphaValue`, the `t_*Texture` bindings
and the `TexAnim*`/`Brightness`/`contrast`/`saturation`/`hue`/`colormatrix`
parameters. The values are D3D enums (`2` = `D3DBLEND_ONE`, `4` =
`D3DBLEND_INVSRCCOLOR`, `5` = `D3DBLEND_SRCALPHA`; `g_BlendOp 1` = `ADD`).

The executable turns the source name into an archive path with
`shader\%s\%s` at `0x00563358` (profile directory, then optional toggle
subdirectory plus effect name). `engine` is **not** an executable string; only
`effects` (`0x0055608c`), `particles`, `bloom`, `z_only`, `standard_lighting`,
and the technique names `DEFAULT` (`0x0056346c`), `INSTANCE` (`0x005633f0`),
`INSTANCE_BULLETS` (`0x005633dc`), `BASE_NoLight` (`0x00563378`) are. So the
`engine` versus `effects` alias is pure material data, and `"effects"` is the
hard-coded fallback of the instanced-mesh path (`effects-engine-remaining-emission.md`,
"Bullet vertex buffer writer", `group+0x1c & 0x10`).

`shader/<profile>/<name>.fb` where `<name>` is the base alias, `<name>2s`,
`<name>_0000` or `<name>_0001`. Two-sidedness is the likely `2s` selector
(**inference**: of the `effects.fx`/`engine.fx` materials parsed from binary
bodies, 151 set `g_CullMode 1` = `D3DCULL_NONE` and 114 set `2` = `CW`), and the base/`2s` effect files are
byte-identical anyway, so they cannot be told apart by program hash. **No
material in any archive names `effects_0000.fx` / `engine_0000.fx`**; what
selects the `_0000`/`_0001` files is unresolved, and no program unique to them
has ever been created in a session (see below).

Technique inside the effect file: `DEFAULT` is the ordinary submesh material
dispatch (`0x004c0150`, [emission-draw-order.md](emission-draw-order.md)),
`INSTANCE`/`INSTANCE_BULLETS` is the instanced-mesh writer/draw
`FUN_004bf960`/`FUN_004bfd40`.

## What actually runs: 81 preserved sessions

One `grep -o '^shader kind=[pv]s id='` pass per log over all 81 session logs
under `/tmp/x3-bottleX3-run*/` yields **39 distinct pixel programs and 24 distinct
vertex programs ever created**, out of 495 PS / 256 VS in the archive. Of the
`effects`/`engine` family only five programs ever exist at runtime, all from the
base alias in profile `3_0`:

| Program | Stage | Technique | Sessions |
| --- | --- | --- | ---: |
| `d5e1c75351ed3f04` | VS 2.0 | `DEFAULT` (`g_TexMatrix` UV) | 69 |
| `89193868c61c3846` | VS 2.0 | `INSTANCE` (direct UV) | 69 |
| `5e484a06672e28fb` | VS 1.1 | `INSTANCE_BULLETS` | 69 |
| `8360f422de08b5bd` | PS 2.0 | `DEFAULT` + `INSTANCE` | 69 |
| `ec1f5c4a2f4e1445` | PS 1.1 | `INSTANCE_BULLETS` | 69 |

That is exactly the content of `shader/3_0/effects.fb` (= `effects2s` = `engine`
= `engine2s`, byte-identical). **The other 18 of the 20 pairs and 8 of the 9
bullet pairs have never been created in any preserved session**: they live only
in `effects_0000/0001`, `engine_0000/0001`, or in the `1_1`/`1_4`/`2_0`/`2_a`/`2_b`
profile directories that this installation does not load.

## Program table

Confidence: **established** = archive material record or byte-level match with a
capture; **inferred** = consistent but not witnessed.

| PS hash | Source alias(es) | Who draws with it | Blend state | Table | Confidence |
| --- | --- | --- | --- | --- | --- |
| `8360f422de08b5bd` | `effects`, `effects2s`, `engine`, `engine2s`, prof 2_0/2_a/2_b/3_0 | every `effects.fx` / `engine.fx` material: ship engine glow, jump-gate whirl and streaks, explosions, weapon bolts/beams, shield/impact sprites, lens flares, sun flares, station/dock glows, menu markers | per-material; see the blend census below | 20-pair, rows `d5e1c753`(Engine) and `89193868`(Effect) | established |
| `ec1f5c4a2f4e1445` | same four base aliases, all six profiles | `INSTANCE_BULLETS` geometry from `FUN_004bfd40` | ADD ONE/INVSRCCOLOR observed (54 draws, 12 frames) | 9-pair bullet, `bullet=true` | established |
| `9975b706e5a1c999`, `ff2473e73a6bdfa1` | `engine_0000`, `engine_0001` | no material names these aliases; never created | unobserved | 20-pair (Engine) | unknown users |
| `8559522220507d5e`, `875e780adb131b16` | `effects_0000/0001`, prof 3_0 only | idem | unobserved | 20-pair (Effect) | unknown users |
| `39f3b4d5b6a5aaed`, `846c5c1a549f9491` | `effects_0000/0001`, prof 2_0/2_a | idem | unobserved | 20-pair (Effect) | unknown users |
| `47e15e20d63b0e93`, `c6dacb8f74b65c97` | `effects_0000/0001`, prof 2_b | idem | unobserved | 20-pair (Effect) | unknown users |
| `f0c91793a75e1203` | `effects`, `effects2s`, prof 2_b only | the base alias in a profile this installation does not load | unobserved | 20-pair (Effect) | unknown users |
| `078494828322bcca`, `2ea025492d370c8e`, `a5c3495e27270b4a` | SM1 `DEFAULT`/`INSTANCE`, prof 1_1/1_4 | same materials as `8360f422` would use on an SM1 machine | unobserved | 9-pair bullet, `bullet=false` | unknown users |
| `84d3de8887c963c5`, `d4a26efb7c603931` | `engine_0000/0001`, `effects_0000/0001` bullets | idem | unobserved | 9-pair bullet, `bullet=true` | unknown users |

### Blend census of the `effects.fx` / `engine.fx` materials

All **288 material records in 218 body files** name one of the two sources
(`effects.fx` 224, `engine.fx` 64). By owner:

| Owner | `engine.fx` | `effects.fx` | Blend (`g_BlendOp` ADD throughout) |
| --- | ---: | ---: | --- |
| `objects/effects/engines/*` — **ship engine glow / thruster flares**, 122 bodies, 122 records | 42 | 50 | **ONE/INVSRCCOLOR (screen)**, Z test on, Z write off, mask 15, alpha test off |
| `objects/effects/engines/*` | 22 | 8 | ONE/ONE (additive) |
| `objects/effects/weapons/bullet_*`, `fx_beams_diff` — bolts and beams | — | 20 | ONE/INVSRCCOLOR |
| `objects/effects/weapons/bullet_push*` | — | 3 | ONE/ONE, SRCALPHA/INVSRCALPHA, DESTALPHA/SRCALPHA |
| `objects/others/argon_gate_effect.pbb` — **jump gate** | — | 2 | ONE/ONE; textures `effects\others\fx_gatewhirl.tga`, `fx_gatestreaks.tga` |
| `objects/effects/explosions/expl.pbb` | — | 1 | ONE/ONE |
| `objects/environments/planets/*` — sun flare sprites (`sun_flare_v2_light.tga`) | — | 62 | 60 ONE/ONE, 1 ONE/INVSRCCOLOR (`sun_lens_diff`), 1 SRCALPHA |
| `objects/v/11000–11011` — **lens flares** (`fx_*flare.dds`, exporter path `…\LensFlares\…`) | — | 12 | SRCALPHA/INVSRCALPHA |
| `objects/v/00518, 10659, 12007, 12009` — impact/shockwave/shield-hit sprites | — | 6 | ONE/INVSRCCOLOR |
| `objects/v/00262, 10656, 10662, 10670, 11994–12010` — misc effect sprites, sun lens | — | 17 | ONE/ONE |
| `objects/effects/menugfx/{docked,dockhere}` | — | 2 | ONE/ONE |
| stations (`dock_tunnel`, `Boron_SYshield`, terraformer hub, XTC) | — | 8 | 6 ONE/ONE, 2 ONE/INVSRCCOLOR |
| ships (Goner TL beams, khaak hive lightning, XTC/x3ap engine and shield props) | — | 17 | 9 ONE/ONE, 8 ONE/INVSRCCOLOR |
| `objects/environments/nebulae/*_stars_01` | — | 4 | 1 ONE/ONE, 1 ONE/INVSRCCOLOR, 2 SRCALPHA/INVSRCALPHA |
| `objects/cut/*`, `arrow_indicator`, `gameover_trans` | — | 12 | mixed; 1 with blending disabled |

Totals: `effects.fx` 114 additive / 92 screen / 16 alpha / 1 DESTALPHA/SRCALPHA
/ 1 blending off; `engine.fx` 22 additive / 42 screen.

### Jump gate: byte-level confirmation

`objects/others/argon_gate_effect.pbb` holds exactly two `effects.fx` materials,
both ADD ONE/ONE with Z test on and Z writes off, binding
`dds/fx_gatewhirl.pck` (**1024×1024 DXT5**) and `dds/fx_gatestreaks.pck`
(**1024×512 DXT5**). That is the historical "two-draw burst" of
[emission-draw-order.md](emission-draw-order.md): one material dispatch, two
subsets, identical object/camera/state, stage-0 texture changing 1024×1024 →
1024×512, VB 792 → 1584, IB 192 → 384, and the per-draw `g_TexMatrix` rows
c10–c11 differing (the whirl scrolls, the streaks use identity). **Established.**

## Engine glow

The player ship's engine glow is drawn by the pair
**VS `d5e1c75351ed3f04` / PS `8360f422de08b5bd`** (the `DEFAULT` technique of the
base `engine`/`effects` alias in profile `3_0`), from the bodies under
`objects/effects/engines/`, which are separate scene objects attached at the
ship's engine nozzles. It is therefore *in* the 20-pair table — the same pair
that draws the jump gate — and is labelled family **Engine**.

No option changes it because of the blend state, not the table:

- 92 of the 122 engine-glow materials (all 42 `engine.fx` screen ones and 50
  `effects.fx` ones) are ADD **ONE/INVSRCCOLOR**.
  `renderer::linear_emission_source_gain_blend` (`src/renderer/linear_emission.cpp:227`)
  returns `SourceGainBlend::Screen` for `dst == D3DBLEND_INVSRCCOLOR` and the
  draw keeps its native program (`source_gain_counts_.refused_screen`,
  `src/proxy/motion_output.cpp`, `prepare_source_gain`).
- Run 83 (`/tmp/x3-bottleX3-run83/session-20260916-035059-216.log`, 64,007
  `emission_source_gain_frame` lines with `gain=5 effect_gain=5`) aggregates to
  **admitted 78,307 (all `admitted_engine`, `admitted_effect` 0),
  refused_screen 124,827, refused_state 101,263, refused_blend 0,
  refused_unknown 0, bind_failures 0**. All 16 sampled refusals are
  `vs=d5e1c753… ps=8360f422… reason=screen_blend blend=1 src=2 dst=4 op=1
  sepalpha=1`. The single `emission_source_gain_pair` line is
  `vs=d5e1c753… ps=8360f422… family=engine gain=5`.
- Per frame the common shapes are `admitted 0–3` with `refused_screen 1–3`:
  the additive population (gate, explosions, flares) varies with what is in
  view, the screen population (engine glows of visible ships) is present in
  nearly every frame. That is why F6 visibly brightened the gate and nothing
  brightened the engines.
- `--screen-emission-additive` does not reach this population either: its table
  (`src/proxy/screen_emission_admission.h`) is the nine **SM1** pairs only, and
  the SM1 programs are never created on this installation. The SM2 screen
  population under `8360f422` has no option at all.
- The 30 engine-glow materials that are ADD ONE/ONE (22 `engine.fx`, 8
  `effects.fx`, e.g. `fx_engine_argon_M3plus`, `fx_engine_split_M3`,
  `fx_engine_usc_m4`) *would* take the engine gain. A further 22 materials
  inside `objects/effects/engines/` do not use these sources at all
  (9 `standard_lighting.fx` ONE/ZERO, 6 `standard_lighting.fx` ONE/ONE,
  4 `terran.fx` ONE/ZERO, 2 `standard_lighting.fx` SRCALPHA/INVSRCALPHA,
  1 `XT_standard_lighting.fx` ONE/ZERO), so part of a nozzle's appearance is a
  hull material program outside every emission table. Whether the ship flown in
  run 83 owns one is not established; the `admitted=2` shape matches the gate's
  two materials, so probably not.

`refused_state` (101,263, no blend triple by construction) remains the late-overlay
population of the same pair; the lens-flare (`objects/v/1100x`, SRCALPHA) and
menu-marker materials are the natural candidates, **inference** only.

## Corrections to the Engine/Effect split

The current split in `src/renderer/linear_emission.cpp:25-70` is documented in
[linear-emission-cost.md](../architecture/linear-emission-cost.md) "Family split"
as alias-derived. The archive material data contradicts two of its premises:

1. **`d5e1c75351ed3f04` / `8360f422de08b5bd` labelled `Engine` is not the engine
   family.** The `DEFAULT` technique of the base alias is shared by every
   `effects.fx` *and* `engine.fx` material, and the only population it has ever
   been observed admitting is the jump-gate whirl/streaks. A truthful label
   would be "base alias, ordinary material dispatch" — it covers gate,
   explosions, flares, station glows and engine glow alike. The engine/effect
   distinction cannot be made from the shader pair, because `engine.fb` and
   `effects.fb` are byte-identical and the same PS serves both techniques.
2. **`89193868c61c3846` / `8360f422de08b5bd` labelled `Effect` is not a separate
   population from (1) either.** It is the same effect file and the same PS; only
   the vertex technique differs (direct UV, instanced-mesh writer). It is created
   in 69 sessions but has never been observed in an admitted or refused draw, so
   its users are unestablished.
3. The `engine_0000`/`engine_0001` provenance used to label four pairs `Engine`
   (`32e75459`/`5b7a3ccd` with `9975b706`/`ff2473e7`) rests on an alias that no
   material names and that has never been loaded. The label is not wrong so much
   as **unfounded**; the same applies to the twelve `effects_0000/0001` `Effect`
   rows.

Practical consequence: splitting the gain by family does not separate engine
glow from effect sprites, because the live population is one PS. The separation
that does exist in the data is the **blend state** — ONE/ONE (gate, explosions,
sun flares, some engines) versus ONE/INVSRCCOLOR (most engines, all bolts and
beams, impacts) versus SRCALPHA (lens flares) — and it is per material, not per
program.

## Missing from the tables

Census over the whole population: 495 archive PS, of which 108 are material
originals (`linear-material-profiles.json` 90 + `xt-material-profiles.json` 14 +
`glass-material-profiles.json` 4), 10 are in the 20-pair table, 6 in the 9-pair
bullet table, and 39 PS have ever been created across the 81 preserved sessions.
Emissive/additive/blended-sprite programs outside all three tables:

| PS hash | Source alias | Users and blend | Sessions | Emitter the gain should cover? |
| --- | --- | --- | ---: | --- |
| `222bee0defcb1852` | `particles`, `particles2s` (SM1.1, all six profiles) | the engine's own particle renderer `0x004bf4c0` (effect `particles`, technique `BASE_NoLight`, stride 0x20); no archive material names `particles.fx`, the renderer sets `g_SrcBlend`/`g_DestBlend` itself; historically observed SRCCOLOR/INVSRCCOLOR | 73 | Yes for smoke/dust/trails, but it is SM1 (no `oC1`/`oC2`) and non-additive; needs the SM1 producer plus a screen/multiply contract |
| `03a16e5c63daa6e8` | `adeffects`, `adeffects2s` (prof 2_0/2_a/2_b/3_0) | advertising signs: `objects/special/x3ap` 158, `objects/others/*_adsign_*` 8, stations 4, cut 3. **172 of 173 materials disable blending**, one is SRCCOLOR/INVSRCCOLOR | 73 | No — opaque emissive billboards; needs a material (not additive) contract |
| `6109cf64c03529dd` | `gui2d` **and** `nebula`, `nebula2s` (SM1.1, all profiles) | 195 `nebula.fx` materials (173 blend off, 14 ONE/INVSRCCOLOR, 3 ONE/ONE, 4 SRCALPHA, 1 SRCCOLOR/INVSRCCOLOR) plus HUD/2D. Shared with UI, so hash alone cannot own a pass | 73 | Partly — the 3 ONE/ONE nebula sprites qualify; blocked by SM1 output and by UI sharing |
| `0a523f33ac47ae05` | `gui2d` **and** `stardust` (SM1.1, all profiles) | `objects/environments/others/dust.pbd` (1 material, SRCALPHA/INVSRCALPHA) plus HUD/2D; 11 `gui2d.fx` materials are ONE/ONE (`objects/effects/menugfx`, `objects/Interface`), 46 are SRCALPHA | 73 | No for stardust (alpha blend); the ONE/ONE `gui2d` markers are UI, deliberately excluded |
| `cd6d6eb4b3d99443` | `planet_haze` (all profiles) | 98 materials, `objects/environments/planets` 87, nebulae 5, stations 3, cut 2, khaak 1; their records carry **no `g_*` state at all**, so the blend comes from the effect file | 73 | No — the family review gives fixed low alpha coverage, not emission |
| `d6f6ba4fee1cd53e` | `moon` (prof 2_0/2_a/2_b/3_0) | 61 materials, all `objects/environments/planets`, all with blending disabled | 31 | No |
| `652a7c5d1e9909a0` | `z_only` | depth/alpha-test pass | 40 | No |
| `1c90e79667bdaddf`, `241c3fa33270f58e`, `b40d09effa812ec8`, `f3172baa8dd19a40`, `ff6eed5a5ddf3a3a` | `bloom` (prof 3_0) | stock compositor | 81 | No — post boundary |
| `a66fb1981ba755b2` | `glass` (prof 3_0) | 75 `glass.fx` materials (69 blend off, 6 ONE/ONE) on ships and cut scenes | 70 | The 6 ONE/ONE glass materials are additive emitters; the program is already a material original, so the gain would need a second route |
| `f7e0b6647a3bfa62` | `nebulafog`, `nebulafog2s` (all profiles) | **277 materials, every one ADD ONE/INVSRCCOLOR**, all `objects/environments/nebulae`, all in text `.pbd` bodies | 0 | Yes in principle — a large uncovered screen-blend emitter family; never created in any preserved session, so no sector visited so far uses it |
| `061889835cc5241f`, `0e311fdab3c8ea49`, `2ff8848b2d0d08f9`, `37e6a5efda1d960a`, `3c7c14b76b1bf8ef`, `66cea7b0cf59a601`, `77a80ea5d6b097e4`, `bf897ecca953bc83`, `e57a3bb1ed14fa5d`, `e6a26abf51f93e0b`, `ef879a8c1d09a180`, `f8089d4363f51236` | `glass*` lower profiles / `_0000`/`_0001` | same glass materials on profiles this installation does not load | 0 | No |
| `128b987483a23771`, `6103491c75c6f502`, `965291a07b94667b`, `bf93fc7acb3377ea` | `adeffects*` variants | as `03a16e5c…` | 0 | No |
| `4f837419582f9785`, `d0aacb2c65e011ed`, `da43623af4f72352`, `214fc10d3672ab92`, `6b0be9563c21ec03`, `c002d0727537c5de` | `gui2d_0000/0001`, `stardust_0000/0001`, `nebula_0000/0001`, `particles_0000/0001`, `nebulafog_0000/0001`, `planet_haze_0000/0001` | `_0000`/`_0001` variants, no material names them | 0 | Only if the `_0000` selector is ever resolved |

### Additive emitters drawn by *material* programs

A census of every `ADD ONE/ONE` material record in the binary bodies, by effect
source, shows the 20-pair table is **not** complete even for additive emission:

| Effect source | ONE/ONE materials | Owners | Programs |
| --- | ---: | --- | --- |
| `XT_standard_lighting.fx` | 639 | `objects/special/x3ap/highway_construction_v1/v2.pbb` (632: `construction_signs.tga`, `warning.tga`, `decoflare.tga`, `poslight.tga`, 158 each), x3ap ships, props | material originals `5f82ecacd39529cd`, `6733b119142c8d42`, `fffdabd910793aba`, `496049cec2066ed3`, `e6794b6ec37ff71a`, `f1b0e820c7b488c3` (prof 3_0) |
| `standard_lighting.fx` | 151 | cutscene warp tunnels (`fx_warp_tunnel.tga`, `objects/cut/06300/06302/06305`), ANIMPROPS and ship position lights, **6 materials inside `objects/effects/engines/`** | material originals `0c1f3f0f440e4a0c`, `7c83ed50c9894e44`, `99153c144030c396`, `64bac8bb307eb896`, `c1452981fd0bff64`, `e70adc744a38ca59` |
| `effects.fx` | 111 | as the census above | `8360f422de08b5bd` (covered) |
| `engine.fx` | 22 | engine glows | `8360f422de08b5bd` (covered) |
| `gui2d.fx` | 11 | menu markers, `objects/Interface` | `0a523f33ac47ae05`, `6109cf64c03529dd` (UI) |
| `glass.fx` | 6 | ship glass | material original `a66fb1981ba755b2` |
| `nebula.fx` | 3 | nebula sprites | `6109cf64c03529dd` |
| `planet_v.fx` | 3 | planet surfaces | not in any table |

So position lights, deco flares, warning signs and warp tunnels are genuine
additive emitters whose pixel program is a *hull* program in the 108 material
originals; `--emission-source-gain` cannot reach them, and gaining a shared hull
program would also gain its opaque draws. The larger gap remains the
**screen-blend** half of `8360f422de08b5bd` (engine glow, bolts, beams, impacts)
and the screen-blend populations of `XT_standard_lighting.fx` (172 materials),
`nebula.fx` (14) and `argon.fx` (8).

## Open unknowns

- What selects `<name>_0000` / `<name>_0001` effect files. No material names
  them, the executable has no `_%04d` format string, and none of their unique
  programs has been created in 81 sessions. Until this is resolved, 18 of the
  20 pairs and 8 of the 9 bullet pairs have no established user.
- The users of the `INSTANCE` pair `89193868` / `8360f422`: created in 69
  sessions, never seen in an admitted or refused draw record. The instanced-mesh
  path's flag `group[+0x18] & 0x20000000` (bullets vs plain `INSTANCE`) and the
  source of the material name behind `group[+0x1c] & 0x10` are still open
  (`effects-engine-remaining-emission.md`).
- Whether the ship flown in run 83 has an ONE/ONE engine material. Naming the
  ship in a future run and matching it to a `fx_engine_*` body would settle it.
- Old-format text bodies (`objects/effects/explosions/explosion_plane.pbd`,
  `exp_sparks.pbd`) carry a `MATERIAL6:` record with no effect-source name and
  no `g_*` state; which effect the loader gives them is not established.
- The `refused_state` population (101,263 in run 83) is attributed to late
  overlays by shape only; the state sample lines do not carry an owner.

Local derivation, untracked: `/private/tmp/.../scratchpad/{arch.py,fxscan.py,
matscan.py,obj_fx.json,materials.json,census.txt}` and
`/tmp/x3-shader-hash-sessions.txt`. No game asset, program byte or decompiler
output is reproduced here.
