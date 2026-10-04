import sys, zlib, struct
def load(p):
    b = open(p, 'rb').read(); i = 8; idat = b''
    while i < len(b):
        n, t = struct.unpack('>I4s', b[i:i+8]); d = b[i+8:i+8+n]
        if t == b'IHDR': w, h, bd, ct = struct.unpack('>IIBB', d[:10])
        if t == b'IDAT': idat += d
        i += 12 + n
    assert bd == 8 and ct in (2, 6); c = 3 if ct == 2 else 4
    raw = zlib.decompress(idat); s = w * c; out = bytearray(); prev = bytearray(s); p = 0
    for _ in range(h):
        f = raw[p]; line = bytearray(raw[p+1:p+1+s]); p += 1 + s
        for x in range(s):
            a = line[x-c] if x >= c else 0; up = prev[x]; ul = prev[x-c] if x >= c else 0
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + up) & 255
            elif f == 3: line[x] = (line[x] + ((a + up) >> 1)) & 255
            elif f == 4:
                pa, pb, pc = abs(up - ul), abs(a - ul), abs(a + up - 2 * ul)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else up if pb <= pc else ul)) & 255
        out += line; prev = line
    return w, h, c, out
for p in sys.argv[1:]:
    w, h, c, px = load(p); n = w * h
    m = sum(0.2126 * px[i] + 0.7152 * px[i+1] + 0.0722 * px[i+2] for i in range(0, n * c, c)) / n
    print('%.3f %s' % (m, p.split('/')[-2] + '/' + p.split('/')[-1][:12]))
