# Emitter plan (HDR emitters under original shading)

Goal: emitters brighter than 1.0 in the native code-value space so exposure and
bloom treat them as light sources, without linear materials and without
brackets ([cost note](linear-emission-cost.md)). Attribution follows the
[effect shader users](../reverse-engineering/effect-shader-users.md) census
(2026-09-17): one live effects program pair draws almost every emitter, so the
question is which *materials* get a gain, not which shaders.

## Population and decision

| Emitter | Drawn by | Blend | Decision |
| --- | --- | --- | --- |
| Weapon bolts | bullet pair `ec1f5c4a…` (instance geometry) | screen | Done: `--screen-emission-additive G` (+ `--screen-emission-additive-alpha`) |
| Ship engine glow, thruster flares (122 materials) | effects pair `d5e1c753…/8360f422…` | 92 screen / 30 additive | Gain: `--emission-source-gain G`, screen draws substituted additive (in flight) |
| Jump gate whirl/streaks | effects pair | additive | Gain (same option; user saw it respond) |
| Beams (`fx_beams_diff`) | effects pair | screen | Gain (same option) |
| Sun flare sprites (62) | effects pair | 60 additive | Gain (same option); watch Auto exposure, the sun is the meter's anchor |
| Impact / shockwave / shield-hit sprites | effects pair | screen | Gain (same option); transient, judge in combat |
| Dock tunnels, station shields, Goner beams, hive lightning | effects pair | mixed | Gain (same option) |
| Position lights, deco flares, warning signs, warp tunnels | hull programs (`standard_lighting`, `XT_standard_lighting`) with ONE/ONE materials | additive | Phase 3: same `--emission-source-gain G` over twelve covered hull programs, opt-in with `--hull-emitters`, blend-keyed on ONE/ONE, whole-output gain; wired in the proxy (2026-09-17), first user bracket pending |
| Explosions, particles | unwitnessed program | unknown | Capture one in the next combat run; if it is the effects pair it is already covered |
| Nebula fog (`nebulafog`, 277 materials), nebula stars, planet atmosphere | own programs | screen / alpha | Not emitters: no gain |
| Alpha-blended smoke, dust, trails | SRCALPHA materials | alpha | No gain (they are occluders, not sources) |
| GUI (`gui2d.fx`), glass | own programs | additive | No gain |

## Phases

1. **One gain for the effects program** (in flight): `--emission-source-gain G`
   over every draw of the effects pair, screen draws substituted additive so a
   gain above 1 cannot invert the destination; one hotkey (Ctrl+Shift+F6). The
   user brackets G at 2 and reports any category that looks wrong.
2. **Per-category gains only if phase 1 shows a need**: the effects pair
   cannot be split by shader, so a category table would key on the bound
   texture identity (engine, gate, sun-flare, weapon, misc textures are
   distinct `dds` names in the archive). Design note first; not started.
3. **Hull-program emitters** (position lights, signs, warp tunnels): admission
   keyed on blend state (ONE/ONE on a covered hull program) rather than on the
   program, one MUL variant as today. Follow-up after phase 1.
   *Status (2026-09-17, wired)*: `linear_emission_hull_source_gain_variant`
   covers the twelve exact `standard_lighting` / `XT_standard_lighting`
   originals of the ONE/ONE census. The sampler question is settled by an
   archive check: the gained `texld r0` sampler (s2 in DEFAULT/2s, s3 in
   BUMPMAP) is `t_LightMapTexture`, and every emitter-art material
   (`poslight`, `decoflare`, `warning`, `construction_signs`,
   `fx_warp_tunnel`) binds that slot to `fx\NONE_BLACK.DDS` or NULL while the
   art sits in `t_DiffuseTexture`, so an r0-only gain multiplied black. The
   transform is therefore the whole-output fallback: `def c223 = (G,0,0,0)`
   at the first declaration, the final colour instruction (`add oC0.xyz, r1,
   r0` / XT `mad oC0.xyz, r1, r2.z, r0`) redirected to write `r0.xyz` with
   its opcode, `_pp`, mask and operands kept (r0 is dead after it: only the
   alpha MUL follows), and one `mul oC0.xyz, r0, c223.x` with the original
   destination token in its place. The additive draw contributes only its
   emitter to the frame, so scaling everything it adds is exact; the native
   alpha `mul oC0.w, r2.w, v0.w`, every word before the site, gain-1 byte
   identity, the c223 collision walk and the 512-slot budget are kept.
   Proxy route (`src/proxy/motion_output.cpp`): one variant per covered
   original at registration (`hull_emission_variant` line, cached, fail
   closed: a covered program whose creation failed is counted
   `refused_variant` per draw), per-draw admission only when the draw-time
   blend shadow reads ADD ONE/ONE with sRGB write off
   (`linear_emission_hull_source_gain_blend`; opaque and screen draws of the
   same program are `refused_blend`, a routed draw `refused_routed`), the
   variant bound for that draw and the application's program restored after
   it; the same `--emission-source-gain G` carries the gain,
   `--hull-emitters` opts the population in (`X3M_HULL_EMISSION_GAIN`,
   refused in the DLL without the effects gain) and Ctrl+Shift+F6 toggles both
   populations. Telemetry: `hull_emission_frame ... admitted= refused_blend=
   refused_variant= programs=` (bit per admitted program) per Present, the
   first admission per program (`hull_emission_program`) and capped refusal
   samples. Default path: one bool test per draw. Evidence: host oracle, the
   `run_linear_material.py --hull-emission-gain` GPU slice (diffuse-authored
   emitter face x G at 0 FP16 codes, black face identical) and the
   `run_sun_share_live.py --case hull_emission` live case
   ([screen-emission ledger](../verification/screen-emission.md)). The
   first user bracket at G = 2 is the open step.
4. **Bloom**: `--bloom-source-clamp` bounds what a gained emitter feeds the
   halo; gains then change brightness and exposure but not halo size
   ([bloom falloff](bloom-falloff.md)). The user brackets 1.0 / 2.0 / none.

## Not planned

Linear emissions in the bracket shape, blanket conversion, per-material art
review and `.fx` archive edits ([mod compatibility](mod-compatibility.md)).
