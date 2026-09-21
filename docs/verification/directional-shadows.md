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

### Run 37 session B (run109), 2026-09-17: first visible shadows; station box too small

Installed run37 build `61725145…` (geometry casters, jitter term), original
shading, `--sun-shadow-lane --shadow-replay-depth --sun-shadow-apply`. User
report: shadows on the ship's hull from station geometry, and the ship's
shadow on station geometry only when very close; no station-on-station
shadowing. Log (39,499 frames): routed depth writers median 93 / max 930 per
frame; admitted casters (`leased` = `replayed` = `draws`) median 8 / max 49;
`capped` 0 everywhere; `dynamic`/`default_pool`/`excluded`/`unknown` 0;
`bounds` admissions on 66 % of frames, `fallback` on 34 %; reads ≤ 5 per
frame, no unreadable. Basis: `extent=250`, `depth_half=512` around the camera
on every capture. F8 frame 32256 (at the station): map 20.5 % occupied with
station structure, twin `covered=61 %`, `f<0.9` on 35 % of covered pixels,
HDR darkening ratio 0.526 vs predicted 0.483 (shadows real and applied);
frame 39454: map 1.2 % occupied (near-camera content only), `f<0.9` 33 %.
**Conclusion:** the apply and replay work; the 250-unit box around the camera
is what limits station-on-station shadows to the parts near the ship. The
extent, depth half-range, cap and bias become launcher options for run 38
(extent 1000–1500 at 2048–4096 texels); cascades remain the answer for a
whole complex. Frame-time comparison not possible at fine granularity (no
`--frame-phases` in this session).

## Tunable cascade-0 box, cap and world-unit bias (2026-09-17, worktree `agent-ae233fa95aef3fdd5`)

Run 37 B showed the map box (half-extent 250, depth ±512 around the camera, hard-coded) admitting
a median 8 / max 49 casters of 93 / 930 routed depth writers, `capped` never firing; the user
wants station-on-station shadows across 2–3 km. Change: `--shadow-replay-extent E` (50–4000,
default 250, `X3M_SHADOW_REPLAY_EXTENT`), `--shadow-replay-depth-half D` (128–8192, default
512, `X3M_SHADOW_REPLAY_DEPTH_HALF`), `--shadow-replay-cap N` (1–1024, default 512,
`X3M_SHADOW_REPLAY_CAP`; record storage 512 → 1024 fixed entries, `Record`, `DepthGeometry` and
`ShadowReplayDraw` arrays in `MotionOutput`, no allocation), `--sun-shadow-bias-units B`
(0–1000, default 0.53571875, `X3M_SUN_SHADOW_BIAS_UNITS`) and `--sun-shadow-bias-clamp-texels T`
(1–64, default 20.97152, `X3M_SUN_SHADOW_BIAS_CLAMP_TEXELS`). All read once at device creation
(`capture.cpp`); the candidate box test, the replay projection and the apply quad take the box
from the one `ShadowReplayCascade`; `shadow_replay_depth_mode` prints `extent= depth_half= cap=`.
Bias (`sun_shadow_apply_bias`, `sun_shadow_apply_pass.h`): constant `(B + 2E/N) / 2D`, clamp and
non-planar fallback `T · (2E/N) / 2D` (world texels: the plane term extrapolates a slope over
at most ~1.9 texels), resolved per frame in `run_sun_shadow_apply` (`skip_reason=bias` when
the law does not resolve);
the defaults are `1.024 − 0.48828125` and `10.24 / 0.48828125` so 250 / 512 / 1024 resolves to
exactly the former float32 0.001 / 0.01 (`test_resolved_bias`). A first version scaled the clamp
with the constant (10 : 1); it made the live `shadow_apply` case's clamp 6.6 units on its 32 / 64 /
512 cascade, above the 4-unit occluder gap, and lit the receiver's sentinel-edge quads through the
fallback (frame 4, TAA off the CPU reference by more than one code); the texel law gives 2.62
units there. The capture-frame
`sun_shadow_apply_params` line gained `bias_units= clamp_texels= texel_world= extent= depth_half=` and the twin's
`parse_apply_params` refuses a line whose printed `bias`/`bias_max` do not resolve from them. (The
brief named 0.003 / 0.01 as today's constants; those are the fixture's literals, production was
0.001 / 0.01 since run 36 and stays so.)

**Cost model.** Map memory `4 N²` bytes R32F plus the D24X8 attachment: 4 + 4 MiB at 1024²,
16 + 16 MiB at 2048², 64 + 64 MiB at 4096². World texel `2E/N`: 0.488 (250 / 1024), 0.977
(1000 / 2048), 0.732 (1500 / 4096). Replay transaction (fixture, CPU-inclusive `us` medians over
the 5 replayed frames): 40.6 µs at 2 draws / 256², 45.8 at 2 draws / 1024² (250 box), 55.8 at
4 draws / 2048² (1000 box: 14.0 µs per draw), first frame after a target creation 1.4–1.6 ms;
the earlier 2 / 8 / 20-draw series (40.7 / 50.4 / 65.8 µs at 1024²) gives ≈ 35 µs fixed plus
≈ 1.4 µs per draw, so a 512-caster frame costs ≈ 0.75 ms CPU before the GPU's own rasterization,
which scales with the casters' triangle counts, not with N. The map size costs the clear and the
apply quad's cache footprint only; the fixture's apply quad median was 3.84 ms at 256² and 3.72 ms
at 2048² (128² target, CPU-inclusive, first frame 22 ms in both).

**Fixture (`run_motion_output.py` on the tree rebased onto `eb5cdab`, clean cmake build, 0 warnings;
`check_no_x87.py` 0 violations).**
`seam-ownership-shadow-replay-on` 169 checks and `seam-ownership-taa-shadow-replay-on` 192: unchanged
behaviour under the new mode line (`extent=250 depth_half=512 cap=2`; the seam narrowing to 8 / 16
still applies), map versus the CPU projection max depth error 1.17e-05, 40,681 covered texels.
New `seam-ownership-shadow-replay-wide` (169 checks; `X3M_SHADOW_REPLAY_EXTENT=1000 …DEPTH_HALF=2048
…SIZE=2048 …CAP=4`, F moved to origin 900 / vertices 825 units by `X3M_FIXTURE_SHADOW_FAR_T=720`, no
narrowing): from frame 1 `bounds=4 capped=0` (L, both casters and F admitted; F replays too),
`replayed=4 draws=4`, frame 0 `fallback=2 reads=4`; map versus CPU 0 coverage disagreements, max
depth error 1.42e-06 over the 4,096-unit depth range, 199 covered texels (48–52 per replayed
frame at the 0.977-unit texel; the unit-size casters cover 2). New
`seam-ownership-shadow-replay-far-refused` (169 checks; the production 250 / 512 box, 1024², same
F at 825 units): `bounds=3 capped=1`, F refused as in the narrow cases, max depth error 3.28e-06,
710 covered texels. Both keep the Lock / no-sun / two-stream refusals and the Reset re-creation.
`sun-shadow-apply` (default): 163 checks (126 fixture, unchanged, plus the new configuration and
per-frame bias checks), `worst_codes=0.998 ambiguous_max=245 edge={376, 345, 31, 0}`, the same
counts as the committed record: the literal .003 / .01 path is byte-identical. New
`sun-shadow-apply-wide` (167 checks; `X3M_FIXTURE_SUNAPPLY_WIDE=1`, the scene scaled ×200 — a
400-unit box, the camera 1,500 units away — under 1000 / 2048 / 2048², bias resolved from the
default B: constant 3.69e-4 = 1.51 units = 1.55 texels, clamp 5.0e-3 = 20.5 units = 21 texels):
0 FP16 violations in 35,266 strict comparisons against the twin, worst 0.998 codes; the analytic
box-on-plane shadow on the plane receivers: 33 disagreements, 29 within one texel of an edge,
4 within two, 0 beyond (the twin's kernel reach); on the box's own faces 14 pixels beyond two
texels (7 / 5 / 1 / 1 at 30° / 50° / 70° / 70°; 8 with the earlier 15-texel clamp), all
camera-silhouette quads where the plane fit is dropped and the fallback pulls the receiver
21 texels towards the light: with the clamp at 4 texels the twin shows 0 such pixels (scratch
analysis of the 30° frame). The validator holds the plane receivers to `beyond_two_texels = 0`
and bounds the box faces at 16. Frames 0, 1 and the Reset identity as before.

**Live** (`run_sun_share_live.py --fixture … --dll …`, the full set after the rebase onto
`eb5cdab`; `shadow_apply` on the seam's 32 / 64 / 512 cascade, where the law gives 5.16e-3 /
2.05e-2 = 0.66 / 2.62 units under the 4-unit occluder gap, recorded as `bias.resolved_by_law`
from the mode line's `bias_units= clamp_texels=`; the DLL prints resolved values only on capture
all 20 cases pass, 32.2 s of fixture time; `shadow_apply` 4 applied frames, shadowed pixels
≥ 3,969 per applied frame, 6 TAA frames exact against the CPU-shadowed reference. Record
`verification/results/bottle-X3/sun-share-live.json` (the earlier `--case shadow_apply` run had
overwritten the 19-case record with one case; restored and rerun in full).

**Host tests.** `test_shadow_replay_depth` (8: new `test_cascade_box_options`,
`test_bias_units_option`), `test_shadow_replay_candidates` (7: new `test_cap_option`),
`test_sun_shadow_apply` (11: new `test_resolved_bias`), `test_lod_scale_launch` OK; launch
`--dry-run` with `--shadow-replay-extent 1500 --shadow-replay-depth-half 3000 --shadow-replay-size
4096 --shadow-replay-cap 512` prints `X3M_SHADOW_REPLAY_EXTENT=1500.0 …DEPTH_HALF=3000.0
…SIZE=4096 …CAP=512 X3M_SUN_SHADOW_BIAS_UNITS=0.53571875`.

**Open.** The 21-texel clamp (today's production 0.01 kept for identity) lights a few
silhouette pixels of a receiver's own faces in the wide fixture (14 of 35,266) while 4 texels
(the detached fixture's tuned literal) does not; whether the game's hulls need the wide fallback
(run 36 tuned only the constant) is a run-38 question (`--sun-shadow-bias-clamp-texels 4`, no
rebuild), as is the visible result of a station-wide box. The bias in texels at the wide setting (1.55) is what the receiver-plane term expects; the
default B alone (0.536 units) is close to one production texel (0.488).

## Run 38 A (run111)

Capture `/tmp/x3-bottleX3-run111` (5.0 GB, 256 files, session log
`session-20260917-073417-212.log`, 10,807,495 lines, 634 MB — never opened
whole; queried with `grep`/`python3`). Options (from `proxy_options` line):
extent=1500, depth_half=3000, map=4096, cap=512, bias_clamp_texels=4,
`X3M_SUN_SHADOW_APPLY=1`, original hull shading (`fill=0 share_applied=1` on
`sun_shadow_original_variant`). Six F8 dumps of 8 frames each: 3897-3904,
15565-15572, 16050-16057, 16633-16640, 19205-19212, 19584-19591 (48 frames,
matches `shadow_replay_map_basis`/`sun_shadow_apply_params` counts of 48).
Screenshots `shadow1..3-{1,2}.png` (shadow absent / present after a slight
pitch-down) are user-side; the corresponding F8 bursts were not separately
timestamped against them in-log (`camera_state` has no field tying a frame to
a named screenshot), so the screenshot-to-burst mapping is inferred from file
mtimes only, not verified against the log.

**Q1 caster counts (H1/H2).** Over all 34,529 `shadow_replay_candidates`
frames: `bounds` median 8 max 51, `leased` (admitted) median 8 max 52,
`capped` median 0 max 0 (`capped>0` in 0/34,529 frames — **H2 refuted**: the
512 cap never engages, so cap-driven reordering cannot be the cause).
Within two F8 bursts where `camera_state` `t=` and every `rNN=` field are
byte-identical across all 8 frames (16633-16640, 19205-19212 — camera frozen,
not turning or moving), `leased` still oscillates: 22,25,25,24,25,20,22,25 and
19,21,24,24,23,24,19,21. **H1 refuted as the sole/primary mechanism**: caster
admission changes frame-to-frame with zero camera motion. Mechanism found in
`src/proxy/motion_output.cpp:6386-6414` (`note_candidate_draw`): a caster
whose vertex-buffer extent isn't cached yet is admitted by a coarse
`origin_rule` (near/far distance only, field `fallback`); extents are filled
by a per-frame-budgeted read (`reads=` field, observed 1-25/frame) via
`queue_candidate_extent`; once known, the real bounds verdict
(`shadow_replay_bounds_verdict`) can exclude a caster the distance rule had
admitted. This extent-cache warm-up/eviction race, not camera angle or the
cap, is the quantified source of the observed admit/drop flicker; two other
bursts (16050-16057, 19584-19591) hold steady at `leased=8` (only the fixed
8 `origin` casters, no extra scenery in range), so flicker is scene-dependent.

**Q2 apply/lane availability (H3).** `sun_shadow_apply_frame`: applied=1 in
34,426/34,529 frames (99.70%); `skip_reason=replay` in 103/34,529 (0.30%),
in 3 contiguous runs (frames 578-597, 6467-6502, 13972-14018), each run
showing `slice0=0 bounds=0 fallback=0` on the matching
`shadow_replay_candidates` line — no candidates existed that frame (empty
scene/loading), not a pitch-triggered refusal. `sun_shadow_lane_frame`
`available=1` in all 34,529 frames. **H3 refuted**: apply is refused only
when there is nothing to apply, not correlated with camera pitch.

**Q3 placement/basis (H4/H5).** `shadow_replay_map_basis.center` is
bit-identical across both frozen-camera bursts (16633-16640:
`-72179.2891,888.427734,-37306.6406` all 8 frames; 19205-19212:
`-72886.8359,1565.91797,-37671.3867` all 8 frames) — no texel-snap jitter
while the camera itself is still. In the moving burst 3897-3904, consecutive
`center` deltas are ~171 units/frame (170.9-172.7, effectively constant)
while the chase-camera `t` delta shrinks 234.8→177.8 units/frame (ship
decelerating, camera lagging) — the map center tracks something with
different dynamics than the visible camera (consistent with tracking the
ship/object transform rather than the interpolated chase camera), but this
capture alone does not show a placement error from it. **H4 undetermined**:
no snap artifact found in this data, but the center-vs-camera decoupling is
unexplained and not cross-checked against ship transform logs.
`sun_shadow_apply_params` at 4096²: `texel_world=0.732421875`,
`clamp_texels=4`, `bias_units=0.53571875` (matches the configured
`--sun-shadow-bias-clamp-texels 4`), constant across the run — no CPU/GPU
twin comparison was run against the captured `depth_1_*.rg32f`/HDR frames in
this pass (`verification/probe/sun_shadow_apply.py` was not invoked; running
it against the 48 F8 frames is the natural follow-up). **H5 undetermined**:
the bias/clamp values are as configured but placement correctness against
the actual GPU output was not verified here.

**Q4 replay cost.** `shadow_replay_depth` `us` vs `draws` at map=4096, from
the 48 F8-adjacent frames (draws 6-25): roughly 40-110 us, no clear linear
fit attempted (insufficient distinct draw counts in this sample: 6, 8, 19-25);
whole-session median/intercept not computed this pass (34,532 lines
available for a full regression — open item).

**Q5 ~30s distant-object flicker.** No matches for `missed`, `history_reset`,
`history_drop`, or any TAA-history-invalidate cadence near 30 s:
`taa_invalidate` (483 lines) fires almost entirely in the first frames (0-9…)
then stops, not periodic. No `cutout::missed`-style line exists in this
build's vocabulary at all (0 hits). The only strictly periodic diagnostic is
`media_cue_window` (116 lines, exactly every 300 frames by frame count,
`qpc` delta 26.3 s between frame 299 and 599 at the frame rate then in
effect, i.e. period is frame-count-fixed, not wall-clock-fixed — coincides
with `X3M_MEDIA_CUE_RETRY_S=30` only by construction, each window reports
audio-cue `attempts/failures`, not object visibility). `frame_end.dt_ms`
spikes are two one-off stalls (15,467 ms and 26,317 ms), not periodic.
**Undetermined**: this capture contains no diagnostic that records distant-
object visibility/culling events; the log has nothing to attribute the ~30s
flicker to. A dedicated diagnostic is needed (see Open issues).

## Run 38 A (run111) diagnosis

2026-09-17, no source edits, no Wine. Inputs: run111 (A: 1500 / 3000 / 4096², clamp 4 texels) and
run112 (A2: 250 / 512 / 4096², default clamp 20.97 texels), build `e575136`. Summary lines were
extracted once into scratch slim logs; per-draw lines only for F8 frames 16635, 16051, 3900, 19586,
16640, 15572. "Measured" = from the capture or code; "inferred" is marked.

### Ranked root causes

**1. The sun direction is read from PS register c4 regardless of the bound program; c4 is
`LightDir_Dir0` in only some hull programs (decides wrong direction, pop with pitch, A = A2, the
self-shadow flicker and most of "only ≤ 52 casters").**
`src/proxy/shadow_replay_depth.h:16` (`depth_sun_register = 4`), latch
`src/proxy/motion_output.cpp:3169-3171` (any `SetPixelShaderConstantF` covering c4, any program),
consumers `motion_output.cpp:6437` (the frame's bounds rows, computed once at the first bounds
test, `candidate_bounds_state_` sticky for the frame) and
`motion_output_shadow_replay_inc.h:32,110` (replay sun = the c4 value latched at the first leased
record). Measured from the CTABs of the 38 dumped pixel programs: c4 is `LightDir_Dir0` in 16,
`g_EnableGlow` in 6 (`fffdabd9…`, `5f82ecac…`, `f1b0e820…`, `496049ce…`, `6733b119…`,
`e6794b6e…`; their `LightDir_Dir0` is c5), `p_DetailMapBlendWeight` in 2 (`517540ae…`,
`d44db877…`; `LightDir_Dir0` is c0), unused in the rest (`a66fb198…` has `LightDir_Dir0` at c0).
On frame 16635 the glow programs carry 144 of 431 eligible routed draws. Two frame states follow
from the first routed draw (measured on the per-draw lines):
- **State A** (first routed draw is a `p_DetailMapBlendWeight` program: an asteroid, model
  `4fef`/`4fee`; frames 16051, 3900, 19586): c4 is not a unit vector, `shadow_replay_basis` fails,
  `candidate_bounds_state_ = -1` for the whole frame, every draw falls back to the 250-unit origin
  rule → `bounds=0 fallback=8 leased=8`: the own ship only, with the true sun (first valid c4 is
  the ship's). Map occupancy 0.01–0.03 % (one 111 × 66-texel blob) although 72 % (636,294 of
  878,288) of the visible geometry pixels of 16051 lie inside the 1500 box. Session: 14,645 frames
  (42.4 %), `leased` max 8. This is why A looks like the 250 box and like A2.
- **State B** (first routed draw is a `g_EnableGlow` station program, model `53a1`; 16635, 15565–72,
  16633–40, 19205–12): c4 = (1,0,0,0) passes `shadow_replay_sun_valid`; the basis is
  `forward=(-1,0,0) right=(0,0,1) up=(0,1,0)` on 24/24 captured B frames of run111 and 2/2 of run112,
  versus the true `forward=(0.2965,-0.4568,0.8387)` on all 38 A frames: 107° off. Bounds test, map and
  apply rows all use the bogus axis consistently, so the GPU matches the twin (16635: darkening ratio
  median 0.2236 vs predicted 0.2235; control 0.997) while 99.5 % of the 852,599 in-box pixels are
  shadowed (map 20.3 % occupied, occupied bbox touching the map edges, map depth minimum 1e-8 = casters
  cut by the near plane of the ±3000 range, `D3DRS_CLIPPING` on, no pancaking,
  `shadow_replay_pass.cpp:199`). That is screenshot `-2`: a large hard-edged dark region in a direction
  unrelated to the sun. Session: 19,781 frames (57.3 %), `leased` median 24 / max 52; 789 A↔B
  transitions. Whether any B frame had the true sun is not observable outside captures (inferred: only
  when the first bounds-tested draw is a c4-`LightDir` program).
- The pitch dependence (measured on `shadow1-1` vs `-2`): the asteroid visible at the top of `-1` leaves
  the view in `-2`; the engine stops submitting it, the first routed draw becomes a station glow draw, A → B.
- Run112 self-shadow flicker: frames 20602 and 21142 are single B frames inside A bursts at a resting
  camera (`replayed` 8 → 25 → 8, basis flips to (-1,0,0)); twin 20602: 20,968 of 25,107 ship pixels
  shadowed vs 2,311 / 2,373 on 20601 / 20603, mean luminance 0.154 vs 0.230. 560 single-frame B blips in
  19,699 frames. Trigger (inferred from cause 2): the extent of the first (detail) draw is evicted that
  frame, so it never calls `ensure_candidate_bounds_rows` and a glow draw seeds the frame.
- "Shadow moves with the ship": in B the direction is a world axis and the receiver set is whatever
  station parts pass the bogus box; with cause 2 and 3 the caster set changes with position and view.

Fix direction (N cascades): take the sun per draw from the bound program's own `LightDir_Dir0` register
(CTAB-resolved per pixel program at creation, cached by program id; refuse a program without it), latch it
only on writes made while such a program is bound, and resolve **one** frame sun before any cascade's
bounds rows are built (previous frame's validated sun for the draw-time tests; never "first valid c4").
Add a plausibility gate: reject a candidate sun that differs from the retained sun by more than a few
degrees unless it persists. The bounds-row failure must not be sticky-silent: log a per-frame
`bounds_state` and keep the previous frame's rows instead of dropping to the origin rule. Every cascade
shares the one sun so retained-basis reuse stays valid.

**2. Extent cache thrash (leased oscillation at a frozen camera).**
`src/proxy/shadow_replay_candidates.h:153-165` (1024-slot direct-mapped, a collision evicts),
`motion_output.cpp:6406-6414` (a miss falls back to the origin rule and queues a re-read),
`:6472-6487` (the re-read evicts the partner). Measured: the submitted node set is identical across the
frozen burst (68 = 68 nodes, 431 draws), yet `reads` never reaches 0 and cycles with period 6
(19,7,8,3,9,6) with `leased` 25,20,22,25,25,24; frames with `bounds=13 fallback=7` are the evicted
own-ship draws (still admitted by origin) and 5 evicted station draws (not admitted). Replaying the hash
on frame 16635's keys: 251 distinct keys in 227 slots, 24 colliding slots holding 48 keys / 82 draws
(e.g. slot 322: vb 2161 model `53a1` vs vb 836 model `bf23`; slot 191: vb 2193 `53a1` vs vb 798 `45c3`);
frame 16051: 311 keys, 40 colliding slots, 84 keys / 154 draws. Session: `reads>0` on 21,172 of 34,529
frames. Each flip also costs READONLY locks every frame forever.
Fix direction: a set-associative or open-addressed cache sized for ≥ 4× the routed key count (keys are
static: managed VB, revision), no eviction of a Known entry by a different key while it was used this
frame; a miss must keep the previous verdict for that draw (or be treated as "meets" for the outer
cascade) rather than the 250 origin rule. One cache serves all cascades (the extent is object-space).

**3. Engine view culling removes off-screen casters (structural, second-order today).**
Measured: 15572 vs 16635, 64 units apart, forward vectors 7.1° apart: 64 of 127 nodes (98 draws) are
not submitted in the second; 16051 vs 16635 (24°): 89 of 126 nodes (253 draws) missing, 31 new. A replay
built from the frame's submitted draws cannot cast from geometry outside the view. The user's point
stands, though: the screenshots' missing shadows are from **visible** geometry, which is cause 1 (state
A admits nothing but the ship), so culling's share of the reported symptom is small until 1 and 2 are
fixed; afterwards it is the remaining source of shadows that pop when the camera turns.
Fix direction: per-cascade caster retention for static nodes (keyed by node + extent key, world rows
derived from the draw's clip rows and that frame's camera, lease refreshed while the buffer stays quiet),
dropped after N frames unseen or on a registry/load epoch change; the retained-C2 design already needs
the same record.

**4. Depth range without pancaking.** `shadow_replay_pass.cpp:199` clips casters at the map's near
plane; measured in B frames (depth min 1e-8, bbox on the map edges). With the true sun a complex longer
than `depth_half` towards the light loses exactly the casters that matter (inferred for the true sun).
Fix direction: clamp light-space z to the near plane in the replay vertex program per cascade, or the
asymmetric range the cascade note's fixture (d) already names.

**5. Bias at the 0.73-unit texel (minor).** Frame 16051 (true sun): constant 1.27 units, clamp 2.93
units; of 8,490 receiver pixels on the map's own nearest surface 2,993 (35 %) have `f<1` but only 25
have `f ≤ 1/3`; 58 of 420 receivers lying 1.27–4.2 units behind an occluder are fully lit. A2 frame
20601 (0.122-unit texel, constant 0.66, clamp 2.56 units because A2 ran the default 20.97-texel clamp):
1,070 of 23,253 (4.6 %) with `f<1`, 107 of 338 lit behind. No evidence of detached shadows from the
4-texel clamp; the cost of the wide texel is soft partial self-darkening of the ship, an argument for
the own-ship cascade keeping its ~0.12-unit texel.

### What is correct (measured)

- Receiver reconstruction and replay rows: on true-sun frames the receiver's sun-space depth minus
  the map depth at its texel has median −2e-5 / +4e-5 / +7e-5 normalized (−0.12 / +0.24 / +0.07 units)
  on covered pixels of 16051 / 19586 / 20601, 54–76 % within 2e-4: the RT2 depth, jittered projection
  terms, camera and per-draw rows agree to well under a texel. The `rows=` of
  `sun_shadow_apply_params` and `shadow_replay_map_basis` are identical strings on every captured frame
  (same camera and basis for replay and apply).
- Centre and snapping: `center − (camera position + 128·forward)` in sun axes is ≤ 0.35 units (half a
  texel is 0.366) on all 11 checked frames, depth axis 0.00. The "171 vs 235→178" discrepancy is an
  artefact of comparing |Δt| of the view translation (which includes rotation) with world motion:
  |Δposition| with position = −t·Rᵀ is 170.8–172.7 per frame, equal to the centre delta. The forward
  offset moves the centre by at most 2·128·sin(Δθ/2) (22 units for 10°, 1.5 % of the 1500 box) and is
  snapped; it is not the pitch pop.
- GPU vs twin: 16635 0.2236 / 0.2235, 16051 0.338 / 0.372 (166 pairs), controls 0.994–0.997.
- Candidate box code path: `shadow_replay_bounds_verdict` and the replay both take the box from
  `depth_cascade_` (1500 / 3000, centre as above, view-space corners → sun NDC); the 250 constant
  survives only as the origin-rule fallback (`shadow_replay_candidates.h:15`,
  `motion_output.cpp:6380,6386`), which is what state A and every cache miss use.

### Ship shadow on stations only when very close

By the code the ship (always leased: `origin=8`) casts onto any receiver within ±E of the snapped centre
in the sun plane and ±D along the sun: 1500 / 3000 units in A, 250 / 512 in A2 (so "very close" is the
expected A2 result, as in run 37). Bias cannot swallow it (1.27 + ≤ 2.93 units against an ~80-unit
ship: map blob 111 × 66 texels × 0.732). Measured in A, true-sun frame 16051: 360 shadowed and 272
covered pixels at view depth ≥ 300, so the ship's shadow does land on the station when state A holds.
Near a station, however, the first routed draws are the station's glow programs, so the frame is in
state B (57 % of run111; 26/26 captured B frames bogus) and the ship's shadow is thrown along world −X
inside a scene already darkened by near-plane-clipped station parts. Inferred: the close-range sightings
are A frames (or B frames where the −X projection happens to hit a nearby face). Not separable further
without a capture taken while the user sees the ship's shadow on a hull.

### A vs A2

| | run111 A (1500 / 3000) | run112 A2 (250 / 512) |
| --- | --- | --- |
| frames | 34,529 | 19,699 |
| state A (bounds=0) | 14,645, leased ≤ 8 | 13,434, leased ≤ 8 |
| state B (bounds>0) | 19,781, leased median 24 / max 52 | 6,249, median 24 / max 44 |
| map occupancy, true sun | 0.01–0.03 % | ship only (24.8k covered px) |
| map occupancy, bogus sun | 15.5–20.3 % | single-frame blips |

The B counts are nearly equal in both boxes because along the bogus +X axis the admitted set is the
same handful of large station pieces; with the true sun the box never got to admit station casters in
any captured frame.

### Not determined

The value of c4 under the glow and detail programs is inferred from the basis, not logged. No per-draw
admitted flag exists, so the five flipping station draws are identified only as members of the 24
colliding slots. Screenshot-to-burst mapping remains by mtime. A capture-frame
`shadow_replay_caster` line (draw index, verdict source, sun register and value) would close all three.

## Sun-shadow cascades and the run-38 fixes (2026-09-17)

Contract [../architecture/shadow-cascades.md](../architecture/shadow-cascades.md) (its
"Implemented" section lists the deviations) plus the mandatory fixes of "Run 38 A (run111)
diagnosis" (main checkout) and the disassembly result in
[../reverse-engineering/camera-and-lights.md](../reverse-engineering/camera-and-lights.md)
("Directional lights: source, space and count"). Cascades are default-off
(`--shadow-cascades`); the fixes below are in shared code and change the single-map path too.
Worktree build (after the review fix round below): clean CMake build zero warnings,
`build/d3d9.dll` sha256 `3495fc3c…45f8280`, `check_no_x87.py`: 513 reachable functions, 0 violations; `build_motion_output.sh` (strict
`-Wall -Wextra -Werror`) clean. No game launch, no install.

**Where the defaults are not byte-identical (sanctioned, shared code).**
1. *One validated sun per sector.* `LightDir_Dir0`'s register is resolved per pixel program from its
   constant table at creation (`src/renderer/shader_constant_register.h`, strictly by name; measured
   on the local dumps: c4 `8759c783…`, c5 `fffdabd9…`/`5f82ecac…`, c0 `517540ae…`/`a66fb198…`);
   pixel registers c0–c31 are shadowed as written; every routed z-writing draw (routed = past the
   main-scene gate) samples its own program's register into `shadow_replay::SunLatch`
   (`src/proxy/shadow_replay_sun.h`): a sample counts only at unit length within 1e-3 with w = 0; the
   first of two agreeing draws latches (fix round below) and the value then stays fixed while samples agree within 1.5°; a frame
   whose samples all disagree is refused `sun_changing`; the same candidate for 8 consecutive frames
   re-latches (that frame refused, every retained cascade voided); a frame without samples reuses the
   sun; before any sample `no_sun`. The bounds rows are never sticky-unavailable
   (`bounds_unavailable=` counts extent-known draws that found no sun). Lines: per frame
   `shadow_replay_sun verdict= register= samples= agree= disagree= invalid= no_register= bounds_state=
   bounds_unavailable= extent_refused= sun=`, events `shadow_replay_sun_latch event=latch|relatch
   register= program= sun=`. Consequence for the script: frame 5 (formerly "no c4 write": the device
   register still holds the sun) now writes another direction for one frame and is refused
   `sun_changing`.
2. *Extent cache.* 1,024 sets × 8 ways (was 1,024 direct-mapped), hashed by the range without the
   revision; an entry returned this frame is never evicted this frame, a store into a fully used set is
   refused and counted (`extent_refused=`); a moved revision answers with the previous extent
   (`verdict=retained`) while the new one is read.
3. *Pancake.* The replay vertex program clamps the light-space z to the near plane and the pixel
   program stores `max(depth, 0)`; the box tests (single verdict and cascade mask) are open on the
   light side. A D3DSBT_ALL block does not restore "no declaration": `ShadowReplayPass` now re-sets the
   caller's FVF or declaration like `SunShadowApplyPass` (found by the direct-drive fixture, whose
   caller has none).
4. *Diagnostics (F8 only).* One `shadow_replay_caster` line per record (`record= vb= cascades=
   verdict=origin|bounds|retained leased= quiet= sun_register= sun_agrees= sun= primitives=`);
   `shadow_replay_map_basis` gains `sun= sun_register= sun_verdict=`.
Bias: unchanged. Engine view culling (cause 3): not addressed here by instruction; the next step is
per-cascade caster retention for static nodes, which needs a static-node identity (node + extent key),
world rows retained from the draw's clip rows and that frame's camera, a lease refreshed while the
buffer stays quiet, and expiry after N unseen frames or a registry/load epoch change.

**Cascades.** `ShadowReplayPass::attach_cascades` (≤ 4 R32F maps of their own sizes, one shared
attachment of the largest, sizes above `MaxTextureWidth/Height` halved, below 64 refused),
`execute_cascades` (one block capture/apply; per map SetRenderTarget, viewport, Clear, its issues;
the listed maps' retained bases voided first), retained basis per map (voided by `before_reset`,
`detach`, a refusal, a failed transaction, a re-latched sun). Projection helper: `depth_toward_light` /
`depth_behind` (the single map sets both to the half range: same doubles as before),
`shadow_cascade_set` (defaults 250 / 1500 / 7500 / 25000, 4096², caps 128 / 512 / 1024 / 1024, budget
640, towards the light 2 × the largest extent, behind max(512, 2 E)), `shadow_cascade_bounds_mask` (one
corner transform, 5 compares per cascade), `shadow_cascade_replays`. Candidates: a cascade mask per
record, per-cascade caps and `capped<i>`; issues storage allocated once at attach (sum of the caps).
Apply: `sun_shadow_cascade_apply_ps.hlsl`, 406 slots (the nine taps are a ps_3_0 loop over rotated
offsets uploaded as c4–c12; the unrolled form was 887 slots and this backend reports
`MaxPixelShader30InstructionSlots = 512`), five samplers, first containing cascade at 0.95, 10 % band,
last cascade fades to lit, absent cascade lit but still owning its pixels. The far map is valid when
replayed this frame or the previous one. Launcher: `--shadow-cascades E0,…|default`,
`--shadow-cascade-sizes`, `--shadow-cascade-caps`, `--shadow-cascade-budget`
(`X3M_SHADOW_CASCADES[_SIZES|_CAPS|_BUDGET]`; off exports `X3M_SHADOW_CASCADES=0` and drops the rest).

**Fixtures** (`X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3
verification/probe/run_motion_output.py --dll build/d3d9.dll --seam
verification/probe/build/motion-output-seam/d3d9.dll --fixture
verification/probe/build/motion_output_fixture.exe <16 cases>`; record
`verification/results/bottle-X3/motion-output-partial.json`, 16 / 16 pass):
- `sun-shadow-apply-cascades` (direct drive of both production passes, real geometry, three 256²
  maps, the checked default set; record `sun-shadow-apply-cascades-fixture.json`): 2,463 fixture checks
  + 188 validator checks, 20 frames. GPU against the cascade twin on the fixture's own map readbacks:
  worst 1.000 FP16 code, 0 violations, 0 identity or alpha changes; ambiguous ≤ 1,691 of 11,742–11,856
  valid pixels (14 %; on the first round's frame 0, 1,357 of 1,556 were the twin's `planar_step` band at
  the plane's horizon, 199 elsewhere). Edge rule `f ≥ 0.5` against the analytic shadow with the owning
  cascade's own texel: 931 disagreements in all, **0 beyond one texel**; interiors (≥ 2 texels from an
  edge, 3 from a contact line) wrong: 0; band monotonicity violations: 0.
  (a) frames 0–1: analytic shadow 879 / 2,376 px, all owned by cascade 1. (b) frames 2–3: 1,116 / 351
  shadowed pixels inside cascade 0's band, shadow on both sides (1,628 + 726, 79 + 798). (c) frames
  4–5: the occluder 2,000 units towards the light is in cascade 0's mask; its own shadow 188 / 256 px,
  interior 105 / 137 all at f = 0. (d) frame 7: camera moved 37 / 6.6 / 21.6 units, far cascade skipped
  (`issues=4 > budget=3`, odd), `far_frame=6`, 893 analytic px on cascade 2, 0 beyond one 58.6-unit
  texel. (e) frame 9 after the Reset: far absent, 9,042 far-owned pixels, 0 shadowed, target
  byte-identical; frame 10 equals frame 8 byte for byte. (f) frame 11: the occluder at 16,000 units
  (beyond the 15,000 light side) is admitted, ≥ 50 map texels at depth exactly 0, its shadow 188 px,
  interior 106 all dark. (g) frames 12–19: the half-texel witness (fix round below). Counters asserted per frame (`c<i>`, `draws<i>`, `far_replayed`, `far_frame`,
  valid flags, per-cascade bias against the law). Also: halving under a faked 128-texel limit
  (256 → 128 on all three, refusal below 64), mixed sizes 64 / 256 / 128 with a 256 attachment,
  a device with 256 slots refuses the cascade program and keeps the single one, the refusals touch
  nothing, 80 state restorations equal.
- `seam-ownership-shadow-replay-cascades` (through the DLL; seam extents 8 / 400, caps 2 / 8, budget 5
  < 6 issues): per frame `c0 c1 capped0 capped1`, `draws0 draws1 far_replayed far_frame issues
  budget` equal the script (frame 0 by the origin rule per cascade `c=2,4`; later `c=2,4 capped0=1`;
  far replayed on 0, 4, 6; retained on 1 (`far_frame=0`, map byte-identical); voided by the Lock
  refusal (3: `far_frame=-1`), the Reset, the sun-changing frame and the multistream frame); every
  replayed map against the CPU projection of the draws its cascade kept, max depth error 4.1e-6;
  245 checks. `…-cascades-casters-20` (1024² maps, 43 issues): 245 checks.
- `seam-ownership-shadow-replay-sun-programs`: the glow pair (`vs_494fe349…`/`ps_fffdabd9…`, c4 =
  (1,0,0,0), sun at c5) and the detail pair (`vs_b0602757…`/`ps_517540ae…`, c4 = (.3,.2,.7,0), sun at
  c0) drawn first on every frame, both routed; one latch event `register=5 program=fffdabd910793aba`;
  6 samples per frame all agreeing; the map's basis is the true sun's on every replayed frame
  (inferred, not run: the former c4 rule would have latched (1,0,0) from G, the first routed draw); 205 checks.
- Unchanged cases against their committed records (volatile keys aside): `sun-shadow-apply` and
  `sun-shadow-apply-wide` 0 differences; `seam-ownership-shadow-replay-on`, `-taa-…-on`, `-wide`,
  `-far-refused` differ only in the frame-5 refusal detail (`no_sun` → `sun_changing`) and the new
  `sun` block; on/off twins byte-identical presented frames (TAA off and on); casters 2 / 8 / 20 pass.
- Live (`run_sun_share_live.py --fixture … --dll …`, the full set after the fix round; record
  `verification/results/bottle-X3/sun-share-live.json` regenerated with 21 cases): 21 / 21 pass, 37.5 s of
  fixture time; the 20 existing cases equal the previously committed record (paths and timings aside). `shadow_apply_cascades` is the `shadow_apply` script through the DLL's
  cascade wiring (seam extents 32 / 256, 512² maps, capture window open): 4 applied frames, ≥ 3,969
  shadowed pixels, 6 TAA frames exact against the CPU-shadowed reference; on capture frames 1 and 3
  one `shadow_map<i>` readback and one `shadow_replay_map_basis cascade=` line per cascade, two
  `shadow_replay_caster` lines, the cascade `sun_shadow_apply_params` line parsed with every printed
  bias resolving by the law, and the cascade twin on the dumps (cascade 0 owns every receiver; the
  script's RT2 dump carries a zero share, so the twin runs with share 1).

**Cost** (measured in the fixtures; CrossOver, not game FPS). Bounds pass per draw, 2,000,000 rounds
in the fixture executable: single verdict 45.0 ns, four-cascade mask 44.6 ns (no measurable delta; the
mask multiplies by 1/m00 once instead of dividing per corner). Sun sample per routed z-writing draw:
one 16-byte copy and two dot products (not separately measurable). Replay transaction through the DLL
(`shadow_replay_depth us=`, unsynchronised CPU): single map 43.7 / 53.5 / 68.0 µs at 2 / 8 / 20 draws
(≈ 1.4 µs per further draw), two cascades 65.7 µs at 2–6 issues and 102.2 µs at 43 issues (≈ 1.0 µs per
further issue; ≈ +13–22 µs fixed for the second map's bind and Clear over two runs). The direct-drive fixture's
EVENT-synchronised replay time (0.8–17 ms) is GPU-inclusive and not a per-draw figure. Apply quad,
EVENT-synchronised: cascades 1.8 ms median against 3.5–5.2 ms for the single-map fixture runs in the
same session (128², dominated by the synchronisation). Memory while cascades are on: the maps, one
attachment, the issue storage (sum of the caps × 52 B: 137 KiB at the defaults); the extent cache is 704 KiB (8,192 × 88 B; was 72 KiB),
a `MotionOutput` member whether or not the counter is on.

**Host.** `test_shadow_cascades` (new, 4: a native driver over the four pure headers — constant-table
lookup on built glow / detail / hull / none / sampler / malformed tables and, when present, five local
dumps; the latch sequences including the glow program first; a frozen set of 1,500 extent keys with
identical answers and no store after the first frame, no eviction of an entry used this frame, stale
revision; the cascade set, budget policy, bounds mask with the open light side, shared-product light
rows; launcher options), `test_shadow_replay_candidates` (10), `test_shadow_replay_depth` (11),
`test_sun_shadow_apply` (16), `test_sun_share_lane` (11), the six mock modules (the
`linear_material_live` mock gained the new members), `test_comparison_hotkeys`; the nine bloom
manifests' generator hash updated (program words untouched). The 13 affected modules: 89 tests OK. One full discovery run before the repairs: 2,120 tests, 15 drift
failures (nine bloom manifests, six `test_comparison_hotkeys` subtests that execute `main()` without the
module's helpers: the cascade validation is therefore a nested function of `main`); both modules rerun
green, the full run not repeated.

**Open.** (1) Native Windows unverified, as everywhere. (2) The cascade program needs 406 of the 512
ps_3_0 slots; a loop inside a dynamic branch is what buys that, so a driver that mishandles it would
show up in the fixture, not in the caps. (3) d3dx warns X4121 on the cascade source (it hoists the two
derivative instructions out of the branches, which is where they already are: dsx/dsy at instructions
110–111, first `ifc` at 116). (4) State-block-applied pixel constants bypass the register shadow (as
they bypassed the c4 latch). (5) Per-vertex pancaking lets a triangle that crosses the near plane lose
the depth test to a caster truly behind it over its far part (the stored value stays exact); the
asymmetric range keeps such triangles rare. (6) Defaults (budget, caps, sizes, extents) await run 38's
numbers; every one is an option. (7) A lost device takes the same refusal path as any failed
transaction (everything retained voided, restoration stopped as in the other passes); no fixture forces a
device loss, for the cascades as for the single map.

### Review fix round (2026-09-17, second commit)

Two reviews of `3f14880`, no blocker; seven items. Final binaries `build/d3d9.dll` `3495fc3c…`, seam
`36539843…`, fixture `8b08e964…`; the 16 motion-output cases and all 21 live cases rerun on them.

1. **Half-texel lookup** (both apply programs and the twin). The replay rasterizes under D3D9: map
   texel (i, j) holds the depth at map position (i, j) / N but is addressed at ((i, j) + 0.5) / N, so the
   receiver now looks up at `suv = muv + 0.5 / N` (nearest texel `round(muv N)`, was `floor`) and the
   receiver-plane term runs on `tapUV − suv`. Programs regenerated (single 993 words and 225 slots, were 974 and 220; cascade
   1,755 words and 406 slots, was 398). The twin takes `legacy_floor=True` (CLI `--legacy-floor`) for captures of earlier
   builds. **Witness on real rasterized maps**: case (g), the far-plane scene with the box at eight
   sub-texel phases (k / 8 of cascade 1's 11.72-unit texel); over the eight frames (7,397 edge pixels)
   the shift, in 0.25-texel steps, at which the analytic shadow best matches the quad's `f ≥ 0.5` shadow
   is **(0, −0.25)** with 383 disagreements against 394 at zero (a bowl centred on zero: 455 / 394 / 430
   across u, 383 / 394 / 486 across v at −0.25 / 0 / +0.25), while the former rule evaluated on the same maps sits at
   (−0.5, −0.75), the grid's corner, with 823 disagreements at zero; asserted ≤ 0.25 and ≥ 0.5. One
   frame alone cannot show it: its straight edges sit at one phase of the grid, worth up to half a
   texel either way (single frames fit between (0, 0) and (0.5, −0.75)). Host: a D3D9-style rasterized
   edge at ten sub-texel positions, bias of the 0.5 crossing < 0.25 texel (round) and > 0.35 (floor).
   **Single-map records regenerated**: the apply script's synthetic map now follows the rasterizer's
   convention (texel a holds the sample at a / N; it was written at (a + 0.5) / N, which is why the
   floor rule had looked right there). `sun-shadow-apply`: edge disagreements 376 → 337, plane 278 →
   241, 0 beyond two texels as before, shadowed per frame 8,847 / 3,761 / 2,266 / 1,632 → 8,831 / 3,716 /
   2,260 / 1,639, worst code 0.998; `-wide`: 145 → 139, box faces beyond two texels 14 → 13. Live
   `shadow_apply`: unchanged (3,969 shadowed pixels; its caster covers every receiver).
2. **Stale extents bounded.** A stale entry records the revision it waits for and since when; its
   re-read is a priority read (front of the 32-slot queue, evicting the last ordinary read when full;
   the queue is read in order under the byte budget). Older than `extent_stale_frames = 8` (the same
   eight scene ends a failing read is given before it is abandoned: by then the buffer is rewritten
   every frame or unreadable) or abandoned, the previous extent is doubled about its centre
   (`verdict=inflated`) instead of trusted; an abandoned re-read is not queued again until the revision
   moves once more. `extent_refused=` is now per frame.
3. **Latch.** The sun latches when a second draw's sample agrees with a waiting one (value, register
   and program of the first); a lone or stray sample only waits (`unlatched=` on the sun line; frame 0
   of the scripts: `agree = routed − 1, unlatched = 1`). Comment corrected: measured per-node spread
   0.753°, gate 1.5°. Refusal detail `sun_relatched` distinct from `sun_changing`. The cascade apply
   skips with `sun` unless the frame's verdict is usable. `sun_register` is resolved for every pixel
   program at creation, unconditionally.
4. **Restore.** Both passes put back the declaration object when one was bound, else the FVF, else
   none (capture order declaration, then FVF); 80 restorations in the cascade script and the apply
   scripts' hostile-state compares unchanged.
5. **Centre precision.** `ShadowReplayBasis` carries the axes and the snapped centre as doubles and every
   row set (light rows, view rows, cascade rows, the box test) folds them in double before narrowing.
   Host, camera 70,000 units out: row-set error 1.1e-8 of cascade 0's NDC against 4.8e-6 for the float
   centre alone (0.0012 units there; up to 0.0078). F8 basis lines print the double centre (`%.12g`).
   Map depth error of `seam-ownership-shadow-replay-on` 1.170e-5 → 1.194e-5 (gate 1e-4), the other
   maps' unchanged; the seam readback still hands the fixture a float centre (its cameras are near the
   origin).
6. **Host counts** (`python3 -m unittest -v`, test methods): `test_shadow_cascades` 4,
   `test_shadow_replay_candidates` 10, `test_shadow_replay_depth` 11, `test_sun_shadow_apply` 17 (was
   16: 41 → 42 for the four shadow modules), `test_sun_share_lane` 11, `test_comparison_hotkeys` 5
   (58), `test_linear_material_live` 17, `test_motion_wrap_states` 4, `test_linear_cutout_contract` 3,
   `test_capture_bloom_lifetime`, `test_motion_hdr_scene`, `test_capture_device_creation` 1 each,
   `test_bloom_programs` 5: 90 in one invocation (the first round's "89" was the same set before the
   new test; I do not reproduce a count of 91 for the mock modules alone: they are 27, 32 with
   `test_bloom_programs`). Full discovery at the end of this round:
   `PYTHONPATH=verification/probe python3 -m unittest discover -s verification/analysis -p 'test_*.py'`:
   **2,121 tests, OK** (544 s).
7. F8 `shadow_replay_caster` lines gained `origin=` (the draw's object origin in world space) and print
   the program's sun at `%.9g`, so one capture gives (node position, direction) pairs.

**Open after the fix round.**
- *The sun is a positional light at finite distance.* The engine computes `normalize(light − node)` per
  mesh part on 16.16 fixed-point positions; |L| raw 1.5689e9 is ≈ 23,900 units if raw is 16.16, and the
  0.753° spread across run111's nodes is what such a distance gives. One latched direction then
  misplaces a shadow by about 1.3 % of its throw at station scale and far more in the 7,500 / 25,000
  cascades. Not confirmed from run111: its log has no per-draw constants or node positions (`draw`,
  `motion_input`, `object_context` carry neither), so the distance could not be triangulated; the new
  `origin=` / `sun=` pairs of the caster lines make the next F8 capture sufficient. Planned follow-up:
  the direction evaluated at each cascade's centre from the engine light position (hook-free poll,
  camera-and-lights.md).
- Stale extents: the inflated box is a heuristic (twice the previous extent); a range rewritten every
  frame with larger motion than that is still mis-admitted, and there is no whole-buffer bound. A
  priority read can evict an ordinary one every frame while more than 32 ranges are stale.
- The latch's first value is the first of two agreeing draws, not a mean: up to the 0.75° spread from
  the sector's central direction, fixed for the sector.
- Pancaking stays per vertex: a triangle crossing the near plane can lose the depth test over its far
  part to a caster truly behind it (the stored depth stays exact).
- The shift fit's v axis is weaker than u in this scene (flat valley along v in single frames); the
  eight-phase sum is what is asserted.

## Caster retention, stages 1 and 2 (2026-09-17)

Contract and what was built: [shadow-caster-retention.md](../architecture/shadow-caster-retention.md),
"Implemented". Default off (`--shadow-retention-census`, `--shadow-caster-retention`; cascades only).
Base a517042, bottle X3 (arm64, `FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`), not installed, not run in
the game. Every Wine command ran as `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py …`.

**Build.** `cmake --build build --clean-first -j8`: 70 objects, 0 warnings, DLL `acdd32c9…`;
`verification/probe/build_motion_output.sh` (seam DLL `da34a3f0…`, fixture `f59928ea…`, `-Werror`).
`verification/probe/check_no_x87.py build/d3d9.dll`: PASS, 77 roots, 520 reachable functions, 0
violations (the record hook runs on the audited draw path).

**Fixture** (`run_motion_output.py --dll … --seam … --fixture …` with the case names; new mode
`shadowretention`, script `verification/probe/motion_output_shadow_retention_inc.h`; records
`verification/results/bottle-X3/seam-ownership-shadow-retention-{live,census,off}-fixture.json`). One
1,461-frame script under the three settings of the DLL, the scripted camera 81,400 units from the
origin, two cascades (seam extents 8 / 400, 256² maps), synthetic nodes born in a synthetic lifetime
observer with the journal's semantics, age cap 640. The fixture asserts the store's levels and
counters through the seam and the COM reference counts of its own buffers and declaration by
`AddRef`/`Release`; the runner compares every sampled cascade map with the CPU twin of the submitted
draws plus, live only, the draws the script says must be kept, each placed by the camera of the frame
that recorded it.

| Setting | Fixture + runner checks | Compared frames (with kept draws) | Maps, worst depth error, coverage disagreements |
| --- | --- | --- | --- |
| live | 8,816 | 38 (28) | 76, 1.7e-6, 0 |
| census | 8,814 | 38 (0) | 76, 1.7e-6, 0 |
| off | 5,876 | 38 (0) | 76, 1.7e-6, 0 |

The three settings present byte-identical frames (one SHA-256 over the 1,461 `COLOR` hashes); with
the option off the log holds no `shadow_retention` line, no `retained=` and no `replayed_` field.

| Case | Result (live; the census takes the same decisions without references or replay) |
| --- | --- |
| a. camera turns away | both casters static from the ninth sighting; kept 3 frames inside the frustum unsubmitted, then 600 frames at yaw 120° + 0.5°/frame, maps equal to the twin on the 9 sampled frames; each buffer and the declaration at baseline + 1 throughout, baseline after retirement. Control (off / census): the blob is absent on the first culled frame |
| b. moving | a node whose rows change every frame never becomes static and leaves on its first unseen frame (`moving_dropped` +1, map = the static node alone); a static node moved while unseen shows once, at the new place, `reclassified` +1, drift 0.75 units |
| c. retired | journal entry: gone before the next replay, its buffer at baseline, the other node kept; 2,049 entries between drains with the node killed silently: `journal_overflow` +1, full revalidation, store empty, baseline |
| d. LOD swap | same serial, `lod` 0 → 1, another buffer: `lod_replaced` +1, records 2 → 2, map = the new mesh only on the swap frame, old buffer at baseline |
| e. shared mesh | two nodes, one buffer: two blobs, `refs_held` 2 (buffer + declaration), buffer at baseline + 1 for two records and still after one node retires, baseline after the second |
| f. buffer Lock | writable Lock of the held unseen buffer: dropped after 2 frames (bound 8), `buffer_changed` +1, baseline |
| g. release before retirement | the fixture releases its last reference; the next replay runs from the store's reference and matches the twin; with no retirement the orphan probe (capability `orphan_probe=1` on this runtime: 2 after `AddRef`, 1 after `Release` on a private managed buffer) drops the record after 3 frames, `buffer_orphaned` +1. Census: `buffer_gone` +1 after 4 frames |
| h. Reset | store empty, `refs_held` 0 and every count at baseline after `Reset`; then a Reset that really fails (`D3DERR_INVALIDCALL` 8876086c, a fixture-owned default-pool buffer alive): flushed all the same (`shadow_retention_flush reason=reset nodes=2 refs=3` twice), baseline, the second Reset succeeds; retention resumes on resubmission and matches the twin |
| i. capacity | 700 static nodes retained unseen + 324 live = 1,024; the 1,025th node: `evicted` +1, `refused` +0, 1,024 nodes; 600 retirements in one frame drained in that frame (599 found: one was the evicted node) with no overflow; 2,049 entries: overflow, revalidation, store empty, the shared mesh at baseline |
| j. excluded classes | one node per bit (`flags12c` 0x20, 0x200, 0x4000, 0x10000000; `flags130` 0x200) and one draw with `known=0`: drawn and replayed live, never in the store (`excluded_class`, `unscoped` counted), maps equal to the off control |
| k. age cap, sun | retained through 640 unseen frames, gone on the 641st (`age` +1, baseline); a persisting second sun direction re-latches on its 8th frame and the store is flushed that frame (`flush=sun`), nothing retained under the old sun; retention resumes under the new one and matches the twin |
| teardown | the script ends with two nodes retained and their buffers already released by the fixture: `shadow_retention_flush reason=teardown nodes=2 refs=3` precedes `device_destroy` (the device-Release hook flushes before its final-Release probe). Host model of the same hook in both alias models: `test_capture_bloom_lifetime`, 42 scenarios, 194 checks |
| l. precision twin | host, `test_shadow_retention`: 2,000 pairs of unrelated cameras at the run111 offset, 600-unit AABB, engine product in float32, recovery in double: worst 0.012 units against `eps` 0.05 |

F8 window (frames 10–17): `shadow_retention_caster` lines with `class=static unseen≥1`,
`shadow_replay_caster … retained=0` on live records and `retained=1` on the issued retained ones.

**Cost** (CPU wall time under Wine/FEX, `X3M_SHADOW_RETENTION_TIMING=1`; not game FPS). Record hook:
median 0.15 µs per recorded draw including its two counter reads (census 0.20). Scene end (`us`):
median 2.6 µs at the script's small stores, 92 µs median and 271 µs worst walk with 1,024 nodes /
1,024 records (700 unseen, box and cascade tests, 1/8 buffer checks, the probe slice); 416 µs the
worst frame of the run; 599 retirements 52 µs (journal 33 µs); overflow + full
revalidation of the store 162 µs. Replay of 1,402 issues (700 retained + 325 live, two cascades):
1.81 ms median, 1.29 µs per issue, the same as live issues (census / off, 1,402 live issues: 1.79 /
1.87 ms). No allocation after attach (the store is 1.77 MB, the live draw list 5,120 entries); no
device call per draw (a new record in live mode takes up to three `AddRef`); no lock added.

**Unchanged cases on the same binaries**: `seam-ownership-shadow-replay-on` 203, `-off` 99,
`-taa-…-on` 226, `-taa-…-off` 122, `-casters-20` 203, `-sun-programs` 205, `-cascades` 245,
`-cascades-casters-20` 245 (max depth error 4.07e-6, as recorded), `-wide` 203, `-far-refused` 203,
`seam-ownership-taa-camera-candidates-on` 216: all pass. Live (`run_sun_share_live.py`, cases
`shadow_apply`, `shadow_apply_cascades`, `original_lane`): pass; equal to the committed
`sun-share-live.json` except `apply_us_max` (timing).

**Host**: `test_shadow_retention` 6 (the store natively: 13 scenario groups incl. the index map
against `std::map`; the line parsers; the launcher options), the six mock modules and the
shadow/runner modules: 135 tests OK; the 16 other launcher modules: 214 OK. Mock updates:
`capture_bloom_lifetime_fixture.cpp` (retention seams of the Release hook + the orphan scenario),
`linear_material_live_fixture.cpp` (teardown and Reset seams).

**Open.**
- Not run in the game. The census run (station approach, 180° turns, a parked minute facing away,
  one gate jump, one load) has to settle `eps`, `age_cap`, `transit_survivors`, the moving share and
  `us` before stage 2 is trusted; `tools/analysis/shadow_retention.py <log>` prints the summary.
- Device loss (`flush=device` on a failed `Present`) is not reachable in the fixture; native Windows
  is cross-compiled only.
- The live path still takes its per-frame lease; the contract's reuse of the held references for
  seen draws is not built.
- Under a cascade's cap the retained records are cut nearest-first per cascade; a record capped out
  of cascade 0 can still be issued into the outer ones (one draw, fewer issues).
- A frame without a routed scene draw has no scene end and replays nothing (as before retention).

**After the merge with main c27e974 (positional sun).** Retained masks are taken against each
cascade's own current basis (its sun and grid anchor, as the transaction), the retained issue rows are
rebuilt for a cascade whose axes differ, and a sun source switch (point ↔ latch) flushes the store
(`flush=sun`). Rerun on the merged tree: clean build 0 warnings (DLL `4151b17b…`), no-x87 PASS (525
reachable, 0 violations); retention live / census / off 8,816 / 8,814 / 5,876 checks, 76 maps each,
worst depth error 1.7e-6, 0 coverage disagreements, presented frames identical; record hook still
0.15 µs per recorded draw, full-store scene end 86 µs median / 238 µs worst walk;
`-cascades` and `-cascades-casters-20` 278, the four `-cascades-poll-*` cases 281 each,
`-sun-programs` 205, `-on` / `-off` 203 / 99, `-wide` / `-far-refused` 203: all pass;
`run_sun_share_live.py` `shadow_apply`, `shadow_apply_cascades`, `original_lane`: pass, equal to the
record except `apply_us_max`. Host: the new per-cascade-sun scenario in `test_shadow_retention` (a
record outside cascade 1 under the shared sun and inside it under cascade 1's own direction) and the
affected modules, 100 tests OK. Not exercised through the DLL: retained records on a frame where the
cascades' directions really differ (the retention script runs on the latch source; the poll cases
retain nothing), and the source-switch flush.

**Review fix round (2026-09-17; the two reviews' items are listed in the contract note,
"Review fix round").** Binaries: seam DLL `b018f715…` and fixture `0c6535cb…` (the four records below
cite these; the seam cases load the seam DLL); production DLL `f7acf09c…` from the incremental build
the audit ran on, `54a43369…` from the final clean build of the same sources (0 warnings; the build is
not byte-reproducible). No-x87: PASS, 525 reachable, 0 violations. `run_motion_output.py --dll … --seam … --fixture …`, 15 cases, all exit 0:

| Case | Checks | Frames | Compared (with kept) | Maps, worst depth error, disagreements |
| --- | --- | --- | --- | --- |
| `seam-ownership-shadow-retention-live` | 9,743 | 1,535 | 39 (28) | 78, 1.7e-6, 0 |
| `…-census` | 9,740 | 1,535 | 39 (0) | 78, 1.7e-6, 0 |
| `…-off` | 6,629 | 1,535 | 39 (0) | 78, 1.7e-6, 0 |
| `…-live-poll` (positional sun, source switch at frame 1516) | 9,890 | 1,547 | 40 (29) | 80, 1.4e-5, 0 |

Live, census and off present byte-identical frames. New script coverage: case m (observer unavailable →
full revalidation, 2 nodes under `revalidate_context_lost`, `flush=observer`; a lost camera under an
overflow's revalidation → 2 more; `FlushAll` through the drain; the drain's epoch moved → `flush=epoch`;
the observer epoch as a key → a deferred `flush=epoch`; device loss through the seam → `flush=device`,
every count at baseline), `model_replaced` in case d, the capacity case with the scene-end reserve
(the filling frame evicts 8, the 1,025th node one more, nothing refused, 591 of 600 retirements found:
9 were the evicted nodes; `far_alternate_due_to_retained` 6 frames with 301 live × 2 = 602 issues under
the 640 budget and 716 retained nodes), and the poll case (`shadow_replay_sun_source` `point` at frame 0,
`latch unavailable` at the switch, `flush=sun` twice). Totals per run: `journal_overflow` 3, `evicted` 9,
`revalidate_context_lost` 4, `reclassified_after_unseen` 1, `refused` 0, `release_queue_full` 0,
`admitted_checked` 9,877.

Cost after the round (Wine/FEX): record hook unchanged at 0.15 µs per recorded draw (census 0.20); the
full-store scene end 491 µs median / 541 µs worst walk (716 retained records checked before issue every
frame at about 0.3 µs each, plus the 1,024-node walk), from 86 / 238 µs before the issue-time check;
599-retirement burst frame 129 µs (journal 32 µs); overflow + revalidation 162 µs; replay of 1,450
issues 1.62 ms median (1.1 µs per issue). Unchanged cases on the same binaries: `-cascades` and
`-cascades-casters-20` 278, the four `-cascades-poll-*` 281, `-sun-programs` 205, `-on`/`-off` 203/99,
`-wide`/`-far-refused` 203: pass. `run_sun_share_live.py` `shadow_apply`, `shadow_apply_cascades`,
`original_lane`: pass, equal to the record except `apply_us_max`. Host: the store driver gained the owed
reuse, lost-context, every-sighting reclassification, issue-time check and reserve-eviction scenarios;
`test_capture_bloom_lifetime` 42 scenarios / 194 checks; the affected modules 100 tests OK. Launcher:
`--shadow-retention-timing` (X3M_SHADOW_RETENTION_TIMING=1) reproduces the per-draw figure.

Open after the round: the issue-time check is the dominant scene-end cost at a full store (about
0.3 µs per retained record per frame, two registry lookups under the registry mutex); batching the
lookups per buffer, or a revision counter the registry publishes without a lock, would bring it back
towards the contract's 0.1 ms.

### Issue-time check batched per buffer (2026-09-17, worktree `agent-a18fd4d740a73398e`)

Design chosen: (a) one registry lookup per distinct buffer identity per scene end, records inherit
the view. `Store::end_scene` now takes `view(identity) -> BufferView`; `Store::buffer_view` is a
direct-mapped 1,024-slot per-walk cache (identity hash, walk serial; a collision re-queries) and
`buffer_verdict` makes the same allocation / generation / revision / pending-Lock compare per record
as before, in the core (host-testable). Not chosen: (b) a lock-free per-buffer revision has no stable
cell to publish (the registry keeps buffer metadata in D3D private data, read by copy) and would add
a store to the Lock hook; (c) event-driven dirty slots couple the ownership layer to the store's slot
table and also touch the Lock hook. (a) leaves the Lock hook and the draw path unchanged by
construction (no hook delta to measure), adds no locking, allocates nothing, and the option-off cost
is unchanged (the walk does not run). The correctness property holds unchanged: every lookup of a
walk precedes that walk's issue on the game thread, so a buffer re-Locked before the scene end is
seen by every record naming it; the next scene end looks every buffer up again (the cache is keyed
by the walk serial, not the frame number, so two scene ends of one frame never share a view). The
frame line gains `buffer_views` (lookups this frame); `admitted_checked` keeps counting records.

Evidence, both runs from fresh builds in this worktree on the unchanged fixture (`run_motion_output.py`
partial run of the four retention cases, Wine/FEX, bottle X3). Before (main `c0c76b1`, DLL `eba8ef82…`):
full-store scene end 512.8 µs median / 573.4 µs worst walk (live), 516.3 / 570.3 (census), 509.7 / 576.8
(live-poll). After (DLL `27878db3…f39e`, seam `3f80bde6…9060`, fixture `8cddae78…8add`; clean build, 0
compiler warnings; `check_no_x87.py` PASS, 526 reachable functions, 0 violations): 44.5 µs median /
104.0 µs worst walk (live), 52.6 / 111.3 (census), 43.4 / 97.4 (live-poll). Full-store frames: 716
records compared (`admitted_checked=716`) against 1 lookup (`buffer_views=1`, the shared mesh); the
worst walk is the first full-store frame (104 µs), the following ones 43–46 µs. Record hook per draw
0.20 µs median (unchanged); burst frame 143.6 → 55.0 µs; overflow + revalidation 172.9 µs (unchanged).
Twin results equal before and after: map `max_depth_error` 1.72e-06 (live) / 1.70e-06 (census, off) /
1.40e-05 (live-poll), 0 coverage disagreements, 78 / 78 / 78 / 80 maps; presented-frame
`color_sha256` `4b6e6748…` identical across live, census and off, the poll case's `446b5534…` equal to
before; `RETENTION_LOCK_DROP frames=1` in every case (the re-Locked buffer drops before the next issue);
orphan drop 4 / 1 / 2 / 4 frames and the failed-Reset result as before. Host:
`test_shadow_retention` gained the shared-buffer scenario (40 records, 2 lookups; one lookup drops all
40 on a rewrite; a second buffer's record survives); the eight affected modules 44 tests OK.

Open: the gain scales with sharing. Retained records on all-distinct buffers still cost one registry
lookup (about 0.3 µs, two map lookups under the registry mutex) per buffer per frame; a bulk
`get_buffer_lock_views` taking the mutex once would be the next step if in-game stores show that shape.


## Sun at finite distance: polled light position, per-cascade suns (2026-09-17)

Default off as the cascades are; without `--shadow-cascades`, and with the poll unavailable, every
record equals main's (below). Not installed, no game launch.

**Premise corrected.** The brief's hypothesis (light ≈ 24,000 units away, "if positions are 16.16")
is false: node and light positions are engine integers × the context scale 0.01, the same unit as
the proxy's camera position (three facts, two measured on run111:
[camera-and-lights.md](../reverse-engineering/camera-and-lights.md), "Position units"). The sun is
**1.57e7 units** away; the 0.753° per-node spread is sector-wide node spread, and the error it
causes is the *latch's*: the latch keeps the constant of an arbitrary node, up to 0.75° from the
direction at the camera (13 units of shadow displacement per 1,000 units of caster distance, 107
cascade-0 texels). That error is what this change removes; the orthographic residual is small (table).

**What was built.**
- `src/proxy/sun_light_poll.{h,cpp}`: hook-free poll of the brightest directional light node
  (`*0x00608518`, array `+0x5e8c`, exact-executable gate, `engine_memory::read` only, layout validated
  per poll: slot count 8, terminator, light bit on every entry). Statuses `disabled`,
  `executable_mismatch`, `slot_unreadable`, `null_context`, `unreadable`, `layout`, `no_directional`.
- `src/proxy/shadow_replay_sun_point.h` (`PointSun`, pure): light = integers × 0.01; up to 8 routed
  draws per frame whose `LightDir_Dir0` agrees with the latch are cross-checked,
  `normalize(light − draw origin)` against the constant within **0.1°** (engine quantisation 0.0008°);
  the frame's source is decided once (first box test or scene end) and holds for the frame; reasons
  counted: `unavailable`, `no_light`, `camera`, `near` (light inside the cascades' volume:
  distance ≤ depth-towards-light + largest extent), `unchecked`, `disagrees`, `cooldown` (120 frames
  after a disagreement), `off`. A source change voids retained maps. The latch still gates the frame
  (`no_sun`, `sun_changing`) and is the portable source (native Windows without the verified EXE:
  `executable_mismatch`, behaviour = main).
- Per-cascade direction: `normalize(light − (camera + forward × forward_offset))`, the unsnapped
  centre (the snapped one differs by < 1 texel, 1e-8 rad). **Swim bound:** a cascade's direction is
  held bit for bit while the ideal stays within `1 / size` rad, so between re-derivations the basis is
  exactly as stable under texel snapping as main's; a re-derivation (camera travel of
  distance / size across the light: 3,830 units at 4096 texels) turns the basis by ≤ `2 / size` rad:
  a shadow whose caster is D units light-ward of its receiver moves ≤ 2 D / size, ≤ 1 texel of that
  cascade for D ≤ its half-extent, once per 3,830 units of travel. A re-deriving cascade adopts the
  next smaller cascade's held direction when within its own threshold, so camera-centred cascades
  normally share one direction bit for bit: `ShadowCascadeBounds::shared` keeps the one-transform
  box test and one draw-rows product per draw; otherwise per-cascade rows
  (`shadow_cascade_bounds_suns`, `shadow_replay_axes_equal`). The apply already took per-cascade rows.
- Log (every cascade frame in this commit; F8 frames and events only, and renamed selection fields, since the fix round below): `shadow_replay_sun_point … source reason poll light
  native distance checks disagreements agreement_deg rederived candidates directional luma
  second_luma flags record_scale poll_us frames_point dir<i>`; `shadow_replay_sun_source` on a change.
  `record_scale` is the engine's own D3DLIGHT position / integer: 0.01 expected in flight.

**Orthographic-per-cascade residual against the true point light**, D = 1.5689e7 units, 4096² maps.
Direction error at the cascade edge = atan(E / D); shadow displacement = h · E / D for a caster h
units light-ward of the receiver; in texels it is h · size / (2 D), the same for every cascade: one
texel at h = 7,660 units (5,420 at the corner).

| Extent | texel | edge (corner) angle | h = extent | h = 50,000 (depth limit) |
| --- | --- | --- | --- | --- |
| 250 | 0.122 | 0.0009° (0.0013°) | 0.004 u = 0.03 tx | 0.80 u = 6.5 tx (corner 9.2) |
| 1,500 | 0.732 | 0.0055° (0.0077°) | 0.14 u = 0.20 tx | 4.8 u = 6.5 tx |
| 7,500 | 3.66 | 0.027° (0.039°) | 3.6 u = 0.98 tx | 23.9 u = 6.5 tx |
| 25,000 | 12.2 | 0.091° (0.129°) | 39.8 u = 3.3 tx | 79.7 u = 6.5 tx |

**Recommendation:** no perspective (point-light) projection for the far cascades at the measured
distance: the worst case is 80 units (16 m) on a 5 km cascade for a caster 10 km up-light, 0.13° of
direction, an eighth of what the latch alone was wrong by. A sector whose light lies within 75,000
units is refused as `near` and counted; if flight logs show that reason (or `distance` below ~1e6),
a perspective far cascade becomes the fix. Inference, not measured: that every sector's sun is as
far as run 39's; the first flight's `distance` field settles it.

**Evidence** (build `547eb606…dada`, clean, 0 warnings; `check_no_x87.py`: PASS, 520 reachable
functions, 0 violations (the poll's QPC delta stays an integer on the draw path); seam
`27b4c22a…d3ca`, fixture `4c31522a…b26c`; bottle X3):
- `X3M_FIXTURE_BOTTLE=X3 … wine_lock.py … run_motion_output.py --dll … --seam … --fixture …` with 19
  cases: 19 / 19 pass (`verification/results/bottle-X3/motion-output-partial.json`). The 16 cases of
  main's record are equal field for field (timings, hashes and paths aside) except: the two cascade
  cases gain `sun_point` (`no_seam`: 8 frames `latch / unavailable / executable_mismatch`) and 33
  checks; `sun-shadow-apply-cascades` gains frame 20 (case h), `point_light` and 240 checks, and its
  total `beyond_one_texel` 0 → 25 is case h's region C alone (the stated residual; frames 0–19: 0).
  `sun-shadow-apply`, `-wide`, single-map and caster-count cases: identical.
- Case h (`sun-shadow-apply-cascades`, production passes + production `PointSun`): light 24,000 units
  away (the brief's hypothetical, so the effect is measurable on 256² maps), 75° to the side, 30°
  elevation; cascade 0's sun differs from cascade 1's (4.9e-3 rad > 1/256), cascade 2 adopts
  cascade 1's: per-cascade bounds rows, draw rows and apply rows on the GPU. Against the analytic
  **point-light** shadow: A (cascade 0, lateral 185, h 61): 0 pixels beyond one texel (predicted
  residual 0.24 tx); B (cascade 1, the nearest to its centre it owns, lateral 546, h 81): 0
  (0.16 tx); C (cascade 1's edge, lateral 1,162, h 500): 25 pixels beyond one texel, predicted
  h r / D = 24.2 units = 2.07 tx. Against the parallel shadow of each cascade's own sun: 0 in all
  three (the maps are right for what they are). In-fixture: unchecked → refused; 3 checks
  < 0.01°; carried validation holds bit for bit; `near`, `disagrees` → `cooldown`, `no_light`.
- Poll through the DLL (`…-cascades-poll-{agree,null,disagree}`, the fixture's own context block
  behind the seam; 4 candidates: point light, sun, fill, forced-directional at the origin):
  agree: 8 / 8 frames `point`, luma 186580 / second 128000, `record_scale` 0.01, 3–4 checks per
  frame, worst agreement 4e-6°, every direction within 1 / size of normalize(light − camera), every
  replayed map's forward = −dir; 4 re-derivations in the record: both cascades on frame 0 (the first
  derivation) and both on frame 7 (the script's camera jump), held bit for bit on frames 1–6; the
  sun-changing frame carried without checks; null: 8 × `unavailable / null_context`;
  disagree (light 35° away): frame 0 `disagrees` (every check), 7 × `cooldown`; both fall back to
  maps whose bases equal the latch's. Poll cost: median 5.7 µs, first frame 74 µs (region queries).
- Box test per draw (2M rounds, 4 cascades): shared suns 45.0 ns (single-map verdict 45.6 ns, main:
  the same path), per-cascade suns 137 ns (only on frames where holds differ).
- Live: `run_sun_share_live.py`: 21 / 21, record equal to main's (temp paths aside).
- Host: `test_shadow_cascades` (PointSun at the run-39 distance: latch error 0.42° at run111's node,
  hold 3,000 / re-derive 5,000 units, ≤ 2 / size turn, every reason, per-cascade masks = each
  cascade's one-sun mask on 28 boxes), `test_sun_shadow_apply`, `test_shadow_replay_depth`,
  `test_shadow_replay_candidates` and the six mock-drift modules: 69 tests OK.

**Not verified / open.** Nothing was read from a live process: the array's content while the sector
view is current, whether `0x00420260`'s nodes enter it, and the engine's tie order are flight
questions the line's `candidates / directional / luma / second_luma / agreement_deg` answer in one
run. The twin's edge offsets use cascade 0's right/up for every cascade (exact to the 0.3° between
suns). `X3M_SHADOW_SUN_POLL=0` disables the poll (launcher flag: fix round below).

### Fix round after review (2026-09-17, second commit)

Build `4f61c7b8…b0a2` (clean, 0 warnings; `check_no_x87.py` PASS, 0 violations), seam `5313c4ae…5795`,
fixture `1a9cc114…dab4`.

1. **Grid anchor.** The texel grid was anchored at the world origin, so a re-derivation (turn ≤ 2 / size)
   moved it by |centre| × turn under the cascade (49 units at 1e5 units out): a new sub-texel phase for
   every edge, which the first bound did not count. `shadow_replay_basis` takes an optional anchor the
   grid passes through (null = the origin law, bit-identical: every one-sun record is unchanged);
   `PointSun` holds one per cascade and a re-derivation moves it to the OLD grid's point beside the
   current centre (the old basis' snapped centre). **Restated swim bound:** between re-derivations none
   (as main); at a re-derivation the grid phase at the centre is preserved (< 1e-3 texel) and a texel r
   units from the centre moves r × turn, ≤ 1 texel at the cascade's edge; separately a shadow whose caster
   is D units light-ward moves ≤ 2 D / size. A retained far map keeps its anchor implicitly (its basis
   stores the snapped centre and axes its rows are built from). Measured: host driver, camera 1e5 units
   out, 5,000-unit move: phase anchored 0.000000 texel, origin-anchored 0.144, edge shift 0.31 texel
   (≤ 1 asserted); through the DLL (`…-poll-agree`, the frame-7 re-derivation, from the logged anchors and
   the old direction's axes): 5.7e-5 texel (< 1e-2 asserted, ≥ 2 samples required).
2. **Selection rule.** The poll's result is now the engine's Dir-slot rule (`flags & 0x800000` or range
   `+0x158 > 0x256250`; score = rounded luma + 0x300 when directional); the admission rule
   (`(flags & 4) && !(flags & 0x400010)`) is evaluated beside it, is the result only while the engine rule
   admits nothing (the lazy flag), and both winners are on the line: `rule`, `rules_agree`,
   `admission_native`, `slot_admitted`, `score`, `second_score`. Not reproduced: a long-range point
   light's per-node distance term (its score here is an upper bound, below any directional light's).
   Fixture block: score 955 / second 896, `rule=engine rules_agree=1`.
3. **Hysteresis.** Once validated, a frame whose poll cannot vouch for itself yet (changed position before
   a checkable draw, or no poll) stays `point` on the last validated position for up to 30 consecutive
   frames (`carried` on the line); only `unavailable` / `no_light`, `near` and `disagrees` (→ `cooldown`)
   switch. A camera-less frame no longer drops the held directions. Host: 30 carried frames bit for bit,
   then `unchecked`; the production order (box test decides `point`, a later draw disagrees: the frame
   finishes `point`, then exactly 120 `cooldown` frames, asserted frame by frame, then `point`).
4. **Refusals through the DLL** (`…-poll-refusals`, one defect per frame on the fixture's block): slot
   count 7, no terminator, an entry without the light bit → `layout` ×3; node pointer 0x10 →
   `unreadable`; point lights only → `no_directional` (reason `no_light`); null context ×3; every frame
   `latch`, maps equal the latch's, events `unavailable → no_light → unavailable`.
5. **Cost.** `QueryPerformanceFrequency` cached (`qpc_frequency_`); the `shadow_replay_sun_point` line is
   written on F8 frames, source changes and re-derivations only, plus one
   `shadow_replay_sun_point_summary` per 300 frames (reason counters); the seam build writes every frame
   for its runner. Poll median 6.2 µs; box test 45.0 ns shared / 137 ns per-cascade suns (unchanged).
6. **Docs / launcher.** The read contract cites
   [voice-startup-sequence.md](../reverse-engineering/voice-startup-sequence.md) §1 (single-threaded game;
   the draws run on the thread that calls `0x0047c640`) as the no-tearing basis. `tools/manage.py
   --shadow-sun-poll on|off` (default on with `--shadow-cascades`, requires it; `X3M_SHADOW_SUN_POLL`
   always written, `0` without cascades so an inherited value cannot leak); launcher tests added.
7. `motion-output-partial.txt` and `motion-output-wine.log` are untracked and ignored; the JSON is the record.

Evidence: `run_motion_output.py` (retained binaries) 20 cases: 20 / 20 pass; main's 16 records equal as
before (case h: 22 pixels beyond one texel in region C, predicted 24.2 units = 2.06 tx; A and B 0; all 0
against their own parallel shadow). `run_sun_share_live.py`: 21 / 21, record equal (temp paths aside).
Host: the ten affected modules 69 tests OK; the sixteen other launcher-option modules 214 tests OK.

## 2026-09-17 At-rest sun-shadow A/B (Ctrl+Shift+F12)

Run 39 needs the cascades' GPU fill/clear cost, and no GPU timer query works on
this backend; the instrument is the `frame_end` median with the shadows on
versus off at rest. The key, the gate and the log contract are in
[comparison-hotkeys.md](../architecture/comparison-hotkeys.md), "Sun shadows at
rest": one boolean tested once at the scene end removes the replay transaction
and the apply quad, both edges void every retained basis, the press is polled
only with `--sun-shadow-apply`, and each press logs `sun_shadow_toggle device=
state= frame=`. The per-frame `shadow_replay_depth` line gained
`shadow_toggle=` (optional in `shadow_replay_depth.py`, so older logs parse
unchanged).

- Host: `test_comparison_hotkeys` 6 tests OK, including the new source/gate case
  and the controls fixture at 12,231 checks (release and ASan/UBSan), 0
  failures. The six mock-drift modules (`test_linear_material_live`,
  `test_motion_wrap_states`, `test_capture_bloom_lifetime`,
  `test_motion_hdr_scene`, `test_linear_cutout_contract`,
  `test_capture_device_creation`) 27 tests OK.
- Clean CMake build: 0 warnings, `build/d3d9.dll` sha256 `33479b74…c75ec8`
  (the fixture runner's own rebuild of the same sources, `6f7eb3bd…a89df5`,
  audits identically); `check_no_x87.py`: 526 reachable functions, no violations.
  `build_motion_output.sh` (strict `-Wall -Wextra -Werror` seam compile): clean,
  the new `x3m_sun_shadow_fixture_toggle` export present.
- Fixture (`X3M_FIXTURE_BOTTLE=X3`, bottle X3): new case
  `seam-ownership-shadow-replay-cascades-toggle` (the cascade script with the
  seam pressed at the boundary of frame 2 off and frame 6 on), 277 checks,
  exit 0. Frames 2-5 carry no `shadow_replay_depth` line at all and both
  cascades read back invalid (the far basis of frame 0 is voided by the press,
  never republished); the Reset of frame 4 leaves the maps released while the
  A/B is off; frame 6 replays both cascades (`draws=[2,4]`, `far_replayed=1`,
  `far_frame=6`: re-replayed, not reused) and recreates the targets
  (`allocations=2` at frame 6 instead of 4); every replayed map equals the CPU
  twin, `max_depth_error` 1.83e-06. The two `sun_shadow_toggle` lines and the
  four `shadow_toggle=1` replay lines are the only new trace lines. All eight
  presented frames are byte-identical to `seam-ownership-shadow-replay-cascades`.
- Records equal: `seam-ownership-shadow-replay-cascades` 278 checks,
  `max_depth_error` 4.0742862848497374e-06; `seam-ownership-shadow-retention-live`
  9,743 checks, 1,535 frames, 39 compared / 28 retained-compared,
  `max_depth_error` 1.7169477474210382e-06 — all identical to the recorded runs.
- Limit: no fixture case requests `--sun-shadow-apply` through the DLL (the
  apply needs the sun lane, which the replay script does not run), so the
  quad's half of the gate is covered by source and the host case only; the
  absence of `sun_shadow_apply_frame` lines while off is not measured in game
  yet. Native Windows behavior is unverified as elsewhere.

### Review fixes (three low findings on the A/B)

1. The ON edge now demands a full replay (`sun_shadow_force_replay_`, consumed
   by the transaction that runs): the frame back on replays every cascade with
   casters whatever `shadow_cascade_replays` would say, because the press voided
   the far map's retained basis. 2. `sun_shadow_toggle` returns -1 and changes
   nothing on a device with neither `sun_apply_requested_` nor
   `depth_replay_requested_`, logging `accepted=0` like an unrequested
   ambient-occlusion press. 3. The single-map path publishes nothing while off:
   `invalidate_retained()` also clears the pass's view rows, the toggle clears
   `depth_basis_`, the off scene end no longer stamps `depth_replayed_frame_`,
   and the F8 map dump is gated on a map this frame actually published, so an
   F8 while off writes no map beside its `valid=0` basis line.

- Host: `test_comparison_hotkeys` 6 OK (controls fixture 12,231 checks, release
  and ASan/UBSan), `test_shadow_replay_depth` 12 OK. Clean build 0 warnings,
  `build/d3d9.dll` sha256 `8c50e2ae…152fa9`, `check_no_x87.py` 526 reachable,
  no violations; seam build clean.
- `seam-ownership-shadow-replay-cascades-toggle` (presses 1 off, 3 on, 4 off,
  6 on; the Reset falls in the second off window): 283 checks, exit 0. Off
  frames 1-2 and 4-5 carry no replay line and read back invalid; frame 3 — odd,
  6 issues against budget 5, so the budget rule alone would skip the far map —
  replays both cascades (`draws=[2,4]`, `far_frame=3`), as does frame 6 after
  the Reset (targets recreated there, `allocations=2`).
- New `seam-ownership-shadow-replay-toggle-single` (single map, press 2 off /
  3 on, F8 over both): 214 checks, exit 0. Frame 2 off: `valid=0` with the
  stale `replayed_frame=1` and **no** `shadow_replay_map_readback`; frame 3 on:
  `valid=1` and its map dumped. Both `sun_shadow_toggle` lines `accepted=1`.
- Records equal: `seam-ownership-shadow-replay-cascades` 278 checks /
  4.0742862848497374e-06; `seam-ownership-shadow-retention-live` 9,743 checks /
  1,535 frames / 39 compared / 28 retained / 1.7169477474210382e-06. All eight
  presented frames of both toggle cases are byte-identical to the plain cascade
  case.

## Run 39 A (run115)

Capture `/private/tmp/x3-bottleX3-run115` (137 non-log files, `session-20260917-223012-212.log`, 204,674,994 bytes / 2,385,012 lines), candidate cc966fb2 from 7492137, cascades 250/1500/7500/25000 at 4096/4096/4096/2048, `--shadow-sun-poll on --shadow-retention-census --sun-shadow-apply`. Read via `tools/analysis/shadow_replay_candidates.py`, `tools/analysis/shadow_retention.py`, `verification/probe/shadow_replay_depth.py` and targeted grep/Python (log never read whole).

1. **Cascades** (26,172 `shadow_replay_depth` frame lines, all 4-cascade): draws median/max — c0 17/51, c1 18/51, c2 25/67, c3 66/170; nonzero-draws fraction 0.9948 (c0-2) / 0.9955 (c3); no `capped` frames anywhere (`shadow_replay_candidates.py` capped_total = [0,0,0,0]). `far_replayed` fraction 0.9955. `us` vs total draws (replayed frames, n=26,054): slope 1.194 µs/draw, intercept 33.8 µs (≈4096² baseline). `sun_shadow_apply_frame`: 26,172 lines, applied 26,054 (99.55%), `skip_reason` counts `none`=26,054, `replay`=102, `sun`=16 (no other refusal reasons).
2. **Sun**: `shadow_replay_sun` verdict counts (27,091 lines) — `sampled`=27,075, `changing`=14, `relatched`=2 (matches `shadow_retention` totals `sun_relatch`=2, `cam_jump`=2). `shadow_replay_sun_point` (165 sampled rows, one `reason=point` throughout — no fallback reason ever fires): `agreement_deg` median 0.000751/max 0.000923; `distance` median 15,674,932/max 15,840,616; `candidates` median 4/max 5; `directional` median 1/max 1, **never** ≥16 — the 16 forced-directional-node path is not exercised this session.
3. **Toggle A/B**: `sun_shadow_toggle device=1 state=0 frame=8193` (off), `state=1 frame=9112` (on) — only 2 events, not the 4 implied by "twice"; log shows one off/on cycle. `frame_end` (300-frame stride) around it: pre-off window 7800→8100 dt_ms=3192 (10.64 ms/frame); off windows 8100→8400=2885 (9.62), 8400→8700=2794 (9.31), 8700→9000=2910 (9.70); post-on 9000→9300=3553 (11.84, straddles the 9112 edge), 9300→9600=3845 (12.82). Coarse read: off ≈9.3-9.7 ms/frame, on ≈10.6-12.8 ms/frame → shadows (replay+apply+clears) cost roughly 1-3 ms/frame at rest; the 300-frame stride is too coarse to isolate a clean single window on each side.
4. **Retention census** (27,091 `shadow_retention_frame` lines via `shadow_retention.py`): peak levels nodes_live/records/static/moving = 55/195/66/55, `age_max` 7200; totals `new_nodes`=545, `retired`=10, `journal_overflow`=5, `buffer_gone`=0, `buffer_orphaned`=0, `revalidate_context_lost`=0, `release_queue_full`=0 (no journal/buffer failure); `flushes.sun`=2, all other flush reasons 0. `resight` age-cap calibration: `<60`=16 moved/0 same, `<600`=6 moved/1 same/1 changed, `<3600`=16 moved/4 same/2 changed, `<14400`=19 moved/0 same — `age_cap_below_bucket`=`<60` (negligible same/changed already below 60 frames). Gate jump (frame 26029, `cam_jump=1`) and save load (frame 26897, `cam_jump=1`) each followed 7 frames later by `flush=sun, sun_relatch=1` (26036, 26904): at relatch `nodes_live`/`records` drop to 0 and `transit_survivors=0` — positions are **not** carried across, fully re-derived (`live_c3` rebuilds 18→128 over the following frames).
5. **Periodicity**: no ~30 s-period signal found in the sampled `frame_end` stride, `taa` lines, `media_cue_window` (94 lines, one per ~300 frames matching the frame_end stride, not a distinct 30 s cadence), or retention flushes — the only flushes are the 2 sun-relatch events above, not periodic. This session's coarse sampling (frame_end/media_cue every 300 frames) cannot rule out a sub-300-frame periodic stutter; a finer per-frame `frame_end` trace would be needed to confirm or refute the run-38 flicker here.
6. **Readback basename rejection**: `shadow_replay_map_readback` appears 96 times (4 cascades × 24 captured frames: 11954-11961, 13985-13992, 17558-17565), e.g. `file=shadow_map0_1_11954.r32f`. Running `tools/analysis/snapshot_x3_run.py` `references()` against this log directly gives **96** issues, all `shadow_replay_map_readback: ignored invalid record (unsafe or unexpected readback basename)` — not the ×7 stated in the brief; that number is not reproduced from this log and is left as a discrepancy. Cause (`tools/analysis/snapshot_x3_run.py:37,118-124`): `READBACKS['shadow_replay_map_readback'] = ('shadow_map', ('r32f',))` and the basename regex is `re.escape(prefix) + r'_(\d+)_(\d+)\.ext'`, i.e. it requires exactly `shadow_map_<device>_<frame>.r32f`. The actual per-cascade names are `shadow_map<k>_<device>_<frame>.r32f` (k=0..3), which fail the regex because the cascade digit sits directly after `shadow_map` with no separating underscore. Fix direction: extend the pattern (or add per-cascade prefixes `shadow_map0`..`shadow_map3`) to admit the cascade index, not widen the regex generally.

Files changed: `docs/verification/directional-shadows.md` (this section only).

## Caster pool control for wide far cascades (2026-09-18, worktree `agent-a46f2892f9578bec0`)

Contract: [../architecture/shadow-cascade-extents.md](../architecture/shadow-cascade-extents.md),
"Caster pool control" (`--shadow-cascade-static-from`, `--shadow-cascade-drop-order importance`,
`--shadow-cascade-records`; set H's blocker lifted when they are on). Base db4a509 merged. Default
off; every default reproduces main byte for byte (same records, drops and log lines). Not installed,
not run in the game. Every Wine command ran as `X3M_FIXTURE_BOTTLE=X3 python3
verification/probe/wine_lock.py …`.

**Build.** `cmake --build build --clean-first -j8`: 0 warnings, DLL `a315adf507ab…`;
`verification/probe/build_motion_output.sh` (`-Werror`): seam `c3a1f68af083…`, fixture
`4664fc55003a…`. `check_no_x87.py build/d3d9.dll`: PASS, 77 roots, 527 reachable functions, 0
violations (a first build had 3: `std::sqrt(float)` in the projected size pulled libm's x87 `sqrtf`
onto the draw path; replaced by `sqrt_sd`).

**Fixture** (new mode `shadowpool`, `verification/probe/motion_output_shadow_pool_inc.h`; records
`verification/results/bottle-X3/seam-ownership-shadow-pool-*-fixture.json`; two cascades of 8 / 40
units, 256² maps, the retention script's world-placed camera and synthetic nodes, an anchor node of
class 0x20 first every frame).
- (a) `…-pool-static-{off,census,live}` (`X3M_SHADOW_CASCADE_STATIC_FROM=1`), 14 frames, 165 / 181 / 181
  checks: the moving node (1/16 row unit per frame = 0.078 u > eps 0.05) shadows cascade 0 on every
  frame and cascade 1 never; the static node and the anchor shadow both (from frame 1 by the ring;
  with a store on, the store's verdict holds the static node out until its eight verified sightings,
  frame 9, the anchor's class being excluded from the store). Per frame `static_only_refused1` = 3, 1,
  1 … (off) and 3, 2 … 2, 1 … (store); `class_store / class_ring / class_miss` totals 0 / 39 / 3
  (off) and 26 / 13 / 3 (census, live). 27 maps per case against the CPU twin of the kept set: 615
  covered texels, 0 coverage disagreements, max depth error 3.8e-5; the three settings present
  byte-identical frames (`color_sha256 d5e47b25…`).
- (b) `…-pool-importance` (caps 16 / 4, `X3M_SHADOW_CASCADE_DROP_ORDER=importance`), 8 frames, 99
  checks: seven scaled casters (1.5 … 0.13) plus the anchor at one distance, the submission order
  rotated every frame. Cascade 1 keeps the four largest on every sized frame (frame 0, no extents:
  the four lowest serials) with `c1=4 capped1=4`, the 16 maps equal to the twin of exactly that set
  (11,777 covered texels, 0 disagreements, max depth error 3.6e-5); `dropped_min_size1` = 1.615 on
  every sized frame (identical: the selection is a function of the casters, not of the order);
  `select_us` median 1.0 (max 174.7 on the first frame).
- (c) `…-pool-records` (`X3M_SHADOW_CASCADE_RECORDS=1024,4096`, caps 1024 / 4095, importance), 6
  frames, 53 checks: 4,095 nodes on one mesh inside cascade 1 only plus the anchor: every frame
  `routed=4096 leased=4095 overflow=0 c0=1 c1=4095 capped1=1 capped=1` (the farthest node, size
  0.0298, dropped), issues 4,096 over the budget 640 so `draws1` = 4095 on even frames and 0 on odd
  ones with the far basis retained, `draws0` = 1 every frame; replay median 2.73 ms for 4,096 issues
  (0.67 µs per issue); `select_us` median 24.9 at 4,096 candidates (max 227.8, frame 0).
- Bench (`sun-shadow-apply-cascades`, `SUNAPPLY_BOUNDS_BENCH`, 2 M rounds): single verdict 45.7 ns,
  four-cascade mask 39.0 ns, the mask with the projected size 45.8 ns (+6.8 ns per draw), the static
  classification (world rows + ring test) 41.2 ns per draw; the scene-end selection over a full
  4,096-record list 15.5 µs (cap 2,048) and 13.5 µs (cap 4,095), 200 rounds each.

**Host.** `test_shadow_cascades` (driver: pool policy ranges and bounds, projected size ordering, the
selection identical under eight rotations, the serial tie-break, compaction, the ring's miss /
static / moving / eviction; launcher: the three options, ranges, absence by default, no leak of an
inherited value), `test_shadow_replay_candidates` (the two optional tails, malformed forms, the
summary), `test_shadow_replay_depth`, `test_shadow_retention`: 34 tests OK.
`tools/analysis/shadow_replay_candidates.py` parses `static_only_refused<i>= class_store= class_ring=
class_miss=` and `dropped_min_size<i>= select_us=`.

**Defaults byte-identical (regression, retained binaries, 13 cases, all exit 0).**
`seam-ownership-shadow-replay-{on,off}` 203 / 99 checks, `…-cascades` 278, `…-cascades-casters-20` 278,
`…-cascades-toggle` 277, `…-cascades-poll-{agree,null,disagree,refusals}` 281 each: the check counts
and the per-frame records of the run-39 candidate record (`verification/results/run39-candidate-build.json`,
`scoped_results.motion_output_source_parity`), map twins 40,693 / 665,266 / 20,665 covered texels
with max depth error ≤ 1.2e-5. `seam-ownership-shadow-retention-{live,census,off,live-poll}` 9743 /
9740 / 6629 / 9890 checks: the committed fixture records are identical apart from the per-draw
timing fields (`draw_us_per_call`). `sun-shadow-apply-cascades` 3292 checks (2891 + the 401 checks
of the extended bench), the analytic and map records identical apart from `replay_us`. Records:
`verification/results/bottle-X3/motion-output-partial.json` and the `*-fixture.json` beside it.

**Not verified.** Nothing ran in the game: the classification ring's miss rate at a busy station
(`class_miss=`) and the importance order's `dropped_min_size3=` under set H are run-39 A2 questions;
the rows the ring compares come from the frame's camera latch, so a frame without a valid latch
classifies everything as moving (counted `class_miss`). Native Windows: source only.

### Large-caster admission and the review fix round (2026-09-18, second commit)

Build `f8bbcb717b9d…` (0 warnings), seam `9b68f8463403…`, fixture `01c2daa9b2b6…`; `check_no_x87.py`:
PASS, 527 reachable functions, 0 violations. `--shadow-cascade-large-min L`
(`X3M_SHADOW_CASCADE_LARGE_MIN`, default 0): a moving draw enters a static-only cascade when its world
AABB extent (the largest side of the box the mask test built, `shadow_cascade_bounds_mask(...,
&extent)`) is `≥ L`, counted `large_admitted<i>=`. **L from the game:** the run-115 F8 census
(`/tmp/x3-bottleX3-run115`, `shadow_retention_caster half=` per node handle and model, 54 nodes over
the two F8 frames; run 111 logs no extents) gives, as 2 × max half: static parts 100–610 u (13 in
100–250, 18 in 250–500, one at 609) and one 38,269-u station hull; moving parts 15–467 u (fighter
parts and turrets; the 15-u ones are model `4a88` with 1,727 primitives, six of them) and one
29,939-u moving hull (model `53b8`, 3,874 primitives). Nothing lies between 610 and 29,939, so any
L in 1,000–10,000 separates this sample; **recommended L = 1,500** from the class scale (M6 ≈ 900 u
refused, M7 ≈ 3,500 u admitted; the extent is per mesh part, so an M7's turrets stay refused while
its hull passes). Two F8 frames of one run: run 39 A2's `large_admitted3=` against
`static_only_refused3=` is the calibration.

Review fixes (30a059b): (1) the ring anchors on the first sighting and re-anchors only on a
beyond-eps move, so a slow drifter is reclassified when its drift reaches eps (host: 0.02-unit
steps, moving on the third); (2) the ring is sized to the record list (two entries per record,
2,048 → 8,192 at 4,096 records) and `class_miss<i>=` counts per cascade; store dependency and limits
in the contract; (3) hysteresis at the cap boundary (a caster kept last frame stays while its size
≥ 0.8 × the cutoff, bounded by the cap; host: the a/b pair holds at 0.9, yields at 0.79, retakes at
1.3); (4) `*projected` / `*extent` are 0 on an unknown mask in both paths; (5) `static_from ≥ count`
(and 0) refused by launcher and DLL, the mode line logs `static_from=none`; (6) `select_us` from
the cached `qpc_frequency_` inside a LastError envelope.

**Fixture, retained binaries, all exit 0** (`verification/results/bottle-X3/*-fixture.json`):
`…-pool-static-{off,census,live}` 165 / 181 / 181 checks with `X3M_SHADOW_CASCADE_LARGE_MIN=100`:
the new 200-u moving sliver W is admitted to cascade 1 from frame 1 (`large_admitted1=1` on 13
frames, `class_miss1=4` on frame 0 only), the 1-u mover never, S and the anchor as before; 27 maps
each equal to the twin of the kept sets (10,747 covered texels, 0 disagreements, max depth error
4.4e-5); `…-pool-static-strict` (no L) 165 checks refuses W on every frame (`large_admitted1=0`,
9,399 texels); the four present byte-identical frames (`d5e47b25…`). `…-pool-importance` 99 checks:
ids 3 and 4 share a scale, 3 sits at the boundary and is 0.02 row units farther on even frames;
the kept set is {3, 5, 6, 7} on every sized frame (hysteresis holds 3; without it the plain order
would take 4 on even frames), 16 maps equal to the twin (11,781 texels, 0 disagreements),
`dropped_min_size1` = 2.136 identical on every frame; `select_us` median 5.0. `…-pool-records` 53
checks unchanged (`c1=4095 capped1=1`, far cascade on even frames), `select_us` median 71.5 at
4,096 (the kept-table refill and 4,096 inserts add about 45 µs to the 25 µs selection). Bench: mask
39.5 ns, mask + size 45.2 ns, classification 39.3 ns/draw, selection at 4,096 records 15.5 / 13.0 µs.
Regression: `…-shadow-replay-cascades` 278, `…-casters-20` 278, `…-toggle` 277, `…-poll-agree` 281,
`…-retention-live` 9743, `…-retention-off` 6629: unchanged. Host: the four shadow modules 34 OK, the
six mock-drift modules 27 OK.

**Open (measured, unexplained).** `select_us` spikes on frame 0 (252 / 377 µs) and once more on
frame 2 of the importance case (281 µs) against 2–5 µs on the other frames; plausibly first touch
of the table and scratch pages and the retired leases' Release path, not measured further.

## Run 39 A (run115) diagnosis

2026-09-17, no source edits, no Wine. Inputs: run115 F8 frames 11954–11961 (the `shadow-bug2`
scene: ship over the outpost deck, 2.15–2.36 km), 13985–13992 and 17558–17565 (outpost at
10–60 km), the per-cascade maps `shadow_map<k>_1_<frame>.r32f` (byte-identical to the bottle's
capture copies), `sun_shadow_apply_params`, `shadow_replay_map_basis`, `camera_state`. Twin
`verification/probe/sun_shadow_apply.py` plus scratch scripts (nearest-texel residual per
cascade, receiver-offset scans). "Measured" = from the capture or code; "inferred" is marked.

### Ranked root causes

**1. The apply quad reconstructs every receiver half a pixel off the RT2 sample position
(decides the moving line, the dark deck beyond it, its serration and flicker, and the ship's
speckled self-shadow).** `src/renderer/quad_vertex_program.h:37-42` shifts the strip by
(−1/W, +1/H) so pixel (i, j) receives `uv = ((i+½)/W, (j+½)/H)` — right for point-sampling RT2 —
but `src/temporal/sun_shadow_cascade_apply_ps.hlsl:71-72` (and `sun_shadow_apply_ps.hlsl:43-44`)
take `ndc = uv·2−1` as that pixel's NDC. Under the D3D9 raster (pixel centres at integer window
coordinates, the same rule the map lookup already honours in the program's header comment) RT2
texel (i, j) holds the depth at `ndc = (2i/W−1, 1−2j/H) = uv·2−1 − (1/W, −1/H)`. The receiver is
therefore displaced laterally by `z/(W·m00)` and `z/(H·m11)` (= z/1024 each at 1280×768): 1.2 units
at 1.2 km, 0.15 units at 150 units — an offset in view space that grows with distance, so on a
sloped receiver the sun-depth residual grows with z and crosses each cascade's world-unit bias
(C0 0.66, C1 1.27, C2 4.2, C3 25 units) at a fixed view distance: an iso-distance contour that
moves with the camera, curved on a flat deck, serrated/flickering by the rotated 3×3 kernel.

Measured on 11954 (own-surface receivers, |residual| < 30 units, C1 at z 300–3200, C2 idem, C0 at
60–260; residual = receiver sun depth − map at the nearest texel, world units):
- as is: C1 median 1.657 (p25/p75 1.21/2.17), 71.9 % above bias; C2 2.48, 26.9 %; C0 0.32, 38.9 %.
- receiver NDC moved by (−1/W, +1/H): C1 −0.025 (−0.41/0.35), **2.0 %**; C2 0.03, 3.4 %; C0 0.07,
  36.8 % (the ship's remaining fraction is real hull occlusion, p75 4.8 units). Every other sign
  combination is worse (C1 x−½ alone 93 %, y−½ alone 100 %, x+½ y+½ 7.3 % with median −1.14).
- best per-receiver lookup offset (scan): −1.5 texels in v in **both** C0 (texel 0.12 u) and C1
  (0.73 u), i.e. a texel-count constant = a world offset ∝ z, trending −1.0 → −2.25 texels from z
  400–800 to 1600–3200 (C1). A z-scale hypothesis (m32 −6.0195, which the naive z_map/z fit of
  1.0033 suggested) makes C1 99.7 % shadowed: rejected.
- twin with the corrected receiver (m20 + 1/W, m21 − 1/H): 11954 shadowed fraction 0.670 → 0.328,
  C1-owned 0.833 → 0.178, C2-owned 0.579 → 0.453, factor mean 0.553 → 0.778; 11958 the same;
  13985 C3-owned 0.935 → 0.681. The corrected factor image has no band on the deck; the remaining
  darkening is structure shadows and the tower's anti-sun face.
- the visible line: C1's bin over-bias fraction 37 % at z 400–800, 78 % at 800–1600, 100 % beyond
  = the contour at ~500–800 units from the camera; C2's 4.2-unit bias hides the same error to ~1.3 km,
  which is why "the actual area with shadows is small" and the lit disc sits around the ship.

Ruled out (measured): H4 basis/rows mismatch — apply `rows<k>` recomputed from the logged basis and
camera agree to 6e-8 relative, offsets < 0.005 units, on 11954/13985/17558; H2/H3 — no receiver
with z < 0 or > 1 inside any box, C3's 2048² map under the 4096 attachment has occupancy 11 % with
finite depths 0.36–0.62, no stale rows; H1 map content — C1 and C2 maps hold the same deck (the
nearest-texel depth is within 1 unit of the receiver after the correction in both); H6 — the
serration is the kernel rotating across the bias threshold, not a band texel mismatch.

**2. (inferred, secondary) The far outpost at 10–60 km stays 68 % shadowed in C3 after the
correction (13985)** with residuals 730–7300 units (median 1829, ratio 1.16): consistent with the
camera seeing the station's anti-sun side (forward · to-light = +0.23) and self-occlusion at 24-unit
texels; not verified against geometry. Revisit after the fix with a user run.

**3. (inferred) The ship's cockpit/nose at z < 100 shows 27-unit residuals** (C0/C1, 6.7 k px):
a caster the main view does not draw opaque (canopy) replayed as an occluder. Separate issue.

### Fix

`src/proxy/motion_output.cpp:7048` (cascades) and `:7112` (single map): add the D3D9 pixel-centre
term to the quad's projection latch — `in.m20 = camera_scene_.m20 + jitter_x + 1.f / float(in.width)`,
`in.m21 = camera_scene_.m21 + jitter_y - 1.f / float(in.height)` — with a comment naming the
convention (`quad_vertices` delivers texel-centre uv; the pixel's NDC is half a pixel left/up of it).
No program change; the `sun_shadow_apply_params` line already prints the resulting m20/m21, so the
twin (which builds `view` from the same m20/m21, `sun_shadow_apply.py:104-107,186`) and its
`analytic_shadow` follow automatically. The AO quad latches the same law at
`motion_output.cpp:1965` (`ao_linearize_ps.hlsl`); its horizon test is relative, so the effect is a
uniform half-pixel shift of the AO field — decide separately.

### Fixture case that would have caught it

The seam cascade case (`run_motion_output.py:994-1043`) compares the quad with a twin and an
analytic shadow that both place the receiver at `(i+½)/W·2−1`, so the receiver error cancels; its
geometry is close enough that z/1024 stays under a quarter texel. Two checks:
1. Host, `verification/analysis/test_sun_shadow_apply.py`: rasterize a tilted plane into a synthetic
   RT2 under the D3D9 rule (depth at `ndc = 2i/W−1`) and a matching map; `expected_factor_cascades`
   must light the plane (own-surface residual < bias/4) — fails with the current m20/m21 law, passes
   with the half-pixel term.
2. Seam, cascade case: a plane receiver far enough that z/1024 > ¼ of the owning cascade's world
   texel at a sun slope ≥ 1 unit/texel, asserted through the existing `shift_fit`/`edge_beyond_one`
   with `analytic_shadow` evaluated at the D3D9 pixel centre. Plus the capture-side self-check that
   flags this on any F8 frame: per cascade, the median own-surface residual (|r| < 30 units) must be
   below bias/2 — run115 C1 gives 1.66 vs 0.63.

## 2026-09-17 Own-ship-adaptive C0 and the ratio guard (`--shadow-cascade-adaptive-c0 K`)

Design and law: [shadow-cascade-extents.md](../architecture/shadow-cascade-extents.md), §5
(amended). Default off; the option is a companion of `--shadow-cascades` (an inherited value
cannot enable it). Worktree `agent-adaptive-c0`, two commits rebased onto main `c7395bd4` (the caster pool control and the run-115 apply fix merged underneath; the combined tree is what the numbers below were taken on).

- Clean CMake build: 0 warnings, `build/d3d9.dll` sha256 `eff688f0…ef46`;
  `check_no_x87.py`: 533 reachable functions, no violations. `build_motion_output.sh` (strict
  seam compile): clean, the new `x3m_shadow_own_ship_fixture_install` export present.
- Host: `test_shadow_cascades` (driver CHECKs for the commit law, hysteresis, the clamp, the
  guard's previous-active rule, the empty box of a dropped cascade against a hull spanning
  every box, the draw radius; launcher value/range/requires cases), `test_shadow_replay_depth`,
  `test_shadow_retention`, `test_sun_shadow_apply`, `test_shadow_replay_candidates`, `test_comparison_hotkeys` and the six
  mock-drift modules: 82 tests OK.
- Fixture (`X3M_FIXTURE_BOTTLE=X3`, bottle X3), the cascade script under 8 / 48 / 240 / 800
  (set R's ratios), K 1.5, two hulls drawn every frame (H1 radius 1.69, H2 radius 33.7 through
  the script's camera), the seam naming one of them as the player ship:
  `seam-ownership-shadow-replay-adaptive-small` 318 checks — one `node` commit at frame 1
  (radius 1.69, `e0=8`, `active_mask=15`), E0 stays 8 on every map;
  `…-adaptive-big` 318 checks — one commit at frame 1 (radius 33.7, `e0=50.59`, texel 0.395,
  depth behind 101.2, `active_mask=13`): from frame 2 cascade 0 reads back at 50.59, cascade 1
  (48 < 3 × 50.59) is never replayed and stays void, cascades 2 and 3 replay as before, the
  counter shows `c1=0`; `…-adaptive-swap` 319 checks — H1 until frame 5 and H2 from it: exactly
  one further `node` commit at frame 5, cascade 0 alone voided at that boundary (its map still
  holds frame 5's replay), cascades 2 and 3 valid on frame 6 as without the option. Every
  replayed map of every active cascade equals the CPU projection of the draws it kept, the
  hulls included (`max_depth_error` 8.46e-06 of the depth range, 0.55 FP16 codes; C0 texel
  0.395 units at E0 = 50.59). The twin gained a far-clip ambiguity band (a hull larger than a
  cascade's depth range is clipped at z = 1 on the GPU; samples within the depth tolerance of
  1 are ambiguous), 1 texel of 42,856 on the small case's frame 4 before the band.
- Records equal: `seam-ownership-shadow-replay-cascades` 278 checks / 4.0742862848497374e-06,
  `…-cascades-toggle` 283 / 5.82e-06 (main's follow-up record), `…-cascades-poll-agree` 281 / 9.65e-06,
  `…-cascades-casters-20` 278 / 3.42e-06, `seam-ownership-shadow-replay-on` 203 / 1.19e-05,
  `seam-ownership-shadow-retention-live` 9,743 checks, 1,535 frames, 39 / 28 compared,
  1.7169477474210382e-06, `sun-shadow-apply-cascades` 2,891 checks — all identical to the
  recorded runs (the tracked result files were restored, only their timestamps differed).
- Cost (host, clang -O2, one core): `shadow_cascade_draw_radius` 11.7 ns per own-ship draw;
  the boundary update 7.3 ns; a commit (set rebuild + four bounds) 130 ns, at most once per
  commit. Per frame in flight with the option on: one registry walk (six to eight bounded
  reads) and, per z-writing draw with a known extent, one pointer compare (the root) or one
  direct-mapped cache probe; a miss walks the parent links (≤ 64 walks per frame). The
  option-OFF path is not free of the change: the origin-rule fallback tests
  `shadow_cascade_active` per cascade per draw (one compare per cascade per draw, 4 at set R),
  and the per-frame bounds skip inactive cascades (none while off). The replay and apply
  transactions are unchanged.
- Limits. The registry walk and the parent-link ancestry are exercised on synthetic nodes
  only (the seam injects the root; scope nodes equal it); the first flight with the option
  must show `own_status=0` and a plausible `own_radius` on the `shadow_cascade_set` lines, and
  measure the chase-camera distance (`camera_state t` against `node+0xb0`) to set K. No
  fixture drives the apply quad through the DLL with a dropped cascade (the never-inside rows
  are covered by source reading and the shader's selection law). Native Windows unverified as
  elsewhere.

### Review fixes (2026-09-18): walk under test, keyed cache, hold on hull-less frames, compaction

Fable review of the first commit found no blocker and six items; all applied in the second
commit (`2dbdf623` after the rebase), rerun on the merged tree.

1. `object_capture::own_ship` / `own_ship_descends` now run on the host over a synthetic
   memory image with the documented layout (`0x608504` → registry → table → bucket → link row →
   cockpit `+0xc` → ref `+0x70` → node `+0x28`; parents through `+0x18`): ready, a stale row
   before the live one, null slot, zero active handle (`NoTarget`, never a stale-row match),
   missing, cycle, malformed bucket count, null ref object (a fresh generation), misaligned
   node, unreadable handle, a foreign generation (rebound registry → another ship), descent
   through two parent links, a foreign root, a wrong root handle, an unreadable parent, the
   16-link bound (`test_shadow_cascades` driver).
2. `own_ship::Cache` (`src/proxy/own_ship_cache.h`, pure): keyed on (node address, node
   handle), flushed when the root, its handle, the load epoch or the registry epoch changes,
   entries expire after 256 frames. Host checks: a reused part address under a new handle
   misses and re-walks the current memory; every flush cause; the stamp expiry. Fixture
   `seam-ownership-shadow-replay-adaptive-reuse` (336 checks): H2 declared H1's part until
   frame 4, then its address kept under another handle: the measured radius drops to H1's
   (1.69) on every capture line from frame 5, `pending_frames` 2/3/4 under the hysteresis, the
   committed E0 stays 50.59 and no re-anchor happens (one `voided_at_boundary`, at frame 1).
3. The probe runs only after the `exact_extent` test (draws without a known extent pay
   nothing), and at most 64 parent walks per frame: the rest count `own_walk_deferred` on the
   `shadow_cascade_set` line and are not-own that frame (not cached). Host thrash case: 500
   distinct nodes a frame over 1,000 frames, 64 walks + 436 deferrals per frame, 2.6 ns per
   draw at `-O2` (the synthetic walk is one failed read; an engine walk is ≤ 16 bounded
   `engine_memory::read`s, so the cap bounds the frame at 64 × that).
4. A boundary without a measured own-ship draw (cockpit view, menu, loading, the ship's first
   frame, no ship resolved) holds the committed E0 and counts `held_frames`; nothing pends, so
   a view toggle never voids C0. Host: 1,000 hull-less boundaries hold E0 = 25,000, the next
   measured ship commits. Fixture: `held_frames=1` on the first commit line of every case.
5. The apply quad's slots are the ACTIVE cascades in order (`shadow_cascade_apply_slots`):
   a dropped cascade is not a slot, so the previous cascade's blend band leads into the next
   active one in the unchanged shader (slot s blends into slot s + 1) and in the twin. The
   params line prints `source<s>=` (the configured cascade a slot samples; the twin reads its
   map file by it, older lines default to the slot) and the `shadow_cascade_set` line prints
   `apply_slots=` (`0,2,3` on the adaptive-big and swap commits, asserted). Host: slot lists
   for the guard's masks; the twin parses `source<s>` and refuses a non-ascending list.
6. The four adaptive `-fixture.json` records are tracked under `verification/results/
   bottle-X3/` (about 1.9 KB each: events, `e0_by_frame`, checks, `max_depth_error`, hashes).

Merged tree: clean CMake build 0 warnings, `build/d3d9.dll` sha256 `cc51e4de…b861`,
`check_no_x87.py` 534 reachable functions, 0 violations; the 12 host modules 84 tests OK;
fixture (`X3M_FIXTURE_BOTTLE=X3`): adaptive small/big/swap/reuse 333 / 333 / 336 / 336 checks,
`max_depth_error` 8.46e-06; records equal for cascades 278, casters-20 278, toggle 283,
poll-agree 281, replay-on 203, retention-live 9,743 and `sun-shadow-apply-cascades` 3,293
(main's record); `seam-ownership-shadow-pool-static-live` (the merged pool control on the
shared draw path) exit 0.


## Run 39 A (run115) fix: the pixel-centre term of the apply latch (2026-09-18, worktree `agent-affda091a6929e138`)

Base: main 30ea19ce (rebased onto it after the adaptive C0 merge). Not installed, no game run.

**Fix.** `src/renderer/quad_vertex_program.h` states the convention once: `quad_pixel_centre_m20(W) = +1/W`,
`quad_pixel_centre_m21(H) = -1/H`, with the D3D9 rule (pixel (i, j) rasterised at window (i, j), NDC
`(2i/W-1, 1-2j/H)`; `quad_vertices` delivers uv `((i+½)/W, (j+½)/H)`, so `uv*2-1` is half a pixel right/down of the
pixel). Derived independently from `quad_vertices` (x0 = -1-1/W gives u(window i) = (i+½)/W; y0 = 1+1/H gives
v(window j) = (j+½)/H) and the raster rule: the sign agrees with the diagnosis. `motion_output.cpp` adds the two terms to
the cascade latch and the single-map latch beside the jitter (`centre_x/centre_y`); no program change; the logged
`m20/m21` carry the term, so the twin follows. The AO latch (`motion_output.cpp`, `ao_jitter_x`) is not changed and
carries a comment: GTAO folds the half-resolution texel-centre ray while the half texel holds the even full pixel, so
every reconstructed point sits 1/hw in NDC beside its pixel, but AO compares reconstructed points only with each other
(a uniform shear of view space by z/(hw m00), ~0.1 degrees of horizon angle) and has no externally rasterised
reference; the CPU reference (`ambient_occlusion_reference.h`, `HalfImage::position`) folds the same ray. Not the same
defect; changing it would move the AO field.

**Fixture.** Both `sunapply` scripts now synthesise RT2 as the D3D9 rasterizer samples it (`sun_apply_latch` /
`sun_apply_texel_direction` in `motion_output_sun_apply_inc.h`: texel (i, j) = the scene at NDC `(2i/W-1, 1-2j/H)` of
the jittered projection) and latch the raster's jitter plus the pixel-centre term, as production does; before, RT2
was synthesised at `(i+½)/W` like the quad's own reconstruction, so no fixture could see the error. The lines log
`raster_m20= raster_m21= legacy_latch=`; `analytic_shadow` evaluates the receiver at the D3D9 pixel centre from the
raster's terms alone (independent of `m20/m21`: it cannot cancel against the twin again) and `parse_params` /
`parse_cascade_params` require the fields. `X3M_FIXTURE_SUNAPPLY_LEGACY_LATCH=1` (fixture only) drops the term: the
pre-fix law against a D3D9-rasterised RT2. New case **i** (`sun-shadow-apply-cascades`, frames 21-28): case a's
scene at scale 170, 30 degrees, jitter (0.125, 0.375), eight sub-texel phases; the receivers 800-1,250 units out, so
z/(W m00) = 3-4.5 units against cascade 1's 11.7-unit texel (over a quarter texel) at 10 units/texel of sun slope.
Runner: the latch law asserted per frame (`m20 = raster_m20 + 1/W`, `m21 = raster_m21 - 1/H`, or the raster's alone
under the legacy witness), the F8 self-check `own_surface_residual` per frame (cascade 1 own-surface median within a
quarter texel on case i), a second shift fit over the i frames, `half_pixel_receiver` and `legacy_latch` in the
record; the cascades inc header joins the sources manifest.

**Twin / F8 self-check.** `sun_shadow_apply.py`: `pixel_centre_terms`, `own_surface_residual` (per cascade, owned
valid pixels, receiver sun depth minus the map at the nearest texel in world units, |r| < 30 units: median, quartiles,
fraction over the constant bias, `median_over_half_bias`), `own_surface_residual_line` ("median own-surface residual
(units): c1 1.626 (bias/2 0.634, over bias 70.9%, n=140791) OVER"), the CLI prints both on cascade frames, and
`--add-pixel-centre` adds the term to a params line logged by a pre-fix build. `expected_factor_cascades` returns
`sun`. Host `test_sun_shadow_apply.PixelCentre`: a tilted plane rasterised under the D3D9 rule into a synthetic RT2
and a 512-texel map: with the term the own-surface median is 0.000 units (bias 0.024, bias/4 0.006) and every core
receiver is lit in both twins; with `m20 = m21 = 0` the median is 0.133 units (over the bias, 100 % over), 0 % lit.
`analytic_shadow` is unchanged by any `m20/m21`, changed by the raster's jitter, and places column 16 at x = 0 exactly.

**Evidence.** Clean build 0 warnings; `check_no_x87.py`: PASS, 534 reachable, 0 violations; DLL `a7d40b76…`, seam
`e74b49f1…`, fixture `0260b6be…`. Host: `test_sun_shadow_apply` 21 OK (3 new), plus `test_motion_output_runner` and
the six mock-drift modules: 60 tests OK.
- Twin on run115 (`sun_shadow_apply.py --log … --frame F [--add-pixel-centre]`), without / with the term:
  11954 C1 own-surface median 1.626 → −0.029 units (bias 1.268), over bias 70.9 % → 1.3 %, C1-owned shadowed
  fraction 0.833 → 0.178; C2 3.527 → 0.038, 40.6 % → 3.8 %, 0.579 → 0.453; C0 0.236 → 0.020 (18.4 % → 17.5 %: the
  hull's real occlusion); factor mean 0.553 → 0.778. 11958: C1 1.622 → −0.034, 71.2 % → 1.2 %. 13985: C3 (the outpost
  at 10-60 km) 19.985 → 1.557 (bias 24.95), 26.0 % → 4.2 %, C3-owned shadowed 0.934 → 0.681; C0 0.049 → 0.005.
- Witness (`X3M_FIXTURE_SUNAPPLY_LEGACY_LATCH=1`, the same binaries): `sun-shadow-apply` fails at frame 2
  (`plane_beyond_two_texels` 2,487 of 3,633 analytic-shadowed plane pixels, `ok` False), `-wide` at frame 2 (5,055),
  `-cascades` at frame 2 (case b: 3,441 ambiguous of 11,742 valid, over the 1/5 bound: the misplaced receivers sit
  at compare equality). Case i on the same run's readbacks (host-only, past the aborted frame): cascade 1 own-surface
  median 7.49-7.58 units per frame (threshold texel/4 = 2.93; over bias 29 % against the 12.25-unit fixture bias), the
  assertion that fails; the summed map-space shift fit stays (−0.25, 0): the receiver displacement lies mostly along
  the sun, which the (u, v) fit cannot see, so the residual is the witness and the fit only a bound. Case g under the
  legacy latch: median 3.68 units.
- Acceptance (`X3M_FIXTURE_BOTTLE=X3 … wine_lock.py … run_motion_output.py --dll … --seam … --fixture …`, 20 cases):
  20 / 20 exit 0. `sun-shadow-apply-cascades` 4,340 checks, 29 frames: case i medians −0.35…−0.41 units, its fit
  (0, 0); case g's fit (0, 0) (was (0, −0.25): the quarter texel was this error), legacy rule (−0.5, −0.5) with
  `mismatch_at_zero` 715 vs 373; `edge_beyond_one` 0 on every frame but h, whose region C count is 28 (was 22, the
  same 2.06-texel residual); `ambiguous_max` 1,742. `sun-shadow-apply` 169 checks, `-wide` 173, `plane_beyond_two_texels`
  0, `worst_codes` unchanged. Cascades 278 / casters-20 278 / toggle 283 / poll 281 ×3 / adaptive-reuse 336 /
  retention 9,743 / 9,740 / 6,629 / 9,890 / pool 165 / 181 / 181 / 165 / 99 / 53: every record equal to the committed
  one field for field (timings, hashes, paths aside), so those records are kept; the three apply records are
  regenerated. Live `run_sun_share_live.py`: 21 / 21 passed; `shadow_apply` and `shadow_apply_cascades` equal to the
  committed record apart from `apply_us_max`/`elapsed_seconds`/`command` (the live script's receiver is wholly
  shadowed, 4,032 / 4,096 owned = shadowed), so `sun-share-live.json` is kept.

Open: the far outpost's remaining 68 % C3 shadowing on 13985 (anti-sun face, inferred) and the ship's cockpit
residual (a canopy replayed as an occluder) are unchanged by this fix; a user run decides. The live cases cannot
witness the receiver error (whole-receiver shadow); the `sunapply` fixture with its D3D9-rule RT2 is the witness.
## Five cascades (2026-09-17, worktree `agent-a76e082432e1ab2af`)

Design: [../architecture/shadow-cascade-extents.md](../architecture/shadow-cascade-extents.md) §3
amendment; mechanism: [../architecture/shadow-cascades.md](../architecture/shadow-cascades.md),
"Amendment: a fifth cascade". `shadow_cascade_max` 4 → 5 (`shadow_cascade_default_count` 4 keeps
the default set), extents to 150,000, cap default 1024 for the fifth, `cascade_capacity` 5, the
mode / device lines list five slots (`extents= sizes= caps= records=`), `shadow_map0..4` F8
readbacks, `c0..c4` counters; launcher `--shadow-cascades` takes 1..5 values within [50, 150000].
Budget rule unchanged: only the last cascade alternates over budget, always in full (no partial
map); with five cascades C4 alone, C3 every frame. Rebased onto main `f197960c` (caster pool
control): its per-cascade `records` default and the pool validator's `records=` expectation grew
to five slots.

- Apply program `sun_shadow_cascade_apply_ps.hlsl`: fifth sampler `s5`, `cascades[25]` at
  `c13–c37`, fifth selection weight and `[branch]` PCF. **499 of 512 ps_3_0 slots** (was 406; the
  estimate of ~70 per branch held at +93), 2,133 words, bytecode sha256 `a1c211e5…1236cf`,
  checked at attach against `MaxPixelShader30InstructionSlots` (fixture: `ps30_slots=512`,
  `cascade_slots=499`). No PCF restructuring was needed; margin 13 slots.
- Memory at 4096²: five `R32F` maps 320 MiB + the shared `D24X8` attachment 64 MiB = 384 MiB.
- Per-draw bounds pass (fixture bench, 2,000,000 rounds): single verdict 46.8 ns, four-cascade
  mask 43.5 ns, **five-cascade mask 42.3 ns** (`mask5_ns`; one more interval test after the shared
  corner transform, within noise of four), per-cascade-sun mask 115.6 ns.
- Clean CMake build (`cmake --build build --clean-first -j4`, 70 objects): 0 warnings;
  `build/d3d9.dll` sha256 `dd06ce80…`; `check_no_x87.py`: PASS, 77 roots, 527 reachable, 0
  violations. Host: `test_shadow_cascades` (five-cascade set, sixth refused, budget policy on
  cascade 4, launcher values and refusals), `test_shadow_replay_candidates`,
  `test_shadow_retention`, `test_snapshot_x3_run`, `test_shadow_replay_depth`: 55 OK.
- New `sun-shadow-apply-cascades-5` (`X3M_FIXTURE_SUNAPPLY_CASCADES=5`; record
  `sun-shadow-apply-cascades-5-fixture.json`): the 30 km set on five 256² maps, 15 frames,
  3,102 checks (2,945 fixture + 157 validator), exit 0. Range frames 'r' at scale 40 / 100 / 560 /
  2,800 / 11,250 put the box's shadow in cascade 0..4 alone (881 / 2,376 / 881 / 884 / 886
  analytically shadowed plane pixels owned by the owner, 0 by any other); seam frames 's' at
  60 / 225 / 1,125 / 5,625 run it through cascade k's band into k + 1 (band-shadowed 1,091 / 868 /
  1,024 / 956, the next cascade 758 / 586 / 591 / 588); the 'd' pair replays C4 on frame 10 and
  samples the retained map through the moved camera on frame 11 (`draws4=0`, `far_frame=10`);
  the Reset before frame 13 leaves C4 absent and lit, frame 14 repeats frame 12 byte for byte.
  Every frame: worst 0.99999 FP16 codes, `edge_beyond_one` 0, `interior_wrong` 0,
  `monotone_violations` 0, ambiguous ≤ 1,720 of 11,856 valid.
- Twin (`sun_shadow_apply.py`), two gated additions the five-cascade validator turns on and the
  older records do not: per-cascade `eps_depth` (the compare ambiguity scaled to the same 1.55
  world units the three-cascade set has, since every cascade here spans 300,512–600,000 units),
  and `band_eps` (a position `EPS_SELECT` away that moves the factor by ≥ a quarter FP16 code
  marks the pixel ambiguous: the band's 1 / 0.10 amplification of the float32 position flipped one
  dark band pixel by 1.0004 codes on the 60-scale seam frame).
- Records against main's committed ones (non-timing fields; `*-fixture.json`): `sun-shadow-apply-cascades`
  changes in exactly two fields, `cascade_program_slots` 406 → 499 and `checks`/`fixture_checks` +2
  (4,340 → 4,342: the new refusal checks "six cascades" and "a sixth map"); every readback comparison,
  edge and half-texel/half-pixel record is byte-identical. `seam-ownership-shadow-retention-off` differs
  only in `presented_identical_to` (two siblings listed instead of one: the runner names the sibling
  cases of the same partial run, not a behaviour). `sun-shadow-apply`, `sun-shadow-apply-wide`, retention
  live / census / live-poll, the six pool cases and the four adaptive cases are identical in every
  non-timing field. Cases without a committed record equal the ledger's counts: replay-cascades 278,
  `-casters-20` 278, `toggle-single` 214, `poll-agree/null/disagree/refusals` 281 each; the toggle case
  reports 283 checks, which is main's own follow-up schedule (the ledger's 277 predates the ON-edge
  full-replay fix on main), not a change of this branch.
- Not done: the optional opposite-parity alternation of the two farthest cascades (the brief's
  option); no in-game run yet (the 30 km set needs `--shadow-cascades 250,1500,7500,37500,150000`).

### Review fixes (2026-09-18, on main `7c9e67f6`; commits `914f2868` merge, `8e756cca` fixes)

Merged main `7c9e67f6` (caster pool control, own-ship-adaptive C0 with the ratio guard's active mask and
the compacted apply slots, the pixel-centre apply latch): the five-slot arrays cover the active mask and
the slot list (`shadow_cascade_apply_slots` writes `out[shadow_cascade_max]`), the mode line lists five
`records=` slots beside `adaptive_c0=`, the five-cascade script runs under the raster-rule RT2 and checks
the latch law per frame (validator 172 checks). Review findings, no blocker:

1. `shadow_replay_candidates` cascade tail: `cascade_fields[400]` overflowed at five cascades with the
   pool options (the worst case needs 528 bytes for the counters alone) and the overflow path blanked every
   per-cascade counter. Now sized from the format strings for `shadow_cascade_max` (bound 866 bytes;
   `static_assert` on one-digit indices; host test `CandidatesLineTail` formats the worst case with
   ten-digit counters and a twenty-digit `select_us` against the bound the source declares), a field that
   does not fit is left off and counted (`candidates_line_truncated_`, one
   `shadow_replay_candidates_truncated` line), never blanked.
2. `shadow_retention_frame` lists `live_c0..4 would_c0..4 capped_c0..4` (parser `FRAME_FIELDS`, the
   retention note and `test_shadow_retention` follow; the fifth slot reads 0 on every existing case).
3. `shadow_replay_pass.h` comment: five maps.
4. `sun-shadow-cascade-apply-program.json` regenerated on this tree: `--check` PASS (2,133 words,
   `a1c211e5…`, 499 slots).
5. `sun_shadow_apply_params` (both apply paths) logs `raster_m20= raster_m21= pixel_centre=1` like the
   fixture line; `sun_shadow_apply.py` reads the logged law when present (`--add-pixel-centre` is ignored
   then and reported as `pixel_centre_logged`), keeps the flag for older logs, and refuses a line whose
   m20/m21 do not carry the term it claims (`test_latch_law_fields`: both forms).

Evidence on `8e756cca`: clean CMake build 0 warnings (70 objects), `build/d3d9.dll` sha256 `9421907e…`,
`check_no_x87.py` PASS 77 roots / 534 reachable / 0 violations; host `test_shadow_cascades`
`test_shadow_replay_candidates` `test_shadow_retention` `test_snapshot_x3_run` `test_shadow_replay_depth`
`test_sun_shadow_apply` 77 OK; the 26-case set (the four `sun-shadow-apply*`, eight replay-cascade,
four adaptive, four retention, six pool cases) all exit 0 with the counts above;
`sun-shadow-apply-cascades-5` 3,117 checks, `edge_beyond_one` 0, `monotone_violations` 0 on all 15
frames, bench five-cascade mask 59.4 ns against 45.4 ns for four this run (42.3 against 43.5 on the
previous run: the two are within run-to-run noise of the same corner transform).

## Sliding ladder behind the adaptive C0 (2026-09-18, worktree `agent-a1c40b35073d19a6c`)

Law: [shadow-cascade-extents.md](../architecture/shadow-cascade-extents.md) §5, amendment of
2026-09-18. The ratio guard (drop `E_i < 3 E_{i−1}`) is replaced: while E0 is above the configured
value, `E_i' = max(E_i_config, E0 × R^i)` capped at the last cascade's configured extent, a cascade
whose slid extent reaches the next one's is dropped (`shadow_cascade_ladder_extent`,
`shadow_cascade_ladder_mask`, `shadow_cascade_adapt_c0(config, e0, out, ratio)`); every changed
cascade re-anchors its grid and voids its retained map (`ShadowCascadeAdaptive::changed`, one
`invalidate_retained(i)` per bit); `--shadow-cascade-ladder-ratio R` (`X3M_SHADOW_CASCADE_LADDER_RATIO`,
[2, 16], default 5, requires the adaptive option). Base: main `84115b4a` (five cascades merged).

- Clean CMake build 0 warnings, `build/d3d9.dll` sha256 `d157b6ee…`; `check_no_x87.py` 534 reachable,
  0 violations; seam DLL `c423ade7…`, fixture `88495a60…` (strict seam compile clean).
- Host: `test_shadow_cascades` (driver CHECKs: the 37,500 set at radius 450 → 675 / 3,375 / 16,875 /
  37,500 all active, with five → 84,375 / 150,000; radius 4,000 → 6,000 / 30,000 / (37,500 capped,
  dropped) / 37,500, with five active 19; the default set at radius 1,000 → 1,500 / 7,500 / (25,000
  dropped) / 25,000 with `changed` 7 then 3 at 1,875; the return to the configured set in one commit
  with `changed` 7, `slid` 0; R 2 / 16 / out of range; E0 at the configured value unslid at any R;
  the slid box wider across and deeper behind with the light reach unchanged; launcher value / range /
  requires / no-leak cases), `test_shadow_replay_depth` (the depth line parses five cascades, six
  refused), `test_shadow_replay_candidates` (`CASCADE_MAX` 5), `test_sun_shadow_apply`,
  `test_shadow_retention`, `test_motion_output_runner` and the six mock-drift modules: 96 tests OK.
- Fixture (`X3M_FIXTURE_BOTTLE=X3`), new cases on 8 / 48 / 240 / 1,200 / 4,800 (the 250 / 1,500 /
  7,500 / 37,500 / 150,000 set at 1/31.25; map sizes 256 / 256 / 512 / 1,024 / 2,048, the far object
  being one texel of a 256-texel far map), K 1.5, R 5:
  `seam-ownership-shadow-replay-ladder-corvette` 373 checks — H3 radius 14.46, one `node` commit at
  frame 1: `e0=21.69`, `extents=21.69,108.45,542.24,2711.19,4800`, `active_mask=31`,
  `apply_slots=0,1,2,3,4`, `slid=15`, `changed=15` (every ratio ≤ 5, no gap behind C0); cascades 0-3
  read back at the slid extents with a void basis after frame 1 and all five replay from frame 3;
  every replayed map equals the twin (`max_depth_error` 1.63e-06; one far map, frame 6 cascade 4,
  coarse: 5 covered texels on both sides, all edge, nothing to compare in depth).
  `…-ladder-destroyer` 373 checks — H4 radius 182.5 (E0 273.8 clears the far object's 240-unit
  reach): `extents=273.76,1368.81,4800,4800,4800`, `active_mask=19`, `apply_slots=0,1,4`: 25 E0 =
  6,845 capped at the ceiling reaches it, so cascades 2 and 3 are dropped, never replayed, their maps
  untouched; `max_depth_error` 5.96e-06.
  `…-ladder-shrink` 376 checks — H3 until frame 5, H1 from it: commits at frames 1 and 5, the second
  `e0=8 extents=8,48,240,1200,4800 slid=0 changed=15`: the configured set back in one commit,
  cascades 0-3 voided at that boundary, cascade 4 (the ceiling, never slid) not in the mask.
- Adaptive cases under the ladder: `…-adaptive-small` 333 checks, record equal to the committed one
  field for field apart from the added `ladder` key (E0 8, `active_mask=15`, `slid=0` throughout);
  `…-adaptive-big` 333 / `…-swap` 336 / `…-reuse` 336 — E0 50.59 now slides C1 to 252.95 and drops
  C2 (240 → 800 capped, reaching C3): `active_mask=11`, `apply_slots=0,1,3`, `changed=7`; the records
  are regenerated (their `map.covered_texels` moved with the slid C1). Regression on the same
  binaries: `seam-ownership-shadow-replay-on` 203 / 1.19e-05, `…-cascades` 278 / 4.07e-06,
  `…-cascades-casters-20` 278 / 3.42e-06, `…-cascades-toggle` 283 / 5.82e-06, `…-cascades-poll-agree`
  281 / 9.65e-06, `…-shadow-retention-live` 9,743 checks, 1,535 frames, 39 / 28 compared,
  1.72e-06, `…-shadow-pool-static-live` 181, `sun-shadow-apply-cascades` 4,342 (`edge_beyond_one`
  28, `ambiguous_max` 1,742), `sun-shadow-apply-cascades-5` 3,117 (`edge_beyond_one` 0): every
  record equal to the committed one apart from timings (restored).
- Cost: no per-draw change (the box test, replay and apply read the live set as before); the
  ladder runs at a commit only (count − 2 multiplications and compares).
- Limits: the fixture drives no apply quad with a slid set (the slots and `source<s>=` are asserted
  on the `shadow_cascade_set` line and by the twin's parser); the far maps at the fixture scale are
  coarse (recorded per map as `ladder.coarse_maps`); a big-ship flight sets K and shows the ladder's
  seams in play; native Windows unverified as elsewhere.

### Review round (2026-09-18, branch `ladder-followup` off main `ee3ff318`): change mask, slid policies, refused index

1. `ShadowCascadeAdaptive::changed` is now `shadow_cascade_change_mask` (extent delta OR active-bit
   delta): a cascade dropped and restored with its extent unchanged (the last one under an E0 at the
   ceiling) is voided at both commits and cannot republish its pre-drop basis. Host: E0 25,000 →
   `changed` 11 (bit 3 with its extent unchanged), back to 250 → 15; `shadow_cascade_change_mask`
   15 / 0.
2. `shadow_cascade_ladder_policy`: caps, records, `static_from` and `large_min` follow the extents
   (each live cascade takes the policy of the configured cascade its extent matches closest in
   ratio; dropped → cap 0; `large_min` scaled by the first static-only cascade's extent over its
   match's; active caps scaled down together when the bounds would exceed the configured sum). The
   draw path's caps and static mask are refreshed at the commit; the candidates line keeps its
   static group whenever the configured set has one. Host: corvette on 250 / 1,500 / 7,500 / 37,500
   with static-from 3, records 1,024 / 1,024 / 2,048 / 4,096, large_min 1,500 → `static_from` 2
   (16,875 static-only), `large_min` 675, records 2,048 / 4,096 / 4,096, bounds ≤ 2,688; destroyer →
   `static_from` 1, the dropped cascade idle; unslid → configured; no configured static → none.
3. `shadow_cascade_ladder_extent(i >= count)` returns NaN and `shadow_cascade_adapt_c0` refuses a
   non-positive or nonfinite extent (host: indices 4 of four and 7 of five).
4. §5 wording "at or above".

Build 0 warnings, `check_no_x87.py` 534 reachable, 0 violations; host 97 tests OK (the 12 modules).
Fixture (`X3M_FIXTURE_BOTTLE=X3`): new `seam-ownership-shadow-replay-ladder-corvette-static` 373
checks (corvette env + `X3M_SHADOW_CASCADE_STATIC_FROM=3`): the commit line prints
`caps=8,8,8,8,8 static_from=2 large_min=0`; from frame 2 cascades 2-4 (542 / 2,711 / 4,800, matching
the configured 1,200 / 4,800 / 4,800) refuse every draw (`static_only_refused2..4=6`, `c2..4=0`,
`leased=5`: F, in the static cascades alone, is not leased) while frames 0-1 refuse on the configured
3-4; replayed maps 6.0e-07. ladder-corvette 373 / destroyer 373 / shrink 376, adaptive small 333 /
big 333 / swap 336 / reuse 336 and pool-static-live 181: equal to the committed records apart from
the added `ladder.static_from*` / `matched_by_frame` keys and timings (the pool record restored).

## Run 40 A (run116)

Capture `/private/tmp/x3-bottleX3-run116` (319 non-log files, `session-20260918-003512-216.log`,
329,902,274 bytes / 4,590,648 lines), candidate d4d824a4 from d415264f, five cascades
250/1,500/7,500/37,500/150,000 at 4096² each, `--shadow-cascade-static-from 3
--shadow-cascade-large-min 1500 --shadow-cascade-drop-order importance --shadow-cascade-records
1024,1024,2048,4096,4096`, sun poll on, retention census, `sun-shadow-apply` on. Read via
`tools/analysis/shadow_replay_candidates.py`, `shadow_retention.py`,
`verification/probe/shadow_replay_depth.py`, `verification/probe/sun_shadow_apply.py --log --capture
--frame` (F8 self-check), and grep/Python against a pre-filtered subset of the shadow/sun/frame_end
lines (log never read whole).

1. **Cascades** (24,296 `shadow_replay_candidates` frame lines, 23,870 `shadow_replay_depth` frame
   lines — the 426-frame gap matches the toggle-off windows below byte for byte). Per-cascade
   `records`/`draws` median/max: c0 8/51, c1 9/51, c2 26/70, c3 40/110, c4 83/356; **0 capped frames
   and 0 capped total on every cascade** (`shadow_replay_candidates.py` `capped_total`=[0,0,0,0,0]).
   `static_only_refused3/4`: sum 1,003,845 / 2,849,841 (median 33 / 108 per frame, max 140 / 628).
   `large_admitted3/4`: sum 693,231 / 2,138,767 (median 33 / 65, max 66 / 331) — most static-eligible
   draws in c3/c4 are admitted by size, not refused. `class_miss3/4`: 409 / 1,535 of 24,296 frames'
   candidates, a fraction of 0.00043 / 0.00052 — negligible. `class_store`/`class_ring` totals
   3,729,505 / 2,064,716. `dropped_min_size3/4` is 0.0 on every frame (the importance drop never
   triggers: no cascade ever hits its record cap, consistent with 0 capped frames). `select_us`
   median 7.4, p90 19.2, max 155.0. `draws0..4` medians match `records0..4` exactly (no cap drops).
   `far_replayed` fraction 0.99996 (23,869/23,870); `far_frame` equals the current frame on every
   replayed row but one (the far cascade is never retained/stale this session — no parity split to
   report). `us` vs total draws (23,870 replayed frames): slope 1.216 µs/draw, intercept 25.7 µs at
   five 4096² maps (run39's four-cascade baseline: slope 1.194, intercept 33.8 — consistent order of
   magnitude, intercept slightly lower despite the extra cascade). `sun_shadow_apply_frame`: 23,870
   lines, `skip_reason`=`none` on all 23,870 (100% applied, 0 skips of any kind — unlike run39's
   102 `replay`/16 `sun` skips). **Candidates-line truncation: 0** (`MalformedLine` count from a full
   streamed re-parse of every `shadow_replay_candidates` line = 0).
2. **Frame cost.** Direct per-frame cost (the reliable number): replay `us` median at the regression
   above ≈ 1.216×166(median total draws)+25.7 ≈ 227 µs, plus `sun_shadow_apply_frame` `us` median
   60.0 (p90 70.6, max 10,125.1 — one spike) ⇒ **shadows cost ≈ 0.29 ms/frame at rest**, well under
   run115's reported 1–3 ms despite five 4096² maps vs four maps at up to 4096/2048. The
   `sun_shadow_toggle` A/B (36 events, frames 10396–12346 rapid-toggled plus one late pair
   23115/23166) cannot be cleanly read off the 300-frame-stride `frame_end` (115 lines total): dt_ms
   per 300-frame window is ~3,200–4,900 ms (≈11–16 ms/frame) before frame 10200, jumps to
   7,330–7,699 ms (≈24–26 ms/frame) for the whole 10500–12900 stretch that contains the toggle
   bursts, but an equally elevated 7,321–8,355 ms plateau recurs at 22800–24000 with **no** toggle
   event nearby and `draws` differing too (877 at 10200 vs 634–651 in the toggle window) — the coarse
   stride is confounded by scene/draw-count changes, so this run cannot isolate the toggle's own
   cost; the direct replay+apply `us` sum above is the trustworthy figure.
3. **Sun.** `shadow_replay_sun` (24,296 lines): `verdict`=`sampled` on all, `unlatched`=1 frame,
   `disagree`=0, `invalid`=0, `bounds_unavailable`=0, `extent_refused`=0 total. `shadow_replay_sun_point`
   (140 sampled rows, `reason`=`point`/`poll`=`ok` on every row — no fallback ever fires):
   `agreement_deg` median 0.000691/max 0.000979; `distance` median 15,679,716; `candidates` median
   4/max 5; `directional` median/max 1 (never the forced-directional path). `rederived` sums to 275
   over 140 polled rows (median 1, max 5 per poll); `frames_point` spans 1–23,941, i.e. one poll per
   ~171 frames — too sparse in this log to split a moving-vs-at-rest re-derivation rate per 1,000
   frames; the per-cascade `dir<i>`/`anchor<i>` fields in the same rows show 1–2 of the 5 cascade
   slots changing between consecutive polls, not a per-cascade counter. `shadow_replay_sun_source`
   and `_sun_latch` log once each (session start), not per frame — no source-per-frame series exists
   in this log; a diagnostic that timestamps `source<i>=` per cascade every N frames (already present
   in the sparse `sun_shadow_apply_params`, 32 lines) at a finer stride would answer "source per
   frame" and the fallback/re-derivation rate directly.
4. **Apply.** `skip_reason` counts: `none`=23,870 (100%), no `replay`/`sun`/other skip this run. F8
   self-check (`sun_shadow_apply.py --frame`, the four F8 clusters 16788/19405/22251/24291): median
   own-surface residual per cascade, all comfortably under bias/2 —
   16788: c0 0.041 (bias/2 0.329, 26.9% over bias, n=13,966), c3 -0.096 (bias/2 9.423, 13.3% over,
   n=75), c4 -1.503 (bias/2 36.889, 0.0% over, n=282), c1/c2 no own-surface samples this frame;
   19405: c0 0.043 (26.9%→21.8% over, n=15,964), c3 0.108 (11.0% over, n=3,138), c4 7.981 (0.0% over,
   n=123); 22251: c0 0.029 (n=18,363), c1 0.128 (bias/2 0.634, 4.3% over, n=19,999), c2 0.145
   (bias/2 2.099, 3.2% over, n=205,123), c3 1.299 (7.1% over, n=1,462), c4 2.223 (0.0% over,
   n=1,460); 24291: c0 -0.000 (5.9% over, n=24,178), c3 3.966 (0.0% over, n=13), c4 -1.035 (0.0%
   over, n=1,736). Every `median_over_half_bias` flag is `false` on all four frames/all cascades —
   the self-check passes.
5. **Retention census** (24,296 `shadow_retention_frame` lines, mode `census` throughout, via
   `shadow_retention.py`): peak levels nodes_live 84 / nodes_unseen 14 / records 373 /
   records_unseen 120 / static 24 / moving 84 / refs_held 0 / age_max 7200; `would_c0..3` peaks
   40/40/41/65, `live_c0..3` peaks 51/51/70/110, `capped_c0..3` peaks all 0 (matches the candidates'
   0 capped frames). `us` median 79.2, p99 179.9, max 672.3; `moving_share` median 0.842. Totals:
   `new_nodes`=127,830, `promoted`=11,609, `superseded`=22,619, `reclassified`=392,
   `reclassified_after_unseen`=29, `journal_overflow`=2, `retired`=0, `buffer_gone`/`buffer_orphaned`/
   `box_exit`/`evicted`=0 (no journal/buffer failure, no eviction fired this session). **0 flush
   lines** (`shadow_retention_flush`=0, all reasons including `sun`=0), **0 `sun_relatch`, 0
   `cam_jump`** over all 24,296 frames — no relatch/re-derivation event fired at all, unlike run39's
   2 sun-relatch flushes; positions were never force-reset. Drift: 8,175 verified frames, 73,755
   samples, max 34.23, p99 median 0.0087 against eps 0.05, 61 frames over eps. Resight table (last
   cumulative row, frame 24600): `<60`=16 moved/1 changed/0 same, `<600`=1 moved, `<3600`=2 moved/2
   changed, `<14400`=7 moved/2 changed, `>=14400`=0; `age_cap_below_bucket`=`<60` (same as run39).
   `expired_{retired,box,gone}` all 0 in every bucket — no store-driven eviction fired. 6,304 F8
   `shadow_retention_caster` lines.
6. **Periodicity.** No ~30 s-period signal in the 300-frame-stride `frame_end` sequence or in
   `sun_relatch`/`cam_jump`/flush counts (all zero — nothing periodic to latch onto). The dt_ms trace
   itself shows two multi-window elevated-cost plateaus (frames ≈10200–13200 and ≈22800–24291,
   each several consecutive 300-frame windows at 2x the baseline) but neither repeats on a fixed
   interval and the first coincides with, the second does not coincide with, a toggle burst — not
   periodic, and the 300-frame (~10 s at this framerate) stride is too coarse to rule out a
   sub-300-frame stutter. A finer per-frame `frame_end` diagnostic (every frame, not every 300) is
   what one more launch should record to settle periodicity.

Files changed: `docs/verification/directional-shadows.md` (this section only).

## Run 40 A (run116) diagnosis

2026-09-18, no source edits, no Wine. Inputs: run116 (`/tmp/x3-bottleX3-run116`), the four F8
bursts 16788–16795 (distant station, 50–216 u/frame), 19405–19412 (distant, 25–154 u/frame),
22251–22258 (outpost at 2.2 km, slowing 5 → 4 u/frame), 24291–24298 (same place, 0–1.9 u/frame,
after the 23115/23166 toggle pair and a 24136–24290 move), the per-cascade maps, `depth_*.rg32f`,
`sun_shadow_apply_params`, `shadow_replay_map_basis`, `shadow_replay_candidates`,
`shadow_replay_depth`, `shadow_replay_caster`, `shadow_retention_frame` / `_caster`,
`shadow_replay_sun_point`, `camera_state` (`t=` differenced for speed). Twin
`verification/probe/sun_shadow_apply.py` (`expected_factor_cascades`, coarse derivatives) with
scratch scripts outside the repository. "Measured" = from the capture or code; "inferred" marked.

### Ranked causes

**1. (measured; the whole-object blink) A feedback cycle between the retention store and the
classification ring admits a static-only caster on alternate frames.** Code path:
`classify_candidate_static` (`src/proxy/motion_output.cpp:6613`) returns the store's verdict for
any node the store knows, and a fresh node is `is_static=false`
(`shadow_retention_core.h:306-316`) → Moving → the draw loses its C3/C4 bits and, with no other
cascade, is not leased; `note_retention_draw` records leased draws only
(`src/proxy/motion_output_shadow_retention_inc.h:236`) → the node is unseen at scene end; an
unseen moving node is removed (`shadow_retention_core.h:659`, `Expiry::Moving`); next frame the
store misses, the ring (`shadow_caster_class.h:57-63`, anchor unchanged) says Static → admitted →
recorded fresh → the cycle repeats. Witness (`shadow_retention_frame`, at rest 22260–22266):
`nodes_live` 17 ↔ 33, `new_nodes` 0 ↔ 16, `first_seen_in_range` 0 ↔ 16, `moving_dropped` 16 ↔ 0,
`records` 130 ↔ 146; the same frames' `shadow_replay_candidates`: c3 54 ↔ 70, c4 59 ↔ 75,
`static_only_refused3` 54 ↔ 38, `class_store` 102 ↔ 86, `class_ring` 14 ↔ 30 (16 draws move
between the store's Moving and the ring's Static every frame); `shadow_replay_depth`
`draws3/4` 54/59 ↔ 70/75. Session-wide: 5,791 of 24,296 frames are in such a cycle (period 2:
`new_nodes>0`, previous `moving_dropped>0`, second-previous `new_nodes>0`), 1,999 of the 3,534
at-rest frames (57 %) and 3,792 of the 21,300 moving ones (18 %); 88 runs, the longest 2,043 frames
(10443–12485, at rest, through 17 of the toggle events) and 668 / 617 / 794 frames while moving.
Cohorts of 5–25 nodes per cycle (`promoted` 0 ↔ 5 at 23110–23113: five nodes even promote on
their second sighting and leave the next frame). The toggle does not touch the store, the ring
or the counters (`motion_output.cpp:2100-2117` voids retained bases only): the cycle ran across
both toggle pairs at rest (23040–24135 spans 23115/23166), so the user's "toggle at rest stops
it" is **not reproduced by the counters**; what starts a run is a node meeting only static-only
cascades whose drift falls under eps for one frame pair (see 3), what ends it is its next step.

**2. (measured; the lit-side speckle and its per-frame re-roll) The far cascades' self-shadow
sits on the compare threshold and is re-rolled by the TAA-jittered receiver every frame, at
rest.** Burst 24291–24298 (caster identity sets identical on all eight frames, `far_replayed=1
far_frame=frame` on every frame, C3/C4 basis centres 0 texels apart laterally, 0.1–0.9 u along
the sun): twin flips (`f<1` changing between consecutive frames on pixels owned by the same
cascade in both) C4 **27–37 %** of its 13.3 k owned pixels per frame, C3 0.03–0.14 %, C0 2–5 %
(the kernel rotation). C4-owned pixels are 57.7 % shadowed, 50.8 % "ambiguous" by the twin's
own rule (a compare within EPS_DEPTH); the shadowed ones' residual (receiver − map at the nearest
texel, world units) p10/25/50/75/90 = −144 / 2.6 / 138 / 662 / 3,690, 39 % under one texel (73 u)
and 64 % under four: self-shadow of the same surface. Attribution 24291 → 24292: B's RT2 with A's
rows, maps and kernel flips 36.6 % (the receiver alone; C4 receivers move 149 u median, 573 u p90
in view z between frames under the jitter at 60–70 km, where one pixel spans ≈66 u ≈ 0.9 texel);
A's RT2 with B's rows (a 0.4-u basis shift along the sun) flips 14.6 %; kernel only 14.6 %; maps
91 % of covered texels changed but add nothing beyond the receiver. Constant bias ×2/×4/×8 (2 / 4
/ 8 texels): C4 shadowed 0.49 / 0.38 / 0.27, flips 0.36 / 0.30 / 0.20 — the bias law cannot fix
it. Moving (16788 → 16789): C3 flips 26 %, C4 28 %, maps 99.9 % re-rasterised (grid 4–5 C3
texels, 1 C4 texel per frame), same-kernel-and-maps still 34 / 30 %, bias ×4 no better (C3 35 %).
The F8 self-check in the run116 section above passes because its |r| < 30 u window is blind at
18 / 73-u texels. The 2.2-km outpost has no shadow at all (C1/C2-owned shadowed fraction 0.0 on
24291): its 38-km hull meets only C4's box (`shadow_cascade_bounds_mask`, C4-only mask 16 on 287
of 318 records), so C2 receivers never see it — a separate gap.

**3. (measured; decides who is a far caster) The corner-drift law at eps 0.05 u classifies every
km-scale body as moving.** Live records at 24291: 318 moving, 0 static; every C3/C4 admission is
`large_admitted` (c3 23 = la3 23, c4 310 = la4 310), 231 C4 draws refused per frame. Per-record
centre drift across the F8 frames (census `centre=`): median 4.0 u/frame at rest, 2.0 moving;
the least-moving records are the 19-km station hull parts (model `5411`) at 0.088–0.14 u/frame at
rest and 0.093–0.13 moving. Objects step: the 61 reclassification events carry 0.8–34 u (17 parts
of one station at 2.08 u each on frame 499 at speed 0.04; 17 at 24136 at speed 1.96 with static
18 → 0). Consequence: small distant asteroids (< 1,500 u) never cast, and nodes only in C3/C4 can
never accrue the eight sightings (they are refused, hence unseen, hence dropped: cause 1).

**Ruled out (measured).** Float error of the world-row recovery under camera motion (the
proposed H1 reading): `drift_max` of verified static sightings on frames without a
reclassification — rest median 0.0156 / p99 0.0493, < 5 u/frame 0.0087 / 0.0459, < 50 u/frame
0.0064 / 0.0122, faster 0.0083 / 0.0478, max 0.0493 in every class, all under eps; the recovery
(`world_rows`, double from the float latch) is stable at 100–2,000 u/frame. H2: `capped3/4` 0,
`dropped_min_size` 0, kept sets identical across each burst (one node changed mask 24 → 28 at
16795). H4: `far_replayed=1`, `far_frame=frame` on every burst frame; 275 anchor re-derivations
over the session in 1+4 pairs ~10 frames apart, the last at 21464, none at rest after 22251 or
at the toggles. H5: the basis centre follows the camera 0.1–1.8 u/frame along the sun (6.7e-7 of
C4's 600-km range); it only matters because of the knife edge in 2 (14.6 % flips from a 0.4-u
shift). H6: the > 30-km station records sit at 63–68 km, reach ≈ 0.45 of C4's 150-km half-extent,
outside the 0.85–0.95 band; `class_miss` 0 on all burst frames.

### Fixes

1. Cycle (cause 1). `src/proxy/motion_output.cpp:6613`: use the store's verdict only when it is
   informative — `is_static` → Static; a node with a verified beyond-eps sighting (a `moved` flag
   set in `finalize_seen`, `shadow_retention_core.h:589`, cleared on promotion) → Moving;
   otherwise (fresh, or streak accruing) fall through to the ring. And
   `src/proxy/motion_output_shadow_retention_inc.h:236`: a draw refused **only** by the static
   gate must still reach `store.seen` (rows and extent are known; the lease matters to the live
   mode's AddRef path only), so the streak can reach eight and the node be promoted while
   refused; then `walk_unseen` no longer drops it. Fixture: `shadowpool` static case
   (`verification/probe/motion_output_shadow_pool_inc.h`, `…-pool-static-census/live`) with a
   second static node S2 meeting cascade 1 (static-only) alone: expect `c1` constant and
   `new_nodes`/`moving_dropped` 0 from frame 2 and S2 promoted at frame 9; today it alternates
   `c1` and `new_nodes`=1/`moving_dropped`=1 with period 2 (`POOL_EXPECT`, `:78`).
2. Far self-shadow (cause 2). Replay the static-only (or all ≥ 2) cascades with inverted culling
   so the map holds back faces (`src/renderer/shadow_replay_pass.cpp:277`, per-record
   `r.cull_mode`: CW ↔ CCW, NONE unchanged): a lit-side receiver then compares against the far
   side of its own body, the residual becomes the body's thickness (≫ bias), and the compare
   leaves the knife edge; contact-shadow loss at 18 / 73-u texels is invisible. Fixture:
   `sun-shadow-apply-cascades-5` with the box's lit face as the receiver at texel ≈ pixel
   footprint, all eight jitter indices: lit-face `f` must be 1 on every frame (today it flips).
   Keep the bias law; a texel-scaled bias alone was measured insufficient (×8 leaves 27 %).
3. Classification scale (cause 3). Ring eps per static-only cascade in texels (e.g. the smallest
   static-only cascade met, `texel_world/8`: C3 2.3 u, C4 9.2 u) at `motion_output.cpp:6621` and
   the store's `in.eps` (`motion_output_shadow_retention_inc.h:290`) for its C3/C4 verdict, so a
   station part stepping 2 u or a hull part jittering 0.1 u stays static at 73-u texels.
   Host: `test_shadow_cascades` ring cases at two eps; fixture: `…-pool-static` with a 0.1-u
   jitter node under a 2-u eps.

**Not verified.** The user's toggle observation (no F8 burst exists at rest between a toggle-on
and the next move; burst 24291 came after a move). Deciding capture: F8 at rest immediately
after Ctrl+Shift+F12 on, then F8 after moving and stopping — compare the twin's C4 flip rate and
the `new_nodes`/`moving_dropped` alternation.

## Run 40 A telemetry gaps closed: `--frame-end-stride` and `--shadow-sun-trace` (2026-09-18, worktree `agent-a6f12dc94bd8bda55`)

Both options are default-off and byte-identical when off; neither changes a decision.

1. **`--frame-end-stride N`** (`X3M_FRAME_END_STRIDE`, 1..100000, default 300; no prerequisite —
   `frame_end` is written in every mode). The Present path's `frame_end` line now divides by the
   stride instead of the literal 300; `N=1` writes one line per frame (~100 B) so frame cost can be
   split by toggle state and a sub-300-frame periodic event becomes visible. Capture frames still
   always log one. The other 300-frame reports of that path (chase camera/aim/transition/lead,
   `admission_metric`, `finite_upload_metrics`) keep their own 300-frame cadence, so only the
   `frame_end` volume changes. The DLL reads the variable once at attach (range-checked, malformed or
   out-of-range keeps 300) and writes `frame_end_stride_mode stride=N` only when it is not the default.
   Reader: `frame_end_stride()` in `tools/analysis/analyze_iteration08_loading.py` reports the stride a
   log was actually written with, per device (most common positive frame delta, so a capture frame's
   off-cadence line does not distort it); `analyze_loading_profile.py` carries it as `frame_end_stride`
   and its gap paragraph names the observed stride instead of a hard-coded 300.
2. **`--shadow-sun-trace`** (`X3M_SHADOW_SUN_TRACE=1`; requires `--shadow-cascades`). One
   `shadow_sun_frame device= frame= source= reason= poll= rederived= rederived_mask= carried= checks=
   disagreements= agreement_deg= distance= cascades=` line per frame from the scene end while the
   cascades are on, beside the existing event/F8-only `shadow_replay_sun_point` (140 rows over 24,296
   frames in run 40 A, too sparse to measure the re-derivation rate moving vs at rest).
   `rederived_mask` is new state on `PointSun` (bit k = cascade k re-derived its held direction this
   frame, one OR per re-derivation, nothing on the draw path). Reader:
   `tools/analysis/shadow_sun_frame.py` — exact field list, mask/count consistency checked at parse
   time, `summary()` gives the source/reason/poll mix, the frames that re-derived anything and the
   per-cascade re-derivation rate, and the agreement/distance spread.

Evidence: clean MinGW i686 RelWithDebInfo build, exit 0, 0 warnings.
`verification.analysis.test_frame_timing` (7), `test_iteration08_loading` (11),
`test_shadow_sun_frame` (5, new), `test_shadow_cascades` + `test_shadow_replay_depth` +
`test_shadow_retention` + `test_loading_profile` (44) all pass. `manage.py launch --dry-run --vanilla`
shows `X3M_FRAME_END_STRIDE=300`, `X3M_SHADOW_SUN_TRACE=0` by default and `1`/`1` with
`--frame-end-stride 1 --shadow-sun-trace`. Not yet exercised under Wine or in a game session.

## Run 40 A (run116) fix (2026-09-18, worktree branch off main `f067f5fa`)

Four changes, one logical fix, per the diagnosis above. Architecture:
[shadow-cascade-extents.md](../architecture/shadow-cascade-extents.md) "Caster pool control"
(amendments) and [shadow-caster-retention.md](../architecture/shadow-caster-retention.md)
("Per-cascade tiers"). Option: `--shadow-cascade-backface-from K|none`
(`X3M_SHADOW_CASCADE_BACKFACE_FROM`; default the texel law: every cascade whose world texel is
at least 8 u, so it applies with the static rule off, as the run 40 command does not use it).
Everything else is on the cascade path, except the near-gate change of cause 4, which also
admits origin-behind-camera draws by their extent on the single-map path (its records changed
in hashes and timings only: no fixture draw has such an origin). Note the former default set
(250 / 1,500 / 7,500 / 25,000 at 4096²: a 12.2-u texel on the last cascade) now gets back faces
on that cascade by default. Residual of cause 2: a caster drawn with `D3DCULL_NONE` has no back
side to invert and keeps the knife edge; `cull_none<k>=` / `cull_inverted<k>=` on
`shadow_replay_depth` (while a cascade replays back faces) count the issued far records by cull
mode, so run 41 shows how many two-sided casters remain.

1. **Cycle (cause 1).** `classify_candidate_static` answers per cascade: the store where its
   node is promoted at that cascade's tier (`Node::static_mask`) or verified moved beyond it
   (`Node::moved_mask`), the ring (`Ring::drift`, one drift against every open cascade's eps)
   elsewhere; a fresh node no longer counts as moving. A draw the gate refused from every
   cascade it met is still a sighting (`note_refused_sighting`, `gate_sightings=` on the
   retention frame line), so the node is promoted while refused instead of dropped.
2. **Far self-shadow (cause 2).** The back-face cascades replay with the cull mode inverted per
   draw (`ShadowReplayMapList::invert_cull`, `shadow_replay_cull_mode`: CW <-> CCW, NONE
   unchanged), so a lit face compares against its own far side. The pre-jitter receiver for
   those cascades was implemented as a rows term (`r.z += r.x jx / m00 + r.y jy / m11`, kept
   as `sun_shadow_apply.unjitter_rows`, the measurement tool), measured and **not applied**:
   on run116 at rest (24291 → 24298) the twin's C4 flip fraction is 0.31–0.37 with the logged
   rows and 0.30–0.40 with the term (C3 0.000–0.001 both); moving (16788 → 16789) C3 0.258 →
   0.473 (n 132), C4 0.276 → 0.257 (n 991): the re-roll is in the sampled depth, not the
   lateral texel choice, and the term would evaluate the shadow up to half a pixel beside the
   sample the scene shaded, so far shadow edges would stop being averaged over the jitter. The
   jittered receiver is the TAA-consistent one: the factor modulates the sample the scene
   shaded and the resolve averages the per-sample factors as every other term. The back-face
   map itself cannot be re-rendered offline (the maps are the game's casters): the fixture
   below is its evidence, the run 41 capture the in-game one. Measured trade-off (the faces
   fixture): the contact line of a box standing on the plane, 17 of 872 plane-shadow pixels
   beyond one texel at a 1,171-u texel and 50° elevation (the plane point near the foot
   compares against the box's side face within the constant bias); none on the control half.
   In the game the faces away from the sun, the back faces themselves, have sun share 0 and
   no verdict shows.
3. **Eps per cascade (cause 3).** `shadow_cascade_class_eps`: a static-only cascade's texel / 8,
   never below the base (2.29 u at 37,500 / 4096, 9.16 u at 150,000 / 4096); per-cascade
   streaks in the store, `reclassified_c<i>=` counted per tier.
4. **Huge hulls (cause 4).** Found by replay of frame 24291: the outpost at 1–10 km (RT2 median
   view z 2.5 km, 80 k receivers) had no candidate between 165 u and 74 km; its 312 z-writing
   draws (models `35ba45c3` 105, `35b8bf23` 30, `35b42b44` 15, `4f79` 15, `5419` 14, …) were
   refused although their AABBs meet every box: the object origin lies at or behind the
   camera plane, `fade_route::origin_distance` fails, `candidate_distance = -1`, and the near
   gate (`near_ok = d >= 6`) zeroed the bounds mask. `bounds_near_ok = near_ok || d < 0`: a
   known extent decides alone when the origin distance is unknown. (The `5411` hull of the
   diagnosis is 74 km out and meets only C4 correctly: sun-space x 40–62 km.) The F8
   own-surface window scales with the texel (`own_surface_window`: eight texels, 30 u minimum).

**Evidence.** Clean CMake build (`cmake --build build -j4`) 0 warnings; `check_no_x87.py` PASS,
77 roots, 537 reachable, 0 violations (clean build, 70 objects, DLL `d177f8326536ef2f…`). Host: `test_shadow_cascades`
(the pool policy, the texel law, `shadow_cascade_class_eps`, `Ring::drift`,
`--shadow-cascade-backface-from`), `test_shadow_retention` (the tiers case),
`test_sun_shadow_apply`, `test_shadow_replay_depth`, `test_shadow_replay_candidates`: 59 tests
OK. Offline: the cycle witness at rest 22260–22270 (`c3` 54 ↔ 70, `c4` 59 ↔ 75, `new_nodes`
0 ↔ 16, `moving_dropped` 16 ↔ 0, period 2); the fixed classification cannot be replayed outside
F8 frames (no per-draw rows in the log) and the F8 bursts hold no cycle (`new_nodes` 0,
`moving_dropped` 0 on 24291–24298, 22251–22258, 16788–16795; per-frame centre drift of the 48 /
25 / 41 nodes seen on consecutive frames: 14–18 static at C3's 2.29 u, 18–20 at C4's 9.16 u),
so the fixture is the witness.

New fixture cases (`verification/probe/run_motion_output.py`; records under
`verification/results/bottle-X3/`), each failing on the pre-fix seam DLL built from `f067f5fa`
with the new fixture (`--dll --seam --fixture`):

| case | fixed build | pre-fix witness |
| --- | --- | --- |
| `seam-ownership-shadow-pool-cycle-census` / `-live` | PASS, 193 checks each: `c1` = 3 from frame 1, `moving_dropped` 0, S and S2 promoted at frame 8; M2 (cascade 1 alone, moving) refused and seen every frame: 13 `gate_sightings`, 1.25 / 1.62 µs per sighting (census / live; `X3M_SHADOW_RETENTION_TIMING=1`) | the fixture's own store expectations fail (exit 1) |
| `seam-ownership-shadow-pool-jitter-off` / `-live` | PASS, 143 / 192 checks: J (0.156 u to and fro) admitted to cascade 1 from frame 1; live: the store answers from frame 9 | frame 1 `records [2, 1]`, `static_only_refused1` 1 |
| `seam-ownership-shadow-pool-hull` | PASS, 77 checks: W (origin 2 u behind the camera plane) admitted to both cascades from frame 1, the map twin covers it | frame 1 `records [1, 1]`, `leased` 1 |
| `sun-shadow-apply-cascades-5-faces` | PASS, 2,861 checks: fixed half interior lit-face pixels darkened 0 / flipping 0 (112–117 per frame), silhouette ≤ 2.4 % / ≤ 2.6 %, the back faces (the dark faces) darkened 17–21 % against 97–98 % on the control half, the plane shadow within one texel but the contact line; readback within one FP16 code (`worst_codes` 1.000) | `X3M_FIXTURE_SUNAPPLY_FACES_FIX=0`: `backface_mask` 0 on frame 8, FAIL |

Regression (38 cases, one runner invocation): the cascade / toggle / poll / adaptive / ladder /
wide / far-refused replay cases, the four retention cases, the six pool cases and the four sun
apply cases all PASS with their previous counts (`sun-shadow-apply-cascades-5` 3,117 checks,
`beyond_one_texel` 0; the retention live case 9,743 checks); the pool `static` records change by
design (S enters cascade 1 on frame 1 under every store setting, `class_store` / `class_ring`
follow the informative rule: 182 checks against 181) and the retention records gain the
`reclassified_c<i>` / `gate_sightings` / `gate_us` fields; everything else is the same behaviour.

**Review follow-up (second commit).** The refused-draw sighting path measured 4.2 µs per sighting
with the geometry queries on every sighting (one queried sighting in the first cycle case); it
now reuses the declaration identity the live store already holds for the range and queries
nothing under the census (rows and cull mode only), so only the first refused sighting of a range
under the live store pays the queries: 1.62 µs live / 1.25 µs census per sighting over 13 in the
cycle case (two of them queried), under Wine/FEX. `cull_none<k>=` / `cull_inverted<k>=` count the
issued far records by cull mode on `shadow_replay_depth` (the pool fixtures' 8 / 40-u cascades are
under the texel law, so the fields appear in the game's logs only; the parser is host-tested). The
ladder carry-over of the texel law is host-tested (the corvette's slid 16,875-u cascade crosses
8 u: mask 8 → 12; an index law slides with the policy match). Rerun after the follow-up: 22 cases
(the six pool run116 cases, the six other pool cases, four cascade replay cases, three retention
cases, replay-on, cascades-5, faces) PASS; no-x87 537 reachable / 0 violations; host 59 tests OK.

**Retention summariser cap scope (2026-09-18, run117 triage).** `tools/analysis/shadow_retention.py`
reduced the per-cascade `would_c<k>` / `capped_c<k>` / `live_c<k>` tail over `range(4)` while the
lines carry five cascades, so the record-capacity cap — which in run117 fires only on c4 — was
invisible in `capped_peak` and unchecked by the `capped <= would` identity. Fixed with a `CASCADES`
constant; `capped_total` and `capped_frames` were added so the summary states the same figure a
raw grep does. On `run117` (session-20260918-025333-212.log, 26,466 frame lines) the summary now
reports `capped_peak=[0,0,0,0,41]`, `capped_total=[0,0,0,0,5917]`, `capped_frames=[0,0,0,0,435]`,
matching `grep -o 'capped_c4=[0-9]*'` (435 nonzero frames, sum 5,917, max 41). The field is the
retention store's record-capacity cap and is distinct from `shadow_replay_candidates`' draw-selection
cap, which is `[0,0,0,0,0]` in the same run. Host test: `test_capped_covers_the_fifth_cascade`.

## Run 40 A (run117) diagnosis: residual lit-station flicker (2026-09-18, worktree `agent-ae3e1d2f57d104caf`)

No source edits, no Wine, no build. Inputs: `/tmp/x3-bottleX3-run117` (installed run40 candidate
from `e8ae3357`; five cascades 250 / 1,500 / 7,500 / 37,500 / 150,000 at 4096², back faces in C3/C4),
the four F8 bursts 5778–5785, 11333–11340, 14780–14787 (far-only outpost, C3 z p50 37.6 km),
24624–24631 (station, C3 z p50 21.2 km), `shadow_map<k>_1_<f>.r32f`, `depth_1_<f>.rg32f`,
`sun_shadow_apply_params`, `shadow_replay_map_basis`, `shadow_replay_caster`, `camera_state`. Twin
`verification/probe/sun_shadow_apply.py` (`expected_factor_cascades`, coarse derivatives) driven by
scratch scripts outside the repository. The triage note's premise (far-map churn 55–86 % per frame)
is answered first; the measured cause is on the receiver side.

### 1. The map churn is a comparison artefact; the caster pass is stable

The camera moved in every burst (basis centre drift per burst 15–73 / 102–123 / 172–192 / 173–183 u;
14780: ≈ 156 u/frame, so the C3 grid stepped 6–7 × 3–4 texels and C4 1–2 × 0–1 per frame). Raw
same-texel deltas, burst 14780: C3 707–729 k occupied texels, 36–41 k occupied↔empty flips (5.5 %),
|Δz| > 1e-4 on 555–606 k (83 %), > 1e-3 on 145–160 k, > 1e-2 on 21–25 k, at most 19 texels bit-equal,
p50 |Δz| 2.7–3.0e-4; C4 59.4 k occupied, 3.6–6.6 k flips, > 1e-4 on 38–49 k, p50 1.7–2.1e-4. The p50
is exactly the camera's advance along the sun axis over the depth range, (Δcentre · forward) / R =
2.66e-4 (C3, R 375 km) and 1.66e-4 (C4, R 600 km): `shadow_replay_basis` snaps the centre in x/y
only (`shadow_replay_projection.h:112`), the depth origin follows the camera, and every texel's
stored depth drifts by the same amount (harmless for the apply, which uses the same basis).
Re-aligned (shift by the snapped centre step in whole texels, subtract the depth-origin drift):
C3 flips 80–100, |Δz| > 1e-3 on 0–5 texels, > 1e-2 on ≤ 1; C4 flips 32–56, > 1e-3 on 15–30, > 1e-2 on
0. Burst 5778 (2–9 u/frame, close station with traffic): C3 aligned flips 629–2,090, > 1e-3 on
1.6–3.3 k (0.2–0.4 %), > 1e-2 on 146–360; C4 flips 95–318 — moving ships, not re-rasterisation
noise. Map storage is not the limit either: normalized sun depth over R in R32F resolves
0.02–0.04 u. The replay's light rows come from the application's unjittered rows (`apply_jitter`,
`motion_output.cpp:3676`); the aligned stability confirms no jitter reaches the caster pass.
Duplicate admission: 0 duplicate (`vb`, `primitives`, `origin`) records among the 284 at 24624 and
the 424 at 14780. The caster-count rise (C4 p50 83 → 251) is the run116 fixes working as designed:
run116 refused 231 C4 / 10 C3 draws per frame under the static-only gate at 24291 (310 admitted);
run117 has `refused=0` session-wide (the store/ring cycle fix and the texel-law eps admit the
km-scale station parts that were cycling), not a defect.

### 2. Measured cause: the receiver's RT2 depth precision at 20–90 km on fine single-sided geometry

RT2 holds device depth z/w in fp32 (`current_depth_ps.hlsl`); the apply reconstructs
z = m32 / (d − m22) with m32 = −6.0000186 (6-u near plane). One ULP of d (2^-24 near 1) is a view-depth
step of z² / 1e8 u: 4.4 u at 21 km, 13.6 u at 37 km, 21 u at 46 km, 85 u at 92 km, and the same
laterally in sun space where the view ray is across the sun (14780: ray · sun = 0.10), i.e.
0.24–0.74 C3 texel and 0.3–1.2 C4 texel. Twin sensitivity to ±1 ULP of the whole RT2 (rows, maps,
kernel unchanged): f changes on 12–15 % of C3-owned pixels and 23–25 % of C4-owned; |Δf| ≥ 2/9 on
9.1 % (24624 C3, 90.8 k owned), 14.2 % (14780 C3, 26.5 k) and 15.7 % (14780 C4, 3.0 k); the kernel
rotation (jitter index + 1) alone changes 0 pixels. The unstable pixels are whole faces, not
silhouettes (pixel-scale map of 24624): partially shaded and ULP-sensitive on 10.4 / 11.9 / 27.5 %
of the owned pixels. On them the map depth at the nearest texel minus the receiver depth is
p25/50/75 = −41 / −8 / +36 u (24624) and −49 / −14 / +80 u (14780): the map holds the receiver's own
surface — single-sided station geometry has no back face to put thickness between receiver and
map (which is why the asteroids stopped flickering and these parts did not) — and the 3×3 texel
depth spread is p50 316 / 384 u (72 u on stable lit pixels): girder-scale structure at 18-u texels
under a 2.9 / 4.7-texel pixel footprint. Every frame re-rolls d by more than one ULP (camera motion
or the jitter), so the re-roll rate above is the flicker.

Apply-side mitigations, twin, |Δf| ≥ 2/9 under ±1 ULP (24624 C3 / 14780 C3 / 14780 C4, base
9.1 / 14.2 / 15.7 %): constant bias + k receiver quanta, k = 2: 8.4 / 10.9 / 13.6, k = 4: 7.7 / 8.7 /
11.2; slope-scaled bias ×1 of the plane term: 4.3 / 11.7 / 9.5; both (k = 4 + plane-noise ×4): 5.9 /
6.3 / 7.0; 5×5 and 7×7 PCF: 4.6 / 9.8 / — and 2.9 / 7.8 / — with 22–58 % more partially shaded pixels;
5-px derivative stencil (not ps_3_0-cheap): 8.2 / 13.2 / —. Selecting C4 instead of C3 by pixel
footprint: 14.9 → 3.0 % ([1.5, 2.5) texels), 7.1 → 2.8, 5.4 → 1.8 (24624), 15.7 → 9.3, 15.4 → 5.9,
8.6 → 2.9 (14780), mean f +0.02–0.05 (coarser). None reaches 1 %: the bias law, the kernel and the
selection only trim a precision problem. The run116 section's "constant bias ×8 leaves 27 %" is the
same limit seen from the other side.

### 3. Fix: receiver depth with ≤ 1e-5 relative error (design decision, not made here)

Needed: |receiver error| ≤ 0.05 texel at 92 km in C4 (≈ 4 u, 4e-5 of z); fp32 linear view depth
gives 6e-8. Options, all changes to the RT2 lane ABI (bytecode transformer
`material_motion.cpp` / `linear_sun_share_inc.h`, TAA resolve, AO linearize, the cascade and
single-map apply, the depth dumps, the twins and the committed apply records): (a) reversed
encoding d′ = (w − z) / w with the vertex variant exporting w − z (formed in the VS from
v.z (1 − m22) − m32, precise) and every consumer decoding z = −m32 / (d′ − (1 − m22)) with the
constant folded on the CPU; (b) a third R32F channel with w (A32B32G32R32F, +8 B/px on RT2);
(c) the share quantized into the integer part of G and frac(w / 2048) in its fraction (0.06 u,
share to 8 bits). (a) keeps the format and bandwidth. Until then the only measured interim is
footprint-aware cascade selection (2.5–5× on C3 receivers at 20–40 km); the proving fixture for the
real fix is a ±1 ULP RT2 invariance check in the apply twin on a single-sided plate with a girder
pattern at 37 km (today ≥ 9 % of its pixels flip; target ≤ 1 %), which cannot pass before the lane
change and was therefore not added.

### 4. The aligned map comparison as a tool (2026-09-18)

The section's alignment arithmetic is now `tools/analysis/shadow_map_diff.py`, so a future burst is
compared without a scratch script: it streams the `shadow_replay_map_basis` lines of the requested
frames out of the session log (never loading it), derives the whole-texel box shift
`(-(Δcentre·right), +(Δcentre·up)) / texel` and the depth-origin drift `(Δcentre·forward) / R`, and
reports per cascade and consecutive pair the occupied counts, the occupied↔empty flips, the texels
past `--eps` with their mean and p50 |Δz|, aligned and unaligned, as text or `--json`. Reproduced on
run117 burst 14780–14787 (`--cascade 3 --cascade 4`, 2.8 s): C3 shift (−6…−7, +3…+4) texels,
depth_offset 2.662e-4, aligned flips 80–100 and |Δz| > 1e-3 on 0–4 texels against unaligned flips
36.0–40.7 k, > 1e-3 on 145–160 k (> 1e-4 on 588.6 k = 83.2 % of the occupied texels at 14780→14781),
p50 2.72–3.00e-4; C4 shift (−1…−2, 0…+1), depth_offset 1.664e-4, aligned flips 32–56 and > 1e-3 on
12–20 (the section's hand figure was 15–30), unaligned flips 3.6–6.6 k, p50 1.69–2.13e-4. Host
contracts: `verification/analysis/test_shadow_map_diff.py` (10 tests, synthetic 64² maps and log).

### 5. Receiver re-roll witness (run117)

Section 2's ±1 ULP experiment is now `tools/analysis/shadow_receiver_reroll.py`, and it runs the
second encoding beside it: the receiver moved by ±1 ULP of the stored fp32 `z/w` (today) and by
±1 ULP of fp32 linear view depth `w` (`shadow-receiver-depth.md`, the proposed `.b` channel). It
reads the frame's `sun_shadow_apply_params` line out of the session log (streamed), the RT2 dump
and the cascade maps, and drives `expected_factor_cascades` (coarse derivatives) six times per
frame; a pixel is flipped when the owning cascade's own 3×3 `f` moves by ≥ 2/9 under either sign.
No Wine, no build, nothing copied out of the burst directory.

`--run /tmp/x3-bottleX3-run117 --frames 24624,14780 --cascade 3 --cascade 4` (9.8 s, device 1):

| frame / cascade | owned px | ULP step (u) | z/w changed | z/w flipped | w flipped | margin p25/50/75 (u) |
| --- | --- | --- | --- | --- | --- | --- |
| 24624 C3 | 90,797 | 4.46 | 14.13 % | **9.07 %** | 0.00 % (0 px) | −33 / −9 / +16 |
| 24624 C4 | 845 | 58.8 | 40.12 % | 21.54 % | 0.00 % (0 px) | −85 / +41 / +207 |
| 14780 C3 | 26,528 | 14.1 | 19.10 % | **14.20 %** | 0.004 % (1 px) | −39 / −14 / +13 |
| 14780 C4 | 2,961 | 21.8 | 34.45 % | **15.74 %** | 0.00 % (0 px) | −191 / +30 / +245 |

The three bolded figures are section 2's 9.1 / 14.2 / 15.7 %, reproduced to 0.03 pp. Under the `w`
encoding the median receiver step falls from 4.5–59 u to 2.0–7.8e-3 u and the flipped class empties
(≤ 0.004 %, below the design's < 0.05 % prediction); `changed` at all falls to ≤ 0.01 %. The margin
percentiles on the flipped class repeat the own-surface finding: at the C3 receivers the map holds
the receiver's own single-sided face within a texel or two (p50 −9 and −14 u against an 18.3-u
texel and an 18.8-u constant bias), so nothing separates receiver from caster and the compare is
decided by the receiver's own quantisation. The blended factor that reaches the screen flips on
the same pixels except inside the C3→C4 blend band at 14780 (12.53 % against 14.20 %), where the
next cascade dilutes the re-roll.

Host contracts: `verification/analysis/test_shadow_receiver_reroll.py` (12 tests) — the two quanta
on a row of depths (z/w: z²/1e8, 13.7 u at 37 km; w: one fp32 ULP, 3.9e-3 u), and a synthetic
single-sided girder plate at 37 km under 18.3-u texels and a 2.9-texel pixel footprint whose map is
its own supersampled depth: 19.6 % of its pixels flip under the z/w quantum and none under the w
quantum (margin p50 −6 u), the design note's § 4 plate case on the host side.
## Receiver depth (RT2 .b) (2026-09-18, worktree `agent-a2fa66402c00c2891`)

Implementation of `docs/architecture/shadow-receiver-depth.md`: `--sun-shadow-receiver-depth
{device,linear}` (`X3M_SUN_SHADOW_RECEIVER_DEPTH`, default `device`) widens the lane's RT2 to
`A32B32G32R32F`; the depth fragment appends `mov oC2.zw, v.y` (3 slots, no literal; the
enforcer accepts rcp/mul/mov with two `oC2` writes), the apply quads read `z = RT2.b` when the
pass finds the wide format bound (`limits.z` / `select.w = 1`, set per frame from
`GetLevelDesc`), TAA and AO admit the format and keep reading `.r`. With the option absent the
route creates `G32R32F` as before and every consumer's bits are unchanged. Fixture build
`build/receiver-depth/d3d9.dll` sha256 `ab4833c8…` (mingw i686, zero warnings, `check_no_x87`
537 reachable / 0 violations); not a candidate.

- Native compile: `current_depth` 35 → 38 words (2 → 3 slots), `sun_shadow_apply` 993 → 998
  words (225 → 226 slots), `sun_shadow_cascade_apply` 2133 → 2138 words (499 → 500 slots;
  `--check` PASS).
- Host: `ReceiverDepthPrecision` (girder plate, 25,600 owned pixels per cascade, 87 % / 75 %
  self-shadowed): ±1 ULP re-roll device 18.8 % (C3 37 km) / 83.5 % (C4 92 km), linear 0.004 % /
  0 %; linear `f` equal to the float64 `f` on 99.996 % / 99.992 %. `test_sun_shadow_apply` 25
  tests, `test_original_sun_share` 5 (the `.b .a` lanes are the control's), structure fixture
  171 rows / 1,711 checks (pixel depth words 7 → 10).
- Sentinels: the ordinary fill writes `c0.wwww = (−1,−1,−1,−1)`; the lane's sentinel program now
  writes `c0.wxww = (−1, 0, −1, −1)` (it wrote `.wxxx`, `.b = 0`, before this change; `G32R32F`
  stores `.rg` only, so its bits are unchanged), so `.b = −1` on uncovered pixels as the design note
  says. The quads keep `valid` on `.r ≥ 0` (the same pixels either way).
- `run_material_motion.py`: 1,510 checks / 82 configurations, `.r` analytic max 2.56e-6, ZFUNC
  EQUAL exact, 1,476 depth samples; clip-w lanes (`.z = .w`) against the analytic `w`: **≤ 3.41
  ULP at 1280×768, ≤ 0.41 at 5120×1440**; the 32×32 configurations reach 52.6 ULP, the
  rasterizer's sub-pixel vertex snapping of the 64-pixel triangle (∝ 1 / width), admitted at 64
  there. The note's ≤ 4 ULP assumption holds at game resolution.
- `run_linear_material.py --original-sun-share`: 648 OSHARE rows, `w_bad=0 w_positive=256` on
  every one (the wide lane target's `.b = .a > 0` survive the share's `.g` write), colour /
  motion / depth byte-identical, max 0.00019 FP16 codes.
- `run_motion_output.py` apply cases run under both encodings: `device` (the committed
  `<name>-fixture.json` records, `depth_encoding=device`) differs from the records before this
  change only in the added per-frame check (+6 / +6 / +29 / +15 / +16), the +1 slot and bench
  nanoseconds: 0 behavioural diffs; `linear` (the `<name>-linear-fixture.json` siblings,
  `rt2.rgba32f`):
  sun-shadow-apply 175 checks worst 0.998 codes; -wide 179 / 0.998; -cascades 4,371 / 1.000,
  edge beyond one texel 28 (as before); -cascades-5 3,132 / 1.000 / 0; -5-faces 2,877 / 1.000.
  One cascades-5 frame-6 pixel sat at 1.00013 codes: an FP16 exponent boundary between the
  reference and the readback; the twin now arbitrates marginal cases by the exact code count
  (`beyond_one_code`), so exactly one code is one code.
- `run_ambient_occlusion.py`: 114 checks, 0 failures; `g32r32f_input_bit_identical` and
  `a32b32g32r32f_input_bit_identical` (the same `.r` beside junk lanes gives the bit-identical
  term).
- `run_sun_share_live.py` (`--dll verification/probe/build/motion-output-seam/d3d9.dll`: the live
  script needs the fixture-seam DLL, the production one fails "sun live needs the HDR/TAA native
  seam"): `shadow_apply` / `shadow_apply_cascades` on the shipping `G32R32F` lane (`format=115`,
  `depth_encoding=device`) and their `-linear` siblings on the wide lane (`format=116`,
  `depth_encoding=linear`): each 6 / 6 TAA frames equal to the CPU reference, quad applied on 4
  frames, frames 0 (replay) and 2 (lane) skipped byte-identical, Reset at frame 4; cascades
  capture frames 1 and 3 owned 4,096 / 0, shadowed 4,096. Record:
  `verification/results/bottle-X3/sun-share-live-receiver-depth.json`.
- `run_sun_share_temporal.py`: both enhanced formats through the depth-draw copy, counts
  16 / 8 / 8 / 2, 210 checks, R32F history equal to every source `.r` bit.
- `tools/manage.py launch --dry-run … --sun-shadow-receiver-depth linear` forwards
  `X3M_SUN_SHADOW_RECEIVER_DEPTH=linear` (the lane's own prerequisites must be on the line).
- Not done: `tools/analysis/shadow_receiver_reroll.py` (the note's captured-data witness) needs
  an F8 burst directory; the flight's `frame_end` delta and burst are the user's.


## Run 40 B (run119) near flicker: the sun-grazing receiver plane on C1 (2026-09-18, worktree `agent-ad56f3afbd1f807a3`)

Inputs: `/tmp/x3-bottleX3-run119` (corvette, live retention, adaptive C0 K 1.5, C0 half-extent 674 u),
burst 2 (16528–16535, the station; C1 owns 90.8 k pixels) with bursts 1 and 3 for the C0/C1 map
comparison, `depth_1_<f>.rg32f`, `shadow_map<k>_1_<f>.r32f`, `sun_shadow_apply_params`,
`shadow_replay_map_basis`, `shadow_replay_caster`, `camera_state`; the twin
(`expected_factor_cascades`, coarse derivatives), `shadow_receiver_reroll.py`, `shadow_map_diff.py`
and scratch scripts outside the repository. No game; Wine only for the two shader recompilations.

### 1. Where the C1 flips are

The ±1 ULP class at 16528 (9,646 px = 10.6 % of C1's owned pixels, own 3×3 f moved by ≥ 2/9) is one
region: screen i p5/50/95 895/947/1168, j 515/689/761 (lower right), view depth 1.41–2.78 km
(p5–p95), receiver quantum z²/1e8 = 0.026 u p50. Its surface is a single plane seen 65° off its
normal (ray · n p50 0.42) with the sun 3.4° above it (|sun · n| p5/50/95 0.051/0.061/0.148,
share 0.31–0.67), planar on 99.8 % (clamp hit 0.2 %), sun-depth slope 25–28 u per C1 texel
(the map's own gradient at the sliver is −27.543 u/texel everywhere, dz/du = 0). The texel under
the receiver is the receiver's own front face: map − receiver p25/50/75 −6.6 / −1.7 / +3.3 u, 70 %
within 8 u (own surface; the 3×3 depth spread is 55 u because the sliver is 5 texel rows wide p50
and the outer taps reach the neighbouring geometry). Against the compare reference the nearest
tap's margin is p25/50/75 −0.08 / +0.17 / +0.67 u (stable lit pixels: +2.04 u = the 2.18-u
constant bias), so the flipping taps sit within ±1 u of the threshold; the 9-tap minimum |margin|
is < 0.033 u on 25 % of the class and < 2 quanta on 38 %.

Frame to frame (same screen pixel, C1-owned in both frames, own f change ≥ 2/9): 30.5 / 28.4 /
20.9 / 28.3 / 26.2 / 26.3 / 20.6 % over the seven consecutive pairs of the burst (blended f
29.4–20.2 %, the on-screen factor 16.5–10.1 %). 5,690 of the 9,646 ULP-flipped pixels change
between 16528 and 16529, but they are 22 % of the 25,415 changed pixels: the ±ULP class marks the
same surface, not the whole re-roll. Caster set and verdicts are stable (triage), the C1 map at
the sliver's world texels is reproducible frame to frame to ±0.24 u (p5–p95 of the aligned
delta once a uniform per-frame offset is removed, §2), and the receiver latch is right: a screen
offset scan of ±1 px in 0.25-px steps moves the own-surface residual by 20 u per 0.25 px in x
and 44 u in y, with the minimum at (0, 0) on three of four frames.

### 2. Why a tenth of the pixels sit on the threshold

Own-surface residual r = map depth at the receiver's texel − plane-predicted depth at that
texel (the plane term at the tap), on the grazing class (cos < 0.1, own surface, planar,
unclamped; 15–17 k px per frame): p5/25/50/75/95 = −3.2 / −2.2 / −1.6 / −0.6 / +0.6 u at 16528,
−3.1 / −1.7 / −0.8 / −0.2 / +0.8 at 16529, −1.6 / −0.4 / +0.5 / +1.0 / +1.9 at 16530; on the
face-on class (cos > 0.5) −0.13 / −0.03 / −0.06 / +0.05 u p50 across the same frames. Two terms:

1. **The plane-fit gradient.** The plate's projection onto the map is compressed by |sun · n| =
   0.06 along its normal, so a 2×2 pixel quad spans dv 0.032 (y) / 0.115 (x) texel across that
   axis while the depth changes 27.4 u per texel along it. The solved gradient gv spreads
   −35.6 / −29.8 / −27.4 / −25.1 / −21.8 u/texel (p5–p95) against the map's −27.5 (fine
   derivatives −33.1 … −22.8): a ±10 % error from the receiver's RT2 noise (0.013 u p50 off a
   local 5×5 plane along the ray, twice the fp32 z/w quantum's share, with dz/dy of 4.5 u per
   pixel) divided by a 0.03–0.12-texel baseline. On the ±1-texel taps that is ±3 u of prediction
   error against a 2.18-u constant bias; r is linear in the sub-texel phase (r = +1.9 (0.5 − fv)
   − 1.3 at 16528, slope 7 % of gv), the signature of a gradient error, not of a lateral offset.
   The RT2 quantum itself reaches the compare amplified by (ray · n) / (sun · n) = 4.5–11
   (p5–p95), i.e. one ULP moves the reference 0.1–0.3 u.
2. **A per-frame uniform along-ray receiver offset of −0.2 … +0.3 u** (fit over all C1
   own-surface pixels: −0.17, −0.13, +0.05, +0.02, −0.20, −0.16, +0.31, +0.07 u on 16528–16535,
   r² 0.02–0.43; the face class drifts with the same sign at 1/12 the amplitude, the amplification
   ratio). It is not the jitter latch (no correlation with jitter_x/y), not the projection
   constants (m22/m32 identical on every capture frame), not the map (stable, above) and not a
   moving part (the world-aligned C1 map delta at the sliver is uniform over the whole class);
   at 1.6 km it is 1.2e-4 of z, the order of the game's fp32 world→clip rounding at 1.6e5-u
   world coordinates (inference, not measured at the source). Amplified ×7 on the grazing
   plane it is the ±1.5-u per-frame drift of r.

Neither the texel snap nor the TAA jitter contributes: `shadow_map_diff.py` aligned shifts are
whole texels (residual 0.000) on C0/C1 in all three bursts; the burst-1 "reslide" of 135.7 u
(24 × 49 C0 texels) is the own ship flying at 135 u/frame with its ship-anchored C0/C1 boxes
(the *unaligned* C0/C1 comparison is the stable one there: 67 % / 1–90 % of texels past 3e-6 with
p50 3.7e-6 / 3.6e-7–1.7e-5, the aligned one 81 % at exactly the depth-origin drift 2.66e-4), and
the receiver is reconstructed at the jittered pixel by design (the latch scan above). A tool note:
on burst 2 the aligned C1 map at the sliver differs from the basis-line arithmetic by a uniform
+3.4 / −6.8 / +0.2 u per frame (1.1e-5 normalized) although the apply's rows and the map agree to
0.2 u on the face class; the `shadow_replay_map_basis` centre/forward reproduce the caster pass's
depth origin only to ~3 u per frame at 73 u/frame of camera motion, which `shadow_map_diff.py`
reports as |Δz| > 3e-6 on 16–18 % of C1's texels. Unresolved; it does not enter the compare.

### 3. Ranking and fix

1. Confirmed, apply-side: the bias law has no term for the plane fit's baseline error, which on a
   sun-grazing plane is a tenth of a 27-u texel slope. **Fix:** every tap's plane term is lowered by
   `slope_texels` texels of |dz/du| + |dz/dv| before its clamp (`sun_shadow_cascade_apply_ps.hlsl`
   `slope`; `SunShadowCascadeInput::slope_texels`, uploaded as texels / size in the cascade's flags
   `.z`; `X3M_SUN_SHADOW_BIAS_SLOPE_TEXELS` / `--sun-shadow-bias-slope-texels`, 0–8, default 0.2;
   `slope<i>` on the params line; the twin's `slope_texels`, absent = 0). Twin on 16528 (owned px;
   ULP flipped; own f ≥ 2/9 between 16528→16529 / 16529→16530; mean C1 shade), slope 0 → 0.2:
   C1 90,852 px 10.62 → **1.36 %**, 30.5 / 28.4 → **15.9 / 15.3 %**, shade 0.237 → 0.144; C0
   0.09 → 0.09 %, 11.5 / 9.4 → 11.1 / 9.2 %; C2 2.72 → 2.61 %, 14.7 / 15.8 → 14.5 / 15.5 %; C3
   2.16 → 2.04 %, 16.3 / 16.7 → 15.4 / 15.8 %; C4 16.5 → 16.5 % (the run117 precision defect,
   unchanged). What remains on C1 is the clamp: the diagonal taps' plane term (1.4 texels × 27.4 u =
   38 u, plus up to half a texel) exceeds the 20.97-texel clamp (34.5 u), and a margin folded before
   the clamp cannot reach them. The unfolded form (a separate `min(slope, clamp)` after the clamp)
   measures 1.12 % / 10.9 / 8.5 % but costs 528 conservative ps_3_0 slots against the 512 the
   program promises every device; the folded form is 508 (499 before). Raising the clamp to 32
   texels with the folded form measures 1.24 % / 12.1 % (twin, 16528→16529): an option for the
   orchestrator, since the clamp is also the non-planar fallback on every cascade.
2. Contributing, not fixed: the ±0.2-u per-frame along-ray receiver offset (§2.2), amplified ×7
   here; its source is not identified.
3. Excluded: caster admission and verdicts (stable), map churn (stable at the sliver), the texel
   snap (whole texels), the jitter latch (scan), the far-cascade precision defect (0.006–0.02 texel
   quanta here).

Cost of the margin on a grazing surface: the compare threshold moves 0.2 × 27 u = 5.5 u along the
sun, i.e. a shadow cast onto a 3.4°-grazing plane retracts by 5.5 / sin 3.4° ≈ 90 u along it (6 %
of a 100-u-tall caster's 1.6-km shadow). A face-on surface gains ~0 extra bias (dz/du, dz/dv → 0); a
plane tilted diagonally to the map axes gets up to √2 over-count from the L1 form |dz/du| + |dz/dv|,
in the conservative direction. The single-map path (cascades off, `sun_shadow_apply_ps.hlsl`)
intentionally has no margin. Slot headroom after the term: 3 of 512. Shader: two
slots per cascade (`abs` modifiers on one `add`, one `mul`; the subtraction rides the plane dot's
third operand); word count 2,133 → 2,170, provenance `verification/results/sun-shadow-cascade-apply-program.json`.

Fixture: `verification/analysis/test_shadow_grazing_plate.py` (5 tests, 0.4 s): a single plane at
1.0–3.7 km, 65° off its normal, the sun 3.4° above it, C1's texel, range and bias law, the map's v
axis along the compressed direction, the plate's own analytic map, and run119's measured receiver
error (±0.02 u of surface noise then fp32 z/w). Slope 0: 5.2 % of the pixels re-roll under ±1 ULP,
12.6 % under a 0.2-u receiver shift, 8.2 % shaded (no occluder: acne); slope 0.2: 1.4 % / 5.2 % /
5.3 %, the clamp-bound taps. With the sun's in-plane direction along the plate's horizontal the
same plate shows 0.2 % / 0.6 % and no dependence on the margin: the class needs the quad's steps
to cross the compressed axis with a component along the ray, which the azimuth scan (0–165°, worst
at 150–165°) sets.

Host checks (all pass): `test_shadow_grazing_plate` 5, `test_sun_shadow_apply` + `test_shadow_receiver_reroll`
34, `test_shader_compiler_provenance` 1, `test_shadow_replay_depth.LauncherGate.test_bias_units_option`
(the new option's default, range and prerequisite). Syntax check of the three changed translation
units with the DLL's flags: clean. Not built, not installed.

Open: the own ship's C0 re-rolls 9–11 % of its pixels per frame at 135 u/frame (its box snaps to
the world texel grid, so the hull's sub-texel phase changes every frame); the C0/C1 alignment
convention of `shadow_map_diff.py` assumes world-static content and misreads the ship-anchored
boxes; the basis-line depth-origin mismatch above.

### 4. GPU evidence (2026-09-18, same worktree)

`run_motion_output.py` fresh build (`cmake --build build --clean-first`: zero compiler warnings; the log's one
"CMake Warning (unused-cli)" is the configure step reporting `CMAKE_TOOLCHAIN_FILE` as unused on a re-configure
of an existing build tree) and the three cascade apply cases twice: at the fixture's slope 0 (the committed
records; identical to `HEAD` in every behavioural key, `checks` +1 for the fixture's new range `require`,
`cascade_program_slots` 499 → 508) and as `-slope` siblings with `X3M_SUN_SHADOW_BIAS_SLOPE_TEXELS=0.2` (the
fixture reads the variable, prints `slope_texels=` on `SUNAPPLY_CONFIG` and `slope<c>=` per cascade; the twin's
`parse_cascade_params` reads it; records
`verification/results/bottle-X3/sun-shadow-apply-cascades{,-5,-5-faces}-slope-fixture.json`). GPU against the
twin, worst FP16 codes (slope 0 / 0.2): cascades 0.9999969 / 0.9999969, cascades-5 0.9999969 / 0.9999969,
5-faces 0.9999977 / 0.9999977; violations 0 / 0 on every frame; ambiguous_max 1742 / 1842, 1755 / 1778,
261 / 259; edge mismatch 1264 / 1266 (beyond one texel 28 / 28), 722 / 721 (0 / 0); the faces predicates hold at
0.2 (fixed half: lit darkened ≤ 1.6 %, interior 0, dark shadowed ≤ 14.5 %; control half dark shadowed ≥ 97 %).
The compare set differs between the two runs on 27 of 29 and 14 of 15 frames (the margin moves taps); the worst
code does not: the program and the twin agree on the new term within one FP16 code. Quad medians differ
within run noise (slope 0 vs 0.2: 762 vs 727 µs device, 742 vs 770 linear, 1,753 vs 1,773 five-cascade), and
the slope-0 rerun itself moved 646 → 762 µs against the committed record (fixture timing on the synthetic
device, not game FPS); the frame-0 `us.max` of the first case of a run is the device's warm-up spike (185 ms), so the committed baseline was re-run with the faces case first: cascades slope-0 `us` 608 / 737 / 12,923 µs min / median / max.

After the merge of the RT2 `.b` receiver (main `62221253`, merge `29e26158` in this worktree): program
recompiled (word count 2,175, conservative slots 500 → 509 of 512), clean rebuild with zero compiler warnings,
and the twelve cascade apply cases re-run under the lock: the six slope-0 records (device and `-linear`) match
the committed ones in every behavioural key (`checks` +1 for the fixture's slope range `require`, slots 500 →
509); the `-slope` and `-slope-linear` siblings at 0.2: worst FP16 codes cascades 0.9999969 / 0.9999969,
cascades-5 0.9999969 (device) / 1.0001320 (linear, the committed linear baseline's own figure, within main's
two-sided `beyond_one_code`), 5-faces 0.9999977 / 0.9999977; violations 0 on every frame; ambiguous_max 1842,
1778, 259; edge beyond one texel 28, 0. Host: `test_shadow_grazing_plate`, `test_sun_shadow_apply`,
`test_shadow_receiver_reroll` (43), `test_shadow_replay_depth` (13), `test_shader_compiler_provenance` (1) OK.

## Receiver depth default flipped (2026-09-18, worktree `agent-a6ec2f585ad680ead`)

Run 41 A2 (`/tmp/x3-bottleX3-run124`) ratified `linear`: per `shadow-receiver-depth.md` §5 the device
path is deleted. The lane's RT2 is `A32B32G32R32F` unconditionally (`lane_depth_format()`; R32F off the
lane); `capture.cpp` reads no `X3M_SUN_SHADOW_RECEIVER_DEPTH`; both apply quads read `z = ds.b` with no
`cmp` (`limits.z` / `select.w` = 0, reserved; `terms.x/.y` uploaded, unread); `depth_share_format` admits
only the wide format (R32F or `G32R32F` ⇒ `skip("format")`, new fixture checks on both quads) and the
attach gate checks `A32B32G32R32F`; the F8 RT2 readback is always 16 B/px `rgba32f`; `sun_shadow_apply_params`
logs `depth_encoding=linear` as a literal. TAA and AO still admit R32F, `G32R32F` and the wide format
(their `.r` read is format-agnostic; the AO fixture's `g32r32f_input_bit_identical` stands).
`manage.py`: `--sun-shadow-receiver-depth linear` is a deprecated no-op (no env forwarded; the queued
run-41 B/C lines stay valid), `device` is refused with a message. The twin keeps `depth_encoding`
(absent = device) for old records and `.rg32f` dumps; the runners run only the linear cases: the eight
`sun-shadow-apply*-fixture.json` records are the former `-linear` siblings (renamed, `case` field
updated), the device records deleted; `run_sun_share_live.py` drops `shadow_apply_linear` /
`shadow_apply_cascades_linear` and requires format 116 on the plain cases.

- Programs (`generate_rigid_motion_pixel.py`, `--check` PASS): `sun_shadow_apply` 998 → 981 words,
  **226 → 221 slots**; `sun_shadow_cascade_apply` 2138 → 2173 words, **509 → 509 slots**: −5
  instructions (the `cmp`, the divide and their moves), +1 `mad` forming `p`, +1 `dsx`/`dsy` pair
  (2 slots each) hoisted before the first `ifc`/`rep` where derivatives must sit; headroom 3.
- Host: `test_sun_shadow_apply`, `test_sun_share_lane` (host mock `SUN_TARGET_HOST_PASS checks=49`),
  `test_motion_readback`, `test_snapshot_x3_run`, `test_shadow_receiver_reroll`,
  `test_material_motion_report`, `test_original_sun_share`, `test_motion_output_runner`,
  `test_shadow_cascades`, `test_linear_sun_share`: 171 tests OK.
- Clean mingw build (`cmake --build build --clean-first`) 0 warnings; `check_no_x87.py` PASS, 537
  reachable, 0 violations.
- `run_motion_output.py` (eight apply cases, `build/d3d9.dll` of this tree): sun-shadow-apply 175 checks
  worst 0.998 codes; -wide 179 / 0.998; -cascades 4,374 / 1.000, edge beyond one texel 28; -cascades-5
  3,135 / 1.000 / 0; -5-faces 2,880 / 1.000; the three `-slope` siblings 4,374 / 3,135 / 2,880, worst
  1.000. Against the former `-linear` records: **0 behavioural diffs** (only `bounds_bench_ns` moved;
  check counts equal, the removed `linear_depth` require replaced by the G32R32F skip check).
- `run_sun_share_live.py` full suite (seam DLL, after the lane-qualification fix; tracked record
  `verification/results/bottle-X3/sun-share-live.json`): 22 / 22 passed, the self test on the wide MRT
  triple (positive, cutout_drop and alpha_mask faults); `shadow_apply` / `shadow_apply_cascades` each
  4 applied frames, frames 0 (replay) and 2 (lane) skipped, `receiver_depth=linear`, `rt2_format=116`,
  shadowed ≥ 3,969; the cascades case logs 2 `depth_encoding=linear` params lines (the single-map case
  has no capture window and proves the encoding by format 116 alone); cascades capture frames 1 / 3
  owned 4,032 / 4,096, shadowed 4,032 / 4,096.
- `run_material_motion.py` passed (82 configurations, 42 wide RT2 / 40 R32F, 1,470 checks, clip-w ≤ 52.6
  ULP on the 32×32 configurations as before); `run_temporal_pass.py` passed; `run_ambient_occlusion.py`
  114 checks, `g32r32f_input_bit_identical` and `a32b32g32r32f_input_bit_identical` kept.
- `manage.py launch --dry-run` with the run-41 B line plus `--sun-shadow-receiver-depth linear`: exit 0,
  no receiver-depth variable in the environment; with `device`: refused (exit 2).
- Lane qualification (`sun_share_lane_inc.h`, review fix): the prerequisite loops check
  `A16B16G16R16F` and `A32B32G32R32F` only (render target + blending, sampled, depth-stencil match;
  `G32R32F` dropped), and the self test draws the production MRT triple (64 + 128 + 128 bits) with a
  wide sampled-copy target; its depth reads compare `.rg` of each 16-byte texel. R32F lane-off case kept.
- Not changed: `linear_material_fixture.cpp`'s gained-variant lane target stays `G32R32F`;
  `sun-share-live-receiver-depth.json` (the gated A/B record) is history.

## Run 174 (Argon Prime): shadows off on 96 % of frames; invalid-share stamp (2026-09-19, worktree `agent-a505bb9b339475fd9`)

Diagnosis from `/tmp/x3-bottleX3-run174/session-20260919-203646-212.log` (local, 800 MB, queried only):

- `sun_shadow_apply_frame`: 18,585 x `applied=0 skip_reason=lane`, 726 x `applied=1 skip_reason=none`. No
  other skip reason occurs. `sun_shadow_lane_frame`: `available=0` on exactly the 18,585 frames with
  `untracked_writers>0` (`failed=1 owner=1`), `available=1` on the other 726.
- `sun_shadow_lane_refusals`: every row is `unregistered=N`, all other buckets 0, `signatures=1 overflow=0`;
  N = 1/2/3/4/5 on 14,055/3,307/871/337/15 frames (24,705 draws). The single `sun_shadow_lane_writer` line is
  VS `ac2319bc3953efc6` / PS `03a16e5c63daa6e8`, `reason=unregistered gate=3 registered=1 z=1 zwrite=1
  declaration=96b83ce555c1cf64 stride=40`. The earlier attribution is confirmed; the veto is
  `SunShareFrame::draw` (an actual depth writer after the first receiver that the lane did not track).
- The pair is the `adeffects` / `adeffects2s` family (`effect-shader-users.md`: advertising signs, 172 of 173
  materials opaque), vs_2_0/ps_2_0 (VS 512 bytes), `hostable_with_ps_2_0_fragment` in the SM2 census. It is
  `unregistered` because the profile table and the rewriter are SM3-only (`motion_output_profiles_inc.h`:
  "every transformable SM3 pair"; `material_motion.cpp` checks `0xffff0300`); the VS has no row. Hosting it
  would need a new SM2 rewriter class (vs_2_0 varying change, 64-slot ps_2_0 budget); an emissive sign
  receives no sun, so a share producer for it has no value. It is legitimately unroutable today.
- The census's two other opaque unregistered pairs of run 174 (`z_only.fb` `Z_Only_Fast` / `Z_Only_Alpha`,
  both already rows of `depth_prepass_profiles.h`: jittered, never routable) are not lane vetoes: `c78b4c68a87fce74`/null
  (512 captured rows) and `803ebfd17f79e413`/`652a7c5d1e9909a0` (192) are the engine's `z_only` depth
  prepass programs (`asteroid-fog-temporal.md`, `effect-shader-users.md`) and draw with `COLORWRITEENABLE 0`
  (`mask=0` on all their `motion_route` rows), so `sun_color_writer` is false and they are never counted.
  A depth prepass does not break the lane's completeness either: it writes device depth only, and the
  routed colour pass of the same surface re-tests LESSEQUAL against it and writes RT2 itself.
  The refusal total 24,705 is fully accounted for by the one adeffects signature. The adeffects pair also
  draws 64 of its 192 captured rows with `zwrite=0` (non-depth writers, never a veto).

Fix: the veto becomes per draw for a program outside the registry. A scene draw refused at gate 3 as
`unregistered` after the first receiver, which is an actual depth writer (`sun_z_state == 7`), is re-issued
once from `after_draw` (`MotionOutput::sun_stamp_draw`) with the application's own VS, streams, constants
and raster state, and only: a constant PS of the bound programs' shader-model family (ps_2_0 or ps_3_0,
created on first use), RT2 bound with `COLORWRITEENABLE2 = GREEN`, RT0/RT1 masked off, z write off,
`ZFUNC EQUAL`, alpha test / blend / fog / sRGB write off. EQUAL against the depth the same program just
wrote selects exactly the pixels the draw won, so RT2`.g` becomes -1 there: the explicit invalid share of
a plain depth writer (architecture note, "SunShareFrame"), which the apply pass excludes. The sign renders
unshadowed and writes no motion; the rest of the frame keeps its shadows. RT2`.r` keeps the earlier depth
there, as for every unrouted draw before. The draw then counts as `depth_updated` (neither receiver nor
untracked). Fail closed: stencil-enabled, user-memory, unknown-version or SM3-with-null-stage draws, a
failed create/set/draw, and `X3M_FIXTURE_SUN_LANE_FAULT=stamp` keep the old veto (`stamp_refused`); every
attempted write is restored in reverse and a failed restoration quarantines like `undo()`. `pair`, gate-4
and xt-repair refusals keep the frame veto unchanged (reviewed fixture expectations `untracked`,
`shadow_apply`, `cutout_pair`). `sun_shadow_lane_frame` gains `stamped=%u stamp_refused=%u`. Cost: one
flag test per unrouted colour writer; about 25 device calls per stamped draw (1-5 per frame in run 174).
Known limitation: a stamp inside an application occlusion query adds its passing samples to that query.

Evidence (bottle X3, fixture and seam rebuilt in the worktree, `run_sun_share_live.py`):

- Final binaries: seam DLL sha256 `7f70e8e8249e3f8d...`, fixture `647eee5752529340...`. All 24 cases pass
  (`passed=true`, 38.8 s of case time; result JSON kept local). The 22 earlier cases are unchanged and
  report `stamped=0 stamp_refused=0` on every frame, including `untracked`, `shadow_apply` and
  `cutout_pair`, whose frame-2 veto (`pair`, `pair`, `state`) still holds.
- `unregistered` (new): on frame 2, after the receiver, an authored vs_2_0/ps_2_0 pair outside the
  registry draws triangle B nearer than the receiver and the full-screen triangle behind it, then an
  authored vs_3_0/ps_3_0 pair draws B moved right, all as depth writers under alpha test on and RT0 mask 7.
  Frame 2: `available=1 receiver_draws=1 untracked_writers=0 stamped=3 stamp_refused=0`, no refusal or
  writer line. `SUN_UNREGISTERED sign_pixels=406 stamped_pixels=406`: every pixel showing either sign
  colour has `.g = -1` with `.r/.b/.a` byte-identical, and every other pixel (including all pixels of the
  losing draw) keeps all lane bytes. The per-draw device snapshot compare passes for all three draws
  (no `RESTORE_DIFF`, no `what=sun_stamp`); the Reset before frame 4 and frames 4-5 pass with the two
  lazily created stamp programs alive; TAA history as in `positive` (4 histories).
- `unregistered_fault` (new, `X3M_FIXTURE_SUN_LANE_FAULT=stamp` armed around the three draws): frame 2
  `available=0 untracked_writers=3 stamped=0 stamp_refused=3`, bucket `unregistered=3`, writer
  `reason=unregistered gate=3 z=1 zwrite=1`; `stamped_pixels=0` and the lane bytes stay stale: the old veto.
- One earlier run on the same binaries did not pass: `cutout_pair_bias` hit the runner's 180 s timeout
  during device attach (session log ends at `linear_cutout_device`, before `sun_shadow_lane_device` and
  before any draw). It started immediately after another session's Wine shader generator released the
  lock, sharing its wineserver; the identical binaries then passed 24/24 with that case at 1.8 s. Not
  reproduced and not explained; recorded as an attach-time stall outside the draw-time stamp.
- Host: `test_sun_sh*.py` 36 OK (the synthetic witness gained the two cases and seven rejections),
  `test_linear_material_live.py` 17 OK and `test_motion_wrap_states.py` 4 OK (snippet mocks mirror the new
  members), `test_motion_hdr_scene` OK after the stamp moved into `src/proxy/sun_share_lane_inc.h`.
  `test_motion_output_runner` has 2 failures that depend only on `run_motion_output.py` and the test file,
  both untouched here (fade-route-overlay-lightmap cases, legacy HDR exposure map).
- Not verified: native Windows (source uses documented D3D9 only; cross-compiles), and the stamp in a
  flight session. No shader generator table changed, so no `--check` run was needed.

Review fixes (Fable review, no blocker; same worktree). Supersedes the binaries and counts above:

- Order: the stamp now runs after `finish_source_gain` / `finish_hull_gain` (its PS restore assumes the
  application's program is on the device) and still before `restore_jitter`; asserted by
  `test_stamp_runs_after_the_gain_finishers_and_before_the_jitter_restore`.
- oDepth: `renderer::pixel_program_writes_depth` (`sun_share_frame.h`) walks a PS once at registration (SM2+
  operand type 9; ps_1_x `texdepth`/`texm3x2depth`; comments and `def` payloads skipped; malformed = true);
  such a draw keeps the veto. `sun_share_host.cpp` +6 checks (41).
- `COLORWRITEENABLE3 = 0` during the stamp, restored. `sun_shadow_lane_frame` gains `stamped_prims`.
- `unregistered_mid` (new case, `X3M_FIXTURE_SUN_LANE_FAULT=stamp_mid`): the fourth issued state write
  reports failure; per-draw snapshot compare clean (no `RESTORE_DIFF`, no `what=sun_stamp`), frame 2
  `available=0 untracked_writers=3 stamp_refused=3`, `stamped_pixels=0`.
- Tracked record `verification/results/bottle-X3/sun-share-live.json`: 25 cases, `passed=true`, 47.5 s,
  seam `428e99336cca2b5e...`, fixture `1053f9adba7ca42c...`; `unregistered` frame 2 `available=1 stamped=3
  stamped_prims=3`, 406/406 pixels. The attach-time stall did not recur.
- EQUAL-depth flight check (no runtime query added): in a capture window, every frame with adeffects
  `motion_route` rows at `zwrite=1` must show `stamped` equal to their count, `stamp_refused=0`,
  `available=1`; in `depth_1_N.rgba32f` the sign pixels (`color_1_N`) must read `.g = -1`. A sign that stays
  shadow-darkened with `stamped>0` would mean the EQUAL re-issue missed (depth not reproduced).
- Prepass table: `4b63594a775cbde0` (89 dwords) and `d2e63b1e5b0e24df` (95), the `z_only_0000.fb` /
  `z_only_0001.fb` copies, added to `depth_prepass_profiles.h`. Evidence flow: rows already in
  `rigid_position_profiles_inc.h` with the same class, fingerprints added to `shader-fingerprints.md`,
  `test_depth_prepass_profiles.py` 4 OK. Byte compare with the existing pair: only dwords 52-53 differ
  (compiler version string in the CTAB comment, 9.10.455 vs 9.15.779).
- `test_motion_output_runner` (2 failures, also on main): the `--light-map-far-fade` merge added the two
  `seam-taa-fade-route-overlay-lightmap*` twins and four `seam-*lightmap-far-fade*` cases without updating
  the test's expected lists and counts; lists updated, 12 OK. Host: `test_sun_sh*.py` 37 OK,
  `test_motion_*.py` 135 OK, `test_linear_material_live.py` 17 OK.


## Run 48 B: Argon Prime sun-lane stamp flight (2026-09-20)

Run180, bottle X3 / CrossOver Preview / arm64 Wine/FEX,
`FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`: all 128 captured frames across four
bursts have lane available=1 and sun-shadow apply applied=1/skip_reason=none.
The user did not specifically inspect advertisement signs and noticed no issue;
this proves captured-frame availability, not a complete sign-artifact verdict.
The two extra bursts are The Hole and Atreus' Clouds by user chronology. Fog
on/off captures and timing limitations are in the [fog ledger](volumetric-fog.md#run-48-b--run180-2026-09-20).
Local reproducible analysis: `verification/results/run48b-triage/reproduce.py`.

## Run 62 (run222) off-screen caster drop: the view inverse is a transpose of a 16.16-quantised rotation (2026-09-22)

Diagnosis only; no source change, no Wine. Evidence `/tmp/x3-bottleX3-run222` (local), compact record
`verification/results/run222-shadow-caster-drop.json`; scratch scripts not committed. Symptom and fog side:
[volumetric-fog.md](volumetric-fog.md), "Run 62 fog flight diagnosis (run222)".

**Cause (measured).** The static class is decided on the object→world rows that
`shadow_retention::world_rows` (`src/proxy/shadow_retention_core.h:216-227`) recovers as
`world = r·(view − t)`, i.e. with the **transpose** of the latched view rotation as its inverse. The latched rotation
is the engine's 16.16 fixed-point basis (`r·65536` is integral to log precision), so it is orthonormal only to
`max|R·Rᵀ − I|` median 1.24e-5, p99 2.1e-5 (6,001 frames). The transpose error is `(R·Rᵀ − I)·t`: at the session's
`|t|` (median 37 km, run222 burst 32.5 km) median 0.39, p90 0.59, max 1.06 units, 8–20 × `eps = 0.05`, and it re-rolls
whenever the orientation changes by one LSB. It is not float noise, not a re-key, not rotating parts and not a
sector re-base: it is a common-mode pseudo-motion of the whole world, proportional to the eye's distance from the
sector origin. The architecture note's precision model (≤ 0.03 u) and fixture case l used orthonormal cameras.

| Measurement | Result |
| --- | --- |
| Station | model `000053aa`, handle 23824, serial 23823 (serial 68097 after the reload, burst 31040), 28 records, 97,855 primitives, world AABB half ≈ (3103, 1538, 10176), `class=moving streak=0 static_mask=0 moved_mask=31` on all 8 frames of every burst it appears in; absent from burst 9152 (0 records) |
| Burst frame lines | 9152: `nodes_live=6 static=0 moving=6`; 9539: `nodes_live=15 static=0 moving=15`; 31040: 14 / 0 / 14; `nodes_unseen=0` in all three. All 3,173 `shadow_retention_caster` records of the five bursts are `moving` |
| Apparent motion of station-class records, consecutive frames (production rows, record centres) | per burst median 0.51–1.07 u/frame, max 1.38–3.35; the player-ship nodes move exactly with the eye, as expected |
| Same centres corrected to the exact inverse, `c' = (r·rᵀ)⁻¹·c` | median 0.003–0.008, p90 ≤ 0.018, **max 0.025 u** over 1,456 record pairs (log-precision floor ≈ 0.005): every one under `eps` |
| Rigid common-mode fit (translation + rotation about the eye) to one station, burst 7910 | residual 0.0004–0.007 u of 0.5–1.3 u; fitted rotation 4–20 µrad = the 1/65536 quantum |
| Frame-level law, 30,032 frames | live static share 0.35 on the 9,948 frames whose predicted error step `‖Δ((R·Rᵀ−I)t)‖` < 0.005 u (orientation bit-frozen) and **0.00** on the 19,994 frames above it; 529 of 539 promotions follow ≥ 8 bit-frozen frames; 444 of 511 reclassifications fall on a predicted step ≥ 0.05 u. Eye speed and turn rate alone do not separate the classes |
| Frame 31045 (camera bit-identical to 31044) | every record delta exactly 0: the recovery is deterministic, the error is the rotation's |

**Is retention working in flight? No, and it never has.** `static=0` on 15,577 of 30,035 frames; the rest are
parked or orientation-frozen stretches plus unseen statics kept from them (`nodes_unseen>0` on 12,146 frames).
`moving_dropped` 1,646, `box_exit`/`age`/`evicted` 0. Same law in older flights: run116 (first census) live static
share 0.20 bit-frozen / 0.00 otherwise, 11,596 of 11,609 promotions after ≥ 8 frozen frames; run212 0.70 / 0.00,
373 of 381. `static=0` frames: run116 3,545/24,296, run117 8,786/26,466, run200 25,863/42,836, run214 10,976/28,776,
run220 18,909/48,881, run221 7,916/19,386. The run116 diagnosis' "ruled out" drift check looked only at sightings
that were already static, i.e. at frozen orientations (survivorship). `classify_candidate_static`
(`src/proxy/motion_output.cpp:7179`) feeds the same rows to the ring, so the static-only far-cascade pool is
affected the same way.

**On-screen shadows, not only fog (measured on the dumped maps).** Occluder texels 9152 → 9539: C0 13,860 → 158,002
(137,703 at the station's depth, ≈ 11.4 km sun-ward of the ship), C1 393 → 1,361,826 (32 % of the map), C2 1,048 →
288,866, C3 41 → 30,264. CPU twin of the apply (`expected_factor_cascades`, the frames' own `sun_shadow_apply_params`
and RT2): shadowed pixels (f < 0.5) 3,483 of 46,876 valid at 9152 against 9,749 of 45,979 at 9539. Frame 9152's
receivers evaluated against 9539's maps (rows re-expressed through both latches with the exact inverse): C2-owned
shadowed pixels 2 → 91 of 132. C0 is not comparable across the bursts (own-ship pose changed), so the C0 share of
the 3,483 → 9,749 difference that is the station is inferred, not isolated.

**Fix (recommended, not made).**
1. Replace the transpose by the exact inverse of the latched rotation (adjugate / det in double, as
   `camera_translation_clip` already does, `src/renderer/camera_reprojection.h:132-142`; its comment's "~1e-7" is
   1.2e-5 on this engine) in **one shared helper computed once per latch**, and use it at every world-recovery site
   together, because live casters, the box centre and the receiver rows cancel the error today only by sharing the
   same pseudo-world: `shadow_retention_core.h:216-227` and `:233-235` (`camera_position`),
   `src/renderer/shadow_replay_projection.h:708-719` (`shadow_cascade_draw_rows`), `:103-104`, `:136`, `:170`,
   `:587-599`, `src/proxy/shadow_replay_sun_point.h:145,168-169`, `src/renderer/static_previous_rows.h:80-81`. Changing
   `world_rows` alone would put retained casters in true world and live casters/receivers in the pseudo-world, a
   0.4–1 u offset = 3–8 C0 texels. Today a retained caster already replays with frame A's pseudo-world inside frame
   B's: the same offset, hidden because almost nothing is retained.
2. Keep `eps`, the 8-sighting streak and node keying as they are; with 1 the station is static from its ninth
   sighting (measured ≤ 0.025 u). Holding moving-class large casters for N frames is not needed for this symptom
   and stays stage 3.

Risks: a working retention is new behaviour in flight — stale shadows of objects that depart or dock while unseen
without a retirement (bounded by `age_cap` 7,200 frames, `box_exit`, the orphan probe), up to 1,024 nodes / 4,096
records held (fixed storage, 1.85 MB, unchanged), `would_c3/c4` issues rising towards the caps (budget 640 issues,
2.16 ms), first-look pop-in unchanged. Per-frame cost of the helper: one 3×3 inverse per latch. Windows: pure CPU
arithmetic, no API.

Fixture that proves it: host `test_shadow_retention.py` case l rebuilt with a 16.16-quantised, non-renormalised
rotation at |t| 33 km and 81 km, orientation stepping ≥ 1 LSB every frame: drift ≤ eps and promotion on sighting 9
(fails today with drift ≈ 0.4–1 u, never promoted); motion-output fixture case a with the same quantised rotating
camera: blob within 1 texel of the twin for 600 unseen frames, plus a live-against-retained C0 alignment check
(same node, seen then unseen, blob shift ≤ 1 texel) to guard fix 1's all-sites requirement.

## Run 62 fix: one exact world-from-view basis per latch at every recovery site (2026-09-22)

Source change, not installed. Fixes the cause of the section above.

**Change.** `renderer::camera_world_basis` (`src/renderer/camera_reprojection.h`): the inverse of the latched rotation,
adjugate over determinant in double, computed once in `camera_state_from_matrices` and carried in `CameraState`
(`wv[9]`, `wv_valid`; a hand-built state computes on demand; a singular one yields a NaN basis that every
consumer's finite check refuses). No `fabs`, one `divsd`. It replaces the transpose at every site that recovers
world space from a latch, so live casters, retained rows, box centres, receivers and the TAA static rows share one
world: `shadow_retention::world_rows` and `camera_position` (hence `classify_candidate_static` and the class ring),
`shadow_replay_basis` (eye and forward), `shadow_replay_light_rows`, `shadow_replay_view_rows`,
`shadow_cascade_bounds_suns` (eye and the view→sun rows), `shadow_cascade_draw_rows`, `PointSun::draw_origin` and
`decide`, `static_previous_rows`, the `shadow_replay_caster origin=` capture line, and `camera_translation_clip`
(which already used the inverse; its "~1e-7" comment now reads 1.2e-5). Already exact and left alone:
`fog_world_inverse` (`fog_volume_math.h`, same arithmetic) — fog and the shadow maps now agree on world space, which
they did not before. The sun-occlusion branch is unmerged and not covered. `eps`, the 8-sighting streak and the node
keying are unchanged. Cost: one 3×3 inverse per latch; the per-draw sites read nine doubles instead of converting nine
floats. Windows: CPU arithmetic only.

| Fixture | Before | After |
| --- | --- | --- |
| Host `test_shadow_retention.py`, new case: 16.16 non-renormalised basis (Gram error 1.9e-5), yaw/pitch stepping every frame (609 of 609 latches differ), eye flying, 33 km / 81 km | recovered drift 1.228 / 3.011 u per frame, promoted 0, held 0 of 600 (8 failed checks) | drift 0.0025 / 0.0065 u (eps 0.05), promoted on sighting 9, held and issued 600 of 600 unseen frames, `reclassified`/`moving_dropped`/`box_exit` 0 |
| Same case, live `shadow_cascade_draw_rows` against the retained rows under the same frame's cascade-0 basis (0.78 u texels) | not measurable (nothing retained); with only `world_rows` fixed and the other sites on the transpose: 1.03 / 2.54 texels | 0.0025 / 0.0054 texel; asserted ≤ 0.25 |
| Host `test_static_previous_rows.py`, new case: quantised pair one step apart, object at view z 600, 24 steps | 0.63 / 1.53 px and the depth check fails (7.7e-5 > 2e-5) | 0.0013 / 0.0037 px (limit 0.05) |
| Existing precision twin (orthonormal cameras, 81 km) | 0.0118 u | 0.0062 u |

**Wine (bottle X3, seam DLL built by the runner from this tree).** The retention script's camera is now the engine's:
rotation rounded to 16.16, yaw wobbling 0.002° a frame (`Fixture::camera_quantised`), 81 km out, settling frames
included; the Python twin (`shadow_replay_depth.world_basis`) inverts exactly. `seam-ownership-shadow-retention-live`
9,743 checks, 39 compared frames (28 with retained blobs), 78 maps, 0 coverage disagreements on clear texels, max depth
error 5.5e-6, scene-end full-store median 48.1 µs; `-census` 9,740; `-off` 6,629; `-live-poll` 9,890 (1.5e-5). Case a
holds both nodes through its 600 unseen frames. Neighbours: `seam-ownership-shadow-pool-static-live` 182 checks, 27
maps, 4.4e-5; `seam-taa-unmatched-static-node` 95 checks, max 0.00066 px. Partial runs, no cross-case comparison. One
validator relaxation, retention script only: with edges off the snapped grid the covered *count* of a ~40-texel blob may
differ by its edge texels (44 against 46 with 12 within 1/16 px of an edge); the assertion now accepts a count
difference up to the ambiguous-texel count when no clear texel disagrees and the depth is within tolerance. No Wine
"before" run was made; the host cases carry the before numbers.

Other checks: affected host modules 10 / 116 tests pass; full default host suite 229 modules / 2,260 tests, 0 failing;
scratch production build (MinGW i686, RelWithDebInfo) 0 warnings; `check_no_x87.py` 549 reachable functions, 0
violations.

**New behaviour in flight, and its bounds.** Retention engages for the first time while the camera turns.
- *Stale shadows.* A destroyed or docked object is retired through the lifetime journal at once, and an object seen
  again elsewhere is reclassified (`reclassified_after_unseen`). The residual is an object that was static for nine
  sightings, then leaves **while unseen and stays unseen** (a parked ship undocking behind the player): its shadow
  stays until `age_cap` (7,200 frames: 2 min at 60 fps, 4 min at 30), `box_exit` (2 × the outermost box) or a buffer
  change / the orphan probe. Stations, the case that matters, do not move. The cap is adequate as a default and should
  not drop yet: lowering it re-creates the pop-out this fix removes for any station behind the player longer than the
  cap. It stays tunable (`X3M_SHADOW_CASTER_RETENTION_AGE`, 1..10,000,000); the 300-frame `shadow_retention_resight`
  buckets (`bN_moved` against `bN_same`) are the calibration, and they were never populated in flight before, so the
  next flight is the first real measurement. Drop the cap only if an old bucket shows a material moved share.
- *Issue growth.* Retained records add issues towards the cascade caps and the 640-issue budget;
  `far_alternate_due_to_retained`, `capped_cN` and `would_cN` on the frame line show it. Storage is fixed (1,024 nodes /
  4,096 records, 1.85 MB). Not measurable before a flight; if the far cascade alternates because of retained issues,
  that is the next thing to tune, not the class law.
- *Per-frame cost.* The store's scene-end walk now has unseen nodes to mask and check in flight (fixture: 48 µs median
  with a full store; run222 frame lines give the real figure via `us=`/`walk_us=`).
- *TAA.* `static_previous_rows` no longer shifts unmatched-static rows by 0.6–1.5 px (at z 600) per orientation step
  far from the origin; expect less shimmer on first-frame statics, no new risk.

**Proof of engagement for the next flight.** New cumulative line `shadow_retention_summary` every 300 frames and once at
teardown (`final=1`): `frames static_frames unseen_frames retained_frames static_nodes moving_nodes unseen_nodes
unseen_max retained_issues retained_issues_max promoted reclassified reclassified_after_unseen moving_dropped retired
box_exit age evicted`. run222's signature was `static_frames` ≈ half of `frames` with static share 0.00 whenever the
orientation moved; a working build shows `static_frames` ≈ `frames` near stations and `retained_frames` > 0 while
turning. Fixture live case: frames 1,535, static_frames 1,354, retained_frames 1,279, retained_issues 15,459.

### Review follow-up (2026-09-22, after merging main f6fef37d)

- **Fog sun direction, one frame.** `shadow_replay_view_rows`' depth row is now the covector `axis·R⁻ᵀ`; the fog pass took
  it as the view-space sun *direction* and `fog_world_basis` applied `R⁻¹` again (≈ 2e-5 rad off, exact before).
  `motion_output_fog_inc.h` now forms the direction as `axis·R` (forward rotation of the basis' world axis), so
  `fog_world_basis` returns the world axis exactly. No other consumer reads a direction out of the view rows (the apply
  and bounds paths use them as position rows).
- **Projection jitter does not reach `world_rows`** (from source and measured): every caller passes `shadow_.rows`, the
  application's unjittered rows (`apply_jitter` jitters a per-draw copy; `g.rows` and `draw_rows()` copy
  `shadow_.rows[window]`), and the latch's `m20`/`m21` are the engine's — the jitter is added at the use sites
  (`camera_scene_.m20 + jitter_x`). run222: `p20=0 p21=0` on all 30,035 valid `camera_state` frames. No change made and
  no jittered host case added, since the input does not exist in production.
- **Singular basis fails closed.** `camera_world_basis` returns a NaN basis (every consumer's finite check refuses);
  `camera_state_from_matrices` refuses a state whose basis does not invert. The transpose fallback is gone.
- **Far-plane `Q`** and its oracle `camera_far_plane_previous_ndc` now use `R_cur⁻¹·R_prev`; identical views give the
  identity. The z-row `1 − 2⁻¹⁶` scaling is untouched. `test_camera_reprojection`, `test_taa_camera_path` pass;
  `run_temporal_pass.py` (bottle X3) passed.
- **Relaxed count rule accounted.** Each case's `map` records `edges_only_maps`, `edges_only_ambiguous_texels`,
  `edges_only_max_count_difference`, `ambiguous_texels`. Live: 1 of 78 maps took the relaxed path (12 ambiguous texels,
  count difference 2); live-poll, census, off: 0.
- **Sun occlusion (landed on main).** `src/proxy/sun_occlusion{.h,.cpp,_core.h}` and `src/renderer/sun_occlusion_pass.*`
  contain no view-rotation arithmetic (`view` there is the engine's view object pointer); nothing to convert.
- **Evidence, one seam build.** One `run_motion_output.py` invocation, seam DLL `c128887b…`, fixture `a0112941…`
  (`verification/results/bottle-X3/motion-output-partial.json`): retention live 9,743 / census 9,740 / off 6,629 /
  live-poll 9,890 checks, pool-static-live 182, unmatched-static-node 95 (max 0.00066 px); numbers identical to the
  first run. Scratch production build 0 warnings, `check_no_x87.py` 567 reachable functions, 0 violations. Ten affected
  host modules 116 tests pass. Full host suite after the merge: 230 modules, 5 failing, all from main's sun-occlusion
  landing and none in files of this change: `test_capture_bloom_lifetime`, `test_motion_wrap_states`,
  `test_motion_hdr_scene` (host doubles lack `sun_occlusion` / `SunOcclusionPass`), `test_lattice_state_capture` (its
  double compiles the `draw_indexed` slice of `capture.cpp`, changed by that landing and untouched here) and
  `test_bloom_programs` (program records pin an older `tools/shaders/generate_rigid_motion_pixel.py` hash). Before the
  merge the suite was 229 / 229.
