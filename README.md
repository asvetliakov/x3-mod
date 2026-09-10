# X3 Modern Renderer

Experimental renderer modernization for X3: Albion Prelude running through
**CrossOver Preview**, Steam bottle. Current implementation: a reversible 32-bit
D3D9 capture proxy. **HDR, TAA and the other visual enhancements are not implemented
yet.** The [roadmap](docs/architecture/roadmap.md) defines the successive testable
iterations and acceptance criteria.

## Build

Requires CMake and the i686 MinGW-w64 compiler. On this machine they are installed.

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j4
```

The resulting `build/d3d9.dll` statically links the MinGW C++/thread runtime.

## Install and run

```sh
python3 tools/manage.py install
python3 tools/manage.py launch --direct
```

The launcher uses `/Applications/CrossOver Preview.app`, `Steam`, and
`drive_c/X3/X3AP.exe`. Its D3D9 override applies to that launch only. The installer
refuses to overwrite an unknown DLL; it does not modify the EXE, archives, registry,
or bottle configuration. For changing graphics settings, omit `--direct` to open the launcher and close it
after use. `--direct` passes X3's `-noabout -skipintro -runinbg` switches. Test in a
modest window.

Shader dumps and timestamped logs appear in `X3/x3-modern-captures/`. By default,
one detailed frame is captured after 120 Present calls. **F8** requests another
capture. Detailed capture deliberately trades frame time for forensic completeness;
expect a hitch. `--capture-start 1000` delays the automatic capture;
`--capture-frames 0` disables automatic capture (F8 still captures one frame).
Capture records live queried state, including stateblock changes, rather than
assuming setter calls describe all current state.

For one combined loading/render-boundary/cursor diagnostic session:

```sh
python3 tools/manage.py launch --direct --telemetry --capture-start 999999 --capture-frames 4
```

Additional telemetry is opt-in. Ctrl+Shift+F7 optionally marks a phase while
Present is running; F8 captures four frames with this command. See the
[single-session test steps](docs/verification/iteration-03.md) and
[coverage limits](docs/verification/telemetry.md). Timing is CPU-side elapsed
time, not GPU timing. Loading optimization and the alt-tab cursor fix remain pending.

```sh
python3 tools/manage.py status
python3 tools/manage.py launch --vanilla
python3 tools/manage.py uninstall
```

Uninstall removes only the owned proxy and its manifest. Logs are retained. Ordinary
CrossOver launching may ignore the proxy unless its DLL override selects native;
use the supplied launcher for reproducible tests.

## Scope of instrumentation

Targets X3AP's imported `Direct3DCreate9` path. Shader bytecode is FNV-1a 64 hashed
and locally dumped; a requested frame records draw order, shaders, targets, depth,
vertex declarations, textures/samplers, render states, typed shader constants and fixed
function transforms. It preserves all original COM identities by giving each
instrumented object a private vtable. No extra GPU resources or render-state changes
are introduced. Shader hashes are identifiers, not cryptographic integrity checks.

`Direct3DCreate9Ex` is forwarded without instrumentation. Additional swapchain
Present and Ex Present are not capture boundaries yet. Pure-device state queries
may fail and captures are incomplete there. Per-draw texture pixels, vertex/index
contents, buffer content revisions and patch draws are not captured yet. This is a
research proxy for this specific game, not a universal D3D9 compatibility layer.

## Documentation

- [Capture format 2 and identity semantics](docs/architecture/capture-format.md)
- [Iteration 2 verification](docs/verification/iteration-02.md)
- [Iteration 3 consolidated diagnostics](docs/verification/iteration-03.md)

- [Current status and next implementation tasks](docs/status.md)

- [Platform and HDR/backend constraints](docs/architecture/platform.md)
- [Implementation roadmap](docs/architecture/roadmap.md)
- Static findings: `docs/reverse-engineering/`
- Independent probes, tests and results: `verification/`
