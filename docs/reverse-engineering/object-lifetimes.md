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
- Not verified in the game. The next Run 77-style exit should show no fault
  and one `engine_memory_read_refused` row. The next save load should show a
  load time unchanged from run287's 22.6 s.


Second-review limits (2026-09-24): the shutdown signal is raised only while the lifetime observer is live and
`read_registry` succeeds at the Destroy hook; if observation was disabled earlier by a `fail()` (capacity, counter,
registry unavailable) or the engine slot is already unreadable there, exit protection falls back to the stalled window
(one tick step). `next_frame()` clears the signal unconditionally, so if engine teardown ever ran off the render
thread with a Present between the registry destroy and the engine free, 100 ms cache trust would return before the
free; the observed exit shape (no Present after teardown) is covered. Both inferred from the code, not observed.
