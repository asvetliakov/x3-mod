# Independent review of the experimental 0.4 checkpoint

Reviewed 2026-09-10 before checkpoint commits, as requested by the user. Reviewers
were separate from the ownership and temporal authors. The review covered the
canonical ownership cleanup, explicit scene-depth copy, loader/capture wiring,
temporal resolve, depth decoder and the verification runners. These components
were uninstalled at that initial review; the consolidated diagnostic build was
subsequently installed and tested by the user. Later corrections described below
remain uninstalled. Gameplay visual integration is a separate gate.

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

## Continued review: runtime and consolidated diagnostics

The following independent passes cover subsequent 0.4 source checkpoints; they
do not broaden the earlier installed-game evidence.

| Component | Finding and correction |
| --- | --- |
| Detached production temporal pass | Reject unknown motion-policy values; preserve device-loss priority when state restoration follows an earlier failure. The fixture checks caller-state restoration, failure publication, output aliasing, queries and stateblock recording. |
| Engine submission trace | Preserve owned patch/protection recovery records before mutation, including partial installation failure; disable observation when TLS setup fails. Exact executable fingerprint, call ABI and nested/foreign-SEH restoration are separately verified. |
| Scene-depth adapter | A recognized wrapper can return S_OK while its internal source-binding query fails. Confirmation now requires successful view status and a confirmed bound source, not just valid storage/epochs. A real copied-and-cleared snapshot plus one injected query failure distinguishes this from a failed Clear or stale resource. |
| Buffer content revisions | No blocking implementation finding. Document required serialization between buffer operations and snapshot queries: metadata locking alone does not make the native mutation and subsequent bookkeeping transactional. Disabled or ambiguous tracking never establishes stable content. |
| Loader, capture wiring and installer | No further blocking findings in option gating, buffer query lifetime/status logging, or explicit DLL-source selection. The installer retains checksum ownership and temporary-file replacement safeguards. |
| Mesh loading hooks | Keep original page-protection recovery records until restoration succeeds, including partial installation and shutdown failure. Refuse IAT reinstallation after teardown so a foreign interceptor cannot form a recursion cycle through an overwritten original. Independent review accepted both fixes and their fault regressions. |

The scene-adapter review also required explicit documentation of unobserved
resource CPU writes and alternate presentation paths. The supported callbacks
are not proof of every possible D3D9 content mutation. Buffer revisions currently
remain in raw capture records; the existing general summary parser does not yet
derive motion correspondence from them.

Standalone scene and buffer fixtures are linked from their respective
[adapter](scene-capture.md) and [buffer](buffer-content.md) verification notes.
The scene fixture invokes adapter callbacks explicitly; actual proxy-hook
wiring is a separate DLL integration gate.

The final combined build passed all 15 DLL integration cases and the forced
adoption-failure native fallback. The scene fixture passes 20 scenarios, 2,228
checks and eight GPU samples; buffer diagnostics pass 530 checks. Mesh timing
passes 68 ABI/IAT and 123 actual native-mesh checks. Existing ownership contracts
remain at 370 native / 431 wrapped checks; copied-depth and loss regressions pass
634 checks / 32 samples and 357 checks / 33 cases respectively. The copied-depth
run preceded only the buffer header's serialization-comment clarification; runtime
ownership code was identical. The analysis suite passes all 87 tests.

Final experimental DLL SHA256:
`81e3b121659c0fa1811641a5dbe019f668341477a787c6729fb5a848e056c516`.
This identifies the combined standalone-verified artifact, not a completed
gameplay visual acceptance test.

## Review after the completed user session

Reviewed 2026-09-11, before the subsequent checkpoint commits:

| Component | Review and correction |
| --- | --- |
| Capture analysis | Guard absent camera identity before attempting camera deltas; require exactly four finite components in each distinct matrix row 0–3. Regression cases preserve unknowns rather than invent motion. Independent light review verified trace, analyzer and regenerated CTAB hashes. Loading claims remain inclusive observed spans, with no inferred cache hit rate. |
| Material radiance | Independent review of exact hash/length/profile gates, instruction boundaries, local zero constants, unchanged alpha/PP/unrelated clamps, atomic rejection and aliases. Five full-program comparisons match independent expected bytes; original GPU fixture passes 42 structural checks and 192 samples through Reset. No game shader replacement is enabled. |
| Full shader sweep | Independent counts/provenance check covers all 17 catalogues, 3,480 effect entries, 751 programs and 57 runtime-dump matches. Review caught unsupported `sincos` arity being treated as complete; it now stays unknown with eight regression cases. No archive program uses it, so program inventory records remain unchanged. The complete analysis suite passes 148 tests. |
| SSE2 compilation | Use `-msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2`, retaining Win32 conventions and ST0 returns. Independent policy review agrees with the ABI probe: 36 callback cases and three scalar returns pass; deliberately incompatible incoming-16 assumptions fail alignment checks. The production naked thunk instructions are unchanged. Object tracing passes 139 checks, temporal rendering passes 44 samples / 40 state comparisons, and material rendering retains its 42 / 192 result. |
| Scene selector and diagnostics | Independent review accepted optional haze with verified background, narrow nonalias scratch-fill acceptance and first-rejection retention. Root review then found failed ColorFill diagnostics could dereference rejected arguments; inspection found the analogous adapter StretchRect path. Both now query arguments only on success. Callback-only invalid-pointer controls are never submitted to native D3D. Final independent delta review verifies matching source hashes and 36 scenarios / 4,908 checks / 16 GPU samples. |

These changes do not establish successful game depth capture: the old trace
lacks ColorFill target identities. They also do not turn camera/object observations
into lifetime-safe motion vectors or the detached material module into FP16 scene
rendering. The current installed diagnostic DLL still has the historical hash
above; source corrections and standalone verification are separate artifacts.

After the final pointer-safety changes, a fresh combined build again passes all
15 DLL integration cases, log/cleanup verification and the forced native fallback.
Uninstalled corrected DLL SHA256:
`b81af5d3c0c9c0fd7cbed54ee7fe613453f8309288b17d7602916e3fa0bd1896`.
