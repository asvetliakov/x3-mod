# Converged long-range fog reference images

Result: **passed-reference-experiment**.

Dense 128-vs-64 far-only T convergence worst-view p99/max: 0.00013332665/0.000220477581 (gates .00025/.00075).
Four endpoint images use 2304 deterministic rays each; sky counts are 1948..2044.
The 30-40 km shell exceeds .002 opacity on 0.6638..0.9425 of sampled sky rays in 19..44 connected screen regions. It is spatially varying and nonzero in all four views; this does not imply sparse or localized support. Within-shell adjacent period-width distance-block sky optical-depth correlations have maximum absolute value 0.0794; report.json records bounds and partial blocks.
Complete-column sky clear fractions below .002 are 0.0000..0.0078; this descriptive screen is not a visual verdict.

Sheets show near24, the unfiltered 2.4-30 km shell, the tapered 30-40 km shell, complete reference, and reference-minus-rejected-candidate. Separate transmission and optical-depth images use fixed scales.
S uses captured production-selected point-sun dir1 with normalized unit radiance; production-scaled S parity remains open.
These cloud-only deterministic images are not user visual acceptance and do not select a production integrator or spatial recipe.

Host runtime: 13.38s. No game, Wine, build, production edit or install was performed.
