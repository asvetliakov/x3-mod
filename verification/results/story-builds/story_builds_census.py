#!/usr/bin/env python3
"""Which KC object carries which build id, and which object the VM holds when (docs/architecture/story-builds.md).
Read-only; prints names, sizes and build ids only (no script bytes).

1. Every L/*.obj in every catalogue (base and addon) and every loose L/*.obj under the game directory: build id
   (BE32 at +8 of the decoded STOR header) and size.
2. The EXE's KC object-name table 0x0057c8c8.. and its readers: the process-init load of x3config (0x004031fd) and the
   main loop's mode switch (0x004036e0, jump table 0x00403818) that picks the object the session function loads.
3. With a log path: the music_queue_kc rows (frame, action, build, CODE size) and the session/stop-all rows around them.

    python3 -I verification/results/story-builds/story_builds_census.py [X3 dir] [x3m.log] > story_builds_census.txt
"""
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
from inspect_x3 import read_catalogue  # noqa: E402

X3 = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('/mnt/c/X3')
LOG = Path(sys.argv[2]) if len(sys.argv) > 2 else None

print('1. KC objects (L/*.obj): source, size, build id')
for d in (X3, X3 / 'addon'):
    for cat in sorted(d.glob('*.cat')):
        for e in read_catalogue(cat):
            p = e['path'].lower()
            if p.startswith('l/') and p.endswith('.obj'):
                with open(cat.with_suffix('.dat'), 'rb') as f:
                    f.seek(e['offset'])
                    head = bytes(b ^ 0x33 for b in f.read(12))
                build = struct.unpack_from('>I', head, 8)[0] if head[:4] == b'STOR' else None
                print('  %-14s %-18s %9d B  build %s' % (cat.relative_to(X3), e['path'], e['size'], build))
loose = sorted(p for p in X3.rglob('*.obj') if p.parent.name.lower() == 'l')
print('  loose L/*.obj: %s' % ([str(p.relative_to(X3)) for p in loose] or 'none'))

print('\n2. X3AP.exe object-name table and its readers')
data = (X3 / 'X3AP.exe').read_bytes()
pe = struct.unpack_from('<I', data, 0x3c)[0]
nsec, opt = struct.unpack_from('<H', data, pe + 6)[0], struct.unpack_from('<H', data, pe + 20)[0]
secs = [struct.unpack_from('<IIII', data, pe + 24 + opt + 40 * i + 8) for i in range(nsec)]


def off(va):
    r = va - 0x400000
    for vs, sva, rs, ra in secs:
        if sva <= r < sva + max(vs, rs):
            return ra + r - sva


def va_of(o):
    for vs, sva, rs, ra in secs:
        if ra <= o < ra + rs:
            return 0x400000 + sva + o - ra


for slot in range(0x57c8c8, 0x57c8e4, 4):
    ptr = struct.unpack_from('<I', data, off(slot))[0]
    name = data[off(ptr):data.index(b'\0', off(ptr))].decode('latin1')
    refs = [va_of(m.start()) for m in re.finditer(re.escape(struct.pack('<I', slot)), data)]
    print('  0x%08x -> %-11s read at %s' % (slot, name, ', '.join('0x%08x' % r for r in refs if r)))
table = struct.unpack_from('<9I', data, off(0x403818))
print('  mode switch 0x004036e0 jmp [ecx*4+0x00403818], [*0x0057fc60+0x4d8] = 0..8: %s' % ', '.join('%d:0x%08x' % (i, v) for i, v in enumerate(table)))
for va, want in ((0x004031fd, 'a1d4c85700'), (0x00403203, 'e858'), (0x004036e7, 'a1ccc85700'), (0x004036ee, 'a1c8c85700'),
                 (0x0040370f, 'e87ce4ffff'), (0x0040373a, 'e801010000'), (0x00403957, '8bbed4040000'), (0x0040396e, 'e8bd960900')):
    got = data[off(va):off(va) + len(want) // 2].hex()
    print('  0x%08x %s %s' % (va, got, 'ok' if got == want else 'MISMATCH (want %s)' % want))

if LOG and LOG.is_file():
    print('\n3. %s: music_queue_kc rows and the session rows next to them' % LOG.name)
    for line in LOG.read_text(errors='replace').splitlines():
        if line.startswith('music_queue_kc '):
            f = dict(kv.split('=', 1) for kv in line.split()[1:] if '=' in kv)
            print('  kc frame=%s action=%s build=%s size=%s' % (f.get('frame'), f.get('action'), f.get('build'), f.get('size')))
        elif line.startswith('session_end') or (line.startswith('music_trace_stop ') and 'name=session_start' in line):
            print('  ' + ' '.join(w for w in line.split() if w.split('=')[0] in ('music_trace_stop', 'session_end', 'frame', 'frames', 'name', 'caller')))
