# Current and previous reactive history

The production temporal resolve and `TemporalPass` now consume current reactive
coverage and preserve the previous coverage alongside color and depth history.
This prevents color from unsupported moving, appearing, disappearing or reordered
contributors entering temporal accumulation. It is **detached production code**;
live game coverage production/routing and final TAA quality remain unimplemented.

`FrameInputs::reactive_policy` makes the precondition explicit:

| Policy | Behavior |
|---|---|
| `Unavailable` (default) | Successful current-only resolve; no usable history is established |
| `KnownNonReactive` | Caller explicitly attests that ordinary accumulation needs no reactive mask |
| `RequiredMask` | Requires a native, matching-size R32F current mask; missing/invalid input rejects the run |

Mask zero means known safe. Every nonzero or nonfinite value means reactive;
source/destination alpha is irrelevant. The caller must supply complete
conservative coverage of visible unsupported RGB contributions on the current
jittered sample grid. Occluded contributors may leave zero coverage. Unknown
visibility or effect coverage must be overmarked or treated as unavailable,
never silently accepted as a clear mask. This change does not infer coverage
from game draw names, resource identity or alpha.

Current marked pixels use current scene-linear HDR color. After camera or object
reprojection, any **positive-weight** previous bilinear tap with reactive
coverage rejects the complete history lookup. A marked zero-weight tap does not
reject. Ordinary per-tap depth rejection and neighborhood clipping remain, but
the resolver does not renormalize around reactive color. This is conservative
history rejection, not motion reconstruction or separate transparent-layer TAA.

## Ownership, state and failures

Under `RequiredMask`, two additional owned R32F textures accompany the ping-pong
FP16 color/R32F depth histories. A third full-screen GPU draw uses explicit
`resolve.hlsl` snapshot mode to canonicalize the caller mask to 0/1. The caller
mask is only borrowed for the run and may then be reused or released. Color,
depth and mask become visible together only after every draw and state
restoration succeeds. Failure invalidates history and publishes no output.

The mask pair follows dimension, epoch, camera-cut, policy-transition and
Reset/shutdown invalidation. Uninitialized mask history is never read: the first
valid resolve cannot use history, and completion requires the snapshot draw.
Changing any reactive policy invalidates old history. Before native Reset the
pass releases all three resource pairs and its shaders. The caller still
serializes rendering/reset and obeys the existing borrowed-device lifetime.

Samplers s5/s6 use point filtering, clamp addressing and no sRGB conversion.
Full caller state restoration now includes those sampler states and textures.
No work is allowed during caller state-block recording or without known-idle
queries. All owned history textures are rejected as any current input, including
the reactive input. See the [source ABI](../../src/temporal/README.md).

The required-mask path adds one R32F snapshot draw and reactive texture reads;
the known-nonreactive path retains the existing two draws. Game cost is not yet
measured. There is no CPU readback in production and no additional shader
compiler/runtime initialization argument: c7.z explicitly selects snapshot mode
on the current production resolve program.

## Original GPU verification

Run `python3 verification/probe/run_temporal_pass.py`. The runner freshly builds
the current SSE2/Win32-stack-safe fixture, compiles the current production HLSL,
and verifies source, executable and D3DX hashes remain unchanged. Evidence is in
`verification/results/temporal-pass.txt` and `temporal-pass-summary.json`.

The expanded pure-device fixture passed **98 numeric samples and 102 complete
state-restoration comparisons**, spanning two resource generations separated by
a real native Reset. The original 44 numeric temporal cases remain included.
No game was launched, no install changed and no game assets were used.
The separate 58-sample standalone resolve regression and 102-sample rigid-motion
producer regression also pass against the changed production shader, with their
fresh source/executable provenance recorded in their existing result manifests.

The added scene is GPU-rendered from original rectangles and an opaque depth
surface. Particles really use `SRCBLEND=SRCCOLOR`,
`DESTBLEND=INVSRCCOLOR`, `BLENDOP=ADD`, no depth writes, and **source alpha zero**.
For a grayscale channel the independent expected blend is
`source² + destination*(1-source)`. A separate original coverage draw uses the
same rectangles/depth, disables blending and writes one. Coverage is not derived
from the resulting alpha; in the tested particle draw, destination alpha remains
one while RGB changes from 0.25 to 0.375.

| Case | Expected RGB | Distinguishing evidence |
|---|---:|---|
| Particle born | `0.375` | Previous clear coverage cannot protect against current particle RGB |
| Particle disappears | `0.25` | Current clear coverage alone would retain stale previous RGB |
| Particle moves | Old location `0.25`, new `0.75` | Previous and current coverage protect both locations |
| Order AB then BA | `0.65625` then `0.5625` | Actual blend is noncommutative; no object-order matching is assumed |
| Particle behind opaque depth | `0.5` | Coverage stays zero and ordinary accumulation remains active |
| Reactive HDR color | `2.25` | Mask rejection preserves values above SDR white |
| Object motion selects marked previous pixel | `0.25` | Previous mask follows object UV, rather than current pixel UV |
| One positive-weight reactive history tap | `0.25` | Whole footprint rejects instead of accepting/renormalizing the other tap |
| Marked zero-weight neighbor | `0.5` | A noncontributing tap does not disable valid history |

Additional checks establish:

- The owned previous mask remains one after the original caller render target is
  cleared/reused, and after a separately supplied caller mask is released.
- Negative, NaN, infinity and positive fractional mask values reject history and
  canonicalize to one in the owned R32F snapshot.
- Missing/wrong-format/wrong-size masks, unknown policies and owned-mask aliasing
  fail closed. Recovery uses current color with invalidated old history.
- Labeled native HRESULT injection on the **third GPU draw** invalidates the
  entire color/depth/mask set. A restoration failure also publishes none of them.
  This is failure orchestration evidence, not a claim of physical device loss.
- Required→Unavailable→Required and Required↔KnownNonReactive transitions never
  reuse the old policy's history. Repeated unavailable frames cannot accumulate.
- The new paths preserve the same hostile native state snapshot as the original
  temporal fixture, including MRT/DS identity, viewport/scissor, streams,
  shader constants, textures and sampler settings. Native Reset succeeds after
  caller resources and the pass are retired, and all cases repeat afterward.

Fixture readback is used only for numeric assertions. Masks pass directly from
their original GPU coverage producer into the temporal runtime, and previous
masks are snapshotted/consumed on the GPU. These bounded cases validate the
consumer and lifetime contract; they do not prove that all live X3 particle,
transparency, equal-depth or late effects contributors have been classified.
