# X3 Modern Renderer: staged implementation and acceptance gates

## Scope and ordering

Target the user's installed **X3AP.exe**, 32-bit Windows under **CrossOver Preview**,
Steam bottle. Keep executable/assets intact initially. TAA is a required destination,
not an optional last polish. The first release is instrumentation, not a claim that
HDR/TAA already works. Later stages require evidence from actual game captures.

| Iteration | Deliverable | Acceptance gate |
|---|---|---|
| 0 — platform | Fingerprint executable, graphics backend, FP16/depth/HDR capabilities | Reproducible baseline and independent capability probes |
| 1 — capture | Reversible D3D9 proxy; shader hashes; draw/state/target/constant captures | Game menus and flying scene render; device reset, COM identity, stateblocks pass; HUD/scene candidates identified |
| 2 — scene boundary | Identify original bloom, opaque scene, transparency and final HUD; camera constant layout | Hashes mapped to effects; capture validates composition order and camera conventions; no HUD contamination |
| 3 — temporal prototype | Depth extraction, camera jitter, reprojection, history rejection and TAA | Stable geometry in motion, controlled ghosting, history reset on jump/menu/resize, object motion accounted for; HUD sharp |
| 4 — HDR scene | Preserve linear radiance in FP16 before clipping; HDR emissives, bloom, exposure and AgX-derived SDR rendering | Numeric >1 scene tests, exposure adaptation, no double gamma/tonemapping; UI composited separately |
| 5 — HDR presentation | Select and validate GPU-native transfer/replay/backend route to D3D11 or Metal presentation | No per-frame CPU readback; verified EDR/scRGB above SDR white on user's display; SDR fallback |
| 6 — materials/lights | Improved material response; recovered world lights; clustered forward rendering | Document material mapping; cluster list correctness; transparent lighting; GPU timing and overflow handling |
| 7 — depth effects | GTAO/SSAO, contact/directional shadows, soft particles, depth-aware lens effects | Stable depth interpretation and temporal filtering; correct occlusion and HUD separation |
| 8 — space rendering | Improved environment reflections, SSR, volumetric nebula/fog, particle lighting | Off-screen SSR fallback, disocclusion handling, quality/performance toggles |

Stages 3–5 may reorder around evidence, but TAA inputs are investigated during
scene-boundary work. A final-frame spatial filter is not TAA. A tone curve on an
8-bit backbuffer is not true HDR. Merely switching D3D APIs cannot provide either.

## Modern API decision

Do not write a general DX9-to-DX11 translator as the opening task. First retain
WineD3D through an API-compatible proxy. Use D3D9 SM3/FP16 where it lets us validate
scene semantics. A later D3D11/Metal path must resolve GPU resource ownership and
shader translation/replay, rather than assuming D3D9 shared handles bridge to any
D3D11 backend on this machine. See [platform evidence](platform.md).

DLSS cannot be assumed to work on Apple hardware or inferred from a CrossOver
checkbox. Native TAA is the baseline temporal requirement. MetalFX or another
supported temporal upscaler can be evaluated once jitter/depth/motion/exposure
inputs exist and backend support is verified.

## Borderless menu bar and MSAA

Record actual CreateDevice/Reset parameters first. Compare ordinary windowed,
exclusive fullscreen and a controlled borderless window. Treat macOS menu bar
presentation as a host window issue, with per-game fullscreen/window handling;
avoid global Dock/menu preference changes. Query windowed multisample support and
check X3's render-target sample matching. Investigate the engine's restriction
before attributing it to D3D9 itself.

## Verification approach

Keep production source in `src/`; scripts in `tools/`; probes and tests in
`verification/`; architecture/reverse-engineering findings in separate `docs/`
groups. Runtime dumps and copyrighted shader bytecode stay local and untracked.
Each visual iteration needs the same scene, controlled camera motion, still and
motion comparison, frame timings, and resize/alt-tab/menu/save-load checks.
Expose features individually so failures can be bisected and rollback is simple.
