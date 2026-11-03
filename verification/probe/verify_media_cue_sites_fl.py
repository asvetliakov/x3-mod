#!/usr/bin/env python3
"""Read-only qualification of the FL media-cue gate (src/engine/fl/media_cue_sites_fl.h, media_cue_fl.cpp) on X3FL.exe.

docs/reverse-engineering/fl/media-cue.md, sections 1 to 4 and "Port (2026-11-03)". Checks on the installed exe (default
/mnt/c/X3FL/X3FL.exe, SHA-256 e7f3aad8..., preferred base 0x400000, every value an RVA):

- the media windows of music_windows_fl_inc.h equal the windows regenerated from the exe (FNV-1a with HIGHLOW zeroed,
  the .reloc census and targets, whole instructions);
- the allocator 0x0b4080..0x0b41a4 decodes gap-free (91 instructions) with plain RETs only at 0x0b40f6 and 0x0b41a3,
  no explicit ESP memory operand anywhere (no stack argument, the return slot never read), ECX copied to EBX and EDX to
  EDI before any other read of either, and the failure exit `xor eax,eax; pop edi/esi/ebx; ret`;
- the claimed span 0x0b4080 is `push ebx; push esi; push edi; mov edi, edx` (5 bytes, no relocation, no relative
  operand), the five-byte jump fits one aligned qword, and no raw branch encoding in .text lands inside it;
- caller sets: the allocator's only direct caller is the wrapper's 0x0b46be; the wrapper's direct callers are exactly
  the header's six, with their returns; no relocated dword points at either entry; the wrapper loads its id/flags
  stack arguments into ECX/EDX after two pushes, so [esp+0xc] at the allocator is the wrapper caller's return;
- the sector selector passes (id, 0, 0) with ECX = EDX = 0 and assigns kind 0x5a at 0x066303 before its play call;
- the video consumer's destination LockRect / UnlockRect calls return inside 0x0fed10..0x0ff578;
- AP refusal: no FL window admits the AP exe, and AP's allocator entry bytes (0x00498140) are not at that VA in FL.

Writes one JSON report (result PASS/FAIL). usage: verify_media_cue_sites_fl.py [X3FL.exe] [--out report.json]
"""
import argparse
import json
import re
import sys
from pathlib import Path

import fl_music_sites_common as c

SITES = c.ROOT / 'src/engine/fl/media_cue_sites_fl.h'


def verify(exe=c.DEFAULT_EXE, sha256=c.FL_SHA256):
    im = c.Image(exe)
    checks, report = {}, {'exe': str(exe), 'sha256': im.sha256}
    if sha256:
        checks['exe_sha256'] = im.sha256 == sha256
    text = SITES.read_text()
    v = {k: int(x, 16) for k, x in re.findall(r'\b([a-z_]+_rva) = (0x[0-9a-f]+)', text)}
    callers = [(int(a, 16), int(b, 16), kind) for a, b, kind in
               re.findall(r'\{(0x[0-9a-f]+), (0x[0-9a-f]+), detail::([a-z]+)\}', text)]
    report['wrapper_callers'] = len(callers)
    c.check_windows(im, 'media', checks, report)
    # ---- the allocator: whole routine, ABI, exits ----
    alloc, ok = im.insns(v['allocator_rva'], 0x0b41a4)
    report['allocator_instructions'] = len(alloc)
    checks['allocator_gap_free'] = ok and len(alloc) == 91
    rets = [i.address - c.BASE for i in alloc if i.mnemonic == 'ret']
    checks['allocator_plain_rets'] = rets == [0x0b40f6, 0x0b41a3] and all(not i.op_str for i in alloc if i.mnemonic == 'ret')
    esp_mem = [hex(i.address) for i in alloc for op in i.operands
               if op.type == 3 and op.mem.base and i.reg_name(op.mem.base) == 'esp']
    report['allocator_esp_memory_operands'] = esp_mem
    checks['allocator_no_stack_argument'] = not esp_mem
    first = [(i.mnemonic, i.op_str) for i in alloc[:5]]
    checks['allocator_register_abi'] = first == [('push', 'ebx'), ('push', 'esi'), ('push', 'edi'), ('mov', 'edi, edx'),
                                                 ('mov', 'ebx, ecx')]
    fail = [(i.mnemonic, i.op_str) for i in alloc if 0x0b40f0 <= i.address - c.BASE <= 0x0b40f6]
    checks['allocator_failure_exit'] = fail[-5:] == [('xor', 'eax, eax'), ('pop', 'edi'), ('pop', 'esi'), ('pop', 'ebx'),
                                                     ('ret', '')]
    # ---- the claimed span ----
    span = c.span_report(im, v['allocator_rva'], v['continue_rva'])
    report['entry_span'] = span['insns']
    checks['entry_span_whole'] = span['whole'] and [x[1:] for x in span['insns']] == [
        ('push', 'ebx'), ('push', 'esi'), ('push', 'edi'), ('mov', 'edi, edx')]
    checks['entry_span_bytes'] = im.read(v['allocator_rva'], 5) == bytes([0x53, 0x56, 0x57, 0x8b, 0xfa])
    checks['entry_span_no_relocation'] = not span['fixups']
    checks['entry_jump_one_qword'] = span['atomic_jump']
    checks['no_entry_into_span'] = not span['interior_hits']
    # ---- caller sets ----
    alloc_callers = c.direct_callers(im, v['allocator_rva'])
    wrapper_callers = c.direct_callers(im, v['wrapper_rva'])
    report['allocator_callers'] = [hex(x) for x in alloc_callers]
    report['wrapper_call_sites'] = [hex(x) for x in wrapper_callers]
    checks['allocator_one_caller'] = alloc_callers == [v['wrapper_call_rva']] and \
        v['wrapper_return_rva'] == v['wrapper_call_rva'] + 5
    checks['wrapper_caller_set'] = sorted(wrapper_callers) == sorted(a for a, _, _ in callers) and \
        all(b == a + 5 for a, b, _ in callers) and len(callers) == 6
    checks['no_pointer_to_entries'] = not c.pointers_to(im, c.BASE + v['allocator_rva']) and \
        not c.pointers_to(im, c.BASE + v['wrapper_rva'])
    wrap, ok = im.insns(v['wrapper_rva'], v['wrapper_call_rva'] + 5)
    checks['wrapper_frame'] = ok and [(i.mnemonic, i.op_str) for i in wrap] == [
        ('push', 'esi'), ('push', 'edi'), ('mov', 'edi, edx'), ('mov', 'esi, ecx'), ('mov', 'edx, dword ptr [esp + 0x10]'),
        ('mov', 'ecx, dword ptr [esp + 0xc]'), ('call', hex(c.BASE + v['allocator_rva']))]
    kinds = {kind for _, _, kind in callers}
    checks['caller_classes'] = kinds == {'query', 'savegame', 'script', 'speech', 'other', 'selector'}
    # ---- the selector: (id, 0, 0), ECX = EDX = 0, kind 0x5a assigned after creation ----
    sel, ok = im.insns(0x06611d, 0x06612b)
    checks['selector_call_shape'] = ok and [(i.mnemonic, i.op_str) for i in sel] == [
        ('push', '0'), ('push', '0'), ('push', 'esi'), ('xor', 'edx, edx'), ('xor', 'ecx, ecx'),
        ('call', hex(c.BASE + v['wrapper_rva']))]
    kind, ok = im.insns(0x0662f2, 0x066318)
    checks['selector_kind_0x5a'] = ok and any(i.address - c.BASE == 0x066303 and i.op_str == 'dword ptr [eax + 0x30], 0x5a'
                                              for i in kind) and kind[-1].op_str == hex(c.BASE + 0x0b4c50)
    # ---- the video consumer's destination lock pair ----
    lock = [i for i in im.insns(0x0fede4, 0x0fedf3)[0]]
    unlock = [i for i in im.insns(0x0ff54f, 0x0ff559)[0]]
    checks['video_lock_returns_in_range'] = (
        lock[-1].op_str == 'dword ptr [eax + 0x34]' and lock[-1].address + lock[-1].size - c.BASE == 0x0fedf3 and
        unlock[-1].op_str == 'dword ptr [ecx + 0x38]' and unlock[-1].address + unlock[-1].size - c.BASE == 0x0ff559 and
        v['video_begin_rva'] <= 0x0fedf3 < v['video_end_rva'] and v['video_begin_rva'] <= 0x0ff559 < v['video_end_rva'])
    # ---- AP refusal ----
    c.ap_refusal('media', checks, report, {'ap_allocator_entry': (0x00498140, bytes([0x53, 0x8b, 0x5c, 0x24, 0x08]))})
    checks['fl_sites_not_ap'] = v['allocator_rva'] + c.BASE != 0x00498140
    return c.finish(report, checks)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('exe', nargs='?', type=Path, default=c.DEFAULT_EXE)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    report = verify(args.exe)
    text = json.dumps(report, indent=1, sort_keys=True)
    if args.out:
        args.out.write_text(text + '\n')
    print(json.dumps({k: report[k] for k in ('result', 'failed', 'media_window_count', 'media_fixup_count',
                                             'allocator_instructions', 'wrapper_call_sites')}, sort_keys=True))
    return 0 if report['result'] == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
