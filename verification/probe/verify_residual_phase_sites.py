#!/usr/bin/env python3
"""Read-only instruction/ABI qualification of the two residual stamps.

Twin of verify_pass_phase_sites.py for the two sites of X3M_RESIDUAL_PHASES
(docs/reverse-engineering/effect-pass-loop.md section 7 and
frame-loop-phases.md section 5d): the ID3DXEffect::Begin dispatch of the
material submission routine 0x004c0150 and the particles-call return in the
frame routine's per-view loop 0x00471f50. Both routines are decoded whole, not
isolated bytes. Verified: exact bytes, whole instructions, no direct branch
into a span interior (from the routines' own decode and from a raw-encoding
scan of every .text byte offset), the exact set of incoming edges landing on
each span start (the four guard/skip edges of the material-initialisation
path onto material_setup; none onto view_particles), the plain-copy contract
(no relative control transfer in either span, so the arena tail replays
byte-identically at any address), the ESP anchors (Begin's `push 1; lea
eax,[esp+0x88]` one push deep writing the pass-count slot the pass loop reads
at frame depth, so material_setup sits at the routine's frame base; the
`push edx; call 0x004bf4c0` whose argument the displaced `add esp,4` removes,
and the `je` that skips the call landing exactly on the span end), the
position of each span inside its loop, disjointness from every installed
game-phase, frame-phase and pass-phase site, the scene hook and the
point-light patch, no indirect jump in either routine, and the absence of any
data reference to a span byte outside .rsrc. The production site table
(src/proxy/residual_phase_sites.h) is checked when present. No Wine or game
launch.
"""
import argparse
import hashlib
import json
import struct
import subprocess
from pathlib import Path

import verify_chase_aim_sites as common
import verify_loop_phase_sites as loop_probe

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'src/proxy/residual_phase_sites.h'
INSTALLED = (ROOT / 'src/proxy/game_phase_sites.h', ROOT / 'src/proxy/frame_phase_sites.h',
             ROOT / 'src/proxy/pass_phase_sites.h')
DEFAULT_EXE = common.DEFAULT_EXE
MATERIAL = (0x4c0150, 0x4c40fc)  # material submission routine (verify_pass_phase_sites.ROUTINE)
FRAME = (0x471f50, 0x47260c)     # frame routine (verify_frame_phase_sites.FRAME)
# name, address, bytes, routine; every span is a plain copy (no rel32 field).
LEDGER = (
    ('material_setup', 0x4c1eab, '8b138b8afc000000', MATERIAL),
    ('view_particles', 0x47230c, '8b151885600083c404', FRAME),
)
SITES = tuple(common.HookSpec('residual_phase_' + row[0], row[1], bytes.fromhex(row[2]), *row[3])
              for row in LEDGER)
# material_setup: the two material-initialisation guards (jne), the
# initialisation path's own exit (jmp) and its parameter-loop skip (je) all
# land on the span start; view_particles is reached only by fall-through from
# the particles call.
INCOMING = {0x4c1eab: {0x4c0c67, 0x4c0de5, 0x4c0ded, 0x4c1e23}, 0x47230c: set()}
# The sub-mesh loop of 0x004c0150 and the per-view loop of 0x00471f50.
LOOPS = {0x4c1eab: (0x4c0223, 0x4c4088), 0x47230c: (0x472197, 0x47238d)}
# Anchors, as (address, expected bytes, meaning), per routine.
MATERIAL_ANCHORS = (
    (0x4c1eb3, '6a01', 'push 1: D3DXFX_DONOTSAVESTATE'),
    (0x4c1eb5, '8d842488000000', 'lea eax,[esp+0x88] one push deep = frame+0x84, the pass-count slot'),
    (0x4c1ebc, '50', 'push eax'),
    (0x4c1ebd, '53', 'push ebx: the effect'),
    (0x4c1ebe, 'ffd1', 'ID3DXEffect::Begin, slot 63 (+0xfc)'),
    (0x4c3fde, '83bc248400000000', 'cmp [esp+0x84],0 at frame depth (pass count)'),
    (0x4c3ff0, '8b442474', 'pass_begin reads the pass index at frame depth'),
)
FRAME_ANCHORS = (
    (0x4722c8, '8b1518856000', 'view_submit_end: mov edx,[0x608518]'),
    (0x472301, '7412', 'je 0x472315: skips the particles call and the span'),
    (0x472306, '52', 'push edx: the particles argument'),
    (0x472307, 'e8b4d10400', 'call 0x004bf4c0: particles'),
    (0x472315, 'a1346f6000', 'the first instruction after the span'),
)
PARTICLES_SKIP = (0x472301, 0x472315)
SCENE_HOOK = (0x4721b1, 0x4721b6)          # X3M_SCENE_HOOK's call site
POINT_LIGHT_PATCH = (0x4c27af, 0x4c27b5)   # src/proxy/point_light_admission_core.h
ARENAS = (0x10000000, 0x71000000, 0xf1000000)
SECTIONS = (('.text', 0x400, 0x130630), ('.rdata', 0x130c00, 0x4074d),
            ('.data', 0x171400, 0xb000), ('.rsrc', 0x17c400, 0x91814))
TEXT_BASE, TEXT_OFFSET, TEXT_SIZE = 0x401000, 0x400, 0x130630


def decode(exe=DEFAULT_EXE):
    decoded = {}
    for bounds in (MATERIAL, FRAME):
        run = subprocess.run([common.OBJDUMP, '-d', '-Mintel', '--insn-width=16',
                              f'--start-address={bounds[0]}', f'--stop-address={bounds[1]}', str(exe)],
                             check=True, capture_output=True, text=True, timeout=60)
        decoded[bounds] = common.parse_objdump(run.stdout, *bounds)
    return decoded


def source_checks(text):
    if text is None:
        return None
    actual = common.parse_source_specs(text)
    expected = [dict(name=s.name, va=s.va, bytes=s.expected, length=len(s.expected),
                     rel32_offset=0, rel32_target=0) for s in SITES]
    return actual == expected


def relocated_bytes(spec, arena):
    """No rel32 field: a correct copy of a plain span is byte-identical at any address."""
    return bytes(spec.expected)


def installed_spans(texts):
    spans = []
    for text in texts:
        spans.extend(loop_probe.installed_spans(text))
    return spans


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


def inspect(image, decoded, source, data, installed):
    checks = {'preferred_base': image.image_base == common.IMAGE_BASE}
    source_ok = source_checks(source)
    checks['source_specs'] = source_ok is not False
    rows = []
    for spec in SITES:
        bounds = (spec.function_start, spec.function_end)
        instructions = decoded.get(bounds, [])
        row = common.inspect_site(image, spec, instructions)
        span = [i for i in instructions if spec.va <= i.va < spec.end]
        controls = [i for i in span if common._is_direct_control(i) is not None]
        row['plain_copy'] = bool(span) and not controls
        row['arena_replay_ok'] = (not controls
                                  and all(relocated_bytes(spec, arena) == image.read(spec.va, len(spec.expected))
                                          for arena in ARENAS))
        sources = {i.va for i in instructions if common._is_direct_control(i) == spec.va}
        row['incoming_sources'] = sorted(f'{s:#010x}' for s in sources)
        row['incoming_ok'] = sources == INCOMING[spec.va]
        loop = LOOPS[spec.va]
        row['in_loop_body'] = loop[0] <= spec.va and spec.end <= loop[1]
        row['no_installed_conflict'] = all(spec.end <= at or spec.va >= at + length for at, length in installed)
        row['ok'] = all(row[k] for k in ('bytes_ok', 'whole_instructions', 'no_interior_branch',
                                         'plain_copy', 'arena_replay_ok', 'incoming_ok',
                                         'in_loop_body', 'no_installed_conflict'))
        rows.append(row)
    checks['sites'] = all(row['ok'] for row in rows)
    material = decoded.get(MATERIAL, [])
    frame = decoded.get(FRAME, [])
    material_by_va = {i.va: i for i in material}
    frame_by_va = {i.va: i for i in frame}
    checks['complete_material_routine'] = (bool(material) and material[0].va == MATERIAL[0]
                                           and material[-1].end == MATERIAL[1])
    checks['complete_frame_routine'] = (bool(frame) and frame[0].va == FRAME[0]
                                        and frame[-1].end == FRAME[1])
    checks['no_indirect_jump'] = not any(i.mnemonic == 'jmp' and 'PTR' in i.operands
                                         for i in material + frame)
    checks['frame_single_ret'] = sum(i.mnemonic == 'ret' for i in frame) == 1
    checks['material_anchors'] = all(va in material_by_va and material_by_va[va].raw.hex() == raw
                                     for va, raw, _ in MATERIAL_ANCHORS)
    checks['frame_anchors'] = all(va in frame_by_va and frame_by_va[va].raw.hex() == raw
                                  for va, raw, _ in FRAME_ANCHORS)
    # The Begin dispatch follows the span directly and the span sits ahead of
    # the pass loop; the particles skip lands on the span end and the span
    # follows the call directly.
    checks['material_precedes_begin'] = (0x4c1eb3 in material_by_va and SITES[0].end == 0x4c1eb3
                                         and SITES[0].end <= 0x4c3ff0)
    checks['particles_skip_lands_on_span_end'] = (
        PARTICLES_SKIP[0] in frame_by_va
        and common._is_direct_control(frame_by_va[PARTICLES_SKIP[0]]) == PARTICLES_SKIP[1]
        and PARTICLES_SKIP[1] == SITES[1].end
        and 0x472307 in frame_by_va and frame_by_va[0x472307].end == SITES[1].va)
    # Every direct jump between view_submit_end and the span stays inside
    # [view_submit_end, span end] (the only call is the particles call the
    # span follows): the span always follows the view's view_submit_end stamp
    # in the same iteration.
    between = [i for i in frame if 0x4722c8 <= i.va < SITES[1].va]
    checks['submit_end_reaches_span'] = all(
        (i.mnemonic == 'call' and i.va == 0x472307) or common._is_direct_control(i) is None
        or 0x4722c8 <= common._is_direct_control(i) <= SITES[1].end
        for i in between)
    checks['scene_hook_disjoint'] = all(s.end <= SCENE_HOOK[0] or s.va >= SCENE_HOOK[1] for s in SITES)
    checks['point_light_patch_disjoint'] = all(s.end <= POINT_LIGHT_PATCH[0]
                                               or s.va >= POINT_LIGHT_PATCH[1] for s in SITES)
    raw_hits = raw_interior_scan(data)
    checks['no_raw_interior_encoding'] = not raw_hits
    data_hits = data_reference_hits(data)
    checks['no_data_reference'] = all(hit['section'] == '.rsrc' and not hit['aligned']
                                      for hit in data_hits)
    return {'result': 'PASS' if all(checks.values()) else 'FAIL', 'checks': checks,
            'source_present': source is not None, 'installed_sites_checked': len(installed),
            'sites': rows, 'raw_interior_hits': raw_hits, 'data_reference_hits': data_hits}


def verify(exe=DEFAULT_EXE, source=SOURCE, installed=INSTALLED):
    data = Path(exe).read_bytes()
    text = Path(source).read_text() if source and Path(source).exists() else None
    spans = installed_spans([Path(path).read_text() for path in installed])
    try:
        report = inspect(common.Image(data), decode(exe), text, data, spans)
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
                      'installed_sites_checked': report.get('installed_sites_checked'),
                      **report['checks']}, indent=2))
    raise SystemExit(report['result'] != 'PASS')
