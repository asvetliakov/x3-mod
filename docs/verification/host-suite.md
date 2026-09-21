# Host suite runner and retired tests

Ledger for the host test suite itself: how it is run, which modules are retired
from default discovery and why. Feature results stay in their own ledgers.

## Running

```sh
/usr/bin/python3 verification/probe/run_host_suite.py            # parallel, default jobs
/usr/bin/python3 verification/probe/run_host_suite.py --jobs 8
/usr/bin/python3 verification/probe/run_host_suite.py --serial   # reference ordering
/usr/bin/python3 verification/probe/run_host_suite.py --include-retired
```

The runner starts one `python -m unittest <module>` worker per discovered
module, longest module first from `verification/probe/host_suite_durations.json`
(a scheduling hint only; refresh with `--write-durations`). Workers run with cwd
at the repository root and `verification/probe` plus `verification/analysis` on
`PYTHONPATH`, which is what the canonical discover command gives a test module,
so `verification.analysis.*` and probe imports resolve identically. Use the
interpreter that has NumPy: `/usr/bin/python3` on the project Mac (`sys.executable`
is what the workers inherit). Exit code is non-zero on any failure, any error,
or any module that runs zero tests.

The reference command stays

```sh
PYTHONPATH=verification/probe python3 -m unittest discover -s verification/analysis -p 'test_*.py'
```

and skips the same retired modules (each carries the `load_tests` hook).

Parallel safety audit (2026-09-21): no module under `verification/analysis`
writes into the repository tree, into `verification/results`, or to a fixed
temp path; every host fixture is compiled and run inside its own
`tempfile.mkdtemp`/`TemporaryDirectory`. The runner therefore needs no serial
group today; `SERIAL_GROUP` in the runner is the place to add one if a module
ever acquires a shared output path.

Microsoft Defender on this Mac quarantines synthetic PE files: a vanished temp
file or `objdump: Operation not permitted` in a worker is Defender, not the
runner.

## Retired modules

Retired means the feature is not flown and not planned: the tests stay in the
tree, keep passing under `--include-retired`, and are hidden from default
discovery by `verification/analysis/retired_tests.py`. Two subjects:
`--linear-materials` (the converted hull material law; the user plays original
hulls) and `--linear-emissions` in its full-surface bracket shape (rejected
2026-09-15). Both are still compiled into the DLL behind default-off options.

| Module | Decision | Subject |
| --- | --- | --- |
| test_linear_material_report | retired | GPU-evidence checker/serialiser for the linear material fixture |
| test_linear_material_profiles | retired | offline site proof of the converted material sites |
| test_linear_material_reference | retired | DEFAULT/BUMPMAP linear colour equations |
| test_linear_material_transformer | retired | the pure transformer over the 115 original programs |
| test_linear_material_constant_port | retired | constant-read-port legality of converted material shaders |
| test_linear_material_fill | retired | `--material-fill` inside the converted law |
| test_linear_material_live | retired | live material control flow (converted path) |
| test_linear_material_live_report | retired | consume-only live material GPU evidence |
| test_linear_glass_reference | retired | glass equations of the converted law |
| test_linear_glass_live_report | retired | live glass report witnesses |
| test_linear_alpha_test_fixture | retired | cutout cases of the converted material fixture |
| test_material_exposure | retired | selective exposure; `material_exposure_*` is referenced only by `linear_material.cpp` |
| test_linear_distance_fade | retired | six-pair fade producer, requires `--linear-materials` |
| test_linear_distance_fade_report | retired | fade evidence gates |
| test_linear_distance_fade_live_report | retired | live fade-region witnesses |
| test_linear_emission_transformer | retired | bracket PS2 emission augmentation |
| test_linear_emission_original_report | retired | bracket actual-original oracle/report |
| test_linear_emission_report | retired | bracket oracle/parser |
| test_linear_emission_coverage_report | retired | bracket three-output coverage producer |
| test_linear_emission_pass_report | retired | bracket component report gates |
| test_linear_emission_fused_report | retired | bracket fused-copy evidence gates |
| test_linear_emission_mrt_report | retired | bracket PS2 MRT composition oracle |
| test_linear_emission_live | retired | bracket live frame/quarantine report |
| test_linear_emission_pass_programs | **kept** | byte-exactness of `linear_emission_copy_clear_inc.h` transfer arrays compiled into the DLL |
| test_linear_emission_pass_host | **kept** | `linear_emission_pass.cpp` still compiles against the public host D3D interfaces |
| test_linear_emission_sm1_transformer | **kept** | the nine SM1 screen pairs: `--screen-emission` / `--screen-emission-additive`, flown |
| test_linear_emission_sm1_report | **kept** | SM1 parity/capability gates of the same flown pairs |
| test_linear_emission_sm1_packed_report | **kept** | packed SM1 ordering/domain/resource gates, flown |
| test_linear_emission_source_gain | **kept** | `--emission-source-gain`, installed and flown |
| test_linear_emission_hull_gain | **kept** | hull guide-light gain (`--hull-emitters`), flown |
| test_linear_sun_share | **kept** | the share-lane generator over the 108 opaque hull programs; the sun-shadow lane is flown on original shading |
| test_linear_cutout_contract | **kept** | cutout identity plus live capture/TAA contracts (depth-surface rebind, history union, retry) |
| test_linear_cutout_live_report | **kept** | cutout live report; the cutout pairs carry the flown lane share |
| test_alpha_test_caps_probe | **kept** | public capability query arguments, not a linear-material subject |

## Timing

Repeated work removed on 2026-09-21 (no assertion changed):
`verify_submit_phase_sites.decode`, `raw_scan` and `data_reference_hits` are
memoised on the image bytes / file identity. The site tests call `inspect()`
about seventy times on the same 55 MB image, and each call re-scanned the whole
`.text` section and re-decoded the EXE through `objdump`.

`test_fog_density_cache.test_window_recentre_readiness_logic` is host compute
(two 128³ grids filled node by node in the deterministic stepped worker), not
waiting; its two binaries are already compiled once per class in `setUpClass`.
It was left as it is.
