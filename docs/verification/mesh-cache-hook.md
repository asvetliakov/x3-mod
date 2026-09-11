# Experimental live adjacency cache hook

`X3M_MESH_CACHE=1` connects the reviewed production adjacency cache to the existing
`loading_trace` shared native `ID3DXMesh::GenerateAdjacency` slot. It requires
`X3M_TELEMETRY=1` and remains **off by default**. This checkpoint has only run
standalone original-mesh fixtures in **CrossOver Preview / Steam**. Those fixtures
do not install the DLL or launch X3. See [iteration 5](iteration-05.md) for the
earlier installed baseline; this portable replacement has not been installed. The game’s eligible mesh count, exact repetition
rate and loading benefit still need a user-run trace.

The [detached core](mesh-adjacency-cache.md) remains independently reusable. Its
16 MiB retained budget includes fixed cache metadata and measured heap payload;
512 entries and one 4 MiB transient candidate bound storage. A successful hit
copies adjacency to the caller’s output. CleanMesh and OptimizeInplace still
process the current original mesh. No mesh data is written to telemetry or disk.

## Activation and native contract

The default path retains its original native dispatch. It performs no new cache
construction, cache-related COM acquisition, buffer lock or
mesh-byte read. The existing timing hook/table lookup and configuration branch
remain. Requested activation pins implementation lifetime; each candidate then
passes these public-interface checks:

- The documented mesh COM slots 20/22/27 are intercepted with saved originals and
  ownership-aware rollback. The actual shared vtable and callable methods must be
  module-backed so saved trampoline chains remain valid after object destruction;
  their owning modules are pinned. Heap/JIT tables with unproven lifetime are not
  hooked. Methods need not share a module with the factory. No code bytes, fixed
  RVAs, PE image base/size profile or file digests identify the implementation.
- Metadata and four mesh Lock/Unlock pointers are recorded on first observation.
  Cache acquisition requires them unchanged. A later foreign method replacement
  therefore falls back through the original algorithm without cache reads.
- Both buffers are SYSTEMMEM, without write-only, managed, shared-VB or unknown
  mesh options. Nondynamic options admit optional 32-bit indices and per-buffer
  software processing; dynamic meshes retain the four actual game variants
  `0x990`, `0x991`, `0x18990`, `0x18991`. Public `GetDesc` must succeed and report
  the correct VB/IB resource type, vertex/index format and exact expected Usage.
  Zero or oversized spans fail. The core further validates declaration, exact key
  size, output range and configured budgets.
- The typed VB/IB interfaces returned by the observed D3DX mesh are held with
  ordinary COM references during preflight. Descriptors are queried through those
  interfaces. Acquisition uses public mesh `Lock*Buffer(READONLY)` and paired
  `Unlock*Buffer`, not a borrowed-native pointer.
- Recognized ownership buffers are checked using `get_buffer_content_view`.
  Requested tracking must already be known, unambiguous, unlocked and successful.
  Replaced wrapper Lock/Unlock routes must make that evidence unknown. Native
  pointers are distinguished by the API's explicit `E_INVALIDARG` result; other
  failures reject. No uncertainty is cleared by cache admission.

**No DLL or EXE version is an activation gate.** The buffer path reads no private
object offsets or backend method/import RVAs. D3DX module pinning controls saved
code/table lifetime only, and applies to the module actually loaded. The cache
uses a process-local adapter token, generation and saved GenerateAdjacency pointer,
not a file digest. A DLL update does not itself reject this public path. Ordinary
loading IAT hooks now qualify the main module through bounded PE32 named-import
parsing, without a whole-EXE fingerprint, fixed image base or fixed image size.

The public API permits readable buffer locks and defines `(0,0)` as locking the
whole VB. WRITEONLY buffers are excluded from READONLY acquisition. DYNAMIC is a
usage choice, not permission for this adapter to use DISCARD or NOOVERWRITE.
See Microsoft's [VB Lock](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3dvertexbuffer9-lock),
[index-buffer access](https://learn.microsoft.com/en-us/windows/win32/direct3d9/index-buffers)
and [D3DX mesh options](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dxmesh).

The caller must serialize application mesh/buffer mutation, vtable changes and
final release throughout qualification, acquisition and algorithm/cache dispatch.
The core's nonblocking workspace lease serializes cache storage; it does not
exclude external application writes. This remains a scoped optional loading cache,
not the replay admission mechanism. No native Windows runtime has been tested;
public API portability is implemented, while Windows runtime validation remains
outstanding. The earlier [Preview unlock review](../reverse-engineering/mesh-unlock-contract.md)
and [dynamic review](../reverse-engineering/mesh-dynamic-contract.md) are historical
backend evidence, no longer production dependencies.

The core's `RuntimeIdentity.systemmem_dynamic_readonly_verified` still defaults to
false. The live adapter asserts it only for its bounded public readable SYSTEMMEM
contract. Generic callers must establish their own acquisition contract. This
change does not admit dynamic GPU buffers or managed WRITEONLY mappings.

The core keys full vertex/index/declaration bytes, counts/options/stride, exact
epsilon, process-local algorithm/function identity, incoming LastError and
supported incoming computational FP state. Incoming LastError partitions the
key so a preserving miss cannot hide a conditional native error change under
a different incoming value.
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

The public acquisition path checks every Lock and Unlock result. The reusable
core distinguishes an unrecoverable acquisition cleanup failure. If it occurs, the triggering hook returns that **observed cleanup
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
chains. Shared-slot records hold no mesh/device references. Pinned implementation modules and the
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
summary only when counters change. Fixed cumulative `mesh_cache_gate` reasons
identify every outer preflight exit; `mesh_cache_gate_first` publishes one numeric
detail per reason (scope, options, slot/entry, status, pool/usage/format/size or
tracker state). First details use release/acquire publication and never change.
Core `mesh_cache_bypass` reasons distinguish FP, identity, configuration, options,
metadata/declaration, size/output bounds, allocation, buffer access and contention.
The first unsupported computational FP state is recorded once; failed result,
LastError and FP admission checks have separate counters. These bounded records
contain no per-mesh keys or payloads.
The existing `MeshAdjacency` loading span now means whole intercepted service
time when the cache is enabled, including lookup/hit or native work; core
`native_ticks` isolates calls that actually reached the saved original.

Gate timing includes public descriptor/tracking checks; it contains no backend
fingerprinting. Module pinning/setup remains charged to the CreateMesh wrapper tail. First-use setup and steady cache work must be separated when judging
a user run. The small correctness fixture is not a game loading benchmark.

## Verification and provenance

Run `python3 verification/probe/run_mesh_cache_hook.py`. It writes `passed:false`
before building, refuses a running X3AP process, snapshots every project compile input and all three native DLLs,
fresh-builds the fixture with the project’s SSE2/stack-alignment ABI flags, and
refuses changed hashes before or after each case. It requires exactly one terminal
PASS line, retains raw report/Wine-log hashes, and preserves a failed summary on
build errors, timeouts or validation exceptions. Host provenance uses the bottle’s
**syswow64** files for the x86 backends; a 32-bit Wine process names them System32.
These backend hashes record the tested environment and are checked for changes
during a run, and none are hardcoded runtime allowlists.

The six-case matrix uses actual imported D3DX CreateMesh/CleanMesh, shared native
vtable interception, the production cache and original synthetic meshes. Native
and ownership-wrapped devices each run cache-off, cache-on, and explicit fault
cases. Assertions cover exact 16/32-bit miss/hit results, all five downstream
clean/optimize results and remaps, complete vertex/index byte invariance, native
failure/null-output semantics, FP/LastError, tracked revisions/pending state,
unknown/pending gate rejection, changed Lock/Unlock routes with tracking off,
changed bytes, recursive contention, native
metadata/Unlock endpoint rejection, public VB/IB forwarding GetDesc acceptance and
failed-but-populated/pool/usage/type/format/size descriptor rejection,
owned-slot restore, surviving saved thunk behavior and final zero device/factory
references. Descriptor spies replace only the held object’s public GetDesc entry, restore
the object table through RAII, and call preflight rather than native algorithms
while descriptors are deliberately false.

Current retained evidence is `verification/results/mesh-cache-hook-summary.json`
and its six raw reports. Native off/on/fault cases pass 1,714/1,927/1,935 checks;
wrapped off/on/fault cases pass 2,189/2,566/2,574 checks (12,905 total). Each
enabled case records 38 core calls, 20 hits, 16 misses, one recursive contention
fallback and one unsupported-FP bypass. Cache-off cases record zero
core calls and no constructed cache. Fault cases emit exactly one restart-required
record and reject four subsequent hooked operations. These numbers establish
actual wiring and parity, not expected X3 repetition. The completed iteration 5
trace and exact option diagnosis are [recorded separately](../reverse-engineering/iteration05-cache-gate.md);
that installed run used the earlier dynamic-rejecting implementation.

The portable replacement passes the complete six-case suite on the frozen
ownership source. The separate named-import suites pass 75 ABI checks and 123 real
mesh checks; the detached core passes 741, including conditional LastError-key
separation. Production and fixture compile with `-Werror`. No Windows runtime
execution has been performed.

In this run, cumulative actual-hook gate time was 0.3833 ms for native-on-normal
and 0.3088 ms for wrapped-on-normal (10 MHz QPC). Each had 38 core calls and 20
hits. These are small synthetic-suite totals, not game loading or per-frame
performance estimates. Lifetime pinning runs once per observed shared table;
subsequent observations reuse pinned originals and check current slot ownership.
