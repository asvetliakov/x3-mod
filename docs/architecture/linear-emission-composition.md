# Ordered linear emission composition

Decision brief, 2026-09-13, after installed BUMPMAP source `df4dc09` /
checkpoint `974f2be`. **Recommendation: build a detached ordered-composition
and reactive-producer feasibility fixture next. Do not integrate a live route
until coverage, failure recovery and cost are justified.** This advances actual
linear additive composition rather than more opaque family aliases.

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
blends. Blindly replaying every unrouted background writer can mark nearly the
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
