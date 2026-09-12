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

The live motion route is opt-in and diagnostic: `--motion-output` (env
`X3M_MOTION_OUTPUT=1`) draws the reviewed material pair through transformed
variants into a private RGBA32F RT1 and writes it back in capture frames. Object
history needs `--object-trace --object-lifetime`; without them the route runs in
sentinel-only mode. `--taa` (env `X3M_TAA=1`, requires `--motion-output` with
both history options, implies `--motion-jitter`) runs the temporal resolve at the game's pre-bloom
copy and presents the resolved image; `--taa-debug` writes the resolved FP16
image in capture frames. This is the first TAA that reaches the screen; it is
verified synthetically, not yet in gameplay. `--hdr` (env `X3M_HDR=1`,
requires `--motion-output`) is the FP16 HDR scene path: the scene renders
into an owned FP16 target and is written back into the game's 8-bit target.
Alone it is stage 1, an identity write-back that leaves the picture unchanged
(to within one 8-bit code) while the topology is exercised. `--hdr-tonemap`
(env `X3M_HDR_TONEMAP=agx`) is stage 2: the write-back becomes the AgX
tonemap of the FP16 scene with auto exposure (a GPU log-luminance meter, host
adaptation), the looks `--hdr-look none|golden|punchy`, the decode
`--hdr-decode gamma2.2|srgb|none`, `--hdr-ev` (offset), `--hdr-ev-manual`
(fixed EV) and `--hdr-clamp`. Honest scope: the presented image is AgX
tonemapped from a gamma-space FP16 scene (the decode is a documented
approximation) and is still LDR to the game's bloom and GUI; verified
against the Python reference synthetically, not in gameplay. See
[live motion route](docs/architecture/live-motion-route.md),
[temporal integration](docs/architecture/temporal-integration.md) and the exact
gameplay commands in
[motion-output verification](docs/verification/motion-output.md#gameplay-diagnostic-run).
`launch --dry-run` validates the options and prints the resolved command and
`X3M_*` environment without starting the game.

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
