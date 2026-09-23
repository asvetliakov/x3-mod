# Music keep and trace: verification ledger

Feature: `--music-keep` / `X3M_MUSIC_KEEP=1` and `--music-trace` / `X3M_MUSIC_TRACE=1`, `src/proxy/music_keep.cpp`
([music-restart.md](../reverse-engineering/music-restart.md) sections 4 and 6). Keep: Patch A, a six-byte trampoline
inside the stop-all `0x004982b0` at `0x004982db` (caller-keyed: save and pause keep the music graph running or paused and
held, alt-tab leaves a music record untouched), and Patch C, the play routine's seek call `0x00498d54` -> `0x004d0430` redirected (same track, start 0:
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
| 2026-09-23 | After Run 72 B: (1) alt-tab + music record → `skip_all`: the A stub leaves for `0x00498359` (the per-record loop's continue, target of the loop's own not-playing `je` at `0x004982d9`; EBP = next node, EBX = 0, four pushes deep; no new site), so no Pause/Stop, flag 2 and context kept, no completion, no replay, no hold; non-music under alt-tab vanilla; save/pause unchanged. (2) Shared entry sites installable in either order: window checks see through a live shared claim only while it holds exactly its own patch (`core::window_matches`); the claim protocol moved to `core::acquire_shared`/`release_shared`. (3) Note §2 loop behaviour, §4, §5 and §6 "Alt-tab" corrected | `PYTHONPATH=verification/probe /usr/bin/python3 -m unittest verification.analysis.test_music_keep`; `python3 verification/results/music-restart/verify_music_keep_writes.py`; scratch build `build-mk-55743` + `check_no_x87.py` | 10 tests OK (harness 57 check sites after the review fixes, was 33: alt-tab DS/DShow music → skip_all unheld, alt-tab non-music → vanilla, both install orders chained and restored only by the last release, foreign byte and changed byte behind a claim refused, failed push on a shared and a fresh claim; 27 patched copies refused incl. the new window and the continue check; A stub has three exits, the exact `cmp eax,1/je 1f`, `cmp eax,3/je 3f` dispatch and a `3:` block of pops, popfd and the `_next` jump, no EDI load there); review fixes: `overlay_claims` continues past a foreign claim and reports it, `window_matches` fails on that result; mutation script `verification/results/music-restart/mutate_music_keep_core.py` (output beside it): five mutations of the core (no see-through, overlay over a foreign write, no skip_all rule, no fresh-claim restore, release restoring early) fail 7/5/6/1/2 checks, PASS; verifier PASS 41/41; clean build 87 objects, 0 warnings; audit PASS, 658 reachable, 0 violations; the A stub's third exit disassembled from the DLL jumps through `_x3m_music_stop_all_next` (build deleted, not a candidate) |
| 2026-09-23 | Second review: (G1) Patch D, `claim_call` at `0x004983d9` (the status query `0x004d14e0`, its only caller) with a thunk that answers 1 = playing for a music record with flag 2 and a skip_all hold while `[0x00608adc]` is 0 and the RunInBackground bit `0x4000` of `[*0x00606f3c]` is clear (the query would answer "ended" without looking and the update would end, complete and restart the track), else forwards; skip_all holds are kept with their own kind, ignored by the seek rule and dropped by every music play; Patch D optional (a refusal logs `status_gate=<reason> alt_tab_mode=paused` and alt-tab takes the flown paused mode); Present samples the active flag and logs `music_keep_active` on each change; the handler's common path is three loads and no call (boundary moved to a noinline helper after the first build showed SjLj registration in the prologue). (G2) note §2/§6: the RunInBackground reading corrected (bit set: loop runs, query genuine; bit clear: pump blocks, but the frame after the drain runs with the flag at 0 and the query answers "ended"). (G3) rollback and shutdown release a shared site only for the feature that acquired it or restore an orphaned live claim at any stage; `acquire_shared` refuses a live site without users (`site_live`) | `PYTHONPATH=verification/probe /usr/bin/python3 -m unittest verification.analysis.test_music_keep`; `python3 verification/results/music-restart/verify_music_keep_writes.py`; `python3 verification/results/music-restart/mutate_music_keep_core.py`; scratch build + `check_no_x87.py` | 10 tests OK (decide_status: skip_all + inactive + bit clear -> playing, active / bit set / flag 2 clear / other media or record / non-music / paused / keep_running / vanilla / paused fallback -> forward, a music play drops the skip_all hold; alt-tab without Patch D -> paused; live site without users refused; 34 patched copies refused incl. the three D windows, the D call, the inactive-arm branches, an interior branch into `0x004983d9` and an extra caller of `0x004d14e0`; the D thunk's exact instruction list); verifier PASS 48/48; mutation script: 10 mutations each fail 1–8 checks, PASS; clean build 87 objects, 0 warnings; audit PASS, 661 reachable, 0 violations; the thunk disassembled from the DLL matches the source and `_x3m_music_keep_status` tests the two flags and `[0x608adc]` before any call (build deleted, not a candidate) |

## Open

- Settled by the flights: the stop-all is followed by a same-id replay at `start_ms=0` for alt-tab (Run 72 A trace 7 of 7 candidate pairs;
  Run 72 B 11/11), pause (Run 72 B 5/5) and save (Run 72 B 2/2), and `music_keep_seek action=skip` kept the position in
  all 18. With `skip_all` (unflown) an alt-tab of a music record produces no seek and no replay at all: expect
  `music_keep_stop name=alt_tab mode=skip_all held=1` and no following `music_trace_play`/`music_keep_seek` for that id;
  the install line must say `status_gate=ok alt_tab_mode=skip_all` (otherwise alt-tab is the flown paused mode).
- Patch D (the status-query gate) is unflown: the next flight's `music_keep_active` lines give how many frames run with the
  engine's active flag at 0 around each alt-tab, and `music_keep_status action=playing` counts the false "ended" answers
  it replaced. The bottle's InputFlags `0x70200107` (RunInBackground clear) is as reported by the second reviewer, not
  re-read here.
- Next flight, `--music-keep --music-trace`: alt-tab without an audible gap; `music_trace reason=ok` beside the keep (the
  shared-site fix); save and pause unchanged (`mode=paused`, `action=skip`); a sector change still switches tracks
  (`action=vanilla` or `pause_then_vanilla`).
- Orphans (G1, note section 6 "Orphans") remain possible only for a DirectShow-path music record under save/pause
  (`keep_running`); every flown track was DirectSound (`mode=paused`), `music_keep_orphan` 0.
- DirectShow-path music under `skip_all` keeps playing audibly in the background (its graph runs on its own threads)
  where vanilla pauses it and goes silent; unflown.
- DirectSound path under `skip_all` where the engine's active flag stays 0 through an alt-tab (native Windows,
  unverified): the loop blocks and the left-playing buffer loops its 5 s ring unserviced (no `DSBCAPS_GLOBALFOCUS`);
  gap recorded in `docs/architecture/platform-portability.md`.
- The ~0.5 s rewind heard on alt-tab in Run 72 B is unexplained.
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
