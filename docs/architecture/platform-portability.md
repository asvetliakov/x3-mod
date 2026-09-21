# Native Windows and CrossOver support

**2026-09-21 media scope update:** owned video playback, its startup/consumer/
destination hooks and LAV dependency are retired from production by user choice.
Earlier media qualification sections below remain historical. The remaining
ID2 allocator refusal uses the existing game hook on both targets; native Windows
runtime verification remains open. Speech decoding is unaffected.

User requirement, added 2026-09-11: the graphics enhancements must work on native
Windows/Direct3D as well as CrossOver Preview. The user currently cannot run
Windows tests. Compiling a Windows DLL or forwarding successfully on another
backend does not establish native-Windows behavior or feature support.

Shared rendering and synchronization must use documented Windows/Direct3D APIs
and portable C++ facilities. Wine exports, private backend layouts and internal
mutexes cannot be prerequisites for required rendering features. A backend-specific
optimization or diagnostic may remain isolated behind explicit capability checks;
required features also need a portable/native-Windows implementation. Unknown
capabilities must be reported honestly rather than silently counted as supported.
The user also requires avoiding exact DLL hash gates, including system graphics
libraries and bundled game DLLs. Replace the private-layout dependency itself. Removing
its hash checks while retaining private-offset reads would not satisfy this.
Recording runtime hashes in test reports remains useful provenance.

## Session identity

Every session log opens with two lines written once by `capture.cpp`
`initialize_log` (implementation in `src/proxy/proxy_identity.cpp`), ahead of
every derived `*_mode` line, so a gameplay log can never lose its provenance:

    proxy_identity sha256=<64 hex|unavailable> bytes=<n> path=<dll path> manifest_sha256=<64 hex|none> source_commit=<commit[-dirty]|unknown> attach_us=<n>
    proxy_options [NAME=VALUE ...]
    proxy_environment [NAME=VALUE ...] count=<n>

The same `initialize_log` writes one wall-clock anchor before the identity
header, and two `loaded_module` lines identify the DLLs the process actually
runs with:

    clock_anchor utc=<ISO8601 with ms> qpc=<ticks> qpc_frequency=<Hz> local_offset_min=<minutes>
    loaded_module name=<dll> path=<full path|none|unavailable> size=<bytes> sha256=<first 16 hex|none|unavailable> hash_us=<n>

`clock_anchor` pairs one `GetSystemTimePreciseAsFileTime` reading (dynamically
resolved; `GetSystemTimeAsFileTime` before Windows 8) with one
`QueryPerformanceCounter` reading, plus the counter frequency and the negated
`GetTimeZoneInformation` bias, so a line stamped in local wall clock by another
component can be mapped onto the `qpc=` field the frame windows carry
(recipe in `docs/verification/sampling-profiler.md`, "Audio correlation").
`loaded_module` is written for the D3D9 backend the proxy forwards to (after
`loader.cpp` loads `<system directory>\d3d9.dll`) and for `d3dx9_37.dll` at the
first device creation, when the game's imports are resolved: the path, size and
hash prefix say which copy of a DLL that exists twice on disk (the game
directory ships its own `d3dx9_37.dll`) is in the process, and which D3D9
implementation is behind the proxy. Nothing is classified by name here; the
same documented calls as above, one file hash each (its cost reported as
`hash_us=`), once, off the render path and, at the device-creation site, ahead
of the hook mutex and of the frame-timing scope; `GetModuleHandleExW` holds a
reference across the read.

`manifest_sha256` is the `sha256` value recorded in the `x3-modern-install.json`
next to the DLL (`none` without a readable manifest); `proxy_options` lists every
`X3M_*` variable of the process environment, sorted, and nothing else, and `proxy_environment` lists, in the same form and from the same enumeration, every `FEX_*`, `WINE*` and `CX_*` variable sorted, values truncated to 200 characters with `…` and the entry count last, so a run can show which emulation and Wine settings the launcher set actually reached the process. The digest
is the SHA-256 of the loaded module's own file, computed at attach through
documented Win32 (`GetModuleFileNameW`, `CreateFileW`/`ReadFile` with
`FILE_FLAG_SEQUENTIAL_SCAN`, CryptoAPI `PROV_RSA_AES`/`CALG_SHA_256`,
`GetEnvironmentStringsW`); nothing runs per frame, `GetLastError` is restored and
no exception escapes (a failure yields `sha256=unavailable`). `attach_us` is what
the header itself cost, measured with QPC: ~122 ms for a 15.8 MB DLL under
CrossOver/FEX, all of it the emulated SHA-256, once per process. Any character
outside printable ASCII in a path or option value becomes `_` so the
space-separated grammar cannot split.

`tools/build/write_source_commit.py` resolves `source_commit` into
`build/generated/x3m_source_commit_inc.h` at configure time and again on every
build (target `x3m_source_commit`), rewriting the header only when the value
changes; the dirty check covers tracked and untracked files under `src`, `cmake`,
`tools` and `CMakeLists.txt`. The same string is embedded in the DLL as the byte
marker `X3M_SOURCE_COMMIT=<commit>`, so `tools/manage.py install` records the
commit of the DLL being installed (`source_commit`, with `manifest_source=dll`)
and falls back to the launcher repository's HEAD (`manifest_source=launcher`) for
a DLL without the marker. The host parser is `tools/analysis/proxy_identity.py`;
its `scan`/`scan_log` never raise on log content and report unparsable lines in
`malformed` (`verification/analysis/test_proxy_identity.py`).

## Current gaps

- The media-cue gate (`--media-cue-trace`, `--media-cue-cache`,
  [media-cues.md](../verification/media-cues.md) §6) is an EXE-side patch on
  `0x00498140` of the non-relocatable, hash-gated X3AP.exe: the same bytes on
  native Windows, so the byte-verified claim, the two-arm stub and the
  return-address capture are platform-independent x86 and need nothing from
  Wine. Its handlers use documented Win32 only (`QueryPerformanceCounter`,
  `GetCurrentThreadId`, `Get/SetLastError`) and the patch machinery's
  `VirtualProtect`/`FlushInstructionCache`. The failing DirectShow build it
  refuses is a CrossOver symptom (missing decoders); the launcher enables the
  cache by default (`--media-cue-cache on`, since 2026-09-17) and ships the same
  bytes on native Windows, where the quartz filters normally decode and the
  cache simply never fills - a missing codec on Windows is exactly the case the
  cache handles, with the same 30 s retry. The cache needs no telemetry. Cross-compiled
  and fixture-qualified under CrossOver; native execution unverified like the
  other engine patches.

- Sun-shadow cascades and the per-program sun ([shadow-cascades.md](shadow-cascades.md),
  [directional-shadows.md](../verification/directional-shadows.md) "Sun-shadow cascades and
  the run-38 fixes") are documented D3D9 only: up to four `R32F` render-target textures and one
  shared `D24X8`/`D16` depth-stencil at least as large as each target (`CheckDeviceFormat`,
  `CheckDepthStencilMatch`), `MaxTextureWidth/Height` checked at attach with the cascade sizes
  halved to fit, one `D3DSBT_ALL` block per pass with the caller's FVF or declaration re-set
  explicitly, the application's own buffers and declaration, `GetRenderTargetData` for the F8
  readbacks, five samplers, `texldl` inside a ps_3_0 loop and dynamic branch, the program's slot
  count (406) gated against `MaxPixelShader30InstructionSlots` (a device below it keeps the
  single-map program and refuses the cascades). `LightDir_Dir0`'s register comes from the
  program's own constant table (the documented `D3DXSHADER_CONSTANTTABLE` layout in the `CTAB`
  comment, bounds-checked) and the application's own `SetPixelShaderConstantF` writes; no Wine
  export, layout or hash. Cross-compiled and fixture-qualified under CrossOver; native
  execution, and a native driver's handling of the loop inside the branch, are unverified.

- Sun-shadow caster retention ([shadow-caster-retention.md](shadow-caster-retention.md),
  `--shadow-retention-census`, `--shadow-caster-retention`, default off) is documented D3D9 and
  COM only: `AddRef`/`Release` on the application's own managed vertex buffer, index buffer and
  vertex declaration (one reference per distinct resource, taken at the draw), the same
  `SetStreamSource`/`SetIndices`/`SetVertexDeclaration` replay as the live records, every
  reference released before every Reset attempt, on a failed Present, at teardown and before
  the application's final device Release. The orphan probe reads the documented
  `AddRef`/`Release` return values in an advisory, fail-safe role behind a capability flag that
  one private managed buffer decides at attach (`orphan_probe=` in `shadow_retention_device`);
  a runtime that does not report counts only delays a drop to the age cap, box exit or
  retirement. Node identity, class bits and the retirement journal come from the game-EXE
  observers behind the executable gate; with the gate closed the feature is off. No Wine
  export, lock, layout or hash. Cross-compiled and fixture-qualified under CrossOver (where
  the probe reports counts); native execution is unverified.

- The point-light root-admission patch (`--point-light-root-admission`,
  [camera-and-lights.md](../reverse-engineering/camera-and-lights.md)
  "Implementation") is documented Win32 only: `VirtualProtect`,
  `FlushInstructionCache`, `VirtualQuery` through the engine-memory reader,
  `GetModuleHandleExW` pin, `Get/SetLastError`; the retargeted `jg` and the
  integer-only detour are process-local and the EXE is non-relocatable and
  hash-gated. Cross-compiled and fixture-qualified under CrossOver; native
  Windows execution unverified like the other engine patches.

- The hybrid unhook ([state-call-fast-path.md](state-call-fast-path.md),
  "Hybrid unhook (step 5, implemented)") reads the route's draw-time render
  and sampler state with `GetRenderState`/`GetSamplerState` instead of hooking
  the setters: documented D3D9 on a non-pure device (the proxy strips
  `D3DCREATE_PUREDEVICE`), with one `GetRenderState` and one `GetSamplerState`
  through the saved native entries at device creation as the capability
  check; a device that refuses either keeps the hooked configuration
  (`state_hooks ... reason=get_failed`). Qualified under CrossOver by the
  benchmark and the motion-output fixture; the native runtime's Get* on the
  game's actual device flags is unverified like the rest.

- The [loading interval recorder](../verification/loading-intervals.md) uses
  documented Windows QPC, TLS, interlocked, allocation and file APIs. Its x86
  build, CPU audit and CrossOver fixture qualification pass; native Windows
  execution and actual game recorder/export overhead remain unverified.

- The locked-prefix bullet bound (step D,
  [screen-emission-bullet-bound.md](screen-emission-bullet-bound.md)) writes a
  sentinel into the application's `D3DLOCK_DISCARD` mapping of a
  `D3DUSAGE_WRITEONLY` dynamic vertex buffer before returning it (25–147 KB
  memset per marked lock) and reads the written prefix back from the same
  mapping at Unlock. Both are documented D3D9 (the mapping is ordinary
  process memory during the lock; DISCARD contents are undefined), but on
  native Windows such mappings are write-combined: the store streams, the
  read is uncached and far slower than the CrossOver figures (12 µs per
  6144-vertex scan, `sentinel_us` on the `locked_prefix_frame` line).
  Unmeasured on native Windows; the documented remedy is a staging lock
  (proxy memory returned, prefix copied at Unlock), which also makes the
  retained copy free.

- The packed screen policy (policy 8, [screen emission](screen-emission-region.md),
  step E) carries its constants as shader `def` literals: the promoted bullet
  producer's `c31` (M = (1, 0, 0, a)) executes inside the application's draw
  after the game has set pixel constants c0–c35 (run 17), and the pass's
  composite `def c0`/`def c1` (decode exponents, the gain) run under the
  pass's own binds. Documented D3D9 semantics load `def` values at
  SetPixelShader and let a later SetPixelShaderConstantF override them; the
  D3DMetal bottle gives the `def` precedence for the producer (a live frame
  with the run-17 block set after the bind composed the law), native Windows
  is unverified. A native override would break the coverage lane, not the
  native lane; the fallback is a capability-checked producer variant without
  constants or per-draw constant save/restore. No gameplay defect has been
  attributed to it.

- The installed 168-pair material route carries generated material RGB in a
  separate whole COLOR1 varying, keeping RGB on the COLOR interpolation path
  and avoiding application D3DRS_WRAP state. Palette scalar relocations copy each
  source WRAP component to its destination for the draw, while generated
  motion/depth TEXCOORD components use the scoped zero-WRAP transaction; caller
  state is restored on success, failure, StateBlock and Reset paths. The X3
  detached and live hostile-WRAP fixtures qualify these contracts. The Preview
  backend's programmable COLOR classifier remained Gouraud when FLAT was
  requested, so those results do not establish native-Windows FLAT behavior.
  Native-Windows interpolation and WRAP execution remain unverified; no observed
  gameplay defect has been attributed to this portability gap. See the
  [palette transport](linear-palette-materials.md), [XT qualification](xt-materials.md)
  and [combined evidence](../verification/combined-glow-materials.md).

- The constant material fill (`--material-fill`, source `a53cf8f`,
  [ledger](../verification/fill-light.md)) is one shader-local `def c215` and
  one `mad` in ps_3_0 per converted pixel program: documented D3D9 only, no
  new API use, capability requirement or backend-specific dependency. It
  compiles for i686 MinGW with the project's SSE2 and four-byte-stack flags;
  X3 detached and live fixtures qualify its CrossOver behaviour. Explicit K=0 omits the fill instructions. The inherited two-constant `MAX`
  violation is now repaired with full-precision temporary staging; the
  [repair ledger](../verification/linear-material-constant-port.md) records
  one-constant-source checks over 1,562 variants and exact retained CrossOver
  GPU parity. Cross-compilation and CrossOver shader creation do not establish
  native Windows creation or runtime, which remain unverified.

- The default-off directional sun-share lane now uses documented D3D9 format,
  MRT, state and shader APIs, with portable A32B32G32R32F-to-R32F shader copying
  (the lane's wide RT2, `shadow-receiver-depth.md`, the only encoding since 2026-09-18: a
  128-bit third MRT beside the 64- and 128-bit first two rests on the already required
  `D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS`; a partial `oC2.zw` write mask; unverified natively).
  CrossOver GPU qualification covers extraction, temporal copying and actual
  receiver/composition/fallback/Reset behavior. Native Windows execution,
  gameplay coverage and GPU performance remain unverified. Missing cached
  shader variants stay unavailable through Reset; complete-cache bind failures
  can recover. See the [sun-share ledger](../verification/directional-shadows.md).

- The original four XT DEFAULT pairs still have malformed SM3 linkage under
  Microsoft's [matching rules](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/shader-model-3)
  and are never used as portable ordinary fallbacks. The installed 14-pair XT
  route instead publishes complete authored ordinary and linear DEFAULT pairs
  together; its ten valid BUMPMAP/BUMPMAP_LOW pairs retain their original
  ordinary programs. Exact-pair readiness, fallback, hostile WRAP,
  interpolation, Reset and retirement pass the detached and live X3 fixtures.
  The authored DEFAULT geometry is a reviewed replacement contract rather than
  recovered native behavior. Native-Windows creation, linkage, interpolation,
  state recovery and runtime execution remain unverified; successful Preview
  execution does not establish them. See [XT materials](xt-materials.md).

- The previously embedded TAA resolve reported 1,179 instruction slots, while the
  X3 fixture device advertises `MaxPixelShader30InstructionSlots=512`; the first
  unrolled supplemental source reported 1,261. Historical X3 execution of those
  shaders does not make them cap compliant. The reviewed all-loop supplemental
  source now compiles to 507 slots / 1,891 DWORDs, within both the documented
  ps_3_0 minimum and the X3 advertised limit. Its focused X3 qualification
  matches the 1,261-slot source exactly over 30 paired frames and passes the full
  temporal and Reset coverage; measured full-pass completion-wall cost is small
  but noisy and is not a GPU-busy or game-FPS claim. The regenerated embedded
  artifact matches the qualified bytecode and is installed after a clean
  build/load check. Gameplay acceptance and native-Windows execution remain
  unverified. See the [emission temporal work](linear-emission-composition.md#consumer-contract).

- Opt-in bloom uses documented D3D9 calls and a compiler-supported x86 SEH
  bridge. Runs 27 and 28 verify CrossOver gameplay integration, including
  visible authored-color halos and working ON/OFF control; Run 28 exercised
  gain 0.35/scatter 0.70. The installed gain 0.375/scatter 0.65 adjustment has
  offline calibration and focused source/reference checks, but its exact
  gameplay appearance and gameplay performance remain unverified. Native-Windows
  execution also remains unverified. Capture hooks ResetEx at slot 132 on
  admitted Ex-capable devices, but the separate
  `Direct3DCreate9Ex`/`CreateDeviceEx` factory route still forwards without
  capture adoption. The bloom lifetime fixture explicitly adopts its genuine
  Ex device under its test-only seam to exercise ResetEx; this does not establish
  production CreateDeviceEx enhancement support. See the
  [authored-glow evidence](bloom-authored-glow.md) and
  [Run 28 comparison](../verification/run28-glow-materials.md).

- The opt-in [chase camera](chase-camera.md) modifies validated game structures
  through an x86 trampoline and uses public Win32 memory, protection and timing
  APIs. No Wine-private interface is required. It cross-compiles with the
  production SSE2/stack contract. Run 18 accepts CrossOver chase aiming and
  stability; the installed lead-marker revision still needs gameplay alignment.
  Native-Windows runtime behavior remains unverified.

- The optional chase HUD forward anchor (source `da2f17b`,
  [ledger](../verification/chase-hud-anchor.md)) uses existing byte-verified
  game seams and documented Windows memory APIs; it adds no Wine-private
  dependency. Its 81 host checks and strict i686/SSE2 compilation establish
  source-level portability, not native-Windows runtime or gameplay glyph/bolt
  alignment, both of which remain unverified.

- The finite-position observer now uses public descriptors, readable managed
  backing and observed wrapper Lock/Unlock transactions. The former exact-Wine
  qualifier is historical verification code only. The portable source still
  needs native-Windows runtime verification; see
  [its contract and evidence](../verification/portable-managed-upload.md).
- Depth copying currently uses the RESZ extension and a D24X8 comparison-sampling
  adapter tested on Preview. Required rendering effects need a suitable depth
  provider on native Windows too; ordinary D3D9 calls alone do not guarantee
  those extension semantics on every driver.
- The proposed use of WineD3D's internal graphics mutex is backend research only.
  It is not the shared replay-exclusion design. Continue a portable ownership/call
  admission protocol that does not depend on backend-private synchronization.
- macOS HDR/window presentation research is platform-specific by nature. A
  native-Windows presentation path must supply the corresponding HDR and window
  behavior through documented Windows graphics interfaces.
- The temporal resolve's 8-bit-to-FP16 copies (`X3M_TAA=1`) no longer
  depend on a driver granting a format-converting `StretchRect`: the route
  decides per device at attach (`motion_output_device ... taa_copy=stretch|draw
  taa_stretch_test=`) from the adapter query *and* a live 4×4 round trip per
  8-bit format, and otherwise copies by same-format `StretchRect` into a
  staging texture plus identity draws both ways (D1; Wine keeps the stretch
  path bit for bit, the fixture's `seam-taa-copy-draw` twin proves the draw
  path equal in history and within one code in presented frames, and the
  temporal fixture's `COPY_MODE` line reports `history_identical=1
  display_max_code_difference=0`). The pass's cached `D3DSBT_ALL` state block
  still holds references to the application objects bound at the copy until
  the next frame's capture (released before Reset); on native D3D those keep
  the device count above the final-release probe until the block is dropped,
  on Wine the application-level references are independent of it.
- Every full-screen quad the proxy draws (temporal resolve, sharpen, copy
  draws, HDR write-back/tonemap/meter chain, the route's self tests and
  sentinel fill) binds the embedded vs_3_0 pass-through and its declaration
  ([`quad_vertex_program.h`](../../src/renderer/quad_vertex_program.h); D2):
  D3D9 pairs ps_3_0 with vs_3_0, and the XYZRHW fixed-function path the
  quads used before is not a documented partner. The sentinel and
  self-test programs carry the ps_3_0 version token for the same reason. The
  fixture-only XYZRHW twin (`X3M_QUAD_FVF_SWITCH`, `X3M_FIXTURE_QUAD_FVF=1`;
  never compiled into production) is byte-identical to the new path on the
  Preview backend (`seam-taa-quad-fvf`, six `QUAD_TWIN ... identical=1`
  lines of the temporal fixture).
- A multisampled main target is refused by name (D3): the selector never
  latches one, and the route logs `motion_output_msaa_refused device= frame=
  msaa=` once, routes and jitters nothing (gate 1), skips the resolve with
  `taa_skip=11` and carries `msaa=` in every frame line (`seam-msaa`; the
  HDR redirect already refused, `refused_msaa`). RT1/RT2 textures cannot
  share a sample count and D3D9 requires every simultaneous target to match.
- The FP16 HDR scene path (`X3M_HDR=1`, stage 1) needs an `A16B16G16R16F`
  render-target texture with post-pixel-shader blending and sampling, the
  three-format independent-bit-depth MRT (FP16 + RGBA32F + R32F) and, for the
  emergency unwind rung only, `CheckDeviceFormatConversion(A16B16G16R16F →
  A8R8G8B8)` plus the self test's live 4×4 copy that demotes the rung; all are
  queried at attach through the documented caps and a live self test, and the
  feature disables itself otherwise. Native drivers
  are untested against this stack ([hdr-scene-path.md](hdr-scene-path.md) §5).
- The app-local `d3d9.dll` exports the seventeen names of the system DLL
  (W1: the fifteen CrossOver exports plus `Direct3D9EnableMaximizedWindowedModeShim`
  and `Direct3DCreate9On12Ex`), so an in-process module resolving them
  through `GetModuleHandle("d3d9")` gets an answer: `Direct3DCreate9On12[Ex]`
  are C++ forwarders that veto admission and log `unproxied=1` when an
  object escapes (`Direct3DCreate9Ex` too); `DebugSetLevel`, `PSGPError`,
  `PSGPSampleTexture` and the shim are signature-agnostic naked `jmp`
  forwarders with `ret N` fallbacks (4/12/20/4 bytes) when the backend lacks
  the export, each logged once (`d3d9_export name= forwarded=`). Host test
  `test_d3d9_exports.py` parses the PE export directory; the Wine fixture
  `run_d3d9_exports.py` resolves all seventeen and calls the forwarded and
  the fallback entry points.
- Session logs fall back to `%LOCALAPPDATA%\x3-modern-renderer\captures`
  when the game directory is not writable (W3; the first log line
  `capture_dir=<path> source=game|localappdata` records the choice;
  `run_d3d9_exports.py`'s read-only-directory case exercises it under Wine).
- Current CrossOver fixtures do not establish native-Windows rendering, reset,
  multithreading, presentation or performance. Everything above is
  Windows-compatible source verified on CrossOver Preview (Steam and X3
  bottles); native-Windows verification remains outstanding and no
  successful Windows run is claimed.

Game EXE/DLL private structures and code hooks remain allowed. Use disassembly
where needed and validate the targeted game ABI/layout; this permission is
separate from avoiding dependencies on private graphics-runtime implementations.

For each new component, review dependencies and capability gates alongside code
correctness and performance. Preserve separate evidence for CPU-only behavior,
Windows builds, CrossOver runtime tests and eventual native-Windows runtime tests.

See the [runtime dependency and interception audit](runtime-dependencies.md) for
concrete remaining gates, removal status and the separate depth-adapter gap.
- The cascade-0 depth replay (`src/renderer/shadow_replay_pass.cpp`, `--shadow-replay-depth`, default
  off, no consumer) uses documented D3D9 only: `CheckDeviceFormat`/`CheckDepthStencilMatch` for the
  R32F map (X8R8G8B8 with colour writes off as the fallback) and its D24X8/D16 attachment, a
  D3DSBT_ALL block, the application's own buffers and declaration under authored vs_3_0/ps_3_0
  programs; verified on CrossOver only (R32F render targets, the depth match, state-block
  capture/apply cost and the single-thread Lock assumption remain unverified natively).
- The sun-shadow apply pass (`src/renderer/sun_shadow_apply_pass.cpp`, standalone, not yet wired
  into the scene end) uses documented D3D9 only: `CheckDeviceFormat` gates for `A32B32G32R32F`/`R32F`
  render-target textures (the `A32B32G32R32F` RT2 is checked per frame from its `GetLevelDesc`;
  an R32F or G32R32F RT2 skips the quad) and post-pixel-shader
  blending on the owning `A16B16G16R16F` format,
  ZERO/SRCCOLOR blend caps, `MaxPixelShader30InstructionSlots` against the embedded ps_3_0 program
  (`dsx`/`dsy` receiver-plane bias, nine point taps), one `D3DSBT_ALL` block, one `DrawPrimitiveUP`
  quad. Cross-compiled with the SSE2/four-byte-stack policy; verified on CrossOver only (the quad
  derivative convention of `dsx`/`dsy` at 2x2 quad granularity, FP16 blend rounding and the format
  gates are unverified natively; the fixture excludes pixels whose result depends on the convention).
- The ambient occlusion pass (`src/renderer/ambient_occlusion_pass.cpp`, step 1, detached) uses
  documented D3D9 only: `CheckDeviceFormat` gates for the R32F/R16F render targets and post-pixel-shader
  blending on the owning format, blend-factor caps, `MaxPixelShader30InstructionSlots` against a
  conservative count of the embedded programs, one `D3DSBT_ALL` block, five `DrawPrimitiveUP` quads.
  Cross-compiled with the SSE2/four-byte-stack policy; native Windows execution unverified. The
  fixture's `FP16_STORE` probe shows the Preview backend truncates FP16 render-target stores; the
  multiply law matches the CPU law within one FP16 ulp (bit-exact wherever the factor is 1), and the
  term stores occlusion so an unoccluded pixel is exactly 0 under either rounding mode. Step 2 (the
  scene-end hook, `--ambient-occlusion`) adds `GetRenderTarget`/`GetContainer` on the owning target and,
  in timing mode only, `CreateQuery` for `TIMESTAMPDISJOINT`/`TIMESTAMPFREQ`/`TIMESTAMP` polled with
  `D3DGETDATA_FLUSH`; a refused query type falls back to CPU wall time (the Preview backend refuses
  them; native drivers generally provide them, unverified here).
- The volumetric fog pass (`src/renderer/fog_pass.cpp`, `--volumetric-fog`, 2026-09-19, default off) uses
  documented D3D9 only: caps fields (shader versions, `MaxPixelShader30InstructionSlots` against 224 slots,
  `SRCALPHA`/`INVSRCALPHA`, `D3DDEVCAPS2_CAN_STRETCHRECT_FROM_TEXTURES`), `CheckDeviceFormat` for an A8R8G8B8
  target, an FP16 target with post-pixel-shader blending and R32F textures, one equal-size RT-to-RT
  `StretchRect` with `D3DTEXF_NONE` inside the scene, `texldl` inside ps_3_0 loops and branches, one
  `D3DSBT_ALL` block. The sector rule observes the engine's `nebulafog` pixel program by hash (no private
  layout, no executable gate); the sun colour comes from the game-private light node the sun poll already
  reads, with a neutral fallback when the poll is unavailable. Cross-compiled with the SSE2/four-byte-stack
  policy; native Windows execution unverified (`docs/verification/volumetric-fog.md`).

- **Cull census** (`--cull-census`, 2026-09-18): two read-only `engine_patch`
  trampolines on the cull/LOD pass, gated on the same EXE hash as the other
  patches; qualified by the site verifier and the CPU fixture under CrossOver
  only. Native Windows: source-compatible, unverified.

## 2026-09-19: `--taa-current-filter` exceeds the guaranteed ps_3_0 slot count

The filtered TAA resolve variant (`src/temporal/resolve_filter.hlsl`, default off) compiles to 521 instruction
slots; ps_3_0 guarantees 512 (`MaxPixelShader30InstructionSlots` may advertise more). Creation is the capability
test: a device that refuses it keeps the plain resolve and logs `motion_output_taa_current_filter unavailable=1`.
The X3 bottle accepts it. The refusal path is fixture-simulated only; no device enforcing the cap has run it.
Before this option can become a default, trim the variant under 512 slots (9 of its 14 added slots).

**Closed 2026-09-19** (`taa-flicker-suppression.md`, step 0): the mask-snapshot modes moved into their own program
(`src/temporal/resolve_snapshot.hlsl`, 46 slots); the plain resolve is 433 slots and the filtered variant 444. Every
embedded resolve program is within 512 (thin 468, thin + filter 480, age 494, age + filter 506; the fixture's
`RESOLVE_BUDGET variant=embedded_*` lines assert it). The refusal path stays as the capability test. The age variants
write `COLOR1` (R32F beside A16B16G16R16F): `TemporalPass` requires `NumSimultaneousRTs >= 2` and
`D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS` and otherwise runs without the adaptive weight
(`motion_output_taa_adaptive_weight unavailable=1`); the fallback is source-reviewed only, no device without the cap has
run it. Native Windows: source-compatible, unverified.


## 2026-09-20: fog-card replacement

`--volumetric-fog-cards replace` uses documented D3D9 colour-write masks and
stream-frequency state, forwarding the native draw and HRESULT. A fresh native
GetStreamSourceFreq on an eligible card rejects instancing; replacement enables
no global setter hooks. Checked per-draw state getters or the existing hooked
cache provide the remaining admission state. No
backend-private layout or export is required. Cross-compilation and X3-bottle
mask/readiness/Reset fixtures pass. Native Windows execution, the full live hook
chain and replacement flight quality remain unverified; see the
[volumetric-fog ledger](../verification/volumetric-fog.md).

## 2026-09-20: R7 whole-call light timer

`--light-phases` uses portable x86 instruction replay and documented Win32
QPC/thread/LastError APIs. Its read-only x87/MXCSR rounding-mode check skips
measurement before any FP mode write when the modes differ; the same fallback
applies on native Windows. No backend-private API, thread layout or DLL identity
is required. Cross-compilation and synthetic X3-bottle CPU/arithmetic fixtures
pass. Native Windows runtime behavior and actual engine-flight timing remain
unverified; see the [R7 timing ledger](../verification/sampling-profiler.md#r7-whole-call-light-phases-2026-09-20).

## 2026-09-20: collision query timing

`--collide-query-phases` uses documented QPC, thread and code-patch APIs plus
reviewed x86 game seams. No backend-private export or layout is required. Its
integer-only rounding guard bypasses timing before any FP-control write when
x87 and MXCSR rounding differ; the engine still executes unchanged. This also
avoids FEX's shared-rounding hazard without detecting a particular backend.
SSE2/four-byte-stack cross-compilation establishes source compatibility only;
native Windows hook, CPU-state and timing execution remains unverified. See
[the query-phase ledger](../verification/collide-query-phases.md) for scoped
host and CrossOver evidence, failure witnesses and diagnostic overhead.

## 2026-09-20: lattice post-route state observation

The opt-in [lattice state observer](taa-lattice-crawl.md#23-opt-in-post-route-state-observation-2026-09-20)
uses documented D3D9 getters and COM reference balancing, plus the existing
verified game object-scope hook. It calls the saved public GetRenderTarget entry
point to observe effective targets without the proxy's logical-binding shim.
No private backend API, resource payload read or in-scene RT copy is added.
Unavailable getters/scopes and ambiguous matches refuse a complete packet.
Host tests and Windows x86 cross-compilation are source evidence. The X3-bottle
actual-helper fixture passed 250 checks against synthetic public COM endpoints
(no rendering or device creation), including CPU state and reference lifetimes.
The clean integration DLL passes the linked no-x87 audit (95 roots / 539
reachable functions / zero violations). Native Windows runtime and live-game
observation remain unverified.

## 2026-09-20: spatial fog production integration

The spatial field uses the same documented D3D9 pass on Windows and CrossOver:
FP16 atlas filtering/targets, RGBA32F current linear-depth input, public native
vtable entries, state blocks plus explicit stream restoration, and legal
out-of-scene RT copying. The borrowed-open-scene transaction reconciles scene
state and poisons downstream injected writes after unrecovered scene-state loss
or failed state restoration. The validated engine-record reader selects only
the two qualified families; unknown
or unavailable authority retains native cards rather than inventing a volume.
No backend-private API/layout is a prerequisite.

Fields are regenerated with pinned NumPy at build time and embedded as RCDATA;
runtime loading uses documented Windows resource APIs. Asset host decoding,
resource cross-compilation and the integrated DLL build pass. The linked CPU
audit passes 95 roots / 540 reachable functions / zero violations. Actual
production CrossOver GPU/resource/recovery and timing fixtures now pass, as
does the synthetic-owner route/card bridge (see the [fog ledger](../verification/volumetric-fog.md#spatial-production-renderer-qualification-2026-09-20)).
Game TAA/appearance and native Windows execution remain unverified. These
CrossOver fixtures are not native Windows runtime proof.


## 2026-09-20: default-disabled media engine consumer

The [consumer checkpoint](media-playback.md#concrete-engine-consumer-checkpoint-2026-09-20)
uses documented Windows thread/stack, memory, protection and instruction-cache
APIs; `GetCurrentThreadStackLimits` has a public `VirtualQuery` fallback. Engine
addresses remain private game ABI with qualified instruction spans, not backend
layout requirements. SSE2/four-byte-stack Windows x86 compilation and 5,055 actual
emitted-code fixture checks under CrossOver/X3 pass. Fixture memory and callbacks
are authored; actual native Windows execution, game integration and general
SEH/C++ unwind across substituted return addresses remain unverified. Admission
requires the separate service/destination/startup integration described in the
[root composition](media-playback.md#qualified-startup-root-and-common-record-ingress-2026-09-20).
That integration uses the same documented APIs and exclusive CPU-only Reset
notification; its Windows x86 compilation and authored CrossOver fixtures do not
establish native Windows runtime or real game playback/Reset behavior.

## 2026-09-20: lattice observer query reference guard

The [observer guard](taa-lattice-crawl.md#27-bound-observer-reference-callbacks-and-device-lifetime-2026-09-20)
uses public D3D9 COM AddRef/Release and getter entry points, shared production
source, and existing CPU/LastError boundaries. It requires no private backend
layout, export or hash. The native device pin remains valid through submission
and cleanup; only the bounded observer query scopes suppress proxy reference
accounting/restoration, while native Release always forwards. Callback frequency
may differ by backend; both callback-free held-resource controls and actual
dropped-resource callbacks are covered by the X3/FEX matrix.

Windows x86 cross-compilation and the linked CPU audit pass. The private actual
capture/MotionOutput/helper fixture passes 830 checks under X3 arm64 Wine with
`FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`, covering lazy/per-draw routing,
Reset/recreation, actual writes, query refusal, native failure and target-device
retirement. Native Windows runtime, process-last-device profiler shutdown and
actual game selection remain unverified. This reproduces a concrete observer
interference mechanism; it does not prove historical Run193 resource ownership
or its first destructive callback. Scoped evidence and remaining limits are in
the [compact record](../../verification/results/lattice-observer-guard-2026-09-20.json).

## 2026-09-21: standalone CloneMesh capture boundary

The [manual boundary](taa-lattice-crawl.md#manual-call-boundary-checkpoint) uses
public CloneMesh and Windows exception interfaces, with a GCC x86 SJLJ-specific
two-object shell/helper contract. It reads no Wine-private layout. Cross-compiled
synthetic exception/state checks pass on X3/FEX; actual CloneMesh capture and
native Windows execution remain unqualified. It is not yet wired into the DLL.

The subsequent [actual manual upload fixture](taa-lattice-crawl.md#actual-manual-clonemesh-upload-observation-qualified)
passes 421 checks with the app-local native D3DX implementation on X3/FEX,
including Reset and exact uploaded bytes. Public module selection/hash is test
provenance only. Native Windows execution and game integration remain open.

## 2026-09-21: stored-density fog cache, worker and slab uploads

The stored-density path of `FogPass` (checkpoint 3, not yet wired into the proxy) uses
`CreateTexture` (SYSTEMMEM and DEFAULT `A16B16G16R16F`), `LockRect` with
`D3DLOCK_NO_DIRTY_UPDATE`, `UpdateSurface` with explicit rectangles, `CreatePixelShader`
and the capabilities `attach` already queries, plus `MaxPixelShader30InstructionSlots >= 512`;
the worker is a C++ `std::thread` with `SetThreadPriority` and no D3D call. No Wine-private
export, layout or hash is a prerequisite, and a refusal leaves the legacy path bit-identical.
Windows x86 cross-compilation and the X3/FEX fixture (48 checks) pass; native Windows
execution, FP16 bilinear precision there and driver cost of 64 small `UpdateSurface` calls per
frame remain unverified ([fog ledger](../verification/volumetric-fog.md#stored-density-runtime-integration-checkpoint-3-cache-manager-worker-uploads-ramps-reset-2026-09-21)).

## 2026-09-21: stored-density fog in the proxy (threads, process exit)

`--volumetric-fog-range stored` (default `legacy`) adds the first `std::thread` to `d3d9.dll`:
MinGW's winpthreads, statically linked, plus `SetThreadPriority`, `GetModuleHandleExW`
(`PIN | FROM_ADDRESS`, when the first worker starts) and QPC. Nothing else is new on the D3D side
(see the entry above). Process exit is handled with documented loader rules only: `DllMain`
`DLL_PROCESS_DETACH` abandons the workers before the CRT's static destructors, without joining,
notifying, locking or logging, because the OS has already ended every other thread. The order
(user `DllMain`, then the module's static destructors) is the mingw-w64 CRT's; the exit fixture
shows it, and shows the unfixed teardown hanging in the static destructor, on X3/FEX. An MSVC
build would need the same order confirmed (its CRT also calls `DllMain` before `_CRT_INIT`
detach). Open: `DisableThreadLibraryCalls` in `DllMain` may keep winpthreads' TLS callback from
seeing the worker's exit (at worst a small per-thread leak; not verified); `std::mutex` /
`std::condition_variable` behaviour of winpthreads on native Windows is unexecuted; native
execution of the whole path remains unverified
([fog ledger](../verification/volumetric-fog.md#stored-density-runtime-integration-checkpoint-4-proxy-wiring-launcher-option-lifetime-2026-09-21)).
