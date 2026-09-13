# Screen emission with overlapping primitives

Architecture investigation and isolated prototype, 2026-09-14. The design began
against `c3732c8`; the packed prototype starts from `29da5a7`. No runtime change
or live integration. The completed bounded X3 packed probe is recorded below. Root reports the separate SM1 source probe passed 1,674
rows across six modes and confirmed the shared-MRT screen gap; that result does
not qualify the packed route below. This supplements the [SM1 source study](linear-emission-sm1.md)
and [emission ownership contract](linear-emission-composition.md). The installed
additive route and the separate nine-pair SM1 promotion work are unchanged.

The proposed ordinary four-target **B/E/q/M** screen extension is insufficient.
Do not ship it by excluding overlapping draws: overlap can occur between
primitives inside one native `DrawIndexedPrimitive` (DIP). Neither geometry
replay admission nor a Direct3D9Ex migration should become an incidental
prerequisite of the SM1 shader patch.

## The missing operation

For each surviving fragment and RGB channel, the selected material law is

```
L' = E + (1 - q) L
```

Here `q` is the native encoded source channel and `E` is the independently
computed linear emission. Native encoded composition instead computes
`B' = q + (1 - q) B`. The alpha tested by the native material remains its sampled
source alpha `a`; it is not generally any one `q` channel or the emission gain.

Ordinary D3D9 applies the same blend state to all MRTs, although each output uses
its own source value. `ONE/INVSRCCOLOR` therefore accumulates E using **E's**
attenuation. With initial L=1 and two fragments `(q,E)=(.5,.2),(.25,.1)`, the
required result is `.1 + .75*(.2 + .5) = .625`. Accumulated E is incorrectly
`.1 + .9*.2 = .28`; even a correct retained transmittance `.75*.5` then produces
`.655`. Separately storing aggregate q cannot recover the missing ordered
cross-terms. This is not an exposure or threshold problem. See Microsoft's
[blend factors](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dblend).

Two whole-DIP passes, first attenuation and then addition, are also wrong:
all attenuation followed by all emission does not interleave the fragments.
Two passes **per primitive** are a different proposal, with topology, instance,
ordering, clipping and derivative obligations and potentially two submissions
per primitive. They do not provide a cheap substitute for one enhanced DIP.

## Ordinary D3D9 candidate: pack both laws by channel

A mathematically complete single-DIP option exists if native fallback can be
retained as packed data until an assembly draw succeeds. It uses four FP16 MRTs
and `ADD/ONE/INVSRCALPHA`. It does not require geometry replay or independent
blend functions.

Keep encoded scene A immutable. For each channel c, prepare a plane P_c with
RGB lanes `(A_c, decode(A)_c, 0)`. RT0 is the persistent reactive mask M: preserve
its red coverage lane while seeding its alpha from A.alpha. An initialization
fullscreen draw can do both using independent output write masks; it never
samples M while M is bound. At the original geometry submission, write:

| Output | Shader value | Writable lanes |
| --- | --- | --- |
| RT0 M | `(1, 0, 0, a)` | red, alpha |
| RT1 P_r | `(q_r, E_r, modified_r, q_r)` | RGB |
| RT2 P_g | `(q_g, E_g, modified_g, q_g)` | RGB |
| RT3 P_b | `(q_b, E_b, modified_b, q_b)` | RGB |

`modified_c` is one when q_c or E_c is nonzero, otherwise zero. Each plane's
source alpha supplies its own scalar attenuation, even though writes to that
plane's destination alpha lane are disabled. The source factor must still come
from the corresponding oCi.a; qualifying this with masked plane-alpha writes is
an actual-device fixture gate. Its red and green lanes
respectively accumulate native encoded B_c and desired linear L_c **in the
original primitive order**. No application texture, vertex or index input is
sampled a second time. Its blue flag remains nonzero after any modification:
if a later source has `modified=0`, q is also zero and the old flag survives.

Hardware alpha testing uses RT0's native `a` and rejects all outputs together.
Depth/clip/discard tests likewise remain those of the original submission.
For the studied screen state without separate alpha blending, changing the
RGB destination factor leaves RT0 alpha's native equation unchanged:
`a + (1-a)*A.alpha`. The initial proposed scope requires full native RGBA writes and separate-alpha
blending disabled. A later separate-alpha case must retain its native alpha
operation/factors on RT0 (and native alpha write enable), while masking plane
alpha writes; it needs independent capability/state/parity proof. It is not
covered by the initial scope.
[Microsoft's MRT contract](https://learn.microsoft.com/en-us/windows/win32/direct3d9/multiple-render-targets)
specifies the shared state, per-output blending, RT0 alpha-test source and
independent-mask restriction.

RT0 red computes `1 + (1-a)*old_mask`: every accepted fragment marks coverage,
including zero gain and alpha zero when the native test accepts it. Existing
[temporal resolve](../../src/temporal/resolve.hlsl) reads only mask red and accepts
only ordered exact zero as safe. Larger positive/nonfinite flag values must
remain conservatively reactive; they must never be interpreted as color or
normalized opacity. Reusing M.alpha is a proposed internal layout change, not a
claim that all present pass, history-copy and diagnostic consumers are qualified.

Assembly obtains native RGBA B from the three red lanes plus M.alpha. Enhanced
C uses the encoded green lanes, but copies A_c exactly wherever that channel's
blue flag is ordered zero, avoiding a spurious decode/encode change. Native
alpha comes from M in both cases. The source q/a domain must be proved bounded
as studied; arbitrary out-of-range blend factors are not silently clamped into
this proof. Shader precision and FP16 blend rounding still require native
parity tests. HDR accumulation must remain representable or have an explicitly
qualified overflow policy; real-number algebra alone does not prove either.

### Availability, failure and cost are separate gates

This route retains enough information for B, but **not a ready native RGBA
surface immediately after the source**. A native assembly pass must succeed
before the existing ready-B fallback guarantee becomes available. A conservative
sequence assembles B first, then C; C failure can then publish B, subject to the
existing successful bind/restore/ownership acknowledgement.

**Persistent B-assembly failure is an open integration decision.** No intercepted
call contract has been accepted for this case: the application-visible HRESULT,
the physical RT0 restored before return, and the count and disposition of any
synchronous bounded retry remain undecided. Retrying assembly from immutable
completed planes requires no geometry replay, but its availability does not
solve persistent failure. Returning an injected failure while restoring A would
lose successfully accepted source work; root explicitly has **not** accepted
that as the production policy. Reporting native success or native fallback with
only A/packed planes available would also be false.

A conditional last-resort content recovery is possible with documented APIs:
read each completed, non-MSAA FP16 plane into one reusable SYSTEMMEM surface,
copy raw halfwords P_r.R/P_g.R/P_b.R/M.A into a CPU output buffer, refill that
surface and upload it into already-bound B with UpdateSurface. Same dimensions
and format, pitch-aware copies, successful locks/unlocks and a complete upload
are mandatory. No float conversion or geometry replay is needed. This could
overwrite a partial B assembly on a healthy device; it cannot repair a failed B
bind, restore, ownership exchange or device loss. Existing after-EndScene
readbacks do not qualify this in-scene round trip.

This optional rung is **not selected for production**. Its four full readbacks
and one upload move at least 37.5 MiB at 1280x768 or 79.1 MiB at 1920x1080,
plus CPU combination and a GPU/CPU synchronization. Preallocated staging plus
output storage would add 15/31.6 MiB respectively. Consider its actual in-scene
qualification only if normal packed performance justifies this route; do not
add it simply to claim unconditional failure recovery.
[GetRenderTargetData](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-getrendertargetdata),
[UpdateSurface](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-updatesurface)
and [FP16 memory layout](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dformat)
define the relevant portable API/bit-copy contracts.

A future contract must prevent a later application call from observing private
packed storage or falsely ready B, and must coordinate native restoration with
logical target/owner publication. If the API itself refuses RT0 restoration,
safe physical binding cannot simply be promised. That failure needs an explicit
supported recovery boundary, not an assumed successful setter. Until these
return/publication semantics are resolved, this is a mathematical prototype,
**not an integration-ready route**. Failed/partial source execution remains
incomplete and revokes mask/history validity; no retry may replay geometry or
manufacture a completed source result.

A first implementation would plausibly need A plus six owned targets
(P_r/P_g/P_b, M, assembled B, C), versus the present four B/E/C/M targets. That
is two additional FP16 surfaces: about 15 MiB at 1280x768 or 31.6 MiB at 1920x1080.
The conservative sequence adds **three fullscreen draws per admitted DIP**:
one initialization, one native B assembly and one enhanced C assembly, in
addition to the one original geometry submission. Packing may be more expensive
than the effects themselves. Resource reuse or
fusing B/C assembly is an optimization requiring its own failure/alias proof,
not an assumption in these counts.

Admission requires the exact studied native state: `ALPHABLENDENABLE=TRUE`,
`BLENDOP=ADD`, `SRCBLEND=ONE`, `DESTBLEND=INVSRCCOLOR`, full RGBA writes and
`SEPARATEALPHABLENDENABLE=FALSE`. The derivation does not cover arbitrary blend
states. It must also check `SrcBlendCaps` for ONE and `DestBlendCaps` for the
substituted INVSRCALPHA factor, and qualify that substituted operation on the
selected FP16 format.

It needs four simultaneous targets, matching sizes, independent masks,
post-pixel MRT operations, and successful FP16 render-target/blend format checks.
D3D9 does not guarantee those FP16 operations universally; Microsoft's
[CheckDeviceFormat documentation](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3d9-checkdeviceformat)
even cautions that floating-point post-pixel checks may fail. A documented
capability check plus actual device qualification is required on both platforms.
The proposed hardware minimum is four qualifying MRTs; support is not assumed
on every Windows backend. A documented minimum is legitimate, and a lower-cap
route is an optional compatibility expansion unless it would regress a published
support baseline. Capability refusal can preserve native rendering but is not
an enhancement on that device. This proposal has no native-Windows runtime
qualification, and its four-target minimum exceeds the present three-target
emission admission.
No MSAA, hardware fog/dither, depth output or depth/stencil mutation is added to
the admitted domain. Native depth rejection and alpha-test comparisons must be
retained. Shader output/register budgets and independent mask behavior need a
real four-output program, not extrapolation from the present three-output pass.

The existing owner/frame/thread/generation, idle-query, no-recording/reentry,
Reset/handoff-pin and target-publication rules still apply. Assemblies use only
owned immutable results, not game-resource replay. Physical RT0 restore and
HdrPass ownership exchange must agree before later scene consumers may observe
B/C. Lost-device or failed restoration is not permission to publish partial work.

## Alternatives and their actual prerequisites

**Three channel replays per DIP.** Execute native B once; seed a linear target;
replay the complete DIP once per RGB channel using ONE/INVSRCALPHA, source alpha
q_c and that channel's write mask. Keep RT0 native B write-disabled, with the
shader still writing native a to oC0; put the linear output at RT1. This resolves
the hardware alpha-test conflict. It is mathematically correct for intra-DIP
overlap because each channel sees the original order. Coverage and unchanged
channel preservation need explicit auxiliary flags. This is four geometry
submissions, not two whole-DIP attenuation/add passes.

That algebra also requires unchanged fragment eligibility. Native B may have
changed depth/stencil before a replay: for example, a later nearer primitive
can prevent a replayed earlier fragment that originally contributed. Therefore
the initial replay domain must prohibit depth writes and stencil mutation and
require no active queries. Otherwise it needs owned/replayable depth, stencil
and query semantics as well as resource stability. Reissuing a queried draw
would itself alter the native query result. Preserving order inside each pass
does not solve either problem.

It is **not currently a live option**. As recorded in
[motion replay exclusion](motion-replay-exclusion.md), references preserve
allocations, not VB/IB/texture contents. Capture locking does not exclude
Lock/DISCARD, outstanding mapped-pointer writes, ProcessVertices, query issue,
state-block application or raw forwarding. A backend mutex is not a portable
solution. Even immediate replay needs complete application write exclusion or
owned stable copies, including all referenced geometry streams and textures.
Absence of D3DCREATE_MULTITHREADED does not certify that the application meets
that stronger contract; its documented role is runtime thread-safety behavior,
not a replay lease. See [D3DCREATE](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dcreate).

**D3D9Ex dual source.** On a qualifying Ex device, a single enhanced DIP could
write E in oC0 and q in oC1 and use ONE/INVSRCCOLOR2 into decoded L. Native a stays
in oC0 for alpha testing; native alpha needs explicit separate-alpha handling.
This is the direct overlap equation, with no replay stability problem. But the
[documented dual-source mode](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dblend)
is for RT0, not independent native B and M outputs. L cannot reconstruct native
B for arbitrary overlapping fragments. Post-source conversion failure therefore
needs an explicitly changed contract retaining L for retry, or incomplete output;
it cannot promise the current native B recovery. Reactive coverage also needs
a new proof rather than an unannounced second geometry submission.

This is not a flag change on an existing ordinary device. The Ex creation and
ownership route, pool behavior and loss/presentation semantics need integration.
In particular [D3DPOOL_MANAGED is invalid on Ex](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dpool),
and [Ex loss behavior differs](https://learn.microsoft.com/en-us/windows/win32/direct3d9/lost-devices).
The game compatibility burden can exceed the proposed emission change.

**Different material law.** A scalar opacity p permits `E + (1-p)L` with ordinary
source-alpha blending. Choosing p from native alpha or max(q) changes colored
attenuation and requires an artistic decision. Preserving native B as well is
still a separate output/blend problem; it does not disappear by renaming q.

**A later D3D11 renderer.** Independent per-target blend state offers more room
for native B and channel planes, but dual source still does not permit arbitrary
MRT output alongside it. A compatibility renderer must specify its feature level,
output layout, alpha/depth behavior and failure contract. The relevant contracts
are [D3D11_BLEND_DESC](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_blend_desc)
and the [output-merger stage](https://learn.microsoft.com/en-us/windows/win32/direct3d11/d3d10-graphics-programming-guide-output-merger-stage).
A full renderer migration is a separate project, not an SM1 conversion step.

## Recommendation and next decisive evidence

Keep SM1 promotion separate and do not integrate the incorrect B/E/q/M screen
law. If the project chooses to pursue ordinary D3D9 first, the packed-channel
single-DIP candidate is the favored prototype, pending explicit review of its
fallback/publication contract. The detached proof below is only a component gate before live wiring: it avoids
the known replay exclusion blocker while retaining reconstructible native color.
Its assembly-dependent fallback and bandwidth cost must be accepted explicitly.

The decisive fixture is a **single indexed draw containing overlapping
primitives**, with asymmetric colored q, distinct E, reversed order, alpha-test
accept/reject (including accepted zero alpha), depth rejection, zero gain and
unchanged channels. Compare packed B against the unmodified native source on
the same device; compare C against the ordered linear oracle; verify mask and
exact alpha. Then inject source/assembly/restore/publication failures and Reset,
and measure source versus initialization/assembly at representative sizes and
overdraw. Those tests need no gameplay load. They establish component behavior,
not a native-Windows runtime result until actually run there. The isolated prototype below implements and qualifies the bounded mathematical
component on X3; no production route is qualified.


## Isolated packed producer checkpoint

`LinearEmissionSm1Outputs::PackedScreen` is a separate four-output probe mode in
`src/renderer/linear_emission_sm1.{h,cpp}`. It reuses the six exact original PS
proofs and nine unchanged VS/pair contracts. It writes M and the three channel
planes above; it does not output a ready native B surface. Partial precision is
rejected for this mode. Native q and sampled alpha remain unmodified. The
established full-precision E path retains source sanitation, decoded-result cap
before fade/gain, final sanitation and finite gain [0,16]. A three-CMP predicate
marks signed nonzero q or positive E; ordered zero alone is unchanged, so the
prototype does not hide negative q by clamping it into a supported domain.

The producer uses only r0–r3 and c0/c30/c31, one sampler and the existing
COLOR0/TEXCOORD0 interface. Its maximum is **201 DWORDs**, **42 arithmetic slots
plus one texture instruction** for scalar profiles, or **41 plus one** for
bullets. POW is counted as three slots. Exact output MOVs, initialized lanes,
one constant read port, full precision, instruction framing and baseline PS2
limits are checked before returning the generated vector. Output failure and
input/output aliasing retain the prior contract. No per-DIP state, D3D calls,
logging, allocation or runtime admission is added by the pure transformer.

Twelve focused transformer tests pass. The host driver covers 210 variants,
all nine exact pair memberships and more than ten thousand structural checks,
including direct resource/precision mutations. Independent arithmetic checks
plane source-alpha/q equality, M native alpha, signed/zero modification flags,
cap-before-quarter-gain, ordered linear recurrence and unchanged channels. All
**180 existing SM1 variants** match the pre-edit canonical SHA-256
`21d26569494ffc58ece3d2de069093746feabb76cbafc678b8058fe3b486adf7`
(sorted filename, NUL, then output bytes); all **100 existing SM2 variants**
still match their accepted GPU hashes. Strict x86/SSE2 core compilation passes.
An initial optimized host diagnostic measured about **0.170 ms for 210
creations** before the final extra packed-precision refusal checks. This is
creation-only diagnostic time, not driver cost, steady GPU time or game FPS.

The finite producer does not by itself bound native q. For example T=256 and
h=.125 produce q=32, despite finite/capped E; negative floating samples also
produce signed native q. The raw algebra then has a negative or greater-than-one
retention factor, so it is not a convex recurrence. Two raw-algebra q=2
fragments would also cancel a previously set flag: `1+(1-2)*1=0`, invalidating
unchanged-channel selection. Device clamping of blend
factors cannot be assumed equivalent between source color and source alpha
without measurement. The prototype therefore separates the [0,1] q/a contract
from signed, greater-than-one and overflow observations. As a useful sufficient
safe color domain, nonnegative T<=1, h<=1 and gain<=16 imply E<=16q; from
linear background L<=16 the ideal recurrence stays <=16 at arbitrary overlap.
This does not certify all native game textures or render states. Mask/modified
flags have a different real-number bound: repeated accepted a=0 fragments grow
M.red with fragment count, and tiny nonzero q can grow the modified flag. FP16
rounding may instead lose small increments; neither a normalized-opacity bound
nor an ideal unbounded-growth claim describes every device readback. They are
reactive predicates, not color or normalized alpha, and range/overflow
observations must remain explicit. No broad clamp or admission policy is introduced by this prototype.

The separate packed fixture source is frozen for all nine original pairs:
**24 conditions × nine pairs × three schedules = 648 measurements**, with
36 variants at gains 1, 0, .25 and 2.5. Schedules submit the two overlapping
primitives in one DIP, two ordered DIPs, and reverse order. Native B RGBA and
unchanged channels are exact comparisons; C and the linear planes use an
independent CPU ordered oracle with measured half-ULP input intervals, transfer
arithmetic tolerance and a target ULP per store. It checks masked plane-alpha
retention while those source alpha values drive blending, M.red preservation
while initialization seeds M.alpha, native alpha tests and depth rejection,
one-DIP/two-DIP equality, a distinct reversed-order result, and Reset. Four
signed/>1/HDR/overflow conditions remain separate boundary diagnostics. No
successful boundary row broadens the admitted mathematical domain.

Authored helper budgets are initialization **22 ALU + one TEX** (two temps,
one sampler, one constant, four outputs); native B assembly **5 ALU + four TEX**
(five temps, four samplers, no constants, one output); C assembly **26 ALU +
five TEX** (seven temps, five samplers, one constant, one output). The fullscreen
VS2 uses two MOVs. B assembly consumes only the three planes and M; C also reads
immutable A for ordered-zero channel preservation. Its comparison uses ABS of
the blue flag so negative flags are not silently classified as unchanged.

Eight focused tests in `test_linear_emission_sm1_packed_report` pass. The
canonical report aggregates 27 pair/schedule rows and useful first failures;
detailed case results and readbacks stay local. It does not log individual
source DIPs. A dedicated 1920×1080 diagnostic separates native DIP,
initialization, packed DIP, B assembly and C assembly, each with four warmups
and eight timed samples. Its eight FP16 fixture targets occupy **132,710,400
bytes**, including the independent native baseline; the proposed algorithm's
seven-target A/planes/M/B/C footprint is **116,121,600 bytes** at that size.
These are target-storage counts, not total driver allocations or measured cost.

The same independent Sol/high reviewer approved the pure core and all four
fixture files with no findings. The subsequent strict x86/SSE2 standalone build
passed on its first attempt, without source corrections. The retained untracked
EXE is `verification/probe/build/linear_emission_sm1_packed_fixture.exe`,
11,143,288 bytes, SHA-256
`6e50d7e097a4c7b9f56dbe5eb5caa7687efd1d56b2363331616f7d53349cb9a2`.
The bounded X3 result follows below. The current twenty-pair runtime registry and
all native-B assembly/publication failure questions remain unchanged.

Focused host/build commands from `/tmp/x3-emission-sm1-packed`:

```sh
PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_linear_emission_sm1_transformer
PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_linear_emission_sm1_packed_report
sh verification/probe/build_linear_emission_sm1_packed.sh
```

The root-owned runner consumes that explicit prebuilt artifact and executes
functional and benchmark processes sequentially under one Wine lease. It has no
build path. Each process has a 900-second timeout; completed timings follow
below.
Probe completion is distinct from in-domain qualification, and neither grants
live publication or an out-of-range color contract.


## Bounded X3 packed result

The root-owned run of the frozen EXE completed both functional and benchmark
processes without rebuild: **7.819 s** and **1.516 s**, respectively. The
[compact result](../../verification/results/bottle-X3/linear-emission-sm1-packed.json)
binds that EXE, all fifteen originals and six focused source inputs; all
bindings still match. All **36 creations** succeeded. Of the **648 measurements**,
the **540 in-domain rows pass every gate**, with zero native-B RGB/alpha, C,
plane, mask, initialization, unchanged-channel or one-DIP/two-DIP failures.
The largest reported normalized comparison fraction among those rows is
**0.248828**. Independent masks, native hardware alpha tests, accepted zero
RGB/alpha/gain coverage, native depth rejection, primitive-order sensitivity,
actual Reset and post-Reset continuation pass. This qualifies the bounded
single-DIP packed algebra and its component assemblies on the tested X3 backend;
it does not grant live publication or native-Windows behavior.

There are **36 failed boundary rows among 108 boundary measurements**. These
must not be hidden by `qualified_in_domain=true`:

| Boundary | Rows | Recorded implication |
| --- | ---: | --- |
| Signed q | 27 | No diagnostic comparison failure in these selected values; no signed-q domain is admitted. |
| q greater than one | 27 | Nine reversed-order rows each have 350 plane-comparison failures (largest normalized fraction 1.793109). In the other eighteen rows, 1,050 channel/pixel positions per row have a zero blue flag despite a changed green lane. |
| T=256, reduced fade/gain | 27 | Selected finite-HDR comparisons pass; this does not establish an arbitrary HDR blend domain. |
| T=65504 overflow | 27 | Every row records 3,150 plane disagreements and 3,150 nonfinite-comparison events against the overflowing raw-equation FP16 reference. |

The q>1 forward/two-DIP flag cancellation is a correctness limit even though it
is deliberately a separate diagnostic rather than an in-domain failure: the
assembly copies A for those zero flags, so it cannot represent the changed
linear channel. A zero C-comparison count in that boundary is **not** proof of
the desired out-of-range composition law. Native assembled B RGB and alpha are
bit-exact against the original in all 648 rows, including boundaries, but that
alone cannot qualify C or the modification flags.

The overflow counter means **actual or reference** was nonfinite; it is not an
independent count of nonfinite GPU storage. The reference recurrence exceeds
FP16 representability and disagrees with the measured planes. Only the first
boundary failure and unconditional q>1 witness retain raw surfaces, so these
aggregates do not establish the overflow case's actual storage bit pattern or
a portable saturation policy. No clamp, expanded q domain or overflow policy
is adopted from this result. The source remains unchanged.

At **1920×1080**, the eight separately synchronized EVENT/QPC samples per phase
have these CPU-inclusive medians:

| Phase | Median ms |
| --- | ---: |
| Native one-DIP baseline | 0.43290 |
| Plane/M initialization | 0.93210 |
| Packed one-DIP source | 0.68705 |
| Native B assembly | 0.69665 |
| Enhanced C assembly | 0.69710 |

The four enhanced-phase medians sum to **3.01290 ms**, compared with 0.43290 ms
for the native draw. This is a strong cost warning for three additional
fullscreen passes per admitted DIP. It is **not a measured contiguous pipeline
cost or slowdown**: each phase was synchronized separately, includes its stated
CPU/binding work and excludes source setters, seeds, resource creation and
oracle readbacks. No game-FPS claim follows. Target storage remains 126.5625 MiB
for eight fixture targets, or 110.7422 MiB for the proposed seven algorithm
targets at this size. No further benchmark was run.

The mathematical prototype is complete for this checkpoint. The twenty-pair
runtime registry remains unchanged. B-assembly failure handling, physical
restoration/publication, live ownership/history behavior, and native Windows
qualification remain open; `live_publication=false` is retained in the result.
The same Sol/high reviewer approved the scoped actual evidence and owning note.
Frozen inputs, compact aggregates, retained range/failure witnesses and the
functional/benchmark raw records agree. No source changes, rebuild, rerun or
additional benchmark were needed for that review.
