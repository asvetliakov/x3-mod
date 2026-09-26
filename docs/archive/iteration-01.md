# Iteration 01 verification, 2026-09-10

## Build and independent checks

- Production x86 DLL builds with CMake/MinGW-w64 using all enabled warnings cleanly.
- PE import inspection: only Win32/UCRT imports, no companion MinGW C++/thread DLL.
- Native and proxy D3D9 smoke programs both report zero failures. Checks include
  COM/resource identity, exact shader bytes, stateblocks, drawing, Present, Reset,
  final release and a second device. Detailed frame capture is exercised.
- Seven parser tests pass: malformed input rejection, archive paths and stale DAT
  names, shader instruction/comment boundaries, padding and FNV reference vectors.
- Independent D3D9 FP16/depth and D3D11/scRGB probes succeed. HDR luminance and
  cross-API resource sharing have not been established.

## Actual game evidence

CrossOver Preview Steam bottle, X3AP 3.3 (2017), original high shader/texture
settings, glow enabled. Launcher initially showed 5120×1440 borderless with
antialiasing disabled. For testing it was changed through the launcher to
1280×768 ordinary windowed; capture reports width=1280 height=768 format=21
(A8R8G8B8), windowed=1, msaa=0, interval=1, behavior flags=0x52.

First proxy run rendered intro/loading UI, dumped 36 shaders, and captured one
complete nine-draw frame with successful Present. All 36 hashes match at least
one embedded shader in the local effect archive index. The intro frame contains
eight draws using PS `6109cf64c03529dd` / VS `7b6393fe2d3e1d85`, and one fixed
function draw. This pixel shader appears in gui2d and nebula effects, demonstrating
why an exact pixel-shader hash alone does not uniquely identify HUD rendering.
The summary is `verification/results/game-intro-capture-summary.json`.

The user exited the first game during an interruption. This was **not an observed
crash**. A subsequent unmodified-rendering baseline reached the animated 3D main
menu. A later direct proxy launch captured a complete animated-menu frame: 690 draws,
46/46 shaders matched, four identified bloom draws, and successful Present. See
[render-pass evidence](../reverse-engineering/runtime-passes.md). Flying-scene
capture remains pending; menu evidence must not be represented as that result.
Desktop automation can observe the controlled game but in-game clicks/keys do not
activate menus reliably. User help loading a flying scene and pressing F8 was
requested. Redundant direct test processes were stopped; unused launcher windows
were dismissed.

## Installation and rollback

Installed app-local `X3/d3d9.dll` with an ownership/checksum manifest. Game EXE,
CAT/DAT assets and bottle configuration are untouched. DLL selection is a
process-local launcher argument. `python3 tools/manage.py uninstall` removes only
the owned DLL/manifest and retains captures. Test window setting is a game graphics
preference and must be restored or explicitly reported at handoff.

## Completed flight capture and final build update

The user supplied two F8 flying-scene captures, frames 5449 and 5652. Each includes
122 draws and successful Present; every one of 47 recorded shaders matches local
archive metadata. See the flight section of [runtime evidence](../reverse-engineering/runtime-passes.md).
This establishes capture operation in gameplay; it does not validate a visual
renderer enhancement or automated input control.

The final build adds defensive vtable handling: preserve Ex tail slots only when
QueryInterface returns the same interface address; complete owning-map allocation
before publishing a replacement vtable. Rebuilt cleanly and reran the independent
proxy smoke with zero failures. Installed SHA-256:
`e716038d5646380b629cda2b93bf92bd030246689e7540cf1a2a521076cd5adf`.
The live game keeps the DLL version loaded at startup until its next launch.

Final checks: 12 offline parser/summary/CTAB tests pass; the compile-only ABI guard
verifies all hooked method offsets and base/Ex vtable lengths against SDK headers.
The user's test graphics setting remains 1280×768 windowed. Future non-menu game
launches/loading are coordinated with the user, per their instruction.
