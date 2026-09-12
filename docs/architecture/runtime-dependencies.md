# Runtime dependencies and interception mechanisms

Audit and replacement checkpoint on 2026-09-11. Native Windows/Direct3D is a required full
renderer target. No system or bundled DLL file digest should be a runtime
prerequisite. The installed iteration-5 build is separate from this uninstalled
source work; the removals below are not claims about that installed build.

## Runtime gates and replacements

| Source and purpose | Audit finding | Status and replacement |
| --- | --- | --- |
| Former `src/ownership/managed_upload_contract.cpp`, runtime and buffer inspection | Exact D3D9/WineD3D SHA-256, PE image sizes, six import/export RVAs, native vtable entry RVAs, resource offsets, heap association and map count | Replaced in production by `portable_managed_upload` and observed wrapper transactions. Legacy code moved to `verification/probe`; its historical fixture is the only retained private-layout qualifier. |
| `src/proxy/loading_trace.cpp`, former `verify_mesh_module` | Bundled D3DX full-file FNV/size plus SHA-256 gates | Removed from source. Public typed COM interfaces, module lifetime, immutable observed dispatch identities and a process-local algorithm generation replace the gates. |
| `src/proxy/loading_trace.cpp`, `cache_buffer_contract` | Eleven fixed D3DX metadata/acquisition method RVAs | Removed with the D3DX file gates. Public descriptors and unchanged observed method routing supply the intended contract. |
| `src/proxy/loading_trace.cpp`, buffer preflight | Former exact D3D9/WineD3D imports, methods and hashes | Current replacement uses typed GetDesc, readable SYSTEMMEM usage, sizes/formats and known wrapper metadata. Source compilation is not native-Windows verification. |
| `src/proxy/mesh_adjacency_cache.{h,cpp}` | Runtime identity formerly included a DLL SHA field and verified-runtime assertions | Process-local algorithm identity and saved method address remain part of the exact-input key. Actual native outcomes determine admission, including FP/LastError parity; cleanup obligations remain. |
| `src/proxy/object_trace.cpp`, initialization | Whole X3AP SHA-256/file size, preferred base, PE checks and exact E8 call bytes | EXE-specific target guards, not a DLL portability restriction. Retain until an equivalent reviewed target/layout validation replaces them. |
| `src/proxy/object_lifetime.cpp`, initialization | Whole X3AP SHA-256 plus six code-region hashes, PE/base checks and displaced instruction/target checks | Same distinction: these justify game-specific inline detours and private engine layout. Do not blindly remove local target checks. |
| `src/proxy/loading_trace.cpp`, main-image installation | X3AP FNV/file size and PE image/base checks before named IAT parsing | Removed from source. Bounded PE32/import-name validation identifies the actual IAT slots without a whole-image allowlist. |
| `src/proxy/loader.cpp` | Absolute `GetSystemDirectoryW` path to `d3d9.dll`, named export lookup | No backend DLL digest or private-layout gate. This is a Windows API loading path; Ex creation is forwarded without full instrumentation. |

`CMakeLists.txt` and ownership-linked fixtures now list the portable helper rather
than the legacy managed qualifier. The actual 21-object production link includes
the portable helper and excludes the historical qualifier; the 18-object forced
fallback also passes. See [checkpoint review](../verification/review-10.md).
Runtime manifests may record the backend version/digest for
reproducibility without rejecting a different implementation.

## Depth remains a capability-specific adapter

`d3d9_ownership.cpp::initialize_copy_depth` probes the RESZ FOURCC and D24X8 depth
texture capability. `copy_depth` binds the destination and writes the RESZ trigger
`0x7fa05000` through `D3DRS_POINTSIZE`. These are backend/driver extension semantics,
not Wine private structure reads, but ordinary D3D9 does not make this a universal
depth-copy contract.

`src/temporal/depth_decode.hlsl` additionally assumes the captured D24X8 texture
exposes point-filtered shadow comparison and reconstructs depth with 26 comparison
fetches. Its existing Preview tests establish that implementation’s numerical
behavior, not all native Windows drivers. The shared renderer needs an explicit
depth-provider interface with capability-specific adapters and a portable
replay-produced-depth route where native depth sampling/copy is unavailable.
Removing DLL hashes alone does not close this feature gap.

## Private game layout versus private graphics runtime layout

The user explicitly permits patching/trampolines and private interfaces in the
game EXE and game DLLs, with disassembly when needed (2026-09-11 clarification).
Removing runtime DLL hash allowlists does not prohibit these game adaptations.

The game observers intentionally read the reviewed X3AP layout:

- `object_trace.cpp` hooks the call at `0x4c5228`, dispatching the original
  `0x4c0150`. It reads known engine/world/view/projection globals and node/camera
  fields, including node handle `+0x28`, position `+0xb0` and model `+0x140`.
- `object_lifetime.cpp` reads the engine registry at engine `+0x0c`, its bucket
  entries and node handles. It observes insert/remove/destroy and load through
  the game-specific sites listed below.

These x86 game addresses are neither D3D9 COM layout nor Wine implementation
layout. Stable game-version support can retain them with appropriate local
validation. The ordinary Win32 SEH/TLS, COM, memory-protection and synchronization
APIs used by the proxy are platform APIs, not CrossOver-private interfaces.

## Interception mechanisms

| Layer | Actual technique | Scope |
| --- | --- | --- |
| Export proxy | App-local `d3d9.dll` exports forward to the system DLL | `loader.cpp`; backend load occurs outside DllMain. No system D3D9 instruction patch. |
| Ownership | Real C++ COM wrapper objects and generated forwarding methods | 15 normal-D3D9 interfaces, 297 methods; canonical application identities and parent/reference rules are handwritten. |
| Capture | Copy each factory/device vtable, replace selected slots in the copy, then replace that object’s vptr | `capture.cpp::Hooks`; preserves the object’s COM identity. It may operate on the ownership wrapper or native capture fallback. It does not overwrite the shared D3D9 implementation vtable. |
| Loading telemetry | Patch 19 named imports in the main EXE’s IAT | File, directory enumeration, cursor, zlib, XML and D3DX helpers. Original dispatch, patch ownership and restoration are retained. |
| Mesh telemetry/cache | Patch shared D3DX mesh vtable slots 20, 22 and 27 | ConvertPointRepsToAdjacency, GenerateAdjacency and OptimizeInplace, respectively, across at most eight observed tables; this is shared-table interception, unlike per-object capture. |
| Object draw scope | Replace one five-byte E8 call operand/site with a dispatcher | Game `0x4c5228`; TLS and x86 SEH bracket original call execution. No relocation of the target function’s prologue. |
| Object lifetime | Three inline JMP detours with displaced-instruction trampolines, plus one E8 load-call replacement | Game insert `0x4efbf0`, remove `0x4efd39`, destroy `0x4efe10`, load call `0x40508d` to `0x47a720`. Register/flags/FP/SEH and patch ownership require their dedicated verification. |

No production inline instruction detour into D3D9 or WineD3D was found. Standard
COM method indices are ABI positions; implementation RVAs are a different kind
of dependency and are being removed from graphics-runtime qualification.

## Hashes that do not restrict a graphics DLL

Shader fingerprints in `rigid_position.cpp`, `material_radiance.cpp` and
`scene_boundary.h` identify reviewed shader content and pass semantics. They are
not executable/DLL version gates. Likewise, cache key hashes accelerate exact
input comparison, and capture hashes identify recorded payloads. Generated
header SHA comments record source provenance only.

`tools/shaders/generate_rigid_motion_pixel.py` now records the actual local D3DX
compiler digest rather than enforcing a compiler allowlist. Its reviewed delta
retains source/compiler/tool stability checks and output validation; regeneration
with the current local compiler preserved the same 176-word embedded program.
The compiler is an authoring tool, not a runtime dependency of that program. Verification reports and analysis inputs use
hashes to bind evidence and detect files changing during a run; retaining that
provenance does not constrain supported production DLLs.

`tools/manage.py` hashes this project’s installed proxy against its own install
manifest before overwriting, deleting or launching it. This protects ownership
of the user’s file; it is not an allowlist for system D3D9 or the game’s bundled
DLLs. Its default bottle and launch command are CrossOver tooling. A native
Windows install/launch frontend remains a separate packaging requirement.

The replacement source, failure behavior, performance measurements and fresh
fixtures have separate review and evidence linked above. No native Windows
runtime test was performed; the depth-provider and live-exclusion gaps remain.
