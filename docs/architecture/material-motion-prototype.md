# Detached material motion transformer

`material_motion_variant` builds a pair of owned shader variants for one reviewed
SM3 material. It is not linked into the proxy's CMake target and performs no D3D
calls. The prototype tests shader behavior before choosing the live binding and
history integration. The [strategy](motion-output-strategy.md) records why this
is being compared with replay; the [candidate review](../reverse-engineering/motion-output-candidate.md)
records exact programs, register headroom and captured draw state.

## Program changes

The VS gains one declaration and four dot products at a point where the original
homogeneous vertex position is still available. Those products apply previous
submitted WVP rows in c252–255 and write o6/TEXCOORD4. Existing vertex fetch,
current position, material outputs and instruction order are unchanged.

The PS gains matching v5/TEXCOORD4 plus a relocated copy of our authored motion
program. Its input moves from v0 to v5, temporaries from r0–2 to r5–7, constants
from c0–4 to c216–220, and output from oC0 to oC1. Relocation preserves operand
swizzles, modifiers and masks; DEF literal bits are copied unchanged. The new
definitions and declaration are inserted in the header, and executable work
follows the original material instructions. The original comments, preshader
metadata and color output remain intact.

RT1 uses the existing RGBA32F previous-UV/depth/validity format. This is motion
correspondence rather than an independent displacement convention: the temporal
consumer knows the current pixel coordinate. Reusing the authored program keeps
its previous-W/depth validity checks, invalid sentinel and jitter handling.

| Input | Meaning |
| --- | --- |
| VS c252–255 | Previous submitted clip-position rows for the corresponding geometry |
| PS c216 | Inverse viewport width/height; previous jitter in UV units |
| PS c217.x | One to request valid history, zero to write the invalid sentinel |
| RT1 | RGBA32F previous UV, previous clip Z/W, and validity |

## Qualification and lifetime

The transformer requires the exact full-program fingerprints and lengths of
both reviewed shaders, validates instruction framing and original position
sites, and validates the supported shape of the authored fragment before
relocating it. It does not patch arbitrary programs or recognize instruction
bytes inside comments. Original and modified program identities stay distinct.

Transformation is creation-time work with owned vector allocations, not work to
repeat per draw. It builds both programs locally and publishes them together
only on success. Inputs may alias either existing output vector. Unsupported
input or allocation failure leaves the previous output intact. There is no
global cache, device retention, native shader compilation or callback in this
module.

The host structural fixture passes in optimized and ASan/UBSan builds. It
reconstructs both originals exactly after removing additions, independently
checks relocated operands and literals, rejects all 57,152 single-bit input
mutations without changing output, and exercises six successful alias layouts.
These are qualification and memory/structure checks; shader execution is
verified separately. See `verification/results/material-motion-structure-summary.json`.

## Caller requirements still outside this module

The caller must establish opaque scene coverage and compatible MRT dimensions,
formats, write masks and state. It must not replace an application-owned RT1.
The initial route requires alpha testing, blending and sRGB writes disabled,
SM3 support and at least 256 VS float constants.
The original VS uses relative light constants, so the actual light count must be
bounded to 0–8 before reserving high constants. A shader fingerprint does not
validate the runtime count.

Previous rows require object/geometry correspondence and history validity. The
current pixel ABI assumes a zero-origin viewport and the established jitter
convention, with matching history dimensions. Resize or Reset invalidates that
history. Previous viewport MinZ/MaxZ must be 0/1 for its clip Z/W output to
equal previous device depth. Inverse dimensions must be finite and positive;
jitter values must be finite. Unknown history must produce invalid motion. CPU-changing geometry,
other materials, transparency, overlays and camera cuts need separate handling.

Finally, a live integration must own and restore its shader/constant/RT changes
and handle concurrent application calls, native failures and Reset. The pure
transformer does not provide those boundaries. Detached fixture resource cleanup
must not be described as complete live-hook state restoration or gameplay TAA.
