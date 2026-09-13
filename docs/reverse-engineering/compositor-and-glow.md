# The bloom compositor `0x004c4750`, the glow option and the scene-end boundary

Static analysis only (Ghidra 12.1.3, `-readOnly -noanalysis`) of the installed
`X3AP.exe`, SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`, preferred
base `0x00400000`; plus in-memory reads of the installed CAT/DAT archives and a
query over the existing flight capture
`verification/results/game-flight-capture-summary.json`. No game or device was
launched. Decompiler output and archive slices stayed under the session
scratchpad and are not committed.

This closes the reverse-engineering gap recorded in
[hdr-scene-path.md](../architecture/hdr-scene-path.md) §9.

Device vtable displacements follow the numbering in
[constant-uploads.md](constant-uploads.md). `ID3DXBaseEffect`/`ID3DXEffect`
slots used below: 9 `GetParameterByName` `0x24`, 13 `GetTechniqueByName` `0x34`,
14 `GetPass` `0x38`, 19 `GetAnnotationByName` `0x4c`, 26 `SetInt` `0x68`,
30 `SetFloat` `0x78`, 34 `SetVector` `0x88`, 51 `GetString` `0xcc`,
52 `SetTexture` `0xd0`, 58 `SetTechnique` `0xe8`, 61 `FindNextValidTechnique`
`0xf4`, 63 `Begin` `0xfc`, 64 `BeginPass` `0x100`, 66 `EndPass` `0x108`,
67 `End` `0x10c`.

## 1. The option test — glow is `VideoD3DFlags2` bit `0x80`

The compositor's first two instructions after the SEH prologue are the whole
gate:

```
004c4765  MOV EAX,[0x00606f34]
004c4770  TEST byte ptr [EAX + 0x100],0x80      ; glow enabled?
004c477b  JZ  0x004c4f48                        ; -> return, nothing is issued
004c4781  MOV ECX,[0x00608b3c]
004c4787  MOV EAX,[ECX + 0x18]
004c478a  CMP byte ptr [EAX + 0x94],0x0         ; device "glow available" flag
004c4791  JZ  0x004c4f48
```

| Test | Address | Meaning |
| --- | --- | --- |
| `*(*0x00606f34 + 0x100) & 0x80` | `0x004c4770` | **user glow setting**; 0 = whole compositor is a no-op |
| `*(*0x00608b3c + 0x18) + 0x94 != 0` | `0x004c478a` | device/shader-profile capability flag, set in device init `0x004d8f10` (`0x004d97fc` → 0, `0x004d990f` → 1, `0x004d9fad` → 0 on the fallback path) |
| `0x00608a64`, `0x00608a68`, `0x00608a6c`, `0x00608a74` all non-null | `0x004c4799`…`0x004c47c3` | scene map, glow map 1, glow map 2, quad vertex buffer (see §3). `0x00608a70` (the vertex declaration) is **not** checked here although it is used at `0x004c4d43` |
| bloom effect present | `0x004c47d2` `CALL 0x004bb0f0("bloom" @0x563020)` with `EAX = 0`; result `[rec+4]` non-null | loads `shader\<profile>\bloom.fb` on demand through the effect loader `0x004bae10` |

There are **no quality levels**: the bit is boolean and the technique is fixed
(§2).

### Where the option word comes from

`*0x00606f34` is the global settings structure. The two render-option words are
loaded from the registry by `FUN_004b6f60`:

```
HKEY_CURRENT_USER\Software\EGOSOFT\<app name from *0x00606f34 + 0xd8>
    "VideoD3DFlags"   (REG_DWORD) -> *0x00606f34 + 0xfc
    "VideoD3DFlags2"  (REG_DWORD) -> *0x00606f34 + 0x100   <- glow bit 0x80
```

(`0x004b7291` is the `VideoD3DFlags2` store; siblings in the same function are
`VideoAdapterName/Ordinal`, `VideoWidth/Height`, `VideoMode`, `VideoViewDistance`,
`VideoTextureQuality`, `VideoShaderQuality`, `VideoAntialiasMode`,
`VideoFilterMode`, `VideoFullscreenGamma`.) `*0x00606f34 + 0xfc` is the word the
per-view clear `0x004bb280` tests — a *different* word from the glow one.

**Menu handler.** The graphics-options apply handler writes the same word, one
menu item per bit. Each block reads the item's checked state and sets or clears
a bit:

| Menu text id | Bit written | Site | Text (page 1912 "Graphic Settings dialog", L044) |
| ---: | --- | --- | --- |
| `0x4d9` (1241) | `+0x100` `0x200` | `0x004cd0f4` / `0x004cd100` | `A&nisotropic Texture Filtering` |
| **`0x4da` (1242)** | **`+0x100` `0x80`** | **`0x004cd128` / `0x004cd134`** | **`&Glow enabled`** |
| `0x4e7` (1255) | `+0x100` `0x800` (inverted) | `0x004cd15c` / `0x004cd168` | `Ship &Colour Variations` |
| `0x4e6` (1254) | `+0x100` `0x1000` (inverted) | `0x004cd190` / `0x004cd19c` | `More Dynamic &Light Sources` |
| `0x4e8` (1256) | `+0x100` `0x2000` | `0x004cd1c4` / `0x004cd1d0` | `Disable &Vertex Size Optimisation` |
| `0x4ea` (1258) | `+0x100` `0x4000` | `0x004cd1f8` / `0x004cd204` | not present on page 1912 |
| `0x4c6` (1222) | `+0xfc` `0x1` | `0x004cd0c6` / `0x004cd0cf` | antialiasing group |

The menu *builder* reads the same bit back to set the checkbox:
`0x004ccd84` pushes `0x4da`, `0x004ccd95` tests `[+0x100] & 0x80`; likewise
`0x004cd48e` / `0x004cd49e`. The text was read from `t/0001-L044.pck`
(`<page id="1912" title="Graphic Settings dialog">`, `<t id="1242">`).

Two other consumers of bit `0x80`, both consistent:

- device init clears it when the capability flag is false:
  `0x004da001 CMP byte [dev+0x94],0` → `0x004da00f AND [*0x00606f34+0x100],~0x80`.
- the material routine `0x004c0150` tests it at `0x004c36a5`, `0x004c3777` and
  `0x004c3e0b` to drive the `g_EnableGlow` effect parameter (§4).

Other bits of the same word seen in the frame path: `0x100` disables the
environment-map pass (`0x004721d7`, `0x0047e82a`) and is *set* by `0x004c4304`.

## 2. Device-call sequence of `0x004c4750`

### Glow off (`bit 0x80` clear, or the capability flag false)

`0x004c4750` issues **nothing**: the `JZ 0x004c4f48` at `0x004c477b` (or
`0x004c4791`) jumps past the SEH frame teardown to `RET`. No `GetRenderTarget`,
no `StretchRect`, no state change, no draw. The frame simply continues with the
back buffer holding the scene as the material passes left it. The same is true
when any of the four resource globals is null.

### Glow on

Ordered, with the call sites. `dev` = `*(*(0x00608b3c+0x18))` (the
`IDirect3DDevice9`), `fx` = the `bloom` effect, `sm` = `*(0x00608b3c+0x1c)`
(the `ID3DXEffectStateManager`-side helper object).

| # | Site | Call | Notes |
| ---: | --- | --- | --- |
| 1 | `0x004c4817` | `dev->GetRenderTarget(0, &saved_rt)` `0x98` | retried once; **the saved main back-buffer surface, the only RT0 the engine ever binds** |
| 2 | `0x004c4852` | `dev->GetDepthStencilSurface(&saved_ds)` `0xa0` | retried once |
| 3 | `0x004c4884` | `dev->SetDepthStencilSurface(NULL)` `0x9c` | depth is off for the whole compositor |
| 4 | `0x004c48ab` | `fx->GetTechniqueByName("DEFAULT")` `0x34` → `0x00661968` | once per process, latched by `0x0066196c` bit 1 |
| 4a | `0x004c48d4` | `fx->FindNextValidTechnique(NULL, &0x00661968)` `0xf4` | only if the named technique is absent; failure unwinds and returns |
| 5 | `0x004c4964` | `fx->SetTechnique(0x00661968)` `0xe8` | |
| 6 | `0x004c498d` / `0x004c49c2` / `0x004c49fa` / `0x004c4a32` | `fx->GetParameterByName(NULL, "t_SceneMap"/"t_GlowMap1"/"t_GlowMap2"/"ViewPortSize")` `0x24` → `0x00661964` / `0x00661960` / `0x0066195c` / `0x00661958` | latched by `0x0066196c` bits 2/4/8/0x10 |
| 7 | `0x004c4a74` / `0x004c4b26` / `0x004c4b57` | `tex->GetSurfaceLevel(0, &surf)` `0x48` on `0x00608a64` / `0x00608a68` / `0x00608a6c` | builds a 3-entry `{surface, name}` table (stride `0x20`) keyed by the parameter names |
| 8 | `0x004c4b8e` / `0x004c4ba1` / `0x004c4bb7` | `dev->ColorFill(surf, NULL, 0x00000000)` `0x8c` | clears scene map and both glow maps to black |
| 9 | `0x004c4bca` / `0x004c4bdf` / `0x004c4bf5` | `fx->SetTexture(h, tex)` `0xd0` via `0x004b9ed0` | `t_SceneMap←0x00608a64`, `t_GlowMap1←0x00608a68`, `t_GlowMap2←0x00608a6c` |
| 10 | `0x004c4c4b` | `fx->SetVector(ViewPortSize, {W, H, 0, 0})` `0x88` | `W`/`H` are the `int16` at `+4`/`+6` of `*(*0x00606f38 + 4)` — the back-buffer size, the same source the textures were created from |
| 11 | `0x004c4c6e` | `dev->SetSoftwareVertexProcessing(TRUE)` `0x134` | only when `*(*(*(0x00608b3c+0x18)+4)+0x6d8) == 2`; **never restored** |
| 12 | `0x004c4c8c` | `dev->StretchRect(saved_rt, NULL, sceneMapSurface, NULL, D3DTEXF_LINEAR)` `0x88` | **the scene copy.** Retried once; failure unwinds and returns without drawing |
| 13 | `0x004c4d50` | `sm->slot 0x60 (0x00608a70)` | set vertex declaration |
| 14 | `0x004c4d6d` | `sm->slot 0x64 (0, 0x00608a74, 0, 0x18)` | set stream 0 = the 4-vertex quad, stride 24 |
| 15 | `0x004c4d80` | `sm->slot 0x68 (0)` | set indices NULL |
| 16 | `0x004c4d92` | `fx->Begin(&passes, D3DXFX_DONOTSAVESTATE)` `0xfc` | flags `= 1`; D3DX will not save/restore device state |
| 17 | loop `0x004c4da4`…`0x004c4e6e`, `passes` times | per pass, below | |
| 17a | `0x004c4dae` | `fx->BeginPass(i)` `0x100` | |
| 17b | `0x004c4dbe` | `fx->GetPass(technique, i)` `0x38` | |
| 17c | `0x004c4dd0` | `fx->GetAnnotationByName(pass, "RenderColorTarget0")` `0x4c` | |
| 17d | `0x004c4de7` | `fx->GetString(annotation, &name)` `0xcc` | |
| 17e | `0x004c4e00` | string compare (`0x00469700`) against the 3-entry table | missing annotation / failed `GetString` takes `saved_rt` (`0x004c4e22`). A successful string lookup with no table match does **not** take that fallback: it leaves the target local unchanged, potentially uninitialized on the first pass. See below. |
| 17f | `0x004c4e3b` | `dev->SetRenderTarget(0, target)` `0x94` | |
| 17g | `0x004c4e3f` | `0x004c6300(target)`: `surface->GetDesc` `0x30` then `dev->SetViewport` `0xbc` | viewport follows the target size |
| 17h | `0x004c4e54` | `dev->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2)` `0x144` | full-screen quad |
| 17i | `0x004c4e5f` | `fx->EndPass()` `0x108` | |
| 18 | `0x004c4e7d` | `fx->End()` `0x10c` | |
| 19 | `0x004c4e8c` / `0x004c4e9c` / `0x004c4eab` | `fx->SetTexture(h, NULL)` `0xd0` | unbinds the three textures |
| 20 | `0x004c4ebd`…`0x004c4eff` | `Release` on the four surface references | |
| 21 | `0x004c4f14` | `dev->SetDepthStencilSurface(saved_ds)` `0x9c`, then release | |

**Not restored on exit:** render target 0 (it is already `saved_rt`, because the
last pass has no `RenderColorTarget0`), the viewport (left at back-buffer size),
and `SetSoftwareVertexProcessing`.

The saved-RT and final-viewport statements describe the inspected stock effect's
successful path. At `0x004c4e10..12` only a matching annotation string writes
the target local `[ESP+0x28]`; exhausting the table branches straight to its
use at `0x004c4e2a`. An unknown annotation string can therefore reuse a prior
target or an uninitialized local. `SetRenderTarget`, viewport setup and the
quad draw results are not checked by this loop. Modified effects require
semantic target admission and successful-binding observation; do not interpret
an unknown string, a requested target or four attempted draws as proof of a
successful stock-style boundary. The proposed
[bloom boundary](../architecture/hdr-bloom-boundary.md) executes the original
fully, then overwrites only RGB while preserving its outgoing state and alpha.

### The passes, from `bloom.fb` and confirmed by capture

`shader/3_0/bloom.fb` (root `01.cat`, 51,720 bytes, bytewise XOR `0x33`, magic
`01 09 ff fe`) declares two techniques, `DEFAULT` and `HDR`, each with four
passes. The engine asks for `DEFAULT` by name.

| Pass | `RenderColorTarget0` | Resolved target | Flight-capture draw (frame 5449) |
| --- | --- | --- | --- |
| `DownSample` | `t_GlowMap1` | `0x00608a68`, half res | 95, ps `1c90e79667bdaddf`, rt0 `0195a3b0` 640×384 |
| `BlurGlowBuffer_Horz_X` | `t_GlowMap2` | `0x00608a6c`, half res | 96, ps `f3172baa8dd19a40`, rt0 `0195a430` 640×384 |
| `BlurGlowBuffer_Vert_Y` | `t_GlowMap1` | `0x00608a68`, half res | 97, ps `241c3fa33270f58e`, rt0 `0195a3b0` 640×384 |
| `FinalCombine` | *(absent)* | `saved_rt`, the back buffer | 98, ps `ff6eed5a5ddf3a3a`, rt0 `0190b7b8` 1280×768 |

`t_SceneMap` is never a pass target: the effect's technique script says
`RenderColorTarget0=t_SceneMap; … ScriptExternal=color`, i.e. the application
supplies the scene, which the engine implements as step 8 + step 12.

**Blend state, from the same capture** (render-state ids as recorded by
`capture.cpp`):

| Draw | `ZENABLE`7 | `ZWRITE`14 | `ALPHABLENDENABLE`27 | `SRCBLEND`19 | `DESTBLEND`20 | `BLENDOP`171 | `COLORWRITEENABLE`168 | `SRGBWRITEENABLE`194 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 95 `DownSample` | 0 | 0 | 0 | 5 | 6 | 1 | 15 | 0 |
| 96 `Horz_X` | 0 | 0 | 0 | 5 | 6 | 1 | 15 | 0 |
| 97 `Vert_Y` | 0 | 0 | 0 | 5 | 6 | 1 | 15 | 0 |
| **98 `FinalCombine`** | 0 | 0 | **1** | **2 = `ONE`** | **4 = `INVSRCCOLOR`** | 1 `ADD` | 15 | 0 |

So the composite is `dst = src + dst·(1 − src)` — a screen-style soft add over
the untouched back-buffer scene, not a plain `ONE`/`ONE` additive. The three
intermediate passes are opaque. Those states are set by the effect passes
through the state manager; the engine sets no blend state around the compositor.

## 3. Resources — created in `0x004c4330`, released in `0x004c46d0`

| Global | Creation | Type / size / format |
| --- | --- | --- |
| `0x00608a64` `t_SceneMap` | `0x004c43a7` `CreateTexture(W, H, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, …)` | full back-buffer size, 8-bit |
| `0x00608a68` `t_GlowMap1` | `0x004c4437` same, `W/2`, `H/2` | half res, 8-bit |
| `0x00608a6c` `t_GlowMap2` | `0x004c44af` same, `W/2`, `H/2` | half res, 8-bit |
| `0x00608a70` | `0x004c4523` `CreateVertexDeclaration(0x0054e960)` | `{0,0,FLOAT4,POSITION0}`, `{0,16,FLOAT2,TEXCOORD0}`, END — stride 24 |
| `0x00608a74` | `0x004c4582` `CreateVertexBuffer(0x60, D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, …)` | 4 vertices; filled through `Lock`/`Unlock` (`0x2c`/`0x30`) with clip-space corners `(±10, ±10, 10, 10)` (i.e. NDC ±1 after the `w = 10` divide, `z = 1`) and UVs `(0,0) (1,0) (0,1) (1,1)` from the floats at `0x005655c0 = 10.0f` and `0x005655c4 = −10.0f` |

`W`/`H` come from the `int16` at `+4`/`+6` of `*(*0x00606f38 + 4)`. This matches
the capture: main target 1280×768 `A8R8G8B8`, two 640×384 `A8R8G8B8`
intermediates ([runtime-passes.md](runtime-passes.md)).

**Complete reference set** for `0x00608a64/68/6c/70/74`: creation `0x004c4330`,
release `0x004c46d0`, use `0x004c4750`. Nothing else in the image touches them.

## 4. Where the glow constants come from

The compositor sets exactly four effect parameters — `t_SceneMap`,
`t_GlowMap1`, `t_GlowMap2`, `ViewPortSize` — and nothing else. A whole-image
string search finds **no reference** to `g_HighlightThreshold`, `SceneIntensity`,
`HighlightIntensity`, `LightMapGlowIntensity`, `g_BlurWidth`, `g_Sigma`,
`g_BlurWeightModifier`, `Exposure` or `Gamma`: the engine never overrides them,
so the artistic intent is entirely the effect file's compiled defaults.

Read from the `bloom.fb` parameter records (scalar layout
`Type=3 FLOAT, Class=0 SCALAR, name, semantic, 0, rows=1, cols=1, value`):

| Parameter | Default | UI label (annotation) |
| --- | ---: | --- |
| `Exposure` | 1.0 | Exposure (slider) |
| `Gamma` | 2.0 | Gamma (slider) |
| `g_BlurWidth` | 5.0 | Blur Width (slider) |
| `g_Sigma` | 0.87 | Sigma (slider) |
| `g_HighlightThreshold` | 1.0 | Highlight threshold (slider) |
| `SceneIntensity` | 0.9 | Scene intensity (slider) |
| `HighlightIntensity` | 2.0 | Highlight intensity (slider) |
| `LightMapGlowIntensity` | 1.2 | LightMap Glow intensity (slider) |
| `g_BlurWeightModifier` | *not decoded* (non-scalar record) | Blur Weight Modifier (slider) |

Interpretation for stage 2: highlights are selected at **1.0** against an 8-bit
scene copy (so, in practice, only fully-saturated pixels), multiplied by 2.0,
and added to 0.9× the scene through the `ONE`/`INVSRCCOLOR` composite; the blur
is a 5-tap-wide Gaussian with σ 0.87 at half resolution, run separably.
`Exposure`/`Gamma` exist in the parameter list and belong to the `HDR`
technique, which this build never selects.

**`g_EnableGlow` is a material parameter, not a bloom parameter.** Its only
reference is `0x004c1de0` inside the material routine `0x004c0150`, where
`GetParameterByName("g_EnableGlow")` is cached at `[effect_record + 0x190]`. The
material routine then drives it from the same option bit:

```
004c36a5  TEST byte ptr [ECX + 0x100],0x80   ; VideoD3DFlags2 glow bit
004c36ac  JZ  0x004c381a                     ; -> the SetFloat(0.0) path below
004c36bb  CMP byte ptr [EAX + 0x94],0x0      ; device capability flag
004c36c2  JZ  0x004c381a
...
004c36fe  FLD1                               ; 1.0f
004c380d  fx->SetFloat(h_g_EnableGlow, value) 0x78
...
004c3761  FLDZ                               ; 0.0f on the disabled path (004c3755)
```

So the material shaders' glow-mask output and the compositor are driven by one
bit, and stage 5 can gate on that single test.

## 5. Frame ordering, GUI and HUD

Exact per-view loop head of the frame routine `0x00471f50`:

```
00472166  MOV byte ptr [ESP + 0x13],BL       ; bloom_done = 0, before the loop
...
00472197  CMP byte ptr [ESP + 0x13],BL       ; loop top: already composited?
004721a6  JNZ 0x004721bb
004721a8  CMP dword ptr [ESI + 0x29c],0x11   ; this view's layer
004721af  JLE 0x004721bb
004721b1  CALL 0x004c4750                    ; <- COMPOSITOR, at most once/frame
004721b6  MOV byte ptr [ESP + 0x13],0x1      ; bloom_done = 1
004721bb  MOV EAX,[ESI + 0x270]              ; env-map marker test follows
```

Consequences:

- The compositor runs **once per frame at most**, at the top of the first
  iteration whose view has `+0x29c > 0x11`, i.e. *before* that view and every
  later view is rendered. Views with layer ≤ `0x11` are the scene; views from
  that point on draw **on top of the composited image**.
- The environment-map excursion (`0x00472201` `EndScene` → `0x00472210`
  `0x0047e820` → `0x0047223d` `BeginScene`) is evaluated *after* the compositor
  call in the same iteration, so in a frame that renders the env map the bloom
  has already run.
- `0x0047c840` (camera/viewport/clear) runs for every view after the composite
  too, and its clear helper `0x004bb280` sets `D3DCLEAR_TARGET` **only** from
  view flag `+0x270 & 0x10` (`D3DCLEAR_ZBUFFER` from `0x20|0x4` plus
  `VideoD3DFlags & 0x400`, `D3DCLEAR_STENCIL` from `0x800` plus
  `VideoD3DFlags & 0x200`). The overlay views evidently do not set `0x10`, since
  the composite survives to `Present`.
- The 2D overlay `0x004c53d0` (`0x004723d5`, `0x004724a7`) and the on-screen
  text `0x004c5830` (`0x0047253f`) both run after the per-view loop, therefore
  after the compositor, and both draw into whatever RT0 is bound.

**They draw into RT0 directly.** A whole-image sweep for
`IDirect3DDevice9::SetRenderTarget` (displacement `0x94`, load-then-call) finds
exactly **two** sites in the entire executable:

| Site | Owner | Role |
| --- | --- | --- |
| `0x004c4e3b` | `0x004c4750` | the per-pass target selection above |
| `0x004c612b` | `0x004c60a0` | end of the `ID3DXRenderToEnvMap` excursion (restores the target D3DX took) |

The scene, the HUD, the 2D overlays and the text therefore all render into the
implicit back buffer; the compositor is the only thing in the game that ever
rebinds RT0, and it leaves it bound to the back buffer.

## 6. Readbacks of RT0

- **`StretchRect`** (`0x88`): the load-then-call sweep returns 16 sites, but 13
  of them are `ID3DXBaseEffect::SetVector` on the same displacement (12 inside
  the material routine `0x004c0150`, one at `0x004c4c4b`). The genuine device
  `StretchRect` calls are `0x004c4c8c` (the compositor's scene copy) and
  `0x004db916` / `0x004dbb1f` inside the surface-copy helpers `0x004db770` /
  `0x004db9e0`, which blit between two managed texture wrappers and are reached
  from `0x004974c0` / `0x004972d0` / `0x00401bb0` / `0x004ee950`, never from
  `0x00471f50`.
- **`GetRenderTargetData`** (`0x80`): three sites, `0x004d0ce4` and `0x004d14cb`
  in `0x004d0c40`, and `0x004dcfbc` in `0x004dced0`. `GetFrontBufferData`
  (`0x84`): **none**.
  - `0x004dced0` is the generic "lock a surface for CPU access" helper: it
    `GetDesc`s, and for a render target it creates a `D3DPOOL_SYSTEMMEM`
    offscreen surface (`0x90`) and copies into it before `LockRect(…, 0x8810)`.
    Its callers are `0x004dac90` (window/device init), `0x004dc540`, `0x004dc990`,
    `0x004dd2c0`, `0x004de9c0` — all texture/device management, none in the frame
    routine.
  - `0x004d0c40` is reached only from `0x004d14e0`, called only from
    `0x00498370`, which walks a request list at `*0x00606f44` and only touches
    entries whose flag word has bit `2` set. `0x00498370` runs in the main loop
    `0x00403840` (three sites), *outside* the frame routine (`0x00471f50` is
    called once from `0x00403f34`). It is a queued image-export path, not
    per-frame work, and it names its own surface rather than RT0.
- **Nothing reads RT0 between the scene draws and the compositor's
  `StretchRect`.** The only RT0 consumers in the frame are that `StretchRect`
  (step 12) and the `FinalCombine` blend into it (step 17h). There is no
  environment-map read of RT0 — `0x0047e820` renders *into* cube faces through
  `ID3DXRenderToEnvMap` and clears them itself — and no screenshot/thumbnail
  readback on a normal frame.

## 7. Implications for the FP16 redirect and the scene-end hook

1. **Hook validation.** The `CALL 0x004c4750` at `0x004721b1` is glow-independent
   (§1 shows the option test is *inside* the callee) and fires at most once per
   frame, latched by `[ESP+0x13]`. A proxy that wants to confirm it has the right
   boundary should *not* look for the `StretchRect` when glow is off: with the
   bit clear the compositor issues **zero** device calls. The usable runtime
   signature with glow on is the pair
   `GetRenderTarget(0) → SetDepthStencilSurface(NULL) → ColorFill ×3 →
   StretchRect(main → 640×384-parent texture, LINEAR)`, and the first
   `SetRenderTarget(0, …)` the proxy ever observes in a frame.
2. **Stage 1 (compositor untouched) is safe.** The compositor's only input is
   `saved_rt` obtained from `GetRenderTarget(0)`. If the hook writes the
   tonemapped 8-bit image into the game's main surface before returning, the
   `StretchRect` at step 12 copies exactly that, and every later pass, the HUD and
   the text are unaffected. The `g_HighlightThreshold = 1.0` default means glow
   will select only pixels that tonemap to white — the "weaker than vanilla glow"
   effect predicted in `hdr-scene-path.md` §1 is quantitatively explained.
3. **The redirect must be unwound before the hook returns.** `saved_rt` is read
   *inside* `0x004c4750`; if the device still holds the FP16 surface at that
   point, the compositor would copy the FP16 target into an `A8R8G8B8` texture
   via `StretchRect` — which WineD3D may accept or reject, and which would then
   composite into the FP16 surface. The existing "reaching `EndScene` still
   redirected is a bug" rule must be tightened to "reaching `0x004c4750` still
   redirected is a bug".
4. **Replacement admission requires more than the glow preference.** The
   earlier one-line skip proposal and abbreviated pointer expressions are
   superseded by [the compositor replacement contract](bloom-compositor-skip.md).
   Re-read the live preference/capability through its validated pointer chains,
   require a successful replacement on the exact owning device, and preserve
   the engine's continuation state and effect-manager caches. The preference
   alone does not prove that skipping the original compositor is safe. Ordinary
   later materials remain only partly qualified; see the
   [late-view analysis](bloom-late-view-state.md).
5. **State residue if the original is skipped.** Skipping `0x004c4750` also skips
   `SetDepthStencilSurface(NULL)`/restore, the viewport change in `0x004c6300`,
   the vertex declaration and stream binding, and — on a software-vertex-
   processing device — `SetSoftwareVertexProcessing(TRUE)`, which the original
   never restores anyway. A replacement pass must therefore leave the viewport at
   the back-buffer size and the depth surface restored to the value
   `GetDepthStencilSurface` returns, or the post-bloom overlay views will render
   with a viewport the game did not set.
6. **No readback breaks.** §6 establishes that normal frames never call
   `GetRenderTargetData`, `GetFrontBufferData` or `LockRect` on RT0. An FP16
   redirect cannot break a screenshot path that does not run; the queued export
   path `0x00498370` runs in the main loop with the back buffer already
   written back.
7. **Alpha still matters.** `FinalCombine`'s source shader is
   `ff6eed5a5ddf3a3a`; the *downsample* shader `1c90e79667bdaddf` is the one
   `hdr-scene-path.md` §1 identifies as deriving its highlight mask from
   `1 − saturate(alpha)` of the scene copy. Since that copy is a `StretchRect`
   of the game's main surface, the tonemap write-back's `oC0.a = scene.a` rule
   is load-bearing exactly as recorded.
8. **`t_SceneMap` is full-resolution `A8R8G8B8` and re-created only by
   `0x004c4330`.** A resolution change or device reset that goes through that
   function will free and re-create all five globals; a proxy holding references
   to any of them (it should not) would dangle.

## Reproduce

```sh
JAVA_HOME='/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home' \
  '/opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless' \
  /tmp/x3-ghidra-research X3Render -process X3AP.exe -readOnly -noanalysis \
  -scriptPath tools/analysis -postScript X3CameraState.java \
  /tmp/x3-compositor/out.txt \
  dec:004c4750 ins:004c4750 dec:004c4330 dec:004c6300 dec:004b9ed0 \
  dec:004bb0f0 dec:004b6f60 dec:004bb280 \
  data:00608a64 data:00608a68 data:00608a6c data:00608a70 data:00608a74 \
  data:00606f34 data:0056374c \
  range:004cd0c6:110 range:004c36a5:30 range:004c370c:40 range:004721a8:70 \
  disp:0x80 disp:0x84 disp:0x88 disp:0x94 load:0x100 load:0x18c load:0x94 \
  ptr:0054e960:10 ptr:005655c0:2
```

Effect metadata and the option label were read in memory from the installed
archives (`shader/3_0/bloom.fb` in root `01.cat`, XOR `0x33`; `t/0001-L044.pck`
in root `06.cat`, XOR `0x33` then gzip, `<page id="1912">`), using
`tools/analysis/inspect_x3.py`'s `read_catalogue`. Nothing was extracted to
disk. The blend-state table was queried from the existing
`verification/results/game-flight-capture-summary.json`, frame 5449, draws
93–106.
