# FL shadow retention: class predicates and owned lifetime handoff

Bounded offline contract, 2026-10-28. **The five class exclusions are established
for FL. The current attachment, journal availability and delayed revalidation
are not an FL retention implementation.** No production code, game/Wine/native
execution, shared build, install or commit was performed. Addresses are preferred
VAs, base `0x400000`; use the admitted profile's loaded base for runtime code.

Sources: [material submission](material-submit.md),
[object lifetime RE](object-lifetimes.md), the later implemented
[owned lifetime accessor](../../verification/farnhams-legacy-lifetimes.md),
[AP caster lifetime](../shadow-caster-lifetime.md),
[retention design](../../architecture/shadow-caster-retention.md), and
[FL sun integration](../../architecture/farnhams-legacy-sun-light.md).
The historical FL lifetime RE note's initial capability warning predates the
factory binding and owned accessor implementation; its native-reader restrictions
still apply.

## 1. Exact predicates and verified FL correspondence

`shadow_retention_core.h::excluded_class` tests
`(flags12c & 0x10004220) || (flags130 & 0x200)`. These are exclusion predicates,
not a positive static-object tag or an own-ship tag. The existing owned FL
material record already copies the two DWORDs at node `+0x12c/+0x130`;
`render_engine_state::current` exposes them as `DrawSample.flags12c/flags130`.
`MotionOutput::sample_scope` deliberately transfers them into the retention
route only for AP today. The new evidence permits an explicitly FL-qualified
transfer of these five meanings, without treating all AP flag names as portable.

| Predicate | AP consumer | FL consumer and established effect |
| --- | --- | --- |
| `+0x12c & 0x20` | `0x47da04/0x47da0a`, branch `0x47da0c` | `0x496902` loads flags, `0x496908` tests CL, `0x49690b` branches to the next class if clear. Camera at `[EBP+0x10]`; node position `+0xb0..b8` minus camera `+0x30..38` at `0x496918..49693d`. Camera-dependent basis stores at `0x496bd1/0x496bd8/0x496bdf`, or transform destination `node+0xc0` at `0x496c76`. |
| `+0x12c & 0x4000` | `0x47dcd9` | `0x496cd3`, next-class edge `0x496cd9 → 0x496d33`. The additional `0x4000000` test can bypass this rewrite. Otherwise camera rows `+0x40/+0x50/+0x60` are copied to node `+0xc0/+0xd0/+0xe0` at `0x496cf3..496d0d`, or combined by `0x50ebe0`. |
| `+0x12c & 0x10000000` | `0x47dd25` | `0x496d33`, next-class edge `0x496d39 → 0x496dce`. Uses camera `+0x274` at `0x496d4b`, then camera basis `+0x40` and node basis destination `+0xc0` at `0x496db0..496dc1`. |
| `+0x12c & 0x200` | `0x47dd94` | `0x496dce`, clear edge `0x496dd4 → 0x496f8f`. Camera-relative position differences at `0x496de9/0x496dec/0x496e35`; basis writes at `0x496eff/0x496f49/0x496f82/0x496f88`. This is a camera-dependent basis class. |
| `+0x130 & 0x200` | `0x4bdf27`, ordinary-world edge `0x4bdf35 → 0x4be253` | Projection `0x4de5af`, ordinary-world edge `0x4de5b9 → 0x4de96d`; set-bit branch constructs the screen projection with viewport and anchor flags, including `0x1000` at `0x4de88b` and `0x4000` at `0x4de8f7`. |

Thus **both `0x200` tests are real and distinct**, in AP as well as FL. The old
retention design's uncertainty over which word carries the screen bit need not
be propagated: screen projection uses `+0x130`; `+0x12c` has an independent
camera-dependent basis branch. Keeping both exclusions preserves the policy.
All four FL traversal branches are after the renderable test at `0x4968d4`
and resource lookup; failure of renderability branches to child traversal at
`0x497550`. These are not an offscreen transform refresh contract.

The traversal entry `0x4968c0` takes the node in ECX, saves it in ESI at
`0x4968cd`, saves EBP/ESI/EDI and aligns local ESP to 16. Camera is the third
stack argument, `[EBP+0x10]`. The flags stay in ECX across the class-test chain.
Each TEST's flags are live to its following conditional branch. The projection
call at `0x496fb9` receives ECX=selected LOD record, EDX=node and first stack
argument=camera; `0x4de581` moves node to EDI. These are analysis boundaries,
**not new proposed hooks**. No register/flags scratch permission is inferred.
Reusing the existing material copy adds no detour and no new native read.

Stability remains the shared store's measured-pose policy: establish world rows,
then accumulate **eight** matching drift comparisons, with complete draw sets,
unchanged model/LOD and per-cascade epsilon tiers. This is not necessarily eight
total draw frames: the initial world-row sample starts with streak zero.
Movement drops/reset-classifies sightings; unseen moving nodes are dropped.
Unseen statics use the last accepted rows, box/age/buffer limits and the existing
bounded residual that an unseen object may start moving. Registry mutation
revision does not observe arbitrary transform, model, parent or flag writes.

Own-ship eligibility is separate. `motion_output_shadow_adaptive_inc.h` uses
`object_capture::own_ship(..., 0x608504)` and a bounded native parent walk,
behind `object_trace::executable_verified()`. Neither of the two flag words
identifies the player ship. Existing [FL UI input RE](ui-input.md)
establishes active cockpit registry `*0x5a82e4`, handle `+0x10`, resolver
`0x41e420` and some cockpit fields; it does **not** establish the complete FL
ship-root/ancestor handoff required here. A copied immediate `parent` token alone
does not establish full ancestry or authorize dereferencing it.

## 2. Journal reuse, ownership and exact missing semantics

FL already shares the observer-owned entry table, serial allocator and 2048-entry
retirement journal. `FlSink::retire` calls the common retirement path;
`invalidate` clears entries and emits the common whole-table invalidation;
`flush_history` emits `FlushAll`. Coordinator begin retires the affected key
before insert/remove forwarding, revokes map/engine before destroy, and bumps
load/history before forwarding load. Normal nested mutations reconcile dirty
keys only when mutation depth reaches zero. Foreign unwind invalidates without
reading a possibly half-mutated map. Existing ABI/unwind tests are recorded in
the lifetime verification note; they were not repeated here.

The existing journal's events and copied epochs can therefore support FL
retirement. **Its current availability result cannot.**
`object_lifetime.cpp::journal_drain` uses `fl_model.readable()`, which requires
`!factory_seen`; completed production from-empty construction keeps that latch
set. `fl_owned_ready()` deliberately accepts completed, covered construction
while rejecting factory/load/mutation scopes, dead/poisoned state, mismatched
counters and exhaustion. `fl_owned_binding/current` call that predicate under
the observer lock and perform no native payload, registry or ownership-code
read. Do not relax `Coordinator::readable()` or enable legacy `current()` to
solve retention: they intentionally retain a stricter native-reader boundary.

Missing consumer adapter semantics are:

1. A provider-aware attachment decision. The current
   `object_trace::active() && object_lifetime::active()` is AP-only. FL may
   prepare empty storage/journal when its startup providers are prepared, but
   may admit sightings only with an available owned binding and current
   qualified scene/material identity. Device creation can precede factory
   completion; avoid a permanent one-shot refusal that never retries activation.
2. FL journal availability using the owned readiness predicate under the same
   observer lock (either a dedicated drain adapter or edition-aware common
   drain). Preserve the cursor, overflow, consumer-registration and `FlushAll`
   semantics. Registration of an empty store is vacuously revalidated; a reused
   store needs full revalidation. A perpetual `available=false` would force an
   O(nodes) scan every frame even if replaced revalidation later succeeds.
3. A delayed owned revalidation callback. Obtain a **fresh** Binding, compare
   the retained engine/map and full observer/load/registry domain, then call
   `fl_owned_current(binding,node,handle,camera,camera_handle)`. Compare the
   returned node serial **and camera serial** with the retained identities.
   The existing node stores camera address/handle but no camera serial, and
   the current revalidation compares only node serial/load/registry. Same
   address+handle reuse is insufficient. Store the engine/domain once per
   store where appropriate and camera serial per retained node.
4. Keep binding revision stable during a revalidation batch and immediately
   recheck its binding before accepting the batch. Fresh revision is an
   exclusion/version check for this operation, not a permanent equality
   requirement against the sighting's old revision: unrelated completed
   mutations must not discard otherwise valid far casters. Overflow requires
   full revalidation; ordinary retirements remain O(events). A changed binding
   during the batch is context loss/retry, never successful validation.
5. Distinguish unknown/replaced node from lost camera/registry/context.
   `PointerMismatch` can describe either node or camera, so the existing
   generic `answered = ... PointerMismatch` does not faithfully distinguish
   camera reuse. A camera-only owned check (node and handle both zero) plus
   serial comparison, or a typed adapter verdict, can preserve the distinction.
   Neither failure may leave an admitted retained record.

`render_engine_state::revalidate(IdentityTicket)` is deliberately an immediate
pre/post-draw check against the **innermost** live material invocation. It is
not the delayed callback: a correctly retained node outlives that invocation.
The delayed path uses owned metadata only and never reads retained node, camera
or descriptor tokens. It keeps D3D VB/IB/declaration references as today; an
engine node token is not an owned native allocation. No engine memory lease is
created by successful lifetime lookup.

The observer's existing mutex protects each metadata operation, not an arbitrary
series of operations or COM callbacks. Drain, fresh-binding checks and batch
publication must account for intervening mutation/reentry. Do not call registry
or node readers from retirement hooks. Do not hold the observer lock across
AddRef/Release, logging, D3D work or engine calls. Existing callbacks retain
GPR/EFLAGS/LastError/full FP-state and foreign-SEH requirements; no new thunk is
required for journal adaptation. Retained releases must keep the existing busy
accounting and removal-before-Release discipline because wrapper final release
can reenter device teardown.

## 3. Concrete implementation and qualification scope

A bounded first change can qualify the owned retention handoff in
`object_lifetime.{h,cpp}`, `shadow_retention{,_core}.h`,
`motion_output_shadow_retention_inc.h` and `sample_scope`: provider/readiness
selection, FL drain availability, full owned retention identity/revalidation,
and the established class mapping. The store math, resource leases, epsilons,
caps, moving policy, eight-comparison promotion and far retention remain shared.
Tests must exercise completed factory construction, in-flight mutation/load,
overflow, retire/replace, same-address node **and camera** reuse, epoch change,
foreign unwind/disable, and observer loss with outstanding retained references.
Use existing synthetic lifetime and retention seams, not a game launch.
In particular, a same-address/same-handle camera replacement must invalidate the
old camera serial; an unrelated key insertion/removal must preserve a retained
caster when a fresh binding validates the same node/camera serials. Keeping the
old revision as a cross-frame equality requirement fails that positive case.
The existing `ShadowRetentionLifetime` function-pointer seam can select the FL
adapter; extend its typed revalidation input/result if needed to carry the full
stored identity and distinguish camera context loss, rather than routing it
through legacy `object_lifetime::current`.

Candidate publication is a separate consumer integration boundary in
`MotionOutput::after_draw`: it is currently guarded by `ap_native_families_`.
An FL candidate must require actual submission success, an owned sample from
that draw, unchanged/current FL material ticket, valid main-scene camera and
sun domain, and a nonrevoked draw sequence. Do this independently of temporal
history: `finish_fl_history` returns early when no history was recorded, and
on invalidation it does not itself clear `route.routed`. Removing the AP boolean
alone can publish an invalid FL route. If lease preparation invokes potentially
reentrant D3D/COM callbacks, revalidate before committing candidates/sightings;
failure must release provisional references and leave no published candidate.
At scene replay, use the scene/camera/sun freshness contract and owned retained
identity, not an expired material invocation. Preserve no-camera/no-sun,
stateblock/query, buffer-bookend, Reset/device-loss and rollback gates.

Alpha casters already have a distinct live path: `alpha_caster_source` captures
the comparison/threshold and holds a managed stage-0 texture lease; replay groups
alpha issues separately. **AP does not retain alpha-tested casters**: both
`note_refused_sighting` callers and the final `note_retention_draw` call explicitly
exclude `route.alpha_tested`, and retained payload has no alpha texture/test.
FL parity must keep these live alpha casters and their original test; never turn
them into opaque retained geometry. Extending retention itself to alpha would
be a separate feature requiring texture/test lifetime and revision evidence.

Own-ship adaptive cascade 0 remains a separate required adapter: establish an
FL ship-root token and ancestry at an owned/excluded engine boundary, carry
lifetime-qualified root/node serials and an ancestry generation or refresh
policy, then feed the existing extent/radius/hysteresis math. Root and node
pointer/handle cache keys alone are inadequate for a new FL lifetime contract.
No qualified FL root/ancestry copy or injection seam is established here; the
existing cockpit evidence is a starting point, not permission to use AP reads.
A full-feature claim cannot leave that adapter permanently refused.

Two policy limits should be explicit in implementation tests. Registry journals
do not report in-place flag/parent/transform changes. Also, current `Store::seen`
returns immediately for an excluded-class sighting, before finding/dropping a
previously retained serial; a newly alpha-tested draw likewise bypasses retention.
A visible transition from an ordinary retained caster to either excluded class
must retire/invalidate its previous retained opaque records, or an earlier
static record can survive as unseen. This source-derived corner is not an
observed game failure. It is distinct from the already accepted unseen-motion
residual and should not be hidden by the FL mapping.

Performance remains bounded: no new native reads or allocation per retained
sighting, one class mask, existing table/row work and first-resource AddRefs;
ordinary journal work O(retirements); O(nodes) only for overflow/unavailability
or a deliberately required revalidation batch. Do not use the old FL journal
availability and accidentally turn every frame into full revalidation. Runtime
FL shadow/own-ship/far-retention behavior and native Windows game behavior
remain unverified; source compatibility and synthetic execution are separate.

## Reproducer and measured evidence

Local ignored producer/result:
`verification/results/fl-port/shadow-retention-contract/verify.py` and
`verification.json`; `raw-disassembly.txt` stays local/untracked.

```sh
python3 verification/results/fl-port/shadow-retention-contract/verify.py
python3 -m py_compile verification/results/fl-port/shadow-retention-contract/verify.py
```

The producer uses project `inspect_x3.PE32` plus Capstone and verifies clean
image SHA-256 before decoding only the established traversal/projection regions.
FL: `/mnt/c/X3FL-orig/X3FL.exe`, 2,804,736 bytes,
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
AP: `/mnt/c/X3AP-orig/X3AP.exe`, 2,153,984 bytes,
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`.

Measured static checks: **4 regions, 1,673 instructions, 58 exact operand
anchors, 22 slice-boundary edges**. FL contributes 1,214 instructions/47
anchors/12 edges; AP 459/11/10. Slice boundaries are checked against decode
from the earlier established function entry, never by starting at an arbitrary
slice offset. **58 altered-anchor negatives** bypass whole-image hashes and
must fail decoded operands/boundaries; **2 interior-boundary negatives** also
pass. Result JSON is parsed and checked by the producer. No full-image call
census was repeated, no incoming computed-edge closure is claimed, and no new
hook span is qualified. Region hashes are provenance, not a runtime relocated
code-admission scheme.
