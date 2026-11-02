#!/usr/bin/env python3
"""AP object identity for the FL far-jet hand-off (docs/verification/farnhams-legacy.md, "Far-jet hand-off on FL, host
evidence"): the engine-effects method (verification/results/fl-engine-effects/ap_object_compare.py: build-ap's flags
without -g, the worktree's sources against `git show <base>:<path>`, objects compared after objcopy --strip-debug) over
the AP translation units that compile the changed handler, its header or the plume stage. Prints one JSON line
{base, compared, identical, different}.

usage: ap_object_compare.py <build-ap dir> <scratch dir> <base commit>
"""
import importlib.util
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('compare', HERE.parent / 'fl-engine-effects/ap_object_compare.py')
compare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compare)
compare.SOURCES = ['src/proxy/engine_far_jets.cpp', 'src/proxy/motion_output.cpp', 'src/proxy/cull_small_parts.cpp',
                   'src/proxy/capture.cpp', 'src/proxy/engine_effects.cpp']

if __name__ == '__main__':
    sys.exit(compare.main())
