# Ordered linear emission composition

Updated 2026-09-13. The initial ordered-bracket experiment is qualified but will
not be promoted unchanged: it alters untouched pixels and lacks post-draw native
fallback. The [revised same-draw candidate](#candidate-retain-native-output-and-publish-an-owned-target)
has passed detached component and focused live-integration qualification. A
default-off live route is now selected for the five exact source pairs; gameplay
and native-Windows behavior remain unverified. The earlier design and its results
below explain the bounded route and its recovery policy.

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
distinguished from clean pre-mutation refusal. This was unresolved before the
live implementation and is addressed by the bounded policy qualified below.

## Reactive coverage: selected supplemental scope

Root selects **source-set-complete supplemental coverage** for enhanced emission,
with a same-draw three-FP16-MRT producer. This is not complete scene reactivity;
the later live qualification below applies it only to successfully enhanced
draws. A classifier or replay producer for every unrelated native writer is not
a prerequisite for this slice.
The installed renderer's existing native background/stardust limitations remain;
see the [background coverage study](../reverse-engineering/background-temporal-coverage.md).
Do not turn that bounded decision into a static shader-wide exemption or mask
nearly the entire background merely to call coverage complete.

### Consumer contract

The existing [resolve shader](../../src/temporal/resolve.hlsl) has independent
mask and depth-sentinel controls: current coverage rejects history at the current
pixel, and any nonzero-weight reactive previous-color tap rejects that history
lookup. Keep opaque motion and the actual far-plane camera transform. Existing
`RequiredMask` retains its complete conservative visible-RGB coverage contract;
existing `DerivedFromDepthSentinel` remains unchanged.

A new explicit `SupplementalMaskWithDepthSentinel` policy requires a
frame-complete **FP16 raw coverage texture for the enhanced source set**. Zero
means no recorded source coverage; every other value, including nonfinite, is
conservatively reactive. The consumer:

1. Validates that raw input, ownership, dimensions and frame regime.
2. Before resolve, canonicalizes and dilates its coverage by one pixel over a 3x3
   neighborhood into the existing next-frame R32F mask surface. Extend the
   existing snapshot shader mode only for this dilation. This moves the existing
   mask snapshot pass before resolve; it does not add another full-screen pass.
3. Resolves using this canonical current mask and the previously owned canonical
   mask, with sentinel/camera controls also enabled. Do not dilate previous
   coverage again. Retain existing rejection of all nonzero-weight Catmull-Rom
   history taps, including disappearing sources.
4. Publishes color, depth and the mask together only after successful processing
   and state restoration, under the existing history ownership/generation rules.

The one-pixel expansion covers enhanced RGB's participation in current 3x3
neighborhood-clipping statistics, not just direct raster coverage. It does not
promise universal image equivalence outside a source or solve unrelated native
history artifacts. Current alpha remains the application's alpha. Coverage is
not radiance: no gamma decode, gain, luminance weighting or exposure applies to
it. Existing `RequiredMask` snapshot behavior is unchanged by this new mode.

Coverage includes every enhanced submission whose result can reach the published
scene, conservatively including native-B fallback submissions. It is independent
of source RGB, fade, alpha and gain. In particular, gain zero can publish A where
native B contains the original emission; zero E is not proof of zero feature
change. A complete frame with no enhanced draws supplies a valid zero mask,
while previous coverage still rejects disappearing effects. Feature/policy and
lifetime transitions invalidate history using the existing mechanisms.

If required supplemental coverage is incomplete, use existing `Unavailable`
with a null mask: output current-only and never complete usable history from
that frame. `history_allowed = false` alone is insufficient because a successful
run can subsequently complete the newly written, inadequately masked history.
A missing producer, snapshot failure or unsuccessful restoration cannot silently
become an empty mask. Color/native-image recovery and usable-history publication
remain separate success decisions.

### Supplemental consumer qualification

The consumer is implemented and independently reviewed, without live wiring.
The focused [X3 result](../../verification/results/bottle-X3/temporal-supplemental-summary.json)
passes 357 checks, 73 numerical samples and 89 state restorations, including
the legacy reactive cases and one actual Reset across two generations. It
qualifies combined motion/sentinel/far-plane handling, current and previous
coverage, exact one-pixel expansion, disappearing effects, nonfinite mask
canonicalization, incomplete-frame history suppression, transitions and failed
operation/restoration recovery. No correctness finding remains in this delta.

Two failed fixture attempts are retained in that result: an advertised-cap
assertion stopped before execution, and an accumulation oracle incorrectly
ignored the existing neighborhood clip. The final run changes those fixture
checks only; reviewed consumer behavior was unchanged. It exited zero in
33.207 seconds. This is verification duration, not frame time.

Relative to the existing RequiredMask path, expansion adds eight mask fetches
to its snapshot draw, without a further pass or steady-state allocation. Relative
to the installed sentinel-only route, enabling supplemental coverage will add
the mask snapshot draw and two R32F history surfaces, plus the producer's raw
mask. End-to-end feature cost still needs measurement.

The same compiler reports 1,179 slots / 4,487 DWORDs for the previously embedded
resolve and reported 1,261 / 4,768 for the initially unrolled supplemental
source. X3 executed both despite advertising a 512-slot limit; that historical
behavior is not cap-compliance evidence. The reviewed source now keeps all ten
bounded neighborhood axes as real loops, preserving their iteration and
arithmetic order, and compiles to 507 slots / 1,891 DWORDs with five slots of
headroom against the documented ps_3_0 minimum.

The focused [loop qualification](../../verification/results/bottle-X3/temporal-loop-qualification.json)
passes the existing two-generation temporal suite and both actual Reset paths
(584 numerical checks and 396 state restorations). Thirty paired frames across
finite, nonfinite, HDR, motion and supplemental-mask sequences match the retained
1,261-slot source exactly in RGB, alpha, depth, mask and history use. Forty-eight
alternating QPC-through-EVENT pairs show positive mean loop costs from 0.008 to
0.222 ms; stationary means are +0.129 ms at 1280x768 and +0.222 ms at 1920x1080,
with noisy per-pair ranges in the other workloads. These are warmed full-pass
completion-wall measurements, not GPU-busy time or game FPS, and establish no
performance improvement. The bounded cost is accepted to meet the documented
shader limit. The embedded program has now been regenerated with the same native
compiler in X3 and matches the qualified 507-slot bytecode exactly; its existing
[provenance record](../../verification/results/temporal-resolve-program.json)
binds source and generated bytes. The reviewed production candidate is built
and installed; its explicit-DLL load check, unchanged import inventory and x87
audit pass. Existing user run 2 covers gameplay acceptance. Native Windows
execution and live producer/consumer integration remain unverified.

### Spatial consumers and source ownership

For the established HDR compositor boundary, `MotionOutput::scene_end_hook`
resolves TAA before ending scene redirection; `capture.cpp` calls it before the
original compositor and replacement bloom preparation. Consequently that bloom's
spatial spread is after history, not an extra pre-TAA mask footprint. See the
[verified ordering](hdr-bloom-boundary.md#decision-and-ordering). Sharpen likewise
acts on the display image, not the stored history.

This ordering does not certify arbitrary earlier scene-color readers, copies or
blur passes. An unqualified reader that can spread enhanced RGB before TAA makes
supplemental completeness invalid unless its affected footprint is explicitly
propagated. Color-order barriers alone do not establish mask completeness.
Current source coverage can conservatively survive later opaque occlusion;
never erase it merely from an unsupported inference about effect motion.

### Selected minimal reader and failure policy

The source audit narrows reader observation to the logical application scene
resource. Under the existing serialized scene-draw/state-restoration contract,
application GetRenderTarget returns `hdr_main_`, GetBackBuffer returns the
application surface, and GetContainer does not reveal private A/B/C. Cache the
logical main texture identity if it has one, observe its sampler bindings even
when mip bias is off, and refresh prebound/state-block bindings at the existing
resynchronization points. A new global texture-descendant graph is not required.
Existing read/copy/writeback/RT-switch/EndScene barriers must distinguish the
qualified post-TAA compositor handoff from an unexpected earlier color export.

An unexpected export after enhancement may persist in an application texture or
CPU buffer and return later. Frame-clear alone cannot restore supplemental
completeness. The initial exceptional policy is to stop enhancement and keep
history unavailable until process restart, rather than silently reset this
uncertainty next frame; a narrower recovery boundary needs evidence that exported
data was discarded. This is an unsupported-path recovery policy, not normal
per-frame classification. No observed engine export is newly declared safe by
this source audit.

A failed original source draw returns its exact HRESULT and is never replayed.
B/E/coverage may be partial: do not compose C or call B a certified native
fallback. Attempt B ownership/binding and application-state restoration only as
best-effort continuation of possible original RT0 side effects, invalidate
history and stop enhancement for that frame. Failed recovery blocks further
renderer work until recovery/Reset. Successful source plus failed composition
is different: B is then a completed native result, provided publication and
restoration succeed.

The existing capture guard serializes its hooked draw/setter paths, but ordinary
application admission alone does not serialize all independent roots. Arbitrary
concurrent/reentrant unguarded GetTexture calls could already observe temporary
HDR/TAA inputs. There is no evidence X3 uses this path; this slice retains the
existing serialized scene-thread contract and does not introduce a global lock
framework. Such concurrent observation remains outside qualification and would
require a bounded guarded-getter policy if supported.

### Third same-draw output: detached qualification

The detached fixture now qualifies an additional constant positive RGB output
at oC2 in the augmented PS2 program. It binds a separate frame-persistent FP16
coverage target beside native B and linear E, clears it to zero once per authored
frame, and uses the original geometry submission. Shared
RGB ADD/ONE/ONE accumulates a conservative nonzero union; it is not independent
MAX blending. Alpha is irrelevant to that RGB union. Original VS, source oC0,
alpha and depth/stencil behavior must remain qualified and unchanged; the initial
source contract has alpha test off and full RGBA writes on all three targets.
No geometry replay is required by this candidate.

Require `NumSimultaneousRTs >= 3`, equal dimensions, no MSAA,
`MRTPOSTPIXELSHADERBLENDING` and FP16 `QUERY_POSTPIXELSHADER_BLENDING` support;
fixed-function fog and dithering are off. All three targets share FP16 bit depth,
so this combination does not add a mixed-bit-depth requirement. Full RGBA writes
on all avoid an independent-write-mask prerequisite. An R32F MRT alternative
would additionally need mixed-bit-depth capability and R32F blending support.
These are [documented MRT contracts](https://learn.microsoft.com/en-us/windows/win32/direct3d9/multiple-render-targets)
and [format queries](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dusage-query),
which the detached X3 result below checks directly. Existing three-target TAA
alone does not establish this producer's blending or original-oC0 parity.

The producer must preserve its mask across B/C publication choices, avoid
aliasing motion/depth targets, prevent stale-frame reuse, and retire references
correctly at Reset/shutdown while reusing allocations during steady rendering.
Canonicalization uses shader sampling into R32F; no cross-format copy equivalence
is assumed. Active queries and state-block recording remain refusal cases.
Measure the added mask writes, snapshot/dilation and lifetime work. The pure
consumer and three-output producer are separately qualified below; their
integration remains a separate gate and neither authorizes live routing.

The earlier detached experiment used immediate single-RT R32F coverage replay
in a synthetic closed world. Its historical results and replay-cost estimates
below remain evidence for that experiment. General native-writer replay and a
complete scene classifier are no longer prerequisites for this supplemental slice.

The pure transformer and detached GPU producer are now qualified for this
bounded source set, without live routing or consumer integration. Independent
source review found no correctness defect. With coverage disabled, all 25
accepted two-output variants remain byte-identical to the prior accepted
transformer outputs. Enabling coverage changes only the previously unused
`c31.y` literal to one and appends two full, unmodified moves after the unchanged
`oC1`: `c31.y` to dead temporary `r3`, then `r3` to `oC2`. The largest result is
31 arithmetic slots plus one texture slot. Ten host tests cover 50 variants and
548 structural, resource, alias and rollback checks. Variant creation remains
bounded CPU work at shader creation; the option adds no per-draw CPU path.

The first and only focused [X3 coverage result](../../verification/results/bottle-X3/linear-emission-mrt-coverage-gpu.json)
passes 81 cases, 201 shader creations, 132 source draws and 97 brackets. Across
the actual five PS2 and three VS2 originals, all 198,656 B/E channels match the
retained two-output path bit-for-bit. All 99,328 native/reference channels and
all 99,328 copy channels match. The mask oracle passes all 20,736 pixels: 15,536
contain positive coverage and 5,200 remain exact zero. It covers source RGB,
fade, gain and alpha at zero, original alpha-test/depth/viewport/scissor
rejection, overlapping sources,
two and sixteen separately published brackets, persistence across E clears and
C rotation, and a rejected next-frame draw after the frame clear. Final alpha
and depth/stencil also pass for all 20,736 pixels. Original and two-output hashes
match the prior accepted record; the result pins all 25 three-output variants,
the fixture executable and current source inputs.

Paired EVENT-completion timings isolate two successful source draws with and
without RT2, followed separately by a populated-mask clear. At 1920x1080 the
median added source-write time is 0.0140 ms (all eight pairs positive, range
0.0032--0.1523 ms), and the clear median is 0.3323 ms. At 1280x768 the write
delta is noisy (median 0.0045 ms, range -0.1453--0.1166 ms; five of eight
positive) and the clear median is 0.3253 ms. These are fixture completion times,
not a GPU-only decomposition or game-FPS evidence; the accepted compositor
benchmark was not repeated.

This qualification covers successful detached same-draw submissions only. It
does not provide the live owner, Reset/shutdown retirement, incomplete or
partially failed MRT recovery, post-source fallback publication, TemporalPass
handoff, gameplay cost or native-Windows execution. Ownership and integration
need separate qualification; gameplay and native Windows remain unverified.
The tested X3 device reports four MRTs,
post-pixel-shader blending and independent write masks; the candidate itself
uses three equal-format FP16 targets with full RGBA masks and therefore does not
make independent write-mask support a requirement.

## Detached fixture and decision gate

This section records the initial experiment, before the supplemental-scope
decision above; its general replay study is not the selected production prerequisite.

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

The unused `HdrPass::exchange_target` component now implements only this narrow
surface-reference transfer. It validates a distinct, same-size, single-level
DEFAULT FP16 render-target texture on the attached device through public D3D9
descriptors, container and level queries, then swaps the caller's owned surface
reference with `target_`. It does not bind a target, validate image content,
reset exposure or meter state, or release either transferred owner reference.
An independent review found that the first version compared raw interface
pointers; the corrected implementation uses the documented
[canonical `IUnknown` identity](https://learn.microsoft.com/en-us/windows/win32/com/rules-for-implementing-queryinterface)
for target distinctness, both device checks and the returned level-zero surface.
Temporary query references, including hostile non-null outputs returned with a
failed HRESULT, are balanced before every refusal.

The extracted production source passes 52 existing display-snapshot scenarios
and 63 exchange scenarios with 1,343 total host checks. These cover descriptor,
container, device and level failures, canonical interface aliases, repeated
ownership rotation, exact non-target HDR/meter/exposure preservation, and the
existing Reset/shutdown cleanup policy. The complete HdrPass translation unit
also cross-compiles for x86. The method performs a bounded sequence of COM
queries per exchange and has zero runtime cost while unused. A future live route
may exchange after each enhanced draw, so its publication cost still needs the
integrated measurement. This host-only checkpoint does not qualify real D3D
surface adoption, physical RT0 and MotionOutput descriptor coordination,
fallback publication, native Windows or live lifetime; the combined ownership
fixture remains the next gate.

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

### LinearEmissionPass component qualification

The unused [LinearEmissionPass](../../src/renderer/linear_emission_pass.h)
implements the candidate as a serialized single-draw transaction. `attach`
borrows the documented device interface and native-call table, while
`ensure_targets` creates the persistent B/E/C/M pool outside a draw bracket.
`begin_frame` clears M once for a new frame. After a successful `prepare`, the
caller issues the original indexed draw exactly once and passes its HRESULT to
`finish`. The selected, bound C or B is then exposed through the exact owning
slot returned by `owning_candidate`; the caller may pass only that slot to
`HdrPass::exchange_target` and must complete the transaction with
`acknowledge_exchange`. A failed source remains `Incomplete` and keeps coverage
unavailable even when best-effort B ownership recovery succeeds. A failed
exchange permits one `recover_native` attempt. Coverage is visible only while
the pass is idle, and `reference_accounting_busy` prevents reference probes
while saved getter references are held. `before_reset` unbinds owned MRT and
depth state before releasing targets; `detach` also releases the retained
programs and declaration. The borrowed device must outlive the pass. The caller remains responsible for owner/thread,
reentrancy, Reset and external handoff-pin serialization stated by
`LinearEmissionBoundary`.

Independent source review closed the initial capability, canonical COM identity,
hostile output-reference, atomic pool-commit, coverage-phase and Reset-order
findings. The production component then passed 63 host scenarios with 1,458
checks; these exercise reference balance and aliases, conditional state caps,
clean refusal, incomplete B exchange, recovery, Prepared/Pending Reset cleanup,
failed native Reset and generation recreation. An earlier sanitizer run was
clean. The focused X3 GPU result
[qualifies the detached component](../../verification/results/bottle-X3/linear-emission-mrt-pass-gpu.json)
over 60 cases and 85 original indexed draws: 245,760 C/B/E/M comparison
channels match the independent qualified path exactly, with 16 fault controls,
two capability twins and a same-instance actual Reset followed by a complete
generation-two transaction.

The first parser pass rejected one descriptive `capzero` count: the CPU ideal
stored 154.625 while actual GPU feedback stored 154.5, on opposite sides of a
154.6012 diagnostic threshold. No GPU work was repeated. The accepted record
preserves that failure and the original raw hashes, then revalidates the same
readback with a reviewed rule: GPU-composed feedback counts are descriptive and
bounded by exact zero-E lanes, while static, single-step and zero-energy seed
counts remain exact and positive high-code witnesses remain mandatory. All
bitwise image, numerical, state, draw, ownership and Reset gates are unchanged.

Paired EVENT-completed timings measured a component cost over the native draw of
0.49065 ms median at 1280x768 and 0.76490 ms at 1920x1080. They include the
component's local owning-slot exchange/acknowledgement model, but exclude the
per-frame M clear, a real `HdrPass::exchange_target`, live hook/classifier work,
allocations and readback. These are focused diagnostic timings, not gameplay
FPS. The component remains detached: it does not qualify a live HdrPass or
TemporalPass route, live material/global constant ownership, real failed-draw
atomicity or device-loss recovery. The fixture covers a finite synthetic shader
and state domain, and native-Windows execution remains unverified.

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
model does not qualify GPU handling. The following detached run qualifies actual
shader creation, native-oC0 parity and MRT output on X3. Native Windows remains
untested; no runtime route or feature support is added at this checkpoint.

### Runtime shader-cache preparation

`MotionOutput` now has a reviewed, default-off, pre-attach emission configuration
and owns cached three-output variants independently of motion-row support.
Original PS2 emission shaders therefore do not disappear at the old no-motion-row
return. Exact VS/PS pair eligibility is refreshed at registration, shader setters
and state resynchronization; no transformation, hashing or pair search is added
to draws. Gain is immutable and finite in [0,16], and coverage is always enabled
for these cached variants. At this cache-only checkpoint no CLI, pass invocation
or live enhancement was wired; the later integration below consumes the cache.

The existing extracted-production-method host fixture passes 3,540 assertions,
including 1,191 emission-cache assertions for default-off behavior, invalid input,
creation/partial-output failures, re-registration, pair aliases, state blocks,
Reset and reference retirement. Strict x86 compilation and a relocatable link
against the unchanged transformer pass. Independent review has no open finding.
This qualifies cache control and lifetime with scripted COM calls; it is not a
new GPU/runtime result. Actual shader bytes retain the original GPU evidence
below. CMake and the existing motion fixture seam link the core as required.

### Actual-original GPU qualification

The first and only focused X3 run of the reviewed transformer passed and exited
cleanly. The accepted
[actual-original result](../../verification/results/bottle-X3/linear-emission-mrt-original-gpu.json)
covers 70 cases, three unchanged original VS2 programs, five untouched original
PS2 programs, all 25 gain variants and 42 shader creations. It executes 105
source draws and accepts the original `_pp` oC0 together with the new full oC1
on this backend. All 71,680 A-to-B copy channels and 71,680
original-versus-augmented native RT0 channels compare exactly.

The independent finite-source oracle passes for native B, linear E and composed
C with maximum tolerance fractions 0.3252, 0.3127 and 0.4544 respectively. The
run preserves 25,168 zero-E lanes, including 33 signed-zero and 70 high-code
witnesses; 17,920 composed alpha and depth pixels and 35,840 direct B/E alpha
values also pass exactly. The result pins the eight original files, 25 generated
variants, fixture/core inputs and executable; independent review regenerated
the same source, burst and invariant totals from the 70 cases.

This qualifies actual-original shader creation, native-output parity and the
finite sampled/affine/fade/gain composition domain on the tested X3
CrossOver/FEX backend. It does not qualify native Windows, a live HdrPass route,
game-owned constants or preshader execution, source NaN/infinity and adverse
nonfinite intermediates, source-submission recovery, device loss or gameplay
behavior. No new benchmark was run, and no runtime feature is added by this
fixture result.

Orchestrator decision: keep the baseline compositor. The branch remains a
separate reproducible experiment, not a selected optimization; do not repeat
this benchmark merely to seek a favorable result. Actual-original qualification
has passed. The supplemental temporal consumer, third same-draw coverage output
and single-draw live ownership integration are now qualified within the limits
below. Historical draw adjacency does not yet justify batching. The fixture cost
is not proof of game FPS or a GPU-only bottleneck.

### Default-off live route and focused X3 qualification

The selected live route is opt-in through `--linear-emissions`; the immutable
gain remains finite in [0,16], and admission additionally requires active HDR,
AgX with gamma-2.2 decode, TAA, the qualified scene owner and thread, and one of
the five exact VS/PS pairs. `Capture` grants a draw-local permission to
`MotionOutput`, which prepares `LinearEmissionPass`, calls the application's
indexed draw exactly once, and finishes the transaction with the original
HRESULT. A successful C or native-recovery B is transferred through the real
`HdrPass::exchange_target` owning slot and acknowledged before its physical
descriptor is published. Logical application RT identity stays fixed. The
pass's M target becomes the supplemental `TemporalPass` input only after a
complete idle transaction.

The route restores lazy MRT and mip state before borrowing source state. Clean
preparation refusal leaves the native draw on A and retains valid frame M. A
failed source returns its exact HRESULT, publishes no C, marks the result
Incomplete and may retain best-effort B ownership without claiming usable
history. Failed restoration or an unknown descriptor stops the frame and drops
TAA. Reset releases the emission pool and cached main-container identity in the
established order; COM reference accounting remains busy across every transient
ownership span. Frame M is cleared once even when emission disappears. Export
of a successfully enhanced image through an unqualified reader quarantines the
feature until process restart; the qualified Hook, BloomCopy and Present
terminal paths and bounded diagnostic flush are exempt. Present remains a
writeback terminal and does not itself initiate TAA.

Independent source review closed with no open finding after the restoration,
descriptor, texture-identity, reference-span and terminal-export corrections.
Five focused extracted-production tests pass 3,872 assertions, including 308
route and 1,191 cache assertions. Strict x86 production and fixture translation
units compile with the required SSE2, incoming-stack and warning flags;
production preprocessing contains none of the fixture-only forced TAA-readback
seam.

The accepted [focused X3 result](../../verification/results/bottle-X3/linear-emission-live-gpu.json)
passes all six configurations: feature off/on in per-draw and lazy RT modes,
plus two 1080p timing processes. Each functional configuration executes 12
frames and ten original emission draws, with exactly one Reset and final device
release. Feature-off cases pass 74,917 checks each; enabled cases pass 197,807
checks each; all four functional cases report 34 state restorations. The two
enabled modes produce the same per-frame mask coverage and the same
resolved-output hashes as each other.
Eleven frames, indices 0-7 and 9-11, match the independent supplemental temporal
reference byte for byte. Frame 8 deliberately submits an invalid-index-buffer
draw, preserves `0x8876086c`, reports Incomplete, routes only the earlier ordinary
draw, resolves no TAA and presents the unresolved HDR image through one clean AgX
writeback. Its independently computed display differs by at most 0.485 of one
8-bit code. Frame 9 observes the retained A-only motion history, reports one of
two keyed draws missing and a 0.5 missing fraction, takes a cut, and runs
current-only; Reset and history resumption are then exercised on frames 10-11.
No functional frame reports emission suppression, export quarantine, lost state,
HDR fallback or leaked mip bias.

The diagnostic timing uses separate, unpaired processes. For one source covering
25% of a 1920x1080 target, the native median is 1.99425 ms and the enabled median
is 3.70745 ms, a 1.71320 ms difference. The completion window includes the
original draw, component copies/composition, real HDR ownership exchange,
supplemental TAA and AgX publication. It excludes source setup, ordinary motion,
the per-frame M clear, readback and Present. These numbers are not additive GPU
pass costs or gameplay FPS.

This qualifies the default-off route, one actual original pair, real component
and HDR ownership, supplemental temporal consumption, Reset/recovery and the
stated failure path on the X3 CrossOver/FEX backend. The fixture uses
`X3M_SCENE_HOOK=0` and a fixture-only owner override, so production game-memory
owner binding reuses the earlier bloom evidence. The failed draw does not prove
partial GPU-submission atomicity, and its Present is an unresolved fallback, not
a healthy Present-triggered resolve. Production owner binding, other exact source
pairs in this combined path, gameplay appearance/FPS, native Windows, device loss
and consecutive lazy-bias retention remain outside this result.

### Next performance experiment

The bounded performance pass selects fusing the exact A-to-B copy and E-zero
initialization into one two-target draw as the next experiment. M stays detached,
blending stays off, and A/native B/source-once recovery remains unchanged. This
removes a clear/setup operation, not E bandwidth; native clear may already be
efficient. Require paired measurements with exact B/E channels, untouched M,
partial preparation faults, native fallback, state restoration and Reset before
adopting it. Pool-generation descriptor/view reuse and fewer redundant local
state sets are later candidates; arbitrary public exchanges retain full
validation. Historical adjacency and partial source coverage do not establish
safe batching or scissored composition.
