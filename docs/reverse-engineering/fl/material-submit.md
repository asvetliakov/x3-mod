# FL material submission: copied node identity and transform

Offline bounded study, 2026-10-28, against `/mnt/c/X3FL-orig/X3FL.exe`,
SHA-256 `e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
Addresses are preferred VAs (image base `0x400000`); use RVAs plus the admitted
loaded base. Owning plan: [bounded renderer sampling](../../architecture/farnhams-legacy-engine-profiles.md#bounded-renderer-sampling-plan-after-value-lifetime-qualification).
No production changes, game/Wine execution, builds, installs or commits.

**FL's actual seam is `0x4e7142 → 0x4e0f30`.** Descriptor and node are in
ECX/EDX; camera and three further words are on the stack. The highest required
node word ends at **+0x154**, since FL model/LOD are `+0x144/+0x150`, not AP's
`+0x140/+0x14c`. This does not require reading the intervening unused words.
Copy the required fields before forwarding and publish only owned data to
nested D3D consumers. An AP-style delayed `current()` heap read inside the
material call is not this contract. Standalone reader activation remains
unqualified: the native borrow needs the owning complete-pass admission below.

**Superseded 2026-11-01 (step 3a).** The entry-time-copy contract above is
replaced: the lazy `current()` read inside the open call is ratified, because
the engine itself dereferences the node for the duration of the call and the
dispatcher's own SEH frame clears the scope on unwind, so no read outlives the
engine's own use. Evidence: the native fixture `verification/probe/fl_object_trace.cpp`
and the forthcoming flight ([object identity](../../architecture/farnhams-legacy-object-identity.md)).

## Evidence and reproducibility

```sh
python3 verification/probe/verify_fl_material_submit.py
python3 -m py_compile verification/probe/verify_fl_material_submit.py
```

[Verifier](../../../verification/probe/verify_fl_material_submit.py), measured:
25 regions, 10,144 decoded instructions, 161 operand anchors, 52 independently
checked region/seam edges, ten rejected negatives. Nine negatives bypass
both exact-region hashes and operand anchors; they test the target, camera
push, caller cleanup, model/LOD offsets, null-node denial, descriptor register,
and VM slot/call transport.
The tenth redirects a real call to an operand byte inside the hook span and
must fail the inbound-edge census. The two argument branches are followed to
the seam and their four outgoing words compared in actual stack order.

The fixed results directory is ignored:
`verification/results/fl-port/material-submit/`. It holds raw disassembly,
`report.json`, the producing/reproduction scripts and supplemental caller
censuses. The verifier emits derived counts and hashes, never executable bytes
into tracked evidence. Whole-text direct-edge/HIGHLOW censuses are conservative
static evidence, not proof against arbitrary computed private calls. Field
interpretation comes from the named native loads/stores, not from a matching
AP offset or a successful readable-page check.

## 1. Submission chain, exact ABI and hook boundary

| Native edge | Established transport |
| --- | --- |
| Immediate `0x497035 → 0x4e6ef0` | `0x497030` pushes EDI=camera; `0x497031` sets EDX=ESI=node; `0x497033` sets ECX=mesh-part; caller removes four bytes at `0x49703a`. |
| Deferred `0x497702 → 0x4e6ef0` | ECX=record `+8` part; EDX=record `+0x10` node; EBX camera pushed at `0x497701`; four-byte cleanup at `0x497709`. Record producer stores part, geometry and node at `0x4970a7/aa/ad`. Queue records are not retained object identities. |
| Wrapper `0x4e6ef0` | Saves node at EBP-0x14; part `+0x64` supplies descriptor at `0x4e6f28`, saved at EBP-0x1c. Camera is recovered from original stack through EBX+8 at `0x4e6fc8`. |
| Convergence `0x4e712c..0x4e7147` | Reload ECX=descriptor; EDX has been reloaded with node on each projection branch (`0x4e7022`, `0x4e70f4`, `0x4e7116`). Both light branches push four words, then one CALL at `0x4e7142`, then `add esp,0x10`. |

At target entry, the contract is:

| Location | Meaning |
| --- | --- |
| ECX | Material/geometry descriptor; opaque identity token for this snapshot |
| EDX | Render node |
| ESP+4 | Camera |
| ESP+8 | Flags, 0 or 2 here; low byte tested at `0x4e135b/0x4e1453` |
| ESP+0xc | First optional light-node pointer, from ESI |
| ESP+0x10 | Second optional light-node pointer, from EBP-0x18 |

The target preserves the pre-alignment stack in EBX (`0x4e0f31`), hence
camera/flags/lights appear there at `+8/+0xc/+0x10/+0x14`. Descriptor is saved
at EBP-0x78 and node at EBP-0x1c (`0x4e0f62..67`). Light consumption is checked
at `0x4e44ec/0x4e4687`. Returns use plain RET; EAX is a scalar result.
The wrapper overwrites that result with 1 at `0x4e714d`; no floating return or
callee stack cleanup is established. AP's six-stack-word cdecl adapter would
lose ECX/EDX and forward the wrong stack.

The candidate hook is **only `[0x4e7142,0x4e7147)`**, one five-byte relative
CALL, no HIGHLOW operand. Its direct target has exactly one observed caller;
the wrapper has two. No surveyed direct branch targets a seam interior and no
HIGHLOW pointer names the seam or the fourteen surveyed entry targets. This does
not permit live patching: retain quiescent install, exact loaded-image checks,
ownership/rollback and teardown rules. No prologue or native SEH record needs
relocation when redirecting this call.

ECX/EDX and all four outgoing words are live at entry. EBX/EBP identify the
wrapper's original/aligned frames; ESI/EDI still hold native values. Preserve
all GPRs, EFLAGS, LastError and FP/SIMD state across observation, with the
four-byte incoming-stack contract and explicitly aligned helper storage.
Do not infer XMM scratch permission from AP: FL projection uses SIMD, and the
material body has extensive XMM use. The native target's first arithmetic
prologue kills incoming arithmetic flags; the caller's `add esp,0x10` kills
return flags before its continuation. Preserving them remains a simple adapter
contract. Save native EAX before exit bookkeeping even though this caller
currently overwrites it.

Both wrapper and material target install native FS:[0] exception frames.
A new observer scope must have per-invocation nesting and foreign-SEH unwind
cleanup; a C++ destructor alone is insufficient. Continued exceptions retain
the scope; unwind removes just that invocation. No native pointer may be read
by cleanup. TLS publication failure must deny observation without changing
forwarded native arguments. These are implementation requirements, not a
qualified new trampoline; existing AP/factory fixtures do not prove this ABI.

## 2. Exact finite field set

| Copied value | FL offset/width | FL evidence and meaning |
| --- | --- | --- |
| Node and camera handles | `+0x28`, DWORD each | Successful allocation stores at `0x4a22a8` and `0x4a4606`; registered numeric identity, not a reference count or readiness event. |
| Parent | node `+0x18`, DWORD | Reparent `0x4a57d0`: incoming parent is original stack+4, saved in EBP at `0x4a57e8`; links child into parent `+0xc` list and stores EBP at `0x4a5809`. It is the immediate parent, not necessarily the ultimate ship/root. Zero is valid. |
| Model/resource key | node `+0x144`, DWORD | Traversal load `0x4968e1 → 0x4a1a40`; setter publishes at `0x4a365d`. Node birth initializes -1 at `0x4a22cc`. |
| Selected geometry/LOD | node `+0x150`, DWORD | Load `0x496f96`, resource `+0xc` array indexing at `0x496fa0`; native selection stores include `0x4962a1/0x4963bb`. Birth sets zero at `0x4a230f`. |
| Flags | node `+0x12c/+0x130`, two DWORDs | Visibility at `0x4968d4`; material reads `+0x12c` at `0x4e0f87`; projection tests `+0x130 & 0x200` at `0x4de5af`. These are mutable native bits; do not import every AP bit name. |
| Position | node `+0xb0/+0xb4/+0xb8`, three DWORDs | Signed integer conversion at `0x4deb15/2a/3f`, multiplied by the camera's context scale for the ordinary world matrix. These raw words are not float bits. |
| Base position | node `+0x30/+0x34/+0x38`, three DWORDs | Required by the subsequently qualified [Background transform](background-temporal.md); distinct from effective position at `+0xb0`. |
| Basis | node rows `+0xc0/+0xd0/+0xe0`, three DWORDs per row | Nine `movd/cvtdq2ps/mulss` consumers at `0x4de9ef..0x4dea99`; signed fixed-point conversion uses constant `0x591994` = 1/65536. Row padding is not a fourth basis component. |
| Scale | node `+0x70`, `+0x80/+0x84/+0x88`, four DWORDs | Base factor at `0x4de59a`, rounded signed products at `0x4de973/0x4de9a1/0x4de9c3`. Per-axis defaults 0x10000 at `0x4a2352/5c/66`; model setter also changes these fields. |

The initial implementation copied the entire **340-byte** node prefix plus the
**4-byte** camera handle. The [complexity audit](../../verification/farnhams-legacy-complexity-audit.md)
reduces the base read set to **25 node DWORDs (100 bytes)**, including the later
Background base position, plus the **4-byte** camera handle. Scratch retains
native offsets; unused words and basis-row padding need no native reads.
Extract typed scalars from owned storage. Do not recursively follow any pointer
solely because it was copied; optional own-ship ancestry has its separate
[borrow contract](own-ship.md). The AP
0x150-byte buffer both reads the wrong model/LOD fields and ends immediately
before the FL LOD word. FL `+0x140` is separately initialized at `0x4a23a2`;
this note does not assign it the AP model meaning. Capture-only alpha/matrix
semantics are a separate qualification, not inferred from this copy.

Descriptor, node, camera, parent, engine and registry addresses may be retained
as **identity tokens only**. Engine/registry/incarnation/load generation and
serials come from the admitted observer's mod-owned binding, not by rereading
`*0x5aa100` then a possibly stale engine `+0xc`. The camera read is solely its
handle: it grants no scene, view or projection-buffer access. Native camera
context scale explains field meaning but is deliberately outside this read set.

## 3. Actual borrow, readiness, writers and reentrancy

The seam is reached from native rendering, not a registry lookup. The target
explicitly rejects null descriptor, node and camera at
`0x4e0f6c..0x4e0f81`, before its first node read. A snapshot must make the same
null denial before touching either allocation. Native render flags further
reject hidden/ineligible nodes (`0x4e0f87..0x4e0fa0`); a copied scope does not
claim that a draw was issued. Descriptor readiness is left with native code;
this snapshot never dereferences it.

The complete native submit cone is bounded: traversal `0x4968c0` has four
observed callers, two recursive and two in scene submission `0x4978e0`.
Scene submission has five callers: three in complete pass `0x48b6d0`, two in
cube rendering `0x4977c0`. Cube rendering has only `0x48b9f6`, inside that same
complete pass. Deferred drain has three callers, also in these pass/cube bodies.
Thus the fixed native seam belongs to the complete-pass borrow, including cube
views; the observer must not silently treat every camera as the main view.

Readiness is achieved along this native preparation path, not on insertion:
`0x497720` invokes LOD/visibility selection `0x495e50` at `0x497745` before
submission (ordinary `0x48ba4c → 0x48ba73/7f`, second pass
`0x48bc96 → 0x48bca9`, cube `0x49786e → 0x497892/9d`). The selector's two
observed callers are that edge and its own recursion. Traversal can then
replace camera-facing basis rows, including stores at `0x496f82/88`, before
projection `0x496fb9` and immediate/deferred submission. Projection's complete
`0x4de570..0x4deb8f` body has **zero CALLs**. It reads and converts the specified
node fields. The copy therefore observes current submission-time values;
`+0x30/+0x40` are not substituted as previous-frame transforms.

**The whole pass is not immutable.** Model setter `0x4a35e0` has 79 surveyed
direct callers and reparent `0x4a57d0` has 24. A real model mutation inside the
pass is `0x48bc69 → 0x48af40 → 0x48b0a9`; it finishes initialization and
publishes the new node at `0x48b0e8` before subsequent submission. Native LOD,
flags and basis writers also run between samples. These are synchronous native
writers, not reasons to hold a lock through D3D or freeze the whole pass.

For the supported owner-thread path, make the entire copy **call-free** and
finish before forwarding to `0x4e0f30`. The native material body first calls an
external target at `0x4e0ff8`; it subsequently calls effects/device methods many
times. Reentrant draws may receive nested owned snapshots, and mutations or
resource releases after forwarding cannot change the outer copied values.
The parent token must never be converted into dereference permission by a later
`engine_node_gather`, culling, lighting or history consumer. Ordinary motion
uses shader-shadow submitted rows; owning this scalar snapshot does not lease
its transitive engine/GPU readers or diagnostic matrices.

The copy's **entry-borrow prerequisite** is the existing
[complete-pass/load admission](ui-load-readiness.md),
[SC_CLOSE/native callback contract](ui-render-exclusion.md), and
[typed recovery/scene continuation closure](ui-render-mutation.md).
They matter before the sample: a returning close/KC destruction during an
earlier native call can invalidate saved camera/node pointers before this seam.
Membership or a final serial check cannot repair that. Conversely, after the
copy no exclusion is needed for its owned words. Recovery can reclaim attached
mesh/texture storage but the copy follows none of it; its actual native callback
closure does not run a model/reparent KC command. Normal end-of-pass playback
mutation is after all these submit calls. A raw full-pass depth counter without
that callback/continuation protocol is not borrow qualification.

### Native-command scheduling closure

The initially unresolved native-command registration has a concrete synchronous
transport. `0x48286f` loads the VM from `0x5aa174`; `0x482886..0x482897` stores
`0x4afbf0` in **VM + 0x30 + type_index*24**. In interpreter `0x4bed90`, the
VM is saved at EBP-0x14 (`0x4bedb9`). Native opcode dispatch at
`0x4c075c..0x4c0783` computes the same `(type_index+2)*3*8` address, loads the
registered function and calls it with five stack words; `0x4c0785` removes
0x14 bytes. The B3D cases then synchronously call model setter at `0x4afcb7`
and reparent at `0x4b08a6`. This is a type-table function invocation, not a
worker entry or an asynchronous job publication.

| Actual interpreter path | Scheduling evidence |
| --- | --- |
| Ready-task scheduler `0x4788e8 → 0x4bed90` | `0x4788b0` reads VM+0x12d8 ready list and passes its current task synchronously. Its four callers are `0x477e63/0x477e68/0x477ebf` in outer loop `0x477ba0`, and `0x4b319f` in `0x4b3120`. The latter has only `0x477d52`, also in that outer loop. The same loop calls complete render at `0x4782cf`. Reuse its qualified normal CFG/owner and callback admission; these are not parallel dispatches. |
| Method helper `0x4bb4f0 → 0x4bed90` | `0x4bb470` saves the task into VM+0x1434, then immediately calls the interpreter before returning. It is entered by synchronous method wrappers `0x4bb500/0x4bb5b0/0x4bb6c0`; known SC_CLOSE uses this family and therefore requires deferral. |
| Interpreter recursion `0x4c0588 → 0x4bed90` | Synchronous nested native call, not another scheduler. |

The bounded census finds exactly those **three** direct interpreter entries and
no HIGHLOW pointer publication of interpreter, scheduler or method-helper entry.
The registered completion callbacks already qualified in the render-exclusion
note only store results/ready state; they do not call these interpreter entries.
Window delivery, typed allocation recovery and the render-end scene callback
are covered by the named owning notes, including the positive close exception.
The active native-command transport therefore adds **no demonstrated concurrent
writer or new callback frontier** to the supported owner-thread pass. Known
same-thread mutations before the seam are allowed and complete before the
call-free copy. This resolves the particular scheduling edge; do not leave a
speculative requirement for arbitrary whole-engine thread exclusion.

This conclusion is conditional on implementing/enforcing that existing native
owner/pass admission, not on a mere thread-ID check after a read. Arbitrary
foreign private-address calls, new callback registrations or replacement
vtable implementations are outside this fixed healthy-native profile. A future
supported callback that runs KC during a live render borrow must extend the
pre-mutation admission contract before it can use this snapshot.

## 4. Implementable snapshot boundary and remaining acceptance

1. Redirect the one CALL through a separately qualified register-aware adapter.
   Prepare a fixed-size stack scope and preserve native state. Use a positively
   admitted complete-pass/owner token and current observer binding; deny during
   load/factory/retirement, incomplete coverage, null arguments or invalid
   incarnation. Do not acquire permission by dereferencing an old global.
2. Under that actual native borrow, perform the 340+4-byte bounded copy with no
   calls, allocation, logging, waiting, D3D or engine pointer walk. If a shared
   metadata gate is required, enter before the first read and leave immediately
   after the last; native mutation must participate before its effects. The
   proven owner-thread path needs no native lock held across forwarding.
3. Resolve handles against mod-owned observer identities and publish an immutable
   scope record only if both node and camera identities are current. Membership
   validates identity; the native borrow supplied dereference/readiness permission.
   Bind the snapshot to load/incarnation/serial generations. Revalidation later
   can invalidate history, never justify reopening a token address.
4. Forward exactly ECX/EDX plus camera/flags/light1/light2. Nested scopes restore
   their parent on return/unwind. `current()` copies the innermost owned record,
   including capture-disabled normal motion; no late engine reread. Drop scope
   state without touching engine storage even when native forwarding unwinds.

The verifier's synthetic model checks four pre-read denials, two nested owned
samples, mutation after the outer copy, the corrected FL offsets and exactly
344 bytes per admitted sample. It is a specification witness, not execution of
an adapter or the native scheduling contract. Required implementation checks
remain register/stack/FP/LastError preservation, ordinary and continued/unwound
SEH, nested callbacks, TLS failure, loaded-base rebasing, quiescent patch/rollback,
and no pointer-following by consumers. Measure the eventual copy cost; the
roughly one-second offline verifier runtime is not game performance. No per-draw
allocation, registry enumeration or generic page-validation walk is required.

This study supplies the seam and finite data contract for implementation. It
neither enables FL consumers nor qualifies camera matrices, resource readers,
shader semantics, native Windows runtime behavior or rendered TAA. Activation
requires the named complete-pass/owner admission and the new adapter's fixture
qualification; the native-dispatch scheduling route above is resolved within
that supported profile.

## Independent source/evidence review

2026-10-28: the main agent reran the verifier and inspected its branch-stack
walker, semantic negatives and snapshot model. Independent targeted decoding
from native function entries confirmed both four-word argument branches,
ECX/EDX transport, stack cleanup, pre-read null checks and the VM table-call
sequence. No actionable finding remained for this static contract. The review
accepts the bounded entry-copy design with its explicit complete-pass/owner
prerequisite; it does not qualify an implemented adapter or enable consumers.

## Optional occlusion-owned inputs, 2026-10-29

The material owner now has an immutable cold demand: `Off`, `Draw` or `Engine`.
Existing preparation overloads select `Off`. `Draw` copies a bounded, ordered
prefix of **six ancestors**, immediate parent first; `Engine` also copies the
exact signed submission-time words at node **`+0xf0/+0xf4/+0xf8`**. These are
separate from world position at `+0xb0` and Background base position at `+0x30`.
This checkpoint supplies inputs only. It enables no renderer occlusion path,
body-name classification or engine-side setter/invalidation consumer.

The payload preserves the existing AP classifier's relation: a hull owns
itself; it additionally grants its immediate parent ownership only when that
parent is top-level. A part tests its first six ancestors in order. An own-ship
root or nozzle parent alone cannot encode that distinction. The copied payload
therefore carries `(token, handle, serial)` for each ordered ancestor, its count
and whether the immediate parent is top-level. A non-null seventh link is
allowed but never dereferenced; null terminates a valid prefix. Unaligned or
wrapping non-null links, zero handles or cycles within the copied prefix deny
this optional payload. Cycles through
the final copied node's parent are rejected using tokens alone.

All optional reads remain inside the existing call-free native-copy interval,
before native material forwarding and callbacks. The graph ownership and
healthy-parent lifetime prerequisite is the existing
[own-ship borrow contract](own-ship.md#3-why-the-existing-material-seam-can-own-this-copy),
under common owner/pass admission; membership does not supply read permission.
Native handle/parent copies are cached within this invocation and shared with
own-ship/nozzle demand. A failed own-ship walk cannot add unused identities to a
nozzle-only batch. Completed optional demands use **one** existing
`fl_owned_nodes` batch, with the material node first and no duplicate tokens.
A failed optional batch falls back to the base node/camera accessor and clears
occlusion inputs while preserving a valid base material record. A changed
binding or common borrow revokes the entire record as before. No new owner,
registry lookup, page-query framework, allocation or renderer native read was
added. There is no malformed-memory catch-and-continue guarantee: the existing
SEH envelope handles unwind, and no record is published before the copy and
identity validation complete.

### Field writers and immediate/deferred transport

[Focused verifier](../../../verification/probe/verify_fl_occlusion_inputs.py), run
against the qualified original FL image, measures **29 semantic anchors** in
five completely decoded regions: writer **236**, preparation **664**, complete
pass **554**, submit **1,007**, wrapper **172** instructions. It rejects **ten**
semantic negatives, including world/base offset substitutions, an unsigned
shift, wrong node registers/record slots, wrong targets, and synthetic scalar
and overlapping wide stores. These semantic checks do not depend on region
hashes. Raw bytes and listings remain local; the report carries derived counts
and hashes under `verification/results/fl-port/occlusion-inputs/`.

| Native address | Established meaning |
| --- | --- |
| `0x494865` | `mov [eax+0xf0],ecx`; EAX reloads the input node saved at stack `+0xc`. |
| `0x4948cc` | `mov [eax+0xf4],ecx`; the same node reload precedes this store. |
| `0x49492e` | `mov [esi+0xf8],ecx`; ESI reloads that saved node at `0x4948da`. |
| `0x48ba3a → 0x494b00` | Preparation precedes selection at `0x48ba4c → 0x497720`; preparation calls the field writer at `0x494f55/0x4951d3 → 0x4947b0`. |
| `0x497035 → 0x4e6ef0` | Immediate route passes node in EDX from ESI and camera from EDI. |
| `0x4970ad`, `0x4976fb`, `0x497702` | Deferred producer stores node at record `+0x10`; drain reloads EDX from that word and calls the same wrapper. |
| `0x4e7142 → 0x4e0f30` | Both routes converge before the existing material callback frontier. |

The writer uses rounded integer shifts/adds, including signed `sar`; the owned
payload preserves exact signed bits and asserts no conversion scale. The
**1,007-instruction** submit region `0x4968c0..0x497720` has **zero explicit
memory destinations overlapping `+0xf0..+0xfb`**. This includes wide stores;
the verifier explicitly accounts for Capstone's move-store access annotation.
This is a direct-destination census, **not callee or calculated-address alias
closure**. It does not establish that preparation values stay unchanged until
submission. The implemented boundary intentionally copies the current words at
the material seam. Engine-side use that assumes a longer immutable interval
still needs its own writer/invalidation acceptance.
