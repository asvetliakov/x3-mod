#!/usr/bin/env python3
"""AP object identity for the FL chase view restore port (docs/verification/farnhams-legacy.md, "Chase view restore on
FL, host evidence"): the engine-effects method (verification/results/fl-engine-effects/ap_object_compare.py: build-ap's
flags without -g, the worktree's sources against `git show <base>:<path>`, objects compared after objcopy
--strip-debug) over the AP translation units this port edits. Prints one JSON line {base, compared, identical,
different}.

usage: ap_object_compare.py <build-ap dir> <scratch dir> [base commit, default f8b255b17]
"""
import importlib.util
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('compare', HERE.parent / 'fl-engine-effects/ap_object_compare.py')
compare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compare)
compare.SOURCES = ['src/proxy/chase_transition.cpp', 'src/proxy/chase_camera.cpp', 'src/proxy/loader.cpp',
                   'src/proxy/capture.cpp']

if __name__ == '__main__':
    if len(sys.argv) == 3:
        sys.argv.append('f8b255b17')
    sys.exit(compare.main())
