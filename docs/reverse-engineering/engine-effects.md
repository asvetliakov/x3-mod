# Ship engine effects: glow jets, emitter effects and particle trails

2026-10-01. Read-only study for the effects design (unknown 5 of
[effects-modernisation-opus.md](../architecture/effects-modernisation-opus.md), "whether engine-glow scale or alpha
tracks throttle"). Installed `X3AP.exe` SHA-256 `fdbf3418…f8ab` (preferred VAs, base `0x00400000`); a fresh
Ghidra 12.1.3 import in the session scratchpad with `tools/analysis/X3DecompileFunctions.java` and
[`X3DecContaining.java`](../../verification/results/engine-effects/X3DecContaining.java) (decompiles the function
containing an address, with its callers), checked against an `i686-w64-mingw32-objdump -M intel` listing; the
Mayhem 3 install (`installed`) and its stock layers (`STOCK_AP_CATALOGUES`, `stock`) read through
`tools/analysis/sector_fog_census.Assets` and `tools/analysis/bob1.py`. No game launch, no Wine, no build. Raw
decompiler output, the listing and extracted game files stay untracked. Data census:
[`engine_effects_data.py`](../../verification/results/engine-effects/engine_effects_data.py) →
[`engine_effects_data_out.txt`](../../verification/results/engine-effects/engine_effects_data_out.txt) (about 1 s).
Tags: **[m]** measured (instruction bytes read, data counted), **[i]** inferred (decompiler reading or reasoning not
checked instruction by instruction), **[u]** not established.

**Answer.** A ship's engine look has three independent parts, all hung on the ship's scene:

1. **Glow jets** — scene parts whose body is listed under `SBTYPE_JET` in `types/Bodies` (every
   `objects/effects/engines/*` body). Each is an ordinary render node, a direct child of the ship's root node, drawn
   through the material path `0x004c0150` (`engine.fx`/`effects.fx`, the `d5e1c753`/`8360f422` pair). Its colour is
   the body material (texture), nothing in a ship record. **Throttle drives its length, not its alpha**: every frame
   `0x004596e0` sets the node's **model-z scale** (`+0x88`, 16.16) to `0.25 + 1.75 · clamp(speed / max speed)`,
   rate-limited to 0.004 per ms, plus +1.0 per steering axis and +4.0 for braking on steering/brake-flagged jets; x/y
   scale stay 1.0. Visibility is a per-part mode word (the scene's `C` field). No alpha, colour or brightness term
   depends on speed; hue/saturation shifting is switched off for jets.
2. **Emitter effects** — `types/Effects` rows (TShips column 11) spawned at the `fx_engine_emitter` dummies: a
   camera-facing `B_GLOW` sprite (`objects/v/00011`/`00213`) and a lens flare, both `EEDF_SPEEDDEPENDENT`, sized
   `min + (max − min) · v / (v + halfspeed · vmax)` relative to the ship.
3. **Trails** — `types/Particles3` particle generators (TShips column 49) at the same emitters, gated by a global
   engine-flag bit; particle size, initial speed and spawn spacing interpolate on `speed / max speed`; drawn by the
   particle renderer `0x004bf4c0`. Mayhem 3 rewrites `Particles3` and uses material 406 (`tex\true\406`) for every
   ship trail; it adds no trail scene parts.

The proxy can read a jet's throttle without any memory access: the glow draw's world matrix carries the node's
per-axis scale, so the model-z basis length over the model-x basis length is the engine's own speed ratio signal.
The engine never tests the glow draw's result, so a suppressed glow draw is safe.

## 1. Selection and loader chain

| address | role |
| --- | --- |
| `0x00435b22..0x00435be8` | `types/Bodies` (`addon\types\Bodies`, string `0x0055f914`): per `SBTYPE_*` section (symbol table `0x00552678`: 2D 0, 2D2 1, 2DY 2, LOGO 3, FC 4, TURRET 5, **JET 6**, RADAR 7, CONTAINER 8, DISPLAY 9, DOCKPOINT 10, FACECAMERA 11, **SMALLJET 12**) a counted list of body ids (names via `0x0046e400`) into `0x006070ec + 4·type` [m] |
| `0x00434620` | node classification by model id `+0x140`: JET list `0x00607104` → **`node+0x130 \|= 0x4000001`** (`0x00434708`); SMALLJET `0x0060711c` → `+0x130 \|= 0x4000000`, `+0x1d8 = 5` (`0x00434730`); TURRET → `+0x1d8 = 10` [m] |
| `0x0043d1d0` | scene-part builder ([ship-scene-parts.md](ship-scene-parts.md)): node `0x00486d10`, attach `0x00489f20` under the root (or under part `F` via `0x00486fd0`, `0x0043d3c5`), `0x0048eb40` copies the part index to `+0x25c`, the scene id to `+0x258` and **part record `+0x38` (the text `C` value) to `+0x260`**; body via `0x00487e30` (`+0x140` model id, `+0x70` = first record `+0x14`, the body scale) [m] |
| `0x0043d80f..0x0043d818` | a jet child sets the owning object's `+0x44 \|= 0x20` [m] |
| `0x0043d896..0x0043d8a8` | a jet gets `+0x12c \|= 0x8000000` (no collision tree is built) [m] |
| `0x0043d8eb..0x0043d905` | a jet starts at scale `(1, 1, 0x147/65536 = 0.005)` via `0x00488270` [m] |
| `0x00491f1f`, `0x00491fb9` | text scene loader: staging record base `0x00609958` + part·`0x58`; `C n;` writes `+0x38` (default 0), `F n;` writes `+0x3c` (default −1); the staging stride (`0x0058`, `0x00491e56`) equals the scene record stride read by `0x0043d1d0`/`0x0048eb40` [m]; the staging → record transfer (`0x004926f4` → `0x004901a0`) [i] |
| `0x004373e9..` | `TShips` (type 7, record stride `0xdb8`, table `0x00606fd4`, count `0x00607054`): col 7 → `+0x44` speed, col 11 → `+0x54` **engine effect** (Effects id), col 12 → `+0x58`, col 14 → `+0x60`, col 16 → `+0x68` scene, col 49 → `+0xc4` **trail** (Particles3 id), `+0xa8` initialised −1 (flight variation index, not a column) [m]; column numbering checked against the Raptor row (col 52 = `OBJ_SHIP_M1`) [m] |

So **the glow is selected by the ship scene, not by a ship record**: a scene part `P n; B effects\engines\<name>; C <mode>`
whose body is in the JET list. TShips col 11 selects only the emitter effect, col 49 only the trail. No reader of
TShips `+0x58` (col 12, "engine glow" in editors; Mayhem zeroes it on 518 of 525 rows) was found near the TShips table
[i: heuristic scan of `+0x58` reads after `0x00606fd4`/`*0xdb8`, none]. Name → body: `objects\effects\engines\<name>`
through the ordinary body resolver (`bob1.resolve_body`, body-format-bob1.md §7).

**Colour.** The glow material is a `engine.fx`/`effects.fx` effect material with a diffuse texture (`fx_engine_red.dds`,
or the animated `-79` loop on 120 installed materials [m]). The renderer skips the per-node `g_Hue`/`g_Saturation`
uploads (node `+0x1c4`, `+0x1c8`, handles `+0x16c`/`+0x168` of the effect wrapper, names resolved at `0x004c1d0e`/
`0x004c1d23`) for nodes with `+0x130 & 0x4000000`, i.e. every jet (`0x004c2e5e`, `test [eax+0x130],0x4000000; jne`) [m].
So a jet's colour is the material alone; no ship record carries a colour. The emitter effects choose colour by row:
Mayhem's rows 700–729 ("Engine, Emitter – Green/Lime/…/White, Small/Medium/Large") differ only in the lens-flare number
(9…38) and size [m].

### Data [m]

| | installed (Mayhem 3) | stock |
| --- | --- | --- |
| `types/Bodies` JET entries (named under `effects\engines`) | 269 (247) | 247 (225) |
| text ship scenes / with engine parts | 861 / 622 | 385 / 214 |
| glow parts by `C` | `0x7001` 2,840, `0x0` 7, `0x3001` 6, `0xf001` 5 | `0x7001` 416, `0x3001` 6, `0xf001` 5, `0x0` 5 |
| `fx_engine_emitter` parts by `C` | `0x1` 826, `0x0` 664, `0x2` 18, `0x3` 1, `0x4` 1 | `0x1` 162, `0x0` 66, `0x2` 12, `0x3` 1, `0x4` 1 |
| glow parts with identity key rotation | 2,627 of 2,858 | 367 of 432 |
| engine parts with an `F` (parent part) field | 0 of 4,399 | 0 of 705 |
| TShips col 11 | 32 distinct, top 727 (65), 724, 722, 709 | 22 distinct, top 5 (293), 11, 3, 13 |
| TShips col 49 (trail) | 45 distinct, 0 on 145 rows | 8 distinct, 1 on 276 rows, 0 on 116 |
| `objects/effects/engines` stems | 251 (22 Mayhem-only, e.g. `fx_engine_xtc_*_big4`, `_verytiny`) | 229 |
| shared stems whose resolved bytes differ | 100 of 229 (Mayhem overrides) | — |
| resolved materials (effect, blend) | `engine.fx` ONE/INVSRCCOLOR 163, `effects.fx` ONE/INVSRCCOLOR 52, `engine.fx` ONE/ONE 21, `effects.fx` ONE/ONE 8, `standard_lighting.fx` 17, `terran.fx` 4 | 99 legacy (pre-effect) materials, `effects.fx` ONE/INVSRCCOLOR 52, `engine.fx` ONE/INVSRCCOLOR 41, `engine.fx` ONE/ONE 21, … |

Worked example: the Raptor scene (`addon/06.cat` `split_m1_raptor_scene.pbd`) has three glow parts
(`fx_engine_xtc_red_big2` ×2, `fx_engine_xtc_red_big3`, `C 28673` = `0x7001`) at z ≈ −520,000 and three
`fx_engine_emitter` dummies (no `C`) 10,000 units further forward; its TShips row has col 7 speed 77,440, col 11 = 720
("Red, Large"), col 12 = 0, col 49 = 0 (no trail).

## 2. Per-frame drive (unknown 5)

Main loop `0x00403840` → `0x00403b17` → `0x0043a360` → `0x0043a3bf` → **`0x004596e0`** (sector object update: 32
object lists at `param+0x50`, stride `0xc`). Object fields used: `+0x10` current speed, `+0x24/+0x28/+0x2c` rotation
rates, `+0x48`/`+0x4a` type/subtype, `+0x50` extension, `+0x70` root render node, `+0xe8` previous speed,
`+0xdc/+0xe0/+0xe4` previous rotation rates, `+0xa8` last update time. The block runs only when `*(*0x0060850c) != 0`
(`0x0045ac6d`) [m]; meaning of that gate [u].

| address | operation [m unless marked] |
| --- | --- |
| `0x0045aca3..0x0045ace4` | `vmax = TShips[sub].+0x44` (`0x0043a690`, AX = type, CX = subtype) × `(1 + ext+0x274 / 65536)` for ships; `ext+0x274` read as the engine-tuning bonus [i] |
| `0x0045ace8..0x0045ad18` | walk the **direct children** of the root (`root+0xc` list); `[esp+0x28]` = child node |
| `0x0045ad74` | jet test `+0x130 & 1` |
| `0x0045ad85` | `C = node+0x260` (the part mode word) |
| `0x0045ad8b..0x0045adb4` | `base = remap(speed = obj+0x10; [0, vmax] → [0x4000, 0x20000])` through `0x0042fb20` (linear, clamped at both ends: `0x0042fb3d..0x0042fb4c`) = **0.25 … 2.0** |
| `0x0045adc6..0x0045aef8` | once per object: `accel = (speed − obj+0xe8)·1000/dt`, angular accelerations `(rate − previous)·1000/dt` per axis, each zeroed below `0xccc` (0.05) |
| `0x0045af0a..0x0045af5d` | main jet (`C & 1` or `C == 0`): target = `base`; shown iff `C & 0x1000` and not `obj+0x44 & 0x4000000`, else `+0x12c \|= 0x100000` (hidden) |
| `0x0045af61..0x0045afae` | every jet: if `C & 0x8000` or `C == 0`, shown iff `C & 0x1000` **and** `obj+0x44 & 0x4000000` (alternate nozzle set, overriding the main rule); otherwise +4.0 when `C & 2` and `accel < 0` (braking). Non-main jets start from `0x28f` (0.01) instead of `base` (`0x0045ada2`) |
| `0x0045afb8..0x0045b006` | +1.0 for each matching steering bit: `0x4`/`0x8` yaw ±, `0x10`/`0x20` pitch ±, `0x40`/`0x80` roll ± (sign of the angular acceleration) |
| `0x0045b00c..0x0045b03c` | **rate limit**: `|z_new − z_old| ≤ dt_ms · 0x106` (0.004 per ms; 0.25 → 2.0 takes 437 ms); `dt_ms = time − obj+0xa8`, clamped to 0…1000 |
| `0x0045b042..0x0045b08b` | non-main jets only: hidden while `z ≤ 0x28f`, shown above (children follow) |
| `0x0045b08d..0x0045b09a` | `0x00488270(node, 0x10000, 0x10000, z)`: `+0x80/+0x84/+0x88` = per-axis scale, `+0xa0` radius = `+0x70 · max(scale)`; skipped when `z` is unchanged (`0x0045b03e`) |

Formula for the ordinary main glow (`C = 0x7001`, 99.4 % of installed glow parts):
`z_target = 0.25 + 1.75 · clamp(speed / vmax, 0, 1)`, `z ← z + clamp(z_target − z, ±0.004·dt_ms)`, scale = `(1, 1, z)`
[m: instruction-level]. **No alpha, colour, brightness or texture term depends on speed** [m for this function and for
the jet branch of `0x004c0150` at `0x004c2e5e`; i for the material parameters, which are constant per material].
Throttle is seen only as the actual speed `obj+0x10`, not the throttle setting [i].

Mode word bits seen in data: `0x7001` = main | `0x1000` visible | `0x2000` no trail | `0x4000` no emitter effect
(§3); `0x3001` = main, visible, no trail, *with* emitter effect; `0xf001` = alternate-mode nozzle. `0x2000`/`0x4000`
are read only by `0x00414590` [m]. A jet without `0x1000` is never shown by this drive: the `fx_engine_emitter` dummies (`C` 0
or 1, a 12-face `standard_lighting.fx` ONE/ONE body) only mark positions for §3 [m: rule; i: that none of them ever
draws].

Other writers of jet state:

| address | condition | effect [m] |
| --- | --- | --- |
| `0x00451cd5..0x00451d0f` (`0x004517a0`, from `0x00443d4d`, `0x00466e9c`) | object placement | every jet child to `(1, 1, 0.01)` and hidden; the per-frame drive then grows it |
| `0x0043c4c1..0x0043c4ed` (`0x0043c2d0`, from `0x00466e54`) | ship objects instanced from a cut scene, except cuts `0x185b`/`0x1864` | jets hidden and their JET bit cleared (never driven again) |
| `0x0045f890..0x0045f908` (`0x0045f270`, from `0x0045ff45`) | uniform object rescale; with its second condition [u] | jets are excluded from the rescale; under the condition hidden and the JET bit cleared |
| `0x00413394` (`0x004132d0`, from `0x0041575c`) | ship break-up: debris objects per child | jets spawn no debris |
| `0x004439c1`, `0x0044424a`, `0x004505aa` | `obj+0x44 & 0x4000000` set from `types/Flight` (`0x00606fac`, stride `0x50` = 5 modes × `0x10`) flags bit 1 of mode × variation `TShips+0xa8`; the stock and installed `Flight` table has one variation with the flag in "Travel" and "Decouple" | swaps main and `0x8000` nozzles; cleared when `+0xa8 = −1` |

SETA: the drive uses game-time deltas (`*(0x00606f34)+0x718`), clamped to 1 s, so a faster clock only shortens the
ramp per frame; the target ratio is unchanged [i]. Docked ships: not traced (they are not drawn) [u]. Boost or
afterburner: no such term in the drive; the only speed multiplier is `ext+0x274` [m for the code, i for its meaning].
Per ship the root node also gets `+0x124 = remap(max |velocity component|; [0, vmax] → [TShips+0x60, TShips+0x64])`
(cols 14/15, "sound vibration min/max" in editors; `0x0045b24c..0x0045b2ad`) [m]; it is not read by the material path
and reads as an engine-sound value [i]. Children flagged `+0x12c & 0x800` get a uniform scale
`remap(speed; [0, vmax] → [root+0x70/4, root+0x70/2])` (`0x0045ad24..0x0045ad6f`) [m]; which parts carry that flag is
not established [u].

## 3. Emitter effects and trails

| address | role |
| --- | --- |
| `0x0045ac7d` → **`0x00414590`** | per frame, per ship: `eff = TShips+0x54`, `trail = TShips+0xc4`; for each direct child jet with `C & 0xffe == 0`, index `k` = ordinal among jet children: unless `C & 0x4000`, `0x004148a0(0, k, eff, obj, …, &pos)` (find-or-create the effect instance in list `0x0057b0f8`, key (obj, 0, k)); unless `C & 0x2000` and only if `*(0x00606f34)+0xfc & 0x40000000`, `0x00412d70(k, trail, obj, &pos)` [m: decompiler, offsets checked at `0x0041472f`, `0x004147c4`, `0x004147ff`] |
| `0x00414cf0` (from `0x00416750`) | effect element update; `EEDF_SPEEDDEPENDENT` (`flags & 1`) size = `max − (max − min)·h/(h + |v|)`, `h = halfspeed · vmax` (`0x00450b30`), × `(1 − rnd·rand)`, × object size `+0xa4` when `EEDF_RELATIVESIZE`; written as uniform node scale `0x004880e0` [i] |
| `0x005530b8..0x005531a0` | `EEDF_*` name/bit table (`SPEEDDEPENDENT 1`, `RELATIVESIZE 2`, `LENSFLARE 4`, … `PUSH 0x10000000`) [m] |
| `0x00412d70` | trail generator per (obj, k): node in list `*(0x00608518)+0x631c`, registered handle (`0x004efcc0`), particle type `*(0x00608518)+0x6328 + id·0x10`; position refreshed every frame [i] |
| `0x00416750` (main-loop object update, `0x00403f2a`) `0x00416c77..0x00416db8` | when the engine-flag bit is set: generator `+0x50 = speed / vmax` (16.16, **unclamped**), `+0xa0` = root render context, then `0x0046c170` emits [i] |
| `0x0046c170` | per emitter definition: size `= lerp(MinSize, MaxSize, ratio)`, initial speed `= lerp(MinSpeed, MaxSpeed, ratio)` (not for `PEDF_TRAIL`), spawn interval ∝ `Density · size · 1000 / (emitter speed term)` × random; emits only while a camera node (`+0x270 & 0x4001`) is within `size · 1024` units (Chebyshev) [i] |
| `0x0046ba20` (from `0x00470e17`) | `types/Particles3` loader, ≤ 100 ids, entry `0x80` bytes: MatID, uv rect, Density, RndVariation, Dir, Min/MaxSpeed, Lifetime, Min/MaxSize, keyword options; `PEDF_2D 1`, `PEDF_TRAIL 2` (`0x0054f0f0`) [m] |
| `0x00472307` → **`0x004bf4c0`** | particle renderer (view flag `0x4000`): effect `particles`, technique `BASE_NoLight`, one `DrawPrimitive(TRIANGLELIST)` per material batch (per-view list `+0x3c`, count `+0x648`; material table `0x00608db0`, stride `0x3c`, texture `+0xc`, flags `+0x10` select the blend) [i]; which function fills the batches from the particle pool is not traced [u] |

Draw paths: the **glow** is the scene node through `0x0047d9c0`/`0x004c4fc0` → `0x004c0150` (material dispatch, the
existing object scope). The **emitter sprite** (`objects/v/00011` or `00213`: a 4-point legacy `MATERIAL3` quad
`B_GLOW`, texture id 369/368, size 600 [m]) and its **lens flare** belong to the effect instance, not to the ship
scene; how the legacy-material sprite is drawn (which pair) is not traced [u]. The **trail** is particle renderer
geometry, one draw per material per view [i].

What drives trail length and alpha: particles are left behind at the emitter's world positions; length ≈ lifetime ×
ship speed, width = the speed-interpolated size, fade from lifetime and `fadeduration` [i]. No trail exists below the
camera distance gate or when the engine-flag bit is clear [i]; which option sets the bit (`0x00497c4d` writes the
whole word from a command message) [u].

Mayhem 3 [m]:

| item | installed | stock |
| --- | --- | --- |
| `types/Particles3` | `addon/07.cat` `bf9a79f8…`, 62 ids grouped M5 / M4 / M3 / TS-TP-M6-M8 × race, then drones, laser and missile trails; no `PEDF_TRAIL` | `addon/01.cat` `b97b50b8…`, 40 ids, 6 `PEDF_TRAIL` |
| ship trail material | every in-use ship trail id uses MatID **406** (`tex\true\406.jpg`, 512², `MPF_DESTINATIONBLEND`, grey 0x80); e.g. id 34: two emitters, size 4,000–12,000 and 1,429–4,286, lifetime 1,500 / 750 ms, density 0.4 | id 1 (276 ships): MatID 1198 `others\trail1_diff` (256²), size 1,000–3,000, lifetime 500 ms |
| `types/Effects` engine rows | 700–729: sprite body 11 (size 0–0.065/0.021/0.015 by Small/Medium/Large) + lens flare 9–38, halfspeed 0.4 | 4–13: sprite 11/213 and/or an `EEDF_LIGHT` element (bodies 115–120), flares commented out; row 5 (293 ships) is the light element only |
| trail origins | the 1,510 `fx_engine_emitter` dummies (`C` 0 or 1) | 242 emitters |

Mayhem adds no trail scene parts and no new part type; it adds glow bodies (22 stems), overrides 100 stock glow bodies
(stock legacy materials become `engine.fx` ONE/INVSRCCOLOR), puts glow and emitter parts in 622 scenes, and switches
trails on for 380 of 525 ships through col 49 [m].

## 4. Identity at draw time

| fact | source |
| --- | --- |
| The glow draw is one invocation of `0x004c0150` with the **jet node** at `ESP+8`, inside the existing object scope (call `0x004c5228`, [object-identity.md](object-identity.md)) | [m] call chain; the jet has no special path (`+0x130` bit 0x200 clear, so `0x004bdee0` builds the ordinary world) |
| The jet node is a scene-part node of its own: `0x00486d10` allocation, registered handle `+0x28`, model id `+0x140` = the JET body id (a named body, so it has a body-table slot, unlike the inline dock parts) | [m] |
| Parent `+0x18` = the ship's **root node**: every engine part lacks `F` (4,399 / 705 parts), so `0x0043d3c5` keeps the root as parent; `0x004596e0` and `0x00414590` walk only the root's direct children | [m] data + code |
| Node → game object: none found. `0x0043ffa0` stores `obj+0x70 = root` and zeroes `root+0x20`; no node field receives the object | [i] constructor read only |
| `+0x88` (16.16) = current z-scale, `+0x80 = +0x84 = 0x10000`, `+0x260` = mode word, `+0x130 & 0x4000001` set, `+0x12c & 0x100000` clear while drawn | [m] writers above |

**Safest read path: no read.** `0x004bdee0` (`0x004be253..0x004be3a9`, ordinary branch) builds the world rows as
basis `+0xc0/+0xd0/+0xe0` × `+0x70 · (+0x80, +0x84, +0x88)/65536` × context scale; translation `+0xb0..+0xb8` [m]. So in
the glow draw's world constants (c4–c6) the model-z basis vector is `+0x88/+0x80` times longer than the model-x basis
vector, i.e. `ratio = |world z axis| / |world x axis| = z`, and `clamp(v/vmax) = (z − 0.25)/1.75` for a main jet,
rate-limited like the engine's own glow [i: which index order c4–c6 use for the 3×3 block must be read from a capture].
This works for every ship, needs no hook, no private layout and no Windows/Wine difference.

Second choice, inside the object scope: read `node+0x88`, validate `node+0x130 & 0x4000001 == 0x4000001`,
`+0x80 == +0x84 == 0x10000`, `+0x18` non-null, and for a plain main glow (`+0x260 == 0x7001`, no steering or brake
bits) `0x147 ≤ +0x88 ≤ 0x20000` (other modes can reach 9.0: base 2.0 + brake 4.0 + three steering axes). That is
the same number as the matrix ratio. Actual speed and max speed need the game object, which the node does not
reach: for the own ship the existing `object_capture::own_ship` walk gives `obj` (`cockpit+0xc`), then
`obj+0x10` (speed), `TShips[obj+0x4a]+0x44` (`*0x00606fd4 + sub·0xdb8`) and `(obj+0x50)+0x274` [i]; for NPC ships a
root → object map would have to be built from the sector lists or from an update-site hook.

Optional update-site hook (not needed for the matrix route), **`0x0045b09a`**: `E8 d1 d1 02 00`, a 5-byte `CALL rel32`
to `0x00488270` (`thiscall`, ECX = jet node, three stack args, `ret 0xc`) [m]. At the call EBX = game object, EDX =
ECX = node; in the caller frame before the three pushes (`0x0045b08d`) `[esp+0x20]` = mode word, `[esp+0x24]` = dt,
`[esp+0x30]` = base z, `[esp+0x34]` = vmax [m]. After return `0x0045b09f` reloads EAX and `0x0045b0a3` sets flags, so
EAX and EFLAGS are dead; `0x00488270` recurses into flagged children through its own call at `0x004882f5`, not through
this site, so a redirect stub is not re-entered; the site runs on the game-update path, only when `z` changes
(`0x0045b03e`). A call redirect that forwards the four registers/arguments unchanged and records (EBX, ECX, z) is
boundary-safe; preserving LastError and x87 state is the stub's duty as at `0x004c5228`.

## 5. Suppression safety

- The draw at `0x004c403c` (`call ecx`, the draw helper) is followed by `mov edx,[ebx]` / `mov eax,[edx+0x108]`
  (`EndPass`) at `0x004c403e`: **its EAX is never tested** [m]. A suppressed draw returning `S_OK` is invisible to the
  engine, as for the other effect draws (emission-draw-order.md "Scoped invocation").
- X3AP creates no D3D query at all (lens-flare-visibility.md §1, 0 `CreateQuery` sites [m]); lens-flare visibility is
  a CPU collision probe, and jets carry `+0x12c & 0x8000000` (no collision tree, `0x0043d8a3`) [m], so suppressing
  the glow does not change any flare or occlusion result [i].
- Debris and one traversal skip jets: `0x00413394` (break-up debris) and `0x0047f8fa` (`0x0047f8a0`, reached from
  `0x00493b40`, role not traced [u]) both skip `+0x130 & 1` [m].
- Culling: jets go through the ordinary per-node pass (`0x0047cfe0`) with their own radius `+0xa0`, which `0x00488270`
  recomputes from the largest axis scale, so a long glow stays in view longer [m]; nothing else reads the glow's
  draw. Suppressing it leaves the emitter sprite, the lens flare and the trail untouched: those are separate draws
  (effect nodes and the particle renderer) and must be suppressed separately if replaced [i].
- Hue/saturation: not uploaded for jets (`0x004c2e5e`), so a replacement need not reproduce a race tint [m].

## 6. Nozzle position, axis and size

- **Position**: the scene part key `{ flags; x; y; z; rotation…; …; }` in the ship scene, relative to the root
  (every engine part is a root child). Ship forward is +z (Raptor front turrets at z ≈ +370,000, glow parts at
  z ≈ −520,000) [m].
- **Axis**: the node's model z, i.e. the ship's z for the 2,627 of 2,858 installed glow parts with an all-zero key
  rotation; 231 carry a rotation [m]. The glow mesh decides the direction: of 251 installed glow bodies, 83 extend
  only towards −z (|z_min| > 2·z_max), 168 extend both ways around the node origin (e.g. `fx_engine_xtc_red_big2`,
  z −1…+1 of its radius 9,366; `fx_engine_argon_m3` z −1…0, radius 4,170) [m]. The plume axis is therefore −z of
  the node; the origin is the part position, not the mesh centre [i].
- **Size**: no record carries a nozzle radius. The node's base size `+0x70` is the glow body's scale (LOD 0 value,
  set by `0x00487e30`) [m]; the emitter effects scale relative to the whole ship (`EEDF_RELATIVESIZE`, object `+0xa4`)
  [i]. A nozzle radius has to come from the glow mesh (its x/y extent × body scale) or from the world rows' x/y
  basis lengths at draw time [i].

## Unknown

- The c4–c6 index order of the glow draw (which register holds which world column) and therefore the exact
  expression for the z-axis length; one capture of a glow draw at two speeds settles it.
- The gate `*(*0x0060850c)`, the engine-flag bit `0x40000000` at `*(0x00606f34)+0xfc` (which UI or script sets it),
  and which ships get a flight variation (`TShips+0xa8`).
- The function that turns the particle pool into the renderer's batches, the particle blend per material flag, and
  whether the 256² particle texture of the historical capture (particle-motion-inputs.md) is a trail material.
- How the legacy-material emitter sprite (`objects/v/00011`) is drawn, and the lens-flare path of `EEDF_LENSFLARE`
  elements.
- Consumers of the root `+0x124` value and of TShips col 12; the population of `+0x12c & 0x800` children.
- Docked and SETA behaviour beyond the code reading above.

## Reproduce

```sh
python3 verification/results/engine-effects/engine_effects_data.py \
  > verification/results/engine-effects/engine_effects_data_out.txt        # about 1 s, bottle X3, read-only
# Ghidra (raw output stays local): -import X3AP.exe into a scratch project, then
#   -postScript X3DecompileFunctions.java <out> 004596e0 00414590 00434620 0043d1d0 0048eb40 00488270 \
#     00412d70 00416750 0046c170 0046ba20 004bf4c0 0043a690 00450b30 0043ffa0
#   -postScript verification/results/engine-effects/X3DecContaining.java <out> 4c2e5e 451cd5 43c4c1 45f890 413394
# objdump windows (verification/results/engine-effects/listing_range.py <listing> <start> <end>):
#   0x0045aca3..0x0045b0b0, 0x00434680..0x00434750, 0x0042fb20..0x0042fbc0, 0x004373e9..0x00437730,
#   0x00491efb..0x004920c0, 0x004be253..0x004be3e8, 0x004c2e46..0x004c2f60, 0x004c3ff0..0x004c4070
```
