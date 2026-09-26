# Archive-wide profile table and structural scene selector review

Independent review of two concurrent uncommitted change sets on top of
`113881b`: (1) the archive-wide motion profile table (`effect_passes.py`, the
inspector's archive mode with spaced position quads and SM2 feasibility, the
169-row generated table, sorted index tables with binary search and the
spaced-quad span validation in the transformer, one profile-row pointer per
shader entry and multiple clip-row windows in the route's shadow) and (2) the
structural scene selector (`scene_boundary.h`, the replay tool, the gameplay
fixtures and the changed `run_motion_output.py` expectations). Every suite
below ran on the final tree after fresh `build/` and `build-ownership/`
rebuilds. Result files, the 2.6 MB profile JSON and the gameplay fixtures were
queried with scripts, never read whole.

## Shadow windows and lookups (change set 1)

`motion_output.cpp` derives the distinct clip-row windows from the table at
compile time (`derive_matrix_windows`: c24 for the 107 point-light rows, c0
for the 62 light-free rows; more than `motion_matrix_windows_max` = 4 fails the
`static_assert`). `set_vertex_constants_f` intersects each upload with each
window independently (`[lo, hi)` clipped to the window, copy offsets relative
to the window base and to the upload start, `rows_known[w]` set only by a
write covering the whole window, partial updates keeping prior knowledge), so
one upload spanning c0–27 refreshes both windows and count clamping cannot
mark a window known from a partial write. `resync_shadow` re-reads every
window after a state block or Reset. `before_draw` reads the row of the VS
actually bound (`shadow_.vs_row`, copied from the registry at
`SetVertexShader` and refreshed when a bound VS is re-registered); gate 3
requires it together with the pair search, and gate 4 reads
`rows_known[window_of(row.matrix_register)]` and bounds `i0.x` only when
`light_loop_bound_required`. That flag is trustworthy: `vertex_structure`
refuses relative addressing whenever the row denies the bound, the JSON shows
no row whose `vs_relative_light_loop` disagrees with the plan, every bounded
row sits at c24 and every unbounded row at c0, and `rows_match_shadow` walks
all 169 rows requiring a shadowed window and, for bounded rows, clip rows
above the c0–23 light block. Registration does two binary searches and one
vector allocation per created program; the per-draw path allocates nothing.
`lower_bound` uses strict less-than, `pair_row` confirms the exact key and
the per-stage lookups stop at the first different fingerprint, so absent keys
below, between and above the table return null.

## Spaced position quads

`vertex_structure` applies the span rule to every executable instruction
with `dp4[0] < at < arithmetic insert`: any block opener, closer or `else`
refuses, and any destination `r<position_temporary>` refuses whatever its
mask (a predicated SM3 instruction still carries its destination at `at+1`,
co-issue is a ps_1_x notion, and `dcl`/`def` in the code section already
refuse). Depth must be zero at each of the four dots, which are validated
individually (exact `dp4` token, single `o0` lane, `r<temp>`, `c<matrix+lane>`),
and the insert is `dp4[3] + 4` by the header's constexpr check with
`arithmetic_boundary` requiring depth zero there. Class C rules are pixel-side
and untouched. The inspector mirrors both rules
(`position_temporary_rewritten_inside_quad` by register type and number,
`position_flow_inside_quad` over `FLOW_OPCODES`), and the structural fixture
adds two perturbations to each of the six spaced rows (temporary written
between the dots with `.x`; a balanced `if b0`/`endif` between them).

## Parser and selector

`effect_passes.py` walks the D3DX container in memory and emits catalogue,
virtual path, effect SHA-256, technique/pass names and indices, and per stage
the FNV-1a 64, DWORD count and status; the program bytes are hashed from the
in-memory slice and never stored. A scan of the profile JSON found no integer
list beyond the offset arrays, no hexadecimal string other than 16- and
64-character hashes and no floats other than shares; `literal_definitions`
carry register and offset only. The replay fixtures hold surfaces
(identity, container, size, format, MSAA) and events (kinds, flags, results,
shader/texture hashes, viewport) only.

The selector rejects a menu frame at its first event: `AwaitInitialClear`
needs a full color+depth Clear with a same-size D24 (77) depth surface (the
four iteration-05 menu frames fail at sequence 1 on a color-only Clear).
Background admits only draws on the latched pair with a full viewport and
plausible z state, or a null-PS draw, and ends at the first full depth-only
Clear; any other event rejects. Scene admits the same draws and only a
`SetDepth(null)` after a depth writer, so a second depth-only Clear rejects
(unit test). `depth_writer` needs `draw_state_known`, which both adapters fold
to `false` for a null PS (`scene_capture.cpp:174`, `motion_output.cpp:830`),
plus `z_enable == z_write == 1`. Up to Scene the adapters compute the same
event; the route also requires known RT/depth/viewport, which `full`/`same`
need anyway, and it never fills draw events beyond Scene (`texture0 = 0`), so
its instance stops at `AwaitCopy` while `in_scene` needs only Scene, as
documented. `SceneSignatures::background` stays as ignored slots; the capture
fixture, the replay harness's `P` line and `MotionOutput::signatures()` still
fill them and compile.

## Findings and fixes

1. **Low, fixed** – `row_orders_valid` (`src/renderer/material_motion.cpp`)
   proved only `rows_by_pair` a permutation; the two per-stage indices were
   checked for range and non-decreasing keys, so a duplicated index or an
   unstable order (breaking "first row in table order") would have passed.
   Each index is now proven a permutation, the pair order strict and the
   per-stage orders stable; `material-motion-prototype.md` says so.
2. **Low, fixed** – the structural fixture exercised the lookups only for rows
   with local programs and only with exact keys and zero; no boundary or
   absent-pair case. `test_table_lookups` in `material_motion_structure.cpp`
   now checks, for all 169 rows without local programs, the pair, vertex-row
   and pixel-row searches against a linear scan (agreeing sides, first row in
   table order, wrong DWORD count refused, the sixteen `±1`/`^1` neighbours of
   every pair: 2,535 absent pairs), the lexicographically first and last pairs,
   the per-stage extremes and keys just outside them, and zero and all-ones
   keys; the runner consumes the `TABLE lookups=PASS` line and records the
   count.
3. **Low, fixed** – `tools/analysis/replay_scene_boundary.py` crashed after a
   complete replay when the fixture path lay outside the repository
   (`Path.relative_to(ROOT)` in `sources_sha256`), losing the report of an
   out-of-tree derivation. The label now falls back to the absolute path; the
   three tracked reports were regenerated (identical decisions, tool hash
   updated).
4. **Documentation, fixed** – `docs/architecture/live-motion-route.md`
   described an intermediate 163-row table (52 A / 99 B, 26 vertex and 105
   pixel programs, 17 unrowed pairs including the six spaced-quad variants)
   and, in "Implementation", the single c24 window, "no PS appears in two
   rows" and "163-row"; `docs/verification/motion-output.md` carried the
   16-row suite numbers; `analyze_iteration06_motion.py`'s
   `selector_explanation` and `BACKGROUND_PAIRS` are marked as the
   pre-correction rules of the captured build; `material-motion.md` records
   the lookup oracle and this run's UV maximum.

Verified correct and left unchanged: the window derivation, upload clipping
and resync above; the gate-3/gate-4 use of the VS row; the span rule; the
selector transitions; the fixture and JSON contents; the runner's production
expectation (the structural rule lets the production DLL enter the synthetic
scene phase, where every scene draw routes sentinel-only at gate 5, never
matched, color hashes unchanged).

Observations, not changed: a PS bound but absent from the registry hashes to
0 and is tolerated like a null PS in the phase rule (never a depth writer);
registration precedes binding in practice. Any frame with a color+depth
Clear, a draw and a depth-only Clear now enters Scene, so variants substitute
there in sentinel-only mode; publishing history still needs the full bloom
chain. `walk` never visits END, so a row whose arithmetic insert is the END
index would refuse (none does). The GPU runner reported 0.0010 px maximum
analytic UV error for the Argon inventory this run against 0.00086 recorded
earlier, within its bound.

## Results after the fixes

`build/` (RelWithDebInfo, `--clean-first`, again by `run_motion_output.py`)
and `build-ownership/` (Release, `--clean-first`, by
`run_ownership_integration.py`) were rebuilt on the final tree.

| Suite | Result |
| --- | --- |
| `run_material_motion_structure.py` | PASS: 169 rows transformed, 0 skipped, 1,691 check groups, 1,551,936 mutations (16 full-sweep rows, 153 sampled), 4,572 of 4,574 program perturbation sites, 1,014 aliases, lookup oracle 2,535 absent pairs, release and ASan/UBSan |
| `run_material_motion.py` | PASS: Argon 1,182 checks / 2,952 samples / 101,318,656 color components / 164 bilateral cases / 82 configurations; rows 169 of 169, 1,230 configurations, 17,413 checks, 44,280 samples, 15,114,240 color components, 2,460 bilateral cases; max 0.0010 px UV, 8.7e-7 depth, replay difference 0 |
| `run_motion_output.py` | PASS: 16 runs; production off/on 6/6 and seam off/on 6/30 checks in plain, ownership, depth and admission; seam-on 44,284 motion / 10,261 matched pixels in every environment; color hashes identical off vs on and across environments; depth-mode `source_epoch` 18 at frame 8 |
| `run_ownership_integration.py` | PASS: 26 cases, all exit 0 |
| `run_ownership_integration_fallback.py` | PASS: 23 production objects, fault `wrap_factory E_OUTOFMEMORY`, binaries unchanged |
| `run_scene_capture.py` | PASS: 36 scenarios, 4,908 checks, 16 samples, fresh build |
| `check_no_x87.py` on `build/d3d9.dll` | PASS: 6 roots, 125 reachable functions, 0 violations |
| `python3 -m unittest discover -s verification/analysis` | 436 tests OK |
| `replay_scene_boundary.py --trace` on `/tmp/x3-iteration05-completed-snapshot.log` and `/tmp/x3-iteration06-snapshot.log` (baseline `113881b` header) | derived fixtures byte-identical to the tracked ones; iteration 05: 28 frames, 24 selected / 4 rejected, 0 changed against the baseline; iteration 06: 68 selected / 0 rejected, 32 changed (the baseline rejected 32); station fixture 12/12 |
