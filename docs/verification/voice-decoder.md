# Voice decoder ledger

Feature ledger for the opt-in WMA decoder plugin path and the DMO fallback hook
(`docs/architecture/voice-decoder-adapter.md`; startup sequence in
`docs/reverse-engineering/voice-startup-sequence.md`). Replica runs are recorded
in `verification/results/bottle-X3/voice-startup-replica.json`; raw output stays
under `/tmp`.

| Date | Change | Check | Result |
| --- | --- | --- | --- |
| 2026-09-14 | Run-13 crash of the DMO fallback hook: `Init` devirtualised to `__cxa_pure_virtual` (`call 0`), fixed by an explicit vtable binding; replica mode `game-dmo-hook` installs the production hook through `engine_patch` on a replica of `004cfd0e..004cfd7c` | `run_voice_startup_replica.py --mode game-dmo-hook` with the v3 plugin, EXE `1257cf19…` (unfixed) then `71b6a942…` (fixed); `test_voice_dmo_fallback.py`; `check_no_x87.py` on the worktree DLL | Unfixed: execute fault at `eip=0` after `hits=1 activations=1`, stub/tail bytes correct (`/tmp/x3-voice-startup-hook1`). Fixed: completed, 3 activations `retries_ok=3`, sites ESI=HR/EDI=0/EBX,ESP intact, 5 samples 882000 bytes, clean teardown 7.1 s (`/tmp/x3-voice-startup-hook2`). Host tests pass; x87 audit PASS (224 reachable functions, no violations) |
