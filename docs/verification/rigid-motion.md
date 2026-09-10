# Detached rigid-object motion producer

`src/renderer/rigid_motion.{h,cpp}` is a production D3D9 GPU pass that produces
the per-pixel input consumed by `src/temporal/resolve.hlsl`. The vertex program is fixed internally by `src/renderer/rigid_replay_program.cpp`;
initialization accepts only the motion pixel shader (`rigid_motion_ps.hlsl`).
The former `rigid_motion_vs.hlsl` is retained as historical illustration and is
not compiled or used by the pass. This is working
detached rendering code, **not live game routing or whole-frame TAA coverage**.
The fixture launches no game and installs nothing.

## Input and output contract

The caller supplies native, borrowed resources: a full-size, non-MSAA RGBA32F
render-target texture, the still-valid original D24X8 scene depth surface, and
an array of explicitly classified draws. The viewport has origin zero and depth
range `[0,1]`. Each draw carries actual stream byte offset, position offset,
stride, optional index buffer and original draw arguments. Accepted topology is
triangle list or strip, with stream frequency exactly one.

Each draw carries a `RigidReplayContract` value. The only production issuer,
`qualify_rigid_replay_source`, checks the complete immutable source bytes against
the reviewed registry and requires SM3, XYZW writes and the exact homogeneous
MAD constructor. It currently admits 32 archive programs and rejects the other
202 legacy row-dot programs. Issue this token once when admitting a source shader,
then cache it with that actual shader identity: replay performs no per-draw
whole-program hash. The token owns its hash/count/provenance values, with no
reference to caller bytecode storage. Default tokens are unknown; callers cannot
construct a token from a boolean or a fabricated metadata record through this API.

**The upstream draw record must bind the token to the actual submitted shader.**
A token copied from another qualified source does not prove that association, and
the detached pass cannot discover it. Only verification builds expose a separately
named synthetic issuer; it has distinct provenance and no archive hash. The
production object is checked to contain no such issuer symbol.

Both FLOAT3 and **FLOAT16_4** preserve their native declaration conversion;
packed input W is ignored. `finite_positions_attested` is a separate mandatory
proof for XYZ: the fixed instruction sequence and synthetic exceptional-value
checks do not remove this requirement. The caller must also attest stable VB/IB
contents through replay, trustworthy adjacent-frame correspondence, and ordinary
opaque rasterization. Alpha test/discard/blend, custom pixel depth, depth bias,
user clipping, scissor and instancing are excluded. Resource/version and
object-lifetime matching belong to the correspondence policy, not this pass.

Current and previous WVP arrays contain the **actual submitted shader-register
rows**, including their actual raster jitter. They are uploaded without matrix
multiplication, transposition or a second jitter injection. The shader
perspective-interpolates previous homogeneous clip coordinates at current raster
samples and divides by previous W. Output is:

- R/G: absolute previous texture UV, including the D3D9 texture half-texel but
  **excluding previous raster jitter**; the existing resolve adds that jitter.
- B: expected previous device depth, without FP16 quantization.
- A: exactly one for valid rigid correspondence; minus one for invalid history.

Invalid previous W, nonfinite or excessively large clip values, and previous
depth outside `[0,1]` produce the invalid sentinel. Previous UV may be outside
the texture; the resolve applies its final bounds check after adding jitter.
There is no production CPU readback or pixel upload.

## Visibility, failure and lifetime limits

Every run first fills the entire motion target with `(0,0,0,-1)` using an
oversized triangle on the same programmable vertex path. **Alpha zero is never
written**: it would incorrectly request camera fallback for unknown regions.
Eligible geometry then uses native depth `EQUAL`, with depth writes disabled,
against the preserved original scene surface. A nearer unsupported occluder
therefore leaves invalid motion. Replay arithmetic or rasterization differences
can reduce coverage under strict equality; no loose-depth substitute is used.

Depth equality is not exclusive color ownership. Equal-depth competing surfaces,
transparent/additive contributors, and later effects can invalidate correspondence
even when this pass's depth test succeeds. The caller must mask or reject those
regions, including contributors omitted from the batch. An eligible draw list
alone does not establish complete scene coverage. This module does not yet select
game draws, apply game jitter, establish object generations or enable TAA.

Malformed metadata and unsupported inputs reject the entire batch before state
mutation. Bounds validation divides the known vertex-buffer allocation by stride
to avoid overflow from extreme draw counts. Index data is not read back: the
caller must provide the original valid min/count/base ranges and unchanged data.

No work is allowed during caller state-block recording or without positive
knowledge that occlusion/statistics queries are idle. A full state block plus
explicit RT/DS, viewport and scissor preservation restores the caller. Ordinary
restore errors continue cleanup; observed device loss stops ordinary setters.
Output is published only after both rendering and restoration succeed. Failed
runs may have partially overwritten the caller's target, which must not be used.

The device is borrowed. Only shaders and two position declarations persist;
VB/IB/RT/DS references do not. The caller serializes rendering/reset, owns target
and depth lifetime, and calls `before_reset`/`shutdown` before native Reset or
final teardown. Reinitialize after Reset. Cost is one initialization draw plus
one replay per submitted object; performance in a game scene is unmeasured.

## Original GPU verification

Run `python3 verification/probe/run_rigid_motion.py`. It always builds the current
fixture with SSE2 arithmetic and the four-byte incoming Win32 stack contract,
hashes sources before/after compilation and execution, and hashes the executable
and D3DX compiler. Runtime-loaded HLSL is part of that frozen source manifest.
All GPU-executed shaders and geometry are original; local archive bytes are read
only for CPU qualification. Native builtin D3D9 is
selected process-locally under CrossOver Preview's Steam bottle.

The final run passed **102 numeric samples, 117 checks and 30 full state comparisons**
on a native pure device, spanning **two resource generations and a real Reset**.
Evidence is in `verification/results/rigid-motion.txt` and
`rigid-motion-summary.json`. It covers:

- Actual exact SM3 token issuance, reviewed legacy refusal, truncated/modified
  byte refusal, version-only legacy forgery refusal, copied token lifetime after
  source byte release, and distinct verification-only synthetic provenance.
  Archive inputs are hashed and read only for CPU qualification; the GPU scene
  uses original synthetic shaders and its explicitly synthetic token.

- Stationary geometry, object translation, indexed FLOAT16_4 input with
  z=`0.333251953125`, unrelated stored W=`7`, and mixed current-depth matrix rows.
  An independently assembled original MAD/temporary-first DP4 shader produces scene depth;
  production replay passes exact native `EQUAL` against it.
- Perspective-varying clip W at two different raster samples. An independent CPU
  oracle solves the raster ray/plane equations and then projects the recovered
  object point through the prior matrix. It does not read shader intermediates.
- Distinct current/previous submitted jitter, correct D3D9 half-texel placement,
  negative prior W, prior depth outside the clip range and current-depth mismatch.
- A nearer unsupported occluder, scene color preservation, and an independent
  depth-integrity draw that must pass against the original far surface while
  failing against the original nearer surface.
- Complete invalid initialization with an empty batch, including all 256 pixels.
- Cached source-token, explicit finite-POSITION, conversion, instancing, topology, finite-matrix, extent, depth-age and
  range refusals, including extreme count/stride and indexed range overflow.
- Real state-block recording refusal and a real active occlusion query that sees
  zero samples from the refused pass. Hostile caller state includes MRTs, packed
  declarations, stream offsets/frequencies, samplers, shader constants, scissor,
  clip planes, blend/test state, depth bias and write masks.
- Explicitly labeled test-only native HRESULT injection for draw failure,
  restoration failure and loss. These prove orchestration; they do not claim
  that the physical device was lost. Real native Reset is tested separately.
- **Actual production resolve consumption:** produced object motion yields
  color `0.5`, whereas the same resolve with camera-only fallback yields `0.5625`.
  The consumer directly samples the unchanged GPU-produced texture. The fixture
  also reads it back for numeric assertions before consumption, but that readback
  data is never fed back or uploaded into the motion input.

The largest low-resolution error was the **Y component at raster pixel (10,6)**
in the varying-W case: actual `0.388483793`, analytic `0.388392866`, an error of
`9.0927e-5` normalized UV or `0.00145483` pixel. The low-resolution analytic
comparison tolerance is `1e-4`; the final FP16 resolve-color comparison uses
`0.001`. A normalized error observed at 16 pixels must not simply be accepted
as a resolution-independent bound for a large display.

Two focused larger-viewport controls therefore render the same perspective plane
and assert the same NDC rays at `(5W/8,3H/8)` and `(3W/8,5H/8)`. They use the
production pass and independent analytic oracle, with additional bounds of
**0.005 pixel in either axis and `2e-6` device depth**. Maximum observed errors
across those two samples are:

| Viewport | Normalized UV error | Pixel error | Device-depth error |
|---|---:|---:|---:|
| 1280×768 | `2.264977e-6` | `0.001739502` | `4.17233e-7` |
| 5120×1440 | `3.57628e-7` | `0.000514984` | `1.19209e-7` |

The decrease in normalized error is consistent with raster/subpixel precision,
rather than the resolution-independent UV loss that would create nearly half a
pixel error at 5120 pixels. This is an inference from these controls, not a proof
of a particular driver's interpolation implementation. These few samples do not
establish every triangle, depth regime or backend; strict equality coverage and
performance still require real game-scene validation.

An early numeric run caught an all-invalid replay after fixed-function XYZRHW
initialization. Direct binding and omission-of-initialization controls rendered
the expected motion. Using the same programmable vertex path for initialization
and replay resolved the issue. The final fixture exercises this sequence on every
run; no claim is made about the underlying driver implementation cause.
