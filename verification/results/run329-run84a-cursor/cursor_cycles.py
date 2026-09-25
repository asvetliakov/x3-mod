#!/usr/bin/env python3
"""Run 84 A cursor evidence from --window-trace/--cursor-reassert rows (read-only, streams the log).
usage: cursor_cycles.py LOG [launch_max_frame]
Prints: (1) whole-session counts of the cursor-relevant rows; (2) launch timeline (frame <= N):
cursor poll/snapshot changes, ring rows, fire; (3) one line per deactivate/activate cycle."""
import re, sys
L = sys.argv[1]; LMAX = int(sys.argv[2]) if len(sys.argv) > 2 else 200
KV = re.compile(r'(\w+)=(\S+)')
KEEP = ('window_msg', 'window_msg_frame', 'cursor_call', 'cursor_reassert', 'cursor_reassert_arm', 'window_trace_flush',
        'cursor_snapshot', 'telemetry_cursor_poll')
counts = {}; setcursor_rows = []; frames_rows = []; calls = []
launch = []; cycles = []; cur = None; last_snap = None; last_poll_key = None
TARGET = '000a0064'
def rect(s): return [int(x) for x in s.split(',')]
def edge(x, y, c):
    return x in (c[0], c[2] - 1) or y in (c[1], c[3] - 1)
with open(L, 'r', errors='replace') as fh:
    for line in fh:
        kind = line.split(' ', 1)[0]
        if kind not in KEEP: continue
        d = dict(KV.findall(line)); fr = int(d.get('frame', -1))
        key = kind + ('' if kind != 'window_msg' else ':' + d.get('name', '?'))
        counts[key] = counts.get(key, 0) + 1
        if kind == 'window_msg' and d.get('name') == 'WM_SETCURSOR': setcursor_rows.append(d)
        if kind == 'window_msg_frame': frames_rows.append(d)
        if kind == 'cursor_call': calls.append(d)
        # launch timeline
        if 0 <= fr <= LMAX:
            if kind == 'telemetry_cursor_poll':
                k = (d['flags'], d['cursor'])
                if k != last_poll_key: launch.append(f"f={fr} poll flags={d['flags']} cursor={d['cursor']} xy={d['x']},{d['y']}"); last_poll_key = k
            elif kind == 'cursor_snapshot':
                k = (d['foreground'], d['cursor_flags'], d['cursor'], d['clip'])
                if k != last_snap: launch.append(f"f={fr} snap fg={d['foreground']} flags={d['cursor_flags']} cursor={d['cursor']} xy={d['x']},{d['y']} clip={d['clip']}"); last_snap = k
            elif kind in ('cursor_reassert', 'cursor_reassert_arm', 'window_trace_flush', 'window_msg_frame', 'cursor_call') or kind == 'window_msg':
                launch.append(f"f={fr} {kind} " + ' '.join(f"{k}={d[k]}" for k in ('action','armed_by','reason','written','expired','name','hwnd','wparam','result','before_flags','up','down','after_flags','pointer_in_client','clip_is_client','clip','mousemove','setcursor','cursor_set','op','handle','thread','count') if k in d))
        # cycles (target window)
        if kind == 'window_msg' and d.get('hwnd') == TARGET:
            n = d['name']
            if n == 'WM_ACTIVATE' and d['wparam'][-4:] == '0000':
                cur = {'deact_f': fr, 'deact_tick': int(d['tick_ms']), 'syscmd': cur_sys if 'cur_sys' in dir() else 0, 'inactive_setcursor': 0,
                       'act_msgs': [], 'snaps_after': [], 'fire': None}
                cycles.append(cur); cur_sys = 0
            elif n == 'WM_SYSCOMMAND': cur_sys = d['wparam']
            elif n == 'WM_SETCURSOR' and cur is not None and not cur['act_msgs']: cur['inactive_setcursor'] += 1; cur['setcursor_result'] = d['result']
            elif cur is not None and n in ('WM_ACTIVATE', 'WM_ACTIVATEAPP', 'WM_SETFOCUS') and d['wparam'] != '00000000' or (cur is not None and n == 'WM_SETFOCUS'):
                cur['act_msgs'].append((n, int(d['tick_ms'])))
            elif cur is not None and cur['fire'] and n == 'WM_SETCURSOR': cur['setcursor_after_fire'] = cur.get('setcursor_after_fire', 0) + 1
        if kind == 'cursor_snapshot' and cur is not None:
            if d['foreground'] != TARGET and not cur['fire']: cur['deact_xy'] = (d['x'], d['y'])
            if cur['fire'] and d['foreground'] == TARGET: cur['snaps_after'].append((fr, d['cursor_flags'], d['cursor'], int(d['x']), int(d['y']), d['clip']))
        if kind == 'cursor_reassert' and cur is not None and d.get('armed_by') == 'activate':
            cur['fire'] = d; cur['fire_f'] = fr
        if kind == 'window_msg_frame' and cur is not None and cur['fire'] and fr >= cur['fire_f']: cur['mframes_after'] = cur.get('mframes_after', 0) + 1
        if kind == 'cursor_call' and cur is not None and cur['fire'] and fr >= cur['fire_f']: cur['calls_after'] = cur.get('calls_after', 0) + 1
print('# whole-session row counts'); [print(f'{v:6d} {k}') for k, v in sorted(counts.items())]
print('# WM_SETCURSOR rows (frame hwnd hit trigger result suppressed)')
for d in setcursor_rows: print(' ', d['frame'], d['hwnd'], d['hit'], d['trigger'], d['result'], d['suppressed'])
print('# window_msg_frame rows'); [print(' ', ' '.join(f'{k}={d[k]}' for k in ('frame','mousemove','ncmousemove','setcursor','cursor_set','cursor_pos','cursor_dropped'))) for d in frames_rows]
print('# cursor_call rows:', len(calls)); [print(' ', d) for d in calls[:20]]
print(f'# launch timeline frames 0..{LMAX} (poll/snapshot change-only)'); [print(' ', s) for s in launch]
print('# cycles: k deact_f syscmd inact_setcursor(result) deact_xy act_order act_to_fire_ms? fire_f fire_xy on_clip_edge flags_after(set) handles_after moved_px setcursor_after calls_after mframes_after')
for i, c in enumerate(cycles, 1):
    f = c['fire']; sa = c['snaps_after']
    if sa:
        first = sa[0]; cl = rect(first[5])
        on_edge = edge(first[3], first[4], cl); moved = max(abs(s[3]-first[3]) + abs(s[4]-first[4]) for s in sa)
        flags = sorted({s[1] for s in sa}); hands = sorted({s[2] for s in sa}); fxy = f'{first[3]},{first[4]}'
    else: on_edge = moved = '-'; flags = hands = []; fxy = '-'
    rx = c.get('deact_xy'); ret_in = '-'
    if rx and f: cc = rect(f['clip']); ret_in = int(cc[0] <= int(rx[0]) < cc[2] and cc[1] <= int(rx[1]) < cc[3])
    order = '>'.join(m[0].replace('WM_', '') for m in c['act_msgs'])
    print(f"{i:2d} {c['deact_f']:5d} {c['syscmd'] or '-':>8} {c['inactive_setcursor']}({c.get('setcursor_result','-')}) ret_xy={c.get('deact_xy','-')} ret_in_clip={ret_in} {order} "
          f"fire_f={c.get('fire_f','-')} pic={f and f['pointer_in_client']} cic={f and f['clip_is_client']} up/down={f and f['up']}/{f and f['down']} "
          f"fire_xy={fxy} edge={on_edge} flags_after={flags} handles_after={hands} moved={moved} snaps={len(sa)} "
          f"setcursor_after={c.get('setcursor_after_fire',0)} calls_after={c.get('calls_after',0)} mframes_after={c.get('mframes_after',0)} inactive_ms={(c['act_msgs'][0][1]-c['deact_tick']) if c['act_msgs'] else '-'}")
