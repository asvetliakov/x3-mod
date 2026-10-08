"""Hitches after the save load (largest frame) and before the last 10 frames. Usage: inflight.py <log>"""
import sys, re
dt=[int(m.group(1)) for l in open(sys.argv[1],'rb') if l.startswith(b'frame_end ') for m in [re.search(rb'dt_ms=(\d+)', l)] if m]
L=max(range(len(dt)),key=dt.__getitem__); s=dt[L+10:len(dt)-10]
print('load_frame',L,'inflight_frames',len(s),'secs~',sum(s)//1000)
for th in (33,50,100): print(f' >{th}ms',sum(x>th for x in s),'first_half',sum(x>th for x in s[:len(s)//2]),'second_half',sum(x>th for x in s[len(s)//2:]))
