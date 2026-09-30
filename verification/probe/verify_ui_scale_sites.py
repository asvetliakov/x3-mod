#!/usr/bin/env python3
"""Read-only qualification of the UI-scale sites (src/proxy/ui_scale_sites.h, src/proxy/ui_scale.cpp).

src/proxy/ui_scale.cpp claims eight engine sites through engine_patch, all or none (docs/architecture/ui-scale.md,
docs/reverse-engineering/gui-scale.md section 5 strategy (b)): the pixel orthographic projection's 2D exit
`mov eax,1` at 0x004be246, the script's screen-size readers (the `mov edx/ecx,[0x006085e4]` after the movsx in cases
0x71/0x72 of 0x00493b40), the four script mouse-delta loads (0x00403d36, 0x00403d7a, 0x00411b40, 0x00411b57) and the
cursor store `mov [0x00607cf0],eax` at 0x004074ec, the two INS click-point loads (case 0x64 at 0x0042ece0, case 0x28
at 0x0042ddf1, gui-scale.md section 6), plus the debug-only entry counter at 0x004bdee0. This checks X3AP.exe
on the host: the structural identity, every expected byte window, a gap-free whole-instruction decode of the whole
.text (objdump), each claimed span on instruction boundaries, no decoded direct branch and no jump-table entry landing
inside a span, every raw rel8/rel32 encoding landing inside a span interior to a decoded instruction, no dword in the
image pointing into a span, the mnemonics that make the return value / EFLAGS / scratch registers dead after each
site, the register writes between the P load and the 2D exit (EAX/EBX/EBP untouched), the callee prefixes (cmp before
any flag read), the SSE census of .text (no XMM use outside the CRT's one movups), the other claims (disjoint, the
chase-fire claim exactly in the cursor window's gap), the source constants against this table, the auto-scale
mapping and the fixed-point forms. No Wine, no game launch.
"""
import argparse
import json
import re
import struct
from pathlib import Path

import exe_identity  # structure + anchors gate; hashes are INFO (docs/reverse-engineering/executable-identity.md)

import verify_chase_aim_sites as common
import verify_collide_sites as collide

ROOT = Path(__file__).resolve().parents[2]
CORE = ROOT / 'src/proxy/ui_scale_sites.h'
DEFAULT_EXE = common.DEFAULT_EXE

# (name, va, expected bytes, continuation, the mnemonics that follow the displaced span)
SITES = {
    'projection': (0x4be246, bytes.fromhex('b801000000'), 0x4be24b, ['pop', 'pop', 'pop', 'pop', 'add', 'ret']),
    'width': (0x496194, bytes.fromhex('8b15e4856000'), 0x49619a, ['push', 'push', 'mov', 'call', 'jmp']),
    'height': (0x4961bb, bytes.fromhex('8b0de4856000'), 0x4961c1, ['push', 'push', 'mov', 'call', 'jmp']),
    'main_x': (0x403d36, bytes.fromhex('0fb78014040000'), 0x403d3d, ['add', 'cmp', 'je']),
    'main_y': (0x403d7a, bytes.fromhex('0fb78016040000'), 0x403d81, ['cmp', 'je']),
    'menu_x': (0x411b40, bytes.fromhex('0fbfba14040000'), 0x411b47, ['push', 'mov', 'xor', 'call']),
    'menu_y': (0x411b57, bytes.fromhex('0fbfba16040000'), 0x411b5e, ['push', 'mov', 'mov', 'call']),
    'cursor': (0x4074ec, bytes.fromhex('a3f07c6000'), 0x4074f1, ['push', 'mov', 'mov', 'call']),
    'overlay_icon': (0x42ece0, bytes.fromhex('8b76065156'), 0x42ece5, ['add', 'push', 'call', 'jmp']),
    'cursor_aim': (0x42ddf1, bytes.fromhex('8b73068b7b0b'), 0x42ddf7, ['jl', 'lea', 'call', 'mov', 'mov', 'mov', 'push', 'push', 'push', 'push', 'call']),
    'entry': (0x4bdee0, bytes.fromhex('83ec0883783c00'), 0x4bdee7, ['push', 'mov', 'push', 'mov', 'push', 'mov', 'push']),
}
# The verified windows (va, bytes) beyond the sites themselves.
WINDOWS = {
    'projection_tail': (0x4be1eb, bytes.fromhex('a13c8a6000decadb442414defad9c9d950348b9b30010000f7c3004000007407d8c2d95834eb11f7c300800000'
                                                '7407d8e2d95834eb02ddd8d9c9d95038d9583cd9500cd95008d95004d9501cd95018d95010d9502cd95024d95820'
                                                'b8010000005f5e5d5b83c408c3')),
    'caller1': (0x47e002, bytes.fromhex('e8d9fe03008b8bac0100008b93a80100005152e8c686070083c41084c0740a')),
    'caller2': (0x47e70c, bytes.fromhex('e8cff703008b7e108b8fac0100008b97a80100005152e8b97f070083c41084c0740a')),
    'width_case': (0x496181, bytes.fromhex('a1386f600085c00f84dde8ffff8b080fbf41048b15e485600050528bc7e84de60000e9d9050000')),
    'height_case': (0x4961a8, bytes.fromhex('a1386f600085c00f84efd9ffff8b000fbf40068b0de485600050518bc7e826e60000e9b2050000')),
    'main_x_window': (0x403d31, bytes.fromhex('a13c6f60000fb7801404000083c424663bc37430')),
    'main_y_window': (0x403d75, bytes.fromhex('a13c6f60000fb78016040000663bc3743f')),
    'menu_x_window': (0x411b36, bytes.fromhex('8b153c6f60008b74240c0fbfba14040000568bcb33c0e86fe3ffff')),
    'menu_y_window': (0x411b51, bytes.fromhex('8b153c6f60000fbfba16040000568bcbb801000000e855e3ffff')),
    'menu_callee': (0x40fec0, bytes.fromhex('837c24040053568bf17409')),
    'cursor_pre': (0x4074cd, bytes.fromhex('8b47018b4f068b570ba3647c60008b4710')),
    'cursor_post': (0x4074e4, bytes.fromhex('8b0de48560006a00a3f07c6000518bc38915ec7c6000e8f1d20900')),
    'overlay_case': (0x42ecc5, bytes.fromhex('8b75188b5601a104856000e84be0feff85c00f8411eaffff8b4e0b8b7606515605ac03000050e8b0acffffe921f1ffff')),
    'aim_case': (0x42ddc0, bytes.fromhex('8b5d188b5301a104856000e850effeff85c0894424100f8412f9ffff8b4d1483ceff0bfe83f903c644240c007c1b83f9048b73068b7b0b'
                                        '7c108d430fe86fab07008844240c8b4424108b54240c52575650e8fa75ffff85c00f84d0f8ffff')),
    'icon_test': (0x4299a0, bytes.fromhex('83ec10535556578b7c24248b874803000085c00f84240300008b0d386f60008b11')),
}
# Decoded gap-free (int3 padding bounds): the five site functions, the projection's two callers and the two callees' prefixes.
FUNCTIONS = {'projection': (0x4bdee0, 0x4be3e3), 'dispatcher': (0x493b40, 0x49679c), 'main_loop': (0x403840, 0x404278),
             'menu_loop': (0x410080, 0x412250), 'cursor': (0x406de0, 0x40770c), 'caller1': (0x47d9c0, 0x47e617),
             'caller2': (0x47e620, 0x47e77d), 'size_callee': (0x4a47f0, 0x4a480d), 'menu_callee': (0x40fec0, 0x40fecb),
             'ins_dispatcher': (0x42d340, 0x42f064), 'icon_test': (0x4299a0, 0x4299c1)}
# The INS dispatcher's jump table (the click-point cases G and H).
INS_JUMP_TABLE, INS_JUMP_TABLE_ENTRIES, OVERLAY_CASE, AIM_CASE = 0x42f064, 0x71, 0x64, 0x28
INS_CASE_STARTS = {OVERLAY_CASE: 0x42ecc5, AIM_CASE: 0x42ddc0}
AIM_FLAGS_VA = 0x42ddee  # cmp ecx,4 whose EFLAGS the jl at 0x42ddf7 reads across the H span
JUMP_TABLE, JUMP_TABLE_ENTRIES, WIDTH_CASE, HEIGHT_CASE = 0x49679c, 0xa8, 0x71, 0x72
CASE_STARTS = {WIDTH_CASE: 0x496181, HEIGHT_CASE: 0x4961a8}
SIZE_CALLEE = 0x4a47f0
SIZE_CALLEE_MNEMONICS = ['push', 'mov', 'push', 'lea', 'mov', 'cmp', 'jb', 'mov', 'call', 'mov']  # cmp writes EFLAGS before jb reads them
CURSOR_FOREIGN_CLAIM = ('chase_cursor_write', 0x4074de, 6)  # chase_fire's claim inside the cursor window's gap
COCKPIT_REGISTRY_SLOT, HUD_CAMERA_OFFSET = 0x608504, 8
STUB_LENGTHS = {'projection': 144, 'size': 21, 'main_mouse': 47, 'menu_mouse': 45, 'cursor': 37, 'entry': 79, 'overlay': 43, 'aim': 47}
EXPECTED_CONSTANTS = {
    'projection_function_va': 0x4bdee0, 'projection_function_end_va': 0x4be3e3, 'projection_site_va': 0x4be246, 'projection_return_va': 0x4be24b,
    'projection_tail_va': 0x4be1eb, 'caller1_va': 0x47e002, 'caller2_va': 0x47e70c, 'entry_site_va': 0x4bdee0, 'entry_return_va': 0x4bdee7,
    'dispatcher_va': 0x493b40, 'dispatcher_end_va': 0x49679c, 'jump_table_va': 0x49679c, 'width_case_va': 0x496181, 'width_site_va': 0x496194,
    'width_return_va': 0x49619a, 'height_case_va': 0x4961a8, 'height_site_va': 0x4961bb, 'height_return_va': 0x4961c1,
    'main_loop_va': 0x403840, 'main_loop_end_va': 0x404278, 'menu_loop_va': 0x410080, 'menu_loop_end_va': 0x412250,
    'main_x_site_va': 0x403d36, 'main_y_site_va': 0x403d7a, 'menu_x_site_va': 0x411b40, 'menu_y_site_va': 0x411b57,
    'main_x_window_va': 0x403d31, 'main_y_window_va': 0x403d75, 'menu_x_window_va': 0x411b36, 'menu_y_window_va': 0x411b51,
    'menu_callee_va': 0x40fec0, 'input_context_slot_va': 0x606f3c, 'cursor_function_va': 0x406de0, 'cursor_function_end_va': 0x40770c,
    'cursor_site_va': 0x4074ec, 'cursor_return_va': 0x4074f1, 'cursor_pre_va': 0x4074cd, 'cursor_post_va': 0x4074e4,
    'cursor_foreign_claim_va': 0x4074de, 'cursor_x_va': 0x607cec, 'cursor_y_va': 0x607cf0, 'cockpit_registry_slot_va': 0x608504,
    'ins_dispatcher_va': 0x42d340, 'ins_dispatcher_end_va': 0x42f064, 'ins_jump_table_va': 0x42f064, 'overlay_case_va': 0x42ecc5,
    'overlay_site_va': 0x42ece0, 'overlay_return_va': 0x42ece5, 'aim_case_va': 0x42ddc0, 'aim_site_va': 0x42ddf1, 'aim_return_va': 0x42ddf7,
    'aim_flags_va': 0x42ddee, 'icon_test_va': 0x4299a0, 'cursor_aim_va': 0x425410,
}
LOG_RE = re.compile(r'\bui_scale setting=(?P<setting>\S+) status=(?P<status>pending|off|refused) reason=(?P<reason>\S+) mode=(?P<mode>auto|fixed|off) '
                    r'scale=(?P<scale>[0-9.]+) diagnostic=(?P<diagnostic>\S+) diagnostic_write=(?P<diagnostic_write>\S+) site=(?P<site>[0-9a-f]{8})')
INSTALL_RE = re.compile(r'\bui_scale_install width=(?P<width>\d+) height=(?P<height>\d+) scale=(?P<scale>[0-9.]+) '
                        r'status=(?P<status>patched|patched_unverified|off|refused) reason=(?P<reason>\S+) failed_site=(?P<failed_site>\S+) '
                        r'mode=(?P<mode>auto|fixed) virtual=(?P<vw>\d+)x(?P<vh>\d+) inverse16=(?P<inverse16>\d+) fixed256=(?P<fixed256>\d+) '
                        r'writes=(?P<writes>\S+) arena_used=(?P<arena>\d+)')
FRAME_RE = re.compile(r'\bui_scale_frame frame=(?P<frame>\d+) frames=(?P<frames>\d+) scale=(?P<scale>[0-9.]+) scaled=(?P<scaled>\d+) '
                      r'excluded=(?P<excluded>\d+) excluded_camera=(?P<camera>[0-9a-f]{8}) cameras=(?P<cameras>\S+)')
CAMERA_RE = re.compile(r'\bui_scale_camera frame=(?P<frame>\d+) excluded_camera=(?P<camera>[0-9a-f]{8}) previous=(?P<previous>[0-9a-f]{8})')
ACCUMULATOR_START = 0x8000  # the remainder's start: the running sum rounds to nearest, symmetric in both directions
RESTORE_RE = re.compile(r'\bui_scale_restore status=(?P<status>restored|restore_failed) registered=(?P<registered>[01]) diagnostic=(?P<diagnostic>\S+)')


# ---- the Python twins of the core header's arithmetic ----
def auto_scale(height):
    if not height:
        return 1.0
    s = ((height * 2 + 540) // 1080) / 2  # nearest half step of height/1080, a tie rounds up
    return min(3.0, max(1.0, s))


def inverse16(s):
    return int(65536 / s + 0.5)


def fixed256(s):
    return int(s * 256 + 0.5)


def virtual_size(real, inv16):
    return (real * inv16 + 0x8000) >> 16


def mouse_step(delta, inv16, acc):
    total = delta * inv16 + acc
    return total >> 16, total & 0xffff


def cursor_real(v, s256):
    return (v * s256 + 0x80) >> 8


def anchor(flags):
    return ((0.0, -1.0, 1.0, -1.0)[(flags >> 12) & 3], (0.0, 1.0, -1.0, 1.0)[(flags >> 14) & 3])


def parse_log_line(line):
    m = LOG_RE.search(line)
    return {k: (int(v, 16) if k == 'site' else float(v) if k == 'scale' else v) for k, v in m.groupdict().items()} if m else None


def parse_install_line(line):
    m = INSTALL_RE.search(line)
    if not m:
        return None
    d = m.groupdict()
    return {k: (float(v) if k == 'scale' else int(v) if k in ('width', 'height', 'vw', 'vh', 'inverse16', 'fixed256', 'arena') else v)
            for k, v in d.items()}


def parse_frame_line(line):
    m = FRAME_RE.search(line)
    if not m:
        return None
    d = m.groupdict()
    cameras = {} if d['cameras'] == '-' else {int(c.split(':')[0], 16): tuple(int(n) for n in c.split(':')[1].split('/')) for c in d['cameras'].split(',')}
    return {'frame': int(d['frame']), 'frames': int(d['frames']), 'scale': float(d['scale']), 'scaled': int(d['scaled']),
            'excluded': int(d['excluded']), 'camera': int(d['camera'], 16), 'cameras': cameras}


def parse_camera_line(line):
    m = CAMERA_RE.search(line)
    return {'frame': int(m.group('frame')), 'camera': int(m.group('camera'), 16), 'previous': int(m.group('previous'), 16)} if m else None


def parse_restore_line(line):
    m = RESTORE_RE.search(line)
    return {'status': m.group('status'), 'registered': int(m.group('registered')), 'diagnostic': m.group('diagnostic')} if m else None


# ---- the source header ----
def source_constants(text):
    values = {}
    for name, value in re.findall(r'\b(\w+_va)\s*=\s*(0x[0-9a-fA-F]+)', text):
        values[name] = int(value, 16)
    arrays = {}
    for name, body in re.findall(r'constexpr unsigned char (expected_\w+)\[\w+\] = \{([^}]*)\}', text):
        arrays[name] = bytes(int(v, 16) for v in re.findall(r'0x([0-9a-fA-F]{2})', body))
    lengths = {name: int(value) for name, value in re.findall(r'\b(\w+_stub_length)\s*=\s*(\d+)', text)}
    return values, arrays, lengths


def source_ok(text):
    values, arrays, lengths = source_constants(text)
    ok = all(values.get(k) == v for k, v in EXPECTED_CONSTANTS.items())
    expected_arrays = {'expected_projection_site': SITES['projection'][1], 'expected_width_site': SITES['width'][1],
                       'expected_height_site': SITES['height'][1], 'expected_main_x_site': SITES['main_x'][1],
                       'expected_main_y_site': SITES['main_y'][1], 'expected_menu_x_site': SITES['menu_x'][1],
                       'expected_menu_y_site': SITES['menu_y'][1], 'expected_cursor_site': SITES['cursor'][1],
                       'expected_entry_site': SITES['entry'][1], 'expected_projection_tail': WINDOWS['projection_tail'][1],
                       'expected_caller1': WINDOWS['caller1'][1], 'expected_caller2': WINDOWS['caller2'][1],
                       'expected_width_case': WINDOWS['width_case'][1], 'expected_height_case': WINDOWS['height_case'][1],
                       'expected_main_x_window': WINDOWS['main_x_window'][1], 'expected_main_y_window': WINDOWS['main_y_window'][1],
                       'expected_menu_x_window': WINDOWS['menu_x_window'][1], 'expected_menu_y_window': WINDOWS['menu_y_window'][1],
                       'expected_menu_callee': WINDOWS['menu_callee'][1], 'expected_cursor_pre': WINDOWS['cursor_pre'][1],
                       'expected_cursor_post': WINDOWS['cursor_post'][1], 'expected_overlay_site': SITES['overlay_icon'][1],
                       'expected_aim_site': SITES['cursor_aim'][1], 'expected_overlay_case': WINDOWS['overlay_case'][1],
                       'expected_aim_case': WINDOWS['aim_case'][1], 'expected_icon_test': WINDOWS['icon_test'][1]}
    ok = ok and all(arrays.get(k) == v for k, v in expected_arrays.items())
    ok = ok and all(lengths.get(f'{k}_stub_length') == v for k, v in STUB_LENGTHS.items())
    ok = ok and 'constexpr std::int32_t accumulator_start = 0x8000;' in text
    return ok


# ---- the image ----
def text_bounds(data):
    for name, virtual_size, virtual_address, raw_size, raw_pointer, _ in exe_identity.section_table(data):
        if name == '.text':
            return exe_identity.IMAGE_BASE + virtual_address, exe_identity.IMAGE_BASE + virtual_address + min(virtual_size, raw_size), raw_pointer
    raise ValueError('no .text section')


def raw_branch_hits(data, spans):
    """(source, target) of every rel8/rel32 branch or call encoding in .text whose target lies strictly inside one of the spans."""
    lo, hi, raw = text_bounds(data)
    text = data[raw:raw + (hi - lo)]
    hits = []
    for i in range(len(text) - 6):
        op = text[i]
        if op in (0xe8, 0xe9):
            target = lo + i + 5 + struct.unpack_from('<i', text, i + 1)[0]
        elif op == 0x0f and 0x80 <= text[i + 1] <= 0x8f:
            target = lo + i + 6 + struct.unpack_from('<i', text, i + 2)[0]
        elif 0x70 <= op <= 0x7f or op == 0xeb or 0xe0 <= op <= 0xe3:
            target = lo + i + 2 + struct.unpack_from('<b', text, i + 1)[0]
        else:
            continue
        for a, b in spans:
            if a < target < b:
                hits.append((lo + i, target))
    return hits


def dword_refs(data, spans):
    """Positions of every (aligned or not) dword in the image whose value lies in (a, b] of a span (a raw pointer into a displaced span)."""
    import numpy as np
    refs = []
    for shift in range(4):
        n = (len(data) - shift) // 4
        words = np.frombuffer(data, dtype='<u4', count=n, offset=shift)
        for a, b in spans:
            for i in np.flatnonzero((words > a) & (words <= b)):
                refs.append((hex(int(i) * 4 + shift), hex(int(words[i]))))
    return refs


def direct_target(instruction):
    if instruction.mnemonic.startswith(('j', 'call', 'loop')) and re.fullmatch(r'0x[0-9a-fA-F]+(?:\s*<.*>)?', instruction.operands):
        return int(instruction.operands.split()[0], 16)
    return None


def written_registers(instruction):
    """The 32-bit GPRs an instruction may write, from its first operand (a superset that also counts push/pop/call targets)."""
    m = instruction.mnemonic
    ops = instruction.operands
    first = ops.split(',')[0].strip() if ops else ''
    regs = set()
    if m in ('pop',) or (m.startswith(('mov', 'lea', 'add', 'sub', 'and', 'or', 'xor', 'imul', 'shr', 'sar', 'shl', 'neg', 'inc', 'dec', 'xchg', 'cmov', 'set'))):
        for reg in ('eax', 'ebx', 'ecx', 'edx', 'esi', 'edi', 'ebp', 'esp'):
            if first == reg or first == reg[1:] or (reg in ('eax', 'ebx', 'ecx', 'edx') and first in (reg[1] + 'l', reg[1] + 'h')):
                regs.add(reg)
    if m in ('call', 'ret') or m.startswith('rep'):
        regs |= {'ecx', 'esi', 'edi'} if m.startswith('rep') else set()
    return regs


def inspect(data, instructions, core_text, claims, xmm=()):
    checks = {}
    report = {}
    by_va = {i.va: i for i in instructions}
    starts = set(by_va)
    ends = {i.end for i in instructions}
    image = common.Image(data)
    checks['exe_identity'] = exe_identity.identity_ok(data)
    # Bytes at every site and window.
    for name, (va, expected, _, _) in SITES.items():
        checks[f'{name}_bytes'] = image.read(va, len(expected)) == expected
    for name, (va, expected) in WINDOWS.items():
        checks[f'{name}_window'] = image.read(va, len(expected)) == expected
    report['site_bytes'] = {name: (image.read(va, len(expected)) or b'').hex() for name, (va, expected, _, _) in SITES.items()}
    # Every claimed span on instruction boundaries; the continuation is the next instruction.
    spans = []
    for name, (va, expected, continuation, following) in SITES.items():
        end = va + len(expected)
        spans.append((va, end))
        checks[f'{name}_boundaries'] = va in starts and end in starts and continuation == end
        seq = []
        cursor = end
        for _ in following:
            if cursor not in by_va:
                break
            seq.append(by_va[cursor].mnemonic)
            cursor = by_va[cursor].end
        checks[f'{name}_following'] = seq == following
        report[f'{name}_following'] = seq
    # No decoded direct branch lands strictly inside a span; the sources of branches to a span's start are reported.
    incoming = {name: [] for name in SITES}
    interior = []
    for i in instructions:
        t = direct_target(i)
        if t is None:
            continue
        for name, (va, expected, _, _) in SITES.items():
            if va < t < va + len(expected):
                interior.append((name, hex(i.va), hex(t)))
            elif t == va:
                incoming[name].append(hex(i.va))
    checks['no_branch_into_spans'] = interior == []
    report['branches_into_spans'] = interior
    report['branches_to_site_starts'] = incoming
    # Raw rel8/rel32 encodings landing inside a span start in the middle of a decoded instruction.
    raw = raw_branch_hits(data, spans)
    not_interior = [(hex(s), hex(t)) for s, t in raw if s in starts]
    checks['raw_branch_hits_interior'] = not_interior == []
    report['raw_branch_hits'] = len(raw)
    report['raw_branch_hits_not_interior'] = not_interior
    # No dword in the image points into a displaced span (past its first byte).
    refs = dword_refs(data, spans)
    checks['no_dword_into_spans'] = refs == []
    report['dword_refs'] = refs
    # The KC jump table: the two case entries, no entry inside a span.
    table = [struct.unpack_from('<I', image.read(JUMP_TABLE + 4 * k, 4))[0] for k in range(JUMP_TABLE_ENTRIES)]
    checks['jump_table_cases'] = table[WIDTH_CASE] == CASE_STARTS[WIDTH_CASE] and table[HEIGHT_CASE] == CASE_STARTS[HEIGHT_CASE]
    inside = [hex(t) for t in table if any(a < t < b for a, b in spans)]
    checks['jump_table_entries_outside_spans'] = inside == []
    report['jump_table_entries_inside'] = inside
    checks['jump_table_in_dispatcher'] = all(FUNCTIONS['dispatcher'][0] <= t < FUNCTIONS['dispatcher'][1] and t in starts for t in table)
    # The INS dispatcher's table: the two click-point cases, no entry inside a span, every entry an instruction start.
    ins_table = [struct.unpack_from('<I', image.read(INS_JUMP_TABLE + 4 * k, 4))[0] for k in range(INS_JUMP_TABLE_ENTRIES)]
    checks['ins_jump_table_cases'] = ins_table[OVERLAY_CASE] == INS_CASE_STARTS[OVERLAY_CASE] and ins_table[AIM_CASE] == INS_CASE_STARTS[AIM_CASE]
    ins_inside = [hex(t) for t in ins_table if any(a < t < b for a, b in spans)]
    checks['ins_jump_table_entries_outside_spans'] = ins_inside == []
    checks['ins_jump_table_in_dispatcher'] = all(FUNCTIONS['ins_dispatcher'][0] <= t < FUNCTIONS['ins_dispatcher'][1] and t in starts for t in ins_table)
    # G and H: the five patch bytes inside one aligned qword (one cmpxchg8b each); H's EFLAGS are live across the span:
    # the instruction before it is `cmp ecx,4`, the one after is `jl`, and nothing in the span writes ECX or EFLAGS
    # in a way the stub does not reproduce (the stub ends with the same `cmp ecx,4`; ECX is not written in the span).
    checks['click_sites_one_qword'] = all((SITES[n][0] & 7) + 5 <= 8 for n in ('overlay_icon', 'cursor_aim'))
    before_h = by_va.get(AIM_FLAGS_VA)
    after_h = by_va.get(SITES['cursor_aim'][2])
    span_h = [by_va[SITES['cursor_aim'][0]], by_va[SITES['cursor_aim'][0] + 3]] if SITES['cursor_aim'][0] + 3 in by_va else []
    checks['aim_flags_live_proof'] = before_h is not None and before_h.mnemonic == 'cmp' and before_h.operands == 'ecx,0x4' and \
        before_h.end == SITES['cursor_aim'][0] and after_h is not None and after_h.mnemonic == 'jl' and len(span_h) == 2 and \
        [i.mnemonic for i in span_h] == ['mov', 'mov'] and not any(written_registers(i) & {'ecx'} for i in span_h) and \
        all(i.mnemonic == 'mov' for i in span_h)  # mov writes no flags: the cmp's flags reach the jl in the engine
    # G: EFLAGS dead (`add eax,0x3ac` follows), the callee writes EDX before reading it (mov edx,[ecx] at 0x4299bf).
    g_after = by_va.get(SITES['overlay_icon'][2])
    checks['overlay_flags_dead_and_edx_written'] = g_after is not None and g_after.mnemonic == 'add' and g_after.operands.startswith('eax,') and \
        by_va.get(0x4299bf) is not None and by_va[0x4299bf].mnemonic == 'mov' and by_va[0x4299bf].operands.startswith('edx,') and \
        not any(written_registers(i) & {'edx'} or 'edx' in i.operands for va, i in by_va.items() if 0x4299a0 <= va < 0x4299bf)
    # The size sites' callee: cmp before jb (EFLAGS dead after the claimed mov), as the fov verifier established.
    callee = []
    cursor = SIZE_CALLEE
    for _ in SIZE_CALLEE_MNEMONICS:
        if cursor not in by_va:
            break
        callee.append(by_va[cursor].mnemonic)
        cursor = by_va[cursor].end
    checks['size_callee_prefix'] = callee == SIZE_CALLEE_MNEMONICS
    checks['menu_callee_cmp_first'] = by_va.get(WINDOWS['menu_callee'][0]) is not None and by_va[WINDOWS['menu_callee'][0]].mnemonic == 'cmp'
    # The 2D exit: between the P load (mov eax,[0x608a3c]) and the site no instruction writes EAX, EBX (after its
    # flag-word load) or EBP; the flag-word load is the one write of EBX in that range.
    tail_va = WINDOWS['projection_tail'][0]
    writes = []
    cursor = tail_va
    ebx_loads = []
    while cursor < SITES['projection'][0]:
        i = by_va[cursor]
        w = written_registers(i)
        if cursor == tail_va:
            w.discard('eax')
        if i.mnemonic == 'mov' and i.operands.startswith('ebx,DWORD PTR [ebx+0x130]'):
            ebx_loads.append(hex(cursor))
            w.discard('ebx')
        if w & {'eax', 'ebx', 'ebp'}:
            writes.append((hex(cursor), i.mnemonic, i.operands))
        cursor = i.end
    checks['projection_registers_held'] = writes == [] and ebx_loads == ['0x4be1fd']
    report['projection_register_writes'] = writes
    checks['projection_first_is_p_load'] = by_va[tail_va].mnemonic == 'mov' and by_va[tail_va].operands.startswith('eax,') and \
        by_va[tail_va].operands.endswith('ds:0x608a3c')
    # XMM: none of the decoded functions (the sites' own, the projection's callers, the callees) touches an XMM
    # register, and XMM0..7 are volatile across every call boundary, so the projection stub's XMM0..2 are dead at the
    # 2D exit. The .text-wide census (a lenient linear sweep, data bytes included) is information only.
    in_functions = [(hex(i.va), i.mnemonic) for i in instructions if 'xmm' in i.operands]
    checks['no_sse_in_site_functions'] = in_functions == []
    report['xmm_in_site_functions'] = in_functions
    xmm = list(xmm)
    report['xmm_text_census'] = {'count': len(xmm), 'first': xmm[:8]}
    # Other claims: disjoint from every span and window, except chase_fire's inside the cursor window's gap.
    own_spans = [(va, va + len(b)) for va, b, _, _ in SITES.values() if va != SITES['entry'][0]]
    own_windows = [(va, va + len(b)) for va, b in WINDOWS.values()]
    overlapping = sorted({(name, hex(address)) for name, address, length in claims for lo, hi in own_spans if address < hi and lo < address + length})
    checks['other_claims_disjoint_from_spans'] = overlapping == []
    report['overlapping_claims'] = overlapping
    # Windows other claims may sit in: only the submit-phase fixture group's two return stamps (right after the two
    # calls, inside the caller windows); with that group on, the run-time window check refuses (caller_mismatch).
    in_windows = sorted({(name, hex(address)) for name, address, length in claims for lo, hi in own_windows if address < hi and lo < address + length})
    checks['other_claims_in_windows_known'] = in_windows == [('submit_phase_world_return_a', '0x47e007'), ('submit_phase_world_return_b', '0x47e711')]
    report['claims_in_windows'] = in_windows
    foreign = [(name, hex(address), length) for name, address, length in claims if address == CURSOR_FOREIGN_CLAIM[1]]
    checks['cursor_foreign_claim_in_gap'] = foreign == [(CURSOR_FOREIGN_CLAIM[0], hex(CURSOR_FOREIGN_CLAIM[1]), CURSOR_FOREIGN_CLAIM[2])] and \
        WINDOWS['cursor_pre'][0] + len(WINDOWS['cursor_pre'][1]) == CURSOR_FOREIGN_CLAIM[1] and \
        CURSOR_FOREIGN_CLAIM[1] + CURSOR_FOREIGN_CLAIM[2] == WINDOWS['cursor_post'][0]
    # The entry claim shares its bytes with the submit-phase fixture group (one of them refuses at run time).
    entry_claims = sorted(name for name, address, length in claims if address == SITES['entry'][0])
    checks['entry_shared_with_submit_phase_only'] = entry_claims == ['submit_phase_world_enter']
    # The two INS dispatcher claims of other modules (fov 0x42dbf8, chase_mode_script 0x42e742) are outside both case windows.
    ins_claims = sorted((name, hex(address)) for name, address, length in claims if FUNCTIONS['ins_dispatcher'][0] <= address < FUNCTIONS['ins_dispatcher'][1])
    report['ins_dispatcher_claims'] = ins_claims
    checks['ins_dispatcher_other_claims_outside_cases'] = all(not (WINDOWS[w][0] <= address < WINDOWS[w][0] + len(WINDOWS[w][1]))
                                                          for _, address, _ in claims for w in ('overlay_case', 'aim_case'))
    report['entry_claims'] = entry_claims
    # Source constants and the arithmetic twins.
    checks['source_constants'] = source_ok(core_text)
    checks['auto_mapping'] = [auto_scale(h) for h in (720, 1080, 1200, 1440, 1600, 2160, 4320, 8640)] == [1.0, 1.0, 1.0, 1.5, 1.5, 2.0, 3.0, 3.0]
    checks['fixed_point'] = (inverse16(1.25), inverse16(1.5), inverse16(2), inverse16(3), fixed256(1.25), fixed256(1.5)) == (52429, 43691, 32768, 21845, 320, 384) and \
        (virtual_size(5120, inverse16(1.25)), virtual_size(1440, inverse16(1.25)), virtual_size(5120, inverse16(1.5)), virtual_size(1440, inverse16(3))) == (4096, 1152, 3413, 480) and \
        virtual_size(5120, 65536) == 5120 and mouse_step(3, 65536, 0) == (3, 0) and cursor_real(2048, fixed256(1.25)) == 2560
    acc, total = ACCUMULATOR_START, 0
    for _ in range(10):
        out, acc = mouse_step(1, inverse16(1.5), acc)
        total += out
    negative, acc_negative = 0, ACCUMULATOR_START
    for _ in range(10):
        out, acc_negative = mouse_step(-1, inverse16(1.5), acc_negative)
        negative += out
    checks['mouse_remainder'] = total == 7 and acc == 10926 and negative == -7 and acc_negative == 54610 and anchor(0x200 | 0x1000 | 0x4000) == (-1.0, 1.0) and anchor(0x200) == (0.0, 0.0) \
        and anchor(0x200 | 0x2000 | 0x8000) == (1.0, -1.0) and anchor(0x200 | 0x1000 | 0x2000) == (-1.0, 0.0)
    report['checks'] = checks
    report['instructions'] = len(instructions)
    report['decoded_ranges'] = {name: [hex(a), hex(b)] for name, (a, b) in FUNCTIONS.items()}
    report['claims_considered'] = len(claims)
    report['identity'] = exe_identity.info(data)
    report['result'] = 'PASS' if all(checks.values()) else 'FAIL'
    return report


def decode_functions(exe):
    instructions = []
    for lo, hi in FUNCTIONS.values():
        instructions += common.parse_objdump(common.objdump_window(exe, lo, hi, timeout=120), lo, hi)
    return instructions


def sse_census(exe, data):
    """(va, mnemonic) of every instruction of a linear objdump sweep of .text that names an XMM register (padding gaps allowed)."""
    lo, hi, _ = text_bounds(data)
    text = common.objdump_window(exe, lo, hi, timeout=600)
    found = []
    for line in text.splitlines():
        m = common._INSTRUCTION_RE.match(line)
        if m and ('xmm' in (m.group(4) or '') or m.group(3).lower() in ('movss', 'mulss', 'cvtsi2ss', 'cvttss2si', 'movups', 'movaps')):
            found.append((hex(int(m.group(1), 16)), m.group(3).lower()))
    return found


def verify(exe=DEFAULT_EXE, core=CORE):
    try:
        data = common.image_bytes(exe)
        instructions = decode_functions(exe)
        claims = collide.other_claims(ROOT, skip_prefix='ui_scale', own_names=())
        return inspect(data, instructions, Path(core).read_text(), claims, sse_census(exe, data))
    except (ValueError, OSError, RuntimeError, KeyError) as error:
        return {'result': 'FAIL', 'checks': {'decode': False}, 'error': str(error)}
    except Exception as error:  # subprocess errors from objdump
        return {'result': 'FAIL', 'checks': {'decode': False}, 'error': f'{type(error).__name__}: {error}'}


def patched_image(data, name='projection'):
    """A copy with the named site's first byte replaced by our jmp opcode (what a second install would find)."""  # noqa: D401
    image = common.Image(data)
    va = SITES[name][0]
    for _, base, vsize, rp, rsize in image.sections:
        if base <= va < base + vsize:
            out = bytearray(data)
            out[rp + va - base] = 0xe9
            return bytes(out)
    raise ValueError('site not mapped')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=DEFAULT_EXE)
    parser.add_argument('--core', type=Path, default=CORE)
    args = parser.parse_args()
    report = verify(args.exe, args.core)
    print(json.dumps(report, indent=2, sort_keys=True))
    raise SystemExit(report['result'] != 'PASS')
