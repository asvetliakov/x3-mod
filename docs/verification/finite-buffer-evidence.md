# Compact finite-position and index evidence

The portable [core](../../src/ownership/finite_buffer_evidence.h) implements the
metadata described in the [upload-evidence design](../architecture/finite-position-evidence.md).
It owns no COM object, native mapping or payload pointer. Its only heap storage
is a vertex-buffer atlas; index buffers retain inline extrema instead of payload.
No floating-point conversion/arithmetic, GPU operation, new native Lock or game
payload capture is performed by this module. Native observation and ownership
integration have separate acceptance gates.

## Atlas, queries and budget

Each aligned four-byte cell has four bits: known, low-half finite, high-half finite,
and float32 finite. Classification reads the mapped bytes using integer shifts
and IEEE exponent masks. Both signed zeros and all finite subnormals qualify;
all infinities and NaNs reject. FLOAT3 queries check three four-byte classifications.
Half4 queries check only the first three half values, preserving native storage
layout while ignoring W. A known cell containing nonfinite W can still certify
finite XYZ. A partially mapped cell remains unknown even when some useful half
components lie inside it: the core never reads beyond the supplied window to
repair a fringe.

Queries include the exact content revision, stream offset, stride, position
offset, signed effective first vertex, count and storage type. FLOAT3 needs
four-byte alignment; Half4 needs two-byte alignment, with correspondingly aligned
strides. Full element bounds include ignored W storage. The allocation and layout
bounds are checked with widened subtraction/division **before multiplication or
range addition**. Zero count, invalid alignment, negative first vertex, oversized
arguments, a stale revision and pending writes return unknown. Nonfinite means a
known nonfinite component was found; unknown elsewhere does not erase that
witness. One exact query result is cached, including finite, nonfinite and unknown
results. Repeated matching queries scan no more components.

`required_payload` returns `ceil(ceil(byte_size/4)/2)` bytes for a vertex atlas,
with checked address-size limits. The supplied allocation budget is enforced
before allocation; allocation failure leaves no atlas or evidence. Reinitializing
releases the previous atlas first, so there is no doubled transient atlas charge.
Index metadata requires zero retained payload bytes. **The owner must reserve the
aggregate/global and per-allocation budget, including its sidecar/core metadata.**
The core's payload budget does not enforce a device-wide limit by itself. No
staging copy or query allocation is hidden outside that number.

## Mutation and publication

Creation starts unknown. `begin_write` accepts only one supported preserving
mapping with a new, nonzero, increasing owner revision. Only `(0,0)` denotes the
whole allocation; nonzero offset with zero length is refused. It invalidates all
intersecting cells and cached queries immediately. A skipped revision clears the
entire atlas before the current span may establish fresh evidence: unobserved
intervening writes cannot preserve untouched cells.

While pending, every query is unknown, including a query for the previous
published revision. `stage_mapped` classifies only complete mapped cells in-place;
it keeps no pointer and allocates no second atlas. The supplied length must match
the normalized mapping exactly. A successful partial/no-complete-cell stage may
prove nothing. After native Unlock, `finish_write` publishes only a matching,
previously staged transaction whose owner reports success and unchanged trusted
identity/reservation/revision/native state. Failed, missing, duplicate or mismatched
staging/finish, nested begin, unsupported/discard mode, malformed ranges and
explicit invalidation reset all evidence. Staged cells cannot leak into a later
preserving write after failure. Revision high-water survives invalidation;
rollover cannot silently re-adopt old evidence. `reset` releases storage and starts
an allocation-generation boundary.

This is a serialized metadata state machine, **not a native mapping validator**.
The owner must enforce the exact dispatch/readability/coherence contract,
same-thread mapping and writes, MFENCE before scan, flags, pointer/window validity,
allocation/contract identity, native pending-lock accounting, and absence of
foreign writes. It must invalidate for ProcessVertices, lost tracking, reset/loss
or contract changes. The core cannot infer those events or make an untrusted
mapping readable. Its revision and budget inputs are owner contracts.

## Index evidence

A complete mapped Index16/Index32 upload computes actual unsigned min/max and
publishes it only after matching successful finish. Every partial write
invalidates the simple whole-buffer certificate. Partial stages do not scan
indices and cannot reconstruct extrema from the old certificate.

A nonempty in-bounds subdraw can obtain the whole-buffer extrema as a conservative
bound, with `exact_range=false`; those values are never labeled as its exact
subrange extrema. `indexed_vertex_range` checks those actual bounds against the
application's declared relative interval, checks signed base addition and the
complete effective declared range, and returns the first vertex for a conservative
position query. The vertex query still validates against the actual VB allocation.
Unknown extrema, negative effective ranges and overflow refuse. This can reject
safe subdraws whose whole-buffer extrema exceed their declared interval, or whose
unused declared vertices are nonfinite. It does not invent per-triangle evidence.

## Verification

Run `python3 verification/probe/run_finite_buffer_evidence.py`. The runner builds
and executes optimized host and ASan/UBSan variants, compiles the actual core for
i686 using SSE2/four-byte incoming-stack flags, and inspects that target object's
instructions for accidental floating arithmetic. All source/artifact/report hashes
are frozen before and after execution. It invalidates a prior PASS before reading
inputs, bounds build/run times and requires exactly one terminal RESULT line.
Two runner regression tests cover malformed/duplicate/nonterminal results and a
missing source replacing a retained PASS. It starts no Wine, GPU or game process.

Both host variants pass **214,651 checks**, including:

- Every 16-bit encoding in each XYZ lane: 196,608 inputs, including alternating
  cell alignment from stride 10/offset 2 and independently varying ignored W.
- Both signs and all 256 float32 exponent fields with five mantissa boundaries
  per lane: 7,680 inputs, including zero, subnormal, finite extreme, Inf and NaN.
- Creation unknown, exact layout/revision cache, changed ranges, storage alignment,
  byte/vertex/index overflow, zero count and stale/unknown revisions.
- Exact-sized partial mapping buffers under ASan, tiny windows `(1,1)`, `(1,2)`
  and `(2,1)` with zero classified bytes, fringe invalidation and disjoint
  preservation across consecutive supported revisions.
- Pending/staged invisibility, failed or missing finish, duplicate stage, nested
  mapping, null pointer, length/revision mismatch, unsupported/discard mode,
  skipped revisions, explicit invalidation and rollover.
- Budget rejection before allocation, deterministic allocation failure,
  reinitialization without doubled retained memory and complete release.
- Real Index16/Index32 extrema, whole/subrange distinction, partial-write
  invalidation, failed commit and signed-base/declared-range checks.
- Hostile FP rounding/exception state preserved by classification and queries.
  The i686 object contains no detected x87 or SSE floating arithmetic.

A 4,096-vertex query scans 12,288 components once; 10,000 exact cached queries add
zero scans. The optimized host sample recorded 14,750 ns for that cold query and
39,791 ns total for the cached loop; sanitizer timings are separately recorded.
These tiny host microbenchmarks establish cache behavior, **not Win32/game loading
cost, native observation overhead, future game coverage or a measured speedup**.
See the [manifest](../../verification/results/finite-buffer-evidence-summary.json)
and [optimized report](../../verification/results/finite-buffer-evidence.txt).

The independent reviewer also compared 640,000 randomized query results against
a separate byte/cell-knownness oracle, including partial writes, failed commits
and revision gaps, without a mismatch. The review harness stayed outside the
repository; the reproducible checked-in suite above is the retained evidence.
