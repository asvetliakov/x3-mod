# Host suite runner

Ledger for the host test suite itself: how it is run and which
linear-named modules stay. Feature results stay in their own ledgers.

## Running

```sh
/usr/bin/python3 verification/probe/run_host_suite.py            # parallel, default jobs
/usr/bin/python3 verification/probe/run_host_suite.py --jobs 8
/usr/bin/python3 verification/probe/run_host_suite.py --serial   # reference ordering
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

and discovers the same modules.

Parallel safety audit (2026-09-21): no module under `verification/analysis`
writes into the repository tree, into `verification/results`, or to a fixed
temp path; every host fixture is compiled and run inside its own
`tempfile.mkdtemp`/`TemporaryDirectory`. The runner therefore needs no serial
group today; `SERIAL_GROUP` in the runner is the place to add one if a module
ever acquires a shared output path.

Microsoft Defender on this Mac quarantines synthetic PE files: a vanished temp
file or `objdump: Operation not permitted` in a worker is Defender, not the
runner.

## Retired modules (deleted)

The 23 retired modules (`--linear-materials`, the converted hull material law,
and the full-surface `--linear-emissions` bracket rejected 2026-09-15) and their
skip hook `verification/analysis/retired_tests.py` were deleted on 2026-09-22 in
cleanup batch 3 (`docs/architecture/cleanup-inventory-2026-09-22.md`, section G).
Both options are still compiled into the DLL behind default-off flags. The
runner keeps `--include-retired` as a no-op. Host suite before and after: 233
modules / 2,311 tests.

Linear-named modules that stay, because they cover flown or compiled behaviour:

| Module | Subject |
| --- | --- |
| test_linear_emission_pass_programs | byte-exactness of `linear_emission_copy_clear_inc.h` transfer arrays compiled into the DLL |
| test_linear_emission_pass_host | `linear_emission_pass.cpp` still compiles against the public host D3D interfaces |
| test_linear_emission_sm1_transformer | the nine SM1 screen pairs: `--screen-emission` / `--screen-emission-additive`, flown |
| test_linear_emission_sm1_report | SM1 parity/capability gates of the same flown pairs |
| test_linear_emission_sm1_packed_report | packed SM1 ordering/domain/resource gates, flown |
| test_linear_emission_source_gain | `--emission-source-gain`, installed and flown |
| test_linear_emission_hull_gain | hull guide-light gain (`--hull-emitters`), flown |
| test_linear_sun_share | the share-lane generator over the 108 opaque hull programs; the sun-shadow lane is flown on original shading |
| test_linear_cutout_contract | cutout identity plus live capture/TAA contracts (depth-surface rebind, history union, retry) |
| test_linear_cutout_live_report | cutout live report; the cutout pairs carry the flown lane share |
| test_alpha_test_caps_probe | public capability query arguments, not a linear-material subject |

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
