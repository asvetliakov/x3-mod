# CrossOver/macOS platform architecture

Inspected 2026-09-10. This document records host/bottle observations and platform decisions, independently of executable disassembly and render-pass identification. Inspection was read-only: no game launch, registry edit, graphics-backend switch, or system preference change was performed for these findings.

## Recommended route and confidence

Start with a **32-bit D3D9 interception/measurement layer forwarding to the existing runtime**. Retain a reversible baseline. The most promising eventual modern API on this installation is **32-bit D3D11 through DXMT to Metal**, with an FP16/scRGB compositor. That is a candidate architecture, not a verified working X3 renderer. It needs an independent 32-bit presentation test and a resource-interoperability test before it becomes a dependency.

The DXMT bottle switch does not convert an application's D3D9 calls to D3D11. DXMT describes itself as D3D10/11, and CodeWeavers separately documents its available backends. Current D3D9 calls still need a D3D9 implementation. [DXMT README](https://raw.githubusercontent.com/3Shain/dxmt/main/README.md), [CrossOver 26 settings](https://support.codeweavers.com/miscellanous/advanced-settings-in-crossover-mac-26).

Do not equate API translation with scene modernization. FP16 lighting requires interception before clamping, temporal AA requires temporal scene inputs, and clustered lighting requires light and material data. None is supplied merely by changing the API used for presentation.

## Local evidence

| Item | Observed value | Consequence |
| --- | --- | --- |
| Host | Apple M5 Pro, 20 GPU cores, 24 GB unified memory; Metal 4 reported | Native Metal is available; translated feature support still needs runtime queries. |
| OS | macOS 26.6.2, build 25G83 | Record alongside every runtime result. |
| CrossOver | `/Applications/CrossOver Preview.app`; bottle version `27.0.0.40921` | This is a Preview installation, not the default `CrossOver.app` path. |
| Bottle | `Steam`, `WineArch=win64`, template `win10_64` | Bottle bitness does not change executable bitness. |
| Executable | `drive_c/X3/X3AP.exe` is PE32 Intel 80386 | In-process Windows proxy, dependencies, and test harness must be x86. |
| Selected graphics backend | `CX_GRAPHICS_BACKEND=dxmt` | Relevant to a future D3D11 path; insufficient to identify actual D3D9 runtime. |
| Other graphics environment | `D3DM_ENABLE_METALFX=1`, `DXMT_ENABLE_NVEXT=1`; `WINEMSYNC=1` | Existing settings; do not treat them as evidence that X3 is using temporal MetalFX. |
| Registry | Global `Software\\Wine\\DllOverrides` empty; no X3-specific override found | A local native proxy may require an application/process-specific load override. |
| Mac driver registry | `AllowSetGamma=0` | Existing gamma-ramp policy is separate from EDR output. |
| App-local graphics DLLs | No `d3d9.dll`, `d3d11.dll`, `dxgi.dll`, `dxvk.conf`, or `dgVoodoo.conf` found under X3 during inspection | No such wrapper was present at this baseline. |
| Installed DXMT | `lib/dxmt/i386-windows/{d3d11,dxgi,winemetal,d3d10core}.dll` | A packaged x86 D3D11 implementation exists. `file` confirms x86 PE32 `d3d11.dll`. |
| Installed DXVK | `lib/dxvk/i386-windows/d3d9.dll` and D3D10/11 DLLs | Available alternative for a controlled compatibility comparison, not currently proven loaded. |
| Installed D3DMetal | `lib/apple_gptk*/wine/x86_64-windows/` graphics DLLs; no i386 counterpart found | Installed GPTK is not a direct drop-in x86 D3D11 dependency for X3. |
| Installed DXMT NGX | `nvngx.dll`/`nvapi64.dll` found in x86_64/aarch64 DXMT directories, not i386 | No packaged x86 NGX route was established. |

The bottle's `windows/syswow64/d3d9.dll` matches the installed Wine i386 D3D9 DLL byte-for-byte (SHA-256 `58cc36cf74128ae4b6211100430d146c3692808146d8d2075e6c5d846162f8cf`). The installed DXVK i386 D3D9 hash differs. This strongly supports Wine D3D9 as the baseline file, **but a running process module/load trace is still required** to establish the actual selected implementation and its backend. Native-load override behavior and wrapper self-loading must also be verified rather than inferred from filenames.

Evidence inputs: the bottle's `cxbottle.conf` and selected graphics sections of `user.reg`; installed CrossOver directory inventory; `file`, `shasum -a 256`, `sw_vers`, `system_profiler SPHardwareDataType SPDisplaysDataType`; and the AppKit probe below. Hardware serial/UUID identifiers are intentionally not retained here.

## Display and actual HDR output

| Display | Geometry | AppKit current/potential EDR headroom at inspection |
| --- | --- | --- |
| Odyssey G95SD, main | 5120×1440 at 120 Hz; scale 1 | 1.0 / 4.0 |
| Built-in Liquid Retina XDR | 3024×1964 physical; 1512×982 logical; scale 2 | 1.0 / 16.0 |

Both returned reference EDR headroom `0.0`. These are live API values, not calibrated peak-nit measurements. Potential headroom above one is evidence that macOS exposes EDR potential. Current headroom equal to one while inspecting the desktop is **not** proof of a permanently SDR display: observe it again while the test owns an active EDR layer. Do not assume the external display's cable, HDR switch, brightness, or window position is correct solely from its model name.

Apple's EDR path uses an extended-range layer, an appropriate extended linear color space, and a floating-point format; it is not established by an FP16 offscreen texture alone. [Apple HDR/EDR rendering session](https://developer.apple.com/videos/play/wwdc2021/10161/), [CAMetalLayer EDR opt-in](https://developer.apple.com/documentation/quartzcore/cametallayer/wantsextendeddynamicrangecontent).

DXMT upstream merged color-space and HDR support in June 2025. That work includes scRGB, FP16 presentation, EDR integration, and conversion of scRGB unit white to 80 nits. It establishes a plausible implementation route; it does not prove this packaged build or our future swapchain is working. [DXMT HDR implementation PR](https://github.com/3Shain/dxmt/pull/70).

A future presentation probe should create an x86 D3D11 device, record feature level and module paths, create `R16G16B16A16_FLOAT`, query `IDXGISwapChain3::CheckColorSpaceSupport`, and select `DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709` when supported. These are the standard FP16/linear HDR pairing. Record every HRESULT and the output descriptor. [Microsoft HDR rendering guide](https://github.com/microsoft/DirectXTK/wiki/Using-HDR-rendering).

Verify a ramp containing 0.18, 1, 2, 4 and higher scene/display values, moving the window between displays and changing SDR/HDR output modes. Preserve distinguishable highlight steps and controlled UI white. An ordinary SDR screenshot alone is insufficient proof of HDR luminance. Record EDR/API evidence and obtain visible comparison on the actual display; instrumented luminance measurement is the stronger acceptance method if available.

AgX and HDR output should have separate implementations/validation. A conventional SDR AgX transform compressing to 0–1 cannot simply be followed by arbitrary brightening and labeled scene-linear HDR. Maintain an explicit scene-linear buffer, exposure, bloom, display mapping, and separately controlled HUD luminance. An initial final-backbuffer color grade must be labeled LDR processing until pre-clamp scene data is proven.

## API choice and interoperability gate

| Route | Assessment | Required proof |
| --- | --- | --- |
| D3D9 proxy → existing D3D9 runtime | Best first milestone; low intervention | Correct exports/COM forwarding, no recursive loading, baseline image and device-reset parity. |
| D3D9 hooks with D3D9 FP16 effects | Useful early experiments | Runtime render-target, blend, filter, depth and sample-format support. Does not establish HDR presentation. |
| D3D9 → D3D11 translation → DXMT | Preferred modern candidate | x86 device/presentation test; shader/resource translation and reset parity; retained interception visibility. |
| Existing Wine D3D9 plus separate D3D11 compositor | Conditional | Demonstrate GPU resource sharing between these actual implementations, including format, ownership and synchronization. |
| D3D9 → DXVK → MoltenVK | Alternative compatibility path | Test supported features, presentation, performance and resource access on packaged macOS build. No assumption of upstream Linux parity. |
| Native Metal renderer/bridge | Technically possible, larger platform-specific work | Wine/native ABI bridge, texture interop, native window ownership, lifetime/synchronization and x86 compatibility. |
| D3D12/Vulkan replacement from scratch | Defer | Adds translation, synchronization and platform risks without supplying missing scene semantics. |

Do not assume Windows D3D9Ex/DXGI shared-resource examples imply interoperable textures between WineD3D and DXMT. The game may use ordinary D3D9, and the implementations can use different native APIs. Copying a full frame through CPU memory is a diagnostic fallback, not a robust main renderer design. At 5120×1440, one RGBA16F texture is 56.25 MiB; transferring one full such image at 120 Hz is about 7.08 GB/s before extra copies and synchronization. Three FP16 color/history surfaces plus one 32-bit depth and one RG16F motion surface already total 225 MiB, excluding all other resources. Budget both unified GPU allocations and x86 process address space.

dgVoodoo2 demonstrates a maintained legacy-API implementation on D3D11/12, so evaluating a mature translator is preferable to assuming D3D9 translation is a small draw-call mapping exercise. Its availability does not prove X3/CrossOver compatibility, HDR semantics, resource extraction, or replacement-shader support. [dgVoodoo2 project](https://github.com/dege-diosg/dgVoodoo2).

## Temporal AA is a first-class investigation

The user's TAA requirement should influence capture from the first milestone: shader bytecode/constants, transforms, render-target/depth lifetimes, viewport, draw identity, and UI boundary. A camera-only history reprojection experiment can help identify inputs, but is not the finished solution for moving ships, lasers, particles, disocclusion or camera cuts.

Correct temporal AA needs jittered scene sampling, trustworthy depth, current/previous transforms or motion vectors, history rejection/clamping, exposure handling, and an unjittered HUD. Apple MetalFX explicitly consumes color, depth and motion, and discusses jitter for quality; AMD's temporal integration guide likewise emphasizes motion-vector requirements. [Apple temporal upscaling](https://developer.apple.com/videos/play/wwdc2022/10103/), [AMD FSR2 integration](https://gpuopen.com/manuals/fidelityfx_sdk2/techniques/super-resolution-temporal/).

CrossOver 26's “DLSS” option is powered by MetalFX and requires the game to enable/use DLSS. Existing bottle environment settings cannot manufacture that integration in X3. NVIDIA's own NGX DLSS requires an RTX GPU; that is distinct from a compatibility implementation backed by Apple MetalFX. Prefer custom temporal resolve or a specifically verified MetalFX bridge after input capture, rather than making an unproven x86 NGX layer mandatory. [CodeWeavers DLSS option](https://support.codeweavers.com/miscellanous/advanced-settings-in-crossover-mac-26), [NVIDIA programming guide](https://raw.githubusercontent.com/NVIDIA/DLSS/main/doc/DLSS_Programming_Guide_Release.pdf).

Windowed MSAA is not categorically prohibited by D3D9: the capability query explicitly accepts a windowed flag. Investigate X3's parameters and actual runtime support for matching color/depth formats, sample count, quality, and swap effect before deciding the failure is architectural. MSAA remains a useful baseline experiment; it does not satisfy the requested temporal stability by itself. [Microsoft multisample query](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3d9-checkdevicemultisampletype).

## Clustered lighting and other effects

Clustered forward rendering groups samples in 3D and assigns lights to clusters, reducing excess light evaluation around depth discontinuities compared with purely 2D tiling. This is a sensible eventual design for separated space objects and transparency, but requires real scene light inputs and replacement lighting shaders. [Original clustered shading paper](https://www.cse.chalmers.se/~uffe/clustered_shading_preprint.pdf).

Proposed progression: recover camera and object transforms; identify light constants and material texture slots; establish one correctly rendered ship in FP16; introduce a bounded CPU-built cluster/light list as a reference; move list building to compute after correctness and profiling. Compute dispatch is not the central unknown. Light semantics, draw classification, resource lifetime and the preservation of transparent effects are.

AO/soft particles/lens occlusion require usable scene depth; SSR additionally requires view-space reconstruction and material/normal information and remains limited to visible screen data. Directional shadows need sun/camera transforms and geometry replay or engine shadow-pass access. Volumetric fog needs sector/fog/sun parameters and a depth-aware composition order. Keep these behind independent feature flags and pass-specific verification rather than enabling broad heuristic shader replacement.

## macOS menu bar issue

Working hypothesis: X3's borderless window is not recognized as native fullscreen presentation, or its rectangle/style/focus transition leaves normal macOS presentation active. This has not been reproduced in this inspection. Log Win32 window styles, outer/client/monitor rectangles, presentation parameters and focus transitions; compare ordinary windowed, borderless and fullscreen modes.

A borderless Windows popup is not evidence of AppKit fullscreen mode. Apple's presentation API controls menu-bar/Dock auto hiding, with constraints on valid option combinations. A later native bridge could change presentation only while the game is focused and restore prior options on focus loss/exit; it must operate in the owning application's context. First test Wine's normal fullscreen behavior. Do not silently change the user's global menu-bar preference to disguise a window-management bug. [Apple presentation options](https://developer.apple.com/documentation/appkit/nsapplication/presentationoptions-swift.struct).

## Next verification milestones

1. Baseline: module trace and D3D9 capture in a small window, then a loaded sector; confirm capture stability and reset/resize behavior.
2. Capability probe: x86 D3D9 FP16/depth/MSAA matrix and independent x86 D3D11/DXMT HDR presentation; keep output under `verification/`.
3. Render semantics: classify scene, particles and HUD; identify camera constants and depth; show debug visualizations and repeat across scenes.
4. Early AA: test windowed MSAA accurately; then jitter and temporal input validation, including moving ships and unjittered HUD.
5. HDR integration gate: prove GPU interop or adopt a coherent translation backend; only then commit to the compositor architecture.
6. Linear lighting/HDR bloom/exposure and material replacement; clustered light reference and compute optimization; depth-dependent effects individually.

Read-only EDR probe and its captured values are in `verification/platform/`; no hardware identifiers are included.

## Runtime verification update (same session)

The independent 32-bit [probe](../../verification/probe/README.md) now successfully
creates D3D9 FP16 render targets and a D3D11 FP16/scRGB flip-discard swapchain.
Every associated HRESULT, including SetColorSpace1 and Present, is successful.
See `verification/results/graphics-capabilities.txt`. D3D9 loads `wined3d.dll` and
reports the emulated 8800 GTX adapter. X3's proxy logs resolve the backend to the
Windows system D3D9 path and report the same adapter.

The probe also reports two Output6 HDR descriptors (10-bit PQ, metadata maxima
400/1600 nits). This updates the API feasibility gate; actual HDR image output and
D3D9/D3D11 resource sharing remain unverified. The module names and Metal cache log
alone do not distinguish DXMT from other host implementations. Do not label the
observed D3D11 run "D3DMetal" solely from that log: that backend identity still
needs host module identification. The bottle remains configured for DXMT.
