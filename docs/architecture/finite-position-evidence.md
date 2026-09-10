# Finite POSITION evidence without per-frame buffer reads

The smallest useful route is an opt-in **upload-observation certificate** attached
to each buffer allocation. Observe the application's existing writable mapping,
classify CPU-visible bytes before its native Unlock, and publish only after that
Unlock succeeds. Later draws query compact metadata using their actual layout,
range and content revision. This requires a positive native mapping
readability/coherence contract; D3D Lock success or WRITEONLY alone cannot supply
it. The reviewed [classification core](../verification/finite-buffer-evidence.md)
and [exact Preview VB/IB qualifier](../verification/managed-upload-contract.md)
are now implemented and independently tested. The [allocation observer](../verification/finite-upload-observer.md)
passes 385 native/wrapped checks, and the combined source DLL passes 18 cases plus
native fallback. The installed iteration-5 game build remains unchanged; no game
finite-coverage result is available yet.

## What the completed iteration 0.5 capture establishes

A read-only metadata audit of `/tmp/x3-iteration05-completed-snapshot.log`
(SHA-256 `e5beaa861d04659fe9c7df05a01845bd05d656a33c643f4b484ff379cf3ccaf8`,
216,605,445 bytes) selects exact archive profiles whose reviewed source is SM3
row-dot/MAD. There are **11,382 successful indexed draws using seven profiles**.
These are shader-position candidates, not already eligible motion draws.

| Observed property | Count / value |
| --- | ---: |
| POSITION0 storage | All FLOAT16_4, offset 0, stream 0 |
| Stride 40 / stride 24 draws | 11,178 / 204 |
| VB pool / usage | All MANAGED (1), WRITEONLY (8) |
| IB pool / usage | All MANAGED (1), WRITEONLY (8) |
| Distinct VB allocations / IB allocations | 1,035 / 1,035 |
| Sum of distinct VB allocation sizes | 115,059,368 bytes |
| Sum of distinct IB allocation sizes | 12,356,076 bytes |
| VB allocations observed in multiple captured frames | 1,032 |
| Observed VB and IB content revisions | All 1, known, unambiguous, no pending locks |
| Last observed lock flags | All `0x800` (NOSYSLOCK) |
| Draws declaring the complete VB range | All 11,382 |
| Draws consuming the complete IB range | All 11,382 |

The complete VB ranges have base/min/stream offsets zero and
`NumVertices * stride == VB bytes`. Complete IB ranges have startIndex zero and
three indices per triangle accounting for the full IB size. There are 1,035
distinct allocation/revision/layout/declared-range combinations. Their XYZ
components contain 17,259,750 bytes when counted once per combination.

The reproducible [metadata audit](../../tools/analysis/analyze_finite_position_inputs.py)
reads the fixed snapshot once, checks its exact SHA-256 and stable size/mtime,
and selects the 32 `homogeneous_row_dots` archive profiles with
`shader_version == 0xfffe0300` in
`verification/results/shader-profile-registry.json` (report SHA-256
`7794751b6f134189c9bf1160fab28a6580c0641c130a25ef886baf37dbb01127`).
Only successful indexed draw-result records in successful, count-matched complete
capture frames contribute to the size/range totals; nonindexed candidates are
reported separately (zero here). Coordinate and allocation-identity mismatches
reject analysis. Run from the repository root:

```sh
python3 tools/analysis/analyze_finite_position_inputs.py \
  /tmp/x3-iteration05-completed-snapshot.log \
  --expected-sha256 e5beaa861d04659fe9c7df05a01845bd05d656a33c643f4b484ff379cf3ccaf8 \
  --output /tmp/finite-position-inputs.json
```

The [compact output](../../verification/results/finite-position-inputs.json) records
snapshot, registry and analyzer hashes. It retains
metadata, not raw payload or a large per-draw report. The separately reviewed
[lifetime audit](../reverse-engineering/iteration05-lifetimes.md) agrees that all
28 captured frames are complete.

These counts describe sampled draws and historical allocation totals, **not peak
memory, upload timing, the complete set of created buffers, measured scan cost or
a guarantee of future hit rate**. The trace does not contain uploaded values or
Lock offset/length pairs. Revision 1 does not prove that an initial lock covered,
or the application initialized, every byte in the allocation.

SYSTEMMEM-only readonly warmup covers **zero** of these material VBs. The existing
mesh-adjacency SYSTEMMEM contract must not be applied to MANAGED render buffers.
Microsoft explicitly says not to combine WRITEONLY creation with READONLY Lock
for either [vertex buffers](https://learn.microsoft.com/en-us/windows/win32/direct3d9/accessing-the-contents-of-a-vertex-buffer)
or [index buffers](https://learn.microsoft.com/en-us/windows/win32/direct3d9/index-buffers).
A once-per-revision readonly warmup can remain a separate future route for
supported readable pools; it cannot solve the observed main-material case.

## Smallest useful producer

Keep existing BufferContent revisions as the authority for observed write order.
Add separate allocation-owned finite-evidence storage with no reference back to
the buffer/device wrapper. It must survive canonical wrapper recreation and be
freed with the native allocation; wrapper address is not its key. A separately
reviewed native private-data sidecar owning only its own memory is one possible
lifetime mechanism. Do not add payload pointers or unbounded vectors to the
existing fixed BufferContent POD without a distinct ownership design.

Because captured buffers use FVF zero, the future draw's declaration/stride is
not available at CreateVertexBuffer or necessarily during the initial upload.
Scanning only a declaration already observed at draw time would miss stable
buffers that never write again. A small layout-independent atlas solves that:

- For each aligned four-byte cell, retain four bits: **known**, low-half finite,
  high-half finite, and float32 finite. Unknown cells never attest a position.
- Compute classifications with integer loads/copies and exponent masks, without
  floating conversions: binary16 finite iff exponent bits are not `0x7c00`;
  binary32 finite iff exponent bits are not `0x7f800000`. Both signed zeros and
  finite subnormals pass; infinities and all NaNs reject.
- A FLOAT3 query uses three aligned float32 classifications. A FLOAT16_4 query
  uses only its first three half classifications. Stored W may be NaN/Inf and
  must not reject XYZ. Require appropriate two/four-byte alignment and a
  compatible stride; unsupported alignments remain unknown initially.

Four bits per four-byte cell cost **12.5% of covered VB capacity**, approximately
14,382,421 bytes for the sampled historical VB-size sum, before metadata and
rounding. This is an arithmetic storage estimate, not a measured live footprint.
A hard per-device/process budget, allocation caps and unknown-on-budget-failure
are required. The atlas retains classifications, not game vertex payload. Its
cold scan still reads the covered upload cells; no bandwidth or loading-time
improvement is claimed. A full raw mirror would instead retain roughly the
115 MB sampled VB total, and is unnecessary for finiteness alone.

## Mutation and publication rules

1. At creation, all evidence is unknown. Successful Lock is not evidence that
   the application's requested span was initialized. Scan only bytes whose final
   readable/coherent mapped representation is covered by the native contract.
   If the contract covers only actually written bytes, an exact CPU upload-source
   interval is additionally needed; generic Lock does not reveal those stores.
2. On successful writable Lock, existing revision tracking advances before the
   pointer reaches the application. The initial managed contract accepts only
   flags 0/NOSYSLOCK; all other modes remain unsupported. Invalidate intersecting
   atlas cells and all
   cached range certificates for the old revision. DISCARD invalidates the whole
   allocation, including untouched regions. NOOVERWRITE is not permission to
   ignore an intersecting write; D3D lock flags are application promises, not
   [runtime-checked write boundaries](https://learn.microsoft.com/en-us/windows/win32/direct3d9/locking-resources).
3. Retain pointer, normalized byte window, flags and associated revision only
   for exactly one observed pending mapping. Never dereference a failed Lock's
   output. The pinned native contract supports `(offset,size) == (0,0)` as the
   whole buffer, but rejects nonzero offset plus size zero; that native dirty
   window is not automatically the remaining bytes. Validate nonzero spans
   against allocation length even when native Lock succeeds.
4. Require the initial supported Lock/Unlock path to remain on the same thread,
   with application writes complete and serialized. Execute an explicit **MFENCE
   before the pre-Unlock scan** to make preceding non-temporal stores visible;
   neither API call ordering nor a compiler-only barrier substitutes for this.
   Cross-thread write handoff needs its own ordering contract and initially
   remains unsupported. Stage classifications for fully covered aligned cells. Do not read outside the mapping to repair
   partial cells. An overlapping partial write makes affected fringe cells
   unknown; unchanged cells may carry forward only under a verified no-discard
   preservation contract. Do not fill unknown/unwritten bytes or invent zeros.
5. Publish after native Unlock succeeds and the same allocation/revision is
   known with no pending locks. Failed Unlock, nested locks, missing metadata,
   tracking failure or reservation tampering invalidate the evidence. No extra
   Lock/Unlock calls or new writes to application payload are introduced.
6. ProcessVertices success invalidates destination evidence; its existing
   revision event supplies no CPU payload. Bypassed-native/foreign writes cannot
   be repaired by a revision key. Preserve the existing ownership boundary's
   serialized-call requirement. Reset/device loss invalidates certificates and
   pending mappings; an initial implementation may conservatively discard even
   managed atlases rather than silently retaining an unreviewed loss contract.

A range certificate key needs device/allocation identity and generation, content
revision, stream byte offset, stride, POSITION byte offset/type, effective first
vertex and count. Include the exact native acquisition-contract generation where
applicable. It stores **finite / contains-nonfinite / unknown** separately. Reuse
only while that revision remains known and unlocked; recheck before actual
replay. Declaration/storage equality and finite payload do not prove geometry
correspondence, object lifetime, source-program compatibility or exclusive color
coverage.

## Index evidence and the useful initial range policy

For nonindexed draws, derive the exact consumed vertex interval from topology and
start/count; a bound IB is irrelevant. For indexed draws, widened signed/unsigned
arithmetic must validate base + actual indices and buffer byte bounds.
`MinVertexIndex/NumVertices` are the application's
[declared indexed range](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-drawindexedprimitive),
not a captured copy or independent proof of the actual index values. A successful
native draw does not establish that the buffer obeyed those promises.

The linked managed-mapping artifact now documents the exact **VB and IB** Lock,
Unlock, GetDesc and private-data endpoints. A shared WineD3D buffer path alone
would not establish the D3D9 IB entry; the typed qualifier and its 461 native
checks enforce that separate boundary. Issuing an IB certificate still requires
the ownership observer's revision, lifetime, flags and successful-publication
checks.

A conservative first producer can scan a complete observed IB upload and retain
its actual min/max plus known range, format, allocation and revision. For the
sampled whole-IB/whole-VB draws this is sufficient to validate all indices and
then require **every XYZ in the entire declared vertex interval** finite. It
avoids retaining indices or looking up finiteness per triangle. Unused nonfinite
vertices can cause a safe rejection. Whole-IB min/max can also conservatively
bound a subdraw, but should not be relabeled as that subdraw's exact min/max.

A partial IB write invalidates this simple whole-buffer certificate. Supporting
arbitrary partial uploads later needs valid per-block/range min/max summaries or
another independently proved source; do not reuse old extrema after an overlap.
Observed draw ranges being complete does not prove original Lock ranges were
complete. The next implementation should report unknown-upload coverage rather
than infer it from the draw parameters.

## Gates before a useful game build

The [pinned native managed-mapping investigation](../reverse-engineering/managed-buffer-write-mapping.md)
now supplies a positive static contract for the installed Preview backend:
MANAGED/WRITEONLY buffers use a pinned initialized CPU heap shadow; the returned
ordinary writable mapping addresses that same shadow, and the dirty window is
the source of the later upload. This permits actual classification of untouched
bytes **within a valid mapped window**, without pretending the application wrote
every byte or certifying anything outside that window. It does not license a
new READONLY lock on these buffers.

The producer must enforce the documented exact module/dispatch, flags,
bounds, single same-thread mapping, MFENCE, ordinary immediate-queue unmap and
serialization constraints. The implemented observer's native fixture and
independent review now verify that integration under the bounded contract.
A VirtualQuery permission check alone is insufficient.
If those runtime gates fail, evidence remains unknown; a concrete CPU upload
source/copy boundary is an alternative requiring its own proof, not a fallback
to per-frame readback.

Acceptance covers original FLOAT3/half4 payloads, all exponent classes and ignored W;
partial/unaligned writes, DISCARD/NOOVERWRITE, nested/failed Lock/Unlock,
ProcessVertices, wrapper recreation, revision rollover/ambiguity, reset/loss,
budgets, index overflow/negative effective base and stale-revision rejection.
Compare native HRESULT/output/LastError, FP state, references and resource bytes.
Measure CPU scan time/bytes and cache reuse separately in the standalone fixture.
Only after those gates should a batched live diagnostic expose known/unknown
range coverage, allocation/revision changes, bytes scanned and bounded CPU cost.
There is currently no measured finite-payload coverage or per-frame speedup.
