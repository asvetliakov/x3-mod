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
- The temporal resolve at the bloom copy (`X3M_TAA=1`) converts the 8-bit
  main target to FP16 and back with `StretchRect`. Native D3D9 grants that
  conversion only where the driver reports it, so the route's attach gate asks
  `CheckDeviceFormatConversion` for A8R8G8B8/X8R8G8B8 to and from
  A16B16G16R16F (`taa_reason=format_conversion` otherwise); on Wine the query
  accepts everything and the fixtures prove the conversion, so native
  behavior remains unverified. The pass's cached `D3DSBT_ALL` state block also
  holds references to the application objects bound at the copy until the
  next frame's capture (released before Reset); on native D3D those keep the
  device count above the final-release probe until the block is dropped, on
  Wine the application-level references are independent of it.
- The FP16 HDR scene path (`X3M_HDR=1`, stage 1) needs an `A16B16G16R16F`
  render-target texture with post-pixel-shader blending and sampling, the
  three-format independent-bit-depth MRT (FP16 + RGBA32F + R32F) and, for the
  emergency unwind rung only, `CheckDeviceFormatConversion(A16B16G16R16F →
  A8R8G8B8)`; all are queried at attach through the documented caps and a
  live self test, and the feature disables itself otherwise. Native drivers
  are untested against this stack ([hdr-scene-path.md](hdr-scene-path.md) §5).
- Current CrossOver fixtures do not establish native-Windows rendering, reset,
  multithreading, presentation or performance. Native-Windows verification remains
  outstanding; no successful Windows run is claimed.

Game EXE/DLL private structures and code hooks remain allowed. Use disassembly
where needed and validate the targeted game ABI/layout; this permission is
separate from avoiding dependencies on private graphics-runtime implementations.

For each new component, review dependencies and capability gates alongside code
correctness and performance. Preserve separate evidence for CPU-only behavior,
Windows builds, CrossOver runtime tests and eventual native-Windows runtime tests.

See the [runtime dependency and interception audit](runtime-dependencies.md) for
concrete remaining gates, removal status and the separate depth-adapter gap.
