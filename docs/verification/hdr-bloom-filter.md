# HDR bloom filter verification

The [filter core](../architecture/hdr-bloom-filter.md) is a standalone numerical
component. It is not integrated into the renderer or installed. The current
camera build and user run plan remain unchanged.

## Evidence available

- Seventeen host filter tests pass, including the independent nine-tap oracle
  versus the four-fetch tent, float32 ceil-half support, odd/thin dimensions,
  constant gain, threshold/exposure order and C++ ABI/variant selection.
- Three additional host controls test the static-budget gate, including the
  two-slot TEXLDL boundary, sparse high registers and unsupported shader forms.
- All eight authored kernels compile with the native D3DX compiler as `ps_3_0`
  and pass the conservative static budget. The
  [compilation record](../../verification/results/bloom-filter-compile.json)
  binds input hashes, compiler, runner/parser/helper sources, transitive shader
  includes and retained local headers/manifests.

| Program | Bytecode words | Static instruction slots |
| --- | --- | --- |
| Area extraction, gamma 2.2 | 1,375 | 330 |
| Area extraction, sRGB | 1,498 | 362 |
| Area extraction, identity | 1,060 | 245 |
| Even extraction, gamma 2.2 | 658 | 145 |
| Even extraction, sRGB | 722 | 160 |
| Even extraction, identity | 518 | 108 |
| Downsample | 358 | 78 |
| Reconstruction | 255 | 45 |

All use at most eleven temporaries, constants through c28 and two 2D samplers;
none retains runtime flow-control branches. The checker uses Microsoft's
[SM3 instruction costs](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx9-graphics-reference-asm-ps-instructions-ps-3-0),
including two slots for non-cube TEXLDL, and the
[512-slot minimum](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx9-graphics-reference-asm-ps-differences).
It refuses unqualified opcodes/forms and out-of-budget resources. These are
static limits, not executed-operation counts or performance measurements.

This compilation creates no D3D device. It establishes neither device shader
creation nor FP16 rendering, filtering precision or GPU timing. The primary
host oracle uses double precision; separate float32 support controls check the
coordinate regression, but these tests do not establish GPU FP16 stores or SM3
nonfinite behavior. The initial three-kernel result and the later 527-slot
branching failure are historical; the eight specializations above supersede both.

Reproduce the compilation, with the game stopped, using:

```sh
python3 verification/probe/wine_lock.py --holder bloom-kernel-compile python3 verification/probe/check_bloom_shaders.py
```

The compiler runner retains authored bytecode locally and publishes a scoped
summary; it does not embed these programs in the installed DLL. Once compilation
starts, a failed run publishes a failed terminal rather than leaving its previous
PASS as current. Guard/input preflight refusals do not begin a new run.

## Next gates

A standalone GPU fixture is being prepared to compare every pyramid stage to
the independent oracle with explicit input and intermediate FP16 quantization.
It must cover black/DC, impulse/chroma, odd and one-pixel dimensions, threshold,
exposure/clamp extremes, Reset and resource cleanup. GPU qualification and
native Windows runtime testing remain pending.

Renderer integration follows the separate
[pre/post-compositor contract](../architecture/hdr-bloom-boundary.md). It must
retain bloom-free TAA history and meter inputs, preserve the existing base
AgX decode/clamp/exposure order, and overwrite RGB from retained pre-original
inputs after the original compositor returns. Preserve its actual destination
alpha, outgoing state and intermediate resources; recover its completed main
image if the replacement write or restoration fails.

The renderer needs explicit s1 texture/sampler preservation and a c0..c28
constant snapshot. Existing fixture snapshots cover only c0..c7 and must be
expanded with hostile canaries. FP16 linear filtering is currently logged by
HdrPass without gating anything: the optimized bloom path must qualify that
capability and shader creation independently of HDR/TAA/AgX availability.
All per-level allocation, preparation, backup, write and restoration failures,
owner/frame/reset changes and reference cleanup require fault coverage.

Measure the full selected path: the retained original compositor, modern
pyramid, candidate preparation, original-image backup, post-return RGB copy and
state/CPU boundary overhead. The analytical tap and memory counts in the design
are not GPU timings or FPS. If sharpen is included, measure it separately;
fusing bloom reconstruction into every RCAS tap can multiply full-resolution
texture work. Reuse allocations and avoid production CPU readback.
