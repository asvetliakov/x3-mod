# GPU transfer to HDR presentation on Preview

Investigated 2026-09-10 on the installed CrossOver Preview 27.0.0.40921, x86
WineD3D/OpenGL and x86 DXMT/D3D11. **The ordinary D3D9/DXGI shared-HANDLE route
does not share pixels on this installation.** A separate native proof establishes
that OpenGL and Metal can share an FP16 IOSurface on this machine, preserving
values above one. Connecting that primitive to WineD3D remains implementation
work; there is no demonstrated production bridge yet.

This updates the interop gate in [platform.md](platform.md). It does not establish
physical HDR presentation or recover HDR values already clamped by the game.
The investigation changed no bottle setting, installed DLL or game file, and
launched no game. Two original, hidden/no-window standalone probes were used.

## Decisive installed-runtime HANDLE test

The [x86 probe](../../verification/probe/hdr_shared_handles.cpp) loads the actual
Wine D3D9 and DXMT D3D11 in one process. It creates ordinary D3D9, D3D9Ex and
D3D11 devices, then tests 64×64 BGRA8 and RGBA16F render-target textures.

| Operation | BGRA8 and RGBA16F result |
| --- | --- |
| Ordinary D3D9 `CreateTexture(..., &shared_handle)` | `E_NOTIMPL` (`80004001`), no texture/handle |
| D3D9Ex export with initially null handle | `S_OK`, allocated texture, handle remains null |
| DXMT `D3D11_RESOURCE_MISC_SHARED` texture / `GetSharedHandle` | `S_OK`, nonnull handle |
| DXMT `OpenSharedResource` of that handle | `S_OK`; clearing the reopened texture changes the original |
| Wine D3D9Ex import of that DXMT handle | **`S_OK` and echoed handle, but independent image storage** |

The last row is why HRESULT-only capability tests are insufficient. The probe
clears the DXMT alias to `(0.25, 0.5, 0.75, 1)`, reads the original as a positive
same-API sharing control, then clears the WineD3D “import” to green. A D3D9
render-target readback establishes completion and verifies its green image.
A subsequent D3D11 staging copy/map shows the original DXMT texture unchanged:

| Format | DXMT original before / after Wine clear | Wine texture after its clear |
| --- | --- | --- |
| BGRA8 bytes | `bf8040ff` / `bf8040ff` | `00ff00ff` |
| RGBA16F bytes | `00340038003a003c` / `00340038003a003c` | `0000003c0000003c` |

Both controls report `dxmt_unchanged=1 wine9_differs=1`. CPU readback is used
only to validate the experiment; it is not proposed for the renderer.

The behavior agrees with Wine's `d3d9_device_CreateTexture`: non-Ex sharing
returns `E_NOTIMPL`; the Ex/default-pool path logs unimplemented sharing and
continues with ordinary texture allocation. The installed D3D9 binary contains
the corresponding diagnostic strings. Upstream source is corroborating evidence,
not a claim that its current revision exactly matches Preview.
[Wine source](https://github.com/wine-mirror/wine/blob/master/dlls/d3d9/device.c#L1443-L1500).

Evidence: [numeric log](../../verification/results/hdr-shared-handles.txt),
[fresh-build command and source/executable hashes](../../verification/results/hdr-shared-handles-summary.json),
[runtime stderr](../../verification/results/hdr-shared-handles-wine.log).
Reproduce with `python3 verification/probe/run_hdr_shared_handles.py`.

## What DXMT sharing actually represents

Installed x86 `winemetal.dll` exports `MTLDevice_newSharedTexture`, shared-event
methods, `WMTBootstrapRegister`/`WMTBootstrapLookUp`, `CreateMetalViewFromHWND`
and Metal-layer color-space/EDR methods. Installed D3D11 strings identify shared
texture import through D3DKMT metadata and Mach-port lookup. The positive DXMT
reopen/pixel control above proves that implementation is functioning locally.

Inspected upstream DXMT commit
`4ddb20e54672c0cb56115ce80d6db1beef94ae28` uses `D3DKMTQueryResourceInfo` and
`D3DKMTOpenResource2` to recover private runtime descriptors and a bootstrap Mach
port name. Winemetal creates/imports `MTLSharedTextureHandle` objects through
Mach ports. This is a specific protocol, not a generic integer that WineD3D
understands. The native implementation also uses private Metal handle selectors;
do not treat its Mach port as an interchangeable public IOSurface handle.
[DXMT import implementation](https://github.com/3Shain/dxmt/blob/4ddb20e54672c0cb56115ce80d6db1beef94ae28/src/d3d11/d3d11_texture_device.cpp#L533-L602),
[Winemetal implementation](https://github.com/3Shain/dxmt/blob/4ddb20e54672c0cb56115ce80d6db1beef94ae28/src/winemetal/unix/winemetal_unix.c#L2666-L2698).

Upstream is design evidence, not a verified ABI match for the installed build.
For example, its native object tokens are 64-bit even in the Windows x86 bridge.
Passing an Objective-C pointer through a 32-bit `void*` would truncate it. A bridge
must use explicit sized tokens and the matching WoW64/unix-call layout.

## Positive host proof: FP16 OpenGL → IOSurface → Metal

The original [host probe](../../verification/platform/iosurface_hdr.mm) creates
an OpenGL context without a window, a 64×64 `'RGhA'` IOSurface with 8-byte pixels,
and a rectangle texture backed by it through `CGLTexImageIOSurface2D`, using
`GL_RGBA16F`/`GL_RGBA`/`GL_HALF_FLOAT`. A GL framebuffer clear writes
`(0.18, 2, 4, 1)`. After `glFinish`, a Metal `RGBA16Float` texture references the
same IOSurface. A Metal compute kernel reads it and writes one validation pixel
to a small buffer.

The retained **x86_64 host executable under Rosetta** reports OpenGL 4.1 on
Apple M5 Pro, complete framebuffer, no GL error, and Metal output
**`(0.180054, 2, 4, 1)`**. Thus the data crosses GL→Metal through GPU image storage
and retains HDR range. Only the final 16-byte verification result is CPU-read.
An initial arm64 run produced the same values; the retained x86_64 run also
checks the architecture used by the installed x86_64 Wine unix modules.

This follows Apple's documented IOSurface texture APIs:
[OpenGL binding guidance](https://developer.apple.com/documentation/professional_video_applications/fximagetile/opengltexture(forcontext:)),
[Metal IOSurface texture creation](https://developer.apple.com/documentation/metal/mtldevice/maketexture(descriptor:iosurface:plane:)),
[Metal texture IOSurface property](https://developer.apple.com/documentation/metal/mtltexture/iosurface).
See [output](../../verification/results/hdr-iosurface.txt) and
[build command, architecture and hashes](../../verification/results/hdr-iosurface-summary.json).

This proof does **not** access a WineD3D resource/context, exercise Winemetal's
x86 ABI, render through a DXGI swapchain or enable a visible EDR layer. It also
uses a CPU completion wait (`glFinish`); GPU-resident data is established,
asynchronous cross-API synchronization and useful full-resolution throughput are
not. No zero-copy or release-performance claim follows from a 64×64 test.

## Concrete implementation route and remaining gates

The useful route to investigate next is a **source-built WineD3D export extension
plus a native IOSurface/Metal presenter**, retaining D3D9 rendering and existing
scene instrumentation. This is a proposed implementation, supported by the host
primitive above; it is not already available through installed D3D9 interfaces.

1. Build against the matching WineD3D/mac driver sources and add an explicit
   renderer-owned FP16 export operation. Resolve/retain the selected resource
   through WineD3D ownership; schedule the operation in the command stream.
   Do not read guessed private struct offsets or call CGL on the application
   submission thread and assume it is WineD3D's GL context.
2. On the GL execution thread, with the correct context/share group current,
   GPU-blit or shader-copy the FP16 scene result to a renderer-owned
   IOSurface-backed rectangle texture. Preserve GL state and WineD3D state-cache
   assumptions. Existing Wine command-stream callbacks are internal mechanisms;
   installed exports inspected here do not provide a public “export this
   IDirect3DTexture9 as CGL/IOSurface” operation.
3. Use a bounded IOSurface ring. Finish GL production before Metal samples a
   slot; wait for Metal completion before reusing it. Start with explicit
   completion waits for correctness, then measure synchronization overhead and
   investigate compatible fences. `glFlush` alone is not a completion proof.
4. A native Metal compositor can consume the proven IOSurface directly and
   render to an FP16, extended-linear, EDR-enabled layer. That route avoids a
   second texture-import protocol. Integrate window ownership, resize, reset,
   focus and teardown explicitly; it must not create a competing uncontrolled
   presentation path in the game window.
5. If the compositor must remain D3D11/DXMT, add an explicit version-matched
   native-resource bridge. A possible design allocates the shared FP16 texture
   through DXMT, obtains its underlying IOSurface in a native extension, then
   binds that storage to the producer GL context. The standard HANDLE test above
   does not supply this extension. Its Metal-object access, IOSurface availability,
   synchronization and WoW64 ABI must each be proved before adoption.

The installed WineD3D export table exposes abstract texture/resource/context
operations, not a GL texture-name/IOSurface export. Matching source availability
is a real prerequisite: CodeWeavers' public source page currently advertises
26.3.0, whereas the tested runtime is Preview 27.0.0.40921. No exact Preview
source/build match was established in this investigation.
[CodeWeavers source downloads](https://www.codeweavers.com/crossover/source).

If a maintainable source-built extension cannot be obtained, the alternative is
a coherent D3D9→D3D11/Metal translation backend so scene and presenter resources
belong to one implementation. That is substantially broader than adding a
compositor and requires new depth/RESZ, shader, lifetime and visual parity tests.
The existing WineD3D proofs cannot simply be carried over as backend guarantees.
Do not replace this gate with CPU frame readback/upload.

Apple also exposes EDR for `NSOpenGLView`; deprecated OpenGL alone does not mean
HDR is mathematically impossible. However, the current WineD3D LDR backbuffer
and window path have not been shown to supply a floating-point EDR drawable.
Changing a view flag cannot recover values clamped before presentation.
[Apple OpenGL EDR property](https://developer.apple.com/documentation/appkit/nsopenglview/wantsextendeddynamicrangeopenglsurface).

Acceptance for the next bridge fixture: an actual WineD3D FP16 draw containing
0.18/1/2/4/8, GPU export on its execution context, numeric Metal consumption,
alternating frame patterns with no stale reuse, reset/resize/release parity,
and measured 1280×768/5120×1440 transfer cost. Then separately validate a visible
EDR ramp on each display and scene-linear content before labeling the game HDR.

## Installed binary identities inspected

Paths are under CrossOver Preview's `Contents/SharedSupport/CrossOver/lib/`.

| File | SHA-256 |
| --- | --- |
| `wine/i386-windows/d3d9.dll` | `58cc36cf74128ae4b6211100430d146c3692808146d8d2075e6c5d846162f8cf` |
| `wine/i386-windows/wined3d.dll` | `f4997bc0465de7e87bac9921bf0274db00ac3b3ba0754fa03f1f33e309a8e863` |
| `dxmt/i386-windows/d3d11.dll` | `99c4e04d6a3024542b9527ae37036237811a0d28018c460b5d96bb08fd4df2b7` |
| `dxmt/i386-windows/winemetal.dll` | `090402c4734d59aec3cf6a67ee840d48d08480d46abb1cf214e0ca07ea4d459c` |
| `dxmt/x86_64-unix/winemetal.so` | `e6eeb6881e533b21b45a9e2c02ffdf29a3236bcb52ef38b1456dc3d686f03b2d` |

Local export/string inspection used MinGW `objdump -p`, `strings`, and `nm`.
DXMT source inspection is pinned above; Wine master source was read as a
corroborating reference. Neither was substituted into CrossOver.
