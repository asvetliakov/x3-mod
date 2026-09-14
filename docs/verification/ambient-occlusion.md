# Ambient occlusion verification ledger

Owning design: `docs/architecture/ambient-occlusion.md`. One entry per run of
`X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_ambient_occlusion.py`
(record `verification/results/bottle-X3/ambient-occlusion-gpu1.json`).

| Date | Commit | Checks | Reference (max abs, five scenes) | Apply law | Chain 1280×768 / 1920×1080 | Verdict |
| --- | --- | --- | --- | --- | --- | --- |
| 2026-09-14 | step 1 (review fixes) | 99 / 0 failures (list, labels repeat for recovery and Reset reruns) | 0, 0, 3.7e-4, 3.7e-4, 3.7e-4 (plane, tilted, sphere, corner, step) | ≤ 1 FP16 ulp; plane bit-exact; FP16 store probe: truncate | 0.83 ms / 1.35 ms (submit 0.11); per-quad breakdown in the record | PASS; cost over the 0.8 ms cap, reported |
| 2026-09-14 | step 1b (cost reduction) | 109 / 0 failures | 0, 0, 2.4e-4, 2.4e-4, 4.9e-4 (plane, tilted, sphere, corner, step) | ≤ 1 FP16 ulp; plane bit-exact; FP16 store probe: truncate | 1.06 ms first block / 0.51 ms repeat block (floor 0.50) · 1.33 ms (floor 1.23) | PASS; warm 1280×768 chain within the 0.8 ms cap and at the 0.5 ms acceptance, cold block above it |
| 2026-09-14 | step 1b independent review (Opus, read-only rerun) | 109 / 0 failures | 0, 0, 2.4e-4, 2.4e-4, 4.9e-4 | ≤ 1 FP16 ulp; plane bit-exact; truncate | 0.78 ms first block / 0.53 ms repeat (floor 0.51) · 1.33 ms (floor 1.22) | PASS; math, c0..c7 layout, state/Reset and ledger numbers confirmed; no check removed vs step 1 (+8 per-quad counts, +2 sphere linearize); five low findings listed below, none blocking step 2 |
| 2026-09-14 | step 1b review fixes (five findings) | 112 / 0 failures (+3 over 109: the 397-slot boundary attach and its detach, the denormal-m32 refusal) | 0, 0, 2.4e-4, 2.4e-4, 4.9e-4 (plane, tilted, sphere, corner, step) | ≤ 1 FP16 ulp; plane and tilted bit-exact; truncate | 1.11 ms first block / 0.75 ms repeat (floor 0.74) · 1.33 ms (floor 1.23) | PASS; bytecode unchanged (blur program sha256 cb9fd497..., 490 words), host tests 4 OK |

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

Step 1b review findings (2026-09-14, none blocking; all five fixed in the "step 1b review fixes" row above):
`ambient_occlusion_fixture.cpp` includes `hdr_writeback_program_inc.h`, missing from the runner's SOURCES
provenance list; the apply-law comment at the reference half-depth feed is stale (GPU half depth only bounded
to 2e-3 relative); the "Bayer noise still averages out" claim in `ao_blur_ps.hlsl` is unverified (the quincunx
covers 7 of the 16 residue cells mod 4 — recounted as 7 while fixing — oracle means moved ≤ 0.002); `finite_params` accepts a denormal m32 that would
overflow the folded 1/|m32|; the twin slot probe fell to 300 against a 397-slot program. The review rerun
did not replace the committed record (restored after the run).
