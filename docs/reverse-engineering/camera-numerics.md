# Numerical camera evidence from flight captures

The two user-triggered captures, frames 5449 and 5652 in
`session-20260910-130335-268.log`, support a specific matrix factorization on
105 of 122 draws in each frame. This advances the camera-input investigation;
it does **not** validate temporal motion vectors, jitter or sampleable depth.

The reusable standard-library analyzer is
[`tools/analysis/analyze_camera.py`](../../tools/analysis/analyze_camera.py).
Its source identities, complete group membership, derived matrices and errors
are in [the numerical report](../../verification/results/game-flight-camera-numerics.json).
The report contains numerical observations, no shader instruction bytes. The
raw log and shader bytes remain local and untracked beside X3.

## Register representation and tested factorization

For this analysis, each captured float4 register becomes one **row** of a
matrix multiplying **column vectors**. The source CTAB parameter class is
MATRIX_COLUMNS (3): equivalently, transpose these matrices to express the
original row-vector convention. This does not prove a CPU-side C++ structure
layout or an engine convention outside these shader paths.

Extend the three captured world/view-inverse rows with `(0,0,0,1)`. Let `W`
be the resulting world matrix, `C` the view-inverse matrix, and `M` the four
WVP rows. Then the tested relationship is:

```text
V = inverse(C)
P = M * inverse(W) * C
M ≈ P_group * V * W
```

`P_group` is the component-wise median of independently recovered projections
for draws having exactly equal `C` within one frame. The reconstruction uses
that one projection across multiple different world matrices. Thus the
reconstruction test is stronger than merely multiplying a matrix by its own
inverse on one draw. Indices locate observations only; matching draw positions
are never treated as persistent object identities. Reversed multiplication
`W * V * P_group` has large residuals and is rejected in this representation.
The transpose-equivalent row-vector formulation is, of course, equivalent.

Common material VS hashes observed in the fit are `37c34a7478544c14`,
`53a0a641107ed76c` and `494fe349b8bc12ec`. Their named registers are WVP c24–27,
world c28–30 and view inverse c34–36. The fit also covers `be199829a9bb78db`
(WVP c0–3, world c4–6, view inverse c10–12) and `d5e1c75351ed3f04`
(WVP c0–3, world c4–6, view inverse c7–9). Other material hashes in the metadata
are not verified by these flight observations.

The analyzer also supports named view-projection inputs, but those shader
families have no qualifying draws in these two frames. It deliberately skips
the 17 draws per frame without this named camera/world factorization, including
shared GUI/nebula and bloom paths. Particle-specific view/projection and shader
instruction consumption are not validated by this report.

## Three coordinate regimes in one frame

The membership and counts below are identical in both captures. Distinct worlds
are distinct **matrix values**, not a count of independently identified objects.

| Observations | Draw count | Distinct world matrices | Camera translation | Maximum relative reconstruction error, either frame |
| --- | ---: | ---: | --- | ---: |
| Draws 2–8 | 7 | 2 | About (-5.41, -9.54, -11.28) in frame 5449 | 8.18e-8 |
| Draws 9–93 | 85 | 21 | About (-54096.63, -95404.63, -112835.22) in frame 5449 | 7.64e-8 |
| Draws 110–122 | 13 | 13 | Zero; almost identity rotation/scale | 8.79e-9 |

The first two groups share exactly the same captured 3×3 camera values. Their
translations differ by approximately a factor of **10,000**. This is evidence
of multiple coordinate scales; it does not identify the engine's physical unit
or prove that the first group is a sky pass. Four draws in the first group have
depth disabled, three have it enabled. All 85 in the second have depth enabled.
All 13 in the third have depth disabled and occur after bloom. Neither the
presence of named camera matrices nor post-bloom order makes a draw eligible
for scene TAA.

Between frames, the first group's translation changes by approximately
`(-0.063391, 0.075390, 0.243032)` and the second by
`(-633.890625, 753.843750, 2430.304688)`. Captured camera rotation values are
unchanged. These are sparse observations 203 frame numbers apart, not controlled
adjacent-frame motion or an object correspondence proof. The third group's
camera is unchanged and approximately identity (diagonal `0.9999999404`).

## Projection and precision

All groups support approximately this projection in the representation above:

```text
[ 0.8   0        0          0 ]
[ 0     1.333333 0          0 ]
[ 0     0        1.000003  -6 ]
[ 0     0        1          0 ]
```

Under the centered +Z perspective hypothesis, the aspect ratio is approximately
1.666667 (matching 1280×768), vertical field of view is approximately 73.7398°,
and near distance is approximately 6 in the relevant coordinate regime. `w_clip`
is consistent with positive view-space Z. This is a numerical projection shape,
not a handedness proof for the entire world coordinate system.

Far distance computed as `-P[2][3] / (P[2][2] - 1)` is ill-conditioned here:
`P[2][2]` is within about 3e-6 of one. Derived values range around 2.00–2.06
million between groups; report them as candidates, not an exact engine far plane.
The large-coordinate group's recovered projection has cancellation errors of
up to 0.0483 absolute between draws, mostly in the translation column, while
its reconstruction relative error remains below 7.64e-8. A small normalized
error alone must not be taken as precise depth-parameter recovery.

The common camera 3×3 block is approximately orthogonal but its maximum
`C_rotation^T * C_rotation - I` error is 3.62e-5. The analyzer uses a full matrix
inverse, **not** a transpose shortcut; quantization or imperfect normalization
would amplify the shortcut's error at large camera translations.

## Implications for temporal implementation

- Recover and classify the coordinate regime per eligible draw. A single global
  camera translation would be wrong for at least two observed groups.
- Verify the three-register affine extension against shader consumption and
  controlled visible motion before production use. The numerical relationship
  is strong evidence for these captured paths, not a complete shader contract.
- Obtain stable geometry/object identity and consecutive frames with known
  camera motion, stationary objects and moving objects. Constant grouping alone
  cannot distinguish instances or establish previous transforms.
- Check projection/depth behavior separately for small-scale geometry, ordinary
  scene geometry, effects, particles and HUD. Resolve sampleable depth before
  writing a reprojection path; D24X8 attachment metadata does not supply it.
- For a future tested WVP path, an NDC jitter hypothesis is to add
  `jitter_x * M_row3` to `M_row0`, and `jitter_y * M_row3` to `M_row1`.
  That follows the recovered column-vector representation, but no game jitter
  experiment has been performed and shared shader paths still require exclusion.

## Reproduction and verification

```sh
python3 tools/analysis/analyze_camera.py \
  "$HOME/Library/Application Support/CrossOver/Bottles/Steam/drive_c/X3/x3-modern-captures/session-20260910-130335-268.log" \
  --metadata verification/results/shader-registers.json --frames 5449 5652 \
  --output verification/results/game-flight-camera-numerics.json
python3 -m unittest discover -s verification/analysis -p test_camera_analysis.py -v
```

Fourteen synthetic tests cover noncommuting transforms across distinct worlds,
known near/far recovery, deliberately inconsistent transforms, typed register
namespace isolation, sparse zero rows, failed float queries, incomplete frames,
invalid matrix rejection, device/frame identity isolation, failed or missing
draw results, query coverage and absent v2 query statuses. The analyzer accepts
legacy float records and capture-v2 `type=f` records; `type=i,b` cannot overwrite
float values. V2 frame keys are `<device>:<frame>`; legacy keys remain `<frame>`.
V2 evidence requires a successful draw result and explicit successful float-query
status covering every named register, with sparse-zero encoding. Missing rows
outside the advertised query count cannot silently become zero. Failed draws
are excluded and reported; mismatched frame draw counts invalidate the frame.
Legacy
logs lack an explicit successful float-query marker, so their sparse-zero
interpretation relies on the documented capture implementation and complete
frame, with explicit query failures excluded. No raw-capture test fixture is
committed.
