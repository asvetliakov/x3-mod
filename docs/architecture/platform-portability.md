# Native Windows and CrossOver support

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
`X3M_*` variable of the process environment, sorted, and nothing else. The digest
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
  MRT, state and shader APIs, with portable G32R32F-to-R32F shader copying.
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
