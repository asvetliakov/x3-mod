# Non-effect (classic) body materials: record, draw path and a merged replacement

Read-only study, 2026-09-29; sections 6 to 9 were added the same day with the baker change that
atlases these materials, and correct the bump placeholder, the colour matrix and the NULL diffuse.
It covers the Mayhem 3 bodies the LOD overlay baker refuses as
`non_effect_material` ([lod-overlay.md](../verification/lod-overlay.md); the refusals are raised in
`tools/analysis/lod_atlas.py` `atlas_tiles` / `animated_record`). The EXE is the installed
`X3AP.exe` (SHA-256 `fdbf3418…34f8ab`, preferred VAs, base `0x00400000`). Method: a fresh Ghidra
12.1.3 headless import in the session scratchpad; decompilation of `0x004c0150`, `0x004b9010`,
`0x004b8f70`, `0x004b9060`, `0x004bb0f0`, `0x004bae10`, `0x004b9ed0` and `0x004ba500`; capstone
listings of `0x004c0150..0x004c40fb`, `0x00481f60..0x004820c0` and `0x00484690..0x004846c4`; and a
token disassembly of `shader/3_0/standard_lighting.fb` (`addon/01.cat`). Raw decompiler output and
shader text stay in the scratchpad, untracked. No Wine, no game launch; the game tree was only read.
Scripts and compact outputs are in `verification/results/lod-mayhem-refusals/`.

Marks: [m] measured from files or bytes, [s] read from code, [i] inferred.

**Result.**

1. **What they are.** Every refused material is a **classic** record: `MAT6` whose flags lack
   `0x02000000` (all 162 drawn by the content bodies), plus one `MAT5` in the helper
   `khaak_cluster_models/00940`. The fields are the texture name, 12 colour words, shininess and
   strength, a texture value and five (map, value) pairs, with no effect or parameters [m] (§1).
2. **How the engine draws them.** Nothing is converted at load; the record keeps no effect object
   (`+0x5c = 0`, `0x00481f74..0x00481ff4`) [s]. At draw time `0x004c0150` takes a separate
   branch. That branch selects the built-in effect **`standard_lighting`**: the same
   `shader\<profile>\standard_lighting.fb` that effect materials naming `standard_lighting.fx`
   use. Its technique is **`BUMPMAP_LOW`** when a bump, cube or light map is set, otherwise
   **`DEFAULT`**. It records its constants into a D3DX parameter block, once per subset, from the
   record words (§3) [s].
3. **Can an effect material reproduce it?** Yes, as an effect material on `standard_lighting.fx`
   with the constants of §3 written out explicitly. The one gap is the technique: the effect path
   never selects `BUMPMAP_LOW`. On the `2_0`..`3_0` profiles a `BUMPMAP_LOW` record without a bump map
   reads the `NONE_NORMAL` placeholder, not the flat `NONE_NORMAL_LOW` one, and is lit with a normal
   about 49° off the vertex normal wherever its vertices have tangents (§6, corrected 2026-09-29).
   `DEFAULT` reproduces it only in a model without the tangent declaration; in a model with it the
   replacement draws `BUMPMAP` over a bump atlas that holds that normal. Every one of the 76 content
   bodies with opaque textured classic materials has exactly **one** constant set among them [m], so
   no constant needs averaging (§4). The baker builds this since 2026-09-29 (§9).
4. **Helpers and content, by rule** (§5): **82** helper bodies and **85** content bodies [m]. Of the
   content bodies, 76 need a classic → `standard_lighting` class (all `addon/12.cat`: 53
   `ships/stellaris`, 19 `stations/stellaris`, 4 Xenon). Three use an untextured opaque classic
   material next to effect materials. Six carry only an additive dock marker (`155.jpg`) next to
   effect materials.

## 1. The records (question 2)

The binary layout is in [body-format-bob1.md](body-format-bob1.md) §2 and the text form in
[body-text-loader.md](body-text-loader.md) §8. This section covers only what the draw path reads
(§3).

| record field | offset | read by `0x004c0150` for | notes |
|---|---|---|---|
| texture | `+0x02` | diffuse id `[esp+0x2c]` (`0x004c04d2`) | `+0x2e` replaces it when both `+0x2e` and `+0x32` are set and arg 4 has bit 2 (`0x004c04b4..0x004c04d0`) |
| ambient rgb | `+0x04/+0x06/+0x08` | — | not read |
| diffuse rgb | `+0x0a/+0x0c/+0x0e` | emissive colour, only with self-illumination | — |
| specular rgb | `+0x10/+0x12/+0x14` | specular power, only when shininess is 0 | — |
| word | `+0x16` | — | 0 or `0xffff` in the census |
| transparency | `+0x18` | — | not read; transparency comes only from the flags |
| self-illumination | `+0x1a` | emissive / diffuse strength | — |
| extra map 0 (name, value) | `+0x1c/+0x1e` | **specular** map `[esp+0x48]` (`0x004c0581`) | semantic 3 |
| extra map 1 | `+0x20/+0x22` | — | not read |
| shininess, strength | `+0x24`, `+0x26` | specular power, specular strength | — |
| flags | `+0x28` | blend, cull, filtering, and the instance index | see "Flags" below |
| texture value | `+0x2c` | diffuse strength | — |
| map 0 (name, value) | `+0x2e/+0x30` | **cube** map `[esp+0x54]`, only when map 1 is empty; value → reflection | semantic 5 |
| map 1 | `+0x32/+0x34` | **bump** map `[esp+0x30]` | semantic 2 |
| map 2 | `+0x36/+0x38` | **light** map `[esp+0x60]` (`0x004c057d`) | semantic 4 |

The semantic numbers come from `0x004ba500`: 1 diffuse, 2 bump, 3 specular, 4 light map, 5 cube map,
6 detail, 7 occlusion. The effect path stores its parameters in the same stack slots, and the
texture binding below serves both paths.

**Flags.** At load the file flags are **overwritten with the `Materials` row flags** whenever the
texture id is a row (`0 ≤ id < rows`: binary `0x0048206e..0x00482093`, text
`0x0048469b..0x004846be`) [s]. A `MAT5` flag word is OR-ed in afterwards
([body-format-bob1.md](body-format-bob1.md) §2). The winning table is
`addon/07.cat:addon/types/Materials.pck` with 1,238 rows. Row 0 has flags `0`. Row 155 has
`MPF_DESTINATIONBLEND | MPF_BESTQUALITY = 0x8002` [m]. The flag names come from table
`0x0054dbe0` [m]: `ALPHATEST 1`, `DESTINATIONBLEND 2`, `ALPHABLEND 4`, `2SIDED 0x10`,
`MULTIPLY2X 0x40`, `MULTIPLY 0x80`, `NOFILTERING 0x100`, `SRCCOLOR 0x400`. So a classic material
named `155.jpg` (or a `MAT5` with texture id 155) draws **additively** whatever its file flags
say.

**Census.** `nonfx_materials.py` covers the 167 ship/station rows of the bake record, reading
record 0 (the batch's source record) and only visible parts. The table lists the distinct record
shapes in the 85 content bodies of §5 [m]. Every file flag word is `0x0`. `map_values` is
`[100,100,100]` for the Stellaris shapes and `[100,30,100]` for the Xenon and salvage shapes.

| materials / bodies | source | texture | amb / dif / spec rgb | self-illum | shin / strength / texval | maps set | draw (§3) |
|---|---|---|---|---|---|---|---|
| 73 / 67 | Stellaris `addon/12` | named `.dds` | 51 / 255 / 0 | 0 | 30 / 100 / 100 | — | opaque, `DEFAULT` |
| 48 / 48 | Stellaris `addon/12` | named `.dds` | 51 / 255 / 0 | 0 | 30 / 100 / 100 | map 2 (`*_light.dds`) | opaque, `BUMPMAP_LOW` |
| 21 / 3 | Xenon `addon/12` | named | 149 / 149 / 229 | 0 | 10 / 60 / 100 | map 2 | opaque, `BUMPMAP_LOW` |
| 5 / 1 | Xenon `xenon_m7_h/hull` | named | 149 / 149 / 229 | 0 | 10 / 120 / 100 | map 2 | opaque, `BUMPMAP_LOW` |
| 4 / 4 | Stellaris `starbase/*_details` | none | 51 / **0,0,255** / 0 | 0 | 30 / 100 / 100 | — | opaque, `DEFAULT`, `NONE_GRAY` diffuse |
| 2 / 2 | Stellaris `tm_machineSalvage/hull*` | named | 149 / 149 / 229 | 0 | 10 / 0 / 100 | map 1 (`DISABLE…_bump`), map 2, extra 0 (`*_spec`) | opaque, `BUMPMAP_LOW` |
| 4 + 2 / 6 | dock bodies `02.cat`, `addon/01` | `155.jpg` → row 155 | 0 / 255 / 255 | 100 | 10 / 0 / 100 | — | **additive**, emissive 1.0 |
| 1 / 1 | `dock9portsdummy` | none | 0 / 255 / 255 | 100 | 10 / 0 / 100 | — | opaque, emissive 1.0, `NONE_GRAY` |
| 1 + 1 / 2 | `XTC_paranid_drone`, `XTC_terran_tm` | none | 126 or 150 grey / 229 | 0 | 10 / 0 / 100 | — | opaque, `NONE_GRAY`, no specular |

Texture resolution (`nonfx_textures.py`, rule of [texture-lookup.md](texture-lookup.md); every
classic record of the 167 bodies, not only those record 0 draws) [m]. In `addon/12`, 162 diffuse
names load a file (128 from `addon/12`, 34 from `01.cat`). One falls back to
the `NONE_BLACK` placeholder. Of the 88 light maps, 85 load and 3 fall back to `NONE_BLACK`
(misspelt `…_ligh`/`…_ight` names and `NONE_BLACK.dds`). The two `DISABLE…_bump` names fall back to
the bump placeholder, and both `*_spec` names load. The 10 dock-marker `155.jpg` names load
`tex/true/155.jpg` from `01.cat`.

## 2. Load: no conversion

`0x00481aa0`, `MAT6`: the flags are read at `0x00481f6f`. Only `flags & 0x02000000` reaches the
effect reader `0x00470490`, which stores the effect object at `+0x5c`. Otherwise `+0x5c` is freed
and zeroed (`0x00481fde..0x00481ff4`), and the texture name goes through `0x004f4cb0` with the
row-flag overwrite above. Neither the text loader nor the binary parser builds an effect object for
a classic record [s]. The `MAT5`/`MAT6` form sets model `+0x50 |= 0x10`, so
`0x004c0150` takes the per-body branch (`[esp+0x38] ≠ 0`); `MAT3` and material-less bodies take
the global `Materials` row branch (`0x004c0592..0x004c061c`), not examined here.

## 3. Draw path in `0x004c0150` (question 3)

| step | address | what happens |
|---|---|---|
| branch | `0x004c049f`, `0x004c0624..0x004c0636` | `flags & 0x02000000` → effect path; otherwise the classic path from `0x004c0a79` |
| texture ids | `0x004c04b4..0x004c058d` | the slots of §1 |
| flags | `0x004c0a7f` (record `+0x28`) or `0x004c0a95` (row `+0x10`) | kept at `[esp+0x70]` / `[esp+0x24]` |
| effect | `0x004c0b06..0x004c0b4b` | `"standard_lighting"`; **`"effects"`** when node `+0x1c == *(*0x00608518+0x64)` (the cockpit view, [frame-loop-phases.md](frame-loop-phases.md)). The instance index is `(flags >> 4) & 1` (two-sided) into the container of `0x004bb0f0`; `0x004bae10` loads both instances from the same `shader\<profile>\<name>` with no defines, so the index only separates the D3DX instances |
| z-only | `0x004c0aa2..0x004c0aeb` | node `+0x130 & 0x20000`: `z_only`, `Z_Only_Alpha` when `flags & 1`, else `Z_Only_Fast` |
| technique | `0x004c0b51..0x004c0b7d` | `BUMPMAP_LOW` when bump, cube or light id ≠ 0 and node `+0x130 & 0x100000` is clear, else `DEFAULT`. Compare the effect path (`0x004c098f..0x004c09ca`): `BUMPMAP` only with a bump id, else `DEFAULT` |
| parameter block | `BeginParameterBlock` `0x004c0df9`, `EndParameterBlock` `0x004c19cc` → subset `+0x2c`; applied per draw at `0x004c1edd` / `0x004c1ef4` | recorded **once per subset** |
| light points | `0x004c137d`, then per draw `0x004c2a1c` region | the block records `g_nNumLightPoint = 0`; the per-draw `SetInt` of the uploaded point-light count follows the block (order [i]) |

**Constants recorded by the classic block** (`0x004c1399..0x004c1970`; float constants read
from the image [m]):

| parameter | value | address |
|---|---|---|
| `g_MatEmissiveColor` (float4) | self-illum `s ≠ 0`: `(dif.r, dif.g, dif.b) × s / 25500` (`0x0056573c` = 1/25500), `w = 1`; else `(0,0,0)` | `0x004c13a6..0x004c13fd`, `0x004c143d..0x004c1454` |
| `g_MatDiffuseStrength` | `s ≠ 0`: **0**; else texture value `+0x2c × 0.01` | `0x004c1407`, `0x004c1426` |
| `g_MatSpecularStrength` | strength `+0x26 × 0.01` | `0x004c1469` |
| `g_MatSpecularPower` | shininess `+0x24 + 1`; shininess 0: `(spec.r+g+b) / 768 × 100 + 1` | `0x004c1482..0x004c14d6` |
| `g_MatReflectionStrength` | cube map set: map-0 value `+0x30 × 0.04`; else 0 | `0x004c14de..0x004c150a` |
| `g_Wrap` | 3 when `*(*arg1)+0x60 & 2`, else 0 | `0x004c170b..0x004c1727` |
| `g_CullMode` | `flags & 0x10` → 1 (none), else 3 (CCW) | `0x004c172c..0x004c1744` |
| `g_BlendOp`, `g_ZEnable`, `g_AlphaValue` | 1 (add), 1, 1.0 | `0x004c174c..0x004c1771` |
| blend state | `flags & 0x4c7 == 0`: `g_ZWriteEnable 1`, `g_AlphaBlendEnable 0`. Otherwise z-write 0, blend 1, and by precedence `0x80` DESTCOLOR/ZERO, `0x40` DESTCOLOR/SRCCOLOR, `0x400` SRCCOLOR/INVSRCCOLOR, `0x04` (or `0x01|0x02`) SRCALPHA/INVSRCALPHA, `0x02` ONE/ONE. Then `flags & 1` without `2`: z-write 1, ONE/ZERO, `g_ALPHATESTENABLE 1`, blend 0 | `0x004c1779..0x004c1908` |
| filtering | `flags & 0x100`: `t_MinFilterTypeDiffuse 1`, `t_FilterTypeDiffuse 1` (point); else anisotropic (3 plus the device maximum) or linear (2) | `0x004c190b..0x004c1972` |

`g_Brightness`, `g_Contrast`, `g_Saturation` and `g_Hue` are not recorded for a classic record:
the gate at `0x004c197a` needs a `colormatrix` parameter (semantic `0x20`) [s].

**Textures per draw** (`0x004c3218..0x004c3b2b`; used by both paths): each slot binds the id's
texture. When the id's table entry holds no texture (id 0 included), the slot's fallback global is
bound instead through `0x004b9ed0(handle, -1)` with `EDI` = the fallback [s]. The placeholder
contents are read from `01.cat` [m].

| handle | id | fallback |
|---|---|---|
| `t_DiffuseTexture` (`+0x68`) | diffuse | `0x00606f70` `NONE_GRAY` (DXT1 `0x8410`, about 0.52 grey, opaque) |
| `t_BumpTexture` (`+0x6c`) | bump; the fallback is always used when node `+0x130 & 0x100000` | `0x00606f64` `NONE_NORMAL_LOW` (A8R8G8B8 `ff8080ff`, flat); `0x00606f60` `NONE_NORMAL` (DXT5, a/g about 0.5) when the device byte `+0xa0 ≠ 0` |
| `t_SpecularTexture` (`+0x70`) | specular | `0x00606f58` `NONE_WHITE` |
| `t_OcclusionTexture` (`+0x74`) | occlusion (0 for classic) | `0x00606f74`; only when node `+0x14c == 0` ([texture-lookup.md](texture-lookup.md) §12) |
| `t_LightMapTexture` (`+0x78`) | light | `0x00606f5c` `NONE_BLACK` (DXT5 black, alpha 0) |
| `t_CubeMapTexture` (`+0x7c`) | cube; bound only when the device byte `+0x95 == 1`, with the `effects\others\envmap_test.dds` substitution | `0x00606f68` |

**What the programs compute** (`standard_lighting_programs.py`; `addon/01.cat:shader/3_0/
standard_lighting.fb`, SHA-256 `bd1cec1d…`; the program identities are [m], the formula is read
from the token disassembly [s]):

| technique | VS | PS |
|---|---|---|
| `DEFAULT` | `494fe349b8bc12ec` | `7c83ed50c9894e44` |
| `BUMPMAP` | `4944d81dfe531b37` | `0c1f3f0f440e4a0c` |
| `BUMPMAP_LOW` | `4944d81dfe531b37` | `99153c144030c396` |

```
N    = DEFAULT: normalize(world normal); BUMP*: normalize(b.x·v5 + b.y·v4 + b.z·v3),
       b = 2·bump.rgb − 1 (BUMPMAP_LOW) or (2·bump.ag − 1, sqrt(1 − x² − y²)) (BUMPMAP)
d_i  = sat(N·L_i)                              i = 0, 1: LightDir_Dir/Color0, 1
s_i  = pow(sat(reflect(−L_i, N)·V), g_MatSpecularPower) · sat(3·d_i)
C    = Σ d_i·Color_i · g_MatDiffuseStrength + Σ s_i·Color_i · spec.r · g_MatSpecularStrength + sat(v0.rgb)
v0   = VS: Σ point lights (g_nNumLightPoint loop) + g_MatEmissiveColor;  v0.a = g_AlphaValue · fog
D    = g_MatColor(c0..c2) · (diffuse.rgb, 1)   -- c0..c2 are not in the PS constant table: set by the effect
rgb  = C · D + cube(R) · spec.r · D · g_MatReflectionStrength + light.rgb
a    = lerp(diffuse.a, light.a, g_EnableGlow) · v0.a
```

The three pixel programs differ only in N and in the reflection vector (`DEFAULT` takes it from the
vertex shader, `BUMP*` per pixel). None of them reads `g_LightAmbientIntensity`; the only ambient-like
term is `v0` (point lights + emissive). For a classic record this means [s]:

- **Lighting**: two directional lights × `texture value/100`; point lights and emissive are added
  unscaled (saturated) and multiplied with the diffuse texture. The ambient colour is not used.
- **Diffuse colour** is not used, except as the emissive colour when self-illumination is set. An
  untextured material draws the `NONE_GRAY` placeholder (0.52 grey), not its colour. The blue
  `citadel_details` material is grey in game [i].
- **Specular**: without a specular map the `NONE_WHITE` placeholder makes the full
  `strength/100 · pow(…, shininess+1)` highlight visible. For the Stellaris hulls that is strength
  1.0, power 31. The specular colour is not used.
- **Emissive**: self-illumination `s` gives `E = dif · s/100 / 255` added in `v0` and switches the
  directional diffuse off (`g_MatDiffuseStrength = 0`). The light map adds its colour unlit.
- **Transparency**: from the flags only (§1 Flags, blend table). The transparency word `+0x18`
  is not read, and output alpha is `diffuse.a` (or the light map's alpha with glow) × `g_AlphaValue`
  (1.0) × fog.

## 4. What a merged replacement must contain (question 4)

The technique row of the table below and the first bullet of the list after it are superseded by
§6 and §9: in a model with the tangent declaration the replacement declares `t_BumpTexture` and
draws `BUMPMAP`.

The effect path uses the same effect instance for a one-sided material (`0x004c0970`: `EAX = 0`)
and records only the material's own parameter list (`0x004c0ec0..0x004c134f`) plus the fixed ones.
A parameter the list omits keeps the value the shared instance last held (D3DX parameter-block
semantics [i]). So the replacement must list everything the classic block records:

| parameter (type) | value |
|---|---|
| material | `MAT6`, flags `0x02000000`, effect `standard_lighting.fx` (`0x004c0880..0x004c08c0` maps `standard_lighting`/`max_standard_lighting` to the built-in name) |
| `t_DiffuseTexture` (STRING) | diffuse atlas; tiles of untextured materials are `NONE_GRAY` texels (not black) |
| `t_LightMapTexture` (STRING) | light atlas; tiles of materials without a light map are black with alpha 0 (`NONE_BLACK`) |
| `t_SpecularTexture` (STRING) | `NULL` (→ `NONE_WHITE`) when no merged material has a specular map; otherwise a specular atlas with white tiles for the others (2 salvage materials only) |
| no `t_BumpTexture`, no `t_CubeMapTexture` | → technique `DEFAULT` (`0x004c09ac..0x004c09ca`), reflection 0 |
| `g_MatDiffuseStrength`, `g_MatSpecularStrength`, `g_MatSpecularPower` (FLOAT) | §3 values; one set per body in all 76 bodies [m] (Stellaris 1.0 / 1.0 / 31, Xenon 1.0 / 0.6 or 1.2 / 11) |
| `g_MatEmissiveColor` (FLOAT4) | `(0,0,0,1)` for all 76 (self-illumination 0) |
| `g_MatReflectionStrength` (FLOAT) | 0 |
| `g_AlphaValue` (FLOAT) 1.0; `g_AlphaBlendEnable` 0, `g_ALPHATESTENABLE` 0, `g_ZWriteEnable` 1, `g_ZEnable` 1, `g_BlendOp` 1, `g_CullMode` 3 | opaque, one-sided (no census material is two-sided) |

`standard_lighting.fb` declares every one of these parameters [m]. A shipped `standard_lighting.fx`
effect material (1,518 shipped ones in 530 bodies [m], §8) can serve as the template for names and types, with
the values replaced.

**Can the material the baker already emits be reused?** Not the copied dominant of another effect.
50 of the 76 bodies also draw `argon.fx` materials, whose shader is different
([remaining-hull-materials.md](remaining-hull-materials.md)); the other 26 draw only classic
materials [m]. The classic groups need their own `standard_lighting.fx` class. The baker's
mixed-effect scheme (one material per effect over one shared atlas set) fits that: the
`standard_lighting.fx` material is synthesised from §3, not copied.

**Not reproducible, or only approximately:**

- **Technique.** A classic material with a light map draws `BUMPMAP_LOW` (48 + 21 + 5 + 2
  materials). The device byte `+0xa0` is traced (§6): it is 1 on every profile from `2_0` up, so
  `NONE_NORMAL` is bound, and `BUMPMAP_LOW`'s RGB decode of its texel (132, 130, 132) gives the
  tangent-space normal (0.658, 0.366, 0.658), 48.8° off the vertex normal [m]. With zero tangents
  (no tangent declaration, text bodies, parts without `0x30000000`) it collapses back to the vertex
  normal. The earlier figures here (0.31, 0.15, 0.94; 20°) were wrong.
- **Merged differing constants.** Specular power differs only across bodies, and self-illumination
  and diffuse strength never differ within one body. If a later mod mixes them within a body,
  `g_MatSpecularPower` cannot be baked into a texture. Specular strength can be baked into the
  specular atlas's red channel (relative to the largest). A self-illuminated tile can be baked as
  black diffuse plus `E × diffuse` in the light-map atlas, which loses the saturation with point
  lights [i].
- **Blended classic materials** (the `155.jpg` markers, additive through the row flags) cannot join
  an opaque atlas. They stay separate groups, like alpha materials.
- **`g_MatColor`** is not inherited (corrected 2026-09-29, §8): every parameter block, classic or
  effect, starts by recording `g_Brightness 1`, `g_Contrast 1`, `g_Saturation 1`, `g_Hue 0`
  (`0x004c0e2c..0x004c0e6b`), and the EXE never names `g_MatColor`, so the effect derives it from
  those four. A replacement that declares none of them gets the same values as the classic block [s].
- The cockpit-view `"effects"` substitution has no effect-path twin. It is not reachable for world
  LODs [i].

## 5. Helper and content bodies (question 5)

The rule is applied to record 0's visible groups (`nonfx_materials.py`, [m]). A body is a
**helper** when it draws no effect material and no opaque textured classic material. Everything it
draws is then an untextured grey box, a black box (diffuse and specular strength 0) or an additive
marker. Otherwise it is **content**.

| class | count | bodies |
|---|---|---|
| helper | **82** | 68 `ships/x3ap/props/col/{esexported,reference-boxes}/COL_*` (12-face untextured boxes), 8 `ships/props` (`cameradummy`, `extern_*_dock`, `tstp_*dummy`), 2 `x3ap/props` turret sockets, 3 `stations/docks` dummies, `khaak_cluster_models/00940` (`MAT5`, texture 155, additive) |
| content, opaque textured classic | **76** | `addon/12.cat`: 53 `ships/stellaris`, 19 `stations/stellaris`, 4 `ships/xenon` (`xenon_m7_h/*`, `xenon_ts_f`) |
| content, opaque untextured classic among effect materials | 3 | `XTC_paranid_drone`, `XTC_terran_tm`, `dock9portsdummy` |
| content, only blended classic among effect materials | 6 | `dock5portsdummy`, `dock5ports_arm_dummy`, `dockCarrier*_scene_dummy` ×2, `M6dockCarrier*_scene_dummy` ×2 |

By source catalogue, the helpers are 70 `addon/01`, 9 `addon/06` and 3 `02.cat`; the content
bodies are 76 `addon/12`, 3 `addon/01` and 6 `02.cat` [m].

## 6. Bump placeholder and tangents (2026-09-29)

Capstone listings of `0x004d9b40..0x004d9fe0`, `0x004da6e0..0x004da7a0`, `0x004c3200..0x004c3420`,
`0x00481780..0x00481aa0` and `0x00482096..0x004823b0`; token disassembly of the three
`standard_lighting` programs (`standard_lighting_programs.py --disassemble`, local); placeholder
texels decoded from `01.cat`.

| fact | address | mark |
|---|---|---|
| The placeholder globals: `0x00606f58` `NONE_WHITE`, `0x00606f5c` `NONE_BLACK`, `0x00606f60` `NONE_NORMAL`, `0x00606f64` `NONE_NORMAL_LOW`, `0x00606f74` `NONE_OCCL_DECAL`, `0x00606f70` `NONE_GRAY`, `0x00606f68` `NONE_ENVI`, `0x00606f6c` `ENVI` | `0x004da6ed..0x004da794` (each result is stored after the next call's arguments are pushed) | [s] |
| Device byte `+0xa0`: 0 for the `1_1`..`1_4` profiles, **1** for `2_0`, `2_b`, `2_a` and `3_0` | `0x004d9d1e`, `0x004d9f8e` (0); `0x004d9dae`, reached from `2.0`, `2.b` `0x004d9e23`, `2.a` `0x004d9e81`, `3.0` `0x004d9ee2` (1) | [s] |
| Bump binding with byte 1: fallback `NONE_NORMAL`, bound when the bump id's entry holds no texture (id 0 included) | `0x004c3339..0x004c3405` | [s] |
| The loader's placeholder for a bump name that loads no file follows the same byte | `0x004f378b` | [s] |
| `NONE_NORMAL` is DXT5, every texel (132, 130, 132, 128); `NONE_NORMAL_LOW` is A8R8G8B8 (128, 128, 255, 255); `NONE_GRAY` is DXT1 (132, 130, 132, 255) | `dds/*.pck` of `01.cat` | [m] |
| `BUMPMAP_LOW` pixel shader: `b = 2·bump.rgb − 1`, `N = normalize(b.x·v5 + b.y·v4 + b.z·v3)`. `BUMPMAP`: `x = 2·bump.a − 1`, `y = 2·bump.g − 1`, `z = sqrt(1 − x² − y²)`, the same sum. `DEFAULT`: `N = normalize(v3)`. Apart from N the three programs compute the same colour; they differ in the cube-map vector only (`DEFAULT` takes it from the vertex shader), which the reflection strength 0 removes | programs `99153c14…`, `0c1f3f0f…`, `7c83ed50…` | [s] |
| Vertex shader of `BUMPMAP` / `BUMPMAP_LOW` (one program): `v3` = normal, `v4` = `TANGENT0`, `v5` = `BINORMAL0`, each transformed by `g_mWorldIT` with `dp4` and `w = 1` | program `4944d81d…` | [s] |
| Model flag `+0x50 & 4` (tangent vertex declaration, [body-text-loader.md](body-text-loader.md) §6) is set by an effect record whose `t_BumpTexture` has a texture id other than 0 (record flag `0x2000`), or by a classic record with a bump id (`+0x32`) | `0x00481a10..0x00481a33`, `0x00482003..0x00482010`; `0x00482349..0x0048234f` | [s] |

So on the profiles the game runs today a classic record that draws `BUMPMAP_LOW` without a bump map
of its own (a light map is enough to select the technique) is lit with
`N = normalize(0.035·B + 0.020·T + 0.035·N_v)`: the tangent-space direction (0.658, 0.366, 0.658),
**48.8°** off the vertex normal [m, from the texel]. Where the vertex has no tangents the sum
collapses to the vertex normal. A vertex has tangents only in a model with the tangent declaration
and in a group with tangent records of a part with `0x30000000`.

`nonfx_bake_tangent_census.py` over record 0 of the 167 bodies (visible parts, reproducible classic
records) [m]:

| technique | tangent declaration | tangent records | faces | bodies |
|---|---|---|---|---|
| `BUMPMAP_LOW` | yes | yes | 5,513 | **49** (tilted in the original) |
| `BUMPMAP_LOW` | no | yes | 1,272 | 1 |
| `BUMPMAP_LOW` | no | no | 50,130 | 4 (text bodies) |
| `DEFAULT` | yes | yes | 25,977 | 54 |
| `DEFAULT` | yes | no | 54 | 1 |
| `DEFAULT` | no | yes or no | 5,879 | 92 |

In the 49 bodies the declaration comes from an effect material with a bump map (47) or a classic
record with a bump name (2, `tm_machineSalvage`). Two readings are assumed, not read: a vertex
shader input missing from the declaration reads zero, and the fourth column of `g_mWorldIT` is zero
(see Unknown). In a model with the declaration neither matters: original and replacement read the
same tangent inputs with the same weights. In the `BUMPMAP_LOW` / no declaration rows (5 bodies:
`tp_machineShuttle/hullB` and the Xenon text bodies `xenon_m7_h/hull`, `hull_no_orb`, `RedOrb`,
`xenon_ts_f/hull`) the original's normal depends on both, so the baker does not merge those records
(§9).

## 7. NULL diffuse on the effect path (2026-09-29)

| step | address | mark |
|---|---|---|
| The material array is zeroed when allocated (`memset` call); an effect record gets its effect object at `+0x5c` and its flags at `+0x28`; `0x00481780` then writes `+0x3c..+0x58` (texture animation) and flag bits | `0x00481f02`, `0x00481f74..0x00481fc0`, `0x00481780..0x00481a92` | [s] |
| Searched for a write to the id words `+0x02`, `+0x1c`, `+0x2e`, `+0x32`, `+0x36`: every instruction of `0x00481780..0x00481a92` and of the loader's effect branch `0x00481f50..0x00482019` with a written memory operand at one of those displacements off any register: **0 and 0 matches**; the same search over the classic branch `0x00482019..0x004823a7` finds its 10 writes. Not searched: the effect reader `0x00470490` (its arguments are the new effect object and the stream, not the record) and callees | `nonfx_bake_listing.py evidence` | [m] |
| The draw reads the diffuse id from `+0x02` for every per-body record, before the path branch | `0x004c04b4..0x004c04d8` | [s] |
| The effect path's texture parameters overwrite a slot only when the parameter's id is not 0 | `0x004c06a6..0x004c06ab` (diffuse), the same test per semantic 2..7 | [s] |
| A NULL name is id 0 | [texture-lookup.md](texture-lookup.md) §2 | [s] |
| Diffuse binding: fallback `0x00606f70` = `NONE_GRAY`, bound when the id's entry holds no texture | `0x004c321d..0x004c32e1` | [s] |

An effect material with a NULL `t_DiffuseTexture` therefore draws the `NONE_GRAY` texel, like a
classic record without a texture, not black. The baker's NULL diffuse tile was black (assumed by
size, 2026-09-24); it is `NONE_GRAY` since 2026-09-29.

Scope, `nonfx_bake_null_diffuse_scan.py` over the 1,079 bodies the installed record bakes: **23**
draw a non-alpha effect material with a NULL diffuse in their source record [m]: `ships/pirate/pirate_m6_osprey/hull`, `ships/split/split_TL`, `ships/split/split_tl_elephant/hull`, `ships/teladi/teladi_M6`, `ships/teladi/teladi_m6_osprey/hull`, `ships/teladi/teladi_TP_L`, `ships/teladi/teladi_TP_S`, `ships/teladi/teladi_tp_toucan/hull`, `ships/teladi/teladi_tp_toucan_hauler/hull`, `ships/teladi/teladi_TS_liquid_M`, `ships/teladi/teladi_TS_liquid_S`, `ships/teladi/teladi_TS_liquidstorage_L`, `ships/teladi/teladi_TS_liquidstorage_M`, `ships/teladi/teladi_TS_storage_M`, `ships/teladi/teladi_TS_storage_S`, `ships/teladi/teladi_ts_vulture/hull`, `ships/teladi/teladi_ts_vulture_advanced/hull`, `ships/teladi/teladi_ts_vulture_hauler/hull`, `ships/teladi/teladi_ts_vulture_sf/hull`, `ships/teladi/teladi_ts_vulture_sf_xl/hull`, `ships/teladi/teladi_ts_vulture_tanker/hull`, `stations/trading_stations/teladi_trading_station_partA`, `stations/trading_stations/teladi_trading_station_partB`.
Three of them were baked with the tools before and after the change (`split_TL`, `teladi_M6`,
`teladi_trading_station_partA`): the diffuse atlas differs and the other four members are
byte-identical [m]. That the other 20 change in the diffuse atlas only is inferred from the same
code path. What the game shows on those parts is not measured; the list belongs to the open run
observations.

## 8. What each path records into the parameter block (2026-09-29)

Both paths share `BeginParameterBlock` (`0x004c0df9`) and the four colour parameters that follow it:
`g_Brightness 1`, `g_Contrast 1`, `g_Saturation 1`, `g_Hue 0` (`0x004c0e2c..0x004c0e6b`) [s]. The
branch is at `0x004c0e6e`.

| | classic path | effect path |
|---|---|---|
| range | `0x004c137d..0x004c1972` | `0x004c0e83..0x004c1378`, then straight to `0x004c197a` |
| constants | everything in §3 | **only the material's own parameter list**; a parameter it omits keeps the instance's last value |
| by semantic (`0x004ba500`) | — | textures 1..8 and the map channels are skipped (bound per draw); `g_CullMode` (`0x11`) is recorded as 1 when the value is 1, else 3 (`0x004c1284..0x004c129e`); `t_MinFilterTypeDiffuse` (`0x0e`) and `t_FilterTypeDiffuse` (`0x0f`) are recorded with the device's choice, the material's value is ignored (`0x004c12bf..0x004c1328`); `g_ALPHATESTENABLE` (`0x12`) = 1 also forces `g_AlphaBlendEnable 0`, `g_ZWriteEnable 1` (`0x004c1355..0x004c1378`); names starting `I_` are skipped; the rest goes to the generic setter `0x004b93a0` |
| `g_ALPHATESTENABLE` | recorded only when the flags ask for alpha test; an opaque record inherits it | as listed |
| `g_Wrap` | 3 when `*(*arg1)+0x60 & 2` (part flag 2, inferred), else 0 | as listed |
| `g_nNumLightPoint` | 0 recorded (`0x004c137d`) | not recorded |
| colour matrix | not touched again | the four defaults are recorded again when the material has a `colormatrix` parameter with value 0 (`0x004c197a..0x004c19c7`) |
| effect instance | index `(flags >> 4) & 1` of `standard_lighting` | index 0 of the name the record stores; `max_standard_lighting` and `standard_lighting` both select `standard_lighting` (`0x004c0880..0x004c08c5`); a `g_CullMode` parameter of 1 selects the `<name>2s` container when it exists (`0x004c08cd..0x004c0957`) |
| technique | `BUMPMAP_LOW` / `DEFAULT` | `BUMPMAP` when the bump id is above 0 and node `+0x130 & 0x100000` is clear, else `DEFAULT` (`0x004c098f..0x004c09ca`) |

`ApplyParameterBlock` (`0x004c1edd` / `0x004c1ef4`) is followed by the per-draw writes in
straight-line code, so a per-draw value wins over a recorded one on both paths.

1,518 shipped effect materials in 530 bodies name `standard_lighting.fx`
(`nonfx_bake_shipped_standard_lighting.py` over the winning members of the 4,613 bodies of the record,
4,455 read; 749 of them in `02.cat`). All carry technique word 1 and flags `0x02000000`; `g_CullMode`
is 2 in 951, 1 in 564 and absent in 3 [m]. The earlier figures 34 and 36 were partial counts of the
dock bodies. The strings the effect path names at `0x004c0880..0x004c09ca` (`max_standard_lighting`,
`standard_lighting`, `2s`, `BUMPMAP`, `DEFAULT`) are listed by `nonfx_bake_listing.py evidence` [m].
They are the evidence that a record naming `standard_lighting.fx` loads and draws through the
effect path with the effect file the classic branch uses; no synthesised one has been drawn yet.

## 9. The replacement as the baker builds it (2026-09-29)

`tools/analysis/lod_atlas.py` (`classic_view`, `classic_material`, `classic_kept`). The body's own
records are never rewritten; the merged materials are appended.

| parameter | value |
|---|---|
| record | `MAT6`, flags `0x02000000`, technique word 1, effect `standard_lighting.fx` |
| `g_Wrap 0`, `g_CullMode 2`, `g_AlphaBlendEnable 0`, `g_BlendOp 1`, `g_SrcBlend 2`, `g_DestBlend 1`, `g_ZEnable 1`, `g_ZWriteEnable 1`, `g_ALPHATESTENABLE 0` | the classic block's opaque state (`g_CullMode 2` is recorded as 3) |
| `g_MatDiffuseStrength`, `g_MatSpecularStrength`, `g_MatSpecularPower`, `g_MatEmissiveColor` | §3 values of the class; one class, and one merged material, per constant set |
| `g_MatReflectionStrength 0`, `g_AlphaValue 1.0` | — |
| `t_MinFilterTypeDiffuse`, `t_FilterTypeDiffuse` | declared so that the engine records its own filter choice |
| `t_DiffuseTexture`, `t_LightMapTexture` | the atlases; an untextured record's tile is the `NONE_GRAY` texel, a record without a light map has a black tile with alpha 0 |
| `t_BumpTexture` | only in a model with the tangent declaration that has a `BUMPMAP_LOW` classic record: the bump atlas, whose tile is the `NONE_NORMAL` normal of §6 for a `BUMPMAP_LOW` record and flat for a `DEFAULT` one |
| no `t_SpecularTexture`, no `t_CubeMapTexture` | `NONE_WHITE` fallback, no reflection |

Kept as their own groups, with their own record and UVs: blended and alpha-tested records (the
`155.jpg` markers), two-sided and point-filtered records, records with a cube map, a bump map that
loads a file, or a specular map that changes the image, `MAT5` records, texture animations, and
records used by a part with flag 2. Also kept: wireframe records (`0x8`), a record with map 0
beside a bump map (the engine replaces the diffuse texture by map 0 when arg 4 has bit 2,
`0x004c04b4..0x004c04d0`), a bump name that loads no file and does not end in `bump` (the loader
binds the placeholder its suffix selects, `NONE_BLACK` by default), and a `BUMPMAP_LOW` record of a
model without the tangent declaration (§6; `lod_atlas.CLASSIC_MISSING_TANGENT_IS_ZERO`, off until
the two assumptions are established). A body whose opaque materials are all kept is refused
`classic_unsupported`: the five bodies of §6. No bump atlas is baked for a model without the tangent
declaration that holds a `BUMPMAP_LOW` classic record, because a merged material naming one would
switch the whole model, its original records included, to the tangent declaration (0 bodies of the
fleet are in that case today [m]).

Residual per shape, `nonfx_bake_formula.py` over the 49 bodies with a merged classic material, 56
lighting conditions, in 8-bit steps [m]. "Formula" holds the texels fixed and compares technique,
normal and constants; "total" adds the DXT encoding of the atlas, which every atlas of the overlay
has.

| original → merged | tangents | bodies / materials | normal off by | formula max / mean | total max / mean |
|---|---|---|---|---|---|
| textured, light map: `BUMPMAP_LOW` → `BUMPMAP` | records | 45 / 45 | 0.29° | 4.47 / 0.064 | 100.7 / 3.56 |
| the same | zero | 45 / 45 | 0 | 0 / 0 | 137.6 / 3.60 |
| textured: `DEFAULT` → `BUMPMAP` (flat tile) | records | 45 / 51 | 1.15° | 11.92 / 0.251 | 114.4 / 0.73 |
| the same | zero | 45 / 51 | 0 | 0 / 0 | 117.6 / 0.61 |
| textured: `DEFAULT` → `DEFAULT` | either | 2 / 2 | 0 | 0 / 0 | 22.3 / 0.54 |
| untextured (`NONE_GRAY`): `DEFAULT` → `BUMPMAP` (flat tile) | records | 4 / 4 | 1.15° | 9.66 / 1.107 | 9.66 / 1.107 |
| the same | zero | 4 / 4 | 0 | 0 / 0 | 0 / 0 |
| untextured (`NONE_GRAY`), no specular: `DEFAULT` → `DEFAULT` | either | 2 / 2 | 0 | 0 / 0 | 0 / 0 |

The largest alpha difference is 17.1 steps (mean below 0.001), DXT encoding again. The shapes of the
bodies that are not baked (filtered `no_draw_gain`: the self-illuminated `dock9portsdummy` record,
the two `tm_machineSalvage` records with a bump name) are built by the same code but were not
measured.

The 1.15° of a `DEFAULT` record merged into a `BUMPMAP` material is the green channel of a flat
normal in DXT5 (128 is stored as 130 in 6 bits); the engine's own `NONE_NORMAL` has the same value.
Its largest effect is on the specular peak at power 31. Differences the comparison does not cover:

- An opaque classic record inherits `g_ALPHATESTENABLE` from the draw before it; the replacement
  sets 0.
- `g_Wrap` (part flag 2): guarded by keeping the record.
- The original draws one subset per material, the replacement one per class; per-draw state that
  depends on the subset order is not modelled.
- Nothing here was drawn in game.

## Unknown

- How the effect turns `g_Brightness`…`g_Hue` into `g_MatColor` (`c0..c2`): the expression lives in
  the effect file and was not decoded. Both paths feed it the same four values (§8).
- What a vertex shader input reads when the vertex declaration lacks it (`TANGENT0`/`BINORMAL0` in a
  model without the tangent declaration). Zero is assumed (§6); Direct3D 9 documents no value.
- The fourth column of the uploaded `g_mWorldIT`: the vertex shaders transform normal, tangent and
  binormal with `dp4` and `w = 1`, so a non-zero column would shift all three. Zero is assumed.
- Part flag 2 as the source of the classic block's `g_Wrap 3` (`*(*arg1)+0x60`, §8): that `*arg1` is
  the part is inferred from the offset. No shipped part of the census carries the bit.
- The profile directory string at device `+0x44` (`3_0` assumed; the `hueshift_off`,
  `hue_lights_off` and `v_lights_off` variants were not disassembled).
- The global `Materials` row path (`MAT3` and material-less bodies, `0x004c0592..0x004c061c`,
  `0x004c151f..0x004c1707`) was read only as far as the table in the listing.
- The per-draw `g_nNumLightPoint` write is taken to follow `ApplyParameterBlock` because the code
  from `0x004c1eab` to `0x004c2a60` is straight-line (no jump back before the apply); which of its
  calls is that write was not matched to a handle.
- Nothing here was checked in game.

No hook is proposed; nothing here is a hook site.

## Reproduce

```sh
# Ghidra (raw output stays local): fresh import, then
# -process X3AP.exe -noanalysis -readOnly -postScript X3DecompileFunctions.java <out> \
#   004c0150 004b9010 004b8f70 004b9060 004bb0f0 004bae10 004b9ed0 004ba500 00481780
# capstone: 0x004c0150..0x004c40fb, 0x00481f60..0x004820c0, 0x00484690..0x004846c4; MPF names 0x0054dbe0
cd verification/results/lod-mayhem-refusals
# Q2/Q4/Q5 census over the bake record (about 5 s, bottle X3, read-only)
PYTHONPATH=../../../tools/analysis python3 nonfx_materials.py > nonfx_materials_out.txt
PYTHONPATH=../../../tools/analysis python3 nonfx_textures.py > nonfx_textures_out.txt
# Q3 program identities; --disassemble writes shader text to a local directory outside the repo
PYTHONPATH=../../../tools/analysis:. python3 standard_lighting_programs.py > standard_lighting_programs_out.txt
# sections 6-8 (2026-09-29): capstone listings of the installed EXE. The searches the note rests on:
python3 nonfx_bake_listing.py evidence > nonfx_bake_listing_out.txt
# a raw listing of one range (keep the output outside the repository); the ranges are in the script's docstring
python3 nonfx_bake_listing.py list 0x004c0866 0x004c09d2 > /tmp/x3ap_004c0866.txt
python3 nonfx_bake_shipped_standard_lighting.py > nonfx_bake_shipped_standard_lighting_out.txt
python3 nonfx_bake_null_diffuse_scan.py > nonfx_bake_null_diffuse_scan_out.txt
# section 9: the bake evidence per nonfx_bake_commands.txt (nonfx_bake_check.py, nonfx_bake_formula.py,
# nonfx_bake_tangent_census.py, nonfx_bake_timing.py and their *_out.txt)
```
