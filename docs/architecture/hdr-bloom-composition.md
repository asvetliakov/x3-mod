# Bloom candidate display transform and sharpen staging

2026-09-13. Authored shader and host numerical preparation for the
[original-then-RGB-replacement boundary](hdr-bloom-boundary.md). This is not
renderer integration, GPU acceptance, a new installation or game evidence.
The independently investigated bloom sampler precision remains a separate
gate. Native Windows runtime behavior remains unverified.

## Operation order and ownership

`src/temporal/bloom_agx_ps.hlsl` samples the retained pre-original resolved
FP16 scene from s0, performs the existing `decodeEngine`, decoded-space
`min(v, exposure.y)`, and `v *= exposure.x`, then adds
`bloomFilter.w * bloomTent(s1, uv)` immediately before the AgX inset. The
addition is float32. There is no intermediate FP16 store, second decode,
second exposure or firefly clamp of the sum. In particular identity-decode
negative channels and exposed base channels above 65504 survive unchanged.
The extraction-only `bloomExposed` sanitizer is not the base transform.

`agx.hlsl::agxTonemapExposed` contains the existing inset, log encoding,
contrast, look and outset expressions in their original order. Ordinary
`agxTonemap` retains its original decode/clamp/exposure expressions and calls
this helper. This source refactor requires native bytecode/provenance
verification of the existing programs; host equation checks alone cannot
establish identical compiled output. The native compiler has now reproduced
all ten existing embedded headers byte-for-byte, recorded in
`verification/results/bloom-agx-refactor-parity.json`; only affected source
provenance changed.

The constants use existing AgX c8–c21 and bloom c24–c28. In this pass c24 is
the first reconstructed bloom level's dimensions/reciprocals and c25 is the
full output dimensions/reciprocals. s0 is point/clamp and s1 follows the
qualified bloom tent sampling contract (currently linear/clamp), both LOD
zero and hardware sRGB decoding disabled. AgX output is already display
encoded, so `SRGBWRITEENABLE=FALSE`. The existing pixel-aligned quad needs no
additional half-texel adjustment. c8 must contain the same latched frame
exposure used by the normal no-bloom writeback; candidate work does not meter
or advance history.

Candidate alpha carries the original scene sample. It is not authority for
the final main-surface alpha: the post-original copy writes RGB only and
preserves the genuine original compositor's alpha.

## Selected candidate topology

Without sharpening, the shader writes the complete A8R8G8B8 candidate directly.
With sharpening, it writes a full-resolution A16B16G16R16F **display** stage,
then existing `taa_sharpen_ps.hlsl` performs display-space RCAS into the complete
A8R8G8B8 candidate. Both passes finish before original compositor execution.
After original execution the boundary only copies candidate RGB, as its
recovery contract requires. No FP32 render target becomes a capability
prerequisite.

This evaluates AgX and the final bloom tent once per pixel. A fused sharpen
shader would evaluate both for all five cross taps. Sharpening exposed scene
values before AgX changes the existing display-space definition and is not
an interchangeable optimization. Using an RGBA8 display stage would avoid
half the stage storage but introduce quantization before RCAS at the final
display-code scale; it is not the selected topology.

Strength zero preserves the original base decode/clamp/exposure arithmetic
and AgX equations. An FP16 display stage can still change the subsequent
sharpen result through quantization. **Do not bypass candidate replacement
merely because strength is zero:** that would restore the stock compositor's
glow, making the modern bloom strength control discontinuous. Disabling the
modern bloom feature selects the legacy route; setting its strength to zero
continues replacing original glow with a zero-modern-bloom candidate.

## Storage-only numerical experiment

`verification/analysis/test_bloom_composition.py` supplies a double-precision
composition oracle built from the independent AgX reference and a scalar
RCAS implementation. It tests the insertion order, identity negatives,
exposed overflow beyond FP16, alpha, zero-strength equivalence and sharpen
neighborhood bounds. Binary16 conversions test both nearest-even and
toward-zero stores; the latter covers the preliminary backend storage
observation without treating it as a portable D3D guarantee.

The deterministic experiment contains 80 five-tap patches covering high
contrast, near-black, chromatic and smooth fields. It crosses all three
decoders, three looks, clamp off/0.25, exposure multipliers 0.125/1/4,
strengths 0/0.05/1 and sharpen settings 0/0.75/1. Sharpness zero uses the actual
direct unstaged path; the other settings compare `RCAS(half(AgX(base+bloom)))`
against fused `RCAS(AgX(base+bloom))`. Every engine/bloom input is FP16
representable, but reconstructed bloom is supplied directly: this does not
test reconstruction sampling. There are 38,880 cases and 116,640 RGB channel
comparisons per rounding mode, including the direct-path cases.

| Display-stage rounding | Maximum absolute RGB error | Mean absolute RGB error | Largest modeled 8-bit code change | Changed code comparisons |
| --- | ---: | ---: | ---: | ---: |
| Nearest-even | 0.0009615865 | 0.0000453640 | 1 | 9,269 |
| Toward zero | 0.0010517414 | 0.0000945797 | 1 | 1,237 |

Both maximum-error cases occur on smooth fields with strongest RCAS and
strength zero. The single-channel half-store bounds in [0,1] are checked
separately (2^-12 nearest, 2^-11 toward zero); RCAS can amplify that error.
Changed-code counts need not order like mean error because the corpus has
many nearly saturated AgX outputs near a display-code rounding boundary.

These are measurements of this corpus, not universal output bounds or GPU
acceptance. The modeled final conversion is nearest-even to 8-bit; shader
float32 arithmetic, actual output conversion, UV evaluation, bloom sampling
and the combined GPU error are excluded. No arbitrary pass threshold was
chosen from the observed error. The future GPU differential must compare
the real staged result with the independent fused oracle and retain both
absolute and final-code errors, identifying storage and sampler contributions.

Reproduce host controls with
`python3 -m unittest discover -s verification/analysis -p test_bloom_composition.py -v`
and print the storage report with
`python3 verification/analysis/test_bloom_composition.py`.

## Performance and storage

Existing embedded programs, inspected with the conservative SM3 budget gate:

| Existing program | Static slots | Texture instructions | Temporary registers |
| --- | ---: | ---: | ---: |
| AgX | 69 | 1 | 5 |
| AgX with RCAS | 407 | 5 | 11 |
| Display RCAS | 91 | 5 | 9 |
| Bloom candidate (initial four-fetch tent) | 108 | 5 | 6 |

The candidate compiled to 603 words, SHA-256
`7a5f651beee4a3a0dcfcfe4eb843022126789f04459b0cec8ca5f984402d94e6`,
with s0/s1 and no runtime control flow. The two staged programs total 199
static slots across their separate draws; this sum is a comparison aid, not
a single-program resource limit. The durable native reproducer is
`python3 verification/probe/wine_lock.py python3 verification/probe/check_bloom_composition.py`.
Its optional `--compare-fused` compiles a separate diagnostic source with five
bloom/AgX evaluations; compilation or budget failure is recorded and returns
nonzero. The checker leaves all ten existing embedded programs untouched.

The durable comparison run compiled the fused alternative but measured
**597 static SM3 slots**, above the portable minimum of 512, and rejected it.
Its overall comparison record is therefore intentionally failed with stable
inputs: candidate compilation/budget passed, fused budget did not. The staged
route uses 108 + 91 = **199 slots across two separately admitted programs**,
versus 597 slots in the fused single program. This supports the selected
staging topology under the required SM3 minimum; it is not a measured GPU
speed ratio, and the extra stage bandwidth/pass remains part of runtime cost.

The selected bloom shader needs one scene read and the four-fetch tent;
display RCAS adds five reads. Its topology therefore uses ten full-resolution
texture instructions per pixel, versus twenty-five for five fused
scene-plus-tent evaluations. This saves fifteen texture instructions and
four AgX evaluations per pixel while adding one full-resolution FP16 render
target write and one pass. These are analytical counts, not timings;
creation/capability qualification and GPU timing remain required.
The separately evolving tent's final compiled budget must be measured after
its precision investigation, not frozen to this initial source estimate.

No per-scene-draw work is introduced by this shader. Resources should be
retained per device/size outside draw loops, with transactional allocation,
exact Reset release and state-bracket ownership supplied by the renderer.
Do not overwrite TAA history or the retained scene to save staging memory.
All figures below exclude device allocation overhead and existing scene/TAA
textures, and include the separate candidate and genuine-original recovery
image required by the boundary:

| Resolution / bloom levels | Bloom pyramid | Candidate + recovery | Optional display stage | Total with stage |
| --- | ---: | ---: | ---: | ---: |
| 1920×1080 / 5 | 11,033,280 B | 16,588,800 B | 16,588,800 B | 44,210,880 B (42.16 MiB) |
| 3840×2160 / 6 | 44,210,880 B | 66,355,200 B | 66,355,200 B | 176,921,280 B (168.73 MiB) |

Without sharpening, omit the stage: the 1080p total is 27,622,080 B
(26.34 MiB). These are persistent/peak added allocations for a straightforward
separate resource layout. Stage use finishes before original execution, but
retaining the allocation still counts against VRAM. Runtime GPU timestamps
must separately measure bloom, candidate transform, display sharpen, original
compositor and backup/commit work; CPU timing must include boundary state and
lock costs. No game FPS claim follows from shader instruction counts.
