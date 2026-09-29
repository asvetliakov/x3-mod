#!/usr/bin/env python3
"""Read-only qualification of the dust-scene leak fix site 0x0041f4d1.

src/proxy/dust_leak_fix.cpp claims the five bytes `SUB dword [ESP+0x20],1`
(83 6c 24 20 01) at the common tail of the dust-scene fill loop in 0x0041efc0
through engine_patch and pushes a release-and-leave stub in front of the tail
(docs/reverse-engineering/object-lifetimes.md, "Run383", "Patch"). This checks
X3AP.exe on the host: the structural identity, the 37-byte window
0x0041f4b7..0x0041f4db (the attach call, the SUB and the JNE back to the loop
head), the instruction boundaries of the whole function, the site as one
whole instruction without a relative branch, the JNE after it reading the
SUB's flags (the stub's flags are dead), exactly the four documented branch
sources of the site (0x0041f336 with EDI = 0, and the three failure branches
0x0041f3d1, 0x0041f436, 0x0041f447) and no direct branch or raw rel8/rel32
encoding in .text landing inside the window elsewhere, no dword pointing into
it, the atomic-word rule for the five patch bytes, the patched span decoded as
a five-byte jmp with the JNE untouched, the stub bytes decoded as the
documented sequence, the loop exit 0x0041f4dc..0x0041f658 free of reads of
the [ESP+0x20] slot at any push depth and ending in the unchanged epilogue
(`ret 8`), the release routine 0x00487be0 (cdecl prologue/epilogue, the list
unlink, the Remove call), the render-manager clear calling it per node, the
constructor's list append and the attach's +0x18/+0x1c writes, no other DLL
claim on the window, and that src/proxy/dust_leak_fix_sites.h carries the same
constants. Also the `dust_leak_fix` log-line parsers. No Wine, no game launch.
"""
import argparse
import hashlib
import json
import re
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path

import exe_identity  # structure + anchors gate; hashes are INFO (docs/reverse-engineering/executable-identity.md)

import verify_collide_sites as sites

common = sites.common
ROOT = Path(__file__).resolve().parents[2]
CORE = ROOT / 'src/proxy/dust_leak_fix_sites.h'
DEFAULT_EXE = common.DEFAULT_EXE
FUNCTION = (0x41efc0, 0x41f65b)  # ret 8 at 0x41f658, int3 padding from 0x41f65b
LOOP_VA, WINDOW_VA, SITE_VA, JNE_VA, EXIT_VA, RET_VA = 0x41f328, 0x41f4b7, 0x41f4d1, 0x41f4d6, 0x41f4dc, 0x41f658
ATTACH_VA, RELEASE_VA, CLEAR_CALL_VA = 0x489da0, 0x487be0, 0x471050
WINDOW = bytes.fromhex('8d7740 e8b10d0d00 8b44246c 8b481c 83c40c 51 8bc7 e8cfa80600 836c242001 0f854cfeffff'.replace(' ', ''))
SITE = bytes.fromhex('836c242001')
WINDOW_STARTS = [0x41f4b7, 0x41f4ba, 0x41f4bf, 0x41f4c3, 0x41f4c6, 0x41f4c9, 0x41f4ca, 0x41f4cc, 0x41f4d1, 0x41f4d6]
FAILURE_SOURCES = [0x41f336, 0x41f3d1, 0x41f436, 0x41f447]
EPILOGUE = [(0x41f649, 'pop', 'edi'), (0x41f64f, 'pop', 'esi'), (0x41f653, 'pop', 'ebp'), (0x41f654, 'pop', 'ebx'),
            (0x41f655, 'add', 'esp,0x4c'), (0x41f658, 'ret', '0x8')]
# The stub template: the call's rel32, the counter's abs32 and the slot's abs32 are filled at emission.
STUB_CODE = bytes.fromhex('85ff 7423 837f1c00 751d 50 51 52 57 e800000000 83c404 5a 59 58 c744242001000000 ff0500000000 ff2500000000'.replace(' ', ''))
CALL_REL32_OFFSET, HITS_ABS32_OFFSET, SLOT_ABS32_OFFSET = 0x0f, 0x23, 0x29
STUB_MNEMONICS = ['test', 'je', 'cmp', 'jne', 'push', 'push', 'push', 'push', 'call', 'add', 'pop', 'pop', 'pop', 'mov', 'inc', 'jmp']
# Engine context pinned as short instruction sequences (docs/reverse-engineering/object-lifetimes.md, "Patch").
RELEASE_PROLOGUE = bytes.fromhex('558bec83e4f85153')            # push ebp; mov ebp,esp; and esp,-8; push ecx; push ebx
RELEASE_EPILOGUE_VA, RELEASE_EPILOGUE = 0x487e1d, bytes.fromhex('5f5e5b8be55dc3')  # pop edi; pop esi; pop ebx; mov esp,ebp; pop ebp; ret
RELEASE_UNLINK_VA, RELEASE_UNLINK = 0x487d55, bytes.fromhex('8b43048b0b89088b138b4304')  # [prev]=next; [next+4]=prev
RELEASE_REMOVE_CALL_VA, RELEASE_REMOVE_TARGET = 0x487d70, 0x4efd30
CLEAR_CALL = bytes.fromhex('56e88a6b0100')                      # push esi; call 0x00487be0
CTOR_APPEND_VA, CTOR_APPEND = 0x486e28, bytes.fromhex('8b4830894b048b50308d482c891a890b895830')
ATTACH_PARENT_VA, ATTACH_PARENT = 0x489dbe, bytes.fromhex('c7471800000000')  # mov [edi+0x18],0
ATTACH_SCENE_VA, ATTACH_SCENE = 0x489de5, bytes.fromhex('895f1c')            # mov [edi+0x1c],ebx
LOG_RE = re.compile(r'\bdust_leak_fix site=(?P<site>[0-9a-f]{8}) status=(?P<status>patched|patched_unverified|off|refused) reason=(?P<reason>\S+) '
                    r'mode=(?P<mode>on|off|-) setting=(?P<setting>[!-~]+) write=(?P<write>none|atomic|plain) stub=(?P<stub>[0-9a-f]{8})')
RESTORE_RE = re.compile(r'\bdust_leak_fix_restore site=(?P<site>[0-9a-f]{8}) status=(?P<status>restored|restore_not_owned|restore_failed) '
                        r'found=(?P<found>[0-9a-f]{10}|--) registered=(?P<registered>[01])')
HITS_RE = re.compile(r'\bdust_leak_fix hits=(?P<hits>\d+) total=(?P<total>\d+) frame=(?P<frame>\d+)')


def parse_log_line(line):
    """The one `dust_leak_fix site=` install line -> dict, or None."""
    match = LOG_RE.search(line)
    if not match:
        return None
    row = match.groupdict()
    row['site'], row['stub'] = int(row['site'], 16), int(row['stub'], 16)
    row['patched'] = row['status'] == 'patched'
    return row


def parse_restore_line(line):
    """The `dust_leak_fix_restore` row written by shutdown() on a dynamic unload -> dict, or None."""
    match = RESTORE_RE.search(line)
    if not match:
        return None
    row = match.groupdict()
    return {'site': int(row['site'], 16), 'status': row['status'], 'found': None if row['found'] == '--' else bytes.fromhex(row['found']),
            'registered': row['registered'] == '1'}


def parse_hits_line(line):
    """The 300-frame `dust_leak_fix hits=` row -> dict, or None."""
    match = HITS_RE.search(line)
    return {k: int(v) for k, v in match.groupdict().items()} if match else None


def source_constants(text):
    """VAs, lengths and the byte arrays from dust_leak_fix_sites.h."""
    def value(name):
        match = re.search(rf'\b{name}\s*=\s*(0x[0-9a-fA-F]+|\d+)\s*[;,]', text)
        return int(match.group(1), 0) if match else None

    def array(name):
        match = re.search(rf'\b{name}\[\w+\]\s*=\s*\{{([^}}]*)\}}', text)
        return bytes(int(b, 0) for b in re.findall(r'0x[0-9a-fA-F]{2}', match.group(1))) if match else b''
    spec = re.search(r'claim_spec\s*=\s*\{\s*"([a-z_0-9]+)"\s*,\s*(0x[0-9a-fA-F]+)\s*,\s*\{([^}]*)\}\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}', text)
    sources = re.search(r'failure_sources\[4\]\s*=\s*\{([^}]*)\}', text)
    return {name: value(name) for name in ('function_va', 'function_end_va', 'loop_va', 'window_va', 'site_va', 'jne_va', 'exit_va', 'ret_va',
                                           'attach_va', 'release_va', 'clear_call_va', 'window_length', 'site_offset', 'site_length',
                                           'stub_code_length', 'stub_length', 'call_rel32_offset', 'hits_abs32_offset', 'slot_abs32_offset',
                                           'scene_offset', 'remaining_slot', 'node_size')} | {
        'window': array('expected_window'), 'site': array('expected_site'), 'stub': array('stub_code'),
        'sources': [int(v, 16) for v in re.findall(r'0x[0-9a-fA-F]+', sources.group(1))] if sources else None,
        'claim': (spec.group(1), int(spec.group(2), 16), bytes(int(b, 0) for b in re.findall(r'0x[0-9a-fA-F]{2}', spec.group(3))),
                  int(spec.group(4)), int(spec.group(5)), int(spec.group(6))) if spec else None}


EXPECTED_CONSTANTS = {'function_va': FUNCTION[0], 'function_end_va': FUNCTION[1], 'loop_va': LOOP_VA, 'window_va': WINDOW_VA, 'site_va': SITE_VA,
                      'jne_va': JNE_VA, 'exit_va': EXIT_VA, 'ret_va': RET_VA, 'attach_va': ATTACH_VA, 'release_va': RELEASE_VA,
                      'clear_call_va': CLEAR_CALL_VA, 'window_length': len(WINDOW), 'site_offset': SITE_VA - WINDOW_VA, 'site_length': len(SITE),
                      'stub_code_length': len(STUB_CODE), 'stub_length': len(STUB_CODE), 'call_rel32_offset': CALL_REL32_OFFSET,
                      'hits_abs32_offset': HITS_ABS32_OFFSET, 'slot_abs32_offset': SLOT_ABS32_OFFSET, 'scene_offset': 0x1c,
                      'remaining_slot': 0x20, 'node_size': 0x270, 'window': WINDOW, 'site': SITE, 'stub': STUB_CODE, 'sources': FAILURE_SOURCES,
                      'claim': ('dust_fill_failed_body', SITE_VA, SITE, 5, 8, 0)}


def encode_stub(stub_at, release, hits, slot):
    """The Python twin of sites::encode_stub."""
    out = bytearray(STUB_CODE)
    out[CALL_REL32_OFFSET:CALL_REL32_OFFSET + 4] = struct.pack('<i', release - (stub_at + CALL_REL32_OFFSET + 4))
    out[HITS_ABS32_OFFSET:HITS_ABS32_OFFSET + 4] = struct.pack('<I', hits)
    out[SLOT_ABS32_OFFSET:SLOT_ABS32_OFFSET + 4] = struct.pack('<I', slot)
    return bytes(out)


def decode_blob(code, va):
    """objdump decode of raw bytes placed at va (a headerless blob, as objdump_window does)."""
    tool = shutil.which(common.OBJDUMP)
    if not tool:
        raise RuntimeError(f'{common.OBJDUMP} not found')
    with tempfile.TemporaryDirectory(prefix='x3-dust-leak-') as directory:
        blob = Path(directory) / 'stub.code'
        blob.write_bytes(code)
        run = subprocess.run([tool, '-D', '-b', 'binary', '-m', 'i386', '-Mintel', '--insn-width=16', f'--adjust-vma={va:#x}', str(blob)],
                             check=True, capture_output=True, text=True, timeout=60)
    return common.parse_objdump(run.stdout, va, va + len(code))


def raw_branch_sources(data, lo, hi):
    """(source VA, target VA) of every rel8/rel32 branch or call encoding in .text landing in [lo, hi) (a superset of real branches)."""
    image = common.Image(data)
    hits = []
    for name, virtual_size, virtual_address, raw_size, raw_pointer, _ in exe_identity.section_table(data):
        if name != '.text':
            continue
        text = data[raw_pointer:raw_pointer + min(virtual_size, raw_size)]
        base = image.image_base + virtual_address
        for i in range(len(text) - 6):
            op = text[i]
            if op in (0xe8, 0xe9):
                target = base + i + 5 + struct.unpack_from('<i', text, i + 1)[0]
            elif op == 0x0f and 0x80 <= text[i + 1] <= 0x8f:
                target = base + i + 6 + struct.unpack_from('<i', text, i + 2)[0]
            elif 0x70 <= op <= 0x7f or op == 0xeb or 0xe0 <= op <= 0xe3:
                target = base + i + 2 + struct.unpack_from('<b', text, i + 1)[0]
            else:
                continue
            if lo <= target < hi:
                hits.append((base + i, target))
    return hits


def patched_image(data, dispatcher=0x10000000):
    """The image with the claim's jump at 0x0041f4d1 (e9 rel32 to `dispatcher`)."""
    image = common.Image(data)
    out = bytearray(data)
    for name, virtual_size, virtual_address, raw_size, raw_pointer, _ in exe_identity.section_table(data):
        start = image.image_base + virtual_address
        if start <= SITE_VA < start + raw_size:
            offset = raw_pointer + SITE_VA - start
            out[offset:offset + 5] = b'\xe9' + struct.pack('<i', dispatcher - (SITE_VA + 5))
            return bytes(out)
    raise ValueError('site outside the image')


def esp_offsets(instructions, lo, hi):
    """Every [esp+0xNN] displacement read or written by the instructions in [lo, hi)."""
    found = set()
    for i in instructions:
        if lo <= i.va < hi:
            for m in re.finditer(r'\[esp\+0x([0-9a-f]+)\]', i.operands):
                found.add(int(m.group(1), 16))
    return found


def inspect(data, instructions, patched_span, patched_jne, stub_decoded, core_text, claims):
    image = common.Image(data)
    by_va = {i.va: i for i in instructions}
    window_end = WINDOW_VA + len(WINDOW)
    incoming = sorted((i.va, t) for i in instructions for t in [common._is_direct_control(i)] if t is not None and WINDOW_VA <= t < window_end)
    site_sources = sorted(a for a, t in incoming if t == SITE_VA)
    interior = {va for i in instructions for va in range(i.va + 1, i.va + len(i.raw))}
    raw = raw_branch_sources(data, WINDOW_VA, window_end)
    raw_real = [(s, t) for s, t in raw if s not in interior]
    raw_elsewhere = [(s, t) for s, t in raw_real if t != SITE_VA]
    raw_site = sorted(s for s, t in raw_real if t == SITE_VA)
    dword_refs = sum(data.count(struct.pack('<I', va)) for va in range(WINDOW_VA, window_end))
    site, jne, exit_, ret = by_va.get(SITE_VA), by_va.get(JNE_VA), by_va.get(EXIT_VA), by_va.get(RET_VA)
    # The nodes-to-fill slot is [ESP+0x20] at the loop's depth; after the tail's pushes (at most five deep before a
    # call) it would alias [ESP+0x24] .. [ESP+0x34]. None of these is read or written between the exit and the ret.
    aliases = {0x20 + 4 * depth for depth in range(0, 6)}
    tail_offsets = esp_offsets(instructions, EXIT_VA, RET_VA)
    epilogue = [(va, by_va[va].mnemonic, by_va[va].operands.replace(' ', '')) for va, _, _ in EPILOGUE if va in by_va]
    release_call_target = None
    if RELEASE_REMOVE_CALL_VA is not None:
        raw_call = image.read(RELEASE_REMOVE_CALL_VA, 5)
        if raw_call and raw_call[0] == 0xe8:
            release_call_target = RELEASE_REMOVE_CALL_VA + 5 + struct.unpack('<i', raw_call[1:])[0]
    clear_call = image.read(CLEAR_CALL_VA, 6)
    clear_target = CLEAR_CALL_VA + 6 + struct.unpack('<i', clear_call[2:])[0] if clear_call and clear_call[:2] == CLEAR_CALL[:2] else None
    stub_slot, stub_hits, stub_release = 0x00123458, 0x0012345c, RELEASE_VA
    overlapping = [(name, hex(address)) for name, address, length in claims if address < window_end and WINDOW_VA < address + length]
    in_function = [(name, hex(address), length) for name, address, length in claims if address < FUNCTION[1] and FUNCTION[0] < address + length]
    checks = {
        'exe_identity': exe_identity.identity_ok(data),
        'preferred_base': image.image_base == common.IMAGE_BASE,
        'window_bytes': image.read(WINDOW_VA, len(WINDOW)) == WINDOW,
        'window_whole_instructions': [i.va for i in instructions if WINDOW_VA <= i.va < window_end] == WINDOW_STARTS and window_end in by_va,
        'function_whole_instructions': FUNCTION[0] in by_va and ret is not None and ret.mnemonic == 'ret' and ret.operands == '0x8' and ret.end == FUNCTION[1],
        'site_whole_instruction': site is not None and site.raw == SITE and site.mnemonic == 'sub' and site.operands.replace(' ', '') == 'DWORDPTR[esp+0x20],0x1',
        'site_no_relative_branch': site is not None and common._is_direct_control(site) is None,
        # The JNE reads ZF of the SUB in the tail; the stub's flags are rewritten by the SUB before any reader.
        'jne_reads_sub_flags': jne is not None and jne.mnemonic == 'jne' and common._is_direct_control(jne) == LOOP_VA and jne.end == EXIT_VA,
        'attach_call_before_site': by_va.get(0x41f4cc) is not None and by_va[0x41f4cc].mnemonic == 'call' and common._is_direct_control(by_va[0x41f4cc]) == ATTACH_VA,
        'exit_is_loop_fall_through': exit_ is not None and exit_.mnemonic == 'mov' and exit_.operands.replace(' ', '') == 'ecx,DWORDPTR[esp+0x64]',
        'site_sources': site_sources == FAILURE_SOURCES,
        'no_interior_branch': all(t == SITE_VA for _, t in incoming),
        'no_raw_branch_into_window': raw_elsewhere == [] and raw_site == FAILURE_SOURCES,
        'no_dword_into_window': dword_refs == 0,
        'atomic_word': (SITE_VA & 7) + 5 <= 8,
        'tail_slot_dead': not (tail_offsets & aliases) and 0x18 not in tail_offsets and 0x1c not in tail_offsets,
        'epilogue_unchanged': epilogue == EPILOGUE,
        'patched_decode': (len(patched_span) == 1 and patched_span[0].mnemonic == 'jmp' and len(patched_span[0].raw) == 5
                           and common._is_direct_control(patched_span[0]) == 0x10000000
                           and len(patched_jne) == 1 and patched_jne[0].mnemonic == 'jne' and common._is_direct_control(patched_jne[0]) == LOOP_VA),
        'stub_decode': ([i.mnemonic for i in stub_decoded] == STUB_MNEMONICS
                        and stub_decoded[0].operands.replace(' ', '') == 'edi,edi'
                        and common._is_direct_control(stub_decoded[1]) == 0x1000 + SLOT_ABS32_OFFSET - 2
                        and stub_decoded[2].operands.replace(' ', '') == 'DWORDPTR[edi+0x1c],0x0'
                        and common._is_direct_control(stub_decoded[3]) == 0x1000 + SLOT_ABS32_OFFSET - 2
                        and [i.operands for i in stub_decoded[4:8]] == ['eax', 'ecx', 'edx', 'edi']
                        and common._is_direct_control(stub_decoded[8]) == stub_release
                        and stub_decoded[9].operands.replace(' ', '') == 'esp,0x4'
                        and [i.operands for i in stub_decoded[10:13]] == ['edx', 'ecx', 'eax']
                        and stub_decoded[13].operands.replace(' ', '') == 'DWORDPTR[esp+0x20],0x1'
                        and f'{stub_hits:#x}' in stub_decoded[14].operands and f'{stub_slot:#x}' in stub_decoded[15].operands),
        'stub_twin': encode_stub(0x1000, stub_release, stub_hits, stub_slot)[:CALL_REL32_OFFSET] == STUB_CODE[:CALL_REL32_OFFSET],
        'release_prologue_epilogue': image.read(RELEASE_VA, len(RELEASE_PROLOGUE)) == RELEASE_PROLOGUE
                                     and image.read(RELEASE_EPILOGUE_VA, len(RELEASE_EPILOGUE)) == RELEASE_EPILOGUE,
        'release_unlinks_and_removes': image.read(RELEASE_UNLINK_VA, len(RELEASE_UNLINK)) == RELEASE_UNLINK and release_call_target == RELEASE_REMOVE_TARGET,
        'clear_calls_release_per_node': clear_call == CLEAR_CALL and clear_target == RELEASE_VA,
        'constructor_appends_to_unattached_list': image.read(CTOR_APPEND_VA, len(CTOR_APPEND)) == CTOR_APPEND,
        'attach_writes_parent_and_scene': image.read(ATTACH_PARENT_VA, len(ATTACH_PARENT)) == ATTACH_PARENT and image.read(ATTACH_SCENE_VA, len(ATTACH_SCENE)) == ATTACH_SCENE,
        'no_other_claim': overlapping == [],
        'source_constants': source_constants(core_text) == EXPECTED_CONSTANTS,
    }
    return {'result': 'PASS' if all(checks.values()) else 'FAIL', 'checks': checks, 'exe_info': exe_identity.info(data), 'site': hex(SITE_VA),
            'site_bytes': (image.read(SITE_VA, len(SITE)) or b'').hex(), 'window_bytes': (image.read(WINDOW_VA, len(WINDOW)) or b'').hex(),
            'site_qword': hex(SITE_VA & ~7), 'incoming_window_branches': [(hex(a), hex(t)) for a, t in incoming],
            'site_sources': [hex(a) for a in site_sources], 'raw_branch_hits': [(hex(s), hex(t)) for s, t in raw],
            'raw_branch_hits_not_interior': [(hex(s), hex(t)) for s, t in raw_real], 'dword_refs': dword_refs,
            'tail_esp_offsets': sorted(hex(o) for o in tail_offsets), 'other_claims': len(claims), 'overlapping_claims': overlapping,
            'other_claims_in_function': in_function, 'function_instructions': len(instructions), 'exe_sha256': hashlib.sha256(data).hexdigest()}


def verify(exe=DEFAULT_EXE, core=CORE):
    data = common.image_bytes(exe)
    try:
        instructions = common.parse_objdump(common.objdump_window(exe, *FUNCTION, timeout=120), *FUNCTION)
        patched = patched_image(data)
        patched_span = common.parse_objdump(common.objdump_window(patched, SITE_VA, SITE_VA + 5), SITE_VA, SITE_VA + 5)
        patched_jne = common.parse_objdump(common.objdump_window(patched, JNE_VA, EXIT_VA), JNE_VA, EXIT_VA)
        stub_decoded = decode_blob(encode_stub(0x1000, RELEASE_VA, 0x0012345c, 0x00123458), 0x1000)
        claims = sites.other_claims(ROOT, skip_prefix='dust_leak_fix', own_names=())
        return inspect(data, instructions, patched_span, patched_jne, stub_decoded, Path(core).read_text(), claims)
    except (ValueError, OSError, subprocess.SubprocessError, RuntimeError, KeyError, IndexError) as error:
        return {'result': 'FAIL', 'checks': {'decode': False}, 'error': str(error)}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=DEFAULT_EXE)
    parser.add_argument('--core', type=Path, default=CORE)
    args = parser.parse_args()
    report = verify(args.exe, args.core)
    print(json.dumps(report, indent=2, sort_keys=True))
    raise SystemExit(report['result'] != 'PASS')
