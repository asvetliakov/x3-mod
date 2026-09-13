# Remaining conventional SM3 hull materials

Pure source qualification, 2026-09-13, following the 70-pair checkpoint
`73f5c51`. The next complete group adds **24 PS, no VS, 40 pairs and 168 archive
pass occurrences**: shared Khaak/Teladi/Teladi_nodiff/Xenon BUMPMAP, Split BUMPMAP,
and Terran DEFAULT/BUMPMAP. The bounded transformer contains **73 originals /
110 exact pairs**. The pure source review is approved. GPU/live qualification and installation of
this group remain pending. Installed coverage is still 30 pairs; the preceding 70-pair
source has separate [GPU/live qualification](linear-standard-materials.md).
This remains an intermediate step in the [complete coverage ledger](material-coverage.md).

The [original study](../reverse-engineering/remaining-hull-materials.md) gives
all 24 identities, exact pair/alias boundaries, source offsets and native
coefficients. Each technique includes six PS and ten pairs, covering base and
both single-light VS variants, affine/non-affine color, ordinary/VFACE, every
recorded toggle and `2s`/`_0000`/`_0001` aliases. The four shared hull names overlap
exactly and count once. Admission uses complete original identities, never a
filename or a representative captured shader.

## Preserved math and implementation

| Family | Diffuse | Specular power | Cube multiplier |
| --- | ---: | ---: | ---: |
| Shared BUMPMAP | 0.5 | 6, original multiply chain | 0.5 |
| Split BUMPMAP | 0.5 | 10, original POW | 1 |
| Terran DEFAULT and BUMPMAP | 1 | 5, original multiply chain | 1 |

All retain outer specular strength 3 and the saturated `3*NdotL` response.
These coefficients are fixed shader facts; this group adds no application scalar
policy. The 18 BUMPMAP programs retain AG reconstruction, VFACE handling and
per-pixel reflection from the original normal/view. Terran DEFAULT uses the
original geometric normal/view and interpolated cube direction. Packed scalar
lanes, normal data and specular masks remain outside RGB conversion.

The existing transfer policy and transformation algorithm are unchanged. New
rows provide exact original counts, source operand locations, clamp registers
and RGB destinations. Shared BUMPMAP includes its additional half-scaled cube
RGB multiply; Terran's fused unit-diffuse/specular MAD retains its original order.
Terran BUMPMAP affine-single PS `042c9ae16f41feff` and `68f0dd6791fd7d3d` retain
partial precision on their homogeneous diffuse MAD at DWORDs 1195 and 1221.
The authored affine operation still precedes diffuse decoding. The token proof,
not a disassembler's printed modifier, establishes that precision detail.

The six existing VS remain shared across old and new families. Class A retains
motion/depth/material TEX4/5/6, o6/o7/o8 and v5/v6/v7. Class B retains TEX5/6/7,
o7/o8/o9 and v6/v7/v8. Material scratch and constant reservations are unchanged;
no new varying allocation or temporal API is introduced. The preceding standard
base depth-TEX7 exception remains confined to its two existing pairs.

New originals peak at 1,358 DWORDs, so the complete corpus guard remains 1,392
with every individual count checked. All RGB lists fit the existing 13-site
capacity. The immutable-original motion transformation and verified span merge
preserve original geometry, alpha and temporal insertions; failures preserve
output. Registration performs the transformation once. The draw path continues
to read its cached pair mask and four/five known sampler states; the bounded
allocation-free pair scan now contains 110 rows at shader-state changes.

## Host proof and performance

The baseline captured before edits includes every **392 previously qualified
combined output**, both depth modes and gains 0/1/4/16. Its framed SHA-256 is
`b9753e6337fd36cbb8bf15851e5821361bd003ed428b9ec859d80289321f6e02`.
All compare byte-exact; the earlier 192/120/72 regressions remain enabled.
The host driver constructs **584 variants** across all 73 originals and checks
the complete 110-pair matrix, cross-family refusals, aliasing and rollback.

Maximum static weighted budgets remain **VS 85/87 and PS 178/180** for depth
off/on, below the SM3 512-slot minimum. Maximum executable instruction counts
are VS 74/76 and PS 133/135. Counts retain POW/NRM=3, cube TEXLD=4,
DP2ADD/LRP=2 and explicit flow costs; unknown forms fail validation.
A local diagnostic measured 584 initial transformations in 7.62 ms total
(about 13.0 microseconds each), with 170.6 ms including file I/O and all negative
checks. This is create-time host cost, not GPU timestamps or game FPS.
The pure core translation unit cross-compiles with the project's i686 MinGW
SSE2 and incoming-stack flags and warnings treated as errors.

The original-profile module passes **33 tests**, certifying 73 originals,
110 pairs and 408 total archive occurrences. Exact semantic schedules and
source/destination/deletion, literal, coefficient, precision and alias mutations
cover the new group; previous 49 proof records remain exact except added VS
family annotations.

The focused transformer module passes **9 tests** in 5.774 seconds, including
10,007 structural-driver checks and all retained byte regressions. The reference
extension passes six new numerical tests and its regenerated 66-PS crosscheck;
the earlier 37-test reference evidence remains unchanged and was not repeated.

## Fallback witness and whole-group qualification

The prior Terran and Split negative controls become covered. The replacement
shared-VS control is **VS `494fe349b8bc12ec` / PS `fffdabd910793aba`**, an existing
class-C static-branch motion pair. Host checks run both actual original stages
through their own row-explicit motion transformer in both depth modes and obtain
`Applied`; material conversion refuses without changing output and the exact
material sampler mask is zero. This is a successful ordinary-motion control,
not an unsupported cross-pair and not a class-A/B ABI substitution. Its PS has
1,648 DWORDs and remains outside the material bound; no guard is relaxed for it.

Qualify all 40 additions together in the existing detached fixture, retaining
the preceding 1,527-case prefix. Exercise every pair/depth/face variant, loop
counts 0/1/8 and fixed lighting, independent colored inputs and gains, plus
per-profile witnesses distinguishing half/unit diffuse, powers 5/6/10 and the
shared half cube term. Include AG/reflection and affine precision witnesses,
operational singular/exceptional cases, exact original alpha and motion/depth
identity. Run representative original/motion/combined cost windows once for the
complete group; do not create subgroup GPU/install cycles.

The actual live fixture must extend its corpus to 110 pairs and replace its
consumed negatives deliberately. Give the class-C control its own original
boolean/constants/sampler setup and its existing temporal register assignments;
compare feature-off/on fallback outputs and require ordinary motion success.
It must not inherit the old BUMP negative setup. Retain both first appearance
and valid history, cached sampler admission, shared-stage alternation, Reset and
owned variant retirement. Independent review and one coordinated X3 run precede
installation. Native Windows behavior and gameplay appearance/performance remain
unverified.

## Authored live extension; execution pending

The live fixture extends its corpus from 70 to 110 pairs, each drawn twice:
**244 frames per configuration**, with 73 distinct covered variant objects.
The existing 24 lifecycle frames remain, but the former Terran negative frames
9 and 19 now bind the exact class-C pair. Its changed VS identity also makes
frames 9, 19 and 20 unmatched; those expectations are updated explicitly rather
than pretending the old history schedule is unchanged. The preceding 70-pair
corpus itself is retained in order, and the new 40 pairs append 80 frames.

The class-C control uses its own original constants, both original static
booleans false, the original five-sampler layout (s4 is a 2D decal, not a cube),
default geometry/declaration and actual temporal row. That PS also reads TEX6.x
which its paired original VS does not provide. The fixture preserves that
original contract without manufacturing a material varying. It compares raw
full-image FP16 hashes between feature-off/on and ownership twins, plus the
separate alpha, state and motion/depth checks; it makes no analytical or finite
RGB claim for these two fallback frames. Other material corpus frames retain
the finite unit-lightmap activation witness.

Five host report tests pass, including C++/Python/proof corpus agreement and
negative raw-bit/history witnesses. The actual fixture translation unit
cross-compiles for x86. The function-only change is independent of the concurrent
emission fixture and adds no shared helper or CLI requirement. The independent
pre-run review below approves this extension; actual execution remains pending.

## Independent source review

The bounded source review approved this 40-pair core for whole-group GPU and live
qualification. The production table was checked against the independently derived
texture, affine, directional-source, clamp, final-output and RGB-precision sites
for all 24 PS. The exact schedule proofs cover every executable instruction and
retain fixed coefficients, AG normal reconstruction, reflection liveness, alpha,
the two authored affine PP operations and the existing VS/PS motion/depth ABIs.
The 110-pair matrix keeps the standard depth-TEXCOORD7 exception confined to its
two prior pairs, and the class-C control proves ordinary motion success while its
material mask remains zero and its material output remains unchanged.

One review finding was fixed: generated metadata had pointed current budgets and
pending qualification at the preceding 70-pair note. The generator and checked-in
JSON now point here, and the affected profile module passes 33/33. Reviewed host
evidence also includes 584 variants and 10,007 structural checks, byte-exact parity
for all 392 preceding outputs, the retained 192/120/72 regressions, the 66-PS
reference crosscheck, clean x86 compilation, and transformed maxima of VS 85/87
and PS 178/180 weighted slots. The approximately 13-microsecond transformation
measurement is create-time host cost; it is not a GPU or frame-rate result.

The detached and live fixture extensions are also approved for the coordinated
X3 run. The detached fixture preserves its 240-byte case ABI and the first 1,527
binary case payloads exactly, then expands to 2,498 cases and 22,482 samples over
all 110 pairs. Its C++ tables are cross-checked against the Python corpus and
source proof; the new analytical cases distinguish all fixed coefficients and
exercise every new PS, AG normal/reflection behavior, affine precision, alpha,
temporal outputs and bounded exceptional inputs. The runner consumes a prebuilt
EXE and binds the executable, actual transformer inputs, reference and all 73
original identities by hash.

The live fixture appends every pair twice with a unique object scope, proving an
unmatched first use and matched second use, all 73 cached variants and their
owned-reference retirement. Its two class-C frames bind the exact original VS/PS,
disable both authored static branches, supply the original constants, declaration
and five sampler types, and compare full-surface FP16 readback bits between the
feature and ownership twins while retaining motion/depth and alpha checks. Review
found that sampler 4 initially inherited filter and address state from preceding
BUMP frames; the fixture now sets point filtering, no mip filter, clamped address
and disabled sRGB explicitly. The affected five report tests and x86 fixture
compile pass after that fix. These approvals establish that the planned runs test
the intended contracts; actual GPU results are still required.
