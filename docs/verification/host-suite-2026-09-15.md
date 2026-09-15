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

## Repair (2026-09-15)

All eleven were test/fixture drift behind reviewed production commits; no
production source was changed. Per test, cause → fix:

| Test | Cause | Fix |
| --- | --- | --- |
| `test_capture_bloom_lifetime` | `capture.cpp` calls `lod_scale::refresh()` on the Reset/Present paths; the fixture had no such namespace | added a no-op `lod_scale::refresh` stub to `capture_bloom_lifetime_fixture.cpp` |
| `test_game_phase_sites` | 123f98d made `chase_transition::emit` a wrapper over the shared `emit_stub`, added seven restore sites and raised the reservation | extract `emit_stub` with `emit`, mirror `restore_filter_count` from its own header, `<atomic>` in the counting emitter; reserve 192→320, rows 9→16, arena used 10724→11792 (free 4592), old-selection 7056→8124 |
| `test_linear_material_live` | sun-share lane, source-gain and additive-screen members/APIs added to `motion_output.h` | mirrored the new `ShaderEntry`/`Shadow`/`MotionRoute`/`Counters` members, `BoundaryState`, `SunShareFrame`, `SunUntrackedReason`, `AdditiveGain` and the sun-share/source-gain transform doubles in `linear_material_live_fixture.cpp` |
| `test_motion_hdr_scene` | header now names `IDirect3DResource9`, `D3DFMT_G32R32F`/`D3DFMT_R32F`; two new out-of-line symbols | extended `motion_hdr_scene_stubs/d3d9.h`; added `LinearEmissionPass::coverage_valid` and `publish_shadow_replay_candidates` stubs |
| `test_motion_wrap_states.test_production_transaction` | same sun-lane members reached the wrap-state seam | added the inert lane members and `blend_shadow_requested` to `motion_wrap_state_fixture.cpp` |
| `test_linear_cutout_contract` | 7f23195 records the cutout verdict in `cutout_ok` | assertion updated to `... && (cutout_ok = cutout_draw_state())` |
| `test_linear_emission_live` | 155ac54 made the sun lane a second reason to force the readback | assertion updated to `config.force_taa_readback = sunlane || (emissions && !emission_bench);` |
| `test_motion_wrap_states.test_route_wiring` | c40ee94 turned the branch into a block and appended `prepare_screen_additive` | assertion matches the block and pins the composition-before-additive order |
| `test_linear_distance_fade` | not golden bytes: the test compiles a frozen baseline from a pinned commit. 5b3b5c3 (PS3 constant-read port repair, reviewed, with GPU parity evidence in `linear-material-constant-port.md`) intentionally stages the sanitizer constant through a `mov`, so 104 pixel programs diverge from the 2cf65ae pin | `BASELINE` pin moved 2cf65ae → 5b3b5c3; 137 originals now byte-exact across three configurations. The repair delta itself stays proved against pre-repair 8722072 by `test_linear_material_constant_port.py` |
| `test_linear_emission_fused_report` | runner drift from 3ded947: `run_linear_emission.py` hashed `ps_*-source-<g>.bin` in every MRT mode, but `linear_emission_fixture.cpp` only writes them under `--source-gain` | `source_gain_sha256` moved inside the `--source-gain` branch |
| `test_linear_emission_sm1_transformer` | not a missing corpus (751 programs are present): `linear_emission_sm1_structure.cpp` asserted mode 5 is refused, but c40ee94 made 5 `AdditiveGain` | invalid-mode set changed to `{0,6,-1}` |
