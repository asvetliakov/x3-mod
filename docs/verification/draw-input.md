# Live draw-input acquisition verification

The production `DrawInputReader` acquires the current application draw's exact
shader rows, input declaration, buffer allocation/revision identities, submitted
range, target/viewport and raster state through the ownership boundary. It does
not reconstruct a matrix or issue a rendering command. The local proof bits are
partial evidence: it deliberately never supplies `LifetimeVerified`, even with
a complete object-trace scope. Opt-in finite XYZ evidence now comes from the ownership observer’s ordinary
MANAGED+WRITEONLY upload records for the exact revision and draw range. The
reader never locks or reads back vertex data. Lifecycle identity, stability until
replay and ownership of final scene color remain external gates.

The original Win32 fixture builds that production reader together with the real
ownership and resource-ID implementations. It creates an actual D3D9 device in
CrossOver Preview with `track_buffer_writes=true`. Compile-only callbacks accept
only the fixture's exact original shader bytes; neither callback nor the
original shader contracts are included in production. The archive shader
registry is verified separately. The fixture stubs only capture log output.

Run without launching the game:

```sh
python3 verification/probe/run_draw_input.py
```

The [summary](../../verification/results/draw-input-summary.json) records the
fresh source/header/include/runner hashes, native D3DX and pinned D3D9/WineD3D
DLL hashes, executable hash,
command and report hash. A previous PASS is invalidated before reading sources
or invoking the compiler. Build and run must preserve all recorded inputs.
The build uses SSE2 and realigns the four-byte incoming Win32 stack contract.

The extended native run passes **219 checks**, including **63 ordinary caller-state
comparisons** and seven getter-failure controls. The original 167-check default-off
behavior remains covered alongside the opt-in finite-upload cases.

## Original controls

- Submitted c0–3, c6–9 and c24–27 rows are copied bitwise, including signed zero
  and nontrivial values. Positive/negative infinity, quiet/signaling NaN and an
  excessive finite magnitude are rejected without rewriting the copied bits.
- Actual FLOAT3 and FLOAT16_4 vertex declarations and native draw calls work;
  stored half W=7 does not alter the input-layout decision. This is acquisition
  verification, not a new pixel-equivalence claim for replacement shaders.
- Indexed and nonindexed submissions, triangle strips, INDEX16/INDEX32 and
  nonzero stream offsets retain their exact API interpretation. A bound IB,
  including one with a pending write, is ignored by nonindexed draws.
- Successful writes advance VB/IB revisions; pending writes and a never-written
  zero revision are rejected. A missing IB rejects indexed draws only.
- Unsupported declarations, UP/invalid methods, zero/overflowing ranges, negative
  effective vertices, incomplete scopes, unknown shaders and partial viewports
  remain explicitly unsupported. Alpha test, blending, stencil, scissor, depth
  bias, slope bias, missing depth writes, unsupported Z comparison, color mask
  and wireframe cannot claim supported coverage.
- Successful submission grants only its own proof; failed submission revokes it.
  Complete scope addresses/handles/session still produce zero lifetime tokens.
- Seven original getter fault seams return failure after populating plausible
  outputs (caps, rows, raster state, stream, declaration and VB/IB descriptors).
  VB/IB descriptors contain valid native sizes but return E_FAIL: neither
  geometry nor coverage is approved based on those populated bytes. Failure cannot
  produce coverage approval; acquired references are released.
- Caller-state comparisons cover reader-consumed state plus surrounding vertex
  constants, scissor and unrelated sampler state. RT/DS identities compare the
  native resource's allocation ID, because a released application wrapper can
  be recreated at a different address without changing the bound resource.
  Device/VB/IB reference counts remain equal across ordinary reads, and Reset
  succeeds after releasing the fixture's DEFAULT buffer. The reader therefore
  retains no DEFAULT references across these controls.

## Opt-in finite-upload extension

A separate fixture device enables both write tracking and finite-position capture.
Its real MANAGED+WRITEONLY VB/IB allocations receive ordinary successful uploads,
using the backend-qualified ownership observer. Positive reader results must
carry a finite state, the same VB revision, a nonzero evidence generation and,
for indexed draws, a known certificate for the matching IB revision. Local proof
bits and source-program qualification remain separate: the fixture’s synthetic
lookup callback does not issue an archive replay token.

The extension checks finite FLOAT3, half-float XYZ with exceptional stored W,
positive nonfinite XYZ evidence, replacement and partial uploads, actual index
bounds against the API’s declared vertex interval, pending mappings, stale
revision queries and whole-IB certificate invalidation after a partial update.
Nonindexed finite queries ignore a bound bad IB. Native resource pointers cannot
claim wrapper-owned evidence, and Reset invalidates old attestation. All reader
calls retain the caller-state and reference checks used by the original controls.

The certificate conservatively covers the full declared vertex interval after
validating whole-allocation index extrema. This may refuse a valid subdraw;
it does not guess an exact index subrange from incomplete metadata. The observer
still requires serialized uploads, draw queries and later replay, and cannot
certify foreign writes that bypass the ownership boundary.

This checkpoint verifies input gathering and conservative refusal. It does not
supply whole-scene temporal coverage, object-lifetime hooks, geometry retention,
jitter, motion replay or a game TAA acceptance result.
