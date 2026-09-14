# Voice decoder ledger

Feature ledger for the opt-in WMA decoder plugin path and the DMO fallback hook
(`docs/architecture/voice-decoder-adapter.md`; startup sequence in
`docs/reverse-engineering/voice-startup-sequence.md`). Replica runs are recorded
in `verification/results/bottle-X3/voice-startup-replica.json`; raw output stays
under `/tmp`.

| Date | Change | Check | Result |
| --- | --- | --- | --- |
| 2026-09-14 | Run-13 crash of the DMO fallback hook: `Init` devirtualised to `__cxa_pure_virtual` (`call 0`), fixed by an explicit vtable binding; replica mode `game-dmo-hook` installs the production hook through `engine_patch` on a replica of `004cfd0e..004cfd7c` | `run_voice_startup_replica.py --mode game-dmo-hook` with the v3 plugin, EXE `1257cf19…` (unfixed) then `71b6a942…` (fixed); `test_voice_dmo_fallback.py`; `check_no_x87.py` on the worktree DLL | Unfixed: execute fault at `eip=0` after `hits=1 activations=1`, stub/tail bytes correct (`/tmp/x3-voice-startup-hook1`). Fixed: completed, 3 activations `retries_ok=3`, sites ESI=HR/EDI=0/EBX,ESP intact, 5 samples 882000 bytes, clean teardown 7.1 s (`/tmp/x3-voice-startup-hook2`). Host tests pass; x87 audit PASS (224 reachable functions, no violations) |
| 2026-09-14 | Review follow-up: fault witness lock-free (fixed record + atomic sequence, unbuffered `WriteFile`, formatted at Present), removed at shutdown/DLL detach; unfixed replica rerun so the record carries the witness through the crash path; runner requires one site witness per stream | Same replica command, unfixed EXE `240073b9…` (temporary revert of the binding, not committed) then fixed EXE `5a29658a…`; both voice test modules; `check_no_x87.py` on the worktree DLL | Unfixed: exit 5 in 1.0 s, record `hook_lines` = install line + `voice_dmo_fallback_fault code=c0000005 eip=00000000 hits=1 activations=1 faults=1` (`/tmp/x3-voice-startup-hook6`; `SetErrorMode(SEM_NOGPFAULTERRORBOX)` in the fixture, since winedbg attach hung two earlier reruns). Fixed: completed 3.6 s, 3 activations `retries_ok=3`, 3 site witnesses, 5 samples 882000 bytes (`/tmp/x3-voice-startup-hook7`). Host tests 29 OK; x87 audit PASS (224 reachable, no violations) |
