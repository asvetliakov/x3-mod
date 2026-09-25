#!/usr/bin/env python3
"""Cost of the proposed launch-time overlay check (docs/architecture/lod-overlay-mods.md section 2) and
the EXE/registry facts of section 1. Read-only; no Wine. Run with the game closed:
    python3 verification/results/lod-overlay-mods/launch_check_cost.py > ..._out.txt"""
import hashlib, json, struct, time
from pathlib import Path

bottle = Path.home() / 'Library/Application Support/CrossOver/Bottles/X3'
g = bottle / 'drive_c/X3'

t = time.perf_counter()
markers = [json.loads(p.read_text()) for p in sorted((g / 'addon').glob('*.x3m-lod.json'))]
size = sum(p.stat().st_size for p in (g / 'addon').glob('*.x3m-lod.json'))
print(f'markers: {len(markers)} files {size} B parsed in {(time.perf_counter() - t) * 1000:.0f} ms;'
      f' slots {[m["slot"] for m in markers]} bodies {[len(m["bodies"]) for m in markers]}'
      f' originals {[m["originals"] for m in markers]} mode {[m["originals_mode"] for m in markers]}')

t = time.perf_counter()
cats = sorted(g.glob('[0-9][0-9].cat')) + sorted((g / 'addon').glob('[0-9][0-9].cat'))
stats = [(p.stat(), p.with_suffix('.dat').stat()) for p in cats]
hashes = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in cats}
print(f'stat {2 * len(cats)} cat/dat files + sha256 of {len(cats)} cats'
      f' ({sum(p.stat().st_size for p in cats)} B): {(time.perf_counter() - t) * 1000:.1f} ms')

t = time.perf_counter()
reg = (bottle / 'user.reg').read_text(errors='replace')
i = reg.find('[Software\\\\EGOSOFT\\\\X3AP]')
j = reg.find('"ModName"', i)
print(f'registry: {reg[j:reg.find(chr(10), j)]} ({(time.perf_counter() - t) * 1000:.1f} ms, user.reg {len(reg)} chars)')

d = (g / 'X3AP.exe').read_bytes()
pe = struct.unpack_from('<I', d, 0x3c)[0]
nsec = struct.unpack_from('<H', d, pe + 6)[0]
base = struct.unpack_from('<I', d, pe + 24 + 28)[0]
secs = []
for k in range(nsec):
    s = pe + 24 + struct.unpack_from('<H', d, pe + 20)[0] + 40 * k
    va, vs, raw, rs = struct.unpack_from('<IIII', d, s + 8)
    secs.append((d[s:s + 8].rstrip(b'\0').decode(), va, raw, rs))
def to_va(off):
    for name, va, raw, rs in secs:
        if raw <= off < raw + rs:
            return f'0x{base + va + off - raw:08x} ({name})'
    return '?'
for needle in (b'ModName\0', b'addon\\mods\\%s.cat\0', b'Software\\EGOSOFT\\%s\0', b'addon\\mods\0', b'*.cat\0'):
    off = d.find(needle)
    print(f'exe string {needle!r}: file offset 0x{off:x} va {to_va(off)}' if off >= 0 else f'exe string {needle!r}: absent')
