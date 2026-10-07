#!/bin/sh
# view_setup triage, run9 (wined3d) vs run10 (DXVK). S9=/tmp/x3-bottleX3-run9/session-*.log S10=/tmp/x3-bottleX3-run10/session-*.log
# us_fields.py <log> > us_fields_run<r>.txt     in-flight medians of every *_us field of the per-frame rows
# slow_join.py <log> > slow_join_run<r>.txt     frame_phases_slow joined to hdr_frame readback_transfer_lock_us by frame (offset scan)
# fit.py slow_join_run<r>.txt > fit_run<r>.txt  view_setup_us = a + b*lock_us, residual view_setup - lock
# once per frame: grep '^telemetry_metric' $S | grep -E 'name=(hdr_meter_readback|present_normal) '  (counts equal per interval)
# stderr: grep -aiE 'sync|flush|stall|wait' launcher-stderr.log  -> only Vulkan extension listing lines, no sync warnings
