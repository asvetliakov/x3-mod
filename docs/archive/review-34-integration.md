# Review 34 — merged loading and exposure integration

## Motion runner provenance and merge review

This section records the independent source review of the uncommitted merge
of the reviewed exposure branch into main after the CryptoAPI cache merge.
It does not report a final-main runtime pass. Root owns the final build,
selected runtime checks, frozen artifact audit and installation.

`run_motion_output.py::sources()` now includes these ten additional execution
inputs in its source-stability checks:

- `tools/analysis/exposure_reference.py`, `agx_reference.py`,
  `analyze_motion_readback.py` and `summarize_capture.py`;
- `verification/probe/verify_ownership_integration.py`,
  `run_ownership_integration.py` and `verify_capture_state.py`;
- `verification/probe/bottle.py`, `game_guard.py` and `wine_lock.py`.

The list includes the transitive local imports of the admission verifier;
`wine_lock.py` is the required execution entrypoint rather than an imported
module. The reviewed exposure branch's 196 saved cases retain their original
scope: six helper hashes were recorded after the runs, and the later
reference docstring change has a separately reviewed executable-AST equality
record. This new manifest does not retroactively strengthen those records.

The motion fixture environment explicitly disables the CryptoAPI cache,
loading probes, mesh cache, adjacency replacement/dumps, resource-reader
replacement, DAT-handle reuse and gzip buffering. Native adjacency and reader
modes are explicit; the disabled gzip buffer has its standard 256 KB setting.
These values override inherited host variables. Dedicated loading fixtures
exercise those optimizations; motion cases are not evidence that real game
loading and rendering have been exercised together.

The merge review compared the resulting files to both parents:

- Capture retains main's CryptoAPI include, opt-in loading initialization,
  cumulative session report, dynamically sized fallback capture path and
  UTF-8 path logging. Exposure adds its six bounded meter parameters without
  replacing those paths. Resource-reader initialization still follows the
  loading hooks.
- The CLI retains the CryptoAPI option/environment assignment and existing
  loading controls, together with the exposure options, bounds checks and
  environment assignments.
- `motion_output.cpp` and `.h` are byte-identical to the exposure parent.
  The fixture retains its 16-float exposure witness, extended scene cases,
  last-covering-region witnesses and returned-unlock failure control, while
  preserving main's corrected MSAA refusal description. No merge loss was
  found in this bounded review.

Host-only checks passed: Python parsing, evaluation of the manifest function
with all ten added files present, and AST inspection of all nine explicit
loading settings (including the already explicit mesh-cache switch). No
Wine command, compiler build or gameplay launch was performed for this
review. Root reviewed the imported helper set and explicit native/off settings,
then found that selected mode returned before the existing full-suite
post-run source check. The follow-up records `sources_after_run` and asserts
equality with `sources_before_build` before publishing `PARTIAL`. A changed
source reaches the existing failure handler; selected mode never becomes a
full-suite pass. Host execution of the actual selected AST branch confirmed
unchanged inputs retain `passed=false`/`PARTIAL`, and changed or missing
helper entries are rejected with the after-map retained. The pre-fix branch
accepted the same changed-input counterexample. This repairs selected-run
source provenance; earlier partial records are not retroactively upgraded.
Root separately reports production-flag syntax checks passing for merged
`capture`, `motion_output`, `hdr_pass` and `exposure`, and 60 targeted
AgX/exposure settings, portability/reference and CryptoAPI host tests passing
in 7.8 seconds. These are host/source checks, not native-Windows runtime
verification.

Selected motion runs deliberately retain `PARTIAL` status: that runner mode
returns before its full-suite cross-case comparisons. Their individually
validated cases must not be described as a new full-suite pass. The final
build should also compare the seam's discovered production object set with
the actual CMake link response, since the seam links `find`-discovered objects.
Unchanged, independently reviewed standalone and performance evidence can be
reused after checking its actual source dependencies against the final tree.

After the exposure merge and selected-runner correction, root checked the
reader's 21-entry before/after source maps against main: both still match.
Its independently reviewed 4,721-check X3 result therefore remains applicable
to the reader sources; exposure integration does not require a duplicate run
of that unchanged standalone fixture.
Root also reran the reader and adjacency host suites: 36 tests passed in
9.55 seconds. Together with the 60 tests above, this is 96 targeted host
tests on the merged tree, not a new full-repository test-suite claim.

## Final combined qualification result

On frozen source `ae03d9a`, X3 completed all eleven selected motion cases:
production off/on, ownership on, HDR ramp, TAA/AgX; and seam exposure,
offset exposure, wrapped exposure, tonemap-fault recovery, meter-self-test
unlock refusal and TAA/automatic exposure. They passed **1,156 checks across
435 frames**. The runner correctly retains `PARTIAL` and `passed=false` for
its full-suite verdict; the separate integrated audit records
`SELECTED_VALIDATED`. Full exposure branch reports remain historical and
byte-preserved.

The candidate DLL SHA-256 is
`ae2482fd5146c62898fbe20c45441d9d14183d705c50e3d03872094ec635b193`.
All 141 expanded source entries match before/after/current; all 40 production
objects match the CMake response/archive/disk inventory. The production DLL
has exactly 17 exports, its audited boundaries pass no-x87 verification
(211 reachable functions, zero violations), and all ten authored shader
programs pass recompilation with `--check`. The durable artifact audit is
[integrated-motion-validation.json](../../verification/results/bottle-X3/integrated-motion-validation.json).
An initial host-audit filename assumption for a copied off-case trace was
corrected against the actual retained trace; it was not a runtime failure.

The final merged [X3 CryptoAPI fixture](../verification/final-crypto-integration.md) also
passes **572 checks in twelve processes**, with 72 source entries stable
before build, after build and after execution. These are modular integration
tests; the user's subsequent gameplay run supplies real game co-activation
and visual acceptance. Native Windows has not been run. Independent artifact
review is recorded in [review-34-artifact-audit.md](review-34-artifact-audit.md).
