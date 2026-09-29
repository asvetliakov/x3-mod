#!/usr/bin/env python3
"""Instructions the cull_small_parts stub executes per node, before and after the dock-port rule (2026-09-29).

Disassembles the stub from the verifier's encoder twin (verification/probe/verify_cull_small_parts_site.py, pinned
byte-for-byte to src/proxy/cull_small_parts_core.h by test_cull_small_parts) and, with --before <git rev>, the same
encoder at that revision, then walks each path with the branch outcomes the path implies, up to the jump into the tail
(`jmp [next]`) or the engine's cull instruction. Prints counts only. Needs i686-w64-mingw32-objdump. No Wine.
Usage: dock_stub_paths.py [--before <rev>]"""
import argparse, importlib.util, re, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'verification/probe'))
AT = 0x10000000


def load(rev):
    if rev is None:
        import verify_cull_small_parts_site as module
        return module
    text = subprocess.run(['git', '-C', str(ROOT), 'show', f'{rev}:verification/probe/verify_cull_small_parts_site.py'],
                          capture_output=True, text=True, check=True).stdout
    with tempfile.NamedTemporaryFile('w', suffix='.py', delete=False) as handle:
        handle.write(text)
    spec = importlib.util.spec_from_file_location('verify_before', handle.name)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def disassemble(code):
    with tempfile.NamedTemporaryFile(suffix='.bin', delete=False) as handle:
        handle.write(code)
    out = subprocess.run(['i686-w64-mingw32-objdump', '-D', '-b', 'binary', '-m', 'i386', '-M', 'intel', f'--adjust-vma={AT:#x}', handle.name],
                         capture_output=True, text=True, check=True).stdout
    listing = {}
    for line in out.splitlines():
        m = re.match(r'\s*([0-9a-f]+):\t(?:[0-9a-f]{2} )+\s*\t(\S+)\s*(.*)', line)
        if m:
            listing[int(m.group(1), 16) - AT] = (m.group(2), m.group(3))
    return listing


def walk(listing, taken):
    """Counts instructions from offset 0; `taken(mnemonic, offset)` decides each conditional branch."""
    at, count = 0, 0
    while True:
        mnemonic, operand = listing[at]
        count += 1
        if mnemonic == 'jmp':
            if 'DWORD PTR' in operand:
                return count, 'tail'
            target = int(operand, 16) - AT
            if not 0 <= target < 0x1000:
                return count, 'cull'
            at = target
            continue
        nxt = min(k for k in listing if k > at)
        if mnemonic.startswith('j'):
            at = int(operand, 16) - AT if taken(mnemonic, at) else nxt
        else:
            at = nxt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before', default=None, help='git revision of the encoder before the dock rule')
    args = parser.parse_args()
    now = load(None)
    code = now.encode_stub(AT, 0x10002000, 0x1000200c, 0x10002004, 0x10002008, 0x10002010, now.CULL_VA, 0x10000094)
    listing = disassemble(code)
    # Branches of the new stub by offset: 7 jle (upper <= 0), 19 jge (s >= upper), 30 jl (s < threshold), 48/60 jb (dock id),
    # 80/101 jne (projectile marker), 120/130 the replayed je/jle (parent limit).
    cases = {
        'disarmed (upper 0)': {7: True},
        's >= upper (every node above both thresholds)': {7: False, 19: True},
        's < threshold (small cull, no parent)': {7: False, 19: False, 30: True, 101: False, 120: True},
        'threshold <= s < upper, not a dock id': {7: False, 19: False, 30: False, 48: False, 60: False},
        'threshold <= s < upper, first-range dock id (cull, no parent)': {7: False, 19: False, 30: False, 48: True, 80: False, 120: True},
        'threshold <= s < upper, M6-range dock id (cull, no parent)': {7: False, 19: False, 30: False, 48: False, 60: True, 80: False, 120: True},
    }
    print(f'new stub: {len(code)} bytes, {len(listing)} instructions')
    for name, outcome in cases.items():
        n, end = walk(listing, lambda mnemonic, at, o=outcome: o[at])
        print(f'  {name}: {n} instructions -> {end}')
    if args.before:
        old = load(args.before)
        code = old.encode_stub(AT, 0x10002000, 0x10002004, 0x10002008, old.CULL_VA, 0x10000054)
        listing = disassemble(code)
        # Old branches: 7 jle (threshold <= 0), 20 jge (s >= threshold), 32 jne (marker), 45/55 the replayed je/jle.
        cases = {'disarmed (threshold 0)': {7: True}, 's >= threshold': {7: False, 20: True},
                 's < threshold (small cull, no parent)': {7: False, 20: False, 32: False, 45: True}}
        print(f'before ({args.before}): {len(code)} bytes, {len(listing)} instructions')
        for name, outcome in cases.items():
            n, end = walk(listing, lambda mnemonic, at, o=outcome: o[at])
            print(f'  {name}: {n} instructions -> {end}')


if __name__ == '__main__':
    main()
