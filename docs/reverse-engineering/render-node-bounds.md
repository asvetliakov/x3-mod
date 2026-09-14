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

with fallback `r = node+0xa0` at `0x004c6d6a` when the model is absent.

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
