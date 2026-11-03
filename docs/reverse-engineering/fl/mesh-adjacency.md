# Farnham's Legacy mesh adjacency contract

Offline checkpoint, 2026-10-29. The existing algorithm and public COM adapter
are shared AP/FL candidates, but **FL is not currently admitted**: its mesh
factories are imports of `d3dx9_43.dll`; `loading_trace.cpp` matches only
`d3dx9_37.dll`. Adding a game-structure adapter, nativecaller patch or separate
adjacency implementation is not justified by this evidence. Admitting `_43`
and executing focused differential/adapter checks remain necessary. No
production change, DLL execution, Wine, game launch, build or installation was
performed for this checkpoint.

Owners: [`src/proxy/loading_trace.cpp`](../../../src/proxy/loading_trace.cpp)
contains import discovery, public resource qualification and mesh vtable hooks;
[`mesh_adjacency_fast.{h,cpp}`](../../../src/proxy/mesh_adjacency_fast.h) contains
the pure algorithm and mode/FP policy. There is no current
`src/engine/ap/mesh_adjacency_fast.*` owner. The original
[D3DX rules](../d3dx-generate-adjacency.md) and
[fast-path ledger](../../verification/mesh-adjacency-fast.md) retain AP evidence;
the [FL port](../../architecture/farnhams-legacy-port.md) owns integration decisions.
Reader, gzip and crypto adapters were not re-audited.

## Identity and reproducible evidence

All addresses below are preferred VAs, not runtime patch addresses. Measured
file provenance:

| Image | Bytes | SHA-256 |
| --- | ---: | --- |
| `/mnt/c/X3AP-orig/X3AP.exe` | 2,153,984 | `fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab` |
| `/mnt/c/X3FL-orig/X3FL.exe` | 2,804,736 | `e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8` |
| AP app-local `d3dx9_37.dll` | 3,786,760 | `c2ccb84c672a9d8966e82a28005a4269886ee304972ac3590c0b8a9c1622a3d8` |
| `/mnt/c/Windows/SysWOW64/d3dx9_43.dll` | 1,998,168 | `0b28546be22c71834501f7d7185ede5d79742457331c7ee09efc14490dd64f5f` |

The FL directory also contains the same `_37` file, but its presence does not
change the EXE's `_43` imports. No app-local `_43` file was present there. The
SysWOW64 x86 DLL is an offline comparison input; actual module selection under
FL/CrossOver is unmeasured. Hashes identify evidence, never production admission.

Run from the repository root:

```sh
python3 verification/results/fl-port/mesh-adjacency-contract/produce.py
```

The compact [record](../../../verification/results/fl-port/mesh-adjacency-contract/record.json)
is produced by the adjacent [script](../../../verification/results/fl-port/mesh-adjacency-contract/produce.py),
which reuses `tools/analysis/inspect_x3.py` and Capstone. Measured PASS:
20 instruction anchors, five mutation negatives, six real relocation-operand
checks, 115 equivalent normalizer instructions and 512 equal coefficient pairs.
It validates its JSON after writing. The record binds six owning source files,
four binary identities and nine fully decoded windows; it contains no raw code.
These are bounded observations, not a general PE admission implementation or
runtime acceptance. This `fl-port` directory is ignored by the repository;
retain only `produce.py` and `record.json` explicitly when committing evidence.

Ghidra 12.1.4 analyzed `_43` in the private project
`/tmp/x3-fl-adjacency-ghidra/D3DX43`. The existing
`tools/analysis/X3DecompileFunctions.java` was used at the addresses in the
tables below. Raw decompilation/listings/logs remain in `/tmp/x3-d3dx43-*.local.*`
and `/tmp/x3-fl-adjacency-callers.local.txt`, untracked. To reproduce targeted
decompilation, import the stated DLL with `-analysisTimeoutPerFile 180`, then
use `-process d3dx9_43.dll -noanalysis -readOnly -scriptPath tools/analysis
-postScript X3DecompileFunctions.java /tmp/adjacency.local.c` followed by the
requested function VAs. Do not commit the output.

## Factories, calls and ABI

| Boundary | AP | FL |
| --- | --- | --- |
| `D3DXCreateMesh` import / caller | `_37`, IAT `0x532360`; thunk `0x4faf12`, call `0x4bb72a` | `_43`, IAT `0x550334`; direct IAT call `0x4dbbdb` |
| `D3DXCleanMesh` import / caller | `_37`, IAT `0x53235c`; thunk `0x4faf18`, call `0x4bc873` | `_43`, IAT `0x550348`; direct IAT call `0x4dcd35` |
| `GenerateAdjacency`, COM slot 22 | slot load `0x4bc761`, call `0x4bc76a` | indirect call `0x4dcc4f` through `+0x58` |
| `ConvertPointRepsToAdjacency`, slot 20 | same documented COM ABI | fallback call `0x4dcca1` through `+0x50` |
| `OptimizeInplace`, slot 27 | same documented COM ABI | call `0x4dcdfa` through `+0x6c` |

These factories are standard x86 `WINAPI`/stdcall functions with six stack
arguments and `HRESULT` in EAX. The current wrappers have compile-time type
checks against the D3DX declarations. FL Create's push sequence
`0x4dbbd0..0x4dbbdb` supplies out, device, declaration, options, vertex count and
face count in right-to-left order. Clean supplies errors, adjacency-out,
mesh-out, adjacency-in, mesh-in and clean type; FL passes type 3.

The COM replacements also use stdcall: `this` is a stack argument, not a
thiscall ECX contract. Slot 22 takes `this, FLOAT epsilon, DWORD* adjacency`
and pops 12 bytes; slot 20 takes `this, const DWORD*, DWORD*`; slot 27 takes
`this, flags, adjacency-in, adjacency-out, face-remap, vertex-remap` and pops
24 bytes. `_43` slot-22 thunks end in `ret 0xc`. There is no inline stolen
instruction span, engine-register reconstruction or caller-return-address
allowlist in the production adjacency route. ECX/EDX/EAX, XMM and flags have
ordinary public-call volatility; EBX/ESI/EDI/EBP and stack restoration are the
compiler's ABI obligations. Hook entries must retain the repository's
four-byte incoming stack realignment flags. Full GPR/flags transparency at an
arbitrary engine instruction would be a different contract, not needed here.

FL `0x4dcc45` writes epsilon bits `0x358637bd` (`1e-6f`) into the stack before
slot 22. AP loads the same bits from `0x565600`. FL `0x4dbb7a..0x4dbbc0`
constructs options `0x990/0x991`, optionally ORs `0x18000`, yielding the same
four dynamic SYSTEMMEM variants the adapter explicitly accepts. Declarations
selected here are at `0x57c3f0/0x57c490`; no production code needs those VAs.
The create/clean/adjacency callers inspect HRESULT and can invoke the game's
recovery callback before retrying. FL adjacency retries, then falls back to
slot 20 with null representatives on failure. Clean publishes/replaces the
mesh and subsequently optimizes it; the proxy observes a successful clean's
output, so both factory routes are needed for coverage.

FL is relocatable (`DYNAMIC_BASE`, unlike AP). Its Create/Clean absolute IAT
operands at `0x4dbbdd/0x4dcd37` each have a HIGHLOW relocation. The producer
checks both at synthetic bases `0x400000`, `0x18000000`, `0x80000000`.
Existing import discovery already walks the supplied loaded module's RVAs
and compares import names; no fixed FL VA or new hook span is needed. These
six arithmetic checks do not emulate OS loading, CFG or memory protections.

## D3DX43 algorithm evidence

Native DLL internals below are **research anchors only**. The shared adapter
uses public interfaces and module pinning, not these layouts or addresses.

| Role | `_43` preferred VA |
| --- | --- |
| `D3DXCreateMesh` / `D3DXCleanMesh` exports | `0x10066547` / `0x10049b3f` |
| 16-bit / 32-bit shared mesh tables | `0x100060d8` / `0x10006150` |
| slot 20, 16-bit / 32-bit | `0x10062e80` / `0x100632ff` |
| slot 22, 16-bit / 32-bit | `0x10062ecc` / `0x1006334b` |
| slot 27, 16-bit / 32-bit | `0x10068d6e` / `0x100690f2` |
| representative workers, 16-bit / 32-bit | `0x1005ef85` / `0x1005f6a1` |
| heap sort / weld-refusal helpers | `0x1004b612` / `0x1004ebe1`, `0x1004ed1e` |
| representatives-to-adjacency, 16-bit / 32-bit | `0x10075b27` / `0x10057a12` |
| reverse lookup / own-edge removal / normal score | `0x1004fd10` / `0x10049d84` / `0x1004fb57` |
| math dispatch / generic installer / SSE2 installer | `0x100469f2` / `0x10047025` / `0x101bc521` |
| generic / SSE2 normalization | `0x10046d90` / `0x101bca21` |

Targeted derived findings match the established `_37` rules: workers reject
null output and write-only options (`0x440`), construct raw corner chains,
heap-sort using the float at vertex byte zero, weld only directly to an
eligible representative, and refuse welding vertices sharing a face. The
converters iterate attribute ranges, skip a face's absent first index and
welded degenerates, insert directed edges at the head, select a reverse edge
by strict float normal score, unlink the selected entry, remove the querying
entry only on successful lookup, and reject repeated neighbours using earlier
slots only. The byte-zero normal reads and normalize-dispatch calls are visible
in `0x1004fb57`; the decompiler's inferred prototype is poor, so assembly is
the authority there. This is manual structural comparison, not complete
instruction equivalence of both workers/converters or exhaustive parity.

The producer establishes stronger, automated arithmetic evidence for the two
supported normalizers: all 74 generic and 41 SSE2 instructions match `_37`
after masking only image address fields, with relative branch targets and
referenced scalar contents checked separately. Register operands, widths,
stack offsets and operations remain exact. The generic table at `_37`
`0x76c2a0` and `_43` `0x101ca2c0` has 512 identical float coefficient pairs
(4,096 bytes, SHA-256
`eb0f36f3077c316b810db07ef265e08363ee902ae52cba4f08eb5ae477963d41`).
Changing a scalar from 3 to 2 or flipping one coefficient bit is rejected.
The `_43` dispatch has the same registry override order (`DisablePSGP`, then
`DisableD3DXPSGP`), value-1 generic/value-2 skip-3DNow decisions, feature-7
3DNow attempt followed otherwise by SSE2/feature-6 SSE selection. The 3DNow
installer additionally tests CPU support before overwriting the generic table.
This supports reuse of the existing detector; actual process dispatch and
registry/CPU edge cases still require the focused fixture. SSE and 3DNow
remain native-only in the production fast service.

## Public resource gate, state and lifetime

The shared gate admits only an observed shared mesh table and unchanged
snapshotted slots `4,5,7,8,9,13,14,15,16,17,18`. Those cover counts,
declaration/stride/options, VB/IB acquisition and locks/unlocks. It requires
SYSTEMMEM, rejects unknown option bits, and restricts dynamic options to the
four values above. Typed acquired VB/IB references are released after checking
their public descriptors: correct type/format, SYSTEMMEM pool, exact expected
dynamic/software usage and sufficient size. Optional ownership content views
reject requested-but-unknown, ambiguous or currently locked content. Backend
vtable identity, Wine private layouts and DLL fingerprints are not prerequisites.

The compute service then requires a single-stream FLOAT3 POSITION0 (last
matching element), valid offset/stride, and either no attribute table or at
most 16 contiguous ranges covering all faces in order. `GetAttributeTable`
(slot 19) is called through the public interface but is **not** among the 11
snapshotted/pinned gate methods. This is the existing adapter's actual boundary,
not a claim that every method is individually pinned/compared. The fresh-game
declaration's exact bytes/stride and `_43` duplicate-position declaration parser
were not fully reconstructed here; public admission remains the authority.

The gate enforces representable VB/IB/output sizes and non-overlap between
output and locked input buffers. Both inputs are read-only locked for compute.
The pure module additionally checks index range, finite coordinates, epsilon
domain and the strict separation condition that distinct positions exceed
`2*epsilon`; grid inputs use a cheap proof, other inputs use a spatial hash.
Output is untouched on module refusal. It supports WORD/DWORD indices;
16-bit inputs with more than 65,535 vertices remain native-only.

Fast requires masked, nearest, 53-bit x87 controls, an empty x87 stack and
MXCSR controls `0x1f80` or `0x9fc0` (sticky flags ignored for admission). It
computes under MXCSR `0x1f80` and restores incoming FP environment/LastError
on the successful-compute and ordinary-refusal paths.
SSE2 meshes with competing reverse-edge normals are refused before output;
generic normalization is shared. Ordinary refusal restores the incoming state
and calls that table's native method. Successful compute returns `S_OK`.
Outer `CpuCallBoundary` transports the x87 register image, MXCSR and LastError
across normal calls. Verify calls native first, compares scratch output and
restores the native post-call state. Neither source inspection nor these
offline checks prove compiled full-state transport on FL.

After an ordinary unlock failure, one recovery attempt is made per buffer.
Persistent failure returns that HRESULT, switches the process to native mode
and avoids native fallback on the still-unrepaired current call. The fault is
process-latched, requiring restart. The output may already have been computed
before this cleanup failure. There is no promise to undo output on that edge.

`observe_mesh` uses one exclusive setup lock and bounded records for eight
distinct shared tables; it pins the factory implementation, table owner and
saved call targets with documented module APIs. Slots `20,22,27` are changed
with aligned pointer CAS/protection restoration, not instruction patches.
Partial setup rolls back owned slots. Protection debt and remaining ownership
remain reportable. Shutdown stops observation, restores only still-owned slots
and retains originals/records/module lifetime for foreign chains. Mesh objects
are not retained; each caller owns its mesh, buffers and output for the call.

No lock is held around native mesh operations or algorithm execution. Distinct
tables have distinct trampolines; a cloned/replaced vptr chained back to one
keeps its original. The service snapshots the thread-local native target before
calling public mesh methods, so nested calls cannot redirect that current
fallback. Scratch is per thread; no user callback is invoked by the pure
algorithm. Same-object concurrent mutation or allocator-driven recursion into
the pure routine is not established as supported; external mesh serialization
remains required, as for direct D3DX use.

Outward COM/SEH exceptions are **unqualified**: the service's buffer references
and read locks use manual cleanup; `light::Span` restores TLS nesting only in
`finish()`. An exception after acquisition can bypass release/unlock and leave
the TLS parent pointing at an unwound span. `CpuCallBoundary` is not a proof of
the throw-point machine state, nor does it repair these resources. Existing
ordinary HRESULT/refusal evidence must not be presented as exception safety.
This is a shared AP/FL limitation; no independent FL owner framework is needed.

## Configuration, caches and source-derived cost

The launcher default is `--mesh-adjacency fast`; `native` opts out. Environment
parsing also accepts `verify`, but it arms only with telemetry and is no longer
a launcher choice. Fast arms without telemetry; `capture.cpp` explicitly calls
`loading_trace::initialize()` for this request. Non-telemetry installation
selects just the two mesh import rows (plus separately requested consumers),
while telemetry also observes the other mesh methods. With the current strict
name match, FL finds zero mesh rows and cannot enable adjacency; simply having
other loading hooks installed does not establish adjacency coverage.

The old result cache and live dump option were removed. Current production
has no mesh-result cache, persistent disk cache, cache key or content invalidation
protocol. Historical cache documents/comments/log field names are not current
behavior. Every admitted call reads current locked bytes and recomputes.
Retained thread scratch grows only when required; at most 16 MiB is retained
per thread, larger arenas are released after the call. A separate optional
per-call normal cache uses two allocations only for competing candidates;
SSE2 fast admission refuses that case before normal allocation. Thread exit or
`release_scratch()` releases retained scratch; mutation, Clean and Optimize
need no result-cache invalidation.

Source-derived costs, **not measured timings**: each ordinary admitted mesh
call performs two readability queries, an eight-record search, 11 method
comparisons, descriptor/content checks and roughly 20 public COM calls
(21 when an attribute table is fetched), including two AddRef-bearing buffer
acquisitions, two releases, two locks and two unlocks. Retry failures add calls.
The non-telemetry wrappers still execute their CPU envelope, timing spans,
thread-local access and fixed atomic counters. These are mesh-preparation
costs; no per-draw hook or loading improvement is established here.

The pure module uses linear input scans/hash construction and a heap sort
(`O(V log V)`) for positive epsilon; hashes, weld-refusal chains, near-neighbour
checks and competing-edge lists can exceed linear work on adversarial meshes.
Scratch is sized once per call and phase storage reused; it is not one malloc
per call after warm-up. Generic competing normals can incur the extra normal
cache allocations. No new algorithm/backend variant or repeated EXE validation
is required to address `_43` import admission.

## Remaining gates

The smallest supported implementation direction is bounded `_43` factory
admission in the existing shared import adapter, preserving AP matching and
existing lifetime/rollback gates. Do not treat the name-only counterfactual
as that implementation. No FL nativecaller hook/profile addresses are needed.

Before claiming the enhancement works on FL, execute the actual shared
adapter/core against the actual intended D3DX43 with AP controls: verify/fast
admission, native comparison, declaration/attribute/options refusal, FP controls,
foreign-table changes, nested calls, lock failures, partial patch rollback and
protection debt, then normal resource recovery/Reset. Qualify outward exception
cleanup if it is part of the activation contract. These are open execution
gates, not new tests added by this documentation checkpoint. Native Windows
and CrossOver must both retain the enhancement; unknown D3DX cases may fall
back per mesh, but forwarding every FL mesh is not support. Actual FL/CrossOver
module selection, user loading time and live behavior remain unverified.

## Port (2026-11-03)

Shipped in the fl edition. `loading_trace.cpp` now matches `d3dx9_43.dll` in the
fl build (`X3M_LOADING_D3DX`), so the two mesh import rows (`D3DXCreateMesh` IAT
`0x150334`, `D3DXCleanMesh` IAT `0x150348`, found by name) admit on FL; the shared
`observe_mesh` patches slots 20/22/27 of the public `ID3DXMesh` vtables as on AP.
A new `fl_mesh_contract` gate (`src/engine/fl/mesh_adjacency_sites_fl.h`) requires
the X3FL.exe identity, the install window, every mesh window (the two factory
calls at `0x0dbbdb`/`0x0dcd35`, the slot-22 adjacency call at `0x0dcc4f` with
epsilon `0x358637bd`, the slot-20 fallback at `0x0dcca1`) and the name-found slots
at exactly `base + RVA`; otherwise it refuses before any slot is touched and logs
`mesh_adjacency_config … reason=… edition=fl`. `X3M_MESH_ADJACENCY=fast` arms it
through `capture.cpp`'s FL block (`loading_trace::initialize()`).

Verified (offline): the site verifier `verification/probe/verify_mesh_adjacency_sites_fl.py`
(windows, mutation refusals, the `_43` named slots, no `d3dx9_37` import, the
call/vtable ABI, AP refusal) PASS; `cmake -DX3M_EDITION=fl` builds with 0
warnings; the shared algorithm's differential against the reference is unchanged
(`test_mesh_adjacency_fast.py`). Not verified: not flown; native Windows and
CrossOver `_43` module selection; a device-level `_43` differential (needs a D3D9
device, out of scope here); loading time.
