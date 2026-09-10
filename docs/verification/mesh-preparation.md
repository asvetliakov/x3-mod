# Standalone mesh adjacency reuse

The original x86 fixture demonstrates byte-exact reuse of successful GenerateAdjacency output for its synthetic meshes, with input invalidation and a bounded retained cache. It does not modify the game, install hooks, or demonstrate faster game loading.

This follows the [loading observation](../reverse-engineering/loading-observations.md) that a native sample landed in D3DX mesh adjacency preparation. The engine's validated sequence uses approximately 1e-6 epsilon, D3DXCleanMesh flags 3 and OptimizeInplace(D3DXMESHOPT_VERTEXCACHE). The fixture calls those public APIs; it contains no copied engine code or game meshes.

## Fixture and checks

`verification/probe/mesh_preparation.cpp` creates an unseen standalone D3D9 device through the builtin runtime and loads the exact local D3DX DLL by absolute path. The Python runner checks SHA-256 `c2ccb84c672a9d8966e82a28005a4269886ee304972ac3590c0b8a9c1622a3d8` before launch. Its `d3d9=b` override is process-local. No Present, game launch, game input, registry edit or installation occurs.

For each original mesh, it runs fresh computation, cache population, and cache reuse. It compares HRESULTs, generated and cleaned adjacency, cleaned vertex/index/attribute/declaration data, optimized data, optimized adjacency, face remaps and vertex remaps exactly. The four valid meshes also have successful cleaning/optimization; the degenerate case preserves failure rather than claiming successful output.

| Original mesh | Generated adjacency | Downstream result |
| --- | --- | --- |
| Closed tetrahedral manifold | All 12 directed edges linked | Exact successful cleaning/optimization parity |
| Two triangles with duplicated seam vertices and differing UVs | 2 directed edges linked despite distinct indices | Exact successful parity |
| Same seam with ~5e-7 positional separation | 2 directed edges linked at 1e-6 epsilon | Exact successful parity |
| Disconnected triangles | No directed edges linked | Exact successful parity |
| Triangle with repeated vertex index | Successful adjacency generation | CleanMesh returns 0x88760b55 identically; OptimizeInplace is not called |

Seven mutation checks change position, index topology, vertex declaration, epsilon, UV payload, runtime identity and mesh options independently. Each misses the original entry and matches a fresh complete sequence. Attributes are deliberately excluded from the adjacency key: a changed face attribute still hits adjacency while cleaning and optimization run on the **current** mesh and produce the same result as the fresh path. Mutating the returned adjacency copy does not modify retained cache data.

The fixture uses 16-bit indices, one vertex stream, FLOAT3 positions and FLOAT2 texture coordinates. It does not establish correctness for every declaration, index format, nonmanifold input or concurrent caller. Public-API behavior on these original cases is the evidence; no claim depends on guessed engine calling conventions.

The final run passed **1,218 checks** with zero failures.

## Cache identity and memory

The cache stores adjacency only. Keys contain the complete vertex and index bytes, full declaration including its terminator, exact epsilon bits, counts, stride, mesh options, schema and DLL fingerprint. Comparison uses full bytes; there is no hash-only identity or pointer identity. Exact-bit keys can conservatively miss semantically equivalent meshes, which is safer than silently reusing different geometry.

Only S_OK adjacency results are admitted. Failures and other successful HRESULTs bypass admission, preserving their original status. Cleaning and optimization are never skipped. The engine's adjacency failure fallback is outside this experiment.

There are at most eight retained entries. Entry structures and vector payload capacities are accounted against **2,097,152 bytes**; allocator bookkeeping is not included. The fixture exercises both slot-count eviction and byte-budget eviction, checks the bound after admission, verifies an evicted key misses and a retained key hits, and rejects oversized inputs/results. Byte-size admission arithmetic uses 64-bit intermediates on x86.

This is a bound on retained cache storage, not total process memory. Temporary key creation and admission candidates have separate bounded allocations, and current mesh/output/snapshot storage remains caller-owned. At most one admission candidate is created at a time; it must independently fit the retained payload limit. A production implementation still needs an explicit total working-set policy and allocation-failure fallback suitable for a 32-bit game. The fixture is single-threaded, uses round-robin eviction, and has no persistent disk cache.

## Timing interpretation

The timing mesh is a 96×96 grid: 18,432 faces and 9,409 vertices. Twenty-one alternating measurements compare repeated uncached GenerateAdjacency against key serialization, full-key lookup and allocation/copy of cached output. Both paths use a warmed DLL and mesh; “cold” in the result means **cache miss computation**, not cold filesystem/OS startup. The uncached measurement uses an already-sized output buffer, while reuse includes output allocation.

Seven additional pairs compare the complete fixture sequence, including creating/filling the mesh, cleaning, optimization and verification snapshots in both paths. Each pair checks exact large-mesh parity. These timings include fixture verification work and do not isolate production API-hook costs. The synthetic vertex/index bytes are already available in host memory; acquiring a game's live buffers and synchronizing them would add unmeasured cost.

The final run measured:

| Measured work | Uncached median | Reuse median |
| --- | ---: | ---: |
| Adjacency versus key/lookup/copy | 96.680 ms | 0.136 ms |
| Complete fixture sequence | 101.524 ms | 4.455 ms |

The grid entry retained 520,264 accounted bytes. These differences demonstrate a synthetic benefit when an identical mesh is reused; the cache miss has no such benefit.

Exact medians/ranges, binary/source fingerprints and verification counts are in [mesh-preparation-summary.json](../../verification/results/mesh-preparation-summary.json); raw numeric evidence is [mesh-preparation.txt](../../verification/results/mesh-preparation.txt). Timing is one local run, not a confidence interval or a forecast of game loading improvement.

## Reproduction and next gate

```sh
sh verification/probe/mesh_build.sh
python3 verification/probe/mesh_run.py
```

The runner has a bounded timeout, checks case/mutation/result records and writes only fixture results. Its original meshes are generated in source, and generated executables remain ignored.

This establishes a viable optimization mechanism for repeated identical meshes. It does not establish that X3 repeatedly prepares identical inputs, how much time the mesh stage consumes across the long presentation gap, or whether a small cache achieves useful hit rates. Before integrating, obtain those quantities as part of an already-coordinated diagnostic session and preserve mesh lifetime, current attributes, failure behavior, thread safety and 32-bit memory pressure. No extra gameplay test was requested for this fixture.
