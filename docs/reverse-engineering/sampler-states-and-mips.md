# Sampler state and mip configuration

How X3AP configures texture filtering and mip chains, established from static
disassembly of the executable identified in [executable.md](executable.md)
(SHA-256 `fdbf3418…4f8ab`) plus sampler state read back at live draws in the
user's flight/menu capture sessions. All addresses are preferred virtual
addresses for that hash only.

The question this answers: can we apply a negative mip LOD bias (about `-0.5`)
to the mip-mapped material stages that TAA jitter supersamples, and where does
such an override have to be issued so the game does not undo it?

Short answers:

1. The executable **never** sets `D3DSAMP_MIPMAPLODBIAS` or
   `D3DSAMP_MAXMIPLEVEL`. The whole material sampler configuration arrives from
   the `.fx` effect files through D3DX and the game's own
   `ID3DXEffectStateManager` implementation.
2. There **is** a state shadow in front of the device call, at `0x004b4fc0`,
   keyed per sampler and per state type. It suppresses redundant device calls.
3. Material textures are loaded with full DDS mip chains; the game's only
   mip-level control is a **top-level skip count** derived from
   `VideoTextureQuality`, not a LOD bias.
4. `MaxAnisotropy` comes from `D3DCAPS9.MaxAnisotropy`, clamped by the shader
   profile and gated on `VideoFilterMode`.

## 1. The sampler state path

### No meaningful direct device calls

`tools/analysis/X3FindVtableCalls.java` (added for this study) scans every
instruction for indirect vtable calls at a given slot displacement. X3AP.exe is
an MSVC `/O2` build that materializes the slot in a register first
(`MOV EAX,[ESI]` / `MOV EDX,[EAX+0x114]` / `CALL EDX`), so a naive
`call [reg+0x114]` byte scan finds nothing — only 31 of 23,230 `CALL`
instructions use the inline memory form.

Reproduce:

```sh
JAVA_HOME=/path/to/jdk /path/to/ghidra/support/analyzeHeadless \
  /tmp/x3-ghidra-research X3Render -process X3AP.exe -noanalysis -readOnly \
  -scriptPath tools/analysis \
  -postScript X3FindVtableCalls.java /tmp/x3-sampler-study/vt.txt 114 10C 5C
```

That reports 41 hits. Every call to `IDirect3DDevice9::SetSamplerState`
(slot 69, `+0x114`) with **constant** arguments belongs to the compositor /
fixed-function blit code, and touches only four state types:

| Call site | Containing function | Sampler | State | Value |
| --- | --- | ---: | --- | --- |
| `0x004c5704` | `0x004c55d0` | 0 | `MINFILTER` (6) | `POINT` (1) |
| `0x004c5715` | `0x004c55d0` | 0 | `MAGFILTER` (5) | `POINT` (1) |
| `0x004c5c3a` | `0x004c5830` | 0 | `MINFILTER` (6) | `POINT` (1) |
| `0x004c5c4b` | `0x004c5830` | 0 | `MAGFILTER` (5) | `POINT` (1) |
| `0x004c5c60` | `0x004c5830` | 0 | `ADDRESSU` (1) | `WRAP` (1) |
| `0x004c5c71` | `0x004c5830` | 0 | `ADDRESSV` (2) | `WRAP` (1) |

The remaining `+0x114` hits (`0x00487610`, `0x004c6222`) pass a single register
argument and are teardown/`Release`-shaped calls on other interfaces, not
sampler state.

`SetTextureStageState` (slot 67, `+0x10C`) constant call sites are the classic
fixed-function "show the texture" and "show vertex colour" setups:

| Call site | Function | Stage | `D3DTSS_` | Value |
| --- | --- | ---: | --- | --- |
| `0x004c5522` | `0x004c53d0` | 0 | `COLORARG1` (2) | `D3DTA_DIFFUSE` (0) |
| `0x004c5533` | `0x004c53d0` | 0 | `COLOROP` (1) | `SELECTARG1` (2) |
| `0x004c5544` | `0x004c53d0` | 0 | `ALPHAARG1` (5) | `D3DTA_DIFFUSE` (0) |
| `0x004c5555` | `0x004c53d0` | 0 | `ALPHAOP` (4) | `SELECTARG1` (2) |
| `0x004c5566` | `0x004c53d0` | 1 | `COLOROP` (1) | `DISABLE` (1) |
| `0x004c5577` | `0x004c53d0` | 1 | `ALPHAOP` (4) | `DISABLE` (1) |
| `0x004c5790`–`0x004c57c3` | `0x004c55d0` | 0 | `COLOROP`, `COLORARG1`, `ALPHAOP`, `ALPHAARG1` | `SELECTARG1` / `D3DTA_TEXTURE` |
| `0x004c57d4`, `0x004c57e5` | `0x004c55d0` | 1 | `COLOROP`, `ALPHAOP` | `DISABLE` (1) |
| `0x004c5cdb`–`0x004c5d30` | `0x004c5830` | 0, 1 | same pattern | same |

**`MIPFILTER` (7), `MIPMAPLODBIAS` (8), `MAXMIPLEVEL` (9), `MAXANISOTROPY` (10)
and `SRGBTEXTURE` (11) never appear as a constant in any direct device
`SetSamplerState` call.** Yet live captures show all of them at non-default
values on material draws (section 3), so they must come from somewhere else.

### The state shadow: the game's `ID3DXEffectStateManager`

[executable.md](executable.md) noted the RTTI text `ID3DXEffectStateManager`;
[ghidra-render-map.md](ghidra-render-map.md) noted that `0x004bae10` creates the
effect and then calls effect slot 71 (`+0x11c`, `SetStateManager`). That
implementation is now identified. Its vtable is at **`0x00562afc`** (the MSVC
complete-object-locator pointer sits at `0x00562af8`), and it has exactly the 21
`ID3DXEffectStateManager` slots:

| Slot | Offset | Method | Implementation |
| ---: | --- | --- | --- |
| 0–2 | `+0x00`–`+0x08` | `QueryInterface`, `AddRef`, `Release` | `0x004b4940`, `0x004b4990`, `0x004b49b0` |
| 3 | `+0x0c` | `SetTransform` | `0x004b4b30` |
| 4 | `+0x10` | `SetMaterial` | `0x004b4b50` |
| 5 | `+0x14` | `SetLight` | `0x004b4b70` |
| 6 | `+0x18` | `LightEnable` | `0x004b4b90` |
| 7 | `+0x1c` | `SetRenderState` | `0x004b4f80` |
| 8 | `+0x20` | `SetTexture` | `0x004b5160` |
| 9 | `+0x24` | **`SetTextureStageState`** | **`0x004b5010`** |
| 10 | `+0x28` | **`SetSamplerState`** | **`0x004b4fc0`** |
| 11 | `+0x2c` | `SetNPatchMode` | `0x004b4bb0` |
| 12 | `+0x30` | `SetFVF` | `0x004b4ab0` |
| 13 | `+0x34` | `SetVertexShader` | `0x004b50e0` |
| 14–16 | `+0x38`–`+0x40` | `SetVertexShaderConstantF/I/B` | `0x004b5340`, `0x004b4bf0`, `0x004b5240` |
| 17 | `+0x44` | `SetPixelShader` | `0x004b5120` |
| 18–20 | `+0x48`–`+0x50` | `SetPixelShaderConstantF/I/B` | `0x004b52a0`, `0x004b4c50`, `0x004b4c70` |

`0x004b4fc0` is `SetSamplerState(this, Sampler, Type, Value)`, `__stdcall`,
`RET 0x10`:

```
004b4fc0  PUSH EBX
004b4fc1  MOV  EBX,[ESP+0xc]      ; Sampler
004b4fc5  CMP  EBX,0xa            ; samplers 0..9 are shadowed
004b4fc9  MOV  EBP,[ESP+0xc]      ; this
004b4fce  MOV  EDI,[ESP+0x1c]     ; Value
004b4fd2  JNC  004b4ff5           ; Sampler >= 10 -> forward unconditionally
004b4fd4  MOV  ECX,[EBP+0x34]     ; this->shadow_base
004b4fd7  MOV  EDX,[ESP+0x18]     ; Type
004b4fdc  LEA  EAX,[EBX+EBX*2]
004b4fdf  LEA  ESI,[ECX+EAX*4]    ; &shadow_base[Sampler]  (12-byte record)
004b4fe2  PUSH EDX
004b4fe3  CALL 004b5720           ; shadow compare/update -> AL
004b4fe8  TEST AL,AL
004b4feb  JNZ  004b4ff5           ; changed -> forward to the device
004b4fed  ...  XOR EAX,EAX; RET 0x10   ; unchanged -> skip the device call
004b4ff5  ...  device->SetSamplerState(Sampler, Type, Value)   [+0x114]
```

`0x004b5010` (`SetTextureStageState`) is byte-for-byte the same shape, calling
device `+0x10C`, with the same `Stage < 10` guard and the same shadow helper.

The shadow helper `0x004b5720` is a keyed container lookup, `ESI` = the
12-byte per-sampler record, `EDI` = the new value, and the state `Type` as the
stack argument:

- `0x004b5733` → `0x004b5840`: find the entry for `Type` in the record.
- `0x004b573b`: entry not present → `0x004b5756` → `0x004b59c0` inserts the
  `(Type, Value)` pair and returns `AL = 1` (changed).
- `0x004b5763`: `CMP [EAX+0x10],EDI` — cached value equals the new value →
  `AL = 0`, **the device call is skipped**.
- `0x004b5770`: otherwise store the new value and return `AL = 1`.

### What this means for a proxy-side override

| Property | Consequence for us |
| --- | --- |
| Shadow is keyed by `(Sampler, Type)` and holds **the value the game last asked for**, not the value the device holds. | The game cannot detect our writes and will never "repair" them. |
| Redundant sets are dropped. | The game does not re-issue sampler state every draw; it issues it once per value change. There is no per-draw `SetSamplerState` for us to sequence after. |
| `MIPMAPLODBIAS` is not in the executable's set at all. | If the `.fx` files also never declare `MipLodBias`, no game write can ever collide with ours. |
| If an `.fx` *does* declare `MipLodBias`, the shadow caches it on the first apply and suppresses every later set of the same value. | Our override would stick permanently rather than being overwritten — which also means the game will **not** restore it for us. |

In both cases the ordering hazard the orchestrator asked about does not arise:
we do not have to race the game's `SetSamplerState`. **Restoring the previous
value is entirely our responsibility.** Leaving a `-0.5` bias resident would
also bias the compositor, bloom and UI samplers, which sample 1:1 render targets
with `MIPFILTER = NONE` or `POINT` and must not be biased.

Samplers `10..15` bypass the shadow entirely (`CMP EBX,0xa`), so any state the
game sets there is written to the device unconditionally. Live captures show
only stages 0–6 ever bound, so this does not matter in practice.

## 2. Texture creation and mip chains

### `D3DXCreateTextureFromFileInMemoryEx` (IAT thunk `0x004faf54`)

Three call sites. Arguments decoded from the push sequence
(`pDevice, pSrcData, SrcDataSize, Width, Height, MipLevels, Usage, Format, Pool,
Filter, MipFilter, ColorKey, pSrcInfo, pPalette, ppTexture`):

| Call site | Function | `MipLevels` | `Usage` | `Format` | `Filter` | `MipFilter` |
| --- | --- | --- | ---: | --- | --- | --- |
| `0x004dc841` | `0x004dc540` | variable (`EBX`) | **`0`** | `D3DFMT_UNKNOWN` (from file) | `D3DX_DEFAULT` | **computed mip-skip, see below** |
| `0x004dd69d` | `0x004dd2c0` | variable | **`0`** | variable | `D3DX_DEFAULT` | `D3DX_DEFAULT` |
| `0x004dec1b` | `0x004de9c0` | variable | **`0`** | table `0x0054e350` | `D3DX_DEFAULT` | `D3DX_DEFAULT` |

`Width`/`Height` are `D3DX_DEFAULT` at `0x004dc841`, i.e. the file's own size.

### `D3DXCreateCubeTextureFromFileInMemoryEx` (thunk `0x004faf4e`)

One call site, `0x004dc73d` in `0x004dc540`, gated on descriptor flag `0x200`
(`TEST [EDX+0x70],0x200` at `0x004dc6ff`):
`Size = D3DX_DEFAULT`, `MipLevels = EBX`, **`Usage = 0`**,
`Format = D3DFMT_UNKNOWN`, `Filter = D3DX_DEFAULT`,
`MipFilter = D3DX_FILTER_POINT (1)`.

### The mip-skip computation — the game's only mip-level control

At `0x004dc7bd`–`0x004dc80a`, immediately before `0x004dc841`:

```
004dc7bd  TEST EAX,0x40000        ; descriptor flag: never downscale this texture
004dc7c2  MOV  EDI,0x1            ; default MipFilter = D3DX_FILTER_POINT, skip 0
004dc7c7  JNZ  004dc80c           ; flagged -> use it as-is
004dc7c9  MOV  ECX,[0x00608b3c]   ; renderer
004dc7cf  MOV  EDX,[ECX+0x18]     ; device-side object
004dc7d2  MOV  ECX,[0x00606f34]   ; settings block
004dc7d8  MOV  EAX,[EDX+0x9c]     ; renderer texture-size budget
004dc7de  SUB  EAX,[ECX+0x750]    ; minus VideoTextureQuality
004dc7e4  LEA  ECX,[EAX+EBX-2]    ; + MipLevels - 2
004dc7e8  CMP  ECX,0x7
004dc7eb  JGE  004dc7f2
004dc7ed  MOV  ECX,0x7            ; clamp the keep-count to >= 7
004dc7f2  MOV  EAX,EBX
004dc7f4  SUB  EAX,ECX            ; skip = MipLevels - keep
004dc7f6  XOR  EDX,EDX
004dc7fa  SETLE DL                ; clamp a negative skip to 0
004dc7fd  SUB  EDX,EDI
004dc7ff  AND  EAX,EDX
004dc801  AND  EAX,0x1f
004dc804  SHL  EAX,0x1a           ; << D3DX_SKIP_DDS_MIP_LEVELS_SHIFT (26)
004dc807  OR   EAX,0x1            ; | D3DX_FILTER_POINT
004dc80a  MOV  EDI,EAX            ; MipFilter argument
```

This is exactly the `D3DX_SKIP_DDS_MIP_LEVELS(count, filter)` macro: the top
five bits of `MipFilter` carry a **count of top DDS mip levels to discard**.
`VideoTextureQuality` therefore lowers texture resolution by dropping the
sharpest levels of the stored chain; it is **not** a LOD bias and it does not
change the filtering of whatever levels survive. The surviving chain is still a
complete chain down to 1×1, so there is always headroom below level 0 for a
negative bias to sample into — and the level-0 image is already the sharpest
one the game will ever have for that texture.

### `IDirect3DDevice9::CreateTexture` (slot 23, `+0x5C`)

Only two functions call it. Arguments decoded as
(`Width, Height, Levels, Usage, Format, Pool, ppTexture, pSharedHandle`):

| Call site | Function | `Levels` | `Usage` | `Format` | `Pool` | Target |
| --- | --- | ---: | --- | --- | --- | --- |
| `0x004dca5b` | `0x004dc990` | **`0`** (full chain) | `0` | table `0x0054e350` | variable | runtime-built texture, flag `0x8000` set |
| `0x004dcab2` | `0x004dc990` | `1` | `0` | table `0x0054e350` | variable | same, flag `0x8000` clear |
| `0x004c43a7` | `0x004c4330` | `1` | `D3DUSAGE_RENDERTARGET` (`1`) | `D3DFMT_A8R8G8B8` (`0x15`) | `D3DPOOL_DEFAULT` | `0x00608a64` (scene map) |
| `0x004c4437` | `0x004c4330` | `1` | `D3DUSAGE_RENDERTARGET` | `D3DFMT_A8R8G8B8` | `D3DPOOL_DEFAULT` | `0x00608a68` (glow map 1) |
| `0x004c44af` | `0x004c4330` | `1` | `D3DUSAGE_RENDERTARGET` | `D3DFMT_A8R8G8B8` | `D3DPOOL_DEFAULT` | `0x00608a6c` (glow map 2) |

`TEST EAX,0x8000` at `0x004dca2f` selects the mip-chained variant, so runtime
textures are either fully mip-chained (`Levels = 0`) or single-level, decided by
a descriptor bit.

The compositor globals `0x00608a64`/`68`/`6c` match
[compositor-and-glow.md](compositor-and-glow.md) and
[runtime-passes.md](runtime-passes.md): `Levels = 1` A8R8G8B8 render targets,
which is why the bloom/composite samplers show `MIPFILTER = NONE` at runtime.

The `+0x5C` hits inside the material routine `0x004c0150` (`0x004c2a59`,
`0x004c2a70`, `0x004c300e`, `0x004c3022`) are three-argument calls on the
`ID3DXEffect` interface, not `CreateTexture`.

### `D3DUSAGE_AUTOGENMIPMAP` is not used

Every texture-creating call above passes `Usage = 0` or
`D3DUSAGE_RENDERTARGET`; `0x400` never appears as a usage argument. No
`SetAutoGenFilterType` (base-texture slot 13, `+0x34`) or
`GenerateMipSubLevels` (slot 15, `+0x3c`) call could be attributed to a texture
object — the displacement scan for `0x34`/`0x3c` produces 108 hits in the
`0x004b0000`–`0x004f0000` range, all of them on other interfaces (effect,
mesh, surface) once their argument counts are checked. Mip chains come from the
DDS files and, for runtime textures, from `Levels = 0`. This is consistent with
the live capture: every mip-mapped material texture is DXT1/DXT5, which D3D
cannot auto-generate mips for.

## 3. Which draw classes sample mip-mapped textures

Static analysis cannot show the `.fx` sampler blocks, so this comes from live
sampler state read back at each draw. The proxy's capture logs the five states
listed in `src/proxy/capture.cpp:374`. Parsed from the user's flight session
`x3-modern-captures/session-20260910-234001-212.log` (first 4,001 draws, 27
distinct `(vs, ps)` pairs), with the pass names taken from
[runtime-passes.md](runtime-passes.md),
[shader-family-review.md](shader-family-review.md),
[shader-fingerprints.md](shader-fingerprints.md),
[scene-boundary-selector.md](scene-boundary-selector.md) and
[compositor-and-glow.md](compositor-and-glow.md).

Aggregate over all bound sampler stages:

| `MIPFILTER` | `MINFILTER` | `MAXANISOTROPY` | Bound-stage observations |
| --- | --- | ---: | ---: |
| `LINEAR` | `ANISOTROPIC` | 16 | 23,983 |
| `NONE` | `LINEAR` | 16 | 3,756 |
| `POINT` | `POINT` | 4 | 120 |
| `LINEAR` | `LINEAR` | 16 | 67 |
| `NONE` | `POINT` | 16 | 40 |
| `POINT` | `LINEAR` | 16 | 16 |
| `POINT` | `LINEAR` | 4 | 12 |
| `LINEAR` | `LINEAR` | 4 | 6 |

Per draw class (`sN:MIN/MAG/MIP`):

| Draws | VS / PS | Effect family | Stage 0–2 (2D material maps) | Stage 3–4 (cube) | Stage 5–6 (32×32 LUTs) |
| ---: | --- | --- | --- | --- | --- |
| 1,231 | `494fe349…` / `fffdabd9…` | `xt_standard_lighting`, `…_damage` | `ANI/LIN/LIN` a16 | `LIN/LIN/NONE` | `ANI/LIN/LIN` a16 |
| 940 | `b0602757…` / `517540ae…` | `asteroid` | `ANI/LIN/LIN` a16 | mixed `ANI/LIN/LIN`, `LIN/LIN/NONE` | `ANI/LIN/LIN` a16 |
| 435 | `37c34a74…` / `5f82ecac…` | `xt_standard_lighting` | `ANI/LIN/LIN` a16 | `ANI/LIN/LIN`, `LIN/LIN/NONE` | `ANI/LIN/LIN` a16 |
| 360 | `53a0a641…` / `8759c783…` | `argon` (station) | `ANI/LIN/LIN` a16 | `LIN/LIN/NONE` | `ANI/LIN/LIN` a16 |
| 292 | `167eb2d5…` / `d44db877…` | `asteroid` | `ANI/LIN/LIN` a16 | `ANI/LIN/LIN` | `ANI/LIN/LIN` a16 |
| 132 | `494fe349…` / `7c83ed50…` | `standard_lighting` | `ANI/LIN/LIN` a16 | `LIN/LIN/NONE` | `ANI/LIN/LIN` a16 |
| 98 + 53 + 8 + 6 | `4944d81d…` / `ca6bfa4a…`, `5e0a10fe…`, … | `argon`, `argon2s` | `ANI/LIN/LIN` a16 | `ANI/LIN/LIN` | `ANI/LIN/LIN` a16 |
| 97 | `c78b4c68…` / `00000000` | `z_only` depth prepass (no PS) | `LIN/LIN/LIN` a16 **or** `POI/POI/POI` a4 | `LIN/LIN/NONE` | `ANI/LIN/LIN` a16 |
| 74 | `7b6393fe…` / `6109cf64…` | sky / `gui2d`, `nebula` | `LIN/LIN/LIN` a16 **or** `POI/POI/NONE` | `LIN/LIN/NONE` | `ANI/LIN/LIN` a16 |
| 24 | `803ebfd1…` / `652a7c5d…` | `z_only` | `POI/POI/POI` a4 | `LIN/LIN/NONE` | `ANI/LIN/LIN` a16 |
| 8 | `ac2319bc…` / `03a16e5c…` | `adeffects` | `LIN/LIN/LIN` a16 | `LIN/LIN/NONE` | `ANI/LIN/LIN` a16 |
| 7 each | `cbbf2610…`/`1c90e796…`, `6059…`/`241c3fa3…`, `6059…`/`f3172baa…`, `1279…`/`ff6eed5a…` | `bloom` downsample / blur / composite | `LIN/LIN/POINT` | `LIN/LIN/NONE` | `ANI/LIN/LIN` a16 |
| 4 | `be199829…` / `cd6d6eb4…` | `planet_haze` | `ANI/LIN/LIN` a16, then `LIN/LIN/LIN` | `LIN/LIN/NONE` | `ANI/LIN/LIN` a16 |
| 6 + 3 + 3 | `f36fc43f…`, `5e484a06…`, `36f98d15…` / `0a523f33…`, `222bee0d…` | `gui2d`, stardust/overlay | `POI/POI/NONE` or `LIN/LIN/LIN` a4 | `LIN/LIN/NONE` | `ANI/LIN/LIN` a16 |

Texture descriptors on the mip-mapped stages are DXT1/DXT5 at 128×128 up to
2048×1024; the cube maps at stages 2–4 are unmipped; the stages 5–6 LUTs are
32×32 DXT5/A8R8G8B8/DXT1.

Conclusions for a bias applied **only** to the routed motion-output draws (the
transformed SM3 material pairs — per
[motion-output-profiles.md](motion-output-profiles.md) the route substitutes
variants keyed by the `(VS, PS)` pair, e.g. VS `37c34a7478544c14` with PS
`31445adb0a62d134` / `d51cf763125cb85a`):

- **Ship and station materials** (`xt_standard_lighting`, `argon`,
  `standard_lighting`) and **asteroids** are the target and the beneficiaries:
  stages 0–2 are trilinear over a real DXT mip chain with anisotropic
  minification. These are exactly the stages that alias under jitter and where a
  `-0.5` bias buys back the detail TAA is supersampling.
- **Planets / `planet_haze`** also sample a mip-mapped stage 0 and would be
  affected if their pair is routed.
- **Backgrounds / skybox / nebula** (`7b6393fe…`/`6109cf64…`): stage 0 is
  either unmipped (`MIP = NONE`) or `LIN/LIN/LIN`. A bias is harmless where
  `MIPFILTER = NONE` (D3D ignores the bias with no mip chain) but would sharpen
  and alias a mipped sky. These pairs are shared with `gui2d`, so pair-keyed
  routing is the only safe discriminator — as
  [runtime-passes.md](runtime-passes.md) already warns, "after bloom == HUD" is
  not a valid partition.
- **Particles, stardust and overlays** (`0a523f33…`): stage 0 is
  `POINT/POINT/NONE` or unmipped `LINEAR`. Not affected; and
  [motion-output-profiles.md](motion-output-profiles.md) already excludes
  particles/stardust/overlays from the route.
- **UI / `gui2d`** and the **bloom chain**: `MIPFILTER` is `NONE` or `POINT` on
  1:1 render targets. These are outside the routed set and must stay unbiased —
  a resident bias would visibly soften or alias the composite. This is the
  reason the override has to be scoped, not global.
- **`z_only` depth prepass**: `POINT/POINT/POINT` a4 or `LIN/LIN/LIN`. A bias
  here would change nothing visible but also buys nothing; leave it alone.

Because the route only substitutes shader variants for specific `(VS, PS)`
pairs, a bias applied at the routed draws automatically lands on the ship,
station, asteroid and planet material stages and automatically misses the
compositor, bloom and UI.

## 4. Anisotropy: caps and configuration

The renderer does read `D3DCAPS9`. The caps block is embedded at offset `0x454`
inside the object at `[[0x00608b3c] + 0x18] + 4`, which makes
`+0x494` = `TextureFilterCaps` (`D3DCAPS9 + 0x40`) and
`+0x4c0` = `MaxAnisotropy` (`D3DCAPS9 + 0x6c`).

Inside the device-init routine `0x004d8f10`:

```
004d9a88  MOV  EDX,[0x00608b3c]
004d9a8e  MOV  EAX,[EDX+0x18]
004d9a91  MOV  ECX,[EAX+0x4]
004d9a94  MOV  EBP,0x400
004d9a99  TEST [ECX+0x494],EBP     ; TextureFilterCaps & D3DPTFILTERCAPS_MINFANISOTROPIC
004d9a9f  JZ   004d9b7d
004d9aa5  CMP  [EDI+0x75c],ESI     ; VideoFilterMode below threshold?
004d9aab  JL   004d9b7d
004d9ab1  TEST [EDI+0x100],0x200   ; renderer flags bit
004d9abb  JZ   004d9b7d
004d9ac1  MOV  byte ptr [EAX+0x96],0x1    ; anisotropy enabled
...
004d9b2c  MOV  EAX,[[…]+0x4c0]     ; D3DCAPS9.MaxAnisotropy
004d9b32  CMP  EAX,0x10
004d9b37  MOV  EAX,0x10            ; clamp to 16  (ps_3_0 branch, label "16x")
004d9b4f  MOV  EAX,[[…]+0x4c0]
004d9b55  CMP  EAX,0x2
004d9b5a  MOV  EAX,0x2             ; clamp to 2   (ps_1_4 branch, label "2x")
004d9b64  MOV  [ECX+0x98],EAX      ; effective max anisotropy
004d9b7d  MOV  byte ptr [EAX+0x96],0x0    ; anisotropy disabled
```

So the effective anisotropy is `min(D3DCAPS9.MaxAnisotropy, 16)` on the
`ps_3_0` profile and `min(…, 2)` on `ps_1_4`, reported through the `"16x"`
(`0x00563e54`) / `"2x"` (`0x00563e48`) strings that feed the
`"Anisotropic Mode:  %s"` benchmark banner at `0x00554f78` / `0x005557b0`. The
live capture's `MAXANISOTROPY = 16` on material stages and `4` on some
compositor/`z_only` stages is consistent with the `ps_3_0` branch.

### Configuration path

Settings are read in `0x004b6f60` (load) and written in `0x004b7b40` (save)
through consecutively stored names into the block at `[0x00606f34]`:

| Setting name | String VA | Settings offset | Consumer |
| --- | --- | --- | --- |
| `VideoViewDistance` | `0x00561818`* | `+0x74c` | — |
| `VideoTextureQuality` | `0x00562c2c` | `+0x750` | mip-skip count at `0x004dc7de` |
| `VideoShaderQuality` | — | `+0x754` | shader profile (`ps_3_0` / `ps_1_4`) |
| `VideoAntialiasMode` | `0x00562c54` | `+0x758` (stored at `0x004b71bf`) | — |
| `VideoFilterMode` | `0x00562c68` | `+0x75c` (stored at `0x004b71f4`) | anisotropy gate at `0x004d9aa5` |

\* `VideoViewDistance` appears only as a raw string; the five names are
consecutive in the string table, and `+0x758`/`+0x75c` are confirmed by the
explicit stores, which fixes `VideoTextureQuality` at `+0x750` — the offset the
mip-skip code reads.

The values reach the effects as parameters, set through the helper `0x004b8f70`
(`ID3DXBaseEffect::GetParameterByName`, slot 9 / `+0x24`, then a set):

| Effect parameter | String VA | Set at | Value |
| --- | --- | --- | --- |
| `t_SamplerMaxAnisotropy` | `0x005634cc` | `0x004c12f6`, `0x004c1955` | `[[0x00608b3c]+0x18]+0x98` (16 or 2) |
| `t_MinFilterTypeDiffuse` | `0x005632a0` | `0x004c12dd`, `0x004c191c`, `0x004c130a` | `3` = `D3DTEXF_ANISOTROPIC` when `+0x96` set, else `2` = `LINEAR`; `1` on a material flagged `0x100` |
| `t_FilterTypeDiffuse` | `0x005632b8` | `0x004c1969` | `2` = `D3DTEXF_LINEAR` |

`t_SamplerMaxAnisotropy` is the **only** `t_Sampler*` string in the
executable, and a string search for `Bias`, `LodBias` and `MipFilter` finds
nothing. The `.fx` sampler blocks therefore parameterise filter type and
anisotropy only; every other sampler state in them is a literal. There is no
game-side LOD-bias knob to reuse or fight.

## 5. Recommended approach for a negative mip LOD bias

### Capture must be extended first

`src/proxy/capture.cpp:374` currently reads back only:

```
D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER, D3DSAMP_MIPFILTER,
D3DSAMP_MAXANISOTROPY, D3DSAMP_SRGBTEXTURE
```

The next diagnostic run must add:

- **`D3DSAMP_MIPMAPLODBIAS` (8)** — the decisive unknown. Static analysis proves
  the executable never sets it, but it cannot see the `.fx` sampler blocks. If a
  capture shows a non-zero bias on material stages, the `.fx` files declare one
  and our value must be composed with theirs rather than replacing it. The value
  is a `float` bit pattern in a `DWORD`, so it must be logged reinterpreted
  (e.g. `memcpy` into a `float` and print `%g`) as well as raw — the existing
  `value=%lu` format would print `0xBF000000` as `3204448256`.
- **`D3DSAMP_MAXMIPLEVEL` (9)** — proves whether the surviving chain's level 0
  is actually reachable. If a stage has `MAXMIPLEVEL > 0`, a negative bias on
  that stage is clamped and buys nothing.

Adding `D3DSAMP_ADDRESSU`/`ADDRESSV` at the same time would also let us confirm
the `WRAP` values seen statically at `0x004c5c60`/`0x004c5c71`, at no extra
cost. Note that the existing `verification/results/*capture*.log` files are all
probe-harness output (`MIN/MAG = POINT`, `MIP = NONE`, `ANISO = 1` on every
stage); only the bottle's `x3-modern-captures/session-*.log` files contain real
material sampler state, so the new enums must be verified against a real
session, not the probe.

### The override itself

- **Scope it to the routed draws.** Set the bias when the motion-output route
  substitutes a material variant and restore it when the draw completes, in the
  same place and the same style as `src/renderer/hdr_pass.cpp:138`, which
  already declares `D3DSAMP_MIPMAPLODBIAS` and `D3DSAMP_MAXMIPLEVEL` in its
  `touched_samplers` save/restore set. That existing pattern is the precedent to
  follow; reusing it keeps the compositor, bloom and UI samplers untouched.
- **Only bias stages whose bound texture has a mip chain.** From section 3 that
  is stages 0–2 and 5–6 on the material pairs; stages 3–4 are cube maps with
  `MIPFILTER = NONE`, where the bias is inert but pointless. Restricting to
  stages with `MIPFILTER != NONE` is cheap and avoids touching state the game
  never set.
- **Ordering is a non-issue.** The game's shadow (section 1) means sampler state
  is written once per value change, not per draw, and `MIPMAPLODBIAS` is never
  among the states the executable writes. We do not need to apply the bias
  "after" any game call. The cost is that the game will never restore it, so the
  restore must be ours and must be unconditional, including on the early-out
  and failure paths.
- **Magnitude.** `-0.5` is one half level. The mip chains are complete down to
  1×1 (section 2), so there is always a level to interpolate toward, and level 0
  is the sharpest image available — a negative bias cannot invent detail beyond
  it, it only shifts the trilinear blend toward level 0. It composes with the
  game's `VideoTextureQuality` mip-skip additively in effect: a high skip count
  means level 0 is already a lower-resolution image, so the bias sharpens toward
  that, not toward the original file's top level.
- **Anisotropy interaction.** Material stages run `MINFILTER = ANISOTROPIC` with
  `MAXANISOTROPY = 16`. A negative LOD bias with anisotropic minification
  increases sample counts and can cost fill rate; the performance pass must
  measure this on the routed draws rather than assume it is free.

## Tooling added

`tools/analysis/X3FindVtableCalls.java` — scans for indirect vtable calls at
given slot displacements, handling both `CALL dword ptr [REG+disp]` and the
`MOV REG,[REG2+disp]` / `CALL REG` form X3AP.exe actually emits, and reports the
containing function plus the preceding `PUSH` operands. It prints addresses and
integer operands only, never decompiler output.

Caveat for reuse: a slot displacement alone does not identify an interface.
`+0x24` matches both `ID3DXEffectStateManager::SetTextureStageState` and
`ID3DXBaseEffect::GetParameterByName`; `+0x1c` matches both
`ID3DXEffectStateManager::SetRenderState` and other slot-7 methods. Every hit in
this document was disambiguated by argument count and by whether the constant
arguments are valid values of the expected enum.

All decompiler and listing output for this study stayed under
`/tmp/x3-sampler-study`.
