#!/usr/bin/env python3
"""Read-only qualification of the FL combat trigger (src/engine/fl/music_alert_sites_fl.h, music_alert_fl.cpp).

docs/reverse-engineering/fl/music-alert.md, sections 1 to 5 and "Port (2026-11-03)". Checks on the installed exe (default
/mnt/c/X3FL/X3FL.exe, SHA-256 e7f3aad8...; every value an RVA):

- the alert windows of music_windows_fl_inc.h equal the windows regenerated from the exe;
- the alert routine 0x02f910..0x02fbe7 decodes gap-free (235 instructions) with one plain RET, no SIMD/x87 instruction,
  EBX = [M+0x38] (the player, M = [base+0x1a82ec]) and EBP = ECX (the registry) at its head, EBP/EBX not written again
  except EBP's spill/reload pair before the seam; the seam 0x02fada is `mov ecx,[ebp+8]; mov eax,[ebp+4]` (6 bytes,
  no relocation, the jump inside one qword), followed by `cmp ecx,eax`; the only raw branch into the span is the JLE at
  0x02fad5 onto its first byte;
- caller sets: alert <- 0x01e55b only, registry update <- 0x0782ca only (ECX = EBP = [base+0x1a82e4] at the call), no
  relocated pointer to either entry;
- the clock bindings: the frame routine writes the step at cfg+0x704 from the clock cfg+0x708 (cfg = [base+0x1a83b0]),
  TI_GetAbsTime reads cfg+0x708; the header's FL offsets are AP's minus one common 0x10;
- AP refusal: no FL window admits the AP exe; AP's seam bytes (0x0042c974) are not at that VA in FL.

Writes one JSON report (result PASS/FAIL). usage: verify_music_alert_sites_fl.py [X3FL.exe] [--out report.json]
"""
import argparse
import json
import re
import sys
from pathlib import Path

import fl_music_sites_common as c

SITES = c.ROOT / 'src/engine/fl/music_alert_sites_fl.h'
AP_CORE = c.ROOT / 'src/proxy/music_alert_core.h'


def verify(exe=c.DEFAULT_EXE, sha256=c.FL_SHA256):
    im = c.Image(exe)
    checks, report = {}, {'exe': str(exe), 'sha256': im.sha256}
    if sha256:
        checks['exe_sha256'] = im.sha256 == sha256
    text = SITES.read_text()
    v = {k: int(x, 16) for k, x in re.findall(r'\b([a-z_0-9]+) = (0x[0-9a-f]+)', text)}
    c.check_windows(im, 'alert', checks, report)
    B = c.BASE
    body, ok = im.insns(v['alert_rva'], v['alert_end_rva'])
    report['alert_instructions'] = len(body)
    checks['alert_gap_free'] = ok and len(body) == 235
    rets = [i for i in body if i.mnemonic == 'ret']
    checks['alert_plain_rets'] = len(rets) == 3 and all(not i.op_str for i in rets)
    simd = [i.mnemonic for i in body if i.mnemonic.startswith('f') or 'xmm' in i.op_str or 'mm' in i.op_str.split(',')[0]]
    checks['alert_no_simd_x87'] = not simd
    head = [(i.mnemonic, i.op_str) for i in body[:7]]
    checks['alert_head_registers'] = head == [
        ('sub', 'esp, 0x14'), ('mov', 'eax, dword ptr [%s]' % hex(B + v['root_slot_rva'])), ('push', 'ebx'),
        ('push', 'ebp'), ('push', 'edi'), ('mov', 'ebx, dword ptr [eax + 0x38]'), ('mov', 'ebp, ecx')]
    # EBX never written after the head; EBP only by the spill/reload of the registry local before the seam.
    site_index = next(k for k, i in enumerate(body) if i.address - B == v['site_rva'])
    ebx_writes, ebp_writes = [], []
    for i in body[7:site_index]:
        _, w = i.regs_access()
        names = {i.reg_name(x) for x in w}
        if names & {'ebx', 'bx', 'bl', 'bh'}:
            ebx_writes.append(hex(i.address))
        if names & {'ebp', 'bp'}:
            ebp_writes.append((hex(i.address), i.op_str))
    report['ebp_writes_before_seam'] = ebp_writes
    checks['seam_ebx_player'] = not ebx_writes
    # EBP = R at the seam on every path: the spill `mov [esp+0xc],ebp` at the head; every other EBP write must reach a
    # reload `mov ebp,[esp+0x10]` (the same local while the walk's ESI is pushed) before the seam (a CFG walk over the
    # decoded body: fall-through and direct branch targets).
    by_addr = {i.address - B: k for k, i in enumerate(body)}
    reload = 'ebp, dword ptr [esp + 0x10]'

    def reaches_seam_unreloaded(start):
        seen, todo = set(), [start]
        while todo:
            k = todo.pop()
            if k in seen or k >= len(body):
                continue
            seen.add(k)
            i = body[k]
            if i.address - B == v['site_rva']:
                return True
            if i.mnemonic == 'mov' and i.op_str == reload:
                continue
            if i.mnemonic == 'ret':
                continue
            if i.mnemonic.startswith('j'):
                try:
                    t = int(i.op_str, 16) - B
                except ValueError:
                    return True
                if t in by_addr:
                    todo.append(by_addr[t])
                if i.mnemonic == 'jmp':
                    continue
            todo.append(k + 1)
        return False
    clobbers = [k for k, i in enumerate(body[7:site_index], 7)
                if {i.reg_name(x) for x in i.regs_access()[1]} & {'ebp', 'bp'} and i.op_str != reload]
    report['ebp_clobbers'] = len(clobbers)
    checks['seam_ebp_registry'] = body[8].op_str == 'dword ptr [esp + 0xc], ebp' and \
        not any(reaches_seam_unreloaded(k + 1) for k in clobbers)
    span = c.span_report(im, v['site_rva'], v['continue_rva'] + 2, decode_from=v['alert_rva'])
    checks['seam_span'] = span['whole'] and [x[1:] for x in span['insns'][:3]] == [
        ('mov', 'ecx, dword ptr [ebp + 8]'), ('mov', 'eax, dword ptr [ebp + 4]'), ('cmp', 'ecx, eax')] and \
        not span['fixups'] and span['atomic_jump'] and im.read(v['site_rva'], 6) == bytes([0x8b, 0x4d, 0x08, 0x8b, 0x45, 0x04])
    edges = c.raw_branch_hits(im, v['site_rva'] - 1, v['continue_rva'])
    report['seam_edges'] = [(hex(s), hex(t)) for s, t in edges]
    checks['seam_only_edge'] = edges == [(v['site_branch_rva'], v['site_rva'])]
    checks['callers_alert'] = c.direct_callers(im, v['alert_rva']) == [v['alert_call_rva']] and \
        not c.pointers_to(im, B + v['alert_rva'])
    checks['callers_registry_update'] = c.direct_callers(im, v['registry_update_rva']) == [v['main_loop_call_rva']] and \
        not c.pointers_to(im, B + v['registry_update_rva'])
    upd, _ = im.insns(v['registry_update_rva'], v['alert_call_rva'] + 5)
    checks['registry_in_ecx'] = [(i.mnemonic, i.op_str) for i in upd[2:5]] == [
        ('mov', 'ebp, dword ptr [%s]' % hex(B + v['registry_slot_rva'])), ('mov', 'ecx, ebp'), ('push', 'edi')]
    clock, _ = im.insns(0x10b600, 0x10b666)
    ck = {(i.mnemonic, i.op_str) for i in clock}
    checks['clock_offsets'] = ('mov', 'edi, dword ptr [%s]' % hex(B + v['cfg_slot_rva'])) in ck and \
        ('mov', 'eax, dword ptr [edi + 0x708]') in ck and ('mov', 'dword ptr [edi + 0x704], ecx') in ck and \
        v['cfg_clock'] == 0x708 and v['cfg_step'] == 0x704 and v['cfg_seta_a'] == 0xbc and v['cfg_seta_b'] == 0xc0
    ti, _ = im.insns(0x0ce057, 0x0ce077)
    checks['ti_abs_time'] = ti[0].op_str == 'eax, dword ptr [%s]' % hex(B + v['cfg_slot_rva']) and \
        ti[2].op_str == 'dword ptr [eax + 0x708]'
    ap = {k: int(x, 16) for k, x in re.findall(r'\b(cfg_[a-z_]+) = (0x[0-9a-f]+)', AP_CORE.read_text())}
    checks['clock_bias_0x10'] = all(ap[k] - v[k] == 0x10 for k in ('cfg_step', 'cfg_clock', 'cfg_seta_a', 'cfg_seta_b'))
    c.ap_refusal('alert', checks, report, {'ap_seam': (0x0042c974, bytes([0x8b, 0x4e, 0x08, 0x8b, 0x46, 0x04]))})
    return c.finish(report, checks)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('exe', nargs='?', type=Path, default=c.DEFAULT_EXE)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    report = verify(args.exe)
    if args.out:
        args.out.write_text(json.dumps(report, indent=1, sort_keys=True) + '\n')
    print(json.dumps({k: report[k] for k in ('result', 'failed', 'alert_window_count', 'alert_fixup_count',
                                             'alert_instructions', 'seam_edges')}, sort_keys=True))
    return 0 if report['result'] == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
