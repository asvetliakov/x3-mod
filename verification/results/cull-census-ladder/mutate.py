# Mutation check of verify_cull_census_sites.py's ladder checks (review, 2026-09-23): one-byte
# mutations of the installed X3AP.exe, each must fail the named check; output in mutate_out.txt.
# Run from verification/probe:  /usr/bin/python3 ../results/cull-census-ladder/mutate.py <scratch dir>
# <scratch dir> must be outside the repository: it receives mut.exe, a mutated copy of the game EXE.
import sys; sys.path.insert(0, '.')
from pathlib import Path
import verify_cull_census_sites as v, verify_chase_aim_sites as c
S = Path(sys.argv[1]); data = bytearray(Path(v.DEFAULT_EXE).read_bytes()); img = c.Image(bytes(data))
def off(va):
    for name, base, vsize, rp, rsize in img.sections:
        if base <= va < base + vsize: return rp + va - base
M = {'M1 model-slot writer ->[esp+0x18]': (0x47d30d, b'\x18'), 'M2 new ebx writer 0x47d45c sub ebx,1': (0x47d45d, b'\xeb'),
     'M3 D-slot writer ->[esp+0xc]': (0x47d1cb, b'\x0c'), 'M4 restore from D slot': (0x47d471, b'\x10'),
     'M5 new esp writer 0x47d42b mov esp,ebp': (0x47d42c, b'\xe5'), 'M6 ebx write outside window 0x47d4e1 movsx ebx': (0x47d4e3, b'\x5b'),
     'M0 unmodified': (None, None)}
for name, (va, b) in M.items():
    d = bytearray(data)
    if va: d[off(va):off(va)+len(b)] = b
    p = S / 'mut.exe'; p.write_bytes(d)
    r = v.verify(p)
    failed = sorted(k for k, ok in r['checks'].items() if not ok)
    print(name, '->', r['result'], 'failed:', failed)
