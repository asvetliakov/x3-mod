#!/usr/bin/env python3
"""Run 189 `seta_restore refused reason=bytes_mismatch` (docs/verification/seta-restore.md): which window the Run 189
build's windows_match() refused first, and why. Replays that build's comparison rule, engine_patch::verify_bytes (length
5..max_prologue, src/proxy/engine_patch.cpp/.h), over the seta_restore_core.h windows in the Run 189 windows_match()
order, compares each against X3AP.exe, and lists every other claim site of the tree (the collide verifier's collector)
that overlaps a window. Read-only.

    python3 -I verification/results/seta-script-toggle/run189_refusal_cause.py [X3AP.exe]
"""
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'verification/probe'))
import verify_collide_sites  # noqa: E402

EXE = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('/mnt/c/X3/X3AP.exe')
H = (ROOT / 'src/proxy/seta_restore_core.h').read_text()
bound = int(re.search(r'max_prologue = (\d+);', (ROOT / 'src/proxy/engine_patch.h').read_text()).group(1))
low = int(re.search(r'length >= (\d+) && length <= max_prologue', (ROOT / 'src/proxy/engine_patch.cpp').read_text()).group(1))
# windows_match() order of the Run 189 build (seta_restore.cpp at ba33e696); two addresses are header expressions
ORDER = [(0x004b148c, 'hook_bytes'), (0x004b1494, 'case_rest'), (0x004b1300, 'dispatch_head'), (0x004b12d0, 'thunk_body'),
         (0x004b161c, 'table_entry'), (0x004a3880, 'native_args'), (0x004a38d5, 'native_ip'), (0x004a39b3, 'return_scan'),
         (0x0049f4c0, 'wrapper_body'), (0x0041211f, 'stop_call')]


def array(name):
    m = re.search(r'\b%s\[[a-z_0-9 ]*\] = \{([^}]*)\}' % re.escape(name), H)
    return bytes(int(v, 0) for v in re.findall(r'0x[0-9a-f]+', m.group(1)))


data = EXE.read_bytes()
pe = struct.unpack_from('<I', data, 0x3c)[0]
nsec, opt = struct.unpack_from('<H', data, pe + 6)[0], struct.unpack_from('<H', data, pe + 20)[0]
base = struct.unpack_from('<I', data, pe + 24 + 28)[0]
sections = [struct.unpack_from('<IIII', data, pe + 24 + opt + 40 * i + 8) for i in range(nsec)]


def rd(va, n):
    for _, sva, rs, ra in sections:
        if base + sva <= va and va + n <= base + sva + rs:
            return data[ra + va - base - sva:ra + va - base - sva + n]
    return None


claims = verify_collide_sites.other_claims(skip_prefix='seta_restore', own_names=())
first = None
print('verify_bytes accepts lengths %d..%d; %d other claim sites collected' % (low, bound, len(claims)))
for va, name in ORDER:
    expected = array(name)
    on_disk = rd(va, len(expected)) == expected
    accepted = low <= len(expected) <= bound
    hits = [n for n, a, l in claims if a < va + len(expected) and va < a + l]
    print('%-13s 0x%08x len=%3d disk_match=%d verify_bytes=%s overlapping_claims=%s'
          % (name, va, len(expected), on_disk, 'pass' if accepted and on_disk else 'refuse', hits or '-'))
    if first is None and not (accepted and on_disk):
        first = (name, va, len(expected))
print('first refused window: %s 0x%08x len=%d' % first if first else 'first refused window: none')
