# Farnham's Legacy registry writers and retained aliases

Offline bounded checkpoint, 2026-10-28. **No additional registry-topology hook is
indicated by the inspected engine paths.** The named factory initializers keep
the fresh map intact; their apparent `+0xc` writer belongs to a different
allocation. The registered VM dispatcher and recovered tree callbacks reach
reviewed birth/removal paths or modify node fields. The modeled map flow finds
29 engine-slot reads, the known constructor store, seven map-to-helper call
edges, and no map-pointer escape or topology write outside those helpers.

This is sufficient to implement and test the **provisional binding seam plus
outer factory scope** described in [registry binding](registry-binding.md),
while keeping consumers denied. It does **not** finish whole-engine writer/alias
closure or authorize connected readers. The remaining concrete proof is the
engine's retained interior-list aliases and selected stack-spilled owners,
described below; no further arbitrary OS or DLL-entry search is proposed.

The [lifetime activation contract](../../architecture/farnhams-legacy-lifetime-activation.md)
continues to distinguish mutation observation, complete identity coverage,
engine readiness and safe value copying. Constructor/factory teardown and
unwind facts are reused from registry binding. Map layout, normal births,
individual removal and bulk retirement are reused from
[object lifetimes](object-lifetimes.md). Resource reader/name-table
contracts are reused from [resource callers](resource-callers.md)
and [resource resolver](resource-resolver.md). This study changes
no production code and performs no game/Wine execution, build, install or commit.

## Reproduction and claim boundaries

```sh
python3 verification/probe/verify_fl_registry_writers.py
python3 -m py_compile verification/probe/verify_fl_registry_writers.py
```

The [verifier](../../../verification/probe/verify_fl_registry_writers.py) checks
clean `/mnt/c/X3FL-orig/X3FL.exe`, 2,804,736 bytes, SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
All addresses below are preferred VAs at base `0x400000`. Measured: **26 regions,
5,383 instructions, 154 exact operand anchors, 183 dispatch-table entries,
eight walker callbacks and twelve negative cases**, 3.001 s on the recorded run.
Region and anchor boundaries are checked against the separate `.text` origin
decode. The three computed dispatches are represented as explicit CFG edges;
normal call-return CFGs do not assert exception-path closure.

Raw decompilation, Ghidra project, disassembly, machine-readable results and
producing scripts stay ignored under `verification/results/fl-port/registry-writers/`.
`reproduce.sh` reruns the verifier, syntax and result-schema checks. Ghidra was
used to discover retained aliases and distinguish owners; machine operands are
the final witnesses. Its inferred custom helper prototypes omit some register
arguments, so a decompiler call with no printed arguments is **not** evidence
that no map was passed.

Six instruction/table mutations bypass image identity, region digests and
boundary checks, requiring their specific semantic rejection. Four more bypass
all those checks **and all operand anchors**, injecting a map-header store,
bucket-head store, hash-next store and map-pointer stack escape into the lookup
path. Ownership-flow checks reject each one. Whole-image mismatch and a walker
slice beginning inside an instruction supply the last two negatives.

The ownership aid seeds the 435 explicit engine-global loads and the already
proved constructor receiver, follows register copies/LEAs, constant add/subtract,
branch merges, EBP-relative locals and candidate ECX/EDX call inputs. It visits
10,945 instructions in 14,833 work-list steps, with 179 candidate argument edges.
These are **analysis counts**, not a claim that every live ECX/EDX is a formal
argument. Partial-register writes kill the full pointer register. Indexed or
loop-widened engine addresses become unknown offsets. Known generic map
helpers terminate the flow and retain their previously reviewed contracts.

This aid is deliberately not a sound whole-program pointer analysis: it does
not reload aliases through general heap memory or ESP-relative locals, summarize
callee return values, or follow exceptions and arbitrary computed callees.
Unsupported provenance can be lost. Zero modeled uncovered writes means exactly
that; it is not a proof that no unmodeled writer exists. All three machine
qualification flags—registry-writer closure, value/free closure and renderer
readiness—remain false.

## Factory initialization: the registry is not the other `+0xc`

The full constructor's sole direct registry publication and factory order are
already reviewed. The following normal callees now have bounded ownership
classification, including failure/cleanup branches in their complete bodies.
No new callback-free-factory assumption is needed for the proposed provisional
binding: observed births may occur while consumers remain denied.

| Factory route | Established owner and writes |
| --- | --- |
| `0x487100` | Retains engine in EDI and `[ebp-0x10]` (`0x48711d/125`); initializes `engine+0xbc` body records, stride `0x1c`, then reads the named resource and calls name setter `0x4866e0` at `0x487372`. |
| `0x487420` | Initializes scene records under `engine+0xc8`; setter call `0x4875c7 -> 0x486a30` writes the separately allocated scene name and record, not engine `+0xc`. |
| `0x485f60` | Retains engine at `[ebp-0x10]`; table begins at `engine+0xcc` (`0x485f88`), stride `0x18`, with the initialization loop bounded by 1,000 at `0x486061/67`. Parsed records stay in that color-table family. |
| `0x483f20` | Retains engine at `[ebp-0x1c]`; initializes the particle table around `+0x632c/+0x6330`, 100 entries, stride `0x10`, and their separately allocated particle records. The seven-entry dispatch at `0x48440c` is bounded by `0x484403` and table `0x4846dc`. Texture registration is the reviewed resource path. |
| `0x4870d0` | Preserves engine receiver in ESI and forwards it to `0x487e50` and `0x4881a0`; first calls cleanup `0x488110`. Cleanup frees the existing `+0xac/+0xb0` group table and its child allocations. |
| `0x487e50` | Saves engine receiver EBX at `[ebp-0x18]`, writes group count `engine+0xac` at `0x487ee9` and table `engine+0xb0` at `0x487f05`. Table entries are count/pointer pairs; their child records are 20 bytes. |
| `0x4881a0` | Reads names and registers bodies via `0x486c60`; after body-ID range checks, writes body record `+0x14` at `0x4882c8`, through `engine+0xbc`. |
| `0x485d80` | Traverses the already mounted catalog array under configuration global `0x5a83b0`, performs catalog lookups and manages temporary strings. It has no engine-global load or registry receiver. |

**Concrete false replacement witness:** `0x488052: mov [ebx+0xc],eax` is inside
`0x487e50`, whose entry originally copied engine ECX to EBX. But `0x487f0b`
zeros EBX into a table index; engine remains saved in `[ebp-0x18]`.
`0x487f24` loads the `+0xb0` table, and `0x487f9c/0x487faa` publish a new child
allocation in its pointer slot. At `0x487fc0`, EBX is replaced by that child
pointer; `0x487fc6` adds the per-record offset. Thus `0x488052` writes child
record `+0xc`, alongside `+4/+8/+0x10`, not the registry pointer. Before body
registration, ECX is explicitly reloaded from the saved engine at `0x487fd1`.
The verifier pins this owner transition and rejects substituting engine `+0xc`
for the actual group-table publication.

The reader, allocation and resource-registration calls remain reentrancy/failure
surfaces. Their presence does not itself imply a registry writer, and their
normal returns do not establish engine readiness. The already reviewed outer
factory-scope failure contract remains required in both construction phases.

## Computed targets with actual engine callers

Constructor registration identifies VM callback `0x4afbf0`. Its primary switch
checks command <= `0xa9` at `0x4afc35`, then dispatches through `0x4b297c` at
`0x4afc41`: **170 entries, 158 distinct targets**. An inner switch checks <=5 at
`0x4b1cec` and uses six entries at `0x4b2c24` through `0x4b1cf5`. All targets are
instruction boundaries within `[0x4afbf0,0x4b297a)`. Its complete normal CFG has
3,201 nodes and 3,617 edges. It contains direct calls, not unresolved indirect
CALL instructions; the two indirect JMP tables are the computed dispatches.

| VM command | Reached concrete operation |
| --- | --- |
| 0 | `0x4afc48 -> 0x4a2200`, ordinary node birth |
| 1 | `0x4b009b -> 0x4a3da0`, related node birth |
| 2, 4 | `0x4afc8b -> 0x4a3300`, ordinary retirement/destruction |
| 5 | `0x4b00bd -> 0x4a4560`, camera birth |
| 6 | `0x4b00f4 -> 0x4a4750`, camera retirement/destruction |
| 36, 38 | `0x4afe61/0x4afec1 -> 0x4a2730`, tree-walker callbacks below |
| 92 | `0x4b1e42 -> 0x48f280`, playback cleanup; its inline render-map lookup precedes the reviewed node/camera destructors. The playback record's own maps are different owners. |
| 130 | `0x4b267e -> 0x485c00`, reviewed recovery/resource traversal |

This table is a reachability classification for these selected targets, not
proof that no other VM command can transitively create or remove a node. Other
VM calls remain in their owner-specific functions; the seven directly
map-carrying engine call edges below are independently checked.

Tree walker `0x4a2730` is a real indirect-call boundary. ECX carries the engine
through EBP to recursive invocations, but its callback receives two
caller-cleaned stack arguments: node and context. `[entry ESP+4]` supplies the
callback, saved in EBX at `0x4a2731`; `[+8]` supplies the node, `[+12]` the
context. It calls EBX at `0x4a2744`, then follows the node's `+0xc` child list,
not engine `+0xc`. It returns `ret 0xc`. The direct caller census is nine outer
sites plus recursion at `0x4a2756`; every outer callback is an immediate target.
There is no HIGHLOW-encoded pointer to the walker entry in the inspected image.

| Callback | Effect on the node supplied on the stack |
| --- | --- |
| `0x4838f0`, `0x483900`, `0x483910` | Store context at node `+0x1c4`, `+0x1c8`, `+0x1d4` respectively |
| `0x4a2770` | OR context into node `+0x12c` |
| `0x4a2780`, `0x4a2790` | Set/clear context bits in node `+0x130` |
| `0x4a27b0`, `0x4a27c0` | Store context at node `+0x13c`, `+0x140` respectively |

All eight callbacks are straight-line ordinary-RET functions, with no calls
or control transfers. In particular, the VM's two walker calls supply
`0x4a27b0` and `0x4a27c0`; they cannot bypass insert/remove by replacing a map.
No concern about a fixture inventing another callback is used to reopen this
native caller set. The node-copy receiver `0x4a27d0` preserves ECX only for
recursion and creates children through `0x4a2870 -> 0x4a2200`; its node-field
copies are not engine-map stores. `0x4a2550` recursively searches child nodes;
`0x483310` reads a separately bounded ten-slot engine resource table.

## Membership topology versus the automatic key counter

The modeled owner flow reaches the following exact map-carrying calls. The
constructor store `0x48261a` is the only modeled engine `+0xc` writer.

| Role | Call sites and target |
| --- | --- |
| Ordinary/related/camera birth | `0x4a229b`, `0x4a3e39`, `0x4a45f9` -> automatic key helper `0x50cfe0` |
| Serialized restore | `0x4934a5` -> insert `0x50cf30` |
| Ordinary/camera removal | `0x4a34ef`, `0x4a48a7` -> remove `0x50d040` |
| Registry teardown | `0x48abad` -> destroy `0x50d120` |

The other modeled engine `+0xc` reads are inline lookups and enumeration. The
retained EDI owner in renderer load accounts for reads at `0x493913`,
`0x493a25`, `0x493a81`, `0x493b21`, `0x493b5f`, `0x493ec0`; its final lookup is
`0x4940a0`. Enumeration repeats map loads at `0x4a28d0` and `0x4ab4d0` across
loop back edges, in addition to their initial loads. Reading `hashNode+8`
changes the owner to the render value: subsequent relinking or node-field
stores are not writes to the hash node or registry header.

**There is a real header write outside insert observation:** automatic-key
helper `0x50cfe0` updates map `+8` at `0x50cffd`, before calling insert at
`0x50d021`. It increments/wraps the candidate counter and can search again
without inserting. This is the allocation counter, not capacity, entry count,
bucket array or membership. Identity lookup does not need it to remain fixed;
a proposed reader must not claim that every header word is protected by the
insert interval. A counter-dependent concurrent snapshot would need an
additional contract, but that is not required for provisional from-empty
membership observation.

The separate counter setter writes `+8` at `0x50cf1f`; its sole direct caller
is `0x4bcd86`, the already reviewed unrelated `+0x15f8` map. Rehash's sole direct
caller remains `0x50cfc5` inside insert. No HIGHLOW-encoded entry pointers to
counter, automatic-key, rehash or walker helpers were found. These are actual
engine reference facts, not an assertion that code can never numerically
construct an address. No concrete computed route passing the bound map to
counter or rehash was found; inventing one is not a reason to add a hook.

## Remaining reachable alias frontier and the next step

No additional map-pointer store, push, return or topology mutation was found in
the modeled routes. **Engine interior pointers do escape**, so the register
result alone cannot certify every later write through those aliases:

The ownership-flow aid also stops at computed JMP successors, including the
particle switch at `0x48440c`. The verifier separately reconstructs that dispatch
CFG, but does not propagate ownership through its edges. Its zero-modeled-write
count therefore does not certify those switch arms; retain their explicit
owner-flow inspection in the remaining proof.

- Intrusive-list publications include `0x4934c1` and `0x4a23ba` storing
  `engine+0x2c` into a node link; camera/related births use the same list family.
  `0x493857` publishes `engine+0x1c`; `0x494235` publishes `engine+0x44`;
  resource list links at `0x49c3c9` and `0x4a17ca` publish `engine+4`.
  Other retained list families include `engine+0x631c` at `0x413778/0x4912d1`.
  These are concrete heap aliases, not copies of the registry pointer.
- The next list proof should finish the **sentinel and unlink address bounds**:
  normal end sentinels must be tested before node callbacks; link stores must
  stay in the list's head/tail fields. In particular the first engine list at
  `+0/+4/+8` is adjacent to registry slot `+0xc`. The reviewed destructor paths
  already supply selected unlink ordering; this study has not generalized that
  contract to every retained list consumer. Do not treat an end sentinel as a
  render value or infer safe offset access merely because its allocation is the
  engine.
- EBP-local engine retention in the named factory initializers is resolved
  above. The flow aid does not follow ESP-local owner spills such as
  `0x415aee`, `0x416490`, `0x48477a`, `0x484a4b`, `0x487a3a`, `0x4913b8`,
  `0x495466` and `0x4964d1`. These give specific producer/consumer functions
  for the remaining offset/argument audit. Scalar-table indexing and implicit
  register returns likewise need their actual ABI before being called an
  engine-pointer escape; a live EAX at RET is not by itself a return contract.

The next minimal **implementation** remains the two already located seams:
provisional bind at `0x48261a`, enclosing factory scope at `0x477692`, plus the
existing three map adapters and load adapter. Keep coverage provisional and
readers denied until the retained-owner proof is reviewed. This checkpoint
adds no interception requirement for the allocator counter or arbitrary VM
entry. If the remaining alias audit finds a real additional registry writer,
qualify that concrete site or require a protected complete baseline; do not
silently claim that later births repair missing survivors.

The next minimal **static proof** is confined to the named retained owners and
intrusive-list consumers, using their stack and sentinel provenance. It does
not need another constructor/factory publication audit or indiscriminate
semantic review of all 443 global-reference instructions. Separately, value
free/lease safety (including `0x485c00 -> 0x4993b0` resource cleanup) and camera,
material or scene readiness remain consumer-specific obligations. Neither
normal renderer-load return nor the current zero modeled-writer result closes
those requirements.
