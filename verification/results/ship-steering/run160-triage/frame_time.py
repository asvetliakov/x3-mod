# frame -> game t_ms (from ship_trace rows) and real elapsed_ms (frame_end). Usage: python3 -I frame_time.py rows.txt f1 f2 ...
import sys,re
want=sorted(int(x) for x in sys.argv[2:]); g={}; r={}
for l in open(sys.argv[1],errors='replace'):
    if l.startswith('ship_trace frame='):
        m=re.match(r'ship_trace frame=(\d+) t_ms=(\d+)',l); f=int(m[1])
        for w in want:
            if w not in g and f>=w: g[w]=(f,int(m[2]))
    elif l.startswith('frame_end '):
        m=re.search(r'frame=(\d+) .*elapsed_ms=(\d+)',l)
        if m and int(m[1]) in want: r[int(m[1])]=int(m[2])
for w in want: print(w,'game_s',g.get(w,(0,0))[1]/1000,'real_s',r.get(w,0)/1000)
