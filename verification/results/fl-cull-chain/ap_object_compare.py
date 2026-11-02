#!/usr/bin/env python3
"""AP object identity for the FL cull/LOD ports (docs/verification/farnhams-legacy.md, "Cull/LOD selector ports"): the
engine-effects method (verification/results/fl-engine-effects/ap_object_compare.py: build-ap's flags without -g, the
worktree's sources against `git show <base>:<path>`, objects compared after objcopy --strip-debug) over the AP
translation units this port touches or must leave alone. Prints one JSON line {base, compared, identical, different}.

usage: ap_object_compare.py <build-ap dir> <scratch dir> [base commit, default d8b71cbfe]
"""
import importlib.util
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('compare', HERE.parent / 'fl-engine-effects/ap_object_compare.py')
compare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compare)
compare.SOURCES = ['src/proxy/cull_small_parts.cpp', 'src/proxy/terran_station_lod.cpp', 'src/proxy/lod_occlusion.cpp',
                   'src/proxy/capture.cpp', 'src/proxy/engine_far_jets.cpp']

if __name__ == '__main__':
    if len(sys.argv) == 3:
        sys.argv.append('d8b71cbfe')
    sys.exit(compare.main())
