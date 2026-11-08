#!/usr/bin/env python3
"""One-liner for the FL weapon hook port (docs/verification/weapon-effects.md, FL hook port 2026-11-08): print the
check counts and verdicts of the three native records beside the FL EXE window match. Reads only the committed
records and the pinned FL EXE bytes; never launches the game.

  python3 -I verification/results/native-windows/weapon-effects/fl_hook_summary.py
"""
import hashlib
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
HERE = ROOT / 'verification/results/native-windows/weapon-effects'
FL_EXE = Path('/mnt/c/X3FL/X3FL.exe')
HEADER = ROOT / 'src/engine/fl/weapon_effects_sites_fl.h'


def record(name):
    d = json.loads((HERE / name).read_text())
    r = (d.get('run') or {}).get('result') or {}
    return {'passed': d.get('passed'), 'checks': d.get('check_count') or len(d.get('checks', [])),
            'verdict': r.get('verdict'), 'failures': r.get('failures')}


def fl_windows_match():
    import re
    if not FL_EXE.is_file():
        return None
    data = FL_EXE.read_bytes()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    n, opt = struct.unpack_from('<H', data, pe + 6)[0], struct.unpack_from('<H', data, pe + 20)[0]
    sec = [struct.unpack_from('<IIII', data, pe + 24 + opt + 40 * i + 8) for i in range(n)]

    def at(rva, size):
        for vs, va, rs, ra in sec:
            if va <= rva < va + max(vs, rs):
                return data[ra + rva - va:ra + rva - va + size]
        raise AssertionError(rva)
    text = HEADER.read_text()
    arrays = {m.group(1): bytes(int(x, 16) for x in re.findall(r'0x([0-9a-f]{2})', m.group(2)))
              for m in re.finditer(r'constexpr unsigned char (\w+)\[\d+\] = \{([^}]*)\}', text)}
    ok = {}
    for name, rva, length, var, fixoff in re.findall(
            r'\{\{"(\w+)", (0x[0-9a-f]+), (\d+), (\w+), (no_fixup|\d+), [^,]+, 0x[0-9a-f]+\}, \d+, 0x[0-9a-f]+\}', text):
        live = bytearray(at(int(rva, 16), len(arrays[var])))
        if fixoff != 'no_fixup':
            live[int(fixoff):int(fixoff) + 4] = b'\0\0\0\0'
        ok[name] = bytes(live) == arrays[var]
    return ok


if __name__ == '__main__':
    out = {'fl_exe_sha256': hashlib.sha256(FL_EXE.read_bytes()).hexdigest()[:16] if FL_EXE.is_file() else None,
           'fl_windows_match': fl_windows_match(), 'hook_ap': record('hook-summary.json'),
           'hook_fl': record('hook-fl-summary.json'), 'd3d': record('summary.json')}
    print(json.dumps(out, indent=1))
