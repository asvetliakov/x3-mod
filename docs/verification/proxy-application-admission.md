# Proxy application admission

The capture and loading hooks now enter the process-wide `ApplicationAdmissionAbi` before taking their capture mutex or starting timing/cache work. `process_admission_monitor()` selects the single monitor through the process-latched, off-by-default `X3M_ADMISSION=1` option. A disabled monitor does not construct an admission core scope. This checkpoint observes application-call boundaries; `motion_live_replay_available` remains false and no replay promotion is added.

## Entry inventory

All 30 capture WINAPI bodies are covered:

| Group | Entrypoints | Count |
| --- | --- | ---: |
| Factory | Release, CreateDevice | 2 |
| Device lifecycle | Release, Reset, Present | 3 |
| Draws | DrawPrimitive, DrawIndexedPrimitive, DrawPrimitiveUP, DrawIndexedPrimitiveUP | 4 |
| Scene/depth boundaries | Clear, SetRenderTarget, SetDepthStencilSurface, StretchRect | 4 |
| Other GPU writes | UpdateSurface, UpdateTexture, ColorFill | 3 |
| Patch draws | DrawRectPatch, DrawTriPatch | 2 |
| Resource creation | Texture, VolumeTexture, CubeTexture, VertexBuffer, IndexBuffer, RenderTarget, DepthStencilSurface | 7 |
| Cursor | SetCursorProperties, SetCursorPosition, ShowCursor | 3 |
| Shaders | CreateVertexShader, CreatePixelShader | 2 |

The existing hook-installation conditions remain: some capture slots are installed only for telemetry or scene-depth capture. Scoping all defined roots does not turn otherwise uninstalled native slots into intercepted calls.

All 16 installed loading IAT roots are covered: `D3DXCreateEffect`, `D3DXCreateTextureFromFileInMemoryEx`, `D3DXCreateCubeTextureFromFileInMemoryEx`, `D3DXLoadSurfaceFromFileInMemory`, `D3DXCreateMesh`, `D3DXCleanMesh`; `CreateFileA`, `ReadFile`, `SetFilePointer`; `SetCursor`, `SetCursorPos`; `gzopen`, `gzread`, `gzseek`, `inflate`, `xmlReadMemory`. The three mesh callback templates—ConvertPointRepsToAdjacency, GenerateAdjacency, OptimizeInplace—are covered for all eight bounded table records (24 possible installed slots). The fixture-only recursive native-call shim is inside its owning adjacency scope; it is not an installed application root.

Admission remains active through native returns, output-dependent observer updates, cache accounting and capture-lock cleanup. It does not wrap logging, initialization, report helpers or arbitrary internal utility calls as new application roots. Nested intercepted calls use the same monitor; there is no TLS exemption. Existing mesh-cache cleanup-fault return policy is unchanged, and no admission refusal is translated into an invented application HRESULT.

## CPU state and cleanup order

Each hook declares `CpuCallBoundary` before its admission adapter and other local guards. Immediately before its original call (or the cache's native/hit dispatch), the incoming x87 state, MXCSR and LastError are restored. Immediately afterward, the new outgoing state is captured. Destructors then finish timing, release capture locks and end admission before the outer boundary restores the native outgoing state. The old cursor-only LastError guard and conditional mesh-create/clean computational guards are superseded by this consistent boundary. Float arguments and every native argument/output slot remain unchanged.

The admission adapter independently preserves state around each bookkeeping operation. Its translation unit alone is compiled without exceptions; capture/loading retain their existing exception flags. Source ordering and a clean i686 compile are not a whole-hook emitted-prologue/epilogue CPU certificate. The final actual-DLL integration retained 24 native FP/LastError witnesses on this runtime. The existing CPU helper calls GetLastError before saving FP state and SetLastError after restoring it; these runtime witnesses do not establish preservation independently of those helpers on every backend. Cross-boundary native/C++ exceptions are outside the ordinary-return preservation claim. Volatile XMM registers follow the existing calling ABI.

The complete CPU boundary also runs when admission is disabled; disabled admission itself performs no monitor/TLS bookkeeping. This change does not claim zero added hook cost. Existing timing spans include portions of the new boundary work, and isolated adapter timing is not complete hook or game FPS evidence.

On final factory or device Release, the native call and capture-map cleanup complete under `HookGuard`; then the lock is released, that capture admission explicitly finishes, and a phase-tagged final snapshot is logged. During child-final teardown, the nested factory snapshot may observe one active outer device root; the subsequent outer device snapshot observes zero in the serial fixture. A legitimate concurrent caller can still be active, so this log never asserts universal process quiescence.

## Batched diagnostics and limits

The existing captured/300-frame Present cadence emits `admission_metric` only when requested, with cumulative admitted-root/promotions counters, current active/waiting/replay gauges, veto bits and first reason. `live_replay_enabled=0 coverage_complete=0` are explicit. `application_admission_final` reports the same monitor after final factory/device cleanup, including `phase=factory|device` and `enabled`. The loader owns `application_admission_mode` startup logging. There are no per-draw admission logs.

Coverage is limited to installed proxy/loading roots plus separately integrated ownership/loader boundaries. Main-IAT interception does not cover unrelated DLL imports, direct native exports, unobserved D3DX objects/methods or completed raw-native mutations. It does not extend a synchronous file/codec scope through asynchronous completion. Unknown callbacks, foreign object routing and native escapes still require their own veto/coverage policy. A few passing API calls cannot establish that every replay segment is callback-free. Accordingly, this checkpoint does not promote replay, enable motion output, or establish complete replay-versus-mutation exclusion.

## Verification status

Both changed production translation units compile with i686 SSE2/four-byte-stack settings, `-Wall -Wextra -Werror`, and unchanged exception flags. The accepted fresh actual-DLL integration passes 26 cases plus forced fallback; its [artifact audit](../../verification/results/admission-integration-artifact-audit.json) binds the 23-object DLL, 20-object fallback and 24 CPU witnesses. The final DLL SHA256 is `5a5f8a78d7c9a802d844368c7a68572c009edd1272b03e8dab306e6bcda39007`.

Explicit [admission-off](../../verification/results/loading-trace-mesh-admission-off-summary.json) and [admission-on](../../verification/results/loading-trace-mesh-admission-on-summary.json) loading regressions each pass 75 fake-import controls and 123 actual native-mesh controls. The enabled final witnesses record 20,030 and 11 admitted roots respectively, with zero active/waiting roots and promotions; disabled witnesses record zero admitted roots. The witness reads the real process monitor after cleanup and adds no artificial admission scope.

The six-case mesh-cache matrix also passes with explicit [admission off](../../verification/results/mesh-cache-hook-admission-off-summary.json) and [admission on](../../verification/results/mesh-cache-hook-admission-on-summary.json): 12,905 checks per mode, covering native/wrapped, cache off/on and injected cleanup fault. Enabled witnesses record 110/121/126 native roots and 2,838/3,411/3,416 wrapped roots in their respective cases; every case ends with zero active/waiting roots and promotions. Off-mode witnesses remain zero. Fresh builds and before/after source/native/executable/report hashes are retained separately for both modes; historical unsuffixed reports are not overwritten.

The [actual-hook benchmark](hook-admission-performance.md) separately measures complete disabled/enabled calls and retained-build differences. Build scripts link the admission core and separately compiled ABI adapter and record `cpu_state.h` plus those module dependencies. No gameplay or installed-DLL mutation is part of this hook change.
