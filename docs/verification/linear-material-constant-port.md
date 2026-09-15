# Linear-material PS3 constant-read portability

Verification note, 2026-09-15. The inherited converted-material sanitizer emitted
`max r#, cApplication, c212.y`. Pixel shader 3.0 permits only one distinct float
constant register as an instruction source. CrossOver accepted the programs, but
that does not establish the documented native Direct3D contract.

The repair stages an application constant through a full-precision temporary
`mov`, copying the source token exactly, then runs the existing ordered `max`
against `c212.y`. Every admitted source in the qualified 108-program corpus is a
direct, identity-swizzle, unmodified constant. Pixel relative addressing is
already refused by validation, so this checkpoint does not claim modifier,
swizzle or relative-address coverage that the corpus does not exercise.
Temporary-input sanitizers and all vertex programs remain byte-identical. This
adds one pixel-shader ALU instruction per affected sanitizer, up to seven per
generated pixel variant, with no allocation, upload, state, lock or per-draw
host work. The added GPU instruction cost is not timed here.

`verification/analysis/test_linear_material_constant_port.py` compares every
emitted legacy, fill and distance-fade pixel variant against retained commit
`8722072`. It requires at most one distinct float constant source per instruction,
and proves that the only bytecode difference is the exact `mov` staging plus the
corresponding constant-to-temporary operand substitution. It also checks the
ordered sanitizer numerically at finite values, signed zero, infinities and NaN.
The host corpus covers 904 legacy, 216 fill, 10 fade and 432 sun-share pixel
variants from all 108 converted pixel programs. It requires the sun-share
variants to differ from committed `0234178` only by the same staging sequence.
The legacy/fill/fade/sun-share groups stage 1776 / 444 / 16 / 888 sanitizer
reads respectively. Maximum weighted PS slots move 298 → 305
(legacy), 299 → 306 (fill), 214 → 216 (fade) and 342 → 349 (sun-share), all below the 512-slot SM3
floor. The hull-only maxima used by the fill note move 178/190 → 180/192
without fill and 179/191 → 181/193 with fill.

## GPU parity and frozen-output migration

Main rebuilt both detached fixtures from the final production inputs and ran
legacy, fill and fade sequentially through `wine_lock.py` with
`X3M_FIXTURE_BOTTLE=X3`. All passed: legacy 4,177 cases / 37,593 samples,
fill 23 / 207, fade 78 cases. Against retained pre-repair reports, 1,193 legacy
creates and 47,425 stable rows, 88 fill creates and 701 stable rows, and 36 fade
creates and 189 stable rows match exactly. All 1,162 fade RGBA32F files have
identical names and bytes. The first fade comparison exposed two unfiltered
`scan_us_max/mean` timing fields; filtering only those fields completed parity
without rerunning the GPU fixture or weakening numeric/state comparisons.

The [compact record](../../verification/results/linear-material-constant-port.json)
binds source, raw reports, summaries, comparator and Wine timings. Legacy waited
28.050 s for the lock and ran 41.419 s; fill and fade ran 6.470 / 8.739 s with
negligible lock wait. These are fixture durations, not GPU performance evidence.
Legacy includes 54 finite, signed-zero, NaN and infinity source cases; fill and
fade do not cover a separate nonfinite cross-product. Sun-share remains
host-qualified only. Native Windows runtime remains unverified.

The intentional 1,388-output zero-fill digest migration replaces
`3db6100189f38fa0b6300f6ff38c6871aa1a9adf59b5bd1bd91e93009c31dde8`
with `0e41b93663e77e7725a5d6bda8f5a84cff2193ee281af10519cf7cdda8267c6c`.
The two hull maxima pins become 180/192; the affected fill tests pass 4/4
(12.581 s). Earlier host qualification passed 13 tests in 52.763 s. Final
independent source/evidence review passed with no blockers.
