# Experimental live adjacency cache hook

`X3M_MESH_CACHE=1` connects the reviewed production adjacency cache to the existing
`loading_trace` shared native `ID3DXMesh::GenerateAdjacency` slot. It requires
`X3M_TELEMETRY=1` and remains **off by default**. This checkpoint has only run
standalone original-mesh fixtures in **CrossOver Preview / Steam**. Those fixtures
do not install the DLL or launch X3; see [iteration 5](iteration-05.md) for the
separate installation and pending game test. The game’s eligible mesh count, exact repetition
rate and loading benefit still need a user-run trace.

The [detached core](mesh-adjacency-cache.md) remains independently reusable. Its
16 MiB retained budget includes fixed cache metadata and measured heap payload;
512 entries and one 4 MiB transient candidate bound storage. A successful hit
copies adjacency to the caller’s output. CleanMesh and OptimizeInplace still
process the current original mesh. No mesh data is written to telemetry or disk.

## Activation and native contract

The default path retains its original native dispatch. It performs no new cache
construction, SHA-256 verification, cache-related COM acquisition, buffer lock or
mesh-byte read. The existing timing hook/table lookup and configuration branch
remain. Requested activation first verifies and pins the exact native D3DX module;
each candidate then passes all of these additional checks:

- Exact PE32 D3DX SHA-256 `c2ccb84c672a9d8966e82a28005a4269886ee304972ac3590c0b8a9c1622a3d8`.
  Generation 1 identifies the process’s single immutable installation. Geometry
  metadata, GetVB/GetIB and all four mesh buffer Lock/Unlock methods must have the
  verified native entry addresses.
- Both buffers are SYSTEMMEM, without dynamic, write-only or shared-VB options.
  Sixteen-bit and 32-bit index meshes are supported. Native descriptors bound all
  copies; zero or oversized spans fail the gate. The core further validates the
  declaration, exact key size, output range and configured budgets.
- Actual held-buffer Lock/Unlock/GetDesc endpoints match the exact builtin x86
  D3D9 implementation, SHA-256 `58cc36cf74128ae4b6211100430d146c3692808146d8d2075e6c5d846162f8cf`.
  Ownership wrappers need the reviewed unchanged Lock/Unlock forwarding certificate;
  descriptor queries use the independently verified borrowed native buffer.
- WineD3D SHA-256 is `f4997bc0465de7e87bac9921bf0274db00ac3b3ba0754fa03f1f33e309a8e863`.
  The D3D9 resource-map, resource-unmap and buffer-get-resource import slots must
  still point to the verified WineD3D exports. Both modules are pinned. Unknown
  endpoints, foreign mesh metadata or changed critical imports bypass the cache.
- Tracked wrapper buffers must already be known, unlocked and have a successful
  tracking status. Existing uncertainty or a pending lock bypasses. The cache
  neither invents writes nor clears pre-existing uncertainty.

The [exact unlock review](../reverse-engineering/mesh-unlock-contract.md) establishes
paired-lock completion for this runtime under ordinary serialized, non-reentrant
application calls and valid object lifetimes. The proof does not cover invocation
from the Wine command-stream thread, concurrent mutation, corruption, or foreign
in-module code patches. The gate checks files/endpoints/imports; it is not a full
in-memory code-integrity system. Different runtimes remain native-only.

The core keys full vertex/index/declaration bytes, counts/options/stride, exact
epsilon, runtime/function identity and supported incoming computational FP state.
Hash collisions require full byte equality. Only admitted `S_OK` results are
reused. Unsupported FP controls, null output, allocation pressure, nonblocking
workspace contention and ordinary acquisition failures retain native behavior.
The exact FP/LastError contract and excluded diagnostic x87 pointers are documented
with the core. The live hook saves incoming state before timing/preflight, restores
it before core/native dispatch, captures outgoing state immediately, and restores
that state after bookkeeping/logging. It does not erase native status side effects.

## Truthful buffer tracking on hits

The wrapper tracker is this mod’s record of actual observed Lock/Unlock calls,
not part of the native mesh API. On the tested native adjacency path, nested
readonly accesses leave the vertex tracker conservatively ambiguous/unknown with
last lock flags `0x10`. A cache hit performs flat mesh readonly acquisitions,
which the native mesh forwards with `0x810` (`READONLY | NOSYSLOCK`); previously
known inputs can remain known and unambiguous. This is a deliberate diagnostic
difference. No synthetic tracker reset or emulated lock sequence is inserted.

Both paths preserve input bytes and write revisions, leave no pending locks, and
retain exact native adjacency/HRESULT/LastError/computational FP results. A hit
cannot make a previously uncertain buffer known because such inputs fail admission.
A native miss that becomes ambiguous may bypass later requests for that same
object; a fresh known mesh with identical bytes can still hit the process cache.
The fixture asserts these separate paths, including old-ambiguity and pending-lock
rejection. Geometry-history consumers continue to trust only actual known revisions.

## Failure and lifetime policy

Successful paired locks under the verified contract have an established cleanup
path. The reusable core nevertheless distinguishes an unrecoverable acquisition
cleanup failure. If it occurs, the triggering hook returns that **observed cleanup
HRESULT**, records one restart-required fault and permanently disables dispatch.
It does not claim that HRESULT came from native GenerateAdjacency and does not
call native on the possibly locked mesh. Subsequent intercepted adjacency,
point-representation conversion, OptimizeInplace and main-IAT CleanMesh calls
return `E_FAIL` without consuming outputs or changing incoming FP/LastError.

This is an **experimental failure-stop policy for these hooks**, not containment
of all native mesh use. Unobserved methods, internal calls, foreign paths and
restored native slots can bypass it. After such a fault, mesh processing must stop
and the application must restart before continuing or tearing down those objects.
There is no automatic kill and no claim of lock repair. The fixture injects the
explicit Outcome seam without leaving a real buffer locked; real persistent unlock
faults were independently tested in the detached core fixture.

One installation per process preserves immutable originals for saved foreign
chains. Shared-slot records hold no mesh/device references. Exact modules and the
cache’s fixed storage remain valid for the process lifetime. First construction
publishes the immutable cache pointer with a release store; dispatch, reports and
fixture readers acquire-load it independently of the mesh registry lock. Reporting
can therefore overlap first-mesh setup without reversing the log/registry lock
order, and still reads cumulative atomics after a fault disables dispatch. At explicit quiescent
shutdown the cache stops accepting calls, releases retained payload and restores
only owned slots; a saved thunk then forwards natively unless the persistent
fault latch is set. Shutdown is not safe during active callbacks and never clears
the fault latch. Default application teardown uses no worker or background cache.

## Batched diagnostics and setup cost

`mesh_cache_metric` records cumulative integer counts, acquired/copied bytes,
retained bytes/entries, acquisition/lookup/copy/native/total QPC ticks, preflight
rejections and `gate_ticks`. It reports through the existing periodic loading
summary only when counters change. It does not emit per-mesh keys or payloads.
The existing `MeshAdjacency` loading span now means whole intercepted service
time when the cache is enabled, including lookup/hit or native work; core
`native_ticks` isolates calls that actually reached the saved original.

Gate timing includes descriptor/endpoint/import checks and the first backend
fingerprinting cost. First native D3DX fingerprint/setup is charged to the
CreateMesh wrapper tail. These one-time file reads must be separated from steady
cache cost when judging a user run. In the final normal cases, cumulative gate
time was 25.056 ms on the native device and 21.833 ms on the wrapped device;
CreateMesh wrapper-tail totals were 119.659 ms and 27.027 ms, respectively. These
totals include first-use setup rather than estimating steady per-call overhead.
The small correctness fixture is not a game loading benchmark; raw QPC fields
and the recorded 10 MHz frequency retain the distinction.

## Verification and provenance

Run `python3 verification/probe/run_mesh_cache_hook.py`. It writes `passed:false`
before building, snapshots every project compile input and all three native DLLs,
fresh-builds the fixture with the project’s SSE2/stack-alignment ABI flags, and
refuses changed hashes before or after each case. It requires exactly one terminal
PASS line, retains raw report/Wine-log hashes, and preserves a failed summary on
build errors, timeouts or validation exceptions. Host provenance uses the bottle’s
**syswow64** files for the x86 backends; a 32-bit Wine process names them System32.

The six-case matrix uses actual imported D3DX CreateMesh/CleanMesh, shared native
vtable interception, the production cache and original synthetic meshes. Native
and ownership-wrapped devices each run cache-off, cache-on, and explicit fault
cases. Assertions cover exact 16/32-bit miss/hit results, all five downstream
clean/optimize results and remaps, complete vertex/index byte invariance, native
failure/null-output semantics, FP/LastError, tracked revisions/pending state,
unknown/pending gate rejection, changed bytes, recursive contention, native
metadata/Unlock endpoint rejection, changed critical-IAT preflight rejection,
owned-slot restore, surviving saved thunk behavior and final zero device/factory
references. The critical-IAT negative calls preflight only, restores the slot and
page protection, and never dispatches the deliberately invalid target.

Current retained evidence is `verification/results/mesh-cache-hook-summary.json`
and its six raw reports. Native off/on/fault cases pass 744/861/869 checks;
wrapped off/on/fault cases pass 939/1,168/1,176 checks (5,757 total). Each enabled case records 17 core calls, eight hits,
eight misses and one recursive contention fallback. Cache-off cases record zero
core calls and no constructed cache. Fault cases emit exactly one restart-required
record and reject four subsequent hooked operations. These numbers establish
actual wiring and parity, not X3 eligibility or expected repetition.
