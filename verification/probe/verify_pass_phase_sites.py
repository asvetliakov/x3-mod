#!/usr/bin/env python3
"""Read-only instruction/ABI qualification of the four effect-pass stamps.

Twin of verify_frame_phase_sites.py for the D3DX pass loop inside the material
submission routine 0x004c0150 (docs/reverse-engineering/effect-pass-loop.md).
The whole routine 0x004c0150..0x004c40fc is decoded, not isolated bytes.
Verified: exact bytes, whole instructions, no direct branch into a span
interior (both from the routine's own decode and from a raw-encoding scan of
every .text byte offset), the exact set of incoming edges landing on a span
start, the plain-copy contract (no relative control transfer in any span, so
the arena tail replays byte-identically at any address), the frame-depth
anchors that prove ESP is at the routine's frame base at all four sites, the
ID3DXEffect/IDirect3DDevice9 vtable displacements the loop dispatches, the
loop's single back edge, disjointness from the point-light admission patch,
and the absence of any data reference to a span byte outside .rsrc. The
production site table (src/proxy/pass_phase_sites.h) is checked when present.
No Wine or game launch.
"""
import argparse
import hashlib
import json
import struct
import subprocess
from pathlib import Path

import verify_chase_aim_sites as common

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'src/proxy/pass_phase_sites.h'
DEFAULT_EXE = common.DEFAULT_EXE
# Material submission routine: push ebp at 0x004c0150, last ret at 0x004c40fb,
# int3 padding from 0x004c40fc (same bounds as point_light_admission_core.h).
ROUTINE = (0x4c0150, 0x4c40fc)
# name, address, bytes; every span is a plain copy (no rel32 field).
LEDGER = (
    ('pass_begin', 0x4c3ff0, '8b4424748b13'),
    ('pass_applied', 0x4c4000, '8b4424288b4814'),
    ('pass_drawn', 0x4c403e, '8b138b8208010000'),
    ('pass_end', 0x4c4049, '8b44247483c001'),
)
SITES = tuple(common.HookSpec('pass_phase_' + row[0], row[1], bytes.fromhex(row[2]), *ROUTINE)
              for row in LEDGER)
# Only the loop back edge lands on a site start; the other three are reached by
# fall-through from the call that precedes them.
INCOMING = {0x4c3ff0: {0x4c405b}, 0x4c4000: set(), 0x4c403e: set(), 0x4c4049: set()}
LOOP = (0x4c3ff0, 0x4c405d)  # body start .. first instruction after the back edge
# Dispatches the loop performs, as (address, expected bytes, meaning).
DISPATCHES = (
    (0x4c1ebe, 'ffd1', 'ID3DXEffect::Begin, slot 63 (+0xfc), loaded at 0x004c1ead'),
    (0x4c3ffe, 'ffd1', 'ID3DXEffect::BeginPass, slot 64 (+0x100), loaded at 0x004c3ff6'),
    (0x4c403c, 'ffd1', 'IDirect3DDevice9::DrawIndexedPrimitive, slot 82 (+0x148)'),
    (0x4c4047, 'ffd0', 'ID3DXEffect::EndPass, slot 66 (+0x108), loaded at 0x004c4040'),
    (0x4c4066, 'ffd2', 'ID3DXEffect::End, slot 67 (+0x10c), loaded at 0x004c405f'),
)
VTABLE_LOADS = (
    (0x4c1ead, '8b8afc000000'),  # mov ecx,[edx+0xfc]   Begin
    (0x4c3ff6, '8b8a00010000'),  # mov ecx,[edx+0x100]  BeginPass
    (0x4c4018, '81c748010000'),  # add edi,0x148        DrawIndexedPrimitive slot
    (0x4c4040, '8b8208010000'),  # mov eax,[edx+0x108]  EndPass
    (0x4c405f, '8b910c010000'),  # mov edx,[ecx+0x10c]  End
)
# Frame-depth anchors: the pass index (frame+0x74) and the pass count
# (frame+0x84) are addressed at the same ESP displacement before the loop,
# between the two calls and after them, so every callee restores ESP and all
# four sites sit at the routine's frame base with no pending argument pushes.
FRAME_ANCHORS = (
    (0x4c3fde, '83bc248400000000'),  # cmp DWORD PTR [esp+0x84],0x0  (pass count)
    (0x4c3fe6, 'c744247400000000'),  # mov DWORD PTR [esp+0x74],0x0  (pass index)
    (0x4c4000, '8b442428'),          # mov eax,[esp+0x28]            (after BeginPass)
    (0x4c4050, '3b842484000000'),    # cmp eax,[esp+0x84]            (after the draw)
    (0x4c4057, '89442474'),          # mov [esp+0x74],eax
)
# The Begin call writes the pass count through `lea eax,[esp+0x88]` one push
# deep, i.e. into the same frame+0x84 slot the loop reads at frame depth.
BEGIN_OUT_PARAM = ((0x4c1eb5, '8d842488000000'), (0x4c1ebc, '50'))
POINT_LIGHT_PATCH = (0x4c27af, 0x4c27b5)  # src/proxy/point_light_admission_core.h
ARENAS = (0x10000000, 0x71000000, 0xf1000000)
SECTIONS = (('.text', 0x400, 0x130630), ('.rdata', 0x130c00, 0x4074d),
            ('.data', 0x171400, 0xb000), ('.rsrc', 0x17c400, 0x91814))
TEXT_BASE, TEXT_OFFSET, TEXT_SIZE = 0x401000, 0x400, 0x130630


def decode(exe=DEFAULT_EXE):
    run = subprocess.run([common.OBJDUMP, '-d', '-Mintel', '--insn-width=16',
                          f'--start-address={ROUTINE[0]}', f'--stop-address={ROUTINE[1]}', str(exe)],
                         check=True, capture_output=True, text=True, timeout=60)
    return {ROUTINE: common.parse_objdump(run.stdout, *ROUTINE)}


def source_checks(text):
    if text is None:
        return None
    actual = common.parse_source_specs(text)
    expected = [dict(name=s.name, va=s.va, bytes=s.expected, length=len(s.expected),
                     rel32_offset=0, rel32_target=0) for s in SITES]
    return actual == expected


def relocated_bytes(spec, arena):
    """Independent reference for the arena tail copy: no rel32 field to re-base,
    so a correct copy of a plain span is byte-identical at any address."""
    return bytes(spec.expected)


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


def inspect(image, decoded, source, data):
    checks = {'preferred_base': image.image_base == common.IMAGE_BASE}
    source_ok = source_checks(source)
    checks['source_specs'] = source_ok is not False
    instructions = decoded.get(ROUTINE, [])
    by_va = {i.va: i for i in instructions}
    rows = []
    for spec in SITES:
        row = common.inspect_site(image, spec, instructions)
        span = [i for i in instructions if spec.va <= i.va < spec.end]
        controls = [i for i in span if common._is_direct_control(i) is not None]
        # Plain copy: the tail replays byte-identically at any arena address.
        row['plain_copy'] = not controls
        row['arena_replay_ok'] = (not controls
                                  and all(relocated_bytes(spec, arena) == image.read(spec.va, len(spec.expected))
                                          for arena in ARENAS))
        sources = {i.va for i in instructions if common._is_direct_control(i) == spec.va}
        row['incoming_sources'] = sorted(f'{s:#010x}' for s in sources)
        row['incoming_ok'] = sources == INCOMING[spec.va]
        row['in_loop_body'] = LOOP[0] <= spec.va and spec.end <= LOOP[1]
        row['ok'] = all(row[k] for k in ('bytes_ok', 'whole_instructions', 'no_interior_branch',
                                         'plain_copy', 'arena_replay_ok', 'incoming_ok',
                                         'in_loop_body'))
        rows.append(row)
    checks['sites'] = all(row['ok'] for row in rows)
    ordered = sorted(SITES, key=lambda s: s.va)
    checks['nonoverlap'] = all(a.end <= b.va for a, b in zip(ordered, ordered[1:]))
    checks['complete_routine'] = (bool(instructions) and instructions[0].va == ROUTINE[0]
                                  and instructions[-1].end == ROUTINE[1])
    checks['no_indirect_jump'] = not any(i.mnemonic == 'jmp' and 'PTR' in i.operands
                                         for i in instructions)
    checks['back_edge'] = (0x4c405b in by_va and by_va[0x4c405b].mnemonic == 'jb'
                           and common._is_direct_control(by_va[0x4c405b]) == 0x4c3ff0
                           and by_va[0x4c405b].end == LOOP[1])
    checks['dispatches'] = all(va in by_va and by_va[va].raw.hex() == raw and by_va[va].mnemonic == 'call'
                               for va, raw, _ in DISPATCHES)
    checks['vtable_loads'] = all(va in by_va and by_va[va].raw.hex() == raw
                                 for va, raw in VTABLE_LOADS)
    checks['frame_anchors'] = all(va in by_va and by_va[va].raw.hex() == raw
                                  for va, raw in FRAME_ANCHORS)
    checks['begin_out_param'] = all(va in by_va and by_va[va].raw.hex() == raw
                                    for va, raw in BEGIN_OUT_PARAM)
    checks['point_light_patch_disjoint'] = all(s.end <= POINT_LIGHT_PATCH[0]
                                               or s.va >= POINT_LIGHT_PATCH[1] for s in SITES)
    raw_hits = raw_interior_scan(data)
    checks['no_raw_interior_encoding'] = not raw_hits
    data_hits = data_reference_hits(data)
    checks['no_data_reference'] = all(hit['section'] == '.rsrc' and not hit['aligned']
                                      for hit in data_hits)
    return {'result': 'PASS' if all(checks.values()) else 'FAIL', 'checks': checks,
            'source_present': source is not None, 'sites': rows,
            'raw_interior_hits': raw_hits, 'data_reference_hits': data_hits}


def verify(exe=DEFAULT_EXE, source=SOURCE):
    data = Path(exe).read_bytes()
    text = Path(source).read_text() if source and Path(source).exists() else None
    try:
        report = inspect(common.Image(data), decode(exe), text, data)
    except (ValueError, OSError, struct.error, subprocess.SubprocessError) as error:
        return {'result': 'FAIL', 'checks': {'decode': False}, 'error': str(error)}
    report['checks']['exe_identity'] = (hashlib.sha256(data).hexdigest() == common.EXPECTED_SHA256
                                        and len(data) == common.EXPECTED_SIZE)
    report['exe_sha256'] = hashlib.sha256(data).hexdigest()
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
