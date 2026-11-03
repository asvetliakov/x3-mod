#!/usr/bin/env python3
"""Read-only qualification of the FL music keep (src/engine/fl/music_keep_sites_fl.h, music_keep_fl.cpp) on X3FL.exe.

docs/reverse-engineering/fl/music-keep.md, sections 1 to 6 and "Port (2026-11-03)". Checks on the installed exe (default
/mnt/c/X3FL/X3FL.exe, SHA-256 e7f3aad8...; every value an RVA):

- the keep windows of music_windows_fl_inc.h equal the windows regenerated from the exe;
- the seven claimed spans: T1 0x0b4210 `push ebx; mov ebx,[owner]` (7 bytes, the one HIGHLOW the owner slot), A
  0x0b423e `mov edi,[esi+0x24]; cmp dword ptr [edi+4],0` (7 bytes, crosses a qword), T2 0x0b4c50 five pushes, T3
  0x0b47a0 `mov eax,[owner]`, and the call sites C 0x0b4d26 (seek), D 0x0b434a (status), E 0x0b4d42 (Run): whole
  instructions, the declared relocations only, qword geometry, no raw branch into any span's interior;
- register / stack contracts: A three pushes deep (caller return at [esp+0xc]), ESI = record, EBX = next node; the
  keep_running label 0x0b4285 and the skip_all continue 0x0b42c4 read neither EDI nor EFLAGS before writing them; C
  with ECX = record, EDX = start ms; D with ECX = record, EDX = [record+0x28], AX consumed; E with ECX = record; T2's id
  at [esp+4] (ECX slot, EDX context); T3's id at [esp+4] (ECX slot, EDX context); the free wrapper's id at [esp+4]
  and its slot guard `cmp edi,0x20; jge`; the pause and the pump take ECX; the pump holds no x87 instruction and its
  only entry is the status query's tail jump; the status query's no-look "ended" exit on [active] == 0 and
  [[input]] & 0x4000 clear;
- caller sets: stop-all six, play four, MOV_StopMovie two, the free wrapper one, the seek three, the status one, the
  Run two, the pause one, each equal to the header's tables; no relocated pointer to any entry;
- the save's gzwrite / gzclose: X3FL.exe imports both from zlib1.dll at the IAT slots the header names; the gzwrite call
  at 0x106871 and the close wrapper's gzclose call at 0x10695d go through them; the wrapper pushes exactly the handle
  (so [esp+8] at the import is the wrapper caller's return), its callers are the save 0x0791fc and 0x114c6a;
- the KC witnesses: the VM loader's build / CODE size / CODE stores, the JMP handler adding [VM+8], and the MOV
  dispatcher (ESI = task, EDX = [task+8], ECX = slot, the id pushed);
- AP refusal: no FL window admits the AP exe; AP's T1/A/T2/T3 bytes are not at their VAs in FL.

Writes one JSON report (result PASS/FAIL). usage: verify_music_keep_sites_fl.py [X3FL.exe] [--out report.json]
"""
import argparse
import json
import re
import sys
from pathlib import Path

import fl_music_sites_common as c

SITES = c.ROOT / 'src/engine/fl/music_keep_sites_fl.h'


def table(text, name):
    body = re.search(r'constexpr Caller %s\[\] = \{(.*?)\n\};' % name, text, re.S).group(1)
    return [(int(a, 16), int(b, 16)) for a, b in re.findall(r'\{(0x[0-9a-f]+), (0x[0-9a-f]+), "', body)]


def reads_before_write(insns, regs):
    """True when an instruction reads one of `regs` before any writes it (straight-line prefix only)."""
    for i in insns:
        r, w = i.regs_access()
        names_r = {i.reg_name(x) for x in r}
        names_w = {i.reg_name(x) for x in w}
        if names_r & regs:
            return True
        if names_w & regs:
            return False
        if i.mnemonic in ('jmp', 'ret') or i.mnemonic.startswith('j'):
            return False
    return False


def verify(exe=c.DEFAULT_EXE, sha256=c.FL_SHA256):
    im = c.Image(exe)
    checks, report = {}, {'exe': str(exe), 'sha256': im.sha256}
    if sha256:
        checks['exe_sha256'] = im.sha256 == sha256
    text = SITES.read_text()
    v = {k: int(x, 16) for k, x in re.findall(r'\b([a-z_0-9]+_rva) = (0x[0-9a-f]+)', text)}
    c.check_windows(im, 'keep', checks, report)
    B = c.BASE
    # ---- T1 and A inside stop-all ----
    t1 = c.span_report(im, v['stop_all_rva'], v['stop_all_continue_rva'])
    checks['t1_span'] = t1['whole'] and [x[1:] for x in t1['insns']] == [
        ('push', 'ebx'), ('mov', 'ebx, dword ptr [%s]' % hex(B + v['owner_slot_rva']))] and \
        t1['fixups'] == [v['stop_all_rva'] + 3] and t1['atomic_jump'] and not t1['interior_hits']
    a = c.span_report(im, v['a_site_rva'], v['a_site_rva'] + 7, decode_from=v['stop_all_rva'])
    checks['a_span'] = a['whole'] and [x[1:] for x in a['insns'][:2]] == [
        ('mov', 'edi, dword ptr [esi + 0x24]'), ('cmp', 'dword ptr [edi + 4], 0')] and not a['fixups'] and \
        not a['interior_hits']
    report['a_jump_atomic'] = a['atomic_jump']  # False: a plain write inside the install window
    pre, _ = im.insns(v['stop_all_rva'], v['a_site_rva'])
    pushes = sum(1 for i in pre if i.mnemonic == 'push') - sum(1 for i in pre if i.mnemonic == 'pop')
    esp_other = [i for i in pre if i.mnemonic not in ('push', 'pop') and 'esp' in i.op_str]
    whole_stop_all, _ = im.insns(v['stop_all_rva'], 0x0b42d1)
    checks['stop_all_no_simd'] = not [i for i in whole_stop_all if i.mnemonic.startswith('f') or 'mm' in i.op_str]
    checks['a_return_slot_0xc'] = pushes == 3 and not esp_other and v['a_site_rva'] - v['stop_all_rva'] == 0x2e
    loop = [(i.address - B, i.mnemonic, i.op_str) for i in pre if i.address - B >= 0x0b4230]
    checks['a_registers'] = loop[:4] == [(0x0b4230, 'test', 'byte ptr [ebx + 0x2c], 2'), (0x0b4234, 'lea', 'esi, [ebx]'),
                                         (0x0b4236, 'mov', 'ebx, dword ptr [ebx]'), (0x0b4238, 'je', hex(B + v['a_next_rva']))]
    skip, _ = im.insns(v['a_skip_rva'], v['a_next_rva'])
    nxt, _ = im.insns(v['a_next_rva'], 0x0b42d1)
    checks['a_skip_label'] = (skip[0].mnemonic, skip[0].op_str) == ('and', 'dword ptr [esi + 0x2c], 0xfffffffd') and \
        not any('edi' in i.op_str for i in skip)
    checks['a_next_label'] = [(i.mnemonic, i.op_str) for i in nxt] == [
        ('cmp', 'dword ptr [ebx], 0'), ('jne', hex(B + 0x0b4230)), ('pop', 'edi'), ('pop', 'esi'), ('pop', 'ebx'), ('ret', '')]
    # ---- C, D, E call sites ----
    play, ok = im.insns(v['play_rva'], 0x0b4e03)
    checks['play_whole'] = ok
    at = {i.address - B: i for i in play}

    def seq(start, n):
        keys = sorted(k for k in at if k >= start)[:n]
        return [(at[k].mnemonic, at[k].op_str) for k in keys]
    checks['c_site'] = at[0x0b4d18].op_str == 'ecx, esi' and seq(0x0b4d24, 3) == [
        ('mov', 'edx, ebx'), ('call', hex(B + v['seek_rva'])), ('test', 'eax, eax')]
    checks['e_site'] = seq(0x0b4d40, 3) == [('mov', 'ecx, esi'), ('call', hex(B + v['run_rva'])), ('test', 'eax, eax')]
    man, ok = im.insns(0x0b42e0, 0x0b448b)
    mat = {i.address - B: i for i in man}
    mk = sorted(k for k in mat if k >= 0x0b4345)[:5]
    checks['d_site'] = ok and [(mat[k].mnemonic, mat[k].op_str) for k in mk] == [
        ('mov', 'edx, dword ptr [esi + 0x28]'), ('mov', 'ecx, esi'), ('call', hex(B + v['status_rva'])),
        ('movzx', 'eax, ax'), ('cmp', 'ax, 1')]
    for name, site in (('c', v['c_site_rva']), ('d', v['d_site_rva']), ('e', v['e_site_rva'])):
        s = c.span_report(im, site, site + 5, decode_from=0x0b42e0 if name == 'd' else v['play_rva'])
        checks[f'{name}_call_whole'] = s['whole'] and not s['fixups'] and not s['interior_hits']
        report[f'{name}_jump_atomic'] = s['atomic_jump']
    # ---- T2 / T3 entries ----
    t2 = c.span_report(im, v['play_rva'], v['play_rva'] + 5)
    checks['t2_span'] = t2['whole'] and [x[1] for x in t2['insns'][:5]] == ['push'] * 5 and \
        [x[2] for x in t2['insns'][:5]] == ['ecx', 'ebx', 'ebp', 'esi', 'edi'] and not t2['fixups'] and \
        t2['atomic_jump'] and not t2['interior_hits']
    checks['t2_stack'] = seq(0x0b4c55, 3) == [('mov', 'edi, dword ptr [esp + 0x18]'), ('mov', 'ebp, ecx'),
                                              ('mov', 'dword ptr [esp + 0x10], edx')]
    t3 = c.span_report(im, v['stop_movie_rva'], v['stop_movie_rva'] + 5)
    stop, ok = im.insns(v['stop_movie_rva'], 0x0b485d)
    checks['t3_span'] = t3['whole'] and t3['insns'][0][1:] == ('mov', 'eax, dword ptr [%s]' % hex(B + v['owner_slot_rva'])) \
        and t3['fixups'] == [v['stop_movie_rva'] + 1] and t3['atomic_jump'] and not t3['interior_hits']
    checks['t3_stack'] = ok and [(i.mnemonic, i.op_str) for i in stop[1:7]] == [
        ('push', 'ebx'), ('push', 'esi'), ('push', 'edi'), ('mov', 'esi, dword ptr [eax]'), ('mov', 'ebx, edx'),
        ('mov', 'edi, ecx')] and any(i.op_str == 'eax, dword ptr [esp + 0x10]' for i in stop) and \
        stop[-1].mnemonic == 'ret' and not stop[-1].op_str
    free, ok = im.insns(v['free_wrapper_rva'], 0x0b46a4)
    fa = {i.address - B: i for i in free}
    checks['free_contract'] = ok and fa[0x0b4648].op_str == 'edi, ecx' and fa[0x0b464a].op_str == 'ebx, edx' and \
        fa[0x0b4654].op_str == 'esi, dword ptr [esp + 0x10]' and fa[0x0b4658].op_str == 'dword ptr [ecx + 0x24], 0' and \
        fa[0x0b467d].op_str == 'edi, 0x20' and fa[0x0b4680].mnemonic == 'jge' and fa[0x0b4698].op_str == hex(B + 0x0b4490) \
        and fa[0x0b4697].mnemonic == 'ret' and not fa[0x0b4697].op_str and fa[0x0b46a2].op_str == hex(B + 0x0b466d)
    pause, ok = im.insns(v['pause_rva'], 0x0b4203)
    checks['pause_contract'] = ok and (pause[0].mnemonic, pause[0].op_str) == ('and', 'dword ptr [ecx + 0x2c], 0xfffffffd') \
        and pause[2].op_str == 'esi, dword ptr [ecx + 0x24]' and pause[-1].mnemonic == 'ret' and not pause[-1].op_str
    pump, ok = im.insns(v['pump_rva'], 0x0fed0f)
    x87 = [i for i in pump if i.mnemonic.startswith('f') and i.mnemonic not in ('fxsave',)]
    checks['pump_contract'] = ok and pump[4].op_str == 'esi, ecx' and not x87 and \
        all(not i.op_str for i in pump if i.mnemonic == 'ret')
    jumps = [s for s, t in c.raw_branch_hits(im, v['pump_rva'] - 1, v['pump_rva'] + 1) if t == v['pump_rva']]
    checks['pump_only_entry'] = jumps == [0x0ff862] and not c.direct_callers(im, v['pump_rva'])
    status, ok = im.insns(v['status_rva'], 0x0ff5a4)
    checks['status_no_look_exit'] = ok and [(i.mnemonic, i.op_str) for i in status] == [
        ('sub', 'esp, 0x1c'), ('cmp', 'dword ptr [%s], 0' % hex(B + v['active_flag_rva'])), ('push', 'ebx'),
        ('push', 'ebp'), ('push', 'esi'), ('mov', 'esi, dword ptr [ecx + 0x24]'), ('push', 'edi'),
        ('jne', hex(B + 0x0ff5a4)), ('mov', 'eax, dword ptr [%s]' % hex(B + v['input_flags_slot_rva'])),
        ('test', 'dword ptr [eax], 0x4000'), ('je', hex(B + 0x0ff631))]
    ended, _ = im.insns(0x0ff631, 0x0ff63e)
    checks['status_ended_ax2'] = (ended[0].mnemonic, ended[0].op_str) == ('mov', 'eax, 2') and ended[-1].mnemonic == 'ret'
    # ---- caller sets ----
    sets = {'stop_all': (v['stop_all_rva'], table(text, 'stop_callers')),
            'play': (v['play_rva'], table(text, 'play_callers')),
            'stop_movie': (v['stop_movie_rva'], table(text, 'stop_movie_callers')),
            'free_wrapper': (v['free_wrapper_rva'], table(text, 'free_callers'))}
    counts = {}
    for name, (target, rows) in sets.items():
        found = c.direct_callers(im, target)
        counts[name] = len(found)
        checks[f'callers_{name}'] = sorted(found) == sorted(r[0] for r in rows) and all(b == a + 5 for a, b in rows) and \
            not c.pointers_to(im, B + target)
    for name, target, expected in (('seek', v['seek_rva'], [0x0b43c0, 0x0b4d26, 0x0b4f43]),
                                   ('status', v['status_rva'], [0x0b434a]), ('run', v['run_rva'], [0x0b4d42, 0x0b4f5f]),
                                   ('pause', v['pause_rva'], [0x0b47f1])):
        found = c.direct_callers(im, target)
        counts[name] = len(found)
        checks[f'callers_{name}'] = sorted(found) == expected
    report['caller_counts'] = counts
    checks['caller_set_sizes'] = [counts[k] for k in ('stop_all', 'play', 'stop_movie', 'free_wrapper')] == [6, 4, 2, 1]
    # ---- the save's import edges ----
    imports = im.imports().get('zlib1.dll', [])
    slots = {e['name']: int(e['iat_va'], 16) - B for e in imports}
    checks['zlib_import_slots'] = slots.get('gzwrite') == v['gzwrite_iat_rva'] and slots.get('gzclose') == v['gzclose_iat_rva']
    gw, _ = im.insns(0x106871, 0x106877)
    checks['gzwrite_call_through_slot'] = (gw[0].mnemonic, gw[0].op_str) == ('call', 'dword ptr [%s]' % hex(B + v['gzwrite_iat_rva']))
    cw, ok = im.insns(v['close_wrapper_rva'], 0x10697a)
    ca = {i.address - B: i for i in cw}
    checks['gzclose_call_through_slot'] = ok and ca[v['gzclose_call_rva']].op_str == 'dword ptr [%s]' % hex(B + v['gzclose_iat_rva']) \
        and v['gzclose_return_rva'] == v['gzclose_call_rva'] + 6 and ca[0x10694e].mnemonic == 'push' and \
        sum(1 for i in cw if i.mnemonic == 'push' and i.address - B < v['gzclose_call_rva']) == 1
    checks['close_wrapper_callers'] = sorted(c.direct_callers(im, v['close_wrapper_rva'])) == [v['save_close_call_rva'], 0x114c6a] \
        and v['save_close_return_rva'] == v['save_close_call_rva'] + 5
    # ---- KC witnesses and the MOV dispatcher ----
    kb, _ = im.insns(0x0b90af, 0x0b90b5)
    kcs, _ = im.insns(0x0b9122, 0x0b913e)
    kj, _ = im.insns(0x0bfb21, 0x0bfb2b)
    checks['kc_witnesses'] = kb[0].op_str == 'dword ptr [edi + 0x143c], eax' and kcs[0].op_str == 'dword ptr [edi + 0x10], ecx' \
        and kcs[-1].op_str == 'dword ptr [edi + 8], eax' and [(i.mnemonic, i.op_str) for i in kj[:2]] == [
            ('mov', 'esi, dword ptr [esi]'), ('add', 'esi, dword ptr [edx + 8]')]
    disp, _ = im.insns(0x0b58e0, 0x0b5903)
    md, _ = im.insns(0x0b5983, 0x0b5996)
    checks['mov_dispatch'] = disp[4].op_str == 'esi, dword ptr [esp + 0x10]' and disp[6].op_str == 'edx, dword ptr [esi + 8]' \
        and [(i.mnemonic, i.op_str) for i in md[1:4]] == [('mov', 'ecx, dword ptr [esp + 0x10]'),
                                                          ('push', 'dword ptr [eax + 1]'),
                                                          ('call', hex(B + v['stop_movie_rva']))]
    # ---- AP refusal ----
    c.ap_refusal('keep', checks, report, {
        'ap_t1': (0x004982b0, bytes([0xa1, 0x44, 0x6f, 0x60, 0x00])),
        'ap_a': (0x004982db, bytes([0x8b, 0x7e, 0x24, 0x39, 0x5f, 0x04])),
        'ap_t2': (0x00498c90, bytes([0x51, 0x53, 0x8b, 0x5c, 0x24, 0x14])),
        'ap_t3': (0x00498810, bytes([0x8b, 0x0d, 0x44, 0x6f, 0x60, 0x00]))})
    return c.finish(report, checks)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('exe', nargs='?', type=Path, default=c.DEFAULT_EXE)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    report = verify(args.exe)
    if args.out:
        args.out.write_text(json.dumps(report, indent=1, sort_keys=True) + '\n')
    print(json.dumps({k: report[k] for k in ('result', 'failed', 'keep_window_count', 'keep_fixup_count', 'caller_counts')},
                     sort_keys=True))
    return 0 if report['result'] == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
