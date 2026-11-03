"""Shared read-only helpers of the three FL collision verifiers (verify_collide_sat_fl.py, verify_collide_memo_sites_fl.py,
verify_collide_box_cull_sites_fl.py): the PE image with its HIGHLOW census and Capstone, the generated window tables
(src/engine/fl/collide_windows_fl_inc.h) checked against the exe, raw interior-entry scans, the AP refusal controls and a
bounded read-before-write explorer over the engine's control flow. No Wine, no game launch, nothing written to the exe.
"""
import hashlib
import re
import struct
import sys
from pathlib import Path

import capstone
from capstone import x86_const as X

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
from inspect_x3 import PE32  # noqa: E402

INC = ROOT / 'src/engine/fl/collide_windows_fl_inc.h'
DEFAULT_EXE = Path('/mnt/c/X3FL/X3FL.exe')
AP_EXE = Path('/mnt/c/X3AP-orig/X3AP.exe')
FL_SHA256 = 'e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8'
BASE = 0x400000


class Image:
    def __init__(self, data):
        self.data = bytes(data)
        self.sha256 = hashlib.sha256(self.data).hexdigest()
        self.pe = PE32(self.data)
        pe_off = self.pe.u32(0x3c)
        reloc_rva, reloc_size = struct.unpack_from('<II', self.data, pe_off + 24 + 96 + 5 * 8)
        self.fixups = set()
        if reloc_rva:
            o, end = self.pe.offset(reloc_rva), self.pe.offset(reloc_rva) + reloc_size
            while o < end:
                page, size = struct.unpack_from('<II', self.data, o)
                if size < 8:
                    break
                for k in range((size - 8) // 2):
                    e = struct.unpack_from('<H', self.data, o + 8 + 2 * k)[0]
                    if e >> 12 == 3:
                        self.fixups.add(page + (e & 0xfff))
                o += size
        table = pe_off + 24 + struct.unpack_from('<H', self.data, pe_off + 20)[0]
        for k, s in enumerate(self.pe.sections):
            s['characteristics'] = struct.unpack_from('<I', self.data, table + 40 * k + 36)[0]
        text = next(s for s in self.pe.sections if s['name'] == '.text')
        self.text = (text['rva'], text['rva'] + text['raw_size'], text['offset'])
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        self.md.detail = True

    def read(self, rva, n):
        o = self.pe.offset(rva)
        return self.data[o:o + n]

    def u32(self, rva):
        return struct.unpack_from('<I', self.read(rva, 4))[0]

    def insns(self, start, end):
        out = list(self.md.disasm(self.read(start, end - start), BASE + start))
        ok = bool(out) and out[-1].address + out[-1].size == BASE + end
        return out, ok

    def insn(self, rva):
        return next(self.md.disasm(self.read(rva, 16), BASE + rva), None)

    def fixups_in(self, start, end):
        return sorted(f for f in self.fixups if start <= f < end)

    def section(self, rva):
        for s in self.pe.sections:
            if s['rva'] <= rva < s['rva'] + max(s['size'], s['raw_size']):
                return s
        return None


def load(exe):
    return Image(Path(exe).read_bytes())


def fnv1a(data):
    h = 0x811c9dc5
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xffffffff
    return h


def windows(table, text=None):
    """[(name, rva, length, fnv, [(offset, target_rva)])] of one table of the generated header."""
    text = text if text is not None else INC.read_text()
    rows = re.search(r'constexpr CodeWindow %s_windows\[\] = \{(.*?)\n\};' % table, text, re.S).group(1)
    fix = re.search(r'constexpr Fixup %s_fixups\[\] = \{(.*?)\n\};' % table, text, re.S).group(1)
    fixups = [(int(o, 16), int(t, 16)) for o, t in re.findall(r'\{0x([0-9a-f]+), 0x([0-9a-f]+)\}', fix)]
    out = []
    for name, rva, length, first, count, h in re.findall(
            r'\{"([a-z0-9_]+)", 0x([0-9a-f]+), 0x([0-9a-f]+), (\d+), (\d+), 0x([0-9a-f]+)\}', rows):
        first, count = int(first), int(count)
        out.append((name, int(rva, 16), int(length, 16), int(h, 16), fixups[first:first + count]))
    return out


def window_matches(im, rva, length, h, fixups, base=BASE):
    """The production decision (collide_windows_fl.h matches()) over the image's bytes at rva."""
    raw = bytearray(im.read(rva, length))
    if len(raw) != length:
        return False
    for o, t in fixups:
        if struct.unpack_from('<I', raw, o)[0] != (base + t) & 0xffffffff:
            return False
        raw[o:o + 4] = b'\0\0\0\0'
    return fnv1a(bytes(raw)) == h


def check_windows(im, table):
    """Per window: hash, the relocation census equal to the declared fixups, whole instructions, no straddling HIGHLOW."""
    rows, ok = {}, True
    for name, rva, length, h, fixups in windows(table):
        census = [f - rva for f in im.fixups_in(rva - 3, rva + length)]
        straddle = any(o < 0 or o + 4 > length for o in census)
        targets = [(im.u32(rva + o) - BASE) & 0xffffffff for o in census if 0 <= o <= length - 4]
        _, whole = im.insns(rva, rva + length)
        good = (window_matches(im, rva, length, h, fixups) and not straddle and
                census == [o for o, _ in fixups] and targets == [t for _, t in fixups] and whole)
        rows[name] = {'rva': hex(rva), 'length': length, 'fixups': len(fixups), 'ok': good}
        ok = ok and good
    return ok, rows


def mutation_controls(im, table):
    """In-memory copies: one flipped byte per window and one shifted relocated dword per window with a fixup must each
    be refused by the production decision. Returns (refused, total)."""
    refused = total = 0
    for name, rva, length, h, fixups in windows(table):
        raw = bytearray(im.read(rva, length))
        for k in (0, length // 2, length - 1):
            mutated = bytearray(raw)
            mutated[k] ^= 0x01
            total += 1
            refused += not _matches_bytes(mutated, h, fixups)
        if fixups:
            mutated = bytearray(raw)
            o = fixups[0][0]
            struct.pack_into('<I', mutated, o, (struct.unpack_from('<I', mutated, o)[0] + 4) & 0xffffffff)
            total += 1
            refused += not _matches_bytes(mutated, h, fixups)
        # a rebased image: every fixup dword moved by the delta must still be admitted at the new base
        rebased = bytearray(raw)
        for o, _ in fixups:
            struct.pack_into('<I', rebased, o, (struct.unpack_from('<I', rebased, o)[0] + 0x10000000) & 0xffffffff)
        total += 1
        refused += _matches_bytes(rebased, h, fixups, BASE + 0x10000000)  # must match: counted as a pass when it does
    return refused, total


def _matches_bytes(raw, h, fixups, base=BASE):
    raw = bytearray(raw)
    for o, t in fixups:
        if struct.unpack_from('<I', raw, o)[0] != (base + t) & 0xffffffff:
            return False
        raw[o:o + 4] = b'\0\0\0\0'
    return fnv1a(bytes(raw)) == h


def ap_refusal(im, table, ap_rvas):
    """The AP addresses refused: every window of `table` evaluated at each AP RVA in this image, and every window on the
    AP exe (when present at /mnt/c/X3AP-orig/X3AP.exe) at its FL RVA. Returns a dict of counts."""
    tried = refused = 0
    for name, rva, length, h, fixups in windows(table):
        for ap in ap_rvas:
            tried += 1
            refused += not window_matches(im, ap, length, h, fixups)
    out = {'fl_image_at_ap_rvas_tried': tried, 'fl_image_at_ap_rvas_refused': refused}
    if AP_EXE.is_file():
        ap = load(AP_EXE)
        n = r = 0
        for name, rva, length, h, fixups in windows(table):
            n += 1
            try:
                r += not window_matches(ap, rva, length, h, fixups)
            except Exception:  # an RVA outside AP's image is refused too
                r += 1
        out.update({'ap_exe_tried': n, 'ap_exe_refused': r, 'ap_exe_sha256': ap.sha256})
    out['all_refused'] = refused == tried and out.get('ap_exe_refused', 0) == out.get('ap_exe_tried', 0)
    return out


def raw_branch_hits(im, start, end):
    """Raw relative-branch encodings in .text whose target lies strictly inside (start, end): [(source, target)]."""
    lo, hi, off = im.text
    data = im.data[off:off + (hi - lo)]
    hits = []
    for i in range(len(data) - 1):
        b, rva, target = data[i], lo + i, None
        if b in (0xe8, 0xe9) and i + 5 <= len(data):
            target = (rva + 5 + struct.unpack_from('<i', data, i + 1)[0]) & 0xffffffff
        elif b == 0xeb or 0x70 <= b <= 0x7f:
            target = rva + 2 + struct.unpack_from('<b', data, i + 1)[0]
        elif b == 0x0f and 0x80 <= data[i + 1] <= 0x8f and i + 6 <= len(data):
            target = (rva + 6 + struct.unpack_from('<i', data, i + 2)[0]) & 0xffffffff
        if target is not None and start < target < end:
            hits.append((rva, target))
    return hits


def direct_callers(im, target):
    lo, hi, off = im.text
    data = im.data[off:off + (hi - lo)]
    return [lo + i for i in range(len(data) - 5)
            if data[i] == 0xe8 and (lo + i + 5 + struct.unpack_from('<i', data, i + 1)[0]) & 0xffffffff == target]


def decoded_inbound(im, start, end, target):
    """Decoded direct branches/calls inside [start, end) whose target is exactly `target`."""
    out = []
    for i in im.insns(start, end)[0]:
        if (i.group(X.X86_GRP_JUMP) or i.group(X.X86_GRP_CALL)) and i.operands and \
                i.operands[0].type == X.X86_OP_IMM and i.operands[0].imm - BASE == target:
            out.append(i.address - BASE)
    return out


def abs_pointers_into(im, start, end):
    """Relocated dwords anywhere whose value points into [start, end)."""
    return sorted(f for f in im.fixups if start <= (im.u32(f) - BASE) & 0xffffffff < end)


X87_PREFIX = re.compile(r'^f(?!xsave|xrstor|emms)')


def is_x87_or_mmx(insn):
    if insn.group(X.X86_GRP_FPU) or insn.group(X.X86_GRP_MMX):
        return True
    if X87_PREFIX.match(insn.mnemonic) and insn.mnemonic not in ('fxsave', 'fxrstor'):
        return True
    return any(insn.reg_name(op.reg).startswith(('st', 'mm')) for op in insn.operands if op.type == X.X86_OP_REG)


# ---- bounded read-before-write explorer ----
GPR = {'eax': 'eax', 'ax': 'eax', 'al': 'eax', 'ah': 'eax', 'ecx': 'ecx', 'cx': 'ecx', 'cl': 'ecx', 'ch': 'ecx',
       'edx': 'edx', 'dx': 'edx', 'dl': 'edx', 'dh': 'edx', 'ebx': 'ebx', 'bx': 'ebx', 'bl': 'ebx', 'bh': 'ebx',
       'esi': 'esi', 'si': 'esi', 'edi': 'edi', 'di': 'edi', 'ebp': 'ebp', 'bp': 'ebp'}
PARTIAL = {'ax', 'al', 'ah', 'cx', 'cl', 'ch', 'dx', 'dl', 'dh', 'bx', 'bl', 'bh', 'si', 'di', 'bp'}


def _reg_items(insn, regs):
    out = set()
    for r in regs:
        name = insn.reg_name(r)
        if name in GPR:
            out.add(GPR[name])
        elif name.startswith('xmm'):
            out.add(name)
        elif name == 'eflags':
            out.add('eflags')
    return out


def _callee_ret_pop(im, target, limit=0x4000):
    pops = set()
    for i in im.md.disasm(im.read(target, limit), BASE + target):
        if i.mnemonic == 'ret':
            pops.add(i.operands[0].imm if i.operands else 0)
            if len(pops) > 1:
                return None
        if i.mnemonic in ('int3',) and pops:
            break
    return pops.pop() if len(pops) == 1 else None


def _jump_table(im, insn, function):
    m = insn.operands[0].mem if insn.operands and insn.operands[0].type == X.X86_OP_MEM else None
    if not m or m.base or m.scale != 4:
        return None
    table, targets = m.disp - BASE, []
    while table in im.fixups:
        t = im.u32(table) - BASE
        if not (function[0] <= t < function[1]):
            break
        targets.append(t)
        table += 4
    return targets or None


def explore(im, start, tracked, function, call_ok=(), max_steps=40000, ret_reads=()):
    """Every path from `start` (an RVA) inside `function` = (lo, hi): a tracked item (a GPR family name, 'eflags',
    'xmmN', or ('stk', offset) relative to the start's ESP) read before the path writes it is a violation. A direct call
    reads ECX/EDX/XMM0-7 unless its target is in `call_ok` (callee RVAs whose register inputs are established), then
    writes EAX/ECX/EDX/EFLAGS/XMM0-7 (volatile); stack slots are followed through calls with the callee's RET n. RET
    reads the items in `ret_reads`. Indirect jumps through a relocated table inside the function are followed; any
    other unresolved exit with items still tracked is reported. Returns (violations, steps, paths_completed)."""
    volatile = {'eax', 'ecx', 'edx', 'eflags'} | {'xmm%d' % k for k in range(8)}
    violations, seen = [], set()
    work = [(start, frozenset(tracked), 0)]
    steps = done = 0
    while work and steps < max_steps:
        at, live, esp = work.pop()
        while True:
            if not live:
                done += 1
                break
            key = (at, live, esp)
            if key in seen:
                break
            seen.add(key)
            steps += 1
            insn = im.insn(at)
            if insn is None or not (function[0] <= at < function[1]):
                violations.append((hex(at), 'left_function', sorted(map(str, live))))
                break
            regs_read, regs_write = insn.regs_access()
            reads = _reg_items(insn, regs_read)
            writes = _reg_items(insn, regs_write)
            # partial writes (AL, CX, ...) keep the upper bits: a read of the family
            for r in regs_write:
                if insn.reg_name(r) in PARTIAL and insn.mnemonic not in ('movzx', 'movsx'):
                    reads.add(GPR[insn.reg_name(r)])
            if insn.mnemonic in ('xor', 'sub', 'pxor', 'xorps') and len(insn.operands) == 2 and \
                    insn.operands[0].type == X.X86_OP_REG and insn.operands[1].type == X.X86_OP_REG and \
                    insn.operands[0].reg == insn.operands[1].reg:
                reads -= _reg_items(insn, [insn.operands[0].reg])  # zeroing idiom
            slot_reads, slot_writes, esp_known = set(), set(), esp is not None
            for op in insn.operands:
                if op.type == X.X86_OP_MEM and op.mem.base and insn.reg_name(op.mem.base) == 'esp' and esp_known:
                    slot = ('stk', op.mem.disp + esp)
                    if op.access & capstone.CS_AC_READ:
                        slot_reads.add(slot)
                    if op.access & capstone.CS_AC_WRITE and op.size >= 4:
                        slot_writes.add(slot)
                    if insn.mnemonic in ('lea',):
                        slot_reads.discard(slot)
            if insn.mnemonic in ('push',) and esp_known and insn.operands[0].type == X.X86_OP_MEM:
                m = insn.operands[0].mem
                if m.base and insn.reg_name(m.base) == 'esp':
                    slot_reads.add(('stk', m.disp + esp))
            if insn.mnemonic == 'pop' and esp_known:
                slot_reads.add(('stk', esp))
            if insn.group(X.X86_GRP_CALL):
                target = insn.operands[0].imm - BASE if insn.operands[0].type == X.X86_OP_IMM else None
                if target not in call_ok:
                    reads |= {'ecx', 'edx'} | {'xmm%d' % k for k in range(8)}
                writes |= volatile
            hit = (reads | slot_reads) & live
            if hit:
                violations.append((hex(BASE + at), insn.mnemonic + ' ' + insn.op_str, sorted(map(str, hit))))
                live = live - hit
            live = live - writes - slot_writes
            # ESP bookkeeping
            if esp is not None:
                if insn.mnemonic == 'push':
                    esp -= 4
                elif insn.mnemonic == 'pop':
                    esp += 4
                elif insn.mnemonic in ('add', 'sub') and insn.operands[0].type == X.X86_OP_REG and \
                        insn.reg_name(insn.operands[0].reg) == 'esp':
                    esp = esp + insn.operands[1].imm if insn.mnemonic == 'add' and insn.operands[1].type == X.X86_OP_IMM \
                        else esp - insn.operands[1].imm if insn.operands[1].type == X.X86_OP_IMM else None
                elif insn.mnemonic == 'lea' and insn.reg_name(insn.operands[0].reg) == 'esp' and \
                        insn.operands[1].mem.base and insn.reg_name(insn.operands[1].mem.base) == 'esp' and \
                        not insn.operands[1].mem.index:
                    esp += insn.operands[1].mem.disp
                elif 'esp' in {insn.reg_name(r) for r in regs_write} and not insn.group(X.X86_GRP_CALL) and \
                        insn.mnemonic not in ('ret',):
                    esp = None
                elif insn.group(X.X86_GRP_CALL):
                    target = insn.operands[0].imm - BASE if insn.operands[0].type == X.X86_OP_IMM else None
                    pop = _callee_ret_pop(im, target) if target is not None else None
                    esp = esp + pop if pop is not None else None
            if esp is None:
                live = frozenset(x for x in live if not isinstance(x, tuple))
            nxt = at + insn.size
            if insn.mnemonic == 'ret':
                hit = set(ret_reads) & live
                if hit:
                    violations.append((hex(BASE + at), 'ret', sorted(hit)))
                done += 1
                break
            if insn.group(X.X86_GRP_JUMP):
                op = insn.operands[0]
                if op.type == X.X86_OP_IMM:
                    target = op.imm - BASE
                    if insn.mnemonic == 'jmp':
                        at = target
                        continue
                    work.append((target, frozenset(live), esp))
                else:
                    targets = _jump_table(im, insn, function)
                    if not targets:
                        violations.append((hex(BASE + at), 'indirect_jump', sorted(map(str, live))))
                        break
                    for t in targets:
                        work.append((t, frozenset(live), esp))
                    break
            if insn.mnemonic in ('int3', 'hlt', 'ud2'):
                violations.append((hex(BASE + at), insn.mnemonic, sorted(map(str, live))))
                break
            at = nxt
    if work:
        violations.append(('budget', 'steps', steps))
    return violations, steps, done


def read_only(im, rva):
    """True when rva lies in a section that is not writable (IMAGE_SCN_MEM_WRITE clear)."""
    s = im.section(rva)
    return bool(s) and not s['characteristics'] & 0x80000000


def report_exe(im):
    return {'sha256': im.sha256, 'size': len(im.data), 'preferred_base': hex(im.pe.base), 'highlow': len(im.fixups),
            'measured_image': im.sha256 == FL_SHA256}
