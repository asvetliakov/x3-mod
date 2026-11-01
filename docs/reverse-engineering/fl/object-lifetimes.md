# Farnham's Legacy object-lifetime observer contract

Offline targeted analysis, 2026-10-27. The four FL counterparts and the reviewed
registry mutation semantics are established below. **This is not an available FL
observer capability:** baseline/install quiescence, all writer coverage, the
adapter's nested/concurrent bookkeeping and foreign-unwind behavior remain open.
Recognition alone must keep `object_trace::executable_verified()` and the
independent AP checks in `object_lifetime::initialize()` AP-only.

Owning design: [engine profiles](../../architecture/farnhams-legacy-engine-profiles.md).
AP baseline: [object lifetimes](object-lifetimes.md). Image inventory:
[FL engine feasibility](engine.md). No production changes, game or
Wine execution, installation, DLL build or commit were performed for this audit.
All addresses are **preferred VAs**; subtract `0x400000` for RVAs. Counts below
are static measurements unless marked synthetic or inferred.

## Provenance and reproducible checks

Clean `/mnt/c/X3FL-orig/X3FL.exe`, 2,804,736 bytes, SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`;
i386 PE32, preferred base `0x400000`, entry `0x51ea9d`, timestamp `0x618416c8`,
eight sections, `SizeOfImage=0x391000`. AP correspondence uses the clean image
identified in the AP note; AP names or address deltas are not FL evidence.

```sh
python3 verification/probe/verify_fl_object_lifetimes.py
python3 -m py_compile verification/probe/verify_fl_object_lifetimes.py
```

The [verifier](../../../verification/probe/verify_fl_object_lifetimes.py) reuses the
project `inspect_x3.PE32` reader and Capstone. It validates the complete image
identity, **253 exact instruction operands, 36 instruction-aligned region
hashes, four hook spans, four section strings and two heap imports**. It rejects
**12 targeted mutations with whole-image and region hashes deliberately
bypassed**, one interior-start boundary witness, and one changed-image identity
witness (**14 negatives**). Region hashes are on-disk
provenance, not relocation-normalized runtime admission. The local report gives
instruction, relocation, XMM and FS-reference counts per region.

All **72 region edges** are checked against a separate linear decode beginning
at the established `.text` entry thunk `0x401000`; skip-data bytes are not accepted
as instruction boundaries. The saved-SEH-link load is
`0x47993b: mov ecx,[ebp-0xc]`. Starting its epilogue slice at `0x47993d` instead
would decode operand byte `f4` as HLT and still consume the entire slice. The new
negative witness deliberately reproduces that complete but incorrect decode and
requires the earlier-origin boundary check to reject it. The corrected epilogue
is `[0x47993b,0x479952)`; the 36 regions contain **2,969 instructions** (the valid
MOV replaces the false HLT, so the instruction total is unchanged).

`verification/results/fl-port/object-lifetimes/` holds the producing
`reproduce.sh`, `verification.json`, raw `disassembly.txt`, and local exploration
scripts. Raw game code stays ignored/untracked. The verifier restricts output to
this results directory and rejects overlap with either clean executable tree.
A linear `.text` direct-edge census with skip-data decoding is explicitly a
bounded survey, not Ghidra function coverage or proof over computed edges.

## Registry ownership, layout and birth ordering

The FL engine slot is **`0x5aa100`**, and its live render-node/camera registry is
**`(*0x5aa100)+0xc`**. This follows from constructor, birth, restore, removal and
bulk-destruction operands together, not an AP-derived offset assumption.

| Object | Established fields / evidence |
| --- | --- |
| Engine | Constructor `0x4825c0` allocates a zeroed 16-byte map at `0x482601`, initializes counter 1/capacity 8 at `0x48260a`/`0x482611`, stores it at engine `+0xc` at `0x48261a`. Neighbor maps `+0x10` and `+0x14` are distinct. |
| Map, 16 bytes | `+0`: bucket-array pointer; `+4`: bucket count; `+8`: automatic key counter; `+0xc`: entry count. Creator `0x50ced0` and insert/remove/rehash agree on these fields. |
| Bucket array | Four-byte head pointers; index `key & (capacity-1)`, e.g. `0x50cf46..0x50cf4c`. Starts absent with capacity 8; insertion allocates and zeroes the array; growth multiplies capacity by four. |
| Hash node, 12 bytes | `+0`: next pointer, `+4`: nonzero key, `+8`: value pointer. `0x50cfa9..0x50cfb2` initializes a fresh node; `0x50cf53` compares key; `0x50d065` reads the removed value. |
| Render node / camera | Handle remains `+0x28`; ordinary/related nodes allocate `0x270` bytes, cameras `0x790`. Serialized reader chooses by its restored flag. These sizes do not establish every other object field. |

Ordinary allocator `0x4a2200` calls automatic insertion at `0x4a229b` with
ECX=`engine+0xc`, EDX=new node. The related allocator `0x4a3da0` uses
`0x4a3e39`; camera allocator `0x4a4560` uses `0x4a45f9`. Each writes the returned
handle to node `+0x28` **after** the map helper returns (`0x4a22a8`, `0x4a3e42`,
`0x4a4606`). A central insert-return observer must not demand the node handle is
already initialized. Registry membership establishes a birth attempt, not a
fully initialized object ready for arbitrary readers.

Serialized reader `0x492ae0` receives its stream in ECX, aligns its local stack
to 16, allocates the same two sizes and restores `node+0x28` at `0x492c45`.
It inserts `(saved handle,node)` through `0x4934a5 -> 0x50cf30`, then attaches the
node to an engine list. It recursively reads children at `0x4934e7`. Therefore
neither a saved handle nor pointer equality is a lifetime token.

Filtering must use the **exact bound map pointer**. Examples which must not be
counted as render-node births: `0x41378c` uses engine `+0x6324`; `0x493837` uses
engine `+0x10` for a `+0x14` handle; playback `0x490422` inserts an `0xe0` record
through an outer object's `+0xc`, then calls the actual camera allocator at
`0x49042a`. Coinciding field offsets do not identify the owner.

## Helpers and mutation semantics

| Role | AP | FL | FL entry ABI and result |
| --- | --- | --- | --- |
| Create | `0x4efb60` | `0x50ced0` | No explicit input; EAX=map or zero. |
| Set counter | `0x4efbc0` | `0x50cef0` | ECX=map, EDX=counter; normalized counter stored at `+8`. |
| Insert / replace | `0x4efbf0` | **`0x50cf30`** | ECX=map, EDX=key, `[entry ESP+4]`=value; ordinary `ret`, caller cleans four bytes. EAX=1 for a new entry, 0 for key zero **or replacement**. |
| Allocate key | `0x4efcc0` | `0x50cfe0` | ECX=map, EDX=value, no stack args; EAX=chosen key or zero. |
| Remove | `0x4efd30` | **`0x50d040`** | ECX=map, EDX=key, no stack args; EAX=removed value or zero, ordinary `ret`. |
| Enumerate | `0x4efda0` | `0x50d0b0` | ECX=map, EDX=current key (zero starts); EAX=next key or zero. Read-only traversal. |
| Destroy | `0x4efe10` | **`0x50d120`** | ECX=map, no stack args, ordinary `ret`; no stable result meaning established. |
| Rehash | `0x4efeb0` | `0x50d1b0` | ECX=map, EDX=new capacity, no stack args, ordinary `ret`; retains keys/values. |

Insertion replaces `entry+8` at `0x50cf78` and returns zero at `0x50cf7b`;
new insertion increments count at `0x50cfb5`. Count >= capacity calls rehash at
`0x50cfc5` with capacity multiplied by four. The automatic allocator increments
`map+8`, wraps values above `0x7ffffffe` to 1 (`0x50cff5..0x50cffd`), skips
occupied keys, and delegates mutation to insert at `0x50d021`. It never grants
an indefinitely monotonic generation.

Removal unlinks either bucket head (`0x50d063`) or predecessor next
(`0x50d085`), saves the value in ESI, frees the hash node at `0x50d08b`, and
then decrements count at `0x50d090`. There is a real interval with already
unlinked nodes and an old count. Remove does **not** free the value.
Destroy walks each bucket and each `next`, frees hash nodes (`0x50d143`), then
bucket array (`0x50d176`) and map (`0x50d18d`). It does not call remove and does
not free values. A removal-only observer misses this bulk retirement.

Rehash saves old capacity/array, writes **new capacity at `0x50d1c5`**, calls
allocation at `0x50d1d2`, then publishes **new array at `0x50d1d9`**. It moves
existing hash nodes by rewriting next pointers and bucket heads, then frees the
old array at `0x50d222`. No serial needs to change on successful rehash, but
traversal during it is invalid. No allocation-result null check precedes the
following insert/rehash stores; allocator failure or an exception is not a
validated no-mutation return.

The census finds one direct rehash caller, `0x50cfc5`, inside insert. Across all
maps it finds 42 insert calls, 27 automatic-allocation calls, 37 remove calls,
and 35 destroy edges (33 calls plus `jmp 0x4b3839` and `jne 0x4b8e11`). Both
tail-entry callers restore their saved registers before transferring to destroy.
This matters to forwarding: entry hooks cannot assume a direct `call` immediately
preceded the return address. Counter's sole direct call, `0x4bcd86`, uses an
owner's `+0x15f8` map, not the render registry. These counts do not close aliased
writer coverage; no claim is made that every one of the engine global's 443
linear-decoded references has been semantically audited.

## Retirement and engine teardown

General node destructor `0x4a3300` unlinks its list (`0x4a34de`, `0x4a34e6`),
then removes handle `+0x28` from the render registry at **`0x4a34ef`**. Only
afterwards does it clear/free `0x790` or `0x270` bytes (`0x4a3575/0x4a357e`,
`0x4a35aa/0x4a35b3`). Camera-specific destructor `0x4a4750` likewise unlinks,
removes at **`0x4a48a7`**, then clears/frees the camera at `0x4a48ec/0x4a48f2`.
An observer should retire the key before forwarding remove; it must not treat
the helper's return as ownership of the value's subsequent lifetime.

Engine teardown `0x48a950` passes `engine+0xc` to destroy at **`0x48abad`**,
continues with other map/resource cleanup, invokes engine destruction at
`0x48ad6c`, and releases the `0x6a48`-byte engine allocation at `0x48ad77`.
The reviewed path does not zero the engine global. The census's two direct
`mov` writes are publication at `0x48a5f1` and its caller at `0x477697`.
A later generic-map callback therefore cannot assume dereferencing the image
slot yields a live engine. `VirtualQuery`, an age limit, or a shutdown signal is
not proof against concurrent decommit between validation and copying.

## Four candidate spans and forwarding requirements

| Event | Span / bytes | Continuation | Live input and displaced effects |
| --- | --- | --- | --- |
| Insert | `0x50cf30`, 6 bytes: `53 8b da 57 8b f9` | `0x50cf36` | ECX map, EDX key, stack value; saves EBX/EDI and copies inputs to them. |
| Remove | `0x50d040`, 6 bytes: `56 57 8b f9 8b f2` | `0x50d046` | ECX map, EDX key; saves ESI/EDI and copies inputs. Unlike AP, hook the whole entry. |
| Destroy | `0x50d120`, 6 bytes: `53 8b d9 83 3b 00` | `0x50d126` | ECX map; saves EBX, copies map, compares `[map]` to zero. **ZF from displaced CMP is live into JE at continuation.** |
| Renderer load | `0x47984c`, 5 bytes: `e8 bf 9c 01 00` | `0x479851` | Original target `0x493510`; ECX=caller-owned stream (`lea ecx,[ebp-0x70]` at `0x479849`), no stack args; EAX tested immediately on return. |

All spans are whole instructions. The first three contain no relative or
absolute image operand and need exact replay before returning to continuation.
The load call's relative target must be reconstructed if moved; preferred target
and call both shift under ASLR. No HIGHLOW relocation overlaps any span. The
verifier checks HIGHLOW at four selected surrounding absolute operands; whole
function hashes include other relocated operands and cannot be compared against
a relocated loaded image without the profile's explicit relocation treatment.

No direct edge into a displaced interior, and no HIGHLOW-encoded pointer into
these spans, was found in the bounded survey. Computed targets remain unproved.
The three six-byte entries lie within aligned eight-byte blocks. The five-byte
load span at offset four crosses an eight-byte block boundary; a single aligned
eight-byte patch cannot cover it. The existing lifetime installer uses `memcpy`
and requires quiescence for installation and rollback, including callers already
inside a helper. These spans do not provide a concurrent patching protocol.

Insert preserves saved EBX/EDI and, on nonzero-key paths, ESI; remove preserves
ESI/EDI; destroy preserves EBX and conditionally ESI/EDI. Each has balanced normal
returns. Preserve all original input and output GPRs/flags around observer work;
no new scratch-register permission follows from a register's C ABI volatility.
Insert/remove overwrite entry arithmetic flags before branching; destroy's
replayed comparison must reach its branch unchanged. The load caller tests EAX,
so callee output flags are dead at that particular continuation, but preserving
them is still the conservative common-adapter contract.

The complete insert/remove/destroy/rehash regions contain **zero XMM or FS
references**. This is not caller SIMD liveness proof: allocator/recovery paths
and other generic-map callers remain relevant. The renderer-load region has
**14 XMM references**, all to XMM0; the serialized node reader also uses XMM0.
Retain the AP wrapper's full aligned FXSAVE/FXRSTOR discipline around bookkeeping,
preserving the target's actual post-call FP/SSE state, MXCSR, x87 state and
LastError. New C++ callbacks still need the four-byte incoming-stack contract.
Do not copy AP's EDI-map/two-stack-argument extraction: FL insert has one stack
argument, while remove/destroy/load have none. The caller owns insert cleanup.

## Load and foreign-unwind boundary

`0x493510` recognizes `B3D `, `NAME`, `INST`, `SCEN` strings, reconstructs nodes
through `0x492ae0`, then reconnects saved references (e.g. render-map lookups
`0x493b21..0x493b4b`). Its single surveyed direct caller is `0x47984c` in
`0x479430`; returns are ordinary `ret`, including early failure `0x493571`
and final outcomes `0x494150`/`0x49416b`. It saves EBX/ESI/EDI/EBP and aligns its
own local frame to 16. There is no entry stack parameter to copy.

The observer's load epoch must advance **before** forwarding and remain advanced
on failure or unwind. Keep histories unavailable while the load scope remains
active. This return is only renderer-subsection completion: the parent immediately
continues another loader at `0x47985c`; it is not universal game-load success,
sector travel, new-game generation or camera-cut evidence.

None of the four target functions establishes a local FS exception record.
The load parent does: handler `0x54d8ba` is installed through FS:[0] at
`0x479446`; its thunk loads metadata `0x59cefc` then jumps to `0x521368`.
Normal parent return loads its saved SEH link at `0x47993b` and restores FS:[0]
at `0x479946`. This establishes an enclosing
game unwind chain, not compatibility of a future injected frame with all foreign
exceptions or continuations. The AP wrapper installs a real SEH record, has
per-invocation stack scope storage, and invalidates on unwind; FL argument
forwarding and cleanup must be tested with its own synthetic nested and foreign
unwind paths. Process termination, continued exceptions, nonlocal transfers and
callback unwind cannot be declared safe from an ordinary `ret` census.

## Exclusion: baseline, consumer reads and observer bookkeeping

The **mid-rehash failure witness** is recorded by the verifier. Synthetic old
capacity 8, new capacity 32 and valid key 31 select old bucket 7. After capacity
publication and before array publication, the same lookup selects bucket 31:
byte offset **124 in a 32-byte old allocation**. This is a derived arithmetic
witness to mixed metadata, not a witnessed FL crash. It explains exactly why a
second snapshot or an equal epoch cannot substitute for exclusion.

The existing AP implementation provides a useful, narrower exclusion contract:

- `x3m_lifetime_enter` takes `model_lock`, increments `in_flight` for the bound
  registry/load, releases that lock, and only then forwards to engine code.
  `finish` decrements under the same lock after the target returns/unwinds.
- `object_lifetime::current` holds `model_lock` throughout ownership check,
  **`in_flight` rejection before `read_registry`**, and both registry lookups.
  A consumer entering first finishes these reads before a new mutation can pass
  its begin callback. A mutation entering first makes the consumer return
  `MutationInProgress`. Thus a complete active FL hook set using this protocol
  can exclude the entire observed insert/rehash/remove/destroy interval from
  **these reads**, including a reentrant `current()` from allocation recovery.
  It does not require discovering a private engine lock for this narrow case.
- Baseline/install is different. `install` invokes `initial_snapshot` before
  setting `observation=true`; wrappers skip observation while it is false.
  The observer's private lock therefore cannot exclude a concurrent engine
  writer during this snapshot or during patch publication. An externally
  quiescent install/baseline/rollback point remains required for FL.
- Post-insert **bookkeeping** has a separate conditional gap: `finish` decrements
  `in_flight`, then calls `lookup` even if another tracked scope remains active.
  Nested/concurrent same-map completion during another rehash could perform the
  mixed-header read above. This is not demonstrated FL behavior. Either establish
  writer confinement/non-overlap or make a future adapter suppress/defer such
  publication until safe; the consumer guard alone does not settle it.
- The lock does not cover `object_trace::current`'s node/camera/engine reads,
  which precede lifetime validation, nor future camera-buffer copies. It also
  does not cover the node free after the remove target has returned or engine
  free after destroy has returned. Retirement tokens reject later logical use;
  they are not a memory lease for a copy outside the protected interval.

Allocation `0x4d8fc0` can call recovery through `*0x5aae24` at `0x4d906a`;
initialization stores `0x47b500` at `0x4778f3`. The callback visits the engine
(`0x485c00`) and texture cleanup (`0x512410`). Guard `0x62de10` prevents recursive
recovery invocation but is not a registry lock. The CRT allocation chain
`0x531395 -> 0x5337fd` calls HeapAlloc at `0x53382f` and has a failure-handler
path at `0x53381c`. Free `0x52f4e5 -> 0x533b75` calls HeapFree at `0x533b8b`.
Heap serialization does not protect registry field updates outside those calls.
No general no-reentry, no-throw, or engine-writer thread-confinement proof follows.

A quiescent FL baseline can use the established map layout: bounded header and
bucket reads; power-of-two nonzero capacity; exact declared count; each nonzero
key in its masked bucket; nonzero value and `value+0x28 == key`; unique keys and
pointers; bounded acyclic traversal; then recheck header, heads and visited nodes.
Use a fresh observer epoch and reject incomplete baselines. AP's existing
65,536-bucket snapshot scratch bound, 262,144-entry observer capacity and
512-node lookup-chain bound are observer policy limits, not measured FL scene
limits. Baseline failure may leave observation waiting for later births, never
silently adopt preexisting nodes at draw time.

## Remaining qualification boundary

Established: FL helper/body semantics, map layout and owner, normal birth/restore,
individual retirement and bulk teardown order, exact candidate spans and explicit
load epoch, relative/absolute relocation distinctions, and the existing consumer
lock protocol's bounded protection.

Still required before enabling an FL capability: a valid quiescent install and
baseline point; render-registry writer coverage beyond the targeted normal paths
and linear census; safe nested/concurrent post-insert bookkeeping; confinement or
another lifetime mechanism for node/camera/engine copies outside `current`;
FL-specific forwarding/unwind/LastError/FP-state fixtures; transactional ownership,
rollback and retained-trampoline tests for the changed spans; and native Windows
plus CrossOver runtime qualification. No heap/cache access should be admitted by
recognition or by the new instruction verifier alone. Successful offline checks
are not verified native Windows behavior or proof that a camera cut is observed.

Independent deep review accepted the bounded contract after correcting the load
epilogue start to `0x47993b` and adding the interior-start regression. Its corrected
rerun passed all 253 instruction witnesses, 72 region-edge checks and 14 negatives
(exit 0, 3.318 s). The remaining activation prerequisites above still apply.
