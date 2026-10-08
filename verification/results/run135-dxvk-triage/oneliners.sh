#!/bin/sh
# Run 135 A triage (run14, DXVK, Run135 7f7c4659). S=/tmp/x3-bottleX3-run14/session-20261008-142814-244.log
# capture_rows.py $S 4218-4229 6113-6124 6291-6302 > capture_rows_run14.txt   (engine_light_frame/engine_stage/engine_frame)
# engine_draw_rows.py $S 4220 6115 6122 6293 6300 > engine_draw_run14.txt     (Ocelot records: handle, value_eff, origin)
# project_nozzles.py $S 4220,6115,6122,6293,6300 > project_nozzles_run14.txt  (copy of run134 script; ndc, view z)
# sample_nozzles.py $S /tmp/x3-bottleX3-run14 6115,6122,6293,6300 > sample_nozzles_run14.txt (HDR box around each nozzle)
# plate_series.py $S > plate_series_run14.txt ; plate_series_summary_run14.txt (inline python over it)
# inflight.py / hitches.py LOG 50|33 / slow_phase.py / lock_max.py for run11, run12, run14 -> *_run11_12_14.txt, hitches33_run*.txt
# dxvk_cache_count.py ~/Library/.../Bottles/X3/drive_c/X3/X3AP.dxvk-cache > dxvk_cache_after_run14.txt (mtime 14:30, after run14)
# stderr: grep -a 'DXVK: Read' /tmp/x3-bottleX3-run1{1,2,4}/launcher-stderr.log -> 212 / 215 / 215 entries read at start
