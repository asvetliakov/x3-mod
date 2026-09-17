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

## Caster-candidate counter (`--shadow-replay-candidates`, lane-independent)

Implementation of [shadow-replay-gates.md](../architecture/shadow-replay-gates.md)
"Implemented": the §3 counter on the motion route, requiring `--motion-output --ownership`
only. Evidence (2026-09-15, worktree `worktree-agent-ade528d6254daa0e2`, bottle X3,
WineArch arm64, `FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`):

- `PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_shadow_replay_candidates verification.analysis.test_motion_output_runner`: 17 tests OK (parser of both lines, sum identities including the reviewed `shadow_mismatch`/`stale` buckets, malformed lines, 16-witness cap, launcher gate and `--dry-run` env).
- Clean CMake build (`--clean-first`) after the review fixes: zero warnings; `check_no_x87.py build/d3d9.dll`: 225 reachable functions, no violations. Strict `-Wall -Wextra -Werror` compile of `motion_output.cpp` (production and seam defines) and `capture.cpp`: clean.
- `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_motion_output.py seam-ownership-taa-camera-candidates-on` (fresh build; same numbers before and after the review fixes): exit 0, `checks=216` (39 from the counter validator), `motion_pixels=44279`; 12 `shadow_replay_candidates` lines (one per scene end, frames 0–11), every one `routed=2 zwrite=2 slice0=2 managed=2 leased=2 quiet=2` with all other buckets 0 (`shadow_mismatch=0 stale=0`; `routed=3`/`1` on the frames whose frame line reports 3/1), 0 witness lines, mode line `requested=1 enabled=1 motion_output=1 ownership=1`; predicates `lease_contract`, `single_thread`, `promotion_possible` true, `managed_boundary` false only because the fixture draws two triangles (`slice0_p50=2`). Reviewed build: `build/d3d9.dll` `1d375de2…`, seam `de5ec257…`, fixture `b2f8e8a5…`, trace `ef90b62d…`; record `verification/results/bottle-X3/motion-output-partial.json`.
- Review fixes applied (diagnostic path only): scene-end views must match the draw-time `allocation_id` and `BufferLockView::generation` (else `stale`, no comparison, no witness); the shadowed binding ids must equal the route key's before pool class and bookends are attributed (else `shadow_mismatch`, no record); a frame serial emits the frame line at most once per frame.
- `./x3run --motion-output --ownership --object-trace --object-lifetime --shadow-replay-candidates --dry-run`: env `X3M_SHADOW_REPLAY_CANDIDATES=1`, `X3M_OWNERSHIP=1`, `X3M_MOTION_OUTPUT=1`, `X3M_LINEAR_MATERIALS=0`, `X3M_TAA=0`, `X3M_SUN_SHADOW_LANE=0`; without `--ownership` the launcher refuses.
- Not exercised live: a Lock between draw and scene end (witness line, `serial_changed`), DYNAMIC/DEFAULT pools, the cutout exclusion and the 64-record overflow; the parser and identities cover their fields synthetically. The next gameplay run with the option answers the four §3 predicates through `tools/analysis/shadow_replay_candidates.py <session log>`.

## Run26 session B / snapshot run66 — refusal buckets on original+linear flight

`/tmp/x3-bottleX3-run66/` (55 files; `--linear-materials --linear-distance-fade
--sun-shadow-lane`, one minute near a station). 5,138 lane frames emitted,
0 available. Bucket totals: `state` 53,712 (rank 1, from three signatures: the
XT class-C "Standard"/"Standard+damage" station/ship material shaders
`f1b0e820c7b488c3`, `64bac8bb307eb896`, `e6794b6ec37ff71a`, registered pairs
failing the gate-4 cutout exact-state check), `unregistered` 16,287 and
`no_zwrite` 13,993 (particles/effects/GUI/stardust families, non-scene),
`fade_arm` 5,644, all other buckets 0. Eleven distinct writer signatures,
`overflow=0`. Verdict: the dominant blocker is a state-match gap on real scene
geometry (fixable in the registry/state gate); the effects population is a
structural exclusion that must not veto if it does not write depth. Next: make
the three XT signatures pass the state gate and confirm that non-depth-writing
draws never count as untracked writers.

## Run26 session B follow-up: XT state gate and non-depth writers (2026-09-16)

Worktree `worktree-agent-a8f56079c839edc2f`, bottle X3, WineArch arm64,
`FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`. No composition or shadow
application change (`shadows=` reported 0 then; since the apply wiring the field is 1 when `--sun-shadow-apply` is on).

**Failing term.** The run66 `sun_shadow_lane_writer` lines carry no state
fields (`reason=state gate=4 registered=1 z=1 zwrite=1 z_known=1` for all
three XT signatures), so the term was resolved from the gate chain and the
captured per-draw states in `verification/results/game-flight-capture-summary.json`:
`37c34a7478544c14/f1b0e820c7b488c3` (26 captured draws) is ZENABLE 1,
ZWRITEENABLE 1, ZFUNC LESSEQUAL, ALPHABLENDENABLE 0, SRGBWRITEENABLE 0,
**ALPHATESTENABLE 1, ALPHAREF 1, ALPHAFUNC GREATEREQUAL, COLORWRITEENABLE 7**.
The gate's `state` bucket is `srgb || !(opaque arm || cutout arm)`; the opaque
arm needs alpha test off and mask 15, the cutout arm needs `cutout::pair`
(only `53a0a641107ed76c/63f96eba9eea7880` and `4944d81dfe531b37/5e0a10fe752b6140`)
plus the exact cutout device state. The XT pairs are not cutout pairs, so a
draw with alpha test on (or with mask 7 and test off) fails the term
`(test == 1 && color == 7 && cutout_pair)`: the exact-arm pair membership, not
sRGB, not blending, not an unknown state. The proxy never sets
SRGBWRITEENABLE (reads only). The two captured `494fe349b8bc12ec/e6794b6ec37ff71a`
draws are opaque (test 0, mask 15); their run66 refusal is inferred to be the
same alpha-test/mask-7 state of the damage-decal pass (not captured with
state fields; the next lane run answers it directly).

**Depth semantics.** Alpha test discards a fragment before the depth write and
before every render-target write, so with the variant's `oC0.a` equal to the
original's (material_motion colour-channel identity; `test_linear_sun_share`
`oC0` identity) the fragments that write depth are exactly the fragments that
write RT2: alpha test is a legitimate receiver state for depth tracking.
COLORWRITEENABLE masks RT0 only (RT1/RT2 masks are set to 15 by the route).
Blending is not colour-only in D3D9 (it blends every bound target, including
the lane values) and stays refused as `blended`.

**Change (sun-lane classification only; lane off patches nothing new).**
Gate 4 gains the tested-opaque arm, active only when the sun lane is latched
on the frame (`sun_lane_active_`) with its linear-material prerequisite
(`linear_material_requested_`): registered pair that is not a cutout pair by
identity (`cutout::pair`, independent of the linear-material flag), z and z
write on, blend off, sRGB off, any nonzero RT0 mask, alpha test on or off
(`src/proxy/motion_output.cpp`). With `--sun-shadow-lane` off every
alpha-tested or partial-mask pair routes exactly as before this change (gate-4
refusal). The arm checks no device capability for alpha-tested draws: it relies
only on the documented D3D9 order (alpha test before depth and target writes),
the ordinary route's RT1/RT2 masks (15) and formats, and the variant's `oC0.a`
identity; it does not check or need MRT post-pixel-shader blending or
independent write masks because blending stays refused, and it does not check
ALPHAFUNC/ALPHAREF because any test gates depth and lane identically. The two
cutout pairs take the exact cutout arm (`cutout_draw_state()`) while that arm
is configured for the frame and stay refused outside its exact state; on a
frame whose exact arm is unconfigured (nonzero configured mip bias, Unsupported
or Retry verdict, HDR off) they enter the tested-opaque arm like any other
registered pair, drawn with their native LOD bias (run 28 session B below;
this superseded the original "never enter it" rule). `route.cutout` (and
`cutout_routed`) stays exact-arm-only; a new `route.alpha_tested` feeds the
replay-candidate W3 exclusion.
`SunShareFrame::draw` takes `depth_writer`: a colour writer after the first
receiver that wrote no depth is counted `non_writers`, reported as
`non_depth_writers=` on `sun_shadow_lane_frame`, and never vetoes. The writer
verdict is fail closed: any non-FALSE ZENABLE (`D3DZB_TRUE` or `D3DZB_USEW`)
with ZWRITEENABLE on is a depth writer, and an unreadable z state stays a
writer. Previously every such draw (run66 `no_zwrite` 13,993 and the
z-write-off part of `fade_arm` 5,644 and `unregistered` 16,287) vetoed. The
`state` bucket now means sRGB write on, or a cutout pair outside its exact
state on a frame whose exact cutout arm is configured (since run 28 session B;
before that fix it also held every cutout-pair draw of a frame whose exact arm
was unconfigured); `no_zwrite` is structurally empty. `analyze_sun_share_lane.py` requires
`non_depth_writers` for the new grammar and reports `grammar_old=true` (value
`null`) for a line without it instead of reading zero.

**Evidence.**
- `PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_sun_share_lane verification.analysis.test_linear_sun_share`: 19 tests OK; sun-share host `PASS checks=35` (was 30: non-writer counted not vetoing, non-writer alone keeps the frame available, unknown z stays a writer, nothing counted before the first receiver).
- Clean CMake build (`--clean-first`) in the worktree after the review fixes: zero warnings; `build/d3d9.dll` sha256 `c46cdcaf…8433c71`; `check_no_x87.py build/d3d9.dll`: 225 reachable functions, no violations; `build_motion_output.sh` (strict `-Wall -Wextra -Werror` seam compile): clean.
- `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_sun_share_live.py --fixture … --dll …`: all 15 cases pass, 27.8 s; `verification/results/bottle-X3/sun-share-live.json`. (The first post-review run failed only in the validator: with nothing routed the cut detector has no sample, so `fixture_cut=0` on every lane-off frame; fixed and rerun.)
  - `xt_state` (new): the receiver drawn on all six frames with alpha test on, ALPHAREF 1, GREATEREQUAL, mask 7 (`SUN_XT_STATE` ×6): every frame `available=1 receiver_draws=1 untracked_writers=0 non_depth_writers=0`, no refusal or writer line, interior depth 0.5 on 3600 pixels, `checks=43269 restorations=18`.
  - `effects` (new): an additive-blended, z-write-off unregistered draw over the receiver on frame 2 (`SUN_EFFECTS frame=2 depth_write=0 blend=1`, owning colour really changed, lane bytes intact): frame 2 `available=1 untracked_writers=0 non_depth_writers=1`, no refusal line, `checks=43272 restorations=19`.
  - `untracked` (expectation corrected by construction): the unreviewed PS alteration is now drawn with z write **on** (an actual depth writer at the receiver's depth); frame 2 `available=0 untracked_writers=1`, bucket `pair=1`, writer `reason=pair gate=3 registered=1 z=1 zwrite=1 z_known=1`. With z write off it would no longer veto, which is the intended behaviour, not a witness of the veto.
  - `xt_state_lane_off` (new): the same XT-state draws with `X3M_SUN_SHADOW_LANE=0`: no `sun_shadow_lane_*` line at all, every frame `motion_output_frame routed=0 gate4=1` and the fixture's per-draw counters (`SUN_XT_STATE lane=0 routed=0 gate4=1` ×6, status codes 89/99), the pre-change routing; TAA output byte-equal to the reference on 6 frames; `checks=63 restorations=18`. `xt_state` with the lane on reports `routed=1 gate4=0` on the same frames (`checks=43275 restorations=18`).
  - `cutout_pair` (new): the cutout pair `53a0a641107ed76c/63f96eba9eea7880` drawn after the receiver on frame 2 with mask 7 and alpha test off (a state only the tested-opaque arm would admit), lane on: gate-4 refusal (`SUN_CUTOUT_PAIR gate4=1`), frame 2 `available=0 receiver_draws=1 untracked_writers=1 non_depth_writers=0`, bucket `state=1`, writer `reason=state gate=4 registered=1 z=1 zwrite=1 z_known=1`; `checks=43273 restorations=19`.
  - `composition_missing` / `composition_failed`: the frame-2 emitter (blend on, z write off, coverage failed) is now `non_depth_writers=1` instead of `unregistered=1`; the frame stays unavailable through `owner=0` (`exclusion_required=1 exclusion_valid=0` for missing). Validator and synthetic witness updated accordingly. All other cases `non_depth_writers=0`.
- Not exercised live: `D3DZB_USEW` (w-buffer support is not assumed under the fixture backend; the rule is the `z != 0` mapping in `evaluate_draw`), an XT pair itself (the fixture's authored pair stands in for the state; the XT programs are game bytes), and the run66 state of `e6794b6ec37ff71a` (inferred above). The next `--sun-shadow-lane` gameplay run shows `state` falling to the cutout-pair residue and `non_depth_writers` absorbing the effects population.

## Cascade-0 depth replay (`--shadow-replay-depth`, no consumer)

Implementation of [shadow-replay-gates.md](../architecture/shadow-replay-gates.md)
"Implemented: cascade-0 depth replay fixture". Evidence (2026-09-16, worktree
`worktree-agent-a10b481cabb8fdfd3`, bottle X3, WineArch arm64, `FEX_X87REDUCEDPRECISION=1`,
`WINEMSYNC=1`):

- `PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_shadow_replay_depth verification.analysis.test_shadow_replay_candidates verification.analysis.test_motion_output_runner`: 22 tests OK (per-frame line parser and identities, the projection chain and rasterizer, the launcher gate: `--shadow-replay-depth` refused without `--motion-output --ownership`, `--shadow-replay-size` bounds and its dependency, env `X3M_SHADOW_REPLAY_DEPTH/SIZE/CANDIDATES`).
- Clean CMake build (`--clean-first`): zero warnings; `check_no_x87.py build/d3d9.dll`: 225 reachable functions, no violations. Strict `-Wall -Wextra -Werror` compiles of `shadow_replay_pass.cpp`, `motion_output.cpp` (production and seam defines), `capture.cpp` (seam) and `loader.cpp`: clean.
- `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_motion_output.py --dll build/d3d9.dll --seam verification/probe/build/motion-output-seam/d3d9.dll --fixture verification/probe/build/motion_output_fixture.exe seam-ownership-shadow-replay-on seam-ownership-shadow-replay-off seam-ownership-taa-shadow-replay-on seam-ownership-taa-shadow-replay-off seam-ownership-shadow-replay-casters-2 seam-ownership-shadow-replay-casters-8 seam-ownership-shadow-replay-casters-20`: exit 0, checks 144/98/167/121/144/144/144. Map versus the CPU projection: 0 coverage disagreements, max depth error 1.2e-05 (256², 8-unit cascade) and ≤ 3.9e-06 (1024²) against the 1e-4 gate, 3,136–81,656 covered texels per case; lease frame `replayed=0 skipped_lease=1`, no-sun frame `skipped_state=draws` (`no_sun`), two-stream frame `skipped_state=1` (`multistream`), the map unchanged on all three; Reset: `allocations=2` at frame 4, replay resumes. Transaction `us` medians 40.8 (2 draws, 256²), 40.7 / 50.4 / 65.8 (2 / 8 / 20 draws, 1024²): ≈ 35 µs fixed plus ≈ 1.4 µs per draw. Presented frames byte-identical to the option-off twins with TAA off and on (32,768 pixels each, colour hashes equal). Record `verification/results/bottle-X3/motion-output-partial.json` (production `131c261e8703…`, seam `a77045059ba6…`, fixture `8087cb8db637…`).
- Sun direction: PS register c4 (`LightDir_Dir0` of the hull programs), decided in the note; the light record is not read.
- Not exercised: the depth-only fallback formats, the record storage limit (64 then, 512 since the casters-by-bounds change, whose fixture exercises the per-frame cap), a production-extent map in the game, native Windows.

### Casters by bounds (2026-09-17, worktree `agent-a7e2c2470cc482842`)

Run 36 B (`/tmp/x3-bottleX3-run106`) hovered over a station deck and replayed only the
own ship (`shadow_replay_candidates … routed=245 zwrite=245 slice0=6 managed=6`,
`shadow_replay_depth replayed=8 draws=8` every frame): slice 0 was the object's origin
distance and the station origin was 2.5 km away. Cascade-0 casters are now the z-writing
managed draws whose vertex extent meets the map box (`shadow-replay-gates.md`, "Casters
by bounds": the extent is the object-space AABB of the draw's vertex range, read once per
buffer range at a scene end through the wrapper's READONLY Lock behind a bookend-view
guard with bounded retry, and cached; a draw without an extent falls back to the origin
rule for that frame and queues the read; the per-frame replay budget is
`X3M_SHADOW_REPLAY_CAP`, 1..512, default 512, storage 512, dropping the last submitted).
The counter line gains `bounds= origin= fallback= capped= reads=`.

Fixture: the replay script draws L first (origin 256 units to the camera's right, beyond
the origin rule's 250; vertices across the 8-unit box; beyond the far plane on screen),
then the casters, then F (origin 300 units, vertices 225 units away; behind the camera);
the runner sets the cap to `casters`, so frame 0 (no extents) replays the casters by the
origin rule and later frames L and the casters but the last submitted one (capped), and
asserts the counter and depth lines per frame (`origin < bounds` on frames 1–7) and the
map against the oracle of the replayed set. The lease-proof Lock moved to caster 0 (the
capped caster is never leased). Bottle X3, WineArch arm64, `FEX_X87REDUCEDPRECISION=1`,
`WINEMSYNC=1`, after the Fable review (bookend guard, L beyond 250, cap drop order):

- `PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_shadow_replay_candidates verification.analysis.test_shadow_replay_depth`: 12 tests OK
  (with `test_motion_output_runner test_sun_share_lane`: 35 OK).
- `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_motion_output.py seam-ownership-shadow-replay-on seam-ownership-taa-shadow-replay-on sun-shadow-apply`
  (the runner's own fresh build: `verification/results/bottle-X3/motion-output-build.log`, clean
  CMake build with zero compiler warnings, seam DLL and fixture; `build/d3d9.dll` sha256
  `e32595f607f13c65…`; `check_no_x87.py`: 504 reachable functions, 0 violations, after the
  draw-time basis made `shadow_replay_basis`/`shadow_replay_view_rows` reachable and their
  `std::fabs`/`std::sqrt`/`std::floor` were replaced by comparisons, `sqrtsd` and a
  `cvttsd2si` floor): exit 0 on all three (168 / 191 / 156 checks). Counter, both replay
  cases, every frame `routed=4 zwrite=4 origin=2`; frame 0
  `bounds=0 fallback=2 slice0=2 managed=2 leased=2 capped=0 reads=4` (four extents read at
  the scene end); frames 1–7 `bounds=3 fallback=0 slice0=3 managed=3 leased=2 capped=1 reads=0`
  (L admitted by bounds although its origin is beyond the rule, F excluded, the last caster
  capped; the cache survives the frame-4 Reset). Depth line `draws=2` on every frame,
  `replayed=2` on the five replayed frames, the three refusals unchanged (`lease/bookends`
  on caster 0, `state/no_sun`, `state/multistream`). Map: 40,681 covered texels over the run
  per case (L included from frame 1), max depth error 1.17e-5 against the oracle; TAA twin
  identical. Per frame: median 58.7 µs (min 34.7, first frame with target creation 1438.6)
  without TAA, 45.2 µs (31.0 / 1331.9) with TAA, for 2 draws at 256²: the transaction's
  fixed cost dominates in the fixture; the ledger's live figure stays ~1.3 µs/draw (run81:
  42.3 µs median for 6 draws at 1024²). `sun-shadow-apply`: worst codes 0.998, edge
  mismatches 376 (345 within one texel, 31 within two, 0 beyond), quad median 3419.6 µs.
- `… run_sun_share_live.py --fixture … --dll … --case shadow_apply`: passed (shadowed
  pixels min 3969, apply max 1700.3 µs).
- Not exercised: the game (first frames of a new mesh set run on the origin rule while
  the extents are read, at most 32 reads / 1 MiB per scene end; a station under the ship
  is expected to raise `draws` to hundreds against the 512 cap), the retry path (a buffer
  found locked at the scene end, a failed Lock), `X3M_ADMISSION=1` with the scene-end
  reads, an object whose origin the rule admits but whose geometry misses the box (no
  fixture object; F lies outside both rules), the
  `seam-ownership-taa-camera-candidates-on` and `casters-N` cases (updated, not rerun),
  native Windows.

## Run 28 session B (run81): the cutout pairs veto every frame under a nonzero mip bias (2026-09-16)

Worktree `agent-a5d92fbad60a80421`, bottle X3, WineArch arm64,
`FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`. Log `/tmp/x3-bottleX3-run81/session-20260916-023735-216.log`
(2123 frames, DLL 2b0969e5 from a26eb9b, `--linear-materials --linear-distance-fade --sun-shadow-lane`,
`proxy_options … X3M_TAA_MIP_BIAS=-0.5`).

**Finding.** `sun_shadow_lane_writer` holds exactly two signatures for the whole
session (`signatures=2` on every refusal line, 4246 = 2 × 2123): the two cutout
pairs `4944d81dfe531b37/5e0a10fe752b6140` and `53a0a641107ed76c/63f96eba9eea7880`,
both `reason=state gate=4 registered=1 z=1 zwrite=1 z_known=1`. `untracked` equals
`state` on every frame because those are the only untracked writers (2–33 per
frame); nothing is counted twice. `linear_material_frame` sums to
`cutout_routed=0 cutout_missed=0` over the run, whereas run66 (same feature set,
mip bias 0) routed 2509 cutout draws and refused no cutout pair. Cause: run 28
was launched with `--taa-mip-bias -0.5`; `cutout_arm_configured()` requires a
zero configured mip bias (alpha-tested-materials.md, "explicit initial refusal,
pending its own native-coverage qualification"), so the exact cutout arm was
unconfigured on every frame, and the 444478a tested-opaque arm excluded the
cutout pairs by identity. Every cutout-pair draw (z write on, blend off, alpha
test on) therefore failed gate 4 as `state` and vetoed the lane as an untracked
depth writer. The captured per-draw states of those pairs are not in the run81
log (inference from the run66 comparison and the fixture reproduction below);
the writer line now carries them.

**Change (lane only).** The tested-opaque arm admits a cutout pair when the
exact cutout arm is not configured for the frame (`cutout_arm_active_` false:
nonzero mip bias, Unsupported/Retry verdict, HDR off), in any state that passes
the rest of gate 4 (z and z write on, blend and sRGB off, nonzero RT0 mask,
alpha test on or off): fixed-function fog, stencil, cull mode, fill mode,
ALPHAFUNC/ALPHAREF and a mask other than 7 are not conditions, because alpha
test rejects a fragment before the depth write and before every target write,
so the fragments that write depth are exactly the fragments that write the lane
whatever the test function, reference or the other pipeline states; none of
those states changes which fragments reach the depth and lane writes. With the
arm configured the pairs keep the exact arm or their `state` refusal outside
its exact state (the `cutout_pair` fixture case is unchanged). `route.cutout`
(`cutout_routed`, fixture faults) is exact-arm-only, and
`mark_cutout_candidate`/`cutout::missed` key on the same latch, so no
composition exclusion changes. With the lane off nothing changes.

Native coverage under bias: a routed draw normally applies the route's
`D3DSAMP_MIPMAPLODBIAS` to its mip-chain stages, which would sample the cutout
alpha at a different level than the native draw and move the alpha-tested
coverage (the reason the exact arm refuses a nonzero bias). An alpha-tested
cutout pair on the tested-opaque arm (`route.native_mip_bias`) therefore
restores instead of applies: any stage still holding the route's bias from an
earlier routed draw gets its native value back before the draw
(`restore_mip_bias`, one `SetSamplerState` per biased stage, nothing when no
stage is biased; the next ordinary routed draw re-applies the bias as before).
Per-draw cost of the skip: one bool test on the route; the restore itself is the
existing restore-point path.

`sun_shadow_lane_writer` gains `test= mask= srgb=` (the values gate 4 actually
read for that draw, -1 when the chain did not read them, packed in
`route.sun_draw_state`), `cutout_pair=` and `arm=` at the first sighting of a
signature (not part of the key). Cost: the gate term replaces
`cutout::pair(vs, ps)` by the shadowed `shadow_.cutout_pair && cutout_arm_active_`
(two bools); the read lambda records which of the three states it read (three
integer compares per read, four reads per draw); the writer fields cost only on
a new signature (at most 64 per device).

**Depth replay in the same run.** `shadow_replay_depth` over the 2123 frames: median
42.3 µs, p95 49.0 µs, `skipped_lease=0 skipped_state=0 skipped_caps=0` (no candidate was
skipped for those reasons). `shadow_replay_candidates` is stable across sampled frames at
`routed=204 zwrite=203 slice0=6 managed=6 leased=6 quiet=6`: 6 candidates pass the
replay-candidate predicate on live geometry, against the fixture's 2.

**Evidence.**
- New live case `cutout_pair_bias` (`X3M_TAA_MIP_BIAS=-0.5`, cutout pair drawn on
  frame 2 after the receiver with alpha test on, ALPHAREF 1, GREATEREQUAL, mask 7,
  z write on; the full-mip-chain ramp bound on the otherwise unused stage 6 so the
  route's bias is observable). Old seam (HEAD 8954cff, same fixture): `SUN_CUTOUT_BIAS
  frame=2 … routed=0 gate4=1 untracked=1`, writer `reason=state gate=4 … z=1 zwrite=1`
  on `63f96eba9eea7880`, fixture exit 1 (the run81 pattern). New seam: the routed
  receiver leaves `bf000000` (-0.5) on stage 6, the admitted cutout draw reads
  `stage_bias=00000000` afterwards (native), `routed=1 gate4=0 untracked=0`, frame 2
  `available=1 receiver_draws=2 untracked_writers=0`, `cutout_routed=0`, interior
  depth 0.5 on 3600 pixels, `checks=46876 restorations=17`.
  The existing `cutout_pair` case (arm configured, test off, mask 7) still refuses:
  writer line `test=0 mask=7 srgb=0 cutout_pair=1 arm=1`; the `untracked` case's
  gate-3 writer reports `test=-1 mask=-1 srgb=-1 cutout_pair=0 arm=1`; the runner
  asserts these fields on every writer line.
- `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_sun_share_live.py --fixture verification/probe/build/motion_output_fixture.exe --dll verification/probe/build/motion-output-seam/d3d9.dll`:
  all 16 cases pass (96 byte-exact TAA frames, 60 history frames, 16 Resets);
  `verification/results/bottle-X3/sun-share-live.json`.
- `… run_linear_material.py --exe verification/probe/build/linear_material_fixture.exe --sun-share`: 216 cases, 55,296 valid pixels (27,648 positive, 27,648 zero), 256 clear controls, max subtraction error 0.000686797113 (unchanged); `verification/results/bottle-X3/sun-share-material-gpu.json`.
- `PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_sun_share_lane verification.analysis.test_linear_sun_share`: 19 tests OK (host `PASS checks=35`, synthetic witness with the run81 twin refused); with the review fixes the seven modules run together: 45 tests OK.
  `… test_linear_material_live test_motion_wrap_states test_capture_bloom_lifetime test_motion_hdr_scene test_linear_cutout_contract`: 26 tests OK (HDR-scene mock mirrors the three render-state enums and an inert `shadow_state_field`).
- Clean CMake build (`--clean-first`, mingw-i686, RelWithDebInfo): zero warnings, `build/d3d9.dll` sha256 `c33f3d8e6623fa4c…`; `check_no_x87.py`: 230 reachable functions, no violations; strict seam compile clean.
- Not exercised: the game's actual cutout-pair states in run81 (the new writer fields answer this on the next `--sun-shadow-lane` run), a cutout pair routed with an actually biased mip-chain stage (fixture textures are single-level), native Windows.

### Run 30 session B (run88), 2026-09-17

DLL `bbadc568…` (`f94290c`), `--linear-materials --linear-distance-fade --sun-shadow-lane`, mip bias -0.5:
`sun_shadow_lane_frame available=1` on all 14,924 frames; `cutout_routed`/`cutout_missed`/`cutout_unavailable` 0 (`cutout_caps` 254 windows), no `sun_shadow_lane_writer` line, `non_depth_writers` 3–203. The station drew no alpha-tested cutout material, so the `e62722a` admission is still unexercised; the cutout pairs belong to Argon station bodies (tech, liquid, farm, solar/mine, trading station; [where cutouts appear](../reverse-engineering/effect-shader-users.md)). Next: one session B at an Argon industrial station.

### Run 31 session B (run90), 2026-09-17

DLL `a9ebfa3b…` (`4adf3dd`, `session-20260916-075050-212.log`, 115,199 lines), options confirm `X3M_LINEAR_MATERIALS=1 X3M_LINEAR_DISTANCE_FADE=1 X3M_SUN_SHADOW_LANE=1 X3M_FRAME_PHASES=1` (log:2-3). `sun_shadow_lane_frame available=1` on all 16,041 sampled frames (log: e.g. 3104-3180), matching run88's full-availability shape; `non_depth_writers` ranges 3-64 (peak at frame=4044, log:~28k). Depth replay `shadow_replay_depth`: 16,041 lines, median 40.7 µs, `skipped_lease`/`skipped_state`/`skipped_caps` all 0 in every sampled frame (e.g. log:"frame=340 ... us=573.7", "frame=341 ... us=57.6").

Cutout draws: zero. All 275 `linear_material_frame` lines show `cutout_routed=0 cutout_missed=0 cutout_unavailable=0` (e.g. log:144,1350,112787-115044), `cutout_caps=1` throughout (capability present, arm never exercised). The four cutout program hashes (`4944d81dfe531b37` vs, `5e0a10fe752b6140` ps, `53a0a641107ed76c` vs, `63f96eba9eea7880` ps) appear only as `shader kind=... dumped=1` compile records near session start (log:1030-1056, before frame=60), never in a routed count — the game loaded the cutout programs but no draw ever went through the tested-opaque (`e62722a`) admission arm. The telemetry has no per-draw shader-pair counter, so which non-cutout pairs the station itself used cannot be read from this log; that is a genuine evidence gap, not a zero result.

Timing: busiest window in this session is frame=900 (`frame_timing`/`frame_phases` log:8257,8262) — `dt_p50_us=32780` (32.8 ms), `draws_p50=727`, `views_p50_us=27034` (27.0 ms) — below session A run89's busy window (37.5 ms / 987 draws / 32.5 ms views). The high-`non_depth_writers` frame (4044, peak 64) falls in the frame=4200 aggregation window (log:30921,30926), which is comparatively light: `dt_p50_us=16318`, `draws_p50=361`, `views_p50_us=13198`. The station encounter increased shadow-lane writer accounting but not overall frame cost.

No abnormal conditions: 0 hits for crash/panic/fatal/shader_unknown/claim_fail/truncat/restore_refus; the 113 case-insensitive "error" hits are all `errors=0`/`error=203`-as-success-code fields inside `dat_handle_pool_metric`, `hdr_device`, `motion_output_device` self-test lines and one `telemetry_span`, none indicating failure.

That gap is now closed by the frame-timing window's `draw_pairs … cutout_pairs=` line (`--frame-timing`, schema in [sampling-profiler.md](sampling-profiler.md)): it names the eight most-drawn `(vs/ps)` pairs of each 300-frame window and reports the hull and station cutout pairs explicitly even at zero, so the next session B reads from it which programs the station actually drew. The two cutout counts do not come from that eight-entry table but from their own counters, incremented on every draw, so a reported zero means the pair never drew rather than that it lost a table slot.

Open: still no session with an actual cutout draw; the station type visited in run90 (identity not recorded in telemetry) did not produce alpha-tested cutout geometry either. A next run needs per-draw shader-pair (vs/ps hash) telemetry to identify what the station actually drew, or a target confirmed (by the user, since launch is out of scope here) to carry the Argon cutout materials.



## Cutout pairs under the tested-opaque arm: code path

Run93 (`--linear-materials --linear-distance-fade --sun-shadow-lane`, default
mip bias -0.5) drew the two cutout pairs 292,413 and 268,716 times while every
`linear_material_frame` line reported `cutout_routed=0 cutout_missed=0
cutout_unavailable=0 cutout_caps=1`: with a nonzero configured bias
`cutout_arm_configured()` is false ([motion_output.cpp:940](../../src/proxy/motion_output.cpp)),
so `cutout_arm_active_` (latched per frame, motion_output.cpp:921) is off and
`route.cutout` — the only input of `counters_.cutout_routed` — is off with it
(motion_output.cpp:4542, 5140). Those draws were neither refused nor invisible;
they took the tested-opaque arm and were counted as ordinary `routed`. Reading
the path for a cutout-pair draw under that default configuration:

- **(a) Routed through the material route: yes.** Gate 4's third admission
  clause, `sun_lane_active_ && linear_material_requested_ &&
  !(shadow_.cutout_pair && cutout_arm_active_) && test <= 1 && color != 0`
  (motion_output.cpp:4504), admits the pair in its exact cutout state
  (alpha test on, mask 7) because the exact arm is inactive. Nothing downstream
  distinguishes it: `route.alpha_tested` is set (4536), `route.cutout` stays
  false (4542), and the draw then takes the ordinary linear-material apply
  (`linear_material_refusal()` == 0 → `bind_variant_pair(route, true)`,
  4615-4654), counting into `routed`, `material_routed` and `depth_routed`.
- **(b) Lane share written: yes, when the pair's profile writes depth.**
  `bind_variant_pair` replaces the PS with `shadow_.ps_sun_material` whenever
  `sun_lane_active_ && route.depth && !route.fade_arm` (motion_output.cpp:4214-4217),
  so oC2 carries this draw's depth and share; `route.sun_receiver` additionally
  requires the material variant and `shadow_.ps_sun_extraction` (4216) and is
  the flag the lane bookkeeping counts as a receiver (5131). A pair whose
  profile writes no depth routes without the lane PS (reason `no_depth`).
- **(c) Native MIPMAPLODBIAS: kept.** `route.native_mip_bias = route.alpha_tested
  && shadow_.cutout_pair` (motion_output.cpp:4541) makes the apply restore any
  stage still holding the route's bias instead of applying it (4655), so the
  alpha source of an alpha-tested cutout draw is the native draw's.
- **(d) Refusal paths.** Gate 1 `feature`, gate 2 `scene`, gate 3
  `unregistered`/`pair`, gate 4 `no_zwrite` (z test or z write off, the first
  check of the chain), `blended`, `state` (sRGB write on, or mask 0), `rows`,
  `geometry`, `read_failed` (motion_output.cpp:4500-4534), and after admission
  `apply_failed` (rollback, 4635-4642). `scope` and `history` are recorded but
  still route. Refusals send the draw down the native path with `route.routed`
  false.

### Per-frame telemetry (`linear_material_frame`)

Next to `cutout_routed` / `cutout_missed` / `cutout_unavailable` / `cutout_caps`
the line now carries the tested-opaque arm's own counts, on frames where the
exact arm is inactive (`shadow_.cutout_pair && !cutout_arm_active_`, the single
branch in `after_draw`; `shadow_.cutout_pair` is false whenever linear materials
are off, so the cost off is that one test):

- `cutout_opaque_routed` — cutout-pair draws routed through the tested-opaque
  arm with a successful native call.
- `cutout_opaque_lane` — of those, the ones that bound the lane variant with the
  share extraction (`route.sun_receiver`), i.e. whose lane share (oC2.g) was
  written.
- `cutout_opaque_refused` and `cutout_opaque_refused_top=<reason>:<n>,…` — the
  refused ones and their top three buckets, named by the existing
  `SunUntrackedReason` table (`src/renderer/sun_share_frame.h`); a refusal
  without a recorded reason (the lane only fills it for a colour writer) falls
  back to its gate's bucket.

Counting is `note_cutout_opaque` (motion_output.cpp:5095): increments only, no
allocation, no device call, no extra state read per draw.

### Fixture evidence (2026-09-16, worktree)

- `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3
  verification/probe/run_sun_share_live.py --fixture
  verification/probe/build/motion_output_fixture.exe --dll
  verification/probe/build/motion-output-seam/d3d9.dll`: all 16 cases pass
  (`verification/results/bottle-X3/sun-share-live.json`, untracked).
  `cutout_pair_bias` frame 2 prints `SUN_CUTOUT_OPAQUE frame=2 routed=1 lane=1
  refused=1 no_zwrite=1 state=0 untracked=0` and its session log's
  `linear_material_frame` reads `cutout_routed=0 … cutout_opaque_routed=1
  cutout_opaque_refused=1 cutout_opaque_lane=1
  cutout_opaque_refused_top=no_zwrite:1`; `RESULT PASS checks=46879
  restorations=18`. The refusal probe is the same cutout pair drawn with z write
  off and a ZERO/ONE blend (colour and depth untouched): it is a non-depth
  colour writer, so frame 2 also reports `non_depth_writers=1` and stays
  available.
- `PYTHONPATH=verification/probe python3 -m unittest
  verification.analysis.test_sun_share_lane
  verification.analysis.test_linear_cutout_contract
  verification.analysis.test_linear_material_live
  verification.analysis.test_motion_wrap_states
  verification.analysis.test_capture_bloom_lifetime
  verification.analysis.test_motion_hdr_scene verification.analysis.test_frame_timing`:
  42 tests OK; `linear_cutout_contract scenarios=41 checks=284 failures=0`
  (unchanged scenario count: the host mocks only gained the new members
  inertly; `note_cutout_opaque` is exercised by the Wine fixture frame above,
  not by a host scenario). Reading caveats: a routed cutout-pair draw whose
  native draw fails counts in neither bucket, so routed + refused can be
  below the pair draw count; with linear materials on but the lane off, an
  inactive-arm frame still accumulates `cutout_opaque_refused` with reason
  `state`; and every non-colour-writer gate-4 refusal collapses to `state`
  through the gate fallback, so `no_zwrite`/`blended`/`geometry` appear only
  for colour writers.
- mingw-i686 RelWithDebInfo build: zero warnings;
  `python3 verification/probe/check_no_x87.py build/d3d9.dll`: 490 reachable
  functions, no violations.

Not exercised: a real game cutout draw through this arm (the counters are what
the next session B reads), and native Windows.

### Run 32 session B (run93), 2026-09-17

`/tmp/x3-bottleX3-run93/session-20260916-165735-212.log` (206,769 lines). DLL
`11c1f119…` (`baee232`), session A1 flags plus `--linear-materials
--linear-distance-fade --sun-shadow-lane` (log:5,124,132-133). User navigated
several sectors instead of holding the busy view.

**Cutout draws.** All 95 `draw_pairs` windows (frame=300..28500) nonzero
`cutout_pairs=<hull>,<station>` (log:2302 first); session totals hull=292,413,
station=268,716. **cutout_routed=0 across all 479 `linear_material_frame`
lines** (cutout_missed/unavailable also 0, cutout_caps=479) — capability
present, tested-opaque arm not exercised, same shape as run66/run88/run81.

**Lane availability.** 28,207 `sun_shadow_lane_frame` lines: available=1 on
25,523, available=0 on 2,684 (frames 24316-28613, log:166363 first), all
`failed=1 owner=1 exclusion_valid=0`, one `sun_shadow_lane_writer
reason=unregistered` (log:166363, frame=24316, gate=3); `non_depth_writers`
peaks 220. `shadow_replay_depth`: 28,207 samples, median 39.9 µs, all
`skipped_*` 0.

No `truncated=1`/`shader_unknown`/`motion_state_lost`/`restore_failures`/
`apply_failures`/`mip_bias_failures`; `incomplete=4` total. Open: which draw
introduced the unregistered writer at frame 24316 is not attributable from
this telemetry.

### Run 33 session C (run97), 2026-09-17

Fixture `/tmp/x3-bottleX3-run97` (58 files), session log
`session-20260916-215146-216.log` (41206 lines), candidate 03c0c9f4 from
a3cafd5. Options confirmed at line 129/137-139: `state_hooks installed=0`,
`sun_shadow_lane_device requested=1 qualified=1 reason=ok`,
`linear_material_frame` present (linear materials on), `mip_bias=-0.5`.

Busy Argon window identified as frames 4380-5580 (log lines 29003-38331) by
`cutout_opaque_routed`/`cutout_opaque_lane` activity, matching run91's ~108
draws/frame (max observed 112 at frame=4860). Over this window (21 samples):
`routed` p50=880 (range 364-1009); `cutout_opaque_routed` p50=90 (range
22-112); `cutout_opaque_lane` equals `cutout_opaque_routed` on every sampled
frame (ratio lane/routed = 1.0, not "routed minus no-depth pairs" as
expected); `cutout_opaque_refused` p50=8 (range 5-8), refused reason is
`no_zwrite` in every case (histogram: no_zwrite=8 frames at 8, one at 7×5,
one at 5, one at 8 — all `no_zwrite`, no other reason seen). Outside this
window `cutout_opaque_*` are 0 or small (2-4), with an early transient
`cutout_opaque_refused_top=scene:4` at frames 420-600 (before the lane
warms up), distinct from the steady-state `no_zwrite` reason.

Lane (`sun_shadow_lane_frame`, 4962 samples, log line 4031 onward):
`available=1` on all 4962 sampled frames (100%); `non_depth_writers` range
3-58, median 40; `untracked_writers`=0 throughout; `failed`=0 throughout.
No `reason=` refusal field appears on `sun_shadow_lane_frame` lines in this
session (only one `reason=ok` line total, on `sun_shadow_lane_device` at
line 138); the run93 `unregistered` writer string does not occur anywhere
in this log (grep count 0). `shadow_replay_depth` (4962 samples): median
38.0 µs, range 0.0-561.1 µs; `skipped_lease`/`skipped_state`/`skipped_caps`
all sum to 0.

No abnormal markers: `claim_fail`, `truncated`, `shader_unknown`,
`chase_refus`, `motion_state_lost` all 0 occurrences; `mip_bias_failures=0`
throughout; `fade_refused`/`fade_held` present but 0 on all 97 frames.
`dropped=1` (pass_phases, frame=300) and `incomplete=1/3` (frame_phases,
frames 300/900/5700) are startup/tail transients, outside the busy window
(4500-5400 all show `dropped=0 incomplete=0`).

## Sun-shadow apply pass (2026-09-17, worktree)

Standalone implementation of [legacy-sun-application.md](../architecture/legacy-sun-application.md)
§2 (`src/renderer/sun_shadow_apply_pass.{h,cpp}`, `src/temporal/sun_shadow_apply_ps.hlsl` →
`sun_shadow_apply_program_inc.h`, 220 conservative ps_3_0 slots) and its §3.3 fixture
(`motion_output_fixture.cpp` mode `sunapply`, CPU twin `verification/probe/sun_shadow_apply.py`,
host test `verification/analysis/test_sun_shadow_apply.py`). No proxy wiring; `ShadowReplayPass`
gained `map_texture()`, `view_rows()`/`set_view_rows()` and `shadow_replay_view_rows` builds the
frame's view → sun rows. The assigned worktree was removed mid-task, so the work sits uncommitted
in the main checkout beside another agent's uncommitted `linear_material` changes, which the
runner's clean build therefore included. Review fixes folded in before the wiring hand-off: RT0 is
bound before its viewport (a viewport must fit the bound target on native D3D9), the share is
saturated before use, the vertex declaration/FVF is re-set after the block `Apply` (a D3DSBT_ALL
block does not restore a null declaration), and the non-planar bias fallback pulls towards the
light.

Case `sun-shadow-apply` (`run_motion_output.py`; production DLL passive, the pass linked into the
fixture): a 128² `A16B16G16R16F` target of 64 colour tiles, synthetic `G32R32F` RT2 (box on a
plane, jittered projection, share pattern with share-free columns and sentinel sky) and a 256²
`R32F` map ray-cast through the `shadow_replay_basis` cascade (half-extent 5, depth half-range 8).
Six frames, odd frames with `caller_scene_open=false` (the pass opens its own scene):

| frame | map | elevation | jitter | exponent | compared | ambiguous | worst (FP16 codes) | shadowed / penumbra | hard-shadow edge mismatches (≤1 / ≤2 / >2 texels) |
|---|---|---|---|---|---|---|---|---|---|
| 0 | far | 50° | 0 | 1 | 8,809 | 212 | 0 (byte-identical) | 0 | – |
| 1 | depth 0 | 50° | 1 | 1 | 8,847 | 204 | 0.994 | 8,847 (f = 0 everywhere) | – |
| 2 | scene | 30° | 2 | 1 | 8,990 | 245 | 0.998 | 3,761 / 255 | 120 / 17 / 0 |
| 3 | scene | 50° | 3 | 1 | 8,756 | 218 | 0.997 | 2,266 / 258 | 109 / 8 / 0 |
| 4 | scene | 70° | 5 | 1/2.2 | 8,757 | 218 | 0.998 | 1,632 / 240 | 58 / 3 / 0 |
| 5 | = 4 after Reset | | | | byte-identical to frame 4 | | | | |

Every compared pixel is within one FP16 code of `C·(1−(1−f)·s)^e` with `f` from the CPU twin on
the same RT2 and map (0 violations in 52,916 pixel comparisons); "ambiguous" pixels (2.3–2.7 %:
a tap or the receiver within 2e-3 texel of a boundary, a compare within 1e-4 of equality, a quad
straddling a sentinel or near the planar threshold, or fine/coarse derivatives disagreeing) are
excluded from the strict compare. Sentinel and share-free pixels, factor-1 pixels and alpha are
byte-identical in every frame. Hard-shadow disagreements against the analytic box shadow are all
within two map texels of an edge (the rotated 3×3 kernel's reach); 345 of 376 within one.
Checks 156 = 126 fixture checks (`RESULT PASS checks=126`) + 30 validator checks (device line,
frame sets, timing, Reset identity, far identity, zero-map law, and per frame sizes, twin,
ambiguity bound, scene predicates). Fixture evidence per frame: skip paths `input` (missing map),
`format` (R32F RT2, A8R8G8B8 target, width and height mismatch), `params`, recording caller and
`device` (an RT2 of a second device) touch nothing (target byte-identical, state snapshot equal);
`reset_pending` refused between `before_reset` and `after_reset`; hostile caller state (blend
factors and op, Z, textures 0–1, scissor, viewport, pixel shader) restored with the snapshot now
covering `SRCBLEND`/`DESTBLEND`/`BLENDOP` and the other normalized render states, textures and
samplers 0–15, the vertex texture samplers and stage 0's coordinate states; `references()` 3
across Reset, 0 after detach. `ShadowReplayPass` accessors: rows round-trip detached and
attached, `before_reset` invalidates the rows and drops the map, `prepare()` publishes the
256² map texture in the attached format.

Bias: constant 0.003 and clamp/fallback 0.01 in normalized sun depth (fixture values, caller-tuned).
The receiver-plane fit is dropped in a 2×2 quad whose view depth steps by more than 5 % (the
silhouette column) and the clamp value then pulls the receiver towards the light; without that
fallback frame 2 showed a one-pixel acne column beside the box at 30°.

Timing (`SUNAPPLY_TIME`, CPU-inclusive, EVENT-synchronized, 128² through the proxy device, mostly
fixed per-call cost): first execute 174.7 ms (block creation and backend program compile), frames
1–4 1.54–3.68 ms, the first frame after the Reset 5.41 ms. The game-size GPU number is the run's
`frame_end` delta once wired. Companion cases on the same build: `production-on` 43 and
`seam-ownership-shadow-replay-on` 144 checks, PASS. Host tests `test_sun_shadow_apply` (6) and
`test_shadow_replay_depth` OK. Compact record (the runner's own output)
`verification/results/bottle-X3/sun-shadow-apply-fixture.json`; program provenance
`verification/results/sun-shadow-apply-program.json`.

Open: quads straddling a sentinel (sky silhouettes) are excluded from the strict compare as
ambiguous, so the quad's behaviour on those pixels is unverified beyond the byte-identity of the
sentinel pixels themselves; the bias constants at production scale and the `dsx`/`dsy` quad
convention on native D3D9 are unmeasured.

## Original-program share producer (2026-09-17)

`linear_material_original_sun_share_pixel_variant` (legacy-sun-application.md 1, "Share
producer"): the fill/motion variant of a reviewed original PS plus, on the original operands,
eight carrier initialisations (`r16`–`r23` = 0), one seed MUL/MAD before each sun MAD, one
parallel MOV/ADD/MUL/MAD per sun-dependent RGB op, the fill twin `fill(sum) − fill(sum − S_sum)`
at K > 0 (16 instructions, 34 slots), the final RGB instruction redirected into `r11` keeping
its `_pp` plus `mov oC0.xyz, r11`, the converted producer's reduction (`sun_reduction`, now with an
optional slack lane) and the sole `mov oC2.y, r23.w` after the motion body. Shader-local
`def c221 = (luma, 2^-20)` and `def c212 = (2.2, 0, 65504, 2^-16)`, both collision-checked with
`r11`–`r23` over the original; the emitted motion insertions (temporaries below `r11`, none of
`c212/c215/c221`) and the fill block (`r12/r13`, its sum, `c215`, the light constant) are
range-checked on their actual words at create time (`original_share_range_free`). A refused plan
keeps the fill/motion variant byte for byte (`share_applied = false`); vertex and unreviewed
programs are `UnsupportedShader`. Limitation of the witness set: the transformer is hash-bound, so
a plan refusal cannot be reached through the public API with any input (all 108 reviewed programs
admit; every other program is refused before planning); the refusal path is proved on the planner
and the range check via the include seam, and the fail-closed emission branch by inspection.

**Host** (`verification/analysis/test_original_sun_share.py`, helper
`verification/probe/original_sun_share_structure.cpp`): 108/108 PS admitted with the share; 152
seeds (64 one-seed, 44 two-seed programs); 29 VS refused; 108 × 2 depth × K {0, 0.05} = 432
variants; the control retained verbatim and in order with exactly the final redirect differing;
every original instruction retained in order; added instructions carry no `_pp` and read one
constant port; combined weighted slots ≤ 262 of 512 (K=0 adds 34–47, K=0.05 a further 34) — per
family maximum hull 207, asteroid 180, palette 214, glass 185, XT 262; 13 synthetic refusals
(untracked fill sum, no/duplicate/misplaced seeds, `_sat`, sun×sun, sun texcoord, `c221` DEF,
`r20` read; motion body reaching `r11` or `c221`, fill block on another sum). Float64 oracle on
the generated tails (seeds intact vs seeds zeroed, XT both branches): `s·Y(C) = Y(C) − Y(C_nosun)`
within 1e-9, colour/alpha/depth equal to the control, zeroed seeds give exactly 0 — 244 tails.

**Detached GPU** (`run_linear_material.py --original-sun-share`, fixture mode 8; compact record
`verification/results/bottle-X3/original-sun-share-gpu.json`, EXE sha256 `277789c1…`): 108 PS ×
{lit calibration M=1, zero sun, sun only} × K {0, 0.05} = 648 cases, 256 px each; colour, alpha,
motion and the RT2 depth bits byte-identical to the control on every pixel (FP16 and RGBA32F);
`oC2.g` against a zero-sun control draw (at K > 0 a fixture-only mode 9 keeps the fill tint
through `c200`, because zeroing the game constant alone also removes the fill term): max error
1.1e-4 (K=0) / 1.9e-4 (K=0.05) FP16 codes of Y(C), tolerance 1; zero-sun faces `s = 0` on all
27,648 px; calibration faces `0 < s < 1`; sun-only faces `s > 0` (max 1.0). Witness that fixed
the guard: on XT BUMP sun-only faces (pairs 150–155) the strict `S ≤ L` comparison read −1 on
66–86 of 256 px per case while `C_nosun = 0` exactly and the error was 1.1e-4 codes: the parallel
full-precision chain and the original `_pp` MAD chain differ by one ulp, flipping with the
occlusion texture's x band; the original variant therefore passes `c212.w = 2^-16` as relative
slack to that comparison (one MAD; the converted producer is byte-identical). The later
create-time range check changed no emitted bytes (432 variant files identical before/after), so
the GPU record stands. Cost is unmeasured: no `us` lines in this slice; the share PS is ≤ 175
(K=0) / 205 (K=0.05) device instructions. No live route, lane latch or native Windows evidence.

Commands:
`PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_original_sun_share verification.analysis.test_original_fill verification.analysis.test_linear_sun_share`
`sh verification/probe/build_linear_material.sh && X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_linear_material.py --original-sun-share --exe verification/probe/build/linear_material_fixture.exe --programs /tmp/x3-shader-sweep/programs`

## Lane latch and apply wiring (2026-09-17)

The proxy integration of [legacy-sun-application.md](../architecture/legacy-sun-application.md)
sections 2 and 4 (`src/proxy/motion_output.{h,cpp}`, `motion_output_shadow_replay_inc.h`,
`sun_share_lane_inc.h`, `capture.cpp`, `tools/manage.py`):

- Latch: `X3M_SUN_SHADOW_LANE` no longer needs linear materials (capture gate,
  `qualify_sun_lane`'s prerequisite, gate 4's third clause). `shadow_.cutout_pair` is the
  identity alone; the exact cutout arm keeps its own `linear_material_requested_` key, so
  with the lane off nothing routes differently. Under original shading a routed depth
  writer of a reviewed pair binds `sun_original_variant` (created once at registration,
  `linear_material_original_sun_share_pixel_variant` composed with `--original-fill` K,
  logged `sun_shadow_original_variant`, counted `original_variants` / `original_refused` on
  the lane line; fail closed to the plain lane motion variant) and is a receiver
  (`route.sun_receiver`). The two cutout pairs enter the tested-opaque arm with their own
  share; `cutout_opaque_*` now also appear on `sun_shadow_lane_frame`.
- Scene end (hook and bloom-copy sites): `publish_sun_lane` → replay transaction (on
  success `set_view_rows` of the replayed frame; a refused frame clears them) →
  `run_sun_shadow_apply` → AO → resolve. The quad runs only with the lane published
  available, `replayed > 0` this frame with rows and map, the owner term valid, HDR
  active and RT0 the FP16 target; every miss skips with the frame byte-identical. One
  `sun_shadow_apply_frame frame= applied= skip_reason= exponent= us= map= result= restore= stage=`
  line per frame with the option on; `sun_shadow_apply_device` once per attach
  (retried after Reset); `before_reset` / `after_reset` forward to the pass. Exponent
  1 on original shading, 1/2.2 with linear materials. Capture frames additionally dump
  the replay map (`shadow_map_<device>_<frame>.r32f`, `shadow_replay_map_readback`) with
  a `shadow_replay_map_basis` line (basis, extent, view rows) whenever the replay is on.
- Launcher: `--sun-shadow-lane` requires `--motion-output --taa --hdr`; new
  `--sun-shadow-apply` (default off) requires `--sun-shadow-lane --shadow-replay-depth`
  and sets `X3M_SUN_SHADOW_APPLY=1`.

Evidence (seam DLL `verification/probe/build/motion-output-seam/d3d9.dll` from this tree;
production `build/d3d9.dll` sha256 `451d3fca…0813`, `check_no_x87.py`: 0 violations):

- `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3
  verification/probe/run_sun_share_live.py --fixture verification/probe/build/motion_output_fixture.exe
  --dll verification/probe/build/motion-output-seam/d3d9.dll`: all 19 cases pass
  (`verification/results/bottle-X3/sun-share-live.json`). `original_lane` (linear
  materials off): 6 frames lane available, `positive=3600` on the sun frames, `zero=3600`
  on the zero-sun frame, two `sun_shadow_original_variant` lines (`8759c7838bbc86c2`,
  `63f96eba9eea7880`, `share_applied=1`), frame 2 `SUN_ORIGINAL_CUTOUT … routed=1 gate4=0
  opaque_routed=1 opaque_lane=1 opaque_refused=0 untracked=0`. `shadow_apply` (ownership,
  rotating camera, 512² map over a 32-unit half-extent, a caster at view depth 16 behind
  the receiver at 12 under the +Z sun): `apply_frames=4`, skipped `{0: replay (lease
  refused), 2: lane (untracked writer)}`, `exponent=1.000000`, `map=512`, `apply_us_max=1716.9`
  (the JSON carries the per-run maximum; the first quad includes the block capture), `replayed=2` on frames 1–5, Reset before
  frame 4; TAA output compared byte-identical on frames 0–2 and within one FP16 code of the
  CPU-shadowed reference `C·(1−s)` on frames 3–5 (`shadowed_pixels_min=3969` of 4096 per
  frame, all darkened by ≥ 1 code); `exact_taa_frames` is derived per run (6 of 6 exact on
  the review rerun, 3 required).
- `PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_sun_share_lane
  verification.analysis.test_original_sun_share`: 16 tests OK (new: the launcher acceptance
  without `--linear-materials`, the `--sun-shadow-apply` refusal, the latch source contract,
  witnesses for both new cases). Snippet mocks and fixtures
  (`test_linear_cutout_contract`, `test_linear_emission_source_gain`, `test_motion_wrap_states`,
  `test_original_fill`, `test_capture_device_creation`, `test_shadow_replay_depth`,
  `test_sun_shadow_apply`): 42 tests OK.
- Fail closed (review round 3): a reviewed original pair whose share producer refused keeps
  its fill/motion bind pair and fails the frame's lane (`original_refused_draws` on the lane
  line, `sun_frame_.failed`); live case `original_share_refused`
  (`X3M_FIXTURE_SUN_LANE_FAULT=original_share`, `--original-fill 0.05`): 6 frames
  `available=0 failed=1 original_refused_draws=1`, `original_fill_frame admitted=1` each frame.
  The replay transaction runs once per frame (a second EndScene keeps the first result's rows
  and counts); `after_reset` clears the per-frame apply/replay frame markers.
- `python3 tools/manage.py launch --dry-run … --sun-shadow-lane --shadow-replay-depth
  --sun-shadow-apply …` prints `X3M_SUN_SHADOW_LANE=1`, `X3M_SHADOW_REPLAY_DEPTH=1`,
  `X3M_SUN_SHADOW_APPLY=1` with `X3M_LINEAR_MATERIALS=0`.

Assumptions: the live comparison tolerates one FP16 code on shadowed frames (the quad's
`pow`/multiply rounding and the history of an earlier shadowed frame), not byte identity;
the `shadows=` field of `sun_shadow_lane_frame` now reports whether the apply option is on;
the quad reconstructs view z from RT2's z/w with AO's default depth mapping
(`ao_default_m22` / `ao_default_m32`, the game's zn = 6 / zf = 2e6 projection), not the
frame's own projection terms, which the camera latch does not carry (note, unknown 2); the
live fixture's perspective rows use the same mapping. `shadow_.cutout_pair` is evaluated
and the `cutout_opaque_*` counting runs only with the lane or linear materials requested.


## Run 36 session B (run106): first apply in game (2026-09-17)

Run 36 B (`/tmp/x3-bottleX3-run106`, proxy `51a3d764…` from c9a8145, original shading,
`--sun-shadow-lane --shadow-replay-depth --sun-shadow-apply`): 24,700 frames
`sun_shadow_apply_frame applied=1 exponent=1`, 32 F8 frames with RT2, map and post-apply HDR
dumps (`hdr_readback` follows `sun_shadow_apply_frame` in the log, the RT2 dump follows the
UI draws, which route nothing: `motion_route` stops at draw 159 of 187). The user saw no
shadow; the first offline analysis reported `sun.z − map` with std 0.10 and a shadowed
fraction swinging 74 % → 28 % with the bias.

**Diagnosis (frames 8979 and 18265; `python3 verification/probe/sun_shadow_apply.py --log …
--capture /tmp/x3-bottleX3-run106 --frame N`, the fixture's CPU twin on the run's data):**

- The projection matches. `camera_state` gives `p00=0.8 p11=1.333333 p20=p21=0` on every
  capture frame (the engine's projection carries no jitter; the route jitters the rows
  itself: `jitter_x=-0.25 jitter_y=0.166667` px on 8979, `-0.4375/0.388889` on 18265).
  Receiver minus nearest map texel on the 15,198 / 17,678 valid pixels: p25/p50/p75 =
  −0.0002 / +0.0004 / +0.0013 (8979) and −0.0008 / −0.0004 / +0.0003 (18265), i.e. within
  ±1 unit for half the pixels; p95 +0.019 / +0.017 (self-occluded pixels 17–19 units behind
  their caster); p5 −0.46 / −0.45. The std 0.10 was that last 5–6 %: receivers whose map
  texel is empty (1.0), the silhouette fringe of a 0.75 %-occupied map — lit by the compare,
  not scatter. Std over the map-covered pixels is 0.0069 / 0.0061.
- Geometry: light-travel direction against the camera forward is 148.7° / 150.3° (the sun
  31° off the view axis ahead of the camera, elevation 27°), the cascade centre 128 units
  ahead as designed. Self-shadows are therefore short strips behind low protrusions on
  the top hull (cockpit hump, engine block, fin roots) plus one-texel silhouette fringes.
  The twin's factor image (ASCII mask in the CLI output; PNG scratch) shows exactly that: the
  hull is factor 1 almost everywhere, `factor < 0.7` on 19 % / 13 % of valid pixels, in
  strips 1–3 px wide; mean factor 0.854 / 0.904.
- The quad did apply. Shadowed pixels (`f ≤ 3/9`, unambiguous) against same-surface lit
  neighbours within 4 px (|Δz| < 1 %, |Δs| < 0.05): HDR luminance ratio p25/p50/p75 =
  0.32 / 0.46 / 0.71 versus the twin's predicted factor 0.36 / 0.44 / 0.65 on 133 pairs
  (8979), 0.17 / 0.37 / 0.70 versus 0.21 / 0.35 / 0.51 on 244 pairs (18265); binned by
  predicted factor, [0, .35) → 0.21 / 0.26, [.35, .55) → 0.46 / 0.43, [.55, .8) → 0.69 / 0.72.
  Lit–lit control pairs (7,522 / 11,238): 0.98. The GPU multiplied by the twin's factor.
- The first analysis's swing came from a nearest-texel hard compare without the PCF and
  receiver-plane term, over a region that included the station behind the ship.

**Verdict.** No single defect: the pass, the rows and the map agree to the map's
quantization, and the HDR carries the predicted darkening. The visible result is small
because the captured views are backlit at 31° with a low-relief hull; the "no shadow"
impression is the geometry, not the pass. One plumbing input was wrong and is fixed:
`run_sun_shadow_apply` passed the camera latch's `m20/m21` (0), ignoring the route's
raster jitter; it now adds `+2 jx / width`, `−2 jy / height` (the `jitter_rows` convention),
as the design's "NDC from the quad UV minus the frame's jitter". Measured effect on the twin:
`f < 0.9` 0.450 → 0.462 (8979), 0.316 → 0.312 (18265); the run106 quad was wrong by up to
0.44 px, below the shadow-edge scale. Bias: constant 0.001 (1 unit) and clamp 0.01 keep
the hull acne-free; with constant 0 the twin shadows 86 % / 52 % of the ship (acne on the
covered hull), so the defaults stay.

**Diagnostic added.** On capture frames `run_sun_shadow_apply` logs
`sun_shadow_apply_params device= frame= m00= m11= jitter_x= jitter_y= m20= m21= m22= m32=
texel= bias= bias_max= planar_step= exponent= jitter_index= map= width= height= rows=<12>`
once per frame after a successful quad (c0–c6 of `sun_shadow_apply_ps.hlsl` exactly).
`sun_shadow_apply.py` gained `parse_apply_params`, `reconstruct_params` (the run106 path from
`camera_state`, `motion_output_frame` and `shadow_replay_map_basis`), `frame_params` (prefers
the line), `load_capture`, `frame_report` (residual percentiles, factor statistics, the
neighbour-pair darkening test with its control, ASCII mask) and a CLI (`--log --capture
--frame [--region y0,y1,x0,x1] [--bias] [--no-jitter]`). `test_sun_shadow_apply`: 10 tests OK
(4 new: line format and errors, reconstruction equality, line preference, report shape).

**Next run.** A capture with the sun 60–120° off the view axis (ship side-lit) at the same
spot; the `sun_shadow_apply_params` lines make the twin exact. Fixture: `sun-shadow-apply`
(`run_motion_output.py`) on this tree, result below.

Fixture on this tree (`X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3
verification/probe/run_motion_output.py sun-shadow-apply`, clean cmake build with the
`run_sun_shadow_apply` change): `checks=156 worst_codes=0.998 ambiguous_max=245
edge={mismatch 376, within one texel 345, two 31, beyond 0} us=3286.4` (median, CPU-inclusive),
the same counts as the pass's original record; the runner's single-case status is `PARTIAL`
by design. The runner needs `verification/probe/build/` to exist (a fresh worktree lacks it:
`abi_check.o` cannot be created), which the first attempt hit before any Wine work.
