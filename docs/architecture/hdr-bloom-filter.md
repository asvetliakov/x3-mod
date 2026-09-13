# Bounded HDR bloom filter core

2026-09-13. This record specifies the original numerical core in
`src/temporal/bloom.h`, `bloom_common.hlsl`, the six `bloom_extract_*_ps.hlsl`
wrappers, `bloom_down_ps.hlsl` and `bloom_up_ps.hlsl`. The standalone host oracle is
`tools/analysis/bloom_reference.py`. It does not establish renderer integration,
successful compositor replacement, game appearance, GPU timing or native
Windows behavior. Native shader compilation and static-budget qualification are recorded in
[filter verification](../verification/hdr-bloom-filter.md); device execution
and renderer integration remain separate gates. No game shader bytes are used.

## Working space and operation order

The source is the resolved FP16 scene before AgX and before the original
compositor. This scene contains game-space codes: gamma-space material lighting
and blending have already happened. Decoding those codes does not recover
scene-referred radiance. The bloom inherits that limitation; later material
work can supply a linear scene using the `none` decode mode.

For each source texel, extraction does these operations in order:

1. Sanitize the FP16 code sample: negative values and NaN become zero; positive
   infinity and values above 65504 clamp to 65504. Alpha is ignored.
2. Apply the existing AgX extended gamma 2.2, piecewise sRGB or identity decode
   definitions. The shader restores exact zero after its `pow` safety floor.
3. Apply the existing decoded-space `X3M_HDR_CLAMP`, using 65504 when unset.
4. Multiply by the same frame exposure that the base scene uses, then bound
   to 65504 **for bloom scratch storage only**.
5. Apply the luminance soft-knee prefilter, then spatially reduce.

The prefilter therefore operates on each decoded/exposed texel before any
averaging. Decoding averaged game-space codes or thresholding an already
downsampled image produces a different result and can discard small highlights.
All texture stages disable hardware sRGB conversion.

For exposed RGB `e`, Rec.709 luminance `y = dot(e, (0.2126,0.7152,0.0722))`,
threshold `t`, knee fraction `knee`, and width `k=t*knee`:

```
q = clamp(y - t + k, 0, 2*k)
soft = q * (q / max(4*k, 1e-10))
weight = saturate(max(y - t, soft) / max(y, 1e-10))
prefiltered = e * weight
```

The knee is well-defined at zero, black stays exactly black, and a common
scalar preserves each texel's chromaticity after the scratch safety clamp.
Threshold zero disables highlight rejection except below the numerical
division floor. That tiny range is below FP16 representable positive values
after a store. A hard knee gives `max(y-t,0)` for neutral input. At the center
of a nonzero knee, the neutral output is `k/4`.

Threshold units are **exposed-linear luminance**, not game code values or EV.
Doubling exposure doubles the value evaluated against a fixed threshold;
automatic exposure can therefore change which surfaces contribute bloom.
This is an intentional initial policy, awaiting visual tuning.

## Bounded pyramid and reconstruction

One through six levels are allowed. Each dimension uses integer ceil-half;
stop after the first 1×1 level. A 1×1 source still needs one extraction level.
For example 13×7 produces 7×4, 4×2, 2×1, 1×1. The pure C++ layout builder
allocates nothing and refuses zero/oversized dimensions or invalid counts
without changing its output. The 16384 dimension limit also bounds shader
coordinate arithmetic; device-specific texture caps must be checked separately.

Each destination pixel covers its exact uniform rectangle in source pixel
coordinates. In an axis with source extent `S` and destination extent `D`,
output index `j` spans `[j*S/D, (j+1)*S/D]`. Each of the at most three touched
source cells receives its exact overlap length, normalized by the sum. The
two-dimensional weights are products of the two axes. This requires nine
point reads in the generic kernel, even when some weights are zero. It avoids
the unbalanced repeated edge texels of naive odd-size 2×2 downsampling.

The area operator preserves DC and the image mean, including odd dimensions
and one-pixel axes. It is symmetric under reflection. The first level performs
prefiltering independently on each of these point reads; later levels already
contain exposed-linear values and use the same area weights directly.

The shader derives source support from exact ceil-half integer geometry,
**not** by multiplying a large cell coordinate by an approximate `S/D` then
flooring. That earlier formulation could select the wrong source cells at
integer boundaries (82→41, 8462→4231 and some 15611→7806 pixels). With
`D=ceil(S/2)`, `b=2D-S` and output index `j`, read cells `[2j-1,2j,2j+1]`
with normalized weights `[b*j,D,D-b*(j+1)]/S`. Their numerators/support
indices are exact float32 integers at the admitted dimension bound. Only
weights round; normalize their sum again before taking products. Even sizes
give `[0,1/2,1/2]`, odd sizes `[j,D,D-1-j]/S`, and a one-pixel axis gives
`[0,1,0]`. Zero-weight off-image taps use clamp addressing.

For extraction when **both source dimensions are even**, the separate
`bloom_extract_even_*_ps.hlsl` shaders read exactly the four contributing texels
with weight 1/4. `bloom_even_extraction` identifies this dimension class;
generic extraction remains required for odd or one-pixel axes. This avoids
five unused decode/threshold evaluations per first-level pixel. Decode mode
is specialized at compile time: `gamma`, `srgb` and `none` wrappers exist for
each geometry class. Identity computes no power and gamma/sRGB evaluate only
their selected transfer curve. `select_bloom_extract` validates the decode
enum and dimensions before selecting one of the six programs. This avoids
unused transfer calculations and keeps the static instruction budget portable;
an intermediate runtime-branch version required 527 instruction slots in the
generic case, exceeding SM3's 512-slot minimum. The native compiler's resulting
slot records must qualify each final variant. No shader variant is selected
by binary hashes or backend identity.

Let `D_i` denote a downsample level, `T` a bilinear reconstruction followed
by a separable `[1,2,1]/4` tent sampled in coarse texel units, and `s` scatter:

```
U_last = D_last
U_i = (1-s)*D_i + s*T(U_(i+1))
B = T(U_0) at full scene resolution
```

The coarsest reconstruction aliases its downsample image without a draw. All
tent weights sum to one; every combine is convex. A constant prefiltered image
`C` therefore produces exactly `C` for any level count and scatter in exact
arithmetic. The repeated additions do not multiply DC gain by the number of
levels. Scatter zero retains only the first level; scatter one retains the
coarsest level reconstructed through every intermediate size. Intermediate
values are bounded on every FP16 store to absorb rounding at 65504.

Clamp addressing is intentional. It preserves constant fields but does not
claim global impulse-energy conservation during tent reconstruction at the
image edges. These are normalized spatial filters, not a calibrated optical
point-spread function. Coarse area decimation can retain temporal phase changes
for moving subpixel highlights; TAA supplies the input, and actual flicker must
be evaluated in game.

## Composition and alpha

The final operation is `existing_exposed_scene + strength*B`, in full precision
immediately before AgX's inset matrix. `bloomComposite` takes that already
decoded/clamped/exposed scene as its first argument; it never calls the
extraction decoder. The existing AgX base can exceed 65504 after exposure and
can retain negative channels with identity decode. Both remain unchanged.
In particular, strength zero gives the original base numerically, without
clipping its high channels or changing chroma.

There must be no intermediate FP16 store of this sum, no second decode or
exposure, and no decoded-space firefly clamp after addition. The shader's
full-precision intermediates are finite for finite FP16 inputs and validated
exposure: the decoded base is capped before exposure and its maximum product
is 65504², far below float32 overflow. Scratch channels are at most 65504;
strength is bounded to [0,1].

Bloom texture alpha is zero and has no meaning. Preserve alpha from the
original scene separately. The engine integration may need an RGB-only final
write to preserve the compositor continuation's existing main-surface alpha;
that ownership/state decision is outside these RGB filter equations.

## D3D9 ABI and resources

All kernels use the existing fullscreen quad's `-0.5` pixel positioning with
ordinary normalized texel-center UVs; do not add another half-texel correction.
The ABI reserves c24–c28, after AgX c8–c21 and RCAS c23:

| Register | Meaning |
| --- | --- |
| c24 | sampled source/coarse width, height, reciprocal width, reciprocal height |
| c25 | destination/fine width, height, reciprocal width, reciprocal height |
| c26 | threshold, knee fraction, scatter, additive strength |
| c27 | exposure multiplier, decoded-space clamp, 65504 scratch maximum, zero |
| c28 | existing AgX decode mode vector |

All extraction variants and downsample bind s0 point/clamp. Reconstruction binds the fine
downsample image to s0 point/clamp and the coarser reconstruction to s1
linear/clamp. The final tent also requires linear filtering. All reads use
LOD zero, with no mipmap dependency. The renderer must enforce the exact
ceil-half dimension relationship for each kernel; the constant preparation
helper only validates individual dimensions and numerical parameters.

Use documented D3D9 FP16 texture/render-target/filter support checks and actual
shader-creation results. No Wine-private structures, locks, export contracts
or exact system-DLL hashes are prerequisites. Native Windows source support
does not imply native Windows runtime validation.

## Initial parameters and performance pass

Diagnostic defaults are five levels, strength 0.05, threshold 1, knee fraction
0.5 and scatter 0.7. These are conservative starting values, not visually
accepted defaults. The numerical core does not enable the feature. Threshold,
radius/scatter, color behavior at the scratch cap and exposure interactions
need controlled game captures with suns, emissives, dark space and bright
nebulae. Original compositor replacement and HUD separation require their
separate engine contract; adding bloom on top of the old glow is not acceptance.

Retain resources per device/size, outside draw loops; recreate on size change
or Reset, with transactional allocation and fallback owned by the renderer.
A straightforward separate down/up allocation needs `2*sum(level pixels) -
coarsest pixels` FP16 pixels. At 1920×1080, five levels contain 690,600 pixels
and use 11,033,280 bytes (10.52 MiB) for both chains. Six levels use 11,053,680
bytes. At 3840×2160, six levels use 44,210,880 bytes (42.16 MiB). These figures
exclude preexisting scene/history and device allocation overhead. Do not claim
the infinite-series area bound for thin images: one-pixel axes shrink linearly.

Each reduction uses nine point instructions; extraction additionally performs
decode/threshold nine times. A direct reconstruction uses nine bilinear tent
reads plus one fine read, and final composition uses nine bloom reads alongside
the existing scene read. At 1080p/five levels, this is 31,763,400 additional
texture instructions before optimization. The implementation fuses each tent
into four bilinear fetches: at fractional texel phase `f`, the one-axis
discrete weights are `[1-f,2-f,1+f,f]/4`. Merging adjacent pairs gives weights
`(3-2f)/4` and `(1+2f)/4` and their weighted sample positions. Both denominators
are at least one. This is exact at arbitrary phase, unlike simply replacing
the tent with four fixed-offset reads. It preserves clamp-edge semantics.
The reduction costs nine reads, reconstruction five, and final bloom four;
the 1080p/five-level total falls to 17,952,600 additional texture instructions
(43.5% fewer), at the cost of bounded coordinate ALU. Four-source extraction
then removes another 2,592,000 instructions at this even resolution, giving
15,360,600 (51.6% fewer than the original generic nine-tap version). Extraction
decode work falls from nine source evaluations to four; within each evaluation
compile-time mode specialization removes the unselected transfer powers and
all dynamic mode branches. These counts are
analytical costs, not GPU timings or game FPS. The first extraction shader's
static SM3 instruction budget and final reconstruction cost require
compiler/runtime assessment.
No extra per-scene-draw validation, allocations or locks belong in this core;
configuration and reciprocal calculations occur once per postprocess pass.

## Host verification

`python3 -m unittest discover -s verification/analysis -p test_bloom_reference.py -v`
passes 17 tests on the host, including a temporary native C++ compile/run of
the header ABI and validation logic. Controls cover odd/thin/terminal layouts,
area mean, DC gain across all six counts and scatter endpoints, black in every
decode mode, central impulse reflection symmetry, nonnegativity/chroma,
hard/soft threshold values, exposure/clamp order, decode-before-filter behavior,
alpha, strength zero with >65504 base channels, finite scratch extremes and
invalid settings. The fused four-fetch tent is compared to the independent
nine-tap oracle at 580 random/edge/texel-phase positions over odd, even and
one-pixel dimensions. The main filter oracle retains the unoptimized nine-tap
operator. The oracle uses double precision and does not simulate GPU
FP16 rounding, filtering precision or SM3 nonfinite comparison behavior.
GPU differential checks and performance evidence remain integration work.

Additional implementation-level controls execute the shader's scalar geometry
in float32 for every output index of source sizes 1–257, 8462, 15611, 16383 and
16384. They compare source support and FP16-max impulse contribution against
the independent overlap oracle, then check DC, reflected weights and mean
gain with explicit rounding tolerances. Four-tap extraction is independently
compared to area extraction after nonlinear prefiltering at even sizes,
including 82×2. This addresses the review findings without weakening the area
operator or assigning the CPU oracle the same coordinate formula. Selector and
wrapper controls cover all six parity/decode combinations and invalid inputs.
