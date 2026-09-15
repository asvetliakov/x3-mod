# Selective material exposure verification

Owning [implementation contract](../architecture/material-selective-exposure.md).
Current installed-build state is in [status](../status.md).

## 2026-09-15 create-time checkpoint — independent review PASS

Selective shader variants are implemented for 137 stages (29 VS, 108 PS), with
no live routing or user-facing mode yet. Reviewer accepted patch
`d72dfced9b129ac85bb5d3087b22d92b2a36f97d3bf1576de5467acd671d3c0b`
and independently passed the nine-test focused module in 33.328 s. The
119 required-module tests and exact 2,192 ordinary-variant comparisons were
reused. Required MinGW x86 SSE2/stack syntax verification passes.

The [material RE evidence](../reverse-engineering/remaining-hull-materials.md)
records diffuse-only seed ancestry, constant/scratch reservations, atomic
publication, point/emissive separation, near-cap output ceilings, fade scratch
normalization, emitted fragments and independently separated component oracles.
Historical constant-port-repair expectations were updated only after comparison
with clean `070df80` proved ordinary byte output unchanged.

Important limit: the emitted PS oracle starts at the diffuse/specular join with
synthetic registers. It establishes the post-angular algebra and tail, not
whole-program GPU behavior. GPU shader creation/rounding, FP16 composition,
live constant transactions/caches/Reset, native Windows execution and gameplay
appearance remain pending. The current selective output marks sun share invalid;
joint shadow qualification remains required before shadows acceptance.
