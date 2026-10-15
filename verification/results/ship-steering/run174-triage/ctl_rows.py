# Run163 Q1/Q3/Q5: every ship_gate_ctl row (membership transitions) with game and real s, compact.
# Usage: python3 -I ctl_rows.py rows.txt (stage/petal via .get(): petal= is gone since the stage B deletion, 2026-10-15)
import sys, os, re; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import sb_time
tm = sb_time.load(sys.argv[1])
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_gate_ctl '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); f = int(d['frame'])
        print('f %6d g %8.1f r %6.1f gate %s id %-6s seq %s key %-3s ord %-4s %s>%s %-8s rank %s %s petal %s' % (f, int(d['t_ms'])/1000, tm.real(f)/1000,
              d['gate'][-4:], d['id'], d['seq'], d['key'], d['order'], d['from'], d['to'], d['why'], d['rank'], d.get('stage', '-'), d.get('petal', '-')))
