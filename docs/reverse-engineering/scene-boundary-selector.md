# Conservative scene-depth boundary selector

The portable [selector](../../src/renderer/scene_boundary.h) recognizes the
observed main-scene/bloom/depth-clear sequence without draw indices or fixed
resource IDs. It emits a **pre-clear preservation candidate**, followed by a
separate confirmation only if that application Clear succeeds. It does not issue
GPU calls, infer a complete HUD partition, or enable TAA.

The twelve complete 0.3 frames in the
[station-test session](station-session-passes.md) all match its ordered pattern
when two explicitly missing query facts are supplied as test assumptions.
**Observed-only replay rejects all twelve frames:** the old Clear records have
no clear-time viewport, and old draw records omit the result of queries for
absent additional render targets. A recorded full viewport at an earlier draw
is insufficient to prove the viewport at the Clear. The runtime adapter must
obtain these facts from successful live queries; the conditional replay is not
replacement evidence for that check.

## Accepted sequence

One selector instance tracks one logical device. A frame starts with a known
nonzero resource generation and a fresh event sequence. Any missing, failed,
unsupported, or inconsistent event rejects the frame; no later bloom can rescue
that rejected prefix.

1. A full-viewport color-and-depth Clear, depth value one, no explicit rectangles:
   single-sampled A8R8G8B8 main color and same-size D24X8 depth.
2. A nonempty background region drawn only with the three verified shader pairs
   below; any one pair suffices and planet haze is optional. A successful full-viewport
   depth-only Clear of those same surfaces follows. This begins local depth epoch two.
3. A scene region on the same color/depth allocations and full viewport, including
   at least one successful draw with depth test and writes enabled. Draw count,
   primitive count and resource lifetime IDs can vary. No intervening clear,
   copy or target change is accepted.
4. Successful unbinding of depth, zero or more successful ColorFill operations on
   positively identified, nonaliasing scratch color textures, then one successful
   full-source/full-destination StretchRect from main color into a distinct same-size A8R8G8B8 texture surface.
5. Four consecutive bloom draws, separated only by the expected RT0 binds.
   Shader pairs, sampler-zero inputs and targets must follow the table below.
   Every draw has a full target viewport, triangle-strip topology, two primitives,
   disabled depth testing/writes, and a known null depth binding.
6. Rebind the original D24X8 depth, then immediately attempt a full-viewport
   depth-only Clear of that depth and main color binding. This is the sole
   preservation candidate. Its successful completion confirms the boundary.

All draws/clears require known absence of extra MRTs. Surfaces must retain their
lifetime IDs, texture-parent IDs, dimensions, format and sample count. Known null
bindings and unavailable queries have distinct representations. Exact success
checks concern application HRESULTs and the required state queries; the impending
Clear result is necessarily unavailable when the pre-call copy must occur.

| Region | VS | PS |
| --- | --- | --- |
| Background candidate | `7b6393fe2d3e1d85` | `6109cf64c03529dd` |
| Background material candidate | `37c34a7478544c14` | `5f82ecacd39529cd` |
| Background planet-haze candidate | `be199829a9bb78db` | `cd6d6eb4b3d99443` |

The material pair also occurs in the main scene. The separate clear epoch and
ordered resource flow distinguish its role here; the pair alone cannot do so.

| Bloom stage | VS | PS | Target / sampler-zero input |
| --- | --- | --- | --- |
| 0 | `cbbf26102694c961` | `1c90e79667bdaddf` | half-size A / copied main-color texture |
| 1 | `6059306306203243` | `f3172baa8dd19a40` | distinct half-size B / A's texture |
| 2 | `6059306306203243` | `241c3fa33270f58e` | A / B's texture |
| 3 | `1279d081455f5815` | `ff6eed5a5ddf3a3a` | original main color / A's texture |

Two triangles and a full viewport are evidence for the observed quad sequence,
not proof of vertex coverage or absence of scissoring. Original vertex contents
and scissor state were not captured. This selector preserves a stage boundary;
it does not promise that every pixel was overwritten by each bloom draw.

## Adapter contract

Call `begin_frame(device, generation, frame)` before the first relevant call, then
feed every completed application Draw, Clear, RT bind, depth bind, ColorFill and StretchRect
through `observe(Event)`. Sequences start at one and remain contiguous. Draw
snapshots describe the state used by that draw. Successful binds carry the new
binding. Injected renderer operations must not be fed back into this stream.

At Clear entry, query actual bindings/viewport and build its pending event. Call
`before_clear` before forwarding the application Clear. A valid selection carries
device, generation, frame, sequence, original color/depth descriptors and local
depth epoch. Preserve the candidate color/depth at that point, check the copy's
own result, then feed the original Clear's actual result to `observe`. Publish a
valid preserved snapshot only if both operations succeed and selection tokens
match. A failed Clear can produce a pre-call candidate but never confirmation.
The selector's depth epoch is local to its frame and must not be confused with
the ownership layer's source-content epoch.

`invalidate()` is mandatory on device loss, Reset, resource-generation change,
or uncertain resource identity. Start again with current generation metadata.
Resource lifetime IDs must not be raw COM addresses. An adapter must also observe
or invalidate on unsupported writes into tracked resources, such as
UpdateSurface or UpdateTexture; a gap-free counter over only a subset of content
mutations cannot prove continuity. Shader/state setters need no individual events
when the required Draw/Clear state is reliably queried at the actual call.

A confirmation is a boundary observation, not successful presentation or a valid
history image for a later frame. The adapter still owns copied-resource lifetime,
copy failures, later device loss, Reset and any temporal-history policy.

`SceneSignatures` stores three background pairs and four bloom pairs by value;
none of the background slots is individually mandatory. The default is the exact
verified profile above. An explicit constructor profile permits a separately
verified game version or original synthetic integration shaders. `begin_frame`
clears per-frame state while retaining that profile. Runtime auto-discovery or
loosening hashes from observed mismatches is not supported.

## Capture-only correction after iteration 0.4

The 0.4 trace contains five initial background draws with the verified
`7b6393fe2d3e1d85` / `6109cf64c03529dd` pair and no planet haze. Sixteen gameplay
frames passed the full initial Clear gate but were rejected at the second Clear
because haze had incorrectly been mandatory. Removing that requirement retains
all other original clear, dimensions, depth writer, color-copy and bloom checks.
Four earlier frames have initial target-only Clears and remain rejected.

The trace also contains three ColorFill calls per frame between the depth unbind
and main-color StretchRect, but it records no fill target descriptors. Their safety
cannot be established retrospectively. The updated adapter records each target's
lifetime ID, texture container, dimensions, format, sample count and rectangle.
Only successful fills of known A8R8G8B8, single-sample texture surfaces whose IDs
and nonzero container IDs do not alias main color or original depth are allowed,
and only in `AwaitCopy`. Standalone surfaces remain rejected until separate
evidence establishes their role. Full and partial rectangles are both safe under
this distinct-target condition; no arbitrary format or shader acceptance expands.

`scene_depth_reject` records the first rejecting event, operation, prior phase,
HRESULT, reason and target descriptors. The first reason and sequence remain
unchanged when later unsupported calls or invalidations occur. Frame-end records
also include `rejection_event`. No claim is made that the 0.4 frames would now pass:
a future capture-only build must supply the missing ColorFill target proof.

## Replay and adversarial verification

The compact [derived fixture](../../verification/fixtures/station-scene-boundaries.json)
contains descriptors, hashes and ordered event types from all 3,131 successful
draws in twelve frames. It includes no game shader bytes, vertices or textures.
Repeated event shapes are interned; event order and observed `after_draw` labels
remain available. Those labels are used only to compare the answer with the
independent pass report, never as classifier inputs. Generation one is an
original test argument; the old trace did not expose the new ownership generation.

The native C++ replay compiles the actual production header. Twenty-four test methods
cover twelve positive conditional replays, strict rejection of missing evidence,
changed draw counts and resource IDs, failed/unknown results, failed pending
Clear, sequence gaps/reordering, incorrect background/scene epochs, no depth
writers, copy/parent/target aliases, all four shader and texture links, wrong
quad state, partial/unknown viewports, depth mismatch, interposed operations,
truncation, device-generation invalidation and custom signature profiles that
survive frame resets. Additional tests cover the no-haze background, full/partial
scratch fills, unknown/main/depth/standalone/format/MSAA/container aliases, fills
in every other phase, failed fill results, and preserving the first rejection
reason/sequence through later unsupported events or invalidation. The failed-Clear case specifically
requires a candidate with no confirmation. A later overlay clear never emits a
second selection.

Conditional selections match the independent pass report exactly: after draws
92, 92, 97, 97, 284, 378, 401, 443, 400, 340, 232 and 143. This variation is useful
proof that a fixed draw index is not the rule.

```sh
python3 -m unittest verification.analysis.test_scene_boundary
python3 tools/analysis/replay_scene_boundary.py \
  --fixture verification/fixtures/station-scene-boundaries.json \
  --output verification/results/scene-boundary-replay.json
```

To regenerate the compact fixture, add `--trace` with the local
`session-20260910-214701-212.log` path. The raw SHA-256 is
`81cd598428312b709889b0fb892c6243a20818f20d0a3bf400eacdc33af017f0`.
The [report](../../verification/results/scene-boundary-replay.json) records fixture,
selector, adapter and analysis-script hashes, strict outcomes and explicitly
conditional outcomes separately. No graphical app is launched for replay.

## What this cannot select safely yet

The selected color is **post-bloom and pre-overlay**, already including background,
main geometry and earlier blended effects. Selected depth covers the main depth
epoch only: background depth was cleared, and depth-disabled/blended effects need
not have matching depths. A single camera/depth reprojection of the whole color
image is therefore still unsafe. This does not locate a clean opaque-only color
buffer, establish per-object motion, identify all later draws as HUD, or select
an HDR-linear source. Post-bloom rendering can contain real scene effects.

The strict background hash pattern and exact bloom chain deliberately reject
unseen scene variants, missing bloom, alternate resolution ratios, formats,
MSAA, extra passes and uncertain inputs. All twelve old frames are one test
session with no timestamp marking docking. A live successful selector/copy trace
is still required before claiming that this boundary works in the game adapter.
