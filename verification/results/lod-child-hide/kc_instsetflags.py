"""KC call sites of the B3D node-flag natives in addon/04.cat:L/x3story.obj (run from anywhere).
STRG decode per docs/reverse-engineering/selection-native-vm.md; a native call is `82 00 00 <u16 STRG offset>`.
Prints the bytes before each B3D_InstSetFlags call (the pushed OR masks: `07 <be32>`, `05 <byte>`, `54` = OR,
opcode meanings inferred) and every `07 00 04 00 00` (push 0x40000) within 60 bytes of a B3D flag native."""
import re, struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools' / 'analysis'))
import sector_fog_census as s, bob1
a = s.Assets(Path(bob1.DEFAULT_GAME))
d = a.read_entry(a.candidates('l/x3story.obj')[-1])
i = d.find(b'STRG'); ln = struct.unpack_from('>I', d, i + 4)[0]; src = d[i + 8:i + 8 + ln]
strg = bytearray(len(src)); strg[0] = ~src[0] & 255
for k in range(1, len(src)): strg[k] = (~src[k] - src[k - 1]) & 255
ci = d.find(b'CODE'); code = d[ci + 8:ci + 8 + struct.unpack_from('>I', d, ci + 4)[0]]
def off(name): return strg.find(b'\0' + name + b'\0') + 1
names = {off(n): n.decode() for n in (b'B3D_InstSetFlags', b'B3D_InstGetFlags', b'B3D_InstSetFlags2', b'B3D_InstGetFlags2')}
calls = {n: [m.start() for m in re.finditer(re.escape(b'\x82\x00\x00' + struct.pack('>H', o)), code)] for o, n in names.items()}
for n, v in calls.items(): print(n, 'strg_offset', off(n.encode()), 'call_sites', len(v))
for p in calls['B3D_InstSetFlags']:
    seg = code[p - 24:p]
    masks = [hex(int.from_bytes(m.group(1), 'big')) for m in re.finditer(rb'\x07(....)\x54', seg, re.S)] + \
            [hex(m.group(1)[0]) for m in re.finditer(rb'\x05(.)\x54', seg, re.S)]
    print('SetFlags call at CODE+%d' % p, 'OR masks before it', masks)
flag_calls = sorted(q for n, v in calls.items() for q in v)
near = [m.start() for m in re.finditer(rb'\x07\x00\x04\x00\x00', code) if any(abs(m.start() - q) < 60 for q in flag_calls)]
print('push 0x40000 total', len(re.findall(rb'\x07\x00\x04\x00\x00', code)), 'within 60 bytes of a B3D flag native', len(near))
