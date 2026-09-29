#!/usr/bin/env python3
"""Read-only qualification of the engine-side lens-flare cull (X3M_LENS_FLARE_GAIN=0) in the installed X3AP.exe.

src/proxy/lens_flare_cull.cpp chains a second stub on the small-parts claim of the per-node cull/LOD pass 0x0047cfe0
at 0x0047d2a2 (verify_cull_small_parts_site.py qualifies that window, site, cull target and branch structure; this
script re-runs it) and sends a node whose model id (+0x140) is a flare body down the engine's own size-cull
instruction at 0x0047d2c3. This adds the facts the claim rests on beyond that window: the lens block
0x00472466..0x00472476 loads the lens scene ([0x00608518]+0x64) and calls the root walker 0x0047e780, the walker
calls the pass at 0x0047e7a5 (`push 0; push edi; mov ecx,esi; call 0x0047cfe0`) for every root of the scene before
the traversal 0x0047e6e0 at 0x00472491 draws it, the pass is called from exactly those two sites (the walker and its
own recursion at 0x0047d53c), the pass reads the model id at 0x0047d2ec (`mov eax,[edi+0x140]`) after the cull point,
and src/proxy/lens_flare_cull_core.h carries the same bytes, VAs and stub layout. Also the stub encoder twin and the
`lens_flare_cull` row parsers the host test exercises. No Wine, no game launch.
"""
import argparse
import json
import re
import struct
from pathlib import Path

import exe_identity  # structure + anchors gate; hashes are INFO (docs/reverse-engineering/executable-identity.md)

import verify_chase_aim_sites as common
import verify_cull_small_parts_site as small

ROOT = Path(__file__).resolve().parents[2]
CORE = ROOT / 'src/proxy/lens_flare_cull_core.h'
DEFAULT_EXE = common.DEFAULT_EXE
PASS_VA, WALKER_VA, TRAVERSAL_VA, BODY_GLOBAL_VA, LENS_SCENE_OFFSET = 0x47cfe0, 0x47e780, 0x47e6e0, 0x608518, 0x64
LENS_WALK_VA, WALKER_CALL_VA, MODEL_READ_VA, LENS_DRAW_VA = 0x472466, 0x47e7a0, 0x47d2ec, 0x472491
LENS_WALK = bytes.fromhex('a118856000 8b4864 51 8bfe e80ac30000'.replace(' ', ''))
WALKER_CALL = bytes.fromhex('6a00 57 8bce e836e8ffff'.replace(' ', ''))
MODEL_READ = bytes.fromhex('8b8740010000')
LENS_DRAW = bytes.fromhex('56 e84ac20000'.replace(' ', ''))  # push esi; call 0x0047e6e0 (the sun-occlusion lens site)
PASS_RECURSION_VA = 0x47d53c
MODEL_OFFSET, ID_SPAN = 0x140, 0x8000
STUB_LENGTH, STUB_MODEL, STUB_REPLAY, STUB_CULL, STUB_CONTINUE = 81, 9, 39, 64, 75
BODY_NAME_COUNT = 42
EXPECTED_CONSTANTS = {'site_va': small.SITE_VA, 'cull_va': small.CULL_VA, 'lens_walk_va': LENS_WALK_VA, 'walker_call_va': WALKER_CALL_VA,
                      'model_read_va': MODEL_READ_VA, 'walker_va': WALKER_VA, 'lens_scene_offset': LENS_SCENE_OFFSET, 'id_span': ID_SPAN,
                      'stub_length': STUB_LENGTH, 'stub_model': STUB_MODEL, 'stub_replay': STUB_REPLAY, 'stub_cull': STUB_CULL,
                      'stub_continue': STUB_CONTINUE, 'lens_walk': LENS_WALK, 'walker_call': WALKER_CALL, 'model_read': MODEL_READ,
                      'body_names': BODY_NAME_COUNT}


def encode_stub(at, enabled, bitmap, culled, cull_target, next_slot):
    """The Python twin of core::encode_stub (the fixture compares the C++ bytes with this layout)."""
    out = bytearray()
    out += b'\x83\x3d' + struct.pack('<I', enabled) + b'\x00'
    out += b'\x74' + bytes([STUB_CONTINUE - 9])
    out += b'\x8b\x87' + struct.pack('<I', MODEL_OFFSET)
    out += b'\x3d' + struct.pack('<I', ID_SPAN)
    out += b'\x73' + bytes([STUB_CONTINUE - 22])
    out += b'\x8b\xc8' + b'\xc1\xe9\x05'
    out += b'\x8b\x0c\x8d' + struct.pack('<I', bitmap)
    out += b'\x0f\xa3\xc1'
    out += b'\x73' + bytes([STUB_CONTINUE - 39])
    out += small.WINDOW[small.SITE_VA - small.WINDOW_VA:small.SITE_VA - small.WINDOW_VA + 25]  # 0x0047d2a2..0x0047d2b9 replayed
    out += b'\xff\x05' + struct.pack('<I', culled)
    out += b'\xe9' + struct.pack('<i', cull_target - (at + STUB_CONTINUE))
    out += b'\xff\x25' + struct.pack('<I', next_slot)
    assert len(out) == STUB_LENGTH and out[STUB_MODEL] == 0x8b and out[STUB_REPLAY] == 0x8b and out[STUB_CULL] == 0xff
    return bytes(out)


STATUS = re.compile(r'lens_flare_cull status=(?P<status>patched|off|refused) reason=(?P<reason>\S+) bodies=(?P<bodies>\d+) mapped=(?P<mapped>\d+) '
                    r'site=0x(?P<site>[0-9a-f]{8}) cull=0x(?P<cull>[0-9a-f]{8}) stub=0x(?P<stub>[0-9a-f]{8}) write=(?P<write>none|atomic|plain)')
WINDOW_ROW = re.compile(r'lens_flare_cull culled=(?P<culled>\d+) total=(?P<total>\d+) enabled=(?P<enabled>[01]) bodies=(?P<bodies>\d+) mapped=(?P<mapped>\d+) frame=(?P<frame>\d+)')
BODIES_ROW = re.compile(r'lens_flare_cull_bodies bodies=(?P<bodies>\d+) mapped=(?P<mapped>\d+) scanned=(?P<scanned>\d+) fixed=(?P<fixed>\d+) '
                        r'dynamic=(?P<dynamic>\d+) enabled=(?P<enabled>[01]) restarts=(?P<restarts>\d+) frame=(?P<frame>\d+)')


def parse_status_line(line):
    m = STATUS.search(line)
    if not m:
        return None
    d = m.groupdict()
    return {'status': d['status'], 'reason': d['reason'], 'bodies': int(d['bodies']), 'mapped': int(d['mapped']), 'site': int(d['site'], 16),
            'cull': int(d['cull'], 16), 'stub': int(d['stub'], 16), 'write': d['write']}


def parse_window_line(line):
    m = WINDOW_ROW.search(line)
    return {k: int(v) for k, v in m.groupdict().items()} if m else None


def parse_bodies_line(line):
    m = BODIES_ROW.search(line)
    return {k: int(v) for k, v in m.groupdict().items()} if m else None


def source_constants(text):
    def value(name):
        m = re.search(r'\b' + name + r'\s*=\s*(0x[0-9a-fA-F]+|\d+)', text)
        return int(m.group(1), 0) if m else None

    def array(name):
        m = re.search(name + r'\[' + name + r'_length\]\s*=\s*\{([^}]*)\}', text)
        return bytes(int(t, 0) for t in re.findall(r'0x[0-9a-fA-F]{2}', m.group(1))) if m else None
    names = re.search(r'body_names\[\]\s*=\s*\{([^}]*)\}', text)
    return {'site_va': small.SITE_VA if 'site_va = small::site_va' in text else None,
            'cull_va': small.CULL_VA if 'cull_va = small::cull_va' in text else None,
            'lens_walk_va': value('lens_walk_va'), 'walker_call_va': value('walker_call_va'), 'model_read_va': value('model_read_va'),
            'walker_va': value('walker_va'), 'lens_scene_offset': value('lens_scene_offset'), 'id_span': value('id_span'),
            'stub_length': value('stub_length'), 'stub_model': value('stub_model'), 'stub_replay': value('stub_replay'),
            'stub_cull': value('stub_cull'), 'stub_continue': value('stub_continue'),
            'lens_walk': array('lens_walk'), 'walker_call': array('walker_call'), 'model_read': array('model_read'),
            'body_names': len(re.findall(r'"v\\\\\d{5}"', names.group(1))) if names else None}


def body_names(text):
    m = re.search(r'body_names\[\]\s*=\s*\{([^}]*)\}', text)
    return [n.replace('\\\\', '\\') for n in re.findall(r'"(v\\\\\d{5})"', m.group(1))] if m else []


def callers_of(data, target):
    """Every direct `call rel32` in .text whose target is `target` (a whole-section byte scan)."""
    image = common.Image(data)
    hits = []
    for name, base, vsize, rp, rsize in image.sections:
        if name != '.text':
            continue
        raw = data[rp:rp + rsize]
        for i in range(len(raw) - 5):
            if raw[i] == 0xe8:
                rel = struct.unpack_from('<i', raw, i + 1)[0]
                if base + i + 5 + rel == target:
                    hits.append(base + i)
    return sorted(hits)


def inspect(data, small_report, core_text):
    image = common.Image(data)
    constants = source_constants(core_text)
    names = body_names(core_text)
    checks = {
        'exe_identity': exe_identity.identity_ok(data),
        'small_parts_site': all(v for k, v in small_report.get('checks', {}).items() if k != 'exe_identity') and 'decode' not in small_report.get('checks', {}),
        'lens_walk_bytes': image.read(LENS_WALK_VA, len(LENS_WALK)) == LENS_WALK,
        'lens_walk_reaches_walker': LENS_WALK_VA + 11 + 5 + struct.unpack('<i', LENS_WALK[12:16])[0] == WALKER_VA,
        'walker_call_bytes': image.read(WALKER_CALL_VA, len(WALKER_CALL)) == WALKER_CALL,
        'walker_calls_pass': WALKER_CALL_VA + 5 + 5 + struct.unpack('<i', WALKER_CALL[6:10])[0] == PASS_VA,
        'lens_draw_follows_walk': image.read(LENS_DRAW_VA - 1, len(LENS_DRAW)) == LENS_DRAW and LENS_DRAW_VA - 1 + 1 + 5 + struct.unpack('<i', LENS_DRAW[2:6])[0] == TRAVERSAL_VA
                                  and LENS_WALK_VA < LENS_DRAW_VA,
        'pass_callers': callers_of(data, PASS_VA) == [PASS_RECURSION_VA, WALKER_CALL_VA + 5],
        'model_read_bytes': image.read(MODEL_READ_VA, len(MODEL_READ)) == MODEL_READ and MODEL_READ[2:4] == struct.pack('<H', MODEL_OFFSET),
        'model_read_after_cull': small.CULL_VA < MODEL_READ_VA < small.FUNCTION[1],
        'source_constants': constants == EXPECTED_CONSTANTS,
        'body_names_distinct_default_form': len(names) == BODY_NAME_COUNT and len(set(names)) == BODY_NAME_COUNT and all(re.fullmatch(r'v\\\d{5}', n) for n in names),
        'encoder': len(encode_stub(0x10000000, 0x10002000, 0x10003000, 0x10002004, small.CULL_VA, 0x10000054)) == STUB_LENGTH,
    }
    return {'result': 'PASS' if all(checks.values()) else 'FAIL', 'checks': checks, 'exe_info': exe_identity.info(data),
            'site': hex(small.SITE_VA), 'cull': hex(small.CULL_VA), 'lens_walk': hex(LENS_WALK_VA), 'walker_call': hex(WALKER_CALL_VA + 5),
            'model_read': hex(MODEL_READ_VA), 'pass_callers': [hex(a) for a in callers_of(data, PASS_VA)], 'body_names': len(names),
            'small_parts_checks': small_report.get('checks')}


def verify(exe=DEFAULT_EXE, core=CORE):
    data = common.image_bytes(exe)
    try:
        return inspect(data, small.verify(exe), Path(core).read_text())
    except (ValueError, OSError, RuntimeError) as error:
        return {'result': 'FAIL', 'checks': {'decode': False}, 'error': str(error)}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=DEFAULT_EXE)
    parser.add_argument('--core', type=Path, default=CORE)
    args = parser.parse_args()
    report = verify(args.exe, args.core)
    print(json.dumps(report, indent=2, sort_keys=True))
    raise SystemExit(report['result'] != 'PASS')
