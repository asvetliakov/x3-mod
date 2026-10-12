# Render-node and mesh-part spatial bounds

Read-only static analysis, 2026-09-14, answering whether X3AP.exe holds a
conservative spatial bound for the geometry it submits in a material draw, and
how a D3D9 proxy could reach it at `DrawIndexedPrimitive` to derive a
conservative screen rectangle for distance-fade composition.

**Answer: yes.** Every mesh part carries a load-computed object-space AABB
derived from exactly the vertex positions its triangles reference, and that part
is reachable from the material-submission argument by one pointer dereference.
The engine's own frustum cull uses a coarser per-model sphere that is **not**
suitable as the composition bound (see *Engine cull*).

## Provenance and method

Installed `X3AP.exe`, preferred base `0x00400000`, SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`. Ghidra
12.1.3, existing read-only project `/tmp/x3-ghidra-research X3Render`,
`-noanalysis -readOnly`, with `tools/analysis/X3DecompileFunctions.java`,
`X3ObjectContext.java` and a local offset-scan script. Raw decompilation and
instruction dumps stay local under `/tmp/x3-bounds-*`. No game, Wine, build or
install was run; nothing outside this note changed. Structural claims below are
checked against the instruction listing, not the decompiler's C alone.

## Structure layout established here

| Struct | Offset | Semantics | Established at |
|---|---|---|---|
| Model (`0x004863c0` cache) | `+0x0c` | Array of geometry/LOD-record pointers | `0x0047d9c0` (`0x0047dfe0`), `0x004bd830` |
| Model | `+0x10` | LOD count (short) | `0x004bd9xx`, `0x0047eb90` |
| Model | `+0x5c`, `+0x60` | Built collision tree, current LOD index | `0x0047eb90` |
| LOD record | `+0x04` | Mesh-part count (short) | `0x0047d9c0`, `0x004bd830` |
| LOD record | `+0x08` | Vertex array base; stride `0x18`, first three int16 = position | `0x0047eb90`, `0x0047f3f0`, `0x00481010` |
| LOD record | `+0x10` | Array of mesh-part pointers | `0x0047d9c0`, `0x004bd830` |
| LOD record | **`+0x20/+0x24/+0x28`** | **Per-axis half-extent about the model origin** | written `0x00481aa0` from `0x0047f350` |
| LOD record | `+0x2c` | Zeroed by `0x0047f350` | `0x0047f350` |
| LOD record | `+0x3c` | Per-LOD GPU build record (`0x4c` bytes) | `0x004bd90b` |
| Mesh part | `+0x00`, `+0x04` | Subgroup count (short), subgroup array (stride `0x20`) | `0x0047eb90`, `0x004bb470` |
| Mesh part | `+0x10/+0x14/+0x18` | Pivot used by the L∞ radius below | `0x00481010` |
| Mesh part | `+0x28` | Sort key (distance) written per frame | `0x0047d9c0` |
| Mesh part | `+0x30` | L∞ (chebyshev) radius about `+0x10..+0x18` | written `0x00481aa0` from `0x00481010` |
| Mesh part | **`+0x40/+0x44/+0x48`** | **Object-space AABB centre × 4** | `0x0047f4d4`, `0x0047f505`, `0x0047f533` |
| Mesh part | **`+0x50/+0x54/+0x58`** | **Object-space AABB half-extent × 4** | `0x0047f4f6`, `0x0047f524`, `0x0047f54b` |
| Mesh part | `+0x60` | Part flags (`0x8000` skip, `0x20` defer, `0x20000`, `0xa0`) | `0x0047d9c0`, `0x0047e6e0` |
| Mesh part | **`+0x64`** | Material/geometry descriptor (`0x18` bytes) | `0x004bd9f1`, read at `0x004c4fc0` |
| Mesh part | `+0x68` | Owning LOD record | `0x00481010` |
| Descriptor (`part+0x64`) | **`+0x00`** | **Back-pointer to the owning mesh part** | `0x004bd9fa` |
| Descriptor | `+0x06`, `+0x08` | `1`; subset count (short) = part's subgroup count | `0x004bda05`, `0x004bd830` |
| Descriptor | `+0x0c` | Subset record array, stride `0x1a8` | `0x004bb533` |
| Subset record | `+0x00`, `+0x04` | Material index, triangle count | `0x004bb470` |
| Subset record | `+0x0c`, `+0x10`, `+0x14` | `IDirect3DVertexBuffer9`, `IDirect3DIndexBuffer9`, `ID3DXMesh` | `0x004bb470` (`GetVertexBuffer` slot `0x34`, `GetIndexBuffer` slot `0x38`) |
| Render node | `+0x70`, `+0x80/+0x84/+0x88` | Base scale, per-axis scale (16.16) | `0x004bdee0`, `0x004880e0`, `0x00488270` |
| Render node | **`+0xa0`** | **Own world-space radius** = `(+0x70 × max(+0x80,+0x84,+0x88)) >> 16` | `0x004880e0`, `0x00488270` |
| Render node | **`+0xa4`** | **Cached subtree radius** (`-1` = dirty) | `0x00488170` |
| Render node | `+0xf0/+0xf4/+0xf8` | Camera-relative coordinates (see `external-camera.md`) | `0x0047cfe0`, `0x0047d9c0` |
| Render node | `+0x12c & 2` | Renderable-this-frame flag; gate at the top of `0x0047d9c0` | written `0x0047cfe0` |
| Render node | `+0x14c` | Selected LOD index | written `0x0047cfe0` |
| Render node | `+0x1b0` | HUD/indicator radius (angular-size path only) | `0x0047e402` |

## 1. Where the engine culls

Culling and LOD selection happen in **`0x0047cfe0`**, a recursive per-node pass
that runs before traversal. It clears or sets node `+0x12c & 2`, the exact flag
`0x0047d9c0` tests on entry. Three separate rejections:

- **Behind the eye**: `node+0xf8 + node+0xa0 < 0` clears the flag. Camera-space
  forward coordinate plus the node's own world radius. Sphere, per node.
- **Distance**: `0x0042f850` node-to-camera distance against camera `+0x370`
  (quality-clamped), and `FUN_00488170(node)` subtracted from the LOD distance.
- **Frustum**: `0x004c6aa0(node, camera, *(node+0x1c)+0x2c)`; a zero result
  clears the flag.

`0x004c6aa0` is a **world-space sphere-versus-frustum test**. It builds
view × projection (`0x004c63b0`, `0x004be460`, `D3DXMatrixMultiply`), extracts
the six planes with `0x004c7c80` (the standard row ± row combinations, then
normalised), and calls `0x004c8270` at `0x004c6e87`, which tests
`dot(n, c) + d + r >= 0` for six planes with `r` at `EDI+0xc`. The centre is the
node origin `+0xb0/+0xb4/+0xb8` × the context scale. The radius is built at
`0x004c6c56`–`0x004c6d5d` from **LOD record 0 only** — `*(model+0xc)`, not the
selected LOD:

```
e_i = ((LOD0[+0x20 + 4i] * node+0x70) >> 16) * node+(0x80 + 4i) / 65536
r   = sqrt(e_x^2 + e_y^2 + e_z^2)            ; 0x004c6d46 via 0x00412440
```

with fallback `r = node+0xa0` at `0x004c6d6a` when the computed radius is 0 (`0x004c6d62`). A node
without a model is not tested at all: model id `node+0x140 <= 0` (`0x004c6aee`) or a cache miss
(`0x004c6c50`) returns 0 (corrected 2026-10-13, §6).

So the engine's bound is **per node, per model, world space, LOD-0 derived** and
is *not* per LOD and not per part. `0x00488170` additionally computes and caches
a subtree radius at `node+0xa4`, expanding `node+0xa0` by each child's local
offset `child+0x30/+0x34/+0x38` plus the child's own cached radius.

**This value is unsuitable as the composition bound**: it is taken from LOD 0
while a different LOD may be submitted, and nothing here proves LOD 0's extents
dominate the other LODs.

## 2. The mesh part carries its own extents, computed from vertex data

`0x0047f3f0(LODrecord, part)` runs at load from the BOB parser `0x00481aa0`. It
walks **every subgroup of the part and every index triplet in each subgroup**,
fetches the vertex at `LOD+0x08 + index*0x18`, and accumulates the per-axis
int16 min/max (seeded ±`0x4000`). Its tail writes, per axis:

```
part[+0x40 + 4i] = (min_i + max_i) * 4 / 2     ; centre,      4x int16 units
part[+0x50 + 4i] = (max_i - min_i) * 4 / 2     ; half-extent, 4x int16 units
```

verified at `0x0047f4f6/0x0047f505/0x0047f524/0x0047f533/0x0047f54b`. The
alternate container branch of `0x00481aa0` reads the same six fields directly
from the file instead of recomputing them, so the fields exist either way.

`0x0047f350(LODrecord, out[4])` then folds them into the LOD-record extents:
`out_i = max over parts of (|part[+0x40+4i]| + part[+0x50+4i])`, stored at LOD
`+0x20/+0x24/+0x28` (`0x0047f394`–`0x0047f3c5`).

`0x00481010(part)` separately computes the **L∞** radius about `part+0x10..0x18`
and stores it at `part+0x30`. Being an L∞ radius, a sphere of that radius does
not bound the box; do not use it as a Euclidean radius.

### Units line up exactly with the submitted world matrix

Mesh vertices are written to the vertex buffer as `int16 × 1/16384`
(`0x004bc1c0`, constant `0x005655d4` = `2^-14`), as float32; the load-time
`CloneMesh` at `0x004bcb60` may re-encode POSITION0 as FLOAT16_4
(see [mesh-buffer-rewrite.md](mesh-buffer-rewrite.md) §5), which changes the
storage format but not the scale. `0x004bdee0` builds the
world matrix with row scale `((node+0x70 × node+0x80..) >> 16) × s` and basis
rows `/65536`. The bound path multiplies its `4 × int16` values by
`node+0x70 >> 16` and `node+0x8x / 65536`. The two chains differ by exactly
`4 × 16384 = 65536`, so:

```
object_space_bound = part[+0x40..] / 65536  +/-  part[+0x50..] / 65536
```

is in **the same units as `POSITION0` in the vertex buffer**. No extra scale is
needed: the submitted WVP already carries `node+0x70`, `node+0x80/84/88`, the
basis and the context conversion.

### The draw covers exactly one subgroup, entirely

`0x004bd830` allocates one `0x18`-byte descriptor per mesh part, stores it at
`part+0x64` (`0x004bd9f1`) and writes the part back-pointer into descriptor `+0`
(`0x004bd9fa`). `0x004bb470` then allocates `subgroupCount × 0x1a8` subset
records at descriptor `+0x0c` and creates **one `ID3DXMesh` per subgroup**
(`D3DXCreateMesh` with that subgroup's triangle and unique-vertex counts,
options `0x990`/`0x991` optionally `| 0x18000`), keeping the mesh at record
`+0x14` and its VB/IB at `+0x0c`/`+0x10`.

The draw itself, at `0x004c403c` inside `0x004c0150`, is

```
DrawIndexedPrimitive(device, D3DPT_TRIANGLELIST, 0, 0,
                     mesh->GetNumVertices(), 0, mesh->GetNumFaces())
```

one call per (subset record × effect pass), between `BeginPass` `vtable+0x100`
and `EndPass` `vtable+0x108`. `BaseVertexIndex`, `MinVertexIndex` and
`StartIndex` are all literal zero, so every draw covers a whole subgroup mesh.
The part AABB covers all of that part's subgroups, hence it is a **superset** of
each individual draw: conservative, as required.

## 3. The deferred path reaches the same bound

`0x0047e6e0` keeps record `+0x08` = mesh part, `+0x0c` = LOD record,
`+0x10` = render node. At `0x0047e761`–`0x0047e769` it loads `ECX = *(record+8)`
(the part) and pushes `*(record+0x10)` (node) and the camera, then calls
`0x004c4fc0`, which reads `part+0x64` and forwards the descriptor to
`0x004c0150`. The immediate path `0x0047e076` passes the same part in `ECX`.
Both paths therefore expose the part, and with it `+0x40..+0x58`, at the moment
the draw is issued. World is recomputed at `0x0047e70c` whenever the node
pointer changes, so the WVP in effect for a drained record belongs to that
record's node.

## 4. Identifying the part at `DrawIndexedPrimitive`

Two routes, both usable together:

1. **Thread-local scope.** The existing seam in `src/proxy/object_trace.cpp`
   redirects the single relative call at `0x004c5228` (inside `0x004c4fc0`) to
   `0x004c0150` and already captures the six stack slots. Slot `ESP+4` is the
   descriptor; `*(descriptor+0)` is the mesh part. That is one extra validated
   4-byte read on an already-read pointer, and it yields the AABB without any
   new patch site. The site remains a five-byte `CALL rel32` at an instruction
   boundary with the callee's arguments fully on the stack; the existing module
   already forwards `EAX`, `LastError` and CPU state and handles foreign SEH
   unwind, so no new hook-site analysis is required for this use. Hooking
   `0x004c4fc0` entry instead would give the part directly in `ECX`, but it
   costs a new patch site and `0x004c4fc0` establishes its own SEH frame at
   entry; the existing seam is preferable.
2. **Buffer identity.** Each subgroup owns a distinct VB/IB pair
   (record `+0x0c`/`+0x10`). The proxy already tracks per-allocation identity
   and write revisions (`src/ownership/finite_buffer_evidence.h`), so the
   stream-0 VB plus IB at draw time is a stable key for the subset record and
   therefore for the part. This works even for draws not covered by the scope.

Stability of the bound:

- **LOD choice** — each LOD record has its own parts with their own AABBs, so
  the bound follows whichever LOD was submitted. This is strictly better than
  the engine's LOD-0 cull radius.
- **Camera-facing basis replacement** (`0x0047d9c0`, node flags `0x20`/`0x200`)
  rewrites the node basis `+0xc0..+0xe8` *before* `0x004bdee0` builds the world
  matrix, so it is already inside the submitted WVP. The object-space AABB is
  unaffected.
- **Non-uniform scale is real**: `0x004bdee0` scales the three world rows
  independently by `node+0x80/+0x84/+0x88`. Projecting the eight AABB corners
  through the submitted WVP handles this exactly; a scalar radius would not.
  (The engine itself takes `max` of the three axes for `node+0xa0`.)
- **Articulation** is expressed as child render nodes with their own world
  matrices and their own draws, not as deformation within one draw.

## 5. Caveats that could make a bound unsafe

| Caveat | Status |
|---|---|
| Vertex-shader displacement | Excluded for the six Asteroid pairs. `rigid-position-profiles.md` proves `clip = WVP · (POSITION0, 1)` with no offset, deformation, W dependency, saturation or divide for `167eb2d5629ab9d3` and `b0602757fce6e870`; `asteroid-fog-temporal.md` shows the fade only writes `COLOR0.a`. Not proved for the four toggle VS of those pairs beyond their membership in the reviewed row-dot set. |
| Billboards / direct-clip shaders | The particle billboard VS and the three direct-clip bloom VS expand or bypass position. They are not on this material path and must be excluded by shader identity, not assumed absent. |
| Skinning | No `BLENDWEIGHT`/`BLENDINDICES` appears in the declared inputs of the reviewed programs (P, T, N, B, G, C). No skinning path was found on this route; this is absence of evidence at the reviewed scope. |
| Dynamic vertex writes | Meshes are created `D3DXMESH` `0x990/0x991` (+`0x18000`) = SYSTEMMEM + DYNAMIC for both buffers (`0x004bb470`, matching `mesh-dynamic-contract.md`). Positions are filled once at load by `0x004bc1c0` (`0x004bbb10` writes only the tangent/binormal floats). **Settled in [mesh-buffer-rewrite.md](mesh-buffer-rewrite.md): no post-load rewrite exists on this path**, and the final drawn buffers are `CloneMesh` output whose POSITION0 may be FLOAT16_4. Keep the per-allocation write-revision gate, sampled at first draw. |
| HUD/selection radii | `node+0x1b0` (angular size at `0x0047e402`) and `part+0x30` (L∞) are *not* raster bounds. `node+0xa0`/`+0xa4` are real world radii but node-level and LOD-0 derived. Do not substitute any of them for the part AABB. |
| Rasterisation margin | The AABB bounds vertex positions, not rasterised samples. A conservative screen rectangle must still be expanded by at least half a pixel per edge, plus any line/point or wide-primitive state, and clamped to the viewport. |
| Clipping | The projected AABB is valid in clip space; a rectangle must be derived with correct handling of corners with `w <= 0` (fall back to the whole viewport) rather than by naive per-corner divide. |

## Conclusion

For the six Asteroid fade pairs, the proven conservative bound for a given draw
is the **owning mesh part's object-space AABB**:

```
centre_os      = (part[+0x40], part[+0x44], part[+0x48]) / 65536
half_extent_os = (part[+0x50], part[+0x54], part[+0x58]) / 65536
```

reached from the draw as `part = *(*(ESP+4 at 0x004c0150) + 0)`, or from the
subset record found by stream-0 VB/IB identity. It bounds exactly the
`POSITION0` values the drawn `ID3DXMesh` was built from, in the units the
submitted WVP (`c24`–`c27`) consumes, so the eight corners projected through the
submitted WVP and the submitted viewport give a conservative screen rectangle
without any engine-side projection.

What remains unproved: that the four Asteroid toggle VS have the same position
path as their bases; and that the fields are correct for bodies loaded through
the alternate `0x10000000` container branch, where the six values are read from the file
rather than recomputed from vertices. None of these has been observed at
runtime — this note is static analysis only, and no capture has yet compared a
projected part AABB against the actual drawn pixels.

## 6. Ship hull extents at run time (capital gate transit, amendment 16)

Read-only static analysis, 2026-10-13, for
[capital-gate-transit.md](../architecture/capital-gate-transit.md) §16.1 "Hull reach across the axis" and §16.8
row 1. Same `X3AP.exe` (SHA-256 `fdbf3418…f8ab`, verified this session), capstone listings of the addresses below
(no decompiler output was relied on; raw listings stay in the session scratchpad). No game, no Wine.

**Answer.** The suggested chain "ship node `+0x1c` → model → `+0x0c`[0]" is wrong in two places. Node `+0x1c` is
the render context, not the model; the model is found from the node's **body id `+0x140`** through the body cache
hash. And a ship's root node `[obj+0x70]` carries TShips column 0, which is body 0 (`testbody`, 47 units) on every
installed M1/M2/M7 row, so **its own LOD-0 record says nothing about the hull**. The hull and every other part are
**child nodes** built from the ship's scene. The width therefore comes from the root's children, each read as below.
LOD 0 is index 0 of the model's LOD array. The extents are in normalised model units (1.0 = 65536) before the node
scale. The collision mesh of a ship part is the **coarsest** LOD, and on 40 of 131 installed capital hulls it is
laterally wider than LOD 0. A reader should therefore take the maximum over all LOD records.

### 6.1 Offset chain and guards (for the reader)

| Step | Read | Guard | Established at |
|---|---|---|---|
| Ship → root node | `root = [obj+0x70]` | non-null; the walk already requires it | class-7 path `0x00441255..0x00441276` (`0x00486d10` alloc, `[obj+0x70]` = node, `0x00487e30` body) |
| Root body | `[root+0x140]` = TShips record `+0` (word; `0xffff` → 0) = **0** for all 118 M1/M2/M7 rows | not used for the width; `0x004c6aa0` itself rejects ids ≤ 0 | `0x0044062e..0x0044063c`, `0x004c6aee` |
| Scene under the root | class 7: `[obj+0x98]` = TShips `+0x68` (scene); `0x00450690` → `0x0043ce30(ctx, scene, root, obj, 0)` | — | `0x00441409..0x00441425`, `0x004506a6` |
| Part → node | one child per scene part (stride `0x58` at scene `+0x50`, body id at part `+0x30`). Body −1: none. Body −2 or 1000..1999: light/effect node via `0x004885a0`. Otherwise a `0x00486d10` node, parented to the root or to its `F` part's node (`0x00486fd0`). A part whose body equals a non-zero root id is the root itself; that cannot happen when the root id is 0. | — | `0x0043cf94..0x0043cfd1`, `0x0043d1ec..0x0043d28c`, `0x0043d3b6..0x0043d3e9` |
| Children list | `c = [n+0x0c]; while c && [c] != 0: …; c = [c]`. The sentinel is `n+0x10`, whose first dword is always 0. `[c+0x04]` = prev, `[c+0x18]` = parent; linking resets the parent's `+0xa4` to −1. | count limit (256, as the station walk); recurse at most 3 deep, budget 64 | link `0x00489f20` (`0x00489f41..0x00489f5c`); init `0x00486d8b..0x00486d97`; walks `0x0047b54c`, `0x0048728b` |
| Scene identity | `[c+0x258]` = scene id, `[c+0x25c]` = part index (part 0 is not always the hull) | — | `0x0048eb43`, `0x0048eb4c` |
| Node → body id | `id = [c+0x140]` | **`id > 0`**. −1 is the initial value (no body); 0 is `testbody`. The body is set only when id > 0. | init `0x00486da9`; set `0x0043d7f1..0x0043d7fc`, `0x00487e8d` |
| Collision set | `[c+0x12c] & 0x8000000` = no collision tree (jets `[c+0x130] & 1`, flags `0x10004000`, bodies `0x3ac`/−1, failed builds). `& 0x1000000` = tree built. Narrow phase tests only nodes with `0x1000000` and without `0x8000000`. | skip `0x8000000`; including nodes without `0x1000000` is conservative | `0x0043d889..0x0043d8a8`, `0x0047ecf5`, `0x0047eed0`, filter `0x0048a929` ([sector-collide.md](sector-collide.md)) |
| Body id → model | `H = [[0x00608518]+0x14]`; `buckets = [H]`, `size = [H+4]`, `key = id + 1`; `e = [buckets + 4*(key & (size-1))]`; `while e: if [e+4] == key: model = [e+8]; e = [e]` | `[0x00608518]`, `H`, `buckets` non-null; size > 0; chain-step limit; a miss = no data (w = r) | `0x004863e2..0x00486419`, hit `0x00486464` |
| **Do not call `0x004863c0`** | Even on a hit it writes `model+0x64 = 0` and may rewrite the coarsest LOD's material indices; on a miss it loads the body from disk (allocation, SEH frame). | replicate the lookup read-only, on the game thread | `0x00486876..0x004868fb`, miss path `0x00486427..` |
| Model → LOD records | `n = (int16)[model+0x10]`, `arr = [model+0x0c]`, `L_i = [arr + 4i]`; **LOD 0 = `[arr+0]`** (index 0, not `model+0x60`) | `1 <= n <= 16` (installed maximum 9); `arr`, `L_i` non-null | `0x004c6c59..0x004c6c62` (cull radius, index 0), parser `0x004823c7` |
| `model+0x60` | The **collision** LOD index written by `0x0047eb90`: min(requested, n−1). Ship scene parts request n−1 (the coarsest); parts of class-5 owners or where `0x0043fd00` is true request 1. It is rewritten on every call, even when the tree already exists, so it does not reliably name the built tree's LOD. The drawn LOD is node `+0x14c`. | do not use as "LOD 0" | `0x0047ec03..0x0047ec24`, `0x0043d8c7..0x0043d8e6` |
| LOD extents | `ext = [L+0x20], [L+0x24], [L+0x28]`: per axis, the max over the LOD's mesh parts of \|centre\| + half, about the **model origin**. The LOD covers only that body's own mesh parts, never child nodes. | each in `[0, 0x20000]` (normalised: the largest axis is 65536) | `0x0047f350` (`0x0047f383..0x0047f3c7`), store `0x00482db9..0x00482dc2`; text loader `0x0048619b` |
| Scale | `s = [c+0x70]` = LOD-0 `+0x14` body value. `s_x,s_y,s_z = [c+0x80/84/88]`: 0x10000, except jets (0x10000, 0x10000, 0x147). | `s > 0`, `s_i > 0` | `0x00487e93..0x00487eaf`, jets `0x0043d8eb..0x0043d905` |
| World half-extent | `e_i = ((ext_i * s + 0x8000) >> 16) * s_i / 65536` in engine units (64-bit product), the engine's own cull arithmetic. 500 units = 1 m. | — | `0x004c6c56..0x004c6d1b` |
| Placement | Offset in the parent frame: `o = [c+0x30..0x38]`, × parent `+0x80..` / 65536 unless parent `+0x12c & 0x10000`, and × parent `+0x70` / 65536 when `[c+0x12c] & 0x40`. Local basis `[c+0x40..]`: 3 rows of 4 dwords, 16.16; world = parent world ∘ local. | if the basis is not the identity, bound the box by \|e\| on every axis (no row/column convention needed) | `0x0047b564..0x0047b648` |

Not the chain: node `+0x1c` is the render context. It is propagated to children by `0x00489f20` → `0x00489d60`.
`[ctx+0x18] & 1` is tested at `0x00487e76`, and `[ctx+0x2c]` (float) is `0x004c6aa0`'s third argument
(`0x0047d0ef..0x0047d0ff`).

**Recommended reading [p]:** for every collision-relevant descendant node with a model, take the per-axis maximum
of `ext` over **all** LOD records (at most 9 × 3 reads). Place the box in the root frame (ship x, y lateral; z is
the nose, basis row 2). Then

`w = max over nodes sqrt((|o_x| + e_x)^2 + (|o_y| + e_y)^2)` and `half-length = max(|o_z| + e_z)`.

The square root matters: a free roll turns the box's lateral cross-section, so `max(e_x, e_y)` (the §16.1 text) is
not a disc radius. Thread and reentrancy: plain reads only, no engine call. They must run on the game thread at the
existing decision sites, as the station part walk does: the cache hash has no lock. When models are freed is not
established, so look the model up again on each read and cache only the derived w, keyed on (object, root node,
child ids). Any failed guard gives w = r.

### 6.2 Static widths of the installed capitals

`verification/results/ship-steering/capital-widths/capital_widths.py` (output `capital_widths.txt`, per-ship
`capital_widths.json`). It applies the §6.1 rules offline: TShips → scene ([game-scale §2](game-scale-and-ship-classes.md)
method, `scene_radius.py`), part first key frame = offset, bodies via `bob1.resolve_body`. The extents use the
file's 10 bound ints when part flags `& 0x10000000` (130 of 131 hulls), as `0x00482d00..0x00482d5f` does, or else
the referenced int16 vertices (`0x0047f3f0`). The two agree within 4 units on the three hulls checked (Titan,
Colossus, Exterminator). **[m]** from data; "hull" is the largest non-effect, non-dummy part. Lateral radius in m:
*box* = LOD-0 box diagonal, *any-LOD* = box over all LODs (what a runtime read gives), *vtx* = exact LOD-0
vertex radius √(x²+y²).

| class (n) | box w/r | any-LOD w/r | vtx w/r | max(e_x,e_y)/r | all parts w/r |
|---|---|---|---|---|---|
| M1 (27) | 0.14 / 0.44 / 1.31 | 0.14 / 0.45 / 1.41 | 0.11 / 0.34 / 1.30 | 0.11 / 0.33 / 1.00 | 0.21 / 0.48 / 1.44 |
| M2 (45) | 0.19 / 0.38 / 1.49 | 0.19 / 0.38 / 1.49 | 0.15 / 0.31 / 0.81 | 0.15 / 0.29 / 1.05 | 0.23 / 0.43 / 1.49 |
| M7 (46) | 0.03 / 0.41 / 1.06 | 0.03 / 0.41 / 1.06 | 0.03 / 0.35 / 1.06 | 0.02 / 0.34 / 1.00 | 0.20 / 0.47 / 1.06 |
| TL (13) | 0.22 / 0.48 / 1.19 | 0.22 / 0.48 / 1.19 | 0.21 / 0.36 / 1.17 | 0.16 / 0.35 / 1.00 | 0.29 / 0.49 / 1.19 |

(min / median / max; r = the engine radius estimate. The maxima are the orbital weapons platforms and starbase rows
typed M1, plus the Megalodon, whose hull part is rotated, so its bound is \|e\|. The M7 minima are two rows where the
hull heuristic picked a gun part; their "all parts" figure is the usable one.)

| Ship (r m) | hull LOD-0 e_x / e_y / e_z m | box | any-LOD | vtx | all parts x / y m |
|---|---|---|---|---|---|
| Titan M2 (1,092) | 303 / 333 / 1,057 (offset z 35) | 450 | 463 | 364 | 303 / 364 |
| Colossus M1 (1,022) | 164 / 333 / 1,022 | 371 | 392 | 333 | 181 / 405 |
| Python M2 (1,074) | 185 / 243 / 1,068 | 305 | 305 | 245 | 185 / 304 |
| Exterminator M2 (759) | 227 / 179 / 759 | 289 | 385 | 244 | 268 / 218 |
| Patroclus M2 (725) | 221 / 212 / 725 | 307 | 307 | 239 | 221 / 268 |
| Boreas M2 (705) | 193 / 160 / 671 | 251 | 251 | 194 | 222 / 205 |

**Consequence for the §16.1 adjacent-lane exemption** (sum of the two w against the settled limits):

* Titan + Titan: 926 (any-LOD), 729 (vtx) against 500.
* Titan + Colossus: 855 / 697 against 502.
* Python + Python: 610 / 490 against 500.
* Exterminator ×2: 770 / 488 against 523.
* Patroclus ×2: 613 / 479 against 523.
* Boreas ×2: 501 / 388 against 523.

The 1.1 km Titan/Colossus pairs of Run158 are **never** exempt in adjacent lanes, even with the exact vertex
radius. With the runtime box read, among the ~755 m M2s only the Boreas pair is. The inferred w = 0.25 r is too
narrow: the median is 0.38–0.45 r with the box read and 0.31–0.35 r with the vertex radius.

### 6.3 Not established

* The runtime census itself: these are static figures from the scene's first key frame. Animated or rotated parts,
  nested scene references and 67 of 131 rows with part bodies the offline resolver cannot name are not modelled.
  The unresolved bodies are numeric ids outside `objects/v` (small turret and gun parts on the samples checked).
  A one-row-per-type census of the §6.1 read settles it.
* The convention of the scene angle field (the 4th key value is treated as a non-zero rotation flag only) and the
  row/column order of a child's local basis. Both are avoided by the \|e\| bound for rotated parts.
* Whether the LOD point arrays (`L+0x08`, count `L+0x00`) survive GPU upload. A runtime vertex scan would tighten w
  to the "vtx" column (20–25 % less), but this read is not established.
* When cached models are released (the cache flush at `0x00486920` walks the other table `[[0x00608518]+0]`).
* What `0x0043fd00` selects (the objects whose part collision uses LOD 1 rather than the coarsest LOD).

### 6.4 Reproduce

```sh
python3 verification/results/ship-steering/capital-widths/capital_widths.py   # about 30 s, reads /mnt/c/X3 only
# listings: capstone over X3AP.exe, VA ranges 004c6aa0:004c6dc0 004863c0:00486918 00487e30:00487f40
#   0043ce30:0043d130 0043d1d0:0043d915 00489f20:00489fa0 00486d10:00486e00 0047b540:0047b675
#   0047eb90:0047ec40 0047f350:0047f3f0 0047f3f0:0047f554 00482cf0:00482dd0 0043ffa0:00442400 00450690:004506cd
```
