"""Static scans of X3AP.exe (fdbf3418...) behind docs/reverse-engineering/lod-child-hide.md.
Usage: exe_flag_scan.py OBJDUMP_LISTING   (i686-w64-mingw32-objdump -d -M intel X3AP.exe > listing)
Prints: every memory write to node flags +0x12c..+0x12f whose value can carry 0x40000 (immediates,
and the register source found within 25 instructions), every reader of 0x40000 in +0x12c, every
reader of the renderable bit 0x2 in +0x12c, every test of +0x130 & 0x100000, every [reg+0x14c] access."""
import re, sys
ins = []
for l in open(sys.argv[1], errors='replace'):
    p = l.rstrip('\n').split('\t')
    if len(p) >= 3 and re.match(r'\s+[0-9a-f]+:', p[0]):
        ins.append((p[0].strip().rstrip(':'), re.sub(r'\s+', ' ', p[2].strip())))
B = r'\[e(?:ax|bx|cx|dx|si|di|bp)[^\]]*'
writes = imm40000 = 0
print('== register-sourced writes to +0x12c (source within 25 instructions)')
for i, (a, t) in enumerate(ins):
    m = re.match(r'(mov|or|xor|add|bts) (?:DWORD|WORD|BYTE) PTR %s\+0x12([c-f])\],(\S+)$' % B, t)
    if not m: continue
    writes += 1
    src = m.group(3)
    if src.startswith('0x'):
        if (int(src, 16) << (8 * (int(m.group(2), 16) - 12))) & 0x40000: imm40000 += 1; print('IMM40000', a, t)
        continue
    ctx = []
    for j in range(i - 1, max(i - 25, 0), -1):
        t2 = ins[j][1]
        if re.match(r'(mov|or|and|xor|lea|movzx|movsx|add) %s,' % src, t2) or t2 == 'pop ' + src:
            ctx.append(ins[j][0] + ' ' + t2)
            if re.match(r'(mov|lea|movzx|movsx|pop) ', t2) or t2 == 'xor %s,%s' % (src, src): break
    print(a, t, '<=', ' ; '.join(ctx))
print('writes', writes, 'immediate writes carrying 0x40000', imm40000)
def reg_tests(pattern_mem, mask_ok):
    out = []
    for i, (a, t) in enumerate(ins):
        m = re.match(r'mov (e\w\w),DWORD PTR %s%s\]' % (B, pattern_mem), t)
        if not m: continue
        r = m.group(1); r8 = {'eax': 'al', 'ebx': 'bl', 'ecx': 'cl', 'edx': 'dl'}.get(r, 'zz')
        for j in range(i + 1, min(i + 8, len(ins))):
            t2 = ins[j][1]
            mm = re.match(r'(test|and) (%s|%s),0x([0-9a-f]+)$' % (r, r8), t2)
            if mm and mask_ok(int(mm.group(3), 16)): out.append((a, t, ins[j][0], t2)); break
            if re.match(r'(mov|lea|pop|xor) %s,' % r, t2): break
    return out
print('== readers of +0x12c & 0x40000')
for a, t in ins:
    if re.search(r'PTR %s\+0x12c\],0x40000$' % B, t) or re.search(r'PTR %s\+0x12e\],0x4$' % B, t): print('direct', a, t)
for x in reg_tests(r'\+0x12c', lambda v: v == 0x40000): print('reg', *x)
print('== readers of +0x12c & 0x2')
for a, t in ins:
    if re.search(r'test (BYTE|DWORD) PTR %s\+0x12c\],0x2$' % B, t): print('direct', a, t)
for x in reg_tests(r'\+0x12c', lambda v: v == 0x2): print('reg', *x)
print('== tests of +0x130 & 0x100000')
for a, t in ins:
    if re.search(r'PTR %s\+0x130\],0x100000$' % B, t) and not t.startswith('or '): print('direct', a, t)
for x in reg_tests(r'\+0x130', lambda v: v & 0x100000 and v != 0xffffffff and not v & 0xfff00000 & ~0x100000): print('reg', *x)
print('== [reg+0x14c] accesses')
for a, t in ins:
    if re.search(r'%s\+0x14c\]' % B, t): print(a, t)
