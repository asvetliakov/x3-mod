# Motion producer integration review

This review records commit `0ce0814`. The subsequent
[performance review](review-08.md) refreshes shared result files for its changed
ownership source; this commit preserves the integration evidence below.

The source checkpoint connects recorded main-scene draws to private rigid-motion
GPU production. It does not enable temporal color accumulation, camera jitter or
any HDR visual feature. The installed iteration-5 DLL remains unchanged.

## Contracts and review findings

- Storage correspondence and temporal continuity are separate API contracts.
  `MotionHistory` defaults to temporal mode with unknown continuity; that default
  cannot reuse a previous frame. Diagnostic mode permits storage pairs while
  explicitly withholding temporal continuity. An explicit cut clears either mode.
  Root reviewed this change and its current source/report hashes; 3,404 Win32/SSE2
  checks pass. Committed separately as `0bbdf3a`.
- Geometry leases retain native allocations, never logical application wrappers.
  They have immutable finite/index requests, nonreused handles and bounded slots
  and bytes. Independent review required inspecting any current wrapper's
  Lock/Unlock endpoints again before exposing a retained native allocation. It
  also required retaining global quota charges until outside-lock native cleanup
  actually finishes, preventing concurrent admission from exceeding the bound.
- Execution tracking observes scene/state-block transitions and query intervals.
  Unsupported query types and unknown native execution remain conservative
  refusals. Review found that successful commands on an already lost device must
  not heal query knowledge. It also required full resource retirement for loss
  returned by getters, containers, private-data operations and ProcessVertices.
- Independent review also required preserving the caller's full computational
  x87 state, MXCSR and LastError around native injection. The Clear hook restores
  incoming state before the original call and its exact outgoing state after all
  instrumentation/destructors; the direct producer API separately preserves state.
- A failed injected motion pass permanently invalidates execution knowledge;
  successful later application commands cannot erase a restoration failure.
- Independent review accepted collection of every main-scene observation,
  including failed/ineligible duplicate keys, and frame-local GPU resource cleanup.
  The optional draw reader acquires a lease while actual getter references are
  alive, reusing the exact finite/index requests. Normal callers acquire no lease.
- GPU production before Clear is provisional. Separate end-frame telemetry records
  whether CPU storage history was committed after successful Clear, a still-valid
  scene selection and successful Present. Neither record claims temporal history.
- The motion PS is compiled from our authored HLSL and embedded; production replay
  requires no runtime compiler. The original game shader bytes remain untracked.

## Live dispatch gate

Final integration review found that the capture mutex does not exclude a worker
starting a VB/IB mapping after lease inspection. The serialized component tests
remain valid, but game-side GPU dispatch is explicitly disabled and reports
`enabled=0 reason=write_exclusion_unavailable`. Live execution tracking also
stays off because its transition/snapshot serialization is not yet established. A future mutation/replay exclusion
protocol must begin before native Lock/ProcessVertices, reject already-pending
mappings and remain held through replay submissions. No installed build changes
or new gameplay test are justified by this checkpoint alone.

## Verification status

Current-source component and affected regression results pass:

| Verification | Result |
| --- | --- |
| CPU matrix correspondence | 3,404 Win32/SSE2 checks |
| Embedded authored motion PS | 118 checks, 102 numerical samples, 30 state restorations |
| Execution state core | 60,365 checks each, optimized and ASan/UBSan |
| Native execution forwarding | 132 checks across 17 labeled cases |
| Native geometry leases | 319 checks, including delayed-destruction quota control |
| Reader with optional leases | 260 checks, 74 state snapshots |
| Private motion producer | 253 checks, 40 numeric samples, 28 GPU and 28 full CPU-state comparisons |
| Finite upload / buffer revisions | 385 / 698 checks |
| Copied depth / loss | 634 checks and 32 samples / 357 checks and 33 cases |
| Scene capture | 4,908 checks, 16 samples, 36 scenarios |
| Ownership parity | 370 native / 431 wrapped checks, verifier PASS |
| Python analysis | 289 tests |

Independent reviewers verified source, native binary, executable and report hashes
for the new components. The final 20-case actual-DLL matrix and 18-object forced
native fallback pass. Twenty Clear witnesses cover normal/pure devices, incoming
and outgoing x87/MXCSR/LastError, exact arguments, success and real native failure.
A fixture-only missing `libwinpthread` dependency was corrected with static linking
before the complete fresh matrix; no production failure was masked.

The verified source DLL SHA256 is
`feb1aa9142d7609fdb540ee6da6cda5983bccc7112f989fd8c9ff91ee12831ec`;
it is not installed. The fallback verification DLL is
`0c567b8c6dfa9bcd64a83029de9302f1d11ee7e2f37f7b537c8143b354238baf`.
Production symbol checks exclude fixture issuers and retain the real qualifier.
The installed game DLL was independently rehashed unchanged as
`ed19a7abf54ae2b9debf912f3d343a0c9217038a2162cb6a9eb174fc8050bbd5`. The private
motion fixture supplies synthetic shader/lifetime admission and boundary results;
it does not claim actual game orchestration or temporal-color eligibility.
