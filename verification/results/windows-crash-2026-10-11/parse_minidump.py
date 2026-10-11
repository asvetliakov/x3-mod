#!/usr/bin/env python3
"""Stdlib minidump query for the 2026-10-11 native Windows crash (X3Crash.dmp).
Usage: parse_minidump.py <dmp> [address_hex ...]
Prints the exception record, the faulting thread's context, the module list
entries near the addresses, the MemoryInfoList row containing each address and
the captured bytes around each address (if any memory stream covers it)."""
import struct, sys

def u32(b, o): return struct.unpack_from('<I', b, o)[0]
def u64(b, o): return struct.unpack_from('<Q', b, o)[0]

def main():
    path = sys.argv[1]
    addrs = [int(a, 16) for a in sys.argv[2:]]
    d = open(path, 'rb').read()
    assert d[:4] == b'MDMP', 'not a minidump'
    nstreams, rva = u32(d, 8), u32(d, 12)
    streams = {}
    for i in range(nstreams):
        t, size, srva = struct.unpack_from('<III', d, rva + 12 * i)
        streams.setdefault(t, []).append((size, srva))
    print('streams', sorted(streams))
    def mdstring(r):
        n = u32(d, r); return d[r + 4:r + 4 + n].decode('utf-16le', 'replace')
    mods = []
    if 4 in streams:
        _, r = streams[4][0]; n = u32(d, r)
        for i in range(n):
            o = r + 4 + 108 * i
            base, size = u64(d, o), u32(d, o + 8)
            mods.append((base, size, mdstring(u32(d, o + 20))))
    mem = []  # (start, bytes)
    if 5 in streams:
        _, r = streams[5][0]; n = u32(d, r)
        for i in range(n):
            o = r + 4 + 16 * i
            start = u64(d, o); size, mrva = u32(d, o + 8), u32(d, o + 12)
            mem.append((start, d[mrva:mrva + size]))
    if 9 in streams:
        _, r = streams[9][0]; n = u64(d, r); base_rva = u64(d, r + 8)
        cur = base_rva
        for i in range(n):
            start, size = u64(d, r + 16 + 16 * i), u64(d, r + 24 + 16 * i)
            mem.append((start, d[cur:cur + size])); cur += size
    print('modules', len(mods), 'memory_ranges', len(mem))
    ctx_by_thread = {}
    if 3 in streams:
        _, r = streams[3][0]; n = u32(d, r)
        for i in range(n):
            o = r + 4 + 48 * i
            tid = u32(d, o); csize, crva = u32(d, o + 40), u32(d, o + 44)
            ctx_by_thread[tid] = (crva, csize)
    def regs(crva):
        # x86 CONTEXT: flags@0, Dr0-7, FloatSave(112) -> segs at 0x8c
        names = ['gs','fs','es','ds','edi','esi','ebx','edx','ecx','eax','ebp','eip','cs','eflags','esp','ss']
        return {nm: u32(d, crva + 0x8c + 4 * k) for k, nm in enumerate(names)}
    if 6 in streams:
        _, r = streams[6][0]
        tid = u32(d, r); code, flags = u32(d, r + 8), u32(d, r + 12)
        addr = u64(d, r + 24); np = u32(d, r + 32)
        info = [u64(d, r + 40 + 8 * k) for k in range(np)]
        print(f'exception thread={tid} code={code:08x} flags={flags} address={addr:08x} params={[hex(x) for x in info]}')
        crva = u32(d, r + 4 + 4 + 152 + 4)  # ThreadContext location after MINIDUMP_EXCEPTION (152 bytes)
        cr = regs(crva)
        print('context', ' '.join(f'{k}={v:08x}' for k, v in cr.items() if k not in ('gs','fs','es','ds','cs','ss')))
    meminfo = []
    if 16 in streams:
        _, r = streams[16][0]
        hsize, esize, n = u32(d, r), u32(d, r + 4), u64(d, r + 8)
        for i in range(n):
            o = r + hsize + esize * i
            base, abase = u64(d, o), u64(d, o + 8)
            aprot = u32(d, o + 16); rsize = u64(d, o + 24)
            state, prot, typ = u32(d, o + 32), u32(d, o + 36), u32(d, o + 40)
            meminfo.append((base, abase, aprot, rsize, state, prot, typ))
    def find_mod(a):
        for b, s, nm in mods:
            if b <= a < b + s: return nm
        return None
    for a in addrs:
        print(f'--- {a:08x} module={find_mod(a)}')
        for base, abase, aprot, rsize, state, prot, typ in meminfo:
            if abase and (base <= a < base + rsize or abase == (a & ~0xffff)):
                print(f'  meminfo base={base:08x} alloc_base={abase:08x} alloc_protect={aprot:#x} size={rsize:#x} state={state:#x} protect={prot:#x} type={typ:#x}')
        for start, b in mem:
            if start <= a < start + len(b):
                off = a - start; lo = max(0, off - 0x30)
                print(f'  captured range {start:08x}+{len(b):#x}')
                for p in range(lo, min(len(b), off + 0x40), 16):
                    print(f'   {start+p:08x}: ' + b[p:p+16].hex(' '))
                break
        else:
            print('  not captured')
    # modules with base in the low range near the address, and anything named dvm
    for b, s, nm in mods:
        if 'dvm' in nm.lower() or 'd3d9' in nm.lower() or any(b <= a < b + s for a in addrs):
            print(f'module {b:08x}+{s:#x} {nm}')
if __name__ == '__main__':
    main()
