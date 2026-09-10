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
