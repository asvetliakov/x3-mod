"""Scan D3DX disassemblies (ps_*.bin.txt) for the colour-matrix (preshader-written c1..c3) and g_Color_* palette use.
Usage: shader_family.py DIR [DIR...]   (dirs hold disassembly from tools/analysis/disassemble_shaders.cpp)
Per ps_3_0 program with a DiffuseTexSampler: preshader inputs, which main-body instructions read the preshader-written
matrix rows, and under which bool branch the palette registers are read."""
import re, sys, os
seen = {}
for d in sys.argv[1:]:
    for n in sorted(os.listdir(d)):
        if n.startswith('ps_') and n.endswith('.txt'):
            seen.setdefault(n[3:19], []).append(os.path.join(d, n))
for h, paths in sorted(seen.items()):
    text = open(paths[0], encoding='latin1').read().replace('\r', '')
    if '    ps_3_0' not in text or 'DiffuseTexSampler' not in text: continue
    head, body = text.split('    ps_3_0', 1)
    pre = head.split('    preshader', 1)
    pre_inputs = sorted(set(re.findall(r'//   (g_\w+)\s+c\d+', pre[0]))) if len(pre) > 1 else []
    pre_out = sorted(set(re.findall(r'^\s+\w+\s+(c\d+)\.', pre[1], re.M))) if len(pre) > 1 else []
    regs = dict((r, nm) for nm, r in re.findall(r'//   (\w+)\s+([cb]\d+)\s+\d', head.split('    preshader')[-1]))
    mix = [r for r, nm in regs.items() if nm == 'g_bColor_Mixing']
    pal = {r for r, nm in regs.items() if nm.startswith('g_Color_') and r.startswith('c')}
    stack = []; mat = []; palr = []
    for ln in body.split('\n'):
        s = ln.strip()
        if not s or s.startswith('//') or s.startswith('def') or s.startswith('dcl'): continue
        op = s.split()[0]
        if op == 'if': stack.append(s.split()[1]); continue
        if op == 'else': stack[-1] = '!' + stack[-1]; continue
        if op == 'endif': stack.pop(); continue
        srcs = s.split(',')[1:] if ',' in s else []
        cs = set(re.findall(r'\b(c\d+)', ','.join(srcs)))
        m = cs & {c for c in pre_out if c in ('c1', 'c2', 'c3')}
        if m: mat.append('%s[%s]' % (op, '/'.join(stack) or 'top'))
        p = cs & pal
        if p: palr.append('%s[%s]' % ('+'.join(regs[x].replace('g_Color_', '') for x in sorted(p)), '/'.join(stack) or 'top'))
    print(h[:8], 'eds=' + ','.join(sorted({p.split('/')[-2] for p in paths})),
          'pre_in=' + ','.join(x.replace('g_', '') for x in pre_inputs) if pre_inputs else 'pre_in=-',
          'pre_out=' + ','.join(pre_out) if pre_out else 'pre_out=-',
          'mix=' + (','.join(mix) or '-'), 'mat=' + (';'.join(mat) or '-'), 'pal=' + (';'.join(palr) or '-'))
