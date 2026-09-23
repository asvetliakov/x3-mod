# Pause key only: verification ledger

Feature: `--pause-key-only` (launcher default on modded launches) / `X3M_PAUSE_KEY_ONLY=1`, key `--pause-key CODE` /
`X3M_PAUSE_KEY` (default `0x1b5`, DIK_PAUSE), `src/proxy/pause_key_only.cpp` and `pause_key_only_core.h`. A 12-byte
in-place rewrite of the flight pause's key exit at `0x004043a5` so only the pause key or a mouse button ends the pause
([pause-dialog-input.md](../reverse-engineering/pause-dialog-input.md) §4.1 and "Implementation"). Nothing is written
unless the variable is exactly `1`, the key parses, the executable hash matches, the install window is open and the
79-byte window at `0x004043a0` matches. Commands: [commands.txt](../../verification/results/pause-dialog-input/commands.txt).

| Date | Check | Command | Result |
| --- | --- | --- | --- |
| 2026-09-23 | Site verifier extended with the DLL bytes: the production core compiled on the host; its 79-byte window equals the installed image (`fdbf3418…`), its original bytes and VAs equal the note, the default-key replacement equals the note's `66 81 fe b5 01 75 05 66 39 de 75 27` and its Python twin, and decodes at `0x004043a5` to `cmp si,0x1b5; jne 0x4043b1; cmp si,bx; jne 0x4043d8` with boundaries `a5/aa/ac/af`; the earlier checks unchanged (17/17 sites, 0 raw branch encodings into the span, 0 dword references, single bit-0 clear) | `python3 verification/results/pause-dialog-input/verify_pause_sites.py --json verification/results/pause-dialog-input/verify_pause_sites.json` | PASS; `--key 0x2c` also decodes to `cmp si,0x2c` with the same targets; `--key 0x2000` refused (`bad_key`, FAIL) |
| 2026-09-23 | Host tests: core compiled (default and custom key bytes, five changed-window variants and an already patched window refused `bytes_mismatch`, keys 0/0x1000/0x2000/0xffff/0x101b5 refused `bad_key`, range ends, narrow and wide `X3M_PAUSE_KEY` parser incl. 15 rejected spellings), C++/Python byte parity, the verifier on the installed image and on a copy already carrying the patch (FAIL), install-line parser, production wiring (initialize after the game-phase claims, `shutdown()` on `FreeLibrary`, CMake source, LastError, rollback states), launcher dry runs: modded default, stale inherited key dropped, `--no-pause-key-only` drops both, `--vanilla` forwards nothing (also with `--no-pause-key-only`), `--pause-key 0x2c`/`437`/`0x11b5`, refusals of `--pause-key-only`, `--pause-key` and both under `--vanilla` (the proxy is not loaded there), of `--pause-key` with `--no-pause-key-only`, and of `0`, `0x1000`, `0x2000`, `pause`; launch command unchanged | `PYTHONPATH=verification/probe /usr/bin/python3 -m unittest verification.analysis.test_pause_key_only verification.analysis.test_collide_box_cull` | 19 tests OK (9 pause-key-only, 10 collide box cull), 8.0 s; after review round 1 (F1 vanilla refusal, F2 read-back rollback, F3 non-zero low 12 bits): see the next row |
| 2026-09-23 | Review round 1 rerun | `PYTHONPATH=verification/probe /usr/bin/python3 -m unittest verification.analysis.test_pause_key_only`; the verifier as above | 9 tests OK (4.2 s); after the second reviewer's items (`rollback_unprotected`, `key()` 0 once the bytes are back) 19 tests OK with test_collide_box_cull (6.6 s); verifier PASS; `--key 0x1000` refused (`bad_key`, FAIL); `pause_key_only.cpp` cross-compiles `-std=c++17 -Wall -Wextra -Werror` (syntax only, no DLL rebuilt) |
| 2026-09-23 | Launcher-default regression: every host module that runs a `manage.py launch --dry-run` (the new variable is on every modded launch) | same `PYTHONPATH`, 30 modules (list in commands.txt) | 418 tests OK, 1 skipped |
| 2026-09-23 | Scratch DLL build (MinGW i686 GCC 16.2.0) and the no-x87 walk; the module has no floating point and no engine-called entry point | `cmake -S . -B build-pause-scratch -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPython3_EXECUTABLE=/usr/bin/python3 && cmake --build build-pause-scratch -j8 --target d3d9`; `/usr/bin/python3 verification/probe/check_no_x87.py build-pause-scratch/d3d9.dll` | build OK, 0 warnings; PASS, 100 roots, 639 reachable functions, 0 violations; sha256 `887f3766…` (worktree scratch, deleted, not a candidate) |

## Open

- Not flown and no Wine fixture: the in-process install (protect, write, read-back, rollback, `late_claim`, LastError)
  follows `point_light_admission`, whose X3 CPU fixture covers that shape, but this module has no fixture of its own.
- First flight: pause with Pause, then press other keys, Esc, Alt/Command and Cmd-Tab away and back; the pause must hold
  until Pause or a click. Read the `pause_key_only` line (`patched=1 reason=ok write=plain`). Two expected corner
  cases, not failures: (1) with Pause held since before the pause, tapping Shift (press, release) unpauses, because
  `cmp si,bx` compares all 16 bits and the read goes `0x1b5` → `0x11b5` → `0x1b5` (accepted, note §4.1 risk e);
  (2) with three or more other keys held, the engine's 3-slot key ring (filled every poll with every held key) can
  starve the Pause key so only a click unpauses; this is engine-inherent, vanilla has the same ring (inferred from
  the note's §2.2, not observed). Open from the note: a
  reactivation click can still unpause if DirectInput reports its button-down after `Acquire` (§4.4); Shift+Pause no
  longer unpauses; without DirectInput keyboard only a click unpauses (§4.1 risks).
- Native Windows: documented Win32 only (`VirtualProtect`, `FlushInstructionCache`, `ReadProcessMemory`,
  `GetEnvironmentVariableW`) and an EXE byte patch keyed to the hash; source and cross-compilation only, not verified
  natively.
