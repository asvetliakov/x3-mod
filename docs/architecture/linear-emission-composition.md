# Ordered linear emission composition

Updated 2026-09-13. The initial ordered-bracket experiment is qualified but will
not be promoted unchanged: it alters untouched pixels and lacks post-draw native
fallback. The [revised same-draw candidate](#candidate-retain-native-output-and-publish-an-owned-target)
has passed its first detached qualification. **No live emission route is selected.** Coverage,
recovery and measured cost remain integration gates; the earlier design and its
results below explain this decision.

## Boundary decision

Targeted [engine ordering RE](../reverse-engineering/emission-draw-order.md)
establishes immediate geometry followed by a sorted deferred list **per bucket**,
not a single additive-only phase. The sorted list has no blend-class partition;
another bucket and optional particle pass can follow. The historical eligible
bursts are followed by nonadditive blends in every sampled frame. Later opaque
occlusion is possible from the call structure but is not observed in that sample.

| Approach | Decision |
| --- | --- |
| Encode each emissive source, then blend into current engine-space RT0 | Reject as linear composition: encoded-unit + encoded-unit decodes to about 4.59 instead of 2 |
| Accumulate selected emission separately and add it at scene end | Reject without order/occlusion proof; already crosses observed screen/alpha blends |
| Decode current destination, perform eligible additions in place in sequence, encode before the next unqualified writer | Correct bounded composition model to test; repeated full-surface transfers are a substantial cost |
| Move the entire writer population to a linear target | Longer-term alternative; requires fallback writers, background, source-dependent blends and all color readers to share a defined contract |

Whole-writer conversion is not simply removing the existing thirty pairs'
compatibility encode. Uncovered shaders still evaluate legacy gamma lighting;
decoding their final result is only a fallback conversion. Screen and particle
blends also use source RGB as destination attenuation. Replacing that value by
HDR radiance changes attenuation and may make it negative. Their radiance and
transmittance/coverage need separate semantics before that transition is honest.

## First source and ordered bracket contract

Study the complete shared engine/effects **SM2 DEFAULT** family, independently
of capture occurrence. The five exact pairs are:

| VS | PS |
| --- | --- |
| `d5e1c75351ed3f04` | `8360f422de08b5bd` |
| `32e75459998d0388` | `9975b706e5a1c999`, `ff2473e73a6bdfa1` |
| `089091aab2d5eb13` | `8559522220507d5e`, `875e780adb131b16` |

Their [existing shader study](material-next-slice.md#2-engineeffects-genuine-additive-emission-needs-a-composition-boundary)
establishes affine/no-affine color and fade/no-fade roles. Decode texture RGB
after the artistic affine operation, then apply the preserved fade and a
separate bounded linear emission gain. The fade is `g_AlphaValue` times optional
VS fog, carried through original COLOR0.x interpolation; it is not recovered
physical intensity. PS output alpha remains sampled texture alpha. Preserve
the original VS, texture sampling and alpha operations. INSTANCE, bullets,
particles, suns and screen-mode draws are outside this source conversion.

Initial admission: positively owned active main-scene FP16 destination,
gamma22 convention, exact paired source contract, RGB ADD/ONE/ONE, Z test on,
Z writes off, no shader depth export, no stencil writes, no MSAA, no alpha test,
known original alpha blend state/mask and disabled sampler/hardware sRGB
conversion. Also require known idle queries, no state-block recording, supported
draw API and complete source state. Alpha-test-off is a bounded first contract,
not an assumption derived from a hash. The same base pair's 924 late overlays
must remain excluded by scene ownership; its 71 screen draws by blend state.

The fixture starts with a **single-draw bracket**, with a synthetic contiguous
two-draw bracket as a cost comparison. Neither queues game geometry:

1. Sample the current engine-space scene into separate linear FP16 scratch,
   decoding RGB and copying alpha unchanged; match pixels without filtering drift.
2. Bind the original depth surface and restore the original emission raster,
   blend and alpha state. Draw linear RGB into scratch at this exact position
   in the application's sequence. Preserve actual alpha blending, not only the
   shader's source alpha.
3. Encode RGB and copy alpha to engine space before any subsequent noneligible
   draw or scene color consumer. Never sample a bound render target. Preserve
   original depth/stencil content. Preservation of live RT1/RT2 attachments is
   a later integration requirement, outside this detached fixture.

**Both full-screen transfers establish their complete state:** bind only the
intended RT0 (unbind additional render targets), set a full-target viewport,
disable scissor, culling, depth/stencil tests and writes, alpha test, blending
and sRGB write, and force RGBA writes. Bind the known transfer shaders and
quad input, with point/clamp/non-sRGB source sampling. Restore the exact source
raster/shader/sampler/blend state before the emission draw and the exact
application state after publication. Inherited viewport, scissor, color mask,
alpha-test or sRGB state must not restrict or modify a transfer.

**Finite FP16 boundaries:** use the existing ordered RGB sanitizer
`S(x) = MIN(MAX(x, +0), 65504)` with qualified operand ordering, mapping NaN
and negative infinity to zero and positive infinity to the ceiling. Decode
raw code values using the established safe gamma-2.2 transfer, then apply S
again to decoded RGB **before storage in linear FP16 scratch**. This explicit
storage cap differs from the uncapped intermediate source-radiance arithmetic
inside material shaders. Apply S to accumulated scratch RGB before the safe
inverse transfer and encoded FP16 store. Preserve exact positive zero for both
signed-zero RGB inputs. Alpha is copied unchanged by both transfers and is
never gamma-converted or passed through the RGB sanitizer.

FP16 decode/store/encode can change a destination even with zero added energy.
Measure zero-emission round trips and repeated sixteen-bracket drift against
the original encoded destination, across black, representative midtones, HDR
values and near-cap inputs. Record maximum absolute/relative error and affected
pixels, distinguishing deliberate cap effects from rounding. Do not claim
bit-exact no-op color or invent an acceptable drift threshold before measuring.

For a burst, the rule is `encode(decode(C_before) + sum(E))`, with alpha's
original ordered equation evaluated independently. The next screen/alpha/opaque
draw must consume this encoded result at its original position. A live burst
would have to close before every unqualified draw, Clear, target/depth change,
scene read/copy, unknown content mutation, query boundary or ownership/lifetime
transition. Stateful batching across application setters is not approved here.

**Post-submission failure is a separate design gate.** Before an emission draw,
failed allocation/capture/decode can restore state and decline the feature.
After successful emission draws into scratch, restoring the pre-burst scene
drops accepted emissions: it is not native fallback. An encode/restore failure
must report an incomplete frame in the detached fixture, never claim successful
publication or replay retained game geometry. A live design must independently
prove completion/recovery or maintain a current native image; doing so may add
further draws/storage. Device loss, partial commit and failed restore must be
distinguished from clean pre-mutation refusal. This remains unresolved before
any live implementation.

## Reactive coverage: producer now, integration later

The existing [resolve shader](../../src/temporal/resolve.hlsl) already samples
current reactive coverage at s5, rejects any nonzero-weight reactive previous
history tap at s6, and preserves current alpha. [TemporalPass](../../src/renderer/temporal_pass.h)
requires R32F masks, snapshots/canonicalizes them with the accepted history,
and invalidates on policy/lifetime transitions. Reuse that ownership and
current/previous logic. Disappearing emission needs previous coverage even when
the current frame contains no emitter; underlying opaque velocity or RT2 depth
does not describe effect motion.

The current live `DerivedFromDepthSentinel` policy and `RequiredMask` policy
are mutually exclusive. Do not switch to an emission-only RequiredMask and
call it complete. A narrowly combined producer policy should drive the existing
independent mask and sentinel shader controls together, retaining actual
far-plane camera reprojection and opaque motion. It must retain the complete
conservative coverage obligation for unsupported blended writers. Existing
camera/background admission must be explicit; unknown background motion is
not made safe by the presence of a view matrix.

**Portable producer candidate for the fixture: immediate single-RT coverage
replay**, entirely inside the draw boundary. It needs no retained geometry,
previous-particle identity or private backend ABI. A standalone mask target
avoids requiring FP16 MRT blending. For admitted sources, use the same original
VS/geometry, viewport, scissor, cull and Z test; disable depth/stencil writes and
write one to coverage. Preserve original alpha rejection where admitted; the
first slice has alpha test off. Conservatively marking black/zero-alpha
fragments is allowed: alpha is not RGB coverage for additive effects.

Completeness extends beyond enhanced sources. For other untracked scene RGB
writers, a separate constant-mask PS can conservatively cover their rasterized
geometry with alpha/discard, depth and stencil rejection relaxed. Retain the
original vertex/primitive path, disable **all** depth/stencil side effects and
do not reproduce a foreign oDepth. This is a superset rather than a claim of
exact visible effect coverage. Shader-model linkage, draw APIs, original
fixed-function paths and query/state interactions need explicit qualification.
No universal arbitrary-draw replay support is assumed. If a draw cannot be
covered safely, coverage is incomplete and history must be withheld for that
frame. Later opaque occlusion can leave conservative mask false positives;
do not clear coverage based on unsupported effect-motion inference.

This is **producer feasibility, not a complete live classifier**. Proven
camera-only/far-plane writers must be distinguished from unsupported animated
blends. The [background coverage study](../reverse-engineering/background-temporal-coverage.md)
finds camera-centered motion in the sampled nebula cohort, but changing stardust
inputs and shared shader aliases prevent a blanket exemption. Blindly replaying every unrouted background writer can mark nearly the
whole screen reactive and remove useful TAA; blindly omitting them does not
prove safety either. The first detached fixture can use a closed synthetic
world with explicitly known reactive/nonreactive writers. Live completeness
and useful far-plane history remain open blockers, not work to solve in this
checkpoint.

Use a directly rendered **R32F single-RT mask**, cleared to zero, with writes
of one and no blending. Geometry outside the mask remains unchanged;
overlapping writes stay one. R32F renderability is already needed by the route;
no R32F blending or intermediate-format conversion is required. Add another
mask format/conversion only if a documented capability issue or measurement
justifies it.
Idle-query admission is mandatory before extra full-screen or geometry draws;
an active occlusion query would observe them. Failed coverage production or
restore cannot establish history. Color and reactive snapshot publication must
refer to the same successful frame/generation.

**Later, outside the first fixture:** a combined sentinel/mask policy, live
writer classification and temporal integration need their own decision. The
existing consumer remains unchanged and its tests are not rerun in this first
feasibility checkpoint. An optional same-draw MRT producer can reduce replay only when the source
contract and device support it. D3D9 applies shared blend state to all targets;
ADD/ONE/ONE mask output one can accumulate conservative coverage, but it is not
an independent MAX blend. Require simultaneous-target count, per-format
`QUERY_POSTPIXELSHADER_BLENDING`, `MRTPOSTPIXELSHADERBLENDING`, independent
write masks and, for different bit depths, `MRTINDEPENDENTBITDEPTHS`. No MSAA;
fixed-function fog on extra MRTs is undefined. A shader-model-compatible
additional output and exact RT0 alpha behavior also need proof.
[Microsoft MRT contract](https://learn.microsoft.com/en-us/windows/win32/direct3d9/multiple-render-targets),
[capability definitions](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dpmisccaps).
This is optional acceleration, not a backend-private renderer prerequisite.

## Detached fixture and decision gate

Use original synthetic shaders/textures and small deterministic readbacks.
Do not copy game bytecode into tracked fixtures. The smallest useful checkpoint
has three coupled parts:

- **Ordered color:** independently colored overlapping emitters over a known
  destination; analytical linear addition before encoding; preserved fade and
  affine transform; original-alpha on/off separate blending and RGB-only/full
  masks with distinct destination alpha. Interleave a screen blend and later
  opaque occluder. A deliberately deferred sidecar must differ in overlapping
  pixels, while the ordered bracket matches its oracle. Test shared late-view
  refusal and unchanged color/depth/alpha outside the admitted operation; include
  zero-emission and repeated-bracket drift characterization. RT1/RT2 invariance
  belongs to the later temporal/live integration contract.
- **Coverage producer:** depth-tested visible/hidden emission, transparent-zero
  source with nonzero RGB, overlapping sources, unsupported screen/alpha
  writers and conservative relaxed-rejection coverage in the synthetic closed
  world. Qualify immediate reuse of vertex state and read back the R32F mask.
  This checkpoint establishes mask production only; it does not integrate or
  rerun the existing temporal consumer, add a combined sentinel policy, or
  qualify MRT acceleration. Later integration can reuse
  `temporal_pass_fixture.cpp::reactive_cases` and its established
  current/previous/history-footprint/fault/Reset behavior.
- **Cost and faults:** separately time two color transfers, source draw,
  coverage replay and state restoration at representative scene
  sizes, plus the original baseline. Inject each
  allocation/capture/transfer/draw/restore failure, separating clean refusal
  from post-submission incomplete frames. Keep native Windows-compatible API
  behavior distinct from execution on CrossOver.

Measure native encoded draws versus ordered decode/add/encode, each with and
without immediate mask replay, at **1280×768 and 1920×1080**. Use one two-draw
burst and a sixteen-burst stress case. Sixteen is a workload sweep, not an
observed per-frame burst count. Use bounded warmups/samples (for example eight
and thirty-two), QPC around submission plus D3D EVENT completion, and report
end-to-end completion time; use GPU timestamps only if separately verified.
Do not infer isolated shader time or game FPS from QPC. Excessive overhead is
a reason to reject live bracketing and revisit the composition architecture.

Historical eligible workload is one two-draw burst per affected frame. That
is a lower bound of **two full-scene transfer passes per burst**, or four for
isolated single-draw brackets, before mask generation. All sampled bursts are
followed by 3–16 further transparent draws. The coverage replay population is
larger than the 32 enhanced draws. In the 24 structurally successful historical
gameplay frames, nonnull-PS/nonzero-color-mask scene draws with blend enabled
or Z writes off give **311 replay candidates / 9,001 main-scene color draws**:
7–21 per frame, mean 12.96. All 311 have both blend on and Z writes off.
Only 147 of them lack a motion-table pair; the other 164 use motion-capable
shaders in blended/no-Z-write states. Pair-table membership is therefore not
a valid reason to omit their reactive coverage or evidence that they routed.
Background contributes **another 120 candidates**, five per frame (four
blended and one unblended). Replaying both regions would add 431 submissions,
12–26 per frame, mean 17.96. These are an upper bound for that captured predicate,
not a complete live admission population or a recommendation. Neither draw
count nor replay count estimates
covered pixels, shader work or game FPS. The user's same-draw motion design
avoids general replay complexity; do not casually reintroduce it for every
unsupported writer. Production endorsement needs measured cost and a complete
coverage/failure contract, not only successful numerical examples.

Confirmed now: real engine ordering, mixed post-emission blends, exact bounded
source roles, existing temporal consumer, and documented D3D capability limits.
Open before live work: acceptable bracket cost; full reactive producer coverage
without disabling useful history; actual inherited alpha-state admission; and
post-submission recovery. No additional engine hook, production code, build,
Wine execution, new capture, install or user-run request was performed for
this study. Root owns the next implementation decision.

## Capture preparation

The existing F8 snapshot now includes `SRCBLENDALPHA`, `DESTBLENDALPHA`,
`BLENDOPALPHA` and `BLENDFACTOR` beside the separate-alpha enable bit. These
four public state queries execute only during requested capture frames, after
`if (!ctx.capture) return`; ordinary rendering gains no queries or logging.
The generic `state id=... value=...` format remains unchanged. This closes the
historical alpha-state provenance gap for the already queued material captures;
it does not establish the values used by the game until those captures arrive.
No additional gameplay run is requested.

## Source and design review verdict

Independent review on 2026-09-13 found no open defect in this study checkpoint.
The ordering conclusions remain bounded to 32 draws in 16 two-draw bursts
across 16 historical frames: every sampled tail contains a nonadditive blend,
while none contains an observed later opaque writer. The proposed bracket now
defines complete full-screen state, finite FP16 boundary handling, alpha
preservation, no-op/repeated-round-trip drift measurement and a direct R32F
single-target mask. Temporal/sentinel integration, live writer completeness,
MRT acceleration and post-submission recovery remain later decisions.

The four added capture render states supply the missing separate-alpha and
blend-factor provenance only during requested F8 snapshots; they add no work to
ordinary draws and do not establish the game's values before a future capture.
This review approves the documents and bounded diagnostic change. It does not
approve a production emission route; native Windows execution, live recovery,
coverage completeness, visual behavior and gameplay cost remain unverified.

## Detached feasibility qualification

Independent source and evidence review on 2026-09-13 found no open defect in
the corrected detached fixture. Its authored SM3 path uses documented D3D9
state blocks and capability checks, needs only one render-target slot, restores
the captured viewport/input/texture/sampler/blend state after each target
switch, and preserves raw sampled alpha while transforming RGB. The accepted
[X3 result](../../verification/results/bottle-X3/linear-emission-gpu.json)
passes 91 cases: 88 retain the analytical RGB oracle, three characterize
repeated transfer drift, and 23,296 values pass in each exact alpha, mask and
depth/stencil probe comparison. The largest analytical RGB
error is 0.620 of tolerance; seven native-gamma or deferred-scene-end controls
differ from the ordered result by 0.0486 to 0.3052 RGB.

Zero-emission round trips changed all 256 sampled pixels. At 1, 16 and 64
brackets, the largest non-cap-domain absolute drift is respectively 0.00390625,
0.0625 and 0.25; the largest residual from the ideal cap-only reference is
0.125, 2 and 8 encoded units. Zero inputs never gained energy, and a verified
negative-zero input produced positive zero after every transfer. These are
descriptive measurements of the complete shader arithmetic and FP16-storage
path, not an isolated rounding-mode diagnosis or an acceptance threshold.

For the historically observed one two-draw burst, 1920x1080 median completion
times were 0.360 ms native, 0.728 ms for one two-source bracket and 1.063 ms for
two single-source brackets; with immediate mask replay they were 0.649, 0.995
and 1.206 ms. The 1280x768 medians were 0.365/0.727/0.946 ms without the mask
and 0.793/1.017/1.214 ms with it. These CrossOver/FEX timings include state
capture/restoration, target/depth queries and event completion; they are
diagnostic and are not game FPS or native-Windows measurements.

The first run usefully exposed a cumulative-drift oracle overreach and point
sampling exactly on an implementation-dependent texel boundary. The reviewed
correction made cumulative drift descriptive, retained finite/cap/alpha/zero
requirements, and moved the sampled witnesses away from texture boundaries;
the failed raw record remains separate from the accepted result. This fixture
qualifies ordered synthetic color, inherited alpha equations, direct R32F mask
production and the documented clean-refusal/incomplete-frame distinction on
the X3 bottle. It does not qualify a production route, a live classifier,
TemporalPass/sentinel integration, post-submission recovery, native-Windows
execution, visual behavior or gameplay cost.


## Orchestrator decision after the experiment

Do not promote the full-scene decode/add/encode prototype unchanged. It proves
ordered additive color and supplies measured cost, but also changes untouched
pixels and has no valid native fallback after accepted source draws. The next
architecture must preserve unchanged encoded pixels, retain a current native
result until publication succeeds, and preserve draw order. Same-draw outputs
and explicit internal target ownership are candidates to investigate before
adding any live hook or route. Complete temporal coverage remains a separate
requirement. The installed renderer and user run queue are unchanged by this
experiment; no new gameplay run is requested.

## Candidate: retain native output and publish an owned target

This is a **detached proof candidate**, not a selected live route. Source review
finds the internal HDR ownership compatible with a same-size target swap; GPU
native-output parity, publication recovery, cost and complete reactive coverage
remain gates. Shader augmentation and its exact source/fade contract require
their own proof.

Keep the current encoded scene **A** immutable during an eligible contiguous
burst. Prepare **B** as an exact encoded copy of A and clear the linear emission
target **E** to zero. Each original geometry submission writes its original
encoded result and alpha into B and a separate linear emission result into E,
using the qualified shared additive blend state. Composite into a fourth
surface **C** from A and E, with native B supplying alpha. Where E is zero,
preserve A's encoded RGB exactly rather than performing a decode/encode round
trip. Selection is per RGB channel: red-only emission must also preserve the
untouched green and blue values, qualified with asymmetric witnesses. Only a completed
C is published; B stays available as the native result until publication
succeeds. Finish the operation before the next original draw or other boundary;
there is no scene-end deferral or retained-geometry replay.

### Ownership and consumer evidence

[HdrPass](../../src/renderer/hdr_pass.h) owns the level-zero `target_` surface.
Resolve, writeback/readback and scene handoff obtain the current target's texture
container when needed. Application `GetRenderTarget(0)` is virtualized to the
logical `hdr_main_`, and application binds of that logical surface map to the
current internal target. The relevant paths are `resolve_hdr`,
`before_set_render_target`, `hdr_logical_render_target`, `hdr_writeback` and
`end_redirect` in [motion_output.cpp](../../src/proxy/motion_output.cpp).
No inspected application-visible alias requires the internal surface identity
to remain fixed.

A narrow same-size adoption operation must coordinate `HdrPass::target_`, the
cached `MotionOutput::hdr_target_` descriptor, physical RT0, `hdr_dirty_ = true`
and clearing borrowed `hdr_resolved_`. Logical main identity, dimensions,
exposure/meter resources and device generation remain valid. The descriptor
currently changes only at latch; a stale descriptor breaks logical state
resynchronization. Resolve additionally checks physical RT0 pointer equality
with `hdr_->target()`. Do not use `release_target()/ensure_target()` to publish:
`release_target()` also destroys the exposure-meter chain.

Publication is restricted to the active scene before TAA resolve and compositor
handoff. `retain_compositor_scene` in [capture.cpp](../../src/proxy/capture.cpp)
deliberately pins the handed-off texture; a pool must not recycle a pinned
surface. Every added resource belongs in device-reference accounting and
Reset/release cleanup, including unbinding it before release. Existing readback,
logical-surface read/write and scene-end paths must observe whichever B or C was
adopted, never stale A.

### State and failure contract

Before source submission, positively flush/unbind injected lazy motion RT1/RT2
and restore their application write masks, then verify the saved application
attachments and state before borrowing RT1 for E. Calling `restore_bindings()`
alone is not a success check: it returns void and clears lazy flags even if an
unbind or write-mask restore fails. The HDR save/restore helper
currently saves texture/sampler stage zero only; an A/E/B composite must cover
every additional stage it touches and unbind all source textures before
rebinding a surface that aliases one. Transfer passes retain the explicit
full-screen state contract above. Restore the exact original source state for
the MRT draw and the application state after publication, including viewport,
scissor, alpha, depth/stencil and all changed bindings. Keep injected operations
inside the guarded original draw scope and outside application draw accounting.
Queries, state-block recording and unresolved bindings remain refusal cases.

- **Before the source draw:** allocation, copy, clear or preparation failure can
  refuse the feature and execute the original draw against A, provided original
  state is successfully restored. A failed restoration is not clean refusal.
- **After successful source submission:** B contains the accepted native result
  only once exact-copy and augmented-oC0 parity are qualified. A composition
  failure can discard C and adopt B without replaying geometry. Existing HDR
  fallback then reads the accepted image through the current owner.
- **Publication or restoration failure:** retaining B is content preservation,
  not proof that the device is using it correctly. A successful B bind and full
  application-state restoration are required to report native recovery.
  `HdrPass::bind()` can change RT0 and then fail viewport/scissor restoration;
  a failed HRESULT does not imply unchanged binding. Retain B and invalidate
  renderer state/history on unsuccessful recovery; report an incomplete frame.
- **Failed source MRT draw:** no transaction has been established that leaves
  both B and E unchanged. In a burst, a failing later draw can also invalidate
  the claim that B is an exact successful-prefix image. Do not silently restore
  A after accepted emissions or replay stale geometry. This remains an explicit
  failure-policy gate distinct from post-draw composition failure.

### Detached proof and cost gate

Qualify exact A-to-B initialization; original versus augmented native B RGB and
alpha; zero-E preservation across repeated operations; asymmetric emission and
order witnesses; C adoption; B adoption after composition failure; failed
binding/restoration; and repeated pool rotation and resource retirement. A
successful source draw followed by failed composition must retain the accepted
native content. Existing temporal-consumer evidence is reused; this candidate
does not resolve live reactive coverage or change sentinel policy.

B/E/C require three additional full-size FP16 surfaces beyond A: **24 bytes per
pixel**, approximately **22.5 MiB at 1280x768** or **47.5 MiB at 1920x1080**, before
mask and other resources. Pool rotation can reuse allocations but does not
remove copy, clear or composite bandwidth. Measure the new operation against
native draws and the existing prototype, including state handling and GPU
completion. One two-draw burst is the observed historical per-frame case;
16 bursts are stress-only. No geometry replay is needed for these two color
outputs, but any separate reactive producer has its own cost and completeness
gate. Native Windows execution and live behavior remain unverified.

### Original SM2 shader headroom and parity limit

Targeted inspection of all five local original PS disassemblies confirms the
small executable bodies below. Raw programs and disassembly remain untracked
under `/tmp/x3-shader-sweep/`; the table contains derived facts only.

| Original PS | GPU arithmetic / texture slots | Source path |
| --- | --- | --- |
| `8360f422de08b5bd`, `9975b706e5a1c999` | 7 / 1 | Affine RGB, then interpolated fade |
| `ff2473e73a6bdfa1` | 2 / 1 | Sampled RGB, then interpolated fade |
| `8559522220507d5e` | 6 / 1 | Affine RGB, no fade |
| `875e780adb131b16` | 1 / 1 | Sampled RGB, no fade |

The affine variants use r0/r1 and c0–c3; all use s0 and t0, and faded variants
read v0.x. Their 63-instruction CPU preshader computes affine coefficients in
metadata; it is not part of the GPU instruction budget. No source has flow
control, depth output or another color output. There is ample apparent space
for bounded decode/gain arithmetic, but a final transformed program still needs
its weighted instruction and register limits checked.

Keep the original VS2 and its COLOR0 interpolation. In the PS, preserve native
sampling, affine arithmetic, RGB fade and raw sampled alpha for oC0. Copy RGB
**before** its native fade into a spare register for linear conversion, then
apply the original fade and new gain to that linear result. Decoding the already
faded oC0 value would change the fade curve; oC0 is also write-only.

SM2 supports additional color outputs with a single full `MOV` per output,
without source modifiers/swizzles or partial output masks. The existing programs
use partial-precision operations, including their final `mov_pp oC0`; preserve
those original tokens for the prototype, and use a plain full `MOV` for new oC1,
with a deterministic +0 alpha lane. This existing `_pp` output conflicts with the
literal linked SM2 output rule: accepting that original output plus new oC1 must
be an explicit shader-creation/runtime gate, not a claimed portable guarantee.
Actual augmentation remains a native-Windows qualification gap. Extra uses can
change driver compilation, so unchanged original instructions alone do not
prove bit-exact native output. The detached authored PS2 experiment must compare
native RT0 with and without the second output across its tested source/state
domain. [Output-register rules](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx9-graphics-reference-asm-ps-registers-output-color)
and [weighted PS2 instructions](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx9-graphics-reference-asm-ps-instructions-ps-2-0)
supply the documented constraints; actual original-program augmentation remains
unqualified by an authored fixture, even if authored partial-precision variants
are accepted on X3.

B and E use the same FP16 format and dimensions, shared ADD/ONE/ONE RGB blend,
and no MSAA. Require MRT count, post-pixel blending/format and needed write-mask
capabilities. Hardware fog and dithering must be off because the extra MRT
results are not defined generally; alpha testing, when tested in the fixture,
uses oC0 to reject all outputs. First live admission remains alpha-test-off and
full RGBA writes. These are documented [D3D9 MRT restrictions](https://learn.microsoft.com/en-us/windows/win32/direct3d9/multiple-render-targets),
not backend-specific assumptions. No production shader or route is added here.

The candidate's exact RGB formula uses the ordered sanitizer S and safe decode /
encode already specified above. Each full-precision source output is
`E_source = S(decode(affine_RGB) * preserved_fade * gain)` before FP16 storage
(where `affine_RGB` means sampled RGB after the optional affine step);
new oC1 arithmetic is full precision, while original native operations retain
their modifiers. Shared additive blending can overflow accumulated E, so form
`e = S(E_sample)` again during composition. For each RGB channel independently:

```
C[i] = (e[i] == +0) ? A[i] : encode(S(decode(A[i]) + e[i]))
C.a  = B.a
```

S maps negative values, NaN and negative infinity to +0, and positive infinity
to 65504 with the specified operand order. S alone can retain negative zero;
the equality branch accepts either signed zero. Any additional zero-sign
canonicalization is separately qualified, not implied by MIN/MAX. Source and
accumulation sanitation need explicit witnesses; a finite-only experiment must
say so if it does not cover them. Exact preservation refers to the qualified
finite encoded A domain (including signed-zero and asymmetric channels when
actually tested), not arbitrary NaN payload transport. A's untouched channels
are selected directly, even outside the decode cap; E alpha is never used for
composition and starts at zero with every new source writing +0 before the
shared alpha equation. Native B alpha remains governed by the original shader
and inherited alpha state. Neither alpha lane is gamma-converted.

Candidate architecture review: Sol/high approved with no open findings after
clarifying original `_pp` output portability, full oC1 alpha, source/accumulation
sanitation (including signed-zero limits), and checked lazy-attachment flushing.
The corrected detached implementation removes an unnecessary independent-write-
mask requirement, uses FP16 rather than an unrelated R32F capability for its
depth witness, and performs one A-to-B target bind per measured copy.

### Detached MRT candidate qualification

Independent source and evidence review on 2026-09-13 found no open defect in the
bounded authored experiment. The accepted
[X3 result](../../verification/results/bottle-X3/linear-emission-mrt-gpu.json)
passes 38 cases over eight VS2/PS2 source variants and 78 shader creations. It
compares 118,784 A-to-B channels and 118,784 original-versus-augmented native
channels exactly,
preserves 74,577 zero-E channels bit-for-bit (including 86 negative-zero and 172
above-decode-cap witnesses), copies B alpha exactly, and passes 9,728 final alpha
and D24S8 probe pixels. All 256 explicitly seeded positive-infinity E channels
produce finite capped C, and the largest analytical RGB error is 0.5634 of the
specified tolerance. Three post-source composition refusals adopt the current
native B; two pre-source cases decline MRT and execute the native path.

At 1280x768, median completion was 0.541 ms native, 1.013 ms for the best-case
two-source MRT burst and 1.471 ms for two single-source brackets; copy, clear and
composite medians were 0.424/0.340/0.495 ms. At 1920x1080 the corresponding
medians were 0.605/1.524/2.401 ms and 0.484/0.431/0.717 ms. Component timings are
separate completion measurements, not an additive decomposition. These are
CrossOver/FEX diagnostics and do not measure game FPS or native Windows.

This qualifies authored same-draw native B/linear E behavior, per-channel C
selection and native-B content fallback on the tested X3 backend. It does not
qualify augmentation of the five original shaders, live HdrPass ownership or
TemporalPass coverage, partial/failed MRT submissions, failed bind/restoration,
negative or NaN E, nonfinite A/source input, native-Windows execution, visual
behavior or gameplay cost. No production route is approved by this result.

Orchestrator decision: retain this as the viable color/recovery prototype, not a
live route. Untouched-channel preservation fixes the first experiment's measured
no-op drift, and native B supplies a bounded recovery image after successful
source draws. Cost remains material: the two single-source brackets add about
1.80 ms over the native diagnostic at 1080p. Investigate the compositor's
full-screen gamma arithmetic for zero-emission pixels before adding live
ownership or original-shader augmentation; any optimization must retain the
same image, alpha, zero-channel and recovery checks. Complete reactive coverage
and actual-original/native-Windows qualification remain independent gates.

### Detached zero-emission branch experiment

Independent source and evidence review found no open defect in the bounded
branch experiment. The shader samples A, E and B before control flow, sanitizes
E, and skips only the decode/add/encode work when all three E channels compare
equal to zero; the branch contains no texture or derivative-dependent
instruction. The comparison renders the baseline and branch compositors from
the same unchanged A/E/B inputs into distinct targets, after native parity has
already been read back. Timing reinitializes and fences matching inputs for each
alternating pair and excludes comparison/readback work.

The accepted
[X3 branch result](../../verification/results/bottle-X3/linear-emission-mrt-branch-gpu.json)
passes the same 38 cases with 81 shader creations. All 115,712 successful
baseline-versus-branch RGBA channels compare bit-for-bit; the existing 118,784
copy and 118,784 native-parity channels, 9,728 alpha and depth pixels, 74,577
zero-E lanes (including 86 negative-zero and 172 high-code witnesses), 256
positive-infinity sanitation lanes, three fallbacks and two refusals also pass.
The largest analytical RGB error remains 0.5634 of tolerance.

Across eight pairs per workload, median paired baseline-minus-branch deltas in
milliseconds (positive favors the branch) and branch wins were:

| Size | Coverage | Composite | Two-source burst | Two brackets |
| --- | --- | ---: | ---: | ---: |
| 1280x768 | dense | +0.00185, 5/8 | -0.00160, 4/8 | +0.09950, 5/8 |
| 1280x768 | sparse | -0.00225, 2/8 | -0.00100, 4/8 | +0.00560, 4/8 |
| 1920x1080 | dense | -0.00205, 4/8 | -0.00635, 3/8 | -0.00415, 2/8 |
| 1920x1080 | sparse | +0.00535, 5/8 | -0.00265, 4/8 | -0.00160, 3/8 |

The branch won 45 of 96 pairs with no ties, while individual paired deltas
included sizeable outliers in both directions. This run establishes image
parity for the authored domain but does not show a consistent performance
benefit. It has the same scope limits as the detached MRT candidate: no original
shader augmentation, live route, native-Windows execution, gameplay cost or
additional nonfinite-input qualification.

### Actual-original transformer source review

Independent review of the pure transformer found no open source defect. Direct
inspection of the five local originals confirms the exact hashes and copy
boundaries, that the complete executable bodies use only r0/r1 and c0-c3, and
that r2/r3, c30/c31 and oC1 are free. The transformer preserves every original
word, comment, `_pp` instruction and raw-alpha path; its sole inserted pre-fade
copy is RGB-only. Its full-precision tail implements ordered source sanitation,
safe scalar POW decode, the decoded-result cap before preserved fade and gain,
the final cap, and a full unmodified oC1 MOV with +0 alpha. The largest result is
29 weighted arithmetic slots plus one texture slot, within PS2 limits.

The focused host module passes seven tests over five exact VS/PS pairs and 25
gain variants, including byte reconstruction, aliasing and failure rollback,
register/output rules, weighted budgets and analytical finite/nonfinite
witnesses. This is creation-time CPU work with bounded linear scans and one
temporary result allocation; it adds no per-draw work. The host arithmetic
model does not qualify GPU handling. Actual shader creation, native-oC0 parity,
MRT output and native-Windows execution remain the next gates; no runtime route
or feature support is added at this checkpoint.

Orchestrator decision: keep the baseline compositor. The branch remains a
separate reproducible experiment, not a selected optimization; do not repeat
this benchmark merely to seek a favorable result. Proceed with bounded
actual-original shader qualification and examine whether the recorded consecutive
draws provide a defensible batching opportunity. The fixture cost remains a
live-integration concern, not proof of game FPS or a GPU-only bottleneck.
