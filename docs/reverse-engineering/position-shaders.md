# Position and lighting shader dataflow

Inspected the actual runtime-captured shader instructions with the game's
`D3DXDisassembleShader` export, independently of CTAB names. All **11 vertex shader
hashes used by the 762 draws** in the two turning bursts were inspected. Their
position paths support the matrix interpretation in
[camera numerics](camera-numerics.md) and [turning evidence](turning-camera.md).
No positional shader animation was found in these 11 programs. This does not
exclude CPU vertex-buffer updates, other shaders in other scenes, or instance
replacement behind a shared resource key.

The original standalone helper is
[`tools/analysis/disassemble_shaders.cpp`](../../tools/analysis/disassemble_shaders.cpp).
It creates no D3D device and launches no game. It loads only the specified D3DX
helper and disassembles trusted local captures. **47 of 47 local programs** were
successfully disassembled. The raw instruction output remains untracked under
`/tmp/x3-shader-disassembly/`. Only hashes, counts and derived findings are retained
in [the inspection record](../../verification/results/position-shader-inspection.json).

## Actual position consumption

In the matrix paths below, the VS first constructs `(input_position.xyz, 1)`;
it does not preserve the supplied input W. Four dot products against consecutive
float4 constants then write clip X, Y, Z and W. No later instruction rewrites
those position components. Thus a constant register is a matrix row in the
column-vector notation used by the numerical analyzer.

| VS hash | Turning draws | Actual position path | Other use of those matrix registers |
| --- | ---: | --- | --- |
| `37c34a7478544c14` | 209 | Homogeneous input dotted with c24–27 → position o0 | None; world, normal and camera work use other registers |
| `494fe349b8bc12ec` | 301 | Same c24–27 path → position o0 | None |
| `53a0a641107ed76c` | 40 | Same c24–27 path → position o0 | None |
| `be199829a9bb78db` | 8 | Homogeneous input dotted with c0–3 → oPos | None; planet-haze candidate uses separate world/normal/camera constants |
| `7b6393fe2d3e1d85` | 84 | Homogeneous input dotted with c0–3 → oPos | None; UV transform uses c4–5 |
| `d5e1c75351ed3f04` | 64 | Homogeneous input dotted with c0–3 → oPos | None; fog/alpha uses separate world/camera constants |
| `5e484a06672e28fb` | 8 | Homogeneous input dotted with c0–3 → oPos | None; CTAB calls this **view-projection**, without a world matrix |
| `cbbf26102694c961` | 8 | Input XYZW copied to position o0 | No matrix; offsets affect UVs only |
| `6059306306203243` | 16 | Input XYZW copied to position o0 | No matrix; offset affects UV only |
| `1279d081455f5815` | 8 | Input XYZW copied to position o0 | No matrix; offsets affect UVs only |
| `f36fc43f30b19d71` | 16 | Input XYZ copied to oPos, W forced to 1 | No application matrix |

The first three programs use world c28–30 to produce world position and use
normal-transform c31–33 for lighting. Their camera input for view direction/fog
reads the `.w` translation components of c34–36. This directly corroborates the
three-register affine representation used in the analysis. Position itself
comes exclusively from the WVP rows; lighting/fog calculations do not deform it.

All inspected position paths are independent of time constants, skinning matrices,
vertex texture fetches, fog booleans and lighting loop counts. The shaders do
transform UVs and generate lighting/fog/reflection varyings, which may change
appearance without changing geometry. The named view-projection effects path
may receive positions already transformed on the CPU; instructions alone do not
establish that upstream coordinate contract. No particle-specific shader was
actually drawn in these turning bursts.

## Jitter locations established, pass eligibility still required

For the verified matrix paths, changing the first two rows as follows shifts
clip XY by the desired amount proportional to clip W:

```text
row0_jittered = row0 + jitter_ndc_x * row3
row1_jittered = row1 + jitter_ndc_y * row3
row2 and row3 remain unchanged
```

On the common material path these are c24/c25 modified using c27; on the other
matrix paths they are c0/c1 using c3. At a viewport width W and height H, a desired
pixel displacement `(dx,dy)` corresponds to NDC `(2*dx/W, -2*dy/H)` with ordinary
D3D viewport Y direction. This is an algebraic injection site, **not an executed
game jitter experiment**. It leaves separate world/lighting/fog constants intact.

Hash-wide injection is still unsafe. `7b6393fe2d3e1d85` is shared by GUI, nebula
and effects; `d5e1c75351ed3f04` also draws depth-disabled final effects with a
near-identity camera. The pass-through bloom shaders have no WVP to patch.
A production experiment must classify an eligible scene draw, change only its
constants, and preserve/restore state before unjittered draws. Depth and scene
composition ownership remain separate gates.

## Point lights are vertex-lit on the observed material paths

The first three common VS programs execute a repetition controlled by **integer
register i0.x**. A local light index starts at zero and increases by one per
iteration. Multiplication by three addresses the structure array:

| Register relative to `3*light_index` | Instruction-derived use |
| --- | --- |
| c0.xyz | Light position minus transformed world position |
| c1.xyz | RGB light multiplier |
| c2.xyz | Constant, linear-distance and squared-distance attenuation coefficients |
| c2.w | Not read by these loops |

The loop normalizes the light vector, computes a saturated normal/light dot
product, forms the attenuation denominator from `(1, distance, distance²)`,
and applies a saturated reciprocal. It accumulates diffuse RGB. After the loop,
a material emissive color is added and sent to vertex COLOR0: c41 in
`37c34a7478544c14`, c40 in `494fe349b8bc12ec` and `53a0a641107ed76c`.
The loops do not contain a separate radius test or point-light specular term.
Normal transformation is not followed by normalization inside these VS loops,
so retaining their exact response is distinct from substituting a normalized
modern material model.

This instruction evidence gives semantics to the first three attenuation
components for these specific shaders. It does not establish physical units,
a universal engine light layout, or the fourth component's meaning elsewhere.
The turning trace's **550 named light observations all have i0.x=0**. Therefore
none of these loops ran an active point-light iteration in those captured draws;
stale float-array entries still cannot establish any active light.

## Pixel consumption exposes an HDR clamp before the target

Inspected material PS hashes `5f82ecacd39529cd`, `fffdabd910793aba`,
`7c83ed50c9894e44`, `8759c7838bbc86c2` and `f1b0e820c7b488c3` receive the
interpolated vertex COLOR0 as input v0. Each explicitly **saturates v0 RGB to
0–1** before combining it with two directional-light contributions and material
textures. This clamps the accumulated vertex point-light plus emissive term
inside the shader, before any render-target conversion. An FP16 target alone
cannot recover this lost range. Directional lighting and other later additions
have separate arithmetic; this finding does not mean the entire PS output is
explicitly saturated by those instructions.

None of these five pixel shaders has a point-light i0 loop. Directional light
registers are c5/c6 and c7/c8 (direction/color pairs) for the bump/XT paths, or
c4/c5 and c6/c7 for the standard/argon paths. Diffuse and specular response uses
those fixed pairs. Point-light processing is the preceding VS loop, not an
uncaptured per-pixel cluster list. The effects PS `8360f422de08b5bd` instead
multiplies its sampled/color-transformed RGB by vertex input v0.x and has no
point-light loop. These findings narrow where replacement lighting and emissive
preservation must occur; they do not implement either feature.

## Reproducing the offline inspection

```sh
mkdir -p /tmp/x3-shader-disassembly
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -static \
  tools/analysis/disassemble_shaders.cpp \
  -o /tmp/x3-shader-disassembly/disassemble_shaders.exe
'/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine' \
  --bottle Steam --no-update --workdir /tmp/x3-shader-disassembly \
  /tmp/x3-shader-disassembly/disassemble_shaders.exe \
  'C:\X3\d3dx9_37.dll' 'C:\X3\x3-modern-captures' \
  'Z:\tmp\x3-shader-disassembly'
```

The helper compiled without warnings with the command above and returned exit 0,
`processed=47 failures=0`. Source, DLL and inspected bytecode SHA-256 values are
in the inspection record; the existing capture shader IDs are FNV-1a-64.
Disassembly is inspected manually, rather than treated as a generic automated
proof that every possible shader family supports these contracts.
