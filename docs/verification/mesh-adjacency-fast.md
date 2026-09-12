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

Pure code (no Windows headers, no globals, `malloc` scratch, SSE2 only; the
production object has no x87 opcode and imports only `malloc`, `free`, `memset`).
Input: the locked vertex bytes with the `FLOAT3` position offset and stride, the
locked 16- or 32-bit indices, the face count and the epsilon. Output:
`DWORD[3 * faces]`, `0xffffffff` where a face has no neighbour across an edge.

1. **Representatives.** Each vertex's three position bit patterns, with `-0.0`
   normalised to `+0.0`, are hashed; a vertex maps to the first vertex with the same
   key. NaN or infinite components return `non_finite` (D3DX's behaviour on such
   input is not reproduced; the caller falls back to native). `-0 == +0` because
   D3DX compares distances: `(+0) - (-0) = 0 <= epsilon`, confirmed by the
   `signed-zero` fixture case.
2. **Equivalence gate.** `ok` is returned only when every pair of *distinct*
   positions is further apart than `2 * epsilon`, so the epsilon neighbourhood of any
   vertex holds only bit-identical positions (a 4x margin on the squared distance
   D3DX compares in 24-bit x87 arithmetic). The gate is first tried as a grid test:
   all coordinates are integer multiples of the power of two `2^(E+2)` where
   `epsilon = 1.m * 2^E` (for `1e-6` that is `2^-18 = 3.8e-6`; the engine's
   `2^-14` grid passes trivially, `quantized=1` in the telemetry). Otherwise a
   cell hash of size `4 * epsilon` over the distinct positions checks the 27
   neighbouring cells; any pair within `2 * epsilon` returns `epsilon_neighbour`
   (native fallback), and coordinates beyond `2^30` cells return `magnitude`.
   Epsilon must be non-negative and finite with a normal `epsilon^2` (`input`
   otherwise); `epsilon = 0` needs no gate.
3. **Edges.** Every face corner becomes its representative (`index_range` if an
   index exceeds the vertex count). A face with a **repeated raw index** (`0,1,0`)
   contributes no edge and pairs with nothing. In a face that repeats a
   representative only through welding, **of two corners sharing a representative
   the one with the larger raw index is invalid** and both edges touching it are
   skipped: `(0,1,4)` with `4` welded to `0` keeps `0->1` only, `(0,4,1)` keeps
   `1->0` only, `(4,5,1)` keeps `1->4` only. The remaining directed edges
   `(v1, v2, other, face, point)` go into a table keyed by the exact pair, one
   chain per pair with **head insertion** (later faces first).
4. **Pairing**, face by face, point by point, skipping entries already filled:
   the reverse pair `(v_next, v_this)` is looked up; a degenerate edge stays
   unused. The first candidate in chain order is taken unless a later candidate's
   face normal (`normalize(cross(p1-p2, p1-p3))` from the edge's own corners,
   single precision; a zero normal scores 0) has a strictly larger dot product
   with the querying face's normal. The querying face's own entry is then retired.
   If the selected face is **already adjacent** to the querying face on another
   edge, nothing is written (the candidate's entry stays for other faces);
   otherwise the candidate's entry is removed and both faces are written, so every
   pairing is mutual and each directed edge pairs at most once.

The `Policy` struct exposes the observable rules (head insertion, normal
selection, raw-degenerate skip, welded-corner drop, single adjacency, plus the
DirectXMesh-style rep-degenerate skip) so that the fixture shows which
alternatives are distinguishable and differ from d3dx9_37.

## D3DX equivalence: evidence and limits

`verification/probe/mesh_adjacency_fast_fixture.cpp` (built and run by
`verification/probe/run_loading_trace.py`, cases `mesh-adjacency-cache-off` and
`mesh-adjacency-cache-on`, the real `d3dx9_37.dll` from the Steam bottle under
CrossOver Preview, builtin d3d9, no game) creates original meshes with
`D3DXCreateMesh`, calls the uninstrumented native `GenerateAdjacency`, runs the
module on the locked buffers and compares byte for byte; then it installs the
production hooks and drives `verify` and `fast` through the shared vtable slot,
with the cache off and on.

Direct runs on 2026-09-12 (`build/mesh_adjacency_fast/mesh_adjacency_fast_fixture.exe 0|1`,
outputs to be re-recorded by the suite under `verification/results/mesh-adjacency-cache-{off,on}-fixture.txt`):

* **37 cases, 35 computable, 35 byte-identical to d3dx9_37, 0 mismatching
  entries**; `nan-position` (`non_finite`) and `unquantized-near-1.5e-6`
  (`epsilon_neighbour`) fall back to native by design. Inventory: cache off
  2179 checks, cache on 2219 checks.
* Tie-breaking evidence (`fan-*`): three faces on one edge pair the most
  parallel normal (`fan-tilted-abc`: A pairs B, not the later C), equal normals
  keep the later face (`fan-coplanar-abc`: A pairs C), four faces pair
  best-normal then the remaining reverse edge in order (`fan-four`). The
  `tail_insertion` and `no_normal_selection` variants each mismatch native.
* Signed zero welds natively (`signed-zero`: 2 welded, same adjacency as exact
  duplicates); `grid-neighbours-6.1e-5` welds nothing; `epsilon-zero` and
  `position-offset-8` (POSITION after a TEXCOORD) equal native.
* Degenerate evidence: `degenerate-aba-aaa` and `degenerate-lookup` (raw
  repeats) give all `-1` natively although a reverse edge exists; the eleven
  `degenerate-welded-*` cases establish the larger-index rule (`041` versus
  `401`, `451` versus `541a/b`) and the zero-normal preference
  (`degenerate-welded-second`); the `rep_degenerate_skip` (DirectXMesh) and
  `no_welded_corner_drop` variants mismatch.
* `duplicate-faces` and `double-adjacency-order` establish the single-adjacency
  refusal after selection (`double_adjacency` variant mismatches).
* Timing, uninstrumented native versus module on the locked buffers (one run,
  QPC): `grid-224x224-32` 99,458 faces: 1.21 s versus 33.2 ms (**36x**; 8.8 ms
  and 143x in another run); `split-150x150-32` 44,402 faces / 133,206 split
  vertices: 2.31 s versus 10.8 ms (**213x**); `star-20000-32` 20,000 faces on
  one welded centre (D3DX's degenerate bucket): 0.78 s versus 4.7 ms (**166x**);
  `split-16` 19,602 faces: 0.68 s versus 4.0 ms (**168x**). Two-face meshes are
  1-3 us either way.
* Hook path: verify mode `calls=37 verify_meshes=35 verify_equal=35
  verify_mismatched=0`, both fallbacks counted with their module status; fast
  mode computes every computable case (`computed=70` over verify+fast,
  `fallbacks=4`, `faults=0`), preserves the caller's LastError, equals the pure
  module and the unhooked native method on fresh content; a MANAGED mesh falls
  back at the public gate; native mode leaves the service idle. With the cache
  on, the verify entries are keyed by the verify service, the first fast pass
  misses and computes, the second pass hits all 37 (`FAST_CACHE
  second_pass_hits=37`), and a module result is admitted and served
  (`admissions+1`, hit on repeat).
* State sweep (`FP_STATE_NATIVE`): d3dx9_37's output is identical and
  deterministic under x87 control `0x027f`/`0x007f`/`0x037f` and MXCSR
  `0x1f80`/`0x9fc0`/`0x9f80`/`0x1fc0` on six meshes (42/42 equal to the module).
  The game state case (`GAME_FP_STATE control=027f mxcsr=9fc0`): 12 hooked
  calls (verify and fast on six meshes) equal the unhooked native method, the
  state is restored after every call, the cache never bypasses (`floating_point`
  0) and keys the state instead.

Limits of the argument:

* The equivalence gate makes `fast` exact for any input it accepts; inputs it
  rejects go to D3DX unchanged. It never reproduces D3DX's welding of *distinct*
  positions within epsilon.
* Normal selection among three or more faces on one directed edge compares
  single-precision dot products computed with SSE; D3DX computes them in x87.
  An exact tie is resolved identically (bucket order); a near tie (two candidate
  faces whose normals differ from the querying face's by less than one float ulp
  of the dot product) could be resolved differently. The telemetry counts
  `multi_candidate_meshes` and `normal_selected` so a game run with `verify`
  shows whether such edges exist at all.
* `verify` is the acceptance test on the game's own meshes: the next user run
  should use `--telemetry --mesh-adjacency verify` and confirm
  `verify_mismatched=0` in the last `mesh_adjacency_metric` line before switching
  to `fast`.

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

`mesh_adjacency mode=... equivalence=exact_position_equality gate=...` at
initialization; `mesh_adjacency_metric cumulative=1 mode=... calls= computed=
fallbacks= faults= faces= vertices= fast_ticks= fast_us= native_ticks= native_us=
verify_meshes= verify_equal= verify_mismatched= verify_entries=
verify_mismatch_entries= quantized= unquantized= welded_vertices=
multi_candidate_meshes= normal_selected= degenerate_edges= fallback_<reason>=
module_<status>=` with the periodic telemetry summary (only when the counters
changed); `mesh_adjacency_gate reason= count=` for the shared public-buffer gate
when the cache is off (the cache prints them as `mesh_cache_gate` otherwise);
`mesh_adjacency verify faces= vertices= equal=0 mismatches= first= native= fast=
native_us= fast_us= ...` per mismatching mesh (bounded). `native_us` in verify
mode is the D3DX time of the same call, so the speed-up on the game's meshes is
`native_us / fast_us` of that line.

## Host tests

`verification/analysis/test_mesh_adjacency_fast.py` tests a Python port of the
algorithm (`tools/analysis/mesh_adjacency_reference.py`, float32 emulated per
operation) on the edge cases (shared edge, exact duplicates, grid neighbours,
signed zero, NaN/infinity, bad epsilon, index range, unquantized gate, three faces
on one edge under every policy, degenerate faces) and cross-checks the C++ module,
built with the host compiler through `verification/probe/mesh_adjacency_fast_host.cpp`,
against the port on those cases and on 60 random meshes with 16- and 32-bit
indices under all policies, including mutual-pairing invariants.
