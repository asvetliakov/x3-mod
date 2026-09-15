# Host suite run — 2026-09-15

Command:

```
PYTHONPATH=verification/probe python3 -m unittest discover -s verification/analysis -p 'test_*.py'
```

Result: `Ran 1852 tests in 478.908s` — `FAILED (failures=9, errors=2)`. No Wine
invocation observed anywhere in the output (no `wine_lock`, `WINEPREFIX`, or
bottle references).

## Failures/errors grouped by cause

### Missing local artifact (fixture/corpus not present)

| Test | Cause |
| --- | --- |
| `test_linear_emission_fused_report.FusedReportTests.test_runner_consumes_prebuilt_and_binds_all_inputs` | fixture writes `ps_{name}-{gain}{suffix}.bin`, production `run_linear_emission.py:1071` reads `ps_{name}-source-{g}.bin` for `g in range(4)`; `FileNotFoundError` on the source-gain filename the fixture never creates |
| `test_linear_emission_sm1_transformer.Sm1TransformerTests.setUpClass` | `CalledProcessError` from the built `structure` tool against `/tmp/x3-shader-sweep/programs`; that corpus directory is a local, untracked shader-sweep artifact not present in this checkout |

### Code/test mismatch (compiled fixture vs. current production headers)

| Test | Cause |
| --- | --- |
| `test_capture_bloom_lifetime.CaptureBloomLifetimeTests.test_production_lifetime_and_reset_control_flow` | compile error: use of undeclared identifier `lod_scale` in the generated `_under_test_inc.h` |
| `test_game_phase_sites.SourceAndReplay.test_combined_hook_arena_footprint_and_admission` | compile error: use of undeclared identifier `emit_stub` in generated `count.cpp` |
| `test_linear_material_live.LinearMaterialLiveTests.test_production_control_flow` | compile errors: undeclared `blend_shadow_requested`, `sun_sentinel_ps_` (20 errors, error limit hit) |
| `test_motion_hdr_scene.MotionHdrSceneTests.test_synchronous_handoff_and_default_null_parity` | compile errors: unknown type `IDirect3DResource9` in `src/proxy/motion_output.h:907`, undeclared `D3DFMT_G32R32F` (3 errors) |
| `test_motion_wrap_states.MotionWrapStatesTests.test_production_transaction` | compile errors: undeclared `blend_shadow_requested`, no member `sun_receiver` in `MotionRoute` (17 errors) |

### Code/test mismatch (string-literal assertion vs. refactored source text)

| Test | Cause |
| --- | --- |
| `test_linear_cutout_contract.LinearCutoutContractTests.test_failure_notifications_keep_native_boundary_and_history_union` | `assertIn('test == 1 && color == 7 && shadow_.cutout_pair && cutout_draw_state()', gate)` — source now reads `... && (cutout_ok = cutout_draw_state())`, literal no longer matches |
| `test_linear_emission_live.EmissionLiveTests.test_successful_resolve_readbacks_include_reset_and_first_frame` | `assertIn('config.force_taa_readback = emissions && !emission_bench;', source)` not found in current generated source |
| `test_motion_wrap_states.MotionWrapStatesTests.test_route_wiring` | `assertIn("else if (route.submit) prepare_composition(call, route)", before)` — source now has `} else if (route.submit) {\n prepare_composition(call, route);` (brace/format change) |

### Byte-exact assertion mismatch (data drift, not obviously artifact-missing)

| Test | Cause |
| --- | --- |
| `test_linear_distance_fade.DistanceFadeProducer.test_existing_opaque_programs_remain_byte_exact` | `assertEqual` tuple mismatch: exported shader bytecode differs from stored `expected` bytes starting at byte offset within a 16184-char shared prefix (31567-char diff) — golden bytes out of sync with current export, code/test mismatch |

## Notes

- Full stdout/stderr saved locally (untracked) at
  `/private/tmp/claude-501/-Users-asvetl-x3-mod/77d6468d-b9c8-46ee-8005-c3ebf5171bf6/scratchpad/host-suite-full.txt`
  and the tail-20000-byte copy per the run brief at
  `/private/tmp/claude-501/-Users-asvetl-x3-mod/77d6468d-b9c8-46ee-8005-c3ebf5171bf6/scratchpad/host-suite.txt`.
- These 11 failures were not diagnosed further or fixed; this is a triage
  record only.
