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

## Current gaps

- The finite-position observer's managed-buffer qualifier validates exact
  CrossOver Preview D3D9/WineD3D binaries, mappings and private layouts. It is a
  backend-specific evidence producer, not a native-Windows implementation.
  Native Windows needs equivalent geometry evidence through a separately
  qualified producer before this gate can support required motion/TAA there.
- The proposed use of WineD3D's internal graphics mutex is backend research only.
  It is not the shared replay-exclusion design. Continue a portable ownership/call
  admission protocol that does not depend on backend-private synchronization.
- macOS HDR/window presentation research is platform-specific by nature. A
  native-Windows presentation path must supply the corresponding HDR and window
  behavior through documented Windows graphics interfaces.
- Current CrossOver fixtures do not establish native-Windows rendering, reset,
  multithreading, presentation or performance. Native-Windows verification remains
  outstanding; no successful Windows run is claimed.

For each new component, review dependencies and capability gates alongside code
correctness and performance. Preserve separate evidence for CPU-only behavior,
Windows builds, CrossOver runtime tests and eventual native-Windows runtime tests.
