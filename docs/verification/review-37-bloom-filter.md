# Review 37: bloom numerical core

This checkpoint covers authored shader sources, the CPU layout/constants ABI,
independent host oracle and offline compiler/budget verification. It does not
implement the engine boundary or a GPU bloom pass in the installed renderer.
The [boundary design](../architecture/hdr-bloom-boundary.md) has its own completed
independent review; GPU execution, integration and game acceptance remain open.

## Findings and fixes

| Finding | Resolution |
| --- | --- |
| Base scene could be clipped to 65504 by a shared extraction helper, changing strength-zero behavior | Composition now accepts the existing decoded/clamped/exposed base directly, preserving values above 65504 and identity-decode negatives. Only scratch extraction is bounded to FP16. |
| Rounded reciprocal geometry selected a preceding texel at valid ceil-half boundaries | Integer support uses `base=2*j-1`, with weights derived from integer parity and numerators. New float32 controls catch the former 31.984375-unit leakage beside an FP16-max sample at width 8462, and cover odd/thin/endpoints through 16384. |
| Nine decode/prefilter calls ran even for a four-contributor footprint | The validated source-parity selector chooses a four-tap extraction program for both-even dimensions. Generic area extraction remains for odd/thin dimensions. |
| Per-tap uniform decode branches pushed generic extraction to 527 slots, above the portable minimum | Six compile-time extraction variants select decode mode and parity. No unused transfer function or runtime decode branch remains. All required programs pass the 512-slot budget; the largest uses 362. |
| Historical SM2-style instruction counting treated TEXLDL as one slot | The new scoped SM3 gate uses the official non-cube cost of two slots and refuses unsupported opcode/forms. Boundary controls reject the former off-by-one admission. |

Initial source review and geometry-fix re-review used Sol xhigh. The user then
changed future reviews to Sol high; final checker/artifact and specialization
review used that setting and approved the frozen sources and final artifacts
with no blocking findings. All twenty host tests passed, and all eight final
native compiler results matched their retained headers, bytecode and manifests. Astra implemented fixes; root independently checked
operation order, proposed the exact integer support and verified the official
instruction-cost table.

## Verification and performance scope

The [verification record](hdr-bloom-filter.md) records seventeen filter host
tests, three static-gate controls and eight native compiler results with exact
input/artifact hashes. Native shader creation, differential GPU readbacks,
FP16/nonfinite behavior and timings remain pending. No game was launched and
the installed DLL was not changed by these tools.

At 1920×1080/five levels, four-fetch reconstruction plus even extraction reduces
analytical bloom texture instructions from 31,763,400 to 15,360,600. The first
level's common gamma extraction falls from the initial generic shader's 472
static slots to 145. These are code/shape costs, not a measured FPS improvement.
The selected integration also retains the original compositor and requires
candidate/recovery images; its total GPU and memory cost must be measured.

The compiler record binds shader/compiler inputs and checker dependencies; it
does not claim a complete execution-environment fingerprint. The tracked
`wine_lock.py` coordination wrapper is outside that hash scope.
