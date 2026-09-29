# dust_leak_fix evidence (2026-09-29)

Ledger: `docs/verification/dust-leak-fix.md`. Commands run from the repository root; every figure in the ledger
comes from one of these.

```sh
# Site verifier on the installed EXE (read-only) -> verify_dust_leak_fix_site_out.json in this directory
python3 verification/probe/verify_dust_leak_fix_site.py > verification/results/dust-leak-fix/verify_dust_leak_fix_site_out.json
python3 -c "import json; r=json.load(open('verification/results/dust-leak-fix/verify_dust_leak_fix_site_out.json')); print(r['result'], sum(r['checks'].values()), len(r['checks']), r['tail_esp_offsets'], r['site_sources'], r['function_instructions'], r['other_claims'])"

# Wine fixture (the only Wine command; never two at once) -> verification/results/bottle-X3/dust-leak-fix-patch.json
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_dust_leak_fix_patch.py
python3 -c "import json; r=json.load(open('verification/results/bottle-X3/dust-leak-fix-patch.json')); print(r['check_count'], r['pass_count'], len(r['cases']), sum(c['ok'] for c in r['cases']), r['exit_status'], r['elapsed_s'], r['timing_us'], r['arena'], r['hits_rows'])"

# Host tests, schema views, full suite, DLL build and the no-x87 walk
PYTHONPATH=verification/probe:verification/analysis python3 -m unittest test_config_schema test_launcher_defaults test_logging_tiers test_dust_leak_fix_site test_game_phase_sites test_sun_flare_fix_site test_terran_lod_site test_lod_occlusion_site
python3 tools/config/generate.py --check
/usr/bin/python3 verification/probe/run_host_suite.py
cmake -S . -B build/dll -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPython3_EXECUTABLE=/usr/bin/python3 && cmake --build build/dll -j8 2>&1 | grep -c warning
python3 verification/probe/check_no_x87.py build/dll/d3d9.dll | grep -E '"result"|"reachable_functions"'
```
