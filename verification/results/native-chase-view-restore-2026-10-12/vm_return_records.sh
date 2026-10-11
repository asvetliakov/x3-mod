#!/bin/sh
# Static witnesses for the manager capture (2026-10-12): the interpreter's call opcodes push a tag-3 return
# offset and a tag-10 saved caller context on the task's cell stack; the return opcode scans for that pair;
# 0x0049f430 seeds the bottom pair (3:0, 10:0); 0x0049f1e0 sends a non-negative key to the static record
# table 0x004b06f0 ([rec+0] == key). Set X= to the installed EXE.
X=${X:-/mnt/c/X3/X3AP.exe}; D=$(dirname "$0")
dis() { python3 -I $D/disasm.py $X $1 $2; }
echo "## opcode table 0x004a4688 -> 0x004a4490 for 0x82..0x88"
python3 -I - $X <<'EOF'
import struct, sys
d = open(sys.argv[1], 'rb').read(); pe = struct.unpack_from('<I', d, 0x3c)[0]
n = struct.unpack_from('<H', d, pe + 6)[0]; opt = struct.unpack_from('<H', d, pe + 20)[0]; base = struct.unpack_from('<I', d, pe + 52)[0]
secs = [struct.unpack_from('<IIII', d, pe + 24 + opt + 40 * i + 8) for i in range(n)]
def off(a):
    for vs, va, rs, ro in secs:
        if base + va <= a < base + va + max(vs, rs): return ro + a - base - va
for op in range(0x82, 0x89):
    idx = d[off(0x004a4688 + op - 1)]; print(hex(op), idx, hex(struct.unpack_from('<I', d, off(0x004a4490 + 4 * idx))[0]))
EOF
echo "## 0x86 class-static call: resolve the pushed key, push return (tag 3) + caller context (tag 10), set task+0x3c"
dis 0x4a3cc4 0x4a3d5e | grep -E '4a3cc[4-9]|4a3cd1|4a3d1[4-9a-f]|4a3d2[0-9a-f]|4a3d3[0-9a-f]|4a3d54'
echo "## 0x88 static self call: pushes the same pair with the current context, task+0x3c unchanged"
dis 0x4a3ed4 0x4a3f1b | grep -E '4a3ee[0-9a-f]|4a3ef[0-9a-f]|4a3f00'
echo "## 0x85 dynamic call: the same pair (0x004a3c85 / 0x004a3c97), new context at 0x004a3cbc"
dis 0x4a3c74 0x4a3cc4 | grep -E '4a3c8[0-9a-f]|4a3c9[0-9a-f]|4a3cbc'
echo "## return-record writers"
dis 0x4a8620 0x4a866d
echo "## return opcode 0x83: scan for tag 10 above tag 3; offset 0 = task end; null or class-less context skipped"
dis 0x4a39a6 0x4a3a09
echo "## native dispatch writes the top index task+0x18 (0x004a38d9)"
dis 0x4a38c0 0x4a38e4
echo "## 0x0049f430 bottom pair, 0x0049f1e0 key resolution, 0x004b06f0 static record search"
dis 0x49f430 0x49f47e
dis 0x49f1e0 0x49f226
dis 0x4b06f0 0x4b0731
