"""Pixel-shader bool constants b0/b1 (g_bIsDecalMap / g_bColor_Mixing in PS b2d8f40c) per draw in a capture range.
Usage: ps_bools.py LOG START END [PS_PREFIX]  -> one row per draw: index ps b0 b1"""
import sys
L, a, b = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]); pref = sys.argv[4] if len(sys.argv) > 4 else ''
cur = ps = None; bools = {}
def flush():
    if cur is not None and ps.startswith(pref):
        print(cur, ps[:8], 'b0=%s' % bools.get('0', '-'), 'b1=%s' % bools.get('1', '-'))
with open(L, 'rb') as f:
    for n, raw in enumerate(f, 1):
        if n < a: continue
        if n > b: break
        s = raw.decode('latin1').rstrip(); h = s.split(' ', 1)[0]
        if h == 'draw':
            flush(); bools = {}
            d = dict(t.split('=', 1) for t in s.split()[1:] if '=' in t)
            cur, ps = int(d['index']), d.get('ps', '')
        elif h == 'constant' and ' kind=ps type=b ' in s:
            d = dict(t.split('=', 1) for t in s.split()[1:]); bools[d['reg']] = d['values']
flush()
