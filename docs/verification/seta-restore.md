# SETA restore: verification ledger

Feature: `--seta-restore` / `X3M_SETA_RESTORE=1` (schema default `1`, `[engine]`), `src/proxy/seta_restore.cpp`,
`src/proxy/seta_restore_core.h`, the shared injection consumer in `src/proxy/chase_transition.cpp`
([architecture](../architecture/seta-restore.md); mechanism:
[seta-script-toggle.md](../reverse-engineering/seta-script-toggle.md) section 3 (i)). A record-only trampoline at
`0x004b148c` arms on the gate's `StopFastForward` (old > 1.0, new 1.0, IP `1558d`, return `15ef8`, story build
1462141839). The chase restore's injection site `0x00410100`, claimed once for both consumers, then calls
`0x0049f4c0(0, 0, 0x96, "StartFastForward", 1, 0)` two presented frames after the destination is Ready.

| Date | Check | Command | Result |
| --- | --- | --- | --- |
| 2026-10-23 | Site verifier: the ten byte windows of the core header against the installed `X3AP.exe` (`fdbf3418…`); the hook span as two whole instructions ending at `0x004b1494`, no relative control; case 9 ends in `ret 0x10` with EAX/ECX/EDX written first and no flag consumer; the dispatcher bounds 16 cases and the table entry 9 is the only one naming the hook; conservative byte-pattern scan of `.text` and a dword scan of every section: nothing points into `0x004b148d..0x004b1493`; the wrapper `0x0049f4c0` (resolve `0x0049f330`, run `0x0049f430`, plain `ret`, `AL = 0` exit) and the EXE's own `StopFastForward` call shape; `0x0049f570` = the same marshalling plus a post-run task lookup and the memory-write-free scan `0x004a21a0`; call sites 56 (`0x0049f4c0`) and 46 (`0x0049f570`); the four globals anchored; the story constants against `seta_toggle_study.txt` | `python3 -I verification/results/seta-script-toggle/verify_seta_restore_sites.py` | PASS, 50 checks, 0 failures |
| 2026-10-23 | Host tests: the state machine and return scan from the core header compiled on the host (75 checks). Covered: the arm and twelve non-arm write shapes; the foreign build refused once and latched; unreadable player/sector/id refused; one arm per transit and re-arm after a VM clear; cancels: factor write in armed and due, VM clear/load, epoch, wall and frame expiry, ship change, unreadable player, sector left, unwound call, VM load during the call; the injection sequence (one walk per presented frame, pause defers, ship not yet parented, Ready, two frames, another controlled object skipped, the call, a write during the call ignored, restored with `frames_after_transit=2`, AL 0 reported); the scan (first record, null and dead contexts skipped, bottom, order, truncated, five unreadable shapes). Also: the launcher flag on `--dry-run` runs (default `1`, `--seta-restore`, `--no-seta-restore` absent, stale shell values ignored, absent under `--vanilla` and refused with it, player mode empty opt-out); the schema entry and generated views; the read site; the wiring (every window compared before the claim, claim before the attach, hook rollback, no-x87 roots, CMake, capture calls, one `restore_sites_install`) | `PYTHONPATH=verification/probe /usr/bin/python3 -m unittest verification.analysis.test_seta_restore verification.analysis.test_config_schema verification.analysis.test_launcher_defaults` | 37 tests OK (8 new; `EXPECTED_EMPTY` and the two opt-out tables gained `X3M_SETA_RESTORE`) |
| 2026-10-23 | Cross-build and the no-x87 walk (`_x3m_seta_restore_ti_stub`, `_x3m_seta_restore_factor_write` added as roots) | `cmake -S . -B build-seta -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPython3_EXECUTABLE=/usr/bin/python3 && cmake --build build-seta -j8 --target d3d9`; `/usr/bin/python3 verification/probe/check_no_x87.py build-seta/d3d9.dll` | fresh configure and 118 objects: 0 warnings; PASS, 145 roots, 1018 reachable functions, 0 violations (worktree build `a09d28a4…`, 62,108,122 B: not a candidate) |
| 2026-10-23 | Chase restore after the injection site became shared (prefilter bit per consumer, `inject_acquire` once, `script_call` with a target) | `PYTHONPATH=verification/probe:verification/analysis /usr/bin/python3 -m unittest verification.analysis.test_chase_transition verification.analysis.test_chase_restore_sites verification.analysis.test_chase_camera verification.analysis.test_music_keep`; `PYTHONPATH=verification/probe python3 verification/probe/verify_chase_restore_sites.py --exe /mnt/c/X3/X3AP.exe` | OK; site verifier PASS (13 wrapper calls inside the input routine, no branch into a span) |
| 2026-10-23 | Review round 1. (1) Expiry is wall time only, 60 s: the 3600-frame bound came off because it depends on the frame rate (30 s at 120 fps). (2) An unreadable VM build id at a SETA stop is not this write: one `refused reason=build_unreadable` row, not latched; only a readable foreign build latches `story_build`. (3) AL = 0 logs `restore_failed result=0` instead of `restored`. (4) Ledger notes below | verifier, the unittest command, fresh configure and build in `build-seta`, `check_no_x87.py` (the commands above) | verifier PASS, 50 checks; 37 tests OK (host harness 80 checks: frames alone never expire, wall expiry only, build unreadable refused once and not latched, a later readable stop arms, AL 0 gives `restore_failed`); 118 objects, 0 warnings; no-x87 PASS, 145 roots, 1018 reachable, 0 violations (worktree build `e0ec310e…`, 62,108,134 B, not a candidate) |

## Open

- Unflown. Expected rows for one gate with SETA on: `seta_restore installed=1 … inject=ok`; `armed … ip=0x1558d
  ret=0x15ef8 ret_name=gate`; `restored frames_after_transit=2 … result=1`; then SETA ramps audibly (sounds 980, 982).
  `ms_since_arm` on the restored row is the first measurement of the warp-in plus stall wall time (note section 5).
- Flight risk, reviewer note 7: any factor write between the arm and the call cancels the ticket. If the warp-in scene,
  the fade or the arrival writes the factor (a cut-scene `621::CutEvent`, speech with a duration), every transit
  cancels. The row that shows it is `seta_restore cancelled reason=factor_write`, with `write_ip`/`write_ret`
  (`write_ret_name=other`, or a named stop) identifying the writer, between the `armed` row and the arrival.
- Reviewer note 6: with `X3M_SETA_RESTORE=1` (the default) this feature claims the chase restore's four shared sites on
  every default launch: injection `0x00410100`, VM clear `0x0049ea80`, VM load `0x004a0880`, EH adapter `0x0052f298`.
  That holds even with `--camera vanilla` or `--no-chase-view-restore`, where they were unclaimed before. Idle cost: one
  prefilter compare per main-loop iteration at the injection site, and a `PreserveCpuState` callback at each VM
  clear/load and EH-adapter pass. `--no-seta-restore` returns to the previous claim set.
- `restore_failed result=0` would mean `0x0049f4c0` did not resolve `"StartFastForward"` for key `0x96`; the ticket
  ends and SETA stays off.
- Not exercised: the story-build refusal on stock AP (`addon/04.cat`, build 1510658513); `0x0049f4c0` resolving the
  mod-supplied name for key `0x96` (the same resolver `0x0049f330` resolves the chase restore's `SelectMode`).
- Native Windows and CrossOver: same source and offsets, documented Win32 only; neither verified in flight.
