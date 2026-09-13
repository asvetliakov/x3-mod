# Screen emission with overlapping primitives

Architecture investigation, 2026-09-14, against `c3732c8`. No runtime change or
new GPU qualification. Root reports the separate SM1 source probe passed 1,674
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
fallback/publication contract. It needs a detached proof before live wiring: it avoids
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
not a native-Windows runtime result until actually run there. No such packed
fixture or new route has been implemented or executed for this note.
