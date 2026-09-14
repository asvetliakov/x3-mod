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

## Current gaps

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
- The ambient occlusion pass (`src/renderer/ambient_occlusion_pass.cpp`, step 1, detached) uses
  documented D3D9 only: `CheckDeviceFormat` gates for the R32F/R16F render targets and post-pixel-shader
  blending on the owning format, blend-factor caps, `MaxPixelShader30InstructionSlots` against a
  conservative count of the embedded programs, one `D3DSBT_ALL` block, five `DrawPrimitiveUP` quads.
  Cross-compiled with the SSE2/four-byte-stack policy; native Windows execution unverified. The
  Preview backend truncates FP16 render-target stores (fixture: the multiply law is bit-exact under a
  truncating model, one ulp under round-to-nearest); the term stores occlusion so 0 is exact either way.
