# X3AP executable: static renderer evidence

Analyzed 2026-09-10, read-only, from the installed Steam bottle's `drive_c/X3/X3AP.exe`. This file identifies itself in its PE export table as `X3AP_NonSteamMaster.exe`; that string alone does not identify how it was acquired or whether it differs from a Steam build.

## Binary identity

| Property | Observed value |
|---|---|
| SHA-256 | `fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab` |
| File size | 2,153,984 bytes |
| Architecture | PE32, i386, Windows GUI |
| Preferred image base | `0x00400000` |
| Entry point | `0x00512ead` |
| PE timestamp | 2017-11-28 18:20:29 UTC (header metadata, not verified release date) |
| Characteristics | 32-bit, large address aware, relocations stripped |
| Linker version | 8.0 |

All addresses below are preferred virtual addresses for **this hash only**. They are research anchors, not approved executable patches. The initial implementation can intercept COM calls without patching them.

## Graphics imports

`d3d9.dll` has exactly one static import: `Direct3DCreate9`, IAT address `0x00532314`. No static `Direct3DCreate9Ex`, D3D11, D3D12 or DXGI imports were found. Dynamic loading remains possible: `LoadLibraryA` and `GetProcAddress` are also imported.

`d3dx9_37.dll` is both imported and installed next to the executable. Relevant IAT slots:

| Function | IAT VA | Use indicated by API name |
|---|---|---|
| `D3DXCreateEffect` | `0x00532324` | In-memory D3DX effect creation |
| `D3DXGetPixelShaderProfile` | `0x00532348` | Shader profile selection |
| `D3DXCreateRenderToEnvMap` | `0x00532354` | Environment-map rendering helper |
| `D3DXMatrixLookAtLH` | `0x0053231c` | Left-handed view matrix helper |
| `D3DXMatrixInverse` | `0x00532364` | Matrix inversion |
| `D3DXMatrixMultiply` | `0x0053236c` | Matrix multiplication |
| `D3DXCreateTextureFromFileInMemoryEx` | `0x00532334` | Texture construction |
| `D3DXCreateCubeTextureFromFileInMemoryEx` | `0x00532338` | Cubemap construction |
| `D3DXLoadSurfaceFromSurface` | `0x00532340` | Surface conversion/copy helper |

Also observed: mesh creation/cleanup, vertex projection, matrix transpose, texture capability checks, image inspection and screenshot saving imports. Imports establish availability, not that a feature is active in a particular scene.

The executable contains RTTI text `ID3DXEffectStateManager`. **Hypothesis:** a custom effect state manager centralizes material state changes; Ghidra cross-references and runtime call traces should establish the implementation before hooking it.

## Windowing clues

Imported Win32 functions include `CreateWindowExA`, `AdjustWindowRectEx`, `ShowWindow`, `SetWindowPos`, `GetMonitorInfoA` and `GetClientRect`. `VideoAntialiasMode`, `VideoShaderQuality`, `VideoD3DFlags` and `VideoD3DFlags2` are embedded setting names. This gives useful breakpoints/logging targets, but does not establish the cause of the macOS menu-bar overlay. The window style, D3D presentation parameters, Wine/CrossOver settings and native host-window state must be captured at runtime.

## Reproduce

```sh
python3 tools/analysis/inspect_x3.py '/path/to/X3' --output /tmp/x3-static-inventory.json
objdump -p '/path/to/X3/X3AP.exe'
objdump -h '/path/to/X3/X3AP.exe'
```

The script emits metadata only and never writes into the game directory. No executable, effect bytecode, textures, or decompiler output is included in this repository.
