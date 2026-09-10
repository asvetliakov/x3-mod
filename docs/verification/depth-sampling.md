# Synthetic INTZ depth sampling in CrossOver Preview

Verified 2026-09-10 with a standalone 32-bit program in the Steam bottle of
**CrossOver Preview.app**. INTZ is numerically sampleable after rendering to it
on this backend. This is evidence for the scene-depth prerequisite of TAA; it
does not add TAA or modify the game's depth surfaces.

## Reproduce

From the project root:

```sh
sh verification/probe/build_depth_sampling.sh
python3 verification/probe/run_depth_sampling.py
```

The runner executes the following invocation with a 60-second timeout and writes
stdout, stderr and a JSON summary to `verification/results/depth-sampling*`:

```sh
"/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine" \
  --bottle Steam --no-update --workdir "$PWD/verification/probe/build" \
  "$PWD/verification/probe/build/depth_sampling.exe" 'C:\X3\d3dx9_37.dll'
```

The executable dynamically loads the existing local compiler. All three shaders
are original, tiny HLSL strings embedded in the probe; no game shader bytecode or
vendor DLL is redistributed. MinGW dependencies are statically linked, including
the threading runtime used by C++ exceptions. The probe creates one hidden window,
destroys it on success/failure, exits automatically, and never launches X3 or
changes its configuration. No process-local override or production proxy is used.

## What was exercised

- HAL device with `D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_PUREDEVICE`
  (`0x50`), exercising pure-device constraints, 64×64 windowed backbuffer.
- `CheckDepthStencilMatch` for INTZ with both A8R8G8B8 and A16B16G16R16F succeeds.
  The allocated depth texture and corresponding color target have matching sizes
  and no multisampling.
- Clear depth to 1, draw a near triangle at 0.25 and a far triangle at 0.75.
  Submit a third triangle over the near one at 0.875 with `LESS` depth test;
  the near result must survive. Both color/depth and raster state are explicit.
- Unbind INTZ from the depth-stencil slot before binding its texture at sampler 0.
  Sample the red channel with point/clamp filtering into the color target using
  a fullscreen quad. The shader replicates depth to RGB and writes alpha 1.
- Read back four interior/clear samples and verify all four channels numerically.
  A point outside the near triangle's diagonal checks triangle coverage rather
  than merely checking its bounding rectangle.
- Repeat for RGBA8 and FP16; unbind and release all created default-pool resources,
  call `Reset`, recreate everything and repeat both formats.

The output contains **16 passing sampled-pixel checks** (four positions × two
formats × two resource generations). Each also checks alpha equals 1. Exit code
is zero, and reset succeeds.

| Pixel case | Expected depth | RGBA8 sampled red | FP16 sampled red |
| --- | ---: | ---: | ---: |
| Near triangle, later far overlap rejected | 0.25 | 0.250980 | 0.250000 |
| Far triangle | 0.75 | 0.749020 | 0.750000 |
| Cleared background | 1 | 1.000000 | 1.000000 |
| Outside triangle coverage | 1 | 1.000000 | 1.000000 |

Results match before/after reset. Tolerances are 1.5/255 for RGBA8 quantization and
0.001 for FP16. The report includes executable/source SHA-256 hashes and the
exact runtime invocation. Loaded module paths are system `d3d9.dll`, system
`wined3d.dll`, and `C:\X3\d3dx9_37.dll`.

## Production implications and remaining gates

A separately allocated INTZ depth texture can receive geometry depth and be
sampled through the existing D3D9 backend. This enables investigating substitution
of eligible non-MSAA scene depth surfaces, retaining a real sampleable texture
until temporal processing. It avoids assuming that `CheckDeviceFormat` alone
proves sampling works.

The probe does **not** convert/copy the game's D24X8 surface into INTZ, prove its
complete frame depth lifetime, validate substitution against X3 surface ownership
or reset behavior, or distinguish main scene from 256×256 depth uses. Production
must identify the correct scene surface, preserve app-visible COM identity and
descriptors, avoid simultaneous depth-write/sampling hazards, restore all state,
and validate real geometry/UI ordering. It must also handle failed allocation,
device loss and resize. MSAA/depth resolves, stencil semantics, nontrivial
projection linearization, camera jitter, motion vectors and TAA are untested.

CPU readback here is a disposable verification mechanism, not a proposed per-frame
production path. Depth values in this test are normalized post-projection depth,
not world-space distances. Pure-device mode verifies the render/sample operations;
the test does not assert that arbitrary pure-device state getters work.
