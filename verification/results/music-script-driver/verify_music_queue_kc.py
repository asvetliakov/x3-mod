#!/usr/bin/env python3
"""The track queue's KC patch window (docs/architecture/music-queue.md; music-script-driver.md section 8.4 "Prerequisite").

Per known story build (src/proxy/story_builds.h) present on disk (catalogues and loose L/x3story*.obj under the game
directory), checks what the run-time detector (src/proxy/story_detect.h) must find there: the 150::WarpToSector entry,
the 6-byte window `op6f; jt <continue>` (file bytes big-endian, the memory form with the operand little-endian after the
loader's byte-swap pass 0x0049e1a0), the continuation, the fork child's StopMusic(500) body behind the window, that no
jump in the whole CODE lands inside the window and no method entry does, and that the patched object (`jmp <continue>`
+ one filler byte) keeps every instruction start outside the window, every method entry, the method's decode and its
symbolic stack depths; the queue's KC IPs (each the only native or call of its kind in its method), the literal ids
ChangeTrackMusic is called with, and that the music methods equal the first entry's modulo relocation (so the fork
child's stack depth and cells carry over). The object the game loads must be a table entry. Then the detector itself,
compiled on the host, runs over an in-memory image of every entry's object and of every other KC object in addon/04.cat
and addon/07.cat (built in a temporary directory, deleted after): it must reproduce each entry exactly and find no
sites in x3config or x3intro (x3galedit runs the game script in the editor: its detection is reported). No bytes are written anywhere else. Prints a summary (the full JSON is written beside this script)
and exits 0 only on PASS.

    python3 -I verification/results/music-script-driver/verify_music_queue_kc.py [X3 dir] > .../verify_music_queue_kc_output.json
"""
import bisect
import importlib.util
import json
import re
import struct
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE.parent / 'ship-navigation-ai' / 'gate-queue'))
from kc_obj import Obj, load_member  # noqa: E402
sys.path.insert(0, str(HERE.parents[1] / 'probe'))
import story_builds_table  # noqa: E402

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
    out['failures'] = sorted(k for k, v in checks.items() if not v)
    return out


MUSIC_METHODS = ((200, 'PlayMusicWithFade'), (200, 'LoopMusic'), (200, 'ChangeTrackMusic'), (200, 'MusicFadeOut'), (200, 'StopMusic'))


def normalized(o, cls, name):
    """A method's instructions with jump targets relative to its entry and call/native operands by name (relocation-free)."""
    e = o.find(cls, name)[0]
    out = []
    for pc in o.insns(e):
        op = o.code[pc]
        if op in JUMPS:
            out.append((op, struct.unpack_from('>I', o.code, pc + 1)[0] - e))
        elif op in (0x82, 0x84, 0x85, 0x86, 0x87, 0x88):
            out.append((op, o.operand(pc)))
        else:
            out.append((op, o.code[pc + 1:pc + o.width(pc)].hex()))
    return out


def site_checks(o, e):
    """The queue's KC IPs of one object: each follows its native or call in the named method, the only one there."""
    allpc = set(o.all_insns())
    entries = o.entries

    def owner(pc):
        en = entries[bisect.bisect_right(entries, pc) - 1]
        return '%d::%s' % o.meth[en][0][:2]

    def only_after(ip, ops, name, method):
        pc = ip - 5
        body = list(o.insns(o.find(*method)[0]))
        hits = [q for q in body if o.code[q] in ops and o.operand(q) == name]
        return pc in allpc and hits == [pc] and owner(pc) == '%d::%s' % method
    return {'play_stop_after_MOV_StopMovie': only_after(e['kc_play_stop_ip'], (0x82,), 'MOV_StopMovie', (200, 'PlayMusicWithFade')),
            'fade_stop_after_MOV_StopMovie': only_after(e['kc_fade_stop_ip'], (0x82,), 'MOV_StopMovie', (200, 'MusicFadeOut')),
            'play_after_MOV_PlayMovie': only_after(e['kc_play_ip'], (0x82,), 'MOV_PlayMovie', (200, 'PlayMusicWithFade')),
            'loop_music_return_after_call_PlayMusicWithFade': only_after(e['kc_loop_music_return'], (0x86, 0x88), '200::PlayMusicWithFade', (200, 'LoopMusic')),
            'change_track_return_after_call_LoopMusic': only_after(e['kc_change_track_return'], (0x86, 0x88), '200::LoopMusic', (200, 'ChangeTrackMusic'))}


def change_track_literals(o):
    allpc = list(o.all_insns())
    lits = []
    for i, pc in enumerate(allpc):
        if o.code[pc] == 0x86 and o.operand(pc) == '200::ChangeTrackMusic':
            arg = allpc[i - 3]
            c = o.code[arg]
            val = {0x01: 0, 0x02: 1, 0x03: 2, 0x04: 3}.get(c)
            if c in (0x05, 0x06, 0x07):
                val = int(o.operand(arg))
            if val is not None:
                lits.append(val)
    return sorted(lits)


def run_detector(objects):
    """The run-time detector (src/proxy/story_detect.h) over an in-memory image of each object, in a temporary directory."""
    sys.path.insert(0, str(ROOT / 'verification/probe'))
    import story_vm_image
    with tempfile.TemporaryDirectory(prefix='x3-story-detect-') as d:
        exe, log = story_vm_image.compile_host(d)
        if exe is None:
            return {'error': log[-400:]}
        out = {}
        for label, raw in objects:
            image = Path(d) / 'image.bin'
            image.write_bytes(story_vm_image.build_image(raw))
            out[label] = story_vm_image.run_host(exe, image)
            image.unlink()
        return out


def main():
    probe = load_probe()
    text = CORE.read_text()
    consts = probe.source_constants(text)
    table = story_builds_table.entries()
    objects = story_builds_table.story_objects(X3)
    loaded = story_builds_table.loaded_story(X3, objects)
    report = {'table': [{k: (v.hex() if isinstance(v, bytes) else ('%05x' % v if isinstance(v, int) and k != 'build' else v))
                         for k, v in e.items()} for e in table],
              'loaded_story': {'source': loaded[0], 'build': loaded[1]} if loaded else None, 'entries': {}}
    failures = []
    if story_builds_table.row_errors():
        failures.append('table_rows')
    if not loaded or loaded[1] not in {e['build'] for e in table}:
        failures.append('loaded_story_not_in_table')
    reference = None
    for e in table:
        found = [(s, raw) for s, b, raw in objects if b == e['build']]
        if not found:
            report['entries'][e['name']] = 'not on disk'
            continue
        for source, raw in found:
            c = dict(consts)
            c.update({k: e[k] for k in ('warp_fork_offset', 'warp_continue_offset', 'warp_method_entry')})
            c['warp_window_length'] = 6
            r = study(raw, c, {'warp_window_memory': e['warp_window_memory'], 'warp_patch_memory': e['warp_patch_memory']}, True)
            o = Obj(raw)
            r['source'] = source
            r['kc_ip_checks'] = site_checks(o, e)
            r['change_track_music_literal_ids'] = change_track_literals(o)
            if reference is None:
                reference = (e['name'], o)
            r['music_methods_equal_to_' + reference[0]] = {'%d::%s' % m: normalized(o, *m) == normalized(reference[1], *m) for m in MUSIC_METHODS}
            report['entries'][e['name']] = r
            label = e['name'] + '_'
            failures += [label + f for f in r['failures']]
            failures += [label + 'ip_' + k for k, v in r['kc_ip_checks'].items() if not v]
            if r['change_track_music_literal_ids'] != [0, 0, 0, 13, 14, 8233, 8234, 8235, 8236, 8237]:
                failures.append(label + 'change_track_literals')
            failures += [label + 'method_' + k for k, v in r['music_methods_equal_to_' + reference[0]].items() if not v]
    # The run-time detector reproduces every entry on disk from its object alone and finds nothing in the other KC objects.
    others = [(cat + ':' + path, b) for cat, path, b in story_builds_table.kc_objects(X3) if not path.lower().endswith('x3story.obj')]
    sources = [(s, raw) for s, b, raw in objects if b in {e['build'] for e in table}]
    from kc_obj import load_member
    sources += [(name, load_member(X3 / name.split(':')[0], name.split(':')[1])) for name, _ in others]
    detected = run_detector(sources)
    report['detector'] = detected
    if 'error' in detected:
        failures.append('detector_not_built')
    else:
        by_build = {e['build']: e for e in table}
        for label, rows in detected.items():
            q, s = rows['queue'], rows['seta']
            b = int(q['build'])
            if b in by_build:
                e = by_build[b]
                want = {'fail': 'none', 'warp_entry': '0x%05x' % e['warp_method_entry'], 'fork': '0x%05x' % e['warp_fork_offset'],
                        'cont': '0x%05x' % e['warp_continue_offset'], 'window': e['warp_window_memory'].hex(), 'patch': e['warp_patch_memory'].hex(),
                        'play_stop': '0x%05x' % e['kc_play_stop_ip'], 'fade_stop': '0x%05x' % e['kc_fade_stop_ip'], 'play': '0x%05x' % e['kc_play_ip'],
                        'loop_return': '0x%05x' % e['kc_loop_music_return'], 'change_return': '0x%05x' % e['kc_change_track_return']}
                if any(q.get(k) != v for k, v in want.items()):
                    failures.append('detector_queue_' + label)
                if s.get('fail') != 'none' or s.get('stop_ip') != '0x%05x' % e['seta_stop_ip'] or s.get('gate_return') != '0x%05x' % e['seta_gate_return']:
                    failures.append('detector_seta_' + label)
            elif 'x3galedit' not in label.lower() and (q.get('fail') == 'none' or s.get('fail') == 'none'):
                # x3config and x3intro carry no game classes; x3galedit runs the game script in the editor, so a detection
                # there is that object's own (reported, not an error)
                failures.append('detector_found_sites_in_' + label)
    report['failures'] = failures
    report['result'] = 'PASS' if not failures else 'FAIL'
    OUTPUT.write_text(json.dumps(report, indent=1) + '\n')
    print(json.dumps({'result': report['result'], 'failures': failures, 'loaded_story': report['loaded_story'],
                      'entries': {k: (v if isinstance(v, str) else {'source': v['source'], 'failures': v['failures']}) for k, v in report['entries'].items()},
                      'detector': {k: {'seta': v['seta'].get('fail'), 'queue': v['queue'].get('fail'), 'where': v['queue'].get('where') or v['seta'].get('where')}
                                   for k, v in detected.items()} if 'error' not in detected else detected}, indent=1))
    return 0 if not failures else 1


if __name__ == '__main__':
    sys.exit(main())
