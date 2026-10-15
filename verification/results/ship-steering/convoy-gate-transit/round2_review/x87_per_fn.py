#!/usr/bin/env python3
"""Lane fix round 2 review: the x87 instructions per function of src/proxy/ship_steering.cpp under i686 MinGW (-O2, the DLL's SSE2 and
stack flags, -fno-exceptions), the working tree against a git revision (default HEAD): builds both into assembly in a temporary tree
(the revision's src/proxy headers), counts the x87 mnemonics per function and prints the functions whose counts differ and the totals.
Usage: python3 x87_per_fn.py [rev] (from the repository root; no Wine, no game)."""
import collections, os, re, subprocess, sys, tempfile

REV = sys.argv[1] if len(sys.argv) > 1 else 'HEAD'
FLAGS = ['-std=c++17', '-O2', '-msse2', '-mfpmath=sse', '-mstackrealign', '-mincoming-stack-boundary=2', '-fno-exceptions', '-S', '-o', '-']
X87 = re.compile(r'^\s+(f(?!ence)[a-z0-9]*)\b')  # x87 mnemonics: fld, fstp, fild, fistp, fldcw, fnstcw, fxch, fmul, ... (not 'fence')


def counts(src_dir):
    asm = subprocess.run(['i686-w64-mingw32-g++'] + FLAGS + [os.path.join(src_dir, 'ship_steering.cpp')], capture_output=True, text=True,
                         check=True).stdout
    per, fn = collections.Counter(), None
    for line in asm.splitlines():
        m = re.match(r'^([A-Za-z_.$][\w.$@?]*):', line)
        if m and not re.match(r'^\.?L\w*\d+$', m.group(1)):  # (not a local label: .L123 / L123 / LC0)
            fn = m.group(1)
            continue
        if fn and X87.match(line):
            per[fn] += 1
    return per


with tempfile.TemporaryDirectory() as t:
    files = subprocess.run(['git', 'ls-tree', '--name-only', REV, 'src/proxy/'], capture_output=True, text=True, check=True).stdout.split()
    for f in files:
        if f.endswith(('.h', '.cpp')):
            with open(os.path.join(t, os.path.basename(f)), 'wb') as o:
                o.write(subprocess.run(['git', 'show', '%s:%s' % (REV, f)], capture_output=True, check=True).stdout)
    a = counts(t)
b = counts('src/proxy')
diff = sorted(k for k in set(a) | set(b) if a[k] != b[k])
print('x87 per function, ship_steering.cpp: %s %d functions %d instructions; tree %d functions %d instructions; differing %d' % (
    REV, len(a), sum(a.values()), len(b), sum(b.values()), len(diff)))
for k in diff:
    print('  %s: %d -> %d' % (k, a[k], b[k]))
