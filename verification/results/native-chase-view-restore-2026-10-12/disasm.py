# usage: python3 -I disasm.py <exe> <start_hex> <end_hex>  - linear x86 disassembly of an image range
import sys, struct, capstone
exe, start, end = sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3], 16)
d = open(exe, 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
nsec = struct.unpack_from('<H', d, pe + 6)[0]; opt = struct.unpack_from('<H', d, pe + 20)[0]
base = struct.unpack_from('<I', d, pe + 24 + 28)[0]
for i in range(nsec):
    o = pe + 24 + opt + 40 * i
    vs, va, rs, ro = struct.unpack_from('<IIII', d, o + 8)
    if base + va <= start < base + va + max(vs, rs):
        off = ro + start - base - va
        md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        for ins in md.disasm(d[off:off + end - start], start):
            print(f"{ins.address:08x} {ins.mnemonic} {ins.op_str}")
        break
