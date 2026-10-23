#!/usr/bin/env python3
"""The track queue's KC patch window (docs/architecture/music-queue.md; music-script-driver.md section 8.4 "Prerequisite").

Reads the story object this install loads (Mayhem's L/x3story.obj in addon/07.cat, build 1462141839) and checks what
src/proxy/music_keep_core.h pins for the in-memory patch of 150::WarpToSector: the method entry, the 6-byte window
`op6f; jt <continue>` at CODE 0x161c3 (file bytes big-endian, the memory form with the operand little-endian after the
loader's byte-swap pass 0x0049e1a0), the continuation (an instruction start of the method, `ldloc`), the fork child's
StopMusic(500) body behind the window, that no jump in the whole CODE lands inside the window and no method entry does,
and that the patched object (`jmp <continue>` + one filler byte) keeps every instruction start outside the window,
every method entry, the method's decode and its symbolic stack depths (0 conflicts, the depth at the continuation
unchanged). Stock AP (addon/04.cat) is decoded for comparison only: its build is not accepted by the proxy. No bytes
are written anywhere. Prints one JSON object (also written beside this script) and exits 0 only on PASS.

    python3 -I verification/results/music-script-driver/verify_music_queue_kc.py [X3 dir] > .../verify_music_queue_kc_output.json
"""
import importlib.util
import json
import re
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE.parent / 'ship-navigation-ai' / 'gate-queue'))
from kc_obj import Obj, load_member  # noqa: E402

X3 = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('/mnt/c/X3')
CORE = ROOT / 'src/proxy/music_keep_core.h'
OUTPUT = HERE / 'verify_music_queue_kc_output.json'
JUMPS = (0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a)


def load_probe():
    spec = importlib.util.spec_from_file_location('verify_music_keep_writes', ROOT / 'verification/results/music-restart/verify_music_keep_writes.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def study(raw, consts, windows, accepted):
    """Checks of one story object; `accepted`: the build the proxy binds to."""
    o = Obj(raw)
    fork, cont, entry = consts['warp_fork_offset'], consts['warp_continue_offset'], consts['warp_method_entry']
    length = consts['warp_window_length']
    out = {'build': o.build, 'code_size': len(o.code), 'checks': {}}
    checks = out['checks']
    entries = o.find(150, 'WarpToSector')
    out['warp_to_sector_entries'] = ['%05x' % e for e in entries]
    checks['method_entry'] = entries == [entry]
    ins = list(o.insns(entry)) if entries == [entry] else []
    starts = set(ins)
    file_window = o.code[fork:fork + length]
    memory_window = bytes([file_window[0], file_window[1]]) + file_window[2:6][::-1]
    out['file_window'] = file_window.hex()
    out['memory_window'] = memory_window.hex()
    checks['window_is_fork_then_jt'] = (len(file_window) == length and file_window[0] == consts['kc_op_fork'] and file_window[1] == consts['kc_op_jt']
                                        and struct.unpack('>I', file_window[2:6])[0] == cont and fork in starts and fork + 1 in starts and fork + 6 in starts)
    checks['memory_form_pinned'] = memory_window == windows['warp_window_memory']
    patch_memory = bytes([consts['kc_op_jmp']]) + struct.pack('<I', cont) + bytes([consts['kc_op_filler']])
    checks['patch_form_pinned'] = patch_memory == windows['warp_patch_memory']
    # The fork child's body behind the window: pushw 500; push1; pushw 200; callcls 200::StopMusic; drop; op70, then the continuation.
    child = [re.sub(r'^.*w\d+\s+', '', o.line(pc)).strip() for pc in ins if fork + length <= pc < cont] if ins else []
    out['fork_child'] = child
    checks['fork_child_is_stop_music_500'] = (len(child) == 6 and child[0] == 'pushw(06) 500' and child[1] == 'push1(02)' and child[2] == 'pushw(06) 200'
                                              and child[3] == 'callcls(86) 200::StopMusic' and child[4] == 'drop(24)' and child[5] == 'op(70)')
    checks['continuation_is_ldloc'] = cont in starts and o.code[cont] == 0x0d
    # No jump in the whole CODE lands inside the window (fork+1..fork+5) and no method entry does.
    allpc = list(o.all_insns())
    into = ['%05x' % pc for pc in allpc if o.code[pc] in JUMPS and fork < struct.unpack_from('>I', o.code, pc + 1)[0] <= fork + length - 1]
    onto = ['%05x' % pc for pc in allpc if o.code[pc] in JUMPS and struct.unpack_from('>I', o.code, pc + 1)[0] == fork]
    out['jumps_into_window_interior'] = into
    out['jumps_onto_window_start'] = onto
    checks['no_branch_into_interior'] = not into
    checks['no_method_entry_inside'] = not any(fork < e <= fork + length - 1 for e in o.entries)
    # The patched object: instruction starts outside the window unchanged, every method entry a start, the method decodes to the
    # same end, the patched window decodes as `jmp cont; push0`, the filler is unreached (no jump onto it, the jmp before it).
    patched = bytearray(raw)
    file_off = o.code_file_off + fork
    patched[file_off:file_off + length] = bytes([consts['kc_op_jmp']]) + struct.pack('>I', cont) + bytes([consts['kc_op_filler']])
    q = Obj(bytes(patched))
    q_ins = list(q.insns(entry))
    q_all = set(q.all_insns())
    old_all = set(allpc)
    outside = lambda s: {pc for pc in s if not fork <= pc < fork + length}
    checks['length_header_build_unchanged'] = len(patched) == len(raw) and bytes(patched[:12]) == raw[:12] and q.build == o.build
    checks['instruction_starts_outside_unchanged'] = outside(old_all) == outside(q_all)
    checks['method_entries_are_starts'] = all(e in q_all for e in q.entries)
    checks['patched_window_decodes'] = (fork in q_all and fork + 5 in q_all and q.code[fork] == consts['kc_op_jmp']
                                        and struct.unpack_from('>I', q.code, fork + 1)[0] == cont and q.code[fork + 5] == consts['kc_op_filler'])
    checks['filler_unreached'] = not any(q.code[pc] in JUMPS and struct.unpack_from('>I', q.code, pc + 1)[0] == fork + 5 for pc in q_all)
    checks['method_end_unchanged'] = bool(ins) and ins[-1] == q_ins[-1]
    # Symbolic stack (kc_obj.track does not model the fork's result cell: it records the jt's depth after the pop and the jmp's
    # depth as is). Equal code before the window gives depth(jt) == depth(jmp) - 1 in that model; in the engine the fork pushes
    # the one cell the jt pops, so the depth flowing to the continuation is the same with the jmp. No new conflicts.
    lines_o, bad_o = o.track(entry)
    lines_q, bad_q = q.track(entry)
    depth_o = {pc: d for pc, d, _ in lines_o}
    depth_q = {pc: d for pc, d, _ in lines_q}
    out['depth_flowing_to_continuation'] = {'vanilla_jt_after_pop': depth_o.get(fork + 1), 'patched_jmp': depth_q.get(fork)}
    out['stack_conflicts'] = {'vanilla': bad_o, 'patched': bad_q}
    checks['stack_depth_at_continuation_unchanged'] = (fork + 1 in depth_o and fork in depth_q and depth_o[fork + 1] + 1 == depth_q[fork]
                                                       and bad_q == bad_o)
    checks['build_accepted_by_proxy'] = (o.build == consts['story_build']) == accepted
    out['failures'] = sorted(k for k, v in checks.items() if not v)
    return out


def main():
    probe = load_probe()
    text = CORE.read_text()
    consts, windows = probe.source_constants(text), probe.source_windows(text)
    report = {'core_constants': {k: consts[k] for k in ('story_build', 'warp_fork_offset', 'warp_continue_offset', 'warp_method_entry', 'warp_window_length')},
              'kc_stop_ips': {'play_stop': '%05x' % consts['kc_play_stop_ip'], 'fade_stop': '%05x' % consts['kc_fade_stop_ip'], 'play': '%05x' % consts['kc_play_ip'],
                              'loop_music_return': '%05x' % consts['kc_loop_music_return'], 'change_track_return': '%05x' % consts['kc_change_track_return']}}
    failures = []
    mayhem = study(load_member(X3 / 'addon/07.cat'), consts, windows, True)
    report['mayhem'] = mayhem
    failures += ['mayhem_' + f for f in mayhem['failures']]
    # Stock AP: the same code at other offsets; decoded for the record, not accepted (its build differs).
    stock = Obj(load_member(X3 / 'addon/04.cat'))
    e = stock.find(150, 'WarpToSector')
    report['stock'] = {'build': stock.build, 'warp_to_sector_entry': '%05x' % e[0] if e else None, 'accepted_by_proxy': stock.build == consts['story_build']}
    if stock.build == consts['story_build']:
        failures.append('stock_build_equals_pinned')
    # The KC IPs the queue tests: the stop in PlayMusicWithFade (callcls at 0x0db4f -> IP 0x0db54), the play (native at 0x0dbf2 -> 0x0dbf7),
    # the fade stop (0x0da12 -> 0x0da17) and the two return IPs.
    o = Obj(load_member(X3 / 'addon/07.cat'))
    allpc = set(o.all_insns())

    def native_before(ip, name):
        pc = ip - 5
        return pc in allpc and o.code[pc] == 0x82 and o.operand(pc) == name

    def call_before(ip, name):
        pc = ip - 5
        return pc in allpc and o.code[pc] in (0x86, 0x88) and o.operand(pc) == name
    ips = {'play_stop_after_MOV_StopMovie': native_before(consts['kc_play_stop_ip'], 'MOV_StopMovie'),
           'fade_stop_after_MOV_StopMovie': native_before(consts['kc_fade_stop_ip'], 'MOV_StopMovie'),
           'play_after_MOV_PlayMovie': native_before(consts['kc_play_ip'], 'MOV_PlayMovie'),
           'loop_music_return_after_callself_PlayMusicWithFade': call_before(consts['kc_loop_music_return'], '200::PlayMusicWithFade'),
           'change_track_return_after_callcls_LoopMusic': call_before(consts['kc_change_track_return'], '200::LoopMusic')}
    report['kc_ip_checks'] = ips
    failures += ['ip_' + k for k, v in ips.items() if not v]
    # The sites' owners.
    import bisect
    entries = o.entries

    def owner(pc):
        en = entries[bisect.bisect_right(entries, pc) - 1]
        return '%d::%s' % o.meth[en][0][:2]
    report['kc_ip_owners'] = {k: owner(consts[c] - 5) for k, c in (('play_stop', 'kc_play_stop_ip'), ('fade_stop', 'kc_fade_stop_ip'), ('play', 'kc_play_ip'))}
    if report['kc_ip_owners'] != {'play_stop': '200::PlayMusicWithFade', 'fade_stop': '200::MusicFadeOut', 'play': '200::PlayMusicWithFade'}:
        failures.append('ip_owners')
    report['failures'] = failures
    report['result'] = 'PASS' if not failures else 'FAIL'
    OUTPUT.write_text(json.dumps(report, indent=1) + '\n')
    print(json.dumps(report, indent=1))
    return 0 if not failures else 1


if __name__ == '__main__':
    sys.exit(main())
