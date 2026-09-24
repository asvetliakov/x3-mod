"""Count KC pushes of 0x80000000 / 0x7fffffff in addon/04.cat:L/x3story.obj and those within 60 bytes
of a B3D_Inst{Get,Set}Flags native call (method as ../lod-child-hide/kc_instsetflags.py)."""
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
names = [b'B3D_InstSetFlags', b'B3D_InstGetFlags']
flag_calls = sorted(m.start() for n in names for m in re.finditer(re.escape(b'\x82\x00\x00' + struct.pack('>H', off(n))), code))
print('B3D_Inst{Set,Get}Flags call sites', len(flag_calls))
for pat, label in ((b'\x07\x80\x00\x00\x00', '0x80000000'), (b'\x07\x7f\xff\xff\xff', '0x7fffffff')):
    hits = [m.start() for m in re.finditer(re.escape(pat), code)]
    near = [h for h in hits if any(abs(h - q) < 60 for q in flag_calls)]
    print('push', label, 'total', len(hits), 'within 60 bytes of a B3D flag native', len(near))
