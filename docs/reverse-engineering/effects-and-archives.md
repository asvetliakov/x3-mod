# Effects, materials and resource archives

## Observed catalogue structure

The analyzed installation has 13 numbered CAT/DAT pairs in the game root and four in `addon/`. The directory bytes decode as `decoded[i] = encoded[i] XOR ((0xdb + i) & 0xff)`. A first-line DAT name is followed by `virtual/path byte_count` entries. Accumulating entry lengths gives offsets into the same-stem DAT file. All 17 pairs have exactly matching accumulated and physical DAT sizes.

The header name is **not reliable**: root `05.cat` says `04.dat`, while root `13.cat`, addon `03.cat` and addon `04.cat` say `foo.dat`. The analysis tool pairs each CAT with the DAT of the same filesystem stem and verifies the total length.

| Catalogue | Entries | Shader entries |
|---|---:|---:|
| Root `01.cat` | 7,015 | 2,304 |
| `addon/01.cat` | 1,858 | 1,176 |
| Other numbered catalogues | 2,835 | 0 |

The two shader catalogues each have directories `1_1`, `1_4`, `2_0`, `2_a`, `2_b`, `3_0`. Root `01.cat` has 384 entries per directory; addon `01.cat` has 196. Names suggest profile variants, but directory labels alone do not prove every contained shader uses that model. In fact, lower shader models appear within the `3_0` effect directory.

Observed effect names include `standard_lighting`, race-specific materials, `asteroid`, `engine`, `glass`, `gui2d`, `bloom`, `particles`, `nebula`, `nebulafog`, `planet_haze`, `planet_v`, `stardust` and `z_only`, with optional `2s`, hue/light toggles and other variants. These are strong candidates for pass classification. **Unknown:** the exact installed runtime override precedence and which variants are selected for current settings. Do not assume a filename uniquely identifies a live shader.

## Compiled effect inspection

Selected addon `shader/3_0/*.fb` DAT slices decode with bytewise XOR `0x33`; the resulting first four bytes are `01 09 ff fe`. They contain effect parameter names and embedded D3D shader token streams. Inspection was in memory; assets were not extracted into the repository.

Useful selected offsets in `addon/01.dat`:

| Virtual effect path | DAT offset (decimal) | Bytes |
|---|---:|---:|
| `shader/3_0/bloom.fb` | 278757732 | 51720 |
| `shader/3_0/gui2d.fb` | 279011868 | 7916 |
| `shader/3_0/particles.fb` | 282500672 | 8756 |
| `shader/3_0/standard_lighting.fb` | 282646984 | 55872 |
| `shader/3_0/z_only.fb` | 285148308 | 7132 |

The bloom effect contains eight SM3 pixel token streams and eight SM3 vertex token streams, including repeated streams across techniques. `standard_lighting.fb` contains at least three SM3 pixel streams. The selected gui2d, particles and z_only effects instead contain SM1.1 streams; their enclosing `3_0` directory does not indicate every shader token version.

## Material/camera/light parameter clues

The executable's render-string region exposes these names, many also present in effect data:

| Group | Observed names | Research implication |
|---|---|---|
| Camera/transforms | `g_mView`, `g_mProj`, `g_mWorld`, `g_mWorldIT`, `g_mViewInverse`, `g_mWorldViewProjection`, `g_mViewProjection` | Named D3DX effects may provide a path to camera recovery and projection jitter |
| Directional lighting | `LightDir_Dir0/1`, `LightDir_Color0/1`, `g_LightAmbientIntensity` | Recover directions, spaces, color encoding and update frequency |
| Point lighting | `g_nNumLightPoint`, `g_LightPoint` | Recover struct layout, cap, source-space and light selection |
| Materials | `g_MatDiffuseStrength`, `g_MatSpecularStrength`, `g_MatSpecularPower`, `g_MatReflectionStrength`, `g_MatEmissiveColor` | Starting semantics for replacement lighting, not proof of PBR or HDR |
| Texture inputs | `t_DiffuseTexture`, `t_BumpTexture`, `t_SpecularTexture`, `t_LightMapTexture`, `t_CubeMapTexture`, `t_OcclusionTexture` | Preserve original asset bindings when replacing shaders |
| Glow/composition | `g_EnableGlow`, `t_SceneMap`, `t_GlowMap1`, `t_GlowMap2`, `ViewPortSize`, `RenderColorTarget0` | Candidate original compositor and scene/glow boundary |
| Depth/particles | `g_ZEnable`, `g_ZWriteEnable`, `z_only`, `Z_Only_Alpha`, `Z_Only_Fast`, `INSTANCE_BULLETS` | Candidate depth and instancing paths, requiring draw verification |
| Fog | `g_EnableFog`, `g_FogClip` | Existing fog controls to recover before volumetric replacement |

The selected bloom effect additionally contains `g_BlurWidth`, `g_Sigma`, `g_HighlightThreshold`, `SceneIntensity`, `LightMapGlowIntensity` and `g_BlurWeightModifier`. **Inference:** the old compositor has configurable filtering, highlight selection and scene/glow contribution. This does not establish render-target precision or whether unclipped HDR values survive.

`standard_lighting.fb` adds `g_MatReflectionBlur`, `g_MatColor` and light-related names. Recover actual constant types/register bindings from live shader CTAB or D3DX effect metadata before assigning numeric meanings.

## What this does not prove

No scene draw capture has yet established HDR precision, UI pass order, motion vectors, depth readability, fog density semantics, a shadow-map pipeline, or a clustered light list. A `z_only` effect is not proof of shadow mapping. A cubemap helper import is not proof of SSR. Shader names support targeted investigation; they do not establish feature completion.
