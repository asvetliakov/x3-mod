# Native Windows audit of the production proxy (2026-09-12)

Read-only review of the committed production source at `a8d4309` (`src/proxy`,
`src/renderer`, `src/temporal`, `src/ownership`; `verification/` excluded; the
untracked in-flight `engine_memory.*` and `mesh_adjacency_fast.*` ignored). The
question: which assumptions hold under CrossOver/Wine (wined3d on Metal, FEX or
Rosetta host, macOS filesystem) but may break or behave differently on native
Windows with a vendor D3D9 driver. No source was modified; no game was launched.
Nothing here is a native-Windows test result: the findings are source-reading
conclusions plus PE-header and build-artifact facts.

Severity scale: **blocker** (feature cannot work / process fails), **likely**
(documented Windows behaviour differs and the code relies on the Wine
behaviour), **possible** (depends on driver/config; fail-closed or untested),
**cosmetic** (labels, docs, robustness).

## Evidence gathered outside the source

| Item | Value | Consequence |
| --- | --- | --- |
| `X3AP.exe` (Steam and X3 bottles, identical) | 2,153,984 bytes, SHA-256 `fdbf3418…f8ab`, link timestamp `0x5a1d70ad` | Matches the digest in `object_trace.cpp::fingerprint`; the Windows Steam depot ships the same file. |
| PE optional header | `ImageBase=0x400000`, `DllCharacteristics=0x0000` (no DYNAMIC_BASE, no NX_COMPAT), file characteristics `0x0123` (RELOCS_STRIPPED), no `.reloc`, no load-config, no TLS | Windows cannot relocate the image: the fixed VAs (`0x4721b1`, `0x4c5228`, `0x4efbf0`, `0x208a38`…) and the `base==0x400000` guard hold on Windows exactly as in the bottle. ASLR is moot. Not LARGE_ADDRESS_AWARE: 2 GB user VA. |
| `build/d3d9.dll` (10.8 MB) | DYNAMIC_BASE, NX_COMPAT, `.reloc`, `.tls` directory present; imports only `KERNEL32`, `USER32`, `ADVAPI32` and `api-ms-win-crt-*-l1-1-0` | No libgcc/libstdc++/winpthread DLL dependency (`-static -static-libgcc -static-libstdc++`, GCC 16.2 posix thread model). The CRT is the UCRT API-set family (Windows 10+ inbox; Windows 7/8.1 only with the UCRT update). |
| Device creation observed in captures | `flags=0x40` (HARDWARE_VERTEXPROCESSING only; no PUREDEVICE, no MULTITHREADED), first device 64×64 windowed then Reset | `Get*` state queries are legal on Windows; Reset with default-pool resources is exercised on every run. |
| Wine-keyed strings in production | none functional: `wine` appears in comments and in the profiler's module-kind label (`sampling_profiler.cpp:26-27,191`) | No `wine_get_version`, bottle path or backend-identity check in production code. |

## Findings

### 1. D3D9 behaviour that Wine tolerates and Windows may not

| ID | Where | Assumes | Why it may differ on Windows | Severity | Fix / check |
| --- | --- | --- | --- | --- | --- |
| D1 | `src/renderer/temporal_pass.cpp:266` (8-bit main surface → FP16 RT-texture level), `src/proxy/motion_output.cpp:569` (FP16 RT-texture level → main surface), `src/renderer/hdr_pass.cpp:702,922` (emergency rung) | A format-converting `StretchRect` between the application's back-buffer/RT surface and a texture level is granted whenever `CheckDeviceFormatConversion` succeeds, inside or outside the application's `BeginScene`/`EndScene` | Non-Ex D3D9 restricts `StretchRect` by surface type, scene state and driver "conversion/stretch" support beyond the adapter-level query (Remarks table of `IDirect3DDevice9::StretchRect`); wined3d performs any copy it can blit. The gate at `motion_output.cpp:839-846` asks only the adapter query. | likely (X3M_TAA=1 without X3M_HDR) | Replace both conversions with point-sampled full-screen draws (the HDR path already owns an identity write-back program and a scene bracket, `hdr_pass.cpp:879-905`); keep `StretchRect` only for same-format RT→RT copies (`temporal_pass.cpp:271`). Until then, treat the copy as a per-device self test at attach (copy a 4×4 RT both ways and compare) and fail closed with `taa_reason=stretch_self_test`. |
| D2 | `src/renderer/temporal_pass.cpp:123-124`, `src/renderer/hdr_pass.cpp:394-395,505-506`, `src/proxy/motion_output.cpp:1062-1063` (self test) | ps_3_0 programs (`*_program_inc.h` all start `0xffff0300`) drawn with the fixed-function vertex path (`SetVertexShader(nullptr)` + `D3DFVF_XYZRHW`) | D3D9's shader-model pairing rule requires vs_3_0 with ps_3_0; whether pre-transformed vertices are exempt is driver-dependent and the debug runtime flags it. wined3d never checks. The game's own routed draws are safe (all 169 profile rows are vs_3_0/ps_3_0, `motion_output_profiles.h:102`). | possible (fails closed: the sentinel self test at `motion_output.cpp:865-871` would refuse the route, so TAA/HDR would simply be unavailable) | Add one vs_3_0 pass-through (POSITION, TEXCOORD0 → oPos/oT0 with the −0.5 pixel offset) and bind it for every full-screen draw and self test; restore through the existing `D3DSBT_ALL` block. |
| D3 | `src/proxy/motion_output.cpp` route binding of RT1/RT2 (`CreateTexture`, `D3DMULTISAMPLE_NONE`, lines 758-770) with the application's RT0; only the HDR redirect refuses MSAA (`motion_output.cpp:1854`) | The application's RT0 is single-sampled | D3D9 requires all simultaneous render targets to share the multisample type; `SetRenderTarget(1, …)` fails with `D3DERR_INVALIDCALL` on Windows if the game enables its MSAA option. Captures so far ran with `msaa=0`. | possible | Gate the route on `main_.msaa == D3DMULTISAMPLE_NONE` (reason `msaa`) at attach/Reset and log it; do not attempt multisampled RT1/RT2 (textures cannot be multisampled). |
| D4 | `src/ownership/d3d9_ownership.cpp:1253-1261,1320` (X3M_OWNERSHIP + X3M_DEPTH_COPY) | `RESZ` render-target FOURCC and a `D3DFMT_D24X8` depth *texture* are both available | RESZ is AMD/Intel-only; D24X8 depth textures are NVIDIA-style (AMD requires INTZ/DF24). No Windows vendor reports the pair; Wine reports both. `depth_decode.hlsl` also assumes PCF comparison sampling of that texture. | likely (experimental path only; the production TAA depth comes from the R32F RT2, not from this adapter) | Keep as an optional adapter and add an INTZ adapter (all DX10-class Windows drivers) or retire the path; already listed as an open gap. |
| D5 | `src/proxy/motion_output.cpp:802-806`, `src/renderer/hdr_pass.cpp:740-757,782-783` | The caps/format gates cover the stack | Gates are complete for what they use: MRT count, `MRTINDEPENDENTBITDEPTHS`, SM3, RT/sampling queries for FP16, RGBA32F, R32F, FP16 post-pixel-shader blending, and live self tests. `MRTPOSTPIXELSHADERBLENDING` is logged, not gated, which is correct because the route admits only `ALPHABLENDENABLE=0` draws (`motion_output.cpp:1678-1683`) and lazy mode restores the application bindings before every non-routed draw (`motion_output.h:249-253`). All samplers are POINT (`temporal_pass.cpp:145-147`, `hdr_pass.cpp:141`), so no FP filtering assumption. | cosmetic | None required; keep the self tests. Note `CheckDeviceFormat(usage=0)` proves texture creation, not point sampling of FP32; the self tests cover it. |
| D6 | `src/renderer/hdr_pass.cpp:836-837,268,560,667,942`, `src/proxy/motion_output.cpp:962-980,2018-2029` | `GetRenderTargetData` into `D3DPOOL_SYSTEMMEM` offscreen-plain surfaces of the same format, then `LockRect(READONLY)` | Legal on Windows (same format/size, single-sampled, sysmem destination). The deferred one-frame ring (`hdr_pass.cpp:477-483`) still stalls the CPU on Windows until the queued copy completes; the cost model was measured on wined3d only. | cosmetic | Measure on Windows; keep the ring. |
| D7 | Reset handling: `src/proxy/capture.cpp:511-535` → `motion_output.cpp:1102-1130`, `temporal_pass.cpp:173-174`, `hdr_pass.h:170` + `release_chain` | All default-pool resources and the `D3DSBT_ALL` state block are released before `Reset` and recreated lazily | Verified by reading: histories, scratch, RT1/RT2, HDR target, meter chain/ring and state blocks are dropped in `before_reset`; shaders survive. `rigid_motion.cpp:26` creates its block per run and drops it in the destructor. Windows refuses `Reset` with any outstanding default-pool object; Wine is equally strict for resources but not for state blocks. | cosmetic | None; keep the fixture's Reset case. |
| D8 | `src/proxy/loading_trace.cpp:408-420,841`, D3DX hooks | `d3dx9_37.dll` is the game's D3DX and its `ID3DXMesh` vtable is patchable | On Windows the real Microsoft D3DX is used (June 2008 redist, installed by Steam); the import names and COM vtable positions are identical, and `VirtualProtect(PAGE_READWRITE)` on its `.rdata` is copy-on-write. The equivalence claim of the fast adjacency replacement (`mesh_adjacency_fast.h:5,27`, untracked) was established against Wine's D3DX reimplementation. | possible | Run the `verify` adjacency mode on Windows before enabling `fast` there; keep native fallback. |

### 2. Windows API and loader

| ID | Where | Assumes | Why it may differ on Windows | Severity | Fix / check |
| --- | --- | --- | --- | --- | --- |
| W1 | `src/proxy/d3d9.def` (11 exports), `src/proxy/loader.cpp:78-192` | Only `Direct3DCreate9` (the game's single import) and the D3DPERF/Debug entry points need to exist | On Windows the app-local module is what `GetModuleHandle("d3d9")` returns to every other in-process DLL (Steam overlay, driver/vendor overlays, Discord, capture tools). Unexported names (`Direct3D9EnableMaximizedWindowedModeShim`, `PSGPError`, `PSGPSampleTexture`, `Direct3DCreate9On12`, `Direct3DCreate9On12Ex`) return NULL from `GetProcAddress`; some callers treat that as fatal. | possible | Add GetProcAddress-forwarding stubs for the remaining system exports (same pattern as `FORWARD_MARKER`). |
| W2 | `src/proxy/loader.cpp:38-42` | `GetSystemDirectoryW` + `LoadLibraryExW(..., LOAD_WITH_ALTERED_SEARCH_PATH)` reaches the system D3D9 | Correct on Windows: a 32-bit process gets `SysWOW64` through file-system redirection; `d3d9` is not on `KnownDLLs`, so the app-local copy wins the normal search order regardless of `SafeDllSearchMode`. The CrossOver install needs the `d3d9=n,b` override that `tools/manage.py:195` passes; Windows needs none. Undocumented for Windows. | cosmetic | Document the Windows install (copy next to `X3AP.exe`, no override, Steam launches with the game directory as CWD) and the untested coexistence with `GameOverlayRenderer.dll`, which inline-hooks `Direct3DCreate9` and the device methods it finds through the vptr that `capture.cpp` replaces. |
| W3 | `src/proxy/capture.cpp:1232-1239` | The directory next to the DLL (`<game>\x3-modern-captures`) is writable | Under `Program Files (x86)` without Steam's ACL grant, `CreateDirectoryW`/`_wfopen` fail (or are virtualised for a manifest-less 32-bit EXE); the proxy keeps running with `logfile==nullptr`, so every diagnostic silently disappears. Steam's `steamapps` is normally user-writable. | possible | Fall back to `%LOCALAPPDATA%\x3-modern\` when the first open fails and record the chosen path in the first log line. |
| W4 | `src/proxy/loading_trace.cpp:894-916` IAT hooks (`KERNEL32.dll!FindFirstFileA/ReadFile/CreateFileA/…`) | Import-descriptor names match the static names in the EXE | Robust on Windows 7+: kernel32→kernelbase forwarding changes the resolved slot *value*, not the descriptor name, and the slot is patched by address with `InterlockedCompareExchangePointer` after `VirtualProtect(PAGE_READWRITE)` (`loading_trace.cpp:~830`). The `readable(…, executable=true)` check on the resolved target is satisfied by kernelbase code. | cosmetic | None. Note for the record: this parses the EXE's import table, not kernel32's export table, so Exploit-Protection EAF does not apply. |
| W5 | `src/proxy/object_lifetime.cpp:242-258` (hand-built `fs:0` registration whose handler is `x3m_lifetime_unwind`, line 228) | Windows accepts an SEH handler inside a MinGW image | x86 `RtlDispatchException` validates the registration (on-stack, 4-aligned: satisfied) and the handler through `RtlIsValidHandler`: images with a SafeSEH table must list it; images without one (MinGW emits no load-config, and `build/d3d9.dll` is not `NO_SEH`) are accepted. With DEP off for this non-NX_COMPAT EXE (default OptIn policy) the check is lenient anyway; with AlwaysOn it still passes because the handler is in an image. SEHOP chain validation passes because the previous record is linked. Wine performs none of these checks, so this is unverified, not wrong. | possible | Keep `NO_SEH` off the link line (never `--no-seh`); keep the handler returning `ExceptionContinueSearch`. If a SafeSEH requirement is ever imposed (e.g. an enterprise policy), switch the bracket to `AddVectoredExceptionHandler` scoped by TLS. |
| W6 | `src/proxy/scene_hook.cpp:21-22,58-65`, `object_trace.cpp:143-158`, `camera_state.cpp:45-47`, `object_lifetime.cpp:418-425` | Image at `0x400000`, exact bytes at fixed VAs, EXE SHA-256 | Holds on Windows: relocations stripped, no DYNAMIC_BASE (see evidence). The SHA gate ties the features to the Steam 3.4 build; GOG/boxed executables fail closed with `executable_mismatch` (motion output, camera state, scene hook and lifetime off; forwarding continues). | possible (documentation) | Document the supported executable; if other builds matter, key the sites on byte patterns plus the six code-region hashes rather than the whole-file digest. |
| W7 | `src/proxy/scene_hook.cpp:65-73`, `object_trace.cpp:96-111`, `object_lifetime.cpp:285-342` | `VirtualProtect(PAGE_EXECUTE_READWRITE)` on the game's `.text` and `FlushInstructionCache` | Standard on Windows (copy-on-write private pages). Windows Defender's "Exploit Protection" code-integrity/ACG options are not enabled for arbitrary desktop games by default. | cosmetic | None. |
| W8 | `src/proxy/scene_hook.cpp:25`, `object_trace.cpp`, `engine_memory.h:18` (rpm mode) | `ReadProcessMemory(GetCurrentProcess(), …)` | Documented and works on own process; it is a syscall per read on Windows too (rpm mode is the A/B reference only). | cosmetic | None. |
| W9 | `src/proxy/sampling_profiler.cpp:129-135,198-245,264-345,435` (X3M_PROFILE) | `SuspendThread`/`GetThreadContext`/`Toolhelp32` on a WOW64 thread, `NtQueryInformationThread` classes 0 and 9, NT_TIB stack bounds, `GetWindowsDirectoryW` prefix to classify modules | All documented or long-stable on Windows; a suspended WOW64 thread reports its 32-bit context. The `%WINDIR%` prefix labels every system module `wine` on Windows (misleading in reports). `ThreadQuerySetWin32StartAddress` (9) is semi-documented. | cosmetic | Rename `KindWine` → `system`; keep the "no Win32 calls inside the suspended window" contract (already followed). |
| W10 | `src/proxy/loading_trace.cpp:97,107,147,191`, `ownership/*.cpp` (`thread_local`), `build/d3d9.dll` `.tls` directory | Implicit TLS works in this DLL | The DLL is loaded by the EXE's import table at process start, so implicit TLS slots exist for every thread; Vista+ also handles `LoadLibrary`-time TLS. | cosmetic | None. |
| W11 | `build/d3d9.dll` imports `api-ms-win-crt-*-l1-1-0.dll` | UCRT is present | Inbox on Windows 10/11; Windows 7/8.1 need KB2999226 (Steam's VC++ 2015+ redist installs it). If absent, the app-local DLL fails to load and the game fails to start (`d3d9.dll` is a static import). | possible | State Windows 10+ as the supported baseline, or build with the msvcrt-flavoured mingw-w64 CRT. |
| W12 | timing (`QueryPerformanceCounter` everywhere: `telemetry.cpp:30-37`, `hdr_pass.cpp:193`, …), paths (`GetModuleFileNameW`, backslashes, wide APIs), env switches (`GetEnvironmentVariableW`) | | Portable as written; QPC is invariant-TSC backed on Windows (cheaper than under Wine, where the comments note a syscall per stamp). | cosmetic | None. |

### 3. Compiler and ABI

| ID | Where | Assumes | Windows consequence | Severity | Fix / check |
| --- | --- | --- | --- | --- | --- |
| C1 | `CMakeLists.txt:22-23` (`-static -static-libgcc -static-libstdc++`, posix thread model) | No MinGW runtime DLLs at load time | Confirmed by the built import table (only system DLLs). | cosmetic | Keep; add an import-table check to the build (fail if any `lib*.dll` import appears). |
| C2 | i686 MinGW uses DWARF-2 C++ EH; `try/catch` in `motion_output.cpp`, `scene_capture.cpp`, `d3d9_ownership.cpp` and others | Exceptions never cross a hook boundary | All catches are local to allocation sites and `application_admission_abi.cpp` is built `-fno-exceptions`. A C++ exception escaping into a game frame or a naked trampoline would be unrecoverable on Windows (no unwind info in the game). Same on Wine. | cosmetic | Keep the rule; consider `-fno-exceptions` on the hook-entry translation units. |
| C3 | `__attribute__((naked))` trampolines with hand-written underscore symbols (`scene_hook.cpp:44-50`, `object_lifetime.cpp:242-263`, `object_trace.cpp:69`) | GCC i686 symbol decoration | Fine for MinGW/GCC and clang on x86; absolute operands (`jmp *_x3m_scene_end_original`) are relocated (`.reloc` present, DYNAMIC_BASE on). | cosmetic | None. |
| C4 | `-mstackrealign -mincoming-stack-boundary=2`, `force_align_arg_pointer` on callbacks, manual 16-byte alignment before `fxsave` | | Correct for legacy 4-byte callers on both platforms. | cosmetic | None. |

### 4. Wine-specific keys

None in production. `tools/manage.py` (bottle names, `--dll d3d9=n,b`) is tooling. The
profiler's `wine` module class (W9) is a label only.

## Status of the open items in platform-portability.md

| Item | Status after this audit |
| --- | --- |
| Finite-position observer, native verification outstanding | Still open; no Windows-specific defect found in `portable_managed_upload.cpp` (public `GetDesc`, `D3DPOOL_MANAGED`, observed Lock/Unlock only). |
| RESZ + D24X8 depth adapter | Still open; D4 sharpens it: the capability pair does not exist on any Windows vendor, so the adapter is effectively Wine-only. Production TAA depth does not use it. |
| WineD3D internal mutex | Closed for production: no reference in `src/`. |
| macOS HDR presentation / Windows presentation path | Still open; nothing in the proxy touches DXGI or EDR, so the FP16 scene path ends in an 8-bit write-back on both platforms. |
| TAA `StretchRect` conversion with `CheckDeviceFormatConversion` | Still open and now the top item (D1); the state-block release before Reset is verified (`temporal_pass.cpp:173`). |
| FP16 HDR stack unverified on native drivers | Still open; gates are complete (D5), the vertex-path question (D2) applies to its write-back and meter draws too. |
| No native Windows run | Still open. |

## Status (pre-review 28, 2026-09-12 evening)

| ID | Status | Where / evidence |
| --- | --- | --- |
| D1 | **Fixed** | `MotionOutput::stretch_round_trip` (attach, per 8-bit format) decides `taa_copy=stretch|draw`; `TemporalPass::configure_copy(true)` copies same-format into a staging texture and converts with identity draws both ways (`Output::display_written`, `copy_result`). No refusal remains (`format_conversion` gone). Fixture: temporal `COPY_MODE ... history_identical=1 display_max_code_difference=0`, motion `seam-taa-copy-draw` twin (`X3M_FIXTURE_STRETCH_FAULT=1`). HDR emergency rung unchanged (already self-tested). |
| D2 | **Fixed** | `src/temporal/quad_vs.hlsl` → `quad_vertex_program_inc.h` (vs_3_0, 43 words), `quad_vertex_program.h` (clip-space quad with the −0.5 shift, declaration); one VS + declaration per pass (TemporalPass, HdrPass, MotionOutput), bound in place of `SetFVF`+null VS; sentinel/self-test programs now ps_3_0. `abi_check.cpp` asserts slot 86. Fixture twin `X3M_QUAD_FVF_SWITCH`/`X3M_FIXTURE_QUAD_FVF=1`: temporal `QUAD_TWIN ... identical=1` ×6, motion `seam-taa-quad-fvf` byte-identical to `seam-taa-on`. |
| D3 | **Fixed** | The selector already refused a multisampled RT0 silently; `MotionOutput::after_clear` now names it: `motion_output_msaa_refused device= frame= msaa= width= height=` once, `main_msaa_` gates every draw, no jitter, `TaaSkip::Msaa` (11), `msaa=` in the frame line; cleared by a single-sampled latch or Reset. Fixture mode `msaa` (`X3M_FIXTURE_MSAA=2`, `CheckDeviceMultiSampleType` required), runner case `seam-msaa`. |
| D4 | Open | RESZ/D24X8 adapter unchanged (experimental path). |
| D5–D8 | Unchanged | Cosmetic / documentation items; D8 (adjacency `verify` on Windows) remains. |
| W1 | **Fixed** | `d3d9.def` 17 names; `loader.cpp`: naked `jmp` forwarders with `ret N` fallbacks (`DebugSetLevel` 4, `PSGPError` 12, `PSGPSampleTexture` 20, shim 4), C++ `Direct3DCreate9On12[Ex]` with admission veto and `unproxied=` log, `Direct3DCreate9Ex` logs the same line. Host test `verification/analysis/test_d3d9_exports.py` (`tools/analysis/pe_exports.py`), Wine fixture `d3d9_exports_fixture.cpp` + `run_d3d9_exports.py`. |
| W2 / W6 / W11 | Open (documentation) | Windows install notes still to be written. |
| W3 | **Fixed** | `capture.cpp::initialize_log`: `%LOCALAPPDATA%\x3-modern-renderer\captures` (else `%USERPROFILE%\AppData\Local`) when the game directory refuses; first line `capture_dir=<path> source=game|localappdata`; `capture_directory()` follows. Runner case `readonly` of `run_d3d9_exports.py`; README and `manage.py status` name both locations. |
| W4, W5, W7–W10, W12, C1–C4 | Unchanged | Verified-by-reading or cosmetic. |
| — | Added | `engine_memory phase=create|summary device= path=direct|rpm reads= queries= hits= rejected= rpm_calls= frame=` (integers only) at device creation and in every telemetry summary; the exposure fixture prints non-finite values as IEEE bits (FEX limitation 2 of bottles.md). |

## Prioritised fix list (no source edits made here)

1. **D1** — replace the two format-converting `StretchRect` copies of the non-HDR TAA path with point-sampled draws (reuse the HDR identity write-back), or add a live 4×4 round-trip self test that fails the route closed.
2. **D2** — add a vs_3_0 pass-through for every proxy full-screen draw and self test (temporal resolve, HDR write-back/tonemap/meter, motion self test).
3. **D3** — refuse the route (and TAA/HDR with it) when RT0 is multisampled; log `reason=msaa`.
4. **W1** — export the remaining system `d3d9.dll` names as forwarding stubs.
5. **W3** — log-directory fallback to `%LOCALAPPDATA%` with the path recorded in the log.
6. **W2 / W6 / W11** — write the Windows install and support notes: app-local DLL with no override, Steam 3.4 executable only, Windows 10+ (UCRT), Steam overlay untested.
7. **D4** — INTZ adapter or retirement of the RESZ path; **D8** — run adjacency `verify` on Windows before `fast`.
8. **W9 / C1** — cosmetic: rename the profiler's `wine` class, add an import-table check to the build.

## Verified as portable by reading (no action)

Device flags (no PUREDEVICE/software VP), Reset lifecycle (D7), readback path (D6),
IAT hooking against forwarders (W4), fixed-VA assumptions given the stripped-
relocation EXE (W6), static runtime (C1), implicit TLS (W10), timing and path
handling (W12), all-POINT samplers and the blend-free route admission (D5).
