# Run 118 A triage (run401 off --debug, run402 native) — 2026-10-03

Logs (untracked, local): /tmp/x3-bottleX3-run401/session-20261003-021004-212.log,
/tmp/x3-bottleX3-run402/session-20261003-021551-216.log. DLL sha256 924d12bd..., source_commit 526a741c (both runs).

1. Aggregates: `python3 triage.py <run401 log> <run402 log> > triage_output.txt`
2. Single rows (fields quoted in the report):
   `grep -a -E '^(engine_effects_(patch|mode|device|bodies|resolve|partial|sites)) ' <log> | cut -c1-500`
3. Capture-frame selector + dt (run401 frame 1231, run402 frame 1534):
   `grep -a -E "^motion_output_frame .*frame=N " <log> | grep -oE '(selector_state|scene_open|scene_end_source|scene_end_check|scene_hook)=[^ ]*'`
   run401 1231: selector_state=9 scene_open=0 scene_hook=1 scene_end_source=hook scene_end_check=1 draws=144 dt_ms=607 (capture frame)
   run402 1534: same selector fields, draws=340 dt_ms=693 (capture frame)
4. Tail: `tail -n 2000 <run401 log> | grep -a -oE '^[a-z_]+' | sort | uniq -c` -> engine_frame 174, engine_memory 4, session_end 1;
   session_end frames=12824 resets=0 exception=0 dropped=0.
