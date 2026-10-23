#!/usr/bin/env python3
"""Read-only check for docs/reverse-engineering/music-script-driver.md.

EXE side (X3AP.exe, unmodified): pins the instruction bytes of the engine alert level (0x0042c890: hostile scan,
5 km / 10 km Chebyshev hysteresis, missile level 2, the three NotifyAlert by-name calls), its single call chain from
the main loop, the KC fork opcode (child linked at the tail and run at once), the native-call IP save and suspend
test, the wake path (head link), MOV_StopMovie / MOV_FreeMovie natives and the four AttackedBy notifications; counts
direct E8 callers; raw-scans .text for a rel8/rel32 branch into the interior of the candidate hook site 0x0042c974.

KC side (L/x3story.obj from addon/07.cat, the Mayhem object this install loads; stock addon/04.cat compared): pins
the decoded instruction windows of 200::PlayMusicWithFade (stop, wait, load, play, replay loop, free),
LoopMusic, ChangeTrackMusic (fork), ChangeTrackToSectorMusic, ChangeTrackToCombatMusic (combat id list),
WaitForEndOfTrack, EnvironmentToMusicTrack, 150::NotifyAlert / IsAlert, 402::EnterSector / WarpEnterSector, and the
caller census of the music entry points. Prints decoded text only, no script bytes. No Wine, no game.

    python3 -I verification/results/music-script-driver/verify_music_script_driver.py [X3 dir] > .../verify_output.json
Exit status 0 only on PASS.
"""
import bisect
import hashlib
import json
import os
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE.parent / 'ship-navigation-ai' / 'gate-queue'))
sys.path.insert(0, str(ROOT / 'verification' / 'probe'))
from kc_obj import Obj, load_member  # noqa: E402
import exe_identity  # noqa: E402

X3 = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(os.environ.get('X3_DIR', '/mnt/c/X3'))
EXE = X3 / 'X3AP.exe'
TEXT_VA, TEXT_RAW, TEXT_SIZE = 0x401000, 0x400, 0x130630

# (name, VA, hex bytes): all inside .text
PATTERNS = [
    # alert level 0x0042c890 (stdcall, one arg = cockpit registry *0x00608504)
    ('alert_head_player_ship', 0x42c890, 'a10c85600083ec0853555657 8b7838 33ed 85ff'),
    ('alert_parent_is_sector', 0x42c8a9, '8b4754 85c0 0f84b1000000 66837848 01'),
    ('alert_32_class_lists', 0x42c8bf, '8b4050 89442410 c744241420000000'),
    ('alert_hostile_test', 0x42c8d6, '8bc6 e8b33f0200 84c0 745f'),
    ('alert_ship_subtype_exclusions', 0x42c8e1,
     '0fb74e48 6683f907 751c 0fb7464a 663d0f01 744b 663d4001 7445 663d4101 743f 663d3f01 7439'),
    ('alert_missile_on_player', 0x42c907, '6683f90a 7508 8b4e50 397910 7435'),
    ('alert_range_hysteresis', 0x42c915,
     '8b54241c 837a0400 8bc6 8bd7 750c e858c9feff 3da0252600 eb0a e84cc9feff 3d404b4c00 7d05 bd01000000'),
    ('alert_level2', 0x42c94a, 'bd02000000'),
    ('alert_merge_pending', 0x42c965, '8b74241c 0fbfc5 3b4608 7e03 894608'),
    ('alert_transition_site', 0x42c974, '8b4e08 8b4604 3bc8 0f84ce000000'),
    ('notify_alert_1_from_0_to_2', 0x42c998, '6a016a016a01 68f0b75500 6896000000 6a006a00 e80f2b0700'),
    ('notify_alert_1_from_0_to_1', 0x42c9e2, '6a016a016a01 68f0b75500 6896000000 6a006a00 e8c52a0700'),
    ('notify_alert_0', 0x42ca2e, '6a006a016a01 68f0b75500 6896000000 6a006a00 e8792a0700'),
    ('alert_commit_and_clear', 0x42ca4a, '8b4608 894604 5f c7460800000000'),
    ('registry_walk_calls_alert', 0x41cde0, '53 8b1d04856000 5657 53 e8a1fa0000'),
    ('main_loop_calls_registry_walk', 0x403f2f, 'e8ac8e0100'),
    ('missile_update_targets_player', 0x453405, '8b4840 d1e9 f6c101 740b c784243c03000001000000'),
    ('missile_update_raises_level2', 0x459451, '8b0d04856000 f7de 1bf6 83e602 0fb7c6 0fbfc0 3b4108 7e03 894108'),
    ('hostile_fn_head', 0x450890, '56 8bf0 85f6 750b'),
    ('hostile_fn_player_relation', 0x4508dd, '8bc6 8bf7 e82affffff 33c9 85c0 0f9cc1'),
    ('relation_foe_mask', 0x45087a, '8b80d0010000 23c2 f7d8 1bc0 c3'),
    ('chebyshev_distance', 0x419280, '8b4870 8b5270 8b4230 83c230 83c130 2b01'),
    # KC VM
    ('fork_link_tail_and_run_child', 0x4a373b,
     '8b81e0120000 894604 8b91e0120000 8932 8d81dc120000 8906 89b1e0120000 89b134140000 e83befffff'),
    ('fork_child_result_0_parent_task_id', 0x4a3725, 'c60001 c7400100000000 c60301 895301 66c7463a0100'),
    ('native_clears_continue_flag', 0x4a38a5, '66c746200000'),
    ('native_saves_ip_after_call', 0x4a38dc, '8bc7 2b4208 89461c'),
    ('native_call_site', 0x4a38ff, '8b8254140000 56 50 ffd1 83c414'),
    ('native_suspend_test', 0x4a3914, '66837e2001 0f855f0a0000'),
    ('wake_links_at_head', 0x4a47c5, '8b442410 8b90d8120000 05d8120000 8916'),
    # MOV natives
    ('mov_play_movie_start0_no_loop', 0x499803, '8b54241c 8b4201 6a00 6aff6aff6aff 6a006a006a00 50 51'),
    ('mov_stop_movie_handler', 0x499875, '8b44241c 8b4001 8b7c240c 51 e88aefffff 83c404 5f b801000000 5e c3'),
    ('stop_movie_not_found_completes_caller_0', 0x49884e, '6a00 55 ffd0'),
    ('stop_movie_found_pause', 0x498859, '83662cfd 8bc6 e8ac8f0300'),
    ('stop_movie_record_completion_1', 0x498886, '8b4614 6a01 50 ffd1'),
    ('stop_movie_clear_context', 0x498898, 'c7461800000000 c7461400000000'),
    ('stop_movie_caller_completion_1', 0x4988be, '6a01 55 ffd0'),
    ('mov_free_movie_handler', 0x499859, '8b54241c 8b74240c 51 8b4a01 e886eeffff'),
    ('free_record_destroys_media', 0x498514, '8b4624 85c0 7405 e820980300'),
    # AttackedBy by-name notifications (string 0x0055fd9c)
    ('attacked_by_1', 0x45e45f, '689cfd5500'), ('attacked_by_1_call', 0x45e46b, 'e850100400'),
    ('attacked_by_2', 0x45e650, '689cfd5500'), ('attacked_by_2_call', 0x45e65c, 'e85f0e0400'),
    ('attacked_by_3', 0x45e9b5, '689cfd5500'), ('attacked_by_3_call', 0x45e9c1, 'e8fa0a0400'),
    ('attacked_by_4', 0x45e9e3, '689cfd5500'), ('attacked_by_4_call', 0x45e9ef, 'e8cc0a0400'),
]
STRINGS = [(0x55b7f0, 'NotifyAlert'), (0x55fd9c, 'AttackedBy')]
# KC opcode -> handler (two-level table 0x004a4688 -> 0x004a4490)
OPCODES = [(0x6f, 0x4a35f6), (0x70, 0x4a4301), (0x82, 0x4a3880)]
CALLEES = {0x42c890: 1, 0x41cde0: 1, 0x4986f0: 1, 0x498810: 3, 0x4d0600: 1, 0x450890: None, 0x49f4c0: None}
HOOK_SITES = {0x42c974: 6}

# KC windows on the Mayhem object: (name, start, end, decoded text)
KC = [
    ('pmwf_volume_gate', 0x0da8e, 0x0daa3,
     'pushw 200; callcls 200::GetMODVolume; pushb 5; le; jf 0daa3; push0; stloc 8; drop'),
    ('pmwf_same_id_and_busy', 0x0daa3, 0x0dae6,
     'ldmem 17; ldloc 8; eq; jf 0dab4; dropn 3; push0; ret; ldmem 17; push0; lt; jf 0dae6; ldmem 0; push1; sub; '
     'stmem 0; drop; ldmem 0; push0; le; jf 0dadc; push0; stmem 17; drop; pushb 5; stmem 0; drop; dropn 3; push0; '
     'ret; jmp 0daec'),
    ('pmwf_mark_stopping', 0x0daec, 0x0db1e,
     'ldmem 17; push0; gt; jf 0db85; ldmem 17; stloc 2; drop; pushb -1; stmem 17; drop; ldloc 7; push0; eq; '
     'jf 0db1d; ldloc 8; push0; gt; jf 0db1d; push1; jmp 0db1e; push0'),
    ('pmwf_stop_old_then_wait', 0x0db4b, 0x0db85,
     'ldloc 1; push1; native MOV_StopMovie; drop; pushb 10; ldmem 17; pushb -1; eq; jf 0db84; ldloc 1; push1; sub; '
     'stloc 2; push0; le; jf 0db76; dropn 4; push0; ret; pushb 100; push1; native TI_Delay; drop; jmp 0db57; drop'),
    ('pmwf_set_current_and_load', 0x0db85, 0x0dbb4,
     'ldloc 7; stmem 17; drop; push0; stmem 18; drop; ldloc 7; push0; gt; jf 0dc71; push0; ldmem 17; stloc 2; '
     'drop; pushw 144; ldmem 17; push2; native MOV_LoadMovie; jf 0dc6b'),
    ('pmwf_play', 0x0dbe8, 0x0dc02,
     'push1; jf 0dc6b; ldmem 17; push1; native MOV_PlayMovie; not; jf 0dc02; push0; stloc 11; drop'),
    ('pmwf_loop_test', 0x0dc02, 0x0dc37,
     'ldmem 17; ldloc 9; ne; jt 0dc1c; ldmem 18; jt 0dc1c; push0; jmp 0dc1d; push1; jt 0dc31; ldloc 10; not; '
     'jt 0dc31; push0; jmp 0dc32; push1; jf 0dc66'),
    ('pmwf_exit_free_and_replay', 0x0dc37, 0x0dc6b,
     'ldloc 1; push1; native MOV_FreeMovie; drop; push0; stmem 18; drop; ldmem 17; pushb -1; eq; jf 0dc5c; '
     'pushb -2; stmem 17; drop; jmp 0dc61; push0; stmem 17; drop; dropn 4; push0; ret; jmp 0dbe8'),
    ('loop_music', 0x0dc76, 0x0dc87,
     'frame 5; push1; push0; ldloc 6; push3; callself 200::PlayMusicWithFade; drop; push0; ret'),
    ('change_track_music_fork', 0x0dcc6, 0x0dcdf,
     'frame 3; op6f; jt 0dcdd; ldloc 4; push1; pushw 200; callcls 200::LoopMusic; drop; op70; push0; ret'),
    ('sector_music_track', 0x0dcf6, 0x0dd40,
     'ldglob 10; jf 0dd0c; push0; ldglob 10; callobj GetEnvironment; jmp 0dd0d; push0; push0; ldloc 2; push2; '
     'pushw 3001; callcls 3001::GetEnvironmentTrack; ldloc 1; not; jf 0dd33; ldloc 2; push1; pushw 200; '
     'callcls 200::EnvironmentToMusicTrack; stloc 2; drop; ldloc 1; push1; pushw 200; callcls 200::ChangeTrackMusic; '
     'drop'),
    ('combat_music_track', 0x0dd4c, 0x0dd7b,
     'callcls 150::GetClientState; pushw 256; and; jf 0dd5c; push0; ret; push0; ldglob 9; callobj GetSector; push1; '
     'ldloc 2; push2; pushw 3001; callcls 3001::GetEnvironmentTrack; ldloc 1; not; jf 0de33'),
    ('combat_music_pick', 0x0ddf9, 0x0de45,
     'mkarr 42; ldmem 17; ldloc 2; push2; native SE_ArrayFind; push0; ge; jf 0de1b; ldmem 17; stloc 3; drop; '
     'jmp 0de32; ldloc 1; push1; native SE_ArraySize; push1; native SE_Random; ldloc 2; index; stloc 3; drop; drop; '
     'ldloc 1; push1; pushw 200; callcls 200::ChangeTrackMusic; drop; dropn 2; push0; ret'),
    ('wait_for_end_of_track', 0x0de68, 0x0de95,
     'frame 2; ldmem 17; ldloc 5; eq; jf 0de93; push1; stmem 18; drop; ldmem 18; jf 0de93; pushw 210; push1; '
     'native TI_Delay; drop; jmp 0de7c; push0; ret'),
    ('environment_to_track_sector', 0x0deff, 0x0df2a,
     'pushw 2001; ldloc 5; push2; native SE_IsClass; jf 0df5f; push0; ldloc 5; callobj GetMusicTrack; push0; gt; '
     'jf 0df2a; push0; ldloc 5; callobj GetMusicTrack; ret'),
    ('environment_to_track_hash', 0x0df3c, 0x0df5c,
     'ldloc 2; pushb 103; mul; ldloc 2; pushb 37; mul; add; pushb 5; add; pushb 22; mod; ldloc 1; pushb 19; add; '
     'stloc 4; dropn 3; ret'),
    ('environment_to_track_other', 0x0df5f, 0x0df6c, 'pushw 2001; pushb 4; push1; native SE_Random; add; ret'),
    ('notify_alert_on', 0x176a7, 0x176cc,
     'ldloc 4; jf 176cc; ldmem 10; not; jf 176c7; push1; stmem 10; drop; push0; pushw 200; '
     'callcls 200::ChangeTrackToCombatMusic; drop; jmp 17738'),
    ('notify_alert_off', 0x176cc, 0x176e3,
     'ldmem 10; jf 17738; push0; stmem 10; drop; push0; pushw 200; callcls 200::ChangeTrackToSectorMusic; drop'),
    ('is_alert', 0x1773a, 0x17741, 'frame 1; ldmem 10; ret'),
    ('sector_music_id_member', 0x9e206, 0x9e20d, 'frame 1; ldmem 32; ret'),
    ('enter_sector_music', 0xfb4a2, 0xfb4da,
     'op6f; jt fb4da; pushw 1000; push1; native TI_Delay; drop; push0; pushw 150; callcls 150::IsAlert; jf fb4cf; '
     'push0; pushw 200; callcls 200::ChangeTrackToCombatMusic; drop; jmp fb4d9; push0; pushw 200; '
     'callcls 200::ChangeTrackToSectorMusic; drop; op70'),
    ('warp_enter_sector_music', 0xfb539, 0xfb571,
     'op6f; jt fb571; pushw 1000; push1; native TI_Delay; drop; push0; pushw 150; callcls 150::IsAlert; jf fb566; '
     'push0; pushw 200; callcls 200::ChangeTrackToCombatMusic; drop; jmp fb570; push0; pushw 200; '
     'callcls 200::ChangeTrackToSectorMusic; drop; op70'),
    ('env_track_default_zero', 0x1a20d2, 0x1a20d7, 'frame 1; push0; ret'),
]
KC_ENTRIES = {(200, 'PlayMusicWithFade'): 0x0da87, (200, 'LoopMusic'): 0x0dc76, (200, 'ChangeTrackMusic'): 0x0dcc6,
              (200, 'ChangeTrackToSectorMusic'): 0x0dcdf, (200, 'ChangeTrackToCombatMusic'): 0x0dd45,
              (200, 'ChangeTrackMusicDelayed'): 0x0de45, (200, 'PlayMusicToEnd'): 0x0dcab,
              (200, 'WaitForEndOfTrack'): 0x0de68, (200, 'EnvironmentToMusicTrack'): 0x0deeb,
              (150, 'NotifyAlert'): 0x1769a, (150, 'IsAlert'): 0x1773a, (402, 'EnterSector'): 0xfb41c,
              (402, 'WarpEnterSector'): 0xfb4e6}
# callers by CODE entry or by name (op 82/84..88); None = only counted
CENSUS = {'ChangeTrackToCombatMusic': 18, 'ChangeTrackToSectorMusic': 23, 'ChangeTrackMusic': 14,
          'NotifyAlert': 0, 'WaitForEndOfTrack': 0, 'PlayMusicToEnd': 0, 'ChangeTrackMusicDelayed': 0,
          'IsAlert': 21, 'PlayMusicWithFade': 6}
CLASS200_NATIVES = {'MOV_SetVolumeMovie', 'MOV_StopMovie', 'MOV_LoadMovie', 'MOV_PlayMovie', 'MOV_FreeMovie'}


def main():
    data = EXE.read_bytes()
    text = data[TEXT_RAW:TEXT_RAW + TEXT_SIZE]
    failures = []

    def rd(va, n):
        return text[va - TEXT_VA:va - TEXT_VA + n]

    def rdany(va, n):
        for sva, size, raw in ((0x401000, 0x130630, 0x400), (0x532000, 0x4074d, 0x130c00), (0x573000, 0xefb58, 0x171400)):
            if sva <= va < sva + size:
                return data[raw + va - sva:raw + va - sva + n]
        raise ValueError(hex(va))
    if not exe_identity.identity_ok(data):
        failures.append('exe_identity')
    for name, va, hx in PATTERNS:
        want = bytes.fromhex(hx.replace(' ', ''))
        if rd(va, len(want)) != want:
            failures.append('pattern_' + name)
    for va, s in STRINGS:
        if rdany(va, len(s) + 1) != s.encode() + b'\0':
            failures.append('string_' + s)
    for op, handler in OPCODES:
        idx = rdany(0x4a4688 + op - 1, 1)[0]
        if struct.unpack('<I', rdany(0x4a4490 + 4 * idx, 4))[0] != handler:
            failures.append('opcode_%02x' % op)
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
    for t, n in CALLEES.items():
        if n is not None and len(callers[t]) != n:
            failures.append('callers_%x' % t)
    for s, hits in interior.items():
        if hits:
            failures.append('interior_%x' % s)
    alert_notify_calls = [c for c in callers[0x49f4c0] if 0x42c890 <= int(c, 16) < 0x42ca61]
    if alert_notify_calls != ['0x0042c9ac', '0x0042c9f6', '0x0042ca42']:
        failures.append('alert_notify_calls')

    # KC
    builds = {}
    kc = {}
    for label, cat in (('mayhem', X3 / 'addon/07.cat'), ('stock', X3 / 'addon/04.cat')):
        raw = load_member(cat)
        builds[label] = (raw, Obj(raw))
    raw, o = builds['mayhem']
    kc['mayhem_sha256'] = hashlib.sha256(raw).hexdigest()
    kc['mayhem_build_id'] = o.build
    if not kc['mayhem_sha256'].startswith('2c3101a8') or o.build != 1462141839:
        failures.append('kc_mayhem_identity')
    for (cid, nm), e in KC_ENTRIES.items():
        if o.find(cid, nm) != [e]:
            failures.append('kc_entry_%d_%s' % (cid, nm))
    for name, a, b, want in KC:
        got = o.decoded(a, b - a)
        if got != want:
            failures.append('kc_' + name)
            kc['mismatch_' + name] = got
    # combat id list = 1001..1042 (pushw cells before mkarr 42)
    e = KC_ENTRIES[(200, 'ChangeTrackToCombatMusic')]
    ins = list(o.insns(e))
    k = ins.index(0x0ddf9)
    vals = sorted(int(o.operand(p)) for p in ins[k - 42:k] if o.code[p] == 0x06)
    kc['mayhem_combat_ids'] = '%d..%d (%d)' % (vals[0], vals[-1], len(vals)) if vals else 'none'
    if vals != list(range(1001, 1043)):
        failures.append('kc_combat_ids')
    entries = o.entries

    def owner(pc):
        en = entries[bisect.bisect_right(entries, pc) - 1]
        return '%d::%s' % o.meth[en][0][:2]
    census = {n: [] for n in CENSUS}
    class200_natives = set()
    for pc in o.all_insns():
        op = o.code[pc]
        if op in (0x82, 0x84, 0x85, 0x86, 0x87, 0x88):
            nm = o.operand(pc).split('::')[-1]
            if nm in census:
                census[nm].append(pc)
            if op == 0x82 and owner(pc).startswith('200::') and o.operand(pc).startswith('MOV_') and \
                    0x0d9b8 <= pc < 0x0de95:  # MusicFadeOut .. WaitForEndOfTrack
                class200_natives.add(o.operand(pc))
    kc['caller_counts'] = {n: len(v) for n, v in census.items()}
    for n, want in CENSUS.items():
        if want is not None and len(census[n]) != want:
            failures.append('kc_census_' + n)
    kc['music_method_mov_natives'] = sorted(class200_natives)
    if class200_natives != CLASS200_NATIVES:
        failures.append('kc_class200_natives')
    # stock comparison: opcode sequences of the shared methods
    _, s = builds['stock']
    cmp = {}
    for (cid, nm) in KC_ENTRIES:
        a, b = s.find(cid, nm), o.find(cid, nm)
        A = [s.code[p] for p in s.insns(a[0])]
        B = [o.code[p] for p in o.insns(b[0])]
        cmp['%d::%s' % (cid, nm)] = 'stock %05x opseq %s' % (a[0], 'equal' if A == B else 'differs (%d/%d)' % (len(A), len(B)))
    kc['stock_compare'] = cmp
    es = s.find(200, 'ChangeTrackToCombatMusic')[0]
    sins = list(s.insns(es))
    mk = [p for p in sins if s.code[p] == 0x28]
    sv = sorted(int(s.operand(p)) for p in sins[sins.index(mk[0]) - int(s.operand(mk[0])):sins.index(mk[0])]
                if s.code[p] in (0x05, 0x06)) if mk else []
    kc['stock_combat_ids'] = '%d..%d (%d)' % (sv[0], sv[-1], len(sv)) if sv else 'none'

    result = {
        'exe': str(EXE), 'exe_sha256': hashlib.sha256(data).hexdigest(), 'exe_info': exe_identity.info(data),
        'patterns': len(PATTERNS), 'strings': len(STRINGS), 'opcodes': len(OPCODES),
        'direct_callers': {'0x%08x' % t: (v if len(v) <= 8 else '%d callers' % len(v)) for t, v in callers.items()},
        'alert_notify_alert_calls': alert_notify_calls,
        'raw_branch_into_hook_site_interior': {'0x%08x' % s: v for s, v in interior.items()},
        'kc_windows': len(KC), 'kc': kc,
        'failures': failures, 'result': 'PASS' if not failures else 'FAIL',
    }
    print(json.dumps(result, indent=1))
    return 0 if not failures else 1


if __name__ == '__main__':
    sys.exit(main())
