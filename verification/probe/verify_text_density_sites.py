#!/usr/bin/env python3
"""Read-only qualification of the text-density sites (src/proxy/text_density_sites.h, src/proxy/text_density.cpp).

src/proxy/text_density.cpp claims, all or none, the font-open entry 0x0048cdc0 (6 bytes), the two block-blit entries
0x0048c090 / 0x0048c460 (7 bytes each), the Materials load call at 0x0048af71 (a call redirect) and, under debug, the
text-line 0x0048b2d0 (6) and rect-fill 0x0048b0b0 (8) entries; at CreateDevice it writes the config field *0x00606f34+0x784
and, for d = 3, the imm32 at 0x004f813b (docs/architecture/text-density.md, docs/reverse-engineering/font-rendering.md
section 3 strategy (a)). This checks X3AP.exe on the host: the structural identity, every byte window the module compares
(sites, helpers, the config stores, the draw path's flag read), a gap-free whole-instruction decode of the site
functions, the helpers, the init routine, the main loop and the config constructor, each claimed span on instruction
boundaries with its continuation the next instruction and the mnemonics that follow, no decoded direct branch and no raw
rel8/rel32 encoding landing inside a span, no dword in the image pointing into a span, the register facts (EDX and EFLAGS
dead after the font span; EAX dead at both blit entries, including the lookup callee; EFLAGS rewritten by the `test`
that follows the blit spans), the call redirect (one caller of 0x0048af70, one direct call of 0x004f44a0, the callee's
SEH prologue), the order proof (config constructor 0x004ec9e0 with its two +0x784 stores called at 0x0040283a and
stored at 0x00402844, Direct3DCreate9 at 0x00402edc, the Materials load at 0x00403497, the main loop at 0x0040373a with
the first font open at 0x00403a26; the only backward branches across the Direct3DCreate9 call are fatal-exit paths),
the four direct callers of the font open, the leaf conventions (the plain-copy wrapper's whole body, `mov esi,eax` /
`push edi` on the colour and alpha leaves), the style window's jle target, every patched word inside one aligned qword,
other claims disjoint, the source constants against this table, and the Python twins of the header's arithmetic.
No Wine, no game launch.
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
CORE = ROOT / 'src/proxy/text_density_sites.h'
DEFAULT_EXE = common.DEFAULT_EXE

# (name, va, claimed bytes, continuation, the mnemonics that follow the displaced span)
SITES = {
    'font': (0x48cdc0, bytes.fromhex('515356578bf9'), 0x48cdc6, ['push', 'push', 'push', 'xor', 'call']),
    'blt_block': (0x48c090, bytes.fromhex('8b4c240883ec24'), 0x48c097, ['test', 'push', 'push', 'push', 'push', 'jge']),
    'blt_alpha': (0x48c460, bytes.fromhex('8b4c240883ec24'), 0x48c467, ['test', 'push', 'push', 'push', 'push', 'jge']),
    'text_line': (0x48b2d0, bytes.fromhex('83ec14535556'), 0x48b2d6, ['mov', 'xor', 'cmp', 'push', 'jge']),
    'rect_fill': (0x48b0b0, bytes.fromhex('83ec1c538b5c2424'), 0x48b0b8, ['test', 'push', 'push', 'push', 'jge']),
}
MATERIALS_SITE, MATERIALS_TARGET, MATERIALS_RETURN, MATERIALS_WRAPPER, MATERIALS_CALLER = 0x48af71, 0x4f44a0, 0x48af76, 0x48af70, 0x403497
STYLE_WINDOW, STYLE_WRITE = 0x4f812d, 0x4f813b
# The verified windows (va, bytes) beyond the claimed spans.
WINDOWS = {
    'font_window': (0x48cdc0, bytes.fromhex('515356578bf9575068c011560033d2e8ec160600')),
    'materials_window': (0x48af70, bytes.fromhex('51e82a950600e8e5a40600')),
    'materials_callee': (0x4f44a0, bytes.fromhex('6aff687003530064')),
    'style_window': (0x4f812d, bytes.fromhex('bf0100000039b8840700007e05bf02000000')),
    'blt_block_window': (0x48c090, bytes.fromhex('8b4c240883ec2485c9535556570f8d9a010000')),
    'blt_alpha_window': (0x48c460, bytes.fromhex('8b4c240883ec2485c9535556570f8d9a010000')),
    'config_read': (0x48c29a, bytes.fromhex('8b15346f60008bb284070000')),
    'row_flag_test': (0x48c28b, bytes.fromhex('a1b08d6000f744b01000000100741e')),
    'row_count_read': (0x48c265, bytes.fromhex('0fbf35ac8d6000')),
    'entry_flag_test': (0x47231a, bytes.fromhex('f7400400010000')),
    'lookup': (0x4f5110, bytes.fromhex('6685c9567c5e663b0dac8d60007d170fbfc18bd0c1e2042bd0a1b08d600066837c900c00743e0fbf15b06960000fbfc1'
                                       '0fbf0db469600003ca3bc17d278b0dac6960008bf0c1e604837c0e08007509e8fcefffff85c0740c8b15ac6960008b4416085ec3'
                                       '33c05ec3')),
    'alloc': (0x4f3950, bytes.fromhex('83ec0853558b6c241c5657bb64000000')),
    'alloc_fields': (0x4f3a11, bytes.fromhex('896e106689460466894e06')),
    'free': (0x4f38d0, bytes.fromhex('53558b6c240c8b450085c05657750883')),
    'copy': (0x4dbbe0, bytes.fromhex('8b4424208b4c241c8b542418578b7c2408508b44241c518b4c241c528b54241c508b44241c5152e8442c010083c4185fc3')),
    'colour_copy': (0x4ee990, bytes.fromhex('538b5c2420558b6c2420568bf057')),
    'alpha_leaf': (0x4efa70, bytes.fromhex('568bf057e8a7d3feff')),
    'text_line_window': (0x48b2d0, bytes.fromhex('83ec145355568b74242433ed3bf5570f8df7000000')),
    'rect_fill_window': (0x48b0b0, bytes.fromhex('83ec1c538b5c242485db5556570f8d5a010000')),
    'config_default_store': (0x4ecae3, bytes.fromhex('898884070000')),
    'config_atol_store': (0x4ecff8, bytes.fromhex('898184070000')),
    'init_ctor_call': (0x40283a, bytes.fromhex('e8a1a10e0083c40885c0a3346f6000')),
    'init_d3d_call': (0x402edc, bytes.fromhex('e88f550d00')),
    'init_materials_call': (0x403497, bytes.fromhex('e8d47a0800')),
    'init_main_loop_call': (0x40373a, bytes.fromhex('e801010000')),
    'loop_cockpit_call': (0x403a26, bytes.fromhex('e8358f0100')),
}
# Decoded gap-free (int3 padding bounds).
FUNCTIONS = {'font_open': (0x48cdc0, 0x48ce3a), 'blt_block': (0x48c090, 0x48c45d), 'blt_alpha': (0x48c460, 0x48c7fd),
             'text_line': (0x48b2d0, 0x48b4ab), 'rect_fill': (0x48b0b0, 0x48b2c1), 'materials_wrapper': (0x48af70, 0x48afb6),
             'style': (0x4f8120, 0x4f8305), 'lookup': (0x4f5110, 0x4f5178), 'copy': (0x4dbbe0, 0x4dbc11),
             'init_routine': (0x402780, 0x40383d), 'main_loop': (0x403840, 0x404278), 'config_ctor': (0x4ec9e0, 0x4ed745)}
INIT_ROUTINE = FUNCTIONS['init_routine']
CONFIG_SLOT, CONFIG_STORE, CONFIG_CTOR, CONFIG_CTOR_CALL, D3D_CREATE_CALL, MAIN_LOOP_CALL, MAIN_LOOP, COCKPIT_INIT, COCKPIT_INIT_CALLS = \
    0x606f34, 0x402844, 0x4ec9e0, 0x40283a, 0x402edc, 0x40373a, 0x403840, 0x41c960, (0x403a26, 0x4050f4)
CONFIG_STORES = (0x4ecae3, 0x4ecff8)
FONT_OPEN_CALLERS = (0x41c9ab, 0x41f7a7, 0x496131, 0x49615b)
FATAL_EXIT = 0x401dd0
LOOKUP_FIRST_EAX_WRITES = (0x4f511f, 0x4f5174)  # movsx eax,cx / xor eax,eax
BLIT_FIRST_EAX_WRITE_OFFSET = 0x13              # mov eax,ds:0x608db8 (the composite path); the other path calls the lookup
COPY_LEAF = 0x4ee850
MPF = {'nofiltering': 0x100, 'fontscale': 0x10000, 'writeable': 0x40000, 'generated': 0x800000}
EXPECTED_CONSTANTS = {
    'config_slot_va': 0x606f34, 'row_count_va': 0x608dac, 'rows_slot_va': 0x608db0, 'table_slot_va': 0x6069ac, 'named_count_va': 0x6069b4,
    'lookup_va': 0x4f5110, 'alloc_va': 0x4f3950, 'free_va': 0x4f38d0, 'copy_va': 0x4dbbe0, 'colour_copy_va': 0x4ee990, 'alpha_leaf_va': 0x4efa70,
    'font_site_va': 0x48cdc0, 'font_return_va': 0x48cdc6, 'materials_wrapper_va': 0x48af70, 'materials_site_va': 0x48af71,
    'materials_target_va': 0x4f44a0, 'materials_return_va': 0x48af76, 'materials_caller_va': 0x403497, 'style_window_va': 0x4f812d,
    'style_write_va': 0x4f813b, 'blt_block_va': 0x48c090, 'blt_alpha_va': 0x48c460, 'config_read_va': 0x48c29a, 'row_flag_test_va': 0x48c28b,
    'row_count_read_va': 0x48c265, 'entry_flag_test_va': 0x47231a, 'alloc_fields_va': 0x4f3a11, 'text_line_va': 0x48b2d0, 'rect_fill_va': 0x48b0b0,
    'init_routine_va': 0x402780, 'init_routine_end_va': 0x40383d, 'config_ctor_call_va': 0x40283a, 'config_ctor_va': 0x4ec9e0,
    'config_store_va': 0x402844, 'd3d_create_call_va': 0x402edc, 'main_loop_call_va': 0x40373a, 'main_loop_va': 0x403840,
    'cockpit_init_call_va': 0x403a26, 'cockpit_init_va': 0x41c960, 'config_default_store_va': 0x4ecae3, 'config_atol_store_va': 0x4ecff8,
}
EXPECTED_ARRAYS = {
    'expected_font_window': 'font_window', 'expected_materials_window': 'materials_window', 'expected_materials_callee': 'materials_callee',
    'expected_style_window': 'style_window', 'expected_blit_window': 'blt_block_window', 'expected_config_read': 'config_read',
    'expected_row_flag_test': 'row_flag_test', 'expected_row_count_read': 'row_count_read', 'expected_entry_flag_test': 'entry_flag_test',
    'expected_lookup': 'lookup', 'expected_alloc': 'alloc', 'expected_alloc_fields': 'alloc_fields', 'expected_free': 'free',
    'expected_copy': 'copy', 'expected_colour_copy': 'colour_copy', 'expected_alpha_leaf': 'alpha_leaf',
    'expected_text_line_window': 'text_line_window', 'expected_rect_fill_window': 'rect_fill_window',
    'expected_config_default_store': 'config_default_store', 'expected_config_atol_store': 'config_atol_store',
}
LOG_RE = re.compile(r'\btext_density setting=(?P<setting>\S+) status=(?P<status>pending|off|refused) reason=(?P<reason>\S+) '
                    r'mode=(?P<mode>auto|fixed|off) value=(?P<value>\d) diagnostic=(?P<diagnostic>[01]) failed_site=(?P<failed_site>\S+) '
                    r'writes=(?P<writes>\S+) site=(?P<site>[0-9a-f]{8})')
INSTALL_RE = re.compile(r'\btext_density_install density=(?P<density>\d) scale=(?P<scale>[0-9.]+) '
                        r'status=(?P<status>patched|patched_unverified|off|refused) reason=(?P<reason>\S+) mode=(?P<mode>auto|fixed) '
                        r'config_before=(?P<config_before>\d+) style=(?P<style>stock|patched) style_write=(?P<style_write>\S+) '
                        r'rows=(?P<rows>\S+) fonts=(?P<fonts>\S+) missing=(?P<missing>\S+) caps_limited_d=(?P<caps_limited_d>\d) '
                        r'max_texture=(?P<max_w>\d+)x(?P<max_h>\d+) arena_used=(?P<arena>\d+)')
ROWS_RE = re.compile(r'\btext_density_rows status=(?P<status>applied|pending|refused|already|off) reason=(?P<reason>\S+) density=(?P<density>\d) '
                     r'rows=(?P<rows>\d+) flagged=(?P<flagged>\d+) unflagged=(?P<unflagged>\d+) nofilter_cleared=(?P<nofilter>\d+) '
                     r'caps_limited_d=(?P<caps_limited_d>\d) largest_row=(?P<largest_w>\d+)x(?P<largest_h>\d+) missing=(?P<missing>\S+)')
FONT_RE = re.compile(r'\btext_density_font name=(?P<name>\S+) size=(?P<size>\d+) density=(?P<density>\d) status=(?P<status>scaled|missing|invalid) '
                     r'file=(?P<file>\S+) cell_width=(?P<cell_width>\d+) y_offset=(?P<y_offset>\d+)')
SHADOW_RE = re.compile(r'\btext_density_shadow src=(?P<src>-?\d+) status=(?P<status>built|refused) reason=(?P<reason>\S+) '
                       r'size=(?P<w>\d+)x(?P<h>\d+) density=(?P<density>\d) bytes=(?P<bytes>\d+) total=(?P<total>\d+) slots=(?P<slots>\d+) ms=(?P<ms>\d+)')
DRAW_RE = re.compile(r'\btext_density_draw fn=(?P<fn>text_line|blt_block|blt_alpha|rect_fill) src=(?P<src>-?\d+) dst=(?P<dst>-?\d+) '
                     r'dst_flagged=(?P<dst_flagged>[01]) src_flagged=(?P<src_flagged>[01]) src_generated=(?P<src_generated>[01]) handled=(?P<handled>[01])')
RESET_RE = re.compile(r'\btext_density_reset shadows=(?P<shadows>\d+) bytes=(?P<bytes>\d+)')
RESTORE_RE = re.compile(r'\btext_density_restore status=(?P<status>restored|restore_failed) registered=(?P<registered>[01])')


# ---- the Python twins of the core header ----
def parse_setting(text):
    """(mode, value): mode 'auto' | 'value' | 'invalid'."""
    if text is None or text == '':
        return 'auto', 1
    if text == 'auto':
        return 'auto', 1
    if text in ('1', '2', '3'):
        return 'value', int(text)
    return 'invalid', 1


def density_for(mode, value, s):
    if mode == 'value':
        return min(3, max(1, value))
    if not s > 1.0:
        return 1
    import math
    return min(3, math.ceil(s))


def font_file(name, size, d, extension):
    if not name or len(name) > 32 or not 1 <= size <= 255 or not 1 <= d <= 3:
        return None
    if any(ord(c) < 0x21 or ord(c) > 0x7e or c in '\\/:' for c in name):
        return None
    return f'f\\{name}{size * d}{extension}'


def plan_row(flags, clear_nofiltering):
    """(edit, new flags): edit 'flag' | 'unflag' | 'none'."""
    if flags & MPF['generated'] and flags & MPF['writeable']:
        f = flags | MPF['fontscale']
        if clear_nofiltering:
            f &= ~MPF['nofiltering']
        return ('none' if f == flags else 'flag'), f
    if flags & MPF['fontscale'] and not flags & MPF['generated']:
        return 'unflag', flags & ~MPF['fontscale']
    return 'none', flags


def clip_destination(W, H, sx, sy, dx, dy, w, h):
    """The engine's clip (0x0048c2c8..0x0048c32c): (drawn, sx, sy, dx, dy, w, h)."""
    ok = True
    if dx < W:
        right = dx + w
        if right > 0:
            if dx < 0:
                sx -= dx
                w = right
                dx = 0
            elif right > W:
                w = W - dx
        else:
            ok = False
    else:
        ok = False
    if dy < H:
        bottom = dy + h
        if bottom > 0:
            if dy < 0:
                sy -= dy
                h = bottom
                dy = 0
            elif bottom > H:
                h = H - dy
        else:
            return (False, sx, sy, dx, dy, w, h)
    else:
        return (False, sx, sy, dx, dy, w, h)
    return (ok, sx, sy, dx, dy, w, h)


def parse_log_line(line):
    m = LOG_RE.search(line)
    return {k: (int(v, 16) if k == 'site' else int(v) if k in ('value', 'diagnostic') else v) for k, v in m.groupdict().items()} if m else None


def parse_install_line(line):
    m = INSTALL_RE.search(line)
    if not m:
        return None
    d = m.groupdict()
    return {k: (float(v) if k == 'scale' else int(v) if k in ('density', 'config_before', 'arena', 'caps_limited_d', 'max_w', 'max_h') else v)
            for k, v in d.items()}


def parse_rows_line(line):
    m = ROWS_RE.search(line)
    return {k: (v if k in ('status', 'reason', 'missing') else int(v)) for k, v in m.groupdict().items()} if m else None


FAMILIES = (('Tahoma', 13), ('Zekton', 26), ('ZektonES', 26), ('Harrier', 24))


def missing_fonts(d, present):
    """The family names (`Tahoma26`) whose pair is not entirely in `present` (a set of `f\\<Name><S*d>.<ext>` paths)."""
    return [f'{name}{size * d}' for name, size in FAMILIES
            if not (font_file(name, size, d, '.abc') in present and font_file(name, size, d, '.tga') in present)]


def caps_density(d, largest_w, largest_h, max_w, max_h):
    for k in range(d, 1, -1):
        if (not max_w or largest_w * k <= max_w) and (not max_h or largest_h * k <= max_h):
            return k
    return 1


def parse_font_line(line):
    m = FONT_RE.search(line)
    return {k: (v if k in ('name', 'status', 'file') else int(v)) for k, v in m.groupdict().items()} if m else None


def parse_shadow_line(line):
    m = SHADOW_RE.search(line)
    return {k: (v if k in ('status', 'reason') else int(v)) for k, v in m.groupdict().items()} if m else None


def parse_draw_line(line):
    m = DRAW_RE.search(line)
    return {k: (v if k == 'fn' else int(v)) for k, v in m.groupdict().items()} if m else None


def parse_reset_line(line):
    m = RESET_RE.search(line)
    return {k: int(v) for k, v in m.groupdict().items()} if m else None


def parse_restore_line(line):
    m = RESTORE_RE.search(line)
    return {'status': m.group('status'), 'registered': int(m.group('registered'))} if m else None


# ---- the source header ----
def source_constants(text):
    text = re.sub(r'//[^\n]*', '', text)  # the byte tables carry commented instructions with hex operands
    values = {name: int(value, 16) for name, value in re.findall(r'\b(\w+_va)\s*=\s*(0x[0-9a-fA-F]+)', text)}
    arrays = {}
    for name, body in re.findall(r'constexpr unsigned char (expected_\w+)\[\w+\] = \{([^}]*)\}', text):
        arrays[name] = bytes(int(v, 16) for v in re.findall(r'0x([0-9a-fA-F]{2})', body))
    return values, arrays


def source_ok(text):
    values, arrays = source_constants(text)
    ok = all(values.get(k) == v for k, v in EXPECTED_CONSTANTS.items())
    ok = ok and all(arrays.get(name) == WINDOWS[window][1] for name, window in EXPECTED_ARRAYS.items())
    ok = ok and 'mpf_nofiltering = 0x100' in text and 'mpf_fontscale = 0x10000' in text and 'mpf_writeable = 0x40000' in text \
        and 'mpf_generated = 0x800000' in text and 'config_density_offset = 0x784' in text and 'generated_surface_flags = 0x402c' in text \
        and '{{"Tahoma", 13}, {"Zekton", 26}, {"ZektonES", 26}, {"Harrier", 24}}' in text and 'row_width_offset = 0x14, row_height_offset = 0x16' in text
    return ok


# ---- the image ----
def text_bounds(data):
    for name, virtual_size, virtual_address, raw_size, raw_pointer, _ in exe_identity.section_table(data):
        if name == '.text':
            return exe_identity.IMAGE_BASE + virtual_address, exe_identity.IMAGE_BASE + virtual_address + min(virtual_size, raw_size), raw_pointer
    raise ValueError('no .text section')


def raw_branches(data):
    """(source, target, kind) of every rel8/rel32 branch or call encoding in .text."""
    lo, hi, raw = text_bounds(data)
    text = data[raw:raw + (hi - lo)]
    out = []
    for i in range(len(text) - 6):
        op = text[i]
        if op in (0xe8, 0xe9):
            out.append((lo + i, (lo + i + 5 + struct.unpack_from('<i', text, i + 1)[0]) & 0xffffffff, 'call' if op == 0xe8 else 'jmp'))
        elif op == 0x0f and 0x80 <= text[i + 1] <= 0x8f:
            out.append((lo + i, (lo + i + 6 + struct.unpack_from('<i', text, i + 2)[0]) & 0xffffffff, 'jcc32'))
        elif 0x70 <= op <= 0x7f or op == 0xeb or 0xe0 <= op <= 0xe3:
            out.append((lo + i, (lo + i + 2 + struct.unpack_from('<b', text, i + 1)[0]) & 0xffffffff, 'rel8'))
    return out


def config_field_stores(data):
    """VAs of every `mov [reg+0x784],reg` encoding in .text (89 /r with mod=10, no SIB, disp32 0x784)."""
    lo, hi, raw = text_bounds(data)
    text = data[raw:raw + (hi - lo)]
    return [lo + i for i in range(len(text) - 6)
            if text[i] == 0x89 and (text[i + 1] & 0xc0) == 0x80 and (text[i + 1] & 7) != 4 and text[i + 2:i + 6] == b'\x84\x07\x00\x00']


def dword_refs(data, spans):
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


FLAG_READERS = ('j', 'set', 'cmov', 'adc', 'sbb', 'rcl', 'rcr', 'pushf', 'lahf', 'into', 'loop')


def reads_flags(instruction):
    m = instruction.mnemonic
    return m.startswith(FLAG_READERS) and m not in ('jmp',)


def inspect(data, instructions, core_text, claims):
    checks = {}
    report = {}
    by_va = {i.va: i for i in instructions}
    starts = set(by_va)
    image = common.Image(data)
    read = lambda va, n: image.read(va, n) or b''
    checks['exe_identity'] = exe_identity.identity_ok(data)
    # A raw branch encoding counts as real when its source is an instruction start of a decoded function or lies
    # outside every decoded range (operand bytes inside a decoded instruction are not branches).
    decoded_ranges = list(FUNCTIONS.values())
    plausible = lambda s: s in starts or not any(lo <= s < hi for lo, hi in decoded_ranges)
    for name, (va, expected, _, _) in SITES.items():
        checks[f'{name}_bytes'] = read(va, len(expected)) == expected
    for name, (va, expected) in WINDOWS.items():
        checks[f'{name}_window'] = read(va, len(expected)) == expected
    report['site_bytes'] = {name: read(va, len(expected)).hex() for name, (va, expected, _, _) in SITES.items()}
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
    spans.append((MATERIALS_SITE, MATERIALS_RETURN))
    spans.append((STYLE_WRITE, STYLE_WRITE + 4))
    # No decoded direct branch lands strictly inside a span (the style imm32 and the call's rel32 included).
    interior = []
    for i in instructions:
        t = direct_target(i)
        if t is None:
            continue
        for a, b in spans:
            if a < t < b:
                interior.append((hex(i.va), hex(t)))
    checks['no_branch_into_spans'] = interior == []
    report['branches_into_spans'] = interior
    raw = raw_branches(data)
    not_interior = [(hex(s), hex(t)) for s, t, _ in raw if any(a < t < b for a, b in spans) and s in starts]
    checks['raw_branch_hits_interior'] = not_interior == []
    report['raw_branch_hits_not_interior'] = not_interior
    refs = dword_refs(data, spans)
    checks['no_dword_into_spans'] = refs == []
    report['dword_refs'] = refs
    # Every patched word inside one aligned qword (one lock cmpxchg8b each).
    checks['patches_one_qword'] = all((va & 7) + 5 <= 8 for va, _, _, _ in SITES.values()) and (MATERIALS_SITE & 7) + 5 <= 8 and (STYLE_WRITE & 7) + 4 <= 8
    # The font span: EDX dead (xor edx,edx before any EDX read), EFLAGS dead (no flag reader before the sprintf call),
    # ESP untouched by the displaced instructions except the pushes the tail replays; the four direct callers.
    cursor = SITES['font'][2]
    edx_dead = flags_dead = False
    seen = []
    while cursor in by_va and by_va[cursor].mnemonic != 'call':
        i = by_va[cursor]
        seen.append((hex(cursor), i.mnemonic, i.operands))
        if 'edx' in i.operands and i.mnemonic != 'xor':
            break
        if i.mnemonic == 'xor' and i.operands == 'edx,edx':
            edx_dead = True
        if reads_flags(i):
            break
        cursor = i.end
    else:
        flags_dead = cursor in by_va and by_va[cursor].mnemonic == 'call'
    checks['font_edx_and_flags_dead'] = edx_dead and flags_dead
    report['font_after_span'] = seen
    font_callers = sorted(s for s, t, kind in raw if t == SITES['font'][0] and kind == 'call' and plausible(s))
    checks['font_callers'] = font_callers == sorted(FONT_OPEN_CALLERS)
    report['font_callers'] = [hex(c) for c in font_callers]
    # The blit spans: EAX dead at entry (no EAX access before the composite path's write or the lookup call), the
    # jge's target is the lookup call, the lookup writes EAX before reading it, EFLAGS rewritten by `test ecx,ecx`.
    for name in ('blt_block', 'blt_alpha'):
        va = SITES[name][0]
        seq = []
        cursor = va
        ok = True
        while cursor in by_va and cursor < va + BLIT_FIRST_EAX_WRITE_OFFSET:
            i = by_va[cursor]
            seq.append(i.mnemonic)
            if re.search(r'\b(eax|ax|al|ah)\b', i.operands):
                ok = False
            cursor = i.end
        first = by_va.get(va + BLIT_FIRST_EAX_WRITE_OFFSET)
        jge = by_va.get(va + 0xd)
        target = direct_target(jge) if jge else None
        call = by_va.get(target) if target else None
        ok = ok and first is not None and first.mnemonic == 'mov' and first.operands.startswith('eax,') and jge is not None and \
            jge.mnemonic == 'jge' and call is not None and call.mnemonic == 'call' and direct_target(call) == 0x4f5110
        checks[f'{name}_eax_dead'] = ok
        after = by_va.get(SITES[name][2])
        checks[f'{name}_flags_rewritten'] = after is not None and after.mnemonic == 'test' and after.operands == 'ecx,ecx'
    lookup_reads = [(hex(i.va), i.operands) for i in instructions
                    if 0x4f5110 <= i.va < LOOKUP_FIRST_EAX_WRITES[0] and re.search(r'\b(eax|ax|al|ah)\b', i.operands)]
    checks['lookup_writes_eax_first'] = lookup_reads == [] and all(
        by_va.get(w) is not None and by_va[w].operands.startswith('eax,') for w in LOOKUP_FIRST_EAX_WRITES)
    report['lookup_eax_reads_before_write'] = lookup_reads
    # The Materials call: the wrapper's one caller, the callee's one direct call, the SEH prologue, no other reference.
    materials_callers = sorted(s for s, t, kind in raw if t == MATERIALS_WRAPPER and plausible(s))
    callee_callers = sorted(s for s, t, kind in raw if t == MATERIALS_TARGET and plausible(s))
    checks['materials_single_caller'] = materials_callers == [MATERIALS_CALLER] and callee_callers == [MATERIALS_SITE]
    site = by_va.get(MATERIALS_SITE)
    checks['materials_site_is_call'] = site is not None and site.mnemonic == 'call' and direct_target(site) == MATERIALS_TARGET and \
        by_va.get(MATERIALS_WRAPPER) is not None and by_va[MATERIALS_WRAPPER].mnemonic == 'push' and \
        by_va.get(MATERIALS_RETURN) is not None and by_va[MATERIALS_RETURN].mnemonic == 'call'
    report['materials_callers'] = [hex(c) for c in materials_callers]
    # The style window: cmp [eax+0x784],edi; jle to the instruction after `mov edi,2`; the imm32 offset.
    jle = by_va.get(STYLE_WINDOW + 11)
    mov2 = by_va.get(STYLE_WINDOW + 13)
    checks['style_window_shape'] = jle is not None and jle.mnemonic == 'jle' and direct_target(jle) == STYLE_WINDOW + 18 and \
        mov2 is not None and mov2.mnemonic == 'mov' and mov2.operands == 'edi,0x2' and mov2.va + 1 == STYLE_WRITE
    # The order proof inside the init routine.
    calls = {va: direct_target(by_va[va]) for va in (CONFIG_CTOR_CALL, D3D_CREATE_CALL, MATERIALS_CALLER, MAIN_LOOP_CALL) if va in by_va}
    checks['init_order_calls'] = calls.get(CONFIG_CTOR_CALL) == CONFIG_CTOR and calls.get(D3D_CREATE_CALL) == 0x4d8470 and \
        calls.get(MATERIALS_CALLER) == MATERIALS_WRAPPER and calls.get(MAIN_LOOP_CALL) == MAIN_LOOP and \
        CONFIG_CTOR_CALL < CONFIG_STORE < D3D_CREATE_CALL < MATERIALS_CALLER < MAIN_LOOP_CALL < INIT_ROUTINE[1]
    store = by_va.get(CONFIG_STORE)
    checks['init_config_store'] = store is not None and store.raw == b'\xa3' + struct.pack('<I', CONFIG_SLOT)
    once = {t: [s for s, tt, kind in raw if tt == t and kind == 'call' and INIT_ROUTINE[0] <= s < INIT_ROUTINE[1] and plausible(s)]
            for t in (CONFIG_CTOR, 0x4d8470, MATERIALS_WRAPPER, MAIN_LOOP)}
    checks['init_calls_once'] = once == {CONFIG_CTOR: [CONFIG_CTOR_CALL], 0x4d8470: [D3D_CREATE_CALL], MATERIALS_WRAPPER: [MATERIALS_CALLER],
                                         MAIN_LOOP: [MAIN_LOOP_CALL]}
    # Backward branches in the init routine across the Direct3DCreate9 call only reach fatal-exit stubs (push imm8; call 0x401dd0).
    backward = []
    for i in instructions:
        if not (D3D_CREATE_CALL <= i.va < INIT_ROUTINE[1]):
            continue
        t = direct_target(i)
        if t is not None and INIT_ROUTINE[0] <= t < D3D_CREATE_CALL:
            push = by_va.get(t)
            call = by_va.get(push.end) if push else None
            fatal = push is not None and push.mnemonic == 'push' and call is not None and call.mnemonic == 'call' and direct_target(call) == FATAL_EXIT
            backward.append((hex(i.va), hex(t), fatal))
    checks['init_backward_branches_fatal'] = all(f for _, _, f in backward) and all(int(t, 16) > CONFIG_STORE for _, t, _ in backward)
    report['init_backward_branches'] = backward
    # The main loop opens the cockpit (the first native font open) at 0x00403a26; both cockpit-init callers lie after the init routine.
    cockpit_callers = sorted(s for s, t, kind in raw if t == COCKPIT_INIT and kind == 'call' and plausible(s))
    checks['cockpit_init_callers_after_init'] = cockpit_callers == sorted(COCKPIT_INIT_CALLS) and all(c >= INIT_ROUTINE[1] for c in cockpit_callers)
    report['cockpit_init_callers'] = [hex(c) for c in cockpit_callers]
    # The config field: exactly the constructor's two stores in .text, both inside 0x004ec9e0.
    stores = config_field_stores(data)
    checks['config_field_two_stores'] = sorted(stores) == sorted(CONFIG_STORES) and all(FUNCTIONS['config_ctor'][0] <= s < FUNCTIONS['config_ctor'][1] for s in stores)
    report['config_field_stores'] = [hex(s) for s in stores]
    # The leaves: the plain-copy wrapper calls 0x004ee850 (rel32), the colour and alpha leaves take EAX (mov esi,eax) and EDI (push edi).
    copy_call = by_va.get(0x4dbc07)
    checks['copy_wrapper_calls_leaf'] = copy_call is not None and copy_call.mnemonic == 'call' and direct_target(copy_call) == COPY_LEAF and \
        by_va.get(0x4dbc0c) is not None and by_va[0x4dbc0c].raw == bytes.fromhex('83c418') and by_va.get(0x4dbc10) is not None and by_va[0x4dbc10].mnemonic == 'ret'
    checks['register_leaves'] = read(0x4ee99b, 3) == bytes.fromhex('8bf057') and read(0x4efa71, 3) == bytes.fromhex('8bf057')
    # Other claims: disjoint from every span and window.
    own = spans + [(va, va + len(b)) for va, b in WINDOWS.values()]
    overlapping = sorted({(name, hex(address)) for name, address, length in claims for lo, hi in own if address < hi and lo < address + length})
    checks['other_claims_disjoint'] = overlapping == []
    report['overlapping_claims'] = overlapping
    # Source constants and the twins.
    checks['source_constants'] = source_ok(core_text)
    checks['parser_twin'] = [parse_setting(t) for t in (None, '', 'auto', '1', '2', '3', '4', '0', 'Auto', '2.0', 'auto ')] == \
        [('auto', 1), ('auto', 1), ('auto', 1), ('value', 1), ('value', 2), ('value', 3), ('invalid', 1), ('invalid', 1), ('invalid', 1),
         ('invalid', 1), ('invalid', 1)]
    checks['density_twin'] = [density_for('auto', 1, s) for s in (1.0, 1.25, 1.5, 2.0, 2.01, 3.0)] == [1, 2, 2, 2, 3, 3] and \
        [density_for('value', v, 1.0) for v in (1, 2, 3)] == [1, 2, 3]
    checks['font_file_twin'] = font_file('Tahoma', 13, 2, '.abc') == 'f\\Tahoma26.abc' and font_file('Zekton', 26, 3, '.tga') == 'f\\Zekton78.tga' \
        and font_file('Harrier', 24, 2, '.abc') == 'f\\Harrier48.abc' and font_file('', 13, 2, '.abc') is None and font_file('a\\b', 13, 2, '.abc') is None
    checks['row_plan_twin'] = plan_row(0x8c0000, True) == ('flag', 0x8d0000) and plan_row(0x8c0100, True) == ('flag', 0x8d0000) and \
        plan_row(0x8c0100, False) == ('flag', 0x8d0100) and plan_row(0x10000, True) == ('unflag', 0) and plan_row(0x8d0000, True) == ('none', 0x8d0000) \
        and plan_row(0x800000, True) == ('none', 0x800000)
    checks['gate_twins'] = missing_fonts(2, {'f\\Tahoma26.abc', 'f\\Tahoma26.tga', 'f\\Zekton52.abc', 'f\\Zekton52.tga', 'f\\ZektonES52.abc'}) == \
        ['ZektonES52', 'Harrier48'] and missing_fonts(3, set()) == ['Tahoma39', 'Zekton78', 'ZektonES78', 'Harrier72'] and \
        caps_density(3, 64, 2048, 4096, 4096) == 2 and caps_density(3, 64, 2048, 8192, 8192) == 3 and caps_density(3, 64, 2048, 0, 0) == 3 and \
        caps_density(2, 1024, 1024, 2048, 1024) == 1 and caps_density(2, 1024, 1024, 2048, 2048) == 2
    checks['clip_twin'] = clip_destination(256, 256, 0, 0, -10, 5, 20, 20) == (True, 10, 0, 0, 5, 10, 20) and \
        clip_destination(256, 256, 0, 0, 250, 250, 20, 20) == (True, 0, 0, 250, 250, 6, 6) and \
        clip_destination(256, 256, 0, 0, 300, 0, 20, 20)[0] is False and clip_destination(256, 256, 0, 0, 0, -30, 20, 20)[0] is False
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


def verify(exe=DEFAULT_EXE, core=CORE):
    try:
        data = common.image_bytes(exe)
        instructions = decode_functions(exe)
        claims = collide.other_claims(ROOT, skip_prefix='text_density', own_names=())
        return inspect(data, instructions, Path(core).read_text(), claims)
    except (ValueError, OSError, RuntimeError, KeyError) as error:
        return {'result': 'FAIL', 'checks': {'decode': False}, 'error': str(error)}
    except Exception as error:  # subprocess errors from objdump
        return {'result': 'FAIL', 'checks': {'decode': False}, 'error': f'{type(error).__name__}: {error}'}


def patched_image(data, name='font'):
    """A copy with the named site's first byte replaced by our jmp opcode (what a second install would find)."""
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
