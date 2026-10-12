#!/usr/bin/env python3
"""Which L/x3story.obj does addon/x3files.xml sign? (the "modified" check of 150::CheckSignatures)

The RSA-2048 public key is the 276-byte PUBLICKEYBLOB that the KC function at CODE 0x25a5 (stock AP object) builds
with one push per byte and 'mkarr 276' (element 0 = last pushed). For each signature the script recovers the signed
MD5 digest with the public key (PKCS#1 v1.5 type 1, CryptoAPI little-endian signature bytes) and compares it with the
MD5 of the candidate data: the <data> block of addon/x3files.xml (whitespace-trimmed, as 0x004cae90 cuts it) and the
decoded L/x3story.obj of every catalogue that carries one (raw, and with NUL bytes replaced by spaces as the loader
0x0049d030 does before 0x004cae90). Prints digests and match flags only; no key or file bytes.

    python3 -I verification/results/ship-navigation-ai/gate-queue/story_signature.py [X3 dir] > story_signature.txt
"""
import base64, hashlib, re, struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from kc_obj import Obj, load_member, W
sys.path.insert(0, str(Path(__file__).resolve().parents[4] / 'tools/analysis'))
from inspect_x3 import read_catalogue

X3 = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('/mnt/c/X3')
o = Obj(load_member(X3 / 'addon/04.cat'))
pc, vals = 0x25a5, []
assert o.code[pc] == 0x6e
while True:
    op = o.code[pc]
    if op == 0x28: n = struct.unpack_from('>h', o.code, pc + 1)[0]; break
    if 1 <= op <= 4: vals.append(op - 1)
    elif op == 0x05: vals.append(o.code[pc + 1])
    elif op == 0x06: vals.append(struct.unpack_from('>h', o.code, pc + 1)[0])
    elif op != 0x6e: raise ValueError(hex(op))
    pc += o.width(pc)
assert n == 276 == len(vals)
blob = bytes(v & 255 for v in reversed(vals))
assert blob[:2] == b'\x06\x02' and blob[8:12] == b'RSA1', blob[:12]
bits, e = struct.unpack_from('<II', blob, 12); nmod = int.from_bytes(blob[20:20 + bits // 8], 'little')
print('key: PUBLICKEYBLOB %d B from KC CODE 0x25a5, alg 0x%08x, %d bits, e = %d, modulus SHA-256 %s' % (
    len(blob), struct.unpack_from('<I', blob, 4)[0], bits, e, hashlib.sha256(blob[20:]).hexdigest()[:16]))
MD5_PREFIX = bytes.fromhex('3020300c06082a864886f70d020505000410')


def digest_of(sig_b64):
    s = int.from_bytes(base64.b64decode(sig_b64), 'little')
    m = pow(s, e, nmod).to_bytes(bits // 8, 'big')
    k = m.index(b'\0', 2)
    assert m[:2] == b'\0\1' and set(m[2:k]) == {0xff} and m[k + 1:k + 1 + len(MD5_PREFIX)] == MD5_PREFIX, 'not PKCS#1 MD5'
    return m[k + 1 + len(MD5_PREFIX):].hex()


for xml in (X3 / 'addon/x3files.xml',):
    t = xml.read_bytes(); print('\n%s: %d B, SHA-256 %s' % (xml.relative_to(X3), len(t), hashlib.sha256(t).hexdigest()[:16]))
    data = t[t.index(b'<data>') + 6:t.index(b'</data>')].strip(b' \t\r\n')
    sig = re.search(rb'<signature>\s*([A-Za-z0-9+/=]+)\s*</signature>', t).group(1).decode()
    d = digest_of(sig); m = hashlib.md5(data).hexdigest()
    print('  <data> block %d B: signed MD5 %s, actual MD5 %s -> %s' % (len(data), d, m, 'VERIFIES' if d == m else 'FAILS'))
    entries = re.findall(rb'<sval type="string" val="([^"]*)" />', data)
    pairs = list(zip(entries[0::2], entries[1::2]))
    print('  %d (file, signature) pairs; first: %s' % (len(pairs), pairs[0][0].decode()))
    signed = {name.decode().lower(): digest_of(s.decode()) for name, s in pairs}
    want = signed['l\\x3story.obj']; print('  L\\x3story.obj signed MD5 %s' % want)
    for d_ in (X3, X3 / 'addon'):
        for cat in sorted(d_.glob('*.cat')):
            for ent in read_catalogue(cat):
                if ent['path'].lower() == 'l/x3story.obj':
                    raw = load_member(cat); sp = raw.replace(b'\0', b' ')
                    hits = [k for k, v in (('raw', raw), ('NUL->space', sp)) if hashlib.md5(v).hexdigest() == want]
                    print('    %-14s x3story.obj %8d B build id %10d: MD5 raw %s  %s' % (
                        cat.relative_to(X3), len(raw), struct.unpack_from('>I', raw, 8)[0], hashlib.md5(raw).hexdigest(),
                        'MATCHES (%s)' % ', '.join(hits) if hits else 'no match'))
    loose = [p for p in (X3 / 'L/x3story.obj', X3 / 'addon/L/x3story.obj') if p.exists()]
    print('  loose x3story.obj present: %s' % ([str(p.relative_to(X3)) for p in loose] or 'none'))
