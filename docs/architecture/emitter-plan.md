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
| Position lights, deco flares, warning signs, warp tunnels | hull programs (`standard_lighting`, `XT_standard_lighting`) with ONE/ONE materials | additive | Later: blend-keyed admission of covered hull programs (a "point light" class); not reachable by the effects gain |
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
4. **Bloom**: `--bloom-source-clamp` bounds what a gained emitter feeds the
   halo; gains then change brightness and exposure but not halo size
   ([bloom falloff](bloom-falloff.md)). The user brackets 1.0 / 2.0 / none.

## Not planned

Linear emissions in the bracket shape, blanket conversion, per-material art
review and `.fx` archive edits ([mod compatibility](mod-compatibility.md)).
