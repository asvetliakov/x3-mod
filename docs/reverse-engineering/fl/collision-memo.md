# Farnham’s Legacy no-contact collision memo contract

Static study, 2026-10-29. FL has the same opportunity as AP for memoing repeated
no-contact mesh queries: its native query still computes root transforms,
clears counters and descends on each invocation. It has no native temporal memo
in this closed query path. The reusable part is AP’s word-key/table policy;
the engine boundary needs a small FL adapter. **Changing AP addresses alone is
incorrect:** FL inlines the AP mode-setting wrapper into its caller, uses SSE
arguments, and scatters its replay state across several globals.

This static contract is derived binary/source evidence. The subsequent
[adapter source checkpoint](../../verification/collide-memo.md) implements this
boundary; native qualification remains separate. The static study itself did
not compile, execute, install or launch anything. The decision to retain
[FL’s native SAT](collision-sat.md) remains in force.

## Evidence and reproduction

```sh
python3 verification/results/fl-port/collision-memo-contract/produce.py
python3 -m py_compile verification/results/fl-port/collision-memo-contract/produce.py
```

The [producer](../../../verification/results/fl-port/collision-memo-contract/produce.py)
uses the project’s PE32/Capstone utilities and writes only derived
[contract.json](../../../verification/results/fl-port/collision-memo-contract/contract.json).
Measured: 14 pinned body ranges, nine bodies in the query/caller closure,
2,599 decoded instruction rows in that closure, 106 HIGHLOW relocations across
the 14 ranges, nine rejected negative controls, and a positive rebase test of
all 14 bodies at preferred base +`0x130000`. Counts include mutually exclusive
branches and any alignment within a named range; these are static counts, not
costs or timings. Raw local listings remain in
`~/.cache/x3m-port/collision-memo-research/`, outside the repository.

| Input | Bytes, measured | SHA-256 |
| --- | ---: | --- |
| `/mnt/c/X3AP-orig/X3AP.exe` | 2,153,984 | `fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab` |
| `/mnt/c/X3FL-orig/X3FL.exe` | 2,804,736 | `e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8` |

Addresses below are preferred VAs at `0x400000`. FL admission must resolve RVAs
against the recognized loaded image and undo HIGHLOW relocation for body pins.
The producer preserves each relocated operand’s identity when normalizing;
zeroing an arbitrary absolute operand would weaken the guard. Existing FL
profile recognition is the cold edition/build gate. AP’s fixed-base admission,
body hashes, windows and SAT/descent claim holes cannot admit FL.

## 1. Mapping and native behavior

The mapping follows model state/root checks, thirteen transform words per
instance, the same four mode inputs, root-relative matrix construction and the
SAT/leaf recursion, rather than an address delta.

| Role | AP | FL |
| --- | --- | --- |
| Transform caller | `0x47f1b0` | `0x4982d0..0x498554` |
| Mode-setting wrapper | `0x4e29f0..0x4e2a50` | Inlined into `0x4982d0`, principally `0x4984a6..0x498504` |
| Memo call / return | `0x47f329` / `0x47f32e` | `0x49853a` / `0x49853f` |
| Root-transform query | `0x4e2780..0x4e2969` | `0x51aae0..0x51b060` |
| Descent | `0x4e2530` | `0x51a6c0..0x51aad8` |
| Triangle leaf | `0x4e2190` | `0x51a260..0x51a6bd` |
| Triangle intersection, including its internal projection work | `0x4e2a50..0x4e3280` | `0x51b2a0..0x51c6d0`, helper `0x51b160..0x51b295` |
| SAT, retained native | `0x4e3280` | `0x51c6d0..0x51cd4b` |
| Matrix products | AP x87 helper group | FL `0x51a0e0..0x51a251`, `0x516b70..0x516cf2` |

Measured bytewise scans of `.text` rel32 and all backed sections’ abs32 find
exactly one encoded call to FL query `0x51aae0`, at `0x49853a`, no absolute
entry pointer, and no encoded target in the four-byte call operand. The decoded
caller has no short branch into that operand. Its two encoded callers are
`0x4a5ff7` and `0x4a6395`: distance mode (`flags=0xc`, cap 8, minimum points to
a local float) and boolean mode (`flags=2`, cap 1, null minimum), respectively.
Computed indirect targets remain outside what those scans can exclude.

The caller tests both body `+0x5c` model pointers before constructing its
transforms. It reads each instance’s signed integer scale `+0x70`, translation
`+0xb4/+0xb0/+0xb8`, and matrix words in physical order
`+0xd4/+0xc4/+0xe4/+0xd0/+0xc0/+0xe0/+0xd8/+0xc8/+0xe8`. SSE converts
these to float32; matrices are multiplied by the constant at `0x591994`.
The producer symbolically follows all these conversions, products, pointer
construction and pushed arguments, retaining their order. Key the resulting
26 float words, as AP does; do not reconstruct them with AP x87 arithmetic.

FL caller `0x4984bf/4c4/4f3/4ff` writes cap, flags, integer tolerance and minimum
pointer **before** the query call. Tolerance uses native `CVTTSS2SI`, not AP’s
`ftol` helper. `0x51aae0` checks both models’ `+0x14 == 3`; an unbuilt input
returns EAX=3 before its normal root/counter initialization. Built inputs
unconditionally compute their relative root transforms, reset visits,
triangle tests and contacts, then call descent at `0x51b04d`.
No cross-query search, key compare, cached verdict or return branch bypassing
that work occurs in its reviewed closure. Native SSE removes AP’s x87 costs,
but does not remove the repeated traversal that memo targets.

## 2. ABI, boundaries, liveness and exception behavior

At the proposed FL memo thunk entry, `0x49853a` has pushed its return address:

| Item | Established FL contract |
| --- | --- |
| ECX / EDX | R1: nine float words / T1: three float words |
| XMM2 low word | s1 float bits; native query immediately spills it at `0x51aaf2` |
| `[entry ESP+4..+0x18]` | Six words: model A, R2, T2, s2 float bits, model B, `flags & ~0xc` |
| Additional literal inputs | Flags `0x5a9044`, cap `0x5aa0e0`, integer tolerance `0x5aa09c`, minimum pointer `0x5aa084` |
| Cleanup | Plain RET; caller adds `0x18` at `0x49853f`; four-byte incoming alignment suffices |
| Preserved GPRs | EBX, EBP, ESI, EDI, ESP |
| Scratch | EAX, ECX, EDX, arithmetic flags and XMM0..XMM7 |
| Result consumed by caller | Only whether contact count `0x5aa0a0` is nonzero |

The call is exactly one five-byte instruction. The predecessor at `0x498539`
pushes model A; the successor drops arguments, zeroes EAX, compares contacts
and sets AL. Consequently incoming EAX and outgoing EAX/ECX/EDX/XMM/flags are
dead to this caller; EBX/ESI/EDI restoration remains required. The query saves
EBP and the other nonvolatile GPRs; leaf/SAT use private eight-byte stack
alignment. The thunk must preserve ECX, EDX and at least XMM2’s low input word
across a lookup helper before a miss or bypass, and preserve the native stack
exactly. AP’s thunk captures ECX/EAX plus ten stack words and cannot be reused
unchanged. A hit may return EAX=0, but zeroing contacts is the observable verdict.

All nine closure bodies use SSE and no x87/MMX, MXCSR load/store, FS-based SEH,
indirect call, Windows API, allocation, lock or callback. Matrix helpers write
nine words only to their ECX output; query uses a local output twice and
`0x61b994` once. Descent helper outputs are local. Triangle intersection writes
three normal words through its output pointer; leaf supplies local stack
storage. Leaf’s only other non-stack indirect write is `*minimum` on the
counted-contact path. No query input, header, box or persistent triangle is
mutated. Inputs are borrowed synchronously; native query retains only its
global minimum pointer and derived scratch, not model/transform pointers.

The engine’s mode/counters/root state are shared globals, so the whole query
is not reentrant even though its arithmetic helpers are. Carry AP’s first-thread
owner and busy/reentry bypass before touching pending state or the table.
A forwarded foreign/reentered invocation is engine behavior; that gate does
not make native global collision state concurrently safe. On normal miss the
single pending entry can be stored only after the engine finishes. An exception
unwinding past the thunk must not publish it: AP leaves busy set, bypasses
further queries and clears at an owner-thread Present; keep that behavior unless
its actual unwind transport is changed and independently qualified.

LastError is untouched by this native closure. Lookup/store/telemetry must
continue preserving it. Lookup must not perturb x87 or MXCSR/control state.
FL arithmetic can set MXCSR exception status or trap with unmasked exceptions;
skipping it does not reproduce those incidental status updates/traps. Stable
FP controls are already an AP memo assumption, expressed there for x87; FL’s
corresponding assumption concerns MXCSR rounding, DAZ/FTZ and masks. Actual
loaded state/stability and any requirement to replay status are unmeasured.
AP’s output-only verify mode does not prove FP-status equivalence. Retaining
native SAT on misses preserves its FL float32 arithmetic and unordered compares.

## 3. Complete writes and replay normalization

The producer enumerates every direct absolute read/write in the closure and
every non-stack indirect store. Closure calls resolve only to the rows above;
helpers have no hidden absolute mutable state. Constants and the global sets
below account for all named operands, with relocation-aware body pins binding
the enumeration. This is stronger than matching the five-byte call alone.

| State | FL addresses and behavior |
| --- | --- |
| Mode already written before the boundary | Flags `0x5a9044`, cap `0x5aa0e0`, tolerance integer `0x5aa09c`, minimum pointer `0x5aa084`; leave these current-call writes intact on a hit |
| Counters replayed | Visits `0x5a9048`, triangle tests `0x5aa0d0`, contacts `0x5aa0a0 = 0` |
| First-contact flag replayed | `0x61b980`, set to `(masked mode == 2)` at `0x51ab70` |
| Root translation replayed | Three words `0x61b988..0x61b990` |
| Root rotation replayed | Nine words `0x61b994..0x61b9b4`; helper call `0x51af57` writes them |
| Root scale replayed | `0x61b9b8` |
| Contact-only writes, left alone on a hit | Six float words `0x5aa0a8..0x5aa0bc`; `*minimum` at `0x51a5eb` |
| One-time initialization | If `0x61b9bc == 0`, query writes visits/contacts=0, `0x61b984=0`, `0x61b9bc=1` before checking model state |

**Bypass while the init flag is zero.** That is a concrete extra FL eligibility
condition: it keeps all one-time writes native without enlarging the key or
turning the unrelated `0x61b984` word into replay state. Constructors and builder
entry points share this init convention; the producer pins representative
ones. An eligible stored entry is therefore always after initialization.

Normalize the 14 root words to AP’s existing `Outputs.root_block` ordering:
T[3], first-contact flag, scale, R[9]. FL scatter/gather uses the four spans
listed above; AP’s single contiguous memcpy would overwrite `0x61b984` and miss
the scale. Existing output capacity remains sufficient: visits, triangles,
tolerance integer and 14 normalized root words. Since tolerance is written
before this FL boundary it can be compared/stored as the existing output word
and left untouched when replaying. Current flags/cap/minimum need no replay.

Every contact requires triangle intersection to return nonzero before leaf
passes `0x51a507`; all contact record writes and the minimum store are after
that gate and lead to incrementing contacts at `0x51a667`. A no-contact query
leaves the record and pointee untouched. The only minimum pointer load in the
closure is `0x51a5d9`, after the leaf’s unconditional triangle counter increment
`0x51a43d`. There is no branch, call or return between leaf entry and that
increment. Thus AP’s minimum-value relaxation is valid for FL: retain pointer
nullness in the key, omit minimum value from the bucket hash, and allow that
word alone to differ when the stored triangle count is zero. Pointer identity
itself is immaterial to a no-contact answer; the current pointer global was
already set by the caller.

The relocation-tagged outside-reference inventory finds no external direct
references to modes, tolerance, minimum, triangle count or normalized root
scratch. Visits/contacts have six outside initializer references each; the
caller’s result read is inside the closure. `0x61b984` also has a separate
outside reference at operand `0x476511`, reinforcing that it is not root state
to overwrite. This inventory is evidence about named references, not proof
against arbitrary computed aliases; replay still preserves all enumerated
scratch rather than relying on its apparent privacy.

## 4. Key, model mutability and the inherited lifetime assumption

FL model layout supports the same 81-word key shape. Header is six words:
root/BV allocation, allocated BV count, persistent triangle allocation, active
triangle count, triangle capacity, state. `+0x14 == 3` means built, `+0` is root.
Each box is `0x48` bytes, with matrix/center at `+0..+0x2c`, half extents
`+0x30..+0x38`, children `+0x3c/+0x40` and persistent triangle pointer `+0x44`.
Triangle insertion allocates records at stride `0x28`, copies that stride and
writes identifier plus nine float coordinates; this is distinct from temporary
builder records at stride `0x34`.

Key the 26 post-conversion transform floats; FL’s already converted tolerance
integer; full flags and cap; minimum nullness/value; both model pointers; both
24-byte headers; and both 72-byte root boxes. The table treats word 26 as an
opaque equality word, so using the literal integer tolerance at this boundary
does not change its shared hash/equality/eviction policy. Using original
tolerance bits would be stricter but requires reaching outside the callee’s
input block; there is no benefit demonstrated for that extra coupling.

Known mutation paths provide bounded support for built immutability:

- `0x517cc0`, insertion, rejects state 3 at `0x517cf4..0x517d00` with result 4
  before modifying geometry; otherwise it may replace the triangle allocation.
- `0x517c10`, begin/reset, frees the old root and triangle buffers, clears the
  five ownership/count fields and writes state 1 at `0x517c89`.
- `0x51b060` invokes builder `0x517e40`, then sets state 3 at `0x51b0a3` on
  success. The builder allocates `2N * 0x48` BV storage and publishes a new root.
- Resource cleanup `0x4993b0` reaches `0x49981a -> 0x51b0f0` through resource
  `+0x5c`, then clears that field at `0x49981f`. The destructor frees both
  allocations and the 24-byte model. There is one encoded direct destructor
  caller in `.text`; this does not exclude computed entry.

No additional FL query-time geometry mutation was found. These paths do not
prove every writer or retirement thread, nor do they establish a durable body
reference. This is the same residual assumption AP already accepts in
[sector collision §14.2](../sector-collide.md): model data beneath the stamped root
is immutable while cached answers may be reused. FL’s different ABI/root layout
does not, by itself, create a new lifetime defect.

The actual shared table expires an entry when `frame - entry.frame > 1`; a
successful hit refreshes `entry.frame`. **This is a sliding expiry since last
use, not a maximum age since original computation.** Matching entries can
survive indefinitely under repeated hits. The wrapper additionally clears on
device Reset, after 100,000 queries without a Present, and after an owner-thread
Present notices an abandoned in-flight call. There is no explicit AP memo
renderer-load, sector-change, registry-retirement or model-destruction callback.
Present expiry is useful stale-address reduction; it is not a proof that a
model cannot change between matching queries.

The exact inherited blind spot is a reset/rebuild or free/reallocation that
returns state 3 at the same model/BV/triangle addresses, with identical header,
root bytes, transforms and mode/minimum key, but different geometry below the
root. For example, an internal root can keep its children’s addresses and coarse
bounds while a deeper leaf/triangle changes from nonintersection to intersection.
A previously stored no-contact query that reached leaves then answers from the
old geometry. A same-address header/root stamp does not detect that change,
and a continually refreshed hit need not age out. This is a concrete key
limitation, **not an observed FL occurrence or demonstrated race**. A transient
non-built state invalidates reuse only if a query actually observes it; the
table does not watch model writes between queries. Verify mode would rerun and
drop a differing output/contact result instead of skipping it.

Existing observer evidence narrows what can be reused without inventing a
collision ownership subsystem. The implemented common object-lifetime observer
has FL factory/load epochs and render-node serials for map `engine+0xc`;
`fl_owned_binding`/journal APIs retain that identity contract. Collision models
are shared resources from the distinct map `engine+0x14`, as confirmed by the
[registry-value lifetime study](registry-value-lifetime.md).
Its recovery `0x485c00 -> 0x4993b0` and ten resource-cleanup callers include
normal reset and teardown. A render-node retirement/serial is not a model
generation, and the current render invocation borrow covers its own invocation,
not collision queries or cross-frame resource retention. Renderer-load epoch
can support an existing coarse flush policy, but does not close model
reset/rebuild/reuse during ordinary resource cleanup. Do not add a per-query
registry walk or claim that existing observer metadata proves model lifetime.
Any stronger exclusion or invalidation requirement is a separate bounded
resource-writer decision; it is not a prerequisite newly demonstrated by FL.

## 5. Minimal adapter and hot-path implications

The concrete next implementation boundary is an FL-only six-word-call thunk
at loaded-base +`0x9853a` targeting loaded-base +`0x11aae0`, selected once by
edition/build. Keep the shared `Table`, hash/equality/minimum relaxation,
expiry/eviction, owner/busy gates, output verification and Present/Reset policy.
Normalize FL’s literal inputs and scatter/gather its output state as above.
The cold profile owns exact caller/query/closure pins and relocation handling;
it must retain the native FL SAT/descent, with no AP SAT replacement claim and
no gratuitous AP entry/rel32 hash holes. Patch claims, instruction boundaries,
module pinning, rollback and shutdown need their existing shared engine-patch
contract; this study neither installs nor alters them. Optional AP census or
phase thunks are separate edition-sensitive hooks, not free dependencies of a
memo port.

This boundary adds no edition test inside traversal. On a lookup it copies
the same 324-byte key as AP, including 192 bytes of model header/root stamps,
reads the existing mode words plus init flag, hashes the first 33 words while
skipping the minimum value, and probes four ways. An ordinary hit replays two
counters plus zero contacts and 56 bytes of normalized root state; it avoids
FL query’s 301 decoded instructions and all descent/leaf work it would reach.
Those instruction counts describe code bodies, not instructions saved on every
path or a measured speedup. Static storage and word copies suffice; there is
no allocation, lock, registry lookup, recursive validation or lifetime scan
needed to express this adapter. Save/restore the live SSE input across lookup
and keep handler arithmetic integer-only. Body/relocation validation belongs
at startup, not per query.

The inherited below-root immutability/ABA and FP-control assumptions must be
stated with the FL implementation; they cannot be upgraded into proven lifetime
or native parity by a successful static pin. Meaningful next acceptance is the
actual FL thunk with synthetic miss/hit/bypass/unwind, init-false refusal,
discontiguous state sentinels, minimum relaxation and rebased-code admission,
followed by the owner’s scoped runtime qualification. No FL speedup, native
Windows behavior or CrossOver gameplay acceptance is established here.

Negative controls reject AP call addresses, FL bytes at the AP wrapper address, a changed
FL query body, a changed scale input, removal of unconditional triangle count,
removal of the built-insertion guard, a second encoded query caller, an encoded
branch into the call operand, and changed relocated-operand identity. The
positive rebase check proves normalization of these static bodies; it is not
execution of the engine at another base.


## 6. Implemented FP context correction

The subsequent native adapter fixture found that caller MXCSR rounding is an
additional output context. At `0x3f80`, native root T.y/T.z (`0x61b98c/0x61b990`)
can be negative zero where the same word key computed under nearest rounding
stored positive zero. The pinned caller/query closure inherits MXCSR and does
not establish stable controls. The AP constant-control assumption therefore
cannot by itself qualify FL cross-rounding reuse.

The implemented FL eligibility seam reads MXCSR after the shared owner/busy
gate, bypasses unmasked exceptions, and clears the existing table when control
bits `0xffc0` change. Status bits remain outside the key and are not replayed.
All word comparisons remain exact; no signed-zero normalization is used. This
keeps the shared key/table/minimum/expiry policy while removing the demonstrated
cross-control reuse. [The verification ledger](../../verification/collide-memo.md)
records the exact field witness, CPU/ABI fixtures at preferred/rebased addresses,
context lifecycle checks and measured added cost. Geometry below root/header
stamps remains subject to the inherited ABA limitation described above.
