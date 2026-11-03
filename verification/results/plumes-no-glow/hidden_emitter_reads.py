#!/usr/bin/env python3
"""Read-only static checks for docs/architecture/plumes-no-glow-hulls.md step 1 (hidden fx_engine_emitter anchors) on
both executables: (1) the per-frame jet drive reads the mode word C at node +0x260, hides a main jet without the
visible bit (+0x12c |= 0x100000) and still reaches the rate limit and the scale setter (+0x88 driven while hidden);
(2) the world/camera transform walks contain no test of the hidden bit (a hidden child's +0xb0 and basis are written
by the same code as its visible siblings'). Prints addresses and verdicts only, never listings or bytes.

  python3 verification/results/plumes-no-glow/hidden_emitter_reads.py [--ap /mnt/c/X3/X3AP.exe] [--fl /mnt/c/X3FL/X3FL.exe]
"""
import argparse
import hashlib
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
from inspect_x3 import PE32  # noqa: E402
import capstone  # noqa: E402

HASH = dict(ap='fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab',
            fl='e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8')
# Per edition: the drive window (C read, the main/visible branch, both hide stores, the rate-limit compare on +0x88, the
# setter call), the transform-walk windows (whole functions) that must not test the hidden bit, the sector update's
# class switch (only class 7 reaches the drive) and the placement loop (every JET child to z 0x28f, hidden): together
# the ship-only gate of the anchor rule's driven-throttle guard.
SPEC = dict(
    ap=dict(drive=(0x45ad74, 0x45b09f), c_read=0x45ad85, hide=(0x45af51, 0x45afae), z_read=0x45b00c, setter_call=0x45b09a,
            setter=0x488270, main_mark=(0x45af2a, 'dword ptr [esp + 0x38], 0'), main_skip=(0x45b047, 'je 0x45b08d'),
            walks=((0x47b800, 0x47bc1f), (0x47bc20, 0x47c3c9)),
            dispatch=(0x45b64c, 0x45b634, 0x45aca3), placement=(0x451cd5, 0x451cf7)),
    fl=dict(drive=(0x4646a6, 0x4649c3), c_read=0x4646b6, hide=(0x464880, 0x4648ce), z_read=0x464932, setter_call=0x4649be,
            setter=0x4a3ae0, main_mark=(0x464857, 'dword ptr [esp + 0x80], 0'), main_skip=(0x46496d, 'je 0x4649b1'),
            walks=((0x494370, 0x494534), (0x4947b0, 0x494af9), (0x494b00, 0x494f7f)),
            dispatch=(0x464fc0, 0x464fa8, 0x4645e0), placement=(0x45b830, 0x45b852)),
)
HIDDEN_TEST = re.compile(r'^(test|and|bt) .*(0x12c\], 0x100000|0x12e\], 0x10)$')


def disasm(data, pe, start, end):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    off = pe.offset(start - pe.base)
    return list(md.disasm(data[off:off + (end - start)], start))


def check(edition, path):
    data = Path(path).read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    pe = PE32(data)
    s = SPEC[edition]
    out = [f'{edition}: {path} sha256 {digest[:16]}… {"(documented)" if digest == HASH[edition] else "(OTHER EXE)"}']
    ok = digest == HASH[edition]
    drive = {i.address: f'{i.mnemonic} {i.op_str}' for i in disasm(data, pe, *s['drive'])}
    c = drive.get(s['c_read'], '')
    hides = [drive.get(a, '') for a in s['hide']]
    z = drive.get(s['z_read'], '')
    call = drive.get(s['setter_call'], '')
    facts = [
        ('C read at +0x260', c.startswith('mov') and c.endswith('+ 0x260]')),
        ('both hide stores set 0x100000 on +0x12c', all(h.startswith('or dword ptr [') and h.endswith('+ 0x12c], 0x100000') for h in hides)),
        ('rate limit reads +0x88', z.startswith('mov') and z.endswith('+ 0x88]')),
        (f'setter call {s["setter"]:#x}', call == f'call {s["setter"]:#x}'),
        ('the main branch clears the non-main marker', drive.get(s['main_mark'][0], '') == 'mov ' + s['main_mark'][1]),
        ('a main jet skips the non-main show/hide step straight to the setter', drive.get(s['main_skip'][0], '') == s['main_skip'][1]),
        ('no +0x12c hidden-bit memory test in the drive window', not any(HIDDEN_TEST.match(t) for t in drive.values())),
    ]
    for start, end in s['walks']:
        ins = disasm(data, pe, start, end)
        tests = [f'{i.address:#x}' for i in ins if HIDDEN_TEST.match(f'{i.mnemonic} {i.op_str}')]
        writes = sum(1 for i in ins if i.mnemonic.startswith('mov') and '+ 0xb0], ' in i.op_str)
        facts.append((f'walk {start:#x}..{end:#x}: {len(ins)} instructions, +0xb0 stores {writes}, hidden-bit tests {tests or "none"}',
                      not tests))
    # The ship-only gate: the sector update's class switch (byte index table, dword target table) sends class 7 alone to
    # the drive entry; placement scales every JET child to (1, 1, 0x28f) and hides it with no class test in between.
    index_va, table_va, drive_entry = s['dispatch']
    index = data[pe.offset(index_va - pe.base):][:11]
    table_raw = data[pe.offset(table_va - pe.base):][:4 * (max(index) + 1)]
    targets = [int.from_bytes(table_raw[4 * b:4 * b + 4], 'little') for b in index]
    to_drive = [cls for cls, t in enumerate(targets) if t == drive_entry]
    facts.append((f'class switch: classes reaching the drive {drive_entry:#x}: {to_drive}', to_drive == [7]))
    place = disasm(data, pe, s['placement'][0], s['placement'][1] + 10)
    text = [f'{i.mnemonic} {i.op_str}' for i in place]
    facts.append(('placement: JET test, push 0x28f, setter, hide; no class read',
                  text[0].endswith('+ 0x130], 1') and 'push 0x28f' in text and f'call {s["setter"]:#x}' in text and
                  any(t.endswith('+ 0x12c], 0x100000') or t.endswith('+ 0x12c], edi') for t in text) and
                  not any('+ 0x48]' in t for t in text)))
    for name, good in facts:
        out.append(f'  {"PASS" if good else "FAIL"} {name}')
        ok = ok and good
    return ok, out


def main(argv=None):
    a = argparse.ArgumentParser()
    a.add_argument('--ap', default='/mnt/c/X3/X3AP.exe')
    a.add_argument('--fl', default='/mnt/c/X3FL/X3FL.exe')
    args = a.parse_args(argv)
    ok = True
    for edition in ('ap', 'fl'):
        good, lines = check(edition, getattr(args, edition))
        ok = ok and good
        print('\n'.join(lines))
    print('PASS' if ok else 'FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
