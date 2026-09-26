# Class C, ownership-wrapper coverage and readback analyzer review

Independent review of three concurrent uncommitted change sets on top of
`ce5a99b`: (1) class C (`RelocatedRegistersWithBranches`) support in the
table-driven transformer, the 16-row generated table, the inspector's
`static_branches` digest and the structural and GPU fixtures; (2) ownership-
wrapper coverage of the live route (`MotionOutput` retaining only the level-0
motion surface, the `ownership_copy_depth` per-frame witness, the sixteen-run
environment matrix in `run_motion_output.py`, `manage.py launch --dry-run`);
(3) the offline readback analyzer `tools/analysis/analyze_motion_readback.py`
with its unit fixtures and note. The three sets were never verified together
before this review; every suite below ran on the final tree after fresh
`build/` and `build-ownership/` rebuilds. Result files under
`verification/results/` and the derived profile JSON were queried, not read.

## Class C control flow

`pixel_structure` admits a branch only for a class C row and only as the exact
tokens: `if` must be `0x01000028` (opcode, length 1, no predication or
co-issue bits), `else` `0x2a`, `endif` `0x2b`; the `if` condition must carry
the parameter bit, no relative bit and register class 14 (`b#`), so `ifc`
(0x29), a float or predicate condition, a relatively addressed or two-operand
`if` and a predicated `if` all refuse. Depth is a counter bounded by
`max_branch_depth = 1` with one `else_seen` flag per level: an `if` at depth 1,
an `else`/`endif` at depth 0 and a second `else` in one block refuse, and the
function requires depth 0 at END (END is the append point, so the appended
fragment is unconditional) and at least one block for class C. Classes A and B
reach `!branches_allowed` first, so any branch refuses them, and
`refused_flow_opcode` still covers call/callnz/loop/ret/endloop/label,
rep/endrep, break/breakc/breakp and setp in every class. The walk is linear,
so the reserved-register, `oDepth` and `texkill` checks cover branch bodies
exactly as straight-line code; the structural fixture proves both in-branch
cases. The four captured programs hold two sequential blocks on `b0`/`b1`
(`static_branches.sites` in the JSON), and the inspector classifies C only
when `only_boolean_if` holds and the depth is at most 1. Coverage: A 35.02%,
B 21.90%, C 40.72% of 11,493 scene draws = 97.6%, as the notes state.

## Findings and fixes

1. **Low, fixed** – the vertex-side revalidation tracked no block depth
   (`src/renderer/material_motion.cpp`, `vertex_structure`). Every table VS
   holds a `rep` light loop and an `if b0` block (`control_flow_counts` for all
   seven), and the previous-clip DP4s are spliced at the row's arithmetic
   insert on trust that it lies outside them; a regenerated row with an insert
   inside the loop or the block would have made the added dots conditional or
   per iteration, and neither the inspector nor the transformer checked it.
   The walk now counts `rep`/`loop`/`if`/`ifc` against
   `endrep`/`endloop`/`endif`, refuses `else`/closers at depth 0 and
   `call`/`callnz`/`ret`/`label` (subroutines make "depth 0" meaningless), and
   requires depth 0 at every position dot, at the arithmetic insert and at
   END. The structural fixture adds one vertex perturbation per row (the last
   executable instruction before the dots becomes an unterminated `if b0`),
   so program perturbations are 26 per class A/B row and 40 per class C row;
   the runner and both notes were updated. No emitted word changes.
2. **Low, fixed** – the analyzer's history key (`KEY_FIELDS` in
   `tools/analysis/analyze_motion_readback.py`) omitted three logged parts of
   the DLL's `RigidDrawKey`: the position program (`vs`) and the declaration's
   `position_offset`/`position_type`. Two draws differing only there are two
   keys in the DLL but were one in the reconstruction, so `history_pairing`
   would have reported false disagreements on such captures. The key now
   carries all 25 logged fields; the unit fixture's line builder and the
   note's "keyed by" sentence follow.
3. **Documentation, fixed** – `docs/verification/motion-output.md` carried
   the pre-table numbers for the material-motion suites ("8 groups, 57,152
   mutations"); replaced with the 16-row numbers. The readback note and
   `verification/results/motion-readback-fixture-*` were regenerated from the
   seam-on run of the final tree (log `5424c7da…e9632`, same numbers as
   before).

Verified correct and left unchanged: the level-0-only motion target
(`ensure_target` releases the texture after `GetSurfaceLevel`; on D3D9 a level
shares its container's count and through the wrapper `Texture::GetSurfaceLevel`
adopts the surface as a device child whose `Release` forwards to the native
surface, so the native texture stays alive and one owned object is one logical
device reference; `device_references()` counts `target_surface_`; the fill,
`SetRenderTarget(1, …)`, `readback` and `fixture_readback` all use the
retained surface as the `GetRenderTargetData` source; `before_reset` releases
it before the wrapper's Reset and `release_resources` before the final
Release, with no later use). The environment matrix: `plain`, `ownership`,
`depth` and `admission` set exactly the documented `X3M_*` variables over a
base that zeroes every other switch; `validate_ownership` requires the wrapped
factory line, the mode line with the depth flags, `create_after`/`reset_after`
witnesses, the per-frame `present` witness only when wrapped and enabled
(`source_epoch = 2·(frame+1)` in depth mode), `scene_depth_frame` only in
depth mode and `verify_admission` plus `admission_metric` only in admission
mode; the color identity compares the 12 per-frame hashes of every run against
the plain route-off run of the same DLL. The analyzer streams the log line by
line and reads readbacks row by row; its half-texel convention
(`uv = ndc·(0.5,−0.5) + 0.5 + 0.5/size − jitter`, integer raster samples)
matches `rigid_motion_ps.hlsl` and `src/temporal/README.md`, and the row-pair
mapping is homogeneous (`M_cur·inv(M_prev)` applied to previous NDC with the
readback depth and divided by the resulting w), inverted by partial-pivot
Gauss-Jordan in double with a relative singularity cutoff; its defaults equal
the note's acceptance criteria. Row counts (16), the 97.6% share, the four
environments and the launcher command agree across README, architecture,
profile and verification notes; every line number cited in the wrapper
analysis matched the tree.

Design-level observations, not changed: the inspector still derives the VS
insert offset without a depth check (the transformer now refuses at run time;
adding it to the tool would change `tool_sha256` in the JSON, so leave it for
the next regeneration). The `if` condition's source-modifier bits (`!b#`) are
not constrained; both forms are safe. The analyzer skips draws whose previous
rows are singular and needs consecutive captured frames for the static and
row-pair checks, as its note says. `launch --dry-run` validates the
installation by design, so it fails without an installed proxy.

## Fixture assessment

`run_material_motion_structure.py` proves per row, in optimized and
ASan/UBSan builds: 21 row perturbations (including the class family swapped
between B and C), 26 or 40 program perturbations, every single-bit mutation,
six alias layouts and the Argon byte identity. `run_material_motion.py`
proves color bit-identity per row on real programs; class C rows run the
three configurations under all four `b0`/`b1` settings per format after a
control that the booleans change the original image (8 controls). Not
exercised per row: light counts above zero and material-constant controls
(Argon inventory only), the game's actual constant values, and object history
through the wrapper (the synthetic observers stay inactive).

## Results after the fixes

`build/` (RelWithDebInfo, `--clean-first`, by `run_motion_output.py`) and
`build-ownership/` (Release, `--clean-first`, by
`run_ownership_integration.py`) were rebuilt on the final tree.

| Suite | Result |
| --- | --- |
| `run_material_motion_structure.py` | PASS: 16 rows transformed, 161 check groups, 925,248 mutations, 96 aliases, 26/40 program perturbations per A-B/C row, release and ASan/UBSan |
| `run_material_motion.py` | PASS: Argon 1,182 checks / 2,952 samples / 101,318,656 color components / 164 bilateral cases / 82 configurations; rows 16 of 16, 168 configurations, 2,376 checks, 6,048 samples, 2,064,384 color components, 8 boolean controls, minimum 912 covered pixels, max 0.00086 px UV, 8.7e-7 depth, replay difference 0 |
| `run_motion_output.py` | PASS: 16 runs; production off/on 6/6 and seam off/on 6/30 checks in plain, ownership, depth and admission; seam-on 44,284 motion / 10,261 matched pixels in every environment; color hashes identical off vs on and across environments; depth-mode `source_epoch` 18 at frame 8 |
| `run_ownership_integration.py` | PASS: 26 cases, all exit 0 |
| `run_ownership_integration_fallback.py` | PASS: 23 production objects, fault `wrap_factory E_OUTOFMEMORY`, binaries unchanged |
| `check_no_x87.py` on `build/d3d9.dll` | PASS: 6 light hooks, 125 reachable functions, 0 x87 opcodes |
| `python3 -m unittest discover -s verification/analysis` | 407 tests OK |
| `manage.py launch --dry-run --direct --ownership --object-trace --object-lifetime --motion-output --telemetry --capture-start 999999 --capture-frames 4` | prints the CrossOver command and the documented `X3M_*` environment; no game process |
| `analyze_motion_readback.py` on the final seam-on run | PASS: 8 frames, 6,082 valid pixels, 3,402/3,402 explained at 0.0018 px max, 12 predictable draws, temporal 0.9–1.0 where the criterion applies |

The Argon inventory and per-row GPU numbers are unchanged from the pre-review
runs: the fixes add refusals that no table program triggers and correct an
analyzer key; they alter no emitted word and no route behaviour.
