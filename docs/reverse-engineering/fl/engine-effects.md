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

## Verified 2026-11-02 against X3FL.exe

Input: the live `C:\X3FL\X3FL.exe` (SHA-256 `e7f3aad8…01dfbb8`, the same image the
2026-10-29 study names as `/mnt/c/X3FL-orig`, which no longer exists) and
`/mnt/c/X3AP-orig/X3AP.exe` for the AP twins. Static only; no Wine, no game.

```sh
python3 -I verification/results/fl-engine-effects/produce.py          # writes contract.json
python3 -I verification/results/fl-engine-effects/produce.py --check  # 0 when it is current
```

[Producer](../../../verification/results/fl-engine-effects/produce.py) decodes
every anchor below with Capstone over `tools/analysis/inspect_x3.py`'s PE reader and
asserts each whole instruction (114 anchors, 7 hashed windows); it parses `.reloc`, runs a
linear direct-edge census of `.text` and reads both editions' `types/Bodies`
through the project's catalogue readers. [contract.json](../../../verification/results/fl-engine-effects/contract.json)
holds offsets, RVAs, counts, conclusions and window hashes only. All figures in
this section are **measured** from the bytes unless marked [i] (inferred).

**Image is relocatable.** DllCharacteristics `0x8140` (DYNAMIC_BASE), 38,327
HIGHLOW fixups. Every address below is an RVA (VA − `0x400000`); a hook adds the
running module base. Absolute operands inside a window are relocated; rel32
CALL/JMP displacements are not. **Window identity** = the bytes of the window
with each relocated dword zeroed (the `object_trace_fl_inc.h` convention),
hashed FNV-1a 32 and SHA-256 (full digests in contract.json).

| Item | AP VA | FL RVA (VA at 0x400000) | FL window RVA, length | Relocated dwords | FNV-1a 32 | SHA-256 (prefix) |
| --- | --- | --- | --- | --- | --- | --- |
| Emitter routine | `0x414590`, obj `[ebp+8]` | `0x014d90` (`0x414d90`), obj ECX → EDI | entry..`0x0150d1` | – | – | – |
| Sole direct caller of the routine | `0x45ac7d` | `0x0645bd` | – | – | – | – |
| Class dispatch (obj `+0x48`, int16) | `0x41460c` | `0x014e07` | `0x014e07`, 21 | none | `cfed2a46` | `5e0572d1409027c3` |
| Window A (TEST C&0x4000 .. ADD ESP) | `0x4147c4`, 47 | `0x015054` | `0x015054`, 41 | none | `5a69143c` | `6d6333262b2134d5` |
| Call A → target | `0x4147eb` → `0x4148a0` | `0x015075` → `0x0150e0`; return `0x01507a`, join `0x01507d` | – | – | – | – |
| Window B (TEST C&0x2000 .. CALL) | `0x4147ff`, 50 | `0x015085` | `0x015085`, 42 | 1 at `0x015092` (cfg slot `0x1a83b0`) | `601f528c` | `e549a433e8fe4156` |
| Call B → target | `0x41482c` → `0x412d70` | `0x0150aa` → `0x013640`; return `0x0150af`, join `0x0150b5` | – | – | – | – |
| JET flag writer `+0x130 \|= 0x4000001` | `0x434708` | `0x03deb3` (classifier `0x03ddd0`) | `0x03de93`, 42 | 1 at `0x03de95` (JET list slot `0x1a8314`) | `aa1cc593` | `ade2f0de7ec6fc3c` |
| Scale setter (writes +0x80/84/88) | not re-derived | `0x0a3ae0`, ECX=node, 3 stack args, `ret 0xc` | – | – | – | – |
| Jet drive (ramp + setter call) | `0x45ad8b..0x45b03c` | `0x0646a6..0x0649c3`, setter call `0x0649be` | – | – | – | – |
| SETA read (warp, mult) | `0x4d1ef0`, cfg `+0xcc/+0xd0` | `0x0fff26`, cfg `+0xbc/+0xc0` | `0x0fff26`, 12 | none | `625a39c5` | `84398aee7b5bfe04` |
| Game clock | cfg `+0x718` | cfg `+0x708` (add at `0x0fff67`) | – | – | – | – |
| Config slot | `0x606f34` | `0x1a83b0` | – | – | – | – |
| Body table id → slot | `0x46df60`, global `0x608518` | `0x0867c0`, global `0x1aa100` | `0x0867c1`, 85 | 1 at `0x0867c3` (engine slot) | `5bad5d11` | `6d73329a63b2e599` |
| Size-cull verdict | `0x47d2a2..0x47d2ec` | `0x0961d3..0x09621c` | `0x0961d3`, 73 | none | `8f680dbc` | `59a638249270ff5c` |

### 1. Suppression call sites

- **Bytes and geometry.** A's CALL `0x415075` starts at qword offset 5, so both
  the 5-byte call and its rel32 (`0x415076..0x415079`) cross the aligned qword
  boundary `0x415078`: **plain write, not atomic**. B's CALL `0x4150aa` starts at
  offset 2 and lies wholly in qword `0x4150a8..0x4150af`: **one `lock cmpxchg8b`**.
  This is the reverse of AP (A atomic, B plain).
- **A's convention** is a register/stack hybrid (MSVC whole-program custom
  convention [i]): **ECX = 0** (effect-list key component; `xor ecx,ecx` at
  `0x415073`), **EDX = k** (jet ordinal, `mov edx,ebx` at `0x415068`), then eight
  stack dwords. At callee entry: `[esp+4]` effect id, **`[esp+8]` object**,
  `[esp+0xc..+0x14]` 0, `[esp+0x18]` &pos, `[esp+0x1c]`,`[esp+0x20]` 0. Caller
  cleanup `add esp,0x20` at `0x41507a`; the callee's only return is plain `ret`
  at `0x41526a`. Against AP's 10-dword cdecl (`0, k, eff, obj, 0,0,0, &pos, 0,0`):
  AP stack args 1 and 2 moved into ECX and EDX, the other eight keep their order,
  so obj moves from `[esp+0x10]` to `[esp+8]`. The callee reads both registers
  (`mov eax,ecx` `0x415101`, `mov ebx,edx` `0x415107`) and keys its instance list
  on (`+0x10` obj, `+0x20` ECX, `+0x24` EDX). It installs an SEH frame
  (`0x4150e3..0x4150f1`); its first flags writer is CMP `0x41510c`, nothing
  before it reads flags.
- **B's convention** is unchanged from AP: `[esp+4]` k, `[esp+8]` trail id,
  **`[esp+0xc]` object**, `[esp+0x10]` &pos; callee pops with `ret 0x10` at both
  returns `0x41371e`, `0x413816`. ECX and EDX are not inputs (ECX is written at
  `0x413656` before any read). First instruction SUB writes flags.
- **&pos** is the caller's 16-byte-aligned local `[esp+0x40]` (frame realigned by
  `and esp,-16` at `0x414d93`).
- **Liveness.** Live across either call: EBX = k, ESI = child node, EDI = object,
  EBP, and the ESP locals (`+0x10` effect id, `+0x14` trail id, `+0x18` k copy,
  `+0x1c` object copy, `+0x40` pos). Dead after either call: EAX, ECX (reloaded
  at `0x41507d`), EDX (next written by IMUL before any read), status flags, XMM0.
- **Class test.** `movzx esi, word [edi+0x48]` at `0x414e07`; 0 → class-0 path,
  **7 → ship table `*0x5a83e4`**, 10 → `*0x5a83f0`, else return 0. Tables are
  `0x5a83c8 + class*4` (AP `0x606fb8 + class*4`), so offset `+0x48` int16 and
  value 7 carry over; class 7 = TShips [i, identical dispatch and the ship-only
  tuning read `cmp si,7` at `0x4645f1`]. Only 7 and 10 reach the windows
  (`test si,si` at `0x414e84`). The stub skips 7 and forwards 10 (missiles [i]).
- **FL stubs.** A: `push eax; mov eax,[esp+0xc]; cmp word [eax+0x48],7; pop eax;
  je skip; jmp [continue_a]; skip: xor eax,eax; ret`. B: exactly AP's stub
  (`[esp+0x10]` after the push, `ret 0x10`). The forward path must leave **ECX
  and EDX** untouched for A.
- **Census.** 72,230 direct edges in `.text`; 0 land strictly inside either
  window. One image dword points into window A: the `.CRT$XPA` string. The A
  target has **10** direct callers and the B target **3**, so the redirect
  must be a call-site claim, never a target-entry hook.

### 2. JET-list flag

The FL writer is `or dword [ecx+0x130], 0x4000001` at `0x43deb3`, in classifier
`0x43ddd0` (node in ECX from `[esp+4]`). It matches node model **`+0x144`** against
the count-prefixed id list `*0x5a8314`. That list is slot SBTYPE_JET = 6 of the
list array `0x5a82fc` (SBTYPE name table at `0x576c68`, SBTYPE_SMALLJET = 12 →
`0x5a832c`, writer `0x43dede` `|= 0x4000000`, `+0x1d8 = 5`). Offset and value are
unchanged. OR-immediate census of `+0x130` (FL 84 writers, AP 53):

- Bit 0 is set only at `0x415939` and `0x43deb3`. `0x415939` sits in the
  effect-instance builder `0x415490`, which marks JET-list bodies inside the
  instance that A's callee constructs (`0x41514a → 0x415340 → 0x4153ed`). It
  sets bit 0 alone (AP twin `0x41514e`).
- Bit `0x4000000` is set only at `0x43deb3`, `0x43dede`, `0x44ae2d` and
  `0x44b04b` (AP `0x434708`, `0x434730`, `0x441922`, `0x441b42`).

The recogniser rule `flags130 & 0x4000001 == 0x4000001` therefore keeps its AP
meaning. The census counts only OR-immediate writers, not MOV or register-OR
writers. `object_trace_fl.cpp::current()` already copies `flags130` (`+0x130`),
`flags12c`, `model` (`+0x144`), `scale[0]` (`+0x70`) and `scale[1..3]`
(`+0x80/+0x84/+0x88`) from its single 0x154-byte node read. **Nothing needs adding.**

### 3. Throttle, size and SETA

- **Node fields are the same as AP:** `+0x70` base scale, `+0x80/+0x84/+0x88` axis
  scales, written only by setter `0x4a3ae0`. The setter writes `+0xa0` =
  `+0x70` × max axis and `+0xa4` = −1, and recurses into children with
  `+0x12c & 0x40`. Throttle = `+0x88 / 65536`. Model = `+0x144` (AP `+0x140`).
- **Drive** (sector update `0x463000`, jet loop from root `+0xc` at `0x464630`):
  JET bit test `0x4646a6`; target endpoints `0x4000..0x20000` (pushes at
  `0x4646c7/0x4646cc`) and hide threshold `0x28f`; current z read at
  `0x464932`; step `0x106 × dt` per update (`0x46493c/0x464950`, Q16, 0.004/ms);
  setter call `0x4649be`. The AP drive has the same constants (`0x106` at
  `0x45b01a`).
- **dt** `[esp+0x44]` = cfg `+0x708` (read `0x46301c`) − object `+0xa8`
  (`0x46319d`). It is **capped at 1000 game ms, and a negative delta also becomes
  1000** (`0x4631a7..0x4631c3`).
- **SETA.** Tick `0x4ffe30` loads cfg `*0x5a83b0` into ECX at `0x4ffed5`, multiplies
  warp **`+0xbc`** by governor **`+0xc0`** (Q16, `0x4fff26/0x4fff2c`), scales the
  real-ms step clamped to 1..200 (`0x4fff41..0x4fff53`), and adds the result to
  `+0x708` at `0x4fff67`. AP reads `+0xcc/+0xd0` and adds to `+0x718`.
- **SETA interaction** [i, from the constants above]: game-ms dt grows with
  SETA, so the native ramp covers more of the 0.25..2 range per rendered frame.
  At ×10 and 25 ms frames dt = 250, a step of about 1.0, so a throttle change
  completes in 1–2 frames. The 1000 ms cap allows up to about 4.0 per update.
  The plume policy must keep consuming the actual `+0x88`. Shared `seta_decode`
  needs the FL pair read from `module_base + 0x1a83b0` → `+0xbc` (8 bytes).
  AP's `expected_seta_site` and `seta_warp_offset` do not apply; FL's identity
  is the 12-byte window above.

### 4. Engine body table and `types/Bodies`

- **Layout is identical to AP, only the global moves:** manager = engine
  `*0x5aa100` (RVA `0x1aa100`, the same slot `object_trace_fl` binds as
  `engine_slot_rva`); fixed count `+0xb4` (11000, written at `0x48a5f7`), dynamic
  `+0xb8`, slots `+0xbc`, **stride `0x1c`** (`lea ecx,[esi*8]; sub ecx,esi` → ×7
  dwords), name pointer `+0xc`; same id map (<1000 id; <20000 id−9000; else
  fixed+id−20000) in `0x4867c0`.
- **Bodies source.** The FL loader `0x43e710` pushes `"addon2\types\Bodies"`
  (string RVA `0x188544`) at `0x43f6fb`. The winning member is
  `addon2/types/Bodies.pck` in `addon2/04.cat` (decoded SHA-256
  `c7226417e14ec1b1…`), with **249 JET + 1 SMALLJET (`566`)**. AP
  `addon/01.cat` has 247 + 1. FL = all 247 AP tokens plus two FL-only names:
  `p39\effects\engines\playeryacht_engine` and
  `p39\effects\engines\terran_ts_xl_engine`. The 22 numeric tokens are
  identical, the spelling convention is the same (backslashes, no `/`), and
  there are no case-fold collisions.
- **Body files.** Of the 227 named FL jet bodies, 120 resolve in root `01.cat`,
  104 in `addon2/01.cat`, 1 in `08.cat`, and 2 in none
  (`fx_engine_teladi_M4`, `fx_engine_supplydrone`, missing on AP too).
- **`tools/effects/engine_bodies.py` needs an FL variant.** Its default view on
  `C:\X3FL` mounts root + physical `addon/` and picks **AP's**
  `addon/01.cat` Bodies (`5793a0ae…`). That loses the two FL bodies and
  measures AP geometry for the 104 bodies that `addon2/01.cat` supplies. Mount
  per `catalogue_mounts.mounted_catalogues(..., 'fl')` and read
  `addon2/types/Bodies`. The key rule (token as written, N → `v\%05d`) carries
  over. The root `08.cat` `types/Bodies` (146 JET) is shadowed by namespace,
  not by mount order.

### 5. Engine-side cull site (brief look)

FL has the equivalent of AP's size-cull verdict at `0x4961d3..0x49621c` (AP
`0x47d2a2..0x47d2ec`): node ESI, measure ECX (AP EDI, ESI). The cull instruction
`and dword [esi+0x12c], 0xfffffffd` is at `0x4961f3` (AP `0x47d2c3`) and the model
read `+0x144` at `0x49621c`. The engine `+0x64` lens-scene read appears at
`0x48bb24` (AP `0x472466`). The replacement-stub liveness, the continuations and
the lens walk are **not qualified**. This is needed only for gain 0.

### 6. Rules for the adapter

1. Stubs stay integer-only and stateless, with no API call or LastError change.
   They read only `obj+0x48`: obj is the routine's live ECX argument, already
   dereferenced at `0x414e07` on the same path (AP justification holds [i]).
   Tail-jumping adds no frame, so A's SEH and allocation behaviour stay native,
   and the stub is reentrant.
2. Install against the running module base. Compare window A (41 bytes, no
   fixup) and window B (42 bytes, the dword at +0xd zeroed or rebased to
   `base + 0x1a83b0`) before any write. Claim B with `cmpxchg8b`. Write A plain,
   only under install quiescence: the routine runs only from sector update
   `0x4645bd` behind the gate `*0x5a82ec != 0` (`0x4645ad`). Roll back both or
   none. Continuations are `base + 0x0150e0` and `base + 0x013640`.
   `engine_patch::install_at`'s AP windows and its AP assumption "B is the plain
   site" do not apply.
3. The draw-path recogniser needs no new native read: use the object_trace
   snapshot (`flags130`, model `+0x144`, scale). The RCS exclusion is model 566,
   which is FL's SMALLJET token.
4. The node walk (`engine_node_append`) stays unpermitted on FL. Section 4's
   missing read-window guarantee is unchanged by this study.
5. The body resolver reads through `base + 0x1aa100` each time. It never caches
   the slot array or a name pointer across frames. It clears its mappings on a
   load, because restore `0x487740`, reset `0x486450` and registration
   `0x486c60` reallocate `+0xbc` and rewrite names
   ([body-name lifetime](body-name-lifetime.md)).
6. Read SETA once per frame from cfg `+0xbc` (8 bytes), after the 12-byte FL
   identity compare.

### Status of the 2026-10-29 claims

| Claim | Status |
| --- | --- |
| §1 table: routine extent, sole caller `0x4645bd`, A/B windows, calls, targets, class `+0x48`=7, A obj `[esp+8]`, ECX=0/EDX=k + 8 dwords + `add esp,0x20`, B 4 dwords `ret 0x10` with returns `0x41371e/0x413816`, A no / B yes qword fit | **confirmed** |
| Ship table `*0x5a83e4`, stride `0xdb8`, effect `+0x54`, trail `+0xc4`; children from root `+0xc`, next `+0`; `C & 0xffe`; A/B gates; cfg `+0xec` bit 30; class-0 second gate (`+0xf0 & 2`) | **confirmed** |
| A's first flag writer CMP `0x41510c`, B SUB; SEH `0x4150e3..0x4150f1`, plain RET `0x41526a` | **confirmed** |
| "After B, INC EBX replaces status flags" | **corrected**: INC leaves CF unchanged. CF is still dead because CMP `0x4150bc` writes all flags before any reader. |
| Installation boundary (FL needs its own windows, A plain under quiescence, B atomic) | **confirmed**; **added**: B's window carries one relocated dword, and each target has other direct callers (10 and 3) |
| No branch into the windows; one image dword (`.CRT$XPA`) | **confirmed** (72,230 edges) |
| §2 classifier `0x43ddd0`, JET/SMALLJET writes, lists `*0x5a8314/*0x5a832c`, model `+0x144` | **confirmed**; list array `0x5a82fc` indexed by SBTYPE **added** |
| Drive list `0x464630`, drive `0x4646a6..0x4646da`, C gates `0x46484b..0x4648d8`, steering, ramp `0x464932..0x4649c3`, setter `0x4a3ae0` fields | **confirmed** |
| Next `0x464bc4`, scene-part C publication `0x4aa787..0x4aa791`, radius cache `0x4a39b0..0x4a3ad3` | unverified (not re-read) |
| Reparent `0x4a5809` (`+0x18`) | confirmed by `tools/analysis/fl_object_trace_constants.py`, not by this producer |
| "Resolve RCS against FL's existing body input tooling" | **corrected**: the existing default view reads AP's Bodies on the FL root, so an FL variant is needed (§4 above) |
| §3 size verdict `0x4961d3..0x49621c`, ESI node / ECX measure; 9-byte replay span with continuation `0x4961dc` | **confirmed**; selector `0x495e50` callers and EBP+8 camera unverified |
| §4 `+0x708` at `0x46301c`, `+0xa8` at `0x46319d`, cap 1000 (negative → 1000), gate `0x4645ad..0x4645bd`, SETA `+0xbc/+0xc0`, clock `+0x708` at `0x4fff67`; AP `+0xcc/+0xd0/+0x718` | **confirmed** |
| Sector caller `0x443b76`, render-preparation order `0x48ba3a/43/4c` | unverified |
| Material/lifetime grant and the missing read window (§4 tail), §5 walk bounds | not re-checked here (source-level claims) |
