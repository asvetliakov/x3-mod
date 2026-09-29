#!/usr/bin/env python3
"""Static EXE checks behind docs/reverse-engineering/font-rendering.md (sections 2-3).

Reads X3AP.exe read-only and writes only derived facts (addresses, byte patterns of hook sites,
the decoded text-style table, the list of cfg+0x784 readers) to font_static_checks.json.
Needs capstone.

  python3 verification/results/font-rendering/font_static_checks.py [game_root]
"""
import hashlib, json, struct, sys
from pathlib import Path

import capstone

GAME = Path(sys.argv[1]) if len(sys.argv) > 1 else \
    Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'
exe = (GAME / 'X3AP.exe').read_bytes()
pe = struct.unpack_from('<I', exe, 0x3c)[0]
nsec = struct.unpack_from('<H', exe, pe + 6)[0]
optsz = struct.unpack_from('<H', exe, pe + 20)[0]
base = struct.unpack_from('<I', exe, pe + 24 + 28)[0]
secs = []
for i in range(nsec):
    o = pe + 24 + optsz + 40 * i
    vs, va, rs, ro = struct.unpack_from('<IIII', exe, o + 8)
    secs.append((exe[o:o + 8].rstrip(b'\0').decode(), va + base, max(vs, rs), ro, rs))


def off(va):
    for _, sva, size, ro, rs in secs:
        if sva <= va < sva + size:
            return va - sva + ro
    raise ValueError(hex(va))


def cstr(va):
    o = off(va)
    return exe[o:exe.index(b'\0', o)].decode('latin1')


md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)


def insns(start, end):
    return [(i.address, i.bytes.hex(), f'{i.mnemonic} {i.op_str}'.strip())
            for i in md.disasm(exe[off(start):off(end)], start)]


out = {'exe_sha256': hashlib.sha256(exe).hexdigest()}
out['strings'] = {hex(a): cstr(a) for a in (0x565478, 0x565480, 0x565484, 0x56548c, 0x565490, 0x5611c0,
                                            0x55b4c8, 0x563cfc)}
out['font_path_format'] = cstr(struct.unpack_from('<I', exe, off(0x57c008 + 0x3c))[0])

# Text-style table 0x005748b8: 11 records x 0x44: int id, short count, short dx[10], dy[10], pass[10]
style = []
for k in range(11):
    o = off(0x5748b8 + 0x44 * k)
    sid, cnt = struct.unpack_from('<ih', exe, o)
    rows = [list(struct.unpack_from('<10h', exe, o + 6 + 20 * r))[:cnt] for r in range(3)]
    style.append({'flag': hex(sid), 'count': cnt, 'dx': rows[0], 'dy': rows[1], 'foreground': rows[2]})
out['style_table_0x005748b8'] = style

# Hook-site byte patterns and the instructions they cover
sites = {
    'font_open_entry_0x0048cdc0': (0x48cdc0, 0x48cdd4),
    'style_factor_0x004f812d': (0x4f812d, 0x4f813f),
    'textline_scale_0x0048b45d': (0x48b45d, 0x48b476),
    'bltblock_leaf_0x0048c419': (0x48c419, 0x48c41e),
    'bltblock_leaf_0x0048c445': (0x48c445, 0x48c44a),
    'bltblockalpha_leaf_0x0048c7e5': (0x48c7e5, 0x48c7ea),
    'native_tahoma_0x0041c992': (0x41c992, 0x41c9b0),
    'native_tahoma_0x0041f799': (0x41f799, 0x41f7ac),
    'fontscale_parse_0x004ecff8': (0x4ecff8, 0x4ecffe),
}
out['sites'] = {k: [f'{a:#010x} {b} {t}' for a, b, t in insns(*v)] for k, v in sites.items()}

# Every instruction that addresses [reg+0x784] in .text (byte search for the disp32 0x784, then a
# one-instruction decode at the two possible starts), with whether the config pointer *0x00606f34
# is loaded (bytes 34 6f 60 00) within the 64 bytes before it.
text = [s for s in secs if s[0] == '.text'][0]
t0, tlen = text[3], text[4]
blob = exe[t0:t0 + tlen]
hits = []
p = blob.find(b'\x84\x07\x00\x00')
while p >= 0:
    for back in (2, 3):
        va = text[1] + p - back
        dec = list(md.disasm(blob[p - back:p - back + 8], va, count=1))
        if dec and dec[0].size >= back + 4 and '0x784]' in dec[0].op_str:
            hits.append({'addr': hex(va), 'insn': f'{dec[0].mnemonic} {dec[0].op_str}',
                         'cfg_load_before': b'\x34\x6f\x60\x00' in blob[max(0, p - 64):p - back]})
            break
    p = blob.find(b'\x84\x07\x00\x00', p + 1)
out['reg_plus_0x784'] = {'count': len(hits), 'with_cfg_load_in_64_bytes': sum(h['cfg_load_before'] for h in hits),
                         'sites': hits}
# Direct callers of the font open 0x0048cdc0 (E8 rel32)
callers = []
p = blob.find(b'\xe8')
while p >= 0:
    if p + 5 <= len(blob):
        va = text[1] + p
        if (va + 5 + struct.unpack_from('<i', blob, p + 1)[0]) & 0xffffffff == 0x48cdc0:
            callers.append(hex(va))
    p = blob.find(b'\xe8', p + 1)
out['callers_0x0048cdc0'] = callers

dst = Path(__file__).with_name('font_static_checks.json')
dst.write_text(json.dumps(out, indent=1) + '\n')
print('wrote', dst, '| +0x784 sites', out['reg_plus_0x784']['count'], 'cfg-loaded',
      out['reg_plus_0x784']['with_cfg_load_in_64_bytes'], '| callers of 0x0048cdc0', callers)
