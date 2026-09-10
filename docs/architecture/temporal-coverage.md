# Temporal coverage by rendering path

Rigid-object replay is one input producer, not the complete TAA implementation.
The 16 initially registered vertex programs prove a particular position path;
the five other captured vertex programs must be handled explicitly. Archive-only
programs likewise need an established path before a final whole-scene claim.

| Path | Required processing | Current boundary |
| --- | --- | --- |
| Ordinary object geometry | Reviewed position conversion, actual submitted current/previous clip matrices, lifecycle-safe identity, stable geometry and matching raster/depth coverage | Exact program lookup, bounded frame correspondence and a detached GPU motion producer are verified. Live lifetime/coverage/jitter integration remains pending. |
| Three original bloom VS (`1279…`, `6059…`, `cbbf…`) | Direct clip-space fullscreen draws need no object WVP. Replace original bloom with HDR bloom downstream of temporal scene reconstruction, avoiding independent jitter on bloom quads. | Their direct-position contracts are known; modern HDR bloom/composition is not implemented. |
| Direct-position GUI/effects VS (`f36f…`) | Classify each draw by actual pass/context. Keep confirmed HUD outside scene TAA and composite at controlled display brightness. Scene effects sharing the program need a separate motion/reactivity route. | The shared hash and post-bloom position in a frame do not prove HUD identity. No blanket exclusion of all uses is valid. |
| Particle billboard VS (`36f9…`) | Account for transformed center and billboard offsets before projection, including prior geometry, camera orientation and current coverage. Use particle correspondence when established; reject/react to history where identities or transparency are unresolved. | Vertex data changes every captured adjacent gameplay frame. Rigid-node history does not apply. Particle correspondence and coverage masks are not implemented. |
| Archive-only programs | Inspect position/data dependencies, then verify runtime input layouts and draw semantics. Select an appropriate producer or explicit rejection policy. | All 256 archived VS are structurally classified: 234 row-dot, 18 direct XYZW, two direct XYZ/W=1 and two billboards. Only the original 16 profiles are registered in production; archive classification does not prove runtime eligibility. Unknown future programs cannot inherit the nearest known hash's behavior. |

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

Temporary rejection prevents false correspondences while producers are built.
It does not count as complete temporal treatment of particles or other excluded
scene content. Final acceptance requires supported jitter, representative motion
tests across these paths, sharp HUD, controlled ghosting/disocclusion and resets
on load/scene/camera/resource discontinuities. The installed game still has no
TAA feature enabled.

See [position profiles](../reverse-engineering/rigid-position-profiles.md),
[motion correspondence](../verification/motion-history.md),
[GPU motion verification](../verification/rigid-motion.md),
[archive position inventory](../reverse-engineering/archive-position-paths.md),
[runtime passes](../reverse-engineering/runtime-passes.md) and
[full feature roadmap](roadmap.md).
