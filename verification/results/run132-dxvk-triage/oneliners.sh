#!/bin/sh
# Run 132 A triage. Usage per run r in 5 6: S=/tmp/x3-bottleX3-run$r
# python3 pass_state.py $S/session-*.log > pass_state_run$r.txt ; python3 other_passes.py ... > other_passes_run$r.txt
# python3 timing.py ... > timing_run$r.txt ; python3 inflight.py ... >> inflight_out.txt
# failed_rows.txt: grep -aE '^[a-z_]*(_failed|_refused)[ _]|failed=[1-9]|unavailable=' (+ nonzero hr= grep, empty)
# stderr_kinds.txt: sed strip timestamp | grep err/warn/info/mvk/GStreamer | digits->N | sort | uniq -c
# shader_rows.txt: grep -ac err: ; grep 'state cache|DXVK: v' launcher-stderr.log ; proxy shader row kinds
grep -a '^resource_identity ' "$1" | sort | uniq -c
