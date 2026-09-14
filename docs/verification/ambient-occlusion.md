# Ambient occlusion verification ledger

Owning design: `docs/architecture/ambient-occlusion.md`. One entry per run of
`X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_ambient_occlusion.py`
(record `verification/results/bottle-X3/ambient-occlusion-gpu1.json`).

| Date | Commit | Checks | Reference (max abs, five scenes) | Apply law | Chain 1280×768 / 1920×1080 | Verdict |
| --- | --- | --- | --- | --- | --- | --- |
| 2026-09-14 | step 1 (review fixes) | 99 / 0 failures (list, labels repeat for recovery and Reset reruns) | 0, 0, 3.7e-4, 3.7e-4, 3.7e-4 (plane, tilted, sphere, corner, step) | ≤ 1 FP16 ulp; plane bit-exact; FP16 store probe: truncate | 0.83 ms / 1.35 ms (submit 0.11); per-quad breakdown in the record | PASS; cost over the 0.8 ms cap, reported |
| 2026-09-14 | step 1b (cost reduction) | 109 / 0 failures | 0, 0, 2.4e-4, 2.4e-4, 4.9e-4 (plane, tilted, sphere, corner, step) | ≤ 1 FP16 ulp; plane bit-exact; FP16 store probe: truncate | 1.06 ms first block / 0.51 ms repeat block (floor 0.50) · 1.33 ms (floor 1.23) | PASS; warm 1280×768 chain within the 0.8 ms cap and at the 0.5 ms acceptance, cold block above it |

Host tests: `test_ambient_occlusion_caps.py`, `test_ambient_occlusion_reference.py`,
`test_ambient_occlusion_report.py` (4 tests; the reference test records the view-angle-horizon plane
measurement 0.9969 mean / 0.8510 minimum). `check_no_x87.py build/d3d9.dll`: PASS with the pass
linked (224 reachable functions, 0 violations); no other object references the pass symbols.

Step 1b notes: four quads (linearize, horizon search, one 2D quincunx blur, apply); largest program 397
of 512 slots (GTAO; linearize 8, blur 93, apply 70). Oracle means moved by at most 0.002 (contact ring
0.9569 → 0.9567, step far side 0.82 → 0.8215; crease near 0.9199, crease floor 0.9639) and the flat-plane
and sentinel identities are exact. Folding the linearization into the horizon search was measured and
rejected on cost (1280×768 chain 1.10–1.18 ms); the record of that variant is not retained, its numbers
are in `docs/architecture/ambient-occlusion.md`, "Step 1b". `check_no_x87.py build/d3d9.dll`: PASS
(224 reachable functions, 0 violations). Host tests: 4, OK.
