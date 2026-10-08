# Run 138 A triage (run17 = Run138 batched cull, run16 = Run137 per-part cull, run15 = Run136 async, run14 = Run135 sync), DXVK.
# python3 cull_run17.py /tmp/x3-bottleX3-run17 > cull_run17_out.txt                                  # (1)(3) cull fields over in-flight frames, cost vs skipped*10us
# python3 ab_cull.py /tmp/x3-bottleX3-run1{7,6,5,4} > ab_cull_out.txt                                # (2) dt, hitches, phase medians, draw bins (field list extended for batched rows; .get default 0)
# python3 windows_by_draws.py /tmp/x3-bottleX3-run1{7,6,5,4} > windows_by_draws_out.txt              # (2)(3) view_submit at matched draws; 20-33/33-50/>50 counts
# python3 tail_by_draws.py /tmp/x3-bottleX3-run1{7,6,5,4} > tail_by_draws_out.txt
# python3 cost_model.py /tmp/x3-bottleX3-run1{7,5,4} > cost_model_out.txt   # NOTE: labels hard-coded; its "run16" rows are run17 here
# matched-window view_submit_p50 medians (windows by mean app draws): run17 240-250 n=6 -> 6516 us; run16 241-252 n=5 -> 8004; run15 248-256 n=5 -> 6796; run14 231-247 n=7 -> 6335
# failure rows: grep -aE '^[a-z_]*(_failed|_refused)[ _]|failed=[1-9]|unavailable=' $S | awk '{print $1}' | sort | uniq -c > failed_kinds_run17.txt
# stderr kinds: grep -aiE 'err|warn|mvk|GStreamer|DXVK' launcher-stderr.log | strip ts, GUID, hex, digits | sort -u ; run16 vs run17 identical (diff rc=0)
