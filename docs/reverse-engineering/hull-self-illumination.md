# Hull self-illumination: the light-map term in the opaque material programs

Read-only bytecode and capture-log study, 2026-09-18. No Wine command, no game
launch, no source change. Inputs: the immutable local sweep cache
`/tmp/x3-shader-sweep/programs` (751 archive programs) and its `manifest.json`
effect index, the tracked profile tables in `src/renderer/`, and the capture
blocks of two preserved sessions (run 39, run 117). Derived names, offsets and
hashes only; no original shader bytes are reproduced here.

Question: where are station/ship "windows" and "fake lights" produced, and can a
source gain reach them the way `--emission-source-gain` reaches the additive
effects program. Answer: they are a **self-illumination (light-map) texture term
added last inside the opaque hull pixel programs**, in the same draw as the lit
surface. It is separable by a single `mul` on the sampled value. The existing
`--hull-emitters` option cannot reach it, for two independent reasons given in
§3.

## 1. The term

Every covered hull technique declares a `LightMapTexSampler` (CTAB name;
`t_LightMapTexture` in the effect parameter list,
[effects-and-archives.md](effects-and-archives.md)). Its stage is **s2** in the
DEFAULT/no-bump layouts and **s3** in the BUMPMAP layouts, matching the sampler
contract already recorded in
[remaining-hull-materials.md](remaining-hull-materials.md) ("Exact conversion
boundaries"). The sampler order is uniform: s0 diffuse, (s1 bump), specular,
light map, cube, and on XT additionally occlusion and detail.

The light-map fetch is the **last `texld` of the program** and the value it
produces is consumed exactly once, by the **final colour instruction**, which is
the last arithmetic instruction before the alpha write:

| Technique group | tail (derived disassembly, offsets = DWORD index into the original) |
| --- | --- |
| `standard_lighting` / race hulls | `texld rL, v1, s{2,3}` · `lrp r2.w, cG.x, rL.w, r1.w` · `add oC0.xyz_pp, r1, rL` · `mul oC0.w_pp, r2.w, v0.w` |
| `XT_standard_lighting` | `pow r2.z, occl.w, c14.x` · `texld rL, v1, s3` · `lrp r2.w, c4.x, rL.w, r1.w` · `mad oC0.xyz_pp, r1, r2.z, rL` · `mul oC0.w_pp, r2.w, v0.w` |
| XT `terra` variants | `texld rL, v1, s{2,3}` · `mad rL.xyz_pp, r2, r3.w, rL` · `lrp r0.w, c4.x, rL.w, r2.w` · `add oC0.xyz_pp, r0, rL` |

`rL` is the light-map destination register: `r0` in 94 of the 100 programs,
`r1` in the six `terra` rows. So the output is literally
`oC0.rgb = lit_colour (x occlusion^g_MatOcclStr on XT) + lightmap.rgb`, matching
the independently derived algebra of the docking-port pair in
[station-material-distance.md](station-material-distance.md) §"Arithmetic".

Properties established for all 100 programs by walking the token stream:

- **No constant scales the RGB term.** The light-map RGB is added at weight 1.
  The only related constant is `g_EnableGlow` (c3 in DEFAULT, c4 in BUMPMAP/XT),
  which is the `lrp` selector for the **alpha** output only
  (`alpha = lerp(diffuse.a, lightmap.a, g_EnableGlow) * v0.w`). There is no
  day/night, damage or vertex-colour gate on the RGB.
- **Not a vertex-colour term.** The separate per-material emissive
  (`g_MatEmissiveColor`, added into `o1.xyz` by the vertex shader) arrives as
  `v0.xyz`, is `sat`-clamped (`mov rC.xyz_sat_pp, v0`) and then **multiplied by
  albedo**; it cannot exceed 1 and it tints with the diffuse texture. That path
  is not the window glow and is not worth gaining.
- **Liveness.** Between the light-map `texld` and the final colour instruction
  the RGB lanes of `rL` are read by nothing in 94 programs, and by the single
  intervening `mad` in the six `terra` rows. `rL.w` is read by the alpha `lrp`
  in all 100, so any inserted operation must be masked `.xyz`.
- `LightMapGlowIntensity` (8 programs, register c1/c2) belongs to the bloom
  compositor programs (`GlowSamp1`/`SceneSampler`), not to any hull program.

## 2. Population and pinned sites

Of the **108 pixel originals** of the selected 137-stage material set
(`linear-material-profiles.json` 90 + `xt-material-profiles.json` 14 +
`glass-material-profiles.json` 4), **100 carry the light-map term**. The eight
that do not are the four glass programs (`9d49f288800f898d`, `a66fb1981ba755b2`,
`ebc9b2b3f1564e9a`, `f31c9e2701c8eee4`, all four rows of
`glass-material-profiles.json`) and the four asteroid-layout programs
(`517540ae6d5e5410`, `550c2a4d4d3ed70f`, `7a0c3388065bb08d`,
`d44db87778a43b61`).
Across the whole archive, 392 of 751 programs declare the sampler; 353 of those
also declare `g_EnableGlow`.

| Final-colour form | light-map register | programs | gap `texld` → final |
| --- | --- | ---: | ---: |
| `add oC0.xyz, r1, rL` | r0 | 86 | 9 DWORDs |
| `mad oC0.xyz, r1, r2.z, rL` (XT) | r0 | 8 | 9 DWORDs |
| `add oC0.xyz, r0, rL` (XT `terra`) | r1 | 6 | 14 DWORDs |

Stage split: 56 programs at s3, 44 at s2. The highest float constant any of the
100 reads is **c26**, so the c212–c223 reservations remain disjoint.

The insertion sites already exist in tracked tables and need no new work:
`Pixel::texture[bump?3:2]` in `src/renderer/linear_material.cpp` for 88 of them,
`XtPixel::texture[stage]` with `XtPixel::texture_reg[stage]` in
`src/renderer/linear_xt_profiles_inc.h` for the remaining 12 (all XT). All
**100 of 100** also appear as pixel rows in
`src/renderer/motion_output_profiles_inc.h`.

## 3. Why `--hull-emitters` does not touch windows

Two independent reasons, both confirmed here:

1. **Wrong programs.** The twelve programs of
   `linear_emission_hull_source_gain_variant` are the six `standard_lighting.fx`
   and six `XT_standard_lighting.fx` originals of the ONE/ONE census
   ([emitter-plan.md](../architecture/emitter-plan.md) phase 3). Station and ship
   hulls are drawn by the **race** effect files: in the manifest,
   `8759c7838bbc86c2` and `ca6bfa4a6cca7e2a` are `shader/3_0/argon.fb`,
   `63f96eba9eea7880` and `5e0a10fe752b6140` are `argon2s.fb`,
   `462342e3e5781384` is `split.fb`, `3b94320087e81945` is shared by
   `khaak/teladi/teladi_nodiff/xenon.fb`. None of those is in the twelve.
2. **Wrong admission law.** The hull gain admits only ADD ONE/ONE draws; the
   opaque hull draws that carry a real light map are blend-off. And on the
   ONE/ONE emitter materials the light-map slot is the black placeholder, which
   is why that law had to be changed to a whole-output gain in the first place
   ([screen-emission.md](../verification/screen-emission.md), 2026-09-17).

## 4. Runtime evidence that the term is live

Capture blocks join a draw's `ps=` to its `texture stage=` / `texture_desc`
rows. Classifying the light-map stage of every draw whose pixel program declares
the sampler (script kept local; log lines are streamed, never read whole):

| Session | draws on such programs | real mipped light map | 32x32 / 1 level | distinct real textures |
| --- | ---: | ---: | ---: | ---: |
| run 39 (`/tmp/x3-bottleX3-run39/session-20260914-162050-212.log`) | 2,422 | 1,881 | 541 | 75 |
| run 117 (`/tmp/x3-bottleX3-run117/session-20260918-025333-212.log`) | 10,019 | 8,459 | 1,560 | 53 |

In run 39 the light-map texture identity differs from the diffuse identity on
**2,414 of 2,422** draws, and 45 of the 52 distinct models seen have at least one
real-light-map draw. Real bindings are DXT5 with full mip chains at
1024x1024 (2,880 draws in run 117), 1024x128, 512x512, 512x128, 256x256,
128x512, 2048x128 and 2048x2048. The single dominant consumer is the Argon
DEFAULT base program `8759c7838bbc86c2` (4,581 real bindings in run 117).

The same XT program serves both populations: `5f82ecacd39529cd` takes 912 real
and 504 placeholder bindings in run 117 — the placeholder draws are the ONE/ONE
emitter props, the real ones are hull. The docking-port pair
`64bac8bb307eb896` studied earlier binds only the 32x32 identity in both
sessions, so its light map is (at least there) the placeholder, consistent with
the 32x32/1-level row already recorded in `station-material-distance.md`.

What this does **not** establish: the texel content. Nothing here was decoded
from a DDS, so "the windows art is in this texture" is an inference from the
term's position (added after all lighting, unlit, weight 1, distinct art of full
resolution) and not a measurement.

## 5. Cheapest transform

One instruction, identical for all 100 programs:

```
def c223, G, 0, 0, 0            ; at the first declaration
...
texld rL, v1, s{2|3}            ; unchanged, the pinned light-map fetch
mul   rL.xyz, rL, c223.x        ; inserted immediately after it
...                             ; every later word verbatim
```

- Insert **immediately after the light-map `texld`**, which is correct for both
  the 94 direct-addend rows and the six `terra` rows whose intervening `mad`
  consumes the value.
- Mask `.xyz` keeps `rL.w` exact for the alpha `lrp`; the final colour
  instruction, the alpha `mul` and all original words stay byte-identical, so
  `G = 1` is a byte-identity transform and a black light map is bit-identical.
- Cost: **+10 DWORDs, +2 instructions, +1 weighted slot** (the DEF costs no
  slot), the same budget as the existing hull-emission variant. Original hull
  programs are 34–71 slots and the combined motion/fill variants 69–311 of the
  512-slot ps_3_0 budget, so the margin is not at issue. Per-draw cost is zero:
  the variant is chosen at bind time, as with every other variant.
- Constant choice: ps_3_0 has only c0–c223, so **c224 does not exist**; a new
  reservation is impossible. Reuse **c223**, which is already the hull-emission
  gain register and is read by no original (max original read c26). It is a
  shader-local `def`, so the ONE/ONE variant and a self-illumination variant of
  the same original never share one program.
- This is a create-time bytecode transform, not a CPU hook: no instruction
  boundary in the EXE, no register or flag liveness, no reentrancy question. The
  only liveness obligations are the two inside the shader named above.

**Host.** `original_fill_transform` in `src/renderer/linear_material.cpp`
(exposed as `linear_material_original_fill_pixel_variant`, the `--original-fill`
machinery) is the natural host: it already walks the original word by word,
already inserts DEFs at `s.first_declaration` and fragments at profile-pinned
sites, already validates the resulting structure and slot budget, and already
resolves both the hull `Pixel` and the `XtPixel` profile for the program. It
keeps native shading (it excludes `X3M_LINEAR_MATERIALS`), which matches the
user preference against linear materials. Its current precondition is a motion
profile row with `depth_output`, which all 100 programs have.

**Prior art inside the project.** The converted (linear-material) route already
implements exactly this gain: at `at == p->texture[bump?3:2]` it emits
`mul r0.xyz, r0, c213.z` (`gain(combined,false,0,2)`), fed by
`LinearMaterialConfig::lightmap_emissive_gain` /
`X3M_LIGHTMAP_EMISSIVE_GAIN` / `--lightmap-emissive-gain`. That option is gated
behind `--linear-materials` and therefore unavailable in the shipped native
path; the work is to host the same one-instruction gain in the original-program
route.

## 6. Open

- Texel content of the bound light maps is unverified (§4); a user bracket of a
  native-path gain, or an archive DDS decode, would settle whether windows are
  authored there.
- The 32x32 single-level identities (1706, 1853, 3700 in run 39) are assumed to
  be the black placeholder by size and by the archive check recorded in the
  emitter plan; their file names were not resolved here.
- Admission policy for a native gain is undecided: every opaque hull draw of the
  100 programs would carry it, so a gain also brightens any non-window light-map
  content (panel stripes, decals) on ships and props alike.
- No GPU, live or native-Windows evidence exists for the proposed transform; the
  eight non-light-map originals (glass, asteroid) would need a separate story if
  a uniform option is wanted.
