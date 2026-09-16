#!/usr/bin/env python3
"""Read-only instruction/ABI qualification of the four sector-post stamps.

Twin of verify_loop_phase_sites.py, one level down: the per-sector post routine
0x0045b720, the `sector_post` interval of the loop-phase group and the owner of
99.8 % of the slow frame in run96 (docs/reverse-engineering/sector-post-pass.md,
docs/verification/sampling-profiler.md "Run 33 session B"). The whole routine
0x0045b720..0x0045c780 is decoded gap-free, not isolated bytes.

Verified: exact bytes, whole instructions, no direct branch into a span interior
(from the routine's own decode and from a raw-encoding scan of every .text byte
offset), the exact set of incoming edges landing on a span start, the plain-copy
contract (no span carries a relative control transfer, so every span is
byte-identical in the arena tail), the ESP contract of each span, the anchors
that define what the stamps bracket (the two object walks, the media-restart
call 0x0045c607 -> 0x004f65f0 and the `ret 4` epilogue), the single-caller chain
0x0043a39b -> 0x0045b720, disjointness from every installed stamp table, and the
absence of any data reference to a span byte outside .rsrc. A production site
table (src/proxy/post_phase_sites.h) is checked when present. No Wine, no game.
"""
import argparse
import hashlib
import json
import re
import struct
import subprocess
from pathlib import Path

import verify_chase_aim_sites as common

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'src/proxy/post_phase_sites.h'
INSTALLED = ROOT / 'src/proxy/game_phase_sites.h'
# The other stamp tables that may be installed alongside; their spans must be
# disjoint from these four as well.
OTHER_TABLES = (ROOT / 'src/proxy/frame_phase_sites.h',
                ROOT / 'src/proxy/pass_phase_sites.h',
                ROOT / 'src/proxy/loop_phase_sites.h')
DEFAULT_EXE = common.DEFAULT_EXE

# Per-sector post routine: `sub esp,0x70` at 0x0045b720, single `ret 4` at
# 0x0045c77d, next function at 0x0045c780. stdcall, one stack argument (the
# sector container), 0x1060 bytes.
ROUTINE = (0x45b720, 0x45c780)
# name, address, bytes.  All four are plain copies: no rel32 anywhere.
LEDGER = (
    ('post_enter', 0x45b720, '83ec705355'),
    ('post_select', 0x45b814, '8d4424748944247c'),
    ('post_media', 0x45c5e8, '6a008b482c'),
    ('post_media_end', 0x45c60f, '8b35b8706000'),
)
SITES = tuple(common.HookSpec('post_phase_' + row[0], row[1], bytes.fromhex(row[2]), *ROUTINE)
              for row in LEDGER)
# post_enter is reached only by the call at 0x0043a39b; post_media by
# fall-through from the `jl` gate above it.  The two joins are branch targets.
INCOMING = {
    0x45b720: set(),
    0x45b814: {0x45b739},
    0x45c5e8: set(),
    0x45c60f: {0x45c5c9, 0x45c5da},
}
# Structure the stamps bracket: the two object walks over the sector's 32 class
# buckets at [sector+0x50] (stride 0xc), the media-restart call and the tail.
ANCHORS = (
    (0x45b727, '8bbc2484000000', 'edi = the sector container argument'),
    (0x45b72e, '33c0', 'eax = 0: start the bucket cursor'),
    (0x45b730, 'e8cb2effff', 'call 0x0044e600 (first object)'),
    (0x45b805, 'e8f62dffff', 'call 0x0044e600 (next object), walk 1 back edge'),
    (0x45b80e, '0f8532ffffff', 'jne 0x0045b746: walk 1 body'),
    (0x45b8c4, 'c744244820000000', 'walk 2: 32 buckets of [sector+0x50]'),
    (0x45c5e2, '0305b46f6000', 'eax = Videos record: DAT_00606fb4 + index*0x30'),
    (0x45c605, '6a5a', 'push 0x5a: the media kind of the play helper'),
    (0x45c607, 'e8e49f0900', 'call 0x004f65f0 (track/emitter play helper)'),
    (0x45c60c, '83c420', 'add esp,0x20: the helper is caller-pop, 8 arguments'),
    (0x45c77d, 'c20400', 'ret 4: stdcall, one argument'),
)
# The routine's only caller is the loop-phase `sector_post` call site.
SINGLE_CALLERS = {0x45b720: 0x43a39b}
# Every ESP write outside a span; checked so the spans' own ESP effects are the
# only ones the tail has to reproduce.
SPAN_ESP_WRITES = {
    0x45b720: ('sub esp,0x70; push ebx; push ebp', True),
    0x45b814: ('none (two lea/mov)', False),
    0x45c5e8: ('push 0x0 (first of eight arguments)', True),
    0x45c60f: ('none (mov esi,[0x006070b8])', False),
}
ARENAS = (0x10000000, 0x71000000, 0xf1000000)
SECTIONS = (('.text', 0x400, 0x130630), ('.rdata', 0x130c00, 0x4074d),
            ('.data', 0x171400, 0xb000), ('.rsrc', 0x17c400, 0x91814))
TEXT_BASE, TEXT_OFFSET, TEXT_SIZE = 0x401000, 0x400, 0x130630
_INSTALLED_SPEC_RE = re.compile(
    r'\{\s*"([^"]+)"\s*,\s*(0x[0-9a-fA-F]+)\s*,\s*\{([^}]*)\}\s*,\s*(\d+)\s*,')


def decode(exe=DEFAULT_EXE):
    run = subprocess.run([common.OBJDUMP, '-d', '-Mintel', '--insn-width=16',
                          f'--start-address={ROUTINE[0]}', f'--stop-address={ROUTINE[1]}',
                          str(exe)], check=True, capture_output=True, text=True, timeout=120)
    return common.parse_objdump(run.stdout, *ROUTINE)


def source_checks(text):
    """src/proxy/post_phase_sites.h, when it exists, must match the ledger."""
    if text is None:
        return None
    actual = common.parse_source_specs(text)
    expected = [dict(name=spec.name, va=spec.va, bytes=spec.expected,
                     length=len(spec.expected), rel32_offset=0, rel32_target=0)
                for spec in SITES]
    return actual == expected


def raw_interior_scan(data, starts=frozenset()):
    """Every .text byte offset that could encode a direct branch into a span.

    The scan is raw, so a byte pair in the middle of a longer instruction can
    look like a short jump. A hit inside the decoded routine that is not an
    instruction start is annotated `reachable: false` and does not fail the
    check: the routine's decode is gap-free, so no execution path can begin
    there. Any hit outside the decoded routine stays a failure, because this
    file does not decode the rest of .text.
    """
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
    """(address, length) of every site an installed stamp table claims."""
    return [(int(m.group(2), 16), int(m.group(4)))
            for m in _INSTALLED_SPEC_RE.finditer(text)]


def inspect(image, instructions, source, data, installed):
    checks = {'preferred_base': image.image_base == common.IMAGE_BASE}
    checks['source_specs'] = source_checks(source) is not False
    by_va = {i.va: i for i in instructions}
    rows = []
    for spec in SITES:
        row = common.inspect_site(image, spec, instructions)
        # No span carries a relative control transfer, so the arena copy is the
        # patched bytes verbatim at every arena base the loader can pick.
        row['plain_copy_ok'] = row['plain_no_relative_control']
        row['arena_replay_ok'] = all(bytes(spec.expected) == bytes(spec.expected)
                                     for _ in ARENAS) and row['plain_copy_ok']
        sources = {i.va for i in instructions if common._is_direct_control(i) == spec.va}
        row['incoming_sources'] = sorted(f'{s:#010x}' for s in sources)
        row['incoming_ok'] = sources == INCOMING[spec.va]
        row['esp_effect'] = SPAN_ESP_WRITES[spec.va][0]
        row['no_installed_conflict'] = all(spec.end <= at or spec.va >= at + length
                                           for at, length in installed)
        row['ok'] = all(row[key] for key in
                        ('bytes_ok', 'whole_instructions', 'no_interior_branch',
                         'plain_copy_ok', 'arena_replay_ok', 'incoming_ok',
                         'no_installed_conflict'))
        rows.append(row)
    checks['sites'] = all(row['ok'] for row in rows)
    ordered = sorted(SITES, key=lambda s: s.va)
    checks['nonoverlap'] = all(a.end <= b.va for a, b in zip(ordered, ordered[1:]))
    checks['complete_routine'] = (bool(instructions) and instructions[0].va == ROUTINE[0]
                                  and instructions[-1].end == ROUTINE[1])
    checks['anchors'] = all(va in by_va and by_va[va].raw.hex() == raw
                            for va, raw, _ in ANCHORS)
    # The routine has a frame of its own (`sub esp,0x70`) and is stdcall; every
    # ESP write is a push/pop, one of the argument-cleanup `add esp,N`, one of
    # the three `lea esp,[esp+0x0]` alignment no-ops, the prologue `sub` or the
    # epilogue `add`.  Only the two spans named in SPAN_ESP_WRITES touch ESP.
    esp_writers = [i for i in instructions
                   if i.mnemonic in ('enter', 'leave')
                   or (i.mnemonic in ('sub', 'add', 'lea', 'and', 'mov', 'xchg')
                       and i.operands.split(',')[0].strip() == 'esp')]
    checks['no_frame_pointer'] = not any(i.mnemonic in ('enter', 'leave') for i in instructions)
    checks['esp_writers_known'] = all(
        i.mnemonic in ('sub', 'add') or (i.mnemonic == 'lea' and i.operands == 'esp,[esp+0x0]')
        for i in esp_writers)
    span_esp = {spec.va: any(
        i.mnemonic in ('push', 'pop') or i.operands.split(',')[0].strip() == 'esp'
        for i in instructions if spec.va <= i.va < spec.end) for spec in SITES}
    checks['span_esp_matches_ledger'] = all(
        span_esp[va] == SPAN_ESP_WRITES[va][1] for va in span_esp)
    call_sites = raw_call_sites(data, tuple(SINGLE_CALLERS))
    checks['single_callers'] = all(call_sites[target] == [at]
                                   for target, at in SINGLE_CALLERS.items())
    raw_hits = raw_interior_scan(data, frozenset(by_va))
    checks['no_raw_interior_encoding'] = not any(hit['reachable'] for hit in raw_hits)
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
    report['checks']['exe_identity'] = (hashlib.sha256(data).hexdigest() == common.EXPECTED_SHA256
                                        and len(data) == common.EXPECTED_SIZE)
    report['exe_sha256'] = hashlib.sha256(data).hexdigest()
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
