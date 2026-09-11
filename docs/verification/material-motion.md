# Same-draw material color and motion prototype

The detached transformer adds motion output to one exact opaque SM3 material
pair. It preserves the original vertex-position and pixel-color instructions,
adds previous homogeneous clip coordinates, and writes the existing RGBA32F
previous-UV / previous-depth / validity ABI to COLOR1. It makes no D3D calls and
is not connected to a game draw hook.

The actual transformed pair passes **1,182 checks, 2,952 analytic motion samples,
101,318,656 color-component comparisons, and 164 bilateral D24 EQUAL cases**
across 82 configurations. Every compared original color component is identical.
The independently authored two-pass replay produces identical motion readback
for every compared pixel, with maximum difference zero. Both normal and pure
hardware vertex-processing devices pass, with an actual Reset on each.

This supports pursuing the same-draw approach for this material. It does not
establish whole-scene motion, temporal continuity, complete transparency handling,
TAA quality, live-game performance or native-Windows behavior.

## Pair, inputs and output contract

Only the following complete local shader programs are supplied:

| Stage | FNV-1a64 | DWORDs | SHA-256 |
| --- | --- | ---: | --- |
| VS | `53a0a641107ed76c` | 526 | `bc402d1c2bfbbcb9fedd98890db845dab2a24da8cfb5a88a74c4eafa40f7a50c` |
| PS | `8759c7838bbc86c2` | 1260 | `9fd15484fe419295cfb3534bd4f978efc8855c1e3e6a06e776533497dad48dc0` |

Raw game bytecode, preshader metadata and disassembly stay local and untracked.
The fixture supplies original triangles, 2×2 diffuse/specular/lightmap textures,
a six-face cubemap and explicit GPU constants. It does not run the effect's CPU
preshader or load game textures. Identity diffuse-color transform rows are
uploaded directly. Point-light counts 0, 1 and 8 stay inside the existing loop's
array bound. Separate positive controls show that changing only native light
count or a used material constant changes the original image; transformed color
still matches under those changed inputs.

Both a FLOAT3-position control and FLOAT16_4 position/texcoord/normal layout are
used. Packed position W is 7; the original shader constructs homogeneous W from
XYZ and ignores that stored W. Normals and view vectors are ordinary finite
inputs. This is not an exhaustive vertex-format or exceptional-input test.

Reserved constants are VS c252–255 for previous WVP rows, PS c216 for inverse
size and previous jitter UV, and c217.x for known-history mode. The extra VS
output o6 / TEXCOORD4 feeds PS v5. The motion body is relocated from the project's
embedded original motion PS, retaining its invalid mode and coordinate ABI.
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
the proof. The reported component count includes repeated comparisons for depth
controls; it is not a count of independent scene samples.

Bilateral depth controls first write depth with the original VS and redraw with
the transformed VS under EQUAL with depth writes disabled, then reverse those
roles. Each resulting material image matches the ordinary original image.
Changing the submitted depth row makes the EQUAL draw reject every fragment.
This proves matching stored D24 depth and coverage for the tested geometry, not
bitwise equality of unquantized clip coordinates at every possible input.

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
`8.65642841e-7` depth. RGBA32F storage alone is not a claim of exact interpolants.
The two-pass reference is a detached GPU reference, without geometry leases,
object-history lookup or application-admission overhead.

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
| 1280×768 | 0.6201 / 0.4851 | 0.6649 / 0.6484 | 0.7143 / 0.6994 |
| 5120×1440 | 1.4869 / 1.4868 | 1.6026 / 1.5441 | 2.2355 / 2.0781 |

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
python3 -m unittest verification/analysis/test_material_motion_report.py
```

The runner rebuilds the detached fixture, refuses a running X3AP process, bounds
runtime, requires exact case/sample/device/Reset/terminal inventories and retains format
query records. It passes fourteen offline parser tests, including rejection controls. Source, exact local shader inputs,
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

Independent review accepted the transformer, host optimized/ASan/UBSan evidence,
GPU fixture, final artifact provenance and stated limits. Review corrected the
native-module mapping and required one shared scene for the timed two-pass
comparison. The final run includes both fixes. There are no remaining findings
within this detached one-pair scope.
