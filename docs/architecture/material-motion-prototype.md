# Table-driven material motion transformer

`material_motion.{h,cpp}` builds owned shader variants for the reviewed SM3
material pairs. It performs no D3D calls; the live route
([live-motion-route.md](live-motion-route.md)) creates the D3D objects from its
output at shader-creation time. The [strategy](motion-output-strategy.md)
records why same-draw output was chosen over replay; the
[candidate review](../reverse-engineering/motion-output-candidate.md) records
the Argon reference splice, and the
[profile table](../reverse-engineering/motion-output-profiles.md) derives the
same facts for the other captured material programs.

## Program changes

The VS gains one declaration and four dot products at a point where the original
homogeneous vertex position is still available. Those products apply previous
submitted WVP rows in c252–255 and write the row's output register as a new
TEXCOORD interpolator (o6/TEXCOORD4 for the reference pair). Existing vertex
fetch, current position, material outputs and instruction order are unchanged.

The PS gains the matching input declaration plus a relocated copy of our
authored motion program. Its input moves from v0 to the row's input register,
temporaries from r0–2 to the row's three temporaries, constants from c0–4 to
c216–220, and output from oC0 to oC1. Relocation preserves operand swizzles,
modifiers and masks; DEF literal bits are copied unchanged. The new definitions
and declaration are inserted in the header, and executable work follows the
original material instructions. The original comments, preshader metadata and
color output remain intact.

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

Every table row uses exactly these constant ranges and oC1 (a `static_assert`
in `material_motion.cpp` checks the table against `MaterialMotionAbi`), so the
route uploads the same ranges for every pair.

## The table and the classes

`src/renderer/motion_output_profiles.h` defines `MotionOutputProfile` and
`MotionOutputClass` and includes the generated row list
`motion_output_profiles_inc.h` (12 rows today: six class A, six class B). A
row carries the original fingerprints, lengths and version tokens, the matrix
register and position temporary, the four position DP4 offsets and lane masks,
the five insertion offsets, the four register choices and the light-loop
bound flag. Rows are derived numbers only; the header states that they are
transformer input, not an eligibility decision.

| Class | Behavior |
| --- | --- |
| A, `ReferenceRegisters` | The reference registers are free in both programs; the emitted words are the Argon fragment with only the insertion offsets and position registers taken from the row. The Argon row produces the same 545/1392-word programs as the earlier hand-written transformer (FNV `07805a216f19b9b6` / `92bd1a32ae7d2a6d`, checked by the structural fixture). |
| B, `RelocatedRegisters` | Same code path; the row names the free VS output/TEXCOORD index and PS input/temporaries, and the authored fragment and the new declarations are relocated to them. |
| C, `RelocatedRegistersWithBranches` | Reserved enumerator; not in the table. The transformer refuses such a row (`UnsupportedShader`) because the pixel-side validation below requires straight-line code. |

Compile-time checks in the header prove that every row is well formed
(register ranges, ordered offsets, XYZW lane masks, contiguous DP4 quad,
END at the append point) and that rows sharing an original program agree on
that program's side of the splice, which the per-program variant scheme in the
live route depends on.

## Qualification at transform time

Each stage is matched to a row by exact full-program fingerprint, DWORD count
and version token (`UnsupportedShader` otherwise). The row's structural facts
are then revalidated against the actual words, and any inconsistency yields
`ProfileMismatch` with the output untouched:

- Instruction framing over the whole program, comments included as opaque
  instructions, with END exactly at the last word.
- VS: a contiguous DEF/DCL header ending exactly at the declaration insert
  and declaring o0 as POSITION0 (so the dots below are the clip position); no
  DEF or DCL afterwards; the four position DP4s at the recorded offsets with
  the exact opcode, o0 lane mask, position temporary and `c<matrix + lane>`
  source; the arithmetic insert one past the last DP4; no original
  declaration, write or read of the chosen output register, TEXCOORD index or
  c252–255 (every parameter token of every instruction is walked, address
  tokens skipped); relative addressing accepted only when the row records the
  draw-time light-loop bound.
- PS: DEFs only before the definition insert, DCLs only between it and the
  declaration insert, executable code afterwards; the END token at the append
  point; no control-flow, predicated or `texkill` instruction, no oDepth
  write and no relative addressing anywhere (classes A and B are defined as
  straight-line programs whose depth is the rasterized depth, which the
  previous-depth output relies on); no original reference to the chosen input
  register, TEXCOORD index, three temporaries, c216–220 or oC1.
- The authored fragment itself is parsed and relocated only if it has the
  expected shape (three DEFs, one declaration, one color output, known
  straight-line opcodes).

The row-explicit entry points (`material_motion_*_variant_for`) take the row
as an argument so the structural fixture can hand in deliberately perturbed
rows, and perturbed programs under a row copy carrying their fingerprint, and
prove each of these refusals; through the table lookups they are unreachable,
since the fingerprint gate refuses first. The lookups match a program to its
first row of a supported class, so a program that a deferred-class row also
names still transforms for its class A/B pairs.

Transformation is creation-time work with owned vector allocations, not work to
repeat per draw. The pair form builds both programs locally and publishes them
together only on success; the per-stage forms replace their output only on
success. Inputs may alias the output vectors. Unsupported input or allocation
failure leaves the previous output intact. There is no global cache, device
retention, native shader compilation or callback in this module.

The host structural fixture passes in optimized and ASan/UBSan builds for all
12 rows. It reconstructs both originals exactly after removing additions,
independently checks relocated operands and literals, refuses 21 row
perturbations and 24 program perturbations per row (declared or written
reserved registers, missing POSITION0, definitions after the header, refused
opcodes, predication, relative addressing, oDepth, malformed END and length
overrun) and all 622,336 single-bit input mutations without changing output,
and exercises six successful alias layouts per row. These are qualification
and memory/structure checks; shader execution is verified separately. See
`verification/results/material-motion-structure-summary.json`.

## Caller requirements still outside this module

The caller must establish opaque scene coverage and compatible MRT dimensions,
formats, write masks and state. It must not replace an application-owned RT1.
The initial route requires alpha testing, blending and sRGB writes disabled,
SM3 support and at least 256 VS float constants.
Every table VS uses relative light constants, so the actual light count must be
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
