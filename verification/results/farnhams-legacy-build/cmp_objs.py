#!/usr/bin/env python3
"""Compare the code and data of every d3d9 object between two build trees.

docs/architecture/farnhams-legacy-build.md: the ap edition must compile what the tree compiled before
the edition switch. For each CMakeFiles/d3d9.dir/**/*.obj present in both trees, the contents of every
non-debug section (objdump -s) and the relocations (objdump -r) are hashed; debug sections (line
numbers move with the #if fences) are ignored. Prints one JSON object.

usage: cmp_objs.py <baseline build dir> <candidate build dir>
"""
import hashlib
import json
import pathlib
import re
import subprocess
import sys

OBJDUMP = 'i686-w64-mingw32-objdump'


def digest(obj):
    out = []
    heads = subprocess.run([OBJDUMP, '-h', str(obj)], capture_output=True, text=True, check=True).stdout
    names = [m.group(1) for m in re.finditer(r'^\s*\d+\s+(\S+)\s', heads, re.M)]
    keep = [n for n in names if not n.startswith('.debug') and not n.startswith('.gnu_debug')]
    for n in keep:
        s = subprocess.run([OBJDUMP, '-s', '-j', n, str(obj)], capture_output=True, text=True, check=True).stdout
        out.append(n + '\n' + '\n'.join(s.split('\n')[3:]))  # skip the file-name header
    r = subprocess.run([OBJDUMP, '-r'] + sum([['-j', n] for n in keep], []) + [str(obj)],
                       capture_output=True, text=True, check=True).stdout
    out.append('\n'.join(r.split('\n')[2:]))
    return hashlib.sha256('\n'.join(out).encode()).hexdigest()


def main():
    a, b = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
    objs_a = {p.relative_to(a): p for p in (a / 'CMakeFiles/d3d9.dir').rglob('*.obj')}
    objs_b = {p.relative_to(b): p for p in (b / 'CMakeFiles/d3d9.dir').rglob('*.obj')}
    common = sorted(set(objs_a) & set(objs_b))
    different = [str(k) for k in common if digest(objs_a[k]) != digest(objs_b[k])]
    print(json.dumps({'objects_baseline': len(objs_a), 'objects_candidate': len(objs_b),
                      'only_baseline': sorted(map(str, set(objs_a) - set(objs_b))),
                      'only_candidate': sorted(map(str, set(objs_b) - set(objs_a))),
                      'identical': len(common) - len(different), 'different': different}, indent=1))


if __name__ == '__main__':
    main()
