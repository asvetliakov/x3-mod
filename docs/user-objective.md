I want you to mod X3 Albion prelude game. It's installed in my crossover in "Steam" bottle, in drive\_c/X3 folder. Executable is X3AP.exe.

It's old DX9 game. The mod should modernize it's graphics (the list of goals is below).

I have Ghidra installed for disassembly/decompilation if you need it.



The list of my goals/enhancements:

1. True HDR support
2. Modern tonemapping such as AgX
3. FP16 lighting + HDR emissive
4. HDR bloom
5. Automatic exposure/eye adaptation
6. New material shaders
7. GTAO/SSAO
8. Better directional shadows/self-shadowing
9. Modern reflections/SSR
10. Improved particles/soft particles
11. TAA Antialiasing (or may be even DLSS) - must, the game is very bad at anti-aliasing, borderless window doesn't work with MSAA and everything is shimmering in motion
12. Volumetric nebula/fog lighting
13. Depth-aware lens effects
14. Anything else you think it's worth to have
15. For some reason in borderless mode macos top menu is rendering on top of it
16. Clustered forward lighting rendering

If i understand correctly we need to re-route some parts to DX11 (or DX12/Vulcan?) right ?&#x20;



Keep and document disassembly/architecture findings in separate logically grouped document(s) . Use Ghidra if needed.

For task logically split it into subtasks so we can iterate/test them. For testing/verfiication you can use PC control and launch the game - probably best to use windowed mode for this (?). Or you can ask me to test something.

When creating source code don't forget to document your code. Keep test/verification files separate of source files



Don't forget to use subagents if needed to manage context in main session



\---

This is message when i asked you in separate chat for proposing changes/features:



Yes. All four are technically possible, but I would **not** approach this as “rewrite the X3 engine into DX11” from day one. X3: Albion Prelude is explicitly a DirectX 9 game, with Shader Model 3.0 supported at the higher quality level, and existing mods/fixes demonstrate that intercepting it via `d3d9.dll` works. ([Steam Store](https://store.steampowered.com/app/201310/X3_Albion_Prelude/?utm_source=chatgpt.com))

The best project architecture would be: **intercept DX9 → understand X3's render passes → selectively replace them with our own modern renderer**.

| Feature                 | Possible? |  Difficulty | My recommendation             |
| ----------------------- | --------: | ----------: | ----------------------------- |
| DX9 → DX11 renderer     |         ✅ |        High | Yes, gradually                |
| AgX tonemapping         |         ✅ |  Low–Medium | Definitely                    |
| True HDR output         |         ✅ | Medium–High | Definitely                    |
| Deferred rendering      |         ✅ |   Very High | Prefer **Forward+/clustered** |
| Modern bloom/exposure   |         ✅ |      Medium | Definitely                    |
| PBR-ish materials       |         ✅ |        High | Very worthwhile               |
| SSAO/GTAO               |         ✅ |      Medium | Worthwhile                    |
| Better shadows          |         ✅ |        High | Worthwhile                    |
| TAA / temporal effects  |         ✅ |        High | Later                         |
| Better particles/lights |         ✅ | Medium–High | Excellent for X3              |

### 1. Replace the DX9 renderer with DX11

**Yes, but there are two very different meanings of “replace.”**

A compatibility renderer is comparatively straightforward:

`X3.exe → our d3d9.dll → D3D11 → GPU`

This is absolutely feasible. In fact, dgVoodoo2 already implements D3D9 on top of D3D11/12, demonstrating that the API translation itself is possible. ([GitHub](https://github.com/dege-diosg/dgVoodoo2?utm_source=chatgpt.com))

But merely translating

`DrawIndexedPrimitive()` → `DrawIndexed()`

doesn't suddenly make the graphics modern. You still get X3's old shaders, old lighting, old render targets and old material model.

What I'd rather build is something like:
```text
X3 Engine
   ↓
our d3d9.dll proxy
   ↓
identify render calls
   │
   ├── UI → original rendering
   ├── particles → original/custom rendering
   ├── opaque ships → OUR renderer
   ├── stations → OUR renderer
   ├── planets → custom shader
   └── final composition → OUR HDR pipeline
                               ↓
                            D3D11
                               ↓
                             DXGI
                               ↓
                         HDR display
```

Initially our DLL would simply forward everything to real D3D9 while logging things such as shader creation, shader constants, vertex declarations, render-target changes, textures, blend modes and draw calls.

Then we'd hash the shaders and start identifying:
```text
shader 0x72A4... = ship hull
shader 0xE131... = asteroid
shader 0x83B2... = planet
shader 0x192C... = laser
shader 0x50AF... = HUD
```

That lets us replace graphics progressively rather than reverse-engineering the entire executable first.

And yes, I can help with the executable side too: inspect the binary/disassembly you provide, identify D3D initialization, camera matrices, light structures, render loops and relevant functions, then write hooks around them.

---

## 2. AgX

This is one of the easiest major improvements.

However there's an important distinction between:
```text
X3 LDR output
   ↓
AgX/ReShade
```

and
```text
X3 scene-linear HDR lighting
   ↓
AgX
   ↓
HDR/SDR output
```

The second is what we actually want.

There are already AgX ReShade implementations, but their own documentation points out the limitation: when you're operating on a final LDR backbuffer, you don't really replace the game's original scene-to-display transform; you're grading an already-compressed image. ([GitHub](https://github.com/FireDragon761138/Reshade_shaders/blob/main/AgX_FromACES.fx?utm_source=chatgpt.com))

For our renderer I would instead create something like:
```cpp
Scene lighting
    ↓
RGBA16F linear framebuffer
    ↓
auto exposure
    ↓
bloom
    ↓
AgX-style display transform
    ↓
HDR mapping
    ↓
DXGI Present()
```

That means stars, suns, engine exhausts, lasers and explosions can have values such as:
```text
nebula             0.05
ship surface       0.4
bright hull         1.5
engine glow        10
laser              30
explosion          80
sun               200+
```

instead of everything being smashed into `0.0–1.0` before post-processing.

For a space game this could make an enormous visual difference.

---

## 3. True HDR

**Yes — this is one of the things I'd specifically target.**

But upgrading the swapchain alone isn't enough.

If X3 currently renders:
```text
lighting
 ↓
8-bit RGB
 ↓
values > 1 clipped
 ↓
backbuffer
```

then changing the monitor output to HDR doesn't restore that lost information.

We need to get control **before clipping**.

Ideally:
```text
geometry
 ↓
lighting
 ↓
RGBA16F scene buffer
 ↓
HDR bloom
 ↓
exposure
 ↓
AgX / display transform
 ↓
HDR output
```

For Windows/DX11, our final compositor can use DXGI and output either an FP16/scRGB path or a 10-bit HDR10/PQ path depending on what proves most reliable.

We would also render the UI separately. That's particularly important because existing X3 ReShade users report that post-processing currently hits the menus/icons along with the 3D scene. ([Nexus Mods](https://www.nexusmods.com/x3terranconflict/mods/109/?tab=description\&utm_source=chatgpt.com))

So instead of making HUD text glow at 1,000 nits:
```text
HDR universe
    ↓
tone/display mapping
    ↓
SDR-like UI composited at controlled luminance
    ↓
display
```

That is proper HDR rather than an “HDR effect.”

---

## 4. Forward → deferred

Possible, but **I would probably not use classic deferred rendering.**

Instead I'd build **clustered Forward+**.

Classic deferred would mean generating something like:
```text
GBuffer
 ├─ Albedo
 ├─ Normal
 ├─ Roughness
 ├─ Metallic/specular
 ├─ Emissive
 └─ Depth
       ↓
    lighting
       ↓
    composition
```

We could intercept X3's opaque geometry draws and generate this ourselves.

The problem is that we'd have to correctly identify practically every material, reconstruct all material parameters, separate transparent geometry, recover X3's light information and ensure effects such as shields, lasers, particles, cockpit glass and engine glows use an appropriate alternate path.

That is basically a renderer rewrite.

### Forward+ is more attractive for X3

Imagine:
```text
depth prepass
      ↓
screen divided into tiles/clusters
      ↓
compute shader:
determine lights affecting every cluster
      ↓
ship pixel shader
loops only through nearby lights
```

Then X3 could potentially have:
```text
Sun
+
station lights
+
50 lasers
+
20 engine lights
+
explosions
+
missiles
```

without every object evaluating every light.

It also handles transparencies much more naturally.

So rather than:

> replace forward renderer with deferred

I'd target:

> **replace X3's old limited forward lighting with D3D11 clustered Forward+ lighting.**

That's considerably more modern.

---

# What I would add beyond your four ideas

For X3 specifically, I'd prioritize:

- **FP16 scene-linear lighting + HDR emissives.** Engines, weapon fire, stars, gates and explosions benefit massively.
- **Physically improved bloom.** Multi-resolution bloom generated from actual HDR luminance rather than simply blurring bright-looking pixels.
- **Automatic exposure/eye adaptation**, with separate exposure behaviour for dark space, bright nebulas and suns.
- **New material shaders.** Keep X3's textures initially but introduce better normal/specular response, roughness, Fresnel and eventually PBR-style materials.
- **GTAO/SSAO and contact shadows.** Subtle, but very helpful for huge station structures and capital ships.
- **Better directional shadows/self-shadowing** on ships and stations.
- **Modern reflections.** Better environment maps first; potentially SSR where appropriate later.
- **Improved particles.** Soft particles, HDR weapon effects, better engine exhaust, sparks and explosions.
- **Better anti-aliasing.** SMAA initially; eventually TAA if we can obtain stable motion vectors and jitter the projection matrix.
- **Higher-quality anisotropic filtering, LOD control and draw distances.**
- **Volumetric nebula/fog lighting**, especially if we can recover the sector/sun parameters.
- **Depth-aware lens effects.** Bloom/flare around suns without destroying cockpit/HUD visibility.
- **Color-managed UI**, so HDR doesn't make icons and text painfully bright.
- **High-resolution texture/material overhaul** while retaining X3's original geometry where it still looks good.

X3 already has community graphical packs replacing textures, nebulae, glow sprites and other assets, so there's quite a lot we can improve without touching engine code as well. ([Nexus Mods](https://www.nexusmods.com/x3terranconflict/mods/107?tab=description\&utm_source=chatgpt.com))

## The project I'd actually build

I'd call the first stage something like **X3 Modern Renderer**.

The progression would be:
```text
Stage 1
d3d9.dll proxy
+
logging/debug overlay
+
shader/texture/draw-call identification

             ↓

Stage 2
custom fullscreen compositor
+
AgX
+
better bloom
+
exposure

             ↓

Stage 3
D3D11/DXGI presentation
+
FP16 scene pipeline
+
true HDR output
+
separate HUD composition

             ↓

Stage 4
replace X3 material shaders
+
HDR emissives
+
improved normals/specular/roughness

             ↓

Stage 5
recover engine lights/camera
+
Forward+ clustered lighting
+
hundreds of dynamic lights

             ↓

Stage 6
shadows
GTAO
volumetrics
temporal AA
modern particles
etc.
```

**Stages 1–3 are very realistic.** Stage 4 is substantial but also realistic. Stage 5 is where serious reverse engineering begins. Stage 6 starts becoming a genuine partial engine replacement.

And importantly, I **wouldn't start by disassembling all of&#x20;****`X3AP.exe`**. The D3D9 interface exposes an enormous amount of information about what the engine is doing already. We can use that to map the renderer, and then disassemble only the functions we need — camera setup, light submission, scene rendering and material setup. That should make the project dramatically more manageable.