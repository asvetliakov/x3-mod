# Ghidra render map

Ghidra 12.1.3 analyzed the executable identified in [executable.md](executable.md) successfully in approximately 85 seconds. The local project lives outside the repository at `/tmp/x3-ghidra-research/X3Render.gpr`; temporary projects are not durable artifacts. The original analysis scripts are committed so the findings can be reproduced.

## Observed function anchors

| Preferred VA | Evidence | Interpretation and confidence |
|---|---|---|
| `0x004d8470` | Calls `Direct3DCreate9(0x20)` at `0x004d848f`; queries vtable slot 4 and iterates adapter records with stride `0x730` | D3D initialization / adapter enumeration, high confidence |
| `0x004dac90` | Calls `CreateWindowExA`, `GetClientRect`, `GetMonitorInfoA` and D3D creation thunk at `0x004dae76` | Window/device initialization, high confidence; inferred prototype unreliable |
| `0x004bae10` | Builds `shader\%s\%s`; reads resource data; calls `D3DXCreateEffect` at `0x004bafca`; subsequently calls effect slot 71 (`+0x11c`) | Effect loader with custom `SetStateManager`, high confidence |
| `0x004c0150` | Looks up world/WVP, material, point-light count and point-light array names; substantial geometry/state logic | Main material/geometry rendering routine, medium-high confidence; not yet fully mapped |
| `0x004bf4c0` | Looks up `g_mView` and `g_mProj`, then changes effects and vertex/stream state | Particle-related render path candidate, medium confidence |
| `0x004c4750` | References bloom, scene/glow maps and `RenderColorTarget0`; performs effect passes and triangle-strip draw calls | Original bloom/composition routine, high confidence |
| `0x004c4100` | Calls `D3DXCreateRenderToEnvMap` at `0x004c4145` | Environment-map helper setup, medium confidence |

Import thunks: `0x004faedc` jumps through the Direct3DCreate9 IAT slot; `0x004faeee` jumps through D3DXCreateEffect; `0x004faf24` jumps through D3DXCreateRenderToEnvMap. They are not separate rendering implementations.

## Renderer object candidates

Decompiled use and COM slot signatures support this provisional layout:

- Global pointer at VA `0x00608b3c` refers to renderer-wide state.
- Offset `+0x00` holds the `IDirect3D9` pointer after successful creation.
- Offset `+0x18` points to a device-side object whose first member is an `IDirect3DDevice9` pointer.
- Offset `+0x1c` is passed to `ID3DXEffect::SetStateManager` and used for render-state, stream and shader-related calls.

These are **inferred object layouts**. No executable hook uses them, and lifetime, reset behavior, all field types and calling conventions remain unverified. A COM proxy is preferable for the first trace because it does not need these assumptions.

## Composition boundary

`0x004c4750` requires several non-null globals in the range `0x00608a64` through `0x00608a74` and rendering option checks before running. It acquires/restores D3D surfaces, obtains the bloom effect, resolves scene/glow inputs and executes effect passes. It resolves each pass's `RenderColorTarget0` annotation before selecting render target 0. A draw uses device vtable offset `0x144`, consistent with `DrawPrimitive`, with triangle-strip type and two primitives.

This is a useful place to correlate runtime shader hashes and render-target events with the original compositor. The observed copies and target switches do not establish texture precision, gamma behavior, or HUD separation. Ghidra's output for this function reports unreachable blocks and loses some argument information, so precise pass ordering must be corroborated against assembly or runtime calls.

## Window flags

In the provisional `0x004dac90` decompilation, input bits `0x08000000`, `0x20000000` and `0x2000` participate in selecting Windows style values. One branch selects `0x90000000`, while others include `0x10000000`, `0x10ca0000` or `0x10cf0000`. This is a lead for mapping the game's borderless/fullscreen configuration; no claim is made yet that a particular input bit fixes macOS's menu bar. The resulting native host window must be inspected under CrossOver.

## Reproduce targeted analysis

Set `JAVA_HOME` to an installed JDK. On the analyzed host Ghidra's default launcher could not locate Java, but the existing Homebrew JDK at `/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home` worked without installing anything.

```sh
mkdir -p /tmp/x3-ghidra-research
JAVA_HOME='/path/to/jdk' '/path/to/ghidra/support/analyzeHeadless' \
  /tmp/x3-ghidra-research X3Render -import '/path/to/X3/X3AP.exe' \
  -analysisTimeoutPerFile 120 -scriptPath tools/analysis \
  -postScript X3RenderXrefs.java
```

To inspect a few functions in the existing project:

```sh
JAVA_HOME='/path/to/jdk' '/path/to/ghidra/support/analyzeHeadless' \
  /tmp/x3-ghidra-research X3Render -process X3AP.exe -noanalysis \
  -scriptPath tools/analysis \
  -postScript X3DecompileFunctions.java /tmp/x3-functions.c 004bae10 004c4750
```

Generated decompilation stays local and must not be committed. Documentation records derived architecture observations rather than reproducing implementation.
