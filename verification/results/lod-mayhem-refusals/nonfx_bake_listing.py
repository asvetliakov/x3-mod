#!/usr/bin/env python3
"""Capstone listings of the installed X3AP.exe behind docs/reverse-engineering/non-effect-materials.md sections
6 to 8, and the searches its claims rest on. Reads the EXE only.

  python3 nonfx_bake_listing.py list 0x004c0866 0x004c09d2 > <local file outside the repository>
  python3 nonfx_bake_listing.py evidence > nonfx_bake_listing_out.txt

`list` prints the raw listing of a range (strings and float constants annotated): raw disassembly stays local and
untracked. `evidence` prints only the instructions the searches match (address, mnemonic, operands):
  writes   instructions of a range with a written memory operand at one of the given displacements (capstone
           operand access, any base register, no index)
  strings  instructions of a range whose operand is the address of a printable string
  byte     instructions anywhere in the code section that access a byte at displacement 0xa0 off a register other
           than esp (the device byte of section 6), found by the displacement bytes and decoded from 2..4 bytes back
Ranges listed for the note (capstone 5.0.7): 0x004c0440..0x004c0624, 0x004c0624..0x004c09d2, 0x004c0dd0..0x004c13a0,
0x004c1700..0x004c1978, 0x004c1972..0x004c19e0, 0x004c1eab..0x004c2a60, 0x004c3140..0x004c3420,
0x00481780..0x00481aa0, 0x00481ee0..0x004820a0, 0x00482096..0x004823b0, 0x004ba500..0x004bae10,
0x004d9b40..0x004d9fe0, 0x004da590..0x004da7a0, 0x004b9010..0x004b9060.
"""
import struct
import sys
from pathlib import Path

import capstone

EXE = Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/X3AP.exe'
RECORD_SLOTS = (0x02, 0x1c, 0x2e, 0x32, 0x36)     # diffuse, specular, map 0, bump, light ids of a material record


class Image:
    def __init__(self, path=EXE):
        self.d = d = Path(path).read_bytes()
        pe = struct.unpack_from('<I', d, 0x3c)[0]
        count, opt = struct.unpack_from('<H', d, pe + 6)[0], struct.unpack_from('<H', d, pe + 20)[0]
        base = struct.unpack_from('<I', d, pe + 24 + 28)[0]
        self.sections = []
        for i in range(count):
            vs, va, rs, ro = struct.unpack_from('<IIII', d, pe + 24 + opt + 40 * i + 8)
            self.sections.append((base + va, max(vs, rs), ro, rs))
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        self.md.detail = True

    def read(self, va, n):
        for a, s, ro, rs in self.sections:
            if a <= va < a + s and va - a < rs:
                return self.d[ro + va - a:ro + va - a + n]
        return None

    def string(self, va):
        b = self.read(va, 80)
        b = b.split(b'\0')[0] if b else b''
        return b.decode('latin1') if len(b) >= 2 and all(32 <= c < 127 for c in b) else None

    def insns(self, a, b):
        return self.md.disasm(self.read(a, b - a), a)

    def note(self, ins):
        out = ''
        for op in ins.operands:
            v = (op.imm & 0xffffffff if op.type == capstone.x86.X86_OP_IMM else
                 op.mem.disp & 0xffffffff if op.type == capstone.x86.X86_OP_MEM and not op.mem.base
                 and not op.mem.index else None)
            if v and 0x500000 <= v < 0x700000:
                s = self.string(v)
                if s:
                    out += f'  ; "{s}"'
                elif op.type == capstone.x86.X86_OP_MEM and ins.mnemonic[0] == 'f' and self.read(v, 8):
                    out += (f'  ; f32 {struct.unpack("<f", self.read(v, 4))[0]:g}' if op.size == 4 else
                            f'  ; f64 {struct.unpack("<d", self.read(v, 8))[0]:g}')
        return out

    def line(self, ins):
        return f'{ins.address:08x}  {ins.mnemonic} {ins.op_str}{self.note(ins)}'

    def writes(self, a, b, disps):
        return [self.line(i) for i in self.insns(a, b) for op in i.operands
                if op.type == capstone.x86.X86_OP_MEM and op.access & capstone.CS_AC_WRITE and op.mem.base
                and not op.mem.index and op.mem.disp in disps]

    def strings(self, a, b):
        return [self.line(i) for i in self.insns(a, b) if '; "' in self.note(i)]

    def byte_access(self, disp):
        a, _, ro, rs = self.sections[0]
        blob, out, seen = self.d[ro:ro + rs], [], set()
        k = blob.find(struct.pack('<I', disp))
        while k >= 0:
            for back in (2, 3, 4):
                ins = next(self.md.disasm(blob[k - back:k - back + 16], a + k - back), None)
                if ins and ins.size >= back + 4 and f'+ {disp:#x}]' in ins.op_str and 'byte ptr' in ins.op_str \
                        and 'esp' not in ins.op_str and ins.address not in seen and ins.mnemonic in ('mov', 'cmp'):
                    seen.add(ins.address)
                    out.append(self.line(ins))
            k = blob.find(struct.pack('<I', disp), k + 1)
        return sorted(out)


def evidence(im):
    show = lambda title, rows: print(f'{title}: {len(rows)}' + ''.join('\n  ' + r for r in rows))
    print(f'{EXE.name}, {len(im.d)} bytes; capstone {capstone.__version__}')
    print('== section 7: writes to the texture id words of a material record '
          f'(displacements {", ".join(hex(x) for x in RECORD_SLOTS)})')
    show('effect record post-load 0x00481780..0x00481a92 (record in ebx)', im.writes(0x00481780, 0x00481a92, RECORD_SLOTS))
    show('MAT6 loader, record index to the end of the effect branch 0x00481f50..0x00482019 (record in edi)',
         im.writes(0x00481f50, 0x00482019, RECORD_SLOTS))
    show('MAT6 loader, classic branch 0x00482019..0x004823a7 (the positive control: the same search finds the'
         ' classic writes)', im.writes(0x00482019, 0x004823a7, RECORD_SLOTS))
    print('  not searched: the effect reader 0x00470490 (called at 0x00481fb3 with the new effect object and the'
          ' stream; the record pointer is not among its arguments) and callees of the two ranges')
    show('== section 8: strings named by the effect path 0x004c0866..0x004c09d2', im.strings(0x004c0866, 0x004c09d2))
    show('== section 8: strings named by the classic technique choice 0x004c0a79..0x004c0b90',
         im.strings(0x004c0a79, 0x004c0b90))
    show('== section 8: colour parameters recorded after BeginParameterBlock 0x004c0df9..0x004c0e83',
         im.strings(0x004c0df9, 0x004c0e83))
    show('== section 6: byte accesses at +0xa0 (mov = the profile selection, cmp = the readers)', im.byte_access(0xa0))
    show('== section 6: profile names around the byte writes 0x004d9c68..0x004d9f95', im.strings(0x004d9c68, 0x004d9f95))
    show('== section 6: placeholder names loaded at device init 0x004da6e0..0x004da7a0', im.strings(0x004da6e0, 0x004da7a0))


def main():
    im = Image()
    if sys.argv[1:2] == ['list'] and len(sys.argv) == 4:
        print('\n'.join(im.line(i) for i in im.insns(int(sys.argv[2], 16), int(sys.argv[3], 16))))
    elif sys.argv[1:] == ['evidence']:
        evidence(im)
    else:
        raise SystemExit(__doc__)


main()
