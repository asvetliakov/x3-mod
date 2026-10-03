# Run 406 (Run 121 A) engine plumes triage

Source: `/tmp/x3-bottleX3-run406/session-20261003-140134-216.log` (untracked, 496 MB), DLL a124a5af from 8fd8d48d.
`eng.log` = `grep -E '^(engine_|capture|f8|session_end)'` of the session log (scratch, not kept).

- `draws.py <log> <engine_bodies.json> frames...` -> `draws_7144_7151_9327_20450.txt` (engine_draw rows, body value, RCS/BRAKE flag bits).
- `stage_window.py <log> 9027 9634` -> `stage_9027_9634.txt` (engine_stage change points around F8 burst 9327).
- `floor_drops.py <log> 9027 9634` -> `floor_drops.txt` (floored falling while nozzles does not; fade/cap ranges).
- `session_stats.py <log>` -> `session_stats.txt` (main-jet s/size histograms from logged frames, stage_us, health); frame_end qpc median dt appended.

Limitation: `engine_draw` is logged only on the first 8 frames with jets and F8 frames; parent (node+0x18) is not logged, so per-ship per-frame floors cannot be reconstructed from this log.
