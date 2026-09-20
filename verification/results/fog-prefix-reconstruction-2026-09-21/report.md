# Fixed angular/depth fog-prefix reconstruction

Result: **failed-prefix-feasibility**.

Four endpoint views use 9,216 independent holdout rays each. Complete all-ray T p99 is 0.059696..0.127455 (gate .001); max is 0.116005..0.206792 (gate .003). Normalized-S gates also fail.
Direct 128/64 far-reference convergence passes with worst T max 0.000212073326; prefix 128/64 quadrature convergence passes with worst T max 0.000158786774. Boundary populations contain 873..1603 rays and fail separately; report.json retains all/sky/geometry/boundary S/T metrics and exact counts.
Constant/vacuum, prefix-plane, invalid-depth, and source-alpha laws pass. Fixed radial, one-column near-object, and screen-depth-step synthetic results are retained. Endpoint failure invokes the ratified stop rule, so the conditional temporal arm did not run.

The maximum complete-T diagnostic per view is not a universal failure witness; the explicit metric tables are authoritative for every failed S, T, convergence, and population gate.
Images are cloud-only fixed-scale near controls, far/complete references, prefix reconstructions, signed errors, and holdout boundary masks. Original capture cards are not static long-range cloud truth.
Numerical source/report and final report source are bound separately because the depth-hash and witness-label corrections occurred after execution. No numerical arrays or images were recomputed during finalization.
Static read/storage estimates are not GPU time or FPS. This failure rejects only the fixed 64x36/32-plane reconstruction; it does not reject all prefix designs or approve the accumulated 40 km haze.
Numerical host runtime: 103.13s. No game, Wine, build, production edit, install, or commit was performed.
