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


## Final CloneMesh private upload interval (2026-09-21)

Derived INDEX16 evidence and prototype contract. Independent deep review identified
the pre-fill failure path described below; safe staging and initialized payload
are separate claims. No live hook or runtime fixture is qualified by this note.

## Decision and exact scope

**Root-ratified boundary: speculative opaque byte staging from the original pre-Unlock private readable mapping.** The selected fresh INDEX16 clone destinations have a bounded private producer interval. The producer is suspended at its normal Unlock, and no game record yet contains the new mesh or its buffers. Copying an authenticated readable mapping into private byte storage establishes safe access only; it does not yet establish initialized geometry. In particular, IB attribute allocation can fail after both locks but before any index stores. Such a span may be staged as opaque bytes, but must not be interpreted, hashed, logged, written to file or exposed to any consumer before successful CloneMesh, both original destination Unlocks returning S_OK, and final public allocation/revision linkage. Failure wipes staging. This is the root's explicit qualification of the original proposal after review; no added diagnostic Lock/Unlock is selected.

Restrict staging to fresh wrapper-created destination allocations within the validated CloneMesh invocation, with actual readable MANAGED backing and the existing full mapped window. Native/default/writeonly fallback, pre-existing allocations, SHAREVB, known source-lock failure, nested/foreign execution, unsuccessful closure or ambiguous linkage refuse. Source-lock failure that is observable before staging refuses early; a later constructor failure discards/wipes provisional bytes. No game-facing success claim follows from an unfilled but legally readable private mapping. Runtime implementation and acceptance remain outstanding.

Proof scope is the ordinary original D3DX route used by the game, with public COM/D3D operation semantics and no unannounced third-party mutation. Arbitrary hooks that steal private pointers are not covered. The selected counts imply the 16-bit path; the 32-bit class entry was identified but its complete allocation/copy path was not qualified here. Do not promote this into a universal CloneMesh implementation theorem.

### Provenance and method

Bottle X3 EXE SHA-256 `fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`; game d3dx9_37.dll, 3,786,760 bytes, SHA-256 `c2ccb84c672a9d8966e82a28005a4269886ee304972ac3590c0b8a9c1622a3d8`. Both preferred bases are 0x00400000; DLL addresses below are preferred VAs, not fixed runtime addresses. These hashes are research provenance, not proposed production prerequisites. Project PE reader plus bounded `/usr/bin/objdump -d --x86-asm-syntax=intel --start-address=... --stop-address=...`; Ghidra 12.1.3 read-only EXE project `/tmp/x3-fog-sizing-ghidra/FogSizing`, new local D3DX project `/tmp/x3-lattice-upload-contract/D3DXUpload`, and `tools/analysis/X3DecompileFunctions.java`. The old X3Render project still has no X3AP program. D3DX import reported unrelated analyzer warnings; conclusions below were checked against actual instructions. Raw evidence stays under `/tmp/x3-lattice-upload-contract/`.

### Linkage and ordering

| Boundary | Derived finding |
|---|---|
| EXE 0x004bcb60 | Stack arguments are subgroup pointer and flags. It selects final declaration, then options 0x440 or 0x660; may add 0x1 for >65535 vertices and 0x18000 for software processing. It never requests SHAREVB 0x1000. Only 0x660 is MANAGED; do not assume every clone qualifies. |
| EXE 0x004bcc19–0x004bcc2e | Mesh=this from subgroup+0x14; pushes output address, device, declaration, options, this. The output is a local stack slot (pre-push ESP+0x10), initially zero. Device is the pointer from `*(*(0x00608b3c+0x18))`, passed unchanged. |
| DLL slot12 | 16-bit mesh vtable 0x00406090 names CloneMesh 0x005a62c1; 32-bit table 0x00406108 names 0x005a2a4f. These are RE identifiers; no production vtable-layout access beyond public slots is needed. |
| DLL 0x005a63d8 | Calls constructor 0x005a257b for a fresh 16-bit destination. New object 0x280 bytes, initializer 0x0059eaf9 zeros counts and buffer slots, retains the supplied device at private +0x230. No game-visible registration/output occurs. Private offsets are evidence only. |
| DLL 0x00593fab → 0x005919b3 | Allocates capacity from source face/vertex counts. Because new destination counts are zero, old-buffer grow/copy branches are not taken. CreateIndexBuffer at 0x00591a9d uses the retained passed device, size faces×6, INDEX16 (101); CreateVertexBuffer at 0x00591cb0 uses vertices×declStride. Returned interfaces stay in locals and the private destination mesh. Both are public device calls, without native-device unwrapping. |
| DLL 0x005a63ea → 0x00593f6c | Index helper locks destination through 0x0058bf4f, which calls actual IB slot11 at 0x0058bf6e with `(0,0,&pointer,0x800)`; source uses 0x810. Copy writes three WORDs for each face, loop 0x005940a2–0x005940de. Source Unlock is 0x00594153; destination Unlock is 0x00594164. Destination pointer is private throughout. |
| DLL 0x005a642f → 0x005949fe | Locks destination VB through slot11 at 0x00594a26, `(0,0,&pointer,0x800)`; then source readonly lock. Equal declarations memcpy all vertices×stride; changed declaration converts every vertex at 0x00594aa1–0x00594ab3. Converter 0x0058caa2 is called with clear=1 and zeros the destination vertex before fields are stored (memset at 0x0058cacd); thus padding is initialized too. Destination Unlock is 0x00594ac2, source Unlock 0x00594ad4. |
| DLL 0x005a6441 | Only after the successful allocation/index-copy and vertex-copy paths does CloneMesh store its destination into caller `*ppCloneMesh`. Before this store, neither the game stack output nor subgroup record can furnish another consumer the destination. Unrelated callback/reentrant game activity has no pointer route to this fresh buffer through this chain. |
| EXE 0x004bcc90–0x004bcc98 | Loads local clone into EAX and subgroup into ESI, calls 0x004bc9c0. No payload access is needed here. |
| EXE 0x004bca0e / 0x004bca1d | Publication helper calls public GetIndexBuffer/GetVertexBuffer into local references and checks signed HRESULT success. Releases old IB/VB/mesh, then publishes new IB/VB/mesh at 0x004bca6d/0x004bca70/0x004bca73. Descriptor/count refresh follows and final declaration is stored at 0x004bcca6. Publication is not atomic and is too late for claiming private construction. |

On the successful copy path, the original producer calls its destination Unlock synchronously after stores and before output publication; failure cleanup can reach that Unlock before stores, as qualified below. At entry into that wrapped Unlock, the producer is suspended; the pointer never escaped its private new mesh/local chain, so there is no game alias authorizing another destination writer. This is the positive ownership argument. It does not depend on the early LOD+0x3c flag, a render thread-ID census, or later quiet revision samples.

The wrapped creation path `create_buffer` returns its application wrapper to the public device caller. D3DX stores and later dispatches that exact returned interface. Thus if the supplied CloneMesh device is authenticated as this ownership device, its new destinations use wrapped Create/Lock/Unlock. A native device, unrecognized allocation, observed fallback to actual WRITEONLY, or overwritten dispatch must refuse. D3DX does not itself call `borrowed_native_device` or create a hidden alternate device on this branch. Existing loading hooks cover slots20/22/27, not CloneMesh, so none of this is captured today.

### Failure, reentrancy and CPU boundary

- D3DX index/vertex helpers **ignore final Unlock HRESULTs**. CloneMesh success alone does not prove a successful close. Observe each original destination Unlock and require S_OK, matching pointer/range/revision/generation, and no nested or intervening mutation before staged bytes become eligible. Then separately require successful CloneMesh and final public VB/IB identity matches.
- Index source-lock failure, or the attribute-table allocation at 0x00594054 failing after both locks, can reach destination Unlock before any index copy; vertex source-lock failure similarly reaches destination Unlock before fill. Thus a completed destination Unlock alone does not prove initialized payload. Earlier opaque staging is authorized only by readable private mapping ownership, not initialized geometry. CloneMesh failure discards/wipes those bytes without interpretation, hashing, logging, file output or exposure. Successful selected full-copy branches above initialize the whole buffers; a generalized partial-span path needs explicit coverage tracking and is outside this first scope.
- DLL failure branches at 0x005a644c–0x005a646a Release provisional meshes. EXE retries at most twice for negative HRESULTs; OOM error callback at 0x004bcc5e is guarded by 0x006090f0, but is not a general reentrancy lock. Scope state must be per invocation; retry, nested CloneMesh, failure or unwinding discards staged candidates. Never carry a provisional candidate through the OOM callback into the next attempt.
- The publication helper has equal-input and failure Release paths; observer code must not infer ownership transfer from a nonnull local output or attempt to repair native cleanup behavior. Public identities queried after successful clone must release only their own acquired refs. Do not hold provisional mesh references merely to keep failed snapshots alive.
- Pre-read validation includes public GetPrivateData/GetType/GetDesc calls, which may reenter. **Recheck the entire candidate/latch/mapping validity after the last such call and before MFENCE/memcpy.** Existing finite `buffer_unlock` checks, considered alone, are not an exclusive token and should not be blindly treated as an arbitrary callback-safe snapshot API.
- Use the existing registry critical section for the final CPU-only check/copy. Wrapped Reset sets `device->resetting` under that same mutex before native Reset (d3d9_ownership.cpp 1378 onward); while the final copy holds it, another wrapped Reset cannot start native dispatch. Check resetting/lost/retiring as well as generation. A same-thread Reset during qualifier calls must latch refusal and be detected after those calls. No COM call, allocation, logging, release or callback is permitted between the final recheck and the end of memcpy. Existing public mutation notification is required for trusted native writes; arbitrary unannounced native mutation remains unsupported, not detectable.
- Preserve computational x87 payload/environment, MXCSR and LastError around all added operations; restore original incoming state before original CloneMesh/Lock/Unlock and original outgoing state afterward. Volatile XMM registers follow x86 COM ABI; preserve any extra live values if an inline seam expands beyond the resolved sequence. MFENCE plus compiler memory barrier precedes payload loads. CPU implementation uses project SSE2/4-byte incoming stack options.
- Original clone/publication functions install no local SEH frame in their inspected bodies. No success publication on exception/unwind is justified. A hook must have tested per-invocation cleanup through the project's supported x86 exception boundary, propagate original exceptions unchanged, and must not assume MinGW C++ catch-all handles every native SEH. Exception fixture acceptance is still required before live use.

### Exact proposed game seam

Preferred scope seam `[0x004bcc2b,0x004bcc30)` is exactly five bytes `8b 41 30 ff d0`: `mov eax,[ecx+0x30]; call eax`. A callsite adapter can reproduce one stdcall `CloneMesh(this,options,decl,device,out)` call, callee pops20, then resume at 0x004bcc30. Incoming ECX holds original vtable and EAX the source mesh; load/store the original target before observer calls. ESI=declaration, EBP=options, EBX=attempt counter remain live nonvolatiles; EDI is overwritten from returned EAX immediately. The result is consumed by `mov edi,eax` then fresh CMPs; no outgoing original EFLAGS are consumed. The original COM call makes EAX/ECX/EDX volatile. Do not patch at 0x004bcc2e alone: it is only two bytes. Validate surrounding context, original bytes and absence of interior control-flow entry before applying any patch; rollback restores the full five-byte span. Exact runtime patch/rollback remains unimplemented/unverified.

The separate `[0x004bcc98,0x004bcc9d)` direct call bytes are `e8 23 fd ff ff`. Its target 0x004bc9c0 uses **EAX=mesh, ESI=subgroup**, not an inferred C prototype. ESI survives; EAX returns boolean. It is unnecessary for the narrow capture if successful clone public identities plus the later actual-bound allocation/revision are authenticated. Prefer one clone scope seam over a second publication hook.

### Selector, portable boundary and next acceptance

Construction counts only filter candidates: `(vertices,faces,stride)=(9680,3784,40)` or `(1267,940,40)`; expected total VB+INDEX16 payload 466,224 bytes. Match exact target declaration and counts publicly. Source counts/addresses are not identities. On successful CloneMesh, public GetVB/GetIB interfaces must authenticate the same immutable allocation IDs created and written within this invocation, with matching owner/device generation and successful upload revisions. On F8, authenticate actual bound allocation IDs/revisions against those retained CPU bytes. Duplicate, stale, reused-address, incomplete or changed-revision matches refuse. This proves producer bytes plus a supported binding/revision match, not simultaneous texture/VB/IB coherence or historical Run177 geometry.

The implementation seam is in the game EXE and uses public CloneMesh and COM/D3D9 methods only. The DLL disassembly explains this shipped game's producer route; private +0x230/+0x234/+0x248 fields, implementation PCs and DLL hashes must not become production access checks or renderer prerequisites. Readable MANAGED backing comes from `portable_managed_upload`; native Windows requires the same creation/wrapper/identity semantics and its own execution validation. A third-party native-device bypass must refuse, not quietly read requested WRITEONLY memory.

**Fixture proposal, with speculative-staging qualification below:** standalone actual d3dx9_37 CloneMesh through the real ownership wrapper plus the proposed scope observer, authored source mesh and exact target declaration, validating exact final VB/IB against the fixture's own authorized post-construction reads. Deterministically inject source-lock failure, destination Unlock failure (while CloneMesh itself may still succeed), readable-creation fallback, nested scope, qualifier reentry, Reset before final recheck, and a second-thread Reset parked at the registry barrier during copy. Require zero unqualified reads/publications; exact bytes only from fresh final destinations; final output identities and revisions match; no added Lock/Unlock/draw; original HRESULT/arguments/FP/LastError preserved; exception/unwind releases scope; no leaked refs. Limit initial RE acceptance to selected INDEX16; qualify INDEX32 separately if implemented. The root owns all build/Wine execution. No game run is needed to execute this prerequisite fixture, and none was launched here.


### Review correction and root ratification: access versus initialized content

The omitted index failure witness is exact: destination Lock succeeds at 0x00593fc2, source Lock at 0x00593fda; attribute-table allocation at 0x00594054 is checked at 0x00594060; null sets E_OUTOFMEMORY at 0x00594064 and branches to cleanup 0x00594145. Destination Unlock at 0x00594164 is reached before the first index store at 0x005940c5. Thus even both successful locks plus destination Unlock do not establish initialized indices. The first-store loop and whole-buffer initialization findings apply to the successful path only.

**Root explicitly ratified speculative opaque staging from the original mapping.** Legally readable, privately owned backing permits a bounded byte copy even when its contents have not yet been initialized by the producer. This is an access contract, not an initialized-payload certificate. The observer may only memcpy into private byte storage at that stage; it must not interpret positions/indices, hash bytes, log them, serialize them, or expose them to a consumer. Initialization qualification and publication require successful real CloneMesh, both observed original destination Unlocks returning S_OK, exact final public GetVB/GetIB identities matching the fresh staged allocations, and unchanged owner/generation/revisions. Any failure wipes provisional storage. Observable source-lock failures should refuse before staging; the attribute-OOM case remains a meaningful private-staging-then-wipe negative control. No extra Lock/Unlock is introduced by this selected path.

The root/author implementation contract also disables the legacy finite classifier for this diagnostic and decouples readable creation from that scanner. This is a required change, not a claim about current source: invoking the existing finite classifier on speculative bytes would violate the no-interpretation boundary.

Fixture acceptance must distinguish pointer-read permission from data qualification. The post-lock attribute allocation OOM may cause a private opaque copy but must produce zero interpretation/hash/log/file/exposure and leave staging wiped, with native HRESULT/arguments/CPU/LastError unchanged. A successful counterpart must publish exact final bytes. Continue to require refusal for unreadable fallback, wrong allocation, invalid window, known source-lock failure, invalidated mapping, nested/foreign activity and reset/generation changes. Final rechecks occur after all qualifier callbacks; CPU copy remains under the existing registry section with no callback inside it. An original native Unlock stays the original application's call, rather than a newly introduced cleanup operation.

### Considered post-success alternative: private ownership true, not selected

Root also considered two added construction-time READONLY Lock/copy/Unlock operations after real CloneMesh success and before adapter return. **Private ownership does extend through that interval:** DLL output at 0x005a6441 is still the suspended caller's stack local; until adapter return, EXE has not resumed at 0x004bcc30 or reached record publication 0x004bca6d/70/73. Public GetVB/GetIB would return refs only to the adapter, which must not leak them. This pointer/publication evidence does not rely on thread identity or quiet bookends.

The complete concurrent/reentrant Reset and added mapping-cleanup contract was not established for that alternative. The registry-only final memcpy barrier excludes wrapped Reset during CPU reads but does not cover native Lock-to-Unlock calls. Our initial blanket claim that any resource Unlock after failed Reset is prohibited was too strong: the Reset page's restricted-method language must be reconciled with the Lost Devices page's explicit locking guarantee and MANAGED resource persistence. No absolute resource-Unlock illegality is claimed. No added mapping/cleanup interval is selected, so this unresolved alternative does not block the root-ratified original-mapping staging path.

Public sources checked directly: [Reset](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-reset), [Lost Devices locking operations](https://learn.microsoft.com/en-us/windows/win32/direct3d9/lost-devices#locking-operations), [multithreading issues](https://learn.microsoft.com/en-us/windows/win32/direct3d9/multithreading-issues). Reset's device-creation-thread restriction remains relevant to fixture design. Native Windows runtime is unverified.
