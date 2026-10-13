# Run160: min centre distance between two capitals from E verdict rows (d_m), and lane-row lat_gap range. Usage: python3 -I capcap_dist.py rows.txt
import sys,collections
C={'0x8d623838':1092,'0x8d6294d8':1022,'0x8d641ea8':1092,'0x363fbee0':1092,'0x8d62fa00':1092,'0x8d613d28':1022}
best={}; n=collections.Counter(); lat=[]
for l in open(sys.argv[1],errors='replace'):
    if l.startswith('ship_avoid_e_verdict'):
        d=dict(t.split('=',1) for t in l.split()[1:] if '=' in t)
        if d['A'] in C and d['B'] in C:
            k=tuple(sorted((d['A'],d['B']))); n[k]+=1; v=(float(d['d_m']),int(d['frame']),d['A_order'],d['B_order'],d['verdict'])
            if k not in best or v<best[k]: best[k]=v
    elif l.startswith('ship_capital_lane'):
        d=dict(t.split('=',1) for t in l.split()[1:] if '=' in t); lat.append((int(d['lat_gap']),int(d['frame']),d['verdict'],d['A']))
for k,v in best.items(): print(k,'rows',n[k],'min d_m',v,'rA+rB',C[k[0]]+C[k[1]])
print('lane rows',len(lat),'lat_gap min',min(lat),'max',max(lat))
