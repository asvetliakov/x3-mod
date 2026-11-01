# Farnham's Legacy registry: retained interior owners

Offline scoped writer checkpoint, 2026-10-28. The retained aliases named in
[registry writers](registry-writers.md) do **not** introduce an
additional render-registry writer or require another registry hook. They are
intrusive-list sentinels, table owners and particle-record owners. Their actual
stores and callback arguments can be separated from `engine+0xc` and the bound
map. The particle switch's previously omitted owner flow is now explicit.

This closes that **named retained-owner frontier under the normal, well-formed
list/table contract**. It is not a sound whole-program alias analysis, a proof
against arbitrary corrupted pointers or a reader capability. The implementation
frontier is still the already qualified provisional publication at `0x48261a`,
the enclosing factory call at `0x477692`, and the three map/load observation
adapters. This study finds no reason to replace those seams with enumeration at
factory return. Actual reader admission additionally needs their implemented
incarnation/coverage protocol and the consumer's value-lifetime and readiness
contract; it must not be enabled merely because this verifier passes.

Constructor, factory failure/unwind, normal registry destruction and allocator
contracts are reused from [registry binding](registry-binding.md)
and [object lifetimes](object-lifetimes.md). This note does not
requalify CPU transport or imply that existing production adapters already meet
that contract. No production change, build, game/Wine execution or install was
performed.

## Reproduction and evidence limits

```sh
python3 verification/probe/verify_fl_registry_retained_aliases.py
python3 -m py_compile verification/probe/verify_fl_registry_retained_aliases.py
```

The [verifier](../../../verification/probe/verify_fl_registry_retained_aliases.py)
uses the same clean `/mnt/c/X3FL-orig/X3FL.exe`, SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`,
2,804,736 bytes, preferred base `0x400000`. Measured in the recorded run:
82 regions, 4,826 region instructions **including overlapping slices**, 770
operand anchors, 47 head-guard simulations, 132 actual-instruction link cases,
seven particle switch arms/136 instructions/13 record stores, eight stack-owner
flows and ten negative cases, 0.471 s. Raw listings, local research scripts, decompiler
output, JSON and `reproduce.sh` stay ignored under
`verification/results/fl-port/registry-retained-aliases/`.

Every region boundary is checked against the separate `.text`-origin decode.
The guard simulator executes each first guard twice, with sentinel-next zero
and real-node-next nonzero. It rejects replacing a next-word test by a pointer
null test. Link cases execute the actual MOV/LEA store sequences for empty and
one-to-five-node lists, all removal positions, both adjacent-sort directions,
and source/destination splices. They check the complete write-address set,
including engine head/tail aliases. They are instruction-level fixtures, not
claims of native Windows or CrossOver execution.

Eight negative cases bypass **all** operand anchors and hashes: pointer-null
substitution, reversed guard branch, unlink/sort/splice payload writes, particle
store bounds, particle owner clobber and a switch target inside an instruction.
A ninth removes the conditional light-capacity alias witness described below.
A tenth changes the particle-list tail store to `engine+0xc`; the stack-owner
flow detects the resulting registry-slot write. None needs a synthetic external
caller of a private game helper.

The new stack aid follows the eight named seeds, register copies, ESP-relative
slots across push/pop/callee cleanup, EBP locals and direct branch joins. Its
measured total is 3,012 visited instruction instances across seeds, 6,245 work
steps and 24 tracked reload sites. It kills partial-register pointer provenance.
Two cleanup summaries with computed dispatch, `0x449690` and `0x502f90`, are
explicitly pinned ordinary-RET bodies. It still does not summarize arbitrary
callee return values, heap reloads or exceptions, and indexed addresses widen
to unknown offsets. **Zero modeled `+0xc` stores is not the closure proof.**
The owner/range arguments below supply the bounded interpretation; do not use
this aid as a replacement for them or for the previous callback inventory.

## The adjacent resource-list sentinel is not a resource object

For a list whose three-word header begins at `L`, the layout is:

| Address | Meaning |
| --- | --- |
| `L+0` | First node, or `L+4` when empty |
| `L+4` | Permanently zero end-marker next word |
| `L+8` | Last node, or `L` when empty |
| Node `+0/+4` | Next/previous link |

For the resource list, `L=engine`. Its end sentinel is therefore `engine+4`,
and its sentinel-relative `+8` would be the registry slot. That access must be
excluded by control flow, not by allocation identity.

At `0x49c3bb..0x49c3cb` and `0x4a17bc..0x4a17cc`, append reads the old tail,
writes `new.previous=oldTail`, `oldTail.next=new`, `new.next=L+4`, and
`L.tail=new`. On an empty list the indirect old-tail store is exactly `L+0`.
It cannot reach `L+0xc`. Resource removal at `0x499838..0x499847` performs
`previous.next=next; next.previous=previous`. For a first/last/sole node these
aliases resolve to `L+0` and/or `L+8`. The preceding map removal is from
**engine `+0x14`** at `0x499830/833`, not the render registry.

The actual resource-head consumers reject `*candidate==0` before payload:

| Consumer | Entry/repeated guard and operation |
| --- | --- |
| Resource recovery | `0x485d05/08`; saved-next guard `0x485d6b/70`, before resource cleanup `0x485d66 -> 0x4993b0` |
| Normal reset | `0x48a8a4/a7`, `0x48a8be/c1`, before `0x48a8b2 -> 0x4993b0` |
| Engine teardown | `0x48aaa5/a8`, `0x48aabe/c1`, before `0x48aab2 -> 0x4993b0` |
| Resource reset | `0x4a1e86/89`, `0x4a1e9e/a1`, before `0x4a1e92 -> 0x4993b0` |
| Selective purge | `0x4a1ec8/cb` in `0x4a1ec0`; payload `+0x64/+8` and cleanup occur only on a real resource |
| Resource update | `0x4a6a4e/51` in `0x4a6a40`, before `+0x54` and child-array processing |

The list end marker is read as a link, never supplied as the resource payload
to these calls. Cleanup may call arbitrary resource-specific work **after**
selecting a real resource; that is a value/reentrancy obligation, not an alias
from the end sentinel to the registry. The existing failure/outer-scope contract
still governs construction failure; these normal link invariants do not turn
an asynchronous fault into a normal return.

## The other retained lists and nontrivial relinking

| End sentinel | Header / publication | Consumer and unlink result |
| --- | --- | --- |
| `engine+0x1c` | Header `+0x18`; publication `0x493857`, also `0x4a4b0a` | Scene traversals use the same next-word guard. Scene destruction `0x4a5549..5555` writes only previous-next/next-previous; engine aliases are `+0x18/+0x20`. Its map is engine `+0x10`. |
| `engine+0x2c` | Header `+0x28`; `0x4934c1`, `0x4a23ba` and related/camera births | Guards precede renderer-load payload repair, save, recovery and rendering walks. Ordinary `0x4a34d8..34e6` and camera `0x4a488c..4898` unlink stores alias only `+0x28/+0x30`; their render-map retire calls remain the reviewed ones. |
| `engine+0x44` | Header `+0x40`; `0x49419d/0x494235` | Cleanup, rendering, sorting and splicing below alias only the two list link words. |
| `engine+0x631c` | Header `+0x6318`; `0x413778`, `0x4912d1` | Serialization guards `0x48e120/123` and `0x48e75f/762`; node cleanup below aliases only `+0x6318/+0x6320`. |

The 47 pinned head entries include recovery/reset/destruction, scene update,
renderer save/load, render-list processing and serialization. Some tests use a
previously zeroed EBX/EDI rather than immediate zero: the zero producers at
`0x48b864`, `0x49768b` and `0x4ab2f7` are separately pinned. The lookup-only walk
at `0x493ad4` follows next pointers to zero and does not read payload.

Sorting `0x4975c0` is not merely an ordinary guarded traversal. It retains
`engine+0x44` in ESI; before comparing two payloads at `+0x14`, it tests the
successor against that end boundary (`0x4975f2/f4` or `0x497642/644`). The swap
sequences `0x4975fe..7616` and `0x49764e..7666` touch only node links and
engine `+0x40/+0x48`. The moving sort boundary remains a node or the end sentinel.
It never becomes a render value or map pointer.

`0x4978f8..791d` moves the complete `+0x40` list onto the `+0x34` list.
The initial guard `0x4978fe/7901` excludes an empty source. Its two boundary
repairs and append stores can affect engine `+0x34/+0x3c/+0x40/+0x48`; the new
end marker is `engine+0x38`. This extra sentinel is an actual consequence of
the named splice, with the same two-link bounds, not a new map escape.

Particle controller nodes in the `+0x6318` list have another retained route:
`0x413794` stores the allocated node in a separate wrapper's `+8`. The stored
value is the allocated node in ESI, **not** `engine+0x631c`; that sentinel is
stored only in the node's next link at `0x413778`. Cleanup at `0x416cd4/d7/d9`
and `0x417722/725/727` loads and null-checks the wrapper payload. The unlink
sequences `0x416cf2..cfe` and `0x41772d..739` again write only the two links.
Their removal helper receives the separate map at engine `+0x6324`
(`0x416d0a/10`, `0x417745/4b`). Serialization cannot publish the sentinel as a
wrapper payload because it selects only guarded real list nodes.

## Eight stack-spilled owners

| Spill | Resolved use after reload |
| --- | --- |
| `0x415aee`, `[esp+0x2c]` | Color-table reads at engine `+0xcc..+0xe0`, stride `0x18`; node properties receive the loaded scalars. Reloads `0x415b44/6e`; no engine store. |
| `0x416490`, `[esp+0x40]` | Same color-table family; five reloads through `0x416558`, no engine store. |
| `0x48477a`, `[esp+0x1c]` | Particle tick transfers a real node from header `+0x62f8` to `+0x62ec`; tail publication `0x4847cd` is `+0x62f4`. After three pushed arguments, reload `0x484961 [esp+0x28]` names the same saved engine. |
| `0x484a4b`, `[esp+0x38]` | Particle emission reads `+0x632c/+0x6330` descriptors, then transfers a real free-list node to the active list; `0x484dea` stores end sentinel `+0x62fc`, `0x484dec` stores tail `+0x6300`. |
| `0x487a3a`, `[esp+0x14]` with one outstanding argument | Body/scene name serialization. Engine is recovered from `[esp+0x10]` after cleanup; `+0xb4/+0xb8/+0xbc` and `+0xc0/+0xc4/+0xc8` are counts/tables. Explicit interior arguments are four-byte count fields `+0xb8/+0xc4`. |
| `0x4913b8`, `[esp+0x1c]` | Restored particle-controller construction reads descriptor count/table `+0x632c/+0x6330`, allocates node `+0xa4` child storage and stores descriptor pointers there; engine is only the table owner. |
| `0x495466`, `[esp+0x10]` | Builds the light candidate array at `+0x5e8c`; each append checks count `<0xfe`, and the zero terminator is at index <=254. Maximum terminator address is engine `+0x6284`. |
| `0x4964d1`, `[esp+8]` | Reads that terminated array and updates 12-byte light-selection records at `+0x628c/+0x6290/+0x6294`, using the configured count at `+0x6288`; sorts selected records with comparator `0x496490`. The exceptional replacement-index case below is distinct from the ordinary array range. |

The name serializer calls `0x502f90` before and after serializing the four-byte
count. That helper can **write**, performing descriptor-driven byte swapping.
Its writes are confined to those count fields; calling it a read-only serializer
would be incorrect. `0x506820` receives the same bounded count source. Neither
operation receives engine base or the render-map pointer as its buffer.

The light sorter receives base `engine+0x628c`, selected count, record size 12
and comparator `0x496490`. That comparator only reads record `+4` and returns
ordering; it neither publishes the record address nor writes through it. This
is a real computed callback with engine interior arguments, distinct from the
map. Capacity is initialized at `0x4da1b7` from the renderer's `+0x4f4` field,
then signed-upper-clamped to eight at `0x4da1bd/c2`; there is no lower clamp.
Normal initialized arrays and non-overflowing index arithmetic are the healthy
engine precondition for this writer classification.

**Bounded native overlap, not a demonstrated registry writer:** replacement
index starts at -1 (`0x4967aa/b3/c3`), while the search threshold starts at 65535
(`0x496799`). With positive capacity 1..8, if every occupied score is >=65535,
no replacement index is selected. If the incoming score is >65535,
`0x4967f8/fa` allows stores at `0x496803/80a/818` with index -1. They target
engine `+0x6280/+0x6284/+0x6288`, the last of which is capacity itself. Thus it
would be incorrect to assert that every execution writes inside the configured
selection array. Zero/nonpositive capacity skips the search and has the same
conditional issue.

The verifier executes that exact branch with capacity 1, occupied score 65536,
incoming score 70000, and finite distance 100: the three writes occur and
capacity becomes 100. This is a **conditional machine-state witness**, not a
claim that healthy scene producers generate those scores. The initial weighted
signed-short color score is at most 32767, or 33535 after the `0x800000` flag's
768 bonus. The later `0x10` directional multiply at `0x4966d9..6733` can amplify
it at machine level; valid producer constraints on that factor are not proved
here. The witness does not assert arbitrary fixture/API invocation authority.

No healthy path from this overlap to `engine+0xc`, or to the bound map's
header/buckets, is established. Later loops can consume the overwritten
capacity, but classifying arbitrary resulting out-of-bounds corruption as a
registry alias would expand this task into unrelated native memory safety.
Keep the healthy-table precondition explicit; this conditional overlap does
**not** establish another registry hook requirement. Consumer work that relies
on these light-selection arrays must resolve the ranking/capacity invariant or
validate its own bounded snapshot. The normal ownership conclusion here is the
separation of these light-table destinations from the render registry.

## Particle dispatch: explicit owner flow through all seven arms

At `0x484090`, EDI changes from engine to the separately allocated descriptor
array loaded through `engine+0x6330`; `0x484099` adds the current 128-byte record
offset. The engine remains in `[ebp-0x1c]`, reloaded only at `0x4845f4` after
processing the record. This is the owner used by switch `0x48440c`, bounded by
`0x484403`, through table `0x4846dc`.

All seven targets (`0x484413`, `0x484425`, `0x484488`, `0x4844eb`, `0x4844fd`,
`0x48450f`, `0x48456c`) retain EDI unchanged and converge at `0x4845be`.
Their 13 descriptor stores address offsets `0x5c,0x60,0x64,0x68,0x6c,0x70,0x74`.
The only other memory stores in those arms advance the independent input
reader's cursor at ESI `+8`. No arm reloads the saved engine, loads the engine
global, passes EDI as a buffer argument, or computes another dispatch. Calls
use the reader in ECX; EDI remains the preserved descriptor owner. The verifier
walks these edges with that owner invariant, rather than relying on the old
ownership aid, which stopped at the computed JMP.

## Admission decision and remaining obligations

For the named normal paths, sentinel rejection prevents engine interiors from
becoming payload objects; relinking cannot address the registry slot; the stack
owners and computed callbacks have separate bounded destinations. No additional
registry publication, map-header/bucket writer or map-pointer escape is exposed.
The separate automatic-key counter write already recorded in registry writers
is unchanged. No new interception point is justified by this frontier.

Implement the proposed from-empty binding/factory scope and exercise its
success, allocation-null, construction failure, post-constructor failure and
terminal revocation paths under the existing binding contract. Keep consumers
unreadable until that implementation has established a complete incarnation.
A completed identity still does not keep a resource/body/material alive or
establish a ready camera. Resource cleanup/leases and renderer readiness remain
separate consumer obligations, not additional speculative registry writers.
The verifier consequently reports the normal named retained-owner closure qualified,
but reader admission, value/free closure and renderer readiness false.
