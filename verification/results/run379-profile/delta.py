# Mean ms/frame per bucket: group R16-R20 vs late away R22-R25 vs early arrival R08-R09 (parses series_out.txt).
rows={}
for l in open('series_out.txt'):
    if l.startswith('R'):
        p=l.replace('|',' ').split(); p=[p[0]]+[''.join(p[1:3])]+p[3:] if p[1].endswith('-') else p; rows[int(p[0][1:])]=[float(x) for x in p[2:]]
hdr='ms/fr pre views rva0 4d6509 4c403e 48f2dd 4721b6 4e25a8 50e21e 4d358f other'.split()
def mean(ix): return [sum(rows[i][j] for i in ix)/len(ix) for j in range(len(hdr))]
G=mean(range(16,21));A=mean(range(22,26));E=mean([8,9])
print('bucket   '+' '.join(f'{h:>7}' for h in hdr))
for n,v in (('group',G),('late',A),('early',E),('G-late',[g-a for g,a in zip(G,A)]),('G-early',[g-e for g,e in zip(G,E)])):
    print(f'{n:8s} '+' '.join(f'{x:7.2f}' for x in v))
