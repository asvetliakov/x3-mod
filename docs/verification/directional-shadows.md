# Directional-sun-share verification ledger

2026-09-15 source-artifact checkpoint. Independent deep review: **ACCEPT**.
The new off-by-default extraction API has no live-renderer caller, configuration
change or resource allocation. Its source checkpoint was committed as `0234178`;
this ledger makes no installed-build claim. Current installation state is in
[status](../status.md).

## Host evidence

The focused host suite passed **13 tests in 26.331 s**. It covers 432 extracted
variants from 108 originals and 152 authored sun-colour MAD consumers. The
baseline retained **1,388 legacy outputs byte-exact**. The host tail/oracle
checks cover 432 depth-on tails, 108 zero-sun cases, 152 isolated authored
lobes, 28 component-domain cases, and 39 synthetic planner/resource cases.
Forced allocation rollback preserves output and failure state at 75 hull and
101 XT allocation points.

Generated variants use **136–342 weighted slots** (an addition of 32–45) and
remain within the 512-slot target. Those figures are static host results;
create-time latency, GPU execution cost and GPU register pressure are
unmeasured.

## Boundaries

This checkpoint proves neither live integration nor GPU execution. No GPU,
Wine, game, Reset/recovery, resource-publication or consumer-integration proof
ran. It also does not establish native-Windows shader creation or raster
semantics. The extraction contract and family-specific evidence are in
[sun-share-material-contract.md](../reverse-engineering/sun-share-material-contract.md).

## Native bytecode blocker at the original checkpoint

Native ps_3_0 validity is currently blocked by an inherited ordinary-material
sanitizer instruction: it reads two distinct float constants in one `MAX` (for
example, `c5` and `c212.y`). All 108 ordinary PS programs have this violation.
The documented ps_3_0 float-constant register limit is one read port per
instruction ([Microsoft register reference](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx9-graphics-reference-asm-ps-registers-ps-3-0)).
The new sun-share instructions comply, but cannot make the containing programs
valid. The repair subsequently landed as `5b3b5c3`, with legacy/fill/fade GPU parity
([repair evidence](linear-material-constant-port.md)). Native Windows runtime
verification remains open.

## 2026-09-15 runtime integration qualification in progress

The isolated runtime implementation at `/tmp/x3-sun-share-runtime` is not yet
integrated. Independent review accepted the corrected cutout positive control;
actual MotionOutput execution and composition coverage remain acceptance gates.
The following retained X3 GPU results were revalidated by that reviewer:

- Material extraction: 216 cases, 55,296 valid drawn pixels (27,648 positive
  and 27,648 zero sun share), 256 empty controls, no invalid pixels; maximum
  subtraction error 0.000686797113. Raw report:
  `/tmp/x3-sun-share-material-gpu/report.txt`.
- Temporal channel copying: eight history twins, four copy failures, two sizes
  across two generations, and 24 hostile-state restorations. Summary:
  `/tmp/x3-sun-share-runtime/verification/results/bottle-X3/sun-share-temporal-summary.json`.

Both executions used the single Wine lock, bottle X3, WineArch arm64,
`FEX_X87REDUCEDPRECISION=1` and `WINEMSYNC=1`. These fixtures do not execute
the actual MotionOutput qualification/publication path. They therefore do not
establish live receiver admission, composition exclusion or complete recovery.
The separate qualification object build passed with zero warnings; its local
record is `/tmp/x3-sun-runtime-fixture-build.json`. It is not an install candidate.
Native Windows execution, gameplay coverage and GPU performance remain open.

### Initial actual-renderer execution checkpoint

The actual MotionOutput positive case passed 43,269 fixture checks and 18 state
restorations across six frames, including Reset; all six TAA readbacks are byte
identical to the R32 reference. The runner initially misparsed nested
`detail=stage=...`; its corrected parser revalidated the retained output without
a GPU rerun. Four early-failure cases (capability, dropped cutout, alpha-write
mask, allocation) also passed, covering 24 additional frames.

At this initial checkpoint qualification remained incomplete: the late-shader case stopped at frame 2
on a TAA-history expectation, and composition stops at frame 1 on an exchange
publication expectation. These were unresolved fixture/runtime diagnoses, not accepted
fallback evidence; the later qualification below resolves them. Small local witnesses:

- Positive: `/tmp/x3-sun-live-positive-revalidation.json`.
- Early passes and late-shader failure: `/var/folders/l6/0sdq5b49401b_4m_26gsl1f00000gn/T/x3-sun-share-live-zfrdwrrm/failed-result.json`.
- Composition: `/var/folders/l6/0sdq5b49401b_4m_26gsl1f00000gn/T/x3-sun-share-live-oarlm2ss/composition/stdout.txt`.

No integration or install follows from this partial checkpoint.

## Runtime integration checkpoint — independent review PASS

The default-off `--sun-shadow-lane` is integrated in source. It publishes a
qualified sun-share/depth lane with same-frame exclusions; it does not render
shadows. The [compact evidence](../../verification/results/sun-share-runtime.json)
binds extraction, temporal and actual-renderer slices to their executable and
DLL identities. Reviewer independently revalidated all 11 actual-renderer cases:
**66 byte-exact TAA frame comparisons, 40 history frames and 11 Resets**.

The fixes separate two fixture defects (duplicate motion keys and non-indexed
submission outside additive admission) from the production history defect.
Successful same-size/same-generation G32-to-R32 fallback now preserves motion
rows; Reset, resize, loss and allocation failure retain normal invalidation.
The extracted production-method fixture passed 49 checks, 27 creates and zero
surviving surfaces; two affected review tests passed in 2.417 s.

Actual additive composition runs cover 18 frames, with exclusion-mask union
counts 4,608 / 3,072 / 3,072 for ordinary/missing/failed coverage cases. Missing
or failed coverage invalidates sun availability and uses current-only TAA with
no history seed. Late shader failure preserves ordinary TAA but an absent cached
variant remains unavailable after Reset (`shader_cache`); bind-only failure with
a complete cache requalifies. No unsafe original-pointer reconstruction occurs.

Final six-case Wine execution took 14.665 s, with negligible lock wait, on X3/
arm64 with `FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`. Retained unaffected
positive/early-failure slices avoid unnecessary reruns. Earlier shader extraction
and temporal-copy qualification remain as recorded above. Native Windows,
gameplay coverage and GPU performance remain open; composition evidence here
is the additive exchange route. No shadow replay, cascades or install is claimed.

## Run25 / snapshot run60 — gameplay coverage refuses every frame

All 8,950 emitted sun-lane frames are unavailable. Each has an untracked-writer
coverage veto (3–99 draws per frame); 1,138 frames also have nonzero completed
exclusion coverage, which is insufficient to remove the veto. The first emitted
witness, frame 1228, has 238 receivers and ten untracked writers with owner
valid and G32R32F active. All 26 variant records report successful extraction
and creation. The [compact observations](../../verification/results/run60-observations.json)
retain identities, counts and source locations.

This is a gameplay coverage blocker, not useful shadow-lane acceptance. The
aggregate cannot identify the refused draw populations or exclude an independent
earlier sticky failure. Next work adds bounded reason buckets and capped cached
draw signatures before changing eligibility or availability. TAA resolves in all
149 sampled periodic records, including the gate window; there are no raster
captures to establish visual continuity. No shadows are applied.

## Refusal-reason diagnostics for the untracked-writer veto (worktree, reviewed once)

Diagnostics only; eligibility, availability and composition are unchanged
(`SunShareFrame::draw` decides untracked exactly as before and now also returns
that verdict and increments a reason bucket). Reasons are the first refusing
gate of the motion route, in chain order (`sun_share_frame.h`,
`SunUntrackedReason`): `unknown feature scene unregistered pair no_zwrite
blended state rows geometry no_depth fade_arm apply_failed scope history
read_failed`. `unknown` means only that no gate was recorded. `unregistered` is
a PS outside the registry (unknown or not yet registered) or a VS without a
profile row; `pair` a registered row without a reviewed pair (or the xt pair not
ready); `read_failed` any failed state getter in gate 4 (z, z write, blend,
test, sRGB, color mask, stream frequency), reported before any value-based
bucket; the remaining gate-4 buckets are the first failing check in the chain's
order, using the values and the cutout verdict the chain itself computed (no
getter is repeated; a draw failing only the cutout arm's exact-state check is
`state`); `scope`/`history` are gates 5/6 (they still route, so they can only
surface as untracked through `no_depth`/`fade_arm` mapping or not at all);
`no_depth`/`fade_arm` are routed draws whose profile writes no depth or that
took the fade arm; `apply_failed` a rolled-back apply.

Cost: with the lane off the refusal path gains three unconditional byte stores
on the route (gate id, z state) and nothing else: no allocation, hashing,
getter or log. With the lane on, each draw already counted untracked runs a
linear scan of the at most 64 cached signatures; the bucket line is formatted
once per frame with untracked writers at scene end.

Grammar:

- `sun_shadow_lane_refusals device=%llu frame=%llu untracked=%lu unknown=%lu
  feature=%lu scene=%lu unregistered=%lu pair=%lu no_zwrite=%lu blended=%lu
  state=%lu rows=%lu geometry=%lu no_depth=%lu fade_arm=%lu apply_failed=%lu
  scope=%lu history=%lu read_failed=%lu signatures=%u overflow=%u`: once per
  frame, after `sun_shadow_lane_frame`, only when `untracked_writers` > 0;
  buckets sum to `untracked`.
- `sun_shadow_lane_writer device=%llu frame=%llu index=%u vs=%016llx ps=%016llx
  reason=%s gate=%u registered=%u z=%u zwrite=%u z_known=%u
  declaration=%016llx stride=%lu`: once per distinct signature (vs, ps, reason,
  declaration id, stride, z/zwrite state, registered) per device, at most 64
  per device; the cache is cleared at attach and before Reset (declaration ids
  may be recycled); further distinct signatures increment `overflow` on the
  refusals line. `gate` is the `MotionGate` value. Every writer frame is a
  refusal frame.

`tools/analysis/analyze_sun_share_lane.py` attaches `untracked_reasons`,
`writer_signatures`, `writer_overflow` per frame plus `untracked_reason_totals`,
`refusal_frames` and `writers`. A malformed bucket line sets the frame's
`diagnostics_malformed` (`refusal_line_truncated`, `refusal_buckets_mismatch`,
`refusal_total_mismatch`) and never drops the readback/eligibility analysis.

Evidence (2026-09-15, worktree `worktree-agent-a8311c34c14d25910`, bottle X3,
WineArch arm64, `FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`), after the review
fixes (gate 5/6 and read-failure buckets, exact cutout verdict, Reset clearing,
malformed-line handling, writer-frame check):

- `PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_sun_share_lane verification.analysis.test_linear_sun_share`: 19 tests OK; sun-share host `PASS checks=30` (was 24).
- Clean CMake build: zero warnings, `build/d3d9.dll` sha256 `beb33aa9…6a867`; `check_no_x87.py build/d3d9.dll`: 225 reachable functions, no violations. Strict `-Wall -Wextra -Werror` compile of `motion_output.cpp`: clean.
- `run_sun_share_live.py` (fixture `verification/probe/build/motion_output_fixture.exe`, seam `verification/probe/build/motion-output-seam/d3d9.dll`, both rebuilt in the worktree): all 11 cases pass, 22.7 s total; positive `checks=43269 restorations=18`, untracked `checks=43272 restorations=19`. Before the review fixes the same run reported positive `checks=43272 restorations=19` and untracked `checks=43269 restorations=18`, byte-identical to a HEAD-baseline seam linked from the unchanged sources (only the new lines differ); per-case frames/histories/resets/TAA/composition counts unchanged. Across the three runs of this session (HEAD baseline, first build, reviewed build) the positive and untracked cases each reported either `43269/18` or `43272/19`, swapping between runs: a pre-existing run-to-run variation of three checks and one restoration in the fixture, not a function of the diagnostics change.
- The `untracked` case (an unreviewed PS alteration drawn with z write off) reports `untracked=1 pair=1 signatures=1 overflow=0` and one writer line `reason=pair gate=3 registered=1 z=1 zwrite=0 z_known=1`; the two failed-composition cases report their frame-2 emission writer as `unregistered=1` (VS without a profile row). The validator requires the bucket line, its completeness and sum, the `pair` bucket and the signature line for that case, refuses duplicate signatures and writer lines outside refusal frames.
- Run60 ran without this build; the next gameplay run with `--sun-shadow-lane` produces the bucket and signature lines directly.
