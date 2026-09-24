"""LOD-switch rows (launcher --lod-switch-log [N], X3M_LOD_SWITCH_LOG) per node, for the Run 79 A ODS flicker question:
does a part pop between merged-LOD records (0 <-> C) while turning, and at which s / T_pad.
Read-only; streams the log line by line (session logs are hundreds of MB).
usage: lod_switch_rows.py LOG [LOG...] [--body SUBSTR]   -> per (node, view): body, switch count, then frame from->to s D T_pad
       lod_switch_rows.py --self-test                     -> checks the parser on a synthetic log"""
import collections
import re
import sys
import tempfile

KV = re.compile(r'(\w+)=(\S*)')


def summarise(path, body_filter=None, out=sys.stdout):
    nodes = collections.defaultdict(list); bodies = {}
    frames = overflow = dropped = 0; switched_frames = []
    with open(path, 'r', errors='replace') as lines:
        for line in lines:
            if not line.startswith('lod_switch'):
                continue
            kind = line.split(' ', 1)[0]
            d = dict(KV.findall(line))
            if kind == 'lod_switch':
                if body_filter and body_filter not in d.get('body', ''):
                    continue
                key = (d['node'], d.get('view', '-'))
                bodies[key] = d.get('body', '-')
                nodes[key].append((int(d['frame']), d['from'], d['to'], d['s'], d['D'], d['T_pad'], d.get('flag31', '-')))
            elif kind == 'lod_switch_overflow':
                overflow += 1; dropped += int(d['dropped'])
            elif kind == 'lod_switch_frame':
                frames += 1; switched_frames.append(int(d['frame']))
    print(f'== {path}', file=out)
    print(f'frames_with_switches={frames} overflow_frames={overflow} dropped_rows={dropped} nodes={len(nodes)}'
          + (f' first_frame={switched_frames[0]} last_frame={switched_frames[-1]}' if switched_frames else ''), file=out)
    for key, rows in sorted(nodes.items(), key=lambda kv: (-len(kv[1]), kv[0])):
        node, view = key
        print(f'node={node} view={view} body={bodies[key]} switches={len(rows)} flag31={rows[0][6]}', file=out)
        for frame, f, t, s, dist, pad, _ in rows:
            print(f'  frame {frame}: {f}->{t} s={s} D={dist} T_pad={pad}', file=out)
    return nodes


def self_test():
    rows = ['cull_census_frame device=1 frame=10 entries=3 overflow=0 unmeasured=0 exited=3 ring=8192 culled_small_exempt_bullet=0',
            'lod_switch frame=11 node=0a1b2c30 body=stations\\station_scenes\\terran\\usc_dock_e_left_rings from=0 to=1 s=172 D=40000 T_pad=173 flag31=1 view=47313d30',
            'lod_switch frame=11 node=0a1b2d40 body=stations\\station_scenes\\terran\\usc_dock_e_tower from=1 to=0 s=198 D=39000 T_pad=197 flag31=1 view=47313d30',
            'lod_switch_frame frame=11 switches=2 nodes=640',
            'lod_switch frame=12 node=0a1b2c30 body=stations\\station_scenes\\terran\\usc_dock_e_left_rings from=1 to=0 s=174 D=39800 T_pad=173 flag31=1 view=47313d30',
            'lod_switch_overflow frame=12 dropped=3 cap=1',
            'lod_switch_frame frame=12 switches=4 nodes=641']
    with tempfile.NamedTemporaryFile('w', suffix='.log', delete=False) as f:
        f.write('\n'.join(rows) + '\n')
    import io
    import os
    text = io.StringIO()
    try:
        nodes = summarise(f.name, out=text)
        tower_only = summarise(f.name, body_filter='tower', out=io.StringIO())
    finally:
        os.unlink(f.name)
    rings = nodes[('0a1b2c30', '47313d30')]
    assert [r[:4] for r in rings] == [(11, '0', '1', '172'), (12, '1', '0', '174')], rings
    assert nodes[('0a1b2d40', '47313d30')][0][5] == '197'
    assert 'frames_with_switches=2 overflow_frames=1 dropped_rows=3 nodes=2 first_frame=11 last_frame=12' in text.getvalue(), text.getvalue()
    assert len(tower_only) == 1
    print(text.getvalue(), end='')
    print('self-test PASS')


if __name__ == '__main__':
    args = sys.argv[1:]
    if args == ['--self-test']:
        self_test(); sys.exit(0)
    body = None
    if '--body' in args:
        i = args.index('--body'); body = args[i + 1]; del args[i:i + 2]
    if not args:
        sys.exit(__doc__)
    for p in args:
        summarise(p, body)
