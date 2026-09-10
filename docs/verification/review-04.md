# Independent review of the experimental 0.4 checkpoint

Reviewed 2026-09-10 before checkpoint commits, as requested by the user. Reviewers
were separate from the ownership and temporal authors. The review covered the
canonical ownership cleanup, explicit scene-depth copy, loader/capture wiring,
temporal resolve, depth decoder and the verification runners. These components
remain uninstalled; gameplay rendering and state integration are separate gates.

## Findings and corrections

| Finding | Correction and distinguishing evidence |
| --- | --- |
| P1: texture-center UV was used directly for raw D3D9 camera reprojection | Subtract half a texel before camera inversion and restore it after prior projection. Original rasterized geometry plus zoom failed the old shader at 0.382568 versus expected 0.375; corrected zoom/rotation and the complete 58-check GPU suite pass. |
| P2: copy preflight loss could leave a previous snapshot exposed as valid | Retire borrowed snapshot and adopted renderer resources on observed DEVICELOST/DEVICENOTRESET. Inject loss at query, mutation and restoration boundaries, including ordinary failure followed by restoration loss. |
| P2: optional initialization and custom stateblock methods did not observe loss | Observe native loss after temporary-reference/output cleanup while preserving application HRESULT/output behavior. Do not permit new renderer-resource adoption on an already observed lost device. |
| P2: decoder midpoint rounding could turn interior depth into exact clear depth 1 | Clamp non-endpoint output below 1 before selecting true endpoints. An independent IEEE float32 model found the adjacent-to-one D24 counterexample even though the measured Preview shader happened to pass it. |
| P2: FP16 fixture conversion lost an exponent carry near powers of two | Correct carry and nearest-even rounding; verify values below 2 and 8 and tie cases. |
| P2: mixed-depth temporal fixture could pass if all history was rejected | Change surviving history to differ from current color. The expected blend now distinguishes partial acceptance from blanket rejection. |
| P2: FP16 object-motion input could lose required absolute UV/depth precision | Require RGBA32F input; expected previous depth 0.5002 tests a case that FP16 would round to 0.5 and incorrectly reject. |
| P2: some runners attributed current source hashes to an existing executable | Freshly compile consumed inputs and verify source/executable stability across build and execution. Preserve raw report bytes in Git for exact hash verification. |

The corrected temporal implementation received an independent fresh GPU rerun:
58 of 58 numeric checks, two resource generations and successful Reset. The
ownership loss regression passes 357 checks across 33 cases. The review also
checked canonical child/parent lifetime, getter/container
paths, output-on-failure semantics, default-off behavior and capture cleanup
after child-induced final Release. Concrete findings were fixed before the final
affected regressions; no review waiver substitutes for passing evidence.

## Evidence and remaining scope

- [Temporal GPU checks and deliberately failing old-shader regression](temporal-resolve.md).
- [Native depth-copy content, compatibility, lifetime and loss regressions](copied-depth.md).
- [Decoder precision, endpoint model and timing limits](depth-decode.md).
- [Actual-DLL integration matrix and forced adoption fallback](../../verification/probe/ownership_integration.md).
- The Python analysis suite passes all 70 tests, including the new decoder
  arithmetic and runner-provenance regressions.

These are bounded code and standalone-runtime checks. Decoder restoration tests
cover the named states, not all possible caller state. Timings include event-query
polling and do not establish game frame cost. No review result establishes game
object correspondence, complete scene/HUD boundaries, full-resolution performance,
or final TAA/HDR visual quality. Those still require implementation and a later
user-coordinated game test.
