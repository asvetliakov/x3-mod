# FL engine emitters, live nozzles and SETA inputs

Bounded offline contract, 2026-10-29. Preferred VAs below use image base
`0x400000`. Inputs: `/mnt/c/X3AP-orig/X3AP.exe` (SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`)
and `/mnt/c/X3FL-orig/X3FL.exe` (SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`).
**[s]** means measured static instruction/source evidence; **[i]** means
inference; **[u]** means unestablished. No production edit, build, Wine,
native fixture or game acceptance. FL engine-effects activation remains closed.

**The existing AP visual and throttle policy is reusable after a small native
input adapter. A rebase of the existing patch is insufficient:** FL changes
emitter A's argument ABI, reverses the two sites' atomic-write geometry, moves
the model key and configuration fields, and has an independently admitted
material/nozzle read boundary. The last point is a missing permission proof,
not evidence of different FL node ownership.

Owning shared policy:
[engine nozzle source](../../architecture/engine-nozzle-source.md),
[modern engine effects](../../architecture/engine-effects-modern.md), and
[AP engine effects](engine-effects.md). Reuse the existing
[FL material layout and borrow](material-submit.md),
[camera publication](camera-buffers.md), and
[owned lifetime](registry-value-lifetime.md) contracts; this
note adds engine-effect consumers, not a second general node layout.

## Reproduce and scope of checks

```sh
python3 verification/results/fl-port/engine-effects-contract/produce.py
```

[Producer](../../../verification/results/fl-port/engine-effects-contract/produce.py)
uses the project's `tools/analysis/inspect_x3.py` PE32 reader and Capstone;
[contract.json](../../../verification/results/fl-port/engine-effects-contract/contract.json)
contains derived hashes, counts and conclusions only. Measured [s]: two exact
input hashes, **25 complete decoded regions**, **108 instruction operand anchors**,
four call-window decodes and simulated redirect-boundary checks, all return
pop sizes in four complete callees, callee incoming-status-flag checks, and
**15 rejected one-byte mutations**. Mutation checks bypass image and region
hashes to exercise operand/ABI anchors independently. The producer validates
its JSON roundtrip and records the shared source inputs' hashes.

The full linear `.text` direct-edge census finds no decoded branch landing
strictly inside the four guarded windows. AP has no raw image dword equal to a
window-interior address. FL has one: value `0x415058` at `0x59bb99` is the ASCII
substring `XPA` in a PE `.CRT$XPA` section-name string; the producer classifies
it explicitly [s]. These finite censuses do not exclude computed targets,
third-party patches or every alternative x86 decode. No raw executable bytes,
shader bytes, decompiler text or full disassembly is written by the producer.

## 1. Suppression sites and native caller ABI

| Contract | AP | FL [s] |
| --- | --- | --- |
| Per-object emitter routine | `0x414590..0x41489a` | `0x414d90..0x4150d1` (exclusive ends) |
| Sole surveyed direct caller | `0x45ac7d` | `0x4645bd`, inside sector update `0x463000` |
| Effect A window / call / target | `0x4147c4..0x4147f3` / `0x4147eb` / `0x4148a0` | `0x415054..0x41507d` / `0x415075` / `0x4150e0` |
| Trail B window / call / target | `0x4147ff..0x414831` / `0x41482c` / `0x412d70` | `0x415085..0x4150af` / `0x4150aa` / `0x413640` |
| Class | signed 16-bit object `+0x48`, ship 7 | same, load `0x414e07`, ship dispatch `0x414e12` |
| A object at original stub entry | `[ESP+0x10]` | **`[ESP+8]`** |
| A arguments and cleanup | ten stack DWORDs; caller `add esp,0x28`; plain RET | **ECX=0, EDX=jet ordinal k, eight stack DWORDs**; caller `add esp,0x20` at `0x41507a`; plain RET |
| B arguments and cleanup | four stack DWORDs, object `[ESP+0xc]`, RET `0x10` | same; returns `0x41371e` / `0x413816` |
| A/B complete call fits aligned qword | yes / no | **no / yes**; same result for four-byte rel32 write |

FL `0x414d90` receives the object in ECX, preserves it in EDI at
`0x414da7`, and dereferences its live root before dispatch. The ship table is
`*0x5a83e4`, subtype stride `0xdb8`, effect `+0x54`, trail `+0xc4`
(`0x414e3e..0x414e4d`): these fields retain AP semantics [s]. Direct children
are walked from root `+0xc`; child `+0` is next and a next value of zero marks
the sentinel. Each JET child increments ordinal k; the emitter eligibility
condition remains `C & 0xffe == 0`. A requires effect ID >0 and no C `0x4000`;
B requires trail ID >0, no C `0x2000`, and cfg `+0xec & 0x40000000`
(AP cfg `+0xfc`). The separate class-0 path still has its additional second
flags-word gate; it is outside the two child suppression sites [s].

At FL A, the eight stack words are effect ID, object, three zeros, position
pointer, zero, zero; ECX is the effect-list key component 0 and EDX is k.
At B they are k, trail ID, object, position pointer. Therefore the FL A class
check must use `[ESP+0xc]` **after** saving EAX with PUSH; FL B continues to
use `[ESP+0x10]` after that PUSH. Both skip ship class 7 only. Other classes
must tail-jump to their actual native targets with **ECX and EDX preserved**,
as well as EAX and the other GPRs [s]. No emitter-type allowlist or new
ship-content policy is required by the mapped native code.

**Liveness and exception boundary [s].** On either child call, EBX=k,
ESI=child, EDI=object and EBP are live across the call and must survive. A's
return EAX is unused; at `0x41507d` ECX is reloaded, status flags are replaced
by TEST, and EAX is overwritten before its next read. After B, INC EBX at
`0x4150b5` replaces status flags; the next iteration overwrites EAX from the
child flags, or the epilogue returns constant 1. EDX is unused before a later
replacement. The ship skip may thus zero EAX and return with changed status
flags. Forwarding may alter status flags because each target writes them
before reading: FL A's first writer is CMP at `0x41510c`; FL B immediately
SUBs ESP at `0x413640`. Do not modify DF, FP/SSE state, segment state or
LastError in the stub. The current AP integer-only class-test/skip shape can
be reused with FL A's corrected object slot and targets; its RET remains
plain, B remains RET `0x10`.

A contains native SEH registration at `0x4150e3..0x4150f1` and restoration
before its plain RET `0x41526a`. A tail jump adds no handler/frame; forwarding
retains native allocation, exception and cleanup behavior. Ship suppression
avoids both native allocation paths. The object is already the native
routine's live argument; no page probe or registry lookup belongs in these
stubs. This inherits the native synchronous caller's lifetime, not permission
to retain that object beyond the invocation. No new mutable per-call state,
allocation, lock or reentrancy guard is needed [i].

**Installation boundary.** Existing `engine_effects_patch::install_at` compares
AP byte windows, even when passed different addresses. FL needs its own
admitted profile/windows and the corrected A stub. FL A's rel32 crosses an
aligned qword; its plain write must occur under existing startup quiescence,
with module pin, both-window verification before the first write, readback,
both-or-none rollback and owned restore. FL B can use the existing atomic
call claim. The AP assumption that B is the plain site does not carry over.
Do not infer startup safety merely from a first-Present heuristic [i].

## 2. Node traversal, classification and throttle

The material provider already establishes the shared position, basis, scale,
handle and parent fields. The actual engine-specific corroboration is:

| Native evidence [s] | FL contract and difference from AP |
| --- | --- |
| Classifier `0x43ddd0`; JET `0x43de9f..0x43deb3`; SMALLJET `0x43dec9..0x43dee8` | Model **`+0x144`**, as the existing FL profile; JET sets `+0x130 \|= 0x4000001`, SMALLJET sets `0x4000000` and threshold `+0x1d8=5`, unchanged bits. Lists are `*0x5a8314` / `*0x5a832c`. |
| Emitter list `0x414eff`, drive list `0x464630`, next `0x464bc4`, reparent `0x4a5809` | root `+0xc`, child `+0` next, child `+0x18` parent; same sentinel shape and direct-child jet drive as AP. This does not census FL scene assets for nested jets. |
| Scene-part C publication `0x4aa787..0x4aa791`; drive `0x4646b6` | record `+0x38` → node `+0x260`; retained C semantics. The proxy walk does not need to read C. |
| Drive `0x4646a6..0x4646da` | JET bit 1; speed `obj+0x10`, vmax from `0x443ee0` and ship tuning extension `+0x274`; target endpoints `0x4000..0x20000`, same as AP. |
| Drive `0x46484b..0x4648d8` | main `C&1` or C=0; visible `C&0x1000`, travel set selected by object `+0x44 & 0x4000000`; hide with node `+0x12c \|=0x100000`. Braking adds `0x40000`; retained AP rule. |
| `0x4648d8..0x464932` | up to three steering contributions, each `0x10000`, same as AP. |
| `0x464932..0x4649c3` | current node z at `+0x88`, limit `0x106*dt_ms` in both directions, scale setter only when changed; `0x4649be → 0x4a3ae0`. |
| Setter `0x4a3ae0..0x4a3b73` | writes `+0x80/+0x84/+0x88`; base `+0x70` times largest scale gives `+0xa0`; invalidates aggregate radius `+0xa4=-1`; same fields as AP. |
| Radius cache `0x4a39b0..0x4a3ad3` versus AP `0x488170..0x488267` | reads cached `+0xa4`, recursively recomputes if negative, publishes at `0x4a3ac1`. Same cached aggregate radius, not a different FL radius field. The proxy must not call this recursive native writer to obtain a radius. |

Thus ordinary main-jet target remains 0.25..2 and the proxy policy
`s=clamp((z-0.25)/1.75)` remains appropriate [s]. Native fixed-point remap
rounding and its degenerate-vmax behavior are not reimplemented: both draw
and node paths should consume the **actual** node scale. The shared guards
(unit x/y, z range `0x147..0x90000`, JET pair, no hidden bit, parent back-pointer)
and `far_record` construction can retain their policy after selecting the
correct model key. The RCS exclusion remains model 566 or a body-table
SMALLJET entry; resolve against FL's existing body input tooling, never an
AP-only body-ID list. AP scene-count maxima are not measurements of FL assets.

The pure `far_record` uses effective position times owned context scale,
normalizes negative model-z basis, computes size from model-x basis/base
scale/x scale, and derives throttle from actual z. That physical input law
is compatible with FL's existing material field contract. AP's flight-proven
c4–c6 shader-row recognition is **not** independently proven for every FL
engine shader by this study; FL shader admission and submitted-row agreement
remain separate prerequisites. Reuse the already-owned camera
`Record::context_scale`, not a later camera/context native dereference.

## 3. Size rejection and far records

FL selector `0x495e50` has two surveyed callers: recursion at `0x495f37`
and scene selection `0x497745`. It rejects frustum-invisible children at
`0x495fd3..0x495fe3`, before projected-size work. A hook at the eventual
size claim therefore still misses an off-screen nozzle [s], the reason AP
added the node walk. The mapped native size verdict is
`0x4961d3..0x49621c` versus AP `0x47d2a2..0x47d2ec`:
maximum positive own/parent `+0x1d8`, then measure<1 unless `+0x12c` has
`0x4000000`; rejected paths clear renderable bit 2. The existing integer
`engine_culls` policy can remain [s].

**The cull hook ABI is not a rebase:** here ESI=node, ECX=measure,
EBP+8=camera, versus AP EDI=node, ESI=measure and the AP stub's stack view
slot. The full FL replacement-stub register/FP liveness, continuations and
all its culling consumers are not qualified here [u]. This note establishes
native inputs/selection semantics, not installable FL small-parts machinery.
Existing draw→far→node dedupe, engine-culled pair suppression, caps and light
hold rule need no demonstrated FL policy change. Preserve the AP source's
explicit residual: a native size rejection past the mod stub's own threshold
can still be resurrected by the node walk; do not describe that AP policy as
an exact census of all native rejects.

## 4. Time, lifetime, reentrancy and the actual missing read window

Sector caller `0x443b76 → 0x463000` runs after sector simulation. It reads
cfg `*0x5a83b0` game milliseconds at **`+0x708`** (`0x46301c`), subtracts
object `+0xa8` (`0x46319d`) and caps negative or >1000 deltas to 1000 before
the jet ramp. The global engine gate before emitters is still nonzero first
word of the object registry (`0x4645ad..0x4645bd`); its broader gameplay
meaning is not assigned here [s/u]. Normal render preparation then runs
`0x48ba3a → 0x494b00` before camera publication
`0x48ba43 → 0x495670` and selection `0x48ba4c → 0x497720` [s].
This establishes update-before-draw ordering for this native path; it does
not make the whole frame/pass immutable.

SETA input differs independently of node layout. FL tick reads requested
warp **cfg `+0xbc`** at `0x4fff26`, multiplies governor **`+0xc0`** at
`0x4fff2c`, applies Q16 rounding, and advances game clock `+0x708` at
`0x4fff67`. AP uses cfg `+0xcc/+0xd0`, absolute time `+0x718`.
Reuse the qualified [FL clock contract](music-alert.md#3-clock-lifecycle-thread-and-reentrancy).
The shared plume `seta_decode`, travel ramp and ribbon game-time input
remain applicable after supplying the owned pair. Merely rebasing
`engine_effects::seta_read` fails: its expected instruction sequence and
warp offset are AP constants. Read once per stage/frame under the existing
config lifetime admission; preserve LastError and existing validation and
Reset/load resets. No time-warp setter, VM call or extra hook is required.
SETA accelerates the game's existing ramp per render frame; the target
throttle law stays unchanged [s/i].

**What current FL material/lifetime code actually grants [s].**
`material_submit_fl.cpp::x3m_fl_material_enter` obtains the calling thread's
`fl_render_admission::current(BorrowStamp)` and fresh
`object_lifetime::fl_owned_binding`, performs its call-free node/camera
copy, then validates owned serials and the unchanged stamp/binding before
publishing an owned TLS record. Recursive submissions get nested records;
unwind cleanup closes them. A binding/serial is owned metadata, not a
native-memory lease. `fl_render_admission.h` explicitly limits BorrowStamp
to one admitted invocation. Reuse these existing mechanisms.

**The precise AP consumer mismatch [s].** `MotionOutput::sample_scope`
currently calls `engine_node_gather` only for Edition::AP. That gathers
native root/camera/registry **tokens** into `RootSet`. Later
`run_engine_plumes` calls `engine_node_append` after `engine_far_append`;
if the stage did not run, `engine_light_frame` can instead call it at the
**next frame boundary**. The append dereferences root `+0xc`, next/child
blocks, camera `+0x1c` then context `+0x2c`, optional root radius `+0xa4`,
and uses legacy `object_lifetime::current`. None of those later transitive
reads is granted by the FL material copy, readable pages, or owned serials.
The gathered root can have crossed native callbacks, scene mutation or the
end of the original borrow. The existing admitted main-thread normal path
and deferred free/load closure are evidence to reuse, not a reason to
invent a second owner system.

**Missing guarantee [u]:** demonstrate that the actual FL append invocation
(including the no-stage fallback) holds fresh existing owner/load/pass
admission across the whole bounded root/list/radius copy, that each list
root is a live native borrow there, and that no native/COM/VM callback or
writer can run during that finite copy. Or produce owned nozzle Raw/radius
values earlier inside an already-established native borrow and consume
those at append. This note does not choose a new hook or assert either
placement is already covered. Parent-chain own-ship admission does not
implicitly authorize traversing every child. Callbacks may occur before
or after a copy; never retain admission as a page-cache permission or hold
an observer lock through them. On exception, load, Reset, identity mismatch,
failed/overflowing walk or lost admission, reject new native samples and
retain the existing local failure/hold semantics, with no stale pointer read.

## 5. Minimal adapter boundary and hot-path implications

The next bounded adapter is **FL engine-effects native inputs**:

1. Bind two FL guarded call windows and continuations, with FL A's object
   slot/argument ABI and the reversed plain/atomic install geometry.
2. Project already-admitted FL scalar/model/identity/camera data into shared
   effect records/Raw. Select model `+0x144` through the existing profile;
   supply owned context scale, parent radius and FL-owned serial metadata.
   Settle the exact list read window above before enabling node sourcing.
3. Supply the once-per-frame owned SETA pair from cfg `+0xbc/+0xc0`, leaving
   the shared travel/flow/throttle/render/light/ribbon policy unchanged.

Measured source operation bounds [s], not timings: `Raw` is 72 bytes;
shared walk caps at 256 children, reads next plus two flags per child and
one additional 336-byte prefix only for JET candidates. With C children
and J candidate jets, a complete nonempty walk uses `2C+J+2` reader calls
and `12C+336J+8` bytes including head/sentinel. At C=J=256 the bound is
**770 reader calls / 89,096 bytes**; early denial/overflow stops the walk.
The current adapter projection changes one offset load, not traversal
complexity. The once-per-frame SETA pair is eight bytes; class-test stubs
remain integer-only, stateless and allocation-free. Avoid per-draw config
validation, body-table scans, native registry lookup or repeated walks.
No new GPU pass, shader variant, lock or allocation follows from these
native differences [i]. FL list population and actual timing remain
unmeasured; AP measured microseconds/scene maxima are not FL performance
acceptance. User flight captures and native Windows behavior remain open.

## Cull-entry integration inventory (2026-10-29)

The [bounded entry producer](../../../verification/results/fl-port/engine-cull-entry/produce.py)
confirms an additional instruction-layout difference: a near redirect at FL
`0x4961d3` must retain/replay a **nine-byte** whole-instruction span, comprising the parent load into
EAX and own-threshold load into EDX. Continuation `0x4961dc` performs TEST EAX,
EAX. The existing writer installs a five-byte redirect; the nine bytes describe
the saved/replayed span, not a requirement for a nine-byte write. AP's five-byte
MOV/TEST replay cannot be reused verbatim. This is local
entry evidence, not complete liveness or hook qualification.

The source inventory also finds two consumers of the existing shared cull-site
chain: `lens_flare_cull.cpp` and `occlusion_engine_cull.cpp`. Porting the small
parts/far-jet entry must preserve their ability to share one owned claim; an
independent FL far-jet patch would leave these consumers unresolved. Their FL
register extraction and branch targets still require qualification. Reuse the
existing chain and policy when those bindings are established; this inventory
does not justify a new dispatch or lifetime framework.
