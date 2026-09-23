#!/usr/bin/env python3
"""Read-only qualification of the small-parts cull trampoline site in 0x0047cfe0.

src/proxy/cull_small_parts.cpp claims the five bytes `mov ecx,[edi+0x18];
test ecx,ecx` at 0x0047d2a2 (the start of the effective-limit computation of
the per-node cull and LOD pass) and, when a node's `s` is below the frame's
pixel threshold, jumps to the engine's own size-cull instruction
`and dword [edi+0x12c],0xfffffffd` at 0x0047d2c3 (docs/reverse-engineering/
lod-selection.md, "Cull small parts site"). This checks the installed X3AP.exe
on the host: exact identity, the 56-byte window 0x0047d294..0x0047d2cc as
whole decoded instructions, that the site is exactly two whole instructions
of five bytes whose displaced `test` is the flag writer for the `je` after
the next instruction `mov eax,[edi+0x1d8]`, that the cull target is the
expected `and` followed by `jmp 0x0047d2d1`, that no direct branch anywhere
in the function lands inside the displaced span, that the site's incoming
branches are exactly the documented ones, that every branch inside the window
stays inside it or lands on its end, that the frame prologue and the three `[esp+0x2c]` stores that make that
slot `s` are the expected bytes, that the claim is disjoint from the
cull-census sites (0x0047d258, 0x0047d528) and the lod_scale site
(0x0047d44b), that the function ends in `ret 8`, that the engine's class-0
projectile marker is established by the pinned instructions (0x004401ae
stores 0x20800000, 0x0044123b..0x00441248 ORs it into the root node's +0x130)
and consumed as 0x20000000 by the occluder filter at 0x00488b00, and that
src/proxy/cull_small_parts_core.h carries the same constants and window. Also
the stub encoder and the `cull_small_parts`, `cull_small_parts_value` and
`cull_small_parts_frame` line parsers the host test exercises. No Wine, no
game launch.
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
CORE = ROOT / 'src/proxy/cull_small_parts_core.h'
DEFAULT_EXE = common.DEFAULT_EXE
FUNCTION = (0x47cfe0, 0x47d552)
WINDOW_VA, SITE_VA, NEXT_VA, JE_VA, CULL_VA, AFTER_CULL_VA = 0x47d294, 0x47d2a2, 0x47d2a7, 0x47d2ad, 0x47d2c3, 0x47d2d1
WINDOW = bytes.fromhex('83fe14 7d09 33f6 83a72c010000fd 8b4f18 85c9 8b87d8010000 740c 8b89d8010000 3bc8 7e02 8bc1 85c0 7e0d 3bf0 7d09 83a72c010000fd eb05'.replace(' ', ''))
SITE = WINDOW[SITE_VA - WINDOW_VA:SITE_VA - WINDOW_VA + 5]
CULL = bytes.fromhex('83a72c010000fd')
WINDOW_INSTRUCTIONS = [0x47d294, 0x47d297, 0x47d299, 0x47d29b, 0x47d2a2, 0x47d2a5, 0x47d2a7, 0x47d2ad, 0x47d2af, 0x47d2b5, 0x47d2b7,
                       0x47d2b9, 0x47d2bb, 0x47d2bd, 0x47d2bf, 0x47d2c1, 0x47d2c3, 0x47d2ca]
SITE_SOURCES = [0x47d28c, 0x47d297]
# Claims this stub must not overlap: the census's two six-byte sites and the lod_scale's six-byte site.
OTHER_CLAIMS = {'cull_census_measure': (0x47d258, 6), 'cull_census_exit': (0x47d528, 6), 'lod_scale': (0x47d44b, 6)}
RET_VA = 0x47d54f
# The stub reads `s` at [ESP+0x2c] with the site's ESP: the frame is `sub esp,0x14` plus four pushes (cdecl calls
# before the site rebalance with `add esp`), and these are the three final writers of that slot before the site.
PROLOGUE = bytes.fromhex('83ec14 8a44241c 53 55 56 57'.replace(' ', ''))
S_STORES = {0x47d229: bytes.fromhex('c744242c00000007'), 0x47d24a: bytes.fromhex('8944242c'), 0x47d250: bytes.fromhex('c744242c01000000')}
STUB_LENGTH, STUB_PROJECTILE, STUB_REPLAY, STUB_CULL, STUB_EXEMPT, STUB_CONTINUE, STUB_SCOPE_BRANCH = 82, 22, 34, 59, 70, 76, 39
SCOPES = ('bodies', 'all')
# The engine's class-0 (TBullets) root-node marker the stub's exemption tests (docs/reverse-engineering/lod-selection.md, "Projectile nodes").
FLAGS130_OFFSET, PROJECTILE_FLAG = 0x130, 0x20000000
MARKER_STORE_VA, MARKER_STORE = 0x4401ae, bytes.fromhex('c744242000008020')              # mov dword [esp+0x20],0x20800000 (class-0 case)
MARKER_OR_VA, MARKER_OR = 0x44123b, bytes.fromhex('8b4570 8b542420 099030010000'.replace(' ', ''))  # mov eax,[ebp+0x70]; mov edx,[esp+0x20]; or [eax+0x130],edx
MARKER_USE_VA, MARKER_USE = 0x488b00, bytes.fromhex('f7873001000000000020')             # test dword [edi+0x130],0x20000000 (occluder filter)
LOG_RE = re.compile(r'\bcull_small_parts requested=(?P<requested>\S+) px=(?P<px>[0-9.e+-]+) patched=(?P<patched>[01]) reason=(?P<reason>\S+) '
                    r'site=0x(?P<site>[0-9a-f]{8}) cull=0x(?P<cull>[0-9a-f]{8}) write=(?P<write>none|atomic|plain) stub=0x(?P<stub>[0-9a-f]{8}) camera=(?P<camera>\S+)(?: scope=(?P<scope>bodies|all|invalid))?'
                    r'(?: projectiles=(?P<projectiles>on|off|marker_mismatch|invalid))?')
VALUE_RE = re.compile(r'\bcull_small_parts_value px=(?P<px>[0-9.e+-]+) m00=(?P<m00>[0-9.e+-]+) width=(?P<width>\d+) threshold=(?P<threshold>-?\d+)')
FRAME_RE = re.compile(r'\bcull_small_parts_frame device=(?P<device>\d+) frame=(?P<frame>\d+) px=(?P<px>[0-9.e+-]+) threshold=(?P<threshold>-?\d+) '
                      r'culled=(?P<culled>\d+) m00=(?P<m00>[0-9.e+-]+) width=(?P<width>\d+)(?: scope=(?P<scope>bodies|all))?'
                      r'(?: projectiles=(?P<projectiles>on|off) exempt_bullet=(?P<exempt>\d+))?')


def encode_stub(at, threshold, culled, exempt, cull_target, next_slot, scope='all', projectiles=True):
    """cmp dword [threshold],0; jle continue; push eax; mov eax,[threshold]; cmp [esp+0x30],eax; pop eax; jge continue;
    test dword [edi+0x130],0x20000000; jne exempt;
    mov ecx,[edi+0x18]; test ecx,ecx; mov eax,[edi+0x1d8]; je cull; mov ecx,[ecx+0x1d8]; cmp ecx,eax; jle cull; mov eax,ecx;
    cull: inc dword [culled]; jmp cull_target; exempt: inc dword [exempt]; continue: jmp [next]. Projectiles off replaces
    bytes 22..33 with jmp 34 and int3 padding. Scope `bodies` replaces bytes 39..58 with jne continue; mov eax,[edi+0x1d8];
    jmp cull; int3 padding (a parented node runs the engine's own compare). The C++ encoder's contract."""
    if scope not in SCOPES:
        raise ValueError('scope must be bodies or all')
    for value in (at, threshold, culled, exempt, cull_target, next_slot):
        if not 0 <= value <= 0xffffffff:
            raise ValueError('addresses must be 32-bit VAs')
    code = (b'\x83\x3d' + struct.pack('<I', threshold) + b'\x00' + b'\x7e' + bytes([STUB_CONTINUE - 9])
            + b'\x50' + b'\xa1' + struct.pack('<I', threshold) + b'\x39\x44\x24\x30' + b'\x58' + b'\x7d' + bytes([STUB_CONTINUE - 22])
            + b'\xf7\x87' + struct.pack('<I', FLAGS130_OFFSET) + struct.pack('<I', PROJECTILE_FLAG) + b'\x75' + bytes([STUB_EXEMPT - 34])
            + b'\x8b\x4f\x18' + b'\x85\xc9' + b'\x8b\x87\xd8\x01\x00\x00' + b'\x74' + bytes([STUB_CULL - 47])
            + b'\x8b\x89\xd8\x01\x00\x00' + b'\x3b\xc8' + b'\x7e' + bytes([STUB_CULL - 57]) + b'\x8b\xc1'
            + b'\xff\x05' + struct.pack('<I', culled) + b'\xe9' + struct.pack('<I', (cull_target - (at + STUB_EXEMPT)) & 0xffffffff)
            + b'\xff\x05' + struct.pack('<I', exempt)
            + b'\xff\x25' + struct.pack('<I', next_slot))
    if not projectiles:
        skip = b'\xeb' + bytes([STUB_REPLAY - 24])
        code = code[:STUB_PROJECTILE] + skip + b'\xcc' * (STUB_REPLAY - STUB_PROJECTILE - len(skip)) + code[STUB_REPLAY:]
    if scope == 'bodies':
        body = b'\x75' + bytes([STUB_CONTINUE - 41]) + b'\x8b\x87\xd8\x01\x00\x00' + b'\xeb' + bytes([STUB_CULL - 49])
        code = code[:STUB_SCOPE_BRANCH] + body + b'\xcc' * (STUB_CULL - STUB_SCOPE_BRANCH - len(body)) + code[STUB_CULL:]
    assert len(code) == STUB_LENGTH
    return code


def threshold_for(px, m00, width):
    """The smallest integer t with t * px_per_s >= px (px_per_s = m00 * width / 1280), the summariser's bucket rule; 0 when unusable."""
    import math
    if not (0 < px <= 64) or not (0.05 < m00 < 20) or not (64 <= width <= 16384):
        return 0
    px_per_s = m00 * width / 1280.0
    t = math.ceil(px / px_per_s)
    while t > 1 and (t - 1) * px_per_s >= px:
        t -= 1
    while t * px_per_s < px:
        t += 1
    return min(t, 0x1000000)


def parse_log_line(line):
    match = LOG_RE.search(line)
    if not match:
        return None
    row = match.groupdict()
    return {'requested': row['requested'], 'px': float(row['px']), 'patched': row['patched'] == '1', 'reason': row['reason'],
            'site': int(row['site'], 16), 'cull': int(row['cull'], 16), 'write': row['write'], 'stub': int(row['stub'], 16), 'camera': row['camera'], 'scope': row['scope'],
            'projectiles': row['projectiles']}


def parse_value_line(line):
    match = VALUE_RE.search(line)
    if not match:
        return None
    row = match.groupdict()
    return {'px': float(row['px']), 'm00': float(row['m00']), 'width': int(row['width']), 'threshold': int(row['threshold'])}


def parse_frame_line(line):
    match = FRAME_RE.search(line)
    if not match:
        return None
    row = match.groupdict()
    return {'device': int(row['device']), 'frame': int(row['frame']), 'px': float(row['px']), 'threshold': int(row['threshold']),
            'culled': int(row['culled']), 'm00': float(row['m00']), 'width': int(row['width']), 'scope': row['scope'],
            'projectiles': row['projectiles'], 'exempt_bullet': int(row['exempt']) if row['exempt'] is not None else None}


def scope_stub_ok():
    args = (0x10000000, 0x10002000, 0x10002004, 0x10002008, CULL_VA, 0x10000054)
    every, bodies = encode_stub(*args, scope='all'), encode_stub(*args, scope='bodies')
    return (len(bodies) == STUB_LENGTH and bodies[:STUB_SCOPE_BRANCH] == every[:STUB_SCOPE_BRANCH] and bodies[STUB_CULL:] == every[STUB_CULL:]
            and bodies[STUB_REPLAY:STUB_REPLAY + 5] == SITE and bodies[39] == 0x75 and 41 + bodies[40] == STUB_CONTINUE
            and bodies[41:47] == bytes.fromhex('8b87d8010000') and bodies[47] == 0xeb and 49 + bodies[48] == STUB_CULL
            and bodies[49:STUB_CULL] == b'\xcc' * (STUB_CULL - 49))


def projectile_stub_ok():
    """The marker test sits on the below-threshold path only (after the jge), reads node+0x130 against 0x20000000 and branches to
    the exempt count, which falls through into the continue jump; `off` jumps over it to the replay; both scopes keep it."""
    args = (0x10000000, 0x10002000, 0x10002004, 0x10002008, CULL_VA, 0x10000054)
    on, off, bodies = encode_stub(*args), encode_stub(*args, projectiles=False), encode_stub(*args, scope='bodies')
    return (on[STUB_PROJECTILE:STUB_PROJECTILE + 2] == b'\xf7\x87' and struct.unpack_from('<II', on, STUB_PROJECTILE + 2) == (FLAGS130_OFFSET, PROJECTILE_FLAG)
            and on[32] == 0x75 and 34 + on[33] == STUB_EXEMPT and on[STUB_EXEMPT:STUB_EXEMPT + 2] == b'\xff\x05'
            and struct.unpack_from('<I', on, STUB_EXEMPT + 2)[0] == 0x10002008 and STUB_EXEMPT + 6 == STUB_CONTINUE
            and 22 + on[21] == STUB_CONTINUE and 9 + on[8] == STUB_CONTINUE and on[STUB_REPLAY:STUB_REPLAY + 5] == SITE
            and off[:STUB_PROJECTILE] == on[:STUB_PROJECTILE] and off[STUB_REPLAY:] == on[STUB_REPLAY:]
            and off[22] == 0xeb and 24 + off[23] == STUB_REPLAY and off[24:STUB_REPLAY] == b'\xcc' * (STUB_REPLAY - 24)
            and bodies[STUB_PROJECTILE:STUB_REPLAY] == on[STUB_PROJECTILE:STUB_REPLAY])


def source_constants(text):
    def value(name):
        match = re.search(rf'\b{name}\s*=\s*(0x[0-9a-fA-F]+|\d+)\s*[;,]', text)
        return int(match.group(1), 0) if match else None

    def array(name):
        match = re.search(rf'\b{name}\[\w+\]\s*=\s*\{{([^}}]*)\}}', text)
        return bytes(int(b, 0) for b in re.findall(r'0x[0-9a-fA-F]{2}', match.group(1))) if match else b''
    names = ('function_va', 'function_end_va', 'window_va', 'site_va', 'next_va', 'je_va', 'cull_va', 'after_cull_va', 'window_length', 'site_offset',
             'site_length', 'cull_offset', 'ret_pop', 'parent_offset', 'threshold_1d8_offset', 'stub_length', 'stub_cull', 'stub_continue', 'stub_scope_branch',
             'stub_projectile', 'stub_replay', 'stub_exempt', 'flags130_offset', 'projectile_flag', 'marker_store_va', 'marker_or_va',
             'marker_store_length', 'marker_or_length')
    return {name: value(name) for name in names} | {'window': array('window'), 'site': array('site'),
                                                     'marker_store': array('marker_store'), 'marker_or': array('marker_or')}


EXPECTED_CONSTANTS = {'function_va': FUNCTION[0], 'function_end_va': FUNCTION[1], 'window_va': WINDOW_VA, 'site_va': SITE_VA, 'next_va': NEXT_VA, 'je_va': JE_VA,
                      'cull_va': CULL_VA, 'after_cull_va': AFTER_CULL_VA, 'window_length': len(WINDOW), 'site_offset': SITE_VA - WINDOW_VA, 'site_length': 5,
                      'cull_offset': CULL_VA - WINDOW_VA, 'ret_pop': 8, 'parent_offset': 0x18, 'threshold_1d8_offset': 0x1d8,
                      'stub_length': STUB_LENGTH, 'stub_cull': STUB_CULL, 'stub_continue': STUB_CONTINUE, 'stub_scope_branch': STUB_SCOPE_BRANCH, 'window': WINDOW, 'site': SITE,
                      'stub_projectile': STUB_PROJECTILE, 'stub_replay': STUB_REPLAY, 'stub_exempt': STUB_EXEMPT, 'flags130_offset': FLAGS130_OFFSET,
                      'projectile_flag': PROJECTILE_FLAG, 'marker_store_va': MARKER_STORE_VA, 'marker_or_va': MARKER_OR_VA,
                      'marker_store_length': len(MARKER_STORE), 'marker_or_length': len(MARKER_OR), 'marker_store': MARKER_STORE, 'marker_or': MARKER_OR}


def decode(exe):
    return common.parse_objdump(common.objdump_window(exe, *FUNCTION, timeout=60), *FUNCTION)


def inspect(data, instructions, core_text):
    image = common.Image(data)
    by_va = {i.va: i for i in instructions}
    branches = [(i.va, t) for i in instructions for t in [common._is_direct_control(i)] if t is not None]
    interior = sorted((a, t) for a, t in branches if SITE_VA < t < SITE_VA + 5)
    sources = sorted(a for a, t in branches if t == SITE_VA)
    window_end = WINDOW_VA + len(WINDOW)
    inside = [(a, t) for a, t in branches if WINDOW_VA <= a < window_end]
    site, test, nxt, je = by_va.get(SITE_VA), by_va.get(SITE_VA + 3), by_va.get(NEXT_VA), by_va.get(JE_VA)
    cull, after = by_va.get(CULL_VA), by_va.get(CULL_VA + 7)
    ret = by_va.get(RET_VA)
    claim = (SITE_VA, SITE_VA + 5)
    checks = {
        'exe_identity': exe_identity.identity_ok(data),
        'preferred_base': image.image_base == common.IMAGE_BASE,
        'window_bytes': image.read(WINDOW_VA, len(WINDOW)) == WINDOW,
        'window_whole_instructions': [i.va for i in instructions if WINDOW_VA <= i.va < window_end] == WINDOW_INSTRUCTIONS and by_va.get(window_end) is not None,
        # Two whole instructions of five bytes; the displaced TEST is the flag writer the JE after the next instruction consumes.
        'site_whole_instructions': site is not None and test is not None and site.raw + test.raw == SITE and site.mnemonic == 'mov' and test.mnemonic == 'test' and test.end == NEXT_VA,
        'next_instruction': nxt is not None and nxt.mnemonic == 'mov' and nxt.raw == bytes.fromhex('8b87d8010000') and nxt.end == JE_VA,
        'je_consumes_flags': je is not None and je.mnemonic == 'je' and common._is_direct_control(je) == 0x47d2bb,
        'cull_instruction': cull is not None and cull.raw == CULL and cull.mnemonic == 'and' and after is not None and after.mnemonic == 'jmp' and common._is_direct_control(after) == AFTER_CULL_VA,
        'prologue_frame': image.read(FUNCTION[0], len(PROLOGUE)) == PROLOGUE
                          and [by_va[a].mnemonic if a in by_va else None for a in (0x47cfe0, 0x47cfe7, 0x47cfe8, 0x47cfe9, 0x47cfea)] == ['sub', 'push', 'push', 'push', 'push'],
        's_slot_stores': all(a in by_va and by_va[a].raw == raw and by_va[a].mnemonic == 'mov' for a, raw in S_STORES.items()),
        'no_interior_branch': interior == [],
        'site_sources': sources == SITE_SOURCES,
        # Every branch inside the window lands inside it or on its end (0x0047d2cc) or on the after-cull target.
        'window_branches_contained': all(WINDOW_VA <= t <= window_end or t == AFTER_CULL_VA for _, t in inside) and len(inside) == 6,
        'claim_disjoint': all(claim[1] <= a or a + n <= claim[0] for a, n in OTHER_CLAIMS.values()),
        'function_ret': ret is not None and ret.mnemonic == 'ret' and ret.raw == bytes.fromhex('c20800') and ret.end == FUNCTION[1],
        'source_constants': source_constants(core_text) == EXPECTED_CONSTANTS,
        'encoder': len(encode_stub(0x10000000, 0x10002000, 0x10002004, 0x10002008, CULL_VA, 0x10000054)) == STUB_LENGTH,
        # The exemption's premise: the class-0 creation path stores 0x20800000 and ORs it into the root node's +0x130, and the engine
        # itself reads bit 0x20000000 of +0x130 (occluder filter). Bytes only: these lie outside the decoded pass.
        'projectile_marker': image.read(MARKER_STORE_VA, len(MARKER_STORE)) == MARKER_STORE and image.read(MARKER_OR_VA, len(MARKER_OR)) == MARKER_OR
                             and image.read(MARKER_USE_VA, len(MARKER_USE)) == MARKER_USE,
        'encoder_projectiles': projectile_stub_ok(),
        # Scope bodies: the stub's parent test is the displaced span itself (same bytes, so the tail's replay leaves ECX/EFLAGS
        # as native on the continue path), its branches land on the stub's cull and continue labels, and only bytes 39..58 differ.
        'encoder_bodies': scope_stub_ok(),
        'threshold_rule': (threshold_for(2, struct.unpack('<f', struct.pack('<I', 0x3f4ccccc))[0], 1280), threshold_for(4, struct.unpack('<f', struct.pack('<I', 0x3f4ccccc))[0], 1280),
                           threshold_for(8, struct.unpack('<f', struct.pack('<I', 0x3f4ccccc))[0], 1280)) == (3, 6, 11),
    }
    return {'result': 'PASS' if all(checks.values()) else 'FAIL', 'checks': checks, 'exe_info': exe_identity.info(data), 'site': hex(SITE_VA), 'cull': hex(CULL_VA),
            'site_sources': [hex(a) for a in sources], 'interior_branches': [(hex(a), hex(t)) for a, t in interior],
            'other_claims': {k: hex(a) for k, (a, _) in OTHER_CLAIMS.items()},
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
