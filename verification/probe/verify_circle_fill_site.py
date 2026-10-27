#!/usr/bin/env python3
"""Read-only qualification of the oversized-disc claim at 0x004ef620 (src/proxy/circle_fill_sites.h, circle_fill.cpp).

src/proxy/circle_fill.cpp claims the entry of the engine's filled-ellipse rasteriser 0x004ef620 (`sub esp,0x4c;
mov eax,[esp+0x58]`, 7 bytes) through engine_patch; radii above 813 texels run a 64-bit port through the engine's
own lock, class function and span leaves (docs/reverse-engineering/sector-map-scanner-range.md section 5). This checks
X3AP.exe on the host: the structural identity, the 18-byte window, a gap-free whole-instruction decode of the
routine with the span on instruction boundaries and the continuation `push ebx`, the only direct call at 0x0048c076
(EDI = surface set right before it, the caller popping 0x14 bytes and reading EBP afterwards), no decoded branch and
no raw rel8/rel32 encoding landing inside the span, no image dword equal to an address in it, EAX/ECX/EDX/EFLAGS
written before read at the entry, EBX/EBP/ESI saved by the routine, the five patch bytes in one aligned qword, the
patched span decoded as one five-byte jmp, the class dispatch read out of the image (index and jump tables, the five
`mov [esp+0xc],leaf` cases, the class call with ECX = EDI and EAX = 0, the lock call), the class function pure (no
call, no store, EBX/EBP/EDI untouched, ESI pushed and popped), the lock's pitch store at +0x18 of its out argument,
the FNV-1a 64 of every compared region, the 813 threshold (exact recurrence: max |d| fits an int32 at 813 and not at
814), no other DLL claim on the window or a region, and that the header carries the same constants. Also the
`circle_fill` log-row parsers. No Wine, no game launch.
"""
import argparse
import hashlib
import json
import re
import struct
import subprocess
from pathlib import Path

import exe_identity  # structure + anchors gate; hashes are INFO (docs/reverse-engineering/executable-identity.md)

import verify_collide_sites as sites

common = sites.common
ROOT = Path(__file__).resolve().parents[2]
CORE = ROOT / 'src/proxy/circle_fill_sites.h'
DEFAULT_EXE = common.DEFAULT_EXE
SITE_VA, CONTINUE_VA, CALLER_VA, CALLER_RETURN_VA = 0x4ef620, 0x4ef627, 0x48c076, 0x48c07b
ROUTINE = (0x4ef620, 0x4efa3c)  # code; the jump table 0x4efa3c (6 dwords) and class index bytes 0x4efa54 (25) follow
JUMP_TABLE, INDEX_TABLE, INDEX_COUNT, DISPATCH_VA = 0x4efa3c, 0x4efa54, 25, 0x4ef6c5
WINDOW = bytes.fromhex('83ec4c8b442458' '535556' '0fbff0' '8bde' '0fb7c8')
SITE = WINDOW[:7]
WINDOW_STARTS = [0x4ef620, 0x4ef623, 0x4ef627, 0x4ef628, 0x4ef629, 0x4ef62a, 0x4ef62d, 0x4ef62f]
LOCK_VA, CLASS_VA, CLASS_END, LOCK_PITCH_STORE = 0x4dcd30, 0x4ee6e0, 0x4ee84b, 0x4dcdff
LEAVES = {0: 0x4b32f0, 5: 0x4b3680, 10: 0x4b3580, 15: 0x4b3340, 24: 0x4b3620}
UNKNOWN_BLITTYPE = 0x4efa28
REGIONS = [(0x4ef620, 0x44d), (0x4ee6e0, 0x16b), (0x4dcd30, 0xe4), (0x4b32f0, 0x4e), (0x4b3340, 0x231), (0x4b3580, 0x9a),
           (0x4b3620, 0x5b), (0x4b3680, 0x74)]
THRESHOLD = 813
LOG_RE = re.compile(r'\bcircle_fill site=(?P<site>[0-9a-f]{8}) status=(?P<status>patched|patched_unverified|off|refused) reason=(?P<reason>\S+) '
                    r'mode=(?P<mode>on|off|-) setting=(?P<setting>[!-~]+) write=(?P<write>none|atomic|plain) threshold=(?P<threshold>\d+) '
                    r'continue=(?P<continue>[0-9a-f]{8})')
WIDE_RE = re.compile(r'\bcircle_fill_wide r=(?P<r>-?\d+) cx=(?P<cx>-?\d+) cy=(?P<cy>-?\d+) aspect=(?P<aspect>-?\d+) '
                     r'surface=(?P<w>-?\d+)x(?P<h>-?\d+) class=(?P<cls>-?\d+) leaf=(?P<leaf>[0-9a-f]{8})')
RESTORE_RE = re.compile(r'\bcircle_fill_restore site=(?P<site>[0-9a-f]{8}) status=(?P<status>restored|restore_not_owned|restore_failed) '
                        r'registered=(?P<registered>[01])')


def parse_log_line(line):
    """The one `circle_fill site=` install row -> dict, or None."""
    match = LOG_RE.search(line)
    if not match:
        return None
    row = match.groupdict()
    return {'site': int(row['site'], 16), 'status': row['status'], 'reason': row['reason'], 'mode': row['mode'], 'setting': row['setting'],
            'write': row['write'],
            'threshold': int(row['threshold']), 'continue': int(row['continue'], 16), 'patched': row['status'] == 'patched'}


def parse_wide_line(line):
    """The first-call `circle_fill_wide` row -> dict, or None."""
    match = WIDE_RE.search(line)
    if not match:
        return None
    row = {k: int(v) for k, v in match.groupdict().items() if k != 'leaf'}
    row['leaf'] = int(match.group('leaf'), 16)
    return row


def parse_restore_line(line):
    """The `circle_fill_restore` row written by shutdown() on a dynamic unload -> dict, or None."""
    match = RESTORE_RE.search(line)
    return {'site': int(match.group('site'), 16), 'status': match.group('status'), 'registered': match.group('registered') == '1'} if match else None


def fnv1a64(data):
    h = 0xcbf29ce484222325
    for b in data:
        h = ((h ^ b) * 0x100000001b3) & 0xffffffffffffffff
    return h


def max_abs_d(r):
    """The largest |decision value| of the routine's recurrence in exact arithmetic (A = B = r^2, the scripts' aspect)."""
    A = B = r * r
    d = 2 * A * (1 - B) + 2 * B * r * (r - 1) + B
    Y, X, m = r, 0, abs(d)
    while True:
        if d >= 0:
            Y -= 1
            d -= Y * 4 * B
            m = max(m, abs(d))
        d += (2 * X + 3) * 2 * A
        X += 1
        m = max(m, abs(d))
        if not Y * B > X * A:
            break
    d = ((Y - 2) * Y + 1) * 2 * B + (X + 1) * X * 2 * A + (1 - 2 * B) * A
    m = max(m, abs(d))
    while True:
        if d <= 0:
            X += 1
            d += X * 4 * A
            m = max(m, abs(d))
        Y -= 1
        d += (3 - 2 * Y) * 2 * B
        m = max(m, abs(d))
        if Y + 1 == 0:
            return m


def source_constants(text):
    """VAs, lengths, the window, the region table, the class map and the claim spec from circle_fill_sites.h."""
    text = re.sub(r'//[^\n]*', '', text)

    def value(name):
        match = re.search(rf'\b{name}\s*=\s*(0x[0-9a-fA-F]+|\d+)\s*[;,]', text)
        return int(match.group(1), 0) if match else None
    window = re.search(r'\bexpected_window\[\w+\]\s*=\s*\{([^}]*)\}', text)
    spec = re.search(r'claim_spec\s*=\s*\{\s*"([a-z_0-9]+)"\s*,\s*(0x[0-9a-fA-F]+)\s*,\s*\{([^}]*)\}\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}', text)
    regions = re.search(r'\bregions\[region_count\]\s*=\s*\{(.*?)\};', text, re.S)
    leaf_names = {name: value(name) for name in ('leaf_class0_va', 'leaf_class5_va', 'leaf_class10_va', 'leaf_class15_va', 'leaf_class24_va')}
    cases = {int(c): leaf_names.get(n) for c, n in re.findall(r'case (\d+): return (\w+);', text)}
    return {name: value(name) for name in ('site_va', 'continue_va', 'caller_va', 'caller_return_va', 'site_length', 'window_length',
                                           'stock_radius_max', 'lock_va', 'class_va', 'lock_out_bytes', 'region_count', 'object_bits',
                                           'object_width', 'object_height', 'object_flags', 'object_backing', 'flag_no_draw', 'flag_locked',
                                           'frame_edi', 'frame_eax', 'frame_return', 'frame_args', 'arg_cx', 'arg_cy', 'arg_r',
                                           'arg_colour', 'arg_aspect')} | {
        'window': bytes(int(b, 0) for b in re.findall(r'0x[0-9a-fA-F]{2}\b', window.group(1))) if window else b'',
        'regions': [(int(a, 16), int(n, 16), int(h, 16)) for a, n, h in
                    re.findall(r'\{\s*(0x[0-9a-fA-F]+)\s*,\s*(0x[0-9a-fA-F]+)\s*,\s*(0x[0-9a-fA-F]+)ull\s*\}', regions.group(1))] if regions else [],
        'cases': cases,
        'claim': (spec.group(1), int(spec.group(2), 16), bytes(int(b, 0) for b in re.findall(r'0x[0-9a-fA-F]{2}', spec.group(3))),
                  int(spec.group(4)), int(spec.group(5)), int(spec.group(6))) if spec else None}


def expected_constants(region_hashes):
    return {'site_va': SITE_VA, 'continue_va': CONTINUE_VA, 'caller_va': CALLER_VA, 'caller_return_va': CALLER_RETURN_VA,
            'site_length': len(SITE), 'window_length': len(WINDOW), 'stock_radius_max': THRESHOLD, 'lock_va': LOCK_VA, 'class_va': CLASS_VA,
            'lock_out_bytes': 0x1c, 'region_count': len(REGIONS), 'object_bits': 0, 'object_width': 4, 'object_height': 6,
            'object_flags': 0x10, 'object_backing': 0x30, 'flag_no_draw': 0x200000, 'flag_locked': 2, 'frame_edi': 0, 'frame_eax': 7,
            'frame_return': 9, 'frame_args': 10, 'arg_cx': 0, 'arg_cy': 1, 'arg_r': 2, 'arg_colour': 3, 'arg_aspect': 4, 'window': WINDOW,
            'regions': [(va, n, h) for (va, n), h in zip(REGIONS, region_hashes)], 'cases': dict(LEAVES),
            'claim': ('circle_fill_wide', SITE_VA, SITE, 7, 0, 0)}


def raw_branch_sources(data, lo, hi):
    """(source VA, target VA) of every rel8/rel32 branch or call encoding in .text landing in [lo, hi) (a superset of real branches)."""
    image = common.Image(data)
    hits = []
    for name, virtual_size, virtual_address, raw_size, raw_pointer, _ in exe_identity.section_table(data):
        if name != '.text':
            continue
        text = data[raw_pointer:raw_pointer + min(virtual_size, raw_size)]
        base = image.image_base + virtual_address
        for i in range(len(text) - 6):
            op = text[i]
            if op in (0xe8, 0xe9):
                target = base + i + 5 + struct.unpack_from('<i', text, i + 1)[0]
            elif op == 0x0f and 0x80 <= text[i + 1] <= 0x8f:
                target = base + i + 6 + struct.unpack_from('<i', text, i + 2)[0]
            elif 0x70 <= op <= 0x7f or op == 0xeb or 0xe0 <= op <= 0xe3:
                target = base + i + 2 + struct.unpack_from('<b', text, i + 1)[0]
            else:
                continue
            if lo <= target < hi:
                hits.append((base + i, target))
    return hits


def patched_image(data, dispatcher=0x10000000):
    """The image with the claim's jump at 0x004ef620 (e9 rel32 to `dispatcher`)."""
    image = common.Image(data)
    out = bytearray(data)
    for name, virtual_size, virtual_address, raw_size, raw_pointer, _ in exe_identity.section_table(data):
        start = image.image_base + virtual_address
        if start <= SITE_VA < start + raw_size:
            offset = raw_pointer + SITE_VA - start
            out[offset:offset + 5] = b'\xe9' + struct.pack('<i', dispatcher - (SITE_VA + 5))
            return bytes(out)
    raise ValueError('site outside the image')


def ops(instruction):
    return instruction.operands.replace(' ', '')


def class_map(image, by_va):
    """Class -> leaf VA (0 = Unknown Blittype) read out of the dispatch: `cmp eax,0x18; ja`, index bytes, jump table, cases."""
    index = image.read(INDEX_TABLE, INDEX_COUNT) or b''
    table = struct.unpack('<6I', image.read(JUMP_TABLE, 24) or b'\0' * 24)
    out = {}
    for cls, slot in enumerate(index):
        target = table[slot] if slot < len(table) else None
        case = by_va.get(target)
        if target == UNKNOWN_BLITTYPE and case is not None and case.mnemonic == 'push':
            out[cls] = 0
        elif case is not None and case.mnemonic == 'mov' and ops(case).startswith('DWORDPTR[esp+0xc],0x'):
            out[cls] = int(ops(case).split(',')[1], 16)
        else:
            out[cls] = None
    return out


def inspect(data, routine, caller, class_fn, patched_span, core_text, claims):
    image = common.Image(data)
    by_va = {i.va: i for i in routine}
    site_end = SITE_VA + len(SITE)
    calls = sorted((s, t) for s, t in raw_branch_sources(data, SITE_VA, SITE_VA + 1) if data and image.read(s, 1) == b'\xe8')
    into = raw_branch_sources(data, SITE_VA + 1, site_end)
    decoded_into = [(i.va, t) for i in routine for t in [common._is_direct_control(i)] if t is not None and SITE_VA < t < site_end]
    dword_refs = sum(data.count(struct.pack('<I', va)) for va in range(SITE_VA, site_end))
    region_hashes = [fnv1a64(image.read(va, n) or b'') for va, n in REGIONS]
    entry = [i for i in routine if i.va < 0x4ef651]  # straight-line entry block up to the first branch (jge at 0x4ef651)
    first = {}
    for i in entry:
        for reg in ('eax', 'ecx', 'edx'):
            if reg in first:
                continue
            o = ops(i)
            if o.startswith(reg + ',') and i.mnemonic in ('mov', 'movzx', 'movsx', 'lea'):
                first[reg] = 'write' if reg not in o.split(',', 1)[1] else 'read'
            elif reg in o:
                first[reg] = 'read'
    rets = [i for i in routine if i.mnemonic == 'ret']
    caller_by = {i.va: i for i in caller}
    cmap = class_map(image, by_va)
    class_writes = [i.va for i in class_fn if i.mnemonic not in ('push', 'pop', 'test', 'cmp', 'ret', 'jmp') and not i.mnemonic.startswith('j')
                    and (re.match(r'^(e?b[xp]|edi|di|bl|bh|bpl)\b', ops(i)) or ops(i).split(',')[0].endswith(']'))]
    class_calls = [i.va for i in class_fn if i.mnemonic == 'call']
    class_pushes = sorted({ops(i) for i in class_fn if i.mnemonic in ('push', 'pop')})
    overlapping = [(name, hex(address)) for name, address, length in claims
                   for va, n in [(SITE_VA, len(WINDOW))] + REGIONS + [(JUMP_TABLE, 24 + INDEX_COUNT)] if address < va + n and va < address + length]
    lock_store = image.read(LOCK_PITCH_STORE, 3)
    checks = {
        'exe_identity': exe_identity.identity_ok(data),
        'preferred_base': image.image_base == common.IMAGE_BASE,
        'window_bytes': image.read(SITE_VA, len(WINDOW)) == WINDOW,
        'window_whole_instructions': [i.va for i in routine if SITE_VA <= i.va < SITE_VA + len(WINDOW)] == WINDOW_STARTS,
        'span_two_instructions': ([(i.mnemonic, ops(i)) for i in routine[:2]] == [('sub', 'esp,0x4c'), ('mov', 'eax,DWORDPTR[esp+0x58]')]
                                  and routine[2].va == CONTINUE_VA and (routine[2].mnemonic, ops(routine[2])) == ('push', 'ebx')),
        'radius_read_movsx': (by_va.get(0x4ef62a) is not None and (by_va[0x4ef62a].mnemonic, ops(by_va[0x4ef62a])) == ('movsx', 'esi,ax')),
        'routine_ends_in_ret': routine[-1].mnemonic == 'ret' and routine[-1].end == ROUTINE[1] and len(rets) == 2
                               and all(ops(i) == '' for i in rets),
        'callee_saved_by_routine': [(i.mnemonic, ops(i)) for i in routine[2:5]] == [('push', 'ebx'), ('push', 'ebp'), ('push', 'esi')]
                                   and all([(j.mnemonic, ops(j)) for j in routine[k - 4:k]] == [('pop', 'esi'), ('pop', 'ebp'), ('pop', 'ebx'), ('add', 'esp,0x4c')]
                                           for k in [routine.index(r) for r in rets]),
        'entry_registers_dead': first.get('eax') == 'write' and first.get('ecx') == 'write' and first.get('edx') == 'write'
                                and routine[0].mnemonic == 'sub' and not any(common._is_direct_control(i) for i in entry),
        'single_caller': [s for s, _ in calls] == [CALLER_VA],
        'caller_sets_edi_cleans_and_reads_ebp': (caller_by.get(0x48c074) is not None and ops(caller_by[0x48c074]) == 'edi,ecx'
                                                 and caller_by.get(CALLER_VA) is not None and common._is_direct_control(caller_by[CALLER_VA]) == SITE_VA
                                                 and caller_by.get(CALLER_RETURN_VA) is not None
                                                 and (caller_by[CALLER_RETURN_VA].mnemonic, ops(caller_by[CALLER_RETURN_VA])) == ('add', 'esp,0x14')
                                                 and caller_by.get(0x48c080) is not None and ops(caller_by[0x48c080]) == 'ecx,ebp'),
        'no_branch_into_span': into == [] and decoded_into == [],
        'no_dword_into_span': dword_refs == 0,
        'atomic_word': (SITE_VA & 7) + 5 <= 8,
        'patched_decode': (len(patched_span) == 1 and patched_span[0].mnemonic == 'jmp' and len(patched_span[0].raw) == 5
                           and common._is_direct_control(patched_span[0]) == 0x10000000),
        'class_dispatch': (by_va.get(0x4ef6bc) is not None and ops(by_va[0x4ef6bc]) == 'eax,0x18' and by_va.get(DISPATCH_VA) is not None
                           and by_va.get(0x4ef6cc) is not None and by_va[0x4ef6cc].mnemonic == 'jmp'
                           and {c: v for c, v in cmap.items() if v} == LEAVES and all(v == 0 for c, v in cmap.items() if c not in LEAVES)),
        'class_call_ecx_edi_eax_0': ([(by_va[a].mnemonic, ops(by_va[a])) for a in (0x4ef6b0, 0x4ef6b2) if a in by_va] == [('mov', 'ecx,edi'), ('xor', 'eax,eax')]
                                     and by_va.get(0x4ef6b4) is not None and common._is_direct_control(by_va[0x4ef6b4]) == CLASS_VA
                                     and by_va.get(0x4ef6b9) is not None and (by_va[0x4ef6b9].mnemonic, ops(by_va[0x4ef6b9])) == ('movsx', 'eax,ax')),
        'lock_call': (by_va.get(0x4ef69a) is not None and common._is_direct_control(by_va[0x4ef69a]) == LOCK_VA
                      and [ops(by_va[a]) for a in (0x4ef693, 0x4ef698, 0x4ef699) if a in by_va] == ['ebp', 'edx', 'eax']
                      and by_va.get(0x4ef6ac) is not None and ops(by_va[0x4ef6ac]) == 'DWORDPTR[edi+0x10],0x2'),
        'class_function_pure': class_fn[-1].end == CLASS_END and class_calls == [] and class_writes == [] and class_pushes == ['esi'],
        'lock_pitch_store_at_0x18': lock_store == bytes.fromhex('895118'),
        'regions_hash': source_constants(core_text)['regions'] == [(va, n, h) for (va, n), h in zip(REGIONS, region_hashes)],
        'threshold_813': max_abs_d(THRESHOLD) < 2 ** 31 <= max_abs_d(THRESHOLD + 1),
        'no_other_claim': overlapping == [],
        'source_constants': source_constants(core_text) == expected_constants(region_hashes),
    }
    return {'result': 'PASS' if all(checks.values()) else 'FAIL', 'checks': checks, 'exe_info': exe_identity.info(data), 'site': hex(SITE_VA),
            'site_bytes': (image.read(SITE_VA, len(SITE)) or b'').hex(), 'site_qword': hex(SITE_VA & ~7),
            'callers': [hex(s) for s, _ in calls], 'raw_into_span': [(hex(s), hex(t)) for s, t in into], 'dword_refs': dword_refs,
            'class_map': {str(c): hex(v) if v else v for c, v in cmap.items()}, 'region_fnv1a64': [f'{h:#018x}' for h in region_hashes],
            'max_abs_d': {str(THRESHOLD): max_abs_d(THRESHOLD), str(THRESHOLD + 1): max_abs_d(THRESHOLD + 1)},
            'routine_instructions': len(routine), 'other_claims': len(claims), 'overlapping_claims': overlapping,
            'exe_sha256': hashlib.sha256(data).hexdigest()}


def verify(exe=DEFAULT_EXE, core=CORE):
    try:
        data = common.image_bytes(exe)
        routine = common.parse_objdump(common.objdump_window(exe, *ROUTINE, timeout=120), *ROUTINE)
        caller = common.parse_objdump(common.objdump_window(exe, 0x48c067, 0x48c08b), 0x48c067, 0x48c08b)
        class_fn = common.parse_objdump(common.objdump_window(exe, CLASS_VA, CLASS_END), CLASS_VA, CLASS_END)
        patched = patched_image(data)
        patched_span = common.parse_objdump(common.objdump_window(patched, SITE_VA, SITE_VA + 5), SITE_VA, SITE_VA + 5)
        claims = sites.other_claims(ROOT, skip_prefix='circle_fill', own_names=())
        return inspect(data, routine, caller, class_fn, patched_span, Path(core).read_text(), claims)
    except (ValueError, OSError, subprocess.SubprocessError, RuntimeError, KeyError, IndexError, AttributeError) as error:
        return {'result': 'FAIL', 'checks': {'decode': False}, 'error': str(error)}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=DEFAULT_EXE)
    parser.add_argument('--core', type=Path, default=CORE)
    args = parser.parse_args()
    report = verify(args.exe, args.core)
    print(json.dumps(report, indent=2, sort_keys=True))
    raise SystemExit(report['result'] != 'PASS')
