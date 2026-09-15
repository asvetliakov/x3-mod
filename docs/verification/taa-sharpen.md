# Post-resolve sharpen (RCAS): fixture record

Design and placement: [temporal-integration.md, "Post-resolve sharpen"](../architecture/temporal-integration.md#post-resolve-sharpen-2026-09-12).
Switch: `X3M_TAA_SHARPEN=<0..1>` (`tools/manage.py launch --taa-sharpen`,
requires `--taa`). Programs: `src/temporal/rcas.hlsl` (core),
`taa_sharpen_ps.hlsl` (418 words) and `agx_sharpen_ps.hlsl` (1,691 words),
embedded through `tools/shaders/generate_rigid_motion_pixel.py`, which now
expands `#include` lines textually and records every include's hash in the
provenance (`verification/results/taa-sharpen-program.json`,
`hdr-tonemap-sharpen-program.json`). The seven pre-existing programs kept
their bytecode hashes through the change (AgX's `main` was split into
`agxTonemap()` plus a guarded `main`; `hdr-tonemap-program.json` still
reads `f5e78954…`, 414 words). Synthetic verification only; nothing here is
gameplay-verified. Bottle: `Steam` (the runners' default).

## Temporal pass fixture (`run_temporal_pass.py`)

`temporal_pass_fixture.exe` now takes the sharpen program as its fourth
source (`<D3DX> <decoder> <resolve> <sharpen> [stationary-only|sharpen-measure]`)
and compiles it from the same files the generator embeds (the fixture
expands the include itself). The suite passes **468 numerical checks and
228 complete state comparisons across two device generations** (448 / 204
before: the new `sharpen_cases` add 10 numerical and 12 state checks per
generation; 386 samples, `RESET PASS` and the three negative controls
unchanged). The cases:

| Case | Result |
| --- | --- |
| Validation | `sharpen` < 0, NaN, > 1 refused (`E_INVALIDARG`, no output); `sharpen` > 0 on the FP16 input path refused; `sharpen` > 0 on a pass initialised without the program refused; the main target untouched by every refusal |
| Off | `sharpen = 0` on a pass created with the program draws nothing (`display_written` false, main target byte-identical) and its history equals a pass without the program byte for byte, over two frames with history |
| On, 1.0 | Two frames through an off and an on pass on the same inputs: FP16 colour and depth history byte-identical; the main target after the on run is the display image, 129 of 256 pixels changed, **0 channels outside the 3×3 min/max** of the unsharpened copy-back, 0 alpha differences; state snapshot identical after every run (the sharpen draw restores RT0, PS, `c23`, samplers) |
| On, 0.5 | Same, 102 of 256 pixels changed, 0 outside, 0 alpha differences |
| NaN/inf | The program alone on an FP16 image: an all-NaN input gives one uniform code (the backend's `saturate(NaN)` = 0); with NaN, +∞ and −∞ pixels among finite values every output lies inside the 3×3 min/max with those taps read as 0, 255 and 0 (`nan_pixel=0,0,0 posinf_pixel=255,255,255 neginf_pixel=0,0,0`), 0 outside, alpha carried |

### Measurement (`sharpen-measure` mode)

A 128×128 synthetic resolved-looking image: 4×-supersampled slanted bars, a
disc and a box on 0.35 grey, box-filtered and blurred with a Gaussian of
σ = 0.75 px (a 10–90% rise of about 1.8 px, the resolved edge of burst 629
in run 2), plus a horizontal ramp band, mildly tinted. Through the pass at
sharpness 0 (the current-only first frame returns the input exactly through
the FP16 round trip, then the copy-back) and 0.25 / 0.5 / 1.0 (the sharpen
draw; every image inside the 3×3 bound of the input). Metrics are the run-2
functions (`analyze_iteration09_run2.class_gradient_energy` on luma, all
pixels; `edge_spread` on the strongest vertical edges, aligned on their 50%
crossings — a comparison between images of the same content, never an
absolute MTF):

| Sharpness | Gain (stops) | Gradient-energy ratio vs 0 | 10–90% rise | MTF50 (c/px) | Edges |
| --- | --- | --- | --- | --- | --- |
| 0 (off) | — | 1.000 | 1.82 px | 0.262 | 1,839 |
| 0.25 | 0.354 (1.5) | **1.043** | 1.71 px | 0.269 | 1,658 |
| 0.5 | 0.5 (1.0) | **1.063** | 1.66 px | 0.266 | 1,502 |
| 1.0 | 1.0 (0) | **1.147** | **1.49 px** | **0.298** | 1,319 |

Reading: the strongest setting recovers about 15% of gradient energy and
0.33 px of edge rise on this content — a fraction of what the supersampling
removes (run 2: ratio 0.51–0.63, MTF50 halved), as expected of a limited
single-lobe sharpen that is forbidden to overshoot; the ratio is monotonic
in the setting, as the runner asserts. The edge count falls with sharpness
because the candidate selection (anisotropy, contrast) is made per image.
Files: `verification/results/temporal-sharpen-measure.txt`, the metrics
under `sharpen_measure` in `temporal-pass-summary.json`.

## Motion-output fixture (`run_motion_output.py`)

The route through the actual proxy DLL and the seam DLL, 83 cases (77
before; `verification/results/motion-output-summary.json`, `passed: true`).
The fixture takes `X3M_TAA_SHARPEN` like the DLL, checks only the alpha
carry of the presented image on sharpened runs (`TAA_SHARPEN … mismatches=0`
on every frame) and waives its "no-history frame is bit-identical" check on
them; the runner compares the presented frames against a Python RCAS
reference (`rcas_reference` in `run_motion_output.py`, the shader's
arithmetic in double on the display-referred image: the resolved FP16
values on the 8-bit route, their clamp for the identity write-back, the AgX
reference at the frame's EV for the tonemap) and the resolved FP16 history
files against the unsharpened twin's.

| Case | History vs twin | Presented vs twin | Presented vs RCAS reference (frames 1–8) |
| --- | --- | --- | --- |
| `seam-taa-sharpen-off` (0) | byte-identical | **byte-identical**, colour hashes and check counts equal | — |
| `seam-taa-sharpen-on` (1.0) | byte-identical | differs (98.6% of pixels exact, largest 9 codes) | max error **0.498 code**, mean 0.088; 0 channels outside the 3×3 min/max of the unsharpened codes; alpha exact; 1.2% of pixels changed |
| `seam-taa-sharpen-half` (0.5) | byte-identical | differs (98.9% exact, largest 3) | max 0.500, mean 0.088; 0 outside; alpha exact; 0.9% changed |
| `seam-taa-hdr-sharpen-on` (identity write-back) | byte-identical | differs (95.9% exact, largest 9) | max 0.500, mean 0.326; 0 outside; alpha exact; 3.6% changed; `hdr_frame … sharpen=ok sharpened=1 sharpen_fallback=0` |
| `seam-taa-hdr-tonemap-sharpen-on` (AgX) | byte-identical | differs (94.3% exact, largest 8) | max 0.500, mean 0.385; 0 outside; alpha exact; 5.0% changed; the other order `AgX(RCAS(resolved))` differs from the presented frame by more than one code on 2–106 pixels per frame (**sharpened after the tonemap**) |

The GPU programs reproduce the double-precision reference to within the
8-bit rounding (every maximum is ≤ 0.5 code): the `rcp`-based divisions of
ps_3_0 do not reach a code. The small changed fractions are the fixture's
scene (flat colours and a few material draws; RCAS does nothing on flat
regions and, by the published limiter, on rings touching 0 or 1 in a
channel). Every frame line reports `taa_sharpen=1` with `taa_copy` at
`S_FALSE` (no copy-back: the pass drew the display image), the pass holds
two device references (resolve and sharpen program) instead of one, and
every pre-existing case reproduces its record (the bit-identical
`seam-taa-sharpen-off` twin is the direct proof for the 8-bit route).

### Bench (`cpu_inclusive_event_synchronized`, median of 20 timed frames)

| Size | Route | Unsharpened boundary | Sharpened boundary | Sharpen cost |
| --- | --- | --- | --- | --- |
| 5120×1440 | 8-bit (RCAS draw replaces the FP16→8-bit `StretchRect`) | 2.252 ms | 2.238 ms | **−0.014 ms** (within the spread: the draw costs what the copy cost) |
| 5120×1440 | HDR, AgX (five AgX evaluations per pixel instead of one) | 2.220 ms | 3.024 ms | **+0.80 ms** |
| 1280×768 | 8-bit | 0.680 ms | 0.809 ms | +0.13 ms |
| 1280×768 | HDR, AgX | 1.251 ms | 1.793 ms | +0.54 ms |

The 8-bit route's sharpen is free at the game size because it is not an
extra pass: the copy-back became the draw. The tonemapped route pays for
tonemapping the four neighbours; if a gameplay profile shows that cost, the
lever is a separate 8-bit pass after one AgX evaluation (one more
full-screen target and draw, about the copy-back's cost) rather than
sharpening before the tonemap.

## Presented-image readback (`present_<device>_<frame>.bgra8`, review 27)

The two `--taa-debug` image readbacks of review 26 were taken before the
sharpen draw: `color_1_*.bgra8` is the pre-resolve main target and
`taa_1_*.rgba16f` is `Output::color_surface`, the unsharpened history input.
Run 10 of [iteration-12.md](iteration-12.md) therefore could not measure the
sharpen at all. `MotionOutput::resolve` now reads the game's main target back
a third time, **after** the RCAS draw (or the point-filtered copy-back when
the sharpen is off or fell back), as `present_<device>_<frame>.bgra8`
(`motion_output_present_readback`, `bgra8_row_major`); on the HDR route
`hdr_writeback` reads it after the write-back of a resolved frame, i.e. after
AgX and/or RCAS. Same machinery as the other readbacks
(`readback_surface`: system-memory surface, `GetRenderTargetData`, file
beside the log), capture frames with `--taa-debug` only, so nothing changes
outside them; `readbacks` on the frame line rises from 4 to 5 (HDR: 6). The
kinds are listed in [capture-format.md](../architecture/capture-format.md).

`run_motion_output.py` checks the new file on every TAA case (25 cases carry
it: production and seam, 8-bit and HDR, plain/ownership/lazy/camera/sentinel,
mip bias, the five sharpen cases), frames 1-8:

| check | result (bottle `Steam`, 2026-09-12) |
| --- | --- |
| log line and file | `result=00000000`, 16,384 bytes, `present_1_<frame>.bgra8`, `bgra8_row_major` on 8 of 8 frames of all 25 cases |
| equals the presented image | **byte-identical** to the fixture's own back-buffer dump `presented_<frame>.bgra8` on every frame of every case (8-bit route: after the sharpen draw / copy-back; HDR route: after the write-back) |
| sharpen off, no tonemap (`seam-taa-on`, `seam-taa-sharpen-off`, production, ownership, lazy, camera, mip bias, `seam-taa-hdr-on`) | equals the resolved FP16 image `taa_1_<frame>.rgba16f` through the 8-bit conversion: max **0.499 code** (production current-only 0.060; HDR identity write-back 0.500), alpha max 0.052 |
| sharpen on (`seam-taa-sharpen-on` 1.0, `-half` 0.5, `seam-taa-hdr-sharpen-on`, `seam-taa-hdr-tonemap-sharpen-on`) | differs from `taa_1` by the Python RCAS reference of it (`rcas_reference`, display-referred; AgX first on the tonemap case): max error **0.498 / 0.4995 / 0.500 / 0.50002 code**, 0 channels outside the 3x3 min/max of the unsharpened codes, alpha exact; the runner's bound stays `SHARPEN_MAX_CODE_ERROR + 0.5` = 1.5 |
| sharpen-off twin | `seam-taa-sharpen-off` remains byte-identical to `seam-taa-on` in history, presented frames and now the present readback |

Summary keys: `cases.<name>.present_readbacks` (per frame `equals_presented`
and, unsharpened, `max_code_error_vs_resolved`) and, on the sharpen cases,
`cases.<name>.sharpen.images.<frame>.present_readback` /
`present_equals_presented`. `tools/analysis/analyze_iteration12.py` consumes
`present_<device>_<frame>.bgra8` directly when a capture has it (section
2.4: 3x3 escapes, change against the unsharpened codes, halo measures,
flicker) and falls back to the RCAS model of `taa_<device>_<frame>` otherwise.

### Suite run with the readback (2026-09-12, worktree of review 27)

One at a time under `wine_lock.py`, bottle `Steam`, `build/d3d9.dll`
SHA-256 `c24b2046010231dbbd5987ad1ad0651f32c1879ba103a21b212934590633c26a` (relinked by the runner's own `cmake --build` from the same sources)
(RelWithDebInfo; also carries the `ID3DXMesh` forward declaration that the
`b10d129` checkpoint's `loading_trace.h` needed to compile):

| Step | Result |
| --- | --- |
| `run_motion_output.py` | passed: **94 cases** (the 83 above plus the mip-bias cases), 25 with the present readback as tabulated |
| `run_temporal_pass.py` | passed: 386 samples, 228 state restorations, 2 generations, `RESET PASS` (the pass and its fixture are untouched by the change) |
| `run_scene_capture.py` | passed |
| `run_ownership_integration.py` | passed: 27 cases, all `exit=0` |
| `check_no_x87.py build/d3d9.dll` | PASS, no violations over 149 reachable functions |
| `python3 -m unittest discover -s verification/analysis` (`PYTHONPATH=verification/probe`) | see [iteration-12.md](iteration-12.md) (adds `test_iteration12.py`) |

## Other suites and checks

Run one at a time under the machine-wide Wine runner lock
(`verification/probe/wine_lock.py`), bottle `Steam`:

| Step | Result |
| --- | --- |
| `run_temporal_pass.py` | passed: 468 numerical / 228 state / 2 generations, 386 samples, `RESET PASS`, three negative controls rejected, sharpen measurement recorded (above) |
| `run_motion_output.py` | passed: 83 cases (77 + `seam-taa-sharpen-{off,on,half}`, `seam-taa-hdr-sharpen-on`, `seam-taa-hdr-tonemap-sharpen-on`, four sharpen benches), every pre-existing case reproducing its record |
| `temporal_run.py` | the fixture passes (`RESULT PASS samples=78 generations=2`, report byte-identical to the tracked one; `resolve.hlsl` and the fixture are untouched by this change) but takes 61 s on the loaded machine (load average 4–5 from concurrent agent suites) against the runner's fixed 60 s `subprocess` timeout, so the runner reports `timed_out`; four attempts. The tracked record was left as committed. Open: rerun on a quiet machine, or widen that timeout |
| `run_ownership_integration.py` | passed: 27 cases, all `exit=0` |
| `run_scene_capture.py` | passed |
| `generate_rigid_motion_pixel.py --check` | PASS for all nine programs (the seven old hashes unchanged) |
| `check_no_x87.py build/d3d9.dll` | PASS, no violations over 130 reachable functions |
| `python3 -m unittest discover -s verification/analysis` (`PYTHONPATH=verification/probe`) | 710 tests OK |
| `manage.py launch --dry-run` | `--taa-sharpen 0.5` exports `X3M_TAA_SHARPEN=0.5`; rejected without `--taa` and outside [0, 1] |

`build/d3d9.dll` SHA-256 (RelWithDebInfo, the DLL the motion-output suite
ran): `43f8bc2d1e04bf7b9b86a87e826d92d9c7dbe2db1f0067f9410764b4b3cc8ffc`.

## 2026-09-16 — sharpen 0.75 and mip bias −0.5 become the TAA defaults

User decision after run 27: with `--taa` on, `X3M_TAA_SHARPEN=0.75` and
`X3M_TAA_MIP_BIAS=-0.5` are the defaults. The launcher always forwards both in
TAA mode (stale-shell-proof, like the chase framing constants) and drops them
outside it; the DLL falls back to the same pair only when `X3M_TAA=1`, so a
direct `WINEDLLOVERRIDES` start matches. An explicit `0` still disables either
and keeps the bit-identical route (the sharpen program is not created).

| Step | Result |
| --- | --- |
| `python3 -m unittest test_taa_image_defaults` (`PYTHONPATH=../probe`, from `verification/analysis`) | 6 tests OK (new module: defaults, stale-value override, explicit 0, dropped without TAA, refusals, DLL fallback gating) |
| `python3 -m unittest` over the 17 launcher/TAA-adjacent analysis modules | 219 tests, 1 pre-existing failure unrelated to this change (`test_linear_material_live.test_production_control_flow`: the `composition_blend_index` extraction fails on the unmodified tree too) |
| `cmake --build build -j4` + `check_no_x87.py build/d3d9.dll` | clean build; PASS, no violations over 230 reachable functions |
| `./x3run --camera chase --motion-output --ownership --object-trace --object-lifetime --taa --hdr --hdr-tonemap --dry-run` | `X3M_TAA_MIP_BIAS=-0.5`, `X3M_TAA_SHARPEN=0.75`; with `--taa-sharpen 0 --taa-mip-bias 0` both `0.0` |

`build/d3d9.dll` SHA-256 (RelWithDebInfo, this worktree, not an install
candidate): `a2d6047d19edfefac095da5533922c7213a40b82564e81842311f57dd8f27f9c`.
