# Capture format 2: typed state and allocation identity

The 0.2 proxy keeps the backend COM objects and draw calls intact. Requested
frames add state needed to distinguish scene inputs before building TAA and new
lighting. It does not implement a temporal resolve or alter shader/render state.

## Device and resource identity

Each device gets a monotonically increasing process-local lifetime ID. Frames
are identified by `(device, frame)`, not by frame number or COM address alone.
The capture summary uses `device:frame` keys for version 2; legacy single-device
logs keep their old frame keys. Sequential devices can reuse exactly the same
address without merging their capture history.

Resources receive an eight-byte ID in project-owned GUID private data, with flags
zero. The runtime owns this small copied metadata and destroys it with the
resource. No COM pointer is stored in it and no resource reference is retained by
the identity registry. Getters' temporary references are released. This follows
[IDirect3DResource9 private-data semantics](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3dresource9-setprivatedata).
A failed metadata read/write records `identity=0`/an error; it never falls back to
an address that might identify an old allocation. If private metadata is removed,
a later ID can conservatively change; IDs are not portable across processes.

These are **allocation IDs, not object or mesh-content IDs**. Several ships can
share a vertex buffer. A dynamic buffer can change contents without changing ID.
Future motion-vector matching must combine allocation/slice/transform evidence
and account for content updates and instancing, or obtain engine entity identity.

## Draw geometry and results

Requested draws include stream slots, allocation IDs, byte offsets, strides,
frequency flags, buffer descriptors, index bindings, and the complete scalar draw
arguments. Binding queries observe state restored through D3DX state blocks.
Query failures are explicit; an absent binding is different from a failed getter.
`GetStreamSource` adds a reference, which the proxy releases after inspection.
[Microsoft stream-source contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-getstreamsource).

For DrawPrimitiveUP/DrawIndexedPrimitiveUP the geometry source is `user_memory`.
The proxy records the call's supplied pointers/stride/index format but does not
attribute previously bound buffers to that draw or read arbitrary user memory.
[Microsoft UP draw contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-drawindexedprimitiveup).

Each intercepted draw has a `draw_result` HRESULT after the unchanged backend
call. A capture of a draw attempt is not automatically evidence of rendered
geometry. Frame summaries also compare recorded and reported draw counts.

Color/depth surface records include their allocation IDs and, when available,
the ID of their containing texture. This connects render destinations to later
texture reads without treating equal-sized surfaces as the same resource.
Standalone/backbuffer surfaces may have no texture container; that is expected.

## Typed constants

Float, integer and boolean registers use distinct `type=f`, `type=i`, and `type=b`
namespaces for both vertex and pixel stages. All 16 int4 and BOOL registers are
recorded, including zero values. Floats retain exact bit patterns (signed zero,
NaN and infinities included); all-zero rows are omitted only under an explicit
successful `encoding=sparse_zero` query status. Float query ranges derive from
cached device limits, bounded by the proxy's supported SM3 range.

The camera analyzer requires explicit successful float-query coverage and a
successful draw result for version 2. A missing/out-of-range row cannot be
invented as zero. Legacy files retain their documented sparse-zero interpretation,
with explicit failure records excluded. Integer/boolean query failure does not
invalidate a separately successful float query.

Pure-device getter support is backend-dependent. The current Preview WineD3D
fixture successfully queries these namespaces and bindings with flags 0x52.
This does not guarantee that another backend supports every query.
## Additive telemetry in build 0.3

Schema 2 remains the draw/constant format. With `--telemetry`, ordered
`capture_event` records add device/frame/sequence, preceding draw count, operation,
result and QPC; detailed Clear/RT/depth/copy arguments follow. The summary tool
keeps these under each frame's `events` and checks sequence continuity. They are
not attached to the previous draw's targets. `draw_begin` precedes the backend
draw; its placeholder success does not override the later `draw_result`.

CPU timing and loading aggregates use separate `telemetry_*` and `loading_*`
events. See [telemetry documentation](../verification/telemetry.md). Existing
schema-2 captures remain readable; telemetry is not required to decode them.

## Additive object and buffer diagnostics in build 0.4

`--object-trace` observes one fingerprint-verified engine submission scope. When
that observer is active, each captured draw includes an `object_context` record
with its explicit `device`, `frame`, `index`, `scoped`, `valid`, observer `session`
and nesting `scope_depth`. Pointer/handle/model/LOD fields are observations from
that scope, not globally stable entity identifiers. A missing record means no
observation was emitted; it must not be converted into an all-zero valid object.
`scoped=0` means the current draw has no observed submission scope. `scoped=1`
alone does not establish that any optional memory read succeeded.

The decimal `valid` mask separately identifies successful bounded reads:

| Bit | Read group |
| --- | --- |
| 1 | Node position/basis/scale and node metadata |
| 2 | Camera handle |
| 4 | Engine/registry pointers |
| 8 | World matrix storage |
| 16 | World-basis matrix storage |
| 32 | View matrix storage |
| 64 | Projection matrix storage |

Matrices are emitted as `object_matrix role=<world|world_basis|view|projection>`
with four rows of four hexadecimal words. Node position uses `object_position`
with three words; node basis uses three `object_basis` rows of three words.
The four node scale words are **`object_matrix role=scale row=0`**, not a separate
`object_scale` event. These are copied raw words. Row/role labels alone do not
establish engine matrix multiplication, coordinate units, handedness, normalized
basis, or the meaning of every scale component. Shader-constant conventions
verified elsewhere must not automatically be applied to these engine-memory rows.

The summary preserves `object_context` as a raw field dictionary and
`object_matrix`, `object_position`, and `object_basis` as ordered lists of raw
field dictionaries under each draw. Hexadecimal `bits` strings remain unchanged,
including signed zero, NaN payloads and infinities; no Python float conversion or
implicit transpose occurs. These bounded diagnostic rows are retained regardless
of `--include-floats`, whose existing purpose remains optional shader float-register
retention. Unknown roles/fields and unavailable status values remain visible.

`object_context_matches_draw` is true only when all three recorded scope
coordinates equal the enclosing draw's device/frame/index. It checks record
attachment only: it does not interpret `scoped`/`valid` or establish object matching.
A mismatched context and its raw rows remain visible for diagnosis; consumers
must reject them as scope evidence. Rows without a context remain raw observations,
not inferred valid matrices. Records outside a current draw, including after a
separate capture-event boundary, are not attributed to an earlier draw. No object
context or row is carried forward into a later draw.

Ownership mode can also record observed vertex/index buffer writes, enabled when
the object observer is actually active. After each queried buffer descriptor,
`buffer_content` records `kind`, allocation `identity`, API `result`, metadata
`status`, `requested`, `known`, `ambiguous`, `revision`, `pending` lock count and
last lock `flags`. The summary stores these records as an ordered per-draw list,
including repeated identities from multiple stream bindings. It does not collapse
vertex/index namespaces or invent metadata for UP/user-memory geometry.

`result=00000000` means a recognized ownership wrapper, not necessarily usable
metadata. Native pointers, disabled tracking, failed metadata queries, pending
locks and sticky ambiguity retain their explicit statuses. Revision zero is not
stable-content evidence. Even requested, known, nonambiguous metadata with no
pending locks describes only writes observed through this ownership boundary;
it is not a payload hash or proof that unobserved native writes did not occur.
Revision equality therefore does not establish engine-object correspondence or
valid motion vectors. No summary field automatically labels a buffer stable.

The additive fields do not alter existing geometry/constant/resource summary
fields. Older captures simply lack these keys. Original metadata-only parser
fixtures cover valid/missing/unscoped/unknown contexts, mismatched coordinates,
raw matrix bits, disabled/pending/ambiguous/failed buffer tracking and draw/frame
isolation in `verification/analysis/test_capture_object_summary.py`.
