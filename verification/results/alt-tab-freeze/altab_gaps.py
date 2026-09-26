#!/usr/bin/env python3
"""Alt-tab freeze triage (2026-09-27). Prints, per session log: qpc gaps > 2 s between
stamped rows (with frame counts), the alt-tab stops and active-flag transitions
(music_keep_*), the window_mode row, and for debug logs the slow frames near each
alt-tab. Also reports whether the CrossOver X3AP.lnk shortcut carries arguments.
Usage: altab_gaps.py LOG [LOG...]   (streams; never loads a log whole)"""
import os, re, struct, sys

LNK = os.path.expanduser('~/Library/Application Support/CrossOver/Bottles/X3/drive_c/users/crossover/'
                         'AppData/Roaming/Microsoft/Windows/Start Menu/X3AP.lnk')

def scan(path):
    stamped, alt, active, window, slow, freq, q0 = [], [], [], None, [], 1e7, None
    with open(path, errors='replace') as f:
        for n, line in enumerate(f, 1):
            kind = line.split(' ', 1)[0]
            if kind == 'clock_anchor':
                q0 = int(re.search(r'qpc=(\d+)', line).group(1))
            m, fr = re.search(r'\bqpc=(\d+)', line), re.search(r'\bframe=(\d+)', line)
            if m and kind in ('media_cue_window', 'frame_end', 'music_keep_seek', 'music_keep_stop',
                              'music_keep_active', 'loading_phase'):
                stamped.append((n, int(m.group(1)), int(fr.group(1)) if fr else None, kind))
            if kind == 'music_keep_stop' and 'name=alt_tab' in line:
                alt.append((n, int(fr.group(1)), int(m.group(1))))
            if kind == 'music_keep_active':
                rib = re.search(r'run_in_background=(\d)', line).group(1)
                frm = re.search(r'from=(\S+) to=(\d)', line)
                active.append((n, int(fr.group(1)), frm.group(1), frm.group(2), rib))
            if kind == 'window_mode' and window is None:
                window = ' '.join(t for t in line.split() if t.split('=')[0] in
                                  ('before', 'after', 'backbuffer', 'windowed', 'action', 'reason'))
            if kind == 'frame_timing_slow':
                d = dict(t.split('=', 1) for t in line.split()[1:] if '=' in t)
                slow.append((int(d['frame']), int(d['dt_us']), int(d.get('gap_pre_us', -1))))
    return stamped, alt, active, window, slow, q0

def main():
    for path in sys.argv[1:]:
        stamped, alt, active, window, slow, q0 = scan(path)
        print(f'== {os.path.basename(path)}')
        print(f'  window_mode: {window}')
        for a, b in zip(stamped, stamped[1:]):
            dt = (b[1] - a[1]) / 1e7
            if dt > 2 and a[2] is not None and b[2] is not None and (b[2] - a[2]) / dt < 60:
                print(f'  gap L{a[0]}->L{b[0]} t={(a[1]-(q0 or a[1]))/1e7:.1f}s dt={dt:.2f}s '
                      f'frames {a[2]}->{b[2]} ({(b[2]-a[2])/dt:.1f} fps) {a[3]}->{b[3]}')
        for n, frame, q in alt:
            print(f'  alt_tab stop L{n} frame={frame} t={(q-(q0 or q))/1e7:.1f}s')
        for n, frame, frm, to, rib in active:
            print(f'  active L{n} frame={frame} {frm}->{to} run_in_background={rib}')
        for n, frame, _ in alt:
            near = [s for s in slow if frame - 2 <= s[0] <= frame + 60 and s[1] > 100000]
            if slow:
                print(f'  slow frames (>100 ms) within 60 frames of alt-tab at {frame}: {near}')
    d = open(LNK, 'rb').read()
    flags = struct.unpack_from('<I', d, 20)[0]
    print(f'X3AP.lnk LinkFlags={flags:#x} HasArguments={bool(flags & 0x20)}')

if __name__ == '__main__':
    main()
