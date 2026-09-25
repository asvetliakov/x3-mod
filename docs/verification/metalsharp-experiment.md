# MetalSharp experiment: X3AP with the x3m proxy under MetalSharp's Wine

Prepared 2026-09-25 (nothing launched). Purpose: let the user launch the modded
game from `/Applications/MetalSharp.app` on two routes and compare against the
CrossOver X3 bottle. Preparation script: `tools/metalsharp/prepare_metalsharp.py`
(`--check` verifies, `--remove` undoes). Wrapper: `tools/metalsharp/x3m_launch.c`.

## What MetalSharp is (measured from the bundle and `~/.metalsharp`)

- MetalSharp 0.73.0 (`Info.plist`), an Electron front end (`app.asar`) over an
  arm64 HTTP backend `~/.metalsharp/runtime/metalsharp-backend`; runtime assets
  are unpacked from the bundle's `Contents/Resources/bundles/*.tar.zst` into
  `~/.metalsharp/runtime/` (3.8 GB).
- Wine: `wine-11.17 (MetalSharp 1.0)` (`bin/wine --version`), a WineForge
  build (licenses `WINEFORGE-*`). `bin/wine` and `bin/wineserver` are **x86_64
  Mach-O**: on Apple silicon they run under **Rosetta 2** (the backend checks
  `/usr/libexec/rosetta/oahd`, sets `ROSETTA_ADVERTISE_AVX`; `ntdll.so` carries
  `ROSETTA_X87_PATH: attaching x87sidecar --cooperative`; `bin/x87sidecar` is an
  arm64 helper that supplies x87 precision under Rosetta, the counterpart of
  CrossOver's `FEX_X87REDUCEDPRECISION=1`). No FEX or Box64 in the bundle.
  32-bit code runs in **wow64** mode inside one 64-bit prefix
  (`~/.metalsharp/prefix-steam`, 859 files in `syswow64`); `WINEARCH=win32`
  is refused by this ntdll.
- Graphics stacks shipped: DXMT (`lib/dxmt`, manifest
  `0.73.0-dxmt-v0.80-baseline-v1`; i386 set = d3d10core, d3d11, dxgi, nvngx,
  winemetal, **no d3d9.dll**), MoltenVK 1.4.1 (`lib/moltenvk-vkmt`; a
  `VkInstance` on "Apple M5 Pro" was created even for the wrapper test below),
  dxvk-1.10.3 + vkd3d-proton (`~/.metalsharp/vkd3d`), GPTK 4 beta 2
  (`runtime/d3dmetal-gptk4-beta2`), Wine's own `wined3d.dll` (27 MB, imports
  opengl32, vulkan-1, winevulkan; Metal shader strings inside).
- Game registry: `~/.metalsharp/sharp-library/library.json`, one JSON object per
  entry (`id`, `name`, `exe_path`, `install_dir`, `engine`, `launch_args`,
  `user_launch_args`, `bottle_id`, ...). Engine ids in the UI: `d3dmetal`,
  `vkd3d`, `d3d9`, `dxmt`, `dxmt_32`, `fna_arm64` (labels D3DMetal, VKD3D,
  D3D9, DXMT, DXMT(32), Mono/FNA; the internal `m9`/`dxvk`/`dxvk_32` also
  display as "D3D9"). An existing directory is used in place: the UI's
  "Install Windows Program" takes an `.exe`/`.msi` path and stores it; nothing
  is copied (the user's two earlier entries point at `.../Bottles/Steam/drive_c/X3/X3AP.exe`
  and a non-existent `/Users/asvetl/X3/X3AP.exe`; both were left untouched).
- No per-game environment editor: `user_launch_args` exists in the schema but
  the renderer has no editor for Sharp Library entries (one reference, in the
  Steam-bottle launch path). Hence the wrapper executable below.
- Launch shapes (the backend's embedded catalog; the same shapes serve the
  Sharp Library engines, inferred from the shared labels):
  - `d3d9` (M9): deploys **Wine's own `d3d9.dll`** (`lib/wine/i386-windows`
    for a 32-bit exe) beside the registered executable, backing the existing
    file up (`%s.metalsharp-backup` / `%s.metalsharp-original` strings; docs:
    "backed up and restored when the mode is switched off"), and sets
    `WINEDLLOVERRIDES=d3d9=n,b;gameoverlayrenderer,gameoverlayrenderer64=d`.
    D3D9 is therefore **not** translated by DXMT on any route: it is wined3d
    (Vulkan/MoltenVK or GL) with the x87 sidecar. The website's "DXMT: D3D9 to
    Metal" wording is not what the shapes do.
  - `dxmt_32` (M11(32)): deploys 32-bit `d3d11.dll`, `dxgi.dll`,
    `dxgi_dxmt.dll`, `d3d10core.dll`, `winemetal.dll` from `lib/dxmt/i386-windows`
    beside the executable and sets `WINEDLLOVERRIDES=d3d11,dxgi,winemetal=n,b;...`.
    `d3d9` is not named, so a D3D9 game falls to Wine's default load order
    (native in the application directory first), i.e. the proxy, then wined3d.
    A D3D9 game gets nothing from DXMT on this route; it is a wined3d run with
    DXMT's environment (`DXMT_*` cache variables) and the 32-bit deployment.
- Logs: app log `~/.metalsharp/logs/<date>.log`; per-launch process output
  `launch-<id>-<timestamp>.log` (backend format string; directory
  `~/.metalsharp/logs/` or `~/.metalsharp/bottles/<id>/logs/`, inferred) and the
  Logs page in the app (live log, crash reports, recent files); DXMT graphics
  logs are opt-in (`METALSHARP_GRAPHICS_RUNTIME_LOGS`, Settings toggle).

## How it was configured

Nothing was written into the CrossOver bottle. MetalSharp deploys DLLs beside
the *registered* executable, so the registered executable is a wrapper outside
the game directory: MetalSharp's deployments (and, on the D3D9 route, its
replacement `d3d9.dll`) land in the wrapper's directory, while the game process
still loads the proxy from `drive_c/X3` (native first, `d3d9=n,b`).

Per route `~/.metalsharp/x3m/<dxmt32|d3d9>/`:

| File | Content |
| --- | --- |
| `x3m-launch.exe` | 32-bit PE (i686-w64-mingw32-gcc, 142,503 B) from `tools/metalsharp/x3m_launch.c`: reads `.cfg` and `.env`, sets the 181 variables, appends `;d3d9=n,b` to the inherited `WINEDLLOVERRIDES`, starts `X3AP.exe -noabout -skipintro -runinbg` with cwd `Z:\...\Bottles\X3\drive_c\X3`, waits, returns the game's exit code, writes `x3m-launch.log` beside itself |
| `x3m-launch.env` | the environment of `manage.py launch --dry-run --bottle X3 --direct --telemetry --camera-log 1` (179 `X3M_*` + 2 `GST_*`) |
| `x3m-launch.cfg` | `dir=` / `exe=` / `args=` |
| `x3m-launch.cmd` | the same as a batch file (manual `wine cmd /c` alternative; not registered, because the backend picks its 32/64-bit deployment from the PE header of the registered file) |

`~/.metalsharp/sharp-library/library.json`: two entries appended
(`sharp_1790400000001` "X3AP x3m proxy [DXMT(32)]", engine `dxmt_32`;
`sharp_1790400000002` "X3AP x3m proxy [D3D9]", engine `d3d9`), `bottle_id`
null like the user's entries (so the shared wow64 `prefix-steam` is used,
inferred from `metalsharp-wine`'s default and the wrapper test). Backup of the
previous file: `library.json.pre-x3m`. Undo everything:
`python3 tools/metalsharp/prepare_metalsharp.py --remove` (or restore the
backup and delete `~/.metalsharp/x3m/`).

## Verification without launching (measured)

- `prepare_metalsharp.py --check`: both exes PE machine 0x14c (i386), env 181
  vars / 179 `X3M_*`, both library entries present with the right engine and
  path, `drive_c/X3/d3d9.dll` sha256 `94c4ef42…f90081` (Run87) unchanged.
- Wrapper exercised under MetalSharp's own Wine with a copy whose `.cfg` points
  at `C:\windows\syswow64\cmd.exe /c exit 7` (no game): 5.2 s wall,
  exit code 7 propagated, 181 variables set, `WINEDLLOVERRIDES` became
  `d3d11,dxgi,winemetal=n,b;d3d9=n,b`, 23 inherited `WINE*`/`MS_*` variables
  logged (`WINEPREFIX=/Users/asvetl/.metalsharp/prefix-steam`, `WINEDLLDIR2=...\i386-windows`).
- No wine process left running afterwards; the game directory's newest entry
  is still the 18:42 regenerate exe (nothing added).

## Click path for the user

1. Open `/Applications/MetalSharp.app`, tab **Sharp Library**.
2. Entry **X3AP x3m proxy [DXMT(32)]**: **Play** (route stored as DXMT(32);
   the launch-mode dropdown shows it and can force another route).
3. Entry **X3AP x3m proxy [D3D9]**: **Play** for the wined3d/x87-sidecar route.
4. Do not launch the user's two older entries (they start the Steam-bottle
   game, or fail on the missing `/Users/asvetl/X3`).
5. Afterwards read `~/.metalsharp/x3m/<route>/x3m-launch.log` (what was set,
   game pid, exit code), the MetalSharp Logs page, and the proxy log
   `drive_c/X3/x3-modern-captures/session-<date>-<pid>.log`.

## What to look for in the proxy log

- `proxy_identity` / `proxy_options` header (the 36 promoted defaults present,
  `telemetry` on, `camera_log 1`).
- `fog_families ...` (14 compiled families loaded from `x3m/fog-families.bin`).
- `capture_caps result=... streams=... vs_float_count=... ps_version=...` at
  device creation, and the `motion_output_taa ... ps30_slots=<n>` row
  (`MaxPixelShader30InstructionSlots`; CrossOver/wined3d reports 512; the value
  is logged, never a gate). fp16 blending, MRT count and R32F support show up
  as the TAA/HDR path's `initialize=<hr>` and the `taa_*_setting` rows: a
  non-zero `initialize` or missing `taa_*` rows means the format checks failed.
- `mesh_adjacency_config`, `window_mode`, `frame_timing` rows as in CrossOver
  runs.
- The wrapper log confirms the inherited `DXMT_*`/`METALSHARP_*` variables and
  the final `WINEDLLOVERRIDES`; if `d3d9=n,b` is missing there, the proxy did
  not load.

## What to compare against CrossOver (same 5120x1440, same save)

- Frame time at the fog-band plants stand and the lattice stand (the
  `frame_timing` rows; not `--gpu-sync-timing`), loading time to the sector,
  and the look: TAA stability on the lattice, plant sparkles, fog bands, sun
  occlusion.
- Whether the proxy loaded at all under MetalSharp's wined3d (session log
  present, `capture_caps` row) and whether wined3d there runs on Vulkan/MoltenVK
  or GL (`WINEDEBUG` is `-all` by default; the Logs page's DXMT/graphics opt-in
  may show it; the registry key `HKCU\Software\Wine\Direct3D\renderer` in
  `prefix-steam/user.reg` is absent, so wined3d's default applies).

## Known limitations

- DXMT(32) is D3D10/D3D11 only; the shipped i386 DXMT set has no `d3d9.dll`.
  Under that route X3AP is a wined3d run (Wine's d3d9 -> wined3d -> MoltenVK or
  GL) with the proxy in front; failure modes are the same as vanilla Wine's
  d3d9, not DXMT's. The "D3D9" route is the same renderer with MetalSharp's own
  `d3d9.dll` copy deployed beside the wrapper (ignored by the game process).
- The voice decoder (`GST_PLUGIN_PATH_1_0` / `GST_REGISTRY_1_0` point at arm64
  GStreamer plugins) cannot load inside an x86_64 Rosetta Wine; expect no
  modded speech under MetalSharp.
- The proxy log and captures are written by the game into
  `drive_c/X3/x3-modern-captures/` (the game directory, as under CrossOver);
  MetalSharp's `.metalsharp-*` backups and deployed DLLs stay in
  `~/.metalsharp/x3m/<route>/`.
- Rosetta wow64 versus CrossOver's arm64 Wine with FEX: CPU-side timings are
  not comparable one to one; GPU-side (wined3d/MoltenVK versus CrossOver's
  D3DMetal/wined3d) is the interesting figure.
- The launch itself (bottle preparation, DLL deployment, whether the backend
  accepts a non-game PE as the executable) is untested by design.
