# Packed screen emission (bullets) — verification ledger

Owning design note: [screen-emission-region.md](../architecture/screen-emission-region.md)
(steps A–C, the step-B bound and its near-plane clipping). Compact records under
`verification/results/bottle-X3/`: `screen-emission-gpu-step-a.json`,
`screen-emission-bound-gpu1.json`, `screen-emission-bound-live1.json`,
`screen-emission-live1.json`. Earlier step A/B/C evidence is summarised in the note's
implementation sections; this ledger starts with the run-15 diagnosis.

| Date | Change / evidence | Commands | Result |
| --- | --- | --- | --- |
| 2026-09-14 | Step B near-plane clipping (run 15: 400/800 bullet draws refused `reason_w`, admitted rectangles 58–90 %; boxes straddle or lie behind the camera plane). `project_box` cuts the locked-prefix box at clip z = 0, `BehindNear` (7) reason, `clipped` counters, capture-only `packed_sample` line. | `PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_fade_region verification.analysis.test_screen_emission_live`; `python3 verification/probe/check_no_x87.py build/d3d9.dll`; `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_locked_prefix_live.py --fixture verification/probe/build/locked_prefix_live_fixture.exe --dll build/d3d9.dll`; `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_linear_distance_fade_live.py --screen-emission --fixture verification/probe/build/motion_output_fixture.exe --dll verification/probe/build/motion-output-seam/d3d9.dll` | Host: 8 + 7 tests OK (`--near` 600 random cases, 0 failures, 0 outside points; hand cases straddle/exact/behind/beam equal to the Python restatement). x87: PASS, 224 reachable functions. Step B live: PASS, 15 frames (9 bound, 6 refused), near_straddle clipped=4 rect (46,10,96,51) 222 ‰, near_exact clipped=4 (46,10,96,63) 287 ‰, near_behind reason 7 refused, near_beam (46,0,96,63) 341 ‰, 11 scans at 24.0 µs. Step C live: PASS, 12 processes; functional 21 frames / 26 sources, 19 eligible / 13 admitted / 12 linear / 1 incomplete / 3 unbounded; brackets s 441, n 986, x 1258, b 1462 px (all < 50 % of 64×64, covering their footprints); witness outside=0 on frames 2,4,5,7,8,10,12,17,18,20; straddle violations {2,7,10,12,18,20: 128, 17: 84}; caps 16 refused; 7 `packed_sample` lines (6 changed). |
