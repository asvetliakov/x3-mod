#!/usr/bin/env python3
"""Which d3d9 objects a change touched: every .obj of a baseline CMakeFiles/d3d9.dir copy against the same build tree
after the change, debug sections stripped (i686-w64-mingw32-objcopy --strip-debug: line tables move with any edit of
an included header, code and data do not). Prints the differing, added and removed objects.
Usage: compare_objects.py <baseline d3d9.dir> <build d3d9.dir> <scratch dir>
"""
import filecmp
import subprocess
import sys
from pathlib import Path


def stripped(path, scratch, tag):
    out = scratch / (tag + '_' + str(path).replace('/', '_'))
    subprocess.run(['i686-w64-mingw32-objcopy', '--strip-debug', str(path), str(out)], check=True)
    return out


def main():
    base, build, scratch = (Path(a) for a in sys.argv[1:4])
    scratch.mkdir(parents=True, exist_ok=True)
    old = {p.relative_to(base): p for p in base.rglob('*.obj')}
    new = {p.relative_to(build): p for p in build.rglob('*.obj')}
    differ = sorted(str(k) for k in old.keys() & new.keys()
                    if not filecmp.cmp(stripped(old[k], scratch, 'a'), stripped(new[k], scratch, 'b'), shallow=False))
    print('objects', len(new), 'differ', len(differ), 'added', sorted(map(str, new.keys() - old.keys())),
          'removed', sorted(map(str, old.keys() - new.keys())))
    for name in differ:
        print('  differs', name)


if __name__ == '__main__':
    main()
