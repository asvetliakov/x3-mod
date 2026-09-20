# Fog distance offline replay

Result: **rejected-integrator**.

Reference convergence: True; candidate24 T numerical: False; candidate24 normalized-S/T numerical: False; sky clear-gap screen: False; false-support gate: True.

Reference128-vs64 |dT| p99/max worst view: 0.000102365017/0.000127732754 (gates .00025/.00075).
Candidate24 |dT| p99/max worst view: 0.24164252/0.292993218; normalized-lighting max-channel |dS| diagnostic: 0.108000509.
Candidate48 alternate: |dT| 0.152763082/0.195159763; normalized-lighting |dS| 0.0891830847.
Minimal worst witness: physical limit 200000, candidate/reference T 0.598590434/0.305597216, |dT| 0.292993218.

Across four endpoint views, sky-only counts are 77..85 of 96 scoring rays and 1948..2044 of 2304 appearance rays. Reference sky clear fractions span 0.0000..0.0118; candidate sky fractions span 0.0000..0.0000. All-ray descriptive fractions are 0.0729..0.1667 reference and 0.0612..0.1285 candidate. Values below .25 flag only this deterministic diagnostic screen; they are not a population estimate, user visual rejection, or evidence that every range design is impossible.
Matched moving-reference temporal |dT residual| p99/max: bluewell 0.127334854/0.142418861; foggreenoutlands 0.020480442/0.0228018761.

Ray sets: 96 endpoint scoring rays/view, 24 matched temporal rays/frame across 64 frames, 2304 appearance rays/view. Host replay: 4.63s.

Cloud-only sheets show near S/T, far S/T, then combined S/T at fixed S=0..0.03 and opacity=0..0.4 scales. S uses captured production-selected point-sun dir1 with unit radiance and retains the normalized .0005/.002 numerical gate; unavailable radiance leaves production-scaled S parity open. See `report.json` for per-view distributions, convergence, errors, temporal residuals, connected regions, hashes and operation counts.

This offline result is not a production selection or flight/native-Windows acceptance.
