# Releases: verification ledger

Player zips built by `tools/release/release.py` (steps in its docstring and in
[config-file.md](../architecture/config-file.md)). Earlier releases are recorded only in their records:
[0.8.0](../../verification/results/release-0.8.0.json), [0.9.0](../../verification/results/release-0.9.0.json).

## Release 1.3.0

2026-10-16, `python3 tools/release/release.py --out /tmp/x3m-release-1.3.0 --regenerate-dir /tmp/x3m-release-1.3.0-regenerate` (dry run, then the real run: exit 0, 75.2 s wall: dll 72.6 s, package 2.5 s) on the WSL2 host (same toolchain as 1.2.0/1.2.1; binutils 2.47). Clean tree at `0bb4facc` (the version bump on top of `a2afcadb`, Run 182 A accepted). Record [release-1.3.0.json](../../verification/results/release-1.3.0.json).

- Over 1.2.1 (Runs 151-182, 172 commits): **AI ship steering, predictive avoidance and convoy gate transit** on by default (`ship_steering`, `ship_avoidance`, `ship_steering_gates*`, the departure lane `ship_steering_gates_lane`; arrivals that turn back are the game's; [design](../architecture/convoy-gate-transit.md), [ledger](ship-steering.md)); **engine trail ribbons** anchored inside the plume, none on the own ship (`engine_ribbons_own_ship` default 0); **TAA on bright hull detail**: the far stabiliser band default `0.985,0,30,34,0.03,0.25` (lock eligibility from ~38.4k view units), and the hull emissive widening of lit hull windows now also on alpha-tested hull parts (unwidened alpha fetch); **native Windows D3D9 fixes**: three generated programs the runtime validator refused since the host move (the hull widening block and its sun-shadow sibling, the bolt far-flag variant, the occlusion cull vertex shader; [hull ledger](hull-emissive-widening.md), [screen emission](screen-emission.md), [occlusion](occlusion-cull.md)) and the native validator harness + host guards (one constant read per instruction, no oC gap); diagnostics (held-by-reason widening row, ship_trace) and the 2026-10-16 cleanup.
- DLL: stripped `a5415ba3…` (42,274,188 B; 0 warnings, x87 0 / 1014 reachable, strip identity PASS, debuglink), unstripped `08c7d7a6…`, `d3d9.debug` 20,834,168 B. Installed into native `C:\X3` as Run183, bytes verified, rollback `rollback-d3d9\d3d9.dll.run182` ([install](../../verification/results/run183-release-install.json)). The user's `x3m.ini` reset to the 1.3.0 template with only `hull_lightmap_gain = 2` set (previous file `x3m.ini.run182`).
- Zip `x3m-1.3.0.zip` `3196459e…` (60,107,722 B): the same 22 entries in package.py's order; copied to `C:\Users\asvet\Downloads\`.
- `x3m-regenerate.exe` is the 1.1.0 binary reused (`bb3ba27b…`): `git diff --stat 72cbed8f..HEAD -- tools/regenerate tools/fog_field_recipe.py` is empty. The native Windows build path for the exe stays open.
- Not flown as a release build; the last flight is Run 182 A on the unstripped Run182 DLL from `7fc522a3` (the release differs by the version string only).

## Release 1.2.1

2026-10-12, `python3 tools/release/release.py --out /tmp/x3m-release-1.2.1 --regenerate-dir <dir with the 1.1.0 x3m-regenerate.exe>` (exit 0, 53.5 s wall: dll 51.3 s, package 2.2 s) on the WSL2 host (same toolchain as 1.2.0). Clean tree at `72cbed8f` (version bump `19826545` carried an unterminated string in the launcher help text; the release was rebuilt from the fix and the first build discarded). Record [release-1.2.1.json](../../verification/results/release-1.2.1.json).

- Over 1.2.0: the **engine-state chase view restore** ([design](../architecture/chase-view-restore-engine-state.md), four amendments): after a gate jump, jumpdrive or ship change the rear chase view is restored by two engine by-name script calls, `SelectMode(258)` on the main monitor and `UpdateVisibility()` on the monitor manager, both captured from the script task's own frames; no dependence on the compiled script program (the 1.2.0 ticket hard-coded the stock `x3story.obj` and never armed under Mayhem). Native runs: 146 A (never bound: script-published sector), 147 A (crashed: callback-slot argument), 148 A (7/7 restored; crosshair HUD hidden after a ship change), 149 A (4/4 restored with `hud_refresh=ok`, two ship changes and two sector changes, exception=0). New setting `chase_view_restore_retries` (0..5, default 3).
- DLL: stripped `5d54aa5d…` (40,185,821 B; 0 warnings, x87 0 / 925 reachable, strip identity PASS over 11 sections, 17 exports, debuglink). Installed into native `C:\X3` as Run150, bytes verified, rollback `rollback-d3d9\d3d9.dll.run149` ([install](../../verification/results/run150-release-install.json)).
- Zip `x3m-1.2.1.zip` `9ce5e2d1…` (59,883,683 B): the same 22 entries in package.py's order; copied to `C:\Users\asvet\Downloads\`.
- `x3m-regenerate.exe` is the 1.1.0 binary reused again (`bb3ba27b…`): `git diff --stat e97c4644..HEAD` over the bundled sources is empty. The native Windows build path for the exe stays open.

## Release 1.2.0

2026-10-11, `python3 tools/release/release.py --out /tmp/x3m-release-1.2.0 --regenerate-dir <dir with the 1.1.0 x3m-regenerate.exe>` (dry run, then the real run: exit 0, 54.7 s wall: dll 52.5 s, package 2.2 s), the first release built on the WSL2 host (Ubuntu 26.04, MinGW GCC 16-posix 16.2.0 from Ubuntu 26.10, binutils 2.45.90, cmake 4.2.3, Python 3.14.4 / NumPy 2.3.5; [platform note](../architecture/platform-portability.md) "Native install and build host"). Clean tree at `e97c4644` (version bump over Run144's `47e977dd`). Record [release-1.2.0.json](../../verification/results/release-1.2.0.json).

- Over 1.1.0: the dust_leak_fix native Windows crash fix (`ed00c53e`; counter out of the RX engine_patch arena, DLL pinned, fail-closed counter_in_arena check; Run 143 A: 7,017 hits, no exception), the fog card state gate admitting `SEPARATEALPHABLENDENABLE` 1 (`47e977dd`; the cockpit-view fog on/off flicker, Run 144 A: steady, 0 `gate:states` in 19,209 frames), the `media_cue` reset form GCC 13 accepts (`a5641bad`), the NumPy 2.x bake (`6bcb39df`, 14/14 pinned atlases reproduced).
- DLL: stripped `2b03a78a…` (40,170,162 B; 0 warnings, x87 0 / 925 reachable with the `.cold` siblings walked, strip identity PASS over 11 sections, 17 exports, debuglink to `d3d9.debug`). Installed into native `C:\X3` as Run145 at 20:48, bytes verified, rollback `x3m\rollback\d3d9.dll.run144` ([install](../../verification/results/run145-release-install.json)).
- Zip `x3m-1.2.0.zip` `aba7afbd…` (59,878,443 B): the same 22 entries as 1.1.0 in package.py's order; copied to `C:\Users\asvet\Downloads\`.
- `x3m-regenerate.exe` is the **1.1.0 binary reused** (`bb3ba27b…`, 29,766,304 B, taken from `C:\X3` where its hash matched the 1.1.0 record): no Wine/CrossOver bottle exists on this host to build it and Windows has no Python installed. Its bundled sources changed since 1.1.0 only in the NumPy version check (`fog_families.py`, `fog_field_recipe.py`, `regenerate/build.py`), which the bundled NumPy 2.0.2 passes either way; the DLL validates `fog-families.bin` by its packet hashes, not by the tool/recipe hash the JSON records (Run 144 A loaded the file the 1.1.0 exe wrote: `event=loaded families=70 packets=35 reason=ok`). The host `x3m-regenerate` binary was not rebuilt. Open: a native Windows build path for the exe (Windows Python 3.12 + PyInstaller, `tools/regenerate/build.py` docstring) before a release that changes the regenerate sources.

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
