# d3dx9_37 `ID3DXMesh::GenerateAdjacency`: the exact rules

Source: the game's own `C:\X3\d3dx9_37.dll` (PE32 i386, 3,786,760 bytes, SHA-256
`c2ccb84c672a9d8966e82a28005a4269886ee304972ac3590c0b8a9c1622a3d8`, the copy the
fixtures load with `d3dx9_37=n,b`; the bottles' `system32`/`syswow64` copies are
Wine's builtin stubs), imported into a private Ghidra project (`/tmp/x3-ghidra-d3dx`,
full analysis) on 2026-09-12. The DLL has no RTTI class names, so the mesh
classes were reached from `D3DXCreateMesh`: options bit 0 (`D3DXMESH_32BIT`)
selects the 32-bit class (vftable `0x00406108`), otherwise the 16-bit class
(vftable `0x00406090`); both tables have 30 slots and their slot 22 is
`GenerateAdjacency`. Function addresses below are image-relative to the
default base `0x00400000`; decompiler text stays under `/tmp`.

| Address | Role |
|---|---|
| `0x005a2c56` / `0x005a27e4` | slot 22 thunks (32-bit / 16-bit): pass the buffers, declaration, options, counts, attribute table and epsilon to the worker |
| `0x0059f2eb` / `0x0059edc6` | worker: point representatives, then adjacency |
| `0x0058c02c` | heap sort of the vertex indices by a float key |
| `0x0058f663` / `0x0058f535` | weld refusal: does a face reference both vertices |
| `0x00599543` / `0x00599494` | epsilon = 0 exact-position hash |
| `0x0059f24b` / `0x0059ed23` | lock and call the point-reps-to-adjacency converter |
| `0x00598226` / `0x00597e88` | point representatives to adjacency (also `ConvertPointRepsToAdjacency`) |
| `0x0058a8b2`, `0x00590630`, `0x0058a8ff` | directed-edge table: insert, lookup with normal selection and unlink, remove |
| `0x005904bd` | normal score of a candidate against the querying edge |
| `0x0056e9f5` | declaration parse (position element) |
| `0x00587e6b`, `0x00756246`, `0x00756732` | CPU dispatch of the math table; SSE2 table; its `D3DXVec3Normalize` |

## 1. Entry checks

* `D3DERR_INVALIDCALL` when the output pointer is null, the options carry
  `D3DXMESH_VB_WRITEONLY` or `D3DXMESH_IB_WRITEONLY` (`0x440`), or the
  declaration has no position element.
* The position element is the **last** stream element with usage `POSITION`,
  usage index 0 and type `FLOAT3` (the parse overwrites on every match; usage
  index 0 is required for every usage other than `TEXCOORD`/`COLOR`, and the
  stream is not checked). The vertex size comes from `D3DXGetDeclVertexSize`.
* The index and vertex buffers are locked read-only for the whole call. The
  16-bit class reads `WORD` indices; both classes treat a face whose **first**
  index is `0xffff` (16-bit) or `0xffffffff` (32-bit) as absent in the adjacency
  stage only (the representative stage would index out of range).

## 2. Point representatives (`rep[v]`)

Per-vertex corner chains are built first: for every face in index order and
every corner `k`, `next[3f+k] = head[index]; head[index] = 3f+k` (head
insertion on the **raw** index). They serve one predicate, **`shares_face(v, w)`**:
walk the corners of raw vertex `v` and return true when any of those faces
references raw index `w` (`0x0058f663`).

### epsilon > 0 (the engine passes `1e-6`)

1. `key[v]` = the **float at byte 0 of vertex `v`**, whatever element lives
   there (the position `x` only when the position is the first element).
2. `order` = the vertex indices sorted by `key` with the DLL's own **binary
   min-heap sort** (`0x0058c02c`): build the heap from `n/2-1` down to 0 and
   extract to the end; the child comparison is `key[right] <= key[left]`
   (choose the right child on ties) and the stop comparison is
   `key[element] < key[child]`. The result is in **descending** key order and the
   order among equal keys is the heap's permutation, not the index order
   (`[0,1,1,0,0,0]` gives `[2,1,3,4,0,5]`). Every later rule sees this order.
3. `rep[*] = -1`, `eps2 = epsilon * epsilon`, window pointer `j = 0`. For
   `i = 0..n-1` with `v = order[i]`:
   * advance `j` while `!(epsilon < key[v] - key[order[j]])` (x87 compare of
     the float difference; the window holds the keys within epsilon **below**
     `key[v]`, and `j > i` always);
   * if `rep[v] != -1` continue (already welded onto an earlier vertex);
   * `rep[v] = v`; for `m = i+1 .. j-1`, `w = order[m]`: if `rep[w] == -1` and
     `dz*dz + dy*dy + dx*dx < eps2` (strict, x87, position differences) and
     **not** `shares_face(v, w)`, then `rep[w] = v`.

   So welding is not transitive (a vertex welds only onto a representative
   directly within epsilon), a refused vertex stays unwelded until the sweep
   reaches it and makes it a representative for the vertices after it, and a
   vertex welded earlier is never re-examined. Which copy of a duplicated
   position becomes the representative is decided by the heap permutation
   (and by the key window when the key is not the position).

### epsilon == 0

Vertices in index order; hash bucket `(int(x) + int(y) + int(z)) mod n` over
20-byte entries `{x, y, z, vertex, next}` with head insertion. A vertex takes the
first entry of its bucket with the **bit-identical** position (float compare, so
`-0 == +0`) and no shared face, else it becomes a new entry and its own
representative. The hash never changes the outcome: equal positions always share
a bucket and the only order that matters is insertion order among them.

## 3. Representatives to adjacency (`0x00598226`)

Table: `V/3` buckets (integer division) of singly linked 20-byte entries
`{v1, v2, v3, face, next}`; bucket `v1 mod (V/3)`; **head insertion**. The face
loop follows the attribute table's ranges (`FaceStart .. FaceStart+FaceCount`)
when the mesh has one, otherwise `0 .. faces-1`; a face outside every range is
never visited (its adjacency stays `-1`).

**Phase 1, insert.** For every visited face: `r[k] = rep[index[k]]` (raw indices
when no representatives are given). If `r[0] == r[1]`, `r[1] == r[2]` or
`r[0] == r[2]` the face inserts **nothing** (welded or raw degeneracy alike).
Otherwise three entries `(r[k], r[k+1], r[k+2], face)` for `k = 0, 1, 2`, in
that order.

**Phase 2, match.** Output `= -1` everywhere. For every visited face with
distinct `r`, for slots `s = 0, 1, 2` with `adj[face][s] == -1`:

1. **Lookup** (`0x00590630`) of the reverse edge `(v1, v2) = (r[s+1], r[s])`
   with `other = r[s+2]` in bucket `v1 mod (V/3)`: the first entry with
   `entry.v1 == v1 && entry.v2 == v2` is the candidate; every later matching
   entry replaces it when `score(best) < score(entry)` (strict; the scores are
   compared as **floats**). If a candidate exists it is **unlinked** from its
   chain and its `face` is the result, else `-1`.
2. `adj[face][s] = result`. If `result == -1`, nothing else happens: the
   querying edge's own entry **stays in the table**.
3. Otherwise the querying face's own entry `(r[s], r[s+1], face)` is removed
   (`0x0058a8ff`, exact match on `v1`, `v2` and `face`).
4. **Single adjacency**: if `result` equals `adj[face][j]` for some **earlier**
   slot `j < s`, `adj[face][s] = -1` and the reverse write is skipped; the
   candidate entry has already been unlinked and is lost, the own entry is
   already removed. Later slots are not consulted.
5. Otherwise `adj[result][c] = face` where `c` is the first corner of the
   result face whose representative is `v1` (unique: the face is
   non-degenerate), i.e. the entry's own corner.

The lookup consumes the winning entry even when it is then refused; a failed
lookup leaves the querying entry for later faces; and an earlier face's write
into a later face's slot makes that slot skip its own lookup. These three
lifetimes are what the pre-review-26 module got wrong (it retired every queried
entry, kept refused candidates and checked all three slots).

`E_FAIL` is returned when more entries were inserted than `3 * faces` (cannot
happen); the output has been fully written by then.

## 4. The normal score (`0x005904bd`)

For a candidate entry `(v1, v2, v3)` and the query `(q1, q2, q3) =
(r[s], r[s+1], r[s+2])`, with `p[v]` the **three floats at byte 0 of vertex
`v`** (the score receives the vertex base and the stride but no element
offset, so it is the position only when the position is the first element;
established by the fixture's random sweep: both of its mismatches had the
position at offset 8, and the byte-0 triple reproduces D3DX):

```
e1 = p[v1] - p[v2];  e2 = p[v1] - p[v3]          (float stores)
n  = cross(e1, e2)                                (x87 products/differences, stored as floats)
f1 = p[q1] - p[q2];  f2 = p[q1] - p[q3]
m  = cross(f1, f2)
D3DXVec3Normalize(n); D3DXVec3Normalize(m)
score = float(n.z*m.z + n.x*m.x + n.y*m.y)        (x87, rounded to float for the compare)
```

`D3DXVec3Normalize` is dispatched at load (`0x00587e6b`): without the registry
overrides `DisablePSGP`/`DisableD3DXPSGP` (absent in both bottles) the SSE2
table (`0x00756246`, feature check 10) is installed when 3DNow (check 7) is
absent, as under FEX and Rosetta. Its normalize (`0x00756732`) is scalar SSE:
`len2 = (x*x + y*y) + z*z` in single precision; below `2^-46` (`0x28800000`)
the result is the zero vector (score 0); otherwise `r = rsqrtss(len2)`,
`r = ((3 - (r*len2)*r) * r) * 0.5` (one Newton step, all `mulss`/`subss`), and
the components are `x*r`, `y*r`, `z*r`. The generic table's normalize
(`_CIsqrt`) would only be used with PSGP disabled or without SSE.

Consequences: the score is CPU/emulator dependent to about one ulp of the
normal (`rsqrtss` is an approximation whose bits differ between Intel, AMD,
FEX and Rosetta), the intermediate precision of the cross product and dot
product is 80-bit under Rosetta and 64-bit under FEX with
`FEX_X87REDUCEDPRECISION=1`, and only near-ties (candidate scores within those
errors) can differ between two conforming implementations.

## 5. Cost

The sweep is `O(n * window)` distance tests plus a corner-chain walk per weld
candidate; with the engine's split-vertex meshes (every position duplicated per
face) the window is the whole group of equal keys, which is why the call is
quadratic on large flat meshes (26.6 s in one call, loading profile run 1).
The edge table's `V/3` buckets keyed by `v1` alone make every fan around a
vertex one chain.

## 6. What the module reproduces

`src/proxy/mesh_adjacency_fast.cpp` implements sections 2-4 verbatim except
that, under its equivalence gate (distinct positions further apart than
`2 * epsilon`), the distance test reduces to bit equality: the sweep walks each
equal-position class in heap order instead of the key window, keeping the
window test only for the key. Two implementation choices keep the output
identical while avoiding D3DX's cost:

* The heap sort runs on packed `(key code, index)` elements and is skipped
  altogether when the position is the first vertex element and no face
  references two distinct vertices of one class: the heap permutation then
  decides only which member represents its class, which no output depends on
  (edges compare representatives for equality; the score normals read identical
  positions). Any face touching two copies of a position, or a key that is not
  the position, runs the full sort and sweep.
* Entries are never unlinked from their chains; a retired flag hides the
  selected entry (inside the lookup) and the querying edge's own entry (after a
  successful lookup), and clearing the flag is the relink of the
  `unlink_refused=false` variant. The relative order of the live entries is
  D3DX's.

The score reads the byte-0 triple like D3DX and uses `rsqrtss` on x86
(`_mm_rsqrt_ss`; the fixture runs the module and D3DX on the same CPU or
emulator, so the approximation's bits match by construction) and `1/sqrt` on the
host, double precision for the cross product (exact for the engine's 24-bit
quantized coordinates) and the dot product. The attribute table is checked
through the public interface: a mesh whose table does not cover the faces
contiguously in order falls back to native. Fixture evidence: 53 named cases and
a 2,000-mesh random differential sweep equal to d3dx9_37 on both bottles
([mesh-adjacency-fast.md](../verification/mesh-adjacency-fast.md)).
