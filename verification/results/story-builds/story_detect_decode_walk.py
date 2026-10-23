#!/usr/bin/env python3
"""The run-time story detector's decode walk (src/proxy/story_detect.h memory_width, unique_site) over the in-memory
images of the KC objects on disk (verification/probe/story_vm_image.py, peephole pass modelled; temporary files only):

1. the in-memory widths walk every CODE byte from offset 1 and land on every method entry (counts, fused opcodes, and
   opcodes without an interpreter handler of their own other than 70);
2. each object's SETA and queue detection with its reader calls (the read budget's measured need);
3. a signature copied into a `0a` operand (PlayMusicWithFade's MOV_StopMovie, StopFastForward's TI_SetTimeWarpFactor)
   with the shipped detector and with a byte-wise scan (the pre-fix unique_site, compiled from a temporary copy).

Prints counts and offsets only.

    python3 -I verification/results/story-builds/story_detect_decode_walk.py [X3 directory] > story_detect_decode_walk.txt
"""
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'verification/probe'))
sys.path.insert(0, str(ROOT / 'verification/results/ship-navigation-ai/gate-queue'))
sys.path.insert(0, str(ROOT / 'verification/results/story-builds'))
import story_vm_image as img  # noqa: E402
import story_builds_table  # noqa: E402
from kc_obj import load_member  # noqa: E402

X3 = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('/mnt/c/X3')
OBJECTS = [('addon/07.cat', 'L/x3story.obj'), ('addon/04.cat', 'L/x3story.obj'), ('addon/07.cat', 'L/x3intro.obj'),
           ('addon/07.cat', 'L/x3config.obj'), ('addon/07.cat', 'L/x3galedit.obj')]
NO_HANDLER = {0x25, 0x26, 0x27, 0x31, *range(0x3b, 0x46), 0x60, 0x61, 0x62, 0x63, *range(0x67, 0x6e), *range(0x71, 0x78),
              *range(0x7a, 0x82), 0x89, 0x8a, 0x8b, *range(0x9a, 0xa0), 0xa8, 0xa9}


def walk(raw):
    seen = {}

    def grab(code, entries):
        seen['code'], seen['entries'] = bytes(code), dict(entries)
    img.build_image(raw, mutate=grab)
    code, entries = seen['code'], set(seen['entries'].values())
    pc, starts, fused, odd = 1, set(), 0, 0
    while pc < len(code):
        starts.add(pc)
        op = code[pc]
        fused += 0x8c <= op <= 0xb1 or (op == 0x83 and code[pc + 1:pc + 3] == b'\x83\x83')
        odd += op == 0 or op > 0xb1 or op in NO_HANDLER
        pc += img.memory_width(code, pc)
    return len(code), pc, len(starts), len(entries), len(entries & starts), fused, odd


def compile_host(directory, bytewise=False):
    inc = Path(directory) / 'inc'
    shutil.copytree(ROOT / 'src/proxy', inc, ignore=shutil.ignore_patterns('*.cpp'))
    if bytewise:
        h = (inc / 'story_detect.h').read_text()
        old = '        const unsigned w = memory_width(buf, i, length);\n        if (!w) return {Fail::decode_error, where};'
        assert old in h
        (inc / 'story_detect.h').write_text(h.replace(old, '        const unsigned w = 1;'))
    exe = Path(directory) / ('host_bytewise' if bytewise else 'host')
    build = subprocess.run([shutil.which('c++') or 'g++', '-std=c++17', '-O2', '-I', str(inc), str(ROOT / 'verification/probe/story_detect_host.cpp'),
                            '-o', str(exe)], capture_output=True, text=True, timeout=180)
    assert build.returncode == 0, build.stderr[-2000:]
    return exe


def run(exe, raw, directory, **kw):
    image = Path(directory) / 'image.bin'
    image.write_bytes(img.build_image(raw, **kw))
    try:
        return img.run_host(exe, image)
    finally:
        image.unlink()


with tempfile.TemporaryDirectory(prefix='x3-decode-walk-') as tmp:
    Path(tmp, 'b').mkdir()
    exe, bytewise = compile_host(tmp), compile_host(Path(tmp, 'b'), bytewise=True)
    print('1. decode walk over CODE (memory widths, peephole modelled)')
    print('   object                         code_bytes walk_end instructions entries aligned fused no_handler')
    raws = {}
    for cat, member in OBJECTS:
        try:
            raws[(cat, member)] = raw = load_member(X3 / cat, member)
        except KeyError:
            print('   %-30s missing' % (cat + ':' + member))
            continue
        print('   %-30s %10d %8d %12d %7d %7d %5d %10d' % ((cat + ':' + member,) + walk(raw)))
    print('2. detections and reader calls (budget 16384)')
    for (cat, member), raw in raws.items():
        out = run(exe, raw, tmp)
        s, q = out['seta'], out['queue']
        print('   %-30s seta %-10s %-24s reads %5s | queue %-10s %-24s reads %5s' % (cat + ':' + member, s['fail'], s['where'] or '-', s['reads'],
                                                                                    q['fail'], q['where'] or '-', q['reads']))
    print('3. a signature copied into a 0a operand (Mayhem)')
    e = story_builds_table.entries()[0]
    stop, play, sstop = e['kc_play_stop_ip'] - 5, e['kc_play_ip'] - 5, e['seta_stop_ip'] - 5

    def embed(code, entries):
        img.embed_in_operand(code, entries[(200, 'PlayMusicWithFade')], entries, code[stop:stop + 5], [(stop, 5), (play, 5)])
        img.embed_in_operand(code, entries[(150, 'StopFastForward')], entries, code[sstop:sstop + 5], [(sstop, 5)])

    def embed_without_true(code, entries):
        embed(code, entries)
        code[stop:stop + 5] = b'\x01' * 5
        code[sstop:sstop + 5] = b'\x01' * 5
    raw = raws[('addon/07.cat', 'L/x3story.obj')]
    print('   expected: seta stop_ip 0x%05x, queue play_stop 0x%05x' % (e['seta_stop_ip'], e['kc_play_stop_ip']))
    for name, mutate, build in (('embedded copy, known build', embed, None), ('true site gone, unknown build', embed_without_true, 1234567890)):
        for label, host in (('decode walk', exe), ('byte-wise scan', bytewise)):
            out = run(host, raw, tmp, build=build, mutate=mutate)
            s, q = out['seta'], out['queue']
            print('   %-30s %-15s seta %-15s %-22s stop_ip %s | queue %-15s %-24s play_stop %s'
                  % (name, label, s['fail'], s['where'] or '-', s['stop_ip'], q['fail'], q['where'] or '-', q['play_stop']))
