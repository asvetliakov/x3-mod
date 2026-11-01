#!/usr/bin/env python3
"""FL object identity of a change (the run216 reviewer's script, generalised): compile the given FL objects from two
exported source trees with the FL build's own flags (build.make / flags.make of BUILD_FL), map each tree's path to
the same prefix, strip debug info and byte-compare.

usage: fl_object_compare.py [--count SYMBOL] BASE_TREE NEW_TREE BUILD_FL OBJ...
       e.g. OBJ = src/proxy/motion_output.cpp.obj src/proxy/sun_occlusion.cpp.obj
            src/renderer/sun_occlusion_pass.cpp.obj src/proxy/loader.cpp.obj src/proxy/capture.cpp.obj
       (the FL objects that include the changed sources; every other object's inputs are unchanged)
       --count SYMBOL also prints SYMBOL's instruction count in both objects (any build dir, e.g. build-ap for the
       probe hook x3m_sun_probe_decide in src/proxy/sun_occlusion.cpp.obj).
"""
import filecmp
import os
import re
import shlex
import subprocess
import sys
import tempfile

args = sys.argv[1:]
count = None
if args[:1] == ['--count']:
    count, args = args[1], args[2:]
base_tree, new_tree, build = (os.path.abspath(p) for p in args[:3])


def instructions(path, symbol):
    """Instruction lines of `symbol` in objdump -d (0 when absent)."""
    text = subprocess.check_output(['i686-w64-mingw32-objdump', '-d', '--no-show-raw-insn', path], text=True)
    body = re.search(r'<_?%s>:\n(.*?)(?:\n\n|\Z)' % re.escape(symbol), text, re.S)
    return len([l for l in body.group(1).splitlines() if re.match(r'\s+[0-9a-f]+:\s+\S', l)]) if body else 0


build_make = open(os.path.join(build, 'CMakeFiles/d3d9.dir/build.make')).read()
flags = open(os.path.join(build, 'CMakeFiles/d3d9.dir/flags.make')).read()
defines = shlex.split(re.search(r'CXX_DEFINES = (.*)', flags).group(1))
cxx_flags = re.search(r'CXX_FLAGS = (.*)', flags).group(1).split()
generated = os.path.join(build, 'generated')
failed = 0
with tempfile.TemporaryDirectory() as out_dir:
    for obj in args[3:]:
        rule = re.search(re.escape('CMakeFiles/d3d9.dir/' + obj) + r': .*?\n\t.*?\n\t(\S*i686-w64-mingw32-g\+\+ .*)', build_make)
        command = rule.group(1)
        extra = re.search(r'\$\(CXX_FLAGS\)(.*?) -MD', command).group(1).split()
        source = re.search(r' -c (\S+)', command).group(1)
        relative = re.sub(r'^.*?/(src/.*)$', r'\1', source)
        stripped = {}
        for name, tree in (('base', base_tree), ('new', new_tree)):
            out = os.path.join(out_dir, name + '_' + obj.replace('/', '_'))
            subprocess.run(['i686-w64-mingw32-g++'] + defines + ['-I' + generated + '/fog_field', '-I' + generated] + cxx_flags +
                           extra + ['-ffile-prefix-map=%s=/X' % tree, '-c', os.path.join(tree, relative), '-o', out], check=True)
            subprocess.run(['i686-w64-mingw32-objcopy', '--strip-debug', out, out + '.s'], check=True)
            stripped[name] = out + '.s'
        same = filecmp.cmp(stripped['base'], stripped['new'], shallow=False)
        failed += not same
        print('same' if same else 'DIFF', obj)
        if count:
            print('  %s instructions: base %d new %d' % (count, instructions(stripped['base'], count),
                                                       instructions(stripped['new'], count)))
sys.exit(1 if failed else 0)
