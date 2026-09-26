# Review 28: GenerateAdjacency parity rewrite (D3DX rules 1-6)

Independent review of the adjacency parity rewrite in the main checkout: HEAD
`b10d129` (the WIP checkpoint that carries the rewritten
`src/proxy/mesh_adjacency_fast.{h,cpp}`, the Python port
`tools/analysis/mesh_adjacency_reference.py`, the dump/replay path in
`src/proxy/loading_trace.{h,cpp}` and `tools/analysis/replay_mesh_adjacency.py`)
plus the uncommitted edits (packed heap sort and the sort shortcut, retired
flags instead of chain unlinking, the `ID3DXMesh` forward declaration, the
runner's 33,272 / 33,328 inventories and `ADJACENCY_RANDOM` assertion, the
docs). Read in full: `d3dx-generate-adjacency.md` (rules 1-6),
`mesh-adjacency-fast.md`, `handoff-adjacency-parity.md`, the module, its
header, the reference port, the replay tool, the host driver, the adjacency
part of `loading_trace.cpp` (compute, both services, the dump writer, the
shared buffer gate) and the test/runner diffs; result files were queried by
script. No game was launched. The review was paused once for an
account switch and resumed; every suite below was rerun on the fixed tree.

## Checklist

1. **Rules vs code.** Verified rule by rule against the decompiled rules and
   the Python port:
   - *Heap sort* (`heapsort`, `order_code`): the packed `(code, index)`
     comparisons `le: code[right] <= code[left]`, `lt: code[element] <
     code[child]` are D3DX's two comparisons; `order_code` is order-preserving
     for finite floats with `-0 == +0` (NaN/Inf keys are rejected earlier by
     `finite_bits` on the byte-0 triple). The `heap_order=false` variant sorts
     stably (ascending index among equal keys), which is the port's `sorted`.
   - *Key window*: the module tests `eps < head[v].x - head[w].x` (double, the
     game's 53-bit x87 control) along the class chain and breaks; since the
     chain is threaded in descending key order the break is equivalent to
     D3DX's monotone `j` boundary, and under the gate only class members pass
     the distance test, so the per-class walk visits exactly D3DX's welding
     candidates.
   - *Sort skip*: the argument holds. With the position at byte 0, class
     members share the key (window never cuts a class) and with no face
     referencing two distinct vertices of one class `shares_face` can never
     refuse, so every class welds whole onto its first member in sweep order.
     Replacing that member by the class's lowest index is a bijective
     relabelling of representative ids: phase 1 compares ids for equality;
     D3DX's bucket `v1 mod V/3` changes with the label but the lookup filters
     on the exact pair and the relative order of matching entries is insertion
     order regardless of bucket; the score normals read the byte-0 triples of
     the representatives, identical up to signed zero, and signed zeros cannot
     flip a strict `<` (they only produce `±0` terms). `order_matters` is
     exactly "some face references two distinct raw indices of one class".
     Pinned by `test_order_shortcut_matches_reference` (module vs the
     always-sorting port on a 6x6 split grid, both heap and stable orders,
     plus one forcing face and an offset-8 key).
   - *Weld refusal*, *repeated representatives insert nothing*, *entry
     lifetime* (selected entry retired inside the lookup before the
     single-adjacency check, own entry retired only after success, earlier
     slots only, `unlink_refused=false` clears the flag = D3DX's relink
     position), *normal score* (`len2` in float, zero below `0x28800000` =
     `2^-46`, `_mm_rsqrt_ss`, Newton `((3-(r*len2)*r)*r)*0.5`, cross product
     with float-stored edges and double-rounded-then-float components = x87
     53-bit double rounding, dot in `z,x,y` order rounded to float, strict
     `<`, byte-0 triples through `head`), *last POSITION wins*, *attribute
     ranges contiguous from 0 covering all faces*: all as documented; the port
     mirrors each (same heap, window, refusal, table lifetimes, f32-per-op
     normal, `-0.0 == 0.0` dict keys on the epsilon-0 path).
   - *SSE path*: `_mm_rsqrt_ss` is reached only through
     `adjacency_compute`, which both services call after `set_default_mxcsr()`;
     `adjacency_fast_service` restores the caller's x87 environment, MXCSR and
     LastError on its single exit before any native fallback, and
     `adjacency_verify_service` restores on all three exits (native failure,
     fault, normal). Observation (not a finding for the game's meshes): D3DX's
     normalize runs under the game's FTZ+DAZ (`0x9fc0`) while the module's runs
     under `0x1f80`; a difference needs a denormal normal component and an
     exact score tie, impossible on the `2^-14` grid (components are multiples
     of `2^-28`); the fixture's state sweep covers the game state.
   - **Finding 1 (medium, fixed)** `mesh_adjacency_fast.cpp` `generate`: D3DX's
     16-bit class treats a face whose first index is `0xffff` as absent in the
     adjacency stage; a 16-bit mesh with more than 65,535 vertices makes that a
     real vertex the module would pair. Added `!indices_32bit && V > 0xffff`
     to the `Status::Input` gate (native fallback). 32-bit `0xffffffff` was
     already `index_range`.
   - **Finding 2 (medium, fixed)** `mesh_adjacency_fast.cpp` pairing loop: a
     failed `malloc` of the lazily allocated normal cache returned
     `Status::Allocation` after `adjacency[]` had been filled with `-1` and
     partly written, violating the header's "written whole only on Ok"
     contract (harmless in production only because both callers then discard
     or overwrite the buffer). The cache is now optional: `NormalCache::acquire`
     tries once and `normal_of` recomputes `face_normal` per candidate when it
     is absent; no status can fail in the pairing phase any more.
   - **Finding 3 (low, fixed)** `loading_trace.cpp` `adjacency_compute`: D3DX's
     declaration parse does not check the stream number, the caller skipped
     elements with `Stream != 0` and could pick a different POSITION element
     than D3DX. Any element outside stream 0 now falls back to native
     (`fallback_declaration`); D3DXCreateMesh meshes are single-stream, so the
     engine's meshes are unaffected.
2. **Fallback gates.** Order: null checks, `cache_buffer_contract` (recorded
   vtable, the 11 contract slots, `D3DXMESH_SYSTEMMEM` required, only
   `SYSTEMMEM|32BIT|DYNAMIC|SOFTWAREPROCESSING` allowed so `VB/IB_WRITEONLY`
   meshes, which D3DX refuses, never reach the module; public VB/IB
   descriptors), declaration (last POSITION, offset within stride, now
   single-stream), attribute table (count, at most 16 ranges, contiguous from
   0, total = faces), sizes (< 2 GB each, output within 4 GB), READONLY locks
   with one unlock retry and the fault path, output/input overlap, then the
   module's own `input`/`index_range`/`non_finite`/`magnitude`/
   `epsilon_neighbour`/`allocation` statuses. Every failure returns before the
   output is used and the fast service calls native with the caller's state
   restored; the verify service compares only when `r.computed`. Both index
   widths are read through `D3DXMESH_32BIT`. With findings 1-2 no path leaves
   a partial table.
3. **Dump/replay.** Writes happen only in verify mode, only for a mismatching
   mesh, after one relaxed atomic load (no per-call cost when off), bounded by
   `adjacency_dump_limit = 256` on a monotonic index; the target is
   `<module dir>\x3-modern-captures\` (the bottle's game directory in
   production, `verification/probe/build/…` for the fixture, both outside the
   tracked tree; `verification/results/*.bin` is ignored as well). No runner
   or tool copies dumps into the repository (`grep x3-modern-captures`: readers
   only). Header layout `8s14I` (64 bytes) matches `AdjacencyDumpHeader` field
   for field; the replay tool checks magic, header size and trailing bytes. The
   self-test round trip (`fan-tilted-abc` written by the fixture, read back,
   replayed in Wine and on the host) is asserted by the runner
   (`REPLAY_SUMMARY dumps=1 failures=0`, recorded in both bottles' summaries).
4. **`struct ID3DXMesh;`** in `loading_trace.h`: compatible with the
   `DECLARE_INTERFACE_` definition (a `struct` in C++), needed because the
   production translation units include the header without `d3dx9mesh.h`;
   compiled in the production build and in the fixture build (which includes
   d3dx9 first). Runner: `MESH_ADJACENCY_CHECKS=(33272,33328)` equals the
   `MESH ADJACENCY RESULT` lines of all four recorded fixture files (Steam and
   X3, cache off/on); the `ADJACENCY_RANDOM trials=2000 equal=2000 mismatched=0`
   assertion and the self-test replay are recorded in both summaries. Cache
   767 and hook 1,714/2,003/2,011/2,189/2,673/2,681 (13,271) unchanged in the
   summaries.
5. **Performance pass.** One arena `malloc` per call, retained per thread up
   to 16 MB (`Retain` releases larger ones); `release_scratch()` is still
   called by nothing outside the module (review-25 open item stands; the
   `thread_local` destructor frees it at thread exit) — design question for
   the orchestrator: call it from the periodic report or at loading end, or
   accept ≤16 MB per loader thread as `status.md` states. The normal cache
   adds two `malloc`s only for multi-candidate meshes. The sort skip is one
   O(F) pass of three class comparisons; the packed sort reads only the heap
   array. Hash quality numbers in the doc are from the host benchmark record
   (`verification/results/mesh-adjacency-fast-host-benchmark.txt`, 2026-09-12
   18:03), not re-measured here.
6. **Docs.** The doc's inventories, random-sweep line, verify counts, replay
   summary and cache/hook counts match the recorded summaries. `status.md`
   keeps `fast` blocked until the user's verify run shows
   `verify_mismatched=0`; wording is right. The DLL hash in
   `mesh-adjacency-fast.md` (`3957f29f…`) was superseded by this review's build
   and the suite record there now carries the rerun.

## Suite results

Clean rebuild after the three fixes (`cmake --build build --clean-first -j4`,
0 warnings, `e26caa3d…`), then every Wine suite under
`wine_lock.py --holder review28`, one at a time, queued behind another agent's
`review27` chain (no game running; logs in the session scratchpad).

| Suite | Result |
|---|---|
| `run_loading_trace.py` (Steam) | PASS: loading-trace 85, loading-mesh 123, mesh-adjacency-cache-off **33,272**, cache-on **33,328**; both `ADJACENCY_RANDOM trials=2000 equal=2000 mismatched=0`, `verify_meshes=51 verify_equal=51 verify_mismatched=0`, `FAST_CACHE second_pass_hits=53`, self-test replay `dumps=1 failures=0`; timings (cache off / on): grid 1.25 s native vs 12.7 / 13.5 ms, split-150 2.26 s vs 5.7 / 5.3 ms, star 0.75 s vs 2.6 / 2.6 ms, split-16 0.65 s vs 4.1 / 2.4 ms (contended lock; 8.5 min wall) |
| `X3M_FIXTURE_BOTTLE=X3 run_loading_trace.py` | PASS: the same inventory 85 / 123 / 33,272 / 33,328, `mismatched=0`, `verify_mismatched=0`, replay `dumps=1 failures=0`; grid 45.3 ms native vs 10.6 / 10.7 ms, split-150 88.0 ms vs 5.5 / 5.6 ms, star 235 ms vs 2.3 ms, split-16 28.7 ms vs 2.0 / 2.1 ms (41 s wall) |
| `run_mesh_adjacency_cache.py` | PASS, 767 checks (unchanged) |
| `run_mesh_cache_hook.py` | PASS, 1,714 / 2,003 / 2,011 / 2,189 / 2,673 / 2,681 (13,271, unchanged) |
| `run_object_lifetime.py` | PASS, 574 checks (as review 26) |
| `run_object_trace.py` | PASS, 166 checks (as review 26) |
| `run_motion_output.py` | PASS (`{"passed": true}`), 110 runs exit 0; its own `--clean-first` relink produced the final DLL below (3.7 min wall) |
| `check_no_x87.py build/d3d9.dll` | PASS on the final `4518259a…`: 13 roots, 149 reachable functions, 0 violations |
| `test_mesh_adjacency_fast.py` (host) | 19 tests OK (7.6 s) |
| `unittest discover -s verification/analysis` | 757 tests OK (33.4 s) |

Final `build/d3d9.dll` SHA-256 (the fixed sources; the review's own clean
build hashed `e26caa3d…` and the runner's relink `4518259a…`, differing only
in the PE timestamp as in reviews 25/26; not installed):
`4518259aee3203e9be6ac5a7885ca95319b1d65a9d7c1d68f73ca3fc132fc3e2`.

## Open / for the orchestrator

- `release_scratch()` has no caller (design choice; see item 5).
- The FTZ/DAZ observation in item 1 is a documented residual, not a code
  change; it cannot affect the engine's quantized meshes.
- The game acceptance run (bottle X3, verify mode, `verify_mismatched=0`)
  remains the user's, as `status.md` says; `fast` stays off until then.

**Verdict:** the rewrite implements the six decompiled rules faithfully; the
three findings are conservative gates/contract fixes that change no fixture
output (all inventories and the random sweep unchanged, 0 mismatches on both
bottles). Ready for the checkpoint commit; the in-game verify run is the
remaining acceptance step before `fast`.
