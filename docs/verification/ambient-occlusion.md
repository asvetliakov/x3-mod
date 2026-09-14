# Ambient occlusion verification ledger

Owning design: `docs/architecture/ambient-occlusion.md`. One entry per run of
`X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_ambient_occlusion.py`
(record `verification/results/bottle-X3/ambient-occlusion-gpu1.json`).

| Date | Commit | Checks | Reference (max abs, five scenes) | Apply law | Chain 1280×768 / 1920×1080 | Verdict |
| --- | --- | --- | --- | --- | --- | --- |
| 2026-09-14 | step 1 | 73 / 0 failures | 0, 0, 3.7e-4, 3.7e-4, 3.7e-4 (plane, tilted, sphere, corner, step) | ≤ 1 FP16 ulp (RNE); bit-exact truncating, plane 100 % | 0.84 ms / 1.39 ms (submit 0.11) | PASS; cost over the 0.8 ms cap, reported |

Host tests: `test_ambient_occlusion_caps.py`, `test_ambient_occlusion_reference.py`,
`test_ambient_occlusion_report.py` (4 tests). `check_no_x87.py build/d3d9.dll`: PASS with the pass
linked (224 reachable functions, 0 violations); no other object references the pass symbols.
