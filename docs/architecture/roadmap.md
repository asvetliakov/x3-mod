# X3 Modern Renderer: staged implementation and acceptance gates

The user-added [modern chase camera](chase-camera.md) is goal 17. The
existing prototype `7f4b251` is integrated, reviewed/fixed, fixture-qualified
and installed at `2e5f1af`; the first user flight confirms activation but
reports subtle framing and trembling. A native position-selection correction
and lower default framing are in progress. It replaces only
the engine's external back view and remains
off by default; gameplay feel, menu behavior, aiming, TAA cuts and cost remain
acceptance items. This integration precedes HDR bloom implementation.

## Scope and ordering

Target **X3AP.exe**, 32-bit, on native Windows/Direct3D and **CrossOver Preview**
(game bottle X3; fixtures default to Steam). Keep executable/assets intact on
disk; validated process-local game hooks are allowed. The [current goal
checklist](../goals.md) supersedes the original staging assumptions below.

As of 2026-09-13, TAA, RCAS sharpen and mip bias have game evidence. AgX is
retained after the fixed-exposure game comparison. FP16 scene redirection is
implemented, but the source lighting is still gamma-space; neither
scene-referred lighting nor HDR display presentation is complete. Native
Windows source compatibility remains distinct from native runtime verification.

Reviews 30–34 are complete; the crypto cache and space-aware exposure meter
are merged, and adjacency/reader fixes are qualified. The combined DLL is
installed with the camera integration at `2e5f1af`; obtain the controlled user runs next.
Sharpen 0.75 / mip bias −0.5 remain
candidate defaults until 0.75 is measured in game. HDR bloom on the FP16 target
is the next visual feature; use the [existing glow/compositor
map](../reverse-engineering/compositor-and-glow.md) and the reviewed
[replacement contract](../reverse-engineering/bloom-compositor-skip.md) to avoid
double bloom or HUD contamination. The latter supersedes the earlier one-line
skip proposal. The [chosen initial boundary](hdr-bloom-boundary.md) runs the
original fully before RGB replacement, avoiding the bypass residue gap while
retaining its GPU cost. Numerical filter work is in progress; integration and
game acceptance remain. A material pass is the prerequisite for real HDR lighting and
emissives. Custom AgX look tuning and temporal upscaling remain candidates.

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

The table records acceptance gates, not a claim that every earlier row is
complete or that the old numerical ordering still governs work. Stage 3 TAA has
the recorded game-capture evidence; this does not assert every original
acceptance subcase has been exercised. Stage 4 has FP16/AgX infrastructure but
lacks scene-referred lighting.
Stages 4–6 overlap: material work precedes acceptance of real HDR lighting.
A final-frame spatial filter is not TAA. A tone curve on an
8-bit backbuffer is not true HDR. Merely switching D3D APIs cannot provide either.

## Modern API decision

The implemented route retains D3D9 through an API-compatible proxy, using
documented D3D9 behavior on both native Windows and CrossOver. There is no
general DX9-to-DX11 translation layer. Use D3D9 SM3/FP16 for scene work.
A future HDR presentation path must resolve GPU resource ownership and
shader translation/replay, rather than assuming D3D9 shared handles bridge to any
D3D11 backend on this machine. See [platform evidence](platform.md).

DLSS cannot be assumed to work on Apple hardware or inferred from a CrossOver
checkbox. Working TAA provides a basis for a temporal-upscaling experiment;
resolution scaling, reconstruction quality and cost still require separate
validation. MetalFX or another backend-specific upscaler is only an optional
capability; it cannot replace required native Windows feature support.

## Borderless menu bar and MSAA

The [window/cursor investigation](window-and-cursor.md) also tracks the separate
macOS arrow plus game cursor after alt-tab; obtain a vanilla A/B first. Neither
that issue nor the menu-bar issue is fixed. Record actual CreateDevice/Reset
parameters. Compare ordinary windowed,
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
