#!/usr/bin/env python3
"""Read-only qualification of the two cull-census trampoline sites in 0x0047cfe0.

src/proxy/cull_census.cpp claims the six-byte `mov eax,[edi+0x1dc]` at
0x0047d258 (the first instruction after both per-node measures are final) and
the six bytes `mov edi,[edi+0xc]; cmp dword [edi],0` at 0x0047d528 (the
common exit of every evaluated node) of the per-node cull and LOD pass
(docs/reverse-engineering/lod-selection.md, "Cull census sites"). This checks
the installed X3AP.exe on the host: exact identity, the 31-byte and 27-byte
windows, that each site is whole decoded instructions of exactly six bytes
with the expected next instruction (the flag writer `test eax,eax`, the flag
consumer `je 0x0047d548`), that no direct branch anywhere in the function
0x0047cfe0..0x0047d551 lands inside either displaced span, that the sites'
incoming branches are exactly the documented ones, that the function ends in
`ret 8`, and that src/proxy/cull_census_core.h carries the same constants and
byte windows. The LOD-ladder fields rest on the pattern the exit stub reads
EBX and the frame slots under (`LADDER_PATTERN`: D in EBX and [esp+0x10], the
0x004863c0 model pointer in EBX and [esp+0x14], the count word at model+0x10,
the record array at model+0x0c, the threshold at record+0x34) and on the exact
writer sets of EBX and ESP between the two sites and of both slots in the
whole function. Also the stub encoders and the `cull_census`,
`cull_census_frame` and per-node row parsers the host test exercises. No
Wine, no game launch.
"""
import argparse
import hashlib
import json
import re
import struct
import subprocess
from pathlib import Path

import exe_identity  # structure + anchors gate; hashes are INFO (docs/reverse-engineering/executable-identity.md)

import verify_chase_aim_sites as common

ROOT = Path(__file__).resolve().parents[2]
CORE = ROOT / 'src/proxy/cull_census_core.h'
DEFAULT_EXE = common.DEFAULT_EXE
FUNCTION = (0x47cfe0, 0x47d552)
MEASURE_WINDOW_VA, MEASURE_SITE_VA, MEASURE_NEXT_VA = 0x47d248, 0x47d258, 0x47d25e
EXIT_WINDOW_VA, EXIT_SITE_VA, EXIT_NEXT_VA = 0x47d519, 0x47d528, 0x47d52e
MEASURE_WINDOW = bytes.fromhex('85c0 8944242c 7508 c744242c01000000 8b87dc010000 85c0 b900001800 7e10'.replace(' ', ''))
EXIT_WINDOW = bytes.fromhex('83f903 7c0a 818f3001000000001000 8b7f0c 833f00 7418 8b742418'.replace(' ', ''))
MEASURE_SITE = MEASURE_WINDOW[MEASURE_SITE_VA - MEASURE_WINDOW_VA:MEASURE_SITE_VA - MEASURE_WINDOW_VA + 6]
EXIT_SITE = EXIT_WINDOW[EXIT_SITE_VA - EXIT_WINDOW_VA:EXIT_SITE_VA - EXIT_WINDOW_VA + 6]
MEASURE_WINDOW_INSTRUCTIONS = [0x47d248, 0x47d24a, 0x47d24e, 0x47d250, 0x47d258, 0x47d25e, 0x47d260, 0x47d265]
EXIT_WINDOW_INSTRUCTIONS = [0x47d519, 0x47d51c, 0x47d51e, 0x47d528, 0x47d52b, 0x47d52e, 0x47d530]
# Direct branches whose target is the site itself (allowed: they land on the jmp).
MEASURE_SOURCES = [0x47d231, 0x47d24e]
EXIT_SOURCES = [0x47d085, 0x47d0a4, 0x47d0ea, 0x47d112, 0x47d1a2, 0x47d1af, 0x47d2e7, 0x47d51c]
EXIT_JE_TARGET = 0x47d548
RET_VA = 0x47d54f
MEASURE_STUB_LENGTH, MEASURE_STUB_CONTINUE, EXIT_STUB_LENGTH, EXIT_STUB_CONTINUE = 43, 37, 39, 33
# The ladder pattern (docs/reverse-engineering/lod-selection.md, "Cull census sites", LOD ladder).
LADDER_PATTERN = [
    (0x47d1c0, '8bd8'),                                 # mov ebx,eax              ; D
    (0x47d1c8, '895c2410'),                             # mov [esp+0x10],ebx       ; D slot
    (0x47d1e9, '8bd8895c2410'),                         # mov ebx,eax; mov [esp+0x10],ebx (scaled D)
    (0x47d2f6, '33dbe9da010000'),                       # xor ebx,ebx; jmp 0x47d4d7 (negative model id)
    (0x47d2fd, '50e8bd9000008bd883c40485db895c2414'),   # push eax; call 0x4863c0; mov ebx,eax; add esp,4; test ebx,ebx; mov [esp+0x14],ebx
    (0x47d321, '0fbf6b10'),                             # movsx ebp,word [ebx+0x10] ; LOD count
    (0x47d42f, '8b4c24148b510c8d1caa8da424000000008b03db4034'),  # model slot -> [+0x0c] records -> [rec] -> fild [rec+0x34]
    (0x47d45f, '83eb04'),                               # sub ebx,4 (loop cursor)
    (0x47d46e, '8b5c2414'),                             # mov ebx,[esp+0x14]       ; restored after the loop
]
# Writers after the measure site and before the exit site (EBX, ESP), and in the whole function (the two slots).
EBX_WRITERS = [0x47d2f6, 0x47d303, 0x47d436, 0x47d45f, 0x47d46e]
ESP_WRITERS = [0x47d2fd, 0x47d305, 0x47d439]
D_SLOT_WRITERS, MODEL_SLOT_WRITERS = [0x47d1c8, 0x47d1eb], [0x47d30a]
_NO_DEST = ('cmp', 'test', 'bt', 'push', 'call', 'jmp', 'nop')
LOG_RE = re.compile(r'\bcull_census requested=(?P<requested>[01]) patched=(?P<patched>[01]) reason=(?P<reason>\S+) measure_site=0x(?P<measure_site>[0-9a-f]{8}) '
                    r'exit_site=0x(?P<exit_site>[0-9a-f]{8}) write_measure=(?P<write_measure>none|atomic|plain) write_exit=(?P<write_exit>none|atomic|plain) '
                    r'stub_measure=0x(?P<stub_measure>[0-9a-f]{8}) stub_exit=0x(?P<stub_exit>[0-9a-f]{8}) ring=(?P<ring>\d+)')
FRAME_RE = re.compile(r'\bcull_census_frame device=(?P<device>\d+) frame=(?P<frame>\d+) entries=(?P<entries>\d+) overflow=(?P<overflow>\d+) '
                      r'unmeasured=(?P<unmeasured>\d+) exited=(?P<exited>\d+) ring=(?P<ring>\d+)')
ROW_RE = re.compile(r'\bcull_census device=(?P<device>\d+) frame=(?P<frame>\d+) view=(?P<view>[0-9a-f]{8}) node=(?P<node>[0-9a-f]{8}) model=(?P<model>[0-9a-f]{8}) '
                    r's=(?P<s>-?\d+) measure=(?P<measure>-?\d+) d=(?P<d>-?\d+) radius=(?P<radius>-?\d+) thr_1dc=(?P<thr_1dc>-?\d+) thr_1d8=(?P<thr_1d8>-?\d+) '
                    r'limit=(?P<limit>-?\d+) flags_in=(?P<flags_in>[0-9a-f]{8}) flags_out=(?P<flags_out>[0-9a-f]{8}) lod=(?P<lod>-?\d+) verdict=(?P<verdict>\w+)'
                    r'(?: scope=\w+)?(?: lods=(?P<lods>-?\d+|-) thr=(?P<thr>-?\d+(?:,-?\d+)*|-)(?: body=(?P<body>[!-~]+))?)?')
VERDICTS = ('kept', 'culled_size', 'culled_min', 'culled_other', 'no_exit', 'culled_small')
HEX_FIELDS = ('view', 'node', 'model', 'flags_in', 'flags_out')


def encode_measure_stub(at, enabled, handler, next_slot):
    """cmp byte [enabled],0; je continue; push eax/ecx/edx; push [esp+0x34] (view); push [esp+0x20] (d); push [esp+0x40] (s);
    push esi; push edi; call handler; add esp,20; pop edx/ecx/eax; continue: jmp [next]. The C++ encoder's contract."""
    for value in (at, enabled, handler, next_slot):
        if not 0 <= value <= 0xffffffff:
            raise ValueError('addresses must be 32-bit VAs')
    code = (b'\x80\x3d' + struct.pack('<I', enabled) + b'\x00' + b'\x74' + bytes([MEASURE_STUB_CONTINUE - 9]) + b'\x50\x51\x52'
            + b'\xff\x74\x24\x34' + b'\xff\x74\x24\x20' + b'\xff\x74\x24\x40' + b'\x56\x57'
            + b'\xe8' + struct.pack('<I', (handler - (at + 31)) & 0xffffffff) + b'\x83\xc4\x14' + b'\x5a\x59\x58'
            + b'\xff\x25' + struct.pack('<I', next_slot))
    assert len(code) == MEASURE_STUB_LENGTH
    return code


def encode_exit_stub(at, enabled, handler, next_slot):
    """cmp byte [enabled],0; je continue; push eax/ecx/edx; push [esp+0x1c] (D slot); push [esp+0x24] (model slot); push ebx; push edi;
    call handler; add esp,16; pop edx/ecx/eax; continue: jmp [next]."""
    for value in (at, enabled, handler, next_slot):
        if not 0 <= value <= 0xffffffff:
            raise ValueError('addresses must be 32-bit VAs')
    code = (b'\x80\x3d' + struct.pack('<I', enabled) + b'\x00' + b'\x74' + bytes([EXIT_STUB_CONTINUE - 9]) + b'\x50\x51\x52'
            + b'\xff\x74\x24\x1c' + b'\xff\x74\x24\x24' + b'\x53\x57'
            + b'\xe8' + struct.pack('<I', (handler - (at + 27)) & 0xffffffff) + b'\x83\xc4\x10' + b'\x5a\x59\x58'
            + b'\xff\x25' + struct.pack('<I', next_slot))
    assert len(code) == EXIT_STUB_LENGTH
    return code


def parse_log_line(line):
    """The one `cull_census` install line -> dict, or None."""
    match = LOG_RE.search(line)
    if not match:
        return None
    row = match.groupdict()
    return {'requested': row['requested'] == '1', 'patched': row['patched'] == '1', 'reason': row['reason'],
            'measure_site': int(row['measure_site'], 16), 'exit_site': int(row['exit_site'], 16),
            'write_measure': row['write_measure'], 'write_exit': row['write_exit'],
            'stub_measure': int(row['stub_measure'], 16), 'stub_exit': int(row['stub_exit'], 16), 'ring': int(row['ring'])}


def parse_frame_line(line):
    """One `cull_census_frame` line -> dict of ints with the bound checked, or None."""
    match = FRAME_RE.search(line)
    if not match:
        return None
    row = {k: int(v) for k, v in match.groupdict().items()}
    row['bounded'] = row['entries'] <= row['ring'] and row['exited'] <= row['entries']
    return row


def parse_row(line):
    """One per-node `cull_census` row -> dict, or None. Rows with the ladder fields also carry
    `lods` (int, None for `-`) and `thr` (list of ints, empty for `-`); older rows carry neither key.
    Rows with the body field also carry `body` (the body-table name, None for `-`)."""
    match = ROW_RE.search(line)
    if not match:
        return None
    row = match.groupdict()
    lods, thr, body = row.pop('lods'), row.pop('thr'), row.pop('body')
    out = {k: (int(v, 16) if k in HEX_FIELDS else v if k == 'verdict' else int(v)) for k, v in row.items()}
    if lods is not None:
        out['lods'] = None if lods == '-' else int(lods)
        out['thr'] = [] if thr == '-' else [int(v) for v in thr.split(',')]
    if body is not None:
        out['body'] = None if body == '-' else body
    return out


def source_constants(text):
    """VAs, lengths, offsets and the two byte windows from cull_census_core.h."""
    def value(name):
        match = re.search(rf'\b{name}\s*=\s*(0x[0-9a-fA-F]+|\d+)\s*[;,]', text)
        return int(match.group(1), 0) if match else None

    def array(name):
        match = re.search(rf'\b{name}\[\w+\]\s*=\s*\{{([^}}]*)\}}', text)
        return bytes(int(b, 0) for b in re.findall(r'0x[0-9a-fA-F]{2}', match.group(1))) if match else b''
    names = ('function_va', 'function_end_va', 'measure_window_va', 'measure_site_va', 'measure_next_va', 'measure_window_length', 'measure_site_offset',
             'site_length', 'exit_window_va', 'exit_site_va', 'exit_next_va', 'exit_window_length', 'exit_site_offset', 'ret_pop',
             'parent_offset', 'radius_offset', 'flags12c_offset', 'model_offset', 'lod_offset', 'threshold_1d8_offset', 'threshold_1dc_offset',
             'ring_size', 'measure_stub_length', 'measure_stub_continue', 'exit_stub_length', 'exit_stub_continue',
             'model_slot_offset', 'd_slot_offset', 'model_records_offset', 'model_lod_count_offset', 'record_threshold_offset', 'ladder_cap')
    return {name: value(name) for name in names} | {'measure_window': array('measure_window'), 'measure_site': array('measure_site'),
                                                     'exit_window': array('exit_window'), 'exit_site': array('exit_site')}


EXPECTED_CONSTANTS = {'function_va': FUNCTION[0], 'function_end_va': FUNCTION[1], 'measure_window_va': MEASURE_WINDOW_VA, 'measure_site_va': MEASURE_SITE_VA,
                      'measure_next_va': MEASURE_NEXT_VA, 'measure_window_length': len(MEASURE_WINDOW), 'measure_site_offset': MEASURE_SITE_VA - MEASURE_WINDOW_VA,
                      'site_length': 6, 'exit_window_va': EXIT_WINDOW_VA, 'exit_site_va': EXIT_SITE_VA, 'exit_next_va': EXIT_NEXT_VA,
                      'exit_window_length': len(EXIT_WINDOW), 'exit_site_offset': EXIT_SITE_VA - EXIT_WINDOW_VA, 'ret_pop': 8,
                      'parent_offset': 0x18, 'radius_offset': 0xa0, 'flags12c_offset': 0x12c, 'model_offset': 0x140, 'lod_offset': 0x14c,
                      'threshold_1d8_offset': 0x1d8, 'threshold_1dc_offset': 0x1dc, 'ring_size': 8192,
                      'measure_stub_length': MEASURE_STUB_LENGTH, 'measure_stub_continue': MEASURE_STUB_CONTINUE,
                      'exit_stub_length': EXIT_STUB_LENGTH, 'exit_stub_continue': EXIT_STUB_CONTINUE,
                      'model_slot_offset': 0x14, 'd_slot_offset': 0x10, 'model_records_offset': 0x0c, 'model_lod_count_offset': 0x10,
                      'record_threshold_offset': 0x34, 'ladder_cap': 8,
                      'measure_window': MEASURE_WINDOW, 'measure_site': MEASURE_SITE, 'exit_window': EXIT_WINDOW, 'exit_site': EXIT_SITE}


def _destination(instruction):
    return instruction.operands.split(',')[0].strip()


def writers(instructions, start, end):
    """In [start, end): the VAs writing EBX (any part) or ESP, and every writer of the literal [esp+0x10]/[esp+0x14] dwords."""
    ebx, esp, d_slot, model_slot = [], [], [], []
    for i in instructions:
        if not start <= i.va < end:
            continue
        dest = _destination(i)
        writes = i.mnemonic not in _NO_DEST
        if ((writes and re.fullmatch(r'e?bx|b[lh]', dest)) or (i.mnemonic == 'pop' and dest == 'ebx')
                or i.mnemonic in ('popa', 'popad') or (i.mnemonic in ('xchg', 'xadd', 'cmpxchg') and re.search(r'\b(e?bx|b[lh])\b', i.operands))):
            ebx.append(i.va)
        if (i.mnemonic in ('push', 'pop', 'pusha', 'pushad', 'popa', 'popad', 'pushf', 'pushfd', 'popf', 'popfd', 'enter', 'leave')
                or (writes and dest == 'esp')):
            esp.append(i.va)
        slot = re.fullmatch(r'(?:\w+ PTR )?\[esp\+0x(1[0-7])\]', dest)
        if slot and writes:
            (d_slot if int(slot.group(1), 16) < 0x14 else model_slot).append(i.va)
    return ebx, esp, d_slot, model_slot


def decode(exe):
    return common.parse_objdump(common.objdump_window(exe, *FUNCTION, timeout=60), *FUNCTION)


def inspect(data, instructions, core_text):
    image = common.Image(data)
    by_va = {i.va: i for i in instructions}
    branches = [(i.va, t) for i in instructions for t in [common._is_direct_control(i)] if t is not None]
    def interior(site):
        return sorted((a, t) for a, t in branches if site < t < site + 6)
    def sources(site):
        return sorted(a for a, t in branches if t == site)
    m_site, m_next = by_va.get(MEASURE_SITE_VA), by_va.get(MEASURE_NEXT_VA)
    x_site, x_cmp, x_next = by_va.get(EXIT_SITE_VA), by_va.get(EXIT_SITE_VA + 3), by_va.get(EXIT_NEXT_VA)
    ret = by_va.get(RET_VA)
    constants = source_constants(core_text)
    ebx_writers, esp_writers, _, _ = writers(instructions, MEASURE_SITE_VA + 6, EXIT_SITE_VA)
    _, _, d_slot_writers, model_slot_writers = writers(instructions, *FUNCTION)
    starts = {i.va for i in instructions}
    checks = {
        'exe_identity': exe_identity.identity_ok(data),
        'preferred_base': image.image_base == common.IMAGE_BASE,
        'measure_window_bytes': image.read(MEASURE_WINDOW_VA, len(MEASURE_WINDOW)) == MEASURE_WINDOW,
        'exit_window_bytes': image.read(EXIT_WINDOW_VA, len(EXIT_WINDOW)) == EXIT_WINDOW,
        'measure_window_whole_instructions': [i.va for i in instructions if MEASURE_WINDOW_VA <= i.va < MEASURE_WINDOW_VA + len(MEASURE_WINDOW)] == MEASURE_WINDOW_INSTRUCTIONS
                                             and by_va.get(MEASURE_WINDOW_VA + len(MEASURE_WINDOW)) is not None,
        'exit_window_whole_instructions': [i.va for i in instructions if EXIT_WINDOW_VA <= i.va < EXIT_WINDOW_VA + len(EXIT_WINDOW)] == EXIT_WINDOW_INSTRUCTIONS
                                          and by_va.get(EXIT_WINDOW_VA + len(EXIT_WINDOW)) is not None,
        # The displaced span is one whole instruction; the next one writes every flag before anything reads them.
        'measure_site_whole_instruction': m_site is not None and m_site.raw == MEASURE_SITE and m_site.mnemonic == 'mov' and m_site.end == MEASURE_NEXT_VA,
        'measure_next_writes_flags': m_next is not None and m_next.mnemonic == 'test' and m_next.raw == bytes.fromhex('85c0'),
        # Two whole instructions; the displaced CMP regenerates the flags the JE after the tail consumes.
        'exit_site_whole_instructions': x_site is not None and x_cmp is not None and x_site.raw + x_cmp.raw == EXIT_SITE and x_site.mnemonic == 'mov'
                                        and x_cmp.mnemonic == 'cmp' and x_cmp.end == EXIT_NEXT_VA,
        'exit_next_consumes_flags': x_next is not None and x_next.mnemonic == 'je' and common._is_direct_control(x_next) == EXIT_JE_TARGET,
        'no_interior_branch': interior(MEASURE_SITE_VA) == [] and interior(EXIT_SITE_VA) == [],
        'measure_sources': sources(MEASURE_SITE_VA) == MEASURE_SOURCES,
        'exit_sources': sources(EXIT_SITE_VA) == EXIT_SOURCES,
        'function_ret': ret is not None and ret.mnemonic == 'ret' and ret.raw == bytes.fromhex('c20800') and ret.end == FUNCTION[1],
        'source_constants': constants == EXPECTED_CONSTANTS,
        # The ladder fields: the pattern bytes, whole instructions, and the exact writer sets the exit guard relies on.
        'ladder_pattern_bytes': all(image.read(va, len(bytes.fromhex(h))) == bytes.fromhex(h) for va, h in LADDER_PATTERN),
        'ladder_pattern_whole_instructions': all(va in starts and va + len(bytes.fromhex(h)) in starts for va, h in LADDER_PATTERN),
        'ebx_writers_between_sites': ebx_writers == EBX_WRITERS,
        'esp_writers_between_sites': esp_writers == ESP_WRITERS,
        'slot_writers': d_slot_writers == D_SLOT_WRITERS and model_slot_writers == MODEL_SLOT_WRITERS,
        'encoders': len(encode_measure_stub(0x10000000, 0x10002000, 0x10001000, 0x10000030)) == MEASURE_STUB_LENGTH
                    and len(encode_exit_stub(0x10000100, 0x10002000, 0x10001100, 0x10000120)) == EXIT_STUB_LENGTH,
    }
    return {'result': 'PASS' if all(checks.values()) else 'FAIL', 'checks': checks, 'exe_info': exe_identity.info(data),
            'measure_site': hex(MEASURE_SITE_VA), 'exit_site': hex(EXIT_SITE_VA),
            'measure_sources': [hex(a) for a in sources(MEASURE_SITE_VA)], 'exit_sources': [hex(a) for a in sources(EXIT_SITE_VA)],
            'interior_branches': [(hex(a), hex(t)) for a, t in interior(MEASURE_SITE_VA) + interior(EXIT_SITE_VA)],
            'ebx_writers': [hex(a) for a in ebx_writers], 'esp_writers': [hex(a) for a in esp_writers],
            'd_slot_writers': [hex(a) for a in d_slot_writers], 'model_slot_writers': [hex(a) for a in model_slot_writers],
            'function_instructions': len(instructions), 'exe_sha256': hashlib.sha256(data).hexdigest()}


def verify(exe=DEFAULT_EXE, core=CORE):
    data = common.image_bytes(exe)
    try:
        return inspect(data, decode(exe), Path(core).read_text())
    except (ValueError, OSError, subprocess.SubprocessError, RuntimeError) as error:
        return {'result': 'FAIL', 'checks': {'decode': False}, 'error': str(error)}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=DEFAULT_EXE)
    parser.add_argument('--core', type=Path, default=CORE)
    args = parser.parse_args()
    report = verify(args.exe, args.core)
    print(json.dumps(report, indent=2, sort_keys=True))
    raise SystemExit(report['result'] != 'PASS')
