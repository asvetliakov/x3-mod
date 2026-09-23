#!/usr/bin/env python3
"""Read-only instruction/ABI qualification of the twenty-two submit stamps.

Twin of verify_residual_phase_sites.py for the sites of X3M_SUBMIT_PHASES
(docs/reverse-engineering/view-submit-hot-path.md sections 4-6): the draw-queue
sort 0x0047e620 and its three callers' returns, the per-node cache walk of the
traversal 0x0047d9c0, the SetTechnique and End dispatches, the Begin ->
pass-loop-guard block and the two D3DXMatrixInverse calls of the material
submission routine 0x004c0150, that routine's entry and its only caller's
return, and the world-matrix routine 0x004bdee0 with its two callers' returns.
Every containing region is decoded whole and gap-free, not as isolated bytes.
Verified: exact bytes, whole instructions, no direct branch into a span
interior (from every decoded region and from a raw-encoding scan of every
.text byte offset), the exact set of incoming jump edges on each span start,
the relative-transfer contract (a span holds no relative transfer except the
one declared `call rel32`, whose target is pinned and whose re-based copy
keeps it at three arena addresses), no x87 instruction in any span, the exact
caller sets of the three bracketed routines from a raw E8/E9 scan with every
return site directly after its call (0x004c0150's one pinned instruction later), the sort routine's shape (no call, no
x87, two 4-byte epilogues that cannot host a claim), the block-skip edge
(0x004c3fd8 -> 0x004c405d) the handler closes the block on, disjointness from
every other SiteSpec table under src/proxy and from the scene hook, the
point-light patch, the cull trampolines and the collide patches, and the
absence of any data reference to a span byte outside .rsrc. The production
site table (src/proxy/submit_phase_sites.h) is checked when present. No Wine
or game launch.
"""
import argparse
import functools
import json
import re
import struct
import subprocess
from pathlib import Path

import exe_identity  # structure + anchors gate; hashes are INFO (docs/reverse-engineering/executable-identity.md)

import verify_chase_aim_sites as common

ROOT = Path(__file__).resolve().parents[2]
PROXY = ROOT / 'src/proxy'
SOURCE = PROXY / 'submit_phase_sites.h'
DEFAULT_EXE = common.DEFAULT_EXE
FRAME = (0x471f50, 0x47260c)       # frame routine (verify_frame_phase_sites.FRAME)
TRAVERSAL = (0x47d9c0, 0x47e620)   # node traversal 0x0047d9c0
SORT = (0x47e620, 0x47e6e0)        # draw-queue sort
QUEUE = (0x47e6e0, 0x47e920)       # queue walk 0x0047e6e0 and the env-map driver
WORLD = (0x4bdee0, 0x4be3f0)       # world matrix
MATERIAL = (0x4c0150, 0x4c40fc)    # material submission routine
CALLER = (0x4c40fc, 0x4c5250)      # the routines up to and including 0x004c0150's only caller
REGIONS = (FRAME, TRAVERSAL, SORT, QUEUE, WORLD, MATERIAL, CALLER)
# name, address, bytes, region, rel32 offset (0 = plain copy), rel32 target
LEDGER = (
    ('sort_enter', 0x47e620, 'f6807002000080', SORT, 0, 0),
    ('sort_return_a', 0x4722b4, '56e826c40000', FRAME, 2, 0x47e6e0),
    ('sort_return_b', 0x472490, '56e84ac20000', FRAME, 2, 0x47e6e0),
    ('sort_return_c', 0x47e8f5, '57e8e5fdffff', QUEUE, 2, 0x47e6e0),
    ('walk_begin', 0x47e264, '8b87a0020000894c2418', TRAVERSAL, 0, 0),
    ('walk_miss', 0x47e285, '6a70e838300900', TRAVERSAL, 3, 0x5112c4),
    ('walk_join', 0x47e315, '8b83f8000000', TRAVERSAL, 0, 0),
    ('technique_begin', 0x4c0c2a, '8b0b8b91e8000000', MATERIAL, 0, 0),
    ('technique_end', 0x4c0c36, '39b7a4010000', MATERIAL, 0, 0),
    ('end_begin', 0x4c405d, '8b0b8b910c010000', MATERIAL, 0, 0),
    ('end_end', 0x4c4068, '8b4d088b8424a0000000', MATERIAL, 0, 0),
    ('block_begin', 0x4c1eb3, '6a018d842488000000', MATERIAL, 0, 0),
    ('block_end', 0x4c3fde, '83bc248400000000', MATERIAL, 0, 0),
    ('inverse_world_begin', 0x4c2251, 'e8b68c0300', MATERIAL, 1, 0x4faf0c),
    ('inverse_world_end', 0x4c2256, '8d842490030000', MATERIAL, 0, 0),
    ('inverse_view_begin', 0x4c2316, 'e8f18b0300', MATERIAL, 1, 0x4faf0c),
    ('inverse_view_end', 0x4c231b, '8b47508b0b', MATERIAL, 0, 0),
    ('material_enter', 0x4c0150, '558bec83e4f0', MATERIAL, 0, 0),
    ('material_return', 0x4c5230, '8b4c24305f5e', CALLER, 0, 0),
    ('world_enter', 0x4bdee0, '83ec0883783c00', WORLD, 0, 0),
    ('world_return_a', 0x47e007, '8b8bac010000', TRAVERSAL, 0, 0),
    ('world_return_b', 0x47e711, '8b7e108b8fac010000', QUEUE, 0, 0),
)
SITES = tuple(common.HookSpec('submit_phase_' + row[0], row[1], bytes.fromhex(row[2]), *row[3]) for row in LEDGER)
RELATIVE = {row[1]: (row[4], row[5]) for row in LEDGER if row[4]}
# Jump edges (not calls, not fall-through) that land on a span start.
INCOMING = {
    0x47e264: {0x47e253}, 0x47e285: {0x47e272}, 0x47e315: {0x47e352},
    0x4c5230: {0x4c502a},  # the caller's path that skips the submission: an idle close
    0x4c0c2a: {0x4c0c05}, 0x4c405d: {0x4c3fd8, 0x4c3fee},
    0x4c4068: {0x4c0655, 0x4c0664, 0x4c0834, 0x4c083f, 0x4c0952, 0x4c097e, 0x4c0989, 0x4c0abe,
               0x4c0ac9, 0x4c0b35, 0x4c0b4b},
}
# Bracketed routine -> {call site: (the stamped return site, the exact bytes between the call and it)}.
# 0x004c0150's return `add esp,0x18` cannot start a span: the skip edge 0x004c502a lands on the
# instruction after it, so the stamp sits there, three bytes after the call returns.
CALLERS = {
    0x47e620: {0x4722af: (0x4722b4, ''), 0x47248b: (0x472490, ''), 0x47e8f0: (0x47e8f5, '')},
    0x4c0150: {0x4c5228: (0x4c5230, '83c418')},
    0x4bdee0: {0x47e002: (0x47e007, ''), 0x47e70c: (0x47e711, '')},
}
ANCHORS = (
    (0x47e62a, '8b3d18856000', 'sort: mov edi,[0x608518], the root the queue counter reads'),
    (0x47e630, '8d7744', 'sort: lea esi,[edi+0x44], the sentinel'),
    (0x47e640, '8b4740', 'sort: mov eax,[edi+0x40], the queue head'),
    (0x47e68c, '5f', 'sort: first epilogue pop edi'), (0x47e68f, 'c3', 'sort: ret 4 bytes later'),
    (0x47e6dc, '5f', 'sort: second epilogue pop edi'), (0x47e6df, 'c3', 'sort: ret abutting 0x0047e6e0'),
    (0x47e26e, '8b08', 'walk: mov ecx,[eax], the record link'),
    (0x47e274, '39580c', 'walk: cmp [eax+0xc],ebx'),
    (0x47e277, '0f84d3000000', 'walk: je 0x47e350, the hit'),
    (0x47e350, '8bf0', 'hit: mov esi,eax, the record the join stamp reads in ESI'),
    (0x47e352, 'ebc1', 'hit: jmp 0x47e315, the join'),
    (0x4c0c34, 'ffd2', 'SetTechnique dispatch'), (0x4c4066, 'ffd2', 'End dispatch'),
    (0x4c1ebe, 'ffd1', 'Begin dispatch'),
    (0x4c3fd8, '0f867f000000', 'geometry guard: jbe 0x4c405d, skips the block_end span'),
    (0x4c3ff0, '8b442474', 'pass_begin directly after the guard'),
)
# Claims that are not SiteSpec rows: (start, end, header, text that must be in it).
FIXED_CLAIMS = (
    (0x4721b1, 0x4721b6, None, None),  # X3M_SCENE_HOOK's call site
    (0x4c27a1, 0x4c27b5, 'point_light_admission_core.h', 'site_va = 0x004c27af'),
    (0x47d248, 0x47d25e, 'cull_census_core.h', 'measure_site_va = 0x0047d258'),
    (0x47d519, 0x47d52e, 'cull_census_core.h', 'exit_site_va = 0x0047d528'),
    (0x47d294, 0x47d2b3, 'cull_small_parts_core.h', 'site_va = 0x0047d2a2'),
    (0x45d58e, 0x45d594, 'collide_box_cull_core.h', 'p1_site_va = 0x0045d58e'),
    (0x45cc7c, 0x45cc82, 'collide_box_cull_core.h', 'p2_site_va = 0x0045cc7c'),
    (0x45d665, 0x45d66a, 'collide_narrow_census_core.h', 'n5_site_va = 0x0045d665'),
    (0x48a9a5, 0x48a9aa, 'collide_narrow_census_core.h', 'n6_site_va = 0x0048a9a5'),
    (0x4e2530, 0x4e2535, 'collide_narrow_census_core.h', 'n7_site_va = 0x004e2530'),
    (0x4e2190, 0x4e2195, 'collide_narrow_census_core.h', 'n8_site_va = 0x004e2190'),
    (0x4e25a3, 0x4e25a8, 'collide_sat_sse2_core.h', 'sat_site_va = 0x004e25a3'),
    (0x47f329, 0x47f32e, 'collide_memo_core.h', 'memo_site_va = 0x0047f329'),
)
ARENAS = (0x10000000, 0x71000000, 0xf1000000)
SECTIONS = (('.text', 0x400, 0x130630), ('.rdata', 0x130c00, 0x4074d),
            ('.data', 0x171400, 0xb000), ('.rsrc', 0x17c400, 0x91814))
TEXT_BASE, TEXT_OFFSET, TEXT_SIZE = 0x401000, 0x400, 0x130630
_SPEC_RE = re.compile(r'\{\s*"([^"]+)"\s*,\s*(0x[0-9a-fA-F]+)\s*,\s*\{([^}]*)\}\s*,\s*(\d+)\s*,')


def decode(exe=DEFAULT_EXE):
    """Memoised on the file's identity (path, size, mtime): the site tests decode
    the same installed EXE in setUpClass and again inside verify()."""
    stat = Path(exe).stat()
    return dict(_decode(str(exe), stat.st_size, stat.st_mtime_ns))  # a fresh mapping over the shared lists


@functools.lru_cache(maxsize=2)
def _decode(exe, size, mtime_ns):
    decoded = {}
    for bounds in REGIONS:
        run = subprocess.run([common.OBJDUMP, '-d', '-Mintel', '--insn-width=16',
                              f'--start-address={bounds[0]}', f'--stop-address={bounds[1]}', str(exe)],
                             check=True, capture_output=True, text=True, timeout=120)
        decoded[bounds] = common.parse_objdump(run.stdout, *bounds)
    return decoded


def source_checks(text):
    """The production table against the ledger (SiteSpec: ..., length, ret_pop, rel32_offset)."""
    if text is None:
        return None
    actual = [(m.group(1), int(m.group(2), 16),
               bytes(int(v, 16) for v in re.findall(r'0x([0-9a-fA-F]{1,2})', m.group(3))),
               int(m.group(4)), int(m.group(5)), int(m.group(6)))
              for m in common._SOURCE_SPEC_RE.finditer(text)]
    expected = [('submit_phase_' + row[0], row[1], bytes.fromhex(row[2]), len(row[2]) // 2, 0, row[4])
                for row in LEDGER]
    return actual == expected


def relocated_bytes(spec, arena):
    """Independent modulo-32-bit reference for the one declared rel32 field."""
    result = bytearray(spec.expected)
    if spec.va in RELATIVE:
        offset = RELATIVE[spec.va][0]
        original = struct.unpack_from('<i', result, offset)[0]
        target = (spec.va + offset + 4 + original) & 0xffffffff
        struct.pack_into('<I', result, offset, (target - arena - offset - 4) & 0xffffffff)
    return bytes(result)


def replay_target(spec, arena):
    offset = RELATIVE[spec.va][0]
    code = relocated_bytes(spec, arena)
    return (arena + offset + 4 + struct.unpack_from('<i', code, offset)[0]) & 0xffffffff


def other_claims(proxy=PROXY, own=SOURCE):
    """(start, end) of every SiteSpec row elsewhere under src/proxy, plus the fixed claims."""
    spans, anchors_ok = [], True
    for path in sorted(list(proxy.glob('*.h')) + list(proxy.glob('*.cpp'))):
        if path.resolve() == Path(own).resolve():
            continue
        for match in _SPEC_RE.finditer(path.read_text()):
            address = int(match.group(2), 16)
            if address >= common.IMAGE_BASE:
                spans.append((address, address + int(match.group(4))))
    for start, end, header, needle in FIXED_CLAIMS:
        spans.append((start, end))
        if header and needle not in (proxy / header).read_text():
            anchors_ok = False
    return spans, anchors_ok


@functools.lru_cache(maxsize=2)
def _raw_scan(data):
    """Raw-encoding scan of .text: branches into span interiors, and E8/E9 edges onto the bracketed routines."""
    interior = {a for s in SITES for a in range(s.va + 1, s.end)}
    hits, callers = [], {target: set() for target in CALLERS}
    for offset in range(TEXT_OFFSET, TEXT_OFFSET + TEXT_SIZE - 6):
        va = TEXT_BASE + (offset - TEXT_OFFSET)
        byte = data[offset]
        if byte in (0xe8, 0xe9):
            target = (va + 5 + struct.unpack_from('<i', data, offset + 1)[0]) & 0xffffffff
            if target in callers:
                callers[target].add(va)
        elif byte == 0x0f and 0x80 <= data[offset + 1] <= 0x8f:
            target = (va + 6 + struct.unpack_from('<i', data, offset + 2)[0]) & 0xffffffff
        elif byte == 0xeb or 0x70 <= byte <= 0x7f or byte in (0xe0, 0xe1, 0xe2, 0xe3):
            target = (va + 2 + struct.unpack_from('<b', data, offset + 1)[0]) & 0xffffffff
        else:
            continue
        if target in interior:
            hits.append({'at': f'{va:#010x}', 'target': f'{target:#010x}'})
    return hits, callers


def raw_scan(data):
    """Both scans below are pure functions of the image bytes and are repeated
    once per inspect() call (the site tests call inspect ~70 times on the same
    55 MB image). Memoise on the bytes and hand out a private copy, so a caller
    that edits the report cannot reach the cached value."""
    hits, callers = _raw_scan(bytes(data))
    return [dict(h) for h in hits], {t: set(c) for t, c in callers.items()}


@functools.lru_cache(maxsize=2)
def _data_reference_hits(data):
    hits = []
    for spec in SITES:
        for va in range(spec.va, spec.end):
            word = struct.pack('<I', va)
            index = data.find(word)
            while index >= 0:
                section = next((name for name, start, size in SECTIONS if start <= index < start + size), '?')
                hits.append({'value': f'{va:#010x}', 'file_offset': f'{index:#x}', 'section': section,
                             'aligned': index % 4 == 0})
                index = data.find(word, index + 1)
    return hits


def data_reference_hits(data):
    return [dict(h) for h in _data_reference_hits(bytes(data))]


def inspect(image, decoded, source, data, claims, claims_anchored=True):
    checks = {'preferred_base': image.image_base == common.IMAGE_BASE}
    checks['source_specs'] = source_checks(source) is not False
    checks['other_claim_anchors'] = claims_anchored
    everything = [i for bounds in REGIONS for i in decoded.get(bounds, [])]
    by_va = {i.va: i for i in everything}
    checks['complete_regions'] = all(decoded.get(b) and decoded[b][0].va == b[0] and decoded[b][-1].end == b[1]
                                     for b in REGIONS)
    rows = []
    for spec in SITES:
        row = common.inspect_site(image, spec, everything)
        span = [i for i in everything if spec.va <= i.va < spec.end]
        controls = [i for i in span if common._is_direct_control(i) is not None]
        if spec.va in RELATIVE:
            offset, target = RELATIVE[spec.va]
            row['relative_ok'] = (len(controls) == 1 and controls[0].mnemonic == 'call'
                                  and controls[0].va + 1 == spec.va + offset and controls[0].end == spec.end
                                  and common._is_direct_control(controls[0]) == target)
            row['arena_replay_ok'] = row['relative_ok'] and all(replay_target(spec, a) == target for a in ARENAS)
        else:
            row['relative_ok'] = bool(span) and not controls
            row['arena_replay_ok'] = row['relative_ok'] and all(
                relocated_bytes(spec, a) == image.read(spec.va, len(spec.expected)) for a in ARENAS)
        row['no_x87'] = bool(span) and not any(i.mnemonic.startswith('f') for i in span)
        sources = {i.va for i in everything if i.mnemonic != 'call' and common._is_direct_control(i) == spec.va}
        row['incoming_sources'] = sorted(f'{s:#010x}' for s in sources)
        row['incoming_ok'] = sources == INCOMING.get(spec.va, set())
        row['no_claim_conflict'] = all(spec.end <= start or spec.va >= end for start, end in claims)
        row['ok'] = all(row[k] for k in ('bytes_ok', 'whole_instructions', 'no_interior_branch', 'relative_ok',
                                         'arena_replay_ok', 'no_x87', 'incoming_ok', 'no_claim_conflict'))
        rows.append(row)
    checks['sites'] = all(row['ok'] for row in rows)
    ordered = sorted(SITES, key=lambda s: s.va)
    checks['sites_disjoint'] = all(a.end <= b.va for a, b in zip(ordered, ordered[1:]))
    checks['anchors'] = all(va in by_va and by_va[va].raw.hex() == raw for va, raw, _ in ANCHORS)
    raw_hits, callers = raw_scan(data)
    # A raw hit is only an operand-byte pattern when it lies inside a region
    # decoded gap-free from its routine start and is not an instruction start
    # there; anywhere else it counts as a real branch.
    def operand_bytes(at):
        return any(b[0] <= at < b[1] for b in REGIONS) and at not in by_va
    real_hits = [h for h in raw_hits if not operand_bytes(int(h['at'], 16))]
    checks['no_raw_interior_encoding'] = not real_hits
    # The caller sets are exact, every caller is a decoded `call`, and each
    # stamped return site starts at the byte after it.
    checks['caller_sets'] = all(callers[target] == set(pairs) for target, pairs in CALLERS.items())
    checks['return_sites_follow_calls'] = all(
        call in by_va and by_va[call].mnemonic == 'call' and by_va[call].end + len(gap) // 2 == site
        and image.read(by_va[call].end, len(gap) // 2) == bytes.fromhex(gap)
        and any(s.va == site for s in SITES)
        for pairs in CALLERS.values() for call, (site, gap) in pairs.items())
    sort = decoded.get(SORT, [])
    checks['sort_shape'] = (bool(sort) and sum(i.mnemonic == 'ret' for i in sort) == 2
                            and not any(i.mnemonic == 'call' or i.mnemonic.startswith('f') for i in sort)
                            and not any(i.mnemonic == 'jmp' and 'PTR' in i.operands for i in sort))
    # The walk pair: no call between the open and either close, so the
    # recursive traversal cannot re-open it while it is open.
    checks['walk_has_no_call'] = not any(i.mnemonic == 'call' for i in everything
                                         if 0x47e264 <= i.va < 0x47e285 or 0x47e350 <= i.va < 0x47e354)
    checks['block_skip_edge'] = (0x4c3fd8 in by_va and common._is_direct_control(by_va[0x4c3fd8]) == 0x4c405d
                                 and 0x4c3fee in by_va and common._is_direct_control(by_va[0x4c3fee]) == 0x4c405d)
    checks['no_indirect_jump_in_material'] = not any(i.mnemonic == 'jmp' and 'PTR' in i.operands
                                                     for i in decoded.get(MATERIAL, []))
    data_hits = data_reference_hits(data)
    checks['no_data_reference'] = all(hit['section'] == '.rsrc' and not hit['aligned'] for hit in data_hits)
    return {'result': 'PASS' if all(checks.values()) else 'FAIL', 'checks': checks,
            'source_present': source is not None, 'other_claims_checked': len(claims), 'sites': rows,
            'raw_interior_hits': raw_hits, 'raw_real_hits': real_hits, 'data_reference_hits': data_hits,
            'callers': {f'{t:#010x}': sorted(f'{c:#010x}' for c in cs) for t, cs in callers.items()}}


def verify(exe=DEFAULT_EXE, source=SOURCE):
    data = Path(exe).read_bytes()
    text = Path(source).read_text() if source and Path(source).exists() else None
    claims, anchored = other_claims()
    try:
        report = inspect(common.Image(data), decode(exe), text, data, claims, anchored)
    except (ValueError, OSError, struct.error, subprocess.SubprocessError) as error:
        return {'result': 'FAIL', 'checks': {'decode': False}, 'error': str(error)}
    report['checks']['exe_identity'] = exe_identity.identity_ok(data)
    report['exe_info'] = exe_identity.info(data)
    report['exe_sha256'] = report['exe_info']['sha256']
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
                      'other_claims_checked': report.get('other_claims_checked'),
                      'sites': len(report.get('sites', [])), **report['checks']}, indent=2))
    raise SystemExit(report['result'] != 'PASS')
