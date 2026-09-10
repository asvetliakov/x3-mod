# Temporal coverage by rendering path

Rigid-object replay is one input producer, not the complete TAA implementation.
The 16 initially registered vertex programs prove a particular position path;
the five other captured vertex programs must be handled explicitly. Archive-only
programs likewise need an established path before a final whole-scene claim.

| Path | Required processing | Current boundary |
| --- | --- | --- |
| Ordinary object geometry | Reviewed position conversion, actual submitted current/previous clip matrices, lifecycle-safe identity, stable geometry and matching raster/depth coverage | All 234 archive row-dot programs are registered. Bounded frame correspondence, actual-state input acquisition and a detached GPU motion producer are verified. Live lifetime/coverage/jitter integration remains pending. |
| Three original bloom VS (`1279…`, `6059…`, `cbbf…`) | Direct clip-space fullscreen draws need no object WVP. Replace original bloom with HDR bloom downstream of temporal scene reconstruction, avoiding independent jitter on bloom quads. | Their direct-position contracts are known; modern HDR bloom/composition is not implemented. |
| Direct-position GUI/effects VS (`f36f…`) | Classify each draw by actual pass/context. Keep confirmed HUD outside scene TAA and composite at controlled display brightness. Scene effects sharing the program need a separate motion/reactivity route. | The shared hash and post-bloom position in a frame do not prove HUD identity. No blanket exclusion of all uses is valid. |
| Particle billboard VS (`36f9…`) | Account for transformed center and billboard offsets before projection, including prior geometry, camera orientation and current coverage. Use particle correspondence when established; reject/react to history where identities or transparency are unresolved. | Vertex data changes every captured adjacent gameplay frame. Rigid-node history does not apply. Current/prior reactive-mask consumption and owned history are implemented and GPU-verified; live mask production and particle correspondence remain pending. |
| Archive-only programs | Inspect position/data dependencies, then verify runtime input layouts and draw semantics. Select an appropriate producer or explicit rejection policy. | All 256 archived VS are classified and registered: 234 row-dot, 18 direct XYZW, two direct XYZ/W=1 and two billboards. An additional 494 exact PS profiles prove absence of discard/depth output. These narrow proofs do not establish runtime eligibility. Unknown future programs cannot inherit the nearest known hash's behavior. |

The motion texture's alpha has a precise meaning: **1** supplies per-pixel
previous UV/depth; **0** permits camera-only reconstruction; any other value
rejects history. Initialize uncovered pixels to **−1**, not zero. A transparent
or unsupported draw over valid rigid geometry must also invalidate/react to its
color contribution; a matching opaque depth value alone does not identify the
source of the final pixel color. Equal-depth competing surfaces are another
coverage ambiguity requiring explicit treatment.

The observed particles blend using source RGB, not alpha. Their reactive mask
must cover the actual RGB contribution and reject prior reactive coverage at the
history lookup, including disappeared particles. The changing, unscoped stardust
buffer also needs this separate policy despite qualifying for row-dot position
arithmetic. See [particle inputs](../reverse-engineering/particle-motion-inputs.md).
The production resolve now owns and checks both mask generations, with explicit
unknown/nonreactive/required-mask policies; see
[reactive history verification](../verification/reactive-history.md).

Temporary rejection prevents false correspondences while producers are built.
It does not count as complete temporal treatment of particles or other excluded
scene content. Final acceptance requires supported jitter, representative motion
tests across these paths, sharp HUD, controlled ghosting/disocclusion and resets
on load/scene/camera/resource discontinuities. The installed game still has no
TAA feature enabled.

## Next position-equivalence work

The current compiled rigid-motion VS already uses a homogeneous MAD constructor,
although its HLSL spells `float4(position.xyz, 1)`. Its DP4 operands are ordered
row/temporary, while the inspected original programs use temporary/row. A
token-generated replay shader can preserve that order, exact literal bits and
swizzles without relying on compiler simplification. This is a candidate for
removing the algebraic-substitution concern, not permission to drop payload gates.

Start qualification with the 32 archive SM3 row-dot programs, which include the
captured main materials. The other 202 row-dot programs use legacy models; six
legacy programs also write output lanes in a different order. Production profile
metadata now retains those distinctions, verified independently against raw
tokens; this alone does not establish exact replay. Compare
generated tokens and actual depth/raster coverage against original synthetic
references, including signed zero, subnormals, finite extremes, NaNs/infinities
and every half-float encoding in each XYZ lane. Stored W remains independent and
ignored by the reviewed position semantics. A CPU `isfinite` scan alone does not
settle signed-zero/subnormal or backend arithmetic equivalence. Any CPU validity
cache must include buffer revision and the actual layout/range, avoiding repeated
per-frame locks for stable geometry. None of this additional qualification is
implemented or verified yet.

See [position profiles](../reverse-engineering/rigid-position-profiles.md),
[motion correspondence](../verification/motion-history.md),
[GPU motion verification](../verification/rigid-motion.md),
[archive position inventory](../reverse-engineering/archive-position-paths.md),
[runtime passes](../reverse-engineering/runtime-passes.md) and
[full feature roadmap](roadmap.md).
