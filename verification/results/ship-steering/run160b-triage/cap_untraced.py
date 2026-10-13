# Run160: capital A_order seen in E verdict rows (covers untraced capitals); 0x12 frame spans per capital. Usage: python3 -I cap_untraced.py rows.txt
import sys,re,collections
C={'0x8d623838','0x8d6294d8','0x8d641ea8','0x363fbee0','0x8d62fa00','0x8d613d28'}
o=collections.defaultdict(collections.Counter); f12=collections.defaultdict(list)
for l in open(sys.argv[1],errors='replace'):
    if not l.startswith('ship_avoid_e_verdict'): continue
    d=dict(t.split('=',1) for t in l.split()[1:] if '=' in t)
    if d.get('A') in C:
        o[d['A']][d['A_order']]+=1
        if d['A_order']=='0x12': f12[d['A']].append(int(d['frame']))
for a in sorted(o):
    fr=f12[a]; print(a,dict(o[a]),'0x12 frames',(min(fr),max(fr),len(fr)) if fr else '-')
