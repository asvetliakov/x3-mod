# Final CloneMesh raw staging prototype

The portable CPU core, readable-creation prerequisite and isolated x86 ABI
boundary are accepted separate checkpoints. A manually armed actual public
CloneMesh observer and focused fixture now pass the parent-owned full build,
X3 runtime and independent combined review. No EXE hook, F8 route or CLI is present,
and these sources are not installed in the game. Native Windows execution is
unverified.

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

The core requires these authenticated inputs from the adapter:

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

## Manual checkpoint boundary

The actual public D3DXCloneMesh fixture establishes exact bytes/linkage,
original Lock/Unlock counts, readable fallback, observed source and ignored
destination failures, qualifier reentry, Reset exclusion, invalidation and
lifetime cleanup for the selected route. The separately accepted x86 boundary
preserves computational state and propagates supported native/C++ unwind;
its synthetic tests do not certify arbitrary inherited-wrapper SEH recovery.
Parent owns emitted-code inspection, cross-compilation and the single Wine
queue. Game-callsite patching and F8 association remain later acceptance gates.

The considered post-success extra READONLY mapping route remains unselected
because its complete Reset/reentrant mapping-cleanup lifetime contract was not
established. This does not assert that resource Unlock is universally illegal
after failed Reset; public Lost Devices locking guarantees qualify that claim.

## Readable-creation prerequisite (accepted separate checkpoint)

`Options::prepare_readable_managed_uploads` is off by default and has no CLI
binding. It requires write tracking and rejects simultaneous finite-position
or locked-prefix scanning. It reuses allocation-owned immutable requested Usage
metadata and the existing transactional creation policy: successful conversion
clears actual native WRITEONLY only for supported MANAGED resources, while an
application descriptor retains requested Usage. Failed readable creation or
metadata admission retries the exact original creation path. Metadata-only
attachment skips `reserve_finite`; ordinary Lock/Unlock skip the legacy finite
qualifiers and typed scanner. Reset preserves the original-Usage metadata but
does not report the finite mode as active. This does not yet authenticate a
CloneMesh scope or add a raw snapshot read.

Focused source fixture `lattice_upload_readable_fixture.cpp` includes the
unchanged `finite_upload_fixture.cpp` CPU/fault helpers with its main renamed;
it does not run that older suite. It exercises actual readable VB/INDEX16 IB
creation, requested/actual descriptor separation, zero atlas bytes/scans even
with a nonzero atlas budget, exact fixture-owned bytes, Reset metadata,
creation/attachment failures, and rejected option combinations. A test-only
native dispatch returns a PAGE_NOACCESS mapping; forwarding Lock/Unlock without
reading it must complete with exactly one native call each. This artificial
negative control is not a D3D mapping-access proof. Ordinary successful bytes
are separately checked with explicitly counted fixture-only READONLY reads.

Parent-owned build (the implementation agent does not execute x86 builds):

```
python3 -B verification/probe/lattice_upload_readable_build.py --output /tmp/x3-lattice-readable-build-v1
```

The builder requires a new output directory, uses SSE2 and four-byte incoming
stack flags, and compiles only the existing admission ABI shell without
exceptions. It records compiler/commands, generated dependency hashes (including
the unchanged included fixture) and executable hash. Parent owns any Wine run
under `wine_lock.py` with `X3M_FIXTURE_BOTTLE=X3`. The saved-log checker is:

```
python3 -B verification/probe/lattice_upload_readable_check.py --build /tmp/x3-lattice-readable-build-v1/build.json --log /tmp/x3-lattice-readable-run-v1.log
```

Host checker acceptance is `python3 -B
verification/probe/lattice_upload_readable_check.py --self-test`; it exercises
one valid log and six corrupt/incomplete/overclaimed logs. It tests the checker,
not D3D9. Neither building nor passing the readable prerequisite establishes
the pending CloneMesh observer, its qualifier reentry barrier or SEH safety.

## Manual actual-CloneMesh observer checkpoint

`clone_upload_observer.h` exposes explicit arm/disarm and retained-copy APIs.
`clone_upload_observer_inc.h`, included by `d3d9_ownership.cpp`, observes original
Create/Lock/Unlock completions only while readable mode is enabled and a manual
scope is bound. The accepted `clone_upload_abi.{h,cpp}` boundary invokes the
actual public `ID3DXMesh::CloneMesh`; its separate synthetic fixture original
must never be enabled in this build. The parent compiles the no-EH shell and EH
helper as two objects from the same ABI source. Arm, disarm and retained-copy
queries have no-EH CPU/LastError shells around exception-enabled bookkeeping.

A scope owns temporary references acquired by completed successful public
getters, plus an explicit CPU sidecar reference during authentication. Releases
finish before the final registry guard. A ready tag makes zeroed ABI Context
abort-safe before prepare completes. The separately monotonic boundary nonce
prevents a stale event ticket from matching a later scope at a reused stack
address. Observer C++ failures refuse and preserve ordinary original forwarding.

The supported exception scope is normal documented D3DX/COM HRESULT behavior,
cleanup of the new observer scope, and native/C++ propagation through the tested
manual ABI boundary. Arbitrary native SEH escaping inherited admission or
registry-held foreign callbacks remains unsupported. A third-party getter that
writes an owned output and raises before returning its HRESULT is also outside
this contract: completed successful outputs are cleaned up, but unauthenticated
failure outputs are not released speculatively. The isolated synthetic boundary
unwind tests do not prove inherited wrapper recovery, and this actual fixture
explicitly reports `inherited_callback_seh_tested=0`.

`lattice_upload_clone_fixture.cpp` uses actual D3DXCreateMesh and CloneMesh with
both selected sizes, authored shuffled INDEX16 indices and distinct per-vertex
attributes. Four success cases cover identical half-position declarations and
FLOAT4-to-half conversion. Each compares retained bytes, canaries, final public
identities, actual clone bytes and original source/destination lock counts.
Later fixture-only READONLY verification is separately counted. Retained-copy
success and short-capacity refusal check full x87 state, MXCSR and LastError.

Implemented negative controls are source VB/IB Lock failure, original destination
Unlock failure ignored by actual D3DX, Reset/reentrant Lock/nested Clone/observer
C++ failure after public qualifiers, readable-conversion fallback to WRITEONLY,
mutation during final temporary Release, and unsupported INDEX32 forwarding.
The concurrent Reset control creates a MULTITHREADED device on the Reset thread,
parks the worker producer before its final CPU guard while it owns registry,
and verifies native Reset cannot start until the raw copy completes. Its two
actual HRESULTs are retained; a successful Reset is not required to establish
that ordering. The fixture-only pause occurs before the live guard, never
between the guard and memcpy. Every session checks device/factory zero references
and no typed atlas work at cleanup.

The private allocator OOM case remains the accepted host-core model, not an
injected D3DX allocation failure. Native source bypass and forwarding-slot faults
are refused in source but do not have separate actual-Clone fixture injections.
This checkpoint does not test game hook installation, F8 association, historical
frame identity, draw-input coherence, texture identity or crawl correction.

The parent retains local build commands, transitive source hashes (including
unchanged included fixtures and both ABI source roles), emitted-code review and
parent-owned X3 executions, including the final acceptance run. Validate saved evidence with:

```
python3 -B verification/probe/lattice_upload_clone_check.py --self-test
python3 -B verification/probe/lattice_upload_clone_check.py --build BUILD_JSON --log SAVED_STDOUT
```

The checker requires four exact byte cases, all negative-control multiplicities,
cleanup counts, actual Reset/Clone HRESULTs, unchanged source/EXE hashes, SSE2
and four-byte stack flags, two real ABI roles and no synthetic-original macro.
Its host test accepts a complete synthetic log and rejects ten corrupt logs;
that test validates the parser only. No extra per-draw work or allocation is
added. Armed mapped operations add bounded public metadata qualifiers, brief
registry sections and one memcpy per accepted destination; invalidation wipes
touched CPU storage. Runtime duration is diagnostic cost, not FPS or loading
performance.

The actual fixture requires an explicit D3DX DLL path argument and records the
resolved module filename, D3DXCreateMesh export RVA and bounded Wine PE marker
check. Parent provenance separately binds the selected native DLL hash to the
reviewed RE input. These are fixture selection/provenance controls only; the
production observer has no DLL path, marker or hash prerequisite. Runs v3/v4
used default DLL search and refused at source Lock flags `0x10` (destinations
used `0`), before any staging. They do not qualify the native route and do not
justify widening its strict `0x810`/`0x800` contract.

Final manual checkpoint: `/tmp/x3-lattice-clone-build-v6/build.json` and
`/tmp/x3-lattice-clone-run-v6/{run.json,lock.json,stdout.log}`. The final X3 run
passes 421 checks across 13 exact-S_OK arm/disarm sessions; the saved-log checker
and independent combined review pass. Runtime was 6.03905 s, with 5.90308 s
inside the lock and 0.00000325 s lock wait. Both concurrent Reset and Clone
returned S_OK. Bottle X3 uses WineArch arm64, `FEX_X87REDUCEDPRECISION=1` and
`WINEMSYNC=1`. Native D3DX SHA-256 is
`c2ccb84c672a9d8966e82a28005a4269886ee304972ac3590c0b8a9c1622a3d8`;
fixture EXE SHA-256 is
`be89541832bdcc37f84c5e82cbfde0b688fdfdc4d44635d26d7ba17c4b3c451e`.
The parent also compiled the ordinary production object without fixture macros;
review found no fixture symbols and verified all three query/control state
shells have no SJLJ registration outside their guard. Failed v3/v4 route probes
and the v5 predecessor remain local provenance. No game was launched or changed.
