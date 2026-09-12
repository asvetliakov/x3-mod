# Fixture runners and CrossOver bottles

Status 2026-09-12 (evening, paused for an account switch): the runner-side
switch is implemented; validation under the X3 bottle has **not produced a
result yet**. See `handoff-bottle-switch.md` for the exact next command.

## Two bottles

| Bottle | WineArch | Wine | Environment lines recorded | Game files |
|---|---|---|---|---|
| `Steam` | `win64` | x86_64 Wine under Rosetta | `WINEMSYNC=1` | `drive_c/X3/X3AP.exe`, `d3dx9_37.dll` (same bytes as X3) |
| `X3` | `arm64` | CrossOver Preview native arm64 Wine, FEX x86 emulation | `FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1` | same |

The game now runs in `X3` (`tools/manage.py` defaults to it, `X3M_BOTTLE`
overrides). The fixtures keep `Steam` as their default so every record under
`verification/results/` stays comparable with the committed evidence.

## Runner-side selection (`verification/probe/bottle.py`)

- `bottle.BOTTLE = os.environ.get('X3M_FIXTURE_BOTTLE', 'Steam')`; every runner
  passes `'--bottle', bottle.BOTTLE, '--no-update'` (48 scripts: the `run_*.py`
  runners, `mesh_run.py`, `temporal_run.py`, and the three `verify_*.py` readers).
  `wine_args()` returns the same triple for new code.
- `bottle.results_dir(root)`: `verification/results` for Steam, otherwise
  `verification/results/bottle-<name>/` (created on demand). Runners write and
  read back their own records there. Generated *inputs* that are not per bottle
  stay on the shared path (`rigid-motion-pixel-program.json`,
  `shader-profile-registry.json`).
- `bottle.bottle_dir()` / `bottle.game_dir()` replace the hard-coded
  `Bottles/Steam/...` paths (native `d3dx9_37.dll`, `drive_c/windows/syswow64`,
  `dosdevices`), so the native module hashes recorded by a run come from the
  bottle that ran it.
- `bottle.describe()` is stored as `bottle` in every summary JSON the runners
  write: `{name, wine_arch, environment{FEX_X87REDUCEDPRECISION, WINEMSYNC},
  cxbottle_conf}`, read from the bottle's `cxbottle.conf`. `bottle.label()` is
  the text form used by `sampling-profiler.txt`'s header. Records without the
  key predate this change and were all produced in the Steam bottle.
- `run_sampling_profiler.py`'s idle-wait now skips `pgrep -fl` continuation
  lines of multi-line commands (they are not `pid args` rows and raised
  `ValueError` before any fixture started).
- The fixture logic, expectations and `src/` are unchanged.

## X3 validation plan and record

Run one suite at a time with `X3M_FIXTURE_BOTTLE=X3`; results land in
`verification/results/bottle-X3/`. Compare against the Steam records in
`verification/results/` (HEAD versions; the working-tree copies of
`temporal-pass-summary.json` and `loading-trace-mesh-summary.json` are mid-edit
by other agents and currently record failures unrelated to bottles).

| Order | Suite | What it stresses under FEX/arm64 | X3 result | Steam reference |
|---|---|---|---|---|
| 1 | `run_sampling_profiler.py` | thread suspension, `GetThreadContext`, TEB reads from the sampler thread | **not run** (the off pass started once, then was interrupted for the pause; no record kept) | pass, 22/22 checks; overhead a 10.5 %, c 5.4 %, main 0.9 %; tick mean 558 us, max 9144 us, 940 ticks, 2536 samples, 0 dropped; walls 4.19 s / 3.43 s |
| 2 | `run_loading_trace.py` | IAT + vtable hooks, x87 `CpuState` under reduced precision | not run | pass, loading-trace 85 checks, loading-mesh 123 checks (HEAD) |
| 3 | `run_motion_output.py` | route, scene-hook callsite patch on the fixture exe, HDR FP16 MRT on D3DMetal | not run | pass, 70 cases; bench 1280x768 boundary median 0.354 ms TAA off / 0.707 ms TAA on; 5120x1440 0.484 ms off |
| 4 | `run_temporal_pass.py` | SM3 resolve on FP16 targets, device Reset | not run | pass, 386 samples, 448/204/2 (working tree) |
| 5 | `run_ownership_integration.py` | 26 isolated proxy cases with `d3d9=n,b` | not run | PASS, 26 cases |
| - | `check_no_x87.py` | host-only | skipped by design | - |

Things to look at once the runs exist: profiler tick cost and context accuracy
(`sampler_cost.tick_us_mean/max`, the `a_*`/`c_*` attribution fractions, and
`b_leaf_kinds`, which may attribute thread B's leaf to a different module under
the FEX loader); whether the loading-trace admission witness and the x87 state
checks hold with `FEX_X87REDUCEDPRECISION=1`; whether the scene-hook callsite
patch on the fixture executable behaves the same under the FEX JIT; and the HDR
FP16 MRT twins on D3DMetal via the arm64 Wine. Do not change fixture
expectations to make an environment difference pass; record the differing check
here instead.
