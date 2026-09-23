# Music keep and trace: verification ledger

Feature: `--music-keep` / `X3M_MUSIC_KEEP=1` and `--music-trace` / `X3M_MUSIC_TRACE=1`, `src/proxy/music_keep.cpp`
([music-restart.md](../reverse-engineering/music-restart.md) sections 4 and 6). Keep: Patch A, a six-byte trampoline
inside the stop-all `0x004982b0` at `0x004982db` (caller-keyed: save and pause keep the music graph running, alt-tab marks
the track), and Patch C, the play routine's seek call `0x00498d54` -> `0x004d0430` redirected (same track, start 0:
no seek). Trace: entry trampolines at `0x004982b0`, `0x00498c90`, `0x00498810`. The DLL's fallback is off; nothing is
written unless the variable is exactly `1`, the executable hash matches and every window, callee head and caller table
verifies; the launcher default is off this round (the same-id replay after the stop-all is unverified).

| Date | Check | Command | Result |
| --- | --- | --- | --- |
| 2026-09-23 | Site verifier of the RE note on the installed EXE (`fdbf3418…`): 38 patterns, 8 table entries, 4 commands, caller census, no literal refs, no raw branch into the six candidate sites | `python3 verification/results/music-restart/verify_music_restart_sites.py` | PASS |
| 2026-09-23 | Writes verifier: every window and constant of `music_keep_core.h` against the EXE (11 windows, 54 constants), the five claim sites decoded as whole instructions ([3,3], [5], [1,1,4], [6]) and modelled as written (`e9 rel32` + untouched sixth byte; `e8 rel32` at the call), the run call target, the `0x0049885f` pause call, the 6 stop-all / 3 play / 3 `0x00498810` callers (E8 target, return address = call + 5), no raw branch into any site interior (one operand-byte hit `0x00498c6f` rejected), direct-caller census of six functions, no literal refs | `python3 verification/results/music-restart/verify_music_keep_writes.py` (output beside it in `verify_writes_output.json`) | PASS, 39/39 checks |
| 2026-09-23 | Host tests: verifier on the installed image and on 25 patched copies (each window, each claim site, the call target, the pause body, the callers, an injected interior branch per site, an extra caller, the hash: all refused), source constants and the three caller tables, the decision logic compiled from the core header on the host (24 checks: preserve/vanilla callers, non-music never held, load drops holds, hold capacity, skip on hold or playing flag, positioned play vanilla, other-media no match, other id pauses a linked/unclaimed/not-playing keep_running hold and only that, paused hold never paused again, non-music play leaves holds), the assembly stubs' shape (saves first, restores before every exit, ESP restored, no x87/SSE, the `[esp+0x20]` / `[esp+0x14]` slots), line parsers and `restart_pairs`, production wiring, launcher gates (default off, `--music-keep`, `--music-trace`, both, `--vanilla` refused, inherited values dropped) | `PYTHONPATH=verification/probe /usr/bin/python3 -m unittest verification.analysis.test_music_keep` | 10 tests OK |
| 2026-09-23 | Scratch build and the no-x87 walk (the five stubs and five handlers added as roots) | `cmake -S . -B build-music-scratch -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPython3_EXECUTABLE=/usr/bin/python3 && cmake --build build-music-scratch -j8 --target d3d9`; `/usr/bin/python3 verification/probe/check_no_x87.py build-music-scratch/d3d9.dll` | 0 warnings; PASS, 110 roots, 650 reachable functions, 0 violations; the five stubs disassembled from the DLL match the source (worktree build `86346cb6…`, deleted, not a candidate) |
| 2026-09-23 | Collide modules whose claim collector now sees the new `*_site_va` constants | `PYTHONPATH=verification/probe /usr/bin/python3 -m unittest verification.analysis.test_collide_box_cull verification.analysis.test_collide_narrow_census verification.analysis.test_collide_sat_sse2` | OK (no overlap with the collide windows) |
| 2026-09-23 | Launcher dry runs | `test_music_keep.MusicLaunchOptions` (`--dry-run` only) | modded launch carries nothing by default; `--music-keep` / `--music-trace` add exactly their variable; with `--vanilla` the parser refuses both (exit 2) |
| 2026-09-23 | Review round 1 (F1 DirectSound-path records paused under save/pause; F2 orphaned keep_running holds paused at any stop-all through a shared entry stub that loops on `0x004d1810`; F3 walk limit 4096 with a `music_walk_cut` line; F4 the id's holds dropped at `MOV_StopMovie` through a shared entry stub; F5 the A window from `0x004982c0`, 38 bytes, pins the pushes behind `[esp+0x10]`; F8 `--vanilla` refusal; F9 `<cstring>` in the harness; G2 write kinds and G4 the manager's re-seek recorded in the note; G3 `keep_armed_`/`trace_armed_` checked by every handler, set only by a complete install) rebased onto `main` `70cd9d92` (pause-key-only merged: three include/CMake conflicts resolved) | `PYTHONPATH=verification/probe /usr/bin/python3 -m unittest verification.analysis.test_music_keep`; `python3 verification/results/music-restart/verify_music_keep_writes.py`; scratch build + `check_no_x87.py` | 10 tests OK (harness 33 checks, 25 patched copies refused, 7 stubs); verifier PASS 39/39; build 0 warnings; audit PASS, 114 roots, 657 reachable, 0 violations; the entry loop stub disassembled from the DLL (`e903f0fb…`, deleted) matches the source |

## Open

- Not flown. First flight is trace-only (`--music-trace`): one alt-tab, one save, one pause; the pairs `stop -> next play`
  must show the same id with `start_ms=0` (section 6 of the note). If the script plays another id or nothing, the keep's
  same-id assumption fails and `--music-keep` stays off.
- Second thing the trace flight must settle (G1, note section 6 "Orphans"): a `music_trace_stop name=save|pause` with no
  following same-id `music_trace_play` means a keep_running track would play on unserviced by the manager (no end poll,
  no loop re-seek) until the next stop-all pauses it (`music_keep_orphan`); leaving flag 2 set instead is not taken
  because the stop-all's completion and context clearing still run (a second, ctx-0 completion at the natural end).
- Third flight (after the trace settles both questions) with `--music-keep --music-trace`: `music_keep_seek action=skip` after each of the three, the music audibly
  continuing at its position; a sector change must still switch tracks (`action=vanilla` or `pause_then_vanilla`).
- The DirectSound path (`0x40` records): `0x004d1870` re-arms `[m+0x48]`/`[m+0x64]` on the skip path; effect unverified
  (note section 5).
- The hold table assumes the stop-all and the play run on one thread (the main loop dispatches the WndProc); a
  cross-thread session would show as interleaved `seq=` values, never a crash path.
- No Wine fixture exercises the stubs; the evidence is the byte model, the host decision tests, the DLL disassembly and
  the flight.

## Run 72 A trace flight (run270, 2026-09-23)

`--music-trace` only (keep off). 41 lines, cap not hit (measured; `verification/results/run270-defaults/`).
Stop-all callers: session_start 7, load 2, alt-tab 12, pause 3, save 0. Alt-tab: 7 of 7 candidate pairs show
stop-all(0x004d36c2 return) followed by a play of the same id at start 0 in the same frame (3–7 ms later); the
other 5 alt-tab stops happened inside a paused state and had no replay. Pause: 1 of 3 replayed the same id
(17 ms later), 2 had no replay. Save: no stop-all row with the save caller exists (either no save was made
while armed or the save path does not reach the hook). Sector change: a new id starts without a stop/replay pair.
Verdict: the same-id replay assumption holds for alt-tab; `--music-keep` may fly (Run 72 B) with the trace
kept on; the save case remains to be traced (the user saves at a known time).

## Run 72 B (run271, 2026-09-23): --music-keep flown

Evidence `verification/results/run271-music-keep/` (measured). The sector music record is on the DirectSound path
(flags 0xd2, bit 0x40), so the keep's rule sent every caller (alt-tab 11, pause 5, save 2) to `mode=paused`; all 18
stops were followed by a same-id replay whose seek was skipped (`music_keep_seek skip` 18, vanilla 11 for new ids).
Saves (both taken while docked, 5.75 s each) kept the track position and did not restart it. Pause pauses the music
(the loop blocks; the user accepts). Alt-tab: the user hears a ~1 s interruption and a ~0.5 s rewind; the replay
follows the stop by 1.7–6.4 ms and frames keep presenting while inactive (10 of 24 focus samples inactive with
146 frames presented; music-restart.md §2's "loop sits in GetMessage" is wrong), so the gap is the DirectSound
re-arm in the pump 0x4d0700 (buffer stopped, cursors reset, SetCurrentPosition(0), Play only after the next 2 s
sample decodes; inferred from the disassembly), not the inactive time; the rewind is unexplained. `music_trace`
did not install (`bytes_mismatch`: the keep had already patched the two shared entry sites), so no trace rows exist.
Next: for the alt-tab caller skip the whole per-record stop (keep flag 2 set, no status 1) so the pump keeps
servicing the buffer and nothing re-arms; fix the shared-site claim so trace and keep coexist; correct the note.
