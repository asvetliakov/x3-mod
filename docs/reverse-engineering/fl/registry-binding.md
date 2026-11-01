# Farnham's Legacy fresh registry binding and factory incarnation

Offline targeted analysis, 2026-10-27. The construction store at `0x48261a`
is a viable **provisional from-empty binding seam**. Its instruction contract
and ordinary constructor reachability are established. It is **not sufficient
with only the four lifetime adapters**: factory constructor-unwind cleanup can
free the engine without calling registry destroy. A scope around factory call
`0x477692` is the concrete additional lifecycle boundary. It can keep every
consumer denied throughout initialization and revoke on abnormal exit without
requiring enumeration at factory return. Full writer/alias closure and that
scope's actual foreign-unwind behavior remain unqualified; no FL reader or
renderer capability follows from this note.

This owns the follow-up to [fresh registry construction](../../architecture/farnhams-legacy-lifetime-activation.md#follow-up-candidate-bind-at-fresh-registry-construction).
It reuses reviewed [object-lifetime](object-lifetimes.md),
[startup](startup.md) and
[startup-exclusion](startup-exclusion.md) facts. Installation
exclusion is a separate project workstream. No production edits, game/Wine
execution, shared build, installation or commit were performed here.

## Reproduction and limits

```sh
python3 verification/probe/verify_fl_registry_binding.py
python3 -m py_compile verification/probe/verify_fl_registry_binding.py
```

The [verifier](../../../verification/probe/verify_fl_registry_binding.py) uses the
project PE reader and Capstone on `/mnt/c/X3FL-orig/X3FL.exe`: 2,804,736 bytes,
SHA-256 `e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
Addresses are preferred VAs at image base `0x400000`. Measured: **17 regions,
2,803 instructions, 98 operand anchors, 34 independently checked region edges,
13 negative cases and thirteen synthetic lifecycle witnesses**, 0.885 s on the
recorded run. Both prospective spans are checked for direct interior edges and
HIGHLOW relocation/address references. The constructor/factory normal CFGs have
248/124 nodes and 263/124 edges. These graphs treat calls as returning and do not
invent exception or callback edges. The exception paths are separately traced
below. Raw listings, producing scripts and JSON remain ignored under
`verification/results/fl-port/registry-binding/`; `reproduce.sh` reruns checks.

Eleven mutated operand/control/metadata witnesses bypass image and region
hashes and boundary checks, each requiring its particular rejection reason.
They include the registry field, next allocation size, empty initialization,
null branch, constructor tail, cleanup state/local/action, return register and
an injected branch into the binding span. Whole-image mismatch and interior
region-start rejection add two negatives. Synthetic models demonstrate null
binding, repeated binding/return mismatch, pre-bind revocation followed by a
resumed binding and return, births during construction, unrelated maps, normal completion,
load/survivor behavior, retirement, address reuse and both construction-failure
phases; they also retain the four-adapter-only stale-binding counterexample.
They are specification witnesses, not execution of a hook, CRT or engine.
The model has no factory-entry method: its second-binding witness covers only
another binding while constructing, not nested factory entry before binding.
Real nested/reentrant factory-scope behavior remains an assembly-fixture obligation.

## Construction and early ownership

The complete constructor is **`[0x4825c0,0x482a2e)`**, not the prefix ending at
`0x482a16`. The branch at `0x482a0a` enters the final-map success tail at
`0x482a16`; ordinary returns are `0x482a15` and `0x482a2d`. Both are dominated
by the candidate binding store. The direct-edge/relocated-pointer census finds
one constructor caller, `0x48a5d8`, and no relocated pointer to its entry.
This does not exclude computed entry or arbitrary branches into its body.

Factory `0x48a5a0` allocates `0x6a48` through zeroing allocator `0x4d9120`, saves
the allocation in `[ebp-0x10]`, and passes it in ECX to the constructor. The
constructor saves ESI and copies ECX into ESI. Before map allocation it clears
an engine-local range and initializes the list at `+0..+8`; `engine+8` points
back into the engine. These stores do not publish the engine to another owner.
The first render map allocation is `0x482601 -> 0x4d8fc0`, ECX=16, EDX=0.
The reviewed allocator's zero-policy path calls CRT allocation, performs any
failure recovery/retry **before** its successful zero-fill, then returns the
fresh block in EAX (`0x4d90ad..b9`). It receives no engine or map-owner argument.

After that return, only a null test and the two map-field stores precede the
binding seam: `+8=1`, `+4=8`. The successful map's buckets `+0` and count `+0xc`
are still zero. EAX retains the map; no intervening call or store exports it.
The first engine-owned map publication is exactly `0x48261a`. The zeroing and
no-intervening-call facts establish ordinary fresh-allocation ownership here;
they are not a claim that arbitrary foreign exception continuation or allocator
corruption is safe. Null allocation skips both field stores but still reaches
`0x48261a` with EAX=0. It does **not** cause the constructor to return failure:
the constructor continues and ultimately returns the engine in EAX.

The complete constructor contains one direct store to its retained ESI owner's
`+0xc` and no later replacement of that field. Neighbor maps `+0x10/+0x14`,
`+0x80/+0x84/+0x90`, `+0x6308/+0x630c/+0x6310/+0x6324`, and `+0x696c` are
separate allocations. Its interior list aliases refer to other offsets. Calls
before the seam are memset and allocation; later calls are those same operations
and the conditional recovery callback at `0x48297e`. The callback registration
at `0x4778f3` follows factory execution, and its slot initially contains zero;
reuse the reviewed caveat about computed writes and CRT new-handler state.
The constructor also registers a VM callback at `0x482897` and its name at
`0x4828ce`. Registration is not proof of immediate invocation, nor does it pass
the provisional engine pointer. The engine global is not published until the
constructor has returned to the factory.

## Exact binding and outer-call contracts

| Seam | Whole instructions and continuation | Required state |
| --- | --- | --- |
| Bind `[0x48261a,0x482620)` | Six bytes `89 46 0c 8d 4a 10`: store EAX to `[ESI+0xc]`, then LEA ECX=`EDX+16`; continue at allocator call `0x482620`. | ESI=engine, EAX=map or zero, EDX=0 from XOR at `0x482618`; preserve ECX=16 for the next allocation. |
| Factory scope `[0x477692,0x477697)` | Five-byte direct CALL, target `0x48a5a0`; continuation writes EAX to `0x5aa100` and TESTs EAX at `0x47769c`. | Zero explicit factory arguments, ordinary RET; returned engine EAX is live. Preserve ESP, callee-saved registers and the factory's actual output state. |

Bind's MOV and LEA do not modify flags. The preceding XOR sets arithmetic flags;
there is no permission to use them as scratch. Both displaced instructions must
execute exactly once in original order before entering the next allocation.
An observer callback may run after replay and before that allocation, with all
GPRs, flags, original ESP and FP/SSE/MXCSR state restored, plus LastError.
The constructor does not use XMM instructions; that does not prove the caller's
XMM/x87 state dead. Use the qualified FNSAVE/FRSTOR route for x87 and explicit
XMM/MXCSR preservation, as required by the
[startup envelope decision](../../architecture/farnhams-legacy-install-boundary-decision.md)
and [CPU-state contract](../../../src/engine/common/cpu_state.h). FXRSTOR does not reliably
restore live x87 tags on Preview. CpuState's ordinary volatile-XMM ABI is not
enough for these transparent instruction seams; their envelope must save XMM
explicitly. This is the required transport, not a claim that existing lifetime
adapters already implement it. Retain the four-byte incoming-stack contract. The callback must use
preallocated state, perform no engine traversal/logging/foreign allocation, and
never let a C++ exception escape into the middle of the constructor. It needs
only the exact engine/map pair and the tiny empty header; failure quarantines
coverage and must not change the game's allocation result.

The binding instructions have neither a relative operand nor an absolute image
operand. Relocating them needs only a jump back to `0x482620`. The factory CALL
requires a recalculated rel32 target if moved. No HIGHLOW relocation overlaps
either span; no direct branch enters either displaced interior, and no HIGHLOW
pointer encodes either span or the four lifecycle entry addresses in this
bounded census. The respective spans lie within aligned eight-byte blocks,
both starting at offset two. This is **not** a concurrent installation protocol:
verify ownership and install/rollback both spans under the established startup
exclusion, with all callback and trampoline state ready before exposure. A
retained trampoline must outlive all entered scopes. Computed edges remain a
separate qualification requirement.

## Factory failure, cleanup and reentrancy

The factory's normal CFG has one return, `0x48a76d`. The constructor returns
before internal global publication `0x48a5f1`; initialization then calls
`0x487100`, `0x487420`, `0x485f60`, `0x483f20`, `0x4870d0`, VirtualAlloc and
`0x485d80` twice. The reader chain already established in startup-exclusion is
callback/failure-capable after that publication. A callback can therefore see
the global while the engine remains under construction. A bind hook must not
turn that visibility into consumer admission.

Engine-allocation failure is a distinct concrete witness: `0x48a5d4` takes
`0x48a5e1`, setting EDI=0; execution publishes zero and reaches the unconditional
store `[edi+0xb4]=0x2af8` at `0x48a5f7`. This is not an ordinary successful null
return. Conversely, a failed **map** allocation can leave a nonzero returned
engine with `engine+0xc=0`; a nonzero factory result alone cannot certify a bound
registry. Do not repair either path by searching the global or adopting later
births without a complete baseline.

The factory installs an FS:[0] record at `0x48a5b1`, handler `0x54e1d2`.
Its state at `[ebp-4]` becomes 0 at `0x48a5cb`, before the constructor call,
and resets to -1 at `0x48a5e3`, before internal publication. Handler metadata
at `0x59dc80` contains magic `0x19930522`, maxState=1, unwind-map pointer
`0x59dca4`, zero catch count, and flags=1. The sole unwind-map pair is
`(-1, 0x54e1c0)`. Handler bridge `0x521368` passes that metadata to
`0x5204fa`. On eligible unwind flags (`0x520547`, mask `0x66`), it calls
`0x520432`. The latter reads record state `+8`, follows eight-byte unwind-map
entries, updates state before invoking the action, and calls `0x523810` at
`0x5204a1`. That action wrapper reconstructs the owner's EBP as record+12
(`0x523818..2b`) before `call eax` at `0x523835`.

The cleanup action `0x54e1c0..d1` pushes size `0x6a48` and the engine allocation
from `[ebp-0x10]`, then calls `0x51e239`. That wrapper forwards its pointer to
`0x4d91e0`, the reviewed CRT free thunk. It calls neither engine destructor nor
registry destroy. Thus an eligible constructor exception **after** binding and
before constructor return can free the engine with no event at insert, remove,
destroy or renderer load. The map may be leaked; an observer must not read the
freed owner to discover that. This is a conditional static unwind witness, not
an observed thrown exception or game crash.

There is also an exception-class limit: dispatcher `0x520525..541` checks the
C++ code `0xe06d7363`, special code `0x80000026`, per-thread state and metadata
flags. With flags bit 0 set and the ordinary zero thread-state gate, other
exception classes return without this cleanup. An access violation is not
established to run the engine-free action. Nonlocal exits and continued
exceptions are not ordinary C++ unwind. The proposed outer scope must account
for them independently; do not equate game cleanup metadata with a safe injected
SEH wrapper.

After the constructor returns, state=-1 disables this action, although the
factory can still fail during table/file/arena initialization. The global can
then remain a partially initialized live allocation. The outer scope must
revoke on those failures as well; wrapping only `0x48a5d8` is insufficient.

## Minimal incarnation protocol for implementation

This is a concrete proposed contract for synthetic qualification, not a claim
that production has implemented it:

1. Enter one stack-owned scope before forwarding factory call `0x477692`.
   Record a new construction attempt and deny all connected consumers. Install
   real per-invocation SEH/unwind accounting. Do not hold the observer lock
   across the factory, allocator, parser or other foreign calls.
2. At `0x48261a`, accept a nonzero empty map only for that active expected
   construction scope, bind the exact engine/map pair, and establish provisional
   from-empty coverage. Null, a second bind or a conflicting/nested factory
   attempt quarantines the attempt. Neither globals nor address equality grant
   a new incarnation. Keep the three generic helpers and load adapter active:
   births during initialization are tracked, but cannot authorize readers.
3. On a normal factory return, match EAX to that exact provisional engine and
   require an unpoisoned scope. Mark construction complete only after all
   bookkeeping is consistent. No enumeration is needed **if** continuous writer
   coverage is independently proved. Construction-complete, coverage-complete,
   and renderer/scene/camera readiness are separate facts. Even this normal
   return does not by itself admit those consumers.
4. On any supported abnormal exit, revoke the incarnation, clear pending
   publications and advance its epoch without dereferencing engine or map.
   Inner game cleanup can free the engine **before** the outer injected unwind
   handler runs. Consumers must therefore remain denied from scope entry
   through all inner cleanup and outer invalidation. This is revoke-before-
   first-reader, not a promise that an outer handler precedes the inner free.
   Search/continuation and repeated handler invocation must not publish or
   double-finish a scope. A conservative exception quarantine can survive a
   resumed continuation; it may not manufacture successful coverage on RET.
5. Retire the bound map before forwarding its ordinary destroy. Retain terminal
   dead/unbound state after engine teardown, including when addresses are reused.
   A second factory invocation is unsupported until an explicit rebind protocol
   is qualified. Outer-scope records and pending tokens cannot be global scratch.

Required assembly fixture cases are exact register/stack/flags/FX/LastError
forwarding at both seams, empty/null map, births before publication and after
internal publication, normal-return mismatch, nested/reentrant construction,
C++ and foreign unwind in both phases, continued exceptions, cleanup callbacks
after inner owner free, and any nonlocal transfer capable of bypassing the
record. The existing synthetic Python model does not close those obligations.
The bound-map filter and deferred mutation-depth-zero publication rules from
the lifetime architecture still apply. A lock around observer bookkeeping is
not a lock around engine writers or a lifetime lease for external readers.

## Registry replacement, load, retirement and the remaining frontier

Established ordinary ownership paths are compact enough to retain explicitly:

| Owner/path | Effect and observation |
| --- | --- |
| Complete constructor `0x4825c0` | One direct engine `+0xc` publication at `0x48261a`; final tail initializes `+0x696c`, not the render registry. |
| Factory `0x48a5a0` | No later direct engine `+0xc` replacement in its body; global publications at `0x48a5f1` and caller `0x477697`. Transitive callbacks are separate. |
| Node/camera births and serialized restore | Reviewed automatic births reach insert; restore `0x4934a2/a5` supplies the render map to insert. Replacement return zero is not failure. |
| Renderer load `0x493510` | Reads render-map aliases for relinking (`0x493913`, `0x4940a0`); reviewed serialized creation reaches insert. It does not establish a new engine incarnation or an empty baseline. Preserve surviving identities across normal load; advance load/history epoch before forwarding, including failed load. |
| Collection `0x4a2890..0x4a2991` | Reads engine `+0xc` at `0x4a289b/28d0`, enumerates next key before deletion, and calls ordinary destructor `0x4a3300` at `0x4a296e`. The other branch changes node flags `+0x130`; it is not a map-header replacement. |
| Ordinary teardown `0x48a950` | Loads map at `0x48aba6`, destroys it at `0x48abad`, calls engine destructor `0x482a30` at `0x48ad6c`, then frees engine at `0x48ad77`. No clearing of engine global or `+0xc` accompanies this retirement. |
| Engine destructor `0x482a30..0x482b02` | Frees two lists, calls `0x484700` over 100 entries beginning `engine+0x632c` with stride16, and frees `+0x6a40` string. It does not independently destroy the render map; direct caller is ordinary teardown. |
| Factory state-0 cleanup | Engine free through `0x54e1c0 -> 0x51e239`; bypasses both destructor and registry-destroy observation. |

The direct lifecycle census finds one caller each of constructor, factory,
engine destructor and teardown (`0x48a5d8`, `0x477692`, `0x48ad6c`, `0x476411`),
and no relocated pointer to those entry addresses. Startup's previously proved
ordinary no-repeat factory path supports the proposed one-shot policy; this
is not an indirect-entry or process-wide one-incarnation proof.

**All engine `+0xc` aliases/replacements are not closed by this work.** The local
30-instruction field-after-global survey is only discovery evidence: examples
`0x49769c`, `0x427e09`, `0x485ab2` refer to other owners after the loaded register
changes. Do not count those as render-registry access simply because their field
is `+0xc`. Read-only lookup roots such as `0x4a2430/0x4a2470`, enumeration
`0x4a2890`, and node-field mutation `0x4ab490` likewise are not direct registry
writers. The verifier makes no exhaustive alias-closure claim from these slices.

The minimal remaining engine proof frontier is therefore:

- Follow engine/map pointer escape and retention through the named factory
  initialization callees and callback entries, and through non-immediate global
  aliases/spills. Establish that the identified publication remains the sole
  render-map slot writer, including computed calls, instead of auditing all
  443 global references without ownership discrimination.
- Close map-header/bucket writers outside the reviewed insert/remove/destroy
  paths. The direct rehash caller inside insert and unrelated-map counter setter
  remain reviewed facts; neither excludes an indirect rehash or direct header
  store through an escaped map pointer.
- Keep value/free closure separate from identity coverage: recovery
  `0x47b500 -> 0x485c00` traverses resource lists and calls `0x4993b0` at
  `0x485d66`, also used by ordinary teardown. Its resource-release transitive
  callees and delayed callbacks must be distinguished from render-node/camera
  frees. The reviewed node/camera destructors retire before value free; a map
  observation interval alone does not pin an externally copied value.
- Qualify the proposed outer factory scope's real exception/nonlocal paths and
  lifecycle admission. Do not solve the cleanup gap by declaring all factory
  callbacks harmless, automatically rebinding stale globals, or reading the
  engine in an outer unwind handler after the inner free.

The result narrows implementation to a binding seam plus one enclosing factory
scope and gives a concrete counterexample against using the old four events
alone. From-empty observation remains the preferred conditional route; it
removes the need for a callback-free final enumeration once writer closure is
proved. It does not remove the separate readiness, writer/free and consumer
lifetime requirements.
