# Review 38: standalone bloom GPU fixture

2026-09-13. Independent Sol high review approved the fixture source and honest
rejected-run checkpoint with no blocking source/documentation findings. This
is not numerical qualification of the bloom filter.

The [fixture](bloom-filter-fixture.md) ran 40 cases over two generations with
Reset, all six extraction variants, 540 stage images and no dimension skips.
The runner correctly rejected 38 reconstruction images; every downsample
stage and both sampling controls passed. Rejections repeat after Reset.
The [compact record](../../verification/results/bloom-filter-first-gpu-failure.json)
matches the full retained report and binds its hash, inputs and rejected stages.

Four host tests and isolated x86 cross-compilation with SSE2, incoming-stack
realignment and warnings as errors passed independently. Review confirmed
per-case texture unbinding and backbuffer restoration release default-pool
references before Reset. Shader/declaration lifetime across Reset is valid.
Per-pass allocation and readback are diagnostic; no production cost or FPS
claim follows from this fixture. The installed DLL is unchanged.

A nonblocking test-quality observation remains for the next fixture revision:
the current intermediate-quantization test proves half-representable stage
values, but does not independently reject a pipeline that passes unquantized
intermediates onward. Code inspection confirms that the present implementation
quantizes before each next stage. Add an adversarial downstream propagation
control alongside the planned independent store/sampler characterization.

The next gate is to distinguish FP16 store rounding and texture-filter precision
from a filter defect using independent GPU controls. The fixed numerical
tolerance has not been widened. GPU quality, renderer integration, native
Windows behavior and game appearance remain unaccepted.
