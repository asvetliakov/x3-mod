# Particle motion inputs and separate temporal handling

The latest 0.4 capture provides the particle draw's **view/projection matrices
and input layout, but no previous particle records or particle identity**.
It supports a concrete reactive-composition path while a separate particle
motion source is developed. It does not justify reusing rigid-object motion
for this shader or declaring temporal antialiasing of particles complete.

## Evidence and scope

Read-only inspection used the same 214,204,690-byte
`session-20260910-234001-212.log` as
[iteration 0.4 camera/motion](iteration04-camera-motion.md), SHA-256
`f8a9f43e18c16d132e5337ba9dd3f69aabc54343c71b4ef57532d8125d9e5196`.
The derived [input report](../../verification/results/particle-motion-inputs.json)
contains layout/state metadata, 16 draw ranges, revision values and digests of
submitted matrix rows. No vertex payload or game shader bytes are included.

Unscoped detail records were attached only to their enclosing `draw` block,
ending at the next capture event/frame boundary. Draw results were attached by
their exact `(device, frame, index)` key, independently of those boundaries.
Complete-frame counting requires a preceding `frame_begin`, `capture=1` at
`frame_end`, and matching observed/reported draw counts. The 14 ordinary
`capture=0` telemetry ends are excluded; there are **20 complete capture frames**.
Particle rows were selected by exact VS hash
`36f98d151fd6b0c6`; all 16 pair with PS `222bee0defcb1852`. All draws succeed.
There is one particle draw in each frame of the four gameplay bursts and none
in the initial 120–123 burst. Archive counterpart `2eea471bc86935f2` has the
same inspected position arithmetic but **zero draws** in this session.

Reproduce the report and its five original-data parser regression tests with:

```sh
python3 tools/analysis/inspect_particle_inputs.py \
  "$HOME/Library/Application Support/CrossOver/Bottles/Steam/drive_c/X3/x3-modern-captures/session-20260910-234001-212.log" \
  --inventory verification/results/shader-sweep-inventory.json \
  --output verification/results/particle-motion-inputs.json
python3 -m unittest discover -s verification/analysis -p test_particle_inputs.py
```

The extractor retains each scoped particle draw result in the report. Regression
tests cover scoped success dictionaries, delayed results across event/draw
boundaries, foreign scopes, failures/duplicates, uncaptured telemetry ends and
incomplete/count-mismatched frames. This corrects the initial report's two
summary defects: counting telemetry ends as captures and comparing a scoped
result dictionary against a result-only dictionary.

## Current particle input contract

All 16 draws use this stream-0 declaration, stride **32 bytes**, stream offset
zero, stream frequency one, and the default declaration method:

| Offset | Declaration | VS input | Instruction-derived role |
| ---: | --- | --- | --- |
| 0 | `POSITION0`, `FLOAT3` | v0.xyz | Position transformed by submitted view rows c0–3 |
| 12 | `TEXCOORD0`, `FLOAT2` | v1.xy | Offset added to view-space XY before projection |
| 20 | `TEXCOORD1`, `FLOAT2` | v2.xy | Texture UV |
| 28 | `COLOR0`, `D3DCOLOR` | v3 | Packed normalized vertex color forwarded to COLOR0 |

The VS constructs homogeneous `(position.xyz,1)`, transforms it by c0–3,
adds the offset to intermediate XY, and projects with c4–7. CTAB calls these
`g_mView` and `g_mProj`. In column-vector notation the position path is:

```text
clip = P * (V * float4(center.xyz, 1) + float4(offset.xy, 0, 0))
```

Calling POSITION0 a particle *center* is the natural billboard interpretation
of this arithmetic; the captured trace does not include the vertex values
needed to prove how many corners share each center. Primitive counts being
even does not independently prove quad pairing or stable particle ordering.

All draws are **non-indexed triangle lists**, `DrawPrimitive`, start vertex 0.
The 42–122 triangles consume 126–366 consecutive vertices, or **4,032–11,712
bytes** from the start of stream 0. VB allocation 1659 is 3,840,000 bytes,
`DYNAMIC | WRITEONLY` (usage 520 / 0x208), DEFAULT pool, FVF zero. Its larger
capacity does not identify which unused bytes contain meaningful data.

IB allocation 1657 is bound, 2,160 bytes, INDEX16, WRITEONLY, MANAGED pool,
revision 1. **DrawPrimitive does not consume that IB.** Its stability is not
evidence of particle topology or correspondence.

| Frames | Triangle counts, in frame order | VB revisions |
| --- | --- | --- |
| 2775–2778 | 114, 114, 116, 116 | 1354–1357 |
| 2878–2881 | 114, 122, 116, 116 | 1457–1460 |
| 3241–3244 | 112, 114, 116, 114 | 1820–1823 |
| 3462–3465 | 46, 46, 44, 42 | 2041–2044 |

Every VB status is known, unambiguous, with no pending lock and successful
queries. Each of the 12 adjacent captured gameplay pairs increments revision
by one. Last successful lock flags are `DISCARD` (0x2000). This records a write
event and makes slot reuse plausible; it does not reveal changed byte ranges,
changed values, or particle creation/deletion identities.

## Depth, blending and appearance

Every particle draw has the same relevant state:

| State | Observed value |
| --- | --- |
| Depth | Enabled, LESSEQUAL, writes disabled |
| Alpha test | Disabled; stored alpha function/reference are inactive |
| RGB blend | Enabled, ADD, source SRCCOLOR, destination INVSRCCOLOR |
| Color writes | RGB only; alpha write disabled |
| Separate alpha blend | Disabled |
| Culling / stencil | Culling disabled; stencil disabled |
| Viewport / target | 1280×768, main A8R8G8B8 target allocation 1 |
| Depth attachment | D24X8 allocation 2, no multisampling |
| Sampled texture s0 | Allocation 1658, 256×256 X8R8G8B8 |

Before render-target clamping/conversion, the recorded RGB blend equation is
componentwise `Cs*Cs + Cd*(1-Cs)`. **Particle alpha does not control this RGB
blend**, and alpha is not written. A reactive mask based only on PS alpha would
therefore describe the wrong contribution. It should conservatively measure
the actual source RGB and respect the same depth/coverage test. Moving this
blend unchanged to an unclamped HDR source also needs qualification: values
above one make its inverse-source-color factor a separate range issue.

## Available history versus missing history

The VS float constant query succeeds for every particle draw. Submitted c0–3
view rows have 13 distinct bit patterns across 16 draws; c4–7 projection rows
are constant across the session. Thus prior view/projection is available for
each adjacent captured pair, although the runtime still has to retain it.
Sparse-zero constant records have a successful query/count declaration, so
omitted rows can be distinguished from a failed capture. The declared literal
c8 inside the shader is not the stale application value of c8.

Every particle draw has `scoped=0`, `valid=0`: no engine node, mesh, camera
handle or object-history identity was associated with it. The fixed-function
world/view/projection transform records are identities and are not the
matrices this VS consumes.

Missing inputs are the actual current and previous center/offset/color/UV
records, stable particle identity and generation, vertex/corner pairing,
birth/death events, and any CPU sorting/compaction policy. Capturing the previous
VB alone would not solve correspondence when slots are reused or sorted.
Unchanged primitive count and one revision increment are insufficient to infer
unchanged particle ordering. Capture gaps cannot be treated as consecutive
simulation frames.

With stable correspondence, exact billboard motion would evaluate both the
current and previous forms of the equation above using their respective
center, offset, view and projection, then compare normalized clip XY. Camera
motion alone misses particle motion, size changes and billboard expansion
changes. New or unmatched particles must invalidate their own history.

## Concrete handling plan for the five captured non-rigid-row shaders

These are different rendering roles, not five reasons to disable scene TAA.
The following is an implementation plan; none of it is enabled by this review.

1. **Three direct-XYZW bloom VS** (`cbbf26102694c961`, `6059306306203243`,
   `1279d081455f5815`): keep full-screen geometry unjittered and put temporal
   scene resolve before the replacement bloom/exposure/display pipeline. These
   passes process images, so their vertices do not need object motion vectors.
   This session has 20 extraction, 40 blur and 20 composition draws using a
   stable 96-byte VB 659. Their offsets affect sampling UVs, not a camera.
2. **Direct XYZ/W=1 path** (`f36fc43f30b19d71`): preserve a separate unjittered
   post-resolve layer for the observed post-bloom draws. They use dynamic VBs
   1663/1667, stride 24, depth disabled, source-alpha/inverse-source-alpha RGB
   blending and RGB-only writes. Their 32 captured draws have no object scope.
   Establish UI versus lens/effect color treatment from draw/texture/pass
   evidence; the shared GUI PS hash does not make every draw HUD. If an effect
   is moved into scene history later, provide explicit motion/reactivity for
   its CPU-generated clip-space geometry.
3. **Particle billboard path** (`36f98d151fd6b0c6`): use the exact c4–7 projection
   contract for scene jitter. Initially render particles in scene color before
   temporal resolve and generate a separate conservative reactive mask from
   their RGB contribution, with their depth test and coverage. Suppress history
   for current reactive coverage **and previous reactive coverage at the
   history lookup**, so disappeared/moved particles do not leave old trails.
   Opaque scene depth does not contain these non-depth-writing surfaces.
   Merely setting a particle velocity to zero is not an adequate fallback.

The initial reactive policy protects resolved rigid geometry while retaining
current particle appearance; it does **not** establish temporally accumulated
particle antialiasing. The next particle stage should retain bounded written
vertex data before DISCARD/overwrite and identify the CPU emitter/particle
generation source, or provide an owned particle stream with explicit previous
attributes. Use those attributes in a dedicated billboard motion shader and
layer-aware temporal resolve; invalidate births/deaths and retain reactivity
for appearance changes. Avoid per-frame GPU readback of this WRITEONLY buffer.

The separate pre-bloom stardust path (`5e484a06672e28fb`, VB 1661) should join
the dynamic-effect reactive/layer policy despite having a row-dot VS. It is
unscoped, changes VB revision every adjacent frame, and disables depth testing
and writes. This is an explicit example of why an accepted position formula
does not by itself certify rigid history.

For verification, use original synthetic billboards with moving centers,
changing offsets, births/deaths, reordered slots, opaque occluders and the
observed RGB blend. Check numerical current/previous clip positions, reactive
coverage and trailing-history rejection before coordinating a user scene test.
The existing trace cannot retrospectively supply those missing particle values.
