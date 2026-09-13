# Same-draw material color and motion prototype

The table-driven transformer adds motion output to the exact opaque SM3
material pairs listed in the generated profile table (171 rows, classes A, B,
C and D: every transformable pair a technique pass of the installed effects
binds, see [motion-output-profiles.md](../reverse-engineering/motion-output-profiles.md)).
It preserves the original vertex-position and pixel-color instructions, adds
previous homogeneous clip coordinates, and writes the existing RGBA32F
previous-UV / previous-depth / validity ABI to COLOR1. It makes no D3D calls;
the live route binds its output (see [motion-output.md](motion-output.md)).

The historical GPU results below qualify the original 169 A/B/C rows. The two
new D rows pass [separate host proof and the bounded X3 R2 GPU fixture](../architecture/damage-motion.md);
independent evidence review is approved; gameplay/native-Windows verification remains pending.

The Argon reference pair passes the full inventory: **1,428 checks, 2,952
analytic motion samples, 101,318,656 color-component comparisons, and 164
bilateral D24 EQUAL cases** across 82 configurations, on normal and pure
hardware vertex-processing devices with an actual Reset on each. Every other
table row then passes the same color/motion/depth comparison on a third
device: **169 of 169 rows, 1,230 configurations, 21,103 checks, 44,280
analytic samples, 15,114,240 color components, 2,460 bilateral cases**, all
with identical color, zero replay-reference difference and the same analytic
maxima as the Argon row (0.00086 px UV, 8.7e-7 depth; the review-15 rerun
reported 0.0010 px UV for the Argon inventory, within the runner's bound). The twelve class C rows
run every configuration under all four pixel boolean settings `b0`/`b1`, after
a control proving the booleans change the original image (24 controls). See
[Results per row](#results-per-row).

This supports the same-draw approach for these materials. It does not
establish whole-scene motion, temporal continuity, complete transparency handling,
TAA quality, live-game performance or native-Windows behavior.

## Pairs, inputs and output contract

The Argon reference pair is supplied explicitly:

| Stage | FNV-1a64 | DWORDs | SHA-256 |
| --- | --- | ---: | --- |
| VS | `53a0a641107ed76c` | 526 | `bc402d1c2bfbbcb9fedd98890db845dab2a24da8cfb5a88a74c4eafa40f7a50c` |
| PS | `8759c7838bbc86c2` | 1260 | `9fd15484fe419295cfb3534bd4f978efc8855c1e3e6a06e776533497dad48dc0` |

The other rows' originals are read by both fixtures from
`/tmp/x3-shader-sweep/programs/{vs,ps}_<fnv64>.bin` at run time; the runners
cross-check each file's SHA-256 and length against
`verification/results/motion-output-profiles.json` and record them in the
summaries. A row whose files are absent is reported as skipped, never
fabricated; the Argon row must always run. All 169 rows' files were present.

Raw game bytecode, preshader metadata and disassembly stay local and untracked.
The fixture supplies original triangles, 2×2 diffuse/specular/lightmap textures,
a six-face cubemap and explicit GPU constants. It does not run the effect's CPU
preshader or load game textures. Identity diffuse-color transform rows are
uploaded directly. Point-light counts 0, 1 and 8 stay inside the existing loop's
array bound. Separate positive controls show that changing only native light
count or a used material constant changes the original image; transformed color
still matches under those changed inputs.

Both a FLOAT3-position control and FLOAT16_4 position/texcoord/normal layout are
used; both now also carry TANGENT0 (1,0,0) and BINORMAL0 (0,1,0) elements
(strides 56 and 40 bytes), because 85 of the archive's row vertex programs
declare them — a program that does not declare an element ignores it, so the
Argon inventory is unchanged. Packed position W is 7; the original shader
constructs homogeneous W from XYZ and ignores that stored W. Normals and view
vectors are ordinary finite inputs. This is not an exhaustive vertex-format or
exceptional-input test.

Reserved constants are VS c252–255 for previous WVP rows, PS c216 for inverse
size and previous jitter UV, and c217.x for known-history mode. The submitted
clip rows are uploaded at the row's `matrix_register`: c24–27 for the 107
point-light rows and c0–3 for the 62 light-free `_0000`/`_0001` rows (the
fixture copies the same rows there; those programs have no light loop). Six
of the light-free rows (asteroid, moon, planet_haze) issue their position dots
non-adjacently; they run the same configurations as every other row. The
extra VS output (o6 / TEXCOORD4 feeding PS v5 for class A rows; the row's free
registers for classes B and C) carries previous clip position. Samplers are
bound according to the original pixel program's declarations: the cubemap on
its cube sampler and the 2×2 textures on the 2D samplers (cycling), which
reproduces the Argon inventory's binding exactly. The motion body is
relocated from the project's embedded original motion PS, retaining its
invalid mode and coordinate ABI.
The prototype requires a **zero-origin viewport**, previous MinZ/MaxZ of 0/1,
matching history dimensions and the existing jitter convention. Nonzero viewport origins are unsupported; the fixture does not
silently interpret them as tested.

Mode 0 writes `(0,0,0,-1)` on covered fragments. Mode 1 supplies previous UV,
previous device depth and positive validity. Uncovered texels in this fixture
retain their initial zero clear value. No consumer treats those as valid motion;
coverage of later unsupported or transparent draws remains a separate requirement.
The test does not bind the output into live temporal resolve.

## Device and attachment evidence

The actual backend reports VS/PS 3.0, four MRTs, 256 VS float constants, 512 VS/PS
instruction slots, and PrimitiveMiscCaps `0x002ecff2`. This includes independent
MRT bit depths, independent write masks and MRT post-pixel operations. Format
queries use the actual adapter display format **X8R8G8B8 (22)**.

A8R8G8B8, RGBA32F and G16R16F all pass RT and post-pixel-blending format queries
and D24X8 depth-match queries. The fixture then performs actual draws/readback
with two combinations, both single-sample and equal-size:

- RGBA32F color + RGBA32F motion, an equal-bit-depth control.
- A8R8G8B8 color + RGBA32F motion, matching the captured scene color format.

The second combination passes on the observed independent-bit-depth backend.
On a device without that capability, the runner permits only the explicitly
reported equal-format control and uses a different exact expected inventory.
That hardware branch has not been exercised here. **G16R16F has query evidence
only**; it is not allocated, rendered, sampled or used as a substitute motion ABI.

## Color, depth and correspondence checks

Every small configuration compares all four original color channels at every
pixel, including alpha and uncovered regions. Light/material sensitivity checks
and positive covered-pixel counts prevent matching black clears from satisfying
the proof. Color targets are cleared to a non-zero color (`0x40201008`) and a
pixel counts as covered when it differs from the texel the device actually
stores for that clear (probed once per target format), so a material whose
output quantizes to zero in every ARGB8 channel — one archive row does — is
still recognized as covered geometry; the motion target keeps its zero clear.
The reported component count includes repeated comparisons for depth
controls; it is not a count of independent scene samples.

Bilateral depth controls first write depth with the original VS and redraw with
the transformed VS under EQUAL with depth writes disabled, then reverse those
roles. Each resulting material image matches the ordinary original image.
Changing the submitted depth row (`c<matrix + 2>`) makes the EQUAL draw reject
every fragment. This proves matching stored D24 depth and coverage for the
tested geometry, not bitwise equality of unquantized clip coordinates at every
possible input.

Motion checks cover stationary and translated geometry, perspective-varying
current and previous W, prior jitter, unknown-history mode and both declarations.
The CPU oracle independently solves the current pixel ray's object coordinate
on the original plane, transforms that point with previous rows, then applies
the existing half-texel and jitter convention. Nine covered raster pixels are
checked per configuration. Large controls additionally use 1280×768 and
5120×1440 targets.

The first diagnostic used a blanket `2e-6` normalized-UV tolerance and failed
at 32×32 by `1.39098901e-5` UV, approximately `0.000445` pixel. The subsequent
independent reference uses the separately authored fixed replay VS and original
motion PS on the same native geometry/depth. It matches the transformed motion
**exactly over all pixels**, distinguishing that shared raster/interpolation
error from a transformer-only discrepancy.

The final analytic bounds are **0.005 pixel in either UV axis and `2e-6` previous
device depth**, with observed maxima `0.00100856683` pixel and
`8.65642841e-7` depth.

**Current depth (RT2, temporal step 1).** Every variant draw also binds an
R32F third target (cleared to 1.0) that the pixel variant writes with the
current clip z/w. Three checks per configuration: the covered/uncovered
pattern (device depth in [0,1] and not the clear wherever the original
covered the pixel, the clear elsewhere); the authored replay drawn with
`ZFUNC EQUAL` over the original's D24 depth with previous rows := current
rows (mode 1), whose `.z` output is the rasterized z/w, compared over every
covered pixel (**exact**, maximum 0 over 7,755,681 pixels in the Argon inventory);
and nine analytic samples per configuration against the double-precision
`z/w` of the current rows at the object point behind the sample, with the
perspective configurations tilting the current clip z by `0.05·y`
(`depth_slope`) so the depth varies over the image. The analytic bound is
`4e-6` (the route oracle's): the pixel-side `rcp`/`mul` of the interpolated
z and w lands up to `2.56e-06` from the double value on this backend
(`2.36e-6` on the first run against a `2e-6` bound), while the replay path,
which performs the same division, agrees exactly. Together with the bilateral
D24 EQUAL controls this proves that RT2 carries the depth the rasterizer
stores for the same sample. RGBA32F storage alone is not a claim of exact interpolants.
The two-pass reference is a detached GPU reference, without geometry leases,
object-history lookup or application-admission overhead.

## Results per row

Each row runs three configurations per render-target format on the third
(non-pure) device, RGBA32F and A8R8G8B8 color with RGBA32F motion: FLOAT3
stationary, FLOAT16_4 perspective + translation + jitter, and the same with
unknown history (mode 0). Light loop count `i0.x = 0` throughout; the same
synthetic geometry, textures and constants as the Argon inventory. Per
configuration: one full-image color comparison, the two-pass replay
reference, nine analytic samples, the three RT2 current-depth checks (pattern,
exact replay comparison, nine analytic z/w samples at 4e-6), two bilateral
D24 EQUAL controls and the changed-depth negative control. Class C rows repeat the three configurations
under pixel booleans `b0/b1` = 0/0, 1/0, 0/1 and 1/1 (`SetPixelShaderConstantB`,
the `booleans` bitmask in the CONFIG line), so color identity and motion are
proven in every branch combination; before each format's configurations the
fixture requires that the original image differs between at least two of the
four settings, so the identity is not vacuous (this held for the six
single-block `b0` programs as well as the six two-block ones). Numbers from
`verification/results/material-motion-summary.json` (`row_results`).

Every row of a class produced the same inventory; the table gives the per-row
numbers by class (169 rows: 56 A, 101 B, 12 C; row order is the table order,
observed rows first).

| Class | Rows | Configs per row | Booleans | Checks | Samples | Color components | Min covered | Max UV px | Max depth | Replay max | RT2 samples | RT2 max |
| :-: | ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| A | 56 | 6 | 0 | 103 | 216 | 73,728 | 912 | 0.00086 | 8.66e-07 | 0 | 54 | 2.56e-06 |
| B | 101 | 6 | 0 | 103 | 216 | 73,728 | 912 | 0.00086 | 8.66e-07 | 0 | 54 | 2.56e-06 |
| C | 12 | 24 | 0/1/2/3 | 411 | 864 | 294,912 | 912 | 0.00086 | 8.66e-07 | 0 | 216 | 2.56e-06 |

The sixteen rows the captured session drew (the previous table, unchanged
fields and order) are rows 0–15:

| Row | VS | PS | Class | Configs | Booleans | Checks | Samples | Color components |
| ---: | --- | --- | :-: | ---: | --- | ---: | ---: | ---: |
| 0 | `494fe349b8bc12ec` | `fffdabd910793aba` | C | 24 | 0/1/2/3 | 339 | 864 | 294,912 |
| 1 | `53a0a641107ed76c` | `8759c7838bbc86c2` | A | 6 | 0 | 85 | 216 | 73,728 |
| 2 | `37c34a7478544c14` | `5f82ecacd39529cd` | C | 24 | 0/1/2/3 | 339 | 864 | 294,912 |
| 3 | `4944d81dfe531b37` | `ca6bfa4a6cca7e2a` | B | 6 | 0 | 85 | 216 | 73,728 |
| 4 | `b0602757fce6e870` | `517540ae6d5e5410` | A | 6 | 0 | 85 | 216 | 73,728 |
| 5 | `4944d81dfe531b37` | `5e0a10fe752b6140` | B | 6 | 0 | 85 | 216 | 73,728 |
| 6 | `37c34a7478544c14` | `f1b0e820c7b488c3` | C | 24 | 0/1/2/3 | 339 | 864 | 294,912 |
| 7 | `53a0a641107ed76c` | `63f96eba9eea7880` | A | 6 | 0 | 85 | 216 | 73,728 |
| 8 | `167eb2d5629ab9d3` | `d44db87778a43b61` | B | 6 | 0 | 85 | 216 | 73,728 |
| 9 | `494fe349b8bc12ec` | `e6794b6ec37ff71a` | C | 24 | 0/1/2/3 | 339 | 864 | 294,912 |
| 10 | `53a0a641107ed76c` | `3b94320087e81945` | A | 6 | 0 | 85 | 216 | 73,728 |
| 11 | `53a0a641107ed76c` | `462342e3e5781384` | A | 6 | 0 | 85 | 216 | 73,728 |
| 12 | `4944d81dfe531b37` | `64bac8bb307eb896` | B | 6 | 0 | 85 | 216 | 73,728 |
| 13 | `494fe349b8bc12ec` | `7c83ed50c9894e44` | A | 6 | 0 | 85 | 216 | 73,728 |
| 14 | `4944d81dfe531b37` | `0c1f3f0f440e4a0c` | B | 6 | 0 | 85 | 216 | 73,728 |
| 15 | `c30104cb0efb6675` | `a66fb1981ba755b2` | B | 6 | 0 | 85 | 216 | 73,728 |

Rows 16–168 are the 153 archive-only rows (never drawn in the captured
session; the spaced-quad rows are 16, 17, 42, 53, 129 and 164); their per-row
numbers equal their class row above and are listed individually in the
summary JSON. The identical maxima are expected: every
table VS transforms the same synthetic position through its clip rows, so the
interpolated previous clip coordinates are the same numbers in every row.
What differs per row is the material color program (compared full-image and
bit-identical) and the register relocation (checked structurally). The
light-sensitivity and material-constant controls run only for the Argon pair,
whose constant layout the fixture models; the other rows receive the same
constant values, which is sufficient for the identity comparison but is not a
claim that those values are meaningful for their materials. The whole GPU run
(build, Argon inventory and 169 rows) takes about 26 s.

The host structural fixture (`run_material_motion_structure.py`) covers the
same 169 rows: 1,691 check groups, 1,551,936 single-bit mutations (every
input DWORD, every bit, for the 16 captured rows: 925,248; a deterministic
evenly strided sample of 2,048 bit positions per program for the other 153
rows: 626,688), 21 row perturbations per row and 26 (class A/B) or 40 (class
C) program perturbation sites per row, plus two for each spaced-quad row (the
position temporary written between the dots; a balanced block between them) (perturbed originals under a row copy
carrying their fingerprint, so the structural revalidation is exercised from
the words; every class refuses an unterminated vertex-side block before the
position dots; light-free rows refuse an injected relative operand under
their denied bound; the class C set covers unbalanced, nested, `if_comp`,
`rep`, `break`/`breakp`, non-boolean, relative, malformed and predicated
branch variants plus reserved registers used inside a branch body, for one-
and two-block programs). A site a program does not offer is reported as
skipped rather than fabricated and never for a captured row: 4,572 of the
4,574 sites ran (rows 129 and 130, both with pixel program `6aaaa2cb27e92cc8`,
have no literal DEF to place on an ABI constant). 1,014 alias layouts, in
optimized and ASan/UBSan builds (about 27 s in all), and the Argon output is byte-identical
to the earlier hand-written transformer. A table-level lookup oracle
(`TABLE lookups=PASS`, review 15) then checks the route's binary searches
against a linear scan of the same table without local programs: for every
row the pair, vertex-row and pixel-row lookups (agreeing sides, first row in
table order, wrong DWORD count refused) and the sixteen neighbouring keys
(`±1`, `^1` on either fingerprint; 2,535 absent pairs), plus the
lexicographically first and last pairs, the smallest and largest per-stage
fingerprints and keys just outside them, zero and all-ones keys. The GPU runner requires at least 512
of the 1,024 pixels of each per-row comparison to be covered original
geometry (912 observed for every row).

## Completed-work cost comparison

Timestamp, timestamp-frequency and timestamp-disjoint creation all return
`8876086a`. Timing therefore uses QPC through successful EVENT completion,
including runtime/driver work, GPU completion and polling with `Sleep(0)`.
It is **not isolated GPU execution time or game FPS**.

The three workloads use the same original geometry, eight point lights,
constants, textures, dimensions and motion/depth ABI:

| Mode | Work |
| --- | --- |
| 0 | Original material color draw |
| 1 | Transformed material color + motion in one draw |
| 2 | Original color draw followed by independent motion replay under depth EQUAL |

Shader/target switches, clearing and draw submission are inside the timed
interval; mode 2 additionally includes its second-pass binding/constant changes,
clear and draw. Every measured workload uses exactly one BeginScene/EndScene
pair; both mode-2 draws execute inside that shared scene. The report records
and validates one scene pair and one or two draws per workload. Common setup is completed with a separate EVENT fence before
QPC starts. No readback occurs inside a timed sample. Six warm-up workloads
exercise all modes; eighteen measured samples alternate `0,1,2 / 2,1,0`, giving
six samples per mode and resolution. Each workload completes before the next
begins, avoiding a stream of identical uncompleted draws hiding earlier work.

| Resolution | Original color mean / median ms | Same-draw mean / median ms | Two-pass mean / median ms |
| --- | ---: | ---: | ---: |
| 1280×768 | 0.6331 / 0.6752 | 0.6484 / 0.6906 | 0.7481 / 0.7296 |
| 5120×1440 | 0.7265 / 0.7088 | 0.8116 / 0.8134 | 1.1897 / 1.1976 |

This bounded sample favors one draw over the matched GPU replay workload,
particularly at the larger target. Six samples with visible variability do not
support a universal percentage improvement. The geometry is one oversized
triangle, with substantial per-pixel work and tiny textures; it does not model
hundreds of real objects, shader changes, material diversity or bandwidth from
game textures. The prototype has no additional buffer maps or replay exclusion
requirements because its motion comes from the same submitted draw, but live
integration and its costs remain unimplemented.

## Reproduction and provenance

```sh
python3 verification/probe/run_material_motion.py
python3 verification/probe/run_material_motion_structure.py
python3 -m unittest verification/analysis/test_material_motion_report.py
```

The runner rebuilds the detached fixture, refuses a running X3AP process, bounds
runtime, requires exact case/sample/device/Reset/terminal inventories for the
Argon section and an exact per-row block inventory for the table section, and
retains format query records. It passes fourteen offline parser tests, including rejection controls. Source, exact local shader inputs,
compiler, native modules, executable, stdout and stderr hashes are retained in
`verification/results/material-motion-summary.json`.

The process reports logical `C:\windows\system32` module paths. As an x86 process
in this bottle, those map to the host bottle's **windows/syswow64 PE32** d3d9 and
wined3d files; the runner checks their i386 PE headers and hashes those files.
It does not hash the unrelated PE32+ host system32 modules as its x86 evidence.
All source/native/local inputs are checked before build, after build and after
execution. Generated executables and raw shader bytes remain untracked. The
fixture unbinds/releases its resources before each successful Reset and closes
its window on completion. This proves fixture cleanup, not live hook restoration.

Independent review accepted the one-pair transformer, host optimized/ASan/UBSan
evidence, GPU fixture, final artifact provenance and stated limits. Review
corrected the native-module mapping and required one shared scene for the
timed two-pass comparison. The table-driven extension (2026-09-12) keeps that
inventory unchanged and adds the per-row section; its independent review is
[review-13.md](review-13.md). The class C extension (same day) adds the
branching rows, the per-boolean repetition and the declaration-driven sampler
binding ([review-14.md](review-14.md)). The archive-wide extension (same day)
grows the table from 16 to 169 rows (including six spaced-quad rows), adds
the tangent/binormal elements, the per-row clip-row register, the probed
clear color and the sampled mutation sweep; the Argon inventory numbers are
unchanged.
