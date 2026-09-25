> Portability update: `RuntimeIdentity` now carries a caller-owned process-local
> `algorithm_token`, not a DLL digest. The adapter qualifies public readable
> SYSTEMMEM buffers and pins implementation lifetime; no DLL/EXE hash allowlist
> remains. Earlier exact-backend observations below are historical evidence.
> The fresh public-contract regression passes 767 checks (2026-09-12, FP contract:
> masked exceptions and an empty x87 stack required, precision/rounding/FTZ/DAZ
> keyed); Windows runtime validation remains outstanding.

# Production adjacency cache core

`src/proxy/mesh_adjacency_cache.{h,cpp}` implements bounded, process-local reuse of
successful native `ID3DXMesh::GenerateAdjacency` results. The 767-check evidence below tests this core separately from the game. The
[off-by-default live hook](mesh-cache-hook.md) connects it to the proxy with additional
public-interface gates and separate evidence. No game loading improvement is claimed. This advances the [original synthetic prototype](mesh-preparation.md)
by acquiring the current mesh through its real native readonly buffer locks and
calling the actual reusable production module in the fixture.

The [iteration 4 trace](../reverse-engineering/iteration04-loading.md) measured
6,588 adjacency calls / 21.287 seconds and repeated aggregate activity vectors.
Those observations did not establish equal mesh bytes, memory-pool eligibility or
cache hit rate. This core deliberately leaves those gates open.

## Interface and lifetime

An explicit `Cache` owns only heap memory. It has no mesh, device, factory or DLL
references, no global registry, disk storage or hooks. The caller supplies a
borrowed native mesh, original native function pointer, exact epsilon and original
output pointer. Runtime identity contains a nonzero caller-owned process-local
algorithm token, a nonzero generation, and a positive public-contract flag. The cache includes the saved function pointer in its key too. The
caller must keep the implementation loaded; `public_contract=true` asserts the caller's
contract, not a DLL fingerprint. The token must change if the caller changes its
algorithm contract without changing the function pointer.

The caller keeps the mesh/output alive and serializes their mutations during the
call, just as required for a direct native mesh operation. It must not clear or
destroy the cache while calls are in progress. There is no implicit synchronization
with other code changing a mesh's geometry. `clear()` frees all retained entries,
keeps cumulative counters, and **does not remove permanent cleanup-failure poison**.

`generate()` returns `Outcome { hr, origin }`. Origins distinguish a native call,
a cache hit, an acquisition-cleanup failure, and an invalid/null function callback.
Consumers must inspect that origin, especially before considering live integration.

## Exact identity and admission

The key contains schema version, process-local algorithm token/generation/function, incoming LastError, exact float
bits for epsilon, full mesh options, vertex/face counts, stride, every declaration
element through its terminator, all vertex/index bytes, and supported incoming
floating-point control/status. A 64-bit hash only selects candidates: every hit
also requires equal key lengths and a full byte comparison. Forced hash collisions
are tested. There is no mesh-address identity or semantic/approximate comparison.

The initial adapter accepts only meshes with **both VB and IB in SYSTEMMEM**, one
vertex stream, and neither write-only nor shared-vertex-buffer options. Dynamic
SYSTEMMEM requires the explicit default-false runtime capability
`systemmem_dynamic_readonly_verified`; the live adapter narrows this to four
reviewed game variants. It supports 16-bit and 32-bit indices. Managed/GPU-backed meshes bypass to avoid
assuming that acquisition costs and synchronization are harmless. The later iteration 5 static-path diagnosis identifies SYSTEMMEM+DYNAMIC options
in the game, while exact repetition and user-run benefit remain unmeasured.

Only `S_OK` results whose observed LastError remains unchanged and whose FP state
satisfies the verified contract are admitted. Other successful HRESULTs and failures
are never cached. The core does not inspect output bytes on failure. A hit copies
adjacency into the current caller's output and returns `S_OK`; it never returns an
internal pointer. Cleaning and optimization remain native operations on the
**current** mesh. Face attributes are excluded from the adjacency key, with a
same-geometry/changed-attributes full downstream parity check.

## Floating-point and LastError contract

The exact native DLL changes x87 status during successful adjacency generation:
the fixture's near-epsilon seam changes status from `0x0000` to `0x0020`, while
other original meshes also change condition-code bits. Merely retaining epsilon
bits or preserving the incoming flags on a hit would be incorrect.

The adapter requires masked x87 and SSE exceptions and an empty x87 stack with
TOP zero. Precision control, rounding and FTZ/DAZ are **keyed, not refused**
(changed 2026-09-12: loading run 2 showed the game entering every call with x87
control `0x027f`, 53-bit precision, and MXCSR `0x9fc0`, FTZ+DAZ, which the
earlier `0x007f`/`0x1f80` requirement bypassed 8,718 times). The adapter still
does **not normalize** the caller's state: incoming x87 control and status and
the whole MXCSR participate in the key, so a result is only replayed under the
state it was computed in. Unmasked exceptions call the original unchanged.
The first incoming state is published once (`mesh_cache_fp_incoming ...
supported=`), and the first unsupported one separately. Successful output status
and MXCSR are retained and replayed on a hit; changed controls/tags reject admission.

Incoming LastError and computational FP state are captured before any metadata or
lock queries, restored immediately before a miss/bypass calls the original, and
native outgoing state is restored after cache bookkeeping. Hit state is restored
after heap release/timing too. Fixture lock callbacks deliberately disturb
both states, then verify the values seen at the actual original entry.

This is a **computational-state contract**, not full machine-state emulation.
x87 diagnostic instruction/data pointers are not reproduced on hits; current
caller pointer fields are retained. Native allocator behavior and other incidental
side effects of executing the skipped algorithm are not replayed. The positive
cache admits a native miss only when its actual successful result preserved the
incoming LastError. It does not infer that property from module pinning or claim
an untested Windows implementation has identical incidental behavior. Other misses
retain the native result and are not cached. Incoming LastError is part of the
exact key, so a preserving candidate under one value cannot hide a conditional
native LastError change under another value. The original fixture explicitly
tests zero-preserving admission, a 123-to-zero native result, then the original
zero-key hit.

## Bounds and concurrency

Defaults and hard ceilings are 16 MiB retained storage, 512 entries and one 4 MiB
transient workspace. Configuration can reduce these limits. Retained accounting
includes `sizeof(Cache)` (fixed entry/counter metadata) and actual `HeapSize`
payload sizes. The fixed object exists even when a configured byte limit is below
its size; such a configuration admits nothing. Allocator bookkeeping, caller
mesh/output memory and fixed stack temporaries are outside that accounting.

Each candidate is a single heap allocation containing the complete key and space
for adjacency. It must fit both the scratch limit and the retained payload limit
before acquisition. Individual 64-bit VB/IB/result span products are bounded before
summing sizes, avoiding malicious-count overflow. Output address spans use 64-bit
range arithmetic. Output overlapping current vertex/index storage bypasses reuse.

A single nonblocking atomic workspace lease bounds transient allocation across all
threads. It can span the original miss computation; concurrent or recursive callers
immediately call the original instead of waiting. No COM registry lock is held.
The lease is a cache-workspace policy, not serialization of native mesh use. The
fixture schedules native calls safely while checking actual thread contention.

Successful admission transfers the existing candidate allocation into a fixed
entry slot. LRU eviction releases entries until both byte and slot limits fit;
there is no extra admission allocation. Thus accounted retained payload plus the
single candidate stays within the two stated limits. The fixture independently
exercises slot eviction, byte eviction before slot exhaustion and oversize bypass.
Counters use bounded fixed storage and atomic cumulative sums, with nontransactional
snapshots. QPC stages cover acquisition, hash/lookup, output copy, original call and
total wrapper time. No per-call payload or mesh content is logged.

## Acquisition failures are explicit

Null output, unsupported inputs/modes, allocation failure, metadata failure and
failed readonly locks bypass with original arguments and original incoming state.
Acquired buffers are unlocked before the algorithm is called. Both success and
failure paths avoid publishing partially acquired keys.

An Unlock failure has unknown retained-lock state. The adapter makes one bounded
retry. If that succeeds, it bypasses normally. If it fails again, the cache becomes
permanently disabled and returns **`AcquisitionCleanupFailure`**, carrying the
cleanup HRESULT, without invoking native adjacency on a possibly locked mesh.
This is intentionally **not** labeled a native HRESULT/fallback.

The caller must stop using the affected mesh until its acquisition failure is
resolved; disabling cache lookup is not an automatic unlock or mesh recovery.
The fixture explicitly repairs its injected retained lock before testing subsequent
disabled passthrough. A live hook cannot simply discard this distinction or claim
full transparency. This exception policy needs conscious integration review
before enabling the adapter in the game. It is a reason this checkpoint remains
a detached module.

## Verification and synthetic cost

Run:

```sh
python3 verification/probe/run_mesh_adjacency_cache.py
```

The runner builds fresh with x86 SSE2, `-mfpmath=sse`, stack realignment and the
four-byte incoming-stack contract, records the current native DLL SHA-256 without an allowlist, launches
only the original standalone fixture through **CrossOver Preview / Steam**, and
checks source/DLL hashes before and after compilation, then source/executable/DLL
hashes before and after execution. It invalidates prior PASS evidence before building
and records failure if compilation, launch or timeout handling fails. Its process-local
`d3d9=b` override neither installs a DLL nor changes bottle settings. A hidden device
is used without Present or game assets. Fixture seams are compile-time only;
production source also compiles independently with `-Wall -Wextra -Werror`.

The final fixture passes **767 checks** (`run_mesh_adjacency_cache.py`, 2026-09-12, unchanged by the adjacency parity rewrite of the same night;
741 before the FP-contract change). Coverage includes:

- The FP contract: ten state variants driven through fill and reuse. Sticky
  status flags, condition codes, 64-bit precision, rounding, FTZ, DAZ and the
  game's `0x027f`/`0x9fc0` (variants 0-7) are keyed and hit on reuse; an unmasked
  x87 denormal exception (variant 8, control `0x007d`) bypasses. Variant 9 asks
  for an unmasked SSE denormal exception (MXCSR `0x1e80`), which the Steam
  bottle's x86_64 Wine under Rosetta 2 does not apply: the state reads back as
  `0x1f80`, so the fixture derives its expectation from the applied state
  (`FP_VARIANT ... applied_mxcsr=00001f80 masked=1 expect=keyed`) and records
  that this variant is only a bypass witness on hosts that honour SSE exception
  masks (native Windows, FEX to be confirmed).

- Five original mesh cases through native generation, cleaning and optimization,
  with exact adjacency, vertex/index/declaration/attribute data, face/vertex remaps
  and HRESULT parity, including a degenerate mesh's preserved CleanMesh failure.
- Runtime, generation, epsilon, position, UV, topology, declaration and option
  mutations; 32-bit indices; changed attributes; forced collisions.
- FP status-key separation/replay, alternate-control bypass, and LastError/FP
  restoration after deliberately disturbed acquisition.
- Native failure/partial output, S_FALSE and changed-LastError admission refusal;
  optional null output; allocation failure, both lock failures, recovered and
  persistent unlock failures, poisoned-instance behavior.
- Default-false dynamic capability performs no acquisition; a positively verified
  dynamic SYSTEMMEM contract admits exact native fill/hit parity.
- Recursive and actual concurrent lease fallback; output alias and wrapping-address
  rejection; all-`0xffffffff` metadata regression; byte/entry limits and clear.

The original 96×96 grid has 18,432 faces / 9,409 vertices. Fifteen alternating local
measurements produced these medians:

| Operation | Median |
| --- | ---: |
| Repeated uncached native generation | 97.348 ms |
| Production adapter miss | 98.578 ms |
| Production adapter hit | 1.163 ms |

The hit includes **real readonly buffer acquisition**, key allocation/copy, full
hash/equality checks and adjacency copy. Both measured paths include common fixture
output-vector creation. The miss includes all acquisition/lookup work plus native
generation and admission. The retained grid entry plus fixture-build cache metadata
accounts for 540,908 bytes. These are warmed-runtime synthetic measurements from
one run, not a confidence interval or game performance forecast. The prototype's
host-memory key timing is not substituted for real acquisition cost.

Raw evidence is [mesh-adjacency-cache.txt](../../verification/results/mesh-adjacency-cache.txt).
[The summary](../../verification/results/mesh-adjacency-cache-summary.json) records
exact source/build/DLL/report hashes and the complete command for the detached
checkpoint. The later [live hook checkpoint](mesh-cache-hook.md) adds reviewed
opt-in wiring, endpoint gates and explicit cleanup-failure handling. Game mesh
eligibility, repeat-hit rate, retained memory pressure and live benefit remain
unmeasured; neither checkpoint installs or launches the game.

**Removed 2026-09-25** (user decision): `--mesh-cache` with `src/proxy/mesh_adjacency_cache.*`, the cache branch of the adjacency hook and its fixtures and runners (the fast/verify adjacency is unchanged); `docs/verification/launcher-options-inventory.md`, "4. Removed 2026-09-25".
