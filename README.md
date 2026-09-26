# X3 Modern Renderer

Experimental renderer modernization for X3: Albion Prelude in the **X3** bottle
of **CrossOver Preview**. The reversible 32-bit D3D9 proxy implements TAA and
AgX; FP16 scene rendering, materials and bloom remain under active development.
True HDR display output and whole-scene linear lighting are incomplete. See
[current status](docs/status.md) and [acceptance goals](docs/goals.md) for the
tested state and remaining work, including native Windows qualification.

## Build

Requires CMake, the i686 MinGW-w64 compiler, and Python 3 with NumPy 2.0.2.
Prepare an interpreter with that exact NumPy version, verify it with
`/absolute/path/to/python3 -c 'import numpy; print(numpy.__version__)'`, and pass
its absolute path to CMake. Configuration stops with an actionable error when
the selected interpreter cannot import NumPy 2.0.2; it never installs packages
or silently selects a different Python.

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DPython3_EXECUTABLE=/absolute/path/to/python3
cmake --build build -j4
```

The resulting `build/d3d9.dll` statically links the MinGW C++/thread runtime.
Replacement media playback is retired; building and launching require no LAV
SDK or provider package. Managed installation removes an active media selection
while retaining the previous proxy, manifest and provider files for rollback.

## Install and run

```sh
python3 tools/manage.py install
python3 tools/manage.py launch
```

Since 2026-09-25 a modded launch turns on the accepted feature set by itself: TAA with HDR, AgX and bloom, the
sun shadows with five cascades, the stored-range fog, the chase camera, the loading fast paths, music keep and the
direct start; the telemetry and capture diagnostics stay opt-in. Each default has an opt-out (`--no-taa`,
`--no-volumetric-fog`, `--camera vanilla`, ...); the list is in the
[launcher inventory](docs/verification/launcher-options-inventory.md#defaults-promoted-2026-09-25).

The launcher uses `/Applications/CrossOver Preview.app`, `Steam`, and
`drive_c/X3/X3AP.exe`. Its D3D9 override applies to that launch only. The installer
refuses to overwrite an unknown DLL; it does not modify the EXE, archives, registry,
or bottle configuration. For changing graphics settings, pass `--no-direct` to open the launcher and close it
after use. `--direct` (a default) passes X3's `-noabout -skipintro -runinbg` switches. Test in a
modest window.

The session log is `X3/x3m.log`; the previous launch's is kept as `X3/x3m.prev.log`
(and a second instance, or a file another program holds, gets `x3m-<pid>.log`).
Shader dumps and readbacks appear in `X3/x3-modern-captures/`. When the game directory
cannot be written (a read-only game directory, e.g. under `Program Files (x86)` on
Windows without Steam's ACL grant), the proxy writes the log to
`%LOCALAPPDATA%\x3-modern-renderer\x3m.log` and the captures to `...\captures`; the
first row of every log, `log_open file=<path> source=game|localappdata|override
previous=renamed|absent|busy|none session=<stamp>`, names the file taken. A launch without
logging options writes only the always tier (session header, errors, one `frame_end` row a
minute, `session_end`); `--perf` adds the performance rows and `--debug` the rendering
diagnostics (`X3M_PERF=1` / `X3M_DEBUG=1` without the launcher;
[logging tiers](docs/architecture/logging-tiers.md)). For a bug report: launch with the
tier asked for, reproduce, quit, and send `x3m.log` (and `x3m.prev.log` if the game was
started again since). Since 2026-09-25 nothing is captured automatically
(`--capture-start 999999`); **F8** captures 8 frames on demand, 300 frames after
the key press (`--capture-frames`, `--capture-delay`). Detailed capture deliberately
trades frame time for forensic completeness; expect a hitch. `--capture-start 120`
restores the automatic capture after 120 Present calls.
Capture records live queried state, including stateblock changes, rather than
assuming setter calls describe all current state.

The installed defaults are recorded in [status](docs/status.md). Both comparison
features (`--motion-output --hdr --hdr-tonemap --hdr-bloom`) are launcher defaults;
`--hdr-exposure fixed` selects fixed EV 0 instead.
During play, hold **Ctrl+Shift**, then press **F9** to switch AUTO/fixed EV 0,
or **F10** to switch bloom ON/OFF. Release the function key between presses.
A brief panel shows the effective state or an unavailable/pending request.
These controls work only while the game is foreground; **F8 is unchanged**.
See [comparison controls](docs/architecture/comparison-hotkeys.md) for capability,
exposure handoff and verification limits.

For one combined loading/render-boundary/cursor diagnostic session:

```sh
python3 tools/manage.py launch --direct --perf --capture-start 999999 --capture-frames 4
```

Additional telemetry is opt-in (`--perf`, `--debug`). Ctrl+Shift+F7 optionally marks a phase while
Present is running; F8 captures four frames with this command. See the
[single-session test steps](docs/verification/iteration-03.md) and
[coverage limits](docs/verification/telemetry.md). Timing is CPU-side elapsed
time, not GPU timing. Loading optimization and the alt-tab cursor fix remain pending.

`--gz-buffer` (env `X3M_GZ_BUFFER=1`, chunk size `--gz-buffer-kb`, default 256)
puts a read-ahead buffer in front of the savegame decoder's zlib imports without
enabling telemetry; it keeps zlib 1.2.3 semantics and logs one `gz_buffer_file`
line per file ([docs/verification/gz-buffer.md](docs/verification/gz-buffer.md)).

`--crypt-cache` (env `X3M_CRYPT_CACHE=1`, no telemetry needed) caches the
CryptoAPI provider handle and imported public key of the per-script signature
check, replacing the three `CryptAcquireContextA` container delete/create/delete
calls per script (4 ms each under Wine, 844 per save load) with one cached
handle; hash and signature verification are untouched. Reviewed game call sites
and a complete lifecycle-hook installation are required. Native handles are
retained until process exit; the scratch container can persist until the next
startup delete ([docs/verification/crypt-cache.md](docs/verification/crypt-cache.md)).

Loading-time switches ([docs/verification/loading-probes.md](docs/verification/loading-probes.md),
[docs/verification/resource-reader.md](docs/verification/resource-reader.md)):
`--debug` (env `X3M_DEBUG=1`, or `X3M_LOADING_PROBES=1` with `X3M_TELEMETRY=1`) adds the CryptoAPI /
per-open import rows and entry-counting trampolines on twelve engine loading
functions; `--resource-read fast` (env `X3M_RESOURCE_READ`; the default) replaces the
archive reader's per-kilobyte decode with one read + one inflate (the `verify` mode, which
compares against the original on every file, is fixture-only since 2026-09-26); `--dat-handles` (env `X3M_DAT_HANDLES=1`) keeps
catalogue `.dat` handles open between resources. All three are exact-executable
only and fail closed. Every mode's `frame_end` lines now carry `elapsed_ms` (since
DLL load), `dt_ms` and `qpc`, so a plain `--direct` log yields load times
(`tools/analysis/analyze_loading_profile.py`, `summarize_profile.py --frame-gaps`).

The live motion route is opt-in and diagnostic: `--motion-output` (env
`X3M_MOTION_OUTPUT=1`) draws the reviewed material pair through transformed
variants into a private RGBA32F RT1 and writes it back in capture frames. Object
history needs `--object-trace --object-lifetime`; without them the route runs in
sentinel-only mode. `--taa` (env `X3M_TAA=1`, requires `--motion-output` with
both history options, implies `--motion-jitter`) runs the temporal resolve at
the engine's scene end and presents the resolved image; `--taa-debug` writes the resolved FP16
image in capture frames. The scene end comes from the engine scene-end hook
(env `X3M_SCENE_HOOK`, on by default with `--motion-output`: a byte-verified
patch of the compositing callsite that fails closed to the game's pre-bloom
copy on any other executable; `--scene-hook off` keeps the copy boundary). This is the first TAA that reaches the screen; it is
verified synthetically, not yet in gameplay. `--hdr` (env `X3M_HDR=1`,
requires `--motion-output`) is the FP16 HDR scene path: the scene renders
into an owned FP16 target and is written back into the game's 8-bit target.
Alone it is stage 1, an identity write-back that leaves the picture unchanged
(to within one 8-bit code) while the topology is exercised. `--hdr-tonemap`
(env `X3M_HDR_TONEMAP=agx`) is stage 2: the write-back becomes the AgX
tonemap of the FP16 scene with auto exposure (a GPU log-luminance tile
meter reduced on the host to a space-aware statistic: the black sky is
excluded, the centre-weighted median of the lit tiles maps to the key, the
brightest 1 % of tiles are held under white, a dead band holds the target
against small changes; EV −3..+1.5 in production/launcher defaults (standalone components retain +2)), the looks `--hdr-look none|golden|punchy`,
the decode `--hdr-decode gamma2.2|srgb|none`, `--hdr-ev` (offset),
`--hdr-ev-manual` (fixed EV), `--hdr-clamp`, and the meter's `--hdr-meter-bg`,
`--hdr-white-target`, `--hdr-key-pull`, `--hdr-ev-deadband`,
`--hdr-edge-weight`, `--hdr-ev-min/max`. Honest scope: the presented image is AgX
tonemapped from a gamma-space FP16 scene (the decode is a documented
approximation) and is still LDR to the game's bloom and GUI; verified
against the Python reference synthetically, not in gameplay. With `--hdr`
and `--taa` together (stage 3) the temporal resolve runs on the FP16 scene
before the write-back — no 8-bit round trip, the history in engine radiance,
a reversible luminance weighting inside the resolve whose constant is the
write-back's exposure (derived only; `--taa-k` was removed 2026-09-25) —
and the presented frame is the tonemap of the resolved image.
`--taa-sharpen 0..1` (env `X3M_TAA_SHARPEN`, requires `--taa`) adds a
robust contrast-adaptive sharpen (RCAS) of the presented image only — the
history is never sharpened; 1 is the strongest setting, unset or 0 leaves
every route bit-identical to the unsharpened one; on the HDR route it runs
after the tonemap ([post-resolve sharpen](docs/architecture/temporal-integration.md#post-resolve-sharpen-2026-09-12),
numbers in [taa-sharpen.md](docs/verification/taa-sharpen.md)). See
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

After installing, updating or removing a mod, run `x3m-regenerate` from the game
directory (one bundled executable, no Python needed): it rebuilds the fog
families and the merged-LOD overlay for the installed mods
([user guide](docs/user/regenerate.md)).

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
- [Optional process-local WMA decoder adapter](docs/architecture/voice-decoder-adapter.md)
  and its [build recipe](docs/architecture/voice-decoder-recipe.md)
- [Implementation roadmap](docs/architecture/roadmap.md)
- Static findings: `docs/reverse-engineering/`
- Independent probes, tests and results: `verification/`
