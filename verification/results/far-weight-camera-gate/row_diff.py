#!/usr/bin/env python3
"""Changed report rows between two temporal fixture reports (the far weight on the camera gate, 2026-09-25).

usage: row_diff.py BEFORE.txt AFTER.txt
Rows are compared line for line by their first token (the row name), in order of appearance per name. Timing rows (wall-clock
figures) are skipped: *_TIMING*, FOLD_TIMING, PASS_TIMING, LOOP_TIMING*, and the `ms=`-bearing TIMING rows. Prints, per row
name, the number of rows before / after, the number of changed rows, and for up to MAX changed rows the changed fields.
"""
import os,re,sys
from collections import defaultdict
MAX=int(os.environ.get('MAX','4'))
SKIP=re.compile(r'TIMING|^LOOP_|^MEASURE')
FIELD=re.compile(r'(\w+)=(\S+)')
def rows(path):
    out=defaultdict(list)
    for line in open(path,encoding='utf-8',errors='replace'):
        line=line.rstrip('\n')
        if not line or SKIP.search(line.split(' ',1)[0]):continue
        out[line.split(' ',1)[0]].append(line)
    return out
before,after=rows(sys.argv[1]),rows(sys.argv[2])
for name in sorted(set(before)|set(after)):
    b,a=before.get(name,[]),after.get(name,[])
    changed=[(x,y) for x,y in zip(b,a) if x!=y]
    if not changed and len(a)==len(b):continue
    print(f'{name}: before={len(b)} after={len(a)} changed={len(changed)}')
    for x,y in changed[:MAX]:
        fx,fy=dict(FIELD.findall(x)),dict(FIELD.findall(y))
        keys=[k for k in fy if fx.get(k)!=fy.get(k)]
        ident=' '.join(f'{k}={fy[k]}' for k in ('program','config','row','width','variant','k','pan','scene','case','label') if k in fy)
        if keys:print('   ',ident,'|',' '.join(f'{k}:{fx.get(k)}->{fy[k]}' for k in keys))
        else:print('   ',ident,'| text:',x[:150],'->',y[:150])
