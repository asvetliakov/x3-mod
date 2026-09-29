import struct, sys, capstone
EXE = "/Users/asvetl/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/X3AP.exe"
data = open(EXE, 'rb').read()
pe = struct.unpack_from('<I', data, 0x3c)[0]
nsec = struct.unpack_from('<H', data, pe + 6)[0]
opt = struct.unpack_from('<H', data, pe + 20)[0]
base = struct.unpack_from('<I', data, pe + 24 + 28)[0]
secs = []
for i in range(nsec):
    o = pe + 24 + opt + i * 40
    name = data[o:o + 8].rstrip(b'\0')
    vs, va, rs, ro = struct.unpack_from('<IIII', data, o + 8)
    secs.append((name, va, vs, ro, rs))


def va2off(va):
    for n, sva, vs, ro, rs in secs:
        if base + sva <= va < base + sva + max(vs, rs):
            return ro + (va - base - sva)


text = [s for s in secs if s[0] == b'.text'][0]
tva = base + text[1]
toff = text[3]
tsz = text[4]
mode = sys.argv[1]
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
if mode == 'callers':
    target = int(sys.argv[2], 16)
    hits = []
    for i in range(tsz - 5):
        if data[toff + i] == 0xe8:
            rel = struct.unpack_from('<i', data, toff + i + 1)[0]
            if tva + i + 5 + rel == target:
                hits.append(tva + i)
    print("callers of %08x:" % target, ["%08x" % h for h in hits])
elif mode in ('calls', 'all'):
    for a in sys.argv[2:]:
        start = int(a.split(':')[0], 16)
        n = int(a.split(':')[1], 16)
        off = va2off(start)
        for ins in md.disasm(data[off:off + n], start):
            if mode == 'all' or ins.mnemonic in ('call', 'jmp', 'ret') or ins.mnemonic.startswith('j'):
                print("%08x %-24s %-6s %s" % (ins.address, ins.bytes.hex(), ins.mnemonic, ins.op_str))
elif mode == 'bytes':
    start = int(sys.argv[2], 16)
    n = int(sys.argv[3])
    print(data[va2off(start):va2off(start) + n].hex())
