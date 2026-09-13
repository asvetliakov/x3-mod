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

## Historical two-draw burst internals

A second streamed pass compared the two snapshots in each of the 16 historical
eligible bursts. All pairs have consecutive `draw_begin` event ordinals. In 15,
the first `draw_result` is immediately followed by the second `draw_begin`.
Frame 1794 has only three intervening `resource identity` records, which tag
the already-bound VB, IB and texture with capture-private IDs; they are
instrumentation, not observed game resource creation or content writes.

Within every pair, the captured scene owner, mesh, node, object, camera and
lifetime/epoch tuple are equal. So are the VS/PS, primitive type, declaration,
all 17 object matrices, position/basis, fixed transforms, render states, sampler
states, RT0/depth identities and formats, viewport, and all captured constants
except VS float rows c10–c11. Both draws succeed. In particular, both retain Z
test on, Z writes off, alpha test off, RGB ADD/ONE/ONE, color mask 15, and
separate-alpha enable 1.

Each second draw nevertheless requires distinct geometry and texture input:
32 primitives/33 vertices become 64/66; stream-0 VB size 792 becomes 1,584;
the 16-bit IB size 192 becomes 384; and the stage-0 DXT5 texture changes from
1024x1024 to 1024x512. The VB, IB and texture identities all differ, though
both buffer snapshots report known revision 1 with no pending or ambiguous
revision. VS c10–c11 also change in all 16 pairs; the second draw always has
c10 `(1,0,0,0)` and c11 `(0,1,0,0)`, while the first varies. These are the only
changing captured constant rows, so any geometry/UV setup they feed must remain
per draw; the capture does not establish their semantic names.

The ordered event vocabulary covers draws, Clear, target/depth changes,
StretchRect and ColorFill. It records none of those operations between a pair.
It is not a complete API call trace: stream/index/texture binds,
shader-constant setters, state-block Apply, queries, locks and update calls can
be absent, as can redundant state changes. Snapshot collection itself calls
getters, descriptor queries and private-data queries. Those observations are
capture instrumentation and do not prove the application made no unlogged
calls. The snapshots also omit alpha blend
factors/operation, blend factor, and scissor enable/rectangle, so equality of
the complete blend and raster state is unproven.

Both draws report scoped object context with equal values and scope depth one,
but the log has no material-dispatch invocation serial or scope enter/leave
event. One invocation and two consecutive invocations with identical arguments
therefore remain indistinguishable. **Adjacent draw ordinals do not yet qualify
batching.** Cross-call batching would require an independently established
engine invocation boundary and preservation of the changing geometry, texture,
c10–c11 and currently unobserved API/state transitions.

Private derived results are `/tmp/x3-emission-between.json` and
`/tmp/x3-emission-between.txt`, produced by `/tmp/x3-emission-between.py`.
Representative raw trace locations are lines 1,031,386–1,031,391 (frame 1794)
and 2,471,447–2,471,449 (frame 2435); no raw trace content is reproduced here.

## Scoped invocation, subset loop and effect passes

Targeted instruction inspection confirms that the existing
[object scope](object-identity.md#implemented-diagnostic-seam-not-installed-or-game-validated)
brackets **one synchronous invocation of `0x004c0150`**, through the patched
call at `0x004c5228`. `x3m_object_dispatch` enters TLS before forwarding all six
arguments and leaves after return; its SEH registration also removes the scope
on foreign unwind. The captured `mesh` is the first argument, the enclosing
material/geometry descriptor. It is not the current subset record. Equal scope
arguments/depth are not an invocation serial: successive calls can reuse them.

| Addresses | Confirmed structure |
| --- | --- |
| `0x004c0207–021d` | Test signed 16-bit subset count at descriptor `+8`; initialize outer index to zero |
| `0x004c0223–023d` | Select record from descriptor `+0xc` plus index × `0x1a8` |
| `0x004c0b85–0bc0`, one setup branch | Derive stride through record `+0x14` object; bind stream zero from record `+0xc` |
| `0x004c0bca–0be5`, same branch | Bind declaration from record `+0x18` |
| `0x004c1ebe` | Effect `Begin` obtains pass count |
| `0x004c1f48–1f65` | Select supplied UV-matrix helper `0x004b92c0` or identity helper `0x004b9280` |
| `0x004c3ffe`, `0x004c403c`, `0x004c4047` | Begin effect pass, dispatch geometry draw, end pass |
| `0x004c405b`, `0x004c4066` | Repeat inner pass loop; then effect `End` |
| `0x004c4068–4082` | Increment outer index, reread subset count, repeat record setup |

Thus one invocation can submit several geometry/material records and several
effect passes per record, while retaining identical object context. Texture and
UV setup repeat inside the outer loop. Some branches substitute the selected
material record. This establishes a mechanism compatible with the observed
geometry/texture/UV changes, **not** that either captured draw took a particular
branch or that the pair belongs to one invocation. Neither loop means exactly
two draws or an additive-only region.

The original VS `d5e1c75351ed3f04` supplies the missing UV semantics directly:
CTAB names c10/c11 `g_TexMatrix`, and the instructions evaluate
`u' = dot((u,v,1), c10.xyz)`, `v' = dot((u,v,1), c11.xyz)` for TEXCOORD0.
Their w components are unused by these dot products. The second draw's captured
rows are therefore an identity UV transform; the first draw's differing rows
change this affine sampling transform. They do not alter WVP position or the
separate fog/fade output. Per-draw UV constants must remain intact even if two
draws later share an accumulation/publication interval. Local source:
`/tmp/x3-shader-sweep/disassembly/vs_d5e1c75351ed3f04.bin.txt`.

Normal returns are at `0x004c40a3`, `0x004c40ca`, `0x004c40e1` and
`0x004c40fb`, including error/skip exits. The wrapper supplies a common normal
leave point, but its return value is not a GPU-success certificate: the draw
result at `0x004c403c` is not checked before effect `EndPass`. Foreign unwind
also requires an explicit incomplete-operation policy, not publication of an
assumed complete accumulated image.

The invocation exit is a concrete possible **maximum lifetime boundary** for
detached accumulation. It does not authorize delaying publication across every
operation inside that invocation. This audit does not certify every indirect
helper/effect callback as color-read-free; the historical event omissions above
remain. A detached multi-subset proof must close before an incompatible draw or
color reader and handle early return/unwind. Establishing the historical
batching opportunity additionally needs invocation/subset/pass identity that
these captures cannot recover. No cross-call batching is qualified or selected.
Instruction evidence is local `/tmp/x3-object-context.txt`, supported by
`/tmp/x3-render-functions.c`; no raw disassembly is tracked.

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
