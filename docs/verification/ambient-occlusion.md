# Ambient occlusion verification ledger

Owning design: `docs/architecture/ambient-occlusion.md`. One entry per run of
`X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_ambient_occlusion.py`
(record `verification/results/bottle-X3/ambient-occlusion-gpu1.json`).

| Date | Commit | Checks | Reference (max abs, five scenes) | Apply law | Chain 1280×768 / 1920×1080 | Verdict |
| --- | --- | --- | --- | --- | --- | --- |
| 2026-09-14 | step 1 (review fixes) | 99 / 0 failures (list, labels repeat for recovery and Reset reruns) | 0, 0, 3.7e-4, 3.7e-4, 3.7e-4 (plane, tilted, sphere, corner, step) | ≤ 1 FP16 ulp; plane bit-exact; FP16 store probe: truncate | 0.83 ms / 1.35 ms (submit 0.11); per-quad breakdown in the record | PASS; cost over the 0.8 ms cap, reported |

Host tests: `test_ambient_occlusion_caps.py`, `test_ambient_occlusion_reference.py`,
`test_ambient_occlusion_report.py` (4 tests; the reference test records the view-angle-horizon plane
measurement 0.9969 mean / 0.8623 minimum). `check_no_x87.py build/d3d9.dll`: PASS with the pass
linked (224 reachable functions, 0 violations); no other object references the pass symbols.
