# GPU depth reconstruction from a native D24X8 snapshot

The original [decoder shader](../../src/temporal/depth_decode.hlsl) reconstructs
raw device depth into R32F from the comparison-sampled D24X8 texture produced by
[the original-preserving RESZ copy](depth-resolve.md). **256/256 precision samples
pass**, including exact zero/one sentinels, their adjacent D24 values, far depths,
source clearing, 64×64 and 1280×768 targets, and resource recreation after Reset.

This supplies a verified GPU-only fallback input for temporal work on this
backend. It is not yet injected into X3 and does not establish game TAA quality
or final-frame performance.

## Shader contract and endpoint handling

The shader expects sampler zero to be a native D24X8 texture with comparison
semantics `reference <= stored_depth`, point filtering, clamp addressing, no
mipmaps and no sRGB. It uses pixel-center UVs at the source dimensions and writes
raw device depth to an **R32F** target. Do not feed it INTZ raw-depth samples or
use an FP16 intermediate for the reconstructed depth.

A 24-step binary search supplies comparison references through `tex2Dproj`'s
third coordinate. Two additional comparisons handle the endpoints: a reference
of exactly one identifies clear/far depth one, and a reference of half a D24
step distinguishes zero from the smallest positive representable D24 value.
The final algorithm therefore performs **26 comparison fetches per pixel**:
24 dependent search steps plus two endpoint tests. It does not classify values
such as 0.9999 as background with an arbitrary epsilon.

The earlier search-only experiment returned clear depth one about two D24 steps
low. The endpoint checks now preserve **exact 0 and exact 1**; explicit test gates
also require every interior source depth to remain strictly between zero and one.
A tolerance-only check would not have established that distinction.

Independent review found a second endpoint risk: IEEE float32 addition can round
an interior search midpoint up to exactly one. The shader now clamps that
midpoint to `[2^-25, 1 - 2^-24]` before applying the explicit endpoint comparisons.
Only the comparison at reference one can emit the clear/far sentinel; the zero
comparison likewise controls the zero sentinel. This adds no texture fetches.

An independent [CPU oracle](../../verification/analysis/test_depth_decode_oracle.py)
checks 8,345 held-out D24 codes, including exponent transitions, endpoint clusters
and a different random seed from the GPU fixture. It models both exact-normalized
D24 comparison and float32-normalized comparison. The old midpoint emits one for
code 16,777,214 in the float32 comparison model; the clamped version keeps it at
`1 - 2^-24`. The exact-D24 model does not reproduce that particular old failure,
consistent with the earlier GPU sample passing. Both corrected models preserve
all sampled endpoint/interior distinctions and stay within two D24 steps (maxima
approximately one and 1.5 steps respectively). These are numerical models, not a
claim about the backend's exact hidden representation. See the
[oracle report](../../verification/results/depth-decode-oracle.json).

## Numeric and state verification

The [standalone fixture](../../verification/probe/depth_decode.cpp) compiles the
actual shader file. Original geometry fills 32 tiles with these depths:

- 0, 0.25, 0.75, 1, 0.9999 and 0.99999;
- one and two D24 steps above zero and below one;
- 22 reproducible pseudorandom 24-bit values from fixed seed `0x13579bdf`.

The fixture renders to an ordinary D24X8 depth surface, copies it to a separate
native D24X8 texture **inside the caller's already-open BeginScene**, and issues
no dummy draw or nested BeginScene/EndScene in the copy helper. It decodes and
checks samples, restores caller state, clears the original depth surface to
0.9375, and decodes/checks the unchanged snapshot again. The entire sequence is
repeated at both sizes before and after Reset.

The acceptance limit is two D24 steps against each submitted depth
(`2 / 16777215`), plus the exact endpoint/interior gates above. Observed maximum
error is **one D24 step** in this set. Examples from 1280×768:

| Source value | Decoded R32F | Meaning |
| --- | --- | --- |
| 0 | 0 exactly | Zero sentinel retained |
| 1 | 1 exactly | Clear/far sentinel retained |
| 0.999899983 | 0.999899983 | Far-depth detail retained |
| 0.999989986 | 0.999989986 | Far-depth detail retained |
| 0.0000000596 | 0.0000000894 | Smallest positive value stays positive; half-step error |
| 0.999999940 | 0.999999881 | Adjacent-to-one geometry stays below one; one-step error |

Adjacent non-endpoint values can still reconstruct to the same float within the
allowed error; this is not a bit-exact extraction of every D24 value. The fixture
samples a bounded set rather than exhaustively testing all 16,777,216 values.
R32F prevents adding FP16 quantization but cannot recover precision absent from
the original D24 projection.

All **four inside-scene copy state checks** and **eight decoder restoration
checks** pass. Copy checks preserve original depth identity, sampler zero and
POINTSIZE bits. Decoder restoration additionally verifies original render target,
pixel shader, depth-enable and depth-write states after applying a captured state
block and explicitly restoring targets/viewport. Resources are released before a
successful Reset. No game, installation or bottle setting is changed.

## Cost measurement and its limits

The backend rejects timestamp, timestamp-frequency and timestamp-disjoint queries
with `8876086a` (not available), so **no GPU timestamp duration is claimed**.
The fixture reports CPU wall time through a successful D3D9 EVENT-query completion.
There is no pixel readback inside either timed loop. Eight warm-up draws precede
each measurement; CPU pixel readback is used only for separate correctness checks.

Two measurements are retained. A 32-draw identical batch completes in about
0.047–0.049 ms/draw at 64×64 and 0.086–0.092 ms/draw at 1280×768, but later full-screen
draws can hide earlier work in a tile renderer, so these numbers are **not** a
reliable independent-frame decode cost. The stronger latency check completes a
separate EVENT query after **each** of 16 draws, preventing such inter-draw hiding:

| Size | Mean completed draw, generation 0 / 1 | Median, generation 0 / 1 |
| --- | --- | --- |
| 64×64 | 1.265 / 1.259 ms | 1.279 / 1.263 ms |
| 1280×768 | 2.031 / 1.708 ms | 2.380 / 1.286 ms |

These times include CPU submission, driver/runtime work, GPU completion and a
**1 ms polling sleep** when the query is pending. The sleep dominates the small
case; results are not isolated GPU execution times. They exclude shader creation,
RESZ copy, temporal resolve, presentation and all X3 rendering. This establishes
a bounded prototype cost observation, not a release-performance guarantee.

## Reproduction and evidence

```sh
python3 -m unittest verification.analysis.test_depth_decode_oracle verification.analysis.test_depth_runner_provenance
python3 verification/analysis/test_depth_decode_oracle.py --report
python3 verification/probe/run_depth_decode.py
```

[Runner](../../verification/probe/run_depth_decode.py),
[numeric/timing log](../../verification/results/depth-decode.txt),
[summary and source/shader/executable hashes](../../verification/results/depth-decode-summary.json),
[backend stderr](../../verification/results/depth-decode-wine.log).

The runner always freshly builds the x86 executable and checks the fixture,
build script, runner and shader hashes before/after compilation and again after
the run; the executable hash must also remain unchanged during the run. A failed
build cannot fall back to a stale executable. Five offline runner tests cover
fresh builds, build failures and source/executable mutation (both depth runners).
The retained GPU run passes all stability checks and builds without warnings.
The runner uses CrossOver Preview's Steam
bottle and a process-local `d3d9=b` override with a 60-second timeout. The renderer
integration must still choose the matching scene color/depth boundary, maintain
history lifetimes and handle reset/cuts, jitter, motion and HUD exclusion.
