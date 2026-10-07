#!/bin/sh
# Run 131 A (DXVK) triage one-liners. Usage: sh run131_oneliners.sh /tmp/x3-bottleX3-run4
S="$1"; L=$(ls "$S"/session-*.log)
grep -a '^resource_identity ' "$L" | sort | uniq -c                    # GetPrivateData failures
grep -a '^session_end ' "$L" | cut -c1-120                              # resets
grep -a '^frame_phases ' "$L" | awk '{for(i=1;i<=NF;i++){split($i,a,"=");v[a[1]]=a[2]} print v["frame"],v["dt_p50_us"],v["dt_p95_us"]}'
sed -E 's/^\[[^]]*\] //' "$S/launcher-stderr.log" | grep -E '^(err|warn):|^\[mvk-(warn|error)\]|^Warning|GStreamer' | sed -E 's/[0-9]+/N/g' | cut -c1-120 | sort | uniq -c | sort -rn
