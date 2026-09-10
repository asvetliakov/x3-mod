# Temporal resolve

`resolve.hlsl` is an original Shader Model 3 temporal reconstruction pass.
`resolve.h` defines its eight float4 constant registers and the CPU history
validity contract. The standalone GPU fixture loads this production shader
verbatim. Neither file is connected to the game loader yet. The shader does not
supply object identity, recover scene depth, inject jitter, or classify game
passes; those are required inputs from the renderer.

## Required inputs and state

| Slot | Input | Contract |
| --- | --- | --- |
| s0 | Current scene color | Scene-linear floating RGB; FP16 supported; no tonemapping/sRGB transfer |
| s1 | Current scene depth | R32F or raw-sampleable device depth, ordinary D3D depth in [0,1] |
| s2 | Previous resolved color | Same color space, exposure scale and dimensions as s0; FP16 |
| s3 | Previous scene depth | Previous frame's **unfiltered original depth**, not blended depth |
| s4 | Optional object reprojection | RGBA32F, interpretation below; FP16 absolute UV/depth is insufficient |

Every sampler uses POINT min/mag, no mip filter, CLAMP U/V and sRGB sampling off.
The resolve performs its own four-tap history reconstruction so each color tap is
tested against its own depth. Hardware bilinear filtering on any input would
violate this contract. Output is a distinct FP16 target, never simultaneously
bound as an input; alpha is one. Disable depth, blending, alpha test, fog and
sRGB output. The shader does not manage or restore application GPU state.

All textures describe the same full, local viewport with the same dimensions.
Source and previous viewport depth ranges must be MinZ=0, MaxZ=1; otherwise the
producer must first undo the viewport depth scale/bias. Reversed-Z is not this
contract.
A subrect of an atlas requires copying/remapping first. In D3D9, raster sample
positions are integers. Use a fullscreen primitive shifted **-0.5 pixel** in
both raster axes so TEXCOORD0 at pixel `(x,y)` is
`((x+0.5)/width, (y+0.5)/height)`. Do not add another half-texel correction in the
shader when **sampling textures**. Camera reconstruction uses a separate
half-texel conversion described below. The fixture verifies both using original
rasterized geometry and nonidentity zoom/rotation, not only identity/translation.

## Matrix and jitter ABI

Upload `ResolveConstants` to PS c0–c7. Four explicit c0–c3 rows multiply a column
vector using `dot`; HLSL's default matrix packing is irrelevant. The matrix is:

```text
previous_unjittered_view_projection * inverse(current_unjittered_view_projection)
```

It maps current unjittered homogeneous clip coordinates to previous unjittered
clip coordinates **in one consistent world coordinate regime**. At a pixel,
subtract **half a texel** and current jitter UV, reconstruct
`(2*u-1, 1-2*v, device_depth, 1)`, multiply, divide by positive previous W, convert
XY to normalized viewport coordinates, then add **half a texel** and previous
jitter UV. Texture center UV and raw camera viewport coordinates differ: at
16×16, pixel (8,8) has texture UV (8.5/16,8.5/16), but raw projection NDC (0,0).
Omitting both half-texel conversions cancels for identity/translation yet causes
errors for zoom and rotation. The unadjusted game camera matrix must not include
another fullscreen-quad sampling correction.
The transformed Z/W is the expected previous depth used for rejection. Merely
comparing old depth against current depth would fail camera translation.

`prepare` accepts jitter in raster pixels: positive X right, positive Y down.
They are actual raster displacements, not camera ray offsets of opposite sign.
Both matrix projections exclude jitter. The input colors and depths must come
from matching jittered rasterization. The resolve outputs on the current sample
grid; previous resolved color retains its previous jitter convention.

`prepare` validates dimensions, finite coefficients, weight and thresholds.
On false the caller must skip dispatch and invalidate history; do not upload a
partially prepared structure. Matrix coefficients above 1e15 are rejected and
projected values outside 1e20 are rejected in the shader. Unprojectable samples
use current color.

## Per-pixel object motion

When enabled, s4 alpha carries three states:

- `0`: camera reprojection is explicitly valid for this pixel (static geometry).
- `1`: RG contains **previous unjittered absolute texture UV**, including the
  half-texel that converts previous raw viewport coordinates to texture centers.
  B contains expected previous device depth for this surface. The resolve adds
  previous raster jitter only; it does not add another half texel on this path.
- `-1`: correspondence is unknown/invalid, so reject history. Use this for dynamic
  geometry without trustworthy previous transforms, deformation or identity.

RGBA32F is required: at a 5120-pixel width, FP16 absolute UV can quantize by
more than a pixel, and FP16 expected depth near 0.5 can lose more than the default
1e-4 rejection tolerance. Using R32F history depth cannot repair precision lost in
the motion input. The fixture checks expected depth 0.5002, which would round to
0.5 in FP16 and falsely reject a matching surface. It tests RGBA32F sampling;
a future GPU motion producer must separately validate render-target support.

The producer writes these exact states. Intermediate alpha values are reserved;
invalid/nonfinite alpha is rejected. The default clear value is -1 when motion
coverage is not known. Globally disabling motion is appropriate only when the
caller knows camera reprojection is sufficient. Missing moving-object motion
must not silently become a static camera fallback.

Expected previous depth is necessary even with a correct previous UV. A moving
object's current depth need not equal its previous depth. The object path must
be derived from actual previous object geometry/transforms, not guessed buffer
identity or draw order. This module does not yet obtain that data from X3.

## Rejection and history lifecycle

A nonpositive previous W, out-of-bounds previous UV, nonfinite color/depth, or
previous depth disagreement rejects history. Four previous pixel-center taps are
validated independently and remaining weights renormalized. Previous RGB is then
clipped to the finite current 3×3 RGB bounds before the blend. This is a bounded
initial resolve; it has no variance statistics, reactive mask, sharpening or
special transparency reconstruction.

Depth tolerance is `absolute + relative * abs(expected_previous_device_depth)`.
Defaults are absolute 1e-4, relative zero. These are **device-depth units**, not
meters. They require per-camera validation/tuning: distant surfaces in a
perspective projection may have very similar device depth. R32F prevents adding
FP16 quantization to D24 depth, but does not solve this projection ambiguity.
No claim of final game disocclusion quality follows from the synthetic fixture.

Colors outside the configurable finite HDR magnitude limit (default 65000) are
invalid. Invalid current color outputs black, finite current color with invalid
depth outputs current color, and invalid neighbors/history never enter the
clipping/blend arithmetic. Finite scene-linear values above one remain above one.

`HistoryState::begin(width,height,epoch)` invalidates on dimensions/epoch change.
The epoch is a stable scene/camera-regime identity plus resource generation,
**not** the frame number or a per-clear depth-content counter. Ordinary rendering
and clears in each frame do not invalidate history. Explicitly invalidate on
device loss/reset, camera cuts, missing depth or motion, failed render/copy,
changes between camera/depth-content regimes (background versus scene), and any
exposure convention change. Use distinct state/epochs for distinct cameras.
Call `completed()` only when **both** the next resolved color and corresponding
current raw depth have been saved successfully. It owns no D3D objects; resource
lifetime, before-Reset release and atomic success belong to the renderer.

The X3 trace clears the same depth allocation between background, scene and UI.
An allocation pointer is not a history epoch. The scene must be resolved while
the matching scene color/depth are intact. UI and unmatched effects must not be
accumulated merely because they appear before Present.

## Verification

See [GPU verification](../../docs/verification/temporal-resolve.md). The fixture
uses original synthetic inputs, FP16 color/history/output, R32F depth, the actual
production shader, and the current CrossOver Preview builtin D3D9 backend.

## Native D24X8 comparison decoder

`depth_decode.hlsl` is a verified fallback for this WineD3D backend's native
D24X8 RESZ snapshots, which expose shadow comparison rather than raw `tex2D`
depth. Bind the snapshot at s0 with point filtering, clamp addressing, no mipmaps
or sRGB, render at its dimensions with pixel-center UVs, and write to **R32F**.
The output is ordinary device depth in [0,1], matching resolve s1/s3. This shader
must not be used on INTZ or other textures already returning raw depth.

It performs 24 dependent comparison searches plus two endpoint checks (26 fetches
per pixel). The fixture preserves exact 0/1, distinguishes adjacent D24 values
from those endpoints, and observes at most one D24 step of error in its tested
set (acceptance limit two). It is not guaranteed bit-exact for every value.
Keep clear-depth semantics separate from the history rejection threshold.

See [decoder verification and cost limits](../../docs/verification/depth-decode.md).
The copy can occur within an existing scene without drawing or replacing the
original depth surface; decoding is a separate full-screen pass that requires
its own state restoration and resource ownership. Neither shader is injected
into the game yet.

The decoder clamps its interior midpoint below one before explicit endpoint
classification, preventing float32 midpoint rounding from aliasing nearby
geometry with clear depth. An independent CPU oracle covers exact-D24 and
float32-normalized comparison models; see the decoder verification document.
