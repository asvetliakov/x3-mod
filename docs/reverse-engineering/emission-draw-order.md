# Geometry ordering relevant to linear emissions

2026-09-13. Read-only targeted Ghidra inspection of the existing X3AP project;
no game run, capture, binary modification or runtime hook. Executable provenance
is the same as [the compositor study](compositor-and-glow.md).

**There is a sorted deferred-geometry subphase, but no established additive-only
phase.** Its list is populated alongside immediate material submissions and is
traversed without an additive/screen/alpha partition. Moving selected draws to
the end of this list, bucket, view or scene is not an equivalent ordering.

## Confirmed callsites and list contract

The ordinary view loop in `0x00471f50` calls camera setup at `0x00472260`, then
visibility preparation at `0x0047226b`. It iterates the integer bucket interval
from `[view+0x1c]+0x4c` through `+0x50`, inclusive:

| Address | Observed operation |
| --- | --- |
| `0x00472295`, conditionally | Collection `0x0047e920` with mode 4 |
| `0x004722a8` | Collection `0x0047e920` with ordinary mode |
| `0x004722af` | Sort `0x0047e620` |
| `0x004722b5` | Traverse/draw `0x0047e6e0` |
| `0x004722bd..0x004722c6` | Increment bucket and repeat |
| `0x00472307` | Optional particle renderer `0x004bf4c0`, under view flag `0x4000` |

These bucket values are not assigned semantic names here. Collection is **not
draw-free**. Its recursive submesh helper `0x0047d9c0` skips submesh flag
`[submesh+0x60]&0x8000`; otherwise, at `0x0047e05b..0x0047e072`, it checks object
`+0x12c & 0x800`, submesh `+0x60 & 0x20`, and object `+0x130 & 0x10`.
If all three are clear, it immediately calls material dispatch `0x004c4fc0`
at `0x0047e076`. If any is set, it queues the submesh. These bits establish
immediate-versus-deferred selection; they are not proven runtime blend enums.

The sole direct caller found for queue allocator `0x0047b2e0` is
`0x0047e0e2`. Nodes are 0x1c bytes and join the common list rooted at
`[0x00608518]+0x40`, with sentinel at `+0x44` and tail at `+0x48`.
Writes at `0x0047e0f6..0x0047e105` populate:

| Node offset | Derived role |
| --- | --- |
| +0 / +4 | Next / previous links |
| +8 | Submesh pointer |
| +0xc | Selected geometry/LOD record |
| +0x10 | Object pointer |
| +0x14 | Sort key copied from submesh +0x28 |
| +0x18 | Per-entry flags, initially zero |

Before enqueue, helpers `0x004f0da0` and `0x004f0c00` transform coordinates;
`0x0047e0b3..0x0047e0df` then scales the resulting submesh +0x28 value by
object +0x70 using signed fixed-point arithmetic and adds object +0xf8.
The exact physical interpretation of this key is unnecessary for the order
claim and is not assigned from inferred decompiler types.

Sort compares only signed node +0x14. View flag `+0x270 & 0x80` selects
ascending order (`0x0047e659/65c`) or descending order
(`0x0047e6a9/6ac`). It rearranges the same linked list; it does not inspect
shader, blend or depth state. Traversal changes object transforms when node
+0x10 changes, updates a submesh flag from node +0x18, calls `0x004c4fc0` at
`0x0047e769`, then follows the next link at `0x0047e76e`. Material dispatch
ultimately reaches `0x004c0150`, whose effect/pass establishes the actual draw
state. The existing [late-state study](bloom-late-view-state.md) explains why
separate-alpha and other states can also be inherited.

This traversal has three direct callsites: ordinary view `0x004722b5`, later
frame path `0x00472491`, and environment-map renderer `0x0047e8f6`.
Thus its entry point alone does not establish main-scene ownership. Within an
ordinary view, another bucket can submit immediate geometry after an earlier
bucket's deferred list. The code does not establish that such later geometry
cannot occlude an earlier emitter. No observed later-opaque example is claimed.

## Historical ordering corroboration

The independent streamed iteration-05 analysis finds the exact base
engine/effects pair's 32 scene ADD/ONE/ONE draws in **16 bursts of two**, one
burst in each of 16 frames. Every burst follows a depth-tested, depth-writing,
unblended draw. There are **3–16 more draws** before the structural scene
handoff, without an intervening target/depth/viewport change or Clear.

None of those sampled tails contains a later depth writer. Every tail does
contain a nonadditive blend. Across the tails there are 46 same-pair screen
draws, 16 particle SRCCOLOR/INVSRCCOLOR draws, 16 depth-disabled
SRCALPHA/INVSRCALPHA draws, and 44 other-shader additive draws. This proves
that a scene-end additive sidecar would cross destination-dependent blends
even in the existing sample. It does not prove an image difference at a
particular pixel without geometry/readback overlap evidence.

Examples: frames 1794–1797 have the eligible burst at draws 688–689 followed
by eight draws; frames 2435–2438 at 358–359 followed by sixteen; frames
2806–2809 at 252–253 followed by three. Draw indices are historical labels,
never runtime admission rules. The same pair also has 71 scene screen draws
and 924 late overlay draws across the full capture.

For mask-cost scoping, the 24 structurally successful gameplay frames contain
9,001 main-scene color draws. The conservative predicate nonnull PS, nonzero
color mask, and (blend enabled or Z writes off) selects 311 draws: 7–21 per
frame, mean 12.96; all 311 have both blend enabled and Z writes off. The five
background draws per frame add 120 candidates (96 blended, 24 unblended).
Including background gives 431 extra submissions, 12–26 per frame. This is
a capture-only upper bound for that predicate, not complete live reactive
classification or GPU cost. Full-screen background masks could erase useful
far-plane TAA; excluding them without a camera-only proof is not completeness.

## Reproduction and limits

Use the existing read-only headless workflow and `X3CameraState.java` from
[the render map](ghidra-render-map.md). Private outputs:
`/tmp/x3-linear-emission-order.txt` and `/tmp/x3-linear-emission-queue.txt`.
Requests were bounded to references/instructions for `0x0047e620`,
`0x0047e6e0`, `0x0047b2e0`, collector `0x0047d9c0`, dispatch `0x004c4fc0`,
the ordinary view callsite range, and the known material alpha-state branch.
Capture derivation is `/tmp/x3-emission-order.json` with scanner alongside it;
trace provenance is linked from [material-color-inputs.md](material-color-inputs.md).

No emission-class flag, universal final-transparent phase, complete source-alpha
state census or safe deferred-geometry ownership contract has been established.
The practical consequence is an **ordered bracket at the actual draw**, not
reordering the engine list. The proposed next step is a detached correctness
and cost fixture in [linear-emission-composition.md](../architecture/linear-emission-composition.md).
