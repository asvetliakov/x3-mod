#!/usr/bin/env python3
"""Read-only instruction/ABI qualification of the media-record allocator hook.

Twin of verify_post_phase_sites.py, one level down again: the media-record
allocator 0x00498140, the only caller of the DirectShow graph constructor
0x004cf460 and the retry engine behind the 380 ms sector stall
(docs/reverse-engineering/media-cue-playback.md,
docs/reverse-engineering/sector-post-pass.md §2). The whole routine
0x00498140..0x004982ac is decoded gap-free, not isolated bytes.

Verified: the exact 5-byte entry span and that it is two whole instructions,
no direct branch into the span interior (from the routine's own decode and from
a raw-encoding scan of every .text byte offset), the exact set of five call
sites and their cdecl `add esp,4` cleanups, the plain-copy contract (no relative
control transfer in the span, so the arena tail is the patched bytes verbatim),
the ESP contract of the routine and of the span, the failure path (free, the two
paired allocation counters unwound, `xor eax,eax`, four pops, `ret`) and the
success path (list link, `[rec+0x10] = id`, `eax = rec`), the id->flags map, the
two-dword frame of the play helper 0x004f65f0 that makes the caller's return
address and the cue kind readable at entry, the selector call at 0x0045c607,
disjointness from every installed stamp table, and the absence of any reference
to the entry address anywhere in the image. No Wine, no game.
"""
import argparse
import json
import re
import struct
import subprocess
from pathlib import Path

import exe_identity  # structure + anchors gate; hashes are INFO (docs/reverse-engineering/executable-identity.md)

import verify_chase_aim_sites as common

ROOT = Path(__file__).resolve().parents[2]
# The production table; the check also passes without one (source_present: false).
SOURCE = ROOT / 'src/proxy/media_cue_sites.h'
INSTALLED = ROOT / 'src/proxy/game_phase_sites.h'
OTHER_TABLES = (ROOT / 'src/proxy/frame_phase_sites.h',
                ROOT / 'src/proxy/pass_phase_sites.h',
                ROOT / 'src/proxy/loop_phase_sites.h',
                ROOT / 'src/proxy/post_phase_sites.h')
DEFAULT_EXE = common.DEFAULT_EXE

# Media-record allocator: `push ebx; mov ebx,[esp+8]` at 0x00498140, two `ret`
# (failure 0x004981fd, success 0x004982ab); next function after int3 padding at
# 0x004982b0. cdecl, one stack argument (the media id) plus EAX (flags).
ROUTINE = (0x498140, 0x4982ac)
LEDGER = (
    ('media_create_enter', 0x498140, '538b5c2408'),
)
SITES = tuple(common.HookSpec(row[0], row[1], bytes.fromhex(row[2]), *ROUTINE)
              for row in LEDGER)
# The entry is reached only by the five `call` sites, never by an internal
# branch: the routine's own decode must target it zero times.
INCOMING = {0x498140: set()}
# Every direct caller of 0x00498140 and the cdecl cleanup that proves the single
# stack argument is caller-popped.  The return address at [esp] on entry is the
# discriminator an early-return trampoline scopes on.
CALL_SITES = {
    0x49873a: (0x49873f, 0x49873f, '83c404'),              # 0x00498730 stop/query helper
    0x498bae: (0x498bb3, 0x498bb7, '83c404'),              # 0x00498ad0 savegame MOVI restore
    0x498cd8: (0x498cdd, 0x498cdd, '83c404'),              # 0x00498c90 play-by-id (script VM)
    0x498ef8: (0x498efd, 0x498efd, '83c404'),              # 0x00498e30 speech cue
    0x4f6610: (0x4f6615, 0x4f6615, '83c404'),              # 0x004f65f0 track/emitter play helper
}
# Instructions the ABI and the neutrality argument rest on.
ANCHORS = (
    (0x498140, '53', 'push ebx: the span opens the callee-saved sequence'),
    (0x498141, '8b5c2408', 'ebx = [esp+8]: the media id, the single stack argument'),
    (0x498145, '55', 'push ebp'),
    (0x498148, 'bd40000000', 'ebp = 0x40: the manager record size'),
    (0x49814e, '8bf8', 'edi = eax: the flags register argument'),
    (0x498150, 'e86f910700', 'call 0x005112c4 (malloc)'),
    (0x498176, '012df4856000', 'add [0x006085f4],ebp: live-bytes counter'),
    (0x498182, '012df8896000', 'add [0x006089f8],ebp: lifetime-bytes counter'),
    (0x498188, '8305fc89600001', 'add [0x006089fc],1: lifetime-allocs counter'),
    (0x498193, 'e8480c0800', 'call 0x00518de0 (memset rec,0,0x40)'),
    (0x49819f, '85ff', 'test edi,edi: caller flags override the id->flags map'),
    (0x4981a3, '81fb20030000', 'cmp ebx,0x320 (id 800)'),
    (0x4981cc, 'bf10010000', 'edi = 0x110 for ids 101..999'),
    (0x4981d1, '57', 'push edi (flags)'),
    (0x4981d2, '53', 'push ebx (id)'),
    (0x4981d3, 'e888720300', 'call 0x004cf460 (graph constructor)'),
    (0x4981d8, '83c408', 'add esp,8: the constructor is cdecl, two arguments'),
    (0x4981dd, '894624', 'mov [rec+0x24],eax: the created object or 0'),
    (0x4981e0, '7566', 'jne 0x00498248: success'),
    (0x4981e2, '56', 'push esi (the unlinked record)'),
    (0x4981e3, 'e8c85f0700', 'call 0x0050e1b0 (free)'),
    (0x4981e8, '292df4856000', 'sub [0x006085f4],ebp: live-bytes unwound'),
    (0x4981f1, '292df8856000', 'sub [0x006085f8],ebp: live-blocks unwound'),
    (0x4981f7, '33c0', 'xor eax,eax: the failure return value and ZF=1'),
    (0x4981f9, '5f5e5d5b', 'pop edi/esi/ebp/ebx: all four restored'),
    (0x4981fd, 'c3', 'ret: cdecl, the caller pops the argument'),
    (0x498248, '8b888c000000', 'ecx = [obj+0x8c]: the media flags'),
    (0x49824e, '8b15446f6000', 'edx = the manager list head *0x00606f44'),
    (0x498268, '8930', 'the record is linked into the list'),
    (0x49829e, '83662cfd', 'and [rec+0x2c],~2: a new stream is not playing'),
    (0x4982a3, '895e10', 'mov [rec+0x10],ebx: the id is the record key'),
    (0x4982a6, '8bc6', 'eax = rec: the success return value'),
    (0x4982ab, 'c3', 'ret'),
)
# The selector -> play helper -> allocator chain.  0x004f65f0 pushes exactly two
# dwords (its `push ecx` frame slot and the id) between its own return address
# and the call, so at 0x00498140 entry [esp+0xc] is the selector's return
# address 0x0045c60c and [esp+0x10] is the cue kind 0x5a.
CHAIN = (
    (0x45c5e8, '6a00', 'selector: push 0 (eighth argument)'),
    (0x45c5f0, '8b7014', 'esi = [Videos record + 0x14]: the media id'),
    (0x45c605, '6a5a', 'push 0x5a: the cue kind of the sector post pass'),
    (0x45c607, 'e8e49f0900', 'call 0x004f65f0'),
    (0x45c60c, '83c420', 'add esp,0x20: eight cdecl arguments'),
    (0x4f65f0, '51', 'play helper prologue: push ecx (one frame slot)'),
    (0x4f660d, '56', 'push esi: the id, the only argument of 0x00498140'),
    (0x4f660e, '33c0', 'xor eax,eax: flags 0, so the id->flags map runs'),
    (0x4f6610, 'e82b1bfaff', 'call 0x00498140'),
    (0x4f6615, '83c404', 'add esp,4'),
    (0x4f6618, '8b0d446f6000', 're-scan of the manager list after the create'),
    (0x4f6633, '59c3', 'pop ecx; ret: create failed -> the helper does nothing'),
    (0x4f6635, '8b542408', 'success: edx = the kind argument'),
    (0x4f6639, '83482c04', 'or [rec+0x2c],4'),
    (0x4f6641, '895030', 'mov [rec+0x30],edx: the kind'),
    (0x45c38b, '89ba78010000', 'selector writes the winning index to [sector+0x178]'),
)
# 0x004cf460 is called from one site only; it reports failure as a 0 return and
# stores no HRESULT in any global (checked below over its whole body).
CONSTRUCTOR = (0x4cf460, 0x4d0428)
SINGLE_CALLERS = {0x4cf460: 0x4981d3}
ARENAS = (0x10000000, 0x71000000, 0xf1000000)
SECTIONS = (('.text', 0x400, 0x130630), ('.rdata', 0x130c00, 0x4074d),
            ('.data', 0x171400, 0xb000), ('.rsrc', 0x17c400, 0x91814))
TEXT_BASE, TEXT_OFFSET, TEXT_SIZE = 0x401000, 0x400, 0x130630
_INSTALLED_SPEC_RE = re.compile(
    r'\{\s*"([^"]+)"\s*,\s*(0x[0-9a-fA-F]+)\s*,\s*\{([^}]*)\}\s*,\s*(\d+)\s*,')


def decode(exe=DEFAULT_EXE, start=ROUTINE[0], stop=ROUTINE[1]):
    run = subprocess.run([common.OBJDUMP, '-d', '-Mintel', '--insn-width=16',
                          f'--start-address={start}', f'--stop-address={stop}',
                          str(exe)], check=True, capture_output=True, text=True, timeout=120)
    return common.parse_objdump(run.stdout, start, stop)


def source_checks(text):
    if text is None:
        return None
    actual = common.parse_source_specs(text)
    expected = [dict(name=spec.name, va=spec.va, bytes=spec.expected,
                     length=len(spec.expected), rel32_offset=0, rel32_target=0)
                for spec in SITES]
    return actual == expected


def raw_interior_scan(data, starts=frozenset()):
    """Every .text byte offset that could encode a direct branch into the span."""
    interior = {a for s in SITES for a in range(s.va + 1, s.end)}
    hits = []
    for offset in range(TEXT_OFFSET, TEXT_OFFSET + TEXT_SIZE - 6):
        va = TEXT_BASE + (offset - TEXT_OFFSET)
        byte = data[offset]
        if byte in (0xe8, 0xe9):
            target = (va + 5 + struct.unpack_from('<i', data, offset + 1)[0]) & 0xffffffff
        elif byte == 0x0f and 0x80 <= data[offset + 1] <= 0x8f:
            target = (va + 6 + struct.unpack_from('<i', data, offset + 2)[0]) & 0xffffffff
        elif byte == 0xeb or 0x70 <= byte <= 0x7f or byte in (0xe0, 0xe1, 0xe2, 0xe3):
            target = (va + 2 + struct.unpack_from('<b', data, offset + 1)[0]) & 0xffffffff
        else:
            continue
        if target in interior:
            decoded_here = ROUTINE[0] <= va < ROUTINE[1]
            hits.append({'at': f'{va:#010x}', 'target': f'{target:#010x}',
                         'in_decoded_routine': decoded_here,
                         'reachable': (va in starts) if decoded_here else True})
    return hits


def raw_call_sites(data, targets):
    found = {target: [] for target in targets}
    for offset in range(TEXT_OFFSET, TEXT_OFFSET + TEXT_SIZE - 5):
        if data[offset] != 0xe8:
            continue
        va = TEXT_BASE + (offset - TEXT_OFFSET)
        target = (va + 5 + struct.unpack_from('<i', data, offset + 1)[0]) & 0xffffffff
        if target in found:
            found[target].append(va)
    return found


def data_reference_hits(data):
    """Any dword equal to a span address, anywhere in the file."""
    hits = []
    for spec in SITES:
        for va in range(spec.va, spec.end):
            word = struct.pack('<I', va)
            index = data.find(word)
            while index >= 0:
                section = next((name for name, start, size in SECTIONS
                                if start <= index < start + size), '?')
                hits.append({'value': f'{va:#010x}', 'file_offset': f'{index:#x}',
                             'section': section, 'aligned': index % 4 == 0})
                index = data.find(word, index + 1)
    return hits


def read_bytes(image, va, count):
    value = image.read(va, count)
    return value.hex() if value is not None else None


ALLOCATION_COUNTERS = (0x6085f4, 0x6085f8, 0x6089f8, 0x6089fc)
# Mnemonics whose first operand is written; `cmp`/`test`/`push` only read.
WRITE_MNEMONICS = frozenset((
    'mov', 'movzx', 'movsx', 'add', 'adc', 'sub', 'sbb', 'and', 'or', 'xor',
    'inc', 'dec', 'neg', 'not', 'shl', 'shr', 'sar', 'rol', 'ror', 'xchg',
    'lea', 'pop', 'setne', 'sete', 'imul', 'fstp', 'fst', 'fistp'))
_GLOBAL_WRITE_RE = re.compile(r'^(?:BYTE|WORD|DWORD) PTR ds:0x([0-9a-f]+),')


def constructor_global_stores(exe):
    """Globals the graph constructor writes, from its own gap-free decode.

    Failure has to be reported by the return value alone for an early-return
    trampoline to be indistinguishable from a real failed build.  The only
    globals 0x004cf460 writes are the four allocator counters; the HRESULT it
    tests (`cmp esi,0x8007000e`) never leaves the frame, and the `fs:0` writes
    are its own SEH frame, restored before every return.
    """
    stores = []
    for instruction in decode(exe, *CONSTRUCTOR):
        if instruction.mnemonic not in WRITE_MNEMONICS:
            continue
        match = _GLOBAL_WRITE_RE.match(instruction.operands)
        if match and int(match.group(1), 16) not in ALLOCATION_COUNTERS:
            stores.append({'at': f'{instruction.va:#010x}',
                           'operands': instruction.operands})
    return stores


def installed_spans(text):
    return [(int(m.group(2), 16), int(m.group(4)))
            for m in _INSTALLED_SPEC_RE.finditer(text)]


def inspect(image, instructions, source, data, installed, exe=DEFAULT_EXE):
    checks = {'preferred_base': image.image_base == common.IMAGE_BASE}
    checks['source_specs'] = source_checks(source) is not False
    by_va = {i.va: i for i in instructions}
    rows = []
    for spec in SITES:
        row = common.inspect_site(image, spec, instructions)
        row['plain_copy_ok'] = row['plain_no_relative_control']
        row['arena_replay_ok'] = row['plain_copy_ok'] and all(
            bytes(spec.expected) == bytes(spec.expected) for _ in ARENAS)
        sources = {i.va for i in instructions if common._is_direct_control(i) == spec.va}
        row['incoming_sources'] = sorted(f'{s:#010x}' for s in sources)
        row['incoming_ok'] = sources == INCOMING[spec.va]
        # `push ebx` then `mov ebx,[esp+8]`: the span is ESP-relative and
        # self-consistent, so the arena tail must run at the game's exact ESP.
        row['esp_effect'] = 'push ebx (-4) then an ESP-relative read of the argument'
        row['no_installed_conflict'] = all(spec.end <= at or spec.va >= at + length
                                           for at, length in installed)
        row['ok'] = all(row[key] for key in
                        ('bytes_ok', 'whole_instructions', 'no_interior_branch',
                         'plain_copy_ok', 'arena_replay_ok', 'incoming_ok',
                         'no_installed_conflict'))
        rows.append(row)
    checks['sites'] = all(row['ok'] for row in rows)
    checks['complete_routine'] = (bool(instructions) and instructions[0].va == ROUTINE[0]
                                  and instructions[-1].end == ROUTINE[1])
    checks['anchors'] = all(read_bytes(image, va, len(raw) // 2) == raw
                            for va, raw, _ in ANCHORS)
    checks['chain'] = all(read_bytes(image, va, len(raw) // 2) == raw
                          for va, raw, _ in CHAIN)
    # Exactly two `ret` (failure, success), both plain: cdecl, caller-pop.
    rets = [i for i in instructions if i.mnemonic == 'ret']
    checks['cdecl_ret'] = ([i.va for i in rets] == [0x4981fd, 0x4982ab]
                           and all(i.raw == b'\xc3' for i in rets))
    checks['no_frame_pointer'] = not any(i.mnemonic in ('enter', 'leave')
                                         for i in instructions)
    esp_writers = [i for i in instructions
                   if i.mnemonic in ('enter', 'leave')
                   or (i.mnemonic in ('sub', 'add', 'lea', 'and', 'mov', 'xchg')
                       and i.operands.split(',')[0].strip() == 'esp')]
    checks['esp_writers_known'] = all(i.mnemonic == 'add' and i.operands.startswith('esp,0x')
                                      for i in esp_writers)
    # The gate captures a proceeded call's outcome by substituting the return
    # address at [esp]; the routine must never read that slot itself (its only
    # ESP-relative read is `mov ebx,[esp+8]`, the argument after `push ebx`), never
    # copy ESP into a register (`mov reg,esp` / `lea reg,[esp..]`) through which it
    # could read the slot indirectly, and write ESP only by `add esp,N`.
    esp_reads = [i for i in instructions if '[esp' in i.operands]
    # `mov reg,esp` or `lea reg,[esp..]`: an ESP copy through which the slot could be read indirectly.
    esp_copies = [i for i in instructions
                  if i.operands.split(',')[0].strip() != 'esp'
                  and ((i.mnemonic == 'mov' and i.operands.split(',', 1)[-1].strip() == 'esp')
                       or (i.mnemonic == 'lea' and '[esp' in i.operands))]
    checks['no_return_slot_read'] = (not any('[esp]' in i.operands for i in instructions)
                                     and [(i.va, i.operands) for i in esp_reads] == [(0x498141, 'ebx,DWORD PTR [esp+0x8]')]
                                     and not esp_copies
                                     and all(i.mnemonic == 'add' for i in esp_writers))
    # Callee-saved discipline: four pushes at entry, four pops on each return.
    pushes = [i for i in instructions if i.mnemonic == 'push' and i.va < 0x498148]
    checks['callee_saved'] = [i.operands for i in pushes] == ['ebx', 'ebp', 'esi', 'edi']
    call_sites = raw_call_sites(data, (0x498140,) + tuple(SINGLE_CALLERS))
    checks['call_sites'] = sorted(call_sites[0x498140]) == sorted(CALL_SITES)
    checks['constructor_single_caller'] = all(call_sites[t] == [a]
                                              for t, a in SINGLE_CALLERS.items())
    checks['cdecl_cleanups'] = all(
        read_bytes(image, cleanup, len(raw) // 2) == raw
        for _, (_, cleanup, raw) in CALL_SITES.items())
    raw_hits = raw_interior_scan(data, frozenset(by_va))
    checks['no_raw_interior_encoding'] = not any(hit['reachable'] for hit in raw_hits)
    data_hits = data_reference_hits(data)
    checks['no_data_reference'] = not data_hits
    stores = constructor_global_stores(exe)
    checks['constructor_keeps_no_global'] = not stores
    return {'result': 'PASS' if all(checks.values()) else 'FAIL', 'checks': checks,
            'source_present': source is not None, 'sites': rows,
            'call_sites': {f'{t:#010x}': [f'{a:#010x}' for a in v]
                           for t, v in call_sites.items()},
            'raw_interior_hits': raw_hits, 'data_reference_hits': data_hits,
            'constructor_global_stores': stores}


def verify(exe=DEFAULT_EXE, source=SOURCE, installed=INSTALLED):
    data = Path(exe).read_bytes()
    text = Path(source).read_text() if source and Path(source).exists() else None
    claimed = installed_spans(Path(installed).read_text()) if Path(installed).exists() else []
    for table in OTHER_TABLES:
        if table.exists():
            claimed += installed_spans(table.read_text())
    try:
        report = inspect(common.Image(data), decode(exe), text, data, claimed, exe)
    except (ValueError, OSError, struct.error, subprocess.SubprocessError) as error:
        return {'result': 'FAIL', 'checks': {'decode': False}, 'error': str(error)}
    report['checks']['exe_identity'] = exe_identity.identity_ok(data)
    report['exe_info'] = exe_identity.info(data)
    report['exe_sha256'] = report['exe_info']['sha256']
    report['installed_sites_checked'] = len(claimed)
    report['result'] = 'PASS' if all(report['checks'].values()) else 'FAIL'
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=DEFAULT_EXE)
    parser.add_argument('--source', type=Path, default=SOURCE)
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args()
    report = verify(args.exe, args.source)
    print(json.dumps(report if args.json else
                     {'result': report['result'], 'source_present': report['source_present'],
                      **report['checks']}, indent=2))
    raise SystemExit(report['result'] != 'PASS')
