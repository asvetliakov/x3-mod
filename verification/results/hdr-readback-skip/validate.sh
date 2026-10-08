#!/bin/sh
# The runner's own validators on the finished case directories of both backends, from the worktree root:
#   sh verification/results/hdr-readback-skip/validate.sh > verification/results/hdr-readback-skip/validation.txt
# (validate_offline.py of the double-buffer entry: the DXVK fixture crashes in teardown after its last frame, exit 5,
# so the runner stops before validating; wined3d directories validate the same way, RESULT line included.)
# Then the per-frame rows of the forced-pending cases (stepped / meter_event_ready / meter_skips / steps / ev).
B=verification/probe/build
V=verification/results/hdr-readback-double-buffer/validate_offline.py
for pair in \
  seam-hdr-meter-pending:20261008-041604-815288 seam-hdr-meter-pending:20261008-041628-575517 \
  seam-hdr-meter-pending-cap:20261008-041609-093256 seam-hdr-meter-pending-cap:20261008-041647-292563 \
  seam-hdr-exposure:20261008-041654-412623 seam-hdr-tonemap-fault:$(ls -d $B/motion-output-seam-hdr-tonemap-fault-2026* | tail -n 1 | sed 's/.*fault-//'); do
  c=${pair%%:*}; d=$B/motion-output-$c-${pair#*:}
  printf '%s ' "$(grep -h '^loaded_module name=d3d9.dll' "$d"/x3-modern-captures/session-*.log | grep -o 'image_size=[0-9]*' | head -n 1)"
  python3 "$V" "$c" "$d" --nan-store-zero 2>&1 | tail -n 1 | cut -c1-400
done
for d in $B/motion-output-seam-hdr-meter-pending-20261008-041604-815288 $B/motion-output-seam-hdr-meter-pending-20261008-041628-575517 \
         $B/motion-output-seam-hdr-meter-pending-cap-20261008-041609-093256 $B/motion-output-seam-hdr-meter-pending-cap-20261008-041647-292563; do
  echo "$(basename "$d") $(grep -h '^loaded_module name=d3d9.dll' "$d"/x3-modern-captures/session-*.log | grep -o 'image_size=[0-9]*' | head -n 1)"
  python3 -c "
import re, sys
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('hdr_frame '):
        f = dict(re.findall(r'(\w+)=([^\s]+)', l))
        print('  frame=%s stepped=%s ready=%s skips=%s steps=%s ev=%s readback=%s' % tuple(f[k] for k in ('frame', 'stepped', 'meter_event_ready', 'meter_skips', 'steps', 'ev', 'readback')))
" "$d"/x3-modern-captures/session-*.log
done
