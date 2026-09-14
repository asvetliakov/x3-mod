# Asteroid normal/specular minification study

Derived read-only study, 2026-09-14. This complements the
[native fade analysis](asteroid-fog-temporal.md) and the
[distance-fade composition work](../architecture/linear-distance-fade.md).
No replacement or texture policy is selected by this note.

## Local evidence

Run 27 uses exact pair `167eb2d5629ab9d3/d44db87778a43b61` for both the
near and far BUMP asteroid draws. In captured frame 20744 the two draws bind the
same four texture identities:

| stage | role from PS CTAB | format | dimensions / mips | captured sampling |
|---|---|---|---|---|
| s0 | base/diffuse | DXT1 | 1024 x 1024 / 11 | anisotropic min, linear mag/mip, 16x, bias 0 |
| s1 | AG normal | DXT5 | 1024 x 1024 / 11 | anisotropic min, linear mag/mip, 16x, bias 0 |
| s2 | scalar specular (red) | DXT1 | 1024 x 1024 / 11 | anisotropic min, linear mag/mip, 16x, bias 0 |
| s3 | detail albedo | DXT1 | 1024 x 1024 / 11 | anisotropic min, linear mag/mip, 16x, bias 0 |

Run 27 globally reports `mip_bias=0` and `taa_sharpen=0`. The far draw is the
known alpha-blended/depth-write-off gate-4 path and does not receive the opaque
material/motion transform; the near draw is routed. The capture contains
texture metadata and identities, but no texture texels, mip contents, chosen
per-pixel LOD, or selected-target-to-node identity.

The original BUMP shader and the independent numerical oracle agree on these
equations. With the sampled DXT5 normal `S`, interpolated tangent `T`, binormal
`B`, geometric normal `Ng`, and view vector `V0`:

```
x = 2*S.a - 1
y = 2*S.g - 1
q = 1 - x*x - y*y
z = sqrt(abs(q))                 // exact RSQ/RCP behavior for ordinary finite q != 0
N = normalize(y*T + x*B + z*Ng)
V = normalize(V0)
```

DEFAULT instead uses `N = normalize(Ng)`. For directional light `j`, with
direction `Lj`, decoded linear color `Cj`, and scalar specular-mask red `m`:

```
d  = saturate(dot(N, Lj))
R  = 2*dot(N, Lj)*N - Lj
h  = saturate(dot(V, R))
fj = d + m*saturate(3*d)*h^3
```

The final directional term is `direct_gain * sum(Cj*fj)`. Albedo is
`base_weight*decode(base.rgb) + detail_weight*decode(detail.rgb)`, and the
linear output is `albedo * (vertex_linear_rgb + directional)`. Thus the
specular term is albedo-modulated. There is no Fresnel, reflection cube, roughness
input, or outer factor of three. Alpha remains `base.a * vertex_alpha`.

All original normal reconstruction, normalization, reflection, dot and cubic
operations carry D3D9 `_pp`; the current material transform deliberately retains
those flags. D3D9 permits an implementation to use reduced precision for `_pp`
instructions. `rsq` also takes the absolute value of its input, so a compressed
or filtered AG sample outside the unit disk folds back to a positive `z` rather
than clamping to the disk. Actual q<0 prevalence is unknown without the texture.

This makes normal/specular minification a plausible independent contributor:
the same changing `N` drives diffuse on both sides and specular chiefly on the
illuminated side. It is not isolated by Run 27, and the fixed exponent 3 is much
broader than the very high-power lobes for which specular aliasing is usually
most severe. Alpha/background mixing, subpixel triangle coverage, albedo/detail,
and TAA correspondence remain separate contributors.

## Shared decode: the station docking-port pair

The station docking-port pair `4944d81dfe531b37/64bac8bb307eb896` uses the identical
AG decode, `sqrt(abs(q))` fold, `N = normalize(y*T + x*B + z*Ng)` reconstruction and
`R = 2*(N·L)*N - L` reflection, with the same anisotropic/trilinear sampling of a
DXT5 1024² normal map at bias 0. The length signal is discarded there for the same
reason, so options 3-5 below apply unchanged to that material. Its differences and
its second, alpha-side darkening channel are in
[station-material-distance.md](station-material-distance.md), section "Docking-port
pair: minification analysis": exponent 6 with an outer `g_MatSpecularStrength = 3`,
`g_MatDiffuseStrength = 0.5`, a cube reflection, an additive unmipped lightmap, no
ambient or emissive floor, and a source-over alpha taken from the minifiable
diffuse-texture alpha.

## Options, in test order

1. **Causal A/B before an appearance change.** On this exact pair, independently
   force a coarser normal mip (s1) and specular-mask mip (s2), for example bias
   +0.5 then +1.0, across both opaque and fogged draws. Add specular-off and flat-
   normal modes only as diagnostic extremes. The existing routed-draw mip-bias
   mechanism cannot test the reported far draw because gate 4 bypasses it.
   Per-component temporal variance from fixed-view/slow-turn captures would say
   whether s1, s2, or neither owns most of the shimmer.

2. **Precision cleanup and reconstruction experiment.** Make the asteroid
   normal/reflection chain full precision, which preserves the mathematical
   equation and gloss. Separately compare native `sqrt(abs(q))` with a robust
   `sqrt(max(q, 0))` reconstruction. The q clamp explicitly changes the
   reconstruction for out-of-disk samples, and its scope cannot be justified
   until s1 mip texels are read back and q<0 rates are measured. These changes
   target precision/fold instability; neither is a minification filter.

3. **Energy-aware specular broadening.** Estimate screen-space normal variance
   using PS 3.0 `dsx/dsy`, reduce the effective exponent from `p=3` to `p'`, and
   scale the lobe peak by `(p'+1)/(p+1)`. At zero variance this is exactly the
   native cubic; at higher variance it trades a lower peak for a wider highlight.
   The scale preserves the integral only for the ideal unoccluded Phong lobe over
   its hemisphere. X3's albedo modulation, `saturate(3*d)` gate, visibility and
   finite-domain truncation prevent a global energy-conservation guarantee.
   This follows the intent of Toksvig/Filament rather than multiplying the
   specular mask down. It is source-only and fits PS 3.0, but screen derivatives
   cannot recover subtexel variance already lost by the sampled mip and it does
   not fix diffuse normal aliasing. Because p is only 3, expect a bounded effect.

4. **Footprint shading average.** Use two samples along the major UV derivative,
   or four quarter-footprint samples, reconstruct each normal, evaluate diffuse
   plus specular for each, and average radiance. Explicit PS 3.0 gradients can
   select a finer child footprint. This directly filters the nonlinear shading,
   addresses both dark-side diffuse and lit-side specular variation, and best
   preserves visible gloss with existing assets. It costs extra normal/specular
   fetches and repeated two-light arithmetic, so a two-tap prototype and game GPU
   timing should precede four taps.

5. **Normal-distribution mip data.** Toksvig uses the shortened length of an
   averaged normal to infer variance, lower the Phong exponent, and reduce the
   widened lobe's peak to conserve energy. The current two-component AG decode
   reconstructs z and normalizes, so it discards that length signal; classic
   Toksvig cannot be applied from the current sampled normal alone. A copied
   texture with a variance channel, or LEAN/CLEAN-style first/second moments,
   would provide stable texture-space filtering at low per-pixel cost and better
   preserve aggregate gloss. It requires texture creation/upload interception,
   memory, binding and Reset/lifetime work, and validation that the source normal
   texture is not shared with incompatible shaders.

A positive LOD bias alone is the cheapest mitigation but deliberately removes
normal/specular detail. It is a useful diagnosis or fallback, not the preferred
appearance-preserving solution. If retained, apply it selectively to s1/s2 and
pair normal smoothing with energy-aware specular broadening.

## TAA interaction

Spatial normal/specular filtering must occur before TAA so each current sample
has bounded radiance. TAA can then integrate the remaining jitter-phase samples;
it should not be asked to turn discontinuous cubic highlights into a stable
material by history alone. Run 27 did use history on 391/395 active samples. The
fogged draw writes neither RT1 nor RT2: its pixels inherit underlying opaque
motion/depth where an opaque surface was already written, or retain the
sentinel/far-plane camera path otherwise. Neither case necessarily represents
the asteroid's own surface correspondence. Any distance-fade prototype should
apply the same material filter to the surface contribution before alpha
composition and expose appropriate reactive/coverage semantics. Specular AA
does not resolve the far alpha overlap/order problem and cannot substitute for
required TAA.

## Primary sources

- Michael Toksvig, *Mipmapping Normal Maps* (NVIDIA technical brief / JGT):
  https://developer.download.nvidia.com/assets/gamedev/docs/Mipmapping_Normal_Maps.pdf
- Google Filament material documentation, specular anti-aliasing controls:
  https://google.github.io/filament/Materials.md.html
- Marc Olano and Dan Baker, *LEAN Mapping*:
  https://userpages.cs.umbc.edu/olano/papers/lean/
- Microsoft D3D9 PS 3.0 gradient and sampler references:
  https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d9types/ne-d3d9types-_d3dshader_instruction_opcode_type
  https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dsy---ps
  https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx9-graphics-reference-asm-ps-instructions-modifiers-ps-2-0
  https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/rsq---ps
  https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dsamplerstatetype
