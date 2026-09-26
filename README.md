# X3 Modern Renderer (x3-modern)

A renderer and engine mod for **X3: Albion Prelude**. It is a 32-bit Direct3D 9
proxy, `d3d9.dll`, placed next to `X3AP.exe`: the game loads it instead of the
system `d3d9.dll`, and the proxy forwards to the real Direct3D 9 while adding its
own passes and a small set of byte-verified patches to the game executable in
memory. Game files (`X3AP.exe`, `.cat`/`.dat` archives) are never modified on disk.

It is developed and flown under **CrossOver (Wine) on macOS**, on Apple silicon
through FEX. **Native Windows** is a required target: the production code uses
documented Direct3D 9 and Win32 behaviour only and cross-compiles for Windows, but
it has not been run on a Windows PC yet
([portability gaps](docs/architecture/platform-portability.md)).

## What it does

| Area | What the mod adds |
| --- | --- |
| Anti-aliasing | Temporal anti-aliasing (TAA) from per-draw motion written by the game's own transformed shaders, camera reprojection, a resolve at the engine's scene end and a light sharpen afterwards |
| HDR look | The scene renders into an FP16 target; auto exposure and an AgX tonemap write it back to the display (SDR output); bloom from the HDR scene; bright emitters (engines, bolts, hull lights) above 1.0 |
| Shadows | Sun shadows for ships and stations from five cascades, with caster retention and alpha-tested casters |
| Fog | Volumetric nebula fog with a stored range, replacing the game's flat fog cards, with light shafts and dust motes |
| Sun | Partial sun occlusion: the flare fades with the visible part of the disc instead of popping; a fix for the flare vanishing near the centre of wide displays |
| Distant objects | A merged-LOD overlay (one draw instead of dozens for distant ships and stations), Terran stations choosing their LOD by screen size, and station occlusion kept on the coarse models |
| Camera | A field-of-view remap in X4-style units (default 90), a raised third-person chase camera with the HUD reticle at the true forward point, and the chase view restored after a gate jump |
| Media and sound | Speech through a process-local WMA decoder, sector music kept across alt-tab, save and pause, and media stability (the ID2 video skip) |
| Loading and input | Faster loading (signature-check cache, archive reader, catalogue handles, mesh adjacency, savegame read-ahead), the flight pause ended only by the Pause key or a click, and a borderless window placed over the macOS menu bar |

The acceptance state of each goal is in [goals](docs/goals.md). The installed build
and what is still open are in [status](docs/status.md).

## Install (player)

1. Quit the game. Unpack the release zip into the game folder, the folder with
   `X3AP.exe` (with CrossOver:
   `~/Library/Application Support/CrossOver/Bottles/<bottle>/drive_c/X3`). The zip
   holds `d3d9.dll`, `x3m.ini`, `x3m-regenerate.exe` (Windows), `x3m-regenerate`
   (macOS) and `README.txt`.
2. Run `x3m-regenerate` once from that folder. It rebuilds the data that depends on
   the installed game and mods: the fog colours of nebula types the renderer does not
   know and the merged-LOD overlay. It asks nothing and ends with `all done`. Run it
   again after installing, updating or removing a mod
   ([user guide](docs/user/regenerate.md)).
3. Start the game as usual. Under CrossOver, Wine prefers its built-in `d3d9` unless
   the bottle's DLL override for `d3d9` selects the native one; the developer
   launcher below sets that override for its own launch only.

**Updating:** let `d3d9.dll`, `x3m-regenerate` and `README.txt` overwrite the old
ones, but keep an `x3m.ini` you edited and compare it with the new one for new
settings. An older file keeps working: missing settings take their defaults,
settings the new version no longer has are ignored and named in `x3m.log`.

**Uninstall:** delete `d3d9.dll`, `x3m.ini`, `x3m-regenerate*`, `x3m-regenerate.log`,
`x3m.log` and `x3m.prev.log`, and what `x3m-regenerate` wrote (the `x3m` folder, the
overlay catalogues beside an `addon/NN.x3m-lod.json` marker, and
`addon/mods/<mod>-x3m-lod.*`); the exact list is in the
[regenerate guide](docs/user/regenerate.md#removing-the-results).

The release zip is built by `tools/release/package.py --dll build/d3d9.dll --out DIR`;
it refuses a stale settings template.

## Configure

All settings live in `x3m.ini` next to `d3d9.dll`. Every player-facing key is in the
shipped template, commented out and showing the value the mod uses anyway, each with
a short explanation; the mod works the same without the file. Remove the `;` in
front of a key to change it, restart the game to apply. The mod only reads the file.
See [settings](docs/user/config.md) for the grammar and the log rows (`config_open`,
`config_key`, `proxy_options`) that show what was loaded and from where.

**Logs.** The mod writes `x3m.log` next to `d3d9.dll`; the previous launch's log is
kept as `x3m.prev.log` (a second instance, or a file another program holds, gets
`x3m-<pid>.log`). When the game folder cannot be written, the log goes to
`%LOCALAPPDATA%\x3-modern-renderer\x3m.log`; the first row, `log_open`, names the
file taken. Without options the log carries only a bounded always tier (session
header, errors, one `frame_end` row a minute, `session_end`; about 0.2 MB per hour).
`debug = 1` (`--debug` in the launcher) adds the rendering diagnostics and enables
the F8 frame capture, about 1.7 GB per hour at 60 fps; `perf = 1` (`--perf`) adds the
performance rows, about 0.36 GB per hour ([logging tiers](docs/architecture/logging-tiers.md)).

**Bug reports:** set `debug = 1`, reproduce, quit the game, and send `x3m.log` (plus
`x3m.prev.log` if the game was started again since, and `x3m.ini` if you changed it).
Set `debug` back afterwards.

## Developer launcher

`tools/manage.py` installs the built DLL into the bottle and launches the game with a
process-local `d3d9` override; it never changes the EXE, archives, registry or bottle
configuration, and refuses to overwrite a DLL it does not own.

```sh
python3 tools/manage.py install              # build/d3d9.dll into the X3 bottle (keeps one previous for rollback)
python3 tools/manage.py launch --dry-run     # validate options, print the command and X3M_* environment
./x3run --direct --debug --perf              # launch through the Wine lock, then snapshot the session evidence
python3 tools/manage.py launch --vanilla     # the built-in d3d9, no proxy
python3 tools/manage.py status | rollback | uninstall
```

`x3run` wraps `manage.py launch --bottle X3` in `verification/probe/wine_lock.py`
and copies the session log afterwards (`tools/analysis/snapshot_x3_run.py`). A launch
without options turns on the accepted feature set; each default has an opt-out
(`--no-taa`, `--no-volumetric-fog`, `--camera vanilla`, ...). By default the launcher
sends `X3M_CONFIG=bare`, so the proxy reads no file and a flight depends only on the
options given. `--config [PATH]` is **player mode**: the proxy uses its built-in
defaults and the file (the `x3m.ini` next to the DLL, or PATH), plus only the options
given explicitly. Every option, its default, opt-out and verdict is in the
[launcher option inventory](docs/verification/launcher-options-inventory.md).

The agents working on this repository never launch the game; flights are run by the
user and recorded in the [run queue](docs/verification/user-runs.md).

## Build

Requirements: CMake, the i686 MinGW-w64 cross compiler, and Python 3 with NumPy
2.0.2 (configuration stops with an error when the given interpreter cannot import
it; nothing is installed for you).

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DPython3_EXECUTABLE=/absolute/path/to/python3
cmake --build build -j4
```

The result is `build/d3d9.dll`, statically linked against the MinGW C++ and thread
runtime. CPU arithmetic uses SSE2 (`-msse2 -mfpmath=sse`) with
`-mstackrealign -mincoming-stack-boundary=2`, because the game's callbacks arrive on
a four-byte-aligned stack.

The settings schema `tools/config/schema.py` generates `src/config/config_schema_inc.h`
and the template `assets/x3m.ini`. After changing the schema run
`python3 tools/config/generate.py`; `python3 tools/config/generate.py --check` fails
when a generated file is stale. `x3m-regenerate` is built separately with
`python3 tools/regenerate/build.py` (`--windows` for the `.exe`;
[details](docs/user/regenerate.md#building-the-executable)).

## Verification

- **Host suite** (pure Python and host C++ harnesses, no Wine):

  ```sh
  /usr/bin/python3 verification/probe/run_host_suite.py
  ```

  It runs the discovered modules in parallel and reproduces, module for module,
  `PYTHONPATH=verification/probe python3 -m unittest discover -s verification/analysis -p 'test_*.py'`
  ([host suite](docs/verification/host-suite.md)). A single module runs with the same
  `PYTHONPATH` and `python3 -m unittest <module>`.
- **Wine fixtures** (`verification/probe/run_*.py` and fixture executables) run the
  proxy against real Direct3D 9 under CrossOver in the X3 bottle. Every Wine command
  goes through the lock, one at a time:

  ```sh
  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py <command...>
  ```

  The lock serialises on `/tmp/x3-wine-runner.lock` and refuses while the game or
  another runner is up ([bottles](docs/verification/bottles.md)).
- **Install candidates** are built once from committed, reviewed sources, qualified,
  and bound to their commit, DLL hash and results in `verification/results/`.

The binding project rules (targets, hook validation, verification scope, Wine lock,
documentation) are in [AGENTS.md](AGENTS.md); Claude Code routing is in
[CLAUDE.md](CLAUDE.md).

## Documentation map

| Where | What |
| --- | --- |
| [docs/status.md](docs/status.md) | The single current-state file: installed build, rollback, what main carries, open items |
| [docs/goals.md](docs/goals.md) | The goal list and its acceptance evidence |
| [docs/user/](docs/user/) | Player guides: [settings](docs/user/config.md), [x3m-regenerate](docs/user/regenerate.md) |
| [docs/architecture/](docs/architecture/) | Design and architecture notes, one per subsystem (for example [temporal integration](docs/architecture/temporal-integration.md), [HDR scene path](docs/architecture/hdr-scene-path.md), [volumetric fog](docs/architecture/volumetric-fog.md), [config file](docs/architecture/config-file.md), [platform portability](docs/architecture/platform-portability.md)) |
| [docs/reverse-engineering/](docs/reverse-engineering/) | Findings about the game executable: hook sites, engine structures, loading, camera, media |
| [docs/verification/](docs/verification/) | One ledger per feature (for example [temporal resolve](docs/verification/temporal-resolve.md), [directional shadows](docs/verification/directional-shadows.md)), the [option inventory](docs/verification/launcher-options-inventory.md) and the [run queue](docs/verification/user-runs.md) |
| [docs/archive/](docs/archive/) | Handoffs, status history, completed runs, the closed iteration and review series, and notes for dropped features |
| `verification/` | Probes, fixtures, host tests and compact results |
