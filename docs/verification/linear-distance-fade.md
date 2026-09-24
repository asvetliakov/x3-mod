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
