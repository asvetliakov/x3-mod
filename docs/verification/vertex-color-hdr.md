# Numeric vertex-color HDR prerequisite

The standalone probe confirms that **VS3 → PS3 COLOR0 preserves values above
one through interpolation into an FP16 render target on this WineD3D backend**.
TEXCOORD0 also preserves them. VS2 → PS2 COLOR0 behaves differently: its values
are clamped per vertex before interpolation. Removing a pixel-shader saturation
can therefore preserve the SM3 vertex-light/emissive contribution on this backend;
the SM2 color path needs a different varying or another explicit strategy.

This is an original synthetic rendering experiment, not an X3 modification, an
HDR display test or a complete FP16 lighting implementation.

## Reproduction and artifacts

```sh
sh verification/probe/build_vertex_color_hdr.sh
python3 verification/probe/run_vertex_color_hdr.py
```

- [Original probe source](../../verification/probe/vertex_color_hdr.cpp)
- [Runner](../../verification/probe/run_vertex_color_hdr.py)
- [Numeric results](../../verification/results/vertex-color-hdr.txt)
- [Machine-readable summary](../../verification/results/vertex-color-hdr-summary.json)
- [Backend stderr](../../verification/results/vertex-color-hdr-wine.log)

The runner uses **CrossOver Preview.app**, Steam bottle, and a process-local
`d3d9=b` override. Resolved modules were `C:\windows\system32\d3d9.dll`,
`C:\windows\system32\wined3d.dll` and `C:\X3\d3dx9_37.dll`. The reported adapter
is WineD3D's emulated NVIDIA GeForce 8800 GTX. Source/executable hashes and the
exact command are recorded in the summary. The compiler DLL compiled only the
small original HLSL programs in this probe; no game shader was copied or patched.
The x86 static build completed without warnings.

## What was measured

A hidden 64×64 device uses hardware vertex processing and PUREDEVICE. Original
VS/PS pairs are compiled for shader models 2 and 3. Each VS receives a FLOAT4
attribute via TEXCOORD0 and forwards it unchanged through either COLOR0 or
TEXCOORD0. Each PS either returns its input directly or explicitly applies
`saturate`. All cases render the same triangle into `A16B16G16R16F` without
blending, depth, fog, alpha testing or sRGB writes.

Two input patterns separate clipping mechanisms:

- Uniform `(0.25, 4, 16, 1)` at all three vertices tests finite HDR channel values.
- A red gradient with vertex values `(0.25, 4, 16)`, plus constant G=4, B=16 and
  A=1, distinguishes preservation, per-vertex clipping, and clipping after
  interpolation. The triangle has W=1 at all vertices, so interpolation is linear.

Three interior pixels `(8,8)`, `(16,8)` and `(8,16)` are read back as half floats.
Expected barycentric weights use the explicitly chosen D3D9 pixel-corner
positions; the two unclamped TEXCOORD paths independently match those expectations.
Tolerance is `max(0.002, abs(expected)*0.0015)` per channel. Three numerical models
are compared; their intersection across all samples must be nonempty. TEXCOORD
must match the unclamped reference, with the explicit PS saturation applied when
requested. The summary intersects the models across both resource generations.

All 16 combinations run twice, releasing default-pool resources and unbinding
shaders/declarations before a successful Reset between generations. The hidden
window and COM objects are cleaned up; no launcher or game is opened. CPU
readback is solely verification, not a proposed renderer transport path.

## Results

**96/96 numeric samples passed across 32 cases**, including Reset and recreation.
Examples below use pixel `(8,8)`; both generations gave the same behavior.

| VS/PS profile | Varying | PS operation | Uniform RGBA | Gradient red |
| --- | --- | --- | --- | ---: |
| 3/3 | COLOR0 | Direct return | (0.25, 4, 16, 1) | 2.83984375 |
| 3/3 | TEXCOORD0 | Direct return | (0.25, 4, 16, 1) | 2.83984375 |
| 3/3 | COLOR0 | Explicit saturate | (0.25, 1, 1, 1) | 1 |
| 3/3 | TEXCOORD0 | Explicit saturate | (0.25, 1, 1, 1) | 1 |
| 2/2 | COLOR0 | Direct return | (0.25, 1, 1, 1) | 0.44921875 |
| 2/2 | COLOR0 | Explicit saturate | (0.25, 1, 1, 1) | 0.44921875 |
| 2/2 | TEXCOORD0 | Direct return | (0.25, 4, 16, 1) | 2.83984375 |
| 2/2 | TEXCOORD0 | Explicit saturate | (0.25, 1, 1, 1) | 1 |

The SM2 COLOR0 gradient equals interpolation of **clipped endpoint values**
`(0.25,1,1)`, not a clipped interpolated value of 1. This locates its loss before
interpolation numerically; the experiment does not identify which internal
WineD3D/compiler stage implements that semantic. The SM3 COLOR0 path shows no
such loss for the tested inputs. “Passed” means the probe consistently identified
these paths and controls, not that every shader profile supports HDR through
COLOR0.

## Renderer implication and remaining limits

[Instruction inspection](../reverse-engineering/position-shaders.md) found that
X3's observed SM3 material vertex shaders accumulate point-light RGB plus emissive
into COLOR0, and several matching pixel shaders explicitly saturate that input.
This probe removes the uncertainty that the SM3 color varying would already
have discarded values above one on this backend. A targeted replacement pixel
shader and pre-clamp FP16 target can preserve that term in principle.

The full path still needs actual game integration, correct material/color-space
interpretation, unclamped scene composition, HDR bloom, exposure/tonemapping and
verified HDR presentation. Other shader arithmetic or targets may clamp additional
terms. Mixed shader-model pairs, other D3D9 backends, blend modes, fog combinations,
negative radiance and physical display luminance were not tested. No result here
establishes that the current X3 scene already contains HDR values.
