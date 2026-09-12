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
  forwarders with `ret N` fallbacks (0/12/20/4 bytes) when the backend lacks
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
