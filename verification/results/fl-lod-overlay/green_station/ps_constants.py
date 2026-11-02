"""Pixel-shader float constants of chosen draws in capture frames, side by side (registers that differ).
Usage: ps_constants.py LOG START:END:IDX[,IDX] ..."""
import sys
L = sys.argv[1]; cols = {}
for spec in sys.argv[2:]:
    a, b, idx = spec.split(':'); a, b = int(a), int(b); idx = {int(i) for i in idx.split(',')}
    cur = None; kind = None
    with open(L, 'rb') as f:
        for n, raw in enumerate(f, 1):
            if n < a: continue
            if n > b: break
            s = raw.decode('latin1').rstrip(); h = s.split(' ', 1)[0]
            if h == 'draw': cur = int(s.split('index=')[1].split()[0])
            elif h == 'constant' and cur in idx and ' kind=ps type=f ' in s:
                d = dict(t.split('=', 1) for t in s.split()[1:])
                import struct
                v = [struct.unpack('<f', bytes.fromhex(x)[::-1])[0] for x in d['bits'].split(',')]
                cols.setdefault('%d:%d' % (a, cur), {})['c' + d['reg']] = ','.join('%.4g' % x for x in v)
keys = sorted({k for c in cols.values() for k in c}, key=lambda r: int(''.join(ch for ch in r if ch.isdigit()) or 0))
names = list(cols)
print('reg', *names, sep=' | ')
for k in keys:
    vals = [cols[c].get(k, '-') for c in names]
    print(k, *vals, 'DIFF' if len(set(vals)) > 1 else '', sep=' | ')
