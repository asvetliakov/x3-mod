# Table-driven material motion transformer review

Independent review of the uncommitted table-driven transformer for
transformation classes A and B: `src/renderer/motion_output_profiles.h` (new),
`src/renderer/material_motion.{h,cpp}`, the registry and gate 3 changes in
`src/proxy/motion_output.cpp`, the structural and GPU fixtures with their
runners, and the architecture, profile and verification notes. Scope was
transform-time revalidation completeness, class B relocation, the runtime
registry/gate changes, fixture strength and documentation accuracy. Result
files under `verification/results/` and the derived profile JSON were queried,
not read.

## Revalidation and relocation

The token walking is sound. `walk` takes every instruction length from the
token's own length field (comment length from bits 16–30, otherwise bits
24–27), refuses an instruction that would overrun the program, and accepts END
only as the exact token `0x0000ffff` at the last word; an END token earlier or
a trailing word after it refuses. Comments are opaque instructions on both
sides of every boundary, so a comment at an insert offset still marks the
boundary. DEF/DEFI/DEFB payloads are never walked as parameters: only their
destination token is checked for a reserved register. Executable instructions
walk every parameter token (destination, predicate and sources) and skip the
address token after a relative operand, after checking it carries the parameter
bit. The four position DP4 tokens are compared exactly (opcode and length,
`o0` with the lane mask, the row's temporary with `.xyzw`, `c<matrix + lane>`),
so a predicated, saturated or partial-precision dot refuses. New instructions
are built from constants without modifier bits; the VS declaration is
`TEXCOORD<i>` without centroid and the PS declaration has no `_pp` or centroid.

Relocation of the authored fragment never touches DEF literal bits (the four
literal words are copied), changes only the 11 index bits of a parameter token
(swizzle, mask, modifiers, shift and the relative bit are preserved; a relative
operand refuses), and maps `c0–4 → c<base>..c<base+4>`, `v0 → v<input>`,
`r0–2 → r<temporary>..`, `oC0 → oC<output>`. The fragment reads `c0` and
`c1.x`, which the route uploads as `c216`/`c217`; `rows_use_public_abi`
proves every row's constant bases and `oC1` equal `MaterialMotionAbi`, so the
relocated code reads what the runtime writes.

`motion_output_profiles_consistent` is a genuine compile-time proof: rows
sharing a VS must agree on every VS-side field (length, version, matrix
register, temporary, DP4 offsets and masks, both inserts, output register,
TEXCOORD index, constant base and the light-loop fields), rows sharing a PS on
every PS-side field, and duplicate pairs are refused. Gate 3 keys on the exact
pair through `material_motion_pair_reviewed`, which also refuses deferred
classes; the registry and per-draw scans are bounded by the 12-row table and
allocate nothing per draw.

## Findings and fixes

Severity ordering: defects fixed first, then design-level items left as is.

1. **Medium, fixed** – the pixel-side revalidation accepted `texkill` and
   writes to `oDepth` (`src/renderer/material_motion.cpp`,
   `pixel_structure`). An oDepth write makes the rasterized depth differ from
   the clip Z/W the fragment reports as previous depth, which the temporal
   consumer's depth comparison relies on, and `texkill` is outside the
   straight-line definition of classes A and B. Both now refuse
   (`ProfileMismatch`). No class A or B program contains either (the derived
   JSON's `texkill_dwords` and `depth_output_dwords` are empty for all
   twelve), so coverage is unchanged.
2. **Medium, fixed** – the vertex-side revalidation never checked that `o0`
   is the POSITION0 output, so the "position" DP4 quad was position only by
   trust in the row. `vertex_structure` now requires a header declaration of
   `o0` with usage POSITION, index 0 (every table VS has one at its first
   output declaration).
3. **Low, fixed** – `motion_output.cpp` hard-codes the shadowed clip-row
   window `c24–27` and gate 4's `i0.x` bound of 8, while the table carries
   `matrix_register` and `light_loop_*` per row; a regenerated table with
   another matrix register would have routed draws with rows the shadow never
   captured. The three sites now use named constants and a `static_assert`
   (`rows_match_shadow`) requires every row to agree with them.
4. **Low, fixed** – the per-stage lookups (`vertex_row`/`pixel_row`) took the
   first row naming the program regardless of class, so a program that a
   future deferred-class (C) row also names would have been refused for its
   A/B pairs. They now take the first row of a supported class.
5. **Fixture, fixed** – the structural fixture only reached the revalidation
   through perturbed *rows*; the program-side checks were never exercised
   from the words. It now runs 24 program perturbations per row under a row
   copy carrying the perturbed program's fingerprint (missing POSITION0,
   declared or written reserved output/TEXCOORD/input/temporary/constant/
   color registers, a definition after the header or between the pixel
   inserts, predication, relative addressing, `texkill`/`if`/`breakp`/`def`
   opcodes in the executable region, an oDepth write, malformed END and an
   instruction length overrunning END), each refused atomically while the
   untouched stage still transforms and the table lookup reports
   `UnsupportedShader`. The GPU runner additionally requires at least 512 of
   the 1,024 pixels of every per-row comparison to be covered original
   geometry (912 observed) instead of merely nonzero.
6. **Documentation, fixed** – the prototype note's qualification list and
   fixture paragraph, the profile note's class description and light-loop
   runtime check, the live-route note's pair keying section and the
   verification note's structural counts and review pointer.

Verified correct and left unchanged: the fingerprint gate before every
structural check (so the row-explicit forms are unreachable through the
lookups without an exact original), `motion_output_profile_valid` re-run at
transform time on the row actually used, the reconstruction of both originals
after removing exactly the 3 + 16 vertex and 18 + 3 + 111 pixel added words,
the bitwise operand comparison in the fixture's `authored` check, the
single-bit sweep over every DWORD of both stages per row (622,336
mutations), and the six alias layouts per row.

Design-level observations, not changed: the transformer trusts the row for
which registers are *free* only after proving the original never declares,
reads or writes them, but it cannot know whether the game binds an
interpolator through a different pixel program at draw time; that is exactly
why eligibility is keyed by the pair, and a VS variant is only ever drawn with
a PS variant from the same row. Class C needs balance and depth checks that
`control_flow_opcode` currently refuses wholesale; the reserved enumerator and
`UnsupportedShader` path mark the extension point. The structural fixture's
row perturbations compare against reference output built by the same
transformer, so they prove refusal and atomicity, not independent
correctness; the reconstruction and Argon byte-identity checks carry that.

## Fixture assessment

`run_material_motion_structure.py` proves, per row and in optimized and
ASan/UBSan builds: the pair, per-stage and row-explicit forms produce the same
programs; the originals are reconstructed exactly after removing the
additions; the relocated operands and literals match the authored fragment
bit-for-bit outside the index field; 21 row perturbations and 24 program
perturbations are refused without touching the output; every single-bit input
mutation is refused; six alias layouts succeed; the Argon row reproduces the
hand-written transformer's 545/1392-word output hashes.

`run_material_motion.py` proves color bit-identity for every row on real
game programs: six configurations per row (RGBA32F and A8R8G8B8 color with
RGBA32F motion; stationary, perspective + translation + jitter, and unknown
history), each with a full-image color comparison (4,096 components, zero
mismatches, at least 912 of 1,024 pixels covered), the independent two-pass
replay reference (maximum difference zero), nine analytic samples, two
bilateral D24 EQUAL controls and the changed-depth negative control. Not
exercised per row: light counts above zero and material-constant controls
(Argon inventory only), and the game's actual constant values.

## Results after the fixes

Both trees were rebuilt with the documented commands (`build/` RelWithDebInfo
by `run_motion_output.py` with `--clean-first`, `build-ownership/` Release by
`run_ownership_integration.py`).

| Suite | Result |
| --- | --- |
| `run_material_motion_structure.py` | PASS: 12 rows transformed, 121 check groups, 622,336 mutations, 72 aliases, 24 program perturbations per row, release and ASan/UBSan |
| `run_material_motion.py` | PASS: Argon 1,182 checks / 2,952 samples / 101,318,656 color components / 164 bilateral cases / 82 configurations; rows 12 of 12, 72 configurations, 1,020 checks, 2,592 samples, 884,736 color components, minimum 912 covered pixels, max 0.00086 px UV, 8.7e-7 depth, replay difference 0 |
| `run_motion_output.py` | PASS: production off/on 6/6 checks, seam off/on 6/30, seam-on 44,284 motion pixels, color hashes identical off vs on for both DLLs |
| `run_ownership_integration.py` | PASS: 26 cases, all exit 0, `build-ownership/` rebuilt with `--clean-first` |
| `run_ownership_integration_fallback.py` | PASS: 23 production objects, fault `wrap_factory E_OUTOFMEMORY`, binaries unchanged before and after |
| `check_no_x87.py` on `build/d3d9.dll` | PASS: 6 light hooks, 125 reachable functions, 0 x87 opcodes |
| `python3 -m unittest discover -s verification/analysis` | 388 tests OK |

The Argon inventory numbers and the per-row GPU numbers are unchanged from the
pre-review run, as expected: the fixes tighten refusals that none of the twelve
originals trigger and add checks; they alter no emitted word.
