# Review 40: bloom display composition

2026-09-13. Independent Sol high review approved the new
[bloom composition shader and staging design](../architecture/hdr-bloom-composition.md).
There is no renderer integration or GPU composition acceptance yet.

The shared AgX exposed-tail helper preserves the existing decode, decoded-space
clamp and exposure order. Bloom is added immediately before the inset matrix,
without another decode/clamp/exposure or FP16 store of that sum. Native
recompilation produced identical headers/bytecode for all ten existing programs.
The [parity record](../../verification/results/bloom-agx-refactor-parity.json)
was strengthened during review to embed the ten native manifests and bind
compiler, input/include/tool and artifact identities. No recompilation was
needed for that record fix.

The [native comparison](../../verification/results/bloom-composition-compile.json)
qualifies the selected candidate at 108/512 SM3 slots, six temporaries and
no flow control. The optional fused five-evaluation diagnostic compiled to
597 slots and was correctly rejected. The raw comparison terminal is failed
because of that negative witness; the selected program's budget passed.

Seven composition tests, three compiler-checker controls and all seventeen
existing AgX tests passed independently. A storage-only corpus compares
38,880 cases / 116,640 RGB channels for each of nearest and toward-zero FP16
rounding. Its largest quantized display difference is one 8-bit code; this
is not a GPU precision guarantee. Strength zero retains the modern candidate
route with no added modern bloom; only disabling the feature restores the
legacy compositor route.

The selected sharpen path prepares display FP16 once, then applies the existing
RCAS shader to an RGBA8 candidate. This requires ten full-resolution texture
instructions instead of twenty-five in the fused comparison, plus FP16
staging memory. The two selected programs total 199 static slots across two
draws; this is not a single-program budget or measured frame cost. The post
copy still preserves the original main alpha. GPU numerical/state/recovery
tests, memory/timing measurements and game acceptance remain.
