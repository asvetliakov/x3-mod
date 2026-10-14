# Run164 Q5: capital 0x12 episodes (cap_orders.txt segments) with game and real s and the cause (evict_fresh.txt / e_evict.txt).
# Usage: python3 -I x12_real.py rows.txt
import sys, os, re; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import sb_time
tm = sb_time.load(sys.argv[1]); D = os.path.dirname(os.path.abspath(__file__)); cur = None; tg = tr = 0.0
for l in open(os.path.join(D, 'cap_orders.txt')):
    if l.startswith('=='): cur = l.split()[1:3]
    m = re.match(r'\s+0x12\s+(\d+)-(\d+) game (\S+) dur (\S+)', l)
    if m:
        f0, f1 = int(m[1]), int(m[2]); g = (tm.game(f1) - tm.game(f0)) / 1000; r = (tm.real(f1) - tm.real(f0)) / 1000; tg += g; tr += r
        print('%s %s f %d-%d game %.1f +%.1f real %.1f +%.1f' % (cur[0], cur[1], f0, f1, tm.game(f0) / 1000, g, tm.real(f0) / 1000, r))
print('total game %.1f real %.1f' % (tg, tr))
