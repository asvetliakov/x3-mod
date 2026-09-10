# Runtime render-pass evidence

## Animated main-menu frame

Captured from the proxy on 2026-09-10. Frame 500 completed with 690 draws and successful Present. All 46 recorded shader hashes have archive matches. This is a menu-scene capture, not a flying-sector capture.

| Draws | Pixel shader | Candidate effect basenames |
| ---: | --- | --- |
| 235 | `517540ae6d5e5410` | asteroid |
| 226 | `fffdabd910793aba` | xt_standard_lighting, xt_standard_lighting_damage |
| 71 | `d44db87778a43b61` | asteroid |
| 60 | `5f82ecacd39529cd` | xt_standard_lighting |
| 35 | `ca6bfa4a6cca7e2a` | argon |
| 33 | `7c83ed50c9894e44` | standard_lighting |
| 13 | `8759c7838bbc86c2` | argon |
| 6 | `6109cf64c03529dd` | gui2d, nebula, nebula2s |
| 4 | `5e0a10fe752b6140` | argon2s |
| 2 | `03a16e5c63daa6e8` | adeffects, adeffects2s |
| 1 | `cd6d6eb4b3d99443` | planet_haze |
| 1 | `1c90e79667bdaddf` | bloom |
| 1 | `f3172baa8dd19a40` | bloom |
| 1 | `241c3fa33270f58e` | bloom |
| 1 | `ff6eed5a5ddf3a3a` | bloom |

All queried bound color targets in this frame have D3DFMT_A8R8G8B8 (21), no MSAA. The main target is 1280×768; the two bloom intermediates are 640×384. Draws 653–655 downsample/blur through these half-resolution targets; draw 656 composites bloom to the main target. This proves the observed pass outputs use normalized 8-bit color; it does not establish the precision of all allocations or every game scene.

The final recorded draw uses the gui2d/nebula shared shader. Classification must combine shader pairs, depth/blend state, vertex layout, target and order. Do not label every matching draw as HUD. The `game-menu-capture-summary.json` file retains per-draw evidence.

The proxy queries live state at each captured draw, so D3DX state manager/stateblock activity is reflected. Texture names/content and stable geometry IDs remain future capture extensions. Camera matrices must be mapped to shader registers and temporal behavior before adding jitter.

## User-triggered flying-scene captures

The user loaded a flying scene and pressed F8. Frames 5449 and 5652 each contain
122 draw calls and complete with successful Present. Both have the same sequence
of shader pairs; all 47 shaders recorded in that session have archive matches.
See `verification/results/game-flight-capture-summary.json`. Desktop automation
was not able to independently obtain a dependable live flight screenshot, so the
scene identification comes from the user's action plus the changed render trace.

- Main color target: A8R8G8B8, 1280×768, no MSAA.
- Bloom intermediates: two A8R8G8B8 640×384 targets. Bloom executes at draws 95–98.
- Subsequent rendering continues through draw 122; the last draws use the shader
  shared by effects/engine materials, with depth disabled and blending enabled.
  Therefore `after bloom == HUD` is not a valid partition.
- Captured depth descriptors include format 77 (D24X8) at 1280×768 and 256×256.
  An attached depth surface is not yet proof we can sample scene depth; INTZ was
  queried only in the independent capability probe.
- 122 same-index shader pairs match across the two captures; float c24–c27 differs
  at 118 matching draw positions. This demonstrates changing constant data. It
  does not prove object correspondence, matrix convention or motion vectors;
  those registers are not WVP for every shader, and draw index is not stable ID.

This closes the initial flight **capture** gate. Scene/UI separation, temporal
inputs, HDR radiance, visual equivalence and performance acceptance remain open.
