# Object lifetimes and temporal-history reset boundaries

There is a concrete renderer-scene load boundary and there are concrete node and
camera retirement boundaries. A render-node handle is **not a lifetime token**:
automatic allocation can wrap, deserialization restores saved handles, and map
insertion can replace an existing value. The opt-in implementation now combines an
explicit renderer-load epoch with central registry mutation tokens; consumers must
also apply the existing device/frame validity gates. The follow-up audit below supersedes
the initial proposal to observe only individual retirement callsites. This investigation does not establish a
universal sector-transition or camera-cut hook.

## Provenance

Read-only analysis of `X3AP.exe`, 2,153,984 bytes, SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`,
x86 PE32, preferred image base `0x00400000`. All addresses below are preferred
VAs for this exact build; use checked module-relative addresses at runtime.
Ghidra 12.1.3 targeted xrefs/instructions/decompilation were checked independently
against MinGW objdump and installed PE bytes. Raw outputs remain local under
`/tmp/x3-lifetime-*`; no game implementation is redistributed here.

The original read-only [fingerprint tool](../../tools/analysis/inspect_object_lifetimes.py)
verifies the full executable hash/size, PE architecture/base, eight call opcodes
and their relative targets, and four central instruction boundaries. The
[derived report](../../verification/results/object-lifetime-sites.json) includes
five-byte call fingerprints, central displaced-instruction bytes and SHA-256
digests of context/callee regions. The runtime observer below uses those exact
boundaries; the analysis tool itself remains read-only.

```sh
python3 tools/analysis/inspect_object_lifetimes.py \
  "$HOME/Library/Application Support/CrossOver/Bottles/Steam/drive_c/X3/X3AP.exe" \
  --output verification/results/object-lifetime-sites.json

JAVA_HOME='/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home' \
  '/opt/homebrew/Cellar/ghidra/12.1.3/libexec/support/analyzeHeadless' \
  /tmp/x3-ghidra-research X3Render -process X3AP.exe -noanalysis -readOnly \
  -scriptPath tools/analysis -postScript X3ObjectContext.java \
  /tmp/x3-lifetime-context.txt 004efcc0 004efd30 00479d10 00487be0 00608518
```

Additional targeted runs used `X3ObjectContext.java` and
`X3DecompileFunctions.java` for `0047a720`, `00470d80`, `00488de0`, `004efbf0`
and parent loader `00404cc0`. Static xref completeness means references resolved
in this Ghidra program, not proof that computed/indirect calls cannot exist.

## Renderer-scene deserialization: a narrow explicit epoch

`0x0047a720` parses a renderer-state stream containing `B3D `, `NAME`, `INST`
and `SCEN` sections. It rebuilds render nodes, cameras and spatial-context
records, then reconnects their stored references. Its node reader `0x00479d10`
allocates fresh storage, restores node `+0x28` from serialized data at
`0x00479dd2`, and installs that handle into the live registry at `0x0047a6ad`.
The node reader also recurses into children. This is direct evidence that a saved
handle can outlive the allocation which previously carried it.

The single statically resolved direct caller of `0x0047a720` is:

| Property | Verified fact |
| --- | --- |
| Caller | `0x00404cc0`, a larger stream/file loading routine |
| Callsite | **`0x0040508d`**, RVA `0x508d` |
| Original call bytes | `e8 8e 56 07 00` |
| Target | `0x0047a720` |
| Entry stack | `ESP+4`: pointer to caller-owned stream state |
| Cleanup/result | Caller adds 4 bytes at `0x00405092`; tests EAX at `0x00405095` |
| Callee evidence | Loads its sole argument from `EBP+8`; initializes its working registers, preserves EBX/ESI/EDI/EBP; ordinary RET |

A callsite wrapper can increment an independent 64-bit `renderer_load_epoch`
**before** forwarding this call. Invalidate histories even if loading fails or
unwinds; never restore the prior epoch on failure. A surrounding load-in-progress
scope should reject history publication until normal completion, with explicit
unwind cleanup if the scope is used. The return only establishes this renderer
subsection's result: the caller subsequently loads other state. It is not a
global “game load succeeded” notification.

This five-byte call seam avoids relocating a function prologue. It still requires
the existing exact-executable/in-memory fingerprint gates, quiescent install and
rollback ownership, and original-code forwarding/unwind verification. No live
patch was attempted here. Incrementing only on successful return would miss
partial mutation; calling it a universal scene generation would overstate the
identified coverage. Sector travel or new-game construction may use other paths.

## Handles, insertions and retirement

The render registry is the pointer at `(*0x00608518)+0xc`. Ordinary node allocator
`0x00486d10`, related allocator `0x004885a0` and camera allocator `0x00488c70`
call `0x004efcc0`. This helper takes the map in EAX and a pointer on the stack,
increments map `+8`, wraps above `0x7ffffffe` to one, and searches occupied keys.
The counter is not monotonic indefinitely and is not a load generation.

Insertion helper `0x004efbf0` takes **EDI = map**, stack `ESP+4 = key`,
`ESP+8 = value`. It returns zero for key zero; an existing key is overwritten
and also returns zero. A fresh insertion updates the map's entry count. Therefore
“zero means no mutation” is false. The automatic allocator normally avoids the
existing-key case, but explicit restore does not provide that guarantee.

Deletion helper `0x004efd30` takes **EDI = map, EDX = key**, no stack arguments.
It returns the removed value in EAX or zero if absent. It does not receive a
normal C++ `this` in ECX; Ghidra's inferred fastcall label is insufficient ABI
evidence. The instructions use EDI directly for buckets/count and EDX for key.

| Candidate callsite | Target | Purpose and extra caller evidence |
| --- | --- | --- |
| `0x004efd09` | `0x004efbf0` | Central insertion within automatic handle allocation; EDI is its map, stack key/value, caller cleans 8 bytes. Generic to many maps: filter the verified render map. |
| `0x0047a6ad` | `0x004efbf0` | Restored render-node insertion; EBX is new node, EDI is render map, stack contains restored handle/node. Caller cleans 8 bytes later at `0x0047a6d0`. |
| **`0x00487d70`** | `0x004efd30` | General node retirement, including recursively released children. EBX is node; EDX is node `+0x28`; EDI is render map. |
| **`0x00488efd`** | `0x004efd30` | Separate camera retirement path. ESI is camera; EDX is camera `+0x28`; EDI is render map. |

The last two occur after list unlinking but **before zeroing and freeing the node
storage**. General release chooses size `0x270` or `0x790` from the camera flag;
the camera-specific path clears/frees `0x790`. Hooking only the general destructor
would miss this separate camera path. A retirement observation can erase the
specific `(registry, pointer, handle)` entry before reuse, and invalidate the
camera's entire temporal history when its camera entry retires. A conservative
global epoch bump is simpler but could reset accumulation on ordinary unrelated
object churn; its cost must be measured rather than assumed negligible.

These callsites remain useful evidence, but the implementation plan below uses
the central helpers, including bulk map destruction. Observing only the load and
two retirement calls would leave insertion/replacement and bulk removal outside
the observer. The follow-up audit resolves the previously ambiguous maps used at
`0x00473672` and `0x004770d1`.

## Follow-up: central mutation and bulk-reset audit

The audit expanded to all 415 statically resolved references to the engine global
across 193 functions, with local targeted decompilation (all 193 completed), the
generic map helpers and their xrefs, and executable instruction searches for
render-node allocation sizes. This is static coverage of the reviewed executable,
not a proof over arbitrary pointer writes from unknown code.
The private expanded outputs are `/tmp/x3-lifetime-central.{txt,c}`,
`/tmp/x3-lifetime-map-helpers.{txt,c}`, `/tmp/x3-lifetime-bulk.{txt,c}` and
`/tmp/x3-render-registry-allrefs.c`. Reproduce the last set by passing the distinct
`function=` addresses from the `TARGET 00608518` section of the first xref output
to `X3DecompileFunctions.java`. Helper targets are `004efbf0`, `004efd30`,
`004efe10`, `004efeb0`, `004efb60`, `004efbc0`; owner-path targets are
`00469e80`, `004710f0`, `0046a520`, `004735c0`, `00476140`, `00472870`, `00475dd0`.

The previous ambiguous insertions are **recording/playback camera records**:

- `0x004735c0` reads `engine+0xa0` at `0x004735ce`; its `+0xc` map holds
  `0xe0`-byte camera snapshots. Insertion at `0x00473672` goes to that map,
  not `engine+0xc`.
- `0x00476140` obtains the same outer context at `0x00476151`, preserves it on
  its stack and uses its `+0xc` at `0x004770cc`. Insertion at `0x004770d1`
  creates a `0xe0` camera record. Immediately afterwards `0x004770d9` calls
  **the actual camera allocator `0x00488c70`**, whose live handle is retained
  in the record at `+0xd0`.
- Context cleanup `0x00472870` and `0x00475dd0` removes these separate records
  with the generic removal/destruction helpers. Playback deletion can also
  resolve and destroy the corresponding live node/camera through the already
  identified general/camera retirement paths. Do not use camera-record creation
  itself as a live-node birth.

The complete map-helper family in the examined code region is:

| Helper | Mutation / ABI | Temporal significance |
| --- | --- | --- |
| `0x004efb60` | Creates a 16-byte map; no explicit args, EAX map | Starts with no buckets, capacity 8, counter 1, count 0 |
| `0x004efbc0` | ESI map, ECX counter; normalizes then writes `map+8` | Counter change alone neither creates nor retires a node |
| `0x004efbf0` | EDI map, stack key/value; insert or replace | **Per-key lifetime version must change even if EAX is zero** |
| `0x004efcc0` | EAX map, stack value; chooses a free key then calls insertion | No separate entry mutation outside `0x004efbf0` |
| `0x004efd30` | EDI map, EDX key; removes key and frees hash entry | Retire matching key before forwarding; missing key may conservatively retire an observation |
| `0x004efda0` | EDI map, EDX current key; enumerates next key | Read-only |
| `0x004efe10` | Stack map; frees every entry, bucket array and map | **Bulk invalidation before the pointer can be reused** |
| `0x004efeb0` | ECX map, EAX new capacity; reallocates/rethreads buckets | Retains key/value pairs; allocation failure/unknown result must disable observation |

Render registry construction is explicit: `0x00469e80` calls the map creator and
stores its return at engine `+0xc`. Complete engine teardown `0x004710f0`
passes that map to **`0x004efe10` at `0x004712e1`** before freeing the engine.
This bulk helper does **not** call per-key deletion, so a deletion-only observer
misses whole-registry retirement. The examined helper family has no independent
in-place clear operation. No separate render-registry bucket/value writer was
identified in the engine-global reference audit; the inline lookup loops read
entries rather than mutating them.

The executable's identified allocations of literal `0x270`/`0x790` node sizes
are the three allocators `0x00486d10`, `0x004885a0`, `0x00488c70` and the shared
serialized reader `0x00479d10`. Each installs its node through the central insert
helper. Identified normal node frees go through general/camera destruction;
bulk engine destruction goes through the central map destructor. Registry sweep
`0x004872c0` calls the general destructor rather than erasing entries itself.
Together these close the previously identified normal birth/retirement/bulk
paths. A literal-size search alone would not establish that result; it is paired
with the allocation and map call-chain inspection.

## Smallest central observation plan

For the reviewed registry API, **three mutation boundaries** cover insert/replace,
individual removal and bulk destruction. They should replace the partial list
of individual lifecycle seams. Filter on the exact tracked render-map pointer,
while retaining the old map token through destruction; do not reinterpret other
maps as render nodes. Separately keep the explicit renderer-load epoch and
device/camera validity gates.

| Boundary | Exact displaced bytes | Resume | Reason this block is usable |
| --- | --- | --- | --- |
| Insert entry `0x004efbf0` | `55 8b 6c 24 08` (5 bytes) | `0x004efbf5` | Two complete instructions; no relative operand |
| Nonempty-map removal block `0x004efd39` | `8b 4f 04 83 e9 01` (6 bytes) | `0x004efd3f` | Two complete instructions; no relative operand; avoids relocating entry's short conditional branch |
| Map destruction entry `0x004efe10` | `53 8b 5c 24 08` (5 bytes) | `0x004efe15` | Two complete instructions; no relative operand |
| Optional conservative rehash entry `0x004efeb0` | `83 ec 08 53 55` (5 bytes) | `0x004efeb5` | Three complete instructions; no relative operand |

The removal boundary is an internal block: the original function has already
loaded the bucket-array pointer into EAX, tested it and taken its nonzero branch.
The empty-map return before this block cannot remove anything. EDI and EDX remain
the map/key inputs; preserve the live EAX and all other original machine state.
These fixed, fingerprinted blocks permit small explicit trampolines. They do not
justify an arbitrary instruction-copy detour utility. The original return paths
and stack layout must be tested, including the internal-block case.

A bounded observer can assign an independent serial to every observed
`(map_epoch, key)` insertion attempt, retire it before removal, and invalidate
all entries before map destruction. Store the value pointer as a consistency
check, never as the serial. Conservatively changing a serial on a failed insert
only discards history. Preserve the exact EAX result: insert returns zero both
for rejection and existing-key replacement, so result-based mutation detection
would be incorrect. A later draw must verify that the registry maps its handle
to its current node before using the observer's token.

Map operations can call allocation recovery code. Track a mutation-in-progress
scope and reject publication during nested/reentrant operations, clearing that
scope on foreign exception unwind. Overflow, missed hook ownership, unreadable
map metadata or observer-capacity exhaustion must make lifetime evidence unknown
and invalidate dependent history. Entries already alive at installation may be
adopted only under a new observer epoch after the complete hook set is active and
a complete quiescent baseline validates all entries; no lazy per-draw adoption or
pre-install history survives. Installing
or rolling back only part of the set must not leave observation enabled.

The optional rehash observer is conservative insurance: no lifetime serial needs
to change for a successful structural rehash, but in-flight map access should be
unavailable. It is only called from central insertion in the resolved xrefs, so
an insertion scope can also cover it. Counter-setter xrefs resolve to non-render
maps in the larger loader and do not bypass key insertion. No extra hook is
needed merely for the registry counter.

The remaining coverage limit is explicit: this audit found no bypass in the
reviewed normal renderer paths, but does not prove the absence of arbitrary
indirect/aliased writes or external modules mutating game structures. The observer
must expose that version-specific coverage, and a runtime consistency mismatch
must invalidate rather than invent a token. In-place camera cuts with a still-live
camera are a separate problem; central lifetime hooks cannot solve them.

## Existing signals that help, and signals that do not prove a lifetime

- Device lifetime ID, resource generation, Reset attempt/loss, failed Present,
  missing/nonadjacent successful frames, failed capture/context reads and scene
  boundary rejection are valid conservative history-invalidating events already
  available to the adapter. They do not by themselves identify every game load.
- A changed engine or registry pointer is cause to reset. Equality is not proof
  of continuity: the two statically resolved writes to global `0x00608518` are
  initialization in `0x00470d80` and its caller `0x00402780`; renderer loading
  mutates structures beneath the same engine. Sampling this pointer is not a
  reload detector.
- Camera pointer/handle, owning coordinate context, viewport/projection regime,
  and matrix validity must be checked. Camera storage lifetime is different from
  camera mode lifetime. In [iteration 0.4](iteration04-camera-motion.md), the
  user-reported third-person burst retains the same main camera pointer and
  handle as preceding gameplay bursts. No exact in-place camera-mode/cut setter
  or authoritative serial was established here.
- Large view/projection changes, elapsed-time gaps and focus/menu transitions can
  conservatively reject history. Numerical thresholds are renderer policy, not
  a proven engine cut event. Smooth motion and discontinuous teleports cannot be
  universally distinguished from two matrices alone.

## Implemented observer and bounded integration contract

Keep the history API's externally supplied epoch mandatory. Record its provenance
explicitly: a verified renderer-load observation, device/reset generation, manual
test epoch, or unavailable. Missing lifetime evidence must not become epoch zero
with implied stability. Publish history only after a complete successful frame;
require the immediately preceding eligible frame and reject any mid-frame epoch
change. Per-object matching still needs geometry revisions, shader/range checks,
unique submitted transforms and conservative treatment of unscoped effects.

The opt-in [observer](../../src/proxy/object_lifetime.cpp) implements the central
insert, nonempty removal and map-destruction boundaries plus the `0x0040508d`
load call. It gates on the full executable identity and six in-memory code-region
hashes, then checks the exact four patch sites. It performs no arbitrary prologue
decoding. All four wrappers forward the custom original ABI, preserve original
output GPRs/flags/x87/SSE state and LastError, and register a real x86 SEH scope.
The synthetic [verification](../verification/object-lifetime-observer.md) covers
normal, nested, foreign-unwind, corrupt-baseline and ownership-failure paths.

At quiescent installation, a complete bounded map snapshot may establish an
observer-start baseline for nodes and cameras already alive. Validation checks
power-of-two bucket count, declared entry count, key/bucket placement, nonzero
keys/values, node handle equality, unique keys and pointers, capacity, readable
chains, and a second pass over all headers, bucket heads and visited entries.
Partial validation publishes no entries. Future observed insertions can then
establish individual births; a draw-time lookup never adopts an unknown birth.
If the renderer does not yet exist, unrelated generic-map operations forward in
a dormant observer state. Losing a formerly bound registry clears evidence and
permanently disables that installation.

Each insertion retires its prior key before forwarding, including overwrite
with zero return; only normal completion and final map membership can publish a
new monotonically increasing serial. Removal retires before forwarding; bulk
destruction clears the map epoch before any storage release. Renderer loading
clears old evidence and advances the load epoch before forwarding, even if it
fails or unwinds. Nested mutation scopes deny snapshots until all relevant calls
complete. Foreign unwind clears all tokens and advances the load epoch. A
successful insertion's internal rehash requires no separate lifetime hook.

`current()` returns separate observer, renderer-load and registry epochs,
mutation revision, node serial and camera serial, with an explicit known/reason
result. No combined hash stands in for those fields. It checks live hook
ownership and final handle-to-pointer membership before publishing known facts.
Capacity exhaustion, counter exhaustion and lost hook ownership disable
observation. Failed membership reads retire the affected identities, so a later
read cannot silently revive their old serials.

### Registry table capacity (2026-09-29)

The observer's own table of live registry keys is a fixed open-addressing array
(`RegistryCapacity` in `object_lifetime.h`, linear probing). Run358 on the Mayhem 3
tree had the observer off from the first routed frame (all 618,169 routed draws
refused at motion gate 5, 0 `motion_lifetime` rows; stock run356 had 4,988), and the
reason was never logged. The shadow-retention row shows the observer's mutation
revision advancing 110,474 during load and then freezing, where stock run356 advanced
97,662 and kept moving (measured). The inferred cause is `capacity_exhausted` of the
former 16384-entry table: node handles reach about 49k on Mayhem 3 against about 29k
on the stock tree; the log cannot distinguish it from another `fail()` reason.

The capacity is now 262144 (2^18, user decision for larger mods): 44 B of static
storage per slot across `entries` and the two install-only baseline arrays,
11,534,336 B in .bss (was 720,896 B); the DLL's .bss is 21,852,456 B. Removal now
shifts later entries of the probe run back (Knuth's Algorithm R) instead of leaving
tombstones. Handles come from an up-only counter, so with tombstones every slot
became non-empty after about capacity distinct handles in one load epoch and each
insert then scanned the whole table twice under the spin lock; now the occupied
slots are always those of a fresh table holding the live keys. At 50,000 live keys
the replayed placement gives a longest hit/miss probe of 1/2 slots for sequential
handles, 1/5 for handles scattered over 1..200000, 9/13 for uniform 32-bit keys and
1/3 for a churned window (`verification/results/object-lifetime-capacity/probe_length.py`).

Every disable records one event under the observer's lock: reason, live and peak
live count read before the failing path clears the table, capacity, and the load
epoch, registry epoch and mutation revision as they were before that path's own
increments (the mutation's `enter()` has already counted it). The next Present logs
it once as the always-tier row `object_lifetime_disabled device= frame= reason=
reason_code= live= peak_live= capacity= load_epoch= registry_epoch=
mutation_revision= before_reinstall=`; `frame` is the logging frame, not the failing
call. An event not yet logged survives a reinstallation with `before_reinstall=1`
(a later disable is then not recorded until it is taken). Under `--debug`,
`object_lifetime_stats` every 300 frames carries `live`, `peak_live`, `max_probe`
(the longest `find()` probe since installation), `capacity`, `status`, `active` and
`load_epoch`; the install row gains `capacity=`. Fixture cases `registry_capacity`
and `registry_churn` are in the
[object-lifetime-observer ledger](../verification/object-lifetime-observer.md).

### Run382: registry growth, and the row that separates an engine leak from stale keys (2026-09-29)

Measured (`verification/results/run382-registry-growth/`, scripts beside their
outputs): on the Mayhem 3 tree the observer's live count grows by 27 keys per frame
from the sector load on (run365 frames 900..3300: 27.11 per frame, `series_out.txt`;
run382: 27.7 per frame until `object_lifetime_disabled reason=capacity_exhausted
live=262144` at frame 10,079). The shadow-retention `mutation_delta` has a floor of 27
per row on every Mayhem run (run365/368/375/379/382, median 27-29) against a mean of
0.2-0.3 on stock (run340/352/356), with `retired_sum=0`: inserts into the node registry
`R+0xc` with almost no removals (`stock_vs_mayhem_out.txt`, `series_out.txt`). Over the
same frames the `cutevent` interval (the scene-graph tick `0x0048f550`,
[script-task-scheduler.md](script-task-scheduler.md)) rises from 0.05 to 12.3 ms at
66-92 ns per estimated key (`cutevent_vs_registry_out.txt`; the per-key figure is a
model, inferred).

Two readings fit that series: the engine's own registry grows (nodes created and never
destroyed: an engine or mod leak, and the per-frame walk grows with it), or the
observer keeps keys the engine already removed (stale keys: a removal path the
observer does not see). The `scene_graph_census` row (`--perf`/`--debug`, every 300
frames, [logging-tiers.md](../architecture/logging-tiers.md)) is the test: it logs the
engine's own count `engine_nodes=[[R+0xc]+0xc]` next to `registry_live=` (the
observer's count). Both rising at 27 per frame is an engine leak; `engine_nodes` flat
while `registry_live` rises is stale keys. The same row names what and who:

* `unattached=` walks the list `R+0x28` of nodes attached to no scene **newest first**
  (tailpred `R+0x30`, prev links `+4`; the engine appends every new node there,
  `0x00486e28`-`0x00486e38`) and histograms the walked nodes' model id `+0x140`
  (`-1` = no model, set at creation `0x00486da9`) as `b0=<body>:<count>` .. `b7=`, the
  body resolved as the cull census does. The walk stops at 300,000 nodes (`bounded=1`),
  on a Brent cycle (`cycle=1`), on any refused read or a terminating slot other than
  `R+0x28` (`truncated=1`), and after 1.8 ms (`capped=1`, checked every 1,024 nodes).
  A capped `unattached=` is a lower bound over the newest nodes, which are the ones a
  leak adds.
* `i0=<site>/<caller>:<count>` .. `i7=` count the registry inserts since the previous
  row, captured at the observer's Insert hook on `0x004efbf0` (only while a tier is on,
  only for the bound registry `R+0xc`). `site` is the return address into the creator:
  when the hooked call returns to `0x004efd0e` (the auto-id register `0x004efcc0`, which
  pushed ebx/esi/edi and two arguments) it is `[esp0+24]`, else the hooked return
  address itself. `caller` is the creator's own return address for the frames read
  from the EXE: `0x00486d7d` (`0x00486d10`, five pushes + the node argument:
  `[frame+28]`), `0x0048860a` (`0x004885a0`) and `0x00488cdb` (`0x00488c70`, the
  0x790-byte node), three pushes + the argument: `[frame+20]`; `0x0047a6b2` (the direct
  insert of `0x00479d10`, an EBP frame from `0x00479d11` with no later EBP write before
  the call: `[ebp+4]`). Every other site logs `caller=00000000`. The stack reads go
  through `engine_memory::read`; the bytes behind these frames are pinned by
  `verification/analysis/test_scene_graph_census.py` against the installed EXE.

Costs (measured, X3 bottle, `verification/results/scene-graph-census-cpu.json`, nodes
as 0x270-byte process-heap blocks): a full walk of 200,000 nodes is memory-latency
bound: 18.4 ms as a bare pointer chase in a shuffled order, 11.2 ms in allocation
order, 18.8-41.1 ms through the validated reader (reading every node's model id
adds 0-27% over next links only across three runs, so every walked node is
histogrammed, `sampled=1`); the row therefore stops
at the budget and costs 1.81-1.89 ms (median of five rows), walking 36,864 (shuffled)
or 17,408 (allocation order) nodes. The insert-caller capture adds 37 ns per hooked
registry insert (object-lifetime fixture, 642 -> 679 ns) and resolves the auto-id path
in 29.2 ns (two reads); at ~275k inserts in a Mayhem load that is about 10-17 ms per
load (inferred), off unless `--perf` or `--debug`.

Every engine read of the observer (`read_registry`, `lookup`, the ownership
check, the baseline snapshot) goes through `src/proxy/engine_memory.h`
(2026-09-12): a span is validated against a cache of `VirtualQuery`'d
committed readable regions, re-validated on the first touch of each frame, then
copied with `rep movsb` in a translation unit built without SSE/MMX (the
in-mutation probe of the fixture compares the mutation's XMM state). The
`X3M_ENGINE_READS=rpm` `ReadProcessMemory` fallback was removed on 2026-09-22
(last commit carrying it: main `59ad2649`); direct reads are the only mode.
A registry page decommitted between frames yields `LookupUnavailable` and
retires the identities instead of faulting (fixture case: bucket array on a
`VirtualAlloc`'d page); the invalidation policy and its residual risk are in
[route-cost-run1.md](../verification/route-cost-run1.md#implemented-2026-09-12-items-13-of-the-ranking).
The fixture also proves both paths publish identical records (hashed) and
reports their per-call cost (`read_path` in the summary JSON).

Rollback preserves foreign code and keeps disabled forwarding targets whenever
ownership recovery is incomplete. Once production patches have been published,
successful shutdown retains the small forwarding trampolines until process exit
and refuses reinstallation. This permits a previously retained foreign chain to
forward without publishing lifetime evidence. Installation and removal still
require quiescence; no attempt is made to patch executing instructions safely.

Synthetic acceptance does not establish live load, travel or camera-switch
coverage. The next user-controlled capture must measure baseline known/unknown
counts, observed central mutations and epoch transitions alongside draw scopes.
The static coverage limits above remain explicit. In-place camera cuts need a
separate conservative history policy. No game was launched or observer installed
into a running game during this mechanism checkpoint.

### Run383: the 27-nodes-per-frame creator (2026-09-29)

**Answer.** The Mayhem 3 leak is the engine's **dust scene fill** `0x0041efc0`. Every
frame it tops the cockpit's dust scene up to the background's `NumDustInstances`. It
allocates each node first and checks the dust body afterwards. When the body cannot be
loaded it jumps past the attach and drops the pointer, so the node stays on the
unattached list `R+0x28` with model id −1. Nothing ever counts it, so the next frame
allocates the same number again. In run383 the sector's background is Mayhem record
103 (`litcube75`): `NumDustInstances` = 27, and none of its eight dust bodies exist.
That gives exactly 27 leaked nodes per frame. The frame-time growth in `cutevent` is
the scene-graph tick walking these nodes: `0x0048f550` calls `0x0048f2b0` for every
node on `R+0x28` (`0x0048f557`-`0x0048f572`).

Labels: **[m]** measured from the run383 log
(`/tmp/x3-bottleX3-run383/session-20260929-101037-212.log`, series in
[`census_series_out.txt`](../../verification/results/run383-dust-leak/census_series_out.txt)),
**[f]** read from the installed data files, **[i]** inferred from disassembly (Ghidra
12.1.3 decompiles and MinGW objdump of the same `X3AP.exe`, SHA-256 `fdbf3418…`; raw
output kept local).

**Measured [m].** Rows 3300 to 8100 each carry the pair `00486d7d/0041f332` at exactly
8,100 per 300 frames (17 of 17 rows). Over those frames `engine_nodes` and `registry_live`
both rise from 20,134 to 150,562. The newest unattached nodes are mostly `model-1`,
e.g. 4,923 of the 5,120 walked at frame 3300. The window that ends at frame 3000 has
7,479 of them, about 277 frames of the leak.
`volumetric_fog_sector` shows sector background `index=103` from frame 2724 on. At frame 8400,
after the game left the sector (`no_cockpit` at 8376), `engine_nodes` is back to 196.

**1. The creator [i].** Return address `0x0041f332` lies in `0x0041efc0` (1,691 bytes).
This routine is the worker behind the INS command `INS_UpdateDustScene`
([sector-fog.md §4](sector-fog.md#4-how-in-sector-fog-is-rendered) describes its fill logic).
Its ABI: ECX = sector object (cockpit `+0x54`); stack `arg1` = dust camera (cockpit
`+0x60`); `arg2` = sector camera (cockpit `+0x58`) or 0; `ret 8` (`0x0041f658`); the
frame is `sub esp,0x4c` plus four pushes. `rec = *0x00606fc0 + sector[+0x13c]*0xdb8 + 0x44`
is the TBackgrounds row. In the code's `rec` offsets: body ids `+0xb0[8]`, rates
`+0xd0[8]`, `NumDustInstances` `+0xf0`, stardust percent `+0x10c` (record `+0xf4`,
`+0x114`, `+0x134`, `+0x150` in sector-fog.md's numbering).

* The walk `0x0041f11d`-`0x0041f301` counts the dust scene's nodes (`[[arg1+0x1c]+8]`)
  that do not have `+0x12c` bit `0x4000000` against `NumDustInstances`.
* The loop `0x0041f328`-`0x0041f4d6` runs `NumDustInstances − count` times:
  * `call 0x00486d10` at `0x0041f32d` (return `0x0041f332`); this reaches Insert at
    `0x00486d78` → `0x004efcc0` → `0x004efd09` → `0x004efbf0`.
  * It writes `+0x30/+0x34/+0x38` = one of 16 offsets from `0x0057adf0` scaled by 8.0,
    then draws a body id from the weighted table.
  * If the body is valid: `0x00487e30` (bind body: `+0x140`, `+0x70`, `+0x144`, `+0x20`),
    `0x004880e0`, `+0x12c |= 0x10000000`, `+0x20 = 0`, random roll into `+0x40`, and
    `0x00489da0` attaches the node to the dust scene `[arg1+0x1c]`. The attach unlinks
    the node from `R+0x28` and sets `+0x18` = 0 and `+0x1c` = scene.
  * Three branches skip all of that and go to `0x0041f4d1`:
    * `0x0041f3d1` `jl`: the id is negative;
    * `0x0041f436` `jne`: for an id below 100,000, the body slot's load-failed flag
      `[[R+0xbc] + slot*0x1c + 0x10]` is set
      ([body-format-bob1.md](body-format-bob1.md), slot table);
    * `0x0041f447` `je`: the body load `0x004863c0(id)` returns 0.

  EDI (the node) is dead after `0x0041f4d1`; the next iteration overwrites it at
  `0x0041f332`.
* What the leaked node holds is only what `0x00486d10` wrote: a zeroed 0x270-byte block
  (`memset` `0x00486d5f`), id `+0x28`, child list `+0xc`, `+0x140/+0x144/+0x148/+0xa4`
  = −1, `+0x130/+0xa0/+0x70/+0x14c/+0x1d8` = 0, identity `+0x40` copied to `+0xc0`,
  scales `+0x80..+0x88` and `+0x1c8..+0x1d0` = 1.0 (16.16). It is appended to `R+0x28`
  (`0x00486e28`-`0x00486e38`). The model id is −1 because the bind never runs.
* The first failure sets the slot's load-failed flag. `0x004863c0` calls `0x0046e100` at
  `0x00486631`/`0x00486819`/`0x00486853`. So later frames take the cheap `0x0041f436`
  exit with no file access [i]. The per-frame cost is 27 × (`malloc` + `memset` + registry
  insert), plus the growing tick walk.

**Why record 103 fails [f].**
[`dust_bodies.py`](../../verification/results/run383-dust-leak/dust_bodies.py) reads
`types/TBackgrounds` as the engine loads it and checks
`objects/environments/nebulae/<fam>/nebula_<fam>_dust_partNN` in all four body formats
for every part whose rate is above zero. The name comes from the loader at
`0x00436e6a`-`0x00436ee2`: `"_dust_part"` + `"%02d"`, registered through `0x0046e400`.
* Installed (Mayhem) tree: the loose `addon/types/TBackgrounds.txt`, 248 rows.
  170 rows have `NumDustInstances` > 0 and no dust body at all, and 103 `litcube75` is
  one of them (`dust=27`, rates 1 × 8)
  ([out](../../verification/results/run383-dust-leak/dust_bodies_installed_out.txt)).
* Stock (`STOCK_AP_CATALOGUES`): one such row, 45 `xtmgreenring`.
  Six other rows lack parts 7/8 at a rate above 0 but have parts 1-6. They leak only
  while the scene fills, because valid picks attach and end the top-up
  ([out](../../verification/results/run383-dust-leak/dust_bodies_stock_out.txt)).

Any sector on one of the 170 Mayhem backgrounds leaks `NumDustInstances` nodes per
frame, i.e. between 17 and 38 in the rows listed (inferred from the same rule; only
record 103 was flown).

**2. Call chain [i]: a main-loop routine, not the script interpreter.** The chain is:
main loop `0x00403840` → `0x00403f2f call 0x0041cde0`. That call comes right after the
object update `0x00416750` and before the frame routine `0x00471f50`. The main-loop
edges at `0x004038c4`, `0x00403aa3`, `0x00403c6c` and `0x00403dbf` jump past it; they
were not decoded here. From there:
* `0x0041cde0` runs `0x004205e0` for every cockpit in `*0x00608504`
  (`0x0041ce3e`);
* `0x004205e0`, the per-frame cockpit update, calls `0x00421698 call 0x0041efc0` when
  `cockpit+0xc != 0` and the dust camera `cockpit+0x60 != 0`;
* `0x0041efc0` then calls `0x0041f32d call 0x00486d10`.

The only other caller is the INS dispatcher `0x0042d340`, case `0x47` at `0x0042ebe0`.
The name table `0x0057aef0` gives entry `0x47` = `INS_UpdateDustScene` (`0x00556538`) and
`0x46` = `INS_InitDustScene` (worker `0x0041ee60`). The dispatcher is registered as the
group `[VM+0x2c]` handler by `0x0041c8f0`. The census logs only the creator's caller,
so it does not say which of the two ran. Exactly 8,100 per 300 frames fits one
cockpit-loop call per frame. One Insert-time read settles it: the return address of
`0x0041efc0` sits 0x60 bytes above the `0x0041f332` slot (`[esp+0x5c]` of its frame at
`0x0041f32d`). It is `0x0042169d` on the cockpit path and `0x0042ebe5` on the script
path.

**3. No matching destroy [i].** Node release `0x00487be0` (cdecl, node on the stack, `ret`
at `0x00487e23`) is the only per-node path that removes from `R+0xc` (`0x00487d70`).
Nothing calls it for these nodes:
* `0x0041efc0` keeps no reference to a node after a failed check;
* the leaked nodes are on no scene or parent list, so no scene teardown reaches them
  (inferred; the dust scene's own destroy was not decoded);
* the per-frame tick visits `R+0x28` but never releases: no call to `0x00487be0` lies in
  `0x0048f2b0`-`0x0048f69f`.

Only the render-manager clear `0x00470f50` frees them. It loops
`while (*[R+0x28]) 0x00487be0([R+0x28])` at `0x00471047`-`0x00471063`, and it is reached
only through the game-state teardown `0x00497190` (`0x004971ce`; callers `0x00401eab`,
`0x00403287`, `0x0040395e`, `0x00404242`, `0x00497162`), i.e. on a load, quit or exit.
That fits the drop to 196 at frame 8400 [m]. This is a creator bug (allocate before
validate), not an unreleased flag or refcount. The B3D save writer `0x00479010` (chunk tag
`0x00561134` `"B3D "`) also counts every `R+0x28` node without `+0x130` bit `0x400`;
the loader reads that set back as the `INST` records (`0x0056113c` `"INST"`, see 4). A leaked node has `+0x130 = 0`, so
leaked nodes present at save time are written to the save [i]. Whether a real save
carries them was not checked.

**4. The load burst `0x0047a6b2`/`0x0047a87a` [i]: a different creator.** `0x0047a87a` is
the return from `0x0047a875 call 0x00479d10`. This is in the renderer-scene loader
`0x0047a720` (the Load hook's callee from `0x0040508d`), in the loop over the `INST`
count read after the `"INST"` tag compare at `0x0047a861`. `0x00479d10` rebuilds one
saved node and inserts it under its saved handle (`0x0047a6ad`). The walk from
`0x0047aa86` then moves each restored node from `R+0x28` to its saved parent
(`+0x18`) or scene (`+0x1c`). So the 115,024 inserts are the savegame's restored node
set, created once per load. The window that holds them (173,601 inserts) ends
with 11,994 nodes in the registry [m], so most of them had left the registry by then.
Which path removed them was not traced. Restored nodes with neither a parent nor a scene stay on `R+0x28`
[i]. Whether this save's `INST` set includes dust nodes leaked before the save is open
(see 3).

**5. Recognising them at Insert time [i]: not possible from the node.** At the Insert
call (`0x00486d78`) the block is all zero: the id `+0x28` and the model id −1 are
written only after Insert returns (`0x00486d88`, `0x00486da9`). Nodes have no vtable,
and the leaked and the kept dust nodes come from the same site. What the proxy can
test at Insert time:
* the stack pair `0x00486d7d` / `0x0041f332`, which the census already reads;
* the model id the creator will draw is not visible yet. Only the background row
  (`NumDustInstances` > 0 with every rated body's load-failed flag set) predicts the
  failure.

After `0x0041efc0` returns, a leaked node looks like this:
* `+0x140 == -1`, `+0x1c == 0`, `+0x18 == 0`;
* `+0x12c` without `0x10000000` (0 in practice);
* `+0x30..+0x38` = one of the 16 dust offsets;
* still on `R+0x28`.

A kept dust node has `+0x140` = a body id, `+0x1c` = the dust scene and `+0x12c` bit
`0x10000000`. Keeping these nodes out of the observer's table would only save
observer capacity. The engine's `R+0xc` map, the `R+0x28` list and the `0x0048f550`
walk grow either way. Stopping the growth needs either the data or the engine to change:
* **Data:** `NumDustInstances` = 0, or dust bodies for the 170 rows.
* **Engine:** release the node on the three failure edges. At `0x0041f4d1` EDI still
  holds it. `0x00487be0` is cdecl and preserves EBX/ESI/EDI/EBP. EBP carries the
  previously drawn id into the next iteration and must survive; flags are dead because
  `0x0041f4d1` is a `sub`.

The engine change is the `dust_leak_fix` patch ("Patch" below); the data change is the mod's.

```sh
S=<scratch>; cp "$X3/X3AP.exe" $S/                      # Ghidra project in scratch, import once
analyzeHeadless $S X3Render -import $S/X3AP.exe -scriptPath tools/analysis \
  -postScript X3FunctionContext.java $S/ctx.txt 0041f332 00421698 0042ebe0 0041ce3e 0047a87a
analyzeHeadless $S X3Render -process X3AP.exe -noanalysis -readOnly -scriptPath tools/analysis \
  -postScript X3DecompileFunctions.java $S/dec.c 0041efc0 004205e0 0041cde0 00486d10 00470f50 00479010 0047a720
PYTHONPATH=tools/analysis python3 verification/results/run383-dust-leak/dust_bodies.py "$X3" [--stock]
python3 verification/results/run383-dust-leak/census_series.py <session.log>
```

#### Patch: `dust_leak_fix` (2026-09-29)

`src/proxy/dust_leak_fix.cpp` with `src/proxy/dust_leak_fix_sites.h` (config key `dust_leak_fix`,
launcher `--dust-leak-fix on|off`, `X3M_DUST_LEAK_FIX`, default on; ledger
[dust-leak-fix.md](../verification/dust-leak-fix.md)). One `engine_patch` claim of the loop's common
tail and a 45-byte stub; the required properties are (a) a node whose body failed is released the
way the engine releases unattached nodes and (b) the fill stops for that frame after the first
failure. Everything below is read from the MinGW objdump of the installed EXE (SHA-256 `fdbf3418…`)
and checked by `verification/probe/verify_dust_leak_fix_site.py` (27 checks) unless marked
otherwise.

**1. The release routine `0x00487be0` is the right one, and a never-bound node satisfies it.** It
is cdecl (node at `[ebp+8]`; `push ebp; mov ebp,esp; and esp,-8; push ecx; push ebx; push esi;
push edi` … `pop edi; pop esi; pop ebx; mov esp,ebp; pop ebp; ret`, the epilogue from `0x00487e1d`
with the `ret` at `0x00487e23`), so EBX/ESI/
EDI/EBP survive and EAX/ECX/EDX do not. Per node it: frees `+0x1ac` when `+0x1a8`/`+0x1ac` are
set; runs `0x004c5330` when `+0x170` is set; runs the `+0x250` callback when `+0x1f4` bit `0x40` is
set; releases every child on the `+0xc` list recursively (`0x00487c90`); detaches from the scene
`0x00489e90` when `+0x1c` is set; frees `+0x1b8` when `+0x130` bit `0x20` is set; scans the
render manager's `R+0x18` lists for references to the node (`0x00487cef`-`0x00487d53`, free through
`0x00486af0`); **unlinks the node from whatever list it is on** (`0x00487d55`: `[prev]=next;
[next+4]=prev`, node `+0` next, `+4` prev); **Removes it from `R+0xc` by its id `+0x28`**
(`0x00487d70 call 0x004efd30`, the map entry that continues at `0x004efd39`); frees the two strings
`+0x20`/`+0x24`; then `memset(node, 0, 0x270)` (or `0x790` when `+0x12c` bit 0) and `free`
(`0x00518de0`, `0x0050e1b0`). The leaked node has every optional field at 0 from the constructor's
`memset` (`0x00486d5f`), an empty child list (`+0xc` → `+0x10` = 0, `0x00486d8b`-`0x00486d97`),
`+0x1c` = 0 and `+0/+4` linking it into `R+0x28` (`0x00486e28`-`0x00486e38`), so it takes none of the
optional branches and needs nothing it lacks. It does not call the Destroy `0x004efe10` (that is
the manager's table teardown, not per node). It is exactly what the render-manager clear
`0x00470f50` runs per `R+0x28` node (`0x00471050 push esi; call 0x00487be0`), i.e. the path that
frees these nodes today on a load or quit. Its `free` does not read LastError, and the engine's
own iteration already called `malloc`/`memset` (`0x00486d1b`, `0x00486d5f`) before the tail, so
LastError is not live at the site (inferred; no LastError reader between the site and the next
`SetLastError` was looked for beyond the fill function).

**2. Site and displaced bytes.** Site `0x0041f4d1` `83 6c 24 20 01` = `SUB dword [ESP+0x20],1`, one
whole five-byte instruction, no relative branch, the first five bytes of the aligned qword
`0x0041f4d0` (one `lock cmpxchg8b`); `SiteSpec {"dust_fill_failed_body", 0x0041f4d1, 5, ret_pop 8,
rel32_offset 0}`. The 37-byte window `0x0041f4b7`-`0x0041f4db` (`lea esi,[edi+0x40]; call 0x004f0270;
mov eax,[esp+0x6c]; mov ecx,[eax+0x1c]; add esp,0xc; push ecx; mov eax,edi; call 0x00489da0`
(attach); the SUB; `jne 0x0041f328`) is compared before the claim. State at the site: EDI = the
node (the three failure branches `0x0041f3d1` jl, `0x0041f436` jne, `0x0041f447` je) or 0
(`0x0041f336` je after a failed allocation) or an attached node (fall-through from the attach; the
attach `0x00489da0` pushes ecx/ebx/esi/edi and pops them, writes `+0x18` = 0 at `0x00489dbe` and
`+0x1c` = scene at `0x00489de5`); EBP = the drawn body id (or the previous iteration's; read by
`0x0041f3cc test ebp,ebp` next iteration, so it must survive); ESI = a scratch pointer; EBX = 0 after
the offset loop; ESP = the function's frame (`sub esp,0x4c` + `push ebx/ebp/esi/edi`), with
`[ESP+0x20]` = nodes still to fill, `[ESP+0x18]` = the pick index; flags = whatever the branch left
(dead: the SUB rewrites them and the JNE reads the SUB's). The site is the target of exactly those
four branches and of nothing else in the image (no other direct branch, no raw rel8/rel32 encoding
in `.text`, no dword reference into the window). The stub (`dust_leak_fix_sites.h`): `test edi,edi;
je done; cmp dword [edi+0x1c],0; jne done; push eax/ecx/edx; push edi; call 0x00487be0; add esp,4;
pop edx/ecx/eax; mov dword [esp+0x20],1; inc dword [hits]; done: jmp [slot]` → tail (`SUB; jmp
0x0041f4d6`). Only a never-attached node (EDI ≠ 0, `+0x1c` = 0) is released; the counter 1 minus
the displaced SUB's 1 = 0 makes the JNE fall through. Every register but the dead flags is as the
engine left it (EAX/ECX/EDX by the pops, the rest callee-saved, ESP balanced), the FPU/SSE state is
untouched (the release path for such a node runs no x87/SSE code, inferred from its callees), and
no pointer into the DLL exists in the stub (the counter word lives in the arena after the slot).

**3. The loop exit is the function's own.** After the JNE falls through, `0x0041f4dc mov
ecx,[esp+0x64]` onwards writes EAX/ECX/EDX/EBX/ESI/EDI/EBP before reading them (`0x0041f4dc`-
`0x0041f566`), and between `0x0041f4dc` and `ret 8` at `0x0041f658` the only ESP-relative accesses
are `[esp+0x64]`, `[esp+0x60]`, `[esp+0x3c]`, `[esp+0x68]`, `[esp+0x40]` and `[esp]` (the verifier's
`tail_esp_offsets`), none an alias of `[ESP+0x20]` at any push depth (at most five deep before a
call), so the counter left at 0 is never read again; the epilogue `pop edi; pop esi; pop ebp; pop
ebx; add esp,0x4c; ret 8` (`0x0041f649`-`0x0041f658`) is unchanged and the stack it pops is the
prologue's. No jump back into the loop from the tail is needed: the exit is reached through the
engine's own JNE, so one site suffices.

**Cost.** Per frame the proxy runs nothing; the stub adds two compares and one indirect jump per
loop iteration (`NumDustInstances` per frame at most), and on a failure one release (what the
engine's clear does per node) instead of a leaked 0x270-byte block, plus the fill stops. Under
`--perf`/`--debug` one `dust_leak_fix hits=` row per 300 frames reads the counter. In a sector on
background 103 the expected row is `hits=300` per 300 frames (one failed attempt per frame) and
`engine_nodes` flat (inferred from the Run383 evidence; not flown yet).

## Exit-time engine read fault (2026-09-24)

**Finding.** Both Run 77 sessions (run287/run288, DLL `268db207`) faulted at exit
with a read of `0x03B88794` at `0x76B137FB`, the `rep movsb` of
`engine_memory::read` (measured: `verification/results/run288-exit-crash/`).
`0x03B88794` is the logged engine object `0x03b88788` + `0xC`, the registry slot
that `read_registry` (this observer) and `object_trace::current` read. The fault
came on the render thread 0.960 s and 0.912 s after the last logged row
(measured), before the device-teardown summaries. Complete engine teardown
`0x004710f0` destroys the registry and then frees the engine object (static
finding above), so later generic-map mutations still reach `read_registry` with
the stale engine pointer in the image slot. Cause (inferred, reproduced on the
host): the reader trusted a cached region for its frame and 100 ms of a tick it
refreshed only on every 64th read; after the last Present no frame advances and
fewer than 64 reads arrive, so the freed, decommitted block passed the cache
without a fresh `VirtualQuery`. Code unchanged since Run 76; the larger image
moved the heap layout so the freed page now decommits (inferred).

**Fix** (`src/proxy/engine_memory.{h,cpp}`):

- `GetTickCount` is sampled on every read. Every age bound uses a signed,
  wrap-safe difference, so a tick sampled before another thread's stamp does
  not count as a stall.
- **Three trust modes.**
  - *Frame:* the last `next_frame()` is at most 250 ms old. A region is
    trusted for its epoch and 100 ms, as before.
  - *Stalled:* the last `next_frame()` is more than 250 ms old (a load, the
    exit). A region is trusted only for 5 ms after its own `VirtualQuery`.
    `GetTickCount` moves in steps (about 15.6 ms on Windows), so the
    effective window is up to one step.
  - *Shutdown:* every read re-validates its whole span (`MEM_COMMIT`,
    readable, not guard/no-access) and is refused when any part fails.
  - A per-read query in the stalled mode would cost too much. run287's save
    load (22.65 s) made 2,848,344 reads in its summary intervals from 0.69 s
    to 22.60 s, of which 2,527,382 fell in one 6.25 s window with one epoch advance and no Present (measured:
    `run288-exit-crash/load_read_counts.{py,txt}`). At the 0.55 µs per query
    measured below that is about 1.4 s (inferred).
- **Epoch API split.** `next_frame()` is a Present: it advances the epoch,
  restarts the stall bound and ends the shutdown signal. Only the motion
  route's per-Present `begin_frame` (and fixtures) call it.
  - `revalidate()` advances the epoch only. `sector_background_context` (at
    BeginScene), `fog_prefill_poll` (every 250 ms inside a stall) and the cull
    census now call it, so they no longer switch cache trust back on during a
    load or drop the signal.
  - Without the motion route there is no Present signal. The reader then
    keeps the 100 ms region age alone, the pre-fix bound with a per-read tick.
- **`begin_shutdown(source)`.** The observer raises it on `Destroy` of the
  bound registry (`registry_destroy`); the next `next_frame()` ends it. At
  exit no Present follows the teardown. The fixture's destroy-then-reinsert
  case shows that a registry can be rebuilt in one process, so the signal is
  not process-sticky.
- `query()` preserves LastError across `VirtualQuery`.
- **Readers.** They already tolerate `false`: `read_registry` fails, the
  observer clears evidence and disables itself (`registry_unavailable`), and
  `object_trace::current` leaves the `Registry` bit clear. Neither logs per
  read.
- **Summary row.** `engine_memory_read_refused reason=shutdown|stalled|
  uncommitted|none count= shutdown= stalled= stalled_reads= strict_reads=
  signals= signal=` carries cumulative counts.
  - It is written each time the last live device is destroyed
    (`capture.cpp`): normally once, at the game's teardown, not at
    `DllMain`'s detach, which must not log.
  - Reads after that destruction are not counted in any row.

**Evidence.**

- Host test `verification/analysis/test_engine_memory_shutdown.py` compiles
  the production TU against a mock `VirtualQuery`/tick: 7 scenarios and 74
  checks PASS (measured). It covers the exit shape, the per-read tick, the
  hot path, 5 ms stalled trust, `revalidate()` leaving the stall bound and
  signal alone, and the shutdown signal.
- On the pre-fix reader the same driver fails 3 of 11 baseline checks,
  including the decommitted block 0.9 s after the last Present (measured:
  `run288-exit-crash/engine_memory_stale_cache.{py,txt}`).
- Hot path, host: 1,000 advancing frames × 48 reads over 3 regions issue
  exactly 3,000 `VirtualQuery` calls, with 0 stalled or strict reads
  (measured).
- Object-lifetime fixture under X3: PASS, 664 checks. Its new `READ_MODES`
  row times the hooked insert+remove cycle (about 15 reads) per mode
  (measured):
  - Frame: 1.365 µs, 0.0002 queries per cycle.
  - Stalled: 1.367 µs, 0.0007 queries per cycle.
  - Shutdown: 9.646 µs, 15 queries per cycle, about 0.55 µs per query.
- The fixture's cost passes now each start from a Present. Journal cycle
  costs (idle / journal, µs, measured):

  | Run | Idle | Journal | Note |
  | --- | --- | --- | --- |
  | 2026-09-22 record | 1.267 | 1.250 | pre-fix reader |
  | First post-fix run | 9.17 | 9.11 | inflated: the destroy case's signal stayed raised |
  | Now | 1.359 | 1.362 | per-read tick included |

- Read-path `snapshot_us` (12 reads) was 0.741–0.755 before and 0.792–0.801
  after, alternating three runs each, about +3.8 ns per read for the tick
  (measured: `run288-exit-crash/engine_memory_read_path_ab.{py,txt}`).
  run287 averaged about 2,745 reads per frame, which is about 11 µs per frame
  (inferred).
- Cull-census fixture: 101 checks, 0 failures (measured).
- Scratch build: 0 warnings. `check_no_x87` PASS (673 reachable functions).
  `engine_memory.cpp.obj` has 0 `%xmm`/`%mm`/`%st` references (measured).

**Limits.**

- A read that races a free on another thread between its query and its copy
  can still fault. In the stalled mode that window is 5 ms, or one tick step.
- That the exit-time reads run on the thread that frees the engine, which
  would close the observed shape, is inferred from the faulting thread being
  the render thread. It is not verified.
- Verified in the game 2026-09-24 (Run 78 A, run295-298 on the Run78 DLL d4ba9f05…): four menu exits, no fault
  (the Run77 fault reproduced in both run287 and run288). The `engine_memory_read_refused` summary row did not appear:
  it is written only when the game's last device release passes through our hook (`capture.cpp:1065-1068`), and the
  game exits without that final release (run287 showed the same; inferred from the last rows being the teardown
  summaries). The expectation "one refused row per exit" is withdrawn; the acceptance is the absence of the fault
  ([triage](../../verification/results/run295-298-run78a/exit_rows_out.txt)).


Second-review limits (2026-09-24): the shutdown signal is raised only while the lifetime observer is live and
`read_registry` succeeds at the Destroy hook; if observation was disabled earlier by a `fail()` (capacity, counter,
registry unavailable) or the engine slot is already unreadable there, exit protection falls back to the stalled window
(one tick step). `next_frame()` clears the signal unconditionally, so if engine teardown ever ran off the render
thread with a Present between the registry destroy and the engine free, 100 ms cache trust would return before the
free; the observed exit shape (no Present after teardown) is covered. Both inferred from the code, not observed.
