# Handoff: fast GenerateAdjacency parity (paused 2026-09-12 night, account switch)

**Resolved 2026-09-12 (later session).** Remaining items 1-4 below are done; the
record is in [mesh-adjacency-fast.md](mesh-adjacency-fast.md) (*D3DX equivalence*,
*Run 8 and the parity fix*, *Parity rewrite: performance*, *Suite record*):
direct fixture runs and `run_loading_trace.py` on both bottles (Steam/Rosetta,
records `verification/results/`; X3/FEX, records `verification/results/bottle-X3/`)
give `ADJACENCY_RANDOM trials=2000 equal=2000 mismatched=0`, 51/51 computable
named cases equal to native and the inventories `cache=0 checks=33272` /
`cache=1 checks=33328` with `failures=0`; `rsqrtss` is bit-identical under FEX
and Rosetta (probe); no seventh rule was needed. The rewrite's performance regression
(heap sort and chain removal) was fixed without changing the output. Item 5, the
game acceptance run, is still the user's:
`python3 tools/manage.py launch --telemetry --mesh-adjacency verify --mesh-adjacency-dump`
on bottle X3 must end with `verify_mismatched=0`. Everything below is the
pre-resolution state, kept for provenance.


State of the working tree (uncommitted; compiles: the x86 module and fixture
objects build with `i686-w64-mingw32-g++ -Werror`, the host unit tests pass).

## Evidence that started this

Bottle X3 run 8 (`/tmp/x3-bottleX3-run8/session-20260912-194314-212.log`, verify
mode): `verify_meshes=7715 verify_equal=7548 verify_mismatched=167
verify_mismatch_entries=26217`, 64 per-mesh lines, mismatches also on meshes with
`multi_candidates=0 degenerate_faces=0` (so not near ties). The x87 control word
in the game is `0x023f`/`0x027f` (53-bit), MXCSR `0x9fc0`.

## D3DX rules derived from the binary (complete; see d3dx-generate-adjacency.md)

DLL: `C:\X3\d3dx9_37.dll` SHA-256
`c2ccb84c672a9d8966e82a28005a4269886ee304972ac3590c0b8a9c1622a3d8` (the copy
the game and the fixtures load; the bottle `system32`/`syswow64` copies are
Wine's builtins). Ghidra project `/tmp/x3-ghidra-d3dx` (D3DX), scripts in the
session scratchpad (`D3dxVtableDecompile.java`, `D3dxXrefs.java`,
`D3dxSqrtScan.java`, `D3dxBytes.java`); decompiler text under
`/tmp/x3-ghidra-d3dx/*.txt`. Vtables `0x00406108` (32-bit mesh) / `0x00406090`
(16-bit), slot 22 -> `0x005a2c56` / `0x005a27e4` -> worker `0x0059f2eb` /
`0x0059edc6`.

Differences from the pre-review-26 module (each one explains part of the 167):

1. **Sweep key and order**: vertices are heapsorted (`0x0058c02c`, descending,
   min-heap with `<=`/`<`, unstable permutation among equal keys) by the float at
   **byte 0** of the vertex, not the position x; the window is keys within
   epsilon below the current one.
2. **Weld refusal** (`0x0058f663`): a vertex never welds onto the current
   representative when some face references both raw indices; it stays
   unwelded until the sweep reaches it and becomes a representative itself.
   Welding is onto the representative only (not transitive). The old
   "larger raw index invalid" rule was an approximation of this.
3. **Degenerate faces**: a face whose three representatives are not distinct
   inserts no edge and gets `-1,-1,-1` (raw or welded alike); no partial insert.
4. **Entry lifetime** (`0x00590630`, `0x0058a8ff`): the selected candidate is
   unlinked inside the lookup (lost even when refused afterwards); the querying
   edge's own entry is removed only after a **successful** lookup (a failed lookup
   leaves it for later faces); the single-adjacency check compares only the
   **earlier** slots of the querying face.
5. **Normal score** (`0x005904bd`): cross(p1-p2, p1-p3) of the entry corners vs
   the query corners, `D3DXVec3Normalize` from the SSE2 table (`0x00756732`:
   `rsqrtss` + one Newton step, zero below `2^-46`), x87 dot `z,x,y` rounded to
   float, strict `<`; the vertices are read at **byte 0** with the stride (no
   position offset), i.e. the byte-0 triple, like the key.
6. Position element = the **last** POSITION/FLOAT3/usage 0 element; faces are
   walked per attribute-table range when a table exists.

## Implemented

* `src/proxy/mesh_adjacency_fast.{h,cpp}`: rewritten to rules 1-5 (equal-position
  classes walked in heap order under the 2-epsilon gate; `rsqrtss` via
  `_mm_rsqrt_ss` on x86, `1/sqrt` on the host; `#pragma STDC FP_CONTRACT OFF` for
  clang, GCC ISO mode). New `Policy` flags: `head_insertion, normal_selection,
  weld_refusal, heap_order, retire_own_entry, unlink_refused, later_slot_check`;
  `Report.refused_welds` replaces `dropped_edges`; `rsqrt_implementation()`.
* `tools/analysis/mesh_adjacency_reference.py`: literal port (heapsort, window
  sweep, refusal, table lifetimes, byte-0 `head_bits`).
* `verification/analysis/test_mesh_adjacency_fast.py`: 18 tests, all OK
  (`PYTHONPATH=verification/probe python3 -m unittest
  verification/analysis/test_mesh_adjacency_fast.py`), including every
  D3DX-verified fixture output of review 25 reproduced by the new rules and 80
  random meshes under all 7 policies (module == port).
* `verification/probe/mesh_adjacency_fast_host.cpp`: 7 policy flags, vertex line
  `x y z hx hy hz`.
* `verification/probe/mesh_adjacency_fast_fixture.cpp`: 16 new named cases
  (refusal, heap order, entry lifetime, key window with position offset 8,
  epsilon 0, fan-eight), a 2,000-mesh random differential sweep against native
  (`ADJACENCY_RANDOM`), a dump writer/reader round trip on `fan-tilted-abc`
  (`mesh-adjacency-selftest.bin` in the fixture's working directory) and a
  `replay <dumps...>` mode (`REPLAY_CASE`/`REPLAY_MISMATCH`/`REPLAY_POLICY`).
* `src/proxy/loading_trace.{h,cpp}`: `X3M_MESH_ADJACENCY_DUMP=1` writes each
  mismatching verify mesh (bound 256) as `<module dir>\x3-modern-captures\
  mesh-adjacency-<n>.bin` (`AdjacencyDumpHeader`, 64 bytes, then declaration,
  vertex bytes, index bytes, native and module adjacency), logs
  `mesh_adjacency_dump index= ... written=`; the verify line gained
  `welded_degenerate_faces= refused_welds= repeated_neighbours=`; the
  `mesh_adjacency mode=` line gained `dump= rsqrt=`; last-POSITION rule; an
  attribute-table gate (falls back unless the ranges cover the faces in order).
* `tools/manage.py --mesh-adjacency-dump` (requires `--mesh-adjacency verify`).
* `tools/analysis/replay_mesh_adjacency.py`: host replay of dumps (module +
  optional `--reference`; `--wine` runs the fixture replay under `wine_lock.py`),
  prints mismatching entries with face indices/positions and which policy
  variant matches native.
* Docs: `docs/reverse-engineering/d3dx-generate-adjacency.md` (rules, addresses),
  `docs/status.md` (fast mode blocked until `verify_mismatched=0`).

## Wine evidence so far (Steam bottle, direct fixture run, before the byte-0 normal fix)

`scratchpad/fixture-direct.txt` of this session: all 53 named cases equal native
(the two by-design fallbacks aside), including the new rule cases and the dump
self-test replay; random sweep `trials=2000 equal=1998 mismatched=2`, both with
position offset 8 -> diagnosed as rule 5's byte-0 read, now implemented in the
module and the port (host tests pass). Not yet rerun under Wine.

## Remaining (in order)

1. Rerun the fixture directly (expect `ADJACENCY_RANDOM ... mismatched=0` and
   `MESH ADJACENCY RESULT cache=0 checks=N failures=0`):
   ```
   sh verification/probe/build_loading_trace.sh
   WINEDLLOVERRIDES='d3dx9_37=n,b;d3d9=b' python3 verification/probe/wine_lock.py --holder mesh_adjacency_fast_direct '/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine' --bottle Steam --no-update --dll 'd3dx9_37=n,b;d3d9=b' --workdir verification/probe/build/mesh_adjacency_fast verification/probe/build/mesh_adjacency_fast/mesh_adjacency_fast_fixture.exe 0 > /tmp/fixture-direct.txt
   ```
   (`d3dx9_37.dll` is already copied next to the fixture by the earlier run.)
2. `verification/probe/run_loading_trace.py`: set `MESH_ADJACENCY_CHECKS` to the
   new cache-off/cache-on inventories (the old 2179/2219 fail now; the run above
   reported 32,152 checks at the point of the random failure), assert the
   `ADJACENCY_RANDOM` line (`mismatched=0`), and run
   `python3 tools/analysis/replay_mesh_adjacency.py verification/probe/build/mesh_adjacency_fast/mesh-adjacency-selftest.bin`
   after the cache-off case (expect `REPLAY_SUMMARY dumps=1 failures=0`).
3. Under the lock: `run_loading_trace.py`, `run_mesh_adjacency_cache.py`,
   `run_mesh_cache_hook.py`, `check_no_x87.py` on a fresh `build/d3d9.dll`,
   `PYTHONPATH=verification/probe python3 -m unittest discover`.
4. Update `docs/verification/mesh-adjacency-fast.md` (run-8 result, root cause =
   rules 1-5, fix, suite numbers, residual risk: `rsqrtss`/x87 near ties, keys
   that are not the position, NaN byte-0 triples fall back).
5. Game acceptance (user-launched, bottle X3):
   `python3 tools/manage.py launch --telemetry --mesh-adjacency verify --mesh-adjacency-dump`;
   the last `mesh_adjacency_metric` line must show `verify_mismatched=0`; any
   `mesh-adjacency-<n>.bin` in `x3-modern-captures` goes through
   `tools/analysis/replay_mesh_adjacency.py --wine <dir>` before touching the
   module again. Only then switch to `--mesh-adjacency fast`.
