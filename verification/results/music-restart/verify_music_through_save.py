#!/usr/bin/env python3
"""Read-only byte/census check for docs/reverse-engineering/music-restart.md section 7 (music through the save).

Pins the DirectSound streaming pump 0x004d0700 (re-arm, cursor read, one-sample-per-call refill with the
free-space test, looping Play, end-of-stream drain), the media constructor's ring/sample sizes and buffer
flags, the forced DirectSound path for audio-only requests, the Run helper's re-arm, the play routine's Run
call (candidate Patch E), the save routine's write order, the gzwrite thunk/IAT slot, the script writer and
loader handling of suspended tasks, and the direct-caller census of the pump chain. Also counts x87
instructions and lists direct call targets inside the pump (capstone). No Wine, no game, nothing written.
Prints one JSON object; exit status 0 only on PASS.

    python3 -I verification/results/music-restart/verify_music_through_save.py [X3AP.exe]
"""
import hashlib
import json
import struct
import sys
from pathlib import Path

EXE = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('/mnt/c/X3/X3AP.exe')
SHA256 = 'fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab'
TEXT_VA, TEXT_RAW = 0x401000, 0x400

PATTERNS = [
    ('pump_entry', 0x4d0700, '83ec44535556578bf0bf01000000'),
    ('pump_rearm', 0x4d070e, '397e487524397e64751f8b46448b088b514850ffd28b464433c9894e4c894e508b1051508b4234ffd0'),
    ('pump_cursor', 0x4d0737, '8b46448b088d542434528d54241852508b4110ffd085c0'),
    ('pump_completion', 0x4d0754, '837e6402751a8b46588b088b511c6a006a0050ffd285c07507c7466404000000'),
    ('pump_space_check', 0x4d079a, '397e48741a8b4e288bc12b464c33d203442414f7f13b5424180f867b020000'),
    ('pump_wpos_advance_play_looping', 0x4d09b8,
     '8b44242c2bc703464c33d2f77628b901000000394e48894e6489564c75208b46448b088b51306a016a006a0050ffd285c00f8cd2000000c7464802000000'),
    ('pump_end_pause', 0x4d0a08, '8b4674c74664080000008b088b512050ffd28b464c2b4424148b4e2803c133d2f7f1c7464804000000895650'),
    ('pump_update_async', 0x4d0a50, '8b46588b088b51186a006a006a006a0150ffd2'),
    ('pump_update_state', 0x4d0b0a, '33c085ff0f94c08d440002894664'),
    ('pump_drain_end', 0x4d0c0c, '8b46448b088b514850ffd25fc7464801000000c7464c000000005e5d66b802005b83c444c3'),
    ('query_ds_tail', 0x4d17b5, '8b868c000000a8087545a840740e8bc65f5e5d5b83c424e92fefffff'),
    ('ctor_force_ds', 0x4cf504, '8b450ca810740383c840'),
    ('ctor_renderer_gate', 0x4cf59e, '8b0d346f6000f7810001000000400000'),
    ('ctor_add_audio_stream', 0x4cf5c0, '8b8b8c0000008b43048b108b523cc1e90657f7d183e1015168842b53006a0050ffd2'),
    ('ctor_sample_2s', 0x4d0244, '8b5424480fb74c244e69d2d0070000b8d34d6210f7e2c1ea06c1e9036a008bf2894b68895360e88187fe'),
    ('ctor_ring_5s', 0x4d02f2, '8b54244833c069d288130000894324894328'),
    ('ctor_dsbd_flags', 0x4d0319,
     'b8d34d6210f7e26a00c1ea068d442444894330c70124000000c7432488000100895328a1ec8a60008b108d73445651508b420cffd085c0'),
    ('ctor_ds_fail', 0x4d0352, '8b838c000000a8100f8500feffff83c8088bf389838c000000e8d0160000eb07'),
    ('ctor_armed', 0x4d0372, 'c7434801000000'),
    ('alloc_flags_copy', 0x498248, '8b888c0000008b15446f6000894e2c'),
    ('alloc_ctor_call', 0x4981d1, '5753e88872030083c40885c0'),
    ('run_rearm', 0x4d19cf, '8b838c000000a8087511a840740dbd01000000896b48896b64eb05bd0100000033ff'),
    ('play_run_call', 0x498d6f, '8bc6e8fa8a030085c07464'),
    ('stop_all_ds_arm', 0x498304, '8b47443bc3740c8b088b514850ffd285c07c0bb801000000894748894764'),
    ('loader_suspended_relink', 0x4a1c69, '663d020075328b4424148b90ec1200008957048b88ec12000089398d90e8120000891789b8ec120000'),
    ('writer_task_state', 0x4a0584, '0fb74d3a66c1c1080fb7d18d44241489542414508d47018bd6e83e8d0400'),
    ('save_order_script', 0x4049e8, '8d74241ce83faf0900'),
    ('save_order_movi', 0x404a08, '8bce51e8c03e0900'),
    ('save_gzclose', 0x404a6b, '8d4c241ce8ec480e00'),
    ('save_gzopen', 0x4045f3, '6a018bf06838585500568d442428e8ea4a0e00'),
    ('gzwrite_thunk', 0x4fae46, 'ff25e0235300'),
    ('oom_cb_install', 0x4035b1, 'c705008a6000806d4000'),
    ('main_loop_sched_mgr', 0x403a7a, 'e8f1bc0900e8ec480900'),
]
# direct E8/E9 callers expected (target -> sorted list of (site, kind))
CENSUS = {
    0x4d0700: [('0x4d17cc', 'jmp')],
    0x4d14e0: [('0x4983d9', 'call')],
    0x498370: [('0x403a7f', 'call'), ('0x403a98', 'call'), ('0x403b04', 'call'), ('0x486809', 'call'),
               ('0x492dbe', 'call'), ('0x49729b', 'call')],
    0x4d1870: [('0x498d71', 'call'), ('0x498f76', 'call')],
    0x4cf460: [('0x4981d3', 'call')],
    0x4988d0: [('0x404a0b', 'call')],
    0x49f930: [('0x4049ec', 'call')],
    0x404530: [('0x403e18', 'call')],
}
PUMP = (0x4d0700, 0x4d0c31)
PUMP_DIRECT_CALLS = {0x518e60, 0x469a30, 0x515810, 0x518de0, 0x4b8b60}
NEW_SITE = 0x498d71  # candidate Patch E (call 0x4d1870)
# 0x498d4e is the ModRM/operand byte 0x7c of the 4-byte `add edi,[esp+0x2c]` at 0x498d4d (a false `jl`),
# the same rejection as in verify_music_restart_sites.py
REJECTED_INTERIOR = {'0x498d4e'}


def main():
    data = EXE.read_bytes()
    sha = hashlib.sha256(data).hexdigest()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    nsec = struct.unpack_from('<H', data, pe + 6)[0]
    opt = struct.unpack_from('<H', data, pe + 20)[0]
    secs = []
    for i in range(nsec):
        o = pe + 24 + opt + 40 * i
        vs, va, rs, ra = struct.unpack_from('<IIII', data, o + 8)
        secs.append((data[o:o + 8].rstrip(b'\0').decode(), 0x400000 + va, vs, ra, rs))

    def off(va):
        for _, sva, _, ra, rs in secs:
            if sva <= va < sva + rs:
                return va - sva + ra
        raise ValueError(hex(va))

    def rd(va, n):
        return data[off(va):off(va) + n]

    failures = []
    if sha != SHA256:
        failures.append('sha256')
    for name, va, hx in PATTERNS:
        if rd(va, len(hx) // 2) != bytes.fromhex(hx):
            failures.append(name)

    # import: which zlib1.dll function sits in IAT slot 0x5323e0
    imp_rva = struct.unpack_from('<I', data, pe + 24 + 104)[0]
    iat_name = None
    p = off(0x400000 + imp_rva)
    while True:
        oft, _, _, name_rva, ft = struct.unpack_from('<IIIII', data, p)
        if not name_rva:
            break
        dll = data[off(0x400000 + name_rva):data.index(b'\0', off(0x400000 + name_rva))].decode()
        k = 0
        while True:
            thunk = struct.unpack_from('<I', data, off(0x400000 + (oft or ft)) + 4 * k)[0]
            if not thunk:
                break
            if 0x400000 + ft + 4 * k == 0x5323e0 and not thunk & 0x80000000:
                h = off(0x400000 + thunk) + 2
                iat_name = dll + '!' + data[h:data.index(b'\0', h)].decode()
            k += 1
        p += 20
    if (iat_name or '').lower() != 'zlib1.dll!gzwrite':
        failures.append('iat_gzwrite')

    text = next(s for s in secs if s[0] == '.text')
    t0, traw, tsize = text[1], text[3], text[4]
    tx = data[traw:traw + tsize]
    found = {t: [] for t in CENSUS}
    interior = []
    for i in range(len(tx) - 6):
        va = t0 + i
        op = tx[i]
        tgt = None
        if op in (0xe8, 0xe9):
            tgt = (va + 5 + struct.unpack_from('<i', tx, i + 1)[0]) & 0xffffffff
            if tgt in found:
                found[tgt].append((hex(va), 'call' if op == 0xe8 else 'jmp'))
        elif op == 0xeb or 0x70 <= op <= 0x7f:
            tgt = (va + 2 + struct.unpack_from('<b', tx, i + 1)[0]) & 0xffffffff
        elif op == 0x0f and 0x80 <= tx[i + 1] <= 0x8f:
            tgt = (va + 6 + struct.unpack_from('<i', tx, i + 2)[0]) & 0xffffffff
        if tgt is not None and NEW_SITE < tgt < NEW_SITE + 5:
            interior.append(hex(va))
    for t, want in CENSUS.items():
        if sorted(found[t]) != sorted(want):
            failures.append('census_%x' % t)
    literal = {hex(t): data.count(struct.pack('<I', t)) for t in (0x4d0700, 0x4d14e0, 0x4d1870)}
    if any(literal.values()):
        failures.append('literal_refs')
    if set(interior) - REJECTED_INTERIOR:
        failures.append('interior_%x' % NEW_SITE)
    notify_iid = bytes.fromhex('830721b0cd89d011af0800a0c925cd16')  # IID_IDirectSoundNotify (bytes_le)
    if notify_iid in data:
        failures.append('dsound_notify_iid_present')

    x87 = None
    calls = []
    try:
        import capstone
        md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        md.detail = True
        x87 = 0
        for ins in md.disasm(rd(PUMP[0], PUMP[1] - PUMP[0]), PUMP[0]):
            if ins.mnemonic.startswith('f') and ins.bytes[0] in range(0xd8, 0xe0):
                x87 += 1
            if ins.mnemonic == 'call':
                calls.append(ins.op_str)
        direct_sites = [c for c in calls if c.startswith('0x')]
        direct = {int(c, 16) for c in direct_sites}
        absolute = sorted({c for c in calls if 'dword ptr [0x' in c})
        if x87:
            failures.append('pump_x87')
        register = [c for c in calls if c in ('eax', 'ebx', 'ecx', 'edx', 'esi', 'edi', 'ebp')]
        if direct != PUMP_DIRECT_CALLS or absolute or len(register) + len(direct_sites) != len(calls):
            failures.append('pump_calls')
    except ImportError:
        direct, absolute, register = set(), [], []

    ring_ms = struct.unpack_from('<I', rd(0x4d02fa, 4))[0]
    sample_ms = struct.unpack_from('<I', rd(0x4d024f, 4))[0]
    dsbd_flags = struct.unpack_from('<I', rd(0x4d0335, 4))[0]
    result = {
        'exe_sha256': sha, 'patterns': len(PATTERNS), 'iat_0x5323e0': iat_name,
        'ring_ms_factor': ring_ms, 'sample_ms_factor': sample_ms, 'dsbuffer_flags': hex(dsbd_flags),
        'dsbuffer_flags_decoded': [n for b, n in ((0x10000, 'GETCURRENTPOSITION2'), (0x8000, 'GLOBALFOCUS'),
                                                 (0x4000, 'STICKYFOCUS'), (0x100, 'CTRLPOSITIONNOTIFY'),
                                                 (0x80, 'CTRLVOLUME'), (0x8, 'LOCSOFTWARE')) if dsbd_flags & b],
        'census': {hex(t): ' '.join('%s:%s' % x for x in sorted(v)) for t, v in found.items()},
        'literal_dword_refs': literal, 'rejected_operand_byte_hits': sorted(REJECTED_INTERIOR),
        'raw_branch_into_0x498d71_interior': interior,
        'pump_x87_instructions': x87, 'pump_direct_calls': sorted(hex(c) for c in direct),
        'pump_absolute_indirect_calls': absolute, 'pump_register_indirect_calls': len(register),
        'failures': failures, 'result': 'PASS' if not failures else 'FAIL',
    }
    print(json.dumps(result, indent=1))
    return 0 if not failures else 1


if __name__ == '__main__':
    sys.exit(main())
