#!/usr/bin/env python3
"""Pins every constant of src/proxy/seta_restore_core.h (X3M_SETA_RESTORE, docs/architecture/seta-restore.md) against the
installed X3AP.exe, read-only: the byte windows the installer compares, the 8-byte hook span at the TI_SetTimeWarpFactor
case entry 0x004b148c (whole decoded instructions, no relative control, entered only through jump-table entry 9, no
branch or dword reference into its interior), the restart call's wrapper 0x0049f4c0 (cdecl, the EXE's own class-150 call
shape), the shared injection site, the anchored engine globals and the story constants of seta_toggle_study.txt.
Ends with RESULT PASS only if every check holds.

    python3 -I verification/results/seta-script-toggle/verify_seta_restore_sites.py [X3AP.exe]
"""
import hashlib
import re
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / 'verification/probe'))
from verify_chase_aim_sites import objdump_window, parse_objdump  # noqa: E402

EXE = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('/mnt/c/X3/X3AP.exe')
SHA256 = 'fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab'
HEADER = ROOT / 'src/proxy/seta_restore_core.h'
FAIL = []


def check(ok, what):
    print('   [%s] %s' % ('ok' if ok else 'FAIL', what))
    if not ok:
        FAIL.append(what)


# ---------------------------------------------------------------- the header's constants
H = HEADER.read_text()


def const(name):
    """A constexpr integer of the header: a literal or an expression over other constants (`hook_next_va`,
    `ti_jump_table_va + 4 * ti_case_index`)."""
    m = re.search(r'\b%s = ([^,;{}]+)[,;]' % re.escape(name), H)
    if not m:
        raise SystemExit('constant %s not found in %s' % (name, HEADER))
    expr = re.sub(r'\b(0x[0-9a-fA-F]+|\d+)u\b', r'\1', m.group(1).split('//')[0].strip())
    names = {n: const(n) for n in re.findall(r'\b[a-z_][a-z_0-9]*\b', expr)}
    return int(eval(expr, {'__builtins__': {}}, names))


def array(name):
    m = re.search(r'\b%s\[[a-z_0-9 ]*\] = \{([^}]*)\}' % re.escape(name), H)
    if not m:
        raise SystemExit('array %s not found in %s' % (name, HEADER))
    return bytes(int(v, 0) for v in re.findall(r'0x[0-9a-f]+', m.group(1)))


# ---------------------------------------------------------------- the image
data = EXE.read_bytes()
pe = struct.unpack_from('<I', data, 0x3c)[0]
nsec, opt = struct.unpack_from('<H', data, pe + 6)[0], struct.unpack_from('<H', data, pe + 20)[0]
BASE = struct.unpack_from('<I', data, pe + 24 + 28)[0]
SECTIONS = []
for i in range(nsec):
    o = pe + 24 + opt + 40 * i
    vs, va, rs, ra = struct.unpack_from('<IIII', data, o + 8)
    SECTIONS.append((data[o:o + 8].rstrip(b'\0').decode(), BASE + va, vs, ra, rs))


def rd(va, n):
    for _, sva, vs, ra, rs in SECTIONS:
        if sva <= va and va + n <= sva + min(max(vs, rs), rs):
            return data[ra + va - sva:ra + va - sva + n]
    return None


print('X3AP.exe %s  %d B' % (EXE, len(data)))
check(hashlib.sha256(data).hexdigest() == SHA256, 'executable SHA-256 %s... (the image the windows were taken from)' % SHA256[:16])

print('Byte windows the installer compares (seta_restore.cpp windows_match)')
WINDOWS = [('hook_va', 'hook_bytes', 'hook_length', 'TI case 9 entry: mov eax,[0x00606f34]; mov edx,[ebx+1]'),
           ('case_rest_va', 'case_rest', 'case_rest_length', 'rest of case 9 through ret 0x10'),
           ('ti_dispatch_va', 'dispatch_head', 'dispatch_length', 'dispatcher head (EBX = args, EBP = task, jmp [eax*4+table])'),
           ('ti_thunk_va', 'thunk_body', 'thunk_length', 'native thunk (argument order)'),
           ('ti_table_entry_va', 'table_entry', None, 'jump-table entry 9'),
           ('native_args_va', 'native_args', 'native_args_length', 'interpreter native call: two u16 operands'),
           ('native_ip_va', 'native_ip', 'native_ip_length', 'interpreter native call: [task+0x18], [task+0x1c] = IP'),
           ('return_scan_va', 'return_scan', 'return_scan_length', 'return opcode scan (tag 10 above tag 3)'),
           ('script_call_va', 'wrapper_body', 'wrapper_length', 'by-key wrapper 0x0049f4c0 (whole function)'),
           ('stop_call_va', 'stop_call', 'stop_call_length', "the EXE's own class-150 StopFastForward call")]
for va_name, bytes_name, length_name, what in WINDOWS:
    va, expected = const(va_name), array(bytes_name)
    if length_name:
        check(len(expected) == const(length_name), '%s has %s = %d bytes' % (bytes_name, length_name, len(expected)))
    check(rd(va, len(expected)) == expected, '%08x %s (%d B)' % (va, what, len(expected)))

hook, hook_len, hook_next = const('hook_va'), const('hook_length'), const('hook_next_va')
table, index, count = const('ti_jump_table_va'), const('ti_case_index'), const('ti_case_count')
print('Hook span: whole instructions, position independent, entered only through the table')
case_end = const('case_rest_va') + const('case_rest_length')
case = parse_objdump(objdump_window(str(EXE), hook, case_end), hook, case_end)
span = [i for i in case if hook <= i.va < hook + hook_len]
check(hook_next == hook + hook_len and span and span[-1].end == hook_next and len(span) == 2,
      'span %08x..%08x = %s' % (hook, hook_next, '; '.join('%s %s' % (i.mnemonic, i.operands) for i in span)))
check(all(not (i.mnemonic.startswith('j') or i.mnemonic == 'call' or 'rip' in i.operands) for i in span),
      'no relative control or IP-relative operand in the span (plain tail copy)')
check(case[-1].mnemonic == 'ret' and case[-1].operands.strip() in ('0x10', '0x0010'), 'case 9 ends in ret 0x10 at %08x' % case[-1].va)
writes_eax = [i for i in span if i.operands.startswith('eax,')]
check(len(writes_eax) == 1 and any(i.operands.startswith('edx,') for i in span) and
      any(i.operands.startswith('ecx,') for i in case[2:3]),
      'EAX, EDX and ECX are written before any read (dead at the site)')
check(not any(i.mnemonic.startswith(('j', 'set', 'cmov', 'adc', 'sbb', 'pushf')) for i in case),
      'no flag consumer in case 9 before its ret (EFLAGS dead)')
disp_end = const('ti_dispatch_va') + const('dispatch_length')
disp = parse_objdump(objdump_window(str(EXE), const('ti_dispatch_va'), disp_end), const('ti_dispatch_va'), disp_end)
check(disp[0].mnemonic == 'cmp' and disp[0].operands.replace(' ', '') == 'eax,0xf' and count == 16,
      'dispatcher bounds the case at 15 (%d table entries)' % count)
check(disp[-1].mnemonic == 'jmp' and ('0x%x' % table) in disp[-1].operands and '*4' in disp[-1].operands,
      'dispatcher: %s %s' % (disp[-1].mnemonic, disp[-1].operands))
entries = struct.unpack('<%dI' % count, rd(table, 4 * count))
check(entries[index] == hook and entries.count(hook) == 1,
      'table 0x%08x[%d] = 0x%08x, the only entry naming the hook' % (table, index, entries[index]))
text = [s for s in SECTIONS if s[0] == '.text'][0]
_, tva, tvs, tra, trs = text
T = data[tra:tra + trs]
lo, hi = hook + 1, hook + hook_len
cands = []
for i in range(len(T) - 6):
    va, b0 = tva + i, T[i]
    if b0 in (0xe8, 0xe9):
        tgt = va + 5 + struct.unpack_from('<i', T, i + 1)[0]
    elif b0 == 0x0f and 0x80 <= T[i + 1] <= 0x8f:
        tgt = va + 6 + struct.unpack_from('<i', T, i + 2)[0]
    elif b0 == 0xeb or 0x70 <= b0 <= 0x7f or b0 in (0xe0, 0xe1, 0xe2, 0xe3):
        tgt = va + 2 + struct.unpack_from('<b', T, i + 1)[0]
    else:
        continue
    if lo <= tgt < hi:
        cands.append(va)
check(not cands, 'no byte offset of .text decodes as a branch into %08x..%08x (conservative scan; candidates %s)'
      % (lo, hi - 1, ['%08x' % c for c in cands[:8]]))
refs = []
for name, sva, vs, ra, rs in SECTIONS:
    blob = data[ra:ra + rs]
    refs += ['%s:%08x' % (name, sva + i) for i in range(len(blob) - 3) if lo <= struct.unpack_from('<I', blob, i)[0] < hi]
check(not refs, 'no dword in any section points into the span interior (%s)' % refs[:8])

print('The restart call: 0x0049f4c0(0, 0, 0x96, "StartFastForward", 1, 0)')
wrap, wrap_end = const('script_call_va'), const('script_call_va') + const('wrapper_length')
w = parse_objdump(objdump_window(str(EXE), wrap, wrap_end), wrap, wrap_end)
calls = [i.operands for i in w if i.mnemonic == 'call']
check(calls == ['0x49f330', '0x49f430'], 'the wrapper resolves through 0x0049f330 and runs through 0x0049f430 (%s)' % calls)
rets = [i for i in w if i.mnemonic == 'ret']
check(len(rets) == 2 and all(not i.operands.strip() for i in rets), 'both exits are a plain ret: cdecl, the caller pops')
check(any(i.mnemonic == 'xor' and i.operands.replace(' ', '') == 'al,al' for i in w), 'AL = 0 on the unresolved exit')
stop, stop_end = const('stop_call_va'), const('stop_call_va') + const('stop_call_length')
s = parse_objdump(objdump_window(str(EXE), stop, stop_end), stop, stop_end)
check([i.mnemonic for i in s] == ['push'] * 6 + ['call', 'add'] and s[-2].operands == '0x%x' % wrap and
      s[-1].operands.replace(' ', '') == 'esp,0x18' and s[3].operands == '0x%x' % const('call_key'),
      "the EXE's own call: six pushes (key 0x96), call 0x%08x, add esp,0x18" % wrap)
name = rd(0x00555e1c, 16)
check(name == b'StopFastForward\0', 'its name operand 0x00555e1c = "StopFastForward"')
check(data.count(b'\0StartFastForward\0') == 0 and re.search(r'call_method\[\] = "StartFastForward"', H) is not None,
      'the EXE has no "StartFastForward" literal; the header supplies it')
# The chase restore's wrapper 0x0049f570: the same argument marshalling, plus a post-run task lookup.
alt = rd(0x0049f570, 0x5d)
check(alt is not None and alt[:0x38] == rd(wrap, 0x38)[:0x38].replace(b'\x33\xdb', b'\x33\xed').replace(b'\x89\x5c\x24\x30\x89\x5c\x24\x34', b'\x89\x6c\x24\x30\x89\x6c\x24\x34'),
      '0x0049f570 marshals the same six-dword prefix (register choice aside)')

post = parse_objdump(objdump_window(str(EXE), 0x0049f61a, 0x0049f676), 0x0049f61a, 0x0049f676)
post_calls = [i.operands for i in post if i.mnemonic == 'call']
check(post_calls == ['0x49f430', '0x4a21a0'] and post[0].operands.replace(' ', '') == 'edi,DWORDPTR[esi+0x8]',
      '0x0049f570 alone adds a post-run lookup of the task id and calls 0x004a21a0 (%s)' % post_calls)
scan = parse_objdump(objdump_window(str(EXE), 0x004a21a0, 0x004a21d7), 0x004a21a0, 0x004a21d7)
writes = [i for i in scan if i.mnemonic not in ('push', 'pop', 'ret', 'cmp', 'test') and not i.mnemonic.startswith('j')
          and ('PTR [' in i.operands.split(',')[0])]
check(not writes and scan[-1].mnemonic == 'ret', '0x004a21a0 writes no memory (a discarded stack scan)')
callers = {}
for target in (wrap, 0x0049f570):
    callers[target] = [tva + i for i in range(len(T) - 5)
                       if T[i] == 0xe8 and tva + i + 5 + struct.unpack_from('<i', T, i + 1)[0] == target]
print('   call sites (E8 rel32 in .text): 0x0049f4c0 %d, 0x0049f570 %d' % (len(callers[wrap]), len(callers[0x0049f570])))
check({0x0041212e, 0x0040f9b2, 0x00410f84} <= set(callers[wrap]),
      '0x0049f4c0 is the class-150 wrapper of StopFastForward (0x0041212e), InputAction (0x0040f9b2), SetAutopilot (0x00410f84)')

print('Autopilot flag read at the arm (SA_SetAutopilot, SA_GetFlags)')
flags, bit = const('ship_flags'), const('autopilot_bit')
check(rd(const('set_autopilot_on_va'), 7) == bytes([0x81, 0x48, flags]) + struct.pack('<I', bit),
      '%08x SA_SetAutopilot on: or [eax+0x%x],0x%x' % (const('set_autopilot_on_va'), flags, bit))
check(rd(const('set_autopilot_off_va'), 7) == bytes([0x81, 0x60, flags]) + struct.pack('<I', ~bit & 0xffffffff),
      '%08x SA_SetAutopilot off: and [eax+0x%x],~0x%x' % (const('set_autopilot_off_va'), flags, bit))
check(rd(const('get_flags_va'), 3) == bytes([0x8b, 0x40, flags]),
      '%08x SA_GetFlags: mov eax,[eax+0x%x] (the flag 2004::CanWarp tests)' % (const('get_flags_va'), flags))

print('Ship-script id the re-bind compares (the engine reads it as the ship script key)')
script = const('ship_script')
check(rd(const('event_script_read_va'), 6) == bytes([0x8b, 0x80]) + struct.pack('<I', script),
      '%08x SA_GetEventObject: mov eax,[eax+0x%x]' % (const('event_script_read_va'), script))
check(rd(const('can_land_script_read_va'), 6) == bytes([0x8b, 0x8e]) + struct.pack('<I', script),
      '%08x CanLand call: mov ecx,[esi+0x%x] (the script key passed to 0x0049f4c0)' % (const('can_land_script_read_va'), script))

print('Shared injection site and engine globals')
inject = const('inject_site_va')
check(rd(inject, 6) == bytes.fromhex('558bec83e4f0'), '%08x input routine entry 55 8b ec 83 e4 f0' % inject)
chase = (ROOT / 'src/proxy/chase_transition.cpp').read_text()
check('{"chase_restore_inject",0x%08x,{0x55,0x8b,0xec,0x83,0xe4,0xf0},6,0,0}' % inject in chase,
      'the chase restore claims that site (restore_specs), the consumer attaches to it')
anchors = (ROOT / 'src/proxy/executable_identity.h').read_text()
for g in ('cfg_ptr_va', 'vm_ptr_va', 'manager_ptr_va', 'game_root_va'):
    v = const(g)
    m = re.search(r'\{(0x[0-9a-f]{8}), 0x%08x, (\d), \{([^}]*)\}' % v, anchors)
    ok = False
    if m:
        at, n = int(m.group(1), 16), int(m.group(2))
        prefix = bytes(int(x, 0) for x in m.group(3).split(','))[:n]
        ok = rd(at, n + 4) == prefix + struct.pack('<I', v)
    check(ok, '%s 0x%08x anchored in executable_identity.h (%s)' % (g, v, m.group(1) if m else 'missing'))

print('Story constants (seta_toggle_study.txt)')
study = (HERE / 'seta_toggle_study.txt').read_text()
check(re.search(r'build %d\b' % const('story_build'), study.split('\n', 3)[1] + study.split('\n', 3)[0]) is not None,
      'installed story build %d (addon/07.cat)' % const('story_build'))
check(re.search(r'15588 ret %x\s+native\s+TI_SetTimeWarpFactor\s+in 150::StopFastForward' % const('stop_ip'), study) is not None,
      'StopFastForward TI_SetTimeWarpFactor at 15588, IP %x' % const('stop_ip'))
check(re.search(r'15ef3 ret %x\s+call\s+StopFastForward\s+in 150::StartGateWarp' % const('gate_return'), study) is not None,
      'StartGateWarp StopFastForward at 15ef3, return %x' % const('gate_return'))
check('RESULT PASS' in study, 'the study itself passed')

print('\nRESULT %s (%d failures)' % ('PASS' if not FAIL else 'FAIL', len(FAIL)))
for f in FAIL:
    print('  FAIL: ' + f)
sys.exit(1 if FAIL else 0)
