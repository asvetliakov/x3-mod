#!/usr/bin/env python3
"""Read-only qualification of the FL resource reader and DAT handle pool sites (src/engine/fl/resource_reader_sites_fl.h,
resource_reader_fl.cpp) on X3FL.exe.

docs/reverse-engineering/fl/resource-reader.md, "Port (2026-11-03)". Checks on the installed exe (default
/mnt/c/X3FL/X3FL.exe, SHA-256 e7f3aad8...; every value an RVA):

- the reader and pool windows of loading_windows_fl_inc.h equal the windows regenerated from the exe, and every
  single-byte or fixup change of a window is refused by the production decision;
- the reader entry: the header's six bytes, whole instructions, no relocation or relative operand, no branch into its
  interior; the ECX contract (ECX moved to EDI at 0x105f61 and not written before; EAX written by `lea` at 0x105f54 and
  not read before), the direct-caller census;
- every core binding from the bytes, not the list: fread/fseek/ftell/malloc/free are the targets of the calls the
  reader, dispatcher, seek, DAT open and allocator make; the four counters are the allocator's update operands in the
  core's order (EDX=0 at both reader calls); the size globals are the reader's two stores; the inflate slots are the
  named zlib1.dll imports;
- the pool: a .text scan of every E8 to fclose (18, compared with the fl-codex reference list, differences reported),
  each a whole instruction on a linear decode from its function start and no branch into its interior, no other
  reference to fclose (no E9, no relocated pointer); the header redirects 17 of them: the eighteenth (0x1397e4) is the
  CRT's fcloseall loop, called only from the exit-time stdio teardown that only the .rdata pointer 0x150480 reaches;
  the DAT fopen call with its "rb" push and string; the FILE flag word at +0x0c with error 0x10 and EOF 0x08 from
  fread's writes;
- disjointness: the claimed spans overlap no other FL identity window except the music keep's and gz_buffer's
  close_wrapper (0x10696c, rel32 bytes only, clear of the wrapper's gzclose call and return the keep relies on), and
  capture.cpp's FL block installs the reader after the keep and gz_buffer;
- AP refusal: no FL window admits the AP exe, the AP reader/pool addresses do not hold their AP bytes in FL.

usage: verify_resource_reader_sites_fl.py [--exe X3FL.exe] [--out report.json]
"""
import argparse
import json
import re
import struct
import subprocess
import sys
from pathlib import Path

import fl_loading_sites_common as c

SITES = c.ROOT / 'src/engine/fl/resource_reader_sites_fl.h'
MUSIC_INC = c.ROOT / 'src/engine/fl/music_windows_fl_inc.h'
CAPTURE = c.ROOT / 'src/proxy/capture.cpp'
B = c.BASE
# AP addresses (src/proxy/resource_reader.cpp): reader entry, the DAT fopen and close fclose call sites, CRT functions.
AP_SITES = {'reader': (0x4e8880, bytes([0x81, 0xec, 0x54, 0x04, 0x00, 0x00])),
            'fopen_site': (0x4e87ff, None), 'fclose_site': (0x4e9392, None)}
AP_CRT = (0x512213, 0x510343, 0x5128e6, 0x5112c4, 0x50e1b0, 0x512a18, 0x50f8d4)


def header():
    t = SITES.read_text()

    def one(name):
        return int(re.search(r'\b%s = (0x[0-9a-f]+)' % name, t).group(1), 16)

    def array(name):
        body = re.search(r'%s\[[^\]]*\] = \{(.*?)\};' % name, t, re.S).group(1)
        return [int(v, 16) for v in re.findall(r'0x[0-9a-f]+', body)]
    return {'reader': one('reader_rva'), 'reader_bytes': bytes(array('reader_bytes')),
            'fread': one('fread_rva'), 'fseek': one('fseek_rva'), 'ftell': one('ftell_rva'),
            'malloc': one('malloc_rva'), 'free': one('free_rva'), 'counters': array('counter_rva'),
            'globals': array('size_global_rva'), 'inflate_slots': [one('inflate_init_slot_rva'),
                                                                   one('inflate_slot_rva'), one('inflate_end_slot_rva')],
            'fopen': one('fopen_rva'), 'fclose': one('fclose_rva'), 'fopen_site': one('fopen_site_rva'),
            'rb': one('rb_string_rva'), 'flag_offset': one('file_flag_offset'), 'error_flag': one('file_error_flag'),
            'close_sites': array('close_site_rva'), 'exit_close': one('exit_close_rva'),
            'exit_fcloseall': one('exit_fcloseall_rva'), 'exit_teardown': one('exit_teardown_rva'),
            'exit_pointer': one('exit_teardown_pointer_rva')}


def call_target(im, rva):
    raw = im.read(rva, 5)
    return (rva + 5 + struct.unpack_from('<i', raw, 1)[0]) & 0xffffffff if raw[0] == 0xe8 else None


def text_scan(im, opcode, target):
    lo, hi, off = im.text
    data = im.data[off:off + (hi - lo)]
    return [lo + i for i in range(len(data) - 5)
            if data[i] == opcode and (lo + i + 5 + struct.unpack_from('<i', data, i + 1)[0]) & 0xffffffff == target]


def function_start(im, rva, starts):
    """The nearest direct-call target at or before rva (the containing function's entry)."""
    best = max((s for s in starts if s <= rva and rva - s < 0x4000), default=None)
    return best


def linear_hits(im, start, rva):
    for i in im.md.disasm(im.read(start, rva - start + 16), B + start):
        a = i.address - B
        if a == rva:
            return True
        if a > rva:
            return False
    return False


def all_call_targets(im):
    lo, hi, off = im.text
    data = im.data[off:off + (hi - lo)]
    out = set()
    for i in range(len(data) - 5):
        if data[i] == 0xe8:
            t = (lo + i + 5 + struct.unpack_from('<i', data, i + 1)[0]) & 0xffffffff
            if lo <= t < hi:
                out.add(t)
    return out


def regs(insn, kind):
    r, w = insn.regs_access()
    names = {insn.reg_name(x) for x in (r if kind == 'read' else w)}
    return names


def codex_list():
    try:
        text = subprocess.run(['git', 'show', 'fl-codex:src/engine/fl/resource_reader_profile_fl.h'], cwd=c.ROOT,
                              capture_output=True, text=True, check=True).stdout
    except (subprocess.CalledProcessError, FileNotFoundError):
        return None
    m = re.search(r'0x105eb0, \{([^}]*)\}, 18', text)
    return [int(v, 16) for v in m.group(1).split(',')] if m else None


def other_windows():
    """Every identity window of the other FL tables: [(table, name, rva, length, fixups)]."""
    out = []
    for path, prefix in ((MUSIC_INC, 'music'), (c.INC, 'loading')):
        text = path.read_text()
        for table, body, fx in re.findall(r'constexpr CodeWindow (\w+)_windows\[\] = \{(.*?)\n\};\n'
                                          r'constexpr Fixup \1_fixups\[\] = \{(.*?)\n\};', text, re.S):
            if prefix == 'loading' and table in ('reader', 'pool'):
                continue
            fixups = [(int(a, 16), int(b, 16)) for a, b in re.findall(r'\{0x([0-9a-f]+), 0x([0-9a-f]+)\}', fx)]
            for name, rva, length, first, count in re.findall(
                    r'\{"([a-z_0-9]+)", 0x([0-9a-f]+), 0x([0-9a-f]+), (\d+), (\d+), 0x[0-9a-f]+\}', body):
                first, count = int(first), int(count)
                out.append((f'{prefix}.{table}', name, int(rva, 16), int(length, 16), fixups[first:first + count]))
    return out


def verify(exe=c.DEFAULT_EXE, sha256=c.FL_SHA256):
    im = c.Image(exe)
    h = header()
    checks, report = {}, {'exe': str(exe), 'sha256': im.sha256}
    checks['identity'] = (sha256 is None or im.sha256 == sha256) and im.pe.base == B
    c.check_windows(im, 'reader', checks, report)
    c.check_windows(im, 'pool', checks, report)
    rc, rr = c.mutation_refusals(im, 'reader')
    pc, pr = c.mutation_refusals(im, 'pool')
    report['mutations'] = {'reader': [rc, rr], 'pool': [pc, pr]}
    checks['window_mutations_refused'] = rc > 0 and pc > 0 and rc == rr and pc == pr

    # ---- reader entry and the ECX contract ----
    entry = h['reader']
    span = c.music.span_report(im, entry, entry + 6)
    head = list(im.md.disasm(im.read(entry, 0x30), B + entry))
    checks['entry_bytes'] = im.read(entry, 6) == h['reader_bytes'] and span['whole'] and not span['fixups'] \
        and not span['interior_hits'] and [i.mnemonic for i in head[:3]] == ['push', 'mov', 'and'] \
        and head[2].address + head[2].size == B + entry + 6
    ecx_written = eax_read = False
    lea_eax = mov_edi_ecx = None
    for i in head:
        a = i.address - B
        if a == 0x105f61 and i.mnemonic == 'mov' and i.op_str == 'edi, ecx':
            mov_edi_ecx = a
            break
        if 'ecx' in regs(i, 'write'):
            ecx_written = True
        if lea_eax is None and 'eax' in regs(i, 'read'):
            eax_read = True
        if i.mnemonic == 'lea' and i.op_str.startswith('eax,'):
            lea_eax = lea_eax or a
    checks['ecx_contract'] = mov_edi_ecx == 0x105f61 and lea_eax == 0x105f54 and not ecx_written and not eax_read
    callers = c.music.direct_callers(im, entry)
    report['reader_callers'] = [hex(x) for x in callers]
    checks['reader_callers'] = len(callers) == 12 and not c.music.pointers_to(im, B + entry) \
        and not text_scan(im, 0xe9, entry)
    report['entry'] = {'insns': span['insns'][:3], 'interior_hits': span['interior_hits']}

    # ---- core bindings from the bytes ----
    binding = {
        'fread': [call_target(im, 0x106741), call_target(im, 0x1067e6)],
        'fseek': [call_target(im, 0x106904), call_target(im, 0x105ee3)],
        'ftell': [call_target(im, 0x10607f)],
        'malloc': [call_target(im, 0x0d9041), call_target(im, 0x0d9077)],
        'free': [call_target(im, 0x10632b)],
        'allocator': [call_target(im, 0x106025), call_target(im, 0x106095)],
    }
    report['bindings'] = {k: [hex(v) if v is not None else None for v in vs] for k, vs in binding.items()}
    checks['crt_bindings'] = all(all(v == h[k] for v in vs) for k, vs in binding.items() if k != 'allocator') \
        and all(v == 0x0d8fc0 for v in binding['allocator'])
    # EDX=0 at both allocator calls: `xor edx,edx` at 0x106023 / 0x106087, nothing writes EDX up to the call.
    def edx_zero_at(xor, call):
        insns = list(im.md.disasm(im.read(xor, call - xor), B + xor))
        return insns and insns[0].mnemonic == 'xor' and insns[0].op_str == 'edx, edx' \
            and insns[-1].address + insns[-1].size == B + call and not any('edx' in regs(i, 'write') for i in insns[1:])
    edx_zero = edx_zero_at(0x106023, 0x106025) and edx_zero_at(0x106087, 0x106095)
    alloc = {i.address - B: i for i in im.md.disasm(im.read(0x0d908e, 0x20), B + 0x0d908e)}

    def operand(at, length):
        f = [x for x in im.fixups_in(at, at + length)]
        return (im.u32(f[0]) - B) if len(f) == 1 else None
    counters = [operand(0x0d908e, 6), operand(0x0d90a0, 7), operand(0x0d9094, 6), operand(0x0d90a7, 6)]
    alloc_shape = [alloc[0x0d908e].mnemonic, alloc[0x0d9094].mnemonic, alloc[0x0d90a0].op_str,
                   alloc[0x0d90a7].mnemonic, alloc[0x0d909a].op_str, alloc[0x0d909d].op_str]
    report['counters_from_bytes'] = [hex(x) if x is not None else None for x in counters]
    checks['counters'] = counters == h['counters'] and edx_zero and alloc_shape == [
        'add', 'add', 'dword ptr [ebx*4 + 0x%x], edi' % (B + h['counters'][1]), 'inc', 'ebx, 0x18', 'ebx, 0x7f']
    rollback = [operand(0x106330, 6), operand(0x106339, 6)]
    checks['live_pair_rollback'] = rollback == h['counters'][:2]
    stores = list(im.md.disasm(im.read(0x10630e, 10), B + 0x10630e))
    globals_ = [operand(0x10630e, 5), operand(0x106313, 5)]
    report['globals_from_bytes'] = [hex(x) for x in globals_ if x is not None]
    checks['size_globals'] = sorted(globals_) == sorted(h['globals']) and all(
        s.mnemonic == 'mov' and s.op_str.endswith(', eax') for s in stores)
    zlib = c.imports(im, 'zlib1.dll')
    checks['inflate_slots'] = [zlib.get(n) for n in ('inflateInit2_', 'inflate', 'inflateEnd')] == h['inflate_slots']

    # ---- pool: the closers, the DAT open, the FILE flags ----
    found = text_scan(im, 0xe8, h['fclose'])
    codex = codex_list()
    report['close_sites_scanned'] = len(found)
    report['close_sites_vs_codex'] = 'absent' if codex is None else {
        'only_scan': [hex(x) for x in sorted(set(found) - set(codex))],
        'only_codex': [hex(x) for x in sorted(set(codex) - set(found))]}
    starts = all_call_targets(im)
    whole, interior, raw_only = [], [], []
    for s in found:
        f = function_start(im, s, starts)
        whole.append(f is not None and linear_hits(im, f, s))
        for src, tgt in c.raw_branch_hits(im, s, s + 5):
            # A raw E8/E9/EB/Jcc byte pattern counts only when it is an instruction on its function's linear decode.
            g = function_start(im, src, starts)
            (interior if g is None or linear_hits(im, g, src) else raw_only).append((src, tgt))
    report['close_interior_hits'] = [(hex(a), hex(b)) for a, b in interior]
    report['close_raw_pattern_not_instruction'] = [(hex(a), hex(b)) for a, b in raw_only]
    checks['close_sites'] = sorted(found) == sorted(h['close_sites'] + [h['exit_close']]) and len(found) == 18 \
        and len(h['close_sites']) == 17 and all(whole) and not interior and (codex is None or sorted(codex) == sorted(found))
    # The one excluded call: the CRT's fcloseall loop, called only from the exit-time stdio teardown, which nothing calls
    # directly and only the .rdata pointer reaches (the CRT's exit table).
    lo, hi, _ = im.text
    fcloseall_callers = c.music.direct_callers(im, h['exit_fcloseall'])
    teardown_pointers = c.music.pointers_to(im, B + h['exit_teardown'])
    report['exit_close'] = {'site': hex(h['exit_close']), 'function': hex(h['exit_fcloseall']),
                            'callers': [hex(x) for x in fcloseall_callers], 'teardown': hex(h['exit_teardown']),
                            'teardown_callers': [hex(x) for x in c.music.direct_callers(im, h['exit_teardown'])],
                            'teardown_pointers': [hex(x) for x in teardown_pointers]}
    checks['exit_close_excluded'] = h['exit_close'] not in h['close_sites'] \
        and linear_hits(im, h['exit_fcloseall'], h['exit_close']) and len(fcloseall_callers) == 1 \
        and linear_hits(im, h['exit_teardown'], fcloseall_callers[0]) \
        and not c.music.direct_callers(im, h['exit_teardown']) and not text_scan(im, 0xe9, h['exit_teardown']) \
        and teardown_pointers == [h['exit_pointer']] and not lo <= h['exit_pointer'] < hi
    checks['fclose_no_other_reference'] = not text_scan(im, 0xe9, h['fclose']) \
        and not c.music.pointers_to(im, B + h['fclose'])
    opens = text_scan(im, 0xe8, h['fopen'])
    report['fopen_callers'] = [hex(x) for x in opens]
    dat = list(im.md.disasm(im.read(h['fopen_site'] - 6, 6 + 5), B + h['fopen_site'] - 6))
    checks['dat_fopen'] = h['fopen_site'] in opens and call_target(im, h['fopen_site']) == h['fopen'] \
        and dat[0].op_str == hex(B + h['rb']) and dat[0].mnemonic == 'push' \
        and im.read(h['rb'], 3) == b'rb\x00' and not c.raw_branch_hits(im, h['fopen_site'], h['fopen_site'] + 5) \
        and not c.music.pointers_to(im, B + h['fopen'])
    flag = list(im.md.disasm(im.read(0x132b44, 3), B + 0x132b44))
    err = list(im.md.disasm(im.read(0x132c83, 0x1b), B + 0x132c83))
    report['file_flags'] = [(i.address - B, i.mnemonic, i.op_str) for i in flag + err[:4] + err[-2:]]
    checks['file_flags'] = flag[0].op_str == 'eax, [ecx + 0x%x]' % h['flag_offset'] \
        and err[0].op_str == hex(h['error_flag']) and err[3].mnemonic == 'lock or' and err[3].op_str == 'dword ptr [eax], ecx' \
        and err[-2].op_str == '8' and h['error_flag'] != 0x20

    # ---- disjointness from the other FL windows; install order ----
    claims = [('entry', entry, 5), ('fopen_call', h['fopen_site'], 5)] + \
        [(f'fclose_{s:06x}', s, 5) for s in h['close_sites']]
    overlaps = []
    for table, name, rva, length, fixups in other_windows():
        for cname, at, n in claims:
            if at < rva + length and rva < at + n:
                overlaps.append((table, name, cname, rva, length, fixups))
    report['window_overlaps'] = [(t, n, cn) for t, n, cn, _, _, _ in overlaps]
    keep_ok = len(overlaps) == 2 and {t for t, *_ in overlaps} == {'music.keep', 'loading.gz'}
    for t, name, cname, rva, length, fixups in overlaps:
        rel = 0x10696c - rva
        keep_ok = keep_ok and name == 'close_wrapper' and cname == 'fclose_10696c' and rva == 0x106940 \
            and rel + 5 <= length and all(o + 4 <= rel + 1 or o >= rel + 5 for o, _ in fixups)
    # The keep's runtime anchors inside the wrapper: the gzclose call 0x10695d..0x106963 and its return 0x106963.
    keep_ok = keep_ok and 0x10696c >= 0x106963
    capture = re.sub(r'//.*', '', CAPTURE.read_text())
    fl = capture[capture.rindex('#if X3M_EDITION_FL', 0, capture.index('fl::gz_buffer_fl::initialize();')):]
    order = [capture.rindex('music_keep::initialize();', 0, capture.index('fl::gz_buffer_fl::initialize();')),
             capture.index('fl::gz_buffer_fl::initialize();'), capture.index('resource_reader::initialize();',
                                                                                 capture.index('fl::gz_buffer_fl'))]
    checks['keep_window_untouched'] = keep_ok and order == sorted(order) and 'resource_reader::initialize();' in fl
    report['keep_window'] = {'window': '0x106940+0x3a', 'claimed_rel32': '0x10696d..0x106970', 'fixup': '+0x1f',
                             'gzclose_call': '0x10695d', 'return': '0x106963'}

    # ---- AP refusal ----
    if c.AP_EXE.exists():
        ap = c.Image(c.AP_EXE)
        admitted = []
        for table in ('reader', 'pool'):
            for name, rva, length, hh, fixups in c.declared_windows(table):
                try:
                    if c.masked_matches(ap, rva, length, hh, fixups):
                        admitted.append(name)
                except (ValueError, struct.error):
                    pass
        fl_vas = {B + v for v in [entry, h['fopen_site'], h['fread'], h['fseek'], h['ftell'], h['malloc'],
                                  h['free'], h['fopen'], h['fclose'], *h['close_sites']]}
        ap_hits = [hex(va) for va, _ in AP_SITES.values() if va in fl_vas] + [hex(v) for v in AP_CRT if v in fl_vas]
        ap_entry_in_fl = im.read(AP_SITES['reader'][0] - B, 6) == AP_SITES['reader'][1]
        fl_entry_in_ap = ap.read(entry, 6) == h['reader_bytes'] and ap.read(AP_SITES['reader'][0] - B, 6) == \
            AP_SITES['reader'][1]
        ap_call_targets = [call_target(ap, AP_SITES['fopen_site'][0] - B), call_target(ap, AP_SITES['fclose_site'][0] - B)]
        report['ap'] = {'windows_admitted': admitted, 'address_collisions': ap_hits,
                        'ap_entry_bytes_in_fl': ap_entry_in_fl,
                        'ap_pool_call_targets': [hex(x) if x else None for x in ap_call_targets]}
        checks['ap_addresses_refused'] = not admitted and not ap_hits and not ap_entry_in_fl and not fl_entry_in_ap \
            and ap_call_targets == [0x512a18 - B, 0x50f8d4 - B]
    else:
        report['ap'] = 'ap_exe_absent'
        checks['ap_addresses_refused'] = False
    return c.finish(report, checks)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--exe', type=Path, default=c.DEFAULT_EXE)
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
