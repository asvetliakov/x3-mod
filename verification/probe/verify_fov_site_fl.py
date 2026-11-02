#!/usr/bin/env python3
"""Read-only qualification of the FL field-of-view sites in X3FL.exe (src/engine/fl/fov_sites_fl.h, fov_fl.cpp).

docs/reverse-engineering/fl/field-of-view.md, "Verified 2026-11-02 against X3FL.exe". Checks, on the installed exe
(default /mnt/c/X3FL/X3FL.exe, SHA-256 e7f3aad8..., the image the constants were measured on; preferred base 0x400000,
every value an RVA):

- every window of fov_sites_fl.h (constructor, INS_SetFocus case, callee prefix, load window, load caller, update
  reader) holds the header's bytes after zeroing the HIGHLOW fields, and the .reloc census inside each window is
  exactly the header's declared fixups, each pointing at the declared target RVA;
- whole-instruction decoding (Capstone) from known instruction starts: the constructor from its entry 0x01df90, the
  serializer from 0x01d9a0, the case from its jump-table entry, the callee and reader from their entries; each window
  ends on an instruction boundary; the constructor site is one `mov dword ptr [esi + 0x24], 0x4000` whose immediate is
  its last four bytes at 0x01e08d; ESI = ECX from 0x01dfac and not written again before the site; the INS_SetFocus span
  is one five-byte `mov eax, dword ptr [abs]` whose operand is the only relocation in the span (replayed verbatim by
  the tail); the load span is `mov al, 1; mov dword ptr [edi + 0x24], edx` (2 + 3 bytes) with no relocation; the
  constructor imm32 holds no relocation;
- qword geometry: the imm32 crosses an aligned qword (plain write), both five-byte jumps fit one (lock cmpxchg8b);
- liveness: after the INS_SetFocus span ECX is written (0x030eb2) before any read, EAX is reloaded by the replay and
  EFLAGS are written (the callee's CMP) before any read; after the load span the serializer reads neither ECX nor
  EFLAGS before RET 8, and both callers (0x020c62 save, 0x020db6 load) TEST AL first; the load caller writes ECX before
  reading it on its success path;
- semantic anchors: dispatcher bound `cmp eax, 0x70` and `jmp [eax*4 + table]`, jump-table entry 0x21 = the case start,
  name-table entry 0x21 = "INS_SetFocus"; constructor direct callers exactly 0x077e21 and 0x0798a5; serializer direct
  callers exactly 0x020c62 and 0x020db6;
- interior entries: every raw E8/E9/EB/7x/0F8x encoding in .text whose target lies strictly inside a detour span, the
  imm32 or a window starts in the middle of a decoded instruction (none inside the two detour spans), and no relocated
  dword anywhere points strictly inside a window;
- the shared 81-entry remap table and vanilla table (verify_fov_site.py's Python twin).

Writes one JSON report to stdout (result PASS/FAIL). No Wine, no game launch, nothing written to the exe.

usage: python3 verification/probe/verify_fov_site_fl.py [--exe X3FL.exe] [--out report.json]
"""
import argparse
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

import capstone

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
from inspect_x3 import PE32  # noqa: E402

import verify_fov_site as ap  # noqa: E402  the shared remap math twin

SITES = ROOT / 'src/engine/fl/fov_sites_fl.h'
DEFAULT_EXE = Path('/mnt/c/X3FL/X3FL.exe')
FL_SHA256 = 'e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8'
BASE = 0x400000
WINDOWS = ('expected_window', 'expected_setfocus_case', 'expected_setfocus_callee', 'expected_load_window',
           'expected_load_caller', 'expected_reader')
CONSTRUCTOR_CALLERS = [0x077e21, 0x0798a5]
SERIALIZER_CALLERS = [0x020c62, 0x020db6]
CASE_DECODE = (0x030e9f, 0x030ec2)
SERIALIZER_DECODE = (0x01d9a0, 0x01df14)
CONSTRUCTOR_DECODE = (0x01df90, 0x01e098)
LOAD_CALLER_DECODE = (0x020db1, 0x020dd0)
SAVE_CALLER_DECODE = (0x020c5d, 0x020c6b)
DISPATCHER_DECODE = (0x030540, 0x030589)


def header_constants(text):
    """The header's numeric constants and byte arrays."""
    values = {}
    for name, value in re.findall(r'\b([a-z_0-9]+)\s*=\s*(0x[0-9a-fA-F]+|\d+)\b', text):
        values.setdefault(name, int(value, 0))
    arrays = {}
    for name, body in re.findall(r'constexpr unsigned char ([a-z_]+)\[[a-z_]+\] = \{(.*?)\};', text, re.S):
        arrays[name] = bytes(int(v, 16) for v in re.findall(r'0x([0-9a-fA-F]{2})', body))
    fixups = {}
    for name, offsets, rvas in re.findall(r'\{"([a-z_]+)", [a-z_]+, [a-z_]+, [a-z_]+, \d, \{([^}]*)\},\s*\{([^}]*)\}\}',
                                          text, re.S):
        fixups[name] = (offsets, rvas)
    return values, arrays, fixups


class Image:
    def __init__(self, path):
        self.data = Path(path).read_bytes()
        self.sha256 = hashlib.sha256(self.data).hexdigest()
        self.pe = PE32(self.data)
        pe_off = self.pe.u32(0x3c)
        reloc_rva, reloc_size = struct.unpack_from('<II', self.data, pe_off + 24 + 96 + 5 * 8)
        self.fixups = set()
        self.other_fixup_types = 0
        o, end = self.pe.offset(reloc_rva), self.pe.offset(reloc_rva) + reloc_size
        while o < end:
            page, size = struct.unpack_from('<II', self.data, o)
            if size < 8:
                break
            for k in range((size - 8) // 2):
                e = struct.unpack_from('<H', self.data, o + 8 + 2 * k)[0]
                if e >> 12 == 3:
                    self.fixups.add(page + (e & 0xfff))
                elif e >> 12:
                    self.other_fixup_types += 1
            o += size
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

    def fixups_in(self, start, end):
        return sorted(f for f in self.fixups if start <= f < end)


def reads(insn, names):
    regs_read, _ = insn.regs_access()
    return bool({insn.reg_name(r) for r in regs_read} & names)


def writes(insn, names):
    _, regs_write = insn.regs_access()
    return bool({insn.reg_name(r) for r in regs_write} & names)


ECX = {'ecx', 'cx', 'cl', 'ch'}
ESI = {'esi', 'si'}


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
    out = []
    for i in range(len(data) - 5):
        if data[i] == 0xe8 and (lo + i + 5 + struct.unpack_from('<i', data, i + 1)[0]) & 0xffffffff == target:
            out.append(lo + i)
    return out


def verify(exe=DEFAULT_EXE, sites_path=SITES, sha256=FL_SHA256):
    """The report; sha256=None skips the hash gate (the host test's mutated copies, to see which check refuses)."""
    values, arrays, declared = header_constants(Path(sites_path).read_text())
    im = Image(exe)
    v = values
    checks = {}
    report = {'exe': str(exe), 'sha256': im.sha256, 'checks': checks}
    checks['identity'] = (sha256 is None or im.sha256 == sha256) and im.pe.base == BASE and im.other_fixup_types == 0
    # ---- windows: bytes with fixups zeroed, the relocation census equal to the declared fixups ----
    window_rows = {}
    names = dict(zip(WINDOWS, ('constructor_mismatch', 'setfocus_mismatch', 'callee_mismatch', 'load_mismatch',
                               'load_caller_mismatch', 'reader_mismatch')))
    rvas = {'expected_window': v['window_rva'], 'expected_setfocus_case': v['setfocus_case_rva'],
            'expected_setfocus_callee': v['setfocus_callee_rva'], 'expected_load_window': v['load_window_rva'],
            'expected_load_caller': v['load_caller_rva'], 'expected_reader': v['reader_rva']}
    targets = {'registry_slot_rva': v['registry_slot_rva'], 'vm_slot_rva': v['vm_slot_rva']}
    all_windows_ok = True
    for array in WINDOWS:
        expected, rva = arrays[array], rvas[array]
        live = bytearray(im.read(rva, len(expected)))
        census = [f - rva for f in im.fixups_in(rva - 3, rva + len(expected))]
        offsets_text, rvas_text = declared[names[array]]
        offsets = [int(x, 0) for x in re.findall(r'0x[0-9a-f]+|\d+', offsets_text)]
        fix_targets = [targets.get(x.strip(), 0) for x in rvas_text.split(',')]
        count = len(census)
        declared_offsets = offsets[:count] if count else []
        pointed = [im.u32(rva + o) - BASE for o in census]
        for o in census:
            live[o:o + 4] = b'\0' * 4
        ok = (bytes(live) == expected and census == declared_offsets and pointed == fix_targets[:count]
              and all(0 <= o <= len(expected) - 4 for o in census))
        window_rows[names[array]] = {'rva': hex(rva), 'length': len(expected), 'fnv1a': hex(fnv1a(bytes(live))),
                                     'fixups': [hex(o) for o in census], 'targets': [hex(t) for t in pointed]}
        all_windows_ok = all_windows_ok and ok
        checks['window_' + names[array].replace('_mismatch', '')] = ok
    report['windows'] = window_rows
    fnv = {'constructor_mismatch': 'window_fnv1a', 'setfocus_mismatch': 'setfocus_case_fnv1a',
           'callee_mismatch': 'setfocus_callee_fnv1a', 'load_mismatch': 'load_window_fnv1a',
           'load_caller_mismatch': 'load_caller_fnv1a', 'reader_mismatch': 'reader_fnv1a'}
    checks['window_hashes_match_header'] = all(int(window_rows[k]['fnv1a'], 16) == v[c] for k, c in fnv.items())
    # ---- constructor: whole instructions from its entry; the site; ESI ----
    ctor, ctor_ok = im.insns(*CONSTRUCTOR_DECODE)
    starts = [i.address - BASE for i in ctor]
    site = next((i for i in ctor if i.address - BASE == v['site_rva']), None)
    esi_set = next((i for i in ctor if i.address - BASE == v['constructor_esi_rva']), None)
    between = [i for i in ctor if v['constructor_esi_rva'] < i.address - BASE < v['site_rva']]
    checks['constructor_decode'] = (ctor_ok and v['window_rva'] in starts and v['site_rva'] in starts
                                    and v['window_rva'] + v['window_length'] == CONSTRUCTOR_DECODE[1])
    checks['constructor_site'] = bool(site and site.mnemonic == 'mov' and site.op_str == 'dword ptr [esi + 0x24], 0x4000'
                                      and site.size == v['site_length'] and site.imm_offset == 3
                                      and site.address - BASE + site.imm_offset == v['write_rva'])
    checks['constructor_esi'] = bool(esi_set and esi_set.mnemonic == 'mov' and esi_set.op_str == 'esi, ecx'
                                     and not any(writes(i, ESI) for i in between if i.mnemonic != 'call'))
    checks['imm32_crosses_qword'] = (v['write_rva'] & 7) + 4 > 8 and not im.fixups_in(v['write_rva'] - 3, v['write_rva'] + 4)
    report['imm32'] = {'rva': hex(v['write_rva']), 'qword_offset': v['write_rva'] & 7,
                       'bytes': im.read(v['write_rva'], 4).hex()}
    report['constructor_callers'] = [hex(c) for c in direct_callers(im, v['constructor_rva'])]
    checks['constructor_callers'] = direct_callers(im, v['constructor_rva']) == CONSTRUCTOR_CALLERS
    # ---- INS_SetFocus: the case, the span, the replayed operand, liveness ----
    case, case_ok = im.insns(*CASE_DECODE)
    span = [i for i in case if v['setfocus_site_rva'] <= i.address - BASE < v['setfocus_return_rva']]
    after = [i for i in case if i.address - BASE >= v['setfocus_return_rva']]
    span_fixups = im.fixups_in(v['setfocus_site_rva'] - 3, v['setfocus_return_rva'])
    checks['setfocus_span'] = (case_ok and len(span) == 1 and span[0].size == 5 and span[0].mnemonic == 'mov'
                               and span[0].op_str == 'eax, dword ptr [%s]' % hex(BASE + v['registry_slot_rva'])
                               and span[0].address + span[0].size == BASE + v['setfocus_return_rva'])
    checks['setfocus_span_one_relocation_operand'] = span_fixups == [v['setfocus_site_rva'] + 1]
    store = after[0] if after else None
    ecx_write = next((k for k, i in enumerate(after) if writes(i, ECX)), None)
    ecx_read_before = any(reads(i, ECX) for i in after[1:ecx_write]) if ecx_write is not None else True
    checks['setfocus_store_then_ecx_dead'] = bool(store and store.op_str == 'dword ptr [eax + 0x24], ecx'
                                                  and ecx_write == 1 and not ecx_read_before)
    callee, callee_ok = im.insns(v['setfocus_callee_rva'], v['setfocus_callee_rva'] + v['setfocus_callee_length'])
    call_at = next((k for k, i in enumerate(after) if i.mnemonic == 'call'), len(after))
    flag_path = after[:call_at + 1] + callee  # execution order: the store, MOV ECX, the CALL, then the callee
    first_flag_write = next((k for k, i in enumerate(flag_path) if writes(i, {'eflags'})), None)
    flag_read_before = any(reads(i, {'eflags'}) for i in flag_path[:first_flag_write or 0])
    checks['setfocus_flags_dead'] = (callee_ok and call_at == 2 and first_flag_write is not None and not flag_read_before
                                     and flag_path[first_flag_write].mnemonic == 'cmp'
                                     and int(after[call_at].op_str, 16) - BASE == v['setfocus_callee_rva'])
    checks['setfocus_qword'] = (v['setfocus_site_rva'] & 7) + 5 <= 8
    # ---- dispatcher, jump table, name table ----
    disp, disp_ok = im.insns(*DISPATCHER_DECODE)
    bound = [i for i in disp if i.mnemonic == 'cmp' and i.op_str == 'eax, 0x70']
    jump = [i for i in disp if i.mnemonic == 'jmp' and i.op_str.startswith('dword ptr [eax*4 + ')]
    table = int(jump[0].op_str.split('+ ')[1].rstrip(']'), 16) - BASE if jump else 0
    entry_rva = table + 4 * 0x21
    name_ptr = im.u32(v['name_entry_rva']) - BASE
    name = im.read(name_ptr, 16).split(b'\0')[0].decode('latin-1')
    report['jump_table'] = {'table': hex(table), 'entry': hex(entry_rva), 'case': hex(im.u32(entry_rva) - BASE),
                            'name': name}
    checks['setfocus_anchors'] = (disp_ok and len(bound) == 1 and len(jump) == 1 and entry_rva == v['jump_table_entry_rva']
                                  and im.u32(entry_rva) - BASE == v['setfocus_case_rva'] and entry_rva in im.fixups
                                  and v['name_entry_rva'] in im.fixups and name == 'INS_SetFocus')
    entries = [im.u32(table + 4 * k) - BASE for k in range(0x71)]
    checks['no_table_entry_inside_case'] = not any(v['setfocus_case_rva'] < e < CASE_DECODE[1] for e in entries)
    # ---- load: the span, no relocation, liveness after it, the callers ----
    ser, ser_ok = im.insns(*SERIALIZER_DECODE)
    lspan = [i for i in ser if v['load_site_rva'] <= i.address - BASE < v['load_return_rva']]
    lafter = [i for i in ser if i.address - BASE >= v['load_return_rva']]
    checks['load_span'] = (ser_ok and [(i.mnemonic, i.op_str, i.size) for i in lspan]
                           == [('mov', 'al, 1', 2), ('mov', 'dword ptr [edi + 0x24], edx', 3)]
                           and not im.fixups_in(v['load_site_rva'] - 3, v['load_return_rva']))
    checks['load_tail_epilogue'] = [(i.mnemonic, i.op_str) for i in lafter] == [('pop', 'edi'), ('pop', 'esi'), ('pop', 'ebx'),
                                                                                  ('ret', '8')]
    checks['load_qword'] = (v['load_site_rva'] & 7) + 5 <= 8
    report['serializer_callers'] = [hex(c) for c in direct_callers(im, v['serializer_rva'])]
    checks['serializer_callers'] = direct_callers(im, v['serializer_rva']) == SERIALIZER_CALLERS
    lc, lc_ok = im.insns(*LOAD_CALLER_DECODE)
    sc, sc_ok = im.insns(*SAVE_CALLER_DECODE)
    lc_after = [i for i in lc if i.address - BASE > v['load_caller_rva']]
    sc_after = [i for i in sc if i.address - BASE > SERIALIZER_CALLERS[0]]
    ecx_w = next((k for k, i in enumerate(lc_after) if writes(i, ECX)), None)
    checks['load_callers_test_al'] = (lc_ok and sc_ok and lc_after[0].mnemonic == 'test' and lc_after[0].op_str == 'al, al'
                                      and sc_after[0].mnemonic == 'test' and sc_after[0].op_str == 'al, al')
    checks['load_caller_ecx_dead'] = ecx_w is not None and not any(reads(i, ECX) for i in lc_after[:ecx_w])
    # ---- interior entries: raw branch encodings and relocated pointers ----
    spans = {'setfocus_span': (v['setfocus_site_rva'], v['setfocus_return_rva']),
             'load_span': (v['load_site_rva'], v['load_return_rva']),
             'imm32': (v['write_rva'] - 1, v['write_rva'] + 4),
             'constructor_window': (v['window_rva'], v['window_rva'] + v['window_length']),
             'setfocus_case': (v['setfocus_case_rva'], CASE_DECODE[1]),
             'load_window': (v['load_window_rva'], v['load_window_rva'] + v['load_window_length'])}
    decoded_starts = {i.address - BASE for i in ctor + case + ser}
    decoded_ranges = [(i.address - BASE, i.address - BASE + i.size) for i in ctor + case + ser]
    interior = {}
    for name_, (a, b) in spans.items():
        hits = raw_branch_hits(im, a, b)
        not_mid = [h for h in hits if h[0] in decoded_starts or not any(s < h[0] < e for s, e in decoded_ranges)]
        pointers = [f for f in im.fixups if a < im.u32(f) - BASE < b]
        interior[name_] = {'raw_hits': [[hex(s), hex(t)] for s, t in hits], 'not_mid_instruction': [hex(s) for s, _ in not_mid],
                           'pointers_inside': [hex(p) for p in pointers]}
    report['interior'] = interior
    checks['no_entry_into_detours'] = not interior['setfocus_span']['raw_hits'] and not interior['load_span']['raw_hits'] \
        and not interior['imm32']['raw_hits']
    checks['window_hits_mid_instruction'] = all(not r['not_mid_instruction'] for r in interior.values())
    checks['no_pointer_into_windows'] = all(not r['pointers_inside'] for r in interior.values())
    # ---- the reader: registry+0x24 is the update's focus read ----
    reader, reader_ok = im.insns(v['reader_rva'], v['reader_rva'] + v['reader_length'])
    checks['reader_reads_focus'] = reader_ok and [(i.mnemonic, i.op_str) for i in reader] == [
        ('mov', 'eax, dword ptr [%s]' % hex(BASE + v['registry_slot_rva'])), ('mov', 'dword ptr [esp + 0x1c], edi'),
        ('mov', 'esi, dword ptr [eax + 0x24]')]
    # ---- the shared tables ----
    table_values = ap.remap_table()
    report['remap_table_70_80_90_100'] = [hex(x) for x in table_values[20:51:10]]
    checks['shared_remap_table'] = (len(table_values) == 81 and table_values[20] == 0x2768 and table_values[40] == 0x3470
                                    and table_values[50] == 0x3b6f and ap.vanilla_table()[40] == 0x4000)
    report['result'] = 'PASS' if all(checks.values()) else 'FAIL'
    report['failed'] = [k for k, ok in checks.items() if not ok]
    return report


def fnv1a(data):
    h = 0x811c9dc5
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xffffffff
    return h


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--exe', type=Path, default=DEFAULT_EXE)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    report = verify(args.exe)
    text = json.dumps(report, indent=1, sort_keys=True)
    if args.out:
        args.out.write_text(text + '\n')
    print(text)
    return 0 if report['result'] == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
