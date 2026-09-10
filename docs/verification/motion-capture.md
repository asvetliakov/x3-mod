# Private pre-Clear motion diagnostics

`MotionCapture` joins bounded CPU matrix correspondence, real ownership geometry
reservations and the fixed rigid GPU replay at a supplied scene boundary. Its
output is private diagnostic data. **Camera continuity and final-color coverage
remain unknown; this is not TAA-eligible output or an installed game feature.**
Live proxy dispatch additionally refuses with `write_exclusion_unavailable`: its
capture mutex does not exclude worker-thread buffer mappings between validation
and replay. The component fixtures explicitly serialize those operations. See
[the exclusion requirement](../architecture/motion-resource-leases.md).

The standalone fixture links the actual `motion_capture.cpp`, `DrawInputReader`,
ownership/finite-upload/geometry-lease code, observed execution state and rigid
GPU pass. It creates original normal and pure D3D9 devices under CrossOver
Preview. This bounded integration fixture uses FLOAT3, nonindexed triangles;
indexed and packed-half replay have separate component fixtures. Original
MANAGED+WRITEONLY uploads supply finite geometry evidence;
there is no extra geometry Lock/readback at collection or replay time.

Shader admission and engine lifetime are explicitly synthetic boundaries. The
reader uses compile-only callbacks for an original MAD/DP4 vertex shader and a
constant-color pixel shader. The fixture confirms that finite evidence does not
qualify these bytes as an archive shader, then assigns the distinct
verification-only synthetic replay contract and acquires a real geometry lease.
A link-only `object_lifetime::current` stub supplies controlled registry epochs
and object/camera serials. The production game lifecycle hooks are not used.

Likewise, the fixture constructs `Selection` from the actual bound resource IDs
and supplies the Clear-confirmation and Present-result arguments directly. It
performs actual application Clears, but the failed-confirmation and failed-Present
cases are lifecycle-input simulations: there is no deliberately failed native
Clear or native Present call. It does **not** test the complete
`capture.cpp`/`SceneCapture` selection/confirmation orchestration. That requires
the separate proxy integration checks and a future authorized game capture.

## Verified boundary behavior

The final run passed **253 checks, 40 numeric components, 28 native GPU state
comparisons and 28 CPU-state comparisons**, covering normal and pure devices and
a real Reset on each. Source, executable and native-runtime hashes match the
recorded manifest. Source or runner changes require a fresh run before current
provenance is claimed.

- The first frame publishes only the invalid sentinel `(0,0,0,-1)`.
- Adjacent committed geometry with current clip-X translation `0.25` produces
  `(0.390625,0.515625,0.5,1)` at raster pixel `(16,16)` in a 32×32 target. These
  values follow directly from the independent scene geometry/identity prior
  transform and D3D9 texture-center offset.
- A late duplicate with conflicting rows invalidates that key before any motion
  can be emitted. A writable VB upload between observation and boundary revokes
  the existing lease; it cannot reuse old finite/revision evidence.
- Active real occlusion-query scopes and real state-block recording refuse the
  diagnostic. A changed synthetic registry mutation revision likewise refuses.
- Failed selected-Clear confirmation and failed supplied Present result prevent
  correspondence commit. The next frame has no usable prior pairing.
- The replay preserves an already-open application scene. Numeric readback is
  performed only in separate closed-scene cases, avoiding changes to that scope.
- Successful replay releases all geometry reservations before returning. Explicit
  cancellation before Reset/teardown retires pending collection; actual native
  Reset succeeds, refreshed uploads are required and old correspondence is gone.
- A test-only native `DrawPrimitiveUP` failure during replay initialization
  publishes nothing and permanently marks execution `NativeBypass`; the next
  frame refuses replay. This is labeled failure injection, not physical device loss.
- Every successful/refused/failed boundary preserves a hostile full 108-byte x87
  environment/live stack, MXCSR and LastError. The synchronous consumer deliberately
  perturbs them, as does the injected failed draw. An independent assembly oracle
  compares bytes; production CPU-state helper code is not reused by the fixture.
- Returned commit status must agree with production/confirmation/frame outcome;
  every boundary reports continuity and color coverage unknown.

The diagnostic consumer reads the borrowed RGBA32F texture synchronously while
it is valid, copies four numeric values and releases all temporary COM references
before returning. It retains no output texture. Production performs no readback.
The fixture compares native RT/DS, viewport/scissor, shaders/declaration,
streams/frequencies, indices, texture/sampler bindings, affected render states
and shader constants before/after the boundary. This is a defined state inventory,
not a claim that every native D3D9 slot is tested.

## Reproduction and report guards

Run `python3 verification/probe/run_motion_capture.py` with the user game stopped
and the synthetic GPU slot available. The runner fresh-builds with SSE2 and the
four-byte incoming Win32 stack contract, forces native builtin D3D9 only for this
process, checks the game-process inventory, and hashes all linked sources,
generated headers, executable, report and the three native runtime binaries.
It fails if inputs change during compilation or execution and invalidates an old
passing manifest before starting. No game launch or installation occurs.

The terminal report must contain exactly 253 checks, 40 samples, 28 GPU-state and 28 CPU-state
comparisons, and both normal/pure device records. Missing checks/samples,
wrong state counts, missing devices, duplicate/trailing results and nonpassing
records reject the run. `python3 verification/analysis/test_motion_capture_report.py`
provides ten portable parser controls, including the zero-check false-PASS case.

Evidence: [report](../../verification/results/motion-capture.txt) and
[provenance](../../verification/results/motion-capture-summary.json).
