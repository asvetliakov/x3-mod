# Do model subgroup buffers get rewritten after load?

Read-only static analysis, 2026-09-14, answering the open issue left by
[render-node-bounds.md](render-node-bounds.md): whether X3AP.exe ever re-locks
and rewrites a model subgroup's vertex or index buffer after the load-time fill,
which would invalidate the per-part AABB at `part+0x40/+0x50` computed from
load-time vertex data by `0x0047f3f0`.

**Answer: no post-load rewrite exists on the model-geometry path.** Every write
to a subgroup buffer happens inside one build function, `0x004bb470`, which runs
once per LOD record under an idempotence guard. The proxy should still keep a
write-revision gate, because the evidence is a bounded negative (see
*§6 What the proxy needs*).

## Provenance and method

Installed `X3AP.exe`, preferred base `0x00400000`, SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`. Ghidra
12.1.3, existing read-only project `/tmp/x3-ghidra-research X3Render`,
`-noanalysis -readOnly`, with `tools/analysis/X3FindVtableCalls.java`,
`X3DecompileFunctions.java`, `X3ObjectContext.java` and three throwaway scripts
(whole-program scalar scan, symbol-reference listing, byte dump) kept in the
session scratchpad. Raw decompilation, instruction listings and dumps stay local
under `/tmp/x3-meshlock-*`. No game, Wine, build or install was run.

## 1. The only writers of a subgroup buffer

`0x004bb470(LODrecord, meshPart, descriptor, flags)` is reached only from
`0x004bd9db` inside `0x004bd830` (single xref). Per subgroup it runs, in order:

| Address | Operation | Effect on the VB/IB |
|---|---|---|
| `0x004bb72a` | `D3DXCreateMesh`, options `0x990`/`0x991` (`\| 0x18000`) | creates both buffers |
| `0x004bb7d4`/`0x004bb7e6` | `GetIndexBuffer` (slot `0x38`) → record `+0x10`; `GetVertexBuffer` (slot `0x34`) → record `+0x0c`; mesh → record `+0x14`. `0x004bb82f`/`0x004bb88e` then read both `GetDesc` (VB/IB slot `0x34`) | — |
| `0x004bb946` → `0x004bc1c0` | `LockVertexBuffer(0,&p)` `0x004bc1f6`, fill, `UnlockVertexBuffer` `0x004bc5ea` | **position fill** |
| `0x004bb95a` → `0x004bc080` | `LockIndexBuffer(0,&p)` `0x004bc0bd`, fill, `UnlockIndexBuffer` `0x004bc1ac` | index fill |
| `0x004bb98d` → `0x004bbb10` | `LockVertexBuffer(0,&p)` `0x004bbb7d`, `UnlockVertexBuffer` `0x004bc06d`; conditional on `flags & 2`, `flags & 0x800`, `part[6] != 0` | writes **only** the float fields at vertex `+0x28..+0x3c` (tangent/binormal); it *reads* the int16 positions of the CPU array but never stores to vertex `+0x00..+0x08` |
| `0x004bb996` → `0x004bc680` | `GenerateAdjacency` (`0x58`) / `ConvertPointRepsToAdjacency` (`0x50`), `D3DXCleanMesh` `0x004bc873`, `OptimizeInplace(0x4000000, …)` (slot `0x6c`) | D3DX-internal rewrite: face/vertex reorder and vertex splits. Position **values** are permuted or duplicated, never altered |
| `0x004bb9a7` → `0x004bcb60` | `CloneMesh` (slot `0x30`) at `0x004bcc2e`, then `0x004bc9c0` | replaces record `+0x0c/+0x10/+0x14` with the clone's VB/IB/mesh (old ones released), and writes the clone's declaration to record `+0x18` at `0x004bcca6` |

`0x004bc1c0` reads the LOD record's CPU vertex array (`param_1[2]` =
`LOD+0x08`, stride `0x18`) and writes `int16 × DAT_005655d4` where
`DAT_005655d4 = 0x38800000 = 2^-14 = 1/16384` — the same array and the same
scale `0x0047f3f0` accumulates its min/max from. The UV scale is
`DAT_005654e0 = 0x37800000 = 2^-16`.

`0x004bc9c0` (called only from `0x004bc680` and `0x004bcb60`) releases the old
VB/IB/mesh, installs the replacements, and refreshes record `+0x24/+0x28`
(buffer sizes from `GetDesc`, VB/IB slot `0x34`), `+0x20/+0x1c/+0x04`
(`GetNumFaces`/`GetNumVertices`).

## 2. Nothing else can reach those buffers

- **`D3DXCreateMesh` has exactly one caller**, `0x004bb470` (thunk `0x004faf12`,
  ref `0x004bb72a`). `D3DXCleanMesh`'s only caller is `0x004bc680`
  (`0x004bc873`). Every `ID3DXMesh` in the process therefore originates in the
  subgroup builder, so every other indirect call at vtable displacements
  `0x3c`/`0x40`/`0x44`/`0x48` found by the whole-program scan (23/13/13/34 hits)
  is on a different interface — effects, device, fonts, sprites — not on
  `ID3DXBaseMesh::Lock*/Unlock*`.
- **Subset-record stride `0x1a8` arithmetic exists in exactly three functions**
  (whole-program scalar scan): `0x004bb4e5`/`0x004bb9b3` in `0x004bb470`,
  `0x004bce86`/`0x004bcea8` in `0x004bccc0` (teardown: releases record
  `+0x0c/+0x10/+0x14`, frees the array), and `0x004c022d` in `0x004c0150` (the
  draw). Single-record pointers are handed only from `0x004bb470` to
  `0x004bc680`/`0x004bcb60`/`0x004bc9c0`, and from `0x004c4fc0` to `0x004c0150`.
- **The draw never locks.** In `0x004c0150` the record is consumed as
  `GetNumBytesPerVertex` (mesh slot `0x20`) → `SetStreamSource(0, record+0x0c,
  0, stride)` (device slot `0x64`) → `SetVertexDeclaration(record+0x18)` (device
  slot `0x60`) → `DrawIndexedPrimitive` at `0x004c403c`. Its only
  displacement-`0x2c` indirect call, `0x004c1e4c`, is an `ID3DXEffect`
  parameter query (its neighbours pass parameter-name strings).
- **The direct `IDirect3DVertexBuffer9::Lock` sites are foreign buffers.** Only
  three of the thirty displacement-`0x2c` call sites have the
  `(offset, size, ppbData, flags)` argument shape: `0x004bea34` in `0x004be7d0`
  (buffer at `*local_70` of a per-system struct, `D3DLOCK_DISCARD 0x2000`),
  `0x004bfdd9` in `0x004bfd40` (same shape, `0x2000`), and `0x004c45e2` in
  `0x004c4330`, which locks the global `DAT_00608a74` whole-buffer with
  `DISCARD`. None of them obtains its buffer from a subset record; they are
  per-frame dynamic streams of other subsystems (callers `0x00471f50`,
  `0x0046d080`, `0x004b9770`/`0x004da960`). The remaining `0x2c` sites take five
  to eight arguments or a string pointer and are not buffer locks.

## 3. Build is once per LOD record

`0x004bd830(model)` loops over the model's LOD records (`model+0x10` count,
`model+0x0c` array) and for each one **only builds when `LOD+0x3c == 0`**;
otherwise it increments the reference counter at build-record `+0x44`. So
`0x004bb470` cannot run twice for the same LOD. Its callers (`0x00486b40`,
`0x00489da0`, `0x0048a1e0`, `0x00489bf0`) are "ensure built" helpers reachable
from the render path, which makes the build lazy but still once.

`0x004863c0` is a hash-table lookup keyed on `id + 1` over
`*(DAT_00608518+0x14)`; only a miss builds the name (`v\%05`) and calls the
loader `0x00483f20`/`0x00481aa0`. Teardown is `0x004bda60` → `0x004bccc0`
(callers `0x004802b0`, `0x004bdb40`); a teardown/rebuild produces *new* D3D
buffer objects, so it cannot silently mutate a buffer the proxy already knows.

## 4. The part AABB after `0x0047f3f0`

`0x0047f3f0` has exactly two callers, both in the model loader:

- `0x00482d71` inside the BOB parser `0x00481aa0` (the path documented in
  `render-node-bounds.md`), immediately followed by `0x00481010` writing
  `part+0x30`;
- `0x00486176` inside the file loader `0x00483f20`, in a nested loop over LOD
  records (`model+0x0c`, index at `ESP+0x10`) and parts (`part+0x04` count),
  i.e. a full recompute for every part of every LOD at load.

`0x00483f20` is called only from `0x004863c0` (cache miss) and `0x00486310`.
Both AABB writers therefore run before any GPU build: `0x004bd830` is invoked by
separate "ensure built" helpers after the model object exists. A whole-program
scan for non-stack stores to all six offsets `+0x40/+0x44/+0x48/+0x50/+0x54/
+0x58` returns many functions, but the render-path candidates resolve to other
structures: `0x0047d9c0`, `0x004c4fc0` and `0x0047e6e0` touch only `ESP`-relative
locals at those displacements, and `0x00479d10` is a bulk render-node copy
(element indices, not byte offsets). No per-frame writer of the part AABB was
found.

## 5. What else can move a rendered position

- **Vertex-shader displacement**: excluded for the six Asteroid fade pairs by
  [rigid-position-profiles.md](rigid-position-profiles.md)
  (`clip = WVP · (POSITION0, 1)`).
- **Skinning**: now excluded structurally, not just by absence of evidence. The
  subgroup vertex layout is fixed by the declarations `0x0054e1d0` (POSITION0
  FLOAT3 @0, TEXCOORD0 FLOAT4 @0x0c, NORMAL0 FLOAT3 @0x1c, stride `0x28`) and
  `0x0054e210` (the same plus TANGENT0/BINORMAL0 FLOAT3, stride `0x40`), and by
  `0x004bc1c0`'s stores of ten or sixteen consecutive floats. There is no
  `BLENDWEIGHT` or `BLENDINDICES` element in any subgroup declaration.
- **Camera-facing basis swap** at `0x0047d9c0`: rewrites the node basis before
  `0x004bdee0` builds the world matrix, so it is inside the submitted WVP
  (`render-node-bounds.md` §4). The object-space box is unaffected.
- **Position format is not always FLOAT3.** `0x004dfb20` creates the
  declarations and, from device capability bits at
  `*(*(DAT_00608b3c+0x18)+0x1b8)` (`0x200`, and bit `0x80` of the low byte),
  publishes the pair used by `CloneMesh` in `0x004bcb60`
  (`DAT_00608d78/0x00608d80` without tangents, `DAT_00608d7c/0x00608d84` with):

  | Caps branch | Declarations | POSITION0 |
  |---|---|---|
  | `0x200`, not `0x80` | `0x0054e240` / `0x0054e260` | **FLOAT16_4** |
  | `0x200` and `0x80` | `0x0054e290` / `0x0054e2b0` | **FLOAT16_4** |
  | not `0x200`, `0x80` | `0x0054e2e0` / `0x0054e300` | FLOAT3 |
  | otherwise | `0x0054e1d0` / `0x0054e210` | FLOAT3 |

  So on a device that advertises the `0x200` capability the buffer actually
  drawn holds half-float positions produced by D3DX's conversion inside
  `CloneMesh`, even though `0x004bc1c0` wrote float32. This is the load-time
  justification for the `2^-10` half-extent expansion in
  [linear-distance-fade-region.md](../architecture/linear-distance-fade-region.md)
  §2: it is required, not merely prudent, whenever the FLOAT16_4 branch is
  taken; in the FLOAT3 branches `int16 × 2^-14` is exact in float32 and no
  expansion would be needed. One expansion of `2^-10` POSITION units covers both
  branches and both rounding modes.

## 6. What the proxy needs

Keep the per-allocation **write-revision gate** described in the region design
note, with the first-seen revision sampled at the first admitted draw of that
allocation, not at creation: the whole load sequence (fill, `CleanMesh`,
`OptimizeInplace`, `CloneMesh`) legitimately locks and rewrites buffers, and the
final clone's VB is a *different* allocation from the one first created. After
the first draw no writer exists on this path, so the gate should never fire;
count firings as a diagnostic rather than expecting them.

Do **not** replace the gate with a load-time-only assumption. The static result
is a bounded negative over the routes enumerated in §2, and the AABB is a value
copied into a proxy-side table; the gate costs two `get_buffer_content_view`
calls and converts any unmodelled writer into a full-viewport fallback instead
of a wrong rectangle. Teardown/rebuild is already safe because allocation ids do
not recur.

## Conclusion

For the six Asteroid fade pairs the part AABB is safe as a **load-time
constant**: their subgroup buffers are written only inside `0x004bb470`, once
per LOD record, before the first draw, and the drawn positions are a
permutation/duplication of exactly the int16 values `0x0047f3f0` bounded, up to
one half-float ULP when the FLOAT16_4 declaration branch is active.

Unproved, and left open:

- Which capability branch `0x004dfb20` takes on the target device — static
  analysis cannot decide it; the `2^-10` expansion makes the choice moot for the
  bound but the exact drawn format is unknown until a capture.
- D3DX's float32→float16 rounding mode inside `CloneMesh` (nearest vs truncate);
  one ULP covers both.
- Exhaustiveness of "no other writer of `part+0x40..+0x58`" rests on
  `0x0047f3f0` having exactly two xrefs plus the alternate `0x00481aa0` branch
  that reads the six fields from file. A function that merely *copies* a part
  record would carry identical values and is therefore harmless, but was not
  enumerated.
- Whether a model can be evicted from the `0x004863c0` cache and reloaded
  mid-session (`0x004802b0`, `0x004bdb40` not analysed). Harmless for the bound:
  new buffers, new allocation ids.
- None of this is observed at runtime; no capture has compared a projected part
  AABB against drawn pixels.
