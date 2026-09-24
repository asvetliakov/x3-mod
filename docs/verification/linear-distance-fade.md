# Linear distance fade (detached producer) — verification ledger

Owning design notes: [linear-distance-fade.md](../architecture/linear-distance-fade.md) and
[linear-distance-fade-region.md](../architecture/linear-distance-fade-region.md). Detached runner
`verification/probe/run_linear_distance_fade.py`, built by `verification/probe/build_linear_distance_fade.sh`;
compact record `verification/results/bottle-X3/linear-distance-fade-gpu.json`. Earlier green runs are
recorded in those notes and in [screen-emission.md](screen-emission.md) (last green before this ledger:
051b7652, 2026-09-14, 78 cases).

| Date | Outcome |
| --- | --- |
| 2026-09-24 | Chain repaired on main (fixture and build script only; production unchanged). (1) Link: 72e799b0 (hull emission gain, 2026-09-17) made `linear_material_fixture.cpp` call `linear_emission_hull_source_gain_variant` and added `src/renderer/linear_emission.cpp` to `build_linear_material.sh` only; the fade script now lists it too (no other TU missing). (2) Identity: `RESULT FAIL actual native/E alpha identity`, case 0 pair 110, 64 of 256 pixels, native oC0.a 0.125 vs oC1.a 1.0 (oC1 = 0.078125/0.1796875/0.5/1, the motion output). Cause: d8642469 (original fill, 2026-09-16) put `if (mode >= 5 && !pixel) return ...;` between the fade block's `} else` and the transform chain in `Shaders::transform`, so the else bound only that line and the ordinary combined transform overwrote the mode-3 fade variant. Fixture defect, not a fade-law drift: the check is unchanged since bee7d71d. Bisect (bottle X3, measured, `verification/results/linear-distance-fade/bisect.sh`): 051b7652 / 6b18a10f / 155ac545 / 48d70c35 PASS, d8642469 / 36d25a79 FAIL, d8642469 with the 48d70c35 fixture PASS. Fix: the mode >= 5 vertex return moved above the `#ifdef` block. (3) After the fix: both fixture builds 0 warnings (-Werror); runner PASS, 78 cases, 264 source calls, fixture `RESULT PASS cases=71`, `FADE_RESULT PASS reset=1 partial_vs_failures=2`, region 29 cases / 0 violations, prefix 13+1 cases / 0 violations, 5.8 s; host tests test_motion_output_runner + test_screen_emission_live + test_fade_region 47 OK. |

**2026-09-24 Run 80 A launch 4 (run307, `--fade-rt2-owner on`, also `--gpu-sync-timing`): the owner flown.**
`fade_rt2_owner_configured requested=1 enabled=1 lane=1`; `fade_owner=1` on all 5,494 frames; `fade_owner_masked`,
`fade_refused`, `fade_evicted`, `overlay_refused` 0 on every frame; `fade_routed` non-zero on 2,197 frames (max 24 per
frame); no error/late_claim/rollback row; clean exit (measured, `verification/results/run307-run80a-fade-owner/`). In
the two stand bursts (3552 at rest, 4613 rest then a 0.16-0.33 deg/frame pan) every owned RT2 pixel carries `.a`
exactly 1.0 (12,050 and 39,721 pixels; every routed pixel at w >= 222k on 3552 is owned), run304 has none: the owner
reaches the resolve input. No CPU row added; GPU medians against run306 (`taa` 6,528 vs 6,784 µs, `taa_mask_tests`
1,376 vs 1,440, `taa_resolve` 2,624 = 2,624, `scene` 26,112 vs 27,136) show no increase. The user saw no flicker,
trail or pop. **Accepted as the Run 81 default (`--fade-rt2-owner on`).** The sentinel stabiliser stays: it ran at
S = 0.7 and fade-rt2-ownership.md §5 needs an S = 0 flight on the stand (condition 3/4), the crop test (condition 2)
was not run, and `fade_routed` max 24 is below the §5 figure of 62 (a different stand). Run 81 carries the S = 0
launch (`X3M_TAA_SENTINEL_STABILISER=0` with the owner on, bursts at rest and in the pan); the replay crop can be
taken from run307's owner box (x 1979-3109, y 583-950). Open: 27.6k same-depth unowned pixels inside the owner box on
4613 cannot be attributed per draw from the dump.

**Run 81 default (not flown):** `--fade-rt2-owner` defaults on with `--taa --motion-output --hdr` (else not sent, one launcher line); `fade_rt2_owner_configured` now logs for on and off with `default=1|0` (host test `test_fade_region`). Wine (measured, X3): `seam-taa-fade-route-routed` 5100 checks with `requested=0 enabled=0 default=0`, `seam-taa-fade-route-routed-owner` 5185 with `requested=1 enabled=1 default=0`.

**2026-09-24 Run 81 A launch 3 (run311, `--taa-sentinel-stabiliser 0`, owner and vote on): removal rejected for now.**
`sentinel_stabiliser=0.000`, owner on every frame (masked/refused 0), 17,130 frames, clean exit (measured,
`verification/results/run311-run81a-stabiliser-off/`). The user sees the fade-band stations "a little shimmering". Cause:
on every fade-band station node the hull draws are routed and owned, but the alpha-tested cutout draws (solar panels of
the dish plants `4944d81dfe531b37/5e0a10fe752b6140`, truss lines of the three-arm plant `53a0a641107ed76c/63f96eba9eea7880`;
engine state blend + alpha test + Z-write off) stay sentinel-class: `fade_route::state` requires `alpha_test == 0`
(`src/proxy/fade_route_core.h:63-67`) and the opaque chain labels them `unmatched=no_zwrite` (`motion_output.cpp:5549`), so
no fade counter sees them. Stand burst 16568-16575 (rest, then a 4-6 px/frame pan): 24 owner draws and 16 unowned cutout
draws per frame; station detail pixels 3,144 owned / 3,673 sentinel (valid depth 0.46); burst 16126 (static) 2,288 / 10,859
(0.17). Input-side change per frame at rest: owned thin parts 26.2 codes, unowned panels 5.35, jitter-cycle aliasing
(inferred). §5: condition 1 met on the stand windows, condition 2 fails (valid depth 0.17-0.48 vs 0.9), condition 3
undecidable (no `taa_` dumps; the replay tool is 1280x768 only), condition 4 fails. **Decision: `sentinel_stabiliser` stays
0.7.** Retry S = 0 only after the alpha-tested cutout pairs become RT2 owners (a fade-arm extension for alpha-tested,
Z-write-off draws: design question; the sun-shadow alpha-caster lease shows the texture is reachable). A future S = 0 vs
0.7 comparison needs `--taa-debug` dumps of `taa_`/`present_` at the stand and a 5120x1440-capable replay.

**2026-09-24 Fade-band alpha-tested cutouts as RT2 owners (fixture, not flown).**
[fade-alpha-cutout-ownership.md](../architecture/fade-alpha-cutout-ownership.md) option A as ratified. `fade_route::state`
takes an eleventh argument `tested_ok` and admits `alpha_test == 1` beside 0; `fade_arm_admits` passes
`!overlay && fade_rt2_owner_ && !linear_material_requested_` (a fade pair, the owner on, original shading; the overlay
arm and the linear-material path unchanged). Departure from the note's step 3: the opaque chain stops at Z-write off
before it reads ALPHATESTENABLE, so the arm stores its own read in `route.fade_tested` and
`route.alpha_tested = route.fade_arm ? route.fade_tested : test != 0` (the light-map widening skips an owned cutout, W3
sees it). Review fix: `route.native_mip_bias = route.alpha_tested && (shadow_.cutout_pair || route.fade_arm)`, so every
owned alpha-tested cutout keeps the native LOD bias under `--taa-mip-bias` (its alpha coverage matches vanilla and the
unbiased prepass); before the fix only the two `linear_cutout.h` pairs under the sun lane kept it. No texkill, texture
lease, program or other device-state write. Counters: `fade_route_frame ... fade_tested=` (appended after
`fade_owner_masked`), fixture status key 86, `fade_rt2_owner_configured ... tested=` (enabled and original shading).
Per-draw cost: three bool loads on a recognised fade-band draw and one increment on an admitted one; an admitted cutout
pays the routed-owner cost it already pays inside FogNear (7.49 µs lazy, `route-per-draw-cost.md`): 0.12 ms at 16 draws
per frame (inferred, not timed).

- **Wine** (bottle X3, measured; `run_motion_output.py`, one run under `wine_lock` after the review fixes, clean build,
  33 cases exit 0): the `faderoute` script `cutout` over the sentinel fill, original shading, lane RT2: a fade-pair panel
  with a centred 8x8-texel hole drawn with the alpha test on (GREATEREQUAL 1) after its textured z_only prepass
  (`803ebfd17f79e413`, null PS, the same test on the fixed-function texture alpha), and a hull quad of the same pair
  without the test at z .5 behind the panel's left part (its own `c78b4c68` prepass). The fixture classifies every pixel
  of the 32x32 region whose reachable sample positions (+-0.5 px jitter) see one panel class and one hull state, runs
  the engine's sequence for it on the CPU (the Z buffer after both prepasses, each draw's alpha and Z test in draw
  order; `fade_cutout_model` in the runner is an independent copy) and requires each draw's colour coverage, RT2 write,
  RT2 value and RT1 rows, and the frame-final RT2, to match; every other pixel is held to per-draw parity (RT2 written
  exactly where colour is covered; never with the owner off) and every frame-final RT2 change to a colour-covered pixel
  (the subset rule). Per frame, 12 frames, model / final / parity / subset / motion mismatches 0 in every case:

  | Case | Variant | checks | classified | panel / hull pass | RT2 owned panel + hull | final RT2 panel / hull / fill | fade_routed / fade_tested |
  | --- | --- | --- | --- | --- | --- | --- | --- |
  | `cutout-owner` | panel then hull | 450 | 887 | 232 / 36 | 232 + 36 | 232 / 36 / 619 | 2 / 1 |
  | `cutout-refused` (owner off) | same | 449 | 887 | 232 / 36 | 0 | 0 / 0 / 887 | 1 / 0 |
  | `cutout-order-owner` | hull first; prepass ALPHAREF 128, colour 1; alpha-.25 band | 450 | 879 | 224 / 72 | 224 + 72 | 224 / 36 / 619 | 2 / 1 |
  | `cutout-mip-owner` | 2-level texture, LOD 0.74, `X3M_TAA_MIP_BIAS=-0.5` | 462 | 887 | 232 / 36 | 232 + 36 | 232 / 36 / 619 | 2 / 1 |
  | `cutout-mip-nobias-owner` | same, bias 0 | 450 | 887 | 232 / 36 | 232 + 36 | 232 / 36 / 619 | 2 / 1 |

  The refused panel is an uncounted gate-4 refusal (`unmatched=no_zwrite`, `fade_refused` 0), RT1/RT2 untouched; the
  raw colour of every frame equals `cutout-owner`'s (12/12, the routed variant keeps the native colour). `order`: the
  36 band pixels under the hull pass the hull first (the prepass at ref 128 skipped them) and the nearer panel overwrites
  them (final hull 36 = the hole only); owned RT2 stays inside the colour coverage. `mip`: the upper half of the raw
  colour equals the bias-0 twin's on 12/12 frames; witness before the fix (same fixture, unfixed DLL): the case fails
  at the panel draw with `RESTORE_DIFF ... samplers_0`, the jitter bias left on the cutout's alpha stage
  ([mip_bias_witness.txt](../../verification/results/fade-alpha-cutout/mip_bias_witness.txt)); the coverage change it
  would cause (level 0 has no hole) is inferred. All: `unjittered_depth_writers` 0, `jittered` 5; capture-frame
  `motion_route` records in draw order, panel `atest=1 gate=0` (owner) / `gate=4 unmatched=no_zwrite` (off), hull
  `atest=0 gate=0`.
- **Refused twin = pre-change behaviour** (measured before the review fixes, first fixture version,
  [compare_refused_head_out.txt](../../verification/results/fade-alpha-cutout/compare_refused_head_out.txt), script
  beside it): the same fixture against DLLs built from 9eaeb180: 60 dumps (colour, RT1, RT2, presented, reference
  resolve) byte-identical, the FADE_CUTOUT lines, the fade-pair route records and the `fade_route_frame` counters
  (without the new field) identical. Not rerun after the review fixes (the owner-off path is unchanged by them).
- **Existing cases unchanged** (measured,
  [compare_fade_cases_out.txt](../../verification/results/fade-alpha-cutout/compare_fade_cases_out.txt), script beside
  it): the 27 `seam-taa-fade-route-*` cases and `seam-taa-cutout-opaque` against the committed summary: 28 compared, 0
  leaves differ (clock leaves included). Its `passed=False status=PARTIAL` is the runner's status for a selected-case
  subset (no cross-case comparisons), not a failure.
- **Host:** `test_fade_region` 31 OK (state table: `alpha_test` 1 admitted only with `tested_ok`, 2 refused, every other
  wrong field refused with it, blend off / Z-write on included; source contract including the native-bias line),
  `test_motion_output_runner` 20 OK (case lists, the model, the validator with 17 mutations); the six `fade_route`
  modules: 1 failure pre-existing on 9eaeb180 (`test_hull_emissive_widening.test_dll_plumbing`: the
  `lightmap_fade_m00_` line changed in 5a81df27).
- **Build:** DLL 0 compiler warnings, fixture and seam `-Werror`, `check_no_x87` 684 reachable / 0 violations.
- **Flight rows to check** (the run214 / run311 stand, `--taa-sentinel-stabiliser 0`, owner on): `fade_rt2_owner_configured
  ... tested=1`; `fade_route_frame` `fade_tested` >= 16 per frame on the stand, `fade_owner_masked = 0`,
  `fade_refused` / `overlay_refused` 0; no `motion_route ... unmatched=no_zwrite` row for PS `5e0a10fe752b6140` or
  `63f96eba9eea7880` on a stand node (their rows `atest=1 fade_arm=1`); `unjittered_depth_writers = 0`; burst: station
  detail pixels valid depth >= 0.9 (run311: 0.17-0.48), the remaining sentinel detail pixels the panel holes and the edge
  halo; stabiliser class 0 on them; a `motion_route` row with `atest=1 fade_arm=1` on a VS other than `4944d81dfe531b37` /
  `53a0a641107ed76c` is a newcomer to name (note section 2.6).

**2026-09-25, sentinel stabiliser off by default** (Run 82 A launch 2, run313/run314, `--taa-sentinel-stabiliser 0` on the
Run82 DLL with the alpha-tested cutout owner; [run313-run82a-stabiliser-off](../../verification/results/run313-run82a-stabiliser-off/),
scripts beside their outputs): `fade_refused` 0 on all 4,493 `fade_route_frame` rows, no `unmatched=no_zwrite` refusal;
station detail pixels with valid depth 0.970-0.974 on the fog-band plant crop at rest (burst 1673) and 0.911-0.965 in the
pan (burst 2124), against 0.17-0.48 on run311 (the second plant in the pan burst 0.47-0.49); the user saw no shimmer.
`docs/architecture/fade-rt2-ownership.md` section 5 conditions 1, 2 and 4 met; condition 3 (replay) still open, no `color_*`
input was dumped. The launcher now sends `X3M_TAA_SENTINEL_STABILISER=0` with `X3M_TAA_SENTINEL_STABILISER_DEFAULT=1` on
every modded `--taa` launch (`sentinel_stabiliser_default=` on the `motion_output_taa` row; explicit values marker 0, an
explicit 0.7 restores the previous look; nothing without `--taa` or under `--vanilla`); the DLL fallback when unset stays
0.7 under the camera gate, so fixtures are unchanged. Host: `test_taa_sentinel_stabiliser_default` 6 tests; the 55
launcher modules 739 tests OK; build 0 warnings, `check_no_x87` 684 / 0.
