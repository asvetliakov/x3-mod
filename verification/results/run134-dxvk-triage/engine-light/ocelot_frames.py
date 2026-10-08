"""Per session: the backend hint, and per frame with Ocelot (radius > 1000) engine_draw rows the big-ship handles
and the engine_light_frame plates= / draws_lit / candidates of that frame."""
import sys, re, collections
for log in sys.argv[1:]:
    hand = collections.defaultdict(list); elf = {}; backend = set()
    with open(log, errors='replace') as f:
        for line in f:
            k = line.split(' ', 1)[0]
            if 'dxvk' in line.lower() and len(backend) < 3 and k != 'engine_draw': backend.add(line[:120].strip())
            if k == 'engine_draw' and ' radius=10961' in line:
                d = dict(t.split('=', 1) for t in line.split()[1:] if '=' in t)
                hand[int(d['frame'])].append(d['name'].split('\\')[-1][-4:] + ':' + d['handle'][-3:])
            elif k == 'engine_light_frame':
                d = dict(t.split('=', 1) for t in line.split()[1:] if '=' in t)
                elf[int(d['frame'])] = (d['plates'], d['draws_lit'], d['candidates'], d['ships'])
    print(log, 'dxvk_lines=', len(backend), sorted(backend)[:1])
    for fr in sorted(hand):
        print('  frame', fr, ' '.join(sorted(hand[fr])), 'engine_light_frame(plates,draws_lit,candidates,ships)=', elf.get(fr))
