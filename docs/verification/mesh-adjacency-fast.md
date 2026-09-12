# Exact-equality adjacency: replacing D3DX's epsilon welding

`X3M_MESH_ADJACENCY=native|verify|fast` (`tools/manage.py launch --mesh-adjacency
{native,verify,fast}`, requires `--telemetry`; default `native`) selects what the
proxy's existing `ID3DXMesh::GenerateAdjacency` vtable hook (slot 22, shared
native table, [loading-performance.md](../reverse-engineering/loading-performance.md))
does with the engine's call at `0x004bc76a`:

* `native` forwards, as before (timing only).
* `verify` calls the native method, recomputes the adjacency with the module
  below, compares all `3 * faces` entries and returns the native result. A
  mismatching mesh logs one full `mesh_adjacency verify ... equal=0` line (at most
  64 lines per process); everything else is aggregated in the periodic
  `mesh_adjacency_metric` line.
* `fast` computes the adjacency with the module and returns `S_OK` without calling
  D3DX. Any qualification failure (gate, declaration, size, lock, or a module status
  other than `ok`) falls through to the native method; a failed `Unlock` that a
  retry cannot repair marks the process faulted and the mode reverts to `native`
  (`mesh_adjacency_fault`), exactly as the cache adapter does.

`CleanMesh`, `OptimizeInplace` and `ConvertPointRepsToAdjacency` are untouched.
The adjacency cache (`X3M_MESH_CACHE`) is composed by making the verify/fast service
the cache's "original" callable: the order is cache lookup, then fast or native
compute on a miss, then admission of an `S_OK` result with unchanged LastError and
computational state (which the fast path preserves by construction).

## Why the engine's call is O(n) work

The engine calls `GenerateAdjacency(1e-6f)` on meshes whose positions were filled at
`0x004bc1c0` as `int16 * (1/16384)` ([loading-profile-run1.md](../reverse-engineering/loading-profile-run1.md)).
D3DX first computes point representatives by welding every vertex to the first
vertex within `epsilon` (its inner loop is the x87 `dx*dx+dy*dy+dz*dz` against
`epsilon^2` scan that cost 31 s of the menu load and 26.6 s in one call), then
converts the representatives into face adjacency. Two positions on the `2^-14`
grid are either bit-identical or at least `6.1e-5` apart, 61 times the epsilon, so
for these meshes the epsilon neighbourhoods are exactly the equality classes and
the welding reduces to a hash of the three position bit patterns.

## Module: `src/proxy/mesh_adjacency_fast.{h,cpp}`

Pure code (no Windows headers, SSE2 only; the production object has no x87
opcode and imports only `malloc`, `free`, `memset` and the compiler's
thread-local support `__emutls_get_address`/`__cxa_thread_atexit`). Its only
state is one thread-local scratch arena: a call computes the byte layout of all
its arrays up front and carves them from a single allocation, which is kept for
the thread's next call when it is at most 16 MB (`release_scratch()` frees it;
larger arenas are released after the call). See *Performance pass* below.
Input: the locked vertex bytes with the `FLOAT3` position offset and stride, the
locked 16- or 32-bit indices, the face count and the epsilon. Output:
`DWORD[3 * faces]`, `0xffffffff` where a face has no neighbour across an edge.

The module implements d3dx9_37's own rules, taken from the disassembly of the
game's `d3dx9_37.dll` ([d3dx-generate-adjacency.md](../reverse-engineering/d3dx-generate-adjacency.md),
rules 1-6 there), and replaces only the epsilon distance test by bit equality
under an equivalence gate:

1. **Equivalence gate.** Each vertex's three position bit patterns, with `-0.0`
   normalised to `+0.0`, form its exact-equality class. `ok` is returned only when
   every pair of *distinct* positions is further apart than `2 * epsilon`, so
   D3DX's `dx*dx+dy*dy+dz*dz < epsilon^2` test (x87, on a vertex pair within the
   key window) is true exactly for class members. The gate is first tried as a
   grid test: all coordinates are integer multiples of the power of two `2^(E+2)`
   where `epsilon = 1.m * 2^E` (for `1e-6` that is `2^-18 = 3.8e-6`; the engine's
   `2^-14` grid passes trivially, `quantized=1` in the telemetry). Otherwise a
   cell hash of size `4 * epsilon` over the distinct positions checks the 27
   neighbouring cells; any pair within `2 * epsilon` returns `epsilon_neighbour`
   (native fallback), and coordinates beyond `2^30` cells return `magnitude`.
   NaN or infinite components (position or byte-0 triple) return `non_finite`.
   Epsilon must be non-negative and finite with a normal `epsilon^2` (`input`
   otherwise); `epsilon = 0` needs no gate.
2. **Point representatives (D3DX rules 1-3).** For `epsilon > 0` the vertices are
   sorted by the float at **byte 0** of each vertex with D3DX's binary heap sort
   (descending, the heap's own permutation among equal keys) and swept in that
   order: an unwelded vertex becomes a representative and welds every later,
   still unwelded member of its class whose key lies within epsilon below its
   own, unless some face references both raw indices (**weld refusal**; the
   vertex then stays unwelded until the sweep reaches it). Welding is onto the
   representative only, never transitive. For `epsilon = 0` the vertices are taken
   in index order and a vertex joins the most recent representative of its class
   that shares no face with it. *Shortcut:* when the position is the first vertex
   element (so class members share the key) and no face references two distinct
   vertices of one class (so no refusal can occur), each class welds whole onto
   whichever member the heap would pick, and the output does not depend on which
   one (faces compare representatives only for equality and the score normals
   read identical positions); the sort is then skipped and the class's lowest
   index is used. Any face touching two copies of a position, or a layout with
   another element before the position, takes the full heap sort and sweep.
3. **Edges (rule 3).** Every face corner becomes its representative
   (`index_range` if an index exceeds the vertex count; a 16-bit mesh with more
   than 65,535 vertices returns `input`, because D3DX's 16-bit class treats a
   face whose first index is `0xffff` as absent, review 28). A face whose three
   representatives are not distinct, through raw repeats or welding alike,
   inserts nothing and receives `-1,-1,-1`. The other faces insert their three
   directed edges `(r[k], r[k+1], other, face)` into a table keyed by the exact
   pair with **head insertion**, so the chain order among the entries of one
   pair is D3DX's (most recent first; D3DX chains by `v1 mod V/3` and filters on
   the pair).
4. **Pairing (rules 4-5)**, face by face, slot by slot, skipping slots already
   filled by an earlier face's write: the reverse pair `(r[k+1], r[k])` is looked
   up; the first live entry of the chain is the candidate unless a later entry's
   score is strictly larger, where the score is the dot product (x87 order `z, x,
   y`, rounded to float) of the two faces' normals `normalize(cross(p1-p2, p1-p3))`
   built from the **byte-0 triples** of the entry's and the query's corners and
   normalised as D3DX's SSE2 `D3DXVec3Normalize` does (`rsqrtss` and one Newton
   step, zero below `2^-46`). The selected entry is retired inside the lookup
   (lost even when refused afterwards); the querying edge's own entry is retired
   only after a **successful** lookup; if the selected face already occupies an
   **earlier** slot of the querying face the slot stays `-1` and nothing is
   written to the other face; otherwise both faces are written.
5. **Attribute table (rule 6)** and the last-`POSITION` declaration rule live in
   the caller (`loading_trace.cpp`): a mesh whose attribute table does not cover
   the faces contiguously in order, or whose declaration has an element outside
   stream 0 (D3DX's parse ignores the stream number), falls back to native.

The `Policy` struct exposes the distinguishable alternatives (`head_insertion`,
`normal_selection`, `weld_refusal`, `heap_order`, `retire_own_entry`,
`unlink_refused`, `later_slot_check`) so that the fixture shows which ones differ
from d3dx9_37; the defaults are D3DX's. `Report` counts representatives, welded
vertices, refused welds, degenerate and welded-degenerate faces, multi-candidate
chains, normal selections, repeated neighbours and unmatched slots.

## D3DX equivalence: evidence and limits

`verification/probe/mesh_adjacency_fast_fixture.cpp` (built and run by
`verification/probe/run_loading_trace.py`, cases `mesh-adjacency-cache-off` and
`mesh-adjacency-cache-on`, the game's own `d3dx9_37.dll` (SHA-256 `c2ccb84c…`,
identical in both bottles) loaded with `d3dx9_37=n,b`, builtin d3d9, no game)
creates original meshes with `D3DXCreateMesh`, calls the uninstrumented native
`GenerateAdjacency`, runs the module on the locked buffers and compares byte for
byte; then it installs the production hooks and drives `verify` and `fast`
through the shared vtable slot, with the cache off and on.

Direct fixture runs on 2026-09-12 after the parity rewrite (the same binary,
`wine_lock.py`, cache off; Steam = x86_64 Wine under Rosetta 2, X3 = arm64 Wine
with FEX, `FEX_X87REDUCEDPRECISION=1`), both bottles identical in every
correctness field:

* **53 named cases, 51 computable, 51 byte-identical to d3dx9_37, 0 mismatching
  entries**; `nan-position` (`non_finite`) and `unquantized-near-1.5e-6`
  (`epsilon_neighbour`) fall back to native by design (their `default` policy
  lines print `equal=0`). The 16 cases added for the D3DX rules (weld refusal,
  heap order, entry lifetime, key window with the position at offset 8, epsilon
  0, fan-eight) all equal native, and every alternative policy variant that the
  fixture prints for the tie-evidence cases (`tail_insertion`,
  `no_normal_selection`, `no_weld_refusal`, `index_order_sweep`,
  `retire_own_entry`, `keep_refused_entry`, `later_slot_check`) is
  distinguishable from native on at least one case.
* **Random differential sweep**: `ADJACENCY_RANDOM trials=2000 equal=2000
  mismatched=0 multi_candidate_meshes=117 refused_weld_meshes=460` on both
  bottles: 2,000 small meshes over a 3x3x2 grid with frequent duplicate
  positions, degenerate and reversed faces, 16- and 32-bit indices, epsilon
  `1e-6` or 0 and TEXCOORD-first layouts with random byte-0 keys. 117 of them
  exercise the normal score (several candidates on one edge) and 460 the weld
  refusal. Before the byte-0 normal rule the same sweep had 2 mismatches, both
  with the position at offset 8 ([handoff-adjacency-parity.md](handoff-adjacency-parity.md)).
* Dump round trip: the fixture writes `fan-tilted-abc` as
  `mesh-adjacency-selftest.bin` (the game's `X3M_MESH_ADJACENCY_DUMP=1` format),
  reads it back, and its `replay` mode reproduces native; the runner then feeds
  the same file to `tools/analysis/replay_mesh_adjacency.py` on the host
  (`REPLAY_SUMMARY dumps=1 failures=0`).
* State sweep (`FP_STATE_NATIVE`): d3dx9_37's output is identical and
  deterministic under x87 control `0x027f`/`0x007f`/`0x037f` and MXCSR
  `0x1f80`/`0x9fc0`/`0x9f80`/`0x1fc0` on six meshes (42/42 equal to the module on
  both bottles).
* Hook path: verify mode `verify_mismatched=0` over the 53 cases, fast mode
  computes every computable case and falls back on the two others, LastError and
  the computational state preserved; with the cache on the second fast pass is
  served entirely from the cache (`FAST_CACHE second_pass_hits=53`). Inventory:
  cache off 33,272 checks, cache on 33,328.
* **`rsqrtss` under FEX.** D3DX's SSE2 `D3DXVec3Normalize` and the module's
  `_mm_rsqrt_ss` execute the same instruction on the same machine, so the
  approximation's bits agree by construction whether the emulator's `rsqrtss`
  matches Intel hardware or not; the X3 run's 117 multi-candidate random meshes
  and the fan cases equal native under FEX exactly as under Rosetta. The module
  therefore keeps `rsqrtss` (a host-side `1/sqrt` would match D3DX only where
  the two happen to round alike). Whether FEX's and Rosetta's `rsqrtss` agree
  with each other is recorded below (*rsqrtss probe*).
* Timing, uninstrumented native versus module on the locked buffers (direct run,
  QPC, single call each). Steam (Rosetta): `grid-224x224-32` 99,458 faces 1.26 s
  versus 21.8 ms before the performance fix below; X3 (FEX): D3DX itself is
  28x faster than under Rosetta (44.7 ms grid, 87.7 ms split-150, 238 ms
  star, 29.5 ms split-16), so the module's advantage there is 3-43x. The
  post-fix numbers are in the suite record.

Limits of the argument:

* The equivalence gate makes `fast` exact for any input it accepts; inputs it
  rejects go to D3DX unchanged. It never reproduces D3DX's welding of *distinct*
  positions within epsilon.
* The normal score is reproduced operation by operation (float stores, `rsqrtss`
  plus one Newton step, x87 dot order) but the cross product and dot product
  use double where D3DX uses the x87 stack at the caller's precision (53-bit in
  the game, 64-bit under `FEX_X87REDUCEDPRECISION=0`); for the engine's 24-bit
  quantized coordinates the products are exact in double, so only near ties of
  two candidate scores within a rounding error of the 80-bit path could differ.
  The telemetry counts `multi_candidate_meshes` and `normal_selected`.
* Keys that are not the position (another element before `POSITION`) and NaN
  byte-0 triples: the former take the full heap sort and window sweep (verified by
  the offset-8 cases and the random sweep), the latter return `non_finite`.
* `verify` remains the acceptance test on the game's own meshes (bottle X3,
  `--telemetry --mesh-adjacency verify --mesh-adjacency-dump`); its last
  `mesh_adjacency_metric` line must show `verify_mismatched=0` before `fast` is
  used, and any `mesh-adjacency-<n>.bin` it writes goes through
  `tools/analysis/replay_mesh_adjacency.py --wine` first.

## Run 8 and the parity fix (2026-09-12)

Bottle X3 run 8 (`--telemetry --mesh-adjacency verify`, the review-25 module)
ended with `verify_meshes=7715 verify_equal=7548 verify_mismatched=167
verify_mismatch_entries=26217`, mismatches also on meshes with
`multi_candidates=0 degenerate_faces=0`, so not near ties. The root cause was
that the review-25 module had inferred D3DX's tie and degeneracy rules from
fixture cases rather than from the binary; the disassembly of the game's
`d3dx9_37.dll` ([d3dx-generate-adjacency.md](../reverse-engineering/d3dx-generate-adjacency.md))
gave six rules it got wrong: (1) the sweep key is the float at byte 0 and the
order is D3DX's heap permutation, not index order; (2) weld refusal when a face
references both vertices, instead of the "larger raw index invalid" rule; (3) a
face whose representatives repeat inserts nothing, also when only welding makes
it degenerate; (4) entry lifetimes: the selected entry is lost even when refused,
the own entry survives a failed lookup, the single-adjacency check reads earlier
slots only; (5) the normal score reads the byte-0 triple and normalises with
`rsqrtss` plus one Newton step; (6) the position element is the last `POSITION`
and faces follow the attribute table. The module, the Python reference port and
the fixture were rewritten to those rules; the evidence above is the result. The
game acceptance run (item 5 of the handoff) is still to be made by the user.

## Computational state

The game enters `GenerateAdjacency` with x87 control `0x027f` (53-bit precision)
and MXCSR `0x9fc0` (FTZ+DAZ), as loading run 2 showed. The verify and fast
services capture that state, load the default MXCSR `0x1f80` for the module's
own SSE2 arithmetic, and restore the caller's x87 environment, MXCSR and
LastError before returning (the hook's `CpuCallBoundary` restores the full x87
state around the whole call as before). The first incoming state is logged once
as `mesh_adjacency_fp_first`. The adjacency cache keys that state instead of
refusing it ([mesh-adjacency-cache.md](mesh-adjacency-cache.md)). The fixture's
`GAME_FP_STATE` case drives six meshes through verify and fast with exactly that
state and checks identical output, the restored state and zero cache
`floating_point` bypasses.

## Telemetry

`mesh_adjacency mode=... equivalence=d3dx_rules+exact_position_equality gate=...
dump= rsqrt=rsqrtss` at initialization; `mesh_adjacency_metric cumulative=1
mode=... calls= computed= fallbacks= faults= faces= vertices= fast_ticks= fast_us=
native_ticks= native_us= verify_meshes= verify_equal= verify_mismatched=
verify_entries= verify_mismatch_entries= quantized= unquantized= welded_vertices=
multi_candidate_meshes= normal_selected= degenerate_edges= fallback_<reason>=
module_<status>=` with the periodic telemetry summary (only when the counters
changed); `mesh_adjacency_gate reason= count=` for the shared public-buffer gate
when the cache is off (the cache prints them as `mesh_cache_gate` otherwise);
`mesh_adjacency verify faces= vertices= equal=0 mismatches= first= native= fast=
native_us= fast_us= ... welded_degenerate_faces= refused_welds=
repeated_neighbours=` per mismatching mesh (bounded). `native_us` in verify mode
is the D3DX time of the same call, so the speed-up on the game's meshes is
`native_us / fast_us` of that line. With `X3M_MESH_ADJACENCY_DUMP=1`
(`--mesh-adjacency-dump`, verify mode only) every mismatching mesh is also
written, up to 256 per process, as `<module dir>\x3-modern-captures\mesh-adjacency-<n>.bin`
(`AdjacencyDumpHeader`, 64 bytes, then declaration, vertex bytes, index bytes,
native and module adjacency; game data, never committed) and logged as
`mesh_adjacency_dump index= ... written=`; `tools/analysis/replay_mesh_adjacency.py
[--reference] [--wine] <dumps or directory>` replays them through the host
module, the Python port and, with `--wine`, the fixture's `replay` mode against
the real d3dx9_37 under the runner lock, printing the mismatching entries with
the faces' indices and positions and which policy variant reproduces native.

## Performance pass (2026-09-12)

Measured with a host benchmark that includes the module source directly
(arm64 clang `-O2`, 20 repeated calls per mesh, minimum and mean; record
`verification/results/mesh-adjacency-fast-host-benchmark.txt`) on the fixture's
four timing meshes, an unquantized grid (cell-scan gate) and a pathological fan
where every face shares one directed edge (all reverse candidates in one chain).
Output checksums are identical before and after on every mesh; the 12 host tests
and the Wine suites above pass on the new code.

* **Allocations.** Before: 13 `malloc`s per call (positions, keys, representatives,
  the vertex table, corners, active, valid, a 24-byte `Edge` per corner, 8-byte
  slot keys, slot heads, dropped flags, plus the cell hash on the unquantized
  path). After: the byte layout is computed up front and carved from **one arena**
  (bump allocation; the representative table and the cell hash are phase scratch
  that is rewound and reused). The arena is **thread-local and retained** between
  calls when it is at most 16 MB (`release_scratch()` frees it; a larger arena is
  released after its call), so a loading burst pays the allocation and the
  first-touch page faults once. The per-corner data shrank from 24 + 12 bytes to
  4 + 12 (the edge id `face * 3 + point` names its corners, so only the chain
  link remains; slots hold an anchor edge id and a chain head, 8 bytes instead of
  12), and `keys` was folded into `positions` (the normalised bit patterns are
  the key). Arena for the 99,458-face grid: 12.6 MB; for a 20,000-face mesh:
  about 2.5 MB. Face normals for candidate selection are cached per edge in a
  lazily allocated block that exists only for meshes with a multi-candidate
  chain (none of the timing meshes; `multi_candidate_meshes` in the telemetry);
  if that block cannot be allocated the normals are recomputed per candidate
  instead of failing the call (review 28: the output is written whole only on `ok`).
* **Chain retirement.** Entries are no longer unlinked (a chain walk per query);
  a retired flag hides them from scans, so retiring the querying face's own
  entry is O(1).
* **Numbers** (min of 20 calls, before -> after): grid 5.39 -> 4.46 ms (1.21x),
  split-150 2.11 -> 1.92 ms (1.10x), star 1.04 -> 0.90 ms (1.16x), split-16
  1.03 -> 0.82 ms (1.25x), unquantized grid 6.97 -> 6.95 ms (the 27-cell scan
  dominates; unchanged), fan-same-edge-2000 12.6 -> 6.25 ms and
  fan-same-edge-8000 199 -> 99 ms (**2.0x**, the normal cache: candidates cost
  one dot product instead of a cross product, square root and three divisions).
* **Hash quality.** Linear-probe statistics with the module's hashes and table
  sizes (load factor at most 1/2): position table mean 1.02-1.47 probes per
  insertion, maximum 3-15 (grid 50,176 distinct keys: 1.31 / 15; the game's
  split layout 133,206 vertices / 22,500 distinct: 1.02 / 3); directed-edge table
  mean 1.11-1.43, maximum 8-25 (grid 298,374 keys in 1,048,576 slots: 1.20 / 12;
  star 60,000 keys in 131,072: 1.43 / 25). These match the random-hash
  expectation for the load factors, so the multiply-and-fmix64 hashes of the
  2^-14 grid bit patterns show no structure; the finalizer is unchanged.
* **Worst case.** The star (one welded centre shared by 20,000 faces) is linear
  in the module, 0.9 ms host / 2.7 ms Wine: its directed edges are all distinct
  pairs, so no chain is long; D3DX's quadratic bucket is what the 288x measures.
  The module's own quadratic case is many faces on one directed edge (every
  query scans the whole chain, as D3DX's rule requires: the best normal among all
  candidates): 8,000 such faces cost 99 ms after the pass, 16x the cost of
  2,000 (quadratic), which is inherent to the rule and does not occur in the
  game's welded layouts (a mesh with such an edge is counted in
  `multi_candidate_meshes`).
* **FP exception state.** Both services (`adjacency_fast_service`,
  `adjacency_verify_service`) load MXCSR `0x1f80` (all SSE exceptions masked,
  flags cleared) before `adjacency_compute` and restore the caller's x87
  environment and MXCSR afterwards; the module object contains no x87 opcode
  (`objdump` audit of `mesh_adjacency_fast.cpp.obj`: 0 x87 mnemonics; its
  undefined symbols are `malloc`, `free`, `memset`, `__emutls_get_address`,
  `__cxa_thread_atexit`), so a pending unmasked x87 exception in the caller's
  state cannot fire inside the module, and `check_no_x87.py build/d3d9.dll`
  passes (0 violations). The fixture's `GAME_FP_STATE` case (x87 `0x027f`, MXCSR
  `0x9fc0`) and the native state sweep cover the game's state.

### Parity rewrite: performance (2026-09-12)

The first parity build lost most of the review-25 gains: the fixture measured
grid 21.8 ms, split-150 18.8 ms, star 6.1 ms, split-16 7.6 ms (Steam, versus
11.3 / 5.1 / 2.7 / 2.5 ms in review 25), and the host benchmark (same meshes as
the fixture, arm64 clang `-O2`, min of 20 calls) 7.4 / 9.9 / 2.9 / 4.1 ms against
the review-25 module's 4.1 / 1.8 / 0.9 / 0.7 ms. A phase-timed copy of the
module put the cost in D3DX's heap sort (7.7 of 10.8 ms on split-150: `O(V log
V)` sifts, each reading a key through the index array) and in the chain walks of
the new `remove` (a second hash lookup per successful pairing). Three fixes,
output checksums identical on every benchmark mesh, module equal to the Python
port in the host tests:

* **Sort shortcut.** With the position at byte 0 and no face referencing two
  distinct vertices of one class, the heap permutation only picks which member
  represents a class, and nothing in the output depends on that (module section
  2); the sort is skipped and `rep = class head`. All four timing meshes and the
  engine's split layouts take this path; a mesh with a face touching two copies
  of a position takes the full sort.
* **Packed heap sort** for the remaining path: elements carry an order-preserving
  32-bit code of the key and the index, so a sift reads only the heap array
  (split-150 with one such face: 5.7 ms on the host, from 13.0 ms).
* **Retired flags** instead of unlinking, as in review 25: the selected entry and
  the own entry are hidden by a flag (clearing it is the relink of the
  `keep_refused_entry` variant); no second lookup.

Host benchmark after the fixes (min of 20 calls): grid 3.97 ms (review 25: 4.12),
split-150 1.90 (1.83), star 0.97 (0.91), split-16 0.92 (0.74),
fan-same-edge-2000 6.19 (5.91), split-150 with one forced full sort 5.70. The
fixture numbers of the rewritten module are in the suite record below.

## Host tests

`verification/analysis/test_mesh_adjacency_fast.py` (run with
`PYTHONPATH=verification/probe python3 -m unittest verification/analysis/test_mesh_adjacency_fast.py`;
pytest is not installed on this machine): 19 tests. The Python port
(`tools/analysis/mesh_adjacency_reference.py`, a literal port of the D3DX rules:
heap sort, key window, refusal, table lifetimes, byte-0 normals, float32
emulated per operation) is tested on the edge cases (shared edge, exact
duplicates, grid neighbours, signed zero, NaN/infinity, bad epsilon, index range,
unquantized gate, heap order, three faces on one edge under every policy,
degenerate faces, weld refusal, key window, head normals, epsilon zero, single
adjacency, own-entry survival), and the C++ module, built with the host
compiler through `verification/probe/mesh_adjacency_fast_host.cpp`, is
cross-checked against the port on those cases, on a split grid that pins the
sort shortcut (with and without a face forcing the full sort, and with a key
that is not the position) and on 80 random meshes with 16- and 32-bit indices
under all 8 policy variants, including mutual-pairing invariants.

## Suite record (2026-09-12, parity fix; bottle Steam unless stated)

Runs under `wine_lock.py` while another agent's review chain cycled the lock on
the same machine, so the fixture timings below are contended (D3DX's own grid
time was 1.33 s against 1.25 s in review 25); the host benchmark above is the
controlled comparison.

* `run_loading_trace.py`: PASS, loading-trace 85, loading-mesh 123,
  mesh-adjacency-cache-off **33,272**, cache-on **33,328**; both adjacency cases
  `ADJACENCY_RANDOM trials=2000 equal=2000 mismatched=0`, verify `verify_meshes=51
  verify_equal=51 verify_mismatched=0`, fast `calls=106 computed=102 fallbacks=4
  faults=0`, `FAST_CACHE second_pass_hits=53`; the self-test dump replay on the
  host `REPLAY_SUMMARY dumps=1 failures=0`. Timings (uninstrumented native
  versus module, cache off / cache on): `grid-224x224-32` 1.33 s versus 14.9 /
  15.6 ms (89x; review 25: 11.3 ms), `split-150x150-32` 2.45 s versus 6.7 / 6.0 ms
  (368-408x; 5.1 ms), `star-20000-32` 0.82 s versus 2.8 / 2.7 ms (293x; 2.7 ms),
  `split-16` 0.70 s versus 2.7 / 2.5 ms (257-274x; 2.5 ms). Records
  `verification/results/mesh-adjacency-cache-{off,on}-fixture.txt`,
  `loading-trace-mesh-summary.json`.
* `X3M_FIXTURE_BOTTLE=X3 run_loading_trace.py` (bottle X3, arm64 Wine + FEX,
  `FEX_X87REDUCEDPRECISION=1 WINEMSYNC=1`, uncontended, 40 s wall): PASS, the same
  inventory 85 / 123 / **33,272** / **33,328**, `mismatched=0`, `verify_meshes=51
  verify_equal=51 verify_mismatched=0`, `FAST_CACHE second_pass_hits=53`, replay
  `dumps=1 failures=0`. Timings (cache off / on): grid 45.2 ms native versus 12.2 /
  11.6 ms (3.7x), split-150 90.9 ms versus 5.0 / 4.6 ms (18x), star 246 ms versus
  2.4 ms (101x), split-16 29.9 ms versus 2.1 ms (14x): D3DX's x87 sweep is 28x
  faster under FEX than under Rosetta, the module about the same, so the gain on
  the game's bottle is 4-100x rather than 90-400x. Records under
  `verification/results/bottle-X3/`.
* Direct fixture runs (cache off, before the performance fix): Steam and X3 both
  `checks=33272 failures=0`, `mismatched=0`, 42/42 state-sweep lines equal
  (*D3DX equivalence* above). Direct Steam rerun after the fix with the lock and
  CPU quiet (`checks=33272 failures=0`, `mismatched=0`): grid 1.22 s versus
  **11.8 ms** (104x; review 25: 11.3 ms), split-150 2.25 s versus **6.1 ms**
  (370x; 5.1 ms), star 0.76 s versus **2.7 ms** (276x; 2.7 ms), split-16 0.64 s
  versus **2.5 ms** (260x; 2.5 ms): the 2-4x regression of the first parity build
  (21.8 / 18.8 / 6.1 / 7.6 ms) is removed; the remaining 1 ms on split-150 is the
  corner-chain build and the per-face class check that the D3DX refusal rule
  needs.
* `run_mesh_adjacency_cache.py`: PASS, 767 checks (unchanged).
* `run_mesh_cache_hook.py`: PASS, 1,714 / 2,003 / 2,011 / 2,189 / 2,673 / 2,681
  (13,271, unchanged).
* `PYTHONPATH=verification/probe python3 -m unittest verification/analysis/test_mesh_adjacency_fast.py`:
  19 tests OK; `unittest discover -s verification/analysis`: 757 tests OK (36 s).
* `build/d3d9.dll` (`cmake --build build -j4`, 0 warnings): SHA-256
  `3957f29f6c15b2fc5333246254e0ccf4ca8d1d07cbc27097e53498c5dcb43eb3`;
  `check_no_x87.py`: PASS, 13 roots (9 light hooks + 4 gz), 149 reachable
  functions, 0 violations. Not installed. (`loading_trace.h` needed a forward
  declaration of `ID3DXMesh` for the production build: the WIP header compiled
  only in the fixtures, which include d3dx9 first.)
* **rsqrtss probe** (a 245 KB standalone x86 executable, `_mm_rsqrt_ss` on 12
  sample inputs plus a dense sweep of 173,000 mantissas in `[1, 4)`, run in both
  bottles under the lock; not tracked): Steam/Rosetta and X3/FEX print
  bit-identical results, `sample_checksum=c0285000 dense_checksum=48c40000`, and
  the samples are Intel's table values (`rsqrtss(1.0) = 0x3f7ff000`,
  `rsqrtss(2.0) = 0x3f34f800`), so on this machine both emulators reproduce the
  hardware instruction and the module's `rsqrtss` equals D3DX's on either bottle
  by construction and by measurement.
* **Review 28 rerun** ([review-28.md](review-28.md), 2026-09-12 late evening):
  after the review's three fixes (16-bit meshes above 65,535 vertices and
  multi-stream declarations fall back to native; a failed normal-cache
  allocation recomputes instead of failing the call) and a clean rebuild, every
  suite passed with unchanged inventories on both bottles: `run_loading_trace.py`
  Steam 85 / 123 / 33,272 / 33,328 and X3 the same, `ADJACENCY_RANDOM ...
  mismatched=0` and `verify_mismatched=0` in all four fixture records, replay
  `dumps=1 failures=0`; cache 767; hook 13,271; object lifetime 574, object
  trace 166; motion output PASS (110 runs); host tests 19 and discover 757 OK.
  Fixture timings (cache off / on): Steam grid 1.25 s versus 12.7 / 13.5 ms,
  split-150 2.26 s versus 5.7 / 5.3 ms, star 0.75 s versus 2.6 ms, split-16
  0.65 s versus 4.1 / 2.4 ms (contended lock); X3 grid 45.3 ms versus 10.6 ms,
  split-150 88.0 ms versus 5.5 ms, star 235 ms versus 2.3 ms, split-16 28.7 ms
  versus 2.0 ms. `build/d3d9.dll` after the runner's relink: SHA-256
  `4518259aee3203e9be6ac5a7885ca95319b1d65a9d7c1d68f73ca3fc132fc3e2`;
  `check_no_x87.py`: 13 roots, 149 reachable functions, 0 violations. Not
  installed.
* Open: the game acceptance run (`--telemetry --mesh-adjacency verify
  --mesh-adjacency-dump`, bottle X3, `verify_mismatched=0`), then `fast`.
