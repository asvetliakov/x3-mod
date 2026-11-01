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

What the rows enable under FL: these hull pairs get past gate 3 (the exact pair
check). They get the same-draw motion variant (oC1), the current-depth output
(RT2) and the sun-lane writer sibling. They then take the remaining draw gates like
any AP pair. What stays off: the linear-material variants built on the motion
variant (original fill, hull light-map gain and widening, sun share). They return
`UnsupportedShader` for every FL pixel program because no linear row exists. The
HDR additive hull gain, exposure/strength twins and distance fade also stay off.

## Part 4b

Read the register layout from each program's constant table (FL: glow c4, hue
c1–c3, XT c7/c9, c12) in `linear_material.cpp` and the XT profiles. Add the gain/XT,
HDR hull gain (`linear_emission.cpp`) and exposure/strength rows. Raise the 1,392
and 1,791 input guards to 1,911 with proof, then run the native validator on every
variant. The motion variants need none of this; the largest is 2,065 DWORDs.
