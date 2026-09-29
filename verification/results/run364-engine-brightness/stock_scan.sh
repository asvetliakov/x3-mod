#!/bin/bash
# Per run: stock or modded (the proxy's lod/mod log hint), hull_emission_draw rows in capture frames by program, and
# hull_emission_frame gain; used to pick a stock burst with engine cards in view.
for d in /tmp/x3-bottleX3-run35[1-6] /tmp/x3-bottleX3-run364; do
  L=$(ls $d/session-*.log | head -1)
  echo "== $d $(grep -m1 -o 'backbuffer=[0-9x]*' $L) $(grep -m1 -o 'hull_emission_gain_mode[^\n]*' $L | cut -c1-90)"
  grep '^hull_emission_draw' $L | python3 -c "
import sys,re,collections
c=collections.Counter(); far=collections.Counter()
for l in sys.stdin:
    d=dict(re.findall(r'(\w+)=(\S+)',l)); c[(d['program'],d['ps'][:8])]+=1
    if float(d.get('origin_w','0'))>1000: far[(d['program'],d['ps'][:8])]+=1
print(' draws by (program,ps):',dict(c),' origin_w>1000:',dict(far))"
done
