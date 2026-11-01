#!/usr/bin/env python3
"""FL object identity: byte-compare objects after `objcopy --strip-debug` (debug line info shifts with any header edit).

usage: fl_object_compare.py BASE_D3D9_DIR NEW_D3D9_DIR OBJ...
       (OBJ relative to both CMakeFiles/d3d9.dir trees; BASE built from the parent commit in the same tree path)
"""
import filecmp
import os
from pathlib import Path
import subprocess
import sys
import tempfile

base, new = Path(sys.argv[1]), Path(sys.argv[2])
for f in sys.argv[3:]:
    with tempfile.TemporaryDirectory() as d:
        a, b = os.path.join(d, 'a.o'), os.path.join(d, 'b.o')
        subprocess.run(['i686-w64-mingw32-objcopy', '--strip-debug', str(base / f), a], check=True)
        subprocess.run(['i686-w64-mingw32-objcopy', '--strip-debug', str(new / f), b], check=True)
        print(('same ' if filecmp.cmp(a, b, shallow=False) else 'DIFF ') + f)
