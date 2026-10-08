# Occlusion cull: verification ledger

Feature: [occlusion-cull.md](../architecture/occlusion-cull.md). Bottle X3 (CrossOver Preview, arm64 Wine with FEX,
`FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`); every Wine command ran alone under `verification/probe/wine_lock.py`.

## 2026-10-08: step 0, the per-query cost gate

Command (backend smoke fixture, new `--occlusion-cost` mode; wined3d with `--d3d9 builtin --wine-env
CX_GRAPHICS_BACKEND=wined3d`, DXVK with CrossOver's bundled `lib/dxvk/i386-windows/d3d9.dll --d3d9-order b
--wine-env CX_GRAPHICS_BACKEND=dxvk`):

```sh
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_d3d9_backend_smoke.py \
  --d3d9 <builtin|dxvk d3d9.dll> --name occlusion-cost-<backend> --no-sweep --occlusion-cost ...
python3 verification/results/occlusion-cull/gate.py   # -> gate.json
```

Records: `verification/results/bottle-X3/d3d9-backend-smoke/occlusion-cost-{wined3d,dxvk}.{json,txt}`,
`verification/results/occlusion-cull/gate.{py,json}`. All figures measured.

| per test (DXVK) | CPU (issue + GetData) | pipeline (event query, with vs without) | lag-1 ready |
| --- | --- | --- | --- |
| production shape (rectangle in c252/c253, ZERO/ONE blend) | 3.06 us p50, 3.37 max | 5.2 us p50, 5.44 max | 100 % at 13 and 20 ms pacing, N 128-512 |
| same with COLORWRITEENABLE 0 | 2.2-2.6 us | 55-79 us | 23-100 %, collapsing with N |
| 12-triangle box with a per-test dynamic-buffer Lock | 2.3-2.6 us | not measured | as above |

| per frame (DXVK, production shape) | CPU | pipeline |
| --- | --- | --- |
| N = 128 | 0.39 ms | 0.67 ms |
| N = 512 | 1.56 ms | 2.73 ms |

wined3d: production shape 1.8 us CPU, 20-23 us pipeline per test, 100 % lag-1 ready; the Lock variant 50-80 us CPU per
test. Unpaced (no frame pacing) neither backend has lag-1 results on DXVK: the fixture's hidden window presents at the
display rate, the GPU queue backs up; at 20 ms pacing every result was ready one frame later.

Gate: production CPU per test 3.37 us max < 5 us on DXVK: **passed**. Policy (user 2026-10-08): no budget setting;
every candidate tested every frame, limited by the fixed pool (512 per frame). At the close views (130-190 candidates,
67-91 hidden) the CPU cost is 0.44-0.64 ms against a 1.74-2.37 ms saving (inferred from the estimate's 26 us per draw).

## 2026-10-08: fixture, host tests, build

D3D fixture `verification/probe/occlusion_cull_fixture.cpp` (the production pass on a real device; runner
`run_occlusion_cull.py`, CMake target `occlusion_cull_fixture`):

```sh
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_occlusion_cull.py --backend wined3d --exe <build>/occlusion_cull_fixture.exe
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_occlusion_cull.py --backend dxvk --exe <build>/occlusion_cull_fixture.exe
```

Records `verification/results/occlusion-cull/fixture-{wined3d,dxvk}.{json,txt}`. Both backends 24/24 checks, for each
of A16B16G16R16F and A8R8G8B8 (measured):

- three hidden parts skipped in all 36 frame-part slots from frame 1 on (the frame after the Reset excluded);
- visible, in-front and alpha-tested parts never skipped (0 of 14 frames);
- the teleported part drawn the frame it left the hull (stability guard);
- the part revealed by the hull moving away skipped at frame K and drawn from K + 1 (`drawn_late` 1);
- the frames with the cull on and off byte-identical except frame K, whose 1,444 differing pixels all lie inside the
  revealed part;
- the application's shaders, declaration, stream, render states and c252/c253 back after every test;
- after the Reset the pool is 1,024 again and the first frame skips nothing; the device's reference count is the same
  after detach as before attach (1,028 references while attached);
- six back-to-back frames without readback: 45 of 54 reads not ready, 0 skipped.

The first fixture run caught a wrong winding choice (the CW and CCW frames culled the test rectangle, so visible parts
read hidden); fixed before the records above.

Host: `test_occlusion_cull` (core driver 49 checks: classification, rectangle, guard, ring, skip rule, classifier over
a synthetic image; draw-site order; lifetime under the accounting; programs; schema; gate record), `test_config_schema`,
`test_motion_wrap_states`, `test_exe_identity`, `test_motion_hdr_scene` OK. Full suite: 290 modules, 3,055 tests; the
only failure was `test_engine_effects.test_bound_to_its_production_sources` (its record binds `motion_output.h` and
`capture.cpp` hashes), resolved by the rerun below.

Build: fresh CMake configure, `d3d9` target 0 warnings; `check_no_x87.py` PASS (782 reachable functions, 0
violations). `tools/config/generate.py --check` PASS (258 settings, 108 in the template).

Not verified: a flight (in-game candidates, readiness and frame time); native Windows execution.

## 2026-10-08: review fixes (route's lazy RT1/RT2, bound formats, ready age, bounds source, stale extents, fill mode, FVF, hull owners)

Blocker: in the lazy RT mode the route keeps RT1 (A32B32G32R32F) and RT2 (R32F) bound between routed draws and the test's
ps wrote oC0 only. Protections measured with RT1/RT2 bound (smoke fixture `occlusion_mrt_cost`, 60 frames after 4
warm-ups, N = 128; records `occlusion-cost-{wined3d,dxvk}.json`, `gate.json` "mrt"; measured):

| protection | DXVK CPU / pipeline per test | wined3d CPU / pipeline per test | RT1 / RT2 texels changed |
| --- | --- | --- | --- |
| none (oC0 only) | 2.85 / 1.37 us | 1.52 / 64.4 us | 0 / 0 here, but undefined by D3D9 |
| flush and rebind RT1/RT2 | 3.56 / 67.5 us | 1.84 / 57.9 us | 0 / 0 |
| COLORWRITEENABLE1/2 = 0 | 2.84 / 63.9 us | 1.61 / 66.5 us | 0 / 0 |
| every output written 0 + ZERO/ONE blend (chosen) | 2.86 / 1.31 us | 1.54 / 17.1 us | 0 / 0 (-0.0 lane kept) |

Both backends report MRT post-pixel-shader blending and blendable A32B32G32R32F and R32F. The pass now writes 0 to every
output the device has, reads the bound targets from the device per test (each must blend; several need MRT blending),
which also fixes the format check under the HDR redirect. Other items: the cull uses the most recent ready result up to
two frames old (`ready_age=` per frame), refuses extents of an earlier buffer revision and non-solid fill, logs
`reason=no_bounds_source` and stays off without the candidate counter, restores the declaration before `SetFVF`, and
registers a hull's parent as owner only when it is a top-level ship root (`ancestry.{py,txt}`: hull ancestry 2 in every
run14/run15 hull draw with a known ancestry, measured).

Rerun (measured): the DXVK gate rows moved within run-to-run spread (production CPU p50 3.21 us, max 4.72 us < 5; the
first run 3.06 / 3.37); `test_occlusion_cull` (core driver 56 checks) OK; fixture 26/26 on wined3d and DXVK: the
fp16_mrt shape keeps RT1/RT2 byte-identical on and off except frame K inside the revealed part (1,444 texels each), all
earlier assertions unchanged; back-to-back frames on DXVK: 45 of 54 one-frame reads not ready, 27 decisions on age-2
results, 15 skips, every one on a ready hidden result (wined3d: 0 age-2 results ready). `generate.py --check` PASS; fresh
`d3d9` build 0 warnings; `check_no_x87.py` PASS (784 functions, 0 violations); `run_engine_effects.py --wine-env
CX_GRAPHICS_BACKEND=wined3d` 8 modes, 297 checks, 0 failed (its record rebinds to this tree).
