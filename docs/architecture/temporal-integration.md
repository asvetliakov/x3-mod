# Temporal integration on the live motion route

Design record, 2026-09-12. This defines how TAA jitter, current depth and the
temporal resolve connect to the [live motion route](live-motion-route.md). It
follows the read-only study of the existing resolve pass against what the game
actually provides; the numbers and file references below come from that study
and from the linked verification documents. Nothing in this document is
implemented yet.

## Placement

The game renders the scene into an A8R8G8B8 main target with D24X8 depth, then
copies the main color to a bloom source with `StretchRect`, composes bloom and
draws overlays. The selector recognizes that copy as `AwaitCopy`
(`src/renderer/scene_boundary.h`). Depth is unbound just before it but its
content survives until the frame's final depth-only Clear.

The resolve runs at that copy point, before the application's `StretchRect`:

```text
main RT (8-bit)  --StretchRect-->  FP16 scratch (current color)
motion RT1, depth RT2, history      --resolve draw-->  FP16 history[next]
history[next]    --StretchRect-->  main RT (in place, 8-bit)
application StretchRect / bloom / overlays proceed unchanged
```

The resolve output cannot be bound as its own input, and the main target is not
known to be a texture level, so the scratch copy is mandatory. Copying back to
8-bit loses nothing the game had; when the FP16 scene path exists the main
target itself becomes FP16 and the copies disappear.

## Inputs the route will produce

| Input | Source | Change from today |
| --- | --- | --- |
| Current color | `StretchRect` of the main target into an owned A16B16G16R16F texture | `TemporalPass` s0 stays FP16; the copy is new |
| Current depth | **RT2, R32F, written by the variants as oC2** from the current clip z/w exported by the VS | new third output in every transformed row; sentinel fill like RT1 |
| Motion | RT1 RGBA32F, unchanged ABI | none |
| Reactive mask | derived from the RT2 sentinel: pixels no routed opaque draw wrote are treated as reactive (history rejected) | resolve samples RT2 instead of a separate mask; `ReactivePolicy::RequiredMask` is satisfied by RT2 |
| Jitter | per-draw explicit rows, see below | new |

Current depth from oC2 covers the pixels of the 16 transformed pairs, which are
97.6% of scene draws but not all scene pixels: background, particles, SM1/SM2
and depth-disabled effects keep the sentinel and therefore fall back to the
current frame in the resolve. That is acceptable for the first visible TAA:
the background has no per-object motion and particles have no history anyway.
The RESZ snapshot plus decoder remains available if complete depth is needed
later; it costs about 26 fetches per pixel.

The VS has enough headroom: every one of the eight vertex programs behind the
16 rows has at least two free output registers after the motion interpolator.
The binding constraint is the ten pixel-shader inputs of SM3; the worst program
declares nine outputs, so motion plus depth reaches exactly ten. The depth
export needs only z and w, so it can share one float4 with a future payload.
The mixed-format MRT gate and self test extend to three formats
(A8R8G8B8, A32B32G32R32F, R32F).

## Jitter

Rows go to the device as `o0.x = dot(r, c24)`, `.y = dot(r, c25)`,
`.w = dot(r, c27)`, so a clip-space offset is `c24 += jx·c27`,
`c25 += jy·c27` with `jx, jy` in NDC units derived from the pixel jitter and
the viewport size. The game's effect state manager caches the rows it last
uploaded and skips identical uploads, so modifying rows inside the upload hook
would leave the previous frame's jitter on every static object (29% of matched
draws have bit-identical rows). Jitter is therefore applied **per draw** in the
route's before-draw step with an explicit constant write of the four rows, and
the original rows are restored after the draw. The shadow keeps the unjittered
rows, so the history stays jitter-free and the prior jitter is carried in
`c216.zw` as the motion ABI already expects.

Jittered draws: every scene-phase draw whose vertex program is in the row-dot
registry with a known matrix register (the c24 material family covers almost
everything observed; the small c0 family and the particle program with
`c4/c5 += j·c7` follow). Not jittered: direct-clip bloom quads, post-bloom
overlays and GUI, and our own fill draws. Draws with an unknown program are not
jittered and are excluded from history by the sentinel.

## History, Reset and cuts

`TemporalPass` owns two FP16 color textures and two R32F depth textures; its
`before_reset` is a full shutdown that also drops its shaders, so the route
re-initializes it after a successful Reset with retained bytecode and folds the
route's resource generation into the history epoch. Ordering is the same as for
the motion target: release before the wrapper's Reset, recreate lazily.

History is invalidated on camera-serial change, load or registry epoch change,
dimension change, failed Present and selector rejection. Those signals already
exist in the route. A view-delta heuristic needs the view-inverse rows
`c34–36` shadowed as well; that is a small addition and is left for the run
evidence to justify.

## Cost and memory

At 5120×1440 the added default-pool memory is about 118 MB for RGBA32F motion,
29 MB for R32F depth, and roughly 180 MB for the FP16 scratch and history pairs.
A compact motion encoding is a later change. Per frame the new GPU work is two
copies, the resolve draw and the fills; none of these has a measured cost yet
and the material-motion fixture numbers exclude them.

## Order of work

1. Add the R32F current-depth output (RT2) and the per-draw jitter to the live
   route, with the structural and GPU fixtures extended, and make the readback
   analyzer use the depth image so the gameplay capture can be checked against
   real depth.
2. Adapt `TemporalPass`: FP16 scratch copy of 8-bit color, direct R32F depth
   (skip the decoder), reactive derived from the RT2 sentinel, jitter inputs.
3. Wire the resolve at the copy boundary behind an off-by-default switch with
   copy-back, then a user-managed run comparing still, turning and flying
   captures with the route off and on.
