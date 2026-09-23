#!/usr/bin/env python3
"""Distance-fade (engine "fog") reproduction for docs/reverse-engineering/distance-fade.md.

Read-only: checks the site bytes quoted by the note in the installed X3AP.exe,
recomputes g_FogClip (c41) the way 0x004c2d43..0x004c2df2 does, reproduces the
Run 27 asteroid constants, and tabulates the effective near/far pair of every
shipped sector per View Distance setting from sector-fog-census.csv.
No game, no Wine.  Usage: python3 verification/results/distance-fade-decode.py
"""
import collections, csv, hashlib, os, struct

EXE = os.path.expanduser('~/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/X3AP.exe')
CENSUS = os.path.join(os.path.dirname(__file__), '..', '..', 'docs', 'reverse-engineering', 'sector-fog-census.csv')

SITES = {  # VA: expected bytes (hex) - instruction text in the note
    0x004c2b56: '8b450cf7802c010000000000020f859f020000',  # node flag test + JNZ bypass
    0x004c2c63: '2b866c0300003bc8894424187e4b',            # SUB F-N / CMP F-D / JLE fog
    0x0047d146: 'f7c20000000275',                          # distance-cull exemption test
    0x0047ea38: 'f7862c010000000000020f8506010000',        # fog-band walk exemption test
    0x004418d7: '6800000002506890714800',                   # planet tree visit push 0x2000000
    0x00441639: '837c0a5c120f850805000081882c01000000000080',  # dock/factory +0x5c==0x12 -> 0x80000000
    0x00441d22: '814d40040000808b8c244c010000',            # constructor common tail
    0x00421548: '8b940848010000',                          # FogNear row +0x148 -> camera
}

def pe_reader(data):
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    nsec = struct.unpack_from('<H', data, pe + 6)[0]
    opt = struct.unpack_from('<H', data, pe + 20)[0]
    base = struct.unpack_from('<I', data, pe + 24 + 28)[0]
    secs = [struct.unpack_from('<8sIIII', data, pe + 24 + opt + 40 * i) for i in range(nsec)]
    def read(va, n):
        rva = va - base
        for _, vs, sva, rs, ro in secs:
            if sva <= rva < sva + max(vs, rs):
                return data[ro + rva - sva: ro + rva - sva + n]
        raise ValueError(hex(va))
    return base, read

def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]

def fogclip(n, f, scale_bits):
    s = struct.unpack('<f', struct.pack('<I', scale_bits))[0]
    ns, fs = n * s, f * s          # x87 extended in the engine; float64 here
    return f32(fs / (fs - ns)), f32(1.0 / (fs - ns))

def f_eff(far, view_distance):
    if view_distance >= 3:
        return max(far, 500_000_000)
    if view_distance == 2:
        return max(far, 100_000_000)
    return far

def main():
    data = open(EXE, 'rb').read()
    print('exe sha256', hashlib.sha256(data).hexdigest())
    base, read = pe_reader(data)
    ok = 0
    for va, hx in SITES.items():
        want = bytes.fromhex(hx)
        got = read(va, len(want))
        ok += got == want
        print(f'site {va:#010x} {"match" if got == want else "MISMATCH " + got.hex()}')
    print(f'sites matched {ok}/{len(SITES)}')

    run27 = (1.0526316166, 2.10526366e-7)
    for bits in (0x3c23d708, 0x3c23d70a):
        x, y = fogclip(25_000_000, 500_000_000, bits)
        print(f'run27 N=25e6 F=500e6 scale={bits:#x}: c41=({x!r}, {y!r}) '
              f'rel.err=({abs(x-run27[0])/run27[0]:.1e}, {abs(y-run27[1])/run27[1]:.1e})')

    rows = list(csv.DictReader(open(CENSUS)))
    pairs = collections.Counter((int(r['near_native']), int(r['far_native'])) for r in rows)
    inside = sum(int(r['near_native']) < int(r['sector_size_native']) for r in rows)
    print(f'sectors {len(rows)}, FogNear < sector size in {inside}')
    km = lambda v: v / 500 / 1000
    print('count near_km far_km | F_eff_km low/med(<2) high(2) very_high(>=3)')
    for (n, f), c in sorted(pairs.items(), key=lambda kv: (-kv[1], kv[0])):
        print(f'{c:3d} {km(n):7.1f} {km(f):7.1f} | ' + ' '.join(f'{km(f_eff(f, v)):7.1f}' for v in (1, 2, 3)))

if __name__ == '__main__':
    main()
