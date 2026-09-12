# Table-driven material motion transformer

`material_motion.{h,cpp}` builds owned shader variants for the reviewed SM3
material pairs. It performs no D3D calls; the live route
([live-motion-route.md](live-motion-route.md)) creates the D3D objects from its
output at shader-creation time. The [strategy](motion-output-strategy.md)
records why same-draw output was chosen over replay; the
[candidate review](../reverse-engineering/motion-output-candidate.md) records
the Argon reference splice, and the
[profile table](../reverse-engineering/motion-output-profiles.md) derives the
same facts for every SM3 material pair a technique pass of the installed
effects binds, so coverage no longer depends on which sectors a capture visited.

## Program changes

The VS gains one declaration and four dot products at a point where the original
homogeneous vertex position is still available. Those products apply previous
submitted WVP rows in c252–255 and write the row's output register as a new
TEXCOORD interpolator (o6/TEXCOORD4 for the reference pair). Since temporal
step 1 a second declaration and two more dot products follow: the current
clip z and w of the same position temporary against `c<matrix+2>` and
`c<matrix+3>`, written to the row's depth output register as `(z, w, z, w)`
(o7/TEXCOORD5 for the reference pair). Existing vertex fetch, current
position, material outputs and instruction order are unchanged.

The PS gains the matching input declaration plus a relocated copy of our
authored motion program. Its input moves from v0 to the row's input register,
temporaries from r0–2 to the row's three temporaries, constants from c0–4 to
c216–220, and output from oC0 to oC1. Relocation preserves operand swizzles,
modifiers and masks; DEF literal bits are copied unchanged. The new definitions
and declaration are inserted in the header, and executable work follows the
original material instructions. The original comments, preshader metadata and
color output remain intact. For rows with `depth_output` the relocated
current-depth fragment (`src/temporal/current_depth_ps.hlsl`: one TEXCOORD
input, `rcp`, `mul`, no literal) follows: its input moves to the row's depth
input register and index, its temporary to the first motion temporary (dead
once oC1 is written) and its output to **oC2**, so RT2 (R32F) receives
`z/w`, the rasterized device depth, wherever the draw covers a pixel. Every
transformer entry point takes `current_depth` (default true); false yields
the motion-only variant, byte-identical to the checkpoint-B1 output, for a
device without a third simultaneous target.

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
| RT2 | R32F current device depth z/w (rows with `depth_output`; -1 sentinel where nothing routed) |

Every table row uses exactly these constant ranges and oC1 (a `static_assert`
in `material_motion.cpp` checks the table against `MaterialMotionAbi`), so the
route uploads the same ranges for every pair.

## The table and the classes

`src/renderer/motion_output_profiles.h` defines `MotionOutputProfile` and
`MotionOutputClass` and includes the generated row list
`motion_output_profiles_inc.h` (169 rows: 56 class A, 101 class B, 12 class C —
every transformable SM3 pairing of the 6,752 technique passes in the archives;
32 distinct vertex and 108 distinct pixel programs). A row carries the original
fingerprints, lengths and version tokens, the matrix register and position
temporary, the four position DP4 offsets and lane masks, the five insertion
offsets, the four register choices, the light-loop bound flag and bound, the
current-depth registers (`vertex_depth_output_register`,
`depth_texcoord_index`, `pixel_depth_input_register`, 255 = none, and
`depth_output`; the inspector's `depth_plan` picks the first free register
other than the motion one per program and the smallest TEXCOORD index no
program of the row's VS/PS sharing component declares, so rows sharing a
program agree; all 169 archive rows have the output, the worst pixel program
reaching exactly the ten SM3 inputs), and
`observed_scene_draws` (capture metadata: draws in one session, zero for the
153 rows never observed; it orders the rows and has no runtime meaning). Rows
are derived numbers only; the header states that they are transformer input,
not an eligibility decision.

Two clip-row families exist: 107 rows read the clip rows from **c24** under
the relative point-light loop (`light_loop_bound_required`, bound 8) and 62
light-free `_0000`/`_0001` variants read them from **c0** with no relative
addressing at all (bound not required). The four position dots are issued in
XYZW order and are adjacent in 163 rows; six light-free asteroid, moon and
planet_haze rows interleave other work between the Z and W dots, so the row
carries each dot's own offset and the insert follows the last dot. Both share the same splice; only the
row's `matrix_register` and bound differ, and the route generalizes its shadow
and gate per row (see "Lookups and the route's per-row state" below).

| Class | Behavior |
| --- | --- |
| A, `ReferenceRegisters` | The reference registers are free in both programs; the emitted words are the Argon fragment with only the insertion offsets and position registers taken from the row. The Argon row produces the same 545/1392-word programs as the earlier hand-written transformer (FNV `07805a216f19b9b6` / `92bd1a32ae7d2a6d`, checked by the structural fixture). |
| B, `RelocatedRegisters` | Same code path; the row names the free VS output/TEXCOORD index and PS input/temporaries, and the authored fragment and the new declarations are relocated to them. |
| C, `RelocatedRegistersWithBranches` | Same splice and relocation as B; the pixel program additionally holds static `if b#`/`else`/`endif` blocks. The pixel-side validation below admits exactly that control flow (boolean constant conditions, balanced, nesting depth at most one, one `else` per block, depth zero at the append point so the appended fragment is unconditional) and refuses every other control-flow opcode; classes A and B refuse any branch, and a class C row refuses a program without one. The twelve `xt_*` programs hold two sequential blocks on `b0` and `b1` (six, the captured ones among them) or a single block on `b0` (six). |

Compile-time checks in the header prove that every row is well formed
(register ranges, ordered offsets, XYZW lane masks, DP4 offsets ascending by
at least one instruction with the insert one past the last, END at the
append point) and that rows sharing an original program agree on
that program's side of the splice, which the per-program variant scheme in the
live route depends on.

## Lookups and the route's per-row state

With 169 rows a linear scan per draw is no longer acceptable, so
`material_motion.cpp` builds three sorted index tables over the rows at
compile time (a constexpr stable insertion sort; a `static_assert` proves
each index a permutation of the rows, the pair order strict and the two
per-stage orders stable, so equal fingerprints keep table order): by
(vertex, pixel) fingerprint, by vertex fingerprint and by pixel fingerprint. `material_motion_profile` / `material_motion_pair_reviewed`
are one binary search each (O(log rows), no allocation), and
`material_motion_vertex_row` / `material_motion_pixel_row` return the first
supported row of a program (rows sharing a program agree on its side, so the
first serves). The live route records that row in its shader registry at
creation time (`ShaderEntry::row`), copies it into the shadow at
`SetVertexShader`, and gate 3 is the pair binary search on the two bound
fingerprints; a VS alias shared with an unreviewed PS still never routes.

The shadow no longer assumes c24: `motion_output.cpp` derives the distinct
clip-row windows the table names at compile time (c24–27 and c0–3 today, at
most `motion_matrix_windows_max`), captures every window from
`SetVertexShaderConstantF` and `GetVertexShaderConstantF` on resync, and gate
4 uses the bound VS row's window and its own `light_loop_bound_required` /
`light_loop_max_count` (`i0.x` in [0, 8] only where the program addresses
relatively). `rows_match_shadow` asserts that every row names a shadowed
window and that a bounded row's clip rows sit above the c0–23 block the bound
protects; `rows_use_public_abi` still requires c252 / c216 / oC1 in every row.

## Qualification at transform time

Each stage is matched to a row by exact full-program fingerprint, DWORD count
and version token (`UnsupportedShader` otherwise). The row's structural facts
are then revalidated against the actual words, and any inconsistency yields
`ProfileMismatch` with the output untouched:

- Instruction framing over the whole program, comments included as opaque
  instructions, with END exactly at the last word.
- VS: the depth output register and TEXCOORD index are reserved like the
  motion ones wherever the row names them (declared, written or read by the
  original refuses); a contiguous DEF/DCL header ending exactly at the declaration insert
  and declaring o0 as POSITION0 (so the dots below are the clip position); no
  DEF or DCL afterwards; the four position DP4s at the recorded offsets with
  the exact opcode, o0 lane mask, position temporary and `c<matrix + lane>`
  source; the arithmetic insert one past the last DP4; between the first DP4
  and the insert no instruction that writes the position temporary (any
  mask) and no block boundary (`rep`/`loop`/`if`/`ifc`/`else`/`endrep`/
  `endloop`/`endif`, so even a balanced block in the span refuses), which
  makes the spaced quads safe; no original declaration, write or read of the
  chosen output register, TEXCOORD index or c252–255 (every parameter token of every instruction is walked, address
  tokens skipped); relative addressing accepted only when the row records the
  draw-time light-loop bound (the light-free c0 rows have none; a relative
  operand under such a row refuses); block depth (`rep`/`loop`/`if`/`ifc`
  against `endrep`/`endloop`/`endif`; the c24 programs hold a `rep` light
  loop and an `if b#` block) zero at every position DP4, at the arithmetic
  insert and at END, so the added dots are unconditional, and no
  `call`/`callnz`/`ret`/`label`.
- PS: DEFs only before the definition insert, DCLs only between it and the
  declaration insert, executable code afterwards; the END token at the append
  point; no predicated or `texkill` instruction, no oDepth write and no
  relative addressing anywhere (every class is a program whose depth is the
  rasterized depth, which the previous-depth output relies on); no original
  reference to the chosen input register, TEXCOORD index, three temporaries,
  c216–220 or oC1, nor (for depth rows) to the depth input register, its
  TEXCOORD index or oC2, inside branch bodies included (the walk is linear over all
  instructions). Control flow: classes A and B refuse every control-flow
  opcode; class C admits only `if` (D3DSIO_IF, exactly one source that is a
  direct boolean constant register `b#`), `else` and `endif`, tracked with a
  depth counter that refuses an `if` at depth 1 (`max_branch_depth`), an
  `else`/`endif` at depth 0, a second `else` in one block, and a depth other
  than zero at END; `ifc` (if_comp), `rep`/`endrep`, `loop`/`endloop`,
  `break`/`breakc`/`breakp`, `call`/`callnz`/`ret`/`label` and `setp`
  refuse in every class, as does a malformed `if` token (length, predication,
  relative or non-boolean condition).
- The authored fragment itself is parsed and relocated only if it has the
  expected shape (three DEFs, one declaration, one color output, known
  straight-line opcodes).

The row-explicit entry points (`material_motion_*_variant_for`) take the row
as an argument so the structural fixture can hand in deliberately perturbed
rows, and perturbed programs under a row copy carrying their fingerprint, and
prove each of these refusals; through the table lookups they are unreachable,
since the fingerprint gate refuses first. The lookups match a program to its
first row of a supported class (all three classes today); rows sharing a
program agree on its side of the splice, so the first row serves.

Transformation is creation-time work with owned vector allocations, not work to
repeat per draw. The pair form builds both programs locally and publishes them
together only on success; the per-stage forms replace their output only on
success. Inputs may alias the output vectors. Unsupported input or allocation
failure leaves the previous output intact. There is no global cache, device
retention, native shader compilation or callback in this module.

The host structural fixture passes in optimized and ASan/UBSan builds for all
169 rows. It reconstructs both originals exactly after removing additions,
independently checks relocated operands and literals, proves the motion-only
form is the depth form minus the depth words (and, for the Argon row, the
earlier 545/1392-word programs), refuses 21 row
perturbations per row (26 with the five depth-register perturbations of a
depth row: o0 or TEXCOORD0 as the depth export, the depth output equal to the
motion output, v0 as the depth input, an output without an input) (including the class family swapped between B and C,
which the pixel words contradict; the previous-row constants placed on a
constant the VS actually reads; and, for light-free rows, a relative operand
injected under the row's denied bound) and 26 program perturbations per class
A/B row (declared or written reserved registers, missing POSITION0,
definitions after the header, refused opcodes, predication, relative
addressing, oDepth, malformed END, length overrun, an unterminated
vertex-side `if` before the position dots and a well-formed pixel static
branch) or 40 per class C row (additionally: a missing `endif`, `endif` or
`else` without `if`, a second `else`, a nested block, `ifc`, `rep`, `break`
and `breakp` opcodes, a float or relatively addressed condition, a two-operand
or predicated `if`, and a reserved temporary written or an ABI constant read
inside a branch body), plus five depth sites per depth row (the depth output
register or TEXCOORD index declared by the VS, the depth input register or
index declared by the PS, oC2 written by the PS); the six spaced-quad rows add two sites each (the
position temporary written between the dots, and a balanced `if b0`/`endif`
placed between them). A site an archive program does not offer is reported
as skipped, never fabricated: two rows share a pixel program with no literal
DEF, so 4,572 of the 4,574 program perturbation sites ran. The single-bit
sweep is exhaustive for the 16 captured rows (925,248 mutations) and a
deterministic evenly strided 2,048 positions per program for the other 153
(626,688), 1,551,936 mutations in all without changing output, and six
successful alias layouts per row (1,014). These are qualification and
memory/structure checks; shader execution is verified separately. See
`verification/results/material-motion-structure-summary.json`.

## Caller requirements still outside this module

The caller must establish opaque scene coverage and compatible MRT dimensions,
formats, write masks and state. It must not replace an application-owned RT1.
The initial route requires alpha testing, blending and sRGB writes disabled,
SM3 support and at least 256 VS float constants.
Every c24 row VS uses relative light constants, so the actual light count must
be bounded to 0–8 before reserving high constants (`light_loop_bound_required`);
the light-free c0 rows need known rows in their window but no bound. A shader
fingerprint does not validate the runtime count.

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
