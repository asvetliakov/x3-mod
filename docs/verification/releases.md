# Releases: verification ledger

Player zips built by `tools/release/release.py` (steps in its docstring and in
[config-file.md](../architecture/config-file.md)). Earlier releases are recorded only in their records:
[0.8.0](../../verification/results/release-0.8.0.json), [0.9.0](../../verification/results/release-0.9.0.json).

## Release 1.1.0

2026-10-09, `python3 tools/release/release.py --out /tmp/x3m-release-1.1.0` (dry run, then the real run: exit 0, 214.9 s wall: dll 55 s, regenerate host 58 s + Windows 100 s, package 2 s), [record](../../verification/results/release-1.1.0.json); the tool record and every artifact stay under `/tmp/x3m-release-1.1.0/`.

- Source: clean `80556bd7`, the version bump (CMake project version 1.1.0, regenerated x3m.ini template header and config_schema_inc.h) on top of `dd2115c1` (Run 140 B accepted); DLL inputs equal the Run140 candidate's (`cd479d02`) plus the version. Contents since 1.0.0 (Run131..Run140): the DXVK backend compatibility fixes (GetPrivateData not-found form, HDR meter readback double-buffered with a lock gate, capture band readback), engine hull light per nozzle plate with the 72-plate cap and the device slot cap, the engine light hold, node-sourced engine nozzles (`engine_nozzle_source`), the batched occlusion cull and its engine-side skip as opt-ins (`occlusion_cull` on | off | engine, default off), the final-Release vtable restore (C++ COM backends), fog/LOD unchanged.
- DLL: stripped `2d65b65f…` (39,937,177 B, shipped), unstripped `acad75c2…` (59,545,513 B), `d3d9.debug` `add1e16b…` (20,604,841 B). 0 build warnings; `check_no_x87` 0 violations on both; strip identity PASS (['.text', '.data', '.rdata', '.xdata', '.bss', '.edata', '.idata', '.tls', '.rsrc', '.reloc'] sections, 17 exports, ['.debug_aranges', '.debug_info', '.debug_abbrev', '.debug_line', '.debug_frame', '.debug_str', '.debug_line_str', '.debug_loclists', '.debug_rnglists'] debug sections removed, debuglink); `X3M_SOURCE_COMMIT` marker once.
- x3m-regenerate: host `1ef2e9dc…` (12,286,096 B, smoke test only), Windows `x3m-regenerate.exe` `bb3ba27b…` (29,766,304 B) built and smoke-tested in bottle X3M-Build through `wine_lock.py` (exit 0).
- Zip `x3m-1.1.0.zip` `f49b2621…` (59,800,944 B): the same 22 entries as 1.0.0 in package.py's order, CRC clean. x3m.ini (22,671 B) begins `; X3 Modern Renderer 1.1.0` and equals `assets/x3m.ini`; `generate --check` PASS (261 settings, 111 in the template).
- Fixture gates reused from the [Run140 candidate](../../verification/results/run140-candidate-build.json) (all 8 engine records bound at cdb7cf14; the version bump touches no DLL logic). Flights: Run 140 A/B (run22, run25, run26) accepted on the DXVK backend; wined3d last flown on Run 133 (run9).
- Installed into bottle X3 as Run141 at 03:55: the shipped stripped DLL `2d65b65f…` (39,937,177 B), bytes verified, EXE unchanged; rollback Run140 `1f702121…` (/tmp/x3-run140-candidate/build/d3d9.dll). Not verified: native Windows.

## Release 1.0.0

2026-10-04, `python3 tools/release/release.py --out /tmp/x3m-release-1.0.0` (dry run, then the real run: exit 0,
204.5 s), [record](../../verification/results/release-1.0.0.json); the tool record and every artifact stay
under `/tmp/x3m-release-1.0.0/`.

- Source: clean `c3cce57f`, the docs-only commit (status.md, user-runs.md) on top of the version bump `082e1d8c`;
  the DLL and regenerate inputs are those of `082e1d8c`. The stripped DLL carries `X3M_SOURCE_COMMIT=c3cce57f…`
  once and README.txt names the same commit.
- DLL: stripped `94d16fa8…` (39,856,549 B, shipped), unstripped `86ed43f4…` (59,097,196 B), `d3d9.debug`
  `e467b717…` (20,226,668 B). 0 build warnings; `check_no_x87` PASS, 0 violations, 766 reachable functions, on
  both DLLs; strip identity PASS (10 loaded sections, 17 exports, marker once, 9 debug sections removed,
  debuglink). No version resource (`.rsrc` holds RT_RCDATA only); the version is in x3m.ini, README.txt and the
  `x3-modern-renderer version=1.0` log row.
- x3m-regenerate: both binaries rebuilt (0.9.0 had reused 0.8.0's) because the tool gained the engine-bodies
  step. Host `38261796…` (12,286,224 B, smoke test only, not shipped): smoke PASS, exit 0 in 26.8 s.
  Windows `x3m-regenerate.exe` `4196d3e0…` (29,768,130 B), built and smoke-tested in bottle X3M-Build through
  `X3M_FIXTURE_BOTTLE=X3 wine_lock.py` (lock wait 0 s, child 84.3 s): smoke PASS, exit 0 in 19.6 s. Both
  smoke logs show `engine bodies summary: 1 bodies (red 1), 1 listed but not loadable` and `all done: fog
  families, LOD overlay and engine bodies regenerated`.
- Zip `x3m-1.0.0.zip` `dc130aad…` (59,764,939 B): the same 22 entries as 0.9.0 in package.py's order, CRC
  clean, DLL/exe/fonts re-hashed. x3m.ini (20,864 B) begins `; X3 Modern Renderer 1.0.0` and is byte-identical
  to `assets/x3m.ini`; `generate --check` PASS (256 settings, 107 in the template).
- Fixture gates reused from the [Run130 candidate](../../verification/results/run130-candidate-build.json)
  (`01114822`): `git diff --stat 01114822..082e1d8c -- src CMakeLists.txt tools assets` touches only
  CMakeLists.txt (VERSION) and assets/x3m.ini (version line). `manage.py launch --dry-run --direct --config
  --debug` exit 0 from the checkout; a dry run with the zip's DLL needs the install.
