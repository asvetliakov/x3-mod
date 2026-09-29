#!/usr/bin/env python3
"""Read-only static checks behind docs/reverse-engineering/script-task-scheduler.md.

For every proposed probe site: exact bytes, whole instructions in the decode of
the containing routine, the direct edges that land on the span start, no direct
branch (decoded or raw .text encoding, including the interpreter's opcode jump
table) into a span interior, the rel32 target of displaced calls, and no aligned
dword data reference to a span byte outside .rsrc. Also prints the raw `e8`
callers of the routines the note's single-caller claims rely on.
No Wine, no game. Usage: python3 check_cutevent_sites.py [X3AP.exe]
"""
import hashlib, re, struct, subprocess, sys
from pathlib import Path

EXE = Path(sys.argv[1]) if len(sys.argv) > 1 else Path.home() / \
    'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/X3AP.exe'
OBJDUMP = 'i686-w64-mingw32-objdump'
BASE = 0x400000
ROUTINES = {  # [start, end) of each decoded routine
    '0048ee90': (0x48ee90, 0x48f2b0), '0048f2b0': (0x48f2b0, 0x48f54c),
    '0048f550': (0x48f550, 0x48f693), '004a26a0': (0x4a26a0, 0x4a4490),
}
SITES = [  # name, va, bytes, routine, rel32 offset or None, expected target
    ('cutevent_views',  0x48f57a, '8b0d18856000', '0048f550', None, None),
    ('cutevent_cuts',   0x48f616, '8b1518856000', '0048f550', None, None),
    ('node_visit',      0x48f2e4, '8b8d58020000', '0048f2b0', None, None),
    ('node_track',      0x48f366, '8b0d346f6000', '0048f2b0', None, None),
    ('node_animate',    0x48f50d, 'e80ee7ffff',   '0048f2b0', 1, 0x48dc20),
    ('cut_frame_call',  0x48f452, 'e829020100',   '0048f2b0', 1, 0x49f680),
    ('cut_event_call1', 0x48f0e2, 'e899050100',   '0048ee90', 1, 0x49f680),
    ('cut_event_call2', 0x48f1db, 'e8a0040100',   '0048ee90', 1, 0x49f680),
    ('native_call',     0x4a38ff, '8b825414000056 50ffd1'.replace(' ', ''), '004a26a0', None, None),
]
CALLEES = [0x48f550, 0x48f2b0, 0x48ee90, 0x48dc20, 0x49f680, 0x49f770, 0x4a26a0, 0x49f430]

data = EXE.read_bytes()
pe = struct.unpack_from('<I', data, 0x3c)[0]
nsec = struct.unpack_from('<H', data, pe + 6)[0]
opt = struct.unpack_from('<H', data, pe + 20)[0]
secs = []
for i in range(nsec):
    o = pe + 24 + opt + 40 * i
    name = data[o:o + 8].rstrip(b'\0').decode()
    vsize, rva, rsize, raw = struct.unpack_from('<IIII', data, o + 8)
    secs.append((name, rva, max(vsize, rsize), raw, rsize))
def f2va(off):
    for n, rva, vs, raw, rs in secs:
        if raw <= off < raw + rs: return BASE + rva + off - raw, n
    return None, '?'
def rd(va, n):
    for _, rva, vs, raw, rs in secs:
        if rva <= va - BASE < rva + rs: return data[raw + va - BASE - rva: raw + va - BASE - rva + n]
text = next(s for s in secs if s[0] == '.text')

insns, edges = {}, {}
for key, (a, b) in ROUTINES.items():
    out = subprocess.run([OBJDUMP, '-d', '-Mintel', '--no-show-raw-insn', f'--start-address={a:#x}',
                          f'--stop-address={b:#x}', str(EXE)], check=True, capture_output=True, text=True).stdout
    starts, br = [], []
    for line in out.splitlines():
        m = re.match(r'\s+([0-9a-f]+):\s+(\S.*)', line)
        if not m: continue
        va = int(m.group(1), 16); starts.append(va)
        m2 = re.match(r'(j\w+|loop\w*)\s+0x([0-9a-f]+)', m.group(2))
        if m2: br.append((va, int(m2.group(2), 16)))
    insns[key], edges[key] = set(starts) | {b}, br
# the interpreter's two-level opcode table (byte table 0x4a4688, target table 0x4a4490)
bt = rd(0x4a4688, 0xb1); jt = [struct.unpack_from('<I', rd(0x4a4490, 4 * (max(bt) + 1)), 4 * i)[0] for i in range(max(bt) + 1)]
edges['004a26a0'] += [(0x4a2707, t) for t in jt]

def raw_targets():
    s, e = text[3], text[3] + text[4]
    for off in range(s, e - 6):
        va = BASE + text[1] + off - s; b = data[off]
        if b in (0xe8, 0xe9): yield va, (va + 5 + struct.unpack_from('<i', data, off + 1)[0]) & 0xffffffff, b
        elif b == 0x0f and 0x80 <= data[off + 1] <= 0x8f: yield va, (va + 6 + struct.unpack_from('<i', data, off + 2)[0]) & 0xffffffff, b
        elif b == 0xeb or 0x70 <= b <= 0x7f or 0xe0 <= b <= 0xe3: yield va, (va + 2 + struct.unpack_from('<b', data, off + 1)[0]) & 0xffffffff, b
interior = {a: n for n, va, hx, *_ in SITES for a in range(va + 1, va + len(hx) // 2)}
raw_hits, callers = [], {c: [] for c in CALLEES}
for at, tgt, b in raw_targets():
    if tgt in interior: raw_hits.append((interior[tgt], at, tgt))
    if b == 0xe8 and tgt in callers: callers[tgt].append(at)

print('exe', EXE.name, hashlib.sha256(data).hexdigest())
ok_all = True
for name, va, hx, key, rel, tgt in SITES:
    n = len(hx) // 2; got = rd(va, n).hex()
    bytes_ok = got == hx
    whole = va in insns[key] and va + n in insns[key]
    into = [(f, t) for f, t in edges[key] if va < t < va + n]
    onto = sorted({f for f, t in edges[key] if t == va})
    tgt_ok = True
    if rel is not None:
        field = struct.unpack('<i', rd(va + rel, 4))[0]
        tgt_ok = (va + rel + 4 + field) & 0xffffffff == tgt
    drefs = []
    for a in range(va, va + n):
        i = data.find(struct.pack('<I', a))
        while i >= 0:
            v, sec = f2va(i)
            if sec != '.rsrc' and i % 4 == 0: drefs.append((hex(a), sec, hex(i)))
            i = data.find(struct.pack('<I', a), i + 1)
    raw_in = [h for h in raw_hits if h[0] == name]
    # a raw hit only matters when its encoding starts on a real instruction of
    # a decoded routine; other hits are byte coincidences inside operands/data
    raw_real = [h for h in raw_in if any(h[1] in insns[k] for k in insns)]
    ok = bytes_ok and whole and not into and tgt_ok and not raw_real and not drefs
    ok_all &= ok
    print(f'{name:16s} {va:#010x} len={n:2d} bytes={"ok" if bytes_ok else got} whole={whole} '
          f'edges_onto={[hex(x) for x in onto]} into={len(into)} raw_into={[(hex(h[1]), hex(h[2])) for h in raw_in]} '
          f'rel32={"ok" if tgt_ok else "BAD"}{"" if rel is None else f"->{tgt:#x}"} data_refs={len(drefs)} '
          f'{"PASS" if ok else "FAIL"}')
for c, sites in callers.items():
    print(f'callers {c:#010x}: {len(sites)} {[hex(s) for s in sites[:8]]}')
print('RESULT', 'PASS' if ok_all else 'FAIL')
