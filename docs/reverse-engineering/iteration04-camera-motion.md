# Iteration 0.4: camera, object motion and scene-depth selection

The user-supplied session contains **20 complete frames and 13,431 successful
draws**. Object transforms change independently of the camera, including in the
last burst, which the user identified as third person. The new node context is
useful evidence for rigid-object motion; it does not yet establish safe lifetime
identity or implement motion vectors. The installed scene-depth selector made
**zero copy attempts and confirmed zero boundaries** in this session.

## Source and reproducibility

The raw source is `session-20260910-234001-212.log` in the Steam bottle's
`drive_c/X3/x3-modern-captures/`, 214,204,690 bytes, SHA-256
`f8a9f43e18c16d132e5337ba9dd3f69aabc54343c71b4ef57532d8125d9e5196`.
Before analysis, process inspection found no X3AP process. File size and mtime
were stable while copying a local `/tmp` snapshot. Raw traces and full summaries
remain untracked. The compact [numerical report](../../verification/results/iteration04-camera-motion.json)
records source and analyzer/dependency hashes.

```sh
python3 tools/analysis/analyze_object_capture.py \
  "$HOME/Library/Application Support/CrossOver/Bottles/Steam/drive_c/X3/x3-modern-captures/session-20260910-234001-212.log" \
  --metadata verification/results/shader-registers.json verification/results/game-docking-shader-registers.json \
  --output verification/results/iteration04-camera-motion.json
python3 -m unittest discover -s verification/analysis -p test_object_capture_analysis.py
```

The eight original-data tests cover matrix layout, integer scale exclusion,
scope mismatch, unknown buffer status, capture gaps, ambiguous per-frame
candidate transforms, absent camera evidence, and malformed/nonfinite matrix
rows. Unknown camera contexts produce no delta, including across gaps. The
analyzer requires exact matrix row keys 0–3 with four finite components,
complete frames, contiguous capture
events and successful draw/Present results before numerical comparisons.

## Coverage and transform convention

| Device:frame burst | Draws | Scoped draws | Unscoped per frame | Node candidates per frame |
| --- | ---: | ---: | ---: | --- |
| 1:120–123 | 2,552 | 2,536 | 4 | 441 each |
| 1:2775–2778 | 1,892 | 1,860 | 8 | 83, 84, 90, 88 |
| 1:2878–2881 | 3,545 | 3,513 | 8 | 121, 123, 124, 116 |
| 1:3241–3244 | 2,706 | 2,674 | 8 | 133, 133, 133, 135 |
| 1:3462–3465 | 2,736 | 2,704 | 8 | 141 each |

All 13,287 scoped observations have `valid=127` and matching device/frame/draw
scope. The other 144 have `scoped=0, valid=0`. There are no observed partial or
mismatched scoped reads. See [object identity](object-identity.md) for the static
field provenance; a successful memory read alone does not establish semantics.

For engine memory matrices `W`, `V`, `P`, the observed layout is row-vector
storage, with transposed shader-register rows:

- 12,872 named world comparisons satisfy `shader_world = transpose(W)` with
  **zero component error**. This numerical comparison does not distinguish
  positive and negative zero. The untransposed hypothesis differs in every case.
- `transpose(V)` agrees with the inverse of the shader's named view-inverse
  matrix, maximum normalized error **1.126e-7** over the same 12,872 comparisons.
- `shader_WVP ≈ transpose(W × V × P)` has maximum normalized error
  **1.932e-4** and maximum absolute coefficient error **0.030011**; 96 comparisons
  exceed normalized error 1e-5. Thus the layout is supported, but recomposition
  is not bit-exact engine arithmetic. Near-camera translation cancellation is a
  plausible contributor, not an established explanation. Prefer submitted WVP
  for a draw where exact clip positions matter.
- For 13,159 observations with `flags130 & 0x200 == 0`, signed node basis words
  divided by 65,536 equal the engine world-basis 3×3 components exactly. The 128
  observations in the alternate branch are excluded from this formula.

Node position, basis and scale words are integer/fixed-point fields, not float
bit patterns. The analyzer decodes only the established matrix roles as floats;
it does not reinterpret `role=scale` as an IEEE matrix.

## Camera changes and candidate object motion

The scheduled 120–123 burst has dominant camera `0f5ed310`, handle 21925. All four
later bursts have dominant camera **`6b2596c8`, handle 30906**, including the final
user-reported third-person burst. Separate background/overlay camera contexts
exist and must not be merged into the main camera merely because their draws
share a frame. Each camera context has one observed view matrix within a frame.

| Burst | Main-camera displacement per adjacent pair | Forward-axis angle change | Candidates with changed world per adjacent pair |
| --- | --- | --- | --- |
| 120–123 | 0 | 0° | 106, 106, 106 |
| 2775–2778 | 165.20–165.69 scene units | 2.224–2.227° | 50, 49, 50 |
| 2878–2881 | 163.41–163.76 scene units | 1.656–1.659° | 60, 59, 45 |
| 3241–3244 | 171.85–171.95 scene units | 0° | 68, 68, 68 |
| 3462–3465 | 0 | 0° | **58, 57, 59** |

The final camera view is unchanged while object worlds change. Camera-only
reprojection therefore cannot represent all observed motion. In each final
frame, 660 ordinary-branch draws belonging to 132 node/handle pairs use the
dominant camera with depth test and depth writes enabled. This is a promising
subset for conservative rigid-object motion validation, not a certified history
set: geometry, shader animation and per-draw correspondence still need gates.

Between 3244 and 3462, the same observed camera pointer/handle has a 4,964.30-unit
translation change and unchanged rotation. The intervening frames are absent;
the trace cannot attribute the entire displacement to switching camera mode.
It does establish that camera-handle changes alone cannot detect that switch.
History needs explicit capture-gap/reset/camera-cut treatment.

Candidates use `(session, node, node_handle, camera, camera_handle, model, lod)`
only across consecutive captured frames. Every candidate has one observed world
bit-matrix within each frame. Across the session, 655 pointers map to 655 handles
with no observed collision, but hook session 1 is not a reload/lifetime barrier.
No correspondence is inferred across burst gaps. Draw order is insufficient:
up to 104 shared candidates change their first draw index in one adjacent pair.

## Buffer revisions and unsupported motion paths

All 26,734 buffer-content observations have successful status, `requested=1`,
`known=1`, `ambiguous=0`, `pending=0`. No observed resource changes revision within
a captured frame. Four VBs increase revision by exactly one in **each of the 12
adjacent gameplay frame pairs**; other shared observed VB/IB revisions remain
unchanged. Revisions track observed writes, not content hashes or all possible
native write paths.

| VB allocation | VS / PS hashes | Location and depth behavior | Consequence |
| --- | --- | --- | --- |
| 1659 | `36f98d151fd6b0c6` / `222bee0defcb1852` | Before bloom; depth test on, writes off; IB 1657 stays revision 1 | Effect/particle candidate with changing geometry and no scoped node |
| 1661 | `5e484a06672e28fb` / `0a523f33ac47ae05` | Before bloom; depth test/writes off | Stardust path requires separate motion/reactivity handling |
| 1663 | `f36fc43f30b19d71` / `0a523f33ac47ae05` | After bloom; depth test/writes off | Overlay/effect path; no rigid-node history |
| 1667 | `f36fc43f30b19d71` / `0a523f33ac47ae05` | After bloom; depth test/writes off | Overlay/effect path; no rigid-node history |

These four paths are unscoped. Their revisions progress 1354–1357, 1457–1460,
1820–1823 and 2041–2044 in the four gameplay bursts respectively. In frame 3462,
the first two are draws 671–672, bloom is 673–676, and the last two are 677–678.
The four bloom draws are also unscoped but use stable VB 659, revision 1. A
conservative temporal prototype must reject/react to unsupported changing
effects and separate overlays from rigid scene reprojection. Neither all
post-bloom rendering nor all uses of the shared GUI pixel shader are proven HUD.

## Why the installed 0.4 selector rejected every frame

These are **capture-time rules**, not claims about subsequently revised code.
Every frame ends rejected (`state=9`, final `rejection=2`), with successful
Present and `attempted=copied=confirmed=0`. The final reason masks the earlier
pattern failure because the installed adapter later invalidates on ColorFill.

- Frames 120–123 begin with color-only Clear (`flags=1`). The selector requires
  color+depth (`flags=3`) and rejects at capture sequence 1, before any draw.
- The other 16 begin with the required full Clear, then exactly five background
  draws using VS `7b6393fe2d3e1d85` / PS `6109cf64c03529dd`. Their second Clear
  follows draw 5, capture sequence 7. The installed rule also requires a haze
  signature, which is absent, so rejection occurs here.
- Three successful ColorFill calls per frame, **60 total**, occur much later:
  after the scene's successful depth unbind and before the main-color copy and
  bloom. There are no pre-initial-Clear ColorFills. Treating initial setup as
  outside a candidate therefore does not repair this trace's pattern failure.

The last pre-bloom draw indices are 600 in the first burst; 449/435/473/471 in
the second; 918/897/841/829 in the third; 664/664/664/666 in the fourth; and 672
in the final burst. Each frame's three ColorFills follow that draw. The offline
resource/shader pass analyzer finds one four-draw bloom chain per frame, but
**ColorFill targets were not logged in 0.4**. It cannot retrospectively establish
that those writes were unrelated scratch work or that a relaxed online selector
would safely accept all 20 frames. Target-aware diagnostics and preservation of
the first rejection are required for the next test.

This session validates object/camera observations and identifies concrete
selector mismatches. It supplies no successful game scene-depth snapshot,
injected motion vectors, temporal accumulation, or visible TAA result.
