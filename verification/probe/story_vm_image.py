"""A KC VM image as the engine builds it in memory, from an L/*.obj on disk, for the run-time story detector
(src/proxy/story_detect.h) compiled on the host (verification/probe/story_detect_host.cpp). The image models the
layout story_detect.h documents: the CODE pass 0x0049e1a0 (operands little-endian, each native name replaced by
u16 group + u16 command), the decoded string table, the class records sorted by key, the classes with their parent
and method rows sorted by name, and the native groups' name tables. It does not model the peephole pass 0x0049e4f0
(which never rewrites the native, call or fork opcodes the signatures start with). The image holds script bytes:
write it to a temporary directory only, never into the repository.

    from story_vm_image import build_image, compile_host, run_host
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


def _cstr(s, off):
    return s[off:s.index(b'\0', off)] if 0 < off < len(s) else b''


def build_image(raw, build=None, mutate=None):
    """The image bytes: b'X3VM', u32 VM address, u32 segment count, then (u32 address, u32 size, bytes) per segment.
    `build` overrides the header build id; `mutate(code: bytearray, entries)` edits the memory-form CODE in place
    (entries: {(class, name): entry})."""
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
            for k in range(pc + 3 if op == 0x78 else pc + 5, pc + n - 3, 4):
                code[k:k + 4] = code[k:k + 4][::-1]
        elif W.get(op) == 3:
            code[pc + 1:pc + 3] = code[pc + 1:pc + 3][::-1]
        elif W.get(op) == 5:
            code[pc + 1:pc + 5] = code[pc + 1:pc + 5][::-1]
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


def run_host(exe, image_path):
    """{'seta': {field: value}, 'queue': {field: value}} from the harness's two key=value rows."""
    run = subprocess.run([str(exe), str(image_path)], capture_output=True, text=True, timeout=60)
    if run.returncode != 0:
        raise RuntimeError(run.stdout + run.stderr)
    out = {}
    for line in run.stdout.splitlines():
        head, _, rest = line.partition(' ')
        out[head] = dict(kv.split('=', 1) for kv in rest.split())
    return out
