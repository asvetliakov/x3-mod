# D3D11 post chain beside the game's D3D9 device: feasibility

Question: could the proxy's post chain (TAA, fog, bloom, HDR) run on a D3D11
device created beside the game's D3D9 device, with the scene, depth and motion
targets shared across the API boundary, and would a DXGI swapchain there give
HDR output? Motivation: the TAA stage is seven full-screen SM3 passes at the
512-slot limit (resolve 505, masks 427 / 428) and scales with pixels; the user
targets 5120x1440; one D3D11 compute dispatch with group-shared memory would
replace the three mask passes.

Probe: `verification/probe/d3d11_interop_fixture.cpp` (CMake target
`d3d11_interop_fixture`), runner `verification/probe/run_d3d11_interop.py`,
record `verification/results/bottle-X3/d3d11-interop.json` (raw transcript
`d3d11-interop.txt` beside it), host test
`verification/analysis/test_d3d11_interop.py`. Every graphics API is loaded at
run time; a missing one reports `status=unavailable` for its step.

## Measured on the X3 bottle (2026-09-24)

Run: `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3
verification/probe/run_d3d11_interop.py --exe <empty dir>/d3d11_interop_fixture.exe`,
X3 bottle (WineArch arm64, `FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`), CrossOver's
`--dll d3d9=b`, 9.3 s, `RESULT checks=7 failures=0 unavailable=0 PASS` (the recorded run; the
same build ran once before with identical outcomes, timings quoted below as a spread). The runner
refuses a `d3d9.dll` beside the fixture: a first run in a shared build directory
loaded the proxy DLL through the application-directory search (its D3D9 figures
were within 15 % of the clean run's and are not recorded). Figures below are
measured unless marked; the summary one-liner is
`verification/results/bottle-X3/d3d11-interop-summary.py`.

### 1. Provenance and devices

| Module | Loaded image | Version resource | Signature / markers | What it is |
|---|---|---|---|---|
| `d3d9.dll` | 180,224 B | 5.3.1.904, product `Wine 11.15` | Wine builtin, `wined3d` | Wine's wined3d D3D9 (what the proxy forwards to) |
| `d3d11.dll` | 3,170,304 B | 10.0.17763.1, product `DXMT` | Wine builtin signature, `DXMT`, `winemetal` | DXMT (D3D11 on Metal), CrossOver's `CX_GRAPHICS_BACKEND=dxmt` |
| `dxgi.dll` | 1,060,864 B | 10.0.17763.1, product `DXMT` | same | DXMT's DXGI |
| `d3dcompiler_47.dll` | 184,320 B | 6.3.9600.16984, product `Wine 11.15` | Wine builtin, `vkd3d` | vkd3d-shader HLSL compiler; compiled all eight probe shaders (ps_3_0 with `VPOS`, vs/ps_5_0, cs_5_0) |

| Device | Result |
|---|---|
| plain D3D9 (`Direct3DCreate9`, `CreateDevice`, HW VP, as the game) | S_OK; identity spoofed by wined3d as `NVIDIA GeForce 8800 GTX` / `nvd3dum.dll` (vendor 10de, device 0191), ps 3.0, 4 RTs, event queries; `QueryInterface(IDirect3DDevice9Ex)` = E_NOINTERFACE |
| D3D9Ex (`Direct3DCreate9Ex`, `CreateDeviceEx`) | S_OK, same identity |
| D3D11 (`D3D11CreateDevice` on `EnumAdapters1(0)` = `Apple M5 Pro`, vendor 106b, LUID 00000001:00002906, 901 MB "dedicated") | S_OK, feature level **11_1**, `BGRA_SUPPORT` accepted |

The D3D9 identity does not match the DXGI adapter's vendor / device (spoofed vs
real), so "same adapter" pairing on this bottle can only go by adapter index or
LUID, not by the documented `VendorId` / `DeviceId` match.

### 2. Sharing (256x256 gradient, one API renders, the other reads back)

| Direction | Format | Create | Handle | Open on the other API | Pixels |
|---|---|---|---|---|---|
| D3D11 `MISC_SHARED` -> D3D9Ex `CreateTexture(pSharedHandle)` | A8R8G8B8, A16B16G16R16F, A32B32G32R32F | S_OK | `GetSharedHandle` S_OK (DXMT returns 0x40000002...) | **S_OK** on D3D9Ex, but the texture is an unrelated allocation: readback all zeros (65,536 / 65,536 mismatches, first 16 bytes 00), and a D3D9 `Clear` of it is not seen through the D3D11 texture (`reverse_exact=0`) | not shared |
| D3D11 -> D3D9Ex depth (`CreateDepthStencilSurface`) | D24S8 (D24_UNORM_S8_UINT), D32F_LOCKABLE (D32_FLOAT) | S_OK | S_OK | S_OK, no way to verify contents on D3D9; same silent path | not shared (inferred from the colour formats) |
| D3D11 -> plain D3D9 | all five | S_OK | S_OK | **E_NOTIMPL** (80004001) | refused |
| D3D9Ex `CreateTexture(&handle)` -> D3D11 `OpenSharedResource` | all five | S_OK | handle stays **NULL** | E_FAIL (80004005) on NULL | refused |
| plain D3D9 `CreateTexture(&handle)` | all five | **E_NOTIMPL** | - | - | refused |
| `IDXGIKeyedMutex` on a D3D11 `SHARED_KEYEDMUTEX` texture | B8G8R8A8 | S_OK | QI S_OK, `AcquireSync` S_OK | D3D11-internal only; a `MISC_SHARED` texture answers E_NOINTERFACE | n/a |

Cross-API sharing is **unavailable on this bottle in both directions**, and the
D3D11 -> D3D9Ex direction fails *silently*: wined3d accepts the handle, returns
S_OK and hands back a private texture. A production path could not trust
`open_hr`; it would need the pixel probe the fixture does (render a known
pattern, read it back through the other device) before enabling anything.

Synchronisation primitives themselves work and cost, on the D3D11 side,
`Flush` + `D3D11_QUERY_EVENT` spin 214-463 us median after a 256x256 draw
(the D3D11-side wait alone); the full render -> wait -> D3D9 `GetRenderTargetData`
round trip 633 us (FP32) to 4,786 us (A8R8G8B8, the first format: includes
first-use costs) median of 8. D3D9's event query with `D3DGETDATA_FLUSH` works
as in `GpuSyncTiming`. These are the hand-over costs a shared chain would pay
twice per frame even if sharing carried pixels.

### 3. The 17x17 max/min dilation of an 8-bit mask (all outputs bit-exact against the CPU reference)

| Resolution | (a) D3D9, 3 x ps_3_0 passes (event bracket, wall) | (c) D3D11, 3 x ps_5_0 passes (timestamp) | (b) D3D11, 1 x cs_5_0 dispatch, 16x16 groups, 8 KB group-shared (timestamp) |
|---|---|---|---|
| 1920x1080 | 414 us (empty bracket 21 us; 480 us in the earlier run) | 239 us (CPU bracket 247 us; 307 earlier) | **190 us** (CPU bracket 198 us; 241 earlier) |
| 5120x1440 | 1,138 us (empty bracket 24 us; 1,105 earlier) | 718 us (CPU bracket 731 us; 718 earlier) | **657 us** (CPU bracket 666 us; 657 earlier) |

Per iteration, median of 5 brackets of 8 chained iterations each; D3D11
timestamps at 1 GHz, no disjoint. The compute dispatch is 2.0-2.2x / 1.7x the D3D9
chain and 1.26x / 1.09x the D3D11 pixel chain; at 5120x1440 the pixel and
compute paths converge on bandwidth (7.4 Mpx in, 4 B/px out). The D3D9 figure is
a wall-clock bracket (it includes FEX-side submission of three draws); the
D3D11 figures are GPU timestamps, so the D3D9 column is an upper bound.
The 1.7-2.2x is the gain from one dispatch replacing three passes of *this*
17-tap emulation, not of the production mask chain (whose three draws read
up to 8 px away and could not be merged as pixel passes; the mask cost model
is in `docs/verification/temporal-resolve.md`).

### 4. HDR through DXGI

| Query | Result |
|---|---|
| `IDXGIFactory2::CreateSwapChainForHwnd`, hidden window | first attempt accepted: `FLIP_DISCARD`, 2 buffers, `R16G16B16A16_FLOAT` |
| `IDXGISwapChain3::CheckColorSpaceSupport` | `RGB_FULL_G22_NONE_P709` present=1, `RGB_FULL_G10_NONE_P709` present=1, `RGB_FULL_G2084_NONE_P2020` present=1 |
| `SetColorSpace1(G10 P709)` | S_OK |
| `Present(0, 0)` once, windowed, hidden | S_OK; `GetContainingOutput` S_OK; `DXGI_FEATURE_PRESENT_ALLOW_TEARING` = 1 |
| `IDXGIOutput6::GetDesc1` DISPLAY1 | 5120x1440, `RGB_FULL_G2084_NONE_P2020`, 10 bits/colour, 0-400 nits (max full frame 400) |
| `IDXGIOutput6::GetDesc1` DISPLAY2 | 1512x982 (the built-in panel), G2084 P2020, 10 bits, 0-1600 nits |

DXMT reports HDR-capable outputs and accepts scRGB and PQ colour spaces on a
flip-model FP16 swapchain. What it does not show: whether the Metal layer
presents EDR content from that swapchain (one present on a hidden window
proves the API path, not the pixels on the display). The 400 / 1600 nit values
match macOS's EDR headroom for the two displays (inferred).

### 5. D3D11 caps

Feature level 11_1; compute shaders (and `ComputeShaders_Plus_RawAndStructuredBuffers_Via_Shader_4_x`
= 1); group-shared 32 KB, 1024 threads per group, 65,535 groups per dimension
(feature-level constants, not queried); `TypedUAVLoadAdditionalFormats` = 1;
`R16G16B16A16_FLOAT` typed UAV load and store 1, render target 1, display 1;
`R8_UNORM`, `R8G8B8A8_UNORM`, `R32_FLOAT`, `R16G16_FLOAT`, `R11G11B10_FLOAT`,
`R10G10B10A2_UNORM`, `R32G32B32A32_FLOAT` UAV load and store 1; `B8G8R8A8_UNORM`
UAV load 1, store 0; `D24_UNORM_S8_UINT` and `D32_FLOAT` depth-stencil and
shader-sample 1; doubles 0; driver command lists and concurrent creates 1;
`MapNoOverwriteOnDynamicConstantBuffer` 1; shader min precision 16-bit for all
stages (`0x2`).

## What native Windows gives by documentation (inferred, not run)

- Cross-API sharing requires **D3D9Ex** on the D3D9 side. `IDirect3DDevice9::CreateTexture`
  / `CreateRenderTarget` / `CreateDepthStencilSurface` accept `pSharedHandle`
  only on a D3D9Ex device with `D3DPOOL_DEFAULT`; on a plain `IDirect3D9` device a
  non-NULL `pSharedHandle` is `D3DERR_INVALIDCALL` for `D3DPOOL_DEFAULT` (the only
  documented plain-D3D9 use is the `D3DPOOL_SYSTEMMEM` user-memory pointer). The
  D3D11 side opens the handle with `ID3D11Device::OpenSharedResource`, and a
  D3D11 texture created with `D3D11_RESOURCE_MISC_SHARED` yields the handle
  through `IDXGIResource::GetSharedHandle`. Documented shareable formats between
  D3D9Ex and D3D10/11: `A8R8G8B8` / `B8G8R8A8_UNORM`, `A16B16G16R16F` /
  `R16G16B16A16_FLOAT`, `A32B32G32R32F` / `R32G32B32A32_FLOAT`, `A2B10G10R10` /
  `R10G10B10A2_UNORM`, `L8` / `R8_UNORM`, `L16` / `R16_UNORM`; depth-stencil
  surfaces are **not** shareable across the boundary (no D3D9 depth format is in
  the list), so depth would have to be copied into a colour format on the D3D9
  side (`R32F` via a full-screen pass, as the depth-resolve pass already does).
- Synchronisation: D3D9 has no keyed mutex. `IDXGIKeyedMutex` exists only for
  D3D10/11/12 resources created with `D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX`,
  and such resources cannot be opened by D3D9Ex. The documented cross-device
  ordering is therefore: producer `Flush` (D3D9: an `D3DQUERYTYPE_EVENT` issue and
  `GetData(D3DGETDATA_FLUSH)` spin, as the production `GpuSyncTiming` already
  does; D3D11: `Flush` and a `D3D11_QUERY_EVENT` spin), then the consumer's use.
  Without the wait the consumer may read a stale surface; on Windows the two
  devices are separate kernel contexts and only the fence semantics of a real
  wait are guaranteed. Cost: one CPU-visible GPU drain per hand-over, twice per
  frame (scene to D3D11, result back to D3D9 or to a DXGI swapchain).
- HDR output: `IDXGISwapChain3::CheckColorSpaceSupport` and `SetColorSpace1` on a
  flip-model swapchain (`DXGI_SWAP_EFFECT_FLIP_DISCARD`, `R16G16B16A16_FLOAT` for
  scRGB `RGB_FULL_G10_NONE_P709`, `R10G10B10A2_UNORM` for `RGB_FULL_G2084_NONE_P2020`),
  `IDXGIOutput6::GetDesc1` for the display's colour space and luminance; the
  swapchain must be flip model (Windows 10 1703+). A plain D3D9 device has no
  HDR path at all, so HDR output means the D3D11 side owns `Present`.

## Architecture sketch, if sharing were available

1. Device pairing at the game's `CreateDevice` (the proxy already wraps
   `IDirect3D9::CreateDevice` in `src/proxy/capture.cpp`): after the game's
   plain device exists, create the D3D11 device on the adapter matched by
   `VendorId` / `DeviceId` (`IDXGIFactory1::EnumAdapters1`), feature level 11_0
   or higher, `D3D11_CREATE_DEVICE_BGRA_SUPPORT`. Because the game's device is
   plain D3D9, the shared surfaces would have to be created on the **D3D11**
   side and opened on D3D9; that needs a D3D9Ex device on the D3D9 side, which
   the game's `Direct3DCreate9` path does not give. The only documented way to
   get D3D9Ex is to answer the game's `Direct3DCreate9` with a `Direct3DCreate9Ex`
   object (the proxy exports both) and its `CreateDevice` with `CreateDeviceEx`:
   a D3D9Ex device is a superset in interface terms but changes documented
   behaviour the game relies on (no `D3DERR_DEVICELOST` on Reset, managed pool
   refused, `D3DPOOL_DEFAULT` lockable rules), which the ownership layer in
   `src/ownership` (managed-upload observers, surface leases, Reset admission)
   would have to re-qualify.
2. Shared targets: FP16 scene (`A16B16G16R16F`), motion (`A16B16G16R16F` or
   `G16R16F` copied into a shareable 4-channel format), depth resolved into an
   `R32F` colour target on D3D9. Each is a D3D11 texture with `MISC_SHARED`
   opened on D3D9Ex; the D3D9 passes render into them as today.
3. Sync: after the last D3D9 write (`EndScene` of the game's frame, plus the
   proxy's own passes), an event-query drain on D3D9; then the D3D11 chain
   (mask dispatch, resolve, bloom, tone map); then a D3D11 event drain before
   D3D9 touches the outputs again. Where `Present` happens: either D3D11 copies
   the display result into a shared `A8R8G8B8` that the D3D9 `Present` shows
   (no HDR, keeps the game's window and cursor handling), or the D3D11 side
   presents through its own flip-model swapchain on the game window and the
   D3D9 `Present` becomes a no-op (HDR possible; the game's window loses the
   D3D9 swapchain, so alt-tab, `Reset` and the double-cursor tracking change).
4. Fallback: when any step is unavailable (no d3d11, no shared handle, the open
   fails, or the readback is not bit-exact), the existing D3D9 chain runs
   unchanged. The capability boundary is the pairing step; no production code
   path may assume the D3D11 side exists.

## Risks

- **Cross-API sharing on CrossOver**: d3d9 is Wine's wined3d front end while
  d3d11 / dxgi are DXMT (`CX_GRAPHICS_BACKEND=dxmt` in the bottle's
  `cxbottle.conf`; `lib/dxmt/i386-windows/{d3d11,dxgi,winemetal}.dll`). Two
  different backends with no common allocator; measured above: no direction
  carries pixels, and the D3D9Ex open reports success anyway.
- **Plain D3D9 vs D3D9Ex**: the game creates a plain device; sharing is a
  D3D9Ex feature. Substituting D3D9Ex changes Reset and pool semantics under
  the game and the ownership invariants (`src/ownership`, `docs/architecture`).
- **Reset**: shared D3D9Ex resources live in `D3DPOOL_DEFAULT` and must be
  released before `Reset` and recreated after; the D3D11 side must drop its
  opened handles in the same window (`before_reset` / `after_reset` as
  `GpuSyncTiming` does), and a failed Reset leaves both sides released.
- **Alt-tab and the double cursor**: a second swapchain on the game window
  changes focus / occlusion behaviour; the double-cursor check after alt-tab
  must be rerun for any presentation change.
- **Two GPU queues**: each hand-over is a full drain; on a single-queue
  translation layer this serialises the frame twice.
- **Toolchain**: the D3D11 shaders need SM4/5 bytecode; the bottle's
  `d3dcompiler_47` (vkd3d-shader) compiled every probe shader, but a production
  chain would precompile.

## Recommendation

**Not feasible as a shared-target chain on the CrossOver target; do not start
the pairing work.** The measured blocker is structural: the game's D3D9 goes
through wined3d and the bottle's D3D11 through DXMT, two backends with no
common allocation, and neither direction of sharing carries pixels (the
D3D9Ex-side open even reports success on an unrelated texture). On native
Windows the sharing itself is documented, but only for a D3D9Ex device, which
the game's `Direct3DCreate9` / `CreateDevice` path does not produce; the
proxy would have to substitute a D3D9Ex object under the game and re-qualify
the ownership and Reset invariants, and the user cannot test Windows. A
feature that works on neither target as the game creates its device is not a
feature.

What the probe does support:

- **The post chain's pixel cost is not a D3D11 problem to solve.** The
  representative dilation shows a 1.7-2.2x gain of one compute dispatch over
  three D3D9 passes and only 1.1-1.3x over three D3D11 pixel passes at
  5120x1440; the SM3 mask chain's cost is bandwidth and taps, which the
  current split-loop / spare-flag-code work already attacks inside D3D9.
  The 512-slot limit is a real constraint, but the answer within D3D9 is
  more passes or smaller programs, not another API.
- **HDR output is a separate question with a real API path on this bottle**:
  DXMT's DXGI reports PQ and scRGB present support on a flip-model FP16
  swapchain and HDR-capable outputs. Using it would still mean the D3D11
  side owning `Present` on the game window with the final image copied
  across the API boundary, which is exactly the sharing that does not work;
  the only remaining route is a CPU round trip (D3D9 readback of the final
  frame, D3D11 upload, present), whose cost at 5120x1440 (7.4 Mpx x 8 B FP16,
  two copies per frame under FEX) would need its own measurement before it
  is considered. Native Windows D3D9 (plain) has no HDR path at all.
- **If the question is ever reopened**, the gate is this fixture: sharing
  must show `opened=1 exact=1 reverse_exact=1` for `A16B16G16R16F` in at least
  one direction on the target bottle before any pairing code is written, and
  the probe must run again after every CrossOver / DXMT update.
