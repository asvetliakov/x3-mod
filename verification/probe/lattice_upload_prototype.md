# Final CloneMesh raw staging prototype

First checkpoint: portable CPU core only. There is no ownership-wrapper
integration, CloneMesh adapter, EXE hook, F8 integration or native fixture yet.
This checkpoint cannot produce a game snapshot. Native COM and SEH acceptance
are explicitly pending.

The parent ratified opaque staging from the producer's original pre-Unlock
mapping. Authenticated readable access is separate from initialized geometry:
the observed D3DX INDEX16 path can fail a private attribute allocation after
both Lock calls succeed but before any index stores. Raw bytes may be copied
privately in that interval. They may not be interpreted, hashed, logged,
serialized or exposed until successful CloneMesh, both destination Unlocks
returning exactly S_OK, both observed source closures, and final public buffer
identities/revisions agree. A failed scope wipes all touched arena ranges with
volatile stores. There are no added Lock/Unlock/draw calls in the selected design.

## CPU core contract

`src/ownership/clone_upload_core.h` owns a fixed 2 MiB arena and two records for
the selected `(vertices,faces,stride)` tuples `(9680,3784,40)` and
`(1267,940,40)`. Their VB+INDEX16 sizes are 409904 and 56320 bytes, total 466224.
The core performs no allocation, COM operation, file I/O, logging or payload
interpretation. Construct it once outside mapping callbacks. All operations
require the ownership registry mutex, including reads of its metadata.

The future adapter must supply facts, not optimistic booleans:

1. Public source counts/declaration/options select a tuple and refuse SHAREVB
   and INDEX32. Authenticate the passed ownership device, generation, and
   source allocation IDs/revisions before the original invocation. Actual
   source wrapper observation is mandatory; a bypass cannot set source success.
2. Only fresh allocations created by that invocation may call `created`.
   Immutable unique allocation IDs come from ownership metadata. Actual native
   descriptor/creation facts establish readable MANAGED backing and the exact
   size. Requested Usage is stored separately. Native WRITEONLY/DYNAMIC,
   wrong sizes, reused/source identities and INDEX32 refuse. Readable creation
   must be decoupled from the existing typed finite scanner before integration.
3. Observe the original full destination `(offset=0,size=0,flags=0x800)` Lock,
   successful source `(0,0,0x810)` Lock, and both original source/destination
   Unlock results. Destination revision must be the fresh allocation's first
   write. Any unsupported/reentrant/foreign event must latch `refuse`, even if
   a later callback restores the apparently expected state.
4. Before `stage`, complete all public descriptor/private-data/identity
   qualifiers and releases. Under the registry mutex, independently rebuild
   the entire CPU guard from live ownership state: invocation/latch, owner,
   generation, thread, actual map pointer, original Lock serial, allocation and
   revision, one pending map, one current Unlock, zero in-flight Locks,
   unchanged dispatch and no reset/loss/retirement/tracking ambiguity. The
   adapter must not call COM while holding this final section. `stage` rechecks
   these values, issues a sequentially consistent fence and memcpy, then
   returns. No callback occurs inside it. The potential touched range is
   recorded before memcpy so scope cleanup can erase an interrupted copy.
5. Restore the application's original state, dispatch original Unlock with
   unchanged arguments, preserve outgoing state, then call `unlocked` only
   with observed exact S_OK and authenticated closed metadata. CloneMesh's
   HRESULT does not substitute for this: D3DX ignores those Unlock results.
6. After original CloneMesh success, acquire final public GetVB/GetIB identities,
   finish/release every qualifier, then perform a fresh CPU-only final guard
   under the registry mutex. Only `finish` can set `producer_payload_valid`.
   Do not hand off scope output before this check. On exception/unwind, the
   adapter must call idempotent `abort` before a successor scope can start.
7. Wrapped Reset and retirement invalidate under the same registry mutex;
   supported native mutations and any later writes invalidate retained IDs.
   `copy_retained` additionally requires current final binding/revision facts.
   Duplicate selected records are wiped and remain refused until explicit
   reset/disarm; no first-object or third-object selection is inferred.

The core does not prove these inputs came from COM, hold resource references,
create an exclusive producer interval, recognize unknown native mutation, or
preserve x87/MXCSR/LastError around a native call. Those are mandatory adapter
responsibilities. The core's mutex requirement alone is not a claim that
current ownership callback qualifiers are already safe. Producer ownership is
the separately reviewed RE interval; no private DLL layout or hash is a runtime
prerequisite. Native Windows compatibility remains source intent, not execution
evidence. `producer_payload_valid` never asserts simultaneous draw-input or
texture coherence, Run177 identity, or a visible crawl correction.

## Host acceptance

```
python3 -B verification/probe/lattice_upload_host_test.py
```

The script builds only a temporary host executable (`-std=c++17 -O2 -Wall
-Wextra -Werror -pthread`) and removes it afterward. The fixed 39 cases cover
both exact payload sizes and output canaries, no exposure before finish,
independence from later source storage changes, observed/unobserved source
Lock failures, incomplete private IB bytes followed by Clone failure/erasure,
actual WRITEONLY/DYNAMIC/non-MANAGED refusal, INDEX32 refusal, stale identity,
revision, generation, pointer, Lock serial, thread and dispatch, reentry,
failed Unlock despite Clone success, nested scopes, duplicate selectors,
stale/idempotent abort and a parked reset thread behind the external registry
mutex through the stage copy. The reset control is a host CPU mutex model;
it is not a real D3D Reset or creation-thread test. The OOM case models the
reviewed branch and does not inject D3DX allocator failure. No SEH, COM,
readable-creation rollback or native CPU-state preservation is tested here.

Cost: one preallocated 2 MiB arena plus bounded metadata; no per-map allocation
in the core. Success performs one O(bytes) memcpy per destination. Failure and
invalidation erase touched ranges, which include the unfilled VB gap if the IB
was staged first. That gap is internal zeroed CPU storage, not an extra native
mapping read. No per-draw payload operation is implemented. Host duration is
diagnostic runtime, not game FPS or loading impact.

## Pending adapter acceptance

Before live use, real public D3DXCloneMesh through the ownership wrapper must
establish exact bytes/linkage, original call counts/arguments/HRESULTs,
readable creation fallback, observed source and ignored destination failures,
qualifier reentry, Reset exclusion, invalidation and lifetime cleanup. A tested
x86 boundary must preserve incoming/outgoing x87 payload/environment, MXCSR
and LastError on ordinary return and correctly discard/propagate native SEH
and C++ unwind. A MinGW catch-all alone is not that proof. Parent owns emitted
code inspection, cross-compilation and the single Wine queue. No game hook or
production acceptance precedes those gates.

The considered post-success extra READONLY mapping route remains unselected
because its complete Reset/reentrant mapping-cleanup lifetime contract was not
established. This does not assert that resource Unlock is universally illegal
after failed Reset; public Lost Devices locking guarantees qualify that claim.
