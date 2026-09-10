# Rigid-object correspondence

The detached production `MotionHistory` provides current/previous submitted WVP
rows for a conservative rigid-motion replay. It does not reconstruct engine
floating-point arithmetic or assume a stationary world. The caller first records
every candidate, seals the frame, then queries pairs. Only a fully successful
frame may be committed as the next history generation.

This two-phase API matters when one object/range appears several times in a
frame. A later conflicting matrix or missing-proof observation invalidates the
whole matching key before any motion is emitted. Identical duplicate matrices
are coalesced; signed-zero bit disagreements are conservatively ambiguous.
No first-draw/last-draw or index-in-frame matching is used.

Each exact key includes trusted object and camera lifetimes, a draw domain,
node/camera/mesh identities, handles, model/LOD, VB/IB allocation IDs and known
content revisions, declaration/program identities, input offsets/type/stride,
topology and all draw ranges. FLOAT3 and the observed FLOAT16_4 position formats
have different byte-size checks. The producer must positively establish all five
proofs: lifetime, stable geometry, reviewed position path, supported coverage and
successful submission. Missing or extra proof bits reject correspondence.

Matrices are copied bit-for-bit from submitted shader rows. Only finite bounded
values are admitted; no transpose/recomposition is performed. Lookup rejection
returns no matrices. `observe(true)` means stored, never that it is eligible.

The previous frame must be adjacent, committed and have the same dimensions and
trusted epoch. Gaps, repeated/reordered frames, zero frame fields, epoch/size
changes, overflow, explicit invalidation, incomplete/failed frames and API misuse
discard prior validity. Missing geometry keys never borrow another object's
transform. The epoch must change at scene/camera cuts, reload/reset and resource
or coordinate/exposure regime changes; ordinary frames retain it.

Storage is bounded by submitted observation count, including duplicates, with a
default capacity of 8,192 and a hard ceiling of 65,536. Overflow and allocation
failure invalidate the entire frame/history rather than selectively retaining
potentially ambiguous entries. Storage owns no COM or engine pointers; integer
identities do not keep resources alive. The caller must serialize access and
ensure referenced buffers remain unchanged until GPU replay.

## Original-data verification

Run `python3 verification/probe/run_motion_history.py`. The runner freshly builds
the actual production implementation and original fixture for Win32 with SSE2
and the four-byte incoming stack contract, executes only that console fixture in
CrossOver Preview and records source/executable/report hashes. Build failures
leave the manifest nonpassing rather than retaining an earlier passing result.

The fixture passes **3,210 checks**, including exact prior/current row retention
for moving objects, every key field, draw reordering, all missing proofs, NaN/
infinity/oversized matrices, both duplicate arrival orders, signed zeros, missing
objects, failed/uncommitted frames, resize/epoch/gap/reset behavior, frame-number
overflow, bounded duplicate floods and injected allocation failure. Check count
includes assertions that rejected results expose no matrix components.

[Result](../../verification/results/motion-history.txt),
[provenance](../../verification/results/motion-history-summary.json).

## Remaining integration

This is a correspondence mechanism, not proof that the current game observer
supplies its required lifetimes. [Disassembly](../reverse-engineering/object-lifetimes.md)
establishes useful load/retirement seams but not universal mutation coverage.
Do not turn pointer/handle equality or a manually constant epoch into a lifetime
attestation. Whole-frame TAA also needs supported jitter, valid scene depth and
per-pixel rejection/reactivity for unsupported color contributors. This module
is not yet wired into the installed game.
