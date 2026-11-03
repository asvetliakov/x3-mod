#!/usr/bin/env python3
"""AP object identity for the FL music family port (docs/verification/farnhams-legacy.md, "music family on FL (host
evidence, 2026-11-03)"): the engine-effects method (verification/results/fl-engine-effects/ap_object_compare.py:
build-ap's flags without -g, the worktree's sources against `git show <base>:<path>`, objects compared after objcopy
--strip-debug) over the AP translation units the port could reach: capture.cpp (its FL branches changed) and the AP
media_cue / music_keep / music_alert modules whose headers the FL modules share. Prints one JSON line.

usage: ap_object_compare.py <build-ap dir> <scratch dir> <base commit>
"""
import importlib.util
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('compare', HERE.parent / 'fl-engine-effects/ap_object_compare.py')
compare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compare)
compare.SOURCES = ['src/proxy/capture.cpp', 'src/proxy/media_cue.cpp', 'src/proxy/music_keep.cpp',
                   'src/proxy/music_alert.cpp']

if __name__ == '__main__':
    sys.exit(compare.main())
