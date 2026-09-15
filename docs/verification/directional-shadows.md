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
application change (`shadows=0` still).

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
- Not exercised: the depth-only fallback formats, the 64-record cap, a production-extent map in the game, native Windows.

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
