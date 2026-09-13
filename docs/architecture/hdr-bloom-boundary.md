# HDR bloom boundary: original execution followed by RGB replacement

2026-09-13. **Live integration implemented, reviewed and fixture-qualified; gameplay pending.**
The [GPU executor](../verification/bloom-pass-fixture.md),
[CPU/SEH bridge](../verification/review-43-compositor-bridge.md), and
[shader bundle](../verification/review-47-bloom-programs.md) now have scoped
standalone qualification. They are connected to the installed renderer behind the opt-in bloom switch;
see [current status](../status.md) for the installed build. This design uses the verified
[compositor control flow](../reverse-engineering/compositor-and-glow.md) and
[late-view inheritance findings](../reverse-engineering/bloom-late-view-state.md).
The earlier [full-bypass contract](../reverse-engineering/bloom-compositor-skip.md)
remains useful for a future optimization; its unresolved residue-equivalence
questions do not block this initial approach.

## Decision and ordering

Prepare the replacement from the pre-compositor scene, execute the original
compositor completely, then overwrite main RGB with the complete replacement
while preserving destination alpha and restoring the original outgoing state.
The original effect and game state manager perform their own transitions, and
the original intermediate textures and main alpha remain genuine. No original
draw is suppressed in this first implementation.

1. Select exactly one eligible owner from the engine device interface, frame
   and reset generation. Resolve TAA and retain this frame's pre-compositor
   FP16 scene. Finish existing scene redirection with the normal no-bloom
   writeback. At that established pre-original baseline, capture state and
   prepare linear bloom and the complete display-transformed candidate in
   scratch inside one state bracket. Restore that exact baseline before the
   original compositor's `GetRenderTarget`, on both preparation success and
   failure. Publish a post-write ticket only after restoration succeeds.
2. Execute the original once, unchanged. Its fills, scene copy, software-VP
   transition, all effect passes, main composition and cleanup remain real.
3. At its return, validate the same owner/generations and expected main/depth
   binding. Save the actual outgoing state and retain a GPU rollback copy of
   the now-finished original main image. If either operation fails, leave the
   original image in place and decline the replacement.
4. Write the complete display-code candidate to main as a deterministic,
   single-target copy: unbind RT1 through the supported MRT limit, disable
   blending, set RGB-only `COLORWRITEENABLE=7`, and explicitly set
   `SRGBWRITEENABLE=FALSE` and the source sampler's `SRGBTEXTURE=FALSE`.
   Establish the remaining copy state explicitly; do not inherit transforms
   or secondary writes from the original effect. Restore every state category
   touched by injected work.
   Publish committed status only after both write and restoration succeed.
   Continue at the original game return address before the first late view.

The candidate must **never sample the original-bloomed main RGB**. Its scene
and bloom inputs were retained before original execution; the post pass
overwrites RGB rather than adding to main. Thus the visible result contains
the new bloom once, although the original compositor still runs. Destination
alpha retains the game's actual result, including alternate effect behavior;
there is no invented alpha-zero policy or stock-effect-hash admission gate.

The normal no-bloom writeback is the sole path allowed to queue this frame's
exposure meter and advance its meter ring/adaptation. Resolve TAA and update
its history at most once for that frame, using the existing attempt latch.
Latch the exact EV consumed by the no-bloom display transform in the invocation
record; candidate preparation uses that same EV with `meter=false`. Candidate,
fallback and recovery copy paths all use `meter=false` and cannot advance
exposure, queue a second readback-ring slot or touch TAA history. Two calls to
an existing HDR copy helper must not implicitly create two meter submissions.
An absent/failed TAA attempt or a repeated boundary cannot start a new attempt
through preparation or post work.

This keeps all four original GPU passes, plus the original copy and clears.
That cost is deliberate for the first implementation and must be measured.
Removing obsolete original work is a later optimization, not a correctness
requirement for the visible bloom replacement.

### Exact display parameters from writeback

`HdrPass::write_back` now accepts an optional `HdrDisplaySnapshot*`, cleared
on entry. Only a successful AgX draw with successful state/RT restoration
and any owned EndScene publishes it. It contains the exact c8–c21 block,
decode selection, effective sharpening strength and c23, dimensions and whether
the source was the caller's resolved texture. A clean unsharpened retry reports
zero sharpening. Identity fallback, failed/unwound writes and rebind-only calls
leave it invalid. Meter failure alone does not invalidate a successful image.

The snapshot neither submits a meter nor computes exposure. The default null
output skips snapshot copies and validation. It owns no COM references. The
MotionOutput handoff below supplies the corresponding pre-original scene before
redirect cleanup; the caller still associates it with the owner/frame/Reset-
qualified invocation. Neither API alone enables bloom or authorizes a
post-compositor write.

The author ran 52 paired host scenarios (104 invocations of the extracted,
unchanged production `write_back` function), plus 26 AgX/exposure checks and a
real MinGW x86 translation-unit compile with the project SSE2/stack flags.
The orchestrator independently reviewed source and fixture with no findings.
The fixture checks exact uploaded blocks, invalidation, both fallback ladders,
at-most-once meter requests and equal device-call sequences/results with null
output. Scripted D3D outcomes qualify this metadata/control flow; existing GPU
state/image evidence remains separate. No GPU calls, allocations, locks or
exposure work were added, and no Wine rerun was needed for this change.

### Synchronous MotionOutput scene handoff

`MotionOutput::scene_end_hook` accepts an optional `MotionHdrSceneCallback` and
caller context. On the existing first eligible Scene-phase hook, it runs the
existing HDR resolve and the normal writeback once through their existing
paths. After a clean AgX writeback, before `end_redirect` releases main and
clears its resolved pointer, the callback receives `MotionHdrScene`: borrowed
device/scene/main pointers, device ID, frame and resource generation, and a
reference to the exact `HdrDisplaySnapshot` consumed by that writeback.

The callback is synchronous, under the existing capture mutex, and is only a
retain/copy handoff. Its references expire when it returns. The caller copies
the snapshot and takes its own scene/main COM references if needed; it must not
throw, reenter MotionOutput, Reset/destroy/invalidate resources or begin GPU
preparation inside the callback. Preparation starts after `scene_end_hook`
returns. This API neither registers invocation storage nor provides Reset/SEH
revocation: those remain required caller work under the ownership contract
below. Ordinary HRESULT failure cleanup is not a claim to recover arbitrary
SEH from COM calls.

With TAA enabled, only this Hook's successful HDR resolve qualifies: a
non-null resolved source, admitted attempt, successful operation/restoration
and resolved/HDR verdict. A failed, skipped or earlier attempt still takes the
existing unresolved display fallback but produces no handoff and cannot cause
another resolve. With TAA disabled, the handoff acquires one temporary
`GetContainer(IID_IDirect3DTexture9)` reference to the unresolved HDR target
after successful writeback. Native COM supplies an owned reference; the public
ownership wrapper supplies its canonical texture wrapper. Either is borrowed
by the callback and released afterwards, including a non-null output returned
with a failed HRESULT. The resolved path needs no extra container acquisition.

Admission also requires an open scene, idle queries, no state-block recording,
no release/reference-probe activity, no prior HDR unwind/block or restoration
failure, matching main identity/dimensions and matching snapshot source and
dimensions. `bloom_boundary_available()` exposes the current CPU-side gates
for later requalification; it permits the correctly ended redirect and does
not establish owner/frame/Reset identity or native post-state validity.
`reference_accounting_busy()` exposes release and temporal-reference-probe
activity to the capture owner's combined lifetime accounting.

A null callback requests no snapshot, container reference or identity lookup.
No new resolve, writeback, exposure-meter submission or history update is
introduced. Existing `capture && taa_debug` diagnostics already flush the
unresolved image before their resolved writeback and can therefore submit more
than one meter; this handoff preserves that behavior and forwards the final
display parameters. Bloom preparation must add no meter submission of its own.

The focused host fixture passes **72 paired scenarios, 1,526 checks** and
compiles the actual MotionOutput header and five
unchanged function bodies (`scene_end_hook`, `resolve_allowed`, `resolve_hdr`,
`hdr_writeback`, `end_redirect`) with scripted renderer/D3D outcomes. It checks
callback ordering and caller-owned references, exact payload forwarding,
single-attempt/default-null parity and the admission/failure cases. This is
control-flow evidence, not GPU restoration, native Windows or invocation
Reset/SEH qualification. The real translation unit is cross-compiled with the
project x86 SSE2/stack flags; no Wine or game run is added for this handoff.

## Ownership, lifetime and return bridge

The default `capture.cpp::scene_end_signal` broadcasts a void notification,
and the default scene hook tail-jumps to original without a post callback. The
optional bloom pre/post route uses one synchronous invocation record containing the exact
owner device, thread, frame, reset generation, original continuation and
retained scene/candidate/main references and the exact pre-original depth
surface reference or an explicit known-null value. Capture that depth identity
at the restored pre-original baseline, compare it again after original return,
and release it with the invocation. An arbitrary valid post-call depth surface
is not an equivalent check. Match the verified engine device
pointer exactly, or qualify canonical `IID_IUnknown` identity if necessary;
never OR device decisions or select by surface dimensions/map order.

The live capture callback only delivers that exact scene-end signal after a
safe owner and its observed scene thread are selected. An unknown owner, wrong
thread or invalid caller executes original once without broadcasting injected
GPU work to other devices. Existing StretchRect/selector/EndScene fallback
remains, with potentially different resolve timing. A safe-owner glow-off or
candidate failure still gets its ordinary exact scene-end writeback. The
`bloom_refusal` diagnostics record whether `ordinary_signal` was delivered.

The pre callback checks the live glow preference, eligible Scene phase,
successful redirect unwind, main identity and replacement resources. An
identified safe owner still receives the ordinary scene-end signal with glow
off or unavailable bloom resources. Unknown owner, wrong thread and nested
invocations decline injected work and run original once; they rely on the
existing StretchRect/selector/EndScene fallback chain instead of the exact hook
signal. This intentionally changes fallback timing rather than broadcasting
GPU work to an unqualified owner. Refuse active queries and state-block recording, and recheck
both immediately before any post-call GPU work: the original ran between those
checks. A failed prepare leaves no post-write ticket. Do not repeat exposure metering, TAA
resolve or history updates in the post callback or recovery.

Pin the owning device context/resources through the original call using the
existing mutex/lifetime discipline, while permitting that same thread's device
hooks. A raw map pointer surviving an unlocked original call is insufficient.
Reset, destruction and generation changes invalidate the ticket; a reference
alone does not authorize use after Reset. Crucially, `before_reset` must revoke
the invocation and drop **all invocation-held `D3DPOOL_DEFAULT` references
before calling native Reset**, including scene/candidate/main, saved-state RT
and depth references, recovery resources and the retained pre-original depth.
Registered invocation storage must let that hook perform the release even if
the wrapper still exists on the stack. Its later cleanup must be idempotent,
and its post callback must observe revocation. Waiting for a post-Reset
generation mismatch can itself make Reset fail because these references remain
live. Apply the same revocation discipline to owner destruction; consume the
ticket exactly once on every other exit.

The verified original takes no stack arguments and ends with plain `RET`.
The implementation must use an **SEH-aware wrapper frame spanning the original
call**, with a toolchain-supported Windows SEH cleanup/finally path that revokes
the ticket and releases invocation references during abnormal unwind. The [isolated compiler-SEH prototype](bloom-return-bridge.md) now passes
synthetic checks on both CrossOver bottles; production packaging and game
integration remain unqualified. Merely
replacing the return address does not install such cleanup, and C++ RAII alone
must not be assumed to run for Windows SEH. The wrapper forwards the original
exception without swallowing it and never executes post GPU work on unwind.
If the production toolchain cannot supply that contract, qualify a compatible
SEH wrapper implementation before integration; a bare return-address bridge is
not a fallback.

Its assembly entry/return bridge preserves integer registers/flags, x87 state,
MXCSR and LastError across injected work. Capture the original call's output
state **immediately on return, before any C++ callback, logging, locking or
destructor runs**, then restore that captured output before the caller resumes.
Validate the real caller continuation and the four-byte incoming-stack
contract. The wrapper's extra CALL/frame must be proven harmless for this
verified no-stack-argument callee; do not infer that from a decompiler
prototype. Both normal and SEH exits require fixture qualification.

## State and recovery coverage

New preparation work has one state bracket around all its GPU mutations,
starting from the established no-bloom pre-original baseline. Save every state
category it can change before its first mutation; restore the exact saved
values on success and failure. Preparation failure with successful restoration
permits the ordinary original-only fallback. Preparation restoration failure
does not: revoke the ticket, disable/report unknown device state, and still
execute the original once under the existing failure policy without claiming
a clean fallback. Do not arm post work merely because scratch pixels exist.

All injected pre/post/backup/recovery operations use the captured native device
function table or a qualified internal-call bypass. They must not pass through
application-facing hooks as ordinary draws/setters: that would contaminate
selector events, route state, application shadows and subsequent manager-cache
assumptions. Restore native device state exactly without publishing injected
bindings as game state. Original game calls continue through their normal
hooks, and the bypass must not hide unrelated/reentrant application calls.

Save **original outgoing** state immediately before post-call injected work and restore
it afterwards. Do not restore pre-compositor state, call the game's cache-reset
helper or write its private caches. This preserves the state/cache relationship
the original produced without needing a census of all later material effects.
Any existing original failure remains an original failure; restoring the actual
state is not a claim to repair a manager cache already desynchronized by the
game's ignored HRESULTs.

The post pass initially admits the expected main backbuffer, valid dimensions,
no MSAA and known depth identity (including null). Validate full-main viewport
when qualifying the original boundary; injected work sets its own full output
viewport and restores the actual outgoing viewport/scissor. Wrong outgoing
RT or unknown state refuses post work, leaving original behavior. Do not
invent a successful original boundary from four attempted draws.

`HdrPass::SavedState` is a starting point, not blanket coverage: it captures
targets/depth, viewport/scissor, FVF/declaration, VS/PS, stream 0 and frequency,
texture/sampler stage 0, selected render/stage states and its PS constant range.
New texture stages/constants must extend this coverage. UP draws require
restoring stream 0. The RGB-only mask must itself be restored. A state block
alone does not prove target/depth/software-VP restoration succeeded.

The original's software-VP transition remains genuine. Injected shaders must
either work in that admitted mode or temporarily choose a supported mode with
documented `GetSoftwareVertexProcessing`/`SetSoftwareVertexProcessing` and
restore it. Existing `SavedState` does not include software VP. Initially
refuse an unqualified mode instead of silently leaving it changed. No
Wine-private device layout, lock, export or exact DLL hash is needed.

Before the candidate's first main write, retain the **finished original** main
image in a reusable GPU recovery surface. The public D3D copy path must be
qualified for its actual formats, resource types and scene phase on each
supported backend; failed/unsupported backup means no replacement that frame.
Use a same-format image capable of restoring RGBA exactly, without new
exposure, tonemapping or bloom. A shader recovery draw must account for all
its own mutations and finish by restoring the already-captured original
outgoing snapshot. A fresh snapshot of partially restored or failed setup
state cannot replace that recovery destination. Keep this recovery image separate from candidate input.
No production CPU readback is required.

| Failure point | Required action |
| --- | --- |
| Ownership/admission refusal before new preparation | Execute original exactly once; no post write. A safe identified owner retains ordinary scene writeback; unknown owner/thread uses the later established fallback chain. |
| Scratch preparation fails, its state restoration succeeds | Restore the exact pre-original baseline; execute original exactly once; no post-write ticket. |
| Preparation restoration fails | Revoke the ticket, disable/report unknown state, and execute original once under the existing device-failure policy; no clean-fallback claim. |
| Original abnormal return or owner/reset change | Consume/drop the ticket; no post write; retain normal exception/device policy. |
| Post-state capture or backup fails before candidate write | Restore any setup mutations; leave the finished original image in place. If that restoration fails, disable/report unknown state with no clean-fallback claim; main remains original if no main write occurred. |
| Candidate write fails | Treat main as potentially modified; recover finished-original RGBA and restore the captured outgoing state. Do not rely on failed draws being atomic. |
| Candidate write succeeds but restoration fails | Do not publish committed status. Recover finished-original RGBA and restore outgoing state; disable/requalify replacement under the existing device-failure policy. |
| Recovery or its restoration fails, or device is lost | Clear the ticket, disable replacement and report unrecovered image/state. No clean-frame or successful-commit claim. |

Original replay is unnecessary on this path: the original ran once, and its
finished image is the fallback. Do not replace that fallback with a no-bloom
scene plus a speculative second original call. Permanent device/restore
failure still has no verified clean continuation.

## Deferred main-draw suppression optimization

If measured cost warrants it, keep genuine original offscreen passes but
suppress only its main-target quad at device slot 81, instruction
`0x004c4e54`, return PC `0x004c4e56`, arguments `TRIANGLESTRIP, 0, 2`. This needs
exact thread/device/invocation/target qualification and exception-safe disarm;
topology or shader identity alone is insufficient. Suppress every admitted
main write, not a hardcoded fourth draw. Do not add ordinary per-draw getters
or allocations for an inactive scope.

Suppressing all quads would leave cleared glow textures black while their
bindings can survive into later materials. Keeping offscreen passes preserves
that content. Even main-only suppression removes original alpha writes and
therefore needs an independently qualified alpha policy or genuine alpha-only
execution; the chosen full-original/RGB-overwrite route avoids this issue.

Before suppression, validate that the three acquired game texture surfaces are
valid, pairwise distinct and do not alias main or scratch; that main cannot be
sampled by a later offscreen pass; and that no extra MRT receives suppressed
side effects. Observe successful target bindings, not requested destinations.
An unknown successful `RenderColorTarget0` string does not take a safe main
fallback: the corrected compositor note shows its unchanged/uninitialized
target local. Modified/dynamic passes require semantic admission and relevant
effect/D3D HRESULT observation, not stock effect or DLL hash gating.

The verified original's three fills and retried main-to-scene `StretchRect`
precede the loop; the exact quad call is its only loop pixel-write site.
Forward those calls with their real results. Zero draws after a failed copy
is not successful state-only execution. Suppression requires a separate
no-bloom recovery/replay design, because no finished original main image exists
to back up. These conditions are optimization prerequisites, not requirements
for the chosen full-original route.

## Qualification and performance

Fixtures must cover the pre/post bridge with full CPU/stack preservation and
SEH cleanup and immediate original-output CPU capture; glow off; wrong/two-device
ownership; wrong thread; reentrancy; frame/reset mismatch and owner destruction;
native Reset observing zero invocation-held default-pool references; exact
pre/post depth identity including null; post-call query/recording changes;
native/internal-call isolation of injected state; preparation/state-save/backup/
write/restore/recovery failures; null depth; non-main viewport; software VP;
and genuine outgoing manager same-value setter behavior. Distinct original
RGB/alpha and offscreen textures should prove that candidate RGB has no old
bloom contribution, alpha and original resources remain intact, and late draws
inherit the same state. Verify one original call and at most one candidate
commit per invocation, with no live ticket after return.

Measure new bloom, retained original compositor, backup/copy GPU work and
state/CPU boundary cost separately. Reuse resources and bounded invocation
records; add no validation/image-backup CPU readbacks or per-draw effect
introspection. The existing exposure meter retains its single scheduled
readback path. Fixture
timings are not game FPS. Report persistent and peak VRAM for the candidate,
recovery image and bloom scratch, including overlap with retained scene/TAA
resources. Measure capture-mutex hold time spanning original execution and
contending wait time separately; do not hide synchronization cost inside a
shader-only timing. Batch flight/HUD/text/menu-glow diagnostics into one
future build for the user to launch. No game launch is authorized to the agent.
Public D3D source and cross-compilation support both required targets; native
Windows runtime behavior remains unverified.

## Independent design review

Sol/xhigh review closed all high/medium findings after revisions to preparation
restoration, Reset-time reference release, exact depth ownership, SEH/CPU output
preservation, post-call admission, single metering/EV ownership, deterministic
MRT/sRGB state and rollback of failed writes/restoration. The annotation-target
correction was checked against assembly. This is design approval only; the
bridge and renderer implementation still require source and runtime review.


## Live integration qualification (2026-09-13)

The working integration selects the production pre/original/post bridge only
with `X3M_HDR_BLOOM=1` (`manage.py --hdr-bloom`), the motion route, FP16 HDR,
AgX and the scene hook. Default-off retains the ordinary scene tail jump.
The scene site and immutable binding stay installed across device destruction;
explicit shutdown requires external quiescence. See the selected lifetime
policy in [the owner study](../reverse-engineering/bloom-invocation-owner.md).

The live callsite's adjacent layer test, branch, CALL, compositor-done write
and following reload were checked again against the installed EXE with bounded
objdump disassembly. The wrapper path checks all 25 bytes, and all production
scene installs now respect the engine patch install window. The once-per-frame
legacy signal also uses `-fno-exceptions`, keeping MinGW SJLJ bookends outside
its CPU transport contract.

The focused production scene-hook fixture passed **43/43 checks in X3**:
ordinary tail forwarding, bridge original-once behavior on acceptance/refusal,
actual caller PC, copied binding, callback storage/context, refusal of shutdown
inside an active invocation, wrong target/missing callback rejection, exact
byte restoration and late-install refusal. It reuses the already-qualified
bridge CPU/SEH transport; it does not replace that matrix or establish live GPU
ownership. [Compact result](../../verification/results/bottle-X3/scene-compositor-summary.json).
Reproduce with an existing CMake bridge package (no production rebuild):

```sh
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py \
  python3 verification/probe/run_scene_compositor.py --bridge-dir build/compositor_bridge
```

Four launcher dry-run controls pass: enabled bloom, default-off, missing AgX
rejection and explicit scene-hook-off rejection. The combined lifetime fixture
passes 714/714 checks in X3 with zero skips; the host lifetime/admission fixture
passes 29 scenarios and 111 checks. [Review 50](../verification/review-50-hdr-bloom-live-integration.md)
approves the code and scoped evidence with no open findings. The clean candidate
is installed in X3; [the compact install record](../../verification/results/bloom-install.json)
binds its source, toolchain, byte identity, load check and rollback. Gameplay
quality, frame cost and native Windows remain unverified. Use run 3 in the
[brief queue](../verification/user-runs.md) for the exposure/bloom comparison.

Following emission integration, the host lifetime fixture now models the
emission-operation guard and scene-owner admission used by production Reset.
Its **33 scenarios / 139 checks** include reentrant Reset/ResetEx refusal while
emission is active, preserving the invocation and resources before native
dispatch, and admission revocation on ordinary successful or failed resets.
Independent review and the focused host test pass. This repairs the test
double and extends coverage; production behavior and the installed DLL are
unchanged.
