# Observed finite upload evidence

The ownership layer now provides opt-in CPU evidence for finite vertex positions and index bounds from application write mappings. `Options.capture_finite_positions` requires `track_buffer_writes`; the default is inert. This is an additional prerequisite for the draw reader, not evidence that a draw is eligible for temporal rendering. No production draw, jitter, readback or extra buffer Lock is introduced.

## Supported allocation and write contract

The allocation must be a newly observed readable MANAGED vertex or index
buffer. The opt-in creation adapter removes native WRITEONLY where needed and
retains the originally requested Usage in immutable allocation metadata for
wrapped GetDesc. All other creation arguments are unchanged, and failed
conversion/admission retries the original request. Existing readable usage-0
MANAGED buffers qualify directly. DEFAULT, DYNAMIC and unobserved adopted
allocations remain unsupported. See the public D3D9/COM contract and lifetime
rules in [portable managed upload](portable-managed-upload.md). The old pinned
qualifier is historical verification code only, outside production `src`.

The observer accepts an ordinary successful write Lock with flags 0 or NOSYSLOCK,
one pending mapping, a consistent observed revision, the same thread and the exact
returned pointer/range. Immediately before the application's original Unlock,
it revalidates the observed transaction, executes MFENCE with a compiler memory
barrier, and classifies the mapped bytes using integer bit operations. It never
modifies those bytes. Publication requires S_OK from the original Unlock plus
matching sidecar reservation, owner generation, revision, descriptor and observed
pending metadata. No native heap, map-count field, module hash or export address
is inspected.

Nested, cross-thread, DISCARD, NOOVERWRITE, ambiguous and unqualified writes cannot publish. ProcessVertices attempts invalidate destination evidence. A failed native Unlock cannot publish staged cells. A Reset attempt or observed device loss retires all atlases and advances the owner generation; a successful Reset permits a later newly observed upload to establish evidence. A permanent revision/private-data failure cannot be cleared by Reset.

Position queries use the actual stream offset, stride, position offset, effective first vertex and count. They support FLOAT3 or FLOAT16_4 XYZ, ignoring Half4 W. Missing cells return Unknown; NaN or infinity in required XYZ returns NonFinite. IB16/IB32 certificates require a complete observed upload. A subdraw may receive conservative whole-allocation extrema, explicitly marked `exact_range=false`; the caller must still validate those bounds against the draw's declared range and signed base vertex. Queries never map or read payload. They authenticate allocation metadata and call ordinary public GetDesc, not a GPU query.

Public D3D9 cannot discover arbitrary native writes outside the wrappers.
Trusted internal mutations must first call `invalidate_native_buffer_evidence`,
which advances the observed storage revision and invalidates finite summaries.
The caller serializes the entire following native interval against queries and
replay. A borrowed pointer permits trusted read-only inspection by contract; it
is technically mutable. Arbitrary unannounced writes and private-GUID replacement
are unsupported. Foreign vtable mutation, resource destruction, queries, uploads
and reset must be serialized as documented by the public API.

## Lifetime, reservations and refusal

A native resource private-IUnknown reservation owns one CPU-only sidecar. It has no owning native resource, wrapper, factory or device reference. Its shared owner contains only CPU counters, budgets and a weak intrusive list. This preserves allocation evidence when a wrapper reaches external zero and is recreated from an existing binding. A later native allocation at a reused address cannot inherit an externally retained old sidecar.

The process has hard limits of 32 MiB of retained atlas payload, 4,096 sidecars and 64 owners. Options can lower per-owner payload and sidecar caps. Sidecar/core/list metadata has a fixed count bound; `metadata_bytes` reports sidecar object sizes, while the separately bounded owner/control-block and allocator bookkeeping are not claimed as payload bytes. Reset releases atlas reservations while attached sidecar metadata remains charged. An externally retained private-IUnknown continues to consume its reservation until its final reference is reclaimed.

Native private-data Release callbacks may execute under a runtime mutex. Final sidecar Release therefore never waits for the ownership registry: an immediately successful try-lock permits CPU-only destruction, otherwise an atomic retirement queue retains the charged node. Safe ownership entry points drain that queue outside native callbacks. Acquisition uses a positive-reference CAS and cannot resurrect a queued zero-reference object. A final external release after all wrappers have gone can remain in the bounded queue until a later safe entry or process teardown.

Reservation validation records the expected sidecar's AddRef callback on the calling thread during public GetPrivateData. It does not infer ownership from the aggregate refcount, which may change through other valid external COM references. POD bytes equal to a sidecar pointer cannot impersonate a private-IUnknown reservation. Foreign replacement through a wrapper permanently disables the owner. Unsupported borrowed-native replacement with a foreign private-IUnknown may cause one returned foreign reference to be retained: its pointer cannot safely be distinguished from a POD spoof for Release. The owner then permanently refuses evidence, and an allocation-specific authentication-failure latch prevents repeated foreign GetPrivateData calls even from immutable GetDesc metadata lookup. Other allocations retain their immutable original Usage after normal evidence retirement. Deliberate metadata tampering has no descriptor-masking guarantee. The fixture explicitly measures and balances this tamper-only exception; ordinary supported paths preserve their reference accounting.

## State preservation and metrics

Lock and Unlock dispatch preserve their original arguments, output slots and HRESULT. The observer restores the caller's incoming x87 environment/register payload, MXCSR and LastError before the native call, captures the native outgoing state immediately, then restores that outgoing state after bookkeeping. Volatile XMM register values follow the normal ABI. FNSAVE with immediate FRSTOR, followed by final FRSTOR/LDMXCSR, is used because the native qualification fixture found that FXSAVE/FXRSTOR altered a live x87 tag/value on this Preview runtime. Queries preserve incoming state in the same supported manner.

Metrics are cumulative per owner: uploads, publications, invalidations, reason counts, bytes classified, position components examined, query-cache hits, and allocation failures. `scan_ticks` covers integer mapped-byte classification; `qualifier_ticks` covers public descriptor/window validation; `query_ticks` covers complete public evidence queries and therefore overlaps qualifier work inside those queries. They must not be summed as disjoint total CPU time. Counters and bounded first-refusal descriptor/flags are reported by proxy batches; there are no per-upload payload logs. These synthetic tests make no claim about live-game loading cost or admission rate.

## Verification

`python3 verification/probe/run_finite_upload.py` builds the actual ownership module and both production helper modules afresh, snapshots source/native-module hashes before and after compilation, checks a unique final PASS line and exact CHECK count, and verifies executable/report hashes after execution. A failure overwrites the summary with `passed:false`, so an old successful report cannot masquerade as fresh evidence. The final suite passes 534 checks. Results are retained in `verification/results/finite-upload-summary.json` and `finite-upload.txt`.

The actual native/wrapped fixture covers Float3 and Half4 finite/nonfinite cases; ignored Half4 W; partial preserving uploads; pending, nested, cross-thread and unsupported mappings; IB16/32 subdraw bounds; disabled mode and both budgets; wrapper recreation; Reset success, failure, pending writes and observed loss; ProcessVertices invalidation; foreign POD and IUnknown reservations; external sidecars after complete device teardown; deterministic native-callback/registry contention and deferred reclamation; concurrent external AddRef during reservation acquisition; and a permanent tracker SetPrivateData failure.

A targeted control explicitly announces a native mutation, then opens the
borrowed native mapping while wrapper pending count remains zero. Notification
advances the storage revision, and the finite query returns Unknown with
`NativeContract`. Native Unlock alone does not restore the snapshot; a later
observed upload rebuilds covered evidence. This replaces the historical private
native-map-count test and does not claim standard D3D9 can detect that bypass.

Portable controls cover VB/IB application-versus-native descriptors, immutable
Usage after wrapper recreation/Reset/permanent tracking failure, exact-end ranges,
zero-size normalization and overflow, unsupported pools/flags, null/wrong-kind
invalidation, off-tracking wrapper forwarding loss, and repeated GetDesc after foreign private-IUnknown replacement.
Converted Create failure with untouched or cleared output and an actual sidecar
SetPrivateData attachment failure each trigger the exact original retry. Seeded
full x87/MXCSR/LastError controls verify both the retry's incoming state and the
selected native call's outgoing state. Failed GetDesc preserves untouched or
partially written native output.

Native-versus-wrapped success/failure comparisons exercise HRESULT/output, LastError and seeded nondefault/live-x87 state. Ordinary writable mappings compare full native payload bytes after observation, using readable MANAGED native backings. Fixture-only scheduling callbacks make the two concurrency regressions deterministic; they are absent from production builds. The portable evidence core has separate optimized/sanitizer/oracle evidence. Broader ownership and draw-reader regressions are retained by their existing runners.

Native allocation failure and metadata-attachment failure are injected at the
original COM boundary; the fixture does not force a physical device/OS allocation
exhaustion. No game was launched and no installed DLL was changed. Runner native
DLL hashes record before/after provenance only; they are not version allowlists.
Native Windows execution and live-game cost remain unverified.
