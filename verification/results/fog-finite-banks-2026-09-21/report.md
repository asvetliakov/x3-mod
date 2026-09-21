# Fixed finite world-space fog-bank preview

500-unit sampling result: **failed-500-unit-sampling**. Authored-medium reference preview: **not selected; user prefers broader, connected clouds**.

Twelve canonical 256x144 reference images cover both families and six frozen poses. Direct 128/64 reference convergence passes (worst T max 0.000220119953). Candidate500 fails 3/12 canonical views; worst T p99/max is 0.00137059987/0.0033454299 (gates .001/.003). Boundary-path temporal gates also fail for green; fixed reference previews remain valid for authored-medium review.
Canonical projected occupancy is 0.0882..1.0000, with 5..10 full-resolution raster-visible banks. At nominal 30/38 km poses, cell(0,0,1) intersects substantially before bank0; the added same-camera bank0-only sheets isolate the intended-distance bank without replacing the all-bank layout previews.
All required support/transport/depth/alpha/order laws pass. Across canonical, path, endpoint, and stress rays, observed maxima are 3.0 bank hits and 137.0 samples, within fixed bounds 4/264. Canonical full-resolution support-edge populations are 3574..6422 pixels if marked for repair.

Cloud-only sheets compare the current 2.4 km control, converged bank reference, candidate500, and signed errors at fixed scales. No scene background was fabricated.
The sampling verdict and authored-medium appearance are separate: candidate500 failure closes that sampling rule, not the fixed bank reference recipe. No radius, spacing, hash, anchor, strength, density, or sample-count adjustment followed.
The original transport/error arrays and 26 images remain cached unchanged. Finalization recomputes analytic full-resolution coverage and merges the separately bound bank0 diagnostic arrays and six sheets, alongside required law/cost gates and depth-boundary labels.
CPU runtime and read/storage counts are not GPU timing or FPS. Production transaction/state/Reset/recovery, native Windows, TAA, and user flight appearance remain open.
Numerical host runtime: 154.45s. No game, Wine, build, production change, install, or commit was performed.
