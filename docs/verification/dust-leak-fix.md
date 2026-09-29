# Dust-scene leak fix: verification ledger

Feature: `--dust-leak-fix on|off` / `X3M_DUST_LEAK_FIX` / config key `dust_leak_fix`, `src/proxy/dust_leak_fix.cpp`,
site header `src/proxy/dust_leak_fix_sites.h`. The engine's dust-scene fill `0x0041efc0` allocates a scene node before
it validates the dust body and drops the node on three failure branches, so a sector whose background names missing
dust bodies leaks `NumDustInstances` nodes per frame on the render manager's unattached list until the next load
([object-lifetimes.md](../reverse-engineering/object-lifetimes.md), "Run383" and its "Patch" subsection). `on` claims
`SUB dword [ESP+0x20],1` at `0x0041f4d1` (the loop's common tail) through `engine_patch` and puts a 45-byte stub in front
of the tail: a never-attached node (EDI ≠ 0, node+0x1c = 0) is released with the engine's own node release
`0x00487be0` (what the render-manager clear runs per unattached node) and the loop ends for that frame. The DLL default
is `on` (unset = patched); the launcher sends `on` on every modded launch and nothing under `--vanilla`.

**Outcome (2026-09-29).** Built and verified on the host and in the X3 bottle; not flown. Site verifier PASS
(27 checks), Wine fixture 80/80 checks and 36/36 executed cases, DLL build 0 warnings, host suite 276 modules /
2,912 tests / 0 failing. The in-game check is a `--perf` flight in a sector on Mayhem background 103: `dust_leak_fix
site=0041f4d1 status=patched`, then `dust_leak_fix hits=300` per 300-frame row and a flat `engine_nodes` in the
`scene_graph_census` rows (Run383 showed +8,100 per 300 frames without the patch).

| Date | Check | Command | Result |
| --- | --- | --- | --- |
| 2026-09-29 | Site qualification on the installed EXE (structural identity; hash INFO). The 37-byte window `0x0041f4b7..0x0041f4db` (10 whole instructions), the whole function `0x0041efc0..0x0041f65b` (473 instructions, `ret 8` at `0x0041f658`), the site as one whole `SUB` without a relative branch, the JNE after it to `0x0041f328`, exactly the four branch sources of the site (`0x0041f336`, `0x0041f3d1`, `0x0041f436`, `0x0041f447`) as direct branches and as raw encodings, no other raw branch or dword into the window, atomic word (`0x0041f4d0`), the patched span as a five-byte `jmp` with the JNE untouched, the stub decoded as the documented 16 instructions, the loop exit `0x0041f4dc..0x0041f658` free of `[ESP+0x20]` aliases (offsets seen: `0x3c 0x40 0x60 0x64 0x68`) with the epilogue unchanged, the release routine's prologue/epilogue/unlink/Remove call, the clear's per-node call, the constructor's list append and the attach's `+0x18`/`+0x1c` writes, no other DLL claim in the window (157 other claims, 0 in the function), header constants | `python3 verification/probe/verify_dust_leak_fix_site.py` (output kept: [`verify_dust_leak_fix_site_out.json`](../../verification/results/dust-leak-fix/verify_dust_leak_fix_site_out.json)) | PASS, 27/27 checks, EXE SHA-256 `fdbf3418…` (measured) |
| 2026-09-29 | Wine fixture on real image pages. `verification/probe/dust_leak_fix_patch_fixture.cpp` links the unchanged `dust_leak_fix.cpp` (read_code/store_pointer/restore through a force-included fault seam) and `engine_patch.cpp`, with the engine's window bytes at `0x0041f4b7` in an image section (MEM_IMAGE, `PAGE_EXECUTE_READ`), the fixture's loop head at `0x0041f328` and exit at `0x0041f4dc`, and a jump at `0x00487be0` to a stand-in release that records its argument and registers, unlinks the node from an `R+0x28`-shaped list, zeroes the block and clobbers EAX/ECX/EDX. Six cases (all failed, all attached, null/attached/failed, attached/failed/attached, one failed, null only) run vanilla, patched, restored, after the rollbacks, with the jump live tail-only and after the late window: vanilla loops to the count with no release; patched ends after the first never-attached node with exactly that node released, unlinked and zeroed, `[ESP+0x20]` = 0, EDI = that node, EAX/ECX/EDX/EBX/ESI/EBP/ESP as at entry, ZF set; cases without a failure are identical patched and vanilla. The release saw EBX/ESI/EBP unchanged, EDI = the node and ESP = site ESP − 20. Refusals (`off`, `On`, too long, executable mismatch, changed window byte, foreign jump, invalid site), install through `initialize()` with the variable unset (row `status=patched reason=ok mode=on setting=- write=atomic`), chain check (jump → dispatcher → stub with its call rel32 to `0x00487be0`, counter and slot → tail `SUB; jmp 0x0041f4d6`), read/store counts (2/1/0), hits counter 4 = detours and the rows `dust_leak_fix hits=4 total=4 frame=300` then `hits=0 total=4 frame=600`, second install refused, restore row and original bytes, rollback on a failed chain link and on a failed read-back (original bytes), a failed take-back (registered, tail-only vanilla behaviour, restore_failed then restored), restore ownership (foreign bytes untouched, unreadable, already original), a read-only view (`protect_failed`, untouched), the late window; LastError preserved around every initialize/shutdown | `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_dust_leak_fix_patch.py` → [`bottle-X3/dust-leak-fix-patch.json`](../../verification/results/bottle-X3/dust-leak-fix-patch.json) | PASS: 80/80 checks, 36/36 cases, exit 0, 8.2 s; install 527.7 us, restore 585.8 us (fixture-inclusive one-off costs); arena 560 B used after the fixture's five installs (measured) |
| 2026-09-29 | Host tests: `test_dust_leak_fix_site` (the header compiled on the host: bytes, 37 single-byte refusals, the strict parser, the default, the stub encoder against the Python twin; the log parsers; capture/loader/CMake wiring; the installed EXE through the verifier and a patched copy failing it; the launcher option: default `on`, explicit, dropped under `--vanilla`, refused with `--vanilla`), `test_config_schema` (generated views current, bare DLL = launcher default), `test_launcher_defaults` (`X3M_DUST_LEAK_FIX=on` in the promoted set, `--dust-leak-fix off` opt-out), `test_logging_tiers` (`dust_leak_fix.cpp` among the DllMain restore-row writers), the neighbouring site tests with the new claim in their other-claims set | `PYTHONPATH=verification/probe:verification/analysis python3 -m unittest test_config_schema test_launcher_defaults test_logging_tiers test_dust_leak_fix_site test_game_phase_sites test_sun_flare_fix_site test_terran_lod_site test_lod_occlusion_site`; `python3 tools/config/generate.py --check`; `/usr/bin/python3 verification/probe/run_host_suite.py` | 93 tests OK; PASS 244 settings, 96 in the template; 276 modules, 2,912 tests, 0 failing, 99.6 s (measured) |
| 2026-09-29 | DLL build (worktree) and the no-x87 walk | `cmake --build build/dll -j8`; `python3 verification/probe/check_no_x87.py build/dll/d3d9.dll` | 0 warnings; PASS, 734 reachable functions, 0 violations (measured) |

**Cost.** Per loop iteration the stub adds `test edi,edi`, one compare of node+0x1c and the indirect jump
(at most `NumDustInstances` iterations per frame); on a failure one call of the engine's release (unlink, Remove,
memset, free) replaces a leaked 0x270-byte allocation, and the fill ends for the frame. Only when every body of the
background is missing does that replace allocating and dropping the remaining nodes; on a background with some
bodies present, vanilla keeps filling in the same frame and attaches later good picks, while the patch ends the fill
at the first failure, so the dust count builds up over several frames instead of one (accepted). Nothing runs on the
proxy side per frame; the 300-frame row reads one arena word.
Arena use is about 80 B (stub, slot, counter, tail). All inferred from the code; the in-game timing is the flight's.

**Not covered.** Native Windows execution: the source uses only documented Win32 (`VirtualProtect`,
`FlushInstructionCache`, `ReadProcessMemory`, `GetEnvironmentVariableW`, `WriteFile`) and cross-compiles, but it has
not been run on Windows. The engine's release routine itself was not executed in the fixture (the stand-in reproduces
its unlink; the routine's behaviour on a never-bound node is established from the disassembly in the note). A thread
executing the site during the write was not exercised; the single aligned 8-byte store is the argument (inferred).
LastError liveness at the site is inferred from the loop's own allocator calls, not from a reader search of the whole
caller chain. Not flown.

## Flight: Run 109 A (run384, 2026-09-29)

Measured from `/tmp/x3-bottleX3-run384/session-20260929-170727-212.log` (script and summary under
[`verification/results/run384-dust-leak-fix/`](../../verification/results/run384-dust-leak-fix/)): `dust_leak_fix
site=0041f4d1 status=patched reason=ok mode=on setting=on write=atomic`; `hits=300` in 62 of 67 300-frame rows (18,877
hits in all; the first sector row is partial and the menu rows are 0); `scene_graph_census engine_nodes` 4,515 at the
first sector row, 4,853 at the last, maximum 5,477 over 19,826 frames (run383: +27 per frame, 150,562 at frame 8100);
`registry_live == engine_nodes` on all 63 sector rows; `loop_phases cutevent_p50_us` 248 -> 140 (run383: 555 -> 10,836).
Frame time p50 16 ms (frames 1000-4000) -> 23 ms (last 3,000 frames) with `input_p50_us` 4,623 -> 7,155: not the node
walk; unexplained, the view and the sector's activity changed during the stay. The user reports it working; no shimmer
return after the stay. Accepted; the patch stays on by default.
