#!/usr/bin/env python3
"""Read-only byte/census check for docs/reverse-engineering/music-restart.md.

Pins the instruction bytes behind the music-restart findings (WM_ACTIVATE
stop-all, save-routine stop-all, MOV_PlayMovie -> seek-to-start, completion
status, MOVI save/restore, music-volume class) on the unmodified X3AP.exe and
counts direct E8/E9 callers and literal references of the entry points the
note names. It also scans every .text byte offset for a rel8/rel32 branch whose
target falls strictly inside one of the proposed 5-byte hook sites (a raw scan:
data bytes can only add false positives, so zero hits is conservative).
No Wine, no game. Prints one JSON object; exit status 0 only on PASS.
"""
import hashlib
import json
import os
import struct
import sys
from pathlib import Path

EXE = Path(os.environ.get('X3AP_EXE', Path.home() /
           'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/X3AP.exe'))
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'verification' / 'probe'))
import exe_identity  # noqa: E402  structure + anchors gate; the hash is INFO (docs/reverse-engineering/executable-identity.md)

SHA256 = exe_identity.SHIPPED_SHA256  # provenance only
TEXT_VA, TEXT_RAW, TEXT_SIZE = 0x401000, 0x400, 1247232

# (name, VA, hex bytes)
PATTERNS = [
    ('wndproc_msg_switch', 0x4d366b, '83ff1cba010000000f8798000000747a'),
    ('wm_activate_input', 0x4d3697, '0fb77d1033c933db3bfb0f94c151e8a6120000'),
    ('wm_activate_inactive_stop_all', 0x4d36b1, '899e84040000891ddc8a6000e8ee4bfcff33c0'),
    ('wm_activate_active_arm', 0x4d36da, 'b801000000a3dc8a60008986840400'),
    ('wm_activateapp_input_only', 0x4d36f5, '33d23955100f94c252e84d12000083c40433c0'),
    ('pump_inactive_getmessage', 0x4d34bc, 'a1dc8a600085c00f85b3000000'),
    ('stop_all_head', 0x4982b0, 'a1446f60005333db3bc3'),
    ('stop_all_pause', 0x4982e3, '8b47743bc3740c8b088b512050ffd2'),
    ('stop_all_dsb_stop', 0x498304, '8b47443bc3740c8b088b514850ffd2'),
    ('stop_all_clear_and_status1', 0x498322, '8b0de485600083662cfd'),
    ('stop_all_callback', 0x498348, '8b4e146a0151ffd083c408895e18895e14'),
    ('save_entry_stop_all', 0x404555, '33ed57896c2414e84f3d0900'),
    ('save_caller', 0x403e07, '83a6a0040000fb'),
    ('save_call', 0x403e18, 'e813070000'),
    ('save_movi_writer_call', 0x404a0a, '51e8c03e0900'),
    ('mov_playmovie_handler', 0x49980a, '6a006aff6aff6aff6a006a006a0050518b4c243051e86cf4ffff'),
    ('mov_loadmovie_flags_override', 0x498735, '508b442410e801faffff'),
    ('play_found_seek', 0x498d51, '8bc657e8d776030083c40485c0'),
    ('play_run', 0x498d6f, '8bc6e8fa8a0300'),
    ('play_set_playing', 0x498d8c, '83c902094e2c'),
    ('seek_pause', 0x4d0476, '8b46748b088b512050ffd2'),
    ('seek_put_current_position', 0x4d04e1, '8b4670dd4424388b088b5120'),
    ('position_helper_get_current', 0x4d060f, '8b40708b088d142452508b4124ffd0'),
    ('pump_inactive_reports_end', 0x4d14eb, '392ddc8a6000'),
    ('pump_end_code', 0x4d15a0, '66b80200'),
    ('pump_finished_status1', 0x49845b, '8b4f146a0151ffd0'),
    ('audio_only_end_test', 0x4d17d1, '8b46708b088d54241052508b4124ffd0'),
    ('movi_restore_mask_playing', 0x498b93, '8b4424348b1183e0fd'),
    ('music_volume_class', 0x498285, 'f6462c8074138b0d346f60008b8174070000'),
    ('mod_volume_to_media', 0x49bffd, 'f700000800007405e8d6c5ffff'),
    ('stop_all_record_span', 0x4982d0, '8bf5f6462c028b6d00747e8b7e24395f047413'),
    ('x2_setpause_stop_all', 0x40705d, '838ea004000001e847120900e940ffffff'),
    ('pause_gates_universe_update', 0x403b09, 'f686a004000001'),
    ('unpause_clears_bit0', 0x4043dd, '8b88a004000083e1fe83c9028988a0040000'),
    ('p_leave_stop_all', 0x497bb1, 'e81aa2f6ffe8f5060000'),
    ('load_entry_stop_all', 0x404ced, 'e8be350900'),
    ('main_loop_entry_stop_all', 0x403878, 'e8334a0900'),
    # 0x498d4d is a whole 4-byte `add edi,[esp+0x2c]`; its operand byte 0x7c at
    # 0x498d4e decodes as a false `jl` into 0x498d71 in the raw scan (rejected below).
    ('play_start_add', 0x498d4d, '037c242c8bc657'),
]
# Raw-scan hits proven to be operand bytes of a containing instruction.
REJECTED_INTERIOR = {0x498d71: {'0x498d4e'}}

# (name, VA, expected dword)
TABLE_ENTRIES = [
    ('mov_table_LoadMovie', 0x499aec + 0 * 4, 0x4997dd),
    ('mov_table_PlayMovie', 0x499aec + 1 * 4, 0x499803),
    ('mov_table_StopMovie', 0x499aec + 3 * 4, 0x499875),
    ('mov_table_PlayMovieFrom', 0x499aec + 5 * 4, 0x499911),
    ('sfx_table_PlayMOD', 0x49c7bc + 5 * 4, 0x49c333),
    ('sfx_table_SetMODVolume', 0x49c7bc + 9 * 4, 0x49c3de),
    ('sfx_table_SetMusicType', 0x49c7bc + 31 * 4, 0x49c6ed),
    ('p_table_P_Leave', 0x498054 + 1 * 4, 0x497ba8),
]

# (module name table, index, expected name, case jump: (index-byte table or None, jump table, bias))
COMMANDS = [
    (0x57c8f0, 3, 'X2_Save', 0x4077a8, 0x40770c, 3, 0x406e28),
    (0x57c8f0, 9, 'X2_SetPause', 0x4077a8, 0x40770c, 3, 0x40705d),
    (0x57a390, 1, 'P_Leave', None, 0x498054, 0, 0x497ba8),
    (0x57a344, 1, 'MOV_PlayMovie', None, 0x499aec, 0, 0x499803),
]

CALLEES = {0x4982b0: 6, 0x4d0430: 3, 0x498c90: 3, 0x4d0600: 1, 0x4d1870: 2, 0x4d1810: 1}
HOOK_SITES = [0x4d36bd, 0x40455c, 0x498d54, 0x4d36da, 0x4982db, 0x498d71]


def main():
    data = EXE.read_bytes()
    sha = hashlib.sha256(data).hexdigest()
    text = data[TEXT_RAW:TEXT_RAW + TEXT_SIZE]

    def rd(va, n):
        if not TEXT_VA <= va < TEXT_VA + TEXT_SIZE:
            raise ValueError(hex(va))
        return text[va - TEXT_VA:va - TEXT_VA + n]

    failures = []
    if not exe_identity.identity_ok(data):
        failures.append('exe_identity')
    for name, va, hexbytes in PATTERNS:
        want = bytes.fromhex(hexbytes)
        if rd(va, len(want)) != want:
            failures.append(name)
    for name, va, value in TABLE_ENTRIES:
        if struct.unpack_from('<I', rd(va, 4))[0] != value:
            failures.append(name)

    def dw(va):
        for base_va, raw in ((0x400000 + 1519616, 1512448), (0x400000 + 1253376, 1248256),
                             (TEXT_VA, TEXT_RAW)):
            if va >= base_va:
                return struct.unpack_from('<I', data, va - base_va + raw)[0]

    def cstr(va):
        for base_va, raw in ((0x400000 + 1519616, 1512448), (0x400000 + 1253376, 1248256)):
            if va >= base_va:
                o = va - base_va + raw
                return data[o:data.index(b'\0', o)].decode('latin1')
    for table, index, name, idx_table, jump, bias, target in COMMANDS:
        slot = index - bias
        entry = data[idx_table - TEXT_VA + TEXT_RAW + slot] if idx_table else slot
        if cstr(dw(table + 4 * index)) != name or dw(jump + 4 * entry) != target:
            failures.append('command_' + name)

    callers = {t: [] for t in CALLEES}
    interior = {s: [] for s in HOOK_SITES}
    for i in range(len(text) - 6):
        va = TEXT_VA + i
        op = text[i]
        targets = []
        if op in (0xe8, 0xe9):
            targets.append((va + 5 + struct.unpack_from('<i', text, i + 1)[0]) & 0xffffffff)
        elif op == 0xeb or 0x70 <= op <= 0x7f:
            targets.append((va + 2 + struct.unpack_from('<b', text, i + 1)[0]) & 0xffffffff)
        elif op == 0x0f and 0x80 <= text[i + 1] <= 0x8f:
            targets.append((va + 6 + struct.unpack_from('<i', text, i + 2)[0]) & 0xffffffff)
        for t in targets:
            if op == 0xe8 and t in callers:
                callers[t].append(hex(va))
            for s in HOOK_SITES:
                if s < t < s + 5:
                    interior[s].append(hex(va))
    for s_, hits in interior.items():
        if set(hits) - REJECTED_INTERIOR.get(s_, set()):
            failures.append('interior_%x' % s_)
    for t, n in CALLEES.items():
        if len(callers[t]) != n:
            failures.append('callers_%x' % t)
    literal = {hex(t): data.count(struct.pack('<I', t)) for t in CALLEES}
    if any(literal.values()):
        failures.append('literal_refs')
    result = {
        'exe_sha256': sha, 'exe_info': exe_identity.info(data), 'patterns': len(PATTERNS), 'table_entries': len(TABLE_ENTRIES), 'commands': len(COMMANDS),
        'direct_callers': {hex(t): v for t, v in callers.items()},
        'literal_dword_refs': literal,
        'raw_branch_into_hook_site_interior': {hex(s): v for s, v in interior.items()},
        'rejected_operand_byte_hits': {hex(k): sorted(v) for k, v in REJECTED_INTERIOR.items()},
        'failures': failures, 'result': 'PASS' if not failures else 'FAIL',
    }
    print(json.dumps(result, indent=1))
    return 0 if not failures else 1


if __name__ == '__main__':
    sys.exit(main())
