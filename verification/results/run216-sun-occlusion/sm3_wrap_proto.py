"""Scratch prototype of the SM3 clip-pair plan (sun-lens-sm3-body.md). Writes wrapped programs to OUT; game-derived, never commit.
usage: proto.py VS.bin PS.bin OUT [texcoord_semantic]"""
import struct, sys
def load(f):
    d = open(f, 'rb').read(); return list(struct.unpack('<%dI' % (len(d) // 4), d))
def fb(x): return struct.unpack('<I', struct.pack('<f', x))[0]
def rtype(t): return ((t >> 28) & 7) | ((t >> 8) & 0x18)
def walk(w):
    at, body, leading = 1, 0, True
    while w[at] != 0xffff:
        t = w[at]; op = t & 0xffff
        if op == 0xfffe:
            at += ((t >> 16) & 0x7fff) + 1
            if leading: body = at
            continue
        if leading: body, leading = at, False
        n = (t >> 24) & 15
        yield at, op, w[at + 1:at + 1 + n], body
        at += n + 1
def scan(w):
    used = {'r': 0, 'c': 0, 's': 0, 'o': 0, 'v': 0}; tc = 0; pos_out = None; body = None; end = None
    for at, op, p, b in walk(w):
        body = b if body is None else body
        if op == 31:
            ty, n = rtype(p[1]), p[1] & 0x7ff
            u, ui = p[0] & 0x1f, (p[0] >> 16) & 0xf
            if ty == 6: used['o'] |= 1 << n; tc |= (1 << ui) if u == 5 else 0; pos_out = n if (u == 0 and ui == 0) else pos_out
            if ty == 1: used['v'] |= 1 << n; tc |= (1 << ui) if (u == 5 and w[0] == 0xffff0300) else 0
            if ty == 10: used['s'] |= 1 << n
            continue
        for k, x in enumerate(p):
            if not x & 0x80000000: continue
            ty, n = rtype(x), x & 0x7ff
            if op == 81 and k > 0: break
            if ty == 0: used['r'] |= 1 << n
            if ty == 2: used['c'] |= 1 << n
        end = at + 1 + len(p)
    return used, tc, pos_out, body, end
def hfree(bits, limit, *skip):
    for i in range(limit - 1, -1, -1):
        if i not in skip and not bits >> i & 1: return i
vs, ps, out = load(sys.argv[1]), load(sys.argv[2]), sys.argv[3]
vu, vtc, pos, vbody, vend = scan(vs); pu, ptc, _, pbody, pend = scan(ps)
K = int(sys.argv[4]) if len(sys.argv) > 4 else hfree(vtc | ptc, 8)
N, M = hfree(vu['o'], 12), hfree(pu['v'], 10)
P = hfree(vu['r'], 32)
print('vs: pos_out o%d, used o %s, texcoord semantics %s; ps: used v %s; K=%d N=o%d M=v%d P=r%d' % (pos, bin(vu['o']), bin(vtc | ptc), bin(pu['v']), K, N, M, P))
# vs wrap
r = vs[:vbody] + [0x0200001f, 0x80000005 | (K << 16), 0xe00f0000 | N]
redirected = 0
i = vbody
while i < vend:
    t = vs[i]; op = t & 0xffff; n = (t >> 24) & 15
    r.append(t)
    for k in range(n):
        x = vs[i + 1 + k]
        if k == 0 and op == 9 and rtype(x) == 6 and (x & 0x7ff) == pos:
            x = 0x80000000 | (x & 0x00ff0000) | P; redirected += 1
        r.append(x)
    i += n + 1
r += [0x02000001, 0xe00f0000 | pos, 0x80e40000 | P, 0x02000001, 0xe00f0000 | N, 0x80e40000 | P, 0xffff]
open(out + '/vs-wrap.bin', 'wb').write(struct.pack('<%dI' % len(r), *r)); print('vs redirected', redirected, 'words', len(vs), '->', len(r))
# ps clip wrap (core_f on and off), mirror of build_pixel with SM3 input encoding
def dt(n, m): return 0x80000000 | (m << 16) | n
def st(n, s=0xe4): return 0x80000000 | (s << 16) | n
def sc(n, s=0xe4, neg=False): return 0xa0000000 | ((1 << 24) if neg else 0) | (s << 16) | n
def sv(n, s=0xe4): return 0x90000000 | (s << 16) | n
def ss(n): return 0xa0e40800 | n
def swz(x, y, z, w): return x | y << 2 | z << 4 | w << 6
dx, dy = 1.0 / 5120, 1.0 / 1440
Sm = hfree(pu['s'], 16); Ds = hfree(pu['s'], 16, Sm)
Kc = hfree(pu['c'], 224); C = hfree(pu['c'], 224, Kc); Dc = hfree(pu['c'], 224, Kc, C); Wc = hfree(pu['c'], 224, Kc, C, Dc)
O = hfree(pu['r'], 32); T = hfree(pu['r'], 32, O); Q = hfree(pu['r'], 32, O, T); S = hfree(pu['r'], 32, O, T, Q)
T2 = hfree(pu['r'], 32, O, T, Q, S); E = hfree(pu['r'], 32, O, T, Q, S, T2)
print('ps: s%d s%d c%d c%d c%d c%d r%d r%d r%d r%d r%d r%d' % (Sm, Ds, Kc, C, Dc, Wc, O, T, Q, S, T2, E))
for core_f in (True, False):
    r = ps[:pbody]
    r += [0x05000051, 0xa00f0000 | Kc, fb(.5), fb(.5), 0, fb(1 / 9), 0x0200001f, 0x90000000, 0xa00f0800 | Sm]
    r += [0x05000051, 0xa00f0000 | C, fb(.5), fb(-.5), fb(.5 + .5 * dx), fb(.5 + .5 * dy),
          0x05000051, 0xa00f0000 | Dc, fb(2 * dx), fb(dy), fb(dx), fb(2 * dy),
          0x05000051, 0xa00f0000 | Wc, fb(-2 * dx), fb(dy), fb(-dx), fb(2 * dy),
          0x0200001f, 0x90000000, 0xa00f0800 | Ds,
          0x0200001f, 0x80000005 | (K << 16), 0x900f0000 | M]
    i = pbody
    while i < pend:
        t = ps[i]; op = t & 0xffff; n = (t >> 24) & 15
        r.append(t)
        for k in range(n):
            x = ps[i + 1 + k]
            if k == 0 and op not in (31, 81) and rtype(x) == 8 and (x & 0x7ff) == 0: x = 0x80000000 | (x & 0x00ff0000) | O
            r.append(x)
        i += n + 1
    r += [0x02000001, dt(T, 15), sc(Kc), 0x03000042, dt(T, 15), st(T), ss(Sm)]
    r += [0x02000006, dt(Q, 8), sv(M, 0xff), 0x03000005, dt(Q, 15), sv(M), st(Q, 0xff),
          0x04000004, dt(Q, 15), st(Q), sc(C, swz(0, 1, 2, 2)), sc(C, swz(2, 3, 2, 2))]
    zero, ninth = sc(Kc, 0xaa), sc(Kc, 0xff)
    r += [0x03000042, dt(T2, 15), st(Q), ss(Ds), 0x04000058, dt(S, 15), st(T2, 0), zero, ninth]
    for c, s, neg in ((Dc, swz(0,1,0,1), 0), (Dc, swz(0,1,0,1), 1), (Wc, swz(0,1,0,1), 0), (Wc, swz(0,1,0,1), 1),
                      (Dc, swz(2,3,2,3), 0), (Dc, swz(2,3,2,3), 1), (Wc, swz(2,3,2,3), 0), (Wc, swz(2,3,2,3), 1)):
        r += [0x03000002, dt(E, 15), st(Q), sc(c, s, neg), 0x03000042, dt(T2, 15), st(E), ss(Ds),
              0x04000058, dt(T2, 15), st(T2, 0), zero, ninth, 0x03000002, dt(S, 15), st(S), st(T2)]
    r += [0x03000005, dt(T, 2), st(T, 0x55), st(S, 0)] if core_f else [0x02000001, dt(T, 2), st(S, 0)]
    r += [0x03000005, dt(O, 7), st(O), st(T, 0x55), 0x02000001, 0x800f0800, st(O), 0xffff]
    name = out + '/ps-clip-%s.bin' % ('coref' if core_f else 'clip')
    open(name, 'wb').write(struct.pack('<%dI' % len(r), *r)); print(name.rsplit('/', 1)[-1], 'words', len(ps), '->', len(r))
