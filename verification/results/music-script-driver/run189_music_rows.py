#!/usr/bin/env python3
"""Run 189 music rows behind docs/reverse-engineering/music-script-driver.md §8.4.

Reads only the music_trace_* / music_keep_* rows of a flight log (grep-style, never the whole file into the output),
pairs every MOV_StopMovie row with the next MOV_PlayMovie row, and prints per stop: frame, KC IP, signed stack index k,
capacity, alert old/new, stopping task, record owner (ctx), the next play's id, task and the frame and QPC gaps
(QPC at 10 MHz, from the log's frame_end rows). Also checks the KC window of 150::WarpToSector's forked StopMusic(500)
in both story builds (the prerequisite patch of §8.4).

    python3 -I verification/results/music-script-driver/run189_music_rows.py [log] [X3 dir] > .../run189_music_rows.txt
"""
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'ship-navigation-ai' / 'gate-queue'))
from kc_obj import Obj, load_member  # noqa: E402

LOG = Path(sys.argv[1]) if len(sys.argv) > 1 else HERE.parent / 'run189-flight' / 'x3m.log'
X3 = Path(sys.argv[2]) if len(sys.argv) > 2 else Path('/mnt/c/X3')
IPS = {0xdb54: 'PlayMusicWithFade stop', 0xda17: 'MusicFadeOut stop', 0xdbf7: 'PlayMusicWithFade play'}


def rows():
    with open(LOG, errors='replace') as f:
        for n, line in enumerate(f, 1):
            if line.startswith(('music_trace', 'music_keep', 'frame_end')):
                kind = line.split()[0]
                kv = dict(re.findall(r'(\w+)=(\S+)', line))
                yield n, kind, kv


def s32(v):
    v = int(v)
    return v - (1 << 32) if v >= 1 << 31 else v


def main():
    rs = list(rows())
    fe = [(int(kv['qpc']), float(kv['elapsed_ms'])) for _, k, kv in rs if k == 'frame_end' and 'qpc' in kv]
    hz = (fe[-1][0] - fe[0][0]) / ((fe[-1][1] - fe[0][1]) / 1000.0)
    print('log %s  qpc_hz %.0f' % (LOG.name, hz))
    print('stop-all rows:', ' '.join('%s@%s' % (kv.get('name'), kv.get('frame')) for _, k, kv in rs
                                     if k == 'music_trace_stop'))
    print('%-6s %-7s %-6s %-24s %4s %3s %5s %-9s %-9s %-9s | %-6s %-9s %-5s %6s %8s' % (
        'line', 'frame', 'id', 'ip', 'k', 'cap', 'alert', 'task', 'ctx', 'cells', 'nextid', 'nexttask', 'same',
        'dframe', 'dms'))
    plays = [(n, kv) for n, k, kv in rs if k == 'music_trace_play']
    for n, k, kv in rs:
        if k != 'music_trace_stop_movie':
            continue
        ip = int(kv['ip'], 16)
        nxt = next(((m, p) for m, p in plays if m > n), (None, None))[1]
        ctx = int(kv['ctx'], 16)
        line = '%-6d %-7s %-6s %-24s %4d %3s %2s/%-2s %-9s %-9d %-9s' % (
            n, kv['frame'], kv['id'], '0x%05x %s' % (ip, IPS.get(ip, '')), s32(kv['k']), kv['capacity'],
            kv['alert_old'], kv['alert_new'], kv['task_id'], ctx, kv.get('cells'))
        if nxt and kv['record'] != '0x00000000':
            df = int(nxt['frame']) - int(kv['frame'])
            dms = (int(nxt['qpc']) - int(kv['qpc'])) * 1000.0 / hz
            line += ' | %-6s %-9s %-5s %6d %8.1f' % (nxt['id'], nxt['task_id'], nxt['task_id'] == kv['task_id'], df, dms)
        print(line)
    # record owner at each stop = task id of the play that started that record
    owners = {kv['record']: kv['task_id'] for _, k, kv in rs if k == 'music_trace_play'}
    checked = [(kv['record'], int(kv['ctx'], 16), owners.get(kv['record'])) for _, k, kv in rs
               if k == 'music_trace_stop_movie' and kv['record'] in owners and kv['record'] != '0x00000000']
    print('stop ctx == task id of the record\'s last play: %d of %d; mismatches %s' % (
        sum(str(c) == o for _, c, o in checked), len(checked), [(r, c, o) for r, c, o in checked if str(c) != o]))
    # KC: 150::WarpToSector forks a child that runs StopMusic(500)
    for label, cat in (('mayhem', 'addon/07.cat'), ('stock', 'addon/04.cat')):
        o = Obj(load_member(X3 / cat))
        e = o.find(150, 'WarpToSector')[0]
        ins = list(o.insns(e))
        i = next(j for j, pc in enumerate(ins) if o.code[pc] == 0x86 and o.operand(pc) == '200::StopMusic')
        win = ins[i - 5:i + 4]
        text = '; '.join(re.sub(r'^.*w\d\s+', '', o.line(pc)).strip() for pc in win)
        fork = win[0]
        tgt = o.operand(win[1])
        into = [hex(pc) for pc in ins if o.code[pc] in (0x32, 0x33, 0x34)
                and fork < int(str(o.operand(pc)), 16) <= fork + 5]
        print('%s 150::WarpToSector %05x: fork at %05x (file +%05x), patch window %05x..%05x -> jmp %s; '
              'branches into window interior: %s' % (label, e, fork, fork + 20, fork, fork + 5, tgt, into or 'none'))
        print('   ' + text)


if __name__ == '__main__':
    main()
