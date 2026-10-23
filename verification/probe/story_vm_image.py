"""A KC VM image as the engine builds it in memory, from an L/*.obj on disk, for the run-time story detector
(src/proxy/story_detect.h) compiled on the host (verification/probe/story_detect_host.cpp). The image models the
layout story_detect.h documents: the CODE pass 0x0049e1a0 (operands little-endian, each native name replaced by
u16 group + u16 command), the decoded string table, the class records sorted by key, the classes with their parent
and method rows sorted by name, and the native groups' name tables, then the peephole pass 0x0049e4f0 (peephole():
its opcode rewrites, then its jump threading). The image holds script bytes: write it to a temporary directory only,
never into the repository.

    from story_vm_image import build_image, compile_host, run_host, memory_width, method_insns
"""
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'verification/results/ship-navigation-ai/gate-queue'))
from kc_obj import Obj, W  # noqa: E402

VM, CODE, STRINGS, RECORDS, CLASSES, ROWS, GROUPS, NAMES = (0x10000000, 0x20000000, 0x30000000, 0x40000000, 0x41000000,
                                                           0x42000000, 0x50000000, 0x51000000)
HOST = ROOT / 'verification/probe/story_detect_host.cpp'


def _clas(raw, o):
    """CLAS rows with their name offsets: {cid: (parent, [(entry, name_offset)])}."""
    kp, kn = o.sections['CLAS']
    c = raw[kp + 8:kp + 8 + kn]
    q = 0

    def w():
        nonlocal q
        v = struct.unpack_from('>I', c, q)[0]
        q += 4
        return v
    out = {}
    for _ in range(w()):
        cid, par, _word, na = w(), w(), w(), w()
        rows = []
        for _m in range(na):
            e, nm, _loc, _argc = w(), w(), w(), w()
            rows.append((e, nm))
        nv = w()
        q += 16 * nv
        out[cid] = (par, rows)
    return out


# In-memory instruction widths, after the peephole pass: the table of its second walk 0x0049e810 (dispatch byte table
# 0x0049e9d4, jump table 0x0049e9a0, X3AP.exe fdbf3418...). Every other opcode is one byte; 78 and 79 carry little-endian
# u16 counts in memory (0x0049e283, 0x0049e2e4). The rewritten opcodes cover their whole fused pattern (95 = 17 24,
# 8c..94 = 3-byte load/store + one byte, 96 = 14 n 23 m, 97..99 = 0d..0f n 17 24, a0..a7 = the increment patterns);
# aa..b1 (push-const / not + jump) are one byte, the jump after them is its own instruction.
MEMORY_WIDTH = {op: 2 for op in (0x05, 0x47, 0x4c, 0x95)}
MEMORY_WIDTH.update({op: 3 for op in (0x06, 0x0d, 0x0e, 0x0f, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x18, 0x19, 0x1a, 0x1b,
                                      0x1c, 0x1d, 0x1f, 0x20, 0x21, 0x23, 0x28, 0x29, 0x2c, 0x30, 0x48, 0x4d, 0x6e)})
MEMORY_WIDTH.update({op: 4 for op in range(0x8c, 0x95)})
MEMORY_WIDTH.update({op: 5 for op in (0x07, 0x0b, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x49, 0x4e, 0x82,
                                      0x84, 0x85, 0x86, 0x87, 0x88, 0x97, 0x98, 0x99)})
MEMORY_WIDTH.update({0x96: 6, 0xa4: 8, 0xa5: 8, 0x0a: 9, 0xa0: 9, 0xa1: 9, 0xa6: 10, 0xa7: 10, 0xa2: 11, 0xa3: 11})


def memory_width(code, pc):
    """The width of the in-memory instruction at pc (story_detect.h memory_width)."""
    op = code[pc]
    if op == 0x78:
        return 11 + 4 * struct.unpack_from('<H', code, pc + 1)[0]
    if op == 0x79:
        a, b = struct.unpack_from('<HH', code, pc + 1)
        return 9 + 8 * (a + b)
    return MEMORY_WIDTH.get(op, 1)


def method_insns(code, entry, entries):
    """The in-memory instruction starts of the method at `entry`, up to the next entry of any method (or CODE's end)."""
    end = min([e for e in entries.values() if e > entry], default=len(code))
    pc, out = entry, []
    while pc < end:
        out.append(pc)
        pc += memory_width(code, pc)
    return out


def _free(pc, width, keep):
    return all(pc + width <= a or a + n <= pc for a, n in keep)


def replace_insn(code, entry, entries, width, keep, new):
    """Overwrite the first whole instruction of `width` bytes in the method at `entry` that overlaps no (start, length) of
    `keep` with `new` (same width); returns its offset."""
    assert len(new) == width
    for pc in method_insns(code, entry, entries):
        if memory_width(code, pc) == width and _free(pc, width, keep):
            code[pc:pc + width] = new
            return pc
    raise AssertionError('no %d-byte instruction to replace at %x' % (width, entry))


def embed_in_operand(code, entry, entries, signature, keep):
    """Replace a run of whole instructions of the method at `entry` (overlapping no range of `keep`) by `0a <8 bytes>` whose
    operand begins with `signature`, then push0 (01) up to the run's end: the signature's bytes then sit inside an operand,
    never at an instruction start. Returns the 0a's offset."""
    assert len(signature) <= 8
    insns = method_insns(code, entry, entries)
    for k, pc in enumerate(insns):
        end = pc
        for q in insns[k:]:
            if not _free(q, memory_width(code, q), keep):
                break
            end = q + memory_width(code, q)
            if end - pc >= 9:
                code[pc:end] = bytes([0x0a]) + bytes(signature) + bytes(8 - len(signature)) + bytes([0x01]) * (end - pc - 9)
                return pc
    raise AssertionError('no 9-byte run to embed at %x' % entry)


def peephole(code):
    """0x0049e4f0 in place over the byte-swapped CODE: the first walk (file widths) rewrites the first opcode byte of
    its patterns (0x0049e528..0x0049e7b5; `0d 0001 14 0002 24 83` loses its first three bytes to `83 83 83`), the second
    (memory widths) threads the operand of every 32/33/34 through jmp chains and constant conditionals
    (0x0049e832..0x0049e88c)."""
    le32 = lambda at: struct.unpack_from('<I', code, at)[0]
    le16 = lambda at: struct.unpack_from('<H', code, at)[0]
    s16 = lambda at: struct.unpack_from('<h', code, at)[0]
    n = len(code)
    pc = 1
    while pc < n:
        op = code[pc]
        at = lambda k: code[pc + k] if pc + k < n else -1
        if op == 0x17:
            if at(1) == 0x24:
                code[pc] = 0x95
        elif op in (0x0d, 0x0e, 0x0f, 0x14, 0x15, 0x16):
            dl = at(3)
            if dl == 0x10:
                if op <= 0x0f:
                    code[pc] = 0x8c + op - 0x0d
            elif dl == 0x17:
                if op <= 0x0f:
                    code[pc] = (0x97 if at(4) == 0x24 else 0x8f) + op - 0x0d
            elif dl == 0x24:
                if op >= 0x14:
                    code[pc] = 0x92 + op - 0x14
            elif op == 0x14 and dl == 0x23:
                code[pc] = 0x96
            elif op == 0x0d and pc + 11 <= n:
                si = s16(pc + 1)
                if si == 1 and dl == 0x14 and le16(pc + 4) == 2 and at(6) == 0x24 and at(7) == 0x83:
                    code[pc:pc + 3] = b'\x83\x83\x83'
                elif dl == 0x02 and at(4) in (0x46, 0x4b) and at(5) == 0x14 and s16(pc + 6) == si + 1:
                    sub = int(at(4) != 0x46)
                    tail, nxt = at(8), (0x46 if sub else 0x4b)   # 0x0049e6b6 / 0x0049e6cb
                    if tail == 0x24:
                        code[pc] = 0xa0 + sub
                    elif tail == 0x02 and at(9) == nxt:
                        code[pc] = (0xa2 if at(10) == 0x24 else 0xa6) + sub
                    else:
                        code[pc] = 0xa4 + sub
        elif op in (0x01, 0x02):
            cl = at(1)
            if cl == 0x34:
                code[pc] = 0xaa + int(op != 1)
            elif cl == 0x33:
                code[pc] = 0xaa + int(op != 2)
            elif cl == 0x32 and pc + 6 <= n:
                t, steps = le32(pc + 2), 0
                while t + 5 <= n and code[t] == 0x32 and steps < n:
                    t, steps = le32(t + 1), steps + 1
                c = code[t] if t < n else -1
                if c == 0x34:
                    code[pc] = 0xac + int(op != 1)
                elif c == 0x33:
                    code[pc] = 0xac + int(op != 2)
            elif cl == 0x64:
                c = at(2)
                if c == 0x34:
                    code[pc] = 0xb0 + int(op != 2)
                elif c == 0x33:
                    code[pc] = 0xb0 + int(op != 1)
        elif op == 0x64:
            if at(1) == 0x34:
                code[pc] = 0xae
            elif at(1) == 0x33:
                code[pc] = 0xaf
        pc += _file_width(code, pc)
    pc = 1
    while pc < n:
        if code[pc] in (0x32, 0x33, 0x34) and pc + 5 <= n:
            t, steps = le32(pc + 1), 0
            while t < n and steps < n:
                c, steps = code[t], steps + 1
                if c == 0x32 and t + 5 <= n:
                    t = le32(t + 1)
                elif c == 0xaa and t + 6 <= n:
                    t = le32(t + 2)
                elif c == 0xb0 and t + 7 <= n:
                    t = le32(t + 3)
                elif c == 0xab:
                    t += 6
                elif c == 0xb1:
                    t += 7
                elif c == 0xac and t + 6 <= n and le32(t + 2) + 5 <= n:
                    t = le32(le32(t + 2) + 1)
                elif c == 0xad and t + 6 <= n:
                    t = le32(t + 2) + 5
                else:
                    break
            struct.pack_into('<I', code, pc + 1, t)
        pc += memory_width(code, pc)


def _file_width(code, pc):
    """The file-form width (0x0049e1a0's table, kc_obj.W), over a byte-swapped CODE (78/79 counts little-endian)."""
    op = code[pc]
    if op in (0x78, 0x79):
        return memory_width(code, pc)
    return W.get(op, 1)


def _cstr(s, off):
    return s[off:s.index(b'\0', off)] if 0 < off < len(s) else b''


def build_image(raw, build=None, mutate=None):
    """The image bytes: b'X3VM', u32 VM address, u32 segment count, then (u32 address, u32 size, bytes) per segment.
    `build` overrides the header build id; `mutate(code: bytearray, entries)` edits the memory-form CODE (after the
    peephole pass) in place (entries: {(class, name): entry})."""
    o = Obj(raw)
    code = bytearray(o.code)
    # native groups: one per name prefix, in sorted order, commands in sorted order
    natives = sorted({o.name(struct.unpack_from('>I', code, pc + 1)[0]) for pc in o.all_insns() if code[pc] == 0x82})
    prefixes = sorted({n.split('_', 1)[0] for n in natives})
    assert len(prefixes) < 31, prefixes
    groups = {p: [n for n in natives if n.split('_', 1)[0] == p] for p in prefixes}
    resolve = {n: (1 + prefixes.index(p), groups[p].index(n)) for p, names in groups.items() for n in names}
    for pc in o.all_insns():
        op = code[pc]
        if op == 0x82:
            g, c = resolve[o.name(struct.unpack_from('>I', code, pc + 1)[0])]
            code[pc + 1:pc + 5] = struct.pack('<HH', g, c)
        elif op == 0x0a:
            code[pc + 1:pc + 9] = code[pc + 1:pc + 5][::-1] + code[pc + 5:pc + 9][::-1]
        elif op in (0x78, 0x79):
            n = o.width(pc)
            for k in range(pc + 1, pc + 3 if op == 0x78 else pc + 5, 2):   # the u16 counts (0x0049e283, 0x0049e2e4)
                code[k:k + 2] = code[k:k + 2][::-1]
            for k in range(pc + 3 if op == 0x78 else pc + 5, pc + n - 3, 4):
                code[k:k + 4] = code[k:k + 4][::-1]
        elif W.get(op) == 3:
            code[pc + 1:pc + 3] = code[pc + 1:pc + 3][::-1]
        elif W.get(op) == 5:
            code[pc + 1:pc + 5] = code[pc + 1:pc + 5][::-1]
    peephole(code)
    clas = _clas(raw, o)
    if mutate:
        entries = {(cid, _cstr(o.s, nm).decode('latin1')): e for cid, (_, rows) in clas.items() for e, nm in rows}
        mutate(code, entries)
    cids = sorted(clas, key=lambda k: struct.unpack('<i', struct.pack('<I', k))[0])
    records, classes, rows_blob = bytearray(), bytearray(), bytearray()
    class_at = {cid: CLASSES + 0x40 * i for i, cid in enumerate(cids)}
    for i, cid in enumerate(cids):
        par, rows = clas[cid]
        rows = sorted(rows, key=lambda r: _cstr(o.s, r[1]))
        records += struct.pack('<iII', struct.unpack('<i', struct.pack('<I', cid))[0], 0, class_at[cid]) + bytes(0x38 - 12)
        cls = bytearray(0x40)
        struct.pack_into('<I', cls, 0x14, class_at.get(par, 0))
        struct.pack_into('<II', cls, 0x28, len(rows), ROWS + len(rows_blob))
        classes += cls
        for e, nm in rows:
            rows_blob += struct.pack('<IIII', e, nm, 0, 0)
    vm = bytearray(0x1440)
    struct.pack_into('<I', vm, 0x08, CODE)
    struct.pack_into('<I', vm, 0x10, len(code))
    struct.pack_into('<II', vm, 0x14, STRINGS, len(o.s))
    struct.pack_into('<II', vm, 0x1c, len(cids), RECORDS)
    struct.pack_into('<I', vm, 0x143c, o.build if build is None else build)
    tables, names = bytearray(), bytearray()
    for g, p in enumerate(prefixes, 1):
        struct.pack_into('<I', vm, 0x40 + g * 0x18, GROUPS + len(tables))
        for n in groups[p]:
            tables += struct.pack('<I', NAMES + len(names))
            names += n.encode('latin1') + b'\0'
        tables += struct.pack('<I', 0)
    segments = [(VM, bytes(vm)), (CODE, bytes(code)), (STRINGS, o.s), (RECORDS, bytes(records)), (CLASSES, bytes(classes)),
                (ROWS, bytes(rows_blob)), (GROUPS, bytes(tables)), (NAMES, bytes(names))]
    out = bytearray(b'X3VM' + struct.pack('<II', VM, len(segments)))
    for at, blob in segments:
        out += struct.pack('<II', at, len(blob)) + blob
    return bytes(out)


def compile_host(directory):
    compiler = shutil.which('c++') or shutil.which('clang++') or shutil.which('g++')
    if compiler is None:
        return None, 'A host C++ compiler is required'
    exe = Path(directory) / 'story_detect_host'
    build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/proxy'),
                            str(HOST), '-o', str(exe)], capture_output=True, text=True, timeout=120)
    return (exe if build.returncode == 0 else None), build.stdout + build.stderr


def run_host(exe, image_path, budget=None):
    """{'seta': {field: value}, 'queue': {field: value}} from the harness's two key=value rows (`budget`: the read budget
    per detection instead of the header's)."""
    run = subprocess.run([str(exe), str(image_path)] + ([str(budget)] if budget is not None else []), capture_output=True,
                         text=True, timeout=60)
    if run.returncode != 0:
        raise RuntimeError(run.stdout + run.stderr)
    out = {}
    for line in run.stdout.splitlines():
        head, _, rest = line.partition(' ')
        out[head] = dict(kv.split('=', 1) for kv in rest.split())
    return out
