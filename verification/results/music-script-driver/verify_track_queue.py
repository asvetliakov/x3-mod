#!/usr/bin/env python3
"""Read-only check for docs/reverse-engineering/music-script-driver.md §7-§8 (track queue: scheduler order of a sector
change, TI_Delay clock and SETA, deferral site contracts, alert exclusions).

EXE side (X3AP.exe, unmodified): pins the TI native group (registration, TI_Delay handler, timer insert with due =
game ms + delay + jitter, the per-frame TI tick and its due test, the TI_SetTimeWarpFactor / TI_GetRealTime fields),
the game clock advance 0x004d1df0 (QPC, real dt clamp 1..200 ms, 16.16 warp x throttle), the main-loop order (time and
TI tick before the scheduler pass before the media manager), the scheduler re-read of the runnable head, the wake and
suspend states, the task auto-id range, the zeroed native-group slot table, the MOV PlayMovie / FreeMovie handlers,
delete-by-id, the stop routine's lookup and caller completion, the manager's natural-end branch, the play routine's
find-or-create and context store, MOVI save / restore, LoadMovie reuse, the fork's return-cell zeroing and the call
opcodes' return cells; counts direct E8 callers; raw-scans .text for a rel8/rel32 branch into the interior of the two
five-byte call sites 0x0049981f and 0x00499865.

KC side (L/x3story.obj, addon/07.cat, Mayhem): the TI_SetTimeWarpFactor callers (SETA), the return IPs pushed by
LoopMusic -> PlayMusicWithFade and ChangeTrackMusic -> LoopMusic, the literal ids passed to ChangeTrackMusic.
Data side: TShips rows 0x10f/0x13f/0x140/0x141 in every addon catalogue that carries TShips.
Prints derived facts only. No Wine, no game.

    python3 -I verification/results/music-script-driver/verify_track_queue.py [X3 dir] > .../verify_track_queue_output.json
Exit status 0 only on PASS.
"""
import bisect
import gzip
import json
import os
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE.parent / 'ship-navigation-ai' / 'gate-queue'))
sys.path.insert(0, str(ROOT / 'verification' / 'probe'))
sys.path.insert(0, str(ROOT / 'tools' / 'analysis'))
from kc_obj import Obj, load_member  # noqa: E402
import exe_identity  # noqa: E402
from inspect_x3 import PE32, read_catalogue  # noqa: E402

X3 = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(os.environ.get('X3_DIR', '/mnt/c/X3'))
EXE = X3 / 'X3AP.exe'
TEXT_VA, TEXT_RAW, TEXT_SIZE = 0x401000, 0x400, 0x130630

PATTERNS = [
    # TI native group: dispatcher 0x004b12d0 registered with a NULL completion, names at 0x00579f80
    ('ti_register_dispatcher', 0x4b11ba, 'c704c8d0124b00'),
    ('ti_register_names', 0x4b11e7, 'c744c840809f5700'),
    # TI_Delay(ms) or (s, ms): 0x004b0b00(EAX = ms, slot, task id), returns 1 without a completion
    ('ti_delay_handler', 0x4b1321,
     '837c241c02 8b4301 57 751f 8b4c2418 69c0e8030000 034306 51 e8c1f7ffff b801000000 5f5e5d5b c21000'),
    ('ti_add_timer_reads_game_ms', 0x4b0b08, '8bf8 81ff00100000 a1346f6000 8ba818070000'),
    ('ti_add_timer_jitter_ms_div16_plus1', 0x4b0b24, '8bc7 99 83e20f 03c2 c1f804 83c001 0fbfc0'),
    ('ti_add_timer_due_and_tail_link', 0x4b0b9f,
     '8b542418 66895808 03ef 896810 89500c 8b4e08 894804 8b5608 5f 8902 8d4e04 5d 8908 894608 5b59 c20800'),
    ('ti_tick_advances_clock_then_reads_game_ms', 0x4b0e0d, 'e8ced30300 f6451c01 a1346f6000 8b8818070000 8b7d0c 894c2418'),
    ('ti_tick_due_test', 0x4b0eec, '8b4710 2b442418 85c0 7e0d 3bc3 7d05 8344241001 8b3f eb78'),
    ('ti_tick_fire_status_0', 0x4b0f29, '8b4f0c 6a00 51 ffd0'),
    ('ti_set_time_warp_factor_writes_cc', 0x4b148c, 'a1346f6000 8b5301 8b0de4856000 6a01 8990cc000000 51 8bc5'),
    ('ti_get_real_time_reads_720', 0x4b1577, '8b0d346f6000 8b9120070000 a1e4856000 52 50 8bc5'),
    # game clock 0x004d1df0
    ('clock_frame_advance_calls_4d1df0', 0x4ee1e1, 'e80a3cfeff'),
    ('clock_qpc', 0x4d1e02, 'ff15e4205300'),
    ('clock_real_ms_add_720', 0x4d1eb0, '01b120070000'),
    ('clock_override_step', 0x4d1ec3, 'a1988a6000 85c0 8915a01e5900 741e 018118070000'),
    ('clock_factor_cc_times_d0', 0x4d1ef0,
     '8b91d0000000 8b81cc000000 89742410 89542414 89442418 8b442418 8b542414 f7ea 0500800000 83d200 0facd010 85f6'),
    ('clock_dt_clamp_1_200', 0x4d1f1e, '85f6 89442418 7f0a c744241001000000 eb10 81fec8000000 7e08 c7442410c8000000'),
    ('clock_game_ms_add_718', 0x4d1f40,
     '8b442410 8b542418 f7ea 0500800000 83d200 0facd010 8b0d346f6000 018118070000'),
    # main loop: per-frame order (pause bit, clock, TI tick, msg pump, 0x0049a130, scheduler, media manager)
    ('main_loop_frame_order', 0x403ac5,
     'f686a004000001 7422 e81de30c00 a1346f6000 8b8818070000 8b9020070000 890d04916000 891508916000 '
     'e80bd30a00 e8b6f90c00 e831660900 e86cbc0900 e867480900'),
    ('main_loop_back_edge', 0x4041d2, '391d587c6000 0f84d2f8ffff'),
    ('pause_sets_bit0_then_stop_all', 0x40705d, '838ea004000001 e847120900'),
    # scheduler / VM
    ('scheduler_rereads_runnable_head', 0x49f79e, '85c0 7411 8b86d8120000 89863414000083380075dd'),
    ('wake_only_suspended_state2', 0x4a4740, '66837e3a02 0f8598000000'),
    ('script_completion_sets_continue_and_wakes', 0x4a4947,
     '57 8d7e28 66c746200100 803f08 7207 8bcf e8e3380000 8b4c2414 53 c60701 894f01 e8d3fdffff'),
    ('suspend_sets_state2', 0x4a437e, '8b442418 66c7463a0200'),
    ('task_auto_id_1_to_7ffffffe', 0x4efcc5, '8b4f08 8b17 8bd9 be01000000 8d4101 3dfeffff7f 894708 7603 897708'),
    ('vm_ctor_zeroes_32_slots', 0x49ced4, '8d4634 b920000000 8d642400 8958fc 8918 895804 89580c 895810 83c018 2bcf 75eb'),
    ('vm_ctor_script_slot_completion', 0x49cf2e, 'c744ce3410494a00'),
    ('native_stack_index_and_ip', 0x4a38c0,
     '8bd3 2b5614 b867666666 f7ea d1fa 8bc2 c1e81f 03c2 8b542418 894618 8bc7 2b4208 89461c'),
    ('fork_saves_parent_ip', 0x4a3602, '89411c'),
    ('fork_scans_for_tag3_return_cell', 0x4a3630, '803e03'),
    ('fork_copy_tests_tag3', 0x4a36c4, '803c1103'),
    ('fork_copy_zeroes_tag3_cells', 0x4a36e6, 'c60003 c7400100000000'),
    ('callcls_return_ip_overwrites_class_cell', 0x4a3d1d, '8bf3 83c204 52 e8f8480000'),
    ('callself_return_ip_new_cell', 0x4a3ee1, '83eb05'),
    ('push_return_ip_tag3', 0x4a862c, '8b442404 894601 c60603'),
    ('push_object_tag10', 0x4a864c, '85ff c6060a'),
    # MOV natives and media manager
    ('mov_play_handler_call_site', 0x49981f, 'e86cf4ffff 83c428 5f b801000000 5e c3'),
    ('mov_free_handler', 0x499859, '8b54241c 8b74240c 51 8b4a01 e886eeffff 83c404 5f b801000000 5e c3'),
    ('delete_by_id_first_with_media', 0x4986b0,
     'a1446f6000 56 8b30 8b06 85c0 7415 8bff 837e2400 7405 394e10 740c 8bf0 8b06 85c0 75ed 33c0 5e c3 '
     'e8f4fdffff b801000000 5e c3'),
    ('free_wrapper_completes_found', 0x4986f0,
     'e8bbffffff 8b0de4856000 85c9 7421 83790400 741b 83fe20 7d16 8d1476 8b4cd134 85c9 740b 50 8b442408 50 ffd1 '
     '83c408 c3'),
    ('stop_first_record_by_id', 0x498810, '8b0d446f6000 55 8b6c2408 56 8b31 833e00 740c 394610 7431 8b36 833e00 75f4'),
    ('stop_caller_completion_1', 0x4988a6, '74ae 8378040074a8 83ff20 7da3 8d0c7f 8b44c834 85c0 7498 6a01 55 ffd0 83c408 5e5d c3'),
    ('manager_saves_next_skips_unflagged', 0x498390, '8bfd 8b472c a802 8b6d00 0f8415010000'),
    ('manager_natural_end', 0x49842b,
     '395f10 0f8481000000 8b4f18 83e0fd 89472c a1e4856000 3bc3 7420 39580474 1b 83f920 7d16 8d1449 8b44d034 '
     '3bc3 740b 8b4f14 6a01 51 ffd0 83c408 895f18 895f14 eb47'),
    ('manager_error_frees_current', 0x4984ae, '8bf7 e81b000000'),
    ('play_find_or_create_retry', 0x498ca2,
     'a1446f6000 8b00 8bf0 833e00 740c 395e10 7468 8b36 833e00 75f4 6683ff02 751e 8b08 85c9 740d 395810 7413 '
     '8bc1 8b08 85c9 75f3 53 33c0 e863f4ffff 83c404 83ef01 6685ff 7fba'),
    ('play_stores_ctx_slot', 0x498dca, '8b4c241c 8b542418 894e14 895618 5f5e5d5b59 c3'),
    ('movi_restore_skips_101_299', 0x498b7e, '83fe64 7e08 81fe2c010000 7c5b'),
    ('movi_restore_creates_missing', 0x498bad, '56 e88df5ffff'),
    ('movi_writer_walks_every_record', 0x498955, '8b15446f6000 8b32 83c404 833e00 0f848e000000 8da42400000000 8bc6 8b4810 8b36'),
    ('load_reuses_existing_id', 0x4987ae, '8b0d446f6000 8b09 8b11 85d2 7411 8d642400 394110 741b'),
    ('load_reuse_completes_1', 0x498803, '6a01 53 ffd0'),
    # alert exclusions (TShips subtypes)
    ('alert_ship_subtype_exclusions', 0x42c8e1,
     '0fb74e48 6683f907 751c 0fb7464a 663d0f01 744b 663d4001 7445 663d4101 743f 663d3f01 7439'),
]
# direct E8 caller counts (None = only listed)
CALLEES = {0x4b0e00: 4, 0x49f770: None, 0x4d1df0: None, 0x4986b0: 1, 0x4986f0: 1, 0x498c90: 3, 0x498810: 3,
           0x498370: None, 0x4984d0: 6, 0x4b0b00: None}
HOOK_SITES = {0x49981f: 5, 0x499865: 5}
TI_TABLE = 0x4b15f8
TI_TABLE_WANT = {0: 0x4b1321, 9: 0x4b148c, 14: 0x4b1577}
TI_NAMES = {0: 'TI_Delay', 9: 'TI_SetTimeWarpFactor', 13: 'TI_SetAbsTime', 14: 'TI_GetRealTime'}


def main():
    data = EXE.read_bytes()
    text = data[TEXT_RAW:TEXT_RAW + TEXT_SIZE]
    pe = PE32(data)
    failures = []
    out = {'exe': str(EXE)}

    def rd(va, n):
        return text[va - TEXT_VA:va - TEXT_VA + n]

    def rdany(va, n):
        off = pe.offset(va - pe.base)
        return data[off:off + n]

    if not exe_identity.identity_ok(data):
        failures.append('exe_identity')
    for name, va, hx in PATTERNS:
        want = bytes.fromhex(hx.replace(' ', ''))
        if rd(va, len(want)) != want:
            failures.append('pattern_' + name)
    out['patterns'] = len(PATTERNS)
    for i, want in TI_TABLE_WANT.items():
        if struct.unpack('<I', rd(TI_TABLE + 4 * i, 4))[0] != want:
            failures.append('ti_table_%d' % i)
    for i, nm in TI_NAMES.items():
        p = struct.unpack('<I', rdany(0x579f80 + 4 * i, 4))[0]
        if rdany(p, len(nm) + 1) != nm.encode() + b'\0':
            failures.append('ti_name_%d' % i)
    iat = {v['iat_va']: v['name'] for vals in pe.imports().values() for v in vals}
    out['iat_0x005320e4'] = iat.get('0x005320e4')
    if out['iat_0x005320e4'] != 'QueryPerformanceCounter':
        failures.append('iat_qpc')

    callers = {t: [] for t in CALLEES}
    interior = {s: [] for s in HOOK_SITES}
    for i in range(len(text) - 6):
        va, op = TEXT_VA + i, text[i]
        targets = []
        if op in (0xe8, 0xe9):
            targets.append((va + 5 + struct.unpack_from('<i', text, i + 1)[0]) & 0xffffffff)
        elif op == 0xeb or 0x70 <= op <= 0x7f:
            targets.append((va + 2 + struct.unpack_from('<b', text, i + 1)[0]) & 0xffffffff)
        elif op == 0x0f and 0x80 <= text[i + 1] <= 0x8f:
            targets.append((va + 6 + struct.unpack_from('<i', text, i + 2)[0]) & 0xffffffff)
        for t in targets:
            if op == 0xe8 and t in callers:
                callers[t].append('0x%08x' % va)
            for s, n in HOOK_SITES.items():
                if s < t < s + n:
                    interior[s].append('0x%08x' % va)
    out['callers'] = {'0x%08x' % t: v for t, v in callers.items()}
    for t, n in CALLEES.items():
        if n is not None and len(callers[t]) != n:
            failures.append('callers_%x' % t)
    if callers[0x4b0e00] != ['0x00403a6b', '0x00403a84', '0x00403af0', '0x00497287']:
        failures.append('ti_tick_callers')
    if '0x00403aff' not in callers[0x49f770] or '0x00403b04' not in callers[0x498370]:
        failures.append('main_loop_scheduler_manager')
    out['hook_site_interior_branches'] = {'0x%08x' % s: v for s, v in interior.items()}
    for s, hits in interior.items():
        if hits:
            failures.append('interior_%x' % s)
    out['hook_site_qword_offset'] = {'0x%08x' % s: s & 7 for s in list(HOOK_SITES) + [0x498c90, 0x498810, 0x4982b0]}

    # KC (Mayhem object)
    raw = load_member(X3 / 'addon/07.cat')
    o = Obj(raw)
    if o.build != 1462141839:
        failures.append('kc_identity')
    entries = o.entries

    def owner(pc):
        en = entries[bisect.bisect_right(entries, pc) - 1]
        return '%d::%s' % o.meth[en][0][:2]
    allpc = list(o.all_insns())
    warp = sorted({owner(pc) for pc in allpc if o.code[pc] == 0x82 and o.operand(pc) == 'TI_SetTimeWarpFactor'})
    out['kc_ti_set_time_warp_factor_callers'] = warp
    if '150::Vbi' not in warp or '150::StopFastForward' not in warp:
        failures.append('kc_seta_warp')
    # StopFastForward resets the factor to 65536 (1.0 in 16.16)
    sff = o.find(150, 'StopFastForward')
    seq = [o.line(pc).split('  ')[-1].strip() for pc in o.insns(sff[0])] if sff else []
    out['kc_stop_fast_forward_tail'] = seq[-6:-2]
    if 'pushd(07) 65536' not in seq:
        failures.append('kc_stop_fast_forward')
    # return IPs: LoopMusic callself PlayMusicWithFade at 0x0dc7f, ChangeTrackMusic callcls LoopMusic at 0x0dcd6
    ret = {}
    for pc, want in ((0x0dc7f, '200::PlayMusicWithFade'), (0x0dcd6, '200::LoopMusic')):
        if pc not in allpc or o.operand(pc) != want:
            failures.append('kc_call_%05x' % pc)
        ret['%05x' % pc] = '%05x' % (pc + o.width(pc))
    out['kc_return_ips'] = ret
    if ret != {'0dc7f': '0dc84', '0dcd6': '0dcdb'}:
        failures.append('kc_return_ips')
    # literal ids passed to ChangeTrackMusic
    lits = {}
    for i, pc in enumerate(allpc):
        if o.code[pc] == 0x86 and o.operand(pc) == '200::ChangeTrackMusic':
            arg = allpc[i - 3]
            c = o.code[arg]
            val = {0x01: 0, 0x02: 1, 0x03: 2, 0x04: 3}.get(c)
            if c in (0x05, 0x06, 0x07):
                val = int(o.operand(arg))
            lits['%05x %s' % (pc, owner(pc))] = val
    out['kc_change_track_music_literal_ids'] = lits
    if sorted(v for v in lits.values() if v is not None) != [0, 0, 0, 13, 14, 8233, 8234, 8235, 8236, 8237]:
        failures.append('kc_change_track_music_ids')

    # TShips rows named by the alert exclusions
    ships = {}
    for cat in sorted((X3 / 'addon').glob('*.cat')):
        for e in read_catalogue(cat):
            if e['path'].lower() == 'addon/types/tships.pck':
                with open(cat.with_suffix('.dat'), 'rb') as f:
                    f.seek(e['offset'])
                    blob = bytes(b ^ 0x33 for b in f.read(e['size']))
                rows = [ln for ln in gzip.decompress(blob).decode('latin1').splitlines()
                        if ln.strip() and not ln.lstrip().startswith('/')][1:]
                ships[cat.name] = {hex(i): rows[i].split(';')[-2] for i in (0x10f, 0x13f, 0x140, 0x141)}
    out['tships_excluded_rows'] = ships
    want_rows = {'0x10f': 'SS_SH_MINE_1', '0x13f': 'SS_SH_TR_MINE', '0x140': 'SS_SH_MINE_2', '0x141': 'SS_SH_MINE_3'}
    if not ships or any(v != want_rows for v in ships.values()):
        failures.append('tships_rows')

    out['failures'] = failures
    out['result'] = 'PASS' if not failures else 'FAIL'
    print(json.dumps(out, indent=1, sort_keys=True))
    return 0 if not failures else 1


if __name__ == '__main__':
    sys.exit(main())
