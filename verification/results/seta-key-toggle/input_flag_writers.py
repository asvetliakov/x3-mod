"""Does the SETA key latch's injected 0x0049f4c0 call set [0x00607c60], the flag that makes 0x004042d1 skip Input for an
empty ring read? Lists every instruction of X3AP.exe that references the flag by absolute address. None may lie in the
by-name wrapper 0x0049f4c0..0x0049f56c, its resolver 0x0049f330 or its runner 0x0049f430, and all must lie on the input
path 0x00404280..0x00410b5f (the scan covers the whole image, so no native the script run reaches writes it by address).
usage: python3 -I input_flag_writers.py /mnt/c/X3/X3AP.exe   (needs i686-w64-mingw32-objdump)"""
import hashlib
import re
import subprocess
import sys

exe = sys.argv[1]
print('X3AP.exe SHA-256', hashlib.sha256(open(exe, 'rb').read()).hexdigest())
dis = subprocess.run(['i686-w64-mingw32-objdump', '-d', '--no-show-raw-insn', exe], capture_output=True, text=True,
                     check=True).stdout
refs = [(int(m.group(1), 16), m.group(2).strip()) for m in re.finditer(r'^\s*([0-9a-f]+):\s*(.*0x607c60\b.*)$', dis, re.M)]
for va, text in refs:
    print('  0x%08x  %s' % (va, text))
fail = []
if len(refs) != 8:
    fail.append('expected 8 references, got %d' % len(refs))
if any(0x0049f330 <= va < 0x0049f570 for va, _ in refs):
    fail.append('a reference inside the wrapper or its callees')
if not all(0x00404280 <= va < 0x00410b60 for va, _ in refs):
    fail.append('a reference outside the input path')
sets = [va for va, t in refs if t.startswith('movl   $0x1')]
if len(sets) != 6:
    fail.append('expected 6 sets, got %d' % len(sets))
print('sets=%d, each right after the input routine\'s own key handling (0x004100f2..0x00410b57); clear 0x0040430f; '
      'test 0x004042d1' % len(sets))
print('RESULT %s%s' % ('PASS' if not fail else 'FAIL', ''.join('\n  FAIL: ' + f for f in fail)))
