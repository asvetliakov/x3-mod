#!/bin/sh
# Producer of ap_object_compare.json / fl_object_compare.json. BASE_AP/BASE_FL: the ap and fl editions (-j4,
# RelWithDebInfo, cmake/mingw-i686.cmake) of main daa2776c built in this worktree's build-ap/build-fl before the
# step-8 edits, their CMakeFiles/d3d9.dir copied aside; build-ap/build-fl then rebuilt with the step-8 changes.
python3 verification/results/farnhams-legacy-build/cmp_objs.py "$BASE_AP" build-ap \
  > verification/results/fl-port/fog-prefill/ap_object_compare.json
python3 verification/results/farnhams-legacy-build/cmp_objs.py "$BASE_FL" build-fl \
  > verification/results/fl-port/fog-prefill/fl_object_compare.json
