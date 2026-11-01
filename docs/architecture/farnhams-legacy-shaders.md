# Farnham's Legacy shader tables

Rule (user, 2026-11-01): shader tables come from offline archive analysis, never from
log rows (no flight visits every sector, ship or station); logs only cross-check.

## Mount order

`tools/analysis/catalogue_mounts.py` is the one mount model of the archive scans
(`effect_passes`, `index_shaders`, `sweep_shaders`, `shader_coverage_offline`).
Each numbered series counts from `01` and ends at its first gap; later catalogues
win. X3AP.exe: root `NN.cat`, then `addon/NN.cat`. X3FL.exe: root, then
`addon2/NN.cat`, **no physical `addon` stage**
([resolver](../reverse-engineering/fl/resource-resolver.md) section 1). The edition is
FL when `X3FL.exe` and `addon2/01.cat` exist. Mounting `addon` too would change no
FL shader (its 1,176 shader paths all recur in `addon2`). Test: `test_catalogue_mounts`.

## Inventory (`verification/results/fl-shader-coverage/`)

FL (`/mnt/c/X3FL`) vs stock AP (`/mnt/c/X3AP-orig`), effective sets: 755 / 751
programs, **721 shared, 34 FL-only, 30 AP-only**. All of the FL-only and AP-only
programs are ps_3_0. All 256 vertex programs are shared. The 34 split into 22
conventional and 12 XT, with 1,370–1,911 DWORDs. All 57 FL dumps are inventory
members, and every logged program in runs 3 and 4 is too (18 and 20 FL-only).

## Motion rows

`inspect_motion_output_profiles.py --base-profiles` writes the FL pairs that the AP
table lacks to `src/renderer/motion_output_profiles_fl_inc.h`. That file is included
after `motion_output_profiles_inc.h` into the same `motion_output_profiles[]`. The
generator plans depth over both includes and refuses to emit if an AP row would
change; the compile-time sharing assert covers both. There are 34 rows: 6 A, 16 B
and 12 C, all on four AP vertex programs. None needed a size or class change; every
row writes depth. Both editions compile 205 rows; in AP the 34 are inert (AP never
creates their pixel programs).

The rows let these hull pairs past gate 3 (exact pair) to the same-draw motion
variant (oC1), the current-depth output (RT2) and the sun-lane writer sibling.

## Material rows (part 4b)

`inspect_fl_linear_materials.py` derives five FL-only includes from the program
bytes and constant tables. Each sits after the AP rows of its table; the AP tables
and includes are untouched, and an FL hash already in an AP table is refused:
`linear_material_pixels_fl_inc.h` (22 conventional `Pixel` rows), `_pairs_fl_inc.h`
(their 22 pairs), `linear_xt_profiles_fl_inc.h` (12 XT rows), `material_exposure_
profiles_fl_inc.h` (34 diffuse joins) and `linear_emission_hull_fl_inc.h` (12 HDR
hull rows). The FL register layout is row data, not code: `Pixel` gains `glow`
(c4), `affine_first` (c1), `temporary_base` (5/6/7, the 4a motion row's) and
`original_only`; FL XT rows carry `light0/light1` (c7/c9), `palette_first` (c16/c19)
and their temporary base (6; 7 for five of the six terraformer rows). AP rows
keep the old defaults. The input guard of the original-shading variants and the
strength twin is 1,911 DWORDs, the largest reviewed original, statically checked
against every row. The emitted program is bounded by the final structure walk
against the device slot budget. The largest FL variant is 5,000 DWORDs and 883 slots.

Switched on for the 34 FL programs under `fl`: original fill, hull light-map gain
(static and far-fade dynamic), emissive widening, sun-share producer and its gained
and widened forms, engine-light twins and LOD-strength twins. The 12
`standard_lighting`/`XT_standard_lighting` FL programs also get the HDR additive hull
gain; the same effect-source rule reproduces the 12 AP rows. The hull program mask
grows from 12 to 24 bits. Proof is in the ledger entry "FL shader coverage 4b".

Still off or AP-only: the converted linear-material path (`original_only`; launcher
option retired), the station distance fade, the two runtime-qualified cutout pairs
(they apply only with converted materials) and FL pixel-coverage rows for the capture
reader (they need an FL sweep `features` inventory and a 1,911-DWORD bound).
`material_radiance` has no production consumer.
