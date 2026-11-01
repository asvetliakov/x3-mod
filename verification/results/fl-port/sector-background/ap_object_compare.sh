#!/bin/sh
# Producer of ap_object_compare.json: build-ap-base is the ap edition (-j4, RelWithDebInfo, mingw-i686) of main
# 600a15ff built in this worktree (git switch --detach main), build-ap the same with the step-6 commits.
python3 verification/results/farnhams-legacy-build/cmp_objs.py build-ap-base build-ap \
  > verification/results/fl-port/sector-background/ap_object_compare.json
