#!/usr/bin/env python3
"""Read-only qualification of the FL dust leak fix site in X3FL.exe (src/engine/fl/dust_leak_fix_sites_fl.h,
dust_leak_fix_fl.cpp).

docs/reverse-engineering/fl/dust-light.md, section 1 and "Port (2026-11-03)". Checks, on the installed exe (default
/mnt/c/X3FL/X3FL.exe, SHA-256 e7f3aad8..., the image the constants were measured on; preferred base 0x400000, every
value an RVA):

- every window of dust_leak_fix_sites_fl.h (alloc, body, tail, release entry, both release exits, attach, manager clear)
  holds the header's bytes after zeroing the HIGHLOW fields, the .reloc census inside each window is exactly the
  header's declared fixups (one: the clear's manager slot), and each FNV-1a equals the header's constant;
- whole-instruction decoding (Capstone) of the updater 0x020660..0x020c3b from its entry (ends on `ret 0xc`); the
  claimed span 0x020ac4..0x020ace is `cmp ebx, [esp + 0x18]` (4) + `jl 0x420950` (6) with no relocation, the JL's rel32
  at span offset 6; the five-byte jump crosses an aligned qword (plain write);
- the five arrivals: the four failure branches 0x02095a je / 0x020a19 js / 0x020a2f jne / 0x020a3e je reach the span
  start, the attach call 0x020abf falls through to it; no other raw E8/E9/EB/7x/0F8x encoding in .text targets the span
  start, none targets its interior, and no relocated dword points into it;
- the never-claim starts: 0x020ac5 (the CMP's second byte) and 0x0e4908 (inside the light decision's CVTTSS2SI at
  0x0e4903, decoded from 0x0e48fb) are not instruction starts, and the header's site is neither;
- register and stack contract at the tail: ESI = the allocation result (0x020956) and not written in the loop body
  before the site; EBX incremented once at the loop head and not written otherwise; the stack depth (pushes, pops and
  each callee's `ret n`) is 0 at the loop head, at every failure source and at the site, so [ESP+0x18] is the count
  slot the loop's pre-check 0x02093c compares and 0x020732/0x02077b write;
- the allocator 0x0a2200 zeroes the 0x270 block (`push 0x270; push 0; push edi; call memset`), so a newborn has
  +0x1c = 0; attach 0x0a56b0 takes ECX = node (`mov ebp, ecx`), writes +0x18 = 0 and +0x1c = the pushed scene, `ret 4`;
- the release 0x0a3300: ECX = node (`mov ebp, ecx` before any EBP-relative read), no stack argument (no read through
  the frame EBP; every return is a plain `ret`), EBX/ESI/EDI/EBP saved by the prologue and popped by both epilogues;
  the manager clear calls it with `mov ecx, eax` and pushes nothing;
- after the loop exit 0x020ace: EBP and EBX written before read, ESI (the freed node) not read before 0x020b28
  redefines it, and nothing reads the count slot;
- the stub (dust_leak_fix_sites_fl.h stub_code): its decode, both skips to the POPFD, the field offsets, balanced
  stack, and the registers it writes (EBX only, besides the saved/restored ones).

Writes one JSON report to stdout (result PASS/FAIL). No Wine, no game launch, nothing written to the exe.

usage: python3 verification/probe/verify_dust_leak_fix_site_fl.py [X3FL.exe] [--exe X3FL.exe] [--out report.json]
"""
import argparse
import json
import re
import sys
from pathlib import Path

import capstone
import capstone.x86 as x86

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'verification/probe'))
from verify_fov_site_fl import Image, direct_callers, fnv1a, raw_branch_hits, reads, writes  # noqa: E402

SITES = ROOT / 'src/engine/fl/dust_leak_fix_sites_fl.h'
DEFAULT_EXE = Path('/mnt/c/X3FL/X3FL.exe')
FL_SHA256 = 'e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8'
BASE = 0x400000
UPDATER = (0x020660, 0x020c3b)
RELEASE = (0x0a3300, 0x0a35d7)
ATTACH = (0x0a56b0, 0x0a5740)
ALLOC = (0x0a2200, 0x0a228a)
LIGHT = (0x0e48fb, 0x0e491b)
CLEAR = (0x08a875, 0x08a894)
WINDOWS = (('alloc_mismatch', 'alloc_window_rva', 'expected_alloc', 'alloc_fnv1a'),
           ('body_mismatch', 'body_window_rva', 'expected_body', 'body_fnv1a'),
           ('tail_mismatch', 'tail_window_rva', 'expected_tail', 'tail_fnv1a'),
           ('release_mismatch', 'release_rva', 'expected_release', 'release_fnv1a'),
           ('release_exit_mismatch', 'release_exit_rvas[0]', 'expected_release_exit', 'release_exit_fnv1a'),
           ('release_exit_mismatch', 'release_exit_rvas[1]', 'expected_release_exit', 'release_exit_fnv1a'),
           ('attach_mismatch', 'attach_rva', 'expected_attach', 'attach_fnv1a'),
           ('clear_mismatch', 'clear_rva', 'expected_clear', 'clear_fnv1a'))
DECLARED_FIXUPS = {'clear_mismatch': [(0x13, 'manager_slot_rva')]}
EBX = {'ebx', 'bx', 'bl', 'bh'}
ESI = {'esi', 'si'}
EBP = {'ebp', 'bp'}

LOG_RE = re.compile(r'\bdust_leak_fix site=(?P<site>[0-9a-f]{8}) rva=(?P<rva>[0-9a-f]{6}) edition=fl '
                    r'status=(?P<status>patched|patched_unverified|off|refused) reason=(?P<reason>\S+) '
                    r'mode=(?P<mode>on|off|-) setting=(?P<setting>[!-~]+) write=(?P<write>none|atomic|plain) '
                    r'stub=(?P<stub>[0-9a-f]{8})')
RESTORE_RE = re.compile(r'\bdust_leak_fix_restore site=(?P<site>[0-9a-f]{8}) rva=(?P<rva>[0-9a-f]{6}) edition=fl '
                        r'status=(?P<status>restored|restore_not_owned|restore_failed|restore_retained) '
                        r'found=(?P<found>[0-9a-f]{10}|--) registered=(?P<registered>[01])')
HITS_RE = re.compile(r'\bdust_leak_fix hits=(?P<hits>\d+) total=(?P<total>\d+) frame=(?P<frame>\d+) edition=fl')


def parse_log_line(line):
    """The one FL `dust_leak_fix site=` install row -> dict, or None."""
    m = LOG_RE.search(line)
    if not m:
        return None
    d = m.groupdict()
    return {'site': int(d['site'], 16), 'rva': int(d['rva'], 16), 'status': d['status'], 'reason': d['reason'],
            'mode': d['mode'], 'setting': d['setting'], 'write': d['write'], 'stub': int(d['stub'], 16),
            'patched': d['status'] == 'patched'}


def parse_restore_line(line):
    """The FL `dust_leak_fix_restore` row -> dict, or None."""
    m = RESTORE_RE.search(line)
    if not m:
        return None
    d = m.groupdict()
    return {'site': int(d['site'], 16), 'status': d['status'],
            'found': None if d['found'] == '--' else bytes.fromhex(d['found']), 'registered': d['registered'] == '1'}


def parse_hits_line(line):
    """The FL 300-frame `dust_leak_fix hits=` row -> dict, or None."""
    m = HITS_RE.search(line)
    return {k: int(v) for k, v in m.groupdict().items()} if m else None


def header_constants(text):
    values = {}
    for name, value in re.findall(r'\b([a-z_0-9]+)\s*=\s*(0x[0-9a-fA-F]+|\d+)\b', text):
        values.setdefault(name, int(value, 0))
    for name, body in re.findall(r'\b([a-z_]+)\[\d\] = \{([^}]*)\}', text):
        for k, v in enumerate(re.findall(r'0x[0-9a-fA-F]+', body)):
            values['%s[%d]' % (name, k)] = int(v, 16)
    arrays = {}
    for name, body in re.findall(r'constexpr unsigned char ([a-z_]+)\[[a-z_]+\] = \{(.*?)\};', text, re.S):
        arrays[name] = bytes(int(v, 16) for v in re.findall(r'0x([0-9a-fA-F]{2})', body))
    return values, arrays


def callee_pop(im, target, cache):
    """The `ret n` of a direct callee (first RET in its linear decode, at most 4 KB), cached."""
    if target not in cache:
        insns = im.md.disasm(im.read(target, 0x1000), BASE + target)
        pop = None
        for i in insns:
            if i.mnemonic == 'ret':
                pop = int(i.op_str, 0) if i.op_str else 0
                break
        cache[target] = pop
    return cache[target]


def esp_depths(im, insns, cache):
    """Bytes pushed below the starting ESP before each instruction of a linear decode (calls net of the callee's pop)."""
    depth, out = 0, {}
    for i in insns:
        out[i.address - BASE] = depth
        if i.mnemonic == 'push':
            depth += 4
        elif i.mnemonic == 'pop':
            depth -= 4
        elif i.mnemonic in ('add', 'sub') and i.op_str.startswith('esp, '):
            n = int(i.op_str.split(', ')[1], 0)
            depth += -n if i.mnemonic == 'add' else n
        elif i.mnemonic == 'call':
            op = i.operands[0]
            pop = callee_pop(im, op.imm - BASE, cache) if op.type == x86.X86_OP_IMM else None
            if pop is None:
                return None
            depth -= pop
    return out


def esp_slot_reads(insns, depths, slot):
    """Instructions whose memory operand is [esp + slot + depth] (the frame slot at this depth)."""
    hits = []
    for i in insns:
        for op in i.operands:
            if op.type == x86.X86_OP_MEM and op.mem.base == x86.X86_REG_ESP and op.mem.index == 0 and \
                    op.mem.disp == slot + depths[i.address - BASE]:
                hits.append(hex(i.address - BASE))
    return hits


def verify(exe=DEFAULT_EXE, sites_path=SITES, sha256=FL_SHA256):
    """The report; sha256=None skips the hash gate (mutated copies in the host test)."""
    v, arrays = header_constants(Path(sites_path).read_text())
    im = Image(exe)
    checks = {}
    report = {'exe': str(exe), 'sha256': im.sha256, 'checks': checks}
    checks['identity'] = (sha256 is None or im.sha256 == sha256) and im.pe.base == BASE and im.other_fixup_types == 0
    # ---- windows ----
    rows, all_ok = [], True
    for name, rva_key, array, fnv_key in WINDOWS:
        expected, rva = arrays[array], v[rva_key]
        live = bytearray(im.read(rva, len(expected)))
        census = [f - rva for f in im.fixups_in(rva - 3, rva + len(expected))]
        declared = DECLARED_FIXUPS.get(name, [])
        pointed = [im.u32(rva + o) - BASE for o in census]
        for o in census:
            live[o:o + 4] = b'\0' * 4
        h = fnv1a(bytes(live))
        ok = (bytes(live) == expected and census == [o for o, _ in declared]
              and pointed == [v[k] for _, k in declared] and h == v[fnv_key])
        rows.append({'name': name, 'rva': hex(rva), 'length': len(expected), 'fnv1a': '%08x' % h,
                     'fixups': [hex(o) for o in census], 'targets': [hex(t) for t in pointed], 'ok': ok})
        all_ok = all_ok and ok
    report['windows'] = rows
    checks['windows_bytes_fixups_hashes'] = all_ok and len(rows) == 8
    # ---- the updater, the span ----
    upd, upd_ok = im.insns(*UPDATER)
    starts = {i.address - BASE: i for i in upd}
    report['updater_instructions'] = len(upd)
    checks['updater_decode'] = (upd_ok and starts.get(v['updater_ret_rva']) is not None
                                and starts[v['updater_ret_rva']].mnemonic == 'ret'
                                and starts[v['updater_ret_rva']].op_str == '0xc'
                                and v['updater_ret_rva'] + 3 == v['updater_end_rva'] == UPDATER[1])
    span = [i for i in upd if v['site_rva'] <= i.address - BASE < v['exit_rva']]
    checks['span_two_whole_instructions'] = ([(i.address - BASE, i.mnemonic, i.op_str, i.size) for i in span] == [
        (v['site_rva'], 'cmp', 'ebx, dword ptr [esp + 0x18]', 4),
        (v['jl_rva'], 'jl', hex(BASE + v['loop_rva']), 6)] and span[1].bytes[:2] == b'\x0f\x8c'
        and v['site_length'] == 10 and v['site_rel32_offset'] == 6 and arrays['expected_site'] == im.read(v['site_rva'], 10))
    checks['span_no_relocation'] = not im.fixups_in(v['site_rva'] - 3, v['exit_rva'])
    checks['jump_crosses_qword'] = (v['site_rva'] & 7) + 5 > 8
    report['span'] = {'rva': hex(v['site_rva']), 'bytes': im.read(v['site_rva'], 10).hex(),
                      'qword_offset': v['site_rva'] & 7}
    # ---- the five arrivals; nothing else reaches the span ----
    sources = [v['failure_sources[%d]' % k] for k in range(4)]
    edges = [(starts[s].mnemonic, int(starts[s].op_str, 16) - BASE) if s in starts else None for s in sources]
    report['arrivals'] = {hex(s): e[0] if e else None for s, e in zip(sources, edges)}
    attach_call = starts.get(v['attach_call_rva'])
    checks['five_arrivals'] = ([e[0] if e else None for e in edges] == ['je', 'js', 'jne', 'je']
                               and all(e and e[1] == v['site_rva'] for e in edges)
                               and attach_call is not None and attach_call.mnemonic == 'call'
                               and int(attach_call.op_str, 16) - BASE == v['attach_rva']
                               and v['attach_call_rva'] + attach_call.size == v['site_rva'])
    to_start = raw_branch_hits(im, v['site_rva'] - 1, v['site_rva'] + 1)
    interior = raw_branch_hits(im, v['site_rva'], v['exit_rva'])
    pointers = sorted(f for f in im.fixups if v['site_rva'] <= im.u32(f) - BASE < v['exit_rva'])
    report['raw_edges_to_site'] = [hex(s) for s, _ in to_start]
    report['raw_edges_into_span'] = [hex(s) for s, _ in interior]
    checks['only_the_four_branches_reach_the_span'] = sorted(s for s, _ in to_start) == sorted(sources)
    checks['no_edge_into_span_interior'] = not interior and not pointers
    # ---- never-claim starts ----
    light, light_ok = im.insns(*LIGHT)
    light_starts = {i.address - BASE for i in light}
    cvt = next((i for i in light if i.mnemonic == 'cvttss2si'), None)
    never = [v['never_claim[0]'], v['never_claim[1]']]
    checks['never_claim_refused'] = (never == [0x020ac5, 0x0e4908] and never[0] not in starts and light_ok
                                     and never[1] not in light_starts and cvt is not None
                                     and cvt.address - BASE < never[1] < cvt.address - BASE + cvt.size + 6
                                     and v['site_rva'] not in never and v['site_rva'] in starts)
    report['never_claim'] = {hex(a): 'interior' for a in never}
    # ---- tail register and stack contract ----
    body = [i for i in upd if v['loop_rva'] <= i.address - BASE < v['site_rva']]
    mov_esi = starts.get(0x020956)
    esi_writes = [hex(i.address - BASE) for i in body if writes(i, ESI) and i.mnemonic != 'call'
                  and i.address - BASE != 0x020956]
    ebx_writes = [hex(i.address - BASE) for i in body if writes(i, EBX) and i.mnemonic != 'call'
                  and i.address - BASE != v['loop_rva']]
    checks['esi_is_the_node'] = (mov_esi is not None and mov_esi.op_str == 'esi, eax' and not esi_writes
                                 and int(starts[0x020951].op_str, 16) - BASE == v['alloc_rva'])
    checks['ebx_is_the_fill_count'] = starts[v['loop_rva']].mnemonic == 'inc' and starts[v['loop_rva']].op_str == 'ebx' \
        and not ebx_writes
    cache = {}
    loop_insns = [i for i in upd if v['loop_rva'] <= i.address - BASE <= v['site_rva']]
    depths = esp_depths(im, loop_insns, cache)
    pre = starts.get(0x02093c)
    count_writes = [hex(a) for a in (0x020732, 0x02077b) if a in starts and starts[a].op_str == 'dword ptr [esp + 0x18], eax']
    report['tail_depths'] = {hex(a): depths[a] for a in sources + [v['site_rva']]} if depths else None
    checks['count_slot_at_the_tail'] = (depths is not None and all(depths[a] == 0 for a in sources + [v['site_rva']])
                                        and pre is not None and pre.op_str == 'ebx, dword ptr [esp + 0x18]'
                                        and len(count_writes) == 2)
    # ---- allocator zeroes; attach ----
    alloc, _ = im.insns(*ALLOC)
    ops = [(i.mnemonic, i.op_str) for i in alloc]
    zero = any(ops[k:k + 3] == [('push', '0x270'), ('push', '0'), ('push', 'edi')] and ops[k + 3][0] == 'call'
               for k in range(len(ops) - 3))
    checks['newborn_scene_zero'] = zero and v['node_size'] == 0x270
    att, att_ok = im.insns(*ATTACH)
    aops = [(i.address - BASE, i.mnemonic, i.op_str) for i in att]
    checks['attach_contract'] = (att_ok and (0x0a56b2, 'mov', 'ebp, ecx') in aops
                                 and (0x0a56cd, 'mov', 'dword ptr [ebp + 0x18], 0') in aops
                                 and (v['attach_scene_store_rva'], 'mov', 'dword ptr [ebp + 0x1c], ebx') in aops
                                 and (0x0a56c9, 'mov', 'ebx, dword ptr [esp + 0x14]') in aops
                                 and [o for o in aops if o[1] == 'ret'] == [(0x0a573d, 'ret', '4')])
    # ---- the release ----
    rel, rel_ok = im.insns(*RELEASE)
    rops = [(i.address - BASE, i.mnemonic, i.op_str) for i in rel]
    prologue = [(m, o) for _, m, o in rops[:9]]
    ebp_set = next(k for k, i in enumerate(rel) if i.address - BASE == 0x0a330d)
    frame_reads = [hex(i.address - BASE) for i in rel[2:ebp_set]  # memory through the frame EBP (a stack argument)
                   if any(op.type == x86.X86_OP_MEM and op.mem.base == x86.X86_REG_EBP for op in i.operands)]
    ebp_writes = [(a, m, o) for (a, m, o), i in zip(rops, rel) if writes(i, EBP)]
    rets = [(a, o) for a, m, o in rops if m.startswith('ret')]
    epilogue = [('pop', 'edi'), ('pop', 'esi'), ('pop', 'ebp'), ('pop', 'ebx'), ('mov', 'esp, ebp'), ('pop', 'ebp'),
                ('ret', '')]
    exits = [[(m, o) for a, m, o in rops if e <= a < e + 8] for e in (v['release_exit_rvas[0]'], v['release_exit_rvas[1]'])]
    checks['release_ecx_node_plain_ret'] = (
        rel_ok and prologue == [('push', 'ebp'), ('mov', 'ebp, esp'), ('and', 'esp, 0xfffffff8'), ('sub', 'esp, 8'),
                                ('push', 'ebx'), ('push', 'ebp'), ('push', 'esi'), ('push', 'edi'), ('mov', 'ebp, ecx')]
        and not frame_reads and rets == [(v['release_exit_rvas[0]'] + 7, ''), (v['release_exit_rvas[1]'] + 7, '')]
        and exits == [epilogue, epilogue]
        and [(a, m) for a, m, _ in ebp_writes] == [(0x0a3301, 'mov'), (0x0a330d, 'mov'), (0x0a359c, 'pop'),
                                                  (0x0a35a0, 'pop'), (0x0a35d1, 'pop'), (0x0a35d5, 'pop')])
    report['release'] = {'instructions': len(rel), 'rets': [hex(a) for a, _ in rets],
                         'direct_callers': len(direct_callers(im, v['release_rva']))}
    clear, clear_ok = im.insns(*CLEAR)
    cops = [(i.address - BASE, i.mnemonic, i.op_str) for i in clear]
    checks['clear_calls_release_with_ecx'] = (clear_ok and (0x08a880, 'mov', 'ecx, eax') in cops
                                              and (v['clear_call_rva'], 'call', hex(BASE + v['release_rva'])) in cops
                                              and not any(m == 'push' for _, m, _ in cops))
    # ---- after the exit ----
    after = [i for i in upd if i.address - BASE >= v['exit_rva']]
    first_esi_write = next((k for k, i in enumerate(after) if writes(i, ESI)), None)
    esi_read = [hex(i.address - BASE) for i in after[:first_esi_write or 0] if reads(i, ESI)]
    exit_depths = esp_depths(im, after, cache)
    slot_reads = esp_slot_reads(after, exit_depths, 0x18) if exit_depths else ['untracked']
    checks['exit_liveness'] = ([(i.mnemonic, i.op_str) for i in after[:2]] == [('mov', 'ebp, dword ptr [esp + 0x5c]'),
                                                                               ('mov', 'ebx, dword ptr [esp + 0x54]')]
                               and first_esi_write is not None and after[first_esi_write].address - BASE == 0x020b28
                               and not esi_read and not slot_reads)
    report['exit'] = {'esi_redefined': hex(after[first_esi_write].address - BASE) if first_esi_write is not None else None,
                      'count_slot_reads': slot_reads}
    # ---- the stub ----
    stub = arrays['stub_code']
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    sins = list(md.disasm(stub, 0))
    smap = {i.address: i for i in sins}
    pushes = sum(1 for i in sins if i.mnemonic in ('push', 'pushfd')) - 1  # the PUSH EAX for SetLastError is popped by it
    pops = sum(1 for i in sins if i.mnemonic in ('pop', 'popfd'))
    written = set()
    for i in sins:
        written |= {i.reg_name(r) for r in i.regs_access()[1]}
    checks['stub_shape'] = (sum(i.size for i in sins) == len(stub) == v['stub_length']
                            and smap[3].mnemonic == 'je' and int(smap[3].op_str, 16) == v['stub_done']
                            and smap[9].mnemonic == 'jne' and int(smap[9].op_str, 16) == v['stub_done']
                            and smap[v['stub_done']].mnemonic == 'popfd' and sins[0].mnemonic == 'pushfd'
                            and smap[5].op_str == 'dword ptr [esi + 0x1c], 0'
                            and [smap[a - 1].mnemonic for a in (v['get_error_rel32_offset'], v['release_rel32_offset'],
                                                                v['set_error_rel32_offset'])] == ['call'] * 3
                            and smap[0x24].op_str == 'ecx, esi' and smap[0x3a].op_str == 'ebx, dword ptr [esp + 0x1c]'
                            and smap[0x1a].mnemonic == 'fxsave' and smap[0x30].mnemonic == 'fxrstor'
                            and smap[v['hits_abs32_offset'] - 2].mnemonic == 'inc'
                            and smap[v['slot_abs32_offset'] - 2].mnemonic == 'jmp' and pushes == pops
                            and 'ebx' in written and not written & {'esi', 'edi'})
    report['stub'] = {'length': len(stub), 'instructions': len(sins), 'hex': stub.hex()}
    report['result'] = 'PASS' if all(checks.values()) else 'FAIL'
    report['failed'] = [k for k, ok in checks.items() if not ok]
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('exe_positional', nargs='?', type=Path)
    parser.add_argument('--exe', type=Path, default=None)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    report = verify(args.exe or args.exe_positional or DEFAULT_EXE)
    text = json.dumps(report, indent=1, sort_keys=True)
    if args.out:
        args.out.write_text(text + '\n')
    print(text)
    return 0 if report['result'] == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
