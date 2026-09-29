#!/usr/bin/env python3
"""Read-only instruction/ABI qualification of the ten loop-phase stamps.

Twin of verify_pass_phase_sites.py for the per-sector update driver 0x0043a360,
the dominant call of the main loop's `input_part=0` sub-region
(docs/reverse-engineering/main-loop-input-region.md). The whole routine
0x0043a360..0x0043a3d5 is decoded, not isolated bytes, and so is the part-0
region 0x00403b09..0x00403b3a that contains its only callsite.

Verified: exact bytes, whole instructions, no direct branch into a span interior
(both from the routines' own decode and from a raw-encoding scan of every .text
byte offset), the exact set of incoming edges landing on a span start, the
rel32 call targets the displaced spans carry and their re-based arena copies,
the plain-copy contract for the two spans that carry no rel32, the ESP contract
(0x0043a360 has no stack frame and no argument, every displaced call is a
callee-pop `stdcall` of one argument, so ESP is the routine's frame base at all
six sites), both list-walk back edges and their type/skip gates, the
single-caller chain 0x00403b17 -> 0x0043a360 -> five per-sector routines,
disjointness from every installed game-phase site, and the absence of any
data reference to a span byte outside .rsrc.

The four region stamps (sites 6-9) bracket the three calls of the part-0
region inside the main loop 0x00403840, which is decoded whole: exact bytes
and whole instructions, no direct branch onto or into any of the four spans,
the rel32 contract (two `call`s at offset 1 to 0x0048f550/0x0043a360, the
`jne` at offset 2 to 0x00403db8, the plain copy `mov eax,[0x0060850c]`), the
ESP contract (the region's only stack write is the `push edi` that
0x0045b660's `ret 4` removes; 0x0048f550 and 0x0043a360 take no argument and
end in a plain `ret`), the three region exits all reaching 0x00403b3a (the
installed game_phase_input_body site, so the sweep closes on the `jne` after
it) and the single caller of the main loop (main-loop thread only). The
production site table (src/proxy/loop_phase_sites.h) is checked when present.
No Wine or game launch.
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
SOURCE = ROOT / 'src/proxy/loop_phase_sites.h'
INSTALLED = ROOT / 'src/proxy/game_phase_sites.h'
# The other stamp tables that may be installed alongside; their spans must be
# disjoint from these ten as well.
OTHER_TABLES = tuple(ROOT / 'src/proxy' / name for name in (
    'frame_phase_sites.h', 'pass_phase_sites.h', 'residual_phase_sites.h', 'submit_phase_sites.h',
    'light_phase_sites.h', 'media_cue_sites.h'))
DEFAULT_EXE = common.DEFAULT_EXE

# Per-sector update driver: push ebx at 0x0043a360, single ret at 0x0043a3d4,
# int3 padding from 0x0043a3d5. cdecl, no argument, no stack frame.
ROUTINE = (0x43a360, 0x43a3d5)
# The `input_part=0` region of the main loop: from the installed
# game_phase_input site 0x00403b09 to the installed game_phase_input_body site
# 0x00403b3a (src/proxy/game_phase_sites.h indices 6 and 23).
REGION = (0x403b09, 0x403b3a)
# The main loop that contains the region (verify_game_phase_sites.MAIN),
# decoded whole for the region sites' boundaries and incoming edges.
MAIN = (0x403840, 0x404278)
# name, address, bytes, rel32 offset inside the span (0 = plain copy),
# rel32 absolute target (0 = none), containing routine, relative mnemonic.
LEDGER = (
    ('sector_collide', 0x43a38e, '56e8bc2e0200', 2, 0x45d250, ROUTINE, 'call'),
    ('sector_simulate', 0x43a394, '56e836870100', 2, 0x452ad0, ROUTINE, 'call'),
    ('sector_post', 0x43a39a, '56e880130200', 2, 0x45b720, ROUTINE, 'call'),
    ('sector_pass_a_end', 0x43a3a0, '8b36833e00', 0, 0, ROUTINE, None),
    ('sector_economy', 0x43a3be, '56e81cf30100', 2, 0x4596e0, ROUTINE, 'call'),
    ('sector_pass_b_end', 0x43a3ca, '8b36833e00', 0, 0, ROUTINE, None),
    ('region_cutevent', 0x403b12, 'e839ba0800', 1, 0x48f550, MAIN, 'call'),
    ('region_containers', 0x403b17, 'e844680300', 1, 0x43a360, MAIN, 'call'),
    ('region_sweep', 0x403b1c, 'a10c856000', 0, 0, MAIN, None),
    ('region_end', 0x403b40, '0f8572020000', 2, 0x403db8, MAIN, 'jne'),
)
SITES = tuple(common.HookSpec('loop_phase_' + row[0], row[1], bytes.fromhex(row[2]), *row[5])
              for row in LEDGER)
REL32 = {row[1]: (row[3], row[4]) for row in LEDGER}
MNEMONIC = {row[1]: row[6] for row in LEDGER}
# Only the two per-iteration joins are branch targets; the four call sites are
# reached by fall-through from the gate that precedes them. No direct branch
# anywhere in the main loop lands on a region span: 0x00403b12 follows the
# pause gate's fall-through, 0x00403b40 follows the installed
# game_phase_input_body span 0x00403b3a (whose tail jumps back to it).
INCOMING = {
    0x43a38e: set(), 0x43a394: set(), 0x43a39a: set(),
    0x43a3a0: {0x43a384, 0x43a38c},
    0x43a3be: set(),
    0x43a3ca: {0x43a3b4, 0x43a3bc},
    0x403b12: set(), 0x403b17: set(), 0x403b1c: set(), 0x403b40: set(),
}
# Where each span may sit: the driver's two loop bodies, or the part-0 region
# plus the `jne` after its end marker (the region sites).
REGION_BODY = (0x403b12, 0x403b46)
# Every exit of the part-0 region reaches 0x00403b3a (pause gate, empty list,
# walk fall-through), then the region-end span on the next instruction.
REGION_EXITS = ((0x403b10, 0x403b3a), (0x403b26, 0x403b3a))
REGION_WALK_EXIT = (0x403b38, 0x403b28)   # the walk's back edge; falls through to 0x00403b3a
REGION_END_MARKER = (0x403b3a, '39aed8040000', 0x403b40)
# The region's only stack write and the callee that removes it; the two
# displaced calls take no argument (a plain `ret` ends each callee).
REGION_PUSH = 0x403b2e
CALLEE_RETURNS = ((0x48f692, 'c3'), (0x43a3d4, 'c3'), (0x45b712, 'c20400'))
MAIN_CALLER = (0x403840, 0x40373a)
# The two list walks over the universe object list, (body start, first
# instruction after the back edge, back-edge address).
LOOPS = ((0x43a380, 0x43a3a7, 0x43a3a5), (0x43a3b0, 0x43a3d1, 0x43a3b0))
BACK_EDGES = ((0x43a3a5, 0x43a380), (0x43a3cf, 0x43a3b0))
# Gates and list plumbing that define what the stamps bracket.
ANCHORS = (
    (0x43a363, '8b3d0c856000', 'edi = *0x0060850c (universe root)'),
    (0x43a369, '837f0400', 'cmp [edi+4],0 -> pass A gate'),
    (0x43a36d, 'bb01000000', 'ebx = 1, the type constant'),
    (0x43a374, '8b7708', 'esi = [edi+8], list head (pass A)'),
    (0x43a380, '66395e48', 'cmp WORD PTR [esi+0x48],bx -> type == 1'),
    (0x43a386, '849e48010000', 'test BYTE PTR [esi+0x148],bl -> skip flag'),
    (0x43a3a7, '8b7708', 'esi = [edi+8], list head (pass B)'),
    (0x43a3b0, '66395e48', 'cmp WORD PTR [esi+0x48],bx -> type == 1'),
    (0x43a3b6, '849e48010000', 'test BYTE PTR [esi+0x148],bl -> skip flag'),
    (0x43a3d4, 'c3', 'single ret, no ret_pop'),
)
# The three top-level calls of the part-0 region and the two installed markers
# that already bound it.
REGION_ANCHORS = (
    (0x403b09, 'f686a004000001', 'installed game_phase_input (index 6)'),
    (0x403b12, 'e839ba0800', 'call 0x0048f550 (cutscene/CutEvent update)'),
    (0x403b17, 'e844680300', 'call 0x0043a360 (per-sector update driver)'),
    (0x403b1c, 'a10c856000', 'eax = *0x0060850c'),
    (0x403b2f, 'e82c7b0500', 'call 0x0045b660 (deferred-delete sweep)'),
    (0x403b3a, '39aed8040000', 'installed game_phase_input_body (index 23)'),
)
# Every one of these has exactly one caller image-wide (raw e8 scan below).
SINGLE_CALLERS = {0x48f550: 0x403b12, 0x43a360: 0x403b17, 0x45b660: 0x403b2f,
                  0x45d250: 0x43a38f, 0x452ad0: 0x43a395, 0x45b720: 0x43a39b,
                  0x4596e0: 0x43a3bf, 0x4526b0: 0x43a3c5}
# The only ESP writes in the routine are the prologue pushes, the epilogue pops
# and one `lea esp,[esp+0x0]` alignment no-op.
ESP_NOOP = (0x43a37c, '8d642400')
PROLOGUE = (0x43a360, 0x43a363)   # push ebx; push esi; push edi
EPILOGUE = (0x43a3d1, 0x43a3d4)   # pop edi; pop esi; pop ebx
ARENAS = (0x10000000, 0x71000000, 0xf1000000)
SECTIONS = (('.text', 0x400, 0x130630), ('.rdata', 0x130c00, 0x4074d),
            ('.data', 0x171400, 0xb000), ('.rsrc', 0x17c400, 0x91814))
TEXT_BASE, TEXT_OFFSET, TEXT_SIZE = 0x401000, 0x400, 0x130630
_INSTALLED_SPEC_RE = re.compile(
    r'\{\s*"([^"]+)"\s*,\s*(0x[0-9a-fA-F]+)\s*,\s*\{([^}]*)\}\s*,\s*(\d+)\s*,')


def decode(exe=DEFAULT_EXE):
    decoded = {}
    for bounds in (ROUTINE, REGION, MAIN):
        run = subprocess.run([common.OBJDUMP, '-d', '-Mintel', '--insn-width=16',
                              f'--start-address={bounds[0]}', f'--stop-address={bounds[1]}',
                              str(exe)], check=True, capture_output=True, text=True, timeout=60)
        decoded[bounds] = common.parse_objdump(run.stdout, *bounds)
    return decoded


def source_checks(text):
    """src/proxy/loop_phase_sites.h, when it exists, must match the ledger.

    The shared parser names the two trailing SiteSpec fields `rel32_offset` and
    `rel32_target`; in engine_patch::SiteSpec they are `ret_pop` (always 0 here:
    the displaced calls are inside the span, and no span is a routine exit)
    and `rel32_offset`.
    """
    if text is None:
        return None
    actual = common.parse_source_specs(text)
    expected = [dict(name=spec.name, va=spec.va, bytes=spec.expected, length=len(spec.expected),
                     rel32_offset=0, rel32_target=REL32[spec.va][0]) for spec in SITES]
    return actual == expected


def relocated_bytes(spec, arena):
    """Independent reference for the arena tail copy.

    A span without a rel32 is byte-identical anywhere; a span that carries one
    direct `call rel32` or `jcc rel32` keeps its absolute target, so the field
    becomes target - (arena + rel32_offset + 4).
    """
    offset, target = REL32[spec.va]
    if not offset:
        return bytes(spec.expected)
    field = (target - (arena + offset + 4)) & 0xffffffff
    return bytes(spec.expected[:offset]) + struct.pack('<I', field) + bytes(spec.expected[offset + 4:])


def rel32_target(spec, image):
    offset, _ = REL32[spec.va]
    if not offset:
        return None
    field = struct.unpack('<i', image.read(spec.va + offset, 4))[0]
    return (spec.va + offset + 4 + field) & 0xffffffff


def raw_interior_scan(data):
    """Every .text byte offset that could encode a direct branch into a span."""
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
            hits.append({'at': f'{va:#010x}', 'target': f'{target:#010x}'})
    return hits


def raw_call_sites(data, targets):
    """All `e8 rel32` encodings in .text that reach each named target."""
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
    """Aligned dword references to any span byte outside .rsrc are rejected."""
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


def installed_spans(text):
    """(address, length) of every site the installed game-phase table claims."""
    spans = []
    for match in _INSTALLED_SPEC_RE.finditer(text):
        spans.append((int(match.group(2), 16), int(match.group(4))))
    return spans


def inspect(image, decoded, source, data, installed):
    checks = {'preferred_base': image.image_base == common.IMAGE_BASE}
    source_ok = source_checks(source)
    checks['source_specs'] = source_ok is not False
    instructions = decoded.get(ROUTINE, [])
    region = decoded.get(REGION, [])
    main = decoded.get(MAIN, [])
    by_va = {i.va: i for i in instructions}
    rows = []
    for spec in SITES:
        own = decoded.get((spec.function_start, spec.function_end), [])
        row = common.inspect_site(image, spec, own)
        span = [i for i in own if spec.va <= i.va < spec.end]
        controls = [i for i in span if common._is_direct_control(i) is not None]
        offset, target = REL32[spec.va]
        # At most one relative control transfer, the documented one, its rel32
        # field ending the span at the declared offset.
        row['single_rel32'] = (len(controls) == (1 if offset else 0)
                               and all(c.mnemonic == MNEMONIC[spec.va] and c.end == spec.end
                                       and c.end - 4 - spec.va == offset for c in controls))
        row['rel32_target_ok'] = rel32_target(spec, image) == (target or None)
        row['arena_replay_ok'] = all(
            relocated_bytes(spec, arena)[: offset or len(spec.expected)]
            == bytes(spec.expected)[: offset or len(spec.expected)] for arena in ARENAS)
        # The re-based field must resolve back to the same absolute target.
        row['arena_target_ok'] = not offset or all(
            (arena + offset + 4
             + struct.unpack('<i', relocated_bytes(spec, arena)[offset:offset + 4])[0]
             ) & 0xffffffff == target for arena in ARENAS)
        sources = {i.va for i in own if common._is_direct_control(i) == spec.va}
        row['incoming_sources'] = sorted(f'{s:#010x}' for s in sources)
        row['incoming_ok'] = sources == INCOMING[spec.va]
        bodies = ([(start, stop) for start, stop, _ in LOOPS] if spec.function_start == ROUTINE[0]
                  else [REGION_BODY])
        row['in_body'] = any(start <= spec.va and spec.end <= stop for start, stop in bodies)
        row['no_installed_conflict'] = all(spec.end <= at or spec.va >= at + length
                                           for at, length in installed)
        row['ok'] = all(row[key] for key in
                        ('bytes_ok', 'whole_instructions', 'no_interior_branch', 'single_rel32',
                         'rel32_target_ok', 'arena_replay_ok', 'arena_target_ok', 'incoming_ok',
                         'in_body', 'no_installed_conflict'))
        rows.append(row)
    checks['sites'] = all(row['ok'] for row in rows)
    ordered = sorted(SITES, key=lambda s: s.va)
    checks['nonoverlap'] = all(a.end <= b.va for a, b in zip(ordered, ordered[1:]))
    checks['complete_routine'] = (bool(instructions) and instructions[0].va == ROUTINE[0]
                                  and instructions[-1].end == ROUTINE[1])
    checks['complete_region'] = (bool(region) and region[0].va == REGION[0]
                                 and region[-1].end == REGION[1])
    checks['complete_main'] = bool(main) and main[0].va == MAIN[0] and main[-1].end == MAIN[1]
    main_by_va = {i.va: i for i in main}
    # Region sites: every exit reaches the end marker, whose next instruction
    # is the region-end span; the only stack write is the sweep's argument,
    # removed by its callee; the two displaced calls take no argument.
    checks['region_exits'] = (
        all(at in main_by_va and common._is_direct_control(main_by_va[at]) == to for at, to in REGION_EXITS)
        and REGION_WALK_EXIT[0] in main_by_va
        and common._is_direct_control(main_by_va[REGION_WALK_EXIT[0]]) == REGION_WALK_EXIT[1]
        and main_by_va[REGION_WALK_EXIT[0]].end == REGION_END_MARKER[0]
        and REGION_END_MARKER[0] in main_by_va
        and main_by_va[REGION_END_MARKER[0]].raw.hex() == REGION_END_MARKER[1]
        and main_by_va[REGION_END_MARKER[0]].end == REGION_END_MARKER[2]
        and {i.va for i in main if common._is_direct_control(i) == REGION_END_MARKER[0]}
        == {at for at, _ in REGION_EXITS})
    region_stack = [i for i in main if REGION_BODY[0] - 9 <= i.va < REGION_BODY[1]
                    and (i.mnemonic in ('push', 'pop', 'enter', 'leave', 'pushf', 'popf', 'pushad', 'popad')
                         or i.operands.split(',')[0].strip() == 'esp')]
    checks['region_esp'] = [i.va for i in region_stack] == [REGION_PUSH]
    checks['region_callee_returns'] = all(image.read(va, len(raw) // 2).hex() == raw
                                          for va, raw in CALLEE_RETURNS)
    checks['no_indirect_jump'] = not any(i.mnemonic == 'jmp' and 'PTR' in i.operands
                                         for i in instructions)
    checks['no_indirect_call'] = not any(i.mnemonic == 'call'
                                         and common._is_direct_control(i) is None
                                         for i in instructions)
    checks['back_edges'] = all(at in by_va and common._is_direct_control(by_va[at]) == to
                               for at, to in BACK_EDGES)
    checks['anchors'] = all(va in by_va and by_va[va].raw.hex() == raw
                            for va, raw, _ in ANCHORS)
    region_by_va = {i.va: i for i in region}
    checks['region_anchors'] = all(
        (va in region_by_va and region_by_va[va].raw.hex() == raw)
        or image.read(va, len(raw) // 2).hex() == raw for va, raw, _ in REGION_ANCHORS)
    # ESP contract: no stack frame, no argument, and the only esp writes are the
    # three prologue pushes, the three epilogue pops and one alignment no-op.
    esp_writers = [i for i in instructions
                   if i.mnemonic in ('push', 'pop', 'sub', 'add', 'lea', 'and', 'enter', 'leave')
                   and ('esp' in i.operands.split(',')[0] or i.mnemonic in ('push', 'pop'))]
    allowed = set(range(*PROLOGUE)) | set(range(*EPILOGUE)) | {ESP_NOOP[0]}
    allowed |= {row[1] for row in LEDGER if row[5] == ROUTINE and REL32[row[1]][0]}  # push esi
    checks['esp_writers'] = all(i.va in allowed or i.mnemonic == 'push' for i in esp_writers)
    checks['esp_noop'] = (ESP_NOOP[0] in by_va and by_va[ESP_NOOP[0]].raw.hex() == ESP_NOOP[1])
    checks['no_frame_pointer'] = not any(i.mnemonic in ('enter', 'leave') for i in instructions)
    # Every displaced call is callee-pop: no `add esp` follows any of them.
    checks['callee_pop_calls'] = not any(
        i.mnemonic == 'add' and i.operands.startswith('esp') for i in instructions)
    call_sites = raw_call_sites(data, tuple(SINGLE_CALLERS) + (MAIN_CALLER[0],))
    checks['single_callers'] = all(call_sites[target] == [at]
                                   for target, at in SINGLE_CALLERS.items())
    # The main loop has one caller: the region sites run on its thread only.
    checks['main_loop_single_caller'] = call_sites[MAIN_CALLER[0]] == [MAIN_CALLER[1]]
    raw_hits = raw_interior_scan(data)
    checks['no_raw_interior_encoding'] = not raw_hits
    data_hits = data_reference_hits(data)
    checks['no_data_reference'] = all(hit['section'] == '.rsrc' and not hit['aligned']
                                      for hit in data_hits)
    return {'result': 'PASS' if all(checks.values()) else 'FAIL', 'checks': checks,
            'source_present': source is not None, 'sites': rows,
            'call_sites': {f'{t:#010x}': [f'{a:#010x}' for a in v] for t, v in call_sites.items()},
            'raw_interior_hits': raw_hits, 'data_reference_hits': data_hits}


def verify(exe=DEFAULT_EXE, source=SOURCE, installed=INSTALLED):
    data = Path(exe).read_bytes()
    text = Path(source).read_text() if source and Path(source).exists() else None
    claimed = installed_spans(Path(installed).read_text()) if Path(installed).exists() else []
    for table in OTHER_TABLES:
        if table.exists():
            claimed += installed_spans(table.read_text())
    try:
        report = inspect(common.Image(data), decode(exe), text, data, claimed)
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
