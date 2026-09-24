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
