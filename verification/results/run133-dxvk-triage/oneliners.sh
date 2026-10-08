#!/bin/sh
# Run 133 A triage: run11 (DXVK, Run133 1a8de604) vs run10 (DXVK, Run132) vs run9 (wined3d, Run132). S=/tmp/x3-bottleX3-run11/session-*.log
# pass_state.py $S > pass_state_run11.txt          (copy of run132 script + hdr_frame meter_event_ready)
# us_fields.py $S > us_fields_run11.txt            (readback_copy_us / readback_lock_us replace readback_transfer_lock_us)
# meter_event.py $S > meter_event_run11.txt        (in-flight meter_event_ready counts, lock_us by state)
# lock_tail.py $S > lock_tail_run11.txt            (in-flight lock_us > 1 ms rows and runs; frame 7059 = 635.6 ms)
# ab.py $S > ab_run11.txt ; inflight.py $S > inflight_run11.txt
# draws_vs_dt.py /tmp/x3-bottleX3-run9 /tmp/x3-bottleX3-run10 /tmp/x3-bottleX3-run11 > draws_vs_dt_out.txt
# failure rows: grep -aE '^[a-z_]*(_failed|_refused)[ _]|failed=[1-9]|unavailable=' $S | awk '{print $1}' | sort | uniq -c > failed_kinds_run11.txt (run10 likewise)
# stderr: sed strip [ts]; grep -aiE 'err|warn|mvk|GStreamer|DXVK'; digits->N; uniq -c > stderr_kinds_run11.txt ; 'zero area' 23/23, VK_MVK_moltenvk 11/11 in run10/run11
